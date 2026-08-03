import unittest
from unittest.mock import patch

from agent_eval.config import SeatConfig
from agent_eval.config import EvalConfig
from agent_eval.agentd import AgentState
from agent_eval.providers import (
    chat_completions_url,
    extract_anthropic_text,
    extract_chat_text,
    extract_openai_responses_text,
    invoke_provider,
    ProviderResult,
    ProviderError,
)


class ProviderExtractionTests(unittest.TestCase):
    def test_openai_direct_and_nested(self):
        self.assertEqual(extract_openai_responses_text({"output_text": "ok"}), "ok")
        payload = {"output": [{"content": [{"type": "output_text", "text": "nested"}]}]}
        self.assertEqual(extract_openai_responses_text(payload), "nested")

    def test_anthropic(self):
        self.assertEqual(extract_anthropic_text({"content": [{"type": "text", "text": "ok"}]}), "ok")

    def test_chat(self):
        self.assertEqual(extract_chat_text({"choices": [{"message": {"content": "ok"}}]}), "ok")

    def test_vllm_origin_or_v1_base(self):
        self.assertEqual(
            chat_completions_url("http://localhost:8000"),
            "http://localhost:8000/v1/chat/completions",
        )
        self.assertEqual(
            chat_completions_url("http://localhost:8000/v1/"),
            "http://localhost:8000/v1/chat/completions",
        )

    @patch("agent_eval.providers._post")
    def test_provider_metadata_raw_output_and_instructions(self, post):
        raw = '{"type":"set_traits","traits":{"aggressive":1,"builder":2,"expansionist":3,"trader":4}}'
        post.return_value = {
            "id": "response-1", "model": "served-model",
            "choices": [{"message": {"content": raw}}],
            "usage": {"prompt_tokens": 10, "completion_tokens": 4},
        }
        seat = SeatConfig(
            "vllm", "SeatOne", "openai_compatible", model="configured-model",
            base_url="http://localhost:8000/v1", instructions="Prefer trade.",
        )
        result = invoke_provider(seat, {"turn": 1})
        self.assertEqual(result.provider_model, "served-model")
        self.assertEqual(result.response_id, "response-1")
        self.assertEqual(result.raw_output, raw)
        url, payload = post.call_args.args[:2]
        self.assertEqual(url, "http://localhost:8000/v1/chat/completions")
        self.assertIn("Seat-specific instructions: Prefer trade.", payload["messages"][0]["content"])
        self.assertEqual(payload["temperature"], 0)
        self.assertEqual(payload["max_tokens"], 256)

    @patch("agent_eval.providers._post")
    def test_invalid_output_error_preserves_token_spend(self, post):
        post.return_value = {
            "id": "bad-1", "model": "served-model",
            "choices": [{"message": {"content": "not json"}}],
            "usage": {"prompt_tokens": 123, "completion_tokens": 17},
        }
        seat = SeatConfig(
            "vllm", "SeatOne", "openai_compatible", model="configured",
            base_url="http://localhost:8000",
        )
        with self.assertRaises(ProviderError) as caught:
            invoke_provider(seat, {"turn": 1})
        self.assertEqual(caught.exception.input_tokens, 123)
        self.assertEqual(caught.exception.output_tokens, 17)
        self.assertEqual(caught.exception.raw_output, "not json")

    @patch("agent_eval.agentd.invoke_provider")
    def test_agent_trace_records_provider_integrity_fields(self, invoke):
        raw = '{"type":"set_traits","traits":{"aggressive":1,"builder":2,"expansionist":3,"trader":4}}'
        action = {
            "type": "set_traits",
            "traits": {"aggressive": 1, "builder": 2, "expansionist": 3, "trader": 4},
        }
        invoke.return_value = ProviderResult(
            action, 7, 3, "served-model", "response-9", raw
        )
        seat = SeatConfig(
            "model", "SeatOne", "openai_responses", model="configured-model"
        )
        config = EvalConfig(
            1, "test", "classic", 1, (1,),
            (seat, SeatConfig("native", "SeatTwo", "native")),
            {"allow_fallbacks": False, "frame_interval": 5, "frame_zoom": 1},
        )
        state = AgentState(config, "internal", {})
        state.process_turn(
            {"turn": 1, "year": -4000, "observations": [
                {"seat_id": "model", "turn": 1, "year": -4000}
            ]}
        )
        event = state.trace[0]
        self.assertEqual(event["configured_model"], "configured-model")
        self.assertEqual(event["provider_model"], "served-model")
        self.assertEqual(event["provider_response_id"], "response-9")
        self.assertEqual(event["raw_output"], raw)
        self.assertEqual(event["action"], action)

    @patch("agent_eval.agentd.invoke_provider")
    def test_fallback_trace_preserves_failed_output_spend(self, invoke):
        invoke.side_effect = ProviderError(
            "invalid action", raw_output="bad", provider_model="served",
            response_id="bad-2", input_tokens=90, output_tokens=11,
        )
        seat = SeatConfig(
            "model", "SeatOne", "openai_responses", model="configured"
        )
        config = EvalConfig(
            1, "test", "classic", 1, (1,),
            (seat, SeatConfig("native", "SeatTwo", "native")),
            {"allow_fallbacks": False, "agentd_port": 0, "frame_interval": 5, "frame_zoom": 1},
        )
        state = AgentState(config, "internal", {})
        state.process_turn({
            "turn": 1, "year": -4000,
            "observations": [{"seat_id": "model", "turn": 1, "year": -4000}],
        })
        event = state.trace[0]
        self.assertTrue(event["fallback"])
        self.assertEqual(event["input_tokens"], 90)
        self.assertEqual(event["output_tokens"], 11)
        self.assertEqual(event["raw_output"], "bad")


if __name__ == "__main__":
    unittest.main()
