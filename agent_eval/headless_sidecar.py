"""Private lifecycle wrapper for the same-revision ``freeciv-agent`` client.

The sidecar is deliberately not an agent-facing transport.  It owns one
inherited AF_UNIX socket, performs the small native bootstrap protocol, and
publishes only a bounded, credential-free health snapshot to the supervisor.
"""

from __future__ import annotations

import errno
import json
import math
import os
import re
import resource
import select
import signal
import socket
import struct
import subprocess
import threading
import time
from collections import deque
from collections.abc import Mapping
from contextlib import contextmanager
from pathlib import Path
from types import MappingProxyType
from typing import Any, Callable

from .v2_control import NATIVE_OBSERVATION_ACTION_SCHEMA_ID


MAX_FRAME = 8192
PROTOCOL_VERSION = 1
NATIVE_PROTOCOL_VERSION = 2
NATIVE_CAPABILITIES = (
    "ACT", "ACT_CAP", "ACT_RELATION_CAP", "OBS_OPEN", "OBS_PAGE",
    "PHASE_AVAILABLE", "SCOPE_OPEN", "SCOPE_PAGE", "STATE_AVAILABLE",
    "STATE_SCOPE_OPEN", "STATE_SCOPE_PAGE",
    "TARGET_ACTION", "RELATION_SCOPE_OPEN", "RELATION_SCOPE_PAGE",
)
NATIVE_ENCODING = "percent-tab"
NATIVE_CAPS = (
    f"CAPS\t{NATIVE_PROTOCOL_VERSION}\t{','.join(NATIVE_CAPABILITIES)}"
    f"\t{NATIVE_ENCODING}\t{MAX_FRAME}"
    f"\t{NATIVE_OBSERVATION_ACTION_SCHEMA_ID}"
)
MAX_CAPS_BYTES = 512
MAX_CAPABILITY_COUNT = 18
MAX_CAPABILITY_BYTES = 64
MAX_ENCODING_BYTES = 32
MAX_SCHEMA_ID_BYTES = 128
MAX_NATIVE_REVISION = (1 << 64) - 1
MAX_NATIVE_PHASE_INTEGER = (1 << 31) - 1
MAX_OBSERVATION_ROWS = 8192
MAX_STATE_SCOPE_ROWS = 40000
MAX_STATE_SCOPE_BYTES = 16 * 1024 * 1024
MAX_OBSERVATION_PAGE = 16
MAX_OBSERVATION_ROW_BYTES = 2047
NATIVE_TOKEN_RE = re.compile(r"^[A-Za-z0-9._~-]{1,64}$")
SNAPSHOT_RE = re.compile(r"^s[0-9]+-[0-9]+$")
SCOPE_VIEW_RE = re.compile(r"^v[0-9]+-[0-9]+$")
STATE_SCOPE_VIEW_RE = re.compile(r"^q[0-9]+-[0-9]+$")
RELATION_SCOPE_VIEW_RE = re.compile(r"^r[0-9]+-[0-9]+$")
INVESTIGATION_SELECTOR_RE = re.compile(r"^i[0-9a-f]{16}$")
NATIVE_ACTOR_RE = re.compile(r"^[pcu]:(?:0|[1-9][0-9]*):[1-9][0-9]*$")
STATE_SCOPE_SELECTOR_RE = re.compile(
    r"^(?:-|[pcu]:(?:0|[1-9][0-9]*):[1-9][0-9]*|"
    r"t(?:0|[1-9][0-9]*)-r(?:0|[1-8])|i[0-9a-f]{16})$"
)
STATE_SCOPE_SECTIONS = frozenset({
    "known_tiles", "tile_window", "cities", "units", "city_sites",
    "diplomacy_clauses", "city_citizens",
    "city_build_choices", "city_worklist", "city_improvements",
    "city_governor", "target_tiles", "pregame_nations",
    "pregame_styles", "pregame_teams", "chat_recipients", "unit_route",
    "investigation",
})
ACTION_SLOT_RE = re.compile(r"^(?:a[0-9A-F]{16}|t[0-9A-F]{24})$")
NATIVE_REASON_RE = re.compile(r"^[A-Z][A-Z0-9_]{0,63}$")
PLAYER_RE = re.compile(r"^[A-Za-z0-9._-]{1,63}$")
PING_RE = re.compile(r"^[A-Za-z0-9._:-]{1,64}$")
TERMINAL_STATES = frozenset({"stopped", "failed"})
SIDE_CAR_STATES = frozenset({
    "new", "starting", "handshaking", "taking", "ready",
    "stopping", "stopped", "failed",
})
_INHERITED_ENV = frozenset({
    "LANG", "LC_ALL", "LC_CTYPE", "PATH", "TMPDIR", "TZ",
})
_SECRET_PARTS = (
    "agent_eval", "auth", "credential", "key", "password", "secret",
    "token",
)
_NATIVE_ERROR_CODES = {
    "BAD_REQUEST": "native_bad_request",
    "BAD_ENCODING": "native_bad_encoding",
    "OBS_TOO_LARGE": "observation_too_large",
    "SNAPSHOT_GONE": "snapshot_gone",
    "BAD_OFFSET": "native_bad_offset",
    "ENCODE_FAILED": "native_encode_failed",
    "BUSY": "native_busy",
    "STALE_SLOT": "stale_slot",
    "NOT_READY": "native_not_ready",
    "STALE_ENTITY": "stale_entity",
    "BAD_ARGUMENT": "native_bad_argument",
    "NOT_SENT": "native_not_sent",
    "STALE_REVISION": "stale_revision",
    "INVALID_ACTOR": "invalid_actor",
    "SCOPE_TOO_LARGE": "actor_scope_too_large",
    "STATE_SCOPE_TOO_LARGE": "state_scope_too_large",
    "SCOPE_GONE": "scope_gone",
    "INVALID_RELATION": "invalid_relation",
    # Native target discovery and server-authoritative action preflight use
    # untagged Freeciv replies.  A timeout or mismatch can leave one reply in
    # flight, so the supervisor must replace this sidecar before reuse.
    "STREAM_DESYNC": "protocol_error",
    "REVALIDATION_DESYNC": "protocol_error",
}
_NONTERMINAL_NATIVE_ERRORS = frozenset(
    value for native, value in _NATIVE_ERROR_CODES.items()
    if native not in {"STREAM_DESYNC", "REVALIDATION_DESYNC"}
) | {
    "native_error", "relation_scope_too_large",
}
_PHASE_MODES = frozenset({
    "concurrent", "players_alternate", "teams_alternate",
})
# Native client log verbosity, as the client's own single-letter levels:
# (f)atal, (e)rror, (w)arning, (n)ormal, (v)erbose.  Anything else is refused
# by the client at launch, including the numeric levels other Freeciv tools
# accept.
#
# The default is deliberately "normal" and not "verbose".  Verbose is the level
# a postmortem wants -- it adds "Beginning turn N" and the connection
# diagnostics whose absence made the turn-66 stderr.log unreadable -- but
# ``log_packet`` is ``log_verbose`` (utility/log.h:138) and Freeciv flushes
# every log line, so verbose costs one flushed stderr write per packet received
# from the server, inside the same single-threaded loop that answers this
# sidecar's IPC.  Measured against the real client on a pregame connection
# alone: 3291 packet lines and 306 KB in ~15 s, against 199 bytes at normal.
# Buying diagnostics by slowing the client's reply path would deepen exactly
# the latency that the turn-66 incident was the tail of, so the level is a
# documented knob for a hunt rather than a default.
NATIVE_LOG_LEVELS = frozenset({"f", "e", "w", "n", "v"})
DEFAULT_NATIVE_LOG_LEVEL = "n"
# Owner-private capture ring.  The client writes its own logs, so the harness
# cannot line-buffer them; it can only bound them and keep a copy of the end.
LOG_RING_BYTES = 8192
LOG_TAIL_LINES = 30
LOG_TAIL_BYTES = 4096
# Per-stream disk ceiling and the tail preserved when it is reached.  Verbose
# logging over a long game must never fill the owner's disk, and truncating to
# nothing would destroy the very evidence the verbosity was raised for.
LOG_ROTATE_BYTES = 4 * 1024 * 1024
LOG_ROTATE_KEEP_BYTES = 256 * 1024
LOG_SWEEP_INTERVAL_S = 5.0
# Core files are truncated at this size by the kernel.  Unlimited cores from a
# long-lived client are a disk-exhaustion risk; a bounded core still carries
# the stack that distinguishes a native fault from a harness one.
DEFAULT_CORE_DUMP_LIMIT_BYTES = 1 << 30
EXIT_FORENSICS_FILENAME = "exit-forensics.json"
# How many replies a client may owe before its stream is abandoned.  Each entry
# is one liveness command whose reply arrived too late to be read; the stream
# stays recoverable because the reply is still identifiable and discardable in
# order.  The cap exists so an endlessly unanswering client is still eventually
# fail-closed rather than accumulating expectations forever.
MAX_UNANSWERED_LIVENESS_REPLIES = 8
# How long a command may wait for the one in flight ahead of it before the
# caller is told the boundary is busy.  The native client resolves a command in
# well under a millisecond at the median and inside ~200 ms at its worst, which
# is the turn-change tick; a bound comfortably above that turns nearly every
# collision into a short wait instead of a refusal, while still refusing rather
# than queueing without limit behind a client that has stopped answering.
COMMAND_QUEUE_WAIT_S = 1.0


def _validate_native_caps(message: str) -> None:
    """Validate the complete native protocol-2 capability contract."""
    if len(message.encode("utf-8")) > MAX_CAPS_BYTES:
        raise SidecarError("protocol_error")
    fields = message.split("\t")
    if len(fields) == 5 and fields[0] == "CAPS":
        # Protocol-2 clients predating schema negotiation used five fields.
        raise SidecarError(
            "schema_mismatch",
            "native sidecar observation/action schema is incompatible",
        )
    if len(fields) != 6 or fields[0] != "CAPS":
        raise SidecarError("protocol_error")
    version, capabilities, encoding, max_frame, schema_id = fields[1:]
    if version != str(NATIVE_PROTOCOL_VERSION):
        raise SidecarError("protocol_error")
    if len(capabilities.encode("ascii", "ignore")) != len(capabilities):
        raise SidecarError("protocol_error")
    capability_items = capabilities.split(",")
    if (
        not 1 <= len(capability_items) <= MAX_CAPABILITY_COUNT
        or any(
            not item or len(item) > MAX_CAPABILITY_BYTES
            or NATIVE_REASON_RE.fullmatch(item) is None
            for item in capability_items
        )
        or len(set(capability_items)) != len(capability_items)
        or tuple(capability_items) != NATIVE_CAPABILITIES
    ):
        raise SidecarError("protocol_error")
    if (
        len(encoding) > MAX_ENCODING_BYTES
        or encoding != NATIVE_ENCODING
        or max_frame != str(MAX_FRAME)
    ):
        raise SidecarError("protocol_error")
    if len(schema_id) > MAX_SCHEMA_ID_BYTES:
        raise SidecarError("protocol_error")
    if schema_id != NATIVE_OBSERVATION_ACTION_SCHEMA_ID:
        raise SidecarError(
            "schema_mismatch",
            "native sidecar observation/action schema is incompatible",
        )


class SidecarError(RuntimeError):
    """A bounded sidecar failure safe to map to a public error code."""

    def __init__(self, code: str, message: str | None = None):
        super().__init__(message or code.replace("_", " "))
        self.code = code


class SidecarActionAmbiguous(SidecarError):
    """An action may have reached native code, but its boundary is unknown.

    ``acceptance`` is present only after a valid, correlated ``ACT_ACCEPTED``.
    Native request identifiers, native action slots, transport errors, and
    callback exception text are deliberately excluded in both cases.
    """

    def __init__(
        self,
        acceptance: dict[str, Any] | None,
        ambiguity_reason: str,
        *,
        stage: str | None = None,
        stream_synchronized: bool = False,
    ):
        accepted = acceptance is not None
        super().__init__(
            (
                "action_accepted_ambiguous"
                if accepted else "action_delivery_ambiguous"
            ),
            (
                "native action was accepted but its outcome is unavailable"
                if accepted else
                "native action delivery or acceptance is unavailable"
            ),
        )
        self.acceptance = None if acceptance is None else dict(acceptance)
        self.ambiguity_reason = ambiguity_reason
        self.stage = stage or ("post_accept" if accepted else "pre_accept")
        self.stream_synchronized = stream_synchronized


class SidecarActionNotAccepted(SidecarError):
    """A correlated native rejection proves the action was not accepted."""


def _payload_bytes(value: str) -> bytes:
    if not isinstance(value, str):
        raise SidecarError("invalid_frame", "IPC payload must be text")
    try:
        payload = value.encode("utf-8", errors="strict")
    except UnicodeEncodeError as exc:
        raise SidecarError("invalid_utf8", "IPC payload is not valid UTF-8") from exc
    if not 1 <= len(payload) <= MAX_FRAME:
        raise SidecarError("invalid_frame", "IPC frame length is outside 1..8192")
    if any(
        byte == 0 or byte in {10, 13, 127}
        or (byte < 32 and byte != 9)
        for byte in payload
    ):
        raise SidecarError("invalid_control", "IPC payload contains a forbidden control")
    return payload


def _canonical_uint(value: str, maximum: int, code: str = "protocol_error") -> int:
    if not value or not value.isascii() or not value.isdecimal():
        raise SidecarError(code)
    parsed = int(value)
    if parsed < 0 or parsed > maximum or str(parsed) != value:
        raise SidecarError(code)
    return parsed


def _percent_encode(value: str) -> str:
    if not isinstance(value, str) or not value:
        raise SidecarError("invalid_argument")
    try:
        raw = value.encode("utf-8", errors="strict")
    except UnicodeEncodeError as exc:
        raise SidecarError("invalid_argument") from exc
    encoded: list[str] = []
    for byte in raw:
        if (
            ord("a") <= byte <= ord("z")
            or ord("A") <= byte <= ord("Z")
            or ord("0") <= byte <= ord("9")
            or byte in b"._~-"
        ):
            encoded.append(chr(byte))
        else:
            encoded.append(f"%{byte:02X}")
    return "".join(encoded)


def _percent_decode(
    value: str, *, maximum_bytes: int, allow_controls: bool = False,
) -> str:
    if not isinstance(value, str):
        raise SidecarError("protocol_error")
    raw = bytearray()
    index = 0
    while index < len(value):
        current = value[index]
        if current == "%":
            if index + 2 >= len(value):
                raise SidecarError("protocol_error")
            digits = value[index + 1:index + 3]
            if any(ch not in "0123456789ABCDEF" for ch in digits):
                raise SidecarError("protocol_error")
            byte = int(digits, 16)
            index += 3
        else:
            byte = ord(current)
            if byte > 0x7F or not (
                current.isascii() and (
                    current.isalnum() or current in "._~-"
                )
            ):
                raise SidecarError("protocol_error")
            index += 1
        if byte == 0 or len(raw) >= maximum_bytes:
            raise SidecarError("protocol_error")
        if not allow_controls and (byte < 0x20 or byte == 0x7F):
            raise SidecarError("protocol_error")
        raw.append(byte)
    try:
        return bytes(raw).decode("utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        raise SidecarError("protocol_error") from exc


class FramedIPC:
    """Deadline-bounded 4-byte-big-endian UTF-8 frames over a stream socket."""

    def __init__(self, stream: socket.socket):
        if stream.family != socket.AF_UNIX or stream.type & socket.SOCK_STREAM == 0:
            raise SidecarError("invalid_socket", "IPC must be an AF_UNIX stream socket")
        self.stream = stream
        self._send_lock = threading.Lock()

    @staticmethod
    def _remaining(deadline: float) -> float:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise SidecarError("deadline_exceeded", "IPC deadline exceeded")
        return remaining

    def _read_exact(self, size: int, deadline: float) -> bytes:
        chunks: list[bytes] = []
        remaining_size = size
        while remaining_size:
            try:
                readable, _, _ = select.select(
                    [self.stream], [], [], self._remaining(deadline),
                )
            except (OSError, ValueError) as exc:
                raise SidecarError("ipc_read_failed") from exc
            if not readable:
                raise SidecarError("deadline_exceeded", "IPC receive deadline exceeded")
            try:
                chunk = self.stream.recv(remaining_size)
            except InterruptedError:
                continue
            except OSError as exc:
                raise SidecarError("ipc_read_failed") from exc
            if not chunk:
                raise SidecarError("unexpected_eof", "sidecar IPC closed unexpectedly")
            chunks.append(chunk)
            remaining_size -= len(chunk)
        return b"".join(chunks)

    def receive(self, deadline: float) -> str:
        header = self._read_exact(4, deadline)
        (length,) = struct.unpack(">I", header)
        if not 1 <= length <= MAX_FRAME:
            raise SidecarError("invalid_frame", "IPC frame length is outside 1..8192")
        payload = self._read_exact(length, deadline)
        if any(
            byte == 0 or byte in {10, 13, 127}
            or (byte < 32 and byte != 9)
            for byte in payload
        ):
            raise SidecarError("invalid_control", "IPC payload contains a forbidden control")
        try:
            return payload.decode("utf-8", errors="strict")
        except UnicodeDecodeError as exc:
            raise SidecarError("invalid_utf8", "IPC payload is not valid UTF-8") from exc

    def send(self, value: str, deadline: float) -> None:
        payload = _payload_bytes(value)
        framed = struct.pack(">I", len(payload)) + payload
        with self._send_lock:
            view = memoryview(framed)
            while view:
                try:
                    _, writable, _ = select.select(
                        [], [self.stream], [], self._remaining(deadline),
                    )
                except (OSError, ValueError) as exc:
                    raise SidecarError("ipc_write_failed") from exc
                if not writable:
                    raise SidecarError("deadline_exceeded", "IPC send deadline exceeded")
                try:
                    sent = self.stream.send(view)
                except InterruptedError:
                    continue
                except OSError as exc:
                    if exc.errno in {errno.EPIPE, errno.ECONNRESET, errno.ENOTCONN}:
                        raise SidecarError(
                            "unexpected_eof", "sidecar IPC closed unexpectedly",
                        ) from exc
                    raise SidecarError("ipc_write_failed") from exc
                if sent <= 0:
                    raise SidecarError("unexpected_eof", "sidecar IPC closed unexpectedly")
                view = view[sent:]


class _OwnerAwareCommandLock:
    """Serialize commands and make all callback-time acquisition fail fast."""

    def __init__(self):
        self._condition = threading.Condition()
        self._owner: int | None = None
        self._callback_depth = 0

    def acquire(
        self, blocking: bool = True, timeout: float = -1,
    ) -> bool:
        owner = threading.get_ident()
        if not blocking and timeout != -1:
            raise ValueError("can't specify a timeout for a non-blocking lock")
        if timeout < 0 and timeout != -1:
            raise ValueError("timeout value must be positive")
        deadline = None if timeout == -1 else time.monotonic() + timeout
        with self._condition:
            if self._owner == owner or self._callback_depth:
                raise SidecarError(
                    "command_in_progress",
                    "sidecar command is unavailable during a callback",
                )
            if self._owner is None:
                self._owner = owner
                return True
            if not blocking:
                return False
            while self._owner is not None:
                if self._callback_depth:
                    raise SidecarError(
                        "command_in_progress",
                        "sidecar command is unavailable during a callback",
                    )
                remaining = (
                    None if deadline is None else deadline - time.monotonic()
                )
                if remaining is not None and remaining <= 0:
                    return False
                self._condition.wait(remaining)
            if self._callback_depth:
                raise SidecarError(
                    "command_in_progress",
                    "sidecar command is unavailable during a callback",
                )
            self._owner = owner
            return True

    def release(self) -> None:
        owner = threading.get_ident()
        with self._condition:
            if self._owner != owner:
                raise RuntimeError("command lock released by non-owner")
            self._owner = None
            self._condition.notify_all()

    @contextmanager
    def callback_scope(self):
        """Suspend ownership behind a barrier while invoking a callback."""
        owner = threading.get_ident()
        active = False
        with self._condition:
            if self._owner == owner:
                self._owner = None
                self._callback_depth += 1
                active = True
                self._condition.notify_all()
        try:
            yield
        finally:
            if active:
                with self._condition:
                    self._callback_depth -= 1
                    if self._callback_depth == 0:
                        if self._owner is not None:
                            raise RuntimeError(
                                "command acquired through callback barrier",
                            )
                        self._owner = owner
                    self._condition.notify_all()

    def __enter__(self) -> _OwnerAwareCommandLock:
        self.acquire()
        return self

    def __exit__(self, *unused: object) -> None:
        self.release()


class HeadlessSidecar:
    """Own one headless Freeciv client and its private bootstrap channel."""

    def __init__(
        self,
        *,
        binary: str | Path,
        run_root: str | Path,
        game_id: str,
        seat_id: str,
        player_name: str,
        host: str,
        port: int,
        generation: int,
        on_exit: Callable[[int, dict[str, Any]], None] | None = None,
        process_factory: Callable[..., Any] = subprocess.Popen,
        handshake_timeout_s: float = 20.0,
        stop_timeout_s: float = 2.0,
        native_log_level: str | None = DEFAULT_NATIVE_LOG_LEVEL,
        core_dump_limit_bytes: int | None = DEFAULT_CORE_DUMP_LIMIT_BYTES,
    ):
        if not PLAYER_RE.fullmatch(player_name):
            raise SidecarError("invalid_player", "sidecar player name is unsafe")
        if not isinstance(generation, int) or generation < 1:
            raise SidecarError("invalid_generation")
        if not isinstance(port, int) or not 1 <= port <= 65535:
            raise SidecarError("invalid_port")
        if native_log_level is not None and native_log_level not in (
            NATIVE_LOG_LEVELS
        ):
            raise SidecarError("invalid_log_level")
        if core_dump_limit_bytes is not None and (
            isinstance(core_dump_limit_bytes, bool)
            or not isinstance(core_dump_limit_bytes, int)
            or core_dump_limit_bytes < 0
        ):
            raise SidecarError("invalid_core_limit")
        self.binary = Path(binary).resolve()
        self.game_id = game_id
        self.seat_id = seat_id
        self.player_name = player_name
        self.host = host
        self.port = port
        self.generation = generation
        self.on_exit = on_exit
        self.process_factory = process_factory
        self.handshake_timeout_s = handshake_timeout_s
        self.stop_timeout_s = stop_timeout_s
        self.native_log_level = native_log_level
        self.core_dump_limit_bytes = core_dump_limit_bytes
        self.run_directory = Path(run_root).resolve() / (
            f"{seat_id}-generation-{generation}"
        )
        self.home_directory = self.run_directory / "home"
        self.options_path = self.run_directory / "freeciv-client.rc"
        self.stdout_path = self.run_directory / "stdout.log"
        self.stderr_path = self.run_directory / "stderr.log"
        self._lock = threading.Condition(threading.RLock())
        self._command_lock = _OwnerAwareCommandLock()
        self._messages: deque[str] = deque(maxlen=128)
        self._ipc: FramedIPC | None = None
        self._socket: socket.socket | None = None
        self._process: Any | None = None
        self._reader_thread: threading.Thread | None = None
        self._monitor_thread: threading.Thread | None = None
        self._stop_requested = False
        self._callback_fired = False
        self._error_code: str | None = None
        self._state = "new"
        self._started_at: float | None = None
        self._ready_at: float | None = None
        self._stopped_at: float | None = None
        self._last_seen_at: float | None = None
        self._exit_code: int | None = None
        self._client_state: str | None = None
        self._server_connected: bool | None = None
        self._seat_state: str | None = None
        # Owner-private native identity keeps the acquired seat stable across
        # pregame leader renames. It must never enter public health payloads.
        self._native_player_number: int | None = None
        self._native_player_lifecycle: int | None = None
        self._caps_received = False
        self._protocol_version: int | None = None
        self._native_revision: int | None = None
        self._capabilities_available = False
        self._native_ready_announced = False
        self._phase_evidence: dict[str, Any] | None = None
        # Owner-private protocol evidence. Raw frames can contain native
        # identities, so this never enters public_health().
        self._protocol_diagnostic_stage: str | None = None
        self._protocol_diagnostic_frame: str | None = None
        # Exit evidence.  ``_exit_code`` alone cannot say whether a death was
        # observed before or after the seat was already given up on, and that
        # distinction is the difference between a client that crashed and a
        # client the harness killed after deciding it had crashed.
        self._exit_observed_at: float | None = None
        self._exit_observed_after_terminal = False
        # Owner-private capture ring over the client's own logs.  The child
        # writes to these files directly, so nothing here can ever block it.
        self._log_lock = threading.Lock()
        self._log_rings: dict[str, dict[str, Any]] = {
            name: {
                "data": b"",
                "bytes": 0,
                "total_bytes": 0,
                "dropped_bytes": 0,
                "last_output_at": None,
            }
            for name in ("stdout", "stderr")
        }
        self._log_swept_at: float | None = None
        # Replies owed by a client that answered a liveness command too late.
        # Each entry names the frame prefixes of one abandoned reply, in the
        # order the client will emit them, so a late answer can be discarded
        # instead of being mistaken for the next command's.
        self._stale_replies: deque[tuple[str, ...]] = deque()
        self._unanswered_replies = 0
        self._discarded_late_replies = 0

    @staticmethod
    def _safe_environment(
        home: Path, options_path: Path, data_path: Path,
    ) -> dict[str, str]:
        environment: dict[str, str] = {}
        for name in _INHERITED_ENV:
            value = os.environ.get(name)
            if value is None or any(part in name.casefold() for part in _SECRET_PARTS):
                continue
            environment[name] = value
        environment.update({
            "HOME": str(home),
            "FREECIV_OPT": str(options_path),
            "FREECIV_DATA_PATH": str(data_path),
        })
        return environment

    def _prepare_private_files(self) -> tuple[Any, Any]:
        self.run_directory.mkdir(parents=True, exist_ok=False, mode=0o700)
        os.chmod(self.run_directory, 0o700)
        self.home_directory.mkdir(mode=0o700)
        os.chmod(self.home_directory, 0o700)
        option_fd = os.open(
            self.options_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600,
        )
        os.close(option_fd)
        # O_APPEND, so that every write lands at the current end of the file.
        # Without it the client keeps its own offset and the disk-cap sweep
        # below would leave a sparse hole in front of the surviving tail,
        # turning the evidence into NUL bytes.
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_APPEND
        stdout_fd = os.open(self.stdout_path, flags, 0o600)
        stderr_fd = os.open(self.stderr_path, flags, 0o600)
        return os.fdopen(stdout_fd, "wb", buffering=0), os.fdopen(
            stderr_fd, "wb", buffering=0,
        )

    def _core_dump_preexec(self) -> Callable[[], None] | None:
        """Permit a bounded core file in the child, or nothing if disabled.

        A native fault that leaves no core is nearly as opaque as no fault at
        all.  The limit is applied between fork and exec, so it is deliberately
        one pre-resolved syscall with no allocation, name lookup or logging:
        anything richer risks deadlocking a forked child of a threaded parent.
        """
        limit = self.core_dump_limit_bytes
        if not limit:
            return None
        try:
            soft, hard = resource.getrlimit(resource.RLIMIT_CORE)
        except (OSError, ValueError, resource.error):  # pragma: no cover
            return None
        if hard != resource.RLIM_INFINITY:
            limit = min(limit, hard)
        if soft != resource.RLIM_INFINITY and soft >= limit:
            # Already permitted at least as much as this sidecar asks for.
            return None
        limits = (limit, hard)

        def apply_core_limit(
            _setrlimit=resource.setrlimit,
            _which=resource.RLIMIT_CORE,
            _limits=limits,
        ) -> None:
            try:
                _setrlimit(_which, _limits)
            except Exception:
                # A child that cannot dump core must still start and play.
                pass

        return apply_core_limit

    def _launch_argv(self, ipc_fd: int, connection_name: str) -> list[str]:
        """The exact private launch command line, with no credentials in it."""
        argv = [
            str(self.binary), "--autoconnect", "--name", connection_name,
            "--server", self.host, "--port", str(self.port),
        ]
        if self.native_log_level is not None:
            # Stated explicitly even at the client's own default level, so the
            # verbosity a log was captured at is recorded in the launch rather
            # than assumed by whoever reads the log afterwards.
            argv += ["--debug", self.native_log_level]
        argv += [
            "--", "--ipc-fd", str(ipc_fd), "--player", self.player_name,
        ]
        return argv

    def _set_state(self, state: str) -> None:
        if state not in SIDE_CAR_STATES:
            raise ValueError(state)
        self._state = state
        self._lock.notify_all()

    @staticmethod
    def _invoke_exit_callback(
        callback: Callable[[int, dict[str, Any]], None],
        generation: int,
        health: dict[str, Any],
    ) -> None:
        try:
            callback(generation, health)
        except Exception:
            pass

    def _terminal(
        self,
        state: str,
        error_code: str | None = None,
        *,
        defer_callback: bool = False,
    ) -> tuple[
        Callable[[int, dict[str, Any]], None] | None,
        dict[str, Any] | None,
    ]:
        callback: Callable[[int, dict[str, Any]], None] | None = None
        health: dict[str, Any] | None = None
        with self._lock:
            if self._state in TERMINAL_STATES:
                return None, None
            if state == "failed":
                self._error_code = error_code or "sidecar_failed"
            self._phase_evidence = None
            self._native_ready_announced = False
            self._stopped_at = time.time()
            self._set_state(state)
            if not self._callback_fired:
                self._callback_fired = True
                callback = self.on_exit
                health = self.public_health()
        if state == "failed" and self._error_code == "protocol_error":
            self._persist_private_protocol_diagnostic()
        # Every terminalization, not only the ones a supervisor is listening
        # for: the evidence has to exist before anyone decides what the loss
        # was, and it must survive this process.
        self._capture_exit_evidence()
        if (
            callback is not None and health is not None
            and not defer_callback
        ):
            self._invoke_exit_callback(callback, self.generation, health)
            return None, None
        return callback, health

    def _record_native_revision_locked(
        self, revision: int, *, reject_regression: bool = True,
    ) -> None:
        if revision < 1 or revision > MAX_NATIVE_REVISION:
            raise SidecarError("protocol_error")
        if (
            reject_regression and self._native_revision is not None
            and revision < self._native_revision
        ):
            raise SidecarError("protocol_error")
        if self._native_revision is None or revision > self._native_revision:
            self._native_revision = revision
            self._lock.notify_all()

    def _demultiplex_locked(self, message: str) -> bool:
        """Record protocol-2 control frames; return true for notifications."""
        if message == "CAPS" or message.startswith("CAPS\t"):
            if self._caps_received or self._state != "handshaking":
                raise SidecarError("protocol_error")
            _validate_native_caps(message)
            self._caps_received = True
            self._protocol_version = NATIVE_PROTOCOL_VERSION
            self._capabilities_available = True
            return False
        if message == "STATE_AVAILABLE" or message.startswith(
            "STATE_AVAILABLE\t"
        ):
            if not self._caps_received:
                raise SidecarError("protocol_error")
            fields = message.split("\t")
            if len(fields) != 2:
                raise SidecarError("protocol_error")
            revision = _canonical_uint(fields[1], MAX_NATIVE_REVISION)
            self._record_native_revision_locked(revision)
            return True
        if message == "PHASE_AVAILABLE" or message.startswith(
            "PHASE_AVAILABLE\t"
        ):
            if not self._caps_received:
                raise SidecarError("protocol_error")
            fields = message.split("\t")
            if len(fields) != 10:
                raise SidecarError("protocol_error")
            revision = _canonical_uint(fields[1], MAX_NATIVE_REVISION)
            turn = _canonical_uint(fields[2], MAX_NATIVE_PHASE_INTEGER)
            phase = _canonical_uint(fields[3], MAX_NATIVE_PHASE_INTEGER)
            mode = fields[4]
            phase_count = _canonical_uint(
                fields[5], MAX_NATIVE_PHASE_INTEGER,
            )
            if revision < 1 or turn < 1 or phase_count < 1:
                raise SidecarError("protocol_error")
            if phase >= phase_count or mode not in _PHASE_MODES:
                raise SidecarError("protocol_error")

            flags: list[bool] = []
            for value in fields[6:10]:
                if value not in {"0", "1"}:
                    raise SidecarError("protocol_error")
                flags.append(value == "1")
            active, alive, done, ready = flags
            if mode == "concurrent" and (
                phase != 0 or phase_count != 1 or not active
            ):
                raise SidecarError("protocol_error")
            if (
                mode == "players_alternate" and phase_count > 512
                or mode == "teams_alternate" and phase_count > 513
            ):
                raise SidecarError("protocol_error")
            if ready and (
                not active or not alive or done
                or not self._native_ready_announced
            ):
                raise SidecarError("protocol_error")

            evidence = {
                "generation": self.generation,
                "revision": revision,
                "turn": turn,
                "phase": phase,
                "mode": mode,
                "phase_count": phase_count,
                "active": active,
                "alive": alive,
                "done": done,
                "ready": ready,
            }
            previous = self._phase_evidence
            self._record_native_revision_locked(revision)
            if previous is not None:
                if revision < previous["revision"]:
                    raise SidecarError("protocol_error")
                if revision == previous["revision"]:
                    if evidence != previous:
                        raise SidecarError("protocol_error")
                    return True
            self._phase_evidence = evidence
            self._lock.notify_all()
            return True
        return False

    def _reader(self) -> None:
        ipc = self._ipc
        if ipc is None:
            return
        while True:
            with self._lock:
                if self._state in TERMINAL_STATES or self._state == "stopping":
                    return
            # Self-throttled, so this costs one stat plus an 8 KiB read every
            # few seconds.  Sampling from the only thread that is guaranteed
            # to run for the whole life of the client means the capture ring
            # is warm before a death rather than after it.
            self._sample_logs()
            try:
                message = ipc.receive(time.monotonic() + 1.0)
            except SidecarError as exc:
                if exc.code == "deadline_exceeded":
                    continue
                with self._lock:
                    stopping = self._state == "stopping"
                if stopping:
                    return
                self._terminal("failed", exc.code)
                return
            try:
                with self._lock:
                    if (
                        self._state in TERMINAL_STATES
                        or self._state == "stopping"
                    ):
                        return
                    self._last_seen_at = time.time()
                    if self._demultiplex_locked(message):
                        continue
                    if message.startswith("READY\t"):
                        self._native_ready_announced = (
                            message[len("READY\t"):] == self.player_name
                        )
                    self._messages.append(message)
                    self._lock.notify_all()
            except SidecarError as exc:
                self._terminal("failed", exc.code)
                return
            fatal = self._fatal_message(message)
            if fatal is None and message.startswith("READY\t"):
                if message[len("READY\t"):] != self.player_name:
                    fatal = SidecarError(
                        "wrong_player", "native sidecar acquired the wrong player",
                    )
            if fatal is not None:
                self._terminal("failed", fatal.code)
                return

    def _monitor(self) -> None:
        process = self._process
        if process is None:
            return
        try:
            returncode = process.wait()
        except Exception:
            self._terminal("failed", "process_wait_failed")
            return
        with self._lock:
            self._exit_code = returncode
            self._exit_observed_at = time.time()
            stopping = self._state == "stopping"
            terminal = self._state in TERMINAL_STATES
            self._exit_observed_after_terminal = terminal
        if not terminal:
            self._terminal(
                "stopped" if stopping else "failed",
                None if stopping else "process_exited",
            )
            return
        # The seat had already been given up on when the client actually died.
        # That is the most misleading case there is -- it is how a harness
        # timeout gets written up as a native crash, and how a real native
        # crash that happened moments later gets lost entirely -- so the exit
        # status is recorded again now that it is finally known.
        self._capture_exit_evidence()

    def _send(self, value: str, deadline: float) -> None:
        # A raw frame is useful only for the command currently in flight.  Do
        # not let a later protocol failure persist evidence from an earlier,
        # successfully parsed exchange.
        with self._lock:
            self._protocol_diagnostic_stage = None
            self._protocol_diagnostic_frame = None
        ipc = self._ipc
        if ipc is None:
            raise SidecarError("sidecar_unavailable")
        ipc.send(value, deadline)

    def _wait_message(
        self, deadline: float, *, diagnostic_stage: str | None = None,
    ) -> str:
        with self._lock:
            while True:
                if self._messages:
                    message = self._messages.popleft()
                    if self._stale_replies and message.startswith(
                        self._stale_replies[0],
                    ):
                        # The answer to a command that already gave up, in the
                        # order it was abandoned.  Discarding it here is what
                        # keeps a late reply from being read as this one's.
                        self._stale_replies.popleft()
                        self._discarded_late_replies += 1
                        continue
                    if diagnostic_stage is not None:
                        self._protocol_diagnostic_stage = diagnostic_stage
                        self._protocol_diagnostic_frame = message
                    return message
                if self._state in TERMINAL_STATES:
                    raise SidecarError(self._error_code or "sidecar_unavailable")
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise SidecarError("deadline_exceeded", "sidecar handshake timed out")
                self._lock.wait(min(remaining, 0.1))

    def _persist_private_protocol_diagnostic(self) -> None:
        """Persist bounded raw protocol evidence in the private run folder."""
        with self._lock:
            stage = self._protocol_diagnostic_stage
            frame = self._protocol_diagnostic_frame
        if stage is None or frame is None:
            return
        payload = json.dumps(
            {"stage": stage, "raw_frame": frame},
            ensure_ascii=False, allow_nan=False, sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
        descriptor = None
        try:
            descriptor = os.open(
                self.run_directory / "protocol-error.json",
                os.O_WRONLY | os.O_CREAT | os.O_TRUNC,
                0o600,
            )
            view = memoryview(payload)
            while view:
                written = os.write(descriptor, view)
                if written <= 0:
                    break
                view = view[written:]
        except OSError:
            pass
        finally:
            if descriptor is not None:
                os.close(descriptor)

    def private_protocol_diagnostic(self) -> MappingProxyType | None:
        """Return raw parser evidence only to the owning supervisor/tests."""
        with self._lock:
            if (
                self._protocol_diagnostic_stage is None
                or self._protocol_diagnostic_frame is None
            ):
                return None
            return MappingProxyType({
                "stage": self._protocol_diagnostic_stage,
                "raw_frame": self._protocol_diagnostic_frame,
            })

    def _commit_ready(self, ready_player: str) -> dict[str, Any]:
        if ready_player != self.player_name:
            raise SidecarError(
                "wrong_player", "native sidecar acquired the wrong player",
            )
        with self._lock:
            process = self._process
            if (
                self._state != "taking"
                or self._stop_requested
                or process is None
                or process.poll() is not None
            ):
                raise SidecarError(
                    self._error_code or "sidecar_unavailable",
                    "sidecar stopped before READY committed",
                )
            self._ready_at = time.time()
            self._set_state("ready")
            return self.public_health()

    @staticmethod
    def _fatal_message(message: str) -> SidecarError | None:
        if message.startswith("TAKE_FAILED\t"):
            return SidecarError("take_failed", "Freeciv did not grant the requested seat")
        if message.startswith("ERROR\t"):
            return SidecarError("protocol_error", "native sidecar rejected bootstrap IPC")
        if message.startswith("DISCONNECTED\t"):
            return SidecarError("disconnected", "native sidecar disconnected from Freeciv")
        return None

    def start_and_take(self) -> dict[str, Any]:
        with self._lock:
            if self._state != "new" or self._stop_requested:
                raise SidecarError("invalid_state", "sidecar has already been started")
            self._set_state("starting")
            self._started_at = time.time()
        parent: socket.socket | None = None
        child: socket.socket | None = None
        stdout_stream = None
        stderr_stream = None
        try:
            stdout_stream, stderr_stream = self._prepare_private_files()
            parent, child = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
            child.set_inheritable(True)
            connection_name = f"AE{self.generation}-{self.player_name}"[:63]
            argv = self._launch_argv(child.fileno(), connection_name)
            environment = self._safe_environment(
                self.home_directory, self.options_path,
                Path(__file__).resolve().parent.parent / "data",
            )
            with self._lock:
                if self._state != "starting" or self._stop_requested:
                    raise SidecarError(
                        "sidecar_unavailable", "sidecar stopped before launch",
                    )
                launch_options: dict[str, Any] = {
                    "cwd": self.run_directory,
                    "env": environment,
                    "stdin": subprocess.DEVNULL,
                    "stdout": stdout_stream,
                    "stderr": stderr_stream,
                    "pass_fds": (child.fileno(),),
                    "close_fds": True,
                    "start_new_session": True,
                    "shell": False,
                }
                preexec = self._core_dump_preexec()
                if preexec is not None:
                    launch_options["preexec_fn"] = preexec
                self._process = self.process_factory(argv, **launch_options)
                self._socket = parent
                self._ipc = FramedIPC(parent)
                self._set_state("handshaking")
                self._reader_thread = threading.Thread(
                    target=self._reader,
                    name=f"freeciv-agent-ipc-{self.game_id}-{self.seat_id}",
                    daemon=True,
                )
                self._monitor_thread = threading.Thread(
                    target=self._monitor,
                    name=f"freeciv-agent-process-{self.game_id}-{self.seat_id}",
                    daemon=True,
                )
                # Publish and start both monitors while holding the lifecycle
                # lock.  stop() can therefore never return before a launcher
                # subsequently publishes unjoined threads.
                self._reader_thread.start()
                self._monitor_thread.start()
            child.close()
            child = None
            stdout_stream.close()
            stderr_stream.close()
            stdout_stream = None
            stderr_stream = None
            deadline = time.monotonic() + self.handshake_timeout_s
            hello = self._wait_message(deadline)
            if hello != "HELLO\t1\tfreeciv-agent":
                raise SidecarError("protocol_error", "native sidecar HELLO is incompatible")
            self._send("HELLO\t1", deadline)
            acknowledgement = self._wait_message(deadline)
            if acknowledgement != "HELLO\tOK\t1":
                raise SidecarError("protocol_error", "native sidecar HELLO acknowledgement is invalid")
            capabilities = self._wait_message(deadline)
            _validate_native_caps(capabilities)
            with self._lock:
                if self._state != "handshaking" or not self._caps_received:
                    raise SidecarError(
                        self._error_code or "sidecar_unavailable",
                        "sidecar stopped during handshake",
                    )
                self._set_state("taking")
            self._send("TAKE", deadline)
            ready_before_take_ack: str | None = None
            take_progress = False
            while True:
                message = self._wait_message(deadline)
                fatal = self._fatal_message(message)
                if fatal is not None:
                    raise fatal
                if message in {"TAKE\tQUEUED", "TAKE\tCOMMAND_SENT", "TAKE\tREADY"}:
                    take_progress = True
                    if (
                        message == "TAKE\tREADY"
                        and ready_before_take_ack is not None
                    ):
                        return self._commit_ready(ready_before_take_ack)
                    continue
                if message.startswith("READY\t"):
                    ready_player = message[len("READY\t"):]
                    if ready_player != self.player_name:
                        raise SidecarError(
                            "wrong_player",
                            "native sidecar acquired the wrong player",
                        )
                    if take_progress:
                        return self._commit_ready(ready_player)
                    # A client which already owns the exact target can emit
                    # READY immediately after its valid CAPS.  Retain that
                    # identity proof, but do not commit until its response to
                    # this bootstrap's TAKE is observed.
                    ready_before_take_ack = ready_player
                    continue
                raise SidecarError("protocol_error", "unexpected native bootstrap message")
        except Exception as exc:
            error = exc if isinstance(exc, SidecarError) else SidecarError("launch_failed")
            self._terminal("failed", error.code)
            self.stop()
            raise error from exc
        finally:
            if child is not None:
                child.close()
            if parent is not None and parent is not self._socket:
                parent.close()
            if stdout_stream is not None:
                stdout_stream.close()
            if stderr_stream is not None:
                stderr_stream.close()

    def _require_protocol_two_locked(self) -> None:
        if (
            self._state != "ready" or not self._caps_received
            or self._protocol_version != NATIVE_PROTOCOL_VERSION
            or not self._capabilities_available
        ):
            raise SidecarError("sidecar_unavailable")

    @staticmethod
    def _request_token(value: str) -> str:
        if not isinstance(value, str) or NATIVE_TOKEN_RE.fullmatch(value) is None:
            raise SidecarError("invalid_request")
        return value

    @staticmethod
    def _snapshot_token(value: str) -> str:
        if (
            not isinstance(value, str) or len(value) > 47
            or SNAPSHOT_RE.fullmatch(value) is None
        ):
            raise SidecarError("invalid_snapshot")
        revision, serial = value[1:].split("-", 1)
        if (
            _canonical_uint(revision, MAX_NATIVE_REVISION, "invalid_snapshot") < 1
            or _canonical_uint(serial, (1 << 32) - 1, "invalid_snapshot") < 1
        ):
            raise SidecarError("invalid_snapshot")
        return value

    @staticmethod
    def _scope_view_token(value: str) -> str:
        if (
            not isinstance(value, str) or len(value) > 47
            or SCOPE_VIEW_RE.fullmatch(value) is None
        ):
            raise SidecarError("invalid_scope")
        revision, serial = value[1:].split("-", 1)
        if (
            _canonical_uint(revision, MAX_NATIVE_REVISION, "invalid_scope") < 1
            or _canonical_uint(serial, (1 << 32) - 1, "invalid_scope") < 1
        ):
            raise SidecarError("invalid_scope")
        return value

    @staticmethod
    def _state_scope_view_token(value: str) -> str:
        if (
            not isinstance(value, str) or len(value) > 47
            or STATE_SCOPE_VIEW_RE.fullmatch(value) is None
        ):
            raise SidecarError("invalid_scope")
        revision, serial = value[1:].split("-", 1)
        if (
            _canonical_uint(revision, MAX_NATIVE_REVISION, "invalid_scope") < 1
            or _canonical_uint(serial, (1 << 32) - 1, "invalid_scope") < 1
        ):
            raise SidecarError("invalid_scope")
        return value

    @staticmethod
    def _relation_scope_view_token(value: str) -> str:
        if (
            not isinstance(value, str) or len(value) > 47
            or RELATION_SCOPE_VIEW_RE.fullmatch(value) is None
        ):
            raise SidecarError("invalid_scope")
        revision, serial = value[1:].split("-", 1)
        if (
            _canonical_uint(revision, MAX_NATIVE_REVISION, "invalid_scope") < 1
            or _canonical_uint(serial, (1 << 32) - 1, "invalid_scope") < 1
        ):
            raise SidecarError("invalid_scope")
        return value

    @staticmethod
    def _actor_ref(value: str) -> str:
        if (
            not isinstance(value, str) or len(value) > 47
            or NATIVE_ACTOR_RE.fullmatch(value) is None
        ):
            raise SidecarError("invalid_actor")
        _, native_id, incarnation = value.split(":", 2)
        _canonical_uint(native_id, (1 << 31) - 1, "invalid_actor")
        _canonical_uint(incarnation, MAX_NATIVE_REVISION, "invalid_actor")
        return value

    @staticmethod
    def _action_slot(value: str) -> str:
        if not isinstance(value, str) or ACTION_SLOT_RE.fullmatch(value) is None:
            raise SidecarError("invalid_action")
        return value

    @staticmethod
    def _raise_native_error(message: str, expected_request: str) -> None:
        if not (message == "ERR" or message.startswith("ERR\t")):
            return
        fields = message.split("\t")
        if len(fields) != 4 or fields[1] != expected_request:
            raise SidecarError("protocol_error")
        code = fields[2]
        if NATIVE_REASON_RE.fullmatch(code) is None:
            raise SidecarError("protocol_error")
        # Validate canonical encoding and its C-side bound, but deliberately
        # discard detail so native text can never cross the transport boundary.
        _percent_decode(fields[3], maximum_bytes=383)
        raise SidecarError(_NATIVE_ERROR_CODES.get(code, "native_error"))

    @staticmethod
    def _raise_native_action_not_accepted(
        message: str, expected_request: str,
    ) -> None:
        """Raise only for an exact correlated pre-accept native rejection."""
        if not (message == "ERR" or message.startswith("ERR\t")):
            return
        fields = message.split("\t")
        if len(fields) != 4 or fields[1] != expected_request:
            raise SidecarError("protocol_error")
        code = fields[2]
        if NATIVE_REASON_RE.fullmatch(code) is None:
            raise SidecarError("protocol_error")
        # The detail is validated to keep framing canonical, then discarded.
        _percent_decode(fields[3], maximum_bytes=383)
        raise SidecarActionNotAccepted(
            _NATIVE_ERROR_CODES.get(code, "native_error"),
        )

    @staticmethod
    def _command_error_is_terminal(error: SidecarError) -> bool:
        """Whether one command failure is evidence that the client is broken.

        ``deadline_exceeded`` deliberately is not.  A missed reply is a latency
        observation: it says the client did not answer inside a budget the
        caller chose, and says nothing at all about whether the client is
        alive, connected or holding a seat.  Treating it as corruption is what
        let a single 1.0 s liveness poll during a turn-change refresh brick a
        healthy seat at turn 66.  Whether an abandoned command leaves this
        sidecar *usable* is a separate question about stream synchronization,
        answered by :meth:`_handle_command_failure`.
        """
        return error.code not in (
            _NONTERMINAL_NATIVE_ERRORS
            | {
                "sidecar_unavailable", "invalid_request", "invalid_snapshot",
                "invalid_scope", "invalid_actor", "invalid_page",
                "invalid_action", "invalid_argument",
                "command_in_progress", "deadline_exceeded",
            }
        )

    def _arm_stream_resync(self, prefixes: tuple[str, ...]) -> bool:
        """Remember one abandoned reply so a late answer cannot be misread.

        The native stream is a single ordered channel of untagged frames, so a
        reply the caller stopped waiting for would otherwise be consumed as the
        *next* command's answer.  Recording its frame prefixes lets it be
        recognized and discarded in order instead, which is what makes a
        timeout survivable rather than terminal.  Only replies to commands with
        no game side effect are ever abandoned this way; a mutating command's
        answer is evidence about the game and is never silently dropped.
        """
        with self._lock:
            if self._state in TERMINAL_STATES:
                return False
            if len(self._stale_replies) >= MAX_UNANSWERED_LIVENESS_REPLIES:
                # A client that owes this many replies is no longer merely
                # slow.  Fail closed, but only after real corroboration.
                return False
            self._stale_replies.append(prefixes)
            self._unanswered_replies += 1
            return True

    def _handle_command_failure(
        self,
        error: SidecarError,
        *,
        sent: bool = True,
        resync: tuple[str, ...] | None = None,
        defer_callback: bool = False,
        on_terminal: Callable[[SidecarError], None] | None = None,
    ) -> tuple[
        Callable[[int, dict[str, Any]], None] | None,
        dict[str, Any] | None,
    ]:
        """Decide what one failed command means for the whole sidecar.

        Two independent questions decide it: is the client broken, and is the
        message stream still synchronized.  A timeout answers only the second,
        and only when the request actually reached the client.  ``resync``
        names the reply this sidecar can still identify and discard when it
        eventually arrives; without it an abandoned request leaves the stream
        unrecoverable and the sidecar must be replaced -- not because the
        client died, but because this object can no longer read it safely.
        """
        code = error.code
        if code == "deadline_exceeded":
            if not sent:
                # Nothing was ever put on the wire, so nothing is owed.
                return None, None
            if resync is not None and self._arm_stream_resync(resync):
                return None, None
        elif not self._command_error_is_terminal(error):
            return None, None
        if on_terminal is not None:
            try:
                on_terminal(error)
            except Exception:
                pass
        return self._terminal("failed", code, defer_callback=defer_callback)

    def _record_native_revision(self, revision: int) -> None:
        with self._lock:
            # Pinned observations and action acknowledgements can legitimately
            # trail a newer asynchronous notification.  They may advance the
            # coalesced value, but never lower it.
            self._record_native_revision_locked(
                revision, reject_regression=False,
            )

    def _acquire_command_slot(
        self, timeout_s: float, *, queue_wait_s: float | None = None,
    ) -> float:
        """Take the single command stream, then start the caller's deadline.

        Two things happen here that used to be conflated.  A command arriving
        while another is in flight now *waits* for the reply ahead of it
        instead of being refused on sight: the client answers in under a
        millisecond at the median, so the collision an agent used to see as a
        429 is a few milliseconds of queueing.  Only a wait that outlives
        ``queue_wait_s`` is busy, and it says so as ``native_busy`` -- a
        retryable, non-terminal refusal that never happened on the wire.

        The deadline then starts *here*, after the stream is owned, rather than
        when the caller began waiting.  A queued command that inherited a
        half-spent budget would fail as ``deadline_exceeded`` -- reported to
        the agent as an unavailable sidecar -- for no reason other than having
        been second in line.
        """
        wait_s = (
            COMMAND_QUEUE_WAIT_S if queue_wait_s is None else queue_wait_s
        )
        if not self._command_lock.acquire(timeout=wait_s):
            raise SidecarError(
                "native_busy", "sidecar command stream is busy",
            )
        return time.monotonic() + timeout_s

    def _obs_open_locked(
        self, request: str, deadline: float,
    ) -> dict[str, Any]:
        """Open one pinned observation while the command lock is held."""
        with self._lock:
            self._require_protocol_two_locked()
        self._send(f"OBS_OPEN\t{request}\tstate", deadline)
        message = self._wait_message(
            deadline, diagnostic_stage="observation.opened",
        )
        fatal = self._fatal_message(message)
        if fatal is not None:
            raise fatal
        self._raise_native_error(message, request)
        fields = message.split("\t")
        if len(fields) != 5 or fields[0] != "OBS_OPENED":
            raise SidecarError("protocol_error")
        if fields[1] != request:
            raise SidecarError("protocol_error")
        snapshot_id = self._snapshot_token(fields[2])
        revision = _canonical_uint(fields[3], MAX_NATIVE_REVISION)
        row_count = _canonical_uint(fields[4], MAX_OBSERVATION_ROWS)
        if (
            revision < 1
            or int(snapshot_id[1:].split("-", 1)[0]) != revision
        ):
            raise SidecarError("protocol_error")
        self._record_native_revision(revision)
        return {
            "request_id": request,
            "snapshot_id": snapshot_id,
            "revision": revision,
            "row_count": row_count,
        }

    def _obs_open(
        self, request_id: str, timeout_s: float = 2.0,
    ) -> dict[str, Any]:
        request = self._request_token(request_id)
        acquired = False
        try:
            deadline = self._acquire_command_slot(timeout_s)
            acquired = True
            return self._obs_open_locked(request, deadline)
        except SidecarError as exc:
            if acquired:
                self._handle_command_failure(exc)
            raise
        finally:
            if acquired:
                self._command_lock.release()

    def _obs_page_locked(
        self,
        request: str,
        snapshot: str,
        revision: int,
        total_count: int,
        offset: int,
        limit: int,
        deadline: float,
    ) -> dict[str, Any]:
        """Read one observation page while the command lock is held."""
        expected_count = min(limit, total_count - offset)
        with self._lock:
            self._require_protocol_two_locked()
        self._send(
            f"OBS_PAGE\t{request}\t{snapshot}\t{offset}\t{limit}",
            deadline,
        )
        begin = self._wait_message(
            deadline, diagnostic_stage="observation.page_begin",
        )
        fatal = self._fatal_message(begin)
        if fatal is not None:
            raise fatal
        self._raise_native_error(begin, request)
        fields = begin.split("\t")
        if (
            len(fields) != 7 or fields[0] != "PAGE_BEGIN"
            or fields[1] != request or fields[2] != snapshot
            or _canonical_uint(fields[3], MAX_NATIVE_REVISION) != revision
            or _canonical_uint(fields[4], MAX_OBSERVATION_ROWS) != offset
            or _canonical_uint(fields[5], MAX_OBSERVATION_PAGE)
            != expected_count
            or _canonical_uint(fields[6], MAX_OBSERVATION_ROWS)
            != total_count
        ):
            raise SidecarError("protocol_error")
        rows: list[str] = []
        for expected_index in range(offset, offset + expected_count):
            row = self._wait_message(
                deadline,
                diagnostic_stage=f"observation.row.{expected_index}",
            )
            fatal = self._fatal_message(row)
            if fatal is not None:
                raise fatal
            self._raise_native_error(row, request)
            row_fields = row.split("\t")
            if (
                len(row_fields) != 5 or row_fields[0] != "ROW"
                or row_fields[1] != request
                or row_fields[2] != snapshot
                or _canonical_uint(
                    row_fields[3], MAX_OBSERVATION_ROWS,
                ) != expected_index
            ):
                raise SidecarError("protocol_error")
            rows.append(_percent_decode(
                row_fields[4], maximum_bytes=MAX_OBSERVATION_ROW_BYTES,
            ))
        end = self._wait_message(
            deadline, diagnostic_stage="observation.page_end",
        )
        fatal = self._fatal_message(end)
        if fatal is not None:
            raise fatal
        self._raise_native_error(end, request)
        end_fields = end.split("\t")
        next_offset = offset + expected_count
        if (
            len(end_fields) != 4 or end_fields[0] != "PAGE_END"
            or end_fields[1] != request or end_fields[2] != snapshot
            or _canonical_uint(
                end_fields[3], MAX_OBSERVATION_ROWS,
            ) != next_offset
        ):
            raise SidecarError("protocol_error")
        self._record_native_revision(revision)
        return {
            "request_id": request,
            "snapshot_id": snapshot,
            "revision": revision,
            "offset": offset,
            "count": expected_count,
            "total_count": total_count,
            "next_offset": next_offset,
            "rows": rows,
        }

    def _obs_page(
        self,
        request_id: str,
        snapshot_id: str,
        revision: int,
        total_count: int,
        offset: int,
        limit: int = MAX_OBSERVATION_PAGE,
        timeout_s: float = 2.0,
    ) -> dict[str, Any]:
        request = self._request_token(request_id)
        snapshot = self._snapshot_token(snapshot_id)
        if (
            isinstance(revision, bool) or not isinstance(revision, int)
            or revision < 1 or revision > MAX_NATIVE_REVISION
            or int(snapshot[1:].split("-", 1)[0]) != revision
            or isinstance(total_count, bool) or not isinstance(total_count, int)
            or not 0 <= total_count <= MAX_OBSERVATION_ROWS
            or isinstance(offset, bool) or not isinstance(offset, int)
            or not 0 <= offset <= total_count
            or isinstance(limit, bool) or not isinstance(limit, int)
            or not 1 <= limit <= MAX_OBSERVATION_PAGE
        ):
            raise SidecarError("invalid_page")
        acquired = False
        try:
            deadline = self._acquire_command_slot(timeout_s)
            acquired = True
            return self._obs_page_locked(
                request, snapshot, revision, total_count, offset, limit,
                deadline,
            )
        except SidecarError as exc:
            if acquired:
                self._handle_command_failure(exc)
            raise
        finally:
            if acquired:
                self._command_lock.release()

    def read_observation(
        self,
        request_id: str,
        timeout_s: float = 5.0,
        *,
        on_terminal_error: Callable[[SidecarError], None] | None = None,
    ) -> dict[str, Any]:
        """Read one coherent native snapshot without exposing native handles.

        The command lock and a single deadline cover both attempts, the open,
        and every page.  A snapshot that expires while it is being paged is
        retried once because expiration is an expected bounded cache race.
        """
        request = self._request_token(request_id)
        if (
            isinstance(timeout_s, bool)
            or not isinstance(timeout_s, (int, float))
            or not math.isfinite(timeout_s)
            or timeout_s <= 0
        ):
            raise SidecarError("invalid_request")
        if on_terminal_error is not None and not callable(on_terminal_error):
            raise SidecarError("invalid_request")
        acquired = False
        try:
            deadline = self._acquire_command_slot(timeout_s)
            acquired = True
            for attempt in range(2):
                try:
                    opened = self._obs_open_locked(request, deadline)
                    rows: list[str] = []
                    offset = 0
                    while offset < opened["row_count"]:
                        page = self._obs_page_locked(
                            request,
                            opened["snapshot_id"],
                            opened["revision"],
                            opened["row_count"],
                            offset,
                            MAX_OBSERVATION_PAGE,
                            deadline,
                        )
                        rows.extend(page["rows"])
                        offset = page["next_offset"]
                    if offset != opened["row_count"] or len(rows) != offset:
                        raise SidecarError("protocol_error")
                    return {
                        "generation": self.generation,
                        "native_revision": opened["revision"],
                        "rows": tuple(rows),
                    }
                except SidecarError as exc:
                    if exc.code != "snapshot_gone" or attempt != 0:
                        raise
            raise SidecarError("snapshot_gone")
        except SidecarError as exc:
            # A caller waiting behind another command can exhaust its own
            # budget without implying corruption or failure of the sidecar.
            if acquired:
                self._handle_command_failure(
                    exc, on_terminal=on_terminal_error,
                )
            raise
        finally:
            if acquired:
                self._command_lock.release()

    def _scope_open_locked(
        self,
        request: str,
        expected_revision: int,
        actor_ref: str,
        deadline: float,
    ) -> dict[str, Any]:
        with self._lock:
            self._require_protocol_two_locked()
        self._send(
            "SCOPE_OPEN\t%s\t%d\t%s" % (
                request, expected_revision, _percent_encode(actor_ref),
            ),
            deadline,
        )
        message = self._wait_message(
            deadline, diagnostic_stage="actor_scope.opened",
        )
        fatal = self._fatal_message(message)
        if fatal is not None:
            raise fatal
        self._raise_native_error(message, request)
        fields = message.split("\t")
        if len(fields) != 8 or fields[0] != "SCOPE_OPENED" or fields[1] != request:
            raise SidecarError("protocol_error")
        revision = _canonical_uint(fields[3], MAX_NATIVE_REVISION)
        returned_actor = self._actor_ref(
            _percent_decode(fields[4], maximum_bytes=47),
        )
        total = _canonical_uint(fields[5], MAX_OBSERVATION_ROWS)
        complete = fields[6] == "1"
        overflow = fields[7] == "1"
        if (
            revision != expected_revision
            or returned_actor != actor_ref
            or fields[6] not in {"0", "1"}
            or fields[7] not in {"0", "1"}
            or complete is overflow
        ):
            raise SidecarError("protocol_error")
        if overflow:
            if fields[2] != "-" or total != 0:
                raise SidecarError("protocol_error")
            raise SidecarError("actor_scope_too_large")
        view = self._scope_view_token(fields[2])
        if int(view[1:].split("-", 1)[0]) != revision:
            raise SidecarError("protocol_error")
        self._record_native_revision(revision)
        return {
            "view_id": view,
            "revision": revision,
            "actor_ref": returned_actor,
            "total_count": total,
            "complete": True,
            "overflow": False,
        }

    def _scope_page_locked(
        self,
        request: str,
        view_id: str,
        revision: int,
        actor_ref: str,
        total_count: int,
        offset: int,
        limit: int,
        deadline: float,
    ) -> dict[str, Any]:
        expected_count = min(limit, total_count - offset)
        with self._lock:
            self._require_protocol_two_locked()
        self._send(
            f"SCOPE_PAGE\t{request}\t{view_id}\t{offset}\t{limit}",
            deadline,
        )
        begin = self._wait_message(
            deadline, diagnostic_stage="actor_scope.page_begin",
        )
        fatal = self._fatal_message(begin)
        if fatal is not None:
            raise fatal
        self._raise_native_error(begin, request)
        fields = begin.split("\t")
        returned_actor = (
            self._actor_ref(_percent_decode(fields[4], maximum_bytes=47))
            if len(fields) == 8 else None
        )
        if (
            len(fields) != 8 or fields[0] != "SCOPE_BEGIN"
            or fields[1] != request or fields[2] != view_id
            or _canonical_uint(fields[3], MAX_NATIVE_REVISION) != revision
            or returned_actor != actor_ref
            or _canonical_uint(fields[5], MAX_OBSERVATION_ROWS) != offset
            or _canonical_uint(fields[6], MAX_OBSERVATION_PAGE) != expected_count
            or _canonical_uint(fields[7], MAX_OBSERVATION_ROWS) != total_count
        ):
            raise SidecarError("protocol_error")
        rows: list[str] = []
        for expected_index in range(offset, offset + expected_count):
            row = self._wait_message(
                deadline,
                diagnostic_stage=f"actor_scope.row.{expected_index}",
            )
            fatal = self._fatal_message(row)
            if fatal is not None:
                raise fatal
            self._raise_native_error(row, request)
            row_fields = row.split("\t")
            if (
                len(row_fields) != 5 or row_fields[0] != "SCOPE_ACTION"
                or row_fields[1] != request or row_fields[2] != view_id
                or _canonical_uint(row_fields[3], MAX_OBSERVATION_ROWS)
                != expected_index
            ):
                raise SidecarError("protocol_error")
            rows.append(_percent_decode(
                row_fields[4], maximum_bytes=MAX_OBSERVATION_ROW_BYTES,
            ))
        end = self._wait_message(
            deadline, diagnostic_stage="actor_scope.page_end",
        )
        fatal = self._fatal_message(end)
        if fatal is not None:
            raise fatal
        self._raise_native_error(end, request)
        end_fields = end.split("\t")
        next_offset = offset + expected_count
        if (
            len(end_fields) != 4 or end_fields[0] != "SCOPE_END"
            or end_fields[1] != request or end_fields[2] != view_id
            or _canonical_uint(end_fields[3], MAX_OBSERVATION_ROWS)
            != next_offset
        ):
            raise SidecarError("protocol_error")
        self._record_native_revision(revision)
        return {
            "generation": self.generation,
            "native_revision": revision,
            "actor_ref": actor_ref,
            "view_id": view_id,
            "offset": offset,
            "count": expected_count,
            "total_count": total_count,
            "next_offset": next_offset,
            "complete": True,
            "overflow": False,
            "rows": tuple(rows),
        }

    def read_actor_scope(
        self,
        request_id: str,
        expected_revision: int,
        actor_ref: str,
        limit: int = MAX_OBSERVATION_PAGE,
        timeout_s: float = 5.0,
    ) -> dict[str, Any]:
        request = self._request_token(request_id)
        actor = self._actor_ref(actor_ref)
        if (
            isinstance(expected_revision, bool)
            or not isinstance(expected_revision, int)
            or not 1 <= expected_revision <= MAX_NATIVE_REVISION
            or isinstance(limit, bool) or not isinstance(limit, int)
            or not 1 <= limit <= MAX_OBSERVATION_PAGE
            or isinstance(timeout_s, bool)
            or not isinstance(timeout_s, (int, float))
            or not math.isfinite(timeout_s) or timeout_s <= 0
        ):
            raise SidecarError("invalid_request")
        acquired = False
        try:
            deadline = self._acquire_command_slot(timeout_s)
            acquired = True
            opened = self._scope_open_locked(
                request, expected_revision, actor, deadline,
            )
            return self._scope_page_locked(
                request, opened["view_id"], opened["revision"], actor,
                opened["total_count"], 0, limit, deadline,
            )
        except SidecarError as exc:
            if acquired:
                self._handle_command_failure(exc)
            raise
        finally:
            if acquired:
                self._command_lock.release()

    def read_actor_scope_page(
        self,
        request_id: str,
        view_id: str,
        revision: int,
        actor_ref: str,
        total_count: int,
        offset: int,
        limit: int,
        timeout_s: float = 5.0,
    ) -> dict[str, Any]:
        request = self._request_token(request_id)
        view = self._scope_view_token(view_id)
        actor = self._actor_ref(actor_ref)
        if (
            isinstance(revision, bool) or not isinstance(revision, int)
            or not 1 <= revision <= MAX_NATIVE_REVISION
            or int(view[1:].split("-", 1)[0]) != revision
            or isinstance(total_count, bool) or not isinstance(total_count, int)
            or not 0 <= total_count <= MAX_OBSERVATION_ROWS
            or isinstance(offset, bool) or not isinstance(offset, int)
            or not 0 <= offset <= total_count
            or isinstance(limit, bool) or not isinstance(limit, int)
            or not 1 <= limit <= MAX_OBSERVATION_PAGE
        ):
            raise SidecarError("invalid_page")
        acquired = False
        try:
            deadline = self._acquire_command_slot(timeout_s)
            acquired = True
            return self._scope_page_locked(
                request, view, revision, actor, total_count, offset,
                limit, deadline,
            )
        except SidecarError as exc:
            if acquired:
                self._handle_command_failure(exc)
            raise
        finally:
            if acquired:
                self._command_lock.release()

    def read_actor_scope_catalog(
        self,
        request_id: str,
        expected_revision: int,
        actor_ref: str,
        timeout_s: float = 30.0,
    ) -> dict[str, Any]:
        """Open and drain one pinned actor catalog under one command lock."""
        request = self._request_token(request_id)
        actor = self._actor_ref(actor_ref)
        if (
            isinstance(expected_revision, bool)
            or not isinstance(expected_revision, int)
            or not 1 <= expected_revision <= MAX_NATIVE_REVISION
            or isinstance(timeout_s, bool)
            or not isinstance(timeout_s, (int, float))
            or not math.isfinite(timeout_s) or timeout_s <= 0
        ):
            raise SidecarError("invalid_request")
        acquired = False
        try:
            deadline = self._acquire_command_slot(timeout_s)
            acquired = True
            opened = self._scope_open_locked(
                request, expected_revision, actor, deadline,
            )
            rows: list[str] = []
            offset = 0
            while offset < opened["total_count"]:
                page = self._scope_page_locked(
                    request, opened["view_id"], opened["revision"], actor,
                    opened["total_count"], offset, MAX_OBSERVATION_PAGE,
                    deadline,
                )
                rows.extend(page["rows"])
                offset = page["next_offset"]
            if offset != opened["total_count"] or len(rows) != offset:
                raise SidecarError("protocol_error")
            return {
                "generation": self.generation,
                "native_revision": opened["revision"],
                "actor_ref": actor,
                "view_id": opened["view_id"],
                "offset": 0,
                "count": offset,
                "total_count": offset,
                "next_offset": offset,
                "complete": True,
                "overflow": False,
                "rows": tuple(rows),
            }
        except SidecarError as exc:
            if acquired:
                self._handle_command_failure(exc)
            raise
        finally:
            if acquired:
                self._command_lock.release()

    def _state_scope_open_locked(
        self,
        request: str,
        expected_revision: int,
        section: str,
        selector: str,
        deadline: float,
    ) -> dict[str, Any]:
        with self._lock:
            self._require_protocol_two_locked()
        self._send(
            "STATE_SCOPE_OPEN\t%s\t%d\t%s\t%s" % (
                request, expected_revision, section,
                _percent_encode(selector),
            ),
            deadline,
        )
        message = self._wait_message(
            deadline, diagnostic_stage="state_scope.opened",
        )
        fatal = self._fatal_message(message)
        if fatal is not None:
            raise fatal
        self._raise_native_error(message, request)
        fields = message.split("\t")
        if (
            len(fields) != 9 or fields[0] != "STATE_SCOPE_OPENED"
            or fields[1] != request
        ):
            raise SidecarError("protocol_error")
        view = self._state_scope_view_token(fields[2])
        revision = _canonical_uint(fields[3], MAX_NATIVE_REVISION)
        returned_section = _percent_decode(fields[4], maximum_bytes=31)
        returned_selector = _percent_decode(fields[5], maximum_bytes=63)
        total = _canonical_uint(fields[6], MAX_STATE_SCOPE_ROWS)
        if (
            revision != expected_revision
            or int(view[1:].split("-", 1)[0]) != revision
            or returned_section != section
            or returned_selector != selector
            or fields[7:] != ["1", "0"]
        ):
            raise SidecarError("protocol_error")
        self._record_native_revision(revision)
        return {
            "view_id": view,
            "revision": revision,
            "section": section,
            "selector": selector,
            "total_count": total,
        }

    def _state_scope_page_locked(
        self,
        request: str,
        view_id: str,
        revision: int,
        section: str,
        selector: str,
        total_count: int,
        offset: int,
        limit: int,
        deadline: float,
    ) -> dict[str, Any]:
        expected_count = min(limit, total_count - offset)
        with self._lock:
            self._require_protocol_two_locked()
        self._send(
            f"STATE_SCOPE_PAGE\t{request}\t{view_id}\t{offset}\t{limit}",
            deadline,
        )
        begin = self._wait_message(
            deadline, diagnostic_stage="state_scope.page_begin",
        )
        fatal = self._fatal_message(begin)
        if fatal is not None:
            raise fatal
        self._raise_native_error(begin, request)
        fields = begin.split("\t")
        if (
            len(fields) != 9 or fields[0] != "STATE_SCOPE_BEGIN"
            or fields[1] != request or fields[2] != view_id
            or _canonical_uint(fields[3], MAX_NATIVE_REVISION) != revision
            or _percent_decode(fields[4], maximum_bytes=31) != section
            or _percent_decode(fields[5], maximum_bytes=63) != selector
            or _canonical_uint(fields[6], MAX_STATE_SCOPE_ROWS) != offset
            or _canonical_uint(fields[7], MAX_OBSERVATION_PAGE)
               != expected_count
            or _canonical_uint(fields[8], MAX_STATE_SCOPE_ROWS) != total_count
        ):
            raise SidecarError("protocol_error")
        rows: list[str] = []
        for expected_index in range(offset, offset + expected_count):
            row = self._wait_message(
                deadline,
                diagnostic_stage=f"state_scope.row.{expected_index}",
            )
            fatal = self._fatal_message(row)
            if fatal is not None:
                raise fatal
            self._raise_native_error(row, request)
            row_fields = row.split("\t")
            if (
                len(row_fields) != 5 or row_fields[0] != "STATE_SCOPE_ROW"
                or row_fields[1] != request or row_fields[2] != view_id
                or _canonical_uint(row_fields[3], MAX_STATE_SCOPE_ROWS)
                   != expected_index
            ):
                raise SidecarError("protocol_error")
            rows.append(_percent_decode(
                row_fields[4], maximum_bytes=MAX_OBSERVATION_ROW_BYTES,
            ))
        end = self._wait_message(
            deadline, diagnostic_stage="state_scope.page_end",
        )
        fatal = self._fatal_message(end)
        if fatal is not None:
            raise fatal
        self._raise_native_error(end, request)
        end_fields = end.split("\t")
        next_offset = offset + expected_count
        if (
            len(end_fields) != 4 or end_fields[0] != "STATE_SCOPE_END"
            or end_fields[1] != request or end_fields[2] != view_id
            or _canonical_uint(end_fields[3], MAX_STATE_SCOPE_ROWS)
               != next_offset
        ):
            raise SidecarError("protocol_error")
        self._record_native_revision(revision)
        return {
            "generation": self.generation,
            "native_revision": revision,
            "section": section,
            "selector": selector,
            "view_id": view_id,
            "offset": offset,
            "count": expected_count,
            "total_count": total_count,
            "next_offset": next_offset,
            "complete": True,
            "overflow": False,
            "rows": tuple(rows),
        }

    def read_state_scope(
        self,
        request_id: str,
        expected_revision: int,
        section: str,
        selector: str,
        limit: int = MAX_OBSERVATION_PAGE,
        timeout_s: float = 5.0,
    ) -> dict[str, Any]:
        request = self._request_token(request_id)
        if (
            isinstance(expected_revision, bool)
            or not isinstance(expected_revision, int)
            or not 1 <= expected_revision <= MAX_NATIVE_REVISION
            or section not in STATE_SCOPE_SECTIONS
            or not isinstance(selector, str)
            or STATE_SCOPE_SELECTOR_RE.fullmatch(selector) is None
            or isinstance(limit, bool) or not isinstance(limit, int)
            or not 1 <= limit <= MAX_OBSERVATION_PAGE
            or isinstance(timeout_s, bool)
            or not isinstance(timeout_s, (int, float))
            or not math.isfinite(timeout_s) or timeout_s <= 0
        ):
            raise SidecarError("invalid_request")
        deadline = self._acquire_command_slot(timeout_s)
        try:
            opened = self._state_scope_open_locked(
                request, expected_revision, section, selector, deadline,
            )
            return self._state_scope_page_locked(
                request, opened["view_id"], opened["revision"], section,
                selector, opened["total_count"], 0, limit, deadline,
            )
        finally:
            self._command_lock.release()

    def read_state_scope_page(
        self,
        request_id: str,
        view_id: str,
        revision: int,
        section: str,
        selector: str,
        total_count: int,
        offset: int,
        limit: int,
        timeout_s: float = 5.0,
    ) -> dict[str, Any]:
        request = self._request_token(request_id)
        view = self._state_scope_view_token(view_id)
        if (
            isinstance(revision, bool) or not isinstance(revision, int)
            or not 1 <= revision <= MAX_NATIVE_REVISION
            or int(view[1:].split("-", 1)[0]) != revision
            or section not in STATE_SCOPE_SECTIONS
            or not isinstance(selector, str)
            or STATE_SCOPE_SELECTOR_RE.fullmatch(selector) is None
            or isinstance(total_count, bool) or not isinstance(total_count, int)
            or not 0 <= total_count <= MAX_STATE_SCOPE_ROWS
            or isinstance(offset, bool) or not isinstance(offset, int)
            or not 0 <= offset <= total_count
            or isinstance(limit, bool) or not isinstance(limit, int)
            or not 1 <= limit <= MAX_OBSERVATION_PAGE
        ):
            raise SidecarError("invalid_page")
        deadline = self._acquire_command_slot(timeout_s)
        try:
            return self._state_scope_page_locked(
                request, view, revision, section, selector, total_count,
                offset, limit, deadline,
            )
        finally:
            self._command_lock.release()

    def read_state_scope_catalog(
        self,
        request_id: str,
        expected_revision: int,
        section: str,
        selector: str,
        timeout_s: float = 30.0,
    ) -> dict[str, Any]:
        """Open and drain one pinned state catalog before public projection."""
        request = self._request_token(request_id)
        if (
            isinstance(expected_revision, bool)
            or not isinstance(expected_revision, int)
            or not 1 <= expected_revision <= MAX_NATIVE_REVISION
            or section not in STATE_SCOPE_SECTIONS
            or not isinstance(selector, str)
            or STATE_SCOPE_SELECTOR_RE.fullmatch(selector) is None
            or isinstance(timeout_s, bool)
            or not isinstance(timeout_s, (int, float))
            or not math.isfinite(timeout_s) or timeout_s <= 0
        ):
            raise SidecarError("invalid_request")
        acquired = False
        try:
            deadline = self._acquire_command_slot(timeout_s)
            acquired = True
            opened = self._state_scope_open_locked(
                request, expected_revision, section, selector, deadline,
            )
            rows: list[str] = []
            row_bytes = 0
            offset = 0
            while offset < opened["total_count"]:
                page = self._state_scope_page_locked(
                    request, opened["view_id"], opened["revision"], section,
                    selector, opened["total_count"], offset,
                    MAX_OBSERVATION_PAGE, deadline,
                )
                for row in page["rows"]:
                    row_bytes += len(row.encode("utf-8")) + 1
                    if row_bytes > MAX_STATE_SCOPE_BYTES:
                        raise SidecarError("state_scope_too_large")
                    rows.append(row)
                offset = page["next_offset"]
            if offset != opened["total_count"] or len(rows) != offset:
                raise SidecarError("protocol_error")
            return {
                "generation": self.generation,
                "native_revision": opened["revision"],
                "section": section,
                "selector": selector,
                "view_id": opened["view_id"],
                "offset": 0,
                "count": offset,
                "total_count": offset,
                "next_offset": offset,
                "complete": True,
                "overflow": False,
                "rows": tuple(rows),
            }
        except SidecarError as exc:
            if acquired:
                self._handle_command_failure(exc)
            raise
        finally:
            if acquired:
                self._command_lock.release()

    def _relation_scope_open_locked(
        self,
        request: str,
        expected_revision: int,
        actor_ref: str,
        counterpart_ref: str,
        deadline: float,
    ) -> dict[str, Any]:
        with self._lock:
            self._require_protocol_two_locked()
        self._send(
            "RELATION_SCOPE_OPEN\t%s\t%d\t%s\t%s" % (
                request, expected_revision, _percent_encode(actor_ref),
                _percent_encode(counterpart_ref),
            ),
            deadline,
        )
        message = self._wait_message(deadline)
        fatal = self._fatal_message(message)
        if fatal is not None:
            raise fatal
        try:
            self._raise_native_error(message, request)
        except SidecarError as exc:
            if exc.code == "actor_scope_too_large":
                raise SidecarError("relation_scope_too_large") from None
            raise
        fields = message.split("\t")
        if (
            len(fields) != 9 or fields[0] != "RELATION_SCOPE_OPENED"
            or fields[1] != request
        ):
            raise SidecarError("protocol_error")
        revision = _canonical_uint(fields[3], MAX_NATIVE_REVISION)
        returned_actor = self._actor_ref(
            _percent_decode(fields[4], maximum_bytes=47),
        )
        returned_counterpart = self._actor_ref(
            _percent_decode(fields[5], maximum_bytes=47),
        )
        total = _canonical_uint(fields[6], MAX_OBSERVATION_ROWS)
        complete = fields[7] == "1"
        overflow = fields[8] == "1"
        if (
            revision != expected_revision
            or returned_actor != actor_ref
            or returned_counterpart != counterpart_ref
            or fields[7] not in {"0", "1"}
            or fields[8] not in {"0", "1"}
            or complete is overflow
        ):
            raise SidecarError("protocol_error")
        if overflow:
            if fields[2] != "-" or total != 0:
                raise SidecarError("protocol_error")
            raise SidecarError("relation_scope_too_large")
        view = self._relation_scope_view_token(fields[2])
        if int(view[1:].split("-", 1)[0]) != revision:
            raise SidecarError("protocol_error")
        self._record_native_revision(revision)
        return {
            "view_id": view,
            "revision": revision,
            "actor_ref": returned_actor,
            "counterpart_ref": returned_counterpart,
            "total_count": total,
            "complete": True,
            "overflow": False,
        }

    def _relation_scope_page_locked(
        self,
        request: str,
        view_id: str,
        revision: int,
        actor_ref: str,
        counterpart_ref: str,
        total_count: int,
        offset: int,
        limit: int,
        deadline: float,
    ) -> dict[str, Any]:
        expected_count = min(limit, total_count - offset)
        with self._lock:
            self._require_protocol_two_locked()
        self._send(
            f"RELATION_SCOPE_PAGE\t{request}\t{view_id}\t{offset}\t{limit}",
            deadline,
        )
        begin = self._wait_message(deadline)
        fatal = self._fatal_message(begin)
        if fatal is not None:
            raise fatal
        self._raise_native_error(begin, request)
        fields = begin.split("\t")
        returned_actor = (
            self._actor_ref(_percent_decode(fields[4], maximum_bytes=47))
            if len(fields) == 9 else None
        )
        returned_counterpart = (
            self._actor_ref(_percent_decode(fields[5], maximum_bytes=47))
            if len(fields) == 9 else None
        )
        if (
            len(fields) != 9 or fields[0] != "RELATION_SCOPE_BEGIN"
            or fields[1] != request or fields[2] != view_id
            or _canonical_uint(fields[3], MAX_NATIVE_REVISION) != revision
            or returned_actor != actor_ref
            or returned_counterpart != counterpart_ref
            or _canonical_uint(fields[6], MAX_OBSERVATION_ROWS) != offset
            or _canonical_uint(fields[7], MAX_OBSERVATION_PAGE) != expected_count
            or _canonical_uint(fields[8], MAX_OBSERVATION_ROWS) != total_count
        ):
            raise SidecarError("protocol_error")
        rows: list[str] = []
        for expected_index in range(offset, offset + expected_count):
            row = self._wait_message(deadline)
            fatal = self._fatal_message(row)
            if fatal is not None:
                raise fatal
            self._raise_native_error(row, request)
            row_fields = row.split("\t")
            if (
                len(row_fields) != 5
                or row_fields[0] != "RELATION_SCOPE_ACTION"
                or row_fields[1] != request or row_fields[2] != view_id
                or _canonical_uint(row_fields[3], MAX_OBSERVATION_ROWS)
                   != expected_index
            ):
                raise SidecarError("protocol_error")
            rows.append(_percent_decode(
                row_fields[4], maximum_bytes=MAX_OBSERVATION_ROW_BYTES,
            ))
        end = self._wait_message(deadline)
        fatal = self._fatal_message(end)
        if fatal is not None:
            raise fatal
        self._raise_native_error(end, request)
        end_fields = end.split("\t")
        next_offset = offset + expected_count
        if (
            len(end_fields) != 4 or end_fields[0] != "RELATION_SCOPE_END"
            or end_fields[1] != request or end_fields[2] != view_id
            or _canonical_uint(end_fields[3], MAX_OBSERVATION_ROWS)
               != next_offset
        ):
            raise SidecarError("protocol_error")
        self._record_native_revision(revision)
        return {
            "generation": self.generation,
            "native_revision": revision,
            "actor_ref": actor_ref,
            "counterpart_ref": counterpart_ref,
            "view_id": view_id,
            "offset": offset,
            "count": expected_count,
            "total_count": total_count,
            "next_offset": next_offset,
            "complete": True,
            "overflow": False,
            "rows": tuple(rows),
        }

    def read_relation_scope(
        self,
        request_id: str,
        expected_revision: int,
        actor_ref: str,
        counterpart_ref: str,
        limit: int = MAX_OBSERVATION_PAGE,
        timeout_s: float = 5.0,
    ) -> dict[str, Any]:
        """Open and read the first page of one player-pair catalog."""
        request = self._request_token(request_id)
        actor = self._actor_ref(actor_ref)
        counterpart = self._actor_ref(counterpart_ref)
        if (
            actor == counterpart
            or isinstance(expected_revision, bool)
            or not isinstance(expected_revision, int)
            or not 1 <= expected_revision <= MAX_NATIVE_REVISION
            or isinstance(limit, bool) or not isinstance(limit, int)
            or not 1 <= limit <= MAX_OBSERVATION_PAGE
            or isinstance(timeout_s, bool)
            or not isinstance(timeout_s, (int, float))
            or not math.isfinite(timeout_s) or timeout_s <= 0
        ):
            raise SidecarError("invalid_request")
        acquired = False
        try:
            deadline = self._acquire_command_slot(timeout_s)
            acquired = True
            opened = self._relation_scope_open_locked(
                request, expected_revision, actor, counterpart, deadline,
            )
            return self._relation_scope_page_locked(
                request, opened["view_id"], opened["revision"], actor,
                counterpart, opened["total_count"], 0, limit, deadline,
            )
        except SidecarError as exc:
            if acquired:
                self._handle_command_failure(exc)
            raise
        finally:
            if acquired:
                self._command_lock.release()

    def read_relation_scope_page(
        self,
        request_id: str,
        view_id: str,
        revision: int,
        actor_ref: str,
        counterpart_ref: str,
        total_count: int,
        offset: int,
        limit: int,
        timeout_s: float = 5.0,
    ) -> dict[str, Any]:
        request = self._request_token(request_id)
        view = self._relation_scope_view_token(view_id)
        actor = self._actor_ref(actor_ref)
        counterpart = self._actor_ref(counterpart_ref)
        if (
            actor == counterpart
            or isinstance(revision, bool) or not isinstance(revision, int)
            or not 1 <= revision <= MAX_NATIVE_REVISION
            or int(view[1:].split("-", 1)[0]) != revision
            or isinstance(total_count, bool) or not isinstance(total_count, int)
            or not 0 <= total_count <= MAX_OBSERVATION_ROWS
            or isinstance(offset, bool) or not isinstance(offset, int)
            or not 0 <= offset <= total_count
            or isinstance(limit, bool) or not isinstance(limit, int)
            or not 1 <= limit <= MAX_OBSERVATION_PAGE
        ):
            raise SidecarError("invalid_page")
        acquired = False
        try:
            deadline = self._acquire_command_slot(timeout_s)
            acquired = True
            return self._relation_scope_page_locked(
                request, view, revision, actor, counterpart, total_count,
                offset, limit, deadline,
            )
        except SidecarError as exc:
            if acquired:
                self._handle_command_failure(exc)
            raise
        finally:
            if acquired:
                self._command_lock.release()

    def read_relation_scope_catalog(
        self,
        request_id: str,
        expected_revision: int,
        actor_ref: str,
        counterpart_ref: str,
        timeout_s: float = 30.0,
    ) -> dict[str, Any]:
        """Open and drain one pinned relation catalog atomically."""
        request = self._request_token(request_id)
        actor = self._actor_ref(actor_ref)
        counterpart = self._actor_ref(counterpart_ref)
        if (
            actor == counterpart
            or isinstance(expected_revision, bool)
            or not isinstance(expected_revision, int)
            or not 1 <= expected_revision <= MAX_NATIVE_REVISION
            or isinstance(timeout_s, bool)
            or not isinstance(timeout_s, (int, float))
            or not math.isfinite(timeout_s) or timeout_s <= 0
        ):
            raise SidecarError("invalid_request")
        acquired = False
        try:
            deadline = self._acquire_command_slot(timeout_s)
            acquired = True
            opened = self._relation_scope_open_locked(
                request, expected_revision, actor, counterpart, deadline,
            )
            rows: list[str] = []
            offset = 0
            while offset < opened["total_count"]:
                page = self._relation_scope_page_locked(
                    request, opened["view_id"], opened["revision"], actor,
                    counterpart, opened["total_count"], offset,
                    MAX_OBSERVATION_PAGE, deadline,
                )
                rows.extend(page["rows"])
                offset = page["next_offset"]
            if offset != opened["total_count"] or len(rows) != offset:
                raise SidecarError("protocol_error")
            return {
                "generation": self.generation,
                "native_revision": opened["revision"],
                "actor_ref": actor,
                "counterpart_ref": counterpart,
                "view_id": opened["view_id"],
                "offset": 0,
                "count": offset,
                "total_count": offset,
                "next_offset": offset,
                "complete": True,
                "overflow": False,
                "rows": tuple(rows),
            }
        except SidecarError as exc:
            if acquired:
                self._handle_command_failure(exc)
            raise
        finally:
            if acquired:
                self._command_lock.release()

    def read_target_action(
        self,
        request_id: str,
        expected_revision: int,
        actor_ref: str,
        native_tile: int,
        timeout_s: float = 5.0,
    ) -> dict[str, Any]:
        """Read one bounded, server-authored target capability catalog."""
        request = self._request_token(request_id)
        actor = self._actor_ref(actor_ref)
        if (
            isinstance(expected_revision, bool)
            or not isinstance(expected_revision, int)
            or not 1 <= expected_revision <= MAX_NATIVE_REVISION
            or isinstance(native_tile, bool)
            or not isinstance(native_tile, int)
            or not 0 <= native_tile <= MAX_NATIVE_PHASE_INTEGER
            or isinstance(timeout_s, bool)
            or not isinstance(timeout_s, (int, float))
            or not math.isfinite(timeout_s) or timeout_s <= 0
        ):
            raise SidecarError("invalid_request")
        command = (
            f"TARGET_ACTION\t{request}\t{expected_revision}\t"
            f"{_percent_encode(actor)}\t{native_tile}"
        )
        acquired = False
        try:
            deadline = self._acquire_command_slot(timeout_s)
            acquired = True
            with self._lock:
                self._require_protocol_two_locked()
            self._send(command, deadline)
            message = self._wait_message(deadline)
            fatal = self._fatal_message(message)
            if fatal is not None:
                raise fatal
            self._raise_native_error(message, request)
            fields = message.split("\t")
            returned_actor = (
                _percent_decode(fields[3], maximum_bytes=47)
                if len(fields) == 6 else None
            )
            count = (
                _canonical_uint(fields[5], 256)
                if len(fields) == 6 else None
            )
            if (
                len(fields) != 6 or fields[0] != "TARGET_BEGIN"
                or fields[1] != request
                or _canonical_uint(fields[2], MAX_NATIVE_REVISION)
                   != expected_revision
                or returned_actor != actor
                or _canonical_uint(fields[4], MAX_NATIVE_PHASE_INTEGER)
                   != native_tile
                or count is None
            ):
                raise SidecarError("protocol_error")
            rows: list[str] = []
            for index in range(count):
                message = self._wait_message(deadline)
                fatal = self._fatal_message(message)
                if fatal is not None:
                    raise fatal
                self._raise_native_error(message, request)
                row_fields = message.split("\t")
                if (
                    len(row_fields) != 4
                    or row_fields[0] != "TARGET_ROW"
                    or row_fields[1] != request
                    or _canonical_uint(row_fields[2], 255) != index
                ):
                    raise SidecarError("protocol_error")
                rows.append(_percent_decode(
                    row_fields[3], maximum_bytes=MAX_OBSERVATION_ROW_BYTES,
                ))
            message = self._wait_message(deadline)
            fatal = self._fatal_message(message)
            if fatal is not None:
                raise fatal
            self._raise_native_error(message, request)
            end_fields = message.split("\t")
            if (
                len(end_fields) != 3
                or end_fields[0] != "TARGET_END"
                or end_fields[1] != request
                or _canonical_uint(end_fields[2], 256) != count
            ):
                raise SidecarError("protocol_error")
            self._record_native_revision(expected_revision)
            return {
                "generation": self.generation,
                "native_revision": expected_revision,
                "actor_ref": actor,
                "native_tile": native_tile,
                "count": count,
                "rows": tuple(rows),
            }
        except SidecarError as exc:
            if acquired:
                self._handle_command_failure(exc)
            raise
        finally:
            if acquired:
                self._command_lock.release()

    def _act(
        self,
        request_id: str,
        action_slot: str,
        arguments: str = "-",
        timeout_s: float = 20.0,
        *,
        expected_revision: int | None = None,
        actor_ref: str | None = None,
        counterpart_ref: str | None = None,
        on_accepted: Callable[[dict[str, Any]], None] | None = None,
        on_ambiguous: Callable[[SidecarActionAmbiguous], None] | None = None,
    ) -> dict[str, Any]:
        request = self._request_token(request_id)
        slot = self._action_slot(action_slot)
        if not isinstance(arguments, str) or not arguments:
            raise SidecarError("invalid_argument")
        if (
            isinstance(timeout_s, bool)
            or not isinstance(timeout_s, (int, float))
            or not math.isfinite(timeout_s)
            or timeout_s <= 0
        ):
            raise SidecarError("invalid_argument")
        if (
            expected_revision is not None
            and (
                isinstance(expected_revision, bool)
                or not isinstance(expected_revision, int)
                or not 1 <= expected_revision <= MAX_NATIVE_REVISION
            )
        ):
            raise SidecarError("invalid_argument")
        if on_accepted is not None and not callable(on_accepted):
            raise SidecarError("invalid_argument")
        if on_ambiguous is not None and not callable(on_ambiguous):
            raise SidecarError("invalid_argument")
        encoded_arguments = _percent_encode(arguments)
        if actor_ref is None:
            if counterpart_ref is not None:
                raise SidecarError("invalid_argument")
            command = f"ACT\t{request}\t{slot}\t{encoded_arguments}"
        else:
            actor = self._actor_ref(actor_ref)
            if expected_revision is None:
                raise SidecarError("invalid_argument")
            if counterpart_ref is None:
                command = (
                    f"ACT_CAP\t{request}\t{expected_revision}\t"
                    f"{_percent_encode(actor)}\t{slot}\t{encoded_arguments}"
                )
            else:
                counterpart = self._actor_ref(counterpart_ref)
                if counterpart == actor:
                    raise SidecarError("invalid_argument")
                command = (
                    f"ACT_RELATION_CAP\t{request}\t{expected_revision}\t"
                    f"{_percent_encode(actor)}\t"
                    f"{_percent_encode(counterpart)}\t{slot}\t"
                    f"{encoded_arguments}"
                )
        if len(command.encode("utf-8")) > MAX_FRAME:
            raise SidecarError("invalid_argument")
        acquired = False
        send_may_have_begun = False
        deferred_exit: tuple[
            Callable[[int, dict[str, Any]], None] | None,
            dict[str, Any] | None,
        ] = (None, None)
        try:
            deadline = self._acquire_command_slot(timeout_s)
            acquired = True
            with self._lock:
                self._require_protocol_two_locked()

            # From the instant _send is invoked, its frame may have been
            # partially written even if the call raises.  There is no safe
            # replay boundary until a correlated native ERR proves rejection.
            send_may_have_begun = True
            try:
                self._send(command, deadline)
            except Exception:
                raise SidecarActionAmbiguous(
                    None, "acceptance_unavailable",
                ) from None

            try:
                accepted = self._wait_message(deadline)
                fatal = self._fatal_message(accepted)
                if fatal is not None:
                    raise fatal
                self._raise_native_action_not_accepted(accepted, request)
                fields = accepted.split("\t")
                if (
                    len(fields) != 5 or fields[0] != "ACT_ACCEPTED"
                    or fields[1] != request or fields[2] != slot
                ):
                    raise SidecarError("protocol_error")
                native_request_id = _canonical_uint(fields[3], (1 << 31) - 1)
                accepted_revision = _canonical_uint(
                    fields[4], MAX_NATIVE_REVISION,
                )
                if native_request_id < 1 or accepted_revision < 1:
                    raise SidecarError("protocol_error")
            except SidecarActionNotAccepted:
                raise
            except SidecarActionAmbiguous:
                raise
            except Exception:
                raise SidecarActionAmbiguous(
                    None, "acceptance_unavailable",
                ) from None

            acceptance = {
                "request_id": request,
                "accepted": True,
                "accepted_revision": accepted_revision,
            }
            try:
                self._record_native_revision(accepted_revision)
                if on_accepted is not None:
                    try:
                        with self._command_lock.callback_scope():
                            on_accepted(dict(acceptance))
                    except Exception:
                        raise SidecarActionAmbiguous(
                            acceptance, "acceptance_callback_failed",
                        ) from None
                if (
                    expected_revision is not None
                    and accepted_revision != expected_revision
                ):
                    raise SidecarActionAmbiguous(
                        acceptance, "accepted_revision_mismatch",
                    )

                result = self._wait_message(deadline)
                fatal = self._fatal_message(result)
                if fatal is not None:
                    raise fatal
                self._raise_native_error(result, request)
                result_fields = result.split("\t")
                if (
                    len(result_fields) != 8
                    or result_fields[0] != "ACT_RESULT"
                    or result_fields[1] != request
                    or result_fields[2] != slot
                    or result_fields[3] not in {
                        "applied", "rejected", "timeout",
                    }
                    or NATIVE_REASON_RE.fullmatch(result_fields[4]) is None
                    or _canonical_uint(
                        result_fields[5], (1 << 31) - 1,
                    ) != native_request_id
                ):
                    raise SidecarError("protocol_error")
                expected_reasons = {
                    "applied": {"POSTCONDITION_VERIFIED"},
                    "rejected": {
                        "POSTCONDITION_NOT_MET",
                        "PROCESSING_BOUNDARY_MISMATCH",
                        "SEAT_EPOCH_CHANGED",
                    },
                    "timeout": {"PROCESSING_TIMEOUT"},
                }[result_fields[3]]
                if result_fields[4] not in expected_reasons:
                    raise SidecarError("protocol_error")
                result_revision = _canonical_uint(
                    result_fields[6], MAX_NATIVE_REVISION,
                )
                observation_selector = result_fields[7]
                if (
                    observation_selector != "-"
                    and INVESTIGATION_SELECTOR_RE.fullmatch(
                        observation_selector,
                    ) is None
                ):
                    raise SidecarError("protocol_error")
                if (
                    result_fields[3] != "applied"
                    and observation_selector != "-"
                ):
                    raise SidecarError("protocol_error")
                if result_revision < accepted_revision:
                    raise SidecarError("protocol_error")
                self._record_native_revision(result_revision)
                if result_fields[4] in {
                    "PROCESSING_BOUNDARY_MISMATCH",
                    "SEAT_EPOCH_CHANGED",
                    "PROCESSING_TIMEOUT",
                }:
                    # Native emits these only after clearing the correlated
                    # request boundary (seat-epoch invalidation clears first;
                    # normal results clear before the event loop can service
                    # another command).  The outcome remains non-retryable and
                    # unknown, but the fully parsed stream stays synchronized.
                    raise SidecarActionAmbiguous(
                        acceptance,
                        {
                            "PROCESSING_BOUNDARY_MISMATCH":
                                "processing_boundary_mismatch",
                            "SEAT_EPOCH_CHANGED": "seat_epoch_changed",
                            "PROCESSING_TIMEOUT": "processing_timeout",
                        }[result_fields[4]],
                        stage="correlated_terminal",
                        stream_synchronized=True,
                    )
                status = result_fields[3]
                return {
                    "request_id": request,
                    "action_slot": slot,
                    "accepted": True,
                    "applied": status == "applied",
                    "status": status,
                    "reason": result_fields[4],
                    "native_request_id": native_request_id,
                    "accepted_revision": accepted_revision,
                    "result_revision": result_revision,
                    "observation_selector": (
                        None if observation_selector == "-"
                        else observation_selector
                    ),
                }
            except SidecarActionAmbiguous:
                raise
            except Exception:
                raise SidecarActionAmbiguous(
                    acceptance, "result_unavailable",
                ) from None
        except SidecarError as exc:
            if isinstance(exc, SidecarActionAmbiguous) and on_ambiguous is not None:
                try:
                    on_ambiguous(exc)
                except Exception:
                    pass
            if not (
                isinstance(exc, SidecarActionAmbiguous)
                and exc.stream_synchronized
            ):
                # Publish terminal state while the action still owns the
                # command gate, then release it before invoking external
                # on_exit code.  No command can slip through in between.
                #
                # An action's reply is never abandoned for resynchronization:
                # it is the only evidence of whether the game applied the act,
                # so a timeout here must reach the supervisor's ambiguity
                # handling rather than be quietly discarded later.
                deferred_exit = self._handle_command_failure(
                    exc, sent=send_may_have_begun, defer_callback=True,
                )
            raise
        finally:
            if acquired:
                self._command_lock.release()
            callback, health = deferred_exit
            if callback is not None and health is not None:
                self._invoke_exit_callback(
                    callback, self.generation, health,
                )

    def execute_action(
        self,
        request_id: str,
        action_slot: str,
        arguments: str = "-",
        timeout_s: float = 20.0,
        *,
        expected_revision: int | None = None,
        on_accepted: Callable[[dict[str, Any]], None] | None = None,
        on_ambiguous: Callable[[SidecarActionAmbiguous], None] | None = None,
    ) -> dict[str, Any]:
        """Execute one native slot with an explicit durability boundary."""
        result = self._act(
            request_id,
            action_slot,
            arguments,
            timeout_s,
            expected_revision=expected_revision,
            on_accepted=on_accepted,
            on_ambiguous=on_ambiguous,
        )
        return {
            "request_id": result["request_id"],
            "accepted": result["accepted"],
            "applied": result["applied"],
            "status": result["status"],
            "reason": result["reason"],
            "accepted_revision": result["accepted_revision"],
            "result_revision": result["result_revision"],
        }

    def execute_scoped_action(
        self,
        request_id: str,
        expected_revision: int,
        actor_ref: str,
        action_slot: str,
        arguments: str = "-",
        timeout_s: float = 20.0,
        *,
        on_accepted: Callable[[dict[str, Any]], None] | None = None,
        on_ambiguous: Callable[[SidecarActionAmbiguous], None] | None = None,
    ) -> dict[str, Any]:
        """Execute one actor-bound capability by stateless re-enumeration."""
        result = self._act(
            request_id,
            action_slot,
            arguments,
            timeout_s,
            expected_revision=expected_revision,
            actor_ref=actor_ref,
            on_accepted=on_accepted,
            on_ambiguous=on_ambiguous,
        )
        return {
            "request_id": result["request_id"],
            "accepted": result["accepted"],
            "applied": result["applied"],
            "status": result["status"],
            "reason": result["reason"],
            "accepted_revision": result["accepted_revision"],
            "result_revision": result["result_revision"],
        }

    def execute_relation_scoped_action(
        self,
        request_id: str,
        expected_revision: int,
        actor_ref: str,
        counterpart_ref: str,
        action_slot: str,
        arguments: str = "-",
        timeout_s: float = 20.0,
        *,
        on_accepted: Callable[[dict[str, Any]], None] | None = None,
        on_ambiguous: Callable[[SidecarActionAmbiguous], None] | None = None,
    ) -> dict[str, Any]:
        """Execute a pair-bound diplomacy capability by re-enumeration."""
        result = self._act(
            request_id,
            action_slot,
            arguments,
            timeout_s,
            expected_revision=expected_revision,
            actor_ref=actor_ref,
            counterpart_ref=counterpart_ref,
            on_accepted=on_accepted,
            on_ambiguous=on_ambiguous,
        )
        return {
            "request_id": result["request_id"],
            "accepted": result["accepted"],
            "applied": result["applied"],
            "status": result["status"],
            "reason": result["reason"],
            "accepted_revision": result["accepted_revision"],
            "result_revision": result["result_revision"],
        }

    def status(self, timeout_s: float = 2.0) -> str:
        """Sample one liveness answer from the client.

        This is the cheapest question the boundary can be asked and the one
        whose failure means the least: a client rebuilding its whole state at a
        turn boundary can miss the budget while perfectly healthy, so a timeout
        here leaves the sidecar usable and merely owes one STATUS reply.  The
        caller decides what a missed sample means; this object refuses to
        decide that a slow client is a dead one.

        Unlike an agent's command this one waits without a bound for the stream,
        because it is issued from a background poller with nobody to answer
        ``busy`` to, and because yielding the stream to real agent work is the
        point: a probe that waits costs a background thread nothing, while a
        probe that jumped the queue costs the agent a turn.
        """
        sent = False
        asked = False
        try:
            with self._command_lock:
                deadline = time.monotonic() + timeout_s
                with self._lock:
                    if self._state != "ready":
                        raise SidecarError("sidecar_unavailable")
                # From here the frame may be partially on the wire even if the
                # call raises, and a half-written frame is a corrupt stream
                # rather than an owed reply.  Only a completed send leaves a
                # reply that can be identified and discarded later.
                sent = True
                self._send("STATUS", deadline)
                asked = True
                while True:
                    message = self._wait_message(deadline)
                    fatal = self._fatal_message(message)
                    if fatal is not None:
                        raise fatal
                    if message.startswith("STATUS\t"):
                        fields: dict[str, str] = {}
                        for part in message.split("\t")[1:]:
                            if "=" in part:
                                name, value = part.split("=", 1)
                                fields[name] = value
                        if (
                            set(fields) != {
                                "state", "server", "seat", "player",
                                "lifecycle",
                            }
                            or fields["server"] not in {"0", "1"}
                            or not fields["state"] or not fields["seat"]
                        ):
                            raise SidecarError("protocol_error", "native STATUS is malformed")
                        try:
                            native_player = int(fields["player"], 10)
                            native_lifecycle = int(fields["lifecycle"], 10)
                        except (TypeError, ValueError):
                            raise SidecarError(
                                "protocol_error", "native STATUS is malformed",
                            ) from None
                        owns_seat = fields["seat"] == "ready"
                        if (
                            native_player < -1
                            or native_lifecycle < 0
                            or owns_seat
                            != (native_player >= 0 and native_lifecycle > 0)
                        ):
                            raise SidecarError(
                                "protocol_error", "native STATUS is malformed",
                            )
                        with self._lock:
                            self._client_state = fields["state"]
                            self._server_connected = fields["server"] == "1"
                            self._seat_state = fields["seat"]
                            self._native_player_number = (
                                native_player if owns_seat else None
                            )
                            self._native_player_lifecycle = (
                                native_lifecycle if owns_seat else None
                            )
                        return message
                    raise SidecarError("protocol_error")
        except SidecarError as exc:
            self._handle_command_failure(
                exc, sent=sent,
                resync=("STATUS\t",) if asked else None,
            )
            raise

    def ping(self, token: str, timeout_s: float = 2.0) -> bool:
        if not PING_RE.fullmatch(token):
            raise SidecarError("invalid_ping")
        sent = False
        asked = False
        try:
            with self._command_lock:
                deadline = time.monotonic() + timeout_s
                with self._lock:
                    if self._state != "ready":
                        raise SidecarError("sidecar_unavailable")
                sent = True
                self._send(f"PING\t{token}", deadline)
                asked = True
                while True:
                    message = self._wait_message(deadline)
                    fatal = self._fatal_message(message)
                    if fatal is not None:
                        raise fatal
                    if message == f"PONG\t{token}":
                        return True
                    raise SidecarError("protocol_error")
        except SidecarError as exc:
            # Like STATUS, a PONG carries no game state, so a late one can be
            # recognized by its token and thrown away.
            self._handle_command_failure(
                exc, sent=sent,
                resync=(f"PONG\t{token}",) if asked else None,
            )
            raise

    @staticmethod
    def _phase_after_revision(value: int) -> int:
        if (
            isinstance(value, bool) or not isinstance(value, int)
            or value < 0 or value > MAX_NATIVE_REVISION
        ):
            raise SidecarError("invalid_argument")
        return value

    @staticmethod
    def _phase_timeout(value: float) -> float:
        if (
            isinstance(value, bool) or not isinstance(value, (int, float))
            or not math.isfinite(value) or value <= 0
            or value > threading.TIMEOUT_MAX
        ):
            raise SidecarError("invalid_argument")
        return float(value)

    @staticmethod
    def _immutable_phase_evidence(
        evidence: dict[str, Any],
    ) -> MappingProxyType:
        return MappingProxyType(dict(evidence))

    def phase_evidence(self) -> MappingProxyType | None:
        """Return the latest sanitized phase fact without issuing a command."""
        with self._lock:
            if (
                self._state in TERMINAL_STATES or self._stop_requested
                or self._phase_evidence is None
            ):
                return None
            return self._immutable_phase_evidence(self._phase_evidence)

    def wait_phase_evidence(
        self, after_revision: int, timeout_s: float,
    ) -> MappingProxyType:
        """Wait for phase evidence newer than ``after_revision``.

        This read uses only the lifecycle condition.  It never takes the
        command gate and never sends a native sidecar command.
        """
        after = self._phase_after_revision(after_revision)
        timeout = self._phase_timeout(timeout_s)
        deadline = time.monotonic() + timeout
        acquired = self._lock.acquire(timeout=timeout)
        if not acquired:
            raise SidecarError("deadline_exceeded")
        try:
            while True:
                if self._state in TERMINAL_STATES or self._stop_requested:
                    raise SidecarError(
                        self._error_code or "sidecar_unavailable",
                    )
                evidence = self._phase_evidence
                if evidence is not None and evidence["revision"] > after:
                    return self._immutable_phase_evidence(evidence)
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise SidecarError("deadline_exceeded")
                self._lock.wait(remaining)
        finally:
            self._lock.release()

    def stop(self) -> dict[str, Any]:
        stop_new = False
        with self._lock:
            self._stop_requested = True
            self._phase_evidence = None
            self._native_ready_announced = False
            self._lock.notify_all()
            process = self._process
            if (
                self._state == "stopped"
                and (process is None or process.poll() is not None)
            ):
                return self.public_health()
            if self._state == "new":
                stop_new = True
            elif self._state not in TERMINAL_STATES:
                self._set_state("stopping")
            ipc = self._ipc
        if stop_new:
            self._terminal("stopped")
            return self.public_health()
        if process is not None and process.poll() is None and ipc is not None:
            try:
                deadline = time.monotonic() + self.stop_timeout_s
                ipc.send("SHUTDOWN", deadline)
                process.wait(timeout=self.stop_timeout_s)
            except Exception:
                pass
        if process is not None and process.poll() is None:
            try:
                self._signal_process(process, signal.SIGTERM)
                process.wait(timeout=self.stop_timeout_s)
            except subprocess.TimeoutExpired:
                try:
                    self._signal_process(process, signal.SIGKILL)
                    process.wait(timeout=self.stop_timeout_s)
                except Exception:
                    pass
            except (OSError, ProcessLookupError):
                pass
        sock = self._socket
        if sock is not None:
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                sock.close()
            except OSError:
                pass
        terminal_state: str | None = None
        terminal_error: str | None = None
        with self._lock:
            if process is not None:
                self._exit_code = process.poll()
            process_exited = process is None or self._exit_code is not None
            if self._state not in TERMINAL_STATES:
                if process_exited:
                    terminal_state = "stopped"
                else:
                    terminal_state = "failed"
                    terminal_error = "stop_failed"
            elif self._state == "stopped" and not process_exited:
                # This should be unreachable for newly-created sidecars, but
                # fail closed if an injected process violates the invariant.
                self._state = "failed"
                self._error_code = "stop_failed"
                self._stopped_at = time.time()
                self._lock.notify_all()
        if terminal_state is not None:
            self._terminal(terminal_state, terminal_error)
        current = threading.current_thread()
        for thread in (self._reader_thread, self._monitor_thread):
            if thread is not None and thread is not current:
                thread.join(timeout=self.stop_timeout_s + 0.25)
        if self._process is not None:
            # A sidecar that was already terminal when stop() ran did not
            # capture the signal stop() then delivered.  Recording it is what
            # tells a later reader that the harness killed this client, rather
            # than leaving a SIGKILL to be read as a native fault.
            self._capture_exit_evidence()
        return self.public_health()

    @staticmethod
    def _signal_process(process: Any, sig: signal.Signals) -> None:
        """Signal the private process group, with a test-double fallback."""
        pid = getattr(process, "pid", None)
        if isinstance(pid, int) and pid > 1:
            try:
                os.killpg(pid, sig)
                return
            except (OSError, ProcessLookupError):
                if process.poll() is not None:
                    return
        if sig == signal.SIGTERM:
            process.terminate()
        else:
            process.kill()

    def public_health(self) -> dict[str, Any]:
        # Poll the child here rather than reporting only what the monitor has
        # reaped: the exit callback fires synchronously from whichever thread
        # observed the failure, and a health snapshot that says "failed" with
        # no exit status and no liveness reads as "the client vanished" when
        # the truth may be "the client is still running and merely slow".
        process = self._process
        try:
            returncode = None if process is None else process.poll()
        except Exception:
            returncode = None
        with self._lock:
            if returncode is None:
                returncode = self._exit_code
            elif self._exit_code is None:
                self._exit_code = returncode
                self._exit_observed_at = time.time()
            return {
                "state": self._state,
                "generation": self.generation,
                "player_name": self.player_name,
                "started_at": self._started_at,
                "ready_at": self._ready_at,
                "last_seen_at": self._last_seen_at,
                "stopped_at": self._stopped_at,
                "exit_code": self._exit_code,
                **self._exit_signal_fields(self._exit_code),
                "process_alive": (
                    process is not None and self._exit_code is None
                ),
                "error_code": self._error_code,
                "client_state": self._client_state,
                "server_connected": self._server_connected,
                "seat_state": self._seat_state,
                "protocol_version": self._protocol_version,
                "native_revision": self._native_revision,
                "capabilities_available": self._capabilities_available,
                "phase_evidence_available": (
                    self._phase_evidence is not None
                    and self._state not in TERMINAL_STATES
                    and not self._stop_requested
                ),
            }

    @staticmethod
    def _tail_bytes(path: Path, maximum_bytes: int) -> bytes | None:
        """Read the end of one file, or None when it cannot be read at all."""
        try:
            with open(path, "rb") as handle:
                handle.seek(0, os.SEEK_END)
                size = handle.tell()
                handle.seek(max(0, size - maximum_bytes))
                return handle.read(maximum_bytes)
        except OSError:
            return None

    @staticmethod
    def _tail_lines(data: bytes, lines: int) -> tuple[str, ...]:
        text = data.decode("utf-8", "replace")
        return tuple(
            line[:512] for line in text.splitlines()[-lines:] if line.strip()
        )

    def _rotate_log(self, path: Path, size: int) -> tuple[int, int]:
        """Cap one private log, keeping its tail; return (size, dropped).

        The client owns the writing end and must never be blocked or made to
        fail by the harness, so the file is truncated in place under O_APPEND
        rather than renamed or piped.  A write racing this rotation can lose a
        partial line; that is acceptable for a diagnostic log and happens only
        once per :data:`LOG_ROTATE_BYTES` of output.
        """
        tail = self._tail_bytes(path, LOG_ROTATE_KEEP_BYTES)
        if tail is None:
            return size, 0
        marker = (
            b"--- earlier output dropped by the sidecar log cap ---\n"
        )
        descriptor = None
        try:
            descriptor = os.open(path, os.O_WRONLY | os.O_APPEND)
            os.truncate(descriptor, 0)
            os.write(descriptor, marker)
            os.write(descriptor, tail)
        except OSError:
            return size, 0
        finally:
            if descriptor is not None:
                try:
                    os.close(descriptor)
                except OSError:
                    pass
        return len(marker) + len(tail), max(0, size - len(tail))

    def _sample_logs(self, *, force: bool = False) -> None:
        """Refresh the capture ring and hold the private logs under their cap.

        The child writes its own diagnostics, so the harness cannot line-buffer
        them; what it can do is keep its own copy of the end of each stream and
        observe *when* the stream last grew.  A client that writes nothing at
        all between startup and death is the turn-66 signature, and it is only
        legible as evidence if the silence itself was recorded.
        """
        now = time.monotonic()
        with self._log_lock:
            if (
                not force and self._log_swept_at is not None
                and now - self._log_swept_at < LOG_SWEEP_INTERVAL_S
            ):
                return
            self._log_swept_at = now
            for name, path in (
                ("stdout", self.stdout_path), ("stderr", self.stderr_path),
            ):
                entry = self._log_rings[name]
                try:
                    stats = os.stat(path)
                except OSError:
                    continue
                size = stats.st_size
                if size > 0:
                    # The client is the only other writer, so the file's own
                    # modification time is the moment it last said anything --
                    # a far more useful fact than when this sweep noticed.
                    entry["last_output_at"] = stats.st_mtime
                dropped = 0
                if size > LOG_ROTATE_BYTES:
                    # Keep the client's last write time across the rotation:
                    # the mtime after this point is the harness's, not its.
                    size, dropped = self._rotate_log(path, size)
                entry["dropped_bytes"] += dropped
                # Everything this stream has ever held, whether or not it is
                # still on disk, to within the one marker line each rotation
                # adds.  What it is for is answering "did the client ever say
                # anything", so exactness below a line does not matter.
                entry["total_bytes"] = entry["dropped_bytes"] + size
                entry["bytes"] = size
                data = self._tail_bytes(path, LOG_RING_BYTES)
                if data is not None:
                    entry["data"] = data

    def _log_evidence(self, name: str, path: Path, lines: int) -> dict[str, Any]:
        """Tail plus growth history for one private client stream."""
        with self._log_lock:
            entry = dict(self._log_rings[name])
        data = self._tail_bytes(path, LOG_TAIL_BYTES)
        if data is None:
            # The file is gone or unreadable; the ring is the only copy left.
            data = entry["data"]
        return {
            "tail": self._tail_lines(data, lines),
            "bytes": entry["total_bytes"],
            "dropped_bytes": entry["dropped_bytes"],
            "last_output_at": entry["last_output_at"],
        }

    def _persist_exit_forensics(self, forensics: Mapping[str, Any]) -> None:
        """Write the exit evidence beside the client, before anyone asks.

        The supervisor normally collects this through the exit callback, but a
        supervisor can itself be gone, restarted or blocked when a client dies.
        A death that is only ever reported in memory is a death that can be
        lost, so it is also recorded in the sidecar's own private directory.

        The record is staged and renamed into place rather than written over
        the live file.  Both lifecycle threads reach a death independently --
        the reader sees the stream end, the monitor reaps the process -- and
        two writers truncating and filling the same path can leave one record
        with the tail of another after it, which is not JSON and not evidence.
        A rename is atomic, so every reader sees one whole record and the last
        writer wins outright.
        """
        record = {
            "game_id": self.game_id,
            "seat_id": self.seat_id,
            "generation": self.generation,
            "player_name": self.player_name,
            "timestamp": time.time(),
            **{
                key: (list(value) if isinstance(value, tuple) else value)
                for key, value in forensics.items()
            },
        }
        descriptor = None
        final_path = self.run_directory / EXIT_FORENSICS_FILENAME
        staged_path = self.run_directory / (
            f"{EXIT_FORENSICS_FILENAME}.{os.getpid()}."
            f"{threading.get_ident()}.tmp"
        )
        staged = False
        try:
            payload = json.dumps(
                record, ensure_ascii=False, allow_nan=False, sort_keys=True,
                separators=(",", ":"),
            ).encode("utf-8")
            descriptor = os.open(
                staged_path,
                os.O_WRONLY | os.O_CREAT | os.O_TRUNC,
                0o600,
            )
            staged = True
            view = memoryview(payload)
            while view:
                written = os.write(descriptor, view)
                if written <= 0:
                    break
                view = view[written:]
            os.close(descriptor)
            descriptor = None
            os.replace(staged_path, final_path)
            staged = False
        except (OSError, TypeError, ValueError):
            # Evidence is subordinate to the failure it describes.
            pass
        finally:
            if descriptor is not None:
                try:
                    os.close(descriptor)
                except OSError:
                    pass
            if staged:
                try:
                    os.unlink(staged_path)
                except OSError:
                    pass

    def _capture_exit_evidence(self) -> dict[str, Any]:
        """Sample and persist everything known about how this sidecar ended."""
        self._sample_logs(force=True)
        forensics = self.private_exit_forensics()
        self._persist_exit_forensics(forensics)
        return forensics

    def _exit_status_snapshot(self) -> dict[str, Any]:
        """Poll the child once and reconcile it with the recorded exit code.

        The monitor thread may not have reaped an exit yet when a command
        publishes a failure, and equally a command may publish a failure for a
        client that is still running.  Both facts have to be sampled here, at
        the same instant, or the resulting record cannot distinguish them.
        """
        process = self._process
        returncode: int | None = None
        if process is not None:
            try:
                returncode = process.poll()
            except Exception:
                returncode = None
        with self._lock:
            if returncode is None:
                returncode = self._exit_code
            elif self._exit_code is None:
                self._exit_code = returncode
                self._exit_observed_at = time.time()
            snapshot = {
                "exit_code": returncode,
                "process_alive": process is not None and returncode is None,
                "process_started": process is not None,
                "exit_observed_at": self._exit_observed_at,
                "exit_observed_after_terminal": (
                    self._exit_observed_after_terminal
                ),
            }
        snapshot.update(self._exit_signal_fields(returncode))
        return snapshot

    @staticmethod
    def _exit_signal_fields(returncode: int | None) -> dict[str, Any]:
        """Split a wait status into a signal number and its name, if any."""
        if not isinstance(returncode, int) or returncode >= 0:
            return {"exit_signal": None, "exit_signal_name": None}
        number = -returncode
        try:
            name = signal.Signals(number).name
        except ValueError:
            name = None
        return {"exit_signal": number, "exit_signal_name": name}

    def private_exit_forensics(
        self, *, tail_lines: int = LOG_TAIL_LINES,
    ) -> dict[str, Any]:
        """Owner-private evidence about how this sidecar stopped working.

        A client can stop serving without dying, so this deliberately reports
        liveness separately from an exit status: ``exit_code`` is null and
        ``process_alive`` true for a client that simply stopped answering,
        which is the difference between a crash and a hang.  The process is
        polled here rather than trusting the monitor thread, which may not
        have reaped it yet when a command deadline publishes the failure.
        """
        status = self._exit_status_snapshot()
        with self._lock:
            state = self._state
            client_state = self._client_state
            error_code = self._error_code
            last_seen_at = self._last_seen_at
            started_at = self._started_at
            stopped_at = self._stopped_at
            unanswered = self._unanswered_replies
            outstanding = len(self._stale_replies)
            discarded = self._discarded_late_replies
            stop_requested = self._stop_requested
        stderr = self._log_evidence("stderr", self.stderr_path, tail_lines)
        stdout = self._log_evidence("stdout", self.stdout_path, tail_lines)
        return {
            **status,
            "sidecar_state": state,
            # Whether the HARNESS asked for this death.  Without it a SIGKILL
            # the supervisor issued during an orderly stop is indistinguishable
            # from an external SIGKILL: identical exit_signal_name, identical
            # process_alive.  Reading the first as a native fault is exactly
            # the mis-attribution this campaign is about.
            "stop_requested": stop_requested,
            "client_state": client_state,
            "error_code": error_code,
            "last_seen_at": last_seen_at,
            "started_at": started_at,
            "stopped_at": stopped_at,
            # A client that dies without writing anything looks exactly like a
            # client that was killed while healthy unless the silence is itself
            # recorded, with the verbosity it was launched at to say whether
            # silence was even possible.
            "native_log_level": self.native_log_level,
            "stderr_tail": stderr["tail"],
            "stdout_tail": stdout["tail"],
            "stderr_bytes": stderr["bytes"],
            "stdout_bytes": stdout["bytes"],
            "stderr_dropped_bytes": stderr["dropped_bytes"],
            "stdout_dropped_bytes": stdout["dropped_bytes"],
            "stderr_last_output_at": stderr["last_output_at"],
            "stdout_last_output_at": stdout["last_output_at"],
            "unanswered_replies": unanswered,
            "outstanding_replies": outstanding,
            "discarded_late_replies": discarded,
            "core_dump_limit_bytes": self.core_dump_limit_bytes,
        }

    def private_native_identity(self) -> tuple[int, int] | None:
        """Return the current native player incarnation to the owner only."""
        with self._lock:
            if (
                self._seat_state != "ready"
                or self._native_player_number is None
                or self._native_player_lifecycle is None
            ):
                return None
            return (
                self._native_player_number,
                self._native_player_lifecycle,
            )
