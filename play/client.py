#!/usr/bin/env python3
"""Standalone, player-only Freeciv session client.

This module intentionally has no imports from the parent Freeciv repository.
It can be copied or mounted with ``play/`` as the harness's entire workspace.
"""

from __future__ import annotations

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import os
import re
import secrets
import stat
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Any

# Layer L1 of the context redesign: a sanctioned, file-based projection of the
# pages this seat already received.  It lives beside this module inside the
# player workspace and imports nothing back from here at import time.
import state_mirror


ROOT = Path(__file__).resolve().parent
DEFAULT_SERVICE_URL = "http://127.0.0.1:8765"
GAME_ID_RE = re.compile(r"^game_[A-Za-z0-9_-]{20,80}$")
CONTROLLER_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{2,95}$")
TERMINAL_STATES = {"completed", "invalid", "failed", "cancelled"}
FULL_CONTROL_V2 = "full-control-v2"
V2_RECEIPT_STATES = {"accepted", "applied", "rejected", "ambiguous"}
V2_TERMINAL_RECEIPTS = {"applied", "rejected", "ambiguous"}
V2_ERROR_CODES = {
    "action_expired", "action_outcome_ambiguous", "conflict",
    "cursor_expired", "illegal_action", "internal_error", "invalid_batch", "invalid_request",
    "not_implemented", "rate_limited", "scope_too_large",
    "sidecar_unavailable", "stale_revision", "unsupported_protocol",
}
V2_SECTIONS = {
    "overview", "pregame_nations", "pregame_styles", "pregame_teams",
    "chat_recipients", "votes", "research",
    "governments", "diplomacy",
    "diplomacy_clauses", "known_tiles", "map_tiles", "infrastructure",
    "cities",
    "city_sites", "units",
    "multipliers", "spaceship", "tombstones", "chat", "city_detail", "city_citizens",
    "city_build_choices", "city_worklist", "city_improvements",
    "city_worker_tasks", "city_trade_routes", "tile_window",
    "city_governor", "unit_route",
}
V2_CITY_SECTIONS = {
    "city_detail", "city_citizens", "city_build_choices", "city_worklist",
    "city_improvements", "city_worker_tasks", "city_trade_routes",
    "city_governor",
}
V2_TURN_SECTIONS = ("overview", "cities", "units", "research")
V2_TURN_PAGE_LIMIT = 16
V2_LEGAL_MATCH_LIMIT = 64
# One actor's whole catalog is the `--actor_id ... --all` promise; a real unit
# catalog runs past 64 rows, so the actor form is bounded by the byte cap
# alone unless the agent asks for a window with --limit.
V2_LEGAL_ACTOR_MATCH_LIMIT = 4096
# Stand-in printed for a subject key whose value must not enter agent context.
# The key still renders, so a discriminator can never vanish silently.
V2_WITHHELD = "<withheld>"
V2_LEGAL_DRAIN_MAX_PAGES = 512
V2_LEGAL_COMPACT_MAX_BYTES = 48 * 1024
V2_LEGAL_SINGLE_ACTION_MAX_BYTES = 64 * 1024
V2_PHASE_STATES = {
    "synchronizing", "native_phase", "phase_not_ready", "inactive_done",
    "awaiting_agent", "ending", "ambiguous_ending", "terminalizing",
}
V2_SIDECAR_FIELDS = {
    "state", "generation", "player_name", "started_at", "ready_at",
    "last_seen_at", "stopped_at", "exit_code", "error_code",
    "client_state", "server_connected", "seat_state",
}
OPAQUE_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
ACTION_KIND_RE = re.compile(r"^[a-z][a-z0-9_]*\.[a-z][a-z0-9_]*$")
CURSOR_RE = re.compile(r"^cursor_[A-Za-z0-9_-]{32}$")
CATALOG_RE = re.compile(r"^catalog_[A-Za-z0-9_-]{32}$")
CITY_ID_RE = re.compile(r"^city_[0-9a-f]{32}$")
TILE_ID_RE = re.compile(r"^tile_[0-9a-f]{32}$")
ACTOR_ID_RE = re.compile(r"^(?:player|city|unit)_[0-9a-f]{32}$")
RELATION_ID_RE = re.compile(r"^relation_[0-9a-f]{32}$")
PLAYER_COLOR_RE = re.compile(r"^#[0-9A-F]{6}$")
# Client-side alias dialect.  Aliases exist only in this process and in the
# private cache; every request still carries the server's opaque ID.
ACTION_ALIAS_RE = re.compile(r"^a([1-9][0-9]{0,3})$")
ENTITY_ALIAS_RE = re.compile(r"^([ucpr])([1-9][0-9]{0,3})$")
TILE_ALIAS_RE = re.compile(r"^[Tt]\((-?[0-9]{1,4}), ?(-?[0-9]{1,4})\)$")
TILE_KEY_RE = re.compile(r"^(-?[0-9]{1,4}),(-?[0-9]{1,4})$")
ALIAS_ENTITY_PREFIXES = {
    "unit": "u", "city": "c", "player": "p", "relation": "r",
}
ALIAS_ENTITY_TYPES = {
    prefix: kind for kind, prefix in ALIAS_ENTITY_PREFIXES.items()
}
ALIAS_ENTITY_KEYS = ("id", "relation_id", "player_id")
V2_MAX_ACTION_ALIASES = 8192
V2_MAX_ENTITY_ALIASES = 4096
V2_MAX_TILE_ALIASES = 4096
V2_MAX_DRAINED_ACTORS = 4096
V2_STATE_LOCK_TIMEOUT_S = 5.0
V2_REQUEST_LOCK_TIMEOUT_S = 45.0
V2_DISPOSITIONS = {
    "receipt_terminal", "receipt_poll", "receipt_first", "retry_exact",
    "refresh",
}
V2_WAKE_REASONS = {
    "phase_active", "game_terminal", "revision_changed", "timeout",
}
V2_EVALUATION_FIELDS = {"objective", "max_turns", "turns_remaining"}


class PlayerError(RuntimeError):
    """A stable, user-facing player client failure."""


class V2ResponseError(PlayerError):
    """A validated non-receipt full-control-v2 error response."""

    def __init__(self, status: int, payload: dict[str, Any]):
        self.status = status
        self.payload = payload
        super().__init__(
            f"HTTP {status}: {payload['error']['message']} "
            f"({payload['error']['code']})"
        )


@dataclass(frozen=True)
class JSONResponse:
    status: int
    value: dict[str, Any]


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
    response = request_json_response(
        method, url, token=token, body=body, timeout=timeout,
    )
    if not 200 <= response.status < 300:
        error = response.value.get("error")
        if isinstance(error, dict):
            message = str(error.get("message") or error.get("code"))
        else:
            message = str(error or f"HTTP {response.status}")
        raise PlayerError(f"HTTP {response.status}: {message}")
    return response.value


def request_json_response(
    method: str,
    url: str,
    *,
    token: str | None = None,
    body: dict[str, Any] | None = None,
    encoded_body: bytes | None = None,
    timeout: float = 60,
) -> JSONResponse:
    """Return object JSON for every HTTP status without following redirects.

    Strategic-v1 continues to call :func:`request_json`, which raises the same
    stable exceptions on non-2xx responses. Full-control-v2 uses this lower
    layer so a valid rejected receipt or structured error is not destroyed by
    the HTTP status.
    """
    if body is not None and encoded_body is not None:
        raise PlayerError("a request cannot contain two JSON bodies")
    data = None
    if encoded_body is not None:
        data = encoded_body
    elif body is not None:
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
            status = response.status
    except urllib.error.HTTPError as exc:
        with exc:
            try:
                value = json.loads(exc.read().decode("utf-8"))
            except (
                OSError, UnicodeDecodeError, json.JSONDecodeError,
                AttributeError,
            ) as decode_exc:
                raise PlayerError(f"HTTP {exc.code}: {exc.reason}") from decode_exc
        status = exc.code
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
    return JSONResponse(status=status, value=value)


def _state_relative_path(path: Path) -> tuple[Path, Path]:
    """Return a lexical private-state path without following its components."""
    root = _state_root()
    destination = Path(os.path.abspath(path.expanduser()))
    # macOS presents the same temporary directory as both /var and
    # /private/var. Canonicalize only the already-trusted workspace prefix;
    # resolving the complete destination would follow the very nested symlink
    # this routine exists to reject.
    lexical_workspace = Path(os.path.abspath(ROOT.expanduser()))
    try:
        workspace_relative = destination.relative_to(lexical_workspace)
    except ValueError:
        pass
    else:
        destination = ROOT.resolve() / workspace_relative
    try:
        relative = destination.relative_to(root)
    except ValueError as exc:
        raise PlayerError("private state files must stay inside PLAY_STATE_DIR") from exc
    if not relative.parts or any(part in {"", ".", ".."} for part in relative.parts):
        raise PlayerError("private state path is invalid")
    return destination, relative


def _open_state_directory(parts: tuple[str, ...], *, create: bool) -> int:
    """Open a state directory through directory fds, rejecting every symlink."""
    workspace = ROOT.resolve()
    root = _state_root()
    try:
        root_parts = root.relative_to(workspace).parts
    except ValueError as exc:
        raise PlayerError("PLAY_STATE_DIR must stay inside the player workspace") from exc
    flags = (
        os.O_RDONLY
        | getattr(os, "O_DIRECTORY", 0)
        | getattr(os, "O_NOFOLLOW", 0)
        | getattr(os, "O_CLOEXEC", 0)
    )
    try:
        descriptor = os.open(workspace, flags)
    except OSError as exc:
        raise PlayerError("the player workspace is not a safe directory") from exc
    try:
        for part in (*root_parts, *parts):
            if part in {"", ".", ".."}:
                raise PlayerError("private state path is invalid")
            try:
                child = os.open(part, flags, dir_fd=descriptor)
            except FileNotFoundError:
                if not create:
                    raise PlayerError(
                        "private state directory does not exist"
                    )
                try:
                    os.mkdir(part, 0o700, dir_fd=descriptor)
                except FileExistsError:
                    # A competing creator is safe only if the no-follow open
                    # below proves that it created a real directory.
                    pass
                try:
                    child = os.open(part, flags, dir_fd=descriptor)
                except OSError as exc:
                    raise PlayerError(
                        "private state directories must be real directories "
                        "inside PLAY_STATE_DIR"
                    ) from exc
            except OSError as exc:
                raise PlayerError(
                    "private state directories must be real directories "
                    "inside PLAY_STATE_DIR"
                ) from exc
            os.close(descriptor)
            descriptor = child
        return descriptor
    except BaseException:
        os.close(descriptor)
        raise


def _write_private_text(path: Path, text: str) -> Path:
    destination, relative = _state_relative_path(path)
    parent_descriptor = _open_state_directory(
        relative.parts[:-1], create=True,
    )
    name = relative.parts[-1]
    temporary = f".{name}.{secrets.token_hex(6)}.tmp"
    flags = (
        os.O_WRONLY | os.O_CREAT | os.O_EXCL
        | getattr(os, "O_NOFOLLOW", 0)
        | getattr(os, "O_CLOEXEC", 0)
    )
    created = False
    try:
        descriptor = os.open(
            temporary, flags, 0o600, dir_fd=parent_descriptor,
        )
        created = True
        try:
            os.fchmod(descriptor, 0o600)
            with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
                stream.write(text)
                stream.flush()
                os.fsync(stream.fileno())
        except BaseException:
            # fdopen owns the descriptor once constructed. If construction
            # itself failed, close the still-open descriptor here.
            try:
                os.close(descriptor)
            except OSError:
                pass
            raise
        os.replace(
            temporary, name,
            src_dir_fd=parent_descriptor, dst_dir_fd=parent_descriptor,
        )
        created = False
        os.fsync(parent_descriptor)
    except OSError as exc:
        raise PlayerError(
            "cannot safely write private state inside PLAY_STATE_DIR"
        ) from exc
    finally:
        if created:
            try:
                os.unlink(temporary, dir_fd=parent_descriptor)
            except FileNotFoundError:
                pass
        os.close(parent_descriptor)
    return destination


def _write_private_json(path: Path, value: dict[str, Any]) -> Path:
    text = json.dumps(value, indent=2, sort_keys=True) + "\n"
    return _write_private_text(path, text)


def _read_private_text(path: Path, label: str) -> str:
    """Read a mode-0600 state file without following its final symlink."""
    _destination, relative = _state_relative_path(path)
    parent_descriptor = _open_state_directory(
        relative.parts[:-1], create=False,
    )
    flags = (
        os.O_RDONLY
        | getattr(os, "O_NOFOLLOW", 0)
        | getattr(os, "O_CLOEXEC", 0)
    )
    try:
        descriptor = os.open(
            relative.parts[-1], flags, dir_fd=parent_descriptor,
        )
        try:
            metadata = os.fstat(descriptor)
            if (
                not stat.S_ISREG(metadata.st_mode)
                or metadata.st_mode & 0o777 != 0o600
            ):
                raise PlayerError(f"private {label} must be a mode-0600 file")
            with os.fdopen(descriptor, "r", encoding="utf-8") as stream:
                descriptor = -1
                return stream.read()
        finally:
            if descriptor >= 0:
                os.close(descriptor)
    except PlayerError:
        raise
    except OSError as exc:
        raise PlayerError(f"cannot safely read private {label}") from exc
    finally:
        os.close(parent_descriptor)


def _load_private_object(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(_read_private_text(path, label))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise PlayerError(f"cannot read {label}: invalid JSON") from exc
    if not isinstance(value, dict):
        raise PlayerError(f"{label} must contain a JSON object")
    return value


@contextmanager
def _private_advisory_lock(path: Path, *, timeout_s: float):
    """Hold a persistent sibling lock safely on macOS and Linux."""
    _destination, relative = _state_relative_path(path)
    parent_descriptor = _open_state_directory(
        relative.parts[:-1], create=True,
    )
    flags = (
        os.O_RDWR | os.O_CREAT
        | getattr(os, "O_NOFOLLOW", 0)
        | getattr(os, "O_CLOEXEC", 0)
    )
    descriptor = -1
    try:
        descriptor = os.open(
            relative.parts[-1], flags, 0o600, dir_fd=parent_descriptor,
        )
        metadata = os.fstat(descriptor)
        if (
            not stat.S_ISREG(metadata.st_mode)
            or metadata.st_mode & 0o777 != 0o600
        ):
            raise PlayerError("private state lock must be a mode-0600 file")
        deadline = time.monotonic() + timeout_s
        while True:
            try:
                fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except BlockingIOError:
                if time.monotonic() >= deadline:
                    raise PlayerError(
                        "another player command is updating this session; "
                        "retry once after it finishes"
                    )
                time.sleep(0.05)
        try:
            yield
        finally:
            try:
                fcntl.flock(descriptor, fcntl.LOCK_UN)
            except OSError:
                pass
    except PlayerError:
        raise
    except OSError as exc:
        raise PlayerError("cannot safely lock private player state") from exc
    finally:
        if descriptor >= 0:
            os.close(descriptor)
        os.close(parent_descriptor)


def _v2_state_lock_path(session_path: Path) -> Path:
    path = _v2_state_path(session_path)
    return path.with_name(path.name + ".lock")


def _v2_request_lock_path(session_path: Path) -> Path:
    return session_path.with_suffix(".v2-request.lock")


def _v2_state_lock(session_path: Path):
    return _private_advisory_lock(
        _v2_state_lock_path(session_path), timeout_s=V2_STATE_LOCK_TIMEOUT_S,
    )


def _v2_request_lock(session_path: Path):
    return _private_advisory_lock(
        _v2_request_lock_path(session_path),
        timeout_s=V2_REQUEST_LOCK_TIMEOUT_S,
    )


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
    _session, relative = _state_relative_path(session_path)
    parent_descriptor = _open_state_directory(
        relative.parts[:-1], create=False,
    )
    flags = (
        os.O_RDONLY
        | getattr(os, "O_NOFOLLOW", 0)
        | getattr(os, "O_CLOEXEC", 0)
    )
    try:
        descriptor = os.open(
            relative.parts[-1], flags, dir_fd=parent_descriptor,
        )
        try:
            metadata = os.fstat(descriptor)
            if not stat.S_ISREG(metadata.st_mode):
                raise PlayerError("the current session must be a regular file")
        finally:
            os.close(descriptor)
    except OSError as exc:
        raise PlayerError(
            "the current session must be a real file inside PLAY_STATE_DIR"
        ) from exc
    finally:
        os.close(parent_descriptor)
    _write_private_text(root / "current", str(relative) + "\n")


def _session_path(explicit: str) -> Path:
    """Resolve the session file every command works against.

    ``--session`` is optional on every command: an explicit path wins, then
    ``PLAY_SESSION``, then the sole private session in this workspace.  Two or
    more sessions stay fail-closed — the seat must be named, because guessing
    one would act with the wrong seat's credentials.
    """
    value = explicit.strip() or os.environ.get("PLAY_SESSION", "").strip()
    if value:
        path = Path(value).expanduser()
        if not path.is_absolute():
            path = ROOT / path
        destination, _relative = _state_relative_path(path)
        return destination
    root = _state_root()
    sessions: list[Path] = []
    for candidate in root.glob("game_*/*.json"):
        try:
            metadata = candidate.lstat()
        except OSError:
            continue
        if stat.S_ISREG(metadata.st_mode) and metadata.st_mode & 0o777 == 0o600:
            sessions.append(candidate.absolute())
    sessions.sort()
    if len(sessions) > 1:
        raise PlayerError(
            "multiple private sessions exist in this player workspace; "
            "the shared .sessions/current pointer is ambiguous. Use the exact "
            "`--session SESSION_FILE` returned by your join on every command, "
            "or export PLAY_SESSION=SESSION_FILE once for the whole seat."
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
    destination, _relative = _state_relative_path(_state_root() / path)
    return destination


def _load_session(explicit: str) -> tuple[Path, dict[str, Any]]:
    path = _session_path(explicit)
    value = _load_private_object(path, "session")
    required = {"game_id", "agent_id", "agent_token", "service_url"}
    if not required.issubset(value) or not isinstance(value["agent_token"], str):
        raise PlayerError(f"session {path} is incomplete")
    return path, value


def _validate_evaluation_context(
    value: Any,
    label: str,
    *,
    expected: dict[str, Any] | None = None,
    required: bool = False,
) -> dict[str, Any] | None:
    if not isinstance(value, dict):
        raise PlayerError(f"invalid {label}")
    present = set(value).intersection(V2_EVALUATION_FIELDS)
    if not present:
        if required:
            raise PlayerError(f"invalid {label}: evaluation context is missing")
        return None
    if present != V2_EVALUATION_FIELDS:
        raise PlayerError(f"invalid {label}: evaluation context is incomplete")
    objective = value["objective"]
    max_turns = value["max_turns"]
    turns_remaining = value["turns_remaining"]
    if (
        not isinstance(objective, str)
        or not objective
        or objective.strip() != objective
        or isinstance(max_turns, bool)
        or not isinstance(max_turns, int)
        or not 1 <= max_turns <= 5000
        or turns_remaining is not None
        and (
            isinstance(turns_remaining, bool)
            or not isinstance(turns_remaining, int)
            or not 0 <= turns_remaining <= max_turns
        )
    ):
        raise PlayerError(f"invalid {label}: evaluation context is malformed")
    if expected is not None:
        for name in ("objective", "max_turns"):
            if name in expected and value[name] != expected[name]:
                raise PlayerError(
                    f"invalid {label}: evaluation {name} changed"
                )
    return {
        "objective": objective,
        "max_turns": max_turns,
        "turns_remaining": turns_remaining,
    }


def _v2_session(explicit: str) -> tuple[Path, dict[str, Any]]:
    path, session = _load_session(explicit)
    if session.get("control_protocol", "strategic-v1") != FULL_CONTROL_V2:
        raise PlayerError("this command is full-control-v2 only")
    if session.get("schema_version") != 1:
        raise PlayerError("the private session has an unsupported schema")
    if (
        not isinstance(session.get("game_id"), str)
        or not GAME_ID_RE.fullmatch(session["game_id"])
        or not isinstance(session.get("agent_id"), str)
        or not OPAQUE_ID_RE.fullmatch(session["agent_id"])
        or not isinstance(session.get("agent_token"), str)
        or not session["agent_token"]
        or not isinstance(session.get("controller_label"), str)
        or not session["controller_label"]
    ):
        raise PlayerError("the private full-control-v2 session is incomplete")
    _validate_evaluation_context(session, "private v2 session")
    # Re-validation rejects a session edited to smuggle credentials or a query
    # into what must be the sole request origin.
    session["service_url"] = service_url(session.get("service_url"))
    return path, session


def _v2_state_path(session_path: Path) -> Path:
    return session_path.with_suffix(".v2-state")


def _empty_action_aliases() -> dict[str, Any]:
    """Return the empty, revision-scoped action alias bucket."""
    return {"state_revision": None, "by_alias": {}}


def _empty_v2_client_state(session: dict[str, Any]) -> dict[str, Any]:
    return {
        "schema_version": 4,
        "game_id": session["game_id"],
        "agent_id": session["agent_id"],
        "last_revision": None,
        "actions": {},
        "pending_catalogs": {},
        "batches": {},
        "receipts": {},
        # Revision-scoped: an action alias is only ever resolvable while its
        # recorded revision is still the newest revision this client knows.
        "action_aliases": _empty_action_aliases(),
        # Game-stable: entity IDs survive revisions, so u1/c1 are assigned once
        # in first-seen order and never re-point at a different entity.
        "entity_aliases": {},
        "tile_aliases": {},
        # Revision-scoped, like `actions`: the actors whose complete catalog
        # this client drained and promoted at `last_revision`.  Only such a
        # catalog may be claimed equivalent to another one.
        "drained_actors": [],
    }


def _load_v2_client_state_unlocked(
    session_path: Path, session: dict[str, Any],
) -> dict[str, Any]:
    path = _v2_state_path(session_path)
    try:
        metadata = path.lstat()
    except FileNotFoundError:
        return _empty_v2_client_state(session)
    except OSError as exc:
        raise PlayerError(f"cannot inspect private v2 client state {path}") from exc
    if (
        stat.S_ISLNK(metadata.st_mode)
        or not stat.S_ISREG(metadata.st_mode)
        or metadata.st_mode & 0o777 != 0o600
    ):
        raise PlayerError(f"private v2 client state {path} must be mode 0600")
    value = _load_private_object(path, "v2 client state")
    legacy_fields = {
        "schema_version", "game_id", "agent_id", "last_revision",
        "actions", "batches", "receipts",
    }
    staged_fields = legacy_fields | {"pending_catalogs"}
    aliased_fields = staged_fields | {
        "action_aliases", "entity_aliases", "tile_aliases",
    }
    current_fields = aliased_fields | {"drained_actors"}
    legacy = set(value) == legacy_fields and value.get("schema_version") == 1
    staged = set(value) == staged_fields and value.get("schema_version") == 2
    aliased = set(value) == aliased_fields and value.get("schema_version") == 3
    current = set(value) == current_fields and value.get("schema_version") == 4
    if (
        not (legacy or staged or aliased or current)
        or value.get("game_id") != session["game_id"]
        or value.get("agent_id") != session["agent_id"]
        or not isinstance(value.get("actions"), dict)
        or not isinstance(value.get("batches"), dict)
        or not isinstance(value.get("receipts"), dict)
        or (staged or aliased or current)
        and not isinstance(value.get("pending_catalogs"), dict)
    ):
        raise PlayerError(f"private v2 client state {path} is invalid")
    if value["last_revision"] is not None:
        _validate_revision(value["last_revision"])
    # Persisted request bodies are strings specifically so retry can send the
    # exact bytes written before the first POST.
    if any(
        not isinstance(batch_id, str) or not OPAQUE_ID_RE.fullmatch(batch_id)
        or not isinstance(body, str)
        for batch_id, body in value["batches"].items()
    ):
        raise PlayerError(f"private v2 client state {path} is invalid")
    if legacy:
        # A v1 cache cannot prove whether a scoped descriptor came from a
        # complete native catalog.  Preserve durable batches and receipts, but
        # fail closed by dropping every executable action during migration.
        migrated = _empty_v2_client_state(session)
        migrated.update({
            "last_revision": value["last_revision"],
            "batches": value["batches"],
            "receipts": value["receipts"],
        })
        _save_v2_client_state_unlocked(session_path, migrated)
        return migrated
    if staged or aliased:
        # A v2 cache predates the alias dialect and a v3 cache predates the
        # drained-catalog record.  Neither holds anything unsound, so keep
        # every capability: numbering starts at the next enumeration, and no
        # catalog is claimed drained until one is drained again.
        value = dict(value)
        value["schema_version"] = 4
        if staged:
            value["action_aliases"] = _empty_action_aliases()
            value["entity_aliases"] = {}
            value["tile_aliases"] = {}
        value["drained_actors"] = []
        _save_v2_client_state_unlocked(session_path, value)
    _validate_alias_state(value)
    _validate_drained_actors(value["drained_actors"])
    _validate_pending_catalogs(value["pending_catalogs"])
    expired = [
        catalog_id
        for catalog_id, pending in value["pending_catalogs"].items()
        if _cursor_expired(pending["cursor_expires_at"])
    ]
    if expired:
        for catalog_id in expired:
            value["pending_catalogs"].pop(catalog_id, None)
        _save_v2_client_state_unlocked(session_path, value)
    return value


def _save_v2_client_state_unlocked(
    session_path: Path, value: dict[str, Any],
) -> None:
    _write_private_json(_v2_state_path(session_path), value)


def _load_v2_client_state(
    session_path: Path, session: dict[str, Any],
) -> dict[str, Any]:
    with _v2_state_lock(session_path):
        return _load_v2_client_state_unlocked(session_path, session)


def _save_v2_client_state(session_path: Path, value: dict[str, Any]) -> None:
    with _v2_state_lock(session_path):
        _save_v2_client_state_unlocked(session_path, value)


def _exact(value: Any, fields: set[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != fields:
        raise PlayerError(
            f"invalid {label}: expected exactly {', '.join(sorted(fields))}"
        )
    return value


def _opaque(value: Any, label: str) -> str:
    if not isinstance(value, str) or OPAQUE_ID_RE.fullmatch(value) is None:
        raise PlayerError(f"invalid {label}")
    return value


def _json_value(value: Any, label: str, depth: int = 0) -> Any:
    if depth > 12:
        raise PlayerError(f"invalid {label}: JSON is nested too deeply")
    if value is None or isinstance(value, (str, bool, int)):
        return value
    if isinstance(value, float):
        if not (float("-inf") < value < float("inf")):
            raise PlayerError(f"invalid {label}: number is not finite")
        return value
    if isinstance(value, list):
        if len(value) > 8192:
            raise PlayerError(f"invalid {label}: too many items")
        return [_json_value(item, label, depth + 1) for item in value]
    if isinstance(value, dict):
        if len(value) > 2048 or any(
            not isinstance(key, str) or not key or len(key) > 128
            for key in value
        ):
            raise PlayerError(f"invalid {label}: invalid object")
        return {
            key: _json_value(item, label, depth + 1)
            for key, item in value.items()
        }
    raise PlayerError(f"invalid {label}: non-JSON value")


def _validate_revision(value: Any) -> dict[str, Any]:
    raw = _exact(value, {"turn", "revision", "state_token"}, "state revision")
    if any(
        isinstance(raw[name], bool) or not isinstance(raw[name], int)
        or raw[name] < 0
        for name in ("turn", "revision")
    ):
        raise PlayerError("invalid state revision counters")
    return {
        "turn": raw["turn"],
        "revision": raw["revision"],
        "state_token": _opaque(raw["state_token"], "state token"),
    }


def _validate_v2_header(
    value: Any, session: dict[str, Any], *, fields: set[str], label: str,
) -> dict[str, Any]:
    raw = _exact(value, fields, label)
    if raw["schema_version"] != 2 or raw["control_protocol"] != FULL_CONTROL_V2:
        raise PlayerError(f"invalid {label}: protocol mismatch")
    if raw.get("game_id") != session["game_id"]:
        raise PlayerError(f"invalid {label}: response belongs to another game")
    if "agent_id" in raw and raw["agent_id"] != session["agent_id"]:
        raise PlayerError(f"invalid {label}: response belongs to another agent")
    return raw


def _validate_error(value: Any) -> dict[str, Any]:
    raw = _exact(
        value,
        {"schema_version", "control_protocol", "error", "state_revision"},
        "v2 error response",
    )
    if raw["schema_version"] != 2 or raw["control_protocol"] != FULL_CONTROL_V2:
        raise PlayerError("invalid v2 error response: protocol mismatch")
    error = _exact(
        raw["error"], {"code", "message", "retryable", "details"},
        "v2 error",
    )
    if (
        error["code"] not in V2_ERROR_CODES
        or not isinstance(error["message"], str)
        or not error["message"].strip()
        or len(error["message"]) > 500
        or not isinstance(error["retryable"], bool)
        or not isinstance(error["details"], dict)
    ):
        raise PlayerError("invalid v2 error response")
    revision = (
        None if raw["state_revision"] is None
        else _validate_revision(raw["state_revision"])
    )
    return {
        "schema_version": 2,
        "control_protocol": FULL_CONTROL_V2,
        "error": {
            "code": error["code"], "message": error["message"].strip(),
            "retryable": error["retryable"],
            "details": _json_value(error["details"], "error details"),
        },
        "state_revision": revision,
    }


def _validate_descriptor(
    value: Any, revision: dict[str, Any],
) -> dict[str, Any]:
    raw = _exact(
        value,
        {
            "action_id", "kind", "label", "subject", "arguments_schema",
            "state_revision",
        },
        "legal action descriptor",
    )
    action_id = _opaque(raw["action_id"], "action ID")
    if (
        not isinstance(raw["kind"], str)
        or ACTION_KIND_RE.fullmatch(raw["kind"]) is None
        or not isinstance(raw["label"], str)
        or not raw["label"].strip()
        or len(raw["label"]) > 240
        or not isinstance(raw["subject"], dict)
        or not isinstance(raw["arguments_schema"], dict)
        or _validate_revision(raw["state_revision"]) != revision
    ):
        raise PlayerError("invalid legal action descriptor")
    return {
        "action_id": action_id,
        "kind": raw["kind"],
        "label": raw["label"].strip(),
        "subject": _json_value(raw["subject"], "action subject"),
        "arguments_schema": _json_value(
            raw["arguments_schema"], "action arguments schema",
        ),
        "state_revision": revision,
    }


def _validate_cursor_expiry(value: Any) -> str:
    if not isinstance(value, str) or not value.endswith("Z"):
        raise PlayerError("invalid v2 page cursor expiry")
    try:
        parsed = datetime.fromisoformat(value[:-1] + "+00:00")
    except ValueError as exc:
        raise PlayerError("invalid v2 page cursor expiry") from exc
    if parsed.tzinfo is None or parsed.utcoffset() != timezone.utc.utcoffset(None):
        raise PlayerError("invalid v2 page cursor expiry")
    return value


def _legacy_catalog_id(
    session: dict[str, Any], revision: dict[str, Any], scope: dict[str, Any],
) -> str:
    canonical = json.dumps(
        [session["game_id"], session["agent_id"], revision, scope],
        sort_keys=True, separators=(",", ":"), ensure_ascii=True,
    ).encode("ascii")
    return "catalog_" + hashlib.sha256(canonical).hexdigest()[:32]


def _validate_page(
    value: Any, session: dict[str, Any], *, legal: bool,
) -> dict[str, Any]:
    raw = _validate_v2_header(
        value, session,
        fields={
            "schema_version", "control_protocol", "game_id", "agent_id",
            "state_revision", "page",
        },
        label="v2 page",
    )
    revision = _validate_revision(raw["state_revision"])
    page = raw["page"]
    if not isinstance(page, dict):
        raise PlayerError("invalid v2 page envelope")
    fields = set(page)
    legacy_base = {"section", "items", "total_items", "next_cursor"}
    current_base = legacy_base | {"cursor_expires_at"}
    legacy_scoped = fields == legacy_base | {"scope"}
    current_scoped = fields == current_base | {
        "scope", "catalog_id", "catalog_complete",
    }
    if fields not in (legacy_base, current_base) and not (
        legacy_scoped or current_scoped
    ):
        raise PlayerError("invalid v2 page envelope")
    section = page["section"]
    if legal:
        if section != "legal_actions":
            raise PlayerError("invalid legal-actions page section")
    elif section not in V2_SECTIONS:
        raise PlayerError("invalid state page section")
    items = page["items"]
    total = page["total_items"]
    cursor = page["next_cursor"]
    if (
        not isinstance(items, list) or len(items) > 16
        or isinstance(total, bool) or not isinstance(total, int)
        or total < len(items)
        or cursor is not None and (
            not isinstance(cursor, str) or CURSOR_RE.fullmatch(cursor) is None
        )
        or cursor is not None and total <= len(items)
    ):
        raise PlayerError("invalid v2 page pagination")
    if "cursor_expires_at" in page:
        cursor_expiry = page["cursor_expires_at"]
        if cursor is None:
            if cursor_expiry is not None:
                raise PlayerError("invalid v2 page cursor expiry")
        else:
            cursor_expiry = _validate_cursor_expiry(cursor_expiry)
    else:
        cursor_expiry = None
    clean_items = (
        [_validate_descriptor(item, revision) for item in items]
        if legal else [_json_value(item, "state page item") for item in items]
    )
    clean_page: dict[str, Any] = {
        "section": section,
        "items": clean_items,
        "total_items": total,
        "next_cursor": cursor,
        "cursor_expires_at": cursor_expiry,
    }
    if "scope" in page:
        if not legal:
            raise PlayerError("invalid state page scope")
        scope_fields = set(page["scope"]) if isinstance(page["scope"], dict) else set()
        if scope_fields == {"actor_id", "actor_type"}:
            scope = _exact(
                page["scope"], {"actor_id", "actor_type"}, "page scope",
            )
            valid = (
                isinstance(scope["actor_id"], str)
                and ACTOR_ID_RE.fullmatch(scope["actor_id"]) is not None
                and scope["actor_type"] in {"player", "city", "unit"}
            )
        elif scope_fields == {
            "actor_id", "actor_type", "target_id", "target_type",
        }:
            scope = _exact(
                page["scope"],
                {"actor_id", "actor_type", "target_id", "target_type"},
                "page scope",
            )
            valid = (
                isinstance(scope["actor_id"], str)
                and ACTOR_ID_RE.fullmatch(scope["actor_id"]) is not None
                and scope["actor_type"] in {"player", "unit", "city"}
                and isinstance(scope["target_id"], str)
                and (
                    scope["actor_type"] == "player"
                    and RELATION_ID_RE.fullmatch(scope["target_id"]) is not None
                    and scope["target_type"] == "diplomatic_relation"
                    or scope["actor_type"] in {"player", "unit", "city"}
                    and TILE_ID_RE.fullmatch(scope["target_id"]) is not None
                    and scope["target_type"] == "tile"
                )
            )
        else:
            raise PlayerError("invalid legal-actions page scope")
        if not valid:
            raise PlayerError("invalid legal-actions page scope")
        clean_page["scope"] = dict(scope)
        if current_scoped:
            catalog_id = page["catalog_id"]
            complete = page["catalog_complete"]
            if (
                not isinstance(catalog_id, str)
                or CATALOG_RE.fullmatch(catalog_id) is None
                or not isinstance(complete, bool)
                or complete != (cursor is None)
            ):
                raise PlayerError("invalid legal-actions catalog metadata")
        else:
            # Compatibility with an already-running old supervisor.  The
            # inferred identity is private to this client cache; a prefix is
            # still staged and cannot execute until the terminal page arrives.
            catalog_id = _legacy_catalog_id(session, revision, dict(scope))
            complete = cursor is None
        clean_page["catalog_id"] = catalog_id
        clean_page["catalog_complete"] = complete
    return {
        "schema_version": 2,
        "control_protocol": FULL_CONTROL_V2,
        "game_id": session["game_id"],
        "agent_id": session["agent_id"],
        "state_revision": revision,
        "page": clean_page,
    }


def _validate_pending_catalogs(value: Any) -> None:
    if not isinstance(value, dict) or len(value) > 128:
        raise PlayerError("private v2 pending catalogs are invalid")
    for catalog_id, pending in value.items():
        if (
            not isinstance(catalog_id, str)
            or CATALOG_RE.fullmatch(catalog_id) is None
            or not isinstance(pending, dict)
            or set(pending) != {
                "state_revision", "scope", "total_items", "items",
                "next_cursor", "cursor_expires_at",
            }
        ):
            raise PlayerError("private v2 pending catalogs are invalid")
        revision = _validate_revision(pending["state_revision"])
        scope = pending["scope"]
        total = pending["total_items"]
        items = pending["items"]
        cursor = pending["next_cursor"]
        expiry = pending["cursor_expires_at"]
        scope_valid = False
        if isinstance(scope, dict) and set(scope) == {
            "actor_id", "actor_type",
        }:
            scope_valid = (
                isinstance(scope["actor_id"], str)
                and ACTOR_ID_RE.fullmatch(scope["actor_id"]) is not None
                and scope["actor_type"] in {"player", "city", "unit"}
            )
        elif isinstance(scope, dict) and set(scope) == {
            "actor_id", "actor_type", "target_id", "target_type",
        }:
            scope_valid = (
                isinstance(scope["actor_id"], str)
                and ACTOR_ID_RE.fullmatch(scope["actor_id"]) is not None
                and scope["actor_type"] in {"player", "unit", "city"}
                and isinstance(scope["target_id"], str)
                and (
                    scope["actor_type"] == "player"
                    and RELATION_ID_RE.fullmatch(scope["target_id"]) is not None
                    and scope["target_type"] == "diplomatic_relation"
                    or scope["actor_type"] in {"player", "unit", "city"}
                    and TILE_ID_RE.fullmatch(scope["target_id"]) is not None
                    and scope["target_type"] == "tile"
                )
            )
        if (
            not scope_valid
            or isinstance(total, bool) or not isinstance(total, int)
            or not 1 <= total <= 8192
            or not isinstance(items, dict) or not 0 < len(items) < total
            or not isinstance(cursor, str) or CURSOR_RE.fullmatch(cursor) is None
            or expiry is not None and not isinstance(expiry, str)
        ):
            raise PlayerError("private v2 pending catalogs are invalid")
        if expiry is not None:
            _validate_cursor_expiry(expiry)
        for action_id, descriptor in items.items():
            if (
                not isinstance(action_id, str)
                or OPAQUE_ID_RE.fullmatch(action_id) is None
                or _validate_descriptor(descriptor, revision)["action_id"]
                   != action_id
            ):
                raise PlayerError("private v2 pending catalogs are invalid")


def _entity_alias_id_matches(alias: str, identifier: Any) -> bool:
    """Report whether a stored entity alias still names its own ID type."""
    match = ENTITY_ALIAS_RE.fullmatch(alias)
    if match is None or not isinstance(identifier, str):
        return False
    kind = ALIAS_ENTITY_TYPES[match.group(1)]
    pattern = RELATION_ID_RE if kind == "relation" else ACTOR_ID_RE
    return (
        pattern.fullmatch(identifier) is not None
        and identifier.startswith(f"{kind}_")
    )


def _validate_alias_state(value: dict[str, Any]) -> None:
    """Prove the private alias tables are closed before anything resolves.

    A drifted table is never repaired in place: an alias that cannot be proved
    to name exactly one opaque ID must not be able to expand into a request.
    """
    actions = value["action_aliases"]
    if (
        not isinstance(actions, dict)
        or set(actions) != {"state_revision", "by_alias"}
        or not isinstance(actions["by_alias"], dict)
        or len(actions["by_alias"]) > V2_MAX_ACTION_ALIASES
        or actions["state_revision"] is None and actions["by_alias"]
    ):
        raise PlayerError("private v2 action aliases are invalid")
    if actions["state_revision"] is not None:
        _validate_revision(actions["state_revision"])
    action_ids: set[str] = set()
    for alias, entry in actions["by_alias"].items():
        if (
            ACTION_ALIAS_RE.fullmatch(alias) is None
            or not isinstance(entry, dict)
            or set(entry) != {"action_id", "actor_id"}
            or not isinstance(entry["action_id"], str)
            or OPAQUE_ID_RE.fullmatch(entry["action_id"]) is None
            or not isinstance(entry["actor_id"], str)
            or entry["actor_id"]
            and ACTOR_ID_RE.fullmatch(entry["actor_id"]) is None
            or entry["action_id"] in action_ids
        ):
            raise PlayerError("private v2 action aliases are invalid")
        action_ids.add(entry["action_id"])
    entities = value["entity_aliases"]
    if not isinstance(entities, dict) or len(entities) > V2_MAX_ENTITY_ALIASES:
        raise PlayerError("private v2 entity aliases are invalid")
    if len(set(entities.values())) != len(entities) or not all(
        _entity_alias_id_matches(alias, identifier)
        for alias, identifier in entities.items()
    ):
        raise PlayerError("private v2 entity aliases are invalid")
    tiles = value["tile_aliases"]
    if not isinstance(tiles, dict) or len(tiles) > V2_MAX_TILE_ALIASES:
        raise PlayerError("private v2 tile aliases are invalid")
    if len(set(tiles.values())) != len(tiles) or not all(
        TILE_KEY_RE.fullmatch(key) is not None
        and isinstance(identifier, str)
        and TILE_ID_RE.fullmatch(identifier) is not None
        for key, identifier in tiles.items()
    ):
        raise PlayerError("private v2 tile aliases are invalid")


def _validate_drained_actors(value: Any) -> None:
    """Prove the drained-catalog record is a closed list of actor IDs.

    The record only ever says "this actor's complete catalog is in `actions`
    at `last_revision`".  A drifted record could make the renderer claim two
    catalogs are equivalent when one of them was never fully read, so it is
    refused rather than repaired.
    """
    if (
        not isinstance(value, list)
        or len(value) > V2_MAX_DRAINED_ACTORS
        or len(set(value)) != len(value)
        or not all(
            isinstance(actor_id, str)
            and ACTOR_ID_RE.fullmatch(actor_id) is not None
            for actor_id in value
        )
    ):
        raise PlayerError("private v2 drained catalogs are invalid")


def _validate_investigation_observation(
    value: Any, revision: dict[str, Any],
) -> dict[str, Any]:
    raw = _exact(
        value,
        {"id", "type", "source", "freshness", "state_revision", "city"},
        "city investigation observation",
    )
    if (
        raw["type"] != "city_investigation"
        or raw["source"] != "human_client_city_info"
        or raw["freshness"] != "captured_at_receipt_revision"
        or _validate_revision(raw["state_revision"]) != revision
    ):
        raise PlayerError("invalid city investigation observation provenance")
    city = _exact(
        raw["city"],
        {
            "id", "name", "size", "production", "shields",
            "improvements", "citizens",
        },
        "city investigation observation city",
    )
    if (
        not isinstance(city["name"], str) or not city["name"]
        or isinstance(city["size"], bool) or not isinstance(city["size"], int)
        or city["size"] < 1
    ):
        raise PlayerError("invalid city investigation observation city")
    production = _exact(
        city["production"], {"id", "kind", "name"},
        "city investigation production",
    )
    if (
        production["kind"] not in {"unit", "improvement"}
        or not isinstance(production["name"], str) or not production["name"]
    ):
        raise PlayerError("invalid city investigation production")
    shields = _exact(
        city["shields"], {"stock", "surplus"},
        "city investigation shields",
    )
    if (
        isinstance(shields["stock"], bool)
        or not isinstance(shields["stock"], int) or shields["stock"] < 0
        or isinstance(shields["surplus"], bool)
        or not isinstance(shields["surplus"], int)
    ):
        raise PlayerError("invalid city investigation shields")
    improvements = city["improvements"]
    if not isinstance(improvements, list) or len(improvements) > 1024:
        raise PlayerError("invalid city investigation improvements")
    clean_improvements: list[dict[str, str]] = []
    for item in improvements:
        improvement = _exact(
            item, {"id", "name"}, "city investigation improvement",
        )
        if not isinstance(improvement["name"], str) or not improvement["name"]:
            raise PlayerError("invalid city investigation improvement")
        clean_improvements.append({
            "id": _opaque(improvement["id"], "improvement ID"),
            "name": improvement["name"],
        })
    if (
        len({item["id"] for item in clean_improvements})
        != len(clean_improvements)
        or len({item["name"] for item in clean_improvements})
           != len(clean_improvements)
    ):
        raise PlayerError("invalid city investigation improvements")
    citizens = _exact(
        city["citizens"], {"feelings", "specialists"},
        "city investigation citizens",
    )
    stages = (
        "base", "luxury", "effects", "nationality", "martial_law", "final",
    )
    feelings = citizens["feelings"]
    if not isinstance(feelings, list) or len(feelings) != len(stages):
        raise PlayerError("invalid city investigation feelings")
    clean_feelings: list[dict[str, Any]] = []
    for index, item in enumerate(feelings):
        feeling = _exact(
            item, {"stage", "happy", "content", "unhappy", "angry"},
            "city investigation feeling",
        )
        if feeling["stage"] != stages[index] or any(
            isinstance(feeling[key], bool)
            or not isinstance(feeling[key], int) or feeling[key] < 0
            for key in ("happy", "content", "unhappy", "angry")
        ):
            raise PlayerError("invalid city investigation feeling")
        clean_feelings.append(dict(feeling))
    specialists = citizens["specialists"]
    if not isinstance(specialists, list) or len(specialists) > 256:
        raise PlayerError("invalid city investigation specialists")
    clean_specialists: list[dict[str, Any]] = []
    for item in specialists:
        specialist = _exact(
            item, {"id", "name", "count"},
            "city investigation specialist",
        )
        if (
            not isinstance(specialist["name"], str) or not specialist["name"]
            or isinstance(specialist["count"], bool)
            or not isinstance(specialist["count"], int)
            or specialist["count"] < 0
        ):
            raise PlayerError("invalid city investigation specialist")
        clean_specialists.append({
            "id": _opaque(specialist["id"], "specialist ID"),
            "name": specialist["name"], "count": specialist["count"],
        })
    if (
        len({item["id"] for item in clean_specialists})
        != len(clean_specialists)
        or len({item["name"] for item in clean_specialists})
           != len(clean_specialists)
    ):
        raise PlayerError("invalid city investigation specialists")
    specialist_population = sum(item["count"] for item in clean_specialists)
    if any(
        sum(item[key] for key in ("happy", "content", "unhappy", "angry"))
        + specialist_population != city["size"]
        for item in clean_feelings
    ):
        raise PlayerError("invalid city investigation population")
    return {
        "id": _opaque(raw["id"], "observation ID"),
        "type": raw["type"], "source": raw["source"],
        "freshness": raw["freshness"], "state_revision": revision,
        "city": {
            "id": _opaque(city["id"], "investigated city ID"),
            "name": city["name"], "size": city["size"],
            "production": {
                "id": _opaque(production["id"], "production ID"),
                "kind": production["kind"], "name": production["name"],
            },
            "shields": dict(shields), "improvements": clean_improvements,
            "citizens": {
                "feelings": clean_feelings,
                "specialists": clean_specialists,
            },
        },
    }


def _validate_receipt(
    value: Any, session: dict[str, Any], *, batch_id: str | None = None,
) -> dict[str, Any]:
    raw = _validate_v2_header(
        value, session,
        fields={
            "schema_version", "control_protocol", "game_id", "agent_id",
            "batch_id", "receipt_state", "idempotent", "state_revision",
            "error", "observation",
        },
        label="v2 receipt",
    )
    receipt_batch = _opaque(raw["batch_id"], "receipt batch ID")
    revision = _validate_revision(raw["state_revision"])
    if (
        batch_id is not None and receipt_batch != batch_id
        or raw["receipt_state"] not in V2_RECEIPT_STATES
        or not isinstance(raw["idempotent"], bool)
    ):
        raise PlayerError("invalid v2 receipt")
    terminal_error = raw["receipt_state"] in {"rejected", "ambiguous"}
    if terminal_error:
        error = _validate_error(raw["error"])
        if error["state_revision"] != revision:
            raise PlayerError("invalid v2 receipt error revision")
        if raw["receipt_state"] == "ambiguous" and (
            error["error"]["code"] != "action_outcome_ambiguous"
            or error["error"]["retryable"]
        ):
            raise PlayerError("invalid ambiguous receipt")
        if (
            raw["receipt_state"] == "rejected"
            and error["error"]["code"] == "action_outcome_ambiguous"
        ):
            raise PlayerError("invalid rejected receipt")
    elif raw["error"] is not None:
        raise PlayerError("invalid v2 receipt error")
    else:
        error = None
    observation = raw["observation"]
    if observation is not None:
        if raw["receipt_state"] != "applied":
            raise PlayerError("invalid v2 receipt observation")
        observation = _validate_investigation_observation(
            observation, revision,
        )
    return {
        "schema_version": 2,
        "control_protocol": FULL_CONTROL_V2,
        "game_id": session["game_id"],
        "agent_id": session["agent_id"],
        "batch_id": receipt_batch,
        "receipt_state": raw["receipt_state"],
        "idempotent": raw["idempotent"],
        "state_revision": revision,
        "error": error,
        "observation": observation,
    }


def _safe_number(value: Any, label: str, *, nullable: bool = False) -> Any:
    if value is None and nullable:
        return None
    if (
        isinstance(value, bool) or not isinstance(value, (int, float))
        or not float("-inf") < float(value) < float("inf") or value < 0
    ):
        raise PlayerError(f"invalid {label}")
    return value


def _validate_phase_end_event(
    value: Any, session: dict[str, Any], seat: dict[str, Any],
) -> dict[str, Any]:
    raw = _exact(
        value,
        {
            "sequence", "turn", "phase", "place", "seat_id",
            "player_name", "player_color", "controller_label",
            "controller_type", "source", "receipt_state", "resolution",
            "deadline_started_at", "ended_at", "elapsed_s",
        },
        "health last phase end",
    )
    if (
        any(
            type(raw[name]) is not int or raw[name] < minimum
            for name, minimum in (
                ("sequence", 1), ("turn", 0), ("phase", 0), ("place", 1),
            )
        )
        or raw["place"] != seat["place"]
        or raw["seat_id"] != seat["seat_id"]
        or raw["player_name"] != seat["player_name"]
        or not isinstance(raw["player_color"], str)
        or PLAYER_COLOR_RE.fullmatch(raw["player_color"]) is None
        or raw["controller_label"] != session["controller_label"]
        or raw["controller_type"] != "external"
        or raw["source"] not in {"agent", "timeout"}
        or raw["receipt_state"] not in V2_TERMINAL_RECEIPTS
        or raw["resolution"] not in {"advanced", "terminal", "failed"}
        or raw["receipt_state"] == "rejected"
        and raw["resolution"] != "failed"
    ):
        raise PlayerError("invalid v2 health last phase end")
    for name in ("deadline_started_at", "ended_at", "elapsed_s"):
        _safe_number(raw[name], f"health last phase end {name}")
    if raw["ended_at"] < raw["deadline_started_at"]:
        raise PlayerError("invalid v2 health last phase end timing")
    return dict(raw)


def _validate_health(value: Any, session: dict[str, Any]) -> dict[str, Any]:
    base_fields = {
        "schema_version", "control_protocol", "game_id", "agent",
        "game_state", "seat", "sidecar", "observation_available",
        "legal_actions_available", "phase", "last_phase_end",
    }
    present = (
        set(value).intersection(V2_EVALUATION_FIELDS)
        if isinstance(value, dict) else set()
    )
    session_context = _validate_evaluation_context(
        session, "private v2 session",
    )
    if present and present != V2_EVALUATION_FIELDS:
        raise PlayerError("invalid v2 health: evaluation context is incomplete")
    if session_context is not None and not present:
        raise PlayerError("invalid v2 health: evaluation context is missing")
    raw = _validate_v2_header(
        value, session,
        fields=base_fields | (V2_EVALUATION_FIELDS if present else set()),
        label="v2 health",
    )
    evaluation = _validate_evaluation_context(
        raw, "v2 health", expected=session_context,
        required=session_context is not None,
    )
    agent = _exact(raw["agent"], {"agent_id", "controller_label"}, "health agent")
    if (
        agent["agent_id"] != session["agent_id"]
        or not isinstance(agent["controller_label"], str)
        or not agent["controller_label"]
        or agent["controller_label"] != session["controller_label"]
    ):
        raise PlayerError("invalid v2 health agent identity")
    seat = _exact(
        raw["seat"], {"place", "seat_id", "player_name"}, "health seat",
    )
    for name in ("place", "seat_id", "player_name"):
        expected = session.get(name)
        if expected is not None and seat[name] != expected:
            raise PlayerError(f"invalid v2 health seat {name}")
    if (
        isinstance(seat["place"], bool) or not isinstance(seat["place"], int)
        or seat["place"] < 1
        or not isinstance(seat["seat_id"], str) or not seat["seat_id"]
        or not isinstance(seat["player_name"], str) or not seat["player_name"]
        or raw["game_state"] not in {
            "lobby", "starting", "running", *TERMINAL_STATES,
        }
        or not isinstance(raw["sidecar"], dict)
        or not set(raw["sidecar"]).issubset(V2_SIDECAR_FIELDS)
        or "state" not in raw["sidecar"]
        or "generation" not in raw["sidecar"]
        or any(
            item is not None and not isinstance(item, (str, int, float, bool))
            for item in raw["sidecar"].values()
        )
        or not isinstance(raw["observation_available"], bool)
        or not isinstance(raw["legal_actions_available"], bool)
    ):
        raise PlayerError("invalid v2 health response")
    phase = raw["phase"]
    if phase is None:
        if raw["game_state"] in TERMINAL_STATES:
            clean_phase = None
        else:
            # A clean native server exit can be observed just before the
            # supervisor classifies its terminal result. No stale actionable
            # phase may survive that window.
            clean_phase = None
    elif raw["game_state"] in TERMINAL_STATES:
        raise PlayerError("terminal v2 health retained stale phase state")
    else:
        phase = _exact(
            phase,
            {"state", "turn", "phase", "active", "timing"},
            "health phase",
        )
        if (
            phase["state"] not in V2_PHASE_STATES
            or phase["turn"] is not None and (
                isinstance(phase["turn"], bool) or not isinstance(phase["turn"], int)
                or phase["turn"] < 0
            )
            or phase["phase"] is not None and (
                isinstance(phase["phase"], bool) or not isinstance(phase["phase"], int)
                or phase["phase"] < 0
            )
            or not isinstance(phase["active"], bool)
        ):
            raise PlayerError("invalid v2 health phase")
        timing = _exact(
            phase["timing"],
            {
                "mode", "timeout_s", "deadline_started_at", "deadline_at",
                "elapsed_s", "remaining_s",
            },
            "health phase timing",
        )
        if timing["mode"] not in {"default", "blitz", "infinite", "custom"}:
            raise PlayerError("invalid v2 health timing mode")
        for name in (
            "timeout_s", "deadline_started_at", "deadline_at", "elapsed_s",
            "remaining_s",
        ):
            _safe_number(timing[name], f"health timing {name}", nullable=True)
        clean_phase = {
            "state": phase["state"], "turn": phase["turn"],
            "phase": phase["phase"], "active": phase["active"],
            "timing": dict(timing),
        }
    if (
        evaluation is not None
        and clean_phase is not None
        and clean_phase["turn"] is not None
        and evaluation["turns_remaining"]
        != max(0, evaluation["max_turns"] - clean_phase["turn"])
    ):
        raise PlayerError("invalid v2 health: turns_remaining is inconsistent")
    last_phase_end = (
        None
        if raw["last_phase_end"] is None
        else _validate_phase_end_event(raw["last_phase_end"], session, seat)
    )
    result = {
        "schema_version": 2,
        "control_protocol": FULL_CONTROL_V2,
        "game_id": session["game_id"],
        "agent": dict(agent),
        "game_state": raw["game_state"],
        "seat": dict(seat),
        "sidecar": _json_value(raw["sidecar"], "sidecar health"),
        "observation_available": raw["observation_available"],
        "legal_actions_available": raw["legal_actions_available"],
        "phase": clean_phase,
        "last_phase_end": last_phase_end,
    }
    if evaluation is not None:
        result.update(evaluation)
    return result


def _validate_wait_response(
    value: Any,
    session: dict[str, Any],
    *,
    until: str,
    after_state_token: str | None,
) -> dict[str, Any]:
    raw = _validate_v2_header(
        value,
        session,
        fields={
            "schema_version", "control_protocol", "game_id", "agent_id",
            "wake_reason", "health", "state_revision",
        },
        label="v2 wait response",
    )
    if raw["agent_id"] != session["agent_id"]:
        raise PlayerError("invalid v2 wait response agent")
    wake_reason = raw["wake_reason"]
    if wake_reason not in V2_WAKE_REASONS:
        raise PlayerError("invalid v2 wait wake reason")
    health = _validate_health(raw["health"], session)
    revision = (
        None
        if raw["state_revision"] is None
        else _validate_revision(raw["state_revision"])
    )
    phase = health["phase"]
    if (
        until == "phase" and revision is not None
        or wake_reason == "phase_active"
        and not (
            until == "phase"
            and isinstance(phase, dict)
            and phase["active"] is True
            and phase["state"] == "awaiting_agent"
            and health["observation_available"] is True
        )
        or wake_reason == "game_terminal"
        and health["game_state"] not in TERMINAL_STATES
        or wake_reason == "revision_changed"
        and not (
            until == "revision"
            and revision is not None
            and isinstance(after_state_token, str)
            and revision["state_token"] != after_state_token
        )
    ):
        raise PlayerError("invalid v2 wait wake contract")
    return {
        "schema_version": 2,
        "control_protocol": FULL_CONTROL_V2,
        "game_id": session["game_id"],
        "agent_id": session["agent_id"],
        "wake_reason": wake_reason,
        "health": health,
        "state_revision": revision,
    }


def _v2_url(session: dict[str, Any], suffix: str) -> str:
    if not suffix.startswith("/") or "?" in suffix or "#" in suffix:
        raise PlayerError("invalid local v2 route")
    return (
        f"{session['service_url']}/v2/games/{session['game_id']}/me{suffix}"
    )


def _v2_response(
    method: str, url: str, session: dict[str, Any], *,
    body: dict[str, Any] | None = None,
    encoded_body: bytes | None = None,
    timeout: float = 60,
) -> JSONResponse:
    response = request_json_response(
        method, url, token=session["agent_token"], body=body,
        encoded_body=encoded_body, timeout=timeout,
    )
    return response


def _raise_validated_v2_error(response: JSONResponse) -> None:
    payload = _validate_error(response.value)
    raise V2ResponseError(response.status, payload)


def _revision_order(value: dict[str, Any]) -> tuple[int, int]:
    return value["turn"], value["revision"]


def _cursor_expired(expires_at: str | None) -> bool:
    if expires_at is None:
        return False
    parsed = datetime.fromisoformat(expires_at[:-1] + "+00:00")
    return parsed.timestamp() <= time.time()


def _drop_pending_for_cursor(
    session_path: Path, state: dict[str, Any], cursor: str,
) -> None:
    session = {
        "game_id": state["game_id"], "agent_id": state["agent_id"],
    }
    with _v2_state_lock(session_path):
        current = _load_v2_client_state_unlocked(session_path, session)
        removed = False
        for catalog_id, pending in tuple(
            current["pending_catalogs"].items()
        ):
            if pending.get("next_cursor") == cursor:
                current["pending_catalogs"].pop(catalog_id, None)
                removed = True
        if removed:
            _save_v2_client_state_unlocked(session_path, current)
        state.clear()
        state.update(current)


def _drop_pending_for_scope(
    session_path: Path,
    state: dict[str, Any],
    actor_id: str,
    target_id: str,
) -> None:
    session = {
        "game_id": state["game_id"], "agent_id": state["agent_id"],
    }
    with _v2_state_lock(session_path):
        current = _load_v2_client_state_unlocked(session_path, session)
        removed = False
        for catalog_id, pending in tuple(
            current["pending_catalogs"].items()
        ):
            scope = pending.get("scope", {})
            if (
                scope.get("actor_id") == actor_id
                and (not target_id or scope.get("target_id") == target_id)
            ):
                current["pending_catalogs"].pop(catalog_id, None)
                removed = True
        if removed:
            _save_v2_client_state_unlocked(session_path, current)
        state.clear()
        state.update(current)


def _entity_alias_prefix(identifier: Any) -> str | None:
    """Return the alias prefix an opaque entity ID may be numbered under."""
    if not isinstance(identifier, str):
        return None
    for kind, prefix in ALIAS_ENTITY_PREFIXES.items():
        if not identifier.startswith(f"{kind}_"):
            continue
        pattern = RELATION_ID_RE if kind == "relation" else ACTOR_ID_RE
        return prefix if pattern.fullmatch(identifier) is not None else None
    return None


def _assign_action_aliases(
    state: dict[str, Any],
    revision: dict[str, Any],
    entries: list[tuple[str, str]],
) -> None:
    """Number newly enumerated actions a1..aN inside their own revision.

    The bucket carries the revision it was built from.  Resolution refuses
    every alias whose recorded revision is not the newest revision this client
    knows, so an alias can never outlive the capability it names.
    """
    table = state["action_aliases"]
    if table["state_revision"] != revision:
        table = _empty_action_aliases()
        table["state_revision"] = revision
        state["action_aliases"] = table
    by_alias = table["by_alias"]
    known = {entry["action_id"] for entry in by_alias.values()}
    number = max((int(alias[1:]) for alias in by_alias), default=0) + 1
    for action_id, actor_id in entries:
        if action_id in known:
            continue
        if len(by_alias) >= V2_MAX_ACTION_ALIASES or number > 9999:
            return
        by_alias[f"a{number}"] = {
            "action_id": action_id, "actor_id": actor_id,
        }
        known.add(action_id)
        number += 1


def _assign_entity_aliases(
    state: dict[str, Any], identifiers: list[Any],
) -> None:
    """Give each newly seen entity a game-stable u1/c1/p1/r1 name."""
    entities = state["entity_aliases"]
    known = set(entities.values())
    numbers = {prefix: 0 for prefix in ALIAS_ENTITY_TYPES}
    for alias in entities:
        prefix, digits = alias[0], alias[1:]
        numbers[prefix] = max(numbers[prefix], int(digits))
    for identifier in identifiers:
        prefix = _entity_alias_prefix(identifier)
        if prefix is None or identifier in known:
            continue
        if len(entities) >= V2_MAX_ENTITY_ALIASES or numbers[prefix] >= 9999:
            continue
        numbers[prefix] += 1
        entities[f"{prefix}{numbers[prefix]}"] = identifier
        known.add(identifier)


def _assign_tile_aliases(
    state: dict[str, Any], tiles: list[tuple[str, int, int]],
) -> None:
    """Cache tile IDs by coordinate so ``T(x,y)`` resolves without the wire."""
    cache = state["tile_aliases"]
    for identifier, x, y in tiles:
        if not -9999 <= x <= 9999 or not -9999 <= y <= 9999:
            continue
        key = f"{x},{y}"
        current = cache.get(key)
        if current == identifier:
            continue
        if current is not None:
            # Tile IDs are game-stable, so a changed ID is contract drift.
            # Drop the entry: an unknown alias fails closed, a wrong one does
            # not.
            cache.pop(key, None)
            continue
        if len(cache) >= V2_MAX_TILE_ALIASES or identifier in cache.values():
            continue
        cache[key] = identifier


def _tile_reference(value: Any, identifier_key: str) -> tuple[
    str, int, int,
] | None:
    """Read one ``(tile_id, x, y)`` triple out of an already-validated item."""
    if not isinstance(value, dict):
        return None
    identifier = value.get(identifier_key)
    x = value.get("x")
    y = value.get("y")
    if (
        not isinstance(identifier, str)
        or TILE_ID_RE.fullmatch(identifier) is None
        or isinstance(x, bool) or not isinstance(x, int)
        or isinstance(y, bool) or not isinstance(y, int)
    ):
        return None
    return identifier, x, y


def _learn_state_aliases(state: dict[str, Any], items: list[Any]) -> None:
    """Learn entity and tile aliases from one already-validated state page."""
    identifiers: list[Any] = []
    tiles: list[tuple[str, int, int]] = []
    for item in items:
        if not isinstance(item, dict):
            continue
        for key in ALIAS_ENTITY_KEYS:
            if key in item:
                identifiers.append(item[key])
        player = item.get("player")
        if isinstance(player, dict) and "id" in player:
            identifiers.append(player["id"])
        for reference in (
            _tile_reference(item, "id"), _tile_reference(item, "tile_id"),
        ):
            if reference is not None:
                tiles.append(reference)
    _assign_entity_aliases(state, identifiers)
    _assign_tile_aliases(state, tiles)


def _learn_descriptor_aliases(
    state: dict[str, Any], descriptors: list[Any], scope: Any = None,
) -> None:
    """Learn tile and entity aliases from validated action descriptors.

    An enumerated action names its own actor and target, so a catalog read on
    its own is enough to give the agent ``u3``/``c1`` to type back — exactly
    the entities this page already showed it.
    """
    tiles: list[tuple[str, int, int]] = []
    identifiers: list[Any] = []
    if isinstance(scope, dict):
        identifiers.append(scope.get("actor_id"))
        identifiers.append(scope.get("target_id"))
    for descriptor in descriptors:
        subject = descriptor["subject"]
        if not isinstance(subject, dict):
            continue
        for key in ("actor", "target"):
            value = subject.get(key)
            if isinstance(value, dict):
                identifiers.append(value.get("id"))
        reference = _tile_reference(subject.get("target"), "id")
        if reference is not None:
            tiles.append(reference)
    _assign_entity_aliases(state, identifiers)
    _assign_tile_aliases(state, tiles)


def _remember_drained_actor(state: dict[str, Any], actor_id: str) -> None:
    """Record that this actor's whole catalog is cached at this revision."""
    drained = state["drained_actors"]
    if (
        not isinstance(actor_id, str)
        or ACTOR_ID_RE.fullmatch(actor_id) is None
        or actor_id in drained
        or len(drained) >= V2_MAX_DRAINED_ACTORS
    ):
        return
    drained.append(actor_id)


def _remember_page_unlocked(
    session_path: Path,
    state: dict[str, Any],
    page: dict[str, Any],
    *,
    legal: bool,
) -> list[dict[str, Any]] | None:
    """Ingest one validated page; return a promoted catalog worth re-mirroring.

    A scoped catalog only earns its ``aN`` names when its final page promotes
    the whole accumulation, so the pages ingested before that must not be the
    last word the mirror hears about those rows.  On the promoting page this
    returns every accumulated descriptor, and the caller re-projects them.
    """
    revision = page["state_revision"]
    prior = state["last_revision"]
    if prior is None or _revision_order(revision) > _revision_order(prior):
        state["last_revision"] = revision
        state["actions"] = {}
        state["pending_catalogs"] = {}
        # A drained catalog names capabilities of exactly one revision, so the
        # record dies with the actions it describes.
        state["drained_actors"] = []
    elif _revision_order(revision) == _revision_order(prior):
        if revision != prior:
            raise PlayerError("state token changed without a newer revision")
    else:
        # An older authenticated page can be displayed but can never revive an
        # expired action capability in local state.
        return None
    if legal:
        public_page = page["page"]
        _learn_descriptor_aliases(
            state, public_page["items"], public_page.get("scope"),
        )
        if "scope" in public_page:
            catalog_id = public_page["catalog_id"]
            scope = public_page["scope"]
            complete = public_page["catalog_complete"]
            total = public_page["total_items"]
            expiry = public_page["cursor_expires_at"]
            for other_id, pending in tuple(state["pending_catalogs"].items()):
                if pending["scope"] == scope and other_id != catalog_id:
                    state["pending_catalogs"].pop(other_id, None)
            if not complete and _cursor_expired(expiry):
                state["pending_catalogs"].pop(catalog_id, None)
                _save_v2_client_state_unlocked(session_path, state)
                raise PlayerError(
                    "legal-action catalog cursor expired; restart the scoped query"
                )
            descriptors = {
                item["action_id"]: item for item in public_page["items"]
            }
            if len(descriptors) != len(public_page["items"]):
                state["pending_catalogs"].pop(catalog_id, None)
                _save_v2_client_state_unlocked(session_path, state)
                raise PlayerError("legal-action catalog repeated an action ID")
            # Aliases are assigned only once the catalog is promoted below: a
            # staged descriptor is not executable yet, and numbering it would
            # advertise an `aN` that `_persist_batch_for_action` must refuse.
            pending = state["pending_catalogs"].get(catalog_id)
            if pending is not None and (
                pending["state_revision"] != revision
                or pending["scope"] != scope
                or pending["total_items"] != total
            ):
                state["pending_catalogs"].pop(catalog_id, None)
                _save_v2_client_state_unlocked(session_path, state)
                raise PlayerError("legal-action catalog metadata changed")
            accumulated = dict(pending["items"] if pending else {})
            for action_id, descriptor in descriptors.items():
                existing = accumulated.get(action_id)
                if existing is not None and existing != descriptor:
                    state["pending_catalogs"].pop(catalog_id, None)
                    _save_v2_client_state_unlocked(session_path, state)
                    raise PlayerError(
                        "one catalog action ID described two different actions"
                    )
                accumulated[action_id] = descriptor
            if len(accumulated) > total:
                state["pending_catalogs"].pop(catalog_id, None)
                _save_v2_client_state_unlocked(session_path, state)
                raise PlayerError("legal-action catalog exceeded its total")
            if not complete:
                # An idempotently replayed older page cannot roll a catalog's
                # continuation cursor backward after later pages were staged.
                next_cursor = public_page["next_cursor"]
                next_expiry = expiry
                if (
                    pending is not None
                    and set(descriptors).issubset(pending["items"])
                    and len(accumulated) == len(pending["items"])
                ):
                    next_cursor = pending["next_cursor"]
                    next_expiry = pending["cursor_expires_at"]
                state["pending_catalogs"][catalog_id] = {
                    "state_revision": revision,
                    "scope": scope,
                    "total_items": total,
                    "items": accumulated,
                    "next_cursor": next_cursor,
                    "cursor_expires_at": next_expiry,
                }
                _save_v2_client_state_unlocked(session_path, state)
                return None
            if pending is None and all(
                state["actions"].get(action_id) == descriptor
                for action_id, descriptor in descriptors.items()
            ):
                # Idempotent replay of an already-promoted final page.
                _assign_action_aliases(state, revision, [
                    (action_id, scope["actor_id"]) for action_id in descriptors
                ])
                _save_v2_client_state_unlocked(session_path, state)
                return list(accumulated.values())
            if len(accumulated) != total:
                state["pending_catalogs"].pop(catalog_id, None)
                _save_v2_client_state_unlocked(session_path, state)
                raise PlayerError(
                    "legal-action catalog completed before every item arrived"
                )
            promoted = dict(state["actions"])
            for action_id, descriptor in accumulated.items():
                existing = promoted.get(action_id)
                if existing is not None and existing != descriptor:
                    state["pending_catalogs"].pop(catalog_id, None)
                    _save_v2_client_state_unlocked(session_path, state)
                    raise PlayerError(
                        "one action ID described two different actions"
                    )
                promoted[action_id] = descriptor
            state["actions"] = promoted
            _assign_action_aliases(state, revision, [
                (action_id, scope["actor_id"]) for action_id in accumulated
            ])
            state["pending_catalogs"].pop(catalog_id, None)
            # Only a complete, promoted, actor-wide catalog may later be
            # rendered as equivalent to another actor's: a target-scoped
            # catalog is a narrower question, and a partial one proves nothing.
            if "target_id" not in scope:
                _remember_drained_actor(state, scope["actor_id"])
            _save_v2_client_state_unlocked(session_path, state)
            return list(accumulated.values())
        for descriptor in public_page["items"]:
            action_id = descriptor["action_id"]
            existing = state["actions"].get(action_id)
            if existing is not None and existing != descriptor:
                raise PlayerError("one action ID described two different actions")
            state["actions"][action_id] = descriptor
        _assign_action_aliases(state, revision, [
            (descriptor["action_id"], "")
            for descriptor in public_page["items"]
        ])
    else:
        _learn_state_aliases(state, page["page"]["items"])
    _save_v2_client_state_unlocked(session_path, state)
    return None


def _remember_page(
    session_path: Path,
    state: dict[str, Any],
    page: dict[str, Any],
    *,
    legal: bool,
) -> list[dict[str, Any]] | None:
    session = {
        "game_id": state["game_id"], "agent_id": state["agent_id"],
    }
    with _v2_state_lock(session_path):
        current = _load_v2_client_state_unlocked(session_path, session)
        try:
            return _remember_page_unlocked(
                session_path, current, page, legal=legal,
            )
        finally:
            state.clear()
            state.update(current)


def _remember_receipt(
    session_path: Path, state: dict[str, Any], receipt: dict[str, Any],
) -> None:
    session = {
        "game_id": state["game_id"], "agent_id": state["agent_id"],
    }
    with _v2_state_lock(session_path):
        current = _load_v2_client_state_unlocked(session_path, session)
        prior = current["receipts"].get(receipt["batch_id"])
        if prior is not None:
            allowed = {
                "accepted": {"accepted", "applied", "rejected", "ambiguous"},
                "applied": {"applied"},
                "rejected": {"rejected"},
                "ambiguous": {"ambiguous"},
            }
            if receipt["receipt_state"] not in allowed.get(
                prior.get("receipt_state"), set(),
            ):
                raise PlayerError(
                    "a command receipt regressed or changed terminal state"
                )
        current["receipts"][receipt["batch_id"]] = receipt
        # A validated receipt proves the game moved on, so it retires every
        # outstanding capability exactly as a newer page would.  The alias
        # bucket is deliberately left in place: it still names the revision it
        # was built from, which is what lets `_expand_action_alias` refuse a
        # stale `aN` by name instead of resolving it to an expired handle.
        revision = receipt["state_revision"]
        prior = current["last_revision"]
        if prior is None or _revision_order(revision) > _revision_order(prior):
            current["last_revision"] = revision
            current["actions"] = {}
            current["pending_catalogs"] = {}
            current["drained_actors"] = []
        _save_v2_client_state_unlocked(session_path, current)
        state.clear()
        state.update(current)


# ---------------------------------------------------------------------------
# L1 local state mirror.
#
# Every response this client validates is projected into plain text files under
# the session directory so the agent can read state with zero network traffic
# and zero context cost until it chooses to look.  The mirror is a projection
# of pages the seat already received: it can never widen what the seat knows,
# and it never carries the private `.v2-state` cache, a bearer token, or a
# state token.  A mirror failure is reported on stderr and never fails the
# command that produced the payload — the authoritative result is the wire
# response, not its projection.
# ---------------------------------------------------------------------------


# The one protocol card.  Join prints it, and `state/header.txt` carries the
# same text, so an agent that lost its join output can re-read the contract
# from a file instead of re-reading the docs.  Everything here names a command
# that exists; nothing here restates a contract an error already carries.
V2_PROTOCOL_CARD = (
    "ALIASES — type these anywhere an ID is accepted: a1..aN one enumerated "
    "action (dies with its revision), u1/c1/p1/r1 a unit, city, player, or "
    "relation (stable all game), T(x,y) a tile you have seen. They expand "
    "locally; the wire carries the server's opaque ID.",
    "ERRORS carry their own remedy: every refusal names the exact command "
    "that fixes it.",
    "just start --nation N --leader L --male|--female [--style S]",
    "just turn                                 one briefing, one revision",
    'just do "u1 VERB ARGS; c1 VERB ARGS"      1..8 orders, one receipt each',
    "just turn --end --await                   end the phase, block, next header",
    "just show [ALIAS|--grep PATTERN]          read local files, zero network",
    "just state --section SECTION              one bounded state page",
    "just legal --actor_id ID --all            one actor's whole menu",
    "just legal --kind KIND --all              one class of action",
    "just batch --action_id ID --arguments JSON",
    "just receipt --batch_id ID | just retry --batch_id ID | just wait",
    "add --json to any of these for the full wire payload; --session only "
    "when this workspace holds a second joined seat",
)


def _mirror_path(session_path: Path) -> Path:
    return state_mirror.mirror_dir(session_path)


def _mirror(action: Any, *arguments: Any, **options: Any) -> None:
    try:
        action(*arguments, **options)
    except Exception as exc:  # noqa: BLE001 - a projection never fails a command
        print(
            f"warning: the local state mirror was not updated: {exc}",
            file=sys.stderr,
        )


def _mirror_page(
    session_path: Path,
    state: dict[str, Any],
    page: dict[str, Any],
    command: str,
) -> None:
    """Project one validated page into the session's mirror files."""
    _mirror(
        state_mirror.update_from_page,
        _mirror_path(session_path), command, page,
        aliases=_alias_map(state),
    )


def _promoted_catalog_page(
    page: dict[str, Any], promoted: list[dict[str, Any]] | None,
) -> dict[str, Any]:
    """Return the page the mirror should project for one legal-actions page.

    While a scoped catalog is still being drained its descriptors carry no
    ``aN`` name, because numbering a staged capability would advertise a handle
    `_persist_batch_for_action` must refuse.  The pages mirrored during the
    drain therefore render `-` in the alias column.  When the final page
    promotes the accumulation the whole catalog is handed to the mirror in one
    piece, so every one of those rows is rewritten with the name it just
    earned.  The returned object is still the validated page — only the item
    list widens, and only to descriptors this same response ingested.
    """
    if promoted is None:
        return page
    projected = dict(page)
    inner = dict(page["page"])
    inner["items"] = promoted
    projected["page"] = inner
    return projected


def _mirror_receipt(
    session_path: Path, receipt: dict[str, Any], command: str = "batch",
) -> None:
    _mirror(
        state_mirror.update_from_receipt,
        _mirror_path(session_path), command, receipt,
    )


def _mirror_health(
    session_path: Path,
    health: dict[str, Any],
    command: str,
    revision: dict[str, Any] | None = None,
) -> None:
    _mirror(
        state_mirror.update_from_health,
        _mirror_path(session_path), command, health,
        revision=revision, commands=V2_PROTOCOL_CARD,
    )


def _print_v2_json(value: dict[str, Any]) -> None:
    print(json.dumps(value, sort_keys=True, separators=(",", ":")))


# Commands whose *success* output is JSON and which therefore declare no
# `--json` flag.  Their refusals must stay JSON too: a machine consumer polling
# `just wait` in a loop has no flag with which to turn prose back off, so
# rendering its error compactly would be a JSON escape hatch with a hole in it.
V2_JSON_ONLY_COMMANDS = frozenset({"act", "next", "result", "wait"})

# The same escape hatch for a machine consumer that owns the environment but
# not the argument vector -- an e2e harness whose shared `subprocess.run`
# helper is built once for every subcommand, for instance.  `PLAY_JSON=1` is
# exactly `--json` on every command that declares it; it can never widen what
# a command does, only which of its two renderings is printed.
V2_JSON_ENVIRONMENT = "PLAY_JSON"
V2_TRUE_STRINGS = frozenset({"1", "on", "true", "yes"})


def _json_environment() -> bool:
    value = os.environ.get(V2_JSON_ENVIRONMENT, "").strip().casefold()
    return value in V2_TRUE_STRINGS


def _json_requested(args: argparse.Namespace) -> bool:
    """Report whether this invocation's output must be full-fidelity JSON."""
    if getattr(args, "command", None) in V2_JSON_ONLY_COMMANDS:
        return True
    return bool(getattr(args, "json_output", False)) or _json_environment()


# ---------------------------------------------------------------------------
# Client-side alias dialect.
#
# Aliases are a purely local addressing convenience.  Every one of them is
# resolved against the already-persisted ``.v2-state`` cache *before* a request
# is built, so the wire only ever carries the server-issued opaque ID.  Action
# aliases are revision-scoped and fail closed the moment a newer revision is
# known, exactly like the stale HMAC they name.
# ---------------------------------------------------------------------------


def _fresh_action_aliases(state: dict[str, Any]) -> dict[str, Any]:
    """Return the action alias bucket while it names the newest revision."""
    table = state["action_aliases"]
    if table["state_revision"] is None or (
        table["state_revision"] != state["last_revision"]
    ):
        return {}
    return table["by_alias"]


def _alias_map(state: dict[str, Any]) -> dict[str, str]:
    """Map every opaque ID this seat can address to its short alias."""
    aliases = {
        identifier: alias
        for alias, identifier in state["entity_aliases"].items()
    }
    for key, identifier in state["tile_aliases"].items():
        aliases[identifier] = f"T({key})"
    for alias, entry in _fresh_action_aliases(state).items():
        aliases[entry["action_id"]] = alias
    return aliases


def _closest_aliases(known: list[str], wanted: str) -> str:
    """Name the nearest known aliases so a typo has an obvious repair."""
    if not known:
        return "none are known yet"

    def number(alias: str) -> int:
        return int(re.sub(r"^[A-Za-z]+", "", alias) or 0)

    index = number(wanted)
    nearest = sorted(
        known, key=lambda alias: (abs(number(alias) - index), alias),
    )
    shown = " ".join(sorted(nearest[:8], key=number))
    return shown + (" …" if len(nearest) > 8 else "")


def _alias_refresh_command(path: Path, actor_id: str) -> str:
    """Name the exact command that re-enumerates aliases.

    The session path is printed only when this workspace cannot resolve the
    seat by itself, so the remedy always runs exactly as it reads.
    """
    command = "just legal"
    try:
        ambiguous = _session_path("").resolve() != path.resolve()
    except PlayerError:
        ambiguous = True
    if ambiguous:
        command += f" --session {path}"
    if actor_id:
        command += f" --actor_id {actor_id} --all"
    return command


def _expand_action_alias(
    state: dict[str, Any], alias: str, path: Path,
) -> str:
    table = state["action_aliases"]
    entry = table["by_alias"].get(alias)
    current = state["last_revision"]
    stale = table["state_revision"] is not None and (
        table["state_revision"] != current
    )
    # An alias that was never assigned is a typo, whatever the bucket's age:
    # telling an agent that `a99` "was enumerated at rev7" is a false
    # statement in the one message whose purpose is to teach.
    if entry is None:
        if not table["by_alias"]:
            raise PlayerError(
                f"unknown action alias {alias}: no legal-action catalog has "
                f"been read yet; run `{_alias_refresh_command(path, '')}`"
            )
        known = _closest_aliases(list(table["by_alias"]), alias)
        if stale:
            raise PlayerError(
                f"unknown action alias {alias}: it was never enumerated, and "
                f"the aliases that were ({known}) died with "
                f"{_revision_label(table['state_revision'])}; re-enumerate "
                f"with `{_alias_refresh_command(path, '')}`"
            )
        raise PlayerError(
            f"unknown action alias {alias}; this revision enumerated {known}"
        )
    if stale:
        actor_id = entry["actor_id"]
        scope = {
            identifier: name
            for name, identifier in state["entity_aliases"].items()
        }.get(actor_id, actor_id)
        remedy = _alias_refresh_command(path, scope)
        raise PlayerError(
            f"action alias {alias} was enumerated at "
            f"{_revision_label(table['state_revision'])} but this seat now "
            f"knows "
            f"{'no revision' if current is None else _revision_label(current)}"
            "; action aliases die with their revision. Re-enumerate with "
            f"`{remedy}` and use the new a1..aN"
        )
    return entry["action_id"]


def _expand_entity_alias(state: dict[str, Any], alias: str) -> str:
    identifier = state["entity_aliases"].get(alias)
    if identifier is not None:
        return identifier
    prefix = alias[0]
    kind = ALIAS_ENTITY_TYPES[prefix]
    known = [
        candidate for candidate in state["entity_aliases"]
        if candidate[0] == prefix
    ]
    raise PlayerError(
        f"unknown {kind} alias {alias}; known {kind} aliases: "
        f"{_closest_aliases(known, alias)}"
    )


def _expand_tile_alias(state: dict[str, Any], x: int, y: int) -> str:
    identifier = state["tile_aliases"].get(f"{x},{y}")
    if identifier is not None:
        return identifier
    known = sorted(
        state["tile_aliases"],
        key=lambda key: (
            abs(int(key.split(",")[0]) - x) + abs(int(key.split(",")[1]) - y),
            key,
        ),
    )[:6]
    nearest = " ".join(f"T({key})" for key in known) or "none are cached yet"
    raise PlayerError(
        f"unknown tile T({x},{y}): no page this seat has read named that "
        f"coordinate. Nearest cached tiles: {nearest}"
    )


def _looks_like_alias(text: str) -> bool:
    return any(
        pattern.fullmatch(text) is not None
        for pattern in (ACTION_ALIAS_RE, ENTITY_ALIAS_RE, TILE_ALIAS_RE)
    )


def _expand_alias(state: dict[str, Any], text: str, path: Path) -> str:
    """Expand one alias to its opaque ID; pass any other text through."""
    if ACTION_ALIAS_RE.fullmatch(text) is not None:
        return _expand_action_alias(state, text, path)
    if ENTITY_ALIAS_RE.fullmatch(text) is not None:
        return _expand_entity_alias(state, text)
    tile = TILE_ALIAS_RE.fullmatch(text)
    if tile is not None:
        return _expand_tile_alias(
            state, int(tile.group(1)), int(tile.group(2)),
        )
    return text


def _resolve_alias_arguments(
    path: Path,
    session: dict[str, Any],
    args: argparse.Namespace,
    fields: tuple[str, ...],
) -> argparse.Namespace:
    """Expand every alias-shaped ID argument before a request is built.

    The arguments are rewritten in place, so no code downstream of this call
    can see an alias.  The cache is only opened when an alias is actually
    present, so a command that already names opaque IDs behaves exactly as it
    did before.
    """
    raw = {
        name: (getattr(args, name, "") or "").strip() for name in fields
    }
    if not any(_looks_like_alias(text) for text in raw.values()):
        return args
    state = _load_v2_client_state(path, session)
    for name, text in raw.items():
        if _looks_like_alias(text):
            setattr(args, name, _expand_alias(state, text, path))
    return args


# ---------------------------------------------------------------------------
# L2 agent surface: compact rendering.
#
# Every renderer below consumes the objects the validators above already proved
# closed (``_validate_page``, ``_validate_receipt``, ``_validate_health``, and
# the compact projection ``_compact_legal_action``).  A payload that cannot be
# read against the documented contract raises ``PlayerError`` naming the
# ``--json`` escape hatch; nothing here prints a blank in place of a value it
# failed to read.  The wire, the audit log, and ``--json`` stay byte-identical.
# ---------------------------------------------------------------------------


def _render(lines: list[str]) -> None:
    for line in lines:
        print(line)


def _drift(label: str) -> PlayerError:
    return PlayerError(
        f"cannot render {label}: the validated payload does not match the "
        "documented contract; re-run the same command with --json"
    )


def _need(item: Any, key: str, label: str) -> Any:
    if not isinstance(item, dict) or key not in item:
        raise _drift(f"{label} {key}")
    return item[key]


def _need_int(item: Any, key: str, label: str) -> int:
    value = _need(item, key, label)
    if isinstance(value, bool) or not isinstance(value, int):
        raise _drift(f"{label} {key}")
    return value


def _need_text(item: Any, key: str, label: str) -> str:
    value = _need(item, key, label)
    if not isinstance(value, str) or not value:
        raise _drift(f"{label} {key}")
    return value


def _scalar(value: Any) -> str:
    """Render one validated JSON value without ever producing an empty cell."""
    if value is None:
        return "-"
    if isinstance(value, bool):
        return "yes" if value else "no"
    if isinstance(value, float):
        return f"{value:g}"
    if isinstance(value, (int, str)):
        return str(value)
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def _json_literal(value: Any) -> str:
    """Render one value exactly as it must appear on the wire."""
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def _plain_name(value: Any) -> str | None:
    """Return the display name a payload object carries, or None."""
    if not isinstance(value, dict):
        return None
    name = value.get("name")
    if isinstance(name, str) and name:
        return name
    identifier = value.get("id")
    if isinstance(identifier, str) and identifier:
        return identifier
    return None


def _flat(value: Any) -> str:
    """Render one nested value on a single line, never as raw JSON."""
    if isinstance(value, list):
        return "|".join(_flat(item) for item in value) or "-"
    if isinstance(value, dict):
        return _plain_name(value) or (
            _scalar(value.get("type")) if "type" in value else "…"
        )
    return _scalar(value)


def _named(value: Any, aliases: dict[str, str] | None = None) -> str:
    """Name one payload object as the agent can type it back.

    An object is resolved through the alias cache first, then by its own
    name or ID.  An object carrying neither renders as a compact typed
    digest -- never as `json.dumps` of the whole payload, which would put more
    bytes in the row than the field it replaced.
    """
    if isinstance(value, dict):
        identifier = value.get("id")
        if isinstance(identifier, str) and identifier and aliases:
            alias = aliases.get(identifier)
            if alias:
                return alias
        plain = _plain_name(value)
        if plain is not None:
            return plain
        kind = value.get("type")
        if isinstance(kind, str) and kind:
            details = [
                f"{key}={_flat(item)}"
                for key, item in sorted(value.items())
                if key != "type" and item is not None
            ]
            return kind + (":" + ",".join(details) if details else "")
        return _flat(value)
    return _scalar(value)


def _coordinates(value: Any) -> str | None:
    """Return ``@x,y`` when a validated payload carries integer coordinates."""
    if not isinstance(value, dict) or "x" not in value or "y" not in value:
        return None
    x = value["x"]
    y = value["y"]
    if (
        isinstance(x, bool) or isinstance(y, bool)
        or not isinstance(x, int) or not isinstance(y, int)
    ):
        raise _drift("tile coordinates")
    return f"@{x},{y}"


def _table(rows: list[list[str]]) -> list[str]:
    widths: list[int] = []
    for row in rows:
        for index, cell in enumerate(row):
            if index >= len(widths):
                widths.append(len(cell))
            else:
                widths[index] = max(widths[index], len(cell))
    lines: list[str] = []
    for row in rows:
        cells = [
            cell if index == len(row) - 1 else cell.ljust(widths[index])
            for index, cell in enumerate(row)
        ]
        lines.append("  ".join(cells).rstrip())
    return lines


def _revision_label(revision: dict[str, Any]) -> str:
    return f"rev{revision['revision']}/t{revision['turn']}"


def _page_status(page: dict[str, Any]) -> str:
    shown = len(page["items"])
    total = page["total_items"]
    if page["next_cursor"] is None:
        return f"{shown}/{total} complete"
    return f"{shown}/{total} more --cursor {page['next_cursor']}"


def _requested_scope(
    actor_id: str, target_id: str,
) -> dict[str, Any] | None:
    """Describe the scope this command asked for, from its own arguments."""
    if not actor_id:
        return None
    scope: dict[str, Any] = {
        "actor_id": actor_id, "actor_type": actor_id.split("_", 1)[0],
    }
    if target_id:
        scope["target_id"] = target_id
        scope["target_type"] = target_id.split("_", 1)[0]
    return scope


def _scope_text(
    scope: dict[str, Any] | None, aliases: dict[str, str] | None = None,
) -> str:
    if not scope:
        return "scope=all"
    named = aliases or {}
    text = (
        f"scope={scope['actor_type']} "
        f"{named.get(scope['actor_id'], scope['actor_id'])}"
    )
    if "target_id" in scope:
        text += (
            f" target={scope['target_type']} "
            f"{named.get(scope['target_id'], scope['target_id'])}"
        )
    return text


_LEGAL_SUBJECT_RESERVED = {
    "operation", "actor", "target", "probability", "legality", "consuming",
    "variant", "gold_cost",
}


def _probability_text(probability: Any) -> str:
    if not isinstance(probability, dict):
        # A present-but-unshaped probability is still a non-default, and a
        # non-default must always render visibly.  The JSON literal is used so
        # `null` cannot be mistaken for an empty cell.
        return "prob=" + _json_literal(probability)
    kind = probability.get("kind")
    low = probability.get("minimum_percent")
    high = probability.get("maximum_percent")
    if (
        not isinstance(kind, str) or not kind
        or isinstance(low, bool) or not isinstance(low, (int, float))
        or isinstance(high, bool) or not isinstance(high, (int, float))
    ):
        return "prob=" + _scalar(probability)
    if low == high:
        return f"prob={low:g}%/{kind}"
    return f"prob={low:g}-{high:g}%/{kind}"


def _schema_summary(schema: Any) -> str:
    """Summarize an argument schema as ``{name:type,…}``.

    An empty schema renders away entirely instead of shipping ``{}`` per item.
    """
    if not isinstance(schema, dict):
        raise _drift("action arguments schema")
    properties = schema.get("properties")
    if not properties:
        return ""
    if not isinstance(properties, dict):
        raise _drift("action arguments schema")
    required = schema.get("required")
    required_names = set(required) if isinstance(required, list) else set()
    parts: list[str] = []
    for name, specification in properties.items():
        kind = "?"
        if isinstance(specification, dict):
            choices = specification.get("enum")
            declared = specification.get("type")
            if isinstance(choices, list) and choices:
                # An enum member is printed as the JSON literal the wire
                # needs, never as a human word: `{ready:yes}` would teach the
                # agent to send the string "yes" where the schema wants true.
                shown = [_json_literal(choice) for choice in choices[:4]]
                if len(choices) > 4:
                    shown.append("…")
                kind = "|".join(shown)
            elif isinstance(declared, str) and declared:
                kind = declared
            elif isinstance(declared, list) and declared:
                kind = "|".join(_scalar(item) for item in declared)
        suffix = "" if name in required_names else "?"
        parts.append(f"{name}{suffix}:{kind}")
    return "{" + ",".join(parts) + "}"


def _row_alias(
    aliases: dict[str, str] | None,
    item: Any,
    key: str,
    prefix: str,
    index: int,
) -> str:
    """Name a row by its durable alias.

    Without a cache the row is display-only and is numbered positionally.
    With a cache in hand the row always shows something that resolves: the
    alias when one is assigned, otherwise the opaque ID itself.  A cached
    render whose item carries no usable ID is contract drift, not a licence to
    invent a positional `u1`/`c1` that already names a different entity.
    """
    if aliases is None:
        return f"{prefix}{index}"
    identifier = item.get(key) if isinstance(item, dict) else None
    if not isinstance(identifier, str) or not identifier:
        raise _drift("entity id")
    return aliases.get(identifier, identifier)


def _legal_row(
    alias: str,
    compact: dict[str, Any],
    scope: dict[str, Any] | None,
    aliases: dict[str, str] | None = None,
) -> list[str]:
    """Render one action: alias, kind, label, target, non-defaults, ID last."""
    subject = compact["subject"]
    if not isinstance(subject, dict):
        raise _drift("legal action subject")
    kind = compact["kind"]
    operation = subject.get("operation")
    if isinstance(operation, str) and operation and not kind.endswith(
        "." + operation,
    ):
        kind = f"{kind}/{operation}"
    detail: list[str] = []
    coordinates = _coordinates(compact["target"])
    tile = _tile_reference(compact["target"], "id")
    if tile is not None:
        # A tile target renders as exactly the alias `--target_id` accepts.
        detail.append(f"T({tile[1]},{tile[2]})")
    elif coordinates is not None:
        detail.append(coordinates)
    elif compact["target"] is not None:
        # A non-tile target is addressed the same way as every other entity:
        # through the alias cache first, so a player target prints `p1`.
        detail.append("→" + _named(compact["target"], aliases))
    actor = subject.get("actor")
    scope_actor = scope["actor_id"] if scope else None
    if isinstance(actor, dict):
        actor_id = actor.get("id")
        if isinstance(actor_id, str) and actor_id != scope_actor:
            detail.append(
                f"actor={(aliases or {}).get(actor_id, actor_id)}"
            )
    elif actor is not None:
        detail.append(f"actor={_scalar(actor)}")
    for key, value in subject.items():
        if key in _LEGAL_SUBJECT_RESERVED:
            continue
        detail.append(f"{key}={_named(value, aliases)}")
    # Omit-when-default only: a non-default probability, legality, consuming
    # flag, or variant is what turns a certain move into a gamble, so each one
    # renders with a leading `!`.
    if "probability" in compact:
        detail.append("!" + _probability_text(compact["probability"]))
    legality = subject.get("legality")
    if legality is not None and legality != "legal":
        detail.append(f"!legality={_scalar(legality)}")
    consuming = subject.get("consuming")
    if consuming is True:
        detail.append("!consuming")
    elif consuming not in (None, False):
        detail.append(f"!consuming={_scalar(consuming)}")
    variant = subject.get("variant")
    if variant is not None:
        detail.append(f"!variant={_scalar(variant)}")
    if "gold_cost" in compact:
        detail.append(f"gold={_scalar(compact['gold_cost'])}")
    if "gold_range" in compact:
        gold_range = compact["gold_range"]
        detail.append(
            "gold_range="
            + "-".join(
                _scalar(gold_range[key])
                for key in ("minimum", "maximum") if key in gold_range
            )
        )
    schema = _schema_summary(compact["argument_schema"])
    if schema:
        detail.append(schema)
    row = [alias, kind, compact["label"], " ".join(detail)]
    # The 39-char opaque handle is printed only when column 0 is a positional
    # number that resolves nowhere.  With a cache the alias (or the ID itself)
    # already addresses the row, and `--json` and the mirror still carry it.
    if aliases is None:
        row.append(compact["action_id"])
    return row


def _legal_rows(
    compacts: list[dict[str, Any]],
    scope: dict[str, Any] | None,
    aliases: dict[str, str] | None = None,
) -> list[str]:
    rows = [
        _legal_row(
            _row_alias(aliases, compact, "action_id", "a", index),
            compact, scope, aliases,
        )
        for index, compact in enumerate(compacts, start=1)
    ]
    return _table(rows)


def _action_kind_key(compact: dict[str, Any]) -> str:
    """Name one action's kind and operation, with no opaque handle in it."""
    subject = compact["subject"]
    if not isinstance(subject, dict):
        raise _drift("legal action subject")
    kind = compact["kind"]
    operation = subject.get("operation")
    if isinstance(operation, str) and operation and not kind.endswith(
        "." + operation,
    ):
        kind = f"{kind}/{operation}"
    return kind


def _action_target_key(compact: dict[str, Any]) -> str:
    """Name one action's target by coordinate or name, never by its hash."""
    target = compact["target"]
    tile = _tile_reference(target, "id")
    if tile is not None:
        return f"T({tile[1]},{tile[2]})"
    coordinates = _coordinates(target)
    if coordinates is not None:
        return coordinates
    if target is None:
        return ""
    return _named(target)


def _catalog_signature(
    compacts: list[dict[str, Any]],
    scope: dict[str, Any] | None,
    aliases: dict[str, str] | None,
) -> tuple[list[tuple[str, str]], list[tuple[str, ...]]]:
    """Sign one catalog twice: by choice offered, and by rendered row.

    The first signature is what "the same options" means — kind, operation and
    target by coordinate, with every revision-bound hash excluded.  The second
    is the row the agent would read, so any difference in probability,
    legality, cost or arguments is still visible as a differing row.
    """
    choices: list[tuple[str, str]] = []
    rows: list[tuple[str, ...]] = []
    for compact in compacts:
        choices.append((_action_kind_key(compact), _action_target_key(compact)))
        rows.append(tuple(_legal_row("", compact, scope, aliases)[1:4]))
    return choices, rows


def _cached_actor_catalog(
    state: dict[str, Any], actor_id: str,
) -> list[dict[str, Any]]:
    descriptors = []
    for descriptor in state["actions"].values():
        subject = descriptor["subject"]
        actor = subject.get("actor") if isinstance(subject, dict) else None
        if isinstance(actor, dict) and actor.get("id") == actor_id:
            descriptors.append(descriptor)
    return descriptors


def _catalog_equivalence(
    state: dict[str, Any],
    result: dict[str, Any],
    scope: dict[str, Any] | None,
    aliases: dict[str, str] | None,
) -> tuple[str, list[dict[str, Any]]] | None:
    """Find another actor whose catalog offers exactly the same choices.

    Only complete catalogs drained at the newest revision this client knows
    are compared, and the comparison never crosses a revision: two catalogs
    built from different revisions describe different capabilities even when
    they read identically.
    """
    if scope is None or "target_id" in scope or result["truncated"]:
        return None
    if result["offset"] or result["byte_limited"]:
        return None
    actor_id = scope["actor_id"]
    drained = state["drained_actors"]
    if (
        state["last_revision"] != result["state_revision"]
        or actor_id not in drained
    ):
        return None
    choices, rows = _catalog_signature(result["actions"], scope, aliases)
    for other_id in drained:
        if other_id == actor_id:
            continue
        descriptors = _cached_actor_catalog(state, other_id)
        if len(descriptors) != len(choices):
            continue
        other_scope = {
            "actor_id": other_id, "actor_type": other_id.split("_", 1)[0],
        }
        other_choices, other_rows = _catalog_signature(
            [_compact_legal_action(item) for item in descriptors],
            other_scope, aliases,
        )
        # Row order must match too: the short line claims "same options in the
        # same order", which is what makes the named aliases usable without
        # reprinting the rows they stand for.
        if other_choices != choices:
            continue
        differing = [
            compact
            for compact, row, other_row in zip(result["actions"], rows, other_rows)
            if row != other_row
        ]
        return other_id, differing
    return None


def _render_legal_page(
    value: dict[str, Any], aliases: dict[str, str] | None = None,
) -> list[str]:
    page = value["page"]
    scope = page.get("scope")
    lines = [
        f"{_revision_label(value['state_revision'])} legal "
        f"{_scope_text(scope, aliases)} {_page_status(page)}"
    ]
    if not page["items"]:
        lines.append("(no legal actions on this page)")
        return lines
    lines.extend(_legal_rows(
        [_compact_legal_action(item) for item in page["items"]], scope,
        aliases,
    ))
    return lines


def _render_legal_compact(
    result: dict[str, Any],
    scope: dict[str, Any] | None,
    aliases: dict[str, str] | None = None,
    equivalence: tuple[str, list[dict[str, Any]]] | None = None,
) -> list[str]:
    kind = result["kind"]
    header = (
        f"{_revision_label(result['state_revision'])} legal "
        + (f"kind={kind} " if kind else "")
        + f"{_scope_text(scope, aliases)} "
        f"{result['shown']}/{result['matched']} matched "
        f"(catalog {result['catalog_total']} complete, "
        f"pages {result['pages_read']})"
    )
    if result["has_more"]:
        # The continuation is printed as the command that runs, not as a bare
        # flag fragment: an error or a limit always names its own remedy.
        named = (aliases or {}).get(
            scope["actor_id"], scope["actor_id"],
        ) if scope else ""
        header += " more: just legal" + (
            f" --kind {kind}" if kind else ""
        ) + (
            f" --actor_id {named}" if named else ""
        ) + f" --all --offset {result['next_offset']}"
    if result["byte_limited"]:
        header += " byte_limited"
    if result["oversized_single"]:
        header += " oversized_single"
    lines = [header]
    if not result["actions"]:
        lines.append(
            f"(no {kind} actions in this catalog)" if kind
            else "(no legal actions in this catalog)"
        )
        return lines
    if equivalence is not None:
        lines.extend(_equivalence_lines(
            result, scope, aliases, equivalence,
        ))
        return lines
    lines.extend(_legal_rows(result["actions"], scope, aliases))
    return lines


def _alias_span(aliases: list[str]) -> str:
    """Name a run of action aliases as ``a3..a7``, listing anything ragged."""
    numbers = []
    for alias in aliases:
        match = ACTION_ALIAS_RE.fullmatch(alias)
        if match is None:
            return " ".join(aliases)
        numbers.append(int(alias[1:]))
    if len(numbers) > 1 and numbers == list(
        range(numbers[0], numbers[0] + len(numbers)),
    ):
        return f"{aliases[0]}..{aliases[-1]}"
    return " ".join(aliases)


def _equivalence_lines(
    result: dict[str, Any],
    scope: dict[str, Any] | None,
    aliases: dict[str, str] | None,
    equivalence: tuple[str, list[dict[str, Any]]],
) -> list[str]:
    """Say "this catalog is that one" instead of reprinting it.

    The claim is only ever made about two complete catalogs read at the same
    revision whose rows line up one for one, so naming this catalog's own
    action aliases is enough to act on any of them.  Every row whose
    decision-relevant detail differs is still printed in full underneath.
    """
    other_id, differing = equivalence
    named = aliases or {}
    actor_id = scope["actor_id"] if scope else ""
    line = (
        f"{named.get(actor_id, actor_id)} == {named.get(other_id, other_id)} "
        f"(rev{result['state_revision']['revision']})"
    )
    own = [
        named.get(compact["action_id"], compact["action_id"])
        for compact in result["actions"]
    ]
    if own:
        line += " " + _alias_span(own)
    if differing:
        line += (
            f" except {len(differing)} row"
            + ("s" if len(differing) != 1 else "")
        )
    lines = [line]
    if differing:
        lines.extend(_legal_rows(differing, scope, aliases))
    return lines


def _unit_row(
    alias: str, item: dict[str, Any], *, show_id: bool = True,
) -> list[str]:
    detail: list[str] = []
    coordinates = _coordinates(item)
    if coordinates is not None:
        detail.append(coordinates)
    if "moves" in item:
        rate = item.get("type_stats")
        rate_text = (
            _scalar(rate["move_rate"])
            if isinstance(rate, dict) and "move_rate" in rate else "?"
        )
        detail.append(f"mv{_scalar(item['moves'])}/{rate_text}")
    if "hp" in item:
        stats = item.get("type_stats")
        maximum = (
            _scalar(stats["max_hp"])
            if isinstance(stats, dict) and "max_hp" in stats else "?"
        )
        detail.append(f"hp{_scalar(item['hp'])}/{maximum}")
    activity = item.get("activity")
    if isinstance(activity, dict) and "name" in activity:
        detail.append(_scalar(activity["name"]))
    route = item.get("route")
    if isinstance(route, dict):
        destination = _coordinates(route.get("destination")) or "?"
        detail.append(
            f"{_scalar(route.get('mode'))}{destination}"
            f"/{_scalar(route.get('order_count'))}steps"
        )
    automation = item.get("automation")
    if isinstance(automation, dict) and automation.get("controller") not in (
        None, "player",
    ):
        detail.append(f"!controller={_scalar(automation['controller'])}")
    scope = item.get("scope")
    if isinstance(scope, str) and scope != "own":
        detail.append(f"scope={scope}")
    row = [alias, _need_text(item, "type", "unit"), " ".join(detail)]
    if show_id:
        row.append(_need_text(item, "id", "unit"))
    return row


def _render_units(
    items: list[dict[str, Any]], aliases: dict[str, str] | None = None,
) -> list[str]:
    # With a cache the alias column already addresses the unit, so the opaque
    # `unit_<32hex>` never enters agent context; `--json` still carries it.
    return _table([
        _unit_row(
            _row_alias(aliases, item, "id", "u", index), item,
            show_id=aliases is None,
        )
        for index, item in enumerate(items, start=1)
    ])


def _city_row(
    alias: str, item: dict[str, Any], *, show_id: bool = True,
) -> list[str]:
    detail: list[str] = []
    coordinates = _coordinates(item)
    if coordinates is not None:
        detail.append(coordinates)
    detail.append(f"sz{_need_int(item, 'size', 'city')}")
    production = item.get("production")
    if isinstance(production, dict):
        text = _named(production)
        if "shield_stock" in production and "shield_cost" in production:
            text += (
                f" {_scalar(production['shield_stock'])}"
                f"/{_scalar(production['shield_cost'])}"
            )
        detail.append(text)
    surplus = item.get("surplus")
    if isinstance(surplus, dict):
        detail.append(" ".join(
            f"{key[0]}{value:+d}" if isinstance(value, int)
            and not isinstance(value, bool) else f"{key[0]}{_scalar(value)}"
            for key, value in surplus.items()
        ))
    row = [alias, _need_text(item, "name", "city"), " ".join(detail)]
    if show_id:
        row.append(_need_text(item, "id", "city"))
    return row


def _render_cities(
    items: list[dict[str, Any]], aliases: dict[str, str] | None = None,
) -> list[str]:
    return _table([
        _city_row(
            _row_alias(aliases, item, "id", "c", index), item,
            show_id=aliases is None,
        )
        for index, item in enumerate(items, start=1)
    ])


def _render_research(
    items: list[dict[str, Any]], aliases: dict[str, str] | None = None,
) -> list[str]:
    """Render one research page; the technology's own name is its handle.

    A technology never receives an entity alias, so no positional column is
    printed: a bare `3` in column 0 would resolve nowhere, and the name is
    exactly what `research set_target NAME` accepts.
    """
    rows: list[list[str]] = []
    for item in items:
        detail: list[str] = []
        if item.get("path_cost") is not None:
            detail.append(f"path{_scalar(item['path_cost'])}")
        if item.get("can_target") is True:
            detail.append("targetable")
        if item.get("can_goal") is True:
            detail.append("goalable")
        unknown = item.get("unknown_prerequisite_count")
        if unknown and isinstance(unknown, int) and not isinstance(
            unknown, bool,
        ):
            detail.append(f"needs{unknown}")
        row = [
            _need_text(item, "name", "research"),
            _need_text(item, "state", "research"), " ".join(detail),
        ]
        if aliases is None:
            row.append(_need_text(item, "id", "research"))
        rows.append(row)
    return _table(rows)


# A terrain glyph must mean the same thing on every page: a code derived from
# whichever terrains happened to appear on one page would silently rename
# Desert when Deep Ocean joined it, and an agent reading two windows would
# route a land unit onto water.  `state_mirror._TERRAIN_CHARS` carries the
# same fixed table, widened here to two characters.
_TERRAIN_CODES = {
    "Arctic": "Ar",
    "Deep Ocean": "Do",
    "Desert": "De",
    "Forest": "Fo",
    "Glacier": "Gl",
    "Grassland": "Gr",
    "Hills": "Hi",
    "Inaccessible": "In",
    "Jungle": "Ju",
    "Lake": "La",
    "Mountains": "Mo",
    "Ocean": "Oc",
    "Plains": "Pl",
    "Swamp": "Sw",
    "Tundra": "Tu",
}


def _terrain_code(name: str) -> str:
    """Derive one terrain's code from its own name and nothing else."""
    known = _TERRAIN_CODES.get(name)
    if known is not None:
        return known
    letters = [character for character in name if character.isalnum()]
    head = letters[0].upper() if letters else "X"
    tail = letters[1].lower() if len(letters) > 1 else "0"
    return head + tail


def _terrain_codes(names: set[str]) -> dict[str, str]:
    """Assign each terrain a page-independent two-character code.

    The code is a pure function of the terrain name, so the same glyph always
    means the same terrain.  Only a genuine collision between two unlisted
    names is broken here, and it is broken deterministically over the colliding
    names alone.
    """
    codes: dict[str, str] = {}
    collisions: dict[str, list[str]] = {}
    for name in sorted(names):
        collisions.setdefault(_terrain_code(name), []).append(name)
    for code, colliding in collisions.items():
        if len(colliding) == 1:
            codes[colliding[0]] = code
            continue
        # Deterministic in the colliding names only: a name keeps its code
        # whenever the same set of names collides, on any page.
        for index, name in enumerate(sorted(colliding)):
            codes[name] = code if index == 0 else code[0] + "0123456789"[
                min(index - 1, 9)
            ]
    return codes


def _tile_cells(items: list[dict[str, Any]]) -> tuple[
    dict[tuple[int, int], tuple[str | None, str]], set[str],
]:
    cells: dict[tuple[int, int], tuple[str | None, str]] = {}
    terrains: set[str] = set()
    for item in items:
        x = _need_int(item, "x", "tile")
        y = _need_int(item, "y", "tile")
        visibility = _need_text(item, "visibility", "tile")
        terrain = item.get("terrain")
        if terrain is None:
            cells[(x, y)] = (None, visibility)
            continue
        if not isinstance(terrain, str) or not terrain:
            raise _drift("tile terrain")
        terrains.add(terrain)
        cells[(x, y)] = (terrain, visibility)
    return cells, terrains


def _render_tiles(
    items: list[dict[str, Any]], aliases: dict[str, str] | None = None,
) -> list[str]:
    """Render a small coordinate grid; fogged and unknown tiles render '?'."""
    cells, terrains = _tile_cells(items)
    xs = sorted({x for x, _ in cells})
    ys = sorted({y for _, y in cells})
    width = xs[-1] - xs[0] + 1
    height = ys[-1] - ys[0] + 1
    codes = _terrain_codes(terrains)
    lines: list[str] = []
    if width > 40 or width * height > 1024:
        lines.extend(_table([
            [
                f"{x},{y}",
                "?" if terrain is None else codes[terrain],
                visibility,
            ]
            for (x, y), (terrain, visibility) in sorted(cells.items())
        ]))
    else:
        grid = [["y\\x", *[str(x) for x in range(xs[0], xs[-1] + 1)]]]
        for y in range(ys[0], ys[-1] + 1):
            row = [str(y)]
            for x in range(xs[0], xs[-1] + 1):
                cell = cells.get((x, y))
                if cell is None:
                    row.append(".")
                elif cell[0] is None:
                    row.append("?")
                elif cell[1] == "visible":
                    row.append(codes[cell[0]])
                else:
                    row.append(codes[cell[0]].lower())
            grid.append(row)
        lines.extend(_table(grid))
    legend = [f"{code}={name}" for name, code in sorted(codes.items())]
    legend.extend((
        "?=unknown/fogged", ".=not on this page", "lowercase=remembered",
    ))
    lines.append("legend " + " ".join(legend))
    for item in items:
        notes: list[str] = []
        if item.get("owner_player_id") is not None:
            notes.append(f"owner={_scalar(item['owner_player_id'])}")
        placement = item.get("infrastructure_placement")
        if placement is not None:
            notes.append(f"building={_named(placement)}")
        if notes:
            lines.append(
                f"T({item['x']},{item['y']}) {' '.join(notes)} "
                f"{_need_text(item, 'id', 'tile')}"
            )
    return lines


def _economy_text(player: Any) -> str:
    if not isinstance(player, dict):
        raise _drift("overview player")
    # Only a real name leads the line: `_named`'s digest of an unnamed object
    # would repeat the very facts this line goes on to print.
    plain = _plain_name(player)
    parts = [] if plain is None else [plain]
    nation = player.get("nation")
    if isinstance(nation, str) and nation:
        parts.append(f"({nation})")
    if "government" in player:
        parts.append(_scalar(player["government"]))
    economy = player.get("economy")
    if isinstance(economy, dict):
        if "gold" in economy:
            parts.append(f"gold {_scalar(economy['gold'])}")
        rates = [
            f"{key[:3]}{_scalar(economy[key])}"
            for key in ("tax", "luxury", "science") if key in economy
        ]
        if rates:
            parts.append("/".join(rates))
    return " ".join(parts)


def _research_text(research: Any) -> str:
    if not isinstance(research, dict):
        raise _drift("overview research")
    target = research.get("target")
    parts = [
        "research " + (_scalar(target) if target else "NO TARGET"),
    ]
    if "bulbs_researched" in research and "cost" in research:
        parts.append(
            f"{_scalar(research['bulbs_researched'])}"
            f"/{_scalar(research['cost'])}"
        )
    if "output" in research:
        parts.append(f"+{_scalar(research['output'])}/turn")
    goal = research.get("goal")
    if goal:
        parts.append(f"goal {_scalar(goal)}")
    return " ".join(parts)


def _render_overview(
    items: list[dict[str, Any]], aliases: dict[str, str] | None = None,
) -> list[str]:
    lines: list[str] = []
    for item in items:
        head = [f"turn {_scalar(item['turn'])}"]
        if "phase" in item and "phase_count" in item:
            head.append(
                f"phase {_scalar(item['phase'])}"
                f"/{_scalar(item['phase_count'])}"
            )
        if "client_state" in item:
            head.append(_scalar(item["client_state"]))
        head.append(_economy_text(item.get("player")))
        lines.append(" | ".join(head))
        lines.append(_research_text(item.get("research")))
        game_map = item.get("map")
        if isinstance(game_map, dict):
            lines.append(
                f"map {_scalar(game_map.get('width'))}x"
                f"{_scalar(game_map.get('height'))} "
                f"{_scalar(game_map.get('topology'))}"
            )
        counts = item.get("counts")
        if isinstance(counts, dict):
            lines.append("counts " + " ".join(
                f"{key} {_scalar(value)}"
                for key, value in counts.items() if value
            ))
    return lines


_STATE_RENDERERS: dict[str, tuple[tuple[str, ...], Any]] = {
    "units": (("id", "type", "x", "y"), _render_units),
    "cities": (("id", "name", "x", "y", "size"), _render_cities),
    "research": (("id", "name", "state"), _render_research),
    "tile_window": (("id", "x", "y", "visibility"), _render_tiles),
    "known_tiles": (("id", "x", "y", "visibility"), _render_tiles),
    "map_tiles": (("id", "x", "y", "visibility"), _render_tiles),
    "overview": (("turn", "player", "research", "counts"), _render_overview),
}


def _flatten_item(item: dict[str, Any]) -> dict[str, str]:
    """Flatten one payload item one level into ``parent.child`` cells.

    A nested object rendered as `json.dumps` costs more than the JSON page it
    was supposed to compact, so one level is spread into its own columns and
    anything deeper is named rather than dumped.
    """
    flat: dict[str, str] = {}
    for key, value in item.items():
        if isinstance(value, dict) and value:
            for inner, nested in value.items():
                flat[f"{key}.{inner}"] = _flat(nested)
            continue
        flat[key] = _flat(value)
    return flat


def _render_generic_items(items: list[Any]) -> list[str]:
    if not all(isinstance(item, dict) for item in items):
        return [
            f"{index}  {_scalar(item)}"
            for index, item in enumerate(items, start=1)
        ]
    flattened = [_flatten_item(item) for item in items]
    columns: list[str] = []
    for item in flattened:
        for key in item:
            if key not in columns:
                columns.append(key)
    # A column identical on every row is a page constant, not per-row data:
    # an actor-scoped page repeats its own 37-char actor ID on every row.
    constants = {
        key: flattened[0][key]
        for key in columns
        if len(flattened) > 1
        and all(key in item for item in flattened)
        and all(item[key] == flattened[0][key] for item in flattened)
    }
    # A column that is empty, zero or false everywhere carries no decision.
    empty = {
        key for key in columns
        if key not in constants
        and all(item.get(key, "-") in {"-", "0", "no", ""} for item in flattened)
    }
    columns = [
        key for key in columns if key not in constants and key not in empty
    ]
    lines: list[str] = []
    if constants:
        lines.append("constants: " + " ".join(
            f"{key}={value}" for key, value in constants.items()
        ))
    if not columns:
        lines.append(f"{len(flattened)} item(s), all fields constant")
        return lines
    if len(columns) > 14:
        lines.extend(
            f"{index}  " + " ".join(
                f"{key}={item[key]}" for key in columns if key in item
            )
            for index, item in enumerate(flattened, start=1)
        )
        return lines
    rows = [["#", *columns]]
    for index, item in enumerate(flattened, start=1):
        rows.append([str(index), *[
            item[key] if key in item else "n/a" for key in columns
        ]])
    lines.extend(_table(rows))
    return lines


def _render_section_items(
    section: str, items: list[Any], aliases: dict[str, str] | None = None,
) -> list[str]:
    renderer = _STATE_RENDERERS.get(section)
    if renderer is not None and all(
        isinstance(item, dict) and all(key in item for key in renderer[0])
        for item in items
    ):
        return renderer[1](items, aliases)
    return _render_generic_items(items)


def _render_state_page(
    value: dict[str, Any], aliases: dict[str, str] | None = None,
) -> list[str]:
    page = value["page"]
    section = page["section"]
    lines = [
        f"{_revision_label(value['state_revision'])} {section} "
        f"{_page_status(page)}"
    ]
    if not page["items"]:
        lines.append(f"(no {section} items on this page)")
        return lines
    lines.extend(_render_section_items(section, page["items"], aliases))
    return lines


def _phase_text(phase: dict[str, Any] | None) -> str:
    if phase is None:
        return "phase none"
    text = (
        f"phase {_scalar(phase['state'])} t{_scalar(phase['turn'])}"
        f"/p{_scalar(phase['phase'])}"
        f" {'active' if phase['active'] else 'inactive'}"
    )
    timing = phase["timing"]
    if timing["remaining_s"] is not None:
        text += f" {_scalar(timing['remaining_s'])}s left"
    elif timing["mode"] == "infinite":
        text += " no deadline"
    return text


def _render_health(health: dict[str, Any]) -> list[str]:
    sidecar = health["sidecar"]
    lines = [
        f"health {health['game_state']} | {_phase_text(health['phase'])} | "
        f"obs {'yes' if health['observation_available'] else 'no'} "
        f"legal {'yes' if health['legal_actions_available'] else 'no'} | "
        f"sidecar {_scalar(sidecar.get('state'))} "
        f"gen {_scalar(sidecar.get('generation'))}"
    ]
    seat = health["seat"]
    identity = (
        f"seat {_scalar(seat['place'])} {seat['player_name']} "
        f"({health['agent']['controller_label']})"
    )
    if "objective" in health:
        identity += (
            f" | objective {health['objective']}"
            f" | turns {_scalar(health['turns_remaining'])}"
            f"/{_scalar(health['max_turns'])} remaining"
        )
    lines.append(identity)
    event = health["last_phase_end"]
    if event is not None:
        lines.append(
            f"last phase end t{_scalar(event['turn'])}"
            f"/p{_scalar(event['phase'])} "
            f"source={event['source']} {event['receipt_state']} "
            f"{event['resolution']} {_scalar(event['elapsed_s'])}s"
        )
    return lines


def _unit_status(item: dict[str, Any]) -> str:
    activity = item.get("activity")
    if isinstance(activity, dict) and isinstance(activity.get("name"), str):
        status = activity["name"]
    else:
        status = "unknown"
    route = item.get("route")
    if isinstance(route, dict):
        status += (
            f" {_scalar(route.get('mode'))}"
            f"{_coordinates(route.get('destination')) or ''}"
        )
    automation = item.get("automation")
    if isinstance(automation, dict) and automation.get("controller") not in (
        None, "player",
    ):
        status += f" !controller={_scalar(automation['controller'])}"
    if "moves" in item:
        rate = item.get("type_stats")
        rate_text = (
            _scalar(rate["move_rate"])
            if isinstance(rate, dict) and "move_rate" in rate else "?"
        )
        status += f" mv{_scalar(item['moves'])}/{rate_text}"
    return status


def _briefing_unit_lines(
    items: list[Any], aliases: dict[str, str] | None = None,
) -> list[str]:
    """Group units that share a type, tile, and status onto one line."""
    # The same precondition `_STATE_RENDERERS["units"]` declares: a grouped
    # line names each unit by alias, so an item without an ID cannot be one.
    if not all(
        isinstance(item, dict) and "id" in item and "type" in item
        and "x" in item and "y" in item
        for item in items
    ):
        return [
            "  " + line
            for line in _render_section_items("units", items, aliases)
        ]
    groups: dict[tuple[str, str, str], list[str]] = {}
    for index, item in enumerate(items, start=1):
        key = (
            _need_text(item, "type", "unit"),
            _coordinates(item) or "@?",
            _unit_status(item),
        )
        groups.setdefault(key, []).append(
            _row_alias(aliases, item, "id", "u", index),
        )
    return [
        f"  {','.join(aliases)} {unit_type} {coordinates} {status}"
        for (unit_type, coordinates, status), aliases in groups.items()
    ]


def _briefing_needs_decision(
    overview: dict[str, Any], units: list[Any], cities: list[Any],
    *, partial: bool = False,
) -> str:
    notes: list[str] = []
    idle = 0
    for item in units:
        if not isinstance(item, dict):
            continue
        automation = item.get("automation")
        activity = item.get("activity")
        has_orders = (
            automation.get("has_orders") is True
            if isinstance(automation, dict) else False
        )
        idle_activity = (
            isinstance(activity, dict) and activity.get("name") == "idle"
        )
        if idle_activity and not has_orders:
            idle += 1
    if idle:
        notes.append(f"{idle} idle unit(s)")
    research = overview.get("research")
    if isinstance(research, dict) and not research.get("target"):
        notes.append("no research target")
    for item in cities:
        if isinstance(item, dict) and item.get("production") is None:
            notes.append(f"{_named(item)} has no production")
    if not notes:
        notes.append("nothing idle; end the phase when ready")
    return (
        "needs decision: " + ", ".join(notes)
        # The count is honest about its own window: a truncated briefing has
        # not seen every unit or city, so it never claims to be the empire's
        # complete decision list.
        + (" (counted over the shown page only)" if partial else "")
    )


def _render_turn(
    result: dict[str, Any],
    *,
    tiles: dict[str, Any] | None = None,
    aliases: dict[str, str] | None = None,
) -> list[str]:
    context = result["context"]
    phase = context["phase"]
    status = result["status"]
    if status != "ready":
        lines = [
            f"turn {status} | {context['game_state']} | "
            f"{_phase_text(phase)} | "
            f"obs {'yes' if context['observation_available'] else 'no'} "
            f"legal {'yes' if context['legal_actions_available'] else 'no'}"
        ]
        lines.extend(f"next: {command}" for command in result["next_commands"])
        return lines
    revision = result["state_revision"]
    overview = result["overview"]
    if not isinstance(overview, dict) or "turn" not in overview:
        raise _drift("turn briefing overview")
    header = [
        f"T{_scalar(overview['turn'])} {_revision_label(revision)}",
        context["game_state"],
        _phase_text(phase),
    ]
    if context.get("turns_remaining") is not None:
        header.append(
            f"{_scalar(context['turns_remaining'])} turns left"
        )
    lines = [" | ".join(header)]
    lines.append(
        _economy_text(overview.get("player"))
        + " | " + _research_text(overview.get("research"))
    )
    cities = result["cities"]
    units = result["units"]
    research = result["research"]
    lines.append(
        f"units {units['shown']}/{units['total']}"
        + (" (truncated)" if units["truncated"] else "")
    )
    lines.extend(_briefing_unit_lines(units["items"], aliases))
    lines.append(
        f"cities {cities['shown']}/{cities['total']}"
        + (" (truncated)" if cities["truncated"] else "")
    )
    if cities["items"]:
        lines.extend(
            "  " + line for line in _render_section_items(
                "cities", cities["items"], aliases,
            )
        )
    lines.append(
        f"research page {research['shown']}/{research['total']}"
        + (" (truncated)" if research["truncated"] else "")
    )
    # The briefing already paid for this page on the wire, so it shows the
    # names an agent needs to pick a target instead of discarding them.
    researchable = _researchable_names(research["items"])
    if researchable:
        lines.append("  researchable: " + " ".join(researchable))
    if tiles is not None and tiles["items"]:
        lines.append("terrain")
        lines.extend(
            "  " + line for line in _render_tiles(tiles["items"], aliases)
        )
    truncated = _briefing_truncation(result)
    lines.append(_briefing_needs_decision(
        overview, units["items"], cities["items"],
        partial=bool(truncated),
    ))
    # A truncated section is never a dead end: the continuation cursor is the
    # only way to reach the rest, so it is printed, not hidden behind --json.
    lines.extend(truncated)
    return lines


def _researchable_names(items: list[Any]) -> list[str]:
    """Name the technologies this seat could target right now."""
    names: list[str] = []
    for item in items:
        if not isinstance(item, dict):
            continue
        if item.get("can_target") is not True:
            continue
        name = item.get("name")
        if isinstance(name, str) and name:
            cost = item.get("path_cost")
            names.append(
                name if cost is None else f"{name}/{_scalar(cost)}"
            )
    return names


def _briefing_truncation(result: dict[str, Any]) -> list[str]:
    """Name the exact continuation for every truncated briefing section."""
    lines: list[str] = []
    for section in ("units", "cities", "research"):
        page = result[section]
        if not page["truncated"]:
            continue
        cursor = page["next_cursor"]
        if not isinstance(cursor, str) or not cursor:
            continue
        lines.append(
            f"next: just state --cursor {cursor}   # rest of {section}"
        )
    return lines


def _batch_intent(state: dict[str, Any], batch_id: str) -> str:
    """Name what a persisted batch asked for, from the local cache only."""
    encoded = state["batches"].get(batch_id)
    if not isinstance(encoded, str):
        return "batch"
    try:
        body = json.loads(encoded)
    except json.JSONDecodeError:
        return "batch"
    commands = body.get("commands") if isinstance(body, dict) else None
    if not isinstance(commands, list) or len(commands) != 1:
        return "batch"
    command = commands[0]
    if not isinstance(command, dict):
        return "batch"
    action_id = command.get("action_id")
    if not isinstance(action_id, str):
        return "batch"
    descriptor = state["actions"].get(action_id)
    if isinstance(descriptor, dict):
        text = f"{descriptor['kind']} {descriptor['label']}"
    else:
        text = action_id
    arguments = command.get("arguments")
    if isinstance(arguments, dict) and arguments:
        text += " {" + ",".join(
            f"{key}={_scalar(value)}" for key, value in arguments.items()
        ) + "}"
    return text


def _error_text(error: dict[str, Any]) -> str:
    body = error["error"]
    return f"{body['code']}: {body['message']}"


_ERROR_REMEDIES = {
    "refresh": "re-read the actor with `just legal --actor_id ID --all`, "
               "then re-issue the order",
    "retry_exact": "retry the same batch with `just retry --batch_id ID`",
    "receipt_first": "resolve the outcome with `just receipt --batch_id ID` "
                     "before any replay",
}


def _render_error_payload(payload: Any) -> list[str]:
    """Render a server refusal compactly, remedy first.

    The payload is the one this client already validated, so the fields are
    read positionally rather than probed; anything unexpected falls back to
    the raw payload so nothing is ever silently dropped.
    """
    if not isinstance(payload, dict) or not isinstance(
        payload.get("error"), dict,
    ):
        return [_scalar(payload)]
    body = payload["error"]
    lines = [f"error {_scalar(body.get('code'))}: {_scalar(body.get('message'))}"]
    revision = payload.get("state_revision")
    if isinstance(revision, dict) and "revision" in revision:
        lines[0] += f"  {_revision_label(revision)}"
    details = body.get("details")
    if isinstance(details, dict):
        safe_next = details.get("safe_next")
        remedy = _ERROR_REMEDIES.get(safe_next) if isinstance(
            safe_next, str,
        ) else None
        batch_id = details.get("batch_id")
        if remedy is not None:
            if isinstance(batch_id, str) and batch_id:
                remedy = remedy.replace("--batch_id ID", f"--batch_id {batch_id}")
            lines.append(f"next ({safe_next}): {remedy}")
        rest = [
            f"{key}={_flat(value)}"
            for key, value in sorted(details.items())
            if key not in {"safe_next", "batch_id"} and value is not None
        ]
        if rest:
            lines.append("  " + " ".join(rest))
    if body.get("retryable") is True:
        lines.append("retryable: the same request may be sent again")
    lines.append("full payload: re-run the same command with --json")
    return lines


def _receipt_line(receipt: dict[str, Any], intent: str) -> str:
    outcome = receipt["receipt_state"]
    if receipt["error"] is not None:
        outcome += " " + _error_text(receipt["error"])
    if receipt["idempotent"]:
        outcome += " idempotent"
    if outcome.startswith("accepted"):
        outcome += " (not final; resolve with just receipt)"
    return (
        f"{intent} → {outcome} {_revision_label(receipt['state_revision'])}"
        f"  {receipt['batch_id']}"
    )


def _observation_lines(observation: dict[str, Any]) -> list[str]:
    city = observation["city"]
    return [
        f"  investigated {city['name']} sz{city['size']} "
        f"{city['production']['name']} "
        f"{city['shields']['stock']} shields "
        f"({city['shields']['surplus']:+d}/turn), "
        f"{len(city['improvements'])} improvements"
    ]


def _render_receipt(receipt: dict[str, Any], intent: str) -> list[str]:
    lines = [_receipt_line(receipt, intent)]
    if receipt["observation"] is not None:
        lines.extend(_observation_lines(receipt["observation"]))
    return lines


def _render_disposition(
    disposition: dict[str, Any], intent: str,
) -> list[str]:
    receipt = disposition["receipt"]
    if receipt is not None:
        return _render_receipt(receipt, intent)
    error = disposition["error"]
    outcome = (
        "not accepted: " + _error_text(error)
        if error is not None else "outcome unknown"
    )
    return [
        f"{intent} → {outcome} next={disposition['disposition']}"
        f"  {disposition['batch_id']}"
    ]


def _render_join(
    session: dict[str, Any], result: dict[str, Any], path: Path,
) -> list[str]:
    timeout = session.get("action_timeout_s")
    timing = (
        f"timing {_scalar(session.get('timing_mode'))}"
        + (
            " no deadline" if timeout is None
            else f" {_scalar(timeout)}s per turn"
        )
    )
    protocol = session["control_protocol"]
    lines = [
        f"joined {session['game_id']} as {session['controller_label']} | "
        f"seat {_scalar(session.get('place'))} "
        f"{_scalar(session.get('player_name'))} | proto {protocol} | "
        f"state {_scalar(result.get('state'))} | {timing}",
        f"session {path}",
    ]
    if "objective" in session:
        lines.append(
            f"objective {session['objective']} | max_turns "
            f"{_scalar(session['max_turns'])} | turns_remaining "
            f"{_scalar(session['turns_remaining'])}"
        )
    if protocol == FULL_CONTROL_V2:
        lines.append(
            "PROTOCOL full-control-v2 — read state, execute one enumerated "
            "opaque action, resolve its receipt. The same card is "
            "state/header.txt."
        )
        lines.extend(V2_PROTOCOL_CARD)
    else:
        lines.extend([
            "PROTOCOL strategic-v1 — poll a turn, submit one action.",
            "  just next --after_turn LAST_TURN",
            "  just act --turn TURN --observation_id ID --action JSON",
        ])
    return lines


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

Read AGENTS.md, then run:

  just join --game_id {game_id} --name {name}{place}

The command returns a `session_file` and the negotiated protocol. If join
reports `strategic-v1`, pass that exact path as `--session` in every command,
never the shared `.sessions/current` pointer, and repeat:

  just next --session SESSION_FILE --after_turn LAST_TURN
  just act --session SESSION_FILE --turn TURN --observation_id OBSERVATION_ID --action '{{"type":"set_traits","traits":{{"aggressive":0,"builder":20,"expansionist":30,"trader":10}}}}'

Advance LAST_TURN only after `act` returns `accepted: true`. If `act` fails or
returns anything else, do not claim success and do not advance; poll again with
the same explicit session and LAST_TURN so the server can redeliver the turn.

If join reports `full-control-v2`, the command contract is the protocol card
join prints; run `just help` for the play card. `--session` is optional there.
Errors carry their own remedy, so read the refusal instead of the docs.

Use only the negotiated protocol's authenticated private state for decisions.
Never inspect parent directories or spectator data. Stop on completed, invalid,
failed, or cancelled. Keep this same conversation active and repeat the loop
until the game is terminal; do not give a final answer or stop merely because
one turn completed. If a command itself fails, fix that command and continue
rather than treating the game as finished. If GAME_ID is still a placeholder,
or join fails, stop and ask the user instead of inventing a game or retrying
blindly.""")
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
    if session["game_id"] != args.game_id:
        raise PlayerError("the join response belongs to a different game")
    if session["controller_label"] != controller:
        raise PlayerError(
            "the join response controller label does not match the requested "
            "harness-model identity"
        )
    if session["control_protocol"] != control_protocol:
        raise PlayerError("the join result changed the preflight control protocol")
    if control_protocol == FULL_CONTROL_V2:
        evaluation = _validate_evaluation_context(
            result, "v2 join result",
        )
        if evaluation is not None:
            session.update(evaluation)
        supported = session["supported_control_protocols"]
        if (
            not isinstance(supported, list)
            or FULL_CONTROL_V2 not in supported
            or any(not isinstance(item, str) or not item for item in supported)
        ):
            raise PlayerError("the v2 join result omitted the negotiated protocol")
        if not isinstance(result.get("v2_transport_available"), bool):
            raise PlayerError("the v2 join result omitted transport availability")
        if (
            result["v2_transport_available"] is not True
            or result.get("state") in TERMINAL_STATES
            or result.get("error") is not None
        ):
            raise PlayerError(
                "the full-control-v2 transport did not become playable; "
                "stop and tell the game owner"
            )
        prefix = f"{base}/v2/games/{args.game_id}/me"
        expected_endpoints = {
            "health_url": f"{prefix}/health",
            "state_url": f"{prefix}/state",
            "legal_actions_url": f"{prefix}/legal-actions",
            "batches_url": f"{prefix}/batches",
            "receipts_url": f"{prefix}/receipts/{{batch_id}}",
            "wait_url": f"{prefix}/wait",
            "openapi_url": f"{base}/v2/openapi.json",
        }
        for name, expected in expected_endpoints.items():
            endpoint = result.get(name)
            if not isinstance(endpoint, str) or endpoint != expected:
                raise PlayerError(
                    f"the v2 join result has an invalid same-origin {name}"
                )
    path = _state_root() / args.game_id / f"{_session_key(controller)}.json"
    _write_private_json(path, session)
    _set_current_session(path)
    public = {key: value for key, value in result.items()
              if key != "agent_token"}
    public["session_saved"] = True
    public["session_file"] = str(path)
    if _json_requested(args):
        _print_json(public)
    else:
        _render(_render_join(session, public, path))
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
        evaluation_line = (
            ""
            if "objective" not in session
            else (
                f"Objective: {session['objective']}\n"
                f"Turn budget: {session['max_turns']} maximum; "
                + (
                    "remaining turns unavailable until native play starts.\n"
                    if session["turns_remaining"] is None
                    else f"{session['turns_remaining']} remaining.\n"
                )
            )
        )
        # Turn one is where the model forms its API model, so exactly one
        # protocol contract is taught: the card already printed on stdout (and
        # kept in `state/header.txt`).  This block carries only what the card
        # cannot -- the evaluation frame and the three rules that are about
        # conduct rather than syntax.
        print(
            f"\nJoined a full-control-v2 session.\nSession file: {path}\n"
            f"Timing mode: {timing_mode}; {deadline}.\n"
            f"{evaluation_line}"
            "Do not use strategic `just next` or `just act`. The command "
            "contract is the protocol card printed on stdout above; it is "
            "also saved in state/header.txt, so `just show header` re-reads "
            "it without a network call.\n"
            "You—the assigned harness/model—must choose every action "
            "yourself. Do not write, launch, or delegate to an automated bot, "
            "and do not hand a unit to the game's own AI.\n"
            "LOBBY FIRST: while health says game_state=lobby, do not call "
            "`just wait`. Run `just start --nation N --leader L "
            "--male|--female [--style S]`: it reads pregame_nations, "
            "pregame_styles and pregame_teams for you, then executes the "
            "enumerated pregame.configure and pregame.set_ready. The last "
            "ready seat starts Freeciv.\n"
            "An ambiguous receipt is terminal and must never be replayed; "
            "resolve it with `just receipt --batch_id ID`.\n"
            "Keep playing until the game is terminal; do not final-answer "
            "merely because one turn completed. A failed wait command is a "
            "harness error to correct, not a terminal game.",
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


def _limit(value: str | None, *, default: int = 16) -> int:
    if value is None:
        return default
    if re.fullmatch(r"(?:[1-9]|1[0-6])", value) is None:
        raise PlayerError("limit must be a canonical integer from 1 through 16")
    return int(value)


def _parse_json_object(value: str, label: str) -> dict[str, Any]:
    def pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, item in items:
            if key in result:
                raise PlayerError(f"{label} must not contain duplicate keys")
            result[key] = item
        return result

    try:
        parsed = json.loads(
            value, object_pairs_hook=pairs,
            parse_constant=lambda token: (_ for _ in ()).throw(
                ValueError(f"non-finite number {token}")
            ),
        )
    except (json.JSONDecodeError, ValueError) as exc:
        raise PlayerError(f"{label} must be valid strict JSON: {exc}") from exc
    if not isinstance(parsed, dict):
        raise PlayerError(f"{label} must be a JSON object")
    return _json_value(parsed, label)


def command_health(args: argparse.Namespace) -> int:
    path, session = _v2_session(args.session)
    response = _v2_response(
        "GET", _v2_url(session, "/health"), session, timeout=10,
    )
    if not 200 <= response.status < 300:
        _raise_validated_v2_error(response)
    value = _validate_health(response.value, session)
    _mirror_health(path, value, "health")
    if _json_requested(args):
        _print_v2_json(value)
    else:
        _render(_render_health(value))
    return 0


def _turn_health_epoch(health: dict[str, Any]) -> tuple[Any, ...]:
    phase = health["phase"]
    return (
        health["game_state"], health["observation_available"],
        health["legal_actions_available"],
        None if phase is None else phase["state"],
        None if phase is None else phase["turn"],
        None if phase is None else phase["phase"],
        None if phase is None else phase["active"],
    )


def _turn_health_context(health: dict[str, Any]) -> dict[str, Any]:
    return {
        "game_state": health["game_state"],
        "objective": health.get("objective"),
        "max_turns": health.get("max_turns"),
        "turns_remaining": health.get("turns_remaining"),
        "agent": health["agent"],
        "seat": health["seat"],
        "sidecar": health["sidecar"],
        "observation_available": health["observation_available"],
        "legal_actions_available": health["legal_actions_available"],
        "phase": health["phase"],
        "last_phase_end": health["last_phase_end"],
    }


def _turn_next_commands(pages: dict[str, dict[str, Any]]) -> list[str]:
    """Name the follow-up commands bare: --session resolves itself."""
    commands = []
    for section in V2_TURN_SECTIONS:
        cursor = pages[section]["page"]["next_cursor"]
        if cursor is not None:
            commands.append(f"just state --cursor {cursor}")
    commands.extend((
        "just legal --kind phase.end --all",
        "just legal --kind research.set_target --all",
        "just legal --kind economy.set_rates --all",
        "just legal --actor_id ACTOR_ID --all",
    ))
    return commands


def _turn_compact_page(page: dict[str, Any]) -> dict[str, Any]:
    value = page["page"]
    return {
        "shown": len(value["items"]),
        "total": value["total_items"],
        "truncated": value["next_cursor"] is not None,
        "items": value["items"],
        "next_cursor": value["next_cursor"],
        "cursor_expires_at": value["cursor_expires_at"],
    }


def _turn_page(
    session: dict[str, Any], section: str,
) -> dict[str, Any]:
    query = urllib.parse.urlencode({
        "section": section, "limit": V2_TURN_PAGE_LIMIT,
    })
    response = _v2_response(
        "GET", _v2_url(session, "/state") + f"?{query}", session,
        timeout=10,
    )
    if not 200 <= response.status < 300:
        _raise_validated_v2_error(response)
    return _validate_page(response.value, session, legal=False)


def _turn_health(session: dict[str, Any]) -> dict[str, Any]:
    response = _v2_response(
        "GET", _v2_url(session, "/health"), session, timeout=10,
    )
    if not 200 <= response.status < 300:
        _raise_validated_v2_error(response)
    return _validate_health(response.value, session)


def _emit_turn(
    args: argparse.Namespace,
    result: dict[str, Any],
    aliases: dict[str, str] | None = None,
) -> None:
    if _json_requested(args):
        _print_v2_json(result)
    else:
        _render(_render_turn(result, aliases=aliases))


def _cached_kind_action(
    state: dict[str, Any], kind: str,
) -> dict[str, Any] | None:
    matches = [item for item in _order_pool(state) if item["kind"] == kind]
    return matches[0] if len(matches) == 1 else None


def _resolve_kind_action(
    path: Path, session: dict[str, Any], kind: str, remedy: str,
) -> tuple[dict[str, Any], dict[str, Any]]:
    """Return one cached action of this kind, enumerating it if it is absent.

    The enumeration is the ordinary drain this client already performs; it
    happens inside the CLI so the agent never pays for a catalog it only
    wanted a single handle from.
    """
    state = _load_v2_client_state(path, session)
    compact = _cached_kind_action(state, kind)
    if compact is None:
        _drain_legal_unlocked(path, session)
        state = _load_v2_client_state(path, session)
        compact = _cached_kind_action(state, kind)
    if compact is None:
        raise PlayerError(
            f"no {kind} action is enumerable for this seat right now; {remedy}"
        )
    return compact, state


def _await_line(wait: dict[str, Any]) -> str:
    """Render the next briefing header from one validated wake."""
    health = wait["health"]
    phase = health["phase"]
    turn = None if phase is None else phase["turn"]
    header = "T?" if turn is None else f"T{_scalar(turn)}"
    revision = wait["state_revision"]
    if revision is not None:
        header += " " + _revision_label(revision)
    return (
        f"{header} | woke {wait['wake_reason']} | {health['game_state']} | "
        f"{_phase_text(phase)} | next: just turn"
    )


def _command_turn_end(
    args: argparse.Namespace,
    path: Path,
    session: dict[str, Any],
    *,
    await_next: bool,
) -> int:
    """End this phase from the cached capability, optionally blocking after."""
    lines: list[str] = []
    wait: dict[str, Any] | None = None
    with _v2_request_lock(path):
        compact, _state = _resolve_kind_action(
            path, session, "phase.end",
            "the phase may not be yours to end yet -- run `just turn` and "
            "check that the phase is active",
        )
        arguments = _default_arguments(compact)
        if arguments is None:
            raise PlayerError(
                "this phase.end action takes arguments; run "
                "`just legal --kind phase.end --all` and submit it with "
                "`just batch`"
            )
        batch_id = _persist_batch_for_action(
            path, session, compact["action_id"], arguments,
        )
        disposition, warning, exit_code = _submit_persisted_batch(
            path, session, batch_id,
        )
        lines.extend(_render_disposition(disposition, "phase end"))
        if warning:
            print(warning, file=sys.stderr)
        if await_next and _order_receipt_ok(disposition):
            wait = _wait_value(path, session, args)
            lines.append(_await_line(wait))
        elif await_next:
            lines.append("not awaited: the phase end was not accepted")
    if _json_requested(args):
        _print_v2_json({
            "schema_version": 1,
            "command": "turn",
            "status": "ended",
            "disposition": disposition,
            "wait": wait,
        })
    else:
        _render(lines)
    return exit_code


def command_turn(args: argparse.Namespace) -> int:
    path, session = _v2_session(args.session)
    end_phase = bool(getattr(args, "end_phase", False))
    await_next = bool(getattr(args, "await_phase", False))
    if await_next and not end_phase:
        raise PlayerError(
            "just turn --await blocks after ending the phase; use "
            "`just turn --end --await`, or `just wait` on its own"
        )
    if end_phase:
        return _command_turn_end(args, path, session, await_next=await_next)
    with _v2_request_lock(path):
        for attempt in range(2):
            health = _turn_health(session)
            if health["game_state"] in TERMINAL_STATES:
                _emit_turn(args, {
                    "schema_version": 1,
                    "command": "turn",
                    "status": "terminal",
                    "context": _turn_health_context(health),
                    "next_commands": [
                        f"just result {session['game_id']}",
                    ],
                })
                return 0
            phase = health["phase"]
            actionable = (
                health["game_state"] == "running"
                and health["observation_available"]
                and isinstance(phase, dict)
                and phase["active"] is True
                and phase["state"] == "awaiting_agent"
            )
            if not actionable:
                if health["game_state"] == "lobby":
                    next_commands = [
                        "just state --section overview",
                        "just state --section pregame_nations",
                        "just state --section pregame_styles",
                        "just state --section pregame_teams",
                        "just legal",
                    ]
                    status = "lobby"
                else:
                    next_commands = ["just wait"]
                    status = "not_ready"
                _emit_turn(args, {
                    "schema_version": 1,
                    "command": "turn",
                    "status": status,
                    "context": _turn_health_context(health),
                    "next_commands": next_commands,
                })
                return 0

            pages = {
                section: _turn_page(session, section)
                for section in V2_TURN_SECTIONS
            }
            final_health = _turn_health(session)
            revisions = [
                pages[section]["state_revision"]
                for section in V2_TURN_SECTIONS
            ]
            consistent = all(value == revisions[0] for value in revisions[1:])
            consistent = consistent and (
                _turn_health_epoch(health) == _turn_health_epoch(final_health)
            )
            phase = final_health["phase"]
            if phase is not None and phase["turn"] is not None:
                consistent = consistent and phase["turn"] == revisions[0]["turn"]
            if not consistent:
                if attempt == 0:
                    continue
                raise PlayerError(
                    "the game changed twice while building the turn briefing; "
                    "run `just turn` again"
                )

            state = _load_v2_client_state(path, session)
            for section in V2_TURN_SECTIONS:
                _remember_page(path, state, pages[section], legal=False)
            for section in V2_TURN_SECTIONS:
                _mirror_page(path, state, pages[section], "turn")
            _mirror_health(path, final_health, "turn", revisions[0])
            overview_items = pages["overview"]["page"]["items"]
            if len(overview_items) != 1:
                raise PlayerError("the turn briefing has no current overview")
            result = {
                "schema_version": 1,
                "command": "turn",
                "status": "ready",
                "context": _turn_health_context(final_health),
                "state_revision": revisions[0],
                "overview": overview_items[0],
                "cities": _turn_compact_page(pages["cities"]),
                "units": _turn_compact_page(pages["units"]),
                "research": _turn_compact_page(pages["research"]),
                "next_commands": _turn_next_commands(pages),
            }
            _emit_turn(args, result, _alias_map(state))
            return 0
    raise AssertionError("unreachable turn briefing state")


def _state_query(args: argparse.Namespace) -> str:
    cursor = args.cursor.strip()
    section = args.section.strip()
    actor_id = getattr(args, "actor_id", "").strip()
    relation_id = getattr(args, "relation_id", "").strip()
    center_id = getattr(args, "center_id", "").strip()
    radius = getattr(args, "radius", None)
    limit = args.limit
    if cursor:
        if (
            section or actor_id or relation_id or center_id or radius is not None
            or limit is not None or CURSOR_RE.fullmatch(cursor) is None
        ):
            raise PlayerError("state cursor must be the only page option")
        return urllib.parse.urlencode({"cursor": cursor})
    if limit is not None and not section:
        raise PlayerError("state --limit requires --section")
    if section and section not in V2_SECTIONS:
        available = ", ".join(sorted(V2_SECTIONS))
        raise PlayerError(
            f"state section {section!r} is not supported; valid sections: "
            f"{available}. Economy and current government are in overview; "
            "government choices are in governments"
        )
    if not section:
        if actor_id or relation_id or center_id or radius is not None:
            raise PlayerError("state scope options require --section")
        return ""
    params: dict[str, Any] = {"section": section, "limit": _limit(limit)}
    if section in V2_CITY_SECTIONS:
        if (
            CITY_ID_RE.fullmatch(actor_id) is None
            or relation_id or center_id or radius is not None
        ):
            raise PlayerError(
                "city state sections require exactly one opaque --actor-id"
            )
        params["actor_id"] = actor_id
    elif section == "diplomacy_clauses":
        if (
            actor_id or RELATION_ID_RE.fullmatch(relation_id) is None
            or center_id or radius is not None
        ):
            raise PlayerError(
                "diplomacy_clauses requires exactly one opaque --relation-id"
            )
        params["relation_id"] = relation_id
    elif section == "tile_window":
        if (
            actor_id or relation_id or TILE_ID_RE.fullmatch(center_id) is None
            or isinstance(radius, bool) or not isinstance(radius, int)
            or not 0 <= radius <= 8
        ):
            raise PlayerError(
                "tile_window requires --center-id and --radius from 0 through 8"
            )
        params["center_id"] = center_id
        params["radius"] = radius
    elif section == "unit_route":
        if (
            ACTOR_ID_RE.fullmatch(actor_id) is None
            or not actor_id.startswith("unit_")
            or relation_id or center_id or radius is not None
        ):
            raise PlayerError(
                "unit_route requires exactly one opaque unit --actor-id"
            )
        params["actor_id"] = actor_id
    elif actor_id or relation_id or center_id or radius is not None:
        raise PlayerError("state scope options are not valid for this section")
    return urllib.parse.urlencode(params)


def command_state(args: argparse.Namespace) -> int:
    path, session = _v2_session(args.session)
    args = _resolve_alias_arguments(
        path, session, args, ("actor_id", "relation_id", "center_id"),
    )
    query = _state_query(args)
    url = _v2_url(session, "/state") + (f"?{query}" if query else "")
    response = _v2_response("GET", url, session, timeout=10)
    if not 200 <= response.status < 300:
        try:
            _raise_validated_v2_error(response)
        except V2ResponseError as exc:
            if exc.payload["error"]["code"] == "cursor_expired" and args.cursor:
                state = _load_v2_client_state(path, session)
                _drop_pending_for_cursor(path, state, args.cursor.strip())
            raise
    value = _validate_page(response.value, session, legal=False)
    state = _load_v2_client_state(path, session)
    _remember_page(path, state, value, legal=False)
    _mirror_page(path, state, value, "state")
    if _json_requested(args):
        _print_v2_json(value)
    else:
        _render(_render_state_page(value, _alias_map(state)))
    return 0


def _legal_query(
    args: argparse.Namespace, *, ignore_limit: bool = False,
) -> str:
    cursor = args.cursor.strip()
    actor = args.actor_id.strip()
    target = args.target_id.strip()
    limit = None if ignore_limit else args.limit
    if cursor:
        if actor or target or limit is not None or CURSOR_RE.fullmatch(cursor) is None:
            raise PlayerError("legal cursor must be the only page option")
        return urllib.parse.urlencode({"cursor": cursor})
    if target:
        if not actor:
            raise PlayerError("legal target requires actor")
        if ACTOR_ID_RE.fullmatch(actor) is None or (
            TILE_ID_RE.fullmatch(target) is None
            and RELATION_ID_RE.fullmatch(target) is None
        ):
            raise PlayerError("actor or target ID has the wrong v2 ID type")
        if limit is not None and RELATION_ID_RE.fullmatch(target) is not None:
            raise PlayerError("legal relation target does not accept a limit")
        params = {"actor_id": actor, "target_id": target}
        if limit is not None:
            params["limit"] = _limit(limit)
        return urllib.parse.urlencode(params)
    if actor and ACTOR_ID_RE.fullmatch(actor) is None:
        raise PlayerError("actor ID has the wrong v2 ID type")
    params: dict[str, Any] = {}
    if actor:
        params["actor_id"] = actor
    if limit is not None:
        params["limit"] = _limit(limit)
    return urllib.parse.urlencode(params)


def _read_legal_page(
    path: Path,
    session: dict[str, Any],
    query: str,
    *,
    cursor: str,
    actor_id: str,
    target_id: str,
) -> dict[str, Any]:
    url = _v2_url(session, "/legal-actions") + (f"?{query}" if query else "")
    response = _v2_response("GET", url, session, timeout=10)
    if not 200 <= response.status < 300:
        try:
            _raise_validated_v2_error(response)
        except V2ResponseError as exc:
            if exc.payload["error"]["code"] == "cursor_expired" and cursor:
                state = _load_v2_client_state(path, session)
                _drop_pending_for_cursor(path, state, cursor)
            raise
    try:
        value = _validate_page(response.value, session, legal=True)
    except PlayerError:
        state = _load_v2_client_state(path, session)
        if cursor:
            _drop_pending_for_cursor(path, state, cursor)
        elif actor_id:
            _drop_pending_for_scope(
                path, state, actor_id, target_id,
            )
        raise
    state = _load_v2_client_state(path, session)
    promoted = _remember_page(path, state, value, legal=True)
    _mirror_page(path, state, _promoted_catalog_page(value, promoted), "legal")
    return value


def _compact_legal_action(descriptor: dict[str, Any]) -> dict[str, Any]:
    subject = descriptor["subject"]
    reserved_subject_terms = {
        "internal", "native", "packet", "private", "wire",
    }
    # The leak guard keeps a would-be internal value out of agent context, but
    # it never hides that a discriminator existed: the key survives with a
    # `<withheld>` value so the row still shows the choice was distinguished,
    # and `--json` carries the payload verbatim.
    compact_subject: dict[str, Any] = {}
    for key, value in subject.items():
        if key in {"target", "probability", "gold_cost"}:
            continue
        withheld = key.startswith("_") or bool(
            reserved_subject_terms.intersection(
                part for part in re.split(r"[^a-z0-9]+", key.casefold())
                if part
            )
        )
        compact_subject[key] = V2_WITHHELD if withheld else value
    result = {
        "action_id": descriptor["action_id"],
        "kind": descriptor["kind"],
        "label": descriptor["label"],
        "subject": compact_subject,
        "target": subject.get("target"),
        "argument_schema": descriptor["arguments_schema"],
    }
    # Omit-when-default, never omit-by-type: the key is dropped only when the
    # payload carries the exact certain-probability envelope.  A probability
    # this client cannot interpret is still a probability, and an espionage
    # gamble that rendered as certain is the one over-compaction that would
    # corrupt a decision.
    if "probability" in subject and subject["probability"] != {
        "kind": "exact", "minimum_percent": 100, "maximum_percent": 100,
    }:
        result["probability"] = subject["probability"]
    gold_cost = subject.get("gold_cost")
    target = subject.get("target")
    if gold_cost is None and isinstance(target, dict):
        gold_cost = target.get("gold_cost")
    if gold_cost is not None:
        result["gold_cost"] = gold_cost
    gold = descriptor["arguments_schema"].get("properties", {}).get("gold")
    if isinstance(gold, dict):
        result["gold_range"] = {
            key: gold[key] for key in ("minimum", "maximum") if key in gold
        }
    return result


def _compact_legal_offset(value: Any) -> int:
    if value in (None, ""):
        return 0
    text = str(value)
    if (
        re.fullmatch(r"(?:0|[1-9][0-9]*)", text) is None
        or int(text) > V2_LEGAL_DRAIN_MAX_PAGES * 16
    ):
        raise PlayerError(
            "legal --offset must be a canonical integer from 0 through 8192"
        )
    return int(text)


def _compact_legal_limit(value: Any, *, default: int = V2_LEGAL_MATCH_LIMIT) -> int:
    if value in (None, ""):
        return default
    text = str(value)
    if re.fullmatch(r"(?:[1-9]|[1-5][0-9]|6[0-4])", text) is None:
        raise PlayerError(
            "legal --kind/--all --limit must be a canonical integer from "
            "1 through 64"
        )
    return int(text)


def _command_legal_all(
    args: argparse.Namespace,
    path: Path,
    session: dict[str, Any],
    kind: str,
) -> int:
    """Drain one catalog completely and print it once.

    ``kind`` selects one class of action; an empty ``kind`` keeps every action
    the drained scope enumerated, which is the ``--actor_id ... --all`` form.
    Either way the drain, the validation, and the atomic promotion of the
    complete catalog into local state are exactly the same work.
    """
    if args.cursor.strip():
        raise PlayerError("legal --all starts a catalog; omit --cursor")
    offset = _compact_legal_offset(getattr(args, "offset", ""))
    compact_limit = _compact_legal_limit(
        getattr(args, "limit", None),
        # `--kind` is a filtered window over every actor, so 64 matches is a
        # sensible ceiling.  `--actor_id ... --all` promises one actor's whole
        # menu, which routinely exceeds 64 rows; only the byte cap bounds it.
        default=V2_LEGAL_MATCH_LIMIT if kind else V2_LEGAL_ACTOR_MATCH_LIMIT,
    )
    query = _legal_query(args, ignore_limit=True)
    cursor = ""
    actor_id = args.actor_id.strip()
    target_id = args.target_id.strip()
    revision = None
    catalog_total = None
    seen_cursors: set[str] = set()
    matched = 0
    compact_actions: list[dict[str, Any]] = []
    compact_bytes = 0
    byte_limited = False
    oversized_single = False
    pages_read = 0
    with _v2_request_lock(path):
        for pages_read in range(1, V2_LEGAL_DRAIN_MAX_PAGES + 1):
            value = _read_legal_page(
                path, session, query, cursor=cursor,
                actor_id=actor_id, target_id=target_id,
            )
            if revision is None:
                revision = value["state_revision"]
                catalog_total = value["page"]["total_items"]
            elif (
                value["state_revision"] != revision
                or value["page"]["total_items"] != catalog_total
            ):
                raise PlayerError(
                    "the legal catalog changed while it was being drained; "
                    "run the same command again"
                )
            for descriptor in value["page"]["items"]:
                if kind and descriptor["kind"] != kind:
                    continue
                match_offset = matched
                matched += 1
                if match_offset < offset or byte_limited:
                    continue
                if len(compact_actions) >= compact_limit:
                    continue
                compact = _compact_legal_action(descriptor)
                encoded_size = len(json.dumps(
                    compact, sort_keys=True, separators=(",", ":"),
                ).encode("utf-8"))
                if compact_bytes + encoded_size > V2_LEGAL_COMPACT_MAX_BYTES:
                    byte_limited = True
                    if not compact_actions:
                        if encoded_size > V2_LEGAL_SINGLE_ACTION_MAX_BYTES:
                            raise PlayerError(
                                "one compact legal action exceeds the bounded "
                                "64 KiB single-action contract"
                            )
                        compact_actions.append(compact)
                        compact_bytes += encoded_size
                        oversized_single = True
                    continue
                compact_actions.append(compact)
                compact_bytes += encoded_size
            cursor = value["page"]["next_cursor"] or ""
            if not cursor:
                break
            if cursor in seen_cursors:
                raise PlayerError("the legal catalog repeated a cursor")
            seen_cursors.add(cursor)
            query = urllib.parse.urlencode({"cursor": cursor})
        else:
            raise PlayerError(
                "the legal catalog exceeded the safe 512-page drain limit"
            )
    next_offset = offset + len(compact_actions)
    has_more = next_offset < matched
    result = {
        "schema_version": 1,
        "command": "legal",
        "kind": kind or None,
        "state_revision": revision,
        "catalog_total": catalog_total,
        "pages_read": pages_read,
        "matched": matched,
        "offset": offset,
        "limit": compact_limit,
        "shown": len(compact_actions),
        "truncated": len(compact_actions) < matched,
        "has_more": has_more,
        "next_offset": next_offset if has_more else None,
        "byte_limited": byte_limited,
        "oversized_single": oversized_single,
        "actions": compact_actions,
    }
    if _json_requested(args):
        _print_v2_json(result)
        return 0
    scope = _requested_scope(actor_id, target_id)
    state = _load_v2_client_state(path, session)
    aliases = _alias_map(state)
    _render(_render_legal_compact(
        result, scope, aliases,
        # An actor's whole catalog is the one that repeats across identical
        # units; a kind-filtered window is not a catalog and is never deduped.
        None if kind else _catalog_equivalence(state, result, scope, aliases),
    ))
    return 0


def command_legal(args: argparse.Namespace) -> int:
    path, session = _v2_session(args.session)
    args = _resolve_alias_arguments(
        path, session, args, ("actor_id", "target_id"),
    )
    kind = getattr(args, "kind", "").strip()
    all_pages = bool(getattr(args, "all_pages", False))
    actor_id = args.actor_id.strip()
    if kind and not all_pages:
        raise PlayerError("use --kind ACTION_KIND and --all together")
    if all_pages and not kind and not actor_id:
        raise PlayerError(
            "legal --all needs a scope: use `--kind ACTION_KIND --all` for one "
            "class of action, or `--actor_id ACTOR_ID [--target_id TARGET_ID] "
            "--all` for one actor's complete catalog"
        )
    if kind and ACTION_KIND_RE.fullmatch(kind) is None:
        raise PlayerError("legal --kind must be an exact public action kind")
    if all_pages:
        return _command_legal_all(args, path, session, kind)
    if getattr(args, "offset", "") not in (None, ""):
        raise PlayerError(
            "legal --offset requires --all with --kind or --actor_id"
        )
    query = _legal_query(args)
    value = _read_legal_page(
        path, session, query, cursor=args.cursor.strip(),
        actor_id=args.actor_id.strip(), target_id=args.target_id.strip(),
    )
    if _json_requested(args):
        _print_v2_json(value)
    else:
        _render(_render_legal_page(
            value, _alias_map(_load_v2_client_state(path, session)),
        ))
    return 0


def _canonical_body(value: dict[str, Any]) -> bytes:
    try:
        return json.dumps(
            value, sort_keys=True, separators=(",", ":"), ensure_ascii=False,
            allow_nan=False,
        ).encode("utf-8")
    except (TypeError, ValueError) as exc:
        raise PlayerError(f"command batch is not canonical JSON: {exc}") from exc


def _batch_disposition(
    session: dict[str, Any],
    batch_id: str,
    disposition: str,
    *,
    receipt: dict[str, Any] | None = None,
    error: dict[str, Any] | None = None,
) -> dict[str, Any]:
    batch_id = _opaque(batch_id, "batch disposition ID")
    if disposition not in V2_DISPOSITIONS:
        raise PlayerError("invalid batch disposition")
    if receipt is not None:
        receipt = _validate_receipt(receipt, session, batch_id=batch_id)
    if error is not None:
        error = _validate_error(error)
    if (
        disposition == "receipt_terminal"
        and (
            receipt is None
            or receipt["receipt_state"] not in V2_TERMINAL_RECEIPTS
            or error is not None
        )
        or disposition == "receipt_poll"
        and (
            receipt is None
            or receipt["receipt_state"] != "accepted"
            or error is not None
        )
        or disposition in {"retry_exact", "refresh"}
        and (receipt is not None or error is None)
        or disposition == "receipt_first" and receipt is not None
    ):
        raise PlayerError("invalid batch disposition payload")
    return {
        "schema_version": 2,
        "control_protocol": FULL_CONTROL_V2,
        "game_id": session["game_id"],
        "agent_id": session["agent_id"],
        "batch_id": batch_id,
        "disposition": disposition,
        "receipt": receipt,
        "error": error,
    }


def _batch_error_disposition(
    response: JSONResponse,
    session: dict[str, Any],
    batch_id: str,
) -> dict[str, Any]:
    error = _validate_error(response.value)
    error_body = error["error"]
    details = error_body["details"]
    if (
        details.get("batch_id") != batch_id
        or details.get("acceptance") != "not_accepted"
        or details.get("safe_next") not in {
            "refresh", "retry_exact", "receipt_first",
        }
    ):
        raise PlayerError("batch error omitted its safe recovery contract")
    code = error_body["code"]
    if code in {"conflict", "internal_error", "action_outcome_ambiguous"}:
        expected = "receipt_first"
    elif (
        code == "rate_limited"
        and response.status == 429
        and error_body["retryable"] is True
        or code == "sidecar_unavailable"
        and response.status == 503
        and error_body["retryable"] is True
    ):
        expected = "retry_exact"
    else:
        expected = "refresh"
    if details["safe_next"] != expected:
        raise PlayerError("batch error recovery contract contradicts its code")
    return _batch_disposition(
        session,
        batch_id,
        details["safe_next"],
        error=error,
    )


def _persist_batch_for_action(
    path: Path,
    session: dict[str, Any],
    action_id: str,
    arguments: dict[str, Any],
) -> str:
    with _v2_state_lock(path):
        state = _load_v2_client_state_unlocked(path, session)
        descriptor = state["actions"].get(action_id)
        if not isinstance(descriptor, dict):
            # A staged action is not expired, it is merely not executable yet:
            # say which of the two happened and name the drain that fixes it.
            staged = next(
                (
                    pending for pending in state["pending_catalogs"].values()
                    if action_id in pending["items"]
                ),
                None,
            )
            if staged is not None:
                scope_id = staged["scope"]["actor_id"]
                named = _alias_map(state).get(scope_id, scope_id)
                raise PlayerError(
                    "unknown or expired action ID: this action came from a "
                    "catalog page that is still incomplete, and only a "
                    "complete catalog is executable; run "
                    f"`just legal --actor_id {named} --all`"
                )
            raise PlayerError(
                "unknown or expired action ID; run the matching `just legal` query"
            )
        revision = _validate_revision(descriptor.get("state_revision"))
        if state["last_revision"] != revision:
            raise PlayerError("the cached action is not from the latest revision")
        batch_id = f"batch_{secrets.token_urlsafe(24)}"
        while batch_id in state["batches"]:
            batch_id = f"batch_{secrets.token_urlsafe(24)}"
        body = {
            "schema_version": 2,
            "control_protocol": FULL_CONTROL_V2,
            "game_id": session["game_id"],
            "agent_id": session["agent_id"],
            "batch_id": batch_id,
            "state_revision": revision,
            "commands": [{"action_id": action_id, "arguments": arguments}],
        }
        state["batches"][batch_id] = _canonical_body(body).decode("utf-8")
        _save_v2_client_state_unlocked(path, state)
        return batch_id


def _submit_persisted_batch(
    path: Path,
    session: dict[str, Any],
    batch_id: str,
) -> tuple[dict[str, Any], str | None, int]:
    state = _load_v2_client_state(path, session)
    encoded = state["batches"].get(batch_id)
    if not isinstance(encoded, str):
        raise PlayerError(f"no persisted command batch {batch_id!r}")
    body_bytes = encoded.encode("utf-8")
    try:
        response = _v2_response(
            "POST", _v2_url(session, "/batches"), session,
            encoded_body=body_bytes, timeout=30,
        )
    except PlayerError as exc:
        return (
            _batch_disposition(session, batch_id, "receipt_first"),
            f"transport outcome is unknown for batch {batch_id}. Check its "
            f"receipt first with `just receipt --batch_id "
            f"{batch_id}`; never blindly replay it.",
            2,
        )
    try:
        receipt = _validate_receipt(response.value, session, batch_id=batch_id)
    except PlayerError:
        if not 200 <= response.status < 300:
            try:
                disposition = _batch_error_disposition(
                    response, session, batch_id,
                )
            except PlayerError:
                return (
                    _batch_disposition(session, batch_id, "receipt_first"),
                    "the server response did not prove that this persisted "
                    "batch was unaccepted; resolve its receipt first",
                    2,
                )
            return (
                disposition,
                "the server proved this batch was not accepted; follow "
                f"the {disposition['disposition']} disposition",
                2,
            )
        return (
            _batch_disposition(session, batch_id, "receipt_first"),
            "the server returned an invalid success response; resolve the "
            "persisted batch by receipt before any retry",
            2,
        )
    cache_warning = None
    try:
        _remember_receipt(path, state, receipt)
        _mirror_receipt(path, receipt)
    except PlayerError:
        cache_warning = (
            "the authoritative receipt was validated but could not be cached "
            "locally; retain this output"
        )
    disposition = (
        "receipt_poll"
        if receipt["receipt_state"] == "accepted"
        else "receipt_terminal"
    )
    return (
        _batch_disposition(
            session, batch_id, disposition, receipt=receipt,
        ),
        cache_warning,
        0,
    )


def command_batch(args: argparse.Namespace) -> int:
    path, session = _v2_session(args.session)
    args = _resolve_alias_arguments(path, session, args, ("action_id",))
    action_id = _opaque(args.action_id.strip(), "action ID")
    arguments = _parse_json_object(args.arguments, "--arguments")
    with _v2_request_lock(path):
        batch_id = _persist_batch_for_action(
            path, session, action_id, arguments,
        )
        intent = _batch_intent(
            _load_v2_client_state(path, session), batch_id,
        )
        try:
            disposition, warning, exit_code = _submit_persisted_batch(
                path, session, batch_id,
            )
        except Exception:
            disposition = _batch_disposition(
                session, batch_id, "receipt_first",
            )
            warning = (
                "local recovery state became unavailable after persistence; "
                "resolve this batch by receipt before any retry"
            )
            exit_code = 2
    if _json_requested(args):
        _print_v2_json(disposition)
    else:
        _render(_render_disposition(disposition, intent))
    if warning:
        print(warning, file=sys.stderr)
    receipt = disposition["receipt"]
    if isinstance(receipt, dict) and receipt["receipt_state"] == "ambiguous":
        print("Ambiguous is terminal; never replay this batch.", file=sys.stderr)
    return exit_code


# ---------------------------------------------------------------------------
# L2 intent surface: `just do "ORDER; ORDER; ..."`.
#
# An order is the agent's own words -- `u1 found_city London`, `c1 build
# Warriors`, `research set_goal Currency`, or a bare action alias `a7`.  Every
# word of it is resolved *client-side* against the already-persisted
# `.v2-state` catalog for the newest revision this seat knows; the wire still
# carries nothing but the server-issued opaque `action_id` the descriptor
# named.  Nothing is guessed: an order that does not select exactly one cached
# capability is refused by name, with the exact enumeration command that would
# make it resolvable, and no order in the batch is sent.
# ---------------------------------------------------------------------------


V2_MAX_ORDERS = 8
V2_MAX_ORDER_WORDS = 12
V2_ACTION_FAMILIES = frozenset({
    "city", "diplomacy", "economy", "government", "phase", "player",
    "pregame", "research", "spaceship", "unit",
})
ORDER_COORDINATE_RE = re.compile(r"^(-?[0-9]{1,4}),(-?[0-9]{1,4})$")


class _OrderUnresolved(Exception):
    """One order named no single cached capability; the batch must not run."""

    def __init__(self, reason: str, actor_id: str = "") -> None:
        super().__init__(reason)
        self.reason = reason
        self.actor_id = actor_id


def _parse_orders(text: Any) -> list[str]:
    if not isinstance(text, str):
        raise PlayerError("just do needs one quoted, semicolon-separated string")
    orders = [part.strip() for part in text.split(";")]
    orders = [order for order in orders if order]
    if not orders:
        raise PlayerError(
            "just do needs at least one order, for example "
            '`just do "u1 found_city London"`'
        )
    if len(orders) > V2_MAX_ORDERS:
        raise PlayerError(
            f"just do accepts 1 through {V2_MAX_ORDERS} orders; "
            f"this line has {len(orders)}"
        )
    for order in orders:
        if len(order.split()) > V2_MAX_ORDER_WORDS:
            raise PlayerError(
                f"order {order!r} has more than {V2_MAX_ORDER_WORDS} words"
            )
    return orders


def _order_pool(state: dict[str, Any]) -> list[dict[str, Any]]:
    """Compact every cached descriptor that still names the newest revision."""
    revision = state["last_revision"]
    return [
        _compact_legal_action(descriptor)
        for descriptor in state["actions"].values()
        if descriptor.get("state_revision") == revision
    ]


def _order_actor(compact: dict[str, Any]) -> str:
    subject = compact["subject"]
    actor = subject.get("actor") if isinstance(subject, dict) else None
    identifier = actor.get("id") if isinstance(actor, dict) else None
    return identifier if isinstance(identifier, str) else ""


def _order_operation(compact: dict[str, Any]) -> str:
    subject = compact["subject"]
    operation = subject.get("operation") if isinstance(subject, dict) else None
    return operation if isinstance(operation, str) else ""


def _order_verbs(compact: dict[str, Any]) -> set[str]:
    """Name every word this cached action answers to, as it advertises itself.

    Only what the descriptor carries is accepted -- its public kind, the tail
    of that kind, its operation, and the `kind/operation` form the compact
    renderer prints.  No synonym is invented, so a verb that resolves is a
    verb the agent read on a catalog page.
    """
    kind = compact["kind"]
    tail = kind.split(".", 1)[-1]
    verbs = {kind, tail}
    operation = _order_operation(compact)
    if operation:
        verbs.update((operation, f"{kind}/{operation}", f"{tail}/{operation}"))
    return {verb.casefold() for verb in verbs}


def _order_target_keys(token: str) -> tuple[str, ...]:
    """Return the target keys a coordinate word could name, or ()."""
    tile = TILE_ALIAS_RE.fullmatch(token)
    if tile is not None:
        x, y = int(tile.group(1)), int(tile.group(2))
    else:
        plain = ORDER_COORDINATE_RE.fullmatch(token)
        if plain is None:
            return ()
        x, y = int(plain.group(1)), int(plain.group(2))
    return (f"T({x},{y})", f"@{x},{y}")


_ORDER_BAD: Any = object()


def _order_value(specification: Any, text: str) -> Any:
    """Coerce one order word to the type this action's own schema declares."""
    if not isinstance(specification, dict):
        return text
    choices = specification.get("enum")
    if isinstance(choices, list) and choices:
        for choice in choices:
            # Both spellings resolve: the JSON literal the catalog prints and
            # the human word `just do` has always accepted.
            if text.casefold() in {
                _scalar(choice).casefold(), _json_literal(choice).casefold(),
            }:
                return choice
        return _ORDER_BAD
    declared = specification.get("type")
    if declared == "integer":
        if re.fullmatch(r"-?(?:0|[1-9][0-9]{0,9})", text) is None:
            return _ORDER_BAD
        return int(text)
    if declared == "number":
        try:
            return float(text)
        except ValueError:
            return _ORDER_BAD
    if declared == "boolean":
        lowered = text.casefold()
        if lowered in {"true", "yes", "on", "1"}:
            return True
        if lowered in {"false", "no", "off", "0"}:
            return False
        return _ORDER_BAD
    return text


def _order_properties(compact: dict[str, Any]) -> tuple[
    dict[str, Any], list[str],
]:
    schema = compact["argument_schema"]
    properties = schema.get("properties") if isinstance(schema, dict) else None
    if not isinstance(properties, dict) or not properties:
        return {}, []
    declared = schema.get("required")
    required = [
        name for name in (declared if isinstance(declared, list) else [])
        if name in properties
    ]
    return properties, required


def _order_arguments(
    compact: dict[str, Any], values: list[str],
) -> dict[str, Any] | None:
    """Bind the order's remaining words to this action's own argument schema."""
    properties, required = _order_properties(compact)
    if not properties:
        return {} if not values else None
    names = required + [name for name in properties if name not in required]
    if not len(required) <= len(values) <= len(names):
        return None
    arguments: dict[str, Any] = {}
    for name, text in zip(names, values):
        converted = _order_value(properties[name], text)
        if converted is _ORDER_BAD:
            return None
        arguments[name] = converted
    return arguments


def _order_discriminators(compact: dict[str, Any]) -> set[str]:
    """Name the words that identify one argument-free action among its peers."""
    words = {compact["label"].casefold()}
    target_key = _action_target_key(compact)
    if target_key:
        words.add(target_key.casefold())
    target = compact["target"]
    if isinstance(target, dict):
        name = target.get("name")
        if isinstance(name, str) and name:
            words.add(name.casefold())
    subject = compact["subject"]
    if isinstance(subject, dict):
        for key, value in subject.items():
            if key in _LEGAL_SUBJECT_RESERVED:
                continue
            words.add(_named(value).casefold())
    return words


def _order_match(
    compact: dict[str, Any], values: list[str],
) -> dict[str, Any] | None:
    arguments = _order_arguments(compact, values)
    if arguments is not None:
        return arguments
    properties, _required = _order_properties(compact)
    if properties or not values:
        return None
    # An action that takes no arguments is selected by what distinguishes it
    # from its siblings: its label, its named target, or a subject value the
    # catalog page already showed.
    if " ".join(values).casefold() in _order_discriminators(compact):
        return {}
    return None


def _default_arguments(compact: dict[str, Any]) -> dict[str, Any] | None:
    """Fill an action whose required arguments have exactly one legal value."""
    properties, required = _order_properties(compact)
    arguments: dict[str, Any] = {}
    for name in required:
        choices = properties[name].get("enum") if isinstance(
            properties[name], dict,
        ) else None
        if not isinstance(choices, list) or len(choices) != 1:
            return None
        arguments[name] = choices[0]
    return arguments


def _order_resolution(
    compact: dict[str, Any], text: str, arguments: dict[str, Any],
) -> dict[str, Any]:
    return {
        "order": text,
        "action_id": compact["action_id"],
        "kind": compact["kind"],
        "operation": _order_operation(compact),
        "label": compact["label"],
        "actor_id": _order_actor(compact),
        "target_key": _action_target_key(compact),
        "arguments": arguments,
    }


def _resolve_order(
    state: dict[str, Any], path: Path, text: str,
) -> dict[str, Any]:
    """Resolve one order against the cache only; never touch the network."""
    tokens = text.split()
    pool = _order_pool(state)
    first = tokens[0]
    rest = tokens[1:]
    actor_id = ""
    verb = ""
    if ACTION_ALIAS_RE.fullmatch(first) is not None:
        # A bare alias already names one exact capability.
        action_id = _expand_action_alias(state, first, path)
        pool = [item for item in pool if item["action_id"] == action_id]
        if not pool:
            raise _OrderUnresolved(
                f"{first} names an action this seat no longer holds"
            )
        values = rest
    else:
        if (
            ENTITY_ALIAS_RE.fullmatch(first) is not None
            or ACTOR_ID_RE.fullmatch(first) is not None
        ):
            actor_id = _expand_alias(state, first, path)
            if ACTOR_ID_RE.fullmatch(actor_id) is None:
                raise _OrderUnresolved(
                    f"{first} is not a unit, city, or player this seat can act as"
                )
            if not rest:
                raise _OrderUnresolved(
                    f"{first} names an actor but no verb; write "
                    f"`{first} <verb> [arguments]`",
                    actor_id,
                )
            verb, rest = rest[0], rest[1:]
            pool = [item for item in pool if _order_actor(item) == actor_id]
            if not pool:
                raise _OrderUnresolved(
                    f"no cached action belongs to {first}", actor_id,
                )
        elif first.casefold() in V2_ACTION_FAMILIES and rest:
            family = first.casefold()
            verb, rest = rest[0], rest[1:]
            pool = [
                item for item in pool
                if item["kind"].split(".", 1)[0] == family
            ]
            if not pool:
                raise _OrderUnresolved(f"no cached {family} action")
        else:
            verb, rest = first, rest
        selector = verb.casefold()
        pool = [item for item in pool if selector in _order_verbs(item)]
        if not pool:
            raise _OrderUnresolved(
                f"no cached action advertises the verb `{verb}`", actor_id,
            )
        values = rest
        if values:
            keys = _order_target_keys(values[0])
            if keys:
                narrowed = [
                    item for item in pool
                    if _action_target_key(item) in keys
                ]
                if not narrowed:
                    raise _OrderUnresolved(
                        f"no cached `{verb}` action targets {values[0]}",
                        actor_id,
                    )
                pool, values = narrowed, values[1:]
    matches = [
        (item, arguments)
        for item, arguments in (
            (item, _order_match(item, values)) for item in pool
        )
        if arguments is not None
    ]
    if not matches:
        raise _OrderUnresolved(
            f"no cached action takes those arguments; "
            f"{len(pool)} candidate(s) matched the verb",
            actor_id,
        )
    if len(matches) > 1:
        named = " ".join(
            alias for alias in (
                _alias_map(state).get(item["action_id"], "")
                for item, _arguments in matches[:8]
            ) if alias
        )
        raise _OrderUnresolved(
            f"{len(matches)} cached actions match; name exactly one by its "
            f"alias{f': {named}' if named else ''}",
            actor_id,
        )
    compact, arguments = matches[0]
    return _order_resolution(compact, text, arguments)


def _rebind_order(
    state: dict[str, Any], resolved: dict[str, Any],
) -> dict[str, Any] | None:
    """Re-point one already-resolved order at the newest revision's handle."""
    matches = [
        item for item in _order_pool(state)
        if _order_actor(item) == resolved["actor_id"]
        and item["kind"] == resolved["kind"]
        and _order_operation(item) == resolved["operation"]
        and _action_target_key(item) == resolved["target_key"]
        and item["label"] == resolved["label"]
    ]
    if len(matches) != 1:
        return None
    return _order_resolution(matches[0], resolved["order"], resolved["arguments"])


def _order_enumeration_command(
    path: Path, state: dict[str, Any], actor_id: str,
) -> str:
    """Name the drain that would make this order resolvable, in the agent's
    own alias dialect when one is already assigned."""
    return _alias_refresh_command(
        path, _alias_map(state).get(actor_id, actor_id) if actor_id else "",
    )


def _unresolved_report(
    path: Path,
    state: dict[str, Any],
    outcomes: list[tuple[str, dict[str, Any] | None, str, str]],
) -> str:
    """Say which orders resolved, which did not, and what to run next."""
    revision = state["last_revision"]
    label = "no revision" if revision is None else _revision_label(revision)
    failed = sum(1 for _text, resolved, _reason, _actor in outcomes if resolved is None)
    lines = [
        f"{failed} of {len(outcomes)} orders did not resolve against the "
        f"cached {label} catalog; nothing was sent"
    ]
    remedies: list[str] = []
    for index, (text, resolved, reason, actor) in enumerate(outcomes, start=1):
        if resolved is not None:
            lines.append(
                f"  {index} resolved    {text}  ->  {resolved['kind']} "
                f"{resolved['label']}"
            )
            continue
        lines.append(f"  {index} unresolved  {text}  --  {reason}")
        remedy = _order_enumeration_command(path, state, actor)
        if remedy not in remedies:
            remedies.append(remedy)
    lines.extend(f"enumerate with: {remedy}" for remedy in remedies)
    return "\n".join(lines)


def _resolve_orders(
    state: dict[str, Any], path: Path, orders: list[str],
) -> list[dict[str, Any]]:
    """Resolve every order up front; refuse the whole batch if any cannot be."""
    outcomes: list[tuple[str, dict[str, Any] | None, str, str]] = []
    for text in orders:
        try:
            outcomes.append((text, _resolve_order(state, path, text), "", ""))
        except _OrderUnresolved as exc:
            outcomes.append((text, None, exc.reason, exc.actor_id))
        except PlayerError as exc:
            outcomes.append((text, None, str(exc), ""))
    if any(resolved is None for _text, resolved, _reason, _actor in outcomes):
        raise PlayerError(_unresolved_report(path, state, outcomes))
    return [resolved for _text, resolved, _reason, _actor in outcomes]


def _drain_legal_unlocked(
    path: Path, session: dict[str, Any], *, actor_id: str = "",
) -> dict[str, Any] | None:
    """Enumerate one catalog into the local cache, holding no new lock.

    Callers already hold this seat's request lock -- the advisory lock is not
    reentrant -- so this is the internal drain that the intent commands use to
    obtain fresh capabilities without printing a catalog into agent context.
    """
    query = (
        urllib.parse.urlencode({"actor_id": actor_id}) if actor_id else ""
    )
    cursor = ""
    seen: set[str] = set()
    revision: dict[str, Any] | None = None
    for _page in range(V2_LEGAL_DRAIN_MAX_PAGES):
        value = _read_legal_page(
            path, session, query, cursor=cursor,
            actor_id=actor_id, target_id="",
        )
        revision = value["state_revision"]
        cursor = value["page"]["next_cursor"] or ""
        if not cursor:
            return revision
        if cursor in seen:
            raise PlayerError("the legal catalog repeated a cursor")
        seen.add(cursor)
        query = urllib.parse.urlencode({"cursor": cursor})
    raise PlayerError("the legal catalog exceeded the safe 512-page drain limit")


def _refresh_orders(
    path: Path, session: dict[str, Any], pending: list[dict[str, Any]],
) -> None:
    """Re-enumerate exactly the catalogs the not-yet-sent orders name."""
    drained: set[str] = set()
    for resolved in pending:
        actor_id = resolved["actor_id"]
        if actor_id in drained:
            continue
        drained.add(actor_id)
        _drain_legal_unlocked(path, session, actor_id=actor_id)


def _order_receipt_ok(disposition: dict[str, Any]) -> bool:
    receipt = disposition["receipt"]
    return (
        isinstance(receipt, dict)
        and receipt["receipt_state"] in {"accepted", "applied"}
    )


def command_do(args: argparse.Namespace) -> int:
    path, session = _v2_session(args.session)
    orders = _parse_orders(getattr(args, "orders", ""))
    keep_going = bool(getattr(args, "continue_on_error", False))
    lines: list[str] = []
    records: list[dict[str, Any]] = []
    exit_code = 0
    applied = 0
    with _v2_request_lock(path):
        state = _load_v2_client_state(path, session)
        pending = _resolve_orders(state, path, orders)
        bound = state["last_revision"]
        stopped = ""
        # An outcome this client has already been told is never discarded: the
        # loop records every receipt line as it is produced, and a failure
        # anywhere below is turned into one more rendered line rather than an
        # exception that would take the applied batch_ids with it.
        while pending:
            resolved = pending.pop(0)
            batch_id = ""
            try:
                batch_id = _persist_batch_for_action(
                    path, session, resolved["action_id"],
                    resolved["arguments"],
                )
                disposition, warning, code = _submit_persisted_batch(
                    path, session, batch_id,
                )
            except (PlayerError, V2ResponseError) as exc:
                lines.append(f"{resolved['order']} → not sent: {exc}")
                stopped = (
                    f"stopped after order {len(records)}"
                    + (
                        f"; resolve {batch_id} with "
                        f"`just receipt --batch_id {batch_id}`"
                        if batch_id else ""
                    )
                )
                exit_code = max(exit_code, 2)
                break
            records.append({
                "order": resolved["order"],
                "action_id": resolved["action_id"],
                "arguments": resolved["arguments"],
                "disposition": disposition,
            })
            lines.extend(_render_disposition(disposition, resolved["order"]))
            if warning:
                print(warning, file=sys.stderr)
            receipt = disposition["receipt"]
            # The summary must never contradict the receipt printed above it:
            # take the newest revision this command has actually been shown.
            if isinstance(receipt, dict) and (
                bound is None
                or _revision_order(receipt["state_revision"])
                > _revision_order(bound)
            ):
                bound = receipt["state_revision"]
            if _order_receipt_ok(disposition):
                applied += 1
            else:
                exit_code = max(exit_code, code or 2)
                if not keep_going:
                    stopped = (
                        f"stopped after order {len(records)}; "
                        f"{len(pending)} not sent "
                        "(pass --continue-on-error to keep going)"
                    )
                    break
            if not pending or not isinstance(receipt, dict):
                continue
            if receipt["state_revision"] == state["last_revision"]:
                continue
            # The order that just landed moved the game on, so every remaining
            # handle is a stale capability.  Re-enumerate exactly what the
            # remaining orders name and re-bind them; never send a handle this
            # client already knows is expired.
            try:
                _refresh_orders(path, session, pending)
            except (PlayerError, V2ResponseError) as exc:
                lines.append(f"could not re-enumerate the remaining orders: {exc}")
                stopped = (
                    f"stopped after order {len(records)}; "
                    f"{len(pending)} not sent"
                )
                exit_code = max(exit_code, 2)
                break
            state = _load_v2_client_state(path, session)
            if state["last_revision"] is not None and (
                bound is None
                or _revision_order(state["last_revision"])
                > _revision_order(bound)
            ):
                bound = state["last_revision"]
            rebound = [_rebind_order(state, item) for item in pending]
            if any(item is None for item in rebound):
                stopped = (
                    f"{_revision_label(bound)} no longer offers "
                    + ", ".join(
                        item["order"] for item, fresh
                        in zip(pending, rebound) if fresh is None
                    )
                    + "; re-read the actor and re-issue those orders"
                )
                exit_code = max(exit_code, 2)
                break
            pending = [item for item in rebound if item is not None]
    summary = f"{applied}/{len(orders)} applied"
    if bound is not None:
        summary += f" {_revision_label(bound)}"
    lines.append(summary)
    if stopped:
        lines.append(stopped)
    if _json_requested(args):
        _print_v2_json({
            "schema_version": 1,
            "command": "do",
            "orders": records,
            "requested": len(orders),
            "applied": applied,
            "state_revision": bound,
            "stopped": stopped or None,
        })
    else:
        _render(lines)
    return exit_code


def _get_receipt_response(
    session: dict[str, Any], batch_id: str,
) -> JSONResponse:
    return _v2_response(
        "GET", _v2_url(session, f"/receipts/{batch_id}"), session, timeout=10,
    )


def command_receipt(args: argparse.Namespace) -> int:
    path, session = _v2_session(args.session)
    batch_id = _opaque(args.batch_id.strip(), "batch ID")
    response = _get_receipt_response(session, batch_id)
    if not 200 <= response.status < 300:
        _raise_validated_v2_error(response)
    receipt = _validate_receipt(response.value, session, batch_id=batch_id)
    state = _load_v2_client_state(path, session)
    intent = _batch_intent(state, batch_id)
    _remember_receipt(path, state, receipt)
    _mirror_receipt(path, receipt, "receipt")
    if _json_requested(args):
        _print_v2_json(receipt)
    else:
        _render(_render_receipt(receipt, intent))
    if receipt["receipt_state"] == "ambiguous":
        print("Ambiguous is terminal; never replay this batch.", file=sys.stderr)
    return 0


def _missing_accepted_receipt(
    session: dict[str, Any], cached: dict[str, Any], batch_id: str,
) -> dict[str, Any]:
    """Terminalize an accepted command whose authoritative receipt vanished."""
    revision = _validate_revision(cached["state_revision"])
    return _validate_receipt({
        "schema_version": 2,
        "control_protocol": FULL_CONTROL_V2,
        "game_id": session["game_id"],
        "agent_id": session["agent_id"],
        "batch_id": batch_id,
        "receipt_state": "ambiguous",
        "idempotent": cached["idempotent"],
        "state_revision": revision,
        "error": {
            "schema_version": 2,
            "control_protocol": FULL_CONTROL_V2,
            "error": {
                "code": "action_outcome_ambiguous",
                "message": (
                    "the server previously accepted this batch but no longer "
                    "has its authoritative receipt; replay is unsafe"
                ),
                "retryable": False,
                "details": {},
            },
            "state_revision": revision,
        },
        "observation": None,
    }, session, batch_id=batch_id)


def _command_retry_locked(
    args: argparse.Namespace, path: Path, session: dict[str, Any],
) -> int:
    batch_id = _opaque(args.batch_id.strip(), "batch ID")
    state = _load_v2_client_state(path, session)
    if batch_id not in state["batches"]:
        raise PlayerError(f"no persisted command batch {batch_id!r}")
    intent = _batch_intent(state, batch_id)

    def emit_receipt(value: dict[str, Any]) -> None:
        if _json_requested(args):
            _print_v2_json(value)
        else:
            _render(_render_receipt(value, intent))

    cached = state["receipts"].get(batch_id)
    accepted: dict[str, Any] | None = None
    if cached is not None:
        receipt = _validate_receipt(cached, session, batch_id=batch_id)
        if receipt["receipt_state"] in {"applied", "rejected", "ambiguous"}:
            emit_receipt(receipt)
            if receipt["receipt_state"] == "ambiguous":
                print(
                    "Ambiguous is terminal; never replay this batch.",
                    file=sys.stderr,
                )
            return 0
        accepted = receipt
    deadline = time.monotonic() + 30.0
    while True:
        response = _get_receipt_response(session, batch_id)
        if 200 <= response.status < 300:
            receipt = _validate_receipt(
                response.value, session, batch_id=batch_id,
            )
            _remember_receipt(path, state, receipt)
            _mirror_receipt(path, receipt, "retry")
            if receipt["receipt_state"] == "accepted" and time.monotonic() < deadline:
                accepted = receipt
                time.sleep(0.25)
                continue
            emit_receipt(receipt)
            if receipt["receipt_state"] == "ambiguous":
                print("Ambiguous is terminal; never replay this batch.", file=sys.stderr)
            return 0
        error = _validate_error(response.value)
        if response.status != 404 or error["error"]["code"] != "invalid_request":
            raise V2ResponseError(response.status, error)
        if accepted is not None:
            receipt = _missing_accepted_receipt(session, accepted, batch_id)
            _remember_receipt(path, state, receipt)
            _mirror_receipt(path, receipt, "retry")
            emit_receipt(receipt)
            print(
                "The accepted receipt disappeared. Its outcome is ambiguous "
                "and terminal; never replay this batch.",
                file=sys.stderr,
            )
            return 0
        disposition, warning, exit_code = _submit_persisted_batch(
            path, session, batch_id,
        )
        if _json_requested(args):
            _print_v2_json(disposition)
        else:
            _render(_render_disposition(disposition, intent))
        if warning:
            print(warning, file=sys.stderr)
        return exit_code


def command_retry(args: argparse.Namespace) -> int:
    # The seat is resolved exactly once: a workspace that gains a second seat
    # between two resolutions would lock one and refuse the other.
    path, session = _v2_session(args.session)
    with _v2_request_lock(path):
        return _command_retry_locked(args, path, session)


def _local_wait_response(
    session: dict[str, Any],
    wake_reason: str,
    health: dict[str, Any],
    revision: dict[str, Any] | None,
) -> dict[str, Any]:
    return {
        "schema_version": 2,
        "control_protocol": FULL_CONTROL_V2,
        "game_id": session["game_id"],
        "agent_id": session["agent_id"],
        "wake_reason": wake_reason,
        "health": health,
        "state_revision": revision,
    }


def _legacy_wait_value(
    path: Path,
    session: dict[str, Any],
    args: argparse.Namespace,
    *,
    until: str,
    baseline: dict[str, Any] | None,
) -> dict[str, Any]:
    """Correct local fallback for supervisors predating private /wait."""
    deadline = time.monotonic() + args.wait_s
    while True:
        health_response = _v2_response(
            "GET", _v2_url(session, "/health"), session, timeout=10,
        )
        if not 200 <= health_response.status < 300:
            _raise_validated_v2_error(health_response)
        health = _validate_health(health_response.value, session)
        if health["game_state"] in TERMINAL_STATES:
            return _local_wait_response(
                session, "game_terminal", health, None,
            )
        phase = health["phase"]
        if (
            until == "phase"
            and isinstance(phase, dict)
            and phase["active"] is True
            and phase["state"] == "awaiting_agent"
            and health["observation_available"] is True
        ):
            return _local_wait_response(
                session, "phase_active", health, None,
            )
        revision = None
        if until == "revision" and health["observation_available"] is True:
            state_response = _v2_response(
                "GET", _v2_url(session, "/state")
                + "?section=overview&limit=16",
                session, timeout=10,
            )
            if not 200 <= state_response.status < 300:
                _raise_validated_v2_error(state_response)
            overview = _validate_page(
                state_response.value, session, legal=False,
            )
            revision = overview["state_revision"]
            cached = _load_v2_client_state(path, session)
            _remember_page(path, cached, overview, legal=False)
            _mirror_page(path, cached, overview, "wait")
            assert baseline is not None
            if revision["state_token"] != baseline["state_token"]:
                return _local_wait_response(
                    session, "revision_changed", health, revision,
                )
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return _local_wait_response(
                session, "timeout", health, revision,
            )
        time.sleep(min(args.poll_s, remaining))


def _wait_value(
    path: Path, session: dict[str, Any], args: argparse.Namespace,
) -> dict[str, Any]:
    """Block until the seat is wanted again, returning the validated wake.

    This is the whole of `just wait` minus its printing, so `turn --end
    --await` blocks on exactly the same contract the standalone command does.
    """
    if not 0 <= args.wait_s <= 300:
        raise PlayerError("wait-s must be in [0, 300]")
    if not 0.05 <= args.poll_s <= 30:
        raise PlayerError("poll-s must be in [0.05, 30]")
    state = _load_v2_client_state(path, session)
    baseline = state["last_revision"]
    until = getattr(args, "until", "phase")
    if until not in {"phase", "revision"}:
        raise PlayerError("wait --until must be phase or revision")
    if until == "revision" and baseline is None:
        raise PlayerError(
            "wait --until revision requires a previously validated state page"
        )
    params: dict[str, str] = {
        "wait_s": f"{args.wait_s:g}", "until": until,
    }
    if until == "revision":
        assert baseline is not None
        params["after_state_token"] = baseline["state_token"]
    response = _v2_response(
        "GET",
        _v2_url(session, "/wait") + "?" + urllib.parse.urlencode(params),
        session,
        timeout=max(10.0, args.wait_s + 10.0),
    )
    if response.status == 404 and (
        set(response.value) == {"error"}
        and isinstance(response.value["error"], str)
    ):
        return _legacy_wait_value(
            path, session, args, until=until, baseline=baseline,
        )
    if not 200 <= response.status < 300:
        _raise_validated_v2_error(response)
    return _validate_wait_response(
        response.value,
        session,
        until=until,
        after_state_token=(
            None if baseline is None else baseline["state_token"]
        ),
    )


def command_wait(args: argparse.Namespace) -> int:
    path, session = _v2_session(args.session)
    _print_v2_json(_wait_value(path, session, args))
    return 0


# ---------------------------------------------------------------------------
# Lobby fast path: `just start --nation N --leader L --male|--female`.
#
# Doc SS5/P2.5: one line replaces the eight-call lobby ritual.  Names are
# resolved case-insensitively against the pregame catalogs this seat already
# holds (or fetches internally, once), and the two wire commands are the same
# `pregame.configure` and `pregame.set_ready` capabilities the catalog offers
# -- with the mandated re-enumeration between them, because configuring the
# seat bumps the revision and expires every outstanding handle.
# ---------------------------------------------------------------------------


V2_PREGAME_CATALOGS: dict[str, dict[str, Any]] = {
    "pregame_nations": {
        "prefix": "nation_",
        "mirror": ("cache", "nations.tsv"),
        "column": "nation",
        "label": "nation",
    },
    "pregame_styles": {
        "prefix": "style_",
        "mirror": ("cache", "styles.tsv"),
        "column": "style",
        "label": "style",
    },
}
V2_LEADER_MAX_BYTES = 47


def _mirror_text(session_path: Path, parts: tuple[str, ...]) -> str | None:
    """Read one mirror projection, or None when this seat has not written it."""
    try:
        return _read_private_text(
            _mirror_path(session_path).joinpath(*parts), "state mirror file",
        )
    except (PlayerError, OSError):
        return None


def _mirror_pregame_catalog(
    session_path: Path, section: str,
) -> list[dict[str, Any]]:
    """Re-read a static catalog from the mirror this client itself wrote.

    A row is used only when it is provably intact: the projection must be
    marked complete and every opaque ID must still be a full, untruncated ID.
    Anything else returns nothing, and the caller fetches the catalog again.
    """
    specification = V2_PREGAME_CATALOGS[section]
    text = _mirror_text(session_path, specification["mirror"])
    if text is None:
        return []
    lines = text.splitlines()
    if not any(
        line.startswith("# ") and line.endswith(" complete") for line in lines
    ):
        return []
    body = [line for line in lines if line and not line.startswith("#")]
    if len(body) < 2:
        return []
    columns = [cell.strip() for cell in body[0].split("\t")]
    if columns[:2] != ["id", specification["column"]]:
        return []
    items: list[dict[str, Any]] = []
    for line in body[1:]:
        cells = [cell.strip() for cell in line.split("\t")]
        if len(cells) != len(columns):
            return []
        row = dict(zip(columns, cells))
        identifier = row["id"]
        if (
            not identifier.startswith(specification["prefix"])
            or OPAQUE_ID_RE.fullmatch(identifier) is None
            or len(identifier) >= 64
        ):
            return []
        item: dict[str, Any] = {
            "id": identifier, "name": row[specification["column"]],
        }
        default_style = row.get("default_style_id", "")
        if default_style.startswith("style_") and len(default_style) < 64:
            item["default_style_id"] = default_style
        items.append(item)
    return items


def _fetch_state_section(
    path: Path, session: dict[str, Any], section: str,
) -> list[dict[str, Any]]:
    """Drain one state section into the cache and the mirror, printing none."""
    query = urllib.parse.urlencode({"section": section, "limit": 16})
    items: list[dict[str, Any]] = []
    for _page in range(V2_LEGAL_DRAIN_MAX_PAGES):
        response = _v2_response(
            "GET", _v2_url(session, "/state") + f"?{query}", session,
            timeout=10,
        )
        if not 200 <= response.status < 300:
            _raise_validated_v2_error(response)
        value = _validate_page(response.value, session, legal=False)
        state = _load_v2_client_state(path, session)
        _remember_page(path, state, value, legal=False)
        _mirror_page(path, state, value, "start")
        items.extend(value["page"]["items"])
        cursor = value["page"]["next_cursor"]
        if cursor is None:
            return items
        query = urllib.parse.urlencode({"cursor": cursor})
    raise PlayerError(f"the {section} catalog exceeded the safe drain limit")


def _pregame_catalog(
    path: Path, session: dict[str, Any], section: str,
) -> list[dict[str, Any]]:
    cached = _mirror_pregame_catalog(path, section)
    if cached:
        return cached
    return _fetch_state_section(path, session, section)


def _pregame_choice(
    items: list[dict[str, Any]], wanted: str, label: str,
) -> dict[str, Any]:
    """Resolve one catalog entry by name, case-insensitively and exactly."""
    matches = [
        item for item in items
        if isinstance(item, dict)
        and isinstance(item.get("name"), str)
        and item["name"].casefold() == wanted.casefold()
        and isinstance(item.get("id"), str)
    ]
    if len(matches) == 1:
        return matches[0]
    known = sorted(
        item["name"] for item in items
        if isinstance(item, dict) and isinstance(item.get("name"), str)
    )
    near = [name for name in known if wanted.casefold() in name.casefold()]
    shown = " ".join((near or known)[:12]) or "none were offered"
    if len(matches) > 1:
        raise PlayerError(
            f"{label} {wanted!r} is offered more than once; this lobby "
            f"catalog is ambiguous"
        )
    raise PlayerError(
        f"no {label} named {wanted!r} is offered; try one of: {shown}"
    )


def _check_pregame_arguments(
    compact: dict[str, Any], arguments: dict[str, Any],
) -> None:
    properties, required = _order_properties(compact)
    missing = [name for name in required if name not in arguments]
    unknown = [name for name in arguments if name not in properties]
    if missing or unknown:
        raise PlayerError(
            f"the enumerated {compact['kind']} action does not take the "
            "arguments this workspace builds; run "
            f"`just legal --kind {compact['kind']} --all --json` and submit "
            "it with `just batch`"
        )


def command_start(args: argparse.Namespace) -> int:
    path, session = _v2_session(args.session)
    nation = (getattr(args, "nation", "") or "").strip()
    leader = (getattr(args, "leader", "") or "").strip()
    style = (getattr(args, "style", "") or "").strip()
    male = bool(getattr(args, "male", False))
    female = bool(getattr(args, "female", False))
    if not nation or not leader:
        raise PlayerError(
            "just start needs --nation NAME and --leader NAME"
        )
    if male == female:
        raise PlayerError(
            "just start needs exactly one of --male or --female"
        )
    if len(leader.encode("utf-8")) > V2_LEADER_MAX_BYTES:
        raise PlayerError(
            f"--leader must be at most {V2_LEADER_MAX_BYTES} UTF-8 bytes"
        )
    lines: list[str] = []
    records: list[dict[str, Any]] = []
    exit_code = 0
    with _v2_request_lock(path):
        health = _turn_health(session)
        _mirror_health(path, health, "start")
        if health["game_state"] != "lobby":
            raise PlayerError(
                "just start configures a lobby seat; this game is "
                f"{health['game_state']} -- run `just turn`"
            )
        chosen = _pregame_choice(
            _pregame_catalog(path, session, "pregame_nations"),
            nation, "nation",
        )
        if style:
            style_id = _pregame_choice(
                _pregame_catalog(path, session, "pregame_styles"),
                style, "style",
            )["id"]
        else:
            style_id = chosen.get("default_style_id")
            if not isinstance(style_id, str) or not style_id:
                raise PlayerError(
                    f"nation {chosen['name']} carries no default style; pass "
                    "--style NAME (see `just state --section pregame_styles`)"
                )
        arguments = {
            "nation_id": chosen["id"],
            "leader_name": leader,
            "is_male": male,
            "style_id": style_id,
        }
        configure, _state = _resolve_kind_action(
            path, session, "pregame.configure",
            "this seat may already be ready -- run "
            "`just legal --kind pregame.set_ready --all` and withdraw "
            "readiness before configuring again",
        )
        _check_pregame_arguments(configure, arguments)
        batch_id = _persist_batch_for_action(
            path, session, configure["action_id"], arguments,
        )
        disposition, warning, exit_code = _submit_persisted_batch(
            path, session, batch_id,
        )
        records.append(disposition)
        lines.extend(_render_disposition(
            disposition,
            f"configure {chosen['name']} {leader} "
            f"{'male' if male else 'female'}",
        ))
        if warning:
            print(warning, file=sys.stderr)
        if not _order_receipt_ok(disposition):
            lines.append("not readied: the configuration was not accepted")
        else:
            # Configuring the seat bumps the revision, so the readiness
            # capability enumerated before it is now expired: re-enumerate
            # before naming it, exactly as the doc requires.
            _drain_legal_unlocked(path, session)
            ready, _fresh = _resolve_kind_action(
                path, session, "pregame.set_ready",
                "run `just legal --kind pregame.set_ready --all` once the "
                "lobby offers readiness, then `just batch` its action_id",
            )
            ready_arguments = _default_arguments(ready)
            if ready_arguments is None or ready_arguments.get("ready") is not True:
                raise PlayerError(
                    "the enumerated pregame.set_ready would withdraw "
                    "readiness rather than set it; this seat is already ready"
                )
            batch_id = _persist_batch_for_action(
                path, session, ready["action_id"], ready_arguments,
            )
            disposition, warning, exit_code = _submit_persisted_batch(
                path, session, batch_id,
            )
            records.append(disposition)
            lines.extend(_render_disposition(disposition, "set ready"))
            if warning:
                print(warning, file=sys.stderr)
    if _json_requested(args):
        _print_v2_json({
            "schema_version": 1,
            "command": "start",
            "nation": chosen["name"],
            "leader": leader,
            "is_male": male,
            "style_id": style_id,
            "dispositions": records,
        })
    else:
        _render(lines)
    return exit_code


# ---------------------------------------------------------------------------
# `just show`: read the L1 mirror, never the network.
#
# Every byte this prints was already written by a command that ingested a page
# for this seat, so a read can never widen what the seat knows past fog -- and
# it costs no server round trip and no wire budget.  The private `.v2-state`
# cache is deliberately not reachable from here: it is a sibling of the mirror
# directory, not a member of it.
# ---------------------------------------------------------------------------


V2_SHOW_FILES: dict[str, tuple[str, ...]] = {
    "header": ("state", "header.txt"),
    "overview": ("state", "overview.tsv"),
    "units": ("state", "units.tsv"),
    "cities": ("state", "cities.tsv"),
    "map": ("state", "map.txt"),
    "delta": ("state", "delta.md"),
    "nations": ("cache", "nations.tsv"),
    "styles": ("cache", "styles.tsv"),
    "governments": ("cache", "governments.tsv"),
}
V2_SHOW_ROW_FILES = ("units", "cities")
V2_SHOW_MAX_MATCHES = 200
SHOW_NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")


def _show_option_files(session_path: Path) -> list[str]:
    """List the per-actor option projections without following a symlink."""
    directory = _mirror_path(session_path) / "state" / "options"
    try:
        _destination, relative = _state_relative_path(directory)
        descriptor = _open_state_directory(relative.parts, create=False)
    except PlayerError:
        return []
    try:
        return sorted(
            name for name in os.listdir(descriptor)
            if name.endswith(".txt") and not name.startswith(".")
        )
    except OSError:
        return []
    finally:
        os.close(descriptor)


def _show_catalog(session_path: Path) -> list[tuple[str, tuple[str, ...]]]:
    entries = list(V2_SHOW_FILES.items())
    entries.extend(
        (f"options/{name[:-4]}", ("state", "options", name))
        for name in _show_option_files(session_path)
    )
    return entries


def _show_present(session_path: Path) -> list[tuple[str, tuple[str, ...], str]]:
    found = []
    for label, parts in _show_catalog(session_path):
        text = _mirror_text(session_path, parts)
        if text is not None:
            found.append((label, parts, text))
    return found


def _show_empty(session_path: Path) -> PlayerError:
    return PlayerError(
        "this seat has no local state mirror yet; run `just turn` (or "
        "`just state --section units`) once and the files appear under "
        f"{_mirror_path(session_path)}"
    )


def _show_default(session_path: Path) -> list[str]:
    present = _show_present(session_path)
    if not present:
        raise _show_empty(session_path)
    lines: list[str] = []
    for label, _parts, text in present:
        if label in {"header", "delta"}:
            lines.extend(text.splitlines())
            lines.append("")
    lines.append(
        "files: " + " ".join(label for label, _parts, _text in present)
    )
    lines.append("read one with `just show NAME`, search with `just show --grep PATTERN`")
    return lines


def _show_rows(session_path: Path, alias: str) -> list[str]:
    """Return the mirror rows that this alias names, from the tables."""
    lines: list[str] = []
    for label in V2_SHOW_ROW_FILES:
        text = _mirror_text(session_path, V2_SHOW_FILES[label])
        if text is None:
            continue
        for line in text.splitlines():
            first = line.split("\t", 1)[0].strip()
            if first == alias:
                lines.append(f"{label}: {line}")
    return lines


def _show_named(session_path: Path, name: str) -> list[str]:
    if SHOW_NAME_RE.fullmatch(name) is None:
        raise PlayerError(
            "just show takes one mirror file name or one entity alias, "
            "for example `just show units` or `just show u1`"
        )
    parts = V2_SHOW_FILES.get(name)
    if parts is not None:
        text = _mirror_text(session_path, parts)
        if text is None:
            raise PlayerError(
                f"this seat has no {name} projection yet; run `just turn` "
                "to write one"
            )
        return text.splitlines()
    lines: list[str] = []
    options = _mirror_text(session_path, ("state", "options", f"{name}.txt"))
    lines.extend(_show_rows(session_path, name))
    if options is not None:
        if lines:
            lines.append("")
        lines.extend(options.splitlines())
    if not lines:
        # A remedy must be runnable: `just legal --actor_id research --all`
        # cannot work, so only an alias-shaped name is offered that repair.
        if ENTITY_ALIAS_RE.fullmatch(name) is not None:
            raise PlayerError(
                f"the mirror holds nothing named {name}; run "
                f"`just legal --actor_id {name} --all` to write "
                f"state/options/{name}.txt, or `just show` to list what is there"
            )
        raise PlayerError(
            f"{name} is not a mirror file or an entity alias; the file names "
            f"are {' '.join(sorted(V2_SHOW_FILES))}, an alias looks like u1 "
            "or c1, and `just show` lists what this seat has written"
        )
    return lines


# A quantified group that is itself quantified is one catastrophic-back-
# tracking shape, but it is not the only one: `(a|aa)+$` overlaps by
# alternation instead and takes exponential time in the line length with no
# nested quantifier anywhere.  Enumerating shapes therefore cannot close the
# class, so the default search is a plain case-insensitive substring — which
# has no backtracking at all — and the regex engine is opt-in behind
# `--regex`, guarded by this pattern *and* by a wall-clock budget.  A wedged
# `just show` costs the seat its turn deadline, and the mirror files are the
# one surface the agent is told is free.
NESTED_QUANTIFIER_RE = re.compile(r"\([^()]*[*+}][^()]*\)\s*[*+{]")

# Whole-command budget for `--regex`.  One pathological pattern cannot be
# interrupted mid-`search`, so the budget is checked between lines: it bounds
# the damage to roughly one line's worth of backtracking rather than the whole
# mirror's, and the refusal names the flagless form that cannot backtrack.
V2_SHOW_GREP_BUDGET_S = 2.0


def _show_grep(
    session_path: Path, pattern: str, *, regex: bool = False,
) -> list[str]:
    if len(pattern) > 200:
        raise PlayerError("just show --grep takes a pattern of at most 200 characters")
    expression: re.Pattern[str] | None = None
    if regex:
        if NESTED_QUANTIFIER_RE.search(pattern) is not None:
            raise PlayerError(
                "just show --grep --regex refuses a quantifier applied to an "
                "already quantified group; drop --regex to search for the "
                "literal text, for example `just show --grep found_city`"
            )
        try:
            expression = re.compile(pattern, re.IGNORECASE)
        except re.error as exc:
            raise PlayerError(
                f"just show --grep pattern is invalid: {exc}; drop --regex to "
                f"search for the literal text `{pattern}`"
            ) from exc
    needle = pattern.casefold()
    present = _show_present(session_path)
    if not present:
        raise _show_empty(session_path)
    lines: list[str] = []
    truncated = False
    deadline = time.monotonic() + V2_SHOW_GREP_BUDGET_S
    for label, _parts, text in present:
        for number, line in enumerate(text.splitlines(), start=1):
            if expression is None:
                if needle not in line.casefold():
                    continue
            else:
                if time.monotonic() > deadline:
                    raise PlayerError(
                        "just show --grep --regex took too long; narrow the "
                        f"pattern, or drop --regex to search for the literal "
                        f"text `{pattern}`"
                    )
                if expression.search(line) is None:
                    continue
            if len(lines) >= V2_SHOW_MAX_MATCHES:
                truncated = True
                break
            lines.append(f"{label}:{number}: {line}")
        if truncated:
            break
    if not lines:
        lines.append(f"no mirror line matches {pattern!r}")
    elif truncated:
        lines.append(
            f"(stopped at {V2_SHOW_MAX_MATCHES} matches; narrow the pattern)"
        )
    return lines


def command_show(args: argparse.Namespace) -> int:
    """Read this seat's mirror files. This command never opens a socket."""
    path, _session = _v2_session(args.session)
    pattern = (getattr(args, "grep", "") or "").strip()
    name = (getattr(args, "name", "") or "").strip()
    if pattern and name:
        raise PlayerError(
            "just show takes either a name or --grep PATTERN, not both"
        )
    regex = bool(getattr(args, "regex", False))
    if regex and not pattern:
        raise PlayerError("just show --regex needs a --grep PATTERN to apply to")
    if pattern:
        selection = f"grep {pattern}"
        lines = _show_grep(path, pattern, regex=regex)
    elif name:
        selection = name
        lines = _show_named(path, name)
    else:
        selection = ""
        lines = _show_default(path)
    if _json_requested(args):
        _print_v2_json({
            "schema_version": 1,
            "command": "show",
            "selection": selection or None,
            "lines": lines,
        })
    else:
        _render(lines)
    return 0


def command_next(args: argparse.Namespace) -> int:
    _path, session = _load_session(args.session)
    if session.get("control_protocol", "strategic-v1") != "strategic-v1":
        raise PlayerError(
            "just next is strategic-v1 only; this full-control-v2 session "
            "uses `just health`, `just state`, and `just legal`"
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
            "uses `just batch` and durable receipts"
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
    positional = (getattr(args, "game_id_positional", None) or "").strip()
    option = (getattr(args, "game_id", None) or "").strip()
    if positional and option and positional != option:
        raise PlayerError("result received two different game IDs")
    game_id = _game_id(option or positional)
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

    def json_escape_hatch(command: argparse.ArgumentParser) -> None:
        """Keep the exact pre-rendering JSON one flag away, byte for byte."""
        command.add_argument(
            "--json", dest="json_output", action="store_true",
            help="print the full-fidelity JSON payload instead of text",
        )

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
    json_escape_hatch(join)
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

    health = commands.add_parser("health")
    health.add_argument("--session", default="")
    json_escape_hatch(health)
    health.set_defaults(handler=command_health)

    turn = commands.add_parser("turn")
    turn.add_argument("--session", default="")
    turn.add_argument(
        "--end", dest="end_phase", action="store_true",
        help="end this phase using the cached phase.end capability",
    )
    turn.add_argument(
        "--await", dest="await_phase", action="store_true",
        help="with --end: block until the next phase, then print its header",
    )
    turn.add_argument("--wait-s", type=float, default=120)
    turn.add_argument("--poll-s", type=float, default=1)
    turn.add_argument("--until", choices=("phase", "revision"), default="phase")
    json_escape_hatch(turn)
    turn.set_defaults(handler=command_turn)

    start = commands.add_parser("start")
    start.add_argument("--session", default="")
    start.add_argument("--nation", default="")
    start.add_argument("--leader", default="")
    start.add_argument("--style", default="")
    start.add_argument("--male", action="store_true")
    start.add_argument("--female", action="store_true")
    json_escape_hatch(start)
    start.set_defaults(handler=command_start)

    do = commands.add_parser("do")
    do.add_argument("--session", default="")
    do.add_argument(
        "--orders", default="",
        help='1..8 semicolon-separated orders, e.g. "u1 found_city London"',
    )
    do.add_argument(
        "--continue-on-error", dest="continue_on_error", action="store_true",
        help="keep issuing later orders after one is rejected",
    )
    json_escape_hatch(do)
    do.set_defaults(handler=command_do)

    show = commands.add_parser("show")
    show.add_argument("--session", default="")
    show.add_argument("name", nargs="?", default="")
    show.add_argument("--grep", default="")
    show.add_argument(
        "--regex", action="store_true",
        help="read --grep as a regular expression instead of literal text",
    )
    json_escape_hatch(show)
    show.set_defaults(handler=command_show)

    state = commands.add_parser("state")
    state.add_argument("--session", default="")
    state.add_argument("--section", default="")
    state.add_argument("--actor-id", default="")
    state.add_argument("--relation-id", default="")
    state.add_argument("--center-id", default="")
    state.add_argument("--radius", type=int)
    state.add_argument("--limit")
    state.add_argument("--cursor", default="")
    json_escape_hatch(state)
    state.set_defaults(handler=command_state)

    legal = commands.add_parser("legal")
    legal.add_argument("--session", default="")
    legal.add_argument("--actor-id", default="")
    legal.add_argument("--target-id", default="")
    legal.add_argument(
        "--limit",
        help=(
            "server page size 1..16, or compact result window 1..64 with "
            "--kind/--all"
        ),
    )
    legal.add_argument("--cursor", default="")
    legal.add_argument("--kind", default="")
    legal.add_argument("--all", dest="all_pages", action="store_true")
    legal.add_argument(
        "--offset", default="",
        help="compact match offset 0..8192; requires --kind/--all",
    )
    json_escape_hatch(legal)
    legal.set_defaults(handler=command_legal)

    batch = commands.add_parser("batch")
    batch.add_argument("--session", default="")
    batch.add_argument("--action-id", required=True)
    batch.add_argument("--arguments", default="{}")
    json_escape_hatch(batch)
    batch.set_defaults(handler=command_batch)

    receipt = commands.add_parser("receipt")
    receipt.add_argument("--session", default="")
    receipt.add_argument("--batch-id", required=True)
    json_escape_hatch(receipt)
    receipt.set_defaults(handler=command_receipt)

    retry = commands.add_parser("retry")
    retry.add_argument("--session", default="")
    retry.add_argument("--batch-id", required=True)
    json_escape_hatch(retry)
    retry.set_defaults(handler=command_retry)

    wait = commands.add_parser("wait")
    wait.add_argument("--session", default="")
    wait.add_argument("--wait-s", type=float, default=120)
    wait.add_argument("--poll-s", type=float, default=1)
    wait.add_argument("--until", choices=("phase", "revision"), default="phase")
    wait.set_defaults(handler=command_wait)

    result = commands.add_parser("result")
    result.add_argument("game_id_positional", nargs="?")
    result.add_argument("--game-id", default="")
    result.set_defaults(handler=command_result)
    return value


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        return int(args.handler(args))
    except V2ResponseError as exc:
        # A refusal is the most decision-relevant payload the agent ever
        # reads, so it is rendered like every success path.  `--json` keeps
        # the byte-identical wire payload for machine consumers.
        if _json_requested(args):
            _print_v2_json(exc.payload)
        else:
            _render(_render_error_payload(exc.payload))
        print(f"error: {exc}", file=sys.stderr)
        return 2
    except PlayerError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
