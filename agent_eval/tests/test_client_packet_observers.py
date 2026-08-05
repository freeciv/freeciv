from __future__ import annotations

from pathlib import Path
import unittest


class ClientPacketObserverTests(unittest.TestCase):
    def test_unit_packet_normalizes_absent_action_decision_tile(self) -> None:
        repository = Path(__file__).resolve().parents[2]
        source = (repository / "client" / "packhand.c").read_text(
            encoding="utf-8"
        )

        unpack = source.split(
            "punit->action_decision_want = packet->action_decision_want;", 1
        )[1].split("punit->client.asking_city_name", 1)[0]
        self.assertIn(
            "packet->action_decision_want == ACT_DEC_NOTHING", unpack,
        )
        self.assertIn("? nullptr", unpack)
        self.assertIn(
            ": index_to_tile(&(wld.map), packet->action_decision_tile)",
            unpack,
        )

    def test_vote_observer_sees_each_structured_cache_transition(self) -> None:
        repository = Path(__file__).resolve().parents[2]
        header = (repository / "client" / "packhand.h").read_text(
            encoding="utf-8"
        )
        source = (repository / "client" / "packhand.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("enum packhand_vote_stage", header)
        self.assertIn("typedef void (*packhand_vote_observer_fn)(", header)
        self.assertIn("void packhand_set_vote_observer(", header)
        setter = source.split(
            "void packhand_set_vote_observer(", 1
        )[1].split("}\n", 1)[0]
        self.assertIn("vote_observer = observer;", setter)
        self.assertIn("observer != NULL ? data : NULL", setter)
        for handler, stage in (
            ("handle_vote_new", "PACKHAND_VOTE_NEW"),
            ("handle_vote_update", "PACKHAND_VOTE_UPDATE"),
            ("handle_vote_resolve", "PACKHAND_VOTE_RESOLVE"),
            ("handle_vote_remove", "PACKHAND_VOTE_REMOVE"),
        ):
            with self.subTest(handler=handler):
                body = source.split(f"void {handler}(", 1)[1].split(
                    "/************************************************************************", 1
                )[0]
                self.assertIn(stage, body)
                self.assertIn(
                    "client.conn.client.request_id_of_currently_handled_packet",
                    body,
                )

    def test_passive_request_correlated_packet_observers_preserve_gui_handlers(
        self,
    ) -> None:
        repository = Path(__file__).resolve().parents[2]
        header = (repository / "client" / "packhand.h").read_text(
            encoding="utf-8"
        )
        source = (repository / "client" / "packhand.c").read_text(
            encoding="utf-8"
        )

        observers = {
            "unit_action_answer": "handle_unit_action_answer",
            "chat_msg": "handle_chat_msg",
            "nuke_tile_info": "handle_nuke_tile_info",
            "unit_combat_info": "handle_unit_combat_info",
        }

        for name, handler in observers.items():
            with self.subTest(observer=name):
                self.assertIn(
                    f"typedef void (*packhand_{name}_observer_fn)(", header
                )
                self.assertIn(
                    f"void packhand_set_{name}_observer(", header
                )

                setter = source.split(
                    f"void packhand_set_{name}_observer(", 1
                )[1].split("}\n", 1)[0]
                self.assertIn(f"{name}_observer = observer;", setter)
                self.assertIn("observer != NULL ? data : NULL", setter)

                body = source.split(f"void {handler}(", 1)[1].split(
                    "/************************************************************************",
                    1,
                )[0]
                self.assertIn(f"if ({name}_observer != NULL)", body)
                callback = body.split(f"{name}_observer(", 1)[1].split(");", 1)[
                    0
                ]
                self.assertIn(
                    "client.conn.client.request_id_of_currently_handled_packet",
                    callback,
                )
                self.assertNotIn("return", callback)

        self.assertLess(
            source.index("nuke_tile_info_observer("),
            source.index("put_nuke_mushroom_pixmaps("),
        )
        chat_handler = source.index("void handle_chat_msg(")
        self.assertLess(
            source.index("chat_msg_observer(", chat_handler),
            source.index("handle_event(", chat_handler),
        )
        self.assertIn(
            "const struct packet_unit_action_answer packet = {", source
        )
        self.assertIn(
            "const struct packet_city_sabotage_list packet = {", source
        )

    def test_reserved_sabotage_observer_can_prevent_hidden_cache_hydration(
        self,
    ) -> None:
        repository = Path(__file__).resolve().parents[2]
        header = (repository / "client" / "packhand.h").read_text(
            encoding="utf-8"
        )
        source = (repository / "client" / "packhand.c").read_text(
            encoding="utf-8"
        )

        self.assertIn(
            "typedef bool (*packhand_city_sabotage_list_observer_fn)(",
            header,
        )
        self.assertIn(
            "void packhand_set_city_sabotage_list_observer(", header
        )
        body = source.split("void handle_city_sabotage_list(", 1)[1].split(
            "/************************************************************************", 1
        )[0]
        observer = body.index("if (city_sabotage_list_observer(")
        early_return = body.index("return;", observer)
        cache_update = body.index("update_improvement_from_packet(")
        self.assertLess(observer, early_return)
        self.assertLess(early_return, cache_update)
        callback = body[observer:early_return]
        self.assertIn(
            "client.conn.client.request_id_of_currently_handled_packet",
            callback,
        )

    def test_city_espionage_chat_events_are_only_corroborative(
        self,
    ) -> None:
        repository = Path(__file__).resolve().parents[2]
        source_path = repository / "client" / "gui-agent" / "protocol_v2.c"
        source = source_path.read_text(encoding="utf-8")

        action_family = source.split(
            "static enum event_type v2_city_espionage_success_event(", 1
        )[1].split("static bool v2_paid_quote_accepted", 1)[0]
        for action in (
            "ACTION_SPY_POISON",
            "ACTION_SPY_POISON_ESC",
            "ACTION_SPY_SABOTAGE_CITY",
            "ACTION_SPY_SABOTAGE_CITY_ESC",
            "ACTION_SPY_SABOTAGE_CITY_PRODUCTION",
            "ACTION_SPY_SABOTAGE_CITY_PRODUCTION_ESC",
        ):
            self.assertIn(action, action_family)
        self.assertNotIn("ACTION_SPY_TARGETED_SABOTAGE_CITY", action_family)

        observer = source.split("static void v2_chat_msg_observer(", 2)[2].split(
            "static void v2_nuke_tile_info_observer", 1
        )[0]
        for binding in (
            "v2_pending.revision",
            "actor_binding_exact",
            "city_binding_exact",
            "request_id, v2_pending.request_id",
            "packet->tile, v2_pending.action.target_tile",
            "packet->event, city_success_event",
        ):
            self.assertIn(binding, observer)
        self.assertIn("E_MY_DIPLOMAT_POISON", observer)
        self.assertIn("sabotage_city_success_event_latched", observer)

        postcondition_source = source.split(
            "static bool v2_action_postcondition(void)", 2
        )[2]
        postconditions = postcondition_source.split(
            "case ACTRES_SPY_POISON:", 1
        )[1].split("case ACTRES_SPY_STEAL_TECH:", 1)[0]
        self.assertIn("fc_agent_v2_poison_city_postcondition", postconditions)
        self.assertIn(
            "v2_pending.action_success_receipt_latched", postconditions
        )
        self.assertIn("fc_agent_v2_sabotage_city_postcondition", postconditions)
        self.assertNotIn("poison_city_success_event_latched", postconditions)
        self.assertNotIn("sabotage_city_success_event_latched", postconditions)

    def test_investigation_observer_captures_exact_normal_client_boundary(
        self,
    ) -> None:
        repository = Path(__file__).resolve().parents[2]
        header = (repository / "client" / "packhand.h").read_text(
            encoding="utf-8",
        )
        source = (repository / "client" / "packhand.c").read_text(
            encoding="utf-8",
        )

        self.assertIn(
            "typedef void (*packhand_investigation_observer_fn)(", header,
        )
        self.assertIn(
            "void packhand_set_investigation_observer(", header,
        )
        setter = source.split(
            "void packhand_set_investigation_observer(", 1,
        )[1].split("}\n", 1)[0]
        self.assertIn("investigation_observer = observer;", setter)
        self.assertIn("observer != NULL ? data : NULL", setter)

        city_info = source.split("void handle_city_info(", 1)[1].split(
            "/**************************************************************************", 1,
        )[0]
        self.assertIn("packet->diplomat_investigate", city_info)
        self.assertIn("PACKHAND_INVESTIGATION_CITY_INFO", city_info)
        self.assertLess(
            city_info.index("agents_city_changed(pcity);"),
            city_info.index("PACKHAND_INVESTIGATION_CITY_INFO"),
        )
        for handler, stage in (
            ("handle_investigate_started", "PACKHAND_INVESTIGATION_STARTED"),
            ("handle_investigate_finished", "PACKHAND_INVESTIGATION_FINISHED"),
        ):
            body = source.split(f"void {handler}(", 1)[1].split(
                "/**************************************************************************", 1,
            )[0]
            self.assertIn(stage, body)
            self.assertIn(
                "client.conn.client.request_id_of_currently_handled_packet",
                body,
            )


if __name__ == "__main__":
    unittest.main()
