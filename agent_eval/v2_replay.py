"""Live replay telemetry for ``full-control-v2`` games, rebuilt from autosaves.

A ``strategic-v1`` server loads ``bridge.lua``, which appends one snapshot row
per turn to ``replay.jsonl``.  A ``full-control-v2`` server never loads that
bridge, so nothing ever wrote that journal for a v2 game and the live viewer
waited forever.  This producer reconstructs the same rows from the per-turn
autosaves a v2 game already writes and appends them to the same journal, so the
live replay endpoint, the archived episode, and the viewer stay unchanged.

Rows carry the strategic-v1 field set with one documented gap the save format
cannot answer: ``research.cost`` and catalog ``cost_base`` are always ``0``
(see :mod:`agent_eval.save_replay`).  Reconstruction is incremental: an
autosave is parsed once, a partially written autosave blocks only itself and is
retried, and the journal is only ever appended to.
"""

from __future__ import annotations

import json
import os
import stat
import threading
from pathlib import Path
from typing import Any, Callable, Mapping

from .save_replay import SAVE_NAME_RE, replay_from_autosaves


REPLAY_SCHEMA_VERSION = 1
# One refresh converts a bounded window of new turns.  A live viewer polls
# again immediately, so a long backlog drains over several polls instead of
# stalling one HTTP request.
TURNS_PER_REFRESH = 12
# How many refreshes an unreadable autosave may block the journal before it is
# skipped with a warning.  A save that is still being written becomes readable
# within a poll or two; a truly corrupt one must not stall the feed forever.
MAX_UNREADABLE_ATTEMPTS = 5
MAX_CONSECUTIVE_FAILURES = 10
JOURNAL_TAIL_BYTES = 256 * 1024
MAX_ROW_PLAYERS = 512


class _JournalUnusable(Exception):
    """The journal holds rows this producer cannot account for."""


def _integer(value: Any, default: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        return default
    return value


def _text(value: Any) -> str:
    return value if isinstance(value, str) else ""


def _row(
    snapshot: Mapping[str, Any],
    game_id: str,
    seat_ids: Mapping[int, str],
) -> dict[str, Any]:
    """Return one strategic-v1 shaped replay row for a reconstructed turn."""
    turn = _integer(snapshot.get("turn"), -1)
    year = _integer(snapshot.get("year"))
    raw_players = snapshot.get("players")
    if turn < 0 or not isinstance(raw_players, list):
        raise ValueError("reconstructed snapshot is malformed")
    players = []
    for raw in sorted(
        raw_players[:MAX_ROW_PLAYERS],
        key=lambda player: _integer(player.get("player_id"), -1),
    ):
        player_id = _integer(raw.get("player_id"), -1)
        if player_id < 0:
            raise ValueError("reconstructed snapshot is malformed")
        research = raw.get("research")
        if not isinstance(research, Mapping):
            research = {}
        tech_id = research.get("tech_id")
        citizens = _integer(raw.get("citizens"))
        players.append({
            "seat_id": seat_ids.get(player_id, f"dynamic-player-{player_id}"),
            "player_id": player_id,
            "player_name": _text(raw.get("player_name")),
            "turn": turn,
            "year": year,
            "nation": _text(raw.get("nation")),
            "government": _text(raw.get("government")),
            "alive": raw.get("alive") is True,
            "score": _integer(raw.get("score")),
            "cities": _integer(raw.get("cities")),
            "citizens": citizens,
            "population": citizens,
            "units": _integer(raw.get("units")),
            "gold": _integer(raw.get("gold")),
            "culture": _integer(raw.get("culture")),
            "known_tech_ids": [
                _integer(value, -1) for value in raw.get("known_tech_ids", ())
                if _integer(value, -1) >= 0
            ],
            "research": {
                "tech_id": None if tech_id is None else _integer(tech_id, -1),
                "name": _text(research.get("name")),
                "bulbs": _integer(research.get("bulbs")),
                "cost": _integer(research.get("cost")),
            },
            "future_techs": _integer(raw.get("future_techs")),
        })
    return {
        "schema_version": REPLAY_SCHEMA_VERSION,
        "game_id": game_id,
        "turn": turn,
        "year": year,
        # v2-only provenance.  Every consumer of the shared schema ignores
        # unknown row fields; this one keeps a raw journal self-describing.
        "source": "autosave",
        "players": players,
    }


class V2ReplayProducer:
    """Append reconstructed replay rows for one live full-control-v2 game.

    ``refresh`` is cheap when nothing changed and is meant to be called from
    the replay read path and once more when the game finalizes.  It never
    raises: replay telemetry is a spectator surface and must not be able to
    fail a match.
    """

    def __init__(
        self,
        runs_root: str | os.PathLike[str],
        game_id: str,
        episode: str | os.PathLike[str],
        *,
        seat_ids: Callable[[], Mapping[int, str]] | None = None,
        cache_root: str | os.PathLike[str] | None = None,
        turns_per_refresh: int = TURNS_PER_REFRESH,
        replay_loader: Callable[..., Mapping[str, Any]] = replay_from_autosaves,
    ):
        self.runs_root = Path(runs_root)
        self.game_id = game_id
        self.episode = Path(episode)
        self.replay_path = self.episode / "replay.jsonl"
        self.catalog_path = self.episode / "replay-catalog.json"
        self.warnings_path = self.episode / "replay-warnings.jsonl"
        self.saves_path = self.episode / "saves"
        self.disabled = False
        self._seat_ids = seat_ids
        self._cache_root = cache_root
        self._turns_per_refresh = max(1, min(250, turns_per_refresh))
        self._replay_loader = replay_loader
        self._lock = threading.RLock()
        self._last_turn: int | None = None
        self._sources_signature: tuple[Any, ...] | None = None
        self._blocked_turn: int | None = None
        self._blocked_attempts = 0
        self._more_pending = False
        self._rescan_requested = False
        self._catalog_written = False
        self._failures = 0

    def refresh(self) -> int:
        """Convert newly appeared autosaves and return the rows appended."""
        with self._lock:
            if self.disabled:
                return 0
            try:
                appended = self._refresh_locked()
            except _JournalUnusable:
                # Another writer owns this journal.  Stay inert rather than
                # interleave rows with it.
                self.disabled = True
                return 0
            except Exception:
                self._failures += 1
                if self._failures >= MAX_CONSECUTIVE_FAILURES:
                    self.disabled = True
                return 0
            self._failures = 0
            return appended

    def drain(self, *, max_refreshes: int = 64) -> int:
        """Convert every pending autosave, bounded, for game finalization."""
        total = 0
        for _ in range(max(1, max_refreshes)):
            appended = self.refresh()
            total += appended
            with self._lock:
                stalled = (
                    self._blocked_turn is not None or self._rescan_requested
                )
            if appended <= 0 and not stalled:
                # Nothing converted and no turn waiting on a retry: the
                # journal is as complete as the autosaves on disk allow.
                break
        return total

    def _refresh_locked(self) -> int:
        signature = self._sources_signature_now()
        if not signature:
            # No autosave exists yet.  Touch nothing, not even the reader's
            # cache directory, until this game has produced one.
            return 0
        if (
            signature == self._sources_signature
            and self._blocked_turn is None
            and not self._more_pending
            and not self._rescan_requested
        ):
            return 0
        self._rescan_requested = False
        if self._last_turn is None:
            self._last_turn = self._journal_last_turn()

        response = self._replay_loader(
            self.runs_root,
            self.game_id,
            (),
            after_turn=self._last_turn,
            limit=self._turns_per_refresh,
            cache_root=self._cache_root,
            complete=False,
        )
        if not isinstance(response, Mapping):
            raise ValueError("replay reconstruction returned no response")

        # Rows are only ever appended for a contiguous run of readable turns:
        # an autosave still being written blocks itself, not the turns after
        # it, and is retried on the next refresh.
        blocked = self._blocked_turn_from(response)
        pending = sorted(
            (
                snapshot for snapshot in response.get("snapshots") or ()
                if isinstance(snapshot, Mapping)
                and _integer(snapshot.get("turn"), -1) > self._last_turn
                and (blocked is None or _integer(snapshot.get("turn"), -1) < blocked)
            ),
            key=lambda snapshot: _integer(snapshot.get("turn"), -1),
        )
        self._write_catalog(response.get("catalog"))
        appended = self._append(pending)
        # A refresh converts a bounded window; remember when the reader said
        # more turns are already on disk so the next call keeps draining.
        self._more_pending = response.get("has_more") is True
        self._track_blocked_turn(blocked, progressed=appended > 0)
        if (
            self._blocked_turn is not None
            and self._blocked_attempts >= MAX_UNREADABLE_ATTEMPTS
        ):
            # The turn is not merely half-written.  Record it the way the v1
            # bridge records a failed capture and move the frontier past it so
            # later turns can still reach the viewer.
            self._append_warning(self._blocked_turn)
            self._last_turn = self._blocked_turn
            self._blocked_turn = None
            self._blocked_attempts = 0
            self._rescan_requested = True
            self._sources_signature = signature
            return appended
        self._sources_signature = signature
        return appended

    def _sources_signature_now(self) -> tuple[Any, ...]:
        entries: list[tuple[str, int, int]] = []
        try:
            with os.scandir(self.saves_path) as scan:
                for entry in scan:
                    if not SAVE_NAME_RE.fullmatch(entry.name):
                        continue
                    try:
                        info = entry.stat(follow_symlinks=False)
                    except OSError:
                        continue
                    if not stat.S_ISREG(info.st_mode):
                        continue
                    entries.append((entry.name, info.st_size, info.st_mtime_ns))
        except OSError:
            return ()
        return tuple(sorted(entries))

    def _journal_last_turn(self) -> int:
        """Return the highest turn already present in the journal."""
        try:
            size = self.replay_path.stat().st_size
        except OSError:
            return 0
        if size <= 0:
            return 0
        try:
            with self.replay_path.open("rb") as stream:
                if size > JOURNAL_TAIL_BYTES:
                    stream.seek(size - JOURNAL_TAIL_BYTES)
                data = stream.read(JOURNAL_TAIL_BYTES + 1)
        except OSError as exc:
            raise _JournalUnusable() from exc
        for line in reversed(data.split(b"\n")):
            if not line.strip():
                continue
            try:
                row = json.loads(line.decode("utf-8"))
            except (UnicodeError, json.JSONDecodeError):
                continue
            turn = _integer(row.get("turn"), -1) if isinstance(row, dict) else -1
            if turn >= 0:
                return turn
        raise _JournalUnusable()

    def _blocked_turn_from(self, response: Mapping[str, Any]) -> int | None:
        """Return the first unconverted turn whose autosave could not be read.

        Warnings about turns already in the journal are ignored: their rows
        were written from a save that was readable at the time, and the
        frontier must never move backwards.
        """
        frontier = self._last_turn or 0
        blocked = [
            turn for warning in response.get("replay_warnings") or ()
            if isinstance(warning, Mapping)
            and (turn := _integer(warning.get("turn"), -1)) > frontier
        ]
        return min(blocked) if blocked else None

    def _track_blocked_turn(self, blocked: int | None, *, progressed: bool) -> None:
        """Count only the refreshes an unreadable turn made no progress on."""
        if blocked is None:
            self._blocked_turn = None
            self._blocked_attempts = 0
            return
        if blocked != self._blocked_turn:
            self._blocked_turn = blocked
            self._blocked_attempts = 0 if progressed else 1
        elif progressed:
            self._blocked_attempts = 0
        else:
            self._blocked_attempts += 1

    def _append_warning(self, turn: int) -> None:
        """Record one unreadable turn the same way the v1 bridge does."""
        try:
            with self.warnings_path.open("a", encoding="utf-8") as stream:
                stream.write(json.dumps({
                    "turn": turn, "message": "replay capture unavailable",
                }, ensure_ascii=False, separators=(",", ":")) + "\n")
                stream.flush()
        except OSError:
            pass

    def _write_catalog(self, catalog: Any) -> None:
        if self._catalog_written or not isinstance(catalog, Mapping):
            return
        encoded = json.dumps(
            catalog, ensure_ascii=False, separators=(",", ":"),
        ) + "\n"
        try:
            if self.catalog_path.read_text(encoding="utf-8") == encoded:
                self._catalog_written = True
                return
        except (OSError, UnicodeError):
            pass
        temporary = self.catalog_path.with_name(self.catalog_path.name + ".tmp")
        try:
            temporary.write_text(encoded, encoding="utf-8")
            os.replace(temporary, self.catalog_path)
        except OSError:
            try:
                temporary.unlink()
            except OSError:
                pass
            return
        self._catalog_written = True

    def _append(self, snapshots: list[Mapping[str, Any]]) -> int:
        if not snapshots:
            return 0
        seat_ids = self._current_seat_ids()
        rows = [_row(snapshot, self.game_id, seat_ids) for snapshot in snapshots]
        payload = "".join(
            json.dumps(row, ensure_ascii=False, separators=(",", ":")) + "\n"
            for row in rows
        )
        with self.replay_path.open("a", encoding="utf-8") as stream:
            stream.write(payload)
            stream.flush()
        self._last_turn = rows[-1]["turn"]
        return len(rows)

    def _current_seat_ids(self) -> dict[int, str]:
        if self._seat_ids is None:
            return {}
        try:
            resolved = self._seat_ids()
        except Exception:
            return {}
        if not isinstance(resolved, Mapping):
            return {}
        return {
            number: seat_id for number, seat_id in resolved.items()
            if isinstance(number, int) and not isinstance(number, bool)
            and number >= 0 and isinstance(seat_id, str) and seat_id
        }


__all__ = [
    "MAX_UNREADABLE_ATTEMPTS", "TURNS_PER_REFRESH", "V2ReplayProducer",
]
