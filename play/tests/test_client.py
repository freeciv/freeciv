from __future__ import annotations

import argparse
import io
import json
import os
import re
import stat
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest.mock import patch

import client


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

    def test_player_just_join_never_expands_a_bearer_into_argv(self):
        source = (client.ROOT / "justfile").read_text(encoding="utf-8")
        self.assertNotIn("join_token", source)
        self.assertNotIn("--join-token", source)
        self.assertIn('--invite "{{ invite }}"', source)

    def test_v2_just_options_parse_and_batch_json_is_one_exact_argument(self):
        dry_run_commands = (
            (
                "legal", "--session", "/tmp/session", "--actor_id",
                "actor_opaque", "--target_id", "tile_opaque",
            ),
            (
                "legal", "--session", "/tmp/session", "--kind",
                "unit.order", "--all", "--offset", "2", "--limit", "3",
            ),
            (
                "batch", "--session", "/tmp/session", "--action_id",
                "action_opaque", "--arguments", "{}",
            ),
            (
                "receipt", "--session", "/tmp/session", "--batch_id",
                "batch_opaque",
            ),
            (
                "retry", "--session", "/tmp/session", "--batch_id",
                "batch_opaque",
            ),
            (
                "wait", "--session", "/tmp/session", "--wait_s", "120",
                "--poll_s", "1",
            ),
            (
                "wait", "--session", "/tmp/session", "--wait-s", "120",
                "--poll-s", "1",
            ),
            ("health", "--session", "/tmp/session", "--json"),
            ("turn", "--session", "/tmp/session", "--json"),
            (
                "state", "--session", "/tmp/session", "--section", "units",
                "--json",
            ),
            ("legal", "--session", "/tmp/session", "--json"),
            (
                "batch", "--session", "/tmp/session", "--action_id",
                "action_opaque", "--arguments", "{}", "--json",
            ),
            (
                "receipt", "--session", "/tmp/session", "--batch_id",
                "batch_opaque", "--json",
            ),
            (
                "retry", "--session", "/tmp/session", "--batch_id",
                "batch_opaque", "--json",
            ),
            # --session is optional on every recipe.
            ("health",),
            ("turn",),
            ("state", "--section", "units"),
            ("legal", "--actor_id", "u1", "--all"),
            ("legal", "--actor_id", "u1", "--target_id", "T(31,72)", "--all"),
            ("legal", "--kind", "phase.end", "--all"),
            ("batch", "--action_id", "a1", "--arguments", "{}"),
            ("receipt", "--batch_id", "batch_opaque"),
            ("retry", "--batch_id", "batch_opaque"),
            ("wait",),
            # The P2 fast paths.
            ("turn", "--end", "--await"),
            ("turn", "--end", "--await", "--wait_s", "60", "--poll_s", "2"),
            ("turn", "--json"),
            (
                "start", "--nation", "English", "--leader", "Ada",
                "--female", "--style", "European",
            ),
            ("do", "u1 found_city London; u2 move 32,73"),
            ("do", "u1 fortify", "--continue-on-error"),
            ("do", "u1 fortify", "--json"),
            ("show",),
            ("show", "units"),
            ("show", "u1"),
            ("show", "--grep", "found_city"),
            ("show", "--grep", "u[0-9]+ Settlers", "--regex"),
        )
        for command in dry_run_commands:
            with self.subTest(command=command[0]):
                completed = subprocess.run(
                    ("just", "--dry-run", *command), cwd=client.ROOT,
                    check=False, capture_output=True, text=True,
                )
                self.assertEqual(
                    completed.returncode, 0,
                    completed.stdout + completed.stderr,
                )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bin_dir = root / "bin"
            bin_dir.mkdir()
            capture = root / "argv"
            fake_python = bin_dir / "python3"
            fake_python.write_text(
                "#!/bin/sh\n"
                ": \"${V2_CAPTURE:?}\"\n"
                "printf '%s\\n' \"$@\" >\"$V2_CAPTURE\"\n",
                encoding="utf-8",
            )
            fake_python.chmod(0o700)
            payload = '{"city_name":"O\'Brien"}'
            environment = dict(os.environ)
            environment["PATH"] = f"{bin_dir}:{environment['PATH']}"
            environment["V2_CAPTURE"] = str(capture)
            completed = subprocess.run(
                (
                    "just", "batch", "--session", "/tmp/session",
                    "--action_id", "action_opaque", "--arguments", payload,
                ),
                cwd=client.ROOT, env=environment, check=False,
                capture_output=True, text=True,
            )
            self.assertEqual(
                completed.returncode, 0,
                completed.stdout + completed.stderr,
            )
            argv = capture.read_text(encoding="utf-8").splitlines()
            self.assertEqual(argv[-2:], ["--arguments", payload])

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
        for command in (
            ("result", self.GAME_ID),
            ("result", "--game_id", self.GAME_ID),
        ):
            with self.subTest(command=command):
                completed = subprocess.run(
                    ("just", "--dry-run", *command), cwd=client.ROOT,
                    check=False, capture_output=True, text=True,
                )
                self.assertEqual(
                    completed.returncode, 0,
                    completed.stdout + completed.stderr,
                )

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

                # `wait` has no `--json` flag, so its refusal has no flag that
                # could turn prose back off: it must print JSON unconditionally.
                payload = self.error(code="conflict")
                stdout = io.StringIO()
                with patch.object(
                    client, "_wait_value",
                    side_effect=client.V2ResponseError(409, payload),
                ), redirect_stdout(stdout), redirect_stderr(io.StringIO()):
                    self.assertEqual(
                        client.main(["wait", "--session", str(session_path)]),
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
        self.assertIn("goto@33,70/4steps", lines[2])
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
                self.assertEqual(len(lines), 1)
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
                self.assertIn("phase awaiting_agent t3/p1 active", lines[0])
                self.assertIn("179s left", lines[0])
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
        self.assertEqual(card[1], "session .sessions/x/codex-test.json")
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
            "scope": {"actor_id": actor_id, "actor_type": "unit"},
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
                # is refused before any socket is opened.
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
        self.assertIn("default gives each agent 180 seconds", prompt)
        self.assertIn("blitz gives 60 seconds", prompt)
        self.assertIn("infinite has no agent deadline", prompt)
        self.assertIn("choose its action\ndirectly", prompt)
        self.assertIn("Do not write, launch, or delegate", prompt)
        self.assertIn("--session SESSION_FILE", prompt)
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
        self.assertIn("`--session` is optional", prompt)
        for retired in (
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
                with self.assertRaisesRegex(
                    client.PlayerError, "shared .sessions/current pointer is ambiguous",
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
                self.assertEqual(migrated["schema_version"], 4)
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
                self.assertEqual(upgraded["schema_version"], 4)
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
                self.assertEqual(recorded["schema_version"], 4)
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
                    self.assertEqual(client.command_wait(args), 0)

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
                    self.assertEqual(client.command_wait(args), 0)
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
        justfile = (client.ROOT / "justfile").read_text(encoding="utf-8")
        menu = [
            line for line in justfile.splitlines()
            if line.strip().startswith('@echo "  just ')
        ]
        self.assertTrue(menu)
        self.assertNotIn("--session", "\n".join(menu))

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
                ):
                    with self.subTest(order=text):
                        resolved = client._resolve_order(
                            state, session_path, text,
                        )
                        self.assertEqual(
                            resolved["action_id"], goal["action_id"],
                        )
                        self.assertEqual(resolved["arguments"], {"tech": tech})

                # A verb the catalog never advertised is never guessed at.
                for text in ("research goal Currency", "u1 set_goal Currency"):
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
                self.assertEqual(len(lines), 3)
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
                self.assertEqual(len(lines), 2, lines)
                self.assertIn("→ applied rev9/t3", lines[0])
                self.assertEqual(lines[1], "1/1 applied rev9/t3")

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
                self.assertEqual(request.call_count, 1)
                lines = stdout.getvalue().splitlines()
                self.assertEqual(len(lines), 3)
                self.assertIn("rejected", lines[0])
                self.assertEqual(lines[1], "0/2 applied rev7/t3")
                self.assertIn("stopped after order 1", lines[2])
                self.assertIn("1 not sent", lines[2])
                self.assertIn("--continue-on-error", lines[2])

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
                self.assertEqual(len(lines), 3)
                self.assertIn("rejected", lines[0])
                self.assertIn("applied", lines[1])
                self.assertEqual(lines[2], "1/2 applied rev7/t3")

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

                def wait(path, current, args):
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
                self.assertIn("woke phase_active", lines[1])
                self.assertIn("awaiting_agent", lines[1])
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
                self.assertEqual(len(lines), 2)
                self.assertTrue(
                    lines[0].startswith("configure English Ada female →"),
                    lines[0],
                )
                self.assertTrue(lines[1].startswith("set ready → applied"))

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

                # Sex is required and exclusive; both refusals precede the
                # first request.
                with patch.object(client, "_v2_response") as blocked:
                    for values in (
                        {"male": True, "female": True},
                        {"male": False, "female": False},
                    ):
                        with self.assertRaisesRegex(
                            client.PlayerError, "--male or --female",
                        ):
                            client.command_start(type("Args", (), {
                                "session": str(session_path),
                                "nation": "English", "leader": "Ada",
                                "style": "", **values,
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
    def just_recipes(source: str) -> dict[str, frozenset[str]]:
        """Map each player recipe to the exact long options it declares."""
        recipes: dict[str, frozenset[str]] = {}
        pending: set[str] = set()
        for line in source.splitlines():
            if not line or line[0].isspace() or ":=" in line:
                continue
            attribute = re.fullmatch(
                r'\[arg\("([A-Za-z_0-9]+)"'
                r'(?:,\s*long(?:="([^"]+)")?)?'
                r'(?:,\s*value="[^"]*")?\)\]',
                line.strip(),
            )
            if attribute is not None:
                pending.add("--" + (attribute.group(2) or attribute.group(1)))
                continue
            if line.startswith("["):
                continue
            head = re.match(r"([a-z][a-z_0-9]*)(?:\s+\S.*)?:\s*$", line)
            if head is not None:
                recipes[head.group(1)] = frozenset(pending)
                pending = set()
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

        `just help` is the one document an agent is told to read, so it is
        budgeted like a payload.  The full reference stays available for
        harness authors and is deliberately not what `just help` prints.
        """
        source = (client.ROOT / "justfile").read_text(encoding="utf-8")

        # A hard-coded `sed -n '1,Np'` window silently truncates the moment the
        # file grows past N, with no test and no error.  Print whole files.
        self.assertNotRegex(source, r"sed -n '1,\d+p'")
        printed = dict(re.findall(r"\n(help|rules):\n\s+@cat (\S+)\n", source))
        self.assertEqual(set(printed), {"help", "rules"})
        self.assertEqual(printed["help"], "docs/play.md")

        budgets = {"docs/play.md": 4096, "docs/gameplay.md": 8192}
        for recipe, relative in printed.items():
            document = client.ROOT / relative
            self.assertTrue(document.is_file(), relative)
            text = document.read_text(encoding="utf-8")
            self.assertLessEqual(
                len(text), budgets[relative],
                f"just {recipe} prints {len(text)} chars of {relative}; "
                f"the agent-facing budget is {budgets[relative]}",
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
        source = (client.ROOT / "justfile").read_text(encoding="utf-8")
        recipes = self.just_recipes(source)
        self.assertLessEqual(
            {
                "join", "start", "turn", "do", "show", "state", "legal",
                "batch", "receipt", "retry", "wait", "health", "result",
            },
            set(recipes),
        )

        # Every recipe forwards only options its own argparse subcommand has.
        subcommands = next(
            action for action in client.parser()._actions
            if isinstance(action, argparse._SubParsersAction)
        ).choices
        options = {
            name: {
                string for action in command._actions
                for string in action.option_strings
            }
            for name, command in subcommands.items()
        }
        forwarded = set()
        for chunk in source.split("\n\n"):
            invocation = re.search(r"client\.py (\w+)", chunk)
            if invocation is None:
                continue
            name = invocation.group(1)
            forwarded.add(name)
            self.assertIn(name, options)
            for flag in sorted(set(re.findall(
                r"(?<![\w-])--[a-z][a-z0-9-]*", chunk[invocation.end():],
            ))):
                self.assertIn(
                    flag, options[name], f"just {name} forwards {flag}",
                )
        self.assertEqual(forwarded, set(options))

        # The join protocol card, the `just` menu, and both agent-facing docs
        # name only commands and options that exist.
        self.assertGreaterEqual(
            self.assert_documented_commands_exist(
                "protocol card", "\n".join(client.V2_PROTOCOL_CARD), recipes,
                markdown=False,
            ),
            8,
        )
        self.assert_documented_commands_exist(
            "justfile", source, recipes, markdown=False,
        )
        for name in (
            "play.md", "commands.md", "full-control-v2.md", "gameplay.md",
        ):
            document = (client.ROOT / "docs" / name).read_text(encoding="utf-8")
            self.assertGreater(
                self.assert_documented_commands_exist(name, document, recipes),
                0,
            )

        # The bare `just` menu is the short workflow and never re-types a path.
        menu = [
            line.strip() for line in source.splitlines()
            if line.strip().startswith(("@echo \"  just ", "@echo '  just "))
        ]
        self.assertNotIn("--session", "\n".join(menu))
        for fast_path in (
            "just turn", "just do ", "just turn --end --await", "just start ",
            "just show ",
        ):
            self.assertTrue(
                any(fast_path in line for line in menu), fast_path,
            )

    def test_workspace_boundary_docs_teach_the_v2_fast_paths(self):
        """AGENTS.md/README.md are read before join and must not contradict it.

        Both files predate the redesign; the failure they caused was a *third*
        protocol contract that forbade the fast paths outright and sent the
        agent to the 28k-char harness-author reference.
        """
        source = (client.ROOT / "justfile").read_text(encoding="utf-8")
        recipes = self.just_recipes(source)
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


if __name__ == "__main__":
    unittest.main()
