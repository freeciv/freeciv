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

from agent_eval.client import ClientError, create_game, join_game, request_json
from agent_eval.supervisor import Supervisor, make_supervisor_server


@unittest.skipUnless(
    os.environ.get("FREECIV_AGENT_E2E") == "1",
    "set FREECIV_AGENT_E2E=1 for the isolated government HTTP smoke",
)
class V2GovernmentRealE2ETests(unittest.TestCase):
    """Exercise government transitions through disposable native processes.

    The immediate assertions cover the safety-critical boundaries: the
    revolution and target-choice packets must each have an observed native
    postcondition before their receipts become ``applied``.  The final
    enactment assertion advances real phases until Classic's deterministic
    revolution timer expires; it does not assume a particular random duration.
    """

    _OPAQUE_ACTION_ID = re.compile(r"action_[0-9a-f]{32}\Z")
    _OPAQUE_ID = re.compile(
        r"(?:activity|city|extra|government|player|production|tech|tile|unit)"
        r"_[0-9a-f]{32}\Z",
    )
    _PUBLIC_ID_FIELDS = frozenset({
        "actor_id", "current_id", "during_revolution_id", "goal_id", "id",
        "owner_player_id", "player_id", "target_id", "tile_id",
    })
    _NATIVE_REF = re.compile(
        r"(?<![A-Za-z0-9_])[pcu]:[0-9]+:[0-9]+(?![A-Za-z0-9_])",
    )
    _NATIVE_SLOT = re.compile(
        r"(?<![A-Za-z0-9_])a[0-9a-fA-F]{16}(?![A-Za-z0-9_])",
    )

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
            with urllib.request.urlopen(request, timeout=60) as response:
                return (
                    response.status,
                    json.loads(response.read().decode("utf-8")),
                )
        except urllib.error.HTTPError as exc:
            with exc:
                return exc.code, json.loads(exc.read().decode("utf-8"))

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

    def _state_section(self, root, token, section, public_payloads):
        page = request_json(
            "GET", f"{root}/state?section={section}&limit=1", token=token,
        )
        public_payloads.append(page)
        self.assertEqual(page["control_protocol"], "full-control-v2")
        self.assertEqual(page["page"]["section"], section)
        items = list(page["page"]["items"])
        total = page["page"]["total_items"]
        cursor = page["page"]["next_cursor"]
        while cursor is not None:
            page = request_json(
                "GET", f"{root}/state?cursor={quote(cursor)}", token=token,
            )
            public_payloads.append(page)
            self.assertEqual(page["page"]["section"], section)
            items.extend(page["page"]["items"])
            cursor = page["page"]["next_cursor"]
        self.assertEqual(len(items), total)
        return items

    def _overview(self, root, token, public_payloads):
        items = self._state_section(
            root, token, "overview", public_payloads,
        )
        self.assertEqual(len(items), 1)
        return items[0]

    def _wait_overview(self, root, token, public_payloads, deadline):
        """Wait through the sidecar handoff between native player phases."""
        last_error = None
        while time.monotonic() < deadline:
            try:
                return self._overview(root, token, public_payloads)
            except ClientError as exc:
                if exc.status != HTTPStatus.SERVICE_UNAVAILABLE:
                    raise
                last_error = exc
                time.sleep(0.05)
        if last_error is not None:
            raise last_error
        self.fail("timed out waiting for a full-control-v2 overview")

    def _actor_actions(self, root, token, actor_id, public_payloads):
        page = request_json(
            "GET",
            f"{root}/legal-actions?actor_id={quote(actor_id)}&limit=1",
            token=token,
        )
        public_payloads.append(page)
        self.assertEqual(page["control_protocol"], "full-control-v2")
        self.assertEqual(page["page"]["scope"], {
            "actor_id": actor_id,
            "actor_type": "player",
        })
        actions = list(page["page"]["items"])
        total = page["page"]["total_items"]
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
        self.assertEqual(len(actions), total)
        return actions

    def _submit(self, root, token, batch, public_payloads):
        receipt = request_json(
            "POST", f"{root}/batches", token=token, body=batch,
        )
        public_payloads.append(receipt)
        self.assertEqual(receipt["control_protocol"], "full-control-v2")
        self.assertEqual(receipt["receipt_state"], "applied", receipt)
        self.assertFalse(receipt["idempotent"])
        self.assertIsNone(receipt["error"])
        return receipt

    def _post_observing_receipt(
        self, root, token, batch, public_payloads, game_id,
    ):
        """POST normally while observing public durable phase transitions."""
        outcome = {}

        def post():
            try:
                outcome["receipt"] = request_json(
                    "POST", f"{root}/batches", token=token, body=batch,
                )
            except BaseException as exc:  # pragma: no cover - re-raised below
                outcome["error"] = exc

        thread = threading.Thread(
            target=post,
            name=f"isolated-v2-government-post-{game_id}",
            daemon=True,
        )
        thread.start()
        observed_states = []
        deadline = time.monotonic() + 65
        while thread.is_alive() and time.monotonic() < deadline:
            status, payload = self._http_json(
                "GET", f"{root}/receipts/{quote(batch['batch_id'])}", token,
            )
            if status in {HTTPStatus.OK, HTTPStatus.ACCEPTED}:
                public_payloads.append(payload)
                state = payload.get("receipt_state")
                if not observed_states or observed_states[-1] != state:
                    observed_states.append(state)
            elif status != HTTPStatus.NOT_FOUND:
                self.fail({"receipt_probe_status": status, "payload": payload})
            time.sleep(0.001)
        thread.join(1)
        self.assertFalse(thread.is_alive())
        if "error" in outcome:
            raise outcome["error"]
        receipt = outcome["receipt"]
        public_payloads.append(receipt)
        if not observed_states or observed_states[-1] != receipt["receipt_state"]:
            observed_states.append(receipt["receipt_state"])
        return receipt, observed_states

    def _unexpected_receipt_diagnostics(
        self,
        root,
        token,
        temporary_path,
        supervisor,
        game_id,
        before,
        receipt,
        desired_government_id,
        observed_receipt_states,
        public_payloads,
    ):
        """Return best-effort evidence without masking the strict failure."""
        try:
            return self._unexpected_receipt_diagnostics_unchecked(
                root,
                token,
                temporary_path,
                supervisor,
                game_id,
                before,
                receipt,
                desired_government_id,
                observed_receipt_states,
                public_payloads,
            )
        except BaseException as exc:  # pragma: no cover - diagnostics only
            return {
                "receipt": receipt,
                "desired_government_id": desired_government_id,
                "observed_receipt_states": observed_receipt_states,
                "accepted_boundary_observed": (
                    "accepted" in observed_receipt_states
                ),
                "diagnostics_capture_error": repr(exc),
            }

    def _unexpected_receipt_diagnostics_unchecked(
        self,
        root,
        token,
        temporary_path,
        supervisor,
        game_id,
        before,
        receipt,
        desired_government_id,
        observed_receipt_states,
        public_payloads,
    ):
        """Capture isolated evidence before the mandatory teardown runs."""
        health_status, health = self._http_json(
            "GET", f"{root}/health", token,
        )
        public_payloads.append(health)
        try:
            after = self._wait_overview(
                root, token, public_payloads, time.monotonic() + 5,
            )
        except Exception as exc:  # pragma: no cover - failure diagnostics
            after = {"overview_error": repr(exc)}
        receipt_status, durable = self._http_json(
            "GET", f"{root}/receipts/{quote(receipt['batch_id'])}", token,
        )
        public_payloads.append(durable)

        service_root = root.split("/v2/games/", 1)[0]
        status_code, game_status = self._http_json(
            "GET", f"{service_root}/v1/games/{game_id}/status", None,
        )
        watch_code, watch = self._http_json(
            "GET", f"{service_root}/v1/games/{game_id}/watch.json", None,
        )

        episode = temporary_path / "runs" / game_id
        log_paths = [
            episode / "server.log",
            episode / "server.stdout.log",
            *sorted((episode / "sidecars").glob("*/stdout.log")),
            *sorted((episode / "sidecars").glob("*/stderr.log")),
        ]
        logs = {}
        for path in log_paths:
            if path.is_file():
                text = path.read_text(encoding="utf-8", errors="replace")
                logs[str(path.relative_to(episode))] = text[-12000:]
        manifest = json.loads(
            (episode / "manifest.json").read_text(encoding="utf-8"),
        )
        durable_records = {}
        for path in sorted((episode / "v2-receipts").glob("*.json")):
            durable_records[path.name] = json.loads(
                path.read_text(encoding="utf-8"),
            )
        game = supervisor.games[game_id]
        with game.condition:
            process = game.process
            server_poll = process.poll() if process is not None else None
            timeline = list(game.timeline)
            sidecars = tuple(game.sidecars.items())
        sidecar_diagnostics = {}
        for place, sidecar in sidecars:
            try:
                sidecar_health = sidecar.public_health()
            except Exception as exc:  # pragma: no cover - diagnostics only
                sidecar_health = {"public_health_error": repr(exc)}
            with sidecar._lock:
                pending_messages = list(sidecar._messages)
            sidecar_diagnostics[str(place)] = {
                "public_health": sidecar_health,
                "pending_protocol_messages": pending_messages,
            }
        before_state = before["player"]["government_state"]
        after_player = after.get("player", {}) if isinstance(after, dict) else {}
        after_state = (
            after_player.get("government_state", {})
            if isinstance(after_player, dict) else {}
        )
        desired_visible = bool(
            after_state.get("target_id") == desired_government_id
            or (
                after_state.get("current_id") == desired_government_id
                and after_state.get("target_id") is None
            )
        )
        return {
            "receipt": receipt,
            "observed_receipt_states": observed_receipt_states,
            "accepted_boundary_observed": (
                "accepted" in observed_receipt_states
            ),
            "durable_receipt_status": receipt_status,
            "durable_receipt": durable,
            "durable_records": durable_records,
            "health_status": health_status,
            "health": health,
            "game_status_http_status": status_code,
            "game_status": game_status,
            "watch_http_status": watch_code,
            "watch_timeline": watch.get("timeline"),
            "private_timeline": timeline,
            "manifest": manifest,
            "server_process_poll": server_poll,
            "server_process_alive_before_teardown": server_poll is None,
            "sidecars_before_teardown": sidecar_diagnostics,
            "before_overview": before,
            "after_overview": after,
            "before_government_state": before_state,
            "after_government_state": after_state,
            "desired_target_visible_after_request": desired_visible,
            "isolated_logs": logs,
        }

    def _assert_opaque_public_contract(self, payloads):
        def visit(value):
            if isinstance(value, dict):
                for key, child in value.items():
                    self.assertNotEqual(key, "slot")
                    self.assertNotEqual(key, "actor_ref")
                    self.assertFalse(key.startswith("native_"), key)
                    if key == "action_id":
                        self.assertIsInstance(child, str)
                        self.assertIsNotNone(
                            self._OPAQUE_ACTION_ID.fullmatch(child), child,
                        )
                    elif key in self._PUBLIC_ID_FIELDS and child is not None:
                        self.assertIsInstance(child, str)
                        self.assertIsNotNone(
                            self._OPAQUE_ID.fullmatch(child), child,
                        )
                    visit(child)
            elif isinstance(value, list):
                for child in value:
                    visit(child)
            elif isinstance(value, str):
                self.assertIsNone(self._NATIVE_REF.search(value), value)
                self.assertIsNone(self._NATIVE_SLOT.search(value), value)

        visit(payloads)
        serialized = json.dumps(payloads, sort_keys=True)
        self.assertNotIn('"strategic-v1"', serialized)
        self.assertNotIn("/internal/v1/", serialized)
        self.assertNotIn("/turns", serialized)

    def test_real_revolution_retarget_and_enactment(self):
        repository = Path(__file__).parents[2]
        server_binary = repository / "build-agent" / "freeciv-server"
        agent_binary = repository / "build-control-v2" / "freeciv-agent"
        self.assertTrue(server_binary.is_file())
        self.assertTrue(os.access(server_binary, os.X_OK))
        self.assertTrue(agent_binary.is_file())
        self.assertTrue(os.access(agent_binary, os.X_OK))

        temporary = tempfile.TemporaryDirectory(
            prefix="freeciv-v2-government-e2e-",
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
                "isolated-government-admin",
                binary=server_binary,
                agent_binary=agent_binary,
            )
            server = make_supervisor_server(supervisor, "127.0.0.1", 0)
            server_thread = threading.Thread(
                target=server.serve_forever,
                name="isolated-v2-government-http",
                daemon=True,
            )
            server_thread.start()

            created = create_game(
                supervisor.service_url,
                "isolated-government-admin",
                {
                    "mode": "single",
                    "places": 2,
                    "turns": 15,
                    "seed": 909,
                    "ruleset": "classic",
                    "objective": "Exercise government control.",
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
                controller_label="codex-government-e2e",
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

            # Retain the exact disposable process handles solely for teardown
            # verification; all game assertions below continue through HTTP.
            isolated_game = supervisor.games[game_id]
            with isolated_game.condition:
                if isolated_game.process is not None:
                    native_processes.append((
                        "freeciv-server", isolated_game.process,
                    ))
                isolated_sidecars = tuple(isolated_game.sidecars.items())
            for place, sidecar in isolated_sidecars:
                if sidecar._process is not None:
                    native_processes.append((
                        f"freeciv-agent-place-{place}", sidecar._process,
                    ))

            initial = self._overview(root, token, public_payloads)
            self.assertTrue(initial["active_phase"])
            self.assertTrue(initial["phase_ready"])
            player = initial["player"]
            self.assertEqual(player["government"], "Despotism")
            self.assertIsNotNone(
                self._OPAQUE_ID.fullmatch(player["id"]), player["id"],
            )
            initial_governance = player["government_state"]
            self.assertEqual(initial_governance["status"], "stable")
            self.assertIsNone(initial_governance["target_id"])
            self.assertTrue(initial_governance["can_revolution"])

            governments = self._state_section(
                root, token, "governments", public_payloads,
            )
            self.assertGreaterEqual(len(governments), 2)
            self.assertTrue(all(
                self._OPAQUE_ID.fullmatch(item["id"])
                for item in governments
            ))
            despotism = next(
                item for item in governments if item["name"] == "Despotism"
            )
            anarchy = next(
                item for item in governments
                if item["id"] == initial_governance["during_revolution_id"]
            )
            self.assertEqual(anarchy["name"], "Anarchy")
            self.assertTrue(despotism["current"])
            self.assertTrue(anarchy["during_revolution"])

            held_page = request_json(
                "GET",
                f"{root}/legal-actions?actor_id={quote(player['id'])}&limit=1",
                token=token,
            )
            public_payloads.append(held_page)
            held_cursor = held_page["page"]["next_cursor"]
            self.assertIsNotNone(held_cursor)
            stale_initial_action = held_page["page"]["items"][0]

            initial_actions = self._actor_actions(
                root, token, player["id"], public_payloads,
            )
            self.assertTrue(all(
                self._OPAQUE_ACTION_ID.fullmatch(action["action_id"])
                for action in initial_actions
            ))
            revolution = next(
                action for action in initial_actions
                if action["kind"] == "government.revolution"
            )
            self.assertEqual(revolution["subject"]["operation"], "revolution")
            self.assertEqual(revolution["subject"]["actor"], {
                "type": "player", "id": player["id"],
            })
            self.assertEqual(
                revolution["subject"]["target"]["id"], anarchy["id"],
            )

            revolution_batch = self._batch(
                game_id, joined, revolution, "e2e.government.revolution",
            )
            revolution_receipt, observed_revolution_states = (
                self._post_observing_receipt(
                    root,
                    token,
                    revolution_batch,
                    public_payloads,
                    game_id,
                )
            )
            if revolution_receipt["receipt_state"] != "applied":
                diagnostics = self._unexpected_receipt_diagnostics(
                    root,
                    token,
                    temporary_path,
                    supervisor,
                    game_id,
                    initial,
                    revolution_receipt,
                    anarchy["id"],
                    observed_revolution_states,
                    public_payloads,
                )
                self.fail(json.dumps(
                    diagnostics, sort_keys=True, indent=2,
                ))
            self.assertFalse(revolution_receipt["idempotent"])
            self.assertIsNone(revolution_receipt["error"])
            replayed_revolution = request_json(
                "POST", f"{root}/batches",
                token=token, body=revolution_batch,
            )
            durable_revolution = request_json(
                "GET", f"{root}/receipts/e2e.government.revolution",
                token=token,
            )
            public_payloads.extend((replayed_revolution, durable_revolution))
            self.assertTrue(replayed_revolution["idempotent"])
            self.assertFalse(durable_revolution["idempotent"])
            self.assertEqual(replayed_revolution["receipt_state"], "applied")
            self.assertEqual(durable_revolution["receipt_state"], "applied")
            self.assertEqual(
                replayed_revolution["state_revision"],
                revolution_receipt["state_revision"],
            )
            self.assertEqual(
                durable_revolution["state_revision"],
                revolution_receipt["state_revision"],
            )

            during = self._overview(root, token, public_payloads)
            during_state = during["player"]["government_state"]
            self.assertEqual(during_state["current_id"], anarchy["id"])
            self.assertEqual(during_state["target_id"], anarchy["id"])
            self.assertEqual(during["player"]["government"], "Anarchy")
            self.assertEqual(during_state["status"], "anarchy")
            self.assertIsInstance(during_state["finish_turn"], int)
            self.assertGreater(during_state["finish_turn"], during["turn"])
            self.assertEqual(
                during_state["turns_remaining"],
                during_state["finish_turn"] - during["turn"],
            )
            self.assertGreater(during_state["turns_remaining"], 0)
            self.assertGreaterEqual(during_state["max_turns"], 1)
            self.assertIn(during_state["method"], {
                "fixed", "random", "quickening", "random_quickening",
            })

            cursor_status, cursor_error = self._http_json(
                "GET",
                f"{root}/legal-actions?cursor={quote(held_cursor)}",
                token,
            )
            public_payloads.append(cursor_error)
            self.assertEqual(cursor_status, HTTPStatus.BAD_REQUEST)
            self.assertEqual(cursor_error["error"]["code"], "invalid_request")

            stale_status, stale_error = self._http_json(
                "POST", f"{root}/batches", token,
                self._batch(
                    game_id,
                    joined,
                    stale_initial_action,
                    "e2e.government.initial-stale",
                ),
            )
            public_payloads.append(stale_error)
            self.assertEqual(stale_status, HTTPStatus.CONFLICT)
            self.assertEqual(stale_error["error"]["code"], "stale_revision")

            during_actions = self._actor_actions(
                root, token, player["id"], public_payloads,
            )
            choose_despotism = next(
                action for action in during_actions
                if action["kind"] == "government.change"
                and action["subject"]["target"]["id"] == despotism["id"]
            )
            self.assertEqual(choose_despotism["subject"]["operation"], "change")
            choice_batch = self._batch(
                game_id, joined, choose_despotism,
                "e2e.government.choose-despotism",
            )
            choice_receipt, observed_receipt_states = (
                self._post_observing_receipt(
                    root, token, choice_batch, public_payloads, game_id,
                )
            )
            if choice_receipt["receipt_state"] != "applied":
                diagnostics = self._unexpected_receipt_diagnostics(
                    root,
                    token,
                    temporary_path,
                    supervisor,
                    game_id,
                    during,
                    choice_receipt,
                    despotism["id"],
                    observed_receipt_states,
                    public_payloads,
                )
                self.fail(json.dumps(
                    diagnostics, sort_keys=True, indent=2,
                ))
            self.assertFalse(choice_receipt["idempotent"])
            self.assertIsNone(choice_receipt["error"])

            targeted = self._overview(root, token, public_payloads)
            targeted_state = targeted["player"]["government_state"]
            self.assertTrue(
                (
                    targeted_state["current_id"] == anarchy["id"]
                    and targeted_state["target_id"] == despotism["id"]
                    and targeted_state["status"] == "anarchy_targeted"
                )
                or (
                    targeted_state["current_id"] == despotism["id"]
                    and targeted_state["target_id"] is None
                    and targeted_state["status"] == "stable"
                ),
                targeted_state,
            )

            fresh_actions = self._actor_actions(
                root, token, player["id"], public_payloads,
            )
            self.assertFalse(any(
                action["kind"] == "government.change"
                and action["subject"]["target"]["id"] == despotism["id"]
                for action in fresh_actions
            ))
            stale_choice_status, stale_choice_error = self._http_json(
                "POST", f"{root}/batches", token,
                self._batch(
                    game_id,
                    joined,
                    choose_despotism,
                    "e2e.government.choice-stale",
                ),
            )
            public_payloads.append(stale_choice_error)
            self.assertEqual(stale_choice_status, HTTPStatus.CONFLICT)
            self.assertEqual(
                stale_choice_error["error"]["code"], "stale_revision",
            )

            # End real phases until the deterministic Classic timer enacts the
            # recorded target.  Classic normally applies that change between
            # snapshots, so this smoke does not claim to exercise the separate
            # due-choice/no-anarchy suppression boundary.
            enacted = targeted
            phase_deadline = time.monotonic() + 45
            phase_index = 0
            while not (
                enacted["player"]["government_state"]["current_id"]
                == despotism["id"]
                and enacted["player"]["government_state"]["target_id"] is None
            ):
                self.assertLess(time.monotonic(), phase_deadline, enacted)
                if enacted["active_phase"] and enacted["phase_ready"]:
                    actions = self._actor_actions(
                        root, token, player["id"], public_payloads,
                    )
                    phase_end = next(
                        action for action in actions
                        if action["kind"] == "phase.end"
                    )
                    phase_index += 1
                    self._submit(
                        root,
                        token,
                        self._batch(
                            game_id,
                            joined,
                            phase_end,
                            f"e2e.government.phase-{phase_index}",
                        ),
                        public_payloads,
                    )
                else:
                    time.sleep(0.05)
                enacted = self._wait_overview(
                    root, token, public_payloads, phase_deadline,
                )

            final_state = enacted["player"]["government_state"]
            self.assertEqual(enacted["player"]["government"], "Despotism")
            self.assertEqual(final_state["current_id"], despotism["id"])
            self.assertIsNone(final_state["target_id"])
            self.assertEqual(final_state["status"], "stable")
            self.assertGreaterEqual(phase_index, 1)

            self._assert_opaque_public_contract(public_payloads)
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
                # Capture every registered disposable process before close so
                # even an early body failure plus a failing close has an exact
                # fallback kill target.
                try:
                    seen_processes = {
                        id(process) for _, process in native_processes
                    }
                    with supervisor.lock:
                        cleanup_games = tuple(supervisor.games.values())
                    for cleanup_game in cleanup_games:
                        with cleanup_game.condition:
                            cleanup_server = cleanup_game.process
                            cleanup_sidecars = tuple(
                                cleanup_game.sidecars.items()
                            )
                        if (
                            cleanup_server is not None
                            and id(cleanup_server) not in seen_processes
                        ):
                            native_processes.append((
                                "freeciv-server", cleanup_server,
                            ))
                            seen_processes.add(id(cleanup_server))
                        for place, cleanup_sidecar in cleanup_sidecars:
                            cleanup_process = cleanup_sidecar._process
                            if (
                                cleanup_process is not None
                                and id(cleanup_process) not in seen_processes
                            ):
                                native_processes.append((
                                    f"freeciv-agent-place-{place}",
                                    cleanup_process,
                                ))
                                seen_processes.add(id(cleanup_process))
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
            if game_id is not None:
                thread_deadline = time.monotonic() + 5
                while any(
                    thread.is_alive() and game_id in thread.name
                    for thread in threading.enumerate()
                ) and time.monotonic() < thread_deadline:
                    time.sleep(0.05)
                leaked_threads = [
                    thread.name for thread in threading.enumerate()
                    if thread.is_alive() and game_id in thread.name
                ]
                if leaked_threads:
                    cleanup_failures.append(
                        f"game threads remained alive: {leaked_threads}",
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
