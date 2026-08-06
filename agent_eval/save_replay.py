"""Read-only replay telemetry reconstructed from Freeciv autosaves.

This is a compatibility reader for games created before ``replay.jsonl`` was
available.  It never loads a save into Freeciv and never writes beside the
source saves.  The persisted save format does not contain the current research
cost, so ``research.cost`` and catalog ``cost_base`` are intentionally ``0``;
the values must not be presented as measured science costs.
"""

from __future__ import annotations

import bz2
import csv
import gzip
import hashlib
import json
import lzma
import os
import re
import shutil
import stat
import subprocess
import tempfile
from pathlib import Path
from typing import Any, Mapping, Sequence


SCHEMA_VERSION = 1
CACHE_VERSION = 2
MAX_SOURCE_BYTES = 128 * 1024 * 1024
MAX_DECOMPRESSED_BYTES = 256 * 1024 * 1024
MAX_PREDECESSOR_PROBES = 32
MAX_PLAYERS = 512
GAME_ID_RE = re.compile(r"^[A-Za-z0-9_-]{20,80}$")
SAVE_NAME_RE = re.compile(
    r"^turn-(\d+)-(?:auto|final)\.sav(?P<compression>\.gz|\.bz2|\.xz|\.zst)?$"
)
PPM_NAME_RE = re.compile(r"^turn-(\d+)-.*\.map\.ppm$")
PLAYER_SECTION_RE = re.compile(r"^player(\d+)$")
PPM_PLAYER_RE = re.compile(
    r'^#\s*playerno:(\d+):color:\(\s*(\d+),\s*(\d+),\s*(\d+)\)'
    r':name:"(.*)"\s*$'
)
SUPPORTED_SAVE_VERSION = 80
MAX_BOARD_WIDTH = 512
MAX_BOARD_HEIGHT = 512
MAX_BOARD_TILES = 128 * 1024
_COMPRESSION_ORDER = {"": 0, ".gz": 1, ".bz2": 2, ".xz": 3, ".zst": 4}
_REPO_ROOT = Path(__file__).resolve().parent.parent


class SaveReplayError(ValueError):
    """An HTTP-safe validation error from the autosave replay reader."""


class _UnreadableSave(Exception):
    def __init__(self, message: str = "An autosave was incomplete or unreadable and was skipped."):
        super().__init__(message)
        self.public_message = message


def _public_text(value: Any, limit: int = 160) -> str:
    if not isinstance(value, str):
        return ""
    return "".join(
        character for character in value
        if character in "\t " or ord(character) >= 0x20
    )[:limit]


def _integer(value: Any, default: int | None = None) -> int | None:
    if isinstance(value, bool):
        return default
    if isinstance(value, int):
        return value
    if isinstance(value, str) and re.fullmatch(r"[-+]?\d+", value.strip()):
        try:
            return int(value)
        except ValueError:
            return default
    return default


def _boolean(value: Any) -> bool | None:
    if value == "TRUE":
        return True
    if value == "FALSE":
        return False
    return None


def _csv_row(value: str) -> list[str]:
    try:
        rows = list(csv.reader(
            [value], escapechar="\\", doublequote=False, strict=True,
        ))
    except csv.Error as exc:
        raise _UnreadableSave() from exc
    if len(rows) != 1:
        raise _UnreadableSave()
    return rows[0]


def _scalar_value(value: str) -> str:
    value = value.strip()
    if not value.startswith('"'):
        return value
    fields = _csv_row(value)
    # Vector-valued secfile scalars use the same comma-separated syntax as a
    # table row.  Preserve those for the field-specific parser below.
    return fields[0] if len(fields) == 1 else value


def _sections(text: str) -> dict[str, list[str]]:
    result: dict[str, list[str]] = {}
    current: list[str] | None = None
    for raw_line in text.splitlines():
        line = raw_line.rstrip("\r")
        match = re.fullmatch(r"\[([^]\x00-\x1f]+)]", line.strip())
        if match:
            current = result.setdefault(match.group(1), [])
        elif current is not None:
            current.append(line)
    return result


def _scalars(lines: Sequence[str]) -> dict[str, str]:
    result: dict[str, str] = {}
    in_table = False
    for raw_line in lines:
        line = raw_line.strip()
        if in_table:
            if line == "}":
                in_table = False
            continue
        if not line or line.startswith((";", "#")) or "=" not in line:
            continue
        key, raw_value = line.split("=", 1)
        key = key.strip()
        raw_value = raw_value.strip()
        if raw_value.startswith("{"):
            in_table = True
            continue
        result[key] = _scalar_value(raw_value)
    if in_table:
        raise _UnreadableSave()
    return result


def _table(lines: Sequence[str], table_name: str) -> tuple[list[str], list[list[str]]]:
    prefix = f"{table_name}="
    for index, raw_line in enumerate(lines):
        line = raw_line.strip()
        if not line.startswith(prefix):
            continue
        value = line[len(prefix):].lstrip()
        if not value.startswith("{"):
            raise _UnreadableSave()
        header = _csv_row(value[1:])
        rows: list[list[str]] = []
        for row_line in lines[index + 1:]:
            stripped = row_line.strip()
            if stripped == "}":
                return header, rows
            if not stripped or stripped.startswith((";", "#")):
                continue
            row = _csv_row(stripped)
            if len(row) != len(header):
                raise _UnreadableSave()
            rows.append(row)
        raise _UnreadableSave()
    raise _UnreadableSave()


def _rows_by_header(
    table: tuple[list[str], list[list[str]]],
) -> list[dict[str, str]]:
    header, rows = table
    if not header or len(set(header)) != len(header):
        raise _UnreadableSave()
    return [dict(zip(header, row, strict=True)) for row in rows]


def _read_limited(stream: Any) -> bytes:
    chunks: list[bytes] = []
    remaining = MAX_DECOMPRESSED_BYTES + 1
    while remaining:
        chunk = stream.read(min(1024 * 1024, remaining))
        if not chunk:
            break
        chunks.append(chunk)
        remaining -= len(chunk)
    value = b"".join(chunks)
    if len(value) > MAX_DECOMPRESSED_BYTES:
        raise _UnreadableSave("An autosave is too large for replay telemetry.")
    return value


def _read_zstd(stream: Any) -> bytes:
    executable = shutil.which("zstd")
    if executable is None:
        raise _UnreadableSave("Zstandard autosaves are unsupported on this host.")
    try:
        process = subprocess.Popen(
            [executable, "--decompress", "--stdout", "--no-progress"],
            stdin=stream,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
    except OSError as exc:
        raise _UnreadableSave("Zstandard autosaves are unsupported on this host.") from exc
    try:
        assert process.stdout is not None
        value = _read_limited(process.stdout)
        if len(value) > MAX_DECOMPRESSED_BYTES:
            process.terminate()
        returncode = process.wait(timeout=10)
    except subprocess.TimeoutExpired as exc:
        process.kill()
        process.wait()
        raise _UnreadableSave() from exc
    except BaseException:
        process.kill()
        process.wait()
        raise
    if returncode != 0:
        raise _UnreadableSave()
    return value


def _stat_signature(value: os.stat_result) -> tuple[int, int, int, int, int, int]:
    return (
        value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns,
        value.st_ctime_ns, value.st_mode,
    )


def _read_stable_save(path: Path) -> tuple[str, tuple[int, int, int, int, int, int]]:
    try:
        descriptor = os.open(
            path, os.O_RDONLY | getattr(os, "O_CLOEXEC", 0)
            | getattr(os, "O_NOFOLLOW", 0),
        )
    except OSError as exc:
        raise _UnreadableSave() from exc
    with os.fdopen(descriptor, "rb") as source:
        before = os.fstat(source.fileno())
        if (
            not stat.S_ISREG(before.st_mode) or before.st_size <= 0
            or before.st_size > MAX_SOURCE_BYTES
        ):
            raise _UnreadableSave()
        try:
            if path.name.endswith(".gz"):
                with gzip.GzipFile(fileobj=source, mode="rb") as stream:
                    value = _read_limited(stream)
            elif path.name.endswith(".bz2"):
                with bz2.BZ2File(source, "rb") as stream:
                    value = _read_limited(stream)
            elif path.name.endswith(".xz"):
                with lzma.LZMAFile(source, "rb") as stream:
                    value = _read_limited(stream)
            elif path.name.endswith(".zst"):
                value = _read_zstd(source)
            else:
                value = _read_limited(source)
        except (OSError, EOFError, gzip.BadGzipFile, lzma.LZMAError) as exc:
            raise _UnreadableSave() from exc
        after = os.fstat(source.fileno())
    if _stat_signature(before) != _stat_signature(after):
        raise _UnreadableSave()
    try:
        text = value.decode("utf-8", errors="strict")
    except UnicodeError as exc:
        raise _UnreadableSave() from exc
    return text, _stat_signature(before)


def _normalize_ruleset_name(value: str) -> str:
    normalized = _public_text(value, 80)
    return normalized.split(":", 1)[1] if normalized.startswith("?") and ":" in normalized else normalized


def _classic_requirements() -> dict[str, tuple[str, ...]]:
    path = _REPO_ROOT / "data" / "classic" / "techs.ruleset"
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise _UnreadableSave("Technology dependencies are temporarily unavailable.") from exc
    sections: list[dict[str, str]] = []
    current: dict[str, str] | None = None
    section_re = re.compile(r"^\[advance_[^]]+]$")
    value_re = re.compile(r'^(name|rule_name|req1|req2)\s*=\s*(?:_\()?"([^"]+)"\)?')
    for raw_line in lines:
        line = raw_line.strip()
        if section_re.fullmatch(line):
            if current is not None:
                sections.append(current)
            current = {}
            continue
        if current is None or not line or line.startswith(";"):
            continue
        match = value_re.match(line)
        if match:
            current[match.group(1)] = match.group(2)
    if current is not None:
        sections.append(current)
    requirements: dict[str, tuple[str, ...]] = {}
    for section in sections:
        name = section.get("name")
        if not name:
            continue
        rule_name = _normalize_ruleset_name(section.get("rule_name", name))
        requirements[rule_name] = tuple(
            _normalize_ruleset_name(parent)
            for parent in (section.get("req1"), section.get("req2"))
            if parent and parent not in {"None", "Never"}
        )
    return requirements


def _technology_catalog(vector: Sequence[str], ruleset: str) -> dict[str, Any]:
    technologies = [
        {
            "id": tech_id,
            "rule_name": _normalize_ruleset_name(name),
            "name": _normalize_ruleset_name(name),
            "cost_base": 0,
        }
        for tech_id, name in enumerate(vector)
        if name not in {"A_NONE", "A_UNSET", "A_FUTURE"}
    ]
    if ruleset != "classic":
        return {"schema_version": SCHEMA_VERSION, "technologies": technologies}
    requirements = _classic_requirements()
    by_name = {technology["rule_name"]: technology for technology in technologies}
    if len(by_name) != 87 or set(by_name) != set(requirements):
        raise _UnreadableSave("The autosave technology catalog does not match bundled Classic rules.")
    depths: dict[str, int] = {}
    visiting: set[str] = set()

    def depth(name: str) -> int:
        if name in depths:
            return depths[name]
        if name in visiting:
            raise _UnreadableSave("The autosave technology catalog is cyclic.")
        visiting.add(name)
        parents = requirements[name]
        if any(parent not in by_name for parent in parents):
            raise _UnreadableSave("The autosave technology catalog is incomplete.")
        value = 0 if not parents else max(depth(parent) for parent in parents) + 1
        visiting.remove(name)
        depths[name] = value
        return value

    result = []
    for technology in technologies:
        name = technology["rule_name"]
        result.append({
            **technology,
            "requires": [by_name[parent]["id"] for parent in requirements[name]],
            "depth": depth(name),
        })
    result.sort(key=lambda row: row["id"])
    return {"schema_version": SCHEMA_VERSION, "technologies": result}


def _technology_vector(savefile: Mapping[str, str]) -> list[str]:
    raw = savefile.get("technology_vector")
    if not raw:
        raise _UnreadableSave()
    vector = [_normalize_ruleset_name(value) for value in _csv_row(raw)]
    expected = _integer(savefile.get("technology_size"))
    if expected is None or expected != len(vector) or not vector:
        raise _UnreadableSave()
    return vector


def _pooled_research(settings_lines: Sequence[str]) -> bool:
    try:
        rows = _rows_by_header(_table(settings_lines, "set"))
    except _UnreadableSave:
        return False
    for row in rows:
        if row.get("name") == "team_pooled_research":
            return _boolean(row.get("value")) is True
    return False


def _research_rows(lines: Sequence[str]) -> dict[int, dict[str, str]]:
    result: dict[int, dict[str, str]] = {}
    for row in _rows_by_header(_table(lines, "r")):
        number = _integer(row.get("number"))
        done = row.get("done")
        if number is None or number < 0 or done is None or not re.fullmatch(r"[01]+", done):
            raise _UnreadableSave()
        result[number] = row
    if not result:
        raise _UnreadableSave()
    return result


def _citizens(score: Mapping[str, str]) -> int:
    fields = ("happy", "content", "unhappy", "angry")
    values: list[int] = []
    for key in fields:
        value = _integer(score.get(key))
        if value is None or value < 0:
            raise _UnreadableSave()
        values.append(value)
    for key, raw_value in score.items():
        if re.fullmatch(r"specialists\d+", key):
            value = _integer(raw_value)
            if value is None or value < 0:
                raise _UnreadableSave()
            values.append(value)
    return sum(values)


def _raw_color(player: Mapping[str, str]) -> str | None:
    components = [_integer(player.get(f"color.{part}")) for part in "rgb"]
    if any(value is None or not 0 <= value <= 255 for value in components):
        return None
    red, green, blue = components
    return f"#{red:02X}{green:02X}{blue:02X}"


def _setting_values(lines: Sequence[str]) -> dict[str, str]:
    try:
        rows = _rows_by_header(_table(lines, "set"))
    except _UnreadableSave:
        return {}
    return {
        row["name"]: row["value"]
        for row in rows
        if isinstance(row.get("name"), str) and isinstance(row.get("value"), str)
    }


def _vector_row(value: str, width: int) -> list[str]:
    row = _csv_row(value)
    if len(row) != width:
        raise _UnreadableSave()
    return row


def _owner_run_row(values: Sequence[str], player_count: int) -> str:
    normalized: list[str] = []
    for raw in values:
        if raw == "-":
            normalized.append("-")
            continue
        owner = _integer(raw)
        if owner is None or owner < 0 or owner >= player_count:
            raise _UnreadableSave()
        normalized.append(str(owner))
    runs: list[str] = []
    start = 0
    while start < len(normalized):
        end = start + 1
        while end < len(normalized) and normalized[end] == normalized[start]:
            end += 1
        runs.append(f"{normalized[start]}:{end - start}")
        start = end
    return ",".join(runs)


def _parse_board(
    sections: Mapping[str, Sequence[str]],
    game_id: str,
    turn: int,
    player_count: int,
) -> dict[str, Any] | None:
    """Parse the omniscient semantic board without player-private knowledge.

    The payload intentionally includes terrain, altitude, public territory, and
    cities only.  Per-player known-map rows, worked tiles, unit orders, and
    other agent-private state never leave the save reader.
    """
    map_lines = sections.get("map")
    savefile_lines = sections.get("savefile")
    if map_lines is None or savefile_lines is None:
        return None
    settings = _setting_values(sections.get("settings", ()))
    width = _integer(settings.get("xsize"))
    height = _integer(settings.get("ysize"))
    if (
        width is None or height is None
        or not 1 <= width <= MAX_BOARD_WIDTH
        or not 1 <= height <= MAX_BOARD_HEIGHT
        or width * height > MAX_BOARD_TILES
    ):
        return None

    try:
        terrain_rows_table = _rows_by_header(_table(savefile_lines, "terrident"))
    except _UnreadableSave:
        return None
    terrain_catalog: list[dict[str, str]] = []
    terrain_codes: set[str] = set()
    for row in terrain_rows_table:
        name = _public_text(row.get("name", ""), 80)
        code = row.get("identifier", "")
        if not name or not isinstance(code, str) or len(code) != 1 or code in terrain_codes:
            raise _UnreadableSave()
        terrain_codes.add(code)
        terrain_catalog.append({"code": code, "name": name})

    map_values = _scalars(map_lines)
    terrain_rows: list[str] = []
    altitude_rows: list[str] = []
    owner_rows: list[str] = []
    for y in range(height):
        terrain = map_values.get(f"t{y:04d}")
        altitude = map_values.get(f"alt{y:04d}")
        owners = map_values.get(f"owner{y:04d}")
        if (
            terrain is None or len(terrain) != width
            or any(code not in terrain_codes for code in terrain)
            or altitude is None or owners is None
        ):
            raise _UnreadableSave()
        altitude_values = _vector_row(altitude, width)
        parsed_altitudes: list[str] = []
        for raw in altitude_values:
            value = _integer(raw)
            if value is None or not -100_000 <= value <= 100_000:
                raise _UnreadableSave()
            parsed_altitudes.append(str(value))
        terrain_rows.append(terrain)
        altitude_rows.append(",".join(parsed_altitudes))
        owner_rows.append(_owner_run_row(_vector_row(owners, width), player_count))

    cities: list[dict[str, Any]] = []
    unit_groups: dict[tuple[int, int, int], dict[str, int]] = {}
    for player_id in range(player_count):
        player_lines = sections.get(f"player{player_id}")
        if player_lines is None:
            raise _UnreadableSave()
        player = _scalars(player_lines)
        expected = _integer(player.get("ncities"), 0)
        if expected is None or expected < 0:
            raise _UnreadableSave()
        if expected:
            city_rows = _rows_by_header(_table(player_lines, "c"))
            if len(city_rows) != expected:
                raise _UnreadableSave()
            for row in city_rows:
                x = _integer(row.get("x"))
                y = _integer(row.get("y"))
                size = _integer(row.get("size"))
                city_id = _integer(row.get("id"))
                name = _public_text(row.get("name", ""), 80)
                if (
                    x is None or y is None or size is None or city_id is None
                    or not 0 <= x < width or not 0 <= y < height
                    or size < 0 or city_id < 0 or not name
                ):
                    raise _UnreadableSave()
                cities.append({
                    "id": city_id,
                    "x": x,
                    "y": y,
                    "player_id": player_id,
                    "name": name,
                    "size": size,
                    "capital": row.get("capital") not in {None, "Not", "None", "FALSE", "0"},
                })
        expected_units = _integer(player.get("nunits"), 0)
        if expected_units is None or expected_units < 0:
            raise _UnreadableSave()
        if expected_units:
            unit_rows = _rows_by_header(_table(player_lines, "u"))
            if len(unit_rows) != expected_units:
                raise _UnreadableSave()
            for row in unit_rows:
                x = _integer(row.get("x"))
                y = _integer(row.get("y"))
                unit_type = _public_text(row.get("type_by_name", ""), 80)
                if (
                    x is None or y is None or not 0 <= x < width
                    or not 0 <= y < height or not unit_type
                ):
                    raise _UnreadableSave()
                types = unit_groups.setdefault((x, y, player_id), {})
                types[unit_type] = types.get(unit_type, 0) + 1
    cities.sort(key=lambda city: (city["y"], city["x"], city["id"]))
    unit_stacks = []
    for (x, y, player_id), types in sorted(
        unit_groups.items(), key=lambda item: (item[0][1], item[0][0], item[0][2]),
    ):
        ranked_types = sorted(types.items(), key=lambda item: (-item[1], item[0]))
        unit_stacks.append({
            "x": x,
            "y": y,
            "player_id": player_id,
            "count": sum(types.values()),
            "types": [
                {"name": name, "count": count}
                for name, count in ranked_types[:3]
            ],
        })
    terrain_catalog.sort(key=lambda terrain: terrain["code"])
    savefile = _scalars(savefile_lines)
    extras_size = _integer(savefile.get("extras_size"), 0)
    if extras_size is None or extras_size < 0 or extras_size > 256:
        raise _UnreadableSave()
    extras_vector = _csv_row(savefile.get("extras_vector", "")) if extras_size else []
    if len(extras_vector) != extras_size:
        raise _UnreadableSave()
    extras_catalog = [
        {"id": extra_id, "name": _normalize_ruleset_name(name)}
        for extra_id, name in enumerate(extras_vector)
    ]
    extra_layers: list[list[str]] = []
    for layer in range((extras_size + 3) // 4):
        rows: list[str] = []
        for y in range(height):
            value = map_values.get(f"e{layer:02d}_{y:04d}")
            if value is None or len(value) != width or not re.fullmatch(r"[0-9a-fA-F]+", value):
                raise _UnreadableSave()
            available_bits = min(4, extras_size - layer * 4)
            if any(int(character, 16) >= 1 << available_bits for character in value):
                raise _UnreadableSave()
            rows.append(value.lower())
        extra_layers.append(rows)
    return {
        "schema_version": SCHEMA_VERSION,
        "game_id": game_id,
        "turn": turn,
        "width": width,
        "height": height,
        "topology": _public_text(settings.get("topology", ""), 32),
        "wrap": _public_text(settings.get("wrap", ""), 32),
        "terrain_catalog": terrain_catalog,
        "terrain_rows": terrain_rows,
        "altitude_rows": altitude_rows,
        "owner_rows": owner_rows,
        "extras_catalog": extras_catalog,
        "extra_layers": extra_layers,
        "cities": cities,
        "unit_stacks": unit_stacks,
    }


def _parse_save(text: str, game_id: str, filename_turn: int) -> dict[str, Any]:
    sections = _sections(text)
    try:
        savefile = _scalars(sections["savefile"])
        game = _scalars(sections["game"])
        players_section = _scalars(sections["players"])
        research = _research_rows(sections["research"])
    except (KeyError, _UnreadableSave) as exc:
        raise _UnreadableSave() from exc

    options = savefile.get("options", "").split()
    version = _integer(savefile.get("version"))
    revision = savefile.get("revision", "")
    if (
        "+version3" not in options or version != SUPPORTED_SAVE_VERSION
        or not re.fullmatch(r"3(?:\.\d+)+(?:[-+._A-Za-z0-9]*)?", revision)
    ):
        raise _UnreadableSave("This autosave format is not supported by the replay reader.")

    turn = _integer(game.get("turn"))
    year = _integer(game.get("year"))
    nplayers = _integer(players_section.get("nplayers"))
    if (
        turn is None or turn < 0 or turn != filename_turn or year is None
        or nplayers is None or not 1 <= nplayers <= MAX_PLAYERS
    ):
        raise _UnreadableSave()
    vector = _technology_vector(savefile)
    ruleset = _public_text(savefile.get("rulesetdir", ""), 80)
    catalog = _technology_catalog(vector, ruleset)
    name_to_id = {
        _normalize_ruleset_name(name): tech_id for tech_id, name in enumerate(vector)
        if name not in {"A_NONE", "A_UNSET", "A_FUTURE"}
    }
    pooled = _pooled_research(sections.get("settings", []))

    player_indices = sorted(
        int(match.group(1)) for section in sections
        if (match := PLAYER_SECTION_RE.fullmatch(section))
    )
    if player_indices != list(range(nplayers)):
        raise _UnreadableSave()
    parsed_players: list[dict[str, Any]] = []
    for player_id in player_indices:
        try:
            player = _scalars(sections[f"player{player_id}"])
            score = _scalars(sections[f"score{player_id}"])
        except (KeyError, _UnreadableSave) as exc:
            raise _UnreadableSave() from exc
        player_name = _public_text(player.get("name", ""), 80)
        alive = _boolean(player.get("is_alive"))
        team_no = _integer(player.get("team_no"))
        row = research.get(team_no if pooled else player_id) if team_no is not None else None
        if not player_name or alive is None or row is None:
            raise _UnreadableSave()
        done = row["done"]
        if len(done) != len(vector):
            raise _UnreadableSave()
        known_ids = [
            tech_id for tech_id, bit in enumerate(done)
            if bit == "1" and vector[tech_id] not in {"A_NONE", "A_UNSET", "A_FUTURE"}
        ]
        now_name_raw = _normalize_ruleset_name(row.get("now_name", ""))
        research_id = name_to_id.get(now_name_raw)
        research_name = (
            "Future Tech" if now_name_raw == "A_FUTURE"
            else "" if now_name_raw in {"", "A_NONE", "A_UNSET"}
            else now_name_raw
        )
        numeric_fields = {
            "score": _integer(score.get("total")),
            "cities": _integer(player.get("ncities")),
            "units": _integer(player.get("nunits")),
            "gold": _integer(player.get("gold")),
            "culture": _integer(score.get("culture")),
            "bulbs": _integer(row.get("bulbs")),
            "future_techs": _integer(row.get("futuretech")),
        }
        if any(value is None for value in numeric_fields.values()):
            raise _UnreadableSave()
        citizens = _citizens(score)
        parsed_players.append({
            "seat_id": f"raw-{player_id}",
            "place": None,
            "player_id": player_id,
            "player_name": player_name,
            "player_color": _raw_color(player),
            "nation": _public_text(player.get("nation", ""), 80),
            "government": _public_text(player.get("government_name", ""), 80),
            "alive": alive,
            "score": numeric_fields["score"],
            "cities": numeric_fields["cities"],
            "citizens": citizens,
            "population": citizens,
            "units": numeric_fields["units"],
            "gold": numeric_fields["gold"],
            "culture": numeric_fields["culture"],
            "known_tech_ids": known_ids,
            "known_tech_names": [vector[tech_id] for tech_id in known_ids],
            "gained_tech_ids": [],
            "lost_tech_ids": [],
            "research": {
                "tech_id": research_id,
                "name": research_name,
                "bulbs": numeric_fields["bulbs"],
                "cost": 0,
            },
            "future_techs": numeric_fields["future_techs"],
            "team_no": team_no,
        })
    return {
        "snapshot": {
            "schema_version": SCHEMA_VERSION,
            "game_id": game_id,
            "turn": turn,
            "year": year,
            "players": parsed_players,
        },
        "catalog": catalog,
        "board": _parse_board(sections, game_id, turn, nplayers),
    }


def _safe_directory(path: Path, label: str, *, create: bool = False) -> Path:
    if create:
        path.mkdir(parents=True, exist_ok=True)
    try:
        info = path.lstat()
    except OSError as exc:
        raise SaveReplayError(f"{label} is unavailable.") from exc
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode):
        raise SaveReplayError(f"{label} is unavailable.")
    return path.resolve()


def _source_directory(runs_root: Path, game_id: str) -> Path:
    root = _safe_directory(runs_root, "Game run storage")
    game_directory = root / game_id
    saves = game_directory / "saves"
    _safe_directory(game_directory, "Game replay storage")
    resolved_saves = _safe_directory(saves, "Game replay storage")
    if resolved_saves.parent != game_directory.resolve() or game_directory.parent.resolve() != root:
        raise SaveReplayError("Game replay storage is unavailable.")
    return resolved_saves


def _cache_directory(
    runs_root: Path, game_id: str, source_directory: Path, cache_root: Path | None,
) -> Path:
    base = cache_root if cache_root is not None else runs_root.parent / "replay-cache"
    prospective = base.resolve(strict=False)
    if (
        prospective == source_directory
        or source_directory in prospective.parents
        or prospective in source_directory.parents
    ):
        raise SaveReplayError("Replay cache must be separate from game saves.")
    base = _safe_directory(base, "Replay cache", create=True)
    cache = _safe_directory(base / game_id, "Replay cache", create=True)
    if cache == source_directory or source_directory in cache.parents or cache in source_directory.parents:
        raise SaveReplayError("Replay cache must be separate from game saves.")
    return cache


def _discover_saves(source_directory: Path) -> dict[int, list[Path]]:
    result: dict[int, list[Path]] = {}
    try:
        entries = list(source_directory.iterdir())
    except OSError as exc:
        raise SaveReplayError("Game replay storage is unavailable.") from exc
    for path in entries:
        match = SAVE_NAME_RE.fullmatch(path.name)
        if not match:
            continue
        try:
            info = path.lstat()
        except OSError:
            continue
        if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
            continue
        result.setdefault(int(match.group(1)), []).append(path)
    for paths in result.values():
        paths.sort(key=lambda path: (
            0 if "-final.sav" in path.name else 1,
            _COMPRESSION_ORDER[Path(path.name).suffix if Path(path.name).suffix != ".sav" else ""],
            path.name,
        ))
    return dict(sorted(result.items()))


def _cache_path(cache_directory: Path, turn: int, source_name: str) -> Path:
    digest = hashlib.sha256(source_name.encode("utf-8")).hexdigest()[:12]
    return cache_directory / f"turn-{turn:010d}-{digest}.json"


def _load_cache(
    cache_path: Path,
    source_name: str,
    signature: tuple[int, int, int, int, int, int],
    game_id: str,
) -> dict[str, Any] | None:
    try:
        info = cache_path.lstat()
        if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
            return None
        value = json.loads(cache_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        return None
    if not isinstance(value, dict) or value.get("cache_version") != CACHE_VERSION:
        return None
    expected = {
        "name": source_name,
        "device": signature[0],
        "inode": signature[1],
        "size": signature[2],
        "mtime_ns": signature[3],
        "ctime_ns": signature[4],
    }
    if value.get("source") != expected or value.get("game_id") != game_id:
        return None
    parsed = value.get("parsed")
    if not isinstance(parsed, dict):
        return None
    snapshot = parsed.get("snapshot")
    catalog = parsed.get("catalog")
    board = parsed.get("board")
    players = snapshot.get("players") if isinstance(snapshot, dict) else None
    if (
        not isinstance(snapshot, dict) or snapshot.get("game_id") != game_id
        or not isinstance(snapshot.get("turn"), int)
        or not isinstance(players, list)
        or any(
            not isinstance(player, dict)
            or not isinstance(player.get("player_id"), int)
            or not isinstance(player.get("player_name"), str)
            or not isinstance(player.get("known_tech_ids"), list)
            or any(not isinstance(tech_id, int) for tech_id in player.get("known_tech_ids", []))
            or not isinstance(player.get("research"), dict)
            for player in players
        )
        or not isinstance(catalog, dict)
        or not isinstance(catalog.get("technologies"), list)
        or board is not None and not _cached_board_is_valid(board, game_id)
    ):
        return None
    return parsed


def _cached_board_is_valid(value: Any, game_id: str) -> bool:
    if not isinstance(value, dict) or value.get("game_id") != game_id:
        return False
    width = value.get("width")
    height = value.get("height")
    if (
        isinstance(width, bool) or not isinstance(width, int)
        or isinstance(height, bool) or not isinstance(height, int)
        or not 1 <= width <= MAX_BOARD_WIDTH
        or not 1 <= height <= MAX_BOARD_HEIGHT
        or width * height > MAX_BOARD_TILES
    ):
        return False
    terrain_rows = value.get("terrain_rows")
    altitude_rows = value.get("altitude_rows")
    owner_rows = value.get("owner_rows")
    extra_layers = value.get("extra_layers")
    cities = value.get("cities")
    unit_stacks = value.get("unit_stacks")
    return (
        isinstance(value.get("turn"), int)
        and isinstance(terrain_rows, list) and len(terrain_rows) == height
        and all(isinstance(row, str) and len(row) == width for row in terrain_rows)
        and isinstance(altitude_rows, list) and len(altitude_rows) == height
        and all(isinstance(row, str) for row in altitude_rows)
        and isinstance(owner_rows, list) and len(owner_rows) == height
        and all(isinstance(row, str) for row in owner_rows)
        and isinstance(extra_layers, list)
        and all(
            isinstance(layer, list) and len(layer) == height
            and all(isinstance(row, str) and len(row) == width for row in layer)
            for layer in extra_layers
        )
        and isinstance(cities, list)
        and all(
            isinstance(city, dict)
            and isinstance(city.get("x"), int) and 0 <= city["x"] < width
            and isinstance(city.get("y"), int) and 0 <= city["y"] < height
            and isinstance(city.get("player_id"), int)
            and isinstance(city.get("name"), str)
            for city in cities
        )
        and isinstance(unit_stacks, list)
        and all(
            isinstance(stack, dict)
            and isinstance(stack.get("x"), int) and 0 <= stack["x"] < width
            and isinstance(stack.get("y"), int) and 0 <= stack["y"] < height
            and isinstance(stack.get("player_id"), int)
            and isinstance(stack.get("count"), int) and stack["count"] > 0
            and isinstance(stack.get("types"), list)
            for stack in unit_stacks
        )
    )


def _write_cache(
    cache_path: Path,
    source_name: str,
    signature: tuple[int, int, int, int, int, int],
    game_id: str,
    parsed: Mapping[str, Any],
) -> None:
    payload = {
        "cache_version": CACHE_VERSION,
        "game_id": game_id,
        "source": {
            "name": source_name,
            "device": signature[0],
            "inode": signature[1],
            "size": signature[2],
            "mtime_ns": signature[3],
            "ctime_ns": signature[4],
        },
        "parsed": parsed,
    }
    temporary_name: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=cache_path.parent,
            prefix=".replay-", suffix=".tmp", delete=False,
        ) as stream:
            temporary_name = stream.name
            os.chmod(stream.name, 0o600)
            json.dump(payload, stream, ensure_ascii=False, separators=(",", ":"), sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_name, cache_path)
        temporary_name = None
    except OSError:
        pass
    finally:
        if temporary_name is not None:
            try:
                os.unlink(temporary_name)
            except OSError:
                pass


def _load_candidate(
    source: Path, turn: int, game_id: str, cache_directory: Path,
) -> dict[str, Any]:
    try:
        info = source.lstat()
    except OSError as exc:
        raise _UnreadableSave() from exc
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
        raise _UnreadableSave()
    signature = _stat_signature(info)
    cache_path = _cache_path(cache_directory, turn, source.name)
    cached = _load_cache(cache_path, source.name, signature, game_id)
    if cached is not None:
        return cached
    text, stable_signature = _read_stable_save(source)
    parsed = _parse_save(text, game_id, turn)
    _write_cache(cache_path, source.name, stable_signature, game_id, parsed)
    return parsed


def _load_turn(
    sources: Sequence[Path], turn: int, game_id: str, cache_directory: Path,
) -> tuple[dict[str, Any] | None, str | None]:
    warning: str | None = None
    for source in sources:
        try:
            return _load_candidate(source, turn, game_id, cache_directory), warning
        except _UnreadableSave as exc:
            warning = exc.public_message
    return None, warning


def _discover_ppm_paths(source_directory: Path) -> dict[int, Path]:
    candidates: dict[int, list[Path]] = {}
    try:
        entries = source_directory.iterdir()
    except OSError:
        return {}
    for path in entries:
        match = PPM_NAME_RE.fullmatch(path.name)
        if match:
            try:
                info = path.lstat()
            except OSError:
                continue
            if stat.S_ISREG(info.st_mode) and not stat.S_ISLNK(info.st_mode):
                candidates.setdefault(int(match.group(1)), []).append(path)
    return {
        turn: sorted(paths, key=lambda item: item.name)[-1]
        for turn, paths in candidates.items()
    }


def _ppm_players(path: Path | None) -> dict[int, tuple[str, str]]:
    if path is None:
        return {}
    try:
        descriptor = os.open(
            path, os.O_RDONLY | getattr(os, "O_CLOEXEC", 0)
            | getattr(os, "O_NOFOLLOW", 0),
        )
        parsed: dict[int, tuple[str, str]] = {}
        with os.fdopen(descriptor, "rb") as stream:
            before = os.fstat(stream.fileno())
            if not stat.S_ISREG(before.st_mode):
                return {}
            magic = stream.readline(32)
            if magic.rstrip(b"\r\n") not in {b"P3", b"P6"}:
                return {}
            consumed = len(magic)
            for _ in range(512):
                line_bytes = stream.readline(4096)
                consumed += len(line_bytes)
                if not line_bytes or consumed > 64 * 1024:
                    break
                try:
                    line = line_bytes.decode("utf-8", errors="strict").rstrip("\r\n")
                except UnicodeError:
                    return {}
                if not line.startswith("#"):
                    break
                match = PPM_PLAYER_RE.fullmatch(line)
                if not match:
                    continue
                red, green, blue = (int(match.group(index)) for index in (2, 3, 4))
                if any(not 0 <= value <= 255 for value in (red, green, blue)):
                    continue
                try:
                    name = _public_text(_scalar_value(f'"{match.group(5)}"'), 80)
                except _UnreadableSave:
                    continue
                parsed[int(match.group(1))] = (name, f"#{red:02X}{green:02X}{blue:02X}")
            after = os.fstat(stream.fileno())
        return parsed if _stat_signature(before) == _stat_signature(after) else {}
    except OSError:
        return {}


def _journal_seat_ids(source_directory: Path) -> dict[int, str]:
    """player_id -> seat_id as recorded by the live replay producer.

    The native save renames agent-controlled players to their rulers
    ("AgentPlace1" becomes "Ada"), so reconstructing identity from the save
    by name misses exactly the configured seats and demotes them to dynamic
    factions. The run's replay journal was written by the running
    supervisor, which knew every player's real seat; its last parseable row
    (the tail may be a torn write) is the final word.
    """
    # source_directory is the validated saves/ subdirectory; the journal
    # sits beside it in the run root.
    path = source_directory.parent / "replay.jsonl"
    try:
        with path.open("rb") as handle:
            handle.seek(0, os.SEEK_END)
            size = handle.tell()
            if size <= 0:
                return {}
            handle.seek(max(0, size - 262144))
            lines = handle.read().splitlines()
    except OSError:
        return {}
    for raw in reversed(lines):
        if not raw.strip():
            continue
        try:
            row = json.loads(raw)
        except (UnicodeError, json.JSONDecodeError):
            continue
        players = row.get("players") if isinstance(row, dict) else None
        if not isinstance(players, list):
            return {}
        mapping: dict[int, str] = {}
        for player in players:
            if not isinstance(player, Mapping):
                continue
            player_id = player.get("player_id")
            seat_id = player.get("seat_id")
            # Older journals recorded ids as floats (0.0); an integral
            # float is the same player.
            if isinstance(player_id, float) and player_id.is_integer():
                player_id = int(player_id)
            if isinstance(player_id, int) and not isinstance(player_id, bool) \
                    and isinstance(seat_id, str):
                mapping[player_id] = seat_id
        return mapping
    return {}


def _enrich_snapshot(
    raw_snapshot: Mapping[str, Any],
    resolved_places: Sequence[Mapping[str, Any]],
    ppm_players: Mapping[int, tuple[str, str]],
    journal_seats: Mapping[int, str] | None = None,
) -> dict[str, Any]:
    configured = {
        row.get("player_name"): row for row in resolved_places
        if isinstance(row, Mapping) and isinstance(row.get("player_name"), str)
    }
    configured_by_seat = {
        row.get("seat_id"): row for row in resolved_places
        if isinstance(row, Mapping) and isinstance(row.get("seat_id"), str)
    }
    snapshot = json.loads(json.dumps(raw_snapshot))
    players = []
    for player in snapshot["players"]:
        ppm = ppm_players.get(player["player_id"])
        if ppm is not None and ppm[0] == player["player_name"]:
            player["player_color"] = ppm[1]
        identity = configured.get(player["player_name"])
        if identity is None and journal_seats:
            identity = configured_by_seat.get(
                journal_seats.get(player["player_id"]),
            )
        if identity is not None:
            player.update({
                "seat_id": identity.get("seat_id", f"raw-{player['player_id']}"),
                "place": identity.get("place"),
                # Save/PPM metadata is authoritative for the actual player;
                # configured color is only a legacy fallback.
                "player_color": player.get("player_color") or identity.get("player_color"),
                "controller_label": identity.get("controller_label"),
                "controller_type": identity.get("controller_type"),
                "model": identity.get("model"),
                "scored": True,
            })
        else:
            player.update({
                "seat_id": f"dynamic-player-{player['player_id']}",
                "place": None,
                "controller_label": "Freeciv dynamic faction",
                "controller_type": "dynamic",
                "model": None,
                "scored": False,
            })
        players.append(player)
    players.sort(key=lambda row: (
        row["place"] is None,
        row["place"] if row["place"] is not None else row["player_id"],
        row["player_name"],
    ))
    snapshot["players"] = players
    return snapshot


def replay_from_autosaves(
    runs_root: str | os.PathLike[str],
    game_id: str,
    resolved_places: Sequence[Mapping[str, Any]] = (),
    *,
    after_turn: int = 0,
    limit: int = 250,
    cache_root: str | os.PathLike[str] | None = None,
    complete: bool = False,
) -> dict[str, Any]:
    """Return a paginated ReplayResponse reconstructed from stable autosaves.

    Only the closest predecessor, the requested page, and one look-ahead
    snapshot are parsed (or loaded from cache).  Corrupt/partial candidates are
    skipped without exposing filesystem paths in public warnings.
    """
    if not isinstance(game_id, str) or not GAME_ID_RE.fullmatch(game_id):
        raise SaveReplayError("Invalid game id.")
    if isinstance(after_turn, bool) or not isinstance(after_turn, int) or after_turn < 0:
        raise SaveReplayError("after_turn must be a non-negative integer.")
    if isinstance(limit, bool) or not isinstance(limit, int) or not 1 <= limit <= 250:
        raise SaveReplayError("limit must be in [1, 250].")
    if not isinstance(resolved_places, Sequence) or isinstance(resolved_places, (str, bytes)):
        raise SaveReplayError("resolved_places must be a sequence.")

    runs_path = Path(runs_root)
    source_directory = _source_directory(runs_path, game_id)
    cache_path = _cache_directory(
        runs_path, game_id, source_directory,
        Path(cache_root) if cache_root is not None else None,
    )
    sources_by_turn = _discover_saves(source_directory)
    ppm_paths = _discover_ppm_paths(source_directory)
    warnings: list[dict[str, Any]] = []
    parsed_by_turn: dict[int, dict[str, Any]] = {}

    def load(turn: int) -> dict[str, Any] | None:
        if turn in parsed_by_turn:
            return parsed_by_turn[turn]
        parsed, warning = _load_turn(sources_by_turn[turn], turn, game_id, cache_path)
        if warning:
            warnings.append({"turn": turn, "message": warning})
        if parsed is not None:
            parsed_by_turn[turn] = parsed
        return parsed

    predecessor: dict[str, Any] | None = None
    predecessor_probes = 0
    for turn in reversed([value for value in sources_by_turn if value <= after_turn]):
        if predecessor_probes >= MAX_PREDECESSOR_PROBES:
            warnings.append({
                "turn": None,
                "message": "Prior technology state was unavailable for this replay page.",
            })
            break
        predecessor_probes += 1
        predecessor = load(turn)
        if predecessor is not None:
            break

    forward: list[dict[str, Any]] = []
    forward_probes = 0
    last_forward_turn = after_turn
    scan_limited = False
    # One candidate per requested row, one lookahead, and a bounded allowance
    # for incomplete saves.  A request never walks an unbounded old corpus.
    max_forward_probes = limit + MAX_PREDECESSOR_PROBES + 1
    for turn in (value for value in sources_by_turn if value > after_turn):
        if forward_probes >= max_forward_probes:
            scan_limited = True
            warnings.append({
                "turn": None,
                "message": "Replay scanning stopped at the safe per-request limit.",
            })
            break
        forward_probes += 1
        last_forward_turn = turn
        parsed = load(turn)
        if parsed is not None:
            forward.append(parsed)
            if len(forward) > limit:
                break

    selected = forward[:limit]
    catalog_source = selected[-1] if selected else predecessor
    previous_known: dict[str, set[int]] = {}
    journal_seats = _journal_seat_ids(source_directory)
    if predecessor is not None:
        enriched_predecessor = _enrich_snapshot(
            predecessor["snapshot"], resolved_places,
            _ppm_players(ppm_paths.get(predecessor["snapshot"]["turn"])),
            journal_seats,
        )
        previous_known = {
            player["seat_id"]: set(player["known_tech_ids"])
            for player in enriched_predecessor["players"]
        }

    snapshots: list[dict[str, Any]] = []
    for parsed in selected:
        turn = parsed["snapshot"]["turn"]
        snapshot = _enrich_snapshot(
            parsed["snapshot"], resolved_places,
            _ppm_players(ppm_paths.get(turn)),
            journal_seats,
        )
        for player in snapshot["players"]:
            current = set(player["known_tech_ids"])
            previous = previous_known.get(player["seat_id"], set())
            player["gained_tech_ids"] = sorted(current - previous)
            player["lost_tech_ids"] = sorted(previous - current)
            previous_known[player["seat_id"]] = current
        snapshots.append(snapshot)

    unique_warnings = {
        (warning.get("turn"), warning["message"]): warning for warning in warnings
    }
    sanitized_warnings = sorted(
        unique_warnings.values(),
        key=lambda warning: (
            warning.get("turn") is None,
            warning.get("turn") if warning.get("turn") is not None else 0,
            warning["message"],
        ),
    )[-100:]
    next_after_turn = snapshots[-1]["turn"] if snapshots else after_turn
    if scan_limited:
        # Every probed turn has either been returned or was unreadable.  Let a
        # client advance over that corrupt window instead of getting stuck.
        next_after_turn = max(next_after_turn, last_forward_turn)
    return {
        "schema_version": SCHEMA_VERSION,
        "game_id": game_id,
        "available": bool(snapshots or predecessor),
        "catalog": catalog_source["catalog"] if catalog_source else None,
        "snapshots": snapshots,
        "next_after_turn": next_after_turn,
        "has_more": len(forward) > limit or scan_limited,
        "complete": bool(complete),
        "replay_warnings": sanitized_warnings,
    }


def board_from_autosave(
    runs_root: str | os.PathLike[str],
    game_id: str,
    resolved_places: Sequence[Mapping[str, Any]] = (),
    *,
    turn: int,
    cache_root: str | os.PathLike[str] | None = None,
) -> dict[str, Any]:
    """Return one compact semantic spectator board for an exact saved turn.

    This deliberately stays separate from replay pagination: even a 5,000-turn
    match transfers only the board currently selected by the spectator.  The
    source save is opened read-only and derived cache data stays outside the run
    artifact directory.
    """
    if not isinstance(game_id, str) or not GAME_ID_RE.fullmatch(game_id):
        raise SaveReplayError("Invalid game id.")
    if isinstance(turn, bool) or not isinstance(turn, int) or turn < 0:
        raise SaveReplayError("turn must be a non-negative integer.")
    if not isinstance(resolved_places, Sequence) or isinstance(resolved_places, (str, bytes)):
        raise SaveReplayError("resolved_places must be a sequence.")

    runs_path = Path(runs_root)
    source_directory = _source_directory(runs_path, game_id)
    cache_path = _cache_directory(
        runs_path, game_id, source_directory,
        Path(cache_root) if cache_root is not None else None,
    )
    sources = _discover_saves(source_directory).get(turn)
    if not sources:
        raise FileNotFoundError("semantic board turn not found")
    parsed, _warning = _load_turn(sources, turn, game_id, cache_path)
    if parsed is None or not _cached_board_is_valid(parsed.get("board"), game_id):
        raise FileNotFoundError("semantic board unavailable")

    snapshot = _enrich_snapshot(
        parsed["snapshot"], resolved_places, {},
        _journal_seat_ids(source_directory),
    )
    board = json.loads(json.dumps(parsed["board"]))
    board["players"] = [{
        "player_id": player["player_id"],
        "player_name": player["player_name"],
        "player_color": player.get("player_color"),
        "seat_id": player.get("seat_id"),
        "place": player.get("place"),
        "controller_label": player.get("controller_label"),
        "controller_type": player.get("controller_type"),
        "model": player.get("model"),
        "nation": player.get("nation", ""),
        "scored": player.get("scored") is True,
    } for player in snapshot["players"]]
    return board


build_replay_response = replay_from_autosaves


__all__ = [
    "SaveReplayError", "board_from_autosave", "build_replay_response",
    "replay_from_autosaves",
]
