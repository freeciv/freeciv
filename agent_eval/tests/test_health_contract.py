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
"""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

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


if __name__ == "__main__":
    unittest.main()
