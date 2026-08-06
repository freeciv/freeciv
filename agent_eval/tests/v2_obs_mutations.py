"""Genuinely-invalid mutations, one per native OBS row kind.

Each entry breaks a fact the C emitter guarantees -- a duplicate key, a
contradiction between two rows, or a row emitted in a state where the emitter
never emits it.  The projector *must* reject every one of them, and the
rejection must be attributable to the named row kind: that is the
wedge-impossibility property.

These are the negative half of the rig.  The positive half lives in
``v2_obs_fixtures``: valid-per-C row sets the projector must accept.
"""

from __future__ import annotations

from collections.abc import Callable, Iterable, Sequence
from dataclasses import dataclass
import re

from agent_eval.tests import v2_obs_fixtures as fixtures

Rows = tuple[str, ...]


def _sorted(rows: Iterable[str]) -> Rows:
    return tuple(sorted(set(rows)))


def find(rows: Sequence[str], prefix: str) -> str:
    for row in rows:
        if row.startswith(prefix):
            return row
    raise AssertionError(f"no row starting with {prefix!r} in the base bundle")


def set_field(row: str, field: str, value: object) -> str:
    pattern = re.compile(rf"(?<= ){re.escape(field)}=[^ ]*")
    replaced, count = pattern.subn(f"{field}={value}", row, count=1)
    if count != 1:
        raise AssertionError(f"field {field!r} not found in {row[:80]!r}")
    return replaced


def edit(rows: Sequence[str], prefix: str, field: str, value: object) -> Rows:
    target = find(rows, prefix)
    return _sorted([row for row in rows if row != target]
                   + [set_field(target, field, value)])


def add(rows: Sequence[str], *extra: str) -> Rows:
    return _sorted(list(rows) + list(extra))


def drop(rows: Sequence[str], prefix: str) -> Rows:
    target = find(rows, prefix)
    return _sorted(row for row in rows if row != target)


def duplicate_with(rows: Sequence[str], prefix: str, **changes: object) -> Rows:
    target = find(rows, prefix)
    clone = target
    for field, value in changes.items():
        clone = set_field(clone, field, value)
    return add(rows, clone)


@dataclass(frozen=True)
class Mutation:
    """One invalid bundle derived from a valid one."""

    row_kind: str
    name: str
    apply: Callable[[Rows], Rows]
    why: str


# --------------------------------------------------------------------------
# The base bundle every mutation is derived from
# --------------------------------------------------------------------------


def base_case() -> fixtures.ObservationCase:
    """A rich but ordinary running-state bundle: two cities, tasks, actions."""
    return fixtures.ObservationCase(
        label="mutation-base",
        cities=(
            fixtures.CitySpec(
                ordinal=0,
                worked_tiles=2,
                idle_tiles=1,
                specialists=fixtures.specialists(entertainers=1, scientists=1),
                worker_tasks=(fixtures.WorkerTaskSpec(tile_offset=1),),
                worklist_length=1,
                build_choices=3,
                improvements=1,
                tile_yields=(2, 1, 0, 0, 0, 0),
            ),
            fixtures.CitySpec(
                ordinal=1,
                worked_tiles=1,
                specialists=fixtures.specialists(entertainers=0, scientists=0),
                worklist_length=0,
                build_choices=2,
                improvements=0,
            ),
        ),
        tile_catalog=True,
        citizen_catalog=True,
        worklist_catalog=True,
        build_choice_catalog=True,
        improvement_catalog=True,
        rally_rows=True,
        own_units=1,
        visible_units=1,
        consuming_actions=1,
    )


def base_rows() -> Rows:
    return fixtures.build_rows(base_case())


# --------------------------------------------------------------------------
# The catalog
# --------------------------------------------------------------------------


def _pregame_row() -> str:
    return fixtures.row("pregame", {
        "ref": "p:1:10", "leader": "Codex", "nation": "none", "sex": "male",
        "style": "none", "ready": 0, "nation_choices": 2, "style_choices": 1,
        "team_choices": 3,
    })


MUTATIONS: tuple[Mutation, ...] = (
    Mutation(
        "meta", "phase_beyond_phase_count",
        lambda rows: edit(rows, "meta ", "phase", 9),
        "the active phase index must be inside phase_count",
    ),
    Mutation(
        "player", "government_contradicts_governance",
        lambda rows: edit(rows, "player ", "government", "Monarchy"),
        "player.government must name the government the governance row marks "
        "current",
    ),
    Mutation(
        "governance", "status_contradicts_the_revolution_clock",
        lambda rows: edit(rows, "governance ", "status", "anarchy"),
        "status is a pure function of current/target/during and finish_turn",
    ),
    Mutation(
        "government", "two_current_governments",
        lambda rows: edit(rows, "government id=2 ", "current", 1),
        "exactly one government row is current",
    ),
    Mutation(
        "multiplier", "duplicate_native_id",
        lambda rows: duplicate_with(rows, "multiplier ", name="Policy2"),
        "multiplier native ids are unique",
    ),
    Mutation(
        "spaceship", "second_spaceship_row",
        lambda rows: duplicate_with(rows, "spaceship state=", population=1),
        "a player has at most one spaceship row",
    ),
    Mutation(
        "spaceship_structural", "duplicate_slot",
        lambda rows: duplicate_with(rows, "spaceship_structural slot=0 ", x=31),
        "spaceship structural slots are unique inside one player's ship",
    ),
    Mutation(
        "research", "choices_count_contradicts_the_catalog",
        lambda rows: edit(rows, "research techs=", "choices_count", 99),
        "choices_count counts the research_tech rows",
    ),
    Mutation(
        "research_tech", "known_tech_is_targetable",
        lambda rows: edit(rows, "research_tech id=3 ", "can_target", 1),
        "an already known technology can never be a research target",
    ),
    Mutation(
        "research_graph", "graph_row_without_a_tech",
        lambda rows: edit(rows, "research_graph id=3 ", "id", 777),
        "every research_graph row names a research_tech row",
    ),
    Mutation(
        "research_edge", "edge_prerequisite_is_unknown",
        lambda rows: edit(rows, "research_edge tech=5 ", "prerequisite", 888),
        "an edge may only reference technologies in the catalog",
    ),
    Mutation(
        "research_unlock", "unlock_tech_is_unknown",
        lambda rows: edit(rows, "research_unlock ", "tech", 999),
        "an unlock hangs off a technology in the catalog",
    ),
    Mutation(
        "diplomacy", "relation_with_self",
        lambda rows: edit(rows, "diplomacy other=", "other", "p:1:10"),
        "a player never holds a diplomacy row about itself",
    ),
    Mutation(
        "diplomacy_intel", "intel_without_an_embassy",
        lambda rows: add(rows, fixtures.row("diplomacy_intel", {
            "other": fixtures.OTHER_REF, "tax": 30, "science": 60,
            "luxury": 10, "culture": 0, "research_id": 4,
            "research_name": "Writing", "bulbs": 4, "cost": 20,
            "known_count": 0, "known_digest": "fnv1a64-cbf29ce484222325",
            "known_ids": "-",
        })),
        "embassy-grade intel rows exist only where the relation reports an "
        "embassy",
    ),
    Mutation(
        "diplomacy_clause", "clause_without_a_meeting",
        lambda rows: add(rows, fixtures.row("diplomacy_clause", {
            "other": fixtures.OTHER_REF, "generation": 0, "position": 0,
            "giver": "p:1:10", "type": "gold", "value_kind": "gold",
            "value": 5, "name": "none",
        })),
        "clause rows exist only inside an open meeting",
    ),
    Mutation(
        "tile", "duplicate_tile_index",
        lambda rows: duplicate_with(
            rows, f"tile index={fixtures.city_tile_base(0)} ", terrain="Plains",
        ),
        "tile indices are unique inside one catalog",
    ),
    Mutation(
        "infrastructure_extra", "extra_without_infrastructure",
        lambda rows: add(rows, fixtures.row("infrastructure_extra", {
            "id": 5, "name": "Irrigation", "cost": 5, "build_time": 2,
            "build_time_factor": 1,
        })),
        "the infrastructure catalog is dense: ids are exactly range(n)",
    ),
    Mutation(
        "city", "size_contradicts_the_citizen_counts",
        lambda rows: edit(rows, "city ref=c:20:200 ", "size", 99),
        "size equals workers plus normal specialists",
    ),
    Mutation(
        "city_site", "site_size_contradicts_the_city",
        lambda rows: edit(rows, "city_site ref=c:20:200 ", "size", 42),
        "a city and its own site agree on name, tile and size",
    ),
    Mutation(
        "city_tile", "second_free_worked_tile",
        lambda rows: edit(
            rows, f"city_tile city=c:20:200 tile={fixtures.city_tile_base(0) + 1} ",
            "free_worked", 1,
        ),
        "exactly one citizen tile is the free city centre",
    ),
    Mutation(
        "city_specialist", "two_default_specialists",
        lambda rows: edit(
            rows, "city_specialist city=c:20:200 specialist=1 ",
            "is_default", 1,
        ),
        "exactly one specialist type is the ruleset default",
    ),
    Mutation(
        "city_worker_task", "targeted_activity_without_its_extra",
        lambda rows: edit(
            rows, "city_worker_task city=c:20:200 ", "target_extra", -1,
        ),
        "irrigate/mine/road/clean tasks always name a target extra",
    ),
    Mutation(
        "city_worklist", "worklist_position_out_of_range",
        lambda rows: edit(rows, "city_worklist city=c:20:200 ", "position", 7),
        "worklist positions are exactly range(worklist_length)",
    ),
    Mutation(
        "city_build_choice", "buildable_now_but_not_queueable",
        lambda rows: edit(
            rows, "city_build_choice city=c:21:201 ", "can_queue", 0,
        ),
        "anything buildable now is queueable",
    ),
    Mutation(
        "city_improvement", "zero_sell_price",
        lambda rows: edit(
            rows, "city_improvement city=c:20:200 ", "sell_price", 0,
        ),
        "a present improvement always has a positive sell price",
    ),
    Mutation(
        "city_rally", "second_rally_row_for_one_city",
        lambda rows: duplicate_with(
            rows, "city_rally city=c:20:200 ", persistent=1,
        ),
        "a city carries exactly one rally row",
    ),
    Mutation(
        "city_governor", "governor_row_in_obs",
        lambda rows: add(rows, fixtures.row("city_governor", {
            "city": "c:20:200", "min_food": 0, "min_production": 0,
            "min_trade": 0, "min_gold": 0, "min_luxury": 0, "min_science": 0,
            "weight_food": 1, "weight_production": 1, "weight_trade": 1,
            "weight_gold": 1, "weight_luxury": 1, "weight_science": 1,
            "celebration_weight": 0, "require_happy": 0, "maximize_growth": 0,
        })),
        "full CMA parameters travel only through the owned-city state scope",
    ),
    Mutation(
        "unit", "home_city_is_unknown",
        lambda rows: edit(rows, "unit ref=u:10:100 ", "home_city", "c:99:999"),
        "an owned unit's home city is one of the player's own cities",
    ),
    Mutation(
        "unit_route", "route_without_orders",
        lambda rows: add(rows, fixtures.row("unit_route", {
            "unit": "u:10:100", "order_index": 0, "reconstructable": 1,
            "step_count": 1,
        })),
        "a route belongs to a unit whose row reports orders",
    ),
    Mutation(
        "unit_route_step", "step_without_a_route",
        lambda rows: add(rows, fixtures.row("unit_route_step", {
            "unit": "u:10:100", "sequence": 0, "kind": "move", "tile": 1,
        })),
        "a route step requires its unit_route row",
    ),
    Mutation(
        "tombstone", "tombstone_kind_contradicts_its_ref",
        lambda rows: add(rows, fixtures.row("tombstone", {
            "ref": "u:900:9000", "kind": "city",
        })),
        "a tombstone's kind matches the entity family of its ref",
    ),
    Mutation(
        "vote", "vote_row_without_its_cast_action",
        lambda rows: add(rows, fixtures.row("vote", {
            "vote_no": 1, "caller": fixtures.OTHER_REF,
            "description": "set%20timeout%2060", "yes": 0, "no": 0,
            "abstain": 0, "num_voters": 2, "percent_required": 50,
            "team_only": 0, "current_vote": 0, "can_vote": 1,
            "status": "active", "outcome_turn": -1, "outcome_phase": -1,
        })),
        "every votable vote carries its player.cast_vote action",
    ),
    Mutation(
        "chat", "chat_sequence_zero",
        lambda rows: add(rows, fixtures.row("chat", {
            "sequence": 0, "turn": 1, "phase": 0, "sender": "server",
            "sender_name": "none", "self": 0, "channel": "global",
            "event": 0, "truncated": 0, "message": "hello",
        })),
        "chat sequence numbers start at one",
    ),
    Mutation(
        "action", "action_rule_outside_the_native_contract",
        lambda rows: edit(rows, "action slot=a0000000000000001 ", "kind",
                          "unit.teleport"),
        "every action row names a rule the native contract defines",
    ),
    Mutation(
        "pregame", "pregame_row_while_running",
        lambda rows: add(rows, _pregame_row()),
        "pregame rows exist only in the preparing client state",
    ),
)


MUTATIONS_BY_KIND: dict[str, tuple[Mutation, ...]] = {}
for _mutation in MUTATIONS:
    MUTATIONS_BY_KIND.setdefault(_mutation.row_kind, ())
    MUTATIONS_BY_KIND[_mutation.row_kind] += (_mutation,)


__all__ = [
    "MUTATIONS", "MUTATIONS_BY_KIND", "Mutation", "add", "base_case",
    "base_rows", "drop", "duplicate_with", "edit", "find", "set_field",
]
