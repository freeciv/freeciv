"""Stdlib-only model provider adapters."""

from __future__ import annotations

import json
import os
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Any

from .actions import validate_action
from .config import SeatConfig


class ProviderError(RuntimeError):
    """A provider request or response was unusable."""

    def __init__(
        self,
        message: str,
        *,
        raw_output: str | None = None,
        provider_model: str | None = None,
        response_id: str | None = None,
        input_tokens: int = 0,
        output_tokens: int = 0,
    ):
        super().__init__(message)
        self.raw_output = raw_output
        self.provider_model = provider_model
        self.response_id = response_id
        self.input_tokens = input_tokens
        self.output_tokens = output_tokens


@dataclass(frozen=True)
class ProviderResult:
    action: dict[str, Any]
    input_tokens: int = 0
    output_tokens: int = 0
    provider_model: str | None = None
    response_id: str | None = None
    raw_output: str | None = None


def extract_openai_responses_text(payload: dict[str, Any]) -> str:
    direct = payload.get("output_text")
    if isinstance(direct, str) and direct:
        return direct
    for item in payload.get("output", []):
        if not isinstance(item, dict):
            continue
        for content in item.get("content", []):
            if not isinstance(content, dict):
                continue
            text = content.get("text")
            if isinstance(text, str) and text:
                return text
    raise ProviderError("OpenAI Responses payload contains no output text")


def extract_anthropic_text(payload: dict[str, Any]) -> str:
    for content in payload.get("content", []):
        if isinstance(content, dict) and content.get("type") == "text":
            text = content.get("text")
            if isinstance(text, str) and text:
                return text
    raise ProviderError("Anthropic Messages payload contains no text")


def extract_chat_text(payload: dict[str, Any]) -> str:
    try:
        text = payload["choices"][0]["message"]["content"]
    except (KeyError, IndexError, TypeError) as exc:
        raise ProviderError("Chat Completions payload contains no message") from exc
    if not isinstance(text, str) or not text:
        raise ProviderError("Chat Completions message content is empty")
    return text


def _parse_action_text(text: str) -> dict[str, Any]:
    stripped = text.strip()
    if stripped.startswith("```"):
        lines = stripped.splitlines()
        if len(lines) >= 3:
            stripped = "\n".join(lines[1:-1])
            if stripped.lstrip().startswith("json"):
                stripped = stripped.lstrip()[4:].lstrip()
    try:
        return validate_action(json.loads(stripped))
    except (json.JSONDecodeError, ValueError) as exc:
        raise ProviderError(f"model returned an invalid action: {exc}") from exc


def _prompt(observation: dict[str, Any], instructions: str | None = None) -> str:
    schema = {
        "type": "set_traits",
        "traits": {
            "aggressive": "integer -49..50",
            "builder": "integer -49..50",
            "expansionist": "integer -49..50",
            "trader": "integer -49..50",
        },
    }
    prompt = (
        "You control one Freeciv classic AI seat through four additive strategic "
        "trait targets. Use only the supplied private seat state. Return exactly one "
        "JSON object, with no prose, matching this schema: "
        + json.dumps(schema, separators=(",", ":"))
        + "\nObservation: "
        + json.dumps(observation, sort_keys=True, separators=(",", ":"))
    )
    if instructions:
        prompt = "Seat-specific instructions: " + instructions.strip() + "\n" + prompt
    return prompt


def _post(url: str, payload: dict[str, Any], headers: dict[str, str], timeout: float) -> dict[str, Any]:
    data = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    request = urllib.request.Request(url, data=data, method="POST")
    request.add_header("Content-Type", "application/json")
    for name, value in headers.items():
        request.add_header(name, value)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            result = json.loads(response.read().decode("utf-8"))
    except (OSError, urllib.error.HTTPError, json.JSONDecodeError) as exc:
        raise ProviderError(f"provider request failed: {exc}") from exc
    if not isinstance(result, dict):
        raise ProviderError("provider response must be a JSON object")
    return result


def _api_key(seat: SeatConfig, default_env: str, required: bool = True) -> str:
    env_name = seat.api_key_env or default_env
    value = os.environ.get(env_name, "")
    if required and not value:
        raise ProviderError(f"missing API key environment variable {env_name}")
    return value


def chat_completions_url(base_url: str) -> str:
    base = base_url.rstrip("/")
    if base.endswith("/v1"):
        return base + "/chat/completions"
    return base + "/v1/chat/completions"


def _provider_result(
    result: dict[str, Any],
    text: str,
    *,
    input_tokens: int,
    output_tokens: int,
) -> ProviderResult:
    provider_model = result.get("model") if isinstance(result.get("model"), str) else None
    response_id = result.get("id") if isinstance(result.get("id"), str) else None
    try:
        action = _parse_action_text(text)
    except ProviderError as exc:
        raise ProviderError(
            str(exc), raw_output=text, provider_model=provider_model,
            response_id=response_id, input_tokens=input_tokens,
            output_tokens=output_tokens,
        ) from exc
    return ProviderResult(
        action, input_tokens, output_tokens, provider_model, response_id, text,
    )


def invoke_provider(seat: SeatConfig, observation: dict[str, Any]) -> ProviderResult:
    prompt = _prompt(observation, seat.instructions)
    if seat.type == "openai_responses":
        key = _api_key(seat, "OPENAI_API_KEY")
        result = _post(
            "https://api.openai.com/v1/responses",
            {
                "model": seat.model,
                "input": prompt,
                "max_output_tokens": 256,
                **seat.options,
            },
            {"Authorization": f"Bearer {key}"},
            seat.timeout_s,
        )
        usage = result.get("usage", {})
        return _provider_result(
            result,
            extract_openai_responses_text(result),
            input_tokens=int(usage.get("input_tokens", 0)),
            output_tokens=int(usage.get("output_tokens", 0)),
        )
    if seat.type == "anthropic_messages":
        key = _api_key(seat, "ANTHROPIC_API_KEY")
        result = _post(
            "https://api.anthropic.com/v1/messages",
            {
                "model": seat.model,
                "max_tokens": 256,
                "temperature": 0,
                "messages": [{"role": "user", "content": prompt}],
                **seat.options,
            },
            {"x-api-key": key, "anthropic-version": "2023-06-01"},
            seat.timeout_s,
        )
        usage = result.get("usage", {})
        return _provider_result(
            result,
            extract_anthropic_text(result),
            input_tokens=int(usage.get("input_tokens", 0)),
            output_tokens=int(usage.get("output_tokens", 0)),
        )
    if seat.type == "openai_compatible":
        key = _api_key(seat, "OPENAI_COMPATIBLE_API_KEY", required=False)
        headers = {"Authorization": f"Bearer {key}"} if key else {}
        result = _post(
            chat_completions_url(seat.base_url),
            {
                "model": seat.model,
                "messages": [{"role": "user", "content": prompt}],
                "max_tokens": 256,
                "temperature": 0,
                **seat.options,
            },
            headers,
            seat.timeout_s,
        )
        usage = result.get("usage", {})
        return _provider_result(
            result,
            extract_chat_text(result),
            input_tokens=int(usage.get("prompt_tokens", 0)),
            output_tokens=int(usage.get("completion_tokens", 0)),
        )
    raise ProviderError(f"seat type {seat.type} is not a model provider")
