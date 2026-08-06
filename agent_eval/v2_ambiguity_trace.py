"""Private, bounded diagnostics for ``full-control-v2`` ambiguities.

This trace is deliberately separate from the public receipt and spectator
surfaces.  It records only a small normalized classification; native handles,
action arguments, observations, model metadata, paths, and exception text are
never accepted by its schema.
"""

from __future__ import annotations

import errno
import json
import os
import re
import secrets
import stat
import threading
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


TRACE_DIRECTORY = "v2-ambiguity-trace"
TRACE_FILENAME = "events.jsonl"
MAX_TRACE_BYTES = 256 * 1024
MAX_TRACE_RECORD_BYTES = 1024

MAX_ROTATION_TEMPORARIES = 4096

_FORMAT = "freeciv-full-control-v2-ambiguity"
_FORMAT_VERSION = 1
_OPAQUE_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
_ROTATION_TEMP = re.compile(r"^\.events\.rotate(\.[0-9a-f]{16})?\.tmp$")
_TIMESTAMP = re.compile(
    r"^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}"
    r"\.[0-9]{3}Z$"
)
_STAGES = frozenset({
    "pre_accept",
    "post_accept",
    "correlated_terminal",
    "post_result_observation",
})
_REASONS = frozenset({
    "acceptance_unavailable",
    "acceptance_callback_failed",
    "accepted_revision_mismatch",
    "result_unavailable",
    "processing_boundary_mismatch",
    "seat_epoch_changed",
    "processing_timeout",
    "context_lost",
    "invalid_result",
    "observation_unavailable",
    "post_result_context_lost",
    "unexpected_failure",
})
_HEALTH_STATES = frozenset({
    "new", "starting", "handshaking", "taking", "ready", "stopping",
    "stopped", "failed", "unknown",
})
_FIELDS = frozenset({
    "acceptance_known",
    "agent_id",
    "ambiguity_reason",
    "batch_id",
    "format",
    "game_id",
    "schema_version",
    "seat_id",
    "sidecar_generation",
    "sidecar_health_state",
    "stage",
    "timestamp",
})


class V2AmbiguityTraceError(RuntimeError):
    """A sanitized private-trace error with no filesystem detail."""

    def __init__(self) -> None:
        super().__init__("the private ambiguity trace is unavailable")


def _opaque(value: Any) -> str:
    if not isinstance(value, str) or _OPAQUE_ID.fullmatch(value) is None:
        raise V2AmbiguityTraceError()
    return value


def _write_all_once(fd: int, data: bytes) -> None:
    """Append one bounded record in one synchronized system call."""
    written = os.write(fd, data)
    if written != len(data):
        raise OSError(errno.EIO, "short private trace write")


class V2AmbiguityTrace:
    """One mode-0600, fsync'd, size-bounded JSONL trace per episode."""

    def __init__(self, episode_root: str | os.PathLike[str], *, game_id: str):
        self.game_id = _opaque(game_id)
        self._lock = threading.RLock()
        self._closed = False
        self._dir_fd = -1

        flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0)
        flags |= getattr(os, "O_DIRECTORY", 0)
        flags |= getattr(os, "O_NOFOLLOW", 0)
        try:
            root_fd = os.open(Path(episode_root), flags)
        except (OSError, TypeError, ValueError):
            raise V2AmbiguityTraceError() from None
        try:
            try:
                os.mkdir(TRACE_DIRECTORY, 0o700, dir_fd=root_fd)
            except FileExistsError:
                pass
            self._dir_fd = os.open(TRACE_DIRECTORY, flags, dir_fd=root_fd)
            metadata = os.fstat(self._dir_fd)
            if not stat.S_ISDIR(metadata.st_mode):
                raise OSError(errno.ENOTDIR, TRACE_DIRECTORY)
            os.fchmod(self._dir_fd, 0o700)
            os.fsync(root_fd)
            os.fsync(self._dir_fd)
        except OSError:
            if self._dir_fd >= 0:
                os.close(self._dir_fd)
                self._dir_fd = -1
            raise V2AmbiguityTraceError() from None
        finally:
            os.close(root_fd)

    def close(self) -> None:
        with self._lock:
            if self._closed:
                return
            self._closed = True
            fd = self._dir_fd
            self._dir_fd = -1
            if fd >= 0:
                try:
                    os.close(fd)
                except OSError:
                    pass

    def __enter__(self) -> "V2AmbiguityTrace":
        return self

    def __exit__(self, _type: Any, _value: Any, _traceback: Any) -> None:
        self.close()

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass

    def record(
        self,
        *,
        agent_id: str,
        batch_id: str,
        seat_id: str,
        stage: str,
        ambiguity_reason: str,
        sidecar_generation: int,
        sidecar_health_state: str,
        acceptance_known: bool,
        timestamp: str | None = None,
    ) -> None:
        if (
            stage not in _STAGES
            or ambiguity_reason not in _REASONS
            or sidecar_health_state not in _HEALTH_STATES
            or type(sidecar_generation) is not int
            or not 1 <= sidecar_generation <= (1 << 63) - 1
            or type(acceptance_known) is not bool
        ):
            raise V2AmbiguityTraceError()
        if timestamp is None:
            timestamp = datetime.now(timezone.utc).isoformat(
                timespec="milliseconds",
            ).replace("+00:00", "Z")
        if (
            not isinstance(timestamp, str)
            or _TIMESTAMP.fullmatch(timestamp) is None
        ):
            raise V2AmbiguityTraceError()
        record = {
            "acceptance_known": acceptance_known,
            "agent_id": _opaque(agent_id),
            "ambiguity_reason": ambiguity_reason,
            "batch_id": _opaque(batch_id),
            "format": _FORMAT,
            "game_id": self.game_id,
            "schema_version": _FORMAT_VERSION,
            "seat_id": _opaque(seat_id),
            "sidecar_generation": sidecar_generation,
            "sidecar_health_state": sidecar_health_state,
            "stage": stage,
            "timestamp": timestamp,
        }
        if set(record) != _FIELDS:
            raise V2AmbiguityTraceError()
        try:
            encoded = json.dumps(
                record,
                ensure_ascii=True,
                sort_keys=True,
                separators=(",", ":"),
                allow_nan=False,
            ).encode("ascii") + b"\n"
        except (TypeError, ValueError, UnicodeError):
            raise V2AmbiguityTraceError() from None
        if len(encoded) > MAX_TRACE_RECORD_BYTES:
            raise V2AmbiguityTraceError()

        with self._lock:
            if self._closed or self._dir_fd < 0:
                raise V2AmbiguityTraceError()
            try:
                self._append_locked(encoded)
            except OSError:
                raise V2AmbiguityTraceError() from None

    def _append_locked(self, encoded: bytes) -> None:
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_APPEND
        flags |= getattr(os, "O_CLOEXEC", 0)
        flags |= getattr(os, "O_NOFOLLOW", 0)
        # Never let opening the trace block on a device or a reader-less FIFO
        # that replaced it: this call runs under the trace lock, and a blocking
        # open would strand every later ambiguity record behind it.
        flags |= getattr(os, "O_NONBLOCK", 0)
        created = False
        try:
            fd = os.open(TRACE_FILENAME, flags, 0o600, dir_fd=self._dir_fd)
            created = True
        except FileExistsError:
            flags &= ~(os.O_CREAT | os.O_EXCL)
            fd = os.open(TRACE_FILENAME, flags, dir_fd=self._dir_fd)
        try:
            if created:
                os.fchmod(fd, 0o600)
            metadata = os.fstat(fd)
            if (
                not stat.S_ISREG(metadata.st_mode)
                or stat.S_IMODE(metadata.st_mode) != 0o600
                or metadata.st_size < 0
            ):
                # Still terminal: a trace that is not a private regular file is
                # a safety question, not a capacity one, and this stream must
                # never append private diagnostics to it.
                raise OSError(errno.EPERM, "unsafe private trace")
            if metadata.st_size + len(encoded) > MAX_TRACE_BYTES:
                os.close(fd)
                fd = -1
                self._replace_with_locked(encoded)
                return
            _write_all_once(fd, encoded)
            os.fsync(fd)
        finally:
            if fd >= 0:
                os.close(fd)
        os.fsync(self._dir_fd)

    def _replace_with_locked(self, encoded: bytes) -> None:
        # A unique name per rotation.  A fixed one turned a single crash
        # between create and rename into a permanent failure: every later
        # rotation hit the leftover with O_EXCL and the trace stopped
        # recording for the rest of the game.  Leftovers are reaped below.
        temporary = f".events.rotate.{secrets.token_hex(8)}.tmp"
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
        flags |= getattr(os, "O_CLOEXEC", 0)
        flags |= getattr(os, "O_NOFOLLOW", 0)
        fd = -1
        created = False
        try:
            fd = os.open(temporary, flags, 0o600, dir_fd=self._dir_fd)
            created = True
            os.fchmod(fd, 0o600)
            _write_all_once(fd, encoded)
            os.fsync(fd)
            os.close(fd)
            fd = -1
            os.replace(
                temporary,
                TRACE_FILENAME,
                src_dir_fd=self._dir_fd,
                dst_dir_fd=self._dir_fd,
            )
            created = False
            os.fsync(self._dir_fd)
        finally:
            if fd >= 0:
                os.close(fd)
            if created:
                try:
                    os.unlink(temporary, dir_fd=self._dir_fd)
                except OSError:
                    pass
            self._reap_rotation_temporaries(keep=temporary)

    def _reap_rotation_temporaries(self, *, keep: str) -> None:
        """Drop rotation temporaries a previous crash left behind."""
        try:
            names = os.listdir(self._dir_fd)
        except OSError:
            return
        for name in names[:MAX_ROTATION_TEMPORARIES]:
            if name == keep or _ROTATION_TEMP.fullmatch(name) is None:
                continue
            try:
                os.unlink(name, dir_fd=self._dir_fd)
            except OSError:
                pass


__all__ = [
    "MAX_TRACE_BYTES",
    "MAX_TRACE_RECORD_BYTES",
    "TRACE_DIRECTORY",
    "TRACE_FILENAME",
    "V2AmbiguityTrace",
    "V2AmbiguityTraceError",
]
