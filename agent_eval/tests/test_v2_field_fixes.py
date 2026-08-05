"""Regression tests for the second field report's server-side defects.

Three defects observed in a live 28-turn full-control-v2 game:

* a schema-valid governance proposal was advertised as legal and then refused
  as a bare ``illegal_action`` carrying no reason at all;
* the game sat in ``phase_not_ready`` and every ``wait`` timed out without
  saying what it was waiting for;
* ``player.surrender`` returned ``applied`` while the game kept running and
  the seat had no way to see its own standing.
"""

from __future__ import annotations

import time
import unittest
from http import HTTPStatus
from unittest.mock import patch

from agent_eval.full_control_v2 import (
    REJECTION_LAYERS,
    REJECTION_REASONS,
    FullControlSchemaError,
    rejection,
    rejection_message,
    structured_error,
    validate_command_receipt,
    validate_rejection,
)
from agent_eval.headless_sidecar import SidecarActionNotAccepted, SidecarError
from agent_eval.supervisor import APIProblem, Game
from agent_eval.v2_control import V2ControlError, V2SeatControl

# Imported as modules, not names: pulling ``SupervisorTests`` into this
# module's namespace would make the loader collect and re-run its whole suite
# here as well.
from agent_eval.tests import test_supervisor as supervisor_tests
from agent_eval.tests import test_v2_control as control_tests

_complete_v2_action_row = supervisor_tests._complete_v2_action_row
native_v2_rows = supervisor_tests.native_v2_rows
_action = control_tests._action
governance_rows = control_tests.governance_rows
observation = control_tests.observation


REVISION = {"turn": 3, "revision": 4, "state_token": "state_token_test"}

SURRENDER_ROW = _complete_v2_action_row(
    "action slot=a00000000000000FD kind=player.surrender "
    "actor=p:1:10 target_tile=-1 source_city=none "
    "destination_city=none target_unit=none transport_context=none "
    "target_tech=-1 vote_no=-1 target_government=-1 max_rate=0 "
    "target_build_kind=none target_build=-1 source_specialist=-1 "
    "target_specialist=-1 target_extra=-1 activity=none "
    "target_name=self native_rule=player.surrender "
    "target_kind=Player result=Surrender%20Recorded "
    "actor_consuming_always=0 legality=legal "
    "probability_kind=exact probability_min=200 probability_max=200 args=none"
)


class V2Harness(unittest.TestCase):
    """The supervisor suite's fake-sidecar harness, without its own tests.

    Subclassing ``SupervisorTests`` would re-run all ~350 of its cases under
    every class here, so the handful of helpers these tests need are bound
    directly instead.
    """

    setUp = supervisor_tests.SupervisorTests.setUp
    tearDown = supervisor_tests.SupervisorTests.tearDown
    create = supervisor_tests.SupervisorTests.create
    ready_v2_action = supervisor_tests.SupervisorTests.ready_v2_action
    ready_v2_non_phase_action = supervisor_tests.SupervisorTests.ready_v2_non_phase_action
    ready_v2_phase_game = supervisor_tests.SupervisorTests.ready_v2_phase_game
    _seed_v2_phase = supervisor_tests.SupervisorTests._seed_v2_phase
    _mark_v2_running = vars(supervisor_tests.SupervisorTests)["_mark_v2_running"]
    v2_batch = vars(supervisor_tests.SupervisorTests)["v2_batch"]
    phase_evidence = vars(supervisor_tests.SupervisorTests)["phase_evidence"]


def receipt(state: str, error: dict[str, object] | None) -> dict[str, object]:
    return {
        "schema_version": 2,
        "control_protocol": "full-control-v2",
        "game_id": "game_test",
        "agent_id": "agent_test",
        "batch_id": "batch_test",
        "receipt_state": state,
        "idempotent": False,
        "state_revision": REVISION,
        "error": error,
        "observation": None,
    }


class RejectionVocabularyTests(unittest.TestCase):
    """The closed attribution vocabulary carried on every rejected receipt."""

    def test_every_reason_has_a_server_authored_message(self):
        for reason in sorted(REJECTION_REASONS):
            with self.subTest(reason=reason):
                message = rejection_message(
                    rejection("preflight", reason),
                )
                self.assertTrue(message.strip())
                self.assertLessEqual(len(message), 500)

    def test_native_attribution_is_appended_and_bounded(self):
        self.assertIn(
            "(native result: POSTCONDITION_NOT_MET)",
            rejection_message(rejection(
                "native_dispatch", "postcondition_not_met",
                native_reason="POSTCONDITION_NOT_MET",
            )),
        )
        self.assertIn(
            "(native code: native_bad_argument)",
            rejection_message(rejection(
                "native_preflight", "native_refused",
                native_code="native_bad_argument",
            )),
        )

    def test_only_closed_tokens_are_accepted(self):
        valid = rejection("catalog", "action_not_advertised")
        self.assertEqual(validate_rejection(valid), valid)
        for invalid in (
            {**valid, "layer": "somewhere"},
            {**valid, "reason": "because"},
            # Free text must never reach the attribution, in any field.
            {**valid, "layer": "native_dispatch",
             "native_reason": "failed at /private/episode/path"},
            {**valid, "layer": "native_preflight",
             "native_code": "slot-41 token=SECRET"},
            # Non-native layers may not claim native attribution at all.
            {**valid, "native_code": "native_error"},
            dict(valid, extra=1),
        ):
            with self.subTest(invalid=invalid):
                with self.assertRaises(FullControlSchemaError):
                    validate_rejection(invalid)

    def test_layers_cover_each_stage_of_the_pipeline(self):
        self.assertEqual(REJECTION_LAYERS, {
            "schema", "revision", "catalog", "arguments", "preflight",
            "native_preflight", "native_dispatch", "store", "runtime",
        })

    def test_a_rejected_receipt_without_attribution_is_a_contract_violation(self):
        attributed = structured_error(
            "illegal_action",
            "The command was rejected.",
            retryable=False,
            details={"rejection": rejection("catalog", "action_not_advertised")},
            state_revision=REVISION,
        )
        self.assertEqual(
            validate_command_receipt(receipt("rejected", attributed))["error"],
            attributed,
        )
        bare = structured_error(
            "illegal_action",
            "The command was rejected.",
            retryable=False,
            state_revision=REVISION,
        )
        with self.assertRaisesRegex(
            FullControlSchemaError, "must attribute its refusal",
        ):
            validate_command_receipt(receipt("rejected", bare))


class PhaseControlProposalTests(unittest.TestCase):
    """Governance proposals that would strand the seat's own phase."""

    def setUp(self) -> None:
        self.control = V2SeatControl("game_test", "agent_test", 1)

    def _resolve(self, rows, name, arguments):
        observed = observation(tuple(sorted(rows)))
        payload = self.control.legal_actions_page(observed)
        action = next(
            item for item in payload["page"]["items"]
            if item["subject"].get("target", {})
            and item["subject"]["operation"] == "propose_server_setting"
            and item["subject"]["target"]["name"] == name
        )
        return self.control.resolve_action(
            observed, payload["state_revision"], action["action_id"],
            arguments,
        )

    @staticmethod
    def _setting_row(slot, name, setting_type, args, *, current, value,
                     minimum=0, maximum=1):
        return _action(
            slot, "player.propose_server_setting", "p:1:10", -1,
            f"player.propose_server_setting_{setting_type}",
            "Server Setting Vote", "Vote Proposed Or Setting Applied", 0,
            args, server_setting_id=slot, server_setting_type=setting_type,
            server_setting_min=minimum, server_setting_max=maximum,
            server_setting_current=current, server_setting_value=value,
            target_name=name,
        )

    def test_enabling_fixedlength_is_refused_with_a_named_reason(self):
        # Freeciv's own can_end_turn() returns false unconditionally while
        # fixedlength is set, and that flag is exactly what the native
        # boundary reports as this seat's phase readiness. Applying the
        # proposal would leave the seat unable to ever end its phase.
        rows = list(governance_rows())
        rows.append(self._setting_row(
            0x520, "fixedlength", "boolean", "none", current=0, value=1,
        ))
        with self.assertRaises(V2ControlError) as refused:
            self._resolve(rows, "fixedlength", {})
        self.assertEqual(refused.exception.code, "invalid_request")
        self.assertEqual(
            refused.exception.details["rejection_reason"],
            "phase_control_conflict",
        )

    def test_disabling_fixedlength_stays_legal_so_a_game_can_recover(self):
        rows = list(governance_rows())
        rows.append(self._setting_row(
            0x521, "fixedlength", "boolean", "none", current=1, value=0,
        ))
        self.assertEqual(
            self._resolve(rows, "fixedlength", {}).native_arguments, "-",
        )

    def test_changing_phasemode_is_refused(self):
        rows = list(governance_rows())
        rows.append(self._setting_row(
            0x522, "phasemode", "enum", "none",
            current=0, value=1, maximum=2,
        ))
        with self.assertRaises(V2ControlError) as refused:
            self._resolve(rows, "phasemode", {})
        self.assertEqual(
            refused.exception.details["rejection_reason"],
            "phase_control_conflict",
        )

    def test_unrelated_settings_are_unaffected(self):
        self.assertEqual(
            self._resolve(
                list(governance_rows()), "turn_timeout", {"value": 30},
            ).native_arguments,
            "value=30",
        )

    def test_setting_argument_refusals_name_their_contract(self):
        rows = list(governance_rows())
        for arguments, expected in (
            ({"value": 301}, "server_setting_out_of_range"),
            ({"value": -1}, "server_setting_out_of_range"),
            ({"value": 60}, "server_setting_unchanged"),
        ):
            with self.subTest(arguments=arguments):
                with self.assertRaises(V2ControlError) as refused:
                    self._resolve(rows, "turn_timeout", arguments)
                self.assertEqual(
                    refused.exception.details["rejection_reason"], expected,
                )


class ReceiptAttributionTests(V2Harness):
    """Every refusal an agent can receive names the layer that refused it."""

    @staticmethod
    def _attribution(payload):
        return payload["error"]["details"]["rejection"]

    def test_native_preflight_rejection_carries_its_native_code(self):
        _created, game, joined, action = self.ready_v2_action()
        self.sidecar_factory.action_error = SidecarActionNotAccepted(
            "native_bad_argument",
        )
        status, receipt = game.v2_submit_batch(
            joined["agent_id"],
            self.v2_batch(game, joined, action, "batch_native_preflight"),
        )
        self.assertEqual(status, HTTPStatus.UNPROCESSABLE_ENTITY)
        self.assertEqual(receipt["receipt_state"], "rejected")
        self.assertEqual(receipt["error"]["error"]["code"], "illegal_action")
        self.assertEqual(self._attribution(receipt["error"]), {
            "layer": "native_preflight",
            "reason": "native_bad_argument",
            "native_code": "native_bad_argument",
            "native_reason": None,
        })
        # The generic prose is replaced by the reason's own sentence.
        self.assertIn(
            "refused the action's argument",
            receipt["error"]["error"]["message"],
        )

    def test_native_dispatch_postcondition_names_the_native_result(self):
        _created, game, joined, action = self.ready_v2_non_phase_action()

        def unapplied(
            sidecar, request_id, action_slot, arguments, timeout_s,
            expected_revision, on_accepted,
        ):
            if on_accepted is not None:
                on_accepted({
                    "request_id": request_id,
                    "accepted": True,
                    "accepted_revision": expected_revision,
                })
            revision = max(
                self.sidecar_factory.native_revision, expected_revision or 1,
            ) + 1
            self.sidecar_factory.native_revision = revision
            return {
                "request_id": request_id,
                "accepted": True,
                "applied": False,
                "status": "rejected",
                "reason": "POSTCONDITION_NOT_MET",
                "accepted_revision": expected_revision,
                "result_revision": revision,
                "observation_selector": None,
            }

        self.sidecar_factory.action_hook = unapplied
        status, receipt = game.v2_submit_batch(
            joined["agent_id"],
            self.v2_batch(game, joined, action, "batch_postcondition"),
        )
        self.assertEqual(status, HTTPStatus.UNPROCESSABLE_ENTITY)
        self.assertEqual(self._attribution(receipt["error"]), {
            "layer": "native_dispatch",
            "reason": "postcondition_not_met",
            "native_code": None,
            "native_reason": "POSTCONDITION_NOT_MET",
        })
        # This is exactly the case the field report hit: a governance
        # proposal that dispatches and then does not take hold.
        self.assertIn(
            "did not take hold", receipt["error"]["error"]["message"],
        )

    def test_transport_rejection_still_names_a_layer(self):
        _created, game, joined, action = self.ready_v2_action()
        self.sidecar_factory.action_error = SidecarError("invalid_action")
        _status, receipt = game.v2_submit_batch(
            joined["agent_id"],
            self.v2_batch(game, joined, action, "batch_transport"),
        )
        self.assertEqual(receipt["receipt_state"], "rejected")
        attribution = self._attribution(receipt["error"])
        self.assertEqual(attribution["layer"], "native_preflight")
        self.assertEqual(attribution["reason"], "native_bad_request")

    def test_schema_and_revision_refusals_are_attributed(self):
        _created, game, joined, action = self.ready_v2_action()
        batch = self.v2_batch(game, joined, action, "batch_schema")
        del batch["commands"]
        with self.assertRaises(APIProblem) as malformed:
            game.v2_submit_batch(joined["agent_id"], batch)
        self.assertEqual(
            self._attribution(malformed.exception.payload)["layer"], "schema",
        )

        stale = self.v2_batch(game, joined, action, "batch_stale")
        stale["state_revision"] = {
            **stale["state_revision"], "revision": 1, "state_token": "stale_1",
        }
        with self.assertRaises(APIProblem) as expired:
            game.v2_submit_batch(joined["agent_id"], stale)
        self.assertEqual(
            self._attribution(expired.exception.payload),
            {
                "layer": "revision",
                "reason": "revision_stale",
                "native_code": None,
                "native_reason": None,
            },
        )

    def test_expired_action_id_is_attributed_to_the_catalog(self):
        _created, game, joined, action = self.ready_v2_action()
        batch = self.v2_batch(game, joined, action, "batch_unknown_action")
        batch["commands"][0]["action_id"] = "action_never_advertised"
        with self.assertRaises(APIProblem) as expired:
            game.v2_submit_batch(joined["agent_id"], batch)
        self.assertEqual(expired.exception.status, HTTPStatus.GONE)
        self.assertEqual(self._attribution(expired.exception.payload), {
            "layer": "catalog",
            "reason": "action_not_advertised",
            "native_code": None,
            "native_reason": None,
        })

    def test_every_native_error_code_maps_to_an_attributed_rejection(self):
        # No sidecar code, known or not, may produce a receipt whose refusal
        # cannot be attributed to a layer.
        for code in (
            "native_bad_argument", "native_bad_request", "native_error",
            "invalid_action", "invalid_argument", "invalid_request",
            "totally_unknown_native_code",
        ):
            with self.subTest(code=code):
                attribution = Game._v2_native_rejection(
                    code, error_code="illegal_action",
                )
                self.assertEqual(attribution["layer"], "native_preflight")
                self.assertIn(attribution["reason"], REJECTION_REASONS)


class WaitingOnTests(V2Harness):
    """``phase_not_ready`` and ``wait`` timeouts name their blocker."""

    def _seed(self, game, **kwargs):
        game._update_v2_phase_ledger(
            self.phase_evidence(game, **kwargs), time.monotonic(),
        )

    def test_phase_not_ready_names_the_seat_and_the_fixedlength_cause(self):
        _created, game, joined = self.ready_v2_phase_game()
        self._seed(game, active_place=1, ready=False)
        health = game.v2_health(joined[0]["agent_id"])

        self.assertEqual(health["phase"]["state"], "phase_not_ready")
        waiting = health["phase"]["waiting_on"]
        self.assertEqual(waiting["kind"], "seat_not_ready")
        self.assertEqual(
            [seat["place"] for seat in waiting["seats"]], [1],
        )
        self.assertTrue(waiting["seats"][0]["is_self"])
        self.assertEqual(waiting["seats"][0]["standing"], "active")
        # The exact engine-level cause of the field report's wedge.
        self.assertIn("fixedlength", waiting["summary"])
        self.assertIsNotNone(waiting["waiting_s"])

    def test_waiting_on_is_absent_only_when_this_seat_may_act(self):
        _created, game, joined = self.ready_v2_phase_game(
            multiplayer=True, places=2,
        )
        self._seed(game, active_place=1, ready=True)
        first = game.v2_health(joined[0]["agent_id"])
        self.assertEqual(first["phase"]["state"], "awaiting_agent")
        self.assertIsNone(first["phase"]["waiting_on"])

        # The seat that does not hold the phase is told who does.
        second = game.v2_health(joined[1]["agent_id"])
        waiting = second["phase"]["waiting_on"]
        self.assertEqual(waiting["kind"], "other_seat")
        self.assertEqual([seat["place"] for seat in waiting["seats"]], [1])
        self.assertFalse(waiting["seats"][0]["is_self"])

    def test_native_phase_and_synchronizing_are_distinguished(self):
        _created, game, joined = self.ready_v2_phase_game(
            multiplayer=True, places=2,
        )
        agent_id = joined[0]["agent_id"]

        self._seed(game, active_place=None)
        self.assertEqual(
            game.v2_health(agent_id)["phase"]["waiting_on"]["kind"],
            "native_phase",
        )

        with game.condition:
            game.v2_phase_ledger["state"] = "synchronizing"
            game.v2_phase_ledger["evidence"] = {}
        waiting = game.v2_health(agent_id)["phase"]["waiting_on"]
        self.assertEqual(waiting["kind"], "phase_synchronization")
        self.assertEqual(
            {seat["place"] for seat in waiting["seats"]}, {1, 2},
        )

    def test_the_fixedlength_wedge_is_diagnosable_the_whole_way_down(self):
        """Reproduce the field report's wedge against the phase machinery.

        Once ``fixedlength`` is enabled, Freeciv's ``can_end_turn()`` returns
        false forever, so the native boundary reports ``ready=0`` on every
        subsequent sample and the seat never regains a phase.end capability.
        That is not recoverable from inside the phase machinery, so what the
        fix owes the agent is an accurate account of it at every step, and a
        failure rather than an indefinite hang.
        """
        _created, game, joined = self.ready_v2_phase_game()
        wedged = self.phase_evidence(game, active_place=1, ready=False)
        agent_id = joined[0]["agent_id"]

        with patch.object(Game, "_write_manifest", autospec=True):
            _claim, failed = game._update_v2_phase_ledger(wedged, 10.0)
            self.assertFalse(failed)
            self.assertEqual(
                game.v2_phase_ledger["state"], "phase_not_ready",
            )
            waiting = game.v2_health(agent_id)["phase"]["waiting_on"]
            self.assertEqual(waiting["kind"], "seat_not_ready")
            self.assertIn("fixedlength", waiting["summary"])

            # No deadline ever starts, because a deadline only starts once
            # the seat is ready, so nothing but the progress guard can end it.
            self.assertIsNone(
                game.v2_phase_ledger["deadline_started_monotonic"],
            )
            _claim, failed = game._update_v2_phase_ledger(wedged, 309.0)
            self.assertFalse(failed)
            _claim, failed = game._update_v2_phase_ledger(wedged, 310.0)
        self.assertTrue(failed)
        self.assertIn("v2_phase_progress_stalled", game.invalid_reasons)

    def test_wait_timeout_carries_the_same_waiting_on(self):
        _created, game, joined = self.ready_v2_phase_game()
        self._seed(game, active_place=1, ready=False)
        response = game.v2_wait(joined[0]["agent_id"], 0.0)

        self.assertEqual(response["wake_reason"], "timeout")
        waiting = response["health"]["phase"]["waiting_on"]
        self.assertEqual(waiting["kind"], "seat_not_ready")
        self.assertIn("fixedlength", waiting["summary"])

    def test_wait_timeout_on_a_surrendered_holder_says_so(self):
        _created, game, joined = self.ready_v2_phase_game(
            multiplayer=True, places=2,
        )
        with game.condition:
            game.v2_surrendered_places.add(1)
        self._seed(game, active_place=1, ready=False)

        response = game.v2_wait(joined[1]["agent_id"], 0.0)
        waiting = response["health"]["phase"]["waiting_on"]
        self.assertEqual(waiting["kind"], "seat_surrendered")
        self.assertEqual(waiting["seats"][0]["standing"], "surrendered")


class SeatStandingTests(V2Harness):
    """A seat can tell "I resigned" from "nothing happened"."""

    def test_standing_starts_active_and_follows_an_applied_surrender(self):
        _created, game, joined = self.ready_v2_phase_game()
        agent_id = joined[0]["agent_id"]
        game._update_v2_phase_ledger(
            self.phase_evidence(game, active_place=1), time.monotonic(),
        )
        self.assertEqual(
            game.v2_health(agent_id)["seat"]["standing"], "active",
        )

        with game.condition:
            game.v2_surrendered_places.add(1)
        self.assertEqual(
            game.v2_health(agent_id)["seat"]["standing"], "surrendered",
        )

        with game.condition:
            game.cancel_requested = True
        self.assertEqual(
            game.v2_health(agent_id)["seat"]["standing"],
            "termination_pending",
        )

    def test_a_dead_seat_reads_as_eliminated_whether_or_not_it_resigned(self):
        _created, game, joined = self.ready_v2_phase_game()
        game._update_v2_phase_ledger(
            self.phase_evidence(game, active_place=1, alive=False),
            time.monotonic(),
        )
        self.assertEqual(
            game.v2_health(joined[0]["agent_id"])["seat"]["standing"],
            "eliminated",
        )
        with game.condition:
            game.v2_surrendered_places.add(1)
        self.assertEqual(
            game.v2_health(joined[0]["agent_id"])["seat"]["standing"],
            "eliminated",
        )

    def test_an_applied_surrender_records_the_seat(self):
        self.sidecar_factory.observation_rows = tuple(sorted(
            list(native_v2_rows()) + [SURRENDER_ROW],
        ))
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-surrender-model",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        self._seed_v2_phase(game)
        legal = game.v2_get_page(joined["agent_id"], "legal_actions", "")
        surrender = next(
            item for item in legal["page"]["items"]
            if item["kind"] == "player.surrender"
        )
        status, receipt = game.v2_submit_batch(
            joined["agent_id"],
            self.v2_batch(game, joined, surrender, "batch_surrender"),
        )
        self.assertEqual(status, HTTPStatus.OK)
        self.assertEqual(receipt["receipt_state"], "applied")
        with game.condition:
            self.assertIn(1, game.v2_surrendered_places)
        self.assertEqual(
            game.v2_health(joined["agent_id"])["seat"]["standing"],
            "surrendered",
        )

    def test_a_surrendered_seat_does_not_wedge_the_ledger_as_not_ready(self):
        _created, game, joined = self.ready_v2_phase_game()
        evidence = self.phase_evidence(game, active_place=1, ready=False)

        # Before the surrender the ledger correctly waits on the seat.
        game._update_v2_phase_ledger(evidence, 10.0)
        self.assertEqual(game.v2_phase_ledger["state"], "phase_not_ready")

        # After it, the seat can never become ready again, so the ledger must
        # stop reporting a readiness that will not arrive.
        with game.condition:
            game.v2_surrendered_places.add(1)
        game._update_v2_phase_ledger(evidence, 11.0)
        self.assertEqual(game.v2_phase_ledger["state"], "inactive_done")

        public = game._public_v2_phase()
        holder = next(
            item for item in public["controllers"] if item["place"] == 1
        )
        self.assertEqual(holder["state"], "inactive_done")

    def test_a_surrendered_seat_still_trips_the_progress_stall_guard(self):
        # It must not wedge silently either: an unrecoverable phase still
        # fails the game rather than hanging forever.
        _created, game, joined = self.ready_v2_phase_game()
        with game.condition:
            game.v2_surrendered_places.add(1)
        evidence = self.phase_evidence(game, active_place=1, ready=False)
        with patch.object(Game, "_write_manifest", autospec=True):
            _claim, failed = game._update_v2_phase_ledger(evidence, 10.0)
            self.assertFalse(failed)
            _claim, failed = game._update_v2_phase_ledger(evidence, 310.0)
        self.assertTrue(failed)
        self.assertIn("v2_phase_progress_stalled", game.invalid_reasons)


if __name__ == "__main__":
    unittest.main()
