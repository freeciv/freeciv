"""The watchdog's one invariant: an idle turn-holder is a stall."""

from __future__ import annotations

import unittest

from agent_eval.play_watchdog import (
    IDLE_CHECKS_BEFORE_PROMPT,
    Player,
    decide,
)


def status(phase_state="running", outcome="pending", **seat_states):
    return {
        "outcome": {"status": outcome},
        "phase": {
            "state": phase_state,
            "controllers": [
                {"controller_label": label, "state": state}
                for label, state in seat_states.items()
            ],
        },
    }


def players():
    return [
        Player(label="pi-x", herdr_name="pi-x", workspace="/w/pi"),
        Player(label="cl-y", herdr_name="cl-y", workspace="/w/cl"),
    ]


class DecideTests(unittest.TestCase):
    def test_idle_turn_holder_is_prompted_after_the_streak(self):
        seats = players()
        args = (
            status(**{"pi-x": "awaiting_agent", "cl-y": "inactive_done"}),
            {"pi-x": "idle", "cl-y": "idle"},
            seats,
        )
        first = decide(*args, 100.0)
        self.assertEqual(first.prompt, [])
        second = decide(*args, 110.0)
        self.assertEqual([p.label for p in second.prompt], ["pi-x"])

    def test_a_working_holder_resets_the_streak(self):
        seats = players()
        stalled = status(**{"pi-x": "awaiting_agent", "cl-y": "inactive_done"})
        decide(stalled, {"pi-x": "idle", "cl-y": "idle"}, seats, 100.0)
        decide(stalled, {"pi-x": "working", "cl-y": "idle"}, seats, 110.0)
        third = decide(stalled, {"pi-x": "idle", "cl-y": "idle"}, seats, 120.0)
        self.assertEqual(third.prompt, [])

    def test_only_the_holder_is_pushed_when_both_idle(self):
        seats = players()
        stalled = status(**{"cl-y": "awaiting_agent", "pi-x": "inactive_done"})
        states = {"pi-x": "idle", "cl-y": "idle"}
        for tick in range(IDLE_CHECKS_BEFORE_PROMPT):
            verdict = decide(stalled, states, seats, 100.0 + tick * 10)
        self.assertEqual([p.label for p in verdict.prompt], ["cl-y"])

    def test_the_cooldown_prevents_spam(self):
        seats = players()
        stalled = status(**{"pi-x": "awaiting_agent", "cl-y": "inactive_done"})
        states = {"pi-x": "idle", "cl-y": "idle"}
        decide(stalled, states, seats, 100.0)
        verdict = decide(stalled, states, seats, 110.0)
        self.assertEqual([p.label for p in verdict.prompt], ["pi-x"])
        seats[0].last_prompt_at = 110.0
        seats[0].idle_streak = 0
        decide(stalled, states, seats, 120.0)
        soon = decide(stalled, states, seats, 130.0)
        self.assertEqual(soon.prompt, [])
        later = decide(stalled, states, seats, 171.0)
        self.assertEqual([p.label for p in later.prompt], ["pi-x"])

    def test_a_blocked_holder_is_never_prompted(self):
        seats = players()
        stalled = status(**{"pi-x": "awaiting_agent", "cl-y": "inactive_done"})
        states = {"pi-x": "blocked", "cl-y": "idle"}
        for tick in range(IDLE_CHECKS_BEFORE_PROMPT + 1):
            verdict = decide(stalled, states, seats, 100.0 + tick * 10)
        self.assertEqual(verdict.prompt, [])

    def test_lobby_pushes_every_unready_seat(self):
        seats = players()
        lobby = status(
            phase_state="synchronizing",
            **{"pi-x": "synchronizing", "cl-y": "ready"},
        )
        states = {"pi-x": "idle", "cl-y": "idle"}
        decide(lobby, states, seats, 100.0)
        verdict = decide(lobby, states, seats, 110.0)
        self.assertEqual([p.label for p in verdict.prompt], ["pi-x"])

    def test_a_terminal_game_stops_the_watchdog(self):
        seats = players()
        done = status(outcome="complete", **{"pi-x": "done", "cl-y": "done"})
        verdict = decide(done, {"pi-x": "idle", "cl-y": "idle"}, seats, 100.0)
        self.assertTrue(verdict.terminal)
        self.assertEqual(verdict.prompt, [])


if __name__ == "__main__":
    unittest.main()
