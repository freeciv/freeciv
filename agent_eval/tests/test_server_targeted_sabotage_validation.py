from __future__ import annotations

from pathlib import Path
import unittest


class ServerTargetedSabotageValidationTests(unittest.TestCase):
    def test_invalid_building_is_rejected_before_sabotage_side_effects(self) -> None:
        source = (
            Path(__file__).resolve().parents[2] / "server" / "diplomats.c"
        ).read_text(encoding="utf-8")
        function = source.split("bool diplomat_sabotage", 1)[1].split(
            "bool spy_steal_gold", 1
        )[0]
        before_infiltration, after_infiltration = function.split(
            "diplomat_infiltrate_tile", 1
        )

        self.assertIn("ACTION_SPY_TARGETED_SABOTAGE_CITY", before_infiltration)
        self.assertIn(
            "ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC", before_infiltration
        )
        self.assertIn(
            "struct impr_type *pimprove = improvement_by_number(improvement);",
            before_infiltration,
        )
        self.assertIn("if (pimprove == NULL)", before_infiltration)
        self.assertIn("if (!city_has_building(pcity, pimprove))", before_infiltration)
        self.assertIn("if (pimprove->sabotage <= 0)", before_infiltration)

        # An invalid or stale targeted choice must reach none of the existing
        # action costs or failure paths.
        validation = before_infiltration.split(
            "if (paction->id == ACTION_SPY_TARGETED_SABOTAGE_CITY", 1
        )[1]
        for mutation in (
            "diplomat_charge_movement",
            "action_failed_dice_roll",
            "wipe_unit",
            "economic.gold",
            "fc_rand(",
        ):
            self.assertNotIn(mutation, validation)
        self.assertGreaterEqual(validation.count("return FALSE;"), 3)

        # Random sabotage and explicit production sabotage retain their
        # existing target-selection branches after infiltration succeeds.
        self.assertIn(
            "action_has_result(paction, ACTRES_SPY_SABOTAGE_CITY)",
            after_infiltration,
        )
        self.assertIn("else if (improvement < 0)", after_infiltration)

    def test_dispatch_rejects_stale_target_before_action_started_signal(self) -> None:
        source = (
            Path(__file__).resolve().parents[2] / "server" / "unithand.c"
        ).read_text(encoding="utf-8")
        perform_action = source.split("bool unit_perform_action(", 1)[1]
        targeted = perform_action.split(
            "case ACTRES_SPY_TARGETED_SABOTAGE_CITY:", 1
        )[1].split("case ACTRES_SPY_SABOTAGE_CITY_PRODUCTION:", 1)[0]
        before_dispatch, dispatch = targeted.split(
            "ACTION_PERFORM_UNIT_CITY(action_type, actor_unit, pcity,", 1
        )

        self.assertIn(
            "action_type == ACTION_SPY_TARGETED_SABOTAGE_CITY",
            before_dispatch,
        )
        self.assertIn(
            "action_type == ACTION_SPY_TARGETED_SABOTAGE_CITY_ESC",
            before_dispatch,
        )
        self.assertIn("sub_tgt_impr == nullptr", before_dispatch)
        self.assertIn("pcity == nullptr", before_dispatch)
        self.assertIn(
            "!city_has_building(pcity, sub_tgt_impr)", before_dispatch
        )
        self.assertIn("sub_tgt_impr->sabotage <= 0", before_dispatch)
        self.assertIn("return FALSE;", before_dispatch)
        self.assertNotIn("illegal_action(", before_dispatch)
        self.assertIn("diplomat_sabotage", dispatch)

        random_sabotage = perform_action.split(
            "case ACTRES_SPY_SABOTAGE_CITY:", 1
        )[1].split("case ACTRES_SPY_TARGETED_SABOTAGE_CITY:", 1)[0]
        production_sabotage = perform_action.split(
            "case ACTRES_SPY_SABOTAGE_CITY_PRODUCTION:", 1
        )[1].split("case ACTRES_SPY_POISON:", 1)[0]
        self.assertIn("B_LAST, paction", random_sabotage)
        self.assertIn("-1, paction", production_sabotage)
        self.assertNotIn("city_has_building", random_sabotage)
        self.assertNotIn("city_has_building", production_sabotage)


if __name__ == "__main__":
    unittest.main()
