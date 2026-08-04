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
    "set FREECIV_AGENT_E2E=1 for the isolated unit orders HTTP smoke",
)
class V2UnitOrdersRealE2ETests(unittest.TestCase):
    """Queue and cancel a real target-on-demand goto through public v2."""

    @staticmethod
    def _batch(game_id, joined, action, batch_id):
        return {
            "schema_version": 2,
            "control_protocol": "full-control-v2",
            "game_id": game_id,
            "agent_id": joined["agent_id"],
            "batch_id": batch_id,
            "state_revision": action["state_revision"],
            "commands": [{"action_id": action["action_id"], "arguments": {}}],
        }

    def _pages(self, root, token, endpoint, query, public_payloads):
        page = request_json("GET", f"{root}/{endpoint}?{query}", token=token)
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

    def _wait_units(self, root, token, public_payloads, predicate, timeout=20):
        deadline = time.monotonic() + timeout
        units = []
        while True:
            try:
                units = self._pages(
                    root, token, "state", "section=units&limit=16",
                    public_payloads,
                )
                if predicate(units):
                    return units
            except ClientError as exc:
                if exc.status != 503:
                    raise
            self.assertLess(time.monotonic(), deadline, units)
            time.sleep(0.05)

    def _execute(self, root, token, game_id, joined, action, batch_id,
                 public_payloads):
        batch = self._batch(game_id, joined, action, batch_id)
        receipt = request_json(
            "POST", f"{root}/batches", token=token, body=batch,
        )
        public_payloads.append(receipt)
        self.assertEqual(receipt["receipt_state"], "applied", receipt)
        self.assertFalse(receipt["idempotent"])
        durable = request_json(
            "GET", f"{root}/receipts/{quote(batch_id)}", token=token,
        )
        public_payloads.append(durable)
        self.assertEqual(durable, receipt)
        replay = request_json(
            "POST", f"{root}/batches", token=token, body=batch,
        )
        public_payloads.append(replay)
        self.assertTrue(replay["idempotent"])
        self.assertEqual(
            {key: value for key, value in replay.items() if key != "idempotent"},
            {key: value for key, value in receipt.items() if key != "idempotent"},
        )

    def test_real_classic_arbitrary_known_target_goto_then_cancel_orders(self):
        repository = Path(__file__).parents[2]
        server_binary = repository / "build-agent" / "freeciv-server"
        agent_binary = repository / "build-control-v2" / "freeciv-agent"
        self.assertTrue(os.access(server_binary, os.X_OK))
        self.assertTrue(os.access(agent_binary, os.X_OK))

        temporary = tempfile.TemporaryDirectory(prefix="freeciv-v2-orders-e2e-")
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
                temporary_path / "runs", "isolated-orders-admin",
                binary=server_binary, agent_binary=agent_binary,
            )
            server = make_supervisor_server(supervisor, "127.0.0.1", 0)
            server_thread = threading.Thread(
                target=server.serve_forever,
                name="isolated-v2-orders-http", daemon=True,
            )
            server_thread.start()
            created = create_game(
                supervisor.service_url, "isolated-orders-admin", {
                    "mode": "single", "places": 2, "turns": 3,
                    "seed": 1201, "ruleset": "classic",
                    "objective": "Exercise target goto and cancel orders.",
                    "timing_mode": "infinite", "action_timeout_s": None,
                    "lobby_timeout_s": 30, "frame_interval": 0,
                    "frame_zoom": 1, "control_protocol": "full-control-v2",
                },
            )
            public_payloads.append(created)
            game = supervisor.game(created["game_id"])
            with game.condition:
                topology_sequence = game.server_output_sequence
            game._send_commands(['set topology ""'])
            topology_ack = game._wait_for_server_output(
                topology_sequence,
                lambda line: (
                    "Console: 'topology' has been set to empty value." in line
                ),
                "rectangular topology acknowledgement",
            )
            self.assertIn("empty value", topology_ack)
            with game.condition:
                wrap_sequence = game.server_output_sequence
            game._send_commands(["set wrap WRAPX"])
            wrap_ack = game._wait_for_server_output(
                wrap_sequence,
                lambda line: (
                    "Console: 'wrap' has been set to " in line
                    and "Wrap East-West" in line
                ),
                "WRAP_X acknowledgement",
            )
            self.assertIn("Wrap East-West", wrap_ack)
            game._send_commands(["set startunits c", "set dispersion 10"])
            joined = join_game(
                supervisor.service_url, created["game_id"],
                created["join_token"], controller_label="codex-orders-e2e",
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

            # Isolated setup only. All gameplay below goes through public v2
            # and the normal Freeciv client goto/clear-order request paths.
            game._send_commands([
                'lua unsafe-cmd local p=find.player("AgentPlace1"); '
                'local t=nil; for u in p:units_iterate() do t=u.tile; break end; '
                'assert(t); p:create_unit(t,find.unit_type("Explorer"),'
                '0,nil,0); for place in whole_map_iterate() do '
                'place:remove_extra("Hut"); place:show(p) end',
            ])
            units = self._wait_units(
                root, token, public_payloads,
                lambda values: any(
                    item["scope"] == "own" and item["type"] == "Explorer"
                    and item["moves"] == 0
                    for item in values
                ),
            )
            explorer = next(
                item for item in units
                if item["scope"] == "own" and item["type"] == "Explorer"
                and item["moves"] == 0
            )
            self.assertEqual(explorer["automation"], {
                "controller": "none", "has_orders": False,
            })
            self.assertEqual(explorer["activity"]["name"], "idle")
            self.assertEqual(explorer["activity"]["progress"], 0)
            self.assertIsNone(explorer["activity"]["target"])
            self.assertEqual(explorer["transport"]["state"], "untransported")
            self.assertEqual(explorer["transport"]["occupied"], 0)

            known_tiles = self._pages(
                root, token, "state", "section=known_tiles&limit=16",
                public_payloads,
            )

            actions = self._pages(
                root, token, "legal-actions",
                f"actor_id={quote(explorer['id'])}&limit=16", public_payloads,
            )
            gotos = [
                action for action in actions
                if action["subject"]["operation"] == "goto"
            ]
            self.assertGreater(len(gotos), 0)
            self.assertLessEqual(len(gotos), 64)
            for action in gotos:
                self.assertEqual(action["kind"], "unit.order")
                self.assertEqual(
                    set(action["subject"]["target"]), {"type", "id", "x", "y"},
                )
                self.assertEqual(action["arguments_schema"], {
                    "type": "object", "properties": {},
                    "additionalProperties": False,
                })
            bounded_targets = {
                action["subject"]["target"]["id"] for action in gotos
            }
            width = max(tile["x"] for tile in known_tiles) + 1
            def wrapped_span(value, origin, size):
                delta = abs(value - origin)
                return min(delta, size - delta)

            # The isolated setup above authoritatively acknowledged overhead
            # rectangular topology with WRAP_X only. For that map,
            # real_map_distance is max(wrapped dx, absolute dy); this mirrors
            # common/map.c's map_distance_vector + real-distance definition.
            def real_distance(tile):
                return max(
                    wrapped_span(tile["x"], explorer["x"], width),
                    abs(tile["y"] - explorer["y"]),
                )

            candidates = sorted(
                (
                    tile for tile in known_tiles
                    if tile["visibility"] in {"remembered", "visible"}
                    and tile["id"] != explorer["tile_id"]
                    and tile["id"] not in bounded_targets
                    and real_distance(tile) > 8
                ),
                key=real_distance,
            )
            self.assertGreater(len(candidates), 0)
            goto = None
            chosen_target = None
            for target in candidates:
                target_page = request_json(
                    "GET",
                    f"{root}/legal-actions?actor_id={quote(explorer['id'])}"
                    f"&target_id={quote(target['id'])}",
                    token=token,
                )
                public_payloads.append(target_page)
                self.assertIn(target_page["page"]["total_items"], {0, 1})
                self.assertIsNone(target_page["page"]["next_cursor"])
                if target_page["page"]["items"]:
                    goto = target_page["page"]["items"][0]
                    chosen_target = target
                    break
            self.assertIsNotNone(goto, "no reachable known target beyond radius 8")
            self.assertIsNotNone(chosen_target)
            self.assertGreater(real_distance(chosen_target), 8)
            self.assertNotIn(chosen_target["id"], bounded_targets)
            self.assertEqual(goto["kind"], "unit.order")
            self.assertEqual(goto["subject"]["operation"], "goto")
            self.assertEqual(goto["subject"]["actor"], {
                "type": "unit", "id": explorer["id"],
            })
            self.assertEqual(goto["subject"]["target"]["id"], chosen_target["id"])
            self.assertEqual(goto["arguments_schema"], {
                "type": "object", "properties": {},
                "additionalProperties": False,
            })
            self._execute(
                root, token, game_id, joined, goto,
                "e2e.orders.goto", public_payloads,
            )
            units = self._wait_units(
                root, token, public_payloads,
                lambda values: any(
                    item["id"] == explorer["id"]
                    and item["automation"]["has_orders"]
                    for item in values
                ),
            )
            queued = next(item for item in units if item["id"] == explorer["id"])
            self.assertEqual(queued["automation"]["controller"], "none")
            self.assertEqual(queued["activity"]["name"], "idle")
            self.assertIsNone(queued["activity"]["target"])

            queued_actions = self._pages(
                root, token, "legal-actions",
                f"actor_id={quote(queued['id'])}&limit=16", public_payloads,
            )
            cancel = next(
                action for action in queued_actions
                if action["subject"]["operation"] == "cancel_orders"
            )
            self._execute(
                root, token, game_id, joined, cancel,
                "e2e.orders.cancel", public_payloads,
            )
            final_units = self._wait_units(
                root, token, public_payloads,
                lambda values: any(
                    item["id"] == explorer["id"]
                    and not item["automation"]["has_orders"]
                    and item["automation"]["controller"] == "none"
                    and item["activity"]["name"] == "idle"
                    and item["activity"]["target"] is None
                    for item in values
                ),
            )
            final = next(
                item for item in final_units if item["id"] == explorer["id"]
            )
            self.assertEqual(final["automation"], {
                "controller": "none", "has_orders": False,
            })

            public = json.dumps(public_payloads, sort_keys=True)
            self.assertIsNone(re.search(r"[pcu]:[0-9]+:[0-9]+", public))
            for private in (
                "native_target_tile", "source_unit_tile", "route_signature",
                "order_count", "goto_tile", "src_tile", "dest_tile",
                "lifecycle_id", "first_request_id", "request_count", '"slot"',
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
                                (f"freeciv-agent-place-{place}", sidecar._process)
                                for place, sidecar in cleanup_game.sidecars.items()
                                if sidecar._process is not None
                            )
                except BaseException as exc:
                    cleanup_failures.append(f"native process capture: {exc!r}")
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
