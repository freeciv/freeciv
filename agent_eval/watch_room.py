"""Isolated native snapshot viewer for games hosted by older supervisors."""

from __future__ import annotations

import bz2
import gzip
import lzma
import os
import re
import secrets
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable

from .client import ClientError, load_private_json


GAME_ID_RE = re.compile(r"^game_[A-Za-z0-9_-]{20,80}$")
SAVE_NAME_RE = re.compile(
    r"^turn-(\d+)(?:-[A-Za-z0-9._-]+)?\.sav(?:\.(?:gz|bz2|xz|zst))?$",
)
SAVE_SUFFIXES = (".sav", ".sav.gz", ".sav.bz2", ".sav.xz", ".sav.zst")
SERVER_READY_TEXT = "Now accepting new client connections"


@dataclass(frozen=True)
class SnapshotSave:
    source: Path
    turn: int
    size: int
    mtime_ns: int


def _replay_fallback(game_id: str) -> str:
    return f"Use `just replay {game_id}` instead."


def locate_game_run(game_id: str, credentials_path: str | Path) -> Path:
    """Resolve the conventional run directory without trusting path input."""
    if not GAME_ID_RE.fullmatch(game_id):
        raise ClientError(None, "snapshot watch room requires a valid game ID")
    credentials = Path(credentials_path).expanduser().resolve()
    value = load_private_json(credentials)
    if value.get("game_id") != game_id:
        raise ClientError(None, "owner credentials belong to a different game")
    game_directory = credentials.parent
    if (
        credentials.name != "owner.json"
        or game_directory.name != game_id
        or game_directory.parent.name != "games"
    ):
        raise ClientError(
            None,
            "snapshot watch room requires the standard "
            f"games/{game_id}/owner.json credentials path. "
            + _replay_fallback(game_id),
        )
    state_root = game_directory.parent.parent.resolve()
    runs_root = (state_root / "runs").resolve()
    run_directory = (runs_root / game_id).resolve()
    if run_directory.parent != runs_root or not run_directory.is_dir():
        raise ClientError(
            None,
            f"cannot find the local run artifacts for {game_id}. "
            + _replay_fallback(game_id),
        )
    return run_directory


def select_stable_snapshot(
    run_directory: str | Path, game_id: str, *, settle_s: float = 0.1,
) -> SnapshotSave:
    """Select the newest turn-numbered save unchanged across two observations."""
    saves_directory = Path(run_directory).resolve() / "saves"
    if not saves_directory.is_dir():
        raise ClientError(
            None,
            f"no save directory is available for {game_id}. "
            + _replay_fallback(game_id),
        )
    before: dict[Path, tuple[int, int, int]] = {}
    for candidate in saves_directory.iterdir():
        match = SAVE_NAME_RE.fullmatch(candidate.name)
        if match is None or candidate.is_symlink() or not candidate.is_file():
            continue
        try:
            stat = candidate.stat()
        except OSError:
            continue
        if stat.st_size > 0:
            before[candidate] = (
                int(match.group(1)), stat.st_size, stat.st_mtime_ns,
            )
    if not before:
        raise ClientError(
            None,
            f"no complete turn save is available yet for {game_id}. "
            + _replay_fallback(game_id),
        )
    if settle_s > 0:
        time.sleep(settle_s)
    stable = []
    for candidate, signature in before.items():
        try:
            stat = candidate.stat()
        except OSError:
            continue
        turn, size, mtime_ns = signature
        if (stat.st_size, stat.st_mtime_ns) == (size, mtime_ns):
            stable.append(SnapshotSave(candidate, turn, size, mtime_ns))
    if not stable:
        raise ClientError(
            None,
            f"the newest save for {game_id} is still being written; retry "
            "after the turn finishes. " + _replay_fallback(game_id),
        )
    return max(stable, key=lambda item: (item.turn, item.mtime_ns, item.source.name))


def _validate_save_copy(path: Path, game_id: str) -> None:
    """Read the copied compressed stream to EOF before Freeciv sees it."""
    try:
        if path.name.endswith(".sav.gz"):
            stream: Any = gzip.open(path, "rb")
        elif path.name.endswith(".sav.bz2"):
            stream = bz2.open(path, "rb")
        elif path.name.endswith(".sav.xz"):
            stream = lzma.open(path, "rb")
        elif path.name.endswith(".sav.zst"):
            zstd = shutil.which("zstd")
            if zstd is None:
                raise ClientError(
                    None,
                    "zstd is required to validate this snapshot save. "
                    + _replay_fallback(game_id),
                )
            result = subprocess.run(
                [zstd, "--quiet", "--test", str(path)],
                capture_output=True,
                text=True,
            )
            if result.returncode != 0:
                raise OSError(result.stderr.strip() or "zstd validation failed")
            return
        else:
            stream = path.open("rb")
        with stream:
            prefix = stream.read(8192)
            if b"[savefile]" not in prefix:
                raise OSError("savefile header is missing")
            while stream.read(1024 * 1024):
                pass
    except ClientError:
        raise
    except (OSError, EOFError, lzma.LZMAError) as exc:
        raise ClientError(
            None,
            f"the copied save for {game_id} is incomplete or invalid: {exc}. "
            + _replay_fallback(game_id),
        ) from exc


def _reserve_loopback_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


class _OutputMonitor:
    def __init__(self, stream: Any):
        self.stream = stream
        self.condition = threading.Condition()
        self.lines: deque[str] = deque(maxlen=512)
        self.finished = False
        self.thread = threading.Thread(target=self._pump, daemon=True)

    def start(self) -> None:
        self.thread.start()

    def _pump(self) -> None:
        try:
            for line in self.stream:
                with self.condition:
                    self.lines.append(line.rstrip("\r\n"))
                    self.condition.notify_all()
        finally:
            with self.condition:
                self.finished = True
                self.condition.notify_all()

    def wait_for(
        self,
        predicate: Callable[[str], bool],
        *,
        timeout_s: float,
        description: str,
        process: Any,
    ) -> str:
        deadline = time.monotonic() + timeout_s
        with self.condition:
            while True:
                for line in self.lines:
                    if predicate(line):
                        return line
                if process.poll() is not None:
                    raise ClientError(
                        None,
                        "snapshot watch room server stopped while waiting for "
                        + description + self.tail_message(),
                    )
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise ClientError(
                        None,
                        "timed out waiting for snapshot watch room "
                        + description + self.tail_message(),
                    )
                self.condition.wait(min(remaining, 0.1))

    def tail_message(self) -> str:
        values = list(self.lines)[-12:]
        return "" if not values else "\nServer output:\n" + "\n".join(values)


def _stop_process(process: Any) -> None:
    if process is None or process.poll() is not None:
        return
    try:
        process.terminate()
    except OSError:
        return
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            process.kill()
        except OSError:
            return
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            pass


def run_snapshot_watch_room(
    game_id: str,
    *,
    credentials_path: str | Path,
    server_binary: str | Path,
    client_binary: str | Path,
    data_path: str | Path,
    settle_s: float = 0.1,
    startup_timeout_s: float = 45,
    connect_timeout_s: float = 20,
    stop_event: threading.Event | None = None,
    on_ready: Callable[[dict[str, Any]], None] | None = None,
    server_process_factory: Callable[..., Any] | None = None,
    client_process_factory: Callable[..., Any] | None = None,
    environment_overrides: dict[str, str] | None = None,
) -> int:
    """Load a read-only checkpoint copy in a disposable loopback server."""
    server_process_factory = server_process_factory or subprocess.Popen
    client_process_factory = client_process_factory or subprocess.Popen
    run_directory = locate_game_run(game_id, credentials_path)
    snapshot = select_stable_snapshot(
        run_directory, game_id, settle_s=settle_s,
    )
    room = Path(tempfile.mkdtemp(prefix=f"freeciv-snapshot-{game_id}-"))
    server = None
    client = None
    monitor = None
    try:
        copied_save = room / snapshot.source.name
        shutil.copyfile(snapshot.source, copied_save)
        os.chmod(copied_save, 0o600)
        source_after = snapshot.source.stat()
        if (
            source_after.st_size != snapshot.size
            or source_after.st_mtime_ns != snapshot.mtime_ns
            or copied_save.stat().st_size != snapshot.size
        ):
            raise ClientError(
                None,
                f"save turn {snapshot.turn} changed while it was copied; "
                "retry after the turn finishes. " + _replay_fallback(game_id),
            )
        _validate_save_copy(copied_save, game_id)
        saves_directory = room / "saves"
        saves_directory.mkdir(mode=0o700)
        server_log = room / "server.log"
        client_log = room / "client.log"
        port = _reserve_loopback_port()
        username = f"Snapshot-{secrets.token_hex(6)}"
        environment = os.environ.copy()
        environment["FREECIV_DATA_PATH"] = str(
            Path(data_path).expanduser().resolve(),
        )
        if environment_overrides:
            environment.update(environment_overrides)
        server_command = [
            str(Path(server_binary).expanduser().resolve()),
            "--Announce", "none",
            "--bind", "127.0.0.1",
            "--port", str(port),
            "--file", str(copied_save),
            "--saves", str(saves_directory),
            "--log", str(server_log),
            "--exit-on-end",
        ]
        server = server_process_factory(
            server_command,
            cwd=room,
            env=environment,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        if server.stdin is None or server.stdout is None:
            raise ClientError(None, "snapshot watch room server pipes unavailable")
        monitor = _OutputMonitor(server.stdout)
        monitor.start()
        # Loaded agent-eval saves use timeout=-1. This command affects only the
        # disposable clone and lets it accept a loopback observer without
        # advancing the frozen snapshot.
        server.stdin.write("set timeout 0\n")
        server.stdin.flush()
        monitor.wait_for(
            lambda line: SERVER_READY_TEXT in line,
            timeout_s=startup_timeout_s,
            description="server startup",
            process=server,
        )
        monitor.wait_for(
            lambda line: "'timeout' has been set to 0" in line,
            timeout_s=5,
            description="frozen timeout acknowledgement",
            process=server,
        )
        print(
            "SNAPSHOT WATCH ROOM — this is not continuously live.\n"
            f"Source: {snapshot.source.name} (turn {snapshot.turn}).\n"
            "The original game and its Freeciv server remain untouched.\n"
            "Close the Freeciv window to remove this isolated room.",
            file=sys.stderr,
            flush=True,
        )
        client_command = [
            str(Path(client_binary).expanduser().resolve()),
            "--autoconnect",
            "--server", "127.0.0.1",
            "--port", str(port),
            "--name", username,
            "--log", str(client_log),
            "--debug", "v",
        ]
        client = client_process_factory(
            client_command, cwd=room, env=environment,
        )
        monitor.wait_for(
            lambda line: f"{username} has connected" in line,
            timeout_s=connect_timeout_s,
            description="SDL client connection",
            process=server,
        )
        server.stdin.write(f"observe {username}\n")
        server.stdin.flush()
        monitor.wait_for(
            lambda line: f"{username} now observes" in line,
            timeout_s=10,
            description="global observer promotion",
            process=server,
        )
        ready = {
            "game_id": game_id,
            "turn": snapshot.turn,
            "source_save": snapshot.source.name,
            "host": "127.0.0.1",
            "port": port,
        }
        print(
            f"Snapshot watch room ready at turn {snapshot.turn}.",
            file=sys.stderr,
            flush=True,
        )
        if on_ready is not None:
            on_ready(ready)
        while True:
            if stop_event is not None and stop_event.is_set():
                return 0
            returncode = client.poll()
            if returncode is not None:
                if returncode != 0:
                    tail = ""
                    try:
                        tail = "\n".join(
                            client_log.read_text(
                                encoding="utf-8", errors="replace",
                            ).splitlines()[-15:]
                        )
                    except OSError:
                        pass
                    raise ClientError(
                        None,
                        f"snapshot Freeciv client exited with status {returncode}"
                        + ("\nClient log:\n" + tail if tail else ""),
                    )
                return 0
            if server.poll() is not None:
                raise ClientError(
                    None,
                    "snapshot watch room server stopped unexpectedly"
                    + (monitor.tail_message() if monitor else ""),
                )
            time.sleep(0.1)
    finally:
        _stop_process(client)
        _stop_process(server)
        if monitor is not None:
            monitor.thread.join(timeout=2)
        if server is not None:
            for stream in (
                getattr(server, "stdin", None),
                getattr(server, "stdout", None),
            ):
                if stream is not None:
                    try:
                        stream.close()
                    except OSError:
                        pass
        shutil.rmtree(room, ignore_errors=True)
