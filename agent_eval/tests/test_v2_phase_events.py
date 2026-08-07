from __future__ import annotations

import json
import os
import stat
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from agent_eval.v2_phase_events import (
    PHASE_END_SOURCES,
    PHASE_EVENT_FILENAME,
    PHASE_EVENT_QUARANTINE_FILENAME,
    V2PhaseEventJournal,
    V2PhaseEventJournalError,
)


class V2PhaseEventJournalTests(unittest.TestCase):
    @staticmethod
    def event(*, turn: int = 3, phase: int = 1, place: int = 1) -> dict:
        return {
            "turn": turn,
            "phase": phase,
            "place": place,
            "seat_id": f"place-{place}",
            "player_name": f"AgentPlace{place}",
            "player_color": "#0067A5" if place == 1 else "#F38400",
            "controller_label": f"harness-model-{place}",
            "controller_type": "external",
            "source": "agent",
            "receipt_state": "applied",
            "resolution": "advanced",
            "deadline_started_at": 1000.0,
            "ended_at": 1002.5,
            "elapsed_s": 2.5,
        }

    def test_append_fsync_mode_reload_and_stable_pagination(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with V2PhaseEventJournal(root) as journal:
                first = journal.append(self.event())
                second = journal.append(
                    self.event(turn=3, phase=2, place=2),
                )
                self.assertEqual((first["sequence"], second["sequence"]), (1, 2))
                page = journal.page(0, 1)
                self.assertEqual(page["items"], [first])
                self.assertEqual(page["next_after_sequence"], 1)
                self.assertTrue(page["has_more"])
            path = root / PHASE_EVENT_FILENAME
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
            lines = path.read_text(encoding="utf-8").splitlines()
            self.assertEqual([json.loads(line)["sequence"] for line in lines], [1, 2])
            with V2PhaseEventJournal(root) as reloaded:
                page = reloaded.page(1, 100)
                self.assertEqual(page["items"], [second])
                self.assertEqual(reloaded.last_for_place(1), first)

    def test_every_phase_end_source_round_trips_and_others_are_refused(self):
        # ``source`` is the only thing that distinguishes a controller that
        # ended its own turn from one that ran out of deadline and from a
        # phase that had nothing left to decide.  Journals written before
        # auto_idle existed carry only the first two and must still reload.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with V2PhaseEventJournal(root) as journal:
                for phase, source in enumerate(
                    sorted(PHASE_END_SOURCES),
                ):
                    record = journal.append(
                        {**self.event(turn=4, phase=phase), "source": source},
                    )
                    self.assertEqual(record["source"], source)
                with self.assertRaises(V2PhaseEventJournalError):
                    journal.append(
                        {**self.event(turn=9), "source": "supervisor"},
                    )
            with V2PhaseEventJournal(root) as reloaded:
                self.assertEqual(
                    [item["source"] for item in reloaded.page(0, 100)["items"]],
                    sorted(PHASE_END_SOURCES),
                )

    def test_duplicate_identity_is_exact_once_and_conflict_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            with V2PhaseEventJournal(directory) as journal:
                first = journal.append(self.event())
                self.assertEqual(journal.append(self.event()), first)
                changed = self.event()
                changed["source"] = "timeout"
                with self.assertRaises(V2PhaseEventJournalError):
                    journal.append(changed)
                self.assertEqual(len(journal.page(0, 100)["items"]), 1)

    def test_append_and_reload_reject_out_of_order_phase_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with V2PhaseEventJournal(root) as journal:
                journal.append(self.event(turn=8, phase=1, place=1))
                with self.assertRaises(V2PhaseEventJournalError):
                    journal.append(self.event(turn=8, phase=0, place=2))

            later = self.event(turn=8, phase=1, place=1)
            later["sequence"] = 1
            earlier = self.event(turn=8, phase=0, place=2)
            earlier["sequence"] = 2
            path = root / PHASE_EVENT_FILENAME
            path.write_text(
                "\n".join(
                    json.dumps(
                        event,
                        ensure_ascii=False,
                        sort_keys=True,
                        separators=(",", ":"),
                        allow_nan=False,
                    )
                    for event in (later, earlier)
                ) + "\n",
                encoding="utf-8",
            )
            os.chmod(path, 0o600)
            # Reload keeps the valid prefix and quarantines the rest: an
            # out-of-order record on disk is a damaged journal, not a reason
            # to refuse to open the game it belongs to.
            with V2PhaseEventJournal(root) as reloaded:
                self.assertEqual(
                    [item["sequence"] for item in reloaded.page(0, 100)["items"]],
                    [1],
                )
                self.assertGreater(reloaded.quarantined_bytes, 0)

    def test_nonterminal_accepted_receipt_cannot_finalize_an_event(self):
        with tempfile.TemporaryDirectory() as directory:
            with V2PhaseEventJournal(directory) as journal:
                event = self.event()
                event["receipt_state"] = "accepted"
                with self.assertRaises(V2PhaseEventJournalError):
                    journal.append(event)
                self.assertEqual(journal.page(0, 100)["items"], [])

    def test_rejected_receipt_can_only_describe_failed_resolution(self):
        with tempfile.TemporaryDirectory() as directory:
            with V2PhaseEventJournal(directory) as journal:
                impossible = self.event()
                impossible["receipt_state"] = "rejected"
                with self.assertRaises(V2PhaseEventJournalError):
                    journal.append(impossible)
                failed = dict(impossible)
                failed["resolution"] = "failed"
                recorded = journal.append(failed)
                self.assertEqual(recorded["receipt_state"], "rejected")
                self.assertEqual(recorded["resolution"], "failed")

    def test_short_write_fails_sanitized(self):
        with tempfile.TemporaryDirectory() as directory:
            with V2PhaseEventJournal(directory) as journal, patch(
                "agent_eval.v2_phase_events.os.write", return_value=1,
            ), self.assertRaisesRegex(
                V2PhaseEventJournalError, "journal is unavailable",
            ):
                journal.append(self.event())

    def test_append_fsync_failure_is_not_silently_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            with V2PhaseEventJournal(directory) as journal, patch(
                "agent_eval.v2_phase_events.os.fsync",
                side_effect=OSError("private filesystem detail"),
            ), self.assertRaisesRegex(
                V2PhaseEventJournalError, "journal is unavailable",
            ) as failed:
                journal.append(self.event())
            self.assertNotIn("private filesystem detail", str(failed.exception))

    def test_corruption_and_noncanonical_records_are_quarantined_on_reload(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with V2PhaseEventJournal(root):
                pass
            path = root / PHASE_EVENT_FILENAME
            path.write_text('{"sequence":1}\n', encoding="utf-8")
            os.chmod(path, 0o600)
            with V2PhaseEventJournal(root) as journal:
                self.assertEqual(journal.page(0, 100)["items"], [])
                self.assertEqual(journal.quarantined_bytes, 15)
                # The journal is usable again from sequence one.
                self.assertEqual(journal.append(self.event())["sequence"], 1)
            quarantine = root / PHASE_EVENT_QUARANTINE_FILENAME
            self.assertEqual(stat.S_IMODE(quarantine.stat().st_mode), 0o600)
            self.assertEqual(
                quarantine.read_text(encoding="utf-8"), '{"sequence":1}\n',
            )
            self.assertEqual(
                [
                    json.loads(line)["sequence"]
                    for line in path.read_text(encoding="utf-8").splitlines()
                ],
                [1],
            )

    def test_torn_final_append_is_repaired_instead_of_bricking_the_journal(self):
        """The turn-66 shape: a process dies mid-append and never restarts."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with V2PhaseEventJournal(root) as journal:
                first = journal.append(self.event(turn=3, phase=1, place=1))
                journal.append(self.event(turn=4, phase=1, place=1))
            path = root / PHASE_EVENT_FILENAME
            lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
            # Keep event one whole, then half of event two and no newline.
            torn = lines[0] + lines[1][: len(lines[1]) // 2]
            path.write_text(torn, encoding="utf-8")
            os.chmod(path, 0o600)
            with V2PhaseEventJournal(root) as journal:
                self.assertEqual(journal.page(0, 100)["items"], [first])
                self.assertEqual(
                    journal.quarantined_bytes, len(torn) - len(lines[0]),
                )
                replacement = journal.append(self.event(turn=4, phase=1, place=1))
                self.assertEqual(replacement["sequence"], 2)
            # The repaired file is a clean, contiguous journal again.
            with V2PhaseEventJournal(root) as reopened:
                self.assertEqual(
                    [
                        item["sequence"]
                        for item in reopened.page(0, 100)["items"]
                    ],
                    [1, 2],
                )
                self.assertEqual(reopened.quarantined_bytes, 0)
            self.assertIn(
                lines[1][: len(lines[1]) // 2],
                (root / PHASE_EVENT_QUARANTINE_FILENAME).read_text(
                    encoding="utf-8",
                ),
            )

    def test_repair_survives_a_filesystem_that_refuses_the_quarantine(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with V2PhaseEventJournal(root) as journal:
                first = journal.append(self.event())
            path = root / PHASE_EVENT_FILENAME
            with path.open("a", encoding="utf-8") as stream:
                stream.write("{not json}\n")
            os.chmod(path, 0o600)
            with patch(
                "agent_eval.v2_phase_events.os.ftruncate",
                side_effect=OSError("injected private detail"),
            ), patch(
                "agent_eval.v2_phase_events.os.pread",
                side_effect=self.pread_only_the_first_read(),
            ):
                journal = V2PhaseEventJournal(root)
            with journal:
                self.assertEqual(journal.page(0, 100)["items"], [first])
                self.assertGreater(journal.quarantined_bytes, 0)

    @staticmethod
    def pread_only_the_first_read():
        """Let the journal read, but fail the forensic copy of the tail."""
        real = os.pread
        state = {"calls": 0}

        def pread(*args, **kwargs):
            state["calls"] += 1
            if state["calls"] > 1:
                raise OSError("injected private detail")
            return real(*args, **kwargs)

        return pread


if __name__ == "__main__":
    unittest.main()
