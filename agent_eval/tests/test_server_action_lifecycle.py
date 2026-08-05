from __future__ import annotations

from pathlib import Path
import unittest


class ServerActionLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        repository = Path(__file__).resolve().parents[2]
        cls.unithand = (repository / "server" / "unithand.c").read_text(
            encoding="utf-8"
        )
        cls.ruleload = (
            repository / "server" / "ruleset" / "ruleload.c"
        ).read_text(encoding="utf-8")

    def test_illegal_action_never_uses_actor_after_fatal_penalty(self) -> None:
        function = self.unithand.split(
            "static void illegal_action(struct player *pplayer,", 2
        )[2].split("static void unit_query_impossible", 1)[0]
        before_penalty, after_penalty = function.split(
            "was_punished = illegal_action_pay_price", 1
        )

        self.assertIn("struct tile *actor_tile = unit_tile(actor);", before_penalty)
        self.assertIn("illegal_action_msg(pplayer", before_penalty)
        self.assertNotIn("unit_tile(actor)", after_penalty)
        self.assertIn("notify_player(pplayer, actor_tile", after_penalty)

    def test_action_macros_mark_gui_requests_as_foreground(self) -> None:
        macros = self.unithand.split("#define ACTION_PERFORM_UNIT_CITY", 1)[1].split(
            "#define ACTION_PERFORM_UNIT_ANY", 1
        )[0]

        self.assertEqual(macros.count("REQEST_PLAYER_INITIATED, requester"), 6)
        self.assertNotIn("TRUE, requester", macros)

    def test_all_four_compatibility_bombards_are_non_lethal(self) -> None:
        block = self.ruleload.split("/* Non Lethal bombard */", 1)[1].split(
            "Forced actions after another action", 1
        )[0]

        for action in (
            "ACTION_BOMBARD",
            "ACTION_BOMBARD2",
            "ACTION_BOMBARD3",
            "ACTION_BOMBARD4",
        ):
            with self.subTest(action=action):
                self.assertIn(
                    f"action_by_number({action})->sub_results,\n"
                    "             ACT_SUB_RES_NON_LETHAL",
                    block,
                )


if __name__ == "__main__":
    unittest.main()
