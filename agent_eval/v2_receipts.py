"""Durable, fail-closed receipts for ``full-control-v2`` command batches.

The store deliberately persists only the public batch identity, its canonical
request hash, and a validated public receipt.  Native action handles and the
command body never cross this durability boundary.
"""

from __future__ import annotations

import copy
import errno
import hashlib
import json
import os
import re
import secrets
import stat
import threading
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .full_control_v2 import (
    FULL_CONTROL_SCHEMA_VERSION,
    FULL_CONTROL_V2,
    FullControlSchemaError,
    structured_error,
    validate_command_receipt,
    validate_state_revision,
    validated_batch_request_hash,
)


RECEIPT_DIRECTORY = "v2-receipts"
MAX_RECORD_BYTES = 64 * 1024
MAX_RECORD_FILES = 100_000

_FORMAT = "freeciv-full-control-v2-receipt"
_FORMAT_VERSION = 1
_PHASES = frozenset({"reserved", "accepted", "applied", "rejected", "ambiguous"})
_TRANSITIONS = {
    "reserved": frozenset({"accepted", "rejected", "ambiguous"}),
    "accepted": frozenset({"applied", "rejected", "ambiguous"}),
}
_OPAQUE_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
_HASH = re.compile(r"^[0-9a-f]{64}$")
_RECORD_NAME = re.compile(r"^[0-9a-f]{64}\.json$")
_TEMP_NAME = re.compile(r"^\.[0-9a-f]{64}\.[0-9a-f]{32}\.tmp$")
_RECORD_FIELDS = frozenset({
    "format",
    "version",
    "phase",
    "request_hash",
    "game_id",
    "agent_id",
    "batch_id",
    "state_revision",
    "receipt",
})


class V2ReceiptStoreError(RuntimeError):
    """A sanitized receipt-store error safe to map to a public error code."""

    code = "internal_error"
    _message = "The command receipt store is unavailable."

    def __init__(self) -> None:
        super().__init__(self._message)


class V2ReceiptConflict(V2ReceiptStoreError):
    """The same agent-scoped batch ID was reused for another request."""

    code = "conflict"
    _message = "The batch ID is already bound to a different request."


class V2ReceiptInvalidBatch(V2ReceiptStoreError):
    """The batch or requested receipt identity is malformed."""

    code = "invalid_batch"
    _message = "The command batch is invalid."


class V2ReceiptInvalidTransition(V2ReceiptStoreError):
    """A receipt transition contradicts its durable state."""

    code = "internal_error"
    _message = "The command receipt transition is invalid."


class V2ReceiptCorrupt(V2ReceiptStoreError):
    """A durable receipt cannot be trusted."""

    code = "internal_error"
    _message = "A durable command receipt is invalid."


@dataclass(frozen=True, slots=True)
class ReceiptReservation:
    """Private handle binding later receipt transitions to one reservation."""

    game_id: str
    agent_id: str
    batch_id: str
    created: bool
    phase: str
    receipt: dict[str, Any] | None
    _request_hash: str = field(repr=False)


def _validate_opaque_id(value: Any) -> str:
    if not isinstance(value, str) or _OPAQUE_ID.fullmatch(value) is None:
        raise V2ReceiptInvalidBatch()
    return value


def _record_filename(agent_id: str, batch_id: str) -> str:
    identity = agent_id.encode("utf-8") + b"\0" + batch_id.encode("utf-8")
    return hashlib.sha256(identity).hexdigest() + ".json"


def _canonical_json(value: Any) -> bytes:
    try:
        encoded = json.dumps(
            value,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
            allow_nan=False,
        ).encode("utf-8")
    except (TypeError, ValueError, UnicodeError):
        raise V2ReceiptCorrupt() from None
    if len(encoded) > MAX_RECORD_BYTES:
        raise V2ReceiptCorrupt()
    return encoded


def _strict_json(data: bytes) -> Any:
    if not data or len(data) > MAX_RECORD_BYTES:
        raise V2ReceiptCorrupt()

    def pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
        value: dict[str, Any] = {}
        for key, item in items:
            if key in value:
                raise ValueError("duplicate key")
            value[key] = item
        return value

    try:
        value = json.loads(
            data.decode("utf-8"),
            object_pairs_hook=pairs,
            parse_constant=lambda _value: (_ for _ in ()).throw(
                ValueError("non-finite number")
            ),
        )
    except (UnicodeError, ValueError, TypeError, json.JSONDecodeError):
        raise V2ReceiptCorrupt() from None
    if _canonical_json(value) != data:
        raise V2ReceiptCorrupt()
    return value


def _public_copy(receipt: dict[str, Any], *, idempotent: bool) -> dict[str, Any]:
    result = copy.deepcopy(receipt)
    result["idempotent"] = idempotent
    return result


def _safe_public_receipt(value: Any) -> dict[str, Any]:
    """Validate a receipt and replace caller-controlled error prose/details.

    Receipt errors cross both a durability and a public boundary.  The schema
    intentionally accepts general structured details, but this store must not
    persist native handles, credentials, command arguments, paths, or exception
    text embedded in otherwise schema-valid error data.
    """
    clean = validate_command_receipt(value)
    error = clean["error"]
    if error is None:
        return clean
    code = error["error"]["code"]
    message = (
        "The action outcome is unknown and the command will not be replayed."
        if clean["receipt_state"] == "ambiguous"
        else "The command was rejected."
    )
    return validate_command_receipt({
        **clean,
        "error": structured_error(
            code,
            message,
            retryable=error["error"]["retryable"],
            details={},
            state_revision=clean["state_revision"],
        ),
    })


class V2ReceiptStore:
    """One durable receipt namespace inside a single game's episode root.

    Construction performs crash recovery: any prior ``reserved`` or
    ``accepted`` record becomes a durable, terminal ``ambiguous`` receipt.
    Such an action must never be replayed after ownership of the native outcome
    was lost.
    """

    def __init__(self, episode_root: str | os.PathLike[str], *, game_id: str):
        self.game_id = _validate_opaque_id(game_id)
        self._lock = threading.RLock()
        self._closed = False
        self._dir_fd = -1
        self._recovered_receipts: tuple[dict[str, Any], ...] = ()

        root_flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0)
        root_flags |= getattr(os, "O_DIRECTORY", 0)
        root_flags |= getattr(os, "O_NOFOLLOW", 0)
        try:
            root_fd = os.open(Path(episode_root), root_flags)
        except (OSError, TypeError, ValueError):
            raise V2ReceiptStoreError() from None
        try:
            try:
                os.mkdir(RECEIPT_DIRECTORY, 0o700, dir_fd=root_fd)
            except FileExistsError:
                pass
            directory_flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0)
            directory_flags |= getattr(os, "O_DIRECTORY", 0)
            directory_flags |= getattr(os, "O_NOFOLLOW", 0)
            self._dir_fd = os.open(
                RECEIPT_DIRECTORY,
                directory_flags,
                dir_fd=root_fd,
            )
            os.fchmod(self._dir_fd, 0o700)
            os.fsync(root_fd)
            os.fsync(self._dir_fd)
        except OSError:
            if self._dir_fd >= 0:
                os.close(self._dir_fd)
                self._dir_fd = -1
            raise V2ReceiptStoreError() from None
        finally:
            os.close(root_fd)

        try:
            with self._lock:
                self._recovered_receipts = self._recover_locked()
        except Exception:
            self.close()
            raise

    def __enter__(self) -> V2ReceiptStore:
        return self

    def __exit__(self, _type: Any, _value: Any, _traceback: Any) -> None:
        self.close()

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass

    @property
    def recovered_receipts(self) -> tuple[dict[str, Any], ...]:
        with self._lock:
            self._require_open()
            return tuple(copy.deepcopy(value) for value in self._recovered_receipts)

    def close(self) -> None:
        lock = getattr(self, "_lock", None)
        if lock is None:
            return
        with lock:
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

    def reserve(self, batch: Any) -> ReceiptReservation:
        """Durably reserve a validated public batch before native dispatch."""
        clean, request_hash = self._validated_batch(batch)
        record = {
            "format": _FORMAT,
            "version": _FORMAT_VERSION,
            "phase": "reserved",
            "request_hash": request_hash,
            "game_id": clean["game_id"],
            "agent_id": clean["agent_id"],
            "batch_id": clean["batch_id"],
            "state_revision": clean["state_revision"],
            "receipt": None,
        }
        name = _record_filename(clean["agent_id"], clean["batch_id"])
        encoded = _canonical_json(record)
        with self._lock:
            self._require_open()
            try:
                self._create_record_locked(name, encoded)
            except FileExistsError:
                existing = self._read_record_locked(name)
                return self._existing_reservation(
                    existing,
                    agent_id=clean["agent_id"],
                    batch_id=clean["batch_id"],
                    request_hash=request_hash,
                )
            return ReceiptReservation(
                game_id=self.game_id,
                agent_id=clean["agent_id"],
                batch_id=clean["batch_id"],
                created=True,
                phase="reserved",
                receipt=None,
                _request_hash=request_hash,
            )

    def probe(self, batch: Any) -> ReceiptReservation | None:
        """Inspect a batch identity without creating or changing a record.

        This is the duplicate fast path used before requiring a live sidecar.
        A ``None`` result is linearized at an explicit absent-file check; if a
        record disappears after that check, the race fails closed as corrupt.
        """
        clean, request_hash = self._validated_batch(batch)
        name = _record_filename(clean["agent_id"], clean["batch_id"])
        with self._lock:
            self._require_open()
            if not self._record_exists_locked(name):
                return None
            existing = self._read_record_locked(name)
            return self._existing_reservation(
                existing,
                agent_id=clean["agent_id"],
                batch_id=clean["batch_id"],
                request_hash=request_hash,
            )

    def transition(
        self,
        reservation: ReceiptReservation,
        receipt: Any,
    ) -> dict[str, Any]:
        """Atomically persist one allowed receipt-state transition."""
        if not isinstance(reservation, ReceiptReservation):
            raise V2ReceiptInvalidTransition()
        try:
            clean = _safe_public_receipt(receipt)
        except FullControlSchemaError:
            raise V2ReceiptInvalidTransition() from None
        if clean["idempotent"]:
            raise V2ReceiptInvalidTransition()
        if (
            clean["game_id"] != reservation.game_id
            or clean["agent_id"] != reservation.agent_id
            or clean["batch_id"] != reservation.batch_id
            or reservation.game_id != self.game_id
        ):
            raise V2ReceiptInvalidTransition()
        name = _record_filename(reservation.agent_id, reservation.batch_id)
        target = clean["receipt_state"]
        with self._lock:
            self._require_open()
            record = self._read_record_locked(name)
            self._check_reservation(record, reservation)
            current = record["phase"]
            if current == target:
                if record["receipt"] != clean:
                    raise V2ReceiptInvalidTransition()
                return _public_copy(clean, idempotent=True)
            if not reservation.created:
                raise V2ReceiptInvalidTransition()
            if target not in _TRANSITIONS.get(current, frozenset()):
                raise V2ReceiptInvalidTransition()
            updated = dict(record)
            updated["phase"] = target
            updated["receipt"] = clean
            self._replace_record_locked(name, _canonical_json(updated))
            return copy.deepcopy(clean)

    def lookup(self, agent_id: Any, batch_id: Any) -> dict[str, Any] | None:
        """Return a copy of a durable public receipt, if one exists yet."""
        agent = _validate_opaque_id(agent_id)
        batch = _validate_opaque_id(batch_id)
        name = _record_filename(agent, batch)
        with self._lock:
            self._require_open()
            if not self._record_exists_locked(name):
                return None
            record = self._read_record_locked(name)
            self._check_identity(record, agent, batch)
            if record["receipt"] is None:
                return None
            return copy.deepcopy(record["receipt"])

    def _require_open(self) -> None:
        if self._closed or self._dir_fd < 0:
            raise V2ReceiptStoreError()

    def _validated_batch(self, batch: Any) -> tuple[dict[str, Any], str]:
        try:
            clean, request_hash = validated_batch_request_hash(batch)
        except (FullControlSchemaError, UnicodeError, ValueError):
            raise V2ReceiptInvalidBatch() from None
        if clean["game_id"] != self.game_id:
            raise V2ReceiptInvalidBatch()
        return clean, request_hash

    def _existing_reservation(
        self,
        record: dict[str, Any],
        *,
        agent_id: str,
        batch_id: str,
        request_hash: str,
    ) -> ReceiptReservation:
        self._check_identity(record, agent_id, batch_id)
        if record["request_hash"] != request_hash:
            raise V2ReceiptConflict()
        existing_receipt = record["receipt"]
        return ReceiptReservation(
            game_id=self.game_id,
            agent_id=agent_id,
            batch_id=batch_id,
            created=False,
            phase=record["phase"],
            receipt=(
                None if existing_receipt is None
                else _public_copy(existing_receipt, idempotent=True)
            ),
            _request_hash=request_hash,
        )

    def _create_record_locked(self, name: str, encoded: bytes) -> None:
        temporary = f".{name[:-5]}.{secrets.token_hex(16)}.tmp"
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_CLOEXEC", 0)
        flags |= getattr(os, "O_NOFOLLOW", 0)
        fd = -1
        linked = False
        temporary_created = False
        try:
            fd = os.open(temporary, flags, 0o600, dir_fd=self._dir_fd)
            temporary_created = True
            os.fchmod(fd, 0o600)
            self._write_all(fd, encoded)
            os.fsync(fd)
            os.close(fd)
            fd = -1
            os.link(
                temporary,
                name,
                src_dir_fd=self._dir_fd,
                dst_dir_fd=self._dir_fd,
                follow_symlinks=False,
            )
            linked = True
            os.unlink(temporary, dir_fd=self._dir_fd)
            temporary_created = False
            os.fsync(self._dir_fd)
        except FileExistsError:
            raise
        except OSError:
            raise V2ReceiptStoreError() from None
        finally:
            if fd >= 0:
                try:
                    os.close(fd)
                except OSError:
                    pass
            if temporary_created:
                try:
                    os.unlink(temporary, dir_fd=self._dir_fd)
                except FileNotFoundError:
                    pass
                except OSError:
                    if not linked:
                        raise V2ReceiptStoreError() from None
        if not linked:
            raise V2ReceiptStoreError()

    def _record_exists_locked(self, name: str) -> bool:
        try:
            os.stat(name, dir_fd=self._dir_fd, follow_symlinks=False)
            return True
        except FileNotFoundError:
            return False
        except OSError:
            raise V2ReceiptCorrupt() from None

    def _replace_record_locked(self, name: str, encoded: bytes) -> None:
        temporary = f".{name[:-5]}.{secrets.token_hex(16)}.tmp"
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_CLOEXEC", 0)
        flags |= getattr(os, "O_NOFOLLOW", 0)
        fd = -1
        temporary_created = False
        try:
            fd = os.open(temporary, flags, 0o600, dir_fd=self._dir_fd)
            temporary_created = True
            os.fchmod(fd, 0o600)
            self._write_all(fd, encoded)
            os.fsync(fd)
            os.close(fd)
            fd = -1
            os.replace(
                temporary,
                name,
                src_dir_fd=self._dir_fd,
                dst_dir_fd=self._dir_fd,
            )
            temporary_created = False
            os.fsync(self._dir_fd)
        except OSError:
            raise V2ReceiptStoreError() from None
        finally:
            if fd >= 0:
                try:
                    os.close(fd)
                except OSError:
                    pass
            if temporary_created:
                try:
                    os.unlink(temporary, dir_fd=self._dir_fd)
                except OSError:
                    pass

    @staticmethod
    def _write_all(fd: int, data: bytes) -> None:
        offset = 0
        while offset < len(data):
            written = os.write(fd, data[offset:])
            if written <= 0:
                raise OSError(errno.EIO, "short write")
            offset += written

    def _read_record_locked(self, name: str) -> dict[str, Any]:
        flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0)
        flags |= getattr(os, "O_NOFOLLOW", 0)
        try:
            fd = os.open(name, flags, dir_fd=self._dir_fd)
        except FileNotFoundError:
            raise V2ReceiptCorrupt() from None
        except OSError:
            raise V2ReceiptCorrupt() from None
        try:
            metadata = os.fstat(fd)
            if not stat.S_ISREG(metadata.st_mode):
                raise V2ReceiptCorrupt()
            if stat.S_IMODE(metadata.st_mode) != 0o600:
                raise V2ReceiptCorrupt()
            if metadata.st_size <= 0 or metadata.st_size > MAX_RECORD_BYTES:
                raise V2ReceiptCorrupt()
            chunks: list[bytes] = []
            remaining = metadata.st_size
            while remaining:
                chunk = os.read(fd, min(remaining, 16 * 1024))
                if not chunk:
                    raise V2ReceiptCorrupt()
                chunks.append(chunk)
                remaining -= len(chunk)
            if os.read(fd, 1):
                raise V2ReceiptCorrupt()
        except OSError:
            raise V2ReceiptCorrupt() from None
        finally:
            try:
                os.close(fd)
            except OSError:
                pass
        return self._validate_record(_strict_json(b"".join(chunks)), name)

    def _validate_record(self, value: Any, name: str) -> dict[str, Any]:
        try:
            if not isinstance(value, dict) or set(value) != _RECORD_FIELDS:
                raise V2ReceiptCorrupt()
            if (
                value["format"] != _FORMAT
                or type(value["version"]) is not int
                or value["version"] != _FORMAT_VERSION
            ):
                raise V2ReceiptCorrupt()
            phase = value["phase"]
            if phase not in _PHASES:
                raise V2ReceiptCorrupt()
            request_hash = value["request_hash"]
            if not isinstance(request_hash, str) or _HASH.fullmatch(request_hash) is None:
                raise V2ReceiptCorrupt()
            game_id = _validate_opaque_id(value["game_id"])
            agent_id = _validate_opaque_id(value["agent_id"])
            batch_id = _validate_opaque_id(value["batch_id"])
            if game_id != self.game_id:
                raise V2ReceiptCorrupt()
            if name != _record_filename(agent_id, batch_id):
                raise V2ReceiptCorrupt()
            state_revision = validate_state_revision(value["state_revision"])
            receipt = value["receipt"]
            if phase == "reserved":
                if receipt is not None:
                    raise V2ReceiptCorrupt()
            else:
                receipt = _safe_public_receipt(receipt)
                if receipt["idempotent"] or receipt["receipt_state"] != phase:
                    raise V2ReceiptCorrupt()
                if (
                    receipt["game_id"] != game_id
                    or receipt["agent_id"] != agent_id
                    or receipt["batch_id"] != batch_id
                ):
                    raise V2ReceiptCorrupt()
            clean = {
                "format": _FORMAT,
                "version": _FORMAT_VERSION,
                "phase": phase,
                "request_hash": request_hash,
                "game_id": game_id,
                "agent_id": agent_id,
                "batch_id": batch_id,
                "state_revision": state_revision,
                "receipt": receipt,
            }
            if value != clean:
                raise V2ReceiptCorrupt()
            return clean
        except (FullControlSchemaError, V2ReceiptInvalidBatch, TypeError, KeyError):
            raise V2ReceiptCorrupt() from None

    @staticmethod
    def _check_identity(record: dict[str, Any], agent_id: str, batch_id: str) -> None:
        if record["agent_id"] != agent_id or record["batch_id"] != batch_id:
            raise V2ReceiptCorrupt()

    def _check_reservation(
        self,
        record: dict[str, Any],
        reservation: ReceiptReservation,
    ) -> None:
        if (
            record["game_id"] != reservation.game_id
            or record["agent_id"] != reservation.agent_id
            or record["batch_id"] != reservation.batch_id
            or record["request_hash"] != reservation._request_hash
        ):
            raise V2ReceiptInvalidTransition()

    def _recover_locked(self) -> tuple[dict[str, Any], ...]:
        try:
            names = os.listdir(self._dir_fd)
        except OSError:
            raise V2ReceiptStoreError() from None
        if len(names) > MAX_RECORD_FILES:
            raise V2ReceiptCorrupt()
        removed_temporary = False
        records: list[tuple[str, dict[str, Any]]] = []
        for name in sorted(names):
            if _TEMP_NAME.fullmatch(name):
                try:
                    os.unlink(name, dir_fd=self._dir_fd)
                    removed_temporary = True
                except OSError:
                    raise V2ReceiptStoreError() from None
                continue
            if _RECORD_NAME.fullmatch(name) is None:
                raise V2ReceiptCorrupt()
            records.append((name, self._read_record_locked(name)))
        if removed_temporary:
            try:
                os.fsync(self._dir_fd)
            except OSError:
                raise V2ReceiptStoreError() from None

        recovered: list[dict[str, Any]] = []
        for name, record in records:
            if record["phase"] not in {"reserved", "accepted"}:
                continue
            revision = (
                record["state_revision"]
                if record["receipt"] is None
                else record["receipt"]["state_revision"]
            )
            receipt = _safe_public_receipt({
                "schema_version": FULL_CONTROL_SCHEMA_VERSION,
                "control_protocol": FULL_CONTROL_V2,
                "game_id": record["game_id"],
                "agent_id": record["agent_id"],
                "batch_id": record["batch_id"],
                "receipt_state": "ambiguous",
                "idempotent": False,
                "state_revision": revision,
                "error": structured_error(
                    "action_outcome_ambiguous",
                    "The action outcome is unknown after recovery and will not be replayed.",
                    retryable=False,
                    state_revision=revision,
                ),
                "observation": None,
            })
            updated = dict(record)
            updated["phase"] = "ambiguous"
            updated["receipt"] = receipt
            self._replace_record_locked(name, _canonical_json(updated))
            recovered.append(copy.deepcopy(receipt))
        return tuple(recovered)


__all__ = [
    "MAX_RECORD_BYTES",
    "RECEIPT_DIRECTORY",
    "ReceiptReservation",
    "V2ReceiptConflict",
    "V2ReceiptCorrupt",
    "V2ReceiptInvalidBatch",
    "V2ReceiptInvalidTransition",
    "V2ReceiptStore",
    "V2ReceiptStoreError",
]
