from __future__ import annotations

import re
import unittest
from pathlib import Path

from agent_eval.full_control_v2 import (
    FULL_CONTROL_SCHEMA_VERSION,
    FULL_CONTROL_V2,
    FullControlSchemaError,
    TERMINAL_RECEIPT_STATES,
    validate_command_receipt,
    validate_initial_command_batch,
    validate_legal_action_descriptor,
    validate_state_revision,
    validate_structured_error,
    validate_supported_control_protocols,
    validated_batch_request_hash,
)


REVISION = {"turn": 7, "revision": 3, "state_token": "state_opaque-7-3"}


class FullControlV2SchemaTests(unittest.TestCase):
    def test_native_state_manifest_classifies_chat_recipients_and_trade_routes(self):
        repository = Path(__file__).parents[2]
        manifest = (
            repository / "client" / "gui-agent" / "state_manifest.def"
        ).read_text(encoding="utf-8")
        rows = re.findall(
            r"^AGENT_V2_STATE_CLASS\(\s*([a-z_]+),",
            manifest,
            flags=re.MULTILINE,
        )
        self.assertEqual(len(rows), 47)
        self.assertEqual(rows.count("chat"), 1)
        self.assertEqual(rows.count("chat_recipient"), 1)
        self.assertEqual(rows.count("city_trade_route"), 1)
        self.assertIn(
            "AGENT_V2_STATE_CLASS(chat, client_packet, "
            "normal_client_visible_history_only)",
            manifest,
        )

        protocol = (
            repository / "client" / "gui-agent" / "protocol_v2.c"
        ).read_text(encoding="utf-8")
        initializer = protocol.split("void fc_agent_v2_init(", 1)[1].split(
            "void fc_agent_v2_reset(void)", 1,
        )[0]
        self.assertIn("fc_assert(AGENT_V2_MANIFEST_COUNT == 47);", initializer)

    def test_relation_frames_reach_the_v2_dispatcher(self):
        source = (
            Path(__file__).parents[2]
            / "client" / "gui-agent" / "protocol_v2.c"
        ).read_text(encoding="utf-8")
        admission = source.split(
            "bool fc_agent_v2_handle(const char *payload, size_t length)", 1,
        )[1].split("if (length > FC_AGENT_IPC_MAX_PAYLOAD)", 1)[0]
        self.assertIn('strlen("RELATION_SCOPE_OPEN")', admission)
        self.assertIn('strlen("RELATION_SCOPE_PAGE")', admission)

    def test_diplomacy_cancel_capability_requires_exact_native_ok(self):
        source = (
            Path(__file__).parents[2]
            / "client" / "gui-agent" / "protocol_v2.c"
        ).read_text(encoding="utf-8")
        self.assertIn("cancel_reason == DIPL_OK ? 1 : 0", source)
        self.assertGreaterEqual(
            source.count("pplayer_can_cancel_treaty(self, other) == DIPL_OK"),
            2,
        )
        relation_scope = source.split(
            "static bool v2_build_relation_scope", 1,
        )[1].split("static void v2_handle_relation_scope_open", 1)[0]
        self.assertNotIn("!= DIPL_ERROR", relation_scope)

    def test_late_treaty_requirement_failure_uses_cleanup(self):
        source = (
            Path(__file__).parents[2] / "server" / "diplhand.c"
        ).read_text(encoding="utf-8")
        late_check = source.split(
            "Check that one who accepted treaty earlier", 1,
        )[1].split("call_treaty_accepted", 1)[0]
        requirement_failure = late_check.split(
            "Clause requirements are no longer fulfilled", 1,
        )[1].split("}", 1)[0]
        self.assertIn("goto cleanup;", requirement_failure)
        self.assertNotIn("return;", requirement_failure)

    def test_shared_tiles_cancel_refreshes_direction_and_cities(self):
        source = (
            Path(__file__).parents[2] / "server" / "plrhand.c"
        ).read_text(encoding="utf-8")
        branch = source.split(
            "if (clause == CLAUSE_SHARED_TILES)", 1,
        )[1].split("diplcheck =", 1)[0]
        self.assertLess(
            branch.index("city_refresh(pcity);"),
            branch.index("send_player_info_c(pplayer, nullptr);"),
        )
        self.assertLess(
            branch.index("send_player_info_c(pplayer, nullptr);"),
            branch.index("sync_cities();"),
        )

    def test_grouped_callback_cleanup_uses_matching_request_boundaries(self):
        source = (
            Path(__file__).parents[2]
            / "client" / "gui-agent" / "protocol_v2.c"
        ).read_text(encoding="utf-8")
        clear = source.split("static void v2_pending_clear(void)", 1)[1]
        clear = clear.split("static void v2_invalidate_seat_epoch", 1)[0]
        self.assertIn(
            "v2_pending.first_request_id, v2_action_processing_started,",
            clear,
        )
        self.assertIn(
            "v2_pending.request_id, v2_action_last_processing_started,",
            clear,
        )
        self.assertNotIn(
            "v2_pending.request_id, v2_action_processing_started,",
            clear,
        )

    def test_state_revision_is_strict_and_normalized(self):
        self.assertEqual(validate_state_revision(REVISION), REVISION)
        for invalid in (
            {**REVISION, "turn": True},
            {**REVISION, "revision": -1},
            {**REVISION, "extra": 1},
            {"turn": 7, "revision": 3},
        ):
            with self.subTest(invalid=invalid), self.assertRaises(
                FullControlSchemaError,
            ):
                validate_state_revision(invalid)

    def test_legal_actions_use_versioned_families_not_a_ruleset_allowlist(self):
        descriptor = {
            "action_id": "act_server-authored",
            "kind": "unit.perform_action",
            "label": "Paradrop Paratrooper to tile (12, 8)",
            "subject": {"type": "unit", "id": "unit_19"},
            "arguments_schema": {},
            "state_revision": REVISION,
        }
        self.assertEqual(
            validate_legal_action_descriptor(descriptor), descriptor,
        )
        future_catalog_operation = {
            **descriptor,
            "kind": "unit.ruleset_catalog_operation",
        }
        self.assertEqual(
            validate_legal_action_descriptor(future_catalog_operation)["kind"],
            "unit.ruleset_catalog_operation",
        )
        for invalid_kind in ("move", "alien.perform", "unit.Perform"):
            with self.subTest(kind=invalid_kind), self.assertRaises(
                FullControlSchemaError,
            ):
                validate_legal_action_descriptor({
                    **descriptor, "kind": invalid_kind,
                })

    def test_initial_batch_is_one_command_and_hash_guarded(self):
        batch = {
            "schema_version": FULL_CONTROL_SCHEMA_VERSION,
            "control_protocol": FULL_CONTROL_V2,
            "game_id": "game_opaque",
            "agent_id": "agent_opaque",
            "batch_id": "batch_opaque",
            "state_revision": REVISION,
            "commands": [{
                "action_id": "act_server-authored",
                "arguments": {"city_name": "Ada"},
            }],
        }
        self.assertEqual(validate_initial_command_batch(batch), batch)
        clean, request_hash = validated_batch_request_hash(batch)
        self.assertEqual(clean, batch)
        self.assertEqual(len(request_hash), 64)
        changed = {
            **batch,
            "commands": [{
                "action_id": "act_server-authored",
                "arguments": {"city_name": "Grace"},
            }],
        }
        self.assertNotEqual(
            request_hash, validated_batch_request_hash(changed)[1],
        )

        two_commands = {
            **batch,
            "commands": batch["commands"] * 2,
        }
        with self.assertRaisesRegex(FullControlSchemaError, "exactly one"):
            validate_initial_command_batch(two_commands)

    def test_structured_errors_and_capability_lists_are_strict(self):
        value = {
            "schema_version": FULL_CONTROL_SCHEMA_VERSION,
            "control_protocol": FULL_CONTROL_V2,
            "error": {
                "code": "stale_revision",
                "message": "State changed; refetch legal actions.",
                "retryable": True,
                "details": {"expected_revision": 4},
            },
            "state_revision": REVISION,
        }
        self.assertEqual(validate_structured_error(value), value)
        self.assertEqual(
            validate_supported_control_protocols(
                ["strategic-v1", "full-control-v2", "future-v3"],
            ),
            ("full-control-v2", "future-v3", "strategic-v1"),
        )
        with self.assertRaises(FullControlSchemaError):
            validate_structured_error({
                **value,
                "error": {**value["error"], "code": "made_up"},
            })
        with self.assertRaises(FullControlSchemaError):
            validate_supported_control_protocols(
                ["full-control-v2", "full-control-v2"],
            )
        self.assertEqual(
            validate_structured_error({
                **value,
                "error": {
                    **value["error"],
                    "code": "not_implemented",
                    "retryable": False,
                },
            })["error"]["code"],
            "not_implemented",
        )
        self.assertEqual(
            validate_structured_error({
                **value,
                "error": {
                    **value["error"],
                    "code": "invalid_request",
                    "retryable": False,
                },
            })["error"]["code"],
            "invalid_request",
        )

    def test_command_receipt_states_and_errors_are_strict(self):
        receipt = {
            "schema_version": FULL_CONTROL_SCHEMA_VERSION,
            "control_protocol": FULL_CONTROL_V2,
            "game_id": "game_opaque",
            "agent_id": "agent_opaque",
            "batch_id": "batch_opaque",
            "receipt_state": "applied",
            "idempotent": False,
            "state_revision": REVISION,
            "error": None,
            "observation": None,
        }
        self.assertEqual(validate_command_receipt(receipt), receipt)
        accepted = {**receipt, "receipt_state": "accepted"}
        self.assertEqual(validate_command_receipt(accepted), accepted)
        investigation = {
            "id": "observation_opaque",
            "type": "city_investigation",
            "source": "human_client_city_info",
            "freshness": "captured_at_receipt_revision",
            "state_revision": REVISION,
            "city": {
                "id": "city_opaque",
                "name": "Beta",
                "size": 2,
                "production": {
                    "id": "production_opaque",
                    "kind": "unit",
                    "name": "Settlers",
                },
                "shields": {"stock": 19, "surplus": 4},
                "improvements": [{"id": "improvement_opaque", "name": "Granary"}],
                "citizens": {
                    "feelings": [{
                        "stage": stage, "happy": 1, "content": 0,
                        "unhappy": 0, "angry": 0,
                    } for stage in (
                        "base", "luxury", "effects", "nationality",
                        "martial_law", "final",
                    )],
                    "specialists": [{
                        "id": "specialist_opaque", "name": "Entertainer",
                        "count": 1,
                    }],
                },
            },
        }
        applied_investigation = {**receipt, "observation": investigation}
        self.assertEqual(
            validate_command_receipt(applied_investigation),
            applied_investigation,
        )
        for invalid in (
            {**accepted, "observation": investigation},
            {
                **applied_investigation,
                "observation": {
                    **investigation,
                    "state_revision": {**REVISION, "revision": 4},
                },
            },
            {
                **applied_investigation,
                "observation": {
                    **investigation,
                    "city": {**investigation["city"], "size": 3},
                },
            },
        ):
            with self.assertRaises(FullControlSchemaError):
                validate_command_receipt(invalid)
        self.assertNotIn("accepted", TERMINAL_RECEIPT_STATES)
        self.assertEqual(
            TERMINAL_RECEIPT_STATES, {"ambiguous", "applied", "rejected"},
        )
        with self.assertRaises(FullControlSchemaError):
            validate_command_receipt({**receipt, "receipt_state": "queued"})
        with self.assertRaises(FullControlSchemaError):
            validate_command_receipt({**receipt, "receipt_state": "rejected"})
        rejected_error = {
            "schema_version": FULL_CONTROL_SCHEMA_VERSION,
            "control_protocol": FULL_CONTROL_V2,
            "error": {
                "code": "illegal_action",
                "message": "The action is no longer legal.",
                "retryable": True,
                "details": {
                    "rejection": {
                        "layer": "catalog",
                        "reason": "action_not_advertised",
                        "native_code": None,
                        "native_reason": None,
                    },
                },
            },
            "state_revision": REVISION,
        }
        rejected = {
            **receipt,
            "receipt_state": "rejected",
            "error": rejected_error,
        }
        self.assertEqual(validate_command_receipt(rejected), rejected)
        # A rejected receipt that names no layer is a contract violation, so
        # a bare illegal_action can no longer reach an agent.
        with self.assertRaisesRegex(
            FullControlSchemaError, "must attribute its refusal",
        ):
            validate_command_receipt({
                **rejected,
                "error": {
                    **rejected_error,
                    "error": {**rejected_error["error"], "details": {}},
                },
            })
        with self.assertRaisesRegex(
            FullControlSchemaError, "same non-null state_revision",
        ):
            validate_command_receipt({
                **rejected,
                "error": {**rejected_error, "state_revision": None},
            })
        with self.assertRaisesRegex(
            FullControlSchemaError, "same non-null state_revision",
        ):
            validate_command_receipt({
                **rejected,
                "error": {
                    **rejected_error,
                    "state_revision": {**REVISION, "revision": 4},
                },
            })

        ambiguous_error = {
            "schema_version": FULL_CONTROL_SCHEMA_VERSION,
            "control_protocol": FULL_CONTROL_V2,
            "error": {
                "code": "action_outcome_ambiguous",
                "message": "The action was accepted but its outcome is unknown.",
                "retryable": False,
                "details": {},
            },
            "state_revision": REVISION,
        }
        ambiguous = {
            **receipt,
            "receipt_state": "ambiguous",
            "error": ambiguous_error,
        }
        self.assertEqual(validate_command_receipt(ambiguous), ambiguous)
        for invalid in (
            {**ambiguous, "error": None},
            {
                **ambiguous,
                "error": {
                    **ambiguous_error,
                    "error": {
                        **ambiguous_error["error"],
                        "retryable": True,
                    },
                },
            },
            {
                **ambiguous,
                "error": {
                    **ambiguous_error,
                    "error": {
                        **ambiguous_error["error"],
                        "code": "illegal_action",
                    },
                },
            },
            {
                **ambiguous,
                "error": {
                    **ambiguous_error,
                    "state_revision": {**REVISION, "revision": 4},
                },
            },
            {**receipt, "receipt_state": "accepted", "error": ambiguous_error},
            {**receipt, "receipt_state": "applied", "error": ambiguous_error},
            {**rejected, "error": ambiguous_error},
        ):
            with self.subTest(invalid=invalid), self.assertRaises(
                FullControlSchemaError,
            ):
                validate_command_receipt(invalid)


if __name__ == "__main__":
    unittest.main()
