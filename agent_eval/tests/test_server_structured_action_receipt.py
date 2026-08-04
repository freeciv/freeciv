from __future__ import annotations

from pathlib import Path
import unittest


class StructuredActionReceiptTests(unittest.TestCase):
    def setUp(self) -> None:
        self.repository = Path(__file__).resolve().parents[2]

    def test_server_emits_exact_result_only_to_controlling_gui_agents(
        self,
    ) -> None:
        packets = (
            self.repository / "common" / "networking" / "packets.h"
        ).read_text(encoding="utf-8")
        packet_schema = (
            self.repository / "common" / "networking" / "packets.def"
        ).read_text(encoding="utf-8")
        server = (self.repository / "server" / "unithand.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("#define AGENT_V2_ACTION_RECEIPT_KIND (252)", packets)
        answer = packet_schema.split("PACKET_UNIT_ACTION_ANSWER = 85", 1)[
            1
        ].split("\nend", 1)[0]
        for field in (
            "UNIT actor_id;",
            "ACTION_TGT target_id;",
            "GOLD cost;",
            "ACTION_ID action_type;",
            "UINT8 request_kind;",
        ):
            self.assertIn(field, answer)

        sender = server.split(
            "static void agent_v2_send_action_receipt", 1
        )[1].split("static bool agent_v2_parse_max_cost", 1)[0]
        self.assertIn("conn_list_iterate(pplayer->connections, pconn)", sender)
        self.assertIn("pconn->client_gui == GUI_AGENT", sender)
        self.assertIn("conn_controls_player(pconn)", sender)
        self.assertIn("performed ? 1 : 0", sender)
        self.assertIn("AGENT_V2_ACTION_RECEIPT_KIND", sender)

        handler = server.split("void handle_unit_do_action", 1)[1].split(
            "void unit_do_action", 1
        )[0]
        self.assertIn("bool performed = unit_perform_action(", handler)
        self.assertLess(
            handler.index("unit_perform_action("),
            handler.index("agent_v2_send_action_receipt("),
        )
        for binding in (
            "pplayer, actor_id, target_id, action_type, performed",
            "actor_id, target_id, sub_tgt_id, name",
            "action_type, ACT_REQ_PLAYER",
        ):
            self.assertIn(binding, handler)

    def test_client_consumes_receipt_only_after_passive_observer(self) -> None:
        packhand = (self.repository / "client" / "packhand.c").read_text(
            encoding="utf-8"
        )
        protocol = (
            self.repository / "client" / "gui-agent" / "protocol_v2.c"
        ).read_text(encoding="utf-8")

        handler = packhand.split("void handle_unit_action_answer", 1)[1].split(
            "/************************************************************************", 1
        )[0]
        self.assertLess(
            handler.index("unit_action_answer_observer("),
            handler.index(
                "if (request_kind == AGENT_V2_ACTION_RECEIPT_KIND)"
            ),
        )
        receipt_return = handler.split(
            "if (request_kind == AGENT_V2_ACTION_RECEIPT_KIND)", 1
        )[1].split("}", 1)[0]
        self.assertIn("return;", receipt_return)

        observer = protocol.split(
            "static void v2_unit_action_answer_observer(", 2
        )[2].split("static void v2_handle_target_action", 1)[0]
        receipt_branch = observer.split(
            "if (packet->request_kind == AGENT_V2_ACTION_RECEIPT_KIND)", 1
        )[1].split(
            "if (v2_target_query.active", 1
        )[0]
        for binding in (
            "fc_agent_v2_action_receipt_matches(",
            "v2_pending.active",
            "v2_pending.processing_started",
            "v2_pending.baseline_captured",
            "v2_pending.seat_epoch == v2_seat_epoch",
            "v2_pending.terminal == FC_AGENT_V2_TERMINAL_NONE",
            "v2_pending.before_unit_present",
            "v2_pending.before_special_target_exact",
            "request_id, v2_pending.request_id",
            "packet->actor_id, v2_pending.action.unit_id",
            "packet->target_id, expected_target",
            "packet->action_type, v2_pending.action.action",
            "packet->cost",
            "v2_pending.action_success_receipt_latched = TRUE",
        ):
            self.assertIn(binding, receipt_branch)
        self.assertIn("return;", receipt_branch)

    def test_city_postconditions_use_structured_receipt_authority(self) -> None:
        protocol = (
            self.repository / "client" / "gui-agent" / "protocol_v2.c"
        ).read_text(encoding="utf-8")
        postconditions = protocol.split(
            "static bool v2_action_postcondition(void)", 2
        )[2].split("case ACTRES_SPY_STEAL_TECH:", 1)[0]

        poison = postconditions.split("case ACTRES_SPY_POISON:", 1)[1].split(
            "case ACTRES_ESTABLISH_EMBASSY:", 1
        )[0]
        random_sabotage = postconditions.split(
            "case ACTRES_SPY_SABOTAGE_CITY:", 1
        )[1].split("case ACTRES_SPY_SABOTAGE_CITY_PRODUCTION:", 1)[0]
        production_sabotage = postconditions.split(
            "case ACTRES_SPY_SABOTAGE_CITY_PRODUCTION:", 1
        )[1]
        for branch in (poison, random_sabotage, production_sabotage):
            self.assertIn("v2_pending.action_success_receipt_latched", branch)
            self.assertNotIn("success_event_latched", branch)


if __name__ == "__main__":
    unittest.main()
