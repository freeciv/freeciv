"""Wedge detection and bounded rollback recovery for full-control-v2 seats."""

import json
import tempfile
import unittest
from types import SimpleNamespace
from http import HTTPStatus
from pathlib import Path
from unittest.mock import patch

from ..supervisor import APIProblem, Game
from ..v2_control import V2ControlError
from ..v2_recovery import (
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
            recovery_kind_for_attempt(budget.next_attempt(7)),
            "sidecar_reattach",
        )
        self.assertEqual(
            recovery_kind_for_attempt(budget.next_attempt(7)),
            "autosave_rollback",
        )

    def test_a_turn_cap_stops_a_third_attempt_on_that_turn(self):
        budget = RecoveryBudget(per_turn=2, per_game=99)
        budget.next_attempt(7)
        budget.next_attempt(7)
        self.assertIsNone(budget.next_attempt(7))
        # A later turn still gets its own allowance.
        self.assertEqual(budget.next_attempt(8), 1)

    def test_a_game_cap_stops_recovery_across_turns(self):
        budget = RecoveryBudget(per_turn=2, per_game=3)
        self.assertEqual(budget.next_attempt(1), 1)
        self.assertEqual(budget.next_attempt(1), 2)
        self.assertEqual(budget.next_attempt(2), 1)
        self.assertIsNone(budget.next_attempt(2))
        self.assertIn("budget", budget.exhausted_reason(2))

    def test_the_exhausted_reason_names_which_cap_was_hit(self):
        per_turn = RecoveryBudget(per_turn=1, per_game=99)
        per_turn.next_attempt(4)
        self.assertIn("turn 4", per_turn.exhausted_reason(4))


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
    from .test_supervisor import SupervisorTests

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
        from .test_v2_control import observation

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

    def project(self, *, happy, content, unhappy, angry, workers,
                specialists):
        import re

        from ..v2_control import V2SeatControl
        from .test_v2_control import complete_v2_rows, valid_rows

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
        control = V2SeatControl("game_x", "agent_x", 1)
        try:
            return control.state_page(self.compact(control, tuple(rows)), "cities")
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

    def test_a_mood_total_that_counts_specialists_twice_is_refused(self):
        # The shape the old rule demanded: mood total equal to size while a
        # specialist also exists.  Freeciv cannot emit it, so it stays a fault.
        with self.assertRaises(V2ControlError):
            self.project(
                happy=0, content=2, unhappy=0, angry=0,
                workers=1, specialists=1,
            )

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
        from .test_v2_control import complete_v2_rows, valid_rows

        # No action rows: a tile-targeted action carries its own separate
        # requirement on the tile catalog, which would confound these cases.
        return tuple(sorted(
            (*complete_v2_rows(valid_rows(actions=False)), self.WORKER_TASK),
        ))

    def test_a_worker_task_projects_when_no_tile_rows_are_exported(self):
        from ..v2_control import V2SeatControl

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
        from ..v2_control import V2SeatControl
        from .test_v2_control import observation

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
        from ..v2_control import V2SeatControl

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
        from ..v2_control import V2SeatControl

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
        from ..v2_control import V2SeatControl
        from .test_v2_control import observation

        control = V2SeatControl("game_x", "agent_x", 1)
        try:
            page = control.state_page(
                observation(self.rows_with_task()), "cities",
            )
        finally:
            control.close()
        self.assertTrue(page["page"]["items"])


if __name__ == "__main__":
    unittest.main()
