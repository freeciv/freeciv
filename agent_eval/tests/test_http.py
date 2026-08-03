import json
import threading
import time
import unittest
import urllib.request
import urllib.error
from dataclasses import replace

from agent_eval.agentd import make_server
from agent_eval.config import EvalConfig, SeatConfig


class HTTPTests(unittest.TestCase):
    def setUp(self):
        self.config = EvalConfig(
            1, "test", "classic", 10, (1,),
            (
                SeatConfig("external", "SeatOne", "external", token_env="SEAT_ONE_TOKEN", timeout_s=2),
                SeatConfig("other", "SeatTwo", "external", token_env="SEAT_TWO_TOKEN", timeout_s=2),
            ),
            {"allow_fallbacks": False, "frame_interval": 5, "frame_zoom": 1},
        )
        self.server = make_server(
            self.config, "127.0.0.1", 0, internal_token="internal-secret",
            external_tokens={"external": "seat-one-secret", "other": "seat-two-secret"},
        )
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()

    def request(self, path, value=None, token=None):
        data = json.dumps(value).encode() if value is not None else None
        headers = {"Content-Type": "application/json"} if data else {}
        if token:
            headers["Authorization"] = f"Bearer {token}"
        req = urllib.request.Request(self.url + path, data=data, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=4) as response:
                return response.status, json.loads(response.read())
        except urllib.error.HTTPError as error:
            with error:
                return error.code, json.loads(error.read())

    def test_health_and_external_coordination(self):
        self.assertEqual(self.request("/health")[0], 200)
        self.assertEqual(self.request("/v1/game")[0], 401)
        self.assertEqual(self.request("/v1/game", token="seat-one-secret")[0], 403)
        self.assertEqual(self.request("/v1/game", token="internal-secret")[0], 200)
        self.assertEqual(self.request("/v1/trace")[0], 401)
        self.assertEqual(self.request("/v1/trace", token="internal-secret")[0], 200)
        observation = {"seat_id": "external", "turn": 3, "year": -3000, "num_cities": 1, "num_units": 2, "gold": 20}
        result = {}
        turn_payload = {"turn": 3, "year": -3000, "observations": [observation]}
        self.assertEqual(self.request("/v1/turn", turn_payload)[0], 401)

        def turn_request():
            result["value"] = self.request(
                "/v1/turn",
                turn_payload,
                "internal-secret",
            )

        worker = threading.Thread(target=turn_request)
        worker.start()
        for _ in range(40):
            self.assertEqual(self.request("/v1/seats/external/observation")[0], 401)
            self.assertEqual(
                self.request("/v1/seats/external/observation", token="seat-two-secret")[0],
                403,
            )
            status, current = self.request(
                "/v1/seats/external/observation", token="seat-one-secret"
            )
            if status == 200 and current["pending"]:
                break
            time.sleep(0.025)
        action = {"type": "set_traits", "traits": {"aggressive": 1, "builder": 2, "expansionist": 3, "trader": 4}}
        payload = {"seat_id": "external", "turn": 3, "action": action}
        self.assertEqual(self.request("/v1/actions", payload)[0], 401)
        self.assertEqual(
            self.request("/v1/actions", payload, "seat-two-secret")[0], 403
        )
        self.assertEqual(
            self.request("/v1/actions", payload, "seat-one-secret")[0], 202
        )
        worker.join(3)
        self.assertEqual(result["value"][1]["actions"][0]["traits"]["trader"], 4)
        self.assertEqual(result["value"][1]["timed_out_seats"], [])
        self.assertTrue(result["value"][1]["benchmark_valid"])


if __name__ == "__main__":
    unittest.main()
