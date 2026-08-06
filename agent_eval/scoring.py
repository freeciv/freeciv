"""Freeciv SCORELOG2 parsing and episode summaries."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Iterable

from .config import controller_fingerprint


class ScorelogError(ValueError):
    pass


def parse_scorelog(path: str | Path) -> dict[str, Any]:
    score_path = Path(path)
    tags: dict[int, str] = {}
    player_records: list[dict[str, Any]] = []
    active_players: dict[int, dict[str, Any]] = {}
    turns: set[int] = set()
    try:
        lines = score_path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise ScorelogError(f"cannot read {score_path}: {exc}") from exc
    if not lines or not lines[0].startswith("#FREECIV SCORELOG2 "):
        raise ScorelogError("not a Freeciv SCORELOG2 file")
    for line in lines:
        if not line or line.startswith("#"):
            continue
        parts = line.split(" ", 3)
        command = parts[0]
        try:
            if command == "tag":
                tags[int(parts[1])] = parts[2]
            elif command == "turn":
                turns.add(int(parts[1]))
            elif command == "addplayer":
                added_turn = int(parts[1])
                player_id = int(parts[2])
                if player_id in active_players:
                    raise ScorelogError(
                        f"player slot {player_id} was added twice"
                    )
                record = {
                    "player_id": player_id,
                    "name": parts[3],
                    "added_turn": added_turn,
                    "removed_turn": None,
                    "values": {},
                }
                player_records.append(record)
                active_players[player_id] = record
            elif command == "delplayer":
                removed_turn = int(parts[1])
                player_id = int(parts[2])
                record = active_players.pop(player_id, None)
                if record is None:
                    raise ScorelogError(
                        f"player slot {player_id} was removed before it was added"
                    )
                record["removed_turn"] = removed_turn
            elif command == "data":
                turn, tag_id, player_id, value = map(int, line.split()[1:])
                record = active_players.get(player_id)
                if record is None:
                    raise ScorelogError(
                        f"score data refers to inactive player slot {player_id}"
                    )
                record["values"].setdefault(turn, {})[
                    tags.get(tag_id, str(tag_id))
                ] = value
                turns.add(turn)
        except (IndexError, ValueError) as exc:
            raise ScorelogError(f"malformed scorelog line: {line}") from exc
    final_turn = max(turns) if turns else None
    rows: list[dict[str, Any]] = []
    if final_turn is not None:
        for record in player_records:
            through_turn = (
                record["removed_turn"]
                if record["removed_turn"] is not None else final_turn
            )
            score_turns = [
                turn for turn, metrics in record["values"].items()
                if turn <= through_turn and "score" in metrics
            ]
            last_score_turn = max(score_turns) if score_turns else None
            metrics = (
                record["values"][last_score_turn]
                if last_score_turn is not None else {}
            )
            rows.append({
                "player_id": record["player_id"],
                "name": record["name"],
                "score": metrics.get("score", 0),
                "metrics": metrics,
                "alive": record["removed_turn"] is None,
                "added_turn": record["added_turn"],
                "removed_turn": record["removed_turn"],
                "last_score_turn": last_score_turn,
            })
    rows.sort(key=lambda item: (-item["score"], item["player_id"]))
    last_score = None
    rank = 0
    for index, row in enumerate(rows, 1):
        if row["score"] != last_score:
            rank = index
            last_score = row["score"]
        row["rank"] = rank
    return {"final_turn": final_turn, "players": rows}


def load_jsonl(path: Path) -> Iterable[dict[str, Any]]:
    if not path.exists():
        return []
    events = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line:
            value = json.loads(line)
            if isinstance(value, dict):
                events.append(value)
    return events


def summarize_recovery(episode: Path) -> dict[str, Any]:
    """Read what this episode had to recover from, from the durable journal.

    A game that rewound real applied turns is not comparable to one that never
    faulted, and until this existed nothing a scorer read said which it was:
    the recovery journal was written, published on private per-agent health,
    and then never looked at again.
    """
    summary: dict[str, Any] = {
        "attempts": 0,
        "by_kind": {},
        "by_outcome": {},
        "rewound_applied_actions": False,
        "recovered_to_turns": [],
    }
    try:
        events = load_jsonl(episode / "v2-recovery" / "events.jsonl")
    except (OSError, ValueError):
        return summary
    for event in events:
        summary["attempts"] += 1
        for field in ("kind", "outcome"):
            value = event.get(field)
            if isinstance(value, str):
                bucket = summary[f"by_{field}"]
                bucket[value] = bucket.get(value, 0) + 1
        if event.get("outcome") != "recovered":
            continue
        turn = event.get("recovered_to_turn")
        if isinstance(turn, int) and not isinstance(turn, bool):
            if turn not in summary["recovered_to_turns"]:
                summary["recovered_to_turns"].append(turn)
        if event.get("rewound_applied_actions") is True:
            summary["rewound_applied_actions"] = True
    summary["recovered_to_turns"].sort()
    return summary


def summarize_episode(
    directory: str | Path,
    *,
    private_player_seats: dict[int, dict[str, Any]] | None = None,
) -> dict[str, Any]:
    episode = Path(directory)
    manifest_path = episode / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8")) if manifest_path.exists() else {}
    score_path = episode / "score.log"
    score = parse_scorelog(score_path) if score_path.exists() else {"final_turn": None, "players": []}
    seats = manifest.get("config", {}).get("seats", [])
    mapping = {item["name"]: item for item in seats}
    if private_player_seats:
        latest_by_player: dict[int, dict[str, Any]] = {}
        unconfigured: list[dict[str, Any]] = []
        for row in score["players"]:
            player_id = row.get("player_id")
            if type(player_id) is not int or player_id not in private_player_seats:
                unconfigured.append(row)
                continue
            previous = latest_by_player.get(player_id)
            if previous is None or (
                row.get("last_score_turn") or -1,
                row.get("added_turn") or -1,
            ) > (
                previous.get("last_score_turn") or -1,
                previous.get("added_turn") or -1,
            ):
                latest_by_player[player_id] = row
        score["players"] = list(latest_by_player.values()) + unconfigured
        score["players"].sort(
            key=lambda item: (-item["score"], item["player_id"]),
        )
        last_score = None
        rank = 0
        for index, row in enumerate(score["players"], 1):
            if row["score"] != last_score:
                rank = index
                last_score = row["score"]
            row["rank"] = rank
    for row in score["players"]:
        seat = (
            private_player_seats.get(row.get("player_id"))
            if private_player_seats else None
        ) or mapping.get(row["name"])
        row["seat_id"] = seat["id"] if seat else row["name"]
        row["controller_fingerprint"] = (
            seat.get("controller_fingerprint") or controller_fingerprint(seat)
            if seat else controller_fingerprint({"id": row["name"]})
        )
    per_seat: dict[str, dict[str, Any]] = {}
    for event in load_jsonl(episode / "decisions.jsonl"):
        if event.get("event") != "decision":
            continue
        seat_id = event["seat_id"]
        current = per_seat.setdefault(
            seat_id,
            {
                "controller_fingerprint": event.get("controller_fingerprint"),
                "turns": 0,
                "decisions": 0,
                "fallbacks": 0,
                "latency_ms": 0.0,
                "input_tokens": 0,
                "output_tokens": 0,
            },
        )
        current["turns"] += 1
        if event.get("action") is not None:
            current["decisions"] += 1
            current["latency_ms"] += float(event.get("latency_ms", 0))
        current["fallbacks"] += int(bool(event.get("fallback")))
        current["input_tokens"] += int(event.get("input_tokens", 0))
        current["output_tokens"] += int(event.get("output_tokens", 0))
    final_turn = score.get("final_turn")
    native_turns = (
        max(0, final_turn - 1)
        if isinstance(final_turn, int) and not isinstance(final_turn, bool)
        else 0
    )
    for seat in seats:
        if seat.get("type") != "native" or seat.get("id") in per_seat:
            continue
        per_seat[seat["id"]] = {
            "controller_fingerprint": (
                seat.get("controller_fingerprint")
                or controller_fingerprint(seat)
            ),
            "turns": native_turns,
            "decisions": 0,
            "fallbacks": 0,
            "latency_ms": 0.0,
            "input_tokens": 0,
            "output_tokens": 0,
        }
    for current in per_seat.values():
        count = current["decisions"]
        current["mean_latency_ms"] = round(current.pop("latency_ms") / count, 3) if count else 0
    recovery = summarize_recovery(episode)
    if recovery["rewound_applied_actions"]:
        # A rewound game cannot be silently ranked against clean ones, even if
        # the supervisor that ran it never got to write its own manifest.
        reasons = manifest.setdefault("invalid_reasons", [])
        if isinstance(reasons, list) and "v2_game_rewound" not in reasons:
            reasons.append("v2_game_rewound")
    return {
        "episode": str(episode),
        "manifest": manifest,
        "score": score,
        "seat_stats": per_seat,
        "recovery": recovery,
    }


def aggregate_leaderboard(summaries: Iterable[dict[str, Any]]) -> list[dict[str, Any]]:
    aggregate: dict[str, dict[str, Any]] = {}
    for summary in summaries:
        valid = bool(summary.get("manifest", {}).get("benchmark_valid"))
        configured = {
            seat["id"]: seat
            for seat in summary.get("manifest", {}).get("config", {}).get("seats", [])
        }

        def identity(seat_id: str, fingerprint: str | None = None) -> tuple[str, dict[str, Any]]:
            seat = configured.get(seat_id, {"id": seat_id})
            return (
                fingerprint or seat.get("controller_fingerprint")
                or controller_fingerprint(seat),
                seat,
            )

        def group(seat_id: str, fingerprint: str | None = None) -> dict[str, Any]:
            key, seat = identity(seat_id, fingerprint)
            return aggregate.setdefault(
                key,
                {
                    "controller_fingerprint": key,
                    "seat_id": seat_id,
                    "type": seat.get("type"),
                    "model": seat.get("model"),
                    "episodes": 0,
                    "valid_episodes": 0,
                    "wins": 0,
                    "score_total": 0.0,
                    "rank_total": 0.0,
                    "turns": 0,
                    "decisions": 0,
                    "fallbacks": 0,
                    "input_tokens": 0,
                    "output_tokens": 0,
                    "latency_weighted_ms": 0.0,
                },
            )

        for row in summary.get("score", {}).get("players", []):
            seat_id = row["seat_id"]
            current = group(
                seat_id, row.get("controller_fingerprint"),
            )
            current["episodes"] += 1
            current["valid_episodes"] += int(valid)
            if valid:
                current["wins"] += int(row["rank"] == 1)
                current["score_total"] += float(row["score"])
                current["rank_total"] += float(row["rank"])
        for seat_id, stats in summary.get("seat_stats", {}).items():
            current = group(
                seat_id, stats.get("controller_fingerprint"),
            )
            decisions = int(stats.get("decisions", 0))
            current["turns"] += int(stats.get("turns", 0))
            current["decisions"] += decisions
            current["fallbacks"] += int(stats.get("fallbacks", 0))
            current["input_tokens"] += int(stats.get("input_tokens", 0))
            current["output_tokens"] += int(stats.get("output_tokens", 0))
            current["latency_weighted_ms"] += (
                float(stats.get("mean_latency_ms", 0)) * decisions
            )
    leaderboard: list[dict[str, Any]] = []
    for current in aggregate.values():
        episodes = current.pop("episodes")
        score_total = current.pop("score_total")
        rank_total = current.pop("rank_total")
        latency_total = current.pop("latency_weighted_ms")
        decisions = current["decisions"]
        current["episodes"] = episodes
        valid_episodes = current["valid_episodes"]
        current["invalid_episodes"] = episodes - valid_episodes
        current["win_rate"] = (
            round(current["wins"] / valid_episodes, 6) if valid_episodes else 0
        )
        current["average_score"] = (
            round(score_total / valid_episodes, 3) if valid_episodes else 0
        )
        current["average_rank"] = (
            round(rank_total / valid_episodes, 3) if valid_episodes else 0
        )
        current["average_latency_ms"] = round(latency_total / decisions, 3) if decisions else 0
        leaderboard.append(current)
    leaderboard.sort(
        key=lambda item: (
            item["average_rank"] if item["valid_episodes"] else float("inf"),
            -item["average_score"],
            item["seat_id"],
        )
    )
    return leaderboard
