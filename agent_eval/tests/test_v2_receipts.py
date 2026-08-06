from __future__ import annotations

import hashlib
import json
import os
import stat
import tempfile
import threading
import unittest
from pathlib import Path
from unittest import mock

from agent_eval.full_control_v2 import rejection, structured_error
from agent_eval.v2_receipts import (
    MAX_RECORD_BYTES,
    RECEIPT_DIRECTORY,
    V2ReceiptConflict,
    V2ReceiptCorrupt,
    V2ReceiptInvalidBatch,
    V2ReceiptInvalidTransition,
    V2ReceiptStore,
    V2ReceiptStoreError,
)


GAME_ID = "game_receipts"
AGENT_ID = "agent_receipts"
REVISION = {"turn": 4, "revision": 9, "state_token": "state_public_4_9"}


def batch(
    batch_id: str = "batch_one",
    *,
    agent_id: str = AGENT_ID,
    arguments: dict[str, object] | None = None,
) -> dict[str, object]:
    return {
        "schema_version": 2,
        "control_protocol": "full-control-v2",
        "game_id": GAME_ID,
        "agent_id": agent_id,
        "batch_id": batch_id,
        "state_revision": REVISION,
        "commands": [{
            "action_id": "action_public",
            "arguments": arguments or {},
        }],
    }


def receipt(
    batch_id: str,
    state: str,
    *,
    agent_id: str = AGENT_ID,
    revision: dict[str, object] | None = None,
) -> dict[str, object]:
    current_revision = dict(revision or REVISION)
    error = None
    if state == "ambiguous":
        error = structured_error(
            "action_outcome_ambiguous",
            "The accepted action outcome is unknown.",
            retryable=False,
            state_revision=current_revision,
        )
    elif state == "rejected":
        error = structured_error(
            "illegal_action",
            "The action is not legal in the current state.",
            retryable=True,
            details={
                "rejection": rejection(
                    "native_preflight",
                    "native_bad_argument",
                    native_code="native_bad_argument",
                ),
            },
            state_revision=current_revision,
        )
    return {
        "schema_version": 2,
        "control_protocol": "full-control-v2",
        "game_id": GAME_ID,
        "agent_id": agent_id,
        "batch_id": batch_id,
        "receipt_state": state,
        "idempotent": False,
        "state_revision": current_revision,
        "error": error,
        "observation": None,
    }


def record_name(agent_id: str, batch_id: str) -> str:
    digest = hashlib.sha256(
        agent_id.encode("utf-8") + b"\0" + batch_id.encode("utf-8")
    ).hexdigest()
    return digest + ".json"


class V2ReceiptStoreTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.episode = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def open_store(self) -> V2ReceiptStore:
        return V2ReceiptStore(self.episode, game_id=GAME_ID)

    def test_directory_file_modes_and_hashed_agent_scoped_name(self):
        with self.open_store() as store:
            store.reserve(batch("private_batch"))
        directory = self.episode / RECEIPT_DIRECTORY
        self.assertEqual(stat.S_IMODE(directory.stat().st_mode), 0o700)
        names = [path.name for path in directory.iterdir()]
        self.assertEqual(names, [record_name(AGENT_ID, "private_batch")])
        self.assertNotIn(AGENT_ID, names[0])
        self.assertNotIn("private_batch", names[0])
        self.assertEqual(
            stat.S_IMODE((directory / names[0]).stat().st_mode), 0o600,
        )

        with self.open_store() as store:
            store.reserve(batch("private_batch", agent_id="agent_other"))
        self.assertEqual(len(list(directory.glob("*.json"))), 2)

    def test_concurrent_same_hash_creates_exactly_one_reservation(self):
        with self.open_store() as store:
            barrier = threading.Barrier(16)
            results = []
            errors = []

            def reserve() -> None:
                try:
                    barrier.wait()
                    results.append(store.reserve(batch("batch_concurrent")))
                except Exception as exc:  # pragma: no cover - asserted below
                    errors.append(exc)

            threads = [threading.Thread(target=reserve) for _ in range(16)]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join()

            self.assertEqual(errors, [])
            self.assertEqual(sum(value.created for value in results), 1)
            self.assertTrue(all(value.phase == "reserved" for value in results))

            duplicate = next(value for value in results if not value.created)
            with self.assertRaises(V2ReceiptInvalidTransition):
                store.transition(
                    duplicate,
                    receipt("batch_concurrent", "accepted"),
                )

    def test_same_hash_is_idempotent_without_rewrite_and_changed_hash_conflicts(self):
        with self.open_store() as store:
            reserved = store.reserve(batch("batch_retry"))
            accepted = store.transition(
                reserved, receipt("batch_retry", "accepted"),
            )
            path = self.episode / RECEIPT_DIRECTORY / record_name(
                AGENT_ID, "batch_retry",
            )
            before = path.read_bytes()
            duplicate = store.reserve(batch("batch_retry"))
            after = path.read_bytes()
            self.assertFalse(accepted["idempotent"])
            self.assertFalse(duplicate.created)
            self.assertTrue(duplicate.receipt["idempotent"])
            self.assertEqual(before, after)

            duplicate_transition = store.transition(
                duplicate, receipt("batch_retry", "accepted"),
            )
            self.assertTrue(duplicate_transition["idempotent"])
            self.assertEqual(before, path.read_bytes())

            with self.assertRaises(V2ReceiptConflict):
                store.reserve(batch(
                    "batch_retry", arguments={"city_name": "Different"},
                ))

    def test_probe_absent_validates_without_writing(self):
        with self.open_store() as store:
            directory = self.episode / RECEIPT_DIRECTORY
            self.assertIsNone(store.probe(batch("batch_probe_absent")))
            self.assertEqual(list(directory.iterdir()), [])
            with self.assertRaises(V2ReceiptInvalidBatch):
                store.probe({**batch("batch_probe_bad"), "game_id": "game_other"})
            with self.assertRaises(V2ReceiptInvalidBatch):
                store.probe({**batch("batch_probe_bad"), "commands": []})
            surrogate = batch("batch_probe_surrogate")
            surrogate["commands"][0]["arguments"] = {"city_name": "\ud800"}
            with self.assertRaises(V2ReceiptInvalidBatch):
                store.probe(surrogate)
            with self.assertRaises(V2ReceiptInvalidBatch):
                store.reserve(surrogate)
            self.assertEqual(list(directory.iterdir()), [])

    def test_probe_returns_same_hash_terminal_receipt_and_conflicts(self):
        with self.open_store() as store:
            reservation = store.reserve(batch("batch_probe_terminal"))
            store.transition(
                reservation, receipt("batch_probe_terminal", "rejected"),
            )
            path = self.episode / RECEIPT_DIRECTORY / record_name(
                AGENT_ID, "batch_probe_terminal",
            )
            before = path.read_bytes()

            duplicate = store.probe(batch("batch_probe_terminal"))
            self.assertIsNotNone(duplicate)
            self.assertFalse(duplicate.created)
            self.assertEqual(duplicate.phase, "rejected")
            self.assertTrue(duplicate.receipt["idempotent"])
            self.assertEqual(before, path.read_bytes())
            with self.assertRaises(V2ReceiptConflict):
                store.probe(batch(
                    "batch_probe_terminal",
                    arguments={"city_name": "Changed"},
                ))
            self.assertEqual(before, path.read_bytes())

    def test_probe_terminal_duplicate_needs_no_live_control(self):
        first = self.open_store()
        reservation = first.reserve(batch("batch_probe_reload"))
        first.transition(
            reservation, receipt("batch_probe_reload", "ambiguous"),
        )
        first.close()
        with self.open_store() as reloaded:
            duplicate = reloaded.probe(batch("batch_probe_reload"))
            self.assertEqual(duplicate.phase, "ambiguous")
            self.assertTrue(duplicate.receipt["idempotent"])
            self.assertEqual(reloaded.recovered_receipts, ())

    def test_probe_absence_linearizes_before_concurrent_reserve(self):
        with self.open_store() as store:
            entered = threading.Event()
            release = threading.Event()
            original_exists = store._record_exists_locked
            results: dict[str, object] = {}

            def blocking_exists(name: str) -> bool:
                entered.set()
                self.assertTrue(release.wait(timeout=5))
                return original_exists(name)

            def run_probe() -> None:
                results["probe"] = store.probe(batch("batch_probe_race"))

            def run_reserve() -> None:
                results["reserve"] = store.reserve(batch("batch_probe_race"))

            with mock.patch.object(
                store, "_record_exists_locked", side_effect=blocking_exists,
            ):
                probe_thread = threading.Thread(target=run_probe)
                reserve_thread = threading.Thread(target=run_reserve)
                probe_thread.start()
                self.assertTrue(entered.wait(timeout=5))
                reserve_thread.start()
                self.assertTrue(reserve_thread.is_alive())
                release.set()
                probe_thread.join(timeout=5)
                reserve_thread.join(timeout=5)

            self.assertFalse(probe_thread.is_alive())
            self.assertFalse(reserve_thread.is_alive())
            self.assertIsNone(results["probe"])
            self.assertTrue(results["reserve"].created)
            duplicate = store.probe(batch("batch_probe_race"))
            self.assertFalse(duplicate.created)
            self.assertEqual(duplicate.phase, "reserved")

    def test_all_allowed_transitions_and_lookup_copies(self):
        routes = (
            ("reserved_accepted_applied", ("accepted", "applied")),
            ("reserved_accepted_rejected", ("accepted", "rejected")),
            ("reserved_accepted_ambiguous", ("accepted", "ambiguous")),
            ("reserved_rejected", ("rejected",)),
            ("reserved_ambiguous", ("ambiguous",)),
        )
        with self.open_store() as store:
            for batch_id, states in routes:
                reservation = store.reserve(batch(batch_id))
                result = None
                for state in states:
                    result = store.transition(
                        reservation, receipt(batch_id, state),
                    )
                self.assertEqual(result["receipt_state"], states[-1])
                loaded = store.lookup(AGENT_ID, batch_id)
                self.assertEqual(loaded, result)
                loaded["receipt_state"] = "changed"
                self.assertEqual(
                    store.lookup(AGENT_ID, batch_id)["receipt_state"],
                    states[-1],
                )
            self.assertIsNone(store.lookup(AGENT_ID, "batch_missing"))

    def test_invalid_transitions_and_cross_identity_fail_closed(self):
        with self.open_store() as store:
            reservation = store.reserve(batch("batch_invalid"))
            with self.assertRaises(V2ReceiptInvalidTransition):
                store.transition(
                    reservation, receipt("batch_invalid", "applied"),
                )
            with self.assertRaises(V2ReceiptInvalidTransition):
                store.transition(
                    reservation,
                    receipt("batch_invalid", "accepted", agent_id="agent_other"),
                )
            accepted = store.transition(
                reservation, receipt("batch_invalid", "accepted"),
            )
            self.assertEqual(accepted["receipt_state"], "accepted")
            store.transition(reservation, receipt("batch_invalid", "applied"))
            with self.assertRaises(V2ReceiptInvalidTransition):
                store.transition(
                    reservation, receipt("batch_invalid", "rejected"),
                )
            bad = receipt("batch_invalid", "applied")
            bad["idempotent"] = True
            with self.assertRaises(V2ReceiptInvalidTransition):
                store.transition(reservation, bad)

    def test_reservation_validates_batch_and_game_scope(self):
        with self.open_store() as store:
            with self.assertRaises(V2ReceiptInvalidBatch):
                store.reserve({**batch(), "game_id": "game_other"})
            with self.assertRaises(V2ReceiptInvalidBatch):
                store.reserve({**batch(), "commands": []})
            with self.assertRaises(V2ReceiptInvalidBatch):
                store.lookup("../agent", "batch")

    def test_restart_recovers_reserved_and_accepted_as_ambiguous_without_replay(self):
        first = self.open_store()
        reserved = first.reserve(batch("batch_orphan_reserved"))
        accepted = first.reserve(batch("batch_orphan_accepted"))
        first.transition(
            accepted, receipt("batch_orphan_accepted", "accepted"),
        )
        first.close()

        with self.open_store() as recovered:
            self.assertEqual(len(recovered.recovered_receipts), 2)
            for batch_id in ("batch_orphan_reserved", "batch_orphan_accepted"):
                value = recovered.lookup(AGENT_ID, batch_id)
                self.assertEqual(value["receipt_state"], "ambiguous")
                self.assertEqual(
                    value["error"]["error"]["code"],
                    "action_outcome_ambiguous",
                )
                self.assertFalse(value["error"]["error"]["retryable"])
                duplicate = recovered.reserve(batch(batch_id))
                self.assertFalse(duplicate.created)
                self.assertTrue(duplicate.receipt["idempotent"])
                with self.assertRaises(V2ReceiptInvalidTransition):
                    recovered.transition(
                        duplicate, receipt(batch_id, "accepted"),
                    )

        with self.open_store() as reloaded:
            self.assertEqual(reloaded.recovered_receipts, ())
            self.assertEqual(
                reloaded.lookup(AGENT_ID, reserved.batch_id)["receipt_state"],
                "ambiguous",
            )

    def test_terminal_receipt_survives_process_style_reload(self):
        first = self.open_store()
        reservation = first.reserve(batch("batch_reload"))
        first.transition(reservation, receipt("batch_reload", "rejected"))
        expected = first.lookup(AGENT_ID, "batch_reload")
        first.close()
        with self.open_store() as second:
            self.assertEqual(second.recovered_receipts, ())
            self.assertEqual(second.lookup(AGENT_ID, "batch_reload"), expected)

    def test_record_contains_no_command_or_native_secrets_and_is_canonical(self):
        with self.open_store() as store:
            reservation = store.reserve(batch(
                "batch_secrets",
                arguments={"city_name": "TOP_SECRET_CITY"},
            ))
            store.transition(
                reservation, receipt("batch_secrets", "accepted"),
            )
            unsafe = store.reserve(batch("batch_unsafe_error"))
            unsafe_receipt = receipt("batch_unsafe_error", "rejected")
            attribution = rejection(
                "native_dispatch",
                "postcondition_not_met",
                native_reason="POSTCONDITION_NOT_MET",
            )
            unsafe_receipt["error"] = structured_error(
                "illegal_action",
                "native request 91 failed at /private/episode/path",
                retryable=True,
                details={
                    "rejection": attribution,
                    "native_slot": "slot-41",
                    "native_request_id": 91,
                    "join_token": "JOIN_SECRET",
                    "arguments": {"city_name": "TOP_SECRET_CITY"},
                },
                state_revision=REVISION,
            )
            safe = store.transition(unsafe, unsafe_receipt)
            # The refusal attribution is an allowlist of one, re-validated
            # against closed vocabularies. Every other caller-supplied detail
            # and the caller's prose are still dropped.
            self.assertEqual(
                safe["error"]["error"]["details"], {"rejection": attribution},
            )
            self.assertEqual(
                safe["error"]["error"]["message"],
                "The native client dispatched the action and the expected "
                "effect did not take hold; the action had no effect. A "
                "governance proposal that needs a vote, or a setting that "
                "only takes effect at a turn boundary, reports this. "
                "(native result: POSTCONDITION_NOT_MET)",
            )
            # A rejection that is not from the closed vocabulary is refused
            # outright rather than silently downgraded.
            forged = store.reserve(batch("batch_forged_rejection"))
            forged_receipt = receipt("batch_forged_rejection", "rejected")
            forged_receipt["error"] = structured_error(
                "illegal_action",
                "The command was rejected.",
                retryable=True,
                details={
                    "rejection": {
                        "layer": "native_dispatch",
                        "reason": "/private/episode/path",
                        "native_code": None,
                        "native_reason": None,
                    },
                },
                state_revision=REVISION,
            )
            with self.assertRaises(V2ReceiptInvalidTransition):
                store.transition(forged, forged_receipt)
        path = self.episode / RECEIPT_DIRECTORY / record_name(
            AGENT_ID, "batch_secrets",
        )
        raw = path.read_bytes()
        decoded = json.loads(raw)
        self.assertEqual(
            raw,
            json.dumps(
                decoded,
                ensure_ascii=False,
                sort_keys=True,
                separators=(",", ":"),
                allow_nan=False,
            ).encode("utf-8"),
        )
        for forbidden in (
            b"TOP_SECRET_CITY",
            b"action_public",
            b"arguments",
            b"native_slot",
            b"native_request_id",
            b"join_token",
            str(self.episode).encode(),
        ):
            self.assertNotIn(forbidden, raw)
        self.assertLessEqual(len(raw), MAX_RECORD_BYTES)
        unsafe_path = self.episode / RECEIPT_DIRECTORY / record_name(
            AGENT_ID, "batch_unsafe_error",
        )
        unsafe_raw = unsafe_path.read_bytes()
        for forbidden in (
            b"native_slot",
            b"native_request_id",
            b"JOIN_SECRET",
            b"TOP_SECRET_CITY",
            b"/private/episode/path",
        ):
            self.assertNotIn(forbidden, unsafe_raw)

    def test_corrupt_noncanonical_contradictory_and_oversize_records_fail_sanitized(self):
        """One unreadable record poisons its own batch, never the store."""
        cases = (b"not-json", b'{"unexpected":true}\n', b"x" * (MAX_RECORD_BYTES + 1))
        for index, contents in enumerate(cases):
            with self.subTest(index=index):
                episode = self.episode / f"case-{index}"
                episode.mkdir()
                directory = episode / RECEIPT_DIRECTORY
                directory.mkdir(mode=0o700)
                path = directory / record_name(AGENT_ID, "batch_corrupt")
                path.write_bytes(contents)
                path.chmod(0o600)
                with V2ReceiptStore(episode, game_id=GAME_ID) as store:
                    self.assertEqual(
                        store.quarantined_records,
                        ({
                            "record": record_name(AGENT_ID, "batch_corrupt"),
                            "reason": "unreadable_record",
                        },),
                    )
                    for operation in (
                        lambda: store.reserve(batch("batch_corrupt")),
                        lambda: store.probe(batch("batch_corrupt")),
                        lambda: store.lookup(AGENT_ID, "batch_corrupt"),
                    ):
                        with self.assertRaises(V2ReceiptCorrupt) as caught:
                            operation()
                        self.assertNotIn(str(episode), str(caught.exception))
                    # Every other batch is untouched.
                    healthy = store.reserve(batch("batch_healthy"))
                    self.assertTrue(healthy.created)
                    self.assertEqual(
                        store.transition(
                            healthy, receipt("batch_healthy", "accepted"),
                        )["receipt_state"],
                        "accepted",
                    )
                # The evidence stays where it was, so the quarantine is
                # identical on every later restart.
                self.assertEqual(path.read_bytes(), contents)
                with V2ReceiptStore(episode, game_id=GAME_ID) as reopened:
                    self.assertEqual(
                        [item["reason"] for item in reopened.quarantined_records],
                        ["unreadable_record"],
                    )
                    # An accepted record crash-recovers to a terminal
                    # ambiguous receipt: the contract the store owes a batch
                    # whose outcome it stopped owning, unchanged by the
                    # quarantine next to it.
                    self.assertEqual(
                        reopened.lookup(AGENT_ID, "batch_healthy")["receipt_state"],
                        "ambiguous",
                    )
                    self.assertEqual(
                        [
                            item["batch_id"]
                            for item in reopened.recovered_receipts
                        ],
                        ["batch_healthy"],
                    )

        with self.open_store() as store:
            reservation = store.reserve(batch("batch_contradiction"))
            store.transition(
                reservation, receipt("batch_contradiction", "accepted"),
            )
        path = self.episode / RECEIPT_DIRECTORY / record_name(
            AGENT_ID, "batch_contradiction",
        )
        value = json.loads(path.read_text())
        value["phase"] = "applied"
        path.write_text(json.dumps(value, sort_keys=True, separators=(",", ":")))
        path.chmod(0o600)
        with self.open_store() as store:
            with self.assertRaises(V2ReceiptCorrupt):
                store.lookup(AGENT_ID, "batch_contradiction")

    def test_symlink_record_and_symlink_episode_root_are_rejected(self):
        directory = self.episode / RECEIPT_DIRECTORY
        directory.mkdir(mode=0o700)
        outside = self.episode / "outside"
        outside.write_text("secret")
        (directory / record_name(AGENT_ID, "batch_link")).symlink_to(outside)
        with self.open_store() as store:
            with self.assertRaises(V2ReceiptCorrupt):
                store.lookup(AGENT_ID, "batch_link")
            self.assertEqual(
                store.quarantined_records,
                ({
                    "record": record_name(AGENT_ID, "batch_link"),
                    "reason": "unreadable_record",
                },),
            )
        self.assertEqual(outside.read_text(), "secret")

        other = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: other.rmdir())
        link = self.episode / "episode-link"
        link.symlink_to(other, target_is_directory=True)
        with self.assertRaises(V2ReceiptStoreError):
            V2ReceiptStore(link, game_id=GAME_ID)

    def test_foreign_entry_is_noted_without_taking_the_store_down(self):
        directory = self.episode / RECEIPT_DIRECTORY
        directory.mkdir(mode=0o700)
        (directory / ".DS_Store").write_bytes(b"\x00\x01")
        (directory / "notes.txt").write_text("left by a human")
        with self.open_store() as store:
            reasons = [item["reason"] for item in store.quarantined_records]
            self.assertEqual(reasons, ["foreign_entry", "foreign_entry"])
            # A foreign name is never echoed back; it is published as a digest.
            for item in store.quarantined_records:
                self.assertRegex(item["record"], r"^[0-9a-f]{64}$")
                self.assertNotIn("notes", item["record"])
            reservation = store.reserve(batch("batch_after_foreign"))
            self.assertTrue(reservation.created)
            self.assertEqual(
                store.transition(
                    reservation, receipt("batch_after_foreign", "accepted"),
                )["receipt_state"],
                "accepted",
            )

    def test_unreapable_temporary_is_noted_and_the_store_still_opens(self):
        directory = self.episode / RECEIPT_DIRECTORY
        directory.mkdir(mode=0o700)
        temporary = directory / ("." + "c" * 64 + "." + "d" * 32 + ".tmp")
        temporary.write_text("partial")
        temporary.chmod(0o600)
        with mock.patch(
            "agent_eval.v2_receipts.os.unlink",
            side_effect=OSError("injected private path"),
        ):
            store = self.open_store()
        with store:
            self.assertEqual(
                [item["reason"] for item in store.quarantined_records],
                ["temporary_retained"],
            )
            reservation = store.reserve(batch("batch_after_temp"))
            self.assertTrue(reservation.created)

    def test_undurable_recovery_poisons_only_that_batch(self):
        """A reserved record that cannot be promoted is never served again."""
        with self.open_store() as store:
            store.reserve(batch("batch_unpromotable"))
            healthy = store.reserve(batch("batch_promotable"))
            store.transition(healthy, receipt("batch_promotable", "accepted"))
        poisoned = record_name(AGENT_ID, "batch_unpromotable")
        real_replace = os.replace

        def replace(source, target, **kwargs):
            if isinstance(target, str) and target == poisoned:
                raise OSError("injected private path")
            return real_replace(source, target, **kwargs)

        with mock.patch("agent_eval.v2_receipts.os.replace", replace):
            store = self.open_store()
        with store:
            self.assertEqual(
                store.quarantined_records,
                ({"record": poisoned, "reason": "recovery_not_durable"},),
            )
            # The un-promoted reservation is unusable, so the command behind
            # it can never be dispatched a second time.
            for operation in (
                lambda: store.reserve(batch("batch_unpromotable")),
                lambda: store.probe(batch("batch_unpromotable")),
                lambda: store.lookup(AGENT_ID, "batch_unpromotable"),
            ):
                with self.assertRaises(V2ReceiptCorrupt) as caught:
                    operation()
                self.assertNotIn("private path", str(caught.exception))
            # The batch beside it still recovered to its terminal receipt.
            self.assertEqual(
                store.lookup(AGENT_ID, "batch_promotable")["receipt_state"],
                "ambiguous",
            )
            self.assertEqual(
                [item["batch_id"] for item in store.recovered_receipts],
                ["batch_promotable"],
            )
            self.assertTrue(store.reserve(batch("batch_fresh")).created)

    def test_orphan_temp_is_removed_and_replace_failure_cleans_temp(self):
        directory = self.episode / RECEIPT_DIRECTORY
        directory.mkdir(mode=0o700)
        temporary = directory / ("." + "a" * 64 + "." + "b" * 32 + ".tmp")
        temporary.write_text("partial")
        temporary.chmod(0o600)
        with self.open_store() as store:
            self.assertFalse(temporary.exists())
            reservation = store.reserve(batch("batch_replace_failure"))
            with mock.patch(
                "agent_eval.v2_receipts.os.replace",
                side_effect=OSError("injected private path"),
            ):
                with self.assertRaises(V2ReceiptStoreError) as caught:
                    store.transition(
                        reservation,
                        receipt("batch_replace_failure", "accepted"),
                    )
            self.assertNotIn("private path", str(caught.exception))
            names = [path.name for path in directory.iterdir()]
            self.assertEqual(names, [record_name(AGENT_ID, "batch_replace_failure")])
            duplicate = store.reserve(batch("batch_replace_failure"))
            self.assertEqual(duplicate.phase, "reserved")

    def test_deleted_record_races_fail_as_sanitized_corruption(self):
        with self.open_store() as store:
            reservation = store.reserve(batch("batch_deleted"))
            path = self.episode / RECEIPT_DIRECTORY / record_name(
                AGENT_ID, "batch_deleted",
            )
            path.unlink()
            with self.assertRaises(V2ReceiptCorrupt) as caught:
                store.transition(
                    reservation, receipt("batch_deleted", "accepted"),
                )
            self.assertNotIn(str(path), str(caught.exception))
            self.assertIsNone(store.lookup(AGENT_ID, "batch_deleted"))

            probe_reservation = store.reserve(batch("batch_probe_deleted"))
            probe_path = self.episode / RECEIPT_DIRECTORY / record_name(
                AGENT_ID, probe_reservation.batch_id,
            )
            original_exists = store._record_exists_locked

            def delete_after_exists(name: str) -> bool:
                exists = original_exists(name)
                probe_path.unlink()
                return exists

            with mock.patch.object(
                store,
                "_record_exists_locked",
                side_effect=delete_after_exists,
            ):
                with self.assertRaises(V2ReceiptCorrupt):
                    store.probe(batch("batch_probe_deleted"))

    def test_initial_reservation_uses_fsynced_temp_and_no_overwrite_link(self):
        with self.open_store() as store:
            events: list[str] = []
            real_fsync = os.fsync
            real_link = os.link
            real_unlink = os.unlink

            def traced_fsync(fd: int) -> None:
                events.append("fsync")
                real_fsync(fd)

            def traced_link(*args, **kwargs) -> None:
                events.append("link")
                real_link(*args, **kwargs)

            def traced_unlink(*args, **kwargs) -> None:
                events.append("unlink")
                real_unlink(*args, **kwargs)

            with mock.patch(
                "agent_eval.v2_receipts.os.fsync", side_effect=traced_fsync,
            ), mock.patch(
                "agent_eval.v2_receipts.os.link", side_effect=traced_link,
            ), mock.patch(
                "agent_eval.v2_receipts.os.unlink", side_effect=traced_unlink,
            ):
                store.reserve(batch("batch_publish_order"))
            self.assertEqual(events, ["fsync", "link", "unlink", "fsync"])

            directory = self.episode / RECEIPT_DIRECTORY
            with mock.patch(
                "agent_eval.v2_receipts.os.link",
                side_effect=OSError("private link failure"),
            ):
                with self.assertRaises(V2ReceiptStoreError):
                    store.reserve(batch("batch_unpublished"))
            self.assertFalse(
                (directory / record_name(AGENT_ID, "batch_unpublished")).exists(),
            )
            self.assertFalse(any(path.suffix == ".tmp" for path in directory.iterdir()))

            def partial_write(fd: int, _data: bytes) -> None:
                os.write(fd, b'{"partial"')
                raise OSError("simulated crash before publish")

            with mock.patch.object(
                V2ReceiptStore, "_write_all", side_effect=partial_write,
            ):
                with self.assertRaises(V2ReceiptStoreError):
                    store.reserve(batch("batch_partial"))
            self.assertFalse(
                (directory / record_name(AGENT_ID, "batch_partial")).exists(),
            )
            self.assertFalse(any(path.suffix == ".tmp" for path in directory.iterdir()))

    def test_atomic_replace_orders_file_fsync_replace_directory_fsync(self):
        with self.open_store() as store:
            reservation = store.reserve(batch("batch_ordering"))
            events: list[tuple[str, int | None]] = []
            real_fsync = os.fsync
            real_replace = os.replace

            def traced_fsync(fd: int) -> None:
                events.append(("fsync", fd))
                real_fsync(fd)

            def traced_replace(*args, **kwargs) -> None:
                events.append(("replace", None))
                real_replace(*args, **kwargs)

            with mock.patch(
                "agent_eval.v2_receipts.os.fsync", side_effect=traced_fsync,
            ), mock.patch(
                "agent_eval.v2_receipts.os.replace", side_effect=traced_replace,
            ):
                store.transition(
                    reservation, receipt("batch_ordering", "accepted"),
                )
            kinds = [event[0] for event in events]
            self.assertEqual(kinds, ["fsync", "replace", "fsync"])

    def test_closed_store_fails_with_sanitized_typed_error(self):
        store = self.open_store()
        store.close()
        with self.assertRaises(V2ReceiptStoreError):
            store.reserve(batch("batch_closed"))


if __name__ == "__main__":
    unittest.main()
