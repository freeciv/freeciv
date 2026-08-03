"""Own the local supervisor, replay gateway, Vite, and Portless routes.

The launcher deliberately binds new loopback ports and records every child it
creates.  It never adopts, replaces, or signals an unrelated listener.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import secrets
import signal
import socket
import stat
import subprocess
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Mapping, Sequence

from .client import load_private_json, write_private_json


VIEWER_PUBLIC_URL = "https://freeciv.localhost"
API_PUBLIC_URL = "https://freeciv-api.localhost"
GAME_ID_RE = re.compile(r"^game_[A-Za-z0-9_-]{20,80}$")


class StackError(RuntimeError):
    """A concise, user-actionable local stack error."""


@dataclass(frozen=True)
class Route:
    hostname: str
    port: int
    pid: int


CommandRunner = Callable[[Sequence[str]], subprocess.CompletedProcess[str]]


def _run(command: Sequence[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        list(command), text=True, capture_output=True, check=False,
    )


def _read_routes(path: Path) -> dict[str, Route]:
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return {}
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise StackError(f"cannot safely read Portless routes: {exc}") from exc
    if not isinstance(raw, list):
        raise StackError("Portless routes.json is not a list")
    routes: dict[str, Route] = {}
    for value in raw:
        if not isinstance(value, dict):
            raise StackError("Portless routes.json contains an invalid route")
        hostname = value.get("hostname")
        port = value.get("port")
        pid = value.get("pid", 0)
        if (
            not isinstance(hostname, str)
            or isinstance(port, bool) or not isinstance(port, int)
            or not 1 <= port <= 65535
            or isinstance(pid, bool) or not isinstance(pid, int) or pid < 0
        ):
            raise StackError("Portless routes.json contains an invalid route")
        routes[hostname] = Route(hostname, port, pid)
    return routes


def _private_record(path: Path) -> dict[str, Any] | None:
    try:
        info = path.lstat()
        if (
            path.is_symlink() or not stat.S_ISREG(info.st_mode)
            or info.st_uid != os.getuid()
            or stat.S_IMODE(info.st_mode) != 0o600
        ):
            raise StackError(f"unsafe local stack record: {path}")
        value = load_private_json(path)
    except FileNotFoundError:
        return None
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        raise StackError(f"cannot safely read local stack record: {exc}") from exc
    if not isinstance(value, dict):
        raise StackError("local stack record is not an object")
    return value


def _pid_is_alive(pid: Any) -> bool:
    if isinstance(pid, bool) or not isinstance(pid, int) or pid <= 1:
        return False
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def _listener_commands(port: int) -> list[str]:
    lsof = subprocess.run(
        ["lsof", "-nP", f"-iTCP:{port}", "-sTCP:LISTEN", "-t"],
        text=True, capture_output=True, check=False,
    )
    if lsof.returncode not in {0, 1}:
        raise StackError("lsof is required to validate a legacy Portless route")
    pids = sorted({line.strip() for line in lsof.stdout.splitlines() if line.strip()})
    commands: list[str] = []
    for pid in pids:
        result = subprocess.run(
            ["ps", "-ww", "-p", pid, "-o", "command="],
            text=True, capture_output=True, check=False,
        )
        if result.returncode != 0 or not result.stdout.strip():
            raise StackError("a legacy Portless listener changed during validation")
        cwd_result = subprocess.run(
            ["lsof", "-a", "-p", pid, "-d", "cwd", "-Fn"],
            text=True, capture_output=True, check=False,
        )
        cwd = next((
            line[1:] for line in cwd_result.stdout.splitlines()
            if line.startswith("n")
        ), "")
        commands.append(result.stdout.strip() + "\n" + cwd)
    return commands


class PortlessAliases:
    """Collision-safe static aliases with exact restoration on shutdown."""

    def __init__(
        self,
        repo_root: Path,
        state_dir: Path,
        record_path: Path,
        *,
        runner: CommandRunner = _run,
        listener_commands: Callable[[int], list[str]] = _listener_commands,
    ):
        self.repo_root = repo_root.resolve()
        self.state_dir = state_dir.expanduser().resolve()
        self.routes_path = self.state_dir / "routes.json"
        self.record_path = record_path.resolve()
        self.runner = runner
        self.listener_commands = listener_commands
        self.previous: dict[str, Route | None] = {}
        self.installed: dict[str, int] = {}

    @staticmethod
    def _name(hostname: str) -> str:
        suffix = ".localhost"
        if not hostname.endswith(suffix) or hostname == suffix:
            raise StackError(f"unsupported Portless hostname: {hostname}")
        return hostname.removesuffix(suffix)

    def _command(self, *arguments: str) -> None:
        result = self.runner(("portless", *arguments))
        if result.returncode != 0:
            detail = (result.stderr or result.stdout).strip()
            raise StackError(detail or f"portless {' '.join(arguments)} failed")

    def _record_allows(self, route: Route) -> bool:
        record = _private_record(self.record_path)
        if record is None or record.get("repo_root") != str(self.repo_root):
            return False
        recorded = record.get("routes")
        if not isinstance(recorded, dict) or recorded.get(route.hostname) != route.port:
            return False
        owner_pid = record.get("owner_pid")
        if _pid_is_alive(owner_pid):
            raise StackError(
                "another `just start` from this checkout is still running"
            )
        return True

    def _legacy_allows(self, route: Route) -> bool:
        expected = {
            "freeciv.localhost": (5173, "agent_eval/viewer"),
            "freeciv-api.localhost": (8765, "-m agent_eval supervisor"),
        }.get(route.hostname)
        if expected is None or route.port != expected[0] or route.pid != 0:
            return False
        commands = self.listener_commands(route.port)
        if not commands:
            return True
        root = str(self.repo_root)
        marker = expected[1]
        return all(root in command and marker in command for command in commands)

    def preflight(self) -> None:
        record = _private_record(self.record_path)
        if record is not None and record.get("repo_root") == str(self.repo_root):
            if _pid_is_alive(record.get("owner_pid")):
                raise StackError(
                    "another `just start` from this checkout is still running"
                )
        routes = _read_routes(self.routes_path)
        for hostname in ("freeciv.localhost", "freeciv-api.localhost"):
            current = routes.get(hostname)
            if current is None:
                continue
            if not self._record_allows(current) and not self._legacy_allows(current):
                raise StackError(
                    f"Portless route https://{hostname} belongs to another "
                    f"service on port {current.port}; it was not changed"
                )

    def _replace(self, hostname: str, port: int) -> None:
        name = self._name(hostname)
        current = _read_routes(self.routes_path).get(hostname)
        if current is not None:
            self._command("alias", "--remove", name)
        try:
            self._command("alias", name, str(port))
        except Exception:
            if current is not None:
                try:
                    self._command("alias", name, str(current.port))
                except Exception:
                    pass
            raise
        updated = _read_routes(self.routes_path).get(hostname)
        if updated != Route(hostname, port, 0):
            raise StackError(f"Portless did not register https://{hostname} safely")

    def install(self, desired: Mapping[str, int]) -> None:
        self.preflight()
        original = _read_routes(self.routes_path)
        try:
            for hostname, port in desired.items():
                current = original.get(hostname)
                self.previous[hostname] = current
                if current == Route(hostname, port, 0):
                    self.installed[hostname] = port
                    continue
                if current is not None and (
                    not self._record_allows(current)
                    and not self._legacy_allows(current)
                ):
                    raise StackError(
                        f"Portless route https://{hostname} changed during startup; "
                        "it was not replaced"
                    )
                self._replace(hostname, port)
                self.installed[hostname] = port
            write_private_json(self.record_path, {
                "schema_version": 1,
                "kind": "freeciv-local-stack-portless-routes",
                "repo_root": str(self.repo_root),
                "owner_pid": os.getpid(),
                "routes": dict(self.installed),
            })
        except Exception:
            self.restore()
            raise

    def restore(self) -> None:
        for hostname, installed_port in reversed(tuple(self.installed.items())):
            try:
                current = _read_routes(self.routes_path).get(hostname)
            except StackError:
                continue
            if current != Route(hostname, installed_port, 0):
                continue
            previous = self.previous.get(hostname)
            try:
                if previous is None:
                    self._command("alias", "--remove", self._name(hostname))
                elif previous.port != installed_port:
                    self._replace(hostname, previous.port)
            except StackError:
                continue
        try:
            record = _private_record(self.record_path)
        except StackError:
            record = None
        if (
            record is not None
            and record.get("repo_root") == str(self.repo_root)
            and record.get("owner_pid") == os.getpid()
        ):
            self.record_path.unlink(missing_ok=True)
        self.installed.clear()


def _wait_private_ready(
    path: Path, process: subprocess.Popen[Any], *, timeout_s: float = 20,
) -> dict[str, Any]:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise StackError(f"service exited during startup with code {process.returncode}")
        try:
            value = _private_record(path)
        except StackError:
            value = None
        if isinstance(value, dict) and value.get("pid") == process.pid:
            return value
        time.sleep(0.05)
    raise StackError("service did not publish readiness before the startup deadline")


def _free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])


def _http_bytes(url: str, timeout_s: float = 3) -> bytes:
    with urllib.request.urlopen(url, timeout=timeout_s) as response:
        if response.status != 200:
            raise StackError(f"health check returned HTTP {response.status}")
        return response.read(8 * 1024 * 1024)


def _wait_http(url: str, process: subprocess.Popen[Any], marker: bytes) -> None:
    deadline = time.monotonic() + 20
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise StackError(f"service exited during startup with code {process.returncode}")
        try:
            if marker in _http_bytes(url, 1):
                return
        except (OSError, urllib.error.URLError, StackError) as exc:
            last_error = exc
        time.sleep(0.1)
    raise StackError(f"service health check failed: {last_error or url}")


def _terminate_owned(process: subprocess.Popen[Any], sig: int) -> None:
    if process.poll() is not None:
        return
    try:
        os.kill(process.pid, sig)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=8)
    except subprocess.TimeoutExpired:
        try:
            os.kill(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            pass


def _public_curl(url: str) -> bytes:
    result = subprocess.run(
        ["curl", "--silent", "--show-error", "--fail", "--max-time", "4", url],
        capture_output=True, check=False,
    )
    if result.returncode != 0:
        raise StackError(result.stderr.decode(errors="replace").strip() or "unavailable")
    return result.stdout


def _resolve_agent_binary(repo_root: Path, override: str | None) -> Path:
    candidate = (
        Path(override).expanduser().resolve()
        if override
        else (repo_root / "build-control-v2/freeciv-agent").resolve()
    )
    if not candidate.is_file() or not os.access(candidate, os.X_OK):
        if override:
            raise StackError(
                f"full-control-v2 agent binary is not executable: {candidate}"
            )
        raise StackError(
            "same-checkout full-control-v2 client is not built; run `just build`"
        )
    return candidate


def start_stack(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).expanduser().resolve()
    state_root = Path(args.state_dir).expanduser().resolve()
    stack_root = state_root / "local-stack"
    stack_root.mkdir(parents=True, exist_ok=True)
    portless_state = Path(
        os.environ.get("PORTLESS_STATE_DIR", str(Path.home() / ".portless"))
    )
    aliases = PortlessAliases(
        repo_root, portless_state, stack_root / "portless-routes.json",
    )
    aliases.preflight()

    python = sys.executable
    node = args.node or "node"
    vite_entry = repo_root / "agent_eval/viewer/node_modules/vite/bin/vite.js"
    binary = repo_root / "build-agent/freeciv-server"
    agent_binary = _resolve_agent_binary(repo_root, args.agent_binary)
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise StackError("Freeciv server is not built; run `just build`")
    if not vite_entry.is_file():
        raise StackError("Vite dependencies are missing; run `just replay-install`")
    if subprocess.run(
        ["portless", "--version"], capture_output=True, check=False,
    ).returncode != 0:
        raise StackError("Portless is required; install it before `just start`")

    invocation = f"{os.getpid()}-{secrets.token_hex(5)}"
    ready_supervisor = stack_root / f"supervisor-{invocation}.json"
    ready_gateway = stack_root / f"gateway-{invocation}.json"
    children: list[tuple[str, subprocess.Popen[Any], Any, int]] = []
    routes_installed = False

    def spawn(label: str, command: list[str], log_name: str, **kwargs: Any):
        log = (stack_root / log_name).open("ab", buffering=0)
        process = subprocess.Popen(
            command,
            cwd=kwargs.pop("cwd", repo_root),
            env=kwargs.pop("env", None),
            stdin=subprocess.DEVNULL,
            stdout=log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
            **kwargs,
        )
        children.append((label, process, log, signal.SIGTERM))
        return process

    try:
        supervisor = spawn(
            "supervisor",
            [
                python, "-B", "-m", "agent_eval", "supervisor",
                "--host", "127.0.0.1", "--port", "0",
                "--public-url", API_PUBLIC_URL,
                "--runs-root", str((state_root / "runs").resolve()),
                "--ready-file", str(ready_supervisor),
                "--agent-binary", str(agent_binary),
            ],
            f"supervisor-{invocation}.log",
            env=os.environ.copy(),
        )
        children[-1] = (*children[-1][:3], signal.SIGINT)
        supervisor_ready = _wait_private_ready(ready_supervisor, supervisor)
        supervisor_raw = supervisor_ready.get("internal_service_url")
        if (
            supervisor_ready.get("service_url") != API_PUBLIC_URL
            or not isinstance(supervisor_raw, str)
            or not supervisor_raw.startswith("http://127.0.0.1:")
        ):
            raise StackError("supervisor published unsafe listener metadata")
        _wait_http(supervisor_raw + "/health", supervisor, b'"ok":true')

        gateway = spawn(
            "replay gateway",
            [
                python, "-B", "-m", "agent_eval.replay_gateway",
                "--host", "127.0.0.1", "--port", "0",
                "--service-url", supervisor_raw,
                "--viewer-public-url", VIEWER_PUBLIC_URL,
                "--runs-root", str((state_root / "runs").resolve()),
                "--cache-root", str((state_root / "replay-cache").resolve()),
                "--repo-root", str(repo_root),
                "--ready-file", str(ready_gateway),
            ],
            f"gateway-{invocation}.log",
        )
        gateway_ready = _wait_private_ready(ready_gateway, gateway)
        gateway_raw = gateway_ready.get("url")
        if (
            gateway_ready.get("viewer_public_url") != VIEWER_PUBLIC_URL
            or not isinstance(gateway_raw, str)
            or not gateway_raw.startswith("http://127.0.0.1:")
        ):
            raise StackError("replay gateway published unsafe listener metadata")
        _wait_http(gateway_raw + "/health", gateway, b'"ok":true')

        vite_port = _free_port()
        vite_raw = f"http://127.0.0.1:{vite_port}"
        vite_env = os.environ.copy()
        vite_env["AGENT_EVAL_SERVICE_URL"] = gateway_raw
        vite = spawn(
            "Vite viewer",
            [
                node, str(vite_entry), "--host", "127.0.0.1",
                "--port", str(vite_port), "--strictPort",
            ],
            f"vite-{invocation}.log",
            cwd=repo_root / "agent_eval/viewer",
            env=vite_env,
        )
        _wait_http(vite_raw + "/", vite, b"<title>Freeciv Agent Arena</title>")
        _wait_http(vite_raw + "/v1/games", vite, b'"games"')

        aliases.install({
            "freeciv.localhost": vite_port,
            "freeciv-api.localhost": int(supervisor_raw.rsplit(":", 1)[1]),
        })
        routes_installed = True
        if b"Freeciv Agent Arena" not in _public_curl(VIEWER_PUBLIC_URL + "/"):
            raise StackError("Portless viewer route returned the wrong application")
        json.loads(_public_curl(VIEWER_PUBLIC_URL + "/v1/games"))
        health = json.loads(_public_curl(API_PUBLIC_URL + "/health"))
        if not isinstance(health, dict) or health.get("ok") is not True:
            raise StackError("Portless API route returned an invalid health response")

        print("Freeciv agent stack is ready", flush=True)
        print(f"  arena: {VIEWER_PUBLIC_URL}", flush=True)
        print(f"  api:   {API_PUBLIC_URL}", flush=True)
        print("Ctrl-C stops only the three services started by this command.", flush=True)
        while True:
            for label, process, _, _ in children:
                if process.poll() is not None:
                    raise StackError(
                        f"{label} exited unexpectedly with code {process.returncode}"
                    )
            time.sleep(0.25)
    except KeyboardInterrupt:
        return 0
    finally:
        if routes_installed:
            aliases.restore()
        for _, process, _, stop_signal in reversed(children):
            _terminate_owned(process, stop_signal)
        for _, _, log, _ in children:
            log.close()
        ready_supervisor.unlink(missing_ok=True)
        ready_gateway.unlink(missing_ok=True)


def replay(args: argparse.Namespace) -> int:
    game_id = args.game_id.strip()
    if game_id and GAME_ID_RE.fullmatch(game_id) is None:
        raise StackError("a valid game ID is required")
    try:
        root = _public_curl(VIEWER_PUBLIC_URL + "/")
        if b"<title>Freeciv Agent Arena</title>" not in root:
            raise StackError("wrong application")
        path = "/v1/games"
        if game_id:
            path += f"/{game_id}/status"
        value = json.loads(_public_curl(VIEWER_PUBLIC_URL + path))
        if game_id:
            if not isinstance(value, dict) or value.get("game_id") != game_id:
                raise StackError("game not found")
        elif not isinstance(value, dict) or not isinstance(value.get("games"), list):
            raise StackError("invalid games index")
    except (OSError, ValueError, json.JSONDecodeError, StackError) as exc:
        raise StackError(
            "the Freeciv arena is not running or the game is unknown; "
            "run `just start` and try again"
        ) from exc
    url = VIEWER_PUBLIC_URL + (f"/watch/{game_id}" if game_id else "")
    print(url, flush=True)
    if not args.no_open and not os.environ.get("AGENT_EVAL_NO_OPEN"):
        opener = "open" if sys.platform == "darwin" else "xdg-open"
        result = subprocess.run(
            [opener, url], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            check=False,
        )
        if result.returncode != 0:
            raise StackError(f"could not open {url}; open it manually")
    return 0


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="python3 -m agent_eval.local_stack")
    sub = parser.add_subparsers(dest="command", required=True)
    start = sub.add_parser("start")
    start.add_argument("--repo-root", default=str(Path.cwd()))
    start.add_argument("--state-dir", default=".agent-eval")
    start.add_argument("--node")
    start.add_argument(
        "--agent-binary",
        help=(
            "full-control-v2 client executable (default: same-checkout "
            "build-control-v2/freeciv-agent)"
        ),
    )
    show = sub.add_parser("replay")
    show.add_argument("game_id", nargs="?", default="")
    show.add_argument("--no-open", action="store_true", help=argparse.SUPPRESS)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        return start_stack(args) if args.command == "start" else replay(args)
    except StackError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
