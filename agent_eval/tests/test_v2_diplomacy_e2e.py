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
    "set FREECIV_AGENT_E2E=1 for the isolated diplomacy HTTP smoke",
)
class V2DiplomacyRealE2ETests(unittest.TestCase):
    @staticmethod
    def _pages(root, token, endpoint, query, public):
        items = []
        next_query = query
        while True:
            payload = request_json(
                "GET", f"{root}/{endpoint}?{next_query}", token=token,
            )
            public.append(payload)
            items.extend(payload["page"]["items"])
            cursor = payload["page"]["next_cursor"]
            if cursor is None:
                return items
            next_query = f"cursor={quote(cursor)}"

    def _submit(self, root, token, game_id, joined, action, batch_id, public):
        receipt = request_json(
            "POST", f"{root}/batches", token=token, body={
                "schema_version": 2,
                "control_protocol": "full-control-v2",
                "game_id": game_id,
                "agent_id": joined["agent_id"],
                "batch_id": batch_id,
                "state_revision": action["state_revision"],
                "commands": [{
                    "action_id": action["action_id"], "arguments": {},
                }],
            },
        )
        public.append(receipt)
        self.assertEqual(receipt["receipt_state"], "applied", receipt)
        return receipt

    def _relation_actions(
        self, root, token, actor_id, relation_id, public,
    ):
        return self._pages(
            root, token, "legal-actions",
            f"actor_id={quote(actor_id)}&target_id={quote(relation_id)}",
            public,
        )

    def _wait_ready(self, root, token, public, timeout=40):
        deadline = time.monotonic() + timeout
        while True:
            health = request_json("GET", f"{root}/health", token=token)
            if (
                health["game_state"] == "running"
                and health["sidecar"]["state"] == "ready"
                and health["sidecar"].get("client_state") == "running"
            ):
                public.append(health)
                return deadline
            self.assertLess(time.monotonic(), deadline, health)
            time.sleep(0.05)

    def _wait_relation(
        self, root, token, player_name, public, predicate=lambda _item: True,
        timeout=40,
    ):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            overview_payload = request_json(
                "GET", f"{root}/state", token=token,
            )
            diplomacy_payload = request_json(
                "GET", f"{root}/state?section=diplomacy&limit=16",
                token=token,
            )
            public.extend((overview_payload, diplomacy_payload))
            overview = overview_payload["page"]["items"][0]
            relation = next((
                item for item in diplomacy_payload["page"]["items"]
                if item["player_name"] == player_name and predicate(item)
            ), None)
            if relation is not None:
                return overview, relation
            time.sleep(0.05)
        self.fail(f"diplomatic relation with {player_name} did not converge")

    def test_real_open_propose_remove_and_close_treaty(self):
        repository = Path(__file__).parents[2]
        server_binary = repository / "build-agent" / "freeciv-server"
        agent_binary = repository / "build-control-v2" / "freeciv-agent"
        self.assertTrue(os.access(server_binary, os.X_OK))
        self.assertTrue(os.access(agent_binary, os.X_OK))

        temporary = tempfile.TemporaryDirectory(
            prefix="freeciv-v2-diplomacy-e2e-",
        )
        temporary_path = Path(temporary.name)
        supervisor = None
        server = None
        server_thread = None
        public = []
        try:
            supervisor = Supervisor(
                temporary_path / "runs", "isolated-diplomacy-admin",
                binary=server_binary, agent_binary=agent_binary,
            )
            server = make_supervisor_server(supervisor, "127.0.0.1", 0)
            server_thread = threading.Thread(
                target=server.serve_forever,
                name="isolated-v2-diplomacy-http", daemon=True,
            )
            server_thread.start()
            created = create_game(
                supervisor.service_url, "isolated-diplomacy-admin", {
                    "mode": "single", "places": 2, "turns": 5,
                    "seed": 1905, "ruleset": "classic",
                    "objective": "Exercise treaty diplomacy controls.",
                    "timing_mode": "infinite", "action_timeout_s": None,
                    "lobby_timeout_s": 30, "frame_interval": 0,
                    "frame_zoom": 1, "control_protocol": "full-control-v2",
                },
            )
            public.append(created)
            game = supervisor.game(created["game_id"])
            game._send_commands(["set startunits c", "set dispersion 10"])
            joined = join_game(
                supervisor.service_url, created["game_id"],
                created["join_token"],
                controller_label="codex-diplomacy-e2e",
                supported_control_protocols=["full-control-v2"],
            )
            public.append(joined)
            game_id = created["game_id"]
            token = joined["agent_token"]
            root = f"{supervisor.service_url}/v2/games/{game_id}/me"
            deadline = self._wait_ready(root, token, public)

            # Disposable fixture only: placing a native player's unit next to
            # the external seat establishes normal Freeciv contact. Every
            # treaty operation below travels through the public v2 HTTP API
            # and the normal human-client diplomacy packet path.
            game._send_commands([
                'lua unsafe-cmd local p=find.player("AgentPlace1"); '
                'local q=find.player("NativePlace2"); assert(p and q); '
                'local a=nil; for u in p:units_iterate() do a=u.tile; break end; '
                'assert(a); local w=find.unit_type("Warriors"); local b=nil; '
                'for t in whole_map_iterate() do if a:sq_distance(t)==1 and '
                'w:can_exist_at_tile(t) and t:num_units()==0 and '
                't:city()==nil then b=t; break end end; assert(b); '
                'q:create_unit(b,w,0,nil,0); a:show(p); b:show(p)',
            ])

            overview, relation = self._wait_relation(
                root, token, "NativePlace2", public,
                lambda item: item["can_open_meeting"],
                timeout=max(1, deadline - time.monotonic()),
            )
            actor_id = overview["player"]["id"]
            relation_id = relation["relation_id"]
            actions = self._relation_actions(
                root, token, actor_id, relation_id, public,
            )
            opened = next(
                item for item in actions
                if item["subject"]["operation"] == "open_meeting"
            )
            self._submit(
                root, token, game_id, joined, opened,
                "e2e.diplomacy.open", public,
            )

            actions = self._relation_actions(
                root, token, actor_id, relation_id, public,
            )
            proposed = next(
                item for item in actions
                if item["subject"]["operation"] == "propose_clause"
                and item["subject"]["clause"]["type"] == "map"
            )
            self._submit(
                root, token, game_id, joined, proposed,
                "e2e.diplomacy.propose-map", public,
            )

            clauses = self._pages(
                root, token, "state",
                f"section=diplomacy_clauses&relation_id={relation_id}&limit=16",
                public,
            )
            self.assertIn("map", {item["type"] for item in clauses})
            actions = self._relation_actions(
                root, token, actor_id, relation_id, public,
            )
            removed = next(
                item for item in actions
                if item["subject"]["operation"] == "remove_clause"
                and item["subject"]["clause"]["type"] == "map"
            )
            self._submit(
                root, token, game_id, joined, removed,
                "e2e.diplomacy.remove-map", public,
            )

            # Leave a different clause present when the meeting closes.  A
            # reopen must start a fresh generation and never surface that
            # closed generation as a ghost treaty.
            actions = self._relation_actions(
                root, token, actor_id, relation_id, public,
            )
            seamap = next(
                item for item in actions
                if item["subject"]["operation"] == "propose_clause"
                and item["subject"]["clause"]["type"] == "sea_map"
            )
            self._submit(
                root, token, game_id, joined, seamap,
                "e2e.diplomacy.propose-seamap", public,
            )
            actions = self._relation_actions(
                root, token, actor_id, relation_id, public,
            )
            closed = next(
                item for item in actions
                if item["subject"]["operation"] == "close_meeting"
            )
            self._submit(
                root, token, game_id, joined, closed,
                "e2e.diplomacy.close", public,
            )
            overview, relation = self._wait_relation(
                root, token, "NativePlace2", public,
                lambda item: item["can_open_meeting"],
            )
            actions = self._relation_actions(
                root, token, actor_id, relation_id, public,
            )
            reopened = next(
                item for item in actions
                if item["subject"]["operation"] == "open_meeting"
            )
            self._submit(
                root, token, game_id, joined, reopened,
                "e2e.diplomacy.reopen", public,
            )
            clauses = self._pages(
                root, token, "state",
                f"section=diplomacy_clauses&relation_id={relation_id}&limit=16",
                public,
            )
            self.assertNotIn("sea_map", {item["type"] for item in clauses})
            actions = self._relation_actions(
                root, token, actor_id, relation_id, public,
            )
            reclosed = next(
                item for item in actions
                if item["subject"]["operation"] == "close_meeting"
            )
            self._submit(
                root, token, game_id, joined, reclosed,
                "e2e.diplomacy.reclose", public,
            )
            payload = json.dumps(public, sort_keys=True)
            self.assertIsNone(re.search(r"[pcu]:[0-9]+:[0-9]+", payload))
            for private in (
                '"slot"', '"actor_ref"', '"counterpart_ref"',
                "clauses_digest", "ACT_RELATION_CAP",
            ):
                self.assertNotIn(private, payload)
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

    def test_two_external_agents_accept_withdraw_and_cancel(self):
        repository = Path(__file__).parents[2]
        server_binary = repository / "build-agent" / "freeciv-server"
        agent_binary = repository / "build-control-v2" / "freeciv-agent"
        self.assertTrue(os.access(server_binary, os.X_OK))
        self.assertTrue(os.access(agent_binary, os.X_OK))

        temporary = tempfile.TemporaryDirectory(
            prefix="freeciv-v2-diplomacy-peers-e2e-",
        )
        temporary_path = Path(temporary.name)
        supervisor = None
        server = None
        server_thread = None
        public = []
        try:
            supervisor = Supervisor(
                temporary_path / "runs", "isolated-diplomacy-peers-admin",
                binary=server_binary, agent_binary=agent_binary,
            )
            server = make_supervisor_server(supervisor, "127.0.0.1", 0)
            server_thread = threading.Thread(
                target=server.serve_forever,
                name="isolated-v2-diplomacy-peers-http", daemon=True,
            )
            server_thread.start()
            created = create_game(
                supervisor.service_url, "isolated-diplomacy-peers-admin", {
                    "mode": "multiplayer", "places": 2, "turns": 5,
                    "seed": 1919, "ruleset": "classic",
                    "objective": "Exercise peer treaty diplomacy controls.",
                    "timing_mode": "infinite", "action_timeout_s": None,
                    "lobby_timeout_s": 30, "frame_interval": 0,
                    "frame_zoom": 1, "control_protocol": "full-control-v2",
                },
            )
            public.append(created)
            game = supervisor.game(created["game_id"])
            game._send_commands(["set startunits c", "set dispersion 10"])
            joined_a = join_game(
                supervisor.service_url, created["game_id"],
                created["join_token"], controller_label="codex-diplomacy-a",
                supported_control_protocols=["full-control-v2"],
            )
            joined_b = join_game(
                supervisor.service_url, created["game_id"],
                created["join_token"], controller_label="claude-diplomacy-b",
                supported_control_protocols=["full-control-v2"],
            )
            public.extend((joined_a, joined_b))
            game_id = created["game_id"]
            root_a = f"{supervisor.service_url}/v2/games/{game_id}/me"
            root_b = root_a
            token_a = joined_a["agent_token"]
            token_b = joined_b["agent_token"]
            self._wait_ready(root_a, token_a, public)
            self._wait_ready(root_b, token_b, public)

            game._send_commands([
                'lua unsafe-cmd local p=find.player("AgentPlace1"); '
                'local q=find.player("AgentPlace2"); assert(p and q); '
                'local a=nil; for u in p:units_iterate() do a=u.tile; break end; '
                'assert(a); local w=find.unit_type("Warriors"); local b=nil; '
                'for t in whole_map_iterate() do if a:sq_distance(t)==1 and '
                'w:can_exist_at_tile(t) and t:num_units()==0 and '
                't:city()==nil then b=t; break end end; assert(b); '
                'q:create_unit(b,w,0,nil,0); a:show(p); b:show(p); '
                'a:show(q); b:show(q)',
            ])
            overview_a, relation_a = self._wait_relation(
                root_a, token_a, "AgentPlace2", public,
                lambda item: item["can_open_meeting"],
            )
            overview_b, relation_b = self._wait_relation(
                root_b, token_b, "AgentPlace1", public,
            )
            actor_a = overview_a["player"]["id"]
            actor_b = overview_b["player"]["id"]
            actions_a = self._relation_actions(
                root_a, token_a, actor_a, relation_a["relation_id"], public,
            )
            opened = next(
                item for item in actions_a
                if item["subject"]["operation"] == "open_meeting"
            )
            self._submit(
                root_a, token_a, game_id, joined_a, opened,
                "e2e.peers.open", public,
            )

            actions_a = self._relation_actions(
                root_a, token_a, actor_a, relation_a["relation_id"], public,
            )
            relation_clause = next(
                item for item in actions_a
                if item["subject"]["operation"] == "propose_clause"
                and item["subject"]["clause"]["type"]
                   in {"ceasefire", "peace"}
            )
            self._submit(
                root_a, token_a, game_id, joined_a, relation_clause,
                "e2e.peers.propose-relation", public,
            )
            actions_a = self._relation_actions(
                root_a, token_a, actor_a, relation_a["relation_id"], public,
            )
            vision = next((
                item for item in actions_a
                if item["subject"]["operation"] == "propose_clause"
                and item["subject"]["clause"]["type"] == "vision"
                and item["subject"]["clause"]["giver_player_id"] == actor_a
            ), None)
            self.assertIsNotNone(vision, [
                (
                    item["subject"]["operation"],
                    item["subject"].get("clause"),
                )
                for item in actions_a
            ])
            self._submit(
                root_a, token_a, game_id, joined_a, vision,
                "e2e.peers.propose-vision", public,
            )
            actions_a = self._relation_actions(
                root_a, token_a, actor_a, relation_a["relation_id"], public,
            )
            accepted_a = next(
                item for item in actions_a
                if item["subject"]["operation"] == "accept"
            )
            self._submit(
                root_a, token_a, game_id, joined_a, accepted_a,
                "e2e.peers.accept-a", public,
            )

            # Acceptance is desired-state based.  Once accepted the catalog
            # offers withdrawal instead of another toggle, and a new batch
            # cannot replay the now-stale acceptance capability.
            actions_a = self._relation_actions(
                root_a, token_a, actor_a, relation_a["relation_id"], public,
            )
            operations_a = {
                item["subject"]["operation"] for item in actions_a
            }
            self.assertIn("withdraw_acceptance", operations_a)
            self.assertNotIn("accept", operations_a)
            with self.assertRaises(ClientError) as replay:
                request_json(
                    "POST", f"{root_a}/batches", token=token_a, body={
                        "schema_version": 2,
                        "control_protocol": "full-control-v2",
                        "game_id": game_id,
                        "agent_id": joined_a["agent_id"],
                        "batch_id": "e2e.peers.accept-a-stale-replay",
                        "state_revision": accepted_a["state_revision"],
                        "commands": [{
                            "action_id": accepted_a["action_id"],
                            "arguments": {},
                        }],
                    },
                )
            self.assertIn(replay.exception.status, {409, 410})

            # Default multiplayer Freeciv uses alternating player phases.
            # Hand control to B through the same public API before asking B
            # to accept, rather than relying on privileged server commands.
            global_actions_a = self._pages(
                root_a, token_a, "legal-actions", "", public,
            )
            phase_end_a = next(
                item for item in global_actions_a
                if item["kind"] == "phase.end"
            )
            self._submit(
                root_a, token_a, game_id, joined_a, phase_end_a,
                "e2e.peers.end-a-phase", public,
            )
            phase_deadline = time.monotonic() + 20
            while True:
                health_b = request_json(
                    "GET", f"{root_b}/health", token=token_b,
                )
                public.append(health_b)
                if (
                    health_b.get("phase") is not None
                    and health_b["phase"]["active"]
                    and health_b["phase"]["state"] == "awaiting_agent"
                ):
                    break
                self.assertLess(time.monotonic(), phase_deadline, health_b)
                time.sleep(0.05)

            # The other controller gets its own opaque player/relation/action
            # IDs; accepting through that independent scope completes the deal.
            overview_b, relation_b = self._wait_relation(
                root_b, token_b, "AgentPlace1", public,
                lambda item: item["meeting"] is not None,
            )
            actor_b = overview_b["player"]["id"]
            actions_b = self._relation_actions(
                root_b, token_b, actor_b, relation_b["relation_id"], public,
            )
            accepted_b = next(
                item for item in actions_b
                if item["subject"]["operation"] == "accept"
            )
            self._submit(
                root_b, token_b, game_id, joined_b, accepted_b,
                "e2e.peers.accept-b", public,
            )

            global_actions_b = self._pages(
                root_b, token_b, "legal-actions", "", public,
            )
            phase_end_b = next(
                item for item in global_actions_b
                if item["kind"] == "phase.end"
            )
            self._submit(
                root_b, token_b, game_id, joined_b, phase_end_b,
                "e2e.peers.end-b-phase", public,
            )
            phase_deadline = time.monotonic() + 20
            while True:
                health_a = request_json(
                    "GET", f"{root_a}/health", token=token_a,
                )
                public.append(health_a)
                if (
                    health_a.get("phase") is not None
                    and health_a["phase"]["active"]
                    and health_a["phase"]["state"] == "awaiting_agent"
                ):
                    break
                self.assertLess(time.monotonic(), phase_deadline, health_a)
                time.sleep(0.05)

            _overview_a, relation_a = self._wait_relation(
                root_a, token_a, "AgentPlace2", public,
                lambda item: item["meeting"] is None
                   and item["gives_vision"],
            )
            actions_a = self._relation_actions(
                root_a, token_a, actor_a, relation_a["relation_id"], public,
            )
            withdraw = next(
                item for item in actions_a
                if item["subject"]["operation"] == "withdraw_vision"
            )
            self._submit(
                root_a, token_a, game_id, joined_a, withdraw,
                "e2e.peers.withdraw-vision", public,
            )
            _overview_a, relation_a = self._wait_relation(
                root_a, token_a, "AgentPlace2", public,
                lambda item: not item["gives_vision"],
            )
            actions_a = self._relation_actions(
                root_a, token_a, actor_a, relation_a["relation_id"], public,
            )
            cancel = next(
                item for item in actions_a
                if item["subject"]["operation"] == "break_relation"
            )
            prior_state = relation_a["state"]
            self._submit(
                root_a, token_a, game_id, joined_a, cancel,
                "e2e.peers.break-relation", public,
            )
            _overview_a, relation_a = self._wait_relation(
                root_a, token_a, "AgentPlace2", public,
                lambda item: item["state"] != prior_state,
            )
            self.assertNotEqual(relation_a["state"], prior_state)

            payload = json.dumps(public, sort_keys=True)
            self.assertIsNone(re.search(r"[pcu]:[0-9]+:[0-9]+", payload))
            for private in (
                '"slot"', '"actor_ref"', '"counterpart_ref"',
                "clauses_digest", "ACT_RELATION_CAP",
            ):
                self.assertNotIn(private, payload)
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
