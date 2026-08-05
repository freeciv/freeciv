from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path

from agent_eval.client import create_game, request_json
from agent_eval.supervisor import Supervisor, make_supervisor_server


@unittest.skipUnless(
    os.environ.get("FREECIV_AGENT_E2E") == "1",
    "set FREECIV_AGENT_E2E=1 for the isolated v2 playability smoke",
)
class V2PlayabilitySmokeRealE2ETests(unittest.TestCase):
    """Prove the short public-CLI loop without touching a live service."""

    WATCHDOG_S = 60.0
    # These player commands default to the compact text rendering; the
    # machine-consumer contract is their byte-identical --json output.
    JSON_FLAG_COMMANDS = frozenset({
        "join", "health", "turn", "state", "legal", "batch", "receipt",
        "retry",
    })

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
        harness: dict,
        service_url: str,
        deadline: float,
        *arguments: str,
        extra_environment: dict[str, str] | None = None,
    ) -> dict:
        remaining = deadline - time.monotonic()
        self.assertGreater(remaining, 0, "v2 playability watchdog expired")
        environment = dict(os.environ)
        environment.update({
            "AGENT_EVAL_SERVICE_URL": service_url,
            "PLAY_STATE_DIR": ".sessions",
        })
        if extra_environment:
            environment.update(extra_environment)
        self._player_commands.append(arguments[0])
        if arguments[0] in self.JSON_FLAG_COMMANDS and "--json" not in arguments:
            arguments = (*arguments, "--json")
        completed = subprocess.run(
            ("python3", "-B", "client.py", *arguments),
            cwd=harness["workspace"],
            env=environment,
            check=False,
            capture_output=True,
            text=True,
            timeout=max(0.1, min(35.0, remaining)),
        )
        self.assertEqual(
            completed.returncode,
            0,
            {
                "arguments": arguments,
                "stdout": completed.stdout,
                "stderr": completed.stderr,
            },
        )
        objects = []
        try:
            parsed = json.loads(completed.stdout)
        except json.JSONDecodeError:
            parsed = None
        if isinstance(parsed, dict):
            objects.append(parsed)
        else:
            for line in completed.stdout.splitlines():
                try:
                    parsed = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if isinstance(parsed, dict):
                    objects.append(parsed)
        self.assertTrue(
            objects,
            {"arguments": arguments, "stdout": completed.stdout},
        )
        return objects[-1]

    def _join(
        self,
        repository: Path,
        temporary_path: Path,
        service_url: str,
        created: dict,
        deadline: float,
        *,
        place: int,
        label: str,
    ) -> dict:
        harness = {
            "place": place,
            "label": label,
            "workspace": self._copy_player_workspace(
                repository / "play", temporary_path / f"harness-{place}",
            ),
        }
        joined = self._run_player(
            harness,
            service_url,
            deadline,
            "join",
            "--game-id", created["game_id"],
            "--name", label,
            "--place", str(place),
            extra_environment={
                "AGENT_EVAL_JOIN_TOKEN": created["join_token"],
            },
        )
        self.assertEqual(joined["control_protocol"], "full-control-v2")
        self.assertEqual(joined["place"], place)
        harness["session"] = Path(joined["session_file"])
        self.assertTrue(harness["session"].is_file())
        return harness

    def _health(
        self, harness: dict, service_url: str, deadline: float,
    ) -> dict:
        return self._run_player(
            harness,
            service_url,
            deadline,
            "health", "--session", str(harness["session"]),
        )

    def _wait_for_lobby_sidecars(
        self, harnesses: list[dict], service_url: str, deadline: float,
    ) -> None:
        last = []
        while time.monotonic() < deadline:
            last = [
                self._health(harness, service_url, deadline)
                for harness in harnesses
            ]
            if all(
                value["game_state"] == "lobby"
                and value["sidecar"]["state"] == "ready"
                and value["sidecar"].get("client_state") == "preparing"
                and value["observation_available"]
                for value in last
            ):
                return
            time.sleep(0.05)
        self.fail({"reason": "pregame sidecars were not ready", "health": last})

    def _state_pages(
        self,
        harness: dict,
        service_url: str,
        deadline: float,
        section: str,
    ) -> list[dict]:
        page = self._run_player(
            harness,
            service_url,
            deadline,
            "state", "--session", str(harness["session"]),
            "--section", section, "--limit", "16",
        )
        items = list(page["page"]["items"])
        cursor = page["page"]["next_cursor"]
        while cursor is not None:
            page = self._run_player(
                harness,
                service_url,
                deadline,
                "state", "--session", str(harness["session"]),
                "--cursor", cursor,
            )
            items.extend(page["page"]["items"])
            cursor = page["page"]["next_cursor"]
        self.assertEqual(len(items), page["page"]["total_items"])
        return items

    def _legal_actions(
        self,
        harness: dict,
        service_url: str,
        deadline: float,
        *,
        actor_id: str | None = None,
    ) -> list[dict]:
        arguments = [
            "legal", "--session", str(harness["session"]), "--limit", "16",
        ]
        if actor_id is not None:
            arguments.extend(("--actor-id", actor_id))
        page = self._run_player(
            harness, service_url, deadline, *arguments,
        )
        items = list(page["page"]["items"])
        cursor = page["page"]["next_cursor"]
        while cursor is not None:
            page = self._run_player(
                harness,
                service_url,
                deadline,
                "legal", "--session", str(harness["session"]),
                "--cursor", cursor,
            )
            items.extend(page["page"]["items"])
            cursor = page["page"]["next_cursor"]
        self.assertEqual(len(items), page["page"]["total_items"])
        return items

    def _apply(
        self,
        harness: dict,
        service_url: str,
        deadline: float,
        action: dict,
        arguments: dict | None = None,
    ) -> dict:
        disposition = self._run_player(
            harness,
            service_url,
            deadline,
            "batch", "--session", str(harness["session"]),
            "--action-id", action["action_id"],
            "--arguments", json.dumps(
                arguments or {}, sort_keys=True, separators=(",", ":"),
            ),
        )
        self.assertIn(
            disposition["disposition"], {"receipt_terminal", "receipt_poll"},
            disposition,
        )
        batch_id = disposition["batch_id"]
        receipt = disposition["receipt"]
        self.assertIsNotNone(receipt)
        while True:
            # Exercise the durable public receipt command even when batch
            # returned a terminal receipt immediately.
            receipt = self._run_player(
                harness,
                service_url,
                deadline,
                "receipt", "--session", str(harness["session"]),
                "--batch-id", batch_id,
            )
            if receipt["receipt_state"] != "accepted":
                break
            time.sleep(0.05)
        self.assertEqual(receipt["receipt_state"], "applied", receipt)
        return receipt

    def _configure(self, harness: dict, service_url: str, deadline: float) -> None:
        nations = self._state_pages(
            harness, service_url, deadline, "pregame_nations",
        )
        styles = self._state_pages(
            harness, service_url, deadline, "pregame_styles",
        )
        self.assertTrue(nations)
        self.assertTrue(styles)
        nation = nations[0]
        self.assertIn(nation["default_style_id"], {
            style["id"] for style in styles
        })
        actions = self._legal_actions(harness, service_url, deadline)
        configure = next(
            action for action in actions
            if action["kind"] == "pregame.configure"
        )
        leader_name = f"{harness['label']}-leader"
        leader_name = leader_name[0].upper() + leader_name[1:]
        self._apply(
            harness,
            service_url,
            deadline,
            configure,
            {
                "nation_id": nation["id"],
                "leader_name": leader_name,
                "is_male": harness["place"] % 2 == 1,
                "style_id": nation["default_style_id"],
            },
        )
        overview = self._state_pages(
            harness, service_url, deadline, "overview",
        )
        self.assertEqual(len(overview), 1)
        self.assertEqual(
            overview[0]["player"]["leader_name"],
            leader_name,
        )

    def _ready(self, harness: dict, service_url: str, deadline: float) -> None:
        actions = self._legal_actions(harness, service_url, deadline)
        ready = next(
            action for action in actions
            if action["kind"] == "pregame.set_ready"
        )
        self._apply(
            harness, service_url, deadline, ready, {"ready": True},
        )

    def _wait_active(
        self, harness: dict, service_url: str, deadline: float,
    ) -> dict:
        remaining = deadline - time.monotonic()
        value = self._run_player(
            harness,
            service_url,
            deadline,
            "wait", "--session", str(harness["session"]),
            "--wait-s", f"{min(20.0, max(0.1, remaining - 0.1)):g}",
            "--poll-s", "0.05",
        )
        self.assertEqual(value["wake_reason"], "phase_active", value)
        return value["health"]

    def _play_one_phase(
        self, harness: dict, service_url: str, deadline: float,
    ) -> dict:
        active = self._wait_active(harness, service_url, deadline)
        overview = self._state_pages(
            harness, service_url, deadline, "overview",
        )
        self.assertEqual(len(overview), 1)
        self.assertTrue(overview[0]["active_phase"])
        player_actions = self._legal_actions(
            harness, service_url, deadline,
            actor_id=overview[0]["player"]["id"],
        )
        player_kinds = {action["kind"] for action in player_actions}
        self.assertIn("player.propose_server_setting", player_kinds)
        self.assertIn("government.revolution", player_kinds)
        self.assertNotIn("player.send_chat", player_kinds)
        units = self._state_pages(
            harness, service_url, deadline, "units",
        )
        self.assertTrue(units)
        for unit in units:
            self.assertTrue(self._legal_actions(
                harness, service_url, deadline, actor_id=unit["id"],
            ))
        actions = self._legal_actions(harness, service_url, deadline)
        research = next(
            action for action in actions
            if action["kind"] == "research.set_target"
        )
        self._apply(harness, service_url, deadline, research)
        actions = self._legal_actions(harness, service_url, deadline)
        phase_end = next(
            action for action in actions if action["kind"] == "phase.end"
        )
        self._apply(harness, service_url, deadline, phase_end)
        return active

    def _wait_for_any_active(
        self,
        harnesses: list[dict],
        service_url: str,
        deadline: float,
        acted_places: set[int],
    ) -> dict:
        last = []
        while time.monotonic() < deadline:
            last = [
                (harness, self._health(harness, service_url, deadline))
                for harness in harnesses
                if harness["place"] not in acted_places
            ]
            active = [
                harness for harness, health in last
                if isinstance(health["phase"], dict)
                and health["phase"]["active"]
                and health["phase"]["state"] == "awaiting_agent"
                and health["observation_available"]
            ]
            if active:
                return active[0]
            time.sleep(0.05)
        self.fail({"reason": "no unacted external phase became active", "last": last})

    def _assert_cli_loop(self) -> None:
        commands = set(self._player_commands)
        self.assertTrue({"wait", "state", "legal", "batch", "receipt"} <= commands)
        self.assertNotIn("next", commands)
        self.assertNotIn("act", commands)

    def _start_isolated(self, temporary_path: Path, admin_token: str):
        repository = Path(__file__).parents[2]
        server_binary = repository / "build-agent" / "freeciv-server"
        agent_binary = repository / "build-control-v2" / "freeciv-agent"
        self.assertTrue(os.access(server_binary, os.X_OK), server_binary)
        self.assertTrue(os.access(agent_binary, os.X_OK), agent_binary)
        supervisor = Supervisor(
            temporary_path / "runs",
            admin_token,
            binary=server_binary,
            agent_binary=agent_binary,
        )
        server = make_supervisor_server(supervisor, "127.0.0.1", 0)
        server_thread = threading.Thread(
            target=server.serve_forever,
            name=f"{admin_token}-http",
            daemon=True,
        )
        server_thread.start()
        return repository, supervisor, server, server_thread

    def test_two_external_cli_harnesses_start_and_advance_one_turn(self):
        temporary = tempfile.TemporaryDirectory(
            prefix="freeciv-v2-playable-two-harness-",
        )
        temporary_path = Path(temporary.name)
        supervisor = server = server_thread = None
        self._player_commands = []
        deadline = time.monotonic() + self.WATCHDOG_S
        try:
            repository, supervisor, server, server_thread = self._start_isolated(
                temporary_path, "isolated-playable-two-admin",
            )
            created = create_game(
                supervisor.service_url,
                "isolated-playable-two-admin",
                {
                    "mode": "multiplayer",
                    "places": 2,
                    "turns": 2,
                    "seed": 909,
                    "ruleset": "classic",
                    "objective": "Short two-harness v2 playability smoke.",
                    "timing_mode": "infinite",
                    "action_timeout_s": None,
                    "lobby_timeout_s": 30,
                    "frame_interval": 0,
                    "frame_zoom": 1,
                    "control_protocol": "full-control-v2",
                },
            )
            harnesses = [
                self._join(
                    repository, temporary_path, supervisor.service_url,
                    created, deadline, place=1, label="codex-playable-e2e",
                ),
                self._join(
                    repository, temporary_path, supervisor.service_url,
                    created, deadline, place=2, label="claude-playable-e2e",
                ),
            ]
            self._wait_for_lobby_sidecars(
                harnesses, supervisor.service_url, deadline,
            )
            for harness in harnesses:
                self._configure(harness, supervisor.service_url, deadline)
            self._ready(harnesses[0], supervisor.service_url, deadline)
            game = supervisor.game(created["game_id"])
            self.assertEqual(game.state, "lobby")
            self.assertEqual(game.start_count, 0)
            self._ready(harnesses[1], supervisor.service_url, deadline)
            self.assertEqual(game.start_count, 1)

            acted_places: set[int] = set()
            while len(acted_places) < 2:
                harness = self._wait_for_any_active(
                    harnesses,
                    supervisor.service_url,
                    deadline,
                    acted_places,
                )
                active = self._play_one_phase(
                    harness, supervisor.service_url, deadline,
                )
                self.assertEqual(active["phase"]["turn"], 1)
                acted_places.add(harness["place"])

            last_status = None
            while time.monotonic() < deadline:
                last_status = request_json(
                    "GET",
                    f"{supervisor.service_url}/v1/games/"
                    f"{created['game_id']}/status",
                )
                if (
                    isinstance(last_status["current_turn"], int)
                    and last_status["current_turn"] >= 2
                ):
                    break
                time.sleep(0.05)
            self.assertIsInstance(last_status["current_turn"], int)
            self.assertGreaterEqual(last_status["current_turn"], 2)
            commands = (game.episode / "server.commands").read_text(
                encoding="utf-8",
            )
            self.assertNotIn("\nstart\n", f"\n{commands}\n")
            self._assert_cli_loop()
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

    def test_external_cli_harness_and_native_ai_advance_to_next_turn(self):
        temporary = tempfile.TemporaryDirectory(
            prefix="freeciv-v2-playable-native-",
        )
        temporary_path = Path(temporary.name)
        supervisor = server = server_thread = None
        self._player_commands = []
        deadline = time.monotonic() + self.WATCHDOG_S
        try:
            repository, supervisor, server, server_thread = self._start_isolated(
                temporary_path, "isolated-playable-native-admin",
            )
            created = create_game(
                supervisor.service_url,
                "isolated-playable-native-admin",
                {
                    "mode": "single",
                    "places": 2,
                    "turns": 2,
                    "seed": 909,
                    "ruleset": "classic",
                    "objective": "Short external-vs-native v2 smoke.",
                    "timing_mode": "infinite",
                    "action_timeout_s": None,
                    "lobby_timeout_s": 30,
                    "frame_interval": 0,
                    "frame_zoom": 1,
                    "control_protocol": "full-control-v2",
                },
            )
            harness = self._join(
                repository, temporary_path, supervisor.service_url,
                created, deadline, place=1, label="codex-native-playable-e2e",
            )
            self._wait_for_lobby_sidecars(
                [harness], supervisor.service_url, deadline,
            )
            self._configure(harness, supervisor.service_url, deadline)
            self._ready(harness, supervisor.service_url, deadline)
            first = self._play_one_phase(
                harness, supervisor.service_url, deadline,
            )
            self.assertEqual(first["phase"]["turn"], 1)

            # The only actor between these two wakes is Freeciv's native AI.
            second = self._wait_active(
                harness, supervisor.service_url, deadline,
            )
            self.assertGreaterEqual(second["phase"]["turn"], 2)
            second_overview = self._state_pages(
                harness, supervisor.service_url, deadline, "overview",
            )
            self.assertEqual(len(second_overview), 1)
            self.assertTrue(second_overview[0]["active_phase"])
            game = supervisor.game(created["game_id"])
            self.assertEqual(game.start_count, 1)
            commands = (game.episode / "server.commands").read_text(
                encoding="utf-8",
            )
            self.assertNotIn("\nstart\n", f"\n{commands}\n")
            self._assert_cli_loop()
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
