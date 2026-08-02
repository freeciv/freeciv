#!/usr/bin/env python3
"""Standalone, player-only Freeciv session client.

This module intentionally has no imports from the parent Freeciv repository.
It can be copied or mounted with ``play/`` as the harness's entire workspace.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import secrets
import stat
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parent
DEFAULT_SERVICE_URL = "http://127.0.0.1:8765"
GAME_ID_RE = re.compile(r"^game_[A-Za-z0-9_-]{20,80}$")
CONTROLLER_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{2,95}$")
TERMINAL_STATES = {"completed", "invalid", "failed", "cancelled"}


class PlayerError(RuntimeError):
    """A stable, user-facing player client failure."""


class _RejectRedirects(urllib.request.HTTPRedirectHandler):
    """Keep bearer credentials on the exact configured supervisor origin."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def service_url(value: str | None = None) -> str:
    raw = (value or os.environ.get("AGENT_EVAL_SERVICE_URL")
           or DEFAULT_SERVICE_URL).strip()
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
        raise PlayerError(
            "AGENT_EVAL_SERVICE_URL must be an http(s) URL without "
            "credentials, query, or fragment"
        )
    return urllib.parse.urlunsplit((
        parsed.scheme.lower(), parsed.netloc.lower(),
        parsed.path.rstrip("/"), "", "",
    ))


def request_json(
    method: str,
    url: str,
    *,
    token: str | None = None,
    body: dict[str, Any] | None = None,
    timeout: float = 60,
) -> dict[str, Any]:
    data = None
    if body is not None:
        data = json.dumps(
            body, sort_keys=True, separators=(",", ":"),
        ).encode("utf-8")
    headers = {"Accept": "application/json"}
    if data is not None:
        headers["Content-Type"] = "application/json"
    if token:
        headers["Authorization"] = f"Bearer {token}"
    request = urllib.request.Request(
        url, data=data, headers=headers, method=method,
    )
    try:
        opener = urllib.request.build_opener(_RejectRedirects())
        with opener.open(request, timeout=timeout) as response:
            value = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        with exc:
            try:
                payload = json.loads(exc.read().decode("utf-8"))
                error = payload.get("error")
                if isinstance(error, dict):
                    message = str(
                        error.get("message") or error.get("code") or exc
                    )
                else:
                    message = str(error or exc)
            except (
                OSError, UnicodeDecodeError, json.JSONDecodeError,
                AttributeError,
            ):
                message = str(exc)
        raise PlayerError(f"HTTP {exc.code}: {message}") from exc
    except urllib.error.URLError as exc:
        reason = getattr(exc, "reason", exc)
        parsed_url = urllib.parse.urlsplit(url)
        origin = urllib.parse.urlunsplit((
            parsed_url.scheme, parsed_url.netloc, "", "", "",
        ))
        raise PlayerError(
            f"cannot reach the Freeciv supervisor at {origin}: "
            f"{reason}. Stop and tell the user; do not retry in a loop."
        ) from exc
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise PlayerError(f"invalid supervisor response: {exc}") from exc
    if not isinstance(value, dict):
        raise PlayerError("the supervisor returned a non-object JSON response")
    return value


def _write_private_json(path: Path, value: dict[str, Any]) -> Path:
    destination = path.expanduser().resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(
        f".{destination.name}.{secrets.token_hex(6)}.tmp"
    )
    descriptor = os.open(
        temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600,
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, destination)
        os.chmod(destination, 0o600)
    finally:
        temporary.unlink(missing_ok=True)
    return destination


def _load_object(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise PlayerError(f"cannot read {label} {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise PlayerError(f"{label} {path} must contain a JSON object")
    return value


def _game_id(value: str) -> str:
    if not GAME_ID_RE.fullmatch(value):
        raise PlayerError("a valid assigned game ID is required")
    return value


def _controller_name(value: str) -> str:
    if (
        not CONTROLLER_RE.fullmatch(value)
        or "-" not in value
        or value.startswith("-")
        or value.endswith("-")
        or value.casefold() in {"agent", "harness-model"}
    ):
        raise PlayerError(
            "--name must be a truthful non-generic harness-model label, "
            "for example codex-gpt-5.6-sol or claude-code-claude-opus"
        )
    return value


def _session_key(controller: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", controller.lower()).strip("-")
    digest = hashlib.sha256(controller.encode("utf-8")).hexdigest()[:12]
    return f"{slug or 'controller'}-{digest}"


def _state_root() -> Path:
    configured = os.environ.get("PLAY_STATE_DIR", ".sessions")
    path = Path(configured).expanduser()
    if not path.is_absolute():
        path = ROOT / path
    resolved = path.resolve()
    if not resolved.is_relative_to(ROOT.resolve()):
        raise PlayerError("PLAY_STATE_DIR must stay inside the player workspace")
    return resolved


def _set_current_session(session_path: Path) -> None:
    root = _state_root()
    root.mkdir(parents=True, exist_ok=True)
    resolved_session = session_path.resolve()
    if not resolved_session.is_relative_to(root):
        raise PlayerError("session files must stay inside PLAY_STATE_DIR")
    pointer = root / "current"
    temporary = pointer.with_name(f".current.{secrets.token_hex(6)}.tmp")
    relative = os.path.relpath(resolved_session, root)
    descriptor = os.open(
        temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600,
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            stream.write(relative + "\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, pointer)
        os.chmod(pointer, 0o600)
    finally:
        temporary.unlink(missing_ok=True)


def _session_path(explicit: str) -> Path:
    value = explicit.strip() or os.environ.get("PLAY_SESSION", "").strip()
    if value:
        path = Path(value).expanduser()
        if not path.is_absolute():
            path = ROOT / path
        resolved = path.resolve()
        if not resolved.is_relative_to(_state_root()):
            raise PlayerError("session files must stay inside PLAY_STATE_DIR")
        return resolved
    root = _state_root()
    sessions = sorted({
        candidate.resolve()
        for candidate in root.glob("game_*/*.json")
        if candidate.resolve().is_relative_to(root)
        and candidate.resolve().is_file()
    })
    if len(sessions) > 1:
        raise PlayerError(
            "multiple private sessions exist in this player workspace; "
            "the shared .sessions/current pointer is ambiguous. Use the exact "
            "`--session SESSION_FILE` returned by your join on every `just "
            "next` and `just act` command."
        )
    if len(sessions) == 1:
        return sessions[0]
    pointer = _state_root() / "current"
    try:
        relative = pointer.read_text(encoding="utf-8").strip()
    except OSError as exc:
        raise PlayerError(
            "no current session; run `just join --game_id ... --name ...` first"
        ) from exc
    path = Path(relative)
    if path.is_absolute() or ".." in path.parts:
        raise PlayerError("the current-session pointer is invalid")
    resolved = (_state_root() / path).resolve()
    if not resolved.is_relative_to(_state_root()):
        raise PlayerError("the current-session pointer is invalid")
    return resolved


def _load_session(explicit: str) -> tuple[Path, dict[str, Any]]:
    path = _session_path(explicit)
    value = _load_object(path, "session")
    required = {"game_id", "agent_id", "agent_token", "service_url"}
    if not required.issubset(value) or not isinstance(value["agent_token"], str):
        raise PlayerError(f"session {path} is incomplete")
    return path, value


def _invite(args: argparse.Namespace) -> tuple[str, str]:
    configured_invite = (
        args.invite.strip() or os.environ.get("PLAY_INVITE", "").strip()
    )
    explicit_token = (
        args.join_token.strip()
        or os.environ.get("AGENT_EVAL_JOIN_TOKEN", "").strip()
    )
    # A CLI/environment token is a complete credential override. Ignore only
    # the implicit default file in that case so a stale local invitation cannot
    # block documented recovery or redirect the request to its old service URL.
    load_invite = bool(configured_invite) or not explicit_token
    invite: dict[str, Any] = {}
    invite_token = ""
    if load_invite:
        invite_root = ROOT / ".invites"
        try:
            root_metadata = invite_root.lstat()
        except OSError as exc:
            raise PlayerError(
                f"the invitation directory is unavailable. Ask the game "
                f"owner to run `just invite {args.game_id}` from the "
                "repository root, then retry once."
            ) from exc
        if (
            stat.S_ISLNK(root_metadata.st_mode)
            or not stat.S_ISDIR(root_metadata.st_mode)
            or not invite_root.resolve().is_relative_to(ROOT.resolve())
        ):
            raise PlayerError(".invites must be a real directory inside play/")
        invite_path = (
            Path(configured_invite).expanduser()
            if configured_invite
            else invite_root / f"{args.game_id}.json"
        )
        if not invite_path.is_absolute():
            invite_path = ROOT / invite_path
        resolved_invite = invite_path.resolve()
        if not resolved_invite.is_relative_to(invite_root.resolve()):
            raise PlayerError("invite files must stay inside .invites/")
        if not resolved_invite.is_file():
            if configured_invite:
                raise PlayerError(
                    f"the configured invitation for {args.game_id} does not "
                    f"exist. Ask the game owner to run "
                    f"`just invite {args.game_id}` from the repository root, "
                    "then retry once."
                )
        else:
            if resolved_invite.stat().st_mode & 0o777 != 0o600:
                raise PlayerError(
                    f"the invitation for {args.game_id} is not mode 0600. "
                    f"Ask the game owner to run `just invite {args.game_id}` "
                    "from the repository root, then retry once."
                )
            try:
                invite = _load_object(resolved_invite, "invite")
            except PlayerError as exc:
                raise PlayerError(
                    f"the invitation for {args.game_id} is unreadable. "
                    f"Ask the game owner to run `just invite {args.game_id}` "
                    "from the repository root, then retry once."
                ) from exc
            if invite.get("schema_version") != 1:
                raise PlayerError(
                    f"the invitation for {args.game_id} has an unsupported "
                    f"schema. Ask the game owner to run "
                    f"`just invite {args.game_id}` from the repository root, "
                    "then retry once."
                )
            if invite.get("game_id") != args.game_id:
                raise PlayerError(
                    f"the invitation belongs to a different game. Ask the game "
                    f"owner to run `just invite {args.game_id}` from the "
                    "repository root, then retry once."
                )
            stored_token = invite.get("join_token")
            if not explicit_token and (
                not isinstance(stored_token, str)
                or not stored_token.strip()
                or stored_token != stored_token.strip()
            ):
                raise PlayerError(
                    f"the invitation for {args.game_id} has an invalid join "
                    f"token. Ask the game owner to run "
                    f"`just invite {args.game_id}` from the repository root, "
                    "then retry once."
                )
            invite_token = stored_token if isinstance(stored_token, str) else ""
            if not isinstance(invite.get("service_url"), str):
                raise PlayerError(
                    f"the invitation for {args.game_id} has an invalid service "
                    f"URL. Ask the game owner to run "
                    f"`just invite {args.game_id}` from the repository root, "
                    "then retry once."
                )
    token = explicit_token or invite_token
    if not token:
        raise PlayerError(
            f"no join invitation for {args.game_id}. Ask the game owner to "
            f"run `just invite {args.game_id}` from the repository root, "
            "then retry once."
        )
    try:
        base = service_url(
            str(invite.get("service_url") or "") or None
            if load_invite else None
        )
    except PlayerError as exc:
        raise PlayerError(
            f"the invitation for {args.game_id} has an invalid service URL. "
            f"Ask the game owner to run `just invite {args.game_id}` from "
            "the repository root, then retry once."
        ) from exc
    return token, base


def _print_json(value: dict[str, Any]) -> None:
    print(json.dumps(value, indent=2, sort_keys=True))


def command_prompt(args: argparse.Namespace) -> int:
    game_id = args.game_id or "GAME_ID"
    name = args.name or "HARNESS-MODEL"
    place = f" --place {args.place}" if args.place else ""
    print(f"""You are an autonomous Freeciv player in a player-only workspace.

Assigned game ID: {game_id}

Before joining, identify yourself with a truthful public harness-model label,
such as codex-gpt-5.6-sol, pi-gpt-5.6-sol, or claude-code-claude-opus.

Timing is reported by the join response: default gives each agent 180 seconds
per turn, blitz gives 60 seconds, and infinite has no agent deadline. You—the
assigned harness/model—must inspect each observation and choose its action
directly. Do not write, launch, or delegate to an automated bot solely to beat
the clock.

Read AGENTS.md and docs/gameplay.md, then run:

  just join --game_id {game_id} --name {name}{place}

The command returns a `session_file`. Copy that exact path into every command;
never rely on the shared `.sessions/current` pointer. If join reports
`strategic-v1`, repeat:

  just next --session SESSION_FILE --after_turn LAST_TURN
  just act --session SESSION_FILE --turn TURN --observation_id OBSERVATION_ID --action '{{"type":"set_traits","traits":{{"aggressive":0,"builder":20,"expansionist":30,"trader":10}}}}'

Advance LAST_TURN only after `act` returns `accepted: true`. If `act` fails or
returns anything else, do not claim success and do not advance; poll again with
the same explicit session and LAST_TURN so the server can redeliver the turn.

If join reports `full-control-v2`, do not use that strategic loop. The join
response will truthfully report whether the sidecar state/batch API is
available; the current foundation fails safely before starting the game.

Use only private `just next` observations for decisions. Never inspect parent
directories or spectator data. Stop on completed, invalid, failed, or
cancelled. If GAME_ID is still a placeholder, or join fails, stop and ask the
user instead of inventing a game or retrying blindly.""")
    return 0


def command_join(args: argparse.Namespace) -> int:
    args.game_id = _game_id(args.game_id)
    controller = _controller_name(args.name)
    token, base = _invite(args)
    try:
        request_json("GET", base + "/health", timeout=3)
    except PlayerError as exc:
        raise PlayerError(
            f"{exc}\nThe assigned game cannot be joined. Stop and tell the user."
        ) from exc
    status = request_json(
        "GET", f"{base}/v1/games/{args.game_id}/status", timeout=10,
    )
    control_protocol = status.get("control_protocol")
    if control_protocol is None:
        control_protocol = "strategic-v1"
    if control_protocol not in {"strategic-v1", "full-control-v2"}:
        raise PlayerError(
            f"game requires unsupported control protocol {control_protocol!r}"
        )
    body: dict[str, Any] = {"controller_label": controller}
    if control_protocol == "full-control-v2":
        body["supported_control_protocols"] = ["full-control-v2"]
    if args.place:
        body["place"] = int(args.place) if args.place.isdigit() else args.place
    try:
        result = request_json(
            "POST", f"{base}/v1/games/{args.game_id}/join",
            token=token, body=body, timeout=30,
        )
    except PlayerError as exc:
        if str(exc).startswith(("HTTP 401:", "HTTP 403:")):
            raise PlayerError(
                f"{exc}\nThe game invitation may be stale. Ask the game "
                f"owner to run `just invite {args.game_id}` from the "
                "repository root, then retry once."
            ) from exc
        raise
    session = {
        "schema_version": 1,
        "service_url": base,
        "game_id": result.get("game_id"),
        "agent_id": result.get("agent_id"),
        "agent_token": result.get("agent_token"),
        "place": result.get("place"),
        "seat_id": result.get("seat_id"),
        "player_name": result.get("player_name"),
        "controller_label": result.get("controller_label"),
        "controller_metadata": result.get("controller_metadata", {}),
        "controller_fingerprint": result.get("controller_fingerprint"),
        "control_protocol": result.get(
            "control_protocol", "strategic-v1",
        ),
        "supported_control_protocols": result.get(
            "supported_control_protocols", [],
        ),
        "timing_mode": result.get("timing_mode"),
        "action_timeout_s": result.get("action_timeout_s"),
    }
    if not all(isinstance(session.get(key), str) and session[key]
               for key in ("game_id", "agent_id", "agent_token")):
        raise PlayerError("the supervisor returned an incomplete join response")
    path = _state_root() / args.game_id / f"{_session_key(controller)}.json"
    _write_private_json(path, session)
    _set_current_session(path)
    public = {key: value for key, value in result.items()
              if key != "agent_token"}
    public["session_saved"] = True
    public["session_file"] = str(path)
    _print_json(public)
    timing_mode = str(result.get("timing_mode") or "unknown")
    action_timeout_s = result.get("action_timeout_s")
    deadline = (
        "no agent deadline"
        if action_timeout_s is None
        else f"{action_timeout_s:g} seconds per agent turn"
        if isinstance(action_timeout_s, (int, float))
        else "deadline unavailable"
    )
    if session["control_protocol"] == "full-control-v2":
        print(
            f"\nJoined a full-control-v2 session.\nSession file: {path}\n"
            "The headless sidecar and v2 state/action routes are not available "
            "yet, so this game has failed safely without starting. Do not use "
            "the strategic `just next` or `just act` loop for this session.",
            file=sys.stderr,
        )
    else:
        print(
            f"\nJoined in {timing_mode} timing mode: {deadline}.\n"
            "You—the assigned harness/model—must inspect each observation and "
            "choose its action directly. Do not write, launch, or delegate to "
            "an automated bot solely to beat the clock.\n"
            f"Session file: {path}\n"
            "Read docs/gameplay.md, then start with "
            f"`just next --session {path} --after_turn 0`. Use that same exact "
            "session path for every next and act command.",
            file=sys.stderr,
        )
    return 0


def command_next(args: argparse.Namespace) -> int:
    _path, session = _load_session(args.session)
    if session.get("control_protocol", "strategic-v1") != "strategic-v1":
        raise PlayerError(
            "just next is strategic-v1 only; this full-control-v2 session "
            "requires the future sidecar state API"
        )
    if args.after_turn < 0 or not 0 <= args.wait_s <= 300:
        raise PlayerError("after-turn must be >= 0 and wait-s must be in [0, 300]")
    query = urllib.parse.urlencode({
        "after_turn": args.after_turn,
        "wait_s": args.wait_s,
    })
    value = request_json(
        "GET",
        f"{service_url(session['service_url'])}/v1/games/"
        f"{session['game_id']}/me/next?{query}",
        token=session["agent_token"],
        timeout=max(10, args.wait_s + 5),
    )
    if value.get("game_id") not in {None, session["game_id"]}:
        raise PlayerError("the next response belongs to a different game")
    if value.get("agent_id") not in {None, session["agent_id"]}:
        raise PlayerError("the next response belongs to a different agent seat")
    _print_json(value)
    if value.get("state") in TERMINAL_STATES:
        print(f"\nGame is {value['state']}; stop the play loop.", file=sys.stderr)
    return 0


def command_act(args: argparse.Namespace) -> int:
    _path, session = _load_session(args.session)
    if session.get("control_protocol", "strategic-v1") != "strategic-v1":
        raise PlayerError(
            "just act is strategic-v1 only; this full-control-v2 session "
            "requires the future sidecar batch API"
        )
    if args.turn <= 0 or not args.observation_id:
        raise PlayerError("a positive turn and nonempty observation ID are required")
    try:
        action = json.loads(args.action)
    except json.JSONDecodeError as exc:
        raise PlayerError(f"--action must be valid JSON: {exc}") from exc
    if not isinstance(action, dict):
        raise PlayerError("--action must be a JSON object")
    value = request_json(
        "POST",
        f"{service_url(session['service_url'])}/v1/games/"
        f"{session['game_id']}/me/actions",
        token=session["agent_token"],
        body={
            "turn": args.turn,
            "observation_id": args.observation_id,
            "action": action,
        },
        timeout=30,
    )
    if value.get("accepted") is not True:
        raise PlayerError(
            "the supervisor did not acknowledge the action as accepted; "
            "do not advance LAST_TURN"
        )
    expected = {
        "game_id": session["game_id"],
        "agent_id": session["agent_id"],
        "turn": args.turn,
        "place": session.get("place"),
        "seat_id": session.get("seat_id"),
        "controller_label": session.get("controller_label"),
    }
    for key, expected_value in expected.items():
        if (
            expected_value is not None and key in value
            and value[key] != expected_value
        ):
            raise PlayerError(
                f"the accepted action acknowledgement has the wrong {key}; "
                "do not advance LAST_TURN"
            )
    _print_json(value)
    return 0


def command_result(args: argparse.Namespace) -> int:
    game_id = _game_id(args.game_id)
    value = request_json(
        "GET", f"{service_url()}/v1/games/{game_id}/result",
        timeout=10,
    )
    _print_json(value)
    return 0


def parser() -> argparse.ArgumentParser:
    value = argparse.ArgumentParser(
        prog="client.py", description="player-only Freeciv session client",
    )
    commands = value.add_subparsers(dest="command", required=True)

    prompt = commands.add_parser("prompt")
    prompt.add_argument("--game-id", default="GAME_ID")
    prompt.add_argument("--name", default="HARNESS-MODEL")
    prompt.add_argument("--place", default="")
    prompt.set_defaults(handler=command_prompt)

    join = commands.add_parser("join")
    join.add_argument("--game-id", required=True)
    join.add_argument("--name", required=True)
    join.add_argument("--place", default="")
    join.add_argument("--invite", default="")
    join.add_argument("--join-token", default="")
    join.set_defaults(handler=command_join)

    next_command = commands.add_parser("next")
    next_command.add_argument("--session", default="")
    next_command.add_argument("--after-turn", type=int, default=0)
    next_command.add_argument("--wait-s", type=float, default=120)
    next_command.set_defaults(handler=command_next)

    act = commands.add_parser("act")
    act.add_argument("--session", default="")
    act.add_argument("--turn", type=int, required=True)
    act.add_argument("--observation-id", required=True)
    act.add_argument("--action", required=True)
    act.set_defaults(handler=command_act)

    result = commands.add_parser("result")
    result.add_argument("--game-id", required=True)
    result.set_defaults(handler=command_result)
    return value


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        return int(args.handler(args))
    except PlayerError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
