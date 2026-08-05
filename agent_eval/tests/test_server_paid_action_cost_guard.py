from __future__ import annotations

from pathlib import Path
import unittest


class ServerPaidActionCostGuardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        repository = Path(__file__).resolve().parents[2]
        cls.unithand = (repository / "server" / "unithand.c").read_text(
            encoding="utf-8"
        )
        cls.diplomats = (repository / "server" / "diplomats.c").read_text(
            encoding="utf-8"
        )

    def test_reserved_ceiling_is_canonical_and_rejected_before_lua(self) -> None:
        parser = self.unithand.split(
            "static bool agent_v2_parse_max_cost", 1
        )[1].split("static bool do_attack", 1)[0]

        self.assertIn('#define AGENT_V2_MAX_COST_PREFIX "agent-v2-max-cost:"', self.unithand)
        self.assertIn("digits[0] == '0' && digits[1] != '\\0'", parser)
        self.assertIn("*digits < '0' || *digits > '9'", parser)
        self.assertIn("value > (INT_MAX - digit) / 10", parser)

        action = self.unithand.split("bool unit_perform_action", 1)[1]
        parse_at = action.index("agent_v2_parse_max_cost")
        first_lua_at = action.index('script_server_signal_emit("action_started_unit_city"')
        self.assertLess(parse_at, first_lua_at)
        self.assertIn("paction->result == ACTRES_SPY_BRIBE_UNIT", action[:first_lua_at])
        self.assertIn("paction->result == ACTRES_SPY_BRIBE_STACK", action[:first_lua_at])
        self.assertIn("paction->result == ACTRES_SPY_INCITE_CITY", action[:first_lua_at])
        self.assertIn("max_cost >= INCITE_IMPOSSIBLE_COST", action[:first_lua_at])

    def test_post_lua_reprice_guard_precedes_every_paid_side_effect(self) -> None:
        bribe = self.diplomats.split("bool diplomat_bribe_unit", 1)[1].split(
            "bool diplomat_bribe_stack", 1
        )[0]
        bribe_cost = bribe.index("bribe_cost = unit_bribe_cost")
        bribe_guard = bribe.index("max_cost_guarded && bribe_cost > max_cost")
        self.assertLess(bribe_cost, bribe_guard)
        for side_effect in (
            "pplayer->economic.gold < bribe_cost",
            "diplomat_infiltrate_tile",
            "unit_change_owner",
            "pplayer->economic.gold -= bribe_cost",
        ):
            self.assertLess(bribe_guard, bribe.index(side_effect))

        stack = self.diplomats.split("bool diplomat_bribe_stack", 1)[1].split(
            "bool spy_attack", 1
        )[0]
        stack_guard = stack.index("max_cost_guarded && bribe_cost > max_cost")
        self.assertLess(stack.index("if (bribe_cost < 0)"), stack_guard)
        for side_effect in (
            "pplayer->economic.gold < bribe_cost",
            "nunit = unit_change_owner",
            "pplayer->economic.gold -= bribe_cost",
        ):
            self.assertLess(stack_guard, stack.index(side_effect))

        incite = self.diplomats.split("bool diplomat_incite", 1)[1].split(
            "bool diplomat_sabotage", 1
        )[0]
        incite_cost = incite.index("revolt_cost = city_incite_cost")
        impossible = incite.index("revolt_cost == INCITE_IMPOSSIBLE_COST")
        incite_guard = incite.index("max_cost_guarded && revolt_cost > max_cost")
        self.assertLess(incite_cost, impossible)
        self.assertLess(impossible, incite_guard)
        for side_effect in (
            "pplayer->economic.gold < revolt_cost",
            "diplomat_infiltrate_tile",
            "action_failed_dice_roll",
            "city_reduce_size",
            "pplayer->economic.gold -= revolt_cost",
        ):
            self.assertLess(incite_guard, incite.index(side_effect))


if __name__ == "__main__":
    unittest.main()
