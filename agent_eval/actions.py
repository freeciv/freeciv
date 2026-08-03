"""Strategic-v1 action schema and deterministic baseline."""

from __future__ import annotations

from typing import Any

TRAITS = ("aggressive", "builder", "expansionist", "trader")
TRAIT_MIN = -49
TRAIT_MAX = 50


class ActionError(ValueError):
    """Raised when an action does not match strategic-v1."""


def _strict_int(value: Any, path: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ActionError(f"{path} must be an integer")
    return value


def validate_action(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ActionError("action must be an object")
    if set(value) != {"type", "traits"}:
        raise ActionError("action must contain exactly type and traits")
    if value["type"] != "set_traits":
        raise ActionError("action.type must be set_traits")
    traits = value["traits"]
    if not isinstance(traits, dict) or set(traits) != set(TRAITS):
        raise ActionError("action.traits must contain exactly " + ", ".join(TRAITS))
    clean: dict[str, int] = {}
    for name in TRAITS:
        number = _strict_int(traits[name], f"action.traits.{name}")
        if not TRAIT_MIN <= number <= TRAIT_MAX:
            raise ActionError(
                f"action.traits.{name} must be in [{TRAIT_MIN}, {TRAIT_MAX}]"
            )
        clean[name] = number
    return {"type": "set_traits", "traits": clean}


def deterministic_action(observation: dict[str, Any]) -> dict[str, Any]:
    """Return a reproducible own-state-only strategic policy."""
    cities = int(observation.get("num_cities", 0))
    units = int(observation.get("num_units", 0))
    gold = int(observation.get("gold", 0))
    turn = int(observation.get("turn", 0))

    def bound(value: int) -> int:
        return max(TRAIT_MIN, min(TRAIT_MAX, value))

    return validate_action(
        {
            "type": "set_traits",
            "traits": {
                "aggressive": bound((units - 2 * cities) * 3),
                "builder": bound(24 - units + cities * 2),
                "expansionist": bound(34 - cities * 5 - turn // 20),
                "trader": bound(18 if gold < 100 else 4),
            },
        }
    )
