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
MAX_PHASE_EVENTS = 1_000_000
MAX_PHASE_EVENT_BYTES = 4096

_OPAQUE_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
_COLOR = re.compile(r"^#[0-9A-F]{6}$")
_SOURCES = frozenset({"agent", "timeout"})
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
    if not isinstance(value, dict) or set(value) != _FIELDS:
        raise V2PhaseEventJournalError()
    for name in ("sequence", "turn", "phase", "place"):
        item = value[name]
        minimum = 1 if name in {"sequence", "place"} else 0
        if type(item) is not int or not minimum <= item <= (1 << 63) - 1:
            raise V2PhaseEventJournalError()
    if expected_sequence is not None and value["sequence"] != expected_sequence:
        raise V2PhaseEventJournalError()
    if value["source"] not in _SOURCES:
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
    if _canonical(clean) != data:
        raise V2PhaseEventJournalError()
    return clean


class V2PhaseEventJournal:
    """One append-only, mode-0600, fsync-backed event stream per episode."""

    def __init__(self, episode_root: str | os.PathLike[str]):
        self._lock = threading.RLock()
        self._closed = False
        self._fd = -1
        self._events: list[dict[str, Any]] = []
        self._identities: dict[tuple[int, int, int], dict[str, Any]] = {}

        root_flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0)
        root_flags |= getattr(os, "O_DIRECTORY", 0)
        root_flags |= getattr(os, "O_NOFOLLOW", 0)
        try:
            root_fd = os.open(Path(episode_root), root_flags)
            flags = os.O_RDWR | os.O_APPEND | os.O_CREAT
            flags |= getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0)
            self._fd = os.open(PHASE_EVENT_FILENAME, flags, 0o600, dir_fd=root_fd)
            metadata = os.fstat(self._fd)
            if not stat.S_ISREG(metadata.st_mode) or metadata.st_nlink != 1:
                raise OSError(errno.EPERM, "unsafe phase event journal")
            os.fchmod(self._fd, 0o600)
            os.fsync(root_fd)
            os.fsync(self._fd)
        except (OSError, TypeError, ValueError):
            if self._fd >= 0:
                os.close(self._fd)
                self._fd = -1
            raise V2PhaseEventJournalError() from None
        finally:
            if "root_fd" in locals():
                os.close(root_fd)
        try:
            self._load()
        except Exception:
            self.close()
            raise

    def _load(self) -> None:
        with self._lock:
            try:
                size = os.fstat(self._fd).st_size
                data = os.pread(self._fd, size, 0)
            except OSError:
                raise V2PhaseEventJournalError() from None
            if len(data) != size or data and not data.endswith(b"\n"):
                raise V2PhaseEventJournalError()
            lines = data.splitlines()
            if len(lines) > MAX_PHASE_EVENTS:
                raise V2PhaseEventJournalError()
            for sequence, line in enumerate(lines, 1):
                event = _strict_line(line, sequence)
                identity = (event["turn"], event["phase"], event["place"])
                if identity in self._identities:
                    raise V2PhaseEventJournalError()
                if self._events and (
                    event["turn"], event["phase"]
                ) <= (
                    self._events[-1]["turn"], self._events[-1]["phase"]
                ):
                    raise V2PhaseEventJournalError()
                self._events.append(event)
                self._identities[identity] = event

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

    def append(self, event: dict[str, Any]) -> dict[str, Any]:
        with self._lock:
            if self._closed or self._fd < 0 or len(self._events) >= MAX_PHASE_EVENTS:
                raise V2PhaseEventJournalError()
            candidate = dict(event)
            candidate["sequence"] = len(self._events) + 1
            clean = validate_phase_event(candidate, expected_sequence=len(self._events) + 1)
            identity = (clean["turn"], clean["phase"], clean["place"])
            existing = self._identities.get(identity)
            if existing is not None:
                comparable = dict(clean)
                comparable["sequence"] = existing["sequence"]
                if comparable != existing:
                    raise V2PhaseEventJournalError()
                return copy.deepcopy(existing)
            if self._events and (
                clean["turn"], clean["phase"]
            ) <= (
                self._events[-1]["turn"], self._events[-1]["phase"]
            ):
                raise V2PhaseEventJournalError()
            encoded = _canonical(clean) + b"\n"
            try:
                written = os.write(self._fd, encoded)
                if written != len(encoded):
                    raise OSError(errno.EIO, "short phase event write")
                os.fsync(self._fd)
            except OSError:
                raise V2PhaseEventJournalError() from None
            self._events.append(clean)
            self._identities[identity] = clean
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
