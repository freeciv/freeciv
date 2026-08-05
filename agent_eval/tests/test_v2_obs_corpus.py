"""The corpus half of the rig: real states from the machines' own game history.

A generator only produces the shapes someone thought to generate.  These tests
read what actually happened -- every ``.agent-eval/runs/*/saves/turn-*.sav.gz``
this machine kept -- and assert three things:

1.  the machine's own history *empirically refutes* the invariant that bricked
    turn 52: in real Freeciv, the citizen mood counters never account for the
    specialists;
2.  every harvested empire projects cleanly through ``V2SeatControl``, at the
    scale the games actually reached (13+ city empires, 20+ pop cities,
    isometric-hex maps, hundreds of turns);
3.  the two named incidents are reconstructible and land where the taxonomy
    says they should -- turn 52 as an observation-shape fault (OURS), turn 66
    as *not* one (C).

If no saves are present (a fresh clone), the corpus tests skip rather than
fail: the rig's synthetic half in ``test_v2_obs_properties`` is the part that
must run everywhere.
"""

from __future__ import annotations

import unittest

from agent_eval.tests import v2_obs_corpus as corpus_module
from agent_eval.tests import v2_obs_fixtures as fixtures
from agent_eval.tests.v2_obs_attribution import project

#: Saves per game.  Every game also always contributes its final turn.
SAMPLES_PER_GAME = 10

#: Pinned so that a silently shrinking gap list is a visible test failure.
EXPECTED_GAP_KEYS = frozenset({
    "city_tile.*",
    "city.citizen_happy|content|unhappy|angry",
    "city_worker_task.*",
    "action.*",
    "tile.known",
    "unit.activity|orders|transport",
    "research.choices_digest",
    "diplomacy.meeting|clauses",
    "game_mEUltpqtzauPGfjI9IlhWJ5x@turn-52",
    "game_XqynGMtFOtaqFbGXaF7lBx66@turn-66",
})


def load_corpus():
    samples = corpus_module.corpus(SAMPLES_PER_GAME)
    if not samples:
        raise unittest.SkipTest(
            "no autosaves under .agent-eval/runs; the corpus half of the rig "
            "needs real game history"
        )
    return samples


class CorpusShapeTests(unittest.TestCase):
    """The corpus has to be big and varied enough to be worth trusting."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.samples = load_corpus()

    def test_the_corpus_spans_many_games_and_deep_turns(self):
        games = {sample.game_id for sample in self.samples}
        turns = [sample.turn for sample in self.samples]
        self.assertGreaterEqual(len(games), 5, "too few distinct games")
        self.assertGreaterEqual(len(self.samples), 40)
        self.assertGreaterEqual(max(turns), 50, "no deep-turn state harvested")

    def test_the_corpus_reaches_the_scales_that_broke_games(self):
        empires = [
            player for sample in self.samples for player in sample.players
        ]
        self.assertTrue(
            any(len(player.cities) >= 13 for player in empires),
            "no 13+ city empire in the corpus",
        )
        self.assertTrue(
            any(
                city.size >= 5
                for player in empires for city in player.cities
            ),
            "no city larger than 4 in the corpus",
        )
        self.assertTrue(
            any(player.specialist_total > 0 for player in empires),
            "no real specialist state in the corpus -- the citizen-mood wedge "
            "would not be covered by real data",
        )
        self.assertTrue(
            any(
                sample.topology.startswith("isometric")
                for sample in self.samples
            ),
            "no isometric map in the corpus",
        )

    def test_incident_saves_are_present_and_readable(self):
        for incident in (
            corpus_module.ENTERTAINER_INCIDENT,
            corpus_module.SIDECAR_EXIT_INCIDENT,
        ):
            with self.subTest(incident=incident):
                sample = corpus_module.incident_sample(incident)
                self.assertIsNotNone(sample, f"{incident} save unreadable")
                self.assertEqual(sample.turn, incident[1])
                self.assertTrue(sample.players)

    def test_the_documented_gaps_are_exactly_the_pinned_set(self):
        """A gap that quietly disappears is a claim nobody checked."""
        keys = frozenset(corpus_module.CORPUS_GAPS)
        self.assertEqual(keys, EXPECTED_GAP_KEYS)
        for key, explanation in corpus_module.CORPUS_GAPS.items():
            with self.subTest(gap=key):
                self.assertGreater(len(explanation), 80, key)


class CitizenMoodInvariantTests(unittest.TestCase):
    """The turn-52 wedge, refuted from the machine's own history.

    Freeciv's ``[scoreN]`` block records ``happy``/``content``/``unhappy``/
    ``angry`` and the per-specialist-type counts for the whole empire, and each
    ``[playerN]`` ``c=`` table records every city's ``size`` and ``nspe*``.
    That is enough to test the exact identity the projector once got wrong,
    against every state these machines ever saved.
    """

    @classmethod
    def setUpClass(cls) -> None:
        cls.samples = load_corpus()
        cls.empires = [
            (sample, player)
            for sample in cls.samples for player in sample.players
            # A score block of all zeros is a player whose score was never
            # calculated (dead, barbarian, or pre-first-score); it carries no
            # mood information either way.
            if player.cities and sum(player.mood) > 0
        ]

    def test_the_history_contains_real_specialist_states(self):
        with_specialists = [
            (sample, player) for sample, player in self.empires
            if player.specialist_total > 0
        ]
        self.assertGreaterEqual(
            len(with_specialists), 20,
            "not enough real specialist states to refute anything",
        )

    def test_mood_counters_do_not_account_for_the_specialists(self):
        """The invariant that bricked turn 52, refuted by the real history.

        The old assertion was ``happy + content + unhappy + angry == size``.
        Across every saved empire that has at least one specialist, the
        *correct* identity (mood plus specialists) reconstructs the population
        an order of magnitude more often than the wrong one.  The wrong one
        still matches occasionally by coincidence -- ``[scoreN]`` is a snapshot
        that can lag the city table, so a shrinking empire can leave a stale
        mood total that happens to equal the new population -- which is exactly
        why the projector may not treat it as an invariant.
        """
        correct: list[str] = []
        wrong: list[str] = []
        for sample, player in self.empires:
            if player.specialist_total == 0:
                continue
            label = (
                f"{sample.describe()} p{player.index}: mood={player.mood} "
                f"specialists={player.specialist_total} "
                f"population={player.citizen_total}"
            )
            if sum(player.mood) + player.specialist_total == player.citizen_total:
                correct.append(label)
            if sum(player.mood) == player.citizen_total:
                wrong.append(label)
        self.assertGreaterEqual(
            len(correct), 20,
            "too few specialist-bearing empires to conclude anything",
        )
        self.assertLessEqual(
            len(wrong), len(correct) // 10,
            "the pre-fix invariant matched too often to be coincidence, which "
            "would mean the corpus is not measuring what it claims:\n"
            + "\n".join(wrong[:10]),
        )

    def test_mood_plus_specialists_reconstructs_the_population(self):
        """The identity Freeciv actually maintains.

        ``[scoreN]`` is a snapshot taken when the score was last calculated and
        can lag the city table inside the same file, so a handful of samples
        disagree; the identity is asserted as the overwhelming rule rather
        than as an absolute, and the specialist-bearing subset is checked
        separately and more strictly.
        """
        agree = 0
        total = 0
        specialist_agree = 0
        specialist_total = 0
        for _sample, player in self.empires:
            total += 1
            matched = (
                sum(player.mood) + player.specialist_total
                == player.citizen_total
            )
            agree += int(matched)
            if player.specialist_total > 0:
                specialist_total += 1
                specialist_agree += int(matched)
        self.assertGreater(total, 50)
        self.assertGreaterEqual(
            agree, int(total * 0.8),
            f"the mood+specialists identity held in only {agree}/{total} "
            "real empires",
        )
        self.assertGreater(specialist_total, 0)
        self.assertGreaterEqual(
            specialist_agree, int(specialist_total * 0.8),
            f"the identity held in only {specialist_agree}/{specialist_total} "
            "empires that actually have specialists",
        )

    def test_the_score_specialist_counts_match_the_city_tables(self):
        """Cross-check the two independent specialist sources in the save."""
        mismatches: list[str] = []
        for sample, player in self.empires:
            city_total = sum(city.specialist_total for city in player.cities)
            if city_total != player.specialist_total:
                mismatches.append(
                    f"{sample.describe()} p{player.index}: "
                    f"cities={city_total} score={player.specialist_total}"
                )
        self.assertLessEqual(
            len(mismatches), len(self.empires) // 5,
            "the score block and the city table disagree too often to treat "
            "either as ground truth:\n" + "\n".join(mismatches[:10]),
        )


class CorpusProjectionTests(unittest.TestCase):
    """Every real empire, converted to rows, must project."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.samples = load_corpus()

    def test_every_harvested_empire_projects(self):
        checked = 0
        for sample in self.samples:
            for index, player in enumerate(sample.players):
                if not player.cities:
                    continue
                checked += 1
                case = corpus_module.case_from_player(sample, index)
                rejection = project(fixtures.build_rows(case))
                if rejection is not None:
                    self.fail(
                        "a real saved empire does not project.\n"
                        f"  sample: {sample.describe()}\n"
                        f"  save: {sample.path}\n"
                        f"  case: {case.describe()}\n"
                        f"  rejection: {rejection.describe()}"
                    )
        self.assertGreater(checked, 40)

    def test_every_harvested_empire_projects_without_the_tile_catalog(self):
        """The compact-OBS shape: cities arrive, tiles do not."""
        for sample in self.samples[:40]:
            for index, player in enumerate(sample.players):
                if not player.cities:
                    continue
                case = corpus_module.case_from_player(
                    sample, index, tile_catalog=False, citizen_catalog=False,
                )
                with self.subTest(sample=sample.describe(), player=index):
                    self.assertIsNone(project(fixtures.build_rows(case)))

    def test_adding_one_entertainer_to_any_real_empire_still_projects(self):
        """The turn-52 transition, applied to every empire in the corpus."""
        checked = 0
        for sample in self.samples:
            for index, player in enumerate(sample.players):
                if not player.cities:
                    continue
                case = corpus_module.with_first_entertainer(
                    corpus_module.case_from_player(sample, index),
                )
                rejection = project(fixtures.build_rows(case))
                checked += 1
                if rejection is not None:
                    self.fail(
                        "one entertainer bricks a real empire.\n"
                        f"  sample: {sample.describe()}\n"
                        f"  rejection: {rejection.describe()}"
                    )
        self.assertGreater(checked, 40)


class IncidentReconstructionTests(unittest.TestCase):
    """The two named incidents, and what the corpus can honestly say.

    These run unconditionally: both saves are committed under
    ``agent_eval/tests/fixtures/incidents``.  They used to depend on
    ``.agent-eval/runs``, which is gitignored, so on any other machine, on CI,
    or after a prune, nine of these tests SKIPPED silently and the run still
    reported OK -- the evidence refuting the assertion that bricked turn 52
    evaporating into a green suite.  A missing fixture is now a failure.
    """

    def setUp(self) -> None:
        for incident in (
            corpus_module.ENTERTAINER_INCIDENT,
            corpus_module.SIDECAR_EXIT_INCIDENT,
        ):
            self.assertIsNotNone(
                corpus_module.incident_sample(incident),
                f"the committed incident fixture for {incident[0]} turn "
                f"{incident[1]} is missing; it is evidence, not a cache",
            )

    def test_the_turn_52_save_stops_one_step_short_of_the_incident(self):
        """Document the gap precisely instead of pretending it is not there."""
        sample = corpus_module.incident_sample(
            corpus_module.ENTERTAINER_INCIDENT,
        )
        self.assertIsNotNone(sample)
        seat = sample.players[0]
        self.assertEqual(len(seat.cities), 13)
        self.assertEqual(
            seat.specialist_total, 0,
            "the turn-52 autosave is expected to predate the first "
            "entertainer; if it now has one, update CORPUS_GAPS",
        )
        self.assertEqual(sum(seat.mood), seat.citizen_total)

    def test_the_reconstructed_turn_52_state_projects(self):
        """The state that bricked the game must now be ordinary."""
        case = corpus_module.entertainer_incident_case()
        self.assertIsNotNone(case)
        entertainers = sum(
            item.count
            for city in case.cities for item in city.specialists
            if item.is_default
        )
        self.assertEqual(entertainers, 1, "the transition was not applied")
        rejection = project(fixtures.build_rows(case))
        self.assertIsNone(
            rejection,
            "the turn-52 first-entertainer state is rejected again: "
            + (rejection.describe() if rejection else ""),
        )

    def test_the_turn_52_empire_projects_through_every_later_entertainer(self):
        """A wedge is defined by never recovering, so sweep the whole ladder."""
        case = corpus_module.entertainer_incident_case()
        self.assertIsNotNone(case)
        for step in range(6):
            with self.subTest(entertainers=step + 1):
                self.assertIsNone(project(fixtures.build_rows(case)))
            case = corpus_module.with_first_entertainer(case)

    def test_the_turn_66_state_projects_so_it_was_not_an_observation_fault(self):
        """Attribution for the C incident, stated as a negative.

        The sidecar client process exited during the T66->T67 turn change with
        a healthy server and a clean autosave.  A save cannot show a missing
        client, so the corpus establishes only what it can: the game state at
        turn 66 projects cleanly, so the incident was not the projector
        refusing a legal observation.  That keeps the turn-66 fault attributed
        to C rather than to OURS/FFI.
        """
        case = corpus_module.sidecar_exit_case()
        self.assertIsNotNone(case)
        rejection = project(fixtures.build_rows(case))
        self.assertIsNone(
            rejection,
            "the turn-66 state does not project, which would reattribute the "
            "incident away from C: "
            + (rejection.describe() if rejection else ""),
        )
        for compact in (True, False):
            with self.subTest(tile_catalog=compact):
                sample = corpus_module.incident_sample(
                    corpus_module.SIDECAR_EXIT_INCIDENT,
                )
                variant = corpus_module.case_from_player(
                    sample, 0, tile_catalog=compact, citizen_catalog=compact,
                )
                self.assertIsNone(project(fixtures.build_rows(variant)))


class CorpusReaderSafetyTests(unittest.TestCase):
    """The corpus reads run artifacts that a live game may also be using."""

    def test_reading_the_incident_saves_does_not_touch_them(self):
        for incident in (
            corpus_module.ENTERTAINER_INCIDENT,
            corpus_module.SIDECAR_EXIT_INCIDENT,
        ):
            paths = corpus_module.save_paths(
                corpus_module.INCIDENT_ROOT,
            ).get(incident[0], []) or corpus_module.save_paths().get(
                incident[0], [],
            )
            self.assertTrue(paths, f"{incident[0]} has no committed save")
            before = {path: path.stat() for path in paths}
            corpus_module.incident_sample(incident)
            for path, stat_before in before.items():
                with self.subTest(path=path.name):
                    stat_after = path.stat()
                    self.assertEqual(stat_before.st_mtime_ns,
                                     stat_after.st_mtime_ns)
                    self.assertEqual(stat_before.st_size, stat_after.st_size)

    def test_the_corpus_creates_no_cache_directories(self):
        """``save_replay``'s public entry points write a derived cache; the
        corpus deliberately uses only the low-level readers so that no run
        directory gains a file while a live game is using it."""
        # Deliberately a finished game: a live run writes saves of its own and
        # would make a directory-listing comparison flaky for reasons that
        # have nothing to do with this reader.
        game_id = corpus_module.ENTERTAINER_INCIDENT[0]
        paths = corpus_module.save_paths(
            corpus_module.INCIDENT_ROOT,
        ).get(game_id, []) or corpus_module.save_paths().get(game_id, [])
        self.assertTrue(paths, f"{game_id} has no committed save")
        run_directory = paths[0].parent.parent
        before = {item.name for item in run_directory.iterdir()}
        corpus_module.load_sample(paths[0])
        corpus_module.incident_sample(corpus_module.ENTERTAINER_INCIDENT)
        after = {item.name for item in run_directory.iterdir()}
        self.assertEqual(after, before)


if __name__ == "__main__":  # pragma: no cover
    unittest.main()
