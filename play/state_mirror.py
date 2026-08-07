"""Local state mirror (layer L1) for the full-control-v2 player client.

`docs/full-control-v2-context-redesign.md` §3/§5-P1: the session directory
gains a CLI-maintained *sanctioned projection* of the pages the seat has
already received.  The raw `.v2-state` cache stays private; these files are
plain text an agent can `grep`/`Read` with zero network traffic and zero
context cost until it chooses to look.

Files written under `mirror_dir(session_path)`::

    state/header.txt        objective, budget, timing, protocol card
    state/phase.json        whose turn it is, machine-readable (see below)
    state/overview.tsv      economy, research, government - one row per fact
    state/units.tsv         one row per unit
    state/cities.tsv        one row per city
    state/diplomacy.tsv     one row per relation, meetings included
    state/map.txt           ASCII terrain grid, fog as '?'
    state/options/<a>.txt   rendered legal-action catalog for one actor
    state/delta.md          short prose digest of what changed
    cache/nations.tsv       static catalogs, fetched once at join
    cache/styles.tsv
    cache/governments.tsv

Integration (one line per ingest site, all of them pure functions)::

    from pathlib import Path
    import state_mirror

    mirror = state_mirror.mirror_dir(session_path)

    # after `_remember_page(path, state, page, legal=False)`
    state_mirror.update_from_page(mirror, "state", page)

    # after `_remember_page(path, state, page, legal=True)`
    state_mirror.update_from_page(mirror, "legal", page)

    # after `_remember_receipt(path, state, receipt)`
    state_mirror.update_from_receipt(mirror, "batch", receipt)

    # after `_validate_health(...)` / the health half of a wait response
    state_mirror.update_from_health(mirror, "turn", health, revision=rev)

Every entry point takes the *already validated* payload - the object returned
by `client._validate_page`, `client._validate_receipt` or
`client._validate_health`.  Nothing here re-parses the wire: the mirror is a
renderer over validated objects, so a contract drift surfaces as a missing
row, never as a silently blanked value.  A fact the payload did not carry is
either omitted (key/value files) or rendered as `-` (fixed-column tables);
no renderer ever substitutes a plausible default.

Guarantees this module keeps:

* Every file begins with `# rev N turn T` - the revision it reflects.  A page
  older than the file on disk is ignored, so a mirror file never goes
  backwards.  `# rev ?` appears only for a header written before any page has
  been ingested (lobby health).
* Writes are atomic and private: they go through
  `client._write_private_text` (temp file + `os.replace`, mode 0600, no
  symlink following, inside `PLAY_STATE_DIR`).  A failure mid-write leaves the
  previous file untouched and no partial file behind.
* Projections only.  Renderers are allowlists over known payload fields, so an
  unexpected field - a bearer token, an invite, a `state_token` - can never
  reach a file.  `state_token` is deliberately never rendered; only the
  `turn`/`revision` counters are.
* Cell values are sanitized (control characters stripped, length capped), so a
  server-supplied name can never forge a `# rev` header line or a table row.

Merging.  A page is a *window*, so page 2 of a unit catalog must not erase
page 1: a page carrying the mirror's own revision merges into the file (rows
keyed by column 0, tiles into the grid), while a newer revision replaces it.
The `# <section> N/T complete|partial` note says how much of the section the
file holds.  Each entry point is a read-modify-write of the files it touches,
so call it from inside the session's request lock, as every v2 command
already holds.

Aliases.  Column 0 of `units.tsv`/`cities.tsv`, the `options/<alias>.txt`
filename and the `a1..aN` action names come from the `aliases` argument -
a mapping of opaque id -> short alias owned by the CLI alias layer (P0.3).
Pass it so both surfaces agree.  Without it the mirror falls back to a stable
local handle (`u.3f9a2c`) for entities; an action the map does not name keeps
`-` in its alias column.  The mirror never *mints* an `aN` name: `a1..aN`
live in the CLI's revision-scoped namespace, so inventing one here would
print a handle that resolves to a different capability - the silent-wrong-
action failure the opaque IDs exist to kill.  A fallback entity handle is a
*display key only*: it is not an alias any command accepts, so using one
fails closed exactly like a stale alias.  Supply either a complete alias map
for a page or none at all; a partial map mixes the two naming schemes in one
file.

`state/phase.json`.  Every other file here is prose or a table, which means a
watcher has to regex it.  The one question a multiplayer harness asks most --
"is it my turn yet, and if not, whose is it and how long have they had it" --
gets a sanctioned JSON answer instead, written by `update_from_health` on
every command that reads health *and* on every internal poll of a blocking
wait, so it stays fresh while the client is blocked rather than freezing for
the whole ten minutes.  Its shape is closed and versioned::

    {"schema_version": 1, "updated_at": 1786058960.4, "turn": 5, "phase": 0,
     "active": false, "game_state": "running", "state": "awaiting_agent",
     "held_s": 587.0, "deadline_s_left": 13.0,
     "holder": {"seat_id": "place-1", "player_name": "AgentPlace1",
                "controller_label": "pi-gpt-5.6-sol", "place": 1}}

`holder` is null when this seat holds the phase or when no seat does.  Like
every file here the write is atomic, so a watcher polling it never reads a
half-written object.
"""

from __future__ import annotations

import json
import re
import sys
import time
from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import Any

__all__ = [
    "append_monitor_log",
    "mirror_dir",
    "read_phase_marker",
    "render_map_yields",
    "update_from_page",
    "update_from_receipt",
    "update_from_health",
    "update_phase_marker",
]

_MAX_CELL = 64
_MAX_DELTA_LINES = 12
_MAX_ROWS = 4096
_CONTROL_RE = re.compile(r"[\x00-\x1f\x7f]")
_HANDLE_WIDTHS = (6, 10, 32)

_STATE_DIR = "state"
_CACHE_DIR = "cache"

_SECTION_TARGETS: dict[str, tuple[str, ...]] = {
    "overview": (_STATE_DIR, "overview.tsv"),
    "units": (_STATE_DIR, "units.tsv"),
    "cities": (_STATE_DIR, "cities.tsv"),
    "diplomacy": (_STATE_DIR, "diplomacy.tsv"),
    "known_tiles": (_STATE_DIR, "map.txt"),
    "map_tiles": (_STATE_DIR, "map.txt"),
    "tile_window": (_STATE_DIR, "map.txt"),
    "pregame_nations": (_CACHE_DIR, "nations.tsv"),
    "pregame_styles": (_CACHE_DIR, "styles.tsv"),
    "governments": (_CACHE_DIR, "governments.tsv"),
}
_HEADER_FILE = (_STATE_DIR, "header.txt")
_PHASE_FILE = (_STATE_DIR, "phase.json")
_PHASE_SCHEMA_VERSION = 1
_MONITOR_LOG_FILE = (_STATE_DIR, "monitor.log")
_MAX_LOG_LINE = 512
_DELTA_FILE = (_STATE_DIR, "delta.md")
_OVERVIEW_FILE = (_STATE_DIR, "overview.tsv")
_MAP_FILE = (_STATE_DIR, "map.txt")
_YIELD_FILE = (_STATE_DIR, "yields.tsv")
_YIELD_COLUMNS = ("tile", "food", "shields", "trade", "worked", "city")

_SECTION_TITLES = {
    "overview": "overview",
    "units": "units",
    "cities": "cities",
    "diplomacy": "diplomacy",
    "known_tiles": "map",
    "map_tiles": "map",
    "tile_window": "map",
    "pregame_nations": "nations",
    "pregame_styles": "styles",
    "governments": "governments",
}

# Stable, documented one-character terrain codes.  Uppercase = currently
# visible, lowercase = remembered through fog, '?' = never seen.
_TERRAIN_CHARS = {
    "Arctic": "A",
    "Deep Ocean": "B",
    "Desert": "D",
    "Forest": "F",
    "Glacier": "C",
    "Grassland": "G",
    "Hills": "H",
    "Inaccessible": "I",
    "Jungle": "J",
    "Lake": "L",
    "Mountains": "M",
    "Ocean": "O",
    "Plains": "P",
    "Swamp": "S",
    "Tundra": "T",
}
_TERRAIN_FALLBACK = "ENRUVWXYZ123456789"

_DEFAULT_PROBABILITY = {
    "kind": "exact", "minimum_percent": 100, "maximum_percent": 100,
}

_DEFAULT_COMMAND_CARD = (
    "just turn                              turn briefing",
    "just state --section SECTION           one state section",
    "just legal --actor_id ACTOR_ID         one actor's option catalog",
    "just batch --action_id ID [--arguments JSON]",
    "just wait                              block until the next phase",
)

_MISSING: Any = object()


# --------------------------------------------------------------------------
# client module access
# --------------------------------------------------------------------------

def _client() -> Any:
    """Return the running player client module.

    `client.py` is executed both as a script (module `__main__`) and imported
    as `client` by the tests.  Importing it again would create a second copy
    whose `PlayerError` class is a *different* class, so `main()` would stop
    catching the errors raised from here.  Resolve the live module instead.
    """
    module = sys.modules.get("client")
    if module is None:
        main = sys.modules.get("__main__")
        filename = getattr(main, "__file__", None)
        if main is not None and filename and Path(filename).name == "client.py":
            module = main
    if module is None:  # pragma: no cover - only when used as a library
        import client as module  # noqa: PLC0415
    return module


def _error(message: str) -> Exception:
    return _client().PlayerError(f"state mirror: {message}")


# --------------------------------------------------------------------------
# private, atomic file access
# --------------------------------------------------------------------------

def _write(session_dir: Path, relative: Sequence[str], text: str) -> Path:
    if not text.endswith("\n"):
        text += "\n"
    return _client()._write_private_text(
        Path(session_dir).joinpath(*relative), text,
    )


def _read(session_dir: Path, relative: Sequence[str]) -> str | None:
    """Return a mirror file's text, or None when it is absent or unreadable."""
    try:
        return _client()._read_private_text(
            Path(session_dir).joinpath(*relative), "state mirror file",
        )
    except (RuntimeError, OSError):
        return None


# --------------------------------------------------------------------------
# small value helpers
# --------------------------------------------------------------------------

def _dig(value: Any, *path: str) -> Any:
    for key in path:
        if not isinstance(value, Mapping) or key not in value:
            return _MISSING
        value = value[key]
    return value


def _cell(value: Any) -> str:
    """Render one table cell: sanitized, capped, never empty."""
    if value is _MISSING or value is None:
        return "-"
    if value is True:
        return "yes"
    if value is False:
        return "no"
    if isinstance(value, float) and value.is_integer():
        value = int(value)
    text = _CONTROL_RE.sub(" ", str(value)).strip()
    text = re.sub(r"\s{2,}", " ", text)
    if len(text) > _MAX_CELL:
        text = text[: _MAX_CELL - 1] + "~"
    return text or "-"


def _pair(first: Any, second: Any, separator: str = "/") -> str:
    if first is _MISSING or second is _MISSING:
        return "-"
    return f"{_cell(first)}{separator}{_cell(second)}"


def _position(item: Mapping[str, Any]) -> str:
    return _pair(_dig(item, "x"), _dig(item, "y"), ",")


def _handle(kind: str, identifier: Any, width: int) -> str:
    text = _cell(identifier)
    digits = re.sub(r"^[a-z_]+_", "", text)
    return f"{kind}.{digits[:width]}" if digits else text


def _alias_map(
    kind: str,
    identifiers: Sequence[Any],
    aliases: Mapping[str, str] | None,
) -> dict[str, str]:
    """Map opaque ids to short names, preferring the CLI's alias layer."""
    resolved: dict[str, str] = {}
    unresolved: list[str] = []
    for identifier in identifiers:
        text = _cell(identifier)
        if aliases is not None and text in aliases:
            resolved[text] = _cell(aliases[text])
        elif text not in resolved:
            unresolved.append(text)
    for width in _HANDLE_WIDTHS:
        candidates = {text: _handle(kind, text, width) for text in unresolved}
        if len(set(candidates.values())) == len(candidates):
            resolved.update(candidates)
            break
    else:  # pragma: no cover - identical ids cannot collide
        resolved.update({text: text for text in unresolved})
    return resolved


def _file_name(alias: str) -> str:
    """Reduce an alias to a safe single path component."""
    name = re.sub(r"[^A-Za-z0-9._-]", "_", alias).strip("._-")
    if not name:
        raise _error("an actor alias has no usable file name")
    return name[:64]


def _revision_pair(revision: Any) -> tuple[int, int]:
    turn = _dig(revision, "turn")
    number = _dig(revision, "revision")
    if type(turn) is not int or type(number) is not int:
        raise _error("payload carries no state revision counters")
    return turn, number


def _rev_line(revision: tuple[int, int] | None) -> str:
    if revision is None:
        return "# rev ? turn ?"
    turn, number = revision
    return f"# rev {number} turn {turn}"


def _newer(current: tuple[int, int], prior: tuple[int, int] | None) -> bool:
    return prior is None or (current[1], current[0]) > (prior[1], prior[0])


def _stale(current: tuple[int, int], prior: tuple[int, int] | None) -> bool:
    """True when the mirror already holds a strictly newer revision."""
    return prior is not None and current != prior and not _newer(current, prior)


# --------------------------------------------------------------------------
# table read/write
# --------------------------------------------------------------------------

class _Table:
    """A parsed mirror table: its revision, column names and rows."""

    __slots__ = ("revision", "columns", "rows")

    def __init__(
        self,
        revision: tuple[int, int] | None,
        columns: tuple[str, ...],
        rows: tuple[tuple[str, ...], ...],
    ) -> None:
        self.revision = revision
        self.columns = columns
        self.rows = rows

    def by_key(self) -> dict[str, tuple[str, ...]]:
        return {row[0]: row for row in self.rows if row}


_REV_LINE_RE = re.compile(r"^# rev (\d+) turn (\d+)\b")


def _parse_table(text: str | None) -> _Table:
    if not text:
        return _Table(None, (), ())
    lines = text.splitlines()
    revision: tuple[int, int] | None = None
    match = _REV_LINE_RE.match(lines[0]) if lines else None
    if match is not None:
        revision = (int(match.group(2)), int(match.group(1)))
    body = [line for line in lines if line and not line.startswith("#")]
    if not body:
        return _Table(revision, (), ())
    columns = tuple(part.strip() for part in body[0].split("\t"))
    rows = tuple(
        tuple(part.strip() for part in line.split("\t"))
        for line in body[1:]
    )
    return _Table(revision, columns, rows)


def _table_text(
    revision: tuple[int, int] | None,
    notes: Sequence[str],
    columns: Sequence[str],
    rows: Sequence[Sequence[str]],
) -> str:
    widths = [len(name) for name in columns]
    for row in rows:
        for index, value in enumerate(row[: len(widths)]):
            widths[index] = max(widths[index], len(value))

    def render(values: Sequence[str]) -> str:
        padded = [
            value.ljust(widths[index]) if index < len(widths) else value
            for index, value in enumerate(values)
        ]
        return "\t".join(padded).rstrip()

    lines = [_rev_line(revision)]
    lines.extend(f"# {note}" for note in notes)
    lines.append(render(list(columns)))
    lines.extend(render(list(row)) for row in rows)
    return "\n".join(lines) + "\n"


def _page_note(section: str, shown: int, total: Any, complete: bool) -> str:
    total_text = _cell(total)
    state = "complete" if complete else "partial - fetch the next cursor"
    return f"{section} {shown}/{total_text} {state}"


# --------------------------------------------------------------------------
# section renderers
# --------------------------------------------------------------------------

def _render_overview(
    items: Sequence[Any], aliases: Mapping[str, str] | None,
) -> tuple[tuple[str, ...], list[list[str]]]:
    columns = ("fact", "value")
    rows: list[list[str]] = []
    if not items:
        return columns, rows
    item = items[0]
    if not isinstance(item, Mapping):
        raise _error("overview item is not an object")

    def add(name: str, value: Any) -> None:
        if value is not _MISSING:
            rows.append([name, _cell(value)])

    add("client_state", _dig(item, "client_state"))
    add("turn", _dig(item, "turn"))
    phase = _dig(item, "phase")
    if phase is not _MISSING:
        rows.append(["phase", _pair(phase, _dig(item, "phase_count"))])
    add("phase_ready", _dig(item, "phase_ready"))
    width = _dig(item, "map", "width")
    if width is not _MISSING:
        rows.append(["map", _pair(width, _dig(item, "map", "height"), "x")])
    add("topology", _dig(item, "map", "topology"))
    player = _dig(item, "player")
    if player is None:
        rows.append(["player", "(none yet)"])
    elif player is not _MISSING:
        add("player", _dig(player, "name"))
        add("nation", _dig(player, "nation"))
        add("government", _dig(player, "government"))
        add("government_status", _dig(player, "government_state", "status"))
        add("alive", _dig(player, "alive"))
        add("phase_done", _dig(player, "phase_done"))
        add("gold", _dig(player, "economy", "gold"))
        tax = _dig(player, "economy", "tax")
        if tax is not _MISSING:
            rows.append([
                "rates",
                "tax{} lux{} sci{}".format(
                    _cell(tax),
                    _cell(_dig(player, "economy", "luxury")),
                    _cell(_dig(player, "economy", "science")),
                ),
            ])
        add("max_rate", _dig(player, "economy", "max_rate"))
    research = _dig(item, "research")
    if research is None:
        rows.append(["research", "(none yet)"])
    elif research is not _MISSING:
        add("research", _dig(research, "target"))
        bulbs = _dig(research, "bulbs_researched")
        if bulbs is not _MISSING:
            rows.append(["bulbs", _pair(bulbs, _dig(research, "cost"))])
        add("research_output", _dig(research, "output"))
        add("research_goal", _dig(research, "goal"))
        add("techs_researched", _dig(research, "techs_researched"))
        add("future_tech", _dig(research, "future_tech"))
    score = _dig(item, "score")
    if isinstance(score, Mapping):
        # The evaluated objective belongs in the projection the delta digest
        # diffs, so a turn's effect on it shows up as `score 17 -> 19` for
        # free.  `exact` leads when the boundary carries it; otherwise the
        # provable lower bound does, marked so it cannot be misread.
        exact = _dig(score, "exact")
        add(
            "score",
            _cell(exact) if isinstance(exact, int) and not isinstance(
                exact, bool,
            ) else f">={_cell(_dig(score, 'lower_bound'))}",
        )
        components = _dig(score, "components")
        if isinstance(components, Mapping):
            add("score_from", " ".join(
                f"{name} {_cell(value)}"
                for name, value in components.items() if value
            ) or "-")
    counts = _dig(item, "counts")
    if isinstance(counts, Mapping):
        # `chat` is the typed event feed (tech learned, city growth, huts).
        # Recording its total is what lets the next briefing say how many
        # events arrived without reading the feed itself.
        for name in ("cities", "units", "known_tiles", "legal_actions", "chat"):
            add(f"count_{name}", _dig(counts, name))
    return columns, rows


def _render_units(
    items: Sequence[Any], aliases: Mapping[str, str] | None,
) -> tuple[tuple[str, ...], list[list[str]]]:
    columns = ("alias", "unit", "who", "pos", "moves", "hp", "activity", "orders")
    names = _alias_map(
        "u", [_dig(item, "id") for item in items], aliases,
    )
    rows: list[list[str]] = []
    for item in items:
        if not isinstance(item, Mapping):
            raise _error("unit item is not an object")
        route = _dig(item, "route")
        orders = "-"
        if isinstance(route, Mapping):
            destination = _dig(route, "destination")
            where = (
                _position(destination)
                if isinstance(destination, Mapping) else "?"
            )
            # Same route summary the CLI's unit tables print, so a row read
            # here and a row read there say the same thing: where the unit is
            # walking and how many path steps of it are left.
            count = _dig(route, "path_step_count")
            if _dig(route, "path_available") is False or count is _MISSING:
                count = _dig(route, "order_count")
            orders = "→({}) {}st".format(
                where, "?" if count is _MISSING else _cell(count),
            )
            mode = _dig(route, "mode")
            if mode is not _MISSING and mode not in (None, "goto"):
                orders += " " + _cell(mode)
        activity = _cell(_dig(item, "activity", "name"))
        target = _dig(item, "activity", "target", "name")
        if activity != "-" and target is not _MISSING and target is not None:
            activity = f"{activity}:{_cell(target)}"
        rows.append([
            names[_cell(_dig(item, "id"))],
            _cell(_dig(item, "type")),
            _cell(_dig(item, "scope")),
            _position(item),
            _pair(_dig(item, "moves"), _dig(item, "type_stats", "move_rate")),
            _pair(_dig(item, "hp"), _dig(item, "type_stats", "max_hp")),
            activity,
            orders,
        ])
    return columns, rows


def _render_cities(
    items: Sequence[Any], aliases: Mapping[str, str] | None,
) -> tuple[tuple[str, ...], list[list[str]]]:
    columns = ("alias", "city", "pos", "size", "building", "shields", "surplus", "buy")
    names = _alias_map(
        "c", [_dig(item, "id") for item in items], aliases,
    )
    rows: list[list[str]] = []
    for item in items:
        if not isinstance(item, Mapping):
            raise _error("city item is not an object")
        surplus = _dig(item, "surplus")
        surplus_text = "-"
        if isinstance(surplus, Mapping):
            surplus_text = "f{} s{} t{}".format(
                _cell(_dig(surplus, "food")),
                _cell(_dig(surplus, "shields")),
                _cell(_dig(surplus, "trade")),
            )
        buy = "-"
        if _dig(item, "production", "can_buy") is True:
            buy = _cell(_dig(item, "production", "buy_cost"))
        rows.append([
            names[_cell(_dig(item, "id"))],
            _cell(_dig(item, "name")),
            _position(item),
            _cell(_dig(item, "size")),
            _cell(_dig(item, "production", "name")),
            _pair(
                _dig(item, "production", "shield_stock"),
                _dig(item, "production", "shield_cost"),
            ),
            surplus_text,
            buy,
        ])
    return columns, rows


def _render_nations(
    items: Sequence[Any], aliases: Mapping[str, str] | None,
) -> tuple[tuple[str, ...], list[list[str]]]:
    columns = ("id", "nation", "default_style_id")
    rows = []
    for item in items:
        if not isinstance(item, Mapping):
            raise _error("nation item is not an object")
        rows.append([
            _cell(_dig(item, "id")),
            _cell(_dig(item, "name")),
            _cell(_dig(item, "default_style_id")),
        ])
    return columns, rows


def _render_styles(
    items: Sequence[Any], aliases: Mapping[str, str] | None,
) -> tuple[tuple[str, ...], list[list[str]]]:
    columns = ("id", "style")
    rows = []
    for item in items:
        if not isinstance(item, Mapping):
            raise _error("style item is not an object")
        rows.append([_cell(_dig(item, "id")), _cell(_dig(item, "name"))])
    return columns, rows


def _render_diplomacy(
    items: Sequence[Any], aliases: Mapping[str, str] | None,
) -> tuple[tuple[str, ...], list[list[str]]]:
    """Project one row per relation, with any open meeting spelled out.

    The meeting columns are separate and literal rather than one prose cell
    because the decisions projection reads them back: `open` plus a clause
    count plus who has accepted is exactly what decides whether this relation
    is still waiting on the seat.
    """
    columns = (
        "alias", "player", "nation", "state", "embassy",
        "meeting", "clauses", "accepted",
    )
    names = _alias_map(
        "r", [_dig(item, "relation_id") for item in items], aliases,
    )
    rows: list[list[str]] = []
    for item in items:
        if not isinstance(item, Mapping):
            raise _error("diplomacy item is not an object")
        meeting = _dig(item, "meeting")
        open_meeting = isinstance(meeting, Mapping)
        accepted = "-"
        if open_meeting:
            sides = [
                side for side, key in (
                    ("you", "self_accepted"), ("them", "other_accepted"),
                ) if _dig(meeting, key) is True
            ]
            accepted = "+".join(sides) if sides else "neither"
        rows.append([
            names[_cell(_dig(item, "relation_id"))],
            _cell(_dig(item, "player_name")),
            _cell(_dig(item, "nation")),
            _cell(_dig(item, "state")),
            _cell(_dig(item, "has_embassy")),
            "open" if open_meeting else "-",
            _cell(_dig(meeting, "clause_count")) if open_meeting else "-",
            accepted,
        ])
    return columns, rows


def _render_governments(
    items: Sequence[Any], aliases: Mapping[str, str] | None,
) -> tuple[tuple[str, ...], list[list[str]]]:
    columns = ("id", "government", "current", "target", "can_change")
    rows = []
    for item in items:
        if not isinstance(item, Mapping):
            raise _error("government item is not an object")
        rows.append([
            _cell(_dig(item, "id")),
            _cell(_dig(item, "name")),
            _cell(_dig(item, "current")),
            _cell(_dig(item, "target")),
            _cell(_dig(item, "can_change")),
        ])
    return columns, rows


_RENDERERS = {
    "overview": _render_overview,
    "units": _render_units,
    "cities": _render_cities,
    "diplomacy": _render_diplomacy,
    "pregame_nations": _render_nations,
    "pregame_styles": _render_styles,
    "governments": _render_governments,
}
_REPLACE_ONLY = {"overview"}


# --------------------------------------------------------------------------
# map projection
# --------------------------------------------------------------------------

_MAP_WINDOW_RE = re.compile(
    r"^# window x (-?\d+)\.\.(-?\d+) y (-?\d+)\.\.(-?\d+)\b",
)
_MAP_ROW_RE = re.compile(r"^\s*(-?\d+) \|(.*)$")
_MAP_TERRAIN_RE = re.compile(r"^# terrain (.*)$")
_MAP_LEGEND_PAIR_RE = re.compile(r"^(\S)=(.+)$")
_LEGEND_SEPARATOR = " · "


def _terrain_chars(
    names: Sequence[str], existing: Mapping[str, str] | None = None,
) -> dict[str, str]:
    """Assign one stable character per terrain name.

    `existing` carries the codes the mirror already used for this map, so a
    merged grid never changes the meaning of a character mid-file.
    """
    assigned: dict[str, str] = dict(existing or {})
    used: set[str] = set(assigned.values())
    for name in sorted(set(names)):
        char = _TERRAIN_CHARS.get(name)
        if char is not None and char not in used:
            assigned[name] = char
            used.add(char)
    for name in sorted(set(names)):
        if name in assigned:
            continue
        candidates = [
            part[0].upper() for part in re.split(r"[^A-Za-z]+", name) if part
        ]
        candidates.extend(_TERRAIN_FALLBACK)
        for candidate in candidates:
            if candidate not in used:
                assigned[name] = candidate
                used.add(candidate)
                break
        else:  # pragma: no cover - the alphabet outlasts a ruleset
            assigned[name] = "*"
    return assigned


class _Map:
    """A parsed `map.txt`: its revision, terrain legend and character grid."""

    __slots__ = ("revision", "legend", "grid")

    def __init__(
        self,
        revision: tuple[int, int] | None,
        legend: dict[str, str],
        grid: dict[tuple[int, int], str],
    ) -> None:
        self.revision = revision
        self.legend = legend
        self.grid = grid

    def known(self) -> int:
        return sum(1 for char in self.grid.values() if char != "?")


def _parse_map(text: str | None) -> _Map:
    grid: dict[tuple[int, int], str] = {}
    legend: dict[str, str] = {}
    if not text:
        return _Map(None, legend, grid)
    lines = text.splitlines()
    revision: tuple[int, int] | None = None
    match = _REV_LINE_RE.match(lines[0]) if lines else None
    if match is not None:
        revision = (int(match.group(2)), int(match.group(1)))
    origin_x: int | None = None
    for line in lines:
        window = _MAP_WINDOW_RE.match(line)
        if window is not None and origin_x is None:
            origin_x = int(window.group(1))
        terrain = _MAP_TERRAIN_RE.match(line)
        if terrain is not None:
            for part in terrain.group(1).split(_LEGEND_SEPARATOR):
                pair = _MAP_LEGEND_PAIR_RE.match(part.strip())
                if pair is not None:
                    legend[pair.group(2)] = pair.group(1)
    if origin_x is None:
        return _Map(revision, legend, grid)
    for line in lines:
        row = _MAP_ROW_RE.match(line)
        if row is None:
            continue
        y = int(row.group(1))
        for offset, char in enumerate(row.group(2)):
            if char != " ":
                grid[(origin_x + offset, y)] = char
    return _Map(revision, legend, grid)


def _map_size(session_dir: Path) -> str | None:
    table = _parse_table(_read(session_dir, _OVERVIEW_FILE))
    for row in table.rows:
        if row and row[0] == "map" and len(row) > 1 and row[1] != "-":
            return row[1]
    return None


def _tile_chars(
    items: Sequence[Any], legend: Mapping[str, str],
) -> tuple[dict[tuple[int, int], str], dict[str, str]]:
    """Return the {(x, y): char} grid this page proves, plus its legend."""
    names = []
    for item in items:
        if not isinstance(item, Mapping):
            raise _error("tile item is not an object")
        if _dig(item, "visibility") != "unknown":
            terrain = _dig(item, "terrain")
            if isinstance(terrain, str):
                names.append(terrain)
    chars = _terrain_chars(names, legend)
    grid: dict[tuple[int, int], str] = {}
    for item in items:
        x = _dig(item, "x")
        y = _dig(item, "y")
        if not isinstance(x, int) or not isinstance(y, int):
            raise _error("tile item carries no coordinates")
        visibility = _dig(item, "visibility")
        terrain = _dig(item, "terrain")
        # Fog is decided by visibility alone: a tile the seat has never seen
        # renders '?' even if the payload volunteered a terrain for it.
        if visibility == "unknown" or not isinstance(terrain, str):
            grid[(x, y)] = "?"
            continue
        char = chars[terrain]
        grid[(x, y)] = char if visibility == "visible" else char.lower()
    return grid, chars


def _render_map(
    session_dir: Path,
    revision: tuple[int, int],
    prior: _Map,
    items: Sequence[Any],
) -> str:
    fresh, legend = _tile_chars(items, prior.legend)
    grid: dict[tuple[int, int], str] = dict(prior.grid)
    grid.update(fresh)
    if not grid:
        return _rev_line(revision) + "\n# no tiles known yet\n"
    xs = [x for x, _y in grid]
    ys = [y for _x, y in grid]
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)
    seen = {char.upper() for char in grid.values()}
    terrain_legend = _LEGEND_SEPARATOR.join(
        f"{char}={name}"
        for name, char in sorted(legend.items())
        if char in seen
    )
    known = sum(1 for char in grid.values() if char != "?")
    size = _map_size(session_dir)
    lines = [
        _rev_line(revision),
        "# map {} · {} of {} tiles known".format(
            size or "size unknown", known, len(grid),
        ),
        f"# window x {min_x}..{max_x} y {min_y}..{max_y}",
        "# legend '?'=never seen · UPPERCASE=visible now · lowercase=remembered",
    ]
    if terrain_legend:
        lines.append(f"# terrain {terrain_legend}")
    for y in range(min_y, max_y + 1):
        row = "".join(
            grid.get((x, y), " ") for x in range(min_x, max_x + 1)
        )
        lines.append(f"{y:>5} |{row}")
    return "\n".join(lines) + "\n"


_YIELD_TILE_RE = re.compile(r"^T\((-?\d{1,4}),(-?\d{1,4})\)$")
# A worked city radius is 5x5; anything much larger is a grid nobody reads, so
# the overlay degrades to a list instead of printing a wall.
_YIELD_WINDOW_MAX = 24


def render_map_yields(session_dir: Path | str) -> list[str]:
    """Overlay the per-tile yields this seat has read onto the terrain grid.

    Both inputs are files the mirror already wrote, so this opens no socket and
    proves nothing beyond fog.  The window is the bounding box of the priced
    tiles — a city's worked radius, not the whole board — and a tile inside it
    whose yields this seat has never read renders `?` rather than a zero.
    """
    session_dir = Path(session_dir)
    parsed = _parse_map(_read(session_dir, _MAP_FILE))
    if not parsed.grid:
        return []
    table = _parse_table(_read(session_dir, _YIELD_FILE))
    priced: dict[tuple[int, int], str] = {}
    if table.columns[:4] == _YIELD_COLUMNS[:4]:
        for row in table.rows:
            match = _YIELD_TILE_RE.match(row[0]) if row else None
            if match is None or len(row) < 4:
                continue
            priced[(int(match.group(1)), int(match.group(2)))] = "/".join(
                row[1:4],
            )
    lines = [_rev_line(parsed.revision)]
    if not priced:
        lines.append(
            "# no tile yields read yet; price a city's tiles with "
            "`just state --section city_citizens --actor_id c1`",
        )
        return lines
    xs = [x for x, _y in priced]
    ys = [y for _x, y in priced]
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)
    lines.append(
        f"# yields · {len(priced)} tiles priced · "
        f"window x {min_x}..{max_x} y {min_y}..{max_y}",
    )
    lines.append(
        "# cell TERRAIN food/shields/trade · '?' = not read for that tile",
    )
    if max_x - min_x + 1 > _YIELD_WINDOW_MAX or (
        max_y - min_y + 1 > _YIELD_WINDOW_MAX
    ):
        lines.extend(
            "{},{} {} {}".format(
                x, y, parsed.grid.get((x, y), "?"), priced[(x, y)],
            )
            for x, y in sorted(priced)
        )
        return lines
    cells = {
        (x, y): "{}{}".format(
            parsed.grid.get((x, y), "?"), priced.get((x, y), "?"),
        )
        for y in range(min_y, max_y + 1)
        for x in range(min_x, max_x + 1)
    }
    width = max(len(text) for text in cells.values())
    lines.append(
        "      " + " ".join(
            str(x).ljust(width) for x in range(min_x, max_x + 1)
        ).rstrip(),
    )
    for y in range(min_y, max_y + 1):
        row = " ".join(
            cells[(x, y)].ljust(width) for x in range(min_x, max_x + 1)
        )
        lines.append(f"{y:>5} |{row}".rstrip())
    return lines


# --------------------------------------------------------------------------
# legal-action catalogs
# --------------------------------------------------------------------------

def _argument_names(schema: Any) -> str:
    if not isinstance(schema, Mapping):
        return "-"
    properties = schema.get("properties")
    if not isinstance(properties, Mapping) or not properties:
        return "-"
    required = schema.get("required")
    required_names = set(required) if isinstance(required, list) else set()
    return "{" + " ".join(
        f"{_cell(name)}*" if name in required_names else _cell(name)
        for name in properties
    ) + "}"


def _target_text(subject: Mapping[str, Any]) -> str:
    target = subject.get("target")
    if not isinstance(target, Mapping):
        return "-"
    x = target.get("x")
    y = target.get("y")
    if isinstance(x, int) and isinstance(y, int):
        return f"{x},{y}"
    name = target.get("name")
    if isinstance(name, str) and name:
        return _cell(name)
    kind = target.get("type")
    return _cell(kind) if isinstance(kind, str) else "-"


def _action_flags(
    subject: Mapping[str, Any], scope_actor: str | None,
) -> str:
    flags: list[str] = []
    # Omit-when-default, never omit-by-field-name (redesign doc §5 P0.2):
    # each field is hidden only when it holds its exact default value.
    probability = subject.get("probability")
    if "probability" in subject and probability != _DEFAULT_PROBABILITY:
        if isinstance(probability, Mapping):
            kind = _cell(probability.get("kind"))
            low = probability.get("minimum_percent")
            high = probability.get("maximum_percent")
            if low is None and high is None:
                flags.append(f"p={kind}")
            elif low == high:
                flags.append(f"p={kind}:{_cell(low)}%")
            else:
                flags.append(f"p={kind}:{_cell(low)}-{_cell(high)}%")
        else:
            flags.append(f"p={_cell(probability)}")
    legality = subject.get("legality")
    if "legality" in subject and legality != "legal":
        flags.append(f"legality={_cell(legality)}")
    if subject.get("consuming") is True:
        flags.append("consuming")
    if subject.get("variant") is not None and "variant" in subject:
        flags.append(f"variant={_cell(subject['variant'])}")
    gold = subject.get("gold_cost")
    if gold is None and isinstance(subject.get("target"), Mapping):
        gold = subject["target"].get("gold_cost")
    if gold is not None:
        flags.append(f"gold={_cell(gold)}")
    actor = subject.get("actor")
    if isinstance(actor, Mapping):
        actor_id = actor.get("id")
        if not (scope_actor is not None and actor_id == scope_actor):
            flags.append(
                "actor={}:{}".format(
                    _cell(actor.get("type")), _cell(actor_id),
                ),
            )
    return " ".join(flags) or "-"


def _render_actions(
    items: Sequence[Any],
    aliases: Mapping[str, str] | None,
    scope_actor: str | None,
) -> tuple[tuple[str, ...], list[list[str]]]:
    columns = ("alias", "kind", "target", "args", "label", "flags")
    rows: list[list[str]] = []
    for item in items:
        if not isinstance(item, Mapping):
            raise _error("legal action descriptor is not an object")
        subject = item.get("subject")
        if not isinstance(subject, Mapping):
            raise _error("legal action descriptor carries no subject")
        action_id = _cell(item.get("action_id"))
        alias = "-"
        if aliases is not None and action_id in aliases:
            alias = _cell(aliases[action_id])
        rows.append([
            alias,
            _cell(item.get("kind")),
            _target_text(subject),
            _argument_names(item.get("arguments_schema")),
            _cell(item.get("label")),
            _action_flags(subject, scope_actor),
        ])
    return columns, rows


# --------------------------------------------------------------------------
# delta digest
# --------------------------------------------------------------------------

_DELTA_SINCE_RE = re.compile(r"^since rev (\d+) turn (\d+)")


def _parse_delta(text: str | None) -> tuple[
    tuple[int, int] | None, tuple[int, int] | None, dict[str, list[str]],
]:
    if not text:
        return None, None, {}
    lines = text.splitlines()
    revision: tuple[int, int] | None = None
    match = _REV_LINE_RE.match(lines[0]) if lines else None
    if match is not None:
        revision = (int(match.group(2)), int(match.group(1)))
    since: tuple[int, int] | None = None
    sections: dict[str, list[str]] = {}
    current: str | None = None
    for line in lines[1:]:
        found = _DELTA_SINCE_RE.match(line)
        if found is not None:
            since = (int(found.group(2)), int(found.group(1)))
        elif line.startswith("## "):
            current = line[3:].strip()
            sections.setdefault(current, [])
        elif line.startswith("- ") and current is not None:
            sections[current].append(line[2:])
    return revision, since, sections


def _delta_text(
    revision: tuple[int, int],
    since: tuple[int, int] | None,
    command: str,
    sections: Mapping[str, list[str]],
) -> str:
    lines = [_rev_line(revision)]
    if since is not None and since != revision:
        lines.append(
            f"since rev {since[1]} turn {since[0]} · last update: {_cell(command)}",
        )
    else:
        lines.append(f"no earlier mirror · last update: {_cell(command)}")
    wrote = False
    for title, entries in sections.items():
        if not entries:
            continue
        wrote = True
        lines.append("")
        lines.append(f"## {title}")
        for entry in entries[:_MAX_DELTA_LINES]:
            lines.append(f"- {entry}")
        if len(entries) > _MAX_DELTA_LINES:
            lines.append(f"- … and {len(entries) - _MAX_DELTA_LINES} more")
    if not wrote:
        lines.append("")
        lines.append("nothing changed.")
    return "\n".join(lines) + "\n"


def _update_delta(
    session_dir: Path,
    revision: tuple[int, int],
    command: str,
    title: str,
    entries: Sequence[str],
) -> Path | None:
    prior_revision, prior_since, sections = _parse_delta(
        _read(session_dir, _DELTA_FILE),
    )
    if not entries:
        # An empty change list still has to move the stamp forward, or the
        # digest keeps claiming an older revision than the tables beside it.
        # Nothing to say at an unchanged (or older) revision, though.
        if prior_revision is None or not _newer(revision, prior_revision):
            return None
    elif prior_revision is not None and _newer(prior_revision, revision):
        # Never walk the digest backwards: a mirror file only moves forward.
        return None
    if prior_revision is None or _newer(revision, prior_revision):
        since = prior_revision
        sections = {}
    else:
        since = prior_since
    merged = dict(sections)
    existing = merged.setdefault(title, [])
    for entry in entries:
        if entry not in existing:
            existing.append(entry)
    return _write(
        session_dir, _DELTA_FILE,
        _delta_text(revision, since, command, merged),
    )


def _row_changes(
    columns: Sequence[str],
    prior: _Table,
    rows: Sequence[Sequence[str]],
) -> list[str]:
    """Describe added, removed and changed rows against the previous file."""
    if not prior.columns:
        return [
            f"{row[0]} {' '.join(row[1:3])}".strip() + " (first observation)"
            for row in rows
        ]
    old = prior.by_key()
    new = {row[0]: tuple(row) for row in rows if row}
    entries: list[str] = []
    for key, row in new.items():
        previous = old.get(key)
        if previous is None:
            described = " ".join(value for value in row[1:3] if value != "-")
            entries.append(f"{key} new: {described}".rstrip(": "))
            continue
        differences = []
        for index, name in enumerate(columns):
            before = previous[index] if index < len(previous) else "-"
            after = row[index] if index < len(row) else "-"
            if index and before != after:
                differences.append(f"{name} {before} -> {after}")
        if differences:
            entries.append(f"{key}: " + "; ".join(differences))
    for key, previous in old.items():
        if key not in new:
            described = " ".join(
                value for value in previous[1:3] if value != "-"
            )
            entries.append(f"{key} gone (was {described})")
    return entries


# --------------------------------------------------------------------------
# public entry points
# --------------------------------------------------------------------------

def mirror_dir(session_path: Path | str) -> Path:
    """Return the mirror directory that belongs to one private session file."""
    path = Path(session_path)
    name = path.stem if path.suffix else path.name + "-mirror"
    if not name or name in {".", ".."}:
        raise _error("the session file name has no mirror directory")
    return path.with_name(name)


def update_from_page(
    session_dir: Path | str,
    command: str,
    page: Mapping[str, Any],
    *,
    aliases: Mapping[str, str] | None = None,
) -> tuple[Path, ...]:
    """Project one validated v2 page (state or legal-actions) into the mirror.

    `page` is the object returned by `client._validate_page`.  Pages older
    than the mirror are ignored; a page carrying the mirror's own revision is
    merged into it; a newer revision replaces it.  Returns the files written.
    """
    session_dir = Path(session_dir)
    revision = _revision_pair(_dig(page, "state_revision"))
    inner = _dig(page, "page")
    if not isinstance(inner, Mapping):
        raise _error("page payload carries no page envelope")
    section = inner.get("section")
    items = inner.get("items")
    if not isinstance(section, str) or not isinstance(items, list):
        raise _error("page envelope carries no section items")
    if len(items) > _MAX_ROWS:
        raise _error("page envelope carries too many items")
    if section == "legal_actions":
        return _update_options(
            session_dir, command, revision, inner, items, aliases,
        )
    if section in {"known_tiles", "map_tiles", "tile_window"}:
        return _update_map(session_dir, command, revision, items)
    if section == "city_citizens":
        return _update_yields(
            session_dir, command, revision, inner, items, aliases,
        )
    target = _SECTION_TARGETS.get(section)
    renderer = _RENDERERS.get(section)
    if target is None or renderer is None:
        return ()
    prior = _parse_table(_read(session_dir, target))
    if _stale(revision, prior.revision):
        return ()
    columns, rows = renderer(items, aliases)
    terminal = inner.get("next_cursor") is None
    if (
        prior.revision == revision
        and not (terminal and inner.get("total_items") == len(items))
        and section not in _REPLACE_ONLY
        and prior.columns == columns
    ):
        # A window of a section the mirror already holds at this exact
        # revision: merge, so page 2 of a unit catalog cannot erase page 1.
        merged = prior.by_key()
        for row in rows:
            merged[row[0]] = tuple(row)
        rows = [list(row) for row in merged.values()]
    title = _SECTION_TITLES[section]
    total = inner.get("total_items")
    # `overview` projects one item into many fact rows; every other section is
    # one row per item, so the row count is the honest "how much do I hold".
    shown = len(items) if section in _REPLACE_ONLY else len(rows)
    complete = terminal and (
        not isinstance(total, int) or shown >= total
    )
    note = _page_note(title, shown, total, complete)
    written = [_write(
        session_dir, target, _table_text(revision, (note,), columns, rows),
    )]
    delta = _update_delta(
        session_dir, revision, command, title,
        _row_changes(columns, prior, rows),
    )
    if delta is not None:
        written.append(delta)
    return tuple(written)


def _update_map(
    session_dir: Path,
    command: str,
    revision: tuple[int, int],
    items: Sequence[Any],
) -> tuple[Path, ...]:
    prior = _parse_map(_read(session_dir, _MAP_FILE))
    if _stale(revision, prior.revision):
        return ()
    known_before = prior.known()
    text = _render_map(session_dir, revision, prior, items)
    written = [_write(session_dir, _MAP_FILE, text)]
    known_after = _parse_map(text).known()
    entries = []
    if known_after != known_before:
        entries.append(
            f"terrain known: {known_before} -> {known_after} tiles",
        )
    delta = _update_delta(session_dir, revision, command, "map", entries)
    if delta is not None:
        written.append(delta)
    return tuple(written)


def _update_yields(
    session_dir: Path,
    command: str,
    revision: tuple[int, int],
    inner: Mapping[str, Any],
    items: Sequence[Any],
    aliases: Mapping[str, str] | None,
) -> tuple[Path, ...]:
    """Project the per-tile yields a `city_citizens` page carried.

    This is the only page that prices a tile in food/shields/trade, so it is
    what `show map --yields` overlays.  A tile is keyed by the coordinate alias
    the CLI already assigned it; a tile this seat has never located by
    coordinate keeps its opaque handle, because inventing a coordinate for it
    would put a tile on the map the seat has not seen.
    """
    rows: list[list[str]] = []
    for item in items:
        if not isinstance(item, Mapping):
            raise _error("city citizen item is not an object")
        if _dig(item, "kind") != "tile":
            continue
        yields = _dig(item, "yields")
        tile_id = _dig(item, "tile_id")
        if not isinstance(yields, Mapping) or not isinstance(tile_id, str):
            continue
        city_id = _dig(item, "city_id")
        rows.append([
            _cell((aliases or {}).get(tile_id, tile_id)),
            _cell(_dig(yields, "food")),
            _cell(_dig(yields, "shields")),
            _cell(_dig(yields, "trade")),
            "yes" if _dig(item, "worked") is True else "no",
            _cell(
                (aliases or {}).get(city_id, city_id)
                if isinstance(city_id, str) else city_id
            ),
        ])
    if not rows:
        return ()
    prior = _parse_table(_read(session_dir, _YIELD_FILE))
    if _stale(revision, prior.revision):
        return ()
    if prior.revision == revision and prior.columns == _YIELD_COLUMNS:
        # One page prices one city's tiles; a second city's page must add to
        # the overlay rather than replace it.
        merged = prior.by_key()
        for row in rows:
            merged[row[0]] = tuple(row)
        rows = [list(row) for row in merged.values()]
    note = _page_note("yields", len(rows), len(rows), True)
    written = [_write(
        session_dir, _YIELD_FILE,
        _table_text(revision, (note,), _YIELD_COLUMNS, rows),
    )]
    delta = _update_delta(
        session_dir, revision, command, "yields",
        [f"tile yields known for {len(rows)} tiles"],
    )
    if delta is not None:
        written.append(delta)
    return tuple(written)


def _update_options(
    session_dir: Path,
    command: str,
    revision: tuple[int, int],
    inner: Mapping[str, Any],
    items: Sequence[Any],
    aliases: Mapping[str, str] | None,
) -> tuple[Path, ...]:
    scope = inner.get("scope")
    scope_actor: str | None = None
    actor_type = "actor"
    if isinstance(scope, Mapping):
        actor_id = scope.get("actor_id")
        scope_actor = actor_id if isinstance(actor_id, str) else None
        actor_type = _cell(scope.get("actor_type"))
    if scope_actor is None:
        name = "unscoped"
        heading = "unscoped catalog (union of the kinds fetched at this rev)"
    else:
        letter = actor_type[0] if actor_type else "a"
        name = _alias_map(letter, [scope_actor], aliases)[scope_actor]
        heading = f"actor {actor_type} {name}"
    target = (_STATE_DIR, "options", f"{_file_name(name)}.txt")
    prior = _parse_table(_read(session_dir, target))
    if _stale(revision, prior.revision):
        return ()
    columns, rows = _render_actions(items, aliases, scope_actor)
    total = inner.get("total_items")
    terminal = (
        bool(inner.get("catalog_complete"))
        if "catalog_complete" in inner
        else inner.get("next_cursor") is None
    )
    # A complete, actor-wide catalog arrives here whole: the client re-projects
    # the promoted accumulation, not just its final page.  Replacing rather
    # than merging is what lets the rows staged before the aliases existed
    # disappear instead of surviving with `-` in the alias column -- and it
    # keeps two capabilities that render identically (same kind, target and
    # label, differing only in a subject key this table does not print) as two
    # addressable rows instead of collapsing them into one.
    whole = (
        scope_actor is not None
        and isinstance(scope, Mapping)
        and "target_id" not in scope
        and terminal
        and (not isinstance(total, int) or len(items) >= total)
    )
    if not whole and prior.revision == revision and prior.columns == columns:
        merged = {tuple(row[1:]): row for row in prior.rows}
        for row in rows:
            merged[tuple(row[1:])] = row
        rows = [list(row) for row in merged.values()]
    complete = terminal and (
        not isinstance(total, int) or len(rows) >= total
    )
    note = _page_note("actions", len(rows), total, complete)
    notes = [heading, note]
    if rows and not any(row[0] != "-" for row in rows):
        # Every alias column is `-`: this projection is readable but not
        # executable.  Say so and name the command that re-issues the names,
        # rather than inventing `aN` handles the CLI never assigned.
        notes.append(
            "no action alias resolves at this revision; re-enumerate with "
            + (
                f"`just legal --actor_id {name} --all`"
                if scope_actor is not None
                else "`just legal --kind KIND --all`"
            )
            + " before acting",
        )
    written = [_write(
        session_dir, target,
        _table_text(revision, tuple(notes), columns, rows),
    )]
    delta = _update_delta(
        session_dir, revision, command, "options",
        [f"options/{name}.txt refreshed: {note}"],
    )
    if delta is not None:
        written.append(delta)
    return tuple(written)


def update_from_receipt(
    session_dir: Path | str,
    command: str,
    receipt: Mapping[str, Any],
) -> tuple[Path, ...]:
    """Record one validated command receipt in the mirror's delta digest.

    `receipt` is the object returned by `client._validate_receipt`.  A receipt
    carrying a newer revision than the tables also tells the agent, in prose,
    that the tables are now behind.
    """
    session_dir = Path(session_dir)
    revision = _revision_pair(_dig(receipt, "state_revision"))
    state = _cell(_dig(receipt, "receipt_state"))
    batch = _cell(_dig(receipt, "batch_id"))
    entry = f"{state} batch {batch[:16]} at rev {revision[1]}"
    if _dig(receipt, "idempotent") is True:
        entry += " (idempotent replay)"
    error = _dig(receipt, "error")
    if isinstance(error, Mapping):
        detail = error.get("error")
        if isinstance(detail, Mapping):
            entry += ": {} - {}".format(
                _cell(detail.get("code")), _cell(detail.get("message")),
            )
    observation = _dig(receipt, "observation")
    if isinstance(observation, Mapping):
        city = observation.get("city")
        if isinstance(city, Mapping):
            entry += ": investigated {} sz{} building {}".format(
                _cell(city.get("name")), _cell(city.get("size")),
                _cell(_dig(city, "production", "name")),
            )
    entries = [entry]
    table = _parse_table(_read(session_dir, _OVERVIEW_FILE))
    if table.revision is not None and _newer(revision, table.revision):
        entries.append(
            f"state files still show rev {table.revision[1]}; "
            "run `just turn` to refresh them",
        )
    delta = _update_delta(
        session_dir, revision, command, "receipts", entries,
    )
    return (delta,) if delta is not None else ()


def _phase_marker(
    health: Mapping[str, Any], announced: Any = None,
) -> str:
    """Render `state/phase.json` from one validated health payload.

    Closed shape, closed value types: every field is copied from a payload
    the client already validated, or is `None`.  A watcher may therefore
    branch on `active` without a schema check, and `schema_version` tells it
    when it may not.

    `announced` is the monitor's `[incarnation, turn, phase]` record of the
    last phase it told anybody about.  It lives here rather than in a file of
    its own so that a monitor restarted mid-phase can read one file and know
    it has nothing to say -- process-singleton stops two monitors running, but
    only this stops a restarted one from re-announcing a turn already
    announced.  `incarnation` is in the tuple because the supervisor bumps it
    on a rollback, and a replayed turn is genuinely a new turn.
    """
    phase = _dig(health, "phase")
    timing = phase.get("timing") if isinstance(phase, Mapping) else None
    holder = None
    if isinstance(phase, Mapping):
        rows = phase.get("waiting_on")
        seats = rows.get("seats") if isinstance(rows, Mapping) else None
        others = [
            row for row in seats or ()
            if isinstance(row, Mapping) and row.get("is_self") is False
        ]
        if len(others) == 1 and phase.get("active") is not True:
            row = others[0]
            holder = {
                "place": _number(row.get("place")),
                "seat_id": _text(row.get("seat_id")),
                "player_name": _text(row.get("player_name")),
                "controller_label": _text(row.get("controller_label")),
            }
    value = {
        "schema_version": _PHASE_SCHEMA_VERSION,
        "updated_at": round(time.time(), 3),
        "game_state": _text(_dig(health, "game_state")),
        "turn": _number(phase.get("turn") if isinstance(phase, Mapping) else None),
        "phase": _number(phase.get("phase") if isinstance(phase, Mapping) else None),
        "state": _text(phase.get("state") if isinstance(phase, Mapping) else None),
        "active": bool(
            isinstance(phase, Mapping) and phase.get("active") is True
        ),
        "held_s": _number(
            timing.get("elapsed_s") if isinstance(timing, Mapping) else None
        ),
        "deadline_s_left": _number(
            timing.get("remaining_s") if isinstance(timing, Mapping) else None
        ),
        "holder": holder,
        "announced": _announced(announced),
    }
    return json.dumps(value, sort_keys=True, indent=2)


def _announced(value: Any) -> list[int] | None:
    """Accept only a well-formed [incarnation, turn, phase], else nothing."""
    if not isinstance(value, (list, tuple)) or len(value) != 3:
        return None
    if any(
        isinstance(item, bool) or not isinstance(item, int) or item < 0
        for item in value
    ):
        return None
    return [int(item) for item in value]


def read_phase_marker(session_dir: Path | str) -> dict[str, Any] | None:
    """Read `state/phase.json` back, or None when it is absent or unusable.

    A marker this module cannot parse is treated as absent rather than as an
    error: it is a projection, and a watcher's own bad file must never stop
    the client that writes it.
    """
    text = _read(Path(session_dir), _PHASE_FILE)
    if text is None:
        return None
    try:
        value = json.loads(text)
    except (ValueError, TypeError):
        return None
    if (
        not isinstance(value, dict)
        or value.get("schema_version") != _PHASE_SCHEMA_VERSION
    ):
        return None
    return value


def update_phase_marker(
    session_dir: Path | str,
    health: Mapping[str, Any],
    *,
    announced: Any = None,
) -> tuple[Path, ...]:
    """Write `state/phase.json` alone, touching no other projection.

    This is the monitor's write.  It deliberately does *not* rewrite
    `state/header.txt`: the monitor is a read-only observer that may be
    running for the whole game, and a long-lived process rewriting the
    projections a real command owns would be a second writer for no gain.
    """
    session_dir = Path(session_dir)
    if announced is None:
        current = read_phase_marker(session_dir)
        announced = current.get("announced") if current is not None else None
    return (
        _write(session_dir, _PHASE_FILE, _phase_marker(health, announced)),
    )


def append_monitor_log(session_dir: Path | str, line: str) -> Path:
    """Append one sanitized line to `state/monitor.log`.

    A backgrounded monitor's stdout is lost to log rotation, to a closed pane,
    or to the agent's own context being compacted.  This is what lets a
    harness answer "when did my turns actually open" after the fact -- and it
    is the record of every `--exec` string this workspace ran.
    """
    session_dir = Path(session_dir)
    stamped = "{} {}".format(
        time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime()),
        _CONTROL_RE.sub(" ", str(line))[:_MAX_LOG_LINE],
    )
    return _client()._append_private_text(
        session_dir.joinpath(*_MONITOR_LOG_FILE), stamped + "\n",
    )


def _number(value: Any) -> int | float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    return value


def _text(value: Any) -> str | None:
    if not isinstance(value, str):
        return None
    rendered = _cell(value)
    return None if rendered == "-" else rendered


def update_from_health(
    session_dir: Path | str,
    command: str,
    health: Mapping[str, Any],
    *,
    revision: Mapping[str, Any] | None = None,
    commands: Sequence[str] | None = None,
) -> tuple[Path, ...]:
    """Write `state/header.txt` and `state/phase.json` from one health payload.

    `health` is the object returned by `client._validate_health`.  Health
    carries no state revision, so pass `revision` (the page revision the same
    command observed) when one is known; otherwise the header reuses the
    newest revision already recorded in the mirror, and stamps `# rev ?` when
    the mirror has never seen a page.
    """
    session_dir = Path(session_dir)
    stamp: tuple[int, int] | None = None
    if revision is not None:
        stamp = _revision_pair(revision)
    else:
        for target in (_OVERVIEW_FILE, _MAP_FILE, _DELTA_FILE):
            table = _parse_table(_read(session_dir, target))
            if table.revision is not None and _newer(table.revision, stamp):
                stamp = table.revision
    lines = [_rev_line(stamp)]

    def add(name: str, value: str) -> None:
        lines.append(f"{name.ljust(9)} {value}")

    add("game", "{} · seat {} {} · controller {}".format(
        _cell(_dig(health, "game_id")),
        _cell(_dig(health, "seat", "place")),
        _cell(_dig(health, "seat", "player_name")),
        _cell(_dig(health, "agent", "controller_label")),
    ))
    objective = _dig(health, "objective")
    if objective is not _MISSING:
        add("objective", _cell(objective))
        add("budget", "turn {} of {} · {} remaining".format(
            _cell(_dig(health, "phase", "turn")),
            _cell(_dig(health, "max_turns")),
            _cell(_dig(health, "turns_remaining")),
        ))
    phase = _dig(health, "phase")
    if isinstance(phase, Mapping):
        add("phase", "{} · turn {} phase {} · active {}".format(
            _cell(phase.get("state")), _cell(phase.get("turn")),
            _cell(phase.get("phase")), _cell(phase.get("active")),
        ))
        timing = phase.get("timing")
        if isinstance(timing, Mapping):
            add("timing", "{} · {}s left of {}s".format(
                _cell(timing.get("mode")),
                _cell(timing.get("remaining_s")),
                _cell(timing.get("timeout_s")),
            ))
    add("game_state", "{} · observation {} · legal_actions {}".format(
        _cell(_dig(health, "game_state")),
        _cell(_dig(health, "observation_available")),
        _cell(_dig(health, "legal_actions_available")),
    ))
    add("sidecar", "{} · server_connected {}".format(
        _cell(_dig(health, "sidecar", "state")),
        _cell(_dig(health, "sidecar", "server_connected")),
    ))
    lines.append("")
    lines.append(
        "protocol  full-control-v2 · every action is an opaque server-issued "
        "capability;",
    )
    lines.append(
        "          aliases are revision-scoped and stop resolving once a "
        "newer revision arrives.",
    )
    lines.append("files     state/overview.tsv state/units.tsv "
                 "state/cities.tsv state/map.txt")
    lines.append("          state/options/<alias>.txt state/delta.md "
                 "cache/*.tsv  (projections; read them, they cost no network)")
    lines.append("")
    for entry in (commands if commands is not None else _DEFAULT_COMMAND_CARD):
        lines.append(_CONTROL_RE.sub(" ", str(entry)))
    # The marker is a read-modify-write like every other file here: the
    # monitor's `announced` tuple must survive an unrelated `just health`,
    # or a restarted monitor would re-announce a turn already announced.
    current = read_phase_marker(session_dir)
    return (
        _write(session_dir, _HEADER_FILE, "\n".join(lines)),
        _write(
            session_dir, _PHASE_FILE,
            _phase_marker(
                health,
                current.get("announced") if current is not None else None,
            ),
        ),
    )
