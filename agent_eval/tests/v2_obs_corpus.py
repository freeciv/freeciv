"""A corpus of real observation-shaped states harvested from the machines' own
autosaves.

Synthetic fixtures only cover the shapes we thought of.  The two wedges that
bricked live games were both shapes nobody thought of, so the rig also reads
what actually happened: every ``.agent-eval/runs/*/saves/turn-*.sav.gz`` this
machine ever wrote, parsed with :mod:`agent_eval.save_replay`'s own readers.

Two saves are named explicitly because they are the incidents:

``game_mEUltpqtzauPGfjI9IlhWJ5x`` turn 52
    the first-entertainer wedge (OURS).

``game_XqynGMtFOtaqFbGXaF7lBx66`` turn 66
    the silent sidecar process exit (C).

Everything is read-only: no save is written, no replay cache is created, and no
run directory is touched.  ``save_replay._read_stable_save`` re-stats the file
after reading, so a save being written by a live game is skipped rather than
half-read.

What a save can and cannot supply
---------------------------------
See :data:`CORPUS_GAPS`.  The short version: a savegame is the *server's*
authoritative state, while an observation is the *client's* projection of it.
Sizes, specialist counts, city counts, governments, gold, techs and the
empire-wide mood aggregate are real; worked-tile geometry, per-city mood splits,
legal-action catalogs and per-player known maps are not in the save at all and
are synthesised by the fixture builder.  Every synthesised field is listed.
"""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from dataclasses import dataclass, replace
import functools
from pathlib import Path
import re

from agent_eval import save_replay
from agent_eval.tests import v2_obs_fixtures as fixtures

REPO_ROOT = Path(__file__).resolve().parents[2]
RUNS_ROOT = REPO_ROOT / ".agent-eval" / "runs"
#: The two incident saves, committed.  ``.agent-eval/runs`` is gitignored and
#: gets pruned, so relying on it meant the campaign's strongest empirical
#: result -- the machine's own history refuting the invariant that bricked turn
#: 52 -- silently SKIPPED on any other machine, on CI, and after a cleanup, and
#: the run still reported OK.  36 KB each is a cheap price for evidence that
#: cannot evaporate.
INCIDENT_ROOT = Path(__file__).resolve().parent / "fixtures" / "incidents"

SAVE_NAME = re.compile(r"^turn-(\d{4,})-auto\.sav(?:\.gz|\.bz2|\.xz|\.zst)?$")

#: The first-entertainer wedge (OURS): every observation after it failed.
ENTERTAINER_INCIDENT = ("game_mEUltpqtzauPGfjI9IlhWJ5x", 52)
#: The silent sidecar process exit (C): server healthy, client gone.
SIDECAR_EXIT_INCIDENT = ("game_XqynGMtFOtaqFbGXaF7lBx66", 66)

#: Precisely what a savegame cannot tell us about an observation row.  Each
#: entry is a projector field family the fixture builder has to synthesise.
CORPUS_GAPS: Mapping[str, str] = {
    "city_tile.*": (
        "Worked-tile assignment is player-private client knowledge. "
        "savegame3 stores it only as the server's own city workers bitmap, "
        "which save_replay._parse_board deliberately does not export (the "
        "board payload is documented as terrain/altitude/territory/cities "
        "only). The corpus therefore knows how many citizens work tiles "
        "(size minus specialists) but not which tiles, so the fixture lays "
        "them out on a synthetic block."
    ),
    "city.citizen_happy|content|unhappy|angry": (
        "Mood is saved only as the per-player aggregate in [scoreN] "
        "(happy/content/unhappy/angry), never per city. The corpus asserts "
        "the aggregate identity that the turn-52 wedge violated and "
        "synthesises a per-city split that sums to the same total."
    ),
    "city_worker_task.*": (
        "Worker tasks are agent intent held by the client; the autosave's "
        "c= table carries no worker_task columns. The wedge shape (a task "
        "whose tile is not in the citizen catalog) is therefore generated, "
        "not harvested."
    ),
    "action.*": (
        "The legal-action catalog is computed by the client against the "
        "ruleset at request time and is never serialised. No save can "
        "reproduce an action row."
    ),
    "tile.known": (
        "Per-player known maps live in the [playerN] map_t*/map_u* vectors "
        "that save_replay does not parse; the board it does parse is "
        "omniscient. Tile knowledge levels are synthesised as fully known."
    ),
    "unit.activity|orders|transport": (
        "save_replay does not export the u= unit table (unit orders are "
        "explicitly named as state that never leaves the reader), so unit "
        "rows are synthesised."
    ),
    "research.choices_digest": (
        "Digests are computed by the emitter over the live ruleset catalog; "
        "the save stores only the known-tech bitvector."
    ),
    "diplomacy.meeting|clauses": (
        "Open diplomacy meetings are transient client/server session state "
        "and are not serialised into an autosave."
    ),
    f"{ENTERTAINER_INCIDENT[0]}@turn-{ENTERTAINER_INCIDENT[1]}": (
        "The turn-52 autosave is written at turn start and still reports "
        "zero specialists in all 13 cities: the first entertainer appeared "
        "during T52 processing, after the save. The incident state must be "
        "reconstructed by applying one worker->entertainer transition to the "
        "real save, which is what entertainer_incident_case() does."
    ),
    f"{SIDECAR_EXIT_INCIDENT[0]}@turn-{SIDECAR_EXIT_INCIDENT[1]}": (
        "The turn-66 fault was a client process exit. A save records no "
        "client at all, so the corpus can only establish the negative: the "
        "state at turn 66 projects cleanly, so the incident was not an "
        "unprojectable observation."
    ),
}


# --------------------------------------------------------------------------
# Harvested shapes
# --------------------------------------------------------------------------


@dataclass(frozen=True)
class CorpusCity:
    city_id: int
    name: str
    x: int
    y: int
    size: int
    specialists: tuple[int, ...]
    food_stock: int
    shield_stock: int
    building_kind: str
    building_name: str
    worklist_length: int
    improvement_count: int
    rally_length: int

    @property
    def specialist_total(self) -> int:
        return sum(self.specialists)

    @property
    def workers(self) -> int:
        return self.size - self.specialist_total


@dataclass(frozen=True)
class CorpusPlayer:
    index: int
    name: str
    nation: str
    government: str
    gold: int
    alive: bool
    city_count: int
    unit_count: int
    mood: tuple[int, int, int, int]
    score_specialists: tuple[int, ...]
    cities: tuple[CorpusCity, ...]

    @property
    def specialist_total(self) -> int:
        return sum(self.score_specialists)

    @property
    def citizen_total(self) -> int:
        return sum(city.size for city in self.cities)


@dataclass(frozen=True)
class CorpusSample:
    game_id: str
    turn: int
    path: Path
    map_width: int
    map_height: int
    topology: str
    players: tuple[CorpusPlayer, ...]

    def describe(self) -> str:
        return (
            f"{self.game_id}@turn-{self.turn:04d} "
            f"({self.map_width}x{self.map_height} {self.topology}, "
            + ", ".join(
                f"p{player.index}:{len(player.cities)}c/"
                f"{player.citizen_total}pop/{player.specialist_total}spe"
                for player in self.players
            )
            + ")"
        )


def _topology(settings: Mapping[str, str]) -> str:
    raw = settings.get("topology", "").upper()
    isometric = "ISO" in raw
    hexagonal = "HEX" in raw
    if isometric and hexagonal:
        return "isometric_hex"
    if isometric:
        return "isometric_square"
    if hexagonal:
        return "hex"
    return "square"


def _int(value: str | None, default: int = 0) -> int:
    parsed = save_replay._integer(value, default)
    return default if parsed is None else parsed


def _cities(lines: Sequence[str]) -> tuple[CorpusCity, ...]:
    try:
        header, rows = save_replay._table(lines, "c")
    except save_replay._UnreadableSave:
        return ()
    index = {name: position for position, name in enumerate(header)}
    specialist_columns = sorted(
        (name for name in index if re.fullmatch(r"nspe\d+", name)),
        key=lambda name: int(name[4:]),
    )

    def cell(row: Sequence[str], name: str, default: str = "0") -> str:
        position = index.get(name)
        if position is None or position >= len(row):
            return default
        return row[position]

    harvested: list[CorpusCity] = []
    for row in rows:
        harvested.append(CorpusCity(
            city_id=_int(cell(row, "id")),
            name=save_replay._public_text(cell(row, "name", ""), 80),
            x=_int(cell(row, "x")),
            y=_int(cell(row, "y")),
            size=_int(cell(row, "size")),
            specialists=tuple(
                _int(cell(row, name)) for name in specialist_columns
            ),
            food_stock=_int(cell(row, "food_stock")),
            shield_stock=_int(cell(row, "shield_stock")),
            building_kind=cell(row, "currently_building_kind", "UnitType"),
            building_name=cell(row, "currently_building_name", "Settlers"),
            worklist_length=_int(cell(row, "wl_length")),
            improvement_count=cell(row, "improvements", "").count("1"),
            rally_length=_int(cell(row, "rally_point_length")),
        ))
    return tuple(harvested)


def load_sample(path: Path) -> CorpusSample | None:
    """Parse one autosave into corpus shapes, or ``None`` if unreadable."""
    try:
        text, _signature = save_replay._read_stable_save(path)
    except save_replay._UnreadableSave:
        return None
    try:
        sections = save_replay._sections(text)
    except save_replay._UnreadableSave:
        return None
    if "game" not in sections or "players" not in sections:
        return None
    try:
        game = save_replay._scalars(sections["game"])
        players_section = save_replay._scalars(sections["players"])
        settings = save_replay._setting_values(sections.get("settings", ()))
    except save_replay._UnreadableSave:
        return None
    turn = _int(game.get("turn"), -1)
    count = _int(players_section.get("nplayers"), -1)
    width = _int(settings.get("xsize"), 0)
    height = _int(settings.get("ysize"), 0)
    if turn < 0 or count < 1 or width < 1 or height < 1:
        return None
    players: list[CorpusPlayer] = []
    for index in range(count):
        player_lines = sections.get(f"player{index}")
        score_lines = sections.get(f"score{index}")
        if player_lines is None or score_lines is None:
            return None
        try:
            player = save_replay._scalars(player_lines)
            score = save_replay._scalars(score_lines)
        except save_replay._UnreadableSave:
            return None
        specialist_keys = sorted(
            (name for name in score if re.fullmatch(r"specialists\d+", name)),
            key=lambda name: int(name[11:]),
        )
        players.append(CorpusPlayer(
            index=index,
            name=save_replay._public_text(player.get("name", ""), 80),
            nation=save_replay._public_text(player.get("nation", ""), 80),
            government=save_replay._public_text(
                player.get("government_name", ""), 80,
            ),
            gold=_int(player.get("gold")),
            alive=save_replay._boolean(player.get("is_alive")) is True,
            city_count=_int(player.get("ncities")),
            unit_count=_int(player.get("nunits")),
            mood=(
                _int(score.get("happy")), _int(score.get("content")),
                _int(score.get("unhappy")), _int(score.get("angry")),
            ),
            score_specialists=tuple(
                _int(score.get(name)) for name in specialist_keys
            ),
            cities=_cities(player_lines),
        ))
    match = SAVE_NAME.match(path.name)
    game_id = path.parent.parent.name
    if match is not None and int(match.group(1)) != turn:
        return None
    return CorpusSample(
        game_id=game_id,
        turn=turn,
        path=path,
        map_width=width,
        map_height=height,
        topology=_topology(settings),
        players=tuple(players),
    )


# --------------------------------------------------------------------------
# Selection
# --------------------------------------------------------------------------


def save_paths(runs_root: Path | None = None) -> dict[str, list[Path]]:
    """Every autosave this machine kept, grouped by game, in turn order."""
    root = RUNS_ROOT if runs_root is None else runs_root
    grouped: dict[str, list[Path]] = {}
    if not root.is_dir():
        return grouped
    for game_directory in sorted(root.iterdir()):
        saves = game_directory / "saves"
        if not saves.is_dir():
            continue
        selected = sorted(
            (path for path in saves.iterdir() if SAVE_NAME.match(path.name)),
            key=lambda path: path.name,
        )
        if selected:
            grouped[game_directory.name] = selected
    return grouped


def _spread(values: Sequence[Path], limit: int) -> list[Path]:
    """Deterministically thin a turn series to ``limit`` evenly spaced saves."""
    if limit <= 0 or len(values) <= limit:
        return list(values)
    step = (len(values) - 1) / (limit - 1) if limit > 1 else 0
    picked = {round(position * step) for position in range(limit)}
    return [values[index] for index in sorted(picked)]


@functools.lru_cache(maxsize=8)
def corpus(per_game: int = 8) -> tuple[CorpusSample, ...]:
    """A bounded, deterministic slice of the machine's own game history.

    Reading every one of the ~2,600 saves would dominate the suite runtime for
    no extra coverage, so each game contributes an evenly spaced sample of its
    turn series plus, always, its last turn -- which is where the incidents
    live.
    """
    samples: list[CorpusSample] = []
    for _game_id, paths in save_paths().items():
        chosen = _spread(paths, per_game)
        if paths and paths[-1] not in chosen:
            chosen.append(paths[-1])
        for path in chosen:
            sample = load_sample(path)
            if sample is not None:
                samples.append(sample)
    return tuple(samples)


def incident_sample(incident: tuple[str, int]) -> CorpusSample | None:
    """Reconstruct one named incident from its committed save.

    Reads the committed fixture first and the opportunistic run history only
    as a fallback, so the reconstruction runs everywhere rather than skipping
    wherever ``.agent-eval/runs`` happens not to exist.
    """
    game_id, turn = incident
    candidates = list(save_paths(INCIDENT_ROOT).get(game_id, []))
    candidates.extend(save_paths().get(game_id, []))
    for path in candidates:
        match = SAVE_NAME.match(path.name)
        if match is not None and int(match.group(1)) == turn:
            sample = load_sample(path)
            if sample is not None:
                return sample
    return None


# --------------------------------------------------------------------------
# Corpus -> projector rows
# --------------------------------------------------------------------------

#: Ruleset-order names for the classic specialist types the saves count.
SPECIALIST_NAMES = ("Entertainer", "Scientist", "Taxman")

#: The fixture map is 64x64 with a 32-tile block per city.
MAX_CORPUS_CITIES = (
    fixtures.MAP_WIDTH * fixtures.MAP_HEIGHT - fixtures.RESERVED_TILES
) // fixtures.CITY_TILE_STRIDE


def city_spec(ordinal: int, city: CorpusCity) -> fixtures.CitySpec:
    """Turn one harvested city into a projector-level city fixture.

    Real: ``size``, the per-type specialist counts, the worklist length and the
    improvement count.  Synthetic: which tiles the workers work, and how the
    non-specialist citizens split across the mood buckets (see CORPUS_GAPS).
    """
    counts = list(city.specialists) + [0] * (
        len(SPECIALIST_NAMES) - len(city.specialists)
    )
    counts = counts[:len(SPECIALIST_NAMES)]
    # Computed against the counts actually emitted, not against every nspe
    # column the save may carry, so that the fixture's size stays the real one.
    workers = max(city.size - sum(counts), 0)
    if workers + sum(counts) == 0:
        # A saved size-0 city cannot exist; keep the fixture legal and let the
        # corpus test flag the sample instead of emitting an invalid bundle.
        counts[0] = 1
    specialists = tuple(
        fixtures.SpecialistSpec(
            native_id=position,
            name=name,
            count=counts[position],
            is_default=position == 0,
        )
        for position, name in enumerate(SPECIALIST_NAMES)
    )
    return fixtures.CitySpec(
        ordinal=ordinal,
        worked_tiles=min(workers, 60),
        idle_tiles=0,
        specialists=specialists,
        worklist_length=min(city.worklist_length, 4),
        build_choices=max(2, min(city.worklist_length, 4) + 1),
        improvements=min(city.improvement_count, 4),
    )


def case_from_player(
    sample: CorpusSample,
    player_index: int,
    *,
    label: str | None = None,
    **overrides: object,
) -> fixtures.ObservationCase:
    """Project one harvested empire into a full observation case."""
    player = sample.players[player_index]
    cities = tuple(
        city_spec(ordinal, city)
        for ordinal, city in enumerate(player.cities[:MAX_CORPUS_CITIES])
    )
    defaults: dict[str, object] = {
        "label": label or f"corpus-{sample.game_id}-t{sample.turn}-p{player_index}",
        "cities": cities,
        "turn": max(sample.turn, 1),
        "topology": sample.topology,
        "tile_catalog": True,
        "citizen_catalog": True,
        "worklist_catalog": True,
        "build_choice_catalog": True,
        "improvement_catalog": True,
        "own_units": 1 if player.unit_count else 0,
        "visible_units": 1,
    }
    defaults.update(overrides)
    return fixtures.ObservationCase(**defaults)  # type: ignore[arg-type]


def with_first_entertainer(
    case: fixtures.ObservationCase,
) -> fixtures.ObservationCase:
    """Apply the exact transition the turn-52 autosave stops one step short of.

    Freeciv turns a working citizen into an entertainer without changing the
    city's size: ``workers`` drops by one, the default specialist's count rises
    by one, and the mood counters -- which cover only the non-specialists --
    drop by one in step.  That last part is what the old invariant got wrong.
    """
    mutated: list[fixtures.CitySpec] = []
    applied = False
    for city in case.cities:
        if applied or city.worked_tiles < 1:
            mutated.append(city)
            continue
        specialists = tuple(
            replace(item, count=item.count + 1)
            if item.is_default else item
            for item in city.specialists
        )
        mutated.append(replace(
            city,
            worked_tiles=city.worked_tiles - 1,
            specialists=specialists,
            mood=None,
        ))
        applied = True
    return replace(case, cities=tuple(mutated))


def entertainer_incident_case() -> fixtures.ObservationCase | None:
    """The turn-52 empire, one entertainer past the state the save captured."""
    sample = incident_sample(ENTERTAINER_INCIDENT)
    if sample is None:
        return None
    return with_first_entertainer(case_from_player(
        sample, 0, label="incident-turn-0052-first-entertainer",
    ))


def sidecar_exit_case() -> fixtures.ObservationCase | None:
    """The turn-66 empire exactly as the server saved it."""
    sample = incident_sample(SIDECAR_EXIT_INCIDENT)
    if sample is None:
        return None
    return case_from_player(sample, 0, label="incident-turn-0066-sidecar-exit")


__all__ = [
    "CORPUS_GAPS", "CorpusCity", "CorpusPlayer", "CorpusSample",
    "ENTERTAINER_INCIDENT", "MAX_CORPUS_CITIES", "RUNS_ROOT",
    "SIDECAR_EXIT_INCIDENT", "SPECIALIST_NAMES", "case_from_player", "corpus",
    "city_spec", "entertainer_incident_case", "incident_sample",
    "load_sample", "save_paths", "sidecar_exit_case", "with_first_entertainer",
]
