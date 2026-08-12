"""Deterministic turn watchdog for herdr-hosted matches.

The invariant a healthy PvP match keeps: **someone is always thinking**.  If
every player agent is idle while the game is not terminal, the match has
stalled -- tolerated for at most ~20 seconds (two 10-second polls) — most often because a harness ended its turn after playing one game
turn and nothing re-invoked it.  This watchdog closes that loop without an
LLM: poll the supervisor for whose phase it is, poll herdr for agent states,
and when the turn-holder's agent has been idle for consecutive checks, prompt
it with the exact workspace path and command to run.

Spawned detached by ``just play … --herdr``; exits on its own when the game
is terminal.  Log: ``.agent-eval/watchdog-<game>.log``.
"""

from __future__ import annotations

import argparse
import json
import os
import ssl
import subprocess
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

POLL_INTERVAL_S = 10.0
IDLE_CHECKS_BEFORE_PROMPT = 2
PROMPT_COOLDOWN_S = 60.0

# herdr states that mean "this agent is thinking or needs a human".
BUSY_STATES = frozenset({"working", "blocked"})

TERMINAL_PHASE_STATES = frozenset({"over", "ended", "aborted", "failed"})


@dataclass
class Player:
    """One seat: the supervisor's label, the herdr address, the workspace."""

    label: str
    herdr_name: str
    workspace: str
    idle_streak: int = 0
    last_prompt_at: float = 0.0
    prompts_sent: int = 0


@dataclass
class Verdict:
    """One poll's decision: who to prompt, or why not."""

    prompt: list[Player] = field(default_factory=list)
    terminal: bool = False
    note: str = ""


def _log(message: str) -> None:
    print(f"[{time.strftime('%Y-%m-%dT%H:%M:%S')}] {message}", flush=True)


# ---------------------------------------------------------------------------
# The pure decision
# ---------------------------------------------------------------------------

def decide(
    status: dict,
    agent_states: dict[str, str],
    players: list[Player],
    now: float,
    *,
    idle_checks: int = IDLE_CHECKS_BEFORE_PROMPT,
    cooldown_s: float = PROMPT_COOLDOWN_S,
) -> Verdict:
    """Who, if anyone, should be prompted this poll.

    Pure: mutates only the players' streak/cooldown bookkeeping, reads the
    clock it is handed.  The rule mirrors the user's invariant directly —
    while the game is not done, an idle turn-holder is a stall.
    """
    outcome = status.get("outcome") or {}
    phase = status.get("phase") or {}
    phase_state = str(phase.get("state") or "")
    if outcome.get("status") not in (None, "pending") or (
        phase_state in TERMINAL_PHASE_STATES
    ):
        return Verdict(terminal=True, note=f"game is terminal ({phase_state})")

    controllers = phase.get("controllers") or []
    by_label = {player.label: player for player in players}
    # The turn-holder: the seat the supervisor says it is waiting on.  In the
    # lobby / synchronizing there may be no awaiting_agent seat, in which case
    # every non-done seat is expected to be getting itself ready.
    holders = [
        by_label[str(entry.get("controller_label"))]
        for entry in controllers
        if str(entry.get("state")) == "awaiting_agent"
        and str(entry.get("controller_label")) in by_label
    ]
    if not holders and phase_state not in ("running",):
        holders = [
            by_label[str(entry.get("controller_label"))]
            for entry in controllers
            if str(entry.get("state")) not in ("ready", "inactive_done", "done")
            and str(entry.get("controller_label")) in by_label
        ]

    verdict = Verdict(note=f"phase={phase_state or '?'}")
    for player in players:
        state = agent_states.get(player.herdr_name, "unknown")
        if player in holders and state not in BUSY_STATES:
            player.idle_streak += 1
        else:
            player.idle_streak = 0
        if (
            player in holders
            and player.idle_streak >= idle_checks
            and now - player.last_prompt_at >= cooldown_s
            and state != "blocked"
        ):
            verdict.prompt.append(player)
    return verdict


# ---------------------------------------------------------------------------
# The two data sources and the one action
# ---------------------------------------------------------------------------

def fetch_status(url: str, token: str, ca_file: str | None) -> dict | None:
    request = urllib.request.Request(
        url, headers={"Authorization": f"Bearer {token}"},
    )
    context = (
        ssl.create_default_context(cafile=ca_file)
        if ca_file and url.startswith("https://")
        else None
    )
    try:
        with urllib.request.urlopen(request, timeout=15, context=context) as response:
            loaded = json.load(response)
            return loaded if isinstance(loaded, dict) else None
    except (urllib.error.URLError, TimeoutError, ValueError, OSError) as exc:
        _log(f"status fetch failed (transient): {exc}")
        return None


def herdr_agent_states() -> dict[str, str] | None:
    try:
        completed = subprocess.run(
            ("herdr", "agent", "list"),
            capture_output=True, text=True, timeout=30, check=False,
        )
        if completed.returncode != 0:
            _log(f"herdr agent list failed: {completed.stderr.strip()[:200]}")
            return None
        payload = json.loads(completed.stdout)
        agents = payload.get("result", {}).get("agents", [])
        return {
            str(entry.get("name")): str(entry.get("status", "unknown"))
            for entry in agents
            if isinstance(entry, dict) and entry.get("name")
        }
    except (OSError, ValueError, subprocess.TimeoutExpired) as exc:
        _log(f"herdr agent list failed (transient): {exc}")
        return None


def nudge(player: Player, game_id: str, turn: object) -> bool:
    text = (
        f"[automated watchdog] It is your turn in {game_id} "
        f"(turn {turn}). Work from {player.workspace} — cd there first. "
        "Run `./play turn` for the briefing, then order every actor in one "
        'call: `./play do "..." --end --await --brief`. Keep playing until '
        "the game is terminal; do not end your session while the match is on."
    )
    try:
        completed = subprocess.run(
            ("herdr", "agent", "prompt", player.herdr_name, text),
            capture_output=True, text=True, timeout=30, check=False,
        )
        if completed.returncode != 0:
            _log(
                f"prompt to {player.herdr_name} failed: "
                f"{completed.stderr.strip()[:200]}"
            )
            return False
        return True
    except (OSError, subprocess.TimeoutExpired) as exc:
        _log(f"prompt to {player.herdr_name} failed (transient): {exc}")
        return False


# ---------------------------------------------------------------------------
# The loop
# ---------------------------------------------------------------------------

def watch(
    game_id: str,
    players: list[Player],
    *,
    service_url: str,
    admin_token: str,
    ca_file: str | None,
    interval_s: float = POLL_INTERVAL_S,
) -> int:
    status_url = f"{service_url}/v1/games/{game_id}/status"
    _log(
        f"watchdog up for {game_id}: "
        + ", ".join(f"{p.label} -> {p.herdr_name}" for p in players)
    )
    while True:
        status = fetch_status(status_url, admin_token, ca_file)
        states = herdr_agent_states()
        if status is not None and states is not None:
            verdict = decide(status, states, players, time.monotonic())
            if verdict.terminal:
                _log(f"{verdict.note}; watchdog done "
                     f"({sum(p.prompts_sent for p in players)} prompts sent)")
                return 0
            for player in verdict.prompt:
                turn = status.get("current_turn")
                if nudge(player, game_id, turn):
                    player.last_prompt_at = time.monotonic()
                    player.idle_streak = 0
                    player.prompts_sent += 1
                    _log(
                        f"prompted {player.herdr_name} (turn {turn}, "
                        f"prompt #{player.prompts_sent})"
                    )
        time.sleep(interval_s)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="play_watchdog")
    parser.add_argument("--game-id", required=True)
    parser.add_argument(
        "--player", action="append", required=True,
        metavar="LABEL=HERDR_NAME=WORKSPACE",
        help="one per seat; repeatable",
    )
    parser.add_argument("--interval", type=float, default=POLL_INTERVAL_S)
    args = parser.parse_args(argv)
    players = []
    for spec in args.player:
        label, herdr_name, workspace = spec.split("=", 2)
        players.append(Player(label=label, herdr_name=herdr_name, workspace=workspace))
    service = os.environ.get(
        "AGENT_EVAL_SERVICE_URL", "https://freeciv-api.localhost",
    )
    token = os.environ.get("AGENT_EVAL_ADMIN_TOKEN", "freeciv-local-dev")
    ca_default = Path.home() / ".portless" / "ca.pem"
    ca_file = os.environ.get("AGENT_EVAL_TLS_CA") or (
        str(ca_default) if ca_default.is_file() else None
    )
    return watch(
        args.game_id, players,
        service_url=service, admin_token=token, ca_file=ca_file,
        interval_s=args.interval,
    )


if __name__ == "__main__":
    raise SystemExit(main())
