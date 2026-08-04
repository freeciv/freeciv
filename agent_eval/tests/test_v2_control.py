from __future__ import annotations

import ast
from dataclasses import FrozenInstanceError
import json
from pathlib import Path
import re
import time
from types import MappingProxyType
import unittest
from unittest import mock

import agent_eval.v2_control as v2_control
from agent_eval.full_control_v2 import validate_legal_action_descriptor
from agent_eval.v2_control import (
    V2ActionResolution,
    V2ControlError,
    V2SeatControl,
)


def _action(
    slot: int,
    kind: str,
    actor: str,
    target: int,
    rule: str,
    target_kind: str,
    result: str,
    consuming: int,
    args: str = "none",
    *,
    target_tech: int = -1,
    vote_no: int = -1,
    target_government: int = -1,
    max_rate: int = 0,
    route_waypoint_limit: int = 0,
    infrastructure_cost: int = 0,
    infrastructure_turns: int = 0,
    infrastructure_choices: tuple[int, ...] = (),
    legality: str = "legal",
    probability_kind: str = "exact",
    probability_min: int = 200,
    probability_max: int = 200,
    gold_cost: int = -1,
    target_build_kind: str = "none",
    target_build: int = -1,
    spaceship_part: str = "none",
    spaceship_value: int = -1,
    target_multiplier: int = -1,
    multiplier_value: int = -1,
    source_specialist: int = -1,
    target_specialist: int = -1,
    target_extra: int = -1,
    activity: str = "none",
    target_name: str = "none",
    target_unit: str = "none",
    transport_context: str = "none",
    source_city: str = "none",
    destination_city: str = "none",
    counterpart: str = "none",
    meeting_generation: int = 0,
    clauses_digest: str = "fnv1a64-0000000000000000",
    self_accepted: int = 0,
    other_accepted: int = 0,
    relation_state: str = "none",
    outgoing_vision: int = 0,
    outgoing_shared_tiles: int = 0,
    clause_giver: str = "none",
    clause_type: str = "none",
    clause_value: int = -1,
    clause_name: str = "none",
    desired_acceptance: int = -1,
) -> str:
    def pct(value: str) -> str:
        return "".join(
            char if char.isalnum() or char in "._~-"
            else f"%{byte:02X}"
            for char in value
            for byte in char.encode("utf-8")
        )

    return (
        f"action slot=a{slot:016X} kind={kind} actor={actor} "
        f"counterpart={counterpart} meeting_generation={meeting_generation} "
        f"clauses_digest={clauses_digest} self_accepted={self_accepted} "
        f"other_accepted={other_accepted} relation_state={relation_state} "
        f"outgoing_vision={outgoing_vision} "
        f"outgoing_shared_tiles={outgoing_shared_tiles} "
        f"clause_giver={clause_giver} "
        f"clause_type={clause_type} clause_value={clause_value} "
        f"clause_name={pct(clause_name)} "
        f"desired_acceptance={desired_acceptance} target_tile={target} "
        f"source_city={source_city} "
        f"destination_city={destination_city} target_unit={target_unit} "
        f"transport_context={transport_context} target_tech={target_tech} "
        f"vote_no={vote_no} target_government={target_government} "
        f"max_rate={max_rate} "
        f"route_waypoint_limit={route_waypoint_limit} "
        f"infrastructure_cost={infrastructure_cost} "
        f"infrastructure_turns={infrastructure_turns} "
        f"infrastructure_choice_count={len(infrastructure_choices)} "
        "infrastructure_choices="
        f"{','.join(map(str, infrastructure_choices)) if infrastructure_choices else '-'} "
        f"target_build_kind={target_build_kind} target_build={target_build} "
        f"spaceship_part={spaceship_part} spaceship_value={spaceship_value} "
        f"target_multiplier={target_multiplier} "
        f"multiplier_value={multiplier_value} "
        f"source_specialist={source_specialist} "
        f"target_specialist={target_specialist} "
        f"target_extra={target_extra} activity={activity} "
        f"target_name={pct(target_name)} "
        f"native_rule={pct(rule)} "
        f"target_kind={pct(target_kind)} result={pct(result)} "
        f"actor_consuming_always={consuming} legality={legality} "
        f"probability_kind={probability_kind} "
        f"probability_min={probability_min} probability_max={probability_max} "
        f"gold_cost={gold_cost} "
        f"args={args}"
    )


def valid_rows(*, actions: bool = True) -> tuple[str, ...]:
    rows = [
        (
            f"meta state=running turn=7 phase={1 if actions else 0} "
            "cache=human-client phase_mode=players_alternate phase_count=2 "
            f"active_phase={1 if actions else 0} "
            f"phase_ready={1 if actions else 0} map_width=16 map_height=16 "
            "topology=square wrap_x=1 wrap_y=0 known_tile_count=3"
        ),
        (
            "player ref=p:1:10 name=Codex nation=Roman government=Despotism "
            "gold=40 tax=30 science=60 luxury=10 alive=1 phase_done=0 "
            "changeable_tax=1 max_rate=70 infrastructure_enabled=0 "
            "infrastructure_points=0"
        ),
        (
            "governance current_id=1 target_id=-1 during_id=0 status=stable "
            "finish_turn=-1 turns_remaining=0 method=random max_turns=5 "
            "untargeted_allowed=1 no_anarchy=0 can_revolution=1 "
            "choices_count=4"
        ),
        (
            "government id=0 name=Anarchy current=0 target=0 during=1 "
            "can_change=0"
        ),
        (
            "government id=1 name=Despotism current=1 target=0 during=0 "
            "can_change=0"
        ),
        (
            "government id=2 name=Monarchy current=0 target=0 during=0 "
            "can_change=1"
        ),
        (
            "government id=3 name=Republic current=0 target=0 during=0 "
            "can_change=1"
        ),
        (
            "multiplier id=0 name=Policy value=50 target=50 start=0 "
            "stop=100 step=10 minimum_turns=2 changed_turn=0 "
            "can_change=0 choice_count=11"
        ),
        (
            "spaceship state=none structurals=0 structurals_placed=0 "
            "components=0 fuel=0 propulsion=0 modules=0 habitation=0 "
            "life_support=0 solar_panels=0 launch_year=9999 population=0 "
            "mass=0 support_permille=0 energy_permille=0 success_permille=0 "
            "travel_time_millis=0 has_capital=1 can_launch=0"
        ),
        (
            "research techs=2 future=0 target=Writing target_id=4 "
            "goal=Pottery goal_id=5 bulbs=4 cost=20 output=3 "
            "choices_count=5 choices_digest=fnv1a64-da5a057e14a5995d"
        ),
        (
            "research_tech id=3 name=Alphabet state=known "
            "can_target=0 can_goal=0"
        ),
        (
            "research_tech id=4 name=Writing state=available "
            "can_target=1 can_goal=1"
        ),
        (
            "research_tech id=5 name=Pottery state=reachable "
            "can_target=0 can_goal=1"
        ),
        (
            "research_tech id=6 name=Bronze%20Working state=available "
            "can_target=1 can_goal=1"
        ),
        (
            "research_tech id=1000 name=Unset state=unset "
            "can_target=0 can_goal=1"
        ),
        (
            "research_graph id=3 name=Alphabet reachable=1 next_step=-1 "
            "unknown_prerequisites=0 path_cost=0"
        ),
        (
            "research_graph id=4 name=Writing reachable=1 next_step=4 "
            "unknown_prerequisites=0 path_cost=20"
        ),
        (
            "research_graph id=5 name=Pottery reachable=1 next_step=4 "
            "unknown_prerequisites=1 path_cost=40"
        ),
        (
            "research_graph id=6 name=Bronze%20Working reachable=1 "
            "next_step=6 unknown_prerequisites=0 path_cost=20"
        ),
        "research_edge tech=3 prerequisite=3 kind=root",
        "research_edge tech=5 prerequisite=4 kind=direct",
        (
            "research_unlock tech=4 kind=unit native_id=12 name=Settlers "
            "scope=build"
        ),
        "diplomacy other=p:2:20 name=Other nation=Romans state=Peace "
        "contact=5 alive=1 turns_left=0 can_meet=1 meeting=0 generation=0 "
        "self_accepted=0 other_accepted=0 clause_count=0 "
        "clauses_digest=fnv1a64-cbf29ce484222325 has_embassy=0 "
        "other_has_embassy=0 gives_vision=0 "
        "receives_vision=0 gives_shared_tiles=0 receives_shared_tiles=0 "
        "can_cancel=1 cancel_reason=allowed",
        "tile index=5 x=1 y=2 known=2 terrain=Grassland owner=p:1:10 placing_extra=-1 placing_extra_name=none placing_turns=0 placing_time=1",
        "tile index=6 x=2 y=2 known=2 terrain=Plains owner=p:2:20 placing_extra=-1 placing_extra_name=none placing_turns=0 placing_time=1",
        "tile index=7 x=3 y=2 known=1 terrain=Hills owner=none placing_extra=-1 placing_extra_name=none placing_turns=0 placing_time=1",
        (
            "city_site ref=c:20:200 owner=p:1:10 name=Alpha%20Centauri "
            "tile=5 x=1 y=2 size=2 visibility=own"
        ),
        (
            "city ref=c:20:200 name=Alpha%20Centauri tile=5 x=1 y=2 size=2 "
            "food=3 shields=2 trade=-1 production_kind=unit "
            "production_id=12 production_name=Settlers shield_stock=10 "
            "shield_cost=40 buy_cost=30 can_buy=1 can_change=1"
            " citizen_tile_count=1 specialist_type_count=1 "
            "worklist_length=0 build_choice_count=2 improvement_count=0 "
            "did_sell=0 allow_disband=0 new_citizens=default "
            "options_conflict=0 airlift_remaining=1 airlift_max=1 "
            "governor_enabled=0"
        ),
        "city_tile city=c:20:200 tile=5 worked=1 free_worked=1 can_work=1",
        (
            "city_specialist city=c:20:200 specialist=0 name=Entertainer "
            "count=2 can_use=1 is_default=1"
        ),
        (
            "city_build_choice city=c:20:200 production_kind=improvement "
            "production_id=5 production_name=Granary can_queue=1 "
            "can_build_now=1"
        ),
        (
            "city_build_choice city=c:20:200 production_kind=unit "
            "production_id=12 production_name=Settlers can_queue=1 "
            "can_build_now=1"
        ),
        (
            "city_rally city=c:20:200 active=0 persistent=0 vigilant=0 "
            "order_count=0 orders_digest=fnv1a64-0000000000000000"
        ),
        (
            "unit ref=u:10:100 scope=own owner=p:1:10 type_id=12 "
            "type=Settlers home_city=c:20:200 converts_to_id=-1 "
            "converts_to=none tile=5 "
            "x=1 y=2 hp=10 moves=3 activity=idle activity_target=-1 "
            "activity_target_name=none activity_progress=0 "
            "transport_state=untransported transporter=none "
            "transport_capacity=0 occupied=0 paradropped=0 paradrop_range=0 "
            "controller=none has_orders=0 orders_repeat=0 "
            "orders_vigilant=0 order_count=0 "
            "orders_digest=fnv1a64-0000000000000000 "
            "orders_destination=-1"
        ),
        (
            "unit ref=u:11:101 scope=visible owner=p:2:20 type_id=13 "
            "type=Warriors tile=6 "
            "x=2 y=2 hp=8"
        ),
        "tombstone ref=u:99:999 kind=unit",
    ]
    rows.extend(
        "spaceship_structural "
        f"slot={slot} x={slot} y=0 required_slot={-1 if slot == 0 else 0} "
        f"placed=0 required_connected={1 if slot == 0 else 0} can_place=0"
        for slot in range(32)
    )
    if actions:
        rows.extend([
            _action(1, "phase.end", "none", -1, "phase.end", "player", "phase_end", 0),
            _action(2, "city.found", "u:10:100", 5, "Found City", "Tile", "Unit Found City", 1, "city_name-required"),
            _action(3, "unit.move", "u:10:100", 6, "Unit Move", "Tile", "Unit Move", 0),
            _action(4, "unit.move", "u:10:100", 6, "Unit Move 2", "Tile", "Unit Move", 0),
            _action(5, "unit.move", "u:10:100", 6, "Unit Move 3", "Tile", "Unit Move", 0),
            _action(6, "unit.attack", "u:10:100", 6, "Attack", "Stack", "Unit Attack", 0),
            _action(7, "unit.attack", "u:10:100", 6, "Attack 2", "Stack", "Unit Attack", 0),
            _action(8, "unit.attack", "u:10:100", 6, "Suicide Attack", "Stack", "Unit Attack", 1),
            _action(9, "unit.attack", "u:10:100", 6, "Suicide Attack 2", "Stack", "Unit Attack", 1),
            _action(10, "research.set_target", "none", -1, "research.set_target", "Technology", "Research Target", 0, target_tech=6),
            _action(11, "research.set_goal", "none", -1, "research.set_goal", "Technology", "Research Goal", 0, target_tech=4),
            _action(12, "research.set_goal", "none", -1, "research.set_goal", "Technology", "Research Goal", 0, target_tech=6),
            _action(13, "research.set_goal", "none", -1, "research.set_goal", "Technology", "Research Goal", 0, target_tech=1000),
            _action(14, "economy.set_rates", "none", -1, "economy.set_rates", "Player", "Economic Rates", 0, "rates-required", max_rate=70),
        ])
    return tuple(sorted(rows))


def pregame_rows(*, ready: bool = False) -> tuple[str, ...]:
    rows = [
        (
            "meta state=preparing turn=0 phase=0 cache=human-client "
            "phase_mode=concurrent phase_count=1 active_phase=0 "
            "phase_ready=0 map_width=1 map_height=1 topology=square "
            "wrap_x=0 wrap_y=0 known_tile_count=0"
        ),
        (
            "pregame ref=p:1:10 leader=Codex nation=none sex=male "
            f"style=none ready={int(ready)} nation_choices=2 "
            "style_choices=2 team_choices=3"
        ),
        _action(
            0x501, "pregame.set_ready", "p:1:10", -1,
            "pregame.set_ready", "Pregame Readiness", "Readiness Changed",
            0, "pregame-ready-required",
            desired_acceptance=0 if ready else 1,
        ),
    ]
    if not ready:
        rows.extend((
            _action(
            0x500, "pregame.configure", "p:1:10", -1,
            "pregame.configure", "Pregame Configuration",
            "Configuration Changed", 0, "pregame-config-required",
            ),
            _action(
                0x502, "pregame.set_team", "p:1:10", -1,
                "pregame.set_team", "Pregame Team", "Team Changed", 0,
                "pregame-team-required", target_name="team",
            ),
        ))
    return tuple(sorted(rows))


def vote_rows(
    *, phase_actions: bool = True, can_vote: bool = True,
    current_vote: str = "none", yes: int = 2,
) -> tuple[str, ...]:
    rows = list(valid_rows(actions=phase_actions))
    rows.append(
        "vote vote_no=42 description=Change%20the%20map%3F "
        f"yes={yes} no=1 abstain=0 num_voters=8 percent_required=60 "
        f"team_only=1 current_vote={current_vote} can_vote={int(can_vote)}"
    )
    if can_vote:
        rows.append(_action(
            15, "player.cast_vote", "p:1:10", -1,
            "player.cast_vote", "Vote", "Vote Recorded", 0,
            "vote-required", vote_no=42, target_name="vote",
        ))
    return tuple(sorted(rows))


def pregame_vote_rows(*, ready: bool = False) -> tuple[str, ...]:
    rows = list(pregame_rows(ready=ready))
    rows.extend((
        (
            "vote vote_no=42 description=Start%20now%3F yes=1 no=0 "
            "abstain=0 num_voters=2 percent_required=50 team_only=0 "
            "current_vote=none can_vote=1"
        ),
        _action(
            0x503, "player.cast_vote", "p:1:10", -1,
            "player.cast_vote", "Vote", "Vote Recorded", 0,
            "vote-required", vote_no=42, target_name="vote",
        ),
    ))
    return tuple(sorted(rows))


def governor_goal(*, science_weight: int = 2) -> dict[str, object]:
    outputs = (
        "food", "production", "trade", "gold", "luxury", "science",
    )
    return {
        "minimum_surplus": {name: 0 for name in outputs},
        "weights": {
            name: science_weight if name == "science" else 1
            for name in outputs
        },
        "celebration_weight": 1,
        "require_happy": False,
        "maximize_growth": False,
    }


def scoped_city_rows() -> tuple[str, ...]:
    return (
        _action(
            101, "city.set_production", "c:20:200", -1,
            "city.set_production", "Production", "Production Changed", 0,
            target_build_kind="improvement", target_build=5,
            target_name="Granary",
        ),
        _action(
            102, "city.buy_production", "c:20:200", -1,
            "city.buy_production", "Production", "Production Bought", 0,
            target_build_kind="unit", target_build=12,
            target_name="Settlers",
        ),
        _action(
            103, "city.set_worklist", "c:20:200", -1,
            "city.set_worklist", "City", "Worklist Changed", 0,
            "worklist-required", target_name="worklist",
        ),
        _action(
            104, "city.set_options", "c:20:200", -1,
            "city.set_options", "City", "City Options Changed", 0,
            "city-options-required", target_name="options",
        ),
        _action(
            105, "city.rename", "c:20:200", -1,
            "city.rename", "City", "City Renamed", 0,
            "city_name-required", target_name="name",
        ),
        _action(
            106, "city.set_governor", "c:20:200", -1,
            "city.set_governor", "City", "Governor Goal Set", 0,
            "governor-goal-required", target_name="governor",
        ),
    )


def city_management_control_rows() -> tuple[tuple[str, ...], tuple[str, ...]]:
    rows = list(valid_rows())
    rows = [
        row.replace(
            "worklist_length=0 build_choice_count=2 improvement_count=0 ",
            "worklist_length=2 build_choice_count=3 improvement_count=2 ",
        ).replace(
            "new_citizens=default options_conflict=0",
            "new_citizens=science options_conflict=1",
        ) if row.startswith("city ref=c:20:200 ") else row
        for row in rows
    ]
    rows.extend((
        (
            "city_worklist city=c:20:200 position=0 "
            "production_kind=improvement production_id=7 "
            "production_name=Temple"
        ),
        (
            "city_worklist city=c:20:200 position=1 "
            "production_kind=improvement production_id=7 "
            "production_name=Temple"
        ),
        (
            "city_build_choice city=c:20:200 production_kind=improvement "
            "production_id=7 production_name=Temple can_queue=0 "
            "can_build_now=0"
        ),
        (
            "city_improvement city=c:20:200 improvement_id=5 name=Granary "
            "sellable=1 sell_price=20"
        ),
        (
            "city_improvement city=c:20:200 improvement_id=7 name=Temple "
            "sellable=0 sell_price=10"
        ),
    ))
    scoped = scoped_city_rows() + (
        _action(
            107, "city.sell_improvement", "c:20:200", -1,
            "city.sell_improvement", "Improvement", "Improvement Sold", 0,
            target_build_kind="improvement", target_build=5,
            target_name="Granary",
        ),
    )
    return tuple(sorted(rows)), scoped


def citizen_control_rows() -> tuple[tuple[str, ...], tuple[str, ...]]:
    rows = [
        row for row in valid_rows()
        if not row.startswith("city_tile ")
        and not row.startswith("city_specialist ")
    ]
    rows = [
        row.replace(
            "size=2 food=3", "size=3 food=3",
        ).replace(
            "citizen_tile_count=1 specialist_type_count=1",
            "citizen_tile_count=4 specialist_type_count=2",
        ) if row.startswith("city ") else row
        for row in rows
    ]
    rows = [
        row.replace("size=2 visibility=own", "size=3 visibility=own")
        if row.startswith("city_site ref=c:20:200 ") else row
        for row in rows
    ]
    rows.extend((
        "tile index=8 x=4 y=2 known=2 terrain=Grassland owner=p:1:10",
        "city_tile city=c:20:200 tile=5 worked=1 free_worked=1 can_work=1",
        "city_tile city=c:20:200 tile=6 worked=1 free_worked=0 can_work=0",
        "city_tile city=c:20:200 tile=7 worked=1 free_worked=0 can_work=0",
        "city_tile city=c:20:200 tile=8 worked=0 free_worked=0 can_work=1",
        (
            "city_specialist city=c:20:200 specialist=0 name=Entertainer "
            "count=1 can_use=1 is_default=1"
        ),
        (
            "city_specialist city=c:20:200 specialist=1 name=Scientist "
            "count=0 can_use=1 is_default=0"
        ),
    ))
    scoped = scoped_city_rows() + (
        _action(
            130, "city.work_tile", "c:20:200", 8,
            "city.work_tile", "City Tile", "Citizen Assigned", 0,
            source_specialist=0, target_name="worked tile",
        ),
        _action(
            131, "city.unwork_tile", "c:20:200", 6,
            "city.unwork_tile", "City Tile", "Citizen Unassigned", 0,
            target_specialist=0, target_name="default specialist",
        ),
        _action(
            132, "city.unwork_tile", "c:20:200", 7,
            "city.unwork_tile", "City Tile", "Citizen Unassigned", 0,
            target_specialist=0, target_name="default specialist",
        ),
        _action(
            133, "city.set_specialist", "c:20:200", -1,
            "city.set_specialist", "Specialist", "Specialist Changed", 0,
            source_specialist=0, target_specialist=1,
            target_name="Scientist",
        ),
    )
    return tuple(sorted(rows)), scoped


def scoped_worker_rows() -> tuple[str, ...]:
    return (
        _action(
            103, "unit.start_activity", "u:10:100", -1,
            "unit.start_activity", "Worker Activity", "Activity Installed", 0,
            activity="cultivate", target_name="cultivate",
        ),
        _action(
            104, "unit.start_activity", "u:10:100", -1,
            "unit.start_activity", "Worker Activity", "Activity Installed", 0,
            target_extra=7, activity="road", target_name="Road",
        ),
        _action(
            105, "unit.start_activity", "u:10:100", -1,
            "unit.start_activity", "Worker Activity", "Activity Installed", 0,
            target_extra=8, activity="pillage", target_name="Irrigation",
        ),
    )


def scoped_unit_self_rows() -> tuple[str, ...]:
    return (
        _action(
            109, "unit.sentry", "u:10:100", -1,
            "unit.sentry", "Unit", "Sentry Installed", 0,
            activity="sentry", target_name="sentry",
        ),
        _action(
            110, "unit.fortify", "u:10:100", -1,
            "Fortify", "Self", "Fortify Installed", 0,
            activity="fortifying", target_name="fortifying",
        ),
        _action(
            111, "unit.convert", "u:10:100", -1,
            "Convert Unit", "Self", "Conversion Installed", 0,
            target_build_kind="unit", target_build=14,
            activity="convert", target_name="Engineers",
        ),
        _action(
            112, "unit.disband", "u:10:100", -1,
            "Disband Unit", "Self", "Unit Disbanded", 1,
            target_name="self",
        ),
        _action(
            113, "unit.homeless", "u:10:100", -1,
            "Unit Make Homeless", "Self", "Home City Cleared", 0,
            target_name="self",
        ),
    )


def noncombat_mobility_rows() -> tuple[tuple[str, ...], tuple[str, ...]]:
    rows = list(valid_rows())
    rows = [
        row.replace("paradrop_range=0", "paradrop_range=8")
        if row.startswith("unit ref=u:10:100 ") else row
        for row in rows
    ]
    rows.extend((
        "tile index=8 x=4 y=2 known=2 terrain=Grassland owner=p:1:10",
        (
            "city_site ref=c:21:201 owner=p:1:10 name=Beta tile=8 "
            "x=4 y=2 size=1 visibility=own"
        ),
        (
            "city ref=c:21:201 name=Beta tile=8 x=4 y=2 size=1 food=1 "
            "shields=1 trade=1 production_kind=unit production_id=12 "
            "production_name=Settlers shield_stock=0 shield_cost=40 "
            "buy_cost=0 can_buy=0 can_change=1 citizen_tile_count=1 "
            "specialist_type_count=1 worklist_length=0 "
            "build_choice_count=0 improvement_count=0 did_sell=0 "
            "allow_disband=0 new_citizens=default options_conflict=0 "
            "airlift_remaining=1 airlift_max=1 governor_enabled=0"
        ),
        "city_tile city=c:21:201 tile=8 worked=1 free_worked=1 can_work=1",
        (
            "city_specialist city=c:21:201 specialist=0 name=Entertainer "
            "count=1 can_use=1 is_default=1"
        ),
        (
            "city_rally city=c:21:201 active=0 persistent=0 vigilant=0 "
            "order_count=0 orders_digest=fnv1a64-0000000000000000"
        ),
    ))
    scoped = (
        _action(
            140, "unit.airlift", "u:10:100", -1,
            "Airlift Unit", "City", "Unit Airlift", 0,
            source_city="c:20:200", destination_city="c:21:201",
            target_name="Beta",
        ),
        _action(
            141, "unit.paradrop", "u:10:100", 6,
            "Paradrop Unit", "Tile", "Unit Paradrop", 0,
            target_name="destination",
        ),
        _action(
            142, "unit.paradrop", "u:10:100", 6,
            "Paradrop Unit Frighten", "Tile", "Unit Paradrop", 0,
            target_name="destination", legality="unresolved",
            probability_kind="not_implemented", probability_min=-1,
            probability_max=-1,
        ),
        _action(
            143, "unit.paradrop", "u:10:100", 6,
            "Paradrop Unit Enter", "Tile", "Unit Paradrop", 0,
            target_name="destination",
        ),
        _action(
            144, "unit.teleport", "u:10:100", 6,
            "Teleport", "Tile", "Teleport", 0,
            target_name="destination",
        ),
        _action(
            145, "unit.teleport", "u:10:100", 6,
            "Teleport2", "Tile", "Teleport", 0,
            target_name="destination",
        ),
        _action(
            146, "unit.teleport", "u:10:100", 6,
            "Teleport3", "Tile", "Teleport", 0,
            target_name="destination",
        ),
        _action(
            147, "unit.teleport", "u:10:100", 6,
            "Teleport Frighten", "Tile", "Teleport", 0,
            target_name="destination",
        ),
        _action(
            148, "unit.teleport", "u:10:100", 6,
            "Teleport Enter", "Tile", "Teleport", 0,
            target_name="destination",
        ),
    )
    return tuple(sorted(rows)), scoped


def economic_unit_rows() -> tuple[tuple[str, ...], tuple[str, ...]]:
    rows, _ = noncombat_mobility_rows()
    rows = tuple(sorted((*rows, (
        "city_site ref=c:30:300 owner=p:2:20 name=Gamma tile=6 "
        "x=2 y=2 size=4 visibility=visible"
    ), (
        "city_site ref=c:31:301 owner=p:2:20 name=Delta tile=7 "
        "x=3 y=2 size=3 visibility=known"
    ))))
    scoped = (
        _action(
            170, "unit.upgrade", "u:10:100", -1,
            "Upgrade Unit", "City", "Unit Upgrade", 0,
            destination_city="c:20:200", target_build_kind="unit",
            target_build=14, target_name="Engineers",
        ),
        _action(
            171, "unit.rehome", "u:10:100", -1,
            "Home City", "City", "Unit Home City", 0,
            destination_city="c:21:201", target_name="Beta",
        ),
        _action(
            172, "unit.join_city", "u:10:100", -1,
            "Join City", "City", "Unit Join City", 1,
            destination_city="c:20:200", target_name="Alpha Centauri",
        ),
        _action(
            173, "unit.establish_trade", "u:10:100", -1,
            "Establish Trade Route", "City", "Unit Establish Trade Route", 1,
            source_city="c:20:200", destination_city="c:30:300",
            target_name="Gamma", legality="unresolved",
            probability_kind="not_implemented", probability_min=-1,
            probability_max=-1,
        ),
        _action(
            174, "unit.marketplace", "u:10:100", -1,
            "Enter Marketplace", "City", "Unit Enter Marketplace", 1,
            source_city="c:20:200", destination_city="c:30:300",
            target_name="Gamma",
        ),
        _action(
            175, "unit.help_wonder", "u:10:100", -1,
            "Help Wonder", "City", "Unit Help Wonder", 1,
            destination_city="c:30:300", target_name="Gamma",
        ),
        _action(
            176, "unit.disband_recover", "u:10:100", -1,
            "Disband Unit Recover", "City", "Unit Disband Recover", 1,
            destination_city="c:20:200", target_name="Alpha Centauri",
        ),
    )
    return rows, scoped


def scoped_government_rows() -> tuple[str, ...]:
    return (
        _action(
            106, "government.revolution", "p:1:10", -1,
            "government.revolution", "Government", "Revolution Started", 0,
            target_government=0, target_name="Anarchy",
        ),
        _action(
            107, "government.change", "p:1:10", -1,
            "government.change", "Government", "Government Choice Recorded",
            0, target_government=2, target_name="Monarchy",
        ),
        _action(
            108, "government.change", "p:1:10", -1,
            "government.change", "Government", "Government Choice Recorded",
            0, target_government=3, target_name="Republic",
        ),
    )


def transport_state_rows() -> tuple[str, ...]:
    rows = list(valid_rows())
    rows.extend((
        (
            "unit ref=u:12:102 scope=own owner=p:1:10 type_id=20 "
            "type=Trireme home_city=none converts_to_id=-1 converts_to=none "
            "tile=5 x=1 y=2 hp=10 moves=3 activity=idle "
            "activity_target=-1 activity_target_name=none activity_progress=0 "
            "transport_state=untransported transporter=none "
            "transport_capacity=2 occupied=1 paradropped=0 paradrop_range=0 "
            "controller=none has_orders=0"
        ),
        (
            "unit ref=u:13:103 scope=own owner=p:1:10 type_id=13 "
            "type=Warriors home_city=none converts_to_id=-1 converts_to=none "
            "tile=5 x=1 y=2 hp=10 moves=3 activity=idle "
            "activity_target=-1 activity_target_name=none activity_progress=0 "
            "transport_state=transported transporter=u:12:102 "
            "transport_capacity=0 occupied=0 paradropped=0 paradrop_range=0 "
            "controller=none has_orders=0"
        ),
        (
            "unit ref=u:14:104 scope=own owner=p:1:10 type_id=13 "
            "type=Warriors home_city=none converts_to_id=-1 converts_to=none "
            "tile=6 x=2 y=2 hp=10 moves=3 activity=idle "
            "activity_target=-1 activity_target_name=none activity_progress=0 "
            "transport_state=untransported transporter=none "
            "transport_capacity=0 occupied=0 paradropped=0 paradrop_range=0 "
            "controller=none has_orders=0"
        ),
    ))
    return tuple(sorted(rows))


def scoped_transport_rows() -> MappingProxyType[str, tuple[str, ...]]:
    return MappingProxyType({
        "u:10:100": (
            _action(
                120, "unit.board", "u:10:100", -1,
                "Transport Board", "Unit", "Unit Transport Board", 0,
                target_name="transporter", target_unit="u:12:102",
            ),
        ),
        "u:12:102": (
            _action(
                121, "unit.load", "u:12:102", -1,
                "Transport Load", "Unit", "Unit Transport Load", 0,
                target_name="cargo", target_unit="u:10:100",
            ),
            _action(
                122, "unit.unload", "u:12:102", -1,
                "Transport Unload", "Unit", "Unit Transport Unload", 0,
                target_name="cargo", target_unit="u:13:103",
                transport_context="u:12:102",
            ),
        ),
        "u:13:103": (
            _action(
                123, "unit.deboard", "u:13:103", -1,
                "Transport Deboard", "Unit", "Unit Transport Deboard", 0,
                target_name="transporter", target_unit="u:12:102",
                transport_context="u:12:102",
            ),
            _action(
                124, "unit.disembark", "u:13:103", 6,
                "Transport Disembark", "Tile", "Unit Transport Disembark", 0,
                target_name="destination", transport_context="u:12:102",
            ),
        ),
        "u:14:104": (
            _action(
                125, "unit.embark", "u:14:104", -1,
                "Transport Embark", "Unit", "Unit Transport Embark", 0,
                target_name="transporter", target_unit="u:12:102",
            ),
        ),
    })


def rows_with_unknown_moves() -> tuple[str, ...]:
    rows = list(valid_rows())
    rows.append(
        "tile index=8 x=0 y=2 known=0 terrain=unknown owner=none"
    )
    for slot, rule in enumerate(
        ("Unit Move", "Unit Move 2", "Unit Move 3"), start=20,
    ):
        rows.append(_action(
            slot, "unit.move", "u:10:100", 8, rule, "Tile", "Unit Move", 0,
            legality="possibly_legal",
            probability_kind="unknown",
            probability_min=0,
            probability_max=200,
        ))
    return tuple(sorted(rows))


def complete_v2_row(row: str) -> str:
    """Fill the current closed-row defaults in legacy test fixtures."""
    if row.startswith("player ") and " infrastructure_enabled=" not in row:
        row += " infrastructure_enabled=0 infrastructure_points=0"
    if row.startswith("tile ") and " placing_extra=" not in row:
        row += (
            " placing_extra=-1 placing_extra_name=none placing_turns=0 "
            f"placing_time={'-1' if ' known=0 ' in row else '1'}"
        )
    if row.startswith("city ") and " citizen_happy=" not in row:
        def city_value(name: str) -> int:
            match = re.search(rf"(?:^| ){name}=(-?[0-9]+)(?: |$)", row)
            assert match is not None
            return int(match.group(1))

        size = city_value("size")
        output_surpluses = {
            "food": city_value("food"),
            "shield": city_value("shields"),
            "trade": city_value("trade"),
            "gold": 0,
            "luxury": 0,
            "science": 0,
        }
        row += (
            f" citizen_happy=0 citizen_content={size} citizen_unhappy=0 "
            f"citizen_angry=0 citizen_workers=0 citizen_specialists={size} "
            "food_stock=0 granary_size=20 growth_turns=1000000000 "
            "pollution=0"
        )
        for output, surplus in output_surpluses.items():
            net = max(0, surplus)
            usage = net - surplus
            row += (
                f" {output}_citizen_base=0 {output}_net={net} "
                f"{output}_surplus={surplus} {output}_usage={usage} "
                f"{output}_waste=0 {output}_unhappy_penalty=0"
            )
    if row.startswith(("city_tile ", "city_specialist ")) \
            and " food=" not in row:
        row += " food=0 shields=0 trade=0 gold=0 luxury=0 science=0"
    if row.startswith("city_specialist ") \
            and " counts_toward_population=" not in row:
        row = row.replace(
            " can_use=", " counts_toward_population=1 can_use=", 1,
        )
    if row.startswith("unit ") and " scope=own " in row \
            and " veteran=" not in row:
        row = re.sub(
            r" hp=([0-9]+) moves=",
            " hp=\\1 veteran=0 veteran_name=Regular veteran_levels=1 "
            "veteran_power=100 veteran_move_bonus=0 fuel=0 max_hp=100 "
            "max_fuel=0 move_rate=3 attack=1 defense=1 firepower=1 "
            "base_upkeep_food=0 base_upkeep_shield=0 "
            "base_upkeep_trade=0 base_upkeep_gold=0 "
            "base_upkeep_luxury=0 base_upkeep_science=0 upkeep_food=0 "
            "upkeep_shield=0 upkeep_trade=0 upkeep_gold=0 "
            "upkeep_luxury=0 upkeep_science=0 moves=",
            row,
            count=1,
        )
    if row.startswith("unit ") and " scope=visible " in row \
            and " veteran=" not in row:
        row += (
            " veteran=0 veteran_name=Regular veteran_levels=1 "
            "veteran_power=100 veteran_move_bonus=0 max_hp=100 max_fuel=0 "
            "move_rate=3 attack=1 defense=1 firepower=1 "
            "base_upkeep_food=0 base_upkeep_shield=0 "
            "base_upkeep_trade=0 base_upkeep_gold=0 "
            "base_upkeep_luxury=0 base_upkeep_science=0"
        )
    if row.startswith("unit ") and " scope=own " in row \
            and " orders_repeat=" not in row:
        row += (
            " orders_repeat=0 orders_vigilant=0 order_count=0 "
            "orders_digest=fnv1a64-0000000000000000 "
            "orders_destination=-1"
        )
    if row.startswith("diplomacy ") and " intel_level=" not in row:
        row = row.replace(
            " has_embassy=",
            " intel_level=contact team=2 team_name=Team%202 same_team=0 "
            "controller=human connected=1 score=17 gold=23 "
            "government=Despotism has_embassy=",
            1,
        )
    if row.startswith("action ") and " route_waypoint_limit=" not in row:
        row = row.replace(
            " target_build_kind=",
            " route_waypoint_limit=0 infrastructure_cost=0 "
            "infrastructure_turns=0 infrastructure_choice_count=0 "
            "infrastructure_choices=- target_build_kind=",
            1,
        )
    return row


def complete_v2_rows(rows: tuple[str, ...]) -> tuple[str, ...]:
    """Complete rows and make parent citizen counts match their children."""
    completed = [complete_v2_row(row) for row in rows]
    specialists: dict[str, int] = {}
    workers: dict[str, int] = {}
    for row in completed:
        city_match = re.search(r"(?:^| )city=([^ ]+)(?: |$)", row)
        if city_match is None:
            continue
        city_ref = city_match.group(1)
        if row.startswith("city_specialist "):
            if " counts_toward_population=0 " in f" {row} ":
                continue
            count = re.search(r"(?:^| )count=([0-9]+)(?: |$)", row)
            assert count is not None
            specialists[city_ref] = specialists.get(city_ref, 0) + int(
                count.group(1)
            )
        elif row.startswith("city_tile ") \
                and " worked=1 " in f" {row} " \
                and " free_worked=0 " in f" {row} ":
            workers[city_ref] = workers.get(city_ref, 0) + 1
    for index, row in enumerate(completed):
        if not row.startswith("city "):
            continue
        ref = re.search(r"^city ref=([^ ]+) ", row)
        assert ref is not None
        city_ref = ref.group(1)
        size = int(re.search(r"(?:^| )size=([0-9]+)(?: |$)", row).group(1))
        worker_count = workers.get(city_ref, 0)
        specialist_count = specialists.get(city_ref, size - worker_count)
        completed[index] = re.sub(
            r" citizen_workers=[0-9]+ citizen_specialists=[0-9]+ ",
            f" citizen_workers={worker_count} "
            f"citizen_specialists={specialist_count} ",
            row,
            count=1,
        )
    return tuple(completed)


def observation(
    rows: tuple[str, ...] | None = None,
    *,
    generation: int = 1,
    revision: int = 11,
) -> dict[str, object]:
    return {
        "generation": generation,
        "native_revision": revision,
        "rows": complete_v2_rows(valid_rows() if rows is None else rows),
    }


def compact_bundle(
    control: V2SeatControl,
    rows: tuple[str, ...],
    *,
    revision: int = 11,
) -> dict[str, object]:
    """Materialize the entity catalogs around a compact native OBS fixture."""
    rows = complete_v2_rows(rows)
    section_prefixes = {
        "cities": ("city ", "city_rally ", "city_worker_task "),
        "units": ("unit ",),
        "city_sites": ("city_site ",),
    }
    scoped_prefixes = (
        "city_tile ", "city_specialist ", "city_worklist ",
        "city_build_choice ", "city_improvement ",
    )
    removed_prefixes = tuple(
        prefix
        for prefixes in section_prefixes.values()
        for prefix in prefixes
    ) + scoped_prefixes + ("diplomacy_clause ", "tombstone ")
    compact = observation(
        tuple(row for row in rows if not row.startswith(removed_prefixes)),
        revision=revision,
    )
    catalogs = {}
    for ordinal, request in enumerate(
        control.prepare_observation_scopes(compact), start=1,
    ):
        selected = tuple(
            row for row in rows
            if row.startswith(section_prefixes[request.section])
        )
        catalogs[request.section] = {
            "generation": 1,
            "native_revision": revision,
            "section": request.section,
            "selector": "-",
            "view_id": f"q{revision}-{ordinal}",
            "offset": 0,
            "count": len(selected),
            "total_count": len(selected),
            "next_offset": len(selected),
            "complete": True,
            "overflow": False,
            "rows": selected,
        }
    return dict(control.materialize_observation_catalogs(compact, catalogs))


def state_scope_catalog(request, rows, *, generation: int = 1):
    selected = tuple(complete_v2_row(row) for row in rows)
    if request.section in {"tile_window", "target_tiles"}:
        selected = tuple(
            (
                "tile_local " + row.removeprefix("tile ")
                + (
                    " resource_extra=-1 resource_name=none has_label=0 "
                    "label=none food=-1 shields=-1 trade=-1"
                    if " known=0 " in f" {row} " else
                    " resource_extra=-1 resource_name=none has_label=0 "
                    "label=none food=0 shields=0 trade=0"
                )
                if row.startswith("tile ") else row
            )
            for row in selected
        )
    return {
        "generation": generation,
        "native_revision": request.native_revision,
        "section": request.section,
        "selector": request.selector,
        "view_id": f"q{request.native_revision}-99",
        "offset": 0,
        "count": len(selected),
        "total_count": len(selected),
        "next_offset": len(selected),
        "complete": True,
        "overflow": False,
        "rows": selected,
    }


def actor_scope_catalog(request, rows, *, generation: int = 1):
    selected = tuple(sorted(complete_v2_row(row) for row in rows))
    return {
        "generation": generation,
        "native_revision": request.native_revision,
        "actor_ref": request.native_actor_ref,
        "view_id": f"v{request.native_revision}-99",
        "offset": 0,
        "count": len(selected),
        "total_count": len(selected),
        "next_offset": len(selected),
        "complete": True,
        "overflow": False,
        "rows": selected,
    }


def replace_row(
    rows: tuple[str, ...], old: str, new: str, *, sort: bool = True,
) -> tuple[str, ...]:
    changed = [row.replace(old, new) for row in rows]
    return tuple(sorted(changed)) if sort else tuple(changed)


def treaty_rows(
    clauses: tuple[tuple[str, int, str, str, int, str], ...],
    *,
    generation: int = 3,
    self_accepted: int = 0,
    other_accepted: int = 0,
) -> tuple[str, ...]:
    """Return a valid non-empty native treaty fixture.

    Clause tuples are ``(giver_ref, native_type, type, value_kind, value,
    encoded_name)`` and must already be in the sidecar's canonical order.
    """
    rows = [row for row in valid_rows() if not row.startswith("diplomacy ")]
    digest = v2_control._diplomacy_clauses_digest([
        {
            "giver_ref": giver,
            "native_type": native_type,
            "native_value": value,
        }
        for giver, native_type, _type, _kind, value, _name in clauses
    ])
    rows.append(
        "diplomacy other=p:2:20 name=Other nation=Romans state=Peace "
        "contact=5 alive=1 turns_left=0 can_meet=0 meeting=1 "
        f"generation={generation} self_accepted={self_accepted} "
        f"other_accepted={other_accepted} clause_count={len(clauses)} "
        f"clauses_digest={digest} has_embassy=0 other_has_embassy=0 "
        "gives_vision=0 receives_vision=0 gives_shared_tiles=0 "
        "receives_shared_tiles=0 can_cancel=1 cancel_reason=allowed"
    )
    for position, (giver, _native_type, clause_type, value_kind,
                   value, name) in enumerate(clauses):
        rows.append(
            f"diplomacy_clause other=p:2:20 generation={generation} "
            f"position={position} giver={giver} type={clause_type} "
            f"value_kind={value_kind} value={value} name={name}"
        )
    return tuple(sorted(rows))


def relation_action(
    slot: int,
    rule: str,
    result: str,
    target_name: str,
    *,
    clauses_digest: str,
    clause_giver: str = "none",
    clause_type: str = "none",
    clause_value: int = -1,
    clause_name: str = "none",
    desired_acceptance: int = -1,
    args: str = "none",
) -> str:
    return _action(
        slot, rule if rule != "diplomacy.propose_gold"
        else "diplomacy.propose_clause",
        "p:1:10", -1, rule, "Diplomatic Relation", result, 0, args,
        target_name=target_name, counterpart="p:2:20",
        meeting_generation=3, clauses_digest=clauses_digest,
        relation_state="Peace", clause_giver=clause_giver,
        clause_type=clause_type, clause_value=clause_value,
        clause_name=clause_name, desired_acceptance=desired_acceptance,
    )


class V2NativeSchemaTests(unittest.TestCase):
    def setUp(self) -> None:
        self.control = V2SeatControl("game_test", "agent_test", 1)

    def test_unit_telemetry_is_exact_for_owned_and_fog_safe_for_visible(self):
        units = self.control.state_page(
            observation(), "units",
        )["page"]["items"]
        owned = next(item for item in units if item["scope"] == "own")
        visible = next(item for item in units if item["scope"] == "visible")

        self.assertEqual(owned["veterancy"], {
            "level": 0, "name": "Regular", "levels": 1,
            "power_factor_percent": 100, "move_bonus": 0,
        })
        self.assertEqual(owned["fuel"], 0)
        self.assertEqual(owned["upkeep"], {
            "food": 0, "shield": 0, "trade": 0, "gold": 0,
            "luxury": 0, "science": 0,
        })
        self.assertEqual(owned["type_stats"]["max_hp"], 100)
        self.assertEqual(visible["veterancy"]["power_factor_percent"], 100)
        self.assertEqual(visible["type_stats"]["attack"], 1)
        for private_field in (
            "fuel", "moves", "upkeep", "home_city_id", "activity",
            "automation", "route", "transport",
        ):
            with self.subTest(private_field=private_field):
                self.assertNotIn(private_field, visible)

        zero_move_rows = replace_row(
            complete_v2_rows(valid_rows()), "move_rate=3", "move_rate=0",
        )
        zero_units = V2SeatControl(
            "game_zero_move", "agent_zero_move", 1,
        ).state_page(
            observation(zero_move_rows, revision=12), "units",
        )["page"]["items"]
        self.assertEqual(
            {item["type_stats"]["move_rate"] for item in zero_units}, {0},
        )

    def test_research_graph_projects_paths_edges_unlocks_and_goal_step(self):
        overview = self.control.state_page(
            observation(), "overview",
        )["page"]["items"][0]
        research = self.control.state_page(
            observation(), "research",
        )["page"]["items"]
        by_name = {item["name"]: item for item in research}

        self.assertEqual(
            overview["research"]["next_goal_step_id"],
            by_name["Writing"]["id"],
        )
        self.assertEqual(by_name["Pottery"]["next_step_id"],
                         by_name["Writing"]["id"])
        self.assertEqual(by_name["Pottery"]["unknown_prerequisite_count"], 1)
        self.assertEqual(by_name["Pottery"]["path_cost"], 40)
        self.assertEqual(by_name["Pottery"]["prerequisites"], [{
            "id": by_name["Writing"]["id"], "kind": "direct",
        }])
        self.assertEqual(by_name["Alphabet"]["prerequisites"], [{
            "id": by_name["Alphabet"]["id"], "kind": "root",
        }])
        self.assertEqual(by_name["Writing"]["unlocks"][0]["kind"], "unit")
        self.assertEqual(by_name["Writing"]["unlocks"][0]["name"], "Settlers")
        self.assertTrue(
            by_name["Writing"]["unlocks"][0]["id"].startswith("unit_type_")
        )

    def test_diplomacy_intel_tiers_enforce_contact_and_embassy_privacy(self):
        contact = self.control.state_page(
            observation(), "diplomacy",
        )["page"]["items"][0]
        self.assertEqual(contact["intel_level"], "contact")
        self.assertEqual(contact["score"], 17)
        self.assertEqual(contact["gold"], 23)
        self.assertEqual(contact["government"], "Despotism")
        self.assertIsNone(contact["rates"])
        self.assertIsNone(contact["known_techs"])

        embassy_rows = list(complete_v2_rows(valid_rows()))
        embassy_rows = list(replace_row(
            tuple(embassy_rows), "intel_level=contact", "intel_level=embassy",
        ))
        known_digest = v2_control._known_techs_digest((3, 4))
        embassy_rows.append(
            "diplomacy_intel other=p:2:20 tax=20 science=70 luxury=10 "
            "culture=31 research_id=5 research_name=Pottery bulbs=7 cost=40 "
            f"known_count=2 known_digest={known_digest} known_ids=3,4"
        )
        embassy = V2SeatControl("game_test", "embassy_agent", 1).state_page(
            observation(tuple(sorted(embassy_rows))), "diplomacy",
        )["page"]["items"][0]
        self.assertEqual(embassy["intel_level"], "embassy")
        self.assertEqual(embassy["rates"], {
            "tax": 20, "science": 70, "luxury": 10,
        })
        self.assertEqual(embassy["culture"], 31)
        self.assertEqual(embassy["research"]["name"], "Pottery")
        self.assertEqual(
            [item["name"] for item in embassy["known_techs"]],
            ["Alphabet", "Writing"],
        )
        self.assertTrue(embassy["known_techs_digest"].startswith("techset_"))

        missing_intel = replace_row(
            complete_v2_rows(valid_rows()),
            "intel_level=contact", "intel_level=embassy",
        )
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            V2SeatControl("game_test", "missing_intel", 1).state_page(
                observation(missing_intel), "diplomacy",
            )
        leaked_none = replace_row(
            complete_v2_rows(valid_rows()),
            "intel_level=contact", "intel_level=none",
        )
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            V2SeatControl("game_test", "leaked_none", 1).state_page(
                observation(leaked_none), "diplomacy",
            )

    def test_telemetry_rows_are_revision_and_digest_dependencies(self):
        first = self.control.state_page(observation(), "units")
        changed_rows = replace_row(
            valid_rows(), "x=2 y=2 hp=8", "x=2 y=2 hp=7",
        )
        second = self.control.state_page(
            observation(changed_rows, revision=12), "units",
        )
        self.assertNotEqual(
            first["state_revision"]["state_token"],
            second["state_revision"]["state_token"],
        )
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            self.control.state_page(
                observation(changed_rows, revision=11), "units",
            )

    @staticmethod
    def _c_macro_string(header: str, name: str) -> str:
        lines = header.splitlines()
        prefix = f"#define {name}"
        for index, line in enumerate(lines):
            if not line.startswith(prefix):
                continue
            parts = [line[len(prefix):]]
            while line.rstrip().endswith("\\"):
                index += 1
                line = lines[index]
                parts.append(line)
            tokens = re.findall(
                r'"(?:\\.|[^"\\])*"', "\n".join(parts),
            )
            return "".join(ast.literal_eval(token) for token in tokens)
        raise AssertionError(f"missing C schema macro {name}")

    def test_native_action_slice_counts_are_pinned(self):
        native_kinds = {
            rule.native_kind for rule in v2_control._ACTION_RULES.values()
        }
        global_kinds = {
            "phase.end", "unit.move", "unit.attack", "city.found",
            "research.set_target", "research.set_goal", "economy.set_rates",
            "player.send_chat", "player.cast_vote",
            "pregame.configure", "pregame.set_ready", "pregame.set_team",
        }
        self.assertEqual(len(native_kinds), 74)
        self.assertTrue(global_kinds < native_kinds)
        self.assertEqual(len(native_kinds - global_kinds), 62)

    def test_multiplier_state_and_scoped_action_are_opaque_and_exact(self):
        rows = tuple(
            row.replace(
                "can_change=0 choice_count=11",
                "can_change=1 choice_count=11",
            ) if row.startswith("multiplier ") else row
            for row in valid_rows()
        )
        control = V2SeatControl("game_multiplier", "agent_multiplier", 1)
        snapshot = control._snapshot(observation(rows))
        multiplier = snapshot.sections["multipliers"][0]
        self.assertNotEqual(multiplier["id"], "0")
        self.assertEqual(multiplier["target"], 50)
        self.assertTrue(multiplier["can_change"])
        actor_id = next(
            actor_id for actor_id, binding in snapshot.actor_bindings.items()
            if binding.kind == "player"
        )
        request = v2_control.V2ActorScopeRequest(
            actor_id=actor_id, actor_kind="player",
            native_actor_ref="p:1:10",
            native_revision=snapshot.native_revision, limit=16,
        )
        row = _action(
            300, "player.set_multiplier", "p:1:10", -1,
            "player.set_multiplier", "Multiplier",
            "Multiplier Target Changed", 0,
            target_multiplier=0, multiplier_value=60,
            target_name="Policy",
        )
        action = control._parse_row(
            "action", dict(token.split("=", 1) for token in row.split()[1:]),
        )
        descriptor, binding = control._project_scoped_action(
            snapshot, request, action, "action_multiplier",
        )
        self.assertEqual(descriptor["kind"], "player.set_multiplier")
        self.assertEqual(descriptor["subject"]["target"]["value"], 60)
        self.assertEqual(binding.operation, "set_multiplier")

    def test_city_governor_state_and_scoped_actions_are_exact(self):
        rows = [
            row.replace("governor_enabled=0", "governor_enabled=1")
            if row.startswith("city ref=c:20:200 ") else row
            for row in valid_rows()
        ]
        governor_row = (
            "city_governor city=c:20:200 min_food=0 min_production=0 "
            "min_trade=0 min_gold=0 min_luxury=0 min_science=0 "
            "weight_food=1 weight_production=1 weight_trade=1 "
            "weight_gold=1 weight_luxury=1 weight_science=1 "
            "celebration_weight=1 require_happy=0 maximize_growth=0"
        )
        control = V2SeatControl("game_governor", "agent_governor", 1)
        current = observation(tuple(sorted(rows)))
        snapshot = control._snapshot(current)
        actor_id = next(
            actor_id for actor_id, binding in snapshot.actor_bindings.items()
            if binding.kind == "city"
        )
        state_request = control.prepare_state_scope(
            current, "city_governor", actor_id=actor_id,
        )
        governor_page = control.materialize_state_scope(state_request, {
            "generation": 1,
            "native_revision": snapshot.native_revision,
            "section": "city_governor",
            "selector": "c:20:200",
            "view_id": f"q{snapshot.native_revision}-1",
            "offset": 0,
            "count": 1,
            "total_count": 1,
            "next_offset": 1,
            "complete": True,
            "overflow": False,
            "rows": (governor_row,),
        })
        self.assertEqual(
            governor_page["page"]["items"][0]["weights"]["production"], 1,
        )
        request = v2_control.V2ActorScopeRequest(
            actor_id=actor_id, actor_kind="city",
            native_actor_ref="c:20:200",
            native_revision=snapshot.native_revision, limit=16,
        )
        clear_row = _action(
            301, "city.clear_governor", "c:20:200", -1,
            "city.clear_governor", "City", "Governor Cleared", 0,
            target_name="governor",
        )
        clear = control._parse_row(
            "action",
            dict(token.split("=", 1) for token in clear_row.split()[1:]),
        )
        descriptor, clear_binding = control._project_scoped_action(
            snapshot, request, clear, "action_clear_governor",
        )
        self.assertEqual(descriptor["kind"], "city.set_governor")
        self.assertEqual(descriptor["subject"]["operation"], "clear_governor")
        self.assertEqual(
            control._resolve_arguments(snapshot, clear_binding, {}), "-",
        )

        set_row = _action(
            302, "city.set_governor", "c:20:200", -1,
            "city.set_governor", "City", "Governor Goal Set", 0,
            "governor-goal-required", target_name="governor",
        )
        set_action = control._parse_row(
            "action",
            dict(token.split("=", 1) for token in set_row.split()[1:]),
        )
        _, set_binding = control._project_scoped_action(
            snapshot, request, set_action, "action_set_governor",
        )
        goal = {
            "minimum_surplus": {
                name: 0 for name in (
                    "food", "production", "trade", "gold", "luxury",
                    "science",
                )
            },
            "weights": {
                name: 1 for name in (
                    "food", "production", "trade", "gold", "luxury",
                    "science",
                )
            },
            "celebration_weight": 1,
            "require_happy": False,
            "maximize_growth": False,
        }
        with self.assertRaisesRegex(V2ControlError, "invalid_request"):
            control._resolve_arguments(snapshot, set_binding, goal)
        goal["weights"]["science"] = 2
        self.assertEqual(
            control._resolve_arguments(snapshot, set_binding, goal),
            "min_food=0,min_production=0,min_trade=0,min_gold=0,"
            "min_luxury=0,min_science=0,weight_food=1,"
            "weight_production=1,weight_trade=1,weight_gold=1,"
            "weight_luxury=1,weight_science=2,celebration_weight=1,"
            "require_happy=0,maximize_growth=0",
        )

    def test_native_schema_id_is_deterministic_and_matches_c_literal(self):
        schema_id = v2_control.NATIVE_OBSERVATION_ACTION_SCHEMA_ID
        self.assertRegex(schema_id, r"^sha256-[0-9a-f]{64}$")
        self.assertEqual(schema_id, v2_control._derive_native_schema_id())
        repository = Path(v2_control.__file__).resolve().parent.parent
        header = (
            repository / "client" / "gui-agent" / "protocol_v2_codec.h"
        ).read_text(encoding="utf-8")
        self.assertEqual(header.count(f'"{schema_id}"'), 1)
        documentation = (
            repository / "docs" / "full-control-v2.md"
        ).read_text(encoding="utf-8")
        self.assertEqual(documentation.count(f"`{schema_id}`"), 1)
        frame_macros = {
            "FC_AGENT_V2_FRAME_SCOPE_OPEN": "SCOPE_OPEN",
            "FC_AGENT_V2_FRAME_SCOPE_OPENED": "SCOPE_OPENED",
            "FC_AGENT_V2_FRAME_SCOPE_PAGE": "SCOPE_PAGE",
            "FC_AGENT_V2_FRAME_SCOPE_BEGIN": "SCOPE_BEGIN",
            "FC_AGENT_V2_FRAME_SCOPE_ACTION": "SCOPE_ACTION",
            "FC_AGENT_V2_FRAME_SCOPE_END": "SCOPE_END",
            "FC_AGENT_V2_FRAME_STATE_SCOPE_OPEN": "STATE_SCOPE_OPEN",
            "FC_AGENT_V2_FRAME_STATE_SCOPE_OPENED": "STATE_SCOPE_OPENED",
            "FC_AGENT_V2_FRAME_STATE_SCOPE_PAGE": "STATE_SCOPE_PAGE",
            "FC_AGENT_V2_FRAME_STATE_SCOPE_BEGIN": "STATE_SCOPE_BEGIN",
            "FC_AGENT_V2_FRAME_STATE_SCOPE_ROW": "STATE_SCOPE_ROW",
            "FC_AGENT_V2_FRAME_STATE_SCOPE_END": "STATE_SCOPE_END",
            "FC_AGENT_V2_FRAME_ACT_CAP": "ACT_CAP",
            "FC_AGENT_V2_FRAME_ACT_RELATION_CAP": "ACT_RELATION_CAP",
            "FC_AGENT_V2_FRAME_RELATION_SCOPE_OPEN": "RELATION_SCOPE_OPEN",
            "FC_AGENT_V2_FRAME_RELATION_SCOPE_OPENED": (
                "RELATION_SCOPE_OPENED"
            ),
            "FC_AGENT_V2_FRAME_RELATION_SCOPE_PAGE": "RELATION_SCOPE_PAGE",
            "FC_AGENT_V2_FRAME_RELATION_SCOPE_BEGIN": (
                "RELATION_SCOPE_BEGIN"
            ),
            "FC_AGENT_V2_FRAME_RELATION_SCOPE_ACTION": (
                "RELATION_SCOPE_ACTION"
            ),
            "FC_AGENT_V2_FRAME_RELATION_SCOPE_END": "RELATION_SCOPE_END",
        }
        for macro, frame in frame_macros.items():
            self.assertEqual(
                self._c_macro_string(header, macro),
                v2_control._PRIVATE_FRAME_CONTRACTS[frame],
            )
        limit_macros = {
            "FC_AGENT_V2_MAX_ACTIONS": v2_control.MAX_SCOPED_ACTIONS,
            "FC_AGENT_V2_MAX_PINNED_SCOPES": (
                v2_control.MAX_PINNED_SCOPE_VIEWS
            ),
            "FC_AGENT_V2_MAX_RELATION_ACTIONS": (
                v2_control.MAX_RELATION_SCOPED_ACTIONS
            ),
            "FC_AGENT_V2_MAX_TARGET_ACTIONS": 256,
            "FC_AGENT_V2_MAX_PINNED_RELATION_SCOPES": (
                v2_control.MAX_PINNED_RELATION_SCOPE_VIEWS
            ),
            "FC_AGENT_V2_MAX_PINNED_STATE_SCOPES": 4,
            "FC_AGENT_V2_PAGE_MAX": v2_control.MAX_PAGE_ITEMS,
            "FC_AGENT_V2_MAX_GOVERNMENTS": v2_control.MAX_GOVERNMENTS,
            "FC_AGENT_V2_MAX_CITY_BUILD_CHOICES": (
                v2_control.MAX_CITY_BUILD_CHOICES
            ),
            "FC_AGENT_V2_MAX_CITY_WORKLIST": v2_control.MAX_CITY_WORKLIST,
            "FC_AGENT_V2_MAX_RALLY_ORDERS": v2_control.MAX_RALLY_ORDERS,
            "FC_AGENT_V2_MAX_UNIT_ROUTE_WAYPOINTS": (
                v2_control.MAX_UNIT_ROUTE_WAYPOINTS
            ),
            "FC_AGENT_V2_MAX_INFRA_CHOICES": (
                v2_control.MAX_INFRASTRUCTURE_CHOICES
            ),
            "FC_AGENT_V2_MAX_VOTES": v2_control.MAX_VOTES,
        }
        for macro, expected in limit_macros.items():
            match = re.search(
                rf"^#define {macro} ([0-9]+)$", header, re.MULTILINE,
            )
            self.assertIsNotNone(match, macro)
            self.assertEqual(int(match.group(1)), expected)
        protocol = (
            repository / "client" / "gui-agent" / "protocol_v2.c"
        ).read_text(encoding="utf-8")
        for local, public in (
            ("AGENT_V2_MAX_ACTIONS", "FC_AGENT_V2_MAX_ACTIONS"),
            ("AGENT_V2_SCOPE_PINNED", "FC_AGENT_V2_MAX_PINNED_SCOPES"),
            ("AGENT_V2_PAGE_MAX", "FC_AGENT_V2_PAGE_MAX"),
        ):
            self.assertIn(f"#define {local} {public}", protocol)

    def test_native_schema_fingerprint_includes_map_topologies(self):
        baseline = v2_control._derive_native_schema_id()
        with mock.patch.object(
            v2_control, "_MAP_TOPOLOGIES",
            frozenset((*v2_control._MAP_TOPOLOGIES, "future_topology")),
        ):
            self.assertNotEqual(v2_control._derive_native_schema_id(), baseline)

    def test_native_schema_fingerprint_includes_telemetry_domains(self):
        baseline = v2_control._derive_native_schema_id()
        with mock.patch.object(
            v2_control,
            "_EXTRA_CAUSE_TAGS",
            tuple(reversed(v2_control._EXTRA_CAUSE_TAGS)),
        ):
            self.assertNotEqual(v2_control._derive_native_schema_id(), baseline)
        with mock.patch.object(
            v2_control, "_FC_INFINITY", v2_control._FC_INFINITY + 1,
        ):
            self.assertNotEqual(v2_control._derive_native_schema_id(), baseline)

    def test_native_schema_fingerprint_includes_special_action_contract(self):
        baseline = v2_control._derive_native_schema_id()
        changed = dict(v2_control._SPECIAL_ACTION_RESULTS)
        key = ("Unit Nuke", "Tile")
        changed[key] = v2_control._SpecialActionRule(
            "future_nuke", "Tile", "Launch nuclear attack on tile",
        )
        with mock.patch.object(
            v2_control, "_SPECIAL_ACTION_RESULTS", MappingProxyType(changed),
        ):
            self.assertNotEqual(v2_control._derive_native_schema_id(), baseline)

    def test_native_hut_extras_and_paradrop_conquer_bind_exact_variants(self):
        protocol = (
            Path(v2_control.__file__).resolve().parent.parent
            / "client" / "gui-agent" / "protocol_v2.c"
        ).read_text(encoding="utf-8")
        for action in (
            "ACTION_CONQUER_EXTRAS", "ACTION_CONQUER_EXTRAS2",
            "ACTION_HUT_ENTER", "ACTION_HUT_ENTER2",
            "ACTION_HUT_FRIGHTEN", "ACTION_HUT_FRIGHTEN2",
            "ACTION_PARADROP_ENTER_CONQUER",
        ):
            self.assertIn(action, protocol)
        self.assertIn("allowed_subresult = ACT_SUB_RES_HUT_ENTER;", protocol)
        self.assertIn("special_target_known_seen", protocol)
        self.assertIn("source_unit_moves", protocol)
        self.assertIn(
            "fc_agent_v2_paradrop_enter_conquer_postcondition(", protocol,
        )
        self.assertIn("*subtarget_id = NO_TARGET;", protocol)
        self.assertIn("int special_subtarget = NO_TARGET;", protocol)

    def test_native_recover_disband_uses_correlated_event_and_exact_delta(self):
        repository = Path(v2_control.__file__).resolve().parent.parent
        protocol = (
            repository / "client" / "gui-agent" / "protocol_v2.c"
        ).read_text(encoding="utf-8")
        codec = (
            repository / "client" / "gui-agent" / "protocol_v2_codec.c"
        ).read_text(encoding="utf-8")
        observer = protocol[
            protocol.rindex("static void v2_chat_msg_observer("):
            protocol.rindex("static void v2_nuke_tile_info_observer(")
        ]
        for contract in (
            "ACTION_DISBAND_UNIT_RECOVER",
            "ACTRES_DISBAND_UNIT_RECOVER",
            "AGENT_V2_ACTION_UNIT_DISBAND_RECOVER",
            "E_CARAVAN_ACTION",
            "caravan_action_event_latched",
            "unit_shield_value",
            "fc_agent_v2_disband_recover_postcondition",
        ):
            self.assertIn(contract, protocol)
        self.assertIn("request_id != v2_pending.request_id", observer)
        self.assertIn("packet->event == E_CARAVAN_ACTION", observer)
        self.assertIn(
            "packet->tile == v2_pending.action.destination_city_tile",
            observer,
        )
        self.assertIn("fc_agent_v2_consumed_city_postcondition", codec)
        self.assertIn(
            "current_shields == before_shields + shields_added", codec,
        )

    def test_native_paid_espionage_receipts_and_phase_revision_are_guarded(self):
        repository = Path(v2_control.__file__).resolve().parent.parent
        protocol = (
            repository / "client" / "gui-agent" / "protocol_v2.c"
        ).read_text(encoding="utf-8")
        codec = (
            repository / "client" / "gui-agent" / "protocol_v2_codec.c"
        ).read_text(encoding="utf-8")
        server = (repository / "server" / "unithand.c").read_text(
            encoding="utf-8",
        )
        for contract in (
            "ACTION_SPY_BRIBE_UNIT", "ACTION_SPY_BRIBE_STACK",
            "ACTRES_SPY_BRIBE_STACK", "ACTION_SPY_INCITE_CITY",
            "ACTION_SPY_INCITE_CITY_ESC", "agent-v2-max-cost:%d",
            "E_MY_DIPLOMAT_BRIBE", "E_MY_DIPLOMAT_INCITE",
            "paid_replacement_conflict", "paid_failure_event_latched",
            "action_success_receipt_latched",
            "bribe_visible_mapping_corroborated",
            "v2_visible_bribe_mapping_matches",
            "v2_unit_action_answer_observer", "v2_chat_msg_observer",
        ):
            self.assertIn(contract, protocol)
        paid_terminal = protocol[
            protocol.index("if (v2_pending.action.kind == "
                           "AGENT_V2_ACTION_UNIT_SPECIAL) {"):
            protocol.index("if (v2_pending.action.kind == "
                           "AGENT_V2_ACTION_UNIT_CANCEL_ORDERS")
        ]
        failure_branch = paid_terminal[
            paid_terminal.index("paid_failure_event_latched"):
        ]
        self.assertIn("FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH",
                      failure_branch)
        self.assertNotIn("FC_AGENT_V2_TERMINAL_POSTCONDITION_NOT_MET",
                         failure_branch)
        self.assertIn("agent-v2-max-cost:", server)
        self.assertIn("max_cost_guarded", server)
        self.assertIn("fc_agent_v2_phase_revision_changed", protocol)
        self.assertIn("Phase evidence is part of native revision identity",
                      codec)
        self.assertNotIn(
            "|| !v2_phase_evidence_equal(&notice->evidence, evidence)",
            codec[codec.index("bool fc_agent_v2_phase_notice_needed("):],
        )

        postcondition = protocol.index(
            "static bool v2_action_postcondition(void)\n{"
        )
        bribe_stack = protocol[
            protocol.index("case ACTRES_SPY_BRIBE_STACK:",
                           postcondition):
            protocol.index("case ACTRES_SPY_BRIBE_UNIT:",
                           postcondition)
        ]
        self.assertIn("action_success_receipt_latched", bribe_stack)
        self.assertIn("before_special_target_exact", bribe_stack)
        self.assertIn("before_unit_lifecycle_id", bribe_stack)
        self.assertIn("bribe_visible_mapping_corroborated", bribe_stack)
        self.assertIn("!v2_pending.bribe_visible_mapping_conflict",
                      bribe_stack)
        self.assertNotIn("paid_success_event_latched", bribe_stack)

    def test_native_classic_combat_receipts_are_exact_and_request_bound(self):
        repository = Path(v2_control.__file__).resolve().parent.parent
        protocol = (
            repository / "client" / "gui-agent" / "protocol_v2.c"
        ).read_text(encoding="utf-8")

        classic = protocol[
            protocol.index("static bool v2_classic_immediate_combat_action("):
            protocol.index("static bool v2_paid_quote_accepted(")
        ]
        for action in (
            "ACTION_ATTACK", "ACTION_SUICIDE_ATTACK",
            "ACTION_COLLECT_RANSOM",
        ):
            self.assertIn(action, classic)
        self.assertNotIn("ACTION_ATTACK2", classic)
        self.assertNotIn("ACTION_SUICIDE_ATTACK2", classic)

        observer_start = protocol.index(
            "static void v2_unit_combat_info_observer(\n"
        )
        observer = protocol[
            observer_start:
            protocol.index("static void v2_chat_msg_observer(\n", observer_start)
        ]
        for binding in (
            "request_id, v2_pending.request_id",
            "packet->attacker_unit_id, v2_pending.action.unit_id",
            "packet->defender_unit_id, defender_on_expected_target",
            "v2_pending.combat_info_latched = TRUE",
        ):
            self.assertIn(binding, observer)

        attack_start = protocol.index("case AGENT_V2_ACTION_ATTACK: {")
        attack = protocol[
            attack_start:
            protocol.index("case AGENT_V2_ACTION_FOUND_CITY:", attack_start)
        ]
        self.assertIn("v2_pending.combat_info_latched", attack)
        self.assertIn("combat_transition", attack)

        special_start = protocol.index("case ACTRES_SPY_SABOTAGE_UNIT: {")
        special = protocol[
            special_start:protocol.index("case ACTRES_NUKE:", special_start)
        ]
        for contract in (
            "ACTION_SPY_SABOTAGE_UNIT_ESC",
            "before_target_unit_hp",
            "ACTION_SPY_ATTACK",
            "spy_attack_actor_loss_event_latched",
            "spy_attack_target_loss_event_latched",
        ):
            self.assertIn(contract, special)
        self.assertIn("case ACTRES_BOMBARD:", special)
        self.assertIn(
            "packhand_set_unit_combat_info_observer(\n"
            "    v2_unit_combat_info_observer, NULL);",
            protocol,
        )
        self.assertIn(
            "packhand_set_unit_combat_info_observer(NULL, NULL);", protocol,
        )

    def test_nonempty_treaty_digest_has_one_cross_language_byte_contract(self):
        clauses = [
            {"giver_ref": "p:1:10", "native_type": 0,
             "native_value": 4},
            {"giver_ref": "p:1:10", "native_type": 1,
             "native_value": 17},
            {"giver_ref": "p:2:20", "native_type": 10,
             "native_value": 0},
        ]
        self.assertEqual(
            v2_control._diplomacy_clauses_digest(clauses),
            "fnv1a64-a1110b42ac608d4e",
        )
        self.assertEqual(
            v2_control._diplomacy_clauses_digest(tuple(reversed(clauses))),
            "fnv1a64-a1110b42ac608d4e",
        )
        repository = Path(v2_control.__file__).resolve().parent.parent
        protocol = (
            repository / "client" / "gui-agent" / "protocol_v2.c"
        ).read_text(encoding="utf-8")
        digest = protocol[
            protocol.index("static bool v2_treaty_clause_keys("):
            protocol.index("static bool v2_add_diplomacy_clause_rows(")
        ]
        self.assertIn('"%d:%d:%d;"', digest)
        self.assertIn("UINT64_C(14695981039346656037)", digest)

    def test_native_action_stream_keeps_acceptance_and_result_contiguous(self):
        repository = Path(v2_control.__file__).resolve().parent.parent
        protocol = (
            repository / "client" / "gui-agent" / "protocol_v2.c"
        ).read_text(encoding="utf-8")
        codec = (
            repository / "client" / "gui-agent" / "protocol_v2_codec.c"
        ).read_text(encoding="utf-8")
        tick = protocol[
            protocol.index("void fc_agent_v2_tick(void)"):
            protocol.index("bool fc_agent_v2_handle(")
        ]
        progress = tick.index("v2_progress_pending();")
        guard = tick.index(
            "!fc_agent_v2_stream_notification_allowed(v2_pending.active)"
        )
        refresh = tick.index("v2_refresh()")
        notification = tick.index('v2_sendf("STATE_AVAILABLE')
        self.assertLess(progress, guard)
        self.assertLess(guard, refresh)
        self.assertLess(refresh, notification)
        self.assertIn("return !pending_active;", codec)

    def test_common_city_rally_replacement_is_transactional_and_canonical(self):
        repository = Path(v2_control.__file__).resolve().parent.parent
        city_source = (repository / "common" / "city.c").read_text(
            encoding="utf-8",
        )
        clear = city_source[
            city_source.index("void city_rally_point_clear("):
            city_source.index("void city_rally_point_receive(")
        ]
        receive = city_source[
            city_source.index("void city_rally_point_receive("):
        ]
        for required in (
            "free(pcity->rally_point.orders);",
            "pcity->rally_point.orders = nullptr;",
            "pcity->rally_point.length = 0;",
            "pcity->rally_point.persistent = FALSE;",
            "pcity->rally_point.vigilant = FALSE;",
        ):
            with self.subTest(required=required):
                self.assertIn(required, clear)
        validate = receive.index("checked_orders = create_unit_orders(")
        reject = receive.index("if (!checked_orders)")
        commit = receive.index("free(pcity->rally_point.orders);")
        self.assertLess(validate, reject)
        self.assertLess(reject, commit)
        self.assertLess(
            receive.index("MAX_LEN_ROUTE < packet->length"), validate,
        )

    def test_legacy_gui_source_tile_still_unsets_rally_without_v2_set(self):
        repository = Path(v2_control.__file__).resolve().parent.parent
        goto_source = (repository / "client" / "goto.c").read_text(
            encoding="utf-8",
        )
        materialize = goto_source[
            goto_source.index("client_rally_plan_new("):
            goto_source.index("void client_rally_plan_destroy(")
        ]
        legacy = goto_source[
            goto_source.index("bool send_rally_tile("):
            goto_source.index("bool send_attack_tile(")
        ]
        self.assertIn("ptile == city_tile(pcity)", materialize)
        self.assertIn("ptile == city_tile(pcity)", legacy)
        self.assertLess(
            legacy.index("ptile == city_tile(pcity)"),
            legacy.index("client_rally_plan_new(pcity, ptile)"),
        )
        self.assertIn("memset(&packet, 0, sizeof(packet));", legacy)
        self.assertIn(
            "send_packet_city_rally_point(&client.conn, &packet, FALSE);",
            legacy,
        )

    def test_c_emitters_consume_the_exact_python_row_grammar(self):
        repository = Path(v2_control.__file__).resolve().parent.parent
        header = (
            repository / "client" / "gui-agent" / "protocol_v2_codec.h"
        ).read_text(encoding="utf-8")
        protocol = (
            repository / "client" / "gui-agent" / "protocol_v2.c"
        ).read_text(encoding="utf-8")
        codec = (
            repository / "client" / "gui-agent" / "protocol_v2_codec.c"
        ).read_text(encoding="utf-8")
        macro_rows = {
            "FC_AGENT_V2_ROW_META": "meta",
            "FC_AGENT_V2_ROW_VOTE": "vote",
            "FC_AGENT_V2_ROW_PLAYER": "player",
            "FC_AGENT_V2_ROW_GOVERNANCE": "governance",
            "FC_AGENT_V2_ROW_GOVERNMENT": "government",
            "FC_AGENT_V2_ROW_RESEARCH": "research",
            "FC_AGENT_V2_ROW_RESEARCH_TECH": "research_tech",
            "FC_AGENT_V2_ROW_DIPLOMACY": "diplomacy",
            "FC_AGENT_V2_ROW_TILE": "tile",
            "FC_AGENT_V2_ROW_TILE_LOCAL": "tile_local",
            "FC_AGENT_V2_ROW_TILE_EXTRA": "tile_extra",
            "FC_AGENT_V2_ROW_CITY": "city",
            "FC_AGENT_V2_ROW_CITY_TILE": "city_tile",
            "FC_AGENT_V2_ROW_CITY_SPECIALIST": "city_specialist",
            "FC_AGENT_V2_ROW_CITY_WORKLIST": "city_worklist",
            "FC_AGENT_V2_ROW_CITY_BUILD_CHOICE": "city_build_choice",
            "FC_AGENT_V2_ROW_CITY_IMPROVEMENT": "city_improvement",
            "FC_AGENT_V2_ROW_INVESTIGATION": "investigation",
            "FC_AGENT_V2_ROW_INVESTIGATION_IMPROVEMENT": (
                "investigation_improvement"
            ),
            "FC_AGENT_V2_ROW_INVESTIGATION_CITIZENS": (
                "investigation_citizens"
            ),
            "FC_AGENT_V2_ROW_INVESTIGATION_SPECIALIST": (
                "investigation_specialist"
            ),
            "FC_AGENT_V2_ROW_CITY_RALLY": "city_rally",
            "FC_AGENT_V2_ROW_UNIT_OWN": "unit_own",
            "FC_AGENT_V2_ROW_UNIT_VISIBLE": "unit_visible",
            "FC_AGENT_V2_ROW_TOMBSTONE": "tombstone",
            "FC_AGENT_V2_ROW_CHAT": "chat",
            "FC_AGENT_V2_ROW_ACTION": "action",
        }
        emitters = protocol + codec
        for macro, schema_key in macro_rows.items():
            with self.subTest(macro=macro):
                template = self._c_macro_string(header, macro)
                self.assertEqual(
                    template, v2_control._ROW_FORMAT_CONTRACTS[schema_key],
                )
                tokens = template.split(" ")
                expected_kind = (
                    "unit" if schema_key.startswith("unit_") else schema_key
                )
                self.assertEqual(tokens[0], expected_kind)
                self.assertEqual(
                    tuple(token.split("=", 1)[0] for token in tokens[1:]),
                    v2_control._ROW_FIELDS[schema_key],
                )
                self.assertIn(macro, emitters)

        # All ordinary emitters use the canonical macros.  The sole generic
        # row forwards the unknown-tile string produced by the codec, whose
        # formatter itself consumes FC_AGENT_V2_ROW_TILE.
        self.assertEqual(
            re.findall(r'v2_add_row\(\s*"([^"]*)"', protocol), ["%s"],
        )
        self.assertEqual(protocol.count('v2_add_row("%s", row)'), 1)
        self.assertIn(
            "snprintf(buffer, buffer_size, FC_AGENT_V2_ROW_TILE", codec,
        )

    def test_native_vote_dispatch_and_receipt_are_narrow_and_bound(self):
        repository = Path(v2_control.__file__).resolve().parent.parent
        protocol = (
            repository / "client" / "gui-agent" / "protocol_v2.c"
        ).read_text(encoding="utf-8")
        self.assertEqual(
            protocol.count("voteinfo_do_vote(action->vote_no"), 1,
        )
        self.assertNotIn("packet_vote_submit", protocol)
        self.assertIn(
            "v2_vote_signature(vote) == action->vote_signature", protocol,
        )
        self.assertIn(
            "!v2_refresh() || v2_revision != selected_revision", protocol,
        )
        self.assertIn(
            "vote->client_vote == v2_pending.desired_client_vote", protocol,
        )
        self.assertIn(
            "FC_AGENT_V2_TERMINAL_PROCESSING_BOUNDARY_MISMATCH", protocol,
        )

    def test_native_city_and_worker_paths_revalidate_and_ack_exact_state(self):
        repository = Path(v2_control.__file__).resolve().parent.parent
        protocol = (
            repository / "client" / "gui-agent" / "protocol_v2.c"
        ).read_text(encoding="utf-8")
        for required in (
            "can_city_build_now(&wld.map, pcity, production, RPT_CERTAIN)",
            "city_change_production(city, &v2_pending.desired_production)",
            "city_buy_production(city)",
            "action_prob_vs_extras(",
            "request_do_action(action->action, action->unit_id,",
            "request_new_unit_activity(unit, ACTIVITY_IDLE)",
            "unit->activity == v2_pending.desired_activity",
            "unit->activity_target == extra",
            "punit->activity == activity",
            "canonicalized to activity+extra",
            "candidate_kind == AGENT_V2_PROBABILITY_NOT_IMPLEMENTED",
            "v2_hash_scoped_catalogs(hash, client_player(), &phase)",
            "actor capability catalog changed during paging",
        ):
            with self.subTest(required=required):
                self.assertIn(required, protocol)

    def test_native_city_citizen_paths_bind_lifetime_and_exact_state(self):
        repository = Path(v2_control.__file__).resolve().parent.parent
        protocol = (
            repository / "client" / "gui-agent" / "protocol_v2.c"
        ).read_text(encoding="utf-8")
        codec = (
            repository / "client" / "gui-agent" / "protocol_v2_codec.c"
        ).read_text(encoding="utf-8")
        packhand = (repository / "client" / "packhand.c").read_text(
            encoding="utf-8",
        )
        city_header = (repository / "common" / "city.h").read_text(
            encoding="utf-8",
        )
        self.assertEqual(
            packhand.count(
                "pcity->client.lifecycle_id = client_city_lifecycle_take();",
            ),
            4,
        )
        placeholder = packhand[
            packhand.index('/* New unseen ("invisible") city, or before city_info */'):
            packhand.index("} else if (NULL == city_tile(pwork))")
        ]
        self.assertNotIn("lifecycle_id", placeholder)
        for required in (
            "if (pcity->client.lifecycle_id == 0)",
            "if (next_client_city_lifecycle_id == UINT64_MAX)",
            "log_fatal(\"Client city lifecycle identity space exhausted.\");",
            "return next_client_city_lifecycle_id++;",
        ):
            with self.subTest(required=required):
                self.assertIn(required, packhand)
        self.assertIn("uint64_t lifecycle_id;", city_header)
        for required in (
            "entry->city_lifecycle_id = pcity->client.lifecycle_id;",
            "a->city_lifecycle_id == b->city_lifecycle_id",
            "fc_agent_v2_city_lifetime_matches(",
            "v2_pending.before_city_lifecycle_id =",
            "== v2_pending.action.city_lifecycle_id",
            "dsend_packet_city_make_worker(&client.conn, city->id,",
            "dsend_packet_city_make_specialist(&client.conn, city->id,",
            "dsend_packet_city_change_specialist(",
            "city_specialists(city)",
            "v2_pending.before_source_specialists - 1",
            "v2_pending.before_target_specialists + 1",
        ):
            with self.subTest(required=required):
                self.assertIn(required, protocol)
        for required in (
            "bool fc_agent_v2_city_lifetime_matches(",
            "tracked_lifecycle != 0",
            "current_lifecycle != 0",
        ):
            with self.subTest(required=required):
                self.assertIn(required, codec)

    def test_native_city_management_uses_packets_and_exact_lifetime_state(self):
        repository = Path(v2_control.__file__).resolve().parent.parent
        protocol = (
            repository / "client" / "gui-agent" / "protocol_v2.c"
        ).read_text(encoding="utf-8")
        for required in (
            "city_set_worklist(city, &v2_pending.desired_worklist);",
            "dsend_packet_city_options_req(",
            "city_rename(city, v2_pending.city_name);",
            "city_sell_improvement(",
            "v2_worklist_count(result, &target)",
            "v2_worklist_count(&pcity->worklist, &target)",
            "fc_agent_v2_worklist_append_allowed(",
            "worklist_append(result, &target)",
            "are_worklists_equal(&city->worklist,",
            "BV_ARE_EQUAL(city->city_options,",
            "strcmp(city_name_get(city), v2_pending.city_name) == 0",
            "!v2_pending.before_city_did_sell",
            "v2_pending.before_city_had_improvement",
            "!city_has_building(city, v2_pending.desired_improvement)",
            "== v2_pending.action.city_lifecycle_id",
            "== v2_pending.action.city_incarnation",
        ):
            with self.subTest(required=required):
                self.assertIn(required, protocol)
        self.assertNotIn("desired_sell_price", protocol)

    def test_native_unit_self_paths_are_scoped_revalidated_and_exact(self):
        repository = Path(v2_control.__file__).resolve().parent.parent
        protocol = (
            repository / "client" / "gui-agent" / "protocol_v2.c"
        ).read_text(encoding="utf-8")
        codec = (
            repository / "client" / "gui-agent" / "protocol_v2_codec.c"
        ).read_text(encoding="utf-8")
        packhand = (repository / "client" / "packhand.c").read_text(
            encoding="utf-8",
        )
        unit_header = (repository / "common" / "unit.h").read_text(
            encoding="utf-8",
        )
        self.assertEqual(protocol.count("v2_build_self_unit_actions("), 3)
        for required in (
            "action_by_result_iterate(paction, result)",
            "action_get_target_kind(paction) != ATK_SELF",
            "action_prob_self(&wld.map, punit, paction->id)",
            "candidate_kind == AGENT_V2_PROBABILITY_NOT_IMPLEMENTED",
            "punit->activity != ACTIVITY_CONVERT",
            "request_new_unit_activity(unit, ACTIVITY_SENTRY)",
            "action->unit_id, 0, \"\");",
            "v2_self_unit_action_still_legal(",
            "fc_agent_v2_unit_conversion_postcondition(",
            "fc_agent_v2_unit_consumed_postcondition(",
            "fc_agent_v2_unit_home_cleared_postcondition(",
            "unit->client.lifecycle_id != action->unit_lifecycle_id",
            "v2_pending.before_unit_present",
            "return punit != NULL ? punit->client.lifecycle_id : 0;",
        ):
            with self.subTest(required=required):
                self.assertIn(required, protocol)
        for required in (
            "before_lifecycle == expected_lifecycle",
            "current_lifecycle == expected_lifecycle",
            "activity_target_none",
            "before_type != desired_type",
            "before_present",
            "!current_present",
            "before_home != 0",
            "bool fc_agent_v2_unit_lifetime_matches(",
            "tracked_lifecycle != 0",
            "current_lifecycle != 0",
        ):
            with self.subTest(required=required):
                self.assertIn(required, codec)
        self.assertIn(
            "punit->client.lifecycle_id = client_unit_lifecycle_take();",
            packhand,
        )
        self.assertIn(
            "if (next_client_unit_lifecycle_id == UINT64_MAX)",
            packhand,
        )
        self.assertIn(
            "log_fatal(\"Client unit lifecycle identity space exhausted.\");",
            packhand,
        )
        self.assertIn("return next_client_unit_lifecycle_id++;", packhand)
        self.assertIn("uint64_t lifecycle_id;", unit_header)

    def test_native_transport_occupancy_reconciles_server_and_local_truth(self):
        repository = Path(v2_control.__file__).resolve().parent.parent
        protocol = (
            repository / "client" / "gui-agent" / "protocol_v2.c"
        ).read_text(encoding="utf-8")
        codec = (
            repository / "client" / "gui-agent" / "protocol_v2_codec.c"
        ).read_text(encoding="utf-8")
        for required in (
            "transporter->client.occupied, occupancy",
            "fc_agent_v2_transport_occupancy_exact(",
            "unit_transport_cargo(transporter)",
        ):
            with self.subTest(required=required):
                self.assertIn(required, protocol)
        self.assertIn(
            "advertised_occupied == (known_occupied > 0)", codec,
        )

    def test_native_transport_authority_matches_classic_client(self):
        repository = Path(v2_control.__file__).resolve().parent.parent
        protocol = (
            repository / "client" / "gui-agent" / "protocol_v2.c"
        ).read_text(encoding="utf-8")
        server = (repository / "server" / "unithand.c").read_text(
            encoding="utf-8",
        )
        client = (repository / "client" / "control.c").read_text(
            encoding="utf-8",
        )
        classic = (
            repository / "data" / "classic" / "actions.ruleset"
        ).read_text(encoding="utf-8")
        transport = protocol.split(
            "enum agent_v2_transport_state", 1,
        )[1].split("static bool v2_probability_is_certain", 1)[0]
        for required in (
            "unit_owner(actor) != self",
            "game_unit_by_number(id)",
            "can_player_see_unit(self, unit)",
            "pplayers_allied(unit_owner(cargo), unit_owner(transporter))",
            "unit_contained_in(transporter, cargo)",
            "v2_transport_component_signature(",
            "occupancy > 0, occupancy, capacity,",
            "transport_before_signature",
            "transport_after_signature",
            "current_signature == v2_pending.action.transport_after_signature",
            "context != unit_transport_get(",
            "adjc_iterate(&wld.map, origin, target)",
            "v2_build_transport_disembark(",
        ):
            with self.subTest(required=required):
                self.assertIn(required, protocol)
        self.assertNotIn("&& unit_owner(cargo) == self", transport)
        self.assertNotIn("&& unit_owner(transporter) == self", transport)
        self.assertNotIn(
            "client_tile_get_known(target) != TILE_UNKNOWN", protocol,
        )
        self.assertNotIn(
            "client_tile_get_known(target_tile) == TILE_UNKNOWN", protocol,
        )
        self.assertIn(
            "struct unit *actor_unit = player_unit_by_number(pplayer, actor_id);",
            server,
        )
        self.assertIn(
            "if (unit_owner(pcargo) == client.conn.playing)", client,
        )
        self.assertIn(
            "request_do_action(ACTION_TRANSPORT_UNLOAD,", client,
        )
        self.assertNotIn('action        = "Transport Load"', classic)


    def test_native_government_paths_use_safe_normal_client_semantics(self):
        repository = Path(v2_control.__file__).resolve().parent.parent
        protocol = (
            repository / "client" / "gui-agent" / "protocol_v2.c"
        ).read_text(encoding="utf-8")
        codec = (
            repository / "client" / "gui-agent" / "protocol_v2_codec.c"
        ).read_text(encoding="utf-8")
        for required in (
            "government_count() > FC_AGENT_V2_MAX_GOVERNMENTS",
            "untargeted_revolution_allowed()",
            "can_change_to_government((struct player *) self, target)",
            "v2_government_change_available(self, candidate)",
            "v2_hash_scoped_catalogs(hash, client_player(), &phase)",
            "start_revolution();",
            "set_government_choice(government);",
            "government capability is no longer legal",
        ):
            with self.subTest(required=required):
                self.assertIn(required, protocol)
        for required in (
            "revolution_finishes > current_turn",
            "revolution_finishes <= 0 && !has_no_anarchy",
            "after_current == during_government",
            "after_target == desired_government",
            "after_current == desired_government && after_target < 0",
        ):
            with self.subTest(required=required):
                self.assertIn(required, codec)


class V2ProjectionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.control = V2SeatControl("game_test", "agent_test", 1)

    def test_chat_feed_and_send_capability_are_bounded_and_strict(self):
        chat_action = _action(
            15, "player.send_chat", "p:1:10", -1,
            "player.send_chat", "Chat Channel", "Chat Echo Received", 0,
            "chat-required",
        )
        chat_row = (
            "chat sequence=9 turn=7 phase=1 sender=player "
            "sender_name=Other self=0 channel=allied event=chat_msg "
            "truncated=0 message=Meet%20at%20dawn"
        )
        rows = tuple(sorted(valid_rows() + (chat_action, chat_row)))
        observation_value = observation(rows)
        feed = self.control.state_page(
            observation_value, section="chat",
        )["page"]["items"]
        self.assertEqual(feed, [{
            "sequence": 9,
            "turn": 7,
            "phase": 1,
            "sender": {"kind": "player", "name": "Other", "self": False},
            "channel": "allied",
            "event": "chat_msg",
            "message": "Meet at dawn",
            "truncated": False,
        }])
        snapshot = self.control._snapshot(observation_value)
        action = next(
            item for item in snapshot.legal_actions
            if item["kind"] == "player.send_chat"
        )
        binding = snapshot.action_bindings[action["action_id"]]
        self.assertEqual(
            self.control._resolve_arguments(
                snapshot, binding,
                {"channel": "global", "message": "Hello world"},
            ),
            "channel=global;message=Hello%20world",
        )
        for message in ("", "/help", ".allies", "Other: secret", "bad\nline"):
            with self.subTest(message=message), self.assertRaisesRegex(
                V2ControlError, "invalid_request",
            ):
                self.control._resolve_arguments(
                    snapshot, binding,
                    {"channel": "global", "message": message},
                )
        inactive = V2SeatControl("game_chat_inactive", "agent_chat", 1)
        inactive_actions = inactive.legal_actions_page(observation(
            tuple(sorted(valid_rows(actions=False) + (chat_action,))),
        ))["page"]["items"]
        self.assertEqual(
            [item["kind"] for item in inactive_actions],
            ["player.send_chat"],
        )

    @staticmethod
    def pregame_catalog(request, rows, *, serial: int):
        rows = tuple(rows)
        return {
            "generation": 1,
            "native_revision": request.native_revision,
            "section": request.section,
            "selector": request.selector,
            "view_id": f"q{request.native_revision}-{serial}",
            "offset": 0,
            "count": len(rows),
            "total_count": len(rows),
            "next_offset": len(rows),
            "complete": True,
            "overflow": False,
            "rows": rows,
        }

    def test_pregame_projects_catalogs_configuration_and_desired_readiness(self):
        current = observation(pregame_rows(), revision=5)
        overview = self.control.state_page(current)["page"]["items"][0]
        self.assertEqual(overview["client_state"], "preparing")
        self.assertFalse(overview["player"]["ready"])

        actions = self.control.legal_actions_page(current)["page"]["items"]
        self.assertEqual(
            {item["kind"] for item in actions},
            {"pregame.configure", "pregame.set_ready", "pregame.set_team"},
        )
        ready = next(item for item in actions
                     if item["kind"] == "pregame.set_ready")
        ready_resolution = self.control.resolve_action(
            current, ready["state_revision"], ready["action_id"],
            {"ready": True},
        )
        self.assertEqual(ready_resolution.native_arguments, "ready=1")

        nation_request = self.control.prepare_state_scope(
            current, "pregame_nations",
        )
        nation_page = self.control.materialize_state_scope(
            nation_request,
            self.pregame_catalog(nation_request, (
                "pregame_nation id=1 name=Romans default_style=2",
                "pregame_nation id=3 name=Greeks default_style=4",
            ), serial=1),
        )
        style_request = self.control.prepare_state_scope(
            current, "pregame_styles",
        )
        style_page = self.control.materialize_state_scope(
            style_request,
            self.pregame_catalog(style_request, (
                "pregame_style id=2 name=European",
                "pregame_style id=4 name=Classical",
            ), serial=2),
        )
        nation = nation_page["page"]["items"][0]
        style = style_page["page"]["items"][0]
        team_request = self.control.prepare_state_scope(
            current, "pregame_teams",
        )
        team_page = self.control.materialize_state_scope(
            team_request,
            self.pregame_catalog(team_request, (
                "pregame_team id=1 name=Blue selected=1 occupied=1 "
                "member_count=2",
                "pregame_team id=2 name=Red selected=0 occupied=1 "
                "member_count=1",
                "pregame_team id=3 name=Green selected=0 occupied=0 "
                "member_count=0",
                "pregame_team_member team=1 player=p:1:10 leader=Codex",
                "pregame_team_member team=1 player=p:2:20 leader=Claude",
                "pregame_team_member team=2 player=p:3:30 leader=Pi",
            ), serial=3),
        )
        teams = team_page["page"]["items"]
        self.assertEqual(len(teams), 3)
        self.assertEqual(
            teams[0], {
                "id": teams[0]["id"],
                "name": "Blue",
                "selected": True,
                "occupied": True,
                "member_count": 2,
                "members": [
                    {
                        "id": teams[0]["members"][0]["id"],
                        "leader_name": "Codex",
                        "self": True,
                    },
                    {
                        "id": teams[0]["members"][1]["id"],
                        "leader_name": "Claude",
                        "self": False,
                    },
                ],
            },
        )
        public_team_json = json.dumps(team_page, sort_keys=True)
        self.assertNotIn("team=", public_team_json)
        self.assertNotIn("p:1:10", public_team_json)
        self.assertNotIn("p:2:20", public_team_json)
        self.assertTrue(all(
            item["id"] not in {"1", "2", "3"} for item in teams
        ))
        set_team = next(
            item for item in actions if item["kind"] == "pregame.set_team"
        )
        team_resolution = self.control.resolve_action(
            current, set_team["state_revision"], set_team["action_id"],
            {"team_id": teams[1]["id"]},
        )
        self.assertEqual(team_resolution.native_arguments, "team=2")
        with self.assertRaises(V2ControlError) as no_op:
            self.control.resolve_action(
                current, set_team["state_revision"], set_team["action_id"],
                {"team_id": teams[0]["id"]},
            )
        self.assertEqual(no_op.exception.code, "invalid_request")
        configure = next(item for item in actions
                         if item["kind"] == "pregame.configure")
        resolution = self.control.resolve_action(
            current, configure["state_revision"], configure["action_id"], {
                "nation_id": nation["id"],
                "leader_name": "claude Five",
                "is_male": False,
                "style_id": style["id"],
            },
        )
        self.assertEqual(
            resolution.native_arguments,
            "nation=1,leader=Claude%20Five,is_male=0,style=2",
        )

        newer = observation(pregame_rows(), revision=6)
        with self.assertRaises(V2ControlError) as stale:
            self.control.resolve_action(
                newer, set_team["state_revision"], set_team["action_id"],
                {"team_id": teams[1]["id"]},
            )
        self.assertEqual(stale.exception.code, "stale_revision")

        blocked = V2SeatControl("game_test", "agent_test", 1)
        blocked.set_pregame_ready_allowed(False)
        blocked_actions = blocked.legal_actions_page(current)["page"]["items"]
        self.assertEqual(
            {item["kind"] for item in blocked_actions},
            {"pregame.configure", "pregame.set_team"},
        )
        blocked_token = blocked_actions[0]["state_revision"]["state_token"]
        blocked.set_pregame_ready_allowed(True)
        released_actions = blocked.legal_actions_page(current)["page"]["items"]
        self.assertEqual(
            {item["kind"] for item in released_actions},
            {"pregame.configure", "pregame.set_ready", "pregame.set_team"},
        )
        self.assertNotEqual(
            blocked_token,
            released_actions[0]["state_revision"]["state_token"],
        )
        blocked.close()

        ready_current = observation(pregame_rows(ready=True), revision=7)
        ready_actions = self.control.legal_actions_page(
            ready_current,
        )["page"]["items"]
        self.assertEqual(len(ready_actions), 1)
        self.assertEqual(ready_actions[0]["kind"], "pregame.set_ready")
        resolution = self.control.resolve_action(
            ready_current, ready_actions[0]["state_revision"],
            ready_actions[0]["action_id"], {"ready": False},
        )
        self.assertEqual(resolution.native_arguments, "ready=0")
        without_action = tuple(
            row for row in pregame_rows()
            if "native_rule=pregame.set_team" not in row
        )
        self.assertInternal(without_action)

        old_summary = tuple(
            row.replace(" team_choices=3", "")
            if row.startswith("pregame ") else row
            for row in pregame_rows()
        )
        self.assertInternal(old_summary)

    def assertInternal(self, rows: tuple[str, ...]) -> None:
        with self.assertRaises(V2ControlError) as caught:
            self.control.state_page(observation(rows))
        self.assertEqual(caught.exception.code, "internal_error")
        self.assertEqual(str(caught.exception), "internal_error")

    def test_airlift_remaining_may_exceed_current_max(self):
        rows = replace_row(
            valid_rows(),
            "airlift_remaining=1 airlift_max=1",
            "airlift_remaining=2 airlift_max=0",
        )
        city = self.control.state_page(
            observation(rows), "cities",
        )["page"]["items"][0]
        self.assertEqual(city["airlift"], {"remaining": 2, "maximum": 0})

    def test_treaty_ids_are_stable_across_edits_and_rotate_on_reopen(self):
        gold = ("p:1:10", 1, "Gold", "gold", 17, "gold")
        advance = (
            "p:1:10", 0, "Advance", "technology", 4, "Writing",
        )
        first = observation(treaty_rows((gold,)), revision=21)
        edited = observation(treaty_rows((advance, gold)), revision=22)
        reopened = observation(
            treaty_rows((advance, gold), generation=4), revision=23,
        )

        relation_first = self.control.state_page(
            first, "diplomacy",
        )["page"]["items"][0]
        gold_first = self.control.state_page(
            first, "diplomacy_clauses",
        )["page"]["items"][0]
        relation_edited = self.control.state_page(
            edited, "diplomacy",
        )["page"]["items"][0]
        gold_edited = self.control.state_page(
            edited, "diplomacy_clauses",
        )["page"]["items"][1]
        relation_reopened = self.control.state_page(
            reopened, "diplomacy",
        )["page"]["items"][0]
        gold_reopened = self.control.state_page(
            reopened, "diplomacy_clauses",
        )["page"]["items"][1]

        self.assertEqual(gold_first["position"], 0)
        self.assertEqual(gold_edited["position"], 1)
        self.assertEqual(
            relation_first["meeting"]["meeting_id"],
            relation_edited["meeting"]["meeting_id"],
        )
        self.assertEqual(gold_first["clause_id"], gold_edited["clause_id"])
        self.assertNotEqual(
            relation_edited["meeting"]["meeting_id"],
            relation_reopened["meeting"]["meeting_id"],
        )
        self.assertNotEqual(
            gold_edited["clause_id"], gold_reopened["clause_id"],
        )

    def test_nonempty_treaty_projection_is_opaque_and_cross_linked(self):
        clauses = (
            ("p:1:10", 0, "Advance", "technology", 4, "Writing"),
            ("p:1:10", 1, "Gold", "gold", 17, "gold"),
            ("p:2:20", 10, "SharedTiles", "none", 0, "none"),
        )
        current = observation(treaty_rows(clauses), revision=24)
        public = json.dumps([
            self.control.state_page(current, "diplomacy"),
            self.control.state_page(current, "diplomacy_clauses"),
        ], sort_keys=True)
        for secret in (
            "p:1:10", "p:2:20", '"native_value"', '"native_type"',
            '"giver_ref"', '"other_ref"', '"technology_id": 4',
            '"city_id": 20',
        ):
            with self.subTest(secret=secret):
                self.assertNotIn(secret, public)
        clauses_page = self.control.state_page(
            current, "diplomacy_clauses",
        )["page"]["items"]
        self.assertEqual(
            clauses_page[0]["meeting_id"],
            clauses_page[1]["meeting_id"],
        )
        self.assertTrue(clauses_page[0]["value"]["id"].startswith("tech_"))
        self.assertEqual(clauses_page[1]["value"], {
            "type": "gold", "amount": 17,
        })

    def test_treaty_digest_and_giver_forgery_fail_closed(self):
        clauses = (("p:1:10", 1, "Gold", "gold", 17, "gold"),)
        valid = treaty_rows(clauses)
        digest = v2_control._diplomacy_clauses_digest([{
            "giver_ref": "p:1:10", "native_type": 1,
            "native_value": 17,
        }])
        hostile = (
            replace_row(
                valid,
                f"clauses_digest={digest}",
                "clauses_digest=fnv1a64-0000000000000000",
            ),
            replace_row(valid, "giver=p:1:10", "giver=p:9:90"),
        )
        for rows in hostile:
            with self.subTest(rows=rows):
                self.assertInternal(rows)

    @staticmethod
    def relation_page(request, rows, *, view="r21-1", total=None):
        rows = tuple(rows)
        total = len(rows) if total is None else total
        return {
            "generation": 1,
            "native_revision": request.native_revision,
            "actor_ref": request.native_actor_ref,
            "counterpart_ref": request.native_counterpart_ref,
            "view_id": view,
            "offset": request.offset,
            "count": len(rows),
            "total_count": total,
            "next_offset": request.offset + len(rows),
            "complete": True,
            "overflow": False,
            "rows": rows,
        }

    def test_relation_scope_is_opaque_atomic_and_tech_hole_safe(self):
        gold = ("p:1:10", 1, "Gold", "gold", 17, "gold")
        rows = treaty_rows((gold,))
        current = observation(rows, revision=21)
        overview = self.control.state_page(current)["page"]["items"][0]
        relation = self.control.state_page(
            current, "diplomacy",
        )["page"]["items"][0]
        request = self.control.prepare_relation_scope(
            current, overview["player"]["id"], relation["relation_id"], 3,
        )
        self.assertEqual(request.native_actor_ref, "p:1:10")
        self.assertEqual(request.native_counterpart_ref, "p:2:20")
        digest = v2_control._diplomacy_clauses_digest([{
            "giver_ref": "p:1:10", "native_type": 1,
            "native_value": 17,
        }])
        actions = (
            relation_action(
                0x201, "diplomacy.close_meeting", "Meeting Closed",
                "meeting", clauses_digest=digest,
            ),
            relation_action(
                0x202, "diplomacy.accept", "Acceptance Recorded",
                "accepted", clauses_digest=digest, desired_acceptance=1,
            ),
            relation_action(
                0x203, "diplomacy.remove_clause", "Clause Removed", "Gold",
                clauses_digest=digest, clause_giver="p:1:10",
                clause_type="Gold", clause_value=17, clause_name="gold",
            ),
            relation_action(
                0x204, "diplomacy.break_relation", "Relation Changed",
                "lower relation", clauses_digest=digest,
            ),
            # Tech 42 deliberately does not appear in this seat's reachable
            # research rows. Rulesets may permit hole trading from the other
            # player, so the native ruleset name is carried by the action.
            relation_action(
                0x205, "diplomacy.propose_clause", "Clause Proposed",
                "Advance", clauses_digest=digest, clause_giver="p:2:20",
                clause_type="Advance", clause_value=42,
                clause_name="Railroad",
            ),
            relation_action(
                0x206, "diplomacy.propose_gold", "Clause Proposed", "Gold",
                clauses_digest=digest, clause_giver="p:1:10",
                clause_type="Gold", clause_value=40, clause_name="gold",
                args="gold-required",
            ),
        )
        first = self.control.relation_scope_page(
            request,
            self.relation_page(request, actions[:3], total=6),
        )
        self.assertEqual(first["page"]["scope"], {
            "actor_id": overview["player"]["id"],
            "actor_type": "player",
            "target_id": relation["relation_id"],
            "target_type": "diplomatic_relation",
        })
        self.assertIsNotNone(first["page"]["next_cursor"])
        self.assertRegex(first["page"]["catalog_id"], r"^catalog_[0-9a-f]{32}$")
        self.assertFalse(first["page"]["catalog_complete"])
        self.assertIsInstance(first["page"]["cursor_expires_at"], str)
        with self.assertRaisesRegex(V2ControlError, "action_expired"):
            self.control.resolve_action(
                current, first["state_revision"],
                first["page"]["items"][0]["action_id"], {},
            )
        continued = self.control.take_relation_scope_cursor(
            first["page"]["next_cursor"], endpoint="legal_actions",
        )
        self.assertIsNotNone(continued)
        final = self.control.relation_scope_page(
            continued,
            self.relation_page(
                continued, actions[3:], view="r21-1", total=6,
            ),
        )
        self.assertIsNone(final["page"]["next_cursor"])
        self.assertEqual(
            final["page"]["catalog_id"], first["page"]["catalog_id"],
        )
        self.assertTrue(final["page"]["catalog_complete"])
        self.assertIsNone(final["page"]["cursor_expires_at"])
        tech = next(
            item for item in final["page"]["items"]
            if item["subject"].get("clause", {}).get("type") == "technology"
        )
        self.assertEqual(tech["subject"]["clause"]["value"]["name"], "Railroad")
        self.assertTrue(
            tech["subject"]["clause"]["value"]["id"].startswith("tech_")
        )
        gold_action = next(
            item for item in final["page"]["items"]
            if item["arguments_schema"].get("required") == ["gold"]
        )
        self.assertEqual(
            gold_action["arguments_schema"]["properties"]["gold"],
            {
                "type": "integer", "minimum": 1, "maximum": 40,
                "multipleOf": 1, "examples": [10],
            },
        )
        resolution = self.control.resolve_action(
            current, gold_action["state_revision"],
            gold_action["action_id"], {"gold": 23},
        )
        self.assertEqual(resolution.native_arguments, "gold=23")
        self.assertEqual(resolution.native_actor_ref, "p:1:10")
        self.assertEqual(resolution.native_counterpart_ref, "p:2:20")
        self.assertTrue(resolution.scoped)
        self.assertTrue(resolution.relation_scoped)
        public = json.dumps((first, final), sort_keys=True)
        self.assertNotIn("p:1:10", public)
        self.assertNotIn("p:2:20", public)
        self.assertNotIn(digest, public)

    def test_relation_scope_can_remove_counterpart_state_clause(self):
        ceasefire = ("p:2:20", 5, "Ceasefire", "none", 0, "none")
        current = observation(treaty_rows((ceasefire,)), revision=25)
        overview = self.control.state_page(current)["page"]["items"][0]
        relation = self.control.state_page(
            current, "diplomacy",
        )["page"]["items"][0]
        request = self.control.prepare_relation_scope(
            current, overview["player"]["id"], relation["relation_id"], 4,
        )
        digest = v2_control._diplomacy_clauses_digest([{
            "giver_ref": "p:2:20", "native_type": 5,
            "native_value": 0,
        }])
        rows = (
            relation_action(
                0x251, "diplomacy.close_meeting", "Meeting Closed",
                "meeting", clauses_digest=digest,
            ),
            relation_action(
                0x252, "diplomacy.accept", "Acceptance Recorded",
                "accepted", clauses_digest=digest, desired_acceptance=1,
            ),
            relation_action(
                0x253, "diplomacy.break_relation", "Relation Changed",
                "lower relation", clauses_digest=digest,
            ),
            relation_action(
                0x254, "diplomacy.remove_clause", "Clause Removed",
                "Ceasefire", clauses_digest=digest,
                clause_giver="p:2:20", clause_type="Ceasefire",
                clause_value=0,
            ),
        )
        page = self.control.relation_scope_page(
            request, self.relation_page(
                request, rows, view="r25-1", total=4,
            ),
        )
        remove = next(
            item for item in page["page"]["items"]
            if item["subject"]["operation"] == "remove_clause"
        )
        self.assertEqual(remove["subject"]["clause"]["type"], "ceasefire")
        self.assertEqual(
            remove["subject"]["clause"]["giver_player_id"],
            relation["player_id"],
        )

    def test_relation_scope_rejects_pair_forgery_and_blocked_cancel(self):
        rows = replace_row(
            valid_rows(),
            "can_cancel=1 cancel_reason=allowed",
            "can_cancel=0 cancel_reason=senate_blocking",
        )
        current = observation(rows, revision=31)
        overview = self.control.state_page(current)["page"]["items"][0]
        relation = self.control.state_page(
            current, "diplomacy",
        )["page"]["items"][0]
        self.assertFalse(relation["can_break_relation"])
        self.assertEqual(relation["cancel_relation"], {
            "allowed": False, "reason": "senate_blocking",
        })
        request = self.control.prepare_relation_scope(
            current, overview["player"]["id"], relation["relation_id"], 16,
        )
        opened = relation_action(
            0x301, "diplomacy.open_meeting", "Meeting Opened", "meeting",
            clauses_digest="fnv1a64-cbf29ce484222325",
        ).replace("meeting_generation=3", "meeting_generation=0")
        forged = self.relation_page(request, (opened,), view="r31-1")
        forged["counterpart_ref"] = "p:9:90"
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            self.control.relation_scope_page(request, forged)

        blocked_break = relation_action(
            0x302, "diplomacy.break_relation", "Relation Changed",
            "lower relation", clauses_digest="fnv1a64-cbf29ce484222325",
        ).replace("meeting_generation=3", "meeting_generation=0")
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            self.control.relation_scope_page(
                request,
                self.relation_page(request, (blocked_break,), view="r31-2"),
            )

    @staticmethod
    def scope_page(request, rows, *, view="v11-1", total=None):
        rows = tuple(rows)
        total = len(rows) if total is None else total
        return {
            "generation": 1,
            "native_revision": request.native_revision,
            "actor_ref": request.native_actor_ref,
            "view_id": view,
            "offset": request.offset,
            "count": len(rows),
            "total_count": total,
            "next_offset": request.offset + len(rows),
            "complete": True,
            "overflow": False,
            "rows": rows,
        }

    def test_actor_scopes_project_global_and_scoped_only_capabilities(self):
        current = observation()
        overview = self.control.state_page(current)["page"]["items"][0]
        player_id = overview["player"]["id"]
        public_city = self.control.state_page(
            current, "cities",
        )["page"]["items"][0]
        public_build_choices = self.control.state_page(
            current, "city_build_choices", actor_id=public_city["id"],
        )["page"]["items"]
        city_id = public_city["id"]
        units = self.control.state_page(
            current, "units",
        )["page"]["items"]
        own_unit_id = next(item["id"] for item in units if item["scope"] == "own")

        action_rows = [row for row in valid_rows() if row.startswith("action ")]
        player_rows = [
            row for row in action_rows
            if " actor=none " in row
        ] + list(scoped_government_rows())
        unit_rows = [
            row for row in action_rows
            if " actor=u:10:100 " in row
        ] + list(scoped_worker_rows())
        city_rows = list(scoped_city_rows())
        for actor_id, expected_kind, rows, serial in (
            (player_id, "player", player_rows, 1),
            (own_unit_id, "unit", unit_rows, 2),
            (city_id, "city", city_rows, 3),
        ):
            with self.subTest(kind=expected_kind):
                request = self.control.prepare_actor_scope(
                    current, actor_id, 16,
                )
                self.assertEqual(request.actor_kind, expected_kind)

                payload = self.control.actor_scope_page(
                    request,
                    self.scope_page(
                        request, rows, view=f"v11-{serial}",
                    ),
                )
                self.assertEqual(payload["page"]["scope"], {
                    "actor_id": actor_id,
                    "actor_type": expected_kind,
                })
                self.assertEqual(payload["page"]["total_items"], len(rows))
                self.assertEqual(len(payload["page"]["items"]), len(rows))
                for item in payload["page"]["items"]:
                    operation = item["subject"]["operation"]
                    arguments = (
                        {"tax": 30, "luxury": 10, "science": 60}
                        if operation == "set_rates" else
                        {"city_name": "Scoped City"}
                        if operation in {"found_city", "rename"} else
                        {"items": [
                            public_build_choices[0]["id"]
                        ]}
                        if operation == "set_worklist" else
                        {"allow_disband": True, "new_citizens": "science"}
                        if operation == "set_options" else governor_goal()
                        if operation == "set_governor" else {}
                    )
                    resolved = self.control.resolve_action(
                        current, item["state_revision"], item["action_id"],
                        arguments,
                    )
                    self.assertTrue(resolved.scoped)
                    self.assertEqual(
                        resolved.native_actor_ref, request.native_actor_ref,
                    )

        global_operations = {
            item["subject"]["operation"]
            for item in self.control.legal_actions_page(current)["page"]["items"]
        }
        self.assertTrue({
            "set_production", "buy_production", "start_activity",
            "cancel_activity", "revolution", "change",
        }.isdisjoint(global_operations))

        player_request = self.control.prepare_actor_scope(
            current, player_id, 16,
        )
        player_page = self.control.actor_scope_page(
            player_request,
            self.scope_page(player_request, player_rows, view="v11-8"),
        )
        government_actions = [
            item for item in player_page["page"]["items"]
            if item["kind"].startswith("government.")
        ]
        self.assertEqual(
            {item["subject"]["operation"] for item in government_actions},
            {"revolution", "change"},
        )
        self.assertEqual(
            {item["subject"]["target"]["name"] for item in government_actions},
            {"Anarchy", "Monarchy", "Republic"},
        )
        self.assertTrue(all(
            item["subject"]["actor"] == {"type": "player", "id": player_id}
            and item["subject"]["target"]["id"].startswith("government_")
            and item["arguments_schema"]["properties"] == {}
            for item in government_actions
        ))

        city_request = self.control.prepare_actor_scope(current, city_id, 16)
        city_page = self.control.actor_scope_page(
            city_request,
            self.scope_page(city_request, city_rows, view="v11-4"),
        )
        self.assertEqual(
            {item["kind"] for item in city_page["page"]["items"]},
            {
                "city.set_production", "city.buy_production",
                "city.set_worklist", "city.set_options", "city.rename",
                "city.set_governor",
            },
        )
        self.assertTrue(all(
            item["subject"]["target"]["id"].startswith("production_")
            for item in city_page["page"]["items"]
            if item["kind"] in {
                "city.set_production", "city.buy_production",
            }
        ))

        worker_request = self.control.prepare_actor_scope(
            current, own_unit_id, 16,
        )
        worker_page = self.control.actor_scope_page(
            worker_request,
            self.scope_page(worker_request, unit_rows, view="v11-5"),
        )
        starts = [
            item for item in worker_page["page"]["items"]
            if item["subject"]["operation"] == "start_activity"
        ]
        self.assertEqual(
            {item["subject"]["target"]["name"] for item in starts},
            {"cultivate", "road", "pillage"},
        )
        pillage = next(
            item for item in starts
            if item["subject"]["target"]["name"] == "pillage"
        )
        self.assertEqual(
            pillage["subject"]["target"]["extra"]["name"], "Irrigation",
        )
        self.assertTrue(
            pillage["subject"]["target"]["extra"]["id"].startswith("extra_")
        )

    def test_target_action_projects_exact_known_tile_and_binds_execution(self):
        current = observation()
        units = self.control.state_page(
            current, "units",
        )["page"]["items"]
        actor_id = next(item["id"] for item in units if item["scope"] == "own")
        tiles = self.control.state_page(
            current, "known_tiles",
        )["page"]["items"]
        target_id = next(item["id"] for item in tiles if item["x"] == 3)
        request = self.control.prepare_target_action(
            current, actor_id, target_id,
        )
        self.assertEqual(request.native_actor_ref, "u:10:100")
        self.assertEqual(request.native_target_tile, 7)
        row = _action(
            0, "unit.goto", "u:10:100", 7, "unit.goto", "Tile",
            "Orders Queued", 0, target_name="destination",
        ).replace(
            "slot=a0000000000000000",
            "slot=t000000070123456789ABCDEF",
        )
        page = self.control.target_action_page(request, {
            "generation": 1,
            "native_revision": request.native_revision,
            "actor_ref": request.native_actor_ref,
            "native_tile": request.native_target_tile,
            "count": 1,
            "rows": (row,),
        })
        self.assertEqual(page["page"]["total_items"], 1)
        self.assertIsNone(page["page"]["next_cursor"])
        action = page["page"]["items"][0]
        self.assertEqual(action["kind"], "unit.order")
        self.assertEqual(action["subject"]["operation"], "goto")
        self.assertEqual(action["subject"]["actor"]["id"], actor_id)
        self.assertEqual(action["subject"]["target"]["id"], target_id)
        resolution = self.control.resolve_action(
            current, action["state_revision"], action["action_id"], {},
        )
        self.assertEqual(resolution.native_slot, "t000000070123456789ABCDEF")
        self.assertEqual(resolution.native_actor_ref, "u:10:100")
        self.assertTrue(resolution.scoped)

        empty = self.control.target_action_page(request, {
            "generation": 1,
            "native_revision": request.native_revision,
            "actor_ref": request.native_actor_ref,
            "native_tile": request.native_target_tile,
            "count": 0,
            "rows": (),
        })
        self.assertEqual(empty["page"]["items"], [])
        self.assertEqual(empty["page"]["total_items"], 0)

    def test_target_route_order_families_are_semantic_and_opaque(self):
        current = observation()
        actor_id = next(
            item["id"] for item in self.control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        target_id = next(
            item["id"] for item in self.control.state_page(
                current, "known_tiles",
            )["page"]["items"] if item["x"] == 3
        )
        request = self.control.prepare_target_action(
            current, actor_id, target_id,
        )
        rows = (
            _action(
                0, "unit.goto", "u:10:100", 7, "unit.goto", "Tile",
                "Orders Queued", 0, target_name="destination",
            ).replace(
                "slot=a0000000000000000",
                "slot=t000000070123456789ABCDEF",
            ),
            _action(
                0, "unit.goto_and_perform", "u:10:100", 7,
                "unit.goto_and_perform", "Action Route", "Orders Queued",
                0, legality="possibly_legal", probability_kind="unknown",
                probability_min=0, probability_max=200,
                target_name="Enter Hut",
            ).replace(
                "slot=a0000000000000000",
                "slot=t00000007123456789ABCDEF0",
            ),
            _action(
                0, "unit.connect_route", "u:10:100", 7,
                "unit.connect_route", "Construction Route", "Orders Queued",
                0, target_extra=4, activity="road", target_name="Road",
            ).replace(
                "slot=a0000000000000000",
                "slot=t00000007ABCDEF0123456789",
            ),
        )
        page = self.control.target_action_page(request, {
            "generation": 1,
            "native_revision": request.native_revision,
            "actor_ref": request.native_actor_ref,
            "native_tile": request.native_target_tile,
            "count": len(rows),
            "rows": rows,
        })
        items = {
            item["subject"]["operation"]: item
            for item in page["page"]["items"]
        }
        self.assertEqual(
            set(items), {"goto", "goto_and_perform", "connect_route"},
        )
        self.assertEqual(
            items["goto_and_perform"]["subject"]["target"]["action"],
            {"type": "native_action", "name": "Enter Hut"},
        )
        self.assertEqual(
            items["connect_route"]["subject"]["target"]["construction"][
                "activity"
            ],
            "road",
        )
        self.assertTrue(
            items["connect_route"]["subject"]["target"]["construction"][
                "id"
            ].startswith("extra_"),
        )
        for item in items.values():
            resolved = self.control.resolve_action(
                current, item["state_revision"], item["action_id"], {},
            )
            self.assertEqual(resolved.native_arguments, "-")
            self.assertTrue(resolved.scoped)
        public = json.dumps(page, sort_keys=True)
        for private in (
            "ORDER_PERFORM_ACTION", "ORDER_ACTION_MOVE", "target_extra=4",
            "native_target_extra", "packet_unit_orders", "subtarget",
        ):
            self.assertNotIn(private, public)

    def test_map_tiles_binds_unknown_goto_and_remembered_paradrop(self):
        rows = tuple(
            row.replace(
                "map_width=16 map_height=16",
                "map_width=4 map_height=4",
            ).replace(" y=2", " y=1")
            for row in valid_rows()
        )
        current = observation(rows)
        actor_id = next(
            item["id"] for item in self.control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        known_rows = {
            5: next(row for row in rows if row.startswith("tile index=5 ")),
            6: next(row for row in rows if row.startswith("tile index=6 ")),
            7: next(row for row in rows if row.startswith("tile index=7 ")),
        }
        map_rows = tuple(
            known_rows.get(
                index,
                "tile "
                f"index={index} x={index % 4} y={index // 4} "
                "known=0 terrain=unknown owner=none placing_extra=-1 "
                "placing_extra_name=none placing_turns=0 placing_time=-1",
            )
            for index in range(16)
        )
        state_request = self.control.prepare_state_scope(
            current, "map_tiles", 16,
        )
        page = self.control.materialize_state_scope(
            state_request, state_scope_catalog(state_request, map_rows),
        )
        self.assertEqual(page["page"]["total_items"], 16)
        unknown = next(
            item for item in page["page"]["items"]
            if item["x"] == 3 and item["y"] == 3
        )
        self.assertEqual(set(unknown), {"id", "x", "y", "visibility"})
        self.assertEqual(unknown["visibility"], "unknown")

        unknown_request = self.control.prepare_target_action(
            current, actor_id, unknown["id"],
        )
        self.assertEqual(unknown_request.native_target_tile, 15)
        goto = _action(
            0, "unit.goto", "u:10:100", 15, "unit.goto", "Tile",
            "Orders Queued", 0, target_name="destination",
        ).replace(
            "slot=a0000000000000000",
            "slot=t0000000F0123456789ABCDEF",
        )
        target_page = self.control.target_action_page(unknown_request, {
            "generation": 1,
            "native_revision": unknown_request.native_revision,
            "actor_ref": unknown_request.native_actor_ref,
            "native_tile": unknown_request.native_target_tile,
            "count": 1,
            "rows": (goto,),
        })
        self.assertEqual(
            target_page["page"]["items"][0]["subject"]["operation"],
            "goto",
        )
        self.assertEqual(
            set(target_page["page"]["items"][0]["subject"]["target"]),
            {"type", "id", "x", "y"},
        )

        unknown_paradrop = _action(
            0, "unit.paradrop", "u:10:100", 15,
            "Paradrop Unit", "Tile", "Unit Paradrop", 0,
            target_name="destination",
        ).replace(
            "slot=a0000000000000000",
            "slot=t0000000FFEDCBA9876543210",
        )
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            self.control.target_action_page(unknown_request, {
                "generation": 1,
                "native_revision": unknown_request.native_revision,
                "actor_ref": unknown_request.native_actor_ref,
                "native_tile": unknown_request.native_target_tile,
                "count": 1,
                "rows": (unknown_paradrop,),
            })

        remembered = next(
            item for item in page["page"]["items"]
            if item["visibility"] == "remembered"
        )
        remembered_request = self.control.prepare_target_action(
            current, actor_id, remembered["id"],
        )
        paradrop = _action(
            0, "unit.paradrop", "u:10:100", 7,
            "Paradrop Unit Frighten", "Tile", "Unit Paradrop", 0,
            target_name="destination", legality="unresolved",
            probability_kind="not_implemented", probability_min=-1,
            probability_max=-1,
        ).replace(
            "slot=a0000000000000000",
            "slot=t000000070123456789ABCDEF",
        )
        remembered_page = self.control.target_action_page(
            remembered_request, {
                "generation": 1,
                "native_revision": remembered_request.native_revision,
                "actor_ref": remembered_request.native_actor_ref,
                "native_tile": remembered_request.native_target_tile,
                "count": 1,
                "rows": (paradrop,),
            },
        )
        descriptor = remembered_page["page"]["items"][0]
        self.assertEqual(descriptor["subject"]["operation"], "paradrop")
        self.assertEqual(
            descriptor["subject"]["target"]["visibility"], "remembered",
        )
        self.assertEqual(
            descriptor["subject"]["probability"]["kind"],
            "not_implemented",
        )

    def test_city_rally_target_action_is_opaque_and_binds_persistence(self):
        current = observation()
        city = self.control.state_page(
            current, "cities",
        )["page"]["items"][0]
        remembered = next(
            item for item in self.control.state_page(
                current, "known_tiles",
            )["page"]["items"] if item["visibility"] == "remembered"
        )
        request = self.control.prepare_target_action(
            current, city["id"], remembered["id"],
        )
        self.assertEqual(request.actor_kind, "city")
        self.assertEqual(request.native_actor_ref, "c:20:200")
        self.assertEqual(request.native_target_tile, 7)
        row = _action(
            0, "city.set_rally", "c:20:200", 7,
            "city.set_rally", "Tile", "Rally Point Set", 0,
            "persistent-required", target_name="destination",
        ).replace(
            "slot=a0000000000000000",
            "slot=t000000070123456789ABCDEF",
        )
        page = self.control.target_action_page(request, {
            "generation": 1,
            "native_revision": request.native_revision,
            "actor_ref": request.native_actor_ref,
            "native_tile": request.native_target_tile,
            "count": 1,
            "rows": (row,),
        })
        action = page["page"]["items"][0]
        self.assertEqual(action["kind"], "city.set_rally")
        self.assertEqual(action["subject"]["operation"], "set_rally")
        self.assertEqual(action["subject"]["actor"]["id"], city["id"])
        self.assertEqual(action["subject"]["target"]["id"], remembered["id"])
        self.assertRegex(
            action["subject"]["variant"], r"^variant_[0-9a-f]{32}$",
        )
        self.assertEqual(action["arguments_schema"], {
            "type": "object",
            "properties": {"persistent": {"type": "boolean"}},
            "required": ["persistent"],
            "additionalProperties": False,
        })
        resolution = self.control.resolve_action(
            current, action["state_revision"], action["action_id"],
            {"persistent": True},
        )
        self.assertEqual(resolution.native_arguments, "persistent=1")
        self.assertEqual(resolution.operation, "set_rally")
        self.assertEqual(resolution.native_actor_ref, "c:20:200")
        self.assertTrue(resolution.scoped)
        for invalid in ({}, {"persistent": 1}, {"persistent": False, "x": 1}):
            with self.subTest(invalid=invalid), self.assertRaisesRegex(
                V2ControlError, "invalid_request",
            ):
                self.control.resolve_action(
                    current, action["state_revision"], action["action_id"],
                    invalid,
                )
        public = json.dumps([city, action], sort_keys=True)
        for private in (
            "c:20:200", "t000000070123456789ABCDEF",
            "fnv1a64-", "orders_digest", "native_target_tile",
        ):
            self.assertNotIn(private, public)

    def test_server_discovered_safe_rare_action_is_opaque_and_paginated(self):
        current = observation()
        actor_id = next(
            item["id"] for item in self.control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        target_id = next(
            item["id"] for item in self.control.state_page(
                current, "known_tiles",
            )["page"]["items"] if item["x"] == 2
        )
        request = self.control.prepare_target_action(
            current, actor_id, target_id, limit=1,
        )
        goto = _action(
            0, "unit.goto", "u:10:100", 6, "unit.goto", "Tile",
            "Orders Queued", 0, target_name="destination",
        ).replace(
            "slot=a0000000000000000",
            "slot=t000000060123456789ABCDEF",
        )
        sabotage = _action(
            0, "unit.special", "u:10:100", 6, "Sabotage Unit", "Unit",
            "Unit Sabotage Unit", 0, legality="possibly_legal",
            probability_kind="range", probability_min=80,
            probability_max=120, target_unit="u:11:101",
            target_name="target",
        ).replace(
            "slot=a0000000000000000",
            "slot=t00000006FEDCBA9876543210",
        )
        first = self.control.target_action_page(request, {
            "generation": 1,
            "native_revision": request.native_revision,
            "actor_ref": request.native_actor_ref,
            "native_tile": request.native_target_tile,
            "count": 2,
            "rows": (goto, sabotage),
        })
        self.assertEqual(first["page"]["scope"], {
            "actor_id": actor_id,
            "actor_type": "unit",
            "target_id": target_id,
            "target_type": "tile",
        })
        self.assertFalse(first["page"]["catalog_complete"])
        first_action = first["page"]["items"][0]
        with self.assertRaisesRegex(V2ControlError, "action_expired"):
            self.control.resolve_action(
                current, first_action["state_revision"],
                first_action["action_id"], {},
            )
        terminal = self.control.continue_page(
            first["page"]["next_cursor"], endpoint="legal_actions",
        )
        self.assertTrue(terminal["page"]["catalog_complete"])
        rare = terminal["page"]["items"][0]
        self.assertEqual(rare["kind"], "unit.perform_action")
        self.assertEqual(rare["subject"]["operation"], "sabotage_unit")
        self.assertNotIn("cost", rare["subject"])
        self.assertEqual(rare["subject"]["probability"], {
            "kind": "range",
            "minimum_percent": 40.0,
            "maximum_percent": 60.0,
        })
        self.assertTrue(rare["subject"]["target"]["id"].startswith("unit_"))
        public = json.dumps(rare, sort_keys=True)
        self.assertNotIn("Sabotage Unit", public)
        self.assertNotIn("unit.special", public)
        resolution = self.control.resolve_action(
            current, rare["state_revision"], rare["action_id"], {},
        )
        self.assertEqual(
            resolution.native_slot, "t00000006FEDCBA9876543210",
        )
        self.assertEqual(resolution.operation, "sabotage_unit")
        self.assertTrue(resolution.scoped)

    def test_paradrop_enter_conquer_is_one_exact_target_special_lease(self):
        current = observation()
        actor_id = next(
            item["id"] for item in self.control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        target_id = next(
            item["id"] for item in self.control.state_page(
                current, "known_tiles",
            )["page"]["items"] if item["x"] == 2
        )
        request = self.control.prepare_target_action(
            current, actor_id, target_id,
        )
        row = _action(
            0, "unit.special", "u:10:100", 6,
            "Paradrop Unit Enter Conquer", "Tile",
            "Unit Paradrop Conquer", 0,
            target_name="target", legality="unresolved",
            probability_kind="not_implemented",
            probability_min=-1, probability_max=-1,
        ).replace(
            "slot=a0000000000000000",
            "slot=t000000060123456789ABCDEF",
        )
        page = self.control.target_action_page(request, {
            "generation": 1,
            "native_revision": request.native_revision,
            "actor_ref": request.native_actor_ref,
            "native_tile": request.native_target_tile,
            "count": 1,
            "rows": (row,),
        })
        action = page["page"]["items"][0]
        self.assertEqual(action["kind"], "unit.perform_action")
        self.assertEqual(action["subject"]["operation"], "paradrop_conquer")
        self.assertEqual(action["subject"]["actor"]["id"], actor_id)
        self.assertEqual(action["subject"]["target"], {
            "type": "tile", "id": target_id, "x": 2, "y": 2,
        })
        self.assertRegex(
            action["subject"]["variant"], r"^variant_[0-9a-f]{32}$",
        )
        self.assertEqual(action["subject"]["probability"], {
            "kind": "not_implemented",
            "minimum_percent": None,
            "maximum_percent": None,
        })
        resolved = self.control.resolve_action(
            current, action["state_revision"], action["action_id"], {},
        )
        self.assertEqual(
            resolved.native_slot, "t000000060123456789ABCDEF",
        )
        self.assertEqual(resolved.native_actor_ref, "u:10:100")
        self.assertEqual(resolved.operation, "paradrop_conquer")
        self.assertTrue(resolved.scoped)
        self.assertNotIn(
            "Paradrop Unit Enter Conquer", json.dumps(action, sort_keys=True),
        )

    def test_paradrop_enter_conquer_forgery_fails_closed(self):
        base = observation()
        cases = {
            "wrong_native_variant": (
                base, 6, "Paradrop Unit Conquer", "unresolved",
                "not_implemented", -1, -1,
            ),
            "resolved_probability": (
                base, 6, "Paradrop Unit Enter Conquer", "legal",
                "exact", 200, 200,
            ),
            "remembered_target": (
                base, 7, "Paradrop Unit Enter Conquer", "unresolved",
                "not_implemented", -1, -1,
            ),
            "used_paradrop": (
                observation(replace_row(
                    valid_rows(), "paradropped=0", "paradropped=1",
                )),
                6, "Paradrop Unit Enter Conquer", "unresolved",
                "not_implemented", -1, -1,
            ),
        }
        for serial, (label, case) in enumerate(cases.items(), start=1):
            current, tile, native_rule, legality, probability, minimum, maximum = case
            control = V2SeatControl("game_test", f"paradrop_conquer_{serial}", 1)
            actor_id = next(
                item["id"] for item in control.state_page(
                    current, "units",
                )["page"]["items"] if item["scope"] == "own"
            )
            target_id = next(
                item["id"] for item in control.state_page(
                    current, "known_tiles",
                )["page"]["items"]
                if item["x"] == (2 if tile == 6 else 3)
            )
            request = control.prepare_target_action(
                current, actor_id, target_id,
            )
            row = _action(
                0, "unit.special", "u:10:100", tile, native_rule, "Tile",
                "Unit Paradrop Conquer", 0, target_name="target",
                legality=legality, probability_kind=probability,
                probability_min=minimum, probability_max=maximum,
            ).replace(
                "slot=a0000000000000000",
                f"slot=t{tile:08X}0123456789ABCDEF",
            )
            with self.subTest(label=label), self.assertRaisesRegex(
                V2ControlError, "internal_error",
            ):
                control.target_action_page(request, {
                    "generation": 1,
                    "native_revision": request.native_revision,
                    "actor_ref": request.native_actor_ref,
                    "native_tile": request.native_target_tile,
                    "count": 1,
                    "rows": (row,),
                })

    def test_classic_hut_and_extras_variants_are_opaque_and_unresolved(self):
        current = observation()
        actor_id = next(
            item["id"] for item in self.control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        target_id = next(
            item["id"] for item in self.control.state_page(
                current, "known_tiles",
            )["page"]["items"] if item["x"] == 2
        )
        specs = (
            ("Conquer Extras", "Extras", "Unit Conquer Extras",
             "conquer_extras", "exact", "legal", 200, 200),
            ("Conquer Extras 2", "Extras", "Unit Conquer Extras",
             "conquer_extras", "exact", "legal", 200, 200),
            ("Enter Hut", "Tile", "Unit Enter Hut", "enter_hut",
             "not_implemented", "unresolved", -1, -1),
            ("Enter Hut 2", "Tile", "Unit Enter Hut", "enter_hut",
             "not_implemented", "unresolved", -1, -1),
            ("Frighten Hut", "Tile", "Unit Frighten Hut", "frighten_hut",
             "not_implemented", "unresolved", -1, -1),
            ("Frighten Hut 2", "Tile", "Unit Frighten Hut", "frighten_hut",
             "not_implemented", "unresolved", -1, -1),
        )
        rows = []
        for index, (native_rule, target_kind, result, _operation,
                    probability_kind, legality, minimum, maximum) in enumerate(specs):
            row = _action(
                index, "unit.special", "u:10:100", 6,
                native_rule, target_kind, result, 0,
                target_name="target", probability_kind=probability_kind,
                legality=legality, probability_min=minimum,
                probability_max=maximum,
            ).replace(
                f"slot=a{index:016X}",
                f"slot=t00000006{index + 1:016X}",
            )
            rows.append(row)
        request = self.control.prepare_target_action(
            current, actor_id, target_id, limit=16,
        )
        page = self.control.target_action_page(request, {
            "generation": 1,
            "native_revision": request.native_revision,
            "actor_ref": request.native_actor_ref,
            "native_tile": request.native_target_tile,
            "count": len(rows),
            "rows": tuple(rows),
        })
        self.assertTrue(page["page"]["catalog_complete"])
        items = page["page"]["items"]
        self.assertEqual(
            [item["subject"]["operation"] for item in items],
            [spec[3] for spec in specs],
        )
        for item, spec in zip(items, specs, strict=True):
            self.assertEqual(item["subject"]["target"]["type"], "tile")
            if "hut" in spec[3]:
                self.assertEqual(item["subject"]["probability"], {
                    "kind": "not_implemented",
                    "minimum_percent": None,
                    "maximum_percent": None,
                })
        variants = [item["subject"]["variant"] for item in items]
        self.assertEqual(len(variants), len(set(variants)))
        public = json.dumps(items, sort_keys=True)
        for private in (
            "Conquer Extras", "Enter Hut", "Frighten Hut",
            "Unit Conquer Extras", "Unit Enter Hut", "Unit Frighten Hut",
            "unit.special",
        ):
            self.assertNotIn(private, public)

    def test_classic_random_espionage_variants_are_exact_and_unresolved(self):
        rows, _ = economic_unit_rows()
        current = observation(rows)
        actor_id = next(
            item["id"] for item in self.control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        target_id = next(
            item["id"] for item in self.control.state_page(
                current, "known_tiles",
            )["page"]["items"] if item["x"] == 2
        )
        specs = (
            ("Sabotage City", "Unit Sabotage City", "sabotage_city", 1),
            ("Sabotage City Escape", "Unit Sabotage City",
             "sabotage_city", 0),
            ("Sabotage City Production Escape",
             "Unit Sabotage City Production", "sabotage_production", 0),
            ("Steal Tech", "Unit Steal Tech", "steal_technology", 1),
            ("Steal Tech Escape Expected", "Unit Steal Tech",
             "steal_technology", 0),
        )
        espionage_rows = tuple(
            _action(
                index, "unit.special", "u:10:100", 6,
                native_rule, "City", result, consuming,
                destination_city="c:30:300", target_name="target",
                legality="unresolved", probability_kind="not_implemented",
                probability_min=-1, probability_max=-1,
            ).replace(
                f"slot=a{index:016X}",
                f"slot=t00000006{index + 1:016X}",
            )
            for index, (native_rule, result, _operation, consuming)
            in enumerate(specs)
        )
        request = self.control.prepare_target_action(
            current, actor_id, target_id, limit=16,
        )
        page = self.control.target_action_page(request, {
            "generation": 1,
            "native_revision": request.native_revision,
            "actor_ref": request.native_actor_ref,
            "native_tile": request.native_target_tile,
            "count": len(espionage_rows),
            "rows": espionage_rows,
        })
        items = page["page"]["items"]
        self.assertEqual(
            [item["subject"]["operation"] for item in items],
            [spec[2] for spec in specs],
        )
        self.assertEqual(
            len({item["subject"]["variant"] for item in items}),
            len(specs),
        )
        for item in items:
            self.assertEqual(item["subject"]["target"]["type"], "city")
            self.assertEqual(item["subject"]["probability"], {
                "kind": "not_implemented",
                "minimum_percent": None,
                "maximum_percent": None,
            })
        public = json.dumps(items, sort_keys=True)
        for private in (
            "Sabotage City", "Steal Tech", "Unit Sabotage",
            "Unit Steal", "unit.special", "c:30:300",
        ):
            self.assertNotIn(private, public)

        excluded = _action(
            0, "unit.special", "u:10:100", 6,
            "Sabotage City Production", "City",
            "Unit Sabotage City Production", 1,
            destination_city="c:30:300", target_name="target",
            legality="unresolved", probability_kind="not_implemented",
            probability_min=-1, probability_max=-1,
        ).replace(
            "slot=a0000000000000000",
            "slot=t0000000600000000000000FF",
        )
        rejected_control = V2SeatControl(
            "game_test", "agent_espionage_reject", 1,
        )
        rejected_actor_id = next(
            item["id"] for item in rejected_control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        rejected_target_id = next(
            item["id"] for item in rejected_control.state_page(
                current, "known_tiles",
            )["page"]["items"] if item["x"] == 2
        )
        rejected_request = rejected_control.prepare_target_action(
            current, rejected_actor_id, rejected_target_id,
        )
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            rejected_control.target_action_page(rejected_request, {
                "generation": 1,
                "native_revision": rejected_request.native_revision,
                "actor_ref": rejected_request.native_actor_ref,
                "native_tile": rejected_request.native_target_tile,
                "count": 1,
                "rows": (excluded,),
            })

    def test_classic_targeted_technology_theft_projects_bound_choices(self):
        rows, _ = economic_unit_rows()
        current = observation(rows)
        actor_id = next(
            item["id"] for item in self.control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        target_id = next(
            item["id"] for item in self.control.state_page(
                current, "known_tiles",
            )["page"]["items"] if item["x"] == 2
        )
        targeted_rows = tuple(
            _action(
                index, "unit.special", "u:10:100", 6,
                "Targeted Steal Tech Escape Expected", "City",
                "Unit Targeted Steal Tech", 0,
                target_tech=tech, destination_city="c:30:300",
                target_name=name, legality="unresolved",
                probability_kind="not_implemented",
                probability_min=-1, probability_max=-1,
            ).replace(
                f"slot=a{index:016X}",
                f"slot=t00000006{index + 1:016X}",
            )
            for index, (tech, name) in enumerate(((4, "Writing"), (5, "Pottery")))
        )
        request = self.control.prepare_target_action(
            current, actor_id, target_id, limit=16,
        )
        page = self.control.target_action_page(request, {
            "generation": 1,
            "native_revision": request.native_revision,
            "actor_ref": request.native_actor_ref,
            "native_tile": request.native_target_tile,
            "count": len(targeted_rows),
            "rows": targeted_rows,
        })
        items = page["page"]["items"]
        self.assertEqual(
            [item["subject"]["technology_choice"]["name"] for item in items],
            ["Writing", "Pottery"],
        )
        self.assertEqual(
            len({item["subject"]["technology_choice"]["id"] for item in items}),
            2,
        )
        self.assertEqual(len({item["subject"]["variant"] for item in items}), 2)
        for item in items:
            self.assertEqual(item["label"], "Steal selected technology")
            self.assertEqual(item["subject"]["operation"], "steal_technology")
            self.assertEqual(item["subject"]["target"]["type"], "city")
            self.assertEqual(item["subject"]["probability"], {
                "kind": "not_implemented",
                "minimum_percent": None,
                "maximum_percent": None,
            })
            resolved = self.control.resolve_action(
                current, item["state_revision"], item["action_id"], {},
            )
            self.assertEqual(resolved.operation, "steal_technology")
            self.assertTrue(resolved.scoped)
        public = json.dumps(items, sort_keys=True)
        for private in (
            "Targeted Steal Tech Escape Expected",
            "Unit Targeted Steal Tech", "unit.special", "c:30:300",
        ):
            self.assertNotIn(private, public)

        excluded = _action(
            0, "unit.special", "u:10:100", 6,
            "Targeted Steal Tech", "City", "Unit Targeted Steal Tech", 1,
            target_tech=4, destination_city="c:30:300", target_name="Writing",
            legality="unresolved", probability_kind="not_implemented",
            probability_min=-1, probability_max=-1,
        ).replace(
            "slot=a0000000000000000", "slot=t0000000600000000000000FF",
        )
        rejected = V2SeatControl("game_test", "agent_targeted_reject", 1)
        rejected_actor = next(
            item["id"] for item in rejected.state_page(current, "units")
            ["page"]["items"] if item["scope"] == "own"
        )
        rejected_target = next(
            item["id"] for item in rejected.state_page(current, "known_tiles")
            ["page"]["items"] if item["x"] == 2
        )
        rejected_request = rejected.prepare_target_action(
            current, rejected_actor, rejected_target,
        )
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            rejected.target_action_page(rejected_request, {
                "generation": 1,
                "native_revision": rejected_request.native_revision,
                "actor_ref": rejected_request.native_actor_ref,
                "native_tile": rejected_request.native_target_tile,
                "count": 1,
                "rows": (excluded,),
            })

    def test_classic_targeted_building_sabotage_projects_bound_choices(self):
        rows, _ = economic_unit_rows()
        current = observation(rows)
        actor_id = next(
            item["id"] for item in self.control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        target_id = next(
            item["id"] for item in self.control.state_page(
                current, "known_tiles",
            )["page"]["items"] if item["x"] == 2
        )
        specs = (
            ("Targeted Sabotage City", 1, 7, "Granary"),
            ("Targeted Sabotage City", 1, 8, "Marketplace"),
            ("Targeted Sabotage City Escape", 0, 7, "Granary"),
            ("Targeted Sabotage City Escape", 0, 8, "Marketplace"),
            ("Targeted Sabotage City Escape", 0, 9, "target"),
        )
        targeted_rows = tuple(
            _action(
                index, "unit.special", "u:10:100", 6,
                native_rule, "City", "Unit Targeted Sabotage City",
                consuming,
                target_build_kind="improvement", target_build=building,
                destination_city="c:30:300", target_name=name,
                legality="unresolved", probability_kind="not_implemented",
                probability_min=-1, probability_max=-1,
            ).replace(
                f"slot=a{index:016X}",
                f"slot=t00000006{index + 1:016X}",
            )
            for index, (native_rule, consuming, building, name)
            in enumerate(specs)
        )
        request = self.control.prepare_target_action(
            current, actor_id, target_id, limit=16,
        )
        page = self.control.target_action_page(request, {
            "generation": 1,
            "native_revision": request.native_revision,
            "actor_ref": request.native_actor_ref,
            "native_tile": request.native_target_tile,
            "count": len(targeted_rows),
            "rows": targeted_rows,
        })
        items = page["page"]["items"]
        self.assertEqual(
            [item["subject"]["building_choice"]["name"] for item in items],
            [spec[3] for spec in specs],
        )
        self.assertEqual(
            len({item["subject"]["building_choice"]["id"] for item in items}),
            len(items),
        )
        self.assertEqual(
            len({item["subject"]["variant"] for item in items}), len(items),
        )
        for item in items:
            self.assertEqual(item["label"], "Sabotage selected city improvement")
            self.assertEqual(item["subject"]["operation"], "sabotage_building")
            self.assertEqual(item["subject"]["target"]["type"], "city")
            self.assertNotIn("gold_cost", item["subject"])
            self.assertEqual(item["arguments_schema"], {
                "type": "object",
                "properties": {},
                "required": [],
                "additionalProperties": False,
            })
            choice_id = item["subject"]["building_choice"]["id"]
            self.assertNotIn(choice_id, {"7", "8"})
            resolved = self.control.resolve_action(
                current, item["state_revision"], item["action_id"], {},
            )
            self.assertEqual(resolved.operation, "sabotage_building")
            self.assertEqual(resolved.native_arguments, "-")
        public = json.dumps(items, sort_keys=True)
        for private in (
            "Targeted Sabotage City", "Unit Targeted Sabotage City",
            "unit.special", "c:30:300", "target_build",
        ):
            self.assertNotIn(private, public)

    def test_target_catalog_rejects_cost_subtarget_cross_kind_and_sentinels(self):
        rows, _ = economic_unit_rows()
        current = observation(rows)
        actor_id = next(
            item["id"] for item in self.control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        target_id = next(
            item["id"] for item in self.control.state_page(
                current, "known_tiles",
            )["page"]["items"] if item["x"] == 2
        )
        invalid_rows = (
            _action(
                0, "unit.special", "u:10:100", 6,
                "Sabotage Unit", "Unit", "Unit Sabotage Unit", 0,
                target_unit="u:11:101", destination_city="c:20:200",
                target_name="target",
            ),
            _action(
                0, "unit.special", "u:10:100", 6,
                "Steal Tech", "City", "Unit Steal Tech", 0,
                target_tech=1, destination_city="c:20:200",
                target_name="target",
            ),
            _action(
                0, "unit.special", "u:10:100", 6,
                "Incite City", "City", "Unit Incite City", 0,
                gold_cost=41, destination_city="c:20:200",
                target_name="target",
            ),
            _action(
                0, "unit.special", "u:10:100", 6,
                "Targeted Sabotage City Escape", "City",
                "Unit Targeted Sabotage City", 0,
                destination_city="c:30:300", target_name="target",
                legality="unresolved", probability_kind="not_implemented",
                probability_min=-1, probability_max=-1,
            ),
            _action(
                0, "unit.special", "u:10:100", 6,
                "Targeted Sabotage City Escape", "City",
                "Unit Targeted Sabotage City", 0,
                target_build_kind="unit", target_build=7,
                destination_city="c:30:300", target_name="Granary",
                legality="unresolved", probability_kind="not_implemented",
                probability_min=-1, probability_max=-1,
            ),
            _action(
                0, "unit.special", "u:10:100", 6,
                "Targeted Sabotage City Escape", "City",
                "Unit Targeted Sabotage City", 0,
                target_tech=4,
                target_build_kind="improvement", target_build=7,
                destination_city="c:30:300", target_name="Granary",
                legality="unresolved", probability_kind="not_implemented",
                probability_min=-1, probability_max=-1,
            ),
            _action(
                0, "unit.special", "u:10:100", 6,
                "Sabotage City Escape", "City", "Unit Sabotage City", 0,
                target_build_kind="improvement", target_build=7,
                destination_city="c:30:300", target_name="Granary",
                legality="unresolved", probability_kind="not_implemented",
                probability_min=-1, probability_max=-1,
            ),
            _action(
                0, "unit.special", "u:10:100", 6,
                "Targeted Sabotage City Escape", "City",
                "Unit Targeted Sabotage City", 0,
                target_build_kind="improvement", target_build=7,
                gold_cost=1, destination_city="c:30:300",
                target_name="Granary", legality="unresolved",
                probability_kind="not_implemented",
                probability_min=-1, probability_max=-1,
            ),
        )
        for index, row in enumerate(invalid_rows):
            row = row.replace(
                "slot=a0000000000000000",
                f"slot=t00000006{index + 1:016X}",
            )
            request = self.control.prepare_target_action(
                current, actor_id, target_id,
            )
            with self.assertRaisesRegex(V2ControlError, "internal_error"):
                self.control.target_action_page(request, {
                    "generation": 1,
                    "native_revision": request.native_revision,
                    "actor_ref": request.native_actor_ref,
                    "native_tile": request.native_target_tile,
                    "count": 1,
                    "rows": (row,),
                })

    def test_paid_espionage_projects_frozen_quoted_maximums(self):
        rows = tuple(sorted(valid_rows() + (
            "city_site ref=c:30:300 owner=p:2:20 name=Beta tile=6 x=2 y=2 "
            "size=3 visibility=visible",
        )))
        current = observation(rows)
        actor_id = next(
            item["id"] for item in self.control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        target_id = next(
            item["id"] for item in self.control.state_page(
                current, "known_tiles",
            )["page"]["items"] if item["x"] == 2
        )
        private_rows = (
            _action(
                0, "unit.special", "u:10:100", 6,
                "Bribe Unit", "Unit", "Unit Bribe Unit", 0,
                gold_cost=17, target_unit="u:11:101", target_name="target",
            ),
            _action(
                1, "unit.special", "u:10:100", 6,
                "Bribe Stack", "Stack", "Unit Bribe Stack", 0,
                gold_cost=19, target_name="target",
            ),
            _action(
                2, "unit.special", "u:10:100", 6,
                "Incite City", "City", "Unit Incite City", 1,
                gold_cost=23, destination_city="c:30:300",
                target_name="target", legality="unresolved",
                probability_kind="not_implemented",
                probability_min=-1, probability_max=-1,
            ),
            _action(
                3, "unit.special", "u:10:100", 6,
                "Incite City Escape", "City", "Unit Incite City", 0,
                gold_cost=29, destination_city="c:30:300",
                target_name="target", legality="unresolved",
                probability_kind="not_implemented",
                probability_min=-1, probability_max=-1,
            ),
        )
        private_rows = tuple(
            row.replace(
                f"slot=a{index:016X}",
                f"slot=t00000006{index + 1:016X}",
            )
            for index, row in enumerate(private_rows)
        )
        request = self.control.prepare_target_action(
            current, actor_id, target_id, limit=16,
        )
        page = self.control.target_action_page(request, {
            "generation": 1,
            "native_revision": request.native_revision,
            "actor_ref": request.native_actor_ref,
            "native_tile": request.native_target_tile,
            "count": 4,
            "rows": private_rows,
        })
        items = page["page"]["items"]
        self.assertEqual(
            [(item["subject"]["operation"], item["subject"]["gold_cost"])
             for item in items],
            [("bribe_unit", 17), ("bribe_stack", 19),
             ("incite_city", 23),
             ("incite_city", 29)],
        )
        stack = next(
            item for item in items
            if item["subject"]["operation"] == "bribe_stack"
        )
        self.assertEqual(stack["subject"]["target"], {
            "type": "unit_stack", "id": target_id, "x": 2, "y": 2,
        })
        for item in items:
            resolved = self.control.resolve_action(
                current, item["state_revision"], item["action_id"], {},
            )
            self.assertEqual(resolved.operation,
                             item["subject"]["operation"])
            self.assertTrue(resolved.scoped)

        known_id = next(
            item["id"] for item in self.control.state_page(
                current, "known_tiles",
            )["page"]["items"] if item["visibility"] != "unknown"
        )
        for wrong_actor, wrong_target in (
            (known_id, known_id),
            (actor_id, actor_id),
            ("unit_" + "g" * 32, known_id),
            (actor_id, "tile_" + "g" * 32),
        ):
            with self.assertRaisesRegex(V2ControlError, "invalid_request"):
                self.control.prepare_target_action(
                    current, wrong_actor, wrong_target,
                )

    def test_investigate_city_variants_are_target_scoped(self):
        rows = tuple(sorted(valid_rows() + (
            "city_site ref=c:30:300 owner=p:2:20 name=Beta tile=6 x=2 y=2 "
            "size=3 visibility=visible",
        )))
        current = observation(rows)
        actor_id = next(
            item["id"] for item in self.control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        target_id = next(
            item["id"] for item in self.control.state_page(
                current, "known_tiles",
            )["page"]["items"] if item["x"] == 2
        )
        private_rows = tuple(
            _action(
                index, "unit.special", "u:10:100", 6,
                rule, "City", "Unit Investigate City", consuming,
                destination_city="c:30:300", target_name="target",
            ).replace(
                f"slot=a{index:016X}",
                f"slot=t00000006{index + 1:016X}",
            )
            for index, rule, consuming in (
                (0, "Investigate City", 0),
                (1, "Investigate City Escape", 1),
            )
        )
        request = self.control.prepare_target_action(
            current, actor_id, target_id,
        )
        page = self.control.target_action_page(request, {
            "generation": 1,
            "native_revision": request.native_revision,
            "actor_ref": request.native_actor_ref,
            "native_tile": request.native_target_tile,
            "count": 2,
            "rows": private_rows,
        })
        self.assertEqual(len(page["page"]["items"]), 2)
        self.assertEqual(
            {item["subject"]["operation"] for item in page["page"]["items"]},
            {"investigate_city"},
        )
        for item in page["page"]["items"]:
            resolution = self.control.resolve_action(
                current, item["state_revision"], item["action_id"], {},
            )
            self.assertEqual(resolution.operation, "investigate_city")
            self.assertTrue(resolution.scoped)
        public = json.dumps(page, sort_keys=True)
        self.assertNotIn("Investigate City Escape", public)
        self.assertNotIn("c:30:300", public)

    def test_investigation_scope_projects_only_bounded_city_info_capture(self):
        rows = tuple(sorted(valid_rows() + (
            "city_site ref=c:30:300 owner=p:2:20 name=Beta tile=6 x=2 y=2 "
            "size=3 visibility=visible",
        )))
        current = observation(rows, revision=12)
        self.control.state_page(current)
        request = self.control.prepare_investigation_scope(
            current, "i0123456789abcdef",
        )
        native_rows = tuple((
            "investigation city=c:30:300 lifecycle=77 tile=6 name=Beta "
            "size=3 production_kind=unit production_id=12 "
            "production_name=Settlers shield_stock=19 shield_surplus=4 "
            "improvement_count=1 feeling_count=6 specialist_count=1",
            "investigation_improvement city=c:30:300 improvement_id=5 "
            "name=Granary",
            *(
                "investigation_citizens city=c:30:300 "
                f"stage={stage} happy=1 content=1 unhappy=0 angry=0"
                for stage in range(6)
            ),
            "investigation_specialist city=c:30:300 specialist=0 "
            "name=Entertainer count=1",
        ))
        projected = self.control.project_investigation_observation(
            current, request, {
                "generation": 1,
                "native_revision": 12,
                "section": "investigation",
                "selector": "i0123456789abcdef",
                "view_id": "q12-1",
                "offset": 0,
                "count": len(native_rows),
                "total_count": len(native_rows),
                "next_offset": len(native_rows),
                "complete": True,
                "overflow": False,
                "rows": native_rows,
            },
        )
        self.assertEqual(projected["type"], "city_investigation")
        self.assertEqual(projected["source"], "human_client_city_info")
        self.assertEqual(
            projected["freshness"], "captured_at_receipt_revision",
        )
        self.assertEqual(projected["city"]["name"], "Beta")
        self.assertEqual(projected["city"]["production"], {
            "id": self.control._production_id("unit", 12),
            "kind": "unit", "name": "Settlers",
        })
        self.assertEqual(projected["city"]["shields"], {
            "stock": 19, "surplus": 4,
        })
        self.assertEqual(
            [item["stage"] for item in projected["city"]["citizens"]["feelings"]],
            list(v2_control.INVESTIGATION_FEELING_STAGES),
        )
        public = json.dumps(projected, sort_keys=True)
        for private in (
            "c:30:300", "lifecycle", "tile", "i0123456789abcdef",
            "route", "worklist", "rally",
        ):
            self.assertNotIn(private, public)

        stale = observation(rows, revision=13)
        self.control.state_page(stale)
        with self.assertRaisesRegex(V2ControlError, "stale_revision"):
            self.control.project_investigation_observation(
                current, request, {},
            )

    def test_stale_target_cursor_never_publishes_pending_bindings(self):
        control = V2SeatControl("game_test", "agent_stale_target", 1)
        current = observation(revision=11)
        actor_id = next(
            item["id"] for item in control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        target_id = next(
            item["id"] for item in control.state_page(
                current, "known_tiles",
            )["page"]["items"] if item["x"] == 2
        )
        request = control.prepare_target_action(
            current, actor_id, target_id, limit=1,
        )
        rows = tuple(
            _action(
                index, "unit.special", "u:10:100", 6,
                "Sabotage Unit", "Unit", "Unit Sabotage Unit", 0,
                target_unit="u:11:101", target_name="target",
            ).replace(
                f"slot=a{index:016X}",
                f"slot=t00000006{index:016X}",
            )
            for index in (1, 2)
        )
        first = control.target_action_page(request, {
            "generation": 1,
            "native_revision": 11,
            "actor_ref": "u:10:100",
            "native_tile": 6,
            "count": 2,
            "rows": rows,
        })
        self.assertEqual(control._scoped_action_bindings, {})
        control.state_page(observation(revision=12))
        with self.assertRaisesRegex(V2ControlError, "stale_revision"):
            control.continue_page(
                first["page"]["next_cursor"], endpoint="legal_actions",
            )
        self.assertEqual(control._scoped_action_bindings, {})

    def test_city_worker_tasks_are_opaque_and_actions_are_enumerated(self):
        rows = tuple(sorted((*valid_rows(), (
            "city_worker_task city=c:20:200 tile=5 activity=road "
            "target_extra=7 target_extra_name=Road want=80"
        ))))
        current = observation(rows)
        city = self.control.state_page(
            current, "cities",
        )["page"]["items"][0]
        tasks = self.control.state_page(
            current, "city_worker_tasks", actor_id=city["id"],
        )["page"]["items"]
        self.assertEqual(len(tasks), 1)
        self.assertRegex(tasks[0]["id"], r"^city_worker_task_[0-9a-f]{32}$")
        self.assertRegex(tasks[0]["tile_id"], r"^tile_[0-9a-f]{32}$")
        self.assertEqual(tasks[0]["priority"], 80)
        self.assertEqual(tasks[0]["activity"]["name"], "road")
        self.assertRegex(
            tasks[0]["activity"]["target_extra"]["id"],
            r"^extra_[0-9a-f]{32}$",
        )
        self.assertNotIn("native", json.dumps(tasks, sort_keys=True))

        change = _action(
            140, "city.change_worker_task", "c:20:200", 5,
            "city.change_worker_task", "City Worker Task",
            "Worker Task Changed", 0, activity="cultivate",
            target_name="cultivate",
        )
        remove = _action(
            141, "city.remove_worker_task", "c:20:200", 5,
            "city.remove_worker_task", "City Worker Task",
            "Worker Task Removed", 0, target_name="standing task",
        )
        request = self.control.prepare_actor_scope(current, city["id"], 16)
        page = self.control.actor_scope_page(
            request,
            self.scope_page(
                request, [*scoped_city_rows(), change, remove],
                view="v11-198",
            ),
        )
        worker_actions = [
            item for item in page["page"]["items"]
            if item["kind"] == "city.manage_worker_task"
        ]
        self.assertEqual(
            {item["subject"]["operation"] for item in worker_actions},
            {"change_worker_task", "remove_worker_task"},
        )
        for descriptor in worker_actions:
            target = descriptor["subject"]["target"]
            self.assertEqual(target["id"], tasks[0]["id"])
            self.assertRegex(target["tile"]["id"], r"^tile_[0-9a-f]{32}$")
            self.assertEqual(
                descriptor["arguments_schema"]["properties"], {},
            )
            resolved = self.control.resolve_action(
                current, descriptor["state_revision"],
                descriptor["action_id"], {},
            )
            self.assertEqual(resolved.native_arguments, "-")

        empty_rows = tuple(
            row for row in valid_rows()
            if not row.startswith("city_worker_task ")
        )
        empty = observation(empty_rows, revision=12)
        empty_city = self.control.state_page(
            empty, "cities",
        )["page"]["items"][0]
        requested = _action(
            142, "city.request_worker_task", "c:20:200", 5,
            "city.request_worker_task", "City Worker Task",
            "Worker Task Requested", 0, target_extra=7, activity="road",
            target_name="Road",
        )
        request = self.control.prepare_actor_scope(empty, empty_city["id"], 16)
        page = self.control.actor_scope_page(
            request,
            self.scope_page(
                request, [*scoped_city_rows(), requested], view="v12-198",
            ),
        )
        descriptor = next(
            item for item in page["page"]["items"]
            if item["kind"] == "city.manage_worker_task"
        )
        self.assertEqual(
            descriptor["subject"]["operation"], "request_worker_task",
        )
        self.assertEqual(
            descriptor["subject"]["target"]["desired"]
                ["target_extra"]["name"],
            "Road",
        )

    def test_active_city_rally_projects_plan_and_clear_capability(self):
        rows = replace_row(
            valid_rows(),
            "city_rally city=c:20:200 active=0 persistent=0 vigilant=0 "
            "order_count=0 orders_digest=fnv1a64-0000000000000000",
            "city_rally city=c:20:200 active=1 persistent=1 vigilant=0 "
            "order_count=3 orders_digest=fnv1a64-0123456789abcdef",
        )
        current = observation(rows)
        city = self.control.state_page(
            current, "cities",
        )["page"]["items"][0]
        detail = self.control.state_page(
            current, "city_detail", actor_id=city["id"],
        )["page"]["items"][0]
        rally = detail["management"]["rally"]
        self.assertEqual(
            {key: rally[key] for key in (
                "active", "persistent", "vigilant", "order_count",
            )},
            {
                "active": True, "persistent": True,
                "vigilant": False, "order_count": 3,
            },
        )
        self.assertRegex(rally["plan_id"], r"^rally_[0-9a-f]{32}$")
        request = self.control.prepare_actor_scope(current, city["id"], 16)
        clear = _action(
            107, "city.clear_rally", "c:20:200", -1,
            "city.clear_rally", "City", "Rally Point Cleared", 0,
            target_name="rally",
        )
        page = self.control.actor_scope_page(
            request,
            self.scope_page(
                request, [*scoped_city_rows(), clear], view="v11-199",
            ),
        )
        descriptor = next(
            item for item in page["page"]["items"]
            if item["subject"]["operation"] == "clear_rally"
        )
        self.assertEqual(descriptor["kind"], "city.set_rally")
        self.assertEqual(descriptor["subject"]["target"], {
            "type": "city", "id": city["id"],
        })
        resolved = self.control.resolve_action(
            current, descriptor["state_revision"], descriptor["action_id"], {},
        )
        self.assertEqual(resolved.operation, "clear_rally")
        self.assertEqual(resolved.native_arguments, "-")
        self.assertTrue(resolved.scoped)
        public = json.dumps(detail, sort_keys=True)
        self.assertNotIn("0123456789abcdef", public)
        self.assertNotIn("orders_digest", public)

    def test_city_rally_order_count_bound_matches_native_route_limit(self):
        inactive = (
            "city_rally city=c:20:200 active=0 persistent=0 vigilant=0 "
            "order_count=0 orders_digest=fnv1a64-0000000000000000"
        )
        accepted = replace_row(
            valid_rows(), inactive,
            "city_rally city=c:20:200 active=1 persistent=0 vigilant=0 "
            "order_count=1999 orders_digest=fnv1a64-0123456789abcdef",
        )
        accepted_observation = observation(accepted)
        city_id = self.control.state_page(
            accepted_observation, "cities",
        )["page"]["items"][0]["id"]
        rally = self.control.state_page(
            accepted_observation, "city_detail", actor_id=city_id,
        )["page"]["items"][0]["management"]["rally"]
        self.assertEqual(rally["order_count"], 1999)

        rejected = replace_row(
            valid_rows(), inactive,
            "city_rally city=c:20:200 active=1 persistent=0 vigilant=0 "
            "order_count=2000 orders_digest=fnv1a64-0123456789abcdef",
        )
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            V2SeatControl(
                "game_test", "rally_limit_rejected", 1,
            ).state_page(observation(rejected), "cities")

    def test_target_action_rejects_unknown_and_cross_seat_tile_ids(self):
        rows = rows_with_unknown_moves()
        control = V2SeatControl("game_test", "agent_unknown", 1)
        current = observation(rows)
        units = control.state_page(current, "units")["page"]["items"]
        actor_id = next(item["id"] for item in units if item["scope"] == "own")
        tiles = control.state_page(
            current, "known_tiles",
        )["page"]["items"]
        unknown_id = next(
            item["id"] for item in tiles if item["visibility"] == "unknown"
        )
        with self.assertRaises(V2ControlError) as unknown:
            control.prepare_target_action(current, actor_id, unknown_id)
        self.assertEqual(unknown.exception.code, "invalid_request")

        other = V2SeatControl("game_test", "agent_other", 1)
        foreign_target = other.state_page(
            observation(), "known_tiles",
        )["page"]["items"][0]["id"]
        with self.assertRaises(V2ControlError) as foreign:
            control.prepare_target_action(current, actor_id, foreign_target)
        self.assertEqual(foreign.exception.code, "invalid_request")

    def test_city_management_state_actions_and_arguments_are_exact(self):
        rows, scoped = city_management_control_rows()
        current = observation(rows)
        city = self.control.state_page(
            current, "cities",
        )["page"]["items"][0]
        management = self.control.state_page(
            current, "city_detail", actor_id=city["id"],
        )["page"]["items"][0]["management"]
        worklist_items = self.control.state_page(
            current, "city_worklist", actor_id=city["id"],
        )["page"]["items"]
        build_choices = self.control.state_page(
            current, "city_build_choices", actor_id=city["id"],
        )["page"]["items"]
        improvements = self.control.state_page(
            current, "city_improvements", actor_id=city["id"],
        )["page"]["items"]
        self.assertEqual(
            [item["name"] for item in worklist_items],
            ["Temple", "Temple"],
        )
        self.assertEqual(
            [item["production_id"] for item in worklist_items],
            [build_choices[1]["id"]] * 2,
        )
        stale_choice = next(
            item for item in build_choices
            if item["name"] == "Temple"
        )
        queue_choice = next(
            item for item in build_choices
            if item["name"] == "Granary"
        )
        self.assertFalse(stale_choice["can_queue"])
        self.assertFalse(stale_choice["can_build_now"])
        self.assertEqual(stale_choice["preservable_count"], 2)
        self.assertTrue(queue_choice["can_queue"])
        self.assertEqual(queue_choice["preservable_count"], 0)
        self.assertEqual(management["options"], {
            "allow_disband": False,
            "new_citizens": "science",
            "conflict": True,
        })
        self.assertEqual(improvements, [
            {
                "city_id": city["id"],
                "id": queue_choice["id"], "name": "Granary",
                "sellable": True, "sell_price": 20,
            },
            {
                "city_id": city["id"],
                "id": stale_choice["id"], "name": "Temple",
                "sellable": False, "sell_price": 10,
            },
        ])

        request = self.control.prepare_actor_scope(current, city["id"], 16)
        payload = self.control.actor_scope_page(
            request,
            self.scope_page(request, scoped, view="v11-180"),
        )
        actions = payload["page"]["items"]
        self.assertEqual(
            {item["subject"]["operation"] for item in actions},
            {
                "set_production", "buy_production", "set_worklist",
                "set_options", "rename", "sell_improvement",
                "set_governor",
            },
        )
        for action in actions:
            validate_legal_action_descriptor(action)
        serialized = json.dumps(
            [city, payload], sort_keys=True, separators=(",", ":"),
        )
        for private in (
            "c:20:200", "a000000000000006A", "native_", "slot=",
            "production_id=5", "improvement_id=5",
        ):
            self.assertNotIn(private, serialized)

        by_operation = {
            item["subject"]["operation"]: item for item in actions
        }
        worklist = by_operation["set_worklist"]
        self.assertTrue(
            worklist["arguments_schema"]["properties"]["items"]
            ["metadata"]["duplicates_allowed"]
        )
        self.assertEqual(
            worklist["arguments_schema"]["properties"]["items"]
            ["metadata"]["eligibility"],
            "can_queue or occurrence does not exceed preservable_count",
        )
        resolved_worklist = self.control.resolve_action(
            current, worklist["state_revision"], worklist["action_id"],
            {"items": [queue_choice["id"], queue_choice["id"]]},
        )
        self.assertEqual(
            resolved_worklist.native_arguments,
            "worklist=improvement:5,improvement:5",
        )
        boundary_worklist = self.control.resolve_action(
            current, worklist["state_revision"], worklist["action_id"],
            {"items": [queue_choice["id"]] * 64},
        )
        self.assertEqual(
            boundary_worklist.native_arguments,
            "worklist=" + ",".join(["improvement:5"] * 64),
        )
        self.assertEqual(
            len(boundary_worklist.native_arguments.encode("ascii")), 904,
        )
        preserved = self.control.resolve_action(
            current, worklist["state_revision"], worklist["action_id"],
            {
                "items": [
                    stale_choice["id"], queue_choice["id"],
                    stale_choice["id"],
                ],
            },
        )
        self.assertEqual(
            preserved.native_arguments,
            "worklist=improvement:7,improvement:5,improvement:7",
        )
        for invalid in (
            {"items": [stale_choice["id"]] * 3},
            {"items": [stale_choice["id"], stale_choice["id"]]},
            {"items": ["production_" + "0" * 32]},
            {"items": [queue_choice["id"]] * 65},
            {"items": [7]},
            {"items": "not-a-list"},
            {},
        ):
            with self.subTest(worklist=invalid), self.assertRaisesRegex(
                V2ControlError, "invalid_request",
            ):
                self.control.resolve_action(
                    current, worklist["state_revision"],
                    worklist["action_id"], invalid,
                )

        options = by_operation["set_options"]
        repaired = self.control.resolve_action(
            current, options["state_revision"], options["action_id"],
            {"allow_disband": False, "new_citizens": "science"},
        )
        self.assertEqual(
            repaired.native_arguments,
            "allow_disband=0,new_citizens=science",
        )
        for invalid_options in (
            {"allow_disband": False, "new_citizens": []},
            {"allow_disband": False, "new_citizens": {}},
            {"allow_disband": False, "new_citizens": 1},
            {"allow_disband": 0, "new_citizens": "science"},
        ):
            with self.subTest(options=invalid_options), self.assertRaisesRegex(
                V2ControlError, "invalid_request",
            ):
                self.control.resolve_action(
                    current, options["state_revision"],
                    options["action_id"], invalid_options,
                )
        renamed = self.control.resolve_action(
            current,
            by_operation["rename"]["state_revision"],
            by_operation["rename"]["action_id"],
            {"city_name": "Cité %= 2"},
        )
        self.assertEqual(renamed.native_arguments, "city_name=Cité %= 2")
        sold = self.control.resolve_action(
            current,
            by_operation["sell_improvement"]["state_revision"],
            by_operation["sell_improvement"]["action_id"], {},
        )
        self.assertEqual(sold.native_arguments, "-")
        self.assertEqual(
            by_operation["sell_improvement"]["subject"]["target"],
            {
                "type": "improvement", "id": queue_choice["id"],
                "name": "Granary", "sell_price": 20,
            },
        )

    def test_city_management_rejects_noops_and_incomplete_capability_sets(self):
        current = observation()
        city = self.control.state_page(
            current, "cities",
        )["page"]["items"][0]
        request = self.control.prepare_actor_scope(current, city["id"], 16)
        payload = self.control.actor_scope_page(
            request,
            self.scope_page(request, scoped_city_rows(), view="v11-181"),
        )
        actions = {
            item["subject"]["operation"]: item
            for item in payload["page"]["items"]
        }
        noop_arguments = {
            "set_worklist": {"items": []},
            "set_options": {
                "allow_disband": False, "new_citizens": "default",
            },
            "rename": {"city_name": "Alpha Centauri"},
        }
        for operation, arguments in noop_arguments.items():
            action = actions[operation]
            with self.subTest(operation=operation), self.assertRaisesRegex(
                V2ControlError, "invalid_request",
            ):
                self.control.resolve_action(
                    current, action["state_revision"], action["action_id"],
                    arguments,
                )

        management_rows, scoped = city_management_control_rows()
        for serial, (label, forged_rows) in enumerate((
            ("omitted_sell", scoped[:-1]),
            (
                "nonsellable_substitution",
                (*scoped[:-1], _action(
                    107, "city.sell_improvement", "c:20:200", -1,
                    "city.sell_improvement", "Improvement",
                    "Improvement Sold", 0,
                    target_build_kind="improvement", target_build=7,
                    target_name="Temple",
                )),
            ),
            ("omitted_options", tuple(
                row for row in scoped
                if " kind=city.set_options " not in row
            )),
        ), start=182):
            control = V2SeatControl(
                "game_test", f"management_scope_{label}", 1,
            )
            scoped_current = observation(management_rows)
            scoped_city = control.state_page(
                scoped_current, "cities",
            )["page"]["items"][0]
            scoped_request = control.prepare_actor_scope(
                scoped_current, scoped_city["id"], 16,
            )
            with self.subTest(label=label), self.assertRaisesRegex(
                V2ControlError, "internal_error",
            ):
                control.actor_scope_page(
                    scoped_request,
                    self.scope_page(
                        scoped_request, forged_rows,
                        view=f"v11-{serial}",
                    ),
                )

        no_worklist_rows = tuple(
            row.replace("build_choice_count=2", "build_choice_count=0")
            if row.startswith("city ref=c:20:200 ") else row
            for row in valid_rows()
            if not row.startswith("city_build_choice ")
        )
        no_worklist_control = V2SeatControl(
            "game_test", "no_worklist_invocation", 1,
        )
        no_worklist_current = observation(no_worklist_rows)
        no_worklist_city = no_worklist_control.state_page(
            no_worklist_current, "cities",
        )["page"]["items"][0]
        self.assertEqual(no_worklist_control.state_page(
            no_worklist_current, "city_build_choices",
            actor_id=no_worklist_city["id"],
        )["page"]["items"], [])
        no_worklist_request = no_worklist_control.prepare_actor_scope(
            no_worklist_current, no_worklist_city["id"], 16,
        )
        no_worklist_scoped = tuple(
            row for row in scoped_city_rows()
            if " kind=city.set_production " not in row
            and " kind=city.set_worklist " not in row
        )
        no_worklist_payload = no_worklist_control.actor_scope_page(
            no_worklist_request,
            self.scope_page(
                no_worklist_request, no_worklist_scoped, view="v11-185",
            ),
        )
        self.assertEqual(
            {
                item["subject"]["operation"]
                for item in no_worklist_payload["page"]["items"]
            },
            {"buy_production", "set_options", "rename", "set_governor"},
        )

    def test_city_management_rows_fail_closed_on_malformed_native_truth(self):
        rows, _ = city_management_control_rows()
        city_row = next(row for row in rows if row.startswith("city ref="))
        stale_choice = next(
            row for row in rows
            if row.startswith("city_build_choice ")
            and "production_id=7 " in row
        )
        worklist_zero = next(
            row for row in rows
            if row.startswith("city_worklist ") and "position=0 " in row
        )
        sellable = next(
            row for row in rows
            if row.startswith("city_improvement ") and "improvement_id=5 " in row
        )
        malformed = {
            "missing_catalog_union_member": tuple(
                row for row in rows if row != stale_choice
            ),
            "duplicate_catalog_identity": (*rows, stale_choice),
            "worklist_position_gap": tuple(
                row.replace("position=0 ", "position=2 ")
                if row == worklist_zero else row for row in rows
            ),
            "worklist_count_mismatch": tuple(
                row.replace("worklist_length=2", "worklist_length=1")
                if row == city_row else row for row in rows
            ),
            "build_count_mismatch": tuple(
                row.replace("build_choice_count=3", "build_choice_count=2")
                if row == city_row else row for row in rows
            ),
            "improvement_count_mismatch": tuple(
                row.replace("improvement_count=2", "improvement_count=1")
                if row == city_row else row for row in rows
            ),
            "production_name_conflict": tuple(
                row.replace("production_name=Settlers", "production_name=Spy")
                if row.startswith("city_build_choice ")
                and "production_id=12 " in row else row for row in rows
            ),
            "improvement_name_conflict": tuple(
                row.replace("name=Granary ", "name=Storehouse ")
                if row == sellable else row for row in rows
            ),
            "zero_sell_price": tuple(
                row.replace("sell_price=20", "sell_price=0")
                if row == sellable else row for row in rows
            ),
            "sold_city_still_sellable": tuple(
                row.replace("did_sell=0", "did_sell=1")
                if row == city_row else row for row in rows
            ),
            "conflict_not_science_precedence": tuple(
                row.replace("new_citizens=science", "new_citizens=gold")
                if row == city_row else row for row in rows
            ),
            "build_now_without_queue": tuple(
                row.replace("can_queue=0 can_build_now=0", "can_queue=0 can_build_now=1")
                if row == stale_choice else row for row in rows
            ),
            "stale_choice_not_in_current_worklist": tuple(
                row.replace("build_choice_count=3", "build_choice_count=4")
                if row == city_row else row for row in rows
            ) + (
                "city_build_choice city=c:20:200 "
                "production_kind=improvement production_id=8 "
                "production_name=Pyramids can_queue=0 can_build_now=0",
            ),
        }
        for label, malformed_rows in malformed.items():
            with self.subTest(label=label), self.assertRaisesRegex(
                V2ControlError, "internal_error",
            ):
                V2SeatControl(
                    "game_test", f"malformed_management_{label}", 1,
                ).state_page(observation(tuple(malformed_rows)))

    def test_city_citizen_state_and_capabilities_are_exact_and_complete(self):
        rows, scoped = citizen_control_rows()
        control = V2SeatControl("game_test", "citizen_agent", 1)
        current = observation(rows)
        city = control.state_page(current, "cities")["page"]["items"][0]
        citizens = control.state_page(
            current, "city_citizens", actor_id=city["id"],
        )["page"]["items"]
        citizen_tiles = [item for item in citizens if item["kind"] == "tile"]
        specialists = [
            item for item in citizens if item["kind"] == "specialist"
        ]
        self.assertEqual(len(citizen_tiles), 4)
        self.assertEqual(len(specialists), 2)
        self.assertEqual(
            sum(item["count"] for item in specialists),
            1,
        )
        request = control.prepare_actor_scope(current, city["id"], 16)
        page = control.actor_scope_page(
            request, self.scope_page(request, list(scoped), view="v11-160"),
        )
        citizen = [
            item for item in page["page"]["items"]
            if item["subject"]["operation"] in {
                "work_tile", "unwork_tile", "set_specialist",
            }
        ]
        self.assertEqual(
            [item["subject"]["operation"] for item in citizen],
            ["work_tile", "unwork_tile", "unwork_tile", "set_specialist"],
        )
        unwork = [
            item["subject"]["target"] for item in citizen
            if item["subject"]["operation"] == "unwork_tile"
        ]
        self.assertEqual(
            {(item["x"], item["y"]) for item in unwork}, {(2, 2), (3, 2)},
        )
        self.assertNotIn("c:20:200", json.dumps(page))
        self.assertNotIn("source_specialist", json.dumps(page))
        for item in citizen:
            resolved = control.resolve_action(
                current, item["state_revision"], item["action_id"], {},
            )
            self.assertEqual(resolved.operation, item["subject"]["operation"])

        missing = V2SeatControl("game_test", "missing_citizen", 1)
        missing_city = missing.state_page(current, "cities")["page"]["items"][0]
        missing_request = missing.prepare_actor_scope(
            current, missing_city["id"], 16,
        )
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            missing.actor_scope_page(
                missing_request,
                self.scope_page(
                    missing_request, list(scoped[:-1]), view="v11-161",
                ),
            )

        omitted = tuple(
            row for row in rows
            if not row.startswith("city_tile city=c:20:200 tile=8 ")
        )
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            V2SeatControl("game_test", "omitted_row", 1).state_page(
                observation(omitted),
            )
        forged_visibility = replace_row(
            rows,
            "city_tile city=c:20:200 tile=7 worked=1 free_worked=0 can_work=0",
            "city_tile city=c:20:200 tile=7 worked=1 free_worked=0 can_work=1",
        )
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            V2SeatControl("game_test", "fogged_worker", 1).state_page(
                observation(forged_visibility),
            )
        invented_remembered_radius = replace_row(
            replace_row(
                rows,
                "city_tile city=c:20:200 tile=7 worked=1 free_worked=0 can_work=0",
                "city_tile city=c:20:200 tile=7 worked=0 free_worked=0 can_work=0",
            ),
            "size=3 food=3", "size=2 food=3",
        )
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            V2SeatControl(
                "game_test", "invented_remembered_radius", 1,
            ).state_page(observation(invented_remembered_radius))

        duplicate_city_claim = tuple(rows) + (
            next(
                row for row in rows
                if row.startswith("city ref=c:20:200 ")
            ).replace(
                "city ref=c:20:200 name=Alpha%20Centauri",
                "city ref=c:21:201 name=Duplicate",
            ),
            *(
                row.replace("city=c:20:200", "city=c:21:201")
                for row in rows
                if row.startswith("city_tile city=c:20:200 ")
                or row.startswith("city_specialist city=c:20:200 ")
            ),
        )
        overlap_city_row = next(
            row for row in rows if row.startswith("city ref=c:20:200 ")
        ).replace(
            "city ref=c:20:200 name=Alpha%20Centauri",
            "city ref=c:21:201 name=Beta",
        ).replace(
            "tile=5 x=1 y=2 size=3",
            "tile=8 x=4 y=2 size=1",
        ).replace(
            "citizen_tile_count=4 specialist_type_count=2",
            "citizen_tile_count=1 specialist_type_count=2",
        )
        overlap_city_claim = tuple(rows) + (
            overlap_city_row,
            "city_tile city=c:21:201 tile=8 worked=1 "
            "free_worked=1 can_work=1",
            *(
                row.replace("city=c:20:200", "city=c:21:201")
                for row in rows
                if row.startswith("city_specialist city=c:20:200 ")
            ),
        )
        malformed_states = {
            "omitted_specialist": tuple(
                row for row in rows
                if "city_specialist city=c:20:200 specialist=1 " not in row
            ),
            "two_defaults": replace_row(
                rows,
                "specialist=1 name=Scientist count=0 can_use=1 is_default=0",
                "specialist=1 name=Scientist count=0 can_use=1 is_default=1",
            ),
            "no_default": replace_row(
                rows,
                "specialist=0 name=Entertainer count=1 can_use=1 is_default=1",
                "specialist=0 name=Entertainer count=1 can_use=1 is_default=0",
            ),
            "conservation": replace_row(
                rows, "size=3 food=3", "size=4 food=3",
            ),
            "free_center_swapped": replace_row(
                replace_row(
                    rows,
                    "city_tile city=c:20:200 tile=5 worked=1 "
                    "free_worked=1 can_work=1",
                    "city_tile city=c:20:200 tile=5 worked=1 "
                    "free_worked=0 can_work=1",
                ),
                "city_tile city=c:20:200 tile=6 worked=1 "
                "free_worked=0 can_work=0",
                "city_tile city=c:20:200 tile=6 worked=1 "
                "free_worked=1 can_work=0",
            ),
            "duplicate_city_worked_tiles": duplicate_city_claim,
            "cross_city_can_work_overlap": overlap_city_claim,
        }
        for label, malformed in malformed_states.items():
            with self.subTest(label=label), self.assertRaisesRegex(
                V2ControlError, "internal_error",
            ):
                V2SeatControl(
                    "game_test", f"citizen_{label}", 1,
                ).state_page(observation(malformed))

        forged = V2SeatControl("game_test", "forged_citizen", 1)
        forged_city = forged.state_page(
            current, "cities",
        )["page"]["items"][0]
        forged_request = forged.prepare_actor_scope(
            current, forged_city["id"], 16,
        )
        forged_extra = _action(
            134, "city.work_tile", "c:20:200", 6,
            "city.work_tile", "City Tile", "Citizen Assigned", 0,
            source_specialist=0, target_name="worked tile",
        )
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            forged.actor_scope_page(
                forged_request,
                self.scope_page(
                    forged_request, [*scoped, forged_extra], view="v11-162",
                ),
            )

        duplicate_city_semantics = {
            "set_production": _action(
                135, "city.set_production", "c:20:200", -1,
                "city.set_production", "Production", "Production Changed", 0,
                target_build_kind="improvement", target_build=5,
                target_name="Granary",
            ),
            "buy_production": _action(
                136, "city.buy_production", "c:20:200", -1,
                "city.buy_production", "Production", "Production Bought", 0,
                target_build_kind="unit", target_build=12,
                target_name="Settlers",
            ),
        }
        for serial, (label, duplicate) in enumerate(
            duplicate_city_semantics.items(), start=163,
        ):
            duplicate_control = V2SeatControl(
                "game_test", f"duplicate_{label}", 1,
            )
            duplicate_city = duplicate_control.state_page(
                current, "cities",
            )["page"]["items"][0]
            duplicate_request = duplicate_control.prepare_actor_scope(
                current, duplicate_city["id"], 16,
            )
            with self.subTest(label=label), self.assertRaisesRegex(
                V2ControlError, "internal_error",
            ):
                duplicate_control.actor_scope_page(
                    duplicate_request,
                    self.scope_page(
                        duplicate_request,
                        [*scoped, duplicate],
                        view=f"v11-{serial}",
                    ),
                )

    def test_actor_scope_cursor_is_exclusive_bound_and_cross_checked(self):
        current = observation()
        own_unit = next(
            item for item in self.control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        request = self.control.prepare_actor_scope(current, own_unit["id"], 2)
        rows = [
            row for row in valid_rows()
            if row.startswith("action ") and " actor=u:10:100 " in row
        ]
        first = self.control.actor_scope_page(
            request,
            self.scope_page(request, rows[:2], total=len(rows)),
        )
        cursor = first["page"]["next_cursor"]
        self.assertIsNotNone(cursor)
        self.assertTrue(self.control.is_actor_scope_cursor(
            cursor, endpoint="legal_actions",
        ))
        continued = self.control.take_actor_scope_cursor(
            cursor, endpoint="legal_actions",
        )
        self.assertEqual(continued.offset, 2)
        self.assertEqual(continued.native_actor_ref, "u:10:100")
        second = self.control.actor_scope_page(
            continued,
            self.scope_page(
                continued, rows[2:4], total=len(rows),
            ),
        )
        self.assertEqual(len(second["page"]["items"]), 2)
        with self.assertRaises(V2ControlError):
            self.control.take_actor_scope_cursor(
                cursor, endpoint="legal_actions",
            )

        forged = dict(self.scope_page(request, rows[:2], total=len(rows)))
        forged["actor_ref"] = "u:11:101"
        with self.assertRaises(V2ControlError) as rejected:
            self.control.actor_scope_page(request, forged)
        self.assertEqual(rejected.exception.code, "internal_error")

    def test_actor_scope_cursor_from_prior_revision_is_stale(self):
        current = observation()
        own_unit = next(
            item for item in self.control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        request = self.control.prepare_actor_scope(current, own_unit["id"], 1)
        rows = [
            row for row in valid_rows()
            if row.startswith("action ") and " actor=u:10:100 " in row
        ]
        first = self.control.actor_scope_page(
            request,
            self.scope_page(request, rows[:1], total=len(rows)),
        )
        cursor = first["page"]["next_cursor"]
        self.assertIsNotNone(cursor)

        self.control.state_page(observation(revision=12))
        self.control.state_page(observation(revision=13))
        self.assertNotIn(request.native_revision, self.control._snapshots)
        self.assertTrue(self.control.is_actor_scope_cursor(
            cursor, endpoint="legal_actions",
        ))
        with self.assertRaisesRegex(V2ControlError, "stale_revision"):
            self.control.take_actor_scope_cursor(
                cursor, endpoint="legal_actions",
            )

        # Authentic stale knowledge remains distinguishable from a forgery so
        # a retrying harness can restart the scoped query safely.
        with self.assertRaisesRegex(V2ControlError, "stale_revision"):
            self.control.take_actor_scope_cursor(
                cursor, endpoint="legal_actions",
            )

    def test_player_scope_rejects_cross_page_government_substitution(self):
        current = observation()
        player_id = self.control.state_page(
            current,
        )["page"]["items"][0]["player"]["id"]
        global_rows = [
            row for row in valid_rows()
            if row.startswith("action ") and " actor=none " in row
        ]
        revolution, monarchy, republic = scoped_government_rows()
        request = self.control.prepare_actor_scope(current, player_id, 7)
        first_rows = global_rows + [monarchy]
        first = self.control.actor_scope_page(
            request,
            self.scope_page(
                request, first_rows, view="v11-9", total=9,
            ),
        )
        staged_monarchy = next(
            item for item in first["page"]["items"]
            if item["subject"]["operation"] == "change"
        )
        with self.assertRaisesRegex(V2ControlError, "action_expired"):
            self.control.resolve_action(
                current, staged_monarchy["state_revision"],
                staged_monarchy["action_id"], {},
            )
        continued = self.control.take_actor_scope_cursor(
            first["page"]["next_cursor"], endpoint="legal_actions",
        )
        duplicate_monarchy = republic.replace(
            "target_government=3", "target_government=2",
        ).replace("target_name=Republic", "target_name=Monarchy")
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            self.control.actor_scope_page(
                continued,
                self.scope_page(
                    continued, [revolution, duplicate_monarchy],
                    view="v11-9", total=9,
                ),
            )
        with self.assertRaisesRegex(V2ControlError, "action_expired"):
            self.control.resolve_action(
                current, staged_monarchy["state_revision"],
                staged_monarchy["action_id"], {},
            )

    def test_paginated_government_bindings_activate_only_after_complete_scope(self):
        current = observation()
        player_id = self.control.state_page(
            current,
        )["page"]["items"][0]["player"]["id"]
        global_rows = [
            row for row in valid_rows()
            if row.startswith("action ") and " actor=none " in row
        ]
        revolution, monarchy, republic = scoped_government_rows()
        request = self.control.prepare_actor_scope(current, player_id, 7)
        first = self.control.actor_scope_page(
            request,
            self.scope_page(
                request, global_rows + [monarchy], view="v11-10", total=9,
            ),
        )
        staged_monarchy = next(
            item for item in first["page"]["items"]
            if item["subject"]["operation"] == "change"
        )
        with self.assertRaisesRegex(V2ControlError, "action_expired"):
            self.control.resolve_action(
                current, staged_monarchy["state_revision"],
                staged_monarchy["action_id"], {},
            )
        continued = self.control.take_actor_scope_cursor(
            first["page"]["next_cursor"], endpoint="legal_actions",
        )
        final = self.control.actor_scope_page(
            continued,
            self.scope_page(
                continued, [revolution, republic],
                view="v11-10", total=9,
            ),
        )
        self.assertIsNone(final["page"]["next_cursor"])
        resolved = self.control.resolve_action(
            current, staged_monarchy["state_revision"],
            staged_monarchy["action_id"], {},
        )
        self.assertEqual(resolved.operation, "change")
        self.assertEqual(resolved.native_actor_ref, "p:1:10")

    def test_paginated_city_bindings_activate_atomically_after_completeness(self):
        rows, scoped = citizen_control_rows()
        current = observation(rows)

        failed = V2SeatControl("game_test", "city_atomic_failed", 1)
        failed_city = failed.state_page(
            current, "cities",
        )["page"]["items"][0]
        failed_request = failed.prepare_actor_scope(
            current, failed_city["id"], 3,
        )
        failed_first = failed.actor_scope_page(
            failed_request,
            self.scope_page(
                failed_request, list(scoped[:3]), view="v11-170",
                total=len(scoped),
            ),
        )
        for action in failed_first["page"]["items"]:
            with self.assertRaisesRegex(V2ControlError, "action_expired"):
                failed.resolve_action(
                    current, action["state_revision"], action["action_id"], {},
                )
        failed_next = failed.take_actor_scope_cursor(
            failed_first["page"]["next_cursor"], endpoint="legal_actions",
        )
        forged_last = _action(
            134, "city.work_tile", "c:20:200", 6,
            "city.work_tile", "City Tile", "Citizen Assigned", 0,
            source_specialist=0, target_name="worked tile",
        )
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            failed.actor_scope_page(
                failed_next,
                self.scope_page(
                    failed_next,
                    [*scoped[3:5], forged_last],
                    view="v11-170",
                    total=len(scoped),
                ),
            )
        for action in failed_first["page"]["items"]:
            with self.assertRaisesRegex(V2ControlError, "action_expired"):
                failed.resolve_action(
                    current, action["state_revision"], action["action_id"], {},
                )

        valid = V2SeatControl("game_test", "city_atomic_valid", 1)
        valid_city = valid.state_page(
            current, "cities",
        )["page"]["items"][0]
        valid_request = valid.prepare_actor_scope(
            current, valid_city["id"], 4,
        )
        valid_first = valid.actor_scope_page(
            valid_request,
            self.scope_page(
                valid_request, list(scoped[:4]), view="v11-171",
                total=len(scoped),
            ),
        )
        valid_next = valid.take_actor_scope_cursor(
            valid_first["page"]["next_cursor"], endpoint="legal_actions",
        )
        valid_middle = valid.actor_scope_page(
            valid_next,
            self.scope_page(
                valid_next, list(scoped[4:8]), view="v11-171",
                total=len(scoped),
            ),
        )
        valid_last = valid.take_actor_scope_cursor(
            valid_middle["page"]["next_cursor"], endpoint="legal_actions",
        )
        valid_final = valid.actor_scope_page(
            valid_last,
            self.scope_page(
                valid_last, list(scoped[8:]), view="v11-171",
                total=len(scoped),
            ),
        )
        self.assertIsNone(valid_final["page"]["next_cursor"])
        valid_build_choice = valid.state_page(
            current, "city_build_choices", actor_id=valid_city["id"],
        )["page"]["items"][0]
        for action in (
            *valid_first["page"]["items"],
            *valid_middle["page"]["items"],
            *valid_final["page"]["items"],
        ):
            operation = action["subject"]["operation"]
            arguments = (
                {"items": [
                    valid_build_choice["id"]
                ]} if operation == "set_worklist"
                else {"allow_disband": True, "new_citizens": "science"}
                if operation == "set_options"
                else {"city_name": "Atomic City"}
                if operation == "rename" else governor_goal()
                if operation == "set_governor" else {}
            )
            resolved = valid.resolve_action(
                current, action["state_revision"], action["action_id"],
                arguments,
            )
            self.assertTrue(resolved.scoped)
            self.assertEqual(resolved.native_actor_ref, "c:20:200")

    def test_paginated_unit_mobility_is_atomic_and_failure_discards_prefix(self):
        rows, mobility = noncombat_mobility_rows()
        current = observation(rows)
        global_rows = [
            row for row in rows
            if row.startswith("action ") and " actor=u:10:100 " in row
        ]
        all_rows = [*global_rows, *mobility]

        failed = V2SeatControl("game_test", "mobility_atomic_failed", 1)
        failed_unit = next(
            item for item in failed.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        failed_request = failed.prepare_actor_scope(
            current, failed_unit["id"], 10,
        )
        failed_first = failed.actor_scope_page(
            failed_request,
            self.scope_page(
                failed_request, all_rows[:10], view="v11-191",
                total=len(all_rows),
            ),
        )
        for action in failed_first["page"]["items"]:
            with self.assertRaisesRegex(V2ControlError, "action_expired"):
                failed.resolve_action(
                    current, action["state_revision"], action["action_id"], {},
                )
        failed_next = failed.take_actor_scope_cursor(
            failed_first["page"]["next_cursor"], endpoint="legal_actions",
        )
        malformed_tail = list(all_rows[10:])
        malformed_tail[-1] = malformed_tail[-1].replace(
            "target_extra=-1", "target_extra=4",
        )
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            failed.actor_scope_page(
                failed_next,
                self.scope_page(
                    failed_next, malformed_tail, view="v11-191",
                    total=len(all_rows),
                ),
            )
        for action in failed_first["page"]["items"]:
            with self.assertRaisesRegex(V2ControlError, "action_expired"):
                failed.resolve_action(
                    current, action["state_revision"], action["action_id"], {},
                )

        valid = V2SeatControl("game_test", "mobility_atomic_valid", 1)
        valid_unit = next(
            item for item in valid.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        valid_request = valid.prepare_actor_scope(
            current, valid_unit["id"], 10,
        )
        valid_first = valid.actor_scope_page(
            valid_request,
            self.scope_page(
                valid_request, all_rows[:10], view="v11-192",
                total=len(all_rows),
            ),
        )
        for action in valid_first["page"]["items"]:
            with self.assertRaisesRegex(V2ControlError, "action_expired"):
                valid.resolve_action(
                    current, action["state_revision"], action["action_id"], {},
                )
        valid_next = valid.take_actor_scope_cursor(
            valid_first["page"]["next_cursor"], endpoint="legal_actions",
        )
        valid_final = valid.actor_scope_page(
            valid_next,
            self.scope_page(
                valid_next, all_rows[10:], view="v11-192",
                total=len(all_rows),
            ),
        )
        self.assertIsNone(valid_final["page"]["next_cursor"])
        for action in (
            *valid_first["page"]["items"], *valid_final["page"]["items"],
        ):
            arguments = (
                {"city_name": "Atomic Mobility City"}
                if action["subject"]["operation"] == "found_city" else {}
            )
            resolved = valid.resolve_action(
                current, action["state_revision"], action["action_id"],
                arguments,
            )
            self.assertTrue(resolved.scoped)
            self.assertEqual(resolved.native_actor_ref, "u:10:100")

    def test_noncombat_mobility_state_variants_and_targets_are_opaque(self):
        rows, mobility = noncombat_mobility_rows()
        current = observation(rows)
        cities = self.control.state_page(
            current, "cities",
        )["page"]["items"]
        units = self.control.state_page(
            current, "units",
        )["page"]["items"]
        own = next(item for item in units if item["scope"] == "own")
        beta = next(item for item in cities if item["name"] == "Beta")
        self.assertEqual(own["paradrop"], {
            "used_this_turn": False, "range": 8,
        })
        self.assertEqual(beta["airlift"], {"remaining": 1, "maximum": 1})

        global_rows = [
            row for row in rows
            if row.startswith("action ") and " actor=u:10:100 " in row
        ]
        request = self.control.prepare_actor_scope(current, own["id"], 16)
        first = self.control.actor_scope_page(
            request,
            self.scope_page(
                request, [*global_rows, *mobility][:16], view="v11-193",
                total=len(global_rows) + len(mobility),
            ),
        )
        continued = self.control.take_actor_scope_cursor(
            first["page"]["next_cursor"], endpoint="legal_actions",
        )
        final = self.control.actor_scope_page(
            continued,
            self.scope_page(
                continued, [*global_rows, *mobility][16:], view="v11-193",
                total=len(global_rows) + len(mobility),
            ),
        )
        projected = [
            item for item in (
                *first["page"]["items"], *final["page"]["items"],
            )
            if item["subject"]["operation"] in {
                "airlift", "paradrop", "teleport",
            }
        ]
        by_operation = {
            operation: [
                item for item in projected
                if item["subject"]["operation"] == operation
            ]
            for operation in ("airlift", "paradrop", "teleport")
        }
        self.assertEqual(
            {key: len(value) for key, value in by_operation.items()},
            {"airlift": 1, "paradrop": 3, "teleport": 5},
        )
        self.assertEqual(
            by_operation["airlift"][0]["subject"]["target"]["id"],
            beta["id"],
        )
        self.assertEqual(
            by_operation["airlift"][0]["subject"]["target"]["tile_id"],
            beta["tile_id"],
        )
        self.assertEqual(
            by_operation["paradrop"][1]["subject"]["probability"]["kind"],
            "not_implemented",
        )
        for operation, expected in (("paradrop", 3), ("teleport", 5)):
            self.assertEqual(len({
                item["subject"]["variant"]
                for item in by_operation[operation]
            }), expected)
        for action in projected:
            validate_legal_action_descriptor(action)
            resolved = self.control.resolve_action(
                current, action["state_revision"], action["action_id"], {},
            )
            self.assertEqual(resolved.operation, action["subject"]["operation"])
        serialized = json.dumps(
            [cities, own, projected], sort_keys=True, separators=(",", ":"),
        )
        for private in (
            "u:10:100", "c:20:200", "c:21:201", "Airlift Unit",
            "Paradrop Unit", "Teleport2", "slot=", "a000000000000008",
        ):
            self.assertNotIn(private, serialized)

    def test_noncombat_mobility_forgery_and_scope_overflow_fail_closed(self):
        rows, mobility = noncombat_mobility_rows()
        current = observation(rows)
        global_rows = [
            row for row in rows
            if row.startswith("action ") and " actor=u:10:100 " in row
        ]
        airlift, paradrop, _, _, teleport, *_ = mobility
        unknown_rows = tuple(sorted((
            *rows,
            "tile index=9 x=5 y=2 known=0 terrain=unknown owner=none",
            _action(
                160, "unit.move", "u:10:100", 9,
                "Unit Move", "Tile", "Unit Move", 0,
                legality="possibly_legal", probability_kind="unknown",
                probability_min=0, probability_max=200,
            ),
        )))
        conquer_rules = (
            "Paradrop Unit Conquer",
            "Paradrop Unit Frighten Conquer",
            "Paradrop Unit Enter Conquer",
            "Teleport Conquer",
            "Teleport Frighten Conquer",
            "Teleport Enter Conquer",
        )
        invalid = {
            "airlift_not_implemented": airlift.replace(
                "legality=legal probability_kind=exact "
                "probability_min=200 probability_max=200",
                "legality=unresolved probability_kind=not_implemented "
                "probability_min=-1 probability_max=-1",
            ),
            "teleport_not_implemented": teleport.replace(
                "legality=legal probability_kind=exact "
                "probability_min=200 probability_max=200",
                "legality=unresolved probability_kind=not_implemented "
                "probability_min=-1 probability_max=-1",
            ),
            "remembered_target": paradrop.replace(
                "target_tile=6", "target_tile=7",
            ),
            "unknown_target": paradrop.replace(
                "target_tile=6", "target_tile=9",
            ),
            "forged_source_lifetime": airlift.replace(
                "source_city=c:20:200", "source_city=c:20:201",
            ),
            "same_source_destination": airlift.replace(
                "destination_city=c:21:201", "destination_city=c:20:200",
            ),
            "source_not_actor_tile": airlift.replace(
                "source_city=c:20:200 destination_city=c:21:201",
                "source_city=c:21:201 destination_city=c:20:200",
            ),
            "unrelated_sentinel": paradrop.replace(
                "target_extra=-1", "target_extra=4",
            ),
        }
        invalid.update({
            f"conquer_variant_{index}": _action(
                149 + index,
                "unit.paradrop" if rule.startswith("Paradrop")
                else "unit.teleport",
                "u:10:100", 6, rule, "Tile",
                "Unit Paradrop" if rule.startswith("Paradrop")
                else "Teleport",
                0, target_name="destination",
            )
            for index, rule in enumerate(conquer_rules)
        })
        for label, forged in invalid.items():
            forged_rows = unknown_rows if label == "unknown_target" else rows
            case_global_rows = [
                row for row in forged_rows
                if row.startswith("action ") and " actor=u:10:100 " in row
            ]
            control = V2SeatControl("game_test", f"mobility_{label}", 1)
            unit = next(
                item for item in control.state_page(
                    observation(forged_rows), "units",
                )["page"]["items"] if item["scope"] == "own"
            )
            request = control.prepare_actor_scope(
                observation(forged_rows), unit["id"], 16,
            )
            with self.subTest(label=label), self.assertRaisesRegex(
                V2ControlError, "internal_error",
            ):
                control.actor_scope_page(
                    request,
                    self.scope_page(
                        request, [*case_global_rows, forged],
                        view="v11-194",
                    ),
                )

        overflow = V2SeatControl("game_test", "mobility_overflow", 1)
        unit = next(
            item for item in overflow.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        request = overflow.prepare_actor_scope(current, unit["id"], 10)
        first = overflow.actor_scope_page(
            request,
            self.scope_page(
                request, [*global_rows, *mobility][:10], view="v11-195",
                total=len(global_rows) + len(mobility),
            ),
        )
        continued = overflow.take_actor_scope_cursor(
            first["page"]["next_cursor"], endpoint="legal_actions",
        )
        final_page = self.scope_page(
            continued, [*global_rows, *mobility][10:], view="v11-195",
            total=len(global_rows) + len(mobility),
        )
        final_page["overflow"] = True
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            overflow.actor_scope_page(continued, final_page)
        for action in first["page"]["items"]:
            with self.assertRaisesRegex(V2ControlError, "action_expired"):
                overflow.resolve_action(
                    current, action["state_revision"], action["action_id"], {},
                )

    def test_city_target_economic_unit_actions_are_opaque_and_fog_safe(self):
        rows, economic = economic_unit_rows()
        current = observation(rows)
        unit = next(
            item for item in self.control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        city_sites = {
            item["name"]: item for item in self.control.state_page(
                current, "city_sites",
            )["page"]["items"]
        }
        self.assertEqual(
            {name: item["visibility"] for name, item in city_sites.items()},
            {
                "Alpha Centauri": "own", "Beta": "own",
                "Gamma": "visible", "Delta": "known",
            },
        )
        self.assertNotIn("production", city_sites["Gamma"])
        self.assertNotIn("surplus", city_sites["Gamma"])

        global_rows = [
            row for row in rows
            if row.startswith("action ") and " actor=u:10:100 " in row
        ]
        request = self.control.prepare_actor_scope(
            current, unit["id"], 16,
        )
        payload = self.control.actor_scope_page(
            request,
            self.scope_page(
                request, [*global_rows, *economic], view="v11-205",
            ),
        )
        expected = {
            "upgrade", "rehome", "join_city", "establish_trade",
            "marketplace", "help_wonder", "disband_recover",
        }
        actions = {
            item["subject"]["operation"]: item
            for item in payload["page"]["items"]
            if item["subject"]["operation"] in expected
        }
        self.assertEqual(set(actions), expected)
        self.assertFalse(actions["upgrade"]["subject"]["consuming"])
        self.assertFalse(actions["rehome"]["subject"]["consuming"])
        for operation in expected - {"upgrade", "rehome"}:
            self.assertTrue(actions[operation]["subject"]["consuming"])
        self.assertEqual(
            actions["establish_trade"]["subject"]["probability"],
            {
                "kind": "not_implemented", "minimum_percent": None,
                "maximum_percent": None,
            },
        )
        self.assertEqual(
            actions["upgrade"]["subject"]["upgrade_to"]["name"],
            "Engineers",
        )
        self.assertTrue(
            actions["upgrade"]["subject"]["upgrade_to"]["id"].startswith(
                "unit_type_"
            )
        )
        self.assertEqual(
            actions["marketplace"]["subject"]["source_city"]["id"],
            city_sites["Alpha Centauri"]["id"],
        )
        self.assertEqual(
            actions["help_wonder"]["subject"]["target"]["id"],
            city_sites["Gamma"]["id"],
        )
        self.assertEqual(
            actions["disband_recover"]["subject"]["target"]["id"],
            city_sites["Alpha Centauri"]["id"],
        )
        for action in actions.values():
            validate_legal_action_descriptor(action)
            resolved = self.control.resolve_action(
                current, action["state_revision"], action["action_id"], {},
            )
            self.assertEqual(resolved.operation, action["subject"]["operation"])
            self.assertEqual(resolved.native_arguments, "-")

        serialized = json.dumps(actions, sort_keys=True, separators=(",", ":"))
        for private in (
            "u:10:100", "c:20:200", "c:30:300", "Upgrade Unit",
            "Establish Trade Route", "Enter Marketplace", "slot=",
            "Disband Unit Recover",
        ):
            self.assertNotIn(private, serialized)

    def test_city_target_economic_unit_forgery_fails_closed(self):
        rows, economic = economic_unit_rows()
        current = observation(rows)
        global_rows = [
            row for row in rows
            if row.startswith("action ") and " actor=u:10:100 " in row
        ]
        for label, forged in {
            "unknown_destination": economic[2].replace(
                "destination_city=c:20:200", "destination_city=c:99:999",
            ),
            "wrong_destination_name": economic[5].replace(
                "target_name=Gamma", "target_name=Delta",
            ),
            "source_on_rehome": economic[1].replace(
                "source_city=none", "source_city=c:20:200",
            ),
            "wrong_trade_home": economic[3].replace(
                "source_city=c:20:200", "source_city=c:21:201",
            ),
            "upgrade_without_type": economic[0].replace(
                "target_build_kind=unit target_build=14",
                "target_build_kind=none target_build=-1",
            ),
        }.items():
            control = V2SeatControl("game_test", f"economic_{label}", 1)
            unit = next(
                item for item in control.state_page(
                    current, "units",
                )["page"]["items"] if item["scope"] == "own"
            )
            request = control.prepare_actor_scope(current, unit["id"], 16)
            with self.subTest(label=label), self.assertRaisesRegex(
                V2ControlError, "internal_error",
            ):
                control.actor_scope_page(
                    request,
                    self.scope_page(
                        request, [*global_rows, forged], view="v11-206",
                    ),
                )
    def test_activity_cancel_is_exact_and_same_activity_restart_fails_closed(self):
        rows = replace_row(
            valid_rows(),
            "activity=idle activity_target=-1 activity_target_name=none "
            "activity_progress=0",
            "activity=road activity_target=7 activity_target_name=Road "
            "activity_progress=3",
        )
        current = observation(rows)
        own_unit = next(
            item for item in self.control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        self.assertEqual(own_unit["activity"]["name"], "road")
        self.assertEqual(own_unit["activity"]["target"]["name"], "Road")
        request = self.control.prepare_actor_scope(current, own_unit["id"], 16)
        global_rows = [
            row for row in rows
            if row.startswith("action ") and " actor=u:10:100 " in row
        ]
        cancel = _action(
            106, "unit.cancel_activity", "u:10:100", -1,
            "unit.cancel_activity", "Unit", "Activity Cancelled", 0,
            activity="idle",
        )
        payload = self.control.actor_scope_page(
            request,
            self.scope_page(
                request, global_rows + [cancel], view="v11-6",
            ),
        )
        descriptor = next(
            item for item in payload["page"]["items"]
            if item["subject"]["operation"] == "cancel_activity"
        )
        resolved = self.control.resolve_action(
            current, descriptor["state_revision"], descriptor["action_id"], {},
        )
        self.assertEqual(resolved.public_kind, "unit.order")
        self.assertEqual(resolved.operation, "cancel_activity")
        self.assertTrue(resolved.scoped)

        same_road = _action(
            107, "unit.start_activity", "u:10:100", -1,
            "unit.start_activity", "Worker Activity", "Activity Installed", 0,
            target_extra=9, activity="road", target_name="Railroad",
        )
        rejected_request = self.control.prepare_actor_scope(
            current, own_unit["id"], 16,
        )
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            self.control.actor_scope_page(
                rejected_request,
                self.scope_page(
                    rejected_request, global_rows + [same_road], view="v11-7",
                ),
            )

    def test_unit_automation_controls_are_scoped_opaque_and_canonical(self):
        current = observation(valid_rows())
        own = next(
            item for item in self.control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        self.assertEqual(
            own["automation"], {"controller": "none", "has_orders": False},
        )
        request = self.control.prepare_actor_scope(current, own["id"], 16)
        global_rows = [
            row for row in valid_rows()
            if row.startswith("action ") and " actor=u:10:100 " in row
        ]
        automation = (
            _action(
                150, "unit.auto_work", "u:10:100", -1,
                "unit.auto_work", "Unit", "Auto Work Installed", 0,
                target_name="auto_work",
            ),
            _action(
                151, "unit.auto_explore", "u:10:100", -1,
                "unit.auto_explore", "Unit", "Auto Explore Installed", 0,
                target_name="auto_explore",
            ),
        )
        payload = self.control.actor_scope_page(
            request,
            self.scope_page(
                request, [*global_rows, *automation], view="v11-196",
            ),
        )
        descriptors = {
            item["subject"]["operation"]: item
            for item in payload["page"]["items"]
        }
        for operation in ("auto_work", "auto_explore"):
            descriptor = descriptors[operation]
            self.assertEqual(descriptor["kind"], "unit.order")
            self.assertRegex(
                descriptor["subject"]["variant"], r"^variant_[0-9a-f]{32}$",
            )
            resolved = self.control.resolve_action(
                current, descriptor["state_revision"],
                descriptor["action_id"], {},
            )
            self.assertEqual(resolved.operation, operation)
            self.assertEqual(resolved.native_arguments, "-")
            self.assertTrue(resolved.scoped)

        active_rows = tuple(sorted(
            row.replace(
                "activity=idle activity_target=-1 activity_target_name=none ",
                "activity=explore activity_target=-1 activity_target_name=none ",
            ).replace(
                "controller=none has_orders=0",
                "controller=auto_explore has_orders=0",
            )
            for row in valid_rows()
            if not (row.startswith("action ") and " actor=u:10:100 " in row)
        ))
        active = observation(active_rows, revision=12)
        active_own = next(
            item for item in self.control.state_page(
                active, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        self.assertEqual(
            active_own["automation"]["controller"], "auto_explore",
        )
        cancel_request = self.control.prepare_actor_scope(
            active, active_own["id"], 16,
        )
        cancel = _action(
            152, "unit.cancel_automation", "u:10:100", -1,
            "unit.cancel_automation", "Unit", "Automation Cancelled", 0,
        )
        cancel_payload = self.control.actor_scope_page(
            cancel_request,
            self.scope_page(
                cancel_request, [cancel], view="v12-197",
            ),
        )
        self.assertEqual(len(cancel_payload["page"]["items"]), 1)
        cancel_descriptor = cancel_payload["page"]["items"][0]
        self.assertEqual(
            cancel_descriptor["subject"]["operation"], "cancel_automation",
        )
        cancel_resolved = self.control.resolve_action(
            active, cancel_descriptor["state_revision"],
            cancel_descriptor["action_id"], {},
        )
        self.assertEqual(cancel_resolved.public_kind, "unit.order")
        self.assertEqual(cancel_resolved.native_arguments, "-")

    def test_cancel_orders_is_scoped_opaque_and_requires_exact_public_state(self):
        queued_rows = tuple(sorted(
            row.replace(
                "controller=none has_orders=0 orders_repeat=0 "
                "orders_vigilant=0 order_count=0 "
                "orders_digest=fnv1a64-0000000000000000 "
                "orders_destination=-1",
                "controller=none has_orders=1 orders_repeat=0 "
                "orders_vigilant=0 order_count=2 "
                "orders_digest=fnv1a64-1234567890abcdef "
                "orders_destination=7",
            )
            for row in valid_rows()
            if not (row.startswith("action ") and " actor=u:10:100 " in row)
        ))
        current = observation(queued_rows, revision=13)
        own = next(
            item for item in self.control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        self.assertEqual(
            own["automation"], {"controller": "none", "has_orders": True},
        )
        self.assertNotIn("goto", own["automation"])
        request = self.control.prepare_actor_scope(current, own["id"], 16)
        cancel = _action(
            153, "unit.cancel_orders", "u:10:100", -1,
            "unit.cancel_orders", "Unit", "Orders Cancelled", 0,
            target_name="orders",
        )
        payload = self.control.actor_scope_page(
            request,
            self.scope_page(request, [cancel], view="v13-198"),
        )
        self.assertEqual(len(payload["page"]["items"]), 1)
        descriptor = payload["page"]["items"][0]
        self.assertEqual(descriptor["kind"], "unit.order")
        self.assertEqual(
            descriptor["subject"]["operation"], "cancel_orders",
        )
        self.assertRegex(
            descriptor["subject"]["variant"], r"^variant_[0-9a-f]{32}$",
        )
        self.assertEqual(descriptor["subject"]["actor"]["type"], "unit")
        resolved = self.control.resolve_action(
            current, descriptor["state_revision"], descriptor["action_id"], {},
        )
        self.assertEqual(resolved.operation, "cancel_orders")
        self.assertEqual(resolved.native_arguments, "-")
        self.assertTrue(resolved.scoped)

        for revision, (old, new) in enumerate((
            (
                "controller=none has_orders=1 orders_repeat=0 "
                "orders_vigilant=0 order_count=2 "
                "orders_digest=fnv1a64-1234567890abcdef "
                "orders_destination=7",
                "controller=none has_orders=0 orders_repeat=0 "
                "orders_vigilant=0 order_count=0 "
                "orders_digest=fnv1a64-0000000000000000 "
                "orders_destination=-1",
            ),
            ("activity=idle", "activity=sentry"),
            ("controller=none has_orders=1",
             "controller=auto_work has_orders=1"),
        ), start=14):
            invalid = observation(
                replace_row(queued_rows, old, new), revision=revision,
            )
            invalid_own = next(
                item for item in self.control.state_page(
                    invalid, "units",
                )["page"]["items"] if item["scope"] == "own"
            )
            invalid_request = self.control.prepare_actor_scope(
                invalid, invalid_own["id"], 16,
            )
            with self.assertRaisesRegex(V2ControlError, "internal_error"):
                self.control.actor_scope_page(
                    invalid_request,
                    self.scope_page(
                        invalid_request, [cancel],
                        view=f"v{revision}-199",
                    ),
                )

    def test_goto_is_scoped_opaque_and_projects_only_known_tile(self):
        current = observation(valid_rows(), revision=19)
        own = next(
            item for item in self.control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        request = self.control.prepare_actor_scope(current, own["id"], 16)
        global_rows = [
            row for row in valid_rows()
            if row.startswith("action ") and " actor=u:10:100 " in row
        ]
        goto = _action(
            154, "unit.goto", "u:10:100", 7,
            "unit.goto", "Tile", "Orders Queued", 0,
            target_name="destination",
        )
        payload = self.control.actor_scope_page(
            request,
            self.scope_page(request, [*global_rows, goto], view="v19-200"),
        )
        descriptor = next(
            item for item in payload["page"]["items"]
            if item["subject"]["operation"] == "goto"
        )
        self.assertEqual(descriptor["kind"], "unit.order")
        self.assertEqual(descriptor["subject"]["operation"], "goto")
        self.assertEqual(
            set(descriptor["subject"]["target"]), {"type", "id", "x", "y"},
        )
        self.assertEqual(
            (descriptor["subject"]["target"]["x"],
             descriptor["subject"]["target"]["y"]),
            (3, 2),
        )
        self.assertRegex(
            descriptor["subject"]["variant"], r"^variant_[0-9a-f]{32}$",
        )
        serialized = json.dumps(descriptor, sort_keys=True)
        for private in (
            "native_target_tile", "source_unit_tile", "route_signature",
            "order_count", "goto_tile", "request_count",
        ):
            self.assertNotIn(private, serialized)
        resolved = self.control.resolve_action(
            current, descriptor["state_revision"], descriptor["action_id"], {},
        )
        self.assertEqual(resolved.operation, "goto")
        self.assertEqual(resolved.native_arguments, "-")
        self.assertTrue(resolved.scoped)

        for revision, (old, new) in enumerate((
            (
                "controller=none has_orders=0 orders_repeat=0 "
                "orders_vigilant=0 order_count=0 "
                "orders_digest=fnv1a64-0000000000000000 "
                "orders_destination=-1",
                "controller=none has_orders=1 orders_repeat=0 "
                "orders_vigilant=0 order_count=2 "
                "orders_digest=fnv1a64-1234567890abcdef "
                "orders_destination=7",
            ),
            ("activity=idle", "activity=sentry"),
        ), start=20):
            invalid = observation(
                replace_row(valid_rows(), old, new), revision=revision,
            )
            invalid_own = next(
                item for item in self.control.state_page(
                    invalid, "units",
                )["page"]["items"] if item["scope"] == "own"
            )
            invalid_request = self.control.prepare_actor_scope(
                invalid, invalid_own["id"], 16,
            )
            with self.assertRaisesRegex(V2ControlError, "internal_error"):
                self.control.actor_scope_page(
                    invalid_request,
                    self.scope_page(
                        invalid_request, [*global_rows, goto],
                        view=f"v{revision}-202",
                    ),
                )

    def test_set_route_resolves_ordered_opaque_waypoints_exactly(self):
        current = observation(valid_rows(), revision=23)
        own = next(
            item for item in self.control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        tiles = self.control.state_page(
            current, "known_tiles",
        )["page"]["items"]
        tile_by_x = {item["x"]: item["id"] for item in tiles}
        request = self.control.prepare_actor_scope(current, own["id"], 16)
        global_rows = [
            row for row in valid_rows()
            if row.startswith("action ") and " actor=u:10:100 " in row
        ]
        route = _action(
            155, "unit.set_route", "u:10:100", -1,
            "unit.set_route", "Route", "Orders Queued", 0,
            "route-required", route_waypoint_limit=64,
            target_name="route",
        )
        payload = self.control.actor_scope_page(
            request,
            self.scope_page(request, [*global_rows, route], view="v23-203"),
        )
        descriptor = next(
            item for item in payload["page"]["items"]
            if item["subject"]["operation"] == "set_route"
        )
        self.assertEqual(descriptor["kind"], "unit.order")
        self.assertEqual(
            descriptor["arguments_schema"]["properties"]["mode"]["enum"],
            ["goto", "patrol"],
        )
        waypoint_metadata = descriptor["arguments_schema"]["properties"][
            "waypoints"
        ]["metadata"]
        self.assertTrue(
            waypoint_metadata["first_item_must_differ_from_actor_source"],
        )
        self.assertTrue(
            waypoint_metadata[
                "goto_final_item_must_differ_from_actor_source"
            ],
        )
        resolved = self.control.resolve_action(
            current, descriptor["state_revision"], descriptor["action_id"],
            {"mode": "patrol", "waypoints": [tile_by_x[3], tile_by_x[2]]},
        )
        self.assertEqual(
            resolved.native_arguments, "mode=patrol;waypoints=7,6",
        )
        closed = self.control.resolve_action(
            current, descriptor["state_revision"], descriptor["action_id"],
            {"mode": "patrol", "waypoints": [tile_by_x[3], tile_by_x[1]]},
        )
        self.assertEqual(
            closed.native_arguments, "mode=patrol;waypoints=7,5",
        )
        for invalid in (
            {"mode": "patrol", "waypoints": [tile_by_x[1]]},
            {"mode": "patrol", "waypoints": [tile_by_x[3], tile_by_x[3]]},
            {"mode": "goto", "waypoints": [tile_by_x[3], tile_by_x[1]]},
            {"mode": "loop", "waypoints": [tile_by_x[3]]},
        ):
            with self.subTest(invalid=invalid), self.assertRaisesRegex(
                V2ControlError, "invalid_request",
            ):
                self.control.resolve_action(
                    current, descriptor["state_revision"],
                    descriptor["action_id"], invalid,
                )

    def test_player_infrastructure_target_projects_choices_and_resolves(self):
        rows = list(replace_row(
            valid_rows(actions=False),
            "infrastructure_enabled=0 infrastructure_points=0",
            "infrastructure_enabled=1 infrastructure_points=100",
        ))
        rows = [
            row for row in rows
            if not row.startswith((
                "tile ", "city_tile ", "city_specialist ",
                "city_worklist ", "city_build_choice ",
                "city_improvement ",
            ))
        ]
        rows.extend((
            "infrastructure_extra id=0 name=Road cost=20 build_time=0 build_time_factor=3",
            "infrastructure_extra id=1 name=Irrigation cost=60 build_time=2 build_time_factor=1",
            "city_site ref=c:30:300 owner=p:2:20 name=Outpost tile=8 "
            "x=4 y=2 size=1 visibility=visible",
        ))
        rows = [
            row.replace("known_tile_count=3", "known_tile_count=4")
            for row in rows
        ]
        current = observation(tuple(sorted(rows)), revision=24)
        overview = self.control.state_page(current)["page"]["items"][0]
        tile = next(
            item for item in self.control.state_page(
                current, "city_sites",
            )["page"]["items"] if item["name"] == "Outpost"
        )
        request = self.control.prepare_target_action(
            current, overview["player"]["id"], tile["tile_id"],
        )
        support = self.control.prepare_target_tile_support(request)
        self.assertEqual(support.selector, "t8-r0")
        self.control.hydrate_state_scope(support, {
            "generation": 1,
            "native_revision": support.native_revision,
            "section": "tile_window",
            "selector": support.selector,
            "view_id": "q24-204",
            "offset": 0,
            "count": 1,
            "total_count": 1,
            "next_offset": 1,
            "complete": True,
            "overflow": False,
            "rows": (
                "tile_local index=8 x=4 y=2 known=2 terrain=Grassland "
                "owner=p:2:20 placing_extra=-1 "
                "placing_extra_name=none placing_turns=0 placing_time=1 "
                "resource_extra=-1 resource_name=none has_label=0 "
                "label=none food=0 shields=0 trade=0",
            ),
        })
        row = _action(
            0, "player.place_infrastructure", "p:1:10", 8,
            "player.place_infrastructure", "Tile",
            "Infrastructure Placement Started", 0,
            "infrastructure-extra-required", target_name="infrastructure",
            infrastructure_choices=(0, 1),
        ).replace(
            "slot=a0000000000000000",
            "slot=t000000080123456789ABCDEF",
        )
        page = self.control.target_action_page(request, {
            "generation": 1,
            "native_revision": request.native_revision,
            "actor_ref": request.native_actor_ref,
            "native_tile": request.native_target_tile,
            "count": 1,
            "rows": (row,),
        })
        descriptor = page["page"]["items"][0]
        self.assertEqual(descriptor["kind"], "player.set_infrastructure")
        choices = descriptor["subject"]["target"]["choices"]
        self.assertEqual(
            [(item["name"], item["cost"], item["turns"])
             for item in choices],
            [("Road", 20, 3), ("Irrigation", 60, 2)],
        )
        resolved = self.control.resolve_action(
            current, descriptor["state_revision"], descriptor["action_id"],
            {"extra_id": choices[1]["extra_id"]},
        )
        self.assertEqual(resolved.native_arguments, "extra=1")
        with self.assertRaisesRegex(V2ControlError, "invalid_request"):
            self.control.resolve_action(
                current, descriptor["state_revision"],
                descriptor["action_id"], {"extra_id": "extra_forged"},
            )

    def test_unit_self_controls_are_scoped_opaque_and_exactly_resolvable(self):
        rows = replace_row(
            valid_rows(),
            "converts_to_id=-1 converts_to=none",
            "converts_to_id=14 converts_to=Engineers",
        )
        current = observation(rows)
        units = self.control.state_page(current, "units")["page"]["items"]
        own = next(item for item in units if item["scope"] == "own")
        city = self.control.state_page(current, "cities")["page"]["items"][0]
        self.assertTrue(own["type_id"].startswith("unit_type_"))
        self.assertEqual(own["home_city_id"], city["id"])
        self.assertEqual(own["conversion"]["target_type"], "Engineers")
        self.assertTrue(
            own["conversion"]["target_type_id"].startswith("unit_type_")
        )
        self.assertNotEqual(own["conversion"]["target_type_id"], own["type_id"])
        conflicting_type = replace_row(
            rows, "scope=visible owner=p:2:20 type_id=13 type=Warriors",
            "scope=visible owner=p:2:20 type_id=14 type=Warriors",
        )
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            V2SeatControl("game_test", "type_conflict", 1).state_page(
                observation(conflicting_type),
            )

        global_operations = {
            item["subject"]["operation"]
            for item in self.control.legal_actions_page(current)["page"]["items"]
        }
        self.assertTrue({
            "sentry", "fortify", "convert", "disband", "make_homeless",
        }.isdisjoint(global_operations))
        for action in scoped_unit_self_rows():
            with self.subTest(global_injection=action.split(" ")[2]):
                injected = V2SeatControl("game_test", "fresh_agent", 1)
                with self.assertRaisesRegex(V2ControlError, "internal_error"):
                    injected.state_page(observation(tuple(sorted(
                        rows + (action,),
                    ))))

        global_rows = [
            row for row in rows
            if row.startswith("action ") and " actor=u:10:100 " in row
        ]
        request = self.control.prepare_actor_scope(current, own["id"], 16)
        payload = self.control.actor_scope_page(
            request,
            self.scope_page(
                request, global_rows + list(scoped_unit_self_rows()),
                view="v11-41",
            ),
        )
        controls = {
            item["subject"]["operation"]: item
            for item in payload["page"]["items"]
            if item["subject"]["operation"] in {
                "sentry", "fortify", "convert", "disband", "make_homeless",
            }
        }
        self.assertEqual(set(controls), {
            "sentry", "fortify", "convert", "disband", "make_homeless",
        })
        self.assertEqual(controls["sentry"]["kind"], "unit.order")
        self.assertTrue(all(
            controls[operation]["kind"] == "unit.perform_action"
            for operation in {"fortify", "convert", "disband", "make_homeless"}
        ))
        self.assertEqual(
            controls["convert"]["subject"]["target"],
            {
                "type": "unit_type",
                "id": own["conversion"]["target_type_id"],
                "name": "Engineers",
            },
        )
        self.assertEqual(
            controls["make_homeless"]["subject"]["target"],
            {"type": "city", "id": city["id"]},
        )
        self.assertEqual(
            controls["disband"]["subject"]["target"],
            controls["disband"]["subject"]["actor"],
        )
        variants = {
            item["subject"]["variant"] for item in controls.values()
        }
        self.assertEqual(len(variants), 5)
        self.assertTrue(all(value.startswith("variant_") for value in variants))
        public = json.dumps(payload, sort_keys=True)
        for private in (
            "Convert Unit", "Disband Unit", "Unit Make Homeless",
            "u:10:100", "c:20:200", '"native_type_id"',
        ):
            self.assertNotIn(private, public)

        for operation, descriptor in controls.items():
            self.assertEqual(
                validate_legal_action_descriptor(descriptor), descriptor,
            )
            resolved = self.control.resolve_action(
                current, descriptor["state_revision"],
                descriptor["action_id"], {},
            )
            self.assertEqual(resolved.operation, operation)
            self.assertEqual(resolved.native_actor_ref, "u:10:100")
            self.assertTrue(resolved.scoped)

    def test_unit_self_controls_reject_hostile_grammar_and_current_state(self):
        rows = replace_row(
            valid_rows(),
            "converts_to_id=-1 converts_to=none",
            "converts_to_id=14 converts_to=Engineers",
        )
        current = observation(rows)
        own = next(
            item for item in self.control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        global_rows = [
            row for row in rows
            if row.startswith("action ") and " actor=u:10:100 " in row
        ]
        sentry, fortify, convert, disband, homeless = scoped_unit_self_rows()
        hostile = (
            sentry.replace("target_kind=Unit", "target_kind=Self"),
            fortify.replace(
                "result=Fortify%20Installed", "result=Activity%20Installed",
            ),
            fortify.replace("activity=fortifying", "activity=fortified"),
            convert.replace("target_build=14", "target_build=15"),
            convert.replace("target_name=Engineers", "target_name=Settlers"),
            disband.replace("actor_consuming_always=1", "actor_consuming_always=0"),
            homeless.replace("target_name=self", "target_name=none"),
            homeless.replace("kind=unit.homeless", "kind=unit.disband"),
            fortify.replace(
                "legality=legal probability_kind=exact "
                "probability_min=200 probability_max=200",
                "legality=unresolved probability_kind=not_implemented "
                "probability_min=-1 probability_max=-1",
            ),
        )
        for serial, forged in enumerate(hostile, start=50):
            with self.subTest(serial=serial):
                request = self.control.prepare_actor_scope(
                    current, own["id"], 16,
                )
                with self.assertRaisesRegex(V2ControlError, "internal_error"):
                    self.control.actor_scope_page(
                        request,
                        self.scope_page(
                            request, global_rows + [forged],
                            view=f"v11-{serial}",
                        ),
                    )

        current_fortifying = observation(
            replace_row(rows, "activity=idle", "activity=fortifying"),
            revision=12,
        )
        fortifying_unit = next(
            item for item in self.control.state_page(
                current_fortifying, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        request = self.control.prepare_actor_scope(
            current_fortifying, fortifying_unit["id"], 16,
        )
        current_global = [
            row for row in current_fortifying["rows"]
            if row.startswith("action ") and " actor=u:10:100 " in row
        ]
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            self.control.actor_scope_page(
                request,
                self.scope_page(
                    request, current_global + [fortify], view="v11-60",
                ),
            )

    def test_transport_state_and_all_six_scoped_controls_are_opaque(self):
        rows = transport_state_rows()
        current = observation(rows)
        public_units = self.control.state_page(
            current, "units",
        )["page"]["items"]
        own = [item for item in public_units if item["scope"] == "own"]
        self.assertEqual(len(own), 4)
        transporter = next(item for item in own if item["type"] == "Trireme")
        carried = next(
            item for item in own
            if item["transport"]["state"] == "transported"
        )
        self.assertEqual(carried["transport"]["transporter_unit_id"],
                         transporter["id"])
        self.assertEqual(transporter["transport"], {
            "state": "untransported",
            "transporter_unit_id": None,
            "capacity": 2,
            "occupied": 1,
        })
        by_native = {
            "u:10:100": next(item for item in own if item["type"] == "Settlers"),
            "u:12:102": transporter,
            "u:13:103": carried,
            "u:14:104": next(
                item for item in own
                if item["type"] == "Warriors"
                and item["transport"]["state"] == "untransported"
            ),
        }
        transport_rows = scoped_transport_rows()
        controls: dict[str, dict[str, object]] = {}
        actor_for_operation: dict[str, str] = {}
        serial = 70
        for native_actor, actor_rows in transport_rows.items():
            request = self.control.prepare_actor_scope(
                current, by_native[native_actor]["id"], 16,
            )
            global_rows = [
                row for row in rows
                if row.startswith("action ")
                and f" actor={native_actor} " in row
            ]
            payload = self.control.actor_scope_page(
                request,
                self.scope_page(
                    request, global_rows + list(actor_rows),
                    view=f"v11-{serial}",
                ),
            )
            serial += 1
            for descriptor in payload["page"]["items"]:
                operation = descriptor["subject"]["operation"]
                if operation in {
                    "board", "deboard", "embark", "disembark", "load",
                    "unload",
                }:
                    controls[operation] = descriptor
                    actor_for_operation[operation] = native_actor
        self.assertEqual(set(controls), {
            "board", "deboard", "embark", "disembark", "load", "unload",
        })
        self.assertTrue(all(
            item["kind"] == "unit.perform_action"
            and item["subject"]["variant"].startswith("variant_")
            and item["subject"]["probability"] == {
                "kind": "exact", "minimum_percent": 100.0,
                "maximum_percent": 100.0,
            }
            for item in controls.values()
        ))
        self.assertEqual(
            controls["disembark"]["subject"]["target"]["type"], "tile",
        )
        self.assertTrue(all(
            controls[operation]["subject"]["transport_context"]["id"]
            in {transporter["id"], by_native["u:12:102"]["id"]}
            for operation in {"deboard", "disembark", "unload"}
        ))
        public = json.dumps({"units": own, "actions": controls}, sort_keys=True)
        for private in (
            "u:10:100", "u:12:102", "u:13:103", "u:14:104",
            "Transport Board", "Transport Deboard", "Transport Embark",
            "Transport Disembark", "Transport Load", "Transport Unload",
            "target_unit_ref", "transport_context_ref", "lifecycle",
        ):
            self.assertNotIn(private, public)
        global_operations = {
            item["subject"]["operation"]
            for item in self.control.legal_actions_page(current)["page"]["items"]
        }
        self.assertTrue(set(controls).isdisjoint(global_operations))
        for operation, descriptor in controls.items():
            resolved = self.control.resolve_action(
                current, descriptor["state_revision"],
                descriptor["action_id"], {},
            )
            self.assertEqual(resolved.operation, operation)
            self.assertEqual(
                resolved.native_actor_ref, actor_for_operation[operation],
            )
            self.assertTrue(resolved.scoped)

    def test_transport_controls_accept_allies_remembered_and_unknown_tiles(self):
        alliance = (
            "diplomacy other=p:2:20 name=Other nation=Romans state=Alliance "
        )
        rows = replace_row(
            transport_state_rows(),
            "diplomacy other=p:2:20 name=Other nation=Romans state=Peace ",
            alliance,
        )
        allied_transporter_rows = replace_row(
            rows,
            "unit ref=u:11:101 scope=visible owner=p:2:20 type_id=13 "
            "type=Warriors tile=6 x=2 y=2 hp=8",
            "unit ref=u:11:101 scope=visible owner=p:2:20 type_id=20 "
            "type=Trireme tile=5 x=1 y=2 hp=8",
        )

        current = observation(allied_transporter_rows)
        units = self.control.state_page(current, "units")["page"]["items"]
        actor = next(item for item in units if item["type"] == "Settlers")
        ally = next(item for item in units if item["scope"] == "visible")
        request = self.control.prepare_actor_scope(current, actor["id"], 16)
        actor_rows = tuple(
            row for row in allied_transporter_rows
            if row.startswith("action ") and " actor=u:10:100 " in row
        )
        page = self.control.actor_scope_page(
            request,
            self.scope_page(request, actor_rows + (
                _action(
                    180, "unit.board", "u:10:100", -1,
                    "Transport Board", "Unit", "Unit Transport Board", 0,
                    target_name="transporter", target_unit="u:11:101",
                ),
            ), view="v11-180"),
        )
        board = next(
            item for item in page["page"]["items"]
            if item["subject"]["operation"] == "board"
        )
        self.assertEqual(board["subject"]["target"], {
            "type": "unit", "id": ally["id"],
        })
        self.assertNotIn("transport_context", board["subject"])

        carried_rows = replace_row(
            allied_transporter_rows,
            "unit ref=u:10:100 scope=own owner=p:1:10 type_id=12 "
            "type=Settlers home_city=c:20:200 converts_to_id=-1 "
            "converts_to=none tile=5 x=1 y=2 hp=10 moves=3 "
            "activity=idle activity_target=-1 activity_target_name=none "
            "activity_progress=0 transport_state=untransported "
            "transporter=none transport_capacity=0 occupied=0",
            "unit ref=u:10:100 scope=own owner=p:1:10 type_id=12 "
            "type=Settlers home_city=c:20:200 converts_to_id=-1 "
            "converts_to=none tile=5 x=1 y=2 hp=10 moves=3 "
            "activity=idle activity_target=-1 activity_target_name=none "
            "activity_progress=0 transport_state=transported "
            "transporter=u:11:101 transport_capacity=0 occupied=0",
        )
        remembered_rows = replace_row(
            carried_rows,
            "tile index=7 x=3 y=2 known=1 terrain=Hills",
            "tile index=7 x=2 y=3 known=1 terrain=Hills",
        )
        remembered_control = V2SeatControl(
            "game_test", "remembered_transport", 1,
        )
        remembered = observation(remembered_rows)
        remembered_units = remembered_control.state_page(
            remembered, "units",
        )["page"]["items"]
        remembered_actor = next(
            item for item in remembered_units if item["type"] == "Settlers"
        )
        remembered_ally = next(
            item for item in remembered_units if item["scope"] == "visible"
        )
        request = remembered_control.prepare_actor_scope(
            remembered, remembered_actor["id"], 16,
        )
        actor_rows = tuple(
            row for row in remembered_rows
            if row.startswith("action ") and " actor=u:10:100 " in row
        )
        page = remembered_control.actor_scope_page(
            request,
            self.scope_page(request, actor_rows + (
                _action(
                    181, "unit.disembark", "u:10:100", 7,
                    "Transport Disembark", "Tile",
                    "Unit Transport Disembark", 0,
                    target_name="destination",
                    transport_context="u:11:101",
                ),
            ), view="v11-181"),
        )
        disembark = next(
            item for item in page["page"]["items"]
            if item["subject"]["operation"] == "disembark"
        )
        self.assertEqual(
            disembark["subject"]["target"]["visibility"], "remembered",
        )
        self.assertEqual(
            disembark["subject"]["transport_context"]["id"],
            remembered_ally["id"],
        )

        unknown_rows = tuple(
            row for row in carried_rows
            if not row.startswith("tile index=7 ")
        )
        unknown_rows = replace_row(
            unknown_rows, "known_tile_count=3", "known_tile_count=2",
        )
        unknown_control = V2SeatControl(
            "game_test", "unknown_transport", 1,
        )
        unknown = observation(unknown_rows)
        unknown_units = unknown_control.state_page(
            unknown, "units",
        )["page"]["items"]
        unknown_actor = next(
            item for item in unknown_units if item["type"] == "Settlers"
        )
        unknown_ally = next(
            item for item in unknown_units if item["scope"] == "visible"
        )
        support = unknown_control.prepare_unit_support_scopes(
            unknown, unknown_actor["id"],
        )[0]
        unknown_control.hydrate_state_scope(
            support,
            state_scope_catalog(support, (
                "tile index=50 x=2 y=3 known=0 terrain=unknown owner=none",
            )),
        )
        request = unknown_control.prepare_actor_scope(
            unknown, unknown_actor["id"], 16,
        )
        actor_rows = tuple(
            row for row in unknown_rows
            if row.startswith("action ") and " actor=u:10:100 " in row
        )
        page = unknown_control.actor_scope_page(
            request,
            self.scope_page(request, actor_rows + (
                _action(
                    182, "unit.disembark", "u:10:100", 50,
                    "Transport Disembark", "Tile",
                    "Unit Transport Disembark", 0,
                    target_name="destination",
                    transport_context="u:11:101",
                ),
            ), view="v11-182"),
        )
        disembark = next(
            item for item in page["page"]["items"]
            if item["subject"]["operation"] == "disembark"
        )
        unknown_target = disembark["subject"]["target"]
        self.assertEqual(set(unknown_target), {
            "type", "id", "x", "y", "visibility",
        })
        self.assertEqual(
            (unknown_target["x"], unknown_target["y"],
             unknown_target["visibility"]),
            (2, 3, "unknown"),
        )
        self.assertEqual(
            disembark["subject"]["transport_context"]["id"],
            unknown_ally["id"],
        )
        public = json.dumps(disembark, sort_keys=True)
        for hidden in (
            "terrain", "owner_player_id", "resource", "extras",
            "native_index", "index=50", "Hills",
        ):
            self.assertNotIn(hidden, public)

        allied_cargo_rows = replace_row(
            rows,
            "unit ref=u:11:101 scope=visible owner=p:2:20 type_id=13 "
            "type=Warriors tile=6 x=2 y=2 hp=8",
            "unit ref=u:11:101 scope=visible owner=p:2:20 type_id=13 "
            "type=Warriors tile=5 x=1 y=2 hp=8",
        )
        allied_cargo_rows = replace_row(
            allied_cargo_rows,
            "transport_capacity=2 occupied=1",
            "transport_capacity=2 occupied=2",
        )
        cargo_control = V2SeatControl("game_test", "allied_cargo", 1)
        cargo_current = observation(allied_cargo_rows)
        cargo_units = cargo_control.state_page(
            cargo_current, "units",
        )["page"]["items"]
        transporter = next(
            item for item in cargo_units if item["type"] == "Trireme"
        )
        visible_cargo = next(
            item for item in cargo_units if item["scope"] == "visible"
        )
        request = cargo_control.prepare_actor_scope(
            cargo_current, transporter["id"], 16,
        )
        actor_rows = tuple(
            row for row in allied_cargo_rows
            if row.startswith("action ") and " actor=u:12:102 " in row
        )
        page = cargo_control.actor_scope_page(
            request,
            self.scope_page(request, actor_rows + (
                _action(
                    182, "unit.unload", "u:12:102", -1,
                    "Transport Unload", "Unit", "Unit Transport Unload", 0,
                    target_name="cargo", target_unit="u:11:101",
                    transport_context="u:12:102",
                ),
            ), view="v11-182"),
        )
        unload = next(
            item for item in page["page"]["items"]
            if item["subject"]["operation"] == "unload"
        )
        self.assertEqual(unload["subject"]["target"], {
            "type": "unit", "id": visible_cargo["id"],
        })

    def test_transport_controls_accept_nested_and_direct_switch_topology(self):
        rows = list(transport_state_rows())
        rows.append(
            "unit ref=u:15:105 scope=own owner=p:1:10 type_id=20 "
            "type=Trireme home_city=none converts_to_id=-1 "
            "converts_to=none tile=5 x=1 y=2 hp=10 moves=3 "
            "activity=idle activity_target=-1 activity_target_name=none "
            "activity_progress=0 transport_state=untransported "
            "transporter=none transport_capacity=2 occupied=0 "
            "paradropped=0 paradrop_range=0 controller=none has_orders=0"
        )
        current = observation(tuple(sorted(rows)))
        units = self.control.state_page(current, "units")["page"]["items"]
        carried = next(
            item for item in units
            if item.get("transport", {}).get("state") == "transported"
        )
        old_transporter = next(
            item for item in units if item["id"]
            == carried["transport"]["transporter_unit_id"]
        )
        new_transporter = next(
            item for item in units
            if item["type"] == "Trireme" and item is not old_transporter
            and item["id"] != old_transporter["id"]
        )

        request = self.control.prepare_actor_scope(
            current, carried["id"], 16,
        )
        actor_rows = tuple(
            row for row in rows
            if row.startswith("action ") and " actor=u:13:103 " in row
        )
        page = self.control.actor_scope_page(
            request,
            self.scope_page(request, actor_rows + (
                _action(
                    184, "unit.board", "u:13:103", -1,
                    "Transport Board", "Unit", "Unit Transport Board", 0,
                    target_name="transporter", target_unit="u:15:105",
                    transport_context="u:12:102",
                ),
            ), view="v11-184"),
        )
        switched = next(
            item for item in page["page"]["items"]
            if item["subject"]["operation"] == "board"
        )
        self.assertEqual(
            switched["subject"]["target"]["id"], new_transporter["id"],
        )
        self.assertEqual(
            switched["subject"]["transport_context"]["id"],
            old_transporter["id"],
        )

        nested_rows = replace_row(
            tuple(sorted(rows)),
            "unit ref=u:12:102 scope=own owner=p:1:10 type_id=20 "
            "type=Trireme home_city=none converts_to_id=-1 converts_to=none "
            "tile=5 x=1 y=2 hp=10 moves=3 activity=idle "
            "activity_target=-1 activity_target_name=none activity_progress=0 "
            "transport_state=untransported transporter=none "
            "transport_capacity=2 occupied=1",
            "unit ref=u:12:102 scope=own owner=p:1:10 type_id=20 "
            "type=Trireme home_city=none converts_to_id=-1 converts_to=none "
            "tile=5 x=1 y=2 hp=10 moves=3 activity=idle "
            "activity_target=-1 activity_target_name=none activity_progress=0 "
            "transport_state=transported transporter=u:15:105 "
            "transport_capacity=2 occupied=1",
        )
        nested_rows = replace_row(
            nested_rows,
            "unit ref=u:15:105 scope=own owner=p:1:10 type_id=20 "
            "type=Trireme home_city=none converts_to_id=-1 "
            "converts_to=none tile=5 x=1 y=2 hp=10 moves=3 "
            "activity=idle activity_target=-1 activity_target_name=none "
            "activity_progress=0 transport_state=untransported "
            "transporter=none transport_capacity=2 occupied=0",
            "unit ref=u:15:105 scope=own owner=p:1:10 type_id=20 "
            "type=Trireme home_city=none converts_to_id=-1 "
            "converts_to=none tile=5 x=1 y=2 hp=10 moves=3 "
            "activity=idle activity_target=-1 activity_target_name=none "
            "activity_progress=0 transport_state=untransported "
            "transporter=none transport_capacity=2 occupied=1",
        )
        nested_control = V2SeatControl("game_test", "nested_transport", 1)
        nested = observation(nested_rows)
        nested_units = nested_control.state_page(
            nested, "units",
        )["page"]["items"]
        nested_actor = next(
            item for item in nested_units
            if item["type"] == "Trireme"
            and item["transport"]["state"] == "transported"
        )
        request = nested_control.prepare_actor_scope(
            nested, nested_actor["id"], 16,
        )
        actor_rows = tuple(
            row for row in nested_rows
            if row.startswith("action ") and " actor=u:12:102 " in row
        )
        page = nested_control.actor_scope_page(
            request,
            self.scope_page(request, actor_rows + (
                _action(
                    185, "unit.deboard", "u:12:102", -1,
                    "Transport Deboard", "Unit", "Unit Transport Deboard", 0,
                    target_name="transporter", target_unit="u:15:105",
                    transport_context="u:15:105",
                ),
            ), view="v11-185"),
        )
        self.assertIn(
            "deboard",
            {item["subject"]["operation"] for item in page["page"]["items"]},
        )


    def test_transport_controls_fail_closed_on_forged_or_stale_state(self):
        rows = transport_state_rows()
        transport_rows = scoped_transport_rows()

        unresolved_transporter = replace_row(
            rows,
            "transport_state=untransported transporter=none "
            "transport_capacity=2 occupied=1",
            "transport_state=unresolved transporter=none "
            "transport_capacity=-1 occupied=-1",
        )
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            V2SeatControl(
                "game_test", "unresolved_transporter", 1,
            ).state_page(observation(unresolved_transporter), "units")

        false_empty_transporter = replace_row(
            rows,
            "transport_state=untransported transporter=none "
            "transport_capacity=2 occupied=1",
            "transport_state=untransported transporter=none "
            "transport_capacity=2 occupied=0",
        )
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            V2SeatControl(
                "game_test", "false_empty_transporter", 1,
            ).state_page(observation(false_empty_transporter), "units")

        for serial, action in enumerate(
            (item for group in transport_rows.values() for item in group),
            start=80,
        ):
            with self.subTest(global_injection=serial):
                control = V2SeatControl("game_test", f"transport_{serial}", 1)
                with self.assertRaisesRegex(V2ControlError, "internal_error"):
                    control.state_page(observation(tuple(sorted(rows + (action,)))))

        hostile = (
            ("u:10:100", transport_rows["u:10:100"][0].replace(
                "target_unit=u:12:102", "target_unit=u:11:101",
            )),
            ("u:10:100", transport_rows["u:10:100"][0].replace(
                "transport_context=none", "transport_context=u:12:102",
            )),
            ("u:13:103", transport_rows["u:13:103"][0].replace(
                "transport_context=u:12:102", "transport_context=none",
            )),
            ("u:13:103", transport_rows["u:13:103"][1].replace(
                "target_tile=6", "target_tile=7",
            )),
            ("u:12:102", transport_rows["u:12:102"][1].replace(
                "target_unit=u:13:103", "target_unit=u:10:100",
            )),
            ("u:10:100", transport_rows["u:10:100"][0].replace(
                "probability_kind=exact probability_min=200 probability_max=200",
                "probability_kind=range probability_min=100 probability_max=200",
            )),
        )
        for serial, (native_actor, forged) in enumerate(hostile, start=90):
            with self.subTest(forged=serial):
                control = V2SeatControl("game_test", f"forged_{serial}", 1)
                current = observation(rows)
                own = control.state_page(current, "units")["page"]["items"]
                actor = next(
                    item for item in own
                    if item["id"] == control._entity_id("unit", native_actor)
                )
                request = control.prepare_actor_scope(current, actor["id"], 16)
                with self.assertRaisesRegex(V2ControlError, "internal_error"):
                    control.actor_scope_page(
                        request,
                        self.scope_page(request, [forged], view=f"v11-{serial}"),
                    )

        for serial, replacement in enumerate((
            (
                "transport_state=untransported transporter=none "
                "transport_capacity=0 occupied=0",
                "transport_state=untransported transporter=none "
                "transport_capacity=1 occupied=0",
            ),
            (
                "transport_state=untransported transporter=none "
                "transport_capacity=0 occupied=0",
                "transport_state=unresolved transporter=none "
                "transport_capacity=0 occupied=-1",
            ),
        ), start=100):
            changed = replace_row(rows, *replacement)
            control = V2SeatControl("game_test", f"state_{serial}", 1)
            current = observation(changed)
            actor = next(
                item for item in control.state_page(
                    current, "units",
                )["page"]["items"] if item["type"] == "Settlers"
            )
            request = control.prepare_actor_scope(current, actor["id"], 16)
            with self.assertRaisesRegex(V2ControlError, "internal_error"):
                control.actor_scope_page(
                    request,
                    self.scope_page(
                        request, list(transport_rows["u:10:100"]),
                        view=f"v11-{serial}",
                    ),
                )

        control = V2SeatControl("game_test", "stale_transport", 1)
        current = observation(rows)
        actor = next(
            item for item in control.state_page(current, "units")["page"]["items"]
            if item["type"] == "Settlers"
        )
        request = control.prepare_actor_scope(current, actor["id"], 16)
        page = control.actor_scope_page(
            request,
            self.scope_page(
                request,
                [
                    row for row in rows
                    if row.startswith("action ") and " actor=u:10:100 " in row
                ] + list(transport_rows["u:10:100"]),
                view="v11-110",
            ),
        )
        descriptor = next(
            item for item in page["page"]["items"]
            if item["subject"]["operation"] == "board"
        )
        with self.assertRaisesRegex(V2ControlError, "stale_revision"):
            control.resolve_action(
                observation(rows, revision=12), descriptor["state_revision"],
                descriptor["action_id"], {},
            )

    def test_scoped_only_actions_reject_forged_actor_and_target_grammars(self):
        current = observation()
        state = self.control.state_page(current)
        city_id = self.control.state_page(
            current, "cities",
        )["page"]["items"][0]["id"]
        own_unit_id = next(
            item["id"] for item in self.control.state_page(
                current, "units",
            )["page"]["items"] if item["scope"] == "own"
        )
        self.assertIsNotNone(state)
        city_set, city_buy = scoped_city_rows()[:2]
        cultivate, road, _ = scoped_worker_rows()
        cases = (
            (
                city_id,
                city_set.replace(
                    "target_build_kind=improvement target_build=5 "
                    "source_specialist=-1 target_specialist=-1 "
                    "target_extra=-1 activity=none target_name=Granary",
                    "target_build_kind=unit target_build=12 "
                    "source_specialist=-1 target_specialist=-1 "
                    "target_extra=-1 activity=none target_name=Settlers",
                ),
            ),
            (
                city_id,
                city_set.replace(
                    "target_build_kind=improvement target_build=5 "
                    "source_specialist=-1 target_specialist=-1 "
                    "target_extra=-1 activity=none target_name=Granary",
                    "target_build_kind=unit target_build=12 "
                    "source_specialist=-1 target_specialist=-1 "
                    "target_extra=-1 activity=none target_name=Forged",
                ),
            ),
            (
                city_id,
                city_buy.replace("actor=c:20:200", "actor=u:10:100"),
            ),
            (
                own_unit_id,
                cultivate.replace("target_extra=-1", "target_extra=7"),
            ),
            (
                own_unit_id,
                road.replace("target_extra=7", "target_extra=-1"),
            ),
        )
        for serial, (actor_id, forged) in enumerate(cases, start=20):
            with self.subTest(serial=serial):
                request = self.control.prepare_actor_scope(
                    current, actor_id, 16,
                )
                with self.assertRaisesRegex(V2ControlError, "internal_error"):
                    self.control.actor_scope_page(
                        request,
                        self.scope_page(
                            request, [forged], view=f"v11-{serial}",
                        ),
                    )

        injected = tuple(sorted(valid_rows() + (city_set,)))
        fresh = V2SeatControl("game_test", "agent_test", 1)
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            fresh.state_page(observation(injected))

    def test_inactive_snapshot_cannot_mint_scoped_only_actions(self):
        inactive = observation(valid_rows(actions=False))
        city_id = self.control.state_page(
            inactive, "cities",
        )["page"]["items"][0]["id"]
        request = self.control.prepare_actor_scope(inactive, city_id, 16)
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            self.control.actor_scope_page(
                request,
                self.scope_page(
                    request, [scoped_city_rows()[0]], view="v11-30",
                ),
            )

    def test_actor_scope_rejects_foreign_forged_and_cross_seat_ids(self):
        current = observation()
        units = self.control.state_page(
            current, "units",
        )["page"]["items"]
        foreign = next(item["id"] for item in units if item["scope"] == "visible")
        other = V2SeatControl("game_test", "other_agent", 1)
        other_player = other.state_page(current)["page"]["items"][0]["player"]["id"]
        for actor_id in (foreign, "unit_" + "0" * 32, other_player):
            with self.subTest(actor_id=actor_id), self.assertRaises(
                V2ControlError,
            ) as rejected:
                self.control.prepare_actor_scope(current, actor_id)
            self.assertEqual(rejected.exception.code, "invalid_request")

    def test_projects_every_row_type_and_all_initial_action_variants(self):
        overview = self.control.state_page(observation())["page"]["items"][0]
        self.assertEqual(tuple(overview), (
            "client_state", "turn", "phase", "phase_mode", "phase_count",
            "active_phase", "phase_ready", "map", "player", "research",
            "counts", "legal_action_counts",
        ))
        self.assertEqual(overview["client_state"], "running")
        self.assertEqual(overview["turn"], 7)
        self.assertEqual(overview["phase"], 1)
        self.assertEqual(overview["phase_mode"], "players_alternate")
        self.assertEqual(overview["phase_count"], 2)
        self.assertIs(overview["active_phase"], True)
        self.assertIs(overview["phase_ready"], True)
        self.assertEqual(overview["map"], {
            "width": 16, "height": 16, "topology": "square",
            "wrap_x": True, "wrap_y": False,
        })
        self.assertEqual(overview["player"]["name"], "Codex")
        self.assertIs(overview["player"]["alive"], True)
        self.assertEqual(tuple(overview["player"]), (
            "id", "name", "nation", "government", "government_state",
            "alive", "phase_done", "economy", "infrastructure",
        ))
        self.assertEqual(overview["player"]["economy"]["science"], 60)
        self.assertEqual(overview["research"]["target"], "Writing")
        self.assertEqual(overview["research"]["goal"], "Pottery")
        self.assertTrue(overview["research"]["target_id"].startswith("tech_"))
        self.assertTrue(overview["research"]["goal_id"].startswith("tech_"))
        self.assertEqual(overview["player"]["economy"]["max_rate"], 70)
        self.assertIs(overview["player"]["economy"]["changeable_tax"], True)
        government_state = overview["player"]["government_state"]
        self.assertEqual(government_state["status"], "stable")
        self.assertEqual(government_state["method"], "random")
        self.assertEqual(government_state["turns_remaining"], 0)
        self.assertIsNone(government_state["target_id"])
        self.assertTrue(government_state["current_id"].startswith("government_"))
        self.assertTrue(
            government_state["during_revolution_id"].startswith("government_")
        )
        self.assertEqual(overview["counts"], {
            "research": 5,
            "governments": 4,
            "multipliers": 1,
            "spaceship": 1,
            "votes": 0,
            "diplomacy": 1,
            "diplomacy_clauses": 0,
            "known_tiles": 3,
            "infrastructure": 0,
            "cities": 1,
            "city_detail": 1,
            "city_citizens": 2,
            "city_worker_tasks": 0,
            "city_build_choices": 2,
            "city_worklist": 0,
            "city_improvements": 0,
            "city_governor": 0,
            "city_sites": 1,
            "units": 2,
            "tombstones": 1,
            "chat": 0,
            "legal_actions": 14,
        })

        research = self.control.state_page(observation(), "research")["page"]["items"]
        self.assertEqual(
            [item["state"] for item in research],
            ["unset", "known", "available", "reachable", "available"],
        )
        self.assertTrue(all(item["id"].startswith("tech_") for item in research))
        governments = self.control.state_page(
            observation(), "governments",
        )["page"]["items"]
        self.assertEqual(
            [item["name"] for item in governments],
            ["Anarchy", "Despotism", "Monarchy", "Republic"],
        )
        self.assertEqual(sum(item["current"] for item in governments), 1)
        self.assertEqual(sum(item["during_revolution"] for item in governments), 1)
        self.assertEqual(
            {item["name"] for item in governments if item["can_change"]},
            {"Monarchy", "Republic"},
        )
        self.assertTrue(all(
            item["id"].startswith("government_") for item in governments
        ))
        relation = self.control.state_page(observation(), "diplomacy")["page"]["items"][0]
        self.assertEqual(relation["state"], "Peace")
        self.assertTrue(relation["player_id"].startswith("player_"))
        tiles = self.control.state_page(observation(), "known_tiles")["page"]["items"]
        self.assertEqual(
            [item["visibility"] for item in tiles],
            ["visible", "visible", "remembered"],
        )
        city = self.control.state_page(observation(), "cities")["page"]["items"][0]
        self.assertEqual(city["name"], "Alpha Centauri")
        self.assertEqual(city["surplus"], {"food": 3, "shields": 2, "trade": -1})
        self.assertEqual(city["production"]["name"], "Settlers")
        self.assertTrue(city["production"]["id"].startswith("production_"))
        self.assertEqual(city["production"]["buy_cost"], 30)
        units = self.control.state_page(observation(), "units")["page"]["items"]
        self.assertEqual(units[0]["moves"], 3)
        self.assertEqual(units[0]["activity"]["name"], "idle")
        self.assertIsNone(units[0]["activity"]["target"])
        self.assertTrue(units[0]["activity"]["id"].startswith("activity_"))
        self.assertNotIn("moves", units[1])
        self.assertNotIn("activity", units[1])
        tombstone = self.control.state_page(observation(), "tombstones")["page"]["items"][0]
        self.assertEqual(tombstone["type"], "unit")
        self.assertTrue(tombstone["id"].startswith("unit_"))

        legal = self.control.legal_actions_page(observation())["page"]["items"]
        self.assertEqual(len(legal), 14)
        self.assertEqual(
            {item["subject"]["operation"] for item in legal},
            {
                "end", "found_city", "move", "attack", "suicide_attack",
                "set_target", "set_goal", "set_rates",
            },
        )
        self.assertTrue(all(set(item) == {
            "action_id", "kind", "label", "subject", "arguments_schema",
            "state_revision",
        } for item in legal))
        for descriptor in legal:
            self.assertEqual(validate_legal_action_descriptor(descriptor), descriptor)
        found = next(
            item for item in legal if item["subject"]["operation"] == "found_city"
        )
        city_name = found["arguments_schema"]["properties"]["city_name"]
        self.assertEqual(city_name["minLength"], 1)
        self.assertEqual(city_name["maxLength"], 119)
        self.assertEqual(city_name["metadata"]["max_utf8_bytes"], 119)
        no_arguments = next(
            item for item in legal if item["subject"]["operation"] == "end"
        )
        self.assertEqual(no_arguments["arguments_schema"], {
            "type": "object",
            "properties": {},
            "additionalProperties": False,
        })
        research_target = next(
            item for item in legal
            if item["subject"]["operation"] == "set_target"
        )
        self.assertEqual(research_target["kind"], "research.set_target")
        self.assertEqual(
            set(research_target["subject"]["target"]),
            {"type", "id", "name", "state"},
        )
        self.assertTrue(
            research_target["subject"]["target"]["id"].startswith("tech_"),
        )
        rates = next(
            item for item in legal
            if item["subject"]["operation"] == "set_rates"
        )
        rate_schema = rates["arguments_schema"]
        self.assertEqual(rate_schema["required"], ["tax", "luxury", "science"])
        self.assertEqual(rate_schema["properties"]["science"], {
            "type": "integer", "minimum": 0, "maximum": 70,
            "multipleOf": 1,
        })
        self.assertEqual(rate_schema["metadata"], {
            "exact_sum": {
                "fields": ["tax", "luxury", "science"],
                "equals": 100,
            },
            "server_step": 1,
        })

    def test_city_summaries_are_compact_and_child_sections_are_scoped(self):
        rows, _scoped = citizen_control_rows()
        current = observation(rows)
        city = self.control.state_page(
            current, "cities",
        )["page"]["items"][0]
        self.assertNotIn("citizens", city)
        self.assertNotIn("management", city)
        detail = self.control.state_page(
            current, "city_detail", actor_id=city["id"],
        )["page"]["items"]
        self.assertEqual(len(detail), 1)
        self.assertEqual(detail[0]["counts"]["citizen_tiles"], 4)
        first = self.control.state_page(
            current, "city_citizens", limit=1, actor_id=city["id"],
        )
        self.assertEqual(first["page"]["total_items"], 6)
        self.assertIsNotNone(first["page"]["next_cursor"])
        second = self.control.continue_page(
            first["page"]["next_cursor"], endpoint="state",
        )
        self.assertEqual(len(second["page"]["items"]), 1)
        self.assertEqual(second["page"]["section"], "city_citizens")
        for section in v2_control._CITY_STATE_SECTIONS:
            with self.subTest(section=section), self.assertRaisesRegex(
                V2ControlError, "invalid_request",
            ):
                self.control.state_page(current, section)
        foreign = V2SeatControl("game_test", "other_agent", 1)
        foreign_city = foreign.state_page(
            current, "cities",
        )["page"]["items"][0]["id"]
        with self.assertRaisesRegex(V2ControlError, "invalid_request"):
            self.control.state_page(
                current, "city_detail", actor_id=foreign_city,
            )

    def test_city_detail_and_citizens_project_exact_output_telemetry(self):
        rows = list(complete_v2_rows(valid_rows(actions=False)))
        city_telemetry = (
            "citizen_happy=1 citizen_content=1 citizen_unhappy=0 "
            "citizen_angry=0 citizen_workers=0 citizen_specialists=2 "
            "food_stock=7 granary_size=20 growth_turns=1000000000 "
            "pollution=2 "
            "food_citizen_base=2 food_net=5 food_surplus=3 food_usage=2 "
            "food_waste=1 food_unhappy_penalty=1 "
            "shield_citizen_base=1 shield_net=2 shield_surplus=2 "
            "shield_usage=0 shield_waste=1 shield_unhappy_penalty=0 "
            "trade_citizen_base=1 trade_net=0 trade_surplus=-1 "
            "trade_usage=1 trade_waste=2 trade_unhappy_penalty=1 "
            "gold_citizen_base=2 gold_net=2 gold_surplus=2 gold_usage=0 "
            "gold_waste=0 gold_unhappy_penalty=0 "
            "luxury_citizen_base=0 luxury_net=0 luxury_surplus=0 "
            "luxury_usage=0 luxury_waste=0 luxury_unhappy_penalty=0 "
            "science_citizen_base=4 science_net=4 science_surplus=4 "
            "science_usage=0 science_waste=0 science_unhappy_penalty=0"
        )
        for index, row in enumerate(rows):
            if row.startswith("city ref=c:20:200 "):
                rows[index] = re.sub(
                    r"citizen_happy=.*$", city_telemetry, row, count=1,
                )
            elif row.startswith("city_tile city=c:20:200 "):
                rows[index] = re.sub(
                    r"food=.*$",
                    "food=2 shields=1 trade=1 gold=0 luxury=0 science=0",
                    row,
                    count=1,
                )
            elif row.startswith("city_specialist city=c:20:200 "):
                rows[index] = re.sub(
                    r"food=.*$",
                    "food=0 shields=0 trade=0 gold=1 luxury=0 science=2",
                    row,
                    count=1,
                )
        current = observation(tuple(rows))
        summary = self.control.state_page(
            current, "cities",
        )["page"]["items"][0]
        self.assertNotIn("outputs", summary)
        city_id = summary["id"]
        detail = self.control.state_page(
            current, "city_detail", actor_id=city_id,
        )["page"]["items"][0]
        self.assertEqual(detail["citizens"], {
            "happy": 1, "content": 1, "unhappy": 0, "angry": 0,
            "workers": 0, "specialists": 2,
        })
        self.assertEqual(detail["food_storage"], {
            "stock": 7, "granary_size": 20, "growth_turns": None,
        })
        self.assertEqual(detail["pollution"], 2)
        self.assertEqual(detail["outputs"]["food"], {
            "citizen_base": 2, "net": 5, "surplus": 3, "usage": 2,
            "waste": 1, "unhappy_penalty": 1, "gross": 7,
        })
        self.assertEqual(detail["outputs"]["shields"]["gross"], 3)
        citizens = self.control.state_page(
            current, "city_citizens", actor_id=city_id,
        )["page"]["items"]
        tile = next(item for item in citizens if item["kind"] == "tile")
        specialist = next(
            item for item in citizens if item["kind"] == "specialist"
        )
        self.assertEqual(tile["yields"], {
            "food": 2, "shields": 1, "trade": 1,
            "gold": 0, "luxury": 0, "science": 0,
        })
        self.assertEqual(specialist["yields"]["science"], 2)

        malformed = tuple(
            row.replace("food_citizen_base=2", "food_citizen_base=3")
            if row.startswith("city ref=c:20:200 ") else row
            for row in rows
        )
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            V2SeatControl(
                "game_test", "bad_city_telemetry", 1,
            ).state_page(observation(malformed))

    def test_superspecialist_yield_does_not_increase_citizen_population(self):
        rows = list(complete_v2_rows(valid_rows(actions=False)))
        for index, row in enumerate(rows):
            if row.startswith("city ref=c:20:200 "):
                rows[index] = (
                    row.replace(
                        "specialist_type_count=1",
                        "specialist_type_count=2",
                    )
                    .replace("food_citizen_base=0", "food_citizen_base=-3")
                    .replace(
                        "shield_citizen_base=0",
                        "shield_citizen_base=2",
                    )
                    .replace(
                        "luxury_citizen_base=0",
                        "luxury_citizen_base=1",
                    )
                    .replace(
                        "science_citizen_base=0",
                        "science_citizen_base=-1",
                    )
                )
        rows.append(
            "city_specialist city=c:20:200 specialist=1 name=Einstein "
            "count=1 counts_toward_population=0 can_use=0 is_default=0 "
            "food=-3 shields=2 trade=0 gold=0 luxury=1 science=-1"
        )
        current = observation(tuple(sorted(rows)))
        overview = self.control.state_page(
            current, "overview",
        )["page"]["items"][0]
        self.assertEqual(overview["counts"]["city_citizens"], 3)
        city = self.control.state_page(
            current, "cities",
        )["page"]["items"][0]
        detail = self.control.state_page(
            current, "city_detail", actor_id=city["id"],
        )["page"]["items"][0]
        self.assertEqual(detail["size"], 2)
        self.assertEqual(detail["citizens"]["specialists"], 2)
        self.assertEqual(detail["outputs"]["food"]["citizen_base"], -3)
        self.assertEqual(detail["outputs"]["science"]["citizen_base"], -1)
        specialists = [
            item for item in self.control.state_page(
                current, "city_citizens", actor_id=city["id"],
            )["page"]["items"]
            if item["kind"] == "specialist"
        ]
        self.assertEqual(len(specialists), 2)
        self.assertEqual(sum(
            item["count"] for item in specialists
            if item["counts_toward_population"]
        ), 2)
        superspecialist = next(
            item for item in specialists
            if not item["counts_toward_population"]
        )
        self.assertEqual(superspecialist["count"], 1)
        self.assertEqual(superspecialist["yields"]["food"], -3)

    def test_local_tile_scope_projects_extras_without_enlarging_known_tiles(self):
        current = observation(valid_rows(actions=False))
        known = self.control.state_page(
            current, "known_tiles",
        )["page"]["items"]
        self.assertTrue(all(
            not {"resource", "label", "yields", "extras"}.intersection(tile)
            for tile in known
        ))
        center_id = next(tile["id"] for tile in known if tile["x"] == 1)
        request = self.control.prepare_state_scope(
            current, "tile_window", center_id=center_id, radius=1,
        )
        local_rows = (
            "tile_local index=5 x=1 y=2 known=2 terrain=Grassland "
            "owner=p:1:10 placing_extra=-1 placing_extra_name=none "
            "placing_turns=0 placing_time=1 resource_extra=7 "
            "resource_name=Wheat has_label=1 label=Farm%20plot "
            "food=-2 shields=1 trade=1",
            "tile_extra tile=5 extra=3 name=Road cause_mask=4",
            "tile_extra tile=5 extra=7 name=Wheat cause_mask=256",
            "tile_local index=6 x=2 y=2 known=1 terrain=Plains "
            "owner=p:2:20 placing_extra=-1 placing_extra_name=none "
            "placing_turns=0 placing_time=1 resource_extra=-1 "
            "resource_name=none has_label=0 label=none "
            "food=1 shields=2 trade=0",
            "tile_extra tile=6 extra=9 name=Ancient%20Fort cause_mask=0",
            "tile_local index=8 x=0 y=2 known=0 terrain=unknown "
            "owner=none placing_extra=-1 placing_extra_name=none "
            "placing_turns=0 placing_time=-1 resource_extra=-1 "
            "resource_name=none has_label=0 label=none "
            "food=-1 shields=-1 trade=-1",
        )
        mismatched = tuple(
            row.replace("name=Wheat cause_mask=256", "name=Fish cause_mask=256")
            for row in local_rows
        )
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            self.control.materialize_state_scope(
                request, state_scope_catalog(request, mismatched),
            )
        page = self.control.materialize_state_scope(
            request, state_scope_catalog(request, local_rows),
        )
        self.assertEqual(page["page"]["total_items"], 3)
        self.assertEqual(len(local_rows), 6)
        center = page["page"]["items"][0]
        self.assertEqual(center["label"], "Farm plot")
        self.assertEqual(center["yields"], {
            "food": -2, "shields": 1, "trade": 1,
        })
        resource = next(
            extra for extra in center["extras"]
            if extra["name"] == "Wheat"
        )
        self.assertEqual(resource["causes"], ["resource"])
        self.assertEqual(center["resource"]["extra_id"], resource["extra_id"])
        remembered = next(
            item for item in page["page"]["items"]
            if item["visibility"] == "remembered"
        )
        self.assertEqual(remembered["extras"][0]["causes"], ["special"])
        unknown = next(
            item for item in page["page"]["items"]
            if item["visibility"] == "unknown"
        )
        self.assertEqual(set(unknown), {
            "id", "x", "y", "visibility", "distance",
        })

    def test_tile_window_wraps_uses_topology_and_redacts_unknown_tiles(self):
        rows = tuple(sorted(rows_with_unknown_moves() + (
            "tile index=15 x=15 y=2 known=2 terrain=Grassland owner=none",
        )))
        current = observation(rows)
        tiles = self.control.state_page(
            current, "known_tiles",
        )["page"]["items"]
        center_id = next(item["id"] for item in tiles if item["x"] == 15)
        window = self.control.state_page(
            current, "tile_window", center_id=center_id, radius=1,
        )["page"]["items"]
        self.assertEqual(window[0]["distance"], 0)
        wrapped_unknown = next(
            item for item in window if item["visibility"] == "unknown"
        )
        self.assertEqual((wrapped_unknown["x"], wrapped_unknown["y"]), (0, 2))
        self.assertEqual(set(wrapped_unknown), {
            "id", "x", "y", "visibility", "distance",
        })
        with self.assertRaisesRegex(V2ControlError, "invalid_request"):
            self.control.state_page(
                current, "tile_window", center_id="tile_" + "0" * 32,
                radius=1,
            )

        hex_rows = list(valid_rows(actions=False))
        hex_rows[hex_rows.index(next(
            row for row in hex_rows if row.startswith("meta ")
        ))] = next(
            row for row in hex_rows if row.startswith("meta ")
        ).replace("topology=square", "topology=hex").replace(
            "wrap_x=1", "wrap_x=0",
        )
        hex_rows.extend((
            "tile index=30 x=0 y=0 known=2 terrain=Grassland owner=none",
            "tile index=31 x=1 y=1 known=2 terrain=Grassland owner=none",
        ))
        hex_control = V2SeatControl("game_test", "hex_agent", 1)
        hex_current = observation(tuple(sorted(hex_rows)))
        hex_tiles = hex_control.state_page(
            hex_current, "known_tiles",
        )["page"]["items"]
        hex_center = next(item["id"] for item in hex_tiles if item["x"] == 0)
        radius_one = hex_control.state_page(
            hex_current, "tile_window", center_id=hex_center, radius=1,
        )["page"]["items"]
        self.assertFalse(any(
            item["x"] == 1 and item["y"] == 1 for item in radius_one
        ))
        radius_two = hex_control.state_page(
            hex_current, "tile_window", center_id=hex_center, radius=2,
        )["page"]["items"]
        diagonal = next(
            item for item in radius_two if item["x"] == 1 and item["y"] == 1
        )
        self.assertEqual(diagonal["distance"], 2)

    def test_map_distance_covers_all_topologies_and_wrap_axes(self):
        expected = {
            "square": ((8, 7), (8, 5), (8, 5)),
            "isometric_square": ((8, 7), (5, 6), (5, 6)),
            "hex": ((9, 7), (5, 6), (5, 6)),
            "isometric_hex": ((8, 7), (7, 7), (7, 7)),
        }
        wrap_modes = ((True, False), (False, True), (True, True))
        for topology, distances in expected.items():
            for (wrap_x, wrap_y), pair in zip(wrap_modes, distances):
                with self.subTest(
                    topology=topology, wrap_x=wrap_x, wrap_y=wrap_y,
                ):
                    meta = {
                        "map_width": 16, "map_height": 12,
                        "topology": topology,
                        "wrap_x": wrap_x, "wrap_y": wrap_y,
                    }
                    self.assertEqual(
                        V2SeatControl._map_distance(meta, 0, 0, 8, 1),
                        pair[0],
                    )
                    self.assertEqual(
                        V2SeatControl._map_distance(meta, 0, 0, 0, 7),
                        pair[1],
                    )

    def test_compact_entity_catalog_exceeds_legacy_8192_limit(self):
        rows = valid_rows(actions=False)
        sites = tuple(
            f"city_site ref=c:{1000 + index}:{2000 + index} "
            f"owner=p:2:20 name=Site{index} tile=5 x=1 y=2 size=1 "
            "visibility=visible"
            for index in range(8199)
        )
        bundled = compact_bundle(self.control, tuple(sorted(rows + sites)))
        page = self.control.state_page(bundled, "city_sites", limit=16)
        self.assertEqual(page["page"]["total_items"], 8200)
        self.assertEqual(len(page["page"]["items"]), 16)
        self.assertIsNotNone(page["page"]["next_cursor"])

    def test_compact_entity_catalog_synthesizes_removed_unit_tombstone(self):
        first = compact_bundle(self.control, valid_rows(), revision=31)
        unit = self.control.state_page(first, "units")["page"]["items"][0]
        second_rows = tuple(
            row for row in valid_rows(actions=False)
            if not row.startswith("unit ")
        )
        second = compact_bundle(self.control, second_rows, revision=32)
        self.assertEqual(
            self.control.state_page(second, "units")["page"]["items"], [],
        )
        tombstones = self.control.state_page(
            second, "tombstones",
        )["page"]["items"]
        self.assertIn({"id": unit["id"], "type": "unit"}, tombstones)

    def test_unit_actor_catalog_hydrates_exact_target_tiles_past_radius_eight(self):
        full_rows = (*(
            row.replace("index=5 x=1 y=2", "index=33 x=1 y=2")
               .replace("tile=5 ", "tile=33 ")
               .replace("target_tile=5 ", "target_tile=33 ")
               .replace("index=6 x=2 y=2", "index=34 x=2 y=2")
               .replace("tile=6 ", "tile=34 ")
               .replace("target_tile=6 ", "target_tile=34 ")
               .replace("index=8 x=0 y=2", "index=32 x=0 y=2")
               .replace("target_tile=8 ", "target_tile=32 ")
            for row in rows_with_unknown_moves()
        ),
            "tile index=42 x=10 y=2 known=2 terrain=Grassland owner=none",
            "tile index=43 x=11 y=2 known=2 terrain=Plains owner=none",
            _action(
                149, "unit.teleport", "u:10:100", 42,
                "Teleport", "Tile", "Teleport", 0,
                target_name="destination",
            ),
            _action(
                150, "unit.teleport", "u:10:100", 43,
                "Teleport2", "Tile", "Teleport", 0,
                target_name="destination",
            ),
        )
        unit_actions = tuple(
            row for row in full_rows
            if row.startswith("action ") and " actor=u:10:100 " in row
        )
        global_rows = tuple(
            row for row in full_rows
            if row not in unit_actions and " known=0 " not in f" {row} "
        )
        bundled = compact_bundle(self.control, global_rows, revision=41)
        unit_id = self.control.state_page(
            bundled, "units",
        )["page"]["items"][0]["id"]
        support = self.control.prepare_unit_support_scopes(
            bundled, unit_id,
        )[0]
        self.assertIsNone(support.radius)
        self.assertEqual(support.section, "target_tiles")
        self.assertEqual(support.selector, "u:10:100")
        tile_rows = tuple(
            row for row in full_rows
            if row.startswith("tile ")
            and any(f"index={index} " in row for index in (32, 33, 34, 42, 43))
        )
        forged_rows = tuple(
            row.replace("index=42 x=10 y=2", "index=42 x=9 y=2")
            for row in tile_rows
        )
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            self.control.hydrate_state_scope(
                support, state_scope_catalog(support, forged_rows),
            )
        self.control.hydrate_state_scope(
            support, state_scope_catalog(support, tile_rows),
        )
        actor_request = self.control.prepare_actor_scope(bundled, unit_id)
        page = self.control.materialize_actor_scope(
            actor_request, actor_scope_catalog(actor_request, unit_actions),
        )
        unknown_moves = [
            item for item in page["page"]["items"]
            if item["subject"]["target"] is not None
            and item["subject"]["target"].get("x") == 0
            and item["subject"]["target"].get("y") == 2
        ]
        self.assertEqual(len(unknown_moves), 3)
        for move in unknown_moves:
            self.assertEqual(move["subject"]["operation"], "move")
            self.assertEqual(move["subject"]["legality"], "possibly_legal")
            self.assertEqual(move["subject"]["target"]["visibility"], "unknown")
            self.assertNotIn("terrain", move["subject"]["target"])
            self.assertNotIn("owner_player_id", move["subject"]["target"])
        distant_targets = {
            (item["subject"]["target"]["x"], item["subject"]["target"]["y"])
            for item in page["page"]["items"]
            if item["subject"]["operation"] == "teleport"
        }
        self.assertTrue({(10, 2), (11, 2)}.issubset(distant_targets))

    def test_relation_clause_state_catalog_accepts_exact_40000_row_cap(self):
        clause_count = 40000
        digest_rows = tuple({
            "giver_ref": "p:1:10", "native_type": 1,
            "native_value": index,
        } for index in range(clause_count))
        digest = v2_control._diplomacy_clauses_digest(digest_rows)
        rows = list(valid_rows(actions=False))
        diplomacy_index = next(
            index for index, row in enumerate(rows)
            if row.startswith("diplomacy ")
        )
        rows[diplomacy_index] = (
            rows[diplomacy_index]
            .replace("can_meet=1 meeting=0 generation=0", (
                "can_meet=0 meeting=1 generation=3"
            ))
            .replace(
                "clause_count=0 clauses_digest=fnv1a64-cbf29ce484222325",
                f"clause_count={clause_count} clauses_digest={digest}",
            )
        )
        clause_rows = tuple(
            "diplomacy_clause other=p:2:20 generation=3 "
            f"position={index} giver=p:1:10 type=Gold value_kind=gold "
            f"value={index} name=gold"
            for index in range(clause_count)
        )
        bundled = compact_bundle(
            self.control, tuple(sorted(tuple(rows) + clause_rows)), revision=51,
        )
        relation_id = self.control.state_page(
            bundled, "diplomacy",
        )["page"]["items"][0]["relation_id"]
        request = self.control.prepare_state_scope(
            bundled, "diplomacy_clauses", relation_id=relation_id,
        )
        over_cap = state_scope_catalog(request, ())
        over_cap.update({
            "count": clause_count + 1,
            "total_count": clause_count + 1,
            "next_offset": clause_count + 1,
        })
        with self.assertRaisesRegex(V2ControlError, "scope_too_large"):
            self.control.materialize_state_scope(request, over_cap)
        page = self.control.materialize_state_scope(
            request, state_scope_catalog(request, clause_rows),
        )
        self.assertEqual(page["page"]["total_items"], clause_count)
        self.assertEqual(len(page["page"]["items"]), 16)
        self.assertEqual(
            page["page"]["items"][0]["position"], 0,
        )
        self.assertIsNotNone(page["page"]["next_cursor"])

    def test_relation_overlay_admission_is_bounded_atomic_and_lru(self):
        snapshot = self.control._snapshot(observation())

        def publish(selector: str, ordinal: int) -> tuple[int, str]:
            request = v2_control.V2StateScopeRequest(
                section="diplomacy_clauses",
                selector=selector,
                native_revision=snapshot.native_revision,
                limit=16,
                relation_id="relation_" + f"{ordinal:032x}",
            )
            self.control._publish_state_scope_private(
                snapshot, request,
                ({"position": ordinal, "name": f"clause-{ordinal}"},),
                (),
                ({"native_type": ordinal, "native_value": ordinal},),
            )
            return snapshot.native_revision, selector

        with mock.patch.object(v2_control, "MAX_RELATION_OVERLAY_ENTRIES", 2):
            first = publish("p:2:20", 1)
            second = publish("p:3:30", 2)
            third = publish("p:4:40", 3)
        self.assertNotIn(first, self.control._relation_overlay_charges)
        self.assertEqual(
            tuple(self.control._relation_overlay_charges), (second, third),
        )
        self.assertEqual(
            self.control._relation_overlay_bytes,
            sum(self.control._relation_overlay_charges.values()),
        )

        before_charges = self.control._relation_overlay_charges.copy()
        before_bytes = self.control._relation_overlay_bytes
        with mock.patch.object(v2_control, "MAX_RELATION_OVERLAY_BYTES", 1):
            with self.assertRaisesRegex(V2ControlError, "scope_too_large"):
                publish("p:5:50", 4)
        self.assertEqual(self.control._relation_overlay_charges, before_charges)
        self.assertEqual(self.control._relation_overlay_bytes, before_bytes)

    def test_unknown_tile_is_topology_only_and_supports_only_move_variants(self):
        unknown = observation(rows_with_unknown_moves())
        state = self.control.state_page(unknown, "known_tiles")
        tile = next(
            item for item in state["page"]["items"]
            if item["visibility"] == "unknown"
        )
        self.assertEqual(tuple(tile), ("id", "x", "y", "visibility"))
        self.assertEqual((tile["x"], tile["y"]), (0, 2))
        self.assertNotIn("terrain", tile)
        self.assertNotIn("owner_player_id", tile)

        legal_payload = self.control.legal_actions_page(unknown)
        legal = legal_payload["page"]["items"]
        if legal_payload["page"]["next_cursor"] is not None:
            legal.extend(self.control.continue_page(
                legal_payload["page"]["next_cursor"],
                endpoint="legal_actions",
            )["page"]["items"])
        moves = [
            item for item in legal
            if item["subject"]["target"] is not None
            and item["subject"]["target"]["id"] == tile["id"]
        ]
        self.assertEqual(len(moves), 3)
        self.assertEqual(
            {item["subject"]["variant"] for item in moves},
            {"standard", "alternative_2", "alternative_3"},
        )
        for move in moves:
            self.assertEqual(move["kind"], "unit.order")
            self.assertEqual(move["subject"]["operation"], "move")
            self.assertEqual(move["subject"]["legality"], "possibly_legal")
            self.assertEqual(move["subject"]["probability"], {
                "kind": "unknown",
                "minimum_percent": 0.0,
                "maximum_percent": 100.0,
            })
            self.assertEqual(
                set(move["subject"]["target"]), {"type", "id", "x", "y"},
            )

        public = json.dumps([state, legal], sort_keys=True)
        for private in (
            "index=8", '"native_index"', '"owner_ref"',
            '"terrain": "unknown"', "p:1:10", "u:10:100",
        ):
            self.assertNotIn(private, public)

    def test_unknown_tile_contract_rejects_hidden_data_or_non_move_capabilities(self):
        rows = rows_with_unknown_moves()
        unknown_actions = tuple(
            row for row in rows
            if row.startswith("action ") and " target_tile=8 " in row
        )
        without_unknown_actions = tuple(
            row for row in rows if row not in unknown_actions
        )
        attack = _action(
            13, "unit.attack", "u:10:100", 8, "Attack", "Stack",
            "Unit Attack", 0,
            legality="possibly_legal", probability_kind="unknown",
            probability_min=0, probability_max=200,
        )
        found = _action(
            14, "city.found", "u:10:100", 8, "Found City", "Tile",
            "Unit Found City", 1, "city_name-required",
            legality="possibly_legal", probability_kind="unknown",
            probability_min=0, probability_max=200,
        )
        hostile = {
            "known_value": replace_row(rows, "known=0", "known=3"),
            "terrain": replace_row(
                rows, "known=0 terrain=unknown", "known=0 terrain=Grassland",
            ),
            "owner": replace_row(
                rows, "terrain=unknown owner=none",
                "terrain=unknown owner=p:2:20",
            ),
            "encoded_sentinel": replace_row(
                rows, "terrain=unknown", "terrain=%75nknown",
            ),
            "orphan": without_unknown_actions,
            "entity_placement": replace_row(
                rows,
                "converts_to=none tile=5 x=1 y=2",
                "converts_to=none tile=8 x=0 y=2",
            ),
            "city_placement": replace_row(
                rows, "name=Alpha%20Centauri tile=5 x=1 y=2",
                "name=Alpha%20Centauri tile=8 x=0 y=2",
            ),
            "visible_unit_placement": replace_row(
                rows, "type=Warriors tile=6 x=2 y=2",
                "type=Warriors tile=8 x=0 y=2",
            ),
            "actor_without_moves": replace_row(
                rows, "hp=10 moves=3 activity=idle",
                "hp=10 moves=0 activity=idle",
            ),
            "attack": tuple(sorted(rows + (attack,))),
            "found_city": tuple(sorted(rows + (found,))),
            "exact_probability": replace_row(
                rows,
                "legality=possibly_legal probability_kind=unknown "
                "probability_min=0 probability_max=200",
                "legality=legal probability_kind=exact "
                "probability_min=200 probability_max=200",
            ),
            "range_probability": replace_row(
                rows,
                "probability_kind=unknown probability_min=0 "
                "probability_max=200",
                "probability_kind=range probability_min=1 "
                "probability_max=199",
            ),
            "not_implemented_probability": replace_row(
                rows,
                "legality=possibly_legal probability_kind=unknown "
                "probability_min=0 probability_max=200",
                "legality=unresolved probability_kind=not_implemented "
                "probability_min=-1 probability_max=-1",
            ),
        }
        for name, malformed in hostile.items():
            with self.subTest(name=name):
                self.control = V2SeatControl("game_test", "agent_test", 1)
                self.assertInternal(malformed)

    def test_unknown_move_resolves_to_the_existing_native_server_validated_slot(self):
        current = observation(rows_with_unknown_moves())
        revision = self.control.legal_actions_page(current)["state_revision"]
        actions = self.control.legal_actions_page(current)["page"]["items"]
        unknown_move = next(
            action for action in actions
            if action["subject"]["operation"] == "move"
            and action["subject"]["probability"]["kind"] == "unknown"
        )
        resolved = self.control.resolve_action(
            current, revision, unknown_move["action_id"], {},
        )
        self.assertIn(
            resolved.native_slot,
            {"a0000000000000014", "a0000000000000015", "a0000000000000016"},
        )
        self.assertEqual(resolved.native_arguments, "-")
        self.assertEqual(resolved.public_kind, "unit.order")
        self.assertEqual(resolved.operation, "move")

    def test_unknown_topology_is_bounded_to_eight_targets_per_actor(self):
        rows = list(valid_rows())
        for offset in range(9):
            tile_index = 20 + offset
            rows.append(
                f"tile index={tile_index} x={offset} y=9 known=0 "
                "terrain=unknown owner=none"
            )
            rows.append(_action(
                20 + offset, "unit.move", "u:10:100", tile_index,
                "Unit Move", "Tile", "Unit Move", 0,
                legality="possibly_legal", probability_kind="unknown",
                probability_min=0, probability_max=200,
            ))
        self.assertInternal(tuple(sorted(rows)))

    def test_public_projection_recursively_contains_no_native_tokens_or_fields(self):
        current = observation()
        payloads = [
            self.control.state_page(current, section)
            for section in (
                "overview", "research", "diplomacy", "known_tiles", "cities",
                "city_sites", "units", "tombstones",
            )
        ]
        city_id = payloads[4]["page"]["items"][0]["id"]
        payloads.extend(
            self.control.state_page(current, section, actor_id=city_id)
            for section in v2_control._CITY_STATE_SECTIONS
        )
        payloads.append(self.control.legal_actions_page(current))
        public = json.dumps(payloads, sort_keys=True)
        for secret in (
            "a0000000000000001", "u:10:100", "u:11:101", "p:1:10",
            "p:2:20", "c:20:200", "native_rule", "target_tile", "target_kind",
            "actor_consuming_always", "probability_min", "probability_max",
            "activity_target", "target_build", "target_extra",
            '"slot"', '"result"',
        ):
            with self.subTest(secret=secret):
                self.assertNotIn(secret, public)
        snapshot = self.control._snapshots[11]
        self.assertIsInstance(snapshot.sections, MappingProxyType)
        with self.assertRaises(TypeError):
            snapshot.sections["units"] = ()
        # A native slot is retained only in the private action binding.
        self.assertTrue(all(
            binding.slot.startswith("a")
            for binding in snapshot.action_bindings.values()
        ))

    def test_unknown_missing_extra_reordered_and_duplicate_rows_fail_closed(self):
        rows = valid_rows()
        cases = {
            "unknown": tuple(sorted(rows + ("mystery value=1",))),
            "missing": replace_row(rows, " cache=human-client", ""),
            "extra": replace_row(rows, " cache=human-client", " cache=human-client extra=1"),
            "reordered": replace_row(
                rows,
                "state=running turn=7 phase=1 cache=human-client",
                "turn=7 state=running phase=1 cache=human-client",
            ),
            "duplicate": tuple(sorted(rows + (rows[0],))),
        }
        for name, hostile in cases.items():
            with self.subTest(name=name):
                self.control = V2SeatControl("game_test", "agent_test", 1)
                self.assertInternal(hostile)

    def test_noncanonical_integers_percent_escapes_utf8_and_controls_fail(self):
        rows = valid_rows()
        cases = (
            replace_row(rows, "turn=7", "turn=07"),
            replace_row(rows, "name=Codex", "name=%43odex"),
            replace_row(rows, "name=Codex", "name=Codex%2fAgent"),
            replace_row(rows, "name=Codex", "name=Codex%0AAgent"),
            replace_row(rows, "name=Codex", "name=%FF"),
            replace_row(rows, "phase_done=0", "phase_done=false"),
            replace_row(rows, "alive=1", "alive=true"),
            replace_row(rows, "phase_ready=1", "phase_ready=true"),
        )
        for hostile in cases:
            self.control = V2SeatControl("game_test", "agent_test", 1)
            self.assertInternal(hostile)

    def test_observation_and_native_row_bounds_are_strict(self):
        invalid_observations = (
            {"generation": 1, "native_revision": 11, "rows": []},
            {"generation": 1, "native_revision": 11, "rows": (), "extra": 1},
            {"generation": True, "native_revision": 11, "rows": valid_rows()},
            {"generation": 1, "native_revision": 0, "rows": valid_rows()},
            {"generation": 1, "native_revision": -1, "rows": valid_rows()},
            {"generation": 1, "native_revision": 11, "rows": ("x" * 768,)},
            {
                "generation": 1,
                "native_revision": 11,
                "rows": tuple(f"meta-{index:04d} value=1" for index in range(8193)),
            },
        )
        for hostile in invalid_observations:
            with self.subTest(hostile_type=type(hostile.get("rows"))):
                with self.assertRaisesRegex(V2ControlError, "internal_error"):
                    self.control.state_page(hostile)

    def test_bad_refs_enums_cross_links_and_action_contracts_fail(self):
        rows = valid_rows()
        cases = (
            replace_row(rows, "player ref=p:1:10", "player ref=u:1:10"),
            replace_row(rows, "state=running", "state=paused"),
            replace_row(rows, "known=1", "known=0"),
            replace_row(rows, "index=6 x=2 y=2 known=2", "index=6 x=2 y=2 known=1"),
            replace_row(rows, "state=Peace", "state=Friends"),
            replace_row(rows, "city ref=c:20:200", "city ref=c:20:0"),
            replace_row(rows, "tile=5 x=1 y=2 size=2", "tile=6 x=1 y=2 size=2"),
            replace_row(rows, "buy_cost=30 can_buy=1", "buy_cost=50 can_buy=1"),
            replace_row(rows, "buy_cost=30 can_buy=1", "buy_cost=0 can_buy=1"),
            replace_row(
                rows,
                "activity=idle activity_target=-1 activity_target_name=none",
                "activity=idle activity_target=7 activity_target_name=Road",
            ),
            replace_row(
                rows,
                "activity=idle activity_target=-1 activity_target_name=none",
                "activity=road activity_target=-1 activity_target_name=none",
            ),
            replace_row(
                rows,
                "scope=visible owner=p:2:20",
                "scope=visible owner=p:1:10",
            ),
            replace_row(rows, "terrain=Plains owner=p:2:20", "terrain=Plains owner=p:2:21"),
            replace_row(rows, "kind=unit.move actor=u:10:100", "kind=unit.move actor=u:11:101"),
            replace_row(rows, "native_rule=Unit%20Move ", "native_rule=Teleport "),
            replace_row(rows, "target_kind=Stack result=Unit%20Attack", "target_kind=Tile result=Unit%20Attack"),
            replace_row(rows, "probability_kind=exact probability_min=200", "probability_kind=range probability_min=200"),
            replace_row(rows, "target_id=4", "target_id=99"),
            replace_row(rows, "goal_id=5", "goal_id=99"),
            replace_row(
                rows, "id=6 name=Bronze%20Working state=available",
                "id=6 name=Bronze%20Working state=known",
            ),
            replace_row(
                rows, "target_tech=6 vote_no=-1 target_government=-1 max_rate=0 ",
                "target_tech=5 vote_no=-1 target_government=-1 max_rate=0 ",
            ),
            replace_row(
                rows, "target_tech=4 vote_no=-1 target_government=-1 max_rate=0 ",
                "target_tech=3 vote_no=-1 target_government=-1 max_rate=0 ",
            ),
            replace_row(
                rows, "changeable_tax=1 max_rate=70",
                "changeable_tax=0 max_rate=70",
            ),
            replace_row(
                rows, "target_tech=-1 vote_no=-1 target_government=-1 max_rate=70 ",
                "target_tech=-1 vote_no=-1 target_government=-1 max_rate=60 ",
            ),
        )
        for hostile in cases:
            self.control = V2SeatControl("game_test", "agent_test", 1)
            self.assertInternal(hostile)

    def test_government_catalog_cross_links_status_and_cap_are_strict(self):
        rows = valid_rows()
        without_governments = tuple(
            row for row in rows if not row.startswith("government ")
        )
        catalog_127 = [
            (
                f"government id={native_id} "
                f"name={'Anarchy' if native_id == 0 else 'Despotism' if native_id == 1 else f'Gov{native_id}'} "
                f"current={1 if native_id == 1 else 0} target=0 "
                f"during={1 if native_id == 0 else 0} "
                f"can_change={1 if native_id >= 2 else 0}"
            )
            for native_id in range(127)
        ]
        bounded = tuple(sorted(
            replace_row(
                without_governments, "choices_count=4", "choices_count=127",
            ) + tuple(catalog_127)
        ))
        page = self.control.state_page(
            observation(bounded), "governments", limit=16,
        )["page"]
        self.assertEqual(page["total_items"], 127)
        self.assertEqual(len(page["items"]), 16)

        catalog_128 = catalog_127 + [
            "government id=127 name=Gov127 current=0 target=0 during=0 "
            "can_change=1"
        ]
        oversized = tuple(sorted(
            replace_row(
                without_governments, "choices_count=4", "choices_count=128",
            ) + tuple(catalog_128)
        ))
        malformed = (
            oversized,
            tuple(row for row in rows if not row.startswith("government id=3 ")),
            replace_row(rows, "name=Republic", "name=Monarchy"),
            replace_row(rows, "id=3 name=Republic", "id=4 name=Republic"),
            replace_row(rows, "id=2 name=Monarchy current=0", "id=2 name=Monarchy current=1"),
            replace_row(rows, "id=2 name=Monarchy current=0 target=0 during=0", "id=2 name=Monarchy current=0 target=0 during=1"),
            replace_row(rows, "current_id=1", "current_id=99"),
            replace_row(rows, "target_id=-1", "target_id=2"),
            replace_row(rows, "status=stable", "status=anarchy"),
            replace_row(rows, "choices_count=4", "choices_count=3"),
            replace_row(rows, "id=1 name=Despotism current=1 target=0 during=0 can_change=0", "id=1 name=Despotism current=1 target=0 during=0 can_change=1"),
            replace_row(rows, "method=random max_turns=5 untargeted_allowed=1", "method=quickening max_turns=5 untargeted_allowed=1"),
            replace_row(rows, "untargeted_allowed=1 no_anarchy=0 can_revolution=1", "untargeted_allowed=0 no_anarchy=0 can_revolution=1"),
            replace_row(rows, "finish_turn=-1", "finish_turn=7"),
            replace_row(rows, "no_anarchy=0", "no_anarchy=1"),
        )
        for hostile in malformed:
            self.control = V2SeatControl("game_test", "agent_test", 1)
            self.assertInternal(hostile)

    def test_all_four_revolution_methods_project_with_exact_targetless_flag(self):
        for method, untargeted, can_revolution in (
            ("fixed", 1, 1),
            ("random", 1, 1),
            ("quickening", 0, 0),
            ("random_quickening", 0, 0),
        ):
            with self.subTest(method=method):
                rows = replace_row(
                    valid_rows(),
                    "method=random max_turns=5 untargeted_allowed=1 "
                    "no_anarchy=0 can_revolution=1",
                    f"method={method} max_turns=5 "
                    f"untargeted_allowed={untargeted} no_anarchy=0 "
                    f"can_revolution={can_revolution}",
                )
                control = V2SeatControl("game_test", "agent_test", 1)
                government_state = control.state_page(
                    observation(rows),
                )["page"]["items"][0]["player"]["government_state"]
                self.assertEqual(government_state["method"], method)
                self.assertIs(
                    government_state["untargeted_allowed"], bool(untargeted),
                )
                self.assertIs(
                    government_state["can_revolution"], bool(can_revolution),
                )

    def test_research_and_rates_capability_sets_are_complete(self):
        rows = valid_rows()
        missing_target = tuple(
            row for row in rows
            if not (row.startswith("action ")
                    and " native_rule=research.set_target " in row)
        )
        missing_goal = tuple(
            row for row in rows
            if not (row.startswith("action ") and " target_tech=4 " in row
                    and " native_rule=research.set_goal " in row)
        )
        missing_rates = tuple(
            row for row in rows
            if not (row.startswith("action ")
                    and " native_rule=economy.set_rates " in row)
        )
        extra_rates = tuple(sorted(rows + (_action(
            30, "economy.set_rates", "none", -1, "economy.set_rates",
            "Player", "Economic Rates", 0, "rates-required", max_rate=70,
        ),)))
        malformed = {
            "known_current_target": replace_row(
                rows,
                "id=4 name=Writing state=available can_target=1 can_goal=1",
                "id=4 name=Writing state=known can_target=0 can_goal=0",
            ),
            "target_name_mismatch": replace_row(
                rows, "target=Writing target_id=4",
                "target=Pottery target_id=4",
            ),
            "goal_name_mismatch": replace_row(
                rows, "goal=Pottery goal_id=5", "goal=Writing goal_id=5",
            ),
            "missing_target": missing_target,
            "missing_goal": missing_goal,
            "missing_rates": missing_rates,
            "extra_rates": extra_rates,
            "invalid_can_target": replace_row(
                rows,
                "id=6 name=Bronze%20Working state=available "
                "can_target=1 can_goal=1",
                "id=6 name=Bronze%20Working state=available "
                "can_target=0 can_goal=1",
            ),
            "invalid_can_goal": replace_row(
                rows,
                "id=5 name=Pottery state=reachable can_target=0 can_goal=1",
                "id=5 name=Pottery state=reachable can_target=0 can_goal=0",
            ),
        }
        for name, malformed_rows in malformed.items():
            with self.subTest(name=name):
                self.control = V2SeatControl("game_test", "agent_test", 1)
                self.assertInternal(malformed_rows)

        inactive = valid_rows(actions=False)
        self.assertEqual(
            self.control.legal_actions_page(observation(inactive))["page"][
                "items"
            ],
            [],
        )

    def test_unset_and_future_current_research_choices_are_consistent(self):
        unset_rows = replace_row(
            valid_rows(), "goal=Pottery goal_id=5", "goal=Unset goal_id=1000",
        )
        unset_rows = tuple(sorted(
            row for row in unset_rows
            if not (row.startswith("action ") and " target_tech=1000 " in row)
        ))
        unset_rows = tuple(sorted(unset_rows + (_action(
            13, "research.set_goal", "none", -1, "research.set_goal",
            "Technology", "Research Goal", 0, target_tech=5,
        ),)))
        unset_overview = self.control.state_page(
            observation(unset_rows),
        )["page"]["items"][0]
        self.assertEqual(unset_overview["research"]["goal"], "Unset")

        future_rows = list(valid_rows())
        future_rows.append(
            "research_tech id=999 name=Future%20Tech state=future "
            "can_target=1 can_goal=1"
        )
        future_rows = list(replace_row(
            tuple(future_rows), "goal=Pottery goal_id=5",
            "goal=Future%20Tech goal_id=999", sort=False,
        ))
        future_rows = list(replace_row(
            tuple(future_rows),
            "choices_count=5 choices_digest=fnv1a64-da5a057e14a5995d",
            "choices_count=6 choices_digest=fnv1a64-429de80b9db0a93c",
            sort=False,
        ))
        future_rows.append(_action(
            15, "research.set_goal", "none", -1, "research.set_goal",
            "Technology", "Research Goal", 0, target_tech=5,
        ))
        future_rows.append(_action(
            16, "research.set_target", "none", -1,
            "research.set_target", "Technology", "Research Target", 0,
            target_tech=999,
        ))
        future_overview = self.control.state_page(
            observation(tuple(sorted(future_rows)), revision=12),
        )["page"]["items"][0]
        self.assertEqual(
            future_overview["research"]["goal"], "Future Tech",
        )

        post_loss_rows = list(valid_rows())
        post_loss_rows.append(
            "research_tech id=999 name=Future%20Tech state=future "
            "can_target=0 can_goal=0"
        )
        post_loss_rows = list(replace_row(
            tuple(post_loss_rows),
            "target=Writing target_id=4 goal=Pottery goal_id=5",
            "target=Future%20Tech target_id=999 "
            "goal=Future%20Tech goal_id=999",
            sort=False,
        ))
        post_loss_rows = list(replace_row(
            tuple(post_loss_rows),
            "choices_count=5 choices_digest=fnv1a64-da5a057e14a5995d",
            "choices_count=6 choices_digest=fnv1a64-d7d6504c02ee89d6",
            sort=False,
        ))
        post_loss_rows.append(_action(
            15, "research.set_target", "none", -1,
            "research.set_target", "Technology", "Research Target", 0,
            target_tech=4,
        ))
        post_loss_rows.append(_action(
            16, "research.set_goal", "none", -1, "research.set_goal",
            "Technology", "Research Goal", 0, target_tech=5,
        ))
        post_loss_overview = self.control.state_page(
            observation(tuple(sorted(post_loss_rows)), revision=13),
        )["page"]["items"][0]
        self.assertEqual(
            post_loss_overview["research"]["target"], "Future Tech",
        )
        self.assertEqual(
            post_loss_overview["research"]["goal"], "Future Tech",
        )

    def test_research_catalog_count_digest_and_unset_are_complete(self):
        rows = valid_rows()

        def without_tech_and_actions(native_id: int) -> tuple[str, ...]:
            return tuple(sorted(
                row for row in rows
                if not row.startswith(f"research_tech id={native_id} ")
                and not (
                    row.startswith("action ")
                    and f" target_tech={native_id} " in row
                    and " native_rule=research.set_" in row
                )
            ))

        malformed = {
            "missing_unset_and_action": without_tech_and_actions(1000),
            "missing_available_tech_and_actions": without_tech_and_actions(6),
            "changed_state": replace_row(
                rows, "id=3 name=Alphabet state=known",
                "id=3 name=Alphabet state=reachable",
            ),
            "changed_name": replace_row(
                rows, "id=3 name=Alphabet", "id=3 name=Alphabet2",
            ),
            "changed_flags": replace_row(
                rows, "id=3 name=Alphabet state=known can_target=0 can_goal=0",
                "id=3 name=Alphabet state=known can_target=0 can_goal=1",
            ),
            "changed_digest": replace_row(
                rows, "choices_digest=fnv1a64-da5a057e14a5995d",
                "choices_digest=fnv1a64-0000000000000000",
            ),
            "changed_count": replace_row(
                rows, "choices_count=5", "choices_count=4",
            ),
        }
        for name, malformed_rows in malformed.items():
            with self.subTest(name=name):
                self.control = V2SeatControl("game_test", "agent_test", 1)
                self.assertInternal(malformed_rows)

    def test_future_false_flags_require_future_to_be_current(self):
        rows = list(valid_rows())
        rows.append(
            "research_tech id=999 name=Future%20Tech state=future "
            "can_target=0 can_goal=0"
        )
        rows = list(replace_row(
            tuple(rows),
            "choices_count=5 choices_digest=fnv1a64-da5a057e14a5995d",
            "choices_count=6 choices_digest=fnv1a64-d7d6504c02ee89d6",
            sort=False,
        ))
        self.assertInternal(tuple(sorted(rows)))

    def test_actions_require_running_player_with_phase_not_done(self):
        rows = valid_rows()
        for hostile in (
            replace_row(rows, "state=running", "state=over"),
            replace_row(rows, "phase_done=0", "phase_done=1"),
            tuple(sorted(
                row for row in rows
                if not row.startswith("player ")
                and not row.startswith("research")
                and not row.startswith("diplomacy ")
                and not row.startswith("city ")
                and not row.startswith("unit ")
            )),
        ):
            self.control = V2SeatControl("game_test", "agent_test", 1)
            self.assertInternal(hostile)

    def test_dead_off_phase_and_done_players_validly_expose_no_actions(self):
        quiet = valid_rows(actions=False)
        cases = {
            "dead": replace_row(quiet, "alive=1", "alive=0"),
            "off_phase": replace_row(
                replace_row(
                    quiet,
                    "turn=7 phase=1 cache=human-client",
                    "turn=7 phase=0 cache=human-client",
                ),
                "active_phase=1", "active_phase=0",
            ),
            "done": replace_row(quiet, "phase_done=0", "phase_done=1"),
        }
        for name, rows in cases.items():
            with self.subTest(name=name):
                self.control = V2SeatControl("game_test", "agent_test", 1)
                legal = self.control.legal_actions_page(observation(rows))
                self.assertEqual(legal["page"]["items"], [])
                overview = self.control.state_page(observation(rows))["page"][
                    "items"
                ][0]
                self.assertFalse(overview["phase_ready"])
                self.assertEqual(overview["counts"]["legal_actions"], 0)

    def test_phase_action_facts_and_action_eligibility_are_coherent(self):
        active = valid_rows()
        quiet = valid_rows(actions=False)
        no_end = tuple(
            row for row in active
            if not (row.startswith("action ") and " native_rule=phase.end " in row)
        )
        two_ends = tuple(sorted(active + (
            _action(
                10, "phase.end", "none", -1, "phase.end", "player",
                "phase_end", 0,
            ),
        )))
        hostile = {
            "ready_without_end": replace_row(
                quiet, "phase_ready=0", "phase_ready=1",
            ),
            "end_without_ready": replace_row(
                active, "phase_ready=1", "phase_ready=0",
            ),
            "ready_without_phase_action": no_end,
            "two_phase_actions": two_ends,
            "dead_with_actions": replace_row(active, "alive=1", "alive=0"),
            "off_phase_with_actions": replace_row(
                replace_row(
                    active,
                    "turn=7 phase=1 cache=human-client",
                    "turn=7 phase=0 cache=human-client",
                ),
                "active_phase=1", "active_phase=0",
            ),
            "done_with_actions": replace_row(
                active, "phase_done=0", "phase_done=1",
            ),
        }
        for name, rows in hostile.items():
            with self.subTest(name=name):
                self.control = V2SeatControl("game_test", "agent_test", 1)
                self.assertInternal(rows)

    def test_phase_metadata_modes_counts_and_booleans_are_strict(self):
        quiet = valid_rows(actions=False)
        concurrent = replace_row(
            replace_row(
                replace_row(
                    quiet,
                    "phase_mode=players_alternate",
                    "phase_mode=concurrent",
                ),
                "phase_count=2", "phase_count=1",
            ),
            "turn=7 phase=1 cache=human-client",
            "turn=7 phase=0 cache=human-client",
        )
        concurrent = replace_row(
            concurrent, "phase_done=0", "phase_done=1",
        )
        concurrent = replace_row(
            concurrent, "active_phase=0", "active_phase=1",
        )
        teams = replace_row(
            quiet, "phase_mode=players_alternate", "phase_mode=teams_alternate",
        )
        for mode, rows in (("concurrent", concurrent), ("teams_alternate", teams)):
            with self.subTest(mode=mode):
                self.control = V2SeatControl("game_test", "agent_test", 1)
                overview = self.control.state_page(observation(rows))["page"][
                    "items"
                ][0]
                self.assertEqual(overview["phase_mode"], mode)

        hostile = (
            replace_row(quiet, "phase_mode=players_alternate", "phase_mode=serial"),
            replace_row(quiet, "phase_count=2", "phase_count=02"),
            replace_row(quiet, "phase_count=2", "phase_count=0"),
            replace_row(
                quiet,
                "turn=7 phase=0 cache=human-client",
                "turn=7 phase=2 cache=human-client",
            ),
            replace_row(quiet, "active_phase=0", "active_phase=true"),
            replace_row(quiet, "active_phase=0", "active_phase=1"),
            replace_row(
                concurrent, "phase_count=1", "phase_count=2",
            ),
            replace_row(
                concurrent, "active_phase=1", "active_phase=0",
            ),
        )
        for rows in hostile:
            self.control = V2SeatControl("game_test", "agent_test", 1)
            self.assertInternal(rows)

    def test_missing_player_cannot_claim_active_or_ready_phase(self):
        quiet = valid_rows(actions=False)
        no_player = tuple(sorted(
            row for row in quiet
            if not row.startswith("player ")
            and not row.startswith("governance ")
            and not row.startswith("government ")
            and not row.startswith("multiplier ")
            and not row.startswith("spaceship ")
            and not row.startswith("spaceship_structural ")
            and not row.startswith("research")
            and not row.startswith("diplomacy ")
            and not row.startswith("city ")
            and not row.startswith("city_site ")
            and not row.startswith("city_tile ")
            and not row.startswith("city_specialist ")
            and not row.startswith("city_worklist ")
            and not row.startswith("city_build_choice ")
            and not row.startswith("city_improvement ")
            and not row.startswith("city_rally ")
            and not row.startswith("unit ")
        ))
        no_player = replace_row(
            no_player, "active_phase=1", "active_phase=0",
        )
        overview = self.control.state_page(observation(no_player))["page"][
            "items"
        ][0]
        self.assertIsNone(overview["player"])
        self.assertFalse(overview["active_phase"])
        self.assertFalse(overview["phase_ready"])

        for hostile in (
            replace_row(no_player, "active_phase=0", "active_phase=1"),
            replace_row(no_player, "phase_ready=0", "phase_ready=1"),
        ):
            self.control = V2SeatControl("game_test", "agent_test", 1)
            self.assertInternal(hostile)

    def test_phase_fields_and_alive_have_exact_native_order(self):
        rows = valid_rows(actions=False)
        hostile = (
            replace_row(rows, " phase_mode=players_alternate", ""),
            replace_row(rows, " phase_ready=0", " phase_ready=0 extra=1"),
            replace_row(
                rows,
                "phase_count=2 active_phase=0",
                "active_phase=0 phase_count=2",
            ),
            replace_row(rows, " alive=1", ""),
            replace_row(
                rows, "alive=1 phase_done=0", "phase_done=0 alive=1",
            ),
        )
        for rows in hostile:
            self.control = V2SeatControl("game_test", "agent_test", 1)
            self.assertInternal(rows)

    def test_long_valid_unit_type_cannot_escape_detail_free_boundary(self):
        long_type = "X" * 300
        rows = replace_row(valid_rows(), "type=Settlers", f"type={long_type}")
        page = self.control.legal_actions_page(observation(rows))
        self.assertEqual(page["page"]["total_items"], 14)
        self.assertTrue(all(
            long_type not in item["label"] for item in page["page"]["items"]
        ))

    def test_live_entity_and_tombstone_collision_fails(self):
        rows = replace_row(valid_rows(), "tombstone ref=u:99:999", "tombstone ref=u:10:100")
        self.assertInternal(rows)

    def test_reused_unit_number_can_tombstone_old_and_expose_new_lifetime(self):
        rows = replace_row(
            valid_rows(),
            "tombstone ref=u:99:999 kind=unit",
            "tombstone ref=u:10:99 kind=unit",
        )
        unit = next(
            item for item in self.control.state_page(
                observation(rows=rows), "units",
            )["page"]["items"]
            if item["scope"] == "own"
        )
        tombstone = self.control.state_page(
            observation(rows=rows), "tombstones",
        )["page"]["items"][0]
        self.assertNotEqual(unit["id"], tombstone["id"])
        self.assertTrue(unit["id"].startswith("unit_"))
        self.assertEqual(tombstone["type"], "unit")

    def test_same_revision_is_stable_and_contradictory_content_fails(self):
        first = self.control.state_page(observation(), "units")
        second = self.control.state_page(observation(), "units")
        self.assertEqual(first, second)
        first_actions = self.control.legal_actions_page(observation())
        self.assertEqual(first_actions, self.control.legal_actions_page(observation()))

        changed = replace_row(valid_rows(), "name=Codex", "name=Claude")
        self.assertInternal(changed)

    def test_entity_ids_persist_but_state_and_action_ids_change_by_revision(self):
        first_units = self.control.state_page(observation(), "units")["page"]["items"]
        first_legal = self.control.legal_actions_page(observation())["page"]["items"]
        second_observation = observation(revision=12)
        second_units = self.control.state_page(second_observation, "units")["page"]["items"]
        second_legal = self.control.legal_actions_page(second_observation)["page"]["items"]
        self.assertEqual(
            [item["id"] for item in first_units],
            [item["id"] for item in second_units],
        )
        self.assertNotEqual(
            [item["action_id"] for item in first_legal],
            [item["action_id"] for item in second_legal],
        )
        self.assertNotEqual(
            first_legal[0]["state_revision"]["state_token"],
            second_legal[0]["state_revision"]["state_token"],
        )

    def test_evicted_revision_regression_fails_closed_with_bounded_history(self):
        self.control.state_page(observation(revision=10), "units")
        self.control.state_page(observation(revision=11))
        self.control.state_page(observation(revision=12))
        self.assertNotIn(10, self.control._snapshots)
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            self.control.state_page(observation(revision=10), "units")

    def test_cache_retains_latest_revisions_not_most_recently_read(self):
        old_cursor = self.control.state_page(
            observation(revision=10), "known_tiles", limit=1,
        )["page"]["next_cursor"]
        self.control.state_page(observation(revision=11))
        self.control.state_page(observation(revision=10))
        self.control.state_page(observation(revision=12))
        self.assertEqual(tuple(self.control._snapshots), (11, 12))
        self.assertEqual(
            self.control.continue_page(old_cursor, endpoint="state")["page"][
                "section"
            ],
            "known_tiles",
        )

    def test_close_zeros_secret_clears_state_and_rejects_future_calls(self):
        page = self.control.state_page(
            observation(), "known_tiles", limit=1,
        )
        cursor = page["page"]["next_cursor"]
        secret = self.control._secret
        self.assertTrue(self.control.has_snapshot)
        self.control.close()
        self.assertFalse(self.control.has_snapshot)
        self.assertEqual(bytes(secret), b"\0" * 32)
        self.assertEqual(self.control._snapshots, {})
        self.assertEqual(self.control._cursors, {})
        for operation in (
            lambda: self.control.state_page(observation()),
            lambda: self.control.legal_actions_page(observation()),
            lambda: self.control.continue_page(cursor, endpoint="state"),
        ):
            with self.assertRaisesRegex(V2ControlError, "sidecar_unavailable"):
                operation()
        self.control.close()

    def test_generation_zero_is_rejected(self):
        with self.assertRaisesRegex(V2ControlError, "invalid_request"):
            V2SeatControl("game_test", "agent_test", 0)

    def test_ids_are_isolated_across_controls_seats_and_generations(self):
        one = self.control.state_page(observation(), "units")
        other_seat = V2SeatControl("game_test", "agent_other", 1)
        two = other_seat.state_page(observation(), "units")
        other_generation = V2SeatControl("game_test", "agent_test", 2)
        three = other_generation.state_page(
            observation(generation=2), "units",
        )
        self.assertNotEqual(one["page"]["items"][0]["id"], two["page"]["items"][0]["id"])
        self.assertNotEqual(one["page"]["items"][0]["id"], three["page"]["items"][0]["id"])
        with self.assertRaises(V2ControlError):
            self.control.state_page(observation(generation=2))

    def test_valid_snapshot_may_have_no_legal_actions(self):
        page = self.control.legal_actions_page(observation(valid_rows(actions=False)))
        self.assertEqual(page["page"], {
            "section": "legal_actions",
            "items": [],
            "total_items": 0,
            "next_cursor": None,
            "cursor_expires_at": None,
        })


class V2PaginationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.control = V2SeatControl("game_test", "agent_test", 1)

    def _synthetic_chain(self, total, limit):
        snapshot = self.control._snapshot(observation())
        values = tuple({"ordinal": index} for index in range(total))
        return self.control._start_page_chain(
            snapshot,
            "state",
            "known_tiles",
            limit,
            values,
            {
                "endpoint": "state",
                "query": {"section": "known_tiles", "limit": limit},
            },
        )

    def _drain_chain(self, first):
        count = len(first["page"]["items"])
        cursor = first["page"]["next_cursor"]
        while cursor is not None:
            page = self.control.continue_page(cursor, endpoint="state")
            count += len(page["page"]["items"])
            cursor = page["page"]["next_cursor"]
        return count

    def test_admitted_low_limit_chains_reach_terminal(self):
        self.assertEqual(self._drain_chain(self._synthetic_chain(303, 1)), 303)
        other = V2SeatControl("game_test", "agent_other", 1)
        self.control = other
        self.assertEqual(
            self._drain_chain(self._synthetic_chain(8192, 16)),
            8192,
        )

    def test_chain_admission_is_atomic_under_capacity_pressure(self):
        with mock.patch.object(v2_control, "MAX_ACTIVE_CURSOR_CHAINS", 1):
            accepted = self._synthetic_chain(4, 1)
            before = dict(self.control._page_chains)
            with self.assertRaisesRegex(V2ControlError, "rate_limited"):
                self._synthetic_chain(4, 1)
            self.assertEqual(self.control._page_chains, before)
            self.assertEqual(self._drain_chain(accepted), 4)

    def test_abandoned_chain_ttl_releases_reserved_capacity(self):
        with mock.patch.object(v2_control, "MAX_ACTIVE_CURSOR_CHAINS", 1):
            with mock.patch.object(v2_control.time, "monotonic", return_value=10.0):
                abandoned = self._synthetic_chain(4, 1)
            cursor = abandoned["page"]["next_cursor"]
            with mock.patch.object(v2_control.time, "monotonic", return_value=311.0):
                replacement = self._synthetic_chain(4, 1)
                self.assertIsNotNone(replacement["page"]["next_cursor"])
                with self.assertRaisesRegex(V2ControlError, "cursor_expired"):
                    self.control.continue_page(cursor, endpoint="state")

    def test_intrinsically_oversized_chain_returns_no_prefix_or_record(self):
        with mock.patch.object(v2_control, "MAX_CURSOR_CHAIN_SLOTS", 1):
            with self.assertRaisesRegex(V2ControlError, "scope_too_large"):
                self._synthetic_chain(4, 1)
        self.assertEqual(self.control._page_chains, {})
        self.assertEqual(self.control._retired_page_chains, {})

    def test_state_and_legal_cursors_are_endpoint_bound_and_idempotent(self):
        state = self.control.state_page(observation(), "known_tiles", limit=1)
        cursor = state["page"]["next_cursor"]
        self.assertIsNotNone(cursor)
        with self.assertRaisesRegex(V2ControlError, "invalid_request"):
            self.control.continue_page(cursor, endpoint="legal_actions")
        continued = self.control.continue_page(cursor, endpoint="state")
        self.assertEqual(continued["page"]["section"], "known_tiles")
        self.assertEqual(len(continued["page"]["items"]), 1)
        replayed = self.control.continue_page(cursor, endpoint="state")
        self.assertEqual(replayed, continued)

        legal = self.control.legal_actions_page(observation(), limit=1)
        legal_cursor = legal["page"]["next_cursor"]
        self.assertFalse(self.control.is_actor_scope_cursor(
            legal_cursor, endpoint="legal_actions",
        ))
        with self.assertRaisesRegex(V2ControlError, "invalid_request"):
            self.control.continue_page(legal_cursor, endpoint="state")
        self.assertEqual(
            self.control.continue_page(
                legal_cursor, endpoint="legal_actions",
            )["page"]["section"],
            "legal_actions",
        )

    def test_cursor_is_instance_bound_and_expires_at_five_minutes(self):
        with mock.patch.object(v2_control.time, "monotonic", return_value=10.0):
            page = self.control.state_page(observation(), "known_tiles", limit=1)
        cursor = page["page"]["next_cursor"]
        other = V2SeatControl("game_test", "agent_test", 1)
        with self.assertRaisesRegex(V2ControlError, "invalid_request"):
            other.continue_page(cursor, endpoint="state")
        with mock.patch.object(v2_control.time, "monotonic", return_value=309.0):
            self.assertEqual(
                self.control.continue_page(cursor, endpoint="state")["page"][
                    "section"
                ],
                "known_tiles",
            )
        fresh = self.control.state_page(observation(), "known_tiles", limit=1)
        with mock.patch.object(
            v2_control.time, "monotonic", return_value=time.monotonic() + 301,
        ):
            with self.assertRaisesRegex(V2ControlError, "cursor_expired"):
                self.control.continue_page(
                    fresh["page"]["next_cursor"], endpoint="state",
                )

    def test_cursor_chain_survives_two_revision_snapshot_eviction(self):
        old = self.control.state_page(observation(revision=10), "known_tiles", limit=1)
        cursor = old["page"]["next_cursor"]
        self.control.state_page(observation(revision=11))
        self.control.state_page(observation(revision=12))
        self.assertEqual(tuple(self.control._snapshots), (11, 12))
        self.assertEqual(
            self.control.continue_page(cursor, endpoint="state")["page"][
                "section"
            ],
            "known_tiles",
        )

    def test_cursor_capacity_preserves_live_and_retired_authenticity(self):
        with mock.patch.object(v2_control.time, "monotonic", return_value=10.0):
            cursors = [
                self.control.state_page(
                    observation(), "known_tiles", limit=2,
                )["page"]["next_cursor"]
                for _ in range(v2_control.MAX_ACTIVE_CURSOR_CHAINS)
            ]
            with self.assertRaisesRegex(V2ControlError, "rate_limited"):
                self.control.state_page(
                    observation(), "known_tiles", limit=2,
                )
        self.assertEqual(
            len(self.control._page_chains),
            v2_control.MAX_ACTIVE_CURSOR_CHAINS,
        )
        with mock.patch.object(v2_control.time, "monotonic", return_value=11.0):
            self.assertEqual(
                len(self.control.continue_page(
                    cursors[0], endpoint="state",
                )["page"]["items"]),
                1,
            )
        with mock.patch.object(v2_control.time, "monotonic", return_value=311.0):
            with self.assertRaisesRegex(V2ControlError, "cursor_expired"):
                self.control.continue_page(cursors[-1], endpoint="state")
            replacement = self.control.state_page(
                observation(), "known_tiles", limit=1,
            )["page"]["next_cursor"]
            self.assertIsNotNone(replacement)
            with self.assertRaisesRegex(V2ControlError, "cursor_expired"):
                self.control.continue_page(cursors[-1], endpoint="state")

    def test_successful_continuation_replays_identically_and_refreshes_ttl(self):
        with mock.patch.object(
            v2_control.time, "monotonic", return_value=10.0,
        ), mock.patch.object(v2_control.time, "time", return_value=1000.0):
            first = self.control.state_page(
                observation(), "known_tiles", limit=1,
            )
        cursor = first["page"]["next_cursor"]
        first_expiry = first["page"]["cursor_expires_at"]
        with mock.patch.object(
            v2_control.time, "monotonic", return_value=20.0,
        ), mock.patch.object(v2_control.time, "time", return_value=1010.0):
            continued = self.control.continue_page(cursor, endpoint="state")
        self.assertNotEqual(
            continued["page"]["cursor_expires_at"], first_expiry,
        )
        with mock.patch.object(
            v2_control.time, "monotonic", return_value=21.0,
        ):
            replay = self.control.continue_page(cursor, endpoint="state")
        self.assertEqual(
            json.dumps(replay, sort_keys=True, separators=(",", ":")),
            json.dumps(continued, sort_keys=True, separators=(",", ":")),
        )
        with mock.patch.object(v2_control.time, "monotonic", return_value=321.0):
            with self.assertRaisesRegex(
                V2ControlError, "cursor_expired",
            ) as expired:
                self.control.continue_page(cursor, endpoint="state")
        self.assertEqual(
            expired.exception.details["restart"],
            {
                "endpoint": "state",
                "query": {"section": "known_tiles", "limit": 1},
            },
        )
        with self.assertRaisesRegex(V2ControlError, "invalid_request"):
            self.control.continue_page(
                "cursor_" + "x" * 32, endpoint="state",
            )

    def test_sections_and_page_limits_are_strict(self):
        for limit in (0, 17, True, "2"):
            with self.subTest(limit=limit), self.assertRaisesRegex(
                V2ControlError, "invalid_request",
            ):
                self.control.state_page(observation(), limit=limit)
        with self.assertRaisesRegex(V2ControlError, "invalid_request"):
            self.control.state_page(observation(), "native_rows")
        with self.assertRaisesRegex(V2ControlError, "invalid_request"):
            self.control.continue_page("cursor_x", endpoint="unknown")

    def test_public_page_byte_ceiling_pages_or_rejects_one_oversized_item(self):
        baseline = V2SeatControl("game_test", "byte_probe", 1).state_page(
            observation(), "known_tiles", limit=1,
        )
        one_item_bytes = v2_control.V2SeatControl._canonical_public_bytes(
            baseline,
        )
        bounded = V2SeatControl("game_test", "byte_agent", 1)
        bounded_probe = bounded.state_page(
            observation(), "known_tiles", limit=1,
        )
        bounded_size = bounded._canonical_public_bytes(bounded_probe)
        with mock.patch.object(
            v2_control, "MAX_PUBLIC_PAGE_BYTES", bounded_size,
        ):
            page = V2SeatControl(
                "game_test", "byte_agent", 1,
            ).state_page(observation(), "known_tiles", limit=3)
        self.assertEqual(len(page["page"]["items"]), 1)
        self.assertIsNotNone(page["page"]["next_cursor"])
        self.assertLessEqual(
            bounded._canonical_public_bytes(page), bounded_size,
        )
        with mock.patch.object(
            v2_control, "MAX_PUBLIC_PAGE_BYTES", one_item_bytes - 1,
        ):
            with self.assertRaisesRegex(V2ControlError, "scope_too_large"):
                V2SeatControl(
                    "game_test", "byte_probe", 1,
                ).state_page(observation(), "known_tiles", limit=1)

    def test_projection_cache_enforces_two_revisions_and_aggregate_byte_limit(self):
        for revision in (10, 11, 12):
            self.control.state_page(observation(revision=revision))
        self.assertEqual(tuple(self.control._snapshots), (11, 12))
        self.assertLessEqual(
            self.control._projected_bytes, v2_control.MAX_PROJECTED_BYTES,
        )

        probe = V2SeatControl("game_test", "agent_test", 1)
        probe.state_page(observation())
        one_size = probe._snapshots[11].canonical_bytes
        bounded = V2SeatControl("game_test", "agent_test", 1)
        with mock.patch.object(v2_control, "MAX_PROJECTED_BYTES", one_size + 1):
            bounded.state_page(observation(revision=11))
            bounded.state_page(observation(revision=12))
            self.assertEqual(tuple(bounded._snapshots), (12,))
            self.assertLessEqual(bounded._projected_bytes, one_size + 1)

        too_small = V2SeatControl("game_test", "agent_test", 1)
        with mock.patch.object(v2_control, "MAX_PROJECTED_BYTES", 1):
            with self.assertRaisesRegex(V2ControlError, "internal_error"):
                too_small.state_page(observation())


class V2ActionResolutionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.control = V2SeatControl("game_test", "agent_test", 1)

    def _actions(
        self,
        *,
        control: V2SeatControl | None = None,
        generation: int = 1,
        revision: int = 11,
    ) -> tuple[dict[str, object], list[dict[str, object]]]:
        target = control or self.control
        payload = target.legal_actions_page(observation(
            generation=generation, revision=revision,
        ))
        return payload["state_revision"], payload["page"]["items"]

    @staticmethod
    def _find(
        actions: list[dict[str, object]], operation: str,
    ) -> dict[str, object]:
        return next(
            action for action in actions
            if action["subject"]["operation"] == operation
        )

    def test_vote_state_and_casting_are_opaque_revision_bound_and_exact(self):
        rows = vote_rows(phase_actions=False)
        observed = observation(rows)
        vote = self.control.state_page(observed, "votes")["page"]["items"][0]
        self.assertEqual(set(vote), {
            "vote_id", "description", "yes", "no", "abstain",
            "num_voters", "percent_required", "team_only",
            "current_vote", "can_vote",
        })
        self.assertNotEqual(vote["vote_id"], "42")
        self.assertRegex(vote["vote_id"], r"^vote_[0-9a-f]{32}$")
        actions_page = self.control.legal_actions_page(observed)
        action = self._find(actions_page["page"]["items"], "cast_vote")
        self.assertEqual(action["subject"]["target"], {
            "type": "vote", "vote_id": vote["vote_id"],
        })
        resolved = self.control.resolve_action(
            observed, actions_page["state_revision"], action["action_id"],
            {"vote_id": vote["vote_id"], "vote": "yes"},
        )
        self.assertEqual(resolved.native_arguments, "vote=yes")
        for arguments in (
            {}, {"vote_id": vote["vote_id"]},
            {"vote_id": vote["vote_id"], "vote": "maybe"},
            {"vote_id": "vote_forged", "vote": "no"},
            {"vote_id": vote["vote_id"], "vote": "abstain", "extra": 1},
        ):
            with self.subTest(arguments=arguments), self.assertRaisesRegex(
                V2ControlError, "invalid_request",
            ):
                self.control.resolve_action(
                    observed, actions_page["state_revision"],
                    action["action_id"], arguments,
                )
        changed = observation(
            vote_rows(phase_actions=False, current_vote="yes", yes=3),
            revision=12,
        )
        changed_vote = self.control.state_page(changed, "votes")["page"][
            "items"
        ][0]
        self.assertNotEqual(vote["vote_id"], changed_vote["vote_id"])

    def test_vote_visibility_without_permission_has_no_cast_action(self):
        observed = observation(
            vote_rows(phase_actions=False, can_vote=False),
        )
        vote = self.control.state_page(observed, "votes")["page"]["items"][0]
        self.assertIs(vote["can_vote"], False)
        self.assertFalse(any(
            item["subject"]["operation"] == "cast_vote"
            for item in self.control.legal_actions_page(observed)["page"]["items"]
        ))

    def test_pregame_vote_uses_the_same_bounded_public_contract(self):
        observed = observation(pregame_vote_rows(), revision=12)
        vote = self.control.state_page(observed, "votes")["page"]["items"][0]
        payload = self.control.legal_actions_page(observed)
        action = self._find(payload["page"]["items"], "cast_vote")
        resolved = self.control.resolve_action(
            observed, payload["state_revision"], action["action_id"],
            {"vote_id": vote["vote_id"], "vote": "abstain"},
        )
        self.assertEqual(resolved.native_arguments, "vote=abstain")

    def test_current_no_argument_action_resolves_to_immutable_private_inputs(self):
        state_revision, actions = self._actions()
        action = self._find(actions, "end")
        resolved = self.control.resolve_action(
            observation(), state_revision, action["action_id"], {},
        )
        self.assertIsInstance(resolved, V2ActionResolution)
        self.assertEqual(resolved.native_slot, "a0000000000000001")
        self.assertEqual(resolved.native_revision, 11)
        self.assertEqual(resolved.native_arguments, "-")
        self.assertEqual(resolved.public_kind, "phase.end")
        self.assertEqual(resolved.operation, "end")
        self.assertEqual(resolved.turn, 7)
        self.assertEqual(resolved.phase, 1)
        with self.assertRaises(FrozenInstanceError):
            resolved.native_slot = "a0000000000000002"

        public = json.dumps([
            self.control.state_page(observation()),
            self.control.legal_actions_page(observation()),
        ], sort_keys=True)
        for private in (
            "a0000000000000001", "native_slot", "native_revision",
            "native_arguments", "argument_contract",
        ):
            self.assertNotIn(private, public)

    def test_research_choices_and_rates_resolve_to_strict_native_inputs(self):
        state_revision, actions = self._actions()
        target = self._find(actions, "set_target")
        resolved_target = self.control.resolve_action(
            observation(), state_revision, target["action_id"], {},
        )
        self.assertEqual(resolved_target.public_kind, "research.set_target")
        self.assertEqual(resolved_target.native_arguments, "-")

        rates = self._find(actions, "set_rates")
        resolved_rates = self.control.resolve_action(
            observation(), state_revision, rates["action_id"],
            {"tax": 30, "luxury": 10, "science": 60},
        )
        self.assertEqual(resolved_rates.public_kind, "economy.set_rates")
        self.assertEqual(
            resolved_rates.native_arguments,
            "tax=30,luxury=10,science=60",
        )
        invalid = (
            {},
            {"tax": 30, "luxury": 10, "science": 60, "other": 0},
            {"tax": True, "luxury": 10, "science": 89},
            {"tax": 30, "luxury": 10, "science": 50},
            {"tax": 20, "luxury": 0, "science": 80},
        )
        for arguments in invalid:
            with self.subTest(arguments=arguments), self.assertRaisesRegex(
                V2ControlError, "invalid_request",
            ):
                self.control.resolve_action(
                    observation(), state_revision, rates["action_id"],
                    arguments,
                )

        max_34_rows = replace_row(
            replace_row(
                valid_rows(),
                "tax=30 science=60 luxury=10",
                "tax=34 science=33 luxury=33",
            ),
            "target_tech=-1 vote_no=-1 target_government=-1 max_rate=70 ",
            "target_tech=-1 vote_no=-1 target_government=-1 max_rate=34 ",
        )
        max_34_rows = replace_row(
            max_34_rows, "changeable_tax=1 max_rate=70",
            "changeable_tax=1 max_rate=34",
        )
        max_34_observation = observation(max_34_rows, revision=12)
        max_34_payload = self.control.legal_actions_page(max_34_observation)
        max_34_action = self._find(
            max_34_payload["page"]["items"], "set_rates",
        )
        self.assertEqual(
            max_34_action["arguments_schema"]["properties"]["tax"]["maximum"],
            34,
        )
        resolved_34 = self.control.resolve_action(
            max_34_observation, max_34_payload["state_revision"],
            max_34_action["action_id"],
            {"tax": 34, "luxury": 33, "science": 33},
        )
        self.assertEqual(
            resolved_34.native_arguments,
            "tax=34,luxury=33,science=33",
        )

    def test_city_name_is_validated_in_utf8_bytes_and_encoded_exactly_once(self):
        state_revision, actions = self._actions()
        action = self._find(actions, "found_city")

        for name in (
            "Ada %= Prime",
            "e\u0301",
            "\u00e9" * 59 + "x",  # 119 UTF-8 bytes.
        ):
            with self.subTest(name=name):
                resolved = self.control.resolve_action(
                    observation(), state_revision, action["action_id"],
                    {"city_name": name},
                )
                # This is the pre-transport native payload.  The sidecar will
                # percent-encode the complete value exactly once.
                self.assertEqual(resolved.native_arguments, f"city_name={name}")
                self.assertEqual(resolved.public_kind, "unit.perform_action")
                self.assertEqual(resolved.operation, "found_city")

        invalid_arguments = (
            None,
            {},
            {"city_name": "", "extra": 1},
            {"city_name": ""},
            {"city_name": 7},
            {"city_name": "x" * 120},
            {"city_name": "\u00e9" * 60},
            {"city_name": "bad\ud800"},
            {"city_name": "bad\0name"},
            {"city_name": "bad\tname"},
            {"city_name": "bad\nname"},
            {"city_name": "bad\rname"},
            {"city_name": "bad\x1fname"},
            {"city_name": "bad\x7fname"},
            {"city_name": "bad\u0085name"},
            {"city_name": "bad\u200ename"},
            {"city_name": "bad\ue000name"},
        )
        for invalid in invalid_arguments:
            with self.subTest(invalid=repr(invalid)), self.assertRaisesRegex(
                V2ControlError, "invalid_request",
            ):
                self.control.resolve_action(
                    observation(), state_revision, action["action_id"], invalid,
                )

    def test_no_argument_actions_require_an_exact_empty_object(self):
        state_revision, actions = self._actions()
        action = self._find(actions, "move")
        self.assertEqual(
            self.control.resolve_action(
                observation(), state_revision, action["action_id"], {},
            ).native_arguments,
            "-",
        )
        for invalid in (None, [], {"extra": 1}, {"city_name": "Ada"}):
            with self.subTest(invalid=invalid), self.assertRaisesRegex(
                V2ControlError, "invalid_request",
            ):
                self.control.resolve_action(
                    observation(), state_revision, action["action_id"], invalid,
                )

    def test_only_newest_exact_state_revision_is_executable(self):
        old_revision, old_actions = self._actions(revision=11)
        old_action = self._find(old_actions, "move")
        new_revision, new_actions = self._actions(revision=12)
        new_action = self._find(new_actions, "move")
        self.assertEqual(tuple(self.control._snapshots), (11, 12))

        stale_cases = (
            (observation(revision=11), old_revision, old_action["action_id"]),
            (observation(revision=12), old_revision, old_action["action_id"]),
            (
                observation(revision=12),
                {**new_revision, "turn": new_revision["turn"] + 1},
                new_action["action_id"],
            ),
            (
                observation(revision=12),
                {**new_revision, "revision": new_revision["revision"] - 1},
                new_action["action_id"],
            ),
            (
                observation(revision=12),
                {**new_revision, "state_token": "state_wrong-token"},
                new_action["action_id"],
            ),
            (
                observation(revision=12),
                {key: value for key, value in new_revision.items()
                 if key != "state_token"},
                new_action["action_id"],
            ),
            (
                observation(revision=12),
                {**new_revision, "extra": 1},
                new_action["action_id"],
            ),
        )
        for fresh, requested, action_id in stale_cases:
            with self.subTest(requested=requested), self.assertRaisesRegex(
                V2ControlError, "stale_revision",
            ):
                self.control.resolve_action(fresh, requested, action_id, {})

        resolved = self.control.resolve_action(
            observation(revision=12), new_revision, new_action["action_id"], {},
        )
        self.assertEqual(resolved.native_revision, 12)

        # Once an older capability is evicted it remains stale, not an
        # implementation failure, at the action boundary.
        self._actions(revision=13)
        self.assertEqual(tuple(self.control._snapshots), (12, 13))
        with self.assertRaisesRegex(V2ControlError, "stale_revision"):
            self.control.resolve_action(
                observation(revision=11), old_revision,
                old_action["action_id"], {},
            )

    def test_action_capabilities_are_seat_generation_and_lifetime_scoped(self):
        state_revision, actions = self._actions()
        current_action = self._find(actions, "move")
        other_seat = V2SeatControl("game_test", "agent_other", 1)
        _, other_actions = self._actions(control=other_seat)
        other_generation = V2SeatControl("game_test", "agent_test", 2)
        _, generation_actions = self._actions(
            control=other_generation, generation=2,
        )
        expired = (
            None,
            "",
            "action_unknown",
            self._find(other_actions, "move")["action_id"],
            self._find(generation_actions, "move")["action_id"],
        )
        for action_id in expired:
            with self.subTest(action_id=action_id), self.assertRaisesRegex(
                V2ControlError, "action_expired",
            ):
                self.control.resolve_action(
                    observation(), state_revision, action_id, {},
                )

        self.control.close()
        with self.assertRaisesRegex(V2ControlError, "sidecar_unavailable"):
            self.control.resolve_action(
                observation(), state_revision, current_action["action_id"], {},
            )

    def test_wrong_observation_generation_fails_closed(self):
        state_revision, actions = self._actions()
        action = self._find(actions, "move")
        with self.assertRaisesRegex(V2ControlError, "internal_error"):
            self.control.resolve_action(
                observation(generation=2), state_revision,
                action["action_id"], {},
            )


if __name__ == "__main__":
    unittest.main()
