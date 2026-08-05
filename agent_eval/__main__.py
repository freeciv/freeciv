"""Session-first CLI for the Freeciv agent evaluation service."""

from __future__ import annotations

import argparse
import json
import os
import re
import secrets
import stat
import subprocess
import sys
import time
import urllib.parse
from pathlib import Path
from typing import Any

from .client import (
    ClientError,
    NativeViewerCompatibilityError,
    create_game,
    join_capabilities,
    join_game,
    load_private_json,
    native_viewer_status,
    next_turn,
    release_native_viewer,
    require_native_viewer_protocol,
    request_json,
    request_native_viewer,
    service_url,
    submit_action,
    write_private_json,
)


def load_config(*args: Any, **kwargs: Any) -> Any:
    """Lazy compatibility seam for the legacy CLI and its callers."""
    from .config import load_config as implementation

    return implementation(*args, **kwargs)


def run_episode(*args: Any, **kwargs: Any) -> Any:
    """Lazy compatibility seam that keeps providers off supervisor startup."""
    from .runner import run_episode as implementation

    return implementation(*args, **kwargs)


def _json(value: Any) -> None:
    print(json.dumps(value, indent=2, sort_keys=True), flush=True)


def _viewer_log_tail(path: Path, lines: int = 20) -> str:
    try:
        values = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return ""
    return "\n".join(values[-lines:])


def _stop_viewer_process(process: Any) -> None:
    if process is None or process.poll() is not None:
        return
    try:
        process.terminate()
    except OSError:
        return
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        try:
            process.kill()
        except OSError:
            return
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            pass


def run_native_viewer_client(
    base: str,
    game_id: str,
    owner_token: str,
    *,
    client_binary: str | Path,
    data_path: str | Path,
    log_dir: str | Path,
    lease_file: str | Path | None = None,
    poll_interval_s: float = 0.1,
    disconnect_grace_s: float = 2.0,
    process_factory: Any = None,
) -> int:
    """Run one native client lease until the GUI closes or the lease fails."""
    process_factory = process_factory or subprocess.Popen
    connection = request_native_viewer(base, game_id, owner_token)
    lease_id = connection["lease_id"]
    try:
        if lease_file is not None:
            write_private_json(
                lease_file,
                {
                    "schema_version": 1,
                    "service_url": base,
                    "game_id": game_id,
                    "lease_id": lease_id,
                },
            )

        log_directory = Path(log_dir).expanduser().resolve()
        log_directory.mkdir(parents=True, exist_ok=True)
        log_path = log_directory / f"viewer-{lease_id}.log"
        command = [
            str(Path(client_binary).expanduser().resolve()),
            "--autoconnect",
            "--server", str(connection["host"]),
            "--port", str(connection["port"]),
            "--name", str(connection["username"]),
            "--log", str(log_path),
            "--debug", "v",
        ]
    except Exception:
        try:
            release_native_viewer(base, game_id, owner_token, lease_id)
        except ClientError:
            pass
        raise
    environment = os.environ.copy()
    environment["FREECIV_DATA_PATH"] = str(Path(data_path).expanduser().resolve())
    print(
        f"Opening Freeciv global observer {connection['username']} for {game_id}...",
        file=sys.stderr,
    )
    if connection.get("game_state") in {"lobby", "starting"}:
        print(
            "The observer will wait in Freeciv's pregame screen until the "
            "match starts, then open the live map automatically.",
            file=sys.stderr,
        )
    print(f"Viewer log: {log_path}", file=sys.stderr)

    process = None
    last_message: tuple[str, str] | None = None
    status_failures = 0
    terminal_error: ClientError | None = None
    normal_end = False
    try:
        launch_state = str(connection.get("state", "waiting_for_client"))
        while launch_state == "enabling_server":
            message_key = (launch_state, str(connection.get("game_state", "unknown")))
            if message_key != last_message:
                print(
                    "Waiting for the current agent turn to finish so Freeciv "
                    "can enable its observer socket...",
                    file=sys.stderr,
                )
                last_message = message_key
            try:
                connection = native_viewer_status(
                    base, game_id, owner_token, lease_id,
                )
                status_failures = 0
            except ClientError as exc:
                status_failures += 1
                if status_failures >= 3:
                    raise ClientError(
                        exc.status,
                        "lost contact while enabling the native viewer: " + str(exc),
                    ) from exc
                time.sleep(poll_interval_s)
                continue
            if not connection.get("active", True):
                raise ClientError(
                    None,
                    "native viewer lease became inactive before the GUI "
                    "could launch",
                )
            launch_state = str(connection.get("state", "unknown"))
            if launch_state == "game_ended":
                print("The game ended before the viewer opened.", file=sys.stderr)
                normal_end = True
                break
            if launch_state in {
                "connect_timeout", "disconnected", "server_disconnected",
                "error", "released",
            }:
                raise ClientError(
                    None,
                    "native viewer could not enable the server: "
                    + str(connection.get("error") or launch_state),
                )
            time.sleep(poll_interval_s)

        if not normal_end:
            process = process_factory(command, env=environment)
        while process is not None:
            returncode = process.poll()
            if returncode is not None:
                if returncode != 0:
                    tail = _viewer_log_tail(log_path)
                    detail = f"\nLast client log lines:\n{tail}" if tail else ""
                    raise ClientError(
                        None,
                        f"Freeciv viewer exited with status {returncode}.{detail}",
                    )
                normal_end = True
                break

            try:
                status = native_viewer_status(
                    base, game_id, owner_token, lease_id,
                )
                status_failures = 0
            except ClientError as exc:
                status_failures += 1
                if status_failures >= 3:
                    raise ClientError(
                        exc.status,
                        "lost contact with the native viewer lease: " + str(exc),
                    ) from exc
                time.sleep(poll_interval_s)
                continue

            state = str(status.get("state", "unknown"))
            game_state = str(status.get("game_state", "unknown"))
            message_key = (state, game_state)
            if message_key != last_message:
                if state == "waiting_for_client":
                    message = "Waiting for the Freeciv GUI to connect..."
                elif state == "connected":
                    message = "Freeciv connected; enabling global observation..."
                elif state == "observing" and game_state != "running":
                    message = (
                        "Observer ready; waiting for the match to start. "
                        "The live map will open automatically."
                    )
                elif state == "game_ready":
                    message = "Live Freeciv map ready. Close the GUI to stop watching."
                else:
                    message = ""
                if message:
                    print(message, file=sys.stderr)
                last_message = message_key

            if state == "game_ended":
                print("The game ended; closing the native viewer.", file=sys.stderr)
                normal_end = True
                _stop_viewer_process(process)
                break
            if state in {
                "connect_timeout", "disconnected", "server_disconnected",
                "error", "released",
            }:
                _stop_viewer_process(process)
                detail = status.get("error") or (
                    "the observer lease ended before the live map was ready"
                )
                tail = _viewer_log_tail(log_path)
                log_detail = f"\nLast client log lines:\n{tail}" if tail else ""
                terminal_error = ClientError(
                    None, f"native viewer {state}: {detail}.{log_detail}",
                )
                break
            if not status.get("active", True):
                _stop_viewer_process(process)
                terminal_error = ClientError(
                    None,
                    "native viewer lease became inactive before the GUI closed",
                )
                break

            if state == "waiting_for_client":
                tail = _viewer_log_tail(log_path).lower()
                if (
                    "you were rejected from the game" in tail
                    or "failed to contact server" in tail
                    or "error contacting server" in tail
                ):
                    _stop_viewer_process(process)
                    terminal_error = ClientError(
                        None,
                        "Freeciv could not join the observer lease.\n"
                        "Last client log lines:\n" + _viewer_log_tail(log_path),
                    )
                    break
            time.sleep(poll_interval_s)
    except KeyboardInterrupt:
        normal_end = True
        _stop_viewer_process(process)
    finally:
        _stop_viewer_process(process)
        # With timeout=1 the server normally consumes the client's EOF almost
        # immediately.  Give its manager a chance to restore timeout=-1 before
        # the explicit idempotent cleanup request.
        deadline = time.monotonic() + max(0, disconnect_grace_s)
        while process is not None and time.monotonic() < deadline:
            try:
                current = native_viewer_status(
                    base, game_id, owner_token, lease_id,
                )
            except ClientError:
                break
            if not current.get("active"):
                break
            time.sleep(min(poll_interval_s, 0.1))
        try:
            released = release_native_viewer(
                base, game_id, owner_token, lease_id,
            )
        except ClientError as exc:
            if terminal_error is None:
                terminal_error = ClientError(
                    exc.status, "could not release native viewer: " + str(exc),
                )
        else:
            if released.get("released") and not released.get(
                "timeout_restored", True,
            ) and terminal_error is None:
                terminal_error = ClientError(
                    None,
                    "Freeciv closed, but benchmark timeout restoration was not "
                    "acknowledged; check the supervisor before reopening",
                )

    if terminal_error is not None:
        raise terminal_error
    return 0 if normal_end else 2


def _service_argument(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--service-url",
        default=None,
        help="supervisor base URL (default: AGENT_EVAL_SERVICE_URL or localhost)",
    )


def _session_parser(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--session", required=True, help="mode-0600 agent session JSON")


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="python3 -m agent_eval")
    sub = parser.add_subparsers(dest="command", required=True)

    supervisor = sub.add_parser(
        "supervisor", help="run the persistent policy-free game service",
    )
    supervisor.add_argument("--host", default="127.0.0.1")
    supervisor.add_argument("--port", type=int, default=8765)
    supervisor.add_argument("--public-url")
    supervisor.add_argument(
        "--ready-file",
        help="write mode-0600 listener metadata and remove it on shutdown",
    )
    supervisor.add_argument("--runs-root", default="session-runs")
    supervisor.add_argument("--binary")
    supervisor.add_argument(
        "--agent-binary",
        help="same-revision freeciv-agent executable for full-control-v2",
    )
    supervisor.add_argument("--admin-token")
    supervisor.add_argument(
        "--admin-token-env", default="AGENT_EVAL_ADMIN_TOKEN",
    )

    game = sub.add_parser("game", help="create, join, and inspect games")
    game_sub = game.add_subparsers(dest="game_command", required=True)

    create = game_sub.add_parser("create", help="create a lobby and Freeciv child")
    _service_argument(create)
    create.add_argument("--admin-token")
    create.add_argument("--mode", choices=("single", "multiplayer"), default="single")
    create.add_argument("--places", type=int, default=2)
    create.add_argument("--turns", type=int, default=5000)
    create.add_argument("--seed", type=int)
    create.add_argument("--ruleset", default="classic")
    create.add_argument(
        "--control-protocol",
        choices=("strategic-v1", "full-control-v2"),
        default="strategic-v1",
    )
    create.add_argument(
        "--objective", default="Maximize final Freeciv civilization score.",
    )
    create.add_argument(
        "--timing-mode", choices=("default", "blitz", "infinite"),
    )
    create.add_argument(
        "--difficulty",
        choices=("novice", "easy", "normal", "hard", "cheating"),
        default="hard",
    )
    create.add_argument("--action-timeout-s", type=float)
    create.add_argument("--lobby-timeout-s", type=float, default=300)
    create.add_argument("--frame-interval", type=int, default=1)
    create.add_argument("--frame-zoom", type=int, default=1)
    create.add_argument(
        "--credentials",
        help=(
            "write owner/join credentials as mode-0600 JSON; supports a "
            "{game_id} path placeholder or destination directory"
        ),
    )
    create.add_argument(
        "--player-invite",
        help=(
            "write a player-only, game-scoped join invitation as mode-0600 "
            "JSON; supports a {game_id} path placeholder"
        ),
    )

    stage_invite = game_sub.add_parser(
        "stage-invite",
        help="rebuild a player invitation from owner credentials",
    )
    stage_invite.add_argument("game_id")
    stage_invite.add_argument(
        "--credentials", required=True,
        help="mode-0600 owner credentials created with the game",
    )
    stage_invite.add_argument(
        "--output", required=True,
        help="player invitation destination; supports a {game_id} placeholder",
    )
    stage_invite.add_argument(
        "--require-open-lobby", action="store_true",
        help="refuse to stage unless the original supervisor reports a lobby",
    )

    join = game_sub.add_parser("join", help="claim an agent place")
    join.add_argument("game_id")
    _service_argument(join)
    join.add_argument("--join-token")
    join.add_argument(
        "--credentials", help="read join token/service URL from create credentials",
    )
    join.add_argument("--place", help="place number or seat id")
    join.add_argument(
        "--name", "--controller-label", dest="controller_label",
        help=(
            "required public harness-model label, for example "
            "codex-gpt-5.6-sol"
        ),
    )
    join.add_argument(
        "--metadata",
        help="public identity metadata JSON, @file, or - for stdin",
    )
    join.add_argument(
        "--supported-control-protocol",
        dest="supported_control_protocols",
        action="append",
        help=(
            "advertise one harness control protocol; repeat for multiple "
            "values (required to join a full-control-v2 game)"
        ),
    )
    join.add_argument(
        "--session", required=True,
        help="write the returned agent session as mode-0600 JSON",
    )

    for name in ("status", "result", "watch"):
        item = game_sub.add_parser(name)
        item.add_argument("game_id")
        _service_argument(item)

    viewer = game_sub.add_parser(
        "native-viewer", help="create an owner-only local GUI observer lease",
    )
    viewer.add_argument("game_id")
    _service_argument(viewer)
    viewer.add_argument("--owner-token")
    viewer.add_argument("--credentials")
    viewer.add_argument(
        "--lease-file",
        help="write the viewer lease as mode-0600 JSON before printing it",
    )

    viewer_release = game_sub.add_parser(
        "native-viewer-release",
        help="idempotently release an owner-only GUI observer lease",
    )
    viewer_release.add_argument("game_id")
    _service_argument(viewer_release)
    viewer_release.add_argument("--owner-token")
    viewer_release.add_argument("--credentials")
    viewer_release.add_argument("--lease-id")
    viewer_release.add_argument("--lease-file")

    viewer_status = game_sub.add_parser(
        "native-viewer-status",
        help="inspect one owner-only GUI observer lease",
    )
    viewer_status.add_argument("game_id")
    _service_argument(viewer_status)
    viewer_status.add_argument("--owner-token")
    viewer_status.add_argument("--credentials")
    viewer_status.add_argument("--lease-id")
    viewer_status.add_argument("--lease-file")

    viewer_run = game_sub.add_parser(
        "native-viewer-run",
        help="lease, launch, supervise, and release a native Freeciv GUI",
    )
    viewer_run.add_argument("game_id")
    _service_argument(viewer_run)
    viewer_run.add_argument("--owner-token")
    viewer_run.add_argument("--credentials")
    viewer_run.add_argument("--lease-file")
    viewer_run.add_argument("--client", required=True)
    viewer_run.add_argument(
        "--snapshot-server",
        default=str(
            Path(__file__).resolve().parent.parent
            / "build-agent" / "freeciv-server"
        ),
        help=(
            "same-revision Freeciv server used only for old-supervisor "
            "snapshot rooms"
        ),
    )
    viewer_run.add_argument("--data-path", required=True)
    viewer_run.add_argument("--log-dir", required=True)
    viewer_run.add_argument("--poll-interval-s", type=float, default=0.1)
    viewer_run.add_argument("--disconnect-grace-s", type=float, default=2.0)

    cancel = game_sub.add_parser("cancel", help="cancel using owner credentials")
    cancel.add_argument("game_id")
    _service_argument(cancel)
    cancel.add_argument("--owner-token")
    cancel.add_argument("--credentials")

    agent = sub.add_parser("agent", help="long-poll and submit through a session")
    agent_sub = agent.add_subparsers(dest="agent_command", required=True)
    next_parser = agent_sub.add_parser("next", help="long-poll for the next turn")
    _session_parser(next_parser)
    next_parser.add_argument("--after-turn", type=int, default=0)
    next_parser.add_argument("--wait-s", type=float, default=30)

    act = agent_sub.add_parser("act", help="submit a strategic-v1 action")
    _session_parser(act)
    act.add_argument("--turn", required=True, type=int)
    act.add_argument("--observation-id", required=True)
    act.add_argument(
        "--action", required=True,
        help="JSON object, @file, or - to read JSON from stdin",
    )
    act.add_argument("--telemetry", help="opaque JSON object/value")

    bot = sub.add_parser(
        "bot", help="run the optional deterministic public-API client",
    )
    _session_parser(bot)

    # Legacy config-first commands remain available during the transition.
    serve = sub.add_parser("serve", help="legacy: run agentd without Freeciv")
    serve.add_argument("config")
    serve.add_argument("--host", default="127.0.0.1")
    serve.add_argument("--port", type=int, default=8765)
    serve.add_argument("--trace")

    run = sub.add_parser("run", help="legacy: run one config-first episode")
    run.add_argument("config")
    run.add_argument("--seed", type=int)
    run.add_argument("--rotation", type=int, default=0)
    run.add_argument("--output")
    run.add_argument("--runs-root", default="runs")

    evaluate = sub.add_parser("eval", help="legacy: run config seeds/rotations")
    evaluate.add_argument("config")
    evaluate.add_argument("--runs-root", default="runs")

    report = sub.add_parser("report", help="parse episode scorelogs/traces")
    report.add_argument("episode", nargs="+")
    report.add_argument("--output")

    render = sub.add_parser("render", help="render PPM map frames to MP4")
    render.add_argument("episode")
    render.add_argument("--output")
    render.add_argument("--fps", type=int, default=4)
    return parser


def _read_json_argument(value: str) -> Any:
    if value == "-":
        text = sys.stdin.read()
    elif value.startswith("@"):
        text = Path(value[1:]).read_text(encoding="utf-8")
    else:
        text = value
    return json.loads(text)


def _credential_value(
    explicit: str | None, environment: str, credentials: dict[str, Any] | None,
    key: str,
) -> str | None:
    return (
        explicit
        or (credentials or {}).get(key)
        or os.environ.get(environment)
    )


def _create_credentials_path(value: str, game_id: str) -> Path:
    """Resolve an exact file, {game_id} template, or destination directory."""
    expanded = value.replace("{game_id}", game_id)
    destination = Path(expanded).expanduser()
    if "{game_id}" not in value and (
        value.endswith((os.sep, "/"))
        or (destination.exists() and destination.is_dir())
    ):
        destination = destination / game_id / "owner.json"
    return destination


def _create_player_invite_path(value: str, game_id: str) -> Path:
    """Resolve an exact invite, {game_id} template, or destination directory."""
    expanded = value.replace("{game_id}", game_id)
    destination = Path(expanded).expanduser()
    if "{game_id}" not in value and (
        value.endswith((os.sep, "/"))
        or (destination.exists() and destination.is_dir())
    ):
        destination = destination / f"{game_id}.json"
    return destination


def _validated_game_id(value: Any) -> str:
    game_id = str(value or "")
    if re.fullmatch(r"game_[A-Za-z0-9_-]{20,80}", game_id) is None:
        raise ClientError(None, "a valid game ID is required")
    return game_id


def _validated_invite_service_url(value: Any) -> str:
    raw = str(value or "").strip()
    parsed = urllib.parse.urlsplit(raw)
    try:
        port = parsed.port
    except ValueError:
        port = -1
    if (
        parsed.scheme.lower() not in {"http", "https"}
        or not parsed.hostname
        or parsed.username
        or parsed.password
        or parsed.query
        or parsed.fragment
        or port == -1
        or (port is not None and not 1 <= port <= 65535)
    ):
        raise ClientError(
            None,
            "owner credentials contain an invalid service URL",
        )
    return urllib.parse.urlunsplit((
        parsed.scheme.lower(), parsed.netloc.lower(),
        parsed.path.rstrip("/"), "", "",
    ))


def _player_invite(
    game_id: Any,
    service: Any,
    join_token: Any,
) -> dict[str, Any]:
    validated_game_id = _validated_game_id(game_id)
    if (
        not isinstance(join_token, str)
        or not join_token.strip()
        or join_token != join_token.strip()
    ):
        raise ClientError(None, "owner credentials do not contain a join token")
    return {
        "schema_version": 1,
        "game_id": validated_game_id,
        "service_url": _validated_invite_service_url(service),
        "join_token": join_token,
    }


def _stage_player_invite(
    destination: str | Path,
    game_id: str,
    service: Any,
    join_token: Any,
) -> Path:
    destination_path = _create_player_invite_path(str(destination), game_id)
    absolute_destination = Path(os.path.abspath(destination_path))
    working_root = Path.cwd().resolve()
    try:
        relative_destination = absolute_destination.relative_to(working_root)
    except ValueError:
        relative_destination = None
    if relative_destination is not None:
        current = working_root
        for part in relative_destination.parts[:-1]:
            current = current / part
            try:
                metadata = current.lstat()
            except FileNotFoundError:
                continue
            except OSError as exc:
                raise ClientError(
                    None, f"cannot inspect player invitation path: {exc}",
                ) from exc
            if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISDIR(
                metadata.st_mode,
            ):
                raise ClientError(
                    None,
                    "player invitation path must not contain symlinks",
                )
    destination_path = absolute_destination
    try:
        metadata = destination_path.lstat()
    except FileNotFoundError:
        pass
    except OSError as exc:
        raise ClientError(None, f"cannot inspect player invitation: {exc}") from exc
    else:
        if not stat.S_ISREG(metadata.st_mode) or destination_path.is_symlink():
            raise ClientError(
                None,
                "player invitation destination must be a regular file",
            )
    return write_private_json(
        destination_path,
        _player_invite(game_id, service, join_token),
    )


def _load_owner_credentials(path: str | Path) -> dict[str, Any]:
    source = Path(path).expanduser()
    try:
        metadata = source.lstat()
    except OSError as exc:
        raise ClientError(None, f"cannot read owner credentials: {exc}") from exc
    if not stat.S_ISREG(metadata.st_mode) or source.is_symlink():
        raise ClientError(None, "owner credentials must be a regular file")
    if stat.S_IMODE(metadata.st_mode) != 0o600:
        raise ClientError(None, "owner credentials must have mode 0600")
    return load_private_json(source)


def _session_main(args: argparse.Namespace) -> int:
    if args.command == "supervisor":
        from .supervisor import Supervisor, make_supervisor_server

        configured = args.admin_token or os.environ.get(args.admin_token_env)
        generated = configured is None
        admin_token = configured or secrets.token_urlsafe(32)
        supervisor = Supervisor(
            args.runs_root, admin_token, binary=args.binary,
            agent_binary=args.agent_binary,
        )
        server = make_supervisor_server(
            supervisor, args.host, args.port, args.public_url,
        )
        ready = {
            "schema_version": 1,
            "state": "ready",
            "pid": os.getpid(),
            "service_url": supervisor.service_url,
            "internal_service_url": supervisor.internal_service_url,
            "runs_root": str(supervisor.runs_root),
            "admin_token_env": args.admin_token_env,
        }
        if generated:
            ready["admin_token"] = admin_token
        ready_path = (
            Path(args.ready_file).expanduser().resolve()
            if args.ready_file else None
        )
        if ready_path is not None:
            write_private_json(ready_path, ready)
        _json(ready)
        print(
            f"supervisor listening at {supervisor.service_url}",
            file=sys.stderr, flush=True,
        )
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            server.server_close()
            supervisor.close()
            if ready_path is not None:
                try:
                    current = load_private_json(ready_path)
                except (OSError, ValueError, json.JSONDecodeError):
                    current = None
                if (
                    isinstance(current, dict)
                    and current.get("pid") == os.getpid()
                    and current.get("internal_service_url")
                    == supervisor.internal_service_url
                ):
                    ready_path.unlink(missing_ok=True)
        return 0

    if args.command == "game":
        base = service_url(getattr(args, "service_url", None))
        if args.game_command == "create":
            admin_token = args.admin_token or os.environ.get(
                "AGENT_EVAL_ADMIN_TOKEN",
            )
            if not admin_token:
                raise ClientError(
                    None,
                    "--admin-token or AGENT_EVAL_ADMIN_TOKEN is required",
                )
            payload = {
                "mode": args.mode,
                "places": args.places,
                "turns": args.turns,
                "ruleset": args.ruleset,
                "objective": args.objective,
                "lobby_timeout_s": args.lobby_timeout_s,
                "frame_interval": args.frame_interval,
                "frame_zoom": args.frame_zoom,
            }
            if args.timing_mode is not None:
                payload["timing_mode"] = args.timing_mode
            if args.control_protocol != "strategic-v1":
                payload["control_protocol"] = args.control_protocol
            if args.difficulty != "hard":
                payload["difficulty"] = args.difficulty
            if args.action_timeout_s is not None:
                payload["action_timeout_s"] = args.action_timeout_s
            if args.seed is not None:
                payload["seed"] = args.seed
            try:
                result = create_game(base, admin_token, payload)
            except ClientError as exc:
                message = str(exc)
                if (
                    exc.status == 400
                    and "timing_mode" in payload
                    and "timing_mode" in message
                    and "unknown" in message.casefold()
                ):
                    raise ClientError(
                        400,
                        f"{message}\nThe running supervisor predates timing "
                        "modes. Stop the existing `just start`, restart it "
                        "with `just start`, then rerun this create command. "
                        "The requested timing mode was not downgraded.",
                    ) from exc
                raise
            game_id = _validated_game_id(result.get("game_id"))
            created_join_token = result.get("join_token")
            if args.credentials:
                credential_path = write_private_json(
                    _create_credentials_path(
                        args.credentials, game_id,
                    ),
                    {
                        "schema_version": 1,
                        "service_url": base,
                        "game_id": game_id,
                        "owner_token": result["owner_token"],
                        "join_token": result["join_token"],
                    },
                )
                result = {
                    key: value for key, value in result.items()
                    if key not in {"owner_token", "join_token"}
                }
                result["credentials_saved"] = True
                result["credentials_file"] = str(credential_path)
                print(
                    f"saved owner/join credentials to {credential_path}",
                    file=sys.stderr,
                )
            if args.player_invite:
                _stage_player_invite(
                    args.player_invite,
                    game_id,
                    base,
                    created_join_token,
                )
                result = {
                    key: value for key, value in result.items()
                    if key not in {"owner_token", "join_token"}
                }
                result["player_invite_saved"] = True
                print(
                    f"staged player invitation for {game_id}",
                    file=sys.stderr,
                )
            _json(result)
            return 0
        if args.game_command == "stage-invite":
            game_id = _validated_game_id(args.game_id)
            credentials = _load_owner_credentials(args.credentials)
            credential_game_id = _validated_game_id(credentials.get("game_id"))
            if credential_game_id != game_id:
                raise ClientError(
                    None,
                    "owner credentials belong to a different game",
                )
            invite = _player_invite(
                game_id,
                credentials.get("service_url"),
                credentials.get("join_token"),
            )
            if args.require_open_lobby:
                status = request_json(
                    "GET",
                    f"{invite['service_url']}/v1/games/{game_id}",
                    timeout=3,
                )
                state = str(status.get("state") or "unknown")
                if state != "lobby":
                    raise ClientError(
                        None,
                        f"game {game_id} is {state}; staging an invitation "
                        "cannot revive a lobby that is no longer open",
                    )
            _stage_player_invite(
                args.output,
                game_id,
                invite["service_url"],
                invite["join_token"],
            )
            print(
                f"staged player invitation for {game_id}",
                file=sys.stderr,
            )
            _json({
                "schema_version": 1,
                "game_id": game_id,
                "player_invite_saved": True,
            })
            return 0
        if args.game_command == "join":
            credentials = (
                load_private_json(args.credentials) if args.credentials else {}
            )
            if args.credentials and args.service_url is None:
                base = service_url(credentials.get("service_url"))
            token = _credential_value(
                args.join_token, "AGENT_EVAL_JOIN_TOKEN",
                credentials, "join_token",
            )
            if not token:
                raise ClientError(
                    None,
                    "--join-token, credentials, or AGENT_EVAL_JOIN_TOKEN is required",
                )
            selected: int | str | None = args.place
            if isinstance(selected, str) and selected.isdigit():
                selected = int(selected)
            metadata = (
                _read_json_argument(args.metadata)
                if args.metadata is not None else None
            )
            negotiated_protocol, advertised_protocols = join_capabilities(
                base, args.game_id,
            )
            if negotiated_protocol == "full-control-v2":
                advertised_protocols = sorted(set(
                    (advertised_protocols or [])
                    + (args.supported_control_protocols or [])
                ))
            result = join_game(
                base, args.game_id, token, selected,
                args.controller_label, metadata,
                advertised_protocols,
            )
            control_protocol = result.get(
                "control_protocol", "strategic-v1",
            )
            supported_control_protocols = result.get(
                "supported_control_protocols", [],
            )
            session_path = write_private_json(
                args.session,
                {
                    "schema_version": 1,
                    "service_url": base,
                    "game_id": result["game_id"],
                    "agent_id": result["agent_id"],
                    "agent_token": result["agent_token"],
                    "place": result["place"],
                    "seat_id": result["seat_id"],
                    "controller_label": result["controller_label"],
                    "controller_metadata": result["controller_metadata"],
                    "controller_fingerprint": result[
                        "controller_fingerprint"
                    ],
                    "control_protocol": control_protocol,
                    "supported_control_protocols": supported_control_protocols,
                    "timing_mode": result.get("timing_mode"),
                    "action_timeout_s": result.get("action_timeout_s"),
                },
            )
            result = {
                key: value for key, value in result.items()
                if key != "agent_token"
            }
            result["session_saved"] = True
            result["session_file"] = str(session_path)
            print(f"saved agent session to {session_path}", file=sys.stderr)
            _json(result)
            return 0
        if args.game_command == "status":
            _json(request_json(
                "GET", f"{base}/v1/games/{args.game_id}/status",
            ))
            return 0
        if args.game_command == "result":
            _json(request_json(
                "GET", f"{base}/v1/games/{args.game_id}/result",
            ))
            return 0
        if args.game_command == "watch":
            _json({
                "schema_version": 1,
                "game_id": args.game_id,
                "watch_url": f"{base}/watch/{args.game_id}",
                "watch_json_url": f"{base}/v1/games/{args.game_id}/watch.json",
                "video_url": f"{base}/v1/games/{args.game_id}/video.mp4",
            })
            return 0
        if args.game_command == "native-viewer":
            credentials = (
                load_private_json(args.credentials) if args.credentials else {}
            )
            if args.credentials and args.service_url is None:
                base = service_url(credentials.get("service_url"))
            token = _credential_value(
                args.owner_token, "AGENT_EVAL_OWNER_TOKEN",
                credentials, "owner_token",
            )
            if not token:
                raise ClientError(None, "owner token is required")
            require_native_viewer_protocol(base, args.game_id)
            viewer_result = request_native_viewer(
                base, args.game_id, token,
            )
            if args.lease_file:
                lease_path = write_private_json(
                    args.lease_file,
                    {
                        "schema_version": 1,
                        "service_url": base,
                        "game_id": args.game_id,
                        "lease_id": viewer_result["lease_id"],
                    },
                )
                viewer_result["lease_file"] = str(lease_path)
            _json(viewer_result)
            return 0
        if args.game_command == "native-viewer-run":
            credentials = (
                load_private_json(args.credentials) if args.credentials else {}
            )
            if args.credentials and args.service_url is None:
                base = service_url(credentials.get("service_url"))
            token = _credential_value(
                args.owner_token, "AGENT_EVAL_OWNER_TOKEN",
                credentials, "owner_token",
            )
            if not token:
                raise ClientError(None, "owner token is required")
            if not args.poll_interval_s > 0:
                raise ClientError(None, "poll interval must be greater than zero")
            if not args.disconnect_grace_s >= 0:
                raise ClientError(None, "disconnect grace must be non-negative")
            try:
                require_native_viewer_protocol(base, args.game_id)
            except NativeViewerCompatibilityError:
                if not args.credentials:
                    raise ClientError(
                        None,
                        "snapshot watch room fallback requires the game's "
                        "standard owner credentials path",
                    )
                from .watch_room import run_snapshot_watch_room

                print(
                    "The running supervisor is too old for safe live observer "
                    "activation; opening an isolated snapshot watch room.",
                    file=sys.stderr,
                    flush=True,
                )
                return run_snapshot_watch_room(
                    args.game_id,
                    credentials_path=args.credentials,
                    server_binary=args.snapshot_server,
                    client_binary=args.client,
                    data_path=args.data_path,
                )
            return run_native_viewer_client(
                base,
                args.game_id,
                token,
                client_binary=args.client,
                data_path=args.data_path,
                log_dir=args.log_dir,
                lease_file=args.lease_file,
                poll_interval_s=args.poll_interval_s,
                disconnect_grace_s=args.disconnect_grace_s,
            )
        if args.game_command == "native-viewer-status":
            credentials = (
                load_private_json(args.credentials) if args.credentials else {}
            )
            lease = (
                load_private_json(args.lease_file) if args.lease_file else {}
            )
            if args.service_url is None and (credentials or lease):
                base = service_url(
                    credentials.get("service_url")
                    or lease.get("service_url"),
                )
            token = _credential_value(
                args.owner_token, "AGENT_EVAL_OWNER_TOKEN",
                credentials, "owner_token",
            )
            if not token:
                raise ClientError(None, "owner token is required")
            lease_id = args.lease_id or lease.get("lease_id")
            if not lease_id:
                raise ClientError(None, "lease ID or lease file is required")
            if lease and lease.get("game_id") != args.game_id:
                raise ClientError(None, "lease file belongs to a different game")
            _json(native_viewer_status(
                base, args.game_id, token, lease_id,
            ))
            return 0
        if args.game_command == "native-viewer-release":
            credentials = (
                load_private_json(args.credentials) if args.credentials else {}
            )
            lease = (
                load_private_json(args.lease_file) if args.lease_file else {}
            )
            if args.service_url is None and (credentials or lease):
                base = service_url(
                    credentials.get("service_url")
                    or lease.get("service_url"),
                )
            token = _credential_value(
                args.owner_token, "AGENT_EVAL_OWNER_TOKEN",
                credentials, "owner_token",
            )
            if not token:
                raise ClientError(None, "owner token is required")
            lease_id = args.lease_id or lease.get("lease_id")
            if not lease_id:
                raise ClientError(None, "lease ID or lease file is required")
            if lease and lease.get("game_id") != args.game_id:
                raise ClientError(None, "lease file belongs to a different game")
            _json(release_native_viewer(
                base, args.game_id, token, lease_id,
            ))
            return 0
        if args.game_command == "cancel":
            credentials = (
                load_private_json(args.credentials) if args.credentials else {}
            )
            if args.credentials and args.service_url is None:
                base = service_url(credentials.get("service_url"))
            token = _credential_value(
                args.owner_token, "AGENT_EVAL_OWNER_TOKEN",
                credentials, "owner_token",
            )
            if not token:
                raise ClientError(None, "owner token is required")
            _json(request_json(
                "POST", f"{base}/v1/games/{args.game_id}/cancel",
                token=token, body={},
            ))
            return 0

    if args.command == "agent":
        session = load_private_json(args.session)
        if args.agent_command == "next":
            _json(next_turn(session, args.after_turn, args.wait_s))
            return 0
        if args.agent_command == "act":
            action = _read_json_argument(args.action)
            telemetry = (
                _read_json_argument(args.telemetry)
                if args.telemetry is not None else None
            )
            _json(submit_action(
                session, args.turn, args.observation_id, action, telemetry,
            ))
            return 0

    if args.command == "bot":
        from .bot import run_bot

        _json(run_bot(args.session))
        return 0
    return -1


def _legacy_main(args: argparse.Namespace) -> int:
    from .agentd import external_tokens_from_environment, make_server
    from .config import ConfigError
    from .runner import RunError, default_episode_path, render_episode
    from .scoring import aggregate_leaderboard, summarize_episode

    def announce(
        episode: Path, control_path: Path, control: dict[str, object],
    ) -> None:
        print(
            "external control ready: "
            f"episode={episode} control={control_path} "
            f"agentd_url={control['agentd_url']}",
            flush=True,
        )

    if args.command == "serve":
        config = load_config(args.config)
        internal_token = os.environ.get("AGENT_EVAL_INTERNAL_TOKEN", "")
        if not internal_token:
            raise ConfigError("AGENT_EVAL_INTERNAL_TOKEN is required for serve")
        server = make_server(
            config, args.host, args.port,
            Path(args.trace).resolve() if args.trace else None,
            internal_token=internal_token,
            external_tokens=external_tokens_from_environment(config),
        )
        host, port = server.server_address
        print(f"agentd listening on http://{host}:{port}", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            server.server_close()
        return 0
    if args.command == "run":
        config = load_config(args.config)
        seed = args.seed if args.seed is not None else config.seeds[0]
        output = (
            Path(args.output) if args.output
            else default_episode_path(
                Path(args.runs_root), config, seed, args.rotation,
            )
        )
        callback = announce if any(
            seat.type == "external" for seat in config.seats
        ) else None
        summary = run_episode(
            config, output, seed=seed, rotation=args.rotation,
            on_ready=callback,
        )
        _json(summary)
        return 0 if summary["manifest"].get("status") == "completed" else 1
    if args.command == "eval":
        config = load_config(args.config)
        summaries = []
        failures = 0
        invalid_benchmarks = 0
        root = Path(args.runs_root)
        for seed in config.seeds:
            for rotation in range(len(config.seats)):
                output = default_episode_path(root, config, seed, rotation)
                summary = run_episode(
                    config, output, seed=seed, rotation=rotation,
                    on_ready=(
                        announce if any(
                            seat.type == "external" for seat in config.seats
                        ) else None
                    ),
                )
                summaries.append(summary)
                failures += int(summary["manifest"].get("status") != "completed")
                invalid_benchmarks += int(
                    not summary["manifest"].get("benchmark_valid", False)
                )
        result = {
            "schema_version": 1,
            "leaderboard": aggregate_leaderboard(summaries),
            "episodes": summaries,
        }
        root.mkdir(parents=True, exist_ok=True)
        report_path = root / f"{config.name}-eval.json"
        report_path.write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        _json({
            "report": str(report_path),
            "episodes": len(summaries),
            "failures": failures,
            "invalid_benchmarks": invalid_benchmarks,
        })
        return 1 if failures else 0
    if args.command == "report":
        summaries = [summarize_episode(path) for path in args.episode]
        value = {
            "schema_version": 1,
            "leaderboard": aggregate_leaderboard(summaries),
            "episodes": summaries,
        }
        text = json.dumps(value, indent=2, sort_keys=True) + "\n"
        if args.output:
            Path(args.output).write_text(text, encoding="utf-8")
        else:
            print(text, end="")
        return 0
    if args.command == "render":
        print(render_episode(args.episode, args.output, args.fps))
        return 0
    raise ConfigError(f"unknown legacy command {args.command}")


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        result = _session_main(args)
        if result >= 0:
            return result
        return _legacy_main(args)
    except ClientError as exc:
        prefix = f"HTTP {exc.status}: " if exc.status is not None else ""
        print(f"error: {prefix}{exc}", file=sys.stderr)
        return 2
    except (OSError, ValueError, KeyError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    except Exception as exc:
        # Preserve the legacy CLI's concise error contract without importing
        # provider-backed modules on the supervisor path.
        if exc.__class__.__module__.startswith("agent_eval"):
            print(f"error: {exc}", file=sys.stderr)
            return 2
        raise


if __name__ == "__main__":
    raise SystemExit(main())
