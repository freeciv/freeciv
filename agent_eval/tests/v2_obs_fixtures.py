"""Deterministic observation-row fixtures for the v2 projector property rig.

This module is a *test helper*, not a test.  It builds complete native
observation bundles ("row sets") that are valid per the C emitter's semantics
and feeds them to :class:`agent_eval.v2_control.V2SeatControl` exactly the way
the sidecar does.

Why it exists
-------------
Two live games were bricked by projector invariants that were wrong about what
the C side is allowed to emit:

``citizen-mood`` (OURS)
    ``v2_control`` asserted that a city's mood counters sum to ``size``.
    Freeciv's ``citizen_base_mood()`` subtracts the specialists first, so the
    counters sum to ``size - specialists``.  The first entertainer in
    ``game_mEUltpqtzauPGfjI9IlhWJ5x`` therefore made *every* later observation
    fail ``internal_error`` -- a fail-closed-forever brick.

``worker-task`` (FFI)
    ``city_worker_task`` rows travel with the compact cities catalog while
    ``tile`` rows are exported only through STATE_SCOPE.  The projector demanded
    a matching tile row unconditionally, so one persisted worker task rejected
    every observation for the rest of the game.

Both are *shape* bugs: a legal C-side state that the python projector refuses.
The only systematic defence is to sweep the legal shape space before a live
game, which is what this module makes cheap.

Row construction is driven off ``v2_control._ROW_FIELDS`` itself, so a fixture
can never drift out of field order, and a new/removed native field breaks the
builder loudly instead of producing a silently malformed row.
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping
from dataclasses import dataclass
import random

import agent_eval.v2_control as v2_control
from agent_eval.tests.test_v2_control import _action, complete_v2_row

MAP_WIDTH = 64
MAP_HEIGHT = 64

#: Every yield-bearing output column shared by city_tile / city_specialist.
YIELD_FIELDS = ("food", "shields", "trade", "gold", "luxury", "science")

#: The projector's city output names, in the order the city row spells them.
CITY_OUTPUTS = ("food", "shield", "trade", "gold", "luxury", "science")


def row(kind: str, fields: Mapping[str, object], *, schema_key: str | None = None) -> str:
    """Render one native row with the exact contracted field order.

    Using ``_ROW_FIELDS`` as the source of truth is deliberate: it makes a
    fixture impossible to write against a stale schema, which is the failure
    mode that produced the worker-task wedge in the first place.
    """
    order = v2_control._ROW_FIELDS[schema_key or kind]
    missing = set(order) - set(fields)
    extra = set(fields) - set(order)
    if missing or extra:
        raise AssertionError(
            f"row {kind!r} field mismatch; missing={sorted(missing)} "
            f"extra={sorted(extra)}"
        )
    return kind + " " + " ".join(f"{name}={fields[name]}" for name in order)


def _bit(value: bool) -> int:
    return 1 if value else 0


# --------------------------------------------------------------------------
# Case description
# --------------------------------------------------------------------------


@dataclass(frozen=True)
class SpecialistSpec:
    """One ``city_specialist`` row.

    ``counts_toward_population`` is the superspecialist discriminator: Freeciv's
    ``city_specialists()`` sums only ``normal_specialist_type_iterate``, so a
    superspecialist is outside ``size`` while still contributing yields.
    """

    native_id: int
    name: str
    count: int
    counts_toward_population: bool = True
    can_use: bool = True
    is_default: bool = False
    yields: tuple[int, ...] = (0, 0, 0, 0, 0, 0)


#: The classic ruleset's normal specialist catalog.  Every city in a bundle
#: carries the *whole* catalog -- counts may be zero -- because the projector
#: proves the specialist ids are ``range(n)`` and identical across cities.
DEFAULT_SPECIALISTS: tuple[SpecialistSpec, ...] = (
    SpecialistSpec(0, "Entertainer", 0, is_default=True),
    SpecialistSpec(1, "Scientist", 0),
    SpecialistSpec(2, "Taxman", 0),
)


@dataclass(frozen=True)
class WorkerTaskSpec:
    """One ``city_worker_task`` row; ``tile_offset`` indexes the city block."""

    tile_offset: int
    activity: str = "irrigate"
    target_extra: int = 0
    target_extra_name: str = "Irrigation"
    want: int = 10


@dataclass(frozen=True)
class CitySpec:
    """One city and every child row family it can carry."""

    ordinal: int
    worked_tiles: int = 1
    idle_tiles: int = 0
    specialists: tuple[SpecialistSpec, ...] = DEFAULT_SPECIALISTS
    mood: tuple[int, int, int, int] | None = None
    worker_tasks: tuple[WorkerTaskSpec, ...] = ()
    worklist_length: int = 0
    build_choices: int = 2
    improvements: int = 0
    trade_route_count: int = 0
    tile_yields: tuple[int, ...] = (0, 0, 0, 0, 0, 0)
    did_sell: bool = False

    @property
    def population_specialists(self) -> int:
        return sum(
            item.count for item in self.specialists
            if item.counts_toward_population
        )

    @property
    def size(self) -> int:
        return self.worked_tiles + self.population_specialists

    @property
    def resolved_mood(self) -> tuple[int, int, int, int]:
        """Mood counters cover only the non-specialist citizens."""
        if self.mood is not None:
            return self.mood
        return (0, self.worked_tiles, 0, 0)


@dataclass(frozen=True)
class ObservationCase:
    """A complete, reproducible native observation bundle description."""

    label: str
    seed: int = 0
    cities: tuple[CitySpec, ...] = ()
    tile_catalog: bool = True
    citizen_catalog: bool = True
    worklist_catalog: bool = False
    build_choice_catalog: bool = False
    improvement_catalog: bool = False
    rally_rows: bool = True
    own_units: int = 1
    visible_units: int = 1
    consuming_actions: int = 0
    tombstones: int = 0
    turn: int = 7
    topology: str = "square"

    def describe(self) -> str:
        return (
            f"{self.label} (seed={self.seed}, cities="
            + ",".join(
                f"[w={city.worked_tiles} s={city.population_specialists}"
                f"/{sum(item.count for item in city.specialists)}"
                f" size={city.size} tasks={len(city.worker_tasks)}]"
                for city in self.cities
            )
            + f", tile_catalog={self.tile_catalog}"
            f", citizen_catalog={self.citizen_catalog}"
            f", worklist_catalog={self.worklist_catalog}"
            f", build_choice_catalog={self.build_choice_catalog}"
            f", improvement_catalog={self.improvement_catalog}"
            f", consuming_actions={self.consuming_actions})"
        )


# --------------------------------------------------------------------------
# Tile geometry
# --------------------------------------------------------------------------

#: Tiles reserved for non-city use (own unit start, enemy unit, spare targets).
RESERVED_TILES = 8
#: Tiles allotted to each city block: centre + worked + idle + tasks.
CITY_TILE_STRIDE = 32


def city_tile_base(ordinal: int) -> int:
    return RESERVED_TILES + ordinal * CITY_TILE_STRIDE


def tile_xy(index: int, topology: str = "square") -> tuple[int, int]:
    """Mirror ``_map_coordinates_for_native_index`` for the given topology."""
    native_x, native_y = index % MAP_WIDTH, index // MAP_WIDTH
    if not topology.startswith("isometric_"):
        return native_x, native_y
    x = native_x + (native_y + (native_y & 1)) // 2
    return x, MAP_WIDTH + native_y - x


# --------------------------------------------------------------------------
# Row families
# --------------------------------------------------------------------------

PLAYER_REF = "p:1:10"
OTHER_REF = "p:2:20"


def meta_row(case: ObservationCase, known_tiles: int) -> str:
    return row("meta", {
        "state": "running",
        "turn": case.turn,
        "phase": 1,
        "cache": "human-client",
        "phase_mode": "players_alternate",
        "phase_count": 2,
        "active_phase": 1,
        "phase_ready": 1,
        "map_width": MAP_WIDTH,
        "map_height": MAP_HEIGHT,
        "topology": case.topology,
        "wrap_x": 1,
        "wrap_y": 0,
        "known_tile_count": known_tiles,
    })


def player_row() -> str:
    return row("player", {
        "ref": PLAYER_REF, "name": "Codex", "nation": "Roman",
        "government": "Despotism", "gold": 40, "tax": 30, "science": 60,
        "luxury": 10, "alive": 1, "phase_done": 0, "changeable_tax": 1,
        "max_rate": 70, "infrastructure_enabled": 0,
        "infrastructure_points": 0,
    })


def governance_rows() -> list[str]:
    rows = [row("governance", {
        "current_id": 1, "target_id": -1, "during_id": 0, "status": "stable",
        "finish_turn": -1, "turns_remaining": 0, "method": "random",
        "max_turns": 5, "untargeted_allowed": 1, "no_anarchy": 0,
        "can_revolution": 1, "choices_count": 4,
    })]
    names = ("Anarchy", "Despotism", "Monarchy", "Republic")
    for native_id, name in enumerate(names):
        rows.append(row("government", {
            "id": native_id, "name": name,
            "current": _bit(native_id == 1),
            "target": 0,
            "during": _bit(native_id == 0),
            "can_change": _bit(native_id >= 2),
        }))
    return rows


def research_rows() -> list[str]:
    rows = [row("research", {
        "techs": 2, "future": 0, "target": "Writing", "target_id": 4,
        "goal": "Pottery", "goal_id": 5, "bulbs": 4, "cost": 20, "output": 3,
        "choices_count": 5,
        "choices_digest": "fnv1a64-da5a057e14a5995d",
    })]
    catalog = (
        (3, "Alphabet", "known", 0, 0),
        (4, "Writing", "available", 1, 1),
        (5, "Pottery", "reachable", 0, 1),
        (6, "Bronze%20Working", "available", 1, 1),
        (1000, "Unset", "unset", 0, 1),
    )
    for native_id, name, state, can_target, can_goal in catalog:
        rows.append(row("research_tech", {
            "id": native_id, "name": name, "state": state,
            "can_target": can_target, "can_goal": can_goal,
        }))
    graph = (
        (3, "Alphabet", 1, -1, 0, 0),
        (4, "Writing", 1, 4, 0, 20),
        (5, "Pottery", 1, 4, 1, 40),
        (6, "Bronze%20Working", 1, 6, 0, 20),
    )
    for native_id, name, reachable, step, unknown, cost in graph:
        rows.append(row("research_graph", {
            "id": native_id, "name": name, "reachable": reachable,
            "next_step": step, "unknown_prerequisites": unknown,
            "path_cost": cost,
        }))
    rows.append(row("research_edge", {
        "tech": 3, "prerequisite": 3, "kind": "root",
    }))
    rows.append(row("research_edge", {
        "tech": 5, "prerequisite": 4, "kind": "direct",
    }))
    rows.append(row("research_unlock", {
        "tech": 4, "kind": "unit", "native_id": 12, "name": "Settlers",
        "scope": "build",
    }))
    return rows


def spaceship_rows() -> list[str]:
    rows = [row("spaceship", {
        "state": "none", "structurals": 0, "structurals_placed": 0,
        "components": 0, "fuel": 0, "propulsion": 0, "modules": 0,
        "habitation": 0, "life_support": 0, "solar_panels": 0,
        "launch_year": 9999, "population": 0, "mass": 0,
        "support_permille": 0, "energy_permille": 0, "success_permille": 0,
        "travel_time_millis": 0, "has_capital": 1, "can_launch": 0,
    })]
    for slot in range(32):
        rows.append(row("spaceship_structural", {
            "slot": slot, "x": slot, "y": 0,
            "required_slot": -1 if slot == 0 else 0,
            "placed": 0,
            "required_connected": 1 if slot == 0 else 0,
            "can_place": 0,
        }))
    return rows


def multiplier_row() -> str:
    return row("multiplier", {
        "id": 0, "name": "Policy", "value": 50, "target": 50, "start": 0,
        "stop": 100, "step": 10, "minimum_turns": 2, "changed_turn": 0,
        "can_change": 0, "choice_count": 11,
    })


def diplomacy_row() -> str:
    return row("diplomacy", {
        "other": OTHER_REF, "name": "Other", "nation": "Romans",
        "state": "Peace", "contact": 5, "alive": 1, "turns_left": 0,
        "can_meet": 1, "meeting": 0, "generation": 0, "self_accepted": 0,
        "other_accepted": 0, "clause_count": 0,
        "clauses_digest": "fnv1a64-cbf29ce484222325",
        "intel_level": "contact", "team": 2, "team_name": "Team%202",
        "same_team": 0, "controller": "human", "connected": 1, "score": 17,
        "gold": 23, "government": "Despotism", "has_embassy": 0,
        "other_has_embassy": 0, "gives_vision": 0, "receives_vision": 0,
        "gives_shared_tiles": 0, "receives_shared_tiles": 0, "can_cancel": 1,
        "cancel_reason": "allowed",
    })


def tile_row(
    index: int, *, owner: str = "none", terrain: str = "Grassland",
    topology: str = "square",
) -> str:
    x, y = tile_xy(index, topology)
    return row("tile", {
        "index": index, "x": x, "y": y, "known": 2, "terrain": terrain,
        "owner": owner, "placing_extra": -1, "placing_extra_name": "none",
        "placing_turns": 0, "placing_time": 1,
    })


def city_family_rows(case: ObservationCase, spec: CitySpec) -> list[str]:
    """Every row that belongs to one city, honouring the case's catalog flags."""
    ref = f"c:{20 + spec.ordinal}:{200 + spec.ordinal}"
    name = f"City{spec.ordinal}"
    base = city_tile_base(spec.ordinal)
    cx, cy = tile_xy(base, case.topology)
    happy, content, unhappy, angry = spec.resolved_mood

    tile_count = 1 + spec.worked_tiles + spec.idle_tiles
    citizen_base = {output: 0 for output in CITY_OUTPUTS}
    rows: list[str] = []

    if case.citizen_catalog:
        # The city centre tile: worked and free, never a "worker".
        rows.append(row("city_tile", {
            "city": ref, "tile": base, "worked": 1, "free_worked": 1,
            "can_work": 1,
            **{name_: value for name_, value in zip(YIELD_FIELDS, spec.tile_yields)},
        }))
        for index, output in zip(YIELD_FIELDS, CITY_OUTPUTS):
            citizen_base[output] += spec.tile_yields[YIELD_FIELDS.index(index)]
        for offset in range(spec.worked_tiles):
            rows.append(row("city_tile", {
                "city": ref, "tile": base + 1 + offset, "worked": 1,
                "free_worked": 0, "can_work": 1,
                **{
                    name_: value
                    for name_, value in zip(YIELD_FIELDS, spec.tile_yields)
                },
            }))
            for position, output in enumerate(CITY_OUTPUTS):
                citizen_base[output] += spec.tile_yields[position]
        for offset in range(spec.idle_tiles):
            rows.append(row("city_tile", {
                "city": ref,
                "tile": base + 1 + spec.worked_tiles + offset,
                "worked": 0, "free_worked": 0, "can_work": 1,
                **{name_: 0 for name_ in YIELD_FIELDS},
            }))
        for item in spec.specialists:
            rows.append(row("city_specialist", {
                "city": ref, "specialist": item.native_id, "name": item.name,
                "count": item.count,
                "counts_toward_population": _bit(item.counts_toward_population),
                "can_use": _bit(item.can_use),
                "is_default": _bit(item.is_default),
                **{
                    name_: value
                    for name_, value in zip(YIELD_FIELDS, item.yields)
                },
            }))
            for position, output in enumerate(CITY_OUTPUTS):
                citizen_base[output] += item.count * item.yields[position]

    if case.build_choice_catalog:
        for position in range(spec.build_choices):
            rows.append(_build_choice_row(ref, position))
    if case.worklist_catalog:
        for position in range(spec.worklist_length):
            kind, native_id, production = _production(position)
            rows.append(row("city_worklist", {
                "city": ref, "position": position, "production_kind": kind,
                "production_id": native_id, "production_name": production,
            }))
    if case.improvement_catalog:
        for position in range(spec.improvements):
            rows.append(row("city_improvement", {
                "city": ref, "improvement_id": 40 + position,
                "name": f"Building{position}",
                "sellable": _bit(not spec.did_sell), "sell_price": 25,
            }))
    if case.rally_rows:
        rows.append(row("city_rally", {
            "city": ref, "active": 0, "persistent": 0, "vigilant": 0,
            "order_count": 0,
            "orders_digest": "fnv1a64-0000000000000000",
        }))
    for task in spec.worker_tasks:
        rows.append(row("city_worker_task", {
            "city": ref, "tile": base + task.tile_offset,
            "activity": task.activity, "target_extra": task.target_extra,
            "target_extra_name": task.target_extra_name, "want": task.want,
        }))

    rows.append(row("city_site", {
        "ref": ref, "owner": PLAYER_REF, "name": name, "tile": base,
        "x": cx, "y": cy, "size": spec.size, "visibility": "own",
    }))

    outputs: dict[str, object] = {}
    for output in CITY_OUTPUTS:
        net = citizen_base[output]
        outputs[f"{output}_citizen_base"] = citizen_base[output]
        outputs[f"{output}_net"] = net
        outputs[f"{output}_surplus"] = net
        outputs[f"{output}_usage"] = 0
        outputs[f"{output}_waste"] = 0
        outputs[f"{output}_unhappy_penalty"] = 0

    rows.append(row("city", {
        "ref": ref, "name": name, "tile": base, "x": cx, "y": cy,
        "size": spec.size,
        "food": outputs["food_surplus"],
        "shields": outputs["shield_surplus"],
        "trade": outputs["trade_surplus"],
        "production_kind": "unit", "production_id": 12,
        "production_name": "Settlers", "shield_stock": 10, "shield_cost": 40,
        "buy_cost": 30, "can_buy": 0, "can_change": 1,
        # The counts stay truthful even when the child catalog is absent: that
        # is exactly the compact-OBS shape whose child rows live in a separate
        # STATE_SCOPE section, and the shape the worker-task wedge mishandled.
        "citizen_tile_count": tile_count,
        "specialist_type_count": len(spec.specialists),
        "worklist_length": spec.worklist_length,
        "build_choice_count": spec.build_choices,
        "improvement_count": spec.improvements,
        "trade_route_count": spec.trade_route_count,
        "trade_route_capacity": 3,
        "did_sell": _bit(spec.did_sell), "allow_disband": 0,
        "new_citizens": "default", "options_conflict": 0,
        "airlift_remaining": 1, "airlift_max": 1, "governor_enabled": 0,
        "citizen_happy": happy, "citizen_content": content,
        "citizen_unhappy": unhappy, "citizen_angry": angry,
        "citizen_workers": spec.worked_tiles,
        "citizen_specialists": spec.population_specialists,
        "food_stock": 0, "granary_size": 20, "growth_turns": 1000000000,
        "pollution": 0,
        **outputs,
    }))
    return rows


def _production(position: int) -> tuple[str, int, str]:
    """A distinct production key per position.

    The projector proves ``(city, production_kind, production_id)`` is unique
    inside a build-choice catalog and that a production id always carries the
    same name, so the fixture may not reuse a key across positions.
    """
    if position == 0:
        return "unit", 12, "Settlers"
    return "improvement", 4 + position, f"Improvement{4 + position}"


def _build_choice_row(ref: str, position: int) -> str:
    kind, native_id, production = _production(position)
    unit = kind == "unit"
    return row("city_build_choice", {
        "city": ref, "production_kind": kind, "production_id": native_id,
        "production_name": production, "can_queue": 1, "can_build_now": 1,
        "shield_cost": 40, "shield_stock_after_change": 10, "turns": 20,
        "turns_with_stock": 15, "upkeep_food": 0, "upkeep_shield": 0,
        "upkeep_trade": 0, "upkeep_gold": 0, "upkeep_luxury": 0,
        "upkeep_science": 0,
        "happy_cost": 0 if unit else -1,
        "unit_attack": 1 if unit else -1,
        "unit_defense": 1 if unit else -1,
        "unit_move_rate": 3 if unit else -1,
        "unit_hp": 10 if unit else -1,
        "unit_firepower": 1 if unit else -1,
        "unit_vision_radius_sq": 2 if unit else -1,
        "unit_transport_capacity": 0 if unit else -1,
        "unit_fuel": 0 if unit else -1,
        "unit_pop_cost": 0 if unit else -1,
        "unit_bombard_rate": 0 if unit else -1,
        "unit_city_size": 1 if unit else -1,
        "unit_paradrop_range": 0 if unit else -1,
        "building_genus": "none" if unit else "Improvement",
        "building_obsolete": -1 if unit else 0,
        "building_redundant": -1 if unit else 0,
        "building_convert": -1 if unit else 0,
        "building_allows_units": -1 if unit else 0,
        "building_allows_extras": -1 if unit else 0,
        "building_prevents_disaster": -1 if unit else 0,
        "building_protects_vs_actions": -1 if unit else 0,
        "building_allows_actions": -1 if unit else 0,
    })


def own_unit_row(
    ordinal: int, native_tile: int, home_city: str, topology: str = "square",
) -> str:
    x, y = tile_xy(native_tile, topology)
    return row("unit", {
        "ref": f"u:{10 + ordinal}:{100 + ordinal}", "scope": "own",
        "owner": PLAYER_REF, "type_id": 12, "type": "Settlers",
        "home_city": home_city, "converts_to_id": -1, "converts_to": "none",
        "tile": native_tile, "x": x, "y": y, "hp": 10, "veteran": 0,
        "veteran_name": "Regular", "veteran_levels": 1, "veteran_power": 100,
        "veteran_move_bonus": 0, "fuel": 0, "max_hp": 100, "max_fuel": 0,
        "move_rate": 3, "attack": 1, "defense": 1, "firepower": 1,
        "base_upkeep_food": 0, "base_upkeep_shield": 0, "base_upkeep_trade": 0,
        "base_upkeep_gold": 0, "base_upkeep_luxury": 0,
        "base_upkeep_science": 0, "upkeep_food": 0, "upkeep_shield": 0,
        "upkeep_trade": 0, "upkeep_gold": 0, "upkeep_luxury": 0,
        "upkeep_science": 0, "moves": 3, "activity": "idle",
        "activity_target": -1, "activity_target_name": "none",
        "activity_progress": 0, "transport_state": "untransported",
        "transporter": "none", "transport_capacity": 0, "occupied": 0,
        "paradropped": 0, "paradrop_range": 0, "controller": "none",
        "has_orders": 0, "orders_repeat": 0, "orders_vigilant": 0,
        "order_count": 0, "orders_digest": "fnv1a64-0000000000000000",
        "orders_destination": -1, "action_decision_want": "nothing",
        "action_decision_tile": -1,
    }, schema_key="unit_own")


def visible_unit_row(
    ordinal: int, native_tile: int, topology: str = "square",
) -> str:
    x, y = tile_xy(native_tile, topology)
    return row("unit", {
        "ref": f"u:{40 + ordinal}:{400 + ordinal}", "scope": "visible",
        "owner": OTHER_REF, "type_id": 13, "type": "Warriors",
        "tile": native_tile, "x": x, "y": y, "hp": 8, "veteran": 0,
        "veteran_name": "Regular", "veteran_levels": 1, "veteran_power": 100,
        "veteran_move_bonus": 0, "max_hp": 100, "max_fuel": 0,
        "move_rate": 3, "attack": 1, "defense": 1, "firepower": 1,
        "base_upkeep_food": 0, "base_upkeep_shield": 0, "base_upkeep_trade": 0,
        "base_upkeep_gold": 0, "base_upkeep_luxury": 0,
        "base_upkeep_science": 0,
    }, schema_key="unit_visible")


# --------------------------------------------------------------------------
# Bundle assembly
# --------------------------------------------------------------------------


def build_rows(case: ObservationCase) -> tuple[str, ...]:
    """Render one case into a sorted, deduplicated native row bundle."""
    rows: list[str] = []
    rows.append(player_row())
    rows.extend(governance_rows())
    rows.append(multiplier_row())
    rows.extend(spaceship_rows())
    rows.extend(research_rows())
    rows.append(diplomacy_row())

    own_tile = 0
    enemy_tile = 1
    referenced: set[int] = {own_tile, enemy_tile}

    for spec in case.cities:
        rows.extend(city_family_rows(case, spec))
        base = city_tile_base(spec.ordinal)
        referenced.add(base)
        if case.citizen_catalog:
            for offset in range(1 + spec.worked_tiles + spec.idle_tiles):
                referenced.add(base + offset)
        for task in spec.worker_tasks:
            referenced.add(base + task.tile_offset)

    home = f"c:{20}:{200}" if case.cities else "none"
    for ordinal in range(case.own_units):
        rows.append(own_unit_row(ordinal, own_tile, home, case.topology))
    for ordinal in range(case.visible_units):
        rows.append(visible_unit_row(ordinal, enemy_tile, case.topology))
    for ordinal in range(case.tombstones):
        rows.append(row("tombstone", {
            "ref": f"u:{900 + ordinal}:{9000 + ordinal}", "kind": "unit",
        }))

    if case.tile_catalog:
        for index in sorted(referenced):
            owner = PLAYER_REF if index >= RESERVED_TILES else "none"
            rows.append(tile_row(index, owner=owner, topology=case.topology))
    rows.append(meta_row(case, len(referenced)))

    rows.extend(
        complete_v2_row(item)
        for item in action_rows(case, own_tile=own_tile, enemy_tile=enemy_tile)
    )
    return tuple(sorted(set(rows)))


#: Consuming action rules, in the order the sweep spends them.  Every entry is
#: a real ``_ACTION_RULES`` key whose ``consuming`` flag is set.
CONSUMING_RULES: tuple[tuple[str, str, str, str, str], ...] = (
    ("Suicide Attack", "unit.attack", "Stack", "Unit Attack", "none"),
    ("Suicide Attack 2", "unit.attack", "Stack", "Unit Attack", "none"),
    ("Found City", "city.found", "Tile", "Unit Found City",
     "city_name-required"),
)


def action_rows(
    case: ObservationCase, *, own_tile: int, enemy_tile: int,
) -> list[str]:
    """Build the exact action catalog an eligible phase has to carry.

    The research/rates entries are not decoration: ``_validate_cross_links``
    proves the action set equals the set derivable from the research and player
    rows, so a bundle missing one is rejected.  Tile-targeted unit actions are
    emitted only when the tile catalog is present, mirroring the C emitter --
    the compact OBS builder emits only global player/communication actions.
    """
    actor = "u:10:100" if case.own_units else "none"
    rows = [
        _action(1, "phase.end", "none", -1, "phase.end", "player",
                "phase_end", 0),
        _action(2, "research.set_target", "none", -1, "research.set_target",
                "Technology", "Research Target", 0, target_tech=6),
        _action(3, "research.set_goal", "none", -1, "research.set_goal",
                "Technology", "Research Goal", 0, target_tech=4),
        _action(4, "research.set_goal", "none", -1, "research.set_goal",
                "Technology", "Research Goal", 0, target_tech=6),
        _action(5, "research.set_goal", "none", -1, "research.set_goal",
                "Technology", "Research Goal", 0, target_tech=1000),
        _action(6, "economy.set_rates", "none", -1, "economy.set_rates",
                "Player", "Economic Rates", 0, "rates-required", max_rate=70),
    ]
    if not (case.own_units and case.tile_catalog):
        return rows
    slot = 7
    rows.append(_action(
        slot, "unit.move", actor, enemy_tile, "Unit Move", "Tile",
        "Unit Move", 0,
    ))
    slot += 1
    for index in range(min(case.consuming_actions, len(CONSUMING_RULES))):
        rule, kind, target_kind, result, args = CONSUMING_RULES[index]
        # A consuming action spends its actor.  The projector has to keep the
        # actor's remaining slots coherent while the unit is about to vanish.
        target = own_tile if rule == "Found City" else enemy_tile
        rows.append(_action(
            slot, kind, actor, target, rule, target_kind, result, 1, args,
        ))
        slot += 1
    return rows


def observation(
    case: ObservationCase, *, generation: int = 1, revision: int = 11,
) -> dict[str, object]:
    return {
        "generation": generation,
        "native_revision": revision,
        "rows": build_rows(case),
    }


# --------------------------------------------------------------------------
# Canonical case shapes
# --------------------------------------------------------------------------

ENTERTAINER = SpecialistSpec(
    native_id=0, name="Entertainer", count=1, is_default=True,
)


def specialists(
    *,
    entertainers: int,
    scientists: int = 0,
    superspecialists: int | None = None,
) -> tuple[SpecialistSpec, ...]:
    """Build a legal specialist catalog.

    Normal specialists must occupy native ids ``0..n-1`` (the projector proves
    ``population_ids == range(len(population_ids))``), superspecialists follow.
    Exactly one specialist is the default and it must count toward population.
    A superspecialist can never be ``can_use``.

    ``superspecialists=None`` omits the superspecialist type entirely;
    ``superspecialists=0`` keeps the type in the catalog with a zero count,
    which is the ordinary steady state of a ruleset that defines one.
    """
    items = [
        SpecialistSpec(0, "Entertainer", entertainers, is_default=True),
        SpecialistSpec(1, "Scientist", scientists),
        SpecialistSpec(2, "Taxman", 0),
    ]
    if superspecialists is not None:
        items.append(SpecialistSpec(
            len(items), "Migrant", superspecialists,
            counts_toward_population=False, can_use=False,
        ))
    return tuple(items)


def basic_city(ordinal: int = 0, **kwargs: object) -> CitySpec:
    return CitySpec(ordinal=ordinal, **kwargs)  # type: ignore[arg-type]


def case(label: str, **kwargs: object) -> ObservationCase:
    kwargs.setdefault("cities", (basic_city(),))
    return ObservationCase(label=label, **kwargs)  # type: ignore[arg-type]


# --------------------------------------------------------------------------
# Deterministic case generator
# --------------------------------------------------------------------------


class CaseGenerator:
    """A seeded generator over the axes that historically bricked live games.

    Deliberately stdlib-only: ``random.Random(seed)`` is reproducible across
    interpreters for the operations used here (``randrange``/``choice`` on
    small ranges), and every emitted case prints its own seed so a failure is
    replayable with :func:`case_for_seed`.
    """

    def __init__(self, seed: int) -> None:
        self.seed = seed
        self._random = random.Random(seed)

    def draw(self) -> ObservationCase:
        rng = self._random
        city_count = rng.choice((0, 1, 1, 1, 2, 3, 5, 13))
        # The specialist *catalog shape* is a ruleset fact, so it is drawn once
        # per empire: the projector proves every city reports the same
        # specialist ids.  Only the counts vary per city.
        has_superspecialist = rng.random() < 0.25
        cities: list[CitySpec] = []
        for ordinal in range(city_count):
            workers = rng.randrange(0, 5)
            entertainers = rng.randrange(0, 4)
            scientists = rng.randrange(0, 2)
            if workers == 0 and entertainers + scientists == 0:
                # A city always has at least one citizen; size 0 is not a
                # legal C-side state and the projector rejects it.
                entertainers = 1
            spec_items = specialists(
                entertainers=entertainers,
                scientists=scientists,
                superspecialists=(
                    rng.randrange(0, 3) if has_superspecialist else None
                ),
            )
            mood = _mood_split(rng, workers)
            idle = rng.choice((0, 0, 1, 2))
            tasks: tuple[WorkerTaskSpec, ...] = ()
            if rng.random() < 0.5:
                # A worker task may only name a tile the city also reports as
                # one of its citizen tiles whenever that catalog is present, so
                # offsets stay inside the city's own tile block.
                available = 1 + workers + idle
                count = min(rng.randrange(1, 3), available)
                tasks = tuple(
                    WorkerTaskSpec(tile_offset=offset)
                    if offset % 2 else
                    WorkerTaskSpec(
                        tile_offset=offset, activity="cultivate",
                        target_extra=-1, target_extra_name="none",
                    )
                    for offset in range(count)
                )
            cities.append(CitySpec(
                ordinal=ordinal,
                worked_tiles=workers,
                idle_tiles=idle,
                specialists=spec_items,
                mood=mood,
                worker_tasks=tasks,
                worklist_length=rng.choice((0, 0, 1, 2)),
                build_choices=rng.choice((2, 3, 4)),
                improvements=rng.choice((0, 0, 1, 2)),
                trade_route_count=rng.choice((0, 0, 1, 2)),
                tile_yields=(rng.choice((0, 1, 2)), rng.choice((0, 1)), 0, 0, 0, 0),
            ))
        citizen_catalog = rng.random() < 0.75
        worklist_catalog = rng.random() < 0.4
        build_choice_catalog = worklist_catalog or rng.random() < 0.4
        return ObservationCase(
            label=f"generated-{self.seed}",
            seed=self.seed,
            cities=tuple(cities),
            tile_catalog=rng.random() < 0.6,
            citizen_catalog=citizen_catalog,
            worklist_catalog=worklist_catalog,
            build_choice_catalog=build_choice_catalog,
            improvement_catalog=rng.random() < 0.4,
            # protocol_v2.c v2_build_city_state_rows emits one city_rally next
            # to every city row, so rally rows always travel with cities.  See
            # RALLYLESS_BUNDLE_IS_UNATTRIBUTED in the attribution suite for
            # what the projector does when that stops being true.
            rally_rows=True,
            own_units=rng.choice((0, 1, 1, 2)),
            visible_units=rng.choice((0, 1)),
            consuming_actions=rng.choice((0, 0, 1, 2)),
            tombstones=rng.choice((0, 0, 1)),
            turn=rng.randrange(1, 200),
            topology=rng.choice((
                "square", "isometric_square", "hex", "isometric_hex",
            )),
        )


def _mood_split(rng: random.Random, workers: int) -> tuple[int, int, int, int]:
    """Split the *non-specialist* citizens across the four mood buckets."""
    remaining = workers
    happy = rng.randrange(0, remaining + 1)
    remaining -= happy
    content = rng.randrange(0, remaining + 1)
    remaining -= content
    unhappy = rng.randrange(0, remaining + 1)
    remaining -= unhappy
    return happy, content, unhappy, remaining


def case_for_seed(seed: int) -> ObservationCase:
    """Reproduce exactly the case a seed produced (printed on any failure)."""
    return CaseGenerator(seed).draw()


def cases_for_seeds(seeds: Iterable[int]) -> list[ObservationCase]:
    return [case_for_seed(seed) for seed in seeds]


__all__ = [
    "CITY_OUTPUTS", "CaseGenerator", "CitySpec", "MAP_HEIGHT", "MAP_WIDTH",
    "ObservationCase", "SpecialistSpec", "WorkerTaskSpec", "YIELD_FIELDS",
    "basic_city", "build_rows", "case", "case_for_seed", "cases_for_seeds",
    "city_family_rows", "city_tile_base", "observation", "row", "specialists",
    "tile_row", "tile_xy",
]
