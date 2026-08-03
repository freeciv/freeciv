import json
import os
import socket
import tempfile
import threading
import time
import unittest
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from agent_eval.actions import deterministic_action
from agent_eval.client import (
    ClientError,
    create_game,
    join_game,
    next_turn,
    request_json,
    submit_action,
)
from agent_eval.supervisor import Supervisor, make_supervisor_server


TERMINAL = {"completed", "invalid", "failed", "cancelled"}


@unittest.skipUnless(
    os.environ.get("FREECIV_AGENT_E2E") == "1",
    "set FREECIV_AGENT_E2E=1 for session supervisor acceptance",
)
class SupervisorE2ETests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.supervisor = Supervisor(self.directory.name, "admin-e2e")
        self.server = make_supervisor_server(
            self.supervisor, "127.0.0.1", 0,
        )
        self.thread = threading.Thread(
            target=self.server.serve_forever, daemon=True,
        )
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(5)
        self.supervisor.close()
        self.directory.cleanup()

    def create(self, mode="single", action_timeout_s=5, turns=2):
        return create_game(
            self.supervisor.service_url,
            "admin-e2e",
            {
                "mode": mode,
                "places": 2,
                "turns": turns,
                "seed": 909,
                "ruleset": "classic",
                "objective": "Maximize civilization score.",
                "action_timeout_s": action_timeout_s,
                "lobby_timeout_s": 30,
                "frame_interval": 1,
                "frame_zoom": 1,
            },
        )

    def wait_terminal(self, game, timeout=30):
        deadline = time.monotonic() + timeout
        while game.state not in TERMINAL and time.monotonic() < deadline:
            time.sleep(0.025)
        self.assertIn(game.state, TERMINAL)

    def play(self, session):
        after_turn = 0
        turns = []
        while True:
            current = next_turn(session, after_turn, 10)
            if "observation" in current:
                submit_action(
                    session,
                    current["turn"],
                    current["observation_id"],
                    deterministic_action(current["observation"]),
                    {"harness": "e2e"},
                )
                after_turn = current["turn"]
                turns.append(after_turn)
                continue
            if current.get("state") in TERMINAL:
                return turns

    def run_partial_bridge_failure(
        self, second_response=None, *, forward_second=False,
    ):
        target_url = self.supervisor.internal_service_url
        request_count = 0
        request_lock = threading.Lock()

        class PartialFailureHandler(BaseHTTPRequestHandler):
            def log_message(self, format, *args):
                return

            def do_POST(handler):
                nonlocal request_count
                length = int(handler.headers.get("Content-Length", "0"))
                body = handler.rfile.read(length)
                with request_lock:
                    request_count += 1
                    current_request = request_count
                if current_request == 1 or forward_second:
                    forwarded = urllib.request.Request(
                        target_url + handler.path,
                        data=body,
                        headers={
                            "Authorization": handler.headers["Authorization"],
                            "Content-Type": "application/json",
                        },
                    )
                    with urllib.request.urlopen(
                        forwarded, timeout=20,
                    ) as response:
                        response_body = response.read()
                        if current_request > 1 and second_response is not None:
                            response_body = second_response
                        handler.send_response(response.status)
                        handler.send_header(
                            "Content-Type", "application/json",
                        )
                        handler.send_header(
                            "Content-Length", str(len(response_body)),
                        )
                        handler.end_headers()
                        handler.wfile.write(response_body)
                    return
                if second_response is None:
                    try:
                        handler.connection.shutdown(socket.SHUT_RDWR)
                    except OSError:
                        pass
                    handler.connection.close()
                    return
                handler.send_response(200)
                handler.send_header("Content-Type", "application/json")
                handler.send_header(
                    "Content-Length", str(len(second_response)),
                )
                handler.end_headers()
                handler.wfile.write(second_response)

        proxy = ThreadingHTTPServer(
            ("127.0.0.1", 0), PartialFailureHandler,
        )
        proxy_thread = threading.Thread(
            target=proxy.serve_forever, daemon=True,
        )
        proxy_thread.start()
        original_internal_url = self.supervisor.internal_service_url
        host, port = proxy.server_address
        self.supervisor.internal_service_url = f"http://{host}:{port}"
        try:
            created = self.create(turns=2)
        finally:
            self.supervisor.internal_service_url = original_internal_url
        game = self.supervisor.game(created["game_id"])
        try:
            joined = join_game(
                self.supervisor.service_url, game.game_id,
                created["join_token"], controller_label="partial-model",
            )
            session = {
                "service_url": self.supervisor.service_url,
                "game_id": game.game_id,
                "agent_token": joined["agent_token"],
            }
            first = next_turn(session, 0, 10)
            self.assertEqual(first["turn"], 1)
            submit_action(
                session, 1, first["observation_id"],
                deterministic_action(first["observation"]),
                {"harness": "partial-bridge-failure"},
            )
            if forward_second:
                second = next_turn(session, 1, 10)
                self.assertEqual(second["turn"], 2)
                submit_action(
                    session, 2, second["observation_id"],
                    deterministic_action(second["observation"]),
                    {"harness": "replaced-second-response"},
                )
            self.wait_terminal(game)
        finally:
            proxy.shutdown()
            proxy.server_close()
            proxy_thread.join(5)
        self.assertEqual(
            [item["turn"] for item in game.timeline],
            [1, 2] if forward_second else [1],
        )
        self.assertIn(game.state, {"invalid", "failed"})
        self.assertFalse(game.status()["benchmark_valid"])
        journal = [
            json.loads(line)
            for line in game.bridge_status_path.read_text().splitlines()
        ]
        self.assertEqual(
            [(item["event"], item["turn"]) for item in journal],
            [("begin", 1), ("ok", 1), ("begin", 2), ("error", 2)],
        )
        if game.state == "invalid":
            self.assertTrue(any(
                reason.startswith("bridge_callback_error:turn=2:")
                for reason in game.invalid_reasons
            ))
        return game

    def test_single_lobby_join_play_watch_and_artifacts(self):
        created = self.create()
        game = self.supervisor.game(created["game_id"])
        self.assertEqual(game.state, "lobby")
        self.assertIsNone(game.process.poll())
        self.assertNotIn(
            "start", (game.episode / "server.commands").read_text().splitlines(),
        )
        joined = join_game(
            self.supervisor.service_url,
            game.game_id,
            created["join_token"],
            controller_label="codex-e2e-model",
            metadata={"client": "codex", "model": "e2e-model"},
        )
        self.assertEqual(game.start_count, 1)
        self.assertEqual(
            (game.episode / "server.commands").read_text().splitlines().count("start"),
            1,
        )
        session = {
            "service_url": self.supervisor.service_url,
            "game_id": game.game_id,
            "agent_token": joined["agent_token"],
        }
        first = next_turn(session, 0, 10)
        self.assertEqual(first["turn"], 1)
        submit_action(
            session, 1, first["observation_id"],
            deterministic_action(first["observation"]),
            {"harness": "e2e"},
        )
        second = next_turn(session, 1, 10)
        self.assertEqual(second["turn"], 2)
        deadline = time.monotonic() + 5
        while not game._ppm_frames() and time.monotonic() < deadline:
            time.sleep(0.025)
        self.assertTrue(game._ppm_frames())
        self.assertEqual(game.state, "running")
        running_watch = request_json("GET", created["watch_json_url"])
        self.assertEqual(running_watch["game"]["state"], "running")
        self.assertTrue(running_watch["frames"])
        self.assertEqual(
            running_watch["game"]["resolved_places"][0][
                "controller_label"
            ],
            "codex-e2e-model",
        )
        replay = request_json(
            "GET", created["replay_url"] + "?after_turn=0&limit=250",
        )
        self.assertTrue(replay["available"])
        self.assertEqual(
            [snapshot["turn"] for snapshot in replay["snapshots"]],
            [1, 2],
        )
        self.assertEqual(replay["replay_warnings"], [])
        catalog = replay["catalog"]["technologies"]
        self.assertEqual(len(catalog), 87)
        catalog_ids = {technology["id"] for technology in catalog}
        self.assertEqual(len(catalog_ids), 87)
        self.assertTrue(all(
            set(technology["requires"]) <= catalog_ids
            for technology in catalog
        ))
        self.assertIn(
            "Railroad",
            {technology["rule_name"] for technology in catalog},
        )
        self.assertNotIn("?tech:", json.dumps(catalog))
        for snapshot in replay["snapshots"]:
            configured = {
                player["player_name"]: player for player in snapshot["players"]
                if player["scored"]
            }
            self.assertEqual(
                set(configured), {"AgentPlace1", "NativePlace2"},
            )
            self.assertEqual(
                configured["AgentPlace1"]["controller_label"],
                "codex-e2e-model",
            )
            self.assertEqual(
                configured["AgentPlace1"]["player_color"], "#0067A5",
            )
            self.assertEqual(
                configured["NativePlace2"]["controller_label"],
                "Freeciv Classic AI",
            )
            self.assertEqual(
                configured["NativePlace2"]["player_color"], "#F38400",
            )
            for player in configured.values():
                self.assertIsInstance(player["alive"], bool)
                for key in (
                    "score", "cities", "citizens", "units", "gold",
                    "culture", "future_techs",
                ):
                    self.assertIsInstance(player[key], int)
                self.assertIsInstance(player["nation"], str)
                self.assertIsInstance(player["government"], str)
                self.assertIsInstance(player["known_tech_ids"], list)
                self.assertIsInstance(player["gained_tech_ids"], list)
                self.assertIsInstance(player["lost_tech_ids"], list)
                self.assertIsInstance(player["research"]["bulbs"], int)
                self.assertIsInstance(player["research"]["cost"], int)
        frames_with_turn = [
            frame for frame in running_watch["frames"]
            if frame["turn"] is not None
        ]
        self.assertTrue(frames_with_turn)
        latest_map_players = {
            player["player_name"]: player
            for player in frames_with_turn[-1]["map_players"]
        }
        self.assertEqual(
            latest_map_players["AgentPlace1"]["player_color"], "#0067A5",
        )
        self.assertEqual(
            latest_map_players["NativePlace2"]["player_color"], "#F38400",
        )
        with urllib.request.urlopen(created["video_url"]) as response:
            self.assertEqual(response.headers.get_content_type(), "video/mp4")
            self.assertGreater(len(response.read()), 100)
        running_video_mtime = (game.episode / "game.mp4").stat().st_mtime_ns
        with urllib.request.urlopen(created["video_url"]) as response:
            self.assertGreater(len(response.read()), 100)
        self.assertEqual(
            (game.episode / "game.mp4").stat().st_mtime_ns,
            running_video_mtime,
        )
        submit_action(
            session, 2, second["observation_id"],
            deterministic_action(second["observation"]),
            {"harness": "e2e"},
        )
        self.wait_terminal(game)
        self.assertEqual(game.state, "completed")
        self.assertTrue(game.status()["benchmark_valid"])
        self.assertGreater(len(game._save_files()), 0)
        self.assertGreater(len(game._ppm_frames()), 0)
        self.assertTrue((game.episode / "report.json").is_file())
        self.assertTrue((game.episode / "game.mp4").is_file())
        report = request_json("GET", created["result_url"])
        self.assertNotIn("episode", report)
        self.assertNotIn(str(game.episode), json.dumps(report))
        self.assertEqual(report["artifact_id"], game.game_id)
        self.assertEqual(
            {row["seat_id"] for row in report["score"]["players"]},
            {"place-1", "place-2"},
        )
        with urllib.request.urlopen(created["watch_url"]) as response:
            watch_html = response.read().decode()
        watch = request_json("GET", created["watch_json_url"])
        replay = request_json(
            "GET", created["replay_url"] + "?after_turn=0&limit=250",
        )
        public_text = watch_html + json.dumps(watch) + json.dumps(replay)
        for secret in (
            created["owner_token"], created["join_token"], joined["agent_token"],
        ):
            self.assertNotIn(secret, public_text)
        self.assertNotIn('"observation"', public_text)
        self.assertNotIn('"action"', public_text)
        self.assertIn("Omniscient Freeciv agent match replay", public_text)
        self.assertIn("Freeciv Agent Arena", watch_html)
        self.assertIn('<div id="root"></div>', watch_html)
        self.assertNotIn(game.game_id, watch_html)
        with urllib.request.urlopen(
            f"{self.supervisor.service_url}/v1/games/{game.game_id}/frames/latest.png"
        ) as response:
            self.assertEqual(response.headers.get_content_type(), "image/png")
            self.assertTrue(response.read().startswith(b"\x89PNG"))
        with urllib.request.urlopen(created["video_url"]) as response:
            self.assertEqual(response.headers.get_content_type(), "video/mp4")
            self.assertGreater(len(response.read()), 100)
        self.assertGreaterEqual(
            (game.episode / "game.mp4").stat().st_mtime_ns,
            running_video_mtime,
        )

    def test_multiplayer_agents_observe_same_turn_before_collective_actions(self):
        created = self.create(mode="multiplayer")
        game = self.supervisor.game(created["game_id"])
        joins = {}

        def join(place):
            joins[place] = join_game(
                self.supervisor.service_url,
                game.game_id,
                created["join_token"],
                place,
                "codex-e2e-a" if place == 1 else "claude-code-e2e-b",
                {
                    "client": "codex" if place == 1 else "claude-code",
                    "model": "e2e-a" if place == 1 else "e2e-b",
                },
            )

        workers = [threading.Thread(target=join, args=(place,)) for place in (1, 2)]
        for worker in workers:
            worker.start()
        for worker in workers:
            worker.join(5)
        self.assertEqual(set(joins), {1, 2})
        self.assertEqual(game.start_count, 1)
        with self.assertRaises(ClientError) as context:
            join_game(
                self.supervisor.service_url, game.game_id, created["join_token"],
                controller_label="extra-model",
            )
        self.assertEqual(context.exception.status, 409)
        sessions = {
            place: {
                "service_url": self.supervisor.service_url,
                "game_id": game.game_id,
                "agent_token": joined["agent_token"],
            }
            for place, joined in joins.items()
        }
        seen = {1: [], 2: []}
        for turn in (1, 2):
            observations = {}

            def get_next(place):
                observations[place] = next_turn(
                    sessions[place], turn - 1, 10,
                )

            polls = [
                threading.Thread(target=get_next, args=(place,))
                for place in (1, 2)
            ]
            for poll in polls:
                poll.start()
            for poll in polls:
                poll.join(15)
            self.assertEqual(
                {value["turn"] for value in observations.values()}, {turn},
            )
            self.assertIsNotNone(game.current_turn)
            for place in (1, 2):
                seen[place].append(observations[place]["turn"])
            submits = []
            for place in (1, 2):
                current = observations[place]
                submits.append(threading.Thread(
                    target=submit_action,
                    args=(
                        sessions[place], turn, current["observation_id"],
                        deterministic_action(current["observation"]),
                        {"harness": f"agent-{place}"},
                    ),
                ))
            for submit in submits:
                submit.start()
            for submit in submits:
                submit.join(5)
        self.wait_terminal(game)
        self.assertEqual(game.state, "completed")
        self.assertEqual(seen, {1: [1, 2], 2: [1, 2]})
        self.assertTrue(all(not item["timed_out_seats"] for item in game.timeline))

    def test_timeout_is_invalid_hold_not_deterministic_fallback(self):
        created = self.create(action_timeout_s=0.25, turns=1)
        game = self.supervisor.game(created["game_id"])
        join_game(
            self.supervisor.service_url, game.game_id, created["join_token"],
            controller_label="timeout-e2e-model",
        )
        self.wait_terminal(game)
        self.assertEqual(game.state, "invalid")
        self.assertFalse(game.status()["benchmark_valid"])
        events = [
            json.loads(line)
            for line in (game.episode / "decisions.jsonl").read_text().splitlines()
            if json.loads(line).get("event") == "decision"
        ]
        self.assertEqual(len(events), 1)
        self.assertIsNone(events[0]["action"])
        self.assertFalse(events[0]["fallback"])
        self.assertEqual(events[0]["source"], "external_timeout")
        self.assertNotIn("deterministic_fallback", json.dumps(events))

    def test_unreachable_bridge_transport_never_completes_valid(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            probe.bind(("127.0.0.1", 0))
            unused_port = probe.getsockname()[1]
        original_internal_url = self.supervisor.internal_service_url
        self.supervisor.internal_service_url = (
            f"http://127.0.0.1:{unused_port}"
        )
        try:
            created = self.create(turns=1)
        finally:
            self.supervisor.internal_service_url = original_internal_url
        game = self.supervisor.game(created["game_id"])
        join_game(
            self.supervisor.service_url, game.game_id, created["join_token"],
            controller_label="transport-e2e-model",
        )
        self.wait_terminal(game)
        self.assertIn(game.state, {"invalid", "failed"})
        self.assertFalse(game.status()["benchmark_valid"])
        self.assertEqual(game.timeline, [])
        if game.state == "invalid":
            self.assertIn("bridge_no_turns", game.invalid_reasons)
        logs = "\n".join(
            path.read_text(encoding="utf-8", errors="replace")
            for path in (
                game.episode / "server.stdout.log",
                game.episode / "server.log",
            )
            if path.exists()
        ).lower()
        self.assertTrue(
            "agent_eval bridge" in logs or "curl:" in logs,
            logs[-2000:],
        )

    def test_malformed_bridge_response_never_completes_valid(self):
        class MalformedResponseHandler(BaseHTTPRequestHandler):
            def log_message(self, format, *args):
                return

            def do_POST(self):
                length = int(self.headers.get("Content-Length", "0"))
                self.rfile.read(length)
                body = (
                    b'{"schema_version":1,"turn":1,"actions":['
                    b'{"seat_id":"place-1","traits":{"aggressive":1}}],'
                    b'"timed_out_seats":[],'
                    b'"benchmark_valid":true}'
                )
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        malformed_server = ThreadingHTTPServer(
            ("127.0.0.1", 0), MalformedResponseHandler,
        )
        malformed_thread = threading.Thread(
            target=malformed_server.serve_forever, daemon=True,
        )
        malformed_thread.start()
        original_internal_url = self.supervisor.internal_service_url
        host, port = malformed_server.server_address
        self.supervisor.internal_service_url = f"http://{host}:{port}"
        try:
            created = self.create(turns=1)
            game = self.supervisor.game(created["game_id"])
            join_game(
                self.supervisor.service_url, game.game_id,
                created["join_token"],
                controller_label="malformed-e2e-model",
            )
            self.wait_terminal(game)
        finally:
            self.supervisor.internal_service_url = original_internal_url
            malformed_server.shutdown()
            malformed_server.server_close()
            malformed_thread.join(5)
        self.assertIn(game.state, {"invalid", "failed"})
        self.assertFalse(game.status()["benchmark_valid"])
        self.assertEqual(game.timeline, [])
        logs = "\n".join(
            path.read_text(encoding="utf-8", errors="replace")
            for path in (
                game.episode / "server.stdout.log",
                game.episode / "server.log",
            )
            if path.exists()
        ).lower()
        self.assertIn("action traits is missing key", logs)

    def test_turn_one_success_then_turn_two_transport_failure_is_invalid(self):
        self.run_partial_bridge_failure()

    def test_turn_one_success_then_strict_json_or_schema_failure_is_invalid(self):
        cases = {
            "invalid-json-with-required-substrings": ((
                b'{"schema_version":1,"turn":2,"actions":[],'
                b'"timed_out_seats":[],'
                b'"benchmark_valid":true trailing}'
            ), "invalid json"),
            "duplicate-key": ((
                b'{"schema_version":1,"turn":2,"turn":2,"actions":[],'
                b'"timed_out_seats":[],'
                b'"benchmark_valid":true}'
            ), "duplicate object key"),
            "unknown-key": ((
                b'{"schema_version":1,"turn":2,"actions":[],'
                b'"timed_out_seats":[],'
                b'"benchmark_valid":true,"unexpected":0}'
            ), "unknown key unexpected"),
            "non-finite-number": ((
                b'{"schema_version":1,"turn":2,"actions":['
                b'{"seat_id":"place-1","traits":{"aggressive":1e309,'
                b'"builder":2,"expansionist":3,"trader":4}}],'
                b'"timed_out_seats":[],'
                b'"benchmark_valid":true}'
            ), "number is not finite"),
            "missing-required-key": ((
                b'{"schema_version":1,"turn":2,"actions":['
                b'{"seat_id":"place-1","traits":{"aggressive":1,'
                b'"builder":2,"expansionist":3,"trader":4}}],'
                b'"benchmark_valid":true}'
            ), "missing key timed_out_seats"),
            "timeout-marked-valid": ((
                b'{"schema_version":1,"turn":2,"actions":[],'
                b'"timed_out_seats":["place-1"],'
                b'"benchmark_valid":true}'
            ), "timed out response cannot be benchmark-valid"),
        }
        for label, (response, expected_error) in cases.items():
            with self.subTest(label=label):
                game = self.run_partial_bridge_failure(response)
                journal = [
                    json.loads(line)
                    for line in game.bridge_status_path.read_text().splitlines()
                ]
                self.assertIn(
                    expected_error, journal[-1]["message"].lower(),
                )

    def test_exact_response_contract_rejects_omitted_controlled_seat(self):
        replacement = (
            b'{"schema_version":1,"turn":2,"actions":[],'
            b'"timed_out_seats":[],"benchmark_valid":true}'
        )
        game = self.run_partial_bridge_failure(
            replacement, forward_second=True,
        )
        journal = [
            json.loads(line)
            for line in game.bridge_status_path.read_text().splitlines()
        ]
        self.assertIn(
            "does not cover every controlled seat",
            journal[-1]["message"].lower(),
        )


if __name__ == "__main__":
    unittest.main()
