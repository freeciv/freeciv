"""Episode orchestration: separate agentd and authoritative Freeciv server."""

from __future__ import annotations

import json
import hashlib
import math
import os
import secrets
import shutil
import socket
import subprocess
import threading
import time
import uuid
from dataclasses import replace
from pathlib import Path
from typing import Any, Callable

from .agentd import external_tokens_from_environment, make_server
from .bridge_status import create_bridge_journal, validate_bridge_journal
from .config import EvalConfig, public_config, rotate_seats
from .scoring import summarize_episode

REPO_ROOT = Path(__file__).resolve().parent.parent


class RunError(RuntimeError):
    pass


def benchmark_outcome(
    status: str, fallback_count: int, allow_fallbacks: bool,
) -> tuple[str, bool]:
    if status == "completed" and fallback_count:
        return ("completed" if allow_fallbacks else "invalid"), False
    return status, status == "completed" and fallback_count == 0


def turn_timeout_seconds(config: EvalConfig) -> int:
    controlled = [
        seat for seat in config.seats
        if seat.type not in {"native", "deterministic"}
    ]
    total = sum(seat.timeout_s for seat in controlled)
    if not math.isfinite(total):
        raise RunError("seat timeouts must be finite")
    overhead = max(15, len(controlled) * 5)
    return max(30, math.ceil(total + overhead))


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _harness_sha256() -> str:
    digest = hashlib.sha256()
    for path in sorted((REPO_ROOT / "agent_eval").glob("*.py")):
        digest.update(path.name.encode("utf-8") + b"\0")
        digest.update(path.read_bytes())
    return digest.hexdigest()


def _git_state() -> tuple[str | None, bool | None]:
    try:
        head = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=REPO_ROOT, check=True,
            capture_output=True, text=True,
        ).stdout.strip()
        dirty = bool(
            subprocess.run(
                ["git", "status", "--porcelain"], cwd=REPO_ROOT, check=True,
                capture_output=True, text=True,
            ).stdout
        )
        return head, dirty
    except (OSError, subprocess.CalledProcessError):
        return None, None


def _canonical_sha256(value: Any) -> str:
    encoded = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False,
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _server_binary(config: EvalConfig) -> Path:
    configured = config.server.get("binary")
    path = Path(configured) if configured else REPO_ROOT / "build-agent" / "freeciv-server"
    if not path.is_absolute():
        path = (REPO_ROOT / path).resolve()
    if not path.is_file() or not os.access(path, os.X_OK):
        raise RunError(f"Freeciv server is not executable: {path}")
    return path


def _commands(
    config: EvalConfig, seed: int, bridge_path: Path | None,
) -> list[str]:
    commands = [
        "set aifill 0",
        "set minplayers 0",
        "set timeout -1",
        f"set endturn {config.turns}",
        "set traitdistribution fixed",
        "set ec_turns 0",
        "set threaded_save disabled",
        f"set mapseed {seed}",
        f"set gameseed {seed}",
        "set scorelog enabled",
        "set scoreloglevel all",
        "set scorefile score.log",
        "set saveturns 1",
        "set autosaves turn|gameover",
        "set savename turn-%04T-%R",
    ]
    frame_interval = config.server["frame_interval"]
    if frame_interval:
        commands.append(
            "mapimg define "
            f"zoom={config.server['frame_zoom']}:map=tcub:show=all:"
            f"turns={frame_interval}:format=ppm|ppm"
        )
    map_size = config.server.get("mapsize")
    if map_size is not None:
        if isinstance(map_size, bool) or not isinstance(map_size, int) or not 20 <= map_size <= 2_048_000:
            raise RunError("server.mapsize must be an integer in [20, 2048000]")
        commands.append(f"set size {map_size}")
    for seat in config.seats:
        commands.append(f"create {seat.name} classic")
    commands.append("hard")
    if bridge_path is not None:
        commands.append(f"lua unsafe-file {bridge_path}")
    extra = config.server.get("extra_commands", [])
    if not isinstance(extra, list) or not all(isinstance(item, str) for item in extra):
        raise RunError("server.extra_commands must be an array of strings")
    commands.extend(extra)
    commands.append("start")
    return commands


def run_episode(
    config: EvalConfig,
    output: str | Path,
    *,
    seed: int,
    rotation: int = 0,
    on_ready: Callable[[Path, Path, dict[str, Any]], None] | None = None,
) -> dict[str, Any]:
    effective = rotate_seats(config, rotation)
    controlled_seats = tuple(
        seat for seat in effective.seats if seat.type != "native"
    )
    uses_bridge = bool(controlled_seats)
    binary = _server_binary(effective)
    bridge_path = REPO_ROOT / "agent_eval" / "bridge.lua"
    git_head, git_dirty = _git_state()
    resolved_config = public_config(effective)
    provenance = {
        "git_head": git_head,
        "git_dirty": git_dirty,
        "binary_sha256": _sha256_file(binary),
        "bridge_sha256": _sha256_file(bridge_path) if uses_bridge else None,
        "harness_sha256": _harness_sha256(),
        "resolved_config_sha256": _canonical_sha256(resolved_config),
    }
    try:
        external_tokens = external_tokens_from_environment(effective)
    except ValueError as exc:
        raise RunError(str(exc)) from exc
    episode = Path(output).resolve()
    episode.mkdir(parents=True, exist_ok=False)
    (episode / "saves").mkdir()
    bridge_status_path = (
        create_bridge_journal(episode) if uses_bridge else None
    )
    trace_path = episode / "decisions.jsonl"
    commands = _commands(
        effective, seed, bridge_path.resolve() if uses_bridge else None,
    )
    turn_timeout = turn_timeout_seconds(effective)
    wall_timeout = effective.server.get(
        "wall_timeout_s", max(120, effective.turns * turn_timeout),
    )
    if isinstance(wall_timeout, bool) or not isinstance(wall_timeout, (int, float)) or wall_timeout <= 0:
        raise RunError("server.wall_timeout_s must be a positive number")
    internal_token = secrets.token_urlsafe(32) if uses_bridge else None
    agent_server = None
    agent_thread = None
    agent_url = None
    if uses_bridge:
        agent_server = make_server(
            effective, "127.0.0.1", effective.server["agentd_port"],
            trace_path, internal_token=internal_token,
            external_tokens=external_tokens,
        )
        agent_thread = threading.Thread(
            target=agent_server.serve_forever, daemon=True,
        )
        agent_thread.start()
        agent_url = f"http://127.0.0.1:{agent_server.server_address[1]}"
    game_id = f"{effective.name}-s{seed}-r{rotation}-{uuid.uuid4().hex[:8]}"
    (episode / "server.commands").write_text("\n".join(commands) + "\n", encoding="utf-8")
    started = time.time()
    manifest = {
        "schema_version": 1,
        "game_id": game_id,
        "seed": seed,
        "map_seed": seed,
        "game_seed": seed,
        "rotation": rotation,
        "started_at": started,
        "status": "running",
        "benchmark_valid": None,
        "invalid_reasons": [],
        "fallbacks": 0,
        "agentd_url": agent_url,
        "turn_timeout_s": turn_timeout,
        "external_seats": [
            {"seat_id": seat.id, "token_env": seat.token_env}
            for seat in effective.seats if seat.type == "external"
        ],
        "server_binary": str(binary),
        "config": resolved_config,
        "provenance": provenance,
        "commands_file": "server.commands",
        "trace_file": "decisions.jsonl" if uses_bridge else None,
        "bridge_status_file": (
            bridge_status_path.name if bridge_status_path else None
        ),
        "scorelog_file": "score.log",
        "control_file": "control.json" if uses_bridge else None,
    }
    manifest_path = episode / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    env = os.environ.copy()
    # An uninstalled Meson build does not know the source data directory once
    # the server cwd is moved into the isolated episode.
    env.setdefault("FREECIV_DATA_PATH", str(REPO_ROOT / "data"))
    bridge_environment_keys = (
        "AGENT_EVAL_URL", "AGENT_EVAL_GAME_ID", "AGENT_EVAL_INTERNAL_TOKEN",
        "AGENT_EVAL_BRIDGE_STATUS_PATH", "AGENT_EVAL_TURN_TIMEOUT_S",
        "AGENT_EVAL_SEATS",
    )
    for key in bridge_environment_keys:
        env.pop(key, None)
    if uses_bridge:
        env["AGENT_EVAL_URL"] = agent_url
        env["AGENT_EVAL_GAME_ID"] = game_id
        env["AGENT_EVAL_INTERNAL_TOKEN"] = internal_token
        env["AGENT_EVAL_BRIDGE_STATUS_PATH"] = str(bridge_status_path)
        env["AGENT_EVAL_TURN_TIMEOUT_S"] = str(turn_timeout)
        env["AGENT_EVAL_SEATS"] = ",".join(
            f"{seat.id}:{seat.name}" for seat in controlled_seats
        )
    with socket.socket() as port_socket:
        port_socket.bind(("127.0.0.1", 0))
        freeciv_port = port_socket.getsockname()[1]
    command = [
        str(binary),
        "--Announce",
        "none",
        "--bind",
        "127.0.0.1",
        "--port",
        str(freeciv_port),
        "--exit-on-end",
        "--ruleset",
        effective.ruleset,
        "--saves",
        str(episode / "saves"),
        "--log",
        str(episode / "server.log"),
    ]
    manifest["server_argv"] = command
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    control = None
    control_path = None
    if uses_bridge:
        control = {
            "schema_version": 1,
            "episode": str(episode),
            "agentd_url": agent_url,
            "turn_timeout_s": turn_timeout,
            "external_seats": manifest["external_seats"],
        }
        control_path = episode / "control.json"
        control_path.write_text(
            json.dumps(control, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    status = "failed"
    returncode: int | None = None
    error: str | None = None
    fallback_count = 0
    try:
        if on_ready is not None and control_path is not None:
            on_ready(episode, control_path, control)
        with (episode / "server.stdout.log").open("wb") as output_stream:
            process = subprocess.Popen(
                command,
                cwd=episode,
                env=env,
                stdin=subprocess.PIPE,
                stdout=output_stream,
                stderr=subprocess.STDOUT,
            )
            try:
                process.communicate(("\n".join(commands) + "\n").encode("utf-8"), timeout=float(wall_timeout))
            except subprocess.TimeoutExpired:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                error = f"episode exceeded wall timeout of {wall_timeout}s"
                status = "timed_out"
            returncode = process.returncode
        traced_seats: set[str] = set()
        traced_turns: list[int] = []
        trace_error: str | None = None
        if trace_path.exists():
            try:
                for line in trace_path.read_text(encoding="utf-8").splitlines():
                    event = json.loads(line)
                    if event.get("event") == "decision":
                        traced_seats.add(event.get("seat_id"))
                        event_turn = event.get("turn")
                        if (
                            isinstance(event_turn, int)
                            and not isinstance(event_turn, bool)
                            and event_turn not in traced_turns
                        ):
                            traced_turns.append(event_turn)
                        fallback_count += int(bool(event.get("fallback")))
            except json.JSONDecodeError as exc:
                trace_error = f"malformed decisions.jsonl: {exc}"
        if status != "timed_out":
            status = "completed" if returncode == 0 and (episode / "score.log").exists() else "failed"
            if status == "failed":
                error = f"freeciv-server exited {returncode} or produced no score.log"
            else:
                bridge_reasons = (
                    validate_bridge_journal(
                        bridge_status_path, traced_turns,
                        episode / "score.log",
                    )
                    if bridge_status_path is not None else []
                )
                missing = sorted(
                    {
                        seat.id for seat in effective.seats
                        if seat.type != "native"
                    }
                    - traced_seats
                )
                server_log = (episode / "server.log").read_text(encoding="utf-8", errors="replace")
                if (
                    "agent_eval bridge:" in server_log.lower()
                    and "bridge_lua_error" not in bridge_reasons
                ):
                    bridge_reasons.append("bridge_lua_error")
                if trace_error:
                    status = "failed"
                    error = trace_error
                elif bridge_reasons:
                    status = "invalid"
                    error = "benchmark invalid: " + ", ".join(bridge_reasons)
                elif missing:
                    status = "failed"
                    error = f"bridge produced no decisions for seats: {', '.join(missing)}"
                elif "lua error:" in server_log:
                    status = "failed"
                    error = "Freeciv reported a Lua error; see server.log"
                elif fallback_count and not effective.server["allow_fallbacks"]:
                    error = (
                        f"benchmark invalid: {fallback_count} deterministic "
                        "fallback decision(s)"
                    )
                status, _ = benchmark_outcome(
                    status, fallback_count, effective.server["allow_fallbacks"],
                )
                manifest["invalid_reasons"] = bridge_reasons
    finally:
        if agent_server is not None:
            agent_server.shutdown()
            agent_server.server_close()
        if agent_thread is not None:
            agent_thread.join(timeout=5)
    finished = time.time()
    manifest.update(
        {
            "status": status,
            "benchmark_valid": benchmark_outcome(
                status, fallback_count, effective.server["allow_fallbacks"],
            )[1],
            "fallbacks": fallback_count,
            "error": error,
            "returncode": returncode,
            "finished_at": finished,
            "duration_s": round(finished - started, 3),
            "frames": len(list(episode.rglob("*.ppm"))),
            "checkpoints": len(
                [
                    path for path in (episode / "saves").iterdir()
                    if path.is_file()
                    and path.name.endswith(
                        (".sav", ".sav.gz", ".sav.bz2", ".sav.xz", ".sav.zst")
                    )
                ]
            ),
        }
    )
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    summary = summarize_episode(episode)
    (episode / "report.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return summary


def default_episode_path(root: Path, config: EvalConfig, seed: int, rotation: int) -> Path:
    stamp = time.strftime("%Y%m%d-%H%M%S")
    return root / f"{config.name}-seed{seed}-rot{rotation}-{stamp}-{uuid.uuid4().hex[:6]}"


def render_episode(directory: str | Path, output: str | Path | None, fps: int = 4) -> Path:
    episode = Path(directory).resolve()
    frames = sorted(episode.rglob("*.ppm"))
    if not frames:
        raise RunError(f"no PPM frames found under {episode}")
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise RunError("ffmpeg is not installed or not on PATH")
    if fps < 1 or fps > 60:
        raise RunError("fps must be in [1, 60]")
    destination = Path(output).resolve() if output else episode / "game.mp4"
    concat = episode / ".frames.ffconcat"
    concat.write_text(
        "ffconcat version 1.0\n"
        + "".join(f"file '{str(frame).replace(chr(39), chr(39) + chr(92) + chr(39) + chr(39))}'\nduration {1 / fps:.6f}\n" for frame in frames)
        + f"file '{frames[-1]}'\n",
        encoding="utf-8",
    )
    try:
        subprocess.run(
            [ffmpeg, "-y", "-safe", "0", "-i", str(concat), "-vf", "pad=ceil(iw/2)*2:ceil(ih/2)*2", "-pix_fmt", "yuv420p", str(destination)],
            check=True,
        )
    finally:
        concat.unlink(missing_ok=True)
    return destination
