"""Private bridge callback journal creation and terminal validation."""

from __future__ import annotations

import json
import os
import re
from pathlib import Path
from typing import Any, Iterable


BRIDGE_STATUS_FILE = "bridge-status.jsonl"
TURN_HEADER_RE = re.compile(r"^turn ([1-9][0-9]*) -?[0-9]+ .+$")


def create_bridge_journal(episode: Path) -> Path:
    """Create an empty owner-only journal before Lua starts appending."""
    path = episode / BRIDGE_STATUS_FILE
    descriptor = os.open(
        path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600,
    )
    os.close(descriptor)
    return path


def validate_bridge_journal(
    path: Path, timeline_turns: Iterable[int], scorelog_path: Path,
) -> list[str]:
    """Return stable invalid-reason strings for any lifecycle discrepancy."""
    reasons: list[str] = []

    def add(reason: str) -> None:
        if reason not in reasons:
            reasons.append(reason)

    events: list[dict[str, Any]] = []
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError:
        lines = []
        add("bridge_status_missing")
    except UnicodeError:
        lines = []
        add("bridge_status_malformed")
    for line_number, line in enumerate(lines, 1):
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            add(f"bridge_status_malformed:line={line_number}")
            continue
        if not isinstance(event, dict):
            add(f"bridge_status_malformed:line={line_number}")
            continue
        kind = event.get("event")
        expected_keys = (
            {"event", "turn", "message"}
            if kind == "error" else {"event", "turn"}
        )
        turn = event.get("turn")
        if (
            kind not in {"begin", "ok", "error"}
            or set(event) != expected_keys
            or isinstance(turn, bool)
            or not isinstance(turn, int)
            or turn < 1
            or (
                kind == "error"
                and (
                    not isinstance(event.get("message"), str)
                    or not event["message"]
                )
            )
        ):
            add(f"bridge_status_malformed:line={line_number}")
            continue
        events.append(event)

    begun_turns: list[int] = []
    ok_turns: list[int] = []
    active_turn: int | None = None
    for event in events:
        kind = event["event"]
        turn = event["turn"]
        if kind == "begin":
            if active_turn is not None:
                add(f"bridge_callback_unresolved:turn={active_turn}")
            active_turn = turn
            begun_turns.append(turn)
            if turn != len(begun_turns):
                add("bridge_turn_sequence_invalid")
            continue
        if active_turn != turn:
            add(f"bridge_status_order_invalid:turn={turn}")
        if kind == "ok":
            ok_turns.append(turn)
        else:
            add(f"bridge_callback_error:turn={turn}:{event['message']}")
        active_turn = None
    if active_turn is not None:
        add(f"bridge_callback_unresolved:turn={active_turn}")

    timeline = list(timeline_turns)
    if ok_turns != timeline:
        add("bridge_status_timeline_mismatch")
    if not ok_turns:
        add("bridge_no_turns")

    score_turns: list[int] = []
    try:
        score_lines = scorelog_path.read_text(encoding="utf-8").splitlines()
    except OSError:
        score_lines = []
        add("bridge_scorelog_missing")
    except UnicodeError:
        score_lines = []
        add("bridge_scorelog_malformed")
    if score_lines and not score_lines[0].startswith("#FREECIV SCORELOG2 "):
        add("bridge_scorelog_malformed")
    for line in score_lines:
        if not line.startswith("turn"):
            continue
        header = TURN_HEADER_RE.match(line)
        if header is None:
            add("bridge_scorelog_malformed")
            continue
        score_turns.append(int(header.group(1)))
    if not score_turns:
        add("bridge_scorelog_turns_missing")
    elif score_turns != list(range(1, max(score_turns) + 1)):
        add("bridge_scorelog_turns_malformed")
    else:
        expected = list(range(1, max(score_turns)))
        if begun_turns != expected:
            add("bridge_scorelog_begin_mismatch")
        if ok_turns != expected:
            add("bridge_scorelog_ok_mismatch")
        if timeline != expected:
            add("bridge_scorelog_timeline_mismatch")
    return reasons
