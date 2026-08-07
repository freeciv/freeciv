"""The supervisor↔play-client health contract, tested across the boundary.

Twice now an additive supervisor health field has taken down every fresh
player workspace, because the play client validates health with closed
field sets and the two sides share no test: ``last_recovery`` (broke
start/health/turn/wait for a live game) and then the sidecar death
forensics ``exit_signal``/``exit_signal_name``/``process_alive`` plus the
``boundary_recovery`` waiting_on kind (broke the next fresh workspace).

This module ends the class: REAL health payloads built by the REAL
supervisor — including the recovery-era states — are fed through the play
client's actual ``_validate_health``. A supervisor field the client does
not tolerate fails here, in CI, instead of in a live game's turn one.

A closed *value* set is the same hazard as a closed field set, and the
idle-phase auto-end added both kinds at once: ``phase.auto_end`` on the
health payload, and ``auto_idle`` as a third ``last_phase_end.source``
alongside ``agent`` and ``timeout``.  Both are covered below, armed and
fired through the real ledger rather than hand-built.
"""

from __future__ import annotations

import importlib.util
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from agent_eval.tests import test_supervisor as supervisor_tests

REPO_ROOT = Path(__file__).resolve().parents[2]


def _load_play_client():
    loaded = sys.modules.get("_health_contract_play_client")
    if loaded is not None:
        return loaded
    play_dir = str(REPO_ROOT / "play")
    spec = importlib.util.spec_from_file_location(
        "_health_contract_play_client", REPO_ROOT / "play" / "client.py",
    )
    module = importlib.util.module_from_spec(spec)
    # Dataclass string-annotation resolution requires the module to be in
    # sys.modules while it executes; client.py also imports its sibling
    # state_mirror by bare name, so play/ must be importable during exec.
    sys.modules["_health_contract_play_client"] = module
    sys.path.insert(0, play_dir)
    try:
        spec.loader.exec_module(module)
    except BaseException:
        sys.modules.pop("_health_contract_play_client", None)
        raise
    finally:
        try:
            sys.path.remove(play_dir)
        except ValueError:
            pass
    return module


class HealthContractTests(unittest.TestCase):
    """Real supervisor payloads through the real play-client validator."""

    setUp = supervisor_tests.SupervisorTests.setUp
    tearDown = supervisor_tests.SupervisorTests.tearDown
    create = supervisor_tests.SupervisorTests.create
    ready_v2_phase_game = supervisor_tests.SupervisorTests.ready_v2_phase_game
    _seed_v2_phase = supervisor_tests.SupervisorTests._seed_v2_phase
    _mark_v2_running = vars(supervisor_tests.SupervisorTests)["_mark_v2_running"]
    phase_evidence = vars(supervisor_tests.SupervisorTests)["phase_evidence"]
    ready_v2_action = supervisor_tests.SupervisorTests.ready_v2_action
    v2_batch = vars(supervisor_tests.SupervisorTests)["v2_batch"]

    def _session_for(self, created, game, joined) -> dict:
        return {
            "game_id": game.game_id,
            "agent_id": joined["agent_id"],
            "controller_label": joined.get("controller_label")
            or "controller-1",
            "place": None,
            "seat_id": None,
            "player_name": None,
            "objective": game.config.get("objective"),
            "max_turns": game.config.get("turns"),
            "turns_remaining": None,
        }

    def _validate(self, game, created, joined) -> dict:
        client = _load_play_client()
        payload = game.v2_health(joined["agent_id"])
        session = self._session_for(created, game, joined)
        try:
            return client._validate_health(payload, session)
        except client.PlayerError as exc:
            self.fail(
                "the play client rejected a real supervisor health "
                f"payload: {exc}\npayload keys: {sorted(payload)}\n"
                f"sidecar keys: {sorted(payload.get('sidecar') or {})}"
            )

    def test_running_game_health_passes_the_play_validator(self):
        created, game, joined = self.ready_v2_phase_game()
        self._validate(game, created, joined[0])

    def test_wedged_and_recovering_health_passes_the_play_validator(self):
        created, game, joined = self.ready_v2_phase_game()
        place = joined[0]["place"]
        with game.condition:
            game.v2_wedged_places[place] = {
                "trigger": "sidecar_exit",
                "detected_at": 0.0,
                "generation": 1,
                "forensics": {},
                "death_context": {},
            }
        cleaned = self._validate(game, created, joined[0])
        phase = cleaned.get("phase") or {}
        waiting = phase.get("waiting_on")
        if waiting is not None:
            self.assertEqual(waiting["kind"], "boundary_recovery")

    def test_phase_end_event_health_passes_the_play_validator(self):
        # The fourth drift (last_phase_end gained "incarnation") slipped
        # past the first version of this suite because the harness game
        # had no phase-end event. Mint one through the real batch +
        # ledger machinery, then validate the whole payload.
        _created, game, joined, action = self.ready_v2_action()
        status, _receipt = game.v2_submit_batch(
            joined["agent_id"],
            self.v2_batch(game, joined, action, "health_contract_end"),
        )
        self.assertEqual(status, 200)
        _claim, failed = game._update_v2_phase_ledger(
            self.phase_evidence(game, turn=8, phase=0, active_place=1),
            10.0,
        )
        self.assertFalse(failed)
        payload = game.v2_health(joined["agent_id"])
        self.assertIsNotNone(
            payload.get("last_phase_end"),
            "the harness game must carry a phase-end event for this test",
        )
        client = _load_play_client()
        session = self._session_for(None, game, joined)
        try:
            client._validate_health(payload, session)
        except client.PlayerError as exc:
            self.fail(
                "the play client rejected a real supervisor health "
                f"payload with a phase-end event: {exc}\n"
                f"event keys: {sorted(payload['last_phase_end'])}"
            )

    def test_recovery_journal_event_health_passes_the_play_validator(self):
        created, game, joined = self.ready_v2_phase_game()
        first = joined[0]
        game._record_v2_recovery_event(
            place=first["place"],
            seat_id=f"place-{first['place']}",
            turn=1,
            attempt=1,
            kind="sidecar_reattach",
            trigger="sidecar_exit",
            outcome="recovered",
            sidecar_generation=2,
            recovered_to_turn=None,
            rewound_applied_actions=False,
            exit_code=None,
            exit_signal=9,
            client_state="running",
        )
        cleaned = self._validate(game, created, first)
        recovery = cleaned.get("last_recovery")
        self.assertIsNotNone(
            recovery,
            "the recovery event must reach health.last_recovery",
        )
        self.assertEqual(recovery["trigger"], "sidecar_exit")


class AutoEndHealthContractTests(unittest.TestCase):
    """The idle-phase auto-end's two additions, through the real validator."""

    setUp = supervisor_tests.SupervisorTests.setUp
    tearDown = supervisor_tests.SupervisorTests.tearDown
    create = supervisor_tests.SupervisorTests.create
    phase_evidence = vars(
        supervisor_tests.SupervisorTests,
    )["phase_evidence"]
    _mark_v2_running = vars(
        supervisor_tests.SupervisorTests,
    )["_mark_v2_running"]
    running_phase = supervisor_tests.V2AutoEndIdlePhaseTests.running_phase
    inline_workers = vars(
        supervisor_tests.V2AutoEndIdlePhaseTests,
    )["inline_workers"]
    tick = supervisor_tests.V2AutoEndIdlePhaseTests.tick
    _session_for = HealthContractTests._session_for

    def _validate(self, game, joined) -> dict:
        client = _load_play_client()
        payload = game.v2_health(joined["agent_id"])
        try:
            return client._validate_health(
                payload, self._session_for(None, game, joined),
            )
        except client.PlayerError as exc:
            self.fail(
                "the play client rejected a real supervisor health "
                f"payload: {exc}\nphase keys: "
                f"{sorted(payload.get('phase') or {})}"
            )

    def test_an_armed_auto_end_passes_the_play_validator(self):
        clock = supervisor_tests._Clock()
        with patch("agent_eval.supervisor.time.monotonic", clock):
            _created, game, joined, _evidence = self.running_phase(clock)
            unarmed = self._validate(game, joined[0])
            self.assertEqual(
                unarmed["phase"]["auto_end"]["armed"], False,
                "the auto_end block must survive the client's field filter",
            )
            self.tick(game, clock)
            armed = self._validate(game, joined[0])
        self.assertTrue(armed["phase"]["auto_end"]["armed"])
        self.assertIsNotNone(armed["phase"]["auto_end"]["remaining_s"])

    def test_an_auto_idle_phase_end_event_passes_the_play_validator(self):
        clock = supervisor_tests._Clock()
        with patch("agent_eval.supervisor.time.monotonic", clock):
            _created, game, joined, _evidence = self.running_phase(clock)
            self.tick(game, clock)
            self.tick(
                game, clock, supervisor_tests.V2_AUTO_END_IDLE_GRACE_S,
            )
            self.assertEqual(
                game.v2_phase_ledger["end"]["source"], "auto_idle",
            )
            _claim, failed = game._update_v2_phase_ledger(
                self.phase_evidence(game, turn=8, phase=0, active_place=1),
                clock.advance(1.0),
            )
            self.assertFalse(failed)
            cleaned = self._validate(game, joined[0])
        event = cleaned["last_phase_end"]
        self.assertIsNotNone(
            event, "the auto-end must reach health.last_phase_end",
        )
        self.assertEqual(event["source"], "auto_idle")


class MultiplayerHealthContractTests(unittest.TestCase):
    """The PvP additions, through the real validator on real payloads.

    `phase.prior_end` is the fifth additive health field in this repo's
    history and the fourth to be added after an earlier one took down every
    fresh workspace. It is built here by a real two-seat game ending a real
    phase, and validated by the real play client.
    """

    setUp = supervisor_tests.SupervisorTests.setUp
    tearDown = supervisor_tests.SupervisorTests.tearDown
    create = supervisor_tests.SupervisorTests.create
    ready_v2_phase_game = supervisor_tests.SupervisorTests.ready_v2_phase_game
    _seed_v2_phase = supervisor_tests.SupervisorTests._seed_v2_phase
    _mark_v2_running = vars(
        supervisor_tests.SupervisorTests,
    )["_mark_v2_running"]
    phase_evidence = vars(supervisor_tests.SupervisorTests)["phase_evidence"]
    v2_batch = vars(supervisor_tests.SupervisorTests)["v2_batch"]
    pvp_game = supervisor_tests.V2PvPWaitSurfaceTests.pvp_game
    end_seat_one_phase = (
        supervisor_tests.V2PvPWaitSurfaceTests.end_seat_one_phase
    )
    _session_for = HealthContractTests._session_for

    def _validate(self, game, joined) -> dict:
        client = _load_play_client()
        payload = game.v2_health(joined["agent_id"])
        try:
            return client._validate_health(
                payload, self._session_for(None, game, joined),
            )
        except client.PlayerError as exc:
            self.fail(
                "the play client rejected a real supervisor health "
                f"payload: {exc}\nphase keys: "
                f"{sorted(payload.get('phase') or {})}\nprior_end keys: "
                f"{sorted((payload.get('phase') or {}).get('prior_end') or {})}"
            )

    def test_a_blocked_seats_health_passes_the_play_validator(self):
        _created, game, joined = self.pvp_game()
        blocked = self._validate(game, joined[1])
        waiting = blocked["phase"]["waiting_on"]
        self.assertEqual(waiting["kind"], "other_seat")
        # The name survives the client's field filter, which is the whole
        # point of naming it: it was reachable only via --json before.
        self.assertIn("AgentPlace1", waiting["summary"])
        self.assertEqual(waiting["seats"][0]["player_name"], "AgentPlace1")
        self.assertFalse(waiting["seats"][0]["is_self"])
        self.assertIsNone(blocked["phase"]["prior_end"])

    def test_a_prior_phase_end_passes_the_play_validator_and_renders(self):
        client = _load_play_client()
        _created, game, joined = self.pvp_game()
        self.end_seat_one_phase(game, joined)
        cleaned = self._validate(game, joined[1])
        prior = cleaned["phase"]["prior_end"]
        self.assertIsNotNone(
            prior, "the other seat's phase end must reach health.phase",
        )
        self.assertEqual(prior["place"], 1)
        self.assertEqual(prior["source"], "agent")
        self.assertEqual(prior["orders_submitted"], 1)
        # It renders, rather than merely validating.
        line = client._prior_end_line(cleaned["phase"])
        self.assertIn("opponent seat 1 AgentPlace1 (controller-1)", line)
        self.assertIn(line, "\n".join(client._render_health(cleaned)))

    def test_a_timeout_with_no_orders_reaches_the_rendered_line(self):
        """The string this whole surface exists for, end to end."""
        client = _load_play_client()
        _created, game, joined = self.pvp_game()
        game.v2_health(joined[1]["agent_id"])
        game.v2_phase_event_journal.append({
            "sequence": 1, "turn": 7, "phase": 1, "place": 1,
            "seat_id": "place-1", "player_name": "AgentPlace1",
            "player_color": "#0067A5", "controller_label": "controller-1",
            "controller_type": "external", "source": "timeout",
            "receipt_state": "applied", "resolution": "advanced",
            "deadline_started_at": 1000.0, "ended_at": 1600.0,
            "elapsed_s": 600.0,
        })
        cleaned = self._validate(game, joined[1])
        self.assertEqual(
            cleaned["phase"]["prior_end"]["orders_submitted"], 0,
        )
        self.assertIn(
            "in 10m0s (timeout — they issued no orders)",
            client._prior_end_line(cleaned["phase"]),
        )

    def test_a_wait_envelope_from_a_blocked_seat_passes_the_validator(self):
        """The wait path validates health too, and it is where a drift bites:
        a rejected envelope there surfaces after an applied phase end."""
        client = _load_play_client()
        _created, game, joined = self.pvp_game()
        envelope = game._v2_wait_response(
            joined[1]["agent_id"], "timeout",
            game.v2_health(joined[1]["agent_id"]), None,
        )
        session = self._session_for(None, game, joined[1])
        try:
            wake = client._validate_wait_response(
                envelope, session, until="phase", after_state_token=None,
            )
        except client.PlayerError as exc:
            self.fail(f"the play client rejected a real wait envelope: {exc}")
        self.assertEqual(
            client._wait_exit_code(wake), client.V2_WAIT_EXIT_RETRY,
        )
        holder = client._holder_seat(wake["health"]["phase"])
        self.assertIsNotNone(holder, "the blocked wake must name the holder")
        self.assertEqual(holder["player_name"], "AgentPlace1")

    def test_a_boundary_recovered_wake_passes_the_validator(self):
        """The reason the server could always send and the client could not
        read. It reached agents as `await failed:` after an applied end."""
        client = _load_play_client()
        _created, game, joined = self.pvp_game()
        envelope = game._v2_wait_response(
            joined[0]["agent_id"], "boundary_recovered",
            game.v2_health(joined[0]["agent_id"]), None,
        )
        session = self._session_for(None, game, joined[0])
        try:
            wake = client._validate_wait_response(
                envelope, session, until="phase", after_state_token=None,
            )
        except client.PlayerError as exc:
            self.fail(
                f"the play client rejected a real boundary_recovered wake: "
                f"{exc}"
            )
        self.assertEqual(wake["wake_reason"], "boundary_recovered")
        self.assertEqual(
            client._wait_exit_code(wake), client.V2_WAIT_EXIT_ACTIVE,
        )

    def test_your_own_order_count_passes_the_play_validator(self):
        """The sixth additive health field, and the one an alarm depends on.

        `last_phase_end.orders_submitted` is what lets the monitor say "your
        phase opened and died with no orders in it" as a fact rather than an
        inference from `source=timeout`.
        """
        client = _load_play_client()
        _created, game, joined = self.pvp_game()
        self.end_seat_one_phase(game, joined)
        cleaned = self._validate(game, joined[0])
        event = cleaned["last_phase_end"]
        self.assertIsNotNone(event)
        self.assertEqual(event["orders_submitted"], 1)
        # And a payload from a supervisor that predates the field still
        # validates, which is the drift that broke this repo four times.
        payload = game.v2_health(joined[0]["agent_id"])
        payload["last_phase_end"] = {
            key: value for key, value in payload["last_phase_end"].items()
            if key != "orders_submitted"
        }
        older = client._validate_health(
            payload, self._session_for(None, game, joined[0]),
        )
        self.assertNotIn("orders_submitted", older["last_phase_end"])

    def test_the_missed_turn_alarm_reads_a_real_supervisor_payload(self):
        """End to end: a real timed-out phase becomes the monitor's alarm."""
        client = _load_play_client()
        _created, game, joined = self.pvp_game()
        game.v2_health(joined[0]["agent_id"])
        game.v2_phase_event_journal.append({
            "sequence": 1, "turn": 7, "phase": 1, "place": 1,
            "seat_id": "place-1", "player_name": "AgentPlace1",
            "player_color": "#0067A5", "controller_label": "controller-1",
            "controller_type": "external", "source": "timeout",
            "receipt_state": "applied", "resolution": "advanced",
            "deadline_started_at": 1000.0, "ended_at": 1600.0,
            "elapsed_s": 600.0,
        })
        cleaned = self._validate(game, joined[0])
        missed = client._missed_phase(cleaned, None)
        self.assertIsNotNone(missed, "an unannounced timeout is a missed turn")
        self.assertEqual(
            client._missed_line(missed, 1, None),
            "T7 | MISSED | your phase t7/p1 opened and was ended by timeout "
            "after 600s — you issued no orders",
        )

    def test_the_marker_file_is_written_from_a_real_health_payload(self):
        """P3's projection is a renderer over validated health; a supervisor
        field it cannot read would blank a value rather than fail loudly."""
        client = _load_play_client()
        mirror_module = sys.modules["state_mirror"]
        _created, game, joined = self.pvp_game()
        cleaned = self._validate(game, joined[1])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            (root / ".sessions").mkdir(parents=True)
            mirror = root / ".sessions" / "game" / "seat"
            # `state_mirror` reaches the running client through
            # `sys.modules["client"]`; under this suite that name belongs to
            # the supervisor's own client module, so bind it for the call.
            with patch.object(client, "ROOT", root), patch.dict(
                sys.modules, {"client": client},
            ), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                mirror_module.update_from_health(mirror, "health", cleaned)
            value = json.loads(
                (mirror / "state" / "phase.json").read_text(encoding="utf-8"),
            )
        self.assertEqual(value["schema_version"], 1)
        self.assertFalse(value["active"])
        self.assertEqual(value["turn"], 7)
        self.assertEqual(value["phase"], 1)
        self.assertEqual(value["holder"], {
            "place": 1, "seat_id": "place-1",
            "player_name": "AgentPlace1",
            "controller_label": "controller-1",
        })
        self.assertIsNotNone(value["deadline_s_left"])
        # A closed schema: exactly the documented keys, and nothing opaque.
        self.assertEqual(set(value), {
            "schema_version", "updated_at", "game_state", "turn", "phase",
            "state", "active", "held_s", "deadline_s_left", "holder",
            "announced",
        })
        self.assertEqual(client.V2_SHOW_FILES["phase"], ("state", "phase.json"))


if __name__ == "__main__":
    unittest.main()
