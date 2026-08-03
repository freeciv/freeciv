"""Optional deterministic bot implemented strictly as a public API client."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .actions import deterministic_action
from .client import ClientError, load_private_json, next_turn, submit_action


def run_bot(session_path: str | Path) -> dict[str, object]:
    session = load_private_json(session_path)
    after_turn = 0
    decisions = 0
    while True:
        current = next_turn(session, after_turn=after_turn, wait_s=30)
        if current.get("state") in {"completed", "invalid", "failed", "cancelled"}:
            return {
                "state": current["state"],
                "game_id": session["game_id"],
                "decisions": decisions,
                "last_turn": after_turn,
            }
        if "observation" not in current:
            continue
        action = deterministic_action(current["observation"])
        submit_action(
            session, current["turn"], current["observation_id"], action,
            telemetry={"client": "agent_eval.bot", "policy": "deterministic"},
        )
        after_turn = current["turn"]
        decisions += 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Run the optional deterministic public-API bot",
    )
    parser.add_argument("--session", required=True)
    args = parser.parse_args(argv)
    try:
        print(json.dumps(run_bot(args.session), sort_keys=True))
        return 0
    except (ClientError, KeyError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
