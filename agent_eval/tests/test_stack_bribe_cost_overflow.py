from __future__ import annotations

from pathlib import Path
import unittest


class StackBribeCostOverflowTests(unittest.TestCase):
    def test_overflow_is_unpayable_before_mutation(self) -> None:
        repository = Path(__file__).resolve().parents[2]
        unit = (repository / "common" / "unit.c").read_text(encoding="utf-8")
        unithand = (repository / "server" / "unithand.c").read_text(
            encoding="utf-8"
        )
        diplomats = (repository / "server" / "diplomats.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("bribe_cost > INT_MAX - unit_cost", unit)
        query = unithand.split("void handle_unit_action_query", 1)[1].split(
            "case ACTRES_SPY_BRIBE_STACK:", 1
        )[1].split("case ACTRES_SPY_INCITE_CITY:", 1)[0]
        self.assertIn("if (cost >= 0)", query)
        self.assertIn("unit_query_impossible", query)
        execute = diplomats.split("bool diplomat_bribe_stack", 1)[1].split(
            "bool spy_attack", 1
        )[0]
        overflow = execute.index("if (bribe_cost < 0)")
        for mutation in (
            "max_cost_guarded && bribe_cost > max_cost",
            "nunit = unit_change_owner",
            "pplayer->economic.gold -= bribe_cost",
        ):
            self.assertLess(overflow, execute.index(mutation))


if __name__ == "__main__":
    unittest.main()
