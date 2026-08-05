import json
import tempfile
import unittest
from pathlib import Path

from agent_eval.config import ConfigError, load_config, rotate_seats


class ConfigTests(unittest.TestCase):
    def write(self, value):
        temp = tempfile.TemporaryDirectory()
        path = Path(temp.name) / "config.json"
        path.write_text(json.dumps(value), encoding="utf-8")
        self.addCleanup(temp.cleanup)
        return path

    def base(self):
        return {"schema_version": 1, "name": "test", "turns": 10, "seeds": [7], "seats": [{"id": "a", "name": "SeatA", "type": "native"}, {"id": "b", "name": "SeatB", "type": "deterministic"}], "server": {}}

    def test_load_and_rotate_positions(self):
        config = load_config(self.write(self.base()))
        rotated = rotate_seats(config, 1)
        self.assertEqual([(seat.id, seat.name) for seat in rotated.seats], [("b", "SeatA"), ("a", "SeatB")])

    def test_two_or_three_seats_only(self):
        value = self.base()
        value["seats"] = value["seats"][:1]
        with self.assertRaises(ConfigError):
            load_config(self.write(value))

    def test_model_required(self):
        value = self.base()
        value["seats"][0]["type"] = "openai_responses"
        with self.assertRaises(ConfigError):
            load_config(self.write(value))

    def test_external_token_and_reserved_provider_options(self):
        value = self.base()
        value["seats"][0]["type"] = "external"
        with self.assertRaisesRegex(ConfigError, "token_env"):
            load_config(self.write(value))
        value["seats"][0] = {
            "id": "a", "name": "SeatA", "type": "openai_responses",
            "model": "model", "options": {"input": "override"},
        }
        with self.assertRaisesRegex(ConfigError, "reserved"):
            load_config(self.write(value))
        value["seats"][0]["options"] = {"api_key": "do-not-store"}
        with self.assertRaisesRegex(ConfigError, "secret"):
            load_config(self.write(value))

    def test_capture_defaults_are_resolved(self):
        config = load_config(self.write(self.base()))
        self.assertEqual(config.server["frame_interval"], 5)
        self.assertEqual(config.server["frame_zoom"], 1)
        self.assertFalse(config.server["allow_fallbacks"])
        self.assertEqual(config.server["agentd_port"], 0)

    def test_missing_turn_limit_defaults_to_allowed_maximum(self):
        value = self.base()
        value.pop("turns")
        self.assertEqual(load_config(self.write(value)).turns, 5000)

    def test_ports_and_timeouts_are_strict_and_finite(self):
        value = self.base()
        value["server"] = {"agentd_port": 1023}
        with self.assertRaisesRegex(ConfigError, "agentd_port"):
            load_config(self.write(value))
        value["server"] = {"agentd_port": 8765}
        self.assertEqual(load_config(self.write(value)).server["agentd_port"], 8765)
        value["seats"][0]["timeout_s"] = float("nan")
        with self.assertRaisesRegex(ConfigError, "finite"):
            load_config(self.write(value))
        value["seats"][0].pop("timeout_s")
        value["server"]["wall_timeout_s"] = float("inf")
        with self.assertRaisesRegex(ConfigError, "finite"):
            load_config(self.write(value))


if __name__ == "__main__":
    unittest.main()
