import json
import os
import stat
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import patch

from agent_eval import v2_ambiguity_trace as trace_module
from agent_eval.v2_ambiguity_trace import (
    TRACE_DIRECTORY,
    TRACE_FILENAME,
    V2AmbiguityTrace,
    V2AmbiguityTraceError,
)


class V2AmbiguityTraceTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)

    def tearDown(self):
        self.directory.cleanup()

    @staticmethod
    def record(trace, *, batch_id="batch_one", reason="processing_timeout"):
        trace.record(
            agent_id="agent_one",
            batch_id=batch_id,
            seat_id="place-1",
            stage="correlated_terminal",
            ambiguity_reason=reason,
            sidecar_generation=3,
            sidecar_health_state="ready",
            acceptance_known=True,
            timestamp="2026-08-02T12:34:56.789Z",
        )

    def test_exact_canonical_private_record_contains_no_free_form_detail(self):
        trace = V2AmbiguityTrace(self.root, game_id="game_one")
        self.record(trace)
        path = self.root / TRACE_DIRECTORY / TRACE_FILENAME
        line = path.read_bytes()
        self.assertTrue(line.endswith(b"\n"))
        value = json.loads(line)
        self.assertEqual(
            set(value),
            {
                "acceptance_known", "agent_id", "ambiguity_reason",
                "batch_id", "format", "game_id", "schema_version",
                "seat_id", "sidecar_generation", "sidecar_health_state",
                "stage", "timestamp",
            },
        )
        self.assertEqual(value["ambiguity_reason"], "processing_timeout")
        self.assertEqual(value["stage"], "correlated_terminal")
        self.assertEqual(
            line,
            json.dumps(
                value, ensure_ascii=True, sort_keys=True,
                separators=(",", ":"), allow_nan=False,
            ).encode("ascii") + b"\n",
        )
        self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
        self.assertEqual(
            stat.S_IMODE((self.root / TRACE_DIRECTORY).stat().st_mode),
            0o700,
        )
        serialized = line.decode("ascii")
        for forbidden in (
            "native_ref", "action_slot", "arguments", "observation",
            "model", "/private/path", "secret-native-detail",
        ):
            self.assertNotIn(forbidden, serialized)

    def test_normalized_schema_rejects_unknown_stage_reason_and_health(self):
        trace = V2AmbiguityTrace(self.root, game_id="game_one")
        cases = (
            {"stage": "private free form"},
            {"ambiguity_reason": "SENSITIVE_/private/native"},
            {"sidecar_health_state": "secret-state"},
            {"sidecar_generation": 0},
            {"acceptance_known": 1},
            {"timestamp": "SENSITIVE free form"},
        )
        base = {
            "agent_id": "agent_one",
            "batch_id": "batch_one",
            "seat_id": "place-1",
            "stage": "post_accept",
            "ambiguity_reason": "result_unavailable",
            "sidecar_generation": 1,
            "sidecar_health_state": "failed",
            "acceptance_known": True,
        }
        for update in cases:
            with self.subTest(update=update):
                with self.assertRaises(V2AmbiguityTraceError):
                    trace.record(**{**base, **update})

    def test_writes_are_serialized_and_each_line_is_canonical(self):
        trace = V2AmbiguityTrace(self.root, game_id="game_one")
        threads = [
            threading.Thread(
                target=self.record,
                args=(trace,),
                kwargs={"batch_id": f"batch_{index}"},
            )
            for index in range(32)
        ]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(2)
            self.assertFalse(thread.is_alive())
        path = self.root / TRACE_DIRECTORY / TRACE_FILENAME
        lines = path.read_bytes().splitlines(keepends=True)
        self.assertEqual(len(lines), 32)
        for line in lines:
            value = json.loads(line)
            self.assertEqual(
                line,
                json.dumps(
                    value, ensure_ascii=True, sort_keys=True,
                    separators=(",", ":"), allow_nan=False,
                ).encode("ascii") + b"\n",
            )

    def test_size_bound_rotates_to_one_complete_latest_record(self):
        trace = V2AmbiguityTrace(self.root, game_id="game_one")
        self.record(trace, batch_id="batch_first")
        path = self.root / TRACE_DIRECTORY / TRACE_FILENAME
        first_size = path.stat().st_size
        with patch.object(
            trace_module, "MAX_TRACE_BYTES", first_size + 1,
        ):
            self.record(trace, batch_id="batch_latest")
        lines = path.read_text(encoding="ascii").splitlines()
        self.assertEqual(len(lines), 1)
        self.assertEqual(json.loads(lines[0])["batch_id"], "batch_latest")
        self.assertLessEqual(path.stat().st_size, first_size + 1)

    def test_leftover_rotation_temporary_does_not_silence_the_trace(self):
        """A crash between create and rename used to kill every later rotation."""
        trace = V2AmbiguityTrace(self.root, game_id="game_one")
        self.record(trace, batch_id="batch_first")
        directory = self.root / TRACE_DIRECTORY
        path = directory / TRACE_FILENAME
        first_size = path.stat().st_size
        leftover = directory / ".events.rotate.tmp"
        leftover.write_text("half a rotation\n", encoding="ascii")
        leftover.chmod(0o600)
        with patch.object(trace_module, "MAX_TRACE_BYTES", first_size + 1):
            self.record(trace, batch_id="batch_second")
            self.record(trace, batch_id="batch_third")
        lines = path.read_text(encoding="ascii").splitlines()
        self.assertEqual(len(lines), 1)
        self.assertEqual(json.loads(lines[0])["batch_id"], "batch_third")
        self.assertFalse(leftover.exists())
        self.assertEqual(
            sorted(item.name for item in directory.iterdir()), [TRACE_FILENAME],
        )

    def test_oversize_trace_file_rotates_instead_of_failing_forever(self):
        trace = V2AmbiguityTrace(self.root, game_id="game_one")
        path = self.root / TRACE_DIRECTORY / TRACE_FILENAME
        path.write_text("x" * (trace_module.MAX_TRACE_BYTES + 64), encoding="ascii")
        path.chmod(0o600)
        self.record(trace, batch_id="batch_after_overflow")
        lines = path.read_text(encoding="ascii").splitlines()
        self.assertEqual(len(lines), 1)
        self.assertEqual(json.loads(lines[0])["batch_id"], "batch_after_overflow")
        self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)

    def test_symlink_directory_and_file_fail_closed_without_following(self):
        outside = self.root / "outside"
        outside.mkdir()
        os.symlink(outside, self.root / TRACE_DIRECTORY)
        with self.assertRaises(V2AmbiguityTraceError):
            V2AmbiguityTrace(self.root, game_id="game_one")
        self.assertEqual(list(outside.iterdir()), [])

        os.unlink(self.root / TRACE_DIRECTORY)
        trace = V2AmbiguityTrace(self.root, game_id="game_one")
        target = self.root / "outside-target"
        target.write_text("unchanged", encoding="ascii")
        os.symlink(target, self.root / TRACE_DIRECTORY / TRACE_FILENAME)
        with self.assertRaises(V2AmbiguityTraceError):
            self.record(trace)
        self.assertEqual(target.read_text(encoding="ascii"), "unchanged")

    def test_symlink_episode_root_and_nonprivate_file_are_rejected(self):
        real_root = self.root / "real-root"
        real_root.mkdir()
        linked_root = self.root / "linked-root"
        os.symlink(real_root, linked_root)
        with self.assertRaises(V2AmbiguityTraceError):
            V2AmbiguityTrace(linked_root, game_id="game_one")

        trace = V2AmbiguityTrace(real_root, game_id="game_one")
        path = real_root / TRACE_DIRECTORY / TRACE_FILENAME
        path.write_text("unsafe\n", encoding="ascii")
        path.chmod(0o644)
        with self.assertRaises(V2AmbiguityTraceError):
            self.record(trace)
        self.assertEqual(path.read_text(encoding="ascii"), "unsafe\n")

    def test_closed_store_is_sanitized_failure(self):
        trace = V2AmbiguityTrace(self.root, game_id="game_one")
        trace.close()
        trace.close()
        with self.assertRaises(V2AmbiguityTraceError) as raised:
            self.record(trace)
        self.assertNotIn(str(self.root), str(raised.exception))


if __name__ == "__main__":
    unittest.main()
