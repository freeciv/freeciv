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
    "votes", "research",
    "governments", "diplomacy",
    "diplomacy_clauses", "known_tiles", "map_tiles", "infrastructure",
    "cities",
    "city_sites", "units",
    "multipliers", "spaceship", "tombstones", "chat", "city_detail", "city_citizens",
    "city_build_choices", "city_worklist", "city_improvements",
    "city_worker_tasks", "tile_window", "city_governor",
}
V2_CITY_SECTIONS = {
    "city_detail", "city_citizens", "city_build_choices", "city_worklist",
    "city_improvements", "city_worker_tasks", "city_governor",
}
V2_TURN_SECTIONS = ("overview", "cities", "units", "research")
V2_TURN_PAGE_LIMIT = 16
V2_LEGAL_MATCH_LIMIT = 64
V2_LEGAL_DRAIN_MAX_PAGES = 512
V2_LEGAL_COMPACT_MAX_BYTES = 48 * 1024
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


def _empty_v2_client_state(session: dict[str, Any]) -> dict[str, Any]:
    return {
        "schema_version": 2,
        "game_id": session["game_id"],
        "agent_id": session["agent_id"],
        "last_revision": None,
        "actions": {},
        "pending_catalogs": {},
        "batches": {},
        "receipts": {},
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
    current_fields = legacy_fields | {"pending_catalogs"}
    legacy = set(value) == legacy_fields and value.get("schema_version") == 1
    current = set(value) == current_fields and value.get("schema_version") == 2
    if (
        not (legacy or current)
        or value.get("game_id") != session["game_id"]
        or value.get("agent_id") != session["agent_id"]
        or not isinstance(value.get("actions"), dict)
        or not isinstance(value.get("batches"), dict)
        or not isinstance(value.get("receipts"), dict)
        or current and not isinstance(value.get("pending_catalogs"), dict)
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


def _remember_page_unlocked(
    session_path: Path,
    state: dict[str, Any],
    page: dict[str, Any],
    *,
    legal: bool,
) -> None:
    revision = page["state_revision"]
    prior = state["last_revision"]
    if prior is None or _revision_order(revision) > _revision_order(prior):
        state["last_revision"] = revision
        state["actions"] = {}
        state["pending_catalogs"] = {}
    elif _revision_order(revision) == _revision_order(prior):
        if revision != prior:
            raise PlayerError("state token changed without a newer revision")
    else:
        # An older authenticated page can be displayed but can never revive an
        # expired action capability in local state.
        return
    if legal:
        public_page = page["page"]
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
                return
            if pending is None and all(
                state["actions"].get(action_id) == descriptor
                for action_id, descriptor in descriptors.items()
            ):
                # Idempotent replay of an already-promoted final page.
                _save_v2_client_state_unlocked(session_path, state)
                return
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
            state["pending_catalogs"].pop(catalog_id, None)
            _save_v2_client_state_unlocked(session_path, state)
            return
        for descriptor in public_page["items"]:
            action_id = descriptor["action_id"]
            existing = state["actions"].get(action_id)
            if existing is not None and existing != descriptor:
                raise PlayerError("one action ID described two different actions")
            state["actions"][action_id] = descriptor
    _save_v2_client_state_unlocked(session_path, state)


def _remember_page(
    session_path: Path,
    state: dict[str, Any],
    page: dict[str, Any],
    *,
    legal: bool,
) -> None:
    session = {
        "game_id": state["game_id"], "agent_id": state["agent_id"],
    }
    with _v2_state_lock(session_path):
        current = _load_v2_client_state_unlocked(session_path, session)
        try:
            _remember_page_unlocked(
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
        _save_v2_client_state_unlocked(session_path, current)
        state.clear()
        state.update(current)


def _print_v2_json(value: dict[str, Any]) -> None:
    print(json.dumps(value, sort_keys=True, separators=(",", ":")))


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

If join reports `full-control-v2`, use the v2 loop printed by join: inspect
authenticated state and legal actions, submit one opaque action, resolve its
receipt, and repeat. While health reports `game_state: lobby`, do not wait:
read `overview`, `pregame_nations`, `pregame_styles`, and `pregame_teams`;
optionally submit the enumerated `pregame.configure` and `pregame.set_team`
with an opaque team ID; refresh after each; then submit the enumerated
`pregame.set_ready` with `{{"ready":true}}`. The last external seat to become
ready starts the native game. For diplomacy, combine `overview.player.id` with a
`diplomacy[].relation_id`, exhaust every legal cursor, and read that meeting's
clauses with `just state --session SESSION_FILE --section diplomacy_clauses
--relation_id RELATION_ID`.
Build the complete deal, then use the enumerated desired acceptance action.
Never replay an ambiguous
acceptance. End your own active phase only with an enumerated `phase.end`
action. After `phase.end`, run `just wait --session SESSION_FILE`, then begin
the next actionable phase with `just turn`. Keep this same conversation active
and repeat the loop until the game is terminal; do not give a final answer or
stop merely because one turn completed. If a wait command itself fails, fix
that command and continue rather than treating the game as finished.

Use only the negotiated protocol's authenticated private state for decisions.
Never inspect parent directories or spectator data. Stop on completed, invalid,
failed, or cancelled. If GAME_ID is still a placeholder, or join fails, stop
and ask the user instead of inventing a game or retrying blindly.""")
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
        print(
            f"\nJoined a full-control-v2 session.\nSession file: {path}\n"
            f"Timing mode: {timing_mode}; {deadline}.\n"
            f"{evaluation_line}"
            "Do not use strategic `just next` or `just act`. Use these "
            "authenticated commands:\n"
            f"  just health --session {path}\n"
            f"  just turn --session {path}\n"
            f"  just state --session {path} --section pregame_nations\n"
            f"  just state --session {path} --section pregame_styles\n"
            f"  just state --session {path} --section pregame_teams\n"
            f"  just state --session {path} --section city_sites\n"
            f"  just legal --session {path}\n"
            f"  just batch --session {path} --action_id ACTION_ID "
            "--arguments '{}'\n"
            f"  just receipt --session {path} --batch_id BATCH_ID\n"
            f"  just wait --session {path}\n"
            "LOBBY FIRST: do not call wait while health says game_state=lobby. "
            "Read overview plus all three pregame catalogs, optionally execute "
            "the enumerated pregame.configure or pregame.set_team with exact "
            "opaque catalog IDs, refresh state and legal actions after each, "
            "then execute the enumerated "
            "pregame.set_ready with arguments {\"ready\":true}. Every external "
            "seat must join before readiness is accepted; the last ready action "
            "starts Freeciv. Once the game is running, begin each decision "
            "with `just turn`; it returns health, overview, cities, units, and "
            "research from one revision. Then use actor-scoped legal queries "
            "to exhaust one owned actor. Read "
            "city_sites for fog-safe city targets. Use "
            "actor plus target for an exact known tile. For diplomacy, use "
            "overview.player.id plus a diplomacy relation_id and exhaust its "
            "cursor before acting; read diplomacy_clauses with that exact "
            "--relation_id and build the complete deal before acceptance. "
            "Never replay an ambiguous acceptance. Execute only an "
            "enumerated opaque action at its exact revision, one command at a "
            "time. An ambiguous receipt is terminal and must never be replayed. "
            "Health last_phase_end reports only your seat; source=timeout "
            "confirms the supervisor auto-ended your phase. After phase.end, "
            "run just wait and then begin the next actionable phase with just "
            "turn. Keep this conversation active until the game is terminal; "
            "do not final-answer merely because one turn completed. A failed "
            "wait command is a harness error to correct, not a terminal game.",
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
    _path, session = _v2_session(args.session)
    response = _v2_response(
        "GET", _v2_url(session, "/health"), session, timeout=10,
    )
    if not 200 <= response.status < 300:
        _raise_validated_v2_error(response)
    value = _validate_health(response.value, session)
    _print_v2_json(value)
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


def _turn_next_commands(
    session_path: Path, pages: dict[str, dict[str, Any]],
) -> list[str]:
    commands = []
    for section in V2_TURN_SECTIONS:
        cursor = pages[section]["page"]["next_cursor"]
        if cursor is not None:
            commands.append(
                f"just state --session {session_path} --cursor {cursor}"
            )
    commands.extend((
        f"just legal --session {session_path} --kind phase.end --all",
        f"just legal --session {session_path} "
        "--kind research.set_target --all",
        f"just legal --session {session_path} "
        "--kind economy.set_rates --all",
        f"just legal --session {session_path} --actor_id ACTOR_ID",
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


def command_turn(args: argparse.Namespace) -> int:
    path, session = _v2_session(args.session)
    with _v2_request_lock(path):
        for attempt in range(2):
            health = _turn_health(session)
            if health["game_state"] in TERMINAL_STATES:
                _print_v2_json({
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
                        f"just state --session {path} --section overview",
                        f"just state --session {path} --section pregame_nations",
                        f"just state --session {path} --section pregame_styles",
                        f"just state --session {path} --section pregame_teams",
                        f"just legal --session {path}",
                    ]
                    status = "lobby"
                else:
                    next_commands = [f"just wait --session {path}"]
                    status = "not_ready"
                _print_v2_json({
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
                "next_commands": _turn_next_commands(path, pages),
            }
            _print_v2_json(result)
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
    elif actor_id or relation_id or center_id or radius is not None:
        raise PlayerError("state scope options are not valid for this section")
    return urllib.parse.urlencode(params)


def command_state(args: argparse.Namespace) -> int:
    path, session = _v2_session(args.session)
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
    _print_v2_json(value)
    return 0


def _legal_query(args: argparse.Namespace) -> str:
    cursor = args.cursor.strip()
    actor = args.actor_id.strip()
    target = args.target_id.strip()
    limit = args.limit
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
    _remember_page(path, state, value, legal=True)
    return value


def _compact_legal_action(descriptor: dict[str, Any]) -> dict[str, Any]:
    subject = descriptor["subject"]
    result = {
        "action_id": descriptor["action_id"],
        "kind": descriptor["kind"],
        "target": subject.get("target"),
        "argument_schema": descriptor["arguments_schema"],
    }
    probability = subject.get("probability")
    if isinstance(probability, dict) and probability != {
        "kind": "exact", "minimum_percent": 100, "maximum_percent": 100,
    }:
        result["probability"] = probability
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


def _command_legal_all(
    args: argparse.Namespace,
    path: Path,
    session: dict[str, Any],
    kind: str,
) -> int:
    if args.cursor.strip():
        raise PlayerError("legal --kind/--all starts a catalog; omit --cursor")
    query = _legal_query(args)
    cursor = ""
    actor_id = args.actor_id.strip()
    target_id = args.target_id.strip()
    revision = None
    catalog_total = None
    seen_cursors: set[str] = set()
    matched = 0
    compact_actions: list[dict[str, Any]] = []
    compact_bytes = 0
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
                if descriptor["kind"] != kind:
                    continue
                matched += 1
                if len(compact_actions) >= V2_LEGAL_MATCH_LIMIT:
                    continue
                compact = _compact_legal_action(descriptor)
                encoded_size = len(json.dumps(
                    compact, sort_keys=True, separators=(",", ":"),
                ).encode("utf-8"))
                if compact_bytes + encoded_size > V2_LEGAL_COMPACT_MAX_BYTES:
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
    _print_v2_json({
        "schema_version": 1,
        "command": "legal",
        "kind": kind,
        "state_revision": revision,
        "catalog_total": catalog_total,
        "pages_read": pages_read,
        "matched": matched,
        "shown": len(compact_actions),
        "truncated": len(compact_actions) < matched,
        "actions": compact_actions,
    })
    return 0


def command_legal(args: argparse.Namespace) -> int:
    path, session = _v2_session(args.session)
    kind = getattr(args, "kind", "").strip()
    all_pages = bool(getattr(args, "all_pages", False))
    if bool(kind) != all_pages:
        raise PlayerError("use --kind ACTION_KIND and --all together")
    if kind:
        if ACTION_KIND_RE.fullmatch(kind) is None:
            raise PlayerError("legal --kind must be an exact public action kind")
        return _command_legal_all(args, path, session, kind)
    query = _legal_query(args)
    value = _read_legal_page(
        path, session, query, cursor=args.cursor.strip(),
        actor_id=args.actor_id.strip(), target_id=args.target_id.strip(),
    )
    _print_v2_json(value)
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
            f"receipt first with `just receipt --session {path} --batch_id "
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
    action_id = _opaque(args.action_id.strip(), "action ID")
    arguments = _parse_json_object(args.arguments, "--arguments")
    with _v2_request_lock(path):
        batch_id = _persist_batch_for_action(
            path, session, action_id, arguments,
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
    _print_v2_json(disposition)
    if warning:
        print(warning, file=sys.stderr)
    receipt = disposition["receipt"]
    if isinstance(receipt, dict) and receipt["receipt_state"] == "ambiguous":
        print("Ambiguous is terminal; never replay this batch.", file=sys.stderr)
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
    _remember_receipt(path, state, receipt)
    _print_v2_json(receipt)
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


def _command_retry_locked(args: argparse.Namespace) -> int:
    path, session = _v2_session(args.session)
    batch_id = _opaque(args.batch_id.strip(), "batch ID")
    state = _load_v2_client_state(path, session)
    if batch_id not in state["batches"]:
        raise PlayerError(f"no persisted command batch {batch_id!r}")
    cached = state["receipts"].get(batch_id)
    accepted: dict[str, Any] | None = None
    if cached is not None:
        receipt = _validate_receipt(cached, session, batch_id=batch_id)
        if receipt["receipt_state"] in {"applied", "rejected", "ambiguous"}:
            _print_v2_json(receipt)
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
            if receipt["receipt_state"] == "accepted" and time.monotonic() < deadline:
                accepted = receipt
                time.sleep(0.25)
                continue
            _print_v2_json(receipt)
            if receipt["receipt_state"] == "ambiguous":
                print("Ambiguous is terminal; never replay this batch.", file=sys.stderr)
            return 0
        error = _validate_error(response.value)
        if response.status != 404 or error["error"]["code"] != "invalid_request":
            raise V2ResponseError(response.status, error)
        if accepted is not None:
            receipt = _missing_accepted_receipt(session, accepted, batch_id)
            _remember_receipt(path, state, receipt)
            _print_v2_json(receipt)
            print(
                "The accepted receipt disappeared. Its outcome is ambiguous "
                "and terminal; never replay this batch.",
                file=sys.stderr,
            )
            return 0
        disposition, warning, exit_code = _submit_persisted_batch(
            path, session, batch_id,
        )
        _print_v2_json(disposition)
        if warning:
            print(warning, file=sys.stderr)
        return exit_code


def command_retry(args: argparse.Namespace) -> int:
    path, _session = _v2_session(args.session)
    with _v2_request_lock(path):
        return _command_retry_locked(args)


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


def _legacy_command_wait(
    path: Path,
    session: dict[str, Any],
    args: argparse.Namespace,
    *,
    until: str,
    baseline: dict[str, Any] | None,
) -> int:
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
            _print_v2_json(_local_wait_response(
                session, "game_terminal", health, None,
            ))
            return 0
        phase = health["phase"]
        if (
            until == "phase"
            and isinstance(phase, dict)
            and phase["active"] is True
            and phase["state"] == "awaiting_agent"
            and health["observation_available"] is True
        ):
            _print_v2_json(_local_wait_response(
                session, "phase_active", health, None,
            ))
            return 0
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
            assert baseline is not None
            if revision["state_token"] != baseline["state_token"]:
                _print_v2_json(_local_wait_response(
                    session, "revision_changed", health, revision,
                ))
                return 0
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            _print_v2_json(_local_wait_response(
                session, "timeout", health, revision,
            ))
            return 0
        time.sleep(min(args.poll_s, remaining))


def command_wait(args: argparse.Namespace) -> int:
    path, session = _v2_session(args.session)
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
        return _legacy_command_wait(
            path, session, args, until=until, baseline=baseline,
        )
    if not 200 <= response.status < 300:
        _raise_validated_v2_error(response)
    value = _validate_wait_response(
        response.value,
        session,
        until=until,
        after_state_token=(
            None if baseline is None else baseline["state_token"]
        ),
    )
    _print_v2_json(value)
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

    health = commands.add_parser("health")
    health.add_argument("--session", required=True)
    health.set_defaults(handler=command_health)

    turn = commands.add_parser("turn")
    turn.add_argument("--session", required=True)
    turn.set_defaults(handler=command_turn)

    state = commands.add_parser("state")
    state.add_argument("--session", required=True)
    state.add_argument("--section", default="")
    state.add_argument("--actor-id", default="")
    state.add_argument("--relation-id", default="")
    state.add_argument("--center-id", default="")
    state.add_argument("--radius", type=int)
    state.add_argument("--limit")
    state.add_argument("--cursor", default="")
    state.set_defaults(handler=command_state)

    legal = commands.add_parser("legal")
    legal.add_argument("--session", required=True)
    legal.add_argument("--actor-id", default="")
    legal.add_argument("--target-id", default="")
    legal.add_argument("--limit")
    legal.add_argument("--cursor", default="")
    legal.add_argument("--kind", default="")
    legal.add_argument("--all", dest="all_pages", action="store_true")
    legal.set_defaults(handler=command_legal)

    batch = commands.add_parser("batch")
    batch.add_argument("--session", required=True)
    batch.add_argument("--action-id", required=True)
    batch.add_argument("--arguments", default="{}")
    batch.set_defaults(handler=command_batch)

    receipt = commands.add_parser("receipt")
    receipt.add_argument("--session", required=True)
    receipt.add_argument("--batch-id", required=True)
    receipt.set_defaults(handler=command_receipt)

    retry = commands.add_parser("retry")
    retry.add_argument("--session", required=True)
    retry.add_argument("--batch-id", required=True)
    retry.set_defaults(handler=command_retry)

    wait = commands.add_parser("wait")
    wait.add_argument("--session", required=True)
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
        _print_v2_json(exc.payload)
        print(f"error: {exc}", file=sys.stderr)
        return 2
    except PlayerError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
