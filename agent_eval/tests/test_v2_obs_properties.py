"""Property sweeps over the observation shapes that have bricked live games.

The rig has two halves and this module runs both:

*   every row set that is valid per the C emitter's semantics must be
    **accepted** -- a rejection here is a fail-closed-forever wedge in waiting;
*   every genuinely-invalid row set must be **rejected** -- fail-closed is the
    correct behaviour when the contract really is broken.

The axes are the ones that actually killed games: specialists from zero to the
whole city, worker tasks with and without the tile and citizen catalogs,
consuming actions, multi-city empires, and the zero-worker edge.  Cases come
from a seeded generator, and every failure message prints the seed that
reproduces it via ``v2_obs_fixtures.case_for_seed``.
"""

from __future__ import annotations

import itertools
import unittest

from agent_eval.tests import v2_obs_fixtures as fixtures
from agent_eval.tests import v2_obs_mutations as mutations
from agent_eval.tests.v2_obs_attribution import project
from agent_eval.v2_control import V2ControlError, V2SeatControl

#: Kept small enough to stay well inside a second, large enough that every
#: axis combination is hit many times over.
GENERATED_SEEDS = range(300)


class ObservationAcceptanceTests(unittest.TestCase):
    """Valid-per-C row sets the projector must never refuse."""

    def assertAccepted(
        self, case: fixtures.ObservationCase, *, note: str = "",
    ) -> None:
        rows = fixtures.build_rows(case)
        rejection = project(rows)
        if rejection is not None:
            self.fail(
                "the projector refused a bundle that is valid per the C "
                f"emitter's semantics.\n  case: {case.describe()}\n"
                f"  reproduce: v2_obs_fixtures.case_for_seed({case.seed})\n"
                f"  rejection: {rejection.describe()}\n"
                f"  {note}"
            )

    def test_specialists_from_zero_to_the_whole_city_are_accepted(self):
        """The citizen-mood wedge, swept.

        Freeciv's mood counters describe only the non-specialist citizens, so a
        city that is *entirely* specialists reports all four mood counters as
        zero.  The old invariant asserted the counters sum to ``size`` and so
        rejected every observation from the first entertainer onward.
        """
        for size in range(1, 9):
            for population_specialists in range(0, size + 1):
                with self.subTest(size=size, specialists=population_specialists):
                    city = fixtures.CitySpec(
                        ordinal=0,
                        worked_tiles=size - population_specialists,
                        specialists=fixtures.specialists(
                            entertainers=population_specialists,
                        ),
                    )
                    self.assertAccepted(fixtures.ObservationCase(
                        label=f"specialists-{population_specialists}-of-{size}",
                        cities=(city,),
                    ))

    def test_mood_splits_over_the_non_specialist_citizens_are_accepted(self):
        """Any distribution of the workers across happy/content/unhappy/angry."""
        workers = 4
        for happy in range(workers + 1):
            for content in range(workers - happy + 1):
                for unhappy in range(workers - happy - content + 1):
                    angry = workers - happy - content - unhappy
                    mood = (happy, content, unhappy, angry)
                    with self.subTest(mood=mood):
                        self.assertAccepted(fixtures.ObservationCase(
                            label=f"mood-{mood}",
                            cities=(fixtures.CitySpec(
                                ordinal=0,
                                worked_tiles=workers,
                                specialists=fixtures.specialists(entertainers=2),
                                mood=mood,
                            ),),
                        ))

    def test_superspecialists_stay_outside_size(self):
        """``city_specialists()`` sums only normal specialist types."""
        for supers in range(0, 4):
            with self.subTest(superspecialists=supers):
                self.assertAccepted(fixtures.ObservationCase(
                    label=f"superspecialists-{supers}",
                    cities=(fixtures.CitySpec(
                        ordinal=0,
                        worked_tiles=2,
                        specialists=fixtures.specialists(
                            entertainers=1, superspecialists=supers,
                        ),
                    ),),
                ))

    def test_worker_tasks_survive_every_catalog_combination(self):
        """The worker-task wedge, swept.

        ``city_worker_task`` rows ride the cities catalog into a compact
        observation; ``tile`` rows only ever arrive through STATE_SCOPE, and
        ``city_tile`` rows only through the city_citizens scope.  All four
        combinations are states the C side really produces.
        """
        tasks = (
            fixtures.WorkerTaskSpec(tile_offset=1),
            fixtures.WorkerTaskSpec(
                tile_offset=2, activity="cultivate",
                target_extra=-1, target_extra_name="none",
            ),
        )
        for tile_catalog, citizen_catalog in itertools.product(
            (True, False), repeat=2,
        ):
            with self.subTest(tiles=tile_catalog, citizens=citizen_catalog):
                self.assertAccepted(fixtures.ObservationCase(
                    label="worker-tasks",
                    cities=(fixtures.CitySpec(
                        ordinal=0, worked_tiles=3, worker_tasks=tasks,
                    ),),
                    tile_catalog=tile_catalog,
                    citizen_catalog=citizen_catalog,
                ), note=(
                    "a persisted worker task must not reject observations for "
                    "the rest of the game"
                ))

    def test_a_worker_task_on_an_unworked_tile_reaches_a_compact_observation(self):
        """The precise live shape: a task whose tile is in no catalog at all."""
        self.assertAccepted(fixtures.ObservationCase(
            label="worker-task-outside-any-catalog",
            cities=(fixtures.CitySpec(
                ordinal=0,
                worked_tiles=1,
                worker_tasks=(fixtures.WorkerTaskSpec(tile_offset=17),),
            ),),
            tile_catalog=False,
            citizen_catalog=False,
        ))

    def test_child_catalogs_are_independently_present_or_absent(self):
        """Each city child family is its own STATE_SCOPE section."""
        city = fixtures.CitySpec(
            ordinal=0, worked_tiles=2, worklist_length=1, build_choices=3,
            improvements=2,
        )
        for citizens, worklist, choices, improvements in itertools.product(
            (True, False), repeat=4,
        ):
            with self.subTest(
                citizens=citizens, worklist=worklist, choices=choices,
                improvements=improvements,
            ):
                self.assertAccepted(fixtures.ObservationCase(
                    label="catalog-matrix",
                    cities=(city,),
                    citizen_catalog=citizens,
                    worklist_catalog=worklist,
                    build_choice_catalog=choices,
                    improvement_catalog=improvements,
                ))

    def test_multi_city_empires_are_accepted(self):
        for count in (1, 2, 5, 13, 24):
            with self.subTest(cities=count):
                self.assertAccepted(fixtures.ObservationCase(
                    label=f"empire-{count}",
                    cities=tuple(
                        fixtures.CitySpec(
                            ordinal=ordinal,
                            worked_tiles=1 + ordinal % 5,
                            specialists=fixtures.specialists(
                                entertainers=ordinal % 3,
                            ),
                        )
                        for ordinal in range(count)
                    ),
                ))

    def test_consuming_actions_are_accepted(self):
        for count in range(len(fixtures.CONSUMING_RULES) + 1):
            with self.subTest(consuming=count):
                self.assertAccepted(fixtures.ObservationCase(
                    label=f"consuming-{count}",
                    cities=(fixtures.CitySpec(ordinal=0, worked_tiles=2),),
                    consuming_actions=count,
                ))

    def test_every_map_topology_is_accepted(self):
        for topology in ("square", "isometric_square", "hex", "isometric_hex"):
            with self.subTest(topology=topology):
                self.assertAccepted(fixtures.ObservationCase(
                    label=f"topology-{topology}",
                    cities=(fixtures.CitySpec(ordinal=0, worked_tiles=2),),
                    topology=topology,
                ))

    def test_an_empire_with_no_cities_is_accepted(self):
        self.assertAccepted(fixtures.ObservationCase(
            label="no-cities", cities=(), own_units=1,
        ))

    def test_trade_routes_and_tombstones_are_accepted(self):
        for routes, tombstones in itertools.product((0, 1, 3), (0, 1, 2)):
            with self.subTest(routes=routes, tombstones=tombstones):
                self.assertAccepted(fixtures.ObservationCase(
                    label=f"routes-{routes}-tombstones-{tombstones}",
                    cities=(fixtures.CitySpec(
                        ordinal=0, worked_tiles=2, trade_route_count=routes,
                    ),),
                    tombstones=tombstones,
                ))

    def test_generated_cases_are_all_accepted(self):
        """The seeded sweep: every generated case is legal, so all must pass."""
        for seed in GENERATED_SEEDS:
            case = fixtures.case_for_seed(seed)
            rows = fixtures.build_rows(case)
            rejection = project(rows)
            if rejection is not None:
                self.fail(
                    "generated observation rejected.\n"
                    f"  reproduce: v2_obs_fixtures.case_for_seed({seed})\n"
                    f"  case: {case.describe()}\n"
                    f"  rejection: {rejection.describe()}"
                )


class GeneratorContractTests(unittest.TestCase):
    """The generator itself has to be trustworthy before its sweep means much."""

    def test_a_seed_reproduces_its_case_exactly(self):
        for seed in (0, 7, 41, 199, 299):
            with self.subTest(seed=seed):
                first = fixtures.case_for_seed(seed)
                second = fixtures.case_for_seed(seed)
                self.assertEqual(first, second)
                self.assertEqual(
                    fixtures.build_rows(first), fixtures.build_rows(second),
                )
                self.assertEqual(first.seed, seed)

    def test_different_seeds_explore_different_shapes(self):
        shapes = {
            fixtures.case_for_seed(seed).describe()
            for seed in GENERATED_SEEDS
        }
        self.assertGreater(len(shapes), len(GENERATED_SEEDS) // 2)

    def test_the_sweep_actually_reaches_the_historic_wedge_shapes(self):
        """A sweep that never generates the dangerous shape proves nothing."""
        cases = [fixtures.case_for_seed(seed) for seed in GENERATED_SEEDS]
        all_specialist_cities = [
            city for case in cases for city in case.cities
            if city.worked_tiles == 0
        ]
        tasks_without_tiles = [
            case for case in cases
            if not case.tile_catalog
            and any(city.worker_tasks for city in case.cities)
        ]
        superspecialists = [
            city for case in cases for city in case.cities
            if any(not item.counts_toward_population for item in city.specialists)
        ]
        big_empires = [case for case in cases if len(case.cities) >= 13]
        self.assertTrue(all_specialist_cities, "no zero-worker city generated")
        self.assertTrue(
            tasks_without_tiles, "no worker task without a tile catalog",
        )
        self.assertTrue(superspecialists, "no superspecialist generated")
        self.assertTrue(big_empires, "no 13+ city empire generated")

    def test_row_builder_is_pinned_to_the_native_schema(self):
        """A fixture cannot be written against a stale field list."""
        with self.assertRaises(AssertionError):
            fixtures.row("tombstone", {"ref": "u:1:1"})
        with self.assertRaises(AssertionError):
            fixtures.row("tombstone", {
                "ref": "u:1:1", "kind": "unit", "surplus": 1,
            })
        rendered = fixtures.row("tombstone", {"ref": "u:1:1", "kind": "unit"})
        self.assertEqual(rendered, "tombstone ref=u:1:1 kind=unit")


class ObservationRejectionTests(unittest.TestCase):
    """Genuinely-invalid row sets: fail-closed is the right answer here."""

    def test_the_mutation_base_bundle_is_accepted(self):
        self.assertIsNone(project(mutations.base_rows()))

    def test_every_invalid_mutation_is_rejected(self):
        base = mutations.base_rows()
        for mutation in mutations.MUTATIONS:
            with self.subTest(kind=mutation.row_kind, name=mutation.name):
                rejection = project(mutation.apply(base))
                self.assertIsNotNone(
                    rejection,
                    f"the projector accepted an impossible bundle: "
                    f"{mutation.why}",
                )
                self.assertEqual(rejection.code, "internal_error")

    def test_a_size_zero_city_is_rejected(self):
        rows = mutations.edit(
            mutations.base_rows(), "city ref=c:21:201 ", "size", 0,
        )
        self.assertIsNotNone(project(rows))

    def test_mood_counters_that_include_specialists_are_recorded_not_rejected(self):
        """The mirror image of the wedge -- and deliberately not fatal.

        Freeciv's emitter reports ``workers = size - specialists``, so mood
        counters that also account for the specialists are not what a coherent
        client emits.  But ``client/packhand.c handle_city_info()`` produces
        exactly this shape on purpose: when the reassembled citizen total
        disagrees with ``packet->size`` it logs and OVERRIDES with
        ``city_size_set(pcity, packet->size)``, leaving ``feel[]`` and
        ``specialists[]`` at the packet values.  The native client keeps
        playing; a projector that rejected here would refuse every subsequent
        observation forever, because the city row is in every bundle.
        """
        city = fixtures.CitySpec(
            ordinal=0, worked_tiles=2,
            specialists=fixtures.specialists(entertainers=2),
        )
        case = fixtures.ObservationCase(label="mood-includes-specialists",
                                        cities=(city,))
        rows = fixtures.build_rows(case)
        self.assertIsNone(project(rows))
        # size is 4, workers 2: a row claiming four content citizens is the
        # post-self-heal client's own accounting.
        broken = mutations.edit(
            rows, "city ref=c:20:200 ", "citizen_content", 4,
        )
        self.assertIsNone(project(broken))

        # Contained, named, and the seat survives it.
        control = V2SeatControl("game_mood_anomaly", "agent", 1)
        try:
            control.state_page({
                "generation": 1, "native_revision": 3, "rows": broken,
            })
            self.assertEqual(
                control.native_anomalies.get("city_citizen_counts"), 1,
            )
            control.state_page({
                "generation": 1, "native_revision": 4, "rows": broken,
            })
        finally:
            control.close()

    def test_rejections_never_mutate_the_seat(self):
        """A refused observation must leave the seat usable, not bricked."""
        control = V2SeatControl("game_reject_probe", "agent", 1)
        good = fixtures.build_rows(mutations.base_case())
        bad = mutations.MUTATIONS[0].apply(good)
        with self.assertRaises(V2ControlError) as caught:
            control.state_page({
                "generation": 1, "native_revision": 5, "rows": bad,
            })
        self.assertEqual(caught.exception.code, "internal_error")
        # The same seat still takes the next, valid observation.
        page = control.state_page({
            "generation": 1, "native_revision": 6, "rows": good,
        })
        self.assertIn("state_revision", page)


if __name__ == "__main__":  # pragma: no cover
    unittest.main()
