"""HTTP coordination server between Freeciv's bridge and seat policies."""

from __future__ import annotations

import json
import hmac
import os
import threading
import time
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

from .actions import ActionError, deterministic_action, validate_action
from .config import EvalConfig, SeatConfig, controller_fingerprint, public_config
from .providers import ProviderError, invoke_provider


class AgentState:
    def __init__(
        self,
        config: EvalConfig,
        internal_token: str,
        external_tokens: dict[str, str],
        trace_path: Path | None = None,
    ):
        if not internal_token:
            raise ValueError("internal bearer token must not be empty")
        self.config = config
        self.seats = {seat.id: seat for seat in config.seats}
        self.internal_token = internal_token
        self.external_tokens = dict(external_tokens)
        expected_external = {seat.id for seat in config.seats if seat.type == "external"}
        if set(self.external_tokens) != expected_external:
            raise ValueError("external bearer token mapping does not match external seats")
        token_values = list(self.external_tokens.values())
        if any(not token for token in token_values):
            raise ValueError("external bearer tokens must not be empty")
        if len(set(token_values)) != len(token_values) or internal_token in token_values:
            raise ValueError("internal and external bearer tokens must be distinct")
        self.trace_path = trace_path
        self.started_at = time.time()
        self.game: dict[str, Any] = {}
        self.observations: dict[str, dict[str, Any]] = {}
        self.external_actions: dict[tuple[int, str], dict[str, Any]] = {}
        self.pending: set[tuple[int, str]] = set()
        self.trace: list[dict[str, Any]] = []
        self.condition = threading.Condition()

    def append_trace(self, event: dict[str, Any]) -> None:
        with self.condition:
            self.trace.append(event)
            if self.trace_path:
                self.trace_path.parent.mkdir(parents=True, exist_ok=True)
                with self.trace_path.open("a", encoding="utf-8") as stream:
                    stream.write(json.dumps(event, sort_keys=True, separators=(",", ":")) + "\n")

    def submit_external(self, turn: int, seat_id: str, action: dict[str, Any]) -> None:
        clean = validate_action(action)
        with self.condition:
            key = (turn, seat_id)
            if key not in self.pending:
                raise ActionError(f"seat {seat_id} is not awaiting an action for turn {turn}")
            self.external_actions[key] = clean
            self.condition.notify_all()

    def _external(self, seat: SeatConfig, observation: dict[str, Any]) -> tuple[dict[str, Any], str | None]:
        key = (int(observation["turn"]), seat.id)
        deadline = time.monotonic() + seat.timeout_s
        with self.condition:
            self.pending.add(key)
            self.condition.notify_all()
            while key not in self.external_actions:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    self.pending.discard(key)
                    return deterministic_action(observation), "external action timed out"
                self.condition.wait(remaining)
            action = self.external_actions.pop(key)
            self.pending.discard(key)
            return action, None

    def process_turn(self, payload: dict[str, Any]) -> dict[str, Any]:
        if not isinstance(payload, dict):
            raise ValueError("turn request must be an object")
        turn = payload.get("turn")
        year = payload.get("year")
        observations = payload.get("observations")
        if isinstance(turn, bool) or not isinstance(turn, int):
            raise ValueError("turn must be an integer")
        if isinstance(year, bool) or not isinstance(year, int):
            raise ValueError("year must be an integer")
        if not isinstance(observations, list):
            raise ValueError("observations must be an array")
        seen: set[str] = set()
        clean_obs: list[dict[str, Any]] = []
        for index, observation in enumerate(observations):
            if not isinstance(observation, dict):
                raise ValueError(f"observations[{index}] must be an object")
            seat_id = observation.get("seat_id")
            if seat_id not in self.seats or seat_id in seen:
                raise ValueError(f"observations[{index}].seat_id is unknown or duplicated")
            if observation.get("turn") != turn or observation.get("year") != year:
                raise ValueError(f"observations[{index}] turn/year mismatch")
            seen.add(seat_id)
            clean_obs.append(dict(observation))
        self.game = {"game_id": payload.get("game_id"), "turn": turn, "year": year}
        self.observations.update({item["seat_id"]: item for item in clean_obs})

        actions: list[dict[str, Any]] = []
        benchmark_valid = True
        for observation in clean_obs:
            seat = self.seats[observation["seat_id"]]
            start = time.monotonic()
            error: str | None = None
            fallback = False
            input_tokens = output_tokens = 0
            provider_model = response_id = raw_output = None
            if seat.type == "native":
                action = None
                source = "native"
            elif seat.type == "deterministic":
                action = deterministic_action(observation)
                source = "deterministic"
            elif seat.type == "external":
                action, error = self._external(seat, observation)
                fallback = error is not None
                source = "deterministic_fallback" if fallback else "external"
            else:
                try:
                    result = invoke_provider(seat, observation)
                    action = result.action
                    input_tokens = result.input_tokens
                    output_tokens = result.output_tokens
                    provider_model = result.provider_model
                    response_id = result.response_id
                    raw_output = result.raw_output
                    source = seat.type
                except (ProviderError, OSError, ValueError) as exc:
                    action = deterministic_action(observation)
                    error = str(exc)
                    fallback = True
                    provider_model = getattr(exc, "provider_model", None)
                    response_id = getattr(exc, "response_id", None)
                    raw_output = getattr(exc, "raw_output", None)
                    input_tokens = int(getattr(exc, "input_tokens", 0))
                    output_tokens = int(getattr(exc, "output_tokens", 0))
                    source = "deterministic_fallback"
            latency_ms = round((time.monotonic() - start) * 1000, 3)
            event = {
                "event": "decision",
                "turn": turn,
                "year": year,
                "seat_id": seat.id,
                "player_name": seat.name,
                "seat_type": seat.type,
                "controller_fingerprint": controller_fingerprint(seat),
                "configured_model": seat.model,
                "provider_model": provider_model,
                "provider_response_id": response_id,
                "source": source,
                "fallback": fallback,
                "error": error,
                "latency_ms": latency_ms,
                "input_tokens": input_tokens,
                "output_tokens": output_tokens,
                "observation": observation,
                "raw_output": raw_output,
                "action": action,
            }
            self.append_trace(event)
            if fallback:
                benchmark_valid = False
            if action is not None:
                actions.append({"seat_id": seat.id, "traits": action["traits"]})
        return {
            "schema_version": 1,
            "turn": turn,
            "actions": actions,
            "timed_out_seats": [],
            "benchmark_valid": benchmark_valid,
        }


class AgentHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address: tuple[str, int], state: AgentState):
        self.state = state
        super().__init__(address, AgentHandler)


class AgentHandler(BaseHTTPRequestHandler):
    server: AgentHTTPServer

    def log_message(self, format: str, *args: object) -> None:
        return

    def _json(self, status: int, value: Any) -> None:
        body = json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _body(self) -> Any:
        length = int(self.headers.get("Content-Length", "0"))
        if length > 1_000_000:
            raise ValueError("request body too large")
        return json.loads(self.rfile.read(length).decode("utf-8"))

    def _bearer(self) -> str | None:
        value = self.headers.get("Authorization", "")
        if not value.startswith("Bearer "):
            return None
        token = value[len("Bearer ") :]
        return token if token else None

    def _authorize_internal(self) -> bool:
        token = self._bearer()
        if token is None:
            self._json(HTTPStatus.UNAUTHORIZED, {"error": "bearer token required"})
            return False
        if not hmac.compare_digest(token, self.server.state.internal_token):
            self._json(HTTPStatus.FORBIDDEN, {"error": "token is not authorized"})
            return False
        return True

    def _authorize_external(self, seat_id: str) -> bool:
        token = self._bearer()
        if token is None:
            self._json(HTTPStatus.UNAUTHORIZED, {"error": "bearer token required"})
            return False
        expected = self.server.state.external_tokens.get(seat_id)
        if expected is None or not hmac.compare_digest(token, expected):
            self._json(HTTPStatus.FORBIDDEN, {"error": "token is not authorized for this seat"})
            return False
        return True

    def do_GET(self) -> None:
        path = urlparse(self.path).path
        state = self.server.state
        if path == "/health":
            self._json(HTTPStatus.OK, {"ok": True, "uptime_s": round(time.time() - state.started_at, 3)})
            return
        if path == "/v1/game":
            if not self._authorize_internal():
                return
            self._json(HTTPStatus.OK, {"config": public_config(state.config), "current": state.game})
            return
        if path == "/v1/trace":
            if not self._authorize_internal():
                return
            self._json(HTTPStatus.OK, {"events": state.trace})
            return
        prefix = "/v1/seats/"
        suffix = "/observation"
        if path.startswith(prefix) and path.endswith(suffix):
            seat_id = path[len(prefix) : -len(suffix)]
            if not self._authorize_external(seat_id):
                return
            observation = state.observations.get(seat_id)
            if observation is None:
                self._json(HTTPStatus.NOT_FOUND, {"error": "no observation for seat"})
            else:
                pending = any(key[1] == seat_id for key in state.pending)
                self._json(HTTPStatus.OK, {"observation": observation, "pending": pending})
            return
        self._json(HTTPStatus.NOT_FOUND, {"error": "not found"})

    def do_POST(self) -> None:
        path = urlparse(self.path).path
        try:
            if path == "/v1/turn":
                if not self._authorize_internal():
                    return
                payload = self._body()
                self._json(HTTPStatus.OK, self.server.state.process_turn(payload))
                return
            if path == "/v1/actions":
                if self._bearer() is None:
                    self._json(HTTPStatus.UNAUTHORIZED, {"error": "bearer token required"})
                    return
                payload = self._body()
                if not isinstance(payload, dict):
                    raise ValueError("action request must be an object")
                seat_id = payload.get("seat_id")
                if seat_id not in self.server.state.seats:
                    self._json(HTTPStatus.FORBIDDEN, {"error": "token is not authorized for this seat"})
                    return
                if not self._authorize_external(seat_id):
                    return
                turn = payload.get("turn")
                if isinstance(turn, bool) or not isinstance(turn, int):
                    raise ValueError("turn must be an integer")
                self.server.state.submit_external(turn, seat_id, payload.get("action"))
                self._json(HTTPStatus.ACCEPTED, {"accepted": True})
                return
            self._json(HTTPStatus.NOT_FOUND, {"error": "not found"})
        except (ActionError, ValueError, json.JSONDecodeError) as exc:
            self._json(HTTPStatus.BAD_REQUEST, {"error": str(exc)})
        except Exception as exc:  # Keep the bridge response well formed.
            self._json(HTTPStatus.INTERNAL_SERVER_ERROR, {"error": str(exc)})


def external_tokens_from_environment(config: EvalConfig) -> dict[str, str]:
    tokens: dict[str, str] = {}
    for seat in config.seats:
        if seat.type != "external":
            continue
        value = os.environ.get(seat.token_env, "")
        if not value:
            raise ValueError(f"missing external bearer token environment variable {seat.token_env}")
        tokens[seat.id] = value
    return tokens


def make_server(
    config: EvalConfig,
    host: str,
    port: int,
    trace_path: Path | None = None,
    *,
    internal_token: str,
    external_tokens: dict[str, str] | None = None,
) -> AgentHTTPServer:
    return AgentHTTPServer(
        (host, port),
        AgentState(config, internal_token, external_tokens or {}, trace_path),
    )
