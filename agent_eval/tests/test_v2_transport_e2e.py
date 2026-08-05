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
    "set FREECIV_AGENT_E2E=1 for the isolated transport HTTP smoke",
)
class V2TransportRealE2ETests(unittest.TestCase):
    """Exercise a real Classic transport action in disposable processes."""

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

    def test_real_classic_embark_and_exact_transport_state(self):
        repository = Path(__file__).parents[2]
        server_binary = repository / "build-agent" / "freeciv-server"
        agent_binary = repository / "build-control-v2" / "freeciv-agent"
        self.assertTrue(os.access(server_binary, os.X_OK))
        self.assertTrue(os.access(agent_binary, os.X_OK))

        temporary = tempfile.TemporaryDirectory(
            prefix="freeciv-v2-transport-e2e-",
        )
        temporary_path = Path(temporary.name)
        supervisor = None
        server = None
        server_thread = None
        game_id = None
        public_payloads = []
        native_processes = []
        body_error = None
        cleanup_failures = []
        try:
            supervisor = Supervisor(
                temporary_path / "runs",
                "isolated-transport-admin",
                binary=server_binary,
                agent_binary=agent_binary,
            )
            server = make_supervisor_server(supervisor, "127.0.0.1", 0)
            server_thread = threading.Thread(
                target=server.serve_forever,
                name="isolated-v2-transport-http",
                daemon=True,
            )
            server_thread.start()
            created = create_game(
                supervisor.service_url,
                "isolated-transport-admin",
                {
                    "mode": "single",
                    "places": 2,
                    "turns": 3,
                    "seed": 731,
                    "ruleset": "classic",
                    "objective": "Exercise an exact owned transport link.",
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
            # This is an isolated pregame fixture, before the only human seat
            # joins.  It gives each player one coastal ferry and land cargo.
            game._send_commands([
                "set startunits cfdd",
                "set dispersion 0",
                "set landmass 20",
            ])
            joined = join_game(
                supervisor.service_url,
                created["game_id"],
                created["join_token"],
                controller_label="codex-gpt-5.6-sol",
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
                item for item in self._pages(
                    root, token, "state", "section=units&limit=16",
                    public_payloads,
                ) if item["scope"] == "own"
            ]
            transporter = next(
                item for item in units if item["type"] == "Trireme"
            )
            self.assertEqual(transporter["transport"], {
                "capacity": 2,
                "occupied": 0,
                "state": "untransported",
                "transporter_unit_id": None,
            })
            catalogs = {
                item["id"]: self._pages(
                    root, token, "legal-actions",
                    f"actor_id={quote(item['id'])}&limit=16",
                    public_payloads,
                ) for item in units
            }
            all_operations = {
                action["subject"]["operation"]
                for actions in catalogs.values() for action in actions
            }
            # Classic has no Transport Load action rule.  Its absence is a
            # ruleset result rather than an emulated fallback.
            self.assertNotIn("load", all_operations)
            cargo, embark = next(
                (item, action)
                for item in units
                for action in catalogs[item["id"]]
                if action["subject"]["operation"] == "embark"
            )
            self.assertEqual(embark["kind"], "unit.perform_action")
            self.assertEqual(
                embark["subject"]["target"],
                {"type": "unit", "id": transporter["id"]},
            )
            self.assertEqual(embark["subject"]["probability"], {
                "kind": "exact",
                "minimum_percent": 100.0,
                "maximum_percent": 100.0,
            })

            stale_scope = request_json(
                "GET",
                f"{root}/legal-actions?actor_id={quote(cargo['id'])}&limit=1",
                token=token,
            )
            public_payloads.append(stale_scope)
            stale_cursor = stale_scope["page"]["next_cursor"]
            self.assertIsNotNone(stale_cursor)

            embark_batch = self._batch(
                game_id, joined, embark, "e2e.transport.embark",
            )
            receipt = request_json(
                "POST", f"{root}/batches", token=token,
                body=embark_batch,
            )
            public_payloads.append(receipt)
            self.assertEqual(receipt["receipt_state"], "applied", receipt)
            self.assertFalse(receipt["idempotent"])
            self.assertIsNone(receipt["error"])
            durable = request_json(
                "GET",
                f"{root}/receipts/{quote('e2e.transport.embark')}",
                token=token,
            )
            public_payloads.append(durable)
            self.assertEqual(durable, receipt)
            repeated = request_json(
                "POST", f"{root}/batches", token=token, body=embark_batch,
            )
            public_payloads.append(repeated)
            self.assertTrue(repeated["idempotent"])
            self.assertEqual(
                {key: value for key, value in repeated.items()
                 if key != "idempotent"},
                {key: value for key, value in receipt.items()
                 if key != "idempotent"},
            )
            with self.assertRaises(ClientError) as stale_action:
                request_json(
                    "POST", f"{root}/batches", token=token,
                    body=self._batch(
                        game_id, joined, embark,
                        "e2e.transport.stale-embark",
                    ),
                )
            self.assertEqual(stale_action.exception.status, 409)
            with self.assertRaises(ClientError) as stale_page:
                request_json(
                    "GET",
                    f"{root}/legal-actions?cursor={quote(stale_cursor)}",
                    token=token,
                )
            self.assertEqual(stale_page.exception.status, 409)

            after = [
                item for item in self._pages(
                    root, token, "state", "section=units&limit=16",
                    public_payloads,
                ) if item["scope"] == "own"
            ]
            carried = next(item for item in after if item["id"] == cargo["id"])
            current_transporter = next(
                item for item in after if item["id"] == transporter["id"]
            )
            self.assertEqual(carried["transport"], {
                "capacity": 0,
                "occupied": 0,
                "state": "transported",
                "transporter_unit_id": transporter["id"],
            })
            self.assertEqual(current_transporter["transport"]["occupied"], 1)
            self.assertEqual(
                (carried["tile_id"], carried["x"], carried["y"]),
                (
                    current_transporter["tile_id"],
                    current_transporter["x"],
                    current_transporter["y"],
                ),
            )

            carried_actions = self._pages(
                root, token, "legal-actions",
                f"actor_id={quote(carried['id'])}&limit=16",
                public_payloads,
            )
            cancel = next(
                action for action in carried_actions
                if action["subject"]["operation"] == "cancel_activity"
            )
            cancel_receipt = request_json(
                "POST", f"{root}/batches", token=token,
                body=self._batch(
                    game_id, joined, cancel, "e2e.transport.cancel-sentry",
                ),
            )
            public_payloads.append(cancel_receipt)
            self.assertEqual(cancel_receipt["receipt_state"], "applied")

            phase_end = next(
                action for action in self._pages(
                    root, token, "legal-actions", "limit=16",
                    public_payloads,
                ) if action["subject"]["operation"] == "end"
            )
            phase_receipt = request_json(
                "POST", f"{root}/batches", token=token,
                body=self._batch(
                    game_id, joined, phase_end, "e2e.transport.next-turn",
                ),
            )
            public_payloads.append(phase_receipt)
            self.assertEqual(phase_receipt["receipt_state"], "applied")

            deadline = time.monotonic() + 30
            while True:
                try:
                    overview = self._pages(
                        root, token, "state", "section=overview&limit=16",
                        public_payloads,
                    )[0]
                    if overview["turn"] >= 2 and overview["active_phase"]:
                        break
                except ClientError as exc:
                    if exc.status != 503:
                        raise
                self.assertLess(time.monotonic(), deadline)
                time.sleep(0.05)

            disembark_actions = [
                action for action in self._pages(
                    root, token, "legal-actions",
                    f"actor_id={quote(carried['id'])}&limit=16",
                    public_payloads,
                ) if action["subject"]["operation"] == "disembark"
            ]
            self.assertGreaterEqual(len(disembark_actions), 1)
            disembark = disembark_actions[0]
            self.assertEqual(disembark["subject"]["target"]["type"], "tile")
            self.assertEqual(disembark["subject"]["transport_context"], {
                "type": "unit", "id": transporter["id"],
            })
            self.assertEqual(disembark["subject"]["probability"], {
                "kind": "exact",
                "minimum_percent": 100.0,
                "maximum_percent": 100.0,
            })
            transport_origin = (
                current_transporter["tile_id"],
                current_transporter["x"],
                current_transporter["y"],
            )
            disembark_receipt = request_json(
                "POST", f"{root}/batches", token=token,
                body=self._batch(
                    game_id, joined, disembark,
                    "e2e.transport.disembark",
                ),
            )
            public_payloads.append(disembark_receipt)
            self.assertEqual(
                disembark_receipt["receipt_state"], "applied",
                disembark_receipt,
            )
            detached = [
                item for item in self._pages(
                    root, token, "state", "section=units&limit=16",
                    public_payloads,
                ) if item["scope"] == "own"
            ]
            detached_cargo = next(
                item for item in detached if item["id"] == cargo["id"]
            )
            stationary_transporter = next(
                item for item in detached if item["id"] == transporter["id"]
            )
            self.assertEqual(detached_cargo["transport"], {
                "capacity": 0,
                "occupied": 0,
                "state": "untransported",
                "transporter_unit_id": None,
            })
            self.assertEqual(
                stationary_transporter["transport"]["occupied"], 0,
            )
            self.assertEqual(
                (
                    stationary_transporter["tile_id"],
                    stationary_transporter["x"],
                    stationary_transporter["y"],
                ),
                transport_origin,
            )
            self.assertEqual(
                (
                    detached_cargo["tile_id"],
                    detached_cargo["x"],
                    detached_cargo["y"],
                ),
                (
                    disembark["subject"]["target"]["id"],
                    disembark["subject"]["target"]["x"],
                    disembark["subject"]["target"]["y"],
                ),
            )

            public = json.dumps(public_payloads, sort_keys=True)
            self.assertIsNone(re.search(r"[pcu]:[0-9]+:[0-9]+", public))
            for private in (
                "Transport Board", "Transport Deboard", "Transport Embark",
                "Transport Disembark", "Transport Load", "Transport Unload",
                '"target_unit_ref"', '"transport_context_ref"',
                '"lifecycle_id"', '"slot"',
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
                                for place, sidecar in cleanup_game.sidecars.items()
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
            failure = self.failureException(
                "isolated teardown failed: " + "; ".join(cleanup_failures),
            )
            if body_error is not None:
                failure.add_note(f"test body also failed: {body_error!r}")
            raise failure
        if body_error is not None:
            raise body_error.with_traceback(body_error.__traceback__)


if __name__ == "__main__":
    unittest.main()
