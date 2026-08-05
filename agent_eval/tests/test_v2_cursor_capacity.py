"""Cursor-registry capacity under the traffic a real seat generates.

A live 16-turn game exhausted the registry with ordinary `legal --actor_id`
enumeration and then could not end its own phase.  These tests pin the three
properties that failure needed: a result that fits in one page reserves
nothing, a newer revision releases the scoped records it just invalidated, and
the enumeration that carries `phase.end` draws on capacity ordinary reads can
never touch.
"""

from __future__ import annotations

import unittest
from unittest import mock

import agent_eval.v2_control as v2_control
from agent_eval.v2_control import V2ControlError, V2SeatControl

from agent_eval.tests.test_v2_control import observation, valid_rows


UNIT_ACTOR_REF = "u:10:100"


def chain_budget(chains, reserve):
    """Shrink the whole chain budget so capacity pressure is reachable."""
    return mock.patch.multiple(
        v2_control,
        MAX_ACTIVE_CURSOR_CHAINS=chains + reserve,
        RESERVED_CATALOG_CHAINS=reserve,
    )


class V2CursorCapacityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.control = V2SeatControl("game_test", "agent_test", 1)

    @staticmethod
    def _scope_page(request, rows, *, total):
        return {
            "generation": 1,
            "native_revision": request.native_revision,
            "actor_ref": request.native_actor_ref,
            "view_id": f"v{request.native_revision}-1",
            "offset": request.offset,
            "count": len(rows),
            "total_count": total,
            "next_offset": request.offset + len(rows),
            "complete": True,
            "overflow": False,
            "rows": tuple(rows),
        }

    def _own_unit_id(self, current):
        return next(
            item["id"]
            for item in self.control.state_page(current, "units")["page"]["items"]
            if item["scope"] == "own"
        )

    def _abandoned_scope_cursor(self, current):
        """Enumerate one actor catalog and walk away, as a rev bump forces."""
        request = self.control.prepare_actor_scope(
            current, self._own_unit_id(current), 1,
        )
        rows = [
            row for row in valid_rows()
            if row.startswith("action ") and f" actor={UNIT_ACTOR_REF} " in row
        ]
        self.assertGreater(len(rows), 1)
        page = self.control.actor_scope_page(
            request, self._scope_page(request, rows[:1], total=len(rows)),
        )
        cursor = page["page"]["next_cursor"]
        self.assertIsNotNone(cursor)
        return cursor

    def _drain(self, first, endpoint):
        items = list(first["page"]["items"])
        cursor = first["page"]["next_cursor"]
        while cursor is not None:
            page = self.control.continue_page(cursor, endpoint=endpoint)
            items.extend(page["page"]["items"])
            cursor = page["page"]["next_cursor"]
        return items

    def test_single_page_result_reserves_no_capacity(self):
        page = self.control.state_page(observation(), "known_tiles", limit=16)
        self.assertIsNone(page["page"]["next_cursor"])
        self.assertEqual(len(self.control._page_chains), 0)
        self.assertEqual(len(self.control._cursors), 0)
        self.assertEqual(len(self.control._retired_page_chains), 0)

    def test_newer_revision_releases_the_scopes_it_invalidated(self):
        cursor = self._abandoned_scope_cursor(observation(revision=11))
        self.assertEqual(len(self.control._cursors), 1)
        self.control.state_page(observation(revision=12))
        self.assertEqual(len(self.control._cursors), 0)
        # Released, not forgotten: the seat still tells an authentic cursor
        # apart from a forged one and names the query to restart.
        with self.assertRaisesRegex(V2ControlError, "stale_revision") as stale:
            self.control.take_actor_scope_cursor(
                cursor, endpoint="legal_actions",
            )
        self.assertEqual(
            stale.exception.details["restart"]["endpoint"], "legal_actions",
        )

    def test_current_revision_scope_cursor_keeps_its_lifetime(self):
        current = observation(revision=11)
        cursor = self._abandoned_scope_cursor(current)
        # A re-read of the same revision is not an advance.
        self.control.state_page(current)
        self.assertIn(cursor, self.control._cursors)
        self.assertTrue(self.control.is_actor_scope_cursor(
            cursor, endpoint="legal_actions",
        ))

    def test_ordinary_chain_still_survives_a_revision_advance(self):
        first = self.control.state_page(
            observation(revision=11), "known_tiles", limit=1,
        )
        cursor = first["page"]["next_cursor"]
        self.control.state_page(observation(revision=12))
        # An ordinary page chain carries its own frozen values, so releasing
        # it would break a traversal that can still complete correctly.
        self.assertEqual(
            self.control.continue_page(cursor, endpoint="state")["page"][
                "section"
            ],
            "known_tiles",
        )

    def test_capacity_refusal_says_when_a_retry_can_succeed(self):
        with chain_budget(1, 1), mock.patch.object(
            v2_control.time, "monotonic", return_value=100.0,
        ), mock.patch.object(v2_control.time, "time", return_value=1_000.0):
            self.control.state_page(observation(), "known_tiles", limit=1)
            with self.assertRaises(V2ControlError) as refused:
                self.control.state_page(observation(), "known_tiles", limit=1)
        self.assertEqual(refused.exception.code, "rate_limited")
        self.assertEqual(
            refused.exception.details["retry_after_seconds"],
            int(v2_control.CURSOR_TTL_SECONDS),
        )
        self.assertEqual(
            refused.exception.details["retry_after"],
            "1970-01-01T00:21:40.000Z",
        )

    def test_phase_end_enumeration_outlives_ordinary_read_pressure(self):
        with chain_budget(2, 2):
            held = [
                self.control.state_page(
                    observation(), "known_tiles", limit=1,
                )["page"]["next_cursor"]
                for _ in range(2)
            ]
            with self.assertRaisesRegex(V2ControlError, "rate_limited"):
                self.control.state_page(observation(), "known_tiles", limit=1)
            catalog = self._drain(
                self.control.legal_actions_page(observation(), limit=1),
                "legal_actions",
            )
        self.assertIn(
            "phase.end", {item["kind"] for item in catalog},
        )
        # Ordinary reads kept every cursor they were promised.
        self.assertEqual(
            self.control.continue_page(held[0], endpoint="state")["page"][
                "section"
            ],
            "known_tiles",
        )

    def test_pressure_reclaims_finished_traversals_and_spares_owed_ones(self):
        with chain_budget(2, 0):
            owed = self.control.state_page(
                observation(), "known_tiles", limit=1,
            )["page"]["next_cursor"]
            finished = self.control.state_page(
                observation(), "known_tiles", limit=1,
            )
            drained = finished["page"]["next_cursor"]
            self._drain(finished, "state")
            # The finished traversal yields its replay so the new caller, who
            # holds no cursor at all, is not refused.
            self.control.state_page(observation(), "known_tiles", limit=1)
            with self.assertRaisesRegex(V2ControlError, "cursor_expired"):
                self.control.continue_page(drained, endpoint="state")
            # The traversal still owing a continuation keeps it.
            self.assertEqual(
                self.control.continue_page(owed, endpoint="state")["page"][
                    "section"
                ],
                "known_tiles",
            )
            with self.assertRaisesRegex(V2ControlError, "rate_limited"):
                self.control.state_page(observation(), "known_tiles", limit=1)

    def test_sixteen_turns_of_scoped_drains_leave_phase_end_reachable(self):
        """Replay the shape of the game that exhausted the registry."""
        with chain_budget(2, 2):
            for revision in range(11, 27):
                current = observation(revision=revision)
                self._abandoned_scope_cursor(current)
                catalog = self._drain(
                    self.control.legal_actions_page(current, limit=1),
                    "legal_actions",
                )
                self.assertIn("phase.end", {item["kind"] for item in catalog})
            self.assertLessEqual(len(self.control._cursors), 1)


if __name__ == "__main__":
    unittest.main()
