from __future__ import annotations

import json
import os
import re
import shutil
import stat
import subprocess
import tempfile
import threading
import time
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from agent_eval.client import create_game, request_json
from agent_eval.supervisor import Supervisor, make_supervisor_server


@unittest.skipUnless(
    os.environ.get("FREECIV_AGENT_E2E") == "1",
    "set FREECIV_AGENT_E2E=1 for the isolated two-harness v2 match",
)
class V2MultiplayerPhaseRealE2ETests(unittest.TestCase):
    """Run two independent player workspaces through a complete real match."""

    # This proof intentionally launches a fresh public player CLI process for
    # each command in two independent copied workspaces. On debug/local builds
    # the hundreds of process starts dominate the twenty-turn game itself.
    MATCH_WATCHDOG_S = 240.0
    MATCH_TURNS = 20

    def _copy_player_workspace(self, source: Path, destination: Path) -> Path:
        shutil.copytree(
            source,
            destination,
            ignore=shutil.ignore_patterns(
                ".invites", ".sessions", "__pycache__", "*.pyc",
            ),
        )
        (destination / ".invites").mkdir(mode=0o700)
        (destination / ".sessions").mkdir(mode=0o700)
        return destination

    def _run_player(
        self,
        workspace: Path,
        service_url: str,
        deadline: float,
        *arguments: str,
        expected_returncode: int = 0,
        extra_environment: dict[str, str] | None = None,
    ) -> dict:
        remaining = deadline - time.monotonic()
        self.assertGreater(remaining, 0, "two-harness match watchdog expired")
        environment = dict(os.environ)
        environment["AGENT_EVAL_SERVICE_URL"] = service_url
        environment["PLAY_STATE_DIR"] = ".sessions"
        if extra_environment:
            environment.update(extra_environment)
        self._player_commands.append(arguments[0])
        completed = subprocess.run(
            ("python3", "-B", "client.py", *arguments),
            cwd=workspace,
            env=environment,
            check=False,
            capture_output=True,
            text=True,
            timeout=max(0.1, min(35.0, remaining)),
        )
        self.assertEqual(
            completed.returncode,
            expected_returncode,
            {
                "arguments": arguments,
                "stdout": completed.stdout,
                "stderr": completed.stderr,
            },
        )
        values = []
        try:
            value = json.loads(completed.stdout)
        except json.JSONDecodeError:
            value = None
        if isinstance(value, dict):
            values.append(value)
        else:
            for line in completed.stdout.splitlines():
                try:
                    value = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if isinstance(value, dict):
                    values.append(value)
        self.assertTrue(
            values,
            {"arguments": arguments, "stdout": completed.stdout},
        )
        return values[-1]

    def _health(
        self, workspace: Path, service_url: str, deadline: float, session: Path,
    ) -> dict:
        return self._run_player(
            workspace, service_url, deadline,
            "health", "--session", str(session),
        )

    def _health_pair(
        self,
        harnesses: list[dict],
        service_url: str,
        deadline: float,
    ) -> list[dict]:
        # These are genuinely independent harness processes. Probe them at the
        # same time so a valid phase transition is less likely to split the
        # observations and process startup does not dominate the match.
        with ThreadPoolExecutor(max_workers=2) as executor:
            futures = [
                executor.submit(
                    self._health,
                    harness["workspace"], service_url, deadline,
                    harness["session"],
                )
                for harness in harnesses
            ]
            return [future.result() for future in futures]

    def _state(
        self,
        workspace: Path,
        service_url: str,
        deadline: float,
        session: Path,
        section: str,
    ) -> dict:
        return self._run_player(
            workspace, service_url, deadline,
            "state", "--session", str(session),
            "--section", section, "--limit", "16",
        )

    def _legal_actions(
        self,
        workspace: Path,
        service_url: str,
        deadline: float,
        session: Path,
    ) -> tuple[list[dict], list[dict]]:
        pages = []
        page = self._run_player(
            workspace, service_url, deadline,
            "legal", "--session", str(session), "--limit", "16",
        )
        pages.append(page)
        actions = list(page["page"]["items"])
        cursor = page["page"]["next_cursor"]
        while cursor is not None:
            page = self._run_player(
                workspace, service_url, deadline,
                "legal", "--session", str(session), "--cursor", cursor,
            )
            pages.append(page)
            actions.extend(page["page"]["items"])
            cursor = page["page"]["next_cursor"]
        self.assertEqual(len(actions), pages[-1]["page"]["total_items"])
        return actions, pages

    def _batch(
        self,
        workspace: Path,
        service_url: str,
        deadline: float,
        session: Path,
        action: dict,
        arguments: dict | None = None,
    ) -> dict:
        return self._run_player(
            workspace, service_url, deadline,
            "batch", "--session", str(session),
            "--action-id", action["action_id"],
            "--arguments", json.dumps(
                arguments or {}, sort_keys=True, separators=(",", ":"),
            ),
        )

    def _receipt(
        self,
        workspace: Path,
        service_url: str,
        deadline: float,
        session: Path,
        batch_id: str,
        *,
        expected_returncode: int = 0,
    ) -> dict:
        return self._run_player(
            workspace, service_url, deadline,
            "receipt", "--session", str(session),
            "--batch-id", batch_id,
            expected_returncode=expected_returncode,
        )

    def _wait_until_playable(
        self,
        harnesses: list[dict],
        service_url: str,
        deadline: float,
    ) -> list[dict]:
        last = []
        while time.monotonic() < deadline:
            last = self._health_pair(harnesses, service_url, deadline)
            if all(
                health["game_state"] == "running"
                and health["sidecar"]["state"] == "ready"
                and health["observation_available"]
                for health in last
            ):
                return last
            time.sleep(0.05)
        self.fail({"reason": "v2 seats did not become playable", "health": last})

    def _assert_safe_payloads(self, payloads: list[dict]) -> None:
        native_ref = re.compile(
            r"(?<![A-Za-z0-9_])[pcu]:[0-9]+:[0-9]+(?![A-Za-z0-9_])",
        )
        native_slot = re.compile(
            r"(?<![A-Za-z0-9_])a[0-9a-fA-F]{16}(?![A-Za-z0-9_])",
        )

        def visit(value):
            if isinstance(value, dict):
                for key, child in value.items():
                    self.assertNotIn(key, {
                        "agent_token", "join_token", "internal_token",
                        "native_ref", "actor_ref", "slot", "request_id",
                    })
                    self.assertFalse(key.startswith("native_"), key)
                    visit(child)
            elif isinstance(value, list):
                for child in value:
                    visit(child)
            elif isinstance(value, str):
                self.assertIsNone(native_ref.search(value), value)
                self.assertIsNone(native_slot.search(value), value)

        visit(payloads)

    def test_two_independent_harnesses_complete_twenty_turns_with_timeout_recovery(self):
        repository = Path(__file__).parents[2]
        server_binary = repository / "build-agent" / "freeciv-server"
        agent_binary = repository / "build-control-v2" / "freeciv-agent"
        self.assertTrue(server_binary.is_file())
        self.assertTrue(os.access(server_binary, os.X_OK))
        self.assertTrue(agent_binary.is_file())
        self.assertTrue(os.access(agent_binary, os.X_OK))

        temporary = tempfile.TemporaryDirectory(
            prefix="freeciv-v2-two-harness-e2e-",
        )
        temporary_path = Path(temporary.name)
        supervisor = None
        server = None
        server_thread = None
        public_payloads: list[dict] = []
        self._player_commands: list[str] = []
        deadline = time.monotonic() + self.MATCH_WATCHDOG_S
        try:
            supervisor = Supervisor(
                temporary_path / "runs",
                "isolated-two-harness-admin",
                binary=server_binary,
                agent_binary=agent_binary,
            )
            server = make_supervisor_server(supervisor, "127.0.0.1", 0)
            server_thread = threading.Thread(
                target=server.serve_forever,
                name="isolated-v2-two-harness-http",
                daemon=True,
            )
            server_thread.start()

            created = create_game(
                supervisor.service_url,
                "isolated-two-harness-admin",
                {
                    "mode": "multiplayer",
                    "places": 2,
                    "turns": self.MATCH_TURNS,
                    "seed": 909,
                    "ruleset": "classic",
                    "objective": "Complete an isolated two-harness v2 match.",
                    "action_timeout_s": 30,
                    "lobby_timeout_s": 30,
                    "frame_interval": 0,
                    "frame_zoom": 1,
                    "control_protocol": "full-control-v2",
                },
            )
            self.assertEqual(created["control_protocol"], "full-control-v2")
            self.assertEqual(created["timing_mode"], "custom")
            self.assertEqual(created["action_timeout_s"], 30)

            harnesses = []
            for place, label in (
                (1, "codex-e2e-model"),
                (2, "claude-e2e-model"),
            ):
                workspace = self._copy_player_workspace(
                    repository / "play",
                    temporary_path / f"harness-{place}",
                )
                joined = self._run_player(
                    workspace,
                    supervisor.service_url,
                    deadline,
                    "join",
                    "--game-id", created["game_id"],
                    "--name", label,
                    "--place", str(place),
                    extra_environment={
                        "AGENT_EVAL_JOIN_TOKEN": created["join_token"],
                    },
                )
                public_payloads.append(joined)
                session = Path(joined["session_file"])
                self.assertTrue(
                    session.resolve().is_relative_to(
                        (workspace / ".sessions").resolve(),
                    ),
                )
                self.assertEqual(stat.S_IMODE(session.stat().st_mode), 0o600)
                private_session = json.loads(session.read_text(encoding="utf-8"))
                self.assertEqual(private_session["agent_id"], joined["agent_id"])
                self.assertEqual(private_session["place"], place)
                self.assertEqual(private_session["controller_label"], label)
                self.assertEqual(
                    private_session["control_protocol"], "full-control-v2",
                )
                harnesses.append({
                    "place": place,
                    "label": label,
                    "workspace": workspace,
                    "session": session,
                    "agent_id": private_session["agent_id"],
                    "research_set": False,
                    "research_batch": None,
                })

            healths = self._wait_until_playable(
                harnesses, supervisor.service_url, deadline,
            )
            public_payloads.extend(healths)
            agent_phase_ends = 0
            skipped = False
            durable_batch = None
            durable_owner = None
            timeout_event = None

            while time.monotonic() < deadline:
                healths = self._health_pair(
                    harnesses, supervisor.service_url, deadline,
                )
                public_payloads.extend(healths)
                game_states = {
                    health["game_state"] for health in healths
                }
                if game_states == {"completed"}:
                    break
                # The two authenticated health calls are intentionally made by
                # independent harness processes, not as one atomic spectator
                # snapshot. A phase or terminal transition may land between
                # them; resample instead of treating adjacent valid epochs as
                # server disagreement.
                if len(game_states) != 1:
                    time.sleep(0.05)
                    continue
                self.assertEqual(game_states, {"running"}, healths)
                phases = [health["phase"] for health in healths]
                if any(phase is None for phase in phases):
                    time.sleep(0.05)
                    continue
                phase_keys = {
                    (phase["turn"], phase["phase"])
                    for phase in phases
                }
                if len(phase_keys) != 1:
                    time.sleep(0.05)
                    continue
                active_indexes = [
                    index for index, health in enumerate(healths)
                    if health["phase"] is not None
                    and health["phase"]["active"]
                    and health["phase"]["state"] == "awaiting_agent"
                ]
                if not active_indexes:
                    time.sleep(0.05)
                    continue
                self.assertEqual(len(active_indexes), 1, healths)
                index = active_indexes[0]
                harness = harnesses[index]
                health = healths[index]

                inactive_actions, inactive_pages = self._legal_actions(
                    harnesses[1 - index]["workspace"],
                    supervisor.service_url,
                    deadline,
                    harnesses[1 - index]["session"],
                )
                public_payloads.extend(inactive_pages)
                self.assertEqual(inactive_actions, [])

                if (
                    not skipped
                    and agent_phase_ends >= 6
                    and harness["place"] == 1
                ):
                    prior_sequence = (
                        health["last_phase_end"]["sequence"]
                        if health["last_phase_end"] is not None else 0
                    )
                    skipped = True
                    while time.monotonic() < deadline:
                        skipped_health = self._health(
                            harness["workspace"], supervisor.service_url,
                            deadline, harness["session"],
                        )
                        public_payloads.append(skipped_health)
                        event = skipped_health["last_phase_end"]
                        if (
                            event is not None
                            and event["sequence"] > prior_sequence
                            and event["source"] == "timeout"
                        ):
                            timeout_event = event
                            self.assertEqual(event["place"], 1)
                            self.assertEqual(event["controller_label"], harness["label"])
                            break
                        time.sleep(0.05)
                    self.assertIsNotNone(timeout_event)
                    continue

                overview = self._state(
                    harness["workspace"], supervisor.service_url,
                    deadline, harness["session"], "overview",
                )
                public_payloads.append(overview)
                overview_item = overview["page"]["items"][0]
                self.assertTrue(overview_item["active_phase"])
                self.assertTrue(overview_item["phase_ready"])
                self.assertEqual(
                    (overview_item["turn"], overview_item["phase"]),
                    (health["phase"]["turn"], health["phase"]["phase"]),
                )

                actions, pages = self._legal_actions(
                    harness["workspace"], supervisor.service_url,
                    deadline, harness["session"],
                )
                public_payloads.extend(pages)
                if not harness["research_set"]:
                    prior_target_id = overview_item["research"]["target_id"]
                    research = next(
                        action for action in actions
                        if action["kind"] == "research.set_target"
                    )
                    research_receipt = self._batch(
                        harness["workspace"], supervisor.service_url,
                        deadline, harness["session"], research,
                    )
                    public_payloads.append(research_receipt)
                    self.assertEqual(research_receipt["receipt_state"], "applied")
                    changed_overview = self._state(
                        harness["workspace"], supervisor.service_url,
                        deadline, harness["session"], "overview",
                    )
                    public_payloads.append(changed_overview)
                    changed_target_id = changed_overview[
                        "page"
                    ]["items"][0]["research"]["target_id"]
                    self.assertNotEqual(changed_target_id, prior_target_id)
                    self.assertEqual(
                        changed_target_id,
                        research["subject"]["target"]["id"],
                    )
                    harness["research_set"] = True
                    harness["research_batch"] = research_receipt["batch_id"]
                    if durable_batch is None:
                        durable_batch = research_receipt["batch_id"]
                        durable_owner = index
                    actions, pages = self._legal_actions(
                        harness["workspace"], supervisor.service_url,
                        deadline, harness["session"],
                    )
                    public_payloads.extend(pages)

                phase_end = next(
                    action for action in actions
                    if action["kind"] == "phase.end"
                )
                phase_receipt = self._batch(
                    harness["workspace"], supervisor.service_url,
                    deadline, harness["session"], phase_end,
                )
                public_payloads.append(phase_receipt)
                self.assertEqual(
                    phase_receipt["receipt_state"], "applied",
                    {
                        "phase": health["phase"],
                        "place": harness["place"],
                        "receipt": phase_receipt,
                    },
                )
                resolved = self._receipt(
                    harness["workspace"], supervisor.service_url,
                    deadline, harness["session"], phase_receipt["batch_id"],
                )
                public_payloads.append(resolved)
                self.assertEqual(resolved, phase_receipt)
                agent_phase_ends += 1
            else:
                game_id = created["game_id"]
                self.fail({
                    "reason": "two-harness match exceeded its 240 second watchdog",
                    "agent_phase_ends": agent_phase_ends,
                    "health": healths,
                    "status": request_json(
                        "GET",
                        f"{supervisor.service_url}/v1/games/{game_id}/status",
                    ),
                    "phase_events": request_json(
                        "GET",
                        f"{supervisor.service_url}/v1/games/{game_id}/phase-events"
                        "?after_sequence=0&limit=100",
                    )["phase_events"][-10:],
                })

            self.assertTrue(skipped)
            self.assertIsNotNone(timeout_event)
            self.assertTrue(all(harness["research_set"] for harness in harnesses))
            self.assertIsNotNone(durable_batch)
            self.assertIsNotNone(durable_owner)

            for harness in harnesses:
                research_durable = self._receipt(
                    harness["workspace"], supervisor.service_url,
                    deadline, harness["session"], harness["research_batch"],
                )
                public_payloads.append(research_durable)
                self.assertEqual(research_durable["receipt_state"], "applied")

            durable_again = self._receipt(
                harnesses[durable_owner]["workspace"],
                supervisor.service_url,
                deadline,
                harnesses[durable_owner]["session"],
                durable_batch,
            )
            public_payloads.append(durable_again)
            self.assertEqual(durable_again["receipt_state"], "applied")
            hidden_again = self._receipt(
                harnesses[1 - durable_owner]["workspace"],
                supervisor.service_url,
                deadline,
                harnesses[1 - durable_owner]["session"],
                durable_batch,
                expected_returncode=2,
            )
            public_payloads.append(hidden_again)
            self.assertEqual(hidden_again["error"]["code"], "invalid_request")

            game_id = created["game_id"]
            phase_events = request_json(
                "GET",
                f"{supervisor.service_url}/v1/games/{game_id}/phase-events"
                "?after_sequence=0&limit=100",
            )
            status = request_json(
                "GET", f"{supervisor.service_url}/v1/games/{game_id}/status",
            )
            result = request_json(
                "GET", f"{supervisor.service_url}/v1/games/{game_id}/result",
            )
            public_payloads.extend((phase_events, status, result))
            events = phase_events["phase_events"]
            expected_phases = self.MATCH_TURNS * 2
            self.assertEqual(len(events), expected_phases, events)
            self.assertEqual(
                len({event["sequence"] for event in events}), expected_phases,
            )
            self.assertEqual(
                [(event["turn"], event["phase"]) for event in events],
                [
                    (turn, phase)
                    for turn in range(1, self.MATCH_TURNS + 1)
                    for phase in range(2)
                ],
            )
            self.assertEqual(
                sum(event["source"] == "agent" for event in events),
                expected_phases - 1,
                [
                    (
                        event["turn"], event["phase"], event["source"],
                        event["elapsed_s"],
                    )
                    for event in events
                ],
            )
            self.assertEqual(
                sum(event["source"] == "timeout" for event in events), 1,
            )
            self.assertEqual(agent_phase_ends, expected_phases - 1)
            self.assertFalse(phase_events["has_more"])
            self.assertTrue(phase_events["complete"])
            self.assertEqual(status["state"], "completed")
            self.assertTrue(status["benchmark_valid"])
            self.assertEqual(status["invalid_reasons"], [])
            self.assertGreaterEqual(status["current_turn"], self.MATCH_TURNS)
            self.assertEqual(result["state"], "completed")
            self.assertTrue(result["benchmark_valid"])
            self.assertTrue(all(health["phase"] is None for health in healths))
            self.assertTrue(all(
                event["receipt_state"] == "applied" for event in events
            ))
            timeout_rows = [
                event for event in events if event["source"] == "timeout"
            ]
            self.assertEqual(timeout_rows[0]["resolution"], "advanced")
            self.assertTrue(all(
                event["place"] == event["phase"] + 1 for event in events
            ))
            expected_identity = {
                1: {
                    "seat_id": "place-1",
                    "player_name": "AgentPlace1",
                    "player_color": "#0067A5",
                    "controller_label": "codex-e2e-model",
                },
                2: {
                    "seat_id": "place-2",
                    "player_name": "AgentPlace2",
                    "player_color": "#F38400",
                    "controller_label": "claude-e2e-model",
                },
            }
            for event in events:
                for field, expected in expected_identity[event["place"]].items():
                    self.assertEqual(event[field], expected)
            self.assertNotIn("next", self._player_commands)
            self.assertNotIn("act", self._player_commands)
            self.assertTrue(set(self._player_commands).issubset({
                "join", "health", "state", "legal", "batch", "receipt",
                "wait",
            }))

            event_payload = json.dumps(events, sort_keys=True).casefold()
            for private_name in (
                "agent_id", "batch_id", "generation", "revision",
                "action_id", "slot", "hash", "bearer", "native_ref",
            ):
                self.assertNotIn(private_name, event_payload)
            self._assert_safe_payloads(public_payloads)
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
