import http.client
import json
import os
import socket
import stat
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from agent_eval.replay_gateway import (
    GATEWAY_KIND,
    GATEWAY_PROTOCOL_VERSION,
    MAX_PROXY_JSON_BYTES,
    TerminalArchive,
    _archive_outcome,
    _archive_result,
    _archive_status,
    _disk_game_row,
    gateway_config,
    make_replay_gateway_server,
    run_replay_gateway,
)
from agent_eval.tests.test_save_replay import save_text


GAME_ID = "game_" + "g" * 24


class FakeUpstreamHandler(BaseHTTPRequestHandler):
    server: "FakeUpstreamServer"

    def log_message(self, format, *args):
        return

    def do_GET(self):
        self.server.requests.append({
            "method": "GET",
            "path": self.path,
            "headers": {key.lower(): value for key, value in self.headers.items()},
        })
        path = self.path.split("?", 1)[0]
        route_path = path.removeprefix("/freeciv")
        if self.server.portless_offline:
            self._reply(
                502,
                b"<html><body>upstream offline</body></html>",
                self.server.portless_content_type,
                {"X-Portless": self.server.portless_header},
            )
            return
        if route_path == "/v1/games":
            self._reply(
                self.server.games_status,
                self.server.games_body,
                "application/json; charset=utf-8",
            )
            return
        if route_path == f"/v1/games/{GAME_ID}/replay.json":
            self._reply(
                self.server.replay_status,
                self.server.replay_body,
                "application/json; charset=utf-8",
            )
            return
        if route_path == f"/v1/games/{GAME_ID}/board.json":
            self._reply(
                self.server.board_status,
                self.server.board_body,
                "application/json; charset=utf-8",
            )
            return
        if route_path == f"/v1/games/{GAME_ID}/watch.json":
            if self.server.watch_redirect:
                self._reply(
                    302,
                    b"",
                    "text/plain",
                    {"Location": f"/v1/games/{GAME_ID}/me/next"},
                )
                return
            self._reply(200, self.server.watch_body, "application/json")
            return
        if route_path == f"/v1/games/{GAME_ID}/frames/0.png":
            self._reply(
                200, self.server.png_body, "image/png",
                {"Cache-Control": "public, max-age=2", "ETag": '"frame"'},
            )
            return
        if route_path == f"/v1/games/{GAME_ID}/video.mp4" and self.server.truncate_video:
            self.send_response(200)
            self.send_header("Content-Type", "video/mp4")
            self.send_header("Content-Length", str(len(self.server.video_body) + 32))
            self.end_headers()
            self.wfile.write(self.server.video_body)
            self.close_connection = True
            return
        if route_path in {
            f"/v1/games/{GAME_ID}",
            f"/v1/games/{GAME_ID}/status",
            f"/v1/games/{GAME_ID}/result",
            f"/v1/games/{GAME_ID}/frames",
            f"/v1/games/{GAME_ID}/frames/latest.png",
            f"/v1/games/{GAME_ID}/video.mp4",
        }:
            self._reply(200, b"{}", "application/json")
            return
        self._reply(404, b'{"error":"missing"}', "application/json")

    def _reply(self, status, body, content_type, headers=None):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        try:
            self.wfile.write(body)
        except BrokenPipeError:
            pass


class FakeUpstreamServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self):
        super().__init__(("127.0.0.1", 0), FakeUpstreamHandler)
        self.requests = []
        self.games_status = 200
        self.games_body = b'{"schema_version":1,"games":[]}'
        self.replay_status = 200
        self.replay_body = (
            b'{"schema_version":1,"game_id":"' + GAME_ID.encode()
            + b'","available":false,"snapshots":[],"next_after_turn":0,'
              b'"has_more":false}'
        )
        self.watch_body = json.dumps({
            "schema_version": 1,
            "frames": [{
                "index": 0,
                "source_name": "turn-0001.map.ppm",
                "png_url": (
                    f"http://127.0.0.1:{self.server_address[1]}"
                    f"/v1/games/{GAME_ID}/frames/0.png"
                ),
            }],
        }, separators=(",", ":")).encode()
        self.png_body = b"\x89PNG\r\n\x1a\nframe"
        self.board_status = 200
        self.board_body = json.dumps({
            "schema_version": 1,
            "game_id": GAME_ID,
            "turn": 1,
            "width": 1,
            "height": 1,
        }, separators=(",", ":")).encode()
        self.video_body = b"partial-video"
        self.watch_redirect = False
        self.truncate_video = False
        self.portless_offline = False
        self.portless_header = "1"
        self.portless_content_type = "text/html; charset=utf-8"


class ReplayGatewayTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.runs_root = self.root / "runs"
        self.cache_root = self.root / "cache"
        self.runs_root.mkdir()

        self.upstream = FakeUpstreamServer()
        self.upstream_thread = threading.Thread(
            target=self.upstream.serve_forever, daemon=True,
        )
        self.upstream_thread.start()
        self.upstream_url = (
            f"http://127.0.0.1:{self.upstream.server_address[1]}"
        )
        self.loader_calls = []

        def loader(
            runs_root,
            game_id,
            resolved_places,
            *,
            after_turn,
            limit,
            cache_root,
            complete,
        ):
            args = (
                runs_root, game_id, resolved_places, after_turn, limit,
                cache_root, complete,
            )
            self.loader_calls.append(args)
            return {
                "schema_version": 1,
                "game_id": GAME_ID,
                "available": True,
                "catalog": None,
                "snapshots": [{"turn": args[3] + 1, "players": []}],
                "next_after_turn": args[3] + 1,
                "has_more": False,
                "complete": args[6],
                "replay_warnings": [],
            }

        self.loader = loader
        self.gateway = make_replay_gateway_server(
            "127.0.0.1",
            0,
            self.upstream_url,
            self.runs_root,
            self.cache_root,
            repo_root=self.root,
            replay_loader=self.loader,
        )
        self.gateway_thread = threading.Thread(
            target=self.gateway.serve_forever, daemon=True,
        )
        self.gateway_thread.start()
        self.gateway_url = (
            f"http://127.0.0.1:{self.gateway.server_address[1]}"
        )

    def tearDown(self):
        self.gateway.shutdown()
        self.gateway.server_close()
        self.gateway_thread.join(3)
        self.upstream.shutdown()
        self.upstream.server_close()
        self.upstream_thread.join(3)
        self.temporary.cleanup()

    def request(self, path, *, method="GET", headers=None, data=None):
        request = urllib.request.Request(
            self.gateway_url + path,
            headers=headers or {},
            data=data,
            method=method,
        )
        try:
            with urllib.request.urlopen(request, timeout=3) as response:
                return response.status, response.headers, response.read()
        except urllib.error.HTTPError as error:
            with error:
                return error.code, error.headers, error.read()

    def write_manifest(
        self,
        game_id=GAME_ID,
        *,
        state="running",
        created_at=10,
        label="pi-gpt-5.5",
    ):
        run = self.runs_root / game_id
        run.mkdir()
        value = {
            "schema_version": 1,
            "game_id": game_id,
            "state": state,
            "benchmark_valid": None,
            "created_at": created_at,
            "current_turn": 7,
            "joined_agents": 1,
            "config": {
                "mode": "single",
                "places": 2,
                "max_agents": 1,
                "turns": 5000,
            },
            "resolved_places": [{
                "place": 1,
                "seat_id": "place-1",
                "player_name": "AgentPlace1",
                "player_color": "#0067A5",
                "controller": "agent",
                "joined": True,
                "controller_label": label,
                "controller_metadata": {
                    "model": "gpt-5.5",
                    "token": "must-not-leak",
                },
                "controller_fingerprint": "must-not-leak",
            }, {
                "place": 2,
                "seat_id": "place-2",
                "player_name": "NativePlace2",
                "player_color": "#F38400",
                "controller": "native_classic_ai",
                "joined": False,
            }],
            "owner_token": "must-not-leak",
        }
        (run / "manifest.json").write_text(json.dumps(value), encoding="utf-8")
        return value

    def write_replay_rows(
        self, game_id=GAME_ID, *, turns=(1, 596), torn_tail=False,
    ):
        path = self.runs_root / game_id / "replay.jsonl"
        rendered = "".join(
            json.dumps({
                "schema_version": 1, "game_id": game_id, "turn": turn,
            }) + "\n"
            for turn in turns
        )
        if torn_tail:
            rendered += '{"schema_version":1,"turn":9'
        path.write_text(rendered, encoding="utf-8")

    def write_terminal_archive(self):
        manifest = self.write_manifest(state="invalid", created_at=20)
        manifest.update({
            "benchmark_valid": False,
            "finished_at": 30,
            "returncode": 0,
            "invalid_reasons": [
                "turn 2 timed out waiting for agent_test",
            ],
        })
        run = self.runs_root / GAME_ID
        (run / "manifest.json").write_text(
            json.dumps(manifest), encoding="utf-8",
        )
        report = {
            "episode": "/private/archive/path",
            "manifest": {
                **manifest,
                "owner_token": "must-not-leak",
            },
            "score": {
                "final_turn": 2,
                "players": [{
                    "seat_id": "place-1",
                    "player_id": 0,
                    "name": "AgentPlace1",
                    "rank": 1,
                    "score": 22,
                    "controller_fingerprint": "must-not-leak",
                    "metrics": {
                        "score": 22,
                        "cities": 1,
                        "secret_token": 999,
                    },
                }, {
                    "seat_id": "place-2",
                    "player_id": 1,
                    "name": "NativePlace2",
                    "rank": 2,
                    "score": 10,
                    "metrics": {"score": 10, "cities": 2},
                }],
            },
            "seat_stats": {
                "place-1": {"private_action": "must-not-leak"},
            },
        }
        (run / "report.json").write_text(
            json.dumps(report), encoding="utf-8",
        )
        saves = run / "saves"
        saves.mkdir()
        (saves / "turn-0001-auto.sav").write_text(
            save_text(1), encoding="utf-8",
        )
        (saves / "turn-0001-M-test.map.ppm").write_text(
            "P3\n"
            '# playerno:0:color:(  0, 103, 165):name:"AgentPlace1"\n'
            '# playerno:2:color:(255,  20, 147):name:"Blackbeard"\n'
            "1 1\n255\n0 0 0\n",
            encoding="utf-8",
        )
        frames = run / "watch_frames"
        frames.mkdir()
        (frames / "000000.png").write_bytes(b"\x89PNG\r\n\x1a\narchive")
        (run / "game.mp4").write_bytes(b"archive-video")
        (run / "decisions.jsonl").write_text(
            '{"action":{"secret":"must-not-leak"}}\n', encoding="utf-8",
        )
        (run / "auth.json").write_text(
            '{"owner_token":"must-not-leak"}\n', encoding="utf-8",
        )
        return run

    def test_health_has_exact_checkout_and_upstream_identity(self):
        status, headers, body = self.request("/health")
        self.assertEqual(status, 200)
        self.assertEqual(headers["X-Content-Type-Options"], "nosniff")
        value = json.loads(body)
        self.assertEqual(value["kind"], GATEWAY_KIND)
        self.assertEqual(value["protocol_version"], GATEWAY_PROTOCOL_VERSION)
        self.assertEqual(value["repo_root"], str(self.root.resolve()))
        self.assertEqual(value["upstream_service_url"], self.upstream_url)
        self.assertEqual(value["runs_root"], str(self.runs_root.resolve()))
        self.assertEqual(value["cache_root"], str(self.cache_root.resolve()))
        self.assertEqual(value["port"], self.gateway.server_address[1])
        self.assertRegex(value["identity"], r"^[0-9a-f]{20}$")

    def test_archive_projections_publish_the_recorded_control_protocol(self):
        def archive(manifest):
            return TerminalArchive(
                game_id=GAME_ID,
                run_root=self.runs_root / GAME_ID,
                manifest=manifest,
                report={},
                state="completed",
                benchmark_valid=True,
                places=[],
                leaderboard=[],
                outcome={"status": "complete"},
            )

        def manifest(config_protocol=None, top_level=None):
            config = {
                "mode": "single", "places": 2, "max_agents": 1, "turns": 10,
            }
            if config_protocol is not None:
                config["control_protocol"] = config_protocol
            value = {
                "game_id": GAME_ID,
                "state": "completed",
                "benchmark_valid": True,
                "created_at": 1,
                "current_turn": 2,
                "joined_agents": 1,
                "config": config,
                "resolved_places": [],
            }
            if top_level is not None:
                value["control_protocol"] = top_level
            return value

        for protocol in ("full-control-v2", "strategic-v1"):
            with self.subTest(protocol=protocol):
                value = manifest(protocol)
                self.assertEqual(
                    _disk_game_row(value)["control_protocol"], protocol,
                )
                self.assertEqual(
                    _archive_status(archive(value), "")["control_protocol"],
                    protocol,
                )

        # Older manifests recorded the protocol only at the top level.
        top_only = manifest(top_level="full-control-v2")
        self.assertEqual(
            _disk_game_row(top_only)["control_protocol"], "full-control-v2",
        )
        self.assertEqual(
            _archive_status(archive(top_only), "")["control_protocol"],
            "full-control-v2",
        )

        # Runs predating the field stay unlabeled; the viewer names the default.
        for legacy in (manifest(), manifest("full-control-v9")):
            self.assertNotIn("control_protocol", _disk_game_row(legacy))
            self.assertNotIn(
                "control_protocol", _archive_status(archive(legacy), ""),
            )

    def test_archive_projections_publish_the_recorded_ai_difficulty(self):
        def archive(manifest):
            return TerminalArchive(
                game_id=GAME_ID,
                run_root=self.runs_root / GAME_ID,
                manifest=manifest,
                report={},
                state="completed",
                benchmark_valid=True,
                places=[],
                leaderboard=[],
                outcome={"status": "complete"},
            )

        def manifest(difficulty=None):
            config = {
                "mode": "single", "places": 2, "max_agents": 1, "turns": 10,
            }
            if difficulty is not None:
                config["difficulty"] = difficulty
            return {
                "game_id": GAME_ID,
                "state": "completed",
                "benchmark_valid": True,
                "created_at": 1,
                "current_turn": 2,
                "joined_agents": 1,
                "config": config,
                "resolved_places": [{
                    "place": 2,
                    "seat_id": "place-2",
                    "player_name": "NativePlace2",
                    "player_color": "#F38400",
                    "controller": "native_classic_ai",
                    "joined": False,
                }],
            }

        for level in ("hard", "cheating"):
            with self.subTest(level=level):
                value = manifest(level)
                self.assertEqual(
                    _disk_game_row(value)["ai_difficulty"], level,
                )
                self.assertEqual(
                    _archive_status(archive(value), "")["ai_difficulty"], level,
                )
                # A native place inherits the game's level even when the row
                # itself predates the per-place field.
                self.assertEqual(
                    _disk_game_row(value)["resolved_places"][0]["ai_difficulty"],
                    level,
                )

        # Archives predating the field publish a null, never a guessed level,
        # and never omit the key -- the viewer then names an unqualified CPU.
        for legacy in (manifest(), manifest("deity")):
            self.assertIsNone(_disk_game_row(legacy)["ai_difficulty"])
            self.assertIsNone(
                _archive_status(archive(legacy), "")["ai_difficulty"],
            )
            self.assertNotIn(
                "ai_difficulty",
                _disk_game_row(legacy)["resolved_places"][0],
            )

    def test_archive_projections_preserve_explicit_timing_but_not_legacy(self):
        def manifest(config):
            return {
                "game_id": GAME_ID,
                "state": "completed",
                "benchmark_valid": True,
                "created_at": 1,
                "current_turn": 2,
                "joined_agents": 1,
                "config": {
                    "mode": "single",
                    "places": 2,
                    "max_agents": 1,
                    "turns": 10,
                    **config,
                },
                "resolved_places": [],
            }

        for config, expected in (
            (
                {"timing_mode": "default", "action_timeout_s": 180},
                {"timing_mode": "default", "action_timeout_s": 180.0},
            ),
            (
                {"timing_mode": "blitz", "action_timeout_s": 60},
                {"timing_mode": "blitz", "action_timeout_s": 60.0},
            ),
            (
                {"timing_mode": "infinite", "action_timeout_s": None},
                {"timing_mode": "infinite", "action_timeout_s": None},
            ),
            (
                {"timing_mode": "custom", "action_timeout_s": 7.5},
                {"timing_mode": "custom", "action_timeout_s": 7.5},
            ),
        ):
            with self.subTest(config=config):
                value = manifest(config)
                archive = TerminalArchive(
                    game_id=GAME_ID,
                    run_root=self.runs_root / GAME_ID,
                    manifest=value,
                    report={},
                    state="completed",
                    benchmark_valid=True,
                    places=[],
                    leaderboard=[],
                    outcome={"status": "complete"},
                )
                row = _disk_game_row(value)
                status = _archive_status(archive, "")
                result = _archive_result(archive, "")
                for key, expected_value in expected.items():
                    self.assertEqual(row[key], expected_value)
                    self.assertEqual(status[key], expected_value)
                    self.assertEqual(result[key], expected_value)

        legacy = manifest({"action_timeout_s": 60})
        legacy_archive = TerminalArchive(
            game_id=GAME_ID,
            run_root=self.runs_root / GAME_ID,
            manifest=legacy,
            report={},
            state="completed",
            benchmark_valid=True,
            places=[],
            leaderboard=[],
            outcome={"status": "complete"},
        )
        for value in (
            _disk_game_row(legacy),
            _archive_status(legacy_archive, ""),
            _archive_result(legacy_archive, ""),
        ):
            self.assertNotIn("timing_mode", value)
            self.assertNotIn("action_timeout_s", value)

        for inconsistent in (
            {"timing_mode": "default", "action_timeout_s": 60},
            {"timing_mode": "blitz", "action_timeout_s": 180},
            {"timing_mode": "infinite", "action_timeout_s": 60},
        ):
            with self.subTest(inconsistent=inconsistent):
                row = _disk_game_row(manifest(inconsistent))
                self.assertNotIn("timing_mode", row)
                self.assertNotIn("action_timeout_s", row)

    def test_watch_json_is_byte_preserved_and_credentials_are_not_forwarded(self):
        status, _, body = self.request(
            f"/v1/games/{GAME_ID}/watch.json",
            headers={
                "Authorization": "Bearer private",
                "Cookie": "session=private",
                "X-Forwarded-Host": "attacker.invalid",
            },
        )
        self.assertEqual(status, 200)
        self.assertEqual(body, self.upstream.watch_body)
        self.assertIn(
            f"http://127.0.0.1:{self.upstream.server_address[1]}",
            json.loads(body)["frames"][0]["png_url"],
        )
        forwarded = self.upstream.requests[-1]["headers"]
        self.assertNotIn("authorization", forwarded)
        self.assertNotIn("cookie", forwarded)
        self.assertNotIn("x-forwarded-host", forwarded)
        self.assertEqual(forwarded["accept"], "application/json")

    def test_upstream_redirect_cannot_escape_the_public_allowlist(self):
        self.upstream.watch_redirect = True
        status, _, body = self.request(
            f"/v1/games/{GAME_ID}/watch.json",
        )
        self.assertEqual(status, 502)
        self.assertIn("redirect", json.loads(body)["error"])
        self.assertEqual(
            [request["path"] for request in self.upstream.requests],
            [f"/v1/games/{GAME_ID}/watch.json"],
        )

    def test_binary_frame_is_streamed_with_safe_headers(self):
        status, headers, body = self.request(
            f"/v1/games/{GAME_ID}/frames/0.png",
        )
        self.assertEqual(status, 200)
        self.assertEqual(body, self.upstream.png_body)
        self.assertEqual(headers.get_content_type(), "image/png")
        self.assertEqual(headers["Cache-Control"], "public, max-age=2")
        self.assertEqual(headers["ETag"], '"frame"')

    def test_truncated_binary_closes_without_appending_a_second_response(self):
        self.upstream.truncate_video = True
        connection = http.client.HTTPConnection(
            "127.0.0.1", self.gateway.server_address[1], timeout=3,
        )
        try:
            connection.request("GET", f"/v1/games/{GAME_ID}/video.mp4")
            response = connection.getresponse()
            self.assertEqual(response.status, 200)
            with self.assertRaises(http.client.IncompleteRead) as context:
                response.read()
            self.assertEqual(context.exception.partial, self.upstream.video_body)
        finally:
            connection.close()

    def test_private_routes_and_all_mutations_never_reach_upstream(self):
        initial = len(self.upstream.requests)
        for method in ("HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"):
            with self.subTest(method=method):
                status, headers, _ = self.request(
                    f"/v1/games/{GAME_ID}/join", method=method,
                )
                self.assertEqual(status, 405)
                self.assertEqual(headers["Allow"], "GET")
        for path in (
            f"/v1/games/{GAME_ID}/join",
            f"/v1/games/{GAME_ID}/me/next",
            f"/v1/games/{GAME_ID}/native-viewer",
            f"/internal/v1/games/{GAME_ID}/turns",
            f"/watch/{GAME_ID}",
        ):
            with self.subTest(path=path):
                self.assertEqual(self.request(path)[0], 404)
        self.assertEqual(len(self.upstream.requests), initial)

    def test_get_body_is_rejected_without_forwarding(self):
        initial = len(self.upstream.requests)
        status, _, body = self.request(
            f"/v1/games/{GAME_ID}/status",
            method="GET",
            data=b"private body",
        )
        self.assertEqual(status, 400)
        self.assertIn("not accepted", json.loads(body)["error"])
        self.assertEqual(len(self.upstream.requests), initial)

    def test_replay_uses_upstream_success_without_parser(self):
        status, _, body = self.request(
            f"/v1/games/{GAME_ID}/replay.json?after_turn=2&limit=3",
        )
        self.assertEqual(status, 200)
        self.assertEqual(body, self.upstream.replay_body)
        self.assertEqual(self.loader_calls, [])
        self.assertEqual(
            self.upstream.requests[-1]["path"],
            f"/v1/games/{GAME_ID}/replay.json?after_turn=2&limit=3",
        )

    def test_replay_404_falls_back_to_autosaves_with_public_manifest(self):
        self.upstream.replay_status = 404
        self.write_manifest(state="completed")
        status, _, body = self.request(
            f"/v1/games/{GAME_ID}/replay.json?limit=7&after_turn=12",
        )
        self.assertEqual(status, 200)
        value = json.loads(body)
        self.assertEqual(value["next_after_turn"], 13)
        self.assertEqual(len(self.loader_calls), 1)
        args = self.loader_calls[0]
        self.assertEqual(args[0], self.runs_root.resolve())
        self.assertEqual(args[1], GAME_ID)
        self.assertEqual(args[3:5], (12, 7))
        self.assertEqual(args[5], self.cache_root.resolve())
        self.assertTrue(args[6])
        self.assertEqual(args[2][0]["controller_label"], "pi-gpt-5.5")
        self.assertEqual(args[2][0]["model"], "gpt-5.5")
        self.assertNotIn("controller_metadata", args[2][0])
        self.assertNotIn("controller_fingerprint", args[2][0])
        self.assertEqual(
            args[2][1]["controller_label"], "Freeciv Classic AI",
        )

    def test_default_gateway_loader_matches_save_replay_keyword_api(self):
        self.upstream.replay_status = 404
        self.write_manifest()
        (self.runs_root / GAME_ID / "saves").mkdir()
        gateway = make_replay_gateway_server(
            "127.0.0.1",
            0,
            self.upstream_url,
            self.runs_root,
            self.cache_root / "actual-parser",
            repo_root=self.root,
        )
        worker = threading.Thread(target=gateway.serve_forever, daemon=True)
        worker.start()
        try:
            url = (
                f"http://127.0.0.1:{gateway.server_address[1]}"
                f"/v1/games/{GAME_ID}/replay.json?after_turn=3&limit=2"
            )
            with urllib.request.urlopen(url, timeout=3) as response:
                value = json.load(response)
            self.assertFalse(value["available"])
            self.assertEqual(value["snapshots"], [])
            self.assertEqual(value["next_after_turn"], 3)
            self.assertFalse(value["has_more"])
        finally:
            gateway.shutdown()
            gateway.server_close()
            worker.join(3)

    def test_replay_upstream_failure_does_not_use_disk_fallback(self):
        self.upstream.replay_status = 500
        self.write_manifest()
        status, _, body = self.request(
            f"/v1/games/{GAME_ID}/replay.json",
        )
        self.assertEqual(status, 500)
        self.assertEqual(json.loads(body)["error"], "upstream returned HTTP 500")
        self.assertEqual(self.loader_calls, [])

    def test_board_is_upstream_first_and_query_is_strict(self):
        status, _, body = self.request(
            f"/v1/games/{GAME_ID}/board.json?turn=1",
        )
        self.assertEqual(status, 200)
        self.assertEqual(body, self.upstream.board_body)
        self.assertEqual(
            self.upstream.requests[-1]["path"],
            f"/v1/games/{GAME_ID}/board.json?turn=1",
        )
        initial = len(self.upstream.requests)
        for query in (
            "", "turn=0", "turn=-1", "turn=nope",
            "turn=1&turn=2", "turn=1&other=2",
        ):
            with self.subTest(query=query):
                suffix = f"?{query}" if query else ""
                self.assertEqual(self.request(
                    f"/v1/games/{GAME_ID}/board.json{suffix}",
                )[0], 400)
        self.assertEqual(len(self.upstream.requests), initial)

    def test_board_preserves_configured_upstream_path_prefix(self):
        gateway = make_replay_gateway_server(
            "127.0.0.1",
            0,
            self.upstream_url + "/freeciv",
            self.runs_root,
            self.cache_root / "prefixed",
            repo_root=self.root,
        )
        worker = threading.Thread(target=gateway.serve_forever, daemon=True)
        worker.start()
        try:
            url = (
                f"http://127.0.0.1:{gateway.server_address[1]}"
                f"/v1/games/{GAME_ID}/board.json?turn=1"
            )
            with urllib.request.urlopen(url, timeout=3) as response:
                self.assertEqual(json.load(response)["turn"], 1)
            self.assertEqual(
                self.upstream.requests[-1]["path"],
                f"/freeciv/v1/games/{GAME_ID}/board.json?turn=1",
            )
        finally:
            gateway.shutdown()
            gateway.server_close()
            worker.join(3)

    def test_replay_query_is_strict_before_upstream_or_disk_access(self):
        initial = len(self.upstream.requests)
        for query in (
            "after_turn=-1",
            "limit=0",
            "limit=251",
            "limit=nope",
            "after_turn=1&after_turn=2",
            "unknown=1",
        ):
            with self.subTest(query=query):
                status, _, _ = self.request(
                    f"/v1/games/{GAME_ID}/replay.json?{query}",
                )
                self.assertEqual(status, 400)
        self.assertEqual(len(self.upstream.requests), initial)
        self.assertEqual(self.loader_calls, [])

    def test_games_404_uses_sorted_sanitized_disk_index(self):
        self.upstream.games_status = 404
        older = self.write_manifest(created_at=1, label="codex-gpt")
        second_id = "game_" + "h" * 24
        newer = self.write_manifest(
            second_id, created_at=2, label="claude-sonnet",
        )
        (self.runs_root / ("game_" + "x" * 24)).mkdir()
        (self.runs_root / ("game_" + "x" * 24) / "manifest.json").write_text(
            "{broken", encoding="utf-8",
        )
        if hasattr(os, "symlink"):
            os.symlink(
                self.runs_root / GAME_ID,
                self.runs_root / ("game_" + "s" * 24),
            )

        status, _, body = self.request("/v1/games")
        self.assertEqual(status, 200)
        value = json.loads(body)
        self.assertEqual(
            [row["game_id"] for row in value["games"]],
            [newer["game_id"], older["game_id"]],
        )
        rendered = body.decode()
        self.assertNotIn("must-not-leak", rendered)
        self.assertNotIn("controller_metadata", rendered)
        self.assertNotIn("controller_fingerprint", rendered)
        self.assertNotIn("owner_token", rendered)
        first = value["games"][0]
        self.assertEqual(first["current_turn"], 7)
        self.assertEqual(first["turns"], 5000)
        self.assertEqual(first["watch_path"], f"/watch/{second_id}")
        self.assertEqual(
            first["resolved_places"][1]["controller_label"],
            "Freeciv Classic AI",
        )

    def test_games_success_merges_terminal_archives_and_live_ids_win(self):
        self.write_terminal_archive()
        self.upstream.games_body = b'{"schema_version":1,"games":[]}'
        merged = json.loads(self.request("/v1/games")[2])
        self.assertEqual(
            [row["game_id"] for row in merged["games"]], [GAME_ID],
        )
        self.assertEqual(merged["games"][0]["state"], "invalid")

        self.upstream.games_body = json.dumps({
            "schema_version": 1,
            "games": [{
                "game_id": GAME_ID,
                "state": "running",
                "watch_path": f"/watch/{GAME_ID}",
            }],
        }, separators=(",", ":")).encode()
        deduplicated = json.loads(self.request("/v1/games")[2])
        self.assertEqual(len(deduplicated["games"]), 1)
        self.assertEqual(deduplicated["games"][0]["state"], "running")

    def test_games_lists_orphaned_run_as_interrupted(self):
        # A run whose supervisor died before finalizing the manifest is
        # neither live upstream nor terminal on disk. It must appear as
        # "interrupted" with its turn recovered from the replay tail —
        # not vanish (the 596-turn game_a8 incident).
        self.upstream.games_body = b'{"schema_version":1,"games":[]}'
        self.write_manifest()
        self.write_replay_rows(turns=(1, 596))
        husk = "game_" + "k" * 24
        self.write_manifest(husk, created_at=3)

        value = json.loads(self.request("/v1/games")[2])
        rows = {row["game_id"]: row for row in value["games"]}
        self.assertNotIn(husk, rows)
        row = rows[GAME_ID]
        self.assertEqual(row["state"], "interrupted")
        self.assertEqual(row["current_turn"], 596)
        self.assertEqual(row["outcome"]["status"], "interrupted")
        self.assertIn("turn 596", row["outcome"]["summary"])

    def test_games_live_upstream_row_is_never_relabeled(self):
        self.write_manifest()
        self.write_replay_rows()
        self.upstream.games_body = json.dumps({
            "schema_version": 1,
            "games": [{
                "game_id": GAME_ID,
                "state": "running",
                "watch_path": f"/watch/{GAME_ID}",
            }],
        }, separators=(",", ":")).encode()
        value = json.loads(self.request("/v1/games")[2])
        self.assertEqual(len(value["games"]), 1)
        self.assertEqual(value["games"][0]["state"], "running")

    def test_games_offline_fallback_includes_interrupted_runs(self):
        self.upstream.portless_offline = True
        self.write_manifest()
        self.write_replay_rows(turns=(1, 44), torn_tail=True)
        value = json.loads(self.request("/v1/games")[2])
        self.assertEqual(len(value["games"]), 1)
        row = value["games"][0]
        self.assertEqual(row["state"], "interrupted")
        self.assertEqual(row["current_turn"], 44)

    def test_replay_offline_serves_interrupted_run_from_disk(self):
        self.upstream.portless_offline = True
        self.write_manifest()
        self.write_replay_rows(turns=(1, 44))
        status, _, body = self.request(f"/v1/games/{GAME_ID}/replay.json")
        self.assertEqual(status, 200)
        self.assertFalse(json.loads(body)["complete"])
        self.assertEqual(len(self.loader_calls), 1)
        self.assertFalse(self.loader_calls[0][6])

    def events_gateway(self, loader, *, offline=False):
        """A gateway wired to an injected events loader, torn down for me."""
        if offline:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
                probe.bind(("127.0.0.1", 0))
                service_url = f"http://127.0.0.1:{probe.getsockname()[1]}"
        else:
            service_url = self.upstream_url
        gateway = make_replay_gateway_server(
            "127.0.0.1",
            0,
            service_url,
            self.runs_root,
            self.cache_root / "events",
            repo_root=self.root,
            upstream_timeout_s=0.2,
            events_loader=loader,
        )
        worker = threading.Thread(target=gateway.serve_forever, daemon=True)
        worker.start()
        self.addCleanup(worker.join, 3)
        self.addCleanup(gateway.server_close)
        self.addCleanup(gateway.shutdown)
        base = f"http://127.0.0.1:{gateway.server_address[1]}"

        def request(path):
            try:
                with urllib.request.urlopen(base + path, timeout=5) as response:
                    return response.status, response.read()
            except urllib.error.HTTPError as error:
                with error:
                    return error.code, error.read()

        return request

    def test_events_upstream_404_falls_back_to_the_disk_derivation(self):
        calls = []

        def loader(runs_root, game_id, resolved_places, *, cache_root, complete):
            calls.append((runs_root, game_id, resolved_places, cache_root, complete))
            return {
                "schema_version": 1,
                "game_id": game_id,
                "available": True,
                "events": [
                    {
                        "turn": 12,
                        "kind": "city_captured",
                        "summary": "pi-gpt-5.5 captured London from Spanish (CPU)",
                        "actors": ["place-1", "place-2"],
                        "weight": 66,
                        "data": {"cities": ["London"]},
                        "token": "must-not-leak",
                    },
                    {"turn": None, "kind": "broken", "summary": "x", "weight": 10},
                    {
                        "turn": 4, "kind": "unweighted", "summary": "no weight",
                        "actors": [], "data": {},
                    },
                ],
                "event_counts": {"city_captured": 1},
                "total_events": 1,
                "truncated": False,
                "omitted_counts": {},
                "last_turn": 12,
                "complete": complete,
                "event_warnings": [{"turn": 3, "message": "a save was skipped"}],
                "token": "must-not-leak",
            }

        self.write_manifest(state="completed")
        request = self.events_gateway(loader)
        status, body = request(f"/v1/games/{GAME_ID}/events.json")
        self.assertEqual(status, 200)
        self.assertNotIn(b"must-not-leak", body)
        value = json.loads(body)
        self.assertEqual(len(value["events"]), 1)
        self.assertEqual(value["events"][0]["turn"], 12)
        self.assertEqual(value["events"][0]["weight"], 66)
        self.assertEqual(value["min_included_weight"], 66)
        self.assertEqual(value["events"][0]["actors"], ["place-1", "place-2"])
        self.assertNotIn("token", value["events"][0])
        self.assertEqual(value["event_counts"], {"city_captured": 1})
        self.assertEqual(value["last_turn"], 12)
        self.assertTrue(value["complete"])
        self.assertEqual(value["event_warnings"][0]["turn"], 3)
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0][1], GAME_ID)
        self.assertEqual(
            [row["seat_id"] for row in calls[0][2]], ["place-1", "place-2"],
        )

    def test_events_offline_serves_interrupted_run_and_rejects_queries(self):
        calls = []

        def loader(runs_root, game_id, resolved_places, *, cache_root, complete):
            calls.append(complete)
            return {
                "schema_version": 1,
                "game_id": game_id,
                "available": True,
                "events": [],
                "event_counts": {},
                "total_events": 0,
                "truncated": True,
                "omitted_counts": {"city_founded": 4},
                "min_included_weight": 0,
                "last_turn": 44,
                "complete": complete,
                "event_warnings": [],
            }

        self.write_manifest()
        self.write_replay_rows(turns=(1, 44))
        request = self.events_gateway(loader, offline=True)
        status, body = request(f"/v1/games/{GAME_ID}/events.json")
        self.assertEqual(status, 200)
        value = json.loads(body)
        self.assertFalse(value["complete"])
        self.assertTrue(value["truncated"])
        self.assertEqual(value["omitted_counts"], {"city_founded": 4})
        self.assertEqual(calls, [False])
        self.assertEqual(
            request(f"/v1/games/{GAME_ID}/events.json?turn=1")[0], 400,
        )
        self.assertEqual(calls, [False])

    def test_events_loader_failures_stay_public(self):
        failure = {"value": FileNotFoundError("private path")}

        def loader(runs_root, game_id, resolved_places, *, cache_root, complete):
            raise failure["value"]

        self.write_manifest(state="completed")
        request = self.events_gateway(loader)
        self.assertEqual(request(f"/v1/games/{GAME_ID}/events.json")[0], 404)
        failure["value"] = ValueError("private corrupt details")
        status, body = request(f"/v1/games/{GAME_ID}/events.json")
        self.assertEqual(status, 503)
        self.assertNotIn(b"private", body)

    def test_games_500_never_masks_failure_with_disk_index(self):
        self.upstream.games_status = 500
        self.write_manifest()
        status, _, body = self.request("/v1/games")
        self.assertEqual(status, 500)
        self.assertEqual(json.loads(body)["error"], "upstream returned HTTP 500")

    def test_exact_portless_offline_502_uses_safe_archive_fallback(self):
        self.upstream.portless_offline = True
        status, _, body = self.request("/v1/games")
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body)["games"], [])

        self.upstream.portless_header = "0"
        status, _, _ = self.request("/v1/games")
        self.assertEqual(status, 502)

        self.upstream.portless_header = "1"
        self.upstream.portless_content_type = "application/json"
        status, _, _ = self.request("/v1/games")
        self.assertEqual(status, 502)

    def test_oversized_upstream_json_is_rejected_before_buffering_all_of_it(self):
        self.upstream.games_body = b"x" * (MAX_PROXY_JSON_BYTES + 1)
        status, _, body = self.request("/v1/games")
        self.assertEqual(status, 502)
        self.assertIn("too large", json.loads(body)["error"])

    def test_bad_ids_traversal_and_queries_are_not_proxied(self):
        initial = len(self.upstream.requests)
        paths = (
            "/v1/games/short/status",
            f"/v1/games/{GAME_ID}/frames/-1.png",
            f"/v1/games/{GAME_ID}/frames/0.png/extra",
            f"/v1/games/{GAME_ID}%2Fstatus",
            f"/v1/games/{GAME_ID}/status?token=private",
        )
        for path in paths:
            with self.subTest(path=path):
                self.assertIn(self.request(path)[0], {400, 404})
        self.assertEqual(len(self.upstream.requests), initial)

    def test_terminal_archive_is_fully_viewable_with_upstream_offline(self):
        run = self.write_terminal_archive()
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            probe.bind(("127.0.0.1", 0))
            offline_port = probe.getsockname()[1]
        board_calls = []
        board_failure = {"value": None}

        def board_loader(
            runs_root, game_id, resolved_places, *, turn, cache_root,
        ):
            if board_failure["value"] is not None:
                raise board_failure["value"]
            board_calls.append((
                runs_root, game_id, resolved_places, turn, cache_root,
            ))
            return {
                "schema_version": 1,
                "game_id": game_id,
                "turn": turn,
                "width": 2,
                "height": 1,
                "topology": "iso",
                "wrap": {"x": True, "y": False},
                "terrain_catalog": [{"id": 0, "name": "Ocean"}],
                "terrain_rows": [[0, 0]],
                "altitude_rows": [[0, 1]],
                "owner_rows": [[-1, 0]],
                "extras_catalog": [],
                "extra_layers": [],
                "cities": [],
                "players": [],
            }

        gateway = make_replay_gateway_server(
            "127.0.0.1",
            0,
            f"http://127.0.0.1:{offline_port}",
            self.runs_root,
            self.cache_root / "offline",
            repo_root=self.root,
            upstream_timeout_s=0.2,
            viewer_public_url="https://freeciv.localhost",
            board_loader=board_loader,
        )
        worker = threading.Thread(target=gateway.serve_forever, daemon=True)
        worker.start()
        base = f"http://127.0.0.1:{gateway.server_address[1]}"

        def offline_request(path):
            request = urllib.request.Request(base + path)
            try:
                with urllib.request.urlopen(request, timeout=5) as response:
                    return response.status, response.headers, response.read()
            except urllib.error.HTTPError as error:
                with error:
                    return error.code, error.headers, error.read()

        try:
            games = json.loads(offline_request("/v1/games")[2])
            self.assertEqual([row["game_id"] for row in games["games"]], [GAME_ID])
            self.assertEqual(games["games"][0]["leaderboard"][0]["score"], 22)
            self.assertEqual(games["games"][0]["outcome"]["status"], "invalid")
            self.assertIn("No valid winner", games["games"][0]["outcome"]["summary"])

            status = json.loads(offline_request(
                f"/v1/games/{GAME_ID}/status",
            )[2])
            self.assertEqual(status["state"], "invalid")
            self.assertFalse(status["benchmark_valid"])
            self.assertEqual(status["leaderboard"][0]["controller_label"], "pi-gpt-5.5")
            self.assertEqual(status["outcome"]["leaders"], ["pi-gpt-5.5"])
            self.assertEqual(
                status["watch_url"],
                f"https://freeciv.localhost/watch/{GAME_ID}",
            )
            self.assertTrue(
                status["replay_url"].startswith("https://freeciv.localhost/"),
            )

            watch_body = offline_request(
                f"/v1/games/{GAME_ID}/watch.json",
            )[2]
            watch = json.loads(watch_body)
            self.assertEqual(watch["frames"][0]["index"], 0)
            self.assertEqual(watch["frames"][0]["turn"], 1)
            self.assertEqual(watch["timeline"], [{"turn": 1}])
            self.assertEqual(watch["frames"][0]["map_players"][0]["player_name"], "AgentPlace1")

            replay = json.loads(offline_request(
                f"/v1/games/{GAME_ID}/replay.json?after_turn=0&limit=1",
            )[2])
            self.assertTrue(replay["available"])
            self.assertEqual(replay["snapshots"][0]["turn"], 1)

            board = json.loads(offline_request(
                f"/v1/games/{GAME_ID}/board.json?turn=1",
            )[2])
            self.assertEqual(board["terrain_rows"], [[0, 0]])
            self.assertEqual(board_calls[0][3], 1)
            board_failure["value"] = FileNotFoundError("private path")
            self.assertEqual(offline_request(
                f"/v1/games/{GAME_ID}/board.json?turn=99",
            )[0], 404)
            board_failure["value"] = ValueError("private corrupt details")
            code, _, corrupt_body = offline_request(
                f"/v1/games/{GAME_ID}/board.json?turn=98",
            )
            self.assertEqual(code, 503)
            self.assertNotIn(b"private", corrupt_body)
            board_failure["value"] = None

            for path, expected in (
                (f"/v1/games/{GAME_ID}/frames/0.png", b"\x89PNG\r\n\x1a\narchive"),
                (f"/v1/games/{GAME_ID}/frames/latest.png", b"\x89PNG\r\n\x1a\narchive"),
                (f"/v1/games/{GAME_ID}/video.mp4", b"archive-video"),
            ):
                with self.subTest(path=path):
                    code, _, body = offline_request(path)
                    self.assertEqual(code, 200)
                    self.assertEqual(body, expected)

            frames = json.loads(offline_request(
                f"/v1/games/{GAME_ID}/frames",
            )[2])
            self.assertEqual(frames["frames"][0]["source_name"], "turn-0001-M-test.map.ppm")

            result_body = offline_request(
                f"/v1/games/{GAME_ID}/result",
            )[2]
            result = json.loads(result_body)
            self.assertEqual(result["artifact_id"], GAME_ID)
            self.assertEqual(result["outcome"]["status"], "invalid")
            self.assertNotIn("episode", result)
            public_text = watch_body + result_body + json.dumps(games).encode()
            self.assertNotIn(b"must-not-leak", public_text)
            self.assertNotIn(b"decisions", public_text)
            self.assertNotIn(str(run).encode(), public_text)

            legacy_manifest = json.loads(
                (run / "manifest.json").read_text(encoding="utf-8"),
            )
            legacy_manifest.pop("state")
            legacy_manifest["status"] = "completed"
            legacy_manifest["benchmark_valid"] = False
            (run / "manifest.json").write_text(
                json.dumps(legacy_manifest), encoding="utf-8",
            )
            legacy_status = json.loads(offline_request(
                f"/v1/games/{GAME_ID}/status",
            )[2])
            self.assertEqual(legacy_status["state"], "completed")
            self.assertFalse(legacy_status["benchmark_valid"])
            self.assertEqual(legacy_status["outcome"]["status"], "invalid")
            self.assertIn("No valid winner", legacy_status["outcome"]["summary"])

            (run / "watch_frames" / "000000.png").unlink()
            os.symlink(
                run / "auth.json",
                run / "watch_frames" / "000000.png",
            )
            self.assertEqual(offline_request(
                f"/v1/games/{GAME_ID}/frames/0.png",
            )[0], 404)
        finally:
            gateway.shutdown()
            gateway.server_close()
            worker.join(3)

    def test_offline_live_run_is_not_exposed_as_terminal_archive(self):
        self.write_manifest(state="running")
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            probe.bind(("127.0.0.1", 0))
            offline_port = probe.getsockname()[1]
        gateway = make_replay_gateway_server(
            "127.0.0.1", 0,
            f"http://127.0.0.1:{offline_port}",
            self.runs_root, self.cache_root / "offline-live",
            repo_root=self.root, upstream_timeout_s=0.2,
        )
        worker = threading.Thread(target=gateway.serve_forever, daemon=True)
        worker.start()
        base = f"http://127.0.0.1:{gateway.server_address[1]}"
        try:
            with urllib.request.urlopen(base + "/v1/games", timeout=3) as response:
                self.assertEqual(json.load(response)["games"], [])
            with self.assertRaises(urllib.error.HTTPError) as context:
                urllib.request.urlopen(
                    base + f"/v1/games/{GAME_ID}/status", timeout=3,
                )
            with context.exception:
                self.assertEqual(context.exception.code, 404)
                context.exception.read()
        finally:
            gateway.shutdown()
            gateway.server_close()
            worker.join(3)

    def test_ready_file_is_private_and_removed_on_clean_shutdown(self):
        ready_file = self.root / "state" / "gateway.json"
        stop = threading.Event()
        observed = []
        worker = threading.Thread(
            target=run_replay_gateway,
            args=(
                "127.0.0.1",
                0,
                self.upstream_url,
                self.runs_root,
                self.cache_root / "second",
            ),
            kwargs={
                "repo_root": self.root,
                "ready_file": ready_file,
                "replay_loader": self.loader,
                "stop_event": stop,
                "on_ready": observed.append,
            },
            daemon=True,
        )
        worker.start()
        for _ in range(100):
            if ready_file.exists() and observed:
                break
            time.sleep(0.01)
        self.assertTrue(ready_file.exists())
        self.assertEqual(stat.S_IMODE(ready_file.stat().st_mode), 0o600)
        value = json.loads(ready_file.read_text())
        self.assertEqual(value["identity"], observed[0]["identity"])
        self.assertEqual(value["pid"], os.getpid())
        stop.set()
        # Wake handle_request immediately instead of waiting for its timeout.
        urllib.request.urlopen(observed[0]["url"] + "/health", timeout=2).close()
        worker.join(3)
        self.assertFalse(worker.is_alive())
        self.assertFalse(ready_file.exists())

    def test_ready_lock_prevents_replacement_cleanup_race(self):
        ready_file = self.root / "state" / "exclusive-gateway.json"
        stop = threading.Event()
        observed = []
        worker = threading.Thread(
            target=run_replay_gateway,
            args=(
                "127.0.0.1", 0, self.upstream_url,
                self.runs_root, self.cache_root / "owner",
            ),
            kwargs={
                "repo_root": self.root,
                "ready_file": ready_file,
                "replay_loader": self.loader,
                "stop_event": stop,
                "on_ready": observed.append,
            },
            daemon=True,
        )
        worker.start()
        for _ in range(100):
            if ready_file.exists() and observed:
                break
            time.sleep(0.01)
        original = ready_file.read_bytes()
        with self.assertRaises(BlockingIOError):
            run_replay_gateway(
                "127.0.0.1",
                0,
                self.upstream_url,
                self.runs_root,
                self.cache_root / "replacement",
                repo_root=self.root,
                ready_file=ready_file,
                replay_loader=self.loader,
                stop_event=threading.Event(),
            )
        self.assertEqual(ready_file.read_bytes(), original)
        stop.set()
        urllib.request.urlopen(observed[0]["url"] + "/health", timeout=2).close()
        worker.join(3)
        self.assertFalse(ready_file.exists())

    def test_loopback_and_service_url_validation(self):
        with self.assertRaisesRegex(ValueError, "loopback"):
            make_replay_gateway_server(
                "0.0.0.0", 0, self.upstream_url,
                self.runs_root, self.cache_root,
            )
        with self.assertRaisesRegex(ValueError, "loopback"):
            make_replay_gateway_server(
                "localhost", 0, self.upstream_url,
                self.runs_root, self.cache_root,
            )
        for bad in (
            "file:///tmp/service",
            "http://user:pass@127.0.0.1:8765",
            "http://127.0.0.1:8765/path?token=private",
            "http://127.0.0.1:8765/../private",
        ):
            with self.subTest(service_url=bad):
                with self.assertRaises(ValueError):
                    gateway_config(bad, self.runs_root, self.cache_root)


if __name__ == "__main__":
    unittest.main()


class ArchiveVictoryTests(unittest.TestCase):
    """The engine records why the game ended; the outcome must surface it."""

    def _leaderboard(self):
        return [
            {"controller_label": "winner", "score": 1856, "rank": 1,
             "score_turn": 753},
            {"controller_label": "loser", "score": 760, "rank": 2,
             "score_turn": 753},
        ]

    def _write(self, root, payload):
        (root / "victory.json").write_text(
            json.dumps(payload), encoding="utf-8",
        )

    def test_victory_condition_is_named_in_the_summary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self._write(root, {
                "schema_version": 1, "victory": "spacerace",
                "winners": ["AgentPlace1"], "turn": 753, "year": 2326,
            })
            outcome = _archive_outcome("completed", self._leaderboard(), root)
        self.assertEqual(outcome["victory"]["code"], "spacerace")
        self.assertEqual(outcome["victory"]["label"], "spaceship victory")
        self.assertEqual(outcome["victory"]["winners"], ["AgentPlace1"])
        self.assertEqual(outcome["victory"]["turn"], 753)
        # The score margin alone never explained why play stopped.
        self.assertEqual(
            outcome["summary"], "winner won by 1096 (spaceship victory)",
        )

    def test_turn_limit_is_reported_as_a_score_victory(self):
        # Reaching the turn limit fires no in-game victory condition, so the
        # match is decided on final civilization score.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self._write(root, {
                "schema_version": 1, "victory": "turn_limit",
                "winners": [], "turn": 5000, "year": 3000,
            })
            outcome = _archive_outcome("completed", self._leaderboard(), root)
        self.assertEqual(outcome["victory"]["code"], "turn_limit")
        self.assertEqual(outcome["victory"]["label"], "score victory")
        self.assertEqual(outcome["victory"]["winners"], [])
        self.assertEqual(
            outcome["summary"], "winner won by 1096 (score victory)",
        )

    def test_unknown_code_is_passed_through_without_inventing_a_label(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self._write(root, {"victory": "brand_new", "winners": []})
            outcome = _archive_outcome("completed", self._leaderboard(), root)
        self.assertEqual(outcome["victory"]["label"], "brand_new")

    def test_missing_or_malformed_record_leaves_the_summary_untouched(self):
        for payload in (None, "", "{not json", json.dumps({"victory": ""}),
                        json.dumps(["not", "a", "dict"])):
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                if payload is not None:
                    (root / "victory.json").write_text(
                        payload, encoding="utf-8",
                    )
                outcome = _archive_outcome(
                    "completed", self._leaderboard(), root,
                )
            self.assertIsNone(outcome["victory"], payload)
            self.assertEqual(outcome["summary"], "winner won by 1096")

    def test_invalid_runs_are_not_annotated_with_a_victory_claim(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self._write(root, {"victory": "conquest", "winners": ["a"]})
            outcome = _archive_outcome("invalid", self._leaderboard(), root)
        # Still reported for diagnostics, but never phrased as a won match.
        self.assertEqual(outcome["victory"]["code"], "conquest")
        self.assertNotIn("conquest victory", outcome["summary"])
