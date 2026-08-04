from __future__ import annotations

import io
import json
import os
import stat
import subprocess
import sys
import tempfile
import threading
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
                args = type("Args", (), {"session": str(session_path)})()
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
                self.assertEqual(result["shown"], 3)
                self.assertFalse(result["truncated"])
                self.assertEqual(
                    set(result["actions"][0]),
                    {"action_id", "kind", "target", "argument_schema"},
                )
                self.assertEqual(
                    result["actions"][1]["probability"], uncertain,
                )
                self.assertNotIn(
                    "internal_detail_kept_only_in_cache", result["actions"][0],
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
        self.assertIn("game_state: lobby", prompt)
        self.assertIn("pregame_nations", prompt)
        self.assertIn("pregame.set_ready", prompt)
        self.assertIn("Keep this same conversation active", prompt)
        self.assertIn("do not give a final answer", prompt)
        self.assertIn("If a wait command itself fails", prompt)

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
                self.assertEqual(migrated["schema_version"], 2)
                self.assertEqual(migrated["actions"], {})
                self.assertEqual(migrated["pending_catalogs"], {})
                self.assertIn("batch_saved", migrated["batches"])
                persisted = json.loads(
                    session_path.with_suffix(".v2-state").read_text(),
                )
                self.assertEqual(persisted, migrated)

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
                            "arguments": "{}",
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
                    "arguments": "{}",
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
        self.assertEqual(
            schemas["HealthEnvelope"]["x-freeciv-lifecycle"]["lobby"]
            ["state_sections"],
            [
                "overview", "pregame_nations", "pregame_styles",
                "pregame_teams", "votes",
            ],
        )
        self.assertEqual(
            set(schemas["HealthEnvelope"]["x-freeciv-lifecycle"]["lobby"]
                ["legal_action_kinds"]),
            {
                "pregame.configure", "pregame.set_team",
                "pregame.set_ready", "player.cast_vote",
            },
        )
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


if __name__ == "__main__":
    unittest.main()
