"""Wedge detection and bounded rollback recovery for ``full-control-v2``.

A native seat can stop answering without any component reporting itself
unhealthy: the Freeciv server keeps running, the sidecar process keeps its
seat, the IPC stream stays synchronized, and yet every catalog read and every
dispatch fails, because the observation the boundary keeps producing is one
the projector refuses.  Nothing in that loop is transient, so retrying is
pointless and the seat silently burns its phase deadline while its health
still says ``ready``.

This module owns the two decisions that turn that state into a recoverable
one: when consecutive boundary failures have proven the seat wedged rather
than merely unlucky, and how far back the game may be rewound to escape it.
The supervisor owns the mechanism; everything here is pure policy plus one
durable, bounded, public-safe journal so benchmark scoring can tell a clean
game from a recovered one.
"""

from __future__ import annotations

import errno
import json
import os
import re
import stat
import threading
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


RECOVERY_DIRECTORY = "v2-recovery"
RECOVERY_FILENAME = "events.jsonl"
MAX_RECOVERY_BYTES = 256 * 1024
MAX_RECOVERY_RECORD_BYTES = 1024

# Three consecutive boundary failures separate a wedge from a race.  Two is
# reachable by an ordinary revision change landing between a read and its
# retry; three in a row, with no successful read in between, has never been
# observed for any cause other than a boundary that cannot recover itself.
WEDGE_FAILURE_THRESHOLD = 3
# A post-result observation that came back unavailable is already one half of
# the evidence, so the next failing read is allowed to trip the wedge.
WEDGE_AMBIGUITY_CREDIT = WEDGE_FAILURE_THRESHOLD - 1

# Two attempts per turn: one re-attach, then one rollback.  A third would only
# repeat the rollback against the same save.
MAX_RECOVERY_ATTEMPTS_PER_TURN = 2
# Four per game bounds the worst case where several turns each wedge once.
MAX_RECOVERY_ATTEMPTS_PER_GAME = 4

RECOVERY_KINDS = frozenset({"sidecar_reattach", "autosave_rollback"})
RECOVERY_OUTCOMES = frozenset({"recovered", "failed", "abandoned"})
RECOVERY_TRIGGERS = frozenset({
    "boundary_internal_error",
    "post_result_observation_unavailable",
    # A sidecar that died or stopped answering is already a proven fault; it
    # enters recovery directly rather than through the failure counter.
    "sidecar_exit",
})

_FORMAT = "freeciv-full-control-v2-recovery"
_FORMAT_VERSION = 1
_OPAQUE_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
_TIMESTAMP = re.compile(
    r"^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}"
    r"\.[0-9]{3}Z$"
)
_SAVE_NAME = re.compile(r"^turn-([0-9]{4,6})-auto\.sav(?:\.gz|\.bz2|\.xz|\.zst)?$")
_MAX_SAVE_CANDIDATES = 4096
_FIELDS = frozenset({
    "attempt",
    "client_state",
    "exit_code",
    "exit_signal",
    "format",
    "game_id",
    "kind",
    "outcome",
    "place",
    "recovered_to_turn",
    "rewound_applied_actions",
    "schema_version",
    "seat_id",
    "sidecar_generation",
    "timestamp",
    "trigger",
    "turn",
})


class V2RecoveryError(RuntimeError):
    """A sanitized recovery-journal error with no filesystem detail."""

    def __init__(self) -> None:
        super().__init__("the full-control-v2 recovery journal is unavailable")


def _opaque(value: Any) -> str:
    if not isinstance(value, str) or _OPAQUE_ID.fullmatch(value) is None:
        raise V2RecoveryError()
    return value


def _bounded_int(value: Any, *, minimum: int, maximum: int) -> int:
    if type(value) is not int or not minimum <= value <= maximum:
        raise V2RecoveryError()
    return value


class WedgeDetector:
    """Count consecutive native-boundary failures for one game's seats.

    Only failures that reached the boundary and produced an unattributable
    internal error are counted.  Every successful boundary read clears the
    seat, so a wedge is always proven by an uninterrupted run.
    """

    def __init__(self, *, threshold: int = WEDGE_FAILURE_THRESHOLD):
        if type(threshold) is not int or threshold < 1:
            raise ValueError("threshold must be a positive integer")
        self.threshold = threshold
        self._failures: dict[int, int] = {}

    def note_success(self, place: int) -> None:
        """Clear a seat: the boundary answered, so it is not wedged."""
        self._failures.pop(place, None)

    def note_failure(self, place: int) -> bool:
        """Count one boundary failure; return true once the seat is wedged."""
        count = self._failures.get(place, 0) + 1
        self._failures[place] = count
        return count >= self.threshold

    def note_ambiguous_observation(self, place: int) -> None:
        """Credit an ambiguous post-result read without tripping on its own.

        An ambiguous terminal receipt is correct behaviour by itself, so it
        must never be sufficient evidence of a wedge.  It does mean the next
        failing read is the second half of a proof rather than a first
        symptom, so the seat starts one short of the threshold.
        """
        self._failures[place] = max(
            self._failures.get(place, 0), min(WEDGE_AMBIGUITY_CREDIT, self.threshold - 1),
        )

    def failures(self, place: int) -> int:
        return self._failures.get(place, 0)

    def clear(self, place: int) -> None:
        self._failures.pop(place, None)


class RecoveryBudget:
    """Bound how often one game may rewind itself.

    Recovery rewinds real play, so an unbounded retry loop would let a
    permanently broken boundary consume a whole game's turns while reporting
    progress.  Both caps are deliberately small: a game that needs more than
    this is not recovering, it is failing.
    """

    def __init__(
        self,
        *,
        per_turn: int = MAX_RECOVERY_ATTEMPTS_PER_TURN,
        per_game: int = MAX_RECOVERY_ATTEMPTS_PER_GAME,
    ):
        self.per_turn = per_turn
        self.per_game = per_game
        # Keyed by (turn, place): the escalation ladder describes ONE seat's
        # evidence.  Sharing it across seats would let a second seat's first
        # ever fault skip the free re-attach and rewind the whole game, and a
        # third seat get no recovery at all.
        self._by_turn: dict[tuple[int, int], int] = {}
        self._total = 0

    @property
    def total(self) -> int:
        return self._total

    def attempts_for_turn(self, turn: int, place: int) -> int:
        return self._by_turn.get((turn, place), 0)

    def next_attempt(self, turn: int, place: int) -> int | None:
        """Reserve the next attempt for ``place`` in ``turn``, or ``None``.

        The returned attempt number is 1-based within the seat's turn and
        selects the escalation tier: attempt 1 re-attaches the boundary,
        attempt 2 rewinds to an autosave.
        """
        if self._total >= self.per_game:
            return None
        used = self._by_turn.get((turn, place), 0)
        if used >= self.per_turn:
            return None
        attempt = used + 1
        self._by_turn[(turn, place)] = attempt
        self._total += 1
        return attempt

    def release(self, turn: int, place: int, *, kind: str, outcome: str) -> bool:
        """Give back an attempt that cost the game nothing.

        The per-game cap exists to bound how much real play a game may discard,
        so only what actually discards play may consume it.  A successful
        ``sidecar_reattach`` rewinds no turns and drops no applied action -- it
        replaces a client against the same live server -- and an attempt that
        was abandoned before it ran did not even do that.  Charging those
        identically to a rollback is what let four successful recoveries kill
        a game on its fifth latency blip.
        """
        if outcome == "recovered" and kind != "sidecar_reattach":
            return False
        if outcome not in {"recovered", "abandoned"}:
            return False
        used = self._by_turn.get((turn, place), 0)
        if used <= 0 or self._total <= 0:
            return False
        if used <= 1:
            self._by_turn.pop((turn, place), None)
        else:
            self._by_turn[(turn, place)] = used - 1
        self._total -= 1
        return True

    def exhausted_reason(self, turn: int, place: int) -> str:
        if self._total >= self.per_game:
            return (
                "the full-control-v2 native boundary wedged and the game's "
                f"recovery budget of {self.per_game} attempts is exhausted"
            )
        return (
            "the full-control-v2 native boundary wedged and turn "
            f"{turn} already used its {self.per_turn} recovery attempts"
        )


def recovery_kind_for_attempt(attempt: int) -> str:
    """Escalate from re-attaching the boundary to rewinding the game.

    A boundary that is broken only in the client is fixed by a new sidecar
    generation against the same live server, which costs nothing and rewinds
    nothing.  Only when that has already been tried is it worth discarding
    real play to escape the state that produced the wedge.
    """
    return "sidecar_reattach" if attempt <= 1 else "autosave_rollback"


def _save_is_readable(path: Path) -> bool:
    try:
        with open(path, "rb") as handle:
            metadata = os.fstat(handle.fileno())
            if not stat.S_ISREG(metadata.st_mode) or metadata.st_size <= 0:
                return False
            return len(handle.read(16)) > 0
    except OSError:
        return False


def select_rollback_save(
    saves_directory: str | os.PathLike[str], *, at_or_before_turn: int,
) -> tuple[Path, int] | None:
    """Return the newest readable per-turn autosave not after ``turn``.

    Freeciv writes ``turn-%04T-auto.sav.gz`` at the start of each turn, so the
    save named for the current turn holds the state before any of this turn's
    actions were applied.  That is the closest point to which the game can be
    rewound while still discarding whatever produced the wedge.
    """
    if type(at_or_before_turn) is not int or at_or_before_turn < 1:
        return None
    try:
        entries = sorted(os.listdir(saves_directory))[:_MAX_SAVE_CANDIDATES]
    except OSError:
        return None
    root = Path(saves_directory)
    candidates: list[tuple[int, Path]] = []
    for name in entries:
        match = _SAVE_NAME.fullmatch(name)
        if match is None:
            continue
        turn = int(match.group(1))
        if turn < 1 or turn > at_or_before_turn:
            continue
        candidates.append((turn, root / name))
    for turn, path in sorted(candidates, reverse=True):
        if _save_is_readable(path):
            return path, turn
    return None


def _write_all_once(fd: int, data: bytes) -> None:
    """Append one bounded record in one synchronized system call."""
    written = os.write(fd, data)
    if written != len(data):
        raise OSError(errno.EIO, "short recovery journal write")


class V2RecoveryJournal:
    """One mode-0600, fsync'd, size-bounded JSONL rollback log per episode."""

    def __init__(self, episode_root: str | os.PathLike[str], *, game_id: str):
        self.game_id = _opaque(game_id)
        self._lock = threading.RLock()
        self._closed = False
        self._dir_fd = -1

        flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0)
        flags |= getattr(os, "O_DIRECTORY", 0)
        flags |= getattr(os, "O_NOFOLLOW", 0)
        try:
            root_fd = os.open(Path(episode_root), flags)
        except (OSError, TypeError, ValueError):
            raise V2RecoveryError() from None
        try:
            try:
                os.mkdir(RECOVERY_DIRECTORY, 0o700, dir_fd=root_fd)
            except FileExistsError:
                pass
            self._dir_fd = os.open(RECOVERY_DIRECTORY, flags, dir_fd=root_fd)
            metadata = os.fstat(self._dir_fd)
            if not stat.S_ISDIR(metadata.st_mode):
                raise OSError(errno.ENOTDIR, RECOVERY_DIRECTORY)
            os.fchmod(self._dir_fd, 0o700)
            os.fsync(root_fd)
            os.fsync(self._dir_fd)
        except OSError:
            if self._dir_fd >= 0:
                os.close(self._dir_fd)
                self._dir_fd = -1
            raise V2RecoveryError() from None
        finally:
            os.close(root_fd)

    def close(self) -> None:
        with self._lock:
            if self._closed:
                return
            self._closed = True
            fd = self._dir_fd
            self._dir_fd = -1
            if fd >= 0:
                try:
                    os.close(fd)
                except OSError:
                    pass

    def __enter__(self) -> "V2RecoveryJournal":
        return self

    def __exit__(self, _type: Any, _value: Any, _traceback: Any) -> None:
        self.close()

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass

    def record(
        self,
        *,
        place: int,
        seat_id: str,
        turn: int,
        attempt: int,
        kind: str,
        trigger: str,
        outcome: str,
        sidecar_generation: int,
        recovered_to_turn: int | None,
        rewound_applied_actions: bool,
        exit_code: int | None = None,
        exit_signal: int | None = None,
        client_state: str | None = None,
        timestamp: str | None = None,
    ) -> dict[str, Any]:
        if (
            kind not in RECOVERY_KINDS
            or outcome not in RECOVERY_OUTCOMES
            or trigger not in RECOVERY_TRIGGERS
            or type(rewound_applied_actions) is not bool
        ):
            raise V2RecoveryError()
        if recovered_to_turn is not None:
            _bounded_int(recovered_to_turn, minimum=1, maximum=(1 << 31) - 1)
        # Exit forensics are best-effort: a sidecar that was never reaped has
        # no code, and one that stopped answering without dying has neither.
        # They are recorded as null rather than omitted so the schema stays
        # closed and a silent death is visibly distinct from a clean one.
        if exit_code is not None:
            _bounded_int(exit_code, minimum=-255, maximum=255)
        if exit_signal is not None:
            _bounded_int(exit_signal, minimum=1, maximum=64)
        if client_state is not None and (
            not isinstance(client_state, str)
            or _OPAQUE_ID.fullmatch(client_state) is None
        ):
            raise V2RecoveryError()
        if timestamp is None:
            timestamp = datetime.now(timezone.utc).isoformat(
                timespec="milliseconds",
            ).replace("+00:00", "Z")
        if (
            not isinstance(timestamp, str)
            or _TIMESTAMP.fullmatch(timestamp) is None
        ):
            raise V2RecoveryError()
        record = {
            "attempt": _bounded_int(attempt, minimum=1, maximum=1024),
            "client_state": client_state,
            "exit_code": exit_code,
            "exit_signal": exit_signal,
            "format": _FORMAT,
            "game_id": self.game_id,
            "kind": kind,
            "outcome": outcome,
            "place": _bounded_int(place, minimum=1, maximum=1024),
            "recovered_to_turn": recovered_to_turn,
            "rewound_applied_actions": rewound_applied_actions,
            "schema_version": _FORMAT_VERSION,
            "seat_id": _opaque(seat_id),
            "sidecar_generation": _bounded_int(
                sidecar_generation, minimum=1, maximum=(1 << 31) - 1,
            ),
            "timestamp": timestamp,
            "trigger": trigger,
            "turn": _bounded_int(turn, minimum=1, maximum=(1 << 31) - 1),
        }
        if set(record) != _FIELDS:
            raise V2RecoveryError()
        try:
            encoded = json.dumps(
                record,
                ensure_ascii=True,
                sort_keys=True,
                separators=(",", ":"),
                allow_nan=False,
            ).encode("ascii") + b"\n"
        except (TypeError, ValueError, UnicodeError):
            raise V2RecoveryError() from None
        if len(encoded) > MAX_RECOVERY_RECORD_BYTES:
            raise V2RecoveryError()

        with self._lock:
            if self._closed or self._dir_fd < 0:
                raise V2RecoveryError()
            try:
                self._append_locked(encoded)
            except OSError:
                raise V2RecoveryError() from None
        return record

    def _append_locked(self, encoded: bytes) -> None:
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_APPEND
        flags |= getattr(os, "O_CLOEXEC", 0)
        flags |= getattr(os, "O_NOFOLLOW", 0)
        created = False
        try:
            fd = os.open(RECOVERY_FILENAME, flags, 0o600, dir_fd=self._dir_fd)
            created = True
        except FileExistsError:
            flags &= ~(os.O_CREAT | os.O_EXCL)
            fd = os.open(RECOVERY_FILENAME, flags, dir_fd=self._dir_fd)
        try:
            if created:
                os.fchmod(fd, 0o600)
            metadata = os.fstat(fd)
            if (
                not stat.S_ISREG(metadata.st_mode)
                or stat.S_IMODE(metadata.st_mode) != 0o600
                or metadata.st_size < 0
                or metadata.st_size + len(encoded) > MAX_RECOVERY_BYTES
            ):
                raise OSError(errno.EPERM, "unsafe recovery journal")
            _write_all_once(fd, encoded)
            os.fsync(fd)
        finally:
            os.close(fd)
        os.fsync(self._dir_fd)


__all__ = [
    "MAX_RECOVERY_ATTEMPTS_PER_GAME",
    "MAX_RECOVERY_ATTEMPTS_PER_TURN",
    "MAX_RECOVERY_BYTES",
    "MAX_RECOVERY_RECORD_BYTES",
    "RECOVERY_DIRECTORY",
    "RECOVERY_FILENAME",
    "RECOVERY_KINDS",
    "RECOVERY_OUTCOMES",
    "RECOVERY_TRIGGERS",
    "WEDGE_FAILURE_THRESHOLD",
    "RecoveryBudget",
    "V2RecoveryError",
    "V2RecoveryJournal",
    "WedgeDetector",
    "recovery_kind_for_attempt",
    "select_rollback_save",
]
