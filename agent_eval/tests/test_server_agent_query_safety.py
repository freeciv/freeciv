from __future__ import annotations

from pathlib import Path
import re
import unittest


class ServerAgentQuerySafetyTests(unittest.TestCase):
    def test_agent_detail_queries_cannot_trigger_illegal_action_penalties(self) -> None:
        repository = Path(__file__).resolve().parents[2]
        packets = (
            repository / "common" / "networking" / "packets.h"
        ).read_text(encoding="utf-8")
        unithand = (repository / "server" / "unithand.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("#define AGENT_V2_ACTION_QUERY_KIND (253)", packets)
        self.assertIn("#define AGENT_V2_ACTION_REVALIDATE_KIND (254)", packets)

        policy = unithand.split(
            "static bool unit_action_query_should_punish", 1
        )[1].split("void handle_unit_action_query", 1)[0]
        self.assertIn("pc->client_gui != GUI_AGENT", policy)
        self.assertIn(
            "request_kind != AGENT_V2_ACTION_QUERY_KIND",
            policy,
        )

        query = unithand.split("void handle_unit_action_query", 1)[1].split(
            "void handle_unit_do_action", 1
        )[0]
        guarded_penalties = re.findall(
            r"if \(unit_action_query_should_punish\(pc, request_kind\)\) \{\s+"
            r"illegal_action\(",
            query,
        )
        self.assertEqual(len(guarded_penalties), 5)
        self.assertEqual(query.count("illegal_action("), 5)
        self.assertEqual(query.count("unit_query_impossible("), 8)


if __name__ == "__main__":
    unittest.main()
