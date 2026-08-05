"""Wedge detection and bounded rollback recovery for full-control-v2 seats."""

import json
import stat
import tempfile
import time
import unittest
from types import SimpleNamespace
from http import HTTPStatus
from pathlib import Path
from unittest.mock import patch

from agent_eval.headless_sidecar import SidecarError
from agent_eval.supervisor import (
    APIProblem,
    Game,
    SupervisorError,
    V2_PHASE_PROGRESS_STALL_S,
    V2_PHASE_RECONCILE_STALL_S,
    V2_PHASE_SYNCHRONIZE_STALL_S,
    V2_SIDECAR_EXIT_HISTORY_LIMIT,
    V2_STATUS_POLL_FAULT_LIMIT,
)
from agent_eval.v2_control import V2ControlError
from agent_eval.v2_recovery import (
    MAX_RECOVERY_ATTEMPTS_PER_GAME,
    RECOVERY_DIRECTORY,
    RECOVERY_FILENAME,
    RecoveryBudget,
    V2RecoveryError,
    V2RecoveryJournal,
    WedgeDetector,
    recovery_kind_for_attempt,
    select_rollback_save,
)


class WedgeDetectorTests(unittest.TestCase):
    def test_a_wedge_needs_an_uninterrupted_run_of_failures(self):
        detector = WedgeDetector(threshold=3)
        self.assertFalse(detector.note_failure(1))
        self.assertFalse(detector.note_failure(1))
        detector.note_success(1)
        self.assertEqual(detector.failures(1), 0)
        self.assertFalse(detector.note_failure(1))
        self.assertFalse(detector.note_failure(1))
        self.assertTrue(detector.note_failure(1))

    def test_seats_are_counted_independently(self):
        detector = WedgeDetector(threshold=2)
        self.assertFalse(detector.note_failure(1))
        self.assertFalse(detector.note_failure(2))
        self.assertTrue(detector.note_failure(1))
        self.assertEqual(detector.failures(2), 1)

    def test_an_ambiguous_observation_is_never_sufficient_on_its_own(self):
        detector = WedgeDetector(threshold=3)
        detector.note_ambiguous_observation(1)
        detector.note_ambiguous_observation(1)
        detector.note_ambiguous_observation(1)
        self.assertLess(detector.failures(1), detector.threshold)
        # ...but it does mean the next real failure completes the proof.
        self.assertTrue(detector.note_failure(1))

    def test_a_successful_read_clears_an_ambiguity_credit(self):
        detector = WedgeDetector(threshold=3)
        detector.note_ambiguous_observation(1)
        detector.note_success(1)
        self.assertFalse(detector.note_failure(1))


class RecoveryBudgetTests(unittest.TestCase):
    def test_attempts_escalate_from_reattach_to_rollback(self):
        budget = RecoveryBudget(per_turn=2, per_game=4)
        self.assertEqual(
            recovery_kind_for_attempt(budget.next_attempt(7, 1)),
            "sidecar_reattach",
        )
        self.assertEqual(
            recovery_kind_for_attempt(budget.next_attempt(7, 1)),
            "autosave_rollback",
        )

    def test_a_turn_cap_stops_a_third_attempt_on_that_turn(self):
        budget = RecoveryBudget(per_turn=2, per_game=99)
        budget.next_attempt(7, 1)
        budget.next_attempt(7, 1)
        self.assertIsNone(budget.next_attempt(7, 1))
        # A later turn still gets its own allowance.
        self.assertEqual(budget.next_attempt(8, 1), 1)

    def test_a_game_cap_stops_recovery_across_turns(self):
        budget = RecoveryBudget(per_turn=2, per_game=3)
        self.assertEqual(budget.next_attempt(1, 1), 1)
        self.assertEqual(budget.next_attempt(1, 1), 2)
        self.assertEqual(budget.next_attempt(2, 1), 1)
        self.assertIsNone(budget.next_attempt(2, 1))
        self.assertIn("budget", budget.exhausted_reason(2, 1))

    def test_the_exhausted_reason_names_which_cap_was_hit(self):
        per_turn = RecoveryBudget(per_turn=1, per_game=99)
        per_turn.next_attempt(4, 1)
        self.assertIn("turn 4", per_turn.exhausted_reason(4, 1))

    def test_each_seat_climbs_its_own_escalation_ladder(self):
        # A shared per-turn ladder made a second seat's FIRST EVER fault read
        # as attempt 2, so it skipped the free re-attach and immediately
        # rewound real turns of the whole game -- and a third seat got no
        # recovery at all.
        budget = RecoveryBudget(per_turn=2, per_game=99)
        self.assertEqual(
            recovery_kind_for_attempt(budget.next_attempt(66, 1)),
            "sidecar_reattach",
        )
        self.assertEqual(
            recovery_kind_for_attempt(budget.next_attempt(66, 2)),
            "sidecar_reattach",
        )
        self.assertEqual(
            recovery_kind_for_attempt(budget.next_attempt(66, 3)),
            "sidecar_reattach",
        )
        # Escalation is still per seat: place 1's second attempt rolls back.
        self.assertEqual(
            recovery_kind_for_attempt(budget.next_attempt(66, 1)),
            "autosave_rollback",
        )
        self.assertEqual(budget.attempts_for_turn(66, 2), 1)

    def test_a_successful_reattach_costs_the_game_nothing(self):
        # The per-game cap bounds how much real play may be DISCARDED.  A
        # re-attach discards none, so charging it identically to a rollback
        # let four successful recoveries kill a game on its fifth blip.
        budget = RecoveryBudget(per_turn=2, per_game=4)
        for turn in (10, 20, 30, 40, 50, 60):
            attempt = budget.next_attempt(turn, 1)
            self.assertEqual(attempt, 1, turn)
            self.assertTrue(budget.release(
                turn, 1, kind="sidecar_reattach", outcome="recovered",
            ))
        self.assertEqual(budget.total, 0)
        self.assertIsNotNone(budget.next_attempt(70, 1))

    def test_a_rollback_stays_charged_forever(self):
        budget = RecoveryBudget(per_turn=2, per_game=4)
        budget.next_attempt(10, 1)
        self.assertFalse(budget.release(
            10, 1, kind="autosave_rollback", outcome="recovered",
        ))
        self.assertEqual(budget.total, 1)

    def test_a_failed_attempt_stays_charged(self):
        budget = RecoveryBudget(per_turn=2, per_game=4)
        budget.next_attempt(10, 1)
        self.assertFalse(budget.release(
            10, 1, kind="sidecar_reattach", outcome="failed",
        ))
        self.assertEqual(budget.total, 1)

    def test_an_abandoned_attempt_is_given_back(self):
        # An attempt refused because the game is being cancelled or torn down
        # never ran, so it may not spend a budget meant for real recoveries.
        budget = RecoveryBudget(per_turn=2, per_game=4)
        budget.next_attempt(10, 1)
        self.assertTrue(budget.release(
            10, 1, kind="autosave_rollback", outcome="abandoned",
        ))
        self.assertEqual(budget.total, 0)
        self.assertEqual(budget.attempts_for_turn(10, 1), 0)


class SelectRollbackSaveTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.saves = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def write(self, name, content=b"freeciv-save"):
        (self.saves / name).write_bytes(content)

    def test_the_newest_save_at_or_before_the_turn_is_chosen(self):
        for turn in (49, 50, 51, 52):
            self.write(f"turn-{turn:04d}-auto.sav.gz")
        selected = select_rollback_save(self.saves, at_or_before_turn=51)
        self.assertIsNotNone(selected)
        path, turn = selected
        self.assertEqual(turn, 51)
        self.assertEqual(path.name, "turn-0051-auto.sav.gz")

    def test_an_unreadable_newest_save_falls_back_to_the_one_before_it(self):
        self.write("turn-0050-auto.sav.gz")
        self.write("turn-0051-auto.sav.gz", content=b"")
        selected = select_rollback_save(self.saves, at_or_before_turn=51)
        self.assertEqual(selected[1], 50)

    def test_unrelated_files_are_never_selected(self):
        self.write("turn-0051-M-abc.map.ppm")
        self.write("manual-save.sav.gz")
        self.assertIsNone(
            select_rollback_save(self.saves, at_or_before_turn=51),
        )

    def test_a_missing_saves_directory_is_not_an_error(self):
        self.assertIsNone(
            select_rollback_save(
                self.saves / "absent", at_or_before_turn=3,
            ),
        )


class RecoveryJournalTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def event(self, **overrides):
        payload = {
            "place": 1,
            "seat_id": "place-1",
            "turn": 52,
            "attempt": 1,
            "kind": "autosave_rollback",
            "trigger": "boundary_internal_error",
            "outcome": "recovered",
            "sidecar_generation": 2,
            "recovered_to_turn": 51,
            "rewound_applied_actions": True,
        }
        payload.update(overrides)
        return payload

    def test_a_rollback_is_appended_as_one_bounded_json_line(self):
        with V2RecoveryJournal(self.root, game_id="game_abc") as journal:
            journal.record(**self.event())
        path = self.root / RECOVERY_DIRECTORY / RECOVERY_FILENAME
        lines = path.read_text(encoding="ascii").splitlines()
        self.assertEqual(len(lines), 1)
        record = json.loads(lines[0])
        self.assertEqual(record["game_id"], "game_abc")
        self.assertEqual(record["kind"], "autosave_rollback")
        self.assertEqual(record["recovered_to_turn"], 51)
        self.assertIs(record["rewound_applied_actions"], True)

    def test_the_schema_is_closed_against_unknown_values(self):
        with V2RecoveryJournal(self.root, game_id="game_abc") as journal:
            for field, value in (
                ("kind", "reboot_everything"),
                ("outcome", "probably_fine"),
                ("trigger", "vibes"),
                ("rewound_applied_actions", "yes"),
                ("place", 0),
                ("turn", -1),
            ):
                with self.subTest(field=field):
                    with self.assertRaises(V2RecoveryError):
                        journal.record(**self.event(**{field: value}))


def _supervisor_test_case():
    """Fetch the shared fake-stack fixture without exporting it as a name.

    Binding it at module scope would make unittest collect the whole
    supervisor suite again every time this module is loaded.
    """
    from agent_eval.tests.test_supervisor import SupervisorTests

    return SupervisorTests


class _SeatHarness(unittest.TestCase):
    """Reuse the supervisor fake stack without rerunning its own suite."""

    _shared = _supervisor_test_case()
    setUp = _shared.setUp
    tearDown = _shared.tearDown
    create = _shared.create
    _mark_v2_running = staticmethod(_shared._mark_v2_running)
    _seed_v2_phase = _shared._seed_v2_phase
    phase_evidence = staticmethod(_shared.phase_evidence)
    ready_v2_action = _shared.ready_v2_action
    ready_v2_non_phase_action = _shared.ready_v2_non_phase_action
    v2_batch = staticmethod(_shared.v2_batch)


class BoundaryWedgeTests(_SeatHarness):
    """Detection, containment, and recovery against the fake seat stack."""

    def wedge_boundary(self, count=3):
        """Make every boundary projection fail the way a wedge does."""
        return patch.object(
            Game,
            "_read_v2_observation_bundle",
            side_effect=V2ControlError("internal_error"),
        )

    def drive_failures(self, game, joined, count=3):
        problems = []
        for _index in range(count):
            with self.assertRaises(APIProblem) as caught:
                game.v2_get_page(joined["agent_id"], "state", "")
            problems.append(caught.exception)
        return problems

    def test_repeated_internal_errors_flip_health_out_of_ready(self):
        _created, game, joined, _action = self.ready_v2_action()
        with patch.object(Game, "_start_v2_boundary_recovery"), \
                self.wedge_boundary():
            problems = self.drive_failures(game, joined)

        self.assertEqual(
            problems[0].payload["error"]["code"], "internal_error",
        )
        self.assertIn(1, game.v2_wedged_places)

        health = game.v2_health(joined["agent_id"])
        self.assertEqual(health["sidecar"]["state"], "wedged")
        self.assertEqual(
            health["sidecar"]["error_code"], "native_boundary_wedged",
        )
        self.assertFalse(health["observation_available"])
        self.assertFalse(health["legal_actions_available"])
        self.assertEqual(
            health["phase"]["waiting_on"]["kind"], "boundary_recovery",
        )

    def test_a_wedged_seat_refuses_retryably_instead_of_internally(self):
        _created, game, joined, _action = self.ready_v2_action()
        with patch.object(Game, "_start_v2_boundary_recovery"), \
                self.wedge_boundary():
            self.drive_failures(game, joined)
            # The boundary is proven dead, so further requests must stop
            # claiming an unattributable internal failure.
            with self.assertRaises(APIProblem) as caught:
                game.v2_get_page(joined["agent_id"], "state", "")
        error = caught.exception.payload["error"]
        self.assertEqual(caught.exception.status, HTTPStatus.SERVICE_UNAVAILABLE)
        self.assertEqual(error["code"], "sidecar_unavailable")
        self.assertTrue(error["retryable"])

    def test_two_failures_alone_do_not_wedge_a_seat(self):
        _created, game, joined, _action = self.ready_v2_action()
        with patch.object(Game, "_start_v2_boundary_recovery"), \
                self.wedge_boundary():
            self.drive_failures(game, joined, count=2)
        self.assertNotIn(1, game.v2_wedged_places)
        self.assertEqual(
            game.v2_health(joined["agent_id"])["sidecar"]["state"], "ready",
        )

    def test_a_successful_read_clears_the_wedge_evidence(self):
        _created, game, joined, _action = self.ready_v2_action()
        with patch.object(Game, "_start_v2_boundary_recovery"), \
                self.wedge_boundary():
            self.drive_failures(game, joined, count=2)
        game.v2_get_page(joined["agent_id"], "state", "")
        with patch.object(Game, "_start_v2_boundary_recovery"), \
                self.wedge_boundary():
            self.drive_failures(game, joined, count=2)
        self.assertNotIn(1, game.v2_wedged_places)

    def test_an_ambiguous_post_result_read_shortens_the_proof(self):
        _created, game, joined, _action = self.ready_v2_action()
        game._note_v2_ambiguous_observation(1)
        with patch.object(Game, "_start_v2_boundary_recovery"), \
                self.wedge_boundary():
            self.drive_failures(game, joined, count=1)
        self.assertIn(1, game.v2_wedged_places)
        self.assertEqual(
            game.v2_wedged_places[1]["trigger"], "boundary_internal_error",
        )

    def test_recovery_republishes_the_seat_on_a_new_generation(self):
        _created, game, joined, _action = self.ready_v2_action()
        before = game.sidecar_generations[1]
        old_sidecar = game.sidecars[1]
        with patch.object(
            Game, "_start_v2_boundary_recovery",
            new=lambda self, place, detected: self._run_v2_boundary_recovery(
                place, detected,
            ),
        ), self.wedge_boundary():
            self.drive_failures(game, joined)

        self.assertNotIn(1, game.v2_wedged_places)
        self.assertEqual(game.sidecar_generations[1], before + 1)
        self.assertIsNot(game.sidecars[1], old_sidecar)
        self.assertEqual(old_sidecar.stop_count, 1)
        self.assertEqual(game.state, "running")

        health = game.v2_health(joined["agent_id"])
        self.assertEqual(health["sidecar"]["state"], "ready")
        self.assertEqual(health["sidecar"]["generation"], before + 1)
        self.assertEqual(health["last_recovery"]["kind"], "sidecar_reattach")
        self.assertEqual(health["last_recovery"]["outcome"], "recovered")

    def test_a_recovered_seat_can_read_and_act_again(self):
        _created, game, joined, _action = self.ready_v2_action()
        with patch.object(
            Game, "_start_v2_boundary_recovery",
            new=lambda self, place, detected: self._run_v2_boundary_recovery(
                place, detected,
            ),
        ), self.wedge_boundary():
            self.drive_failures(game, joined)
        self._seed_v2_phase(game)
        page = game.v2_get_page(joined["agent_id"], "legal_actions", "")
        self.assertTrue(page["page"]["items"])

    def test_the_rollback_event_is_recorded_for_scoring(self):
        _created, game, joined, _action = self.ready_v2_action()
        with patch.object(
            Game, "_start_v2_boundary_recovery",
            new=lambda self, place, detected: self._run_v2_boundary_recovery(
                place, detected,
            ),
        ), self.wedge_boundary():
            self.drive_failures(game, joined)
        path = game.episode / RECOVERY_DIRECTORY / RECOVERY_FILENAME
        records = [
            json.loads(line)
            for line in path.read_text(encoding="ascii").splitlines()
        ]
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0]["outcome"], "recovered")
        self.assertEqual(records[0]["kind"], "sidecar_reattach")
        self.assertEqual(records[0]["place"], 1)
        self.assertEqual(records[0]["seat_id"], "place-1")
        # A re-attach discards no play at all.
        self.assertIsNone(records[0]["recovered_to_turn"])
        self.assertIs(records[0]["rewound_applied_actions"], False)

    def test_a_second_attempt_rolls_back_to_the_newest_readable_autosave(self):
        _created, game, joined, _action = self.ready_v2_action()
        saves = game.episode / "saves"
        saves.mkdir(exist_ok=True)
        for turn in (5, 6, 7):
            (saves / f"turn-{turn:04d}-auto.sav.gz").write_bytes(b"save")
        with game.condition:
            # This seat already applied an action on the turn being rewound.
            game.v2_applied_turns[1] = 7

        reloaded = []
        with patch.object(
            Game, "_start_v2_boundary_recovery",
            new=lambda self, place, detected: self._run_v2_boundary_recovery(
                place, detected,
            ),
        ), patch.object(
            Game, "_v2_recovery_reload_server",
            new=lambda self, path: (reloaded.append(path), True)[1],
        ), patch.object(
            Game, "_v2_recovery_start_loaded_game", new=lambda self: True,
        ), self.wedge_boundary():
            # Make the first tier fail so recovery has to escalate.
            self.sidecar_factory.fail_next = True
            self.drive_failures(game, joined)

        self.assertEqual([path.name for path in reloaded], [
            "turn-0007-auto.sav.gz",
        ])
        path = game.episode / RECOVERY_DIRECTORY / RECOVERY_FILENAME
        records = [
            json.loads(line)
            for line in path.read_text(encoding="ascii").splitlines()
        ]
        self.assertEqual([item["kind"] for item in records], [
            "sidecar_reattach", "autosave_rollback",
        ])
        self.assertEqual(records[0]["outcome"], "failed")
        rollback = records[1]
        self.assertEqual(rollback["outcome"], "recovered")
        self.assertEqual(rollback["recovered_to_turn"], 7)
        self.assertIs(rollback["rewound_applied_actions"], True)

    def reloadable_game(self, game, *, turns=(7,)):
        """Give a game readable autosaves and a launchable fake server."""
        saves = game.episode / "saves"
        saves.mkdir(exist_ok=True)
        for turn in turns:
            (saves / f"turn-{turn:04d}-auto.sav.gz").write_bytes(b"save")
        process = SimpleNamespace(
            poll=lambda: None,
            stdin=None,
            wait=lambda **kwargs: 0,
            terminate=lambda: None,
            kill=lambda: None,
        )
        self.supervisor.process_factory = lambda *a, **k: process
        with game.condition:
            game.process = process
        return process

    def test_a_reloaded_save_is_started_after_the_seat_is_retaken(self):
        _created, game, joined, _action = self.ready_v2_action()
        self.reloadable_game(game)

        # One ordered log of everything the recovery drives, so the sequence
        # itself is what the assertion is about.
        events = []
        real_attach = Game._v2_recovery_attach_seat

        def attach(inner, place, generation):
            events.append("attach")
            return real_attach(inner, place, generation)

        def send(inner, commands, **kwargs):
            events.append(tuple(commands))

        with patch.object(
            Game, "_start_v2_boundary_recovery",
            new=lambda inner, place, detected:
                inner._run_v2_boundary_recovery(place, detected),
        ), patch.object(
            Game, "_v2_recovery_attach_seat", new=attach,
        ), patch.object(
            Game, "_send_commands", new=send,
        ), patch.object(
            Game, "_wait_for_prompt", new=lambda inner, timeout_s=20: None,
        ), patch.object(
            Game, "_pump_output", new=lambda inner: None,
        ), patch.object(
            # The fake server process returns from wait() at once, which would
            # let its monitor finalize the game mid-recovery.
            Game, "_monitor", new=lambda inner, process=None: None,
        ), self.wedge_boundary():
            self.sidecar_factory.fail_next = True
            self.drive_failures(game, joined)

        settings, retake, start = events[-3:]
        # Freeciv leaves a loaded save in pregame, so the settings alone would
        # hang the game forever.
        self.assertIn("set savename turn-%04T-%R", settings)
        self.assertEqual(retake, "attach")
        self.assertEqual(start, ("start",))

        journal = (game.episode / "server.commands").read_text(
            encoding="utf-8",
        ).splitlines()
        self.assertIn("# reload turn-0007-auto.sav.gz", journal)
        self.assertEqual(journal[-1], "start")
        self.assertEqual(journal.count("start"), 1)

    def test_the_server_is_replaced_only_while_no_sidecar_is_registered(self):
        _created, game, joined, _action = self.ready_v2_action()
        self.reloadable_game(game)
        registered = []

        with patch.object(
            Game, "_start_v2_boundary_recovery",
            new=lambda inner, place, detected:
                inner._run_v2_boundary_recovery(place, detected),
        ), patch.object(
            Game, "_v2_recovery_reload_server",
            new=lambda inner, path: (
                registered.append(dict(inner.sidecars)), True,
            )[1],
        ), patch.object(
            Game, "_v2_recovery_start_loaded_game", new=lambda inner: True,
        ), self.wedge_boundary():
            self.sidecar_factory.fail_next = True
            self.drive_failures(game, joined)

        # A sidecar still registered while its server dies is read by the
        # status poller as an unexpected seat loss, which fails the game.
        self.assertEqual(registered, [{}])

    def test_starting_a_reloaded_save_does_not_consult_the_original_latch(self):
        _created, game, joined, _action = self.ready_v2_action()
        self.reloadable_game(game)
        with game.condition:
            # The lobby start latched long ago; a rollback must start anyway.
            game.start_sent = True
            before = game.start_count

        with patch.object(
            Game, "_wait_for_prompt", new=lambda inner, timeout_s=20: None,
        ):
            self.assertTrue(game._v2_recovery_start_loaded_game())
            # And again, so a second rollback attempt after a failed one is
            # not silently unable to resume the game.
            self.assertTrue(game._v2_recovery_start_loaded_game())

        self.assertEqual(game.start_count, before + 2)
        journal = (game.episode / "server.commands").read_text(
            encoding="utf-8",
        ).splitlines()
        self.assertEqual(journal.count("start"), 2)

    def test_a_second_rollback_attempt_starts_the_game_again(self):
        _created, game, joined, _action = self.ready_v2_action()
        self.reloadable_game(game)
        with game.condition:
            game.v2_recovery_budget = RecoveryBudget(per_turn=3, per_game=3)
        starts = []
        # The first rollback fails at its start; the next one must retry it.
        outcomes = [False, True]

        with patch.object(
            Game, "_start_v2_boundary_recovery",
            new=lambda inner, place, detected:
                inner._run_v2_boundary_recovery(place, detected),
        ), patch.object(
            Game, "_v2_recovery_reload_server", new=lambda inner, path: True,
        ), patch.object(
            Game, "_v2_recovery_start_loaded_game",
            new=lambda inner: (
                starts.append(True), outcomes.pop(0) if outcomes else True,
            )[1],
        ), self.wedge_boundary():
            self.sidecar_factory.fail_next = True
            self.drive_failures(game, joined)

        self.assertEqual(len(starts), 2)
        path = game.episode / RECOVERY_DIRECTORY / RECOVERY_FILENAME
        records = [
            json.loads(line)
            for line in path.read_text(encoding="ascii").splitlines()
        ]
        self.assertEqual(
            [item["outcome"] for item in records],
            ["failed", "failed", "recovered"],
        )
        self.assertNotIn(1, game.v2_wedged_places)
        self.assertEqual(game.state, "running")

    def test_a_rollback_that_discards_nothing_says_so(self):
        _created, game, joined, _action = self.ready_v2_action()
        saves = game.episode / "saves"
        saves.mkdir(exist_ok=True)
        (saves / "turn-0007-auto.sav.gz").write_bytes(b"save")
        with game.condition:
            game.v2_applied_turns[1] = 6

        with patch.object(
            Game, "_start_v2_boundary_recovery",
            new=lambda self, place, detected: self._run_v2_boundary_recovery(
                place, detected,
            ),
        ), patch.object(
            Game, "_v2_recovery_reload_server", new=lambda self, path: True,
        ), patch.object(
            Game, "_v2_recovery_start_loaded_game", new=lambda self: True,
        ), self.wedge_boundary():
            self.sidecar_factory.fail_next = True
            self.drive_failures(game, joined)

        path = game.episode / RECOVERY_DIRECTORY / RECOVERY_FILENAME
        rollback = [
            json.loads(line)
            for line in path.read_text(encoding="ascii").splitlines()
        ][-1]
        self.assertIs(rollback["rewound_applied_actions"], False)

    def test_an_exhausted_budget_fails_the_game_with_a_clear_reason(self):
        _created, game, joined, _action = self.ready_v2_action()
        with game.condition:
            game.v2_recovery_budget = RecoveryBudget(per_turn=0, per_game=0)

        with patch.object(
            Game, "_start_v2_boundary_recovery",
            new=lambda self, place, detected: self._run_v2_boundary_recovery(
                place, detected,
            ),
        ), self.wedge_boundary():
            self.drive_failures(game, joined)

        self.assertEqual(game.state, "failed")
        self.assertIn("wedged", game.error)
        self.assertIn("v2_boundary_wedged", game.invalid_reasons)
        path = game.episode / RECOVERY_DIRECTORY / RECOVERY_FILENAME
        records = [
            json.loads(line)
            for line in path.read_text(encoding="ascii").splitlines()
        ]
        self.assertEqual(records[-1]["outcome"], "abandoned")

    def test_a_boundary_that_never_recovers_stops_at_the_cap(self):
        _created, game, joined, _action = self.ready_v2_action()
        with patch.object(
            Game, "_start_v2_boundary_recovery",
            new=lambda self, place, detected: self._run_v2_boundary_recovery(
                place, detected,
            ),
        ), patch.object(
            Game, "_v2_recovery_rebuild_seat", new=lambda self, place, kind: False,
        ), patch.object(
            Game, "_v2_recovery_reload_server", new=lambda self, path: True,
        ), patch.object(
            Game, "_v2_recovery_start_loaded_game", new=lambda self: True,
        ), self.wedge_boundary():
            self.drive_failures(game, joined)

        self.assertEqual(game.state, "failed")
        path = game.episode / RECOVERY_DIRECTORY / RECOVERY_FILENAME
        records = [
            json.loads(line)
            for line in path.read_text(encoding="ascii").splitlines()
        ]
        self.assertLessEqual(
            sum(1 for item in records if item["outcome"] != "abandoned"),
            MAX_RECOVERY_ATTEMPTS_PER_GAME,
        )
        self.assertEqual(records[-1]["outcome"], "abandoned")

    def test_wait_names_the_recovery_as_its_wake_reason(self):
        _created, game, joined, _action = self.ready_v2_action()
        before = game.sidecar_generations[1]
        real_health = Game.v2_health
        recovered = []
        case = self

        def health_once(game_self, agent_id):
            # Recover the seat underneath a caller that is already waiting,
            # which is the only way a wait can be woken by a rollback.
            if not recovered:
                recovered.append(True)
                with patch.object(
                    Game, "_start_v2_boundary_recovery",
                    new=lambda inner, place, detected:
                        inner._run_v2_boundary_recovery(place, detected),
                ), case.wedge_boundary():
                    case.drive_failures(game, joined)
            return real_health(game_self, agent_id)

        with patch.object(Game, "v2_health", new=health_once):
            waited = game.v2_wait(joined["agent_id"], 0)

        self.assertEqual(waited["wake_reason"], "boundary_recovered")
        self.assertEqual(
            waited["health"]["sidecar"]["generation"], before + 1,
        )
        self.assertEqual(waited["health"]["sidecar"]["state"], "ready")

    def test_an_ambiguous_receipt_is_never_replayed_by_recovery(self):
        _created, game, joined, action = self.ready_v2_non_phase_action()
        batch = self.v2_batch(game, joined, action, batch_id="batch_ambig")
        with patch.object(
            Game,
            "_read_v2_post_result_observation_bundle",
            side_effect=V2ControlError("internal_error"),
        ):
            status, receipt = game.v2_submit_batch(joined["agent_id"], batch)
        self.assertEqual(receipt["receipt_state"], "ambiguous")

        with patch.object(
            Game, "_start_v2_boundary_recovery",
            new=lambda self, place, detected: self._run_v2_boundary_recovery(
                place, detected,
            ),
        ), self.wedge_boundary():
            self.drive_failures(game, joined)
        self.assertNotIn(1, game.v2_wedged_places)

        # The terminal receipt survives the new generation unchanged, and the
        # command behind it is not dispatched a second time.
        before = self.sidecar_factory.action_count
        replayed_status, replayed = game.v2_get_receipt(
            joined["agent_id"], "batch_ambig",
        )
        self.assertEqual(replayed["receipt_state"], "ambiguous")
        self.assertEqual(self.sidecar_factory.action_count, before)

    def test_strategic_v1_games_never_arm_wedge_detection(self):
        created = self.create()
        game = self.supervisor.game(created["game_id"])
        game._note_v2_boundary_outcome(1, ok=False)
        game._note_v2_boundary_outcome(1, ok=False)
        game._note_v2_boundary_outcome(1, ok=False)
        game._note_v2_ambiguous_observation(1)
        self.assertEqual(game.v2_wedged_places, {})
        self.assertEqual(game.state, "lobby")


class _SidecarExitHarness(_SeatHarness):
    """Shared fixture for every seat-loss suite below."""

    FORENSICS = {
        "exit_code": -11,
        "exit_signal": 11,
        "exit_signal_name": "SIGSEGV",
        "process_alive": False,
        "sidecar_state": "failed",
        "client_state": "running",
        "error_code": "process_exited",
        "last_seen_at": 1234.5,
        "stderr_tail": ["Assertion failed: bad packet", "backtrace line"],
        "stdout_tail": [],
    }

    def sync_recovery(self, *, on_enter=None):
        """Run recovery on the calling thread so outcomes are deterministic.

        ``on_enter`` runs once the loss has been detected and before recovery
        acts, which is where a test undoes whatever killed the old sidecar so
        its replacement can come up healthy.
        """
        def run(inner, place, detected):
            if on_enter is not None:
                on_enter()
            return inner._run_v2_boundary_recovery(place, detected)

        return patch.object(Game, "_start_v2_boundary_recovery", new=run)

    def arm_forensics(self, sidecar, **overrides):
        payload = {**self.FORENSICS, **overrides}
        sidecar.private_exit_forensics = lambda: dict(payload)
        return payload

    def running_game(self):
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-exit-model",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        with game.condition:
            game.start_sent = True
        # Give the game a real current turn, so a rollback has somewhere to
        # roll back to; recovery refuses a save from a turn it has not reached.
        self._seed_v2_phase(game, turn=7)
        return game, joined

    def journal(self, game):
        path = game.episode / RECOVERY_DIRECTORY / RECOVERY_FILENAME
        if not path.exists():
            return []
        return [
            json.loads(line)
            for line in path.read_text(encoding="ascii").splitlines()
        ]

class SidecarExitRecoveryTests(_SidecarExitHarness):
    """A lost seat on a live server is recovered, not mourned."""

    def test_a_mid_game_sidecar_death_reattaches_and_play_continues(self):
        game, joined = self.running_game()
        dying = self.sidecar_factory.created[-1]
        self.arm_forensics(dying)

        with self.sync_recovery():
            dying.die()

        self.assertEqual(game.state, "running")
        self.assertEqual(game.sidecar_generations[1], 2)
        self.assertIsNot(game.sidecars[1], dying)
        self.assertNotIn(1, game.v2_wedged_places)
        self.assertGreaterEqual(dying.stop_count, 1)

        self._seed_v2_phase(game)
        page = game.v2_get_page(joined["agent_id"], "legal_actions", "")
        self.assertTrue(page["page"]["items"])

    def test_the_death_is_journalled_as_a_reattach_with_its_forensics(self):
        game, _joined = self.running_game()
        dying = self.sidecar_factory.created[-1]
        self.arm_forensics(dying)

        with self.sync_recovery():
            dying.die()

        records = self.journal(game)
        self.assertEqual(len(records), 1)
        record = records[0]
        self.assertEqual(record["trigger"], "sidecar_exit")
        self.assertEqual(record["kind"], "sidecar_reattach")
        self.assertEqual(record["outcome"], "recovered")
        # The forensics that fit a bounded record travel with it.
        self.assertEqual(record["exit_code"], -11)
        self.assertEqual(record["exit_signal"], 11)
        self.assertEqual(record["client_state"], "running")

    def test_the_full_forensics_land_in_the_private_exit_diagnostic(self):
        game, _joined = self.running_game()
        dying = self.sidecar_factory.created[-1]
        self.arm_forensics(dying)

        with self.sync_recovery():
            dying.die()

        diagnostic = json.loads(
            (game.episode / "sidecar-exit-diagnostic.json").read_text(
                encoding="utf-8",
            ),
        )
        forensics = diagnostic["forensics"]
        self.assertEqual(forensics["exit_signal_name"], "SIGSEGV")
        self.assertIs(forensics["process_alive"], False)
        self.assertEqual(
            forensics["stderr_tail"],
            ["Assertion failed: bad packet", "backtrace line"],
        )

    def test_a_second_death_never_erases_the_first_death_s_logs(self):
        # Recovery makes several deaths per game the expected case, and the
        # latest-death file is overwritten by each one.  The log tails are the
        # only evidence separating a native crash from a silent
        # disappearance, so losing the earlier ones re-creates exactly the
        # unattributable seat loss this whole mechanism exists to prevent.
        game, _joined = self.running_game()
        first = self.sidecar_factory.created[-1]
        self.arm_forensics(first, stderr_tail=["first death: SIGSEGV"])
        with self.sync_recovery():
            first.die()

        second = self.sidecar_factory.created[-1]
        self.assertIsNot(second, first)
        self.arm_forensics(
            second, exit_code=1, exit_signal=None, exit_signal_name=None,
            stderr_tail=["second death: clean exit"],
        )
        with self.sync_recovery():
            second.die()

        history = json.loads(
            (game.episode / "sidecar-exit-history.json").read_text(
                encoding="utf-8",
            ),
        )
        self.assertEqual(
            [death["forensics"]["stderr_tail"] for death in history["deaths"]],
            [["first death: SIGSEGV"], ["second death: clean exit"]],
        )
        self.assertEqual(
            [death["generation"] for death in history["deaths"]], [1, 2],
        )
        self.assertEqual(
            stat.S_IMODE(
                (game.episode / "sidecar-exit-history.json").stat().st_mode,
            ),
            0o600,
        )
        # The latest-death file keeps saying what it always said.
        diagnostic = json.loads(
            (game.episode / "sidecar-exit-diagnostic.json").read_text(
                encoding="utf-8",
            ),
        )
        self.assertEqual(
            diagnostic["forensics"]["stderr_tail"], ["second death: clean exit"],
        )

    def test_the_death_history_is_bounded(self):
        game, _joined = self.running_game()
        for index in range(V2_SIDECAR_EXIT_HISTORY_LIMIT + 3):
            game._persist_sidecar_exit_diagnostic(
                1, index + 1, {"state": "failed"}, {"exit_code": index},
            )
        history = json.loads(
            (game.episode / "sidecar-exit-history.json").read_text(
                encoding="utf-8",
            ),
        )
        self.assertEqual(
            len(history["deaths"]), V2_SIDECAR_EXIT_HISTORY_LIMIT,
        )
        # The newest deaths are the ones kept.
        self.assertEqual(
            history["deaths"][-1]["forensics"]["exit_code"],
            V2_SIDECAR_EXIT_HISTORY_LIMIT + 2,
        )

    def test_a_client_that_hangs_without_dying_is_recorded_as_such(self):
        # The turn-66 incident: exit_code null, process still alive, the
        # client simply stopped answering.  It must be attributable.
        game, _joined = self.running_game()
        dying = self.sidecar_factory.created[-1]
        self.arm_forensics(
            dying,
            exit_code=None, exit_signal=None, exit_signal_name=None,
            process_alive=True, error_code="deadline_exceeded",
        )

        with self.sync_recovery():
            dying.die()

        record = self.journal(game)[0]
        self.assertIsNone(record["exit_code"])
        self.assertIsNone(record["exit_signal"])
        self.assertEqual(record["client_state"], "running")
        diagnostic = json.loads(
            (game.episode / "sidecar-exit-diagnostic.json").read_text(
                encoding="utf-8",
            ),
        )
        self.assertIs(diagnostic["forensics"]["process_alive"], True)
        self.assertEqual(
            diagnostic["forensics"]["error_code"], "deadline_exceeded",
        )

    def test_a_lobby_sidecar_death_still_fails_the_game(self):
        created = self.create(
            mode="multiplayer", places=2, control_protocol="full-control-v2",
        )
        game = self.supervisor.game(created["game_id"])
        game.join(
            created["join_token"], 1, "codex-lobby-death",
            supported_control_protocols=["full-control-v2"],
        )
        self.assertEqual(game.state, "lobby")

        with self.sync_recovery():
            self.sidecar_factory.created[-1].die()

        self.assertEqual(game.state, "failed")
        self.assertFalse(game.start_sent)
        self.assertEqual(self.journal(game), [])

    def test_a_death_during_teardown_still_fails_the_game(self):
        game, _joined = self.running_game()
        with game.condition:
            game.cancel_requested = True
        dying = self.sidecar_factory.created[-1]

        with self.sync_recovery():
            dying.die()

        self.assertNotEqual(game.sidecar_generations[1], 2)
        self.assertEqual(self.journal(game), [])

    def test_the_failure_reason_names_how_the_client_died(self):
        game, _joined = self.running_game()
        with game.condition:
            game.v2_recovery_budget = RecoveryBudget(per_turn=0, per_game=0)
        dying = self.sidecar_factory.created[-1]
        self.arm_forensics(dying)

        with self.sync_recovery():
            dying.die()

        self.assertEqual(game.state, "failed")
        self.assertIn("SIGSEGV", game.error)
        self.assertIn("last native client state running", game.error)
        self.assertEqual(self.journal(game)[-1]["outcome"], "abandoned")

    def test_repeated_successful_reattaches_never_exhaust_the_game(self):
        # A tier-1 re-attach rewinds no play and discards nothing, so it must
        # not consume a budget whose entire purpose is bounding discarded
        # play.  Charging it did: four successful recoveries used to leave the
        # fifth fault with no budget, and the game died of being healthy.
        game, _joined = self.running_game()
        with game.condition:
            game.v2_recovery_budget = RecoveryBudget(per_turn=1, per_game=1)

        for _round in range(4):
            self.arm_forensics(self.sidecar_factory.created[-1])
            with self.sync_recovery():
                self.sidecar_factory.created[-1].die()
            self.assertEqual(game.state, "running")

        self.assertEqual(game.sidecar_generations[1], 5)
        self.assertEqual(game.v2_recovery_budget.total, 0)
        outcomes = [item["outcome"] for item in self.journal(game)]
        self.assertEqual(outcomes, ["recovered"] * 4)

    def test_an_unrecoverable_death_still_exhausts_the_budget(self):
        # The other direction: an attempt that RAN AND FAILED stays charged,
        # so a genuinely broken client still fails closed rather than looping.
        game, _joined = self.running_game()
        with game.condition:
            game.v2_recovery_budget = RecoveryBudget(per_turn=1, per_game=1)
        self.arm_forensics(self.sidecar_factory.created[-1])
        self.sidecar_factory.fail_next = True

        with self.sync_recovery():
            self.sidecar_factory.created[-1].die()

        self.assertEqual(game.state, "failed")
        self.assertEqual(game.v2_recovery_budget.total, 1)
        outcomes = [item["outcome"] for item in self.journal(game)]
        self.assertEqual(outcomes, ["failed", "abandoned"])

    def test_a_death_the_reattach_cannot_fix_escalates_to_rollback(self):
        game, _joined = self.running_game()
        saves = game.episode / "saves"
        saves.mkdir(exist_ok=True)
        (saves / "turn-0007-auto.sav.gz").write_bytes(b"save")
        self.arm_forensics(self.sidecar_factory.created[-1])
        reloaded = []

        with patch.object(
            Game, "_v2_recovery_reload_server",
            new=lambda inner, path: (reloaded.append(path), True)[1],
        ), patch.object(
            Game, "_v2_recovery_start_loaded_game", new=lambda inner: True,
        ), self.sync_recovery():
            self.sidecar_factory.fail_next = True
            self.sidecar_factory.created[-1].die()

        self.assertEqual([path.name for path in reloaded], [
            "turn-0007-auto.sav.gz",
        ])
        kinds = [item["kind"] for item in self.journal(game)]
        self.assertEqual(kinds, ["sidecar_reattach", "autosave_rollback"])
        self.assertTrue(all(
            item["trigger"] == "sidecar_exit" for item in self.journal(game)
        ))

    def test_the_status_poller_survives_a_recovered_exit(self):
        # Nothing restarts the status thread once it returns, so a poller that
        # stops after a recovered loss silently freezes the phase ledger.
        game, _joined = self.running_game()
        self.arm_forensics(self.sidecar_factory.created[-1])
        self.sidecar_factory.status_error = SidecarError("sidecar_unavailable")

        with self.sync_recovery(on_enter=lambda: setattr(
            self.sidecar_factory, "status_error", None,
        )):
            keep_polling = game._poll_v2_sidecars_once()

        self.assertTrue(
            keep_polling,
            "a recovered seat loss must not end the status poll loop",
        )
        self.assertEqual(game.state, "running")
        self.assertEqual(game.sidecar_generations[1], 2)

    def test_the_status_poller_stops_once_a_failed_game_is_terminal(self):
        game, _joined = self.running_game()
        with game.condition:
            game.v2_recovery_budget = RecoveryBudget(per_turn=0, per_game=0)
        self.arm_forensics(self.sidecar_factory.created[-1])
        self.sidecar_factory.status_error = SidecarError("sidecar_unavailable")

        with self.sync_recovery():
            game._poll_v2_sidecars_once()

        # Recovery was entered and then abandoned by the caps, so the loop is
        # ended by the terminal game state on its next tick rather than by the
        # exit itself.  Either way it does not spin forever.
        self.assertEqual(game.state, "failed")
        self.assertFalse(game._poll_v2_sidecars_once())

    def test_a_loss_while_starting_is_recovered_with_a_fresh_grace(self):
        game, _joined = self.running_game()
        with game.condition:
            game.state = "starting"
            game.sidecar_start_deadline = time.monotonic() - 60
        dying = self.sidecar_factory.created[-1]
        self.arm_forensics(dying)

        with self.sync_recovery():
            dying.die()

        self.assertEqual(game.sidecar_generations[1], 2)
        # The new generation must not be measured against the old deadline.
        self.assertGreater(game.sidecar_start_deadline, time.monotonic())

    def test_the_phase_watchdogs_do_not_fail_a_game_under_recovery(self):
        game, _joined = self.running_game()
        with game.condition:
            game.v2_recovery_in_flight[1] = {
                "kind": "autosave_rollback",
                "attempt": 2,
                "turn": 7,
                "started_at": time.time(),
                "target_turn": 7,
            }
            # Both stall clocks are already long past their limits.
            game.v2_phase_ledger["synchronizing_started_monotonic"] = (
                time.monotonic() - 10 * V2_PHASE_SYNCHRONIZE_STALL_S
            )
            game.v2_phase_ledger["end"] = {
                "claim_id": "claim", "key": (7, 0), "place": 1,
                "source": "agent", "receipt_state": "applied",
                "deadline_started_at": time.time(),
                "deadline_started_monotonic": time.monotonic(),
                "reconcile_started_monotonic": (
                    time.monotonic() - 10 * V2_PHASE_RECONCILE_STALL_S
                ),
            }

        # No evidence at all: exactly what a torn-down seat looks like.
        claim, failed = game._update_v2_phase_ledger([], time.monotonic())

        self.assertFalse(failed)
        self.assertEqual(game.state, "running")
        self.assertNotIn(
            "v2_phase_synchronization_stalled", game.invalid_reasons,
        )
        self.assertNotIn(
            "v2_phase_reconciliation_stalled", game.invalid_reasons,
        )

    def test_the_phase_watchdogs_restart_their_clocks_after_recovery(self):
        game, _joined = self.running_game()
        with game.condition:
            game.v2_recovery_in_flight[1] = {
                "kind": "sidecar_reattach", "attempt": 1, "turn": 7,
                "started_at": time.time(), "target_turn": None,
            }
            game.v2_phase_ledger["synchronizing_started_monotonic"] = (
                time.monotonic() - 10 * V2_PHASE_SYNCHRONIZE_STALL_S
            )
        game._update_v2_phase_ledger([], time.monotonic())
        with game.condition:
            # Recovery finishes; the clock must have been reset, not merely
            # ignored, or the very next sample fails the game.
            game.v2_recovery_in_flight.pop(1)
        _claim, failed = game._update_v2_phase_ledger([], time.monotonic())
        self.assertFalse(failed)
        self.assertEqual(game.state, "running")

    def test_the_phase_watchdogs_still_fail_a_game_with_no_recovery(self):
        game, _joined = self.running_game()
        with game.condition:
            game.v2_phase_ledger["synchronizing_started_monotonic"] = (
                time.monotonic() - 10 * V2_PHASE_SYNCHRONIZE_STALL_S
            )

        _claim, failed = game._update_v2_phase_ledger([], time.monotonic())

        self.assertTrue(failed)
        self.assertIn(
            "v2_phase_synchronization_stalled", game.invalid_reasons,
        )

    def test_the_watchdogs_hold_between_detection_and_the_recovery_thread(self):
        # Detection and registration are two steps: the loss is recorded in
        # v2_wedged_places, and only then does the recovery thread publish
        # itself in v2_recovery_in_flight.  A poll landing in that window must
        # not start a stall clock against a seat already known to be gone.
        game, _joined = self.running_game()
        with game.condition:
            game.v2_wedged_places[1] = {
                "trigger": "sidecar_exit", "turn": 7,
                "detected_at": time.time(), "generation": 1,
            }
            self.assertEqual(game.v2_recovery_in_flight, {})
            game.v2_phase_ledger["synchronizing_started_monotonic"] = (
                time.monotonic() - 10 * V2_PHASE_SYNCHRONIZE_STALL_S
            )

        _claim, failed = game._update_v2_phase_ledger([], time.monotonic())

        self.assertFalse(failed)
        self.assertEqual(game.state, "running")

    def test_a_seat_under_recovery_reports_itself_recovering_not_ready(self):
        game, joined = self.running_game()
        dying = self.sidecar_factory.created[-1]
        self.arm_forensics(dying)
        observed = {}

        def capture():
            observed.update(game.v2_health(joined["agent_id"]))

        # Sampled from inside the recovery, which is the only moment the
        # answer can be wrong in the way that matters.
        with self.sync_recovery(on_enter=capture):
            dying.die()

        self.assertEqual(observed["sidecar"]["state"], "wedged")
        # A dead client and an unusable projector are recovered the same way
        # but caused differently, so health must not give them one code.
        self.assertEqual(
            observed["sidecar"]["error_code"], "native_client_exited",
        )
        self.assertFalse(observed["observation_available"])
        self.assertFalse(observed["legal_actions_available"])
        self.assertEqual(
            observed["phase"]["waiting_on"]["kind"], "boundary_recovery",
        )
        self.assertIn(
            "native client exited",
            observed["phase"]["waiting_on"]["summary"],
        )
        # ...and it goes back to ready once the seat is republished.
        self.assertEqual(
            game.v2_health(joined["agent_id"])["sidecar"]["state"], "ready",
        )

    def test_the_death_context_places_the_loss_in_the_game(self):
        game, _joined = self.running_game()
        dying = self.sidecar_factory.created[-1]
        self.arm_forensics(dying)
        with game.condition:
            game.sidecar_health[1]["client_state"] = "running"

        with self.sync_recovery():
            dying.die()

        died_at = json.loads(
            (game.episode / "sidecar-exit-diagnostic.json").read_text(
                encoding="utf-8",
            ),
        )["died_at"]
        self.assertEqual(died_at["turn"], 7)
        self.assertEqual(died_at["phase"], 1)
        self.assertEqual(died_at["last_status_client_state"], "running")
        self.assertIsInstance(died_at["phase_ledger_state"], str)

    def test_an_unrecoverable_death_names_where_in_the_game_it_happened(self):
        game, _joined = self.running_game()
        with game.condition:
            game.v2_recovery_budget = RecoveryBudget(per_turn=0, per_game=0)
        dying = self.sidecar_factory.created[-1]
        self.arm_forensics(dying)

        with self.sync_recovery():
            dying.die()

        self.assertEqual(game.state, "failed")
        self.assertIn("turn 7", game.error)
        self.assertIn("phase 1", game.error)

    def test_the_turn_66_sequence_ends_in_a_recovered_game(self):
        """Applied phase end, then the client dies inside the turn change.

        This is the shape of the incident that ended
        ``game_XqynGMtFOtaqFbGXaF7lBx66``: the seat's phase end was applied,
        the server was healthy with a fresh autosave, and the sidecar process
        stopped during the T66->T67 ingestion.  Nothing about that is a reason
        to end a game.
        """
        _created, game, joined, action = self.ready_v2_action()
        with game.condition:
            game.start_sent = True
        status, receipt = game.v2_submit_batch(
            joined["agent_id"],
            self.v2_batch(game, joined, action, batch_id="turn_66_phase_end"),
        )
        self.assertEqual(status, HTTPStatus.OK)
        self.assertEqual(receipt["receipt_state"], "applied")
        end = game.v2_phase_ledger["end"]
        self.assertEqual(end["receipt_state"], "applied")
        # The transition has already been unreconciled for longer than the
        # watchdog tolerates, which is what makes this sequence lethal today.
        with game.condition:
            end["reconcile_started_monotonic"] = (
                time.monotonic() - 10 * V2_PHASE_RECONCILE_STALL_S
            )

        dying = game.sidecars[1]
        self.arm_forensics(
            dying, exit_code=None, exit_signal=None, exit_signal_name=None,
            process_alive=True, error_code="deadline_exceeded",
        )
        self.sidecar_factory.status_error = SidecarError("unexpected_eof")

        with self.sync_recovery(on_enter=lambda: setattr(
            self.sidecar_factory, "status_error", None,
        )):
            keep_polling = game._poll_v2_sidecars_once()

        self.assertTrue(keep_polling)
        self.assertEqual(game.state, "running")
        self.assertEqual(game.sidecar_generations[1], 2)
        self.assertNotIn(1, game.v2_wedged_places)
        record = self.journal(game)[-1]
        self.assertEqual(record["trigger"], "sidecar_exit")
        self.assertEqual(record["outcome"], "recovered")
        self.assertIsNone(record["exit_code"])

        # The rebuilt seat has not reported phase evidence yet, which is the
        # real state of the game the instant recovery returns.  Neither stall
        # clock may still be carrying the window in which the seat was gone.
        _claim, failed = game._update_v2_phase_ledger([], time.monotonic())
        self.assertFalse(failed)
        self.assertEqual(game.state, "running")

        # The game keeps running afterwards: the rebuilt seat is polled and
        # the ledger advances.
        self._seed_v2_phase(game)
        self.assertTrue(game._poll_v2_sidecars_once())
        self.assertEqual(game.state, "running")
        self.assertIsNone(game.error)
        self.assertEqual(game.invalid_reasons, [])

    def test_the_detach_window_does_not_recurse_into_a_second_recovery(self):
        # Tearing the wedged generation down stops a sidecar that has not
        # died, which fires its exit callback.  That is recovery's own doing
        # and must not be read as a fresh loss, or every recovery costs two
        # attempts and the budget halves.
        game, _joined = self.running_game()
        self.arm_forensics(self.sidecar_factory.created[-1])
        self.sidecar_factory.status_error = SidecarError("unexpected_eof")

        with self.sync_recovery(on_enter=lambda: setattr(
            self.sidecar_factory, "status_error", None,
        )):
            game._poll_v2_sidecars_once()

        # The old sidecar was stopped by recovery, so its callback ran.
        self.assertGreaterEqual(self.sidecar_factory.created[0].stop_count, 1)
        self.assertEqual(len(self.journal(game)), 1)
        # Exactly one attempt was taken, and being a successful re-attach it
        # was given back: what must never happen is a SECOND attempt.
        self.assertEqual(game.v2_recovery_budget.attempts_for_turn(1, 1), 0)
        self.assertEqual(game.v2_recovery_budget.total, 0)
        self.assertEqual(game.sidecar_generations[1], 2)
        self.assertEqual(game.state, "running")

    def test_strategic_v1_sidecar_handling_is_untouched(self):
        created = self.create()
        game = self.supervisor.game(created["game_id"])
        with game.condition:
            game.state = "running"
            game.start_sent = True
        self.assertFalse(game._v2_sidecar_exit_recoverable_locked(1))

    def test_strategic_v1_sidecar_loss_never_enters_recovery(self):
        created = self.create()
        game = self.supervisor.game(created["game_id"])
        with game.condition:
            game.state = "running"
            game.start_sent = True
        with self.sync_recovery():
            self.assertFalse(game._on_sidecar_exit(1, 1, {"state": "failed"}))
        self.assertEqual(game.v2_wedged_places, {})
        self.assertEqual(game.v2_recovery_in_flight, {})
        self.assertEqual(self.journal(game), [])
        self.assertEqual(game.state, "running")


class RecoveryDoesNotKillTheGameItSavesTests(_SidecarExitHarness):
    """Every way the safety net used to be the thing that ended the game."""

    def phase_claim(self, game, *, turn, phase, place=1):
        return {
            "key": (turn, phase),
            "place": place,
            "source": "agent",
            "receipt_state": "applied",
            "claim_id": f"claim-{turn}-{phase}-{place}",
            "deadline_started_at": time.time() - 1.0,
            "deadline_started_monotonic": time.monotonic() - 1.0,
        }

    def phase_events(self, game):
        path = game.episode / "phase-events.jsonl"
        if not path.exists():
            return []
        return [
            json.loads(line)
            for line in path.read_text(encoding="utf-8").splitlines()
        ]

    def test_a_replayed_phase_end_after_a_rollback_does_not_fail_the_game(self):
        # A SUCCESSFUL tier-2 rollback used to brick the game one phase later.
        # The ledger was rewound, but V2PhaseEventJournal.append enforced a
        # strictly-increasing (turn, phase) with no notion of a rewind, so the
        # first replayed phase end raised, invalidated the journal, and ended
        # the game as v2_phase_event_journal_unavailable.
        game, _joined = self.running_game()
        with game.condition:
            self.assertTrue(game._finalize_v2_phase_end_locked(
                self.phase_claim(game, turn=7, phase=0), "advanced",
            ))
            game._v2_rewind_phase_ledger_locked(7)
            replayed = game._finalize_v2_phase_end_locked(
                self.phase_claim(game, turn=7, phase=0), "advanced",
            )

        self.assertTrue(replayed)
        self.assertEqual(game.state, "running")
        self.assertIsNone(game.error)
        self.assertEqual(game.invalid_reasons, [])
        self.assertFalse(game.v2_phase_event_journal_failed)

        # Both records survive, ordered, and the replay is visibly a replay.
        events = self.phase_events(game)
        self.assertEqual([item["sequence"] for item in events], [1, 2])
        self.assertEqual([item["turn"] for item in events], [7, 7])
        self.assertEqual(events[0].get("incarnation", 0), 0)
        self.assertEqual(events[1]["incarnation"], 1)

    def test_a_rewind_discards_the_facts_that_describe_rewound_turns(self):
        # A surrender inside a rewound turn did not happen in the game that is
        # now running.  Leaving it behind parked the rescued seat in
        # inactive_done until the progress-stall clock ended the game -- a
        # deadlock created by the recovery itself.
        game, _joined = self.running_game()
        with game.condition:
            game.v2_surrendered_places.add(1)
            game.v2_applied_turns[1] = 12
            game.v2_pending_phase_ends["stuck"] = {"key": (9, 0)}
            self.assertEqual(game._v2_seat_standing_locked(1), "surrendered")

            game._v2_rewind_phase_ledger_locked(9)

            self.assertEqual(game.v2_surrendered_places, set())
            self.assertEqual(game.v2_applied_turns, {})
            self.assertEqual(game.v2_pending_phase_ends, {})
            self.assertNotEqual(game._v2_seat_standing_locked(1), "surrendered")

    def test_an_applied_turn_before_the_rewind_point_survives(self):
        game, _joined = self.running_game()
        with game.condition:
            game.v2_applied_turns[1] = 4
            game._v2_rewind_phase_ledger_locked(9)
            self.assertEqual(game.v2_applied_turns, {1: 4})

    def test_a_batch_id_resent_after_a_rollback_is_dispatched_again(self):
        # `just retry --batch_id ID` is the documented response to an
        # unresolved command, and an unresolved command is exactly what a
        # wedge produces right before a rollback.  Answering it from the
        # pre-rollback receipt tells the agent "applied" for an action the
        # current game has never seen.
        _created, game, joined, action = self.ready_v2_non_phase_action()
        batch = self.v2_batch(game, joined, action, batch_id="batch_retry")
        _status, receipt = game.v2_submit_batch(joined["agent_id"], batch)
        self.assertEqual(receipt["receipt_state"], "applied")

        store = game.v2_receipt_store
        self.assertIsNotNone(store.lookup(joined["agent_id"], "batch_retry"))

        with game.condition:
            game._v2_rewind_phase_ledger_locked(2)

        # The pre-rollback receipt is still durable evidence on disk, but the
        # current incarnation has never seen this batch id.
        self.assertIsNone(store.lookup(joined["agent_id"], "batch_retry"))
        self.assertIsNone(store.probe(batch))
        self.assertTrue(store.reserve(batch).created)

    def test_a_cancel_during_recovery_stays_a_cancel(self):
        # An owner cancel that landed mid-recovery was rewritten as a failed
        # game: the refusal became outcome='failed', which recursed with no
        # backoff, burned the turn's attempts and called _fail_v2_wedged_game
        # -- overwriting 'cancelled by owner' and appending v2_boundary_wedged.
        game, _joined = self.running_game()
        dying = self.sidecar_factory.created[-1]
        self.arm_forensics(dying)

        def cancel():
            with game.condition:
                game.cancel_requested = True
                game.error = "cancelled by owner"

        with self.sync_recovery(on_enter=cancel):
            dying.die()

        self.assertEqual(game.error, "cancelled by owner")
        self.assertEqual(game.invalid_reasons, [])
        self.assertNotEqual(game.state, "failed")
        self.assertEqual(
            [item["outcome"] for item in self.journal(game)], ["abandoned"],
        )
        # An abandoned attempt never ran, so it costs the game nothing.
        self.assertEqual(game.v2_recovery_budget.total, 0)

    def test_a_normal_teardown_during_recovery_is_not_a_wedge(self):
        # `sidecars_stopping` is what _poll_v2_sidecars_once sets on all_over,
        # i.e. an ordinary game-over.  Routing it into the wedge failure path
        # rewrote completed games as harness failures.
        game, _joined = self.running_game()
        dying = self.sidecar_factory.created[-1]
        self.arm_forensics(dying)

        def teardown():
            with game.condition:
                game.sidecars_stopping = True

        with self.sync_recovery(on_enter=teardown):
            dying.die()

        self.assertEqual(game.state, "running")
        self.assertIsNone(game.error)
        self.assertEqual(game.invalid_reasons, [])
        self.assertEqual(
            [item["outcome"] for item in self.journal(game)], ["abandoned"],
        )

    def test_a_rollback_does_not_route_the_other_seats_into_recovery(self):
        # `_v2_recovery_reload_server` disowns the server before killing it,
        # and the completion-grace deferral is gated on `process is not None`
        # -- exactly False in that window.  Every surviving seat went straight
        # into its own recovery against a server being replaced, drained the
        # shared budget, and failed the game mid-rollback.
        game, _joined = self.running_game()
        with game.condition:
            game.v2_server_replacing = True
            game.v2_recovery_in_flight[1] = {
                "kind": "autosave_rollback", "attempt": 2, "turn": 7,
                "started_at": time.time(), "target_turn": 5,
            }
            game.sidecar_ready_generations[2] = 1
            game.sidecar_generations[2] = 1
            game.place_agents[2] = "agent-two"

        keep_polling = game._on_sidecar_exit(2, 1, {
            "state": "failed", "error_code": "disconnected",
        })

        self.assertTrue(keep_polling)
        self.assertNotIn(2, game.v2_wedged_places)
        self.assertEqual(game.state, "running")
        self.assertIsNone(game.error)

    def test_the_latch_clears_so_a_real_loss_is_still_detected(self):
        # The latch must not outlive the window it describes, or a genuine
        # seat loss is suppressed for the rest of the game.
        game, _joined = self.running_game()
        saves = game.episode / "saves"
        saves.mkdir(exist_ok=True)
        (saves / "turn-0005-auto.sav.gz").write_bytes(b"save")
        with patch.object(
            Game, "_launch_from_save", new=lambda inner, path: None,
        ), patch.object(Game, "_v2_game_live", new=lambda inner: True):
            self.assertTrue(game._v2_recovery_reload_server(
                saves / "turn-0005-auto.sav.gz",
            ))
        self.assertFalse(game.v2_server_replacing)

    def test_a_reload_that_fails_still_clears_the_latch(self):
        game, _joined = self.running_game()
        saves = game.episode / "saves"
        saves.mkdir(exist_ok=True)
        with patch.object(
            Game, "_launch_from_save",
            new=lambda inner, path: (_ for _ in ()).throw(RuntimeError("no")),
        ):
            self.assertFalse(game._v2_recovery_reload_server(
                saves / "turn-0005-auto.sav.gz",
            ))
        self.assertFalse(game.v2_server_replacing)


class LivenessPollTests(_SidecarExitHarness):
    """One slow status sample is 'slow'.  It was being read as 'gone'."""

    def slow_status(self):
        return SidecarError("deadline_exceeded")

    def test_a_single_slow_poll_never_destroys_a_healthy_client(self):
        # The turn-66 mechanism.  `status(timeout_s=1.0)` was the tightest
        # budget in the system and ANY SidecarError became a seat loss, so one
        # latency tail SIGTERM/SIGKILLed a live, seat-owning client, bumped
        # the generation, and spent a recovery attempt on nothing.
        game, _joined = self.running_game()
        alive = self.sidecar_factory.created[-1]
        alive.private_exit_forensics = lambda: {"process_alive": True}
        self.sidecar_factory.status_error = self.slow_status()

        with self.sync_recovery():
            keep_polling = game._poll_v2_sidecars_once()

        self.assertTrue(keep_polling)
        self.assertEqual(alive.stop_count, 0)
        self.assertEqual(game.sidecar_generations[1], 1)
        self.assertEqual(game.v2_recovery_budget.total, 0)
        self.assertEqual(self.journal(game), [])
        self.assertEqual(game.state, "running")
        self.assertNotIn(1, game.v2_wedged_places)

    def test_two_slow_polls_on_one_turn_do_not_rewind_the_game(self):
        # A second blip used to escalate straight to tier 2, so pure client
        # latency discarded real play and was journalled as a native wedge.
        game, _joined = self.running_game()
        alive = self.sidecar_factory.created[-1]
        alive.private_exit_forensics = lambda: {"process_alive": True}
        self.sidecar_factory.status_error = self.slow_status()

        with self.sync_recovery():
            for _sample in range(6):
                self.assertTrue(game._poll_v2_sidecars_once())

        self.assertEqual(alive.stop_count, 0)
        self.assertEqual(game.sidecar_generations[1], 1)
        self.assertEqual(self.journal(game), [])
        self.assertEqual(game.state, "running")

    def test_a_live_process_is_slow_no_matter_how_many_samples_it_drops(self):
        game, _joined = self.running_game()
        alive = self.sidecar_factory.created[-1]
        alive.private_exit_forensics = lambda: {"process_alive": True}
        self.sidecar_factory.status_error = self.slow_status()
        with game.condition:
            # Pretend the misses already span the window.
            game.v2_liveness_misses[1] = (time.monotonic() - 3600.0, 99)

        with self.sync_recovery():
            self.assertTrue(game._poll_v2_sidecars_once())

        self.assertEqual(alive.stop_count, 0)
        self.assertEqual(game.state, "running")

    def test_a_run_of_misses_with_a_dead_process_is_still_a_loss(self):
        # Corroboration is a delay, not an amnesty: a client that really went
        # away must still be recovered.
        game, _joined = self.running_game()
        dying = self.sidecar_factory.created[-1]
        self.arm_forensics(dying)
        self.sidecar_factory.status_error = self.slow_status()
        with game.condition:
            game.v2_liveness_misses[1] = (time.monotonic() - 3600.0, 99)

        with self.sync_recovery(on_enter=lambda: setattr(
            self.sidecar_factory, "status_error", None,
        )):
            self.assertTrue(game._poll_v2_sidecars_once())

        self.assertEqual(game.sidecar_generations[1], 2)
        self.assertEqual(
            [item["outcome"] for item in self.journal(game)], ["recovered"],
        )

    def test_one_good_sample_forgets_every_miss_before_it(self):
        game, _joined = self.running_game()
        with game.condition:
            game.v2_liveness_misses[1] = (time.monotonic() - 3600.0, 99)
        self._seed_v2_phase(game)

        self.assertTrue(game._poll_v2_sidecars_once())
        self.assertEqual(game.v2_liveness_misses, {})

    def test_the_liveness_budget_is_the_loosest_in_the_system(self):
        # A health check has no caller waiting on its answer; it must never be
        # tighter than the request path or the recovery path.
        from agent_eval.supervisor import V2_LIVENESS_POLL_TIMEOUT_S

        self.assertGreaterEqual(V2_LIVENESS_POLL_TIMEOUT_S, 5.0)


class SeatLossAttributionTests(_SidecarExitHarness):
    """Name the loss by the evidence, never by assumption."""

    def unrecoverable(self, game):
        with game.condition:
            game.v2_recovery_budget = RecoveryBudget(per_turn=0, per_game=0)
            # Make the loss unrecoverable so the manifest error is written.
            game.state = "starting"
            game.start_sent = True

    def fail_seat(self, forensics):
        game, _joined = self.running_game()
        with game.condition:
            game.process = None
            game.start_sent = True
        dying = self.sidecar_factory.created[-1]
        dying.private_exit_forensics = lambda: dict(forensics)
        with patch.object(
            Game, "_v2_sidecar_exit_recoverable_locked",
            new=lambda inner, place: False,
        ):
            dying.die()
        return game

    def test_a_client_that_is_still_running_is_not_reported_as_exited(self):
        # The sentence that sent the turn-66 hunt after a native crash for a
        # day: "sidecar exited (... stopped answering while still running)".
        game = self.fail_seat({
            "process_alive": True, "client_state": "running",
        })
        self.assertNotIn("sidecar exited", game.error)
        self.assertIn("stopped answering while still running", game.error)
        self.assertIn("sidecar_unresponsive", game.invalid_reasons)
        self.assertNotIn("sidecar_exited", game.invalid_reasons)

    def test_an_observed_exit_status_is_still_reported_as_an_exit(self):
        game = self.fail_seat({"exit_code": 1, "process_alive": False})
        self.assertIn("full-control-v2 sidecar exited", game.error)
        self.assertIn("sidecar_exited", game.invalid_reasons)

    def test_health_says_whether_the_process_is_still_alive(self):
        # Without these three fields an operator watching a live game sees
        # `state=failed, exit_code=null` -- byte-identical to a silent death.
        game, _joined = self.running_game()
        sidecar = self.sidecar_factory.created[-1]
        sidecar.public_health = lambda: {
            "state": "failed", "generation": sidecar.generation,
            "player_name": sidecar.player_name, "exit_code": None,
            "exit_signal": None, "exit_signal_name": None,
            "process_alive": True, "error_code": "status_unavailable",
        }
        health = game._sanitized_sidecar_health(sidecar, 1)
        self.assertIs(health["process_alive"], True)
        self.assertIsNone(health["exit_signal"])
        self.assertIsNone(health["exit_signal_name"])


class RecoveryIsVisibleToScoringTests(_SidecarExitHarness):
    """A rewound game may not be ranked against a clean one."""

    def test_the_manifest_carries_what_had_to_be_recovered(self):
        game, _joined = self.running_game()
        dying = self.sidecar_factory.created[-1]
        self.arm_forensics(dying)

        with self.sync_recovery():
            dying.die()

        with game.condition:
            game._write_manifest()
        manifest = json.loads(
            (game.episode / "manifest.json").read_text(encoding="utf-8"),
        )
        self.assertEqual(manifest["recovery"]["attempts"], 1)
        self.assertEqual(
            manifest["recovery"]["by_kind"], {"sidecar_reattach": 1},
        )
        self.assertFalse(manifest["recovery"]["rewound_applied_actions"])

    def test_a_clean_game_says_so_by_carrying_no_recovery_block(self):
        game, _joined = self.running_game()
        with game.condition:
            game._write_manifest()
        manifest = json.loads(
            (game.episode / "manifest.json").read_text(encoding="utf-8"),
        )
        self.assertIsNone(manifest["recovery"])

    def test_a_rewound_game_is_marked_invalid_for_ranking(self):
        game, _joined = self.running_game()
        with game.condition:
            game._note_v2_recovery_in_summary_locked({
                "place": 1, "kind": "autosave_rollback",
                "outcome": "recovered", "recovered_to_turn": 5,
                "rewound_applied_actions": True,
            })
        self.assertIn("v2_game_rewound", game.invalid_reasons)
        with game.condition:
            block = game._v2_recovery_manifest_locked()
        self.assertTrue(block["rewound_applied_actions"])
        self.assertEqual(block["recovered_to_turns"], [5])

    def test_the_scorer_reads_the_journal_even_without_a_manifest(self):
        from agent_eval.scoring import summarize_episode

        game, _joined = self.running_game()
        dying = self.sidecar_factory.created[-1]
        self.arm_forensics(dying)
        with self.sync_recovery():
            dying.die()

        summary = summarize_episode(game.episode)
        self.assertEqual(summary["recovery"]["attempts"], 1)
        self.assertEqual(
            summary["recovery"]["by_outcome"], {"recovered": 1},
        )


class SidecarExitForensicsTests(unittest.TestCase):
    """What the sidecar itself can say about how it stopped."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def sidecar(self, *, returncode, stderr=""):
        """A real sidecar object that has failed, with a stubbed child.

        Built through the actual constructor rather than ``__new__``: forensics
        read the whole recorded lifecycle, so an instance assembled field by
        field would silently stop matching the class it stands for.
        """
        from agent_eval.headless_sidecar import HeadlessSidecar

        instance = HeadlessSidecar(
            binary=self.root / "freeciv-agent",
            run_root=self.root / "sidecars",
            game_id="game_forensics-0000000000",
            seat_id="place-1",
            player_name="Forensics",
            host="127.0.0.1",
            port=5555,
            generation=1,
        )
        instance.run_directory.mkdir(parents=True, exist_ok=True)
        instance._process = SimpleNamespace(poll=lambda: returncode)
        instance._state = "failed"
        instance._client_state = "running"
        instance._error_code = "process_exited"
        instance._last_seen_at = 99.0
        instance.stderr_path.write_text(stderr, encoding="utf-8")
        instance.stdout_path.write_text("", encoding="utf-8")
        return instance

    def test_a_signal_death_is_named(self):
        forensics = self.sidecar(returncode=-11).private_exit_forensics()
        self.assertEqual(forensics["exit_code"], -11)
        self.assertEqual(forensics["exit_signal"], 11)
        self.assertEqual(forensics["exit_signal_name"], "SIGSEGV")
        self.assertIs(forensics["process_alive"], False)

    def test_a_clean_exit_code_carries_no_signal(self):
        forensics = self.sidecar(returncode=3).private_exit_forensics()
        self.assertEqual(forensics["exit_code"], 3)
        self.assertIsNone(forensics["exit_signal"])

    def test_a_live_process_reports_no_exit_status(self):
        # The distinction that the turn-66 incident had no way to express.
        forensics = self.sidecar(returncode=None).private_exit_forensics()
        self.assertIsNone(forensics["exit_code"])
        self.assertIs(forensics["process_alive"], True)
        self.assertEqual(forensics["client_state"], "running")

    def test_the_stderr_tail_is_bounded_to_the_last_lines(self):
        stderr = "\n".join(f"line {index}" for index in range(200))
        forensics = self.sidecar(
            returncode=-6, stderr=stderr,
        ).private_exit_forensics(tail_lines=30)
        self.assertEqual(len(forensics["stderr_tail"]), 30)
        self.assertEqual(forensics["stderr_tail"][-1], "line 199")

    def test_a_missing_log_is_not_an_error(self):
        instance = self.sidecar(returncode=0)
        instance.stderr_path.unlink()
        self.assertEqual(instance.private_exit_forensics()["stderr_tail"], ())


class WedgedCityProjectionTests(unittest.TestCase):
    """The projection rule whose old form produced the original wedge.

    Freeciv's mood counters (``feel[*][FEELING_FINAL]``) describe only the
    citizens who are not specialists: ``citizen_base_mood`` subtracts
    ``city_specialists`` before distributing them, and the client reassembles
    city size as the mood total plus the normal specialists.  A projector that
    asserts the mood total equals the size therefore accepts every city until
    the first specialist appears, and then rejects every observation forever.
    """

    # The fixture city is size 2; every case below re-splits those two
    # citizens between tiles and specialists rather than resizing it, so no
    # other size-derived field has to move with it.
    SIZE = 2

    def compact(self, control, rows, *, revision=11):
        """Merge already-authored rows without re-normalizing their citizens.

        The shared ``compact_bundle`` helper reconciles every city row's
        citizen counts against its children, which would overwrite exactly the
        values these cases are about.
        """
        from agent_eval.tests.test_v2_control import observation

        section_prefixes = {
            "cities": ("city ", "city_rally ", "city_worker_task "),
            "units": ("unit ", "unit_route "),
            "city_sites": ("city_site ",),
        }
        scoped = (
            "city_tile ", "city_specialist ", "city_worklist ",
            "city_build_choice ", "city_improvement ",
        )
        removed = tuple(
            prefix for prefixes in section_prefixes.values()
            for prefix in prefixes
        ) + scoped + ("diplomacy_clause ", "tombstone ")
        compact = dict(observation((), revision=revision))
        compact["rows"] = tuple(
            row for row in rows if not row.startswith(removed)
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
        return dict(
            control.materialize_observation_catalogs(compact, catalogs)
        )

    def rows(self, *, happy, content, unhappy, angry, workers,
             specialists):
        import re

        from agent_eval.tests.test_v2_control import complete_v2_rows, valid_rows

        counters = (
            f"citizen_happy={happy} citizen_content={content} "
            f"citizen_unhappy={unhappy} citizen_angry={angry} "
            f"citizen_workers={workers} citizen_specialists={specialists}"
        )
        rows = []
        for row in complete_v2_rows(valid_rows(actions=False)):
            if row.startswith("city ref=c:20:200 "):
                row = re.sub(
                    r"citizen_happy=[0-9]+ citizen_content=[0-9]+ "
                    r"citizen_unhappy=[0-9]+ citizen_angry=[0-9]+ "
                    r"citizen_workers=[0-9]+ citizen_specialists=[0-9]+",
                    counters,
                    row,
                    count=1,
                )
            rows.append(row)
        return tuple(rows)

    def project(self, *, happy, content, unhappy, angry, workers,
                specialists):
        from agent_eval.v2_control import V2SeatControl

        rows = self.rows(
            happy=happy, content=content, unhappy=unhappy, angry=angry,
            workers=workers, specialists=specialists,
        )
        control = V2SeatControl("game_x", "agent_x", 1)
        try:
            return control.state_page(self.compact(control, rows), "cities")
        finally:
            control.close()

    def test_a_city_with_an_entertainer_projects_instead_of_wedging(self):
        page = self.project(
            happy=0, content=1, unhappy=0, angry=0,
            workers=1, specialists=1,
        )
        self.assertEqual(page["page"]["items"][0]["size"], self.SIZE)

    def test_a_city_with_no_specialist_still_projects(self):
        page = self.project(
            happy=0, content=2, unhappy=0, angry=0,
            workers=2, specialists=0,
        )
        self.assertEqual(page["page"]["items"][0]["size"], self.SIZE)

    def test_a_mood_total_that_counts_specialists_twice_is_recorded_not_fatal(self):
        # The shape the old rule DEMANDED: mood total equal to size while a
        # specialist also exists.  Freeciv's own emitter cannot produce it from
        # a coherent city -- but client/packhand.c handle_city_info() can, and
        # deliberately does: on a server/client citizen disagreement it logs
        # "%d citizens not equal %d city size" and OVERRIDES with
        # city_size_set(pcity, packet->size), leaving feel[] and specialists[]
        # untouched.  The native client keeps playing through that, so the
        # projector must too: the city row is in EVERY observation bundle, and
        # rejecting on it would brick the seat forever -- the turn-52 shape one
        # layer down.  Record and name it instead.
        page = self.project(
            happy=0, content=2, unhappy=0, angry=0,
            workers=1, specialists=1,
        )
        self.assertEqual(page["page"]["items"][0]["size"], self.SIZE)

    def test_the_recorded_citizen_anomaly_names_itself(self):
        from agent_eval.v2_control import V2SeatControl

        control = V2SeatControl("game_anomaly", "agent_x", 1)
        try:
            rows = self.rows(
                happy=0, content=2, unhappy=0, angry=0,
                workers=1, specialists=1,
            )
            control.state_page(self.compact(control, rows), "cities")
            self.assertEqual(
                control.native_anomalies.get("city_citizen_counts"), 1,
            )
        finally:
            control.close()

    def test_workers_and_specialists_must_still_account_for_the_size(self):
        with self.assertRaises(V2ControlError):
            self.project(
                happy=0, content=1, unhappy=0, angry=0,
                workers=1, specialists=0,
            )


class WedgedWorkerTaskProjectionTests(unittest.TestCase):
    """The second wedge-class rule: worker tasks outliving the tile catalog.

    ``city_worker_task`` rows travel with the cities catalog and so reach a
    compact observation (`v2_control.py:2547`, emitted at
    `client/gui-agent/protocol_v2.c:8108-8129`), while tile rows are exported
    only through STATE_SCOPE and do not
    (`client/gui-agent/protocol_v2.c:7553-7554`).  A rule that demanded a
    matching tile row for every worker task therefore rejected every
    observation for as long as the task persisted.
    """

    WORKER_TASK = (
        "city_worker_task city=c:20:200 tile=5 activity=road "
        "target_extra=7 target_extra_name=Road want=80"
    )

    def rows_with_task(self):
        from agent_eval.tests.test_v2_control import complete_v2_rows, valid_rows

        # No action rows: a tile-targeted action carries its own separate
        # requirement on the tile catalog, which would confound these cases.
        return tuple(sorted(
            (*complete_v2_rows(valid_rows(actions=False)), self.WORKER_TASK),
        ))

    def test_a_worker_task_projects_when_no_tile_rows_are_exported(self):
        from agent_eval.v2_control import V2SeatControl

        control = V2SeatControl("game_x", "agent_x", 1)
        try:
            # Production never exports tile rows in the compact bundle; the
            # shared fixture still carries them, so drop them here.
            bundle = WedgedCityProjectionTests.compact(
                self, control,
                tuple(
                    row for row in self.rows_with_task()
                    if not row.startswith("tile ")
                ),
            )
            self.assertFalse(
                any(row.startswith("tile ") for row in bundle["rows"]),
                "the compact observation must carry no tile rows",
            )
            page = control.state_page(bundle, "cities")
        finally:
            control.close()
        self.assertTrue(page["page"]["items"])

    def test_a_full_tile_catalog_still_cross_checks_the_worker_task(self):
        from agent_eval.v2_control import V2SeatControl
        from agent_eval.tests.test_v2_control import observation

        # Point the task at a tile the seat cannot work, with the whole tile
        # catalog present.  The check must still fire.
        rows = tuple(sorted((
            *(
                row for row in self.rows_with_task()
                if row != self.WORKER_TASK
            ),
            self.WORKER_TASK.replace("tile=5", "tile=4096"),
        )))
        control = V2SeatControl("game_x", "agent_x", 1)
        try:
            with self.assertRaises(V2ControlError):
                control.state_page(observation(rows), "cities")
        finally:
            control.close()

    def test_extra_name_consistency_still_holds_without_a_tile_catalog(self):
        from agent_eval.v2_control import V2SeatControl

        # Guarding the tile requirement must not disarm the checks that stay
        # valid when no tile rows were exported: two worker tasks naming the
        # same native extra with different names is still a fault.
        rows = tuple(sorted((
            *(
                row for row in self.rows_with_task()
                if not row.startswith("tile ")
            ),
            self.WORKER_TASK.replace(
                "tile=5 activity=road", "tile=6 activity=mine",
            ).replace("target_extra_name=Road", "target_extra_name=Mine"),
        )))
        control = V2SeatControl("game_x", "agent_x", 1)
        try:
            with self.assertRaises(V2ControlError):
                control.state_page(
                    WedgedCityProjectionTests.compact(self, control, rows),
                    "cities",
                )
        finally:
            control.close()

    def test_an_unknown_city_ref_still_fails_without_a_tile_catalog(self):
        from agent_eval.v2_control import V2SeatControl

        rows = tuple(sorted((
            *(
                row for row in self.rows_with_task()
                if not row.startswith("tile ") and row != self.WORKER_TASK
            ),
            self.WORKER_TASK.replace("city=c:20:200", "city=c:99:999"),
        )))
        control = V2SeatControl("game_x", "agent_x", 1)
        try:
            with self.assertRaises(V2ControlError):
                control.state_page(
                    WedgedCityProjectionTests.compact(self, control, rows),
                    "cities",
                )
        finally:
            control.close()

    def test_a_full_tile_catalog_accepts_a_task_on_a_visible_tile(self):
        from agent_eval.v2_control import V2SeatControl
        from agent_eval.tests.test_v2_control import observation

        control = V2SeatControl("game_x", "agent_x", 1)
        try:
            page = control.state_page(
                observation(self.rows_with_task()), "cities",
            )
        finally:
            control.close()
        self.assertTrue(page["page"]["items"])


class _FakeShutdownEvent:
    """A shutdown latch that trips on a counted wait instead of a clock."""

    def __init__(self, *, stop_after):
        self.stop_after = stop_after
        self.waits = 0

    def is_set(self):
        return False

    def set(self):
        return None

    def wait(self, timeout=None):
        self.waits += 1
        return self.waits >= self.stop_after


class _FakeStdin:
    def __init__(self, written):
        self.written = written
        self.closed = False

    def write(self, value):
        self.written.append(value)

    def flush(self):
        return None

    def close(self):
        self.closed = True


class LifecycleRaceTests(_SeatHarness):
    """Process and thread lifetimes across a seat or server replacement.

    Every case here is a race that ends a live game quietly: a thread that
    stops running, a server nobody reaps, a console batch that lands on the
    wrong process, a rewind the watchdogs read as corruption.  Each one is
    injected deterministically, with hooks rather than sleeps.
    """

    def running_game(self):
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        joined = game.join(
            created["join_token"], controller_label="codex-lifecycle-model",
            supported_control_protocols=["full-control-v2"],
        )
        self._mark_v2_running(game)
        with game.condition:
            game.start_sent = True
        self._seed_v2_phase(game, turn=7)
        return game, joined

    def fake_process(self, written=None, *, alive=True):
        record = SimpleNamespace(
            written=[] if written is None else written,
            terminated=0,
            killed=0,
        )
        record.stdin = _FakeStdin(record.written)
        record.poll = lambda: None if alive else 0
        record.wait = lambda **kwargs: 0
        record.terminate = lambda: setattr(
            record, "terminated", record.terminated + 1,
        )
        record.kill = lambda: setattr(record, "killed", record.killed + 1)
        return record

    def journal(self, game):
        path = game.episode / RECOVERY_DIRECTORY / RECOVERY_FILENAME
        if not path.exists():
            return []
        return [
            json.loads(line)
            for line in path.read_text(encoding="ascii").splitlines()
        ]

    def sync_recovery(self):
        return patch.object(
            Game, "_start_v2_boundary_recovery",
            new=lambda inner, place, detected:
                inner._run_v2_boundary_recovery(place, detected),
        )

    # ---- the status poller's own lifetime -----------------------------

    def test_a_poll_losing_a_retired_generation_is_not_a_dead_game(self):
        # Recovery retires a generation on its own thread while a poll is
        # already in flight against it.  The poll's status call then fails
        # against a sidecar nobody owns, which belongs to nobody: it is not
        # the loss of a seat, and it is certainly not the end of the game.
        game, _joined = self.running_game()
        sidecar = game.sidecars[1]

        def retire_then_fail(timeout_s=1.0):
            with game.condition:
                game.sidecar_generations[1] = 2
            raise SidecarError("unexpected_eof")

        sidecar.status = retire_then_fail

        self.assertFalse(game._poll_v2_sidecars_once())
        self.assertEqual(game.state, "running")
        self.assertIsNone(game.error)
        self.assertTrue(game._v2_game_live())

    def test_the_status_poll_loop_ends_only_when_the_game_does(self):
        # Nothing restarts this thread, so a poll that declines to continue
        # must not be able to end it while the game is still being played.
        game, _joined = self.running_game()
        polls = []
        event = _FakeShutdownEvent(stop_after=3)
        self.addCleanup(
            setattr, self.supervisor, "shutdown_event",
            self.supervisor.shutdown_event,
        )
        self.supervisor.shutdown_event = event

        with patch.object(
            Game, "_poll_v2_sidecars_once",
            new=lambda inner: bool(polls.append(True)),
        ):
            game._poll_v2_sidecars()

        self.assertEqual(len(polls), 3)
        self.assertEqual(event.waits, 3)

    def test_the_status_poll_loop_stops_at_once_on_a_terminal_game(self):
        game, _joined = self.running_game()
        polls = []
        event = _FakeShutdownEvent(stop_after=99)
        self.addCleanup(
            setattr, self.supervisor, "shutdown_event",
            self.supervisor.shutdown_event,
        )
        self.supervisor.shutdown_event = event
        with game.condition:
            game.state = "failed"

        with patch.object(
            Game, "_poll_v2_sidecars_once",
            new=lambda inner: bool(polls.append(True)),
        ):
            game._poll_v2_sidecars()

        # One poll, no tick: a finished game must not leave a thread spinning.
        self.assertEqual(len(polls), 1)
        self.assertEqual(event.waits, 0)

    def test_a_run_of_poll_faults_fails_the_game_instead_of_vanishing(self):
        # An unexpected exception used to end the status thread outright,
        # which leaves a live game nobody samples: no phase advances, no
        # deadline fires, and no artifact anywhere says what happened.
        game, _joined = self.running_game()
        event = _FakeShutdownEvent(stop_after=99)
        self.addCleanup(
            setattr, self.supervisor, "shutdown_event",
            self.supervisor.shutdown_event,
        )
        self.supervisor.shutdown_event = event

        def always_faults(inner):
            raise RuntimeError("/private/run/dir leaked into the message")

        with patch.object(
            Game, "_poll_v2_sidecars_once", new=always_faults,
        ):
            game._poll_v2_sidecars()

        self.assertEqual(game.state, "failed")
        self.assertIn("v2_status_poll_failed", game.invalid_reasons)
        self.assertIn("RuntimeError", game.error)
        # The exception's text can carry paths; its type cannot.
        self.assertNotIn("/private/run/dir", game.error)
        self.assertEqual(event.waits, V2_STATUS_POLL_FAULT_LIMIT - 1)

    def test_a_single_poll_fault_is_survived(self):
        game, _joined = self.running_game()
        event = _FakeShutdownEvent(stop_after=4)
        self.addCleanup(
            setattr, self.supervisor, "shutdown_event",
            self.supervisor.shutdown_event,
        )
        self.supervisor.shutdown_event = event
        polls = []

        def flaky(inner):
            polls.append(True)
            # Fault, recover, fault again: a run has to be uninterrupted.
            if len(polls) in {1, 3}:
                raise RuntimeError("transient")
            return True

        with patch.object(Game, "_poll_v2_sidecars_once", new=flaky):
            game._poll_v2_sidecars()

        self.assertEqual(len(polls), 4)
        self.assertEqual(game.state, "running")
        self.assertIsNone(game.error)

    # ---- the phase watchdogs versus a rewound game --------------------

    def test_a_rollback_rewinds_the_phase_ledger_with_the_game(self):
        # The ledger key is the last turn and phase every seat agreed on, and
        # evidence that goes backwards fails the game.  A rollback moves the
        # game backwards on purpose, so a ledger left at the pre-rollback key
        # turns the first honest sample from the reloaded server into a phase
        # regression -- recovery succeeds and the game dies of it.
        game, _joined = self.running_game()
        saves = game.episode / "saves"
        saves.mkdir(exist_ok=True)
        (saves / "turn-0005-auto.sav.gz").write_bytes(b"save")
        self.assertEqual(game.v2_phase_ledger["key"], (7, 1))
        with game.condition:
            game.v2_phase_ledger["deadline_started_monotonic"] = (
                time.monotonic()
            )

        dying = self.sidecar_factory.created[-1]
        dying.private_exit_forensics = lambda: {"exit_code": 1}
        with patch.object(
            Game, "_v2_recovery_reload_server", new=lambda inner, path: True,
        ), patch.object(
            Game, "_v2_recovery_start_loaded_game", new=lambda inner: True,
        ), self.sync_recovery():
            # Fail the first tier so recovery has to escalate to a rollback.
            self.sidecar_factory.fail_next = True
            dying.die()

        self.assertEqual(self.journal(game)[-1]["outcome"], "recovered")
        self.assertEqual(game.v2_phase_ledger["key"], (5, 0))
        self.assertIsNone(
            game.v2_phase_ledger["deadline_started_monotonic"],
        )

        # ...and the reloaded server's own first sample is accepted.
        self._seed_v2_phase(game, turn=5, phase=0)
        self.assertEqual(game.state, "running")
        self.assertNotIn("v2_phase_regression", game.invalid_reasons)
        self.assertIsNone(game.error)

    def test_a_rewound_ledger_still_refuses_evidence_older_than_the_rewind(self):
        # Rewinding is not a licence to accept anything afterwards: only the
        # turn the game was actually reloaded to.
        game, _joined = self.running_game()
        with game.condition:
            game._v2_rewind_phase_ledger_locked(5)

        _claim, failed = game._update_v2_phase_ledger(
            self.phase_evidence(game, turn=4, phase=0, active_place=1),
            time.monotonic(),
        )

        self.assertTrue(failed)
        self.assertIn("v2_phase_regression", game.invalid_reasons)

    def test_the_progress_watchdog_is_held_while_recovery_runs(self):
        game, _joined = self.running_game()
        now = time.monotonic()
        rows = self.phase_evidence(
            game, turn=7, phase=1, active_place=1, ready=False,
        )
        with game.condition:
            game.v2_recovery_in_flight[1] = {
                "kind": "sidecar_reattach", "attempt": 1, "turn": 7,
                "started_at": time.time(), "target_turn": None,
            }
            game.v2_phase_ledger["progress_marker"] = (
                (7, 1), "phase_not_ready", 1,
            )
            game.v2_phase_ledger["progress_started_monotonic"] = (
                now - 10 * V2_PHASE_PROGRESS_STALL_S
            )

        _claim, failed = game._update_v2_phase_ledger(rows, now)

        self.assertFalse(failed)
        self.assertEqual(game.state, "running")
        self.assertNotIn("v2_phase_progress_stalled", game.invalid_reasons)
        # Held at this sample, not merely skipped: a clock that keeps its old
        # start fires the instant recovery finishes.
        self.assertEqual(
            game.v2_phase_ledger["progress_started_monotonic"], now,
        )

    def test_the_progress_watchdog_still_fails_a_game_with_no_recovery(self):
        game, _joined = self.running_game()
        now = time.monotonic()
        rows = self.phase_evidence(
            game, turn=7, phase=1, active_place=1, ready=False,
        )
        with game.condition:
            self.assertEqual(game.v2_recovery_in_flight, {})
            game.v2_phase_ledger["progress_marker"] = (
                (7, 1), "phase_not_ready", 1,
            )
            game.v2_phase_ledger["progress_started_monotonic"] = (
                now - 10 * V2_PHASE_PROGRESS_STALL_S
            )

        _claim, failed = game._update_v2_phase_ledger(rows, now)

        self.assertTrue(failed)
        self.assertIn("v2_phase_progress_stalled", game.invalid_reasons)

    def test_a_recovery_that_never_finishes_still_dies_at_the_caps(self):
        # The watchdogs are held for as long as recovery runs, so the caps are
        # the only thing left bounding a recovery that cannot succeed.  If
        # they did not fire, holding the watchdogs would have replaced a
        # bricked seat with a game that hangs forever instead.
        game, _joined = self.running_game()
        dying = self.sidecar_factory.created[-1]
        dying.private_exit_forensics = lambda: {"exit_code": 1}
        samples = []

        def never_rebuilds(inner, place, kind, before_attach=None):
            # Sample the watchdogs from inside the stuck recovery.
            samples.append(inner._update_v2_phase_ledger([], time.monotonic()))
            return False

        with patch.object(
            Game, "_v2_recovery_rebuild_seat", new=never_rebuilds,
        ), self.sync_recovery():
            dying.die()

        self.assertTrue(samples)
        self.assertFalse(any(failed for _claim, failed in samples))
        self.assertEqual(game.state, "failed")
        self.assertIn("recovery attempts", game.error)
        self.assertEqual(self.journal(game)[-1]["outcome"], "abandoned")
        # ...and the status poller is released by that terminal state.
        self.assertFalse(game._v2_game_live())

    def test_a_teardown_that_begins_mid_recovery_is_never_undone(self):
        # Neither entry point starts a recovery while the seats are being torn
        # down, so a stopping latch seen by a recovery already under way
        # belongs to a teardown that began after it.  Retaking the seat there
        # reconnects a client to a server that cannot exit until its clients
        # leave, and the teardown has already sampled the sidecars it means to
        # stop, so nothing would ever stop the new one.
        game, _joined = self.running_game()
        original = game.sidecars[1]
        with game.condition:
            game.v2_wedged_places[1] = {
                "trigger": "sidecar_exit", "turn": 7,
                "detected_at": time.time(), "generation": 1,
            }
            game.sidecars_stopping = True

        self.assertFalse(
            game._v2_recovery_rebuild_seat(1, "sidecar_reattach"),
        )

        self.assertTrue(game.sidecars_stopping)
        self.assertIs(game.sidecars[1], original)
        self.assertEqual(game.sidecar_generations[1], 1)
        self.assertEqual(original.stop_count, 0)

    # ---- the server process across a replacement ----------------------

    def test_a_superseded_server_monitor_never_publishes_its_error(self):
        # A retired server's monitor can fail on its way out.  Publishing that
        # error sets the game's failure for the *next* classification, so a
        # process recovery deliberately replaced would end the game that
        # replaced it.
        game, _joined = self.running_game()
        live = self.fake_process()
        with game.condition:
            game.process = live

        def explode():
            raise OSError("wait() failed on the retired server")

        retired = SimpleNamespace(wait=explode, stdin=None, poll=lambda: 1)
        game._monitor(retired)

        self.assertIsNone(game.error)
        self.assertEqual(game.state, "running")
        self.assertFalse(game.server_exit_observed)

    def test_the_current_server_monitor_still_publishes_its_error(self):
        game, _joined = self.running_game()

        def explode():
            raise OSError("wait() failed on the live server")

        current = SimpleNamespace(wait=explode, stdin=None, poll=lambda: 1)
        with game.condition:
            game.process = current

        with patch("agent_eval.supervisor.summarize_episode", return_value={}):
            game._monitor(current)

        self.assertIn("could not monitor freeciv-server", game.error)
        self.assertEqual(game.state, "failed")

    def test_a_console_batch_never_finishes_against_a_replaced_server(self):
        # A rollback can disown and replace the server between two commands of
        # one batch.  Half a batch of settings applied to a server that was
        # never meant to receive them is worse than a refused batch.
        game, _joined = self.running_game()
        first = self.fake_process()
        second = self.fake_process()
        with game.condition:
            game.process = first

        def swap_server(inner, timeout_s=20):
            with inner.condition:
                inner.process = second

        self.send.stop()
        try:
            with patch.object(Game, "_wait_for_prompt", new=swap_server):
                with self.assertRaises(SupervisorError) as caught:
                    game._send_commands(["set timeout 0", "set saveturns 1"])
        finally:
            self.send.start()

        self.assertIn("replaced", str(caught.exception))
        self.assertEqual(first.written, [b"set timeout 0\n"])
        self.assertEqual(second.written, [])

    def test_a_server_launched_into_a_failed_game_is_never_left_running(self):
        # Terminalization on another thread runs while the old server is gone
        # and the new one does not exist yet, so its terminate finds nothing.
        # Whatever comes up afterwards has to reap itself.
        game, _joined = self.running_game()
        previous = self.fake_process()
        with game.condition:
            game.process = previous
        launched = self.fake_process()

        def launch_into_a_failed_game(inner, save_path):
            with inner.condition:
                # Exactly what a concurrent _fail_v2_wedged_game leaves
                # behind: a terminal game and no process to terminate.
                inner.state = "failed"
                inner.error = "failed while the server was being replaced"
                self.assertIsNone(inner.process)
                inner.process = launched

        with patch.object(
            Game, "_launch_from_save", new=launch_into_a_failed_game,
        ):
            advanced = game._v2_recovery_reload_server(
                game.episode / "saves" / "turn-0005-auto.sav.gz",
            )

        self.assertFalse(advanced)
        self.assertEqual(launched.terminated, 1)

    def test_a_server_launched_into_a_shutdown_is_never_left_running(self):
        # The service stops games by cancelling them and waiting on their
        # process.  A rollback holding no process at that instant is waited on
        # for nothing, so a server that comes up afterwards would outlive the
        # supervisor that launched it.
        game, _joined = self.running_game()
        previous = self.fake_process()
        with game.condition:
            game.process = previous
        launched = self.fake_process()

        def launch_into_a_shutdown(inner, save_path):
            inner.supervisor.shutdown_event.set()
            with inner.condition:
                inner.process = launched

        with patch.object(
            Game, "_launch_from_save", new=launch_into_a_shutdown,
        ):
            advanced = game._v2_recovery_reload_server(
                game.episode / "saves" / "turn-0005-auto.sav.gz",
            )

        self.assertFalse(advanced)
        self.assertEqual(launched.terminated, 1)

    def test_a_replacement_never_inherits_the_retired_console_state(self):
        # One output pump owns at_prompt and the ordered output lines.  If the
        # retired pump is still draining a dead server's pipe, its bytes are
        # read as the new server's: its prompt, its turn markers, its timeout
        # acknowledgements.
        game, _joined = self.running_game()
        previous = self.fake_process()
        order = []
        with game.condition:
            game.process = previous
            game.at_prompt = True
            game.output_thread = SimpleNamespace(
                join=lambda timeout=None: order.append("joined-old-pump"),
            )

        def launch(inner, save_path):
            order.append("launched")
            self.assertFalse(inner.at_prompt)
            with inner.condition:
                inner.process = self.fake_process()

        with patch.object(Game, "_launch_from_save", new=launch):
            self.assertTrue(game._v2_recovery_reload_server(
                game.episode / "saves" / "turn-0005-auto.sav.gz",
            ))

        self.assertEqual(order, ["joined-old-pump", "launched"])
        self.assertIsNone(game.output_thread)


if __name__ == "__main__":
    unittest.main()
