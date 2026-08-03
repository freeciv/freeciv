import json
import os
import stat
import subprocess
import tempfile
import unittest
from argparse import Namespace
from pathlib import Path
from unittest.mock import patch

from agent_eval.client import write_private_json
from agent_eval.local_stack import (
    PortlessAliases,
    Route,
    StackError,
    _parser,
    _read_routes,
    _resolve_agent_binary,
    replay,
)


class FakePortless:
    def __init__(self, routes_path: Path):
        self.routes_path = routes_path
        self.commands = []

    def _read(self):
        if not self.routes_path.exists():
            return []
        return json.loads(self.routes_path.read_text(encoding="utf-8"))

    def _write(self, routes):
        self.routes_path.parent.mkdir(parents=True, exist_ok=True)
        self.routes_path.write_text(json.dumps(routes), encoding="utf-8")

    def __call__(self, command):
        self.commands.append(tuple(command))
        arguments = list(command)[1:]
        routes = self._read()
        if arguments[:2] == ["alias", "--remove"]:
            hostname = arguments[2] + ".localhost"
            routes = [row for row in routes if row["hostname"] != hostname]
        elif arguments and arguments[0] == "alias":
            hostname = arguments[1] + ".localhost"
            routes.append({
                "hostname": hostname,
                "port": int(arguments[2]),
                "pid": 0,
            })
        else:
            return subprocess.CompletedProcess(command, 2, "", "bad command")
        self._write(routes)
        return subprocess.CompletedProcess(command, 0, "", "")


class PortlessAliasTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.repo = self.root / "checkout"
        self.repo.mkdir()
        self.state = self.root / "portless"
        self.state.mkdir()
        self.routes = self.state / "routes.json"
        self.record = self.root / "private" / "routes.json"
        self.fake = FakePortless(self.routes)

    def tearDown(self):
        self.temporary.cleanup()

    def write_routes(self, *routes):
        self.routes.write_text(json.dumps([
            {"hostname": hostname, "port": port, "pid": pid}
            for hostname, port, pid in routes
        ]), encoding="utf-8")

    def manager(self, listener_commands=lambda port: []):
        return PortlessAliases(
            self.repo,
            self.state,
            self.record,
            runner=self.fake,
            listener_commands=listener_commands,
        )

    def test_unknown_alias_fails_closed_without_mutation(self):
        self.write_routes(("freeciv.localhost", 7001, 0))
        manager = self.manager()
        with self.assertRaisesRegex(StackError, "belongs to another service"):
            manager.install({"freeciv.localhost": 4111})
        self.assertEqual(self.fake.commands, [])
        self.assertEqual(_read_routes(self.routes)["freeciv.localhost"].port, 7001)

    def test_known_legacy_aliases_are_restored_and_never_forced(self):
        self.write_routes(
            ("freeciv.localhost", 5173, 0),
            ("freeciv-api.localhost", 8765, 0),
        )

        def listeners(port):
            if port == 5173:
                return [f"node {self.repo.resolve()}/agent_eval/viewer/vite.js"]
            return []

        manager = self.manager(listeners)
        manager.install({
            "freeciv.localhost": 4111,
            "freeciv-api.localhost": 4222,
        })
        current = _read_routes(self.routes)
        self.assertEqual(current["freeciv.localhost"], Route(
            "freeciv.localhost", 4111, 0,
        ))
        self.assertEqual(stat.S_IMODE(self.record.stat().st_mode), 0o600)
        manager.restore()
        current = _read_routes(self.routes)
        self.assertEqual(current["freeciv.localhost"].port, 5173)
        self.assertEqual(current["freeciv-api.localhost"].port, 8765)
        flattened = " ".join(" ".join(command) for command in self.fake.commands)
        self.assertNotIn("--force", flattened)
        self.assertNotIn("prune", flattened)
        self.assertNotIn("proxy stop", flattened)

    def test_live_checkout_record_prevents_second_stack(self):
        self.write_routes()
        write_private_json(self.record, {
            "repo_root": str(self.repo.resolve()),
            "owner_pid": os.getpid(),
            "routes": {},
        })
        with self.assertRaisesRegex(StackError, "still running"):
            self.manager().preflight()
        self.assertEqual(self.fake.commands, [])


class ReplayCommandTests(unittest.TestCase):
    def test_replay_only_probes_and_opens_public_url(self):
        game_id = "game_" + "a" * 24
        responses = {
            "https://freeciv.localhost/": b"<title>Freeciv Agent Arena</title>",
            f"https://freeciv.localhost/v1/games/{game_id}/status": json.dumps({
                "game_id": game_id,
            }).encode(),
        }
        with patch(
            "agent_eval.local_stack._public_curl",
            side_effect=lambda url: responses[url],
        ), patch(
            "agent_eval.local_stack.subprocess.run",
            return_value=subprocess.CompletedProcess([], 0, "", ""),
        ) as run:
            self.assertEqual(replay(Namespace(game_id=game_id, no_open=False)), 0)
        run.assert_called_once_with(
            ["open", f"https://freeciv.localhost/watch/{game_id}"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )

    def test_replay_failure_tells_user_to_start_stack(self):
        with patch(
            "agent_eval.local_stack._public_curl",
            side_effect=StackError("offline"),
        ):
            with self.assertRaisesRegex(StackError, "run `just start`"):
                replay(Namespace(game_id="", no_open=True))


class AgentBinaryWiringTests(unittest.TestCase):
    def test_default_is_exact_same_checkout_binary_and_override_is_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            checkout = root / "checkout"
            default = checkout / "build-control-v2/freeciv-agent"
            default.parent.mkdir(parents=True)
            default.write_text("agent", encoding="utf-8")
            default.chmod(0o700)
            override = root / "custom-agent"
            override.write_text("custom", encoding="utf-8")
            override.chmod(0o700)
            self.assertEqual(
                _resolve_agent_binary(checkout, None), default.resolve(),
            )
            self.assertEqual(
                _resolve_agent_binary(checkout, str(override)), override.resolve(),
            )
            parsed = _parser().parse_args([
                "start", "--repo-root", str(checkout),
                "--agent-binary", str(override),
            ])
            self.assertEqual(parsed.agent_binary, str(override))

    def test_missing_or_non_executable_agent_binary_fails_before_startup(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            checkout = root / "checkout"
            checkout.mkdir()
            with self.assertRaisesRegex(StackError, "run `just build`"):
                _resolve_agent_binary(checkout, None)
            override = root / "not-executable"
            override.write_text("agent", encoding="utf-8")
            override.chmod(0o600)
            with self.assertRaisesRegex(StackError, "not executable"):
                _resolve_agent_binary(checkout, str(override))

    def test_just_build_and_start_wire_the_same_checkout_agent_target(self):
        source = (
            Path(__file__).resolve().parents[2] / "justfile"
        ).read_text(encoding="utf-8")
        self.assertIn("meson setup build-control-v2", source)
        self.assertIn("meson compile -C build-control-v2 freeciv-agent", source)
        self.assertIn("-Dclients=agent", source)
        self.assertIn(
            '${AGENT_EVAL_AGENT_BINARY:-$PWD/build-control-v2/freeciv-agent}',
            source,
        )
        local_stack_source = (
            Path(__file__).resolve().parents[1] / "local_stack.py"
        ).read_text(encoding="utf-8")
        self.assertIn('"--agent-binary", str(agent_binary)', local_stack_source)


if __name__ == "__main__":
    unittest.main()
