import json
import os
import re
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
from http import HTTPStatus
from pathlib import Path
from urllib.parse import quote

from agent_eval.client import create_game, join_game, request_json
from agent_eval.supervisor import Supervisor, make_supervisor_server


@unittest.skipUnless(
    os.environ.get("FREECIV_AGENT_E2E") == "1",
    "set FREECIV_AGENT_E2E=1 for the isolated city/worker HTTP smoke",
)
class V2CityWorkerRealE2ETests(unittest.TestCase):
    """Exercise actor-scoped management against disposable native processes.

    Classic seed 909 starts the external seat with two Settlers and two
    Workers on a tile where road is legal.  The isolated fixture installs one
    normal Classic Granary through Freeciv's test-only Lua edit API so sale can
    be exercised without waiting dozens of turns.  Every gameplay command,
    including all four city-management actions, still travels through the
    authenticated public v2 HTTP surface and durable receipt path.
    """

    @staticmethod
    def _http_json(method, url, token, body=None):
        data = None
        if body is not None:
            data = json.dumps(
                body, sort_keys=True, separators=(",", ":"),
            ).encode("utf-8")
        headers = {"Accept": "application/json"}
        if token is not None:
            headers["Authorization"] = f"Bearer {token}"
        if data is not None:
            headers["Content-Type"] = "application/json"
        request = urllib.request.Request(
            url, data=data, headers=headers, method=method,
        )
        try:
            with urllib.request.urlopen(request, timeout=20) as response:
                return (
                    response.status,
                    json.loads(response.read().decode("utf-8")),
                )
        except urllib.error.HTTPError as exc:
            with exc:
                return exc.code, json.loads(exc.read().decode("utf-8"))

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

    def _actor_actions(self, root, token, actor_id, public_payloads):
        page = request_json(
            "GET",
            f"{root}/legal-actions?actor_id={quote(actor_id)}&limit=16",
            token=token,
        )
        public_payloads.append(page)
        self.assertEqual(page["control_protocol"], "full-control-v2")
        self.assertEqual(page["page"]["scope"]["actor_id"], actor_id)
        actions = list(page["page"]["items"])
        cursor = page["page"]["next_cursor"]
        while cursor is not None:
            page = request_json(
                "GET",
                f"{root}/legal-actions?cursor={quote(cursor)}",
                token=token,
            )
            public_payloads.append(page)
            actions.extend(page["page"]["items"])
            cursor = page["page"]["next_cursor"]
        self.assertEqual(len(actions), page["page"]["total_items"])
        return actions

    def _state_items(self, root, token, section, public_payloads):
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

    def _assert_no_native_identifiers(self, values):
        native_ref = re.compile(
            r"(?<![A-Za-z0-9_])[pcu]:[0-9]+:[0-9]+"
            r"(?![A-Za-z0-9_])"
        )
        native_slot = re.compile(
            r"(?<![A-Za-z0-9_])a[0-9a-f]{16}(?![A-Za-z0-9_])"
        )

        def visit(value):
            if isinstance(value, dict):
                for key, child in value.items():
                    self.assertNotEqual(key, "slot")
                    self.assertNotEqual(key, "actor_ref")
                    self.assertFalse(key.startswith("native_"), key)
                    visit(child)
            elif isinstance(value, list):
                for child in value:
                    visit(child)
            elif isinstance(value, str):
                self.assertIsNone(native_ref.search(value), value)
                self.assertIsNone(native_slot.search(value), value)

        visit(values)

    def test_real_actor_scoped_worker_city_and_durable_receipts(self):
        repository = Path(__file__).parents[2]
        server_binary = repository / "build-agent" / "freeciv-server"
        agent_binary = repository / "build-control-v2" / "freeciv-agent"
        self.assertTrue(server_binary.is_file())
        self.assertTrue(os.access(server_binary, os.X_OK))
        self.assertTrue(agent_binary.is_file())
        self.assertTrue(os.access(agent_binary, os.X_OK))

        temporary = tempfile.TemporaryDirectory(
            prefix="freeciv-v2-city-worker-e2e-",
        )
        temporary_path = Path(temporary.name)
        supervisor = None
        server = None
        server_thread = None
        public_payloads = []
        try:
            supervisor = Supervisor(
                temporary_path / "runs",
                "isolated-city-worker-admin",
                binary=server_binary,
                agent_binary=agent_binary,
            )
            server = make_supervisor_server(supervisor, "127.0.0.1", 0)
            server_thread = threading.Thread(
                target=server.serve_forever,
                name="isolated-v2-city-worker-http",
                daemon=True,
            )
            server_thread.start()

            created = create_game(
                supervisor.service_url,
                "isolated-city-worker-admin",
                {
                    "mode": "single",
                    "places": 2,
                    "turns": 5,
                    "seed": 909,
                    "ruleset": "classic",
                    "objective": "Exercise city and worker control.",
                    "timing_mode": "infinite",
                    "action_timeout_s": None,
                    "lobby_timeout_s": 30,
                    "frame_interval": 0,
                    "frame_zoom": 1,
                    "control_protocol": "full-control-v2",
                },
            )
            public_payloads.append(created)
            self.assertEqual(created["control_protocol"], "full-control-v2")
            joined = join_game(
                supervisor.service_url,
                created["game_id"],
                created["join_token"],
                controller_label="codex-city-worker-e2e",
                supported_control_protocols=["full-control-v2"],
            )
            public_payloads.append(joined)
            self.assertEqual(joined["control_protocol"], "full-control-v2")
            self.assertEqual(
                joined["supported_control_protocols"], ["full-control-v2"],
            )

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

            status = request_json(
                "GET", f"{supervisor.service_url}/v1/games/{game_id}/status",
            )
            public_payloads.append(status)
            self.assertEqual(status["state"], "running")
            self.assertEqual(status["mode"], "single")
            self.assertEqual(status["control_protocol"], "full-control-v2")

            overview = request_json(
                "GET", f"{root}/state?section=overview&limit=16", token=token,
            )
            units_page = request_json(
                "GET", f"{root}/state?section=units&limit=16", token=token,
            )
            cities_page = request_json(
                "GET", f"{root}/state?section=cities&limit=16", token=token,
            )
            public_payloads.extend((overview, units_page, cities_page))
            self.assertEqual(overview["control_protocol"], "full-control-v2")
            self.assertTrue(overview["page"]["items"][0]["active_phase"])
            self.assertEqual(cities_page["page"]["items"], [])

            health_after_read = request_json(
                "GET", f"{root}/health", token=token,
            )
            public_payloads.append(health_after_read)
            self.assertTrue(health_after_read["observation_available"])
            self.assertTrue(health_after_read["legal_actions_available"])

            own_units = [
                unit for unit in units_page["page"]["items"]
                if unit["scope"] == "own"
            ]
            worker = next(
                unit for unit in own_units if unit["type"] == "Workers"
            )
            settler = next(
                unit for unit in own_units if unit["type"] == "Settlers"
            )
            self.assertEqual(worker["activity"]["name"], "idle")

            held_page = request_json(
                "GET",
                f"{root}/legal-actions?actor_id={quote(worker['id'])}&limit=1",
                token=token,
            )
            public_payloads.append(held_page)
            held_cursor = held_page["page"]["next_cursor"]
            self.assertIsNotNone(held_cursor)
            stale_action = held_page["page"]["items"][0]

            worker_actions = self._actor_actions(
                root, token, worker["id"], public_payloads,
            )
            start_road = next(
                action for action in worker_actions
                if action["subject"]["operation"] == "start_activity"
                and action["subject"]["target"]["name"] == "road"
            )
            self.assertEqual(start_road["kind"], "unit.perform_action")
            self.assertEqual(
                start_road["subject"]["actor"],
                {"type": "unit", "id": worker["id"]},
            )
            self.assertEqual(
                start_road["subject"]["target"]["extra"]["name"], "Road",
            )
            worker_batch = self._batch(
                game_id, joined, start_road, "e2e.worker.road", {},
            )
            worker_receipt = request_json(
                "POST", f"{root}/batches", token=token, body=worker_batch,
            )
            public_payloads.append(worker_receipt)
            self.assertEqual(worker_receipt["receipt_state"], "applied")
            self.assertFalse(worker_receipt["idempotent"])

            replayed_receipt = request_json(
                "POST", f"{root}/batches", token=token, body=worker_batch,
            )
            durable_receipt = request_json(
                "GET", f"{root}/receipts/e2e.worker.road", token=token,
            )
            public_payloads.extend((replayed_receipt, durable_receipt))
            self.assertTrue(replayed_receipt["idempotent"])
            # Direct receipt lookup returns the durable original; replaying
            # the identical batch is what carries the idempotence marker.
            self.assertFalse(durable_receipt["idempotent"])
            self.assertEqual(replayed_receipt["receipt_state"], "applied")
            self.assertEqual(durable_receipt["receipt_state"], "applied")
            self.assertEqual(
                durable_receipt["state_revision"],
                worker_receipt["state_revision"],
            )

            refreshed_units = request_json(
                "GET", f"{root}/state?section=units&limit=16", token=token,
            )
            public_payloads.append(refreshed_units)
            refreshed_worker = next(
                unit for unit in refreshed_units["page"]["items"]
                if unit["id"] == worker["id"]
            )
            self.assertEqual(refreshed_worker["activity"]["name"], "road")
            self.assertEqual(
                refreshed_worker["activity"]["id"],
                start_road["subject"]["target"]["id"],
            )
            self.assertEqual(
                refreshed_worker["activity"]["target"],
                start_road["subject"]["target"]["extra"],
            )

            cursor_status, cursor_error = self._http_json(
                "GET",
                f"{root}/legal-actions?cursor={quote(held_cursor)}",
                token,
            )
            public_payloads.append(cursor_error)
            # An authentic revision-bound actor cursor remains distinguishable
            # long enough to report a retryable stale revision.
            self.assertEqual(cursor_status, HTTPStatus.CONFLICT)
            self.assertEqual(
                cursor_error["error"]["code"], "stale_revision",
            )

            stale_batch = self._batch(
                game_id, joined, stale_action, "e2e.stale.action", {},
            )
            stale_status, stale_error = self._http_json(
                "POST", f"{root}/batches", token, stale_batch,
            )
            public_payloads.append(stale_error)
            self.assertEqual(stale_status, HTTPStatus.CONFLICT)
            self.assertEqual(stale_error["error"]["code"], "stale_revision")

            settler_actions = self._actor_actions(
                root, token, settler["id"], public_payloads,
            )
            found_city = next(
                action for action in settler_actions
                if action["subject"]["operation"] == "found_city"
            )
            self.assertEqual(found_city["kind"], "unit.perform_action")
            found_receipt = request_json(
                "POST",
                f"{root}/batches",
                token=token,
                body=self._batch(
                    game_id,
                    joined,
                    found_city,
                    "e2e.city.found",
                    {"city_name": "E2E City"},
                ),
            )
            public_payloads.append(found_receipt)
            self.assertEqual(found_receipt["receipt_state"], "applied")

            founded_cities = request_json(
                "GET", f"{root}/state?section=cities&limit=16", token=token,
            )
            public_payloads.append(founded_cities)
            city = next(
                item for item in founded_cities["page"]["items"]
                if item["name"] == "E2E City"
            )
            initial_production_id = city["production"]["id"]
            self.assertFalse(city["production"]["can_buy"])
            self.assertIn("citizens", city)
            self.assertTrue(city["citizens"]["tiles"])
            self.assertTrue(city["citizens"]["specialists"])
            self.assertIn("management", city)
            self.assertFalse(city["management"]["did_sell"])
            self.assertEqual(city["management"]["rally"], {
                "active": False,
                "persistent": False,
                "vigilant": False,
                "order_count": 0,
                "plan_id": None,
            })

            known_tiles = self._state_items(
                root, token, "known_tiles", public_payloads,
            )
            same_tile = request_json(
                "GET",
                f"{root}/legal-actions?actor_id={quote(city['id'])}"
                f"&target_id={quote(city['tile_id'])}",
                token=token,
            )
            public_payloads.append(same_tile)
            self.assertEqual(same_tile["page"]["items"], [])
            set_rally = None
            for target in known_tiles:
                if (
                    target["id"] == city["tile_id"]
                    or target["visibility"] == "unknown"
                ):
                    continue
                candidate = request_json(
                    "GET",
                    f"{root}/legal-actions?actor_id={quote(city['id'])}"
                    f"&target_id={quote(target['id'])}",
                    token=token,
                )
                public_payloads.append(candidate)
                self.assertIn(candidate["page"]["total_items"], {0, 1})
                if candidate["page"]["items"]:
                    set_rally = candidate["page"]["items"][0]
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
            rally_receipt = request_json(
                "POST", f"{root}/batches", token=token,
                body=self._batch(
                    game_id, joined, set_rally, "e2e.city.rally.set",
                    {"persistent": True},
                ),
            )
            public_payloads.append(rally_receipt)
            self.assertEqual(rally_receipt["receipt_state"], "applied")
            rallied_city = next(
                item for item in self._state_items(
                    root, token, "cities", public_payloads,
                ) if item["id"] == city["id"]
            )
            rally = rallied_city["management"]["rally"]
            self.assertTrue(rally["active"])
            self.assertTrue(rally["persistent"])
            self.assertFalse(rally["vigilant"])
            self.assertGreater(rally["order_count"], 0)
            self.assertRegex(rally["plan_id"], r"^rally_[0-9a-f]{32}$")

            rally_actions = self._actor_actions(
                root, token, city["id"], public_payloads,
            )
            clear_rally = next(
                action for action in rally_actions
                if action["subject"]["operation"] == "clear_rally"
            )
            self.assertEqual(clear_rally["kind"], "city.set_rally")
            clear_receipt = request_json(
                "POST", f"{root}/batches", token=token,
                body=self._batch(
                    game_id, joined, clear_rally, "e2e.city.rally.clear", {},
                ),
            )
            public_payloads.append(clear_receipt)
            self.assertEqual(clear_receipt["receipt_state"], "applied")
            cleared_city = next(
                item for item in self._state_items(
                    root, token, "cities", public_payloads,
                ) if item["id"] == city["id"]
            )
            self.assertEqual(cleared_city["management"]["rally"], {
                "active": False,
                "persistent": False,
                "vigilant": False,
                "order_count": 0,
                "plan_id": None,
            })
            queue_choice = next(
                item for item in city["management"]["build_choices"]
                if item["can_queue"]
            )

            management_actions = self._actor_actions(
                root, token, city["id"], public_payloads,
            )
            set_worklist = next(
                action for action in management_actions
                if action["subject"]["operation"] == "set_worklist"
            )
            worklist_batch = self._batch(
                game_id, joined, set_worklist, "e2e.city.worklist",
                {"items": [queue_choice["id"], queue_choice["id"]]},
            )
            worklist_receipt = request_json(
                "POST", f"{root}/batches", token=token, body=worklist_batch,
            )
            worklist_replay = request_json(
                "POST", f"{root}/batches", token=token, body=worklist_batch,
            )
            durable_worklist = request_json(
                "GET", f"{root}/receipts/e2e.city.worklist", token=token,
            )
            public_payloads.extend((
                worklist_receipt, worklist_replay, durable_worklist,
            ))
            self.assertEqual(worklist_receipt["receipt_state"], "applied")
            self.assertTrue(worklist_replay["idempotent"])
            self.assertEqual(durable_worklist["receipt_state"], "applied")
            worklist_cities = request_json(
                "GET", f"{root}/state?section=cities&limit=16", token=token,
            )
            public_payloads.append(worklist_cities)
            worklist_city = next(
                item for item in worklist_cities["page"]["items"]
                if item["id"] == city["id"]
            )
            self.assertEqual(
                [
                    item["production_id"]
                    for item in worklist_city["management"]["worklist"]
                ],
                [queue_choice["id"], queue_choice["id"]],
            )

            option_actions = self._actor_actions(
                root, token, city["id"], public_payloads,
            )
            set_options = next(
                action for action in option_actions
                if action["subject"]["operation"] == "set_options"
            )
            current_options = worklist_city["management"]["options"]
            desired_options = {
                "allow_disband": not current_options["allow_disband"],
                "new_citizens": (
                    "gold"
                    if current_options["new_citizens"] == "science"
                    else "science"
                ),
            }
            options_receipt = request_json(
                "POST", f"{root}/batches", token=token,
                body=self._batch(
                    game_id, joined, set_options, "e2e.city.options",
                    desired_options,
                ),
            )
            public_payloads.append(options_receipt)
            self.assertEqual(options_receipt["receipt_state"], "applied")
            options_cities = request_json(
                "GET", f"{root}/state?section=cities&limit=16", token=token,
            )
            public_payloads.append(options_cities)
            options_city = next(
                item for item in options_cities["page"]["items"]
                if item["id"] == city["id"]
            )
            self.assertEqual(
                options_city["management"]["options"],
                {**desired_options, "conflict": False},
            )

            rename_actions = self._actor_actions(
                root, token, city["id"], public_payloads,
            )
            rename = next(
                action for action in rename_actions
                if action["subject"]["operation"] == "rename"
            )
            rename_receipt = request_json(
                "POST", f"{root}/batches", token=token,
                body=self._batch(
                    game_id, joined, rename, "e2e.city.rename",
                    {"city_name": "E2E Managed City"},
                ),
            )
            public_payloads.append(rename_receipt)
            self.assertEqual(rename_receipt["receipt_state"], "applied")
            renamed_cities = request_json(
                "GET", f"{root}/state?section=cities&limit=16", token=token,
            )
            public_payloads.append(renamed_cities)
            renamed_city = next(
                item for item in renamed_cities["page"]["items"]
                if item["id"] == city["id"]
            )
            self.assertEqual(renamed_city["name"], "E2E Managed City")

            # Test-only state setup on this disposable Classic server.  The
            # sale itself remains a normal opaque public v2 capability.
            supervisor.game(game_id)._send_commands([
                "lua unsafe-cmd local p=find.player(\"AgentPlace1\"); "
                "local b=find.building_type(\"Granary\"); "
                "for c in p:cities_iterate() do c:create_building(b) end",
            ])
            deadline = time.monotonic() + 10
            while True:
                installed_cities = request_json(
                    "GET", f"{root}/state?section=cities&limit=16",
                    token=token,
                )
                installed_city = next(
                    item for item in installed_cities["page"]["items"]
                    if item["id"] == city["id"]
                )
                installed_granary = next((
                    item
                    for item in installed_city["management"]["improvements"]
                    if item["name"] == "Granary" and item["sellable"]
                ), None)
                if installed_granary is not None:
                    break
                self.assertLess(time.monotonic(), deadline, installed_city)
                time.sleep(0.05)
            public_payloads.append(installed_cities)
            before_sale_overview = request_json(
                "GET", f"{root}/state?section=overview&limit=16",
                token=token,
            )
            public_payloads.append(before_sale_overview)
            before_sale_gold = before_sale_overview[
                "page"
            ]["items"][0]["player"]["economy"]["gold"]
            sell_actions = self._actor_actions(
                root, token, city["id"], public_payloads,
            )
            sell = next(
                action for action in sell_actions
                if action["subject"]["operation"] == "sell_improvement"
                and action["subject"]["target"]["id"]
                == installed_granary["id"]
            )
            self.assertEqual(
                sell["subject"]["target"]["sell_price"],
                installed_granary["sell_price"],
            )
            sell_receipt = request_json(
                "POST", f"{root}/batches", token=token,
                body=self._batch(
                    game_id, joined, sell, "e2e.city.sell", {},
                ),
            )
            public_payloads.append(sell_receipt)
            self.assertEqual(sell_receipt["receipt_state"], "applied")
            sold_cities = request_json(
                "GET", f"{root}/state?section=cities&limit=16", token=token,
            )
            after_sale_overview = request_json(
                "GET", f"{root}/state?section=overview&limit=16",
                token=token,
            )
            public_payloads.extend((sold_cities, after_sale_overview))
            sold_city = next(
                item for item in sold_cities["page"]["items"]
                if item["id"] == city["id"]
            )
            self.assertTrue(sold_city["management"]["did_sell"])
            self.assertNotIn(
                installed_granary["id"],
                {
                    item["id"]
                    for item in sold_city["management"]["improvements"]
                },
            )
            self.assertEqual(
                after_sale_overview["page"]["items"][0]
                ["player"]["economy"]["gold"],
                before_sale_gold + installed_granary["sell_price"],
            )

            city_actions = self._actor_actions(
                root, token, city["id"], public_payloads,
            )
            unwork = next(
                action for action in city_actions
                if action["subject"]["operation"] == "unwork_tile"
            )
            citizen_tile_id = unwork["subject"]["target"]["id"]
            before_specialist_total = sum(
                item["count"] for item in city["citizens"]["specialists"]
            )
            unwork_batch = self._batch(
                game_id, joined, unwork, "e2e.city.unwork", {},
            )
            unwork_receipt = request_json(
                "POST", f"{root}/batches", token=token, body=unwork_batch,
            )
            unwork_replay = request_json(
                "POST", f"{root}/batches", token=token, body=unwork_batch,
            )
            durable_unwork = request_json(
                "GET", f"{root}/receipts/e2e.city.unwork", token=token,
            )
            public_payloads.extend((
                unwork_receipt, unwork_replay, durable_unwork,
            ))
            self.assertEqual(unwork_receipt["receipt_state"], "applied")
            self.assertFalse(unwork_receipt["idempotent"])
            self.assertTrue(unwork_replay["idempotent"])
            self.assertEqual(unwork_replay["receipt_state"], "applied")
            self.assertFalse(durable_unwork["idempotent"])
            self.assertEqual(durable_unwork["receipt_state"], "applied")
            self.assertEqual(
                durable_unwork["state_revision"],
                unwork_receipt["state_revision"],
            )

            unworked_cities = request_json(
                "GET", f"{root}/state?section=cities&limit=16", token=token,
            )
            public_payloads.append(unworked_cities)
            unworked_city = next(
                item for item in unworked_cities["page"]["items"]
                if item["id"] == city["id"]
            )
            unworked_tile = next(
                item for item in unworked_city["citizens"]["tiles"]
                if item["tile_id"] == citizen_tile_id
            )
            self.assertFalse(unworked_tile["worked"])
            self.assertEqual(
                sum(
                    item["count"]
                    for item in unworked_city["citizens"]["specialists"]
                ),
                before_specialist_total + 1,
            )

            after_unwork_actions = self._actor_actions(
                root, token, city["id"], public_payloads,
            )
            specialist_change = next(
                action for action in after_unwork_actions
                if action["subject"]["operation"] == "set_specialist"
            )
            specialist_target = specialist_change["subject"]["target"]
            specialist_before = {
                item["id"]: item["count"]
                for item in unworked_city["citizens"]["specialists"]
            }
            specialist_receipt = request_json(
                "POST", f"{root}/batches", token=token,
                body=self._batch(
                    game_id, joined, specialist_change,
                    "e2e.city.specialist", {},
                ),
            )
            public_payloads.append(specialist_receipt)
            self.assertEqual(specialist_receipt["receipt_state"], "applied")
            specialist_cities = request_json(
                "GET", f"{root}/state?section=cities&limit=16", token=token,
            )
            public_payloads.append(specialist_cities)
            specialist_city = next(
                item for item in specialist_cities["page"]["items"]
                if item["id"] == city["id"]
            )
            specialist_after = {
                item["id"]: item["count"]
                for item in specialist_city["citizens"]["specialists"]
            }
            self.assertEqual(
                specialist_after[specialist_target["from"]["id"]],
                specialist_before[specialist_target["from"]["id"]] - 1,
            )
            self.assertEqual(
                specialist_after[specialist_target["id"]],
                specialist_before[specialist_target["id"]] + 1,
            )

            after_specialist_actions = self._actor_actions(
                root, token, city["id"], public_payloads,
            )
            work = next(
                action for action in after_specialist_actions
                if action["subject"]["operation"] == "work_tile"
                and action["subject"]["target"]["id"] == citizen_tile_id
            )
            work_receipt = request_json(
                "POST", f"{root}/batches", token=token,
                body=self._batch(
                    game_id, joined, work, "e2e.city.work", {},
                ),
            )
            public_payloads.append(work_receipt)
            self.assertEqual(work_receipt["receipt_state"], "applied")
            reworked_cities = request_json(
                "GET", f"{root}/state?section=cities&limit=16", token=token,
            )
            public_payloads.append(reworked_cities)
            reworked_city = next(
                item for item in reworked_cities["page"]["items"]
                if item["id"] == city["id"]
            )
            self.assertTrue(next(
                item for item in reworked_city["citizens"]["tiles"]
                if item["tile_id"] == citizen_tile_id
            )["worked"])
            self.assertEqual(
                sum(
                    item["count"]
                    for item in reworked_city["citizens"]["specialists"]
                ),
                sum(
                    item["count"]
                    for item in specialist_city["citizens"]["specialists"]
                ) - 1,
            )

            city_actions = self._actor_actions(
                root, token, city["id"], public_payloads,
            )
            set_actions = [
                action for action in city_actions
                if action["subject"]["operation"] == "set_production"
            ]
            self.assertTrue(set_actions)
            self.assertTrue(all(
                action["kind"] == "city.set_production"
                and action["subject"]["target"]["id"]
                != initial_production_id
                for action in set_actions
            ))
            self.assertEqual(
                [
                    action for action in city_actions
                    if action["subject"]["operation"] == "buy_production"
                ],
                [],
            )
            selected_production = set_actions[0]
            selected_target = selected_production["subject"]["target"]
            production_receipt = request_json(
                "POST",
                f"{root}/batches",
                token=token,
                body=self._batch(
                    game_id,
                    joined,
                    selected_production,
                    "e2e.city.production",
                    {},
                ),
            )
            public_payloads.append(production_receipt)
            self.assertEqual(production_receipt["receipt_state"], "applied")

            changed_cities = request_json(
                "GET", f"{root}/state?section=cities&limit=16", token=token,
            )
            public_payloads.append(changed_cities)
            changed_city = next(
                item for item in changed_cities["page"]["items"]
                if item["id"] == city["id"]
            )
            self.assertEqual(
                changed_city["production"]["id"], selected_target["id"],
            )
            self.assertEqual(
                changed_city["production"]["kind"], selected_target["kind"],
            )
            self.assertEqual(
                changed_city["production"]["name"], selected_target["name"],
            )

            post_change_actions = self._actor_actions(
                root, token, city["id"], public_payloads,
            )
            buy_actions = [
                action for action in post_change_actions
                if action["subject"]["operation"] == "buy_production"
            ]
            if buy_actions:
                buy_receipt = request_json(
                    "POST",
                    f"{root}/batches",
                    token=token,
                    body=self._batch(
                        game_id,
                        joined,
                        buy_actions[0],
                        "e2e.city.buy",
                        {},
                    ),
                )
                public_payloads.append(buy_receipt)
                self.assertEqual(buy_receipt["receipt_state"], "applied")

            self._assert_no_native_identifiers(public_payloads)
            self.assertNotIn(
                '"strategic-v1"',
                json.dumps(public_payloads, sort_keys=True),
            )
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
