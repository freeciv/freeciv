from __future__ import annotations

import io
import json
import os
import stat
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest.mock import patch

import client


class PlayerClientTests(unittest.TestCase):
    def test_player_just_join_never_expands_a_bearer_into_argv(self):
        source = (client.ROOT / "justfile").read_text(encoding="utf-8")
        self.assertNotIn("join_token", source)
        self.assertNotIn("--join-token", source)
        self.assertIn('--invite "{{ invite }}"', source)

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
                "state": "failed",
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
            self.assertIn("Do not use the strategic", stderr.getvalue())
            self.assertNotIn("just next --session", stderr.getvalue())
            session = next((root / ".sessions" / game_id).glob("*.json"))
            saved = json.loads(session.read_text(encoding="utf-8"))
            self.assertEqual(saved["control_protocol"], "full-control-v2")

            next_args = type("Args", (), {
                "session": str(session), "after_turn": 0, "wait_s": 0,
            })()
            with patch.object(client, "ROOT", root), patch.dict(
                os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
            ), self.assertRaisesRegex(client.PlayerError, "strategic-v1 only"):
                client.command_next(next_args)

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


if __name__ == "__main__":
    unittest.main()
