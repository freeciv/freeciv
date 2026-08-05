import http.client
import hashlib
import io
import gzip
import json
import os
import re
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
from contextlib import redirect_stderr, redirect_stdout
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urljoin
from unittest.mock import patch

from agent_eval import v2_control
from agent_eval.__main__ import _parser, main, run_native_viewer_client
from agent_eval.bridge_status import validate_bridge_journal
from agent_eval.client import (
    ClientError,
    NativeViewerCompatibilityError,
    controller_session_key,
    create_game,
    join_capabilities,
    join_game,
    load_private_json,
    native_viewer_status,
    next_turn,
    require_native_viewer_protocol,
    release_native_viewer,
    request_json,
    request_native_viewer,
    submit_action,
    write_private_json,
)
from agent_eval.watch_room import (
    locate_game_run,
    run_snapshot_watch_room,
    select_stable_snapshot,
)
from agent_eval.replay_gateway import gateway_config
from agent_eval.scoring import aggregate_leaderboard
from agent_eval.headless_sidecar import (
    SidecarActionAmbiguous,
    SidecarActionNotAccepted,
    SidecarError,
)
from agent_eval.supervisor import (
    APIProblem,
    Game,
    NATIVE_VIEWER_SIGNAL_GUARD_S,
    Supervisor,
    SupervisorError,
    SupervisorHTTPServer,
    VIEWER_DIST_ROOT,
    _classic_tech_requirements,
    _classic_technology_catalog,
    make_supervisor_server,
)
from agent_eval.v2_control import (
    V2ActionResolution,
    V2ControlError,
    V2SeatControl,
)
from agent_eval.v2_ambiguity_trace import TRACE_DIRECTORY, TRACE_FILENAME
from agent_eval.v2_receipts import (
    ReceiptReservation,
    V2ReceiptConflict,
    V2ReceiptStoreError,
)
from agent_eval.v2_phase_events import (
    PHASE_EVENT_FILENAME,
    V2PhaseEventJournalError,
)


ACTION = {
    "type": "set_traits",
    "traits": {
        "aggressive": 1,
        "builder": 2,
        "expansionist": 3,
        "trader": 4,
    },
}


def observation(seat_id, turn=1, year=-4000):
    return {
        "seat_id": seat_id,
        "player_id": int(seat_id.split("-")[-1]) - 1,
        "player_name": seat_id,
        "turn": turn,
        "year": year,
    }


def _complete_v2_action_row(row):
    if row.startswith("player ") and " infrastructure_enabled=" not in row:
        return row + " infrastructure_enabled=0 infrastructure_points=0"
    if row.startswith("tile ") and " placing_extra=" not in row:
        return row + (
            " placing_extra=-1 placing_extra_name=none placing_turns=0 "
            f"placing_time={'-1' if ' known=0 ' in row else '1'}"
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
    if row.startswith("unit ") and " scope=own " in row \
            and " orders_repeat=" not in row:
        row += (
            " orders_repeat=0 orders_vigilant=0 order_count=0 "
            "orders_digest=fnv1a64-0000000000000000 "
            "orders_destination=-1"
        )
    if row.startswith("unit ") and " scope=own " in row \
            and " action_decision_want=" not in row:
        row += " action_decision_want=nothing action_decision_tile=-1"
    if row.startswith("diplomacy ") and " intel_level=" not in row:
        row = row.replace(
            " has_embassy=",
            " intel_level=contact team=2 team_name=Team%202 same_team=0 "
            "controller=human connected=1 score=17 gold=23 "
            "government=Despotism has_embassy=",
            1,
        )
    if not row.startswith("action "):
        return row
    if " vote_no=" not in row:
        row = row.replace(
            " target_government=", " vote_no=-1 target_government=", 1,
        )
    if " server_setting_id=" not in row:
        row = row.replace(
            " target_government=",
            " server_setting_id=-1 server_setting_type=none "
            "server_setting_min=0 server_setting_max=0 "
            "server_setting_current=-1 server_setting_value=-1 "
            "target_government=",
            1,
        )
    if " route_waypoint_limit=" not in row:
        row = row.replace(
            " target_build_kind=",
            " route_waypoint_limit=0 infrastructure_cost=0 "
            "infrastructure_turns=0 infrastructure_choice_count=0 "
            "infrastructure_choices=- target_build_kind=",
            1,
        )
    if " spaceship_part=" not in row:
        row = row.replace(
            " source_specialist=",
            " spaceship_part=none spaceship_value=-1 "
            "target_multiplier=-1 multiplier_value=-1 source_specialist=",
            1,
        )
    if " subtarget_kind=" not in row:
        row = row.replace(
            " activity=",
            " subtarget_kind=none subresults=none activity=",
            1,
        )
    if " counterpart=" in row:
        return (
            row if " gold_cost=" in row
            else row.replace(" args=", " gold_cost=-1 args=")
        )
    actor_end = row.index(" ", row.index(" actor=") + 1)
    completed = (
        row[:actor_end]
        + " counterpart=none meeting_generation=0 "
        "clauses_digest=fnv1a64-0000000000000000 self_accepted=0 "
        "other_accepted=0 relation_state=none outgoing_vision=0 "
        "outgoing_shared_tiles=0 clause_giver=none clause_type=none "
        "clause_value=-1 clause_name=none desired_acceptance=-1"
        + row[actor_end:]
    )
    if " gold_cost=" not in completed:
        completed = completed.replace(" args=", " gold_cost=-1 args=")
    return completed


def native_v2_rows(*, tile_count=1, action_count=6, malformed=False):
    if action_count not in {0} and action_count < 6:
        raise ValueError("action observations require six complete capabilities")
    if action_count > 6 and tile_count < 1:
        raise ValueError("unit actions require one visible target tile")
    rows = [
        (
            f"meta state=running turn=7 phase={1 if action_count else 0} "
            "cache=human-client "
            "phase_mode=players_alternate phase_count=2 "
            f"active_phase={1 if action_count else 0} "
            f"phase_ready={1 if action_count else 0} "
            "map_width=16 map_height=16 topology=square wrap_x=1 wrap_y=0 "
            f"known_tile_count={tile_count}"
        ),
        (
            "player ref=p:1:10 name=Codex nation=Roman government=Despotism "
            "gold=40 tax=30 science=60 luxury=10 alive=1 phase_done=0 "
            "changeable_tax=1 max_rate=70"
        ),
        (
            "governance current_id=1 target_id=-1 during_id=0 status=stable "
            "finish_turn=-1 turns_remaining=0 method=random max_turns=5 "
            "untargeted_allowed=1 no_anarchy=0 can_revolution=1 "
            "choices_count=4"
        ),
        "government id=0 name=Anarchy current=0 target=0 during=1 can_change=0",
        (
            "government id=1 name=Despotism current=1 target=0 during=0 "
            "can_change=0"
        ),
        "government id=2 name=Monarchy current=0 target=0 during=0 can_change=1",
        "government id=3 name=Republic current=0 target=0 during=0 can_change=1",
        (
            "multiplier id=0 name=Policy value=50 target=50 start=0 "
            "stop=100 step=10 minimum_turns=2 changed_turn=0 "
            "can_change=0 choice_count=11"
        ),
        (
            "spaceship state=none structurals=0 structurals_placed=0 "
            "components=0 fuel=0 propulsion=0 modules=0 habitation=0 "
            "life_support=0 solar_panels=0 launch_year=9999 population=0 "
            "mass=0 support_permille=0 energy_permille=0 "
            "success_permille=0 travel_time_millis=0 has_capital=1 "
            "can_launch=0"
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
    ]
    if action_count:
        rows.extend((
            "action slot=a0000000000000001 kind=phase.end actor=none "
            "target_tile=-1 source_city=none destination_city=none target_unit=none transport_context=none "
            "target_tech=-1 vote_no=-1 server_setting_id=-1 "
            "server_setting_type=none server_setting_min=0 "
            "server_setting_max=0 server_setting_current=-1 "
            "server_setting_value=-1 target_government=-1 max_rate=0 "
            "target_build_kind=none target_build=-1 source_specialist=-1 target_specialist=-1 target_extra=-1 "
            "activity=none target_name=none "
            "native_rule=phase.end target_kind=player result=phase_end "
            "actor_consuming_always=0 legality=legal probability_kind=exact "
            "probability_min=200 probability_max=200 args=none",
            "action slot=a0000000000000002 kind=research.set_target actor=none "
            "target_tile=-1 source_city=none destination_city=none target_unit=none transport_context=none "
            "target_tech=6 vote_no=-1 server_setting_id=-1 "
            "server_setting_type=none server_setting_min=0 "
            "server_setting_max=0 server_setting_current=-1 "
            "server_setting_value=-1 target_government=-1 max_rate=0 "
            "target_build_kind=none target_build=-1 source_specialist=-1 target_specialist=-1 target_extra=-1 "
            "activity=none target_name=none "
            "native_rule=research.set_target target_kind=Technology "
            "result=Research%20Target actor_consuming_always=0 legality=legal "
            "probability_kind=exact probability_min=200 probability_max=200 "
            "args=none",
            "action slot=a0000000000000003 kind=research.set_goal actor=none "
            "target_tile=-1 source_city=none destination_city=none target_unit=none transport_context=none "
            "target_tech=4 vote_no=-1 server_setting_id=-1 "
            "server_setting_type=none server_setting_min=0 "
            "server_setting_max=0 server_setting_current=-1 "
            "server_setting_value=-1 target_government=-1 max_rate=0 "
            "target_build_kind=none target_build=-1 source_specialist=-1 target_specialist=-1 target_extra=-1 "
            "activity=none target_name=none "
            "native_rule=research.set_goal target_kind=Technology "
            "result=Research%20Goal actor_consuming_always=0 legality=legal "
            "probability_kind=exact probability_min=200 probability_max=200 "
            "args=none",
            "action slot=a0000000000000004 kind=research.set_goal actor=none "
            "target_tile=-1 source_city=none destination_city=none target_unit=none transport_context=none "
            "target_tech=6 vote_no=-1 server_setting_id=-1 "
            "server_setting_type=none server_setting_min=0 "
            "server_setting_max=0 server_setting_current=-1 "
            "server_setting_value=-1 target_government=-1 max_rate=0 "
            "target_build_kind=none target_build=-1 source_specialist=-1 target_specialist=-1 target_extra=-1 "
            "activity=none target_name=none "
            "native_rule=research.set_goal target_kind=Technology "
            "result=Research%20Goal actor_consuming_always=0 legality=legal "
            "probability_kind=exact probability_min=200 probability_max=200 "
            "args=none",
            "action slot=a0000000000000005 kind=research.set_goal actor=none "
            "target_tile=-1 source_city=none destination_city=none target_unit=none transport_context=none "
            "target_tech=1000 vote_no=-1 server_setting_id=-1 "
            "server_setting_type=none server_setting_min=0 "
            "server_setting_max=0 server_setting_current=-1 "
            "server_setting_value=-1 target_government=-1 max_rate=0 "
            "target_build_kind=none target_build=-1 source_specialist=-1 target_specialist=-1 target_extra=-1 "
            "activity=none target_name=none "
            "native_rule=research.set_goal target_kind=Technology "
            "result=Research%20Goal actor_consuming_always=0 legality=legal "
            "probability_kind=exact probability_min=200 probability_max=200 "
            "args=none",
            "action slot=a0000000000000006 kind=economy.set_rates actor=none "
            "target_tile=-1 source_city=none destination_city=none target_unit=none transport_context=none "
            "target_tech=-1 vote_no=-1 server_setting_id=-1 "
            "server_setting_type=none server_setting_min=0 "
            "server_setting_max=0 server_setting_current=-1 "
            "server_setting_value=-1 target_government=-1 max_rate=70 "
            "target_build_kind=none target_build=-1 source_specialist=-1 target_specialist=-1 target_extra=-1 "
            "activity=none target_name=none "
            "native_rule=economy.set_rates target_kind=Player "
            "result=Economic%20Rates actor_consuming_always=0 legality=legal "
            "probability_kind=exact probability_min=200 probability_max=200 "
            "args=rates-required",
        ))
    if action_count > 6:
        rows.append(
            "unit ref=u:10:100 scope=own owner=p:1:10 type_id=13 "
            "type=Warriors home_city=none converts_to_id=-1 "
            "converts_to=none tile=0 "
            "x=0 y=0 hp=10 veteran=0 veteran_name=Green veteran_levels=2 "
            "veteran_power=100 veteran_move_bonus=0 fuel=0 max_hp=10 "
            "max_fuel=0 move_rate=3 attack=1 defense=1 firepower=1 "
            "base_upkeep_food=0 base_upkeep_shield=1 base_upkeep_trade=0 "
            "base_upkeep_gold=0 base_upkeep_luxury=0 "
            "base_upkeep_science=0 upkeep_food=0 upkeep_shield=1 "
            "upkeep_trade=0 upkeep_gold=0 upkeep_luxury=0 upkeep_science=0 "
            "moves=3 activity=idle activity_target=-1 "
            "activity_target_name=none activity_progress=0 "
            "transport_state=untransported transporter=none "
            "transport_capacity=0 occupied=0 paradropped=0 paradrop_range=0 "
            "controller=none has_orders=0 orders_repeat=0 orders_vigilant=0 "
            "order_count=0 orders_digest=fnv1a64-0000000000000000 "
            "orders_destination=-1 action_decision_want=nothing "
            "action_decision_tile=-1"
        )
        rows.extend(
            (
                f"action slot=a{index:016X} kind=unit.move actor=u:10:100 "
                "target_tile=0 source_city=none destination_city=none target_unit=none transport_context=none "
                "target_tech=-1 vote_no=-1 server_setting_id=-1 "
                "server_setting_type=none server_setting_min=0 "
                "server_setting_max=0 server_setting_current=-1 "
                "server_setting_value=-1 target_government=-1 max_rate=0 "
                "target_build_kind=none target_build=-1 source_specialist=-1 target_specialist=-1 target_extra=-1 "
                "activity=none target_name=none "
                "native_rule=Unit%20Move target_kind=Tile "
                "result=Unit%20Move actor_consuming_always=0 legality=legal "
                "probability_kind=exact probability_min=200 "
                "probability_max=200 args=none"
            )
            for index in range(7, action_count + 1)
        )
    rows.extend(
        f"tile index={index} x={index % 16} y={index // 16} "
        "known=2 terrain=Grassland owner=none"
        for index in range(tile_count)
    )
    rows.extend(
        "spaceship_structural "
        f"slot={slot} x={slot} y=0 required_slot={-1 if slot == 0 else 0} "
        f"placed=0 required_connected={1 if slot == 0 else 0} can_place=0"
        for slot in range(32)
    )
    if malformed:
        rows.append("native secret=must-not-escape")
    return tuple(sorted(_complete_v2_action_row(row) for row in rows))


def native_v2_city_build_choice_row(*, production_kind, production_id, name):
    if production_kind == "unit":
        telemetry = (
            "shield_cost=10 shield_stock_after_change=4 turns=3 "
            "turns_with_stock=3 upkeep_food=0 upkeep_shield=1 "
            "upkeep_trade=0 upkeep_gold=0 upkeep_luxury=0 "
            "upkeep_science=0 happy_cost=0 unit_attack=1 unit_defense=1 "
            "unit_move_rate=3 unit_hp=10 unit_firepower=1 "
            "unit_vision_radius_sq=5 unit_transport_capacity=0 unit_fuel=0 "
            "unit_pop_cost=0 unit_bombard_rate=0 unit_city_size=0 "
            "unit_paradrop_range=0 building_genus=none "
            "building_obsolete=-1 building_redundant=-1 "
            "building_convert=-1 building_allows_units=-1 "
            "building_allows_extras=-1 building_prevents_disaster=-1 "
            "building_protects_vs_actions=-1 building_allows_actions=-1"
        )
    elif production_kind == "improvement":
        telemetry = (
            "shield_cost=60 shield_stock_after_change=4 turns=28 "
            "turns_with_stock=28 upkeep_food=0 upkeep_shield=0 "
            "upkeep_trade=0 upkeep_gold=1 upkeep_luxury=0 "
            "upkeep_science=0 happy_cost=-1 unit_attack=-1 unit_defense=-1 "
            "unit_move_rate=-1 unit_hp=-1 unit_firepower=-1 "
            "unit_vision_radius_sq=-1 unit_transport_capacity=-1 "
            "unit_fuel=-1 unit_pop_cost=-1 unit_bombard_rate=-1 "
            "unit_city_size=-1 unit_paradrop_range=-1 "
            "building_genus=improvement building_obsolete=0 "
            "building_redundant=0 building_convert=0 "
            "building_allows_units=0 building_allows_extras=0 "
            "building_prevents_disaster=0 building_protects_vs_actions=0 "
            "building_allows_actions=0"
        )
    else:
        raise ValueError("unsupported fixture production kind")
    return (
        f"city_build_choice city=c:20:200 production_kind={production_kind} "
        f"production_id={production_id} production_name={name} can_queue=1 "
        f"can_build_now=1 {telemetry}"
    )


def native_v2_pregame_rows(*, ready=False):
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
        _complete_v2_action_row(
            "action slot=a0000000000000501 kind=pregame.set_ready "
            "actor=p:1:10 target_tile=-1 source_city=none "
            "destination_city=none target_unit=none transport_context=none "
            "target_tech=-1 vote_no=-1 server_setting_id=-1 "
            "server_setting_type=none server_setting_min=0 "
            "server_setting_max=0 server_setting_current=-1 "
            "server_setting_value=-1 target_government=-1 max_rate=0 "
            "target_build_kind=none target_build=-1 source_specialist=-1 "
            "target_specialist=-1 target_extra=-1 activity=none "
            "target_name=readiness native_rule=pregame.set_ready "
            "target_kind=Pregame%20Readiness result=Readiness%20Changed "
            "actor_consuming_always=0 legality=legal probability_kind=exact "
            "probability_min=200 probability_max=200 "
            "args=pregame-ready-required"
        ),
        _complete_v2_action_row(
            "action slot=a0000000000000504 kind=player.send_chat "
            "actor=p:1:10 target_tile=-1 source_city=none "
            "destination_city=none target_unit=none transport_context=none "
            "target_tech=-1 vote_no=-1 server_setting_id=-1 "
            "server_setting_type=none server_setting_min=0 "
            "server_setting_max=0 server_setting_current=-1 "
            "server_setting_value=-1 target_government=-1 max_rate=0 "
            "target_build_kind=none target_build=-1 source_specialist=-1 "
            "target_specialist=-1 target_extra=-1 activity=none "
            "target_name=none native_rule=player.send_chat "
            "target_kind=Chat%20Channel result=Chat%20Echo%20Received "
            "actor_consuming_always=0 legality=legal probability_kind=exact "
            "probability_min=200 probability_max=200 args=chat-required"
        ),
    ]
    rows[2] = rows[2].replace(
        "desired_acceptance=-1", f"desired_acceptance={0 if ready else 1}",
    )
    if not ready:
        rows.append(_complete_v2_action_row(
            "action slot=a0000000000000500 kind=pregame.configure "
            "actor=p:1:10 target_tile=-1 source_city=none "
            "destination_city=none target_unit=none transport_context=none "
            "target_tech=-1 vote_no=-1 server_setting_id=-1 "
            "server_setting_type=none server_setting_min=0 "
            "server_setting_max=0 server_setting_current=-1 "
            "server_setting_value=-1 target_government=-1 max_rate=0 "
            "target_build_kind=none target_build=-1 source_specialist=-1 "
            "target_specialist=-1 target_extra=-1 activity=none "
            "target_name=configuration native_rule=pregame.configure "
            "target_kind=Pregame%20Configuration "
            "result=Configuration%20Changed actor_consuming_always=0 "
            "legality=legal probability_kind=exact probability_min=200 "
            "probability_max=200 args=pregame-config-required"
        ))
        rows.append(_complete_v2_action_row(
            "action slot=a0000000000000502 kind=pregame.set_team "
            "actor=p:1:10 target_tile=-1 source_city=none "
            "destination_city=none target_unit=none transport_context=none "
            "target_tech=-1 vote_no=-1 server_setting_id=-1 "
            "server_setting_type=none server_setting_min=0 "
            "server_setting_max=0 server_setting_current=-1 "
            "server_setting_value=-1 target_government=-1 max_rate=0 "
            "target_build_kind=none target_build=-1 source_specialist=-1 "
            "target_specialist=-1 target_extra=-1 activity=none "
            "target_name=team native_rule=pregame.set_team "
            "target_kind=Pregame%20Team result=Team%20Changed "
            "actor_consuming_always=0 legality=legal probability_kind=exact "
            "probability_min=200 probability_max=200 "
            "args=pregame-team-required"
        ))
    return tuple(sorted(rows))


def _native_v2_scoped_rows(actor_ref):
    if actor_ref == "p:1:10":
        return (
            "action slot=a0000000000000069 kind=government.revolution "
            "actor=p:1:10 target_tile=-1 source_city=none destination_city=none target_unit=none "
            "transport_context=none target_tech=-1 "
            "target_government=0 max_rate=0 target_build_kind=none "
            "target_build=-1 source_specialist=-1 target_specialist=-1 target_extra=-1 activity=none "
            "target_name=Anarchy native_rule=government.revolution "
            "target_kind=Government result=Revolution%20Started "
            "actor_consuming_always=0 legality=legal probability_kind=exact "
            "probability_min=200 probability_max=200 args=none",
            "action slot=a000000000000006A kind=government.change "
            "actor=p:1:10 target_tile=-1 source_city=none destination_city=none target_unit=none "
            "transport_context=none target_tech=-1 "
            "target_government=2 max_rate=0 target_build_kind=none "
            "target_build=-1 source_specialist=-1 target_specialist=-1 target_extra=-1 activity=none "
            "target_name=Monarchy native_rule=government.change "
            "target_kind=Government result=Government%20Choice%20Recorded "
            "actor_consuming_always=0 legality=legal probability_kind=exact "
            "probability_min=200 probability_max=200 args=none",
            "action slot=a000000000000006B kind=government.change "
            "actor=p:1:10 target_tile=-1 source_city=none destination_city=none target_unit=none "
            "transport_context=none target_tech=-1 "
            "target_government=3 max_rate=0 target_build_kind=none "
            "target_build=-1 source_specialist=-1 target_specialist=-1 target_extra=-1 activity=none "
            "target_name=Republic native_rule=government.change "
            "target_kind=Government result=Government%20Choice%20Recorded "
            "actor_consuming_always=0 legality=legal probability_kind=exact "
            "probability_min=200 probability_max=200 args=none",
        )
    if actor_ref == "c:20:200":
        return (
            "action slot=a0000000000000065 kind=city.set_production "
            "actor=c:20:200 target_tile=-1 source_city=none destination_city=none target_unit=none "
            "transport_context=none target_tech=-1 "
            "target_government=-1 max_rate=0 "
            "target_build_kind=improvement target_build=5 source_specialist=-1 target_specialist=-1 target_extra=-1 "
            "activity=none target_name=Granary native_rule=city.set_production "
            "target_kind=Production result=Production%20Changed "
            "actor_consuming_always=0 legality=legal probability_kind=exact "
            "probability_min=200 probability_max=200 args=none",
            "action slot=a0000000000000066 kind=city.buy_production "
            "actor=c:20:200 target_tile=-1 source_city=none destination_city=none target_unit=none "
            "transport_context=none target_tech=-1 "
            "target_government=-1 max_rate=0 "
            "target_build_kind=unit target_build=2 source_specialist=-1 target_specialist=-1 target_extra=-1 "
            "activity=none target_name=Warriors native_rule=city.buy_production "
            "target_kind=Production result=Production%20Bought "
            "actor_consuming_always=0 legality=legal probability_kind=exact "
            "probability_min=200 probability_max=200 args=none",
            "action slot=a000000000000006C kind=city.set_worklist "
            "actor=c:20:200 target_tile=-1 source_city=none destination_city=none target_unit=none "
            "transport_context=none target_tech=-1 vote_no=-1 "
            "server_setting_id=-1 server_setting_type=none "
            "server_setting_min=0 server_setting_max=0 "
            "server_setting_current=-1 server_setting_value=-1 "
            "target_government=-1 "
            "max_rate=0 target_build_kind=none target_build=-1 "
            "source_specialist=-1 target_specialist=-1 target_extra=-1 "
            "activity=none target_name=worklist "
            "native_rule=city.set_worklist target_kind=City "
            "result=Worklist%20Changed actor_consuming_always=0 "
            "legality=legal probability_kind=exact probability_min=200 "
            "probability_max=200 args=worklist-required",
            "action slot=a000000000000006D kind=city.set_options "
            "actor=c:20:200 target_tile=-1 source_city=none destination_city=none target_unit=none "
            "transport_context=none target_tech=-1 vote_no=-1 "
            "server_setting_id=-1 server_setting_type=none "
            "server_setting_min=0 server_setting_max=0 "
            "server_setting_current=-1 server_setting_value=-1 "
            "target_government=-1 "
            "max_rate=0 target_build_kind=none target_build=-1 "
            "source_specialist=-1 target_specialist=-1 target_extra=-1 "
            "activity=none target_name=options "
            "native_rule=city.set_options target_kind=City "
            "result=City%20Options%20Changed actor_consuming_always=0 "
            "legality=legal probability_kind=exact probability_min=200 "
            "probability_max=200 args=city-options-required",
            "action slot=a000000000000006E kind=city.rename "
            "actor=c:20:200 target_tile=-1 source_city=none destination_city=none target_unit=none "
            "transport_context=none target_tech=-1 vote_no=-1 "
            "server_setting_id=-1 server_setting_type=none "
            "server_setting_min=0 server_setting_max=0 "
            "server_setting_current=-1 server_setting_value=-1 "
            "target_government=-1 "
            "max_rate=0 target_build_kind=none target_build=-1 "
            "source_specialist=-1 target_specialist=-1 target_extra=-1 "
            "activity=none target_name=name native_rule=city.rename "
            "target_kind=City result=City%20Renamed "
            "actor_consuming_always=0 legality=legal "
            "probability_kind=exact probability_min=200 "
            "probability_max=200 args=city_name-required",
            "action slot=a000000000000006F kind=city.set_governor "
            "actor=c:20:200 target_tile=-1 source_city=none "
            "destination_city=none target_unit=none transport_context=none "
            "target_tech=-1 vote_no=-1 server_setting_id=-1 "
            "server_setting_type=none server_setting_min=0 "
            "server_setting_max=0 server_setting_current=-1 "
            "server_setting_value=-1 target_government=-1 max_rate=0 "
            "target_build_kind=none target_build=-1 source_specialist=-1 "
            "target_specialist=-1 target_extra=-1 activity=none "
            "target_name=governor native_rule=city.set_governor "
            "target_kind=City result=Governor%20Goal%20Set "
            "actor_consuming_always=0 legality=legal probability_kind=exact "
            "probability_min=200 probability_max=200 "
            "args=governor-goal-required",
        )
    if actor_ref == "u:10:100":
        return (
            "action slot=a0000000000000067 kind=unit.start_activity "
            "actor=u:10:100 target_tile=-1 source_city=none destination_city=none target_unit=none "
            "transport_context=none target_tech=-1 "
            "target_government=-1 max_rate=0 "
            "target_build_kind=none target_build=-1 source_specialist=-1 target_specialist=-1 target_extra=-1 "
            "activity=cultivate target_name=cultivate "
            "native_rule=unit.start_activity target_kind=Worker%20Activity "
            "result=Activity%20Installed actor_consuming_always=0 "
            "legality=legal probability_kind=exact probability_min=200 "
            "probability_max=200 args=none",
            "action slot=a0000000000000068 kind=unit.start_activity "
            "actor=u:10:100 target_tile=-1 source_city=none destination_city=none target_unit=none "
            "transport_context=none target_tech=-1 "
            "target_government=-1 max_rate=0 "
            "target_build_kind=none target_build=-1 source_specialist=-1 target_specialist=-1 target_extra=7 "
            "subtarget_kind=extra subresults=none activity=pillage "
            "target_name=Irrigation "
            "native_rule=unit.start_activity target_kind=Worker%20Activity "
            "result=Activity%20Installed actor_consuming_always=0 "
            "legality=legal probability_kind=exact probability_min=200 "
            "probability_max=200 args=none",
        )
    return ()


def native_v2_scoped_rows(actor_ref):
    return tuple(
        _complete_v2_action_row(row)
        for row in _native_v2_scoped_rows(actor_ref)
    )


def native_v2_relation_action(
    slot, rule, result, target_name, *, clause_value=-1, clause_name="none",
):
    native_kind = (
        "diplomacy.propose_clause"
        if rule == "diplomacy.propose_clause" else rule
    )
    row = _complete_v2_action_row(
        f"action slot=a{slot:016X} kind={native_kind} actor=p:1:10 "
        "target_tile=-1 source_city=none destination_city=none "
        "target_unit=none transport_context=none target_tech=-1 "
        "target_government=-1 max_rate=0 target_build_kind=none "
        "target_build=-1 source_specialist=-1 target_specialist=-1 "
        "target_extra=-1 activity=none "
        f"target_name={target_name} native_rule={rule} "
        f"target_kind=Diplomatic%20Relation result={result} "
        "actor_consuming_always=0 legality=legal probability_kind=exact "
        "probability_min=200 probability_max=200 args=none"
    )
    replacements = {
        "counterpart=none": "counterpart=p:2:20",
        "meeting_generation=0": "meeting_generation=3",
        "clauses_digest=fnv1a64-0000000000000000": (
            "clauses_digest=fnv1a64-cbf29ce484222325"
        ),
        "relation_state=none": "relation_state=Peace",
    }
    if rule == "diplomacy.accept":
        replacements["desired_acceptance=-1"] = "desired_acceptance=1"
    if rule == "diplomacy.propose_clause":
        replacements.update({
            "clause_giver=none": "clause_giver=p:2:20",
            "clause_type=none": "clause_type=Advance",
            "clause_value=-1": f"clause_value={clause_value}",
            "clause_name=none": f"clause_name={clause_name}",
        })
    for old, new in replacements.items():
        row = row.replace(old, new)
    return row


def raw_json_request(url, token=None):
    headers = {"Accept": "application/json"}
    if token is not None:
        headers["Authorization"] = f"Bearer {token}"
    request = urllib.request.Request(url, headers=headers, method="GET")
    try:
        with urllib.request.urlopen(request, timeout=2) as response:
            return response.status, json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        with exc:
            return exc.code, json.loads(exc.read().decode("utf-8"))


def write_scorelog(path, *turns):
    path.write_text(
        "#FREECIV SCORELOG2 test\n"
        + "".join(f"turn {turn} 0 year\n" for turn in turns),
        encoding="utf-8",
    )


def write_player_scores(path, turn, *rows):
    text = ["#FREECIV SCORELOG2 test", "tag 0 score"]
    for player_id, (name, _score) in enumerate(rows):
        text.append(f"addplayer 0 {player_id} {name}")
    text.append(f"turn {turn} 0 year")
    for player_id, (_name, score) in enumerate(rows):
        text.append(f"data {turn} 0 {player_id} {score}")
    path.write_text("\n".join(text) + "\n", encoding="utf-8")


def write_complete_scorelog(path, turns, *rows):
    turns = tuple(turns)
    text = ["#FREECIV SCORELOG2 test", "tag 0 score"]
    for index, turn in enumerate(turns):
        text.append(f"turn {turn} 0 year")
        if index == 0:
            for player_id, (name, _score) in enumerate(rows):
                text.append(f"addplayer {turn} {player_id} {name}")
        if index == len(turns) - 1:
            for player_id, (_name, score) in enumerate(rows):
                text.append(f"data {turn} 0 {player_id} {score}")
    path.write_text("\n".join(text) + "\n", encoding="utf-8")


def classic_raw_catalog():
    technologies = []
    for tech_id, rule_name in enumerate(_classic_tech_requirements(), 1):
        sidecar_name = "?tech:Railroad" if rule_name == "Railroad" else rule_name
        technologies.append({
            "id": tech_id,
            "rule_name": sidecar_name,
            "name": sidecar_name,
            "cost_base": tech_id * 10,
        })
    return {"schema_version": 1, "technologies": technologies}


def replay_player(player_id, player_name, known=(), **overrides):
    value = {
        "seat_id": f"raw-{player_id}",
        "player_id": player_id,
        "player_name": player_name,
        "nation": "Romans",
        "government": "Despotism",
        "alive": True,
        "score": 12,
        "cities": 1,
        "citizens": 2,
        "units": 3,
        "gold": 40,
        "culture": 5,
        "known_tech_ids": list(known),
        "research": {"tech_id": None, "name": "", "bulbs": 0, "cost": 0},
        "future_techs": 0,
    }
    value.update(overrides)
    return value


class FakeSidecar:
    def __init__(self, factory, **kwargs):
        self.factory = factory
        self.kwargs = kwargs
        self.generation = kwargs["generation"]
        self.player_name = kwargs["player_name"]
        self.callback = kwargs["on_exit"]
        self.state = "new"
        self.stop_count = 0
        self.start_count = 0
        self.error_code = None
        self.read_count = 0
        self.client_state = "running"

    def public_health(self):
        return {
            "state": self.state,
            "generation": self.generation,
            "player_name": self.player_name,
            "client_state": self.client_state if self.state == "ready" else None,
            "server_connected": self.state == "ready",
            "seat_state": "ready" if self.state == "ready" else "idle",
            "error_code": self.error_code,
            # Supervisor sanitization must discard every field below.
            "argv": ["--secret", "do-not-expose"],
            "environment": {"TOKEN": "do-not-expose"},
            "log_path": "/private/do-not-expose",
        }

    def start_and_take(self):
        self.start_count += 1
        if self.factory.start_gate is not None:
            self.factory.start_gate.wait(2)
        if self.factory.fail_next:
            self.factory.fail_next = False
            self.state = "failed"
            self.error_code = "take_failed"
            self.callback(self.generation, self.public_health())
            raise RuntimeError("fake take failure")
        self.state = "ready"
        if self.factory.die_after_ready:
            self.factory.die_after_ready = False
            self.state = "failed"
            self.error_code = "process_exited"
            self.callback(self.generation, self.public_health())
        return self.public_health()

    def status(self, timeout_s=1):
        if self.factory.status_error is not None:
            raise self.factory.status_error
        if self.state != "ready":
            raise RuntimeError("fake sidecar not ready")
        response = self.factory.status_response
        match_state = re.search(r"(?:^|\t)state=([^\t]+)", response)
        if match_state is not None:
            self.client_state = match_state.group(1)
        if "\tplayer=" not in response:
            match = re.fullmatch(r"AgentPlace([0-9]+)", self.player_name)
            owns_seat = "\tseat=ready" in response
            player = int(match.group(1)) - 1 if match and owns_seat else -1
            lifecycle = self.generation if owns_seat else 0
            response += f"\tplayer={player}\tlifecycle={lifecycle}"
        return response

    def phase_evidence(self):
        if self.factory.phase_evidence_hook is not None:
            return self.factory.phase_evidence_hook(self)
        value = self.factory.phase_evidence_by_player.get(self.player_name)
        return None if value is None else dict(value)

    def read_observation(
        self, request_id, timeout_s=5, *, on_terminal_error=None,
    ):
        self.read_count += 1
        try:
            if self.factory.read_hook is not None:
                return self._compact_observation(
                    self.factory.read_hook(self, request_id, timeout_s)
                )
            if self.factory.observation_error is not None:
                raise self.factory.observation_error
        except SidecarError as exc:
            if on_terminal_error is not None:
                on_terminal_error(exc)
            raise
        rows = self.factory.observation_rows_by_player.get(
            self.player_name, self.factory.observation_rows,
        )
        match = re.fullmatch(r"AgentPlace([0-9]+)", self.player_name)
        if self.player_name not in self.factory.observation_rows_by_player and match:
            player_number = int(match.group(1))
            if player_number != 1:
                player_ref = f"p:{player_number}:{9 + player_number}"
                rows = tuple(
                    row.replace("p:1:10", player_ref)
                    for row in rows
                    if not row.startswith("action ")
                )
                rows = tuple(
                    row.replace("active_phase=1", "active_phase=0").replace(
                        "phase_ready=1", "phase_ready=0",
                    )
                    for row in rows
                )
        return self._compact_observation({
            "generation": self.generation,
            "native_revision": self.factory.native_revision,
            "rows": rows,
        })

    @staticmethod
    def _compact_observation(observation):
        """Mirror the v2 native OBS contract used by the real sidecar."""
        entity_prefixes = (
            "city_site ", "city ", "rally ", "unit ",
            "diplomacy_clause ", "tombstone ",
            "city_tile ", "city_specialist ", "city_worklist ",
            "city_build_choice ", "city_improvement ", "city_rally ",
        )
        return {
            **observation,
            "rows": tuple(
                row for row in observation["rows"]
                if not row.startswith(entity_prefixes)
            ),
        }

    def _scope_rows(self, actor_ref):
        rows = self.factory.observation_rows_by_player.get(
            self.player_name, self.factory.observation_rows,
        )
        if actor_ref.startswith("p:"):
            global_rows = tuple(
                row for row in rows
                if row.startswith("action ") and " actor=none " in row
            )
            return global_rows + native_v2_scoped_rows(actor_ref)
        if actor_ref.startswith("u:"):
            global_rows = tuple(
                row for row in rows
                if row.startswith("action ")
                and f" actor={actor_ref} " in row
            )
            return global_rows + native_v2_scoped_rows(actor_ref)
        if actor_ref.startswith("c:"):
            return native_v2_scoped_rows(actor_ref)
        return ()

    def read_actor_scope(
        self, request_id, expected_revision, actor_ref, limit=16, timeout_s=5,
    ):
        self.factory.scope_count += 1
        rows = self._scope_rows(actor_ref)
        return {
            "generation": self.generation,
            "native_revision": expected_revision,
            "actor_ref": actor_ref,
            "view_id": f"v{expected_revision}-1",
            "offset": 0,
            "count": min(limit, len(rows)),
            "total_count": len(rows),
            "next_offset": min(limit, len(rows)),
            "complete": True,
            "overflow": False,
            "rows": rows[:limit],
        }

    def read_actor_scope_page(
        self, request_id, view_id, revision, actor_ref, total_count, offset,
        limit, timeout_s=5,
    ):
        self.factory.scope_page_count += 1
        rows = self._scope_rows(actor_ref)
        page = rows[offset:offset + limit]
        return {
            "generation": self.generation,
            "native_revision": revision,
            "actor_ref": actor_ref,
            "view_id": view_id,
            "offset": offset,
            "count": len(page),
            "total_count": total_count,
            "next_offset": offset + len(page),
            "complete": True,
            "overflow": False,
            "rows": page,
        }

    def read_actor_scope_catalog(
        self, request_id, expected_revision, actor_ref, timeout_s=30,
    ):
        self.factory.scope_count += 1
        rows = self._scope_rows(actor_ref)
        return {
            "generation": self.generation,
            "native_revision": expected_revision,
            "actor_ref": actor_ref,
            "view_id": f"v{expected_revision}-1",
            "offset": 0,
            "count": len(rows),
            "total_count": len(rows),
            "next_offset": len(rows),
            "complete": True,
            "overflow": False,
            "rows": rows,
        }

    @staticmethod
    def _tile_local_row(row):
        if row.startswith("tile "):
            row = "tile_local " + row[len("tile "):]
        if " resource_extra=" not in row:
            row += (
                " resource_extra=-1 resource_name=none has_label=0 "
                "label=none food=2 shields=1 trade=0"
            )
        return row

    def _state_scope_rows(self, section, selector):
        if section == "investigation":
            return self.factory.investigation_rows
        if self.factory.state_scope_rows is not None \
                and section == "tile_window":
            return tuple(
                self._tile_local_row(row)
                for row in self.factory.state_scope_rows
            )
        rows = self.factory.observation_rows_by_player.get(
            self.player_name, self.factory.observation_rows,
        )
        match = re.fullmatch(r"AgentPlace([0-9]+)", self.player_name)
        if self.player_name not in self.factory.observation_rows_by_player and match:
            player_number = int(match.group(1))
            if player_number != 1:
                player_ref = f"p:{player_number}:{9 + player_number}"
                rows = tuple(row.replace("p:1:10", player_ref) for row in rows)
        if section == "known_tiles":
            return tuple(
                row for row in rows
                if row.startswith("tile ") and " known=0 " not in f" {row} "
            )
        if section == "tile_window":
            match = re.fullmatch(r"t([0-9]+)-r([0-8])", selector)
            assert match is not None
            center = int(match.group(1))
            radius = int(match.group(2))
            selected = []
            for row in rows:
                if not row.startswith("tile "):
                    continue
                tile = int(re.search(r"\bindex=([0-9]+)", row).group(1))
                dx = abs(tile - center)
                dx = min(dx, 16 - dx) if dx <= 16 else dx
                if dx <= radius:
                    selected.append(self._tile_local_row(row))
            return tuple(selected)
        if section in {"cities", "units", "city_sites"}:
            prefixes = {
                "cities": ("city ", "city_rally "),
                "units": ("unit ",),
                "city_sites": ("city_site ",),
            }[section]
            return tuple(row for row in rows if row.startswith(prefixes))
        if section == "diplomacy_clauses":
            selected = tuple(
                row for row in rows
                if row.startswith("diplomacy_clause ")
                and f" other={selector} " in f" {row} "
            )
            return tuple(sorted(
                selected,
                key=lambda row: int(re.search(
                    r"\bposition=([0-9]+)", row,
                ).group(1)),
            ))
        if section == "target_tiles":
            targets = {
                int(re.search(r"\btarget_tile=(-?[0-9]+)", row).group(1))
                for row in self._scope_rows(selector)
                if " target_tile=" in f" {row} "
                and int(re.search(
                    r"\btarget_tile=(-?[0-9]+)", row,
                ).group(1)) >= 0
            }
            return tuple(
                self._tile_local_row(row) for row in rows
                if row.startswith("tile ")
                and int(re.search(r"\bindex=([0-9]+)", row).group(1))
                    in targets
            )
        prefix = {
            "city_citizens": ("city_tile ", "city_specialist "),
            "city_build_choices": ("city_build_choice ",),
            "city_worklist": ("city_worklist ",),
            "city_improvements": ("city_improvement ",),
            "city_trade_routes": ("city_trade_route ",),
            "city_governor": ("city_governor ",),
        }[section]
        return tuple(
            row for row in rows
            if row.startswith(prefix) and f" city={selector} " in f" {row} "
        )

    def read_state_scope_catalog(
        self, request_id, expected_revision, section, selector, timeout_s=30,
    ):
        self.factory.state_scope_sections.append(section)
        self.factory.target_pipeline.append(
            ("support", expected_revision, section, selector),
        )
        rows = self._state_scope_rows(section, selector)
        return {
            "generation": self.generation,
            "native_revision": expected_revision,
            "section": section,
            "selector": selector,
            "view_id": f"q{expected_revision}-1",
            "offset": 0,
            "count": len(rows),
            "total_count": len(rows),
            "next_offset": len(rows),
            "complete": True,
            "overflow": False,
            "rows": rows,
        }

    def read_relation_scope(
        self, request_id, expected_revision, actor_ref, counterpart_ref,
        limit=16, timeout_s=5,
    ):
        self.factory.relation_scope_count += 1
        self.factory.last_relation_actor = actor_ref
        self.factory.last_relation_counterpart = counterpart_ref
        rows = self.factory.relation_rows
        return {
            "generation": self.generation,
            "native_revision": expected_revision,
            "actor_ref": actor_ref,
            "counterpart_ref": counterpart_ref,
            "view_id": f"r{expected_revision}-1",
            "offset": 0,
            "count": min(limit, len(rows)),
            "total_count": len(rows),
            "next_offset": min(limit, len(rows)),
            "complete": True,
            "overflow": False,
            "rows": rows[:limit],
        }

    def read_relation_scope_page(
        self, request_id, view_id, revision, actor_ref, counterpart_ref,
        total_count, offset, limit, timeout_s=5,
    ):
        self.factory.relation_scope_page_count += 1
        rows = self.factory.relation_rows[offset:offset + limit]
        return {
            "generation": self.generation,
            "native_revision": revision,
            "actor_ref": actor_ref,
            "counterpart_ref": counterpart_ref,
            "view_id": view_id,
            "offset": offset,
            "count": len(rows),
            "total_count": total_count,
            "next_offset": offset + len(rows),
            "complete": True,
            "overflow": False,
            "rows": rows,
        }

    def read_relation_scope_catalog(
        self, request_id, expected_revision, actor_ref, counterpart_ref,
        timeout_s=30,
    ):
        self.factory.relation_scope_count += 1
        self.factory.last_relation_actor = actor_ref
        self.factory.last_relation_counterpart = counterpart_ref
        rows = self.factory.relation_rows
        return {
            "generation": self.generation,
            "native_revision": expected_revision,
            "actor_ref": actor_ref,
            "counterpart_ref": counterpart_ref,
            "view_id": f"r{expected_revision}-1",
            "offset": 0,
            "count": len(rows),
            "total_count": len(rows),
            "next_offset": len(rows),
            "complete": True,
            "overflow": False,
            "rows": rows,
        }

    def read_target_action(
        self, request_id, expected_revision, actor_ref, native_tile,
        timeout_s=5,
    ):
        self.factory.target_count += 1
        self.factory.last_target_actor = actor_ref
        self.factory.last_target_tile = native_tile
        self.factory.target_pipeline.append(
            ("target", expected_revision, actor_ref, native_tile),
        )
        if self.factory.target_error is not None:
            raise self.factory.target_error
        if self.factory.target_empty:
            rows = ()
        else:
            is_city = actor_ref.startswith("c:")
            is_player = actor_ref.startswith("p:")
            rows = (_complete_v2_action_row(
                f"action slot=t{native_tile:08X}0123456789ABCDEF "
                f"kind={'player.place_infrastructure' if is_player else 'city.set_rally' if is_city else 'unit.goto'} "
                f"actor={actor_ref} target_tile={native_tile} "
                "source_city=none destination_city=none target_unit=none "
                "transport_context=none target_tech=-1 "
                "target_government=-1 max_rate=0 "
                "route_waypoint_limit=0 infrastructure_cost=0 "
                "infrastructure_turns=0 "
                f"infrastructure_choice_count={1 if is_player else 0} "
                f"infrastructure_choices={'0' if is_player else '-'} "
                "target_build_kind=none target_build=-1 "
                "source_specialist=-1 target_specialist=-1 target_extra=-1 "
                f"activity=none target_name={'infrastructure' if is_player else 'destination'} "
                f"native_rule={'player.place_infrastructure' if is_player else 'city.set_rally' if is_city else 'unit.goto'} "
                "target_kind=Tile "
                f"result={'Infrastructure%20Placement%20Started' if is_player else 'Rally%20Point%20Set' if is_city else 'Orders%20Queued'} "
                "actor_consuming_always=0 "
                "legality=legal probability_kind=exact "
                "probability_min=200 probability_max=200 "
                f"args={'infrastructure-extra-required' if is_player else 'persistent-required' if is_city else 'none'}"
            ),)
        return {
            "generation": self.generation,
            "native_revision": expected_revision,
            "actor_ref": actor_ref,
            "native_tile": native_tile,
            "count": len(rows),
            "rows": rows,
        }

    def execute_action(
        self, request_id, action_slot, arguments="-", timeout_s=20,
        *, expected_revision=None, on_accepted=None, on_ambiguous=None,
    ):
        self.factory.action_count += 1
        if self.factory.action_hook is not None:
            try:
                return self.factory.action_hook(
                    self, request_id, action_slot, arguments, timeout_s,
                    expected_revision, on_accepted,
                )
            except SidecarActionAmbiguous as exc:
                if on_ambiguous is not None:
                    on_ambiguous(exc)
                raise
        if self.factory.action_error is not None:
            error = self.factory.action_error
            if isinstance(error, SidecarActionAmbiguous):
                if on_ambiguous is not None:
                    on_ambiguous(error)
            raise error
        if on_accepted is not None:
            on_accepted({
                "request_id": request_id,
                "accepted": True,
                "accepted_revision": expected_revision,
            })
        result_revision = max(self.factory.native_revision, expected_revision or 1) + 1
        self.factory.native_revision = result_revision
        return {
            "request_id": request_id,
            "accepted": True,
            "applied": True,
            "status": "applied",
            "reason": "POSTCONDITION_VERIFIED",
            "accepted_revision": expected_revision,
            "result_revision": result_revision,
            "observation_selector": None,
        }

    def execute_scoped_action(
        self, request_id, expected_revision, actor_ref, action_slot,
        arguments="-", timeout_s=20, *, on_accepted=None, on_ambiguous=None,
    ):
        self.factory.scoped_action_count += 1
        self.factory.last_scoped_actor = actor_ref
        return self.execute_action(
            request_id, action_slot, arguments, timeout_s,
            expected_revision=expected_revision, on_accepted=on_accepted,
            on_ambiguous=on_ambiguous,
        )

    def execute_relation_scoped_action(
        self, request_id, expected_revision, actor_ref, counterpart_ref,
        action_slot, arguments="-", timeout_s=20, *, on_accepted=None,
        on_ambiguous=None,
    ):
        self.factory.relation_action_count += 1
        self.factory.last_relation_actor = actor_ref
        self.factory.last_relation_counterpart = counterpart_ref
        return self.execute_action(
            request_id, action_slot, arguments, timeout_s,
            expected_revision=expected_revision, on_accepted=on_accepted,
            on_ambiguous=on_ambiguous,
        )

    def stop(self):
        self.stop_count += 1
        if self.state not in {"stopped", "failed"}:
            self.state = "stopped"
            self.callback(self.generation, self.public_health())
        return self.public_health()

    def die(self):
        self.state = "failed"
        self.error_code = "process_exited"
        self.callback(self.generation, self.public_health())


class FakeSidecarFactory:
    def __init__(self):
        self.created = []
        self.fail_next = False
        self.die_after_ready = False
        self.start_gate = None
        self.status_response = "STATUS\tstate=running\tserver=1\tseat=ready"
        self.status_error = None
        self.observation_rows = native_v2_rows()
        self.state_scope_rows = None
        self.investigation_rows = ()
        self.relation_rows = ()
        self.observation_rows_by_player = {}
        self.native_revision = 11
        self.target_pipeline = []
        self.relation_scope_count = 0
        self.relation_scope_page_count = 0
        self.relation_action_count = 0
        self.last_relation_actor = None
        self.last_relation_counterpart = None
        self.observation_error = None
        self.read_hook = None
        self.phase_evidence_by_player = {}
        self.phase_evidence_hook = None
        self.action_count = 0
        self.scope_count = 0
        self.scope_page_count = 0
        self.state_scope_sections = []
        self.target_count = 0
        self.last_target_actor = None
        self.last_target_tile = None
        self.target_empty = False
        self.target_error = None
        self.scoped_action_count = 0
        self.last_scoped_actor = None
        self.action_error = None
        self.action_hook = None

    def __call__(self, **kwargs):
        sidecar = FakeSidecar(self, **kwargs)
        self.created.append(sidecar)
        return sidecar


class SupervisorTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.launch = patch.object(Game, "_launch", autospec=True)
        self.send = patch.object(Game, "_send_commands", autospec=True)
        self.launch_mock = self.launch.start()
        self.send_mock = self.send.start()
        self.sidecar_factory = FakeSidecarFactory()
        self.supervisor = Supervisor(
            self.directory.name, "admin-secret",
            binary="/unused/freeciv-server", process_factory=lambda *a, **k: None,
            sidecar_factory=self.sidecar_factory,
        )
        self.supervisor.service_url = "http://127.0.0.1:9876"

    def tearDown(self):
        self.supervisor.close()
        self.send.stop()
        self.launch.stop()
        self.directory.cleanup()

    def create(self, **overrides):
        config = {
            "mode": "single",
            "places": 2,
            "turns": 2,
            "seed": 7,
            "objective": "win cleanly",
            "action_timeout_s": 1,
            "lobby_timeout_s": 0,
            "frame_interval": 1,
            "frame_zoom": 1,
            **overrides,
        }
        return self.supervisor.create_game(config)

    @staticmethod
    def _mark_v2_running(game):
        """Make fake-sidecar route tests independent of the status thread."""
        with game.condition:
            game.state = "running"
            game.condition.notify_all()

    def _seed_v2_phase(self, game, *, place=1, turn=7, phase=1):
        generation = game.sidecar_generations[place]
        player_name = game.places[place - 1].player_name
        self.sidecar_factory.phase_evidence_by_player[player_name] = {
            "generation": generation,
            "revision": self.sidecar_factory.native_revision,
            "turn": turn,
            "phase": phase,
            "mode": "players_alternate",
            "phase_count": 2,
            "active": True,
            "ready": True,
            "alive": True,
            "done": False,
        }
        game._update_v2_phase_ledger(
            self.phase_evidence(game, turn=turn, phase=phase,
                                active_place=place),
            time.monotonic(),
        )

    def test_v2_runtime_gate_requires_fresh_connection_and_seat_health(self):
        _created, game, _joined, _action = self.ready_v2_action()
        sidecar = game.sidecars[1]
        generation = game.sidecar_generations[1]
        with game.condition:
            self.assertTrue(game._v2_seat_runtime_active_locked(
                1, generation, sidecar,
            ))
        healthy = sidecar.public_health()
        for field, value in (
            ("client_state", "stopped"),
            ("server_connected", False),
            ("seat_state", "idle"),
        ):
            current = dict(healthy)
            current[field] = value
            with self.subTest(field=field), patch.object(
                sidecar, "public_health", return_value=current,
            ), game.condition:
                self.assertFalse(game._v2_seat_runtime_active_locked(
                    1, generation, sidecar,
                ))

    def test_v2_all_native_clients_over_hands_off_to_server_monitor(self):
        _created, game, _joined = self.ready_v2_phase_game(
            multiplayer=True, places=2,
        )
        self.sidecar_factory.status_response = (
            "STATUS\tstate=over\tserver=1\tseat=ready"
        )
        with game.condition:
            game.start_sent = True
            game.v2_phase_ledger["end"] = {
                "claim_id": "final-phase-claim",
                "key": (2, 1),
                "place": 2,
                "source": "agent",
                "receipt_state": "applied",
                "deadline_started_at": time.time(),
                "deadline_started_monotonic": time.monotonic(),
            }
            game.v2_phase_ledger["key"] = (2, 1)

        self.assertFalse(game._poll_v2_sidecars_once())

        self.assertTrue(game.sidecars_stopping)
        self.assertEqual(game.state, "running")
        self.assertIsNone(game.error)
        self.assertNotIn(
            "v2_phase_reconciliation_stalled", game.invalid_reasons,
        )
        self.assertEqual(game.v2_phase_ledger["state"], "terminalizing")
        self.assertEqual(
            game.v2_phase_ledger["end"]["claim_id"], "final-phase-claim",
        )
        self.assertTrue(all(
            sidecar.stop_count == 1 for sidecar in game.sidecars.values()
        ))

    def ready_v2_action(self):
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-batch-model",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        self._seed_v2_phase(game)
        legal = game.v2_get_page(joined["agent_id"], "legal_actions", "")
        action = legal["page"]["items"][0]
        return created, game, joined, action

    def ready_v2_non_phase_action(self):
        created, game, joined, action = self.ready_v2_action()
        if action["kind"] == "phase.end":
            legal = game.v2_get_page(
                joined["agent_id"], "legal_actions", "limit=16",
            )
            action = next(
                item for item in legal["page"]["items"]
                if item["kind"] != "phase.end"
            )
        return created, game, joined, action

    def ready_v2_vote_action(self):
        rows = list(native_v2_rows())
        rows.extend((
            (
                "vote vote_no=42 caller=alice description=Start%20now%3F "
                "yes=1 no=0 abstain=0 num_voters=2 percent_required=50 "
                "team_only=0 current_vote=none can_vote=1 status=active "
                "outcome_turn=-1 outcome_phase=-1"
            ),
            _complete_v2_action_row(
                "action slot=a00000000000000FE kind=player.cast_vote "
                "actor=p:1:10 target_tile=-1 source_city=none "
                "destination_city=none target_unit=none "
                "transport_context=none target_tech=-1 vote_no=42 "
                "target_government=-1 max_rate=0 target_build_kind=none "
                "target_build=-1 source_specialist=-1 "
                "target_specialist=-1 target_extra=-1 activity=none "
                "target_name=vote native_rule=player.cast_vote "
                "target_kind=Vote result=Vote%20Recorded "
                "actor_consuming_always=0 legality=legal "
                "probability_kind=exact probability_min=200 "
                "probability_max=200 args=vote-required"
            ),
        ))
        self.sidecar_factory.observation_rows = tuple(sorted(rows))
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-vote-model",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        self._seed_v2_phase(game)
        legal = game.v2_get_page(joined["agent_id"], "legal_actions", "")
        action = next(
            item for item in legal["page"]["items"]
            if item["kind"] == "player.cast_vote"
        )
        vote_id = action["subject"]["target"]["vote_id"]
        return game, joined, action, vote_id

    @staticmethod
    def v2_batch(game, joined, action, batch_id="batch_one", arguments=None):
        return {
            "schema_version": 2,
            "control_protocol": "full-control-v2",
            "game_id": game.game_id,
            "agent_id": joined["agent_id"],
            "batch_id": batch_id,
            "state_revision": action["state_revision"],
            "commands": [{
                "action_id": action["action_id"],
                "arguments": {} if arguments is None else arguments,
            }],
        }

    def ready_v2_phase_game(
        self, *, timing_mode="default", action_timeout_s=None,
        multiplayer=False, places=2,
    ):
        if timing_mode == "custom":
            timing = {"action_timeout_s": action_timeout_s or 7.5}
        else:
            timeout = {
                "default": 600, "infinite": None,
            }[timing_mode]
            timing = {
                "timing_mode": timing_mode,
                "action_timeout_s": timeout,
            }
        with patch.object(Game, "_poll_v2_sidecars", autospec=True):
            created = self.create(
                mode="multiplayer" if multiplayer else "single",
                places=places,
                control_protocol="full-control-v2",
                **timing,
            )
            game = self.supervisor.game(created["game_id"])
            joined = [
                game.join(
                    created["join_token"], place.number,
                    f"controller-{place.number}",
                    supported_control_protocols=["full-control-v2"],
                )
                for place in game.joinable_places
            ]
        self._mark_v2_running(game)
        return created, game, joined

    @staticmethod
    def phase_evidence(
        game, *, turn=7, phase=0, count=2, active_place=None,
        ready=True, alive=True, done=False, revisions=None,
    ):
        rows = []
        for place in game.joinable_places:
            agent_id = game.place_agents[place.number]
            generation = game.sidecar_generations[place.number]
            row = {
                "place": place.number,
                "generation": generation,
                "sidecar": game.sidecars[place.number],
                "control": game.v2_controls[place.number],
                "agent_id": agent_id,
                "controller_label": game.agents[agent_id]["controller_label"],
                "turn": turn,
                "phase": phase,
                "mode": "players_alternate",
                "count": count,
                "active": place.number == active_place,
                "ready": ready if place.number == active_place else False,
                "alive": alive,
                "done": done if place.number == active_place else False,
            }
            if revisions is not None:
                row["seat_local_revision"] = revisions[place.number]
            rows.append(row)
        return rows

    def test_turn_limit_defaults_are_consistent_across_entrypoints(self):
        self.assertEqual(
            _parser().parse_args(["game", "create"]).turns, 5000,
        )
        parsed = _parser().parse_args(["game", "create"])
        self.assertIsNone(parsed.timing_mode)
        self.assertIsNone(parsed.action_timeout_s)
        self.assertEqual(parsed.control_protocol, "strategic-v1")
        config = self.supervisor._config({})
        self.assertEqual(config["turns"], 5000)
        self.assertEqual(config["timing_mode"], "default")
        self.assertEqual(config["action_timeout_s"], 180)
        self.assertEqual(config["control_protocol"], "strategic-v1")

    def test_join_capability_preflight_is_legacy_safe_and_v2_aware(self):
        with patch(
            "agent_eval.client.request_json",
            side_effect=[{}, {"control_protocol": "full-control-v2"}],
        ):
            self.assertEqual(
                join_capabilities("http://example.test", "game_" + "a" * 24),
                ("strategic-v1", None),
            )
            self.assertEqual(
                join_capabilities("http://example.test", "game_" + "b" * 24),
                ("full-control-v2", ["full-control-v2"]),
            )

    def test_control_protocol_v1_default_is_additive_and_golden(self):
        created = self.create()
        self.assertEqual(created["control_protocol"], "strategic-v1")
        game = self.supervisor.game(created["game_id"])
        manifest = json.loads((game.episode / "manifest.json").read_text())
        self.assertEqual(manifest["control_protocol"], "strategic-v1")
        self.assertEqual(
            manifest["config"]["control_protocol"], "strategic-v1",
        )
        self.assertEqual(game.status()["control_protocol"], "strategic-v1")
        joined = game.join(
            created["join_token"], controller_label="golden-v1-model",
        )
        self.assertEqual(joined["control_protocol"], "strategic-v1")
        self.assertEqual(joined["supported_control_protocols"], [])
        for field in ("objective", "max_turns", "turns_remaining"):
            self.assertNotIn(field, joined)
        self.assertIn(joined["state"], {"starting", "running"})
        self.assertEqual(game.start_count, 1)
        self.assertIn("hard", game._setup_commands())
        self.assertTrue(any(
            "bridge.lua" in command for command in game._setup_commands()
        ))
        self.assertEqual(self.send_mock.call_args_list[-1].args[1], ["start"])

    def test_full_control_v2_negotiates_and_waits_for_native_player_ready(self):
        with self.assertRaises(APIProblem) as invalid:
            self.supervisor._config({"control_protocol": "full-control-v3"})
        self.assertEqual(invalid.exception.status, HTTPStatus.BAD_REQUEST)
        self.sidecar_factory.status_response = (
            "STATUS\tstate=preparing\tserver=1\tseat=ready"
        )
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        self.assertEqual(created["control_protocol"], "full-control-v2")
        self.assertEqual(game.status()["control_protocol"], "full-control-v2")
        self.assertIn("hard", game._setup_commands())
        self.assertFalse(any(
            "bridge.lua" in command for command in game._setup_commands()
        ))
        with self.assertRaises(APIProblem) as missing:
            game.join(
                created["join_token"],
                controller_label="missing-v2-capability",
            )
        self.assertEqual(missing.exception.status, HTTPStatus.UPGRADE_REQUIRED)
        self.assertEqual(
            missing.exception.payload["error"]["code"],
            "unsupported_protocol",
        )
        server = make_supervisor_server(self.supervisor, "127.0.0.1", 0)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            host, port = server.server_address
            base = f"http://{host}:{port}"
            capability_values = (
                None,
                [],
                "full-control-v2",
                ["strategic-v1"],
                ["full-control-v2", "full-control-v2"],
            )
            for index, capabilities in enumerate(capability_values):
                with self.subTest(capabilities=capabilities), self.assertRaises(
                    ClientError,
                ) as rejected:
                    body = {
                        "controller_label": f"http-v2-capability-{index}",
                    }
                    if capabilities is not None:
                        body["supported_control_protocols"] = capabilities
                    request_json(
                        "POST", f"{base}/v1/games/{game.game_id}/join",
                        token=created["join_token"], body=body,
                    )
                self.assertEqual(
                    rejected.exception.status, HTTPStatus.UPGRADE_REQUIRED,
                )
                self.assertIn("full-control-v2", str(rejected.exception))
                self.assertEqual(game.place_agents, {})
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)
        with self.assertRaises(APIProblem) as incompatible:
            game.join(
                created["join_token"],
                controller_label="incompatible-v2-capability",
                supported_control_protocols=["strategic-v1"],
            )
        self.assertEqual(
            incompatible.exception.status, HTTPStatus.UPGRADE_REQUIRED,
        )

        joined = game.join(
            created["join_token"],
            controller_label="capable-v2-model",
            supported_control_protocols=["strategic-v1", "full-control-v2"],
        )
        self.assertEqual(joined["control_protocol"], "full-control-v2")
        self.assertNotIn("next_url", joined)
        self.assertNotIn("actions_url", joined)
        self.assertIn("/v2/games/", joined["state_url"])
        self.assertTrue(joined["v2_transport_available"])
        self.assertEqual(joined["state"], "lobby")
        self.assertEqual(joined["objective"], "win cleanly")
        self.assertEqual(joined["max_turns"], 2)
        self.assertIsNone(joined["turns_remaining"])
        self.assertIn("/v2/games/", joined["health_url"])
        self.assertEqual(
            joined["wait_url"],
            f"{self.supervisor.service_url}/v2/games/"
            f"{game.game_id}/me/wait",
        )
        self.assertEqual(
            joined["openapi_url"],
            f"{self.supervisor.service_url}/v2/openapi.json",
        )
        self.assertFalse(game.start_sent)
        self.assertEqual(game.start_count, 0)
        self.assertIsNone(game.started_at)
        self.assertFalse(any(
            call.args[1] == ["start"] for call in self.send_mock.call_args_list
        ))
        manifest = json.loads((game.episode / "manifest.json").read_text())
        self.assertEqual(manifest["state"], "lobby")
        self.assertEqual(
            manifest["config"]["control_protocol"], "full-control-v2",
        )
        reconnected = game.join(
            joined["agent_token"],
            supported_control_protocols=["full-control-v2", "strategic-v1"],
        )
        self.assertTrue(reconnected["reconnected"])
        self.assertEqual(
            {
                name: reconnected[name]
                for name in ("objective", "max_turns", "turns_remaining")
            },
            {
                "objective": "win cleanly",
                "max_turns": 2,
                "turns_remaining": None,
            },
        )
        with self.assertRaises(APIProblem) as next_gate:
            game.next_for_agent(joined["agent_id"], 0, 0)
        self.assertEqual(next_gate.exception.status, HTTPStatus.CONFLICT)
        with self.assertRaises(APIProblem) as action_gate:
            game.submit_action(joined["agent_id"], {})
        self.assertEqual(action_gate.exception.status, HTTPStatus.CONFLICT)

        gate_server = make_supervisor_server(self.supervisor, "127.0.0.1", 0)
        gate_thread = threading.Thread(
            target=gate_server.serve_forever, daemon=True,
        )
        gate_thread.start()
        try:
            gate_host, gate_port = gate_server.server_address
            gate_base = f"http://{gate_host}:{gate_port}"
            for method, suffix, body in (
                ("GET", "me/next?after_turn=0&wait_s=0", None),
                ("POST", "me/actions", {}),
            ):
                with self.subTest(route=suffix), self.assertRaises(
                    ClientError,
                ) as gated:
                    request_json(
                        method,
                        f"{gate_base}/v1/games/{game.game_id}/{suffix}",
                        token=joined["agent_token"], body=body,
                    )
                self.assertEqual(gated.exception.status, HTTPStatus.CONFLICT)
        finally:
            gate_server.shutdown()
            gate_server.server_close()
            gate_thread.join(2)

    def test_v2_private_health_and_wait_derive_evaluation_turn_budget(self):
        created = self.create(
            control_protocol="full-control-v2",
            turns=12,
            objective="Reach the configured victory condition.",
        )
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="context-aware-model",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        self._seed_v2_phase(game, turn=7)

        health = game.v2_health(joined["agent_id"])
        self.assertEqual(health["objective"], joined["objective"])
        self.assertEqual(health["max_turns"], 12)
        self.assertEqual(health["turns_remaining"], 5)

        waited = game.v2_wait(joined["agent_id"], 0)
        self.assertEqual(waited["wake_reason"], "phase_active")
        self.assertEqual(
            {
                name: waited["health"][name]
                for name in ("objective", "max_turns", "turns_remaining")
            },
            {
                "objective": "Reach the configured victory condition.",
                "max_turns": 12,
                "turns_remaining": 5,
            },
        )

    def test_v2_setup_commands_and_bridge_environment_are_strictly_split(self):
        parsed = _parser().parse_args([
            "supervisor", "--agent-binary", "/tmp/freeciv-agent-test",
        ])
        self.assertEqual(parsed.agent_binary, "/tmp/freeciv-agent-test")
        strategic = self.create(frame_interval=0)
        strategic_game = self.supervisor.game(strategic["game_id"])
        self.assertEqual(
            strategic_game._setup_commands(),
            [
                "set aifill 0",
                "set minplayers 0",
                "set maxplayers 2",
                "set timeout -1",
                "set endturn 2",
                "set plrcolormode PLR_SET",
                "set traitdistribution fixed",
                "set ec_turns 0",
                "set threaded_save disabled",
                "set mapseed 7",
                "set gameseed 7",
                "set scorelog enabled",
                "set scoreloglevel all",
                "set scorefile score.log",
                "set saveturns 1",
                "set autosaves turn|gameover",
                "set savename turn-%04T-%R",
                "create AgentPlace1 classic",
                "create NativePlace2 classic",
                "playercolor AgentPlace1 0067A5",
                "playercolor NativePlace2 F38400",
                "hard",
                "lua unsafe-file " + str(
                    (Path(__file__).resolve().parents[2] / "agent_eval" / "bridge.lua").resolve()
                ),
            ],
        )
        strategic_environment = strategic_game._process_environment("internal-secret")
        self.assertEqual(
            strategic_environment["AGENT_EVAL_INTERNAL_TOKEN"],
            "internal-secret",
        )
        self.assertIn("AGENT_EVAL_BRIDGE_STATUS_PATH", strategic_environment)
        self.assertIn("AGENT_EVAL_TURN_URL", strategic_environment)
        self.assertIn("AGENT_EVAL_TURN_TIMEOUT_S", strategic_environment)

        with patch.dict(os.environ, {
            "AGENT_EVAL_INTERNAL_TOKEN": "inherited-secret",
            "AGENT_EVAL_TURN_URL": "http://inherited.invalid",
        }):
            created = self.create(
                control_protocol="full-control-v2", frame_interval=0,
            )
            game = self.supervisor.game(created["game_id"])
            environment = game._process_environment("must-not-appear")
        self.assertFalse(any(name.startswith("AGENT_EVAL_") for name in environment))
        commands = game._setup_commands()
        self.assertEqual(commands[:9], [
            "set aifill 0",
            "set minplayers 0",
            "set maxplayers 2",
            "set timeout 0",
            "set first_timeout 0",
            "set autotoggle disabled",
            "set phasemode PLAYER",
            "set fixedlength disabled",
            "set turnblock disabled",
        ])
        self.assertLess(commands.index("create AgentPlace1 classic"),
                        commands.index("aitoggle AgentPlace1"))
        self.assertLess(commands.index("aitoggle AgentPlace1"),
                        commands.index("hard"))
        self.assertNotIn("aitoggle NativePlace2", commands)
        self.assertFalse(any("bridge.lua" in command for command in commands))

    def test_v2_phase_deadlines_cover_presets_and_custom_timeouts(self):
        for mode, timeout in (
            ("default", 600.0), ("custom", 60.0),
            ("custom", 7.5), ("infinite", None),
        ):
            with self.subTest(mode=mode):
                _created, game, _joined = self.ready_v2_phase_game(
                    timing_mode=mode, action_timeout_s=timeout,
                )
                evidence = self.phase_evidence(
                    game, phase=1, active_place=1,
                )
                claim, failed = game._update_v2_phase_ledger(evidence, 10.0)
                self.assertIsNone(claim)
                self.assertFalse(failed)
                self.assertEqual(
                    game.v2_phase_ledger["deadline_started_monotonic"], 10.0,
                )
                if timeout is None:
                    claim, failed = game._update_v2_phase_ledger(
                        evidence, 10000.0,
                    )
                    self.assertIsNone(claim)
                    self.assertFalse(failed)
                else:
                    claim, failed = game._update_v2_phase_ledger(
                        evidence, 10.0 + timeout,
                    )
                    self.assertFalse(failed)
                    self.assertEqual(claim["source"], "timeout")
                    self.assertEqual(claim["key"], (7, 1))

    def test_v2_phase_consensus_ignores_seat_local_revisions(self):
        _created, game, _joined = self.ready_v2_phase_game(
            multiplayer=True, places=2,
        )
        evidence = self.phase_evidence(
            game, phase=0, active_place=1, revisions={1: 4, 2: 9001},
        )
        claim, failed = game._update_v2_phase_ledger(evidence, 10.0)
        self.assertIsNone(claim)
        self.assertFalse(failed)
        self.assertEqual(game.v2_phase_ledger["state"], "awaiting_agent")
        self.assertEqual(game.v2_phase_ledger["active_place"], 1)
        evidence[1]["active"] = True
        _claim, failed = game._update_v2_phase_ledger(evidence, 11.0)
        self.assertTrue(failed)
        self.assertIn("v2_phase_protocol", game.invalid_reasons)

    def test_v2_phase_count_is_advisory_and_never_resets_authority(self):
        _created, game, _joined = self.ready_v2_phase_game(
            timing_mode="custom", action_timeout_s=1.0,
        )
        first = self.phase_evidence(
            game, phase=1, active_place=1, count=2,
        )
        claim, failed = game._update_v2_phase_ledger(first, 10.0)
        self.assertIsNone(claim)
        self.assertFalse(failed)
        skewed = self.phase_evidence(
            game, phase=1, active_place=1, count=99,
        )
        claim, failed = game._update_v2_phase_ledger(skewed, 10.5)
        self.assertIsNone(claim)
        self.assertFalse(failed)
        self.assertEqual(game.v2_phase_ledger["key"], (7, 1))
        self.assertEqual(game.v2_phase_ledger["reported_phase_counts"], [99])
        self.assertEqual(
            game.v2_phase_ledger["deadline_started_monotonic"], 10.0,
        )
        claim, failed = game._update_v2_phase_ledger(skewed, 11.0)
        self.assertFalse(failed)
        self.assertEqual(claim["key"], (7, 1))

    def test_v2_native_progress_watchdog_is_independent_and_generous(self):
        _created, game, _joined = self.ready_v2_phase_game(
            timing_mode="infinite",
        )
        native = self.phase_evidence(game, phase=0, active_place=None)
        with patch(
            "agent_eval.supervisor.V2_PHASE_PROGRESS_STALL_S", 300.0,
        ):
            claim, failed = game._update_v2_phase_ledger(native, 10.0)
            self.assertIsNone(claim)
            self.assertFalse(failed)
            _claim, failed = game._update_v2_phase_ledger(native, 309.999)
            self.assertFalse(failed)
            _claim, failed = game._update_v2_phase_ledger(native, 310.0)
        self.assertTrue(failed)
        self.assertEqual(game.state, "failed")
        self.assertIn("v2_phase_progress_stalled", game.invalid_reasons)
        self.assertEqual(game.v2_phase_ledger["state"], "failed")
        self.assertEqual(game.v2_phase_ledger["evidence"], {})

    def test_v2_cancel_terminalizes_phase_without_losing_last_turn(self):
        _created, game, joined = self.ready_v2_phase_game()
        game._update_v2_phase_ledger(
            self.phase_evidence(game, turn=12, phase=1, active_place=1),
            10.0,
        )
        self.assertEqual(game.status()["current_turn"], 12)
        game.cancel()
        self.assertTrue(game.cancel_requested)
        self.assertEqual(game.v2_phase_ledger["state"], "cancelled")
        self.assertEqual(game.v2_phase_ledger["key"], (12, 1))
        self.assertEqual(game.v2_phase_ledger["evidence"], {})
        self.assertIsNone(game.v2_phase_ledger["active_place"])
        self.assertIsNone(game.v2_phase_ledger["end"])
        health = game.v2_health(joined[0]["agent_id"])
        self.assertEqual(health["game_state"], "cancelled")
        self.assertIsNone(health["phase"])

    def test_v2_phase_transition_skew_synchronizes_then_advances(self):
        _created, game, _joined = self.ready_v2_phase_game(
            multiplayer=True, places=2,
        )
        old = self.phase_evidence(game, phase=0, active_place=1)
        game._update_v2_phase_ledger(old, 10.0)
        self.assertEqual(game.v2_phase_ledger["key"], (7, 0))

        mixed = self.phase_evidence(game, phase=0, active_place=1)
        mixed[1].update({"phase": 1, "active": True, "ready": True})
        claim, failed = game._update_v2_phase_ledger(mixed, 20.0)
        self.assertIsNone(claim)
        self.assertFalse(failed)
        self.assertEqual(game.state, "running")
        self.assertEqual(game.v2_phase_ledger["state"], "synchronizing")
        self.assertIsNone(game.v2_phase_ledger["active_place"])
        phase = game.status()["phase"]
        self.assertIsNone(phase["active_controller"])
        self.assertTrue(all(
            controller["state"] == "synchronizing"
            for controller in phase["controllers"]
        ))

        current = self.phase_evidence(game, phase=1, active_place=2)
        claim, failed = game._update_v2_phase_ledger(current, 30.0)
        self.assertIsNone(claim)
        self.assertFalse(failed)
        self.assertEqual(game.v2_phase_ledger["key"], (7, 1))
        self.assertEqual(game.v2_phase_ledger["state"], "awaiting_agent")
        self.assertEqual(game.v2_phase_ledger["active_place"], 2)

    def test_v2_missing_phase_evidence_fails_after_sync_grace(self):
        _created, game, _joined = self.ready_v2_phase_game(
            timing_mode="infinite",
        )
        claim, failed = game._update_v2_phase_ledger([], 10.0)
        self.assertIsNone(claim)
        self.assertFalse(failed)
        self.assertEqual(
            game.v2_phase_ledger["synchronizing_started_monotonic"], 10.0,
        )
        claim, failed = game._update_v2_phase_ledger([], 39.999)
        self.assertIsNone(claim)
        self.assertFalse(failed)
        claim, failed = game._update_v2_phase_ledger([], 40.0)
        self.assertIsNone(claim)
        self.assertTrue(failed)
        self.assertIn("v2_phase_synchronization_stalled", game.invalid_reasons)

    def test_v2_mixed_phase_evidence_fails_after_sync_grace(self):
        _created, game, _joined = self.ready_v2_phase_game(
            timing_mode="infinite", multiplayer=True, places=2,
        )
        mixed = self.phase_evidence(game, phase=0, active_place=1)
        mixed[1].update({"phase": 1, "active": True, "ready": True})
        claim, failed = game._update_v2_phase_ledger(mixed, 10.0)
        self.assertIsNone(claim)
        self.assertFalse(failed)
        claim, failed = game._update_v2_phase_ledger(mixed, 40.0)
        self.assertIsNone(claim)
        self.assertTrue(failed)
        self.assertIn("v2_phase_synchronization_stalled", game.invalid_reasons)

    def test_v2_transient_phase_skew_resets_synchronization_clock(self):
        _created, game, _joined = self.ready_v2_phase_game(
            timing_mode="infinite", multiplayer=True, places=2,
        )
        mixed = self.phase_evidence(game, phase=0, active_place=1)
        mixed[1].update({"phase": 1, "active": True, "ready": True})
        game._update_v2_phase_ledger(mixed, 10.0)
        consensus = self.phase_evidence(game, phase=1, active_place=2)
        _claim, failed = game._update_v2_phase_ledger(consensus, 20.0)
        self.assertFalse(failed)
        self.assertIsNone(
            game.v2_phase_ledger["synchronizing_started_monotonic"],
        )

        next_mixed = self.phase_evidence(game, phase=1, active_place=2)
        next_mixed[0].update({
            "turn": 8, "phase": 0, "active": True, "ready": True,
        })
        _claim, failed = game._update_v2_phase_ledger(next_mixed, 45.0)
        self.assertFalse(failed)
        self.assertEqual(
            game.v2_phase_ledger["synchronizing_started_monotonic"], 45.0,
        )
        _claim, failed = game._update_v2_phase_ledger(next_mixed, 70.0)
        self.assertFalse(failed)
        current = self.phase_evidence(
            game, turn=8, phase=0, active_place=1,
        )
        _claim, failed = game._update_v2_phase_ledger(current, 80.0)
        self.assertFalse(failed)
        self.assertEqual(game.state, "running")
        self.assertIsNone(
            game.v2_phase_ledger["synchronizing_started_monotonic"],
        )

    def test_v2_phase_native_inactive_and_readiness_states(self):
        _created, game, _joined = self.ready_v2_phase_game()
        evidence = self.phase_evidence(game, phase=0, active_place=None)
        game._update_v2_phase_ledger(evidence, 10.0)
        self.assertEqual(game.v2_phase_ledger["state"], "native_phase")
        self.assertIsNone(
            game.v2_phase_ledger["deadline_started_monotonic"],
        )

        evidence = self.phase_evidence(
            game, phase=1, active_place=1, ready=False,
        )
        game._update_v2_phase_ledger(evidence, 20.0)
        self.assertEqual(game.v2_phase_ledger["state"], "phase_not_ready")
        self.assertIsNone(
            game.v2_phase_ledger["deadline_started_monotonic"],
        )
        evidence = self.phase_evidence(
            game, phase=1, active_place=1, alive=False,
        )
        game._update_v2_phase_ledger(evidence, 21.0)
        self.assertEqual(game.v2_phase_ledger["state"], "inactive_done")
        evidence = self.phase_evidence(
            game, phase=1, active_place=1, done=True,
        )
        game._update_v2_phase_ledger(evidence, 22.0)
        self.assertEqual(game.v2_phase_ledger["state"], "inactive_done")

    def test_v2_phase_deadline_survives_same_phase_flicker_and_resets_on_advance(self):
        _created, game, _joined = self.ready_v2_phase_game(
            multiplayer=True, places=2,
        )
        game._update_v2_phase_ledger(
            self.phase_evidence(
                game, phase=0, active_place=1, revisions={1: 1, 2: 50},
            ),
            10.0,
        )
        game._update_v2_phase_ledger(
            self.phase_evidence(
                game, phase=0, active_place=1, ready=False,
                revisions={1: 2, 2: 99},
            ),
            20.0,
        )
        game._update_v2_phase_ledger(
            self.phase_evidence(
                game, phase=0, active_place=1, revisions={1: 3, 2: 100},
            ),
            30.0,
        )
        self.assertEqual(
            game.v2_phase_ledger["deadline_started_monotonic"], 10.0,
        )

        game._update_v2_phase_ledger(
            self.phase_evidence(game, phase=1, active_place=2), 40.0,
        )
        self.assertEqual(game.v2_phase_ledger["active_place"], 2)
        self.assertEqual(
            game.v2_phase_ledger["deadline_started_monotonic"], 40.0,
        )

    def test_v2_phase_forward_jump_is_allowed_but_same_turn_regression_fails(self):
        _created, game, _joined = self.ready_v2_phase_game()
        game._update_v2_phase_ledger(
            self.phase_evidence(game, turn=7, phase=0, active_place=1), 1.0,
        )
        _claim, failed = game._update_v2_phase_ledger(
            self.phase_evidence(game, turn=9, phase=1, active_place=1), 2.0,
        )
        self.assertFalse(failed)
        self.assertEqual(game.v2_phase_ledger["key"], (9, 1))
        _claim, failed = game._update_v2_phase_ledger(
            self.phase_evidence(game, turn=9, phase=0, active_place=1), 3.0,
        )
        self.assertTrue(failed)
        self.assertEqual(game.state, "failed")
        self.assertIn("v2_phase_regression", game.invalid_reasons)

    def test_v2_phase_accessor_is_command_free_and_revalidates_ownership(self):
        _created, game, _joined = self.ready_v2_phase_game()
        sidecar = game.sidecars[1]
        generation = game.sidecar_generations[1]
        observed = []

        def phase_hook(_sidecar):
            observed.append(game.condition._is_owned())
            return {
                "generation": generation,
                "revision": 77,
                "turn": 7,
                "phase": 1,
                "mode": "players_alternate",
                "phase_count": 2,
                "active": True,
                "alive": True,
                "done": False,
                "ready": True,
            }

        self.sidecar_factory.phase_evidence_hook = phase_hook
        reads = sidecar.read_count
        evidence = game._collect_v2_phase_evidence(1, generation, sidecar)
        self.assertEqual(observed, [False])
        self.assertEqual(sidecar.read_count, reads)
        self.assertEqual(evidence["seat_local_revision"], 77)
        with game.condition:
            game.sidecar_generations[1] += 1
        self.assertIsNone(
            game._collect_v2_phase_evidence(1, generation, sidecar),
        )

    def test_v2_timeout_uses_paginated_public_action_and_durable_path(self):
        _created, game, _joined = self.ready_v2_phase_game(
            timing_mode="custom", action_timeout_s=.1,
        )
        self.sidecar_factory.observation_rows = tuple(sorted(
            row.replace(
                "slot=a0000000000000001 kind=phase.end",
                "slot=aFFFFFFFFFFFFFFFF kind=phase.end",
            )
            for row in native_v2_rows(action_count=20)
        ))
        evidence = self.phase_evidence(game, phase=1, active_place=1)
        game._update_v2_phase_ledger(evidence, 1.0)
        claim, failed = game._update_v2_phase_ledger(evidence, 1.1)
        self.assertFalse(failed)
        reads = game.sidecars[1].read_count
        game._run_v2_timeout_phase_end(claim)
        self.assertEqual(self.sidecar_factory.action_count, 1)
        # The exact correlated native phase result is authoritative.
        self.assertEqual(game.sidecars[1].read_count - reads, 1)
        self.assertEqual(
            game.v2_phase_ledger["end"]["receipt_state"], "applied",
        )
        status, receipt = game.v2_get_receipt(
            claim["agent_id"], claim["batch_id"],
        )
        self.assertEqual(status, HTTPStatus.OK)
        self.assertEqual(receipt["receipt_state"], "applied")

    def test_v2_timeout_claim_does_not_dispatch_after_generation_loss(self):
        _created, game, _joined = self.ready_v2_phase_game(
            timing_mode="custom", action_timeout_s=.1,
        )
        evidence = self.phase_evidence(game, phase=1, active_place=1)
        game._update_v2_phase_ledger(evidence, 1.0)
        claim, _failed = game._update_v2_phase_ledger(evidence, 1.1)
        with game.condition:
            game.sidecar_generations[1] += 1
        game._run_v2_timeout_phase_end(claim)
        self.assertEqual(self.sidecar_factory.action_count, 0)
        self.assertEqual(game.v2_phase_ledger["state"], "failed")
        self.assertEqual(game.state, "failed")
        self.assertIn("v2_phase_timeout_failed", game.invalid_reasons)

    def test_v2_reconciliation_grace_starts_at_acceptance_not_claim(self):
        _created, game, _joined = self.ready_v2_phase_game(
            timing_mode="custom", action_timeout_s=.1,
        )
        evidence = self.phase_evidence(game, phase=1, active_place=1)
        game._update_v2_phase_ledger(evidence, 1.0)
        claim, _failed = game._update_v2_phase_ledger(evidence, 1.1)

        retry, failed = game._update_v2_phase_ledger(evidence, 100.0)
        self.assertIsNone(retry)
        self.assertFalse(failed)
        game._note_phase_end_receipt(claim, "reserved")
        retry, failed = game._update_v2_phase_ledger(evidence, 200.0)
        self.assertIsNone(retry)
        self.assertFalse(failed)
        self.assertIsNone(
            game.v2_phase_ledger["end"]["reconcile_started_monotonic"],
        )

        with patch("agent_eval.supervisor.time.monotonic", return_value=300.0):
            game._note_phase_end_receipt(claim, "accepted")
        with patch("agent_eval.supervisor.time.monotonic", return_value=400.0):
            game._note_phase_end_receipt(claim, "ambiguous")
        self.assertEqual(
            game.v2_phase_ledger["end"]["reconcile_started_monotonic"],
            300.0,
        )
        retry, failed = game._update_v2_phase_ledger(evidence, 329.999)
        self.assertIsNone(retry)
        self.assertFalse(failed)
        retry, failed = game._update_v2_phase_ledger(evidence, 330.0)
        self.assertIsNone(retry)
        self.assertTrue(failed)
        self.assertIn("v2_phase_reconciliation_stalled", game.invalid_reasons)

        _created, advanced, _joined = self.ready_v2_phase_game(
            timing_mode="custom", action_timeout_s=.1,
        )
        old = self.phase_evidence(advanced, phase=1, active_place=1)
        advanced._update_v2_phase_ledger(old, 1.0)
        advanced_claim, _failed = advanced._update_v2_phase_ledger(old, 1.1)
        with patch("agent_eval.supervisor.time.monotonic", return_value=300.0):
            advanced._note_phase_end_receipt(advanced_claim, "accepted")
        current = self.phase_evidence(
            advanced, turn=8, phase=0, active_place=1,
        )
        retry, failed = advanced._update_v2_phase_ledger(current, 400.0)
        self.assertIsNone(retry)
        self.assertFalse(failed)
        self.assertEqual(advanced.v2_phase_ledger["key"], (8, 0))

    def test_v2_phase_end_reserve_failure_fails_without_native_send(self):
        _created, game, joined, action = self.ready_v2_action()
        store = game.v2_receipt_store
        with patch.object(
            store, "reserve", side_effect=V2ReceiptStoreError(),
        ), self.assertRaises(APIProblem) as failed:
            game.v2_submit_batch(
                joined["agent_id"],
                self.v2_batch(
                    game, joined, action, batch_id="phase_reserve_failure",
                ),
            )
        self.assertEqual(failed.exception.status, HTTPStatus.INTERNAL_SERVER_ERROR)
        self.assertEqual(self.sidecar_factory.action_count, 0)
        self.assertEqual(game.state, "failed")
        self.assertEqual(game.v2_phase_ledger["state"], "failed")
        self.assertIn("v2_phase_receipt_unavailable", game.invalid_reasons)

    def test_v2_phase_end_incomplete_reservation_fails_without_native_send(self):
        _created, game, joined, action = self.ready_v2_action()
        store = game.v2_receipt_store
        incomplete = ReceiptReservation(
            game_id=game.game_id,
            agent_id=joined["agent_id"],
            batch_id="phase_incomplete_reservation",
            created=False,
            phase="reserved",
            receipt=None,
            _request_hash="test",
        )
        with patch.object(
            store, "reserve", return_value=incomplete,
        ), self.assertRaises(APIProblem) as failed:
            game.v2_submit_batch(
                joined["agent_id"],
                self.v2_batch(
                    game, joined, action,
                    batch_id="phase_incomplete_reservation",
                ),
            )
        self.assertEqual(failed.exception.status, HTTPStatus.INTERNAL_SERVER_ERROR)
        self.assertEqual(self.sidecar_factory.action_count, 0)
        self.assertEqual(game.state, "failed")
        self.assertIn("v2_phase_receipt_unavailable", game.invalid_reasons)

    def test_v2_safe_reserve_conflict_releases_claim_and_keeps_deadline(self):
        _created, game, joined, action = self.ready_v2_action()
        initial_deadline = game.v2_phase_ledger[
            "deadline_started_monotonic"
        ]
        evidence = self.phase_evidence(game, phase=1, active_place=1)
        game._update_v2_phase_ledger(evidence, 10.0)
        store = game.v2_receipt_store
        with patch.object(
            store, "reserve", side_effect=V2ReceiptConflict(),
        ), self.assertRaises(APIProblem) as conflict:
            game.v2_submit_batch(
                joined["agent_id"],
                self.v2_batch(
                    game, joined, action, batch_id="phase_reserve_conflict",
                ),
            )
        self.assertEqual(conflict.exception.status, HTTPStatus.CONFLICT)
        self.assertEqual(self.sidecar_factory.action_count, 0)
        self.assertEqual(game.state, "running")
        self.assertIsNone(game.v2_phase_ledger["end"])
        self.assertEqual(game.v2_phase_ledger["state"], "awaiting_agent")
        self.assertEqual(
            game.v2_phase_ledger["deadline_started_monotonic"],
            initial_deadline,
        )

    def test_v2_agent_timeout_phase_end_race_dispatches_exactly_once(self):
        _created, game, joined = self.ready_v2_phase_game(
            timing_mode="custom", action_timeout_s=.1,
        )
        legal = game.v2_get_page(joined[0]["agent_id"], "legal_actions", "")
        action = next(
            item for item in legal["page"]["items"]
            if item["kind"] == "phase.end"
        )
        agent_batch = self.v2_batch(
            game, joined[0], action, batch_id="agent_phase_race",
        )
        evidence = self.phase_evidence(game, phase=1, active_place=1)
        game._update_v2_phase_ledger(evidence, 1.0)
        claim, _failed = game._update_v2_phase_ledger(evidence, 1.1)
        entered = threading.Event()
        release = threading.Event()

        def blocked_action(
            _sidecar, request_id, _slot, _arguments, _timeout,
            expected_revision, on_accepted,
        ):
            entered.set()
            release.wait(2)
            on_accepted({
                "request_id": request_id,
                "accepted": True,
                "accepted_revision": expected_revision,
            })
            result_revision = expected_revision + 1
            self.sidecar_factory.native_revision = result_revision
            return {
                "request_id": request_id,
                "accepted": True,
                "applied": True,
                "status": "applied",
                "reason": "POSTCONDITION_VERIFIED",
                "accepted_revision": expected_revision,
                "result_revision": result_revision,
                "observation_selector": None,
            }

        self.sidecar_factory.action_hook = blocked_action
        timeout_worker = threading.Thread(
            target=game._run_v2_timeout_phase_end, args=(claim,),
        )
        agent_result = []

        def submit_agent():
            try:
                agent_result.append(game.v2_submit_batch(
                    joined[0]["agent_id"], agent_batch,
                ))
            except APIProblem as exc:
                agent_result.append(exc)

        timeout_worker.start()
        self.assertTrue(entered.wait(1))
        agent_worker = threading.Thread(target=submit_agent)
        agent_worker.start()
        release.set()
        timeout_worker.join(2)
        agent_worker.join(2)
        self.assertFalse(timeout_worker.is_alive())
        self.assertFalse(agent_worker.is_alive())
        self.assertEqual(self.sidecar_factory.action_count, 1)
        self.assertEqual(
            game.v2_phase_ledger["end"]["receipt_state"], "applied",
        )
        self.assertEqual(len(agent_result), 1)
        self.assertIsInstance(agent_result[0], APIProblem)

    def test_v2_agent_phase_end_updates_ledger_from_durable_receipt(self):
        _created, game, joined, action = self.ready_v2_action()
        status, receipt = game.v2_submit_batch(
            joined["agent_id"],
            self.v2_batch(game, joined, action, batch_id="agent_phase_end"),
        )
        self.assertEqual(status, HTTPStatus.OK)
        self.assertEqual(receipt["receipt_state"], "applied")
        self.assertEqual(game.v2_phase_ledger["state"], "ending")
        self.assertEqual(game.v2_phase_ledger["end"]["source"], "agent")
        self.assertEqual(
            game.v2_phase_ledger["end"]["receipt_state"], "applied",
        )

    def test_v2_applied_phase_end_does_not_require_inactive_observation(self):
        _created, game, joined, action = self.ready_v2_action()
        sidecar = game.sidecars[joined["place"]]
        reads_before = sidecar.read_count
        reads = 0

        def reject_post_result_read(current, _request_id, _timeout):
            nonlocal reads
            reads += 1
            if reads > 1:
                raise AssertionError(
                    "an applied phase end has no active post-result observation"
                )
            return {
                "generation": current.generation,
                "native_revision": self.sidecar_factory.native_revision,
                "rows": self.sidecar_factory.observation_rows,
            }

        self.sidecar_factory.read_hook = reject_post_result_read
        status, receipt = game.v2_submit_batch(
            joined["agent_id"],
            self.v2_batch(
                game, joined, action, batch_id="phase_end_no_snapshot",
            ),
        )
        self.assertEqual(status, HTTPStatus.OK)
        self.assertEqual(receipt["receipt_state"], "applied")
        self.assertEqual(sidecar.read_count - reads_before, 1)
        self.assertEqual(
            game.v2_phase_ledger["end"]["receipt_state"], "applied",
        )

    def test_v2_applied_vote_does_not_require_resolved_vote_observation(self):
        game, joined, action, vote_id = self.ready_v2_vote_action()
        reads = 0

        def reject_post_result_read(current, _request_id, _timeout):
            nonlocal reads
            reads += 1
            if reads > 1:
                raise SidecarError("snapshot_gone")
            return {
                "generation": current.generation,
                "native_revision": self.sidecar_factory.native_revision,
                "rows": self.sidecar_factory.observation_rows,
            }

        self.sidecar_factory.read_hook = reject_post_result_read
        status, receipt = game.v2_submit_batch(
            joined["agent_id"],
            self.v2_batch(
                game, joined, action, batch_id="vote_no_snapshot",
                arguments={"vote_id": vote_id, "vote": "yes"},
            ),
        )
        self.assertEqual(status, HTTPStatus.OK)
        self.assertEqual(receipt["receipt_state"], "applied")
        self.assertEqual(reads, 2)

    def test_v2_nondecisive_vote_uses_fresh_receipt_revision(self):
        game, joined, action, vote_id = self.ready_v2_vote_action()
        status, receipt = game.v2_submit_batch(
            joined["agent_id"],
            self.v2_batch(
                game, joined, action, batch_id="vote_fresh_snapshot",
                arguments={"vote_id": vote_id, "vote": "yes"},
            ),
        )
        self.assertEqual(status, HTTPStatus.OK)
        self.assertEqual(receipt["receipt_state"], "applied")
        self.assertGreater(
            receipt["state_revision"]["revision"],
            action["state_revision"]["revision"],
        )

    def test_v2_agent_phase_end_rejection_releases_claim_and_can_retry(self):
        _created, game, joined, action = self.ready_v2_action()
        initial_deadline = game.v2_phase_ledger[
            "deadline_started_monotonic"
        ]
        evidence = self.phase_evidence(game, phase=1, active_place=1)
        game._update_v2_phase_ledger(evidence, 10.0)
        self.sidecar_factory.action_error = SidecarActionNotAccepted(
            "native_bad_request",
        )
        rejected_batch = self.v2_batch(
            game, joined, action, batch_id="agent_phase_rejected",
        )
        status, receipt = game.v2_submit_batch(
            joined["agent_id"], rejected_batch,
        )
        self.assertEqual(status, HTTPStatus.UNPROCESSABLE_ENTITY)
        self.assertEqual(receipt["receipt_state"], "rejected")
        self.assertEqual(game.state, "running")
        self.assertIsNone(game.v2_phase_ledger["end"])
        self.assertEqual(game.v2_phase_ledger["state"], "awaiting_agent")
        self.assertEqual(
            game.v2_phase_ledger["deadline_started_monotonic"],
            initial_deadline,
        )
        stored_status, stored = game.v2_get_receipt(
            joined["agent_id"], "agent_phase_rejected",
        )
        self.assertEqual(stored_status, HTTPStatus.OK)
        self.assertEqual(stored["receipt_state"], "rejected")
        duplicate_status, duplicate = game.v2_submit_batch(
            joined["agent_id"], rejected_batch,
        )
        self.assertEqual(duplicate_status, HTTPStatus.UNPROCESSABLE_ENTITY)
        self.assertEqual(duplicate["receipt_state"], "rejected")
        self.assertTrue(duplicate["idempotent"])
        self.assertEqual(duplicate["batch_id"], receipt["batch_id"])
        self.assertEqual(self.sidecar_factory.action_count, 1)

        self.sidecar_factory.action_error = None
        retry_status, retry = game.v2_submit_batch(
            joined["agent_id"],
            self.v2_batch(
                game, joined, action, batch_id="agent_phase_retry",
            ),
        )
        self.assertEqual(retry_status, HTTPStatus.OK)
        self.assertEqual(retry["receipt_state"], "applied")
        self.assertEqual(self.sidecar_factory.action_count, 2)

    def test_v2_timeout_phase_end_rejection_fails_once_with_durable_receipt(self):
        _created, game, _joined = self.ready_v2_phase_game(
            timing_mode="custom", action_timeout_s=.1,
        )
        evidence = self.phase_evidence(game, phase=1, active_place=1)
        game._update_v2_phase_ledger(evidence, 1.0)
        claim, _failed = game._update_v2_phase_ledger(evidence, 1.1)
        self.sidecar_factory.action_error = SidecarActionNotAccepted(
            "native_bad_request",
        )
        game._run_v2_timeout_phase_end(claim)
        self.assertEqual(self.sidecar_factory.action_count, 1)
        self.assertEqual(game.state, "failed")
        self.assertEqual(game.v2_phase_ledger["state"], "failed")
        self.assertIn("v2_phase_timeout_rejected", game.invalid_reasons)
        stored_status, stored = game.v2_get_receipt(
            claim["agent_id"], claim["batch_id"],
        )
        self.assertEqual(stored_status, HTTPStatus.OK)
        self.assertEqual(stored["receipt_state"], "rejected")
        game._run_v2_timeout_phase_end(claim)
        self.assertEqual(self.sidecar_factory.action_count, 1)
        _stored_status, replayed = game.v2_get_receipt(
            claim["agent_id"], claim["batch_id"],
        )
        self.assertEqual(replayed, stored)

    def test_v2_ambiguous_phase_end_is_never_replayed_and_stall_fails(self):
        _created, game, _joined = self.ready_v2_phase_game(
            timing_mode="custom", action_timeout_s=.1,
        )
        evidence = self.phase_evidence(game, phase=1, active_place=1)
        game._update_v2_phase_ledger(evidence, 1.0)
        claim, _failed = game._update_v2_phase_ledger(evidence, 1.1)

        def ambiguous(
            _sidecar, request_id, _slot, _arguments, _timeout,
            expected_revision, on_accepted,
        ):
            on_accepted({
                "request_id": request_id,
                "accepted": True,
                "accepted_revision": expected_revision,
            })
            raise SidecarActionAmbiguous("action_outcome_ambiguous")

        self.sidecar_factory.action_hook = ambiguous
        game._run_v2_timeout_phase_end(claim)
        self.assertEqual(self.sidecar_factory.action_count, 1)
        self.assertEqual(game.v2_phase_ledger["state"], "ambiguous_ending")
        reconcile_started = game.v2_phase_ledger["end"][
            "reconcile_started_monotonic"
        ]
        retry, failed = game._update_v2_phase_ledger(
            evidence, reconcile_started + 1.0,
        )
        self.assertIsNone(retry)
        self.assertFalse(failed)
        retry, failed = game._update_v2_phase_ledger(
            [], reconcile_started + 30.0,
        )
        self.assertIsNone(retry)
        self.assertTrue(failed)
        self.assertEqual(self.sidecar_factory.action_count, 1)
        self.assertIn("v2_phase_reconciliation_stalled", game.invalid_reasons)

    def test_v2_phase_telemetry_is_public_safe_and_absent_from_v1(self):
        strategic = self.create()
        strategic_game = self.supervisor.game(strategic["game_id"])
        self.assertNotIn("phase", strategic_game.status())
        self.assertNotIn("set fixedlength disabled", strategic_game._setup_commands())
        self.assertNotIn("set turnblock disabled", strategic_game._setup_commands())

        _created, game, _joined = self.ready_v2_phase_game()
        game._update_v2_phase_ledger(
            self.phase_evidence(
                game, phase=1, active_place=1, revisions={1: 987654},
            ),
            10.0,
        )
        phase = game.status()["phase"]
        self.assertEqual(phase["state"], "awaiting_agent")
        serialized = json.dumps(phase).casefold()
        for private_name in (
            "revision", "slot", "action_id", "token", "batch_id", "hash",
        ):
            self.assertNotIn(private_name, serialized)

    def test_v2_phase_end_journal_advances_once_and_health_is_seat_filtered(self):
        _created, game, joined, action = self.ready_v2_action()
        status, receipt = game.v2_submit_batch(
            joined["agent_id"],
            self.v2_batch(game, joined, action, "phase_event_advanced"),
        )
        self.assertEqual(status, HTTPStatus.OK)
        self.assertEqual(receipt["receipt_state"], "applied")
        _claim, failed = game._update_v2_phase_ledger(
            self.phase_evidence(game, turn=8, phase=0, active_place=1),
            time.monotonic(),
        )
        self.assertFalse(failed)
        page = game.phase_events(0, 100)
        self.assertEqual(len(page["phase_events"]), 1)
        event = page["phase_events"][0]
        self.assertEqual(event["sequence"], 1)
        self.assertEqual((event["turn"], event["phase"]), (7, 1))
        self.assertEqual(event["source"], "agent")
        self.assertEqual(event["receipt_state"], "applied")
        self.assertEqual(event["resolution"], "advanced")
        self.assertEqual(event["place"], 1)
        self.assertEqual(event["seat_id"], "place-1")
        self.assertEqual(event["player_name"], "AgentPlace1")
        self.assertEqual(event["player_color"], "#0067A5")
        self.assertEqual(event["controller_label"], "codex-batch-model")
        self.assertEqual(event["controller_type"], "external")
        self.assertGreaterEqual(event["ended_at"], event["deadline_started_at"])
        self.assertGreaterEqual(event["elapsed_s"], 0)
        game._terminalize_v2_phase_locked("completed")
        self.assertEqual(len(game.phase_events(0, 100)["phase_events"]), 1)
        health = game.v2_health(joined["agent_id"])
        self.assertEqual(health["last_phase_end"], event)
        self.assertNotIn("agent_id", json.dumps(event))

    def test_v2_phase_event_waits_for_terminal_receipt_after_advance_race(self):
        _created, game, joined, action = self.ready_v2_action()
        accepted = threading.Event()
        release = threading.Event()

        def blocked_action(
            _sidecar, request_id, _slot, _arguments, _timeout,
            expected_revision, on_accepted,
        ):
            on_accepted({
                "request_id": request_id,
                "accepted": True,
                "accepted_revision": expected_revision,
            })
            accepted.set()
            release.wait(2)
            result_revision = expected_revision + 1
            self.sidecar_factory.native_revision = result_revision
            return {
                "request_id": request_id,
                "accepted": True,
                "applied": True,
                "status": "applied",
                "reason": "POSTCONDITION_VERIFIED",
                "accepted_revision": expected_revision,
                "result_revision": result_revision,
                "observation_selector": None,
            }

        self.sidecar_factory.action_hook = blocked_action
        result = []
        worker = threading.Thread(target=lambda: result.append(
            game.v2_submit_batch(
                joined["agent_id"],
                self.v2_batch(
                    game, joined, action, "phase_event_accepted_race",
                ),
            ),
        ))
        worker.start()
        self.assertTrue(accepted.wait(1))
        game._update_v2_phase_ledger(
            self.phase_evidence(game, turn=8, phase=0, active_place=1),
            time.monotonic(),
        )
        self.assertEqual(game.phase_events(0, 100)["phase_events"], [])
        release.set()
        worker.join(2)
        self.assertFalse(worker.is_alive())
        self.assertEqual(result[0][1]["receipt_state"], "applied")
        events = game.phase_events(0, 100)["phase_events"]
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["receipt_state"], "applied")
        self.assertEqual(events[0]["resolution"], "advanced")

    def test_v2_phase_events_remain_in_native_order_across_late_receipts(self):
        _created, game, _joined = self.ready_v2_phase_game(
            multiplayer=True, places=2,
        )
        now = time.time()
        monotonic = time.monotonic()
        earlier = {
            "claim_id": "earlier-claim",
            "key": (7, 0),
            "place": 1,
            "source": "agent",
            "receipt_state": "accepted",
            "deadline_started_at": now,
            "deadline_started_monotonic": monotonic,
        }
        later = {
            "claim_id": "later-claim",
            "key": (7, 1),
            "place": 2,
            "source": "agent",
            "receipt_state": "applied",
            "deadline_started_at": now,
            "deadline_started_monotonic": monotonic,
        }
        with game.condition:
            self.assertFalse(
                game._finalize_v2_phase_end_locked(earlier, "advanced"),
            )
            self.assertFalse(
                game._finalize_v2_phase_end_locked(later, "advanced"),
            )
        self.assertEqual(game.phase_events(0, 100)["phase_events"], [])

        game._note_phase_end_receipt(earlier, "applied")

        events = game.phase_events(0, 100)["phase_events"]
        self.assertEqual(
            [(event["turn"], event["phase"]) for event in events],
            [(7, 0), (7, 1)],
        )
        self.assertEqual([event["sequence"] for event in events], [1, 2])
        self.assertEqual(game.v2_pending_phase_ends, {})

    def test_v2_phase_events_complete_waits_for_late_terminal_receipt(self):
        _created, game, joined, action = self.ready_v2_action()
        accepted = threading.Event()
        release = threading.Event()

        def blocked_action(
            _sidecar, request_id, _slot, _arguments, _timeout,
            expected_revision, on_accepted,
        ):
            on_accepted({
                "request_id": request_id,
                "accepted": True,
                "accepted_revision": expected_revision,
            })
            accepted.set()
            release.wait(2)
            result_revision = expected_revision + 1
            self.sidecar_factory.native_revision = result_revision
            return {
                "request_id": request_id,
                "accepted": True,
                "applied": True,
                "status": "applied",
                "reason": "POSTCONDITION_VERIFIED",
                "accepted_revision": expected_revision,
                "result_revision": result_revision,
                "observation_selector": None,
            }

        self.sidecar_factory.action_hook = blocked_action
        result = []
        worker = threading.Thread(target=lambda: result.append(
            game.v2_submit_batch(
                joined["agent_id"],
                self.v2_batch(
                    game, joined, action, "phase_event_late_terminal",
                ),
            ),
        ))
        worker.start()
        self.assertTrue(accepted.wait(1))
        with game.condition:
            game.state = "completed"
            game._terminalize_v2_phase_locked("completed")
        pending = game.phase_events(0, 100)
        self.assertEqual(pending["phase_events"], [])
        self.assertFalse(pending["complete"])

        release.set()
        worker.join(2)
        self.assertFalse(worker.is_alive())
        self.assertIn(
            result[0][1]["receipt_state"], {"applied", "ambiguous"},
        )
        complete = game.phase_events(0, 100)
        self.assertTrue(complete["complete"])
        self.assertEqual(len(complete["phase_events"]), 1)
        self.assertEqual(
            complete["phase_events"][0]["receipt_state"],
            result[0][1]["receipt_state"],
        )
        self.assertEqual(complete["phase_events"][0]["resolution"], "terminal")

    def test_v2_phase_end_journal_terminal_failed_and_timeout_sources(self):
        for terminal_state, resolution in (
            ("completed", "terminal"), ("failed", "failed"),
        ):
            with self.subTest(terminal_state=terminal_state):
                _created, game, joined, action = self.ready_v2_action()
                game.v2_submit_batch(
                    joined["agent_id"],
                    self.v2_batch(
                        game, joined, action,
                        f"phase_event_{terminal_state}",
                    ),
                )
                with game.condition:
                    game._terminalize_v2_phase_locked(terminal_state)
                events = game.phase_events(0, 100)["phase_events"]
                self.assertEqual(len(events), 1)
                self.assertEqual(events[0]["resolution"], resolution)

        _created, game, joined = self.ready_v2_phase_game(
            timing_mode="custom", action_timeout_s=.1,
        )
        evidence = self.phase_evidence(game, phase=1, active_place=1)
        game._update_v2_phase_ledger(evidence, 1.0)
        claim, failed = game._update_v2_phase_ledger(evidence, 1.1)
        self.assertFalse(failed)
        game._run_v2_timeout_phase_end(claim)
        game._update_v2_phase_ledger(
            self.phase_evidence(game, turn=8, phase=0, active_place=1),
            2.0,
        )
        timeout_event = game.phase_events(0, 100)["phase_events"][0]
        self.assertEqual(timeout_event["source"], "timeout")
        self.assertEqual(
            game.v2_health(joined[0]["agent_id"])["last_phase_end"],
            timeout_event,
        )

    def test_v2_health_last_phase_end_never_crosses_seats(self):
        _created, game, joined = self.ready_v2_phase_game(
            multiplayer=True, places=2,
        )
        events = []
        for place_number in (1, 2):
            place = game.places[place_number - 1]
            agent = game.agents[game.place_agents[place_number]]
            events.append(game.v2_phase_event_journal.append({
                "turn": 7,
                "phase": place_number - 1,
                "place": place_number,
                "seat_id": place.seat_id,
                "player_name": place.player_name,
                "player_color": place.player_color,
                "controller_label": agent["controller_label"],
                "controller_type": "external",
                "source": "timeout" if place_number == 2 else "agent",
                "receipt_state": "applied",
                "resolution": "advanced",
                "deadline_started_at": 1000.0 + place_number,
                "ended_at": 1001.0 + place_number,
                "elapsed_s": 1.0,
            }))
        first_health = game.v2_health(joined[0]["agent_id"])
        second_health = game.v2_health(joined[1]["agent_id"])
        self.assertEqual(first_health["last_phase_end"], events[0])
        self.assertEqual(second_health["last_phase_end"], events[1])
        self.assertNotEqual(
            first_health["last_phase_end"]["place"],
            second_health["last_phase_end"]["place"],
        )

    def test_v2_phase_event_journal_failure_invalidates_without_details(self):
        _created, game, joined, action = self.ready_v2_action()
        game.v2_submit_batch(
            joined["agent_id"],
            self.v2_batch(game, joined, action, "phase_event_write_failure"),
        )
        with patch.object(
            game.v2_phase_event_journal,
            "append",
            side_effect=V2PhaseEventJournalError(),
        ):
            _claim, failed = game._update_v2_phase_ledger(
                self.phase_evidence(game, turn=8, phase=0, active_place=1),
                time.monotonic(),
            )
        self.assertTrue(failed)
        self.assertEqual(game.state, "failed")
        self.assertEqual(game.v2_phase_ledger["state"], "failed")
        self.assertEqual(
            game.invalid_reasons.count("v2_phase_event_journal_unavailable"),
            1,
        )
        serialized = json.dumps(game.status())
        self.assertNotIn("phase_event_write_failure", serialized)
        self.assertNotIn(str(game.episode), serialized)

    def test_v2_phase_events_public_http_pagination_queries_and_v1_absence(self):
        strategic = self.create()
        strategic_game = self.supervisor.game(strategic["game_id"])
        self.assertFalse((strategic_game.episode / PHASE_EVENT_FILENAME).exists())
        self.assertNotIn("phase_events_url", strategic_game.urls())

        _created, game, joined, action = self.ready_v2_action()
        game.v2_submit_batch(
            joined["agent_id"],
            self.v2_batch(game, joined, action, "phase_http_one"),
        )
        game._update_v2_phase_ledger(
            self.phase_evidence(game, turn=8, phase=0, active_place=1),
            time.monotonic(),
        )
        first = game.phase_events(0, 100)["phase_events"][0]
        second = dict(first)
        second.pop("sequence")
        second.update({
            "turn": 8,
            "phase": 0,
            "deadline_started_at": first["ended_at"],
            "ended_at": first["ended_at"] + 1,
            "elapsed_s": 1,
        })
        game.v2_phase_event_journal.append(second)

        server = make_supervisor_server(self.supervisor, "127.0.0.1", 0)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        host, port = server.server_address
        root = f"http://{host}:{port}/v1/games"
        url = f"{root}/{game.game_id}/phase-events"
        try:
            first_page = request_json("GET", f"{url}?limit=1")
            self.assertEqual(len(first_page["phase_events"]), 1)
            self.assertEqual(first_page["next_after_sequence"], 1)
            self.assertTrue(first_page["has_more"])
            second_page = request_json(
                "GET", f"{url}?after_sequence=1&limit=100",
            )
            self.assertEqual(
                [event["sequence"] for event in second_page["phase_events"]],
                [2],
            )
            self.assertFalse(second_page["has_more"])
            serialized = json.dumps(first_page).casefold()
            for private_name in (
                "agent_id", "batch_id", "generation", "revision",
                "action_id", "slot", "hash", "bearer", "native_ref",
            ):
                self.assertNotIn(private_name, serialized)
            for query in (
                "?extra=1", "?after_sequence=", "?after_sequence=-1",
                "?after_sequence=+1", "?after_sequence=01", "?limit=0",
                "?limit=251", "?limit=1&limit=2",
            ):
                with self.subTest(query=query), self.assertRaises(
                    ClientError,
                ) as rejected:
                    request_json("GET", url + query)
                self.assertEqual(rejected.exception.status, HTTPStatus.BAD_REQUEST)
            with self.assertRaises(ClientError) as trailing:
                request_json("GET", url + "/")
            self.assertEqual(trailing.exception.status, HTTPStatus.NOT_FOUND)
            with self.assertRaises(ClientError) as v1_absent:
                request_json(
                    "GET",
                    f"{root}/{strategic_game.game_id}/phase-events",
                )
            self.assertEqual(v1_absent.exception.status, HTTPStatus.NOT_FOUND)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)

    def test_missing_agent_binary_is_lazy_and_v1_remains_available(self):
        isolated = Supervisor(
            self.directory.name + "-lazy", "admin-lazy",
            binary="/unused/freeciv-server",
            process_factory=lambda *args, **kwargs: None,
            agent_binary="/definitely/missing/freeciv-agent",
        )
        try:
            created = isolated.create_game({
                "mode": "single", "places": 2, "turns": 2, "seed": 9,
                "ruleset": "classic", "objective": "v1 remains available",
                "lobby_timeout_s": 0, "frame_interval": 0,
            })
            self.assertEqual(created["control_protocol"], "strategic-v1")
            with self.assertRaises(APIProblem) as unavailable:
                isolated.create_game({
                    "mode": "single", "places": 2, "turns": 2, "seed": 10,
                    "ruleset": "classic", "objective": "v2 requires binary",
                    "control_protocol": "full-control-v2",
                    "lobby_timeout_s": 0, "frame_interval": 0,
                })
            self.assertEqual(
                unavailable.exception.status, HTTPStatus.SERVICE_UNAVAILABLE,
            )
            self.assertEqual(
                unavailable.exception.payload["error"]["code"],
                "sidecar_unavailable",
            )
        finally:
            isolated.close()

    def test_v2_two_seat_ready_gating_reconnect_and_start_once(self):
        self.sidecar_factory.status_response = (
            "STATUS\tstate=preparing\tserver=1\tseat=ready"
        )
        created = self.create(
            mode="multiplayer", places=2, control_protocol="full-control-v2",
        )
        game = self.supervisor.game(created["game_id"])
        first = game.join(
            created["join_token"], 1, "codex-model-one",
            supported_control_protocols=["full-control-v2"],
        )
        self.assertEqual(game.state, "lobby")
        self.assertEqual(game.start_count, 0)
        self.assertEqual(len(self.sidecar_factory.created), 1)
        second = game.join(
            created["join_token"], 2, "claude-model-two",
            supported_control_protocols=["full-control-v2"],
        )
        self.assertEqual(game.state, "lobby")
        self.assertEqual(game.start_count, 0)
        self.assertTrue(game.v2_pregame_gate_open)
        self.assertEqual(len(self.sidecar_factory.created), 2)
        reconnected = game.join(
            first["agent_token"],
            supported_control_protocols=["full-control-v2"],
        )
        self.assertTrue(reconnected["reconnected"])
        self.assertEqual(len(self.sidecar_factory.created), 2)
        self.assertEqual(game.start_count, 0)
        self.assertEqual(sum(
            call.args[1] == ["start"]
            for call in self.send_mock.call_args_list
        ), 0)
        self.assertEqual(second["place"], 2)

    def test_v2_last_native_ready_action_starts_without_console_start(self):
        self.sidecar_factory.status_response = (
            "STATUS\tstate=preparing\tserver=1\tseat=ready"
        )
        self.sidecar_factory.observation_rows = native_v2_pregame_rows()
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"],
            controller_label="codex-pregame-control",
            supported_control_protocols=["full-control-v2"],
        )
        legal = game.v2_get_page(
            joined["agent_id"], "legal_actions", "",
        )["page"]["items"]
        ready = next(item for item in legal if item["kind"] == "pregame.set_ready")
        status, receipt = game.v2_submit_batch(
            joined["agent_id"],
            self.v2_batch(
                game, joined, ready, "batch_pregame_ready", {"ready": True},
            ),
        )
        self.assertEqual(status, HTTPStatus.OK)
        self.assertEqual(receipt["receipt_state"], "applied")
        self.assertEqual(game.state, "starting")
        self.assertEqual(game.start_count, 1)
        self.assertFalse(any(
            call.args[1] == ["start"] for call in self.send_mock.call_args_list
        ))

    def test_v2_failed_take_rolls_back_and_stale_generation_is_ignored(self):
        self.sidecar_factory.fail_next = True
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        with self.assertRaises(APIProblem) as unavailable:
            game.join(
                created["join_token"], controller_label="codex-failed-take",
                supported_control_protocols=["full-control-v2"],
            )
        self.assertEqual(unavailable.exception.status, HTTPStatus.SERVICE_UNAVAILABLE)
        self.assertEqual(unavailable.exception.payload["error"]["code"],
                         "sidecar_unavailable")
        self.assertEqual(game.state, "lobby")
        self.assertEqual(game.agents, {})
        self.assertEqual(game.place_agents, {})
        self.assertEqual(game.v2_controls, {})
        failed = self.sidecar_factory.created[-1]

        joined = game.join(
            created["join_token"], controller_label="codex-retry-model",
            supported_control_protocols=["full-control-v2"],
        )
        self.assertTrue(joined["v2_transport_available"])
        state_before = game.state
        failed.die()
        self.assertEqual(game.state, state_before)
        self.assertEqual(game.sidecar_generations[1], 2)

    def test_v2_ready_then_immediate_exit_never_commits_join(self):
        self.sidecar_factory.die_after_ready = True
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        with self.assertRaises(APIProblem) as unavailable:
            game.join(
                created["join_token"], controller_label="codex-racy-model",
                supported_control_protocols=["full-control-v2"],
            )
        self.assertEqual(unavailable.exception.status, HTTPStatus.SERVICE_UNAVAILABLE)
        self.assertEqual(game.state, "lobby")
        self.assertEqual(game.agents, {})
        self.assertEqual(game.place_agents, {})
        self.assertFalse(game.start_sent)

    def test_v2_sidecar_death_recovers_in_game_and_fails_in_lobby(self):
        """A seat is never handed to Freeciv AI, whichever way it is lost."""
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        game.join(
            created["join_token"], controller_label="codex-current-model",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        game.start_sent = True
        current = self.sidecar_factory.created[-1]
        current.die()
        # Mid-game the server still holds the state and its autosaves, so the
        # seat comes back on a new generation instead of ending the game.
        self.assertEqual(game.state, "running")
        self.assertEqual(game.sidecar_generations[1], 2)
        self.assertNotIn("sidecar_exited", game.invalid_reasons)
        self.assertFalse(game.sidecars_stopping)
        self.assertFalse(any(
            call.args[1] == ["aitoggle AgentPlace1"]
            for call in self.send_mock.call_args_list
        ))

        lobby_created = self.create(
            mode="multiplayer", places=2, control_protocol="full-control-v2",
        )
        lobby_game = self.supervisor.game(lobby_created["game_id"])
        lobby_game.join(
            lobby_created["join_token"], 1, "codex-lobby-death",
            supported_control_protocols=["full-control-v2"],
        )
        self.assertEqual(lobby_game.state, "lobby")
        self.sidecar_factory.created[-1].die()
        self.assertEqual(lobby_game.state, "failed")
        self.assertFalse(lobby_game.start_sent)

    def test_v2_sidecar_exit_persists_private_sanitized_diagnostic(self):
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        game.join(
            created["join_token"], controller_label="codex-diagnostic-model",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        game.start_sent = True
        sidecar = self.sidecar_factory.created[-1]
        sidecar.state = "failed"
        sidecar.error_code = "process_exited"
        sidecar.callback(sidecar.generation, {
            **sidecar.public_health(),
            "exit_code": 17,
            "last_seen_at": 1234.5,
            "stopped_at": 1235.5,
            "join_token": "must-not-persist",
            "raw_frame": "must-not-persist",
            "private_observation": "must-not-persist",
        })

        path = game.episode / "sidecar-exit-diagnostic.json"
        diagnostic = json.loads(path.read_text(encoding="utf-8"))
        timestamp = diagnostic.pop("timestamp")
        self.assertIsInstance(timestamp, float)
        self.assertEqual(diagnostic, {
            "died_at": {
                "turn": None,
                "phase": None,
                "phase_ledger_state": "synchronizing",
                "seat_local_revision": None,
                # The last state the supervisor accepted while the seat still
                # worked, not the null the dying generation now reports.
                "last_status_client_state": "running",
            },
            "error_code": "process_exited",
            "forensics": {},
            "exit_code": 17,
            "game_id": created["game_id"],
            "generation": 1,
            "last_seen_at": 1234.5,
            "place": 1,
            "sidecar_state": "failed",
            "stopped_at": 1235.5,
        })
        self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
        text = path.read_text(encoding="utf-8")
        self.assertNotIn("must-not-persist", text)
        # The diagnostic is written whichever way the loss is resolved; this
        # seat was mid-game on a live server, so it is recovered.
        self.assertEqual(game.state, "running")
        self.assertNotIn("sidecar_exited", game.invalid_reasons)

    def test_v2_join_does_not_depend_on_console_start(self):
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        self.send_mock.reset_mock()
        self.send_mock.side_effect = RuntimeError("start rejected")
        joined = game.join(
            created["join_token"], controller_label="codex-start-failure",
            supported_control_protocols=["full-control-v2"],
        )
        self.assertEqual(joined["state"], "lobby")
        self.assertTrue(joined["agent_token"])
        self.assertTrue(joined["v2_transport_available"])
        self.send_mock.assert_not_called()
        self.assertEqual(self.sidecar_factory.created[-1].stop_count, 0)

    def test_v2_cancel_lobby_timeout_and_close_cleanup_sidecars(self):
        created = self.create(
            mode="multiplayer", places=2, control_protocol="full-control-v2",
            lobby_timeout_s=0.1,
        )
        game = self.supervisor.game(created["game_id"])
        game.join(
            created["join_token"], 1, "codex-lobby-model",
            supported_control_protocols=["full-control-v2"],
        )
        first = self.sidecar_factory.created[-1]
        game._lobby_watchdog()
        self.assertGreaterEqual(first.stop_count, 1)

        created = self.create(
            mode="multiplayer", places=2, control_protocol="full-control-v2",
        )
        game = self.supervisor.game(created["game_id"])
        game.join(
            created["join_token"], 1, "claude-cancel-model",
            supported_control_protocols=["full-control-v2"],
        )
        cancelled = self.sidecar_factory.created[-1]
        game.cancel()
        self.assertGreaterEqual(cancelled.stop_count, 1)

        created = self.create(
            mode="multiplayer", places=2, control_protocol="full-control-v2",
        )
        game = self.supervisor.game(created["game_id"])
        game.join(
            created["join_token"], 1, "pi-close-model",
            supported_control_protocols=["full-control-v2"],
        )
        closed = self.sidecar_factory.created[-1]
        self.supervisor.close()
        self.assertGreaterEqual(closed.stop_count, 1)

    def test_v2_ready_commit_rechecks_every_teardown_gate(self):
        for gate in ("cancel_requested", "error", "sidecars_stopping"):
            with self.subTest(gate=gate):
                start_gate = threading.Event()
                self.sidecar_factory.start_gate = start_gate
                created = self.create(
                    mode="multiplayer", places=2,
                    control_protocol="full-control-v2",
                )
                game = self.supervisor.game(created["game_id"])
                result: list[object] = []
                created_before = len(self.sidecar_factory.created)

                def join():
                    try:
                        result.append(game.join(
                            created["join_token"], 1, f"codex-{gate}",
                            supported_control_protocols=["full-control-v2"],
                        ))
                    except Exception as exc:
                        result.append(exc)

                thread = threading.Thread(target=join)
                thread.start()
                deadline = time.monotonic() + 1
                while (
                    len(self.sidecar_factory.created) <= created_before
                    or self.sidecar_factory.created[-1].start_count == 0
                ):
                    self.assertLess(time.monotonic(), deadline)
                    time.sleep(0.005)
                with game.condition:
                    if gate == "error":
                        game.error = "teardown started"
                    else:
                        setattr(game, gate, True)
                    game.condition.notify_all()
                start_gate.set()
                thread.join(2)
                self.assertFalse(thread.is_alive())
                self.assertEqual(len(result), 1)
                self.assertIsInstance(result[0], APIProblem)
                self.assertEqual(game.agents, {})
                self.assertEqual(game.place_agents, {})
                self.assertFalse(game.sidecar_ready_generations)
                self.assertFalse(game.start_sent)
                self.sidecar_factory.start_gate = None

    def test_v2_status_loss_fails_established_seat_with_startup_grace(self):
        created = self.create(
            mode="multiplayer", places=2, control_protocol="full-control-v2",
        )
        game = self.supervisor.game(created["game_id"])
        place = game.joinable_places[0]
        sidecar = game._make_sidecar(place, 1)
        sidecar.state = "ready"
        with game.condition:
            game.sidecars[place.number] = sidecar
            game.sidecar_generations[place.number] = 1
            game.sidecar_ready_generations[place.number] = 1
            game.state = "starting"
            game.start_sent = True
            game.sidecar_start_deadline = time.monotonic() + 1

        self.sidecar_factory.status_response = (
            "STATUS\tstate=preparing\tserver=1\tseat=ready"
        )
        self.assertTrue(game._poll_v2_sidecars_once())
        self.assertEqual(game.state, "starting")
        self.assertEqual(game.sidecar_ready_generations[place.number], 1)

        self.sidecar_factory.status_response = (
            "STATUS\tstate=preparing\tserver=0\tseat=idle"
        )
        self.assertFalse(game._poll_v2_sidecars_once())
        self.assertEqual(game.state, "failed")
        self.assertNotIn(place.number, game.sidecar_ready_generations)
        self.assertIn("sidecar_exited", game.invalid_reasons)
        self.assertGreaterEqual(sidecar.stop_count, 1)

        created = self.create(
            mode="multiplayer", places=2, control_protocol="full-control-v2",
        )
        game = self.supervisor.game(created["game_id"])
        place = game.joinable_places[0]
        sidecar = game._make_sidecar(place, 1)
        sidecar.state = "ready"
        with game.condition:
            game.sidecars[place.number] = sidecar
            game.sidecar_generations[place.number] = 1
            game.sidecar_ready_generations[place.number] = 1
            game.state = "starting"
            game.start_sent = True
            game.sidecar_start_deadline = time.monotonic() - 1
        self.sidecar_factory.status_response = (
            "STATUS\tstate=preparing\tserver=1\tseat=ready"
        )
        self.assertFalse(game._poll_v2_sidecars_once())
        self.assertEqual(game.state, "failed")
        self.assertNotIn(place.number, game.sidecar_ready_generations)

    def test_v2_status_poll_skips_action_callback_barrier(self):
        created = self.create(
            mode="multiplayer", places=2,
            control_protocol="full-control-v2",
        )
        game = self.supervisor.game(created["game_id"])
        place = game.joinable_places[0]
        sidecar = game._make_sidecar(place, 1)
        sidecar.state = "ready"
        with game.condition:
            game.sidecars[place.number] = sidecar
            game.sidecar_generations[place.number] = 1
            game.sidecar_ready_generations[place.number] = 1
            game.state = "running"
            game.start_sent = True

        self.sidecar_factory.status_error = SidecarError(
            "command_in_progress",
        )
        self.assertTrue(game._poll_v2_sidecars_once())

        self.assertEqual(game.state, "running")
        self.assertEqual(game.sidecar_ready_generations[place.number], 1)
        self.assertNotIn("sidecar_exited", game.invalid_reasons)
        self.assertEqual(sidecar.stop_count, 0)

    def test_v2_normal_server_exit_ignores_sidecar_disconnect_during_drain(self):
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        game.join(
            created["join_token"], controller_label="codex-clean-exit",
            supported_control_protocols=["full-control-v2"],
        )
        current = self.sidecar_factory.created[-1]

        class FinishedProcess:
            stdin = None

            @staticmethod
            def wait():
                return 0

            @staticmethod
            def poll():
                return 0

        class DisconnectDuringDrain:
            @staticmethod
            def join(timeout=None):
                current.die()

        game.process = FinishedProcess()
        game.output_thread = DisconnectDuringDrain()
        (game.episode / "score.log").write_text("score", encoding="utf-8")
        with patch.object(
            game, "_configured_score_snapshot", return_value={}
        ), patch("agent_eval.supervisor.summarize_episode", return_value={}):
            game._monitor()
        self.assertTrue(game.server_exit_observed)
        self.assertEqual(game.state, "completed")
        self.assertNotIn("sidecar_exited", game.invalid_reasons)

    def test_v2_disconnect_before_server_exit_gets_bounded_completion_grace(self):
        self.sidecar_factory.observation_rows = native_v2_rows(
            tile_count=3, action_count=6,
        )

        class DeferredProcess:
            stdin = None

            def __init__(self):
                self.returncode = None
                self.exited = threading.Event()

            def poll(self):
                return self.returncode

            def wait(self, timeout=None):
                if not self.exited.wait(timeout):
                    raise subprocess.TimeoutExpired("deferred-server", timeout)
                return self.returncode

            def finish(self, returncode):
                self.returncode = returncode
                self.exited.set()

            def terminate(self):
                self.finish(-15)

            def kill(self):
                self.finish(-9)

        def wait_until(predicate, timeout=1):
            deadline = time.monotonic() + timeout
            while not predicate():
                self.assertLess(time.monotonic(), deadline)
                time.sleep(0.005)

        def disconnect_with_cached_running(sidecar):
            sidecar.state = "failed"
            sidecar.error_code = "process_exited"
            health = sidecar.public_health()
            health.update({
                "client_state": "running",
                "server_connected": True,
                "seat_state": "ready",
            })
            sidecar.callback(sidecar.generation, health)

        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-pre-exit",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        state = game.v2_get_page(
            joined["agent_id"], "state", "section=known_tiles&limit=1",
        )
        legal = game.v2_get_page(
            joined["agent_id"], "legal_actions", "limit=1",
        )
        state_cursor = state["page"]["next_cursor"]
        legal_cursor = legal["page"]["next_cursor"]
        self.assertIsNotNone(state_cursor)
        self.assertIsNotNone(legal_cursor)
        current = self.sidecar_factory.created[-1]
        process = DeferredProcess()
        game.process = process
        with patch(
            "agent_eval.supervisor.V2_SIDECAR_COMPLETION_GRACE_S", 0.2,
        ):
            disconnect_with_cached_running(current)
            wait_until(lambda: game.sidecar_exit_grace_generations.get(1) == 1)
            self.assertNotEqual(game.state, "failed")
            health = game.v2_health(joined["agent_id"])
            self.assertEqual(health["sidecar"]["state"], "failed")
            self.assertFalse(health["observation_available"])
            self.assertFalse(health["legal_actions_available"])
            for endpoint, query in (
                ("state", ""),
                ("state", f"cursor={state_cursor}"),
                ("legal_actions", ""),
                ("legal_actions", f"cursor={legal_cursor}"),
            ):
                with self.subTest(endpoint=endpoint, query=query):
                    with self.assertRaises(APIProblem) as unavailable:
                        game.v2_get_page(joined["agent_id"], endpoint, query)
                    self.assertEqual(
                        unavailable.exception.status,
                        HTTPStatus.SERVICE_UNAVAILABLE,
                    )
                    self.assertEqual(
                        unavailable.exception.payload["error"]["code"],
                        "sidecar_unavailable",
                    )
                    encoded = json.dumps(unavailable.exception.payload)
                    self.assertNotIn("process_exited", encoded)
            process.finish(0)
            wait_until(lambda: 1 not in game.sidecar_exit_grace_generations)
        (game.episode / "score.log").write_text("score", encoding="utf-8")
        with patch.object(
            game, "_configured_score_snapshot", return_value={}
        ), patch("agent_eval.supervisor.summarize_episode", return_value={}):
            game._monitor()
        self.assertEqual(game.state, "completed")
        self.assertNotIn("sidecar_exited", game.invalid_reasons)

        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        game.join(
            created["join_token"], controller_label="codex-real-loss",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        game.start_sent = True
        current = self.sidecar_factory.created[-1]
        process = DeferredProcess()
        game.process = process
        with patch(
            "agent_eval.supervisor.V2_SIDECAR_COMPLETION_GRACE_S", 0.03,
        ):
            disconnect_with_cached_running(current)
            # The grace still bounds how long the loss stays undecided; what
            # follows it is now a recovery rather than a failed game.
            wait_until(lambda: game.sidecar_generations.get(1) == 2)
            wait_until(lambda: current.stop_count >= 1)
        self.assertEqual(game.state, "running")
        self.assertNotIn("sidecar_exited", game.invalid_reasons)
        self.assertEqual(game.sidecar_ready_generations.get(1), 2)
        self.assertGreaterEqual(current.stop_count, 1)

    def test_v2_health_routes_are_scoped_sanitized_and_truthful(self):
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-health-model",
            supported_control_protocols=["full-control-v2"],
        )
        server = make_supervisor_server(self.supervisor, "127.0.0.1", 0)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        host, port = server.server_address
        base = f"http://{host}:{port}"
        try:
            health = request_json(
                "GET", f"{base}/v2/games/{game.game_id}/me/health",
                token=joined["agent_token"],
            )
            self.assertEqual(health["schema_version"], 2)
            self.assertEqual(health["agent"]["agent_id"], joined["agent_id"])
            self.assertEqual(health["seat"]["seat_id"], "place-1")
            self.assertEqual(
                health["observation_available"],
                health["game_state"] == "running",
            )
            self.assertEqual(
                health["legal_actions_available"],
                health["game_state"] == "running",
            )
            encoded = json.dumps(health)
            for secret_field in ("argv", "environment", "log_path", "do-not-expose"):
                self.assertNotIn(secret_field, encoded)
            self.assertEqual(health["sidecar"]["client_state"], "running")
            self.assertTrue(health["sidecar"]["server_connected"])
            self.assertEqual(health["sidecar"]["seat_state"], "ready")
            with self.assertRaises(ClientError) as wrong_caller:
                request_json(
                    "GET", f"{base}/v2/games/{game.game_id}/me/health",
                    token="another-agent-token",
                )
            self.assertEqual(wrong_caller.exception.status, HTTPStatus.FORBIDDEN)

            self._mark_v2_running(game)
            bootstrap_health = request_json(
                "GET", f"{base}/v2/games/{game.game_id}/me/health",
                token=joined["agent_token"],
            )
            self.assertTrue(bootstrap_health["observation_available"])
            self.assertTrue(bootstrap_health["legal_actions_available"])
            self.assertFalse(game.v2_controls[joined["place"]].has_snapshot)
            state = request_json(
                "GET", f"{base}/v2/games/{game.game_id}/me/state",
                token=joined["agent_token"],
            )
            self.assertEqual(state["agent_id"], joined["agent_id"])
            self.assertEqual(state["game_id"], game.game_id)
            self.assertEqual(state["page"]["section"], "overview")
            legal = request_json(
                "GET", f"{base}/v2/games/{game.game_id}/me/legal-actions",
                token=joined["agent_token"],
            )
            self.assertEqual(legal["agent_id"], joined["agent_id"])
            self.assertEqual(legal["page"]["section"], "legal_actions")
            health = request_json(
                "GET", f"{base}/v2/games/{game.game_id}/me/health",
                token=joined["agent_token"],
            )
            self.assertTrue(health["observation_available"])
            self.assertTrue(health["legal_actions_available"])

            for method, suffix, body, expected in (
                ("POST", "batches", {}, HTTPStatus.BAD_REQUEST),
                ("GET", "receipts/batch_test", None, HTTPStatus.NOT_FOUND),
            ):
                with self.subTest(suffix=suffix), self.assertRaises(ClientError) as error:
                    request_json(
                        method,
                        f"{base}/v2/games/{game.game_id}/me/{suffix}",
                        token=joined["agent_token"], body=body,
                    )
                self.assertEqual(error.exception.status, expected)

            with self.assertRaises(ClientError) as internal:
                request_json(
                    "POST", f"{base}/internal/v1/games/{game.game_id}/turns",
                    token="not-relevant", body={},
                )
            self.assertEqual(internal.exception.status, HTTPStatus.CONFLICT)
            with self.assertRaises(ClientError) as viewer:
                request_json(
                    "POST", f"{base}/v1/games/{game.game_id}/native-viewer",
                    token=created["owner_token"], body={},
                )
            self.assertEqual(viewer.exception.status, HTTPStatus.CONFLICT)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)

    def test_v2_public_pages_wait_for_running_even_with_cached_cursors(self):
        self.sidecar_factory.observation_rows = native_v2_rows(
            tile_count=3, action_count=6,
        )
        # Do not let the fake status thread race the deliberately forced
        # pre-running states below.
        with patch.object(Game, "_poll_v2_sidecars", autospec=True):
            created = self.create(control_protocol="full-control-v2")
            game = self.supervisor.game(created["game_id"])
            joined = game.join(
                created["join_token"],
                controller_label="codex-prerunning-gate",
                supported_control_protocols=["full-control-v2"],
            )

        self.assertEqual(game.state, "lobby")
        self._mark_v2_running(game)
        state = game.v2_get_page(
            joined["agent_id"], "state", "section=known_tiles&limit=1",
        )
        legal = game.v2_get_page(
            joined["agent_id"], "legal_actions", "limit=1",
        )
        state_cursor = state["page"]["next_cursor"]
        legal_cursor = legal["page"]["next_cursor"]
        self.assertIsNotNone(state_cursor)
        self.assertIsNotNone(legal_cursor)
        reads = self.sidecar_factory.created[-1].read_count

        for pre_running_state in ("lobby",):
            with game.condition:
                game.state = pre_running_state
                game.condition.notify_all()
            health = game.v2_health(joined["agent_id"])
            self.assertFalse(health["observation_available"])
            self.assertFalse(health["legal_actions_available"])
            for endpoint, query in (
                ("state", ""),
                ("state", f"cursor={state_cursor}"),
                ("legal_actions", ""),
                ("legal_actions", f"cursor={legal_cursor}"),
            ):
                with self.subTest(
                    game_state=pre_running_state,
                    endpoint=endpoint,
                    query=query,
                ), self.assertRaises(APIProblem) as unavailable:
                    game.v2_get_page(joined["agent_id"], endpoint, query)
                self.assertEqual(
                    unavailable.exception.status,
                    HTTPStatus.SERVICE_UNAVAILABLE,
                )
                self.assertEqual(
                    unavailable.exception.payload["error"]["code"],
                    "sidecar_unavailable",
                )
                self.assertNotIn(
                    pre_running_state,
                    json.dumps(unavailable.exception.payload),
                )
            self.assertEqual(
                self.sidecar_factory.created[-1].read_count, reads,
            )

        self._mark_v2_running(game)
        health = game.v2_health(joined["agent_id"])
        self.assertTrue(health["observation_available"])
        self.assertTrue(health["legal_actions_available"])
        self.assertEqual(
            game.v2_get_page(
                joined["agent_id"], "state", f"cursor={state_cursor}",
            )["page"]["section"],
            "known_tiles",
        )
        self.assertEqual(
            game.v2_get_page(
                joined["agent_id"], "legal_actions",
                f"cursor={legal_cursor}",
            )["page"]["section"],
            "legal_actions",
        )
        self.assertEqual(
            game.v2_get_page(joined["agent_id"], "state", "")["page"][
                "section"
            ],
            "overview",
        )

    def test_v2_get_routes_page_every_section_and_enforce_query_scope(self):
        self.sidecar_factory.observation_rows = native_v2_rows(
            tile_count=20, action_count=20,
        )
        created = self.create(
            mode="multiplayer", places=2, control_protocol="full-control-v2",
        )
        game = self.supervisor.game(created["game_id"])
        first = game.join(
            created["join_token"], 1, "codex-route-one",
            supported_control_protocols=["full-control-v2"],
        )
        second = game.join(
            created["join_token"], 2, "claude-route-two",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        server = make_supervisor_server(self.supervisor, "127.0.0.1", 0)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        host, port = server.server_address
        root = f"http://{host}:{port}/v2/games/{game.game_id}/me"
        try:
            status, default = raw_json_request(
                f"{root}/state", first["agent_token"],
            )
            self.assertEqual(status, HTTPStatus.OK)
            self.assertEqual(set(default), {
                "schema_version", "control_protocol", "game_id", "agent_id",
                "state_revision", "page",
            })
            self.assertEqual(default["agent_id"], first["agent_id"])
            self.assertEqual(default["page"]["section"], "overview")
            overview = default["page"]["items"][0]
            self.assertEqual(overview["phase_mode"], "players_alternate")
            self.assertEqual(overview["phase_count"], 2)
            self.assertTrue(overview["active_phase"])
            self.assertTrue(overview["phase_ready"])
            self.assertTrue(overview["player"]["alive"])
            self.assertEqual(overview["map"], {
                "width": 16, "height": 16, "topology": "square",
                "wrap_x": True, "wrap_y": False,
            })

            for section in (
                "overview", "research", "diplomacy", "known_tiles",
                "cities", "units", "tombstones",
            ):
                with self.subTest(section=section):
                    status, page = raw_json_request(
                        f"{root}/state?section={section}&limit=16",
                        first["agent_token"],
                    )
                    self.assertEqual(status, HTTPStatus.OK)
                    self.assertEqual(page["page"]["section"], section)
                    self.assertLessEqual(len(page["page"]["items"]), 16)

            _, known = raw_json_request(
                f"{root}/state?section=known_tiles&limit=16",
                first["agent_token"],
            )
            center_id = next(
                item["id"] for item in known["page"]["items"]
                if item["visibility"] != "unknown"
            )
            status, window = raw_json_request(
                f"{root}/state?section=tile_window&center_id={center_id}"
                "&radius=2&limit=2",
                first["agent_token"],
            )
            self.assertEqual(status, HTTPStatus.OK)
            self.assertEqual(window["page"]["section"], "tile_window")
            self.assertTrue(all("distance" in item for item in window["page"]["items"]))

            status, tiles = raw_json_request(
                f"{root}/state?section=known_tiles&limit=3",
                first["agent_token"],
            )
            self.assertEqual(status, HTTPStatus.OK)
            cursor = tiles["page"]["next_cursor"]
            reads = self.sidecar_factory.created[0].read_count
            status, continued = raw_json_request(
                f"{root}/state?cursor={cursor}", first["agent_token"],
            )
            self.assertEqual(status, HTTPStatus.OK)
            self.assertEqual(len(continued["page"]["items"]), 3)
            self.assertEqual(self.sidecar_factory.created[0].read_count, reads)
            status, used = raw_json_request(
                f"{root}/state?cursor={cursor}", first["agent_token"],
            )
            self.assertEqual(status, HTTPStatus.OK)
            self.assertEqual(used, continued)

            _, cross_source = raw_json_request(
                f"{root}/state?section=known_tiles&limit=1",
                first["agent_token"],
            )
            cross_cursor = cross_source["page"]["next_cursor"]
            for token, suffix in (
                (first["agent_token"], f"legal-actions?cursor={cross_cursor}"),
                (second["agent_token"], f"state?cursor={cross_cursor}"),
            ):
                status, error = raw_json_request(f"{root}/{suffix}", token)
                self.assertEqual(status, HTTPStatus.BAD_REQUEST)
                self.assertEqual(error["error"]["code"], "invalid_request")

            status, legal = raw_json_request(
                f"{root}/legal-actions?limit=16", first["agent_token"],
            )
            self.assertEqual(status, HTTPStatus.OK)
            self.assertEqual(legal["page"]["section"], "legal_actions")
            self.assertEqual(len(legal["page"]["items"]), 16)
            self.assertIsNotNone(legal["page"]["next_cursor"])

            player_id = overview["player"]["id"]
            status, actor_legal = raw_json_request(
                f"{root}/legal-actions?actor_id={player_id}&limit=1",
                first["agent_token"],
            )
            self.assertEqual(status, HTTPStatus.OK)
            self.assertEqual(actor_legal["page"]["scope"], {
                "actor_id": player_id,
                "actor_type": "player",
            })
            actor_cursor = actor_legal["page"]["next_cursor"]

            status, second_state = raw_json_request(
                f"{root}/state", second["agent_token"],
            )
            self.assertEqual(status, HTTPStatus.OK)
            second_player_id = second_state["page"]["items"][0]["player"][
                "id"
            ]
            status, foreign_actor = raw_json_request(
                f"{root}/legal-actions?actor_id={second_player_id}",
                first["agent_token"],
            )
            self.assertEqual(status, HTTPStatus.BAD_REQUEST)
            self.assertEqual(
                foreign_actor["error"]["code"], "invalid_request",
            )
            status, foreign_cursor = raw_json_request(
                f"{root}/legal-actions?cursor={actor_cursor}",
                second["agent_token"],
            )
            self.assertEqual(status, HTTPStatus.BAD_REQUEST)
            self.assertEqual(
                foreign_cursor["error"]["code"], "invalid_request",
            )

            invalid_queries = (
                "section=", "section=overview&section=units", "unknown=1",
                "limit=", "limit=0", "limit=17", "limit=01", "limit=%2B1",
                "limit=%201", "cursor=", "cursor=x&limit=1", "li%6Dit=1",
                "section=%6fverview", "limit=1&", "limit=1&&section=overview",
                "section=city_detail", "section=cities&actor_id=city_"
                + "0" * 32,
                "section=city_detail&actor_id=player_" + "0" * 32,
                "section=tile_window", "section=tile_window&center_id="
                + center_id, "section=tile_window&center_id=" + center_id
                + "&radius=9", "section=overview&center_id=" + center_id,
                "section=tile_window&center_id=" + center_id
                + "&radius=1&cursor=" + "x" * 39,
            )
            for query in invalid_queries:
                with self.subTest(query=query):
                    status, error = raw_json_request(
                        f"{root}/state?{query}", first["agent_token"],
                    )
                    self.assertEqual(status, HTTPStatus.BAD_REQUEST)
                    self.assertEqual(error["error"]["code"], "invalid_request")
                    self.assertEqual(error["error"]["details"], {})
            status, error = raw_json_request(
                f"{root}/legal-actions?section=overview", first["agent_token"],
            )
            self.assertEqual(status, HTTPStatus.BAD_REQUEST)
            self.assertEqual(error["error"]["code"], "invalid_request")

            # Authentication precedes even deliberately malformed query parsing.
            status, missing = raw_json_request(f"{root}/state?limit=01")
            self.assertEqual(status, HTTPStatus.UNAUTHORIZED)
            self.assertEqual(missing["error"]["code"], "invalid_request")
            status, wrong = raw_json_request(
                f"{root}/state?limit=01", "wrong-agent-token",
            )
            self.assertEqual(status, HTTPStatus.FORBIDDEN)
            self.assertEqual(wrong["error"]["code"], "invalid_request")

            legacy = self.create()
            status, unsupported = raw_json_request(
                f"http://{host}:{port}/v2/games/{legacy['game_id']}/me/"
                "state?limit=01",
            )
            self.assertEqual(status, HTTPStatus.CONFLICT)
            self.assertEqual(
                unsupported["error"]["code"], "unsupported_protocol",
            )

            public = json.dumps(
                [default, tiles, legal, actor_legal], sort_keys=True,
            )
            for native_secret in (
                "a0000000000000001", "p:1:10", "native_rule",
                "target_tile", "snapshot", "row_count",
            ):
                self.assertNotIn(native_secret, public)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)

    def test_v2_get_routes_map_sidecar_and_projection_errors_without_details(self):
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-error-model",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        server = make_supervisor_server(self.supervisor, "127.0.0.1", 0)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        host, port = server.server_address
        url = f"http://{host}:{port}/v2/games/{game.game_id}/me/state"
        try:
            cases = (
                ("native_busy", HTTPStatus.TOO_MANY_REQUESTS, "rate_limited"),
                ("sidecar_unavailable", HTTPStatus.SERVICE_UNAVAILABLE, "sidecar_unavailable"),
                ("native_not_ready", HTTPStatus.SERVICE_UNAVAILABLE, "sidecar_unavailable"),
                ("deadline_exceeded", HTTPStatus.SERVICE_UNAVAILABLE, "sidecar_unavailable"),
                ("snapshot_gone", HTTPStatus.SERVICE_UNAVAILABLE, "sidecar_unavailable"),
                ("observation_too_large", HTTPStatus.SERVICE_UNAVAILABLE, "sidecar_unavailable"),
                ("protocol_error", HTTPStatus.BAD_GATEWAY, "internal_error"),
                ("native_error", HTTPStatus.INTERNAL_SERVER_ERROR, "internal_error"),
            )
            for code, expected_status, expected_code in cases:
                with self.subTest(code=code):
                    self.sidecar_factory.observation_error = SidecarError(
                        code, "SENSITIVE_NATIVE_DETAIL_/private/path",
                    )
                    status, error = raw_json_request(url, joined["agent_token"])
                    self.assertEqual(status, expected_status)
                    self.assertEqual(error["error"]["code"], expected_code)
                    encoded = json.dumps(error)
                    self.assertNotIn("SENSITIVE", encoded)
                    self.assertNotIn("/private/path", encoded)

            self.sidecar_factory.observation_error = None
            self.sidecar_factory.observation_rows = native_v2_rows(malformed=True)
            status, malformed = raw_json_request(url, joined["agent_token"])
            self.assertEqual(status, HTTPStatus.INTERNAL_SERVER_ERROR)
            self.assertEqual(malformed["error"]["code"], "internal_error")
            self.assertNotIn("must-not-escape", json.dumps(malformed))
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)

    def test_v2_actor_scoped_legal_actions_page_and_execute_under_seat_lock(self):
        rows = list(native_v2_rows(tile_count=2, action_count=10))
        rows.append(
            "city ref=c:20:200 name=Alpha tile=0 x=0 y=0 size=2 "
            "food=3 shields=2 trade=1 production_kind=unit "
            "production_id=2 production_name=Warriors shield_stock=4 "
            "shield_cost=10 buy_cost=12 can_buy=1 can_change=1 "
            "citizen_tile_count=1 specialist_type_count=1 "
            "worklist_length=0 build_choice_count=2 improvement_count=0 "
            "trade_route_count=0 trade_route_capacity=3 "
            "did_sell=0 allow_disband=0 new_citizens=default "
            "options_conflict=0 airlift_remaining=1 airlift_max=1 "
            "governor_enabled=0 citizen_happy=0 citizen_content=0 "
            "citizen_unhappy=0 citizen_angry=0 citizen_workers=0 "
            "citizen_specialists=2 food_stock=5 granary_size=20 "
            "growth_turns=5 pollution=0 food_citizen_base=2 food_net=5 "
            "food_surplus=3 food_usage=2 food_waste=0 "
            "food_unhappy_penalty=0 shield_citizen_base=1 shield_net=2 "
            "shield_surplus=2 shield_usage=0 shield_waste=0 "
            "shield_unhappy_penalty=0 trade_citizen_base=0 trade_net=1 "
            "trade_surplus=1 trade_usage=0 trade_waste=0 "
            "trade_unhappy_penalty=0 gold_citizen_base=0 gold_net=0 "
            "gold_surplus=0 gold_usage=0 gold_waste=0 "
            "gold_unhappy_penalty=0 luxury_citizen_base=0 luxury_net=0 "
            "luxury_surplus=0 luxury_usage=0 luxury_waste=0 "
            "luxury_unhappy_penalty=0 science_citizen_base=2 "
            "science_net=0 science_surplus=0 science_usage=0 "
            "science_waste=0 science_unhappy_penalty=0"
        )
        rows.append(
            "city_site ref=c:20:200 owner=p:1:10 name=Alpha tile=0 "
            "x=0 y=0 size=2 visibility=own"
        )
        rows.append(
            "city_tile city=c:20:200 tile=0 worked=1 free_worked=1 "
            "can_work=1 food=2 shields=1 trade=0 gold=0 luxury=0 science=0"
        )
        rows.extend((
            native_v2_city_build_choice_row(
                production_kind="improvement", production_id=5,
                name="Granary",
            ),
            native_v2_city_build_choice_row(
                production_kind="unit", production_id=2, name="Warriors",
            ),
        ))
        rows.append(
            "city_specialist city=c:20:200 specialist=0 name=Entertainer "
            "count=2 counts_toward_population=1 can_use=1 is_default=1 "
            "food=0 shields=0 trade=0 gold=0 luxury=0 science=1"
        )
        rows.append(
            "city_rally city=c:20:200 active=0 persistent=0 vigilant=0 "
            "order_count=0 orders_digest=fnv1a64-0000000000000000"
        )
        self.sidecar_factory.observation_rows = tuple(sorted(rows))
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-scoped-actions",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        self._seed_v2_phase(game)

        overview = game.v2_get_page(
            joined["agent_id"], "state", "",
        )["page"]["items"][0]
        player_id = overview["player"]["id"]
        unit_id = game.v2_get_page(
            joined["agent_id"], "state", "section=units",
        )["page"]["items"][0]["id"]
        city_id = game.v2_get_page(
            joined["agent_id"], "state", "section=cities",
        )["page"]["items"][0]["id"]
        for section in (
            "city_detail", "city_citizens", "city_build_choices",
            "city_worklist", "city_improvements", "city_trade_routes",
            "city_governor",
            "city_worker_tasks",
        ):
            with self.subTest(state_section=section):
                state_page = game.v2_get_page(
                    joined["agent_id"], "state",
                    f"section={section}&actor_id={city_id}&limit=1",
                )
                self.assertEqual(state_page["page"]["section"], section)
                self.assertLessEqual(len(state_page["page"]["items"]), 1)

        unscoped = game.v2_get_page(
            joined["agent_id"], "legal_actions", "limit=1",
        )
        self.assertNotIn("scope", unscoped["page"])
        unscoped_cursor = unscoped["page"]["next_cursor"]
        execution_lock = game.v2_execution_locks[joined["place"]][2]
        self.assertTrue(execution_lock.acquire(timeout=.1))
        try:
            with patch(
                "agent_eval.supervisor.V2_EXECUTION_LOCK_TIMEOUT_S", .01,
            ):
                unscoped_continued = game.v2_get_page(
                    joined["agent_id"], "legal_actions",
                    f"cursor={unscoped_cursor}",
                )
        finally:
            execution_lock.release()
        self.assertEqual(len(unscoped_continued["page"]["items"]), 1)
        self.assertNotIn("scope", unscoped_continued["page"])
        scoped = game.v2_get_page(
            joined["agent_id"], "legal_actions",
            f"actor_id={player_id}&limit=2",
        )
        self.assertEqual(scoped["page"]["scope"], {
            "actor_id": player_id,
            "actor_type": "player",
        })
        self.assertEqual(scoped["page"]["total_items"], 9)
        self.assertEqual(len(scoped["page"]["items"]), 2)
        self.assertEqual(self.sidecar_factory.scope_count, 1)
        cursor = scoped["page"]["next_cursor"]
        continued = game.v2_get_page(
            joined["agent_id"], "legal_actions", f"cursor={cursor}",
        )
        self.assertEqual(len(continued["page"]["items"]), 2)
        self.assertEqual(self.sidecar_factory.scope_page_count, 0)
        remaining = continued
        while remaining["page"]["next_cursor"] is not None:
            remaining = game.v2_get_page(
                joined["agent_id"], "legal_actions",
                f"cursor={remaining['page']['next_cursor']}",
            )
        self.assertTrue(remaining["page"]["catalog_complete"])

        unit_scope = game.v2_get_page(
            joined["agent_id"], "legal_actions",
            f"actor_id={unit_id}&limit=16",
        )
        self.assertEqual(unit_scope["page"]["scope"]["actor_type"], "unit")
        self.assertEqual(unit_scope["page"]["total_items"], 6)
        self.assertEqual(
            {item["subject"]["operation"] for item in unit_scope["page"]["items"]}
            - {"move"},
            {"start_activity"},
        )
        city_scope = game.v2_get_page(
            joined["agent_id"], "legal_actions",
            f"actor_id={city_id}",
        )
        self.assertEqual(city_scope["page"]["total_items"], 6)
        self.assertEqual(
            {item["subject"]["operation"] for item in city_scope["page"]["items"]},
            {
                "set_production", "buy_production", "set_worklist",
                "set_options", "rename", "set_governor",
            },
        )
        serialized_scopes = json.dumps([unit_scope, city_scope], sort_keys=True)
        for private in (
            "c:20:200", "u:10:100", "target_build", "target_extra",
            "native_rule", "a0000000000000065", "a0000000000000068",
        ):
            self.assertNotIn(private, serialized_scopes)

        expiring = game.v2_get_page(
            joined["agent_id"], "legal_actions",
            f"actor_id={player_id}&limit=1",
        )
        expiring_cursor = expiring["page"]["next_cursor"]
        sidecar = self.sidecar_factory.created[-1]
        with patch.object(
            sidecar, "read_actor_scope_page",
            side_effect=SidecarError(
                "scope_gone", "SENSITIVE native pinned view detail",
            ),
        ):
            frozen = game.v2_get_page(
                joined["agent_id"], "legal_actions",
                f"cursor={expiring_cursor}",
            )
        self.assertEqual(frozen["state_revision"], expiring["state_revision"])
        self.assertNotIn("SENSITIVE", json.dumps(frozen))

        for endpoint, query in (
            ("state", f"actor_id={player_id}"),
            ("legal_actions", f"cursor={cursor}&actor_id={player_id}"),
            ("legal_actions", "actor_id=unit_" + "0" * 32),
        ):
            with self.subTest(endpoint=endpoint, query=query), self.assertRaises(
                APIProblem,
            ) as invalid:
                game.v2_get_page(joined["agent_id"], endpoint, query)
            self.assertEqual(invalid.exception.status, HTTPStatus.BAD_REQUEST)
            self.assertEqual(
                invalid.exception.payload["error"]["code"], "invalid_request",
            )

        action = scoped["page"]["items"][0]
        self.sidecar_factory.action_error = SidecarActionNotAccepted(
            "stale_revision", "SENSITIVE native revision detail",
        )
        stale_batch = self.v2_batch(
            game, joined, action, batch_id="batch_scoped_stale",
        )
        stale_status, stale_receipt = game.v2_submit_batch(
            joined["agent_id"], stale_batch,
        )
        self.assertEqual(stale_status, HTTPStatus.CONFLICT)
        self.assertEqual(stale_receipt["receipt_state"], "rejected")
        self.assertEqual(
            stale_receipt["error"]["error"]["code"], "stale_revision",
        )
        self.assertTrue(stale_receipt["error"]["error"]["retryable"])
        duplicate_status, duplicate = game.v2_submit_batch(
            joined["agent_id"], stale_batch,
        )
        self.assertEqual(duplicate_status, HTTPStatus.CONFLICT)
        self.assertTrue(duplicate["idempotent"])
        self.assertEqual(duplicate["receipt_state"], "rejected")
        self.assertNotIn("SENSITIVE", json.dumps(stale_receipt))
        self.sidecar_factory.action_error = None
        status, receipt = game.v2_submit_batch(
            joined["agent_id"],
            self.v2_batch(
                game, joined, action, batch_id="batch_scoped_player",
            ),
        )
        self.assertEqual(status, HTTPStatus.OK)
        self.assertEqual(receipt["receipt_state"], "applied")
        self.assertEqual(self.sidecar_factory.scoped_action_count, 2)
        self.assertEqual(self.sidecar_factory.last_scoped_actor, "p:1:10")

        server = make_supervisor_server(self.supervisor, "127.0.0.1", 0)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        host, port = server.server_address
        root = f"http://{host}:{port}/v2/games/{game.game_id}/me"
        try:
            for actor_id, actor_ref, operation, batch_id in (
                (player_id, "p:1:10", "change", "batch_scoped_government"),
                (city_id, "c:20:200", "set_production", "batch_scoped_city"),
                (unit_id, "u:10:100", "start_activity", "batch_scoped_unit"),
            ):
                refreshed = request_json(
                    "GET",
                    f"{root}/legal-actions?actor_id={actor_id}&limit=16",
                    token=joined["agent_token"],
                )
                scoped_action = next(
                    item for item in refreshed["page"]["items"]
                    if item["subject"]["operation"] == operation
                )
                action_receipt = request_json(
                    "POST", f"{root}/batches",
                    token=joined["agent_token"],
                    body=self.v2_batch(
                        game, joined, scoped_action, batch_id=batch_id,
                    ),
                )
                self.assertEqual(action_receipt["receipt_state"], "applied")
                self.assertEqual(
                    self.sidecar_factory.last_scoped_actor, actor_ref,
                )
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)
        target_id = next(
            item["id"] for item in game.v2_get_page(
                joined["agent_id"], "state", "section=known_tiles",
            )["page"]["items"] if item["x"] == 1
        )
        rally_page = game.v2_get_page(
            joined["agent_id"], "legal_actions",
            f"actor_id={city_id}&target_id={target_id}",
        )
        self.assertEqual(rally_page["page"]["total_items"], 1)
        rally_action = rally_page["page"]["items"][0]
        self.assertEqual(rally_action["kind"], "city.set_rally")
        self.assertEqual(
            rally_action["subject"]["operation"], "set_rally",
        )
        status, rally_receipt = game.v2_submit_batch(
            joined["agent_id"],
            self.v2_batch(
                game, joined, rally_action,
                batch_id="batch_target_rally",
                arguments={"persistent": True},
            ),
        )
        self.assertEqual(status, HTTPStatus.OK)
        self.assertEqual(rally_receipt["receipt_state"], "applied")
        self.assertEqual(self.sidecar_factory.last_scoped_actor, "c:20:200")
        self.assertEqual(self.sidecar_factory.scoped_action_count, 6)

    def test_v2_capacity_refusal_states_a_remedy_and_spares_phase_end(self):
        self.sidecar_factory.observation_rows = native_v2_rows(
            tile_count=4, action_count=10,
        )
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-capacity",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        agent_id = joined["agent_id"]
        query = "section=known_tiles&limit=1"
        with patch.multiple(
            v2_control,
            MAX_ACTIVE_CURSOR_CHAINS=2,
            RESERVED_CATALOG_CHAINS=1,
        ):
            game.v2_get_page(agent_id, "state", query)
            with self.assertRaises(APIProblem) as refused:
                game.v2_get_page(agent_id, "state", query)
            # Ordinary reads are refused, but the enumeration that carries
            # phase.end draws on capacity they can never consume.
            catalog = game.v2_get_page(agent_id, "legal_actions", "limit=16")
        error = refused.exception.payload["error"]
        self.assertEqual(refused.exception.status, HTTPStatus.TOO_MANY_REQUESTS)
        self.assertEqual(error["code"], "rate_limited")
        self.assertTrue(error["retryable"])
        wait = error["details"]["retry_after_seconds"]
        self.assertIsInstance(wait, int)
        self.assertTrue(0 < wait <= v2_control.CURSOR_TTL_SECONDS)
        self.assertIn(f"retry in {wait}s", error["message"])
        self.assertTrue(error["details"]["retry_after"].endswith("Z"))
        self.assertIn(
            "phase.end", {item["kind"] for item in catalog["page"]["items"]},
        )

    def test_v2_exact_target_action_query_empty_errors_and_execution_lock(self):
        self.sidecar_factory.observation_rows = native_v2_rows(
            tile_count=2, action_count=10,
        )
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-target-action",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        units = game.v2_get_page(
            joined["agent_id"], "state", "section=units",
        )["page"]["items"]
        actor_id = next(item["id"] for item in units if item["scope"] == "own")
        tiles = game.v2_get_page(
            joined["agent_id"], "state", "section=known_tiles",
        )["page"]["items"]
        target_id = next(item["id"] for item in tiles if item["x"] == 1)
        query = f"actor_id={actor_id}&target_id={target_id}&limit=1"

        execution_lock = game.v2_execution_locks[joined["place"]][2]
        self.assertTrue(execution_lock.acquire(timeout=.1))
        try:
            with patch(
                "agent_eval.supervisor.V2_EXECUTION_LOCK_TIMEOUT_S", .01,
            ), self.assertRaises(APIProblem) as blocked:
                game.v2_get_page(
                    joined["agent_id"], "legal_actions", query,
                )
        finally:
            execution_lock.release()
        self.assertEqual(blocked.exception.status, HTTPStatus.TOO_MANY_REQUESTS)
        self.assertEqual(
            blocked.exception.payload["error"]["code"], "rate_limited",
        )
        self.assertTrue(blocked.exception.payload["error"]["retryable"])
        self.assertEqual(self.sidecar_factory.target_count, 0)

        target_page = game.v2_get_page(
            joined["agent_id"], "legal_actions", query,
        )
        self.assertEqual(target_page["page"]["total_items"], 1)
        self.assertIsNone(target_page["page"]["next_cursor"])
        self.assertEqual(target_page["page"]["scope"], {
            "actor_id": actor_id,
            "actor_type": "unit",
            "target_id": target_id,
            "target_type": "tile",
        })
        self.assertTrue(target_page["page"]["catalog_complete"])
        target_action = target_page["page"]["items"][0]
        self.assertEqual(target_action["subject"]["operation"], "goto")
        self.assertEqual(target_action["subject"]["actor"]["id"], actor_id)
        self.assertEqual(target_action["subject"]["target"]["id"], target_id)
        self.assertEqual(self.sidecar_factory.target_count, 1)
        self.assertEqual(self.sidecar_factory.last_target_actor, "u:10:100")
        self.assertEqual(self.sidecar_factory.last_target_tile, 1)

        status, receipt = game.v2_submit_batch(
            joined["agent_id"],
            self.v2_batch(
                game, joined, target_action, batch_id="batch_target_goto",
            ),
        )
        self.assertEqual(status, HTTPStatus.OK)
        self.assertEqual(receipt["receipt_state"], "applied")
        self.assertEqual(self.sidecar_factory.last_scoped_actor, "u:10:100")

        self.sidecar_factory.target_empty = True
        empty = game.v2_get_page(
            joined["agent_id"], "legal_actions", query,
        )
        self.assertEqual(empty["page"]["items"], [])
        self.assertEqual(empty["page"]["total_items"], 0)
        self.assertIsNone(empty["page"]["next_cursor"])
        self.sidecar_factory.target_empty = False

        self.sidecar_factory.target_error = SidecarError("stale_revision")
        with self.assertRaises(APIProblem) as stale:
            game.v2_get_page(joined["agent_id"], "legal_actions", query)
        self.assertEqual(stale.exception.status, HTTPStatus.CONFLICT)
        self.assertEqual(
            stale.exception.payload["error"]["code"], "stale_revision",
        )
        self.assertTrue(stale.exception.payload["error"]["retryable"])
        self.sidecar_factory.target_error = None

        other_created = self.create(control_protocol="full-control-v2")
        other_game = self.supervisor.game(other_created["game_id"])
        other_joined = other_game.join(
            other_created["join_token"], controller_label="other-seat",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(other_game)
        foreign_target = other_game.v2_get_page(
            other_joined["agent_id"], "state", "section=known_tiles",
        )["page"]["items"][1]["id"]
        before_invalid = self.sidecar_factory.target_count
        for invalid_query in (
            f"actor_id={actor_id}&target_id=tile_{'0' * 32}",
            f"actor_id={actor_id}&target_id={foreign_target}",
            f"actor_id={actor_id}&target_id=not-a-tile",
            f"actor_id={actor_id}&target_id={target_id}&limit=17",
            f"actor_id={actor_id}&target_id={target_id}&section=overview",
            f"actor_id={actor_id}&target_id={target_id}&cursor="
            f"cursor_{'A' * 32}",
        ):
            with self.subTest(query=invalid_query), self.assertRaises(
                APIProblem,
            ) as invalid:
                game.v2_get_page(
                    joined["agent_id"], "legal_actions", invalid_query,
                )
            self.assertEqual(invalid.exception.status, HTTPStatus.BAD_REQUEST)
            self.assertEqual(
                invalid.exception.payload["error"]["code"],
                "invalid_request",
            )
        self.assertEqual(self.sidecar_factory.target_count, before_invalid)

    def test_v2_infrastructure_target_hydrates_exact_tile_before_discovery(self):
        rows = [
            row for row in native_v2_rows(tile_count=1, action_count=0)
            if not row.startswith("tile ")
        ]
        rows = [
            row.replace(
                "infrastructure_enabled=0 infrastructure_points=0",
                "infrastructure_enabled=1 infrastructure_points=100",
            )
            for row in rows
        ]
        rows.extend((
            "infrastructure_extra id=0 name=Road cost=20 build_time=0 "
            "build_time_factor=3",
            "unit ref=u:10:100 scope=own owner=p:1:10 type_id=13 "
            "type=Warriors home_city=none converts_to_id=-1 "
            "converts_to=none tile=8 x=4 y=2 hp=10 moves=3 "
            "activity=idle activity_target=-1 activity_target_name=none "
            "activity_progress=0 transport_state=untransported "
            "transporter=none transport_capacity=0 occupied=0 "
            "paradropped=0 paradrop_range=0 controller=none has_orders=0 "
            "orders_repeat=0 orders_vigilant=0 order_count=0 "
            "orders_digest=fnv1a64-0000000000000000 "
            "orders_destination=-1",
        ))
        self.sidecar_factory.observation_rows = tuple(sorted(
            _complete_v2_action_row(row) for row in rows
        ))
        self.sidecar_factory.state_scope_rows = (
            "tile index=8 x=4 y=2 known=2 terrain=Grassland owner=none "
            "placing_extra=-1 placing_extra_name=none placing_turns=0 "
            "placing_time=2",
        )
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-infrastructure",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)

        overview = game.v2_get_page(
            joined["agent_id"], "state", "",
        )["page"]["items"][0]
        unit = game.v2_get_page(
            joined["agent_id"], "state", "section=units",
        )["page"]["items"][0]
        page = game.v2_get_page(
            joined["agent_id"], "legal_actions",
            f"actor_id={overview['player']['id']}&target_id={unit['tile_id']}",
        )

        action = page["page"]["items"][0]
        self.assertEqual(action["kind"], "player.set_infrastructure")
        self.assertEqual(
            [(choice["name"], choice["cost"], choice["turns"])
             for choice in action["subject"]["target"]["choices"]],
            [("Road", 20, 6)],
        )
        self.assertEqual(
            self.sidecar_factory.target_pipeline[-2:],
            [
                ("support", 11, "tile_window", "t8-r0"),
                ("target", 11, "p:1:10", 8),
            ],
        )

    def test_v2_nonempty_clause_scope_builds_once_and_pages_8200_rows(self):
        clause_count = 8200
        digest = v2_control._diplomacy_clauses_digest(tuple(
            {
                "giver_ref": "p:1:10", "native_type": 1,
                "native_value": index,
            }
            for index in range(clause_count)
        ))
        relation = (
            "diplomacy other=p:2:20 name=Claude nation=Romans state=Peace "
            "contact=5 alive=1 turns_left=0 can_meet=0 meeting=1 "
            "generation=3 self_accepted=0 other_accepted=0 "
            f"clause_count={clause_count} clauses_digest={digest} "
            "has_embassy=0 other_has_embassy=0 gives_vision=0 "
            "receives_vision=0 gives_shared_tiles=0 receives_shared_tiles=0 "
            "can_cancel=0 cancel_reason=not_allowed"
        )
        clauses = tuple(
            "diplomacy_clause other=p:2:20 generation=3 "
            f"position={index} giver=p:1:10 type=Gold value_kind=gold "
            f"value={index} name=gold"
            for index in range(clause_count)
        )
        self.sidecar_factory.observation_rows = tuple(sorted(
            native_v2_rows(action_count=0)
            + (_complete_v2_action_row(relation),) + clauses
        ))
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-clause-state",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        diplomacy = game.v2_get_page(
            joined["agent_id"], "state", "section=diplomacy",
        )["page"]["items"][0]
        before = self.sidecar_factory.state_scope_sections.count(
            "diplomacy_clauses",
        )
        first = game.v2_get_page(
            joined["agent_id"], "state",
            "section=diplomacy_clauses&relation_id="
            + diplomacy["relation_id"] + "&limit=16",
        )
        self.assertEqual(first["page"]["total_items"], clause_count)
        self.assertEqual(len(first["page"]["items"]), 16)
        self.assertIsNotNone(first["page"]["next_cursor"])
        self.assertEqual(
            self.sidecar_factory.state_scope_sections.count(
                "diplomacy_clauses",
            ) - before,
            1,
        )
        continued = game.v2_get_page(
            joined["agent_id"], "state",
            "cursor=" + first["page"]["next_cursor"],
        )
        self.assertEqual(len(continued["page"]["items"]), 16)
        self.assertEqual(
            self.sidecar_factory.state_scope_sections.count(
                "diplomacy_clauses",
            ) - before,
            1,
        )

    def test_v2_relation_scope_routes_pages_and_executes_exact_pair(self):
        relation = (
            "diplomacy other=p:2:20 name=Claude nation=Romans state=Peace "
            "contact=5 alive=1 turns_left=0 can_meet=0 meeting=1 "
            "generation=3 self_accepted=0 other_accepted=0 clause_count=0 "
            "clauses_digest=fnv1a64-cbf29ce484222325 has_embassy=0 "
            "other_has_embassy=0 gives_vision=0 receives_vision=0 "
            "gives_shared_tiles=0 receives_shared_tiles=0 can_cancel=0 "
            "cancel_reason=not_allowed"
        )
        self.sidecar_factory.observation_rows = tuple(sorted(
            native_v2_rows() + (_complete_v2_action_row(relation),)
        ))
        self.sidecar_factory.relation_rows = (
            native_v2_relation_action(
                0x401, "diplomacy.close_meeting", "Meeting%20Closed",
                "meeting",
            ),
            native_v2_relation_action(
                0x402, "diplomacy.accept", "Acceptance%20Recorded",
                "accepted",
            ),
        ) + tuple(
            native_v2_relation_action(
                0x410 + index, "diplomacy.propose_clause",
                "Clause%20Proposed", "Advance", clause_value=40 + index,
                clause_name=f"Tech{index}",
            )
            for index in range(15)
        )
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-diplomacy",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        overview = game.v2_get_page(
            joined["agent_id"], "state", "",
        )["page"]["items"][0]
        public_relation = game.v2_get_page(
            joined["agent_id"], "state", "section=diplomacy",
        )["page"]["items"][0]
        query = (
            f"actor_id={overview['player']['id']}"
            f"&target_id={public_relation['relation_id']}"
        )
        with self.assertRaises(APIProblem) as invalid_limit:
            game.v2_get_page(
                joined["agent_id"], "legal_actions", query + "&limit=1",
            )
        self.assertEqual(invalid_limit.exception.status, HTTPStatus.BAD_REQUEST)
        self.assertEqual(self.sidecar_factory.relation_scope_count, 0)
        first = game.v2_get_page(
            joined["agent_id"], "legal_actions", query,
        )
        self.assertEqual(first["page"]["total_items"], 17)
        self.assertEqual(len(first["page"]["items"]), 16)
        self.assertIsNotNone(first["page"]["next_cursor"])
        self.assertEqual(self.sidecar_factory.relation_scope_count, 1)
        self.assertEqual(first["page"]["scope"], {
            "actor_id": overview["player"]["id"],
            "actor_type": "player",
            "target_id": public_relation["relation_id"],
            "target_type": "diplomatic_relation",
        })
        accepted = next(
            action for action in first["page"]["items"]
            if action["subject"]["operation"] == "accept"
        )
        final = game.v2_get_page(
            joined["agent_id"], "legal_actions",
            f"cursor={first['page']['next_cursor']}",
        )
        self.assertEqual(len(final["page"]["items"]), 1)
        self.assertIsNone(final["page"]["next_cursor"])
        self.assertEqual(self.sidecar_factory.relation_scope_page_count, 0)
        status, receipt = game.v2_submit_batch(
            joined["agent_id"],
            self.v2_batch(
                game, joined, accepted, batch_id="batch_relation_accept",
            ),
        )
        self.assertEqual(status, HTTPStatus.OK)
        self.assertEqual(receipt["receipt_state"], "applied")
        self.assertEqual(self.sidecar_factory.relation_action_count, 1)
        self.assertEqual(self.sidecar_factory.last_relation_actor, "p:1:10")
        self.assertEqual(
            self.sidecar_factory.last_relation_counterpart, "p:2:20",
        )

    def test_v2_stale_actor_scope_cursor_is_retryable_conflict(self):
        self.sidecar_factory.observation_rows = native_v2_rows(
            tile_count=2, action_count=10,
        )
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-stale-cursor",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        server = make_supervisor_server(self.supervisor, "127.0.0.1", 0)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        host, port = server.server_address
        root = f"http://{host}:{port}/v2/games/{game.game_id}/me"
        try:
            status, state = raw_json_request(
                f"{root}/state", joined["agent_token"],
            )
            self.assertEqual(status, HTTPStatus.OK)
            player_id = state["page"]["items"][0]["player"]["id"]
            status, scoped = raw_json_request(
                f"{root}/legal-actions?actor_id={player_id}&limit=1",
                joined["agent_token"],
            )
            self.assertEqual(status, HTTPStatus.OK)
            cursor = scoped["page"]["next_cursor"]
            self.assertIsNotNone(cursor)

            for _ in range(2):
                self.sidecar_factory.native_revision += 1
                status, _ = raw_json_request(
                    f"{root}/state", joined["agent_token"],
                )
                self.assertEqual(status, HTTPStatus.OK)
            for _ in range(2):
                status, stale = raw_json_request(
                    f"{root}/legal-actions?cursor={cursor}",
                    joined["agent_token"],
                )
                self.assertEqual(status, HTTPStatus.CONFLICT)
                self.assertEqual(stale["error"]["code"], "stale_revision")
                self.assertTrue(stale["error"]["retryable"])
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)

    def test_v2_scoped_cursor_retry_replay_concurrency_and_expiry(self):
        self.sidecar_factory.observation_rows = native_v2_rows(tile_count=3)
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-cursor-retry",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        state = game.v2_get_page(joined["agent_id"], "state", "")
        player_id = state["page"]["items"][0]["player"]["id"]
        first = game.v2_get_page(
            joined["agent_id"], "legal_actions",
            f"actor_id={player_id}&limit=1",
        )
        cursor = first["page"]["next_cursor"]
        sidecar = self.sidecar_factory.created[-1]
        with patch.object(
            sidecar, "read_actor_scope_page",
            side_effect=AssertionError("continuation performed native I/O"),
        ):
            continued = game.v2_get_page(
                joined["agent_id"], "legal_actions", f"cursor={cursor}",
            )
            replayed = game.v2_get_page(
                joined["agent_id"], "legal_actions", f"cursor={cursor}",
            )
        self.assertEqual(
            json.dumps(continued, sort_keys=True, separators=(",", ":")),
            json.dumps(replayed, sort_keys=True, separators=(",", ":")),
        )

        concurrent_first = game.v2_get_page(
            joined["agent_id"], "legal_actions",
            f"actor_id={player_id}&limit=1",
        )
        concurrent_cursor = concurrent_first["page"]["next_cursor"]
        results: list[dict[str, Any]] = []
        errors: list[Exception] = []

        def fetch() -> None:
            try:
                results.append(game.v2_get_page(
                    joined["agent_id"], "legal_actions",
                    f"cursor={concurrent_cursor}",
                ))
            except Exception as exc:  # pragma: no cover - assertion reports it
                errors.append(exc)

        with patch.object(
            sidecar, "read_actor_scope_page",
            side_effect=AssertionError("continuation performed native I/O"),
        ):
            one = threading.Thread(target=fetch)
            two = threading.Thread(target=fetch)
            one.start()
            two.start()
            one.join(2)
            two.join(2)
        self.assertEqual(errors, [])
        self.assertEqual(len(results), 2)
        self.assertEqual(results[0], results[1])

        expiring = game.v2_get_page(
            joined["agent_id"], "state", "section=known_tiles&limit=1",
        )["page"]["next_cursor"]
        with patch(
            "agent_eval.v2_control.time.monotonic", return_value=10**12,
        ), self.assertRaises(APIProblem) as expired:
            game.v2_get_page(
                joined["agent_id"], "state", f"cursor={expiring}",
            )
        self.assertEqual(expired.exception.status, HTTPStatus.GONE)
        self.assertEqual(
            expired.exception.payload["error"]["code"], "cursor_expired",
        )
        self.assertEqual(
            expired.exception.payload["error"]["details"]["restart"],
            {
                "endpoint": "state",
                "query": {"section": "known_tiles", "limit": 1},
            },
        )
        with self.assertRaises(APIProblem) as forged:
            game.v2_get_page(
                joined["agent_id"], "state",
                "cursor=cursor_" + "x" * 32,
            )
        self.assertEqual(forged.exception.status, HTTPStatus.BAD_REQUEST)
        self.assertEqual(
            forged.exception.payload["error"]["code"], "invalid_request",
        )

    def test_v2_blocked_read_releases_game_lock_and_replacement_discards_page(self):
        self.sidecar_factory.observation_rows = native_v2_rows(tile_count=3)
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-blocked-model",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        old_state = game.v2_get_page(
            joined["agent_id"], "state", "section=known_tiles&limit=1",
        )
        old_cursor = old_state["page"]["next_cursor"]
        old_legal = game.v2_get_page(
            joined["agent_id"], "legal_actions", "",
        )
        old_action = old_legal["page"]["items"][0]["action_id"]
        entered = threading.Event()
        release = threading.Event()

        def blocked(sidecar, request_id, timeout_s):
            entered.set()
            self.assertTrue(release.wait(2))
            return {
                "generation": sidecar.generation,
                "native_revision": 11,
                "rows": native_v2_rows(),
            }

        self.sidecar_factory.read_hook = blocked
        outcomes = []

        def read():
            try:
                outcomes.append(game.v2_get_page(
                    joined["agent_id"], "state", "",
                ))
            except Exception as exc:
                outcomes.append(exc)

        worker = threading.Thread(target=read)
        worker.start()
        self.assertTrue(entered.wait(1))
        self.assertTrue(game.condition.acquire(timeout=0.2))
        game.condition.release()

        place = joined["place"]
        with game.condition:
            old_control = game.v2_controls.pop(place)
            generation = game.sidecar_generations[place] + 1
            replacement = game._make_sidecar(game.joinable_places[0], generation)
            replacement.state = "ready"
            game.sidecars[place] = replacement
            game.sidecar_generations[place] = generation
            game.sidecar_ready_generations[place] = generation
            game.sidecar_health[place] = game._sanitized_sidecar_health(
                replacement, generation,
            )
            replacement_control = V2SeatControl(
                game.game_id, joined["agent_id"], generation,
            )
            game.v2_controls[place] = replacement_control
            game.v2_execution_locks[place] = (
                generation, replacement_control, threading.RLock(),
            )
            game.condition.notify_all()
        old_control.close()
        release.set()
        worker.join(2)
        self.assertFalse(worker.is_alive())
        self.assertEqual(len(outcomes), 1)
        self.assertIsInstance(outcomes[0], APIProblem)
        self.assertEqual(outcomes[0].status, HTTPStatus.SERVICE_UNAVAILABLE)
        self.assertEqual(
            outcomes[0].payload["error"]["code"], "sidecar_unavailable",
        )
        self.assertFalse(game.v2_controls[place].has_snapshot)
        with self.assertRaises(APIProblem) as stale_cursor:
            game.v2_get_page(
                joined["agent_id"], "state", f"cursor={old_cursor}",
            )
        self.assertEqual(stale_cursor.exception.status, HTTPStatus.BAD_REQUEST)
        self.sidecar_factory.read_hook = None
        new_state = game.v2_get_page(joined["agent_id"], "state", "")
        new_legal = game.v2_get_page(
            joined["agent_id"], "legal_actions", "",
        )
        self.assertNotEqual(
            old_state["state_revision"]["state_token"],
            new_state["state_revision"]["state_token"],
        )
        self.assertNotEqual(
            old_action, new_legal["page"]["items"][0]["action_id"],
        )

    def test_v2_controller_is_closed_on_established_failure_and_cancel(self):
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-close-failure",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        game.v2_get_page(joined["agent_id"], "state", "")
        control = game.v2_controls[joined["place"]]
        self.assertTrue(control.has_snapshot)
        self.sidecar_factory.created[-1].die()
        self.assertFalse(control.has_snapshot)
        self.assertNotIn(joined["place"], game.v2_controls)

        created = self.create(
            mode="multiplayer", places=2, control_protocol="full-control-v2",
        )
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], 1, "codex-close-cancel",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        game.v2_get_page(joined["agent_id"], "state", "")
        control = game.v2_controls[joined["place"]]
        game.cancel()
        self.assertFalse(control.has_snapshot)
        self.assertEqual(game.v2_controls, {})
        with self.assertRaises(APIProblem) as stopped:
            game.v2_get_page(joined["agent_id"], "state", "")
        self.assertEqual(stopped.exception.status, HTTPStatus.SERVICE_UNAVAILABLE)

    def test_v2_batch_applies_persists_and_replays_after_terminal(self):
        _created, game, joined, action = self.ready_v2_non_phase_action()
        command = self.v2_batch(game, joined, action, "batch_applied")
        status, receipt = game.v2_submit_batch(joined["agent_id"], command)
        self.assertEqual(status, HTTPStatus.OK)
        self.assertEqual(receipt["receipt_state"], "applied")
        self.assertFalse(receipt["idempotent"])
        self.assertEqual(self.sidecar_factory.action_count, 1)
        self.assertGreater(
            receipt["state_revision"]["revision"],
            command["state_revision"]["revision"],
        )
        receipt_status, loaded = game.v2_get_receipt(
            joined["agent_id"], "batch_applied",
        )
        self.assertEqual(receipt_status, HTTPStatus.OK)
        self.assertEqual(loaded, receipt)

        game.cancel()
        duplicate_status, duplicate = game.v2_submit_batch(
            joined["agent_id"], command,
        )
        self.assertEqual(duplicate_status, HTTPStatus.OK)
        self.assertTrue(duplicate["idempotent"])
        self.assertEqual(self.sidecar_factory.action_count, 1)
        self.assertEqual(game.v2_controls, {})
        self.assertEqual(game.v2_execution_locks, {})

    def test_v2_accepted_is_readable_during_action_then_becomes_ambiguous(self):
        _created, game, joined, action = self.ready_v2_action()
        accepted = threading.Event()
        release = threading.Event()

        def uncertain(
            _sidecar, _request_id, _slot, _arguments, _timeout,
            expected_revision, on_accepted,
        ):
            on_accepted({
                "accepted": True,
                "accepted_revision": expected_revision,
            })
            accepted.set()
            self.assertTrue(release.wait(2))
            raise SidecarActionAmbiguous(
                {"accepted": True, "accepted_revision": expected_revision},
                "test_boundary",
            )

        self.sidecar_factory.action_hook = uncertain
        command = self.v2_batch(game, joined, action, "batch_uncertain")
        result = []
        worker = threading.Thread(
            target=lambda: result.append(game.v2_submit_batch(
                joined["agent_id"], command,
            )),
        )
        worker.start()
        self.assertTrue(accepted.wait(1))
        status, in_flight = game.v2_get_receipt(
            joined["agent_id"], "batch_uncertain",
        )
        self.assertEqual(status, HTTPStatus.OK)
        self.assertEqual(in_flight["receipt_state"], "accepted")
        self.assertNotIn("request", json.dumps(in_flight))
        self.assertNotIn("slot", json.dumps(in_flight))
        release.set()
        worker.join(2)
        self.assertFalse(worker.is_alive())
        self.assertEqual(result[0][0], HTTPStatus.ACCEPTED)
        self.assertEqual(result[0][1]["receipt_state"], "ambiguous")
        self.assertEqual(
            result[0][1]["error"]["error"]["code"],
            "action_outcome_ambiguous",
        )
        self.assertFalse(result[0][1]["error"]["error"]["retryable"])
        final_status, final_receipt = game.v2_get_receipt(
            joined["agent_id"], "batch_uncertain",
        )
        self.assertEqual(final_status, HTTPStatus.OK)
        self.assertEqual(final_receipt["receipt_state"], "ambiguous")

    def test_v2_correlated_ambiguity_keeps_game_alive_and_traces_privately(self):
        _created, game, joined, action = self.ready_v2_action()
        if action["kind"] == "phase.end":
            legal = game.v2_get_page(
                joined["agent_id"], "legal_actions", "limit=16",
            )
            action = next(
                item for item in legal["page"]["items"]
                if item["kind"] != "phase.end"
            )

        def correlated(
            _sidecar, request_id, _slot, _arguments, _timeout,
            expected_revision, on_accepted,
        ):
            acceptance = {
                "request_id": request_id,
                "accepted": True,
                "accepted_revision": expected_revision,
            }
            on_accepted(acceptance)
            raise SidecarActionAmbiguous(
                acceptance,
                "processing_timeout",
                stage="correlated_terminal",
                stream_synchronized=True,
            )

        self.sidecar_factory.action_hook = correlated
        command = self.v2_batch(
            game, joined, action, "batch_correlated_ambiguous",
        )
        status, receipt = game.v2_submit_batch(joined["agent_id"], command)
        self.assertEqual(status, HTTPStatus.ACCEPTED)
        self.assertEqual(receipt["receipt_state"], "ambiguous")
        self.assertEqual(game.state, "running")
        self.assertEqual(game.sidecars[joined["place"]].state, "ready")
        self.assertIn(joined["place"], game.v2_controls)

        trace_path = game.episode / TRACE_DIRECTORY / TRACE_FILENAME
        trace_rows = [
            json.loads(line)
            for line in trace_path.read_text(encoding="ascii").splitlines()
        ]
        self.assertEqual(len(trace_rows), 1)
        self.assertEqual(trace_rows[0]["stage"], "correlated_terminal")
        self.assertEqual(
            trace_rows[0]["ambiguity_reason"], "processing_timeout",
        )
        self.assertTrue(trace_rows[0]["acceptance_known"])
        self.assertEqual(trace_rows[0]["sidecar_health_state"], "ready")
        self.assertEqual(trace_rows[0]["seat_id"], joined["seat_id"])
        self.assertEqual(stat.S_IMODE(trace_path.stat().st_mode), 0o600)

        public = json.dumps({
            "receipt": receipt,
            "status": game.status(),
            "picker": game.picker_state(),
            "health": game.v2_health(joined["agent_id"]),
        })
        for private in (
            "processing_timeout", "correlated_terminal",
            "ambiguity_reason", "acceptance_known",
        ):
            self.assertNotIn(private, public)

        duplicate_status, duplicate = game.v2_submit_batch(
            joined["agent_id"], command,
        )
        self.assertEqual(duplicate_status, HTTPStatus.ACCEPTED)
        self.assertTrue(duplicate["idempotent"])
        self.assertEqual(self.sidecar_factory.action_count, 1)

        self.sidecar_factory.action_hook = None
        next_command = self.v2_batch(
            game, joined, action, "batch_after_correlated_ambiguous",
        )
        next_status, next_receipt = game.v2_submit_batch(
            joined["agent_id"], next_command,
        )
        self.assertEqual(next_status, HTTPStatus.OK)
        self.assertEqual(next_receipt["receipt_state"], "applied")
        self.assertEqual(game.state, "running")
        self.assertEqual(self.sidecar_factory.action_count, 2)

    def test_v2_ambiguity_trace_failure_never_changes_durable_receipt(self):
        _created, game, joined, action = self.ready_v2_action()
        trace = game.v2_ambiguity_trace
        self.assertIsNotNone(trace)
        trace.close()
        warning_count = game.v2_ambiguity_trace_warning_count
        self.sidecar_factory.action_error = SidecarActionAmbiguous(
            None, "acceptance_unavailable",
        )
        command = self.v2_batch(
            game, joined, action, "batch_trace_unavailable",
        )
        status, receipt = game.v2_submit_batch(joined["agent_id"], command)
        self.assertEqual(status, HTTPStatus.ACCEPTED)
        self.assertEqual(receipt["receipt_state"], "ambiguous")
        self.assertGreater(
            game.v2_ambiguity_trace_warning_count, warning_count,
        )
        loaded_status, loaded = game.v2_get_receipt(
            joined["agent_id"], "batch_trace_unavailable",
        )
        self.assertEqual(loaded_status, HTTPStatus.OK)
        self.assertEqual(loaded, receipt)
        duplicate_status, duplicate = game.v2_submit_batch(
            joined["agent_id"], command,
        )
        self.assertEqual(duplicate_status, HTTPStatus.ACCEPTED)
        self.assertTrue(duplicate["idempotent"])
        self.assertEqual(self.sidecar_factory.action_count, 1)

    def test_strategic_v1_has_no_v2_ambiguity_trace(self):
        created = self.create(control_protocol="strategic-v1")
        game = self.supervisor.game(created["game_id"])
        self.assertIsNone(game.v2_ambiguity_trace)
        self.assertFalse((game.episode / TRACE_DIRECTORY).exists())

    def test_v2_trace_construction_failure_does_not_block_game_or_receipts(self):
        with patch(
            "agent_eval.supervisor.V2AmbiguityTrace",
            side_effect=RuntimeError("SENSITIVE /private/trace"),
        ):
            created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        self.assertIsNone(game.v2_ambiguity_trace)
        self.assertEqual(game.v2_ambiguity_trace_warning_count, 1)
        joined = game.join(
            created["join_token"],
            controller_label="codex-trace-unavailable",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        self._seed_v2_phase(game)
        legal = game.v2_get_page(joined["agent_id"], "legal_actions", "")
        command = self.v2_batch(
            game, joined, legal["page"]["items"][0],
            "batch_without_trace",
        )
        self.sidecar_factory.action_error = SidecarActionAmbiguous(
            None, "acceptance_unavailable",
        )
        status, receipt = game.v2_submit_batch(joined["agent_id"], command)
        self.assertEqual(status, HTTPStatus.ACCEPTED)
        self.assertEqual(receipt["receipt_state"], "ambiguous")
        self.assertEqual(game.v2_ambiguity_trace_warning_count, 2)
        self.assertNotIn("SENSITIVE", json.dumps(receipt))

    def test_v2_same_batch_concurrency_executes_once_and_conflicts_on_change(self):
        _created, game, joined, action = self.ready_v2_action()
        accepted = threading.Event()
        release = threading.Event()

        def blocked(
            _sidecar, request_id, _slot, _arguments, _timeout,
            expected_revision, on_accepted,
        ):
            on_accepted({
                "request_id": request_id,
                "accepted": True,
                "accepted_revision": expected_revision,
            })
            accepted.set()
            self.assertTrue(release.wait(2))
            self.sidecar_factory.native_revision += 1
            return {
                "request_id": request_id,
                "accepted": True,
                "applied": True,
                "status": "applied",
                "reason": "POSTCONDITION_VERIFIED",
                "accepted_revision": expected_revision,
                "result_revision": self.sidecar_factory.native_revision,
                "observation_selector": None,
            }

        self.sidecar_factory.action_hook = blocked
        command = self.v2_batch(game, joined, action, "batch_concurrent")
        first = []
        worker = threading.Thread(
            target=lambda: first.append(game.v2_submit_batch(
                joined["agent_id"], command,
            )),
        )
        worker.start()
        self.assertTrue(accepted.wait(1))
        duplicate_status, duplicate = game.v2_submit_batch(
            joined["agent_id"], command,
        )
        self.assertEqual(duplicate_status, HTTPStatus.ACCEPTED)
        self.assertTrue(duplicate["idempotent"])
        changed = json.loads(json.dumps(command))
        changed["commands"][0]["arguments"] = {"changed": True}
        with self.assertRaises(APIProblem) as conflict:
            game.v2_submit_batch(joined["agent_id"], changed)
        self.assertEqual(conflict.exception.status, HTTPStatus.CONFLICT)
        self.assertEqual(
            conflict.exception.payload["error"]["code"], "conflict",
        )
        self.assertEqual(self.sidecar_factory.action_count, 1)
        release.set()
        worker.join(2)
        self.assertFalse(worker.is_alive())
        self.assertEqual(first[0][0], HTTPStatus.OK)
        self.assertEqual(self.sidecar_factory.action_count, 1)

    def test_v2_second_batch_at_old_revision_is_stale_without_reservation(self):
        _created, game, joined, action = self.ready_v2_action()
        first = self.v2_batch(game, joined, action, "batch_first")
        second = self.v2_batch(game, joined, action, "batch_second")
        self.assertEqual(
            game.v2_submit_batch(joined["agent_id"], first)[0],
            HTTPStatus.OK,
        )
        with self.assertRaises(APIProblem) as stale:
            game.v2_submit_batch(joined["agent_id"], second)
        self.assertEqual(stale.exception.status, HTTPStatus.CONFLICT)
        self.assertEqual(
            stale.exception.payload["error"]["code"], "stale_revision",
        )
        self.assertEqual(
            stale.exception.payload["error"]["details"],
            {
                "batch_id": "batch_second",
                "acceptance": "not_accepted",
                "safe_next": "refresh",
                "rejection": {
                    "layer": "revision",
                    "reason": "revision_stale",
                    "native_code": None,
                    "native_reason": None,
                },
            },
        )
        self.assertEqual(self.sidecar_factory.action_count, 1)
        with self.assertRaises(APIProblem) as absent:
            game.v2_get_receipt(joined["agent_id"], "batch_second")
        self.assertEqual(absent.exception.status, HTTPStatus.NOT_FOUND)

    def test_v2_batch_recovery_details_are_safe_and_identity_gated(self):
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-recovery-model",
            supported_control_protocols=["full-control-v2"],
        )
        revision = {
            "turn": 1, "revision": 1, "state_token": "state_test",
        }
        valid = {
            "schema_version": 2,
            "control_protocol": "full-control-v2",
            "game_id": game.game_id,
            "agent_id": joined["agent_id"],
            "batch_id": "batch_public_contract",
            "state_revision": revision,
            "commands": [{"action_id": "action_test", "arguments": {}}],
        }

        malformed = dict(valid)
        malformed["batch_id"] = "batch_malformed_must_not_echo"
        malformed.pop("commands")
        with self.assertRaises(APIProblem) as malformed_error:
            game.v2_submit_batch(joined["agent_id"], malformed)
        self.assertNotIn(
            malformed["batch_id"], json.dumps(malformed_error.exception.payload),
        )
        # The attribution names the layer that refused and nothing else; the
        # caller's batch ID and body still never appear in the payload.
        self.assertEqual(
            malformed_error.exception.payload["error"]["details"],
            {
                "rejection": {
                    "layer": "schema",
                    "reason": "batch_malformed",
                    "native_code": None,
                    "native_reason": None,
                },
            },
        )

        for field, wrong in (
            ("game_id", "game_wrong_identity"),
            ("agent_id", "agent_wrong_identity"),
        ):
            with self.subTest(field=field):
                mismatched = json.loads(json.dumps(valid))
                mismatched["batch_id"] = f"batch_wrong_{field}"
                mismatched[field] = wrong
                with self.assertRaises(APIProblem) as mismatch_error:
                    game.v2_submit_batch(joined["agent_id"], mismatched)
                encoded = json.dumps(mismatch_error.exception.payload)
                self.assertNotIn(mismatched["batch_id"], encoded)
                self.assertNotIn("not_accepted", encoded)

        with game.condition:
            game.v2_receipts_closing = True
        try:
            with self.assertRaises(APIProblem) as closing:
                game.v2_submit_batch(joined["agent_id"], valid)
        finally:
            with game.condition:
                game.v2_receipts_closing = False
        self.assertEqual(closing.exception.status, HTTPStatus.SERVICE_UNAVAILABLE)
        self.assertEqual(closing.exception.payload["error"]["details"], {
            "batch_id": valid["batch_id"],
            "acceptance": "not_accepted",
            "safe_next": "refresh",
        })

    def test_v2_batch_recovery_mapping_and_reserved_uncertainty(self):
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-receipt-model",
            supported_control_protocols=["full-control-v2"],
        )
        for code, retryable, expected in (
            ("stale_revision", True, "refresh"),
            ("action_expired", True, "refresh"),
            ("illegal_action", False, "refresh"),
            ("rate_limited", True, "retry_exact"),
            ("sidecar_unavailable", True, "retry_exact"),
            ("sidecar_unavailable", False, "refresh"),
            ("conflict", False, "receipt_first"),
            ("internal_error", False, "receipt_first"),
        ):
            with self.subTest(code=code, retryable=retryable):
                problem = game._v2_problem(
                    HTTPStatus.CONFLICT, code, "public", retryable=retryable,
                )
                decorated = game._v2_not_accepted_problem(
                    problem, "batch_mapping",
                )
                self.assertEqual(
                    decorated.payload["error"]["details"]["safe_next"],
                    expected,
                )

        batch = {
            "schema_version": 2,
            "control_protocol": "full-control-v2",
            "game_id": game.game_id,
            "agent_id": joined["agent_id"],
            "batch_id": "batch_reserved_uncertain",
            "state_revision": {
                "turn": 1, "revision": 1, "state_token": "state_test",
            },
            "commands": [{"action_id": "action_test", "arguments": {}}],
        }
        reservation = game.v2_receipt_store.reserve(batch)
        self.assertTrue(reservation.created)
        with self.assertRaises(APIProblem) as uncertain:
            game.v2_submit_batch(joined["agent_id"], batch)
        self.assertEqual(
            uncertain.exception.payload["error"]["code"], "internal_error",
        )
        self.assertEqual(
            uncertain.exception.payload["error"]["details"], {},
        )

    def test_v2_scope_size_errors_match_openapi_413(self):
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        for source in (
            V2ControlError("scope_too_large"),
            SidecarError("actor_scope_too_large"),
            SidecarError("relation_scope_too_large"),
            SidecarError("state_scope_too_large"),
        ):
            with self.subTest(source=str(source)), self.assertRaises(
                APIProblem,
            ) as raised:
                game._raise_v2_get_error(source)
            self.assertEqual(
                raised.exception.status, HTTPStatus.REQUEST_ENTITY_TOO_LARGE,
            )
            self.assertEqual(
                raised.exception.payload["error"]["code"], "scope_too_large",
            )

    def test_v2_wait_default_is_phase_actionable_not_global_revision(self):
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-wait-model",
            supported_control_protocols=["full-control-v2"],
        )

        def health(*, state="running", active=False, phase_state="waiting"):
            return {
                "game_state": state,
                "phase": {"active": active, "state": phase_state},
                "observation_available": True,
            }

        with patch.object(game, "v2_health", return_value=health()):
            waited = game.v2_wait(joined["agent_id"], 0)
        self.assertEqual(waited["wake_reason"], "timeout")
        self.assertIsNone(waited["state_revision"])

        with patch.object(game, "v2_health", return_value=health(
            active=True, phase_state="ending",
        )):
            waited = game.v2_wait(joined["agent_id"], 0)
        self.assertEqual(waited["wake_reason"], "timeout")

        with patch.object(game, "v2_health", return_value=health(
            active=True, phase_state="awaiting_agent",
        )):
            waited = game.v2_wait(joined["agent_id"], 0)
        self.assertEqual(waited["wake_reason"], "phase_active")

        with patch.object(game, "v2_health", return_value=health(
            state="completed",
        )):
            waited = game.v2_wait(joined["agent_id"], 0)
        self.assertEqual(waited["wake_reason"], "game_terminal")

        revision = {
            "turn": 8, "revision": 3, "state_token": "state_new:3",
        }
        with patch.object(game, "v2_health", return_value=health()), patch.object(
            game, "v2_get_page", return_value={"state_revision": revision},
        ) as state_read:
            waited = game.v2_wait(
                joined["agent_id"], 0, until="revision",
                after_state_token="state_old:2",
            )
        self.assertEqual(waited["wake_reason"], "revision_changed")
        self.assertEqual(waited["state_revision"], revision)
        state_read.assert_called_once()

    def test_v2_wait_http_is_auth_first_and_openapi_is_public(self):
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-http-wait-model",
            supported_control_protocols=["full-control-v2"],
        )
        response = {
            "schema_version": 2,
            "control_protocol": "full-control-v2",
            "game_id": game.game_id,
            "agent_id": joined["agent_id"],
            "wake_reason": "timeout",
            "health": {"game_state": "running"},
            "state_revision": None,
        }
        server = make_supervisor_server(self.supervisor, "127.0.0.1", 0)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        host, port = server.server_address
        base = f"http://{host}:{port}"
        try:
            status, contract = raw_json_request(f"{base}/v2/openapi.json")
            self.assertEqual(status, HTTPStatus.OK)
            self.assertIn(
                "/v2/games/{game_id}/me/wait", contract["paths"],
            )
            status, unauthorized = raw_json_request(
                f"{base}/v2/games/{game.game_id}/me/wait?wait_s=invalid",
            )
            self.assertEqual(status, HTTPStatus.UNAUTHORIZED)
            self.assertEqual(unauthorized["error"]["code"], "invalid_request")
            status, invalid_encoding = raw_json_request(
                f"{base}/v2/games/{game.game_id}/me/wait?"
                "until=revision&after_state_token=%FF",
                joined["agent_token"],
            )
            self.assertEqual(status, HTTPStatus.BAD_REQUEST)
            self.assertEqual(
                invalid_encoding["error"]["code"], "invalid_request",
            )
            with patch.object(game, "v2_wait", return_value=response) as wait:
                status, value = raw_json_request(
                    f"{base}/v2/games/{game.game_id}/me/wait?"
                    "wait_s=0&until=revision&after_state_token=state_old%3A2",
                    joined["agent_token"],
                )
            self.assertEqual(status, HTTPStatus.OK)
            self.assertEqual(value, response)
            wait.assert_called_once_with(
                joined["agent_id"], 0.0, until="revision",
                after_state_token="state_old:2",
            )
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)

    def test_v2_definitive_native_rejections_are_durable_and_sanitized(self):
        _created, game, joined, action = self.ready_v2_action()
        cases = (
            ("native_busy", HTTPStatus.TOO_MANY_REQUESTS, "rate_limited"),
            ("stale_slot", HTTPStatus.GONE, "action_expired"),
            ("stale_entity", HTTPStatus.GONE, "action_expired"),
            ("native_not_ready", HTTPStatus.SERVICE_UNAVAILABLE,
             "sidecar_unavailable"),
            ("native_bad_argument", HTTPStatus.UNPROCESSABLE_ENTITY,
             "illegal_action"),
        )
        for index, (native, expected_status, expected_code) in enumerate(cases):
            with self.subTest(native=native):
                self.sidecar_factory.action_error = SidecarActionNotAccepted(
                    native, "SENSITIVE /private/native",
                )
                command = self.v2_batch(
                    game, joined, action, f"batch_reject_{index}",
                )
                status, receipt = game.v2_submit_batch(
                    joined["agent_id"], command,
                )
                self.assertEqual(status, expected_status)
                self.assertEqual(receipt["receipt_state"], "rejected")
                self.assertEqual(
                    receipt["error"]["error"]["code"], expected_code,
                )
                encoded = json.dumps(receipt)
                self.assertNotIn("SENSITIVE", encoded)
                self.assertNotIn("/private", encoded)

    def test_v2_postcondition_rejection_uses_fresh_public_revision(self):
        _created, game, joined, action = self.ready_v2_action()

        def rejected(
            _sidecar, request_id, _slot, _arguments, _timeout,
            expected_revision, on_accepted,
        ):
            on_accepted({
                "request_id": request_id,
                "accepted": True,
                "accepted_revision": expected_revision,
            })
            self.sidecar_factory.native_revision += 1
            return {
                "request_id": request_id,
                "accepted": True,
                "applied": False,
                "status": "rejected",
                "reason": "POSTCONDITION_NOT_MET",
                "accepted_revision": expected_revision,
                "result_revision": self.sidecar_factory.native_revision,
                "observation_selector": None,
            }

        self.sidecar_factory.action_hook = rejected
        command = self.v2_batch(game, joined, action, "batch_postcondition")
        status, receipt = game.v2_submit_batch(joined["agent_id"], command)
        self.assertEqual(status, HTTPStatus.UNPROCESSABLE_ENTITY)
        self.assertEqual(receipt["receipt_state"], "rejected")
        self.assertEqual(
            receipt["error"]["error"]["code"], "illegal_action",
        )
        self.assertEqual(
            receipt["state_revision"], receipt["error"]["state_revision"],
        )
        self.assertGreater(
            receipt["state_revision"]["revision"],
            command["state_revision"]["revision"],
        )

    def test_v2_applied_investigation_materializes_exact_revision_observation(self):
        rows = tuple(sorted((*native_v2_rows(tile_count=7),
            "city_site ref=c:30:300 owner=p:2:20 name=Beta tile=6 x=6 y=0 "
            "size=3 visibility=visible",
        )))
        self.sidecar_factory.observation_rows = rows
        _created, game, joined, action = self.ready_v2_action()
        self.sidecar_factory.investigation_rows = tuple((
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
        control = game.v2_controls[joined["place"]]
        resolution = V2ActionResolution(
            native_slot="a0000000000000000",
            native_revision=self.sidecar_factory.native_revision,
            native_arguments="-",
            public_kind="unit.perform_action",
            operation="investigate_city",
            turn=7,
            phase=1,
        )

        def investigated(
            _sidecar, request_id, _slot, _arguments, _timeout,
            expected_revision, on_accepted,
        ):
            on_accepted({
                "request_id": request_id,
                "accepted": True,
                "accepted_revision": expected_revision,
            })
            self.sidecar_factory.native_revision += 1
            return {
                "request_id": request_id,
                "accepted": True,
                "applied": True,
                "status": "applied",
                "reason": "POSTCONDITION_VERIFIED",
                "accepted_revision": expected_revision,
                "result_revision": self.sidecar_factory.native_revision,
                "observation_selector": "i0123456789abcdef",
            }

        self.sidecar_factory.action_hook = investigated
        with patch.object(control, "resolve_action", return_value=resolution):
            status, receipt = game.v2_submit_batch(
                joined["agent_id"],
                self.v2_batch(
                    game, joined, action, "batch_investigate_city",
                ),
            )
        self.assertEqual(status, HTTPStatus.OK)
        self.assertEqual(receipt["receipt_state"], "applied")
        self.assertEqual(receipt["observation"]["type"], "city_investigation")
        self.assertEqual(
            receipt["observation"]["state_revision"],
            receipt["state_revision"],
        )
        self.assertEqual(receipt["observation"]["city"]["name"], "Beta")
        public = json.dumps(receipt, sort_keys=True)
        self.assertNotIn("c:30:300", public)
        self.assertNotIn("i0123456789abcdef", public)
        self.assertEqual(
            self.sidecar_factory.state_scope_sections[-1], "investigation",
        )

    def test_v2_context_loss_before_and_after_reservation_is_fail_closed(self):
        _created, game, joined, action = self.ready_v2_action()
        control = game.v2_controls[joined["place"]]
        original_resolve = control.resolve_action

        def lose_before(*args, **kwargs):
            resolved = original_resolve(*args, **kwargs)
            with game.condition:
                game.v2_execution_locks.pop(joined["place"], None)
            return resolved

        control.resolve_action = lose_before
        before = self.v2_batch(game, joined, action, "batch_before_loss")
        with self.assertRaises(APIProblem) as unavailable:
            game.v2_submit_batch(joined["agent_id"], before)
        self.assertEqual(
            unavailable.exception.status, HTTPStatus.SERVICE_UNAVAILABLE,
        )
        with self.assertRaises(APIProblem) as absent:
            game.v2_get_receipt(joined["agent_id"], "batch_before_loss")
        self.assertEqual(absent.exception.status, HTTPStatus.NOT_FOUND)
        self.assertEqual(self.sidecar_factory.action_count, 0)

        # Restore the exact generation lock, then invalidate it only after the
        # durable reservation has been created.
        execution_lock = threading.Lock()
        with game.condition:
            game.v2_execution_locks[joined["place"]] = (
                control.generation, control, execution_lock,
            )
        control.resolve_action = original_resolve
        store = game.v2_receipt_store
        original_reserve = store.reserve

        def lose_after(batch):
            reservation = original_reserve(batch)
            with game.condition:
                game.v2_execution_locks.pop(joined["place"], None)
            return reservation

        with patch.object(store, "reserve", side_effect=lose_after):
            after = self.v2_batch(
                game, joined, action, "batch_after_reserve_loss",
            )
            status, receipt = game.v2_submit_batch(joined["agent_id"], after)
        self.assertEqual(status, HTTPStatus.ACCEPTED)
        self.assertEqual(receipt["receipt_state"], "ambiguous")
        self.assertEqual(self.sidecar_factory.action_count, 0)

    def test_v2_context_loss_at_native_rejection_is_ambiguous(self):
        _created, game, joined, action = self.ready_v2_action()

        def lose_during_send(
            _sidecar, _request_id, _slot, _arguments, _timeout,
            _expected_revision, _on_accepted,
        ):
            with game.condition:
                game.v2_execution_locks.pop(joined["place"], None)
            raise SidecarActionNotAccepted("native_not_ready")

        self.sidecar_factory.action_hook = lose_during_send
        command = self.v2_batch(game, joined, action, "batch_send_loss")
        status, receipt = game.v2_submit_batch(joined["agent_id"], command)
        self.assertEqual(status, HTTPStatus.ACCEPTED)
        self.assertEqual(receipt["receipt_state"], "ambiguous")
        self.assertEqual(
            receipt["error"]["error"]["code"],
            "action_outcome_ambiguous",
        )

    def test_v2_missing_fresh_result_state_is_accepted_but_ambiguous(self):
        _created, game, joined, action = self.ready_v2_non_phase_action()
        reads = 0

        def fail_second(sidecar, _request_id, _timeout):
            nonlocal reads
            reads += 1
            if reads == 2:
                raise SidecarError(
                    "snapshot_gone", "SENSITIVE /private/native",
                )
            return {
                "generation": sidecar.generation,
                "native_revision": self.sidecar_factory.native_revision,
                "rows": self.sidecar_factory.observation_rows,
            }

        self.sidecar_factory.read_hook = fail_second
        command = self.v2_batch(game, joined, action, "batch_fresh_missing")
        status, receipt = game.v2_submit_batch(joined["agent_id"], command)
        self.assertEqual(status, HTTPStatus.ACCEPTED)
        self.assertEqual(receipt["receipt_state"], "ambiguous")
        encoded = json.dumps(receipt)
        self.assertNotIn("SENSITIVE", encoded)
        self.assertNotIn("/private", encoded)
        trace_path = game.episode / TRACE_DIRECTORY / TRACE_FILENAME
        trace = json.loads(trace_path.read_text(encoding="ascii"))
        self.assertEqual(trace["stage"], "post_result_observation")
        self.assertEqual(
            trace["ambiguity_reason"], "observation_unavailable",
        )
        self.assertTrue(trace["acceptance_known"])

    def test_v2_post_result_native_not_ready_is_retried(self):
        _created, game, joined, action = self.ready_v2_non_phase_action()
        reads = 0

        def transient_second_read(sidecar, _request_id, _timeout):
            nonlocal reads
            reads += 1
            if reads == 2:
                raise SidecarError("native_not_ready")
            return {
                "generation": sidecar.generation,
                "native_revision": self.sidecar_factory.native_revision,
                "rows": self.sidecar_factory.observation_rows,
            }

        self.sidecar_factory.read_hook = transient_second_read
        command = self.v2_batch(game, joined, action, "batch_fresh_retry")
        status, receipt = game.v2_submit_batch(joined["agent_id"], command)
        self.assertEqual(status, HTTPStatus.OK)
        self.assertEqual(receipt["receipt_state"], "applied")
        self.assertEqual(reads, 3)

    def test_v2_batch_holds_no_game_condition_during_locks_io_or_projection(self):
        _created, game, joined, action = self.ready_v2_action()
        place = joined["place"]
        control = game.v2_controls[place]
        store = game.v2_receipt_store

        def assert_unlocked():
            self.assertFalse(game.condition._is_owned())

        class CheckedLock:
            def __init__(self):
                self.lock = threading.Lock()

            def acquire(self, *args, **kwargs):
                assert_unlocked()
                return self.lock.acquire(*args, **kwargs)

            def release(self):
                assert_unlocked()
                return self.lock.release()

        checked_lock = CheckedLock()
        with game.condition:
            game.v2_execution_locks[place] = (
                control.generation, control, checked_lock,
            )

        original_probe = store.probe
        original_reserve = store.reserve
        original_transition = store.transition
        original_resolve = control.resolve_action

        def checked_probe(*args, **kwargs):
            assert_unlocked()
            return original_probe(*args, **kwargs)

        def checked_reserve(*args, **kwargs):
            assert_unlocked()
            return original_reserve(*args, **kwargs)

        def checked_transition(*args, **kwargs):
            assert_unlocked()
            return original_transition(*args, **kwargs)

        def checked_resolve(*args, **kwargs):
            assert_unlocked()
            return original_resolve(*args, **kwargs)

        def checked_read(sidecar, _request_id, _timeout):
            assert_unlocked()
            return {
                "generation": sidecar.generation,
                "native_revision": self.sidecar_factory.native_revision,
                "rows": self.sidecar_factory.observation_rows,
            }

        def checked_action(
            _sidecar, request_id, _slot, _arguments, _timeout,
            expected_revision, on_accepted,
        ):
            assert_unlocked()
            on_accepted({
                "request_id": request_id,
                "accepted": True,
                "accepted_revision": expected_revision,
            })
            self.sidecar_factory.native_revision += 1
            return {
                "request_id": request_id,
                "accepted": True,
                "applied": True,
                "status": "applied",
                "reason": "POSTCONDITION_VERIFIED",
                "accepted_revision": expected_revision,
                "result_revision": self.sidecar_factory.native_revision,
                "observation_selector": None,
            }

        self.sidecar_factory.read_hook = checked_read
        self.sidecar_factory.action_hook = checked_action
        with patch.object(store, "probe", side_effect=checked_probe), patch.object(
            store, "reserve", side_effect=checked_reserve,
        ), patch.object(
            store, "transition", side_effect=checked_transition,
        ), patch.object(control, "resolve_action", side_effect=checked_resolve):
            command = self.v2_batch(game, joined, action, "batch_no_condition")
            status, receipt = game.v2_submit_batch(joined["agent_id"], command)
        self.assertEqual(status, HTTPStatus.OK)
        self.assertEqual(receipt["receipt_state"], "applied")

    def test_v2_receipt_store_remains_open_until_supervisor_close(self):
        _created, game, joined, action = self.ready_v2_action()
        command = self.v2_batch(game, joined, action, "batch_close")
        game.v2_submit_batch(joined["agent_id"], command)
        store = game.v2_receipt_store
        game.cancel()
        self.assertEqual(
            store.lookup(joined["agent_id"], "batch_close")["receipt_state"],
            "applied",
        )
        self.supervisor.close()
        with self.assertRaises(Exception):
            store.lookup(joined["agent_id"], "batch_close")

    def test_v2_supervisor_close_drains_blocked_batch_before_store_close(self):
        _created, game, joined, action = self.ready_v2_non_phase_action()
        trace_store = game.v2_ambiguity_trace
        self.assertIsNotNone(trace_store)
        accepted = threading.Event()
        release = threading.Event()

        def blocked(
            _sidecar, request_id, _slot, _arguments, _timeout,
            expected_revision, on_accepted,
        ):
            on_accepted({
                "request_id": request_id,
                "accepted": True,
                "accepted_revision": expected_revision,
            })
            accepted.set()
            self.assertTrue(release.wait(2))
            self.sidecar_factory.native_revision += 1
            return {
                "request_id": request_id,
                "accepted": True,
                "applied": True,
                "status": "applied",
                "reason": "POSTCONDITION_VERIFIED",
                "accepted_revision": expected_revision,
                "result_revision": self.sidecar_factory.native_revision,
                "observation_selector": None,
            }

        self.sidecar_factory.action_hook = blocked
        command = self.v2_batch(game, joined, action, "batch_close_race")
        action_result = []
        action_thread = threading.Thread(
            target=lambda: action_result.append(game.v2_submit_batch(
                joined["agent_id"], command,
            )),
        )
        action_thread.start()
        self.assertTrue(accepted.wait(1))
        close_thread = threading.Thread(target=self.supervisor.close)
        close_thread.start()
        deadline = time.monotonic() + 1
        while not game.v2_receipts_closing and time.monotonic() < deadline:
            time.sleep(0.005)
        self.assertTrue(game.v2_receipts_closing)
        self.assertTrue(close_thread.is_alive())
        release.set()
        action_thread.join(2)
        close_thread.join(2)
        self.assertFalse(action_thread.is_alive())
        self.assertFalse(close_thread.is_alive())
        self.assertEqual(action_result[0][0], HTTPStatus.ACCEPTED)
        self.assertEqual(action_result[0][1]["receipt_state"], "ambiguous")
        self.assertEqual(game.v2_active_receipt_operations, 0)
        trace_path = game.episode / TRACE_DIRECTORY / TRACE_FILENAME
        trace = json.loads(trace_path.read_text(encoding="ascii"))
        self.assertEqual(trace["stage"], "post_result_observation")
        self.assertEqual(
            trace["ambiguity_reason"], "observation_unavailable",
        )
        with self.assertRaises(Exception):
            trace_store.record(
                agent_id=joined["agent_id"],
                batch_id="batch_after_close",
                seat_id=joined["seat_id"],
                stage="pre_accept",
                ambiguity_reason="context_lost",
                sidecar_generation=1,
                sidecar_health_state="stopped",
                acceptance_known=False,
            )

    def test_v2_batch_and_receipt_http_routes_are_strict_and_auth_first(self):
        _created, game, joined, action = self.ready_v2_action()
        command = self.v2_batch(game, joined, action, "batch_http")
        server = make_supervisor_server(self.supervisor, "127.0.0.1", 0)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        host, port = server.server_address
        root = f"http://{host}:{port}/v2/games/{game.game_id}/me"
        try:
            applied = request_json(
                "POST", f"{root}/batches",
                token=joined["agent_token"], body=command,
            )
            self.assertEqual(applied["receipt_state"], "applied")
            loaded = request_json(
                "GET", f"{root}/receipts/batch_http",
                token=joined["agent_token"],
            )
            self.assertEqual(loaded, applied)
            duplicate = request_json(
                "POST", f"{root}/batches",
                token=joined["agent_token"], body=command,
            )
            self.assertTrue(duplicate["idempotent"])
            self.assertEqual(self.sidecar_factory.action_count, 1)
            with patch.object(
                game.v2_receipt_store,
                "lookup",
                side_effect=RuntimeError("SENSITIVE /private/receipt/path"),
            ):
                failed_status, failed_lookup = raw_json_request(
                    f"{root}/receipts/batch_http",
                    joined["agent_token"],
                )
            self.assertEqual(
                failed_status, HTTPStatus.INTERNAL_SERVER_ERROR,
            )
            self.assertEqual(
                failed_lookup["error"]["code"], "internal_error",
            )
            self.assertNotIn("SENSITIVE", json.dumps(failed_lookup))
            self.assertNotIn("/private", json.dumps(failed_lookup))

            with self.assertRaises(ClientError) as query:
                request_json(
                    "POST", f"{root}/batches?extra=1",
                    token=joined["agent_token"], body=command,
                )
            self.assertEqual(query.exception.status, HTTPStatus.BAD_REQUEST)
            with self.assertRaises(ClientError) as trailing:
                request_json(
                    "POST", f"{root}/batches/",
                    token=joined["agent_token"], body=command,
                )
            self.assertEqual(
                trailing.exception.status, HTTPStatus.BAD_REQUEST,
            )
            self.assertEqual(self.sidecar_factory.action_count, 1)
            with self.assertRaises(ClientError) as receipt_query:
                request_json(
                    "GET", f"{root}/receipts/batch_http?extra=1",
                    token=joined["agent_token"],
                )
            self.assertEqual(
                receipt_query.exception.status, HTTPStatus.BAD_REQUEST,
            )
            with self.assertRaises(ClientError) as alias:
                request_json(
                    "GET", f"{root}/receipts/%62atch_http",
                    token=joined["agent_token"],
                )
            self.assertEqual(alias.exception.status, HTTPStatus.NOT_FOUND)

            connection = http.client.HTTPConnection(host, port, timeout=2)
            connection.request(
                "POST", f"/v2/games/{game.game_id}/me/batches",
                body=b"{",
                headers={"Content-Length": "1"},
            )
            response = connection.getresponse()
            unauthenticated = json.loads(response.read().decode("utf-8"))
            self.assertEqual(response.status, HTTPStatus.UNAUTHORIZED)
            self.assertEqual(
                unauthenticated["error"]["code"], "invalid_request",
            )
            connection.close()

            duplicate_json = (
                '{"schema_version":2,"schema_version":2}'
            ).encode("utf-8")
            connection = http.client.HTTPConnection(host, port, timeout=2)
            connection.request(
                "POST", f"/v2/games/{game.game_id}/me/batches",
                body=duplicate_json,
                headers={
                    "Authorization": f"Bearer {joined['agent_token']}",
                    "Content-Length": str(len(duplicate_json)),
                },
            )
            response = connection.getresponse()
            malformed = json.loads(response.read().decode("utf-8"))
            self.assertEqual(response.status, HTTPStatus.BAD_REQUEST)
            self.assertEqual(malformed["error"]["code"], "invalid_batch")
            connection.close()
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)

    def test_v2_monitor_does_not_require_strategic_bridge_journal(self):
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])

        class FinishedProcess:
            stdin = None

            def wait(self):
                return 0

            def poll(self):
                return 0

        game.process = FinishedProcess()
        (game.episode / "score.log").write_text("score", encoding="utf-8")
        with patch(
            "agent_eval.supervisor.validate_bridge_journal",
            side_effect=AssertionError("v2 must not validate bridge journal"),
        ), patch.object(game, "_configured_score_snapshot", return_value={}), patch(
            "agent_eval.supervisor.summarize_episode", return_value={}
        ):
            game._monitor()
        self.assertEqual(game.state, "completed")

    def test_timing_presets_are_strict_and_custom_timeout_is_preserved(self):
        for mode, timeout in (
            ("default", 180),
            ("blitz", 60),
            ("infinite", None),
        ):
            with self.subTest(mode=mode):
                config = self.supervisor._config({"timing_mode": mode})
                self.assertEqual(config["timing_mode"], mode)
                self.assertEqual(config["action_timeout_s"], timeout)
        custom = self.supervisor._config({"action_timeout_s": 7.5})
        self.assertEqual(custom["timing_mode"], "custom")
        self.assertEqual(custom["action_timeout_s"], 7.5)
        self.assertEqual(custom["difficulty"], "hard")

    def test_v2_rejects_blitz_and_difficulty_is_validated(self):
        v2 = {"control_protocol": "full-control-v2"}
        config = self.supervisor._config(dict(v2))
        self.assertEqual(config["timing_mode"], "default")
        self.assertEqual(config["action_timeout_s"], 600)
        self.assertEqual(config["difficulty"], "hard")
        with self.assertRaises(APIProblem) as refused:
            self.supervisor._config({**v2, "timing_mode": "blitz"})
        self.assertIn("blitz is strategic-v1 only", str(refused.exception))
        infinite = self.supervisor._config({**v2, "timing_mode": "infinite"})
        self.assertIsNone(infinite["action_timeout_s"])
        cheating = self.supervisor._config({**v2, "difficulty": "cheating"})
        self.assertEqual(cheating["difficulty"], "cheating")
        v1_blitz = self.supervisor._config({"timing_mode": "blitz"})
        self.assertEqual(v1_blitz["action_timeout_s"], 60)
        with self.assertRaises(APIProblem):
            self.supervisor._config({"difficulty": "deity"})
        for payload in (
            {"timing_mode": "turbo"},
            {"timing_mode": "blitz", "action_timeout_s": 180},
            {"timing_mode": "infinite", "action_timeout_s": 60},
        ):
            with self.subTest(payload=payload), self.assertRaises(APIProblem):
                self.supervisor._config(payload)

    def test_bridge_zero_transport_timeout_means_no_curl_deadline(self):
        source = Path("agent_eval/bridge.lua").read_text(encoding="utf-8")
        self.assertIn("turn_timeout_s < 0", source)
        self.assertIn("--max-time 0 as no timeout", source)
        self.assertIn('"curl --silent --show-error --fail --max-time "', source)
        command_start = source.index('local command = "curl')
        command_end = source.index("local execute_ok", command_start)
        curl_argv_source = source[command_start:command_end]
        self.assertNotIn("internal_token", curl_argv_source)
        self.assertIn('"--config " .. shell_quote(auth_path)', curl_argv_source)
        self.assertIn("umask 077", source)
        self.assertIn(
            "Authorization: Bearer ' .. internal_token", source,
        )

    def test_single_roster_join_start_once_and_hash_only_storage(self):
        created = self.create()
        game = self.supervisor.game(created["game_id"])
        self.assertEqual(game.state, "lobby")
        self.assertEqual(game.max_agents, 1)
        self.assertEqual(
            [place["controller"] for place in created["resolved_places"]],
            ["agent", "native_classic_ai"],
        )
        for generic in (None, "Agent", "HARNESS-MODEL", "Codex"):
            with self.subTest(generic=generic), self.assertRaises(
                APIProblem,
            ) as context:
                game.join(
                    created["join_token"], controller_label=generic,
                )
            self.assertEqual(context.exception.status, 400)
            self.assertIn("harness-model", str(context.exception))
        self.assertNotIn("start", game._setup_commands())
        joined = game.join(
            created["join_token"], controller_label="test-model",
        )
        self.assertEqual(joined["state"], "starting")
        self.assertEqual(game.start_count, 1)
        self.assertEqual(
            self.send_mock.call_args_list[-1].args[1], ["start"],
        )
        reconnected = game.join(joined["agent_token"])
        self.assertTrue(reconnected["reconnected"])
        self.assertEqual(reconnected["agent_id"], joined["agent_id"])
        self.assertEqual(game.start_count, 1)
        with self.assertRaises(APIProblem) as context:
            game.join(
                created["join_token"], controller_label="other-model",
            )
        self.assertEqual(context.exception.status, 409)
        artifact_text = "\n".join(
            path.read_text(errors="replace")
            for path in game.episode.rglob("*") if path.is_file()
        )
        self.assertNotIn(created["owner_token"], artifact_text)
        self.assertNotIn(created["join_token"], artifact_text)
        self.assertNotIn(joined["agent_token"], artifact_text)
        auth = json.loads((game.episode / "auth.json").read_text())
        self.assertEqual(len(auth["owner_token_sha256"]), 64)
        self.assertEqual(oct((game.episode / "auth.json").stat().st_mode & 0o777), "0o600")

    def test_multiplayer_collective_barrier_idempotency_and_stale_conflict(self):
        created = self.create(mode="multiplayer", places=2)
        game = self.supervisor.game(created["game_id"])
        first = game.join(
            created["join_token"], 1, controller_label="first-model",
        )
        second = game.join(
            created["join_token"], 2, controller_label="second-model",
        )
        self.assertEqual(game.start_count, 1)
        payload = {
            "schema_version": 1,
            "game_id": game.game_id,
            "turn": 1,
            "year": -4000,
            "observations": [
                observation("place-1"), observation("place-2"),
            ],
        }
        result = {}
        worker = threading.Thread(
            target=lambda: result.setdefault("response", game.process_turn(payload)),
        )
        worker.start()
        next_first = game.next_for_agent(first["agent_id"], 0, 1)
        next_second = game.next_for_agent(second["agent_id"], 0, 1)
        self.assertEqual(next_first["turn"], next_second["turn"])
        self.assertNotEqual(
            next_first["observation_id"], next_second["observation_id"],
        )
        status, accepted = game.submit_action(
            first["agent_id"],
            {
                "turn": 1,
                "observation_id": next_first["observation_id"],
                "action": ACTION,
                "telemetry": {"model": "first"},
            },
        )
        self.assertEqual(status, 202)
        status, retry = game.submit_action(
            first["agent_id"],
            {
                "turn": 1,
                "observation_id": next_first["observation_id"],
                "action": ACTION,
                "telemetry": {"model": "first"},
            },
        )
        self.assertEqual(status, 200)
        self.assertTrue(retry["idempotent"])
        game.submit_action(
            second["agent_id"],
            {
                "turn": 1,
                "observation_id": next_second["observation_id"],
                "action": ACTION,
                "telemetry": None,
            },
        )
        worker.join(2)
        self.assertFalse(worker.is_alive())
        self.assertEqual(len(result["response"]["actions"]), 2)
        self.assertEqual(game.process_turn(payload), result["response"])
        differing = {**payload, "year": -3999}
        differing["observations"] = [
            observation("place-1", year=-3999),
            observation("place-2", year=-3999),
        ]
        with self.assertRaises(APIProblem) as context:
            game.process_turn(differing)
        self.assertEqual(context.exception.status, 409)
        conflict = {
            "turn": 1,
            "observation_id": next_first["observation_id"],
            "action": {**ACTION, "traits": {**ACTION["traits"], "builder": 9}},
            "telemetry": {"model": "first"},
        }
        with self.assertRaises(APIProblem) as context:
            game.submit_action(first["agent_id"], conflict)
        self.assertEqual(context.exception.status, 409)
        with self.assertRaises(APIProblem) as context:
            game.submit_action(
                first["agent_id"],
                {
                    "turn": 2,
                    "observation_id": "obsolete",
                    "action": ACTION,
                    "telemetry": None,
                },
            )
        self.assertEqual(context.exception.status, 409)
        self.assertEqual(game.timeline[0]["timed_out_seats"], [])

    def test_barrier_redelivers_missing_seat_and_acknowledges_submitted_seat(self):
        created = self.create(
            mode="multiplayer", places=2,
            timing_mode="infinite", action_timeout_s=None,
        )
        game = self.supervisor.game(created["game_id"])
        first = game.join(
            created["join_token"], 1,
            controller_label="pi-gpt-5.6-sol",
            metadata={"model": "private-model-marker"},
        )
        second = game.join(
            created["join_token"], 2,
            controller_label="pi-claude-opus",
        )
        payload = {
            "schema_version": 1,
            "game_id": game.game_id,
            "turn": 1,
            "year": -4000,
            "observations": [
                {**observation("place-1"), "private_marker": "first-secret"},
                {**observation("place-2"), "private_marker": "second-secret"},
            ],
        }
        result = {}
        worker = threading.Thread(
            target=lambda: result.setdefault("response", game.process_turn(payload)),
        )
        worker.start()
        first_turn = game.next_for_agent(first["agent_id"], 0, 1)
        second_turn = game.next_for_agent(second["agent_id"], 0, 1)
        self.assertFalse(first_turn["action_received"])
        self.assertFalse(first_turn["redelivered"])
        self.assertEqual(first_turn["seats_remaining"], 2)

        barrier = game.status()["barrier"]
        self.assertIsNotNone(barrier)
        self.assertEqual(
            [row["state"] for row in barrier["controllers"]],
            ["thinking", "thinking"],
        )
        public_barrier = json.dumps(barrier, sort_keys=True)
        for private_value in (
            first_turn["observation_id"], second_turn["observation_id"],
            "first-secret", "second-secret", "private-model-marker",
            first["controller_fingerprint"], first["agent_token"],
        ):
            self.assertNotIn(private_value, public_barrier)
        for forbidden_key in (
            "observation", "observation_id", "action", "telemetry",
            "reasoning", "controller_metadata", "controller_fingerprint",
            "agent_id",
        ):
            self.assertNotIn(forbidden_key, barrier["controllers"][0])

        status, accepted = game.submit_action(
            first["agent_id"],
            {
                "turn": 1,
                "observation_id": first_turn["observation_id"],
                "action": ACTION,
                "telemetry": {"reasoning": "private-reasoning-marker"},
            },
        )
        self.assertEqual(status, HTTPStatus.ACCEPTED)
        self.assertEqual(accepted["status"], "accepted")
        self.assertTrue(accepted["action_received"])
        self.assertTrue(accepted["waiting_for_others"])
        self.assertEqual(accepted["seats_remaining"], 1)
        self.assertEqual(accepted["place"], 1)
        self.assertEqual(accepted["seat_id"], "place-1")
        self.assertEqual(accepted["controller_label"], "pi-gpt-5.6-sol")

        with patch(
            "agent_eval.supervisor.BARRIER_REMINDER_INTERVAL_S", 0.01,
        ):
            submitted_wait = game.next_for_agent(first["agent_id"], 1, 0.2)
        self.assertEqual(submitted_wait["state"], "waiting")
        self.assertTrue(submitted_wait["action_received"])
        self.assertTrue(submitted_wait["waiting_for_others"])
        self.assertEqual(submitted_wait["current_turn"], 1)
        self.assertEqual(submitted_wait["seats_remaining"], 1)
        self.assertIn("was received", submitted_wait["message"])

        # Reproduce the broken loop: it advanced after_turn even though this
        # authenticated seat never received an accepted action response.
        with game.condition:
            game.current_turn["published_at"] -= 31
        recovered = game.next_for_agent(second["agent_id"], 1, 0)
        self.assertEqual(recovered["observation_id"], second_turn["observation_id"])
        self.assertTrue(recovered["redelivered"])
        self.assertFalse(recovered["action_received"])
        self.assertIn("No action has been received", recovered["reminder"])

        status, second_accepted = game.submit_action(
            second["agent_id"],
            {
                "turn": 1,
                "observation_id": recovered["observation_id"],
                "action": ACTION,
                "telemetry": None,
            },
        )
        self.assertEqual(status, HTTPStatus.ACCEPTED)
        self.assertTrue(second_accepted["action_received"])
        self.assertFalse(second_accepted["waiting_for_others"])
        self.assertEqual(second_accepted["seats_remaining"], 0)
        worker.join(1)
        self.assertFalse(worker.is_alive())
        self.assertIsNone(game.status()["barrier"])

        status, retry = game.submit_action(
            first["agent_id"],
            {
                "turn": 1,
                "observation_id": first_turn["observation_id"],
                "action": ACTION,
                "telemetry": {"reasoning": "private-reasoning-marker"},
            },
        )
        self.assertEqual(status, HTTPStatus.OK)
        self.assertEqual(retry["status"], "already_accepted")
        self.assertTrue(retry["idempotent"])

    def test_timeout_holds_traits_and_marks_invalid_without_fallback(self):
        created = self.create(action_timeout_s=0.1)
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="timeout-model",
        )
        response = game.process_turn({
            "schema_version": 1,
            "game_id": game.game_id,
            "turn": 1,
            "year": -4000,
            "observations": [observation("place-1")],
        })
        self.assertEqual(response["actions"], [])
        self.assertEqual(response["timed_out_seats"], ["place-1"])
        self.assertFalse(response["benchmark_valid"])
        self.assertFalse(game.status()["benchmark_valid"])
        self.assertEqual(game.status()["outcome"]["status"], "pending")
        events = [
            json.loads(line)
            for line in (game.episode / "decisions.jsonl").read_text().splitlines()
            if json.loads(line).get("event") == "decision"
        ]
        self.assertIsNone(events[0]["action"])
        self.assertEqual(events[0]["source"], "external_timeout")
        self.assertFalse(events[0]["fallback"])
        self.assertNotIn("deterministic", (game.episode / "decisions.jsonl").read_text())
        after_timeout = game.next_for_agent(joined["agent_id"], 1, 0)
        self.assertEqual(after_timeout["state"], "waiting")
        self.assertIsNone(after_timeout["action_received"])
        self.assertNotIn("observation_id", after_timeout)

    def test_infinite_timing_waits_for_action_with_null_deadline_and_stays_valid(self):
        created = self.create(
            timing_mode="infinite", action_timeout_s=None,
        )
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-infinite-model",
        )
        payload = {
            "schema_version": 1,
            "game_id": game.game_id,
            "turn": 1,
            "year": -4000,
            "observations": [observation("place-1")],
        }
        result = {}
        worker = threading.Thread(
            target=lambda: result.setdefault("response", game.process_turn(payload)),
        )
        worker.start()
        deadline = time.monotonic() + 1
        with game.condition:
            while game.current_turn is None and time.monotonic() < deadline:
                game.condition.wait(0.01)
        self.assertIsNotNone(game.current_turn)
        worker.join(0.05)
        self.assertTrue(worker.is_alive())
        next_turn = game.next_for_agent(joined["agent_id"], 0, 0)
        self.assertEqual(next_turn["timing_mode"], "infinite")
        self.assertIsNone(next_turn["action_timeout_s"])
        self.assertIsNone(next_turn["deadline_at"])
        self.assertEqual(game._process_environment("internal")[
            "AGENT_EVAL_TURN_TIMEOUT_S"
        ], "0")

        status = game.status()
        self.assertEqual(status["timing_mode"], "infinite")
        self.assertIsNone(status["action_timeout_s"])
        self.assertEqual(game.picker_state()["timing_mode"], "infinite")
        self.assertEqual(
            game.watch_state()["game"]["timing_mode"], "infinite",
        )
        self.assertEqual(
            json.loads((game.episode / "manifest.json").read_text())[
                "config"
            ]["timing_mode"],
            "infinite",
        )
        self.assertEqual(joined["timing_mode"], "infinite")
        self.assertIsNone(joined["action_timeout_s"])
        self.assertEqual(created["timing_mode"], "infinite")
        self.assertIsNone(created["action_timeout_s"])

        game.submit_action(
            joined["agent_id"],
            {
                "turn": 1,
                "observation_id": next_turn["observation_id"],
                "action": ACTION,
                "telemetry": None,
            },
        )
        worker.join(1)
        self.assertFalse(worker.is_alive())
        self.assertEqual(result["response"]["timed_out_seats"], [])
        self.assertTrue(result["response"]["benchmark_valid"])
        self.assertEqual(game.invalid_reasons, [])

    def test_infinite_timing_barrier_wakes_on_owner_cancellation(self):
        created = self.create(
            timing_mode="infinite", action_timeout_s=None, seed=8,
        )
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-cancel-model",
        )
        result = {}
        worker = threading.Thread(target=lambda: result.setdefault(
            "response",
            game.process_turn({
                "schema_version": 1,
                "game_id": game.game_id,
                "turn": 1,
                "year": -4000,
                "observations": [observation("place-1")],
            }),
        ))
        worker.start()
        deadline = time.monotonic() + 1
        with game.condition:
            while game.current_turn is None and time.monotonic() < deadline:
                game.condition.wait(0.01)
        self.assertIsNotNone(game.current_turn)
        game.cancel()
        worker.join(1)
        self.assertFalse(worker.is_alive())
        self.assertEqual(result["response"]["timed_out_seats"], ["place-1"])
        self.assertFalse(result["response"]["benchmark_valid"])
        self.assertEqual(game.invalid_reasons, [])
        with game.condition:
            game.state = "cancelled"
            game.condition.notify_all()
        terminal = game.next_for_agent(joined["agent_id"], 1, 0)
        self.assertEqual(terminal["state"], "cancelled")
        self.assertEqual(terminal["seat_id"], "place-1")
        self.assertNotIn("observation_id", terminal)

    def test_controller_identity_is_stable_across_places_and_distinct_by_model(self):
        metadata = {"client": "codex", "model": "gpt-5.6"}
        first_created = self.create()
        first_game = self.supervisor.game(first_created["game_id"])
        first = first_game.join(
            first_created["join_token"], controller_label="codex-model-a",
            metadata=metadata,
        )
        first_fingerprint = first["controller_fingerprint"]

        second_created = self.create()
        second_game = self.supervisor.game(second_created["game_id"])
        second = second_game.join(
            second_created["join_token"], controller_label="claude-model-a",
            metadata={"client": "claude-code", "model": "claude-opus"},
        )
        self.assertNotEqual(
            first_fingerprint, second["controller_fingerprint"],
        )

        rotated_created = self.create(mode="multiplayer", places=2)
        rotated_game = self.supervisor.game(rotated_created["game_id"])
        rotated_game.join(
            rotated_created["join_token"], 1,
            controller_label="baseline-model", metadata={"model": "baseline"},
        )
        rotated = rotated_game.join(
            rotated_created["join_token"], 2,
            controller_label="codex-model-a", metadata=metadata,
        )
        self.assertEqual(
            first_fingerprint, rotated["controller_fingerprint"],
        )
        first_manifest = json.loads(
            (first_game.episode / "manifest.json").read_text()
        )
        rotated_manifest = json.loads(
            (rotated_game.episode / "manifest.json").read_text()
        )
        self.assertEqual(
            first_manifest["config"]["seats"][0]["controller_fingerprint"],
            rotated_manifest["config"]["seats"][1]["controller_fingerprint"],
        )
        self.assertEqual(
            first_game.status()["resolved_places"][0]["controller_label"],
            "codex-model-a",
        )
        trace = [
            json.loads(line)
            for line in (first_game.episode / "decisions.jsonl").read_text().splitlines()
        ]
        self.assertEqual(trace[0]["event"], "join")
        self.assertEqual(trace[0]["controller_label"], "codex-model-a")
        self.assertEqual(
            trace[0]["controller_fingerprint"], first_fingerprint,
        )
        with self.assertRaises(APIProblem):
            first_game.join(
                first["agent_token"], controller_label="Changed",
            )
        secret_created = self.create()
        secret_game = self.supervisor.game(secret_created["game_id"])
        with self.assertRaises(APIProblem):
            secret_game.join(
                secret_created["join_token"],
                metadata={"api_key": "must-not-store"},
            )

    def test_validation_and_policy_free_service_module(self):
        for bad in (float("nan"), float("inf")):
            with self.assertRaises(APIProblem):
                self.create(action_timeout_s=bad)
        with self.assertRaises(APIProblem):
            self.create(places=17)
        with self.assertRaises(APIProblem):
            self.create(ruleset="sandbox")
        source = Path("agent_eval/supervisor.py").read_text()
        self.assertNotIn("providers", source)
        self.assertNotIn("deterministic_action", source)

    def test_admin_owner_and_agent_auth_fail_closed(self):
        with self.assertRaises(APIProblem) as context:
            self.supervisor.authorize_admin(None)
        self.assertEqual(context.exception.status, 401)
        with self.assertRaises(APIProblem) as context:
            self.supervisor.authorize_admin("wrong")
        self.assertEqual(context.exception.status, 403)
        created = self.create()
        game = self.supervisor.game(created["game_id"])
        with self.assertRaises(APIProblem) as context:
            game.authorize_owner("wrong")
        self.assertEqual(context.exception.status, 403)
        joined = game.join(
            created["join_token"], controller_label="auth-model",
        )
        with self.assertRaises(APIProblem) as context:
            game.authenticate_agent(created["owner_token"])
        self.assertEqual(context.exception.status, 403)
        agent_id, agent = game.authenticate_agent(joined["agent_token"])
        self.assertEqual(agent_id, joined["agent_id"])
        self.assertEqual(agent["place"], 1)

    def test_private_session_file_mode(self):
        path = Path(self.directory.name) / "nested" / "agent-session.json"
        write_private_json(path, {"agent_token": "secret"})
        self.assertEqual(oct(path.stat().st_mode & 0o777), "0o600")
        self.assertEqual(load_private_json(path)["agent_token"], "secret")

    def test_agent_client_requires_accepted_identity_matching_ack(self):
        session = {
            "service_url": "http://127.0.0.1:8765",
            "game_id": "game_12345678901234567890",
            "agent_id": "agent-one",
            "agent_token": "secret",
            "place": 1,
            "seat_id": "place-1",
            "controller_label": "codex-test-model",
        }
        with patch(
            "agent_eval.client.request_json",
            return_value={"accepted": False},
        ), self.assertRaisesRegex(ClientError, "do not advance LAST_TURN"):
            submit_action(session, 1, "obs_one", ACTION)
        with patch(
            "agent_eval.client.request_json",
            return_value={
                "accepted": True,
                "game_id": session["game_id"],
                "agent_id": "another-agent",
                "turn": 1,
            },
        ), self.assertRaisesRegex(ClientError, "wrong agent_id"):
            submit_action(session, 1, "obs_one", ACTION)
        with patch(
            "agent_eval.client.request_json",
            return_value={
                "state": "running",
                "game_id": session["game_id"],
                "agent_id": "another-agent",
            },
        ), self.assertRaisesRegex(ClientError, "different agent seat"):
            next_turn(session, 0, 0)

    def test_controller_session_keys_survive_lossy_slug_collisions(self):
        labels = (
            "codex-model+a",
            "codex-model/a",
            "Codex-model-a",
            "codex-model-a",
        )
        keys = [controller_session_key(label) for label in labels]
        self.assertEqual(len(set(keys)), len(labels))
        self.assertTrue(all(key.startswith("codex-model-a-") for key in keys))
        self.assertTrue(all(len(key.rsplit("-", 1)[1]) == 12 for key in keys))
        self.assertEqual(
            controller_session_key(labels[0]),
            controller_session_key(labels[0]),
        )
        game_directory = Path(self.directory.name) / "games" / "game-test"
        paths = [
            write_private_json(
                game_directory / f"{key}.json", {"controller_label": label},
            )
            for label, key in zip(labels, keys)
        ]
        self.assertEqual(len(set(paths)), len(labels))
        self.assertEqual(len(list(game_directory.glob("*.json"))), len(labels))

    def test_public_result_redacts_absolute_episode_path(self):
        created = self.create()
        game = self.supervisor.game(created["game_id"])
        game.state = "completed"
        (game.episode / "report.json").write_text(
            json.dumps({
                "episode": str(game.episode),
                "manifest": {"status": "completed"},
                "score": {"players": []},
                "seat_stats": {},
            }),
            encoding="utf-8",
        )
        result = game.result()
        self.assertNotIn("episode", result)
        self.assertEqual(result["artifact_id"], game.game_id)
        self.assertNotIn(str(game.episode), json.dumps(result))

    def test_live_scoreboard_names_controller_and_separates_invalidity(self):
        created = self.create()
        game = self.supervisor.game(created["game_id"])
        game.join(
            created["join_token"], controller_label="pi-gpt-5.6-sol",
            metadata={"model": "gpt-5.6-sol"},
        )
        write_player_scores(
            game.episode / "score.log", 101,
            ("AgentPlace1", 136), ("NativePlace2", 94),
        )
        game.state = "invalid"
        game.invalid_reasons = ["turn 2 timed out waiting for place-1"]
        (game.episode / "report.json").write_text(
            json.dumps({"episode": str(game.episode), "score": {}}),
            encoding="utf-8",
        )

        status = game.status()
        self.assertEqual(
            [place["player_color"] for place in status["resolved_places"]],
            ["#0067A5", "#F38400"],
        )
        self.assertEqual(
            [row["controller_label"] for row in status["leaderboard"]],
            ["pi-gpt-5.6-sol", "Freeciv Classic AI"],
        )
        self.assertEqual(
            [row["player_color"] for row in status["leaderboard"]],
            ["#0067A5", "#F38400"],
        )
        setup_commands = game._setup_commands()
        self.assertIn("set plrcolormode PLR_SET", setup_commands)
        self.assertIn("playercolor AgentPlace1 0067A5", setup_commands)
        self.assertIn("playercolor NativePlace2 F38400", setup_commands)
        self.assertEqual(status["outcome"]["status"], "invalid")
        self.assertEqual(
            status["outcome"]["summary"],
            "No valid winner; pi-gpt-5.6-sol led by 42 at the last complete score",
        )
        self.assertFalse(status["benchmark_valid"])
        result = game.result()
        self.assertEqual(result["state"], "invalid")
        self.assertEqual(result["outcome"]["status"], "invalid")
        self.assertEqual(result["invalid_reasons"], game.invalid_reasons)
        html = game.watch_html()
        self.assertEqual(html, (VIEWER_DIST_ROOT / "index.html").read_text())
        self.assertNotIn(game.game_id, html)

        # A partial write never replaces the last complete snapshot with
        # transient zero scores.
        write_player_scores(
            game.episode / "score.log", 102, ("AgentPlace1", 1),
        )
        self.assertEqual(game.status()["leaderboard"][0]["score"], 136)

    def test_failed_game_reports_last_leader_not_a_winner(self):
        created = self.create()
        game = self.supervisor.game(created["game_id"])
        game.join(created["join_token"], controller_label="codex-model")
        write_player_scores(
            game.episode / "score.log", 4,
            ("AgentPlace1", 12), ("NativePlace2", 9),
        )
        game.state = "failed"
        outcome = game.status()["outcome"]
        self.assertEqual(outcome["status"], "invalid")
        self.assertEqual(
            outcome["summary"],
            "No valid winner; codex-model led by 3 at the last complete score",
        )

    def test_delplayer_advances_survivor_score_and_final_margin(self):
        created = self.create()
        game = self.supervisor.game(created["game_id"])
        game.join(
            created["join_token"], controller_label="codex-survivor-model",
        )
        write_player_scores(
            game.episode / "score.log", 2,
            ("AgentPlace1", 25), ("NativePlace2", 30),
        )
        self.assertEqual(
            game.status()["outcome"]["summary"],
            "Freeciv Classic AI leads by 5",
        )
        (game.episode / "score.log").write_text(
            """#FREECIV SCORELOG2 test
tag 0 score
turn 1 -4000 4000 BC
addplayer 1 0 AgentPlace1
addplayer 1 1 NativePlace2
data 1 0 0 10
data 1 0 1 12
turn 2 -3960 3960 BC
data 2 0 0 25
data 2 0 1 30
turn 3 -3920 3920 BC
delplayer 2 1
data 3 0 0 50
turn 4 -3880 3880 BC
data 4 0 0 70
""",
            encoding="utf-8",
        )
        game.state = "completed"
        status = game.status()
        self.assertEqual(
            [row["score"] for row in status["leaderboard"]], [70, 30],
        )
        self.assertEqual(
            [row["alive"] for row in status["leaderboard"]], [True, False],
        )
        self.assertEqual(
            [row["score_turn"] for row in status["leaderboard"]], [4, 2],
        )
        self.assertEqual(status["outcome"]["status"], "won")
        self.assertEqual(
            status["outcome"]["summary"],
            "codex-survivor-model won by 40",
        )
        self.assertEqual(status["outcome"]["score_turn"], 4)

    def test_terminal_without_scores_has_no_valid_winner(self):
        for state in ("completed", "invalid", "failed", "cancelled"):
            with self.subTest(state=state):
                created = self.create(seed={
                    "completed": 100, "invalid": 101,
                    "failed": 102, "cancelled": 103,
                }[state])
                game = self.supervisor.game(created["game_id"])
                game.state = state
                outcome = game.status()["outcome"]
                self.assertEqual(outcome["status"], "invalid")
                self.assertEqual(
                    outcome["summary"],
                    "No valid winner; no complete score snapshot is available",
                )
                self.assertEqual(outcome["leaders"], [])
                self.assertIsNone(outcome["margin"])
                self.assertIsNone(outcome["score_turn"])

    def test_observation_ids_are_safe_as_cli_option_values(self):
        created = self.create()
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="cli-test-model",
        )
        payload = {
            "schema_version": 1,
            "game_id": game.game_id,
            "turn": 1,
            "year": -4000,
            "observations": [observation("place-1")],
        }
        result = {}
        with patch(
            "agent_eval.supervisor.secrets.token_urlsafe",
            return_value="-starts-with-a-dash",
        ):
            worker = threading.Thread(
                target=lambda: result.setdefault(
                    "response", game.process_turn(payload),
                ),
            )
            worker.start()
            next_turn = game.next_for_agent(joined["agent_id"], 0, 1)
            self.assertEqual(
                next_turn["observation_id"], "obs_-starts-with-a-dash",
            )
            game.submit_action(
                joined["agent_id"],
                {
                    "turn": 1,
                    "observation_id": next_turn["observation_id"],
                    "action": ACTION,
                    "telemetry": None,
                },
            )
            worker.join(2)
        self.assertIn("response", result)

    def test_native_viewer_lease_signals_once_and_restores_timeout(self):
        class Process:
            def __init__(self):
                self.signals = []

            def poll(self):
                return None

            def send_signal(self, value):
                self.signals.append(value)

            def terminate(self):
                return None

            def wait(self, timeout=None):
                return 0

            def terminate(self):
                return None

            def wait(self, timeout=None):
                return 0

        created = self.create()
        game = self.supervisor.game(created["game_id"])
        game.state = "running"
        game.process = Process()
        with patch("agent_eval.supervisor.threading.Thread.start"):
            lease_result = game.request_native_viewer()
        self.assertEqual(len(game.process.signals), 1)
        self.assertEqual(lease_result["host"], "127.0.0.1")
        self.assertEqual(lease_result["port"], game.freeciv_port)
        self.assertNotIn("port", game.status())
        with self.assertRaises(APIProblem) as context:
            game.request_native_viewer()
        self.assertEqual(context.exception.status, 409)

        lease = game.native_viewer
        game.observed_timeout = 0
        game.observed_timeout_sequence = (
            game.native_timeout_override_sequence + 1
        )
        game.server_output_tail += (
            f"{lease['username']} has connected\n"
            f"Lost connection: {lease['username']}\n"
        )
        game._manage_native_viewer(lease)
        commands = [call.args[1] for call in self.send_mock.call_args_list]
        self.assertIn([f"observe {lease['username']}"], commands)
        self.assertIn(["set timeout 1"], commands)
        self.assertIn(["set timeout -1"], commands)
        self.assertIsNone(game.native_viewer)
        with self.assertRaises(APIProblem) as context:
            game.request_native_viewer()
        self.assertIn("retry in", str(context.exception))
        self.assertEqual(len(game.process.signals), 1)

        # A failed reset is fail-closed: never risk interpreting another
        # SIGINT as a request to terminate the server.
        stuck_lease = {"username": "Watch-stuck"}
        game.native_viewer = stuck_lease
        game.socket_polling_enabled = True
        self.send_mock.side_effect = SupervisorError("mock reset failure")
        game._restore_native_timeout(stuck_lease)
        self.assertTrue(game.socket_polling_enabled)
        with self.assertRaises(APIProblem) as context:
            game.request_native_viewer()
        self.assertIn("reset safely", str(context.exception))
        self.assertEqual(len(game.process.signals), 1)
        self.send_mock.side_effect = None
        game.socket_polling_enabled = False

        game.state = "completed"
        with self.assertRaises(APIProblem) as context:
            game.request_native_viewer()
        self.assertIn("replay", str(context.exception))

    def test_running_viewer_defers_signal_until_turn_http_response_finishes(self):
        class Process:
            def __init__(self):
                self.signals = []

            def poll(self):
                return None

            def send_signal(self, value):
                self.signals.append(value)

            def terminate(self):
                return None

            def wait(self, timeout=None):
                return 0

        created = self.create()
        game = self.supervisor.game(created["game_id"])
        game.state = "running"
        game.current_turn = {"resolved": False}
        game.process = Process()
        with patch.object(game, "_manage_native_viewer", return_value=None):
            connection = game.request_native_viewer()
        self.assertEqual(connection["activation_timeout_s"], 16)
        self.assertEqual(game.native_viewer["state"], "enabling_server")
        self.assertEqual(game.process.signals, [])
        game.current_turn["resolved"] = True
        game.native_viewer_turn_response_sent()
        game.current_turn = None
        deadline = time.monotonic() + 1
        while not game.process.signals and time.monotonic() < deadline:
            time.sleep(0.01)
        self.assertEqual(len(game.process.signals), 1)
        self.assertTrue(game.native_viewer["signal_sent"])
        game.release_native_viewer(connection["lease_id"])

    def test_infinite_turn_viewer_activation_wait_is_unbounded_and_releasable(self):
        class Process:
            def __init__(self):
                self.signals = []

            def poll(self):
                return None

            def send_signal(self, value):
                self.signals.append(value)

            def terminate(self):
                return None

            def wait(self, timeout=None):
                return 0

        created = self.create(
            timing_mode="infinite", action_timeout_s=None,
        )
        game = self.supervisor.game(created["game_id"])
        game.state = "running"
        game.current_turn = {"resolved": False}
        game.process = Process()
        with patch.object(
            game, "_schedule_native_viewer_activation_signal",
            return_value=None,
        ), patch.object(game, "_manage_native_viewer", return_value=None):
            connection = game.request_native_viewer()
        lease = game.native_viewer
        self.assertIsNone(connection["activation_timeout_s"])
        self.assertEqual(lease["state"], "enabling_server")

        manager = threading.Thread(
            target=Game._manage_native_viewer, args=(game, lease), daemon=True,
        )
        # A finite 195-second fallback would see this simulated clock jump and
        # fail immediately. Infinite mode must not consult an activation clock.
        with patch(
            "agent_eval.supervisor.time.monotonic",
            side_effect=[0.0, 1_000.0, 2_000.0],
        ):
            manager.start()
            time.sleep(0.05)
            self.assertTrue(manager.is_alive())
            self.assertEqual(lease["state"], "enabling_server")
            released = game.release_native_viewer(connection["lease_id"])
        manager.join(1)
        self.assertFalse(manager.is_alive())
        self.assertEqual(released["state"], "released")
        self.assertEqual(lease["state"], "released")
        self.assertFalse(game.native_viewer_status(lease["lease_id"])["active"])
        self.assertEqual(game.process.signals, [])
        game.current_turn = None

    def test_running_viewer_defers_in_cleared_turn_before_http_response_finishes(self):
        class Process:
            def __init__(self):
                self.signals = []

            def poll(self):
                return None

            def send_signal(self, value):
                self.signals.append(value)

            def terminate(self):
                return None

            def wait(self, timeout=None):
                return 0

        created = self.create()
        game = self.supervisor.game(created["game_id"])
        game.state = "running"
        game.current_turn = None
        game.process = Process()

        # process_turn has cleared current_turn, but its HTTP response has not
        # finished writing to Lua's synchronous curl yet.
        game.native_viewer_turn_response_started()
        with patch.object(game, "_manage_native_viewer", return_value=None):
            connection = game.request_native_viewer()
        self.assertEqual(game.native_turn_responses_in_flight, 1)
        self.assertEqual(game.native_viewer["state"], "enabling_server")
        self.assertEqual(game.process.signals, [])

        game.native_viewer_turn_response_sent()
        time.sleep(0.03)
        self.assertEqual(game.process.signals, [])
        game._record_server_output_line(
            "AGENT_EVAL_NATIVE_TURN_RESPONSE_DONE turn=1",
        )
        deadline = time.monotonic() + 1
        while not game.process.signals and time.monotonic() < deadline:
            time.sleep(0.01)
        self.assertEqual(game.native_turn_responses_in_flight, 0)
        self.assertEqual(len(game.process.signals), 1)
        self.assertTrue(game.native_viewer["signal_sent"])
        game.release_native_viewer(connection["lease_id"])

    def test_running_viewer_waits_for_bridge_marker_after_http_response(self):
        class Process:
            def __init__(self):
                self.signals = []

            def poll(self):
                return None

            def send_signal(self, value):
                self.signals.append(value)

            def terminate(self):
                return None

            def wait(self, timeout=None):
                return 0

        created = self.create()
        game = self.supervisor.game(created["game_id"])
        game.state = "running"
        game.current_turn = None
        old_marker_sequence = game._record_server_output_line(
            "AGENT_EVAL_NATIVE_TURN_RESPONSE_DONE turn=1",
        )
        final_turn = game.config["turns"]
        generation = game.native_viewer_turn_response_started()
        game.native_viewer_turn_response_identified(generation, final_turn)
        game.native_viewer_turn_response_sent()
        game.process = Process()

        with patch.object(game, "_manage_native_viewer", return_value=None):
            connection = game.request_native_viewer()
        self.assertEqual(game.process.signals, [])
        self.assertTrue(game.native_viewer["activation_signal_scheduled"])

        time.sleep(0.08)
        self.assertEqual(game.process.signals, [])
        marker_sequence = game._record_server_output_line(
            f"2: AGENT_EVAL_NATIVE_TURN_RESPONSE_DONE turn={final_turn}",
        )
        deadline = time.monotonic() + 1
        while not game.process.signals and time.monotonic() < deadline:
            time.sleep(0.01)
        self.assertEqual(len(game.process.signals), 1)
        self.assertEqual(game.native_turn_response_completed_generation, generation)
        self.assertGreater(marker_sequence, old_marker_sequence)
        self.assertEqual(game.native_turn_response_marker_sequence, marker_sequence)
        self.assertEqual(game.native_turn_response_marker_turn, final_turn)
        self.assertTrue(game.native_viewer["signal_sent"])
        game.release_native_viewer(connection["lease_id"])

    def test_releasing_while_enabling_aborts_deferred_signal(self):
        class Process:
            def __init__(self):
                self.signals = []

            def poll(self):
                return None

            def send_signal(self, value):
                self.signals.append(value)

            def terminate(self):
                return None

            def wait(self, timeout=None):
                return 0

        created = self.create()
        game = self.supervisor.game(created["game_id"])
        game.state = "running"
        game.current_turn = None
        generation = game.native_viewer_turn_response_started()
        game.native_viewer_turn_response_identified(generation, 3)
        game.process = Process()

        with patch.object(game, "_manage_native_viewer", return_value=None):
            connection = game.request_native_viewer()
        lease = game.native_viewer
        self.assertEqual(lease["state"], "enabling_server")

        released = game.release_native_viewer(connection["lease_id"])
        self.assertEqual(released["state"], "released")
        self.assertEqual(lease["state"], "released")
        self.assertFalse(game.native_viewer_status(lease["lease_id"])["active"])
        self.assertTrue(lease["timeout_restored"])

        game.native_viewer_turn_response_sent()
        game._record_server_output_line(
            "AGENT_EVAL_NATIVE_TURN_RESPONSE_DONE turn=3",
        )
        time.sleep(0.05)
        self.assertEqual(game.process.signals, [])

    def test_native_viewer_lobby_lease_uses_timeout_and_reopens_without_signal(self):
        class Process:
            def __init__(self):
                self.signals = []

            def poll(self):
                return None

            def send_signal(self, value):
                self.signals.append(value)

            def terminate(self):
                return None

            def wait(self, timeout=None):
                return 0

        created = self.create()
        game = self.supervisor.game(created["game_id"])
        game.process = Process()

        with patch("agent_eval.supervisor.threading.Thread.start"):
            first = game.request_native_viewer()
        first_lease = game.native_viewer
        self.assertEqual(first_lease["activation_mode"], "lobby_timeout")
        self.assertEqual(game.process.signals, [])
        self.assertIsNone(game.last_native_viewer_sigint_at)
        self.assertTrue(game.socket_polling_enabled)

        released = game.release_native_viewer(first["lease_id"])
        self.assertTrue(released["timeout_restored"])
        self.assertFalse(game.socket_polling_enabled)

        # Lobby activation never sends SIGINT, so the running-state signal
        # guard must not delay a safe immediate reopen.
        with patch("agent_eval.supervisor.threading.Thread.start"):
            second = game.request_native_viewer()
        self.assertEqual(game.process.signals, [])
        self.assertEqual(game.native_viewer["activation_mode"], "lobby_timeout")
        game.release_native_viewer(second["lease_id"])

        commands = [call.args[1] for call in self.send_mock.call_args_list]
        self.assertEqual(commands.count(["set timeout 0"]), 2)
        self.assertEqual(commands.count(["set timeout -1"]), 2)

        # Once the zero-timeout write is attempted, a prompt/reset failure is
        # fail-closed rather than risking a later unsafe activation.
        self.send_mock.side_effect = SupervisorError("mock console failure")
        try:
            with self.assertRaises(APIProblem) as context:
                game.request_native_viewer()
            self.assertIn("could not enable", str(context.exception))
            self.assertTrue(game.socket_polling_enabled)
            self.assertEqual(game.process.signals, [])
            with self.assertRaises(APIProblem) as context:
                game.request_native_viewer()
            self.assertIn("reset safely", str(context.exception))
        finally:
            self.send_mock.side_effect = None
            game.socket_polling_enabled = False

    def test_native_viewer_stale_timeout_state_self_heals_only_from_newer_reset(self):
        class Process:
            def __init__(self):
                self.signals = []

            def poll(self):
                return None

            def send_signal(self, value):
                self.signals.append(value)

            def terminate(self):
                return None

            def wait(self, timeout=None):
                return 0

        created = self.create()
        game = self.supervisor.game(created["game_id"])
        game.process = Process()
        game.socket_polling_enabled = True
        game.native_timeout_override_sequence = 5

        # The setup-time timeout=-1 predates the viewer activation and cannot
        # prove that the later override was restored.
        game.observed_timeout = -1
        game.observed_timeout_sequence = 5
        with self.assertRaises(APIProblem) as context:
            game.request_native_viewer()
        self.assertIn("reset safely", str(context.exception))

        # A fresh ordered acknowledgement after activation safely repairs the
        # stale in-memory flag without another signal or server mutation.
        game.observed_timeout_sequence = 6
        with patch("agent_eval.supervisor.threading.Thread.start"):
            connection = game.request_native_viewer()
        self.assertFalse(game.process.signals)
        self.assertEqual(game.native_viewer["activation_mode"], "lobby_timeout")
        game.release_native_viewer(connection["lease_id"])

    def test_native_viewer_stale_timeout_state_does_not_heal_from_zero_or_unknown(self):
        class Process:
            def poll(self):
                return None

            def terminate(self):
                return None

            def wait(self, timeout=None):
                return 0

        created = self.create()
        game = self.supervisor.game(created["game_id"])
        game.process = Process()
        game.socket_polling_enabled = True
        game.native_timeout_override_sequence = 3
        for observed, sequence in ((None, 0), (0, 4), (1, 5)):
            game.observed_timeout = observed
            game.observed_timeout_sequence = sequence
            with self.assertRaises(APIProblem) as context:
                game.request_native_viewer()
            self.assertIn("reset safely", str(context.exception))

    def test_lobby_viewer_activation_precedes_concurrent_last_join_start(self):
        class Process:
            def __init__(self):
                self.signals = []

            def poll(self):
                return None

            def send_signal(self, value):
                self.signals.append(value)

            def terminate(self):
                return None

            def wait(self, timeout=None):
                return 0

        created = self.create()
        game = self.supervisor.game(created["game_id"])
        game.process = Process()
        timeout_entered = threading.Event()
        allow_timeout = threading.Event()
        results = {}

        def send_commands(_game, commands, **_kwargs):
            if commands == ["set timeout 0"]:
                timeout_entered.set()
                if not allow_timeout.wait(2):
                    raise AssertionError("test did not release timeout command")

        self.send_mock.side_effect = send_commands
        try:
            with patch.object(game, "_manage_native_viewer", return_value=None):
                viewer_thread = threading.Thread(
                    target=lambda: results.setdefault(
                        "viewer", game.request_native_viewer(),
                    ),
                )
                viewer_thread.start()
                self.assertTrue(timeout_entered.wait(1))

                join_thread = threading.Thread(
                    target=lambda: results.setdefault(
                        "join",
                        game.join(
                            created["join_token"],
                            controller_label="race-test-model",
                        ),
                    ),
                )
                join_thread.start()

                deadline = time.monotonic() + 1
                while time.monotonic() < deadline:
                    with game.condition:
                        if game.place_agents:
                            break
                    time.sleep(0.005)
                self.assertTrue(game.place_agents)
                self.assertTrue(join_thread.is_alive())
                self.assertEqual(game.start_count, 0)

                allow_timeout.set()
                viewer_thread.join(2)
                join_thread.join(2)
                self.assertFalse(viewer_thread.is_alive())
                self.assertFalse(join_thread.is_alive())
        finally:
            allow_timeout.set()
            self.send_mock.side_effect = None

        commands = [call.args[1] for call in self.send_mock.call_args_list]
        self.assertLess(
            commands.index(["set timeout 0"]), commands.index(["start"]),
        )
        self.assertEqual(game.process.signals, [])
        self.assertEqual(results["join"]["state"], "starting")
        game.release_native_viewer(results["viewer"]["lease_id"])

    def test_native_viewer_release_is_scoped_idempotent_and_blocks_late_observe(self):
        class Process:
            def __init__(self):
                self.signals = []

            def poll(self):
                return None

            def send_signal(self, value):
                self.signals.append(value)

            def terminate(self):
                return None

            def wait(self, timeout=None):
                return 0

        created = self.create()
        game = self.supervisor.game(created["game_id"])
        game.state = "running"
        game.process = Process()
        with patch("agent_eval.supervisor.threading.Thread.start"):
            connection = game.request_native_viewer()
        lease = game.native_viewer
        game.server_output_tail += f"{lease['username']} has connected\n"

        stale = game.release_native_viewer("viewer_stale")
        self.assertEqual(stale["state"], "stale_lease")
        self.assertIs(game.native_viewer, lease)
        released = game.release_native_viewer(connection["lease_id"])
        self.assertTrue(released["released"])
        self.assertTrue(released["timeout_restored"])
        self.assertIsNone(game.native_viewer)
        game._manage_native_viewer(lease)
        commands = [call.args[1] for call in self.send_mock.call_args_list]
        self.assertNotIn([f"observe {lease['username']}"], commands)
        self.assertEqual(commands.count(["set timeout -1"]), 1)
        again = game.release_native_viewer(connection["lease_id"])
        self.assertEqual(again["state"], "inactive")
        with self.assertRaises(APIProblem) as context:
            game.request_native_viewer()
        self.assertIn("retry in", str(context.exception))
        self.assertEqual(len(game.process.signals), 1)

    def test_native_viewer_http_route_requires_owner_bearer(self):
        class Process:
            def __init__(self):
                self.signals = []

            def poll(self):
                return None

            def send_signal(self, value):
                self.signals.append(value)

            def terminate(self):
                return None

            def wait(self, timeout=None):
                return 0

        server = make_supervisor_server(self.supervisor, "127.0.0.1", 0)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            created = self.create(action_timeout_s=0.1)
            game = self.supervisor.game(created["game_id"])
            game.process = Process()
            with self.assertRaises(ClientError) as context:
                request_native_viewer(
                    self.supervisor.service_url, game.game_id, "wrong",
                )
            self.assertEqual(context.exception.status, 403)
            value = request_native_viewer(
                self.supervisor.service_url,
                game.game_id,
                created["owner_token"],
            )
            self.assertEqual(value["game_id"], game.game_id)
            self.assertTrue(value["local_only"])
            self.assertEqual(game.state, "lobby")
            self.assertEqual(game.process.signals, [])
            self.assertEqual(
                game.native_viewer["activation_mode"], "lobby_timeout",
            )
            with self.assertRaises(ClientError) as context:
                native_viewer_status(
                    self.supervisor.service_url, game.game_id, "wrong",
                    value["lease_id"],
                )
            self.assertEqual(context.exception.status, 403)
            viewer_status = native_viewer_status(
                self.supervisor.service_url, game.game_id,
                created["owner_token"], value["lease_id"],
            )
            self.assertEqual(viewer_status["state"], "waiting_for_client")
            self.assertTrue(viewer_status["active"])
            with self.assertRaises(ClientError) as context:
                release_native_viewer(
                    self.supervisor.service_url, game.game_id, "wrong",
                    value["lease_id"],
                )
            self.assertEqual(context.exception.status, 403)
            released = release_native_viewer(
                self.supervisor.service_url, game.game_id,
                created["owner_token"], value["lease_id"],
            )
            self.assertTrue(released["released"])
            viewer_status = native_viewer_status(
                self.supervisor.service_url, game.game_id,
                created["owner_token"], value["lease_id"],
            )
            self.assertEqual(viewer_status["state"], "released")
            self.assertFalse(viewer_status["active"])
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)

    def test_unauthorized_internal_post_cannot_wedge_native_activation(self):
        class Process:
            def __init__(self):
                self.signals = []

            def poll(self):
                return None

            def send_signal(self, value):
                self.signals.append(value)

            def terminate(self):
                return None

            def wait(self, timeout=None):
                return 0

        server = make_supervisor_server(self.supervisor, "127.0.0.1", 0)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            created = self.create()
            game = self.supervisor.game(created["game_id"])
            request = urllib.request.Request(
                self.supervisor.service_url
                + f"/internal/v1/games/{game.game_id}/turns",
                data=b"{}",
                headers={
                    "Authorization": "Bearer wrong-internal-token",
                    "Content-Type": "application/json",
                },
                method="POST",
            )
            with self.assertRaises(urllib.error.HTTPError) as context:
                urllib.request.urlopen(request, timeout=2)
            self.assertEqual(context.exception.code, 403)
            context.exception.close()
            self.assertEqual(game.native_turn_response_generation, 0)
            self.assertEqual(game.native_turn_response_pending, {})

            game.state = "running"
            game.current_turn = None
            game.process = Process()
            with patch.object(game, "_manage_native_viewer", return_value=None):
                connection = game.request_native_viewer()
            self.assertEqual(len(game.process.signals), 1)
            self.assertTrue(game.native_viewer["signal_sent"])
            game.release_native_viewer(connection["lease_id"])
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)

    def test_public_and_internal_service_urls_are_separate(self):
        server = make_supervisor_server(
            self.supervisor, "127.0.0.1", 0,
            "https://games.example.test/base",
        )
        try:
            created = self.create()
            game = self.supervisor.game(created["game_id"])
            environment = game._process_environment("internal-secret")
            self.assertTrue(
                environment["AGENT_EVAL_TURN_URL"].startswith(
                    "http://127.0.0.1:"
                )
            )
            self.assertNotIn(
                "games.example.test", environment["AGENT_EVAL_TURN_URL"],
            )
            self.assertTrue(
                game.urls()["watch_url"].startswith(
                    "https://games.example.test/base/"
                )
            )
            self.assertTrue(
                game.urls()["replay_url"].startswith(
                    "https://games.example.test/base/"
                )
            )
            self.assertEqual(
                game.watch_html(),
                (VIEWER_DIST_ROOT / "index.html").read_text(),
            )
            self.assertNotIn(game.game_id, game.watch_html())
        finally:
            server.server_close()

    def test_public_games_index_is_newest_first_and_picker_safe(self):
        first_created = self.create(seed=201)
        first = self.supervisor.game(first_created["game_id"])
        first.created_at = 100.0
        first.join(
            first_created["join_token"], controller_label="codex-picker-model",
            metadata={"model": "picker-model"},
        )
        first.state = "running"
        first.latest_turn = {"turn": 7}
        write_player_scores(
            first.episode / "score.log", 7,
            ("AgentPlace1", 42), ("NativePlace2", 31),
        )
        second_created = self.create(mode="multiplayer", places=3, seed=202)
        second = self.supervisor.game(second_created["game_id"])
        second.created_at = 200.0

        payload = self.supervisor.games_index()

        self.assertEqual(payload["schema_version"], 1)
        self.assertEqual(
            [row["game_id"] for row in payload["games"]],
            [second.game_id, first.game_id],
        )
        row = payload["games"][1]
        self.assertEqual(set(row), {
            "game_id", "state", "created_at", "current_turn", "turns",
            "benchmark_valid", "mode", "timing_mode", "action_timeout_s",
            "places", "max_agents",
            "joined_agents", "resolved_places", "leaderboard", "outcome",
            "watch_path",
        })
        self.assertEqual(row["current_turn"], 7)
        self.assertEqual(row["mode"], "single")
        self.assertEqual(row["timing_mode"], "custom")
        self.assertEqual(row["action_timeout_s"], 1)
        self.assertEqual(row["places"], 2)
        self.assertEqual(row["max_agents"], 1)
        self.assertEqual(row["joined_agents"], 1)
        self.assertEqual(row["watch_path"], f"/watch/{first.game_id}")
        self.assertEqual(
            [place["controller_label"] for place in row["resolved_places"]],
            ["codex-picker-model", "Freeciv Classic AI"],
        )
        self.assertEqual(
            [place["player_color"] for place in row["resolved_places"]],
            ["#0067A5", "#F38400"],
        )
        self.assertEqual(
            [place["controller"] for place in row["resolved_places"]],
            ["agent", "native_classic_ai"],
        )
        self.assertEqual(
            [place["joined"] for place in row["resolved_places"]],
            [True, False],
        )
        self.assertEqual(row["outcome"]["status"], "leads")
        public_text = json.dumps(payload)
        for secret in (
            first_created["owner_token"], first_created["join_token"],
            second_created["owner_token"], second_created["join_token"],
            str(first.episode), str(second.episode),
        ):
            self.assertNotIn(secret, public_text)
        self.assertNotIn("controller_metadata", public_text)
        self.assertNotIn("replay", public_text)

    def test_games_index_releases_registry_and_turn_locks_before_scores(self):
        created = self.create(seed=203)
        game = self.supervisor.game(created["game_id"])
        entered = threading.Event()
        release = threading.Event()
        result = {}

        def slow_scores():
            entered.set()
            release.wait(2)
            return []

        with patch.object(game, "_leaderboard", side_effect=slow_scores):
            worker = threading.Thread(
                target=lambda: result.setdefault(
                    "payload", self.supervisor.games_index(),
                ),
            )
            worker.start()
            self.assertTrue(entered.wait(1))
            self.assertTrue(game.condition.acquire(timeout=0.2))
            game.condition.release()
            self.assertTrue(self.supervisor.lock.acquire(timeout=0.2))
            self.supervisor.lock.release()
            release.set()
            worker.join(2)
        self.assertFalse(worker.is_alive())
        self.assertEqual(result["payload"]["games"][0]["game_id"], game.game_id)

    def test_http_exposes_public_games_picker_index(self):
        server = make_supervisor_server(self.supervisor, "127.0.0.1", 0)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            created = self.create(seed=204)
            with urllib.request.urlopen(
                f"{self.supervisor.service_url}/v1/games", timeout=2,
            ) as response:
                payload = json.loads(response.read())
                self.assertEqual(response.status, 200)
                self.assertEqual(
                    response.headers["Cache-Control"], "no-store",
                )
            self.assertEqual(payload["schema_version"], 1)
            self.assertEqual(payload["games"][0]["game_id"], created["game_id"])
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)

    def test_internal_url_uses_specific_bind_and_brackets_ipv6(self):
        def fake_server_init(server, address, handler):
            server.server_address = address

        with patch.object(
            ThreadingHTTPServer, "__init__", autospec=True,
            side_effect=fake_server_init,
        ):
            server = SupervisorHTTPServer(
                ("192.0.2.40", 4567), self.supervisor,
            )
            self.assertEqual(
                self.supervisor.internal_service_url,
                "http://192.0.2.40:4567",
            )
            self.assertEqual(
                self.supervisor.service_url, "http://192.0.2.40:4567",
            )
            server = SupervisorHTTPServer(
                ("0.0.0.0", 4568), self.supervisor,
            )
            self.assertEqual(
                self.supervisor.internal_service_url,
                "http://127.0.0.1:4568",
            )
            server = SupervisorHTTPServer(
                ("2001:db8::40", 4569), self.supervisor,
            )
            self.assertEqual(
                self.supervisor.internal_service_url,
                "http://[2001:db8::40]:4569",
            )
            self.assertEqual(server.address_family, socket.AF_INET6)
            SupervisorHTTPServer(("::", 4570), self.supervisor)
            self.assertEqual(
                self.supervisor.internal_service_url,
                "http://[::1]:4570",
            )

    def test_no_authoritative_turn_cannot_complete_valid(self):
        class FinishedProcess:
            def __init__(self):
                self.stdin = io.BytesIO()

            def poll(self):
                return 0

            def wait(self):
                return 0

        created = self.create()
        game = self.supervisor.game(created["game_id"])
        game.process = FinishedProcess()
        game.state = "starting"
        write_scorelog(game.episode / "score.log", 1, 2, 3)
        with patch(
            "agent_eval.supervisor.summarize_episode",
            return_value={"manifest": {}, "score": {"players": []}},
        ):
            game._monitor()
        self.assertEqual(game.state, "invalid")
        self.assertFalse(game.status()["benchmark_valid"])
        self.assertIn("bridge_no_turns", game.invalid_reasons)

    def test_bridge_journal_accepts_contiguous_early_terminal_lifecycle(self):
        path = Path(self.directory.name) / "early-terminal.jsonl"
        path.write_text(
            '{"event":"begin","turn":1}\n'
            '{"event":"ok","turn":1}\n',
            encoding="utf-8",
        )
        scorelog = Path(self.directory.name) / "early-score.log"
        write_scorelog(scorelog, 1, 2)
        self.assertEqual(validate_bridge_journal(path, [1], scorelog), [])

    def test_bridge_journal_fails_closed_on_malformed_scorelog_headers(self):
        journal = Path(self.directory.name) / "malformed-score-journal.jsonl"
        journal.write_text(
            '{"event":"begin","turn":1}\n'
            '{"event":"ok","turn":1}\n',
            encoding="utf-8",
        )
        scorelog = Path(self.directory.name) / "malformed-score.log"
        scorelog.write_text(
            "#FREECIV SCORELOG2 test\nturn one 0 year\n",
            encoding="utf-8",
        )
        reasons = validate_bridge_journal(journal, [1], scorelog)
        self.assertIn("bridge_scorelog_malformed", reasons)
        self.assertIn("bridge_scorelog_turns_missing", reasons)

    def test_monitor_allows_clean_contiguous_early_terminal(self):
        class FinishedProcess:
            def __init__(self):
                self.stdin = io.BytesIO()

            def poll(self):
                return 0

            def wait(self):
                return 0

        created = self.create(turns=2)
        game = self.supervisor.game(created["game_id"])
        game.process = FinishedProcess()
        game.state = "running"
        game.timeline.append({"turn": 1})
        game.bridge_status_path.write_text(
            '{"event":"begin","turn":1}\n'
            '{"event":"ok","turn":1}\n',
            encoding="utf-8",
        )
        write_complete_scorelog(
            game.episode / "score.log", (1, 2),
            ("AgentPlace1", 20), ("NativePlace2", 10),
        )
        with patch(
            "agent_eval.supervisor.summarize_episode",
            return_value={"manifest": {}, "score": {"players": []}},
        ):
            game._monitor()
        self.assertEqual(game.state, "completed")
        self.assertTrue(game.status()["benchmark_valid"])

    def test_monitor_accepts_turn_limit_scorelog_reconciliation(self):
        class FinishedProcess:
            def __init__(self):
                self.stdin = io.BytesIO()

            def poll(self):
                return 0

            def wait(self):
                return 0

        created = self.create(turns=2)
        game = self.supervisor.game(created["game_id"])
        game.process = FinishedProcess()
        game.state = "running"
        game.timeline.extend(({"turn": 1}, {"turn": 2}))
        game.bridge_status_path.write_text(
            '{"event":"begin","turn":1}\n'
            '{"event":"ok","turn":1}\n'
            '{"event":"begin","turn":2}\n'
            '{"event":"ok","turn":2}\n',
            encoding="utf-8",
        )
        write_complete_scorelog(
            game.episode / "score.log", (1, 2, 3),
            ("AgentPlace1", 30), ("NativePlace2", 15),
        )
        with patch(
            "agent_eval.supervisor.summarize_episode",
            return_value={"manifest": {}, "score": {"players": []}},
        ):
            game._monitor()
        self.assertEqual(game.state, "completed")
        self.assertTrue(game.status()["benchmark_valid"])

    def test_monitor_invalidates_completed_process_with_incomplete_scores(self):
        class FinishedProcess:
            def __init__(self):
                self.stdin = io.BytesIO()

            def poll(self):
                return 0

            def wait(self):
                return 0

        created = self.create(turns=2)
        game = self.supervisor.game(created["game_id"])
        game.join(
            created["join_token"], controller_label="codex-incomplete-score",
        )
        game.process = FinishedProcess()
        game.state = "running"
        game.timeline.append({"turn": 1})
        game.bridge_status_path.write_text(
            '{"event":"begin","turn":1}\n'
            '{"event":"ok","turn":1}\n',
            encoding="utf-8",
        )
        write_complete_scorelog(
            game.episode / "score.log", (1, 2),
            ("AgentPlace1", 99),
        )

        game._monitor()

        self.assertEqual(game.state, "invalid")
        self.assertIn("score_snapshot_incomplete", game.invalid_reasons)
        status = game.status()
        self.assertFalse(status["benchmark_valid"])
        self.assertEqual(status["outcome"]["status"], "invalid")
        manifest = json.loads((game.episode / "manifest.json").read_text())
        self.assertEqual(manifest["status"], "invalid")
        self.assertFalse(manifest["benchmark_valid"])
        self.assertIn(
            "score_snapshot_incomplete", manifest["invalid_reasons"],
        )
        summary = json.loads((game.episode / "report.json").read_text())
        self.assertFalse(summary["manifest"]["benchmark_valid"])
        board = aggregate_leaderboard([summary])
        self.assertTrue(board)
        self.assertTrue(all(row["valid_episodes"] == 0 for row in board))
        self.assertTrue(all(row["wins"] == 0 for row in board))
        result = game.result()
        self.assertEqual(result["state"], "invalid")
        self.assertFalse(result["benchmark_valid"])
        self.assertIn(
            "score_snapshot_incomplete", result["invalid_reasons"],
        )
        self.assertEqual(result["outcome"]["status"], "invalid")

    def test_monitor_rejects_silent_scorelog_tail(self):
        class FinishedProcess:
            def __init__(self):
                self.stdin = io.BytesIO()

            def poll(self):
                return 0

            def wait(self):
                return 0

        created = self.create(turns=2)
        game = self.supervisor.game(created["game_id"])
        game.process = FinishedProcess()
        game.state = "running"
        game.timeline.append({"turn": 1})
        game.bridge_status_path.write_text(
            '{"event":"begin","turn":1}\n'
            '{"event":"ok","turn":1}\n',
            encoding="utf-8",
        )
        write_complete_scorelog(
            game.episode / "score.log", (1, 2, 3),
            ("AgentPlace1", 30), ("NativePlace2", 15),
        )
        with patch(
            "agent_eval.supervisor.summarize_episode",
            return_value={"manifest": {}, "score": {"players": []}},
        ):
            game._monitor()
        self.assertEqual(game.state, "invalid")
        self.assertFalse(game.status()["benchmark_valid"])
        self.assertIn("bridge_scorelog_ok_mismatch", game.invalid_reasons)
        self.assertIn(
            "bridge_scorelog_timeline_mismatch", game.invalid_reasons,
        )

    def test_bridge_journal_rejects_partial_error_after_success(self):
        path = Path(self.directory.name) / "partial-error.jsonl"
        path.write_text(
            '{"event":"begin","turn":1}\n'
            '{"event":"ok","turn":1}\n'
            '{"event":"begin","turn":2}\n'
            '{"event":"error","turn":2,"message":"transport failed"}\n',
            encoding="utf-8",
        )
        scorelog = Path(self.directory.name) / "partial-score.log"
        write_scorelog(scorelog, 1, 2, 3)
        reasons = validate_bridge_journal(path, [1], scorelog)
        self.assertTrue(any(
            reason.startswith("bridge_callback_error:turn=2:")
            for reason in reasons
        ))

    def test_close_waits_for_inflight_create_and_prevents_registration(self):
        class LiveProcess:
            def __init__(self):
                self.returncode = None
                self.terminated = False

            def poll(self):
                return self.returncode

            def terminate(self):
                self.terminated = True
                self.returncode = -15

            def kill(self):
                self.returncode = -9

            def wait(self, timeout=None):
                return self.returncode

        process = LiveProcess()
        entered = threading.Event()
        release = threading.Event()
        create_result = {}

        def slow_launch(game, internal_token):
            game.process = process
            entered.set()
            release.wait(2)

        self.launch_mock.side_effect = slow_launch

        def create_game():
            try:
                create_result["created"] = self.create(seed=9)
            except Exception as exc:
                create_result["error"] = exc

        creator = threading.Thread(target=create_game)
        creator.start()
        self.assertTrue(entered.wait(1))
        closer = threading.Thread(target=self.supervisor.close)
        closer.start()
        time.sleep(0.05)
        self.assertTrue(closer.is_alive())
        release.set()
        creator.join(2)
        closer.join(2)
        self.assertFalse(creator.is_alive())
        self.assertFalse(closer.is_alive())
        self.assertTrue(process.terminated)
        self.assertEqual(self.supervisor.games, {})
        self.assertEqual(self.supervisor.reserved_game_ids, set())
        self.assertIsInstance(create_result.get("error"), APIProblem)
        self.assertEqual(create_result["error"].status, 503)
        with self.assertRaises(APIProblem) as context:
            self.create(seed=10)
        self.assertEqual(context.exception.status, 503)

    def test_slow_create_does_not_block_existing_game_lookup(self):
        existing_created = self.create()
        existing = self.supervisor.game(existing_created["game_id"])
        entered = threading.Event()
        release = threading.Event()
        result = {}

        def slow_launch(game, internal_token):
            entered.set()
            release.wait(2)

        self.launch_mock.side_effect = slow_launch

        def create_slow_game():
            try:
                result["created"] = self.create(seed=8)
            except Exception as exc:
                result["error"] = exc

        worker = threading.Thread(target=create_slow_game)
        worker.start()
        self.assertTrue(entered.wait(1))
        fail_safe = threading.Timer(0.75, release.set)
        fail_safe.start()
        started = time.monotonic()
        looked_up = self.supervisor.game(existing.game_id)
        elapsed = time.monotonic() - started
        release.set()
        fail_safe.cancel()
        worker.join(2)
        self.assertIs(looked_up, existing)
        self.assertLess(elapsed, 0.25)
        self.assertFalse(worker.is_alive())
        self.assertNotIn("error", result)
        self.assertIn(result["created"]["game_id"], self.supervisor.games)
        self.assertEqual(self.supervisor.reserved_game_ids, set())

    def test_watch_page_is_the_committed_react_entrypoint(self):
        created = self.create()
        game = self.supervisor.game(created["game_id"])
        html = game.watch_html()
        self.assertEqual(html, (VIEWER_DIST_ROOT / "index.html").read_text())
        self.assertIn('<div id="root"></div>', html)
        self.assertIn('/viewer/assets/', html)
        self.assertNotIn(game.game_id, html)
        self.assertNotIn("owner_token", html)

    def test_watch_filesystem_work_does_not_hold_the_turn_condition(self):
        created = self.create()
        game = self.supervisor.game(created["game_id"])
        lock_states = []

        def frames():
            lock_states.append(game.condition._is_owned())
            return {"frames": []}

        def replay():
            lock_states.append(game.condition._is_owned())
            return {"available": False}

        with (
            patch.object(game, "frame_manifest", side_effect=frames),
            patch.object(game, "_replay_data", side_effect=replay),
        ):
            game.watch_state()
        self.assertEqual(lock_states, [False, False])

    def test_classic_technology_catalog_is_normalized_closed_and_acyclic(self):
        catalog = _classic_technology_catalog(classic_raw_catalog())
        technologies = catalog["technologies"]
        self.assertEqual(len(technologies), 87)
        by_id = {technology["id"]: technology for technology in technologies}
        self.assertEqual(len(by_id), 87)
        railroad = next(
            technology for technology in technologies
            if technology["rule_name"] == "Railroad"
        )
        self.assertEqual(railroad["name"], "Railroad")
        self.assertNotIn("?tech:", json.dumps(catalog))
        for technology in technologies:
            for requirement in technology["requires"]:
                self.assertIn(requirement, by_id)
                self.assertLess(
                    by_id[requirement]["depth"], technology["depth"],
                )

    def test_replay_paginates_enriches_and_tolerates_partial_tail(self):
        created = self.create()
        game = self.supervisor.game(created["game_id"])
        game.join(
            created["join_token"], controller_label="codex-gpt-test",
            metadata={"model": "gpt-test"},
        )
        raw_catalog = classic_raw_catalog()
        game.replay_catalog_path.write_text(
            json.dumps(raw_catalog), encoding="utf-8",
        )
        catalog = _classic_technology_catalog(raw_catalog)
        alphabet_id = next(
            technology["id"] for technology in catalog["technologies"]
            if technology["rule_name"] == "Alphabet"
        )
        snapshots = [
            {
                "schema_version": 1,
                "game_id": game.game_id,
                "turn": 1,
                "year": -4000,
                "players": [
                    replay_player(0, "AgentPlace1"),
                    replay_player(1, "NativePlace2"),
                ],
            },
            {
                "schema_version": 1,
                "game_id": game.game_id,
                "turn": 2,
                "year": -3950,
                "players": [
                    replay_player(
                        0, "AgentPlace1", [alphabet_id], score=18,
                    ),
                    replay_player(1, "NativePlace2", score=15),
                    replay_player(
                        2, "Blackbeard", nation="Pirate", score=999,
                    ),
                ],
            },
        ]
        game.replay_path.write_text(
            "".join(json.dumps(snapshot) + "\n" for snapshot in snapshots)
            + '{"schema_version":1,"turn":',
            encoding="utf-8",
        )
        game.replay_warnings_path.write_text(
            json.dumps({
                "turn": 2,
                "message": f"secret {created['owner_token']} /private/path",
            }) + "\n",
            encoding="utf-8",
        )

        first = game.replay_state(0, 1)
        self.assertTrue(first["available"])
        self.assertTrue(first["has_more"])
        self.assertEqual(first["next_after_turn"], 1)
        self.assertEqual(len(first["catalog"]["technologies"]), 87)
        codex = first["snapshots"][0]["players"][0]
        self.assertEqual(codex["place"], 1)
        self.assertEqual(codex["controller_label"], "codex-gpt-test")
        self.assertEqual(codex["model"], "gpt-test")
        self.assertEqual(codex["player_color"], "#0067A5")
        self.assertTrue(codex["scored"])

        second = game.replay_state(first["next_after_turn"], 1)
        self.assertFalse(second["has_more"])
        codex = second["snapshots"][0]["players"][0]
        self.assertEqual(codex["gained_tech_ids"], [alphabet_id])
        pirate = second["snapshots"][0]["players"][2]
        self.assertEqual(pirate["controller_type"], "dynamic")
        self.assertIsNone(pirate["place"])
        self.assertFalse(pirate["scored"])
        warning_text = json.dumps(second["replay_warnings"])
        self.assertIn("incomplete trailing record", warning_text)
        self.assertIn("unavailable for this turn", warning_text)
        self.assertNotIn(created["owner_token"], warning_text)
        self.assertNotIn("/private/path", warning_text)
        self.assertIsNone(game.status()["benchmark_valid"])
        game.invalid_reasons.append("turn 2 timed out")
        self.assertFalse(game.status()["benchmark_valid"])

    def test_frame_manifest_reads_turn_and_exact_dynamic_faction_colors(self):
        created = self.create()
        game = self.supervisor.game(created["game_id"])
        game.join(
            created["join_token"], controller_label="codex-gpt-test",
        )
        frame = game.episode / "turn-0042-M-test.map.ppm"
        frame.write_text(
            "P3\n"
            "# version:2\n"
            '# playerno:0:color:(  0, 103, 165):name:"AgentPlace1"\n'
            '# playerno:1:color:(243, 132,   0):name:"NativePlace2"\n'
            '# playerno:2:color:(255,  20, 147):name:"Blackbeard"\n'
            "1 1\n255\n0 0 0\n",
            encoding="utf-8",
        )
        row = game.frame_manifest()["frames"][0]
        self.assertEqual(row["turn"], 42)
        players = row["map_players"]
        self.assertEqual(
            [player["player_color"] for player in players],
            ["#0067A5", "#F38400", "#FF1493"],
        )
        self.assertEqual(players[0]["controller_label"], "codex-gpt-test")
        self.assertEqual(players[1]["controller_label"], "Freeciv Classic AI")
        self.assertFalse(players[2]["scored"])
        self.assertEqual(players[2]["controller_type"], "dynamic")
        parser = game._ppm_map_players
        with patch.object(game, "_ppm_map_players", wraps=parser) as parse_mock:
            game.frame_manifest()
            game.frame_manifest()
            parse_mock.assert_not_called()
            frame.write_text(frame.read_text() + "\n", encoding="utf-8")
            game.frame_manifest()
            self.assertEqual(parse_mock.call_count, 1)

    def test_http_serves_safe_committed_viewer_and_immutable_assets(self):
        server = make_supervisor_server(
            self.supervisor, "127.0.0.1", 0,
        )
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            created = self.create()
            base = self.supervisor.service_url
            with urllib.request.urlopen(f"{base}/health", timeout=2) as response:
                health = json.loads(response.read())
            self.assertEqual(health["native_viewer_protocol"], {
                "version": 1,
                "lease_status": True,
                "bridge_response_ack": True,
                "release_during_activation": True,
            })
            with urllib.request.urlopen(f"{base}/", timeout=2) as response:
                arena = response.read().decode("utf-8")
                self.assertEqual(
                    arena, (VIEWER_DIST_ROOT / "arena.html").read_text(),
                )
                self.assertEqual(response.headers["Cache-Control"], "no-store")
                self.assertIn(
                    "default-src 'self'",
                    response.headers["Content-Security-Policy"],
                )
                self.assertEqual(
                    response.headers["Referrer-Policy"], "no-referrer",
                )
            with urllib.request.urlopen(
                f"{base}/watch/{created['game_id']}", timeout=2,
            ) as response:
                body = response.read().decode("utf-8")
                self.assertEqual(
                    body, (VIEWER_DIST_ROOT / "index.html").read_text(),
                )
                self.assertEqual(response.headers["Cache-Control"], "no-store")
                self.assertIn(
                    "default-src 'self'",
                    response.headers["Content-Security-Policy"],
                )
            self.assertNotIn(created["game_id"], body)
            self.assertNotIn(created["owner_token"], body)
            self.assertNotIn(created["join_token"], body)

            asset = next(
                path for path in (VIEWER_DIST_ROOT / "assets").iterdir()
                if f"./viewer/assets/{path.name}" in arena
            )
            arena_asset_url = urljoin(
                f"{base}/", f"./viewer/assets/{asset.name}",
            )
            self.assertIn(f"./viewer/assets/{asset.name}", arena)
            with urllib.request.urlopen(
                arena_asset_url, timeout=2,
            ) as response:
                self.assertEqual(response.read(), asset.read_bytes())
                self.assertEqual(
                    response.headers["Cache-Control"],
                    "public, max-age=31536000, immutable",
                )
                self.assertEqual(
                    response.headers["X-Content-Type-Options"], "nosniff",
                )
            with self.assertRaises(urllib.error.HTTPError) as context:
                urllib.request.urlopen(
                    f"{base}/viewer/assets/..%2Findex.html", timeout=2,
                )
            self.assertEqual(context.exception.code, 404)
            context.exception.close()
            with self.assertRaises(urllib.error.HTTPError) as context:
                urllib.request.urlopen(
                    f"{base}/v1/games/{created['game_id']}/replay.json?limit=251",
                    timeout=2,
                )
            self.assertEqual(context.exception.code, 400)
            context.exception.close()
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)

    def test_http_serves_arena_api_watch_and_assets_under_public_prefix(self):
        server = make_supervisor_server(
            self.supervisor, "127.0.0.1", 0,
            "https://games.example.test/freeciv",
        )
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        connection = None
        try:
            host, port = server.server_address
            local = f"http://{host}:{port}"
            request = urllib.request.Request(
                f"{local}/freeciv/v1/games",
                data=b"{}",
                headers={
                    "Authorization": "Bearer admin-secret",
                    "Content-Type": "application/json",
                },
                method="POST",
            )
            with urllib.request.urlopen(request, timeout=2) as response:
                self.assertEqual(response.status, HTTPStatus.CREATED)
                game_id = json.loads(response.read())["game_id"]

            with urllib.request.urlopen(
                f"{local}/freeciv/", timeout=2,
            ) as response:
                arena = response.read().decode("utf-8")
                self.assertEqual(
                    arena, (VIEWER_DIST_ROOT / "arena.html").read_text(),
                )
                self.assertEqual(
                    response.headers["Referrer-Policy"], "no-referrer",
                )
            with urllib.request.urlopen(
                f"{local}/freeciv/watch/{game_id}", timeout=2,
            ) as response:
                self.assertEqual(
                    response.read().decode("utf-8"),
                    (VIEWER_DIST_ROOT / "index.html").read_text(),
                )
            with urllib.request.urlopen(
                f"{local}/freeciv/v1/games", timeout=2,
            ) as response:
                index = json.loads(response.read())
                self.assertEqual(index["games"][0]["game_id"], game_id)
                self.assertEqual(
                    index["games"][0]["watch_path"],
                    f"/freeciv/watch/{game_id}",
                )

            asset = next(
                path for path in (VIEWER_DIST_ROOT / "assets").iterdir()
                if f"./viewer/assets/{path.name}" in arena
            )
            self.assertIn(f"./viewer/assets/{asset.name}", arena)
            with urllib.request.urlopen(
                f"{local}/freeciv/viewer/assets/{asset.name}", timeout=2,
            ) as response:
                self.assertEqual(response.read(), asset.read_bytes())
                self.assertEqual(
                    response.headers["Cache-Control"],
                    "public, max-age=31536000, immutable",
                )

            connection = http.client.HTTPConnection(host, port, timeout=2)
            connection.request("GET", "/freeciv?mode=live&turn=2")
            response = connection.getresponse()
            self.assertEqual(response.status, HTTPStatus.PERMANENT_REDIRECT)
            self.assertEqual(
                response.getheader("Location"),
                "/freeciv/?mode=live&turn=2",
            )
            self.assertNotIn("#", response.getheader("Location"))
            self.assertEqual(response.read(), b"")
            connection.close()
            connection = None

            with self.assertRaises(urllib.error.HTTPError) as context:
                urllib.request.urlopen(
                    f"{local}/freecivish/", timeout=2,
                )
            self.assertEqual(context.exception.code, HTTPStatus.NOT_FOUND)
            context.exception.close()
        finally:
            if connection is not None:
                connection.close()
            server.shutdown()
            server.server_close()
            thread.join(2)

    def test_http_redirects_trailing_watch_slash_without_losing_proxy_prefix(self):
        server = make_supervisor_server(
            self.supervisor, "127.0.0.1", 0,
        )
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        connection = None
        try:
            created = self.create()
            game_id = created["game_id"]
            host, port = server.server_address
            connection = http.client.HTTPConnection(host, port, timeout=2)
            connection.request(
                "GET", f"/watch/{game_id}/?mode=live&turn=2",
            )
            response = connection.getresponse()
            location = response.getheader("Location")
            self.assertEqual(response.status, HTTPStatus.PERMANENT_REDIRECT)
            self.assertEqual(location, f"../{game_id}?mode=live&turn=2")
            self.assertEqual(response.read(), b"")
            self.assertEqual(
                urljoin(
                    f"https://example.test/freeciv/watch/{game_id}/",
                    location,
                ),
                f"https://example.test/freeciv/watch/{game_id}?mode=live&turn=2",
            )
        finally:
            if connection is not None:
                connection.close()
            server.shutdown()
            server.server_close()
            thread.join(2)

    @patch("agent_eval.__main__.join_game")
    @patch("agent_eval.__main__.create_game")
    def test_cli_saved_credentials_and_session_never_echo_tokens(
        self, create_mock, join_mock,
    ):
        create_mock.return_value = {
            "schema_version": 1,
            "game_id": "game_" + "a" * 24,
            "state": "lobby",
            "owner_token": "owner-raw-secret",
            "join_token": "join-raw-secret",
        }
        credentials_template = (
            Path(self.directory.name) / "games" / "{game_id}" / "owner.json"
        )
        credentials = (
            Path(self.directory.name) / "games"
            / ("game_" + "a" * 24) / "owner.json"
        )
        invite_template = (
            Path(self.directory.name) / "invites" / "{game_id}.json"
        )
        invite = (
            Path(self.directory.name) / "invites"
            / ("game_" + "a" * 24 + ".json")
        )
        output = io.StringIO()
        error_output = io.StringIO()
        with redirect_stdout(output), redirect_stderr(error_output):
            result = main([
                "game", "create", "--admin-token", "admin",
                "--credentials", str(credentials_template),
                "--player-invite", str(invite_template),
            ])
        self.assertEqual(result, 0)
        self.assertNotIn("owner-raw-secret", output.getvalue())
        self.assertNotIn("join-raw-secret", output.getvalue())
        self.assertNotIn("join-raw-secret", error_output.getvalue())
        self.assertTrue(json.loads(output.getvalue())["credentials_saved"])
        self.assertTrue(json.loads(output.getvalue())["player_invite_saved"])
        self.assertEqual(
            load_private_json(credentials)["owner_token"], "owner-raw-secret",
        )
        self.assertEqual(invite.stat().st_mode & 0o777, 0o600)
        self.assertEqual(load_private_json(invite), {
            "schema_version": 1,
            "service_url": "http://127.0.0.1:8765",
            "game_id": "game_" + "a" * 24,
            "join_token": "join-raw-secret",
        })

        join_mock.return_value = {
            "schema_version": 1,
            "game_id": "game_" + "a" * 24,
            "agent_id": "agent-id",
            "agent_token": "agent-raw-secret",
            "place": 1,
            "seat_id": "place-1",
            "controller_label": "Codex",
            "controller_metadata": {"model": "gpt"},
            "controller_fingerprint": "f" * 64,
            "control_protocol": "full-control-v2",
            "supported_control_protocols": ["full-control-v2"],
        }
        session = Path(self.directory.name) / "agent.json"
        output = io.StringIO()
        with patch(
            "agent_eval.__main__.join_capabilities",
            return_value=("full-control-v2", ["full-control-v2"]),
        ), redirect_stdout(output), redirect_stderr(io.StringIO()):
            result = main([
                "game", "join", "game_" + "a" * 24,
                "--join-token", "join-raw-secret",
                "--controller-label", "Codex",
                "--metadata", '{"model":"gpt"}',
                "--session", str(session),
            ])
        self.assertEqual(result, 0)
        self.assertNotIn("agent-raw-secret", output.getvalue())
        self.assertTrue(json.loads(output.getvalue())["session_saved"])
        self.assertEqual(
            load_private_json(session)["agent_token"], "agent-raw-secret",
        )
        self.assertEqual(
            load_private_json(session)["control_protocol"], "full-control-v2",
        )
        self.assertEqual(
            join_mock.call_args.args[-1], ["full-control-v2"],
        )

    @patch("agent_eval.__main__.join_game")
    @patch("agent_eval.__main__.join_capabilities")
    def test_cli_legacy_join_response_saves_v1_session_and_token(
        self, capabilities_mock, join_mock,
    ):
        capabilities_mock.return_value = ("strategic-v1", None)
        join_mock.return_value = {
            "schema_version": 1,
            "game_id": "game_" + "l" * 24,
            "agent_id": "legacy-agent",
            "agent_token": "legacy-agent-secret",
            "place": 1,
            "seat_id": "place-1",
            "controller_label": "legacy-model",
            "controller_metadata": {},
            "controller_fingerprint": "e" * 64,
        }
        session = Path(self.directory.name) / "legacy-agent.json"
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            result = main([
                "game", "join", "game_" + "l" * 24,
                "--join-token", "legacy-join-secret",
                "--controller-label", "legacy-model",
                "--session", str(session),
            ])
        self.assertEqual(result, 0)
        saved = load_private_json(session)
        self.assertEqual(saved["agent_token"], "legacy-agent-secret")
        self.assertEqual(saved["control_protocol"], "strategic-v1")
        self.assertEqual(saved["supported_control_protocols"], [])
        self.assertIsNone(join_mock.call_args.args[-1])

    @patch("agent_eval.__main__.create_game")
    def test_cli_forwards_timing_mode_without_inventing_timeout(self, create_mock):
        create_mock.return_value = {
            "schema_version": 1,
            "game_id": "game_" + "t" * 24,
            "state": "lobby",
            "owner_token": "owner-raw-secret",
            "join_token": "join-raw-secret",
            "timing_mode": "infinite",
            "action_timeout_s": None,
        }
        credentials = Path(self.directory.name) / "timing-owner.json"
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            result = main([
                "game", "create", "--admin-token", "admin",
                "--timing-mode", "infinite",
                "--credentials", str(credentials),
            ])
        self.assertEqual(result, 0)
        payload = create_mock.call_args.args[2]
        self.assertEqual(payload["timing_mode"], "infinite")
        self.assertNotIn("action_timeout_s", payload)
        self.assertNotIn("control_protocol", payload)

    @patch("agent_eval.__main__.create_game")
    def test_cli_explains_that_old_supervisor_needs_restart_for_timing(
        self, create_mock,
    ):
        create_mock.side_effect = ClientError(
            400, "game request has unknown fields: ['timing_mode']",
        )
        stderr = io.StringIO()
        with redirect_stderr(stderr):
            result = main([
                "game", "create",
                "--admin-token", "admin",
                "--timing-mode", "infinite",
            ])
        self.assertEqual(result, 2)
        self.assertRegex(
            stderr.getvalue(),
            "running supervisor predates timing modes.*restart it with "
            "`just start`.*was not downgraded",
        )

    def test_cli_stage_invite_rebuilds_only_game_scoped_player_credentials(self):
        game_id = "game_" + "b" * 24
        credentials = Path(self.directory.name) / "owner.json"
        invite = Path(self.directory.name) / "player-invite.json"
        write_private_json(credentials, {
            "schema_version": 1,
            "service_url": "http://127.0.0.1:8765",
            "game_id": game_id,
            "owner_token": "owner-secret-not-for-player",
            "join_token": "join-secret-for-one-game",
        })
        output = io.StringIO()
        error_output = io.StringIO()
        with redirect_stdout(output), redirect_stderr(error_output):
            result = main([
                "game", "stage-invite", game_id,
                "--credentials", str(credentials),
                "--output", str(invite),
            ])
        self.assertEqual(result, 0)
        self.assertNotIn("owner-secret-not-for-player", output.getvalue())
        self.assertNotIn("join-secret-for-one-game", output.getvalue())
        self.assertNotIn("join-secret-for-one-game", error_output.getvalue())
        self.assertEqual(invite.stat().st_mode & 0o777, 0o600)
        self.assertEqual(load_private_json(invite), {
            "schema_version": 1,
            "service_url": "http://127.0.0.1:8765",
            "game_id": game_id,
            "join_token": "join-secret-for-one-game",
        })
        public = json.loads(output.getvalue())
        self.assertEqual(public["game_id"], game_id)
        self.assertTrue(public["player_invite_saved"])

        other_game_id = "game_" + "c" * 24
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            result = main([
                "game", "stage-invite", other_game_id,
                "--credentials", str(credentials),
                "--output", str(invite),
            ])
        self.assertEqual(result, 2)

        credentials.chmod(0o644)
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            result = main([
                "game", "stage-invite", game_id,
                "--credentials", str(credentials),
                "--output", str(invite),
            ])
        self.assertEqual(result, 2)

        credentials.chmod(0o600)
        unrelated = Path(self.directory.name) / "unrelated.json"
        unrelated.write_text("untouched", encoding="utf-8")
        linked_invite = Path(self.directory.name) / "linked-invite.json"
        linked_invite.symlink_to(unrelated)
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            result = main([
                "game", "stage-invite", game_id,
                "--credentials", str(credentials),
                "--output", str(linked_invite),
            ])
        self.assertEqual(result, 2)
        self.assertEqual(unrelated.read_text(encoding="utf-8"), "untouched")

        write_private_json(credentials, {
            "schema_version": 1,
            "service_url": "http://127.0.0.1:notaport",
            "game_id": game_id,
            "owner_token": "owner-secret-not-for-player",
            "join_token": "join-secret-for-one-game",
        })
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            result = main([
                "game", "stage-invite", game_id,
                "--credentials", str(credentials),
                "--output", str(invite),
            ])
        self.assertEqual(result, 2)

        write_private_json(credentials, {
            "schema_version": 1,
            "service_url": "http://127.0.0.1:8765",
            "game_id": game_id,
            "owner_token": "owner-secret-not-for-player",
            "join_token": "join-secret-for-one-game",
        })
        repo = Path(__file__).parents[2]
        with tempfile.TemporaryDirectory(dir=repo) as directory:
            root = Path(directory)
            actual_parent = root / "actual"
            actual_parent.mkdir()
            linked_parent = root / "linked"
            linked_parent.symlink_to(actual_parent, target_is_directory=True)
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                result = main([
                    "game", "stage-invite", game_id,
                    "--credentials", str(credentials),
                    "--output", str(linked_parent / "invite.json"),
                ])
            self.assertEqual(result, 2)
            self.assertFalse((actual_parent / "invite.json").exists())

        terminal_invite = Path(self.directory.name) / "terminal-invite.json"
        with patch(
            "agent_eval.__main__.request_json",
            return_value={"state": "failed"},
        ), redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            result = main([
                "game", "stage-invite", game_id,
                "--credentials", str(credentials),
                "--output", str(terminal_invite),
                "--require-open-lobby",
            ])
        self.assertEqual(result, 2)
        self.assertFalse(terminal_invite.exists())

    def test_credentialed_client_request_rejects_redirects(self):
        captured_authorization = []

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                self.send_response(302)
                self.send_header(
                    "Location",
                    f"http://127.0.0.1:{self.server.server_port}/capture",
                )
                self.send_header("Content-Length", "0")
                self.end_headers()

            def do_GET(self):
                captured_authorization.append(self.headers.get("Authorization"))
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(b"{}")

            def log_message(self, _format, *_args):
                return

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            host, port = server.server_address
            with self.assertRaises(ClientError) as raised:
                request_json(
                    "POST", f"http://{host}:{port}/join",
                    token="join-secret", body={}, timeout=2,
                )
            self.assertEqual(raised.exception.status, 302)
            self.assertEqual(captured_authorization, [])
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)

    @patch("agent_eval.__main__.require_native_viewer_protocol")
    @patch("agent_eval.__main__.request_native_viewer")
    def test_cli_native_viewer_reads_owner_credentials(
        self, viewer_mock, protocol_mock,
    ):
        game_id = "game_" + "v" * 24
        credentials = Path(self.directory.name) / "viewer-owner.json"
        write_private_json(
            credentials,
            {
                "service_url": "http://127.0.0.1:9999",
                "game_id": game_id,
                "owner_token": "owner-secret",
            },
        )
        viewer_mock.return_value = {
            "game_id": game_id,
            "host": "127.0.0.1",
            "port": 12345,
            "username": "Watch-test",
        }
        output = io.StringIO()
        with redirect_stdout(output):
            result = main([
                "game", "native-viewer", game_id,
                "--credentials", str(credentials),
            ])
        self.assertEqual(result, 0)
        viewer_mock.assert_called_once_with(
            "http://127.0.0.1:9999", game_id, "owner-secret",
        )
        protocol_mock.assert_called_once_with(
            "http://127.0.0.1:9999", game_id,
        )
        self.assertNotIn("owner-secret", output.getvalue())

    @patch("agent_eval.__main__.require_native_viewer_protocol")
    @patch("agent_eval.__main__.release_native_viewer")
    @patch("agent_eval.__main__.request_native_viewer")
    def test_cli_persists_and_releases_native_viewer_lease(
        self, viewer_mock, release_mock, protocol_mock,
    ):
        game_id = "game_" + "l" * 24
        credentials = Path(self.directory.name) / "lease-owner.json"
        lease_file = Path(self.directory.name) / "viewer-lease.json"
        write_private_json(
            credentials,
            {
                "service_url": "http://127.0.0.1:9998",
                "game_id": game_id,
                "owner_token": "owner-secret",
            },
        )
        viewer_mock.return_value = {
            "game_id": game_id,
            "lease_id": "viewer_lease-id",
            "host": "127.0.0.1",
            "port": 12346,
            "username": "Watch-lease",
        }
        release_mock.return_value = {
            "game_id": game_id,
            "lease_id": "viewer_lease-id",
            "released": True,
        }
        with redirect_stdout(io.StringIO()):
            self.assertEqual(main([
                "game", "native-viewer", game_id,
                "--credentials", str(credentials),
                "--lease-file", str(lease_file),
            ]), 0)
        self.assertEqual(
            load_private_json(lease_file)["lease_id"], "viewer_lease-id",
        )
        with redirect_stdout(io.StringIO()):
            self.assertEqual(main([
                "game", "native-viewer-release", game_id,
                "--credentials", str(credentials),
                "--lease-file", str(lease_file),
            ]), 0)
        release_mock.assert_called_once_with(
            "http://127.0.0.1:9998", game_id,
            "owner-secret", "viewer_lease-id",
        )
        protocol_mock.assert_called_once_with(
            "http://127.0.0.1:9998", game_id,
        )

    def test_native_viewer_protocol_preflight_rejects_old_supervisor(self):
        game_id = "game_" + "o" * 24
        with patch(
            "agent_eval.client.request_json",
            return_value={"ok": True, "games": 1},
        ) as request_mock, self.assertRaises(ClientError) as context:
            require_native_viewer_protocol("http://127.0.0.1:9996", game_id)
        message = str(context.exception)
        self.assertIn("already-running supervisor", message)
        self.assertIn("game was not changed", message)
        self.assertIn(f"just replay {game_id}", message)
        self.assertIn("restart the supervisor", message)
        request_mock.assert_called_once_with(
            "GET", "http://127.0.0.1:9996/health", timeout=10,
        )

    def test_native_viewer_protocol_preflight_accepts_safe_features(self):
        protocol = {
            "version": 1,
            "lease_status": True,
            "bridge_response_ack": True,
            "release_during_activation": True,
        }
        with patch(
            "agent_eval.client.request_json",
            return_value={"ok": True, "native_viewer_protocol": protocol},
        ):
            self.assertEqual(
                require_native_viewer_protocol(
                    "http://127.0.0.1:9996", "game_" + "p" * 24,
                ),
                protocol,
            )

    @patch("agent_eval.watch_room.run_snapshot_watch_room", return_value=0)
    @patch(
        "agent_eval.__main__.require_native_viewer_protocol",
        side_effect=NativeViewerCompatibilityError(None, "old supervisor"),
    )
    @patch("agent_eval.__main__.run_native_viewer_client")
    def test_cli_falls_back_to_snapshot_room_before_live_lease(
        self, live_mock, protocol_mock, snapshot_mock,
    ):
        game_id = "game_" + "q" * 24
        credentials = Path(self.directory.name) / "owner.json"
        write_private_json(credentials, {
            "service_url": "http://127.0.0.1:9994",
            "game_id": game_id,
            "owner_token": "owner-secret",
        })
        with redirect_stderr(io.StringIO()) as stderr:
            result = main([
                "game", "native-viewer-run", game_id,
                "--credentials", str(credentials),
                "--client", "/tmp/freeciv-sdl2",
                "--snapshot-server", "/tmp/freeciv-server",
                "--data-path", "/tmp/data",
                "--log-dir", self.directory.name,
            ])
        self.assertEqual(result, 0)
        protocol_mock.assert_called_once_with(
            "http://127.0.0.1:9994", game_id,
        )
        live_mock.assert_not_called()
        snapshot_mock.assert_called_once_with(
            game_id,
            credentials_path=str(credentials),
            server_binary="/tmp/freeciv-server",
            client_binary="/tmp/freeciv-sdl2",
            data_path="/tmp/data",
        )
        self.assertIn("isolated snapshot watch room", stderr.getvalue())

    def test_just_surfaces_native_watch_replay_and_safe_agent_prompt(self):
        repo = Path(__file__).parents[2]
        justfile = (repo / "justfile").read_text()
        self.assertIn("watch game_id: build build-viewer", justfile)
        self.assertIn('replay game_id="":', justfile)
        self.assertIn("start: build replay-build", justfile)
        self.assertIn("-m agent_eval.local_stack start", justfile)
        self.assertIn("-m agent_eval.local_stack replay", justfile)
        self.assertIn("https://freeciv-api.localhost", justfile)
        self.assertNotIn("cleanup_spawned_gateway", justfile)
        self.assertNotIn("-m agent_eval.replay_gateway", justfile)
        self.assertIn("replay-build: replay-install", justfile)
        self.assertIn("replay-dev: replay-install", justfile)
        self.assertIn("replay-check: replay-install", justfile)
        self.assertIn("game native-viewer", justfile)
        self.assertIn("game native-viewer-run", justfile)
        self.assertIn("game native-viewer-release", justfile)
        self.assertIn("--log-dir", justfile)
        self.assertIn("--snapshot-server", justfile)
        self.assertIn("trap cleanup EXIT", justfile)
        self.assertNotIn("exec env FREECIV_DATA_PATH", justfile)
        self.assertIn("controller_name=HARNESS-MODEL", justfile)
        self.assertIn("games/{game_id}/owner.json", justfile)
        self.assertEqual(
            justfile.count('--player-invite "play/.invites/{game_id}.json"'),
            1,
        )
        self.assertIn("invite game_id:", justfile)
        self.assertIn("game stage-invite", justfile)
        self.assertIn("games/{{ game_id }}", justfile)
        self.assertIn("build-viewer/build.ninja", justfile)
        self.assertIn("--ninja-args=--quiet", justfile)
        self.assertNotIn("meson setup --reconfigure build-viewer", justfile)
        self.assertGreaterEqual(
            justfile.count(
                'mode_or_places="default" places_or_turns="" turns="" '
                'max_turns="":'
            ),
            2,
        )
        for recipe in ("single", "multi"):
            max_turns = subprocess.run(
                ["just", "--dry-run", recipe, "--max-turns", "321"],
                cwd=repo, text=True, capture_output=True,
            )
            self.assertEqual(max_turns.returncode, 0, max_turns.stderr)
            rendered_max_turns = max_turns.stdout + max_turns.stderr
            mode_word = "single" if recipe == "single" else "multiplayer"
            self.assertIn(
                f'just _create {mode_word} "$protocol" "$difficulty" '
                '"$a" "$b" "$c" "321"',
                rendered_max_turns,
            )
            self.assertIn("protocol=full-control-v2", rendered_max_turns)
            self.assertIn("difficulty=hard", rendered_max_turns)
            for mode in ("infinite",):
                mode_dry_run = subprocess.run(
                    ["just", "--dry-run", recipe, mode],
                    cwd=repo, text=True, capture_output=True,
                )
                self.assertEqual(
                    mode_dry_run.returncode, 0, mode_dry_run.stderr,
                )
                self.assertIn(
                    f'for token in "{mode}"',
                    mode_dry_run.stdout + mode_dry_run.stderr,
                )
            legacy_dry_run = subprocess.run(
                ["just", "--dry-run", recipe, "4", "200"],
                cwd=repo, text=True, capture_output=True,
            )
            self.assertEqual(
                legacy_dry_run.returncode, 0, legacy_dry_run.stderr,
            )
            rendered_legacy = legacy_dry_run.stdout + legacy_dry_run.stderr
            self.assertIn('for token in "4" "200" "" ""', rendered_legacy)
            invalid_mode = subprocess.run(
                ["just", recipe, "turbo"],
                cwd=repo, text=True, capture_output=True,
            )
            self.assertEqual(invalid_mode.returncode, 2)
            self.assertIn(
                "timing mode must be default, blitz, or infinite",
                invalid_mode.stderr,
            )
        invite_dry_run = subprocess.run(
            [
                "just", "--dry-run", "invite",
                "game_12345678901234567890",
            ],
            cwd=repo, text=True, capture_output=True,
        )
        self.assertEqual(
            invite_dry_run.returncode, 0, invite_dry_run.stderr,
        )
        rendered_invite = invite_dry_run.stdout + invite_dry_run.stderr
        self.assertIn("game stage-invite", rendered_invite)
        self.assertIn("--require-open-lobby", rendered_invite)
        self.assertIn(
            "play/.invites/game_12345678901234567890.json",
            rendered_invite,
        )
        self.assertGreaterEqual(justfile.count("controller_session_key"), 2)
        dry_run = subprocess.run(
            ["just", "--dry-run", "replay", "game_demo"],
            cwd=repo, text=True, capture_output=True,
        )
        self.assertEqual(dry_run.returncode, 0, dry_run.stderr)
        rendered_dry_run = dry_run.stdout + dry_run.stderr
        self.assertIn("-m agent_eval.local_stack replay", rendered_dry_run)
        self.assertIn('"game_demo"', rendered_dry_run)
        rejected = subprocess.run(
            ["just", "join", "--game_id", "game_" + "x" * 24],
            cwd=repo, text=True, capture_output=True,
        )
        self.assertEqual(rejected.returncode, 2)
        self.assertIn("harness-model identity is required", rejected.stderr)
        bare = subprocess.run(
            ["just", "join"], cwd=repo, text=True, capture_output=True,
        )
        self.assertEqual(bare.returncode, 0)
        self.assertIn("--name HARNESS-MODEL", bare.stdout)
        self.assertIn(f"cd {repo.resolve() / 'play'}", bare.stdout)
        self.assertIn("player-only join command", bare.stdout)
        self.assertIn("Do not run the repository-root owner join", bare.stdout)
        self.assertNotIn("From the repository root, run", bare.stdout)

    def test_just_timing_argument_orders_execute_expected_create(self):
        repo = Path(__file__).parents[2]
        fake_bin = Path(self.directory.name) / "timing-bin"
        fake_bin.mkdir()
        capture = Path(self.directory.name) / "create-args"
        python = fake_bin / "python3"
        python.write_text(
            "#!/bin/sh\nprintf '%s\\n' \"$@\" >\"$JUST_CAPTURE\"\n",
            encoding="utf-8",
        )
        python.chmod(0o755)
        environment = {
            **os.environ,
            "PATH": f"{fake_bin}{os.pathsep}{os.environ['PATH']}",
            "JUST_CAPTURE": str(capture),
            "AGENT_EVAL_STATE_DIR": str(
                Path(self.directory.name) / "timing-state"
            ),
        }

        cases = (
            ([], "2", "default", "5000", "full-control-v2", "hard"),
            (["2", "infinite"], "2", "infinite", "5000",
             "full-control-v2", "hard"),
            (["v1", "3", "blitz", "150"], "3", "blitz", "150",
             "strategic-v1", "hard"),
            (["infinite", "2", "150"], "2", "infinite", "150",
             "full-control-v2", "hard"),
            (["cheating", "2", "150"], "2", "default", "150",
             "full-control-v2", "cheating"),
            (["2", "infinite", "--max-turns", "321"], "2", "infinite", "321",
             "full-control-v2", "hard"),
        )
        for recipe in ("single", "multi"):
            for arguments, places, mode, turns, protocol, difficulty in cases:
                with self.subTest(recipe=recipe, arguments=arguments):
                    result = subprocess.run(
                        ["just", recipe, *arguments],
                        cwd=repo,
                        env=environment,
                        text=True,
                        capture_output=True,
                    )
                    self.assertEqual(result.returncode, 0, result.stderr)
                    values = capture.read_text(encoding="utf-8").splitlines()

                    def option(name):
                        return values[values.index(name) + 1]

                    self.assertEqual(option("--places"), places)
                    self.assertEqual(option("--timing-mode"), mode)
                    self.assertEqual(option("--turns"), turns)
                    self.assertEqual(option("--control-protocol"), protocol)
                    self.assertEqual(option("--difficulty"), difficulty)

            invalid = subprocess.run(
                ["just", recipe, "2", "turbo"],
                cwd=repo,
                env=environment,
                text=True,
                capture_output=True,
            )
            self.assertEqual(invalid.returncode, 2)
            self.assertIn(
                "after places, use default, blitz, infinite, or a numeric turn limit",
                invalid.stderr,
            )
            v2_blitz = subprocess.run(
                ["just", recipe, "2", "blitz"],
                cwd=repo,
                env=environment,
                text=True,
                capture_output=True,
            )
            self.assertEqual(v2_blitz.returncode, 2)
            self.assertIn("blitz is strategic-v1 only", v2_blitz.stderr)

    def _obsolete_just_replay_reuses_healthy_vite_for_picker_or_direct_game(self):
        repo = Path(__file__).parents[2]
        fake_bin = Path(self.directory.name) / "bin"
        fake_bin.mkdir()
        capture = Path(self.directory.name) / "opened-url"
        npm_called = Path(self.directory.name) / "npm-called"
        state_dir = Path(self.directory.name) / "state"
        state_dir.mkdir()
        service_url = "http://127.0.0.1:8765"
        config = gateway_config(
            service_url,
            state_dir / "runs",
            state_dir / "replay-cache",
            repo_root=repo,
        )
        gateway_pid = 4343
        game_id = "game_" + "r" * 24

        class GatewayHealthHandler(BaseHTTPRequestHandler):
            def log_message(self, format, *args):
                return

            def do_GET(self):
                if self.path == "/health":
                    value = self.server.identity
                elif self.path == "/v1/games":
                    value = self.server.games
                elif self.path == f"/v1/games/{game_id}/status":
                    value = {"game_id": game_id, "state": "invalid"}
                else:
                    self.send_error(404)
                    return
                body = json.dumps(
                    value, sort_keys=True, separators=(",", ":"),
                ).encode()
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        gateway_server = ThreadingHTTPServer(
            ("127.0.0.1", 0), GatewayHealthHandler,
        )
        gateway_port = gateway_server.server_address[1]
        gateway_url = f"http://127.0.0.1:{gateway_port}"
        gateway_ready = {
            "schema_version": 1,
            "ok": True,
            "kind": "freeciv-replay-gateway",
            "protocol_version": 1,
            "identity": config.identity,
            "pid": gateway_pid,
            "host": "127.0.0.1",
            "port": gateway_port,
            "url": gateway_url,
            "repo_root": str(config.repo_root),
            "upstream_service_url": config.upstream_service_url,
            "runs_root": str(config.runs_root),
            "cache_root": str(config.cache_root),
        }
        gateway_server.identity = gateway_ready
        gateway_server.games = {
            "schema_version": 1,
            "games": [{"game_id": game_id, "state": "invalid"}],
        }
        gateway_thread = threading.Thread(
            target=gateway_server.serve_forever, daemon=True,
        )
        gateway_thread.start()
        gateway_state = state_dir / f"replay-gateway-{config.identity}.json"
        gateway_ready_file = (
            state_dir / f"replay-gateway-{config.identity}.ready.json"
        )
        gateway_state.write_text(json.dumps({
            "schema_version": 1,
            "kind": "freeciv-replay-gateway-launch",
            "pid": gateway_pid,
            "identity": config.identity,
            "repo_root": str(config.repo_root),
            "upstream_service_url": config.upstream_service_url,
            "runs_root": str(config.runs_root),
            "cache_root": str(config.cache_root),
            "ready_file": str(gateway_ready_file.resolve()),
        }), encoding="utf-8")
        gateway_ready_file.write_text(
            json.dumps(gateway_ready), encoding="utf-8",
        )
        gateway_state.chmod(0o600)
        gateway_ready_file.chmod(0o600)

        vite_entry = repo / "agent_eval/viewer/node_modules/vite/bin/vite.js"
        vite_identity = hashlib.sha256("\0".join((
            str(repo.resolve()), config.identity, gateway_url, service_url,
        )).encode("utf-8")).hexdigest()[:20]
        vite_state = state_dir / f"replay-vite-{vite_identity}.json"
        vite_state.write_text(json.dumps({
            "schema_version": 1,
            "kind": "freeciv-replay-vite-launch",
            "pid": 4242,
            "repo_root": str(repo.resolve()),
            "stack_identity": config.identity,
            "gateway_url": gateway_url,
            "upstream_service_url": service_url,
            "vite_entry": str(vite_entry),
            "port": 5173,
        }), encoding="utf-8")
        vite_state.chmod(0o600)
        curl = fake_bin / "curl"
        curl.write_text(
            "#!/bin/sh\n"
            "case \"$*\" in\n"
            "  *127.0.0.1:8765/health*) "
            "[ \"${UPSTREAM_OFFLINE:-0}\" = 1 ] && exit 22; exit 0 ;;\n"
            "  *127.0.0.1:5173/@vite/client*) "
            'exec "$REAL_PYTHON" -c \'import os; '
            '[os.write(1, b"vite" + b"x" * 4092) '
            "for _ in range(1024)]\' ;;\n"
            "  *127.0.0.1:5173/*) "
            "printf '<title>Freeciv Agent Arena</title>'; exit 0 ;;\n"
            "esac\n"
            "exit 22\n",
            encoding="utf-8",
        )
        opener = fake_bin / "open"
        opener.write_text(
            '#!/bin/sh\nprintf "%s" "$1" >"$OPEN_CAPTURE"\n',
            encoding="utf-8",
        )
        npm = fake_bin / "npm"
        npm.write_text(
            '#!/bin/sh\ntouch "$NPM_CALLED"\nexit 99\n',
            encoding="utf-8",
        )
        ps = fake_bin / "ps"
        ps.write_text(
            '#!/bin/sh\n'
            'case "$*" in\n'
            '  *"-p 4343"*) printf "%s\\n" "$GATEWAY_PROCESS_COMMAND" ;;\n'
            '  *"-p 4242"*) printf "%s\\n" "$VITE_PROCESS_COMMAND" ;;\n'
            '  *) exit 1 ;;\n'
            'esac\n',
            encoding="utf-8",
        )
        lsof = fake_bin / "lsof"
        lsof.write_text(
            '#!/bin/sh\n'
            'case "$*" in\n'
            f'  *"iTCP:{gateway_port}"*) printf "4343\\n" ;;\n'
            '  *"iTCP:5173"*) printf "4242\\n" ;;\n'
            '  *) exit 1 ;;\n'
            'esac\n',
            encoding="utf-8",
        )
        python = fake_bin / "python3"
        python.write_text(
            '#!/bin/sh\n'
            'if [ "$1" = "-" ] && [ "${2:-}" = "5173" ]; then exit 0; fi\n'
            'exec "$REAL_PYTHON" "$@"\n',
            encoding="utf-8",
        )
        for path in (curl, opener, npm, ps, lsof, python):
            path.chmod(0o755)
        environment = {
            **os.environ,
            "PATH": f"{fake_bin}{os.pathsep}{os.environ['PATH']}",
            "OPEN_CAPTURE": str(capture),
            "NPM_CALLED": str(npm_called),
            "AGENT_EVAL_STATE_DIR": str(state_dir),
            "AGENT_EVAL_SERVICE_URL": service_url + "/",
            "REAL_PYTHON": sys.executable,
            "VITE_PROCESS_COMMAND": (
                f"node {vite_entry} --host 127.0.0.1 "
                "--port 5173 --strictPort"
            ),
            "GATEWAY_PROCESS_COMMAND": (
                "python3 -B -m agent_eval.replay_gateway "
                "--host 127.0.0.1 --port 0 "
                f"--service-url {service_url} "
                f"--runs-root {config.runs_root} "
                f"--cache-root {config.cache_root} "
                f"--repo-root {repo.resolve()} "
                f"--ready-file {gateway_ready_file.resolve()}"
            ),
        }
        try:
            picker = subprocess.run(
                ["just", "replay"], cwd=repo, env=environment,
                text=True, capture_output=True,
            )
            self.assertEqual(picker.returncode, 0, picker.stderr)
            self.assertEqual(capture.read_text(), "http://127.0.0.1:5173/")
            direct = subprocess.run(
                ["just", "replay", game_id], cwd=repo, env=environment,
                text=True, capture_output=True,
            )
            self.assertEqual(direct.returncode, 0, direct.stderr)
            self.assertEqual(
                capture.read_text(), f"http://127.0.0.1:5173/watch/{game_id}",
            )
            self.assertFalse(npm_called.exists())

            environment["UPSTREAM_OFFLINE"] = "1"
            offline_picker = subprocess.run(
                ["just", "replay"], cwd=repo, env=environment,
                text=True, capture_output=True,
            )
            self.assertEqual(
                offline_picker.returncode, 0, offline_picker.stderr,
            )
            self.assertEqual(capture.read_text(), "http://127.0.0.1:5173/")
            offline_direct = subprocess.run(
                ["just", "replay", game_id], cwd=repo, env=environment,
                text=True, capture_output=True,
            )
            self.assertEqual(
                offline_direct.returncode, 0, offline_direct.stderr,
            )
            self.assertEqual(
                capture.read_text(), f"http://127.0.0.1:5173/watch/{game_id}",
            )
            capture.unlink()
            missing_archive = subprocess.run(
                ["just", "replay", "game_" + "z" * 24],
                cwd=repo, env=environment, text=True, capture_output=True,
            )
            self.assertEqual(missing_archive.returncode, 2)
            self.assertIn("No matching live game", missing_archive.stderr)
            self.assertIn("safe terminal archive", missing_archive.stderr)
            self.assertFalse(capture.exists())
            environment["UPSTREAM_OFFLINE"] = "0"

            wrong = json.loads(vite_state.read_text())
            wrong["upstream_service_url"] = "http://127.0.0.1:9999"
            vite_state.write_text(json.dumps(wrong), encoding="utf-8")
            vite_state.chmod(0o600)
            mismatch = subprocess.run(
                ["just", "replay", game_id], cwd=repo, env=environment,
                text=True, capture_output=True,
            )
            self.assertEqual(mismatch.returncode, 2)
            self.assertIn("not be reused or stopped", mismatch.stderr)
            self.assertFalse(capture.exists())
            self.assertFalse(npm_called.exists())
            self.assertTrue(gateway_thread.is_alive())
        finally:
            gateway_server.shutdown()
            gateway_server.server_close()
            gateway_thread.join(2)

    def _obsolete_just_replay_cleans_only_gateway_spawned_before_vite_conflict(self):
        repo = Path(__file__).parents[2].resolve()
        fake_bin = Path(self.directory.name) / "cleanup-bin"
        fake_bin.mkdir()
        state_dir = Path(self.directory.name) / "cleanup-state"
        state_dir.mkdir()
        service_url = "http://127.0.0.1:8765"
        config = gateway_config(
            service_url,
            state_dir / "runs",
            state_dir / "replay-cache",
            repo_root=repo,
        )
        ready_file = (
            state_dir / f"replay-gateway-{config.identity}.ready.json"
        ).resolve()
        gateway_log = (
            state_dir / f"replay-gateway-{config.identity}.log"
        ).resolve()
        curl = fake_bin / "curl"
        curl.write_text('#!/bin/sh\nexit 0\n', encoding="utf-8")
        python = fake_bin / "python3"
        python.write_text(
            '#!/bin/sh\n'
            'if [ "$1" = "-" ] && [ "${2:-}" = "5173" ]; then exit 0; fi\n'
            'exec "$REAL_PYTHON" "$@"\n',
            encoding="utf-8",
        )
        ps = fake_bin / "ps"
        ps.write_text(
            '#!/bin/sh\nprintf "%s\\n" "$GATEWAY_PROCESS_COMMAND"\n',
            encoding="utf-8",
        )
        lsof = fake_bin / "lsof"
        lsof.write_text(
            '#!/bin/sh\n'
            'exec "$REAL_PYTHON" - "$GATEWAY_READY_FILE" <<\'PY\'\n'
            'import json, sys\n'
            'from pathlib import Path\n'
            'print(json.loads(Path(sys.argv[1]).read_text())["pid"])\n'
            'PY\n',
            encoding="utf-8",
        )
        npm_called = Path(self.directory.name) / "cleanup-npm-called"
        npm = fake_bin / "npm"
        npm.write_text(
            '#!/bin/sh\ntouch "$NPM_CALLED"\nexit 99\n',
            encoding="utf-8",
        )
        opener = fake_bin / "open"
        opener.write_text('#!/bin/sh\nexit 99\n', encoding="utf-8")
        for path in (curl, python, ps, lsof, npm, opener):
            path.chmod(0o755)
        environment = {
            **os.environ,
            "PATH": f"{fake_bin}{os.pathsep}{os.environ['PATH']}",
            "REAL_PYTHON": sys.executable,
            "AGENT_EVAL_STATE_DIR": str(state_dir),
            "AGENT_EVAL_SERVICE_URL": service_url,
            "GATEWAY_READY_FILE": str(ready_file),
            "NPM_CALLED": str(npm_called),
            "GATEWAY_PROCESS_COMMAND": (
                "python3 -B -m agent_eval.replay_gateway "
                "--host 127.0.0.1 --port 0 "
                f"--service-url {service_url} "
                f"--runs-root {config.runs_root} "
                f"--cache-root {config.cache_root} "
                f"--repo-root {repo} --ready-file {ready_file}"
            ),
        }
        result = subprocess.run(
            ["just", "replay"], cwd=repo, env=environment,
            text=True, capture_output=True,
        )
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("not be reused or stopped", result.stderr)
        self.assertFalse(npm_called.exists())
        self.assertTrue(gateway_log.is_file())
        ready_line = json.loads(gateway_log.read_text().splitlines()[0])
        spawned_pid = ready_line["pid"]
        with self.assertRaises(ProcessLookupError):
            os.kill(spawned_pid, 0)
        self.assertFalse(ready_file.exists())
        self.assertFalse(
            (state_dir / f"replay-gateway-{config.identity}.json").exists()
        )

    def _obsolete_just_replay_fails_closed_without_listener_owner_tool(self):
        repo = Path(__file__).parents[2]
        just_binary = shutil.which("just")
        self.assertIsNotNone(just_binary)
        fake_bin = Path(self.directory.name) / "owner-tool-bin"
        fake_bin.mkdir()
        curl = fake_bin / "curl"
        curl.write_text('#!/bin/sh\nexit 0\n', encoding="utf-8")
        python = fake_bin / "python3"
        python.write_text(
            '#!/bin/sh\nexec "$REAL_PYTHON" "$@"\n', encoding="utf-8",
        )
        for name in ("bash", "mkdir"):
            target = shutil.which(name)
            self.assertIsNotNone(target)
            (fake_bin / name).symlink_to(target)
        for path in (curl, python):
            path.chmod(0o755)
        environment = {
            **os.environ,
            "PATH": str(fake_bin),
            "REAL_PYTHON": sys.executable,
            "AGENT_EVAL_STATE_DIR": str(
                Path(self.directory.name) / "no-owner-state"
            ),
        }
        result = subprocess.run(
            [just_binary, "replay"], cwd=repo, env=environment,
            text=True, capture_output=True,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("install lsof or ss", result.stderr)

    def test_just_replay_delegates_picker_and_direct_game_to_portless_stack(self):
        repo = Path(__file__).parents[2]
        for game_id in ("", "game_" + "r" * 24):
            with self.subTest(game_id=game_id or "picker"):
                command = ["just", "--dry-run", "replay"]
                if game_id:
                    command.append(game_id)
                result = subprocess.run(
                    command, cwd=repo, text=True, capture_output=True,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                rendered = result.stdout + result.stderr
                self.assertIn("-m agent_eval.local_stack replay", rendered)
                self.assertIn(f'"{game_id}"', rendered)
                self.assertNotIn("replay_gateway", rendered)
                self.assertNotIn("--port 5173", rendered)

    def test_just_replay_leaves_process_ownership_to_local_stack(self):
        repo = Path(__file__).parents[2]
        justfile = (repo / "justfile").read_text(encoding="utf-8")
        replay_recipe = justfile[justfile.index('replay game_id="":'):]
        self.assertIn(
            'python3 -B -m agent_eval.local_stack replay "{{ game_id }}"',
            replay_recipe,
        )
        self.assertNotIn("cleanup_spawned_gateway", replay_recipe)
        self.assertNotIn("nohup", replay_recipe)
        self.assertNotIn("kill ", replay_recipe)

    def test_just_replay_has_no_listener_owner_tool_dependency(self):
        repo = Path(__file__).parents[2]
        justfile = (repo / "justfile").read_text(encoding="utf-8")
        replay_recipe = justfile[justfile.index('replay game_id="":'):]
        self.assertNotIn("lsof", replay_recipe)
        self.assertNotIn("ss -", replay_recipe)
        self.assertIn("-m agent_eval.local_stack replay", replay_recipe)


class SnapshotWatchRoomTests(unittest.TestCase):
    class Buffer(io.StringIO):
        def close(self):
            self.was_closed = True

    class Process:
        def __init__(self, output="", returncode=None):
            self.stdin = SnapshotWatchRoomTests.Buffer()
            self.stdout = SnapshotWatchRoomTests.Buffer(output)
            self.returncode = returncode
            self.terminated = False
            self.killed = False

        def poll(self):
            return self.returncode

        def terminate(self):
            self.terminated = True
            self.returncode = -15

        def kill(self):
            self.killed = True
            self.returncode = -9

        def wait(self, timeout=None):
            return self.returncode

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.state = Path(self.directory.name) / ".agent-eval"
        self.game_id = "game_" + "s" * 24
        self.credentials = (
            self.state / "games" / self.game_id / "owner.json"
        )
        write_private_json(self.credentials, {
            "schema_version": 1,
            "service_url": "http://127.0.0.1:9995",
            "game_id": self.game_id,
            "owner_token": "owner-secret",
        })
        self.run_directory = self.state / "runs" / self.game_id
        self.saves = self.run_directory / "saves"
        self.saves.mkdir(parents=True)

    def tearDown(self):
        self.directory.cleanup()

    def write_save(self, turn):
        path = self.saves / f"turn-{turn:04d}-auto.sav.gz"
        with gzip.open(path, "wb") as stream:
            stream.write(
                b"\n[scenario]\nis_scenario=FALSE\n\n[savefile]\n"
                b'options=" +version3"\nversion=80\n'
            )
        return path

    def test_locator_and_selector_use_newest_stable_turn_save(self):
        old = self.write_save(7)
        newest = self.write_save(8)
        self.assertEqual(
            locate_game_run(self.game_id, self.credentials),
            self.run_directory.resolve(),
        )
        selected = select_stable_snapshot(
            self.run_directory, self.game_id, settle_s=0,
        )
        self.assertEqual(selected.source, newest.resolve())
        self.assertEqual(selected.turn, 8)
        self.assertNotEqual(selected.source, old.resolve())

    def test_selector_skips_save_that_changes_during_stability_check(self):
        stable = self.write_save(7)
        changing = self.write_save(8)

        def mutate(_seconds):
            with changing.open("ab") as stream:
                stream.write(b"still-writing")

        with patch("agent_eval.watch_room.time.sleep", side_effect=mutate):
            selected = select_stable_snapshot(
                self.run_directory, self.game_id, settle_s=0.1,
            )
        self.assertEqual(selected.source, stable.resolve())
        self.assertEqual(selected.turn, 7)

    def test_snapshot_room_copies_observes_and_cleans_isolated_children(self):
        source = self.write_save(12)
        source_bytes = source.read_bytes()
        username = "Snapshot-abc123"
        server = self.Process(
            "3: Now accepting new client connections on port 45678.\n"
            "> Console: 'timeout' has been set to 0.\n"
            f"3: {username} has connected from localhost.\n"
            f"> {username} now observes\n",
        )
        client = self.Process(returncode=0)
        server_calls = []
        client_calls = []
        room = Path(self.directory.name) / "watch-room"
        ready = []

        def make_room(*_args, **_kwargs):
            room.mkdir()
            return str(room)

        def make_server(command, **kwargs):
            server_calls.append((command, kwargs))
            return server

        def make_client(command, **kwargs):
            client_calls.append((command, kwargs))
            return client

        with patch(
            "agent_eval.watch_room.tempfile.mkdtemp", side_effect=make_room,
        ), patch(
            "agent_eval.watch_room.secrets.token_hex", return_value="abc123",
        ), redirect_stderr(io.StringIO()) as stderr:
            result = run_snapshot_watch_room(
                self.game_id,
                credentials_path=self.credentials,
                server_binary="/tmp/freeciv-server",
                client_binary="/tmp/freeciv-sdl2",
                data_path="/tmp/data",
                settle_s=0,
                server_process_factory=make_server,
                client_process_factory=make_client,
                on_ready=ready.append,
            )
        self.assertEqual(result, 0)
        self.assertEqual(source.read_bytes(), source_bytes)
        self.assertFalse(room.exists())
        self.assertTrue(server.terminated)
        self.assertFalse(client.terminated)
        self.assertIn("set timeout 0\n", server.stdin.getvalue())
        self.assertIn(f"observe {username}\n", server.stdin.getvalue())
        server_command, server_options = server_calls[0]
        self.assertEqual(
            server_command[server_command.index("--bind") + 1], "127.0.0.1",
        )
        copied = Path(server_command[server_command.index("--file") + 1])
        self.assertNotEqual(copied, source)
        self.assertEqual(copied.parent, room)
        self.assertEqual(server_options["cwd"], room)
        self.assertEqual(client_calls[0][1]["cwd"], room)
        self.assertEqual(ready[0]["turn"], 12)
        self.assertIn("SNAPSHOT WATCH ROOM", stderr.getvalue())
        self.assertIn("not continuously live", stderr.getvalue())

    def test_missing_save_has_actionable_replay_fallback(self):
        with self.assertRaises(ClientError) as context:
            select_stable_snapshot(
                self.run_directory, self.game_id, settle_s=0,
            )
        self.assertIn(f"just replay {self.game_id}", str(context.exception))


class NativeViewerConsoleTests(unittest.TestCase):
    class Process:
        def __init__(self):
            self.stdin = None

        def poll(self):
            return None

        def terminate(self):
            return None

        def wait(self, timeout=None):
            return 0

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.launch = patch.object(Game, "_launch", autospec=True)
        self.launch.start()
        self.supervisor = Supervisor(
            self.directory.name,
            "admin",
            binary="/unused/freeciv-server",
            process_factory=lambda *args, **kwargs: None,
        )
        created = self.supervisor.create_game({
            "mode": "single",
            "places": 2,
            "turns": 2,
            "seed": 19,
            "lobby_timeout_s": 0,
        })
        self.game = self.supervisor.game(created["game_id"])
        self.process = self.Process()
        self.game.process = self.process

    def tearDown(self):
        self.supervisor.close()
        self.launch.stop()
        self.directory.cleanup()

    def test_timeout_minus_one_uses_fresh_semantic_ack_not_idle_prompt(self):
        game = self.game

        class Stdin:
            def write(self, value):
                self.value = value
                game.at_prompt = False
                game._record_server_output_line(
                    "> Console: 'timeout' has been set to -1.",
                )
                game._record_server_output_line("Game saved as turn-0042.sav.gz")
                return len(value)

            def flush(self):
                return None

        self.process.stdin = Stdin()
        self.game._send_timeout(-1)
        self.assertFalse(self.game.at_prompt)
        self.assertEqual(self.game.observed_timeout, -1)
        self.assertEqual(self.process.stdin.value, b"set timeout -1\n")

    def test_old_timeout_ack_cannot_satisfy_later_command(self):
        old_sequence = self.game._record_server_output_line(
            "> Console: 'timeout' has been set to -1.",
        )
        with self.assertRaises(SupervisorError) as context:
            self.game._wait_for_timeout(-1, old_sequence, timeout_s=0.01)
        self.assertIn("timed out", str(context.exception))

    def test_signal_timeout_line_updates_ordered_observed_state(self):
        sequence = self.game._record_server_output_line(
            "Setting timeout to 0. Autogame will stop.",
        )
        self.assertEqual(self.game.observed_timeout, 0)
        self.assertEqual(self.game.observed_timeout_sequence, sequence)

    def test_bridge_completion_marker_is_ordered_and_public_safe(self):
        generation = self.game.native_viewer_turn_response_started()
        self.game.native_viewer_turn_response_identified(generation, 42)
        sequence = self.game._record_server_output_line(
            "2: AGENT_EVAL_NATIVE_TURN_RESPONSE_DONE turn=42",
        )
        self.assertEqual(
            self.game.native_turn_response_completed_generation, generation,
        )
        self.assertEqual(self.game.native_turn_response_marker_sequence, sequence)
        self.assertEqual(self.game.native_turn_response_marker_turn, 42)
        self.assertNotIn("token", self.game.server_output_lines[-1][1].lower())


class NativeViewerLauncherTests(unittest.TestCase):
    class Process:
        def __init__(self, returncodes):
            self.returncodes = list(returncodes)
            self.returncode = None
            self.terminated = False
            self.killed = False

        def poll(self):
            if self.returncode is not None:
                return self.returncode
            if self.returncodes:
                value = self.returncodes.pop(0)
                if value is not None:
                    self.returncode = value
                return value
            return None

        def terminate(self):
            self.terminated = True
            self.returncode = -15

        def kill(self):
            self.killed = True
            self.returncode = -9

        def wait(self, timeout=None):
            return self.returncode

    def connection(self):
        return {
            "game_id": "game_" + "n" * 24,
            "lease_id": "viewer_test-lease",
            "host": "127.0.0.1",
            "port": 34567,
            "username": "Watch-test",
            "game_state": "running",
        }

    def test_launcher_logs_waits_for_ready_and_releases_after_normal_close(self):
        process = self.Process([None, None, 0])
        statuses = [
            {"state": "waiting_for_client", "game_state": "running", "active": True},
            {"state": "game_ready", "game_state": "running", "active": True},
            {"state": "disconnected", "game_state": "running", "active": False},
        ]
        commands = []

        def factory(command, **kwargs):
            commands.append((command, kwargs))
            return process

        with tempfile.TemporaryDirectory() as directory, patch(
            "agent_eval.__main__.request_native_viewer",
            return_value=self.connection(),
        ), patch(
            "agent_eval.__main__.native_viewer_status",
            side_effect=lambda *_args: statuses.pop(0),
        ), patch(
            "agent_eval.__main__.release_native_viewer",
            return_value={"released": False, "state": "inactive"},
        ) as release_mock, redirect_stderr(io.StringIO()) as stderr:
            result = run_native_viewer_client(
                "http://127.0.0.1:9997",
                self.connection()["game_id"],
                "owner",
                client_binary="/tmp/freeciv-sdl2",
                data_path="/tmp/data",
                log_dir=directory,
                lease_file=Path(directory) / "lease.json",
                poll_interval_s=0.001,
                disconnect_grace_s=0.01,
                process_factory=factory,
            )
        self.assertEqual(result, 0)
        self.assertFalse(process.terminated)
        self.assertIn("--autoconnect", commands[0][0])
        self.assertIn("--log", commands[0][0])
        self.assertIn("--debug", commands[0][0])
        self.assertEqual(
            commands[0][1]["env"]["FREECIV_DATA_PATH"],
            str(Path("/tmp/data").resolve()),
        )
        self.assertIn("Live Freeciv map ready", stderr.getvalue())
        release_mock.assert_called_once()

    def test_launcher_terminates_gui_that_falls_back_after_disconnect(self):
        process = self.Process([None])
        status = {
            "state": "disconnected",
            "game_state": "running",
            "active": False,
            "error": "server connection closed",
        }
        with tempfile.TemporaryDirectory() as directory, patch(
            "agent_eval.__main__.request_native_viewer",
            return_value=self.connection(),
        ), patch(
            "agent_eval.__main__.native_viewer_status", return_value=status,
        ), patch(
            "agent_eval.__main__.release_native_viewer",
            return_value={"released": False, "state": "inactive"},
        ):
            with self.assertRaises(ClientError) as context:
                run_native_viewer_client(
                    "http://127.0.0.1:9997",
                    self.connection()["game_id"],
                    "owner",
                    client_binary="/tmp/freeciv-sdl2",
                    data_path="/tmp/data",
                    log_dir=directory,
                    poll_interval_s=0.001,
                    disconnect_grace_s=0,
                    process_factory=lambda *_args, **_kwargs: process,
                )
        self.assertIn("disconnected", str(context.exception))
        self.assertTrue(process.terminated)

    def test_launcher_terminates_connect_timeout_with_useful_error(self):
        process = self.Process([None])
        status = {
            "state": "connect_timeout",
            "game_state": "running",
            "active": False,
            "error": "Freeciv client did not connect",
        }
        with tempfile.TemporaryDirectory() as directory, patch(
            "agent_eval.__main__.request_native_viewer",
            return_value=self.connection(),
        ), patch(
            "agent_eval.__main__.native_viewer_status", return_value=status,
        ), patch(
            "agent_eval.__main__.release_native_viewer",
            return_value={"released": False, "state": "inactive"},
        ):
            with self.assertRaises(ClientError) as context:
                run_native_viewer_client(
                    "http://127.0.0.1:9997",
                    self.connection()["game_id"],
                    "owner",
                    client_binary="/tmp/freeciv-sdl2",
                    data_path="/tmp/data",
                    log_dir=directory,
                    poll_interval_s=0.001,
                    disconnect_grace_s=0,
                    process_factory=lambda *_args, **_kwargs: process,
                )
        self.assertIn("did not connect", str(context.exception))
        self.assertTrue(process.terminated)

    def test_launcher_aborts_when_enabling_lease_becomes_inactive(self):
        connection = self.connection()
        connection["state"] = "enabling_server"
        launched = []
        inactive = {
            "state": "enabling_server",
            "game_state": "running",
            "active": False,
        }
        with tempfile.TemporaryDirectory() as directory, patch(
            "agent_eval.__main__.request_native_viewer",
            return_value=connection,
        ), patch(
            "agent_eval.__main__.native_viewer_status", return_value=inactive,
        ) as status_mock, patch(
            "agent_eval.__main__.release_native_viewer",
            return_value={"released": False, "state": "inactive"},
        ) as release_mock:
            with self.assertRaises(ClientError) as context:
                run_native_viewer_client(
                    "http://127.0.0.1:9997",
                    connection["game_id"],
                    "owner",
                    client_binary="/tmp/freeciv-sdl2",
                    data_path="/tmp/data",
                    log_dir=directory,
                    poll_interval_s=0.001,
                    disconnect_grace_s=0,
                    process_factory=lambda *_args, **_kwargs: launched.append(True),
                )
        self.assertIn("became inactive", str(context.exception))
        self.assertEqual(launched, [])
        status_mock.assert_called_once()
        release_mock.assert_called_once()


@unittest.skipUnless(
    os.environ.get("FREECIV_NATIVE_VIEWER_E2E") == "1",
    "set FREECIV_NATIVE_VIEWER_E2E=1 for real SDL viewer smoke",
)
class NativeViewerRealSmokeTests(unittest.TestCase):
    def test_real_midgame_open_close_reopen_on_ephemeral_ports(self):
        repo = Path(__file__).parents[2]
        server_binary = repo / "build-agent" / "freeciv-server"
        client_binary = repo / "build-viewer" / "freeciv-sdl2"
        self.assertTrue(server_binary.is_file())
        self.assertTrue(client_binary.is_file())
        with tempfile.TemporaryDirectory(prefix="freeciv-native-smoke-") as directory:
            supervisor = Supervisor(
                directory, "native-smoke-admin", binary=server_binary,
            )
            server = make_supervisor_server(supervisor, "127.0.0.1", 0)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                created = create_game(
                    supervisor.service_url,
                    "native-smoke-admin",
                    {
                        "mode": "single", "places": 2, "turns": 100,
                        "seed": 4242, "ruleset": "classic",
                        "objective": "native viewer smoke",
                        "action_timeout_s": 5, "lobby_timeout_s": 30,
                        "frame_interval": 0, "frame_zoom": 1,
                    },
                )
                game = supervisor.game(created["game_id"])
                join_game(
                    supervisor.service_url,
                    game.game_id,
                    created["join_token"],
                    controller_label="smoke-native-viewer",
                )
                deadline = time.monotonic() + 15
                while game.state != "running" and time.monotonic() < deadline:
                    time.sleep(0.05)
                self.assertEqual(game.state, "running")
                deadline = time.monotonic() + 10
                while (
                    game.native_turn_response_marker_sequence == 0
                    and time.monotonic() < deadline
                ):
                    time.sleep(0.05)
                self.assertGreater(game.native_turn_response_marker_sequence, 0)

                for attempt in (1, 2):
                    if attempt == 2:
                        time.sleep(max(
                            0,
                            NATIVE_VIEWER_SIGNAL_GUARD_S + 0.1
                            - (
                                time.monotonic()
                                - (game.last_native_viewer_sigint_at or 0)
                            ),
                        ))
                    lease = request_native_viewer(
                        supervisor.service_url,
                        game.game_id,
                        created["owner_token"],
                    )
                    deadline = time.monotonic() + 25
                    status = native_viewer_status(
                        supervisor.service_url, game.game_id,
                        created["owner_token"], lease["lease_id"],
                    )
                    while (
                        status["state"] == "enabling_server"
                        and time.monotonic() < deadline
                    ):
                        time.sleep(0.05)
                        status = native_viewer_status(
                            supervisor.service_url, game.game_id,
                            created["owner_token"], lease["lease_id"],
                        )
                    self.assertEqual(status["state"], "waiting_for_client")

                    client_log = Path(directory) / f"client-{attempt}.log"
                    environment = os.environ.copy()
                    environment.update({
                        "FREECIV_DATA_PATH": str(repo / "data"),
                        "SDL_VIDEODRIVER": "dummy",
                        "SDL_AUDIODRIVER": "dummy",
                    })
                    process = subprocess.Popen(
                        [
                            str(client_binary), "--autoconnect",
                            "--server", lease["host"],
                            "--port", str(lease["port"]),
                            "--name", lease["username"],
                            "--log", str(client_log), "--debug", "v",
                        ],
                        env=environment,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                    )
                    try:
                        deadline = time.monotonic() + 20
                        while time.monotonic() < deadline:
                            self.assertIsNone(process.poll())
                            status = native_viewer_status(
                                supervisor.service_url, game.game_id,
                                created["owner_token"], lease["lease_id"],
                            )
                            if status["state"] == "game_ready":
                                break
                            time.sleep(0.05)
                        self.assertEqual(status["state"], "game_ready")
                    finally:
                        process.terminate()
                        process.wait(timeout=5)

                    deadline = time.monotonic() + 10
                    while (
                        game.socket_polling_enabled
                        and time.monotonic() < deadline
                    ):
                        time.sleep(0.05)
                    self.assertFalse(game.socket_polling_enabled)
                    status = native_viewer_status(
                        supervisor.service_url, game.game_id,
                        created["owner_token"], lease["lease_id"],
                    )
                    self.assertTrue(status["timeout_restored"])
            finally:
                server.shutdown()
                server.server_close()
                thread.join(5)
                supervisor.close()


@unittest.skipUnless(
    os.environ.get("FREECIV_SNAPSHOT_WATCH_E2E") == "1",
    "set FREECIV_SNAPSHOT_WATCH_E2E=1 for real snapshot room smoke",
)
class SnapshotWatchRoomRealSmokeTests(unittest.TestCase):
    def test_generated_match_save_opens_in_disposable_global_observer_room(self):
        repo = Path(__file__).parents[2]
        server_binary = repo / "build-agent" / "freeciv-server"
        client_binary = repo / "build-viewer" / "freeciv-sdl2"
        self.assertTrue(server_binary.is_file())
        self.assertTrue(client_binary.is_file())
        with tempfile.TemporaryDirectory(
            prefix="freeciv-snapshot-room-e2e-",
        ) as directory:
            state = Path(directory) / ".agent-eval"
            supervisor = Supervisor(
                state / "runs", "snapshot-room-admin", binary=server_binary,
            )
            http_server = make_supervisor_server(supervisor, "127.0.0.1", 0)
            thread = threading.Thread(
                target=http_server.serve_forever, daemon=True,
            )
            thread.start()
            try:
                created = create_game(
                    supervisor.service_url,
                    "snapshot-room-admin",
                    {
                        "mode": "single", "places": 2, "turns": 100,
                        "seed": 5252, "ruleset": "classic",
                        "objective": "snapshot watch room smoke",
                        "action_timeout_s": 0.2, "lobby_timeout_s": 30,
                        "frame_interval": 0, "frame_zoom": 1,
                    },
                )
                game = supervisor.game(created["game_id"])
                join_game(
                    supervisor.service_url,
                    game.game_id,
                    created["join_token"],
                    controller_label="snapshot-room-smoke",
                )
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    if list((game.episode / "saves").glob("turn-*.sav*")):
                        break
                    time.sleep(0.05)
                credentials = (
                    state / "games" / game.game_id / "owner.json"
                )
                write_private_json(credentials, {
                    "schema_version": 1,
                    "service_url": supervisor.service_url,
                    "game_id": game.game_id,
                    "owner_token": created["owner_token"],
                })
                snapshot = select_stable_snapshot(
                    game.episode, game.game_id, settle_s=0.1,
                )
                source_signature = (
                    snapshot.source.stat().st_size,
                    snapshot.source.stat().st_mtime_ns,
                    snapshot.source.read_bytes(),
                )
                stop = threading.Event()
                ready = []

                def observed(value):
                    ready.append(value)
                    threading.Timer(0.5, stop.set).start()

                result = run_snapshot_watch_room(
                    game.game_id,
                    credentials_path=credentials,
                    server_binary=server_binary,
                    client_binary=client_binary,
                    data_path=repo / "data",
                    stop_event=stop,
                    on_ready=observed,
                    environment_overrides={
                        "SDL_VIDEODRIVER": "dummy",
                        "SDL_AUDIODRIVER": "dummy",
                    },
                )
                self.assertEqual(result, 0)
                self.assertEqual(ready[0]["turn"], snapshot.turn)
                self.assertEqual(ready[0]["host"], "127.0.0.1")
                self.assertNotEqual(ready[0]["port"], game.freeciv_port)
                self.assertEqual(
                    (
                        snapshot.source.stat().st_size,
                        snapshot.source.stat().st_mtime_ns,
                        snapshot.source.read_bytes(),
                    ),
                    source_signature,
                )
                self.assertIsNone(game.process.poll())
            finally:
                http_server.shutdown()
                http_server.server_close()
                thread.join(2)
                supervisor.close()


class LaunchFailureTests(unittest.TestCase):
    class Process:
        def __init__(self):
            self.stdin = io.BytesIO()
            self.stdout = io.BytesIO()
            self.terminated = False
            self.killed = False
            self.returncode = None

        def poll(self):
            return self.returncode

        def terminate(self):
            self.terminated = True

        def kill(self):
            self.killed = True
            self.returncode = -9

        def wait(self, timeout=None):
            if timeout is not None and not self.killed:
                raise subprocess.TimeoutExpired("freeciv-server", timeout)
            return self.returncode

    def test_pregame_failure_terminates_kills_and_closes_exact_child(self):
        with tempfile.TemporaryDirectory() as directory:
            process = self.Process()
            supervisor = Supervisor(
                directory, "admin", binary="/unused/freeciv",
                process_factory=lambda *args, **kwargs: process,
            )
            with patch.object(
                Game, "_wait_for_prompt",
                side_effect=SupervisorError("mock prompt failure"),
            ):
                with self.assertRaises(SupervisorError):
                    supervisor.create_game({
                        "mode": "single", "places": 2, "turns": 1,
                        "seed": 1, "lobby_timeout_s": 0,
                    })
            self.assertTrue(process.terminated)
            self.assertTrue(process.killed)
            self.assertTrue(process.stdin.closed)
            self.assertTrue(process.stdout.closed)
            self.assertEqual(supervisor.games, {})
            self.assertEqual(supervisor.reserved_game_ids, set())
            manifests = list(Path(directory).glob("game_*/manifest.json"))
            self.assertEqual(len(manifests), 1)
            manifest = json.loads(manifests[0].read_text())
            self.assertEqual(manifest["state"], "failed")
            self.assertIn("prompt failure", manifest["error"])


if __name__ == "__main__":
    unittest.main()
