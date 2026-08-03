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
from urllib.parse import urljoin
from unittest.mock import patch

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
from agent_eval.v2_control import V2SeatControl
from agent_eval.v2_ambiguity_trace import TRACE_DIRECTORY, TRACE_FILENAME
from agent_eval.v2_receipts import (
    ReceiptReservation,
    V2ReceiptConflict,
    V2ReceiptStoreError,
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
            f"phase_ready={1 if action_count else 0}"
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
    ]
    if action_count:
        rows.extend((
            "action slot=a0000000000000001 kind=phase.end actor=none "
            "target_tile=-1 target_unit=none transport_context=none "
            "target_tech=-1 target_government=-1 max_rate=0 "
            "target_build_kind=none target_build=-1 source_specialist=-1 target_specialist=-1 target_extra=-1 "
            "activity=none target_name=none "
            "native_rule=phase.end target_kind=player result=phase_end "
            "actor_consuming_always=0 legality=legal probability_kind=exact "
            "probability_min=200 probability_max=200 args=none",
            "action slot=a0000000000000002 kind=research.set_target actor=none "
            "target_tile=-1 target_unit=none transport_context=none "
            "target_tech=6 target_government=-1 max_rate=0 "
            "target_build_kind=none target_build=-1 source_specialist=-1 target_specialist=-1 target_extra=-1 "
            "activity=none target_name=none "
            "native_rule=research.set_target target_kind=Technology "
            "result=Research%20Target actor_consuming_always=0 legality=legal "
            "probability_kind=exact probability_min=200 probability_max=200 "
            "args=none",
            "action slot=a0000000000000003 kind=research.set_goal actor=none "
            "target_tile=-1 target_unit=none transport_context=none "
            "target_tech=4 target_government=-1 max_rate=0 "
            "target_build_kind=none target_build=-1 source_specialist=-1 target_specialist=-1 target_extra=-1 "
            "activity=none target_name=none "
            "native_rule=research.set_goal target_kind=Technology "
            "result=Research%20Goal actor_consuming_always=0 legality=legal "
            "probability_kind=exact probability_min=200 probability_max=200 "
            "args=none",
            "action slot=a0000000000000004 kind=research.set_goal actor=none "
            "target_tile=-1 target_unit=none transport_context=none "
            "target_tech=6 target_government=-1 max_rate=0 "
            "target_build_kind=none target_build=-1 source_specialist=-1 target_specialist=-1 target_extra=-1 "
            "activity=none target_name=none "
            "native_rule=research.set_goal target_kind=Technology "
            "result=Research%20Goal actor_consuming_always=0 legality=legal "
            "probability_kind=exact probability_min=200 probability_max=200 "
            "args=none",
            "action slot=a0000000000000005 kind=research.set_goal actor=none "
            "target_tile=-1 target_unit=none transport_context=none "
            "target_tech=1000 target_government=-1 max_rate=0 "
            "target_build_kind=none target_build=-1 source_specialist=-1 target_specialist=-1 target_extra=-1 "
            "activity=none target_name=none "
            "native_rule=research.set_goal target_kind=Technology "
            "result=Research%20Goal actor_consuming_always=0 legality=legal "
            "probability_kind=exact probability_min=200 probability_max=200 "
            "args=none",
            "action slot=a0000000000000006 kind=economy.set_rates actor=none "
            "target_tile=-1 target_unit=none transport_context=none "
            "target_tech=-1 target_government=-1 max_rate=70 "
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
            "x=0 y=0 hp=10 moves=3 activity=idle activity_target=-1 "
            "activity_target_name=none activity_progress=0 "
            "transport_state=untransported transporter=none "
            "transport_capacity=0 occupied=0"
        )
        rows.extend(
            (
                f"action slot=a{index:016X} kind=unit.move actor=u:10:100 "
                "target_tile=0 target_unit=none transport_context=none "
                "target_tech=-1 target_government=-1 max_rate=0 "
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
        f"tile index={index} x={index} y=0 known=2 terrain=Grassland owner=none"
        for index in range(tile_count)
    )
    if malformed:
        rows.append("native secret=must-not-escape")
    return tuple(sorted(rows))


def native_v2_scoped_rows(actor_ref):
    if actor_ref == "p:1:10":
        return (
            "action slot=a0000000000000069 kind=government.revolution "
            "actor=p:1:10 target_tile=-1 target_unit=none "
            "transport_context=none target_tech=-1 "
            "target_government=0 max_rate=0 target_build_kind=none "
            "target_build=-1 source_specialist=-1 target_specialist=-1 target_extra=-1 activity=none "
            "target_name=Anarchy native_rule=government.revolution "
            "target_kind=Government result=Revolution%20Started "
            "actor_consuming_always=0 legality=legal probability_kind=exact "
            "probability_min=200 probability_max=200 args=none",
            "action slot=a000000000000006A kind=government.change "
            "actor=p:1:10 target_tile=-1 target_unit=none "
            "transport_context=none target_tech=-1 "
            "target_government=2 max_rate=0 target_build_kind=none "
            "target_build=-1 source_specialist=-1 target_specialist=-1 target_extra=-1 activity=none "
            "target_name=Monarchy native_rule=government.change "
            "target_kind=Government result=Government%20Choice%20Recorded "
            "actor_consuming_always=0 legality=legal probability_kind=exact "
            "probability_min=200 probability_max=200 args=none",
            "action slot=a000000000000006B kind=government.change "
            "actor=p:1:10 target_tile=-1 target_unit=none "
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
            "actor=c:20:200 target_tile=-1 target_unit=none "
            "transport_context=none target_tech=-1 "
            "target_government=-1 max_rate=0 "
            "target_build_kind=improvement target_build=5 source_specialist=-1 target_specialist=-1 target_extra=-1 "
            "activity=none target_name=Granary native_rule=city.set_production "
            "target_kind=Production result=Production%20Changed "
            "actor_consuming_always=0 legality=legal probability_kind=exact "
            "probability_min=200 probability_max=200 args=none",
            "action slot=a0000000000000066 kind=city.buy_production "
            "actor=c:20:200 target_tile=-1 target_unit=none "
            "transport_context=none target_tech=-1 "
            "target_government=-1 max_rate=0 "
            "target_build_kind=unit target_build=2 source_specialist=-1 target_specialist=-1 target_extra=-1 "
            "activity=none target_name=Warriors native_rule=city.buy_production "
            "target_kind=Production result=Production%20Bought "
            "actor_consuming_always=0 legality=legal probability_kind=exact "
            "probability_min=200 probability_max=200 args=none",
        )
    if actor_ref == "u:10:100":
        return (
            "action slot=a0000000000000067 kind=unit.start_activity "
            "actor=u:10:100 target_tile=-1 target_unit=none "
            "transport_context=none target_tech=-1 "
            "target_government=-1 max_rate=0 "
            "target_build_kind=none target_build=-1 source_specialist=-1 target_specialist=-1 target_extra=-1 "
            "activity=cultivate target_name=cultivate "
            "native_rule=unit.start_activity target_kind=Worker%20Activity "
            "result=Activity%20Installed actor_consuming_always=0 "
            "legality=legal probability_kind=exact probability_min=200 "
            "probability_max=200 args=none",
            "action slot=a0000000000000068 kind=unit.start_activity "
            "actor=u:10:100 target_tile=-1 target_unit=none "
            "transport_context=none target_tech=-1 "
            "target_government=-1 max_rate=0 "
            "target_build_kind=none target_build=-1 source_specialist=-1 target_specialist=-1 target_extra=7 "
            "activity=pillage target_name=Irrigation "
            "native_rule=unit.start_activity target_kind=Worker%20Activity "
            "result=Activity%20Installed actor_consuming_always=0 "
            "legality=legal probability_kind=exact probability_min=200 "
            "probability_max=200 args=none",
        )
    return ()


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

    def public_health(self):
        return {
            "state": self.state,
            "generation": self.generation,
            "player_name": self.player_name,
            "client_state": "running" if self.state == "ready" else None,
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
        return self.factory.status_response

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
                return self.factory.read_hook(self, request_id, timeout_s)
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
        return {
            "generation": self.generation,
            "native_revision": self.factory.native_revision,
            "rows": rows,
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
        self.observation_rows_by_player = {}
        self.native_revision = 11
        self.observation_error = None
        self.read_hook = None
        self.phase_evidence_by_player = {}
        self.phase_evidence_hook = None
        self.action_count = 0
        self.scope_count = 0
        self.scope_page_count = 0
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

    def ready_v2_action(self):
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-batch-model",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        legal = game.v2_get_page(joined["agent_id"], "legal_actions", "")
        action = legal["page"]["items"][0]
        return created, game, joined, action

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
                "default": 180, "blitz": 60, "infinite": None,
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
        self.assertIn(joined["state"], {"starting", "running"})
        self.assertEqual(game.start_count, 1)
        self.assertIn("hard", game._setup_commands())
        self.assertTrue(any(
            "bridge.lua" in command for command in game._setup_commands()
        ))
        self.assertEqual(self.send_mock.call_args_list[-1].args[1], ["start"])

    def test_full_control_v2_negotiates_and_starts_only_with_ready_sidecar(self):
        with self.assertRaises(APIProblem) as invalid:
            self.supervisor._config({"control_protocol": "full-control-v3"})
        self.assertEqual(invalid.exception.status, HTTPStatus.BAD_REQUEST)
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
        self.assertEqual(joined["state"], "starting")
        self.assertIn("/v2/games/", joined["health_url"])
        self.assertTrue(game.start_sent)
        self.assertEqual(game.start_count, 1)
        self.assertIsNotNone(game.started_at)
        self.assertTrue(any(
            call.args[1] == ["start"] for call in self.send_mock.call_args_list
        ))
        manifest = json.loads((game.episode / "manifest.json").read_text())
        self.assertIn(manifest["state"], {"starting", "running"})
        self.assertEqual(
            manifest["config"]["control_protocol"], "full-control-v2",
        )
        reconnected = game.join(
            joined["agent_token"],
            supported_control_protocols=["full-control-v2", "strategic-v1"],
        )
        self.assertTrue(reconnected["reconnected"])
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
            ("default", 180.0), ("blitz", 60.0),
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
                    self.assertEqual(claim["key"], (7, 1, 2))

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

    def test_v2_phase_transition_skew_synchronizes_then_advances(self):
        _created, game, _joined = self.ready_v2_phase_game(
            multiplayer=True, places=2,
        )
        old = self.phase_evidence(game, phase=0, active_place=1)
        game._update_v2_phase_ledger(old, 10.0)
        self.assertEqual(game.v2_phase_ledger["key"], (7, 0, 2))

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
        self.assertEqual(game.v2_phase_ledger["key"], (7, 1, 2))
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
        self.assertEqual(game.v2_phase_ledger["state"], "synchronizing")
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
        self.assertEqual(game.v2_phase_ledger["key"], (9, 1, 2))
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
        # One locked observation selects and resolves; one verifies the result.
        self.assertEqual(game.sidecars[1].read_count - reads, 2)
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
        self.assertEqual(advanced.v2_phase_ledger["key"], (8, 0, 2))

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
            game.v2_phase_ledger["deadline_started_monotonic"], 10.0,
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

    def test_v2_agent_phase_end_rejection_releases_claim_and_can_retry(self):
        _created, game, joined, action = self.ready_v2_action()
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
            game.v2_phase_ledger["deadline_started_monotonic"], 10.0,
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
        self.assertIn(game.state, {"starting", "running"})
        self.assertEqual(game.start_count, 1)
        self.assertEqual(len(self.sidecar_factory.created), 2)
        reconnected = game.join(
            first["agent_token"],
            supported_control_protocols=["full-control-v2"],
        )
        self.assertTrue(reconnected["reconnected"])
        self.assertEqual(len(self.sidecar_factory.created), 2)
        self.assertEqual(game.start_count, 1)
        self.assertEqual(sum(
            call.args[1] == ["start"]
            for call in self.send_mock.call_args_list
        ), 1)
        self.assertEqual(second["place"], 2)

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

    def test_v2_current_sidecar_death_fails_game_without_ai_fallback(self):
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        game.join(
            created["join_token"], controller_label="codex-current-model",
            supported_control_protocols=["full-control-v2"],
        )
        current = self.sidecar_factory.created[-1]
        current.die()
        self.assertEqual(game.state, "failed")
        self.assertIn("sidecar_exited", game.invalid_reasons)
        self.assertTrue(game.sidecars_stopping)
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

    def test_v2_start_failure_returns_terminal_join_and_stops_sidecars(self):
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        self.send_mock.side_effect = RuntimeError("start rejected")
        joined = game.join(
            created["join_token"], controller_label="codex-start-failure",
            supported_control_protocols=["full-control-v2"],
        )
        self.assertEqual(joined["state"], "failed")
        self.assertIn("could not start game", joined["error"])
        self.assertTrue(joined["agent_token"])
        self.assertFalse(joined["v2_transport_available"])
        self.assertGreaterEqual(self.sidecar_factory.created[-1].stop_count, 1)

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
        current = self.sidecar_factory.created[-1]
        process = DeferredProcess()
        game.process = process
        with patch(
            "agent_eval.supervisor.V2_SIDECAR_COMPLETION_GRACE_S", 0.03,
        ):
            disconnect_with_cached_running(current)
            wait_until(lambda: game.state == "failed")
            wait_until(lambda: current.stop_count >= 1)
        self.assertIn("sidecar_exited", game.invalid_reasons)
        self.assertNotIn(1, game.sidecar_ready_generations)
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
            self.assertFalse(health["observation_available"])
            self.assertFalse(health["legal_actions_available"])
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

        self.assertEqual(game.state, "starting")
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

        for pre_running_state in ("lobby", "starting"):
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
            self.assertEqual(status, HTTPStatus.BAD_REQUEST)
            self.assertEqual(used["error"]["code"], "invalid_request")

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
                ("protocol_error", HTTPStatus.INTERNAL_SERVER_ERROR, "internal_error"),
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
            "citizen_tile_count=1 specialist_type_count=1"
        )
        rows.append(
            "city_tile city=c:20:200 tile=0 worked=1 free_worked=1 can_work=1"
        )
        rows.append(
            "city_specialist city=c:20:200 specialist=0 name=Entertainer "
            "count=2 can_use=1 is_default=1"
        )
        self.sidecar_factory.observation_rows = tuple(sorted(rows))
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-scoped-actions",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)

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
        self.assertEqual(self.sidecar_factory.scope_page_count, 1)

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
        self.assertEqual(city_scope["page"]["total_items"], 2)
        self.assertEqual(
            {item["subject"]["operation"] for item in city_scope["page"]["items"]},
            {"set_production", "buy_production"},
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
        ), self.assertRaises(APIProblem) as expired:
            game.v2_get_page(
                joined["agent_id"], "legal_actions",
                f"cursor={expiring_cursor}",
            )
        self.assertEqual(expired.exception.status, HTTPStatus.CONFLICT)
        self.assertEqual(
            expired.exception.payload["error"]["code"], "stale_revision",
        )
        self.assertTrue(expired.exception.payload["error"]["retryable"])
        self.assertNotIn("SENSITIVE", json.dumps(expired.exception.payload))

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
        self.assertEqual(self.sidecar_factory.scoped_action_count, 5)

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
            status, stale = raw_json_request(
                f"{root}/legal-actions?cursor={cursor}",
                joined["agent_token"],
            )
            self.assertEqual(status, HTTPStatus.CONFLICT)
            self.assertEqual(stale["error"]["code"], "stale_revision")
            self.assertTrue(stale["error"]["retryable"])

            status, consumed = raw_json_request(
                f"{root}/legal-actions?cursor={cursor}",
                joined["agent_token"],
            )
            self.assertEqual(status, HTTPStatus.BAD_REQUEST)
            self.assertEqual(consumed["error"]["code"], "invalid_request")
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)

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
            game.v2_controls[place] = V2SeatControl(
                game.game_id, joined["agent_id"], generation,
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
        _created, game, joined, action = self.ready_v2_action()
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
        self.assertEqual(self.sidecar_factory.action_count, 1)
        with self.assertRaises(APIProblem) as absent:
            game.v2_get_receipt(joined["agent_id"], "batch_second")
        self.assertEqual(absent.exception.status, HTTPStatus.NOT_FOUND)

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
        _created, game, joined, action = self.ready_v2_action()
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
        _created, game, joined, action = self.ready_v2_action()
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
        self.assertIn('vite_url="http://127.0.0.1:5173"', justfile)
        self.assertIn('"$vite_url/@vite/client"', justfile)
        self.assertIn("--port 5173 --strictPort", justfile)
        self.assertIn('repo_root="$(pwd -P)"', justfile)
        self.assertIn("from agent_eval.replay_gateway import gateway_config", justfile)
        self.assertIn("-m agent_eval.replay_gateway", justfile)
        self.assertIn('--runs-root "$runs_root"', justfile)
        self.assertIn('--cache-root "$cache_root"', justfile)
        self.assertIn('--ready-file "$gateway_ready_file"', justfile)
        self.assertIn('exec nohup env AGENT_EVAL_SERVICE_URL="$gateway_url"', justfile)
        self.assertIn('"stack_identity": sys.argv[4]', justfile)
        self.assertIn('"upstream_service_url": sys.argv[6]', justfile)
        self.assertIn("cleanup_spawned_gateway", justfile)
        self.assertIn("It will not be reused or stopped", justfile)
        self.assertNotIn("nohup npm --prefix agent_eval/viewer run dev", justfile)
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
        self.assertIn("--observation-id=OBSERVATION_ID", justfile)
        self.assertIn("controller_name=HARNESS-MODEL", justfile)
        self.assertIn("games/{game_id}/owner.json", justfile)
        self.assertEqual(
            justfile.count('--player-invite "play/.invites/{game_id}.json"'),
            2,
        )
        self.assertIn("invite game_id:", justfile)
        self.assertIn("game stage-invite", justfile)
        self.assertIn("games/{{ game_id }}", justfile)
        self.assertIn("build-viewer/build.ninja", justfile)
        self.assertIn("--ninja-args=--quiet", justfile)
        self.assertNotIn("meson setup --reconfigure build-viewer", justfile)
        self.assertEqual(
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
            self.assertIn('turn_limit="321"', rendered_max_turns)
            self.assertIn('--turns "$turn_limit"', rendered_max_turns)
            self.assertIn('--timing-mode "$timing_mode"', rendered_max_turns)
            self.assertIn("--lobby-timeout-s 0", rendered_max_turns)
            self.assertIn(
                '--player-invite "play/.invites/{game_id}.json"',
                rendered_max_turns,
            )
            for mode in ("blitz", "infinite"):
                mode_dry_run = subprocess.run(
                    ["just", "--dry-run", recipe, mode],
                    cwd=repo, text=True, capture_output=True,
                )
                self.assertEqual(
                    mode_dry_run.returncode, 0, mode_dry_run.stderr,
                )
                self.assertIn(
                    f'first="{mode}"',
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
            self.assertIn('first="4"', rendered_legacy)
            self.assertIn('second="200"', rendered_legacy)
            self.assertIn('positional_turns="$second"', rendered_legacy)
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
        self.assertIn("-m agent_eval.replay_gateway", rendered_dry_run)
        self.assertIn(
            'AGENT_EVAL_SERVICE_URL="$gateway_url"', rendered_dry_run,
        )
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
            ([], "2", "default", "5000"),
            (["2", "infinite"], "2", "infinite", "5000"),
            (["3", "blitz", "150"], "3", "blitz", "150"),
            (["infinite", "2", "150"], "2", "infinite", "150"),
            (["2", "150"], "2", "default", "150"),
            (["2", "infinite", "--max-turns", "321"], "2", "infinite", "321"),
        )
        for recipe in ("single", "multi"):
            for arguments, places, mode, turns in cases:
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

    def test_just_replay_reuses_healthy_vite_for_picker_or_direct_game(self):
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

    def test_just_replay_cleans_only_gateway_spawned_before_vite_conflict(self):
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

    def test_just_replay_fails_closed_without_listener_owner_tool(self):
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
