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
import select
import signal
import socket
import struct
import subprocess
import threading
import time
from collections import deque
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
    ):
        if not PLAYER_RE.fullmatch(player_name):
            raise SidecarError("invalid_player", "sidecar player name is unsafe")
        if not isinstance(generation, int) or generation < 1:
            raise SidecarError("invalid_generation")
        if not isinstance(port, int) or not 1 <= port <= 65535:
            raise SidecarError("invalid_port")
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
        stdout_fd = os.open(
            self.stdout_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600,
        )
        stderr_fd = os.open(
            self.stderr_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600,
        )
        return os.fdopen(stdout_fd, "wb", buffering=0), os.fdopen(
            stderr_fd, "wb", buffering=0,
        )

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
            stopping = self._state == "stopping"
            terminal = self._state in TERMINAL_STATES
        if not terminal:
            self._terminal(
                "stopped" if stopping else "failed",
                None if stopping else "process_exited",
            )

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
            argv = [
                str(self.binary), "--autoconnect", "--name", connection_name,
                "--server", self.host, "--port", str(self.port), "--",
                "--ipc-fd", str(child.fileno()), "--player", self.player_name,
            ]
            environment = self._safe_environment(
                self.home_directory, self.options_path,
                Path(__file__).resolve().parent.parent / "data",
            )
            with self._lock:
                if self._state != "starting" or self._stop_requested:
                    raise SidecarError(
                        "sidecar_unavailable", "sidecar stopped before launch",
                    )
                self._process = self.process_factory(
                    argv,
                    cwd=self.run_directory,
                    env=environment,
                    stdin=subprocess.DEVNULL,
                    stdout=stdout_stream,
                    stderr=stderr_stream,
                    pass_fds=(child.fileno(),),
                    close_fds=True,
                    start_new_session=True,
                    shell=False,
                )
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
        return error.code not in (
            _NONTERMINAL_NATIVE_ERRORS
            | {
                "sidecar_unavailable", "invalid_request", "invalid_snapshot",
                "invalid_scope", "invalid_actor", "invalid_page",
                "invalid_action", "invalid_argument",
                "command_in_progress",
            }
        )

    def _record_native_revision(self, revision: int) -> None:
        with self._lock:
            # Pinned observations and action acknowledgements can legitimately
            # trail a newer asynchronous notification.  They may advance the
            # coalesced value, but never lower it.
            self._record_native_revision_locked(
                revision, reject_regression=False,
            )

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
        try:
            with self._command_lock:
                deadline = time.monotonic() + timeout_s
                return self._obs_open_locked(request, deadline)
        except SidecarError as exc:
            if self._command_error_is_terminal(exc):
                self._terminal("failed", exc.code)
            raise

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
        try:
            with self._command_lock:
                deadline = time.monotonic() + timeout_s
                return self._obs_page_locked(
                    request, snapshot, revision, total_count, offset, limit,
                    deadline,
                )
        except SidecarError as exc:
            if self._command_error_is_terminal(exc):
                self._terminal("failed", exc.code)
            raise

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
        deadline = time.monotonic() + timeout_s
        acquired = False
        try:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not self._command_lock.acquire(
                timeout=remaining,
            ):
                raise SidecarError("deadline_exceeded")
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
            if acquired and self._command_error_is_terminal(exc):
                if on_terminal_error is not None:
                    try:
                        on_terminal_error(exc)
                    except Exception:
                        pass
                self._terminal("failed", exc.code)
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
        deadline = time.monotonic() + timeout_s
        acquired = False
        try:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not self._command_lock.acquire(timeout=remaining):
                raise SidecarError("deadline_exceeded")
            acquired = True
            opened = self._scope_open_locked(
                request, expected_revision, actor, deadline,
            )
            return self._scope_page_locked(
                request, opened["view_id"], opened["revision"], actor,
                opened["total_count"], 0, limit, deadline,
            )
        except SidecarError as exc:
            if acquired and self._command_error_is_terminal(exc):
                self._terminal("failed", exc.code)
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
        deadline = time.monotonic() + timeout_s
        try:
            with self._command_lock:
                return self._scope_page_locked(
                    request, view, revision, actor, total_count, offset,
                    limit, deadline,
                )
        except SidecarError as exc:
            if self._command_error_is_terminal(exc):
                self._terminal("failed", exc.code)
            raise

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
        deadline = time.monotonic() + timeout_s
        acquired = False
        try:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not self._command_lock.acquire(timeout=remaining):
                raise SidecarError("deadline_exceeded")
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
            if acquired and self._command_error_is_terminal(exc):
                self._terminal("failed", exc.code)
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
        deadline = time.monotonic() + timeout_s
        with self._command_lock:
            opened = self._state_scope_open_locked(
                request, expected_revision, section, selector, deadline,
            )
            return self._state_scope_page_locked(
                request, opened["view_id"], opened["revision"], section,
                selector, opened["total_count"], 0, limit, deadline,
            )

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
        deadline = time.monotonic() + timeout_s
        with self._command_lock:
            return self._state_scope_page_locked(
                request, view, revision, section, selector, total_count,
                offset, limit, deadline,
            )

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
        deadline = time.monotonic() + timeout_s
        acquired = False
        try:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not self._command_lock.acquire(timeout=remaining):
                raise SidecarError("deadline_exceeded")
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
            if acquired and self._command_error_is_terminal(exc):
                self._terminal("failed", exc.code)
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
        deadline = time.monotonic() + timeout_s
        acquired = False
        try:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not self._command_lock.acquire(timeout=remaining):
                raise SidecarError("deadline_exceeded")
            acquired = True
            opened = self._relation_scope_open_locked(
                request, expected_revision, actor, counterpart, deadline,
            )
            return self._relation_scope_page_locked(
                request, opened["view_id"], opened["revision"], actor,
                counterpart, opened["total_count"], 0, limit, deadline,
            )
        except SidecarError as exc:
            if acquired and self._command_error_is_terminal(exc):
                self._terminal("failed", exc.code)
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
        deadline = time.monotonic() + timeout_s
        try:
            with self._command_lock:
                return self._relation_scope_page_locked(
                    request, view, revision, actor, counterpart, total_count,
                    offset, limit, deadline,
                )
        except SidecarError as exc:
            if self._command_error_is_terminal(exc):
                self._terminal("failed", exc.code)
            raise

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
        deadline = time.monotonic() + timeout_s
        acquired = False
        try:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not self._command_lock.acquire(timeout=remaining):
                raise SidecarError("deadline_exceeded")
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
            if acquired and self._command_error_is_terminal(exc):
                self._terminal("failed", exc.code)
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
        deadline = time.monotonic() + timeout_s
        acquired = False
        try:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not self._command_lock.acquire(
                timeout=remaining,
            ):
                raise SidecarError("deadline_exceeded")
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
            if acquired and self._command_error_is_terminal(exc):
                self._terminal("failed", exc.code)
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
        deadline = time.monotonic() + timeout_s
        acquired = False
        send_may_have_begun = False
        deferred_exit: tuple[
            Callable[[int, dict[str, Any]], None] | None,
            dict[str, Any] | None,
        ] = (None, None)
        try:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not self._command_lock.acquire(
                timeout=remaining,
            ):
                raise SidecarError("deadline_exceeded")
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
            if (
                self._command_error_is_terminal(exc)
                and not (
                    isinstance(exc, SidecarActionAmbiguous)
                    and exc.stream_synchronized
                )
                and not (
                    not send_may_have_begun
                    and exc.code == "deadline_exceeded"
                )
            ):
                # Publish terminal state while the action still owns the
                # command gate, then release it before invoking external
                # on_exit code.  No command can slip through in between.
                deferred_exit = self._terminal(
                    "failed", exc.code, defer_callback=True,
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
        try:
            with self._command_lock:
                deadline = time.monotonic() + timeout_s
                with self._lock:
                    if self._state != "ready":
                        raise SidecarError("sidecar_unavailable")
                self._send("STATUS", deadline)
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
            if exc.code not in {"sidecar_unavailable", "command_in_progress"}:
                self._terminal("failed", exc.code)
            raise

    def ping(self, token: str, timeout_s: float = 2.0) -> bool:
        if not PING_RE.fullmatch(token):
            raise SidecarError("invalid_ping")
        try:
            with self._command_lock:
                deadline = time.monotonic() + timeout_s
                with self._lock:
                    if self._state != "ready":
                        raise SidecarError("sidecar_unavailable")
                self._send(f"PING\t{token}", deadline)
                while True:
                    message = self._wait_message(deadline)
                    fatal = self._fatal_message(message)
                    if fatal is not None:
                        raise fatal
                    if message == f"PONG\t{token}":
                        return True
                    raise SidecarError("protocol_error")
        except SidecarError as exc:
            if exc.code not in {"sidecar_unavailable", "command_in_progress"}:
                self._terminal("failed", exc.code)
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
        with self._lock:
            return {
                "state": self._state,
                "generation": self.generation,
                "player_name": self.player_name,
                "started_at": self._started_at,
                "ready_at": self._ready_at,
                "last_seen_at": self._last_seen_at,
                "stopped_at": self._stopped_at,
                "exit_code": self._exit_code,
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
