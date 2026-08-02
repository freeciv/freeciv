"""Adaptive observe/act driver for one Freeciv strategic-v1 session.

Drives the sanctioned `just next` / `just act` commands so every per-turn
deadline is met, choosing trait modifiers from an adaptive phase policy.

Score model being optimized (Freeciv get_civ_score):
    citizens*1 + happy*1 + techs*2 + wonders*5 + spaceship + built/10 + kills/3
so the levers are: many cities, large and happy populations, and full tech.

Traits are relative priorities, so each phase differentiates rather than
maxing everything.

Usage: python3 -B runner.py --after-turn N --turns 500 --log play.log
"""

import argparse
import json
import subprocess
import sys
import time

TERMINAL = {"completed", "invalid", "failed", "cancelled"}

# (aggressive, builder, expansionist, trader)
PHASES = {
    # Land grab: settlers above all; avoid wars that eat settlers.
    "expand": {"aggressive": -35, "builder": 0, "expansionist": 50, "trader": 20},
    # Land is filling: keep settling, start infrastructure and trade.
    "grow": {"aggressive": -30, "builder": 30, "expansionist": 35, "trader": 35},
    # Land is gone: infrastructure for size/happiness, trade for research.
    "develop": {"aggressive": -25, "builder": 45, "expansionist": 10, "trader": 45},
    # Endgame: tech and wonders; population already near terrain limits.
    "science": {"aggressive": -20, "builder": 50, "expansionist": -10, "trader": 50},
    # Recovery from a wide-but-shallow empire: raise every city's ceiling
    # (size + happiness + science buildings) and stop suppressing military,
    # which left us undefended against default-aggression AIs.
    "boom": {"aggressive": 0, "builder": 50, "expansionist": 15, "trader": 45},
    # Losing cities: stop provoking, build defensive/hardening infrastructure.
    "defend": {"aggressive": -40, "builder": 50, "expansionist": 5, "trader": 25},
}


def run(args, timeout):
    return subprocess.run(args, capture_output=True, text=True, timeout=timeout)


def parse_json(text):
    start = text.find("{")
    if start < 0:
        return None
    try:
        return json.loads(text[start:])
    except json.JSONDecodeError:
        return None


def choose_phase(turn, cities, history, forced):
    """Pick a phase from observed trends. history is a list of (turn, cities, score)."""
    if forced:
        return forced

    # Recent city loss means we are being taken apart; harden first.
    recent = [h for h in history if h[0] >= turn - 12]
    if recent and cities < max(h[1] for h in recent) - 1:
        return "defend"

    # City growth over the last ~40 turns decides expansion value.
    window = [h for h in history if h[0] >= turn - 40]
    if window:
        gained = cities - min(h[1] for h in window)
    else:
        gained = 99

    # Pure land-grab only until a real base exists; builder 0 is too costly
    # to keep once there are cities worth improving.
    if cities < 10 and turn < 150:
        return "expand"
    # Keep settling as long as it is actually producing new cities.
    if gained >= 2:
        return "grow"
    if gained >= 1 or turn < 500:
        return "develop"
    return "science"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--session", required=True)
    ap.add_argument("--after-turn", type=int, required=True)
    ap.add_argument("--turns", type=int, default=500)
    ap.add_argument("--wait-s", type=int, default=120)
    ap.add_argument("--force-phase", default=None, choices=sorted(PHASES))
    ap.add_argument("--log", default="play.log")
    args = ap.parse_args()

    log = open(args.log, "a", buffering=1)
    last_turn = args.after_turn
    played = 0
    history = []
    phase = None
    started = time.monotonic()

    while played < args.turns:
        proc = run(
            ["just", "next", "--session", args.session,
             "--after_turn", str(last_turn),
             "--wait_s", str(args.wait_s)],
            timeout=args.wait_s + 60,
        )
        payload = parse_json(proc.stdout)
        if payload is None:
            msg = f"STOP next-failed rc={proc.returncode} {proc.stderr.strip()[:300]}"
            print(msg); log.write(msg + "\n")
            return 2

        state = payload.get("state")
        if state in TERMINAL:
            msg = f"TERMINAL state={state} turn={payload.get('turn')}"
            print(msg); log.write(msg + "\n")
            return 0
        if state == "waiting" or not payload.get("observation_id"):
            continue

        turn = payload["turn"]
        obs = payload.get("observation", {})
        cities = obs.get("num_cities", 0)
        score = obs.get("civilization_score", 0)

        new_phase = choose_phase(turn, cities, history, args.force_phase)
        if new_phase != phase:
            phase = new_phase
            msg = f"PHASE turn={turn} -> {phase} {PHASES[phase]}"
            print(msg, flush=True); log.write(msg + "\n")

        action = json.dumps({"type": "set_traits", "traits": PHASES[phase]})
        act = run(
            ["just", "act", "--session", args.session, "--turn", str(turn),
             "--observation_id", payload["observation_id"], "--action", action],
            timeout=60,
        )
        if not (parse_json(act.stdout) or {}).get("accepted"):
            msg = f"STOP act-rejected turn={turn} {act.stderr.strip()[:300]}"
            print(msg); log.write(msg + "\n")
            return 2

        line = (
            f"turn={turn} year={obs.get('year')} score={score} cities={cities} "
            f"units={obs.get('num_units')} gold={obs.get('gold')} "
            f"bulbs={obs.get('bulbs')} gov={obs.get('government')} "
            f"research={obs.get('research')} culture={obs.get('culture')} "
            f"phase={phase}"
        )
        log.write(line + "\n")
        if turn % 25 == 0:
            print(line, flush=True)

        if not obs.get("alive", True):
            msg = f"STOP not-alive turn={turn}"
            print(msg); log.write(msg + "\n")
            return 0

        history.append((turn, cities, score))
        history = history[-200:]
        last_turn = turn
        played += 1

    msg = (f"CHUNK-DONE turns={played} last_turn={last_turn} phase={phase} "
           f"elapsed_s={time.monotonic() - started:.0f}")
    print(msg, flush=True); log.write(msg + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
