import json
import os
import re
import tempfile
import threading
import time
import unittest
from pathlib import Path
from urllib.parse import quote

from agent_eval.client import create_game, join_game, request_json
from agent_eval.supervisor import Supervisor, make_supervisor_server


@unittest.skipUnless(
    os.environ.get("FREECIV_AGENT_E2E") == "1",
    "set FREECIV_AGENT_E2E=1 for the isolated unit-self HTTP smoke",
)
class V2UnitSelfRealE2ETests(unittest.TestCase):
    """Exercise naturally legal unit-self controls in disposable processes."""

    @staticmethod
    def _batch(game_id, joined, action, batch_id):
        return {
            "schema_version": 2,
            "control_protocol": "full-control-v2",
            "game_id": game_id,
            "agent_id": joined["agent_id"],
            "batch_id": batch_id,
            "state_revision": action["state_revision"],
            "commands": [{
                "action_id": action["action_id"],
                "arguments": {},
            }],
        }

    def _section(self, root, token, section, public_payloads):
        page = request_json(
            "GET", f"{root}/state?section={section}&limit=16", token=token,
        )
        public_payloads.append(page)
        items = list(page["page"]["items"])
        total = page["page"]["total_items"]
        cursor = page["page"]["next_cursor"]
        while cursor is not None:
            page = request_json(
                "GET", f"{root}/state?cursor={quote(cursor)}", token=token,
            )
            public_payloads.append(page)
            items.extend(page["page"]["items"])
            cursor = page["page"]["next_cursor"]
        self.assertEqual(len(items), total)
        return items

    def _actions(self, root, token, actor_id, public_payloads):
        page = request_json(
            "GET",
            f"{root}/legal-actions?actor_id={quote(actor_id)}&limit=16",
            token=token,
        )
        public_payloads.append(page)
        self.assertEqual(page["page"]["scope"], {
            "actor_id": actor_id,
            "actor_type": "unit",
        })
        actions = list(page["page"]["items"])
        total = page["page"]["total_items"]
        cursor = page["page"]["next_cursor"]
        while cursor is not None:
            page = request_json(
                "GET", f"{root}/legal-actions?cursor={quote(cursor)}",
                token=token,
            )
            public_payloads.append(page)
            actions.extend(page["page"]["items"])
            cursor = page["page"]["next_cursor"]
        self.assertEqual(len(actions), total)
        return actions

    def _submit(
        self, root, token, game_id, joined, action, batch_id, public_payloads,
    ):
        receipt = request_json(
            "POST", f"{root}/batches", token=token,
            body=self._batch(game_id, joined, action, batch_id),
        )
        public_payloads.append(receipt)
        self.assertEqual(receipt["receipt_state"], "applied", receipt)
        self.assertFalse(receipt["idempotent"])
        self.assertIsNone(receipt["error"])
        return receipt

    def test_real_sentry_idle_fortify_and_disband(self):
        repository = Path(__file__).parents[2]
        server_binary = repository / "build-agent" / "freeciv-server"
        agent_binary = repository / "build-control-v2" / "freeciv-agent"
        self.assertTrue(os.access(server_binary, os.X_OK))
        self.assertTrue(os.access(agent_binary, os.X_OK))

        temporary = tempfile.TemporaryDirectory(
            prefix="freeciv-v2-unit-self-e2e-",
        )
        temporary_path = Path(temporary.name)
        supervisor = None
        server = None
        server_thread = None
        public_payloads = []
        try:
            supervisor = Supervisor(
                temporary_path / "runs",
                "isolated-unit-self-admin",
                binary=server_binary,
                agent_binary=agent_binary,
            )
            server = make_supervisor_server(supervisor, "127.0.0.1", 0)
            server_thread = threading.Thread(
                target=server.serve_forever,
                name="isolated-v2-unit-self-http",
                daemon=True,
            )
            server_thread.start()
            created = create_game(
                supervisor.service_url,
                "isolated-unit-self-admin",
                {
                    "mode": "single",
                    "places": 2,
                    "turns": 5,
                    "seed": 909,
                    "ruleset": "classic",
                    "objective": "Exercise exact unit self controls.",
                    "timing_mode": "infinite",
                    "action_timeout_s": None,
                    "lobby_timeout_s": 30,
                    "frame_interval": 0,
                    "frame_zoom": 1,
                    "control_protocol": "full-control-v2",
                },
            )
            public_payloads.append(created)
            joined = join_game(
                supervisor.service_url,
                created["game_id"],
                created["join_token"],
                controller_label="codex-unit-self-e2e",
                supported_control_protocols=["full-control-v2"],
            )
            public_payloads.append(joined)
            game_id = created["game_id"]
            token = joined["agent_token"]
            root = f"{supervisor.service_url}/v2/games/{game_id}/me"

            deadline = time.monotonic() + 30
            while True:
                health = request_json("GET", f"{root}/health", token=token)
                if (
                    health["game_state"] == "running"
                    and health["sidecar"]["state"] == "ready"
                    and health["sidecar"].get("client_state") == "running"
                ):
                    break
                self.assertLess(time.monotonic(), deadline, health)
                time.sleep(0.05)
            public_payloads.append(health)

            units = [
                item for item in self._section(
                    root, token, "units", public_payloads,
                ) if item["scope"] == "own"
            ]
            self.assertGreaterEqual(len(units), 2)
            self.assertTrue(all(
                item["type_id"].startswith("unit_type_") for item in units
            ))
            initial_actions = {
                unit["id"]: self._actions(
                    root, token, unit["id"], public_payloads,
                ) for unit in units
            }
            unit = next(
                candidate for candidate in units
                if {"sentry", "fortify"}.issubset({
                    action["subject"]["operation"]
                    for action in initial_actions[candidate["id"]]
                })
            )
            controls = {
                action["subject"]["operation"]: action
                for action in initial_actions[unit["id"]]
            }
            self.assertEqual(sum(
                action["subject"]["operation"] == "fortify"
                for action in initial_actions[unit["id"]]
            ), 1)
            self.assertNotIn("convert", controls)
            self.assertNotIn("make_homeless", controls)
            self.assertRegex(
                controls["sentry"]["subject"]["variant"],
                r"\Avariant_[0-9a-f]{32}\Z",
            )
            self.assertRegex(
                controls["fortify"]["subject"]["variant"],
                r"\Avariant_[0-9a-f]{32}\Z",
            )

            self._submit(
                root, token, game_id, joined, controls["sentry"],
                "e2e.unit.sentry", public_payloads,
            )
            sentried = next(
                item for item in self._section(
                    root, token, "units", public_payloads,
                ) if item["id"] == unit["id"]
            )
            self.assertEqual(sentried["activity"]["name"], "sentry")
            sentry_actions = self._actions(
                root, token, unit["id"], public_payloads,
            )
            self.assertNotIn(
                "sentry",
                {item["subject"]["operation"] for item in sentry_actions},
            )
            cancel = next(
                item for item in sentry_actions
                if item["subject"]["operation"] == "cancel_activity"
            )
            self._submit(
                root, token, game_id, joined, cancel,
                "e2e.unit.idle", public_payloads,
            )
            idle = next(
                item for item in self._section(
                    root, token, "units", public_payloads,
                ) if item["id"] == unit["id"]
            )
            self.assertEqual(idle["activity"]["name"], "idle")

            fortify = next(
                item for item in self._actions(
                    root, token, unit["id"], public_payloads,
                ) if item["subject"]["operation"] == "fortify"
            )
            self._submit(
                root, token, game_id, joined, fortify,
                "e2e.unit.fortify", public_payloads,
            )
            fortified = next(
                item for item in self._section(
                    root, token, "units", public_payloads,
                ) if item["id"] == unit["id"]
            )
            self.assertIn(
                fortified["activity"]["name"], {"fortifying", "fortified"},
            )
            post_fortify = self._actions(
                root, token, unit["id"], public_payloads,
            )
            self.assertNotIn(
                "fortify",
                {item["subject"]["operation"] for item in post_fortify},
            )

            consumed = next(candidate for candidate in units if candidate != unit)
            disband = next(
                item for item in self._actions(
                    root, token, consumed["id"], public_payloads,
                ) if item["subject"]["operation"] == "disband"
            )
            self.assertTrue(disband["subject"]["consuming"])
            self._submit(
                root, token, game_id, joined, disband,
                "e2e.unit.disband", public_payloads,
            )
            remaining = self._section(root, token, "units", public_payloads)
            self.assertNotIn(consumed["id"], {item["id"] for item in remaining})

            public = json.dumps(public_payloads, sort_keys=True)
            self.assertIsNone(re.search(r"[pcu]:[0-9]+:[0-9]+", public))
            for private in (
                "Convert Unit", "Disband Unit", "Unit Make Homeless",
                '"native_type_id"', '"slot"', '"actor_ref"',
            ):
                self.assertNotIn(private, public)
        finally:
            if server is not None:
                server.shutdown()
                server.server_close()
            if server_thread is not None:
                server_thread.join(5)
            if supervisor is not None:
                supervisor.close()
            temporary.cleanup()

        self.assertFalse(server_thread.is_alive())
        self.assertFalse(temporary_path.exists())


if __name__ == "__main__":
    unittest.main()
