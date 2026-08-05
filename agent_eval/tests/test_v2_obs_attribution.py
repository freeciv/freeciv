"""Wedge-impossibility: every rejection the projector can raise must say what
row family it is about.

``V2ControlError("internal_error")`` is deliberately detail-free on the wire,
and that is correct -- the public envelope must not leak native shape.  But an
operator whose seat has just bricked needs to know *which row family* the
projector refused, or the next wedge costs another live game to diagnose.

This suite asserts the meta-property that makes a wedge survivable:

1.  every row kind the OBS grammar accepts has a genuinely-invalid mutation in
    the catalog (no blind spots);
2.  every such mutation is *contained* -- a ``V2ControlError`` with code
    ``internal_error``, never an uncaught exception;
3.  every rejection is *attributable* -- the offending row family is
    recoverable from the failure, both from inside (traceback + source) and
    from outside (differential re-projection, which is what a supervisor can
    actually run against a wedged seat).

It also pins the one bundle shape that is currently *not* contained, so that
fixing it is a visible event rather than a silent one.
"""

from __future__ import annotations

from dataclasses import replace
import unittest

from agent_eval.tests import v2_obs_attribution as attribution
from agent_eval.tests import v2_obs_fixtures as fixtures
from agent_eval.tests import v2_obs_mutations as mutations
from agent_eval.v2_control import V2ControlError, V2SeatControl

#: Rejections raised by a guard that is about the *whole bundle* rather than
#: one row family name the projector's full row vocabulary.  That is the
#: code's own scope, not a failure of attribution, so precision is not
#: asserted for them.
BUNDLE_WIDE_MUTATIONS = frozenset({
    "second_spaceship_row",       # the "at most one of each singleton" guard
    "pregame_row_while_running",  # the preparing/running state partition
})

#: The maximum number of row kinds a single-family rejection may name before
#: the attribution stops being useful to an operator.
PRECISION_LIMIT = 6


class MutationCoverageTests(unittest.TestCase):
    def test_every_grammar_row_kind_has_an_invalid_mutation(self):
        """A blind spot in the catalog is a blind spot in the rig."""
        covered = frozenset(mutations.MUTATIONS_BY_KIND)
        missing = attribution.BUCKET_ROW_KINDS - covered
        extra = covered - attribution.BUCKET_ROW_KINDS
        self.assertEqual(
            missing, frozenset(),
            "these OBS row kinds have no invalid-mutation coverage: "
            f"{sorted(missing)}",
        )
        self.assertEqual(
            extra, frozenset(),
            f"these mutations name a row kind the grammar does not bucket: "
            f"{sorted(extra)}",
        )

    def test_the_bucket_vocabulary_is_read_from_the_parser(self):
        """The coverage set must track ``_parse_rows``, not a hand-written list."""
        self.assertIn("city_worker_task", attribution.BUCKET_ROW_KINDS)
        self.assertIn("city_specialist", attribution.BUCKET_ROW_KINDS)
        self.assertGreaterEqual(len(attribution.BUCKET_ROW_KINDS), 30)

    def test_every_mutation_explains_the_C_fact_it_breaks(self):
        for mutation in mutations.MUTATIONS:
            with self.subTest(name=mutation.name):
                self.assertTrue(mutation.why.strip())
                self.assertGreater(len(mutation.why), 30)


class RejectionContainmentTests(unittest.TestCase):
    """A refused observation must fail closed *and* stay recoverable."""

    def setUp(self) -> None:
        self.base = mutations.base_rows()
        self.assertIsNone(attribution.project(self.base))

    def test_every_mutation_is_contained_as_a_typed_error(self):
        for mutation in mutations.MUTATIONS:
            with self.subTest(kind=mutation.row_kind, name=mutation.name):
                rows = mutation.apply(self.base)
                control = V2SeatControl("game_containment_probe", "agent", 1)
                with self.assertRaises(V2ControlError) as caught:
                    control.state_page({
                        "generation": 1, "native_revision": 3, "rows": rows,
                    })
                self.assertEqual(caught.exception.code, "internal_error")
                self.assertEqual(caught.exception.details, {})

    def test_every_rejection_is_attributable(self):
        """No rejection may be a bare, anonymous internal_error."""
        unattributed = []
        for mutation in mutations.MUTATIONS:
            rejection = attribution.project(mutation.apply(self.base))
            self.assertIsNotNone(rejection, mutation.name)
            if not rejection.attributed:
                unattributed.append((mutation.name, rejection.describe()))
        self.assertEqual(
            unattributed, [],
            "these rejections name no row kind at all, so a future wrong "
            "invariant here would brick a seat without identifying itself:\n"
            + "\n".join(f"  {name}: {detail}" for name, detail in unattributed),
        )

    def test_attribution_names_the_row_kind_that_was_broken(self):
        for mutation in mutations.MUTATIONS:
            with self.subTest(kind=mutation.row_kind, name=mutation.name):
                rows = mutation.apply(self.base)
                rejection = attribution.project(rows)
                self.assertIsNotNone(rejection)
                differential = attribution.differential_row_kinds(
                    self.base, rows,
                )
                named = rejection.row_kinds | differential
                self.assertIn(
                    mutation.row_kind, named,
                    f"neither the traceback nor a differential re-projection "
                    f"pointed at the {mutation.row_kind!r} row.\n"
                    f"  static: {sorted(rejection.row_kinds)}\n"
                    f"  differential: {sorted(differential)}\n"
                    f"  {rejection.describe()}",
                )

    def test_differential_attribution_is_exact(self):
        """Reverting the offending family always clears the rejection.

        This is the probe a supervisor can run on a wedged seat without any
        knowledge of the projector's internals, so it must be exact rather
        than merely suggestive.
        """
        for mutation in mutations.MUTATIONS:
            with self.subTest(kind=mutation.row_kind, name=mutation.name):
                differential = attribution.differential_row_kinds(
                    self.base, mutation.apply(self.base),
                )
                self.assertIn(mutation.row_kind, differential)

    def test_attribution_stays_precise_enough_to_act_on(self):
        for mutation in mutations.MUTATIONS:
            if mutation.name in BUNDLE_WIDE_MUTATIONS:
                continue
            with self.subTest(kind=mutation.row_kind, name=mutation.name):
                rejection = attribution.project(mutation.apply(self.base))
                self.assertLessEqual(
                    len(rejection.row_kinds), PRECISION_LIMIT,
                    f"attribution named {len(rejection.row_kinds)} row kinds; "
                    "an operator cannot act on that.\n"
                    f"  {rejection.describe()}",
                )

    def test_bundle_wide_guards_still_name_their_row_kind(self):
        for name in BUNDLE_WIDE_MUTATIONS:
            mutation = next(
                item for item in mutations.MUTATIONS if item.name == name
            )
            with self.subTest(name=name):
                rejection = attribution.project(mutation.apply(self.base))
                self.assertIn(mutation.row_kind, rejection.row_kinds)


class AttributionMechanismTests(unittest.TestCase):
    """The attributor is a measuring instrument; check it against knowns."""

    def test_a_parse_level_rejection_reports_the_row_kind_it_was_parsing(self):
        rows = mutations.edit(mutations.base_rows(), "meta ", "phase", 9)
        rejection = attribution.project(rows)
        self.assertEqual(rejection.function, "_parse_row")
        self.assertIn("meta", rejection.row_kinds)

    def test_a_rejection_inside_a_shared_helper_is_attributed_to_its_caller(self):
        """``unique()`` and ``_entity_ref()`` are shared by every row family."""
        rows = mutations.duplicate_with(
            mutations.base_rows(),
            f"tile index={fixtures.city_tile_base(0)} ",
            terrain="Plains",
        )
        rejection = attribution.project(rows)
        self.assertIn("tile", rejection.row_kinds)

    def test_the_attributor_reports_nothing_when_nothing_was_rejected(self):
        self.assertIsNone(attribution.project(mutations.base_rows()))

    def test_differential_attribution_needs_no_source_knowledge(self):
        rows = mutations.edit(
            mutations.base_rows(), "city_worker_task city=c:20:200 ",
            "target_extra", -1,
        )
        self.assertEqual(
            attribution.differential_row_kinds(mutations.base_rows(), rows),
            frozenset({"city_worker_task"}),
        )


class ContainedFailureTests(unittest.TestCase):
    """Shapes that used to escape the envelope, kept fixed."""

    def test_a_city_without_its_rally_row_is_a_typed_rejection(self):
        """Promoted from ``UncontainedFailureTests`` once it was contained.

        ``protocol_v2.c v2_build_city_state_rows`` emits one ``city_rally`` row
        next to every ``city`` row, back to back in the same encode branch, so
        the rally row travels with the city row itself.  The cardinality check
        used to be guarded on the ``city_rally`` bucket, which let this bundle
        past ``_validate_cross_links``; ``_project`` then indexed
        ``native_city_rallies[ref]`` unguarded and raised a bare ``KeyError``
        straight out of ``state_page`` -- no envelope, no code, nothing naming
        the row family, strictly worse than ``internal_error``.

        Two independent fixes hold it now: the cardinality check is
        unconditional, and ``_snapshot`` converts any non-``_ObservationError``
        into a typed ``internal_error`` rather than letting it escape.
        """
        case = replace(mutations.base_case(), rally_rows=False)
        rows = fixtures.build_rows(case)
        control = V2SeatControl("game_rallyless_probe", "agent", 1)
        observation = {
            "generation": 1, "native_revision": 3, "rows": rows,
        }
        with self.assertRaises(V2ControlError) as caught:
            control.state_page(observation)
        self.assertEqual(caught.exception.code, "internal_error")
        self.assertEqual(caught.exception.details, {})

    def test_the_rally_less_bundle_names_its_row_kind(self):
        """Containment is not enough: the fault has to be attributable."""
        case = replace(mutations.base_case(), rally_rows=False)
        rejection = attribution.project(fixtures.build_rows(case))
        self.assertIsNotNone(rejection)
        self.assertIn("city_rally", rejection.row_kinds)

    def test_no_shape_the_rig_can_reach_escapes_the_envelope(self):
        """Everything else the generator can build is contained or accepted."""
        uncontained: list[str] = []
        for seed in range(120):
            case = fixtures.case_for_seed(seed)
            control = V2SeatControl("game_scan_probe", "agent", 1)
            try:
                control.state_page({
                    "generation": 1, "native_revision": 3,
                    "rows": fixtures.build_rows(case),
                })
            except V2ControlError:
                pass
            except Exception as error:  # noqa: BLE001 - that is the finding
                uncontained.append(f"seed={seed}: {error!r}")
        self.assertEqual(uncontained, [])


if __name__ == "__main__":  # pragma: no cover
    unittest.main()
