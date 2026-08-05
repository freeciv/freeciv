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
    "set FREECIV_AGENT_E2E=1 for the isolated unit automation HTTP smoke",
)
class V2UnitAutomationRealE2ETests(unittest.TestCase):
    """Exercise native autowork, autoexplore, and grouped cancel via v2."""

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
        return receipt

    def test_real_classic_unit_automation_and_grouped_cancel(self):
        repository = Path(__file__).parents[2]
        server_binary = repository / "build-agent" / "freeciv-server"
        agent_binary = repository / "build-control-v2" / "freeciv-agent"
        self.assertTrue(os.access(server_binary, os.X_OK))
        self.assertTrue(os.access(agent_binary, os.X_OK))

        temporary = tempfile.TemporaryDirectory(prefix="freeciv-v2-auto-e2e-")
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
                temporary_path / "runs", "isolated-auto-admin",
                binary=server_binary, agent_binary=agent_binary,
            )
            server = make_supervisor_server(supervisor, "127.0.0.1", 0)
            server_thread = threading.Thread(
                target=server.serve_forever,
                name="isolated-v2-auto-http", daemon=True,
            )
            server_thread.start()
            created = create_game(
                supervisor.service_url, "isolated-auto-admin", {
                    "mode": "single", "places": 2, "turns": 3,
                    "seed": 991, "ruleset": "classic",
                    "objective": "Exercise exact unit automation.",
                    "timing_mode": "infinite", "action_timeout_s": None,
                    "lobby_timeout_s": 30, "frame_interval": 0,
                    "frame_zoom": 1, "control_protocol": "full-control-v2",
                },
            )
            public_payloads.append(created)
            game = supervisor.game(created["game_id"])
            game._send_commands(["set startunits c", "set dispersion 10"])
            joined = join_game(
                supervisor.service_url, created["game_id"],
                created["join_token"], controller_label="codex-auto-e2e",
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

            # Test-only setup: gameplay control below uses only authenticated
            # public v2 capabilities and normal Freeciv client requests.
            game._send_commands([
                'lua unsafe-cmd local p=find.player("AgentPlace1"); '
                'local t=nil; for u in p:units_iterate() do t=u.tile; break end; '
                'assert(t); p:create_unit(t,find.unit_type("Workers"),0,nil,0); '
                'p:create_unit(t,find.unit_type("Explorer"),0,nil,0); t:show(p)',
            ])
            units = self._wait_units(
                root, token, public_payloads,
                lambda values: {"Workers", "Explorer"} <= {
                    item["type"] for item in values if item["scope"] == "own"
                },
            )
            worker = next(
                item for item in units
                if item["scope"] == "own" and item["type"] == "Workers"
            )
            explorer = next(
                item for item in units
                if item["scope"] == "own" and item["type"] == "Explorer"
            )
            self.assertEqual(worker["automation"]["controller"], "none")
            self.assertEqual(explorer["moves"], 0)

            worker_actions = self._pages(
                root, token, "legal-actions",
                f"actor_id={quote(worker['id'])}&limit=16", public_payloads,
            )
            auto_work = next(
                action for action in worker_actions
                if action["subject"]["operation"] == "auto_work"
            )
            self.assertEqual(auto_work["kind"], "unit.order")
            self._execute(
                root, token, game_id, joined, auto_work,
                "e2e.automation.work", public_payloads,
            )
            units = self._wait_units(
                root, token, public_payloads,
                lambda values: any(
                    item["id"] == worker["id"]
                    and item["automation"]["controller"] == "auto_work"
                    for item in values
                ),
            )
            worker = next(item for item in units if item["id"] == worker["id"])
            worker_cancel_actions = self._pages(
                root, token, "legal-actions",
                f"actor_id={quote(worker['id'])}&limit=16", public_payloads,
            )
            self.assertEqual(
                {
                    action["subject"]["operation"]
                    for action in worker_cancel_actions
                },
                {"cancel_automation"},
            )
            self._execute(
                root, token, game_id, joined, worker_cancel_actions[0],
                "e2e.automation.work-cancel", public_payloads,
            )
            self._wait_units(
                root, token, public_payloads,
                lambda values: any(
                    item["id"] == worker["id"]
                    and item["automation"]["controller"] == "none"
                    and item["activity"]["name"] == "idle"
                    and item["activity"]["target"] is None
                    for item in values
                ),
            )

            explorer_actions = self._pages(
                root, token, "legal-actions",
                f"actor_id={quote(explorer['id'])}&limit=16", public_payloads,
            )
            auto_explore = next(
                action for action in explorer_actions
                if action["subject"]["operation"] == "auto_explore"
            )
            self.assertEqual(auto_explore["kind"], "unit.order")
            self._execute(
                root, token, game_id, joined, auto_explore,
                "e2e.automation.explore", public_payloads,
            )
            units = self._wait_units(
                root, token, public_payloads,
                lambda values: any(
                    item["id"] == explorer["id"]
                    and item["automation"]["controller"] == "auto_explore"
                    and item["activity"]["name"] == "explore"
                    for item in values
                ),
            )
            explorer = next(item for item in units if item["id"] == explorer["id"])
            cancel_actions = self._pages(
                root, token, "legal-actions",
                f"actor_id={quote(explorer['id'])}&limit=16", public_payloads,
            )
            self.assertEqual(
                {action["subject"]["operation"] for action in cancel_actions},
                {"cancel_automation"},
            )
            cancel = cancel_actions[0]
            self._execute(
                root, token, game_id, joined, cancel,
                "e2e.automation.cancel", public_payloads,
            )
            final_units = self._wait_units(
                root, token, public_payloads,
                lambda values: any(
                    item["id"] == explorer["id"]
                    and item["automation"]["controller"] == "none"
                    and item["activity"]["name"] == "idle"
                    and item["activity"]["target"] is None
                    for item in values
                ),
            )
            final_explorer = next(
                item for item in final_units if item["id"] == explorer["id"]
            )
            self.assertFalse(final_explorer["automation"]["has_orders"])

            # Prove the full UNIT_INFO latch, not merely the final-state
            # fallback: on a fully known map a fresh Explorer briefly enters
            # auto-explore, finds no unknown destination, and returns to
            # manual idle before request processing finishes.
            existing_unit_ids = {item["id"] for item in final_units}
            game._send_commands([
                'lua unsafe-cmd local p=find.player("AgentPlace1"); '
                'local t=nil; for u in p:units_iterate() do t=u.tile; break end; '
                'assert(t); p:create_unit(t,find.unit_type("Explorer"),'
                '0,nil,-1); for place in whole_map_iterate() do '
                'place:remove_extra("Hut"); place:show(p) end',
            ])
            latch_units = self._wait_units(
                root, token, public_payloads,
                lambda values: any(
                    item["id"] not in existing_unit_ids
                    and item["scope"] == "own"
                    and item["type"] == "Explorer"
                    and item["moves"] > 0
                    for item in values
                ),
            )
            latch_explorer = next(
                item for item in latch_units
                if item["id"] not in existing_unit_ids
                and item["scope"] == "own"
                and item["type"] == "Explorer"
            )
            latch_actions = self._pages(
                root, token, "legal-actions",
                f"actor_id={quote(latch_explorer['id'])}&limit=16",
                public_payloads,
            )
            latch_auto_explore = next(
                action for action in latch_actions
                if action["subject"]["operation"] == "auto_explore"
            )
            self._execute(
                root, token, game_id, joined, latch_auto_explore,
                "e2e.automation.explore-latch", public_payloads,
            )
            latch_final_units = self._wait_units(
                root, token, public_payloads,
                lambda values: any(
                    item["id"] == latch_explorer["id"]
                    and item["automation"]["controller"] == "none"
                    and item["activity"]["name"] == "idle"
                    and item["activity"]["target"] is None
                    for item in values
                ),
                timeout=3,
            )
            latch_final = next(
                item for item in latch_final_units
                if item["id"] == latch_explorer["id"]
            )
            self.assertEqual(latch_final["automation"]["controller"], "none")

            public = json.dumps(public_payloads, sort_keys=True)
            self.assertIsNone(re.search(r"[pcu]:[0-9]+:[0-9]+", public))
            for private in (
                "SSA_AUTOEXPLORE", "SSA_AUTOWORKER", "lifecycle_id",
                "first_request_id", "request_count", '"slot"',
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
                cleanup_failures.append(f"temporary root remained: {temporary_path}")

        if cleanup_failures:
            cleanup_error = self.failureException(
                "isolated teardown failed: " + "; ".join(cleanup_failures),
            )
            if body_error is not None:
                raise cleanup_error from body_error
            raise cleanup_error
        if body_error is not None:
            raise body_error
