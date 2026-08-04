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

from agent_eval.client import ClientError, create_game, join_game, request_json
from agent_eval.supervisor import Supervisor, make_supervisor_server


@unittest.skipUnless(
    os.environ.get("FREECIV_AGENT_E2E") == "1",
    "set FREECIV_AGENT_E2E=1 for the isolated economic-unit HTTP smoke",
)
class V2EconomicUnitRealE2ETests(unittest.TestCase):
    """Exercise all six city-target unit controls in disposable processes."""

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

    def _pages(self, root, token, endpoint, query, public_payloads):
        page = request_json(
            "GET", f"{root}/{endpoint}?{query}", token=token,
        )
        public_payloads.append(page)
        items = list(page["page"]["items"])
        total = page["page"]["total_items"]
        cursor = page["page"]["next_cursor"]
        while cursor is not None:
            page = request_json(
                "GET", f"{root}/{endpoint}?cursor={quote(cursor)}",
                token=token,
            )
            public_payloads.append(page)
            items.extend(page["page"]["items"])
            cursor = page["page"]["next_cursor"]
        self.assertEqual(len(items), total)
        return items

    def _state(self, root, token, section, public_payloads):
        return self._pages(
            root, token, "state", f"section={section}&limit=16",
            public_payloads,
        )

    def _actions(self, root, token, actor_id, public_payloads):
        actions = self._pages(
            root, token, "legal-actions",
            f"actor_id={quote(actor_id)}&limit=16", public_payloads,
        )
        return actions

    def _submit(
        self, root, token, game_id, joined, action, batch_id,
        public_payloads,
    ):
        self._active_submission = {
            "action_id": action["action_id"],
            "actor_id": action["subject"]["actor"]["id"],
            "batch_id": batch_id,
            "operation": action["subject"]["operation"],
            "stage": "posting_batch",
        }
        batch = self._batch(game_id, joined, action, batch_id)
        receipt = request_json(
            "POST", f"{root}/batches", token=token, body=batch,
        )
        self._active_submission["stage"] = "receipt_received"
        self._active_submission["receipt_state"] = receipt.get(
            "receipt_state",
        )
        public_payloads.append(receipt)
        diagnostics = None
        if receipt["receipt_state"] != "applied":
            try:
                diagnostics = {
                    "cities": self._state(
                        root, token, "cities", public_payloads,
                    ),
                    "units": self._state(
                        root, token, "units", public_payloads,
                    ),
                }
            except ClientError as exc:
                diagnostics = {"state_error": (exc.status, str(exc))}
        self.assertEqual(
            receipt["receipt_state"], "applied",
            {"receipt": receipt, "post_state": diagnostics},
        )
        self.assertFalse(receipt["idempotent"])
        self.assertIsNone(receipt["error"])
        durable = request_json(
            "GET", f"{root}/receipts/{quote(batch_id)}", token=token,
        )
        self._active_submission["stage"] = "durable_receipt_received"
        public_payloads.append(durable)
        self.assertEqual(durable, receipt)
        repeated = request_json(
            "POST", f"{root}/batches", token=token, body=batch,
        )
        self._active_submission["stage"] = "idempotent_receipt_received"
        public_payloads.append(repeated)
        self.assertTrue(repeated["idempotent"])
        self.assertEqual(
            {key: value for key, value in repeated.items()
             if key != "idempotent"},
            {key: value for key, value in receipt.items()
             if key != "idempotent"},
        )
        self._last_submission = dict(self._active_submission)
        self._active_submission = None
        return receipt

    def _private_failure_diagnostics(self, game):
        diagnostics = {
            "active_submission": getattr(
                self, "_active_submission", None,
            ),
            "last_submission": getattr(self, "_last_submission", None),
            "server_output_tail": (
                game.server_output_tail[-32_768:]
                if game is not None else "game not created"
            ),
            "server_returncode": (
                game.process.poll()
                if game is not None and game.process is not None else None
            ),
        }
        if game is None:
            return diagnostics
        trace_path = game.episode / "v2-ambiguity-trace" / "events.jsonl"
        try:
            diagnostics["ambiguity_trace"] = trace_path.read_text(
                encoding="ascii", errors="replace",
            )[-16_384:]
        except OSError as exc:
            diagnostics["ambiguity_trace"] = repr(exc)
        for place, sidecar in game.sidecars.items():
            process = sidecar._process
            sidecar_diagnostics = {
                "health": sidecar.public_health(),
                "returncode": process.poll() if process is not None else None,
                "queued_protocol_messages": list(sidecar._messages),
            }
            for stream, path in (
                ("stdout_tail", sidecar.stdout_path),
                ("stderr_tail", sidecar.stderr_path),
            ):
                try:
                    sidecar_diagnostics[stream] = path.read_text(
                        encoding="utf-8", errors="replace",
                    )[-32_768:]
                except OSError as exc:
                    sidecar_diagnostics[stream] = repr(exc)
            diagnostics[f"sidecar_{place}"] = sidecar_diagnostics
        return diagnostics

    def _wait_for(self, description, probe, predicate, game):
        deadline = time.monotonic() + 20
        last = None
        while True:
            last = probe()
            if predicate(last):
                return last
            self.assertLess(
                time.monotonic(), deadline,
                f"timed out waiting for {description}: {last!r}; "
                f"server={game.server_output_tail!r}",
            )
            time.sleep(0.05)

    def test_real_classic_all_six_city_target_unit_actions(self):
        repository = Path(__file__).parents[2]
        server_binary = repository / "build-agent" / "freeciv-server"
        agent_binary = repository / "build-control-v2" / "freeciv-agent"
        self.assertTrue(os.access(server_binary, os.X_OK))
        self.assertTrue(os.access(agent_binary, os.X_OK))

        temporary = tempfile.TemporaryDirectory(
            prefix="freeciv-v2-economic-unit-e2e-",
        )
        temporary_path = Path(temporary.name)
        supervisor = None
        server = None
        server_thread = None
        native_processes = []
        public_payloads = []
        body_error = None
        cleanup_failures = []
        game = None
        self._active_submission = None
        self._last_submission = None
        try:
            supervisor = Supervisor(
                temporary_path / "runs", "isolated-economic-unit-admin",
                binary=server_binary, agent_binary=agent_binary,
            )
            server = make_supervisor_server(supervisor, "127.0.0.1", 0)
            server_thread = threading.Thread(
                target=server.serve_forever,
                name="isolated-v2-economic-unit-http", daemon=True,
            )
            server_thread.start()
            created = create_game(
                supervisor.service_url,
                "isolated-economic-unit-admin",
                {
                    "mode": "single",
                    "places": 2,
                    "turns": 3,
                    "seed": 909,
                    "ruleset": "classic",
                    "objective": "Exercise exact city-target unit actions.",
                    "timing_mode": "infinite",
                    "action_timeout_s": None,
                    "lobby_timeout_s": 30,
                    "frame_interval": 0,
                    "frame_zoom": 1,
                    "control_protocol": "full-control-v2",
                },
            )
            public_payloads.append(created)
            game = supervisor.game(created["game_id"])
            game._send_commands(["set landmass 70", "set dispersion 10"])
            joined = join_game(
                supervisor.service_url, created["game_id"],
                created["join_token"],
                controller_label="codex-economic-unit-e2e",
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

            # Test-only setup is confined to this disposable game. Every
            # gameplay mutation below is still selected and submitted through
            # the authenticated public v2 action and receipt surface.
            game._send_commands([
                'lua unsafe-cmd local p=find.player("AgentPlace1"); '
                'local a=nil; for u in p:units_iterate() do a=u.tile; '
                'break end; assert(a); local grass=find.terrain("Grassland"); '
                'local b=nil; local c=nil; local d=nil; '
                'for t in whole_map_iterate() do if t.terrain.id==grass.id '
                'and t:num_units()==0 and t:city()==nil and '
                'a:sq_distance(t)>=36 then if b==nil then b=t elseif c==nil '
                'and b:sq_distance(t)>=36 then c=t elseif d==nil and '
                'b:sq_distance(t)>=36 and c:sq_distance(t)>=36 then d=t; '
                'break end end end; assert(b and c and d); '
                'assert(edit.city_create(p,a,"Economic Source",p)); '
                'assert(edit.city_create(p,b,"Unit City",p)); '
                'assert(edit.city_create(p,c,"Trade City",p)); '
                'assert(edit.city_create(p,d,"Wonder City",p))',
                'lua unsafe-cmd local p=find.player("AgentPlace1"); '
                'local function give(n) local t=find.tech_type(n); assert(t); '
                'if not p:knows_tech(t) then '
                'assert(p:give_tech(t,0,false,"researched")) end end; '
                'give("Bronze Working"); give("Trade"); give("Explosives"); '
                'p:change_gold(10000)',
                'lua unsafe-cmd local p=find.player("AgentPlace1"); '
                'local s=nil; local u=nil; local t=nil; local w=nil; '
                'for c in p:cities_iterate() do if c.name=="Economic Source" '
                'then s=c elseif c.name=="Unit City" then u=c elseif '
                'c.name=="Trade City" then t=c elseif c.name=="Wonder City" '
                'then w=c end end; assert(s and u and t and w); '
                'u:change_size(2,p); '
                'p:create_unit(u.tile,find.unit_type("Workers"),0,u,-1); '
                'p:create_unit(u.tile,find.unit_type("Warriors"),0,s,-1); '
                'p:create_unit(u.tile,find.unit_type("Settlers"),0,s,-1); '
                'p:create_unit(t.tile,find.unit_type("Caravan"),0,s,-1); '
                'p:create_unit(t.tile,find.unit_type("Caravan"),0,s,-1); '
                'p:create_unit(w.tile,find.unit_type("Caravan"),0,s,-1); '
                's.tile:show(p); u.tile:show(p); t.tile:show(p); w.tile:show(p)',
            ])

            def fixture_state():
                cities = self._state(
                    root, token, "cities", public_payloads,
                )
                units = self._state(root, token, "units", public_payloads)
                return cities, units

            cities, units = self._wait_for(
                "economic fixture state", fixture_state,
                lambda value: (
                    {city["name"] for city in value[0]}
                    >= {"Economic Source", "Unit City", "Trade City",
                        "Wonder City"}
                    and sum(
                        unit["type"] == "Caravan" for unit in value[1]
                    ) >= 3
                    and {"Workers", "Warriors", "Settlers"}
                    <= {unit["type"] for unit in value[1]}
                ), game,
            )
            cities_by_name = {city["name"]: city for city in cities}
            source = cities_by_name["Economic Source"]
            unit_city = cities_by_name["Unit City"]
            trade_city = cities_by_name["Trade City"]
            wonder_city = cities_by_name["Wonder City"]

            sites = self._state(root, token, "city_sites", public_payloads)
            sites_by_name = {site["name"]: site for site in sites}
            for name, city in cities_by_name.items():
                site = sites_by_name[name]
                self.assertEqual(site["id"], city["id"])
                self.assertEqual(site["tile_id"], city["tile_id"])
                self.assertEqual(site["visibility"], "own")
                self.assertNotIn("production", site)
                self.assertNotIn("citizens", site)

            # Prepare a real Great Wonder target through the public city
            # production action before exercising Help Wonder.
            wonder_actions = self._actions(
                root, token, wonder_city["id"], public_payloads,
            )
            colossus = next(
                action for action in wonder_actions
                if action["subject"]["operation"] == "set_production"
                and action["subject"]["target"]["name"] == "Colossus"
            )
            self._submit(
                root, token, game_id, joined, colossus,
                "e2e.economic.prepare-wonder", public_payloads,
            )

            def unit_at(unit_type, tile_id):
                return next(
                    unit for unit in self._state(
                        root, token, "units", public_payloads,
                    )
                    if unit["scope"] == "own"
                    and unit["type"] == unit_type
                    and unit["tile_id"] == tile_id
                )

            worker = unit_at("Workers", unit_city["tile_id"])
            upgrade = next(
                action for action in self._actions(
                    root, token, worker["id"], public_payloads,
                )
                if action["subject"]["operation"] == "upgrade"
                and action["subject"]["target"]["id"] == unit_city["id"]
            )
            self.assertEqual(upgrade["subject"]["upgrade_to"]["name"], "Engineers")
            self.assertFalse(upgrade["subject"]["consuming"])
            self._submit(
                root, token, game_id, joined, upgrade,
                "e2e.economic.upgrade", public_payloads,
            )
            upgraded = next(
                unit for unit in self._state(
                    root, token, "units", public_payloads,
                ) if unit["id"] == worker["id"]
            )
            self.assertEqual(upgraded["type"], "Engineers")

            warrior = unit_at("Warriors", unit_city["tile_id"])
            self.assertEqual(warrior["home_city_id"], source["id"])
            rehome = next(
                action for action in self._actions(
                    root, token, warrior["id"], public_payloads,
                )
                if action["subject"]["operation"] == "rehome"
                and action["subject"]["target"]["id"] == unit_city["id"]
            )
            self.assertFalse(rehome["subject"]["consuming"])
            self._submit(
                root, token, game_id, joined, rehome,
                "e2e.economic.rehome", public_payloads,
            )
            rehomed = next(
                unit for unit in self._state(
                    root, token, "units", public_payloads,
                ) if unit["id"] == warrior["id"]
            )
            self.assertEqual(rehomed["home_city_id"], unit_city["id"])

            settler = unit_at("Settlers", unit_city["tile_id"])
            city_before_join = next(
                city for city in self._state(
                    root, token, "cities", public_payloads,
                ) if city["id"] == unit_city["id"]
            )
            join_city = next(
                action for action in self._actions(
                    root, token, settler["id"], public_payloads,
                )
                if action["subject"]["operation"] == "join_city"
                and action["subject"]["target"]["id"] == unit_city["id"]
            )
            self.assertTrue(join_city["subject"]["consuming"])
            self._submit(
                root, token, game_id, joined, join_city,
                "e2e.economic.join-city", public_payloads,
            )
            after_join_units = self._state(
                root, token, "units", public_payloads,
            )
            self.assertNotIn(settler["id"], {unit["id"] for unit in after_join_units})
            city_after_join = next(
                city for city in self._state(
                    root, token, "cities", public_payloads,
                ) if city["id"] == unit_city["id"]
            )
            self.assertEqual(city_after_join["size"], city_before_join["size"] + 1)

            trade_caravans = [
                unit for unit in self._state(
                    root, token, "units", public_payloads,
                )
                if unit["scope"] == "own" and unit["type"] == "Caravan"
                and unit["tile_id"] == trade_city["tile_id"]
            ]
            self.assertEqual(len(trade_caravans), 2)
            first_trade_actions = self._actions(
                root, token, trade_caravans[0]["id"], public_payloads,
            )
            establish_trade = next(
                action for action in first_trade_actions
                if action["subject"]["operation"] == "establish_trade"
                and action["subject"]["target"]["id"] == trade_city["id"]
            )
            self.assertEqual(
                establish_trade["subject"]["source_city"]["id"], source["id"],
            )
            self.assertTrue(establish_trade["subject"]["consuming"])
            self._submit(
                root, token, game_id, joined, establish_trade,
                "e2e.economic.establish-trade", public_payloads,
            )

            remaining_trade = next(
                unit for unit in self._state(
                    root, token, "units", public_payloads,
                )
                if unit["scope"] == "own" and unit["type"] == "Caravan"
                and unit["tile_id"] == trade_city["tile_id"]
            )
            marketplace = next(
                action for action in self._actions(
                    root, token, remaining_trade["id"], public_payloads,
                )
                if action["subject"]["operation"] == "marketplace"
                and action["subject"]["target"]["id"] == trade_city["id"]
            )
            self.assertEqual(
                marketplace["subject"]["source_city"]["id"], source["id"],
            )
            self.assertTrue(marketplace["subject"]["consuming"])
            self._submit(
                root, token, game_id, joined, marketplace,
                "e2e.economic.marketplace", public_payloads,
            )

            wonder_caravan = unit_at("Caravan", wonder_city["tile_id"])
            wonder_before = next(
                city for city in self._state(
                    root, token, "cities", public_payloads,
                ) if city["id"] == wonder_city["id"]
            )
            self.assertEqual(wonder_before["production"]["name"], "Colossus")
            help_wonder = next(
                action for action in self._actions(
                    root, token, wonder_caravan["id"], public_payloads,
                )
                if action["subject"]["operation"] == "help_wonder"
                and action["subject"]["target"]["id"] == wonder_city["id"]
            )
            self.assertTrue(help_wonder["subject"]["consuming"])
            self._submit(
                root, token, game_id, joined, help_wonder,
                "e2e.economic.help-wonder", public_payloads,
            )
            wonder_after = next(
                city for city in self._state(
                    root, token, "cities", public_payloads,
                ) if city["id"] == wonder_city["id"]
            )
            self.assertEqual(
                wonder_after["production"]["shield_stock"],
                wonder_before["production"]["shield_stock"] + 50,
            )
            final_units = self._state(root, token, "units", public_payloads)
            consumed_ids = {
                settler["id"], trade_caravans[0]["id"],
                remaining_trade["id"], wonder_caravan["id"],
            }
            self.assertTrue(consumed_ids.isdisjoint(
                {unit["id"] for unit in final_units}
            ))

            public = json.dumps(public_payloads, sort_keys=True)
            self.assertIsNone(re.search(r"[pcu]:[0-9]+:[0-9]+", public))
            for private in (
                "Upgrade Unit", "Home City", "Join City",
                "Establish Trade Route", "Enter Marketplace", "Help Wonder",
                '"native_",', '"slot"', '"lifecycle_id"',
            ):
                self.assertNotIn(private, public)
        except BaseException as exc:
            try:
                private_diagnostics = self._private_failure_diagnostics(game)
            except BaseException as diagnostic_exc:
                private_diagnostics = {
                    "diagnostic_capture_error": repr(diagnostic_exc),
                    "original_error": repr(exc),
                }
            body_error = self.failureException(
                f"{exc!r}; private fixture diagnostics={private_diagnostics!r}",
            )
        finally:
            if server is not None:
                try:
                    server.shutdown()
                    server.server_close()
                except BaseException as exc:
                    cleanup_failures.append(f"HTTP server close: {exc!r}")
            if server_thread is not None:
                try:
                    server_thread.join(5)
                except BaseException as exc:
                    cleanup_failures.append(f"HTTP thread join: {exc!r}")
            if supervisor is not None:
                try:
                    with supervisor.lock:
                        cleanup_games = tuple(supervisor.games.values())
                    for cleanup_game in cleanup_games:
                        with cleanup_game.condition:
                            if cleanup_game.process is not None:
                                native_processes.append((
                                    "freeciv-server", cleanup_game.process,
                                ))
                            native_processes.extend(
                                (
                                    f"freeciv-agent-place-{place}",
                                    sidecar._process,
                                )
                                for place, sidecar
                                in cleanup_game.sidecars.items()
                                if sidecar._process is not None
                            )
                except BaseException as exc:
                    cleanup_failures.append(
                        f"native process capture: {exc!r}",
                    )
                try:
                    supervisor.close()
                except BaseException as exc:
                    cleanup_failures.append(f"supervisor close: {exc!r}")
            for label, process in native_processes:
                try:
                    if process.poll() is None:
                        cleanup_failures.append(
                            f"{label} process remained alive after close",
                        )
                        process.kill()
                        process.wait(timeout=5)
                except BaseException as exc:
                    cleanup_failures.append(
                        f"{label} process verification: {exc!r}",
                    )
            if server_thread is not None and server_thread.is_alive():
                cleanup_failures.append("HTTP server thread remained alive")
            try:
                temporary.cleanup()
            except BaseException as exc:
                cleanup_failures.append(f"temporary cleanup: {exc!r}")
            if temporary_path.exists():
                cleanup_failures.append(
                    f"temporary root remained: {temporary_path}",
                )

        if cleanup_failures:
            cleanup_error = self.failureException(
                "isolated teardown failed: " + "; ".join(cleanup_failures),
            )
            if body_error is not None:
                raise cleanup_error from body_error
            raise cleanup_error
        if body_error is not None:
            raise body_error


if __name__ == "__main__":
    unittest.main()
