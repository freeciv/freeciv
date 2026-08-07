"""Durable public-safe phase-end events for ``full-control-v2`` games."""

from __future__ import annotations

import copy
import errno
import json
import math
import os
import re
import stat
import threading
from pathlib import Path
from typing import Any


PHASE_EVENT_FILENAME = "phase-events.jsonl"
PHASE_EVENT_QUARANTINE_FILENAME = "phase-events.quarantine.jsonl"
MAX_PHASE_EVENTS = 1_000_000
MAX_PHASE_EVENT_BYTES = 4096
# The whole journal is read into memory once at load.  ``MAX_PHASE_EVENTS``
# alone would allow a four-gigabyte read, so bound the bytes too: 64 MiB holds
# far more phase events than any real game emits, and anything past it is
# treated as a damaged tail rather than as a reason to refuse to start.
MAX_PHASE_EVENT_JOURNAL_BYTES = 64 * 1024 * 1024
# How much of a discarded tail is preserved for forensics.
MAX_QUARANTINE_BYTES = 1024 * 1024

_OPAQUE_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
_COLOR = re.compile(r"^#[0-9A-F]{6}$")
# Who ended the phase.  ``agent`` is the controller's own call, ``timeout`` is
# the action deadline being enforced against it, and ``auto_idle`` is a phase
# that was ended for a seat which had provably nothing left to decide and had
# gone quiet.  The three are kept apart here because a replay, a forensic read
# and a scorer all need to know which of them a turn ended by: a timeout is a
# controller failing to act, and an auto-idle end is emphatically not.
PHASE_END_SOURCES = frozenset({"agent", "timeout", "auto_idle"})
_RECEIPT_STATES = frozenset({"applied", "ambiguous", "rejected"})
_RESOLUTIONS = frozenset({"advanced", "terminal", "failed"})
_CONTROLLER_TYPES = frozenset({"external", "native"})
_FIELDS = frozenset({
    "sequence",
    "turn",
    "phase",
    "place",
    "seat_id",
    "player_name",
    "player_color",
    "controller_label",
    "controller_type",
    "source",
    "receipt_state",
    "resolution",
    "deadline_started_at",
    "ended_at",
    "elapsed_s",
})
# A rolled-back game replays turns it has already journaled.  The replayed
# phase is a different event from the one it replaces -- different wall clock,
# different outcome -- so it needs its own identity rather than colliding with
# history a spectator already read.  ``incarnation`` supplies that identity:
# 0 is the original run, and every autosave rollback bumps it.  It is written
# to the wire only when non-zero, so a journal from a game that never rolled
# back is byte-identical to what previous versions produced and still loads.
_OPTIONAL_FIELDS = frozenset({"incarnation"})
MAX_INCARNATION = (1 << 31) - 1


class V2PhaseEventJournalError(RuntimeError):
    """A stable sanitized journal failure safe for evaluation state."""

    def __init__(self) -> None:
        super().__init__("the full-control-v2 phase event journal is unavailable")


def _canonical(value: Any) -> bytes:
    try:
        encoded = json.dumps(
            value,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
            allow_nan=False,
        ).encode("utf-8")
    except (TypeError, ValueError, UnicodeError):
        raise V2PhaseEventJournalError() from None
    if not encoded or len(encoded) > MAX_PHASE_EVENT_BYTES:
        raise V2PhaseEventJournalError()
    return encoded


def _safe_text(value: Any, *, opaque: bool = False) -> str:
    if (
        not isinstance(value, str)
        or not value
        or len(value) > 160
        or any(ord(character) < 0x20 for character in value)
        or opaque and _OPAQUE_ID.fullmatch(value) is None
    ):
        raise V2PhaseEventJournalError()
    return value


def validate_phase_event(value: Any, *, expected_sequence: int | None = None) -> dict[str, Any]:
    """Return a closed-schema public event or fail without leaking input."""
    if not isinstance(value, dict) or set(value) - _OPTIONAL_FIELDS != _FIELDS:
        raise V2PhaseEventJournalError()
    incarnation = value.get("incarnation", 0)
    if type(incarnation) is not int or not 0 <= incarnation <= MAX_INCARNATION:
        raise V2PhaseEventJournalError()
    for name in ("sequence", "turn", "phase", "place"):
        item = value[name]
        minimum = 1 if name in {"sequence", "place"} else 0
        if type(item) is not int or not minimum <= item <= (1 << 63) - 1:
            raise V2PhaseEventJournalError()
    if expected_sequence is not None and value["sequence"] != expected_sequence:
        raise V2PhaseEventJournalError()
    if value["source"] not in PHASE_END_SOURCES:
        raise V2PhaseEventJournalError()
    if value["receipt_state"] not in _RECEIPT_STATES:
        raise V2PhaseEventJournalError()
    if value["resolution"] not in _RESOLUTIONS:
        raise V2PhaseEventJournalError()
    if value["receipt_state"] == "rejected" and value["resolution"] != "failed":
        raise V2PhaseEventJournalError()
    if value["controller_type"] not in _CONTROLLER_TYPES:
        raise V2PhaseEventJournalError()
    if not isinstance(value["player_color"], str) or _COLOR.fullmatch(
        value["player_color"],
    ) is None:
        raise V2PhaseEventJournalError()
    for name in ("deadline_started_at", "ended_at", "elapsed_s"):
        item = value[name]
        if (
            isinstance(item, bool)
            or not isinstance(item, (int, float))
            or not math.isfinite(item)
            or item < 0
        ):
            raise V2PhaseEventJournalError()
    if value["ended_at"] < value["deadline_started_at"]:
        raise V2PhaseEventJournalError()
    return {
        "sequence": value["sequence"],
        "incarnation": incarnation,
        "turn": value["turn"],
        "phase": value["phase"],
        "place": value["place"],
        "seat_id": _safe_text(value["seat_id"], opaque=True),
        "player_name": _safe_text(value["player_name"]),
        "player_color": value["player_color"],
        "controller_label": _safe_text(value["controller_label"]),
        "controller_type": value["controller_type"],
        "source": value["source"],
        "receipt_state": value["receipt_state"],
        "resolution": value["resolution"],
        "deadline_started_at": float(value["deadline_started_at"]),
        "ended_at": float(value["ended_at"]),
        "elapsed_s": round(float(value["elapsed_s"]), 3),
    }


def _wire(event: dict[str, Any]) -> dict[str, Any]:
    """Return the on-disk form: incarnation 0 is implied, never written.

    Keeping the original run's records field-for-field identical to what
    earlier versions wrote means adding rollback support does not quarantine
    the history of any game that never rolled back.
    """
    if event.get("incarnation"):
        return event
    wire = dict(event)
    wire.pop("incarnation", None)
    return wire


def _strict_line(data: bytes, sequence: int) -> dict[str, Any]:
    if not data or len(data) > MAX_PHASE_EVENT_BYTES:
        raise V2PhaseEventJournalError()

    def pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, item in items:
            if key in result:
                raise ValueError("duplicate key")
            result[key] = item
        return result

    try:
        value = json.loads(
            data.decode("utf-8"),
            object_pairs_hook=pairs,
            parse_constant=lambda _value: (_ for _ in ()).throw(ValueError()),
        )
    except (UnicodeError, ValueError, TypeError, json.JSONDecodeError):
        raise V2PhaseEventJournalError() from None
    clean = validate_phase_event(value, expected_sequence=sequence)
    if _canonical(_wire(clean)) != data:
        raise V2PhaseEventJournalError()
    return clean


class V2PhaseEventJournal:
    """One append-only, mode-0600, fsync-backed event stream per episode.

    Loading repairs rather than refuses.  This journal is a public phase-event
    stream, not the durable command-receipt contract: a torn final append or a
    damaged tail is a reason to keep the longest valid prefix and move the rest
    aside, never a reason to stop a game from starting.  Nothing downstream can
    replay a command because of it.
    """

    def __init__(self, episode_root: str | os.PathLike[str]):
        self._lock = threading.RLock()
        self._closed = False
        self._fd = -1
        self._events: list[dict[str, Any]] = []
        self._identities: dict[tuple[int, int, int, int], dict[str, Any]] = {}
        self._quarantined_bytes = 0
        # The incarnation every subsequent append is stamped with, and the
        # last (turn, phase) seen *within* it.  Ordering is only meaningful
        # inside one incarnation: a rollback deliberately moves the game back.
        self._incarnation = 0
        self._last_key: tuple[int, int] | None = None

        root_flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0)
        root_flags |= getattr(os, "O_DIRECTORY", 0)
        root_flags |= getattr(os, "O_NOFOLLOW", 0)
        try:
            root_fd = os.open(Path(episode_root), root_flags)
        except (OSError, TypeError, ValueError):
            raise V2PhaseEventJournalError() from None
        try:
            flags = os.O_RDWR | os.O_APPEND | os.O_CREAT
            flags |= getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0)
            self._fd = os.open(PHASE_EVENT_FILENAME, flags, 0o600, dir_fd=root_fd)
            metadata = os.fstat(self._fd)
            if not stat.S_ISREG(metadata.st_mode) or metadata.st_nlink != 1:
                raise OSError(errno.EPERM, "unsafe phase event journal")
            os.fchmod(self._fd, 0o600)
            os.fsync(root_fd)
            os.fsync(self._fd)
            self._load(root_fd)
        except (OSError, TypeError, ValueError):
            self.close()
            raise V2PhaseEventJournalError() from None
        except Exception:
            self.close()
            raise
        finally:
            os.close(root_fd)

    @property
    def quarantined_bytes(self) -> int:
        """Bytes of damaged journal tail this load moved aside, if any."""
        with self._lock:
            return self._quarantined_bytes

    def _load(self, root_fd: int) -> None:
        with self._lock:
            try:
                size = os.fstat(self._fd).st_size
                readable = min(size, MAX_PHASE_EVENT_JOURNAL_BYTES)
                data = os.pread(self._fd, readable, 0) if readable else b""
            except OSError:
                raise V2PhaseEventJournalError() from None
            if len(data) != readable:
                raise V2PhaseEventJournalError()
            # Everything after the last newline is an incomplete append: a
            # crash between `write` and its completion, which is exactly the
            # fault that used to make a restart impossible.
            lines = data.split(b"\n")
            lines.pop()
            kept = 0
            for sequence, line in enumerate(lines, 1):
                if sequence > MAX_PHASE_EVENTS:
                    break
                try:
                    event = _strict_line(line, sequence)
                    incarnation = event["incarnation"]
                    identity = (
                        incarnation, event["turn"], event["phase"],
                        event["place"],
                    )
                    if identity in self._identities:
                        raise V2PhaseEventJournalError()
                    if incarnation < self._incarnation:
                        raise V2PhaseEventJournalError()
                    if incarnation > self._incarnation:
                        self._incarnation = incarnation
                        self._last_key = None
                    key = (event["turn"], event["phase"])
                    if self._last_key is not None and key <= self._last_key:
                        raise V2PhaseEventJournalError()
                except V2PhaseEventJournalError:
                    # This record and everything after it is unusable: the
                    # stream is sequence-contiguous, so no later record can be
                    # kept without renumbering history a spectator already read.
                    break
                self._events.append(event)
                self._identities[identity] = event
                self._last_key = key
                kept += len(line) + 1
            if kept != size:
                self._quarantine_tail(root_fd, kept, size)

    def _quarantine_tail(self, root_fd: int, kept: int, size: int) -> None:
        """Move a damaged tail aside and truncate to the valid prefix.

        Best effort by construction: if the tail cannot be preserved or the
        file cannot be truncated, the journal still opens against the prefix it
        validated.  The alternative — refusing to construct — takes down the
        whole game for a diagnostic stream.
        """
        self._quarantined_bytes = max(0, size - kept)
        flags = os.O_WRONLY | os.O_CREAT | os.O_APPEND
        flags |= getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0)
        quarantine_fd = -1
        try:
            tail = os.pread(self._fd, min(size - kept, MAX_QUARANTINE_BYTES), kept)
            if tail:
                quarantine_fd = os.open(
                    PHASE_EVENT_QUARANTINE_FILENAME, flags, 0o600, dir_fd=root_fd,
                )
                os.fchmod(quarantine_fd, 0o600)
                if not tail.endswith(b"\n"):
                    tail += b"\n"
                os.write(quarantine_fd, tail)
                os.fsync(quarantine_fd)
        except OSError:
            pass
        finally:
            if quarantine_fd >= 0:
                try:
                    os.close(quarantine_fd)
                except OSError:
                    pass
        try:
            os.ftruncate(self._fd, kept)
            os.fsync(self._fd)
            os.fsync(root_fd)
        except OSError:
            pass

    def close(self) -> None:
        with self._lock:
            if self._closed:
                return
            self._closed = True
            fd = self._fd
            self._fd = -1
            if fd >= 0:
                try:
                    os.close(fd)
                except OSError:
                    pass

    @property
    def incarnation(self) -> int:
        """Which run of the game subsequent appends belong to."""
        with self._lock:
            return self._incarnation

    def begin_incarnation(self) -> int:
        """Start a new incarnation because the game was rolled back.

        Appends nothing and discards nothing: every record already written
        stays exactly as a spectator read it.  What changes is that the turns
        about to be replayed are journaled under a fresh identity, so the
        replay is a visible discontinuity rather than a contradiction that
        takes the game down.
        """
        with self._lock:
            if self._closed or self._fd < 0:
                raise V2PhaseEventJournalError()
            if self._incarnation >= MAX_INCARNATION:
                raise V2PhaseEventJournalError()
            self._incarnation += 1
            self._last_key = None
            return self._incarnation

    def append(self, event: dict[str, Any]) -> dict[str, Any]:
        with self._lock:
            if self._closed or self._fd < 0 or len(self._events) >= MAX_PHASE_EVENTS:
                raise V2PhaseEventJournalError()
            candidate = dict(event)
            candidate["sequence"] = len(self._events) + 1
            # The journal, not the caller, owns which run a record belongs to.
            candidate["incarnation"] = self._incarnation
            clean = validate_phase_event(candidate, expected_sequence=len(self._events) + 1)
            identity = (
                clean["incarnation"], clean["turn"], clean["phase"],
                clean["place"],
            )
            existing = self._identities.get(identity)
            if existing is not None:
                comparable = dict(clean)
                comparable["sequence"] = existing["sequence"]
                if comparable != existing:
                    raise V2PhaseEventJournalError()
                return copy.deepcopy(existing)
            if self._last_key is not None and (
                clean["turn"], clean["phase"]
            ) <= self._last_key:
                raise V2PhaseEventJournalError()
            encoded = _canonical(_wire(clean)) + b"\n"
            try:
                written = os.write(self._fd, encoded)
                if written != len(encoded):
                    raise OSError(errno.EIO, "short phase event write")
                os.fsync(self._fd)
            except OSError:
                raise V2PhaseEventJournalError() from None
            self._events.append(clean)
            self._identities[identity] = clean
            self._last_key = (clean["turn"], clean["phase"])
            return copy.deepcopy(clean)

    def page(self, after_sequence: int, limit: int) -> dict[str, Any]:
        if (
            type(after_sequence) is not int
            or not 0 <= after_sequence <= (1 << 63) - 1
            or type(limit) is not int
            or not 1 <= limit <= 250
        ):
            raise V2PhaseEventJournalError()
        with self._lock:
            if self._closed:
                raise V2PhaseEventJournalError()
            # Sequence is contiguous and one-based, so the cursor is also the
            # zero-based slice offset. Never scan or copy unbounded history on
            # this authentication-free spectator route.
            start = min(after_sequence, len(self._events))
            end = min(start + limit, len(self._events))
            items = self._events[start:end]
            return {
                "items": copy.deepcopy(items),
                "next_after_sequence": (
                    items[-1]["sequence"] if items else after_sequence
                ),
                "has_more": end < len(self._events),
            }

    def last_for_place(self, place: int) -> dict[str, Any] | None:
        if type(place) is not int or place < 1:
            raise V2PhaseEventJournalError()
        with self._lock:
            if self._closed:
                raise V2PhaseEventJournalError()
            for event in reversed(self._events):
                if event["place"] == place:
                    return copy.deepcopy(event)
            return None

    def __enter__(self) -> "V2PhaseEventJournal":
        return self

    def __exit__(self, _type: Any, _value: Any, _traceback: Any) -> None:
        self.close()

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass
