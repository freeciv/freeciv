from __future__ import annotations

from pathlib import Path
import unittest


class ServerDiplomatLifecycleTests(unittest.TestCase):
    def test_spy_nuke_does_not_reuse_actor_pointer_after_escape(self) -> None:
        source = (
            Path(__file__).resolve().parents[2] / "server" / "diplomats.c"
        ).read_text(encoding="utf-8")
        function = source.split("bool spy_nuke_city", 1)[1].split(
            "static void diplomat_charge_movement", 1
        )[0]
        before_escape, after_escape = function.split(
            "diplomat_escape_full(act_player, act_unit, TRUE,\n"
            "                       tgt_tile, tgt_city_link, paction);",
            1,
        )

        self.assertIn(
            "actor_consumed = utype_is_consumed_by_action(paction, act_utype);",
            before_escape,
        )
        self.assertIn("actor_id = act_unit->id;", before_escape)
        self.assertNotIn("act_unit", after_escape)
        self.assertIn(
            "player_unit_by_number(act_player, actor_id)", after_escape
        )


if __name__ == "__main__":
    unittest.main()
