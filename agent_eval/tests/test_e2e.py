import json
import os
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
from dataclasses import replace
from pathlib import Path

from agent_eval.config import SeatConfig, load_config
from agent_eval.runner import render_episode, run_episode


@unittest.skipUnless(
    os.environ.get("FREECIV_AGENT_E2E") == "1",
    "set FREECIV_AGENT_E2E=1 to run the real Freeciv bridge smoke",
)
class FreecivE2ETests(unittest.TestCase):
    def test_native_only_baseline_skips_bridge_and_remains_valid(self):
        config = load_config("agent_eval/examples/native-vs-deterministic.json")
        config = replace(
            config,
            name="native-only-baseline",
            turns=2,
            seats=(
                SeatConfig("native-a", "NativeOne", "native"),
                SeatConfig("native-b", "NativeTwo", "native"),
            ),
            server={
                **config.server,
                "frame_interval": 1,
                "wall_timeout_s": 90,
            },
        )
        with tempfile.TemporaryDirectory() as directory:
            episode = Path(directory) / "episode"
            summary = run_episode(config, episode, seed=103)
            manifest = summary["manifest"]
            self.assertEqual(manifest["status"], "completed")
            self.assertTrue(manifest["benchmark_valid"])
            self.assertEqual(manifest["fallbacks"], 0)
            self.assertIsNone(manifest["agentd_url"])
            self.assertIsNone(manifest["trace_file"])
            self.assertIsNone(manifest["bridge_status_file"])
            self.assertIsNone(manifest["control_file"])
            self.assertIsNone(manifest["provenance"]["bridge_sha256"])
            commands = (episode / "server.commands").read_text().splitlines()
            self.assertFalse(any(
                command.startswith("lua unsafe-file ") for command in commands
            ))
            self.assertFalse((episode / "bridge-status.jsonl").exists())
            self.assertFalse((episode / "decisions.jsonl").exists())
            self.assertFalse((episode / "control.json").exists())
            self.assertTrue((episode / "report.json").is_file())
            self.assertGreater(manifest["frames"], 0)
            self.assertGreater(manifest["checkpoints"], 0)
            self.assertEqual(summary["score"]["final_turn"], 3)
            self.assertEqual(
                set(summary["seat_stats"]), {"native-a", "native-b"},
            )
            for stats in summary["seat_stats"].values():
                self.assertEqual(stats["turns"], 2)
                self.assertEqual(stats["decisions"], 0)
            video = render_episode(episode, None)
            self.assertEqual(video, (episode / "game.mp4").resolve())
            self.assertGreater(video.stat().st_size, 100)

    def test_two_turn_server_bridge_smoke(self):
        config = load_config("agent_eval/examples/native-vs-deterministic.json")
        config = replace(
            config,
            turns=2,
            server={
                **config.server,
                "frame_interval": 1,
                "wall_timeout_s": 90,
            },
        )
        with tempfile.TemporaryDirectory() as directory:
            episode = Path(directory) / "episode"
            summary = run_episode(config, episode, seed=101)
            self.assertEqual(summary["manifest"]["status"], "completed")
            self.assertTrue(summary["manifest"]["benchmark_valid"])
            self.assertEqual(summary["manifest"]["fallbacks"], 0)
            self.assertGreaterEqual(summary["manifest"]["turn_timeout_s"], 30)
            self.assertGreater(summary["manifest"]["frames"], 0)
            self.assertGreater(summary["manifest"]["checkpoints"], 0)
            self.assertEqual(summary["seat_stats"]["native"]["turns"], 2)
            self.assertEqual(summary["seat_stats"]["native"]["decisions"], 0)
            self.assertEqual(summary["seat_stats"]["deterministic"]["turns"], 2)
            self.assertEqual(summary["seat_stats"]["deterministic"]["decisions"], 2)
            provenance = summary["manifest"]["provenance"]
            self.assertEqual(len(provenance["binary_sha256"]), 64)
            self.assertEqual(len(provenance["bridge_sha256"]), 64)
            self.assertEqual(len(provenance["harness_sha256"]), 64)
            self.assertEqual(len(provenance["resolved_config_sha256"]), 64)
            events = [
                json.loads(line)
                for line in (episode / "decisions.jsonl").read_text().splitlines()
            ]
            self.assertEqual(
                {event["seat_id"] for event in events}, {"deterministic"},
            )
            research = [event["observation"]["research"] for event in events]
            self.assertTrue(all(not value.startswith("Team ") for value in research))
            self.assertTrue(any(value for value in research))
            output = (episode / "server.stdout.log").read_text()
            self.assertIn("'traitdistribution' has been set", output)
            self.assertIn("'ec_turns' has been set to 0", output)
            self.assertIn("'threaded_save' has been set to disabled", output)

    def test_provider_fallback_invalidates_benchmark(self):
        config = load_config("agent_eval/examples/native-vs-deterministic.json")
        model = replace(
            config.seats[1], type="openai_responses", model="never-called",
            api_key_env="FREECIV_AGENT_E2E_MISSING_KEY",
        )
        config = replace(
            config,
            turns=2,
            seats=(config.seats[0], model),
            server={**config.server, "frame_interval": 0, "wall_timeout_s": 90},
        )
        old_key = os.environ.pop("FREECIV_AGENT_E2E_MISSING_KEY", None)
        try:
            with tempfile.TemporaryDirectory() as directory:
                summary = run_episode(
                    config, Path(directory) / "episode", seed=101
                )
                self.assertEqual(summary["manifest"]["status"], "invalid")
                self.assertFalse(summary["manifest"]["benchmark_valid"])
                self.assertEqual(summary["manifest"]["fallbacks"], 2)
                self.assertEqual(summary["manifest"]["frames"], 0)
        finally:
            if old_key is not None:
                os.environ["FREECIV_AGENT_E2E_MISSING_KEY"] = old_key

    def test_managed_external_seat_discovery_and_actions(self):
        token_env = "FREECIV_AGENT_E2E_EXTERNAL_TOKEN"
        token = "external-e2e-secret"
        config = load_config("agent_eval/examples/native-vs-deterministic.json")
        config = replace(
            config,
            turns=2,
            seats=(
                SeatConfig(
                    "external", "SeatOne", "external", token_env=token_env,
                    timeout_s=10,
                ),
                SeatConfig("native", "SeatTwo", "native"),
            ),
            server={
                **config.server,
                "agentd_port": 0,
                "frame_interval": 0,
                "wall_timeout_s": 90,
            },
        )
        old_token = os.environ.get(token_env)
        os.environ[token_env] = token
        try:
            with tempfile.TemporaryDirectory() as directory:
                episode = Path(directory) / "episode"
                result = {}

                def run():
                    try:
                        result["summary"] = run_episode(config, episode, seed=101)
                    except Exception as exc:
                        result["error"] = exc

                worker = threading.Thread(target=run)
                worker.start()
                deadline = time.monotonic() + 30
                manifest = None
                while time.monotonic() < deadline:
                    path = episode / "manifest.json"
                    if path.exists():
                        try:
                            manifest = json.loads(path.read_text())
                        except json.JSONDecodeError:
                            manifest = None
                        if manifest and manifest.get("agentd_url"):
                            break
                    time.sleep(0.025)
                self.assertIsNotNone(manifest)
                agentd_url = manifest["agentd_url"]
                self.assertEqual(
                    manifest["external_seats"],
                    [{"seat_id": "external", "token_env": token_env}],
                )
                control = json.loads((episode / "control.json").read_text())
                self.assertEqual(control["agentd_url"], agentd_url)
                self.assertEqual(
                    control["turn_timeout_s"], manifest["turn_timeout_s"]
                )
                self.assertNotIn(token, (episode / "manifest.json").read_text())
                self.assertNotIn(token, (episode / "control.json").read_text())

                acted: set[int] = set()
                action = {
                    "type": "set_traits",
                    "traits": {
                        "aggressive": 1, "builder": 2,
                        "expansionist": 3, "trader": 4,
                    },
                }
                while worker.is_alive() and time.monotonic() < deadline:
                    request = urllib.request.Request(
                        agentd_url + "/v1/seats/external/observation",
                        headers={"Authorization": f"Bearer {token}"},
                    )
                    try:
                        with urllib.request.urlopen(request, timeout=2) as response:
                            current = json.loads(response.read())
                    except urllib.error.HTTPError as error:
                        with error:
                            current = None
                    except OSError:
                        current = None
                    if current and current["pending"]:
                        turn = int(current["observation"]["turn"])
                        if turn not in acted:
                            body = json.dumps({
                                "seat_id": "external", "turn": turn,
                                "action": action,
                            }).encode()
                            submit = urllib.request.Request(
                                agentd_url + "/v1/actions", data=body,
                                headers={
                                    "Authorization": f"Bearer {token}",
                                    "Content-Type": "application/json",
                                },
                            )
                            with urllib.request.urlopen(submit, timeout=2) as response:
                                self.assertEqual(response.status, 202)
                            acted.add(turn)
                    time.sleep(0.025)
                worker.join(5)
                self.assertFalse(worker.is_alive())
                if "error" in result:
                    raise result["error"]
                summary = result["summary"]
                self.assertEqual(acted, {1, 2})
                self.assertEqual(summary["manifest"]["status"], "completed")
                self.assertTrue(summary["manifest"]["benchmark_valid"])
                self.assertEqual(summary["manifest"]["fallbacks"], 0)
        finally:
            if old_token is None:
                os.environ.pop(token_env, None)
            else:
                os.environ[token_env] = old_token
