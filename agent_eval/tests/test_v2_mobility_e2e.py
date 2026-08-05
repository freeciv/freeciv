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
    "set FREECIV_AGENT_E2E=1 for the isolated mobility HTTP smoke",
)
class V2MobilityRealE2ETests(unittest.TestCase):
    """Exercise Classic airlift and target-scoped paradrop through public v2."""

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

    def _wait_overview(self, root, token, public_payloads, predicate):
        deadline = time.monotonic() + 30
        while True:
            try:
                overview = self._pages(
                    root, token, "state", "section=overview&limit=16",
                    public_payloads,
                )[0]
                if predicate(overview):
                    return overview
            except ClientError as exc:
                if exc.status != 503:
                    raise
            self.assertLess(time.monotonic(), deadline)
            time.sleep(0.05)

    def _assert_durable_idempotent(
        self, root, token, batch, batch_id, receipt, public_payloads,
    ):
        durable = request_json(
            "GET", f"{root}/receipts/{quote(batch_id)}", token=token,
        )
        public_payloads.append(durable)
        self.assertEqual(durable, receipt)
        repeated = request_json(
            "POST", f"{root}/batches", token=token, body=batch,
        )
        public_payloads.append(repeated)
        self.assertTrue(repeated["idempotent"])
        self.assertEqual(
            {key: value for key, value in repeated.items()
             if key != "idempotent"},
            {key: value for key, value in receipt.items()
             if key != "idempotent"},
        )

    def test_real_classic_airlift_then_seen_paradrop(self):
        repository = Path(__file__).parents[2]
        server_binary = repository / "build-agent" / "freeciv-server"
        agent_binary = repository / "build-control-v2" / "freeciv-agent"
        self.assertTrue(os.access(server_binary, os.X_OK))
        self.assertTrue(os.access(agent_binary, os.X_OK))

        temporary = tempfile.TemporaryDirectory(
            prefix="freeciv-v2-mobility-e2e-",
        )
        temporary_path = Path(temporary.name)
        supervisor = None
        server = None
        server_thread = None
        public_payloads = []
        native_processes = []
        body_error = None
        cleanup_failures = []
        try:
            supervisor = Supervisor(
                temporary_path / "runs",
                "isolated-mobility-admin",
                binary=server_binary,
                agent_binary=agent_binary,
            )
            server = make_supervisor_server(supervisor, "127.0.0.1", 0)
            server_thread = threading.Thread(
                target=server.serve_forever,
                name="isolated-v2-mobility-http",
                daemon=True,
            )
            server_thread.start()
            created = create_game(
                supervisor.service_url,
                "isolated-mobility-admin",
                {
                    "mode": "single",
                    "places": 2,
                    "turns": 3,
                    "seed": 887,
                    "ruleset": "classic",
                    "objective": "Exercise exact noncombat mobility.",
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
            game._send_commands([
                "set startunits cc",
                "set dispersion 10",
                "set landmass 70",
            ])
            joined = join_game(
                supervisor.service_url,
                created["game_id"],
                created["join_token"],
                controller_label="codex-mobility-e2e",
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
            initial = self._wait_overview(
                root, token, public_payloads,
                lambda value: value["active_phase"],
            )

            # Disposable test-only setup. Gameplay actions below still use
            # opaque authenticated v2 capabilities and normal client packets.
            game._send_commands([
                'lua unsafe-cmd local p=find.player("AgentPlace1"); '
                'local a=nil; for u in p:units_iterate() do a=u.tile; '
                'break end; assert(a); local b=nil; '
                'local grass=find.terrain("Grassland"); '
                'for t in whole_map_iterate() do if t.terrain.id==grass.id '
                'and t:num_units()==0 and t:city()==nil and '
                'a:sq_distance(t)>=36 then b=t; break end end; assert(b); '
                'assert(edit.city_create(p,a,"Airlift Source",p)); '
                'assert(edit.city_create(p,b,"Airlift Destination",p))',
                'lua unsafe-cmd local p=find.player("AgentPlace1"); '
                'local s=nil; local d=nil; for c in p:cities_iterate() do '
                'if c.name=="Airlift Source" then s=c elseif '
                'c.name=="Airlift Destination" then d=c end end; '
                'assert(s and d); local a=find.building_type("Airport"); '
                's:create_building(a); d:create_building(a); '
                'p:create_unit(s.tile,find.unit_type("Warriors"),0,s,-1); '
                'local para=find.unit_type("Paratroopers"); '
                'p:create_unit(s.tile,para,0,s,-1); s.tile:show(p); '
                'd.tile:show(p); for t in whole_map_iterate() do if '
                't.id~=s.tile.id and s.tile:sq_distance(t)<=100 and '
                'para:can_exist_at_tile(t) and t:city()==nil and '
                't:num_units()==0 then t:show(p) end end',
            ])

            deadline = time.monotonic() + 15
            while True:
                cities = self._pages(
                    root, token, "state", "section=cities&limit=16",
                    public_payloads,
                )
                units = self._pages(
                    root, token, "state", "section=units&limit=16",
                    public_payloads,
                )
                names = {city["name"] for city in cities}
                types = {unit["type"] for unit in units}
                if {
                    "Airlift Source", "Airlift Destination",
                } <= names and {"Warriors", "Paratroopers"} <= types:
                    break
                self.assertLess(
                    time.monotonic(), deadline,
                    (cities, units, game.server_output_tail),
                )
                time.sleep(0.05)

            phase_end = next(
                action for action in self._pages(
                    root, token, "legal-actions", "limit=16",
                    public_payloads,
                ) if action["subject"]["operation"] == "end"
            )
            phase_batch = self._batch(
                game_id, joined, phase_end, "e2e.mobility.next-turn",
            )
            phase_receipt = request_json(
                "POST", f"{root}/batches", token=token, body=phase_batch,
            )
            public_payloads.append(phase_receipt)
            # PLAYER-phase rollover can outrun the phase-end postcondition in
            # this edit-heavy fixture. Its turn transition, not that receipt,
            # is the setup gate for resetting city airlift counters.
            self.assertIn(
                phase_receipt["receipt_state"], {"applied", "ambiguous"},
                phase_receipt,
            )
            timeout_fallback = phase_receipt["receipt_state"] == "ambiguous"
            if timeout_fallback:
                # The isolated fixture may invalidate the normal phase-end
                # acknowledgement while its Lua edits are still settling.
                # A one-second native timeout advances only this disposable
                # game; restore infinite timeout as soon as turn 2 begins.
                game._send_timeout(1)
            self._wait_overview(
                root, token, public_payloads,
                lambda value: (
                    value["turn"] > initial["turn"] and value["active_phase"]
                ),
            )
            if timeout_fallback:
                game._send_timeout(0)

            cities = self._pages(
                root, token, "state", "section=cities&limit=16",
                public_payloads,
            )
            source = next(
                city for city in cities if city["name"] == "Airlift Source"
            )
            destination = next(
                city for city in cities
                if city["name"] == "Airlift Destination"
            )
            self.assertGreaterEqual(source["airlift"]["remaining"], 1)
            self.assertGreaterEqual(destination["airlift"]["remaining"], 1)
            units = [
                unit for unit in self._pages(
                    root, token, "state", "section=units&limit=16",
                    public_payloads,
                ) if unit["scope"] == "own"
            ]
            warrior = next(
                unit for unit in units
                if unit["type"] == "Warriors"
                and unit["tile_id"] == source["tile_id"]
            )
            paratrooper = next(
                unit for unit in units
                if unit["type"] == "Paratroopers"
                and unit["tile_id"] == source["tile_id"]
            )

            warrior_actions = self._pages(
                root, token, "legal-actions",
                f"actor_id={quote(warrior['id'])}&limit=16",
                public_payloads,
            )
            airlift = next(
                action for action in warrior_actions
                if action["subject"]["operation"] == "airlift"
                and action["subject"]["target"]["id"] == destination["id"]
            )
            self.assertEqual(airlift["kind"], "unit.perform_action")
            self.assertEqual(
                airlift["subject"]["target"]["tile_id"],
                destination["tile_id"],
            )
            airlift_batch = self._batch(
                game_id, joined, airlift, "e2e.mobility.airlift",
            )
            airlift_receipt = request_json(
                "POST", f"{root}/batches", token=token, body=airlift_batch,
            )
            public_payloads.append(airlift_receipt)
            self.assertEqual(
                airlift_receipt["receipt_state"], "applied", airlift_receipt,
            )
            self.assertFalse(airlift_receipt["idempotent"])
            self._assert_durable_idempotent(
                root, token, airlift_batch, "e2e.mobility.airlift",
                airlift_receipt, public_payloads,
            )
            moved_warrior = next(
                unit for unit in self._pages(
                    root, token, "state", "section=units&limit=16",
                    public_payloads,
                ) if unit["id"] == warrior["id"]
            )
            self.assertEqual(moved_warrior["tile_id"], destination["tile_id"])

            # Map-sized relocation catalogs are intentionally absent from the
            # actor scope. Select an observed normal-client tile, then request
            # the exact actor/target catalog used for execution.
            known_tiles = self._pages(
                root, token, "state", "section=known_tiles&limit=64",
                public_payloads,
            )
            paradrop = None
            target_page_items = []
            for tile in known_tiles:
                if (
                    tile["id"] == paratrooper["tile_id"]
                    or tile["visibility"] != "visible"
                ):
                    continue
                target_page_items = self._pages(
                    root, token, "legal-actions",
                    f"actor_id={quote(paratrooper['id'])}"
                    f"&target_id={quote(tile['id'])}&limit=16",
                    public_payloads,
                )
                paradrop = next(
                    (
                        action for action in target_page_items
                        if action["subject"]["operation"] == "paradrop"
                        and action["subject"]["probability"]["kind"]
                        == "not_implemented"
                    ),
                    None,
                )
                if paradrop is not None:
                    break
            self.assertIsNotNone(paradrop, target_page_items)
            self.assertEqual(
                paradrop["subject"]["probability"]["kind"],
                "not_implemented",
            )
            target = paradrop["subject"]["target"]
            self.assertEqual(target["type"], "tile")
            paradrop_batch = self._batch(
                game_id, joined, paradrop, "e2e.mobility.paradrop",
            )
            paradrop_receipt = request_json(
                "POST", f"{root}/batches", token=token,
                body=paradrop_batch,
            )
            public_payloads.append(paradrop_receipt)
            self.assertEqual(
                paradrop_receipt["receipt_state"], "applied",
                paradrop_receipt,
            )
            self.assertFalse(paradrop_receipt["idempotent"])
            self._assert_durable_idempotent(
                root, token, paradrop_batch, "e2e.mobility.paradrop",
                paradrop_receipt, public_payloads,
            )
            dropped = next(
                unit for unit in self._pages(
                    root, token, "state", "section=units&limit=16",
                    public_payloads,
                ) if unit["id"] == paratrooper["id"]
            )
            self.assertEqual(dropped["tile_id"], target["id"])
            self.assertTrue(dropped["paradrop"]["used_this_turn"])

            public = json.dumps(public_payloads, sort_keys=True)
            self.assertIsNone(re.search(r"[pcu]:[0-9]+:[0-9]+", public))
            for private in (
                "Airlift Unit", "Paradrop Unit", "Teleport Enter",
                '"source_city"', '"destination_city"', '"lifecycle_id"',
                '"slot"',
            ):
                self.assertNotIn(private, public)
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
