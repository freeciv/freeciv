from __future__ import annotations

from pathlib import Path
import unittest


class ServerBribeStackLifecycleTests(unittest.TestCase):
    def test_bribe_stack_reacquires_callback_sensitive_objects(self) -> None:
        source = (
            Path(__file__).resolve().parents[2] / "server" / "diplomats.c"
        ).read_text(encoding="utf-8")
        function = source.split("bool diplomat_bribe_stack", 1)[1].split(
            "bool spy_attack", 1
        )[0]
        self.assertIn("int bribed_ids[stack_size];", function)
        self.assertNotIn("unit_list_iterate_safe(pvictim->units", function)
        self.assertIn("game_unit_by_number(bribed_ids[stack_index])", function)
        self.assertIn("unit_tile(pbribed) != pvictim", function)
        self.assertIn(
            "player_city_by_number(pplayer, diplomat_homecity) != NULL",
            function,
        )
        self.assertGreaterEqual(
            function.count(
                "pdiplomat = player_unit_by_number(pplayer, diplomat_id);"
            ), 2,
        )
        tail = function.split(
            "/* The original pointer may have been invalidated", 1
        )[1]
        self.assertLess(
            tail.index("pcity = tile_city(pvictim);"),
            tail.index("action_auto_perf_unit_do("),
        )


if __name__ == "__main__":
    unittest.main()
