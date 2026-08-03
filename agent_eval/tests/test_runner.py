import io
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch

from agent_eval.__main__ import main
from agent_eval.config import EvalConfig, SeatConfig
from agent_eval.runner import _commands, benchmark_outcome, turn_timeout_seconds


class RunnerTests(unittest.TestCase):
    def test_native_only_commands_omit_unsafe_bridge(self):
        config = EvalConfig(
            1, "native-baseline", "classic", 2, (1,),
            (
                SeatConfig("native-a", "NativeOne", "native"),
                SeatConfig("native-b", "NativeTwo", "native"),
                SeatConfig("native-c", "NativeThree", "native"),
            ),
            {
                "allow_fallbacks": False,
                "frame_interval": 1,
                "frame_zoom": 1,
            },
        )
        commands = _commands(config, 1, None)
        self.assertIn("hard", commands)
        self.assertEqual(commands[-1], "start")
        self.assertFalse(any(
            command.startswith("lua unsafe-file ") for command in commands
        ))

    def test_fallback_invalid_by_default(self):
        self.assertEqual(benchmark_outcome("completed", 1, False), ("invalid", False))

    def test_allowed_fallback_completes_but_is_not_valid(self):
        self.assertEqual(benchmark_outcome("completed", 1, True), ("completed", False))

    def test_clean_completion_is_valid(self):
        self.assertEqual(benchmark_outcome("completed", 0, False), ("completed", True))

    @patch("agent_eval.__main__.run_episode")
    @patch("agent_eval.__main__.load_config")
    def test_run_cli_is_nonzero_for_invalid_benchmark(self, load, run):
        load.return_value = EvalConfig(
            1, "test", "classic", 1, (1,),
            (
                SeatConfig("a", "SeatOne", "native"),
                SeatConfig("b", "SeatTwo", "deterministic"),
            ),
            {"allow_fallbacks": False, "frame_interval": 5, "frame_zoom": 1},
        )
        run.return_value = {"manifest": {"status": "invalid"}}
        with redirect_stdout(io.StringIO()):
            result = main(["run", "config.json", "--output", "episode"])
        self.assertEqual(result, 1)

    def test_whole_turn_timeout_can_exceed_old_curl_cap(self):
        config = EvalConfig(
            1, "test", "classic", 1, (1,),
            (
                SeatConfig(
                    "external", "SeatOne", "external",
                    token_env="EXTERNAL_TOKEN", timeout_s=350,
                ),
                SeatConfig("native", "SeatTwo", "native"),
            ),
            {"allow_fallbacks": False, "agentd_port": 0, "frame_interval": 5, "frame_zoom": 1},
        )
        self.assertGreater(turn_timeout_seconds(config), 300)

    @patch("agent_eval.__main__.run_episode")
    @patch("agent_eval.__main__.load_config")
    def test_external_run_cli_flushes_discovery_metadata(self, load, run):
        load.return_value = EvalConfig(
            1, "test", "classic", 1, (1,),
            (
                SeatConfig(
                    "external", "SeatOne", "external",
                    token_env="EXTERNAL_TOKEN",
                ),
                SeatConfig("native", "SeatTwo", "native"),
            ),
            {"allow_fallbacks": False, "agentd_port": 0, "frame_interval": 5, "frame_zoom": 1},
        )

        def fake_run(*args, **kwargs):
            kwargs["on_ready"](
                Path("/episode"), Path("/episode/control.json"),
                {"agentd_url": "http://127.0.0.1:8765"},
            )
            return {"manifest": {"status": "completed"}}

        run.side_effect = fake_run
        stream = io.StringIO()
        with redirect_stdout(stream):
            result = main(["run", "config.json", "--output", "episode"])
        self.assertEqual(result, 0)
        self.assertIn("external control ready", stream.getvalue())
        self.assertIn("/episode/control.json", stream.getvalue())
