"""Small stdlib client used by CLI commands and external bot examples."""

from __future__ import annotations

import hashlib
import json
import os
import re
import secrets
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any


DEFAULT_SERVICE_URL = "http://127.0.0.1:8765"
NATIVE_VIEWER_PROTOCOL_VERSION = 1


class ClientError(RuntimeError):
    def __init__(self, status: int | None, message: str):
        super().__init__(message)
        self.status = status


class NativeViewerCompatibilityError(ClientError):
    """The running supervisor predates the safe native-viewer protocol."""


class _RejectRedirects(urllib.request.HTTPRedirectHandler):
    """Never forward owner, admin, join, or agent bearers through redirects."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def controller_session_key(controller_label: str) -> str:
    """Return a readable, exact-label-stable local session filename key."""
    slug = re.sub(r"[^a-z0-9]+", "-", controller_label.lower()).strip("-")
    slug = slug or "controller"
    digest = hashlib.sha256(controller_label.encode("utf-8")).hexdigest()[:12]
    return f"{slug}-{digest}"


def request_json(
    method: str,
    url: str,
    *,
    token: str | None = None,
    body: Any | None = None,
    timeout: float = 60,
) -> dict[str, Any]:
    data = None if body is None else json.dumps(
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
                value = json.loads(exc.read().decode("utf-8"))
                error = value.get("error")
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
        raise ClientError(exc.code, message) from exc
    except (OSError, json.JSONDecodeError) as exc:
        raise ClientError(None, str(exc)) from exc
    if not isinstance(value, dict):
        raise ClientError(None, "service returned a non-object JSON response")
    return value


def write_private_json(path: str | Path, value: Any) -> Path:
    destination = Path(path).expanduser().resolve()
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


def load_private_json(path: str | Path) -> dict[str, Any]:
    source = Path(path).expanduser().resolve()
    try:
        value = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ClientError(None, f"cannot read session {source}: {exc}") from exc
    if not isinstance(value, dict):
        raise ClientError(None, f"session {source} must contain a JSON object")
    return value


def service_url(value: str | None = None) -> str:
    return (
        value or os.environ.get("AGENT_EVAL_SERVICE_URL") or DEFAULT_SERVICE_URL
    ).rstrip("/")


def create_game(
    base_url: str, admin_token: str, config: dict[str, Any],
) -> dict[str, Any]:
    return request_json(
        "POST", service_url(base_url) + "/v1/games",
        token=admin_token, body=config,
    )


def join_game(
    base_url: str, game_id: str, join_token: str,
    selected_place: int | str | None = None,
    controller_label: str | None = None,
    metadata: Any = None,
    supported_control_protocols: list[str] | None = None,
) -> dict[str, Any]:
    body: dict[str, Any] = {}
    if selected_place is not None:
        body["place"] = selected_place
    if controller_label is not None:
        body["controller_label"] = controller_label
    if metadata is not None:
        body["metadata"] = metadata
    if supported_control_protocols is not None:
        body["supported_control_protocols"] = supported_control_protocols
    return request_json(
        "POST", f"{service_url(base_url)}/v1/games/{game_id}/join",
        token=join_token, body=body,
    )


def join_capabilities(
    base_url: str, game_id: str,
) -> tuple[str, list[str] | None]:
    """Preflight protocol without sending new fields to an old supervisor."""
    status = request_json(
        "GET", f"{service_url(base_url)}/v1/games/{game_id}/status",
        timeout=10,
    )
    protocol = status.get("control_protocol")
    if protocol is None or protocol == "strategic-v1":
        return "strategic-v1", None
    if protocol == "full-control-v2":
        return protocol, [protocol]
    raise ClientError(
        None, f"game requires unsupported control protocol {protocol!r}",
    )


def request_native_viewer(
    base_url: str, game_id: str, owner_token: str,
) -> dict[str, Any]:
    return request_json(
        "POST",
        f"{service_url(base_url)}/v1/games/{game_id}/native-viewer",
        token=owner_token,
        body={},
        timeout=20,
    )


def require_native_viewer_protocol(
    base_url: str, game_id: str,
) -> dict[str, Any]:
    """Fail before mutating an older supervisor's native-viewer state."""
    health = request_json(
        "GET", service_url(base_url) + "/health", timeout=10,
    )
    protocol = health.get("native_viewer_protocol")
    compatible = bool(
        isinstance(protocol, dict)
        and protocol.get("version") == NATIVE_VIEWER_PROTOCOL_VERSION
        and protocol.get("lease_status") is True
        and protocol.get("bridge_response_ack") is True
        and protocol.get("release_during_activation") is True
    )
    if not compatible:
        advertised = (
            "unversioned"
            if not isinstance(protocol, dict)
            else f"version {protocol.get('version', 'unknown')}"
        )
        raise NativeViewerCompatibilityError(
            None,
            f"native live viewing cannot be enabled safely for {game_id}: "
            f"the already-running supervisor advertises {advertised}, but "
            f"watcher protocol {NATIVE_VIEWER_PROTOCOL_VERSION} is required. "
            "The game was not changed and can keep running. An old lease or "
            "stale in-memory viewer flag cannot be repaired through that "
            "supervisor's public API. Use `just replay " + game_id + "` now; "
            "after all current games finish, restart the supervisor and create "
            "a new game to use `just watch`. Updating files does not update an "
            "already-running supervisor process.",
        )
    return protocol


def release_native_viewer(
    base_url: str, game_id: str, owner_token: str, lease_id: str,
) -> dict[str, Any]:
    return request_json(
        "POST",
        f"{service_url(base_url)}/v1/games/{game_id}/native-viewer/release",
        token=owner_token,
        body={"lease_id": lease_id},
        timeout=30,
    )


def native_viewer_status(
    base_url: str, game_id: str, owner_token: str, lease_id: str,
) -> dict[str, Any]:
    query = urllib.parse.urlencode({"lease_id": lease_id})
    return request_json(
        "GET",
        f"{service_url(base_url)}/v1/games/{game_id}/native-viewer?{query}",
        token=owner_token,
        timeout=10,
    )


def next_turn(
    session: dict[str, Any], after_turn: int = 0, wait_s: float = 30,
) -> dict[str, Any]:
    query = urllib.parse.urlencode(
        {"after_turn": after_turn, "wait_s": wait_s},
    )
    value = request_json(
        "GET",
        (
            f"{service_url(session.get('service_url'))}/v1/games/"
            f"{session['game_id']}/me/next?{query}"
        ),
        token=session["agent_token"],
        timeout=max(10, wait_s + 5),
    )
    if value.get("game_id") not in {None, session["game_id"]}:
        raise ClientError(None, "next response belongs to a different game")
    if value.get("agent_id") not in {None, session["agent_id"]}:
        raise ClientError(None, "next response belongs to a different agent seat")
    return value


def submit_action(
    session: dict[str, Any], turn: int, observation_id: str,
    action: dict[str, Any], telemetry: Any = None,
) -> dict[str, Any]:
    value = request_json(
        "POST",
        (
            f"{service_url(session.get('service_url'))}/v1/games/"
            f"{session['game_id']}/me/actions"
        ),
        token=session["agent_token"],
        body={
            "turn": turn,
            "observation_id": observation_id,
            "action": action,
            "telemetry": telemetry,
        },
    )
    if value.get("accepted") is not True:
        raise ClientError(
            None,
            "supervisor did not acknowledge the action as accepted; "
            "do not advance LAST_TURN",
        )
    expected = {
        "game_id": session["game_id"],
        "agent_id": session["agent_id"],
        "turn": turn,
        "place": session.get("place"),
        "seat_id": session.get("seat_id"),
        "controller_label": session.get("controller_label"),
    }
    for key, expected_value in expected.items():
        if (
            expected_value is not None and key in value
            and value[key] != expected_value
        ):
            raise ClientError(
                None,
                f"accepted action acknowledgement has the wrong {key}; "
                "do not advance LAST_TURN",
            )
    return value
