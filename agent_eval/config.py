"""Versioned JSON configuration for the evaluation harness."""

from __future__ import annotations

import json
import hashlib
import math
import re
from dataclasses import dataclass, field, replace
from pathlib import Path
from typing import Any

SCHEMA_VERSION = 1
SEAT_TYPES = {
    "native",
    "deterministic",
    "external",
    "openai_responses",
    "anthropic_messages",
    "openai_compatible",
}
SAFE_NAME = re.compile(r"^[A-Za-z][A-Za-z0-9_-]{0,31}$")


class ConfigError(ValueError):
    """Raised when the versioned config is invalid."""


@dataclass(frozen=True)
class SeatConfig:
    id: str
    name: str
    type: str
    model: str | None = None
    base_url: str | None = None
    api_key_env: str | None = None
    token_env: str | None = None
    instructions: str | None = None
    timeout_s: float = 30.0
    options: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True)
class EvalConfig:
    schema_version: int
    name: str
    ruleset: str
    turns: int
    seeds: tuple[int, ...]
    seats: tuple[SeatConfig, ...]
    server: dict[str, Any]


def _number(value: Any, label: str, *, minimum: float) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ConfigError(f"{label} must be a number")
    try:
        number = float(value)
    except OverflowError as exc:
        raise ConfigError(f"{label} must be finite") from exc
    if not math.isfinite(number):
        raise ConfigError(f"{label} must be finite")
    if number < minimum:
        raise ConfigError(f"{label} must be >= {minimum}")
    return number


def _seat(raw: Any, index: int) -> SeatConfig:
    if not isinstance(raw, dict):
        raise ConfigError(f"seats[{index}] must be an object")
    for required in ("id", "name", "type"):
        if required not in raw:
            raise ConfigError(f"seats[{index}].{required} is required")
    sid, name, kind = raw["id"], raw["name"], raw["type"]
    if not isinstance(sid, str) or not SAFE_NAME.fullmatch(sid):
        raise ConfigError(f"seats[{index}].id must match {SAFE_NAME.pattern}")
    if not isinstance(name, str) or not SAFE_NAME.fullmatch(name):
        raise ConfigError(f"seats[{index}].name must match {SAFE_NAME.pattern}")
    if kind not in SEAT_TYPES:
        raise ConfigError(f"seats[{index}].type must be one of {sorted(SEAT_TYPES)}")
    model = raw.get("model")
    if kind in {"openai_responses", "anthropic_messages", "openai_compatible"}:
        if not isinstance(model, str) or not model:
            raise ConfigError(f"seats[{index}].model is required for {kind}")
    base_url = raw.get("base_url")
    if kind == "openai_compatible" and (
        not isinstance(base_url, str) or not base_url.startswith(("http://", "https://"))
    ):
        raise ConfigError(f"seats[{index}].base_url must be an HTTP URL")
    api_key_env = raw.get("api_key_env")
    if api_key_env is not None and (
        not isinstance(api_key_env, str) or not api_key_env
    ):
        raise ConfigError(f"seats[{index}].api_key_env must be a non-empty string")
    token_env = raw.get("token_env")
    if token_env is not None and (not isinstance(token_env, str) or not token_env):
        raise ConfigError(f"seats[{index}].token_env must be a non-empty string")
    if kind == "external" and token_env is None:
        raise ConfigError(f"seats[{index}].token_env is required for external")
    if kind != "external" and token_env is not None:
        raise ConfigError(f"seats[{index}].token_env is only valid for external")
    instructions = raw.get("instructions")
    if instructions is not None and (
        not isinstance(instructions, str) or not instructions.strip()
    ):
        raise ConfigError(f"seats[{index}].instructions must be a non-empty string")
    timeout_s = _number(raw.get("timeout_s", 30), f"seats[{index}].timeout_s", minimum=0.1)
    options = raw.get("options", {})
    if not isinstance(options, dict):
        raise ConfigError(f"seats[{index}].options must be an object")
    reserved = {"model", "input", "messages", "max_tokens", "max_output_tokens", "temperature"}
    conflicts = sorted(set(options) & reserved)
    if conflicts:
        raise ConfigError(
            f"seats[{index}].options cannot override reserved fields: {conflicts}"
        )
    secret_fields = sorted(
        key for key in options
        if any(part in key.lower() for part in ("api_key", "secret", "authorization", "bearer"))
    )
    if secret_fields:
        raise ConfigError(
            f"seats[{index}].options cannot contain secret fields: {secret_fields}"
        )
    known = {
        "id", "name", "type", "model", "base_url", "api_key_env",
        "token_env", "instructions", "timeout_s", "options",
    }
    unknown = set(raw) - known
    if unknown:
        raise ConfigError(f"seats[{index}] has unknown fields: {sorted(unknown)}")
    return SeatConfig(
        sid, name, kind, model, base_url, api_key_env, token_env,
        instructions, timeout_s, options,
    )


def _server(raw: Any) -> dict[str, Any]:
    if not isinstance(raw, dict):
        raise ConfigError("server must be an object")
    known = {
        "binary", "wall_timeout_s", "mapsize", "extra_commands",
        "agentd_port", "allow_fallbacks", "frame_interval", "frame_zoom",
    }
    unknown = sorted(set(raw) - known)
    if unknown:
        raise ConfigError(f"server has unknown fields: {unknown}")
    value = {
        "allow_fallbacks": False,
        "agentd_port": 0,
        "frame_interval": 5,
        "frame_zoom": 1,
        **raw,
    }
    if not isinstance(value["allow_fallbacks"], bool):
        raise ConfigError("server.allow_fallbacks must be a boolean")
    agentd_port = value["agentd_port"]
    if (
        isinstance(agentd_port, bool) or not isinstance(agentd_port, int)
        or not (agentd_port == 0 or 1024 <= agentd_port <= 65535)
    ):
        raise ConfigError(
            "server.agentd_port must be 0 or an integer in [1024, 65535]"
        )
    interval = value["frame_interval"]
    if isinstance(interval, bool) or not isinstance(interval, int) or not 0 <= interval <= 99:
        raise ConfigError("server.frame_interval must be an integer in [0, 99]")
    zoom = value["frame_zoom"]
    if isinstance(zoom, bool) or not isinstance(zoom, int) or not 1 <= zoom <= 5:
        raise ConfigError("server.frame_zoom must be an integer in [1, 5]")
    binary = value.get("binary")
    if binary is not None and (not isinstance(binary, str) or not binary):
        raise ConfigError("server.binary must be a non-empty string")
    wall_timeout = value.get("wall_timeout_s")
    if wall_timeout is not None:
        _number(wall_timeout, "server.wall_timeout_s", minimum=0.1)
    mapsize = value.get("mapsize")
    if mapsize is not None and (
        isinstance(mapsize, bool) or not isinstance(mapsize, int)
        or not 20 <= mapsize <= 2_048_000
    ):
        raise ConfigError("server.mapsize must be an integer in [20, 2048000]")
    extra = value.get("extra_commands", [])
    if not isinstance(extra, list) or not all(isinstance(item, str) for item in extra):
        raise ConfigError("server.extra_commands must be an array of strings")
    return value


def load_config(path: str | Path) -> EvalConfig:
    config_path = Path(path)
    try:
        raw = json.loads(config_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ConfigError(f"cannot read config {config_path}: {exc}") from exc
    if not isinstance(raw, dict):
        raise ConfigError("config must be an object")
    version = raw.get("schema_version")
    if version != SCHEMA_VERSION:
        raise ConfigError(f"schema_version must be {SCHEMA_VERSION}")
    name = raw.get("name")
    if not isinstance(name, str) or not SAFE_NAME.fullmatch(name):
        raise ConfigError(f"name must match {SAFE_NAME.pattern}")
    ruleset = raw.get("ruleset", "classic")
    if not isinstance(ruleset, str) or not SAFE_NAME.fullmatch(ruleset):
        raise ConfigError(f"ruleset must match {SAFE_NAME.pattern}")
    turns = raw.get("turns", 5000)
    if isinstance(turns, bool) or not isinstance(turns, int) or not 1 <= turns <= 5000:
        raise ConfigError("turns must be an integer in [1, 5000]")
    seeds_raw = raw.get("seeds", [1])
    if not isinstance(seeds_raw, list) or not seeds_raw:
        raise ConfigError("seeds must be a non-empty array")
    seeds: list[int] = []
    for index, seed in enumerate(seeds_raw):
        if isinstance(seed, bool) or not isinstance(seed, int) or not 1 <= seed <= 2_147_483_647:
            raise ConfigError(f"seeds[{index}] must be an integer in [1, 2147483647]")
        seeds.append(seed)
    seats_raw = raw.get("seats")
    if not isinstance(seats_raw, list) or not 2 <= len(seats_raw) <= 3:
        raise ConfigError("seats must contain 2 or 3 entries")
    seats = tuple(_seat(item, index) for index, item in enumerate(seats_raw))
    if len({seat.id for seat in seats}) != len(seats):
        raise ConfigError("seat ids must be unique")
    if len({seat.name for seat in seats}) != len(seats):
        raise ConfigError("seat names must be unique")
    server = _server(raw.get("server", {}))
    known_top = {"schema_version", "name", "ruleset", "turns", "seeds", "seats", "server"}
    unknown_top = set(raw) - known_top
    if unknown_top:
        raise ConfigError(f"config has unknown fields: {sorted(unknown_top)}")
    return EvalConfig(version, name, ruleset, turns, tuple(seeds), seats, dict(server))


def rotate_seats(config: EvalConfig, rotation: int) -> EvalConfig:
    """Rotate controllers across stable Freeciv player positions."""
    count = len(config.seats)
    rotation %= count
    physical_names = [seat.name for seat in config.seats]
    controllers = list(config.seats[-rotation:] + config.seats[:-rotation]) if rotation else list(config.seats)
    rotated = tuple(replace(controller, name=physical_names[index]) for index, controller in enumerate(controllers))
    return replace(config, seats=rotated)


def public_config(config: EvalConfig) -> dict[str, Any]:
    """Serialize config without reading or exposing secret environment values."""
    return {
        "schema_version": config.schema_version,
        "name": config.name,
        "ruleset": config.ruleset,
        "turns": config.turns,
        "seeds": list(config.seeds),
        "seats": [
            {
                "id": seat.id,
                "name": seat.name,
                "type": seat.type,
                "model": seat.model,
                "base_url": seat.base_url,
                "api_key_env": seat.api_key_env,
                "token_env": seat.token_env,
                "instructions": seat.instructions,
                "timeout_s": seat.timeout_s,
                "options": seat.options,
                "controller_fingerprint": controller_fingerprint(seat),
            }
            for seat in config.seats
        ],
        "server": config.server,
    }


def controller_fingerprint(seat: SeatConfig | dict[str, Any]) -> str:
    def get(name: str) -> Any:
        return getattr(seat, name) if isinstance(seat, SeatConfig) else seat.get(name)

    identity = {
        "id": get("id"),
        "type": get("type"),
        "model": get("model"),
        "instructions": get("instructions"),
        "base_url": get("base_url"),
        "options": get("options") or {},
    }
    encoded = json.dumps(
        identity, sort_keys=True, separators=(",", ":"), ensure_ascii=False,
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()
