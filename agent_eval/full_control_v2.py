"""Strict transport primitives for the implemented ``full-control-v2`` sidecar.

The native Freeciv integration lives in the headless client and supervisor;
this module keeps their versioned, transport-independent values closed and
validatable.
"""

from __future__ import annotations

import hashlib
import json
import math
import re
from typing import Any, Iterable


STRATEGIC_V1 = "strategic-v1"
FULL_CONTROL_V2 = "full-control-v2"
CONTROL_PROTOCOLS = frozenset({STRATEGIC_V1, FULL_CONTROL_V2})
FULL_CONTROL_SCHEMA_VERSION = 2
INITIAL_MAX_COMMANDS_PER_BATCH = 1

ACTION_FAMILIES = frozenset({
    "city",
    "diplomacy",
    "economy",
    "government",
    "phase",
    "player",
    "pregame",
    "research",
    "spaceship",
    "unit",
})

# Closed public kinds projected by the current static native action rules.  A
# runtime catalog is intentionally only the currently legal, situational
# subset: ``unit.perform_action`` carries the live action/target supplied by
# Freeciv, and validators still accept future operations inside the versioned
# top-level families.
REQUIRED_ACTION_KINDS = frozenset({
    "city.assign_citizen",
    "city.buy_production",
    "city.rename",
    "city.manage_worker_task",
    "city.set_governor",
    "city.set_options",
    "city.set_production",
    "city.set_rally",
    "city.set_specialist",
    "city.set_worklist",
    "city.sell_improvement",
    "diplomacy.acceptance",
    "diplomacy.clause",
    "diplomacy.meeting",
    "diplomacy.relation",
    "diplomacy.withdraw",
    "economy.set_rates",
    "government.change",
    "government.revolution",
    "phase.end",
    "player.cancel_vote",
    "player.cast_vote",
    "player.propose_server_setting",
    "player.set_infrastructure",
    "player.set_multiplier",
    "player.send_chat",
    "player.surrender",
    "pregame.configure",
    "pregame.set_ready",
    "pregame.set_team",
    "research.set_goal",
    "research.set_target",
    "spaceship.launch",
    "spaceship.place_component",
    "unit.order",
    "unit.perform_action",
})
PROJECTED_PUBLIC_ACTION_KIND_COUNT = 36
PROJECTED_NATIVE_ACTION_KIND_COUNT = 79


def assert_projected_action_contract(
    public_kinds: Iterable[str], native_kinds: Iterable[str],
) -> None:
    """Fail import when the static native-to-public action map drifts.

    This checks the complete projection rules, never one runtime legal-action
    catalog.  Conditional actions may therefore be absent from any individual
    observation without weakening the versioned contract.
    """
    projected_public = frozenset(public_kinds)
    projected_native = frozenset(native_kinds)
    if projected_public != REQUIRED_ACTION_KINDS:
        missing = sorted(REQUIRED_ACTION_KINDS - projected_public)
        unexpected = sorted(projected_public - REQUIRED_ACTION_KINDS)
        raise RuntimeError(
            "full-control-v2 public action projection drift: "
            f"missing={missing!r} unexpected={unexpected!r}"
        )
    if len(projected_public) != PROJECTED_PUBLIC_ACTION_KIND_COUNT:
        raise RuntimeError(
            "full-control-v2 public action kind count drift: "
            f"expected={PROJECTED_PUBLIC_ACTION_KIND_COUNT} "
            f"actual={len(projected_public)}"
        )
    if len(projected_native) != PROJECTED_NATIVE_ACTION_KIND_COUNT:
        raise RuntimeError(
            "full-control-v2 native action kind count drift: "
            f"expected={PROJECTED_NATIVE_ACTION_KIND_COUNT} "
            f"actual={len(projected_native)}"
        )

ERROR_CODES = frozenset({
    "action_expired",
    "action_outcome_ambiguous",
    "conflict",
    "cursor_expired",
    "illegal_action",
    "internal_error",
    "invalid_batch",
    "invalid_request",
    "not_implemented",
    "rate_limited",
    "scope_too_large",
    "sidecar_unavailable",
    "stale_revision",
    "unsupported_protocol",
})
RECEIPT_STATES = frozenset({
    "accepted", "ambiguous", "applied", "rejected",
})
TERMINAL_RECEIPT_STATES = frozenset({"ambiguous", "applied", "rejected"})

# Which validation layer refused a command.  Every rejected receipt names
# exactly one, so "the server said no" is never the whole story an agent gets.
REJECTION_LAYERS = frozenset({
    "schema",            # the public batch failed closed-schema validation
    "revision",          # the batch named a state revision that is not current
    "catalog",           # the action was never advertised at that revision
    "arguments",         # the action's own argument contract refused the value
    "preflight",         # a supervisor precondition refused before dispatch
    "native_preflight",  # the native boundary refused before dispatching
    "native_dispatch",   # native accepted, dispatched, and the effect failed
    "store",             # the durable receipt store refused the batch identity
    "runtime",           # the seat runtime could not carry the command
})

# The specific refusal.  This vocabulary is closed and server-authored: no
# caller text, native prose, path, or exception message ever enters it, so a
# reason is safe to persist in a durable receipt and to echo to the agent.
REJECTION_REASONS = frozenset({
    "batch_malformed",
    "revision_stale",
    "action_not_advertised",
    "arguments_invalid",
    "server_setting_out_of_range",
    "server_setting_unchanged",
    "pregame_nation_unknown",
    "pregame_style_unknown",
    "pregame_leader_invalid",
    "pregame_configuration_unchanged",
    "phase_control_conflict",
    "phase_not_current",
    "seat_unavailable",
    "receipt_conflict",
    "native_busy",
    "native_bad_argument",
    "native_bad_request",
    "native_not_ready",
    "native_entity_expired",
    "native_refused",
    "postcondition_not_met",
    "internal_failure",
})

_REJECTION_MESSAGES = {
    "batch_malformed":
        "The command batch did not match the full-control-v2 schema.",
    "revision_stale":
        "The command named a state revision that is no longer current; "
        "refresh the observation and reissue against the new revision.",
    "action_not_advertised":
        "The action was not advertised as legal at the requested state "
        "revision; re-enumerate legal actions and reissue.",
    "arguments_invalid":
        "The action arguments did not satisfy the action's own contract.",
    "server_setting_out_of_range":
        "The proposed server setting value is outside the range the server "
        "advertises for that setting.",
    "server_setting_unchanged":
        "The proposed server setting value already equals the current value, "
        "so there is nothing to propose.",
    "pregame_nation_unknown":
        "The nation_id field is not one the pregame_nations section offers "
        "at this revision; only that field is wrong. That section lists only "
        "nations no other player holds, so a nation that was free when it "
        "was read can be claimed before the command lands; re-read "
        "pregame_nations and pick from the current list.",
    "pregame_style_unknown":
        "The style_id field is not one of the IDs the pregame_styles section "
        "advertises at this revision; only that field is wrong.",
    "pregame_leader_invalid":
        "The leader_name field is empty, too long, padded, or contains "
        "control characters; only that field is wrong.",
    "pregame_configuration_unchanged":
        "Every field already holds the requested value, so the configuration "
        "would not change.",
    "phase_control_conflict":
        "The proposal would change the server settings that full-control-v2 "
        "uses to hand phases to agents, which would leave this seat unable to "
        "end its phase.",
    "phase_not_current":
        "The phase this command belongs to is no longer the current phase.",
    "seat_unavailable":
        "This seat's runtime was not available to carry the command.",
    "receipt_conflict":
        "The batch ID is already bound to a different request.",
    "native_busy":
        "The native client was already executing a command; retry the same "
        "batch.",
    "native_bad_argument":
        "The native client refused the action's argument before dispatching "
        "it.",
    "native_bad_request":
        "The native client refused the action request before dispatching it.",
    "native_not_ready":
        "The native client cannot issue this action in its current state; it "
        "may become issuable again in a later phase.",
    "native_entity_expired":
        "The action's native subject no longer exists at the current "
        "revision; re-enumerate legal actions and reissue.",
    "native_refused":
        "The native client refused the action before dispatching it.",
    "postcondition_not_met":
        "The native client dispatched the action and the expected effect did "
        "not take hold; the action had no effect. A governance proposal that "
        "needs a vote, or a setting that only takes effect at a turn "
        "boundary, reports this.",
    "internal_failure":
        "The command could not be completed by the control server.",
}
if set(_REJECTION_MESSAGES) != REJECTION_REASONS:
    # Not an `assert`: under `python -O` an assert vanishes, and the drift it
    # was guarding would surface as a `KeyError` out of `rejection_message` —
    # inside receipt validation, where an unexpected exception type escapes
    # the sanitizing handlers and fails a command with an unattributed 500.
    raise RuntimeError(
        "full-control-v2 rejection vocabulary drift: "
        f"unmessaged={sorted(REJECTION_REASONS - set(_REJECTION_MESSAGES))!r} "
        f"unknown={sorted(set(_REJECTION_MESSAGES) - REJECTION_REASONS)!r}"
    )

_OPAQUE_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
_ACTION_KIND = re.compile(r"^([a-z][a-z0-9_]*)\.([a-z][a-z0-9_]*)$")
_NATIVE_CODE = re.compile(r"^[a-z][a-z0-9_]{0,63}$")
_NATIVE_REASON = re.compile(r"^[A-Z][A-Z0-9_]{0,63}$")


class FullControlSchemaError(ValueError):
    """Raised when a full-control-v2 protocol value is malformed."""


def _exact_object(value: Any, fields: set[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise FullControlSchemaError(f"{label} must be an object")
    if set(value) != fields:
        raise FullControlSchemaError(
            f"{label} must contain exactly {', '.join(sorted(fields))}"
        )
    return value


def _non_negative_int(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise FullControlSchemaError(f"{label} must be a non-negative integer")
    return value


def _opaque_id(value: Any, label: str) -> str:
    if not isinstance(value, str) or not _OPAQUE_ID.fullmatch(value):
        raise FullControlSchemaError(
            f"{label} must be an opaque ASCII identifier of 1 to 128 characters"
        )
    return value


def _json_value(value: Any, label: str, *, depth: int = 0) -> Any:
    if depth > 12:
        raise FullControlSchemaError(f"{label} is nested too deeply")
    if value is None or isinstance(value, (str, bool, int)):
        return value
    if isinstance(value, float):
        if not math.isfinite(value):
            raise FullControlSchemaError(f"{label} must contain finite numbers")
        return value
    if isinstance(value, list):
        if len(value) > 1024:
            raise FullControlSchemaError(f"{label} contains too many array items")
        return [
            _json_value(item, f"{label}[{index}]", depth=depth + 1)
            for index, item in enumerate(value)
        ]
    if isinstance(value, dict):
        if len(value) > 256:
            raise FullControlSchemaError(f"{label} contains too many fields")
        clean: dict[str, Any] = {}
        for key, item in value.items():
            if not isinstance(key, str) or not key or len(key) > 128:
                raise FullControlSchemaError(
                    f"{label} keys must be non-empty strings of at most 128 characters"
                )
            clean[key] = _json_value(
                item, f"{label}.{key}", depth=depth + 1,
            )
        return clean
    raise FullControlSchemaError(f"{label} must contain only JSON values")


def validate_control_protocol(value: Any) -> str:
    if value not in CONTROL_PROTOCOLS:
        raise FullControlSchemaError(
            "control_protocol must be strategic-v1 or full-control-v2"
        )
    return value


def validate_supported_control_protocols(value: Any) -> tuple[str, ...]:
    if not isinstance(value, list) or not value:
        raise FullControlSchemaError(
            "supported_control_protocols must be a non-empty array"
        )
    if len(value) > 16:
        raise FullControlSchemaError(
            "supported_control_protocols must contain at most 16 entries"
        )
    clean: list[str] = []
    for index, protocol in enumerate(value):
        if (
            not isinstance(protocol, str)
            or not protocol
            or len(protocol) > 64
        ):
            raise FullControlSchemaError(
                f"supported_control_protocols[{index}] must be a non-empty string"
            )
        if protocol in clean:
            raise FullControlSchemaError(
                "supported_control_protocols must not contain duplicates"
            )
        clean.append(protocol)
    return tuple(sorted(clean))


def validate_state_revision(value: Any) -> dict[str, Any]:
    raw = _exact_object(
        value, {"turn", "revision", "state_token"}, "state_revision",
    )
    return {
        "turn": _non_negative_int(raw["turn"], "state_revision.turn"),
        "revision": _non_negative_int(
            raw["revision"], "state_revision.revision",
        ),
        "state_token": _opaque_id(
            raw["state_token"], "state_revision.state_token",
        ),
    }


def validate_legal_action_descriptor(value: Any) -> dict[str, Any]:
    """Validate one server-authored action without interpreting its ID."""
    raw = _exact_object(
        value,
        {
            "action_id", "kind", "label", "subject",
            "arguments_schema", "state_revision",
        },
        "legal_action",
    )
    kind = raw["kind"]
    match = _ACTION_KIND.fullmatch(kind) if isinstance(kind, str) else None
    if match is None or match.group(1) not in ACTION_FAMILIES:
        raise FullControlSchemaError(
            "legal_action.kind must be a versioned family.operation name; "
            f"family must be one of {sorted(ACTION_FAMILIES)}"
        )
    label = raw["label"]
    if not isinstance(label, str) or not label.strip() or len(label) > 240:
        raise FullControlSchemaError(
            "legal_action.label must be a non-empty string of at most 240 characters"
        )
    subject = _json_value(raw["subject"], "legal_action.subject")
    arguments_schema = _json_value(
        raw["arguments_schema"], "legal_action.arguments_schema",
    )
    if not isinstance(subject, dict):
        raise FullControlSchemaError("legal_action.subject must be an object")
    if not isinstance(arguments_schema, dict):
        raise FullControlSchemaError(
            "legal_action.arguments_schema must be an object"
        )
    return {
        "action_id": _opaque_id(raw["action_id"], "legal_action.action_id"),
        "kind": kind,
        "label": label.strip(),
        "subject": subject,
        "arguments_schema": arguments_schema,
        "state_revision": validate_state_revision(raw["state_revision"]),
    }


def validate_initial_command_batch(value: Any) -> dict[str, Any]:
    raw = _exact_object(
        value,
        {
            "schema_version", "control_protocol", "game_id", "agent_id",
            "batch_id", "state_revision", "commands",
        },
        "command_batch",
    )
    if raw["schema_version"] != FULL_CONTROL_SCHEMA_VERSION:
        raise FullControlSchemaError("command_batch.schema_version must be 2")
    if raw["control_protocol"] != FULL_CONTROL_V2:
        raise FullControlSchemaError(
            "command_batch.control_protocol must be full-control-v2"
        )
    commands = raw["commands"]
    if not isinstance(commands, list) or len(commands) != INITIAL_MAX_COMMANDS_PER_BATCH:
        raise FullControlSchemaError(
            "initial full-control-v2 command batches must contain exactly one command"
        )
    command = _exact_object(
        commands[0], {"action_id", "arguments"}, "command_batch.commands[0]",
    )
    arguments = _json_value(
        command["arguments"], "command_batch.commands[0].arguments",
    )
    if not isinstance(arguments, dict):
        raise FullControlSchemaError(
            "command_batch.commands[0].arguments must be an object"
        )
    clean = {
        "schema_version": FULL_CONTROL_SCHEMA_VERSION,
        "control_protocol": FULL_CONTROL_V2,
        "game_id": _opaque_id(raw["game_id"], "command_batch.game_id"),
        "agent_id": _opaque_id(raw["agent_id"], "command_batch.agent_id"),
        "batch_id": _opaque_id(raw["batch_id"], "command_batch.batch_id"),
        "state_revision": validate_state_revision(raw["state_revision"]),
        "commands": [{
            "action_id": _opaque_id(
                command["action_id"], "command_batch.commands[0].action_id",
            ),
            "arguments": arguments,
        }],
    }
    return clean


def validated_batch_request_hash(value: Any) -> tuple[dict[str, Any], str]:
    """Validate a public batch and derive its server-private retry identity."""
    clean = validate_initial_command_batch(value)
    encoded = json.dumps(
        clean, sort_keys=True, separators=(",", ":"), ensure_ascii=False,
    ).encode("utf-8")
    return clean, hashlib.sha256(encoded).hexdigest()


def validate_structured_error(value: Any) -> dict[str, Any]:
    raw = _exact_object(
        value,
        {"schema_version", "control_protocol", "error", "state_revision"},
        "error_response",
    )
    if raw["schema_version"] != FULL_CONTROL_SCHEMA_VERSION:
        raise FullControlSchemaError("error_response.schema_version must be 2")
    if raw["control_protocol"] != FULL_CONTROL_V2:
        raise FullControlSchemaError(
            "error_response.control_protocol must be full-control-v2"
        )
    error = _exact_object(
        raw["error"], {"code", "message", "retryable", "details"},
        "error_response.error",
    )
    if error["code"] not in ERROR_CODES:
        raise FullControlSchemaError(
            f"error_response.error.code must be one of {sorted(ERROR_CODES)}"
        )
    if (
        not isinstance(error["message"], str)
        or not error["message"].strip()
        or len(error["message"]) > 500
    ):
        raise FullControlSchemaError(
            "error_response.error.message must be a non-empty string of at most 500 characters"
        )
    if not isinstance(error["retryable"], bool):
        raise FullControlSchemaError(
            "error_response.error.retryable must be a boolean"
        )
    details = _json_value(error["details"], "error_response.error.details")
    if not isinstance(details, dict):
        raise FullControlSchemaError(
            "error_response.error.details must be an object"
        )
    state_revision = raw["state_revision"]
    return {
        "schema_version": FULL_CONTROL_SCHEMA_VERSION,
        "control_protocol": FULL_CONTROL_V2,
        "error": {
            "code": error["code"],
            "message": error["message"].strip(),
            "retryable": error["retryable"],
            "details": details,
        },
        "state_revision": (
            None if state_revision is None
            else validate_state_revision(state_revision)
        ),
    }


def structured_error(
    code: str,
    message: str,
    *,
    retryable: bool,
    details: dict[str, Any] | None = None,
    state_revision: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Build and validate one full-control-v2 error envelope."""
    return validate_structured_error({
        "schema_version": FULL_CONTROL_SCHEMA_VERSION,
        "control_protocol": FULL_CONTROL_V2,
        "error": {
            "code": code,
            "message": message,
            "retryable": retryable,
            "details": details or {},
        },
        "state_revision": state_revision,
    })


def validate_rejection(value: Any) -> dict[str, Any]:
    """Validate one closed refusal attribution carried on a rejected receipt.

    Every field is drawn from a server-authored closed vocabulary.  ``layer``
    and ``reason`` are ours; ``native_code`` is the mapped sidecar error code
    and ``native_reason`` the native result reason, both already canonical
    tokens rather than the native boundary's free prose.  Nothing here can
    carry a native handle, a command argument, a path, or exception text, so a
    rejection is safe to persist durably and to return to the agent.
    """
    raw = _exact_object(
        value,
        {"layer", "reason", "native_code", "native_reason"},
        "rejection",
    )
    if raw["layer"] not in REJECTION_LAYERS:
        raise FullControlSchemaError(
            f"rejection.layer must be one of {sorted(REJECTION_LAYERS)}"
        )
    if raw["reason"] not in REJECTION_REASONS:
        raise FullControlSchemaError(
            f"rejection.reason must be one of {sorted(REJECTION_REASONS)}"
        )
    native_code = raw["native_code"]
    if native_code is not None and (
        not isinstance(native_code, str)
        or _NATIVE_CODE.fullmatch(native_code) is None
    ):
        raise FullControlSchemaError(
            "rejection.native_code must be a lowercase token or null"
        )
    native_reason = raw["native_reason"]
    if native_reason is not None and (
        not isinstance(native_reason, str)
        or _NATIVE_REASON.fullmatch(native_reason) is None
    ):
        raise FullControlSchemaError(
            "rejection.native_reason must be an uppercase token or null"
        )
    if raw["layer"] not in {"native_preflight", "native_dispatch"} and (
        native_code is not None or native_reason is not None
    ):
        raise FullControlSchemaError(
            "only a native rejection layer may carry native attribution"
        )
    return {
        "layer": raw["layer"],
        "reason": raw["reason"],
        "native_code": native_code,
        "native_reason": native_reason,
    }


def rejection(
    layer: str,
    reason: str,
    *,
    native_code: str | None = None,
    native_reason: str | None = None,
) -> dict[str, Any]:
    """Build one validated refusal attribution."""
    return validate_rejection({
        "layer": layer,
        "reason": reason,
        "native_code": native_code,
        "native_reason": native_reason,
    })


def rejection_message(value: dict[str, Any]) -> str:
    """Return the server-authored sentence for one validated rejection."""
    clean = validate_rejection(value)
    message = _REJECTION_MESSAGES[clean["reason"]]
    if clean["native_reason"] is not None:
        message = f"{message} (native result: {clean['native_reason']})"
    elif clean["native_code"] is not None:
        message = f"{message} (native code: {clean['native_code']})"
    return message[:500]


def receipt_rejection(value: Any) -> dict[str, Any] | None:
    """Extract the validated rejection carried by a structured error, if any.

    Returns ``None`` when the error carries no attribution; raises when it
    carries one that does not validate, so a malformed attribution can never
    be mistaken for an absent one.
    """
    if not isinstance(value, dict):
        return None
    error = value.get("error")
    if not isinstance(error, dict):
        return None
    details = error.get("details")
    if not isinstance(details, dict) or "rejection" not in details:
        return None
    return validate_rejection(details["rejection"])


def validate_city_investigation_observation(value: Any) -> dict[str, Any]:
    """Validate the bounded, immutable CITY_INFO capture on an applied receipt."""
    raw = _exact_object(
        value,
        {
            "id", "type", "source", "freshness", "state_revision", "city",
        },
        "city_investigation_observation",
    )
    if raw["type"] != "city_investigation":
        raise FullControlSchemaError(
            "city_investigation_observation.type must be city_investigation"
        )
    if raw["source"] != "human_client_city_info":
        raise FullControlSchemaError(
            "city_investigation_observation.source must be human_client_city_info"
        )
    if raw["freshness"] != "captured_at_receipt_revision":
        raise FullControlSchemaError(
            "city_investigation_observation.freshness must be "
            "captured_at_receipt_revision"
        )
    city = _exact_object(
        raw["city"],
        {
            "id", "name", "size", "production", "shields",
            "improvements", "citizens",
        },
        "city_investigation_observation.city",
    )
    if (
        not isinstance(city["name"], str) or not city["name"]
        or len(city["name"].encode("utf-8")) > 1024
    ):
        raise FullControlSchemaError(
            "city_investigation_observation.city.name must be non-empty"
        )
    size = _non_negative_int(
        city["size"], "city_investigation_observation.city.size",
    )
    if size == 0:
        raise FullControlSchemaError(
            "city_investigation_observation.city.size must be positive"
        )
    production = _exact_object(
        city["production"], {"id", "kind", "name"},
        "city_investigation_observation.city.production",
    )
    if production["kind"] not in {"unit", "improvement"}:
        raise FullControlSchemaError(
            "city_investigation_observation.city.production.kind is invalid"
        )
    if (
        not isinstance(production["name"], str) or not production["name"]
        or len(production["name"].encode("utf-8")) > 1024
    ):
        raise FullControlSchemaError(
            "city_investigation_observation.city.production.name must be non-empty"
        )
    shields = _exact_object(
        city["shields"], {"stock", "surplus"},
        "city_investigation_observation.city.shields",
    )
    stock = _non_negative_int(
        shields["stock"], "city_investigation_observation.city.shields.stock",
    )
    surplus = shields["surplus"]
    if isinstance(surplus, bool) or not isinstance(surplus, int):
        raise FullControlSchemaError(
            "city_investigation_observation.city.shields.surplus must be an integer"
        )
    improvements = city["improvements"]
    if not isinstance(improvements, list) or len(improvements) > 1024:
        raise FullControlSchemaError(
            "city_investigation_observation.city.improvements must be a bounded array"
        )
    clean_improvements: list[dict[str, str]] = []
    for index, item in enumerate(improvements):
        improvement = _exact_object(
            item, {"id", "name"},
            f"city_investigation_observation.city.improvements[{index}]",
        )
        name = improvement["name"]
        if (
            not isinstance(name, str) or not name
            or len(name.encode("utf-8")) > 1024
        ):
            raise FullControlSchemaError(
                "city investigation improvement names must be non-empty"
            )
        clean_improvements.append({
            "id": _opaque_id(improvement["id"], "investigation improvement id"),
            "name": name,
        })
    if (
        len({item["id"] for item in clean_improvements})
        != len(clean_improvements)
        or len({item["name"] for item in clean_improvements})
           != len(clean_improvements)
    ):
        raise FullControlSchemaError(
            "city investigation improvements must be unique"
        )
    citizens = _exact_object(
        city["citizens"], {"feelings", "specialists"},
        "city_investigation_observation.city.citizens",
    )
    feelings = citizens["feelings"]
    expected_stages = (
        "base", "luxury", "effects", "nationality", "martial_law", "final",
    )
    if not isinstance(feelings, list) or len(feelings) != len(expected_stages):
        raise FullControlSchemaError(
            "city investigation feelings must contain all six stages"
        )
    clean_feelings: list[dict[str, Any]] = []
    for index, item in enumerate(feelings):
        feeling = _exact_object(
            item, {"stage", "happy", "content", "unhappy", "angry"},
            f"city_investigation_observation.city.citizens.feelings[{index}]",
        )
        if feeling["stage"] != expected_stages[index]:
            raise FullControlSchemaError(
                "city investigation feelings must use canonical stage order"
            )
        clean_feelings.append({
            "stage": feeling["stage"],
            **{
                key: _non_negative_int(
                    feeling[key], f"city investigation feelings {key}",
                )
                for key in ("happy", "content", "unhappy", "angry")
            },
        })
    specialists = citizens["specialists"]
    if not isinstance(specialists, list) or len(specialists) > 256:
        raise FullControlSchemaError(
            "city investigation specialists must be a bounded array"
        )
    clean_specialists: list[dict[str, Any]] = []
    for index, item in enumerate(specialists):
        specialist = _exact_object(
            item, {"id", "name", "count"},
            f"city_investigation_observation.city.citizens.specialists[{index}]",
        )
        name = specialist["name"]
        if (
            not isinstance(name, str) or not name
            or len(name.encode("utf-8")) > 1024
        ):
            raise FullControlSchemaError(
                "city investigation specialist names must be non-empty"
            )
        clean_specialists.append({
            "id": _opaque_id(specialist["id"], "investigation specialist id"),
            "name": name,
            "count": _non_negative_int(
                specialist["count"], "investigation specialist count",
            ),
        })
    if (
        len({item["id"] for item in clean_specialists})
        != len(clean_specialists)
        or len({item["name"] for item in clean_specialists})
           != len(clean_specialists)
    ):
        raise FullControlSchemaError(
            "city investigation specialists must be unique"
        )
    specialist_population = sum(item["count"] for item in clean_specialists)
    if any(
        sum(item[key] for key in ("happy", "content", "unhappy", "angry"))
        + specialist_population != size
        for item in clean_feelings
    ):
        raise FullControlSchemaError(
            "city investigation citizen counts must equal city size"
        )
    return {
        "id": _opaque_id(raw["id"], "city_investigation_observation.id"),
        "type": raw["type"],
        "source": raw["source"],
        "freshness": raw["freshness"],
        "state_revision": validate_state_revision(raw["state_revision"]),
        "city": {
            "id": _opaque_id(city["id"], "city_investigation_observation.city.id"),
            "name": city["name"],
            "size": size,
            "production": {
                "id": _opaque_id(
                    production["id"],
                    "city_investigation_observation.city.production.id",
                ),
                "kind": production["kind"],
                "name": production["name"],
            },
            "shields": {"stock": stock, "surplus": surplus},
            "improvements": clean_improvements,
            "citizens": {
                "feelings": clean_feelings,
                "specialists": clean_specialists,
            },
        },
    }


def validate_command_receipt(value: Any) -> dict[str, Any]:
    raw = _exact_object(
        value,
        {
            "schema_version", "control_protocol", "game_id", "agent_id",
            "batch_id", "receipt_state", "idempotent", "state_revision",
            "error", "observation",
        },
        "command_receipt",
    )
    if raw["schema_version"] != FULL_CONTROL_SCHEMA_VERSION:
        raise FullControlSchemaError("command_receipt.schema_version must be 2")
    if raw["control_protocol"] != FULL_CONTROL_V2:
        raise FullControlSchemaError(
            "command_receipt.control_protocol must be full-control-v2"
        )
    if raw["receipt_state"] not in RECEIPT_STATES:
        raise FullControlSchemaError(
            f"command_receipt.receipt_state must be one of {sorted(RECEIPT_STATES)}"
        )
    if not isinstance(raw["idempotent"], bool):
        raise FullControlSchemaError(
            "command_receipt.idempotent must be a boolean"
        )
    state_revision = validate_state_revision(raw["state_revision"])
    observation = raw["observation"]
    if observation is not None:
        observation = validate_city_investigation_observation(observation)
        if (
            raw["receipt_state"] != "applied"
            or observation["state_revision"] != state_revision
        ):
            raise FullControlSchemaError(
                "only an applied receipt may contain an observation at its revision"
            )
    error = raw["error"]
    if error is not None:
        error = validate_structured_error(error)
    terminal_error_state = raw["receipt_state"] in {"ambiguous", "rejected"}
    if terminal_error_state and error is None:
        raise FullControlSchemaError(
            "an ambiguous or rejected command receipt must contain a structured error"
        )
    if not terminal_error_state and error is not None:
        raise FullControlSchemaError(
            "only an ambiguous or rejected command receipt may contain an error"
        )
    if terminal_error_state and (
        error["state_revision"] is None
        or error["state_revision"] != state_revision
    ):
        raise FullControlSchemaError(
            "an ambiguous or rejected receipt's structured error must contain the same "
            "non-null state_revision as the receipt"
        )
    if raw["receipt_state"] == "ambiguous" and (
        error["error"]["code"] != "action_outcome_ambiguous"
        or error["error"]["retryable"]
    ):
        raise FullControlSchemaError(
            "an ambiguous receipt must contain a nonretryable "
            "action_outcome_ambiguous error"
        )
    if (
        raw["receipt_state"] == "rejected"
        and error["error"]["code"] == "action_outcome_ambiguous"
    ):
        raise FullControlSchemaError(
            "an action_outcome_ambiguous error requires an ambiguous receipt"
        )
    if raw["receipt_state"] == "rejected":
        # A refusal an agent cannot attribute to a layer is a contract
        # violation, not a receipt.  Fail closed here so no path can mint a
        # bare illegal_action again.
        attribution = receipt_rejection(error)
        if attribution is None:
            raise FullControlSchemaError(
                "a rejected command receipt must attribute its refusal in "
                "error.details.rejection"
            )
        error["error"]["details"]["rejection"] = attribution
    return {
        "schema_version": FULL_CONTROL_SCHEMA_VERSION,
        "control_protocol": FULL_CONTROL_V2,
        "game_id": _opaque_id(raw["game_id"], "command_receipt.game_id"),
        "agent_id": _opaque_id(raw["agent_id"], "command_receipt.agent_id"),
        "batch_id": _opaque_id(raw["batch_id"], "command_receipt.batch_id"),
        "receipt_state": raw["receipt_state"],
        "idempotent": raw["idempotent"],
        "state_revision": state_revision,
        "error": error,
        "observation": observation,
    }
