"""Offline export of a finished run into a compact dataset for video rendering.

The exporter walks a run directory and emits two JSON documents that a
Remotion project can consume with no live service, no network, and no second
save parser: ``meta.json`` holds everything constant across the match and
``frames.json`` holds one entry per replay turn.

Board snapshots come from :func:`agent_eval.save_replay.board_from_autosave`,
the same reader the replay gateway serves ``/v1/games/<id>/board.json`` from,
so the exported map is byte-for-byte the map the viewer draws.  Scores come
from ``replay.jsonl``, which records every turn even when a turn has no
autosave; turns without a save reuse the previous board and are flagged so the
renderer (and this module's summary) can report interpolation density.

Run directories are opened read-only.  Derived parse caches and the export
itself are written outside the run artifact directory.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path
from typing import Any, Iterator, Mapping, Sequence


SCHEMA_VERSION = 1

# Extras worth drawing on a 2D strategic map.  Everything else in the ruleset
# catalog is either invisible at this scale or player-private flavour.
INFRASTRUCTURE_BITS: tuple[tuple[str, int], ...] = (
    ("road", 1),
    ("railroad", 2),
    ("river", 4),
    ("pollution", 8),
)

# Fields copied per player per turn out of replay.jsonl.  Kept explicit so a
# schema drift upstream fails loudly here instead of silently emptying a panel.
STAT_INTEGER_FIELDS: tuple[str, ...] = (
    "score", "cities", "citizens", "population", "units", "gold", "culture",
)


class VideoExportError(RuntimeError):
    """A run directory could not be turned into a render dataset."""


def _read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise VideoExportError(f"cannot read {path.name}: {error}") from error
    except (UnicodeError, json.JSONDecodeError) as error:
        raise VideoExportError(f"{path.name} is not valid JSON") from error
    if not isinstance(value, dict):
        raise VideoExportError(f"{path.name} must contain a JSON object")
    return value


def _read_replay_rows(path: Path) -> list[dict[str, Any]]:
    """Return replay.jsonl rows ordered by turn, dropping unusable lines."""
    rows: dict[int, dict[str, Any]] = {}
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise VideoExportError(f"cannot read replay.jsonl: {error}") from error
    except UnicodeError as error:
        raise VideoExportError("replay.jsonl is not valid UTF-8") from error
    for line in lines:
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            continue
        if not isinstance(row, dict):
            continue
        turn = row.get("turn")
        players = row.get("players")
        if isinstance(turn, bool) or not isinstance(turn, int) or turn < 0:
            continue
        if not isinstance(players, list):
            continue
        rows[turn] = row
    if not rows:
        raise VideoExportError("replay.jsonl contains no usable turns")
    return [rows[turn] for turn in sorted(rows)]


def _integer(value: Any, fallback: int = 0) -> int:
    """Coerce a replay number to int, tolerating the Lua bridge's floats.

    Some runs record ``"player_id": 0.0`` rather than ``0``. Rejecting those
    silently emptied every stats panel, so integral floats are accepted here
    while genuinely fractional values still fall back.
    """
    if isinstance(value, bool):
        return fallback
    if isinstance(value, int):
        return value
    if isinstance(value, float) and value.is_integer():
        return int(value)
    return fallback


def _text(value: Any, fallback: str = "") -> str:
    return value if isinstance(value, str) and value else fallback


def _resolved_places(manifest: Mapping[str, Any]) -> list[dict[str, Any]]:
    places = manifest.get("resolved_places")
    if not isinstance(places, list):
        return []
    return [place for place in places if isinstance(place, dict)]


def _ai_difficulty(manifest: Mapping[str, Any]) -> str | None:
    """The game's server AI level, or None for a run archived without one."""
    config = manifest.get("config")
    level = config.get("difficulty") if isinstance(config, Mapping) else None
    return level if isinstance(level, str) and level else None


def _seat_labels(manifest: Mapping[str, Any]) -> dict[str, dict[str, Any]]:
    """Map seat id to the controller identity the title card should credit."""
    labels: dict[str, dict[str, Any]] = {}
    config = manifest.get("config")
    seats = config.get("seats") if isinstance(config, dict) else None
    for seat in seats if isinstance(seats, list) else []:
        if not isinstance(seat, dict):
            continue
        seat_id = _text(seat.get("id"))
        if not seat_id:
            continue
        labels[seat_id] = {
            "controller_label": _text(seat.get("controller_label")) or None,
            "controller_type": _text(seat.get("type")) or None,
            "model": _text(seat.get("model")) or None,
            "ai_difficulty": _text(seat.get("ai_difficulty")) or None,
        }
    for place in _resolved_places(manifest):
        place_number = _integer(place.get("place"), -1)
        seat_id = f"place-{place_number}" if place_number >= 0 else ""
        if not seat_id:
            continue
        entry = labels.setdefault(seat_id, {})
        for key in (
            "controller_label", "controller_type", "model", "ai_difficulty",
        ):
            value = _text(place.get(key)) or None
            if value is not None:
                entry[key] = value
        color = _text(place.get("player_color"))
        if color:
            entry["player_color"] = color
    # The AI level is one game-wide server setting, so a seat the server drives
    # inherits it even when only the top-level config recorded it -- which is
    # every run archived before the per-seat field existed.
    game_level = _ai_difficulty(manifest)
    if game_level:
        for entry in labels.values():
            if entry.get("controller_type") == "native":
                entry["ai_difficulty"] = entry.get("ai_difficulty") or game_level
    return labels


def _derive_events(
    runs_root: Path,
    game_id: str,
    places: Sequence[Mapping[str, Any]],
    cache_root: Path,
) -> dict[str, Any]:
    """Return the derived event log, or an empty-but-valid log if it fails.

    Events are a garnish on the film: a run whose saves cannot be walked should
    still produce a video, so any extraction failure degrades to `available:
    false` rather than taking the whole export down with it.
    """
    empty: dict[str, Any] = {
        "available": False, "events": [], "event_counts": {}, "total_events": 0,
        "truncated": False, "omitted_counts": {}, "last_turn": 0,
    }
    try:
        from .game_events import events_from_autosaves

        payload = events_from_autosaves(
            runs_root, game_id, list(places), cache_root=cache_root,
        )
    except Exception:  # noqa: BLE001 - the film matters more than its captions
        return empty
    if not isinstance(payload, Mapping):
        return empty
    events = payload.get("events")
    if not isinstance(events, list):
        return empty
    return {
        "available": payload.get("available") is not False,
        "events": events,
        "event_counts": payload.get("event_counts") or {},
        "total_events": _integer(payload.get("total_events"), len(events)),
        "truncated": payload.get("truncated") is True,
        "omitted_counts": payload.get("omitted_counts") or {},
        "last_turn": _integer(payload.get("last_turn")),
    }


def _board_turns(run_directory: Path) -> list[int]:
    """Return every turn with at least one autosave, ascending."""
    from .save_replay import _discover_saves

    saves_directory = run_directory / "saves"
    if not saves_directory.is_dir():
        return []
    return sorted(_discover_saves(saves_directory))


def _load_board(
    runs_root: Path,
    game_id: str,
    places: Sequence[Mapping[str, Any]],
    turn: int,
    cache_root: Path,
) -> dict[str, Any] | None:
    """Load one exact-turn board through the gateway's own reader."""
    from .replay_gateway import _default_board_loader

    try:
        board = _default_board_loader(
            runs_root, game_id, list(places), turn=turn, cache_root=cache_root,
        )
    except (FileNotFoundError, OSError):
        return None
    except Exception:  # noqa: BLE001 - an unreadable save must not stop the export
        return None
    if not isinstance(board, Mapping):
        return None
    return dict(board)


def _infrastructure_rows(board: Mapping[str, Any]) -> list[str]:
    """Collapse the extras bitfield down to the layers the video draws.

    One lowercase hex digit per tile, bits per :data:`INFRASTRUCTURE_BITS`.
    """
    width = _integer(board.get("width"))
    height = _integer(board.get("height"))
    catalog = board.get("extras_catalog")
    layers = board.get("extra_layers")
    if not isinstance(catalog, list) or not isinstance(layers, list):
        return ["0" * width for _ in range(height)]
    wanted: dict[int, int] = {}
    for extra in catalog:
        if not isinstance(extra, dict):
            continue
        name = _text(extra.get("name")).lower()
        extra_id = _integer(extra.get("id"), -1)
        if extra_id < 0:
            continue
        for key, bit in INFRASTRUCTURE_BITS:
            if name == key:
                wanted[extra_id] = bit
    rows: list[str] = []
    for y in range(height):
        row: list[str] = []
        for x in range(width):
            mask = 0
            for extra_id, bit in wanted.items():
                layer_index, bit_index = divmod(extra_id, 4)
                layer = layers[layer_index] if layer_index < len(layers) else None
                if not isinstance(layer, list) or y >= len(layer):
                    continue
                line = layer[y]
                if not isinstance(line, str) or x >= len(line):
                    continue
                try:
                    nibble = int(line[x], 16)
                except ValueError:
                    continue
                if nibble & (1 << bit_index):
                    mask |= bit
            row.append(f"{mask:x}")
        rows.append("".join(row))
    return rows


def _compact_cities(board: Mapping[str, Any]) -> list[list[int]]:
    cities = board.get("cities")
    result: list[list[int]] = []
    for city in cities if isinstance(cities, list) else []:
        if not isinstance(city, dict):
            continue
        result.append([
            _integer(city.get("x"), -1),
            _integer(city.get("y"), -1),
            _integer(city.get("player_id"), -1),
            _integer(city.get("size")),
            1 if city.get("capital") is True else 0,
        ])
    return [row for row in result if row[0] >= 0 and row[1] >= 0 and row[2] >= 0]


def _compact_units(board: Mapping[str, Any]) -> list[list[int]]:
    stacks = board.get("unit_stacks")
    result: list[list[int]] = []
    for stack in stacks if isinstance(stacks, list) else []:
        if not isinstance(stack, dict):
            continue
        result.append([
            _integer(stack.get("x"), -1),
            _integer(stack.get("y"), -1),
            _integer(stack.get("player_id"), -1),
            _integer(stack.get("count"), 1),
        ])
    return [row for row in result if row[0] >= 0 and row[1] >= 0 and row[2] >= 0]


def _city_names(board: Mapping[str, Any]) -> dict[str, str]:
    """Name lookup keyed by tile, so frames can stay purely numeric."""
    names: dict[str, str] = {}
    cities = board.get("cities")
    for city in cities if isinstance(cities, list) else []:
        if not isinstance(city, dict):
            continue
        name = _text(city.get("name"))
        x = _integer(city.get("x"), -1)
        y = _integer(city.get("y"), -1)
        if name and x >= 0 and y >= 0:
            names[f"{x},{y}"] = name
    return names


def _player_directory(
    replay_rows: Sequence[Mapping[str, Any]],
    board: Mapping[str, Any] | None,
    seat_labels: Mapping[str, Mapping[str, Any]],
) -> list[dict[str, Any]]:
    """Merge replay seats with board factions into one ordered player table."""
    board_players: dict[int, dict[str, Any]] = {}
    entries = board.get("players") if isinstance(board, Mapping) else None
    for entry in entries if isinstance(entries, list) else []:
        if not isinstance(entry, dict):
            continue
        player_id = _integer(entry.get("player_id"), -1)
        if player_id >= 0:
            board_players[player_id] = entry

    players: dict[int, dict[str, Any]] = {}
    for row in replay_rows:
        for player in row.get("players", []):
            if not isinstance(player, dict):
                continue
            player_id = _integer(player.get("player_id"), -1)
            if player_id < 0:
                continue
            seat_id = _text(player.get("seat_id")) or None
            labels = dict(seat_labels.get(seat_id or "", {}))
            existing = players.get(player_id, {})
            players[player_id] = {
                "player_id": player_id,
                "seat_id": seat_id,
                # Seats are the benchmarked contestants; dynamic factions such
                # as barbarians also score but never occupy a place.
                "seat": bool(seat_id and seat_id.startswith("place-")),
                "name": _text(player.get("player_name"), existing.get("name", "")),
                "nation": _text(player.get("nation"), existing.get("nation", "")),
                "color": _text(
                    (board_players.get(player_id) or {}).get("player_color"),
                    _text(labels.get("player_color"), existing.get("color", "")),
                ),
                "controller_label": labels.get("controller_label")
                or existing.get("controller_label"),
                "controller_type": labels.get("controller_type")
                or existing.get("controller_type"),
                "model": labels.get("model") or existing.get("model"),
                "ai_difficulty": labels.get("ai_difficulty")
                or existing.get("ai_difficulty"),
                "scored": True,
            }
    for player_id, entry in board_players.items():
        if player_id in players:
            continue
        players[player_id] = {
            "player_id": player_id,
            "seat_id": _text(entry.get("seat_id")) or None,
            "seat": False,
            "name": _text(entry.get("player_name"), f"Faction {player_id}"),
            "nation": _text(entry.get("nation")),
            "color": _text(entry.get("player_color"), "#8a949c"),
            "controller_label": _text(entry.get("controller_label")) or None,
            "controller_type": _text(entry.get("controller_type")) or None,
            "model": _text(entry.get("model")) or None,
            "ai_difficulty": _text(entry.get("ai_difficulty")) or None,
            "scored": False,
        }
    return [players[player_id] for player_id in sorted(players)]


def _player_stats(row: Mapping[str, Any]) -> list[dict[str, Any]]:
    stats: list[dict[str, Any]] = []
    for player in row.get("players", []):
        if not isinstance(player, dict):
            continue
        player_id = _integer(player.get("player_id"), -1)
        if player_id < 0:
            continue
        research = player.get("research")
        known = player.get("known_tech_ids")
        entry: dict[str, Any] = {
            "player_id": player_id,
            "alive": player.get("alive") is not False,
            "government": _text(player.get("government")),
            "techs": len(known) if isinstance(known, list) else 0,
            "future_techs": _integer(player.get("future_techs")),
            "researching": _text(
                research.get("name") if isinstance(research, dict) else "",
            ),
            # Bulbs accumulated toward the current tech. `cost` is recorded as
            # zero throughout these archives, so no completion ratio is exported
            # rather than a fabricated denominator.
            "bulbs": _integer(
                research.get("bulbs") if isinstance(research, dict) else 0,
            ),
            "research_cost": _integer(
                research.get("cost") if isinstance(research, dict) else 0,
            ),
        }
        for field in STAT_INTEGER_FIELDS:
            entry[field] = _integer(player.get(field))
        stats.append(entry)
    return stats


def _iter_frames(
    replay_rows: Sequence[Mapping[str, Any]],
    runs_root: Path,
    game_id: str,
    places: Sequence[Mapping[str, Any]],
    board_turns: Sequence[int],
    cache_root: Path,
    progress: bool,
) -> Iterator[tuple[dict[str, Any], dict[str, Any] | None]]:
    """Yield ``(frame, board)`` pairs in turn order.

    ``board`` is the freshly parsed snapshot when this turn had a readable
    save, otherwise ``None`` and the frame carries no board payload -- the
    renderer holds the previous board in that case.
    """
    available = set(board_turns)
    previous_terrain: list[str] | None = None
    previous_infrastructure: list[str] | None = None
    previous_board_turn: int | None = None
    total = len(replay_rows)
    for index, row in enumerate(replay_rows):
        turn = _integer(row.get("turn"), -1)
        board = None
        if turn in available:
            board = _load_board(runs_root, game_id, places, turn, cache_root)
        frame: dict[str, Any] = {
            "turn": turn,
            "year": _integer(row.get("year")),
            "stats": _player_stats(row),
        }
        if board is None:
            frame["board_turn"] = previous_board_turn
            frame["interpolated"] = True
        else:
            terrain = [
                line for line in board.get("terrain_rows", [])
                if isinstance(line, str)
            ]
            owners = [
                line for line in board.get("owner_rows", [])
                if isinstance(line, str)
            ]
            infrastructure = _infrastructure_rows(board)
            frame["board_turn"] = turn
            frame["interpolated"] = False
            frame["owners"] = owners
            frame["cities"] = _compact_cities(board)
            frame["units"] = _compact_units(board)
            frame["city_names"] = _city_names(board)
            # Terrain and infrastructure change rarely; ship them only on
            # change so a 596-turn export stays a few megabytes.
            frame["terrain"] = terrain if terrain != previous_terrain else None
            frame["infrastructure"] = (
                infrastructure if infrastructure != previous_infrastructure else None
            )
            previous_terrain = terrain
            previous_infrastructure = infrastructure
            previous_board_turn = turn
        if progress and (index % 25 == 0 or index + 1 == total):
            percent = (index + 1) * 100 // max(1, total)
            print(
                f"  boards {index + 1}/{total} ({percent}%)",
                file=sys.stderr, flush=True,
            )
        yield frame, board


def export_run(
    runs_root: str | Path,
    game_id: str,
    output_directory: str | Path,
    *,
    cache_root: str | Path | None = None,
    progress: bool = False,
) -> dict[str, Any]:
    """Write ``meta.json`` and ``frames.json`` and return the export summary."""
    runs_path = Path(runs_root)
    run_directory = runs_path / game_id
    if not run_directory.is_dir():
        raise VideoExportError(f"no run directory for {game_id}")
    output_path = Path(output_directory)
    output_path.mkdir(parents=True, exist_ok=True)
    cache_path = Path(cache_root) if cache_root is not None else output_path / "cache"
    cache_path.mkdir(parents=True, exist_ok=True)

    manifest = _read_json(run_directory / "manifest.json")
    replay_rows = _read_replay_rows(run_directory / "replay.jsonl")
    places = _resolved_places(manifest)
    seat_labels = _seat_labels(manifest)
    board_turns = _board_turns(run_directory)

    started = time.monotonic()
    frames: list[dict[str, Any]] = []
    first_board: dict[str, Any] | None = None
    last_board: dict[str, Any] | None = None
    for frame, board in _iter_frames(
        replay_rows, runs_path, game_id, places, board_turns, cache_path, progress,
    ):
        if board is not None:
            if first_board is None:
                first_board = board
            last_board = board
        frames.append(frame)
    if first_board is None:
        raise VideoExportError("no readable autosave board in this run")

    # The renderer needs a board on frame zero; walk the leading gap forward.
    leading_gap = 0
    for frame in frames:
        if frame["board_turn"] is not None:
            break
        leading_gap += 1
    seed_turn = frames[leading_gap]["board_turn"] if leading_gap < len(frames) else None
    for frame in frames[:leading_gap]:
        frame["board_turn"] = seed_turn

    # Every derived event ships. The composition decides which ones a given
    # playback speed has room for, so re-tuning caption density never means
    # re-exporting a game.
    events = _derive_events(runs_path, game_id, places, cache_path)

    interpolated_turns = [frame["turn"] for frame in frames if frame["interpolated"]]
    config = manifest.get("config") if isinstance(manifest.get("config"), dict) else {}
    players = _player_directory(replay_rows, last_board, seat_labels)
    meta: dict[str, Any] = {
        "schema_version": SCHEMA_VERSION,
        "game_id": game_id,
        "generated_at": time.time(),
        "control_protocol": _text(manifest.get("control_protocol")),
        "ruleset": _text(config.get("ruleset")),
        "objective": _text(config.get("objective")),
        "ai_difficulty": _ai_difficulty(manifest),
        "timing_mode": _text(config.get("timing_mode")),
        "state": _text(manifest.get("state")),
        "status": _text(manifest.get("status")),
        "error": _text(manifest.get("error")) or None,
        "seeds": config.get("seeds") if isinstance(config.get("seeds"), list) else [],
        "created_at": manifest.get("created_at"),
        "started_at": manifest.get("started_at"),
        "finished_at": manifest.get("finished_at"),
        "width": _integer(first_board.get("width")),
        "height": _integer(first_board.get("height")),
        "topology": _text(first_board.get("topology")),
        "wrap": _text(first_board.get("wrap")),
        "terrain_catalog": first_board.get("terrain_catalog", []),
        "altitude_rows": first_board.get("altitude_rows", []),
        "infrastructure_bits": {key: bit for key, bit in INFRASTRUCTURE_BITS},
        "players": players,
        "first_turn": frames[0]["turn"],
        "last_turn": frames[-1]["turn"],
        "frame_count": len(frames),
        "board_turn_count": len(board_turns),
        "interpolated_turn_count": len(interpolated_turns),
        "interpolated_turns": interpolated_turns[:512],
        "board_density": round(
            (len(frames) - len(interpolated_turns)) / max(1, len(frames)), 6,
        ),
        "event_counts": events["event_counts"],
        "total_events": events["total_events"],
        "events_truncated": events["truncated"],
    }

    meta_path = output_path / "meta.json"
    frames_path = output_path / "frames.json"
    events_path = output_path / "events.json"
    meta_path.write_text(json.dumps(meta, separators=(",", ":")), encoding="utf-8")
    frames_path.write_text(
        json.dumps({"schema_version": SCHEMA_VERSION, "game_id": game_id,
                    "frames": frames}, separators=(",", ":")),
        encoding="utf-8",
    )
    events_path.write_text(
        json.dumps({"schema_version": SCHEMA_VERSION, "game_id": game_id, **events},
                   separators=(",", ":")),
        encoding="utf-8",
    )
    return {
        "game_id": game_id,
        "output_directory": str(output_path),
        "meta_path": str(meta_path),
        "frames_path": str(frames_path),
        "events_path": str(events_path),
        "meta_bytes": meta_path.stat().st_size,
        "frames_bytes": frames_path.stat().st_size,
        "events_bytes": events_path.stat().st_size,
        "event_count": len(events["events"]),
        "events_available": events["available"],
        "frame_count": len(frames),
        "board_turn_count": len(board_turns),
        "interpolated_turn_count": len(interpolated_turns),
        "interpolated_turns": interpolated_turns,
        "first_turn": frames[0]["turn"],
        "last_turn": frames[-1]["turn"],
        "width": meta["width"],
        "height": meta["height"],
        "players": [
            {"player_id": player["player_id"], "name": player["name"],
             "nation": player["nation"], "color": player["color"]}
            for player in players
        ],
        "elapsed_s": round(time.monotonic() - started, 3),
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="python -m agent_eval.video_export",
        description="Export a run directory into a Remotion render dataset.",
    )
    parser.add_argument("game_id", help="Run identifier under the runs root.")
    parser.add_argument(
        "--runs-root", default=".agent-eval/runs",
        help="Directory holding run directories (read-only).",
    )
    parser.add_argument(
        "--out", default=None,
        help="Export directory (default .agent-eval/video-exports/<game_id>).",
    )
    parser.add_argument(
        "--cache-root", default=None,
        help="Save-parse cache directory (default <out>/cache).",
    )
    parser.add_argument(
        "--quiet", action="store_true", help="Suppress per-board progress.",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = build_parser().parse_args(list(argv) if argv is not None else None)
    output = arguments.out or str(
        Path(".agent-eval/video-exports") / arguments.game_id,
    )
    try:
        summary = export_run(
            arguments.runs_root,
            arguments.game_id,
            output,
            cache_root=arguments.cache_root,
            progress=not arguments.quiet,
        )
    except VideoExportError as error:
        print(f"video export failed: {error}", file=sys.stderr)
        return 2
    printable = dict(summary)
    printable["interpolated_turns"] = summary["interpolated_turns"][:32]
    print(json.dumps(printable, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
