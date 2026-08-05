from __future__ import annotations

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
    "set FREECIV_AGENT_E2E=1 for the isolated city rally HTTP smoke",
)
class V2CityRallyRealE2ETests(unittest.TestCase):
    """Set and clear a real rally plan through the authenticated v2 API."""

    @staticmethod
    def _batch(game_id, joined, action, batch_id, arguments):
        return {
            "schema_version": 2,
            "control_protocol": "full-control-v2",
            "game_id": game_id,
            "agent_id": joined["agent_id"],
            "batch_id": batch_id,
            "state_revision": action["state_revision"],
            "commands": [{
                "action_id": action["action_id"],
                "arguments": arguments,
            }],
        }

    def _items(self, root, token, endpoint, query, public):
        page = request_json(
            "GET", f"{root}/{endpoint}?{query}", token=token,
        )
        public.append(page)
        items = list(page["page"]["items"])
        total = page["page"]["total_items"]
        cursor = page["page"]["next_cursor"]
        while cursor is not None:
            page = request_json(
                "GET", f"{root}/{endpoint}?cursor={quote(cursor)}",
                token=token,
            )
            public.append(page)
            items.extend(page["page"]["items"])
            cursor = page["page"]["next_cursor"]
        self.assertEqual(len(items), total)
        return items

    def _execute(self, root, token, game_id, joined, action, batch_id,
                 arguments, public):
        body = self._batch(
            game_id, joined, action, batch_id, arguments,
        )
        receipt = request_json(
            "POST", f"{root}/batches", token=token, body=body,
        )
        public.append(receipt)
        diagnostic = ""
        if receipt["receipt_state"] != "applied":
            root_path = getattr(self, "_diagnostic_root", None)
            if root_path is not None:
                diagnostic = "\n".join(
                    f"[{path}]\n{path.read_text(encoding='utf-8', errors='replace')}"
                    for path in root_path.rglob("*.log")
                )
        self.assertEqual(
            receipt["receipt_state"], "applied", f"{receipt}\n{diagnostic}",
        )
        self.assertFalse(receipt["idempotent"])
        durable = request_json(
            "GET", f"{root}/receipts/{quote(batch_id)}", token=token,
        )
        public.append(durable)
        self.assertEqual(durable, receipt)
        replay = request_json(
            "POST", f"{root}/batches", token=token, body=body,
        )
        public.append(replay)
        self.assertTrue(replay["idempotent"])
        self.assertEqual(replay["receipt_state"], "applied")

    def test_real_classic_target_rally_set_and_clear(self):
        repository = Path(__file__).parents[2]
        server_binary = repository / "build-agent" / "freeciv-server"
        agent_binary = repository / "build-control-v2" / "freeciv-agent"
        self.assertTrue(os.access(server_binary, os.X_OK))
        self.assertTrue(os.access(agent_binary, os.X_OK))

        temporary = tempfile.TemporaryDirectory(prefix="freeciv-v2-rally-e2e-")
        temporary_path = Path(temporary.name)
        self._diagnostic_root = temporary_path
        supervisor = None
        server = None
        server_thread = None
        public = []
        native_processes = []
        body_error = None
        cleanup_failures = []
        try:
            supervisor = Supervisor(
                temporary_path / "runs", "isolated-rally-admin",
                binary=server_binary, agent_binary=agent_binary,
            )
            server = make_supervisor_server(supervisor, "127.0.0.1", 0)
            server_thread = threading.Thread(
                target=server.serve_forever,
                name="isolated-v2-rally-http", daemon=True,
            )
            server_thread.start()
            created = create_game(
                supervisor.service_url, "isolated-rally-admin", {
                    "mode": "single", "places": 2, "turns": 3,
                    "seed": 909, "ruleset": "classic",
                    "objective": "Exercise city rally control.",
                    "timing_mode": "infinite", "action_timeout_s": None,
                    "lobby_timeout_s": 30, "frame_interval": 0,
                    "frame_zoom": 1, "control_protocol": "full-control-v2",
                },
            )
            public.append(created)
            joined = join_game(
                supervisor.service_url, created["game_id"],
                created["join_token"], controller_label="codex-rally-e2e",
                supported_control_protocols=["full-control-v2"],
            )
            public.append(joined)
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
            public.append(health)

            # Isolated setup only: create the subject city through Freeciv's
            # test edit API. Every rally action below uses public v2 HTTP.
            game = supervisor.game(game_id)
            game._send_commands([
                'lua unsafe-cmd local p=find.player("AgentPlace1"); '
                'local t=nil; for u in p:units_iterate() do t=u.tile; break end; '
                'assert(t); p:city_create(t,"Rally City",nil)',
            ])
            deadline = time.monotonic() + 20
            city = None
            while city is None:
                cities = self._items(
                    root, token, "state", "section=cities&limit=16", public,
                )
                city = next(
                    (item for item in cities if item["name"] == "Rally City"),
                    None,
                )
                self.assertLess(time.monotonic(), deadline, cities)
                if city is None:
                    time.sleep(0.05)
            self.assertEqual(city["management"]["rally"], {
                "active": False, "persistent": False, "vigilant": False,
                "order_count": 0, "plan_id": None,
            })
            tiles = self._items(
                root, token, "state", "section=known_tiles&limit=16", public,
            )
            same = request_json(
                "GET",
                f"{root}/legal-actions?actor_id={quote(city['id'])}"
                f"&target_id={quote(city['tile_id'])}", token=token,
            )
            public.append(same)
            self.assertEqual(same["page"]["items"], [])

            set_rally = None
            set_target_id = None
            for tile in tiles:
                if (
                    tile["id"] == city["tile_id"]
                    or tile["visibility"] == "unknown"
                ):
                    continue
                candidate = request_json(
                    "GET",
                    f"{root}/legal-actions?actor_id={quote(city['id'])}"
                    f"&target_id={quote(tile['id'])}", token=token,
                )
                public.append(candidate)
                self.assertIn(candidate["page"]["total_items"], {0, 1})
                if candidate["page"]["items"]:
                    set_rally = candidate["page"]["items"][0]
                    set_target_id = tile["id"]
                    break
            self.assertIsNotNone(set_rally, "no reachable known rally target")
            self.assertEqual(set_rally["kind"], "city.set_rally")
            self.assertEqual(
                set_rally["subject"]["operation"], "set_rally",
            )
            self.assertEqual(set_rally["arguments_schema"], {
                "type": "object",
                "properties": {"persistent": {"type": "boolean"}},
                "required": ["persistent"],
                "additionalProperties": False,
            })
            self._execute(
                root, token, game_id, joined, set_rally,
                "e2e.rally.set", {"persistent": True}, public,
            )

            rallied = next(
                item for item in self._items(
                    root, token, "state", "section=cities&limit=16", public,
                ) if item["id"] == city["id"]
            )
            rally = rallied["management"]["rally"]
            self.assertTrue(rally["active"])
            self.assertTrue(rally["persistent"])
            self.assertFalse(rally["vigilant"])
            self.assertGreater(rally["order_count"], 0)
            self.assertRegex(rally["plan_id"], r"^rally_[0-9a-f]{32}$")

            replacement = None
            for tile in tiles:
                if (
                    tile["id"] in {city["tile_id"], set_target_id}
                    or tile["visibility"] == "unknown"
                ):
                    continue
                candidate = request_json(
                    "GET",
                    f"{root}/legal-actions?actor_id={quote(city['id'])}"
                    f"&target_id={quote(tile['id'])}", token=token,
                )
                public.append(candidate)
                self.assertIn(candidate["page"]["total_items"], {0, 1})
                if candidate["page"]["items"]:
                    replacement = candidate["page"]["items"][0]
                    break
            self.assertIsNotNone(replacement, "no second rally target")
            self._execute(
                root, token, game_id, joined, replacement,
                "e2e.rally.replace", {"persistent": False}, public,
            )
            replaced = next(
                item for item in self._items(
                    root, token, "state", "section=cities&limit=16", public,
                ) if item["id"] == city["id"]
            )
            replaced_rally = replaced["management"]["rally"]
            self.assertTrue(replaced_rally["active"])
            self.assertFalse(replaced_rally["persistent"])
            self.assertFalse(replaced_rally["vigilant"])
            self.assertGreater(replaced_rally["order_count"], 0)
            self.assertRegex(
                replaced_rally["plan_id"], r"^rally_[0-9a-f]{32}$",
            )
            self.assertNotEqual(replaced_rally["plan_id"], rally["plan_id"])

            city_actions = self._items(
                root, token, "legal-actions",
                f"actor_id={quote(city['id'])}&limit=16", public,
            )
            clear = next(
                item for item in city_actions
                if item["subject"]["operation"] == "clear_rally"
            )
            self.assertEqual(clear["kind"], "city.set_rally")
            self._execute(
                root, token, game_id, joined, clear,
                "e2e.rally.clear", {}, public,
            )
            cleared = next(
                item for item in self._items(
                    root, token, "state", "section=cities&limit=16", public,
                ) if item["id"] == city["id"]
            )
            self.assertEqual(cleared["management"]["rally"], {
                "active": False, "persistent": False, "vigilant": False,
                "order_count": 0, "plan_id": None,
            })

            encoded = json.dumps(public, sort_keys=True)
            self.assertIsNone(re.search(r"[pcu]:[0-9]+:[0-9]+", encoded))
            for private in (
                "orders_digest", "fnv1a64-", "native_target_tile",
                "rally_point", "request_count", '"slot"',
            ):
                self.assertNotIn(private, encoded)
        except BaseException as exc:
            body_error = exc
        finally:
            if server is not None:
                try:
                    server.shutdown()
                    server.server_close()
                except BaseException as exc:
                    cleanup_failures.append(f"HTTP server close: {exc!r}")
            if server_thread is not None:
                server_thread.join(5)
            if supervisor is not None:
                try:
                    with supervisor.lock:
                        games = tuple(supervisor.games.values())
                    for game in games:
                        with game.condition:
                            if game.process is not None:
                                native_processes.append(("server", game.process))
                            native_processes.extend(
                                (f"agent-{place}", sidecar._process)
                                for place, sidecar in game.sidecars.items()
                                if sidecar._process is not None
                            )
                    supervisor.close()
                except BaseException as exc:
                    cleanup_failures.append(f"supervisor close: {exc!r}")
            for label, process in native_processes:
                if process.poll() is None:
                    cleanup_failures.append(f"{label} remained alive")
                    process.kill()
                    process.wait(timeout=5)
            if server_thread is not None and server_thread.is_alive():
                cleanup_failures.append("HTTP server thread remained alive")
            try:
                temporary.cleanup()
            except BaseException as exc:
                cleanup_failures.append(f"temporary cleanup: {exc!r}")
            if temporary_path.exists():
                cleanup_failures.append("temporary root remained")

        if cleanup_failures:
            cleanup_error = self.failureException(
                "isolated teardown failed: " + "; ".join(cleanup_failures),
            )
            if body_error is not None:
                raise cleanup_error from body_error
            raise cleanup_error
        if body_error is not None:
            raise body_error
