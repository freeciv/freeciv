from __future__ import annotations

import argparse
import contextlib
import io
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import textwrap
import threading
import time
import unittest
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from contextlib import contextmanager, redirect_stderr, redirect_stdout
from pathlib import Path
from unittest.mock import patch

import client
import state_mirror


class PlayerClientTests(unittest.TestCase):
    GAME_ID = "game_12345678901234567890"
    AGENT_ID = "agent_test-controller"

    @classmethod
    def v2_session(cls, root: Path) -> tuple[Path, dict]:
        session = {
            "schema_version": 1,
            "service_url": "http://127.0.0.1:8765",
            "game_id": cls.GAME_ID,
            "agent_id": cls.AGENT_ID,
            "agent_token": "v2-agent-secret",
            "place": 1,
            "seat_id": "place-1",
            "player_name": "AgentPlace1",
            "controller_label": "codex-test-model",
            "control_protocol": "full-control-v2",
            "objective": "Maximize final Freeciv civilization score.",
            "max_turns": 5000,
            "turns_remaining": None,
        }
        path = root / ".sessions" / cls.GAME_ID / "codex-test.json"
        client._write_private_json(path, session)
        return path, session

    @staticmethod
    def revision(number: int = 7, *, turn: int = 3) -> dict:
        return {
            "turn": turn,
            "revision": number,
            "state_token": f"state_{number:032d}",
        }

    @classmethod
    def page(
        cls, session: dict, *, legal: bool, revision: dict,
        items: list | None = None, cursor: str | None = None,
    ) -> dict:
        return {
            "schema_version": 2,
            "control_protocol": "full-control-v2",
            "game_id": session["game_id"],
            "agent_id": session["agent_id"],
            "state_revision": revision,
            "page": {
                "section": "legal_actions" if legal else "overview",
                "items": [] if items is None else items,
                "total_items": len([] if items is None else items)
                + (1 if cursor else 0),
                "next_cursor": cursor,
            },
        }

    @classmethod
    def descriptor(cls, revision: dict, action_id: str = "action_opaque") -> dict:
        return {
            "action_id": action_id,
            "kind": "phase.end",
            "label": "End phase",
            "subject": {"operation": "end"},
            "arguments_schema": {"type": "object"},
            "state_revision": revision,
        }

    @classmethod
    def receipt(
        cls, session: dict, batch_id: str, state: str = "applied",
        *, revision: dict | None = None,
    ) -> dict:
        current = revision or cls.revision(8)
        error = None
        if state in {"rejected", "ambiguous"}:
            code = (
                "action_outcome_ambiguous"
                if state == "ambiguous" else "illegal_action"
            )
            error = cls.error(code=code, revision=current, retryable=False)
        return {
            "schema_version": 2,
            "control_protocol": "full-control-v2",
            "game_id": session["game_id"],
            "agent_id": session["agent_id"],
            "batch_id": batch_id,
            "receipt_state": state,
            "idempotent": False,
            "state_revision": current,
            "error": error,
            "observation": None,
        }

    @staticmethod
    def error(
        *, code: str = "invalid_request", revision: dict | None = None,
        retryable: bool = False,
    ) -> dict:
        return {
            "schema_version": 2,
            "control_protocol": "full-control-v2",
            "error": {
                "code": code,
                "message": "validated test error",
                "retryable": retryable,
                "details": {},
            },
            "state_revision": revision,
        }

    @classmethod
    def health(
        cls, session: dict, *, active: bool = False,
        game_state: str = "running",
    ) -> dict:
        phase = None if game_state in client.TERMINAL_STATES else {
            "state": "awaiting_agent" if active else "native_phase",
            "turn": 3,
            "phase": 1,
            "active": active,
            "timing": {
                "mode": "default", "timeout_s": 180,
                "deadline_started_at": 1000.0, "deadline_at": 1180.0,
                "elapsed_s": 1.0, "remaining_s": 179.0,
            },
        }
        return {
            "schema_version": 2,
            "control_protocol": "full-control-v2",
            "game_id": session["game_id"],
            "objective": session["objective"],
            "max_turns": session["max_turns"],
            "turns_remaining": (
                None if phase is None else session["max_turns"] - phase["turn"]
            ),
            "agent": {
                "agent_id": session["agent_id"],
                "controller_label": session["controller_label"],
            },
            "game_state": game_state,
            "seat": {
                "place": 1, "seat_id": "place-1",
                "player_name": "AgentPlace1",
            },
            "sidecar": {"state": "ready", "generation": 1},
            "observation_available": game_state == "running",
            "legal_actions_available": game_state == "running",
            "phase": phase,
            "last_phase_end": None,
        }

    @classmethod
    def wait_response(
        cls, session: dict, wake_reason: str, *, active: bool = False,
        game_state: str = "running", revision: dict | None = None,
    ) -> dict:
        return {
            "schema_version": 2,
            "control_protocol": "full-control-v2",
            "game_id": session["game_id"],
            "agent_id": session["agent_id"],
            "wake_reason": wake_reason,
            "health": cls.health(
                session, active=active, game_state=game_state,
            ),
            "state_revision": revision,
        }

    def test_v2_turn_restarts_once_and_returns_one_compact_revision(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                health = self.health(session, active=True)

                def state_page(section, revision, items, *, cursor=None):
                    page = self.page(
                        session, legal=False, revision=revision,
                        items=items, cursor=cursor,
                    )
                    page["page"]["section"] = section
                    return page

                first = self.revision(7)
                drifted = self.revision(8)
                stable = self.revision(9)
                cursor = "cursor_" + "c" * 32
                responses = [
                    client.JSONResponse(200, health),
                    client.JSONResponse(200, state_page(
                        "overview", first, [{
                            "turn": 3,
                            "player": {
                                "government": "Despotism",
                                "economy": {"gold": 25},
                            },
                        }],
                    )),
                    client.JSONResponse(200, state_page(
                        "cities", first, [{"id": "city_" + "a" * 32}],
                    )),
                    client.JSONResponse(200, state_page(
                        "units", drifted, [{"id": "unit_" + "b" * 32}],
                    )),
                    client.JSONResponse(200, state_page(
                        "research", drifted, [{"name": "Alphabet"}],
                    )),
                    client.JSONResponse(200, health),
                    client.JSONResponse(200, health),
                    client.JSONResponse(200, state_page(
                        "overview", stable, [{"turn": 3, "player": None}],
                    )),
                    client.JSONResponse(200, state_page(
                        "cities", stable,
                        [{"id": "city_" + "a" * 32}], cursor=cursor,
                    )),
                    client.JSONResponse(200, state_page(
                        "units", stable, [{"id": "unit_" + "b" * 32}],
                    )),
                    client.JSONResponse(200, state_page(
                        "research", stable, [{"name": "Alphabet"}],
                    )),
                    client.JSONResponse(200, health),
                ]
                stdout = io.StringIO()
                args = type("Args", (), {
                    "session": str(session_path), "json_output": True,
                })()
                with patch.object(
                    client, "_v2_response", side_effect=responses,
                ) as request, redirect_stdout(stdout):
                    self.assertEqual(client.command_turn(args), 0)

                result = json.loads(stdout.getvalue())
                self.assertEqual(result["status"], "ready")
                self.assertEqual(result["state_revision"], stable)
                self.assertEqual(result["cities"]["next_cursor"], cursor)
                self.assertTrue(
                    any(
                        command.endswith(f"--cursor {cursor}")
                        for command in result["next_commands"]
                    ),
                    result["next_commands"],
                )
                self.assertEqual(result["cities"]["shown"], 1)
                self.assertEqual(result["cities"]["total"], 2)
                self.assertTrue(result["cities"]["truncated"])
                self.assertEqual(request.call_count, 12)
                state = client._load_v2_client_state(session_path, session)
                self.assertEqual(state["last_revision"], stable)

    def test_v2_result_accepts_positional_or_named_id_and_state_hints(self):
        parser = client.parser()
        self.assertEqual(
            parser.parse_args(["result", self.GAME_ID]).game_id_positional,
            self.GAME_ID,
        )
        self.assertEqual(
            parser.parse_args([
                "result", "--game-id", self.GAME_ID,
            ]).game_id,
            self.GAME_ID,
        )
        state_args = type("Args", (), {
            "cursor": "", "section": "economy", "actor_id": "",
            "relation_id": "", "center_id": "", "radius": None,
            "limit": None,
        })()
        with self.assertRaisesRegex(
            client.PlayerError, "Economy and current government are in overview",
        ):
            client._state_query(state_args)

    def test_v2_legal_kind_all_drains_compacts_and_caches_full_actions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(11)

                def action(action_id, kind, target_name, probability):
                    descriptor = self.descriptor(revision, action_id)
                    descriptor["kind"] = kind
                    descriptor["subject"] = {
                        "operation": "set_target",
                        "target": (
                            None if target_name is None else {
                                "type": "technology",
                                "id": "tech_" + action_id,
                                "name": target_name,
                            }
                        ),
                        "probability": probability,
                        "internal_detail_kept_only_in_cache": True,
                    }
                    return descriptor

                exact = {
                    "kind": "exact", "minimum_percent": 100,
                    "maximum_percent": 100,
                }
                uncertain = {
                    "kind": "unknown", "minimum_percent": 0,
                    "maximum_percent": 100,
                }
                target_one = action(
                    "action_target_one", "research.set_target", "Alphabet",
                    exact,
                )
                phase_end = action(
                    "action_phase_end", "phase.end", None, exact,
                )
                target_two = action(
                    "action_target_two", "research.set_target", "Bronze Working",
                    uncertain,
                )
                target_three = action(
                    "action_target_three", "research.set_target", "Ceremonial Burial",
                    exact,
                )
                cursor_one = "cursor_" + "a" * 32
                cursor_two = "cursor_" + "b" * 32
                pages = [
                    self.page(
                        session, legal=True, revision=revision,
                        items=[target_one, phase_end], cursor=cursor_one,
                    ),
                    self.page(
                        session, legal=True, revision=revision,
                        items=[target_two], cursor=cursor_two,
                    ),
                    self.page(
                        session, legal=True, revision=revision,
                        items=[target_three],
                    ),
                ]
                for page in pages:
                    page["page"]["total_items"] = 4
                args = type("Args", (), {
                    "session": str(session_path), "actor_id": "",
                    "target_id": "", "limit": None, "cursor": "",
                    "kind": "research.set_target", "all_pages": True,
                    "json_output": True,
                })()
                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response",
                    side_effect=[
                        client.JSONResponse(200, page) for page in pages
                    ],
                ) as request, redirect_stdout(stdout):
                    self.assertEqual(client.command_legal(args), 0)

                result = json.loads(stdout.getvalue())
                self.assertEqual(result["state_revision"], revision)
                self.assertEqual(result["catalog_total"], 4)
                self.assertEqual(result["pages_read"], 3)
                self.assertEqual(result["matched"], 3)
                self.assertEqual(result["offset"], 0)
                self.assertEqual(result["limit"], 64)
                self.assertEqual(result["shown"], 3)
                self.assertFalse(result["truncated"])
                self.assertFalse(result["has_more"])
                self.assertIsNone(result["next_offset"])
                self.assertFalse(result["byte_limited"])
                self.assertFalse(result["oversized_single"])
                self.assertEqual(
                    set(result["actions"][0]),
                    {
                        "action_id", "kind", "label", "subject", "target",
                        "argument_schema",
                    },
                )
                # The leak guard hides the internal *value* but never the fact
                # that a discriminator existed: the key survives as
                # `<withheld>`, and `--json` still carries the cached payload.
                self.assertEqual(
                    result["actions"][0]["subject"],
                    {
                        "operation": "set_target",
                        "internal_detail_kept_only_in_cache":
                            client.V2_WITHHELD,
                    },
                )
                self.assertEqual(
                    result["actions"][1]["probability"], uncertain,
                )
                self.assertIn(
                    f"cursor={cursor_one}", request.call_args_list[1].args[1],
                )
                self.assertIn(
                    f"cursor={cursor_two}", request.call_args_list[2].args[1],
                )
                cached = client._load_v2_client_state(session_path, session)
                self.assertEqual(cached["actions"][target_one["action_id"]], target_one)
                self.assertEqual(cached["actions"][phase_end["action_id"]], phase_end)

    def test_v2_compact_legal_pages_resume_after_byte_bound(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(13)
                actions = []
                for index, order in enumerate(("sentry", "fortify", "wake")):
                    descriptor = self.descriptor(
                        revision, f"action_order_{index}",
                    )
                    descriptor.update({
                        "kind": "unit.order",
                        "label": f"Order {order}",
                        "subject": {
                            "operation": "order", "order": order,
                        },
                    })
                    actions.append(descriptor)
                page = self.page(
                    session, legal=True, revision=revision, items=actions,
                )
                single_action_bytes = min(
                    len(json.dumps(
                        client._compact_legal_action(descriptor),
                        sort_keys=True, separators=(",", ":"),
                    ).encode("utf-8"))
                    for descriptor in actions
                )
                results = []
                for offset in range(3):
                    args = type("Args", (), {
                        "session": str(session_path), "actor_id": "",
                        "target_id": "", "limit": "2", "cursor": "",
                        "kind": "unit.order", "all_pages": True,
                        "offset": str(offset), "json_output": True,
                    })()
                    stdout = io.StringIO()
                    with patch.object(
                        client, "_v2_response",
                        return_value=client.JSONResponse(200, page),
                    ) as request, patch.object(
                        client, "V2_LEGAL_COMPACT_MAX_BYTES",
                        single_action_bytes - 1,
                    ), redirect_stdout(stdout):
                        self.assertEqual(client.command_legal(args), 0)
                    self.assertNotIn("limit=", request.call_args.args[1])
                    results.append(json.loads(stdout.getvalue()))

                self.assertEqual(
                    [result["actions"][0]["action_id"] for result in results],
                    [descriptor["action_id"] for descriptor in actions],
                )
                self.assertEqual(
                    [result["next_offset"] for result in results],
                    [1, 2, None],
                )
                self.assertEqual(
                    [result["has_more"] for result in results],
                    [True, True, False],
                )
                self.assertEqual(
                    [result["byte_limited"] for result in results],
                    [True, True, True],
                )
                self.assertTrue(all(
                    result["oversized_single"] for result in results
                ))
                self.assertTrue(all(result["matched"] == 3 for result in results))
                cached = client._load_v2_client_state(session_path, session)
                self.assertEqual(set(cached["actions"]), {
                    descriptor["action_id"] for descriptor in actions
                })

    def test_v2_compact_legal_action_retains_semantic_discriminators(self):
        revision = self.revision(12)
        order = self.descriptor(revision, "action_order")
        order.update({
            "kind": "unit.order",
            "label": "Sentry Warrior",
            "subject": {
                "operation": "order",
                "order": "sentry",
                "actor": {"id": "unit_" + "a" * 32, "type": "unit"},
                "target": None,
                "probability": {
                    "kind": "exact", "minimum_percent": 100,
                    "maximum_percent": 100,
                },
                "internal_native_packet": 77,
                "private_context": "not part of the public projection",
                "wire_sequence": 88,
            },
        })
        perform = self.descriptor(revision, "action_perform")
        perform.update({
            "kind": "unit.perform_action",
            "label": "Sabotage City production",
            "subject": {
                "operation": "perform_action",
                "action": "sabotage_city",
                "building_choice": {
                    "id": "improvement_choice", "name": "Production",
                },
                "target": {
                    "id": "city_" + "b" * 32, "type": "city",
                    "name": "Target City",
                },
            },
        })

        compact_order = client._compact_legal_action(order)
        compact_perform = client._compact_legal_action(perform)

        self.assertEqual(compact_order["label"], "Sentry Warrior")
        # Reserved keys keep their name and lose only their value, so the
        # agent can always tell that a discriminator existed (doc §5 forbids
        # unconditional field omission; only defaults may be elided).
        self.assertEqual(compact_order["subject"], {
            "operation": "order",
            "order": "sentry",
            "actor": {"id": "unit_" + "a" * 32, "type": "unit"},
            "internal_native_packet": client.V2_WITHHELD,
            "private_context": client.V2_WITHHELD,
            "wire_sequence": client.V2_WITHHELD,
        })
        for key in (
            "internal_native_packet", "private_context", "wire_sequence",
        ):
            self.assertIn(key, compact_order["subject"])
            self.assertNotEqual(
                compact_order["subject"][key], order["subject"][key],
            )
        self.assertEqual(compact_perform["subject"], {
            "operation": "perform_action",
            "action": "sabotage_city",
            "building_choice": {
                "id": "improvement_choice", "name": "Production",
            },
        })
        self.assertEqual(compact_perform["target"]["name"], "Target City")

    @classmethod
    def rendered_descriptor(
        cls, revision: dict, action_id: str, *,
        kind: str = "unit.order", label: str = "Sentry Warriors",
        subject: dict | None = None, schema: dict | None = None,
    ) -> dict:
        default_subject = {
            "actor": {"type": "unit", "id": "unit_" + "a" * 32},
            "target": None,
            "operation": "order",
            "order": "sentry",
            "variant": None,
            "consuming": False,
            "legality": "legal",
            "probability": {
                "kind": "exact", "minimum_percent": 100,
                "maximum_percent": 100,
            },
        }
        return {
            "action_id": action_id,
            "kind": kind,
            "label": label,
            "subject": default_subject if subject is None else subject,
            "arguments_schema": {} if schema is None else schema,
            "state_revision": revision,
        }

    def test_v2_text_legal_page_prints_the_envelope_exactly_once(self):
        revision = self.revision(11)
        session = {"game_id": self.GAME_ID, "agent_id": self.AGENT_ID}
        actor = "unit_" + "a" * 32
        certain = self.rendered_descriptor(revision, "action_" + "1" * 32)
        gamble = self.rendered_descriptor(
            revision, "action_" + "2" * 32,
            kind="unit.perform_action", label="Steal technology",
            subject={
                "actor": {"type": "unit", "id": actor},
                "target": {
                    "type": "city", "id": "city_" + "c" * 32, "name": "Paris",
                },
                "operation": "perform_action",
                "variant": "targeted_steal_tech",
                "consuming": True,
                "legality": "possibly_legal",
                "probability": {
                    "kind": "unknown", "minimum_percent": 0,
                    "maximum_percent": 100,
                },
            },
            schema={
                "type": "object",
                "properties": {"name": {"type": "string"}},
                "required": ["name"],
            },
        )
        page = self.page(
            session, legal=True, revision=revision,
            items=[certain, gamble], cursor="cursor_" + "9" * 32,
        )
        page["page"].update({
            "total_items": 3,
            "cursor_expires_at": "2999-01-01T00:00:00.000Z",
            "scope": {"actor_id": actor, "actor_type": "unit"},
            "catalog_id": "catalog_" + "e" * 32,
            "catalog_complete": False,
        })
        validated = client._validate_page(page, session, legal=True)

        lines = client._render_legal_page(validated)

        self.assertEqual(len(lines), 3)
        header, first, second = lines
        self.assertIn("rev11/t3", header)
        self.assertIn(f"scope=unit {actor}", header)
        self.assertIn("2/3", header)
        self.assertIn("more --cursor cursor_" + "9" * 32, header)
        # The envelope never repeats inside the body.
        for row in (first, second):
            for repeated in (
                "rev11", "state_token", "state_revision", "schema_version",
                "control_protocol", self.AGENT_ID, self.GAME_ID,
            ):
                self.assertNotIn(repeated, row)
        self.assertTrue(first.startswith("a1 "), first)
        self.assertTrue(first.endswith("action_" + "1" * 32), first)
        self.assertTrue(second.endswith("action_" + "2" * 32), second)
        # Omit-when-default: a certain, non-consuming, legal, variant-free
        # action shows none of those four fields, and an empty argument
        # schema renders away entirely.
        self.assertNotIn("prob", first)
        self.assertNotIn("legality", first)
        self.assertNotIn("consuming", first)
        self.assertNotIn("variant", first)
        self.assertNotIn("{", first)
        self.assertIn("order=sentry", first)
        # A non-default value is always visible and always marked.
        self.assertIn("!prob=0-100%/unknown", second)
        self.assertIn("!legality=possibly_legal", second)
        self.assertIn("!consuming", second)
        self.assertIn("!variant=targeted_steal_tech", second)
        self.assertIn("{name:string}", second)
        self.assertIn("→Paris", second)

    def test_v2_text_commands_keep_a_byte_identical_json_escape_hatch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                health = self.health(session, active=True)
                page = self.page(
                    session, legal=True, revision=revision,
                    items=[self.descriptor(revision)],
                )
                batch_id = "batch_" + "A" * 24
                receipt = self.receipt(session, batch_id, "applied")
                state_page = {
                    "schema_version": 2,
                    "control_protocol": "full-control-v2",
                    "game_id": session["game_id"],
                    "agent_id": session["agent_id"],
                    "state_revision": revision,
                    "page": {
                        "section": "units",
                        "items": [{
                            "id": "unit_" + "a" * 32, "scope": "own",
                            "type": "Settlers", "tile_id": "tile_" + "b" * 32,
                            "x": 31, "y": 72, "hp": 20, "moves": 3,
                            "type_stats": {"max_hp": 20, "move_rate": 3},
                            "activity": {"name": "idle"},
                            "route": None,
                        }],
                        "total_items": 1,
                        "next_cursor": None,
                        "cursor_expires_at": None,
                    },
                }
                validators = {
                    "health": lambda body: client._validate_health(
                        body, session,
                    ),
                    "legal": lambda body: client._validate_page(
                        body, session, legal=True,
                    ),
                    "state": lambda body: client._validate_page(
                        body, session, legal=False,
                    ),
                    "receipt": lambda body: client._validate_receipt(
                        body, session, batch_id=batch_id,
                    ),
                }
                for name, command, payload, args in (
                    (
                        "health", client.command_health, health,
                        {"session": str(session_path)},
                    ),
                    (
                        "legal", client.command_legal, page,
                        {
                            "session": str(session_path), "actor_id": "",
                            "target_id": "", "limit": None, "cursor": "",
                            "kind": "", "all_pages": False, "offset": "",
                        },
                    ),
                    (
                        "state", client.command_state, state_page,
                        {
                            "session": str(session_path), "section": "units",
                            "actor_id": "", "relation_id": "", "center_id": "",
                            "radius": None, "limit": None, "cursor": "",
                        },
                    ),
                    (
                        "receipt", client.command_receipt, receipt,
                        {"session": str(session_path), "batch_id": batch_id},
                    ),
                ):
                    with self.subTest(command=name):
                        expected = json.dumps(
                            validators[name](payload),
                            sort_keys=True, separators=(",", ":"),
                        ) + "\n"
                        text = io.StringIO()
                        with patch.object(
                            client, "_v2_response",
                            return_value=client.JSONResponse(200, payload),
                        ), redirect_stdout(text):
                            self.assertEqual(
                                command(type("Args", (), dict(args))()), 0,
                            )
                        rendered = io.StringIO()
                        with patch.object(
                            client, "_v2_response",
                            return_value=client.JSONResponse(200, payload),
                        ), redirect_stdout(rendered):
                            self.assertEqual(
                                command(type("Args", (), {
                                    **args, "json_output": True,
                                })()),
                                0,
                            )
                        self.assertEqual(rendered.getvalue(), expected)
                        self.assertNotEqual(text.getvalue(), expected)
                        self.assertFalse(
                            text.getvalue().startswith("{"), text.getvalue(),
                        )

                parsed = client.parser()
                for command_line in (
                    ["health", "--session", "s", "--json"],
                    ["turn", "--session", "s", "--json"],
                    ["state", "--session", "s", "--json"],
                    ["legal", "--session", "s", "--json"],
                    ["batch", "--session", "s", "--action-id", "a", "--json"],
                    ["receipt", "--session", "s", "--batch-id", "b", "--json"],
                    ["retry", "--session", "s", "--batch-id", "b", "--json"],
                    ["join", "--game-id", "g", "--name", "n", "--json"],
                ):
                    with self.subTest(command=command_line[0]):
                        self.assertTrue(
                            parsed.parse_args(command_line).json_output,
                        )
                    without = [
                        item for item in command_line if item != "--json"
                    ]
                    self.assertFalse(parsed.parse_args(without).json_output)

                # No command may fall between the two stools: it either has a
                # `--json` flag or its output is JSON unconditionally.  A new
                # subcommand that forgets both would ship a text-only surface
                # with no escape hatch at all.
                subcommands = next(
                    action for action in parsed._actions
                    if isinstance(action, argparse._SubParsersAction)
                ).choices
                for name, command in subcommands.items():
                    if name == "prompt":
                        continue  # not a protocol command; prints one prompt
                    flags = {
                        string for action in command._actions
                        for string in action.option_strings
                    }
                    with self.subTest(command=name):
                        self.assertTrue(
                            "--json" in flags
                            or name in client.V2_JSON_ONLY_COMMANDS,
                            f"just {name} has no JSON escape hatch",
                        )
                        self.assertFalse(
                            "--json" in flags
                            and name in client.V2_JSON_ONLY_COMMANDS,
                            f"just {name} is both flagged and JSON-only",
                        )

    def test_v2_json_escape_hatch_covers_turn_batch_retry_and_wait(self):
        """The commands the first loop only truthiness-checked, round-tripped.

        `turn`, `batch` and `retry` compose their payload client-side, so the
        invariant is not "equals one wire body" but "prints exactly the one
        canonical JSON object, with the text form a separate projection".
        `wait` declares no `--json` at all, so its refusals must stay JSON.
        """
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                descriptor = self.descriptor(revision)
                batch_id = "batch_" + "J" * 24
                applied = self.receipt(session, batch_id)
                health = self.health(session, active=True)

                def turn_responses():
                    pages = []
                    for section in client.V2_TURN_SECTIONS:
                        items = [] if section != "overview" else [{
                            "turn": 3,
                            "player": {
                                "government": "Despotism",
                                "economy": {"gold": 25},
                            },
                            "research": {
                                "target": "Bronze Working",
                                "bulbs_researched": 0, "cost": 28,
                            },
                        }]
                        page = self.page(
                            session, legal=False, revision=revision,
                            items=items,
                        )
                        page["page"]["section"] = section
                        page["page"]["total_items"] = len(items)
                        pages.append(client.JSONResponse(200, page))
                    return [
                        client.JSONResponse(200, health), *pages,
                        client.JSONResponse(200, health),
                    ]

                def seed_action():
                    client._save_v2_client_state(
                        session_path, client._empty_v2_client_state(session),
                    )
                    state = client._empty_v2_client_state(session)
                    client._remember_page(
                        session_path, state,
                        self.page(
                            session, legal=True, revision=revision,
                            items=[descriptor],
                        ),
                        legal=True,
                    )

                def seed_batch():
                    state = client._load_v2_client_state(session_path, session)
                    state["batches"][batch_id] = json.dumps(
                        {"batch_id": batch_id},
                        sort_keys=True, separators=(",", ":"),
                    )
                    client._save_v2_client_state(session_path, state)

                def run(command, args, *, json_output, responses, setup=None):
                    if setup is not None:
                        setup()
                    stdout = io.StringIO()
                    patched = (
                        patch.object(
                            client, "_v2_response", side_effect=responses(),
                        )
                        if callable(responses)
                        else patch.object(
                            client, "_v2_response", return_value=responses,
                        )
                    )
                    with patched, patch.object(
                        client.secrets, "token_urlsafe", return_value="J" * 24,
                    ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                        self.assertIn(
                            command(type("Args", (), {
                                **args, "json_output": json_output,
                            })()),
                            (0, 2),
                        )
                    return stdout.getvalue()

                cases = (
                    (
                        "turn", client.command_turn,
                        {"session": str(session_path)},
                        turn_responses, seed_action,
                    ),
                    (
                        "batch", client.command_batch,
                        {
                            "session": str(session_path),
                            "action_id": descriptor["action_id"],
                            "arguments": '{"ready":true}',
                        },
                        client.JSONResponse(200, applied), seed_action,
                    ),
                    (
                        "retry", client.command_retry,
                        {"session": str(session_path), "batch_id": batch_id},
                        client.JSONResponse(200, applied), seed_batch,
                    ),
                )
                for name, command, args, responses, setup in cases:
                    with self.subTest(command=name):
                        raw = run(
                            command, args, json_output=True,
                            responses=responses, setup=setup,
                        )
                        parsed = json.loads(raw)
                        self.assertIsInstance(parsed, dict)
                        # Exactly one canonical object: no pretty-printing, no
                        # second line, nothing a machine consumer must strip.
                        self.assertEqual(
                            raw,
                            json.dumps(
                                parsed, sort_keys=True,
                                separators=(",", ":"),
                            ) + "\n",
                        )
                        text = run(
                            command, args, json_output=False,
                            responses=responses, setup=setup,
                        )
                        self.assertNotEqual(text, raw)
                        self.assertFalse(text.startswith("{"), text)

                # `wait` now honours the same contract as every other v2
                # command: compact text by default, the wire payload behind
                # `--json`.
                payload = self.error(code="conflict")
                stdout = io.StringIO()
                with patch.object(
                    client, "_wait_value",
                    side_effect=client.V2ResponseError(409, payload),
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(
                        client.main([
                            "wait", "--session", str(session_path), "--json",
                        ]),
                        2,
                    )
                self.assertEqual(json.loads(stdout.getvalue()), payload)
                self.assertEqual(
                    stdout.getvalue(),
                    json.dumps(
                        payload, sort_keys=True, separators=(",", ":"),
                    ) + "\n",
                )
                # A command that *does* declare `--json` still renders its
                # refusal compactly without the flag.
                compact = io.StringIO()
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(409, payload),
                ), redirect_stdout(compact), redirect_stderr(io.StringIO()):
                    self.assertEqual(
                        client.main(["health", "--session", str(session_path)]),
                        2,
                    )
                self.assertFalse(compact.getvalue().startswith("{"))

                # PLAY_JSON=1 is `--json` for a consumer that owns the
                # environment but not the argument vector.
                seed_action()
                health_args = {"session": str(session_path)}
                flagged = run(
                    client.command_health, health_args, json_output=True,
                    responses=client.JSONResponse(200, health),
                )
                for value, expected in (
                    ("1", flagged), ("TRUE", flagged), ("yes", flagged),
                    ("", None), ("0", None), ("maybe", None),
                ):
                    with self.subTest(play_json=value), patch.dict(
                        os.environ, {"PLAY_JSON": value}, clear=False,
                    ):
                        printed = run(
                            client.command_health, health_args,
                            json_output=False,
                            responses=client.JSONResponse(200, health),
                        )
                        if expected is None:
                            self.assertFalse(printed.startswith("{"), printed)
                        else:
                            self.assertEqual(printed, expected)

    def test_v2_state_sections_render_aligned_tables_and_fog(self):
        revision = self.revision(9)
        tile = "tile_" + "b" * 32

        def state_page(section, items):
            return {
                "state_revision": revision,
                "page": {
                    "section": section, "items": items,
                    "total_items": len(items), "next_cursor": None,
                    "cursor_expires_at": None,
                },
            }

        units = [
            {
                "id": "unit_" + "1" * 32, "scope": "own", "type": "Settlers",
                "tile_id": tile, "x": 31, "y": 72, "hp": 20, "moves": 3,
                "type_stats": {"max_hp": 20, "move_rate": 3},
                "activity": {"name": "idle"},
                "automation": {"controller": "player", "has_orders": False},
                "route": None,
            },
            {
                "id": "unit_" + "2" * 32, "scope": "own", "type": "Workers",
                "tile_id": tile, "x": 31, "y": 72, "hp": 10, "moves": 1,
                "type_stats": {"max_hp": 10, "move_rate": 3},
                "activity": {"name": "irrigate"},
                "automation": {"controller": "ai", "has_orders": True},
                "route": {
                    "mode": "goto", "order_count": 4,
                    "destination": {"tile_id": tile, "x": 33, "y": 70},
                },
            },
        ]
        lines = client._render_state_page(state_page("units", units))
        self.assertEqual(lines[0], "rev9/t3 units 2/2 complete")
        self.assertTrue(lines[1].startswith("u1  Settlers"), lines[1])
        self.assertIn("@31,72 mv3/3 hp20/20 idle", lines[1])
        self.assertTrue(lines[1].endswith("unit_" + "1" * 32))
        self.assertIn("→(33,70) 4st", lines[2])
        self.assertIn("!controller=ai", lines[2])
        self.assertEqual(
            lines[1].index("unit_" + "1" * 32),
            lines[2].index("unit_" + "2" * 32),
            "unit rows must stay column aligned",
        )

        cities = [{
            "id": "city_" + "1" * 32, "name": "London", "x": 31, "y": 72,
            "size": 1, "surplus": {"food": 2, "shields": 1, "trade": 0},
            "production": {
                "kind": "unit", "name": "Warriors", "shield_stock": 0,
                "shield_cost": 10,
            },
        }]
        city_lines = client._render_state_page(state_page("cities", cities))
        self.assertIn("c1  London", city_lines[1])
        self.assertIn("sz1 Warriors 0/10 f+2 s+1 t+0", city_lines[1])

        tiles = []
        for y in (71, 72):
            for x in (30, 31):
                item = {
                    "id": f"tile_{x}{y}" + "0" * 28, "x": x, "y": y,
                    "visibility": "unknown" if (x, y) == (30, 71) else "visible",
                }
                if (x, y) != (30, 71):
                    item["terrain"] = "Ocean" if x == 30 else "Desert"
                    item["owner_player_id"] = None
                    item["infrastructure_placement"] = None
                tiles.append(item)
        grid = client._render_state_page(state_page("tile_window", tiles))
        self.assertEqual(grid[1].split(), ["y\\x", "30", "31"])
        self.assertEqual(grid[2].split(), ["71", "?", "De"])
        self.assertEqual(grid[3].split(), ["72", "Oc", "De"])
        self.assertIn("?=unknown/fogged", grid[4])
        self.assertIn("Oc=Ocean", grid[4])

        research = [{
            "id": "tech_1", "name": "Alphabet", "state": "researchable",
            "can_target": True, "can_goal": True, "path_cost": 1,
            "unknown_prerequisite_count": 0,
        }]
        research_lines = client._render_state_page(
            state_page("research", research),
        )
        self.assertIn("Alphabet", research_lines[1])
        self.assertIn("researchable", research_lines[1])
        self.assertIn("targetable", research_lines[1])

        # An unmodelled section still renders every field it carries.
        chat = client._render_state_page(state_page("chat", [
            {"id": "chat_1", "sender": "Ada", "text": "hi"},
            {"id": "chat_2", "sender": "Bob", "text": "hello"},
        ]))
        self.assertEqual(chat[1].split(), ["#", "id", "sender", "text"])
        self.assertIn("chat_1", chat[2])
        self.assertEqual(
            client._render_state_page(state_page("units", []))[1],
            "(no units items on this page)",
        )

    def test_v2_renderers_fail_closed_on_contract_drift(self):
        revision = self.revision(9)
        page = {
            "state_revision": revision,
            "page": {
                "section": "tile_window",
                "items": [{
                    "id": "tile_" + "a" * 32, "x": 31, "y": "seventy-two",
                    "visibility": "visible", "terrain": "Desert",
                }],
                "total_items": 1, "next_cursor": None,
                "cursor_expires_at": None,
            },
        }
        with self.assertRaisesRegex(client.PlayerError, "--json"):
            client._render_state_page(page)
        page["page"]["items"] = [{
            "id": "tile_" + "a" * 32, "x": 31, "y": 72,
            "visibility": "visible", "terrain": 7,
        }]
        with self.assertRaisesRegex(client.PlayerError, "tile terrain"):
            client._render_state_page(page)

    def test_v2_turn_briefing_groups_units_and_names_the_decision(self):
        revision = self.revision(8, turn=1)
        tile = "tile_" + "b" * 32
        units = [
            {
                "id": f"unit_{index}" + "0" * 30, "scope": "own",
                "type": "Settlers", "tile_id": tile, "x": 31, "y": 72,
                "hp": 20, "moves": 3,
                "type_stats": {"max_hp": 20, "move_rate": 3},
                "activity": {"name": "idle"},
                "automation": {"controller": "player", "has_orders": False},
                "route": None,
            }
            for index in (1, 2)
        ]
        units.append({
            "id": "unit_3" + "0" * 30, "scope": "own", "type": "Explorer",
            "tile_id": tile, "x": 31, "y": 72, "hp": 10, "moves": 9,
            "type_stats": {"max_hp": 10, "move_rate": 9},
            "activity": {"name": "sentry"},
            "automation": {"controller": "player", "has_orders": True},
            "route": None,
        })
        overview = {
            "client_state": "running", "turn": 1, "phase": 0,
            "phase_count": 1,
            "player": {
                "name": "Ada", "nation": "English", "government": "Despotism",
                "economy": {
                    "gold": 50, "tax": 40, "science": 60, "luxury": 0,
                },
            },
            "research": {
                "target": "Bronze Working", "bulbs_researched": 0,
                "cost": 28, "output": 3, "goal": None,
            },
            "counts": {"units": 3, "cities": 0},
        }
        context = {
            "game_state": "running", "objective": "score",
            "max_turns": 5000, "turns_remaining": 4999,
            "agent": {
                "agent_id": self.AGENT_ID,
                "controller_label": "codex-test-model",
            },
            "seat": {
                "place": 1, "seat_id": "place-1",
                "player_name": "AgentPlace1",
            },
            "sidecar": {"state": "ready", "generation": 1},
            "observation_available": True,
            "legal_actions_available": True,
            "phase": {
                "state": "awaiting_agent", "turn": 1, "phase": 0,
                "active": True,
                "timing": {
                    "mode": "default", "timeout_s": 180,
                    "deadline_started_at": 1000.0, "deadline_at": 1180.0,
                    "elapsed_s": 1.0, "remaining_s": 179.0,
                },
            },
            "last_phase_end": None,
        }
        result = {
            "schema_version": 1, "command": "turn", "status": "ready",
            "context": context, "state_revision": revision,
            "overview": overview,
            "cities": {
                "shown": 0, "total": 0, "truncated": False, "items": [],
                "next_cursor": None, "cursor_expires_at": None,
            },
            "units": {
                "shown": 3, "total": 3, "truncated": False, "items": units,
                "next_cursor": None, "cursor_expires_at": None,
            },
            "research": {
                "shown": 16, "total": 88, "truncated": True, "items": [],
                "next_cursor": "cursor_" + "a" * 32,
                "cursor_expires_at": None,
            },
            "next_commands": ["just wait --session S"],
        }

        lines = client._render_turn(result)

        self.assertTrue(lines[0].startswith("T1 rev8/t1 | running |"), lines[0])
        self.assertIn("179s left", lines[0])
        self.assertIn("4999 turns left", lines[0])
        self.assertIn("Despotism gold 50 tax40/lux0/sci60", lines[1])
        self.assertIn("research Bronze Working 0/28 +3/turn", lines[1])
        self.assertIn("  u1,u2 Settlers @31,72 idle mv3/3", lines)
        self.assertIn("  u3 Explorer @31,72 sentry mv9/9", lines)
        decision = [
            line for line in lines if line.startswith("needs decision: ")
        ]
        self.assertEqual(len(decision), 1, lines)
        self.assertTrue(
            decision[0].startswith("needs decision: 2 idle unit(s)"),
            decision[0],
        )
        # A count taken over a truncated page must say so rather than claim
        # to be the empire's authoritative decision list.
        self.assertIn("shown page only", decision[0])
        self.assertTrue(any(
            line.startswith("research page 16/88 (truncated)")
            for line in lines
        ))
        # A truncated section is never a dead end: the briefing prints the
        # continuation the `--json` `next_commands` block already carried.
        self.assertTrue(
            any(
                line.startswith("next: ") and "cursor_" in line
                for line in lines
            ),
            lines,
        )

        # A terrain ring renders only when tile data is present.
        with_tiles = client._render_turn(result, tiles={"items": [{
            "id": "tile_" + "a" * 32, "x": 31, "y": 72,
            "visibility": "visible", "terrain": "Desert",
        }]})
        self.assertIn("terrain", with_tiles)

        waiting = client._render_turn({
            "schema_version": 1, "command": "turn", "status": "not_ready",
            "context": context, "next_commands": ["just wait --session S"],
        })
        self.assertTrue(waiting[0].startswith("turn not_ready | running"))
        self.assertEqual(waiting[-1], "next: just wait --session S")

    def test_v2_receipts_render_one_line_with_the_batch_id_last(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                descriptor = self.descriptor(revision)
                state = client._empty_v2_client_state(session)
                client._remember_page(
                    session_path, state,
                    self.page(
                        session, legal=True, revision=revision,
                        items=[descriptor],
                    ),
                    legal=True,
                )
                batch_id = "batch_" + "R" * 24
                args = type("Args", (), {
                    "session": str(session_path),
                    "action_id": descriptor["action_id"],
                    "arguments": '{"ready":true}',
                })()
                stdout = io.StringIO()
                with patch.object(
                    client.secrets, "token_urlsafe", return_value="R" * 24,
                ), patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(
                        200, self.receipt(session, batch_id),
                    ),
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_batch(args), 0)
                lines = stdout.getvalue().splitlines()
                # One receipt line plus the focus-loop tail.
                self.assertEqual(len(lines), 2)
                self.assertTrue(lines[1].startswith("next: "), lines[1])
                self.assertIn("phase.end End phase", lines[0])
                self.assertIn("{ready=yes}", lines[0])
                self.assertIn("→ applied rev8/t3", lines[0])
                self.assertTrue(lines[0].endswith(batch_id), lines[0])
                self.assertNotIn("schema_version", lines[0])
                self.assertNotIn("receipt_state", lines[0])

                other_id = "batch_" + "S" * 24
                receipt_args = type("Args", (), {
                    "session": str(session_path), "batch_id": other_id,
                })()
                rejected = self.receipt(session, other_id, "rejected")
                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(200, rejected),
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_receipt(receipt_args), 0)
                lines = stdout.getvalue().splitlines()
                self.assertEqual(len(lines), 1)
                self.assertIn(
                    "→ rejected illegal_action: validated test error",
                    lines[0],
                )
                self.assertTrue(lines[0].endswith(other_id))

    def test_v2_health_and_join_render_compact_cards(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                args = type("Args", (), {"session": str(session_path)})()
                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(
                        200, self.health(session, active=True),
                    ),
                ), redirect_stdout(stdout):
                    self.assertEqual(client.command_health(args), 0)
                lines = stdout.getvalue().splitlines()
                self.assertEqual(len(lines), 2)
                self.assertIn("health running", lines[0])
                self.assertIn("YOUR TURN · t3/p1", lines[0])
                self.assertIn("2m59s left of 3m0s", lines[0])
                self.assertIn("sidecar ready gen 1", lines[0])
                self.assertIn("seat 1 AgentPlace1 (codex-test-model)", lines[1])
                self.assertIn("turns 4997/5000 remaining", lines[1])
                self.assertNotIn("state_token", stdout.getvalue())

        card = client._render_join(
            {
                "game_id": self.GAME_ID,
                "controller_label": "codex-test-model",
                "place": 1, "player_name": "AgentPlace1",
                "control_protocol": "full-control-v2",
                "timing_mode": "default", "action_timeout_s": 180,
                "objective": "Maximize final score", "max_turns": 5000,
                "turns_remaining": None,
            },
            {"state": "running", "session_saved": True},
            Path(".sessions/x/codex-test.json"),
        )
        self.assertIn("proto full-control-v2", card[0])
        self.assertIn("timing default 180s per turn", card[0])
        # Join binds the workspace instead of printing a path to re-type.
        self.assertEqual(
            card[1],
            f"this workspace is now playing {self.GAME_ID} — commands need "
            "no --session",
        )
        self.assertNotIn("codex-test.json", "\n".join(card))
        self.assertTrue(any("just turn" in line for line in card))
        self.assertTrue(any("--json" in line for line in card))
        self.assertFalse(any("agent_token" in line for line in card))

    # ------------------------------------------------------------------
    # Client-side aliases (doc §5/P0.3).
    # ------------------------------------------------------------------

    @classmethod
    def section_page(
        cls, session: dict, *, section: str, revision: dict,
        items: list, cursor: str | None = None,
    ) -> dict:
        return {
            "schema_version": 2,
            "control_protocol": "full-control-v2",
            "game_id": session["game_id"],
            "agent_id": session["agent_id"],
            "state_revision": revision,
            "page": {
                "section": section,
                "items": items,
                "total_items": len(items) + (1 if cursor else 0),
                "next_cursor": cursor,
            },
        }

    @classmethod
    def scoped_legal_page(
        cls, session: dict, *, revision: dict, items: list, actor_id: str,
        catalog: str | None = None, cursor: str | None = None,
    ) -> dict:
        page = cls.page(
            session, legal=True, revision=revision, items=items, cursor=cursor,
        )
        page["page"].update({
            "cursor_expires_at": (
                "2999-01-01T00:00:00.000Z" if cursor else None
            ),
            "scope": {
                "actor_id": actor_id, "actor_type": actor_id.split("_", 1)[0],
            },
            "catalog_id": catalog or "catalog_" + "e" * 32,
            "catalog_complete": cursor is None,
        })
        return page

    @staticmethod
    def unit_item(identifier: str, tile: str, x: int, y: int) -> dict:
        return {
            "id": identifier, "scope": "own", "type": "Settlers",
            "tile_id": tile, "x": x, "y": y, "hp": 20, "moves": 3,
            "type_stats": {"max_hp": 20, "move_rate": 3},
            "activity": {"name": "idle"},
            "automation": {"controller": "player", "has_orders": False},
            "route": None,
        }

    @staticmethod
    def alias_args(**values):
        defaults = {
            "session": "", "section": "", "actor_id": "", "target_id": "",
            "relation_id": "", "center_id": "", "radius": None,
            "limit": None, "cursor": "", "kind": "", "all_pages": False,
            "offset": "", "action_id": "", "arguments": "{}",
        }
        defaults.update(values)
        return type("Args", (), defaults)()

    def test_v2_aliases_are_assigned_once_in_first_seen_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                first = self.revision(7)
                second = self.revision(9)
                tiles = ["tile_" + character * 32 for character in "abc"]
                units = ["unit_" + character * 32 for character in "abc"]
                city = "city_" + "d" * 32
                pages = [
                    self.section_page(
                        session, section="units", revision=first,
                        items=[
                            self.unit_item(units[0], tiles[0], 31, 72),
                            self.unit_item(units[1], tiles[1], 30, 72),
                        ],
                        cursor="cursor_" + "1" * 32,
                    ),
                    self.section_page(
                        session, section="units", revision=first,
                        items=[self.unit_item(units[2], tiles[2], 29, 72)],
                    ),
                    self.section_page(
                        session, section="cities", revision=second, items=[{
                            "id": city, "name": "London", "x": 31, "y": 72,
                            "size": 1, "tile_id": tiles[0],
                            "surplus": {"food": 2},
                            "production": {"kind": "unit", "name": "Warriors"},
                        }],
                    ),
                ]
                state = client._empty_v2_client_state(session)
                for page in pages:
                    client._remember_page(
                        session_path, state,
                        client._validate_page(page, session, legal=False),
                        legal=False,
                    )
                # First-seen order, and the second page continues the count
                # instead of restarting at u1.
                self.assertEqual(state["entity_aliases"], {
                    "u1": units[0], "u2": units[1], "u3": units[2],
                    "c1": city,
                })
                self.assertEqual(state["tile_aliases"], {
                    "31,72": tiles[0], "30,72": tiles[1], "29,72": tiles[2],
                })
                # Entity aliases are game-stable: the revision bump that wiped
                # the action cache left them untouched.
                self.assertEqual(state["last_revision"], second)
                self.assertEqual(state["actions"], {})

                # Re-reading the same units at the newer revision re-points
                # nothing and invents nothing.
                client._remember_page(
                    session_path, state,
                    client._validate_page(self.section_page(
                        session, section="units", revision=second,
                        items=[self.unit_item(units[1], tiles[1], 30, 72)],
                    ), session, legal=False),
                    legal=False,
                )
                self.assertEqual(state["entity_aliases"]["u2"], units[1])
                self.assertEqual(len(state["entity_aliases"]), 4)

                # The rendered rows carry the durable alias, so page two of a
                # unit catalog never prints a u1 that means something else.
                rendered = client._render_state_page(
                    client._validate_page(pages[1], session, legal=False),
                    client._alias_map(state),
                )
                self.assertTrue(rendered[1].startswith("u3 "), rendered[1])

    def test_v2_action_aliases_die_with_their_revision(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                first = self.revision(7)
                second = self.revision(9)
                actor = "unit_" + "a" * 32
                old_one = self.descriptor(first, "action_" + "1" * 32)
                old_two = self.descriptor(first, "action_" + "2" * 32)
                new_one = self.descriptor(second, "action_" + "9" * 32)
                state = client._empty_v2_client_state(session)
                client._remember_page(
                    session_path, state,
                    client._validate_page(self.scoped_legal_page(
                        session, revision=first, items=[old_one, old_two],
                        actor_id=actor,
                    ), session, legal=True),
                    legal=True,
                )
                self.assertEqual(
                    {
                        alias: entry["action_id"]
                        for alias, entry in
                        state["action_aliases"]["by_alias"].items()
                    },
                    {
                        "a1": old_one["action_id"],
                        "a2": old_two["action_id"],
                    },
                )
                self.assertEqual(
                    client._expand_alias(state, "a2", session_path),
                    old_two["action_id"],
                )

                # The agent's own action bumps the revision.  The alias bucket
                # still names the revision it came from, so a1 fails closed
                # instead of silently re-pointing.
                client._remember_page(
                    session_path, state,
                    client._validate_page(self.section_page(
                        session, section="overview", revision=second, items=[],
                    ), session, legal=False),
                    legal=False,
                )
                self.assertEqual(client._fresh_action_aliases(state), {})
                with self.assertRaises(client.PlayerError) as refusal:
                    client._expand_alias(state, "a1", session_path)
                message = str(refusal.exception)
                self.assertIn("rev7/t3", message)
                self.assertIn("rev9/t3", message)
                # The remedy is bare: this workspace resolves its sole session
                # by itself, so no 122-char path is re-typed to run it.
                self.assertIn("`just legal --actor_id ", message)
                self.assertIn(" --all`", message)
                self.assertNotIn("--session", message)

                # Only re-enumeration may re-use the number, and then it names
                # the freshly proved capability.
                client._remember_page(
                    session_path, state,
                    client._validate_page(self.scoped_legal_page(
                        session, revision=second, items=[new_one],
                        actor_id=actor, catalog="catalog_" + "f" * 32,
                    ), session, legal=True),
                    legal=True,
                )
                self.assertEqual(
                    client._expand_alias(state, "a1", session_path),
                    new_one["action_id"],
                )
                with self.assertRaisesRegex(
                    client.PlayerError, "unknown action alias a2",
                ):
                    client._expand_alias(state, "a2", session_path)
                reloaded = client._load_v2_client_state(session_path, session)
                self.assertEqual(
                    reloaded["action_aliases"]["state_revision"], second,
                )

    def test_v2_multi_page_catalog_mirrors_every_row_with_its_alias(self):
        """A drained catalog is projected whole, not page by page.

        Aliases are assigned only when the final page promotes the whole
        accumulation, so a page mirrored while the catalog was still staged
        carries `-` in the alias column.  With `MAX_PAGE_ITEMS` well under a
        real unit's menu, that is nearly every catalog: the mirror must be
        re-projected from the promoted catalog, or `just show u1` presents the
        majority of the menu as unaddressable.
        """
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                actor = "unit_" + "a" * 32
                labels = ("Alpha", "Bravo", "Charlie", "Delta")
                items = [
                    self.actor_action(
                        revision, "action_" + str(index) * 26, actor,
                        label=label, x=30 + index, y=72,
                    )
                    for index, label in enumerate(labels, start=1)
                ]
                cursor = "cursor_" + "c" * 32
                catalog = "catalog_" + "a" * 32
                pages = [
                    client.JSONResponse(200, self.scoped_legal_page(
                        session, revision=revision, items=items[:2],
                        actor_id=actor, catalog=catalog, cursor=cursor,
                    )),
                    client.JSONResponse(200, self.scoped_legal_page(
                        session, revision=revision, items=items[2:],
                        actor_id=actor, catalog=catalog,
                    )),
                ]
                # `total_items` is the catalog's size, not the page's.
                for page in pages:
                    page.value["page"]["total_items"] = len(items)
                with patch.object(
                    client, "_v2_response", side_effect=pages,
                ), redirect_stdout(io.StringIO()):
                    self.assertEqual(client.command_legal(self.alias_args(
                        session=str(session_path), actor_id=actor,
                        all_pages=True,
                    )), 0)

                projection = (
                    client._mirror_path(session_path)
                    / "state" / "options" / "u1.txt"
                ).read_text(encoding="utf-8")
                rows = [
                    line for line in projection.splitlines()
                    if line and not line.startswith("#")
                ]
                header, *body = rows
                self.assertTrue(header.startswith("alias"), header)
                self.assertEqual(len(body), len(labels))
                aliases = [line.split("\t")[0].strip() for line in body]
                self.assertNotIn("-", aliases, projection)
                self.assertEqual(sorted(aliases), ["a1", "a2", "a3", "a4"])
                for label in labels:
                    self.assertIn(label, projection)
                self.assertIn("actions 4/4 complete", projection)
                self.assertNotIn("no action alias resolves", projection)
                # Every alias the projection advertises really executes.
                state = client._load_v2_client_state(session_path, session)
                for alias in aliases:
                    self.assertIn(
                        client._expand_alias(state, alias, session_path),
                        {item["action_id"] for item in items},
                    )

    def test_v2_a_receipt_retires_the_aliases_it_outdated(self):
        """A receipt advances the revision exactly as a newer page does.

        The usual driver of a revision bump is the agent's own order, and the
        first thing it gets back is a receipt, not a page.  If a receipt did
        not retire the outstanding capabilities, every `aN` the agent still
        held would resolve to an expired handle and be refused by the *server*
        instead of failing closed locally with a runnable remedy.
        """
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                first = self.revision(7)
                actor = "unit_" + "a" * 32
                one = self.descriptor(first, "action_" + "1" * 32)
                two = self.descriptor(first, "action_" + "2" * 32)
                state = client._empty_v2_client_state(session)
                client._remember_page(
                    session_path, state,
                    client._validate_page(self.scoped_legal_page(
                        session, revision=first, items=[one, two],
                        actor_id=actor,
                    ), session, legal=True),
                    legal=True,
                )
                self.assertEqual(
                    set(client._fresh_action_aliases(state)), {"a1", "a2"},
                )
                self.assertIn(actor, state["drained_actors"])

                batch_id = "batch_" + "B" * 24
                receipt = client._validate_receipt(
                    self.receipt(session, batch_id, "applied"), session,
                    batch_id=batch_id,
                )
                # The fixture's applied receipt reports the next revision.
                self.assertEqual(
                    client._revision_order(receipt["state_revision"]),
                    client._revision_order(self.revision(8)),
                )
                client._remember_receipt(session_path, state, receipt)

                self.assertEqual(
                    state["last_revision"], receipt["state_revision"],
                )
                self.assertEqual(state["actions"], {})
                self.assertEqual(state["pending_catalogs"], {})
                self.assertEqual(state["drained_actors"], [])
                self.assertEqual(client._fresh_action_aliases(state), {})
                with self.assertRaises(client.PlayerError) as refusal:
                    client._expand_alias(state, "a1", session_path)
                message = str(refusal.exception)
                self.assertIn("rev7/t3", message)
                self.assertIn("rev8/t3", message)
                self.assertIn("`just legal --actor_id ", message)
                self.assertIn(" --all`", message)
                # The refusal survives a reload: it is persisted, not in-memory.
                reloaded = client._load_v2_client_state(session_path, session)
                self.assertEqual(client._fresh_action_aliases(reloaded), {})
                self.assertEqual(reloaded["actions"], {})

    def test_v2_alias_expansion_never_puts_an_alias_on_the_wire(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                actor = "unit_" + "a" * 32
                tile = "tile_" + "b" * 32
                action = self.descriptor(revision, "action_" + "1" * 32)
                responses = [
                    client.JSONResponse(200, self.section_page(
                        session, section="units", revision=revision,
                        items=[self.unit_item(actor, tile, 31, 72)],
                    )),
                    client.JSONResponse(200, self.scoped_legal_page(
                        session, revision=revision, items=[action],
                        actor_id=actor,
                    )),
                    client.JSONResponse(200, self.section_page(
                        session, section="tile_window", revision=revision,
                        items=[{
                            "id": tile, "x": 31, "y": 72,
                            "visibility": "visible", "terrain": "Desert",
                            "owner_player_id": None,
                            "infrastructure_placement": None,
                        }],
                    )),
                ]
                sent: list[tuple] = []

                def record(method, url, current, **options):
                    sent.append((url, options.get("encoded_body")))
                    return responses[len(sent) - 1]

                out = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=record,
                ), redirect_stdout(out):
                    self.assertEqual(client.command_state(self.alias_args(
                        session=str(session_path), section="units",
                    )), 0)
                    self.assertEqual(client.command_legal(self.alias_args(
                        session=str(session_path), actor_id="u1",
                    )), 0)
                    self.assertEqual(client.command_state(self.alias_args(
                        session=str(session_path), section="tile_window",
                        center_id="T(31,72)", radius=2,
                    )), 0)
                self.assertIn(f"actor_id={actor}", sent[1][0])
                self.assertIn(f"center_id={tile}", sent[2][0])

                receipt = self.receipt(
                    session, "batch_" + "z" * 24, "applied", revision=revision,
                )

                def submit(method, url, current, **options):
                    sent.append((url, options.get("encoded_body")))
                    body = json.loads(options["encoded_body"])
                    receipt["batch_id"] = body["batch_id"]
                    return client.JSONResponse(200, receipt)

                with patch.object(
                    client, "_v2_response", side_effect=submit,
                ), redirect_stdout(out):
                    self.assertEqual(client.command_batch(self.alias_args(
                        session=str(session_path), action_id="a1",
                    )), 0)
                body = json.loads(sent[-1][1])
                self.assertEqual(
                    body["commands"],
                    [{"action_id": action["action_id"], "arguments": {}}],
                )
                # Nothing the agent typed as an alias reached the wire.
                for url, encoded in sent:
                    payload = url + (
                        "" if encoded is None else encoded.decode("utf-8")
                    )
                    for alias in ("u1", "a1", "T(31,72)"):
                        self.assertNotIn(alias, payload, payload)
                # Once the seat learns a newer revision, the alias it just used
                # is refused before any socket is opened -- `--no-refresh` is
                # the harness form that never spends an extra request.
                cached = client._load_v2_client_state(session_path, session)
                client._remember_page(
                    session_path, cached,
                    client._validate_page(self.section_page(
                        session, section="overview",
                        revision=self.revision(9), items=[],
                    ), session, legal=False),
                    legal=False,
                )
                with patch.object(client, "_v2_response") as blocked:
                    with self.assertRaisesRegex(
                        client.PlayerError, "die with their revision",
                    ):
                        client.command_batch(self.alias_args(
                            session=str(session_path), action_id="a1",
                            no_refresh=True,
                        ))
                    blocked.assert_not_called()

                # The rendered surface offers the aliases the cache can honour.
                printed = out.getvalue()
                self.assertIn("u1  Settlers", printed)
                self.assertIn("scope=unit u1", printed)
                self.assertIn("a1  phase.end", printed)
                # A tile target prints exactly what --target_id accepts.
                self.assertEqual(
                    client._legal_row("a1", client._compact_legal_action(
                        self.rendered_descriptor(
                            revision, "action_" + "3" * 32,
                            kind="unit.goto", label="Move",
                            subject={
                                "operation": "goto",
                                "target": {
                                    "type": "tile", "id": tile,
                                    "x": 31, "y": 72,
                                },
                            },
                        ),
                    ), None)[3],
                    "T(31,72)",
                )

    def test_v2_unknown_alias_names_the_closest_known_ones(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                units = ["unit_" + character * 32 for character in "abc"]
                tiles = ["tile_" + character * 32 for character in "abc"]
                state = client._empty_v2_client_state(session)
                client._remember_page(
                    session_path, state,
                    client._validate_page(self.section_page(
                        session, section="units", revision=revision, items=[
                            self.unit_item(units[0], tiles[0], 31, 72),
                            self.unit_item(units[1], tiles[1], 30, 72),
                            self.unit_item(units[2], tiles[2], 29, 72),
                        ],
                    ), session, legal=False),
                    legal=False,
                )
                with self.assertRaisesRegex(
                    client.PlayerError, r"unknown unit alias u7; known unit "
                    r"aliases: u1 u2 u3",
                ):
                    client._expand_alias(state, "u7", session_path)
                with self.assertRaisesRegex(
                    client.PlayerError, "known city aliases: none are known",
                ):
                    client._expand_alias(state, "c1", session_path)
                with self.assertRaisesRegex(
                    client.PlayerError,
                    r"unknown tile T\(31,99\).*T\(31,72\) T\(30,72\) T\(29,72\)",
                ):
                    client._expand_alias(state, "T(31,99)", session_path)
                with self.assertRaisesRegex(
                    client.PlayerError,
                    "no legal-action catalog has been read yet",
                ):
                    client._expand_alias(state, "a1", session_path)
                # Anything that is not alias-shaped is passed through
                # untouched, so opaque IDs keep working exactly as before.
                self.assertEqual(
                    client._expand_alias(state, units[0], session_path),
                    units[0],
                )
                self.assertFalse(client._looks_like_alias("unit_x"))
                self.assertFalse(client._looks_like_alias("a0"))
                self.assertTrue(client._looks_like_alias("T(-3,4)"))

    def test_v2_alias_tables_fail_closed_on_private_cache_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                unit = "unit_" + "a" * 32
                for broken in (
                    {"entity_aliases": {"u1": "not-an-id"}},
                    {"entity_aliases": {"u1": unit, "u2": unit}},
                    {"entity_aliases": {"x1": unit}},
                    {"tile_aliases": {"31,72": "unit_" + "a" * 32}},
                    {"action_aliases": {
                        "state_revision": None,
                        "by_alias": {"a1": {
                            "action_id": "action_x", "actor_id": "",
                        }},
                    }},
                    {"action_aliases": {
                        "state_revision": self.revision(7),
                        "by_alias": {"a1": "action_x"},
                    }},
                ):
                    state = client._empty_v2_client_state(session)
                    state.update(broken)
                    client._write_private_json(
                        session_path.with_suffix(".v2-state"), state,
                    )
                    with self.assertRaisesRegex(
                        client.PlayerError, "aliases are invalid",
                    ):
                        client._load_v2_client_state(session_path, session)

    def test_service_url_rejects_credentials_and_query(self):
        with self.assertRaises(client.PlayerError):
            client.service_url("http://user:secret@localhost:8765")
        with self.assertRaises(client.PlayerError):
            client.service_url("http://localhost:8765/?token=secret")
        for value in (
            "http://localhost:notaport",
            "http://localhost:99999",
        ):
            with self.subTest(value=value), self.assertRaises(client.PlayerError):
                client.service_url(value)

    def test_bearer_request_rejects_redirect_without_forwarding_token(self):
        captured_authorization = []

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                self.send_response(302)
                self.send_header(
                    "Location",
                    f"http://127.0.0.1:{self.server.server_port}/capture",
                )
                self.send_header("Content-Length", "0")
                self.end_headers()

            def do_GET(self):
                captured_authorization.append(self.headers.get("Authorization"))
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(b"{}")

            def log_message(self, _format, *_args):
                return

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            host, port = server.server_address
            with self.assertRaisesRegex(client.PlayerError, "HTTP 302"):
                client.request_json(
                    "POST", f"http://{host}:{port}/join",
                    token="join-secret", body={}, timeout=2,
                )
            self.assertEqual(captured_authorization, [])
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)

    def test_controller_identity_is_non_generic_harness_model(self):
        self.assertEqual(
            client._controller_name("codex-gpt-5.6-sol"),
            "codex-gpt-5.6-sol",
        )
        for value in ("Agent", "HARNESS-MODEL", "codex", "-codex-gpt"):
            with self.subTest(value=value), self.assertRaises(client.PlayerError):
                client._controller_name(value)

    def test_prompt_names_timing_modes_and_requires_direct_model_choice(self):
        output = io.StringIO()
        args = type("Args", (), {
            "game_id": "game_12345678901234567890",
            "name": "claude-code-claude-opus",
            "place": "",
        })()
        with redirect_stdout(output):
            self.assertEqual(client.command_prompt(args), 0)
        prompt = output.getvalue()
        self.assertIn(
            "default gives each agent 180 seconds\nper turn on strategic-v1 "
            "and 10 minutes on full-control-v2",
            prompt,
        )
        self.assertIn("blitz gives 60 seconds", prompt)
        self.assertIn("infinite has no agent deadline", prompt)
        self.assertIn("choose its action\ndirectly", prompt)
        self.assertIn("Do not write, launch, or delegate", prompt)
        # The bootstrap prompt teaches the bound workspace, not a path to
        # re-type: every command it shows is bare.
        self.assertIn("Join binds this workspace to the seat it joined", prompt)
        self.assertNotIn("--session", prompt)
        self.assertIn("just next --after_turn LAST_TURN", prompt)
        self.assertIn("Advance LAST_TURN only after", prompt)
        self.assertIn("Keep this same conversation active", prompt)
        self.assertIn("do not give a final answer", prompt)
        self.assertIn("If a command itself fails", prompt)

    def test_prompt_teaches_one_v2_contract_and_not_the_old_ritual(self):
        # The bootstrap prompt is read before join, so it wins the ordering
        # against the protocol card.  It must therefore point at the card and
        # `just help` rather than teach a second, pre-redesign contract.
        output = io.StringIO()
        args = type("Args", (), {
            "game_id": "game_12345678901234567890",
            "name": "claude-code-claude-opus",
            "place": "",
        })()
        with redirect_stdout(output):
            self.assertEqual(client.command_prompt(args), 0)
        prompt = output.getvalue()
        self.assertIn("the command contract is the protocol card", prompt)
        self.assertIn("just help", prompt)
        self.assertIn("no later command names a\nsession", prompt)
        for retired in (
            "--session",
            "pregame_nations",
            "pregame_styles",
            "pregame_teams",
            "pregame.configure",
            "pregame.set_ready",
            "game_state: lobby",
            "Copy that exact path into every command",
            "diplomacy_clauses",
            "phase.end",
            "docs/gameplay.md",
        ):
            self.assertNotIn(retired, prompt)
        self.assertLess(len(prompt), 2400)

    def test_multiple_harness_sessions_require_explicit_session(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            state = root / ".sessions"
            first = state / "game_12345678901234567890" / "pi-gpt.json"
            second = state / "game_12345678901234567890" / "pi-claude.json"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                client._write_private_json(first, {"agent_token": "first"})
                client._write_private_json(second, {"agent_token": "second"})
                client._set_current_session(second)
                # Unbound and ambiguous: the refusal names the one command
                # that makes this workspace unambiguous.
                with self.assertRaisesRegex(
                    client.PlayerError, r"just use GAME_ID",
                ):
                    client._session_path("")
                self.assertEqual(
                    client._session_path(str(first)), first.resolve(),
                )

    def test_wrong_current_session_act_fails_before_request(self):
        game_id = "game_12345678901234567890"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            state = root / ".sessions" / game_id
            first = state / "pi-gpt.json"
            second = state / "pi-claude.json"
            base = {
                "schema_version": 1,
                "service_url": "http://127.0.0.1:8765",
                "game_id": game_id,
                "place": 1,
                "seat_id": "place-1",
                "controller_label": "pi-gpt-5.6-sol",
            }
            args = type("Args", (), {
                "session": "",
                "turn": 1,
                "observation_id": "obs_first",
                "action": json.dumps({
                    "type": "set_traits",
                    "traits": {
                        "aggressive": 1, "builder": 2,
                        "expansionist": 3, "trader": 4,
                    },
                }),
            })()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                client._write_private_json(first, {
                    **base, "agent_id": "agent-first",
                    "agent_token": "first-secret",
                })
                client._write_private_json(second, {
                    **base, "agent_id": "agent-second",
                    "agent_token": "second-secret", "place": 2,
                    "seat_id": "place-2",
                    "controller_label": "pi-claude-opus",
                })
                client._set_current_session(second)
                with patch.object(client, "request_json") as request:
                    with self.assertRaisesRegex(
                        client.PlayerError, "multiple private sessions",
                    ):
                        client.command_act(args)
                    request.assert_not_called()

                args.session = str(first)
                acknowledgement = {
                    "accepted": True,
                    "game_id": game_id,
                    "agent_id": "agent-first",
                    "place": 1,
                    "seat_id": "place-1",
                    "controller_label": "pi-gpt-5.6-sol",
                    "turn": 1,
                }
                with patch.object(
                    client, "request_json", return_value=acknowledgement,
                ) as request, redirect_stdout(io.StringIO()):
                    self.assertEqual(client.command_act(args), 0)
                self.assertEqual(request.call_args.kwargs["token"], "first-secret")

    def test_act_requires_explicit_accepted_acknowledgement(self):
        game_id = "game_12345678901234567890"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            session = root / ".sessions" / game_id / "agent.json"
            args = type("Args", (), {
                "session": str(session), "turn": 1,
                "observation_id": "obs_one",
                "action": json.dumps({
                    "type": "set_traits",
                    "traits": {
                        "aggressive": 1, "builder": 2,
                        "expansionist": 3, "trader": 4,
                    },
                }),
            })()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                client._write_private_json(session, {
                    "service_url": "http://127.0.0.1:8765",
                    "game_id": game_id, "agent_id": "agent-one",
                    "agent_token": "secret",
                })
                with patch.object(
                    client, "request_json", return_value={"accepted": False},
                ), self.assertRaisesRegex(
                    client.PlayerError, "do not advance LAST_TURN",
                ):
                    client.command_act(args)

    def test_private_json_and_current_pointer_are_mode_0600(self):
        with tempfile.TemporaryDirectory(dir=client.ROOT) as directory:
            state = Path(directory)
            session = state / "game_example" / "agent.json"
            with patch.dict(os.environ, {"PLAY_STATE_DIR": str(state)}):
                client._write_private_json(session, {"secret": "value"})
                client._set_current_session(session)
                resolved = client._session_path("")
            self.assertEqual(resolved, session.resolve())
            self.assertEqual(stat.S_IMODE(session.stat().st_mode), 0o600)
            self.assertEqual(
                stat.S_IMODE((state / "current").stat().st_mode), 0o600,
            )

    def test_private_session_write_rejects_nested_symlink_escape(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            state = root / ".sessions"
            outside = Path(directory) / "outside"
            root.mkdir()
            state.mkdir()
            outside.mkdir()
            game = state / self.GAME_ID
            game.symlink_to(outside, target_is_directory=True)
            destination = game / "escaped-session.json"
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ), self.assertRaisesRegex(
                client.PlayerError, "real directories",
            ):
                client._write_private_json(destination, {"secret": "value"})
            self.assertFalse((outside / destination.name).exists())
            self.assertEqual(list(outside.iterdir()), [])

    def test_invite_is_game_scoped_and_token_is_not_returned_publicly(self):
        with tempfile.TemporaryDirectory(
            dir=client.ROOT / ".invites",
        ) as directory:
            invite = Path(directory) / "invite.json"
            invite.write_text(json.dumps({
                "schema_version": 1,
                "game_id": "game_12345678901234567890",
                "service_url": "http://127.0.0.1:8765",
                "join_token": "join-secret",
            }), encoding="utf-8")
            invite.chmod(0o600)
            args = type("Args", (), {
                "invite": str(invite),
                "game_id": "game_12345678901234567890",
                "join_token": "",
            })()
            token, base = client._invite(args)
            self.assertEqual(token, "join-secret")
            self.assertEqual(base, "http://127.0.0.1:8765")
            args.game_id = "game_09876543210987654321"
            with self.assertRaises(client.PlayerError):
                client._invite(args)

            args.game_id = "game_12345678901234567890"
            invite.write_text(json.dumps({
                "schema_version": 1,
                "service_url": "http://127.0.0.1:8765",
                "join_token": "join-secret",
            }), encoding="utf-8")
            with self.assertRaisesRegex(client.PlayerError, "different game"):
                client._invite(args)

    def test_missing_or_broken_invite_names_owner_recovery_command(self):
        game_id = "game_missinginvite1234567890"
        args = type("Args", (), {
            "invite": "",
            "game_id": game_id,
            "join_token": "",
        })()
        with self.assertRaisesRegex(
            client.PlayerError,
            rf"just invite {game_id}",
        ):
            client._invite(args)

        with tempfile.TemporaryDirectory(
            dir=client.ROOT / ".invites",
        ) as directory:
            invite = Path(directory) / "broken.json"
            invite.write_text("{", encoding="utf-8")
            invite.chmod(0o600)
            args.invite = str(invite)
            with self.assertRaisesRegex(
                client.PlayerError,
                rf"just invite {game_id}",
            ):
                client._invite(args)

    def test_invite_root_symlink_cannot_escape_player_workspace(self):
        game_id = "game_inviteroot123456789012"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            outside = Path(directory) / "outside"
            root.mkdir()
            outside.mkdir()
            (root / ".invites").symlink_to(outside, target_is_directory=True)
            args = type("Args", (), {
                "invite": "",
                "game_id": game_id,
                "join_token": "",
            })()
            with patch.object(client, "ROOT", root), self.assertRaisesRegex(
                client.PlayerError,
                "real directory inside play",
            ):
                client._invite(args)

    def test_explicit_token_ignores_bad_implicit_invite_and_uses_env_url(self):
        game_id = "game_tokenoverride1234567890"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            invites = root / ".invites"
            invites.mkdir(parents=True)
            bad_default = invites / f"{game_id}.json"
            bad_default.write_text("{", encoding="utf-8")
            bad_default.chmod(0o600)
            args = type("Args", (), {
                "invite": "",
                "game_id": game_id,
                "join_token": "",
            })()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {
                    "AGENT_EVAL_JOIN_TOKEN": "explicit-secret",
                    "AGENT_EVAL_SERVICE_URL": "http://127.0.0.1:9999",
                },
                clear=False,
            ):
                token, base = client._invite(args)
            self.assertEqual(token, "explicit-secret")
            self.assertEqual(base, "http://127.0.0.1:9999")

    def test_stale_invite_rejection_names_owner_recovery_command(self):
        game_id = "game_staleinvite12345678901"
        with tempfile.TemporaryDirectory(
            dir=client.ROOT / ".invites",
        ) as directory:
            invite = Path(directory) / "stale.json"
            invite.write_text(json.dumps({
                "schema_version": 1,
                "game_id": game_id,
                "service_url": "http://127.0.0.1:8765",
                "join_token": "stale-secret",
            }), encoding="utf-8")
            invite.chmod(0o600)
            args = type("Args", (), {
                "game_id": game_id,
                "name": "claude-code-claude-opus",
                "place": "",
                "invite": str(invite),
                "join_token": "",
            })()
            with patch.object(
                client,
                "request_json",
                side_effect=[{}, {}, client.PlayerError("HTTP 401: unauthorized")],
            ), self.assertRaisesRegex(
                client.PlayerError,
                rf"just invite {game_id}",
            ):
                client.command_join(args)

    def test_join_reports_and_saves_exact_timing_contract(self):
        game_id = "game_joinmode12345678901234"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            invites = root / ".invites"
            invites.mkdir(parents=True)
            invite = invites / f"{game_id}.json"
            invite.write_text(json.dumps({
                "schema_version": 1,
                "game_id": game_id,
                "service_url": "http://127.0.0.1:8765",
                "join_token": "join-secret",
            }), encoding="utf-8")
            invite.chmod(0o600)
            args = type("Args", (), {
                "game_id": game_id,
                "name": "claude-code-claude-opus",
                "place": "",
                "invite": "",
                "join_token": "",
            })()
            joined = {
                "schema_version": 1,
                "game_id": game_id,
                "agent_id": "agent-test",
                "agent_token": "agent-private-secret",
                "place": 1,
                "seat_id": "place-1",
                "player_name": "AgentPlace1",
                "controller_label": "claude-code-claude-opus",
                "controller_metadata": {},
                "controller_fingerprint": "f" * 64,
                "timing_mode": "infinite",
                "action_timeout_s": None,
            }
            stdout = io.StringIO()
            stderr = io.StringIO()
            with patch.object(client, "ROOT", root), patch.object(
                client,
                "request_json",
                side_effect=[{}, {}, joined],
            ) as request, patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ), redirect_stdout(stdout), redirect_stderr(stderr):
                self.assertEqual(client.command_join(args), 0)
            self.assertNotIn("agent-private-secret", stdout.getvalue())
            self.assertNotIn("agent-private-secret", stderr.getvalue())
            self.assertIn(
                "Joined in infinite timing mode: no agent deadline",
                stderr.getvalue(),
            )
            self.assertIn("choose its action directly", stderr.getvalue())
            session_files = list((root / ".sessions" / game_id).glob("*.json"))
            self.assertEqual(len(session_files), 1)
            session = json.loads(session_files[0].read_text(encoding="utf-8"))
            self.assertEqual(session["timing_mode"], "infinite")
            self.assertIsNone(session["action_timeout_s"])
            self.assertEqual(session["control_protocol"], "strategic-v1")
            self.assertEqual(session["supported_control_protocols"], [])
            self.assertNotIn(
                "supported_control_protocols",
                request.call_args_list[2].kwargs["body"],
            )

    def test_join_rejects_controller_label_different_from_requested_name(self):
        game_id = "game_joinidentity123456789012"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            invites = root / ".invites"
            invites.mkdir(parents=True)
            invite = invites / f"{game_id}.json"
            invite.write_text(json.dumps({
                "schema_version": 1,
                "game_id": game_id,
                "service_url": "http://127.0.0.1:8765",
                "join_token": "join-secret",
            }), encoding="utf-8")
            invite.chmod(0o600)
            args = type("Args", (), {
                "game_id": game_id,
                "name": "codex-requested-model",
                "place": "",
                "invite": "",
                "join_token": "",
            })()
            joined = {
                "game_id": game_id,
                "agent_id": "agent-test",
                "agent_token": "agent-private-secret",
                "place": 1,
                "seat_id": "place-1",
                "player_name": "AgentPlace1",
                "controller_label": "claude-returned-model",
            }
            with patch.object(client, "ROOT", root), patch.object(
                client, "request_json",
                side_effect=[{}, {}, joined],
            ), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ), self.assertRaisesRegex(
                client.PlayerError, "requested harness-model identity",
            ):
                client.command_join(args)
            self.assertFalse((root / ".sessions").exists())

    def test_full_control_join_advertises_capability_and_never_prints_v1_loop(self):
        game_id = "game_fullcontrol123456789012"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            invites = root / ".invites"
            invites.mkdir(parents=True)
            invite = invites / f"{game_id}.json"
            invite.write_text(json.dumps({
                "schema_version": 1,
                "game_id": game_id,
                "service_url": "http://127.0.0.1:8765",
                "join_token": "join-secret",
            }), encoding="utf-8")
            invite.chmod(0o600)
            args = type("Args", (), {
                "game_id": game_id,
                "name": "codex-full-control-model",
                "place": "",
                "invite": "",
                "join_token": "",
            })()
            joined = {
                "game_id": game_id,
                "agent_id": "agent-v2",
                "agent_token": "agent-v2-secret",
                "place": 1,
                "seat_id": "place-1",
                "player_name": "AgentPlace1",
                "controller_label": "codex-full-control-model",
                "controller_metadata": {},
                "controller_fingerprint": "f" * 64,
                "control_protocol": "full-control-v2",
                "supported_control_protocols": ["full-control-v2"],
                "objective": "Win by the configured evaluation objective.",
                "max_turns": 321,
                "turns_remaining": None,
                "v2_transport_available": True,
                "health_url": f"http://127.0.0.1:8765/v2/games/{game_id}/me/health",
                "state_url": f"http://127.0.0.1:8765/v2/games/{game_id}/me/state",
                "legal_actions_url": f"http://127.0.0.1:8765/v2/games/{game_id}/me/legal-actions",
                "batches_url": f"http://127.0.0.1:8765/v2/games/{game_id}/me/batches",
                "receipts_url": f"http://127.0.0.1:8765/v2/games/{game_id}/me/receipts/{{batch_id}}",
                "wait_url": f"http://127.0.0.1:8765/v2/games/{game_id}/me/wait",
                "openapi_url": "http://127.0.0.1:8765/v2/openapi.json",
                "state": "running",
            }
            stderr = io.StringIO()
            with patch.object(client, "ROOT", root), patch.object(
                client, "request_json",
                side_effect=[
                    {}, {"control_protocol": "full-control-v2"}, joined,
                ],
            ) as request, patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ), redirect_stdout(io.StringIO()), redirect_stderr(stderr):
                self.assertEqual(client.command_join(args), 0)
            self.assertEqual(
                request.call_args_list[2].kwargs["body"][
                    "supported_control_protocols"
                ],
                ["full-control-v2"],
            )
            self.assertIn("Do not use strategic", stderr.getvalue())
            self.assertNotIn("just next --session", stderr.getvalue())
            self.assertIn("LOBBY FIRST", stderr.getvalue())
            self.assertIn("pregame_nations", stderr.getvalue())
            self.assertIn("pregame_styles", stderr.getvalue())
            self.assertIn("pregame.configure", stderr.getvalue())
            self.assertIn("pregame.set_ready", stderr.getvalue())
            session = next((root / ".sessions" / game_id).glob("*.json"))
            saved = json.loads(session.read_text(encoding="utf-8"))
            self.assertEqual(saved["control_protocol"], "full-control-v2")
            self.assertEqual(
                saved["objective"],
                "Win by the configured evaluation objective.",
            )
            self.assertEqual(saved["max_turns"], 321)
            self.assertIsNone(saved["turns_remaining"])
            self.assertIn("Objective: Win by", stderr.getvalue())
            self.assertIn("321 maximum", stderr.getvalue())

            next_args = type("Args", (), {
                "session": str(session), "after_turn": 0, "wait_s": 0,
            })()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ), self.assertRaisesRegex(client.PlayerError, "strategic-v1 only"):
                client.command_next(next_args)

    def test_full_control_join_rejects_unplayable_terminal_or_error_result(self):
        game_id = "game_unplayable12345678901234"
        base = "http://127.0.0.1:8765"
        for name, override in (
            ("transport", {"v2_transport_available": False, "state": "starting"}),
            ("terminal", {"v2_transport_available": True, "state": "failed"}),
            (
                "error",
                {
                    "v2_transport_available": True,
                    "state": "running",
                    "error": "sidecar startup failed",
                },
            ),
        ):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory) / "play"
                invites = root / ".invites"
                invites.mkdir(parents=True)
                invite = invites / f"{game_id}.json"
                invite.write_text(json.dumps({
                    "schema_version": 1,
                    "game_id": game_id,
                    "service_url": base,
                    "join_token": "join-secret",
                }), encoding="utf-8")
                invite.chmod(0o600)
                args = type("Args", (), {
                    "game_id": game_id,
                    "name": "codex-full-control-model",
                    "place": "",
                    "invite": "",
                    "join_token": "",
                })()
                prefix = f"{base}/v2/games/{game_id}/me"
                joined = {
                    "game_id": game_id,
                    "agent_id": "agent-v2",
                    "agent_token": "agent-v2-secret",
                    "place": 1,
                    "seat_id": "place-1",
                    "player_name": "AgentPlace1",
                    "controller_label": "codex-full-control-model",
                    "controller_metadata": {},
                    "controller_fingerprint": "f" * 64,
                    "control_protocol": "full-control-v2",
                    "supported_control_protocols": ["full-control-v2"],
                    "health_url": f"{prefix}/health",
                    "state_url": f"{prefix}/state",
                    "legal_actions_url": f"{prefix}/legal-actions",
                    "batches_url": f"{prefix}/batches",
                    "receipts_url": f"{prefix}/receipts/{{batch_id}}",
                    **override,
                }
                with patch.object(client, "ROOT", root), patch.object(
                    client, "request_json",
                    side_effect=[
                        {}, {"control_protocol": "full-control-v2"}, joined,
                    ],
                ), patch.dict(
                    os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
                ), self.assertRaisesRegex(
                    client.PlayerError, "did not become playable",
                ):
                    client.command_join(args)
                self.assertFalse((root / ".sessions").exists())

    # ------------------------------------------------------------------
    # Workspace = seat (redesign doc §5/P1.5).
    #
    # Optional is not ambient: with `--session` merely optional the observed
    # agent still pasted the path on 79 of 82 commands.  Join now *binds* the
    # workspace, so these tests are about one fact — after a join, nothing
    # names a seat again.
    # ------------------------------------------------------------------

    @staticmethod
    def stage_invite(root: Path, game_id: str) -> None:
        invites = root / ".invites"
        invites.mkdir(parents=True, exist_ok=True)
        invite = invites / f"{game_id}.json"
        invite.write_text(json.dumps({
            "schema_version": 1,
            "game_id": game_id,
            "service_url": "http://127.0.0.1:8765",
            "join_token": "join-secret",
        }), encoding="utf-8")
        invite.chmod(0o600)

    def join_once(
        self, root: Path, game_id: str, name: str = "codex-bind-model",
    ) -> tuple[str, str]:
        """Run one complete full-control-v2 join; return (stdout, stderr)."""
        self.stage_invite(root, game_id)
        prefix = f"http://127.0.0.1:8765/v2/games/{game_id}/me"
        joined = {
            "game_id": game_id,
            "agent_id": "agent-v2",
            "agent_token": "agent-v2-secret",
            "place": 1,
            "seat_id": "place-1",
            "player_name": "AgentPlace1",
            "controller_label": name,
            "controller_metadata": {},
            "controller_fingerprint": "f" * 64,
            "control_protocol": "full-control-v2",
            "supported_control_protocols": ["full-control-v2"],
            "objective": "Win by the configured evaluation objective.",
            "max_turns": 321,
            "turns_remaining": None,
            "v2_transport_available": True,
            "health_url": f"{prefix}/health",
            "state_url": f"{prefix}/state",
            "legal_actions_url": f"{prefix}/legal-actions",
            "batches_url": f"{prefix}/batches",
            "receipts_url": f"{prefix}/receipts/{{batch_id}}",
            "wait_url": f"{prefix}/wait",
            "openapi_url": "http://127.0.0.1:8765/v2/openapi.json",
            "state": "running",
        }
        args = type("Args", (), {
            "game_id": game_id, "name": name, "place": "",
            "invite": "", "join_token": "",
        })()
        stdout, stderr = io.StringIO(), io.StringIO()
        with patch.object(
            client, "request_json",
            side_effect=[{}, {"control_protocol": "full-control-v2"}, joined],
        ), redirect_stdout(stdout), redirect_stderr(stderr):
            self.assertEqual(client.command_join(args), 0)
        return stdout.getvalue(), stderr.getvalue()

    def test_join_binds_this_workspace_and_a_second_join_rebinds_it(self):
        first = "game_bindfirst1234567890123"
        second = "game_bindsecond123456789012"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                stdout, stderr = self.join_once(root, first)
                self.assertIn(
                    f"this workspace is now playing {first} — commands need "
                    "no --session",
                    stdout,
                )
                # The path an agent would otherwise re-type is not printed.
                # (The card names `state/phase.json`, a workspace projection
                # with no path to re-type, so the session file is what this
                # actually forbids.)
                self.assertNotIn(".sessions", stdout)
                self.assertNotIn(str(root), stdout)
                self.assertNotIn("Session file:", stderr)

                binding = client._state_root() / client.SEAT_BINDING_NAME
                self.assertEqual(stat.S_IMODE(binding.stat().st_mode), 0o600)
                text = binding.read_text(encoding="utf-8")
                self.assertNotIn("agent-v2-secret", text)
                saved = json.loads(text)
                session = next((root / ".sessions" / first).glob("*.json"))
                self.assertEqual(saved["game_id"], first)
                self.assertEqual(
                    saved["session"], f"{first}/{session.name}",
                )
                self.assertRegex(
                    saved["bound_at"], r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$",
                )
                self.assertEqual(
                    client._session_path(""), session.resolve(),
                )

                # A join for a different game rebinds, and says so once.
                rebound, _stderr = self.join_once(root, second)
                self.assertIn(
                    f"this workspace is now playing {second}, rebound from "
                    f"{first} — commands need no --session",
                    rebound,
                )
                later = next((root / ".sessions" / second).glob("*.json"))
                self.assertEqual(len(client._private_sessions()), 2)
                self.assertEqual(client._session_path(""), later.resolve())

    def test_session_resolution_is_explicit_then_env_then_binding_then_sole(self):
        first = "game_orderfirst123456789012"
        second = "game_ordersecond12345678901"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                self.join_once(root, first)
                self.join_once(root, second)
                bound = next((root / ".sessions" / second).glob("*.json"))
                other = next((root / ".sessions" / first).glob("*.json"))

                # The binding beats the sole-session rule it replaces.
                self.assertEqual(client._session_path(""), bound.resolve())
                # The environment beats the binding.
                with patch.dict(
                    os.environ, {"PLAY_SESSION": str(other)}, clear=False,
                ):
                    self.assertEqual(
                        client._session_path(""), other.resolve(),
                    )
                    # An explicit argument beats the environment.
                    self.assertEqual(
                        client._session_path(str(bound)), bound.resolve(),
                    )

                # Only an *unbound* workspace with two seats is refused, and
                # the refusal names the command that binds it.
                (client._state_root() / client.SEAT_BINDING_NAME).unlink()
                with self.assertRaisesRegex(
                    client.PlayerError, r"`just use GAME_ID`",
                ) as refusal:
                    client._session_path("")
                self.assertIn("none of them is bound", str(refusal.exception))

                # One seat still needs no binding at all.
                other.unlink()
                self.assertEqual(client._session_path(""), bound.resolve())

    def test_a_stale_binding_never_wins_over_the_real_sole_seat(self):
        game_id = "game_stalebinding1234567890"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                self.join_once(root, game_id)
                session = next((root / ".sessions" / game_id).glob("*.json"))
                client._write_private_json(
                    client._state_root() / client.SEAT_BINDING_NAME,
                    {
                        "schema_version": 1,
                        "game_id": "game_deletedseat12345678901",
                        "session": "game_deletedseat12345678901/gone.json",
                        "bound_at": "2026-01-01T00:00:00Z",
                    },
                )
                self.assertEqual(client._session_path(""), session.resolve())

                # A binding this client cannot parse fails closed, naming the
                # command that repairs it.
                client._write_private_text(
                    client._state_root() / client.SEAT_BINDING_NAME,
                    "{\"game_id\": \"not-a-game\"}\n",
                )
                with self.assertRaisesRegex(
                    client.PlayerError, r"`just use GAME_ID`",
                ):
                    client._session_path("")

    def test_use_binds_by_path_or_game_id_and_fails_closed_when_ambiguous(self):
        first = "game_usefirst12345678901234"
        second = "game_usesecond1234567890123"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                self.join_once(root, first, "codex-first-model")
                self.join_once(root, second, "codex-second-model")
                one = next((root / ".sessions" / first).glob("*.json"))

                def use(target: str = "") -> str:
                    args = type("Args", (), {"target": target})()
                    stdout = io.StringIO()
                    with redirect_stdout(stdout):
                        self.assertEqual(client.command_use(args), 0)
                    return stdout.getvalue()

                # By exact path, from the workspace-relative form an operator
                # would actually type.
                relative = one.resolve().relative_to(root.resolve())
                self.assertIn(
                    f"this workspace is now playing {first}, rebound from "
                    f"{second}",
                    use(str(relative)),
                )
                self.assertEqual(client._session_path(""), one.resolve())

                # By game ID, when that game holds exactly one seat.
                self.assertIn(
                    f"this workspace is now playing {second}, rebound from "
                    f"{first}",
                    use(second),
                )

                # With no argument it reports the seat, never a token.
                report = use()
                self.assertIn(f"playing {second}", report)
                self.assertIn("commands need no --session", report)
                self.assertNotIn("agent-v2-secret", report)

                # Two seats in one game: fail closed, listing both commands.
                sibling = one.with_name("codex-sibling-model.json")
                client._write_private_json(
                    sibling, json.loads(one.read_text(encoding="utf-8")),
                )
                with self.assertRaisesRegex(
                    client.PlayerError, r"name the one you are playing",
                ) as ambiguous:
                    client.command_use(type("Args", (), {"target": first})())
                message = str(ambiguous.exception)
                self.assertIn(f"`just use {relative}`", message)
                self.assertIn(
                    f"`just use {sibling.resolve().relative_to(root.resolve())}`",
                    message,
                )
                # The refused rebind left the previous binding untouched.
                self.assertEqual(
                    client._read_seat_binding()["game_id"], second,
                )

                # A game this workspace never joined names the join command.
                missing = "game_neverjoined123456789012"
                with self.assertRaisesRegex(
                    client.PlayerError,
                    rf"just join --game_id {missing}",
                ):
                    client.command_use(type("Args", (), {"target": missing})())

    def test_use_on_an_unbound_workspace_names_join(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ), self.assertRaisesRegex(
                client.PlayerError, r"just join --game_id GAME_ID",
            ):
                client.command_use(type("Args", (), {"target": ""})())

    def test_the_play_shim_is_the_same_cli_as_client_py(self):
        """`./play X` must be `client.py X`, not a second argument surface."""
        shim = client.ROOT / "play"
        self.assertTrue(shim.is_file())
        self.assertTrue(os.access(shim, os.X_OK), "play is not executable")

        completed = subprocess.run(
            (str(shim), "--help"), cwd=client.ROOT,
            check=False, capture_output=True, text=True,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        subcommands = next(
            action for action in client.parser()._actions
            if isinstance(action, argparse._SubParsersAction)
        ).choices
        for name in subcommands:
            self.assertIn(name, completed.stdout)
        self.assertIn("./play", completed.stdout)

        unknown = subprocess.run(
            (str(shim), "definitely-not-a-command"), cwd=client.ROOT,
            check=False, capture_output=True, text=True,
        )
        self.assertEqual(unknown.returncode, 2)

        # A real subcommand reaches the real handler, with the environment
        # the shim was given.
        with tempfile.TemporaryDirectory(dir=client.ROOT) as directory:
            environment = dict(os.environ)
            environment["PLAY_STATE_DIR"] = directory
            environment.pop("PLAY_SESSION", None)
            bound = subprocess.run(
                (str(shim), "use"), cwd=client.ROOT, env=environment,
                check=False, capture_output=True, text=True,
            )
            self.assertEqual(bound.returncode, 2)
            self.assertIn("not bound to a seat", bound.stderr)

    def test_session_and_invite_paths_cannot_escape_workspace(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.dict(os.environ, {"PLAY_STATE_DIR": directory}):
                with self.assertRaises(client.PlayerError):
                    client._state_root()
        args = type("Args", (), {
            "invite": "/tmp/outside-invite.json",
            "game_id": "game_12345678901234567890",
            "join_token": "",
        })()
        with patch.object(Path, "is_file", return_value=True):
            with self.assertRaises(client.PlayerError):
                client._invite(args)

    def test_v2_state_legal_queries_cache_and_invalidate_by_revision(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                first = self.revision(7)
                action = self.descriptor(first)
                responses = [
                    client.JSONResponse(200, self.page(
                        session, legal=False, revision=first,
                    )),
                    client.JSONResponse(200, self.page(
                        session, legal=True, revision=first, items=[action],
                    )),
                    client.JSONResponse(200, self.page(
                        session, legal=False, revision=self.revision(8),
                    )),
                ]
                state_args = type("Args", (), {
                    "session": str(session_path), "section": "overview",
                    "limit": "4", "cursor": "",
                })()
                legal_args = type("Args", (), {
                    "session": str(session_path),
                    "actor_id": "unit_" + "a" * 32,
                    "target_id": "tile_" + "b" * 32, "limit": None,
                    "cursor": "",
                })()
                with patch.object(
                    client, "_v2_response", side_effect=responses,
                ) as request, redirect_stdout(io.StringIO()):
                    self.assertEqual(client.command_state(state_args), 0)
                    self.assertEqual(client.command_legal(legal_args), 0)
                    cached = client._load_v2_client_state(
                        session_path, session,
                    )
                    self.assertIn(action["action_id"], cached["actions"])
                    self.assertEqual(client.command_state(state_args), 0)
                self.assertTrue(
                    request.call_args_list[0].args[1].endswith(
                        "/state?section=overview&limit=4"
                    )
                )
                self.assertTrue(
                    request.call_args_list[1].args[1].endswith(
                        "/legal-actions?actor_id=unit_"
                        + "a" * 32 + "&target_id=tile_" + "b" * 32
                    )
                )
                cached = client._load_v2_client_state(session_path, session)
                self.assertEqual(cached["last_revision"], self.revision(8))
                self.assertEqual(cached["actions"], {})
                state_file = session_path.with_suffix(".v2-state")
                self.assertEqual(stat.S_IMODE(state_file.stat().st_mode), 0o600)
                self.assertNotIn("v2-agent-secret", state_file.read_text())

                bad_state = type("Args", (), {
                    "cursor": "cursor_" + "a" * 32,
                    "section": "cities", "limit": None,
                })()
                with self.assertRaises(client.PlayerError):
                    client._state_query(bad_state)
                bad_legal = type("Args", (), {
                    "cursor": "", "actor_id": "", "target_id": "tile_x",
                    "limit": None,
                })()
                with self.assertRaises(client.PlayerError):
                    client._legal_query(bad_legal)

    def test_v2_state_scoped_query_construction_is_strict(self):
        city_id = "city_" + "a" * 32
        unit_id = "unit_" + "e" * 32
        tile_id = "tile_" + "b" * 32

        def args(**values):
            defaults = {
                "cursor": "", "section": "", "actor_id": "",
                "relation_id": "", "center_id": "", "radius": None,
                "limit": None,
            }
            defaults.update(values)
            return type("Args", (), defaults)()

        self.assertEqual(client._state_query(args(
            section="city_citizens", actor_id=city_id, limit="3",
        )), (
            "section=city_citizens&limit=3&actor_id=" + city_id
        ))
        self.assertEqual(client._state_query(args(
            section="tile_window", center_id=tile_id, radius=4,
        )), (
            "section=tile_window&limit=16&center_id=" + tile_id + "&radius=4"
        ))
        self.assertEqual(client._state_query(args(
            section="unit_route", actor_id=unit_id, limit="5",
        )), (
            "section=unit_route&limit=5&actor_id=" + unit_id
        ))
        self.assertEqual(
            client._state_query(args(section="pregame_nations")),
            "section=pregame_nations&limit=16",
        )
        self.assertEqual(
            client._state_query(args(section="pregame_styles", limit="4")),
            "section=pregame_styles&limit=4",
        )
        relation_id = "relation_" + "d" * 32
        self.assertEqual(client._state_query(args(
            section="diplomacy_clauses", relation_id=relation_id, limit="2",
        )), (
            "section=diplomacy_clauses&limit=2&relation_id=" + relation_id
        ))
        for invalid in (
            args(section="city_detail"),
            args(section="cities", actor_id=city_id),
            args(section="unit_route"),
            args(section="unit_route", actor_id=city_id),
            args(section="tile_window", center_id=tile_id),
            args(section="tile_window", center_id=tile_id, radius=9),
            args(section="tile_window", actor_id=city_id,
                 center_id=tile_id, radius=1),
            args(section="diplomacy_clauses"),
            args(section="diplomacy_clauses", relation_id="not/opaque"),
            args(section="diplomacy_clauses",
                 relation_id="player_" + "d" * 32),
            args(section="cities", relation_id=relation_id),
            args(cursor="cursor_" + "c" * 32, actor_id=city_id),
        ):
            with self.subTest(invalid=vars(invalid)), self.assertRaises(
                client.PlayerError,
            ):
                client._state_query(invalid)

        parser = client.parser()
        parsed = parser.parse_args([
            "state", "--session", "session.json", "--section",
            "tile_window", "--center-id", tile_id, "--radius", "2",
        ])
        self.assertEqual(parsed.center_id, tile_id)
        self.assertEqual(parsed.radius, 2)
        clauses = parser.parse_args([
            "state", "--session", "session.json", "--section",
            "diplomacy_clauses", "--relation-id", relation_id,
        ])
        self.assertEqual(clauses.relation_id, relation_id)

    def test_v2_relation_scope_and_treaty_clause_section_are_valid(self):
        session = {
            "game_id": self.GAME_ID,
            "agent_id": self.AGENT_ID,
            "control_protocol": "full-control-v2",
        }
        revision = self.revision()
        relation_page = self.page(
            session, legal=True, revision=revision,
            items=[self.descriptor(revision)],
        )
        relation_page["page"]["scope"] = {
            "actor_id": "player_" + "a" * 32,
            "actor_type": "player",
            "target_id": "relation_" + "b" * 32,
            "target_type": "diplomatic_relation",
        }
        clean = client._validate_page(relation_page, session, legal=True)
        self.assertEqual(
            clean["page"]["scope"], relation_page["page"]["scope"],
        )
        args = type("Args", (), {
            "cursor": "", "actor_id": "player_" + "a" * 32,
            "target_id": "relation_" + "b" * 32, "limit": None,
        })()
        self.assertIn("target_id=relation_", client._legal_query(args))
        args.limit = "1"
        with self.assertRaisesRegex(client.PlayerError, "does not accept"):
            client._legal_query(args)
        target_args = type("Args", (), {
            "cursor": "", "actor_id": "unit_" + "a" * 32,
            "target_id": "tile_" + "b" * 32, "limit": "3",
        })()
        self.assertEqual(client._legal_query(target_args), (
            "actor_id=unit_" + "a" * 32
            + "&target_id=tile_" + "b" * 32 + "&limit=3"
        ))

        for actor_type in ("player", "unit", "city"):
            target_page = json.loads(json.dumps(relation_page))
            target_page["page"]["scope"] = {
                "actor_id": actor_type + "_" + "a" * 32,
                "actor_type": actor_type,
                "target_id": "tile_" + "b" * 32,
                "target_type": "tile",
            }
            clean_target = client._validate_page(
                target_page, session, legal=True,
            )
            self.assertEqual(
                clean_target["page"]["scope"],
                target_page["page"]["scope"],
            )

        clauses = self.page(
            session, legal=False, revision=revision, items=[{"type": "gold"}],
        )
        clauses["page"]["section"] = "diplomacy_clauses"
        self.assertEqual(
            client._validate_page(clauses, session, legal=False)["page"]["section"],
            "diplomacy_clauses",
        )
        hostile = json.loads(json.dumps(relation_page))
        hostile["page"]["scope"]["target_type"] = "tile"
        with self.assertRaisesRegex(
            client.PlayerError, "invalid legal-actions page scope",
        ):
            client._validate_page(hostile, session, legal=True)
        wrong_relation_id = json.loads(json.dumps(relation_page))
        wrong_relation_id["page"]["scope"]["target_id"] = (
            "player_" + "b" * 32
        )
        with self.assertRaisesRegex(
            client.PlayerError, "invalid legal-actions page scope",
        ):
            client._validate_page(wrong_relation_id, session, legal=True)
        wrong_relation_actor = json.loads(json.dumps(relation_page))
        wrong_relation_actor["page"]["scope"].update({
            "actor_id": "city_" + "a" * 32,
            "actor_type": "city",
        })
        with self.assertRaisesRegex(
            client.PlayerError, "invalid legal-actions page scope",
        ):
            client._validate_page(wrong_relation_actor, session, legal=True)

    def test_v2_scoped_catalogs_stage_then_promote_atomically(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                for label, scope in (
                    ("actor", {
                        "actor_id": "unit_" + "a" * 32,
                        "actor_type": "unit",
                    }),
                    ("relation", {
                        "actor_id": "player_" + "b" * 32,
                        "actor_type": "player",
                        "target_id": "relation_" + "c" * 32,
                        "target_type": "diplomatic_relation",
                    }),
                    ("target", {
                        "actor_id": "player_" + "b" * 32,
                        "actor_type": "player",
                        "target_id": "tile_" + "c" * 32,
                        "target_type": "tile",
                    }),
                ):
                    with self.subTest(scope=label):
                        session_path, session = self.v2_session(root)
                        state = client._empty_v2_client_state(session)
                        client._save_v2_client_state(session_path, state)
                        revision = self.revision(7)
                        one = self.descriptor(
                            revision, "action_" + label + "_one",
                        )
                        two = self.descriptor(
                            revision, "action_" + label + "_two",
                        )
                        catalog_id = "catalog_" + (
                            "1" if label == "actor" else "2"
                        ) * 32
                        prefix = self.page(
                            session, legal=True, revision=revision,
                            items=[one], cursor="cursor_" + "d" * 32,
                        )
                        prefix["page"].update({
                            "scope": scope,
                            "cursor_expires_at": "2999-01-01T00:00:00.000Z",
                            "catalog_id": catalog_id,
                            "catalog_complete": False,
                        })
                        clean_prefix = client._validate_page(
                            prefix, session, legal=True,
                        )
                        client._remember_page(
                            session_path, state, clean_prefix, legal=True,
                        )
                        self.assertEqual(state["actions"], {})
                        self.assertEqual(
                            set(state["pending_catalogs"][catalog_id]["items"]),
                            {one["action_id"]},
                        )
                        batch_args = type("Args", (), {
                            "session": str(session_path),
                            "action_id": one["action_id"],
                            "arguments": "{}",
                        })()
                        with patch.object(
                            client, "_v2_response",
                        ) as no_request, self.assertRaisesRegex(
                            client.PlayerError, "unknown or expired action ID",
                        ):
                            client.command_batch(batch_args)
                        no_request.assert_not_called()

                        final = self.page(
                            session, legal=True, revision=revision, items=[two],
                        )
                        final["page"].update({
                            "scope": scope,
                            "total_items": 2,
                            "cursor_expires_at": None,
                            "catalog_id": catalog_id,
                            "catalog_complete": True,
                        })
                        clean_final = client._validate_page(
                            final, session, legal=True,
                        )
                        client._remember_page(
                            session_path, state, clean_final, legal=True,
                        )
                        self.assertEqual(
                            set(state["actions"]),
                            {one["action_id"], two["action_id"]},
                        )
                        self.assertEqual(state["pending_catalogs"], {})

    def test_v2_state_migration_drops_ambiguous_legacy_actions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                legacy = {
                    "schema_version": 1,
                    "game_id": session["game_id"],
                    "agent_id": session["agent_id"],
                    "last_revision": self.revision(7),
                    "actions": {
                        "action_legacy": self.descriptor(self.revision(7)),
                    },
                    "batches": {"batch_saved": '{"batch_id":"batch_saved"}'},
                    "receipts": {},
                }
                client._write_private_json(
                    session_path.with_suffix(".v2-state"), legacy,
                )
                migrated = client._load_v2_client_state(session_path, session)
                self.assertEqual(migrated["schema_version"], 5)
                self.assertEqual(migrated["actions"], {})
                self.assertEqual(migrated["drained_actors"], [])
                self.assertEqual(migrated["pending_catalogs"], {})
                self.assertIn("batch_saved", migrated["batches"])
                self.assertEqual(
                    migrated["action_aliases"],
                    {"state_revision": None, "by_alias": {}},
                )
                self.assertEqual(migrated["entity_aliases"], {})
                self.assertEqual(migrated["tile_aliases"], {})
                persisted = json.loads(
                    session_path.with_suffix(".v2-state").read_text(),
                )
                self.assertEqual(persisted, migrated)

                # A v2 cache predates aliases only; every capability it proved
                # survives the upgrade, and numbering restarts empty.
                staged = {
                    "schema_version": 2,
                    "game_id": session["game_id"],
                    "agent_id": session["agent_id"],
                    "last_revision": self.revision(7),
                    "actions": {
                        "action_kept": self.descriptor(
                            self.revision(7), "action_kept",
                        ),
                    },
                    "pending_catalogs": {},
                    "batches": {"batch_saved": '{"batch_id":"batch_saved"}'},
                    "receipts": {},
                }
                client._write_private_json(
                    session_path.with_suffix(".v2-state"), staged,
                )
                upgraded = client._load_v2_client_state(session_path, session)
                self.assertEqual(upgraded["schema_version"], 5)
                self.assertEqual(set(upgraded["actions"]), {"action_kept"})
                self.assertEqual(upgraded["entity_aliases"], {})
                self.assertEqual(upgraded["drained_actors"], [])
                self.assertEqual(
                    upgraded["action_aliases"]["state_revision"], None,
                )

                # A v3 cache predates the drained-catalog record only.  Every
                # alias and every proved action survives; nothing is claimed
                # drained until a catalog is drained again.
                aliased = {
                    "schema_version": 3,
                    "game_id": session["game_id"],
                    "agent_id": session["agent_id"],
                    "last_revision": self.revision(7),
                    "actions": {
                        "action_kept": self.descriptor(
                            self.revision(7), "action_kept",
                        ),
                    },
                    "pending_catalogs": {},
                    "batches": {"batch_saved": '{"batch_id":"batch_saved"}'},
                    "receipts": {},
                    "action_aliases": {
                        "state_revision": self.revision(7),
                        "by_alias": {
                            "a1": {
                                "action_id": "action_kept",
                                "actor_id": "unit_" + "a" * 32,
                            },
                        },
                    },
                    "entity_aliases": {"u1": "unit_" + "a" * 32},
                    "tile_aliases": {"31,72": "tile_" + "b" * 32},
                }
                client._write_private_json(
                    session_path.with_suffix(".v2-state"), aliased,
                )
                recorded = client._load_v2_client_state(session_path, session)
                self.assertEqual(recorded["schema_version"], 5)
                # A v3 alias survives with an empty semantic identity: still
                # resolvable at its own revision, never carried across a bump.
                self.assertEqual(
                    recorded["action_aliases"]["by_alias"]["a1"]["semantics"],
                    "",
                )
                self.assertEqual(set(recorded["actions"]), {"action_kept"})
                self.assertEqual(
                    recorded["entity_aliases"], {"u1": "unit_" + "a" * 32},
                )
                self.assertEqual(
                    set(recorded["action_aliases"]["by_alias"]), {"a1"},
                )
                self.assertEqual(recorded["drained_actors"], [])
                self.assertEqual(
                    json.loads(
                        session_path.with_suffix(".v2-state").read_text(),
                    ),
                    recorded,
                )

    def test_v2_cursor_expiry_error_discards_pending_catalog(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                descriptor = self.descriptor(revision)
                cursor = "cursor_" + "e" * 32
                page = self.page(
                    session, legal=True, revision=revision,
                    items=[descriptor], cursor=cursor,
                )
                page["page"].update({
                    "scope": {
                        "actor_id": "unit_" + "a" * 32,
                        "actor_type": "unit",
                    },
                    "cursor_expires_at": "2999-01-01T00:00:00.000Z",
                    "catalog_id": "catalog_" + "f" * 32,
                    "catalog_complete": False,
                })
                state = client._empty_v2_client_state(session)
                client._remember_page(
                    session_path, state,
                    client._validate_page(page, session, legal=True),
                    legal=True,
                )
                error = self.error(code="cursor_expired", retryable=True)
                error["error"]["details"] = {
                    "restart": {
                        "endpoint": "legal_actions",
                        "query": {"actor_id": "unit_" + "a" * 32},
                    },
                }
                args = type("Args", (), {
                    "session": str(session_path), "cursor": cursor,
                    "actor_id": "", "target_id": "", "limit": None,
                })()
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(410, error),
                ), self.assertRaises(client.V2ResponseError) as expired:
                    client.command_legal(args)
                self.assertEqual(
                    expired.exception.payload["error"]["code"],
                    "cursor_expired",
                )
                reloaded = client._load_v2_client_state(session_path, session)
                self.assertEqual(reloaded["pending_catalogs"], {})

    def test_v2_batch_persists_before_send_and_retry_is_receipt_first(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                descriptor = self.descriptor(revision)
                state = client._empty_v2_client_state(session)
                client._remember_page(
                    session_path, state,
                    self.page(
                        session, legal=True, revision=revision,
                        items=[descriptor],
                    ),
                    legal=True,
                )
                batch_id = "batch_" + "A" * 24

                def uncertain(*_args, **_kwargs):
                    persisted = client._load_v2_client_state(
                        session_path, session,
                    )
                    self.assertIn(batch_id, persisted["batches"])
                    raise client.PlayerError("connection reset")

                args = type("Args", (), {
                    "session": str(session_path),
                    "action_id": descriptor["action_id"],
                    "arguments": '{"city":"München"}',
                    "json_output": True,
                })()
                output = io.StringIO()
                with patch.object(
                    client.secrets, "token_urlsafe", return_value="A" * 24,
                ), patch.object(
                    client, "_v2_response", side_effect=uncertain,
                ), redirect_stdout(output), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_batch(args), 2)
                disposition = json.loads(output.getvalue())
                self.assertEqual(disposition["batch_id"], batch_id)
                self.assertEqual(disposition["disposition"], "receipt_first")
                persisted = client._load_v2_client_state(session_path, session)
                exact = persisted["batches"][batch_id].encode("utf-8")
                self.assertEqual(
                    client._canonical_body(json.loads(exact.decode("utf-8"))),
                    exact,
                )
                self.assertNotIn(b"v2-agent-secret", exact)

                absent = client.JSONResponse(
                    404, self.error(code="invalid_request"),
                )
                applied = self.receipt(session, batch_id)
                posts = []

                def post(_method, _url, _session, **kwargs):
                    posts.append(kwargs["encoded_body"])
                    return client.JSONResponse(200, applied)

                retry_args = type("Args", (), {
                    "session": str(session_path), "batch_id": batch_id,
                    "json_output": True,
                })()
                with patch.object(
                    client, "_get_receipt_response", return_value=absent,
                ), patch.object(
                    client, "_v2_response", side_effect=post,
                ), redirect_stdout(io.StringIO()):
                    self.assertEqual(client.command_retry(retry_args), 0)
                self.assertEqual(posts, [exact])

                with patch.object(
                    client, "_get_receipt_response",
                ) as no_get, patch.object(
                    client, "_v2_response",
                ) as no_post, redirect_stdout(io.StringIO()):
                    self.assertEqual(client.command_retry(retry_args), 0)
                no_get.assert_not_called()
                no_post.assert_not_called()

    def test_v2_retry_accepted_then_missing_is_ambiguous_without_resend(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                batch_id = "batch_accepted_then_missing"
                state = client._empty_v2_client_state(session)
                state["batches"][batch_id] = json.dumps(
                    {"batch_id": batch_id},
                    sort_keys=True, separators=(",", ":"),
                )
                accepted = client._validate_receipt(
                    self.receipt(session, batch_id, "accepted"), session,
                    batch_id=batch_id,
                )
                state["receipts"][batch_id] = accepted
                client._save_v2_client_state(session_path, state)
                absent = client.JSONResponse(
                    404, self.error(code="invalid_request"),
                )
                args = type("Args", (), {
                    "session": str(session_path), "batch_id": batch_id,
                    "json_output": True,
                })()
                stdout = io.StringIO()
                stderr = io.StringIO()
                with patch.object(
                    client, "_get_receipt_response", return_value=absent,
                ), patch.object(
                    client, "_v2_response",
                ) as no_post, redirect_stdout(stdout), redirect_stderr(stderr):
                    self.assertEqual(client.command_retry(args), 0)
                no_post.assert_not_called()
                printed = json.loads(stdout.getvalue())
                self.assertEqual(printed["receipt_state"], "ambiguous")
                self.assertIn("never replay", stderr.getvalue())
                persisted = client._load_v2_client_state(
                    session_path, session,
                )
                self.assertEqual(
                    persisted["receipts"][batch_id]["receipt_state"],
                    "ambiguous",
                )

    def test_join_identity_defaults_come_from_playconfig(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root):
                config_path = root / ".playconfig.json"
                config_path.write_text(json.dumps({
                    "schema_version": 1,
                    "game_id": "game_12345678901234567890",
                    "name": "codex-gpt-5.6-sol",
                    "place": 2,
                }), encoding="utf-8")
                args = type("Args", (), {
                    "game_id": "", "name": "", "place": "",
                })()
                client._apply_play_defaults(args)
                self.assertEqual(args.game_id, "game_12345678901234567890")
                self.assertEqual(args.name, "codex-gpt-5.6-sol")
                self.assertEqual(args.place, "2")
                explicit = type("Args", (), {
                    "game_id": "game_09876543210987654321",
                    "name": "pi-gpt-5.5", "place": "",
                })()
                client._apply_play_defaults(explicit)
                self.assertEqual(
                    explicit.game_id, "game_09876543210987654321",
                )
                self.assertEqual(explicit.name, "pi-gpt-5.5")
                config_path.write_text(json.dumps({
                    "schema_version": 1, "game_id": "nope", "name": "x",
                    "place": None,
                }), encoding="utf-8")
                bad = type("Args", (), {
                    "game_id": "", "name": "", "place": "",
                })()
                with self.assertRaisesRegex(
                    client.PlayerError, "playconfig",
                ):
                    client._apply_play_defaults(bad)
                config_path.unlink()
                untouched = type("Args", (), {
                    "game_id": "", "name": "", "place": "",
                })()
                client._apply_play_defaults(untouched)
                self.assertEqual(untouched.game_id, "")

    def test_v2_busy_reads_retry_inside_one_command(self):
        busy = client.JSONResponse(429, {"error": {
            "code": "rate_limited",
            "message": "the full-control-v2 sidecar is busy",
            "details": {},
        }})
        ok = client.JSONResponse(200, {"fine": True})
        with patch.object(
            client, "request_json_response", side_effect=[busy, ok],
        ) as request, patch.object(client.time, "sleep") as slept:
            response = client._v2_response(
                "GET", "http://x/state", {"agent_token": "t"},
            )
        self.assertEqual(response.status, 200)
        self.assertEqual(request.call_count, 2)
        self.assertEqual(slept.call_count, 1)
        capacity = client.JSONResponse(429, {"error": {
            "code": "rate_limited",
            "message": "cursor registry is at capacity",
            "details": {"retry_after_seconds": 12},
        }})
        with patch.object(
            client, "request_json_response", side_effect=[capacity],
        ) as request:
            response = client._v2_response(
                "GET", "http://x/state", {"agent_token": "t"},
            )
        self.assertEqual(request.call_count, 1)
        with patch.object(
            client, "request_json_response", side_effect=[busy],
        ) as request:
            response = client._v2_response(
                "POST", "http://x/batch", {"agent_token": "t"},
            )
        self.assertEqual(request.call_count, 1)

    def test_v2_await_failure_never_hides_an_applied_phase_end(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                args = type("Args", (), {
                    "session": str(session_path), "json": False,
                    "wait_s": "", "poll_s": "",
                })()
                disposition = {"receipt": {"receipt_state": "applied"}}
                stdout = io.StringIO()
                with patch.object(
                    client, "_phase_end_locked",
                    return_value=(disposition, "", 0, [
                        "phase end \u2192 applied rev9/t3  batch_x",
                    ]),
                ), patch.object(
                    client, "_await_and_brief_locked",
                    side_effect=client.PlayerError(
                        "invalid v2 health: unexpected future_field",
                    ),
                ), patch.object(
                    client, "_order_receipt_ok", return_value=True,
                ), contextlib.redirect_stdout(stdout):
                    code = client._command_turn_end(
                        args, session_path, session,
                        await_next=True, brief=True,
                    )
                text = stdout.getvalue()
                self.assertEqual(code, 2)
                self.assertIn("phase end", text)
                self.assertIn(
                    "phase ended: the receipt above is authoritative", text,
                )
                self.assertIn("await failed", text)
                self.assertIn("do not", text)
                self.assertIn("re-run `turn --end`", text)

    def test_v2_health_accepts_and_renders_standing_and_waiting_on(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                health = self.health(session)
                health["seat"]["standing"] = "surrendered"
                health["phase"]["waiting_on"] = {
                    "kind": "seat_not_ready",
                    "summary": "fixedlength holds native turn-done",
                    "waiting_s": 12.5,
                    "seats": [{
                        "place": 1, "seat_id": "place-1",
                        "player_name": "AgentPlace1",
                        "controller_label": session["controller_label"],
                        "standing": "surrendered", "is_self": True,
                    }],
                }
                args = type(
                    "Args", (), {"session": str(session_path), "json": False},
                )()
                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(200, health),
                ), contextlib.redirect_stdout(stdout):
                    self.assertEqual(client.command_health(args), 0)
                text = stdout.getvalue()
                self.assertIn("standing surrendered", text)
                self.assertIn(
                    "waiting on seat_not_ready: "
                    "fixedlength holds native turn-done (blocked 12s)",
                    text,
                )

    def test_v2_health_rejects_bad_standing_and_waiting_on_shapes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                args = type(
                    "Args", (), {"session": str(session_path), "json": False},
                )()
                bad_standing = self.health(session)
                bad_standing["seat"]["standing"] = "victorious"
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(200, bad_standing),
                ), self.assertRaisesRegex(
                    client.PlayerError, "health seat standing",
                ):
                    client.command_health(args)
                bad_waiting = self.health(session)
                bad_waiting["phase"]["waiting_on"] = {
                    "kind": "coffee", "summary": "x", "waiting_s": None,
                    "seats": [],
                }
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(200, bad_waiting),
                ), self.assertRaisesRegex(
                    client.PlayerError,
                    "unknown waiting_on kind 'coffee'.*re-materialize",
                ):
                    client.command_health(args)
                recovery_kind = self.health(session)
                recovery_kind["phase"]["waiting_on"] = {
                    "kind": "boundary_recovery",
                    "summary": "tier-1 reattach in progress",
                    "waiting_s": 3.0,
                    "seats": [],
                }
                recovery_kind["sidecar"]["exit_signal"] = None
                recovery_kind["sidecar"]["exit_signal_name"] = None
                recovery_kind["sidecar"]["process_alive"] = True
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(200, recovery_kind),
                ):
                    self.assertEqual(client.command_health(args), 0)
                drifted = self.health(session)
                drifted["sidecar"]["brand_new_field"] = 1
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(200, drifted),
                ), self.assertRaisesRegex(
                    client.PlayerError,
                    "unexpected sidecar field.*brand_new_field.*re-materialize",
                ):
                    client.command_health(args)

    def test_v2_health_rejects_controller_identity_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                health = self.health(session)
                health["agent"]["controller_label"] = "claude-other-model"
                args = type("Args", (), {"session": str(session_path)})()
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(200, health),
                ), self.assertRaisesRegex(
                    client.PlayerError, "health agent identity",
                ):
                    client.command_health(args)

    def test_v2_health_accepts_non_actionable_terminalizing_handoff(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                _session_path, session = self.v2_session(root)
                health = self.health(session)
                health["phase"].update({
                    "state": "terminalizing",
                    "active": False,
                })
                health["phase"]["timing"] = {
                    "mode": "default", "timeout_s": 180,
                    "deadline_started_at": None, "deadline_at": None,
                    "elapsed_s": None, "remaining_s": None,
                }

                clean = client._validate_health(health, session)

                self.assertEqual(clean["phase"]["state"], "terminalizing")
                self.assertFalse(clean["phase"]["active"])

    def test_v2_non_2xx_receipts_and_structured_errors_survive(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                batch_id = "batch_rejected_opaque"
                state = client._empty_v2_client_state(session)
                body = {"batch_id": batch_id}
                state["batches"][batch_id] = json.dumps(
                    body, sort_keys=True, separators=(",", ":"),
                )
                client._save_v2_client_state(session_path, state)
                rejected = self.receipt(session, batch_id, "rejected")
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(422, rejected),
                ):
                    disposition, warning, exit_code = client._submit_persisted_batch(
                        session_path, session, batch_id,
                    )
                self.assertEqual(exit_code, 0)
                self.assertIsNone(warning)
                self.assertEqual(disposition["disposition"], "receipt_terminal")
                self.assertEqual(
                    disposition["receipt"]["receipt_state"], "rejected",
                )

                health_args = type("Args", (), {"session": str(session_path)})()
                error = self.error(
                    code="scope_too_large", retryable=False,
                )
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(413, error),
                ), self.assertRaises(client.V2ResponseError) as raised:
                    client.command_health(health_args)
                self.assertEqual(raised.exception.status, 413)
                self.assertEqual(
                    raised.exception.payload["error"]["code"],
                    "scope_too_large",
                )

    def test_v2_wait_uses_server_actionable_phase_without_fetching_state(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                urls = []

                def response(_method, url, _session, **_kwargs):
                    urls.append(url)
                    return client.JSONResponse(200, self.wait_response(
                        session, "phase_active", active=True,
                    ))

                args = type("Args", (), {
                    "session": str(session_path), "wait_s": 120,
                    "poll_s": 1,
                })()
                with patch.object(
                    client, "_v2_response", side_effect=response,
                ), patch.object(client.time, "sleep") as no_sleep, \
                        redirect_stdout(io.StringIO()):
                    self.assertEqual(client.command_wait(args), 0)
                no_sleep.assert_not_called()
                self.assertEqual(len(urls), 1)
                self.assertIn("/me/wait?", urls[0])
                self.assertNotIn("/state?", urls[0])

                urls.clear()
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(
                        200, self.wait_response(
                            session, "game_terminal", game_state="completed",
                        ),
                    ),
                ), redirect_stdout(io.StringIO()):
                    # P1: the wake reason is the exit status. A terminal game
                    # is 66 so a `until just wait; do :; done` loop stops.
                    self.assertEqual(
                        client.command_wait(args), client.V2_WAIT_EXIT_TERMINAL,
                    )

    def test_v2_health_strictly_validates_caller_phase_end_feedback(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                _session_path, session = self.v2_session(root)
            event = {
                "sequence": 9,
                "turn": 7,
                "phase": 1,
                "place": 1,
                "seat_id": "place-1",
                "player_name": "AgentPlace1",
                "player_color": "#0067A5",
                "controller_label": session["controller_label"],
                "controller_type": "external",
                "source": "timeout",
                "receipt_state": "applied",
                "resolution": "advanced",
                "deadline_started_at": 1000.0,
                "ended_at": 1180.25,
                "elapsed_s": 180.25,
            }
            payload = self.health(session, active=False)
            payload["last_phase_end"] = event
            clean = client._validate_health(payload, session)
            self.assertEqual(clean["last_phase_end"], event)
            self.assertEqual(clean["last_phase_end"]["source"], "timeout")

            for name, value in (
                ("place", 2),
                ("seat_id", "place-2"),
                ("controller_label", "other-harness-model"),
                ("receipt_state", "accepted"),
                ("receipt_state", "reserved"),
            ):
                invalid = self.health(session)
                invalid_event = dict(event)
                invalid_event[name] = value
                invalid["last_phase_end"] = invalid_event
                with self.subTest(name=name), self.assertRaises(client.PlayerError):
                    client._validate_health(invalid, session)
            leaked = self.health(session)
            leaked_event = dict(event)
            leaked_event["batch_id"] = "must-not-cross-health"
            leaked["last_phase_end"] = leaked_event
            with self.assertRaises(client.PlayerError):
                client._validate_health(leaked, session)
            rejected_advance = self.health(session)
            rejected_event = dict(event)
            rejected_event.update({
                "receipt_state": "rejected", "resolution": "advanced",
            })
            rejected_advance["last_phase_end"] = rejected_event
            with self.assertRaises(client.PlayerError):
                client._validate_health(rejected_advance, session)

    def test_v2_wait_server_handles_lobby_until_phase_is_actionable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                urls = []

                def response(_method, url, _session, **_kwargs):
                    urls.append(url)
                    return client.JSONResponse(200, self.wait_response(
                        session, "phase_active", active=True,
                    ))

                args = type("Args", (), {
                    "session": str(session_path), "wait_s": 120,
                    "poll_s": 1,
                })()
                with patch.object(
                    client, "_v2_response", side_effect=response,
                ), patch.object(client.time, "sleep") as sleep, \
                        redirect_stdout(io.StringIO()):
                    self.assertEqual(client.command_wait(args), 0)
                sleep.assert_not_called()
                self.assertEqual(len(urls), 1)
                self.assertIn("until=phase", urls[0])
                self.assertNotIn("/state?", urls[0])

    def test_v2_wait_lobby_timeout_returns_health_without_calling_state(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                args = type("Args", (), {
                    "session": str(session_path), "wait_s": 0,
                    "poll_s": 1,
                })()
                stderr = io.StringIO()
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(
                        200, self.wait_response(
                            session, "timeout", game_state="lobby",
                        ),
                    ),
                ) as request, redirect_stdout(io.StringIO()), \
                        redirect_stderr(stderr):
                    # P1: a timeout means "still not yours, call me again",
                    # which is EX_TEMPFAIL and not success.
                    self.assertEqual(
                        client.command_wait(args), client.V2_WAIT_EXIT_RETRY,
                    )
                self.assertEqual(request.call_count, 1)
                self.assertIn("/me/wait?", request.call_args.args[1])
                self.assertEqual(stderr.getvalue(), "")

    def test_v2_batch_prints_each_closed_disposition_exactly_once(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                state = client._empty_v2_client_state(session)
                cases = (
                    ("poll", 202, "accepted", None, "receipt_poll", 0),
                    ("terminal", 200, "applied", None, "receipt_terminal", 0),
                    ("rate", 429, None, "rate_limited", "retry_exact", 2),
                    ("busy", 503, None, "sidecar_unavailable", "retry_exact", 2),
                    ("stopped", 503, None, "sidecar_unavailable", "refresh", 2),
                    ("stale", 409, None, "stale_revision", "refresh", 2),
                    ("argument", 422, None, "illegal_action", "refresh", 2),
                    ("conflict", 409, None, "conflict", "receipt_first", 2),
                )
                for index, (
                    label, status, receipt_state, error_code,
                    expected, expected_exit,
                ) in enumerate(cases):
                    with self.subTest(label=label):
                        token = chr(ord("A") + index) * 24
                        batch_id = "batch_" + token
                        # An applied receipt retires every outstanding
                        # capability, so each case re-enumerates at the
                        # revision this seat now holds — exactly what an agent
                        # must do between two commands.
                        revision = self.revision(7 + index)
                        descriptor = self.descriptor(revision)
                        client._remember_page(
                            session_path, state,
                            self.page(
                                session, legal=True, revision=revision,
                                items=[descriptor],
                            ),
                            legal=True,
                        )
                        if receipt_state is not None:
                            body = self.receipt(
                                session, batch_id, receipt_state,
                            )
                        else:
                            body = self.error(
                                code=error_code,
                                retryable=(
                                    error_code == "rate_limited"
                                    or label == "busy"
                                ),
                            )
                            body["error"]["details"] = {
                                "batch_id": batch_id,
                                "acceptance": "not_accepted",
                                "safe_next": expected,
                            }
                        args = type("Args", (), {
                            "session": str(session_path),
                            "action_id": descriptor["action_id"],
                            "arguments": "{}", "json_output": True,
                        })()
                        stdout = io.StringIO()
                        with patch.object(
                            client.secrets, "token_urlsafe", return_value=token,
                        ), patch.object(
                            client, "_v2_response",
                            return_value=client.JSONResponse(status, body),
                        ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                            self.assertEqual(
                                client.command_batch(args), expected_exit,
                            )
                        lines = stdout.getvalue().splitlines()
                        self.assertEqual(len(lines), 1)
                        disposition = json.loads(lines[0])
                        self.assertEqual(disposition["batch_id"], batch_id)
                        self.assertEqual(disposition["disposition"], expected)

    def test_v2_batch_invalid_or_unproved_response_is_receipt_first(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                descriptor = self.descriptor(revision)
                state = client._empty_v2_client_state(session)
                client._remember_page(
                    session_path, state,
                    self.page(
                        session, legal=True, revision=revision,
                        items=[descriptor],
                    ),
                    legal=True,
                )
                args = type("Args", (), {
                    "session": str(session_path),
                    "action_id": descriptor["action_id"],
                    "arguments": "{}", "json_output": True,
                })()
                stdout = io.StringIO()
                with patch.object(
                    client.secrets, "token_urlsafe", return_value="Z" * 24,
                ), patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(503, self.error(
                        code="sidecar_unavailable", retryable=True,
                    )),
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_batch(args), 2)
                result = json.loads(stdout.getvalue())
                self.assertEqual(result["disposition"], "receipt_first")
                self.assertEqual(result["batch_id"], "batch_" + "Z" * 24)

                contradictory = self.error(
                    code="sidecar_unavailable", retryable=False,
                )
                contradictory["error"]["details"] = {
                    "batch_id": result["batch_id"],
                    "acceptance": "not_accepted",
                    "safe_next": "retry_exact",
                }
                with self.assertRaisesRegex(client.PlayerError, "contradicts"):
                    client._batch_error_disposition(
                        client.JSONResponse(503, contradictory),
                        session,
                        result["batch_id"],
                    )

    def test_v2_state_merge_is_serialized_across_processes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                client._save_v2_client_state(
                    session_path, client._empty_v2_client_state(session),
                )
                script = """
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
import client
client.ROOT = Path(sys.argv[2])
path = Path(sys.argv[3])
session = client._load_private_object(path, 'session')
batch_id = sys.argv[4]
state = client._load_v2_client_state(path, session)
receipt = {
  'schema_version': 2, 'control_protocol': 'full-control-v2',
  'game_id': session['game_id'], 'agent_id': session['agent_id'],
  'batch_id': batch_id, 'receipt_state': 'applied', 'idempotent': False,
  'state_revision': {'turn': 3, 'revision': 8,
                     'state_token': 'state_' + '8' * 32},
  'error': None,
}
client._remember_receipt(path, state, receipt)
"""
                environment = dict(os.environ)
                environment["PLAY_STATE_DIR"] = ".sessions"
                processes = [
                    subprocess.Popen(
                        [
                            sys.executable, "-c", script,
                            str(Path(client.__file__).resolve().parent),
                            str(root), str(session_path),
                            f"batch_process_{index}",
                        ],
                        cwd=root,
                        env=environment,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                        text=True,
                    )
                    for index in range(8)
                ]
                failures = []
                for process in processes:
                    stdout, stderr = process.communicate(timeout=10)
                    if process.returncode != 0:
                        failures.append((process.returncode, stdout, stderr))
                self.assertEqual(failures, [])
                final = client._load_v2_client_state(session_path, session)
                self.assertEqual(
                    set(final["receipts"]),
                    {f"batch_process_{index}" for index in range(8)},
                )

    def test_v2_openapi_contract_has_closed_routes_refs_and_enums(self):
        contract = json.loads(
            (client.ROOT / "docs" / "full-control-v2.openapi.json").read_text()
        )
        self.assertEqual(contract["openapi"], "3.1.0")
        self.assertEqual(contract["info"]["version"], "2.0.0")
        self.assertEqual(set(contract["paths"]), {
            "/v2/openapi.json",
            "/v2/games/{game_id}/me/health",
            "/v2/games/{game_id}/me/state",
            "/v2/games/{game_id}/me/legal-actions",
            "/v2/games/{game_id}/me/wait",
            "/v2/games/{game_id}/me/batches",
            "/v2/games/{game_id}/me/receipts/{batch_id}",
        })

        def walk(value):
            if isinstance(value, dict):
                reference = value.get("$ref")
                if isinstance(reference, str):
                    self.assertTrue(reference.startswith("#/"))
                    target = contract
                    for part in reference[2:].split("/"):
                        target = target[part]
                for item in value.values():
                    walk(item)
            elif isinstance(value, list):
                for item in value:
                    walk(item)

        walk(contract)
        schemas = contract["components"]["schemas"]
        self.assertEqual(
            set(schemas["CommandReceipt"]["properties"]["receipt_state"]["enum"]),
            client.V2_RECEIPT_STATES,
        )
        self.assertEqual(
            set(schemas["CliBatchDisposition"]["properties"]["disposition"]["enum"]),
            client.V2_DISPOSITIONS,
        )
        self.assertEqual(
            set(schemas["StructuredError"]["properties"]["error"]
                ["properties"]["code"]["enum"]),
            client.V2_ERROR_CODES,
        )
        self.assertEqual(
            set(contract["components"]["parameters"]["StateSection"]
                ["schema"]["enum"]),
            client.V2_SECTIONS,
        )
        legal_response = (
            contract["paths"]["/v2/games/{game_id}/me/legal-actions"]
            ["get"]["responses"]["200"]["content"]["application/json"]
            ["schema"]
        )
        self.assertEqual(
            legal_response["$ref"],
            "#/components/schemas/LegalActionPageEnvelope",
        )
        legal_items = schemas["LegalActionPage"]["properties"]["items"]
        self.assertEqual(
            legal_items["items"]["$ref"],
            "#/components/schemas/LegalActionDescriptor",
        )
        self.assertEqual(
            set(schemas["LegalActionDescriptor"]["required"]),
            {
                "action_id", "kind", "label", "subject",
                "arguments_schema", "state_revision",
            },
        )
        self.assertEqual(
            schemas["LegalActionDescriptor"]["properties"]["kind"]["$ref"],
            "#/components/schemas/ActionKind",
        )
        self.assertEqual(
            schemas["ActionKind"]["pattern"], client.ACTION_KIND_RE.pattern,
        )
        self.assertIn(
            "operation", schemas["LegalActionSubject"]["required"],
        )
        self.assertNotEqual(
            schemas["Page"]["properties"]["items"]["items"], {},
        )
        self.assertEqual(
            schemas["HealthEnvelope"]["x-freeciv-lifecycle"]["lobby"]
            ["state_sections"],
            [
                "overview", "pregame_nations", "pregame_styles",
                "pregame_teams", "votes", "chat", "chat_recipients",
            ],
        )
        self.assertEqual(
            set(schemas["HealthEnvelope"]["x-freeciv-lifecycle"]["lobby"]
                ["legal_action_kinds"]),
            {
                "pregame.configure", "pregame.set_team",
                "pregame.set_ready", "player.cast_vote", "player.send_chat",
                "player.propose_server_setting", "player.cancel_vote",
            },
        )
        self.assertIn("chat_recipients", client.V2_SECTIONS)
        self.assertEqual(
            set(schemas["ChatRecipient"]["required"]),
            {"id", "name", "self", "connected", "can_message"},
        )
        # Error details are a documented vocabulary, not a free-form bag: the
        # renderer spells three of these keys out in full, so the spec must
        # keep naming them.
        details = schemas["StructuredError"]["properties"]["error"][
            "properties"
        ]["details"]
        self.assertEqual(details["$ref"], "#/components/schemas/ErrorDetails")
        detail_properties = schemas["ErrorDetails"]["properties"]
        self.assertLessEqual(
            {
                "pregame_nation_unknown", "pregame_style_unknown",
                "pregame_leader_invalid", "pregame_configuration_unchanged",
            },
            set(detail_properties["rejection_reason"]["enum"]),
        )
        self.assertIn("retry_after_seconds", detail_properties)
        self.assertEqual(
            set(detail_properties["safe_next"]["enum"]),
            set(client._ERROR_REMEDIES),
        )
        # Every restart query key the spec allows must be one the client can
        # spell as a runnable command, or the remedy would be withheld.
        restart_query = schemas["PageRestart"]["properties"]["query"]
        self.assertLessEqual(
            set(restart_query["properties"]),
            set(client.V2_RESTART_OPTIONS),
        )
        self.assertEqual(
            set(schemas["PageRestart"]["properties"]["endpoint"]["enum"]),
            set(client.V2_RESTART_COMMANDS),
        )
        # The scored objective is part of the overview contract.
        self.assertEqual(
            schemas["Page"]["x-freeciv-section-item-schemas"]["overview"],
            {"$ref": "#/components/schemas/Overview"},
        )
        self.assertEqual(
            schemas["Overview"]["properties"]["score"],
            {"$ref": "#/components/schemas/OverviewScore"},
        )
        self.assertEqual(
            set(schemas["OverviewScore"]["required"]),
            {"exact", "lower_bound", "components", "unobserved"},
        )
        self.assertEqual(
            set(schemas["OverviewScore"]["properties"]["components"]
                ["properties"]),
            {"citizens", "techs", "spaceship"},
        )
        send_chat = schemas["SendChatArguments"]
        self.assertEqual(
            send_chat["oneOf"][0]["properties"]["channel"]["enum"],
            ["global", "allied"],
        )
        self.assertEqual(
            send_chat["oneOf"][1]["properties"]["channel"]["const"],
            "private",
        )
        self.assertIn("recipient_id", send_chat["oneOf"][1]["properties"])
        self.assertIn("chat_recipients", json.dumps(send_chat))
        self.assertEqual(
            set(schemas["PregameConfigureArguments"]["required"]),
            {"nation_id", "leader_name", "is_male", "style_id"},
        )
        self.assertEqual(
            schemas["PregameReadyArguments"]["required"], ["ready"],
        )
        self.assertEqual(
            schemas["RelationId"]["pattern"], "^relation_[0-9a-f]{32}$",
        )
        self.assertEqual(
            schemas["TileId"]["pattern"], "^tile_[0-9a-f]{32}$",
        )
        self.assertEqual(
            set(schemas["WaitEnvelope"]["properties"]["wake_reason"]["enum"]),
            client.V2_WAKE_REASONS,
        )
        encoded = json.dumps(contract).casefold()
        self.assertNotIn("agent_token", encoded)
        self.assertNotIn("secret-token", encoded)

    def test_v2_state_file_symlinks_and_invalid_transitions_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                outside = Path(directory) / "outside"
                outside.write_text("{}", encoding="utf-8")
                outside.chmod(0o600)
                session_path.with_suffix(".v2-state").symlink_to(outside)
                with self.assertRaisesRegex(client.PlayerError, "mode 0600"):
                    client._load_v2_client_state(session_path, session)

                session_path.with_suffix(".v2-state").unlink()
                state_lock = session_path.with_suffix(".v2-state.lock")
                state_lock.unlink()
                state_lock.symlink_to(outside)
                with self.assertRaisesRegex(
                    client.PlayerError, "safely lock private player state",
                ):
                    client._load_v2_client_state(session_path, session)
                state_lock.unlink()

                linked_session = session_path.with_name("linked-session.json")
                linked_session.symlink_to(session_path)
                with self.assertRaisesRegex(
                    client.PlayerError, "safely read private session",
                ):
                    client._load_session(str(linked_session))

                state = client._empty_v2_client_state(session)
                batch_id = "batch_terminal_transition"
                applied = client._validate_receipt(
                    self.receipt(session, batch_id, "applied"), session,
                )
                client._remember_receipt(session_path, state, applied)
                ambiguous = client._validate_receipt(
                    self.receipt(session, batch_id, "ambiguous"), session,
                )
                with self.assertRaisesRegex(client.PlayerError, "terminal state"):
                    client._remember_receipt(
                        session_path, state, ambiguous,
                    )

    # -- ergonomics: implicit session, actor catalogs, catalog dedup --------

    @classmethod
    def actor_action(
        cls,
        revision: dict,
        action_id: str,
        actor_id: str,
        *,
        kind: str = "unit.order",
        operation: str = "move",
        label: str = "Move",
        x: int = 31,
        y: int = 72,
        probability: dict | None = None,
    ) -> dict:
        subject: dict = {
            "operation": operation,
            "actor": {"id": actor_id, "type": "unit", "name": "Settlers"},
            "target": {
                "id": "tile_" + f"{x:04d}{y:04d}".rjust(32, "0"),
                "x": x, "y": y,
            },
            "probability": probability or {
                "kind": "exact", "minimum_percent": 100, "maximum_percent": 100,
            },
        }
        return {
            "action_id": action_id,
            "kind": kind,
            "label": label,
            "subject": subject,
            "arguments_schema": {"type": "object"},
            "state_revision": revision,
        }

    def test_v2_session_defaults_to_the_sole_seat_and_refuses_two(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)

                # One private session: every v2 command resolves it itself.
                args = type("Args", (), {"session": ""})()
                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(
                        200, self.health(session, active=True),
                    ),
                ) as request, redirect_stdout(stdout):
                    self.assertEqual(client.command_health(args), 0)
                self.assertEqual(request.call_count, 1)
                self.assertIn("health running", stdout.getvalue())

                # A second joined seat is ambiguous, and the refusal happens
                # before any authenticated request is sent.
                other = root / ".sessions" / "game_09876543210987654321"
                client._write_private_json(other / "second.json", session)
                with patch.object(client, "_v2_response") as blocked:
                    with self.assertRaisesRegex(
                        client.PlayerError, "multiple private sessions",
                    ):
                        client.command_health(args)
                blocked.assert_not_called()

                # PLAY_SESSION names the seat without repeating it per command.
                with patch.dict(
                    os.environ, {"PLAY_SESSION": str(session_path)},
                    clear=False,
                ):
                    self.assertEqual(
                        client._session_path("").resolve(),
                        session_path.resolve(),
                    )
                    with patch.object(
                        client, "_v2_response",
                        return_value=client.JSONResponse(
                            200, self.health(session, active=True),
                        ),
                    ) as named, redirect_stdout(io.StringIO()):
                        self.assertEqual(client.command_health(args), 0)
                    self.assertEqual(named.call_count, 1)

        # No rendered hint re-types the 122-char session path.
        commands = client._turn_next_commands({
            section: {"page": {"next_cursor": None}}
            for section in client.V2_TURN_SECTIONS
        })
        self.assertNotIn("--session", " ".join(commands))
        self.assertIn("just legal --actor_id ACTOR_ID --all", commands)

    def test_v2_legal_all_drains_one_actor_catalog_without_a_kind(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                actor = "unit_" + "a" * 32
                first = self.actor_action(
                    revision, "action_" + "1" * 26, actor, x=31, y=72,
                )
                second = self.actor_action(
                    revision, "action_" + "2" * 26, actor, x=32, y=72,
                )
                third = self.actor_action(
                    revision, "action_" + "3" * 26, actor,
                    kind="unit.found_city", operation="found",
                    label="Found city", x=31, y=72,
                )
                cursor = "cursor_" + "a" * 32
                pages = [
                    self.scoped_legal_page(
                        session, revision=revision, items=[first, second],
                        actor_id=actor, catalog="catalog_" + "1" * 32,
                        cursor=cursor,
                    ),
                    self.scoped_legal_page(
                        session, revision=revision, items=[third],
                        actor_id=actor, catalog="catalog_" + "1" * 32,
                    ),
                ]
                for page in pages:
                    page["page"]["total_items"] = 3
                sent: list[str] = []

                def record(method, url, current, **options):
                    sent.append(url)
                    return client.JSONResponse(200, pages[len(sent) - 1])

                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=record,
                ), redirect_stdout(stdout):
                    self.assertEqual(client.command_legal(self.alias_args(
                        session=str(session_path), actor_id=actor,
                        all_pages=True,
                    )), 0)

                # One drain, no cursor ceremony left for the agent.
                self.assertEqual(len(sent), 2)
                self.assertIn(f"actor_id={actor}", sent[0])
                self.assertIn(f"cursor={cursor}", sent[1])
                lines = stdout.getvalue().splitlines()
                self.assertEqual(len(lines), 4)
                self.assertIn("rev7/t3 legal scope=unit u1", lines[0])
                self.assertIn("3/3 matched", lines[0])
                self.assertIn("catalog 3 complete, pages 2", lines[0])
                self.assertNotIn("kind=", lines[0])
                self.assertNotIn("--cursor", lines[0])
                for index, action in enumerate(
                    (first, second, third), start=1,
                ):
                    # An aliased row drops the 32-hex opaque ID: the alias is
                    # the handle, and `--json` below still carries every ID.
                    self.assertTrue(lines[index].startswith(f"a{index} "))
                    self.assertNotIn(
                        action["action_id"], lines[index], lines[index],
                    )
                    self.assertEqual(
                        client._expand_alias(
                            client._load_v2_client_state(
                                session_path, session,
                            ),
                            f"a{index}", session_path,
                        ),
                        action["action_id"],
                    )
                self.assertIn("unit.order/move", lines[1])
                self.assertIn("T(31,72)", lines[1])
                self.assertIn("unit.found_city/found", lines[3])

                # The whole catalog was promoted atomically, exactly as the
                # --kind form promotes, and the drain is recorded.
                state = client._load_v2_client_state(session_path, session)
                self.assertEqual(
                    set(state["actions"]),
                    {
                        first["action_id"], second["action_id"],
                        third["action_id"],
                    },
                )
                self.assertEqual(state["pending_catalogs"], {})
                self.assertEqual(state["drained_actors"], [actor])
                self.assertEqual(
                    client._expand_alias(state, "a2", session_path),
                    second["action_id"],
                )

                # The JSON escape hatch still carries every full field.
                sent.clear()
                raw = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=record,
                ), redirect_stdout(raw):
                    self.assertEqual(client.command_legal(self.alias_args(
                        session=str(session_path), actor_id=actor,
                        all_pages=True, json_output=True,
                    )), 0)
                result = json.loads(raw.getvalue())
                self.assertIsNone(result["kind"])
                self.assertEqual(result["catalog_total"], 3)
                self.assertEqual(result["shown"], 3)
                self.assertEqual(
                    [action["action_id"] for action in result["actions"]],
                    [
                        first["action_id"], second["action_id"],
                        third["action_id"],
                    ],
                )

    def test_v2_legal_all_requires_a_scope_and_keeps_the_kind_form(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                with patch.object(client, "_v2_response") as blocked:
                    with self.assertRaisesRegex(
                        client.PlayerError, "legal --all needs a scope",
                    ) as unscoped:
                        client.command_legal(self.alias_args(
                            session=str(session_path), all_pages=True,
                        ))
                    with self.assertRaisesRegex(
                        client.PlayerError,
                        "use --kind ACTION_KIND and --all together",
                    ):
                        client.command_legal(self.alias_args(
                            session=str(session_path), kind="phase.end",
                        ))
                    with self.assertRaisesRegex(
                        client.PlayerError,
                        r"legal --offset requires --all",
                    ):
                        client.command_legal(self.alias_args(
                            session=str(session_path), offset="4",
                        ))
                blocked.assert_not_called()
                message = str(unscoped.exception)
                self.assertIn("--kind ACTION_KIND --all", message)
                self.assertIn("--actor_id ACTOR_ID", message)

    def test_v2_identical_actor_catalogs_render_once_per_revision(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                actors = [f"unit_{letter * 32}" for letter in "abcd"]

                def catalog(
                    actor_id: str, tag: str, *, probability: dict | None = None,
                ) -> dict:
                    page = self.scoped_legal_page(
                        session,
                        revision=revision,
                        items=[
                            self.actor_action(
                                revision, f"action_{tag}" + "0" * 25, actor_id,
                                x=31, y=72,
                            ),
                            self.actor_action(
                                revision, f"action_{tag}" + "1" * 25, actor_id,
                                kind="unit.found_city", operation="found",
                                label="Found city", x=31, y=72,
                                probability=probability,
                            ),
                        ],
                        actor_id=actor_id,
                        catalog=f"catalog_{tag * 32}",
                    )
                    return page

                def drain(actor_id: str, page: dict) -> list[str]:
                    stdout = io.StringIO()
                    with patch.object(
                        client, "_v2_response",
                        return_value=client.JSONResponse(200, page),
                    ), redirect_stdout(stdout):
                        self.assertEqual(client.command_legal(self.alias_args(
                            session=str(session_path), actor_id=actor_id,
                            all_pages=True,
                        )), 0)
                    return stdout.getvalue().splitlines()

                # The first actor prints its whole catalog.
                first = drain(actors[0], catalog(actors[0], "a"))
                self.assertEqual(len(first), 3)
                self.assertNotIn("==", "\n".join(first))

                # The second offers exactly the same choices: one line, no
                # rows, and its own aliases named so it stays executable.
                second = drain(actors[1], catalog(actors[1], "b"))
                self.assertEqual(len(second), 2)
                self.assertIn("rev7/t3 legal scope=unit u2", second[0])
                self.assertEqual(second[1], "u2 == u1 (rev7) a3..a4")
                state = client._load_v2_client_state(session_path, session)
                self.assertEqual(
                    client._expand_alias(state, "a3", session_path),
                    "action_b" + "0" * 25,
                )
                self.assertEqual(
                    client._expand_alias(state, "a4", session_path),
                    "action_b" + "1" * 25,
                )

                # A differing row is never hidden by the equivalence claim.
                third = drain(actors[2], catalog(
                    actors[2], "c",
                    probability={
                        "kind": "unknown", "minimum_percent": 0,
                        "maximum_percent": 100,
                    },
                ))
                self.assertEqual(len(third), 3)
                self.assertEqual(third[1], "u3 == u1 (rev7) a5..a6 except 1 row")
                self.assertIn("!prob=0-100%/unknown", third[2])
                self.assertIn("unit.found_city/found", third[2])

                state = client._load_v2_client_state(session_path, session)
                self.assertEqual(state["drained_actors"], actors[:3])

                # The same options in a different order are not claimed
                # equivalent: the short line's alias run means "row for row".
                reordered_actor = "unit_" + "e" * 32
                reordered = catalog(reordered_actor, "e")
                reordered["page"]["items"].reverse()
                reordered_lines = drain(reordered_actor, reordered)
                self.assertEqual(len(reordered_lines), 3)
                self.assertNotIn("==", "\n".join(reordered_lines))

                # A newer revision expires every cached catalog, so nothing may
                # be claimed equivalent across revisions.
                later = self.revision(9)
                fourth = self.scoped_legal_page(
                    session,
                    revision=later,
                    items=[
                        self.actor_action(
                            revision=later,
                            action_id="action_d" + "0" * 25,
                            actor_id=actors[3], x=31, y=72,
                        ),
                        self.actor_action(
                            revision=later,
                            action_id="action_d" + "1" * 25,
                            actor_id=actors[3], kind="unit.found_city",
                            operation="found", label="Found city",
                            x=31, y=72,
                        ),
                    ],
                    actor_id=actors[3],
                    catalog="catalog_" + "d" * 32,
                )
                lines = drain(actors[3], fourth)
                self.assertEqual(len(lines), 3)
                self.assertNotIn("==", "\n".join(lines))
                # u5: the reordered actor above claimed u4 when it was seen.
                self.assertIn("rev9/t3 legal scope=unit u5", lines[0])
                state = client._load_v2_client_state(session_path, session)
                self.assertEqual(state["drained_actors"], [actors[3]])

                # Re-reading the same actor at the new revision still cannot
                # borrow the expired equivalence.
                repeat = drain(actors[3], fourth)
                self.assertNotIn("==", "\n".join(repeat))


    # ------------------------------------------------------------------
    # I5: intent commands (`do`, `turn --end --await`, `start`) and the
    # local state mirror behind `show`.
    # ------------------------------------------------------------------

    @staticmethod
    def do_args(orders: str, session: str, **values):
        defaults = {
            "session": session, "orders": orders,
            "continue_on_error": False,
        }
        defaults.update(values)
        return type("Args", (), defaults)()

    @classmethod
    def pregame_action(
        cls, revision: dict, action_id: str, kind: str, operation: str,
        label: str, schema: dict, target: dict,
    ) -> dict:
        return {
            "action_id": action_id,
            "kind": kind,
            "label": label,
            "subject": {
                "operation": operation,
                "actor": {
                    "id": "player_" + "f" * 32, "type": "player",
                    "name": "AgentPlace1",
                },
                "target": target,
                "variant": None,
                "consuming": False,
                "legality": "legal",
                "probability": {
                    "kind": "exact", "minimum_percent": 100,
                    "maximum_percent": 100,
                },
            },
            "arguments_schema": schema,
            "state_revision": revision,
        }

    def cache_actor_catalog(
        self, session_path: Path, session: dict, revision: dict,
        actor: str, items: list,
    ) -> None:
        page = self.scoped_legal_page(
            session, revision=revision, items=items, actor_id=actor,
            catalog="catalog_" + "a" * 32,
        )
        with patch.object(
            client, "_v2_response",
            return_value=client.JSONResponse(200, page),
        ), redirect_stdout(io.StringIO()):
            self.assertEqual(client.command_legal(self.alias_args(
                session=str(session_path), actor_id=actor, all_pages=True,
            )), 0)

    @staticmethod
    def found_city_action(base: dict) -> dict:
        action = dict(base)
        action["arguments_schema"] = {
            "type": "object",
            "properties": {"city_name": {"type": "string"}},
            "required": ["city_name"],
        }
        return action

    def test_v2_do_then_turn_end_rebinds_phase_end_at_the_new_revision(self):
        """The doc §8 headline loop: `just do …` then `just turn --end`.

        The order's own receipt bumps the revision, which retires every cached
        capability including the `phase.end` handle the previous briefing may
        have shown.  `turn --end` must therefore re-drain before it submits;
        reusing the pre-batch handle would post an expired capability at the
        pre-batch revision and be refused by the server.
        """
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                first = self.revision(7)
                second = self.revision(8)
                third = self.revision(9)
                actor = "unit_" + "a" * 32
                found = self.found_city_action(self.actor_action(
                    first, "action_" + "1" * 26, actor,
                    kind="unit.found_city", operation="found",
                    label="Found city", x=31, y=72,
                ))
                # The seat also holds a phase.end handle *before* the order,
                # so a client that reused its cache would find one and never
                # re-enumerate.
                stale_end = self.descriptor(first, "action_" + "2" * 26)
                self.cache_actor_catalog(
                    session_path, session, first, actor, [found, stale_end],
                )
                cached = client._load_v2_client_state(session_path, session)
                self.assertIn(stale_end["action_id"], cached["actions"])

                fresh_end = self.descriptor(second, "action_" + "3" * 26)
                sent: list[tuple[str, str, dict | None]] = []

                def responder(method, url, current, **options):
                    body = options.get("encoded_body")
                    payload = (
                        json.loads(body.decode("utf-8"))
                        if body is not None else None
                    )
                    sent.append((method, url, payload))
                    if method == "POST":
                        revision = (
                            second if payload["state_revision"] == first
                            else third
                        )
                        return client.JSONResponse(200, self.receipt(
                            session, payload["batch_id"], "applied",
                            revision=revision,
                        ))
                    return client.JSONResponse(200, self.page(
                        session, legal=True, revision=second,
                        items=[fresh_end],
                    ))

                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_do(self.do_args(
                        "u1 found_city London", str(session_path),
                    )), 0)
                    self.assertEqual(client.command_turn(type("Args", (), {
                        "session": str(session_path),
                        "end_phase": True, "await_phase": False,
                    })()), 0)

                methods = [method for method, _url, _payload in sent]
                # POST the order, GET a fresh catalog, POST the phase end.
                self.assertEqual(methods, ["POST", "GET", "POST"])
                self.assertIn("/legal-actions", sent[1][1])
                bodies = [
                    payload for _method, _url, payload in sent
                    if payload is not None
                ]
                self.assertEqual(bodies[0]["state_revision"], first)
                self.assertEqual(
                    bodies[0]["commands"][0]["action_id"], found["action_id"],
                )
                # The phase end went out at the post-receipt revision, with the
                # handle the re-drain proved -- never the pre-batch one.
                self.assertEqual(bodies[1]["state_revision"], second)
                self.assertEqual(
                    bodies[1]["commands"][0]["action_id"],
                    fresh_end["action_id"],
                )
                self.assertNotEqual(
                    bodies[1]["commands"][0]["action_id"],
                    stale_end["action_id"],
                )
                lines = stdout.getvalue().splitlines()
                self.assertTrue(
                    any("found_city" in line for line in lines), lines,
                )
                self.assertTrue(
                    any("phase end" in line for line in lines), lines,
                )

    def test_v2_do_refuses_every_order_when_one_cannot_be_resolved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                actor = "unit_" + "a" * 32
                move = self.actor_action(
                    revision, "action_" + "1" * 26, actor, x=32, y=72,
                )
                self.cache_actor_catalog(
                    session_path, session, revision, actor, [move],
                )

                # One unresolvable order refuses the whole line, and the
                # refusal happens before any request leaves the client.
                with patch.object(client, "_v2_response") as blocked:
                    with self.assertRaises(client.PlayerError) as refusal:
                        client.command_do(self.do_args(
                            "u1 move 32,72; u1 teleport 99,99; c9 build X",
                            str(session_path),
                        ))
                blocked.assert_not_called()
                message = str(refusal.exception)
                lines = message.splitlines()
                self.assertIn(
                    "2 of 3 orders did not resolve", lines[0],
                )
                self.assertIn("rev7/t3", lines[0])
                self.assertIn("nothing was sent", lines[0])
                self.assertIn("1 resolved", lines[1])
                self.assertIn("u1 move 32,72", lines[1])
                self.assertIn("2 unresolved", lines[2])
                self.assertIn("teleport", lines[2])
                self.assertIn("3 unresolved", lines[3])
                # Every unresolved order names the exact command to run, in
                # the alias dialect the agent already types.
                self.assertIn(
                    "enumerate with: just legal --actor_id u1 --all", message,
                )

                # Bounds and shape are refused with no request either.
                with patch.object(client, "_v2_response") as blocked:
                    with self.assertRaisesRegex(
                        client.PlayerError, "1 through 8 orders",
                    ):
                        client.command_do(self.do_args(
                            "; ".join(["u1 move 32,72"] * 9),
                            str(session_path),
                        ))
                    with self.assertRaisesRegex(
                        client.PlayerError, "at least one order",
                    ):
                        client.command_do(self.do_args("  ;  ", str(session_path)))
                blocked.assert_not_called()

    def test_v2_order_grammar_uses_only_what_the_catalog_advertised(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                goal = self.pregame_action(
                    revision, "action_" + "g" * 26, "research.set_goal",
                    "set_goal", "Set research goal",
                    {
                        "type": "object",
                        "properties": {
                            "tech": {
                                "type": "string",
                                "enum": ["Currency", "Alphabet"],
                            },
                        },
                        "required": ["tech"],
                    },
                    None,
                )
                page = self.page(
                    session, legal=True, revision=revision, items=[goal],
                )
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(200, page),
                ), redirect_stdout(io.StringIO()):
                    self.assertEqual(client.command_legal(self.alias_args(
                        session=str(session_path),
                    )), 0)
                state = client._load_v2_client_state(session_path, session)

                # Family form, full kind, bare verb, and a bare action alias
                # all name the same capability; enum values are matched
                # case-insensitively and rendered back exactly as advertised.
                for text, tech in (
                    ("research set_goal currency", "Currency"),
                    ("research.set_goal Currency", "Currency"),
                    ("set_goal Alphabet", "Alphabet"),
                    ("a1 Alphabet", "Alphabet"),
                    # The one documented Tier-1 word for this capability.
                    ("research goal Currency", "Currency"),
                ):
                    with self.subTest(order=text):
                        resolved = client._resolve_order(
                            state, session_path, text,
                        )
                        self.assertEqual(
                            resolved["action_id"], goal["action_id"],
                        )
                        self.assertEqual(resolved["arguments"], {"tech": tech})

                # A verb the catalog never advertised is never guessed at,
                # and a Tier-1 word still needs its capability in scope.
                for text in ("research target Currency", "u1 set_goal Currency"):
                    with self.subTest(order=text):
                        with self.assertRaises(Exception):
                            client._resolve_order(state, session_path, text)

                # A value outside the advertised enum is refused, not coerced.
                with self.assertRaises(Exception):
                    client._resolve_order(
                        state, session_path, "research set_goal Pottery",
                    )

                # Two cached actions that answer the same words are ambiguous,
                # and the refusal names the aliases that disambiguate them.
                actor = "unit_" + "a" * 32
                self.cache_actor_catalog(
                    session_path, session, revision, actor, [
                        self.actor_action(
                            revision, "action_" + "1" * 26, actor,
                            x=32, y=72,
                        ),
                        self.actor_action(
                            revision, "action_" + "2" * 26, actor,
                            x=31, y=71,
                        ),
                    ],
                )
                state = client._load_v2_client_state(session_path, session)
                with self.assertRaisesRegex(
                    Exception, "2 cached actions match",
                ) as ambiguous:
                    client._resolve_order(state, session_path, "u1 move")
                self.assertRegex(str(ambiguous.exception), r"a\d+ a\d+")
                # Naming the target disambiguates without a request.
                self.assertEqual(
                    client._resolve_order(
                        state, session_path, "u1 move 31,71",
                    )["action_id"],
                    "action_" + "2" * 26,
                )

    def test_v2_do_sends_one_batch_per_order_and_rebinds_after_a_bump(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                first = self.revision(7)
                later = self.revision(9)
                actor = "unit_" + "a" * 32
                move = self.actor_action(
                    first, "action_" + "1" * 26, actor, x=32, y=72,
                )
                found = self.found_city_action(self.actor_action(
                    first, "action_" + "2" * 26, actor,
                    kind="unit.found_city", operation="found",
                    label="Found city", x=31, y=72,
                ))
                self.cache_actor_catalog(
                    session_path, session, first, actor, [move, found],
                )
                fresh_found = self.found_city_action(self.actor_action(
                    later, "action_" + "4" * 26, actor,
                    kind="unit.found_city", operation="found",
                    label="Found city", x=31, y=72,
                ))
                refreshed = self.scoped_legal_page(
                    session, revision=later,
                    items=[
                        self.actor_action(
                            later, "action_" + "3" * 26, actor, x=32, y=72,
                        ),
                        fresh_found,
                    ],
                    actor_id=actor, catalog="catalog_" + "b" * 32,
                )
                sent: list[tuple[str, str, dict | None]] = []

                def responder(method, url, current, **options):
                    body = options.get("encoded_body")
                    payload = (
                        json.loads(body.decode("utf-8"))
                        if body is not None else None
                    )
                    sent.append((method, url, payload))
                    if method == "POST":
                        return client.JSONResponse(200, self.receipt(
                            session, payload["batch_id"], "applied",
                            revision=later,
                        ))
                    return client.JSONResponse(200, refreshed)

                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_do(self.do_args(
                        "u1 move 32,72; u1 found_city London",
                        str(session_path),
                    )), 0)

                # One single-command wire batch per order, with exactly one
                # internal re-enumeration between them: the first order bumped
                # the revision, so the second order's handle was re-bound.
                methods = [method for method, _url, _payload in sent]
                self.assertEqual(methods, ["POST", "GET", "POST"])
                bodies = [
                    payload for _method, _url, payload in sent
                    if payload is not None
                ]
                for body in bodies:
                    self.assertEqual(len(body["commands"]), 1)
                self.assertEqual(
                    bodies[0]["commands"][0],
                    {"action_id": move["action_id"], "arguments": {}},
                )
                self.assertEqual(bodies[0]["state_revision"], first)
                # The second order was never sent with the expired handle.
                self.assertEqual(
                    bodies[1]["commands"][0],
                    {
                        "action_id": fresh_found["action_id"],
                        "arguments": {"city_name": "London"},
                    },
                )
                self.assertEqual(bodies[1]["state_revision"], later)
                self.assertNotEqual(
                    bodies[1]["commands"][0]["action_id"],
                    found["action_id"],
                )

                # No alias, verb, or invented coordinate reaches the wire.
                # (Server-issued batch IDs are random, so they are excluded
                # from the scan rather than allowed to make it flaky.)
                wire = json.dumps([
                    (
                        method, url,
                        None if payload is None else {
                            key: value for key, value in payload.items()
                            if key != "batch_id"
                        },
                    )
                    for method, url, payload in sent
                ], sort_keys=True)
                for forbidden in ("u1", "a1", "a2", "found_city", "32,72"):
                    self.assertNotIn(forbidden, wire)

                lines = stdout.getvalue().splitlines()
                self.assertEqual(len(lines), 4)
                self.assertTrue(lines[3].startswith("next: "), lines[3])
                self.assertTrue(lines[0].startswith("u1 move 32,72 → applied"))
                self.assertTrue(
                    lines[1].startswith("u1 found_city London → applied"),
                    lines[1],
                )
                self.assertTrue(lines[0].endswith(bodies[0]["batch_id"]))
                self.assertTrue(lines[1].endswith(bodies[1]["batch_id"]))
                self.assertEqual(lines[2], "2/2 applied rev9/t3")

    def test_v2_do_summary_never_contradicts_the_receipt_above_it(self):
        """A one-order `do` must report the revision its own receipt proved.

        The summary is the line an agent anchors on; printing the pre-batch
        revision teaches it that its own action did not move the game, and
        that its `a1..aN` are still fresh when they are not.
        """
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                before = self.revision(7)
                after = self.revision(9)
                actor = "unit_" + "a" * 32
                found = self.found_city_action(self.actor_action(
                    before, "action_" + "2" * 26, actor,
                    kind="unit.found_city", operation="found",
                    label="Found city", x=31, y=72,
                ))
                self.cache_actor_catalog(
                    session_path, session, before, actor, [found],
                )

                def responder(method, url, current, **options):
                    body = options.get("encoded_body")
                    payload = json.loads(body.decode("utf-8"))
                    return client.JSONResponse(200, self.receipt(
                        session, payload["batch_id"], "applied",
                        revision=after,
                    ))

                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_do(self.do_args(
                        "u1 found_city London", str(session_path),
                    )), 0)

                lines = stdout.getvalue().splitlines()
                self.assertEqual(len(lines), 3, lines)
                self.assertIn("→ applied rev9/t3", lines[0])
                self.assertEqual(lines[1], "1/1 applied rev9/t3")
                self.assertTrue(lines[2].startswith("next: "), lines[2])

                # `--json` carries the same revision, not the pre-batch one.
                self.cache_actor_catalog(
                    session_path, session, after,
                    actor,
                    [self.found_city_action(self.actor_action(
                        after, "action_" + "3" * 26, actor,
                        kind="unit.found_city", operation="found",
                        label="Found city", x=31, y=72,
                    ))],
                )
                raw = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ), redirect_stdout(raw), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_do(self.do_args(
                        "u1 found_city London", str(session_path),
                        json_output=True,
                    )), 0)
                self.assertEqual(
                    json.loads(raw.getvalue())["state_revision"], after,
                )

    def test_v2_do_never_discards_an_outcome_it_already_printed(self):
        """An applied order's batch_id survives a later failure in the batch.

        Losing it tells the agent the command failed while the server holds an
        applied batch, and the agent then re-issues a real duplicate action.
        """
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                before = self.revision(7)
                after = self.revision(9)
                actor = "unit_" + "a" * 32
                move = self.actor_action(
                    before, "action_" + "1" * 26, actor, x=32, y=72,
                )
                found = self.found_city_action(self.actor_action(
                    before, "action_" + "2" * 26, actor,
                    kind="unit.found_city", operation="found",
                    label="Found city", x=31, y=72,
                ))
                self.cache_actor_catalog(
                    session_path, session, before, actor, [move, found],
                )
                posted: list[str] = []

                def responder(method, url, current, **options):
                    if method == "POST":
                        payload = json.loads(
                            options["encoded_body"].decode("utf-8"),
                        )
                        posted.append(payload["batch_id"])
                        return client.JSONResponse(200, self.receipt(
                            session, payload["batch_id"], "applied",
                            revision=after,
                        ))
                    # The internal re-enumeration is where the wheels come off.
                    raise client.PlayerError(
                        "the v2 request could not be sent: connection reset"
                    )

                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_do(self.do_args(
                        "u1 move 32,72; u1 found_city London",
                        str(session_path),
                    )), 2)

                output = stdout.getvalue()
                self.assertNotEqual(output, "")
                lines = output.splitlines()
                # The applied order, its server-issued batch_id, and the
                # remedy that resolves it are all still on stdout.
                self.assertEqual(len(posted), 1)
                self.assertTrue(
                    lines[0].startswith("u1 move 32,72 → applied rev9/t3"),
                    lines[0],
                )
                self.assertTrue(lines[0].endswith(posted[0]), lines[0])
                self.assertIn("connection reset", output)
                self.assertIn("1/2 applied rev9/t3", lines)
                self.assertTrue(
                    any("stopped after order 1" in line for line in lines),
                    lines,
                )

    def test_v2_do_stops_on_the_first_rejection_unless_told_to_continue(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                actor = "unit_" + "a" * 32
                move = self.actor_action(
                    revision, "action_" + "1" * 26, actor, x=32, y=72,
                )
                found = self.found_city_action(self.actor_action(
                    revision, "action_" + "2" * 26, actor,
                    kind="unit.found_city", operation="found",
                    label="Found city", x=31, y=72,
                ))
                self.cache_actor_catalog(
                    session_path, session, revision, actor, [move, found],
                )

                def responder(method, url, current, **options):
                    payload = json.loads(
                        options["encoded_body"].decode("utf-8"),
                    )
                    state = (
                        "rejected"
                        if payload["commands"][0]["action_id"]
                        == move["action_id"] else "applied"
                    )
                    return client.JSONResponse(200, self.receipt(
                        session, payload["batch_id"], state,
                        revision=revision,
                    ))

                orders = "u1 move 32,72; u1 found_city London"
                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ) as request, redirect_stdout(stdout), redirect_stderr(
                    io.StringIO(),
                ):
                    self.assertEqual(client.command_do(self.do_args(
                        orders, str(session_path),
                    )), 2)
                # The refused actor's own options print under the refusal, and
                # the cached catalog answers them: still one request.
                self.assertEqual(request.call_count, 1)
                lines = stdout.getvalue().splitlines()
                self.assertEqual(len(lines), 6)
                self.assertIn("rejected", lines[0])
                self.assertEqual(lines[1], "0/2 applied rev7/t3")
                self.assertIn("stopped after order 1", lines[2])
                self.assertIn("1 not sent", lines[2])
                self.assertIn("--continue-on-error", lines[2])
                self.assertEqual(lines[3], "u1 can (rev7/t3): 2 options")
                self.assertTrue(lines[4].startswith("a1  unit.order/move"))
                self.assertTrue(
                    lines[5].startswith("a2  unit.found_city/found"), lines[5],
                )

                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ) as request, redirect_stdout(stdout), redirect_stderr(
                    io.StringIO(),
                ):
                    self.assertEqual(client.command_do(self.do_args(
                        orders, str(session_path), continue_on_error=True,
                    )), 2)
                self.assertEqual(request.call_count, 2)
                lines = stdout.getvalue().splitlines()
                self.assertEqual(len(lines), 7)
                self.assertIn("rejected", lines[0])
                self.assertIn("applied", lines[1])
                self.assertEqual(lines[2], "1/2 applied rev7/t3")
                self.assertEqual(lines[3], "u1 can (rev7/t3): 2 options")
                # The focus tail is still the last word of the command.
                self.assertTrue(lines[6].startswith("next: "), lines[6])

    def refused_seat(self, session_path: Path, session: dict, revision: dict):
        """One unit with a two-action catalog, and the order that refuses."""
        actor = "unit_" + "a" * 32
        move = self.actor_action(
            revision, "action_" + "1" * 26, actor, x=32, y=72,
        )
        found = self.found_city_action(self.actor_action(
            revision, "action_" + "2" * 26, actor,
            kind="unit.found_city", operation="found",
            label="Found city", x=31, y=72,
        ))
        self.cache_actor_catalog(
            session_path, session, revision, actor, [move, found],
        )
        return actor, move, found

    def test_v2_refusal_fetches_and_bounds_the_refused_actor_options(self):
        """A refusal that moved the game re-reads the menu it hands back."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                moved = self.revision(9)
                actor, _move, _found = self.refused_seat(
                    session_path, session, revision,
                )
                # Thirty-seven options at the newer revision, over the three
                # pages the wire bounds them to: far more than the twelve a
                # refusal is allowed to print.
                catalog = [
                    self.actor_action(
                        moved, "action_" + f"{index:026d}", actor,
                        x=30 + index, y=72,
                    )
                    for index in range(37)
                ]
                pages = [catalog[0:16], catalog[16:32], catalog[32:]]
                reads: list[str] = []

                def responder(method, url, current, **options):
                    if method == "POST":
                        payload = json.loads(
                            options["encoded_body"].decode("utf-8"),
                        )
                        # The refusal itself carries the newer revision, so
                        # every cached capability for this actor is retired.
                        return client.JSONResponse(200, self.receipt(
                            session, payload["batch_id"], "rejected",
                            revision=moved,
                        ))
                    index = len(reads)
                    reads.append(url)
                    page = self.scoped_legal_page(
                        session, revision=moved, items=pages[index],
                        actor_id=actor, catalog="catalog_" + "b" * 32,
                        cursor=(
                            "cursor_" + str(index + 1) * 32
                            if index + 1 < len(pages) else None
                        ),
                    )
                    page["page"]["total_items"] = len(catalog)
                    return client.JSONResponse(200, page)

                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_do(self.do_args(
                        "u1 move 32,72", str(session_path),
                    )), 2)
                lines = stdout.getvalue().splitlines()
                # One drain of exactly the refused actor's catalog, cursors
                # followed, and nothing else enumerated.
                self.assertEqual(len(reads), 3, reads)
                self.assertIn(f"actor_id={actor}", reads[0])
                self.assertIn("rejected", lines[0])
                self.assertEqual(lines[1], "0/1 applied rev9/t3")
                self.assertIn("stopped after order 1", lines[2])
                self.assertEqual(
                    lines[3],
                    "u1 can (rev9/t3): 12 of 37 shown — all: "
                    "just legal --actor_id u1 --all",
                )
                rows = lines[4:]
                self.assertEqual(len(rows), 12)
                # The rows are the `just legal` rows, alias first, so the very
                # next call can name one without another command.
                self.assertEqual(rows[0], "a1   unit.order/move  Move  T(30,72)")
                self.assertEqual(
                    rows[11], "a12  unit.order/move  Move  T(41,72)",
                )

    def test_v2_refusal_prints_one_options_section_for_each_refused_actor(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                first = "unit_" + "a" * 32
                second = "unit_" + "b" * 32
                for index, actor in enumerate((first, second)):
                    self.cache_actor_catalog(
                        session_path, session, revision, actor, [
                            self.actor_action(
                                revision, "action_" + f"{index:026d}", actor,
                                x=32, y=72,
                            ),
                        ],
                    )

                def responder(method, url, current, **options):
                    payload = json.loads(
                        options["encoded_body"].decode("utf-8"),
                    )
                    return client.JSONResponse(200, self.receipt(
                        session, payload["batch_id"], "rejected",
                        revision=revision,
                    ))

                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ) as request, redirect_stdout(stdout), redirect_stderr(
                    io.StringIO(),
                ):
                    self.assertEqual(client.command_do(self.do_args(
                        "u1 move 32,72; u2 move 32,72", str(session_path),
                        continue_on_error=True,
                    )), 2)
                lines = stdout.getvalue().splitlines()
                # Both refused actors are answered, and the still-valid cached
                # catalogs mean neither answer cost a round trip.
                self.assertEqual(request.call_count, 2)
                self.assertEqual(lines[3], "u1 can (rev7/t3): 1 options")
                self.assertEqual(lines[5], "u2 can (rev7/t3): 1 options")

    def test_v2_refusal_survives_an_options_lookup_that_fails(self):
        """Enrichment that cannot run leaves the refusal exactly as it was."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                moved = self.revision(9)
                self.refused_seat(session_path, session, revision)

                def responder(method, url, current, **options):
                    if method == "POST":
                        payload = json.loads(
                            options["encoded_body"].decode("utf-8"),
                        )
                        return client.JSONResponse(200, self.receipt(
                            session, payload["batch_id"], "rejected",
                            revision=moved,
                        ))
                    # The re-read the enrichment needs is refused outright.
                    return client.JSONResponse(503, self.error(
                        code="sidecar_unavailable", retryable=True,
                    ))

                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_do(self.do_args(
                        "u1 move 32,72", str(session_path),
                    )), 2)
                lines = stdout.getvalue().splitlines()
                # The three lines this refusal printed before enrichment
                # existed, and nothing else.
                self.assertEqual(len(lines), 3)
                self.assertIn("rejected", lines[0])
                self.assertEqual(lines[1], "0/1 applied rev9/t3")
                self.assertIn("stopped after order 1", lines[2])
                self.assertNotIn("can (", stdout.getvalue())

    def test_v2_applied_orders_print_no_options_section(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                self.refused_seat(session_path, session, revision)

                def responder(method, url, current, **options):
                    payload = json.loads(
                        options["encoded_body"].decode("utf-8"),
                    )
                    return client.JSONResponse(200, self.receipt(
                        session, payload["batch_id"], "applied",
                        revision=revision,
                    ))

                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ) as request, redirect_stdout(stdout), redirect_stderr(
                    io.StringIO(),
                ):
                    self.assertEqual(client.command_do(self.do_args(
                        "u1 move 32,72", str(session_path),
                    )), 0)
                printed = stdout.getvalue()
                self.assertEqual(request.call_count, 1)
                self.assertIn("1/1 applied rev7/t3", printed)
                self.assertNotIn("can (", printed)

    def test_v2_batch_refusal_prints_the_actor_options_too(self):
        """The single-action surface answers a refusal the same way `do` does."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                self.refused_seat(session_path, session, revision)

                def responder(method, url, current, **options):
                    payload = json.loads(
                        options["encoded_body"].decode("utf-8"),
                    )
                    return client.JSONResponse(200, self.receipt(
                        session, payload["batch_id"], "rejected",
                        revision=revision,
                    ))

                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    # A terminal receipt resolved the batch, so the exit code
                    # is unchanged; only the rendering gains the options.
                    self.assertEqual(client.command_batch(self.alias_args(
                        session=str(session_path), action_id="a1",
                    )), 0)
                lines = stdout.getvalue().splitlines()
                self.assertIn("rejected", lines[0])
                self.assertEqual(lines[1], "u1 can (rev7/t3): 2 options")
                self.assertTrue(lines[2].startswith("a1 "), lines[2])

    def test_v2_turn_end_await_ends_the_phase_then_blocks_then_heads(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                ended = self.revision(8)
                woke = self.revision(9)
                phase_end = self.pregame_action(
                    revision, "action_" + "e" * 26, "phase.end", "end",
                    "End phase", {"type": "object"}, None,
                )
                page = self.page(
                    session, legal=True, revision=revision,
                    items=[phase_end],
                )
                order: list[str] = []

                def responder(method, url, current, **options):
                    if method == "POST":
                        order.append("batch")
                        payload = json.loads(
                            options["encoded_body"].decode("utf-8"),
                        )
                        self.assertEqual(
                            payload["commands"][0],
                            {
                                "action_id": phase_end["action_id"],
                                "arguments": {},
                            },
                        )
                        return client.JSONResponse(200, self.receipt(
                            session, payload["batch_id"], "applied",
                            revision=ended,
                        ))
                    order.append("legal")
                    return client.JSONResponse(200, page)

                waking = self.wait_response(
                    session, "phase_active", active=True, revision=woke,
                )

                def wait(path, current, args, **options):
                    order.append("wait")
                    return waking

                args = type("Args", (), {
                    "session": str(session_path), "end_phase": True,
                    "await_phase": True, "wait_s": 120.0, "poll_s": 1.0,
                    "until": "phase",
                })()
                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ), patch.object(
                    client, "_wait_value", side_effect=wait,
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_turn(args), 0)

                # Enumerate the capability, execute it, block, then head.
                self.assertEqual(order, ["legal", "batch", "wait"])
                lines = stdout.getvalue().splitlines()
                self.assertEqual(len(lines), 2)
                self.assertTrue(lines[0].startswith("phase end → applied"))
                self.assertIn("rev8/t3", lines[0])
                self.assertIn("T3 rev9/t3", lines[1])
                self.assertIn("YOUR TURN · t3/p1", lines[1])
                self.assertIn("next: just turn", lines[1])

                # --await alone never ends a phase.
                lonely = type("Args", (), {
                    "session": str(session_path), "end_phase": False,
                    "await_phase": True,
                })()
                with patch.object(client, "_v2_response") as blocked:
                    with self.assertRaisesRegex(
                        client.PlayerError, "just turn --end --await",
                    ):
                        client.command_turn(lonely)
                blocked.assert_not_called()

    def test_v2_start_resolves_names_then_configures_then_readies(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                lobby = self.revision(4, turn=0)
                configured = self.revision(5, turn=0)
                ready_revision = self.revision(6, turn=0)
                nation = "nation_" + "a" * 32
                style = "style_" + "b" * 32
                nations = self.section_page(
                    session, section="pregame_nations", revision=lobby,
                    items=[
                        {
                            "id": nation, "name": "English",
                            "default_style_id": style,
                        },
                        {
                            "id": "nation_" + "c" * 32, "name": "Zulu",
                            "default_style_id": style,
                        },
                    ],
                )
                configure = self.pregame_action(
                    lobby, "action_" + "1" * 26, "pregame.configure",
                    "configure", "Choose nation, leader, sex, and style",
                    {
                        "type": "object",
                        "properties": {
                            "nation_id": {"type": "string"},
                            "leader_name": {"type": "string"},
                            "is_male": {"type": "boolean"},
                            "style_id": {"type": "string"},
                        },
                        "required": [
                            "nation_id", "leader_name", "is_male", "style_id",
                        ],
                    },
                    {"type": "pregame_configuration"},
                )
                set_ready = self.pregame_action(
                    configured, "action_" + "2" * 26, "pregame.set_ready",
                    "set_ready", "Mark ready",
                    {
                        "type": "object",
                        "properties": {
                            "ready": {"type": "boolean", "enum": [True]},
                        },
                        "required": ["ready"],
                    },
                    {"type": "pregame_readiness", "desired_ready": True},
                )
                catalogs = [
                    self.page(
                        session, legal=True, revision=lobby,
                        items=[configure],
                    ),
                    self.page(
                        session, legal=True, revision=configured,
                        items=[set_ready],
                    ),
                ]
                steps: list[str] = []
                bodies: list[dict] = []

                def responder(method, url, current, **options):
                    if "/health" in url:
                        steps.append("health")
                        return client.JSONResponse(200, self.health(
                            session, game_state="lobby",
                        ))
                    if "/state" in url:
                        steps.append("nations")
                        self.assertIn("section=pregame_nations", url)
                        return client.JSONResponse(200, nations)
                    if "legal-actions" in url:
                        steps.append("legal")
                        return client.JSONResponse(
                            200,
                            catalogs[
                                min(steps.count("legal"), len(catalogs)) - 1
                            ],
                        )
                    steps.append("batch")
                    payload = json.loads(
                        options["encoded_body"].decode("utf-8"),
                    )
                    bodies.append(payload)
                    return client.JSONResponse(200, self.receipt(
                        session, payload["batch_id"], "applied",
                        revision=(
                            configured if payload["state_revision"] == lobby
                            else ready_revision
                        ),
                    ))

                args = type("Args", (), {
                    "session": str(session_path), "nation": "eNgLiSh",
                    "leader": "Ada", "style": "", "male": False,
                    "female": True,
                })()
                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_start(args), 0)

                # Lobby check, catalog, enumerate, configure, RE-ENUMERATE,
                # ready: the refresh between the two steps is mandatory.
                self.assertEqual(steps, [
                    "health", "nations", "legal", "batch", "legal", "batch",
                ])
                self.assertEqual(bodies[0]["commands"][0], {
                    "action_id": configure["action_id"],
                    "arguments": {
                        "nation_id": nation, "leader_name": "Ada",
                        "is_male": False, "style_id": style,
                    },
                })
                self.assertEqual(bodies[1]["commands"][0], {
                    "action_id": set_ready["action_id"],
                    "arguments": {"ready": True},
                })
                self.assertEqual(bodies[1]["state_revision"], configured)
                lines = stdout.getvalue().splitlines()
                self.assertEqual(len(lines), 3)
                self.assertEqual(
                    lines[0],
                    "starting as English — Ada (female), style the nation "
                    "default",
                )
                self.assertTrue(
                    lines[1].startswith("configure English Ada female →"),
                    lines[1],
                )
                self.assertTrue(lines[2].startswith("set ready → applied"))

                # A nation that is not on the catalog is refused by name.
                missing = type("Args", (), {
                    "session": str(session_path), "nation": "Atlantean",
                    "leader": "Ada", "style": "", "male": True,
                    "female": False,
                })()
                with self.assertRaisesRegex(
                    client.PlayerError, "no nation named 'Atlantean'",
                ) as unknown:
                    with patch.object(
                        client, "_v2_response", side_effect=responder,
                    ):
                        client.command_start(missing)
                self.assertIn("English", str(unknown.exception))

                # Sex is optional but exclusive; the refusal precedes the
                # first request.
                with patch.object(client, "_v2_response") as blocked:
                    with self.assertRaisesRegex(
                        client.PlayerError, "--male or --female",
                    ):
                        client.command_start(type("Args", (), {
                            "session": str(session_path),
                            "nation": "English", "leader": "Ada",
                            "style": "", "male": True, "female": True,
                        })())
                blocked.assert_not_called()

    def test_v2_responses_are_mirrored_and_show_never_opens_a_socket(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                unit = "unit_" + "a" * 32
                tile = "tile_" + "a" * 32
                units = self.section_page(
                    session, section="units", revision=revision,
                    items=[self.unit_item(unit, tile, 31, 72)],
                )
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(200, units),
                ), redirect_stdout(io.StringIO()):
                    self.assertEqual(client.command_state(self.alias_args(
                        session=str(session_path), section="units",
                    )), 0)
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(
                        200, self.health(session, active=True),
                    ),
                ), redirect_stdout(io.StringIO()):
                    self.assertEqual(client.command_health(type("Args", (), {
                        "session": str(session_path),
                    })()), 0)
                move = self.actor_action(
                    revision, "action_" + "1" * 26, unit, x=32, y=72,
                )
                self.cache_actor_catalog(
                    session_path, session, revision, unit, [move],
                )

                # The mirror is a set of readable projections beside the
                # private session file, and never the private cache itself.
                mirror = client._mirror_path(session_path)
                self.assertEqual(mirror.name, session_path.stem)
                written = {
                    path.relative_to(mirror).as_posix()
                    for path in mirror.rglob("*") if path.is_file()
                }
                self.assertLessEqual(
                    {
                        "state/units.tsv", "state/header.txt",
                        "state/delta.md", "state/options/u1.txt",
                    },
                    written,
                )
                units_text = (mirror / "state" / "units.tsv").read_text(
                    encoding="utf-8",
                )
                self.assertTrue(units_text.startswith("# rev 7 turn 3"))
                self.assertIn("u1", units_text)
                self.assertIn("Settlers", units_text)
                unit_row = next(
                    line for line in units_text.splitlines()
                    if "Settlers" in line
                )
                self.assertNotIn(revision["state_token"], units_text)
                self.assertNotIn(session["agent_token"], units_text)
                options_text = (
                    mirror / "state" / "options" / "u1.txt"
                ).read_text(encoding="utf-8")
                self.assertIn("unit.order", options_text)

                def show(**values):
                    stdout = io.StringIO()
                    arguments = {
                        "session": str(session_path), "name": "", "grep": "",
                        "regex": False,
                    }
                    arguments.update(values)
                    with patch.object(
                        client, "_v2_response",
                    ) as blocked, redirect_stdout(stdout):
                        self.assertEqual(client.command_show(
                            type("Args", (), arguments)(),
                        ), 0)
                    # The one guarantee `show` sells: zero network.
                    blocked.assert_not_called()
                    return stdout.getvalue()

                listing = show()
                self.assertIn("files: header", listing)
                self.assertIn("options/u1", listing)
                self.assertIn("just turn", listing)
                self.assertEqual(show(name="units"), units_text)
                alias_view = show(name="u1")
                self.assertIn("units: u1", alias_view)
                self.assertIn("unit.order", alias_view)
                matched = show(grep="Settlers")
                self.assertIn("units:4:", matched)
                self.assertIn(
                    "no mirror line matches 'zzz-not-here'",
                    show(grep="zzz-not-here"),
                )

                # A name the mirror does not hold names its own remedy, and a
                # traversal attempt never reaches outside the mirror.
                with self.assertRaisesRegex(
                    client.PlayerError, r"just legal --actor_id u9 --all",
                ):
                    show(name="u9")
                for hostile in ("../codex-test.v2-state", "/etc/passwd"):
                    with self.assertRaisesRegex(
                        client.PlayerError, "one mirror file name",
                    ):
                        show(name=hostile)
                with self.assertRaisesRegex(
                    client.PlayerError, "not both",
                ):
                    show(name="units", grep="Settlers")

                # --grep is literal by default, so a pattern that would
                # backtrack catastrophically as a regex is just text that is
                # not in the mirror -- and it answers immediately.
                started = time.monotonic()
                self.assertIn(
                    "no mirror line matches",
                    show(grep="(a|aa)+$"),
                )
                self.assertLess(time.monotonic() - started, 1.0)
                # Literal means literal: a metacharacter stands for itself,
                # so the pattern that only a regex engine could match finds
                # nothing until --regex is passed.
                self.assertIn("Settlers", unit_row)
                self.assertIn(
                    "no mirror line matches", show(grep="Settl.rs"),
                )

                # --regex opts into the engine and keeps both guards.
                self.assertIn("units:4:", show(grep="Settl.rs", regex=True))
                with self.assertRaisesRegex(
                    client.PlayerError, r"already quantified group",
                ):
                    show(grep="(a+)+$", regex=True)
                with patch.object(client, "V2_SHOW_GREP_BUDGET_S", -1.0):
                    with self.assertRaisesRegex(
                        client.PlayerError, r"took too long; narrow the",
                    ):
                        show(grep="(a|aa)+$", regex=True)
                with self.assertRaisesRegex(
                    client.PlayerError, r"drop --regex",
                ):
                    show(grep="(unbalanced", regex=True)
                with self.assertRaisesRegex(
                    client.PlayerError, r"needs a --grep PATTERN",
                ):
                    show(regex=True)

    # ------------------------------------------------------------------
    # Docs and the join protocol card (doc §5/P2.7, §6).
    # ------------------------------------------------------------------

    # The owner workspace, not this one, defines these recipes.
    OWNER_RECIPES = frozenset({"invite", "single", "multi"})

    @staticmethod
    def client_recipes() -> dict[str, frozenset[str]]:
        """Map each subcommand to its argparse long options.

        The justfile veneer died in the play-cli cutover; the client's own
        argparse tree (which accepts both `--wait_s` and `--wait-s`) is now the
        one vocabulary the agent-facing docs may draw from.  `help` and
        `rules` survive as workspace commands on the shipped `./play` binary.
        """
        subcommands = next(
            action for action in client.parser()._actions
            if isinstance(action, argparse._SubParsersAction)
        ).choices
        recipes = {
            name: frozenset(
                spelling
                for action in command._actions
                for string in action.option_strings
                # The shipped `./play` binary accepts both spellings of every
                # long option (the veneer's underscore translation moved into
                # it), so the docs may use either.
                for spelling in (string, "--" + string[2:].replace("-", "_")
                                 if string.startswith("--") else string)
            )
            for name, command in subcommands.items()
        }
        recipes["help"] = frozenset()
        recipes["rules"] = frozenset()
        return recipes

    @staticmethod
    def command_snippets(text: str, *, markdown: bool) -> list[str]:
        """Return the command-shaped spans of a document, prose excluded."""
        if not markdown:
            return text.splitlines()
        snippets: list[str] = []
        fenced = False
        for line in text.splitlines():
            if line.startswith("```"):
                fenced = not fenced
                continue
            if fenced:
                snippets.append(line)
            else:
                snippets.extend(re.findall(r"`([^`]+)`", line))
        return snippets

    def assert_documented_commands_exist(
        self, label: str, text: str, recipes: dict[str, frozenset[str]],
        *, markdown: bool = True,
    ) -> int:
        checked = 0
        for snippet in self.command_snippets(text, markdown=markdown):
            names = re.findall(r"\bjust ([a-z][a-z_0-9]*)", snippet)
            if not names:
                continue
            checked += 1
            allowed: set[str] = set()
            for name in names:
                if name in self.OWNER_RECIPES:
                    continue
                self.assertIn(
                    name, recipes, f"{label}: no `just {name}` recipe: {snippet}",
                )
                allowed |= recipes[name]
            for flag in sorted(set(
                re.findall(r"(?<![\w-])--[a-z][a-z0-9_-]*", snippet)
            )):
                self.assertIn(
                    flag, allowed,
                    f"{label}: {names} has no option {flag}: {snippet}",
                )
        return checked

    # ---- rendered-surface budget gate (redesign doc §7) -------------------
    #
    # §7 makes context cost a first-class eval metric: "a harness regression
    # that doubles turn cost should fail CI the same way a scoring bug does".
    # These are upper bounds on the *rendered* surface for the doc's own §8
    # fixtures, so re-adding a dropped opaque-ID column, a per-item revision
    # block, or a default-valued field fails here instead of on a live seat.
    # Ratchet them down as the renderers get tighter; never up.
    BUDGET_ACTOR_CATALOG = 1400   # 21-action settler catalog (§8 call 4)
    BUDGET_BRIEFING = 500         # 5-unit turn briefing (§8 call 3)
    BUDGET_RECEIPT = 120          # one applied order (§8 call 5, per line)

    ACTOR_UNIT = "unit_" + "a" * 32

    @classmethod
    def settler_catalog(cls) -> tuple[dict, dict, dict]:
        """The doc §8 settler menu: 21 actions, one actor, all legal."""
        revision = cls.revision(8)
        items: list[dict] = []
        aliases = {cls.ACTOR_UNIT: "u1"}

        def subject(operation: str, **extra) -> dict:
            base = {
                "actor": {"type": "unit", "id": cls.ACTOR_UNIT},
                "target": None,
                "operation": operation,
                "variant": None,
                "consuming": False,
                "legality": "legal",
                "probability": {
                    "kind": "exact", "minimum_percent": 100,
                    "maximum_percent": 100,
                },
            }
            base.update(extra)
            return base

        def add(index, kind, label, body, schema=None) -> None:
            action_id = "action_" + f"{index:032d}"
            items.append(cls.rendered_descriptor(
                revision, action_id, kind=kind, label=label,
                subject=body, schema=schema,
            ))
            aliases[action_id] = f"a{index}"

        def tile(index, x, y) -> dict:
            return {
                "type": "tile", "id": "tile_" + f"{index:032d}",
                "x": x, "y": y,
            }

        add(
            1, "unit.found_city", "Found City",
            subject("found_city", consuming=True),
            {
                "type": "object",
                "properties": {"name": {"type": "string"}},
                "required": ["name"],
            },
        )
        for offset, order in enumerate((
            "cultivate", "plant", "mine", "irrigate", "road", "fortify",
            "sentry", "pillage", "disband",
        )):
            add(
                2 + offset, "unit.order", order.capitalize(),
                subject("order", order=order),
            )
        for offset, (x, y) in enumerate((
            (30, 72), (31, 71), (31, 73), (32, 72),
            (30, 71), (32, 71), (30, 73), (32, 73),
        )):
            add(
                11 + offset, "unit.order", "Move",
                subject("order", order="move", target=tile(offset, x, y)),
            )
        for offset, (x, y) in enumerate(((29, 72), (28, 70), (34, 75))):
            add(
                19 + offset, "unit.order", "Go to",
                subject(
                    "order", order="goto", target=tile(50 + offset, x, y),
                ),
            )
        compacts = [client._compact_legal_action(item) for item in items]
        result = {
            "schema_version": 1, "command": "legal", "kind": None,
            "state_revision": revision, "catalog_total": len(items),
            "pages_read": 2, "matched": len(items), "offset": 0,
            "limit": client.V2_LEGAL_ACTOR_MATCH_LIMIT,
            "shown": len(items), "truncated": False, "has_more": False,
            "next_offset": None, "byte_limited": False,
            "oversized_single": False, "actions": compacts,
        }
        scope = {"actor_id": cls.ACTOR_UNIT, "actor_type": "unit"}
        return result, scope, aliases

    def test_the_rendered_surface_stays_inside_its_context_budget(self):
        """§7's regression gate: a render that doubles in size fails CI."""
        result, scope, aliases = self.settler_catalog()
        catalog = "\n".join(
            client._render_legal_compact(result, scope, aliases)
        )
        self.assertLessEqual(
            len(catalog), self.BUDGET_ACTOR_CATALOG,
            f"a 21-action actor catalog renders {len(catalog)} chars:\n"
            + catalog,
        )
        # Every row still resolves: the alias is the handle, and the 32-hex
        # opaque ID it replaced is what the budget buys back.
        rows = catalog.splitlines()[1:]
        self.assertEqual(len(rows), 21)
        for index, row in enumerate(rows, start=1):
            self.assertTrue(row.startswith(f"a{index} "), row)
            self.assertNotIn("action_" + "0" * 20, row)
        self.assertLess(
            len(catalog), len(json.dumps(result)) // 4,
            "the compact catalog must be far smaller than its --json form",
        )

        briefing = "\n".join(client._render_turn(self.briefing_result()))
        self.assertLessEqual(
            len(briefing), self.BUDGET_BRIEFING,
            f"a 5-unit briefing renders {len(briefing)} chars:\n" + briefing,
        )

        session = {"game_id": self.GAME_ID, "agent_id": self.AGENT_ID}
        receipt = client._validate_receipt(
            self.receipt(session, "batch_" + "A" * 24, "applied"), session,
        )
        for line in client._render_receipt(receipt, "u1 found_city London"):
            self.assertLessEqual(
                len(line), self.BUDGET_RECEIPT, line,
            )

    @classmethod
    def briefing_result(cls) -> dict:
        """The doc §8 turn-3 briefing: five units, one tile, no cities."""
        revision = cls.revision(8, turn=1)
        tile = "tile_" + "b" * 32

        def unit(index, kind, moves, activity) -> dict:
            return {
                "id": f"unit_{index}" + "0" * 30, "scope": "own",
                "type": kind, "tile_id": tile, "x": 31, "y": 72,
                "hp": 20, "moves": moves,
                "type_stats": {"max_hp": 20, "move_rate": moves},
                "activity": {"name": activity},
                "automation": {"controller": "player", "has_orders": False},
                "route": None,
            }

        units = [
            unit(1, "Settlers", 3, "idle"), unit(2, "Settlers", 3, "idle"),
            unit(3, "Workers", 3, "idle"), unit(4, "Workers", 3, "idle"),
            unit(5, "Explorer", 9, "idle"),
        ]
        return {
            "schema_version": 1, "command": "turn", "status": "ready",
            "state_revision": revision,
            "context": {
                "game_state": "running", "objective": "score",
                "max_turns": 5000, "turns_remaining": 4999,
                "agent": {
                    "agent_id": cls.AGENT_ID,
                    "controller_label": "codex-test-model",
                },
                "seat": {
                    "place": 1, "seat_id": "place-1",
                    "player_name": "AgentPlace1",
                },
                "sidecar": {"state": "ready", "generation": 1},
                "observation_available": True,
                "legal_actions_available": True,
                "phase": {
                    "state": "awaiting_agent", "turn": 1, "phase": 0,
                    "active": True,
                    "timing": {
                        "mode": "default", "timeout_s": 180,
                        "deadline_started_at": 1000.0, "deadline_at": 1180.0,
                        "elapsed_s": 1.0, "remaining_s": 179.0,
                    },
                },
                "last_phase_end": None,
            },
            "overview": {
                "client_state": "running", "turn": 1, "phase": 0,
                "phase_count": 1,
                "player": {
                    "name": "Ada", "nation": "English",
                    "government": "Despotism",
                    "economy": {
                        "gold": 50, "tax": 40, "science": 60, "luxury": 0,
                    },
                },
                "research": {
                    "target": "Bronze Working", "bulbs_researched": 0,
                    "cost": 28, "output": 3, "goal": None,
                },
                "counts": {"units": 5, "cities": 0},
            },
            "cities": {
                "shown": 0, "total": 0, "truncated": False, "items": [],
                "next_cursor": None, "cursor_expires_at": None,
            },
            "units": {
                "shown": 5, "total": 5, "truncated": False, "items": units,
                "next_cursor": None, "cursor_expires_at": None,
            },
            "research": {
                "shown": 0, "total": 0, "truncated": False, "items": [],
                "next_cursor": None, "cursor_expires_at": None,
            },
            "next_commands": [],
        }

    def test_a_section_without_a_dedicated_renderer_still_compacts(self):
        """24 of 30 sections fall through to the generic table; it must earn
        its place against `--json` rather than merely reformatting it."""
        city = "city_" + "c" * 32
        items = [
            {
                "id": "choice_" + f"{index:026d}", "city_id": city,
                "kind": "unit" if index % 2 else "improvement",
                "name": f"Choice {index}",
                "shield_cost": 10 + index,
                "shield_stock": 4,
                "turns": 2 + index,
                "can_buy": index % 3 == 0,
                "buy_cost": 40 + index,
                "upkeep": {
                    "food": 0, "shield": 0, "gold": 0,
                    "luxury": 0, "science": 0, "trade": 0,
                },
                "stats": {"attack": index % 4, "defense": 1},
            }
            for index in range(16)
        ]
        lines = client._render_generic_items(items)
        text = "\n".join(lines)
        raw = json.dumps(items, sort_keys=True, separators=(",", ":"))
        self.assertLess(
            len(text), len(raw) * 4 // 10,
            f"the generic renderer emitted {len(text)} chars against "
            f"{len(raw)} of --json:\n{text}",
        )
        # A column identical on every row is a page constant, printed once.
        self.assertIn("constants: ", lines[0])
        self.assertIn(city, lines[0])
        self.assertEqual(sum(line.count(city) for line in lines), 1)
        # A nested object is flattened, never JSON-dumped into a cell.
        self.assertNotIn("{", text)
        self.assertIn("stats.attack", text)
        # A nested column that is zero on every row is stated once, not
        # repeated on all 16 rows.
        self.assertEqual(sum(line.count("upkeep.food") for line in lines), 1)
        self.assertIn("upkeep.food=0", lines[0])

    def test_a_terrain_code_means_the_same_terrain_on_every_page(self):
        """A glyph that changes meaning between pages routes a unit into sea."""
        alone = client._terrain_codes({"Desert"})
        crowded = client._terrain_codes({"Deep Ocean", "Desert", "Forest"})
        self.assertEqual(alone["Desert"], crowded["Desert"])
        self.assertNotEqual(crowded["Deep Ocean"], crowded["Desert"])
        # Two pages of one drain must agree glyph for glyph.
        first = client._terrain_codes({"Grassland", "Hills", "Ocean"})
        second = client._terrain_codes(
            {"Grassland", "Hills", "Ocean", "Glacier", "Jungle", "Swamp"},
        )
        for name, code in first.items():
            self.assertEqual(second[name], code, name)
        self.assertEqual(len(set(second.values())), len(second))
        # An unlisted terrain is still deterministic from its own name.
        self.assertEqual(
            client._terrain_codes({"Wasteland"})["Wasteland"],
            client._terrain_codes({"Wasteland", "Desert"})["Wasteland"],
        )

    def test_a_schema_enum_prints_the_literal_the_wire_accepts(self):
        """`{ready:yes}` is a value the server rejects; print JSON literals."""
        summary = client._schema_summary({
            "type": "object",
            "properties": {"ready": {"type": "boolean", "enum": [True]}},
            "required": ["ready"],
        })
        self.assertIn("true", summary)
        self.assertNotIn("yes", summary)
        # A genuine string enum stays distinguishable from the boolean one.
        strings = client._schema_summary({
            "type": "object",
            "properties": {"vote": {"type": "string", "enum": ["yes", "no"]}},
            "required": ["vote"],
        })
        self.assertIn('"yes"', strings)
        self.assertNotEqual(summary, strings)
        nullable = client._schema_summary({
            "type": "object",
            "properties": {"goal": {"enum": [None]}},
        })
        self.assertIn("null", nullable)

    def test_the_agent_facing_doc_surface_stays_inside_its_budget(self):
        """§6: every doc char an agent reads each game is a per-turn cost.

        `help` prints `docs/play.md` and `rules` prints `docs/gameplay.md`
        (the shipped `./play` binary and the Python client agree), so both
        documents are budgeted like payloads.  The full reference stays
        available for harness authors and is deliberately not what `help`
        prints.
        """
        budgets = {"docs/play.md": 4096, "docs/gameplay.md": 8192}
        for relative, budget in budgets.items():
            document = client.ROOT / relative
            self.assertTrue(document.is_file(), relative)
            text = document.read_text(encoding="utf-8")
            self.assertLessEqual(
                len(text), budget,
                f"help/rules prints {len(text)} chars of {relative}; "
                f"the agent-facing budget is {budget}",
            )

        card = client.ROOT / "docs" / "play.md"
        played = card.read_text(encoding="utf-8")
        for fast_path in (
            "just start ", "just turn ", "just do ", "just turn --end --await",
            "just show", "--json",
        ):
            self.assertIn(fast_path, played, fast_path)
        # The play card points at the reference; it never inlines it.
        self.assertIn("docs/commands.md", played)
        reference = (
            client.ROOT / "docs" / "commands.md"
        ).read_text(encoding="utf-8")
        self.assertIn("play.md", reference)
        self.assertIn("harness authors", reference)

    def test_documented_commands_and_flags_all_exist(self):
        """Nothing the agent reads may name a command or flag we do not have."""
        recipes = self.client_recipes()
        self.assertLessEqual(
            {
                "join", "start", "turn", "do", "show", "state", "legal",
                "batch", "receipt", "retry", "wait", "health", "result",
            },
            set(recipes),
        )

        # The join protocol card and both agent-facing docs name only commands
        # and options that exist.
        self.assertGreaterEqual(
            self.assert_documented_commands_exist(
                "protocol card", "\n".join(client.V2_PROTOCOL_CARD), recipes,
                markdown=False,
            ),
            8,
        )
        for name in (
            "play.md", "commands.md", "full-control-v2.md", "gameplay.md",
        ):
            document = (client.ROOT / "docs" / name).read_text(encoding="utf-8")
            self.assertGreater(
                self.assert_documented_commands_exist(name, document, recipes),
                0,
            )

    def test_workspace_boundary_docs_teach_the_v2_fast_paths(self):
        """AGENTS.md/README.md are read before join and must not contradict it.

        Both files predate the redesign; the failure they caused was a *third*
        protocol contract that forbade the fast paths outright and sent the
        agent to the 28k-char harness-author reference.
        """
        recipes = self.client_recipes()
        for name in ("AGENTS.md", "README.md"):
            document = (client.ROOT / name).read_text(encoding="utf-8")
            self.assert_documented_commands_exist(name, document, recipes)
            for fast_path in ("start", "turn", "do", "show"):
                self.assertRegex(
                    document,
                    rf"`(just )?{fast_path}`|`just {fast_path} ",
                    f"{name} never names the {fast_path} fast path",
                )
            self.assertIn("just help", document)
            # The agent-facing card, not the harness-author reference, is the
            # command contract these files point at.
            self.assertNotRegex(
                document,
                r"command help is in `docs/commands\.md`",
            )
        agents = (client.ROOT / "AGENTS.md").read_text(encoding="utf-8")
        self.assertRegex(agents, r"harness-author\s+reference")
        self.assertNotIn(
            "use only `health`/`state`/`legal`", agents,
        )
        for invariant in (
            "exact state revision",
            "receipt first",
            "ambiguous",
        ):
            self.assertIn(invariant, agents)

    def test_every_taught_surface_numbers_join_as_the_first_step(self):
        """A live agent ran `just start` blind because nothing said join ran first.

        The bare menu, AGENTS.md and the play card are the only three places
        it can read before the first command, so all three must open with the
        same numbered order and agree on which step comes second.
        """
        order = ("just join", "just start", "just turn", "just do")
        surfaces = {
            "AGENTS.md": (
                client.ROOT / "AGENTS.md"
            ).read_text(encoding="utf-8"),
            "docs/play.md": (
                client.ROOT / "docs" / "play.md"
            ).read_text(encoding="utf-8"),
        }
        for name, text in surfaces.items():
            positions = [text.find(step) for step in order]
            for step, position in zip(order, positions):
                self.assertNotEqual(position, -1, f"{name} never names {step}")
            self.assertEqual(
                positions, sorted(positions),
                f"{name} teaches the loop out of order: "
                f"{dict(zip(order, positions))}",
            )
            # The steps are numbered, not just ordered: an agent that skims
            # reads "1." and runs it.
            self.assertRegex(
                text, r"1\.\s+just join",
                f"{name} does not number `just join` as step 1",
            )
            self.assertRegex(
                text, r"2\.\s+just start",
                f"{name} does not number `just start` as step 2",
            )
        # `just start` collides with the repository stack command by name, so
        # every surface that teaches it must disambiguate.
        for name in ("AGENTS.md", "docs/play.md"):
            self.assertIn(
                "repository stack", surfaces[name],
                f"{name} never distinguishes `just start` from the stack's",
            )

    def test_a_preconfigured_workspace_answers_every_v2_command_with_join(self):
        """`just play` workspaces need `just join`, not the generic join form.

        Printing `just join --game_id ... --name ...` here teaches a command
        line the agent cannot fill in, at the one moment it has no other
        source of truth.
        """
        commands = (
            "start", "turn", "show", "state", "legal", "health", "wait", "do",
            "batch", "receipt", "retry", "use",
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            (root / ".sessions").mkdir(parents=True)
            (root / ".playconfig.json").write_text(
                json.dumps({
                    "schema_version": 1, "game_id": self.GAME_ID,
                    "name": "codex-test-model", "place": None,
                    "control_protocol": "full-control-v2",
                }),
                encoding="utf-8",
            )
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                for name in commands:
                    with self.subTest(command=name):
                        with self.assertRaises(client.PlayerError) as caught:
                            client._session_path("") if name != "use" else (
                                client.command_use(
                                    argparse.Namespace(target="", json=False),
                                )
                            )
                        message = str(caught.exception)
                        self.assertIn("`just join`", message)
                        self.assertIn(self.GAME_ID, message)
                        self.assertNotIn("--game_id", message)
                        self.assertNotIn("multiple private sessions", message)
                # Without the config the generic remedy is still correct.
                (root / ".playconfig.json").unlink()
                with self.assertRaises(client.PlayerError) as caught:
                    client._session_path("")
                self.assertIn("--game_id", str(caught.exception))

    def test_v2_health_survives_an_additive_server_field(self):
        """The supervisor gained `last_recovery`; every command died on it.

        A closed schema that fails the whole surface on one unknown key is a
        worse failure than the drift it detects, so an optional field is
        accepted and any drift names itself.
        """
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                _path, session = self.v2_session(root)
        recovery = {
            "attempt": 1,
            "client_state": "running",
            "exit_code": None,
            "exit_signal": None,
            "format": "freeciv-full-control-v2-recovery",
            "game_id": self.GAME_ID,
            "kind": "sidecar_reattach",
            "outcome": "recovered",
            "place": 1,
            "recovered_to_turn": 51,
            "rewound_applied_actions": True,
            "schema_version": 1,
            "seat_id": "place-1",
            "sidecar_generation": 2,
            "timestamp": "2026-08-05T09:49:19.508Z",
            "trigger": "sidecar_exit",
            "turn": 52,
        }
        absent = self.health(session, active=True)
        self.assertNotIn("last_recovery", client._validate_health(
            absent, session,
        ))

        null = dict(absent, last_recovery=None)
        self.assertIsNone(
            client._validate_health(null, session)["last_recovery"],
        )

        populated = dict(absent, last_recovery=recovery)
        clean = client._validate_health(populated, session)
        self.assertEqual(clean["last_recovery"], recovery)
        rendered = "\n".join(client._render_health(clean))
        self.assertIn("last recovery t52 sidecar_reattach recovered", rendered)
        self.assertIn("rolled back", rendered)

        for broken in (
            dict(recovery, kind="teleport"),
            dict(recovery, outcome="fine"),
            dict(recovery, place=2),
            {key: value for key, value in recovery.items() if key != "turn"},
        ):
            with self.assertRaises(client.PlayerError):
                client._validate_health(
                    dict(absent, last_recovery=broken), session,
                )

    def test_a_drifted_schema_names_the_field_that_drifted(self):
        """Listing the expected fields alone leaves nothing to act on."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                _path, session = self.v2_session(root)
        drifted = dict(self.health(session, active=True), invented_field=1)
        with self.assertRaises(client.PlayerError) as caught:
            client._validate_health(drifted, session)
        self.assertIn("unexpected invented_field", str(caught.exception))

        truncated = self.health(session, active=True)
        del truncated["sidecar"]
        with self.assertRaises(client.PlayerError) as caught:
            client._validate_health(truncated, session)
        self.assertIn("missing sidecar", str(caught.exception))

    def test_play_do_takes_its_orders_positionally_like_just_do(self):
        """`./play do "…"` is advertised by the wrapper and must parse."""
        parsed = client.parser()
        positional = parsed.parse_args(["do", "u1 found_city London"])
        self.assertEqual(positional.positional_orders, ["u1 found_city London"])
        flagged = parsed.parse_args(["do", "--orders", "u1 found_city London"])
        self.assertEqual(flagged.orders, "u1 found_city London")
        self.assertEqual(flagged.positional_orders, [])

    def test_v2_join_card_and_state_header_carry_the_same_contract(self):
        """Join teaches the protocol once; header.txt repeats it for free."""
        card = client.V2_PROTOCOL_CARD
        self.assertTrue(card[0].startswith("ALIASES"))
        self.assertIn("dies with its revision", card[0])
        self.assertIn("the wire carries the server's opaque ID", card[0])
        self.assertTrue(card[1].startswith("ERRORS carry their own remedy"))
        self.assertNotIn("state_token", "\n".join(card))

        joined = client._render_join(
            {
                "game_id": self.GAME_ID,
                "controller_label": "codex-test-model",
                "place": 1, "player_name": "AgentPlace1",
                "control_protocol": "full-control-v2",
                "timing_mode": "default", "action_timeout_s": 180,
            },
            {"state": "lobby"},
            Path(".sessions/x/codex-test.json"),
        )
        for line in card:
            self.assertIn(line, joined)
        self.assertTrue(any(
            "state/header.txt" in line for line in joined
        ))
        # strategic-v1 seats never see the v2 card.
        strategic = client._render_join(
            {
                "game_id": self.GAME_ID,
                "controller_label": "codex-test-model",
                "place": 1, "player_name": "AgentPlace1",
                "control_protocol": "strategic-v1",
                "timing_mode": "default", "action_timeout_s": 180,
            },
            {"state": "lobby"},
            Path(".sessions/x/codex-test.json"),
        )
        self.assertNotIn(card[0], strategic)

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                args = type("Args", (), {"session": str(session_path)})()
                stderr = io.StringIO()
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(
                        200, self.health(session, active=True),
                    ),
                ), redirect_stdout(io.StringIO()), redirect_stderr(stderr):
                    self.assertEqual(client.command_health(args), 0)
                self.assertEqual(stderr.getvalue(), "")
                header = (
                    client._mirror_path(session_path) / "state" / "header.txt"
                ).read_text(encoding="utf-8")
                for line in card:
                    self.assertIn(line, header)
                self.assertNotIn(session["agent_token"], header)
                self.assertNotIn("state_token", header)

    # ------------------------------------------------------------------
    # I6: global-catalog grouping, the focus loop, semantic alias
    # continuity, map overlays, and zero-argument start (redesign doc §9).
    # ------------------------------------------------------------------

    PLAYER = "player_" + "f" * 32

    @classmethod
    def player_action(
        cls, revision: dict, action_id: str, *, kind: str, operation: str,
        label: str, target: dict | None = None,
        probability: dict | None = None, subject_extra: dict | None = None,
    ) -> dict:
        subject = {
            "operation": operation,
            "actor": {"id": cls.PLAYER, "type": "player"},
            "target": target,
            "probability": probability or {
                "kind": "exact", "minimum_percent": 100,
                "maximum_percent": 100,
            },
        }
        subject.update(subject_extra or {})
        return {
            "action_id": action_id,
            "kind": kind,
            "label": label,
            "subject": subject,
            "arguments_schema": {"type": "object"},
            "state_revision": revision,
        }

    @classmethod
    def global_catalog(cls) -> tuple[dict, dict]:
        """A 147-row-shaped global catalog in miniature: the §9.1 fixture."""
        revision = cls.revision(11)
        session = {"game_id": cls.GAME_ID, "agent_id": cls.AGENT_ID}
        items = [
            cls.player_action(
                revision, f"action_setting{index:025d}",
                kind="player.propose_server_setting",
                operation="propose_server_setting",
                label=f"Propose setting {index}",
                subject_extra={"setting": f"setting{index}"},
            )
            for index in range(6)
        ]
        # One proposal is a gamble; it must survive the collapse individually.
        items.append(cls.player_action(
            revision, "action_gamble" + "0" * 20,
            kind="player.propose_server_setting",
            operation="propose_server_setting",
            label="Propose fixedlength",
            subject_extra={"setting": "fixedlength"},
            probability={
                "kind": "unknown", "minimum_percent": 0,
                "maximum_percent": 100,
            },
        ))
        items.extend(
            cls.player_action(
                revision, f"action_goal{index:028d}",
                kind="research.set_goal", operation="set_goal",
                label=f"Goal {name}",
                target={
                    "type": "research", "id": f"research_{index:032d}",
                    "name": name,
                },
            )
            for index, name in enumerate(
                ("Alphabet", "Bronze Working", "Currency"),
            )
        )
        items.append(cls.player_action(
            revision, "action_rates" + "0" * 21,
            kind="economy.set_rates", operation="set_rates",
            label="Set tax rates",
        ))
        page = cls.page(session, legal=True, revision=revision, items=items)
        return client._validate_page(page, session, legal=True), session

    def test_v2_global_catalog_groups_families_and_full_prints_them_flat(self):
        validated, _session = self.global_catalog()

        grouped = client._render_legal_page(validated)
        body = grouped[1:]
        # Six plain proposals become one governance line that names its own
        # drill-down; the seventh is a gamble and prints as its own row.
        governance = [line for line in body if line.startswith("governance:")]
        self.assertEqual(len(governance), 1)
        self.assertEqual(
            governance[0],
            "governance: 6 setting proposals — just legal --kind "
            "player.propose_server_setting --all",
        )
        gamble = [line for line in body if "!prob=0-100%/unknown" in line]
        self.assertEqual(len(gamble), 1, body)
        self.assertIn("Propose fixedlength", gamble[0])
        # Research goals collapse to one row that still names every choice.
        goals = [
            line for line in body if line.startswith("research.set_goal:")
        ]
        self.assertEqual(len(goals), 1)
        for name in ("Alphabet", "Bronze Working", "Currency"):
            self.assertIn(name, goals[0])
        self.assertIn("3 choices", goals[0])
        # A gameplay family is never collapsed.
        self.assertTrue(
            any("economy.set_rates" in line and "choices" not in line
                for line in body),
            body,
        )
        self.assertTrue(body[-1].endswith("add --full for the flat list"))

        flat = client._render_legal_page(validated, full=True)
        self.assertEqual(len(flat), 12)
        for index, line in enumerate(flat[1:], start=1):
            self.assertTrue(line.startswith(f"a{index} "), line)
        self.assertNotIn("governance:", "\n".join(flat))
        # Grouping is rendering only: the same page, same rows, fewer lines.
        self.assertLess(len("\n".join(grouped)), len("\n".join(flat)))

    def test_v2_scoped_catalog_rendering_is_untouched_by_grouping(self):
        revision = self.revision(11)
        session = {"game_id": self.GAME_ID, "agent_id": self.AGENT_ID}
        actor = "unit_" + "a" * 32
        items = [
            self.actor_action(
                revision, f"action_move{index:027d}", actor,
                x=30 + index, y=72,
            )
            for index in range(3)
        ]
        page = self.scoped_legal_page(
            session, revision=revision, items=items, actor_id=actor,
        )
        validated = client._validate_page(page, session, legal=True)
        lines = client._render_legal_page(validated)
        self.assertEqual(len(lines), 4)
        for index, line in enumerate(lines[1:], start=1):
            self.assertTrue(line.startswith(f"a{index} "), line)

    def test_v2_unit_rows_carry_a_route_summary_everywhere(self):
        revision = self.revision(9)
        tile = "tile_" + "b" * 32
        walking = {
            "id": "unit_" + "3" * 32, "scope": "own", "type": "Workers",
            "tile_id": tile, "x": 31, "y": 72, "hp": 10, "moves": 2,
            "type_stats": {"max_hp": 10, "move_rate": 3},
            "activity": {"name": "idle"},
            "automation": {"controller": "player", "has_orders": True},
            "route": {
                "mode": "goto", "order_count": 5, "path_available": True,
                "path_step_count": 3,
                "destination": {"tile_id": tile, "x": 40, "y": 60},
            },
        }
        self.assertIn("→(40,60) 3st", " ".join(client._unit_row("u3", walking)))
        self.assertIn("→(40,60) 3st", client._unit_status(walking))
        # A patrol says so; an unreconstructable path falls back to the
        # queued order count rather than inventing a step number.
        walking["route"]["mode"] = "patrol"
        walking["route"]["path_available"] = False
        self.assertIn("→(40,60) 5st patrol", client._unit_status(walking))
        # A unit with no route says nothing about one.
        idle = dict(walking, route=None)
        self.assertNotIn("→", client._unit_status(idle))

    UNIT_ONE = "unit_" + "1" * 32
    UNIT_TWO = "unit_" + "2" * 32
    CITY_ONE = "city_" + "1" * 32
    TILE_ONE = "tile_" + "1" * 32

    @classmethod
    def focus_units(cls) -> list[dict]:
        return [
            {
                "id": cls.UNIT_ONE, "scope": "own", "type": "Settlers",
                "tile_id": cls.TILE_ONE, "x": 31, "y": 72, "hp": 20,
                "moves": 3, "type_stats": {"max_hp": 20, "move_rate": 3},
                "activity": {"name": "idle"},
                "automation": {"controller": "player", "has_orders": False},
                "route": None,
            },
            {
                "id": cls.UNIT_TWO, "scope": "own", "type": "Workers",
                "tile_id": cls.TILE_ONE, "x": 31, "y": 72, "hp": 10,
                "moves": 2, "type_stats": {"max_hp": 10, "move_rate": 2},
                "activity": {"name": "idle"},
                "automation": {"controller": "player", "has_orders": False},
                "route": None,
            },
            {
                # Walking somewhere, so it needs no decision this phase.
                "id": "unit_" + "3" * 32, "scope": "own", "type": "Explorer",
                "tile_id": cls.TILE_ONE, "x": 31, "y": 72, "hp": 10,
                "moves": 3, "type_stats": {"max_hp": 10, "move_rate": 3},
                "activity": {"name": "idle"},
                "automation": {"controller": "player", "has_orders": True},
                "route": {
                    "mode": "goto", "order_count": 4, "path_available": True,
                    "path_step_count": 4,
                    "destination": {"tile_id": cls.TILE_ONE, "x": 40, "y": 60},
                },
            },
        ]

    @classmethod
    def focus_cities(cls) -> list[dict]:
        return [{
            "id": cls.CITY_ONE, "name": "London", "x": 31, "y": 72,
            "size": 1, "production": None,
            "surplus": {"food": 2, "shields": 1, "trade": 0},
        }]

    def stage_focus_seat(
        self, session_path: Path, session: dict, revision: dict,
    ) -> None:
        """Ingest one units page, one cities page, and two actor catalogs."""
        pages = {
            "units": self.section_page(
                session, section="units", revision=revision,
                items=self.focus_units(),
            ),
            "cities": self.section_page(
                session, section="cities", revision=revision,
                items=self.focus_cities(),
            ),
        }
        for section, payload in pages.items():
            with patch.object(
                client, "_v2_response",
                return_value=client.JSONResponse(200, payload),
            ), redirect_stdout(io.StringIO()):
                self.assertEqual(client.command_state(self.alias_args(
                    session=str(session_path), section=section,
                )), 0)
        self.cache_actor_catalog(
            session_path, session, revision, self.UNIT_ONE,
            [
                self.actor_action(
                    revision, "action_found" + "0" * 20, self.UNIT_ONE,
                    kind="unit.found_city", operation="found_city",
                    label="Found city",
                ),
                self.actor_action(
                    revision, "action_move1" + "0" * 20, self.UNIT_ONE,
                    x=31, y=72,
                ),
            ],
        )
        self.cache_actor_catalog(
            session_path, session, revision, self.UNIT_TWO,
            [
                self.actor_action(
                    revision, "action_sentry" + "0" * 19, self.UNIT_TWO,
                    kind="unit.sentry", operation="sentry", label="Sentry",
                ),
                self.actor_action(
                    revision, "action_road0" + "0" * 20, self.UNIT_TWO,
                    kind="unit.start_activity", operation="road",
                    label="Build road",
                ),
            ],
        )

    def test_v2_do_ends_with_one_focus_line_that_skips_what_it_ordered(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                self.stage_focus_seat(session_path, session, revision)
                sent = 0

                def responder(method, url, current, **options):
                    nonlocal sent
                    sent += 1
                    body = json.loads(options["encoded_body"].decode("utf-8"))
                    return client.JSONResponse(200, self.receipt(
                        session, body["batch_id"], "applied",
                        revision=revision,
                    ))

                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_do(self.do_args(
                        "u1 move 31,72", str(session_path),
                    )), 0)
                lines = stdout.getvalue().splitlines()
                # One receipt, one summary, one focus line -- and exactly one
                # request: the focus line costs no round trip.
                self.assertEqual(sent, 1)
                self.assertEqual(len(lines), 3)
                focus = lines[2]
                # The actor just ordered is never offered back; the walk is
                # units before cities, by alias number.
                self.assertTrue(focus.startswith("next: u2 Workers @31,72 idle"))
                self.assertNotIn("u1 ", focus)
                # The road verb outranks the sentry housekeeping verb.
                self.assertLess(
                    focus.index("road"), focus.index("sentry"), focus,
                )
                self.assertIn("a4 road", focus)
                self.assertLessEqual(len(focus), 120 + len("next: "))

    def test_v2_focus_line_degrades_instead_of_fetching_after_a_bump(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                self.stage_focus_seat(session_path, session, revision)
                sent = 0

                def responder(method, url, current, **options):
                    nonlocal sent
                    sent += 1
                    body = json.loads(options["encoded_body"].decode("utf-8"))
                    return client.JSONResponse(200, self.receipt(
                        session, body["batch_id"], "applied",
                        revision=self.revision(9),
                    ))

                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_do(self.do_args(
                        "u1 move 31,72", str(session_path),
                    )), 0)
                lines = stdout.getvalue().splitlines()
                self.assertEqual(sent, 1)
                focus = lines[-1]
                # The catalog died with the revision, so the line names the
                # actor and its enumeration command rather than fetching.
                self.assertEqual(
                    focus,
                    "next: u2 Workers @31,72 idle — "
                    "just legal --actor_id u2 --all",
                )

    def test_v2_focus_line_is_absent_from_json_and_from_a_failed_batch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                self.stage_focus_seat(session_path, session, revision)

                def responder(method, url, current, **options):
                    body = json.loads(options["encoded_body"].decode("utf-8"))
                    return client.JSONResponse(200, self.receipt(
                        session, body["batch_id"], "applied",
                        revision=revision,
                    ))

                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_do(self.do_args(
                        "u1 move 31,72", str(session_path), json_output=True,
                    )), 0)
                printed = stdout.getvalue()
                self.assertNotIn("next:", printed)
                payload = json.loads(printed)
                self.assertEqual(set(payload), {
                    "schema_version", "command", "orders", "requested",
                    "applied", "state_revision", "stopped",
                })

                # A rejected order applies nothing, so there is no receipt to
                # hang a focus line on.
                def rejects(method, url, current, **options):
                    body = json.loads(options["encoded_body"].decode("utf-8"))
                    return client.JSONResponse(200, self.receipt(
                        session, body["batch_id"], "rejected",
                        revision=revision,
                    ))

                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=rejects,
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_do(self.do_args(
                        "u1 move 31,72", str(session_path),
                    )), 2)
                self.assertNotIn("next:", stdout.getvalue())

    def test_v2_turn_decisions_lists_actors_and_refetches_only_what_is_stale(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                self.stage_focus_seat(session_path, session, revision)
                calls: list[str] = []

                def responder(method, url, current, **options):
                    calls.append(url)
                    raise AssertionError(f"unexpected request: {url}")

                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ), redirect_stdout(stdout):
                    self.assertEqual(client.command_turn(type("Args", (), {
                        "session": str(session_path), "end_phase": False,
                        "await_phase": False, "decisions": True,
                        "wait_s": 120, "poll_s": 1, "until": "phase",
                    })()), 0)
                # Both mirror tables are fresh and the catalog is cached, so a
                # decisions pass costs nothing on the wire.
                self.assertEqual(calls, [])
                lines = stdout.getvalue().splitlines()
                self.assertEqual(lines[0], "rev7/t3 decisions 3")
                self.assertTrue(lines[1].startswith("u1 Settlers @31,72 idle"))
                self.assertIn("a1 found_city", lines[1])
                self.assertTrue(lines[2].startswith("u2 Workers @31,72 idle"))
                self.assertTrue(
                    lines[3].startswith("c1 London @31,72 no production"),
                    lines[3],
                )
                # The unit with a standing route is not asked to decide again.
                self.assertNotIn("Explorer", stdout.getvalue())
                for line in lines[1:]:
                    self.assertLessEqual(len(line), 120, line)

    def test_v2_turn_decisions_refuses_to_double_as_the_phase_end(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, _session = self.v2_session(root)
                with patch.object(client, "_v2_response") as blocked:
                    with self.assertRaisesRegex(
                        client.PlayerError, "before `just turn --end`",
                    ):
                        client.command_turn(type("Args", (), {
                            "session": str(session_path), "end_phase": True,
                            "await_phase": False, "decisions": True,
                            "wait_s": 120, "poll_s": 1, "until": "phase",
                        })())
                blocked.assert_not_called()

    # ------------------------------------------------------------------
    # The one-call turn: a steady-state turn is `do … --end --await
    # --brief`, and the focus loop teaches the batch that fills it.
    # ------------------------------------------------------------------

    def stage_batch_seat(
        self, session_path: Path, session: dict, revision: dict,
    ) -> None:
        """The focus seat, plus the city catalog that makes c1 composable."""
        self.stage_focus_seat(session_path, session, revision)
        self.cache_actor_catalog(
            session_path, session, revision, self.CITY_ONE, [
                self.city_action(
                    revision, "action_prod01" + "0" * 19,
                    kind="city.set_production", operation="set_production",
                    label="Build Warriors",
                    target=self.production_target("Warriors", "a"),
                ),
                self.city_action(
                    revision, "action_prod02" + "0" * 19,
                    kind="city.set_production", operation="set_production",
                    label="Build Granary",
                    target=self.production_target("Granary", "b"),
                ),
            ],
        )

    def test_v2_focus_tail_composes_every_actor_into_one_do(self):
        """Three actors need orders, so the tail is the batch that gives them."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                self.stage_batch_seat(session_path, session, revision)

                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response",
                    side_effect=AssertionError("the tail opened a socket"),
                ), redirect_stdout(stdout):
                    self.assertEqual(client.command_turn(self.turn_args(
                        str(session_path), decisions=True,
                    )), 0)
                lines = stdout.getvalue().splitlines()
                self.assertEqual(lines[0], "rev7/t3 decisions 3")
                # One command, every actor, each with its top-ranked option
                # written the way `just do` reads it.
                self.assertEqual(
                    lines[-1],
                    'next 3 actors: just do "u1 found_city T(31,72); '
                    'u2 road T(31,72); c1 build Warriors"',
                )
                self.assertLessEqual(len(lines[-1]), 200)

                # And the composed command runs exactly as printed.
                composed = lines[-1].split('just do "', 1)[1].rstrip('"')
                state = client._load_v2_client_state(session_path, session)
                resolved = client._resolve_orders(
                    state, session_path, client._parse_orders(composed),
                )
                self.assertEqual(
                    [item["action_id"] for item in resolved],
                    [
                        "action_found" + "0" * 20,
                        "action_road0" + "0" * 20,
                        "action_prod01" + "0" * 19,
                    ],
                )

                # On the receipt path the same tail costs no round trip, and
                # the actor just ordered is never offered back.
                sent = 0

                def responder(method, url, current, **options):
                    nonlocal sent
                    sent += 1
                    body = json.loads(options["encoded_body"].decode("utf-8"))
                    return client.JSONResponse(200, self.receipt(
                        session, body["batch_id"], "applied",
                        revision=revision,
                    ))

                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_do(self.do_args(
                        "u1 found_city T(31,72)", str(session_path),
                    )), 0)
                self.assertEqual(sent, 1)
                receipted = stdout.getvalue().splitlines()
                self.assertEqual(
                    receipted[-1],
                    'next 2 actors: just do "u2 road T(31,72); '
                    'c1 build Warriors"',
                )

    def test_v2_composed_order_never_prints_a_command_that_would_refuse(self):
        """Only orders that resolve *and* bind their whole schema are offered."""
        revision = self.revision(7)
        founding = self.found_city_action(self.actor_action(
            revision, "action_found" + "0" * 20, self.UNIT_ONE,
            kind="unit.found_city", operation="found_city",
            label="Found city",
        ))
        moving = self.actor_action(
            revision, "action_move1" + "0" * 20, self.UNIT_ONE, x=31, y=72,
        )
        pool = [
            client._compact_legal_action(item) for item in (founding, moving)
        ]
        # `found_city` outranks `move`, but its city name is the player's to
        # choose, so `u1 found_city T(31,72)` would refuse. The tail yields to
        # the next option rather than printing it.
        self.assertEqual(client._decision_order(pool, "u1"), "u1 move T(31,72)")

        # With no schema to fill it is offered first, as its rank says.
        plain = [client._compact_legal_action(item) for item in (
            self.actor_action(
                revision, "action_found" + "0" * 20, self.UNIT_ONE,
                kind="unit.found_city", operation="found_city",
                label="Found city",
            ),
            moving,
        )]
        self.assertEqual(
            client._decision_order(plain, "u1"), "u1 found_city T(31,72)",
        )

        # Two actions sharing one verb and one target key cannot be told
        # apart by the printed line, so neither is offered.
        twins = [client._compact_legal_action(item) for item in (
            self.actor_action(
                revision, "action_twin1" + "0" * 20, self.UNIT_ONE,
                kind="unit.start_activity", operation="road", label="Road",
            ),
            self.actor_action(
                revision, "action_twin2" + "0" * 20, self.UNIT_ONE,
                kind="unit.start_activity", operation="road", label="Road",
            ),
        )]
        self.assertEqual(client._decision_order(twins, "u1"), "")
        self.assertEqual(client._decision_order(pool, ""), "")
        self.assertEqual(client._decision_order([], "u1"), "")

    def test_v2_focus_tail_falls_back_when_the_composed_line_overflows(self):
        rows = [
            {
                "alias": f"u{number}",
                "actor_id": f"unit_{number}",
                "state": "idle",
                "options": [],
                "option_count": 1,
                "order": f"u{number} connect_route T(100,{number:03d})",
                "remedy": f"just legal --actor_id u{number} --all",
            }
            for number in range(1, 9)
        ]
        # Eight orders of this width do not fit, so the line trims to the
        # actors it can name while the count stays honest.
        trimmed = client._batch_focus_command(rows)
        self.assertTrue(trimmed.startswith("next 8 actors, top "), trimmed)
        self.assertLessEqual(len(trimmed), 200)

        # Widen every order past the point where even two fit, and the tail
        # stops composing rather than printing a truncated command.
        for row in rows:
            row["order"] = row["order"] + " " + "x" * 90
        self.assertEqual(
            client._batch_focus_command(rows),
            "next 8 actors need orders — just turn --decisions",
        )

    def test_v2_focus_tail_keeps_one_actor_and_teaches_the_one_call_ending(self):
        one = [{
            "alias": "u2", "actor_id": "unit_2", "state": "Workers idle",
            "options": ["a4 road"], "option_count": 1,
            "order": "u2 road T(31,72)",
            "remedy": "just legal --actor_id u2 --all",
        }]
        # One actor is not a batch: the single-actor row is still the truth.
        self.assertEqual(client._batch_focus_command(one), "")

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                state = client._load_v2_client_state(session_path, session)
                # No actor needs orders, so the tail names the one call that
                # ends the turn and starts the next one.
                self.assertEqual(
                    client._next_focus_line(
                        session_path, state, frozenset(),
                    ),
                    "next: no actors need orders — "
                    "just turn --end --await --brief",
                )

    def one_call_responder(
        self, session: dict, revision: dict, phase_end: dict,
        log: list[str], *, order_receipt: str = "applied",
    ):
        """Answer every request one `do … --end --await --brief` call makes."""
        briefing = self.briefing_responder(session, revision)

        def responder(method, url, current, **options):
            if method == "POST":
                payload = json.loads(options["encoded_body"].decode("utf-8"))
                action_id = payload["commands"][0]["action_id"]
                ending = action_id == phase_end["action_id"]
                log.append("end" if ending else "order")
                return client.JSONResponse(200, self.receipt(
                    session, payload["batch_id"],
                    "applied" if ending else order_receipt,
                    revision=revision,
                ))
            if "/legal" in url:
                log.append("legal")
                return client.JSONResponse(200, self.page(
                    session, legal=True, revision=revision,
                    items=[phase_end],
                ))
            log.append("brief")
            return briefing(method, url, current, **options)

        return responder

    def test_v2_one_call_turn_orders_ends_wakes_and_briefs(self):
        """`do "…" --end --await --brief` is the whole steady-state turn."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                self.stage_batch_seat(session_path, session, revision)
                phase_end = self.pregame_action(
                    revision, "action_" + "e" * 26, "phase.end", "end",
                    "End phase", {"type": "object"}, None,
                )
                log: list[str] = []
                waking = self.wait_response(
                    session, "phase_active", active=True,
                    revision=self.revision(9),
                )

                def wait(path, current, args, **options):
                    log.append("wait")
                    return waking

                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response",
                    side_effect=self.one_call_responder(
                        session, revision, phase_end, log,
                    ),
                ), patch.object(
                    client, "_wait_value", side_effect=wait,
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_do(self.do_args(
                        "u1 found_city T(31,72); c1 build Warriors",
                        str(session_path), end_phase=True, await_phase=True,
                        brief=True,
                    )), 0)

                # Two orders, one phase end (enumerated first), one wake, then
                # the briefing -- in that order, in one call.
                self.assertEqual(
                    log[:5], ["order", "order", "legal", "end", "wait"],
                )
                self.assertIn("brief", log)
                printed = stdout.getvalue()
                lines = printed.splitlines()
                self.assertTrue(lines[0].startswith("u1 found_city T(31,72) →"))
                self.assertTrue(lines[1].startswith("c1 build Warriors →"))
                self.assertEqual(lines[2], "2/2 applied rev7/t3")
                self.assertTrue(lines[3].startswith("phase end → applied"))
                # The wake header does not point at the briefing it prints.
                self.assertIn("YOUR TURN · t3/p1", lines[4])
                self.assertNotIn("next: just turn", lines[4])
                # The next turn's whole briefing, decisions line and all.
                self.assertIn("T3 rev7/t3 | running", printed)
                self.assertIn("units 3/3", printed)
                self.assertIn("cities 1/1", printed)
                self.assertIn("needs decision:", printed)
                self.assertNotIn("phase NOT ended", printed)

    def test_v2_one_call_turn_composite_json_carries_each_part_once(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                self.stage_batch_seat(session_path, session, revision)
                phase_end = self.pregame_action(
                    revision, "action_" + "e" * 26, "phase.end", "end",
                    "End phase", {"type": "object"}, None,
                )
                waking = self.wait_response(
                    session, "phase_active", active=True,
                    revision=self.revision(9),
                )
                log: list[str] = []
                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response",
                    side_effect=self.one_call_responder(
                        session, revision, phase_end, log,
                    ),
                ), patch.object(
                    client, "_wait_value", return_value=waking,
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_do(self.do_args(
                        "u1 found_city T(31,72)", str(session_path),
                        end_phase=True, await_phase=True, brief=True,
                        json_output=True,
                    )), 0)
                payload = json.loads(stdout.getvalue())
                # The `do` shape is untouched; the composite is added beside it.
                self.assertEqual(set(payload), {
                    "schema_version", "command", "orders", "requested",
                    "applied", "state_revision", "stopped",
                    "end", "wait", "turn", "turn_error",
                })
                self.assertEqual(payload["applied"], 1)
                self.assertEqual(payload["end"]["receipt"]["receipt_state"], "applied")
                self.assertEqual(payload["wait"]["wake_reason"], "phase_active")
                self.assertEqual(payload["turn"]["command"], "turn")
                self.assertEqual(payload["turn"]["status"], "ready")
                self.assertIsNone(payload["turn_error"])

                # `turn --end --await --brief --json` returns the same three
                # parts, and no `do` fields it never had.
                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response",
                    side_effect=self.one_call_responder(
                        session, revision, phase_end, [],
                    ),
                ), patch.object(
                    client, "_wait_value", return_value=waking,
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_turn(self.turn_args(
                        str(session_path), end_phase=True, await_phase=True,
                        brief=True, json_output=True,
                    )), 0)
                composite = json.loads(stdout.getvalue())
                self.assertEqual(set(composite), {
                    "schema_version", "command", "status",
                    "end", "wait", "turn", "turn_error",
                })
                self.assertEqual(composite["command"], "turn")
                self.assertEqual(composite["status"], "briefed")
                self.assertEqual(composite["turn"]["status"], "ready")

                # Without --brief the shape `turn --end --await --json` has
                # always returned is untouched.
                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response",
                    side_effect=self.one_call_responder(
                        session, revision, phase_end, [],
                    ),
                ), patch.object(
                    client, "_wait_value", return_value=waking,
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_turn(self.turn_args(
                        str(session_path), end_phase=True, await_phase=True,
                        json_output=True,
                    )), 0)
                ended = json.loads(stdout.getvalue())
                self.assertEqual(set(ended), {
                    "schema_version", "command", "status", "disposition",
                    "wait",
                })
                self.assertEqual(ended["status"], "ended")

    def test_v2_do_end_never_ends_a_phase_whose_batch_stopped(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                self.stage_batch_seat(session_path, session, revision)
                phase_end = self.pregame_action(
                    revision, "action_" + "e" * 26, "phase.end", "end",
                    "End phase", {"type": "object"}, None,
                )
                log: list[str] = []
                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response",
                    side_effect=self.one_call_responder(
                        session, revision, phase_end, log,
                        order_receipt="rejected",
                    ),
                ), patch.object(
                    client, "_wait_value",
                    side_effect=AssertionError("a stopped batch awaited"),
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_do(self.do_args(
                        "u1 found_city T(31,72); c1 build Warriors",
                        str(session_path), end_phase=True, await_phase=True,
                        brief=True,
                    )), 2)
                printed = stdout.getvalue()
                # The first order was refused, so the batch stopped, nothing
                # enumerated phase.end, and the turn is still the agent's.
                self.assertEqual(log, ["order"])
                self.assertIn("phase NOT ended", printed)
                self.assertIn("0/2 orders applied", printed)
                self.assertIn("0/2 applied", printed)
                self.assertIn("just turn --end --await --brief", printed)

    def test_v2_do_end_prints_the_receipts_then_the_end_failure(self):
        """An end that cannot run never takes the applied orders with it."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                self.stage_batch_seat(session_path, session, revision)

                def no_phase_end(method, url, current, **options):
                    if method == "POST":
                        payload = json.loads(
                            options["encoded_body"].decode("utf-8"),
                        )
                        return client.JSONResponse(200, self.receipt(
                            session, payload["batch_id"], "applied",
                            revision=revision,
                        ))
                    # This seat holds no phase.end at this revision.
                    return client.JSONResponse(200, self.page(
                        session, legal=True, revision=revision, items=[],
                    ))

                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=no_phase_end,
                ), patch.object(
                    client, "_wait_value",
                    side_effect=AssertionError("awaited an unended phase"),
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_do(self.do_args(
                        "u1 found_city T(31,72)", str(session_path),
                        end_phase=True, await_phase=True, brief=True,
                    )), 2)
                lines = stdout.getvalue().splitlines()
                # Receipt, summary, then the failure with its own remedy --
                # in that order, never swallowed.
                self.assertTrue(lines[0].startswith("u1 found_city T(31,72) →"))
                self.assertIn("applied", lines[0])
                self.assertEqual(lines[1], "1/1 applied rev7/t3")
                self.assertTrue(
                    lines[2].startswith("phase NOT ended: "), lines[2],
                )
                self.assertIn("just turn", lines[2])
                # The turn is still the agent's, so the tail still names it.
                self.assertEqual(
                    lines[3],
                    'next 2 actors: just do "u2 road T(31,72); '
                    'c1 build Warriors"',
                )

    def test_v2_brief_without_a_wake_is_refused_with_the_form_that_works(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, _session = self.v2_session(root)
                with patch.object(client, "_v2_response") as blocked:
                    for args, pattern in (
                        (
                            self.turn_args(
                                str(session_path), end_phase=True, brief=True,
                            ),
                            r"just turn --end --await --brief",
                        ),
                        (
                            self.turn_args(str(session_path), brief=True),
                            r"just turn --end --await --brief",
                        ),
                    ):
                        with self.assertRaisesRegex(
                            client.PlayerError, pattern,
                        ):
                            client.command_turn(args)
                    for values, pattern in (
                        ({"end_phase": True, "brief": True}, r"--end --await"),
                        ({"brief": True}, r"--end --await"),
                        ({"await_phase": True}, r"just do --await"),
                    ):
                        with self.assertRaisesRegex(
                            client.PlayerError, pattern,
                        ):
                            client.command_do(self.do_args(
                                "u1 fortify", str(session_path), **values,
                            ))
                blocked.assert_not_called()

    def stage_stale_aliases(
        self, session_path: Path, session: dict, old: dict, new: dict,
    ) -> tuple[dict, dict]:
        """Cache two actions at `old`, then let the seat learn `new`."""
        found = self.actor_action(
            old, "action_found" + "7" * 20, self.UNIT_ONE,
            kind="unit.found_city", operation="found_city",
            label="Found city",
        )
        move = self.actor_action(
            old, "action_move" + "7" * 21, self.UNIT_ONE, x=32, y=73,
        )
        self.cache_actor_catalog(
            session_path, session, old, self.UNIT_ONE, [found, move],
        )
        cached = client._load_v2_client_state(session_path, session)
        self.assertEqual(
            {
                alias: entry["action_id"]
                for alias, entry in cached["action_aliases"]["by_alias"].items()
            },
            {"a1": found["action_id"], "a2": move["action_id"]},
        )
        client._remember_page(
            session_path, cached,
            client._validate_page(self.section_page(
                session, section="overview", revision=new, items=[],
            ), session, legal=False),
            legal=False,
        )
        return found, move

    def test_v2_a_stale_alias_is_rebound_by_meaning_and_keeps_its_number(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                old, new = self.revision(7), self.revision(9)
                found, move = self.stage_stale_aliases(
                    session_path, session, old, new,
                )
                # The same two actions at the new revision, in the opposite
                # order and with new handles: fresh numbering alone would swap
                # what a1 and a2 mean.
                fresh_move = self.actor_action(
                    new, "action_move" + "9" * 21, self.UNIT_ONE, x=32, y=73,
                )
                fresh_found = self.actor_action(
                    new, "action_found" + "9" * 20, self.UNIT_ONE,
                    kind="unit.found_city", operation="found_city",
                    label="Found city",
                )
                sent: list[tuple[str, bytes | None]] = []

                def responder(method, url, current, **options):
                    sent.append((url, options.get("encoded_body")))
                    if "legal-actions" in url:
                        return client.JSONResponse(200, self.scoped_legal_page(
                            session, revision=new,
                            items=[fresh_move, fresh_found],
                            actor_id=self.UNIT_ONE,
                            catalog="catalog_" + "b" * 32,
                        ))
                    body = json.loads(options["encoded_body"].decode("utf-8"))
                    return client.JSONResponse(200, self.receipt(
                        session, body["batch_id"], "applied", revision=new,
                    ))

                stdout = io.StringIO()
                with patch.object(
                    client.secrets, "token_urlsafe", return_value="R" * 24,
                ), patch.object(
                    client, "_v2_response", side_effect=responder,
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_batch(self.alias_args(
                        session=str(session_path), action_id="a1",
                    )), 0)
                lines = stdout.getvalue().splitlines()
                self.assertEqual(lines[0], "a1 rebound at rev9")
                # The wire carries only the handle the fresh enumeration
                # issued -- never the expired one, never the alias.
                body = json.loads(sent[-1][1].decode("utf-8"))
                self.assertEqual(
                    body["commands"],
                    [{
                        "action_id": fresh_found["action_id"], "arguments": {},
                    }],
                )
                self.assertEqual(body["state_revision"], new)
                for url, encoded in sent:
                    payload = url + (
                        "" if encoded is None else encoded.decode("utf-8")
                    )
                    self.assertNotIn(found["action_id"], payload)
                    self.assertNotIn("a1", payload)
                # a1 still means "found this city"; a2 still means that move.
                rebound = client._load_v2_client_state(session_path, session)
                self.assertEqual(
                    {
                        alias: entry["action_id"]
                        for alias, entry
                        in rebound["action_aliases"]["by_alias"].items()
                    },
                    {
                        "a1": fresh_found["action_id"],
                        "a2": fresh_move["action_id"],
                    },
                )

    def test_v2_a_vanished_or_ambiguous_alias_still_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                old, new = self.revision(7), self.revision(9)
                self.stage_stale_aliases(session_path, session, old, new)
                gone = self.actor_action(
                    new, "action_other" + "9" * 20, self.UNIT_ONE,
                    kind="unit.sentry", operation="sentry", label="Sentry",
                )

                def missing(method, url, current, **options):
                    self.assertIn("legal-actions", url)
                    return client.JSONResponse(200, self.scoped_legal_page(
                        session, revision=new, items=[gone],
                        actor_id=self.UNIT_ONE,
                        catalog="catalog_" + "b" * 32,
                    ))

                with patch.object(
                    client, "_v2_response", side_effect=missing,
                ), redirect_stdout(io.StringIO()):
                    with self.assertRaisesRegex(
                        client.PlayerError, "die with their revision",
                    ):
                        client.command_batch(self.alias_args(
                            session=str(session_path), action_id="a1",
                        ))

    def test_v2_two_actions_with_one_meaning_refuse_to_be_rebound(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                old, new = self.revision(7), self.revision(9)
                self.stage_stale_aliases(session_path, session, old, new)
                twins = [
                    self.actor_action(
                        new, f"action_twin{index}" + "9" * 20, self.UNIT_ONE,
                        kind="unit.found_city", operation="found_city",
                        label="Found city",
                    )
                    for index in range(2)
                ]

                def ambiguous(method, url, current, **options):
                    self.assertIn("legal-actions", url)
                    return client.JSONResponse(200, self.scoped_legal_page(
                        session, revision=new, items=twins,
                        actor_id=self.UNIT_ONE,
                        catalog="catalog_" + "b" * 32,
                    ))

                with patch.object(
                    client, "_v2_response", side_effect=ambiguous,
                ), redirect_stdout(io.StringIO()):
                    with self.assertRaisesRegex(
                        client.PlayerError, "a1 names 2 actions at rev9/t3",
                    ) as refusal:
                        client.command_batch(self.alias_args(
                            session=str(session_path), action_id="a1",
                        ))
                # Both candidates are named so the agent can pick one.
                self.assertRegex(str(refusal.exception), r"\(a\d+ a\d+\)")

    def test_v2_no_refresh_keeps_the_plain_refusal_and_sends_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                old, new = self.revision(7), self.revision(9)
                self.stage_stale_aliases(session_path, session, old, new)
                with patch.object(client, "_v2_response") as blocked:
                    with self.assertRaisesRegex(
                        client.PlayerError, "die with their revision",
                    ):
                        client.command_batch(self.alias_args(
                            session=str(session_path), action_id="a1",
                            no_refresh=True,
                        ))
                    with self.assertRaisesRegex(
                        client.PlayerError, "die with their revision",
                    ):
                        client.command_do(self.do_args(
                            "a1", str(session_path), no_refresh=True,
                        ))
                blocked.assert_not_called()

    def test_v2_do_rebinds_a_stale_alias_before_it_resolves_the_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                old, new = self.revision(7), self.revision(9)
                self.stage_stale_aliases(session_path, session, old, new)
                fresh_found = self.actor_action(
                    new, "action_found" + "9" * 20, self.UNIT_ONE,
                    kind="unit.found_city", operation="found_city",
                    label="Found city",
                )
                sent: list[tuple[str, bytes | None]] = []

                def responder(method, url, current, **options):
                    sent.append((url, options.get("encoded_body")))
                    if "legal-actions" in url:
                        return client.JSONResponse(200, self.scoped_legal_page(
                            session, revision=new, items=[fresh_found],
                            actor_id=self.UNIT_ONE,
                            catalog="catalog_" + "b" * 32,
                        ))
                    body = json.loads(options["encoded_body"].decode("utf-8"))
                    return client.JSONResponse(200, self.receipt(
                        session, body["batch_id"], "applied", revision=new,
                    ))

                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_do(self.do_args(
                        "a1", str(session_path),
                    )), 0)
                lines = stdout.getvalue().splitlines()
                self.assertEqual(lines[0], "a1 rebound at rev9")
                self.assertIn("→ applied rev9/t3", lines[1])
                body = json.loads(sent[-1][1].decode("utf-8"))
                self.assertEqual(
                    body["commands"][0]["action_id"], fresh_found["action_id"],
                )

    NATION_ENGLISH = "nation_" + "a" * 32
    NATION_ZULU = "nation_" + "c" * 32
    STYLE_EUROPEAN = "style_" + "b" * 32

    def start_responder(
        self,
        session: dict,
        steps: list[str],
        bodies: list[dict],
        *,
        nations: list[dict] | None = None,
        leader: str = "Boudica",
        sex: str = "female",
    ):
        lobby = self.revision(4, turn=0)
        configured = self.revision(5, turn=0)
        ready = self.revision(6, turn=0)
        offered = [
            {
                "id": self.NATION_ENGLISH, "name": "English",
                "default_style_id": self.STYLE_EUROPEAN,
            },
            {
                "id": self.NATION_ZULU, "name": "Zulu",
                "default_style_id": self.STYLE_EUROPEAN,
            },
        ] if nations is None else nations
        configure = self.pregame_action(
            lobby, "action_" + "1" * 26, "pregame.configure", "configure",
            "Choose nation, leader, sex, and style",
            {
                "type": "object",
                "properties": {
                    "nation_id": {"type": "string"},
                    "leader_name": {"type": "string"},
                    "is_male": {"type": "boolean"},
                    "style_id": {"type": "string"},
                },
                "required": [
                    "nation_id", "leader_name", "is_male", "style_id",
                ],
            },
            {"type": "pregame_configuration"},
        )
        set_ready = self.pregame_action(
            configured, "action_" + "2" * 26, "pregame.set_ready", "set_ready",
            "Mark ready",
            {
                "type": "object",
                "properties": {"ready": {"type": "boolean", "enum": [True]}},
                "required": ["ready"],
            },
            {"type": "pregame_readiness", "desired_ready": True},
        )
        catalogs = [
            self.page(session, legal=True, revision=lobby, items=[configure]),
            self.page(
                session, legal=True, revision=configured, items=[set_ready],
            ),
        ]

        def responder(method, url, current, **options):
            if "/health" in url:
                steps.append("health")
                return client.JSONResponse(
                    200, self.health(session, game_state="lobby"),
                )
            if "section=pregame_nations" in url:
                steps.append("nations")
                return client.JSONResponse(200, self.section_page(
                    session, section="pregame_nations", revision=lobby,
                    items=offered,
                ))
            if "section=overview" in url:
                steps.append("overview")
                return client.JSONResponse(200, self.section_page(
                    session, section="overview", revision=lobby, items=[{
                        "client_state": "preparing", "turn": 0, "phase": None,
                        "player": {
                            "id": self.PLAYER, "leader_name": leader,
                            "nation": None, "sex": sex, "style": None,
                            "ready": False,
                        },
                    }],
                ))
            if "legal-actions" in url:
                steps.append("legal")
                return client.JSONResponse(
                    200,
                    catalogs[min(steps.count("legal"), len(catalogs)) - 1],
                )
            steps.append("batch")
            payload = json.loads(options["encoded_body"].decode("utf-8"))
            bodies.append(payload)
            return client.JSONResponse(200, self.receipt(
                session, payload["batch_id"], "applied",
                revision=(
                    configured if payload["state_revision"] == lobby
                    else ready
                ),
            ))

        return responder

    @staticmethod
    def start_args(session: str, **values):
        defaults = {
            "session": session, "nation": "", "leader": "", "style": "",
            "male": False, "female": False,
        }
        defaults.update(values)
        return type("Args", (), defaults)()

    def test_v2_start_with_no_arguments_resolves_every_choice_itself(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                steps: list[str] = []
                bodies: list[dict] = []
                responder = self.start_responder(session, steps, bodies)
                stdout = io.StringIO()
                with patch.object(
                    client.random, "choice",
                    side_effect=lambda items: items[-1],
                ) as picked, patch.object(
                    client, "_v2_response", side_effect=responder,
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(
                        client.command_start(self.start_args(str(session_path))),
                        0,
                    )
                # The nation is drawn from what the lobby actually offers,
                # sorted, so seeding the RNG reproduces the pick.
                self.assertEqual(
                    [item["name"] for item in picked.call_args[0][0]],
                    ["English", "Zulu"],
                )
                self.assertEqual(steps, [
                    "health", "nations", "overview",
                    "legal", "batch", "legal", "batch",
                ])
                self.assertEqual(bodies[0]["commands"][0]["arguments"], {
                    "nation_id": self.NATION_ZULU,
                    # The controller label, reduced to what Freeciv accepts.
                    "leader_name": "codex-test-model",
                    # The seat's own lobby default, not a client invention.
                    "is_male": False,
                    "style_id": self.STYLE_EUROPEAN,
                })
                lines = stdout.getvalue().splitlines()
                self.assertEqual(
                    lines[0],
                    "starting as Zulu — codex-test-model (female), style "
                    "the nation default",
                )

    def test_v2_start_flags_each_override_exactly_what_they_name(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                steps: list[str] = []
                bodies: list[dict] = []
                responder = self.start_responder(session, steps, bodies)
                with patch.object(
                    client.random, "choice", side_effect=AssertionError,
                ), patch.object(
                    client, "_v2_response", side_effect=responder,
                ), redirect_stdout(io.StringIO()), redirect_stderr(
                    io.StringIO(),
                ):
                    self.assertEqual(client.command_start(self.start_args(
                        str(session_path), nation="english", male=True,
                    )), 0)
                # A named nation never draws, and a named sex never reads the
                # lobby overview: only what is missing is fetched.
                self.assertNotIn("overview", steps)
                self.assertEqual(bodies[0]["commands"][0]["arguments"], {
                    "nation_id": self.NATION_ENGLISH,
                    "leader_name": "codex-test-model",
                    "is_male": True,
                    "style_id": self.STYLE_EUROPEAN,
                })

    def test_v2_start_falls_back_to_the_lobby_leader_when_the_label_is_unusable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                session["controller_label"] = "***"
                client._write_private_json(session_path, session)
                steps: list[str] = []
                bodies: list[dict] = []
                responder = self.start_responder(
                    session, steps, bodies, leader="Boudica", sex="male",
                )
                with patch.object(
                    client.random, "choice",
                    side_effect=lambda items: items[0],
                ), patch.object(
                    client, "_v2_response", side_effect=responder,
                ), redirect_stdout(io.StringIO()), redirect_stderr(
                    io.StringIO(),
                ):
                    self.assertEqual(
                        client.command_start(self.start_args(str(session_path))),
                        0,
                    )
                self.assertEqual(
                    bodies[0]["commands"][0]["arguments"]["leader_name"],
                    "Boudica",
                )
                self.assertIs(
                    bodies[0]["commands"][0]["arguments"]["is_male"], True,
                )

    def test_v2_start_sanitizes_a_label_and_picks_a_sex_deterministically(self):
        self.assertEqual(client._sanitized_leader("pi-gpt-5.5"), "pi-gpt-5.5")
        self.assertEqual(
            client._sanitized_leader("  codex/test  model  "),
            "codex-test model",
        )
        self.assertEqual(client._sanitized_leader("***"), "")
        self.assertEqual(client._sanitized_leader(""), "")
        self.assertLessEqual(
            len(client._sanitized_leader("A" * 200).encode("utf-8")),
            client.V2_LEADER_MAX_BYTES,
        )

    def test_v2_start_picks_the_same_sex_twice_when_the_lobby_names_none(self):
        chosen = []
        for _run in range(2):
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory) / "play"
                root.mkdir()
                with patch.object(client, "ROOT", root), patch.dict(
                    os.environ,
                    {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                    clear=False,
                ):
                    session_path, session = self.v2_session(root)
                    steps: list[str] = []
                    bodies: list[dict] = []
                    # The lobby volunteers no usable sex, so the fallback is a
                    # pure function of the resolved leader name.
                    responder = self.start_responder(
                        session, steps, bodies, sex="unspecified",
                    )
                    with patch.object(
                        client.random, "choice",
                        side_effect=lambda items: items[0],
                    ), patch.object(
                        client, "_v2_response", side_effect=responder,
                    ), redirect_stdout(io.StringIO()), redirect_stderr(
                        io.StringIO(),
                    ):
                        self.assertEqual(client.command_start(
                            self.start_args(str(session_path)),
                        ), 0)
                    chosen.append(
                        bodies[0]["commands"][0]["arguments"]["is_male"],
                    )
        self.assertEqual(chosen[0], chosen[1])
        self.assertEqual(
            chosen[0],
            bool(
                client.hashlib.sha256(b"codex-test-model").digest()[0] % 2,
            ),
        )

    def test_v2_start_fails_closed_when_the_lobby_offers_no_nation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                steps: list[str] = []
                responder = self.start_responder(
                    session, steps, [], nations=[],
                )
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ), redirect_stdout(io.StringIO()):
                    with self.assertRaisesRegex(
                        client.PlayerError,
                        r"just state --section pregame_nations",
                    ):
                        client.command_start(self.start_args(str(session_path)))
                self.assertNotIn("batch", steps)

    def test_v2_show_map_yields_reads_two_local_files_and_no_socket(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                tile = "tile_" + "1" * 32
                pages = [
                    self.section_page(
                        session, section="known_tiles", revision=revision,
                        items=[{
                            "id": tile, "x": 31, "y": 72,
                            "visibility": "visible", "terrain": "Grassland",
                            "owner_player_id": None,
                            "infrastructure_placement": None,
                        }],
                    ),
                    self.section_page(
                        session, section="city_citizens", revision=revision,
                        items=[{
                            "city_id": self.CITY_ONE, "kind": "tile",
                            "tile_id": tile, "worked": True,
                            "free_worked": True, "can_work": True,
                            "yields": {
                                "food": 2, "shields": 1, "trade": 0,
                                "gold": 0, "luxury": 0, "science": 0,
                            },
                        }],
                    ),
                ]
                for index, payload in enumerate(pages):
                    with patch.object(
                        client, "_v2_response",
                        return_value=client.JSONResponse(200, payload),
                    ), redirect_stdout(io.StringIO()):
                        self.assertEqual(client.command_state(self.alias_args(
                            session=str(session_path),
                            section=payload["page"]["section"],
                            actor_id="" if index == 0 else self.CITY_ONE,
                        )), 0)
                stdout = io.StringIO()
                with patch.object(client, "_v2_response") as blocked:
                    with redirect_stdout(stdout):
                        self.assertEqual(client.command_show(type("Args", (), {
                            "session": str(session_path), "name": "map",
                            "grep": "", "regex": False, "yields": True,
                        })()), 0)
                blocked.assert_not_called()
                printed = stdout.getvalue()
                self.assertIn("# rev 7 turn 3", printed)
                self.assertIn("1 tiles priced", printed)
                self.assertIn("G2/1/0", printed)
                # `--yields` is a map overlay and says so anywhere else.
                with self.assertRaisesRegex(
                    client.PlayerError, r"just show map\s+--yields",
                ):
                    client.command_show(type("Args", (), {
                        "session": str(session_path), "name": "units",
                        "grep": "", "regex": False, "yields": True,
                    })())


    # ------------------------------------------------------------------
    # I6: the taught loop runs as printed, the taxonomy the filter accepts is
    # the taxonomy it prints, Tier-1 words reach real capabilities, and a
    # projection never claims to be current when it is not.
    # ------------------------------------------------------------------

    @staticmethod
    def turn_args(session: str, **values):
        defaults = {
            "session": session, "end_phase": False, "await_phase": False,
            "decisions": False, "wait_s": 120, "poll_s": 1, "until": "phase",
        }
        defaults.update(values)
        return type("Args", (), defaults)()

    @classmethod
    def briefing_overview(cls, *, chat: int | None = None) -> dict:
        item = {
            "turn": 3,
            "player": {
                "government": "Despotism", "economy": {"gold": 25},
            },
            "research": {
                "target": "Bronze Working", "bulbs_researched": 0, "cost": 28,
            },
            "counts": {"cities": 1, "units": 3, "legal_actions": 0},
        }
        if chat is not None:
            item["counts"]["chat"] = chat
        return item

    def briefing_responder(
        self, session: dict, revision: dict, *,
        chat: int | None = None, log: list[str] | None = None,
    ):
        """Answer exactly the requests one `just turn` briefing makes."""
        items = {
            "overview": [self.briefing_overview(chat=chat)],
            "units": self.focus_units(),
            "cities": self.focus_cities(),
            "research": [],
        }

        def responder(method, url, current, **options):
            if log is not None:
                log.append(url)
            if "/health" in url:
                return client.JSONResponse(
                    200, self.health(session, active=True),
                )
            if "/state?" in url:
                section = re.search(r"section=(\w+)", url).group(1)
                page = self.section_page(
                    session, section=section, revision=revision,
                    items=items[section],
                )
                return client.JSONResponse(200, page)
            raise AssertionError(f"unexpected request: {url}")

        return responder

    def test_v2_taught_loop_runs_as_printed_from_a_cold_cache(self):
        """`just turn` then `just do` — the four-command loop, literally.

        The briefing enumerates no capabilities, so the first order of a turn
        names an actor whose menu this seat has never read.  `do` draws that
        one menu, says so, and applies the order; nothing reaches the wire
        before the whole batch resolves.
        """
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response",
                    side_effect=self.briefing_responder(session, revision),
                ), redirect_stdout(stdout):
                    self.assertEqual(
                        client.command_turn(self.turn_args(str(session_path))),
                        0,
                    )
                briefing = stdout.getvalue()
                # The briefing carries the per-actor option summaries §8's
                # transcript budgeted for.  Nothing is cached yet, so each row
                # degrades to its own drill-down rather than fetching.
                self.assertIn("needs decision:", briefing)
                self.assertIn(
                    "u1 Settlers @31,72 idle — just legal --actor_id u1 --all",
                    briefing,
                )
                self.assertIn("c1 London @31,72 no production", briefing)

                found = self.found_city_action(self.actor_action(
                    revision, "action_found" + "5" * 20, self.UNIT_ONE,
                    kind="unit.found_city", operation="found_city",
                    label="Found city",
                ))
                catalog = self.scoped_legal_page(
                    session, revision=revision, items=[found],
                    actor_id=self.UNIT_ONE,
                )
                sent: list[str] = []

                def responder(method, url, current, **options):
                    sent.append(f"{method} {url.split('/v2/')[-1][:20]}")
                    if method == "GET":
                        return client.JSONResponse(200, catalog)
                    body = json.loads(options["encoded_body"].decode("utf-8"))
                    return client.JSONResponse(200, self.receipt(
                        session, body["batch_id"], "applied",
                        revision=self.revision(8),
                    ))

                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(client.command_do(self.do_args(
                        "u1 found_city London", str(session_path),
                    )), 0)
                lines = stdout.getvalue().splitlines()
                # One fetch, one send: the loop as printed, at one extra call.
                self.assertEqual(len(sent), 2)
                self.assertEqual(lines[0], "fetched u1 options (rev7)")
                self.assertIn("applied", lines[1])
                self.assertIn("1/1 applied", lines[2])

    def test_v2_do_fetches_every_unread_actor_before_it_sends_anything(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                # Learn the entity aliases without learning any capability.
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(200, self.section_page(
                        session, section="units", revision=revision,
                        items=self.focus_units(),
                    )),
                ), redirect_stdout(io.StringIO()):
                    self.assertEqual(client.command_state(self.alias_args(
                        session=str(session_path), section="units",
                    )), 0)

                catalogs = {
                    self.UNIT_ONE: self.scoped_legal_page(
                        session, revision=revision, actor_id=self.UNIT_ONE,
                        catalog="catalog_" + "1" * 32,
                        items=[self.actor_action(
                            revision, "action_sentry" + "1" * 19,
                            self.UNIT_ONE, kind="unit.sentry",
                            operation="sentry", label="Sentry",
                        )],
                    ),
                    self.UNIT_TWO: self.scoped_legal_page(
                        session, revision=revision, actor_id=self.UNIT_TWO,
                        catalog="catalog_" + "2" * 32,
                        items=[self.actor_action(
                            revision, "action_sentry" + "2" * 19,
                            self.UNIT_TWO, kind="unit.sentry",
                            operation="sentry", label="Sentry",
                        )],
                    ),
                }
                posts = 0

                def responder(method, url, current, **options):
                    nonlocal posts
                    if method == "GET":
                        actor = re.search(r"actor_id=(unit_[0-9a-f]+)", url)
                        return client.JSONResponse(
                            200, catalogs[actor.group(1)],
                        )
                    posts += 1
                    raise AssertionError("nothing may be sent")

                # `u1 teleport` is a bad verb, not a cold cache.  Both actors
                # are still fetched first -- the whole batch is re-checked
                # before anything is sent -- and then the batch refuses whole.
                with patch.object(
                    client, "_v2_response", side_effect=responder,
                ):
                    with self.assertRaises(client.PlayerError) as refusal:
                        client.command_do(self.do_args(
                            "u1 teleport 9,9; u2 sentry", str(session_path),
                        ))
                self.assertEqual(posts, 0)
                message = str(refusal.exception)
                self.assertIn("fetched u1 options (rev7)", message)
                self.assertIn("fetched u2 options (rev7)", message)
                self.assertIn("1 of 2 orders did not resolve", message)
                self.assertIn("2 resolved", message)
                self.assertIn(
                    "enumerate with: just legal --actor_id u1 --all", message,
                )

                # An actor already read is never re-fetched for a bad verb.
                with patch.object(client, "_v2_response") as blocked:
                    with self.assertRaisesRegex(
                        client.PlayerError, "did not resolve",
                    ):
                        client.command_do(self.do_args(
                            "u1 teleport 9,9", str(session_path),
                        ))
                blocked.assert_not_called()

                # `--no-refresh` keeps the plain refusal and fetches nothing.
                client._save_v2_client_state(
                    session_path, client._empty_v2_client_state(session),
                )
                with patch.object(client, "_v2_response") as blocked:
                    with self.assertRaisesRegex(
                        client.PlayerError, "did not resolve",
                    ):
                        client.command_do(self.do_args(
                            "u1 sentry", str(session_path), no_refresh=True,
                        ))
                blocked.assert_not_called()

    def test_v2_briefing_counts_new_events_and_names_the_feed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response",
                    side_effect=self.briefing_responder(
                        session, self.revision(7), chat=5,
                    ),
                ), redirect_stdout(stdout):
                    self.assertEqual(
                        client.command_turn(self.turn_args(str(session_path))),
                        0,
                    )
                self.assertIn(
                    "events: 5 new — just state --section chat",
                    stdout.getvalue(),
                )

                # The next briefing counts only what arrived since this one.
                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response",
                    side_effect=self.briefing_responder(
                        session, self.revision(9), chat=7,
                    ),
                ), redirect_stdout(stdout):
                    self.assertEqual(
                        client.command_turn(self.turn_args(str(session_path))),
                        0,
                    )
                self.assertIn("events: 2 new", stdout.getvalue())

                # An unchanged feed is not news and costs no line.
                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response",
                    side_effect=self.briefing_responder(
                        session, self.revision(11), chat=7,
                    ),
                ), redirect_stdout(stdout):
                    self.assertEqual(
                        client.command_turn(self.turn_args(str(session_path))),
                        0,
                    )
                self.assertNotIn("events:", stdout.getvalue())


    def test_v2_the_scored_objective_is_on_the_line_read_first(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)

                def briefing(score) -> str:
                    responder = self.briefing_responder(
                        session, self.revision(7),
                    )
                    overview = self.briefing_overview()
                    if score is not None:
                        overview["score"] = score

                    def scored(method, url, current, **options):
                        response = responder(method, url, current, **options)
                        if "section=overview" in url:
                            response.value["page"]["items"] = [overview]
                        return response

                    stdout = io.StringIO()
                    with patch.object(
                        client, "_v2_response", side_effect=scored,
                    ), redirect_stdout(stdout):
                        self.assertEqual(client.command_turn(
                            self.turn_args(str(session_path)),
                        ), 0)
                    return stdout.getvalue().splitlines()[0]

                # A lower bound is never printed as if it were the score.
                self.assertIn("score >=17 (citizens 7, techs 5)", briefing({
                    "exact": None,
                    "lower_bound": 17,
                    "components": {
                        "citizens": 7, "techs": 5, "spaceship": 0,
                    },
                    "unobserved": ["wonders", "culture"],
                }))
                # An exact score drops the qualifier entirely.
                self.assertIn("score 23 (citizens 7, techs 5)", briefing({
                    "exact": 23,
                    "lower_bound": 17,
                    "components": {
                        "citizens": 7, "techs": 5, "spaceship": 0,
                    },
                    "unobserved": [],
                }))
                # An older server sends no score, and no line is invented.
                self.assertNotIn("score", briefing(None))

        # The same projection leads the `overview` state page.
        item = {
            "turn": 3,
            "player": {"government": "Despotism", "economy": {"gold": 25}},
            "research": {"target": "Bronze Working"},
            "score": {
                "exact": None, "lower_bound": 4,
                "components": {"citizens": 4, "techs": 0, "spaceship": 0},
                "unobserved": ["wonders"],
            },
        }
        self.assertIn("score >=4 (citizens 4)", client._render_overview(
            [item],
        )[0])
        # And the mirror records it, so the delta digest can diff it and
        # `just show overview` can read it without a request.
        _columns, rows = state_mirror._render_overview([item], None)
        recorded = {name: value for name, value in rows}
        self.assertEqual(recorded["score"], ">=4")
        self.assertEqual(recorded["score_from"], "citizens 4")

    def test_v2_legal_kind_accepts_the_taxonomy_its_own_column_prints(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                move = self.actor_action(
                    revision, "action_" + "m" * 26, "unit_" + "a" * 32,
                    x=32, y=72,
                )
                goal = self.pregame_action(
                    revision, "action_" + "g" * 26, "research.set_goal",
                    "set_goal", "Set research goal", {"type": "object"},
                    {"type": "technology", "id": "tech_1", "name": "Currency"},
                )
                catalog = self.page(
                    session, legal=True, revision=revision,
                    items=[move, goal],
                )

                def run(kind: str) -> str:
                    stdout = io.StringIO()
                    with patch.object(
                        client, "_v2_response",
                        return_value=client.JSONResponse(200, catalog),
                    ), redirect_stdout(stdout):
                        self.assertEqual(client.command_legal(self.alias_args(
                            session=str(session_path), kind=kind,
                            all_pages=True,
                        )), 0)
                    return stdout.getvalue()

                # The kind column prints `unit.order/move`; both that string
                # and the bare kind select the row it was copied from.
                printed = run("unit.order")
                self.assertIn("unit.order/move", printed)
                self.assertIn("1/1 matched", printed)
                self.assertIn("1/1 matched", run("unit.order/move"))
                self.assertIn("1/1 matched", run("research.set_goal"))

                # A kind that matches nothing is an error naming the kinds
                # that are really there -- never an affirmative empty page.
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(200, catalog),
                ):
                    with self.assertRaises(client.PlayerError) as empty:
                        client.command_legal(self.alias_args(
                            session=str(session_path),
                            kind="unit.order/goto", all_pages=True,
                        ))
                message = str(empty.exception)
                self.assertIn("matched none of the 2 actions", message)
                self.assertIn("unit.order/move", message)
                self.assertIn("research.set_goal", message)

                # A malformed kind lists the kinds this seat has read, the
                # way `state --section bogus` lists its sections.
                with patch.object(client, "_v2_response") as blocked:
                    with self.assertRaises(client.PlayerError) as unknown:
                        client.command_legal(self.alias_args(
                            session=str(session_path), kind="bogus",
                            all_pages=True,
                        ))
                blocked.assert_not_called()
                self.assertIn("is not an action kind", str(unknown.exception))
                self.assertIn("unit.order/move", str(unknown.exception))

    def test_v2_legal_kind_names_the_actor_scope_that_holds_the_kind(self):
        """The 0/0 regression: unit kinds live in per-actor catalogs."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                actor = "unit_" + "a" * 32
                self.cache_actor_catalog(
                    session_path, session, revision, actor,
                    [self.actor_action(
                        revision, "action_" + "m" * 26, actor, x=32, y=72,
                    )],
                )
                # The unscoped catalog carries no unit rows at all.
                global_page = self.page(
                    session, legal=True, revision=revision,
                    items=[self.pregame_action(
                        revision, "action_" + "g" * 26, "research.set_goal",
                        "set_goal", "Set research goal", {"type": "object"},
                        {
                            "type": "technology", "id": "tech_1",
                            "name": "Currency",
                        },
                    )],
                )
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(200, global_page),
                ):
                    with self.assertRaises(client.PlayerError) as refusal:
                        client.command_legal(self.alias_args(
                            session=str(session_path), kind="unit.order",
                            all_pages=True,
                        ))
                message = str(refusal.exception)
                self.assertIn("actor-scoped kind", message)
                self.assertIn("just legal --actor_id u1 --all", message)

    @classmethod
    def city_action(
        cls,
        revision: dict,
        action_id: str,
        *,
        kind: str,
        operation: str,
        label: str,
        schema: dict | None = None,
        target: dict | None = None,
    ) -> dict:
        return {
            "action_id": action_id,
            "kind": kind,
            "label": label,
            "subject": {
                "operation": operation,
                "actor": {
                    "id": cls.CITY_ONE, "type": "city", "name": "London",
                },
                "target": target,
                "probability": {
                    "kind": "exact", "minimum_percent": 100,
                    "maximum_percent": 100,
                },
            },
            "arguments_schema": schema or {"type": "object"},
            "state_revision": revision,
        }

    @staticmethod
    def production_target(name: str, suffix: str) -> dict:
        return {
            "type": "production", "id": f"production_{suffix * 8}",
            "kind": "improvement", "name": name,
        }

    def stage_tier1_seat(
        self, session_path: Path, session: dict, revision: dict,
    ) -> None:
        """Cache one unit and one city catalog carrying every Tier-1 target."""
        self.cache_actor_catalog(
            session_path, session, revision, self.UNIT_ONE, [
                # Two moves teach T(40,60) and T(41,61) to the tile cache.
                self.actor_action(
                    revision, "action_move1" + "0" * 20, self.UNIT_ONE,
                    x=40, y=60,
                ),
                self.actor_action(
                    revision, "action_move2" + "0" * 20, self.UNIT_ONE,
                    x=41, y=61,
                ),
                {
                    "action_id": "action_route" + "0" * 20,
                    "kind": "unit.order",
                    "label": "Set route",
                    "subject": {
                        "operation": "set_route",
                        "actor": {
                            "id": self.UNIT_ONE, "type": "unit",
                            "name": "Explorer",
                        },
                        "target": None,
                    },
                    "arguments_schema": {
                        "type": "object",
                        "properties": {
                            "mode": {
                                "type": "string",
                                "enum": ["goto", "patrol"],
                            },
                            "waypoints": {
                                "type": "array", "minItems": 1,
                                "maxItems": 8, "items": {"type": "string"},
                            },
                        },
                        "required": ["mode", "waypoints"],
                    },
                    "state_revision": revision,
                },
            ],
        )
        self.cache_actor_catalog(
            session_path, session, revision, self.CITY_ONE, [
                self.city_action(
                    revision, "action_prod01" + "0" * 19,
                    kind="city.set_production", operation="set_production",
                    label="Build Warriors",
                    target=self.production_target("Warriors", "a"),
                ),
                self.city_action(
                    revision, "action_prod02" + "0" * 19,
                    kind="city.set_production", operation="set_production",
                    label="Build Granary",
                    target=self.production_target("Granary", "b"),
                ),
                self.city_action(
                    revision, "action_work01" + "0" * 19,
                    kind="city.set_worklist", operation="set_worklist",
                    label="Set worklist",
                    schema={
                        "type": "object",
                        "properties": {
                            "items": {
                                "type": "array", "minItems": 0,
                                "maxItems": 7, "items": {"type": "string"},
                            },
                        },
                        "required": ["items"],
                    },
                ),
                self.city_action(
                    revision, "action_rally1" + "0" * 19,
                    kind="city.set_rally", operation="set_rally",
                    label="Set rally point",
                    schema={
                        "type": "object",
                        "properties": {"persistent": {"type": "boolean"}},
                        "required": ["persistent"],
                    },
                    target={
                        "id": "tile_" + f"{33:04d}{70:04d}".rjust(32, "0"),
                        "x": 33, "y": 70,
                    },
                ),
            ],
        )

    def test_v2_tier1_verbs_reach_the_capabilities_they_name(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                self.stage_tier1_seat(session_path, session, revision)
                state = client._load_v2_client_state(session_path, session)
                waypoints = [
                    "tile_" + f"{x:04d}{y:04d}".rjust(32, "0")
                    for x, y in ((40, 60), (41, 61))
                ]

                for order, action_id, arguments in (
                    (
                        "u1 route 40,60 41,61", "action_route" + "0" * 20,
                        {"mode": "goto", "waypoints": waypoints},
                    ),
                    (
                        "u1 patrol T(40,60) T(41,61)",
                        "action_route" + "0" * 20,
                        {"mode": "patrol", "waypoints": waypoints},
                    ),
                    (
                        "c1 build Warriors", "action_prod01" + "0" * 19, {},
                    ),
                    (
                        "c1 queue Granary Warriors",
                        "action_work01" + "0" * 19,
                        {"items": [
                            "production_" + "b" * 8, "production_" + "a" * 8,
                        ]},
                    ),
                    (
                        "c1 rally 33,70", "action_rally1" + "0" * 19,
                        {"persistent": False},
                    ),
                ):
                    with self.subTest(order=order):
                        resolved = client._resolve_order(
                            state, session_path, order,
                        )
                        self.assertEqual(resolved["action_id"], action_id)
                        self.assertEqual(resolved["arguments"], arguments)

                # A Tier-1 word whose capability this actor does not advertise
                # fails closed naming the enumeration command, and sends
                # nothing -- the actor's whole menu is already cached, so this
                # is a real refusal and not a cache miss.
                with patch.object(client, "_v2_response") as blocked:
                    with self.assertRaises(client.PlayerError) as refusal:
                        client.command_do(self.do_args(
                            "u1 rally 33,70", str(session_path),
                        ))
                blocked.assert_not_called()
                message = str(refusal.exception)
                self.assertIn("city.set_rally/set_rally", message)
                self.assertIn("does not advertise", message)
                self.assertIn(
                    "enumerate with: just legal --actor_id u1 --all", message,
                )

                # A list element the catalog never named by that name is
                # refused rather than guessed at.
                with self.assertRaisesRegex(
                    Exception, "no cached target is named Colossus",
                ):
                    client._resolve_order(
                        state, session_path, "c1 queue Colossus",
                    )
                with self.assertRaisesRegex(Exception, r"unknown tile"):
                    client._resolve_order(
                        state, session_path, "u1 route 99,99",
                    )

    def test_v2_show_leads_with_a_staleness_banner_when_it_is_behind(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                self.stage_focus_seat(session_path, session, revision)

                def show(name: str) -> str:
                    stdout = io.StringIO()
                    with patch.object(client, "_v2_response") as blocked:
                        with redirect_stdout(stdout):
                            self.assertEqual(
                                client.command_show(type("Args", (), {
                                    "session": str(session_path),
                                    "name": name, "grep": "", "regex": False,
                                    "yields": False,
                                })()),
                                0,
                            )
                    blocked.assert_not_called()
                    return stdout.getvalue()

                # Current: no banner.
                self.assertNotIn("stale:", show("units"))

                # Learn a newer revision without rewriting the unit table.
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(200, self.section_page(
                        session, section="cities", revision=self.revision(12),
                        items=self.focus_cities(),
                    )),
                ), redirect_stdout(io.StringIO()):
                    self.assertEqual(client.command_state(self.alias_args(
                        session=str(session_path), section="cities",
                    )), 0)

                banner = (
                    "stale: rendered at rev7, now rev12 — aliases will be "
                    "re-verified by meaning on use"
                )
                self.assertEqual(show("units").splitlines()[0], banner)
                # The option projection carries the same banner, and it is
                # written at read time: the file itself still says rev 7.
                options = show("u1")
                self.assertEqual(options.splitlines()[0], banner)
                self.assertIn("# rev 7 turn 3", options)
                stored = client._mirror_text(
                    session_path, ("state", "options", "u1.txt"),
                )
                self.assertNotIn("stale:", stored)
                # The table that *is* current answers without the banner.
                self.assertNotIn("stale:", show("cities"))

    def test_v2_show_in_the_lobby_names_a_command_that_works_there(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, _session = self.v2_session(root)
                with patch.object(client, "_v2_response") as blocked:
                    with self.assertRaises(client.PlayerError) as refusal:
                        client.command_show(type("Args", (), {
                            "session": str(session_path), "name": "",
                            "grep": "", "regex": False, "yields": False,
                        })())
                blocked.assert_not_called()
                message = str(refusal.exception)
                # `just turn` in the lobby writes no mirror, so it cannot be
                # the remedy an empty mirror names first.
                self.assertIn("just state --section overview", message)
                self.assertLess(
                    message.index("just state --section overview"),
                    message.index("just turn"),
                )

    def test_v2_refusals_lead_with_the_phase_when_the_seat_is_not_active(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(7)
                self.stage_focus_seat(session_path, session, revision)
                ended = self.health(session, active=True)
                ended["phase"]["state"] = "inactive_done"
                client._mirror_health(session_path, ended, "turn", revision)
                note = (
                    "your phase is not active (state inactive_done) "
                    "— just wait"
                )

                with patch.object(client, "_v2_response") as blocked:
                    with self.assertRaises(client.PlayerError) as refusal:
                        client.command_do(self.do_args(
                            "u1 teleport 9,9", str(session_path),
                        ))
                blocked.assert_not_called()
                message = str(refusal.exception)
                self.assertEqual(message.splitlines()[0], note)
                # Re-enumerating cannot help, so it is not offered.
                self.assertNotIn("enumerate with:", message)

                # `legal` learns the same fact from the same cached header.
                with patch.object(client, "_v2_response") as blocked:
                    with self.assertRaises(client.PlayerError) as legal:
                        client.command_legal(self.alias_args(
                            session=str(session_path), kind="bogus",
                            all_pages=True,
                        ))
                blocked.assert_not_called()
                self.assertEqual(str(legal.exception).splitlines()[0], note)

                # An active phase leaves every refusal exactly as it was.
                client._mirror_health(
                    session_path, self.health(session, active=True), "turn",
                    revision,
                )
                with patch.object(client, "_v2_response") as blocked:
                    with self.assertRaises(client.PlayerError) as active:
                        client.command_do(self.do_args(
                            "u1 teleport 9,9", str(session_path),
                        ))
                blocked.assert_not_called()
                self.assertNotIn("phase is not active", str(active.exception))
                self.assertIn("enumerate with:", str(active.exception))

    def test_v2_wait_prints_compact_text_and_keeps_json_behind_the_flag(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                wake = self.wait_response(
                    session, "phase_active", active=True,
                )
                stdout = io.StringIO()
                with patch.object(
                    client, "_wait_value", return_value=wake,
                ), redirect_stdout(stdout):
                    self.assertEqual(client.main([
                        "wait", "--session", str(session_path),
                    ]), 0)
                text = stdout.getvalue()
                self.assertFalse(text.startswith("{"), text)
                lines = text.splitlines()
                self.assertIn("YOUR TURN · t3/p1", lines[0])
                self.assertIn("next: just turn", lines[0])
                self.assertTrue(lines[1].startswith("health running"), lines[1])
                # Nothing the JSON carries is invented and nothing raw leaks.
                self.assertNotIn("deadline_started_at", text)

                stdout = io.StringIO()
                with patch.object(
                    client, "_wait_value", return_value=wake,
                ), redirect_stdout(stdout):
                    self.assertEqual(client.main([
                        "wait", "--session", str(session_path), "--json",
                    ]), 0)
                self.assertEqual(json.loads(stdout.getvalue()), wake)

    @staticmethod
    def error_lines(code: str, details: dict, *, retryable: bool = False):
        return client._render_error_payload({
            "error": {
                "code": code, "message": "the request could not be completed",
                "details": details, "retryable": retryable,
            },
        })

    def test_v2_the_json_remedy_line_is_only_printed_when_it_pays(self):
        empty = self.error_lines("rate_limited", {}, retryable=True)
        self.assertNotIn(
            "full payload: re-run the same command with --json", empty,
        )
        self.assertIn("retryable: the same request may be sent again", empty)

        # A scalar detail is printed verbatim, so `--json` adds nothing.
        scalar = self.error_lines(
            "illegal_action", {"safe_next": "refresh", "field": "leader_name"},
        )
        self.assertIn("  field=leader_name", scalar)
        self.assertNotIn(
            "full payload: re-run the same command with --json", scalar,
        )

        # A nested detail is the one thing the compact form has to elide.
        nested = self.error_lines(
            "illegal_action",
            {"safe_next": "refresh", "rejected": {"argument": {"a": 1}}},
        )
        self.assertIn(
            "full payload: re-run the same command with --json", nested,
        )

    def test_v2_rate_limits_and_retired_cursors_name_a_real_next_step(self):
        limited = self.error_lines(
            "rate_limited",
            {
                "retry_after_seconds": 12,
                "retry_after": "2999-01-01T00:00:12.000Z",
            },
            retryable=True,
        )
        self.assertIn(
            "next: retry the same command in 12s "
            "(not before 2999-01-01T00:00:12.000Z)",
            limited,
        )
        # The clock is the remedy; the flag that cannot help is not offered.
        self.assertNotIn(
            "full payload: re-run the same command with --json", limited,
        )
        self.assertNotIn("  retry_after_seconds=12", limited)

        # A retired page chain forwards the query that restarts it, and the
        # remedy is that query spelled as a command that runs as printed.
        stale = self.error_lines(
            "stale_revision",
            {
                "restart": {
                    "endpoint": "state",
                    "query": {"section": "known_tiles", "limit": 16},
                },
            },
        )
        self.assertIn(
            "next: just state --section known_tiles --limit 16", stale,
        )
        scoped = self.error_lines(
            "cursor_expired",
            {
                "restart": {
                    "endpoint": "legal_actions",
                    "query": {
                        "actor_id": "unit_" + "a" * 32, "limit": 16,
                    },
                },
            },
        )
        self.assertIn(
            "next: just legal --actor_id unit_" + "a" * 32 + " --limit 16",
            scoped,
        )

        # A restart this client cannot spell exactly is never guessed at: the
        # raw detail still prints, but no unrunnable command is invented.
        unknown = self.error_lines(
            "stale_revision",
            {
                "restart": {
                    "endpoint": "state",
                    "query": {"section": "units", "unknown_option": 1},
                },
            },
        )
        self.assertFalse(
            [line for line in unknown if line.startswith("next: just")],
            unknown,
        )

    # -----------------------------------------------------------------
    # Advertised-but-unreachable actions.
    #
    # From a real 596-turn game: the `governments` section reported
    # `can_change=yes` for Democracy at turn 224 while every attempt to
    # enumerate the action came back empty, and an open Spanish ceasefire
    # meeting was never answered.  Neither was a rules condition.  The
    # government rows were drained and then dropped by the actor catalog's
    # byte cap, whose only notice is a header fragment a pipe removes; the
    # meeting was reachable only through an actor-plus-target query that no
    # refusal ever named.  These lock the negative space: a bounded window
    # says what it withheld, and a refusal names the query that works.
    # -----------------------------------------------------------------

    GOVERNMENT_ACTOR_PLAYER = "player_" + "b" * 32
    GOVERNMENT_RELATION = "relation_" + "e" * 32

    @classmethod
    def government_catalog(cls, *, shown: int) -> tuple[dict, dict, dict]:
        """A player catalog whose government rows sort last, truncated."""
        revision = cls.revision(1273, turn=224)
        aliases = {cls.GOVERNMENT_ACTOR_PLAYER: "p1"}
        items: list[dict] = []

        def subject(operation: str, **extra) -> dict:
            base = {
                "actor": {
                    "type": "player", "id": cls.GOVERNMENT_ACTOR_PLAYER,
                },
                "target": None,
                "operation": operation,
                "variant": "standard",
                "consuming": False,
                "legality": "legal",
                "probability": {
                    "kind": "exact", "minimum_percent": 100,
                    "maximum_percent": 100,
                },
            }
            base.update(extra)
            return base

        def add(index, kind, label, body) -> None:
            action_id = "action_" + f"{index:032d}"
            items.append(cls.rendered_descriptor(
                revision, action_id, kind=kind, label=label, subject=body,
            ))
            aliases[action_id] = f"a{index}"

        for index, tech in enumerate((
            "Magnetism", "Sanitation", "Chemistry", "Railroad", "Economics",
        ), start=1):
            add(
                index, "research.set_goal", f"Set research goal to {tech}",
                subject("set_goal", target={
                    "type": "tech", "id": "tech_" + f"{index:032d}",
                    "name": tech,
                }),
            )
        # The tail of the real catalog, in its real order.
        add(
            6, "government.revolution", "Start an untargeted revolution",
            subject("revolution", target={
                "type": "government", "id": "government_" + "1" * 32,
                "name": "Anarchy",
            }),
        )
        for index, name in enumerate(
            ("Despotism", "Monarchy", "Democracy"), start=7,
        ):
            add(
                index, "government.change", f"Change government to {name}",
                subject("change", target={
                    "type": "government", "id": "government_" + f"{index}" * 32,
                    "name": name,
                }),
            )
        compacts = [
            client._compact_legal_action(item) for item in items[:shown]
        ]
        hidden: dict[str, int] = {}
        for item in items[shown:]:
            key = client._descriptor_kind_key(item)
            hidden[key] = hidden.get(key, 0) + 1
        result = {
            "schema_version": 1, "command": "legal", "kind": None,
            "state_revision": revision, "catalog_total": len(items),
            "pages_read": 1, "matched": len(items), "offset": 0,
            "limit": client.V2_LEGAL_ACTOR_MATCH_LIMIT,
            "shown": shown, "truncated": shown < len(items),
            "has_more": shown < len(items),
            "next_offset": shown if shown < len(items) else None,
            "byte_limited": True, "oversized_single": False,
            "hidden_kinds": dict(sorted(hidden.items())),
            "actions": compacts,
        }
        scope = {
            "actor_id": cls.GOVERNMENT_ACTOR_PLAYER, "actor_type": "player",
        }
        return result, scope, aliases

    def test_a_truncated_actor_catalog_names_the_kinds_it_withheld(self):
        """A bounded window must never read as an empty one."""
        result, scope, aliases = self.government_catalog(shown=5)
        rendered = client._render_legal_compact(result, scope, aliases)
        # The regression: the agent piped this through `grep government` and
        # read the empty result as "the rules forbid a revolution".  Every
        # withheld kind is now named in the body, below the rows, where a
        # pipe and a scroll both keep it.
        matched = [line for line in rendered if "government" in line]
        self.assertTrue(matched, "\n".join(rendered))
        tail = rendered[-1]
        self.assertIn("government.change (3)", tail)
        self.assertIn("government.revolution (1)", tail)
        self.assertIn("--actor_id p1 --all --offset 5", tail)
        self.assertNotIn(tail, rendered[0])

        # A relation window is a two-parameter query, so its continuation
        # names both parameters or it enumerates a different catalog.
        relation_result, _scope, _aliases = self.government_catalog(shown=5)
        relation_result["kind"] = None
        relation_scope = {
            "actor_id": self.GOVERNMENT_ACTOR_PLAYER, "actor_type": "player",
            "target_id": self.GOVERNMENT_RELATION,
            "target_type": "relation",
        }
        self.assertIn(
            "--actor_id p1 --target_id r1 --all",
            client._render_legal_compact(
                relation_result, relation_scope,
                {**aliases, self.GOVERNMENT_RELATION: "r1"},
            )[-1],
        )

        # A window that hid nothing gains no line at all.
        whole, scope, aliases = self.government_catalog(shown=9)
        self.assertEqual(whole["hidden_kinds"], {})
        self.assertNotIn(
            "not shown",
            "\n".join(client._render_legal_compact(whole, scope, aliases)),
        )

    def test_the_drain_counts_every_matched_row_it_did_not_print(self):
        """`hidden_kinds` is what the renderer's promise rests on."""
        result, _scope, _aliases = self.government_catalog(shown=6)
        self.assertEqual(result["hidden_kinds"], {"government.change": 3})
        self.assertEqual(
            result["shown"] + sum(result["hidden_kinds"].values()),
            result["matched"],
        )

    def test_a_real_drain_reports_the_government_rows_its_cap_ate(self):
        """End to end: the counting lives in the drain, not in the renderer."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                revision = self.revision(1273, turn=224)
                actions = []
                for index, (kind, operation, label) in enumerate((
                    ("research.set_goal", "set_goal", "Goal Sanitation"),
                    ("research.set_goal", "set_goal", "Goal Railroad"),
                    ("government.revolution", "revolution", "Revolt"),
                    ("government.change", "change", "Change to Democracy"),
                )):
                    descriptor = self.descriptor(revision, f"action_gov_{index}")
                    descriptor.update({
                        "kind": kind,
                        "label": label,
                        "subject": {
                            "actor": {
                                "type": "player",
                                "id": self.GOVERNMENT_ACTOR_PLAYER,
                            },
                            "operation": operation,
                        },
                    })
                    actions.append(descriptor)
                page = self.page(
                    session, legal=True, revision=revision, items=actions,
                )
                args = type("Args", (), {
                    "session": str(session_path),
                    "actor_id": self.GOVERNMENT_ACTOR_PLAYER,
                    "target_id": "", "limit": None, "cursor": "",
                    "kind": "", "all_pages": True, "offset": "",
                    "json_output": True,
                })()
                widest = max(
                    len(json.dumps(
                        client._compact_legal_action(descriptor),
                        sort_keys=True, separators=(",", ":"),
                    ).encode("utf-8"))
                    for descriptor in actions
                )
                stdout = io.StringIO()
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(200, page),
                ), patch.object(
                    client, "V2_LEGAL_COMPACT_MAX_BYTES", widest * 2,
                ), redirect_stdout(stdout):
                    self.assertEqual(client.command_legal(args), 0)
                result = json.loads(stdout.getvalue())
                self.assertEqual(result["shown"], 2)
                self.assertEqual(result["hidden_kinds"], {
                    "government.change": 1, "government.revolution": 1,
                })
                self.assertEqual(
                    result["shown"] + sum(result["hidden_kinds"].values()),
                    result["matched"],
                )
                # Every drained descriptor is still staged: the rows were
                # withheld from the print, never from the cache.
                cached = client._load_v2_client_state(session_path, session)
                self.assertEqual(set(cached["actions"]), {
                    descriptor["action_id"] for descriptor in actions
                })

    def test_a_player_scoped_kind_refusal_names_its_own_scope(self):
        """`--kind government.change --all` must not read as "no such action"."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ):
                session_path, session = self.v2_session(root)
                client._save_v2_client_state(
                    session_path, client._empty_v2_client_state(session),
                )
                for selector, expected in (
                    ("government.change", "--kind government.change --all"),
                    ("government.revolution", "your own player scope"),
                    ("spaceship.launch", "your own player scope"),
                    ("diplomacy.acceptance", "--target_id RELATION_ID"),
                ):
                    with self.subTest(selector=selector):
                        error = client._kind_matched_nothing(
                            session_path, session, selector, None,
                            ["research.set_goal", "phase.end"], 112,
                            self.revision(1273, turn=224),
                        )
                        message = str(error)
                        self.assertIn(expected, message)
                        # The old fallback sent a government query looking for
                        # a unit or a city; nothing else did.
                        self.assertNotIn(
                            "unit and city kinds are enumerated per actor",
                            message,
                        )
                # A kind that really is unit-scoped keeps the old remedy.
                self.assertIn(
                    "unit and city kinds are enumerated per actor",
                    str(client._kind_matched_nothing(
                        session_path, session, "unit.order", None,
                        ["phase.end"], 112, self.revision(1273, turn=224),
                    )),
                )

    def test_a_relation_used_as_an_actor_names_the_query_that_works(self):
        """The one ID an agent reaches for as an actor and never can be."""
        def args(**values):
            defaults = {
                "cursor": "", "actor_id": "", "target_id": "", "limit": None,
            }
            defaults.update(values)
            return type("Args", (), defaults)()

        with self.assertRaises(client.PlayerError) as caught:
            client._legal_query(args(actor_id=self.GOVERNMENT_RELATION))
        message = str(caught.exception)
        self.assertIn("is a diplomacy target, not an actor", message)
        self.assertIn(f"--target_id {self.GOVERNMENT_RELATION}", message)
        # The form it names is the form the query builder accepts.
        self.assertEqual(
            client._legal_query(args(
                actor_id="player_" + "b" * 32,
                target_id=self.GOVERNMENT_RELATION,
            )),
            "actor_id=player_" + "b" * 32
            + "&target_id=" + self.GOVERNMENT_RELATION,
        )

    def test_diplomacy_refusals_name_the_flag_just_accepts(self):
        """`--relation-id` is rejected by the wrapper before the client sees it."""
        def args(**values):
            defaults = {
                "cursor": "", "section": "", "actor_id": "", "relation_id": "",
                "center_id": "", "radius": None, "limit": None,
            }
            defaults.update(values)
            return type("Args", (), defaults)()

        with self.assertRaises(client.PlayerError) as caught:
            client._state_query(args(section="diplomacy_clauses"))
        message = str(caught.exception)
        self.assertIn("--relation_id", message)
        self.assertNotIn("--relation-id", message)

    def test_a_pending_meeting_names_its_actor_plus_target_drill_down(self):
        """The old remedy named a kind the global catalog can never hold."""
        relation = {
            "type": "diplomatic_relation", "id": self.GOVERNMENT_RELATION,
        }
        pool = [{
            "action_id": "action_" + "9" * 32,
            "kind": "diplomacy.acceptance",
            "label": "Accept the treaty",
            "subject": {
                "actor": {
                    "type": "player", "id": self.GOVERNMENT_ACTOR_PLAYER,
                },
                "operation": "accept",
            },
            "target": relation,
            "argument_schema": {},
        }]
        aliases = {
            self.GOVERNMENT_ACTOR_PLAYER: "p1",
            self.GOVERNMENT_RELATION: "r1",
        }
        self.assertEqual(
            client._meeting_remedy(pool, "r1", aliases),
            "just legal --actor_id p1 --target_id r1 --all",
        )
        # Without aliases the opaque IDs still compose a runnable command.
        self.assertEqual(
            client._meeting_remedy(pool, "diplomacy", {}),
            f"just legal --actor_id {self.GOVERNMENT_ACTOR_PLAYER} "
            f"--target_id {self.GOVERNMENT_RELATION} --all",
        )

    CITY_DETAIL = "city_" + "4" * 32

    @staticmethod
    def city_outputs(**named) -> dict:
        """Build an `outputs` map the way the boundary composes one."""
        return {
            name: {
                "citizen_base": base, "net": net, "surplus": surplus,
                "usage": usage, "waste": waste, "unhappy_penalty": unhappy,
                "gross": net + waste + unhappy,
            }
            for name, (base, net, surplus, usage, waste, unhappy)
            in named.items()
        }

    @classmethod
    def city_detail_item(cls, **overrides) -> dict:
        item = {
            "id": cls.CITY_DETAIL, "owner_player_id": "player_" + "c" * 32,
            "name": "London", "tile_id": "tile_" + "d" * 32,
            "x": 45, "y": 46, "size": 1,
            "surplus": {"food": 2, "shields": 5, "trade": 1},
            "production": {
                "id": "production_" + "e" * 32, "kind": "unit",
                "name": "Settlers", "shield_stock": 25, "shield_cost": 40,
                "buy_cost": 41, "can_buy": True, "can_change": True,
            },
            "airlift": {"remaining": 0, "maximum": 0},
            "trade_routes": {"count": 0, "capacity": 2},
            "governor_enabled": False,
            "citizens": {
                "happy": 0, "content": 1, "unhappy": 0, "angry": 0,
                "workers": 1, "specialists": 0,
            },
            "citizen_counts_consistent": True,
            "food_storage": {
                "stock": 14, "granary_size": 20, "growth_turns": 3,
            },
            "pollution": 0,
            "outputs": cls.city_outputs(
                food=(3, 3, 2, 1, 0, 0), shields=(5, 5, 5, 0, 0, 0),
                trade=(1, 1, 1, 0, 0, 0), gold=(0, 0, 0, 0, 0, 0),
                science=(0, 1, 1, 0, 0, 0),
            ),
            "counts": {
                "citizen_tiles": 19, "specialist_types": 3, "worklist": 0,
                "build_choices": 106, "improvements": 1, "trade_routes": 0,
            },
            "management": {
                "did_sell": False,
                "rally": {
                    "active": False, "persistent": False, "vigilant": False,
                    "order_count": 0, "plan_id": None,
                },
                "governor": {"enabled": False},
                "options": {
                    "allow_disband": False, "new_citizens": "default",
                    "conflict": False,
                },
            },
        }
        item.update(overrides)
        return item

    def test_v2_city_detail_prints_the_numbers_it_used_to_elide(self):
        """A 596-turn game read `outputs.food=…` and paid to guess past it.

        Every number below is already on the page the agent asked for, so the
        rendering owes it a value, not an ellipsis -- and where a number really
        does live in a child section, it owes the command that prints it.
        """
        revision = self.revision(56, turn=8)
        aliases = {self.CITY_DETAIL: "c1"}

        def rendered(item):
            return client._render_state_page({
                "state_revision": revision,
                "page": {
                    "section": "city_detail", "items": [item],
                    "total_items": 1, "next_cursor": None,
                    "cursor_expires_at": None,
                },
            }, aliases)

        calm = rendered(self.city_detail_item())
        body = "\n".join(calm)
        self.assertNotIn("…", body)
        self.assertNotIn("outputs.food", body)
        self.assertEqual(calm[1], "c1 London @45,46 sz1 f+2 s+5 t+1")
        self.assertIn("granary 14/20 food +2/turn grows in 3t", body)
        self.assertIn("citizens 1: 1 content", body)
        self.assertIn(
            "build Settlers unit 25/40 shields +5/turn done in 3t "
            "· buy 41 gold",
            body,
        )
        # The worked-tile yield total is a first-class column, and a column
        # that is zero on every row is not printed at all.
        header = next(line for line in calm if line.strip().startswith("output"))
        self.assertEqual(
            header.split(), ["output", "base", "gross", "used", "surplus"],
        )
        self.assertIn("food     3     3      1     +2", body)
        # An output this city neither makes nor spends is not a row.
        self.assertFalse(
            [line for line in calm if line.strip().startswith("gold")], calm,
        )
        # A collection that lives elsewhere is named as the command for it.
        self.assertIn(
            "  19 tile yields: just state --section city_citizens "
            "--actor_id c1",
            calm,
        )
        self.assertIn(
            "  106 build choices: just state --section city_build_choices "
            "--actor_id c1",
            calm,
        )

        besieged = rendered(self.city_detail_item(
            name="York", size=7,
            surplus={"food": -2, "shields": 1, "trade": 4}, pollution=3,
            production={
                "id": "production_" + "f" * 32, "kind": "improvement",
                "name": "City Walls", "shield_stock": 0, "shield_cost": 60,
                "buy_cost": 240, "can_buy": False, "can_change": False,
            },
            citizens={
                "happy": 1, "content": 2, "unhappy": 3, "angry": 1,
                "workers": 7, "specialists": 0,
            },
            food_storage={
                "stock": 9, "granary_size": 60, "growth_turns": -4,
            },
            outputs=self.city_outputs(
                food=(12, 12, -2, 14, 0, 0), shields=(9, 4, 1, 3, 2, 3),
                trade=(11, 8, 4, 4, 3, 0),
            ),
            management={
                "did_sell": True,
                "rally": {
                    "active": True, "persistent": True, "vigilant": False,
                    "order_count": 3, "plan_id": "rally_" + "a" * 32,
                },
                "governor": {"enabled": True},
                "options": {
                    "allow_disband": False, "new_citizens": "gold",
                    "conflict": False,
                },
            },
        ))
        stressed = "\n".join(besieged)
        self.assertIn("!pollution 3", besieged[1])
        self.assertIn("!starving, famine in 4t", stressed)
        # `city_unhappy()` is `happy < unhappy + 2 * angry` over exactly these
        # counters, so the verdict is the server's own.
        self.assertIn(
            "citizens 7: 1 happy, 2 content, 3 unhappy, 1 angry !disorder",
            stressed,
        )
        # Where the shields went, term by term.
        header = next(
            line for line in besieged if line.strip().startswith("output")
        )
        self.assertEqual(header.split(), [
            "output", "base", "waste", "unhappy", "net", "used", "surplus",
        ])
        self.assertIn("shields  9     2      3        4    3     +1", stressed)
        self.assertIn("!cannot buy this turn", stressed)
        self.assertIn("!locked: this city already bought this turn", stressed)
        self.assertIn("!sold here this turn", stressed)
        self.assertIn("!governor on", stressed)
        self.assertIn("!rally 3 orders persistent", stressed)
        self.assertIn("!new_citizens=gold", stressed)
        for line in calm + besieged:
            self.assertLessEqual(len(line), 120, line)

        # Without an alias cache the drill-down still runs as printed: the
        # opaque ID is typed rather than a positional `c1` that resolves
        # nowhere.
        bare = client._render_state_page({
            "state_revision": revision,
            "page": {
                "section": "city_detail", "items": [self.city_detail_item()],
                "total_items": 1, "next_cursor": None,
                "cursor_expires_at": None,
            },
        })
        self.assertIn(
            f"  19 tile yields: just state --section city_citizens "
            f"--actor_id {self.CITY_DETAIL}",
            bare,
        )

    def test_v2_city_citizens_totals_the_tiles_its_citizens_work(self):
        revision = self.revision(10, turn=1)
        items = [
            {
                "city_id": self.CITY_DETAIL, "kind": "tile",
                "tile_id": "tile_" + "1" * 32, "worked": True,
                "free_worked": True, "can_work": True,
                "yields": {"food": 2, "shields": 1, "trade": 1},
            },
            {
                "city_id": self.CITY_DETAIL, "kind": "tile",
                "tile_id": "tile_" + "2" * 32, "worked": True,
                "free_worked": False, "can_work": True,
                "yields": {"food": 2, "shields": 2, "trade": 0},
            },
            {
                "city_id": self.CITY_DETAIL, "kind": "tile",
                "tile_id": "tile_" + "3" * 32, "worked": False,
                "free_worked": False, "can_work": True,
                "yields": {"food": 1, "shields": 0, "trade": 3},
            },
            {
                "city_id": self.CITY_DETAIL, "kind": "specialist",
                "id": "specialist_" + "9" * 32, "name": "Entertainer",
                "count": 1, "counts_toward_population": True,
                "can_use": True, "is_default": True,
                "yields": {"luxury": 2},
            },
        ]
        lines = client._render_state_page({
            "state_revision": revision,
            "page": {
                "section": "city_citizens", "items": items,
                "total_items": len(items), "next_cursor": None,
                "cursor_expires_at": None,
            },
        }, {self.CITY_DETAIL: "c1"})
        # The total the tile swap is judged against, without adding the rows
        # up by hand -- and it is `city_detail`'s `base` row for this page.
        self.assertEqual(lines[1], "worked 2 of 4 rows on this page: f4 s3 t1")
        self.assertEqual(lines[2], "specialists: 1 Entertainer")
        self.assertTrue(any("tile_" + "1" * 32 in line for line in lines))

    def test_v2_buy_cost_rides_the_city_row_and_the_decision_line(self):
        """The buy action quotes no price, so the city surfaces must.

        `city.buy_production` carries no `gold_cost` on the wire, which is why
        `gold=` never appears on its legal row; the price is on the city, and
        the city is what both of these render.
        """
        revision = self.revision(19, turn=4)
        city = {
            "id": self.CITY_ONE, "name": "London", "x": 31, "y": 72,
            "size": 3, "surplus": {"food": 1, "shields": 2, "trade": 3},
            "production": {
                "kind": "unit", "name": "Musketeers", "shield_stock": 30,
                "shield_cost": 30, "buy_cost": 4, "can_buy": True,
            },
        }
        rows = client._render_state_page({
            "state_revision": revision,
            "page": {
                "section": "cities", "items": [city], "total_items": 1,
                "next_cursor": None, "cursor_expires_at": None,
            },
        }, {self.CITY_ONE: "c1"})
        self.assertIn("buy=4", rows[1])

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                page = self.section_page(
                    session, section="cities", revision=revision,
                    items=[city],
                )
                with patch.object(
                    client, "_v2_response",
                    return_value=client.JSONResponse(200, page),
                ), redirect_stdout(io.StringIO()):
                    self.assertEqual(client.command_state(self.alias_args(
                        session=str(session_path), section="cities",
                    )), 0)
                state = client._load_v2_client_state(session_path, session)
                found = client._decision_city_rows(
                    session_path, state, client._alias_map(state),
                )
                self.assertEqual(len(found), 1)
                # The city whose build completes this turn is offered with the
                # price of finishing it now already on the line.
                self.assertIn("buy=4", found[0]["state"])
                self.assertLessEqual(
                    len(client._decision_line(found[0])), 120,
                )

    def test_v2_build_choice_forfeit_appears_only_when_it_is_derivable(self):
        """`shield_stock_after_change` is the native client's own arithmetic.

        `city_change_production_penalty()` (common/city.c) has already run on
        the other side of the wire, so the forfeit is a subtraction, not a rule
        this client re-derives.  It still needs the stock the city holds *now*,
        which lives in the `cities` mirror -- so the warning is printed only
        when that mirror already stands at this page's own revision.
        """
        revision = self.revision(19, turn=4)
        choices = [
            {
                "city_id": self.CITY_ONE, "id": "production_" + "1" * 32,
                "kind": "improvement", "name": "City Walls",
                "can_queue": True, "can_build_now": True,
                "cost": {
                    "shields": 60, "shield_stock_after_change": 12,
                    "turns": 15, "turns_with_stock": 12,
                },
                "upkeep": {"gold": 1, "food": 0, "shields": 0},
                "happy_cost": None, "unit": None,
                "building": {"genus": "Improvement"},
            },
            {
                "city_id": self.CITY_ONE, "id": "production_" + "2" * 32,
                "kind": "unit", "name": "Musketeers",
                "can_queue": True, "can_build_now": False,
                "cost": {
                    "shields": 30, "shield_stock_after_change": 25,
                    "turns": 6, "turns_with_stock": 1,
                },
                "upkeep": {"gold": 0, "food": 0, "shields": 1},
                "happy_cost": 1, "unit": {"attack": 3}, "building": None,
            },
        ]
        city = {
            "id": self.CITY_ONE, "name": "London", "x": 31, "y": 72,
            "size": 3, "surplus": {"food": 1, "shields": 2, "trade": 3},
            "production": {
                "kind": "unit", "name": "Musketeers", "shield_stock": 25,
                "shield_cost": 30, "buy_cost": 12, "can_buy": True,
            },
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)

                def read(section, items, at):
                    page = self.section_page(
                        session, section=section, revision=at, items=items,
                    )
                    stdout = io.StringIO()
                    with patch.object(
                        client, "_v2_response",
                        return_value=client.JSONResponse(200, page),
                    ), redirect_stdout(stdout):
                        self.assertEqual(client.command_state(self.alias_args(
                            session=str(session_path), section=section,
                            actor_id=(
                                self.CITY_ONE if section.startswith("city_")
                                else ""
                            ),
                        )), 0)
                    return stdout.getvalue()

                read("cities", [city], revision)
                fresh = read("city_build_choices", choices, revision)
                self.assertIn("stock 25 shields", fresh)
                self.assertIn("!forfeits 13 of 25 shields", fresh)
                # A switch inside the same production class costs nothing, and
                # says nothing.
                musketeers = next(
                    line for line in fresh.splitlines()
                    if "Musketeers" in line
                )
                self.assertNotIn("forfeits", musketeers)
                self.assertIn("!worklist only", musketeers)
                self.assertIn("!upkeep shields 1", musketeers)
                self.assertIn("!unhappy 1", musketeers)
                for line in fresh.splitlines():
                    self.assertLessEqual(len(line), 120, line)

                # One revision later the mirror's stock is only a memory, so
                # the page reports what it holds and claims no forfeit.
                stale = read(
                    "city_build_choices", choices, self.revision(21, turn=4),
                )
                self.assertNotIn("forfeits", stale)
                self.assertNotIn("stock 25 shields", stale)
                self.assertIn("keep 12", stale)

    RELATION_SPAIN = "relation_" + "d" * 32
    RELATION_ROME = "relation_" + "f" * 32

    @classmethod
    def diplomacy_items(cls, *, accepted: bool = False) -> list[dict]:
        return [
            {
                "relation_id": cls.RELATION_SPAIN,
                "player_id": "player_" + "b" * 32,
                "player_name": "Isabella", "nation": "Spanish",
                "alive": True, "state": "cease-fire", "has_embassy": True,
                "other_has_embassy": False, "can_open_meeting": True,
                "treaty_turns_left": 4,
                "meeting": {
                    "meeting_id": "meeting_" + "e" * 32, "generation": 3,
                    "self_accepted": accepted, "other_accepted": True,
                    "clause_count": 2, "clauses_token": "treaty_x",
                },
            },
            {
                "relation_id": cls.RELATION_ROME,
                "player_id": "player_" + "a" * 32,
                "player_name": "Caesar", "nation": "Roman", "alive": True,
                "state": "war", "has_embassy": False,
                "other_has_embassy": False, "can_open_meeting": True,
                "treaty_turns_left": None, "meeting": None,
            },
        ]

    def read_diplomacy(
        self, session_path: Path, session: dict, revision: dict, items: list,
    ) -> str:
        page = self.section_page(
            session, section="diplomacy", revision=revision, items=items,
        )
        stdout = io.StringIO()
        with patch.object(
            client, "_v2_response",
            return_value=client.JSONResponse(200, page),
        ), redirect_stdout(stdout):
            self.assertEqual(client.command_state(self.alias_args(
                session=str(session_path), section="diplomacy",
            )), 0)
        return stdout.getvalue()

    @staticmethod
    def show_args(session_path: Path, name: str):
        return type("Args", (), {
            "session": str(session_path), "name": name, "grep": "",
            "regex": False, "yields": False,
        })()

    def test_v2_diplomacy_is_mirrored_and_reachable_by_relation_alias(self):
        """`just show diplomacy` used to refuse: there was no such table.

        Every other section an agent reads lands in the mirror, so a relation
        was the one entity whose row could not be re-read without a request.
        """
        revision = self.revision(210, turn=40)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)
                page = self.read_diplomacy(
                    session_path, session, revision, self.diplomacy_items(),
                )
                # The section names the open meeting on the row and the clause
                # page as the command that prints it.
                self.assertIn(
                    "!meeting open 2 clauses accepted by them, awaiting you",
                    page,
                )
                self.assertIn(
                    "clauses: just state --section diplomacy_clauses "
                    "--relation_id r1",
                    page,
                )
                self.assertNotIn(self.RELATION_SPAIN, page)
                for line in page.splitlines():
                    self.assertLessEqual(len(line), 120, line)

                shown = io.StringIO()
                with redirect_stdout(shown):
                    self.assertEqual(
                        client.command_show(
                            self.show_args(session_path, "diplomacy"),
                        ), 0,
                    )
                table = shown.getvalue()
                self.assertIn(
                    "alias\tplayer\tnation\tstate\tembassy\tmeeting\t"
                    "clauses\taccepted",
                    table.replace("  ", "").replace(" \t", "\t"),
                )
                self.assertIn("Isabella", table)
                self.assertIn("cease-fire", table)

                # A relation is addressed by alias like any other entity.
                row = io.StringIO()
                with redirect_stdout(row):
                    self.assertEqual(
                        client.command_show(
                            self.show_args(session_path, "r1"),
                        ), 0,
                    )
                self.assertIn("diplomacy:", row.getvalue())
                self.assertIn("Isabella", row.getvalue())

    def test_v2_an_unanswered_meeting_reaches_the_decisions_block(self):
        """Spain's cease-fire sat open while decisions offered an idle worker.

        A meeting only reached the list once a diplomacy descriptor happened to
        be cached, and nothing prompts that read -- so the mirror, which is the
        seat's own record that the meeting exists, is read too.
        """
        revision = self.revision(210, turn=40)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                session_path, session = self.v2_session(root)

                def meetings(items):
                    self.read_diplomacy(
                        session_path, session, revision, items,
                    )
                    state = client._load_v2_client_state(
                        session_path, session,
                    )
                    return client._decision_meeting_rows(
                        session_path, state, client._alias_map(state),
                    )

                # No diplomacy action is cached anywhere, and the row appears
                # anyway, naming who is waiting and on what.
                rows = meetings(self.diplomacy_items())
                self.assertEqual(len(rows), 1)
                self.assertEqual(rows[0]["alias"], "r1")
                self.assertEqual(
                    rows[0]["state"],
                    "meeting pending: Isabella, cease-fire, 2 clauses",
                )
                self.assertEqual(
                    rows[0]["remedy"],
                    "just legal --actor_id YOUR_PLAYER_ID --target_id r1 "
                    "--all",
                )
                line = client._decision_line(rows[0])
                self.assertLessEqual(len(line), 120, line)
                # A meeting is never composed into the batch line: its actor is
                # this seat's player, not the relation the row is named after.
                self.assertEqual(rows[0]["order"], "")

                # A meeting this seat has already accepted owes no decision.
                self.assertEqual(
                    meetings(self.diplomacy_items(accepted=True)), [],
                )

    def test_v2_a_meeting_without_the_diplomacy_page_names_that_read(self):
        """Knowing a meeting is open and nothing else is still worth a row."""
        state = {
            "entity_aliases": {"r1": self.GOVERNMENT_RELATION},
            "tile_aliases": {},
            "action_aliases": client._empty_action_aliases(),
            "last_revision": self.revision(210, turn=40),
            "actions": {
                "action_" + "9" * 32: {
                    "action_id": "action_" + "9" * 32,
                    "kind": "diplomacy.acceptance",
                    "label": "Accept the treaty",
                    "subject": {
                        "operation": "accept",
                        "actor": {
                            "type": "player",
                            "id": self.GOVERNMENT_ACTOR_PLAYER,
                        },
                        "target": {
                            "type": "diplomatic_relation",
                            "id": self.GOVERNMENT_RELATION,
                        },
                    },
                    "arguments_schema": {"type": "object", "properties": {}},
                    "state_revision": self.revision(210, turn=40),
                },
            },
        }
        with tempfile.TemporaryDirectory() as directory:
            rows = client._decision_meeting_rows(
                Path(directory), state, client._alias_map(state),
            )
        self.assertEqual(len(rows), 1)
        # The mirror has never been written, so the row cannot say who or what
        # -- and names the one page that would.
        self.assertEqual(
            rows[0]["state"],
            "meeting pending (unread: just state --section diplomacy "
            "--limit 16)",
        )
        self.assertIn("just state --section diplomacy", rows[0]["state"])

    def test_v2_state_refusals_spell_every_flag_the_way_just_accepts(self):
        """A remedy naming `--actor-id` fails as printed; the wrapper takes `_`."""
        def args(**values):
            defaults = {
                "cursor": "", "section": "", "actor_id": "",
                "relation_id": "", "center_id": "", "radius": None,
                "limit": None,
            }
            defaults.update(values)
            return type("Args", (), defaults)()

        for invalid, expected in (
            (args(section="city_detail"), "--actor_id"),
            (args(section="unit_route"), "--actor_id"),
            (
                args(section="tile_window", center_id="tile_" + "a" * 32),
                "--center_id",
            ),
            (
                args(section="tile_window", center_id="tile_" + "a" * 32),
                "--radius",
            ),
        ):
            with self.subTest(expected=expected):
                with self.assertRaises(client.PlayerError) as caught:
                    client._state_query(invalid)
                message = str(caught.exception)
                self.assertIn(expected, message)
                if "_" in expected:
                    self.assertNotIn(expected.replace("_", "-"), message)

    def test_v2_a_changeable_government_names_the_catalog_that_holds_it(self):
        """`can_change yes` used to name no way to act on it.

        `government.*` is enumerated only in this seat's own player scope, so
        an agent that reads this section and then searches the global catalog
        by kind finds nothing and concludes the rules forbid the change.
        """
        revision = self.revision(210, turn=40)

        def rendered(items, state):
            return client._render_state_page({
                "state_revision": revision,
                "page": {
                    "section": "governments", "items": items,
                    "total_items": len(items), "next_cursor": None,
                    "cursor_expires_at": None,
                },
            }, {}, None, state)

        state = {
            "entity_aliases": {"p1": self.GOVERNMENT_ACTOR_PLAYER},
            "tile_aliases": {},
            "action_aliases": client._empty_action_aliases(),
            "last_revision": revision,
            "actions": {
                "action_" + "8" * 32: {
                    "action_id": "action_" + "8" * 32,
                    "kind": "player.send_chat",
                    "label": "Send chat",
                    "subject": {
                        "operation": "send_chat",
                        "actor": {
                            "type": "player",
                            "id": self.GOVERNMENT_ACTOR_PLAYER,
                        },
                    },
                    "arguments_schema": {"type": "object", "properties": {}},
                    "state_revision": revision,
                },
            },
        }
        settled = [{
            "id": "government_1", "name": "Despotism", "current": True,
            "target": False, "during_revolution": False, "can_change": False,
        }]
        changeable = settled + [{
            "id": "government_2", "name": "Monarchy", "current": False,
            "target": False, "during_revolution": False, "can_change": True,
        }]
        hint = (
            "government actions are player-scoped: "
            "just legal --actor_id p1 --all"
        )
        self.assertEqual(rendered(changeable, state)[-1], hint)
        # Nothing to change, nothing to say.
        self.assertNotIn(hint, rendered(settled, state))
        # Without a cache there is no player alias to print, so no command is
        # printed either.
        self.assertNotIn(hint, rendered(changeable, None))



def _monitor_holder_pid(session_path):
    """The pid recorded by whichever process currently holds the lock."""
    holder = client._monitor_holder(session_path)
    return None if holder is None else holder.get("pid")


class PvPWaitInteropTests(unittest.TestCase):
    """The multiplayer wait surface, end to end.

    Every case here reproduces something a live two-agent match actually did:
    a wake reason the client could not parse, an exit status that said
    "success" for "still not your turn", a briefing printed for a phase the
    caller did not hold, and a marker file frozen for the whole of somebody
    else's ten minutes.
    """

    GAME_ID = PlayerClientTests.GAME_ID
    AGENT_ID = PlayerClientTests.AGENT_ID

    @staticmethod
    def opponent(place: int = 2) -> dict:
        return {
            "place": place,
            "seat_id": f"place-{place}",
            "player_name": "AgentPlace2",
            "controller_label": "pi-gpt-5.6-sol",
            "standing": "active",
            "is_self": False,
        }

    @classmethod
    def health(
        cls, session: dict, *, mine: bool, remaining_s: float = 587.0,
        elapsed_s: float = 13.0, game_state: str = "running",
        prior_end: dict | None = None, turn: int | None = None,
    ) -> dict:
        value = PlayerClientTests.health(
            session, active=mine, game_state=game_state,
        )
        phase = value["phase"]
        if phase is None:
            return value
        if turn is not None:
            phase["turn"] = turn
            value["turns_remaining"] = session["max_turns"] - turn
        phase["state"] = "awaiting_agent"
        phase["timing"] = {
            "mode": "default", "timeout_s": 600.0,
            "deadline_started_at": 1000.0, "deadline_at": 1600.0,
            "elapsed_s": elapsed_s, "remaining_s": remaining_s,
        }
        if not mine:
            phase["waiting_on"] = {
                "kind": "other_seat",
                "summary": (
                    "Seat 2 AgentPlace2 (pi-gpt-5.6-sol) holds turn 3 phase 1 "
                    "and has not ended it."
                ),
                "waiting_s": elapsed_s,
                "seats": [cls.opponent()],
            }
        if prior_end is not None:
            phase["prior_end"] = prior_end
        return value

    @classmethod
    def wake(
        cls, session: dict, reason: str, **options,
    ) -> dict:
        return {
            "schema_version": 2,
            "control_protocol": "full-control-v2",
            "game_id": session["game_id"],
            "agent_id": session["agent_id"],
            "wake_reason": reason,
            "health": cls.health(session, **options),
            "state_revision": None,
        }

    @contextmanager
    def workspace(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "play"
            root.mkdir()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ,
                {"PLAY_STATE_DIR": ".sessions", "PLAY_SESSION": ""},
                clear=False,
            ):
                yield PlayerClientTests.v2_session(root)

    @staticmethod
    def args(session_path: Path, **overrides):
        values = {
            "session": str(session_path), "wait_s": 120.0, "poll_s": 1.0,
            "until": "phase", "for_turn": False, "max_s": None,
            "json_output": False,
        }
        values.update(overrides)
        return argparse.Namespace(**values)

    # ---- P0a: the wake reason the server could always send ----------------

    def test_boundary_recovered_is_a_wake_reason_the_client_accepts(self):
        """The supervisor has always been able to send it; we could not read it.

        It arrives when this seat's native boundary was republished under a
        wait -- and on the `--end --await` path it surfaced as `await failed:`
        *after* the phase end had applied, which is the one moment a client
        must not be telling the agent it does not understand the server.
        """
        self.assertIn("boundary_recovered", client.V2_WAKE_REASONS)
        with self.workspace() as (session_path, session):
            raw = self.wake(session, "boundary_recovered", mine=True)
            clean = client._validate_wait_response(
                raw, session, until="phase", after_state_token=None,
            )
            self.assertEqual(clean["wake_reason"], "boundary_recovered")
            # It is a satisfied wake, not a "come back later" one.
            self.assertEqual(
                client._wait_exit_code(clean), client.V2_WAIT_EXIT_ACTIVE,
            )

    def test_the_served_openapi_lists_every_wake_reason_the_client_takes(self):
        contract = json.loads(
            (client.ROOT / "docs" / "full-control-v2.openapi.json").read_text(
                encoding="utf-8",
            ),
        )
        enum = contract["components"]["schemas"]["WaitEnvelope"][
            "properties"
        ]["wake_reason"]["enum"]
        self.assertEqual(set(enum), client.V2_WAKE_REASONS)

    # ---- P0b: the hint that could never fire ------------------------------

    def test_the_mirror_phase_hint_reads_the_yes_no_the_mirror_writes(self):
        """`state_mirror` writes `active no`; this tested for `active False`."""
        with self.workspace() as (session_path, session):
            mirror = client._mirror_path(session_path)
            state_mirror.update_from_health(
                mirror, "health", self.health(session, mine=False),
            )
            header = (mirror / "state" / "header.txt").read_text(
                encoding="utf-8",
            )
            self.assertIn("active no", header)
            self.assertNotIn("active False", header)
            self.assertIn(
                "your phase is not active",
                client._cached_phase_note(session_path),
            )
            state_mirror.update_from_health(
                mirror, "health", self.health(session, mine=True),
            )
            self.assertEqual(client._cached_phase_note(session_path), "")

    # ---- P1: the exit status carries the wake reason ----------------------

    def test_wait_exit_codes_come_from_real_wait_outcomes(self):
        cases = (
            ("phase_active", {"mine": True}, client.V2_WAIT_EXIT_ACTIVE),
            ("boundary_recovered", {"mine": True},
             client.V2_WAIT_EXIT_ACTIVE),
            ("timeout", {"mine": False}, client.V2_WAIT_EXIT_RETRY),
            ("game_terminal", {"mine": False, "game_state": "completed"},
             client.V2_WAIT_EXIT_TERMINAL),
        )
        with self.workspace() as (session_path, session):
            for reason, options, expected in cases:
                wake = self.wake(session, reason, **options)
                with self.subTest(reason=reason), patch.object(
                    client, "_wait_value", return_value=wake,
                ), redirect_stdout(io.StringIO()):
                    self.assertEqual(
                        client.command_wait(self.args(session_path)),
                        expected,
                    )

    def test_the_json_payload_is_unchanged_by_the_new_exit_status(self):
        with self.workspace() as (session_path, session):
            wake = self.wake(session, "timeout", mine=False)
            stdout = io.StringIO()
            with patch.object(
                client, "_wait_value", return_value=wake,
            ), redirect_stdout(stdout):
                code = client.command_wait(
                    self.args(session_path, json_output=True),
                )
            self.assertEqual(code, client.V2_WAIT_EXIT_RETRY)
            self.assertEqual(json.loads(stdout.getvalue()), wake)

    def test_a_timeout_wake_names_the_holder_instead_of_calling_it_a_wake(self):
        with self.workspace() as (session_path, session):
            wake = self.wake(session, "timeout", mine=False)
            stdout = io.StringIO()
            with patch.object(
                client, "_wait_value", return_value=wake,
            ), redirect_stdout(stdout):
                client.command_wait(self.args(session_path))
            lines = stdout.getvalue().splitlines()
            self.assertIn("still seat 2 AgentPlace2 (pi-gpt-5.6-sol)", lines[0])
            self.assertIn("held 13s", lines[0])
            self.assertIn("9m47s left", lines[0])
            # The old tail pointed at a command that can only be refused.
            self.assertNotIn("next: just turn", lines[0])
            self.assertIn("just wait --for-turn", lines[0])
            self.assertIn(f"[exit {client.V2_WAIT_EXIT_RETRY}]", lines[0])
            self.assertIn("NOT YOUR TURN · seat 2 AgentPlace2", lines[1])

    # ---- P2: bounds and --for-turn ---------------------------------------

    def test_the_wait_ceiling_covers_a_whole_opponent_phase(self):
        self.assertEqual(client.V2_WAIT_S_MAX, 615.0)
        with self.workspace() as (session_path, session):
            args = self.args(session_path, wait_s=615.0)
            with patch.object(
                client, "_v2_response",
                return_value=client.JSONResponse(
                    200, self.wake(session, "phase_active", mine=True),
                ),
            ) as request, redirect_stdout(io.StringIO()):
                client._wait_value(session_path, session, args)
            self.assertIn("wait_s=615", request.call_args.args[1])
            with self.assertRaisesRegex(client.PlayerError, r"\[0, 615\]"):
                client._wait_value(
                    session_path, session,
                    self.args(session_path, wait_s=616.0),
                )

    @contextmanager
    def clocked(self, script):
        """Run a fake `/wait` on a clock that only advances by what it blocked.

        Wall-clock time is the thing under test here -- the whole point of a
        deadline-bounded wait is how long it blocks for -- so the test owns
        the clock rather than sleeping through it.
        """
        now = [0.0]
        blocked: list[float] = []

        def responder(_method, url, _session, **_options):
            query = urllib.parse.parse_qs(urllib.parse.urlsplit(url).query)
            waited = float(query["wait_s"][0])
            blocked.append(waited)
            now[0] += waited
            return client.JSONResponse(200, script(now[0]))

        with patch.object(
            client.time, "monotonic", side_effect=lambda: now[0],
        ), patch.object(client, "_v2_response", side_effect=responder):
            yield now, blocked

    def test_for_turn_is_bounded_by_the_holders_remaining_deadline(self):
        """One call covers one opponent turn, and not a second longer.

        The bound is the holder's own deadline plus one grace window, so a
        holder whose clock has run out cannot keep the caller blocked by
        reporting zero for ever.
        """
        with self.workspace() as (session_path, session):
            def script(elapsed):
                return self.wake(
                    session, "timeout", mine=False,
                    remaining_s=max(0.0, 40.0 - elapsed),
                    elapsed_s=min(600.0, 560.0 + elapsed),
                )

            stdout = io.StringIO()
            with self.clocked(script) as (now, blocked), \
                    redirect_stdout(stdout):
                code = client.command_wait(
                    self.args(session_path, for_turn=True),
                )
            self.assertEqual(code, client.V2_WAIT_EXIT_RETRY)
            # Short internal polls, never one 120 s block.
            self.assertTrue(
                all(item <= client.V2_WAIT_TICK_S for item in blocked), blocked,
            )
            # It waited out the 40 s deadline plus one 15 s grace, and stopped
            # rather than rolling the grace forward against a pinned zero.
            self.assertGreaterEqual(now[0], 40.0)
            self.assertLessEqual(now[0], 40.0 + client.V2_FOR_TURN_GRACE_S)
            # Every tick said what it was waiting on.
            ticks = [
                line for line in stdout.getvalue().splitlines()
                if line.startswith("… waiting on")
            ]
            self.assertEqual(len(ticks), len(blocked) - 1)
            self.assertIn("seat 2 AgentPlace2 (pi-gpt-5.6-sol)", ticks[0])

    def test_for_turn_returns_the_moment_the_phase_is_ours(self):
        with self.workspace() as (session_path, session):
            def script(elapsed):
                return self.wake(
                    session,
                    "phase_active" if elapsed >= 30.0 else "timeout",
                    mine=elapsed >= 30.0,
                    remaining_s=max(0.0, 300.0 - elapsed),
                )

            with self.clocked(script) as (now, _blocked), \
                    redirect_stdout(io.StringIO()):
                code = client.command_wait(
                    self.args(session_path, for_turn=True),
                )
            self.assertEqual(code, client.V2_WAIT_EXIT_ACTIVE)
            self.assertEqual(now[0], 30.0)

    def test_for_turn_max_is_a_hard_ceiling_over_the_holders_deadline(self):
        with self.workspace() as (session_path, session):
            def script(elapsed):
                return self.wake(
                    session, "timeout", mine=False,
                    remaining_s=max(0.0, 600.0 - elapsed),
                )

            with self.clocked(script) as (now, _blocked), \
                    redirect_stdout(io.StringIO()):
                code = client.command_wait(
                    self.args(session_path, for_turn=True, max_s=45.0),
                )
            self.assertEqual(code, client.V2_WAIT_EXIT_RETRY)
            self.assertEqual(now[0], 45.0)

    def test_max_without_for_turn_is_refused_rather_than_ignored(self):
        with self.workspace() as (session_path, _session):
            with self.assertRaisesRegex(client.PlayerError, "--for-turn"):
                client.command_wait(self.args(session_path, max_s=30.0))

    def test_a_plain_wait_still_makes_exactly_one_request(self):
        """Without --for-turn nothing loops: the old shape is untouched."""
        with self.workspace() as (session_path, session):
            with patch.object(
                client, "_v2_response",
                return_value=client.JSONResponse(
                    200, self.wake(session, "timeout", mine=False),
                ),
            ) as request, redirect_stdout(io.StringIO()):
                client.command_wait(self.args(session_path))
            self.assertEqual(request.call_count, 1)
            self.assertIn("wait_s=120", request.call_args.args[1])

    # ---- P3: the marker file ---------------------------------------------

    def test_the_phase_marker_is_written_on_every_tick_of_a_wait(self):
        """The mirror used to freeze for the whole of a blocking wait."""
        with self.workspace() as (session_path, session):
            marker = (
                client._mirror_path(session_path) / "state" / "phase.json"
            )
            seen: list[dict] = []

            def script(elapsed):
                if marker.exists():
                    seen.append(json.loads(marker.read_text(encoding="utf-8")))
                return self.wake(
                    session,
                    "phase_active" if elapsed >= 45.0 else "timeout",
                    mine=elapsed >= 45.0,
                    remaining_s=max(0.0, 300.0 - elapsed),
                    elapsed_s=min(600.0, 300.0 + elapsed),
                )

            with self.clocked(script) as (_now, blocked), \
                    redirect_stdout(io.StringIO()):
                client.command_wait(self.args(session_path, for_turn=True))
            # One marker read per request after the first: it was refreshed
            # between every pair of polls, not once at the end.
            self.assertEqual(len(seen), len(blocked) - 1)
            self.assertEqual(
                [item["deadline_s_left"] for item in seen],
                [285.0, 270.0][: len(seen)],
            )
            final = json.loads(marker.read_text(encoding="utf-8"))
            self.assertTrue(final["active"])
            self.assertIsNone(final["holder"])

    def test_the_phase_marker_is_a_closed_schema_a_watcher_can_branch_on(self):
        with self.workspace() as (session_path, session):
            mirror = client._mirror_path(session_path)
            state_mirror.update_from_health(
                mirror, "health", self.health(session, mine=False),
            )
            value = json.loads(
                (mirror / "state" / "phase.json").read_text(encoding="utf-8"),
            )
            self.assertEqual(set(value), {
                "schema_version", "updated_at", "game_state", "turn", "phase",
                "state", "active", "held_s", "deadline_s_left", "holder",
                "announced",
            })
            self.assertIsNone(value["announced"])
            self.assertEqual(value["schema_version"], 1)
            self.assertFalse(value["active"])
            self.assertEqual(value["turn"], 3)
            self.assertEqual(value["held_s"], 13.0)
            self.assertEqual(value["deadline_s_left"], 587.0)
            self.assertEqual(value["holder"], {
                "place": 2, "seat_id": "place-2",
                "player_name": "AgentPlace2",
                "controller_label": "pi-gpt-5.6-sol",
            })
            # No token, no cursor, nothing opaque ever reaches the file.
            self.assertNotIn("state_", (mirror / "state" / "phase.json")
                             .read_text(encoding="utf-8"))
            # On this seat's own turn there is no holder to name.
            state_mirror.update_from_health(
                mirror, "health", self.health(session, mine=True),
            )
            mine = json.loads(
                (mirror / "state" / "phase.json").read_text(encoding="utf-8"),
            )
            self.assertTrue(mine["active"])
            self.assertIsNone(mine["holder"])

    def test_the_phase_marker_is_written_atomically_and_privately(self):
        with self.workspace() as (session_path, session):
            mirror = client._mirror_path(session_path)
            written = state_mirror.update_from_health(
                mirror, "health", self.health(session, mine=False),
            )
            marker = mirror / "state" / "phase.json"
            self.assertIn(
                marker.resolve(), {item.resolve() for item in written},
            )
            self.assertEqual(stat.S_IMODE(marker.stat().st_mode), 0o600)
            # No temp file survives the write.
            self.assertEqual(
                sorted(
                    item.name for item in marker.parent.iterdir()
                ),
                ["header.txt", "phase.json"],
            )

    # ---- P4a: the strings -------------------------------------------------

    @staticmethod
    def prior_end(source: str, orders: int | None, elapsed_s: float = 600.0):
        return {
            "place": 2, "seat_id": "place-2", "player_name": "AgentPlace2",
            "controller_label": "pi-gpt-5.6-sol",
            "turn": 3, "phase": 0, "source": source,
            "receipt_state": "applied", "resolution": "advanced",
            "elapsed_s": elapsed_s, "orders_submitted": orders,
        }

    def test_a_timeout_with_no_orders_says_they_issued_no_orders(self):
        """The difference between thinking for ten minutes and not being there.

        In the match this came from one seat submitted nothing on nine of
        thirteen turns and every surface reported a played turn.
        """
        with self.workspace() as (_session_path, session):
            cases = (
                (self.prior_end("timeout", 0),
                 "timeout — they issued no orders"),
                (self.prior_end("timeout", 6),
                 "timeout — their deadline passed"),
                (self.prior_end("agent", 7, elapsed_s=32.0), "agent, 7 orders"),
                (self.prior_end("agent", 1, elapsed_s=32.0), "agent, 1 order"),
                (self.prior_end("timeout", None),
                 "timeout — their deadline passed"),
                (self.prior_end("auto_idle", 0),
                 "auto_idle — the service ended their idle phase"),
            )
            for prior, expected in cases:
                health = self.health(session, mine=True, prior_end=prior)
                clean = client._validate_health(health, session)
                line = client._prior_end_line(clean["phase"])
                with self.subTest(expected=expected):
                    self.assertIn(expected, line)
                    self.assertIn(
                        "opponent seat 2 AgentPlace2 (pi-gpt-5.6-sol) ended "
                        "t3/p0 in",
                        line,
                    )
                    self.assertIn(line, "\n".join(client._render_health(clean)))

    def test_health_leads_with_whose_turn_it_is_and_names_them(self):
        with self.workspace() as (session_path, session):
            payload = self.health(
                session, mine=False, remaining_s=13.0, elapsed_s=587.0,
            )
            stdout = io.StringIO()
            with patch.object(
                client, "_v2_response",
                return_value=client.JSONResponse(200, payload),
            ), redirect_stdout(stdout):
                client.command_health(self.args(session_path))
            lines = stdout.getvalue().splitlines()
            self.assertIn(
                "NOT YOUR TURN · seat 2 AgentPlace2 (pi-gpt-5.6-sol) holds "
                "t3/p1 · held 9m47s of 10m0s · 13s left",
                lines[0],
            )
            self.assertIn("just wait --for-turn", lines[-1])
            self.assertIn("exit 0 = go, 75 = still theirs", lines[-1])

    def test_health_on_your_own_turn_says_so_without_a_holder(self):
        with self.workspace() as (_session_path, session):
            clean = client._validate_health(
                self.health(session, mine=True, remaining_s=592.0,
                            elapsed_s=8.0),
                session,
            )
            first = client._render_health(clean)[0]
            self.assertIn("YOUR TURN · t3/p1 · 9m52s left of 10m0s", first)
            self.assertNotIn("NOT YOUR TURN", first)

    def test_an_active_phase_that_cannot_be_ended_is_not_called_your_turn(self):
        """`active` and `actionable` are not the same fact, and never were."""
        with self.workspace() as (_session_path, session):
            payload = self.health(session, mine=True)
            payload["phase"]["state"] = "phase_not_ready"
            clean = client._validate_health(payload, session)
            first = client._render_health(clean)[0]
            self.assertIn("YOUR PHASE · phase_not_ready t3/p1", first)
            self.assertNotIn("YOUR TURN", first)

    def test_prior_end_is_optional_and_strictly_validated_when_present(self):
        with self.workspace() as (_session_path, session):
            without = client._validate_health(
                self.health(session, mine=True), session,
            )
            self.assertNotIn("prior_end", without["phase"])
            self.assertEqual(client._prior_end_line(without["phase"]), "")
            for name, value in (
                ("place", 1),          # never this seat
                ("source", "native"),
                ("receipt_state", "reserved"),
                ("resolution", "queued"),
                ("orders_submitted", -1),
                ("turn", -1),
            ):
                broken = self.prior_end("timeout", 0)
                broken[name] = value
                with self.subTest(field=name), self.assertRaises(
                    client.PlayerError,
                ):
                    client._validate_health(
                        self.health(session, mine=True, prior_end=broken),
                        session,
                    )
            leaked = self.prior_end("timeout", 0)
            leaked["batch_id"] = "must-not-cross-health"
            with self.assertRaises(client.PlayerError):
                client._validate_health(
                    self.health(session, mine=True, prior_end=leaked), session,
                )

    # ---- composites: applied work is never masked by a wait outcome -------

    def test_a_composite_await_never_masks_applied_work(self):
        """An applied phase end must not exit non-zero for a wait outcome.

        And it must not print a briefing for a phase the caller does not
        hold: that briefing's `next: just turn` tail is an invitation to an
        out-of-turn action that can only be refused.
        """
        with self.workspace() as (session_path, session):
            revision = PlayerClientTests.revision(7)
            ended = PlayerClientTests.revision(8)
            phase_end = PlayerClientTests.pregame_action(
                revision, "action_" + "e" * 26, "phase.end", "end",
                "End phase", {"type": "object"}, None,
            )
            page = PlayerClientTests.page(
                session, legal=True, revision=revision, items=[phase_end],
            )

            def responder(method, url, _session, **options):
                if method == "POST":
                    payload = json.loads(
                        options["encoded_body"].decode("utf-8"),
                    )
                    return client.JSONResponse(200, PlayerClientTests.receipt(
                        session, payload["batch_id"], "applied",
                        revision=ended,
                    ))
                return client.JSONResponse(200, page)

            stdout = io.StringIO()
            with patch.object(
                client, "_v2_response", side_effect=responder,
            ), patch.object(
                client, "_wait_until_turn",
                return_value=self.wake(session, "timeout", mine=False),
            ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                code = client.command_turn(self.args(
                    session_path, end_phase=True, await_phase=True, brief=True,
                    decisions=False,
                ))
            text = stdout.getvalue()
            # The applied end is authoritative and its exit code survives.
            self.assertEqual(code, 0)
            self.assertIn("phase end → applied", text)
            # No briefing, and no invitation to act out of turn.
            self.assertIn("not briefed: the phase is not yours yet", text)
            self.assertIn("next: just wait --for-turn", text)
            self.assertNotIn("next: just turn", text)

    def test_a_composite_await_blocks_to_the_holders_deadline(self):
        """`--await` means what it says; 120 s against a 600 s phase did not."""
        with self.workspace() as (session_path, session):
            def script(elapsed):
                return self.wake(
                    session,
                    "phase_active" if elapsed >= 200.0 else "timeout",
                    mine=elapsed >= 200.0,
                    remaining_s=max(0.0, 200.0 - elapsed),
                )

            with self.clocked(script) as (now, blocked), \
                    redirect_stdout(io.StringIO()):
                wake = client._wait_until_turn(
                    session_path, session,
                    client._wait_args(self.args(session_path)),
                    for_turn=False,
                )
            self.assertEqual(wake["wake_reason"], "phase_active")
            # 120 s of the caller's own budget, then 15 s ticks: the first
            # tick past the opponent's 200 s lands at 210.
            self.assertEqual(now[0], 210.0)
            # The first poll is the caller's own --wait-s; the rest are ticks.
            self.assertEqual(blocked[0], 120.0)
            self.assertTrue(
                all(item <= client.V2_WAIT_TICK_S for item in blocked[1:]),
                blocked,
            )

    def test_a_composite_await_in_a_single_seat_game_is_unchanged(self):
        """No holder named, no loop: one poll of exactly --wait-s, as before."""
        with self.workspace() as (session_path, session):
            plain = PlayerClientTests.wait_response(session, "timeout")
            with patch.object(
                client, "_v2_response",
                return_value=client.JSONResponse(200, plain),
            ) as request:
                wake = client._wait_until_turn(
                    session_path, session,
                    client._wait_args(self.args(session_path)),
                    for_turn=False,
                )
            self.assertEqual(wake["wake_reason"], "timeout")
            self.assertEqual(request.call_count, 1)
            self.assertIn("wait_s=120", request.call_args.args[1])

    # ---- the monitor ------------------------------------------------------

    @staticmethod
    def monitor_args(session_path: Path, **overrides):
        values = {
            "session": str(session_path), "wait_s": 120.0, "poll_s": 1.0,
            "once": False, "stop": False, "status": False,
            "exec_command": "", "exit_code": 0, "max_s": None,
            "json_output": False,
        }
        values.update(overrides)
        return argparse.Namespace(**values)

    def scripted_monitor(self, wakes):
        """Feed `_monitor_loop` a fixed sequence of wakes, then stop it.

        The loop is unbounded by design, so the script ends with a terminal
        wake; a test that runs out of wakes fails loudly instead of hanging.
        """
        remaining = list(wakes)

        def blocked(_path, _session, _args, **_options):
            if not remaining:
                raise AssertionError("the monitor asked for one wake too many")
            value = remaining.pop(0)
            if isinstance(value, Exception):
                raise value
            return value

        return patch.object(client, "_wait_until_turn", side_effect=blocked)

    def test_monitor_once_announces_the_open_phase_and_exits(self):
        with self.workspace() as (session_path, session):
            with self.scripted_monitor([
                self.wake(session, "phase_active", mine=True),
            ]), redirect_stdout(io.StringIO()) as stdout, redirect_stderr(
                io.StringIO(),
            ):
                code = client.command_monitor(
                    self.monitor_args(session_path, once=True),
                )
            self.assertEqual(code, 0)
            self.assertEqual(len(stdout.getvalue().splitlines()), 1)

    def test_the_announce_line_keeps_the_shape_harnesses_already_parse(self):
        """Two harnesses parse this line; a notifier must not break a parser.

        It is byte-identical to the header `just wait` printed before the PvP
        legibility work, which redesigned the *waiting* line, not this one.
        """
        with self.workspace() as (session_path, session):
            wake = self.wake(session, "phase_active", mine=True)
            with self.scripted_monitor([wake]), redirect_stdout(
                io.StringIO(),
            ) as stdout, redirect_stderr(io.StringIO()):
                client.command_monitor(
                    self.monitor_args(session_path, once=True),
                )
            line = stdout.getvalue().splitlines()[0]
            self.assertEqual(line, (
                "T3 | woke phase_active | running | "
                "phase awaiting_agent t3/p1 active 587s left | next: just turn"
            ))
            # The legacy shape, reconstructed from the same payload.
            health = wake["health"]
            self.assertEqual(line, (
                f"T{health['phase']['turn']} | woke {wake['wake_reason']} | "
                f"{health['game_state']} | "
                f"{client._phase_text(health['phase'])} | next: just turn"
            ))

    def test_a_still_theirs_wake_is_the_monitors_business_not_the_agents(self):
        with self.workspace() as (session_path, session):
            with self.scripted_monitor([
                self.wake(session, "timeout", mine=False),
                self.wake(session, "timeout", mine=False),
                self.wake(session, "phase_active", mine=True),
            ]), redirect_stdout(io.StringIO()) as stdout, redirect_stderr(
                io.StringIO(),
            ):
                code = client.command_monitor(
                    self.monitor_args(session_path, once=True),
                )
            self.assertEqual(code, 0)
            self.assertEqual(len(stdout.getvalue().splitlines()), 1)

    def test_exit_code_lets_a_harness_declare_how_to_be_told(self):
        """pi escalates on non-zero exit; guessing which status is not a plan."""
        with self.workspace() as (session_path, session):
            with self.scripted_monitor([
                self.wake(session, "phase_active", mine=True),
            ]), redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                code = client.command_monitor(self.monitor_args(
                    session_path, once=True, exit_code=75,
                ))
            self.assertEqual(code, 75)
            with self.assertRaisesRegex(client.PlayerError, "--exit-code"):
                client.command_monitor(self.monitor_args(
                    session_path, once=True, exit_code=300,
                ))

    def test_persistent_monitor_announces_every_turn_until_terminal(self):
        with self.workspace() as (session_path, session):
            with self.scripted_monitor([
                self.wake(session, "phase_active", mine=True),
                self.wake(session, "timeout", mine=False),
                self.wake(session, "phase_active", mine=True, turn=4),
                self.wake(
                    session, "game_terminal", mine=False,
                    game_state="completed",
                ),
            ]), redirect_stdout(io.StringIO()) as stdout, redirect_stderr(
                io.StringIO(),
            ):
                code = client.command_monitor(
                    self.monitor_args(session_path),
                )
            self.assertEqual(code, client.V2_WAIT_EXIT_TERMINAL)
            lines = stdout.getvalue().splitlines()
            self.assertEqual(len(lines), 3)
            self.assertIn("T3 | woke phase_active", lines[0])
            self.assertIn("T4 | woke phase_active", lines[1])
            self.assertIn("GAME OVER", lines[2])

    # ---- idempotency ------------------------------------------------------

    def test_a_second_persistent_monitor_is_a_no_op_that_reports_the_first(self):
        """Singleton by kernel lock, not by bookkeeping."""
        with self.workspace() as (session_path, session):
            holder = {"pid": 41207, "since": "16:21:04", "game_id": "g"}
            stdout = io.StringIO()
            with client._monitor_lock(session_path, holder) as first:
                self.assertIsNone(first, "the first monitor must acquire")
                # From this process, a second acquire sees the lock held.
                self.assertEqual(
                    client._monitor_holder(session_path)["pid"], 41207,
                )
                with self.scripted_monitor([]), redirect_stdout(stdout):
                    code = client.command_monitor(
                        self.monitor_args(session_path),
                    )
            self.assertEqual(code, 0)
            self.assertIn("monitor already running", stdout.getvalue())
            self.assertIn("pid 41207", stdout.getvalue())
            self.assertIn("since 16:21:04", stdout.getvalue())

    def test_the_singleton_holds_against_a_second_process_not_just_a_thread(self):
        """`flock` is per-open-file-description, so in-process agreement
        proves nothing about the case that matters: two `just monitor`
        invocations from a shell."""
        with self.workspace() as (session_path, _session):
            holder = textwrap.dedent(f"""
                import os, sys, time
                sys.path.insert(0, {str(client.ROOT.parent)!r})
                sys.path.insert(0, {str(Path(client.__file__).parent)!r})
                os.environ["PLAY_STATE_DIR"] = ".sessions"
                import client
                from pathlib import Path
                from unittest.mock import patch
                with patch.object(client, "ROOT", Path({str(client.ROOT)!r})):
                    with client._monitor_lock(
                        Path({str(session_path)!r}), {{"pid": os.getpid()}},
                    ) as running:
                        print("blocked" if running else "acquired", flush=True)
                        time.sleep(float(sys.argv[1]))
            """)
            first = subprocess.Popen(
                [sys.executable, "-c", holder, "5"],
                stdout=subprocess.PIPE, text=True,
            )
            try:
                self.assertEqual(first.stdout.readline().strip(), "acquired")
                # A second process is refused while the first lives, and can
                # say who has it.
                second = subprocess.run(
                    [sys.executable, "-c", holder, "0"],
                    capture_output=True, text=True, timeout=30,
                )
                self.assertEqual(second.stdout.strip(), "blocked", second.stderr)
                self.assertEqual(
                    _monitor_holder_pid(session_path), first.pid,
                )
            finally:
                first.terminate()
                first.wait(timeout=30)
                first.stdout.close()
            # The kernel released it when the holder died: nothing to reap.
            self.assertIsNone(client._monitor_holder(session_path))
            third = subprocess.run(
                [sys.executable, "-c", holder, "0"],
                capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(third.stdout.strip(), "acquired", third.stderr)

    def test_a_released_lock_leaves_nothing_for_the_next_monitor_to_reap(self):
        """Crash recovery is the kernel's job, so there is no stale state."""
        with self.workspace() as (session_path, _session):
            with client._monitor_lock(session_path, {"pid": 1234}):
                self.assertIsNotNone(client._monitor_holder(session_path))
            self.assertIsNone(client._monitor_holder(session_path))
            self.assertEqual(
                stat.S_IMODE(
                    client._monitor_lock_path(session_path).stat().st_mode,
                ),
                0o600,
            )

    def test_once_takes_no_lock_so_the_two_bindings_compose(self):
        with self.workspace() as (session_path, session):
            with client._monitor_lock(session_path, {"pid": 4242}):
                with self.scripted_monitor([
                    self.wake(session, "phase_active", mine=True),
                ]), redirect_stdout(io.StringIO()) as stdout, redirect_stderr(
                    io.StringIO(),
                ):
                    code = client.command_monitor(
                        self.monitor_args(session_path, once=True),
                    )
            self.assertEqual(code, 0)
            self.assertNotIn("already running", stdout.getvalue())

    def test_a_restarted_persistent_monitor_does_not_repeat_a_turn(self):
        """Process-singleton is not enough; the announcement must dedupe too."""
        with self.workspace() as (session_path, session):
            open_phase = self.wake(session, "phase_active", mine=True)
            with self.scripted_monitor([
                open_phase,
                self.wake(
                    session, "game_terminal", mine=False,
                    game_state="completed",
                ),
            ]), redirect_stdout(io.StringIO()) as stdout, redirect_stderr(
                io.StringIO(),
            ):
                client.command_monitor(self.monitor_args(session_path))
            self.assertIn("T3 | woke phase_active", stdout.getvalue())
            marker = state_mirror.read_phase_marker(
                client._mirror_path(session_path),
            )
            self.assertEqual(marker["announced"], [0, 3, 1])
            # Restarted onto the same still-open phase, it says nothing about
            # it and waits for the next one.
            with self.scripted_monitor([
                open_phase,
                self.wake(
                    session, "game_terminal", mine=False,
                    game_state="completed",
                ),
            ]), redirect_stdout(io.StringIO()) as stdout, redirect_stderr(
                io.StringIO(),
            ):
                client.command_monitor(self.monitor_args(session_path))
            self.assertNotIn("woke phase_active", stdout.getvalue())
            self.assertIn("GAME OVER", stdout.getvalue())

    def test_once_always_answers_even_on_an_already_announced_phase(self):
        """A wake-up call that stayed silent would hang the harness for ever."""
        with self.workspace() as (session_path, session):
            wake = self.wake(session, "phase_active", mine=True)
            state_mirror.update_phase_marker(
                client._mirror_path(session_path), wake["health"],
                announced=[0, 3, 1],
            )
            with self.scripted_monitor([wake]), redirect_stdout(
                io.StringIO(),
            ) as stdout, redirect_stderr(io.StringIO()):
                code = client.command_monitor(
                    self.monitor_args(session_path, once=True),
                )
            self.assertEqual(code, 0)
            self.assertIn("woke phase_active", stdout.getvalue())

    def test_an_unrelated_command_never_clears_the_announced_tuple(self):
        """`just health` must not make a restarted monitor repeat itself."""
        with self.workspace() as (session_path, session):
            mirror = client._mirror_path(session_path)
            health = self.health(session, mine=True)
            state_mirror.update_phase_marker(
                mirror, health, announced=[0, 3, 1],
            )
            state_mirror.update_from_health(mirror, "health", health)
            self.assertEqual(
                state_mirror.read_phase_marker(mirror)["announced"], [0, 3, 1],
            )

    def test_a_rollback_replays_the_turn_and_is_announced_again(self):
        """The incarnation is in the tuple because a replayed turn is new."""
        with self.workspace() as (session_path, session):
            mirror = client._mirror_path(session_path)
            state_mirror.update_phase_marker(
                mirror, self.health(session, mine=True), announced=[0, 3, 1],
            )
            replayed = self.wake(session, "phase_active", mine=True)
            replayed["health"]["last_phase_end"] = self.own_end(
                incarnation=1, turn=2, phase=1, source="agent",
            )
            with self.scripted_monitor([replayed]), redirect_stdout(
                io.StringIO(),
            ) as stdout, redirect_stderr(io.StringIO()):
                client.command_monitor(
                    self.monitor_args(session_path, once=True),
                )
            self.assertIn("woke phase_active", stdout.getvalue())
            self.assertEqual(
                state_mirror.read_phase_marker(mirror)["announced"],
                [1, 3, 1],
            )

    # ---- the missed-turn alarm --------------------------------------------

    @staticmethod
    def own_end(
        *, incarnation=0, turn=3, phase=1, source="timeout",
        orders=0, elapsed_s=600.0,
    ):
        return {
            "sequence": 4, "incarnation": incarnation, "turn": turn,
            "phase": phase, "place": 1, "seat_id": "place-1",
            "player_name": "AgentPlace1", "player_color": "#0067A5",
            "controller_label": "codex-test-model",
            "controller_type": "external", "source": source,
            "receipt_state": "applied", "resolution": "advanced",
            "deadline_started_at": 1000.0, "ended_at": 1600.0,
            "elapsed_s": elapsed_s, "orders_submitted": orders,
        }

    def test_a_turn_that_opened_and_died_unplayed_raises_the_alarm(self):
        """The exact failure this whole surface exists for.

        A machine that slept through a phase gets told so; nothing else in
        the workspace is positioned to notice.
        """
        with self.workspace() as (session_path, session):
            missed = self.wake(session, "phase_active", mine=True, turn=6)
            missed["health"]["last_phase_end"] = self.own_end(
                turn=5, phase=1, source="timeout", orders=0,
            )
            with self.scripted_monitor([missed]), redirect_stdout(
                io.StringIO(),
            ) as stdout, redirect_stderr(io.StringIO()):
                client.command_monitor(
                    self.monitor_args(session_path, once=True),
                )
            lines = stdout.getvalue().splitlines()
            self.assertEqual(lines[0], (
                "T5 | MISSED | your phase t5/p1 opened and was ended by "
                "timeout after 600s — you issued no orders"
            ))
            # The alarm never swallows the announcement of the open phase.
            self.assertIn("T6 | woke phase_active", lines[1])

    def test_a_second_consecutive_miss_escalates_its_wording(self):
        """Two phases lost in a row means the notification path itself is
        broken, and the wording stops being a note and becomes an alarm."""
        with self.workspace() as (session_path, session):
            def reconnect(open_turn, ended_turn, source="timeout"):
                wake = self.wake(
                    session, "phase_active", mine=True, turn=open_turn,
                )
                wake["health"]["last_phase_end"] = self.own_end(
                    turn=ended_turn, phase=1, source=source,
                    orders=4 if source == "agent" else 0,
                )
                return wake

            with self.scripted_monitor([
                # A turn this seat played itself: no alarm, and it is the
                # last turn we can honestly say an order was issued on.
                reconnect(4, 3, source="agent"),
                # Slept through t5 entirely; woke into t6.
                reconnect(6, 5),
                # Slept again: t7 opened and died without ever being seen.
                reconnect(8, 7),
                self.wake(
                    session, "game_terminal", mine=False,
                    game_state="completed",
                ),
            ]), redirect_stdout(io.StringIO()) as stdout, redirect_stderr(
                io.StringIO(),
            ):
                client.command_monitor(self.monitor_args(session_path))
            text = stdout.getvalue()
            self.assertIn("T5 | MISSED | your phase t5/p1", text)
            self.assertNotIn("MISSED ×2 | your phase t5", text)
            self.assertIn("T7 | MISSED ×2 | your phase t7/p1", text)
            self.assertIn("you have not issued an order since t3.", text)
            self.assertIn(
                "Your monitor is not reaching you — check it now; the game "
                "is advancing without you.",
                text,
            )

    def test_a_turn_the_agent_was_told_about_and_ignored_is_not_a_miss(self):
        """The alarm is "nothing reached you", not "you played badly"."""
        with self.workspace() as (session_path, session):
            open_phase = self.wake(session, "phase_active", mine=True, turn=6)
            ignored = self.wake(session, "phase_active", mine=True, turn=7)
            ignored["health"]["last_phase_end"] = self.own_end(
                turn=6, phase=1, source="timeout", orders=0,
            )
            with self.scripted_monitor([
                open_phase, ignored,
                self.wake(
                    session, "game_terminal", mine=False,
                    game_state="completed",
                ),
            ]), redirect_stdout(io.StringIO()) as stdout, redirect_stderr(
                io.StringIO(),
            ):
                client.command_monitor(self.monitor_args(session_path))
            self.assertNotIn("MISSED", stdout.getvalue())

    def test_a_phase_this_seat_ended_itself_is_never_a_missed_turn(self):
        """`--await` opening a turn the monitor never announced is normal."""
        with self.workspace() as (session_path, session):
            played = self.wake(session, "phase_active", mine=True, turn=6)
            played["health"]["last_phase_end"] = self.own_end(
                turn=5, phase=1, source="agent", orders=4, elapsed_s=32.0,
            )
            with self.scripted_monitor([played]), redirect_stdout(
                io.StringIO(),
            ) as stdout, redirect_stderr(io.StringIO()):
                client.command_monitor(
                    self.monitor_args(session_path, once=True),
                )
            self.assertNotIn("MISSED", stdout.getvalue())

    def test_orders_issued_without_a_phase_end_are_reported_as_such(self):
        with self.workspace() as (_session_path, _session):
            line = client._missed_line(
                self.own_end(turn=5, source="timeout", orders=3), 1, None,
            )
            self.assertIn(
                "you issued 3 orders but never ended the phase", line,
            )
            unknown = client._missed_line(
                {**self.own_end(turn=5), "orders_submitted": None}, 1, None,
            )
            self.assertIn("was ended by timeout after 600s", unknown)
            self.assertNotIn("no orders", unknown)

    # ---- transport faults are absorbed, never raised ----------------------

    def test_a_dropped_socket_is_retried_with_backoff_not_raised(self):
        """A laptop sleep is what a left-running monitor exists to survive."""
        with self.workspace() as (session_path, session):
            slept: list[float] = []
            with self.scripted_monitor([
                client.PlayerError("connection reset"),
                client.PlayerError("connection reset"),
                self.wake(session, "phase_active", mine=True),
            ]), patch.object(
                client.time, "sleep", side_effect=slept.append,
            ), redirect_stdout(io.StringIO()) as stdout, redirect_stderr(
                io.StringIO(),
            ) as stderr:
                code = client.command_monitor(
                    self.monitor_args(session_path, once=True),
                )
            self.assertEqual(code, 0)
            self.assertEqual(slept, [
                client.V2_MONITOR_BACKOFF_START_S,
                client.V2_MONITOR_BACKOFF_START_S * 2,
            ])
            self.assertIn("retrying after connection reset", stderr.getvalue())
            self.assertIn("woke phase_active", stdout.getvalue())

    def test_the_backoff_is_capped_so_a_dead_service_is_not_hammered(self):
        with self.workspace() as (session_path, session):
            slept: list[float] = []
            faults = [client.PlayerError("down")] * 8
            with self.scripted_monitor([
                *faults, self.wake(session, "phase_active", mine=True),
            ]), patch.object(
                client.time, "sleep", side_effect=slept.append,
            ), redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                client.command_monitor(
                    self.monitor_args(session_path, once=True),
                )
            self.assertEqual(max(slept), client.V2_MONITOR_BACKOFF_MAX_S)
            self.assertEqual(slept[-1], client.V2_MONITOR_BACKOFF_MAX_S)

    def test_a_rebound_workspace_stops_the_monitor(self):
        """A monitor must never watch a game the workspace has left."""
        with self.workspace() as (session_path, session):
            other = session_path.with_name("other-seat.json")
            # `just use` rewrote the binding under a monitor already watching
            # the seat it resolved at startup.
            with patch.object(
                client, "_load_session", return_value=(other, session),
            ), self.scripted_monitor([]), redirect_stdout(
                io.StringIO(),
            ) as stdout:
                code = client._monitor_loop(
                    self.monitor_args(session_path), session_path, session,
                    once=True, hook="", exit_code=0,
                )
            self.assertEqual(code, 0)
            self.assertIn("rebound to another seat", stdout.getvalue())

    # ---- the four channels ------------------------------------------------

    def test_every_announcement_reaches_the_marker_and_the_log(self):
        with self.workspace() as (session_path, session):
            mirror = client._mirror_path(session_path)
            with self.scripted_monitor([
                self.wake(session, "phase_active", mine=True),
            ]), redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                client.command_monitor(
                    self.monitor_args(session_path, once=True),
                )
            marker = state_mirror.read_phase_marker(mirror)
            self.assertTrue(marker["active"])
            self.assertEqual(marker["announced"], [0, 3, 1])
            log = (mirror / "state" / "monitor.log").read_text(
                encoding="utf-8",
            )
            self.assertIn("monitor started --once", log)
            self.assertIn("woke phase_active", log)
            # Append-only: a second run adds to it rather than replacing it.
            with self.scripted_monitor([
                self.wake(session, "game_terminal", mine=False,
                          game_state="completed"),
            ]), redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                client.command_monitor(
                    self.monitor_args(session_path, once=True),
                )
            grown = (mirror / "state" / "monitor.log").read_text(
                encoding="utf-8",
            )
            self.assertTrue(grown.startswith(log), grown)
            self.assertIn("GAME OVER", grown)

    def test_the_monitor_writes_the_marker_and_nothing_else(self):
        """A process alive for the whole game is not a second writer of the
        projections a real command owns."""
        with self.workspace() as (session_path, session):
            mirror = client._mirror_path(session_path)
            with self.scripted_monitor([
                self.wake(session, "phase_active", mine=True),
            ]), redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                client.command_monitor(
                    self.monitor_args(session_path, once=True),
                )
            self.assertEqual(
                sorted(item.name for item in (mirror / "state").iterdir()),
                ["monitor.log", "phase.json"],
            )

    def test_the_monitor_never_reads_or_writes_the_revision_cursor(self):
        """Staying in phase mode is what makes it incapable of racing."""
        with self.workspace() as (session_path, session):
            with patch.object(
                client, "_v2_response",
                return_value=client.JSONResponse(
                    200, self.wake(session, "phase_active", mine=True),
                ),
            ) as request, patch.object(
                client, "_load_v2_client_state",
            ) as cached, redirect_stdout(io.StringIO()), redirect_stderr(
                io.StringIO(),
            ):
                client.command_monitor(
                    self.monitor_args(session_path, once=True),
                )
            cached.assert_not_called()
            self.assertIn("until=phase", request.call_args.args[1])
            self.assertNotIn("after_state_token", request.call_args.args[1])
            self.assertFalse(
                client._v2_state_path(session_path).exists(),
            )

    def test_a_stateless_wait_is_refused_outside_phase_mode(self):
        with self.workspace() as (session_path, session):
            with self.assertRaisesRegex(
                client.PlayerError, "phase-mode only",
            ):
                client._wait_value(
                    session_path, session,
                    self.args(session_path, until="revision"),
                    stateless=True,
                )

    def test_monitor_exec_runs_a_hook_with_the_game_in_its_environment(self):
        with self.workspace() as (session_path, session):
            recorded: list[dict] = []

            def fake_run(command, shell, env):
                recorded.append({"command": command, "shell": shell, **{
                    key: env[key] for key in (
                        "FREECIV_GAME_ID", "FREECIV_TURN", "FREECIV_PHASE",
                        "FREECIV_YOUR_TURN", "FREECIV_DEADLINE_S",
                        "FREECIV_HOLDER_LABEL",
                    )
                }})
                return subprocess.CompletedProcess(command, 0)

            with self.scripted_monitor([
                self.wake(session, "phase_active", mine=True),
            ]), patch.object(
                client.subprocess, "run", side_effect=fake_run,
            ), redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                code = client.command_monitor(self.monitor_args(
                    session_path, once=True, exec_command="notify-me",
                ))
            self.assertEqual(code, 0)
            self.assertEqual(recorded, [{
                "command": "notify-me", "shell": True,
                "FREECIV_GAME_ID": session["game_id"],
                "FREECIV_TURN": "3", "FREECIV_PHASE": "1",
                "FREECIV_YOUR_TURN": "1", "FREECIV_DEADLINE_S": "587",
                "FREECIV_HOLDER_LABEL": "",
            }])
            log = (
                client._mirror_path(session_path) / "state" / "monitor.log"
            ).read_text(encoding="utf-8")
            # Every hook invocation is recorded with its string: the log is
            # what makes a contract violation auditable after the fact.
            self.assertIn("exec notify-me", log)

    def test_a_failing_hook_is_reported_and_never_stops_the_monitor(self):
        with self.workspace() as (session_path, session):
            with self.scripted_monitor([
                self.wake(session, "phase_active", mine=True),
            ]), patch.object(
                client.subprocess, "run",
                return_value=subprocess.CompletedProcess("boom", 3),
            ), redirect_stdout(io.StringIO()), redirect_stderr(
                io.StringIO(),
            ) as stderr:
                code = client.command_monitor(self.monitor_args(
                    session_path, once=True, exec_command="boom",
                ))
            self.assertEqual(code, 0)
            self.assertIn("--exec exited 3", stderr.getvalue())
            self.assertIn(
                "exec exited 3",
                (
                    client._mirror_path(session_path)
                    / "state" / "monitor.log"
                ).read_text(encoding="utf-8"),
            )

    def test_a_hook_that_plays_the_game_is_refused(self):
        """`--exec 'just do ... --end'` is an autoplay bot in one flag.

        Imperfect by design -- a wrapper script defeats it -- but it turns a
        silent contract violation into a deliberate bypass.
        """
        with self.workspace() as (session_path, _session):
            for hook in (
                'just do "u1 fortify" --end',
                "just turn --end --await",
                "./play batch --action-id a1",
                "sleep 1; just retry --batch-id b",
                "notify && just start",
            ):
                with self.subTest(hook=hook), self.assertRaisesRegex(
                    client.PlayerError, "never plays",
                ):
                    client.command_monitor(self.monitor_args(
                        session_path, once=True, exec_command=hook,
                    ))

    def test_a_hook_that_only_notifies_is_allowed(self):
        with self.workspace() as (session_path, _session):
            for hook in (
                "curl -X POST https://hooks.example/notify",
                "osascript -e 'display notification \"your turn\"'",
                "echo done >> /tmp/turns.log",
                "cat state/phase.json | jq .turn",
            ):
                with self.subTest(hook=hook):
                    self.assertEqual(client._monitor_exec_refusal(hook), "")

    # ---- --status and --stop ----------------------------------------------

    def test_status_reports_the_running_monitor_and_what_it_watches(self):
        with self.workspace() as (session_path, session):
            stdout = io.StringIO()
            with redirect_stdout(stdout):
                self.assertEqual(
                    client.command_monitor(
                        self.monitor_args(session_path, status=True),
                    ),
                    client.V2_WAIT_EXIT_RETRY,
                )
            self.assertIn("monitor not running", stdout.getvalue())
            state_mirror.update_phase_marker(
                client._mirror_path(session_path),
                self.health(session, mine=False),
            )
            stdout = io.StringIO()
            with client._monitor_lock(
                session_path, {"pid": 41207, "since": "16:21:04"},
            ), redirect_stdout(stdout):
                self.assertEqual(
                    client.command_monitor(
                        self.monitor_args(session_path, status=True),
                    ),
                    0,
                )
            text = stdout.getvalue()
            self.assertIn("monitor running (pid 41207", text)
            self.assertIn("since 16:21:04", text)
            self.assertIn("watching t3/p1", text)
            self.assertIn("seat 2 holds it", text)

    def test_stop_signals_the_monitor_and_confirms_it_released(self):
        with self.workspace() as (session_path, _session):
            stdout = io.StringIO()
            with redirect_stdout(stdout):
                self.assertEqual(
                    client.command_monitor(
                        self.monitor_args(session_path, stop=True),
                    ),
                    0,
                )
            self.assertIn("monitor not running", stdout.getvalue())

            released: list[int] = []
            lock = client._monitor_lock(session_path, {"pid": 4242})
            lock.__enter__()

            def fake_kill(pid, signal_number):
                released.append(pid)
                self.assertEqual(signal_number, client.signal.SIGTERM)
                lock.__exit__(None, None, None)

            stdout = io.StringIO()
            with patch.object(
                client.os, "kill", side_effect=fake_kill,
            ), redirect_stdout(stdout):
                code = client.command_monitor(
                    self.monitor_args(session_path, stop=True),
                )
            self.assertEqual(code, 0)
            self.assertEqual(released, [4242])
            self.assertIn("monitor stopped (pid 4242)", stdout.getvalue())

    def test_stopping_an_already_dead_monitor_is_not_an_error(self):
        with self.workspace() as (session_path, _session):
            with client._monitor_lock(session_path, {"pid": 4242}):
                stdout = io.StringIO()
                with patch.object(
                    client.os, "kill", side_effect=ProcessLookupError,
                ), redirect_stdout(stdout):
                    code = client.command_monitor(
                        self.monitor_args(session_path, stop=True),
                    )
            self.assertEqual(code, 0)
            self.assertIn("monitor not running", stdout.getvalue())

    def test_monitor_keeps_the_same_json_escape_hatch_as_wait(self):
        with self.workspace() as (session_path, session):
            wake = self.wake(session, "phase_active", mine=True)
            stdout = io.StringIO()
            with self.scripted_monitor([wake]), redirect_stdout(
                stdout,
            ), redirect_stderr(io.StringIO()):
                client.command_monitor(self.monitor_args(
                    session_path, once=True, json_output=True,
                ))
            self.assertEqual(json.loads(stdout.getvalue()), wake)


if __name__ == "__main__":
    unittest.main()
