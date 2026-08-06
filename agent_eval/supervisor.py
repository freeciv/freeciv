"""Session-first Freeciv game supervisor.

The supervisor is deliberately policy-free.  It owns Freeciv processes and
turn barriers, while arbitrary agent harnesses connect through the public HTTP
API.  Provider adapters and deterministic policies live outside this module.
"""

from __future__ import annotations

import hashlib
import hmac
import json
import math
import os
import re
import secrets
import signal
import shutil
import socket
import subprocess
import threading
import time
from collections import deque
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Callable, Iterable, Mapping
from urllib.parse import parse_qs, unquote, urlparse

from .actions import ActionError, TRAIT_MAX, TRAIT_MIN, TRAITS, validate_action
from .bridge_status import create_bridge_journal, validate_bridge_journal
from .config import controller_fingerprint
from .full_control_v2 import (
    FULL_CONTROL_V2,
    STRATEGIC_V1,
    REJECTION_REASONS,
    FullControlSchemaError,
    rejection,
    rejection_message,
    structured_error,
    validate_initial_command_batch,
    validate_control_protocol,
    validate_supported_control_protocols,
)
from .headless_sidecar import (
    HeadlessSidecar,
    SidecarActionAmbiguous,
    SidecarActionNotAccepted,
    SidecarError,
)
from .scoring import ScorelogError, parse_scorelog, summarize_episode
from .v2_control import MAX_PAGE_ITEMS, V2ControlError, V2SeatControl
from .v2_ambiguity_trace import V2AmbiguityTrace
from .v2_receipts import (
    ReceiptReservation,
    V2ReceiptConflict,
    V2ReceiptCorrupt,
    V2ReceiptInvalidBatch,
    V2ReceiptInvalidTransition,
    V2ReceiptStore,
    V2ReceiptStoreError,
)
from .v2_phase_events import (
    V2PhaseEventJournal,
    V2PhaseEventJournalError,
)
from .v2_recovery import (
    MAX_RECOVERY_ATTEMPTS_PER_GAME,
    RecoveryBudget,
    V2RecoveryError,
    V2RecoveryJournal,
    WedgeDetector,
    recovery_kind_for_attempt,
    select_rollback_save,
)
from .v2_replay import V2ReplayProducer


REPO_ROOT = Path(__file__).resolve().parent.parent
VIEWER_DIST_ROOT = REPO_ROOT / "agent_eval" / "viewer" / "dist"
V2_OPENAPI_PATH = REPO_ROOT / "play" / "docs" / "full-control-v2.openapi.json"
VIEWER_ASSET_NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,199}$")
VIEWER_ASSET_CONTENT_TYPES = {
    ".css": "text/css; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".png": "image/png",
    ".svg": "image/svg+xml; charset=utf-8",
    ".woff": "font/woff",
    ".woff2": "font/woff2",
}
VIEWER_CONTENT_SECURITY_POLICY = (
    "default-src 'self'; base-uri 'none'; connect-src 'self'; "
    "font-src 'self'; form-action 'none'; frame-ancestors 'none'; "
    "img-src 'self' data:; object-src 'none'; script-src 'self'; "
    "style-src 'self'"
)
GAME_ID_RE = re.compile(r"^[A-Za-z0-9_-]{20,80}$")
CONTROLLER_LABEL_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9 ._:/+@-]{0,79}$")
NATIVE_VIEWER_SIGNAL_GUARD_S = 2.0
NATIVE_VIEWER_CONNECT_TIMEOUT_S = 20
NATIVE_VIEWER_EVENT_LIMIT = 1024
BARRIER_REMINDER_INTERVAL_S = 30.0
NATIVE_VIEWER_PROTOCOL = {
    "version": 1,
    "lease_status": True,
    "bridge_response_ack": True,
    "release_during_activation": True,
}
TERMINAL_STATES = {"completed", "invalid", "failed", "cancelled"}
# A non-terminal run this quiet is a dead supervisor's leftover, not a live
# game: live games write replay/phase telemetry many times a minute. The
# margin also keeps a second supervisor sharing a runs root from finalizing
# a neighbour's game that is merely between writes.
ORPHAN_RUN_QUIET_S = 300.0
TIMING_MODE_TIMEOUTS: dict[str, float | None] = {
    "default": 180.0,
    "blitz": 60.0,
    "infinite": None,
}
# Full-control-v2 phases cover a whole human-style turn (reads, catalog
# enumeration, and every order), so the default budget is ten minutes and
# the 60-second blitz preset is not offered at all.
V2_TIMING_MODE_TIMEOUTS: dict[str, float | None] = {
    "default": 600.0,
    "infinite": None,
}
AI_DIFFICULTY_LEVELS = ("novice", "easy", "normal", "hard", "cheating")
V2_WAIT_REASONS = frozenset({
    "phase_active", "game_terminal", "revision_changed", "timeout",
    "boundary_recovered",
})
# How long a caller's own wait may block while its seat is being recovered
# before the wait answers with a plain timeout instead.
V2_RECOVERY_WAIT_GRACE_S = 120.0
_NATIVE_CODE_TOKEN = re.compile(r"^[a-z][a-z0-9_]{0,63}$")
# Sidecar native error tokens mapped onto the closed public refusal
# vocabulary.  Anything absent falls back to ``native_refused``, which still
# names the layer, so no rejection can lose its attribution.
_V2_NATIVE_REJECTION_REASONS = {
    "native_busy": "native_busy",
    "native_bad_argument": "native_bad_argument",
    "invalid_argument": "native_bad_argument",
    "native_bad_request": "native_bad_request",
    "invalid_request": "native_bad_request",
    "invalid_action": "native_bad_request",
    "native_not_ready": "native_not_ready",
    "native_not_sent": "native_not_ready",
    "command_in_progress": "native_busy",
    "stale_slot": "native_entity_expired",
    "stale_entity": "native_entity_expired",
    "stale_revision": "revision_stale",
    "sidecar_unavailable": "seat_unavailable",
    "deadline_exceeded": "seat_unavailable",
    "native_error": "native_refused",
}
CONSOLE_TIMEOUT_RE = re.compile(
    r"['\"]timeout['\"].*?set to\s+(-?\d+)", re.IGNORECASE,
)
SIGNAL_TIMEOUT_RE = re.compile(
    r"Setting timeout to\s+(-?\d+)", re.IGNORECASE,
)
NATIVE_TURN_RESPONSE_DONE_RE = re.compile(
    r"\bAGENT_EVAL_NATIVE_TURN_RESPONSE_DONE turn=(\d+)\b",
)
FRAME_TURN_RE = re.compile(r"(?:^|[^A-Za-z0-9])turn-(\d+)(?:-|[^0-9]|$)")
PPM_PLAYER_RE = re.compile(
    r'^#\s*playerno:(\d+):color:\(\s*(\d+),\s*(\d+),\s*(\d+)\)'
    r':name:"(.*)"\s*$'
)
# Kelly-derived high-contrast colors. Freeciv and every public legend use
# this single mapping, so the service never guesses colors from a map image.
PLAYER_COLOR_HEX = (
    "0067A5", "F38400", "008856", "BE0032",
    "875692", "F3C300", "E68FAC", "848482",
    "F99379", "604E97", "F6A600", "B3446C",
    "DCD300", "882D17", "A1CAF1", "C2B280",
)
SECRET_METADATA_PARTS = {
    "api_key", "apikey", "authorization", "bearer", "credential",
    "password", "secret", "session", "token",
}
SIDECAR_HEALTH_FIELDS = frozenset({
    "state", "generation", "player_name", "started_at", "ready_at",
    "last_seen_at", "stopped_at", "exit_code", "error_code",
    "client_state", "server_connected", "seat_state",
    # The three facts that separate "the client crashed" from "the client is
    # alive and merely slow".  Without them a live-but-unresponsive seat reads
    # as `state=failed, exit_code=null`, byte-identical to a silent death --
    # which is exactly the ambiguity that cost a day of the turn-66 hunt.
    "exit_signal", "exit_signal_name", "process_alive",
})

class _RecoveryAbandoned:
    """A recovery attempt that never ran because the game is going away.

    Distinct from ``False``: an attempt that *ran and failed* is evidence the
    seat is unrecoverable and must escalate, while one that was refused by a
    cancel, a completed-game teardown or an already-exited server proves
    nothing at all.  Conflating them is what let an owner's cancel land in the
    manifest as ``v2_boundary_wedged``.
    """

    __slots__ = ()

    def __bool__(self) -> bool:
        return False

    def __repr__(self) -> str:
        return "RECOVERY_ABANDONED"


RECOVERY_ABANDONED = _RecoveryAbandoned()
V2_SIDECAR_EXIT_DIAGNOSTIC_FILENAME = "sidecar-exit-diagnostic.json"
V2_SIDECAR_EXIT_HISTORY_FILENAME = "sidecar-exit-history.json"
# One more than a game's whole recovery budget, so every death a recovered
# game can survive keeps its own logs and the first one is never the one lost.
V2_SIDECAR_EXIT_HISTORY_LIMIT = MAX_RECOVERY_ATTEMPTS_PER_GAME + 1
V2_SIDECAR_STARTUP_GRACE_S = 20.0
# The liveness poll has no caller waiting on its answer, so it must carry the
# LOOSEST budget in the system, not the tightest.  It used to be 1.0s -- half
# the 2.0s of a request-path status and a fifth of the 5.0s a recovery gets --
# and a single missed sample destroyed a healthy seat-owning client.
V2_LIVENESS_POLL_TIMEOUT_S = 6.0
# One slow sample is "slow", never "gone".  A seat-loss verdict from a timeout
# alone needs a run of misses that no latency tail explains, spanning real
# time, with no evidence the process is still alive.
V2_LIVENESS_MISS_THRESHOLD = 3
V2_LIVENESS_MISS_WINDOW_S = 10.0
V2_SIDECAR_COMPLETION_GRACE_S = 2.0
V2_OBSERVATION_TIMEOUT_S = 5.0
V2_POST_RESULT_OBSERVATION_RETRY_S = 2.0
V2_POST_RESULT_OBSERVATION_RETRY_INTERVAL_S = 0.02
V2_SCOPE_MATERIALIZATION_TIMEOUT_S = 30.0
V2_ACTION_TIMEOUT_S = 20.0
V2_EXECUTION_LOCK_TIMEOUT_S = 30.0
# How long a read may wait for the seat work already in flight before the agent
# is told the boundary is busy.  Reads used to be refused the instant the seat
# was occupied, which turned every overlap -- including this service's own
# background liveness probe -- into a 429 the agent had to retry.  The native
# client answers in well under a millisecond at the median and inside ~200 ms at
# a turn-change tick, so a bound comfortably above that converts nearly every
# collision into a few milliseconds of waiting.  It stays far below the
# mutation lock's budget because a queued read is only worth waiting for while
# its answer is still fresh.
V2_READ_LOCK_WAIT_S = 1.0
# A boundary command that the agent completed is itself proof the client is
# answering, so the liveness poller stands down for this long afterwards rather
# than contending with the agent for the one command stream.
V2_LIVENESS_AGENT_ACTIVITY_YIELD_S = 1.5
# ...but never for longer than this since the seat's last real probe.  Agent
# traffic is continuous during play and would otherwise suppress sampling
# indefinitely, and only a real probe can observe a seat that was lost, a
# client that went OVER, or a boundary that wedged.
V2_LIVENESS_MAX_PROBE_GAP_S = 2.0
V2_PHASE_RECONCILE_STALL_S = 30.0
V2_PHASE_SYNCHRONIZE_STALL_S = 30.0
# Independent control-plane guard for a coherent native phase that makes no
# forward progress. This remains active in infinite model-timing mode; it never
# chooses a model action and is deliberately much more generous than polling or
# request timeouts.
V2_PHASE_PROGRESS_STALL_S = 300.0
# Consecutive unexpected faults in the seat status poll before the game is
# failed rather than left unwatched.  One is a transient the next sample can
# recover from; a run of three, a quarter second apart, is the thread itself
# failing, and a game nobody polls never advances another phase.
V2_STATUS_POLL_FAULT_LIMIT = 3
# Cadence of the background replay keep-warm thread. Turns take tens of
# seconds, a no-op refresh is a single scandir, and the thread converges on
# any backlog in bounded batches before sleeping.
V2_REPLAY_KEEPWARM_INTERVAL_S = 5.0
V2_STATE_SECTIONS = frozenset({
    "overview", "votes", "research", "diplomacy", "diplomacy_clauses",
    "known_tiles", "map_tiles", "cities", "units", "city_sites",
    "governments", "tombstones", "city_detail", "city_citizens",
    "city_worker_tasks",
    "city_build_choices", "city_worklist", "city_improvements",
    "city_trade_routes", "city_governor", "tile_window", "multipliers",
    "spaceship",
    "infrastructure", "pregame_nations", "pregame_styles",
    "pregame_teams", "chat", "chat_recipients", "unit_route",
})
V2_CITY_STATE_SECTIONS = frozenset({
    "city_detail", "city_citizens", "city_build_choices", "city_worklist",
    "city_improvements", "city_trade_routes", "city_governor",
    "city_worker_tasks",
})
V2_CURSOR_RE = re.compile(r"cursor_[A-Za-z0-9_-]{32}")
V2_ACTOR_ID_RE = re.compile(r"(?:player|city|unit)_[0-9a-f]{32}")
V2_TILE_ID_RE = re.compile(r"tile_[0-9a-f]{32}")
V2_RELATION_ID_RE = re.compile(r"relation_[0-9a-f]{32}")
V2_STATE_TOKEN_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._:-]{0,127}")


class SupervisorError(RuntimeError):
    """Raised for supervisor setup failures."""


class APIProblem(RuntimeError):
    """An HTTP-safe API error."""

    def __init__(
        self, status: int, message: str, payload: dict[str, Any] | None = None,
    ):
        super().__init__(message)
        self.status = status
        self.payload = payload


def _viewer_html(name: str) -> str:
    """Read one of the two committed viewer shells, never an arbitrary path."""
    if name not in {"arena.html", "index.html"}:
        raise APIProblem(HTTPStatus.NOT_FOUND, "viewer entrypoint not found")
    try:
        return (VIEWER_DIST_ROOT / name).read_text(encoding="utf-8")
    except OSError as exc:
        raise APIProblem(
            HTTPStatus.SERVICE_UNAVAILABLE,
            "the replay viewer build is unavailable",
        ) from exc


def _token() -> str:
    return secrets.token_urlsafe(32)


def _digest(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def _same_token(value: str | None, expected_digest: str) -> bool:
    return bool(value) and hmac.compare_digest(_digest(value), expected_digest)


def _canonical(value: Any) -> str:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False,
    )


def _normalize_ruleset_name(value: str) -> str:
    """Remove Freeciv gettext context while retaining the public name."""
    return re.sub(r"^\?[^:]+:", "", value).strip()


def _classic_tech_requirements() -> dict[str, tuple[str, ...]]:
    """Parse only names/prerequisites from the bundled Classic ruleset."""
    path = REPO_ROOT / "data" / "classic" / "techs.ruleset"
    sections: list[dict[str, str]] = []
    current: dict[str, str] | None = None
    section_re = re.compile(r"^\[advance_[^]]+\]$")
    value_re = re.compile(
        r'^(name|rule_name|req1|req2)\s*=\s*(?:_\()?"([^"]+)"\)?'
    )
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if section_re.fullmatch(line):
            if current is not None:
                sections.append(current)
            current = {}
            continue
        if current is None or not line or line.startswith(";"):
            continue
        match = value_re.match(line)
        if match:
            current[match.group(1)] = match.group(2)
    if current is not None:
        sections.append(current)
    result: dict[str, tuple[str, ...]] = {}
    for section in sections:
        name = section.get("name")
        if not name:
            continue
        rule_name = _normalize_ruleset_name(section.get("rule_name", name))
        result[rule_name] = tuple(
            _normalize_ruleset_name(requirement)
            for requirement in (section.get("req1"), section.get("req2"))
            if requirement and requirement not in {"None", "Never"}
        )
    return result


def _classic_technology_catalog(value: Any) -> dict[str, Any]:
    """Validate and close the Lua catalog over Classic prerequisites."""
    if not isinstance(value, dict) or not isinstance(
        value.get("technologies"), list,
    ):
        raise ValueError("technology catalog is unavailable")
    requirements = _classic_tech_requirements()
    by_name: dict[str, dict[str, Any]] = {}
    seen_ids: set[int] = set()
    for raw in value["technologies"]:
        if not isinstance(raw, dict):
            raise ValueError("technology catalog is malformed")
        tech_id = raw.get("id")
        cost_base = raw.get("cost_base")
        rule_name = raw.get("rule_name")
        display_name = raw.get("name")
        if (
            isinstance(tech_id, bool) or not isinstance(tech_id, (int, float))
            or not math.isfinite(tech_id) or int(tech_id) != tech_id
            or not 0 <= tech_id <= 511 or int(tech_id) in seen_ids
            or isinstance(cost_base, bool)
            or not isinstance(cost_base, (int, float))
            or not math.isfinite(cost_base) or cost_base < 0
            or not isinstance(rule_name, str)
            or not isinstance(display_name, str)
        ):
            raise ValueError("technology catalog is malformed")
        normalized_id = int(tech_id)
        normalized_rule_name = _normalize_ruleset_name(rule_name)
        if (
            normalized_rule_name in by_name
            or normalized_rule_name not in requirements
        ):
            raise ValueError("technology catalog does not match Classic")
        seen_ids.add(normalized_id)
        by_name[normalized_rule_name] = {
            "id": normalized_id,
            "rule_name": normalized_rule_name,
            "name": _normalize_ruleset_name(display_name),
            "cost_base": cost_base,
        }
    if len(by_name) != 87 or set(by_name) != set(requirements):
        raise ValueError("technology catalog does not match Classic")

    visiting: set[str] = set()
    depths: dict[str, int] = {}

    def depth(name: str) -> int:
        if name in depths:
            return depths[name]
        if name in visiting:
            raise ValueError("technology catalog contains a cycle")
        visiting.add(name)
        parents = requirements[name]
        if any(parent not in by_name for parent in parents):
            raise ValueError("technology catalog is not closed")
        value = 0 if not parents else max(depth(parent) for parent in parents) + 1
        visiting.remove(name)
        depths[name] = value
        return value

    technologies = []
    for rule_name, technology in by_name.items():
        technologies.append({
            **technology,
            "requires": [by_name[parent]["id"] for parent in requirements[rule_name]],
            "depth": depth(rule_name),
        })
    technologies.sort(key=lambda technology: technology["id"])
    return {"schema_version": 1, "technologies": technologies}


def _controller_identity_fingerprint(
    controller_label: str, metadata: Any,
) -> str:
    identity = {
        "controller_label": controller_label,
        "metadata": metadata,
    }
    return hashlib.sha256(_canonical(identity).encode("utf-8")).hexdigest()


def _validate_metadata(value: Any) -> Any:
    if value is None:
        return {}
    try:
        encoded = _canonical(value)
    except (TypeError, ValueError) as exc:
        raise APIProblem(
            HTTPStatus.BAD_REQUEST, "metadata must be JSON-serializable",
        ) from exc
    if len(encoded.encode("utf-8")) > 16_384:
        raise APIProblem(
            HTTPStatus.BAD_REQUEST, "metadata must be at most 16384 bytes",
        )

    def inspect(item: Any) -> None:
        if isinstance(item, dict):
            for key, nested in item.items():
                if not isinstance(key, str):
                    raise APIProblem(
                        HTTPStatus.BAD_REQUEST,
                        "metadata object keys must be strings",
                    )
                normalized = key.lower().replace("-", "_")
                if any(part in normalized for part in SECRET_METADATA_PARTS):
                    raise APIProblem(
                        HTTPStatus.BAD_REQUEST,
                        f"metadata must not contain secret field {key!r}",
                    )
                inspect(nested)
        elif isinstance(item, list):
            for nested in item:
                inspect(nested)

    inspect(value)
    return value


def _last_recorded_turn(run: Path) -> int | None:
    """The newest turn in a run's replay telemetry, or None.

    Reads only the file tail; the final line may be a torn write from the
    interrupted run being finalized, so scan back to the first line that
    parses.
    """
    try:
        with (run / "replay.jsonl").open("rb") as handle:
            handle.seek(0, os.SEEK_END)
            size = handle.tell()
            if size <= 0:
                return None
            handle.seek(max(0, size - 65536))
            lines = handle.read().splitlines()
    except OSError:
        return None
    for raw in reversed(lines):
        if not raw.strip():
            continue
        try:
            row = json.loads(raw)
        except (UnicodeError, json.JSONDecodeError):
            continue
        if isinstance(row, dict):
            turn = row.get("turn")
            if isinstance(turn, int) and turn > 0:
                return turn
        return None
    return None


def _orphan_player_seats(
    run: Path, manifest: Mapping[str, Any],
) -> dict[int, dict[str, Any]] | None:
    """player_id -> configured seat, for finalizing a run offline.

    A live game knows this mapping; for an orphan it is recovered from the
    first replay row, which records each player's seat_id. Without replay
    rows, seats are assumed in player order — the shape every launcher here
    produces.
    """
    config = manifest.get("config")
    seats = config.get("seats") if isinstance(config, dict) else None
    if not isinstance(seats, list) or not seats:
        return None
    by_id = {
        seat["id"]: seat for seat in seats
        if isinstance(seat, dict) and isinstance(seat.get("id"), str)
    }
    mapping: dict[int, dict[str, Any]] = {}
    try:
        with (run / "replay.jsonl").open("r", encoding="utf-8") as handle:
            for line in handle:
                if not line.strip():
                    continue
                row = json.loads(line)
                for player in row.get("players") or []:
                    player_id = player.get("player_id")
                    seat = by_id.get(player.get("seat_id"))
                    if isinstance(player_id, int) and seat is not None:
                        mapping[player_id] = seat
                break
    except (OSError, UnicodeError, json.JSONDecodeError):
        mapping = {}
    if not mapping:
        mapping = {
            index: seat for index, seat in enumerate(seats)
            if isinstance(seat, dict)
        }
    return mapping or None


def _atomic_json(path: Path, value: Any, mode: int = 0o644) -> None:
    temporary = path.with_name(f".{path.name}.{secrets.token_hex(6)}.tmp")
    descriptor = os.open(
        temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, mode,
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        os.chmod(path, mode)
    finally:
        temporary.unlink(missing_ok=True)


def _finite_number(
    value: Any, label: str, *, minimum: float, allow_zero: bool = False,
) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise APIProblem(HTTPStatus.BAD_REQUEST, f"{label} must be a number")
    number = float(value)
    if not math.isfinite(number):
        raise APIProblem(HTTPStatus.BAD_REQUEST, f"{label} must be finite")
    if allow_zero and number == 0:
        return number
    if number < minimum:
        raise APIProblem(
            HTTPStatus.BAD_REQUEST, f"{label} must be >= {minimum}",
        )
    return number


def _integer(
    value: Any, label: str, *, minimum: int, maximum: int,
) -> int:
    if (
        isinstance(value, bool) or not isinstance(value, int)
        or not minimum <= value <= maximum
    ):
        raise APIProblem(
            HTTPStatus.BAD_REQUEST,
            f"{label} must be an integer in [{minimum}, {maximum}]",
        )
    return value


@dataclass(frozen=True)
class Place:
    number: int
    native_player_number: int
    seat_id: str
    player_name: str
    joinable: bool
    player_color: str

    def public(self) -> dict[str, Any]:
        return {
            "place": self.number,
            "seat_id": self.seat_id,
            "player_name": self.player_name,
            "controller": "agent" if self.joinable else "native_classic_ai",
            "player_color": self.player_color,
        }


class Game:
    """One isolated Freeciv child and its concurrent external-agent barrier."""

    def __init__(
        self,
        supervisor: "Supervisor",
        game_id: str,
        config: dict[str, Any],
        owner_token: str,
        join_token: str,
        internal_token: str,
    ):
        self.supervisor = supervisor
        self.game_id = game_id
        self.config = config
        self.owner_token_hash = _digest(owner_token)
        self.join_token_hash = _digest(join_token)
        self.internal_token_hash = _digest(internal_token)
        self.episode = (supervisor.runs_root / game_id).resolve()
        self.episode.mkdir(parents=True, exist_ok=False)
        (self.episode / "saves").mkdir()
        (self.episode / "watch_frames").mkdir()
        self.bridge_status_path = create_bridge_journal(self.episode)
        self.replay_path = self.episode / "replay.jsonl"
        self.replay_catalog_path = self.episode / "replay-catalog.json"
        self.replay_warnings_path = self.episode / "replay-warnings.jsonl"
        self.victory_path = self.episode / "victory.json"
        self.replay_path.write_text("", encoding="utf-8")
        self.condition = threading.Condition(threading.RLock())
        self.console_lock = threading.RLock()
        self.frame_lock = threading.Lock()
        self.frame_metadata_lock = threading.RLock()
        self.frame_metadata_cache: dict[
            Path, tuple[tuple[int, int, str], dict[str, Any]]
        ] = {}
        self.video_lock = threading.Lock()
        self.replay_lock = threading.RLock()
        self.replay_cache_signature: tuple[Any, ...] | None = None
        self.replay_cache: dict[str, Any] | None = None
        self.video_frame_signature: str | None = None
        self.score_snapshot: dict[str, Any] | None = None
        self.state = "lobby"
        self.created_at = time.time()
        self.started_at: float | None = None
        self.finished_at: float | None = None
        self.current_turn: dict[str, Any] | None = None
        self.latest_turn: dict[str, Any] | None = None
        self.turn_request_hashes: dict[int, str] = {}
        self.turn_responses: dict[int, dict[str, Any]] = {}
        self.submissions: dict[tuple[int, str], dict[str, Any]] = {}
        self.timeline: list[dict[str, Any]] = []
        self.invalid_reasons: list[str] = []
        self.error: str | None = None
        self.returncode: int | None = None
        self.cancel_requested = False
        self.start_sent = False
        self.start_count = 0
        self.process: subprocess.Popen[str] | Any | None = None
        self.output_thread: threading.Thread | None = None
        self.at_prompt = False
        self.server_output_tail = ""
        self.server_output_sequence = 0
        self.server_output_lines: deque[tuple[int, str]] = deque(
            maxlen=NATIVE_VIEWER_EVENT_LIMIT,
        )
        self.observed_timeout: int | None = None
        self.observed_timeout_sequence = 0
        self.native_viewer: dict[str, Any] | None = None
        self.native_viewer_leases: dict[str, dict[str, Any]] = {}
        self.socket_polling_enabled = False
        self.native_timeout_override_sequence: int | None = None
        self.last_native_viewer_sigint_at: float | None = None
        self.native_turn_responses_in_flight = 0
        self.native_turn_response_generation = 0
        self.native_turn_response_completed_generation = 0
        self.native_turn_response_pending: dict[int, int | None] = {}
        self.native_turn_response_completed: set[int] = set()
        self.native_turn_response_marker_sequence = 0
        self.native_turn_response_marker_turn: int | None = None
        self.monitor_thread: threading.Thread | None = None
        self.lobby_thread: threading.Thread | None = None
        self.agents: dict[str, dict[str, Any]] = {}
        self.place_agents: dict[int, str] = {}
        self.sidecars: dict[int, Any] = {}
        self.sidecar_generations: dict[int, int] = {}
        self.sidecar_ready_generations: dict[int, int] = {}
        self.sidecar_health: dict[int, dict[str, Any]] = {}
        # Owner-private mapping from a configured place to the exact native
        # player incarnation reported by its sidecar. Public surfaces must
        # never expose these native handles.
        self.v2_native_player_identities: dict[int, tuple[int, int, int]] = {}
        self.v2_controls: dict[int, V2SeatControl] = {}
        self.v2_execution_locks: dict[
            int, tuple[int, V2SeatControl, threading.Lock]
        ] = {}
        self.v2_pregame_execution_lock = threading.Lock()
        self.v2_pregame_gate_open = False
        self.v2_pregame_ready_places: set[int] = set()
        # Seats whose own applied receipt says they resigned. Freeciv keeps a
        # surrendered player alive until it reaps them, so this is the only
        # way to tell "I resigned and am waiting" from "nothing happened".
        self.v2_surrendered_places: set[int] = set()
        self.v2_receipt_store: V2ReceiptStore | None = None
        self.v2_receipt_store_failed = False
        self.v2_phase_event_journal: V2PhaseEventJournal | None = None
        self.v2_pending_phase_ends: dict[str, dict[str, Any]] = {}
        self.v2_phase_event_journal_failed = False
        self.v2_failure_cleanup_started = False
        self.v2_ambiguity_trace: V2AmbiguityTrace | None = None
        self.v2_ambiguity_trace_warning_count = 0
        # A native boundary can stop answering while every component still
        # reports itself healthy.  These track the proof, the bounded budget
        # for escaping it, and what the seat should be told meanwhile.
        self.v2_wedge_detector = WedgeDetector()
        self.v2_recovery_budget = RecoveryBudget()
        # Serializes the recovery MECHANISM across seats: rebuilding a seat and
        # replacing the server are both whole-game operations, and two of them
        # at once corrupt each other.  Never held while self.condition is held.
        self.v2_recovery_lock = threading.RLock()
        self.v2_recovery_journal: V2RecoveryJournal | None = None
        self.v2_recovery_journal_failed = False
        # place -> {"trigger", "turn", "detected_at"} while the seat is known
        # wedged and has not been recovered.  Presence alone closes the seat's
        # runtime gate, so no request can be served against a dead boundary.
        self.v2_wedged_places: dict[int, dict[str, Any]] = {}
        # place -> {"kind", "attempt", "turn", "target_turn"} while a recovery
        # is in flight, for the seat-facing explanation of the wait.
        self.v2_recovery_in_flight: dict[int, dict[str, Any]] = {}
        # place -> (first_miss_monotonic, consecutive_misses) for liveness
        # polls that timed out.  A timeout is evidence of slowness; only a run
        # of them, over real time, with a dead process, is evidence of loss.
        self.v2_liveness_misses: dict[int, tuple[float, int]] = {}
        # place -> monotonic time of the last boundary command the seat's agent
        # completed, and of the last STATUS the poller actually issued.  A
        # command the client answered is liveness evidence the poller does not
        # need to duplicate, so it stands down for a moment afterwards instead
        # of competing with the agent for the single command stream.  The probe
        # clock bounds that: no amount of agent traffic may stop real sampling.
        self.v2_last_agent_command: dict[int, tuple[float, int]] = {}
        self.v2_last_liveness_probe: dict[int, tuple[float, int]] = {}
        # place -> the last recorded rollback event, published on health.
        self.v2_last_recovery: dict[int, dict[str, Any]] = {}
        # Game-wide recovery facts, published on the manifest so a scorer can
        # tell a clean game from a recovered one without reading the journal.
        self.v2_recovery_summary: dict[str, Any] = {
            "attempts": 0,
            "by_kind": {},
            "by_outcome": {},
            "rewound_applied_actions": False,
            "recovered_to_turns": [],
        }
        # Every unexpected seat loss this game survived, newest last, kept
        # owner-private: recovery means the latest-death file is no longer the
        # only death.
        self.v2_sidecar_exit_history: list[dict[str, Any]] = []
        # place -> the newest turn in which this seat had an action applied,
        # so a rollback can say whether it rewound past one.
        self.v2_applied_turns: dict[int, int] = {}
        # Which run of this game is current: 0 until an autosave rollback
        # replaces the world, then one more per rollback.  Everything keyed on
        # "what happened in turn T" -- phase events, command receipts -- has to
        # be scoped by it, because after a rewind turn T happens again and is
        # not the same turn T.
        self.v2_incarnation = 0
        # True only while a tier-2 rollback is between disowning the old
        # server and launching its replacement.  Every other seat's client
        # loses its connection in that window; that is the recovery working,
        # not a seat loss, and must never start a second recovery.
        self.v2_server_replacing = False
        self.v2_replay_producer: V2ReplayProducer | None = None
        self.v2_active_receipt_operations = 0
        self.v2_receipts_closing = False
        self.v2_phase_ledger: dict[str, Any] = {
            "state": "synchronizing",
            "key": None,
            "evidence": {},
            "active_place": None,
            "deadline_started_monotonic": None,
            "deadline_started_at": None,
            "synchronizing_started_monotonic": None,
            "reported_phase_counts": [],
            "progress_marker": None,
            "progress_started_monotonic": None,
            "end": None,
        }
        self.sidecar_exit_grace_generations: dict[int, int] = {}
        self.sidecars_stopping = False
        self.sidecar_start_deadline: float | None = None
        self.sidecar_status_thread: threading.Thread | None = None
        self.v2_replay_keepwarm_thread: threading.Thread | None = None
        self.server_exit_observed = False
        self.freeciv_port = supervisor.reserve_game_port()

        joinable = 1 if config["mode"] == "single" else config["places"]
        self.max_agents = joinable
        self.places = tuple(
            Place(
                number=index,
                native_player_number=index - 1,
                seat_id=f"place-{index}",
                player_name=(
                    f"AgentPlace{index}" if index <= joinable
                    else f"NativePlace{index}"
                ),
                joinable=index <= joinable,
                player_color=f"#{PLAYER_COLOR_HEX[index - 1]}",
            )
            for index in range(1, config["places"] + 1)
        )
        self._write_auth()
        self._write_manifest()
        try:
            if config["control_protocol"] == FULL_CONTROL_V2:
                self.v2_receipt_store = V2ReceiptStore(
                    self.episode, game_id=self.game_id,
                )
                self.v2_phase_event_journal = V2PhaseEventJournal(self.episode)
                try:
                    self.v2_ambiguity_trace = V2AmbiguityTrace(
                        self.episode, game_id=self.game_id,
                    )
                except Exception:
                    # Diagnostics are intentionally weaker than command
                    # durability.  An unsafe/unavailable trace never prevents
                    # a game from starting and can never authorize replay.
                    self.v2_ambiguity_trace_warning_count += 1
                # A full-control-v2 server never loads the strategic-v1 Lua
                # bridge, so nothing would append replay telemetry.  Rebuild
                # the same rows from the autosaves this game already writes.
                self.v2_replay_producer = V2ReplayProducer(
                    self.supervisor.runs_root,
                    self.game_id,
                    self.episode,
                    seat_ids=self._replay_seat_ids,
                    cache_root=self.supervisor.replay_cache_root,
                )
            self._launch(internal_token)
        except Exception:
            if self.v2_ambiguity_trace is not None:
                self.v2_ambiguity_trace.close()
            if self.v2_receipt_store is not None:
                self.v2_receipt_store.close()
            if self.v2_phase_event_journal is not None:
                self.v2_phase_event_journal.close()
            raise
        finally:
            supervisor.release_game_port(self.freeciv_port)

    @property
    def joinable_places(self) -> tuple[Place, ...]:
        return tuple(place for place in self.places if place.joinable)

    def _seat_config(self, place: Place) -> dict[str, Any]:
        kind = "external" if place.joinable else "native"
        agent_id = self.place_agents.get(place.number)
        agent = self.agents.get(agent_id) if agent_id else None
        seat = {
            "id": place.seat_id,
            "name": place.player_name,
            "type": kind,
            "model": (
                agent["metadata"].get("model")
                if agent and isinstance(agent["metadata"], dict)
                and isinstance(agent["metadata"].get("model"), str)
                else None
            ),
            "base_url": None,
            "instructions": self.config["objective"] if place.joinable else None,
            "options": {},
            "controller_label": (
                agent["controller_label"] if agent else None
            ),
            "controller_metadata": (
                agent["metadata"] if agent else {}
            ),
        }
        seat["controller_fingerprint"] = (
            agent["controller_fingerprint"] if agent
            else (None if place.joinable else controller_fingerprint(seat))
        )
        return seat

    def _public_places(self) -> list[dict[str, Any]]:
        rows = []
        for place in self.places:
            row = place.public()
            agent_id = self.place_agents.get(place.number)
            agent = self.agents.get(agent_id) if agent_id else None
            row["joined"] = agent is not None
            if agent is not None:
                metadata = agent["metadata"]
                row.update(
                    {
                        "controller_label": agent["controller_label"],
                        "controller_type": "external",
                        "model": (
                            metadata.get("model")
                            if isinstance(metadata, dict)
                            and isinstance(metadata.get("model"), str)
                            else None
                        ),
                        "controller_metadata": metadata,
                        "controller_fingerprint": agent[
                            "controller_fingerprint"
                        ],
                    }
                )
            elif not place.joinable:
                row.update({
                    "controller_label": "Freeciv Classic AI",
                    "controller_type": "native",
                    "model": "classic",
                })
            rows.append(row)
        return rows

    def _place_identity(self, place: Place) -> dict[str, Any]:
        agent_id = self.place_agents.get(place.number)
        agent = self.agents.get(agent_id) if agent_id else None
        if place.joinable:
            metadata = agent["metadata"] if agent else {}
            return {
                "controller_label": (
                    agent["controller_label"]
                    if agent else "Unclaimed agent place"
                ),
                "controller_type": "external",
                "model": (
                    metadata.get("model")
                    if isinstance(metadata, dict)
                    and isinstance(metadata.get("model"), str)
                    else None
                ),
            }
        return {
            "controller_label": "Freeciv Classic AI",
            "controller_type": "native",
            "model": "classic",
        }

    def _private_player_seats_locked(self) -> dict[int, dict[str, Any]]:
        """Build report attribution without persisting native handles."""
        result: dict[int, dict[str, Any]] = {}
        for place in self.places:
            native_player = place.native_player_number
            identity = self.v2_native_player_identities.get(place.number)
            if identity is not None:
                native_player = identity[1]
            result[native_player] = self._seat_config(place)
        return result

    def _replay_seat_ids(self) -> dict[int, str]:
        """Map each native player number to the seat that configured it.

        Replay rows rebuilt from autosaves identify players by number, so this
        mirrors ``_private_player_seats_locked`` and keeps an archived journal
        and the episode report attributing the same player to the same seat.
        """
        with self.condition:
            result: dict[int, str] = {}
            for place in self.places:
                identity = self.v2_native_player_identities.get(place.number)
                result[
                    identity[1] if identity is not None
                    else place.native_player_number
                ] = place.seat_id
            return result

    def _manifest(self, state: str | None = None) -> dict[str, Any]:
        return {
            "schema_version": 1,
            "game_id": self.game_id,
            "state": state or self.state,
            "status": state or self.state,
            "control_protocol": self.config["control_protocol"],
            "benchmark_valid": (
                (state or self.state) == "completed"
                if (state or self.state) in TERMINAL_STATES
                else (False if self.invalid_reasons else None)
            ),
            "error": self.error,
            "invalid_reasons": list(self.invalid_reasons),
            "created_at": self.created_at,
            "started_at": self.started_at,
            "finished_at": self.finished_at,
            "returncode": self.returncode,
            "config": {
                "schema_version": 1,
                "name": f"session-{self.game_id[:12]}",
                "mode": self.config["mode"],
                "places": self.config["places"],
                "max_agents": self.max_agents,
                "turns": self.config["turns"],
                "seeds": [self.config["seed"]],
                "ruleset": self.config["ruleset"],
                "objective": self.config["objective"],
                "control_protocol": self.config["control_protocol"],
                "timing_mode": self.config["timing_mode"],
                "action_timeout_s": self.config["action_timeout_s"],
                "lobby_timeout_s": self.config["lobby_timeout_s"],
                "server": {
                    "frame_interval": self.config["frame_interval"],
                    "frame_zoom": self.config["frame_zoom"],
                },
                "seats": [self._seat_config(place) for place in self.places],
            },
            "resolved_places": self._public_places(),
            "joined_agents": len(self.agents),
            "start_count": self.start_count,
            "current_turn": (
                self._current_turn_locked()
            ),
            "commands_file": "server.commands",
            "trace_file": "decisions.jsonl",
            "bridge_status_file": self.bridge_status_path.name,
            "scorelog_file": "score.log",
            "frames": len(self._ppm_frames()),
            "checkpoints": len(self._save_files()),
            "video_file": "game.mp4" if (self.episode / "game.mp4").exists() else None,
            "recovery": self._v2_recovery_manifest_locked(),
        }

    def _write_manifest(self, state: str | None = None) -> None:
        _atomic_json(self.episode / "manifest.json", self._manifest(state))

    def _write_auth(self) -> None:
        _atomic_json(
            self.episode / "auth.json",
            {
                "schema_version": 1,
                "owner_token_sha256": self.owner_token_hash,
                "join_token_sha256": self.join_token_hash,
                "internal_token_sha256": self.internal_token_hash,
                "agents": {
                    agent_id: {
                        "place": agent["place"],
                        "token_sha256": agent["token_hash"],
                    }
                    for agent_id, agent in self.agents.items()
                },
            },
            mode=0o600,
        )

    def _setup_commands(self) -> list[str]:
        full_control = self.config["control_protocol"] == FULL_CONTROL_V2
        commands = [
            "set aifill 0",
            "set minplayers 0",
            f"set maxplayers {self.config['places']}",
            "set timeout 0" if full_control else "set timeout -1",
            f"set endturn {self.config['turns']}",
            "set plrcolormode PLR_SET",
            "set traitdistribution fixed",
            "set ec_turns 0",
            "set threaded_save disabled",
            f"set mapseed {self.config['seed']}",
            f"set gameseed {self.config['seed']}",
            "set scorelog enabled",
            "set scoreloglevel all",
            "set scorefile score.log",
            "set saveturns 1",
            "set autosaves turn|gameover",
            "set savename turn-%04T-%R",
        ]
        if full_control:
            commands[4:4] = [
                "set first_timeout 0",
                "set autotoggle disabled",
                "set phasemode PLAYER",
                "set fixedlength disabled",
                "set turnblock disabled",
            ]
        if self.config["frame_interval"]:
            commands.append(
                "mapimg define "
                f"zoom={self.config['frame_zoom']}:map=tcub:show=all:"
                f"turns={self.config['frame_interval']}:format=ppm|ppm"
            )
        commands.extend(
            f"create {place.player_name} classic" for place in self.places
        )
        commands.extend(
            f"playercolor {place.player_name} {place.player_color[1:]}"
            for place in self.places
        )
        if full_control:
            commands.extend(
                f"aitoggle {place.player_name}"
                for place in self.joinable_places
            )
        commands.append(self.config.get("difficulty", "hard"))
        if self.config["control_protocol"] == STRATEGIC_V1:
            commands.append(
                "lua unsafe-file "
                f"{(REPO_ROOT / 'agent_eval' / 'bridge.lua').resolve()}"
            )
        return commands

    def _launch(self, internal_token: str) -> None:
        binary = self.supervisor.binary
        commands = self._setup_commands()
        (self.episode / "server.commands").write_text(
            "\n".join(commands) + "\n", encoding="utf-8",
        )
        environment = self._process_environment(internal_token)
        command = [
            str(binary),
            "--Announce",
            "none",
            "--bind",
            "127.0.0.1",
            "--port",
            str(self.freeciv_port),
            "--exit-on-end",
            "--ruleset",
            self.config["ruleset"],
            "--saves",
            str(self.episode / "saves"),
            "--log",
            str(self.episode / "server.log"),
        ]
        try:
            self.process = self.supervisor.process_factory(
                command,
                cwd=self.episode,
                env=environment,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                bufsize=0,
            )
        except Exception as exc:
            self._abort_launch(f"could not launch freeciv-server: {exc}")
            raise
        try:
            self.output_thread = threading.Thread(
                target=self._pump_output,
                name=f"freeciv-output-{self.game_id}",
                daemon=True,
            )
            self.output_thread.start()
            self._wait_for_prompt()
            self._send_commands(commands)
            self.monitor_thread = threading.Thread(
                target=self._monitor, args=(self.process,),
                name=f"freeciv-monitor-{self.game_id}",
                daemon=True,
            )
            self.monitor_thread.start()
            if self.config["lobby_timeout_s"] > 0:
                self.lobby_thread = threading.Thread(
                    target=self._lobby_watchdog,
                    name=f"freeciv-lobby-{self.game_id}",
                    daemon=True,
                )
                self.lobby_thread.start()
        except Exception as exc:
            self._abort_launch(f"freeciv pregame setup failed: {exc}")
            raise

    def _process_environment(self, internal_token: str) -> dict[str, str]:
        environment = os.environ.copy()
        environment.setdefault("FREECIV_DATA_PATH", str(REPO_ROOT / "data"))
        if self.config["control_protocol"] == FULL_CONTROL_V2:
            for name in tuple(environment):
                if name.startswith("AGENT_EVAL_"):
                    del environment[name]
            return environment
        environment["AGENT_EVAL_GAME_ID"] = self.game_id
        environment["AGENT_EVAL_INTERNAL_TOKEN"] = internal_token
        environment["AGENT_EVAL_BRIDGE_STATUS_PATH"] = str(
            self.bridge_status_path
        )
        environment["AGENT_EVAL_REPLAY_PATH"] = str(self.replay_path)
        environment["AGENT_EVAL_REPLAY_CATALOG_PATH"] = str(
            self.replay_catalog_path
        )
        environment["AGENT_EVAL_REPLAY_WARNINGS_PATH"] = str(
            self.replay_warnings_path
        )
        environment["AGENT_EVAL_VICTORY_PATH"] = str(self.victory_path)
        environment["AGENT_EVAL_TURN_URL"] = (
            f"{self.supervisor.internal_service_url}/internal/v1/games/"
            f"{self.game_id}/turns"
        )
        action_timeout_s = self.config["action_timeout_s"]
        environment["AGENT_EVAL_TURN_TIMEOUT_S"] = (
            "0" if action_timeout_s is None
            else str(max(30, math.ceil(action_timeout_s + 15)))
        )
        environment["AGENT_EVAL_SEATS"] = ",".join(
            f"{place.seat_id}:{place.player_name}"
            for place in self.joinable_places
        )
        environment["AGENT_EVAL_REPLAY_SEATS"] = ",".join(
            f"{place.seat_id}:{place.player_name}" for place in self.places
        )
        return environment

    def _abort_launch(self, message: str) -> None:
        process = self.process
        if process is not None:
            if process.stdin is not None:
                try:
                    process.stdin.close()
                except OSError:
                    pass
            if process.poll() is None:
                try:
                    process.terminate()
                except OSError:
                    pass
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    try:
                        process.kill()
                    except OSError:
                        pass
                    try:
                        process.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        pass
            if self.output_thread is not None:
                self.output_thread.join(timeout=2)
            if process.stdout is not None and not process.stdout.closed:
                try:
                    process.stdout.close()
                except OSError:
                    pass
            self.returncode = process.poll()
        with self.condition:
            self.state = "failed"
            self.error = message
            self.finished_at = time.time()
            self._terminalize_v2_phase_locked("failed")
            self._write_manifest()
            self.condition.notify_all()

    def _pump_output(self) -> None:
        process = self.process
        if process is None or process.stdout is None:
            return
        tail = b""
        line_buffer = b""
        with process.stdout, (self.episode / "server.stdout.log").open(
            "ab", buffering=0,
        ) as log:
            while True:
                chunk = process.stdout.read(1)
                if not chunk:
                    break
                if isinstance(chunk, str):
                    chunk = chunk.encode("utf-8", errors="replace")
                log.write(chunk)
                tail = (tail + chunk)[-2:]
                line_buffer = (line_buffer + chunk)[-65_536:]
                with self.condition:
                    self.server_output_tail = (
                        self.server_output_tail
                        + chunk.decode("utf-8", errors="replace")
                    )[-131_072:]
                    self.at_prompt = tail == b"> "
                    if chunk == b"\n":
                        self._record_server_output_line_locked(
                            line_buffer.decode("utf-8", errors="replace").rstrip(
                                "\r\n",
                            ),
                        )
                        line_buffer = b""
                    self.condition.notify_all()
        with self.condition:
            self.condition.notify_all()

    def _record_server_output_line_locked(self, line: str) -> int:
        """Record one ordered output line while ``condition`` is held."""
        self.server_output_sequence += 1
        sequence = self.server_output_sequence
        self.server_output_lines.append((sequence, line))
        timeout_match = CONSOLE_TIMEOUT_RE.search(line)
        if timeout_match is None:
            timeout_match = SIGNAL_TIMEOUT_RE.search(line)
        if timeout_match is not None:
            self.observed_timeout = int(timeout_match.group(1))
            self.observed_timeout_sequence = sequence
        response_done_match = NATIVE_TURN_RESPONSE_DONE_RE.search(line)
        if response_done_match is not None:
            marker_turn = int(response_done_match.group(1))
            self.native_turn_response_marker_sequence = sequence
            self.native_turn_response_marker_turn = marker_turn
            matching_generation = next((
                generation
                for generation, turn in self.native_turn_response_pending.items()
                if turn == marker_turn
            ), None)
            if matching_generation is None:
                matching_generation = next((
                    generation
                    for generation, turn in self.native_turn_response_pending.items()
                    if turn is None
                ), None)
            if matching_generation is not None:
                del self.native_turn_response_pending[matching_generation]
                self.native_turn_response_completed.add(matching_generation)
                while (
                    self.native_turn_response_completed_generation + 1
                    in self.native_turn_response_completed
                ):
                    completed = self.native_turn_response_completed_generation + 1
                    self.native_turn_response_completed.remove(completed)
                    self.native_turn_response_completed_generation = completed
        return sequence

    def _record_server_output_line(self, line: str) -> int:
        """Testable synchronized entrypoint for an ordered server line."""
        with self.condition:
            sequence = self._record_server_output_line_locked(line)
            self.condition.notify_all()
            return sequence

    def _wait_for_server_output(
        self,
        after_sequence: int,
        predicate: Callable[[str], bool],
        description: str,
        timeout_s: float = 20,
    ) -> str:
        deadline = time.monotonic() + timeout_s
        with self.condition:
            while True:
                for sequence, line in self.server_output_lines:
                    if sequence > after_sequence and predicate(line):
                        return line
                if self.supervisor.shutdown_event.is_set():
                    raise SupervisorError("supervisor is shutting down")
                process = self.process
                if process is not None and process.poll() is not None:
                    raise SupervisorError(
                        "freeciv-server exited while waiting for " + description
                    )
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise SupervisorError(
                        "timed out waiting for " + description
                    )
                self.condition.wait(min(remaining, 0.1))

    def _wait_for_timeout(
        self, value: int, after_sequence: int, timeout_s: float = 20,
    ) -> None:
        self._wait_for_server_output(
            after_sequence,
            lambda _line: (
                self.observed_timeout_sequence > after_sequence
                and self.observed_timeout == value
            ),
            f"Freeciv to acknowledge timeout {value}",
            timeout_s,
        )

    def _wait_for_prompt(self, timeout_s: float = 20) -> None:
        deadline = time.monotonic() + timeout_s
        with self.condition:
            while True:
                if self.supervisor.shutdown_event.is_set():
                    raise SupervisorError("supervisor is shutting down")
                if self.at_prompt:
                    # A response line can briefly begin with the same two
                    # bytes.  It is a real prompt only if it stays idle.
                    self.condition.wait(0.02)
                    if self.at_prompt:
                        return
                process = self.process
                if process is not None and process.poll() is not None:
                    raise SupervisorError(
                        "freeciv-server exited while waiting for its prompt"
                    )
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise SupervisorError(
                        "timed out waiting for freeciv-server prompt"
                    )
                self.condition.wait(min(remaining, 0.1))

    def _send_commands(
        self,
        commands: list[str],
        *,
        wait_for_prompt: bool = True,
        expected_timeout: int | None = None,
    ) -> None:
        if expected_timeout is not None and len(commands) != 1:
            raise ValueError("expected_timeout requires exactly one command")
        with self.console_lock:
            # Bound to one exact process for the whole batch.  A rollback can
            # disown and replace the server between two commands of the same
            # batch, and a batch that finishes against a different server than
            # it started against is neither the settings it meant to apply nor
            # a failure anyone would see.
            process = self.process
            if process is None or process.stdin is None:
                raise SupervisorError("Freeciv stdin is unavailable")
            for command in commands:
                if self.supervisor.shutdown_event.is_set():
                    raise SupervisorError("supervisor is shutting down")
                if self.process is not process:
                    raise SupervisorError(
                        "the Freeciv server was replaced while sending "
                        "console commands"
                    )
                try:
                    with self.condition:
                        self.at_prompt = False
                        after_sequence = self.server_output_sequence
                    value: Any = (command + "\n").encode("utf-8")
                    process.stdin.write(value)
                    process.stdin.flush()
                except (BrokenPipeError, OSError) as exc:
                    raise SupervisorError(
                        f"cannot write Freeciv command: {exc}"
                    ) from exc
                if expected_timeout is not None:
                    # timeout=-1 immediately resumes the autogame, so the
                    # server prompt is intentionally unusable.  Its fresh
                    # setting acknowledgement is the authoritative result.
                    self._wait_for_timeout(expected_timeout, after_sequence)
                elif wait_for_prompt:
                    self._wait_for_prompt()

    def _send_timeout(self, value: int) -> None:
        self._send_commands(
            [f"set timeout {value}"],
            wait_for_prompt=False,
            expected_timeout=value,
        )

    def _lobby_watchdog(self) -> None:
        deadline = time.monotonic() + self.config["lobby_timeout_s"]
        timed_out = False
        with self.condition:
            while (
                self.state == "lobby"
                and len(self.place_agents) < self.max_agents
            ):
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    self.error = "lobby timed out before all agent places joined"
                    self.state = "failed"
                    self.finished_at = time.time()
                    self._terminalize_v2_phase_locked("failed")
                    self._write_manifest()
                    self.condition.notify_all()
                    timed_out = True
                    break
                self.condition.wait(remaining)
        if timed_out:
            self._stop_all_sidecars()
            self._terminate_child()

    def _terminate_child(self) -> None:
        process = self.process
        if process is None or process.poll() is not None:
            return
        try:
            process.terminate()
        except OSError:
            return

        def kill_later() -> None:
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                try:
                    process.kill()
                except OSError:
                    pass

        threading.Thread(target=kill_later, daemon=True).start()

    def _monitor(self, process: Any = None) -> None:
        if process is None:
            process = self.process
        if process is None:
            return
        monitor_error: str | None = None
        try:
            returncode = process.wait()
        except Exception as exc:
            returncode = None
            monitor_error = f"could not monitor freeciv-server: {exc}"
        with self.condition:
            if self.process is not process:
                # Boundary recovery disowns a server before replacing it, so
                # this exit is the intended end of a superseded process, not
                # the end of the game.  Its replacement has its own monitor.
                # Nothing about a superseded process may be published, an
                # error least of all: a game whose error is set is failed at
                # its next classification, so recording one here would let a
                # retired server end a game that recovery had just rescued.
                return
            if monitor_error is not None:
                self.error = monitor_error
        # Once wait() has returned, native client disconnects are an expected
        # consequence of server completion.  Publish that fact before output
        # draining or sidecar shutdown so their callbacks cannot invalidate a
        # normally completed match during finalization.
        with self.condition:
            self.server_exit_observed = True
            self._terminalize_v2_phase_locked("terminalizing")
            self.condition.notify_all()
        if process.stdin is not None:
            process.stdin.close()
        if self.output_thread is not None:
            self.output_thread.join(timeout=5)
        self._stop_all_sidecars()
        with self.condition:
            self.returncode = returncode
            self.finished_at = time.time()
            if self.cancel_requested:
                target = "cancelled"
            elif self.error:
                target = "failed"
            elif returncode == 0 and (self.episode / "score.log").exists():
                if self.config["control_protocol"] == STRATEGIC_V1:
                    bridge_reasons = validate_bridge_journal(
                        self.bridge_status_path,
                        (entry["turn"] for entry in self.timeline),
                        self.episode / "score.log",
                    )
                    server_text = "\n".join(
                        path.read_text(encoding="utf-8", errors="replace")
                        for path in (
                            self.episode / "server.log",
                            self.episode / "server.stdout.log",
                        )
                        if path.exists()
                    ).lower()
                    if (
                        "agent_eval bridge:" in server_text
                        and "bridge_lua_error" not in bridge_reasons
                    ):
                        bridge_reasons.append("bridge_lua_error")
                    for reason in bridge_reasons:
                        if reason not in self.invalid_reasons:
                            self.invalid_reasons.append(reason)
                try:
                    self._configured_score_snapshot()
                except (OSError, ScorelogError, TypeError, ValueError):
                    reason = "score_snapshot_incomplete"
                    if reason not in self.invalid_reasons:
                        self.invalid_reasons.append(reason)
                target = "invalid" if self.invalid_reasons else "completed"
            else:
                target = "failed"
                self.error = (
                    f"freeciv-server exited {returncode} or produced no score.log"
                )
            self._write_manifest(target)
        self._drain_v2_replay()
        try:
            with self.condition:
                private_player_seats = self._private_player_seats_locked()
            summary = summarize_episode(
                self.episode,
                private_player_seats=private_player_seats,
            )
            _atomic_json(self.episode / "report.json", summary)
        except Exception as exc:
            with self.condition:
                if self.error is None:
                    self.error = f"could not finalize report: {exc}"
                if target == "completed":
                    target = "failed"
        if self._ppm_frames():
            try:
                self._render_video(force=True)
            except Exception as exc:
                with self.condition:
                    if self.error is None:
                        self.error = f"could not render video: {exc}"
        with self.condition:
            self.state = target
            self._terminalize_v2_phase_locked(target)
            self._write_manifest()
            try:
                summary = summarize_episode(
                    self.episode,
                    private_player_seats=self._private_player_seats_locked(),
                )
                _atomic_json(self.episode / "report.json", summary)
            except Exception:
                pass
            self.condition.notify_all()

    @staticmethod
    def _sanitized_sidecar_health(
        sidecar: Any, generation: int,
    ) -> dict[str, Any]:
        try:
            source = (
                sidecar.public_health()
                if callable(getattr(sidecar, "public_health", None))
                else getattr(sidecar, "health", {})
            )
            if callable(source):
                source = source()
        except Exception:
            source = {"state": "failed", "error_code": "health_unavailable"}
        if not isinstance(source, dict):
            source = {"state": "failed", "error_code": "health_unavailable"}
        clean = {
            key: value for key, value in source.items()
            if key in SIDECAR_HEALTH_FIELDS
            and (value is None or isinstance(value, (str, int, float, bool)))
        }
        clean["generation"] = generation
        clean.setdefault("state", "unknown")
        return clean

    def _make_sidecar(self, place: Place, generation: int) -> Any:
        callback = lambda current_generation, health: self._on_sidecar_exit(
            place.number, current_generation, health,
        )
        return self.supervisor.sidecar_factory(
            binary=self.supervisor.agent_binary,
            run_root=self.episode / "sidecars",
            game_id=self.game_id,
            seat_id=place.seat_id,
            player_name=place.player_name,
            host="127.0.0.1",
            port=self.freeciv_port,
            generation=generation,
            on_exit=callback,
        )

    @staticmethod
    def _collect_v2_sidecar_forensics(sidecar: Any) -> dict[str, Any]:
        """Ask one sidecar how it stopped working, tolerating any answer.

        A seat can be lost precisely because its sidecar is unusable, so this
        must never raise: no evidence is a worse outcome than partial
        evidence, but neither may replace the original failure.

        This runs with the game condition held, deliberately.  The cost is one
        non-blocking ``poll()`` and two tail reads bounded to 4 KiB each, of
        files in the run directory the supervisor already writes under the
        same lock; the benefit is that the process, the sidecar's own state
        and its logs are sampled as one instant.  Releasing the lock first
        would let the generation be retired and the sidecar stopped in
        between, so the evidence would describe a seat that no longer exists.
        """
        collect = getattr(sidecar, "private_exit_forensics", None)
        if not callable(collect):
            return {}
        try:
            forensics = collect()
        except Exception:
            return {}
        if not isinstance(forensics, Mapping):
            return {}
        clean: dict[str, Any] = {}
        for key in (
            "exit_code", "exit_signal", "exit_signal_name", "process_alive",
            "sidecar_state", "client_state", "error_code", "last_seen_at",
        ):
            value = forensics.get(key)
            if value is None or isinstance(value, (str, int, float, bool)):
                clean[key] = value
        for key in ("stderr_tail", "stdout_tail"):
            value = forensics.get(key)
            if isinstance(value, (list, tuple)):
                clean[key] = [
                    item[:512] for item in value
                    if isinstance(item, str)
                ][-30:]
        return clean

    @staticmethod
    def _v2_forensic_summary(forensics: Mapping[str, Any]) -> str:
        """One human-readable clause naming how a seat's client stopped."""
        if not forensics:
            return "no exit diagnostics were available"
        signal_name = forensics.get("exit_signal_name")
        exit_signal = forensics.get("exit_signal")
        exit_code = forensics.get("exit_code")
        if isinstance(exit_signal, int):
            cause = f"killed by signal {signal_name or exit_signal}"
        elif isinstance(exit_code, int):
            cause = f"exited with code {exit_code}"
        elif forensics.get("process_alive") is True:
            cause = "stopped answering while still running"
        else:
            cause = "stopped without an observed exit status"
        client_state = forensics.get("client_state")
        error_code = forensics.get("error_code")
        detail = []
        if isinstance(client_state, str):
            detail.append(f"last native client state {client_state}")
        if isinstance(error_code, str):
            detail.append(f"sidecar error {error_code}")
        return cause + (f" ({', '.join(detail)})" if detail else "")

    @staticmethod
    def _v2_seat_loss_attribution(
        forensics: Mapping[str, Any],
    ) -> tuple[str, str]:
        """Name the seat loss by the evidence, not by assumption.

        The hard-coded wording used to assert "sidecar exited" for a client
        that was demonstrably still running, producing the self-contradicting
        sentence "sidecar exited (... stopped answering while still running)".
        That sentence sent the turn-66 hunt after a native crash for a day.
        The machine-readable reason token was wrong in the same way, and it is
        what scoring consumes.
        """
        exited = isinstance(forensics.get("exit_code"), int) or isinstance(
            forensics.get("exit_signal"), int,
        )
        if not exited and forensics.get("process_alive") is True:
            return (
                "sidecar_unresponsive",
                "full-control-v2 sidecar stopped answering while still "
                "running",
            )
        return ("sidecar_exited", "full-control-v2 sidecar exited")

    def _v2_sidecar_exit_recoverable_locked(self, place_number: int) -> bool:
        """Whether a lost seat should be recovered instead of ending the game.

        Only a mid-game seat behind a live server qualifies.  The server owns
        the authoritative state and its per-turn autosaves, so a new sidecar
        generation can retake the seat having lost nothing.  A loss during the
        lobby, during an intentional teardown, or after the server itself has
        gone keeps the existing fail-closed behaviour, because in those cases
        there is either nothing to return to or something else already owns
        the outcome.
        """
        return bool(
            self.config["control_protocol"] == FULL_CONTROL_V2
            # "starting" counts: the server is up and the game may already be
            # under way, so the seat still has somewhere to come back to.
            and self.state in {"running", "starting"}
            and self.start_sent
            and not self.cancel_requested
            and not self.sidecars_stopping
            and not self.server_exit_observed
            and self.place_agents.get(place_number) is not None
            and place_number not in self.v2_wedged_places
            and place_number not in self.v2_recovery_in_flight
        )

    def _v2_death_context_locked(
        self, place_number: int, last_client_state: Any = None,
    ) -> dict[str, Any]:
        """Where in the game one seat was when its client stopped serving.

        Forensics from the sidecar say how it died; this says when.  Without
        the turn, the phase and the seat-local revision that the boundary had
        last agreed on, a death recorded mid-transition cannot be told apart
        from one at rest, which is exactly the ambiguity that left the turn-66
        incident unattributable.  ``last_client_state`` is passed in by the
        caller rather than read here, because it has to come from the last
        health the supervisor itself accepted, before the failure health of
        the dying generation overwrites it: what the client last said while it
        was working is evidence, and what it says while dying is not.
        """
        ledger = self.v2_phase_ledger
        key = ledger.get("key")
        keyed = key if isinstance(key, tuple) and len(key) == 2 else (None, None)
        evidence = ledger.get("evidence", {}).get(place_number)
        revision = (
            evidence.get("seat_local_revision")
            if isinstance(evidence, Mapping) else None
        )
        # Both come from the ledger key, which is the last turn and phase every
        # seat agreed on.  That survives the death: live consensus does not,
        # because it needs the seat that has just stopped reporting.
        return {
            "turn": keyed[0],
            "phase": keyed[1],
            "phase_ledger_state": ledger.get("state"),
            "seat_local_revision": revision if isinstance(revision, int) else None,
            "last_status_client_state": (
                last_client_state if isinstance(last_client_state, str) else None
            ),
        }

    @staticmethod
    def _v2_death_context_summary(context: Mapping[str, Any]) -> str:
        """One clause placing a seat loss in the game, for the manifest error."""
        turn = context.get("turn")
        phase = context.get("phase")
        where = (
            f"turn {turn}" if isinstance(turn, int) else "an unknown turn"
        )
        if isinstance(phase, int):
            where += f" phase {phase}"
        ledger_state = context.get("phase_ledger_state")
        if isinstance(ledger_state, str):
            where += f" while the phase ledger was {ledger_state}"
        revision = context.get("seat_local_revision")
        if isinstance(revision, int):
            where += f", at seat revision {revision}"
        return where

    def _persist_sidecar_exit_diagnostic(
        self,
        place_number: int,
        generation: int,
        health: Mapping[str, Any],
        forensics: Mapping[str, Any] | None = None,
        context: Mapping[str, Any] | None = None,
    ) -> None:
        """Best-effort owner-private evidence for an unexpected seat loss."""
        record = {
            "died_at": dict(context or {}),
            "error_code": health.get("error_code"),
            "exit_code": health.get("exit_code"),
            "forensics": dict(forensics or {}),
            "game_id": self.game_id,
            "generation": generation,
            "last_seen_at": health.get("last_seen_at"),
            "place": place_number,
            "sidecar_state": health.get("state"),
            "stopped_at": health.get("stopped_at"),
            "timestamp": time.time(),
        }
        # Recovery makes several deaths in one game the expected case rather
        # than an impossible one, and the single latest-death file is
        # overwritten by each.  The journal keeps every death's exit status,
        # but the log tails are the only evidence that distinguishes a native
        # crash from a silent disappearance, so the earlier ones are kept here
        # too.  Bounded, because this is evidence and not a log.
        self.v2_sidecar_exit_history.append(record)
        del self.v2_sidecar_exit_history[:-V2_SIDECAR_EXIT_HISTORY_LIMIT]
        try:
            _atomic_json(
                self.episode / V2_SIDECAR_EXIT_HISTORY_FILENAME,
                {
                    "schema_version": 1,
                    "game_id": self.game_id,
                    "deaths": list(self.v2_sidecar_exit_history),
                },
                mode=0o600,
            )
        except Exception:
            pass
        try:
            _atomic_json(
                self.episode / V2_SIDECAR_EXIT_DIAGNOSTIC_FILENAME,
                record,
                mode=0o600,
            )
        except Exception:
            # Diagnostics are subordinate to the original sidecar failure.
            # Never prevent or alter fail-closed terminalization if the run
            # directory has become unavailable or unwritable.
            pass

    def _on_sidecar_exit(
        self, place_number: int, generation: int, health: Any,
        *, after_completion_grace: bool = False,
    ) -> bool:
        """Handle one seat loss; return true when recovery has taken it over."""
        should_terminate = False
        should_stop = False
        should_defer = False
        should_recover: dict[str, Any] | None = None
        control_to_close: V2SeatControl | None = None
        clean: dict[str, Any]
        with self.condition:
            if self.sidecar_generations.get(place_number) != generation:
                return False
            sidecar = self.sidecars.get(place_number)
            # The last health the supervisor accepted while the seat was still
            # working; the failure health below is about to replace it, and it
            # is the only record of what the native client was doing before it
            # stopped.
            last_good_client_state = self.sidecar_health.get(
                place_number, {},
            ).get("client_state")
            clean = self._sanitized_sidecar_health(sidecar, generation)
            if isinstance(health, dict):
                clean.update({
                    key: value for key, value in health.items()
                    if key in SIDECAR_HEALTH_FIELDS
                    and (value is None or isinstance(value, (str, int, float, bool)))
                })
                clean["generation"] = generation
            self.sidecar_health[place_number] = clean
            if (
                self.sidecars_stopping or self.cancel_requested
                or self.state in TERMINAL_STATES or self.server_exit_observed
            ):
                self.sidecar_exit_grace_generations.pop(place_number, None)
                self.condition.notify_all()
                return False
            if self.v2_server_replacing and place_number not in (
                self.v2_recovery_in_flight
            ):
                # A tier-2 rollback is between disowning the old server and
                # launching its replacement.  Every seat that is not the one
                # being recovered loses its connection in that window BY
                # DESIGN.  Treating that as a seat loss starts a competing
                # recovery per surviving seat, which drains the shared budget
                # and fails the game in the middle of the rollback that was
                # saving it.  Keep polling: once the replacement server is up
                # the latch clears, and this seat then takes its OWN tier-1
                # re-attach against it -- serialized behind the rollback by
                # v2_recovery_lock, and charged to its own (turn, place)
                # ladder rather than to the rollback's.
                self.condition.notify_all()
                return True
            # A bootstrap failure is rolled back transactionally by join().
            # Only a generation which was previously READY is an unexpected
            # loss of an established human seat.
            if self.sidecar_ready_generations.get(place_number) != generation:
                self.condition.notify_all()
                return False
            if after_completion_grace:
                if (
                    self.sidecar_exit_grace_generations.get(place_number)
                    != generation
                ):
                    return False
                self.sidecar_exit_grace_generations.pop(place_number, None)
            elif self.sidecar_exit_grace_generations.get(place_number) == generation:
                # STATUS polling may observe the same failed sidecar while the
                # exact Freeciv process is still inside its completion grace.
                return False

            process = self.process
            try:
                server_returncode = process.poll() if process is not None else None
            except Exception:
                server_returncode = None
            if process is not None and server_returncode is not None:
                # The server monitor owns classification from its exact exit
                # code and score artifact.  A sidecar disconnect is expected
                # once that process has exited, even if its monitor has not
                # yet drained output.
                self.condition.notify_all()
                return False

            deferrable = clean.get("error_code") in {
                "disconnected", "unexpected_eof", "process_exited",
            }
            if not after_completion_grace and process is not None and deferrable:
                self.sidecar_exit_grace_generations[place_number] = generation
                should_defer = True
            else:
                self.sidecar_ready_generations.pop(place_number, None)
                control_to_close = self.v2_controls.pop(place_number, None)
                lock_record = self.v2_execution_locks.get(place_number)
                if (
                    lock_record is not None
                    and lock_record[0] == generation
                    and lock_record[1] is control_to_close
                ):
                    self.v2_execution_locks.pop(place_number, None)
                # Ask the dying sidecar how it died before anything discards
                # it.  This is the only moment its process, its state and its
                # logs are all still reachable.
                forensics = self._collect_v2_sidecar_forensics(sidecar)
                context = self._v2_death_context_locked(
                    place_number, last_good_client_state,
                )
                self._persist_sidecar_exit_diagnostic(
                    place_number, generation, clean, forensics, context,
                )
                if self._v2_sidecar_exit_recoverable_locked(place_number):
                    # A mid-game seat loss on a live server is exactly what
                    # tier-1 recovery is for: the server still holds the
                    # authoritative state and its autosaves, so retaking the
                    # seat on a new generation discards no play at all.  The
                    # failure counter is not consulted, because a seat whose
                    # client stopped serving is already a proven fault.
                    should_recover = {
                        "trigger": "sidecar_exit",
                        "turn": self._current_turn_locked() or 1,
                        "detected_at": time.time(),
                        "generation": generation,
                        "forensics": forensics,
                        "death_context": context,
                    }
                    self.v2_wedged_places[place_number] = should_recover
                    self.v2_wedge_detector.clear(place_number)
                    self._write_manifest()
                    self.condition.notify_all()
                else:
                    reason_token, prefix = self._v2_seat_loss_attribution(
                        forensics,
                    )
                    self.error = (
                        prefix + "; the human-controlled "
                        "seat was not replaced by Freeciv AI ("
                        + self._v2_forensic_summary(forensics)
                        + "; lost at "
                        + self._v2_death_context_summary(context) + ")"
                    )
                    if (
                        self.start_sent
                        and reason_token not in self.invalid_reasons
                    ):
                        self.invalid_reasons.append(reason_token)
                    self.state = "failed"
                    self.finished_at = time.time()
                    self._terminalize_v2_phase_locked("failed")
                    self._write_manifest()
                    self.condition.notify_all()
                    should_terminate = True
                    should_stop = True
        if control_to_close is not None:
            control_to_close.close()
        if should_recover is not None:
            self._start_v2_boundary_recovery(place_number, should_recover)
            return True
        if should_defer:
            threading.Thread(
                target=self._finish_sidecar_exit_grace,
                args=(place_number, generation, clean),
                name=(
                    f"freeciv-agent-exit-grace-{self.game_id}-"
                    f"{place_number}-{generation}"
                ),
                daemon=True,
            ).start()
            return False
        if should_stop:
            self._stop_all_sidecars()
        if should_terminate:
            self._terminate_child()
        return False

    def _finish_sidecar_exit_grace(
        self, place_number: int, generation: int, health: dict[str, Any],
    ) -> None:
        deadline = time.monotonic() + V2_SIDECAR_COMPLETION_GRACE_S
        while True:
            with self.condition:
                if (
                    self.sidecar_exit_grace_generations.get(place_number)
                    != generation
                ):
                    return
                if (
                    self.sidecars_stopping or self.cancel_requested
                    or self.state in TERMINAL_STATES or self.server_exit_observed
                    or self.sidecar_generations.get(place_number) != generation
                ):
                    self.sidecar_exit_grace_generations.pop(place_number, None)
                    self.condition.notify_all()
                    return
                process = self.process
                try:
                    server_returncode = (
                        process.poll() if process is not None else None
                    )
                except Exception:
                    server_returncode = None
                if process is not None and server_returncode is not None:
                    self.sidecar_exit_grace_generations.pop(place_number, None)
                    self.condition.notify_all()
                    return
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            if self.supervisor.shutdown_event.wait(min(remaining, 0.05)):
                with self.condition:
                    if (
                        self.sidecar_exit_grace_generations.get(place_number)
                        == generation
                    ):
                        self.sidecar_exit_grace_generations.pop(
                            place_number, None,
                        )
                        self.condition.notify_all()
                return
        self._on_sidecar_exit(
            place_number, generation, health, after_completion_grace=True,
        )

    def _stop_all_sidecars(self) -> None:
        with self.condition:
            if self.config["control_protocol"] != FULL_CONTROL_V2:
                return
            self.sidecars_stopping = True
            sidecars = tuple(self.sidecars.items())
            controls = tuple(self.v2_controls.values())
            self.v2_controls.clear()
            self.v2_execution_locks.clear()
            self.condition.notify_all()
        for control in controls:
            control.close()
        for place_number, sidecar in sidecars:
            try:
                sidecar.stop()
            except Exception:
                pass
            clean = self._sanitized_sidecar_health(
                sidecar, self.sidecar_generations.get(place_number, 0),
            )
            with self.condition:
                self.sidecar_health[place_number] = clean
                self.condition.notify_all()

    @staticmethod
    def _parse_sidecar_status(message: str) -> dict[str, str]:
        if not message.startswith("STATUS\t"):
            return {}
        fields: dict[str, str] = {}
        for item in message.split("\t")[1:]:
            if "=" not in item:
                continue
            name, value = item.split("=", 1)
            if name in {
                "state", "server", "seat", "player", "lifecycle",
            } and value:
                fields[name] = value
        return fields

    def _record_v2_native_identity_locked(
        self,
        place: Place,
        generation: int,
        fields: dict[str, str],
    ) -> None:
        """Verify and retain one STATUS identity without publishing it."""
        if "player" not in fields and "lifecycle" not in fields:
            # Compatibility for injected/older test sidecars. The bundled
            # native sidecar always supplies both fields.
            return
        try:
            player_number = int(fields.get("player", ""), 10)
            lifecycle = int(fields.get("lifecycle", ""), 10)
        except (TypeError, ValueError):
            raise SidecarError("protocol_error") from None
        if (
            fields.get("seat") != "ready"
            or player_number != place.native_player_number
            or lifecycle <= 0
        ):
            raise SidecarError("wrong_player")
        current = self.v2_native_player_identities.get(place.number)
        identity = (generation, player_number, lifecycle)
        if current is not None and current != identity:
            raise SidecarError("wrong_player")
        self.v2_native_player_identities[place.number] = identity

    def _place_identity_indexes_locked(
        self,
    ) -> tuple[
        dict[str, tuple[Place, dict[str, Any]]],
        dict[int, tuple[Place, dict[str, Any]]],
    ]:
        by_name: dict[str, tuple[Place, dict[str, Any]]] = {}
        by_player: dict[int, tuple[Place, dict[str, Any]]] = {}
        for place in self.places:
            value = (place, self._place_identity(place))
            by_name[place.player_name] = value
            identity = self.v2_native_player_identities.get(place.number)
            if (
                identity is not None
                and identity[0] == self.sidecar_generations.get(place.number)
            ):
                by_player[identity[1]] = value
            elif not place.joinable:
                by_player[place.native_player_number] = value
        return by_name, by_player

    def _v2_runtime_active_locked(self) -> bool:
        """Exact cleanup gate for a still-running full-control-v2 game."""
        return bool(
            self.config["control_protocol"] == FULL_CONTROL_V2
            and self.state == "running"
            and not self.cancel_requested
            and not self.sidecars_stopping
            and not self.server_exit_observed
        )

    def _v2_control_active_locked(self) -> bool:
        """Whether exact v2 state/action transport may serve this game."""
        return bool(
            self.config["control_protocol"] == FULL_CONTROL_V2
            and self.state in {"lobby", "starting", "running"}
            and not self.cancel_requested
            and not self.sidecars_stopping
            and not self.server_exit_observed
        )

    def _v2_seat_runtime_active_locked(
        self,
        place: int,
        generation: int,
        sidecar: Any,
        *,
        agent_id: str | None = None,
        control: V2SeatControl | None = None,
    ) -> bool:
        """Return the single exact predicate for a usable live v2 seat."""
        health = self.sidecar_health.get(place, {})
        current_health = self._sanitized_sidecar_health(sidecar, generation)
        expected_client_states = {
            "lobby": {"preparing"},
            "starting": {"preparing", "running"},
            "running": {"running"},
        }.get(self.state, set())
        return bool(
            self._v2_control_active_locked()
            # A wedged boundary keeps every component reporting itself healthy,
            # so it has to be excluded here explicitly.  Closing the one gate
            # every v2 path shares turns an unattributable internal error into
            # an honest retryable "seat unavailable" for reads, dispatches and
            # health alike, and keeps it closed until recovery republishes the
            # seat on a new generation.
            and place not in self.v2_wedged_places
            and self.sidecars.get(place) is sidecar
            and self.sidecar_generations.get(place) == generation
            and self.sidecar_ready_generations.get(place) == generation
            and self.sidecar_exit_grace_generations.get(place) != generation
            and health.get("generation") == generation
            and health.get("state") == "ready"
            and health.get("client_state") in expected_client_states
            and health.get("server_connected") is True
            and health.get("seat_state") == "ready"
            and current_health.get("state") == "ready"
            and current_health.get("client_state") in expected_client_states
            and current_health.get("server_connected") is True
            and current_health.get("seat_state") == "ready"
            and (
                agent_id is None
                or self.place_agents.get(place) == agent_id
                and self.agents.get(agent_id, {}).get("place") == place
            )
            and (
                control is None
                or self.v2_controls.get(place) is control
                and control.agent_id == agent_id
                and control.generation == generation
            )
        )

    @staticmethod
    def _v2_phase_receipt_final(receipt_state: Any) -> bool:
        return receipt_state in {"applied", "ambiguous", "rejected"}

    def _run_v2_failure_cleanup(self) -> None:
        self._stop_all_sidecars()
        self._terminate_child()

    def _invalidate_v2_phase_event_journal_locked(self) -> None:
        """Fail closed once without trying to write another phase event."""
        if self.v2_phase_event_journal_failed:
            return
        self.v2_phase_event_journal_failed = True
        reason = "v2_phase_event_journal_unavailable"
        if reason not in self.invalid_reasons:
            self.invalid_reasons.append(reason)
        self.error = "full-control-v2 phase-end provenance could not be persisted"
        self.state = "failed"
        self.finished_at = time.time()
        ledger = self.v2_phase_ledger
        ledger["state"] = "failed"
        ledger["evidence"] = {}
        ledger["active_place"] = None
        ledger["deadline_started_monotonic"] = None
        ledger["deadline_started_at"] = None
        ledger["synchronizing_started_monotonic"] = None
        ledger["progress_marker"] = None
        ledger["progress_started_monotonic"] = None
        ledger["end"] = None
        self.v2_pending_phase_ends.clear()
        try:
            self._write_manifest()
        except Exception:
            pass
        self.condition.notify_all()
        if not self.v2_failure_cleanup_started:
            self.v2_failure_cleanup_started = True
            threading.Thread(
                target=self._run_v2_failure_cleanup,
                name=f"freeciv-agent-phase-journal-failure-{self.game_id}",
                daemon=True,
            ).start()

    def _finalize_v2_phase_end_locked(
        self, claim: dict[str, Any], resolution: str,
    ) -> bool:
        """Queue one resolved claim and journal resolved phases in native order."""
        if claim.get("journaled") is True:
            return True
        claim["resolution"] = resolution
        claim_id = claim.get("claim_id")
        if isinstance(claim_id, str):
            self.v2_pending_phase_ends[claim_id] = claim
        else:
            self._invalidate_v2_phase_event_journal_locked()
            return False

        # Native phase resolution is observed in order, but its durable batch
        # receipt can finish later on another request thread.  Never let a
        # later finalized receipt overtake an earlier unresolved claim.
        queued = sorted(
            self.v2_pending_phase_ends.values(),
            key=lambda item: (
                item.get("key")
                if isinstance(item.get("key"), tuple)
                and len(item["key"]) == 2
                else (1 << 63, 1 << 63)
            ),
        )
        for pending in queued:
            if not self._v2_phase_receipt_final(pending.get("receipt_state")):
                break
            if not self._append_v2_phase_event_locked(pending):
                return False
        return claim.get("journaled") is True

    def _append_v2_phase_event_locked(self, claim: dict[str, Any]) -> bool:
        """Append one already ordered, resolved, receipt-final claim."""
        if claim.get("journaled") is True:
            return True
        journal = self.v2_phase_event_journal
        key = claim.get("key")
        place_number = claim.get("place")
        if (
            journal is None
            or not isinstance(key, tuple) or len(key) != 2
            or type(key[0]) is not int or type(key[1]) is not int
            or type(place_number) is not int
            or not 1 <= place_number <= len(self.places)
        ):
            self._invalidate_v2_phase_event_journal_locked()
            return False
        place = self.places[place_number - 1]
        agent_id = self.place_agents.get(place_number)
        agent = self.agents.get(agent_id) if agent_id is not None else None
        source = claim.get("source")
        receipt_state = claim.get("receipt_state")
        resolution = claim.get("resolution")
        if (
            agent is None
            or source not in {"agent", "timeout"}
            or not self._v2_phase_receipt_final(receipt_state)
            or resolution not in {"advanced", "terminal", "failed"}
            or receipt_state == "rejected" and resolution != "failed"
        ):
            self._invalidate_v2_phase_event_journal_locked()
            return False
        ended_at = time.time()
        ended_monotonic = time.monotonic()
        deadline_started_at = claim.get("deadline_started_at")
        deadline_started_monotonic = claim.get("deadline_started_monotonic")
        if (
            isinstance(deadline_started_at, bool)
            or not isinstance(deadline_started_at, (int, float))
            or not math.isfinite(deadline_started_at)
            or deadline_started_at < 0
            or deadline_started_at > ended_at
            or isinstance(deadline_started_monotonic, bool)
            or not isinstance(deadline_started_monotonic, (int, float))
            or not math.isfinite(deadline_started_monotonic)
            or deadline_started_monotonic < 0
            or deadline_started_monotonic > ended_monotonic
        ):
            self._invalidate_v2_phase_event_journal_locked()
            return False
        event = {
            "turn": key[0],
            "phase": key[1],
            "place": place_number,
            "seat_id": place.seat_id,
            "player_name": place.player_name,
            "player_color": place.player_color,
            "controller_label": agent["controller_label"],
            "controller_type": "external",
            "source": source,
            "receipt_state": receipt_state,
            "resolution": resolution,
            "deadline_started_at": float(deadline_started_at),
            "ended_at": ended_at,
            "elapsed_s": max(
                0.0, ended_monotonic - float(deadline_started_monotonic),
            ),
        }
        try:
            journal.append(event)
        except V2PhaseEventJournalError:
            self._invalidate_v2_phase_event_journal_locked()
            return False
        claim["journaled"] = True
        claim_id = claim.get("claim_id")
        if isinstance(claim_id, str):
            self.v2_pending_phase_ends.pop(claim_id, None)
        return True

    def _terminalize_v2_phase_locked(self, state: str) -> None:
        """Clear actionable phase state while retaining the last consensus key."""
        if self.config["control_protocol"] != FULL_CONTROL_V2:
            return
        ledger = self.v2_phase_ledger
        if state == "terminalizing":
            # The monitor has observed native exit but has not yet classified
            # it as completed/invalid/cancelled versus failed. Preserve an
            # in-flight end claim until that authoritative classification.
            ledger["state"] = state
            ledger["evidence"] = {}
            ledger["active_place"] = None
            ledger["synchronizing_started_monotonic"] = None
            ledger["progress_marker"] = None
            ledger["progress_started_monotonic"] = None
            return
        end = ledger.get("end")
        resolution = "failed" if state == "failed" else "terminal"
        if isinstance(end, dict):
            self._finalize_v2_phase_end_locked(end, resolution)
        if self.v2_phase_event_journal_failed:
            state = "failed"
        ledger["state"] = state
        ledger["evidence"] = {}
        ledger["active_place"] = None
        ledger["deadline_started_monotonic"] = None
        ledger["deadline_started_at"] = None
        ledger["synchronizing_started_monotonic"] = None
        ledger["progress_marker"] = None
        ledger["progress_started_monotonic"] = None
        ledger["end"] = None

    def _v2_rewind_phase_ledger_locked(self, target_turn: int) -> None:
        """Move phase consensus back with a game that has been rolled back.

        The ledger's key is the last turn and phase every seat agreed on, and
        every later sample is checked against it: evidence that goes backwards
        is corruption and fails the game.  An autosave rollback makes the game
        itself go backwards, on purpose, so unless the ledger is rewound with
        it the first honest sample from the reloaded server is read as a phase
        regression and ends the game recovery just saved.

        Safe only here, in the window where the seat is detached and the
        server has already been replaced: no sidecar is registered, so no
        evidence can be sampled against a half-rewound ledger.  The deadline
        and stall clocks are discarded for the same reason -- they were
        measuring a phase that no longer exists -- and the in-flight end claim
        with them, because the phase it would have ended was rewound away and
        the recovery journal, not a phase event, is the record of that.

        Everything else that describes a rewound turn goes with it.  A
        monotone per-seat fact recorded in a turn the game is about to replay
        is not history any more, it is a claim about a turn that no longer
        happened, and leaving it behind deadlocks the seat the rollback just
        rescued: a stale surrender keeps the seat parked in ``inactive_done``
        until the progress-stall clock ends the game.
        """
        journal = self.v2_phase_event_journal
        if journal is not None and not self.v2_phase_event_journal_failed:
            # The replayed phases have to be journaled under a fresh identity.
            # Without this the first replayed phase end contradicts the record
            # of the phase it replaces and fails the game the rollback saved.
            try:
                self.v2_incarnation = journal.begin_incarnation()
            except V2PhaseEventJournalError:
                self._invalidate_v2_phase_event_journal_locked()
        else:
            self.v2_incarnation += 1
        # Receipt identity has to move with it.  `just retry --batch_id ID` is
        # the sanctioned response to an unresolved command, and an unresolved
        # command is exactly what a wedge produces right before a rollback, so
        # without this the retry is answered from a pre-rollback receipt and
        # never dispatched against the reloaded server.
        store = self.v2_receipt_store
        if store is not None and not self.v2_receipt_store_failed:
            try:
                store.begin_incarnation()
            except V2ReceiptStoreError:
                self.v2_receipt_store_failed = True
        # A surrender inside a rewound turn did not happen in the game that is
        # now running.
        self.v2_surrendered_places.clear()
        self.v2_applied_turns = {
            place: applied
            for place, applied in self.v2_applied_turns.items()
            if applied < target_turn
        }
        # An end claim that never became receipt-final blocks the sorted
        # prefix loop forever, silently stopping all later journaling.  The
        # phase it would have ended was rewound away with everything else.
        self.v2_pending_phase_ends.clear()
        ledger = self.v2_phase_ledger
        ledger["key"] = (target_turn, 0)
        ledger["evidence"] = {}
        ledger["active_place"] = None
        ledger["state"] = "synchronizing"
        ledger["synchronizing_started_monotonic"] = time.monotonic()
        ledger["deadline_started_monotonic"] = None
        ledger["deadline_started_at"] = None
        ledger["progress_marker"] = None
        ledger["progress_started_monotonic"] = None
        ledger["end"] = None
        self.condition.notify_all()

    def _v2_consensus_turn_locked(self) -> int | None:
        key = self.v2_phase_ledger.get("key")
        return key[0] if isinstance(key, tuple) and len(key) == 2 else None

    def _current_turn_locked(self) -> int | None:
        if self.config["control_protocol"] == FULL_CONTROL_V2:
            return self._v2_consensus_turn_locked()
        return (
            self.current_turn["turn"] if self.current_turn is not None
            else (self.latest_turn["turn"] if self.latest_turn else None)
        )

    def _v2_evaluation_context_locked(self) -> dict[str, Any]:
        """Return game-scoped evaluation context without native identifiers."""
        current_turn = self._current_turn_locked()
        max_turns = self.config["turns"]
        return {
            "objective": self.config["objective"],
            "max_turns": max_turns,
            "turns_remaining": (
                None
                if current_turn is None
                else max(0, max_turns - current_turn)
            ),
        }

    def _collect_v2_phase_evidence(
        self, place_number: int, generation: int, sidecar: Any,
    ) -> dict[str, Any] | None:
        """Copy one command-free sidecar phase fact outside the condition."""
        with self.condition:
            agent_id = self.place_agents.get(place_number)
            agent = self.agents.get(agent_id) if agent_id is not None else None
            control = self.v2_controls.get(place_number)
            current = bool(
                agent_id is not None and agent is not None and control is not None
                and self._v2_seat_runtime_active_locked(
                    place_number, generation, sidecar,
                    agent_id=agent_id, control=control,
                )
            )
        if not current or control is None or agent is None or agent_id is None:
            return None
        try:
            raw = sidecar.phase_evidence()
            if raw is None:
                return None
            phase = dict(raw)
            if phase.get("generation") != generation:
                return None
            evidence = {
                "place": place_number,
                "generation": generation,
                "sidecar": sidecar,
                "control": control,
                "agent_id": agent_id,
                "controller_label": agent["controller_label"],
                "seat_local_revision": phase["revision"],
                "turn": phase["turn"],
                "phase": phase["phase"],
                "mode": phase["mode"],
                "count": phase["phase_count"],
                "active": phase["active"],
                "ready": phase["ready"],
                "alive": phase["alive"],
                "done": phase["done"],
            }
        except Exception:
            return None
        with self.condition:
            if not self._v2_phase_context_current_locked(evidence):
                return None
        return evidence

    def _v2_phase_context_current_locked(
        self, evidence: dict[str, Any],
    ) -> bool:
        place = evidence["place"]
        generation = evidence["generation"]
        return bool(
            self._v2_seat_runtime_active_locked(
                place, generation, evidence["sidecar"],
                agent_id=evidence["agent_id"], control=evidence["control"],
            )
        )

    def _fail_v2_phase_locked(self, reason: str, message: str) -> None:
        if reason not in self.invalid_reasons:
            self.invalid_reasons.append(reason)
        self.error = message
        self.state = "failed"
        self.finished_at = time.time()
        self._terminalize_v2_phase_locked("failed")
        self._write_manifest()
        self.condition.notify_all()

    def _v2_phase_progress_stalled_locked(
        self,
        *,
        key: tuple[int, int],
        state: str,
        active_place: int | None,
        now: float,
    ) -> bool:
        """Track coherent native progress independently of model deadlines."""
        ledger = self.v2_phase_ledger
        marker = (key, state, active_place)
        if ledger.get("progress_marker") != marker:
            ledger["progress_marker"] = marker
            ledger["progress_started_monotonic"] = now
            return False
        if state not in {"native_phase", "phase_not_ready", "inactive_done"}:
            return False
        started = ledger.get("progress_started_monotonic")
        return bool(
            started is not None
            and now - started >= V2_PHASE_PROGRESS_STALL_S
        )

    def _set_v2_phase_wait_state_locked(
        self,
        *,
        key: tuple[int, int],
        state: str,
        active_place: int | None,
        now: float,
    ) -> bool:
        ledger = self.v2_phase_ledger
        ledger["state"] = state
        if self._v2_phase_progress_stalled_locked(
            key=key, state=state, active_place=active_place, now=now,
        ):
            self._fail_v2_phase_locked(
                "v2_phase_progress_stalled",
                "full-control-v2 native phase made no forward progress",
            )
            return True
        return False

    def _update_v2_phase_ledger(
        self, evidence_rows: list[dict[str, Any]], now: float,
    ) -> tuple[dict[str, Any] | None, bool]:
        """Apply one consensus sample under the condition and claim timeout once."""
        with self.condition:
            if not self._v2_runtime_active_locked():
                return None, False
            current = {
                row["place"]: row for row in evidence_rows
                if self._v2_phase_context_current_locked(row)
            }
            expected = {place.number for place in self.joinable_places}
            ledger = self.v2_phase_ledger
            ledger["evidence"] = current
            end = ledger.get("end")
            if self.v2_recovery_in_flight or self.v2_wedged_places:
                # Recovery tears a seat down and republishes it on a new
                # generation, so missing and unsynchronized evidence is
                # expected for as long as it runs.  Hold both stall clocks at
                # this sample rather than merely skipping the decision, or
                # they fire the instant recovery finishes.  A recovery that
                # never finishes is still bounded, by its own attempt caps.
                # A wedged seat counts even before its recovery thread has
                # registered itself: detection and registration are two steps,
                # and a poll landing between them must not start the clock
                # running against a seat that is already known to be gone.
                ledger["synchronizing_started_monotonic"] = now
                if end is not None and end.get(
                    "reconcile_started_monotonic",
                ) is not None:
                    end["reconcile_started_monotonic"] = now
                # The progress clock is the third one, and it is held for the
                # same reason: a re-attach that keeps the turn and phase it
                # started from lands back on the marker it left, so without
                # this the whole detection-and-recovery window is charged to a
                # boundary that was being rebuilt and could not make progress
                # by construction.
                if ledger.get("progress_started_monotonic") is not None:
                    ledger["progress_started_monotonic"] = now
            reconcile_started = (
                end.get("reconcile_started_monotonic")
                if end is not None else None
            )
            reconciliation_stalled = bool(
                reconcile_started is not None
                and now - reconcile_started >= V2_PHASE_RECONCILE_STALL_S
            )
            if set(current) != expected:
                if reconciliation_stalled:
                    self._fail_v2_phase_locked(
                        "v2_phase_reconciliation_stalled",
                        "full-control-v2 phase end did not reconcile",
                    )
                    return None, True
                if end is None:
                    started = ledger.get("synchronizing_started_monotonic")
                    if started is None:
                        ledger["synchronizing_started_monotonic"] = now
                    elif now - started >= V2_PHASE_SYNCHRONIZE_STALL_S:
                        self._fail_v2_phase_locked(
                            "v2_phase_synchronization_stalled",
                            "full-control-v2 phase evidence did not synchronize",
                        )
                        return None, True
                else:
                    ledger["synchronizing_started_monotonic"] = None
                ledger["active_place"] = None
                ledger["state"] = "synchronizing"
                return None, False

            consensus = {
                (row["turn"], row["phase"], row["mode"])
                for row in current.values()
            }
            if len(consensus) != 1:
                if reconciliation_stalled:
                    self._fail_v2_phase_locked(
                        "v2_phase_reconciliation_stalled",
                        "full-control-v2 phase end did not reconcile",
                    )
                    return None, True
                if end is None:
                    started = ledger.get("synchronizing_started_monotonic")
                    if started is None:
                        ledger["synchronizing_started_monotonic"] = now
                    elif now - started >= V2_PHASE_SYNCHRONIZE_STALL_S:
                        self._fail_v2_phase_locked(
                            "v2_phase_synchronization_stalled",
                            "full-control-v2 phase evidence did not synchronize",
                        )
                        return None, True
                else:
                    ledger["synchronizing_started_monotonic"] = None
                # Phase packets arrive independently per sidecar.  A mixed
                # global tuple, including two active flags from different
                # epochs, is normal transition skew rather than corruption.
                ledger["active_place"] = None
                ledger["state"] = "synchronizing"
                return None, False
            active = [row for row in current.values() if row["active"]]
            if (
                next(iter(consensus))[2] != "players_alternate"
                or len(active) > 1
            ):
                self._fail_v2_phase_locked(
                    "v2_phase_protocol",
                    "full-control-v2 phase evidence was inconsistent",
                )
                return None, True
            turn, phase, _mode = next(iter(consensus))
            key = (turn, phase)
            counts = sorted({row["count"] for row in current.values()})
            ledger["reported_phase_counts"] = counts
            previous = ledger.get("key")
            if previous is not None:
                if turn < previous[0] or (
                    turn == previous[0] and phase < previous[1]
                ):
                    self._fail_v2_phase_locked(
                        "v2_phase_regression",
                        "full-control-v2 phase evidence regressed",
                    )
                    return None, True
            ledger["synchronizing_started_monotonic"] = None
            if previous != key:
                previous_end = ledger.get("end")
                if previous is not None and isinstance(previous_end, dict):
                    self._finalize_v2_phase_end_locked(
                        previous_end, "advanced",
                    )
                    if self.v2_phase_event_journal_failed:
                        return None, True
                ledger["key"] = key
                ledger["deadline_started_monotonic"] = None
                ledger["deadline_started_at"] = None
                ledger["progress_marker"] = None
                ledger["progress_started_monotonic"] = None
                ledger["end"] = None

            active_row = active[0] if active else None
            ledger["active_place"] = (
                active_row["place"] if active_row is not None else None
            )
            end = ledger.get("end")
            if end is not None:
                if reconciliation_stalled:
                    self._fail_v2_phase_locked(
                        "v2_phase_reconciliation_stalled",
                        "full-control-v2 phase end did not reconcile",
                    )
                    return None, True
                ledger["state"] = (
                    "ambiguous_ending"
                    if end.get("receipt_state") == "ambiguous" else "ending"
                )
                return None, False
            if active_row is None:
                failed = self._set_v2_phase_wait_state_locked(
                    key=key, state="native_phase", active_place=None, now=now,
                )
                return None, failed
            if (
                not active_row["alive"]
                or active_row["done"]
                # A seat that resigned cannot become ready again, so reporting
                # it as merely "not ready" invites a control loop to keep
                # waiting for a readiness that will never arrive. It is
                # inactive, and the progress-stall guard still applies.
                or active_row["place"] in self.v2_surrendered_places
            ):
                failed = self._set_v2_phase_wait_state_locked(
                    key=key, state="inactive_done",
                    active_place=active_row["place"], now=now,
                )
                return None, failed
            if not active_row["ready"]:
                failed = self._set_v2_phase_wait_state_locked(
                    key=key, state="phase_not_ready",
                    active_place=active_row["place"], now=now,
                )
                return None, failed

            ledger["state"] = "awaiting_agent"
            self._v2_phase_progress_stalled_locked(
                key=key, state="awaiting_agent",
                active_place=active_row["place"], now=now,
            )
            timeout = self.config["action_timeout_s"]
            if ledger["deadline_started_monotonic"] is None:
                ledger["deadline_started_monotonic"] = now
                ledger["deadline_started_at"] = time.time()
            if (
                timeout is None
                or now < ledger["deadline_started_monotonic"] + timeout
            ):
                return None, False

            claim = {
                "claim_id": secrets.token_urlsafe(18),
                "key": key,
                "place": active_row["place"],
                "agent_id": active_row["agent_id"],
                "generation": active_row["generation"],
                "source": "timeout",
                "receipt_state": "claiming",
                "claimed_monotonic": now,
                "deadline_started_monotonic": ledger[
                    "deadline_started_monotonic"
                ],
                "deadline_started_at": ledger["deadline_started_at"],
                "reconcile_started_monotonic": None,
                "batch_id": (
                    f"timeout.t{turn}.p{phase}."
                    f"seat{active_row['place']}.g{active_row['generation']}"
                ),
            }
            ledger["end"] = claim
            ledger["state"] = "ending"
            return dict(claim), False

    def _phase_end_claim_for_action(
        self, place: int, generation: int, batch_id: str,
        resolution: Any, overview: dict[str, Any],
        internal_claim: dict[str, Any] | None,
    ) -> dict[str, Any]:
        key = (resolution.turn, resolution.phase)
        with self.condition:
            ledger = self.v2_phase_ledger
            current_key = ledger.get("key")
            if current_key != key:
                raise self._v2_problem(
                    HTTPStatus.CONFLICT, "stale_revision",
                    "the requested phase is no longer current", retryable=True,
                )
            existing = ledger.get("end")
            if internal_claim is not None:
                if (
                    existing is None
                    or existing.get("claim_id") != internal_claim.get("claim_id")
                    or existing.get("key") != key
                    or existing.get("place") != place
                    or existing.get("generation") != generation
                ):
                    raise self._v2_unavailable()
                return existing
            evidence = ledger.get("evidence", {}).get(place)
            if (
                ledger.get("state") != "awaiting_agent"
                or ledger.get("active_place") != place
                or evidence is None
                or evidence.get("generation") != generation
                or evidence.get("agent_id") != self.place_agents.get(place)
                or evidence.get("turn") != key[0]
                or evidence.get("phase") != key[1]
                or not evidence.get("active")
                or not evidence.get("ready")
                or not evidence.get("alive")
                or evidence.get("done")
            ):
                raise self._v2_problem(
                    HTTPStatus.CONFLICT, "stale_revision",
                    "the active phase consensus is not actionable",
                    retryable=True,
                )
            if existing is not None and existing.get("receipt_state") != "rejected":
                raise self._v2_problem(
                    HTTPStatus.TOO_MANY_REQUESTS, "rate_limited",
                    "a phase end is already in progress", retryable=True,
                )
            claim = {
                "claim_id": secrets.token_urlsafe(18),
                "key": key,
                "place": place,
                "agent_id": self.place_agents.get(place),
                "generation": generation,
                "source": "agent",
                "receipt_state": "claiming",
                "claimed_monotonic": time.monotonic(),
                "deadline_started_monotonic": ledger[
                    "deadline_started_monotonic"
                ],
                "deadline_started_at": ledger["deadline_started_at"],
                "reconcile_started_monotonic": None,
                "batch_id": batch_id,
            }
            ledger["key"] = key
            ledger["active_place"] = place
            ledger["end"] = claim
            ledger["state"] = "ending"
            return claim

    def _note_phase_end_receipt(
        self, claim: dict[str, Any] | None, receipt_state: str,
    ) -> None:
        if claim is None:
            return
        with self.condition:
            current = self.v2_phase_ledger.get("end")
            claim_id = claim.get("claim_id")
            pending = (
                self.v2_pending_phase_ends.get(claim_id)
                if isinstance(claim_id, str) else None
            )
            current_matches = bool(
                isinstance(current, dict)
                and current.get("claim_id") == claim_id
                and self.v2_phase_ledger.get("key") == claim.get("key")
            )
            target = current if current_matches else pending
            if target is None or self.v2_phase_event_journal_failed:
                return
            target["receipt_state"] = receipt_state
            if (
                receipt_state in {
                    "accepted", "applied", "ambiguous", "rejected",
                }
                and target.get("reconcile_started_monotonic") is None
            ):
                target["reconcile_started_monotonic"] = time.monotonic()
            resolution = target.get("resolution")
            if isinstance(resolution, str):
                self._finalize_v2_phase_end_locked(target, resolution)
            if current_matches and not self.v2_phase_event_journal_failed:
                self.v2_phase_ledger["state"] = (
                    "ambiguous_ending"
                    if receipt_state == "ambiguous" else "ending"
                )
            self.condition.notify_all()

    def _release_phase_end_claim(self, claim: dict[str, Any] | None) -> None:
        """Release one provably unaccepted claim without resetting its clock."""
        if claim is None:
            return
        with self.condition:
            current = self.v2_phase_ledger.get("end")
            if (
                not self._v2_runtime_active_locked()
                or current is None
                or current.get("claim_id") != claim.get("claim_id")
                or self.v2_phase_ledger.get("key") != claim.get("key")
            ):
                return
            self.v2_phase_ledger["end"] = None
            self.v2_phase_ledger["state"] = "awaiting_agent"
            self.condition.notify_all()

    def _fail_phase_end_durability(
        self, claim: dict[str, Any] | None,
    ) -> bool:
        """Fail one exact pre-send claim when receipt durability is unsafe."""
        if claim is None:
            return False
        with self.condition:
            current = self.v2_phase_ledger.get("end")
            if (
                not self._v2_runtime_active_locked()
                or current is None
                or current.get("claim_id") != claim.get("claim_id")
                or self.v2_phase_ledger.get("key") != claim.get("key")
            ):
                return False
            self._fail_v2_phase_locked(
                "v2_phase_receipt_unavailable",
                "full-control-v2 could not durably reserve the phase end",
            )
            return True

    def _handle_rejected_phase_end(
        self, claim: dict[str, Any] | None,
    ) -> bool:
        """Release agent rejection or fail a timed-out phase deterministically."""
        if claim is None:
            return False
        with self.condition:
            current = self.v2_phase_ledger.get("end")
            if (
                not self._v2_runtime_active_locked()
                or current is None
                or current.get("claim_id") != claim.get("claim_id")
                or self.v2_phase_ledger.get("key") != claim.get("key")
            ):
                return False
            if current.get("source") == "agent":
                self.v2_phase_ledger["end"] = None
                self.v2_phase_ledger["state"] = "awaiting_agent"
                self.condition.notify_all()
                return False
            self._fail_v2_phase_locked(
                "v2_phase_timeout_rejected",
                "full-control-v2 timed phase end was definitively rejected",
            )
            return True

    @staticmethod
    def _v2_phase_end_batch_from_observation(
        game_id: str, claim: dict[str, Any], control: V2SeatControl,
        observation: dict[str, Any],
    ) -> dict[str, Any]:
        """Select the current public phase.end capability, following pages."""
        page = control.legal_actions_page(observation, MAX_PAGE_ITEMS)
        while True:
            for action in page["page"]["items"]:
                if action.get("kind") == "phase.end":
                    return {
                        "schema_version": 2,
                        "control_protocol": FULL_CONTROL_V2,
                        "game_id": game_id,
                        "agent_id": claim["agent_id"],
                        "batch_id": claim["batch_id"],
                        "state_revision": action["state_revision"],
                        "commands": [{
                            "action_id": action["action_id"],
                            "arguments": {},
                        }],
                    }
            cursor = page["page"].get("next_cursor")
            if cursor is None:
                raise V2ControlError("action_expired")
            page = control.continue_page(cursor, endpoint="legal_actions")

    def _run_v2_timeout_phase_end(self, claim: dict[str, Any]) -> None:
        try:
            self._begin_v2_receipt_operation()
            try:
                self._v2_submit_batch_active(
                    claim["agent_id"], None, internal_phase_claim=claim,
                )
            finally:
                self._end_v2_receipt_operation()
        except Exception:
            self._note_phase_end_receipt(claim, "rejected")
            failed = False
            with self.condition:
                current = self.v2_phase_ledger.get("end")
                if (
                    self._v2_runtime_active_locked()
                    and current is not None
                    and current.get("claim_id") == claim.get("claim_id")
                    and self.v2_phase_ledger.get("key") == claim.get("key")
                ):
                    self._fail_v2_phase_locked(
                        "v2_phase_timeout_failed",
                        "full-control-v2 timed phase end could not be dispatched",
                    )
                    failed = True
            if failed:
                self._stop_all_sidecars()
                self._terminate_child()

    def _start_v2_timeout_phase_end(self, claim: dict[str, Any]) -> None:
        threading.Thread(
            target=self._run_v2_timeout_phase_end,
            args=(claim,),
            name=(
                f"freeciv-agent-phase-timeout-{self.game_id}-"
                f"{claim['place']}-{claim['generation']}"
            ),
            daemon=True,
        ).start()

    def _v2_clear_liveness_misses(self, place_number: int) -> None:
        """Any successful sample proves the client was never gone."""
        with self.condition:
            self.v2_liveness_misses.pop(place_number, None)

    def _v2_liveness_probe_may_yield(
        self, place_number: int, generation: int, now: float,
    ) -> bool:
        """Whether the agent's own traffic already answered this probe.

        The poller and the agent share one command stream per seat, and the
        poller used to walk into it every 250 ms whether or not the seat was
        busy playing.  A command the agent completed proves precisely what a
        STATUS would have asked -- the client is up, connected and answering --
        so during active play the probe is a duplicate that costs the agent
        contention.

        Yielding is bounded three ways over.  It lasts only while the agent's
        proof is fresh, and never past a hard ceiling on the gap between real
        samples, because a skipped probe observes nothing: seat loss, a client
        reporting OVER and a wedged boundary are all only ever seen by an
        actual probe, and continuous agent traffic must not be able to hide
        them.  And it happens only while the phase ledger is quietly waiting
        on the agent to act.  The moment the boundary owes a transition --
        an end that has not reconciled, evidence still being synchronized --
        the poll stops being a duplicate of the agent's traffic and becomes
        the only thing watching the turn change, which is exactly where a
        client is most likely to die.  That window is short and rare compared
        with the body of a turn, where this yield does its work.

        A skipped probe is not a failed one: it counts toward no miss total
        and clears none, so wedge detection sees exactly the sequence of real
        samples it would have seen anyway, just sparser.
        """
        with self.condition:
            if (
                self.v2_phase_ledger.get("end") is not None
                or self.v2_phase_ledger.get("state") != "awaiting_agent"
            ):
                return False
            command = self.v2_last_agent_command.get(place_number)
            probe = self.v2_last_liveness_probe.get(place_number)
        if command is None or command[1] != generation:
            return False
        if now - command[0] > V2_LIVENESS_AGENT_ACTIVITY_YIELD_S:
            return False
        if probe is None or probe[1] != generation:
            # This generation has never been sampled directly.  Nothing may
            # stand in for its first real probe.
            return False
        return now - probe[0] < V2_LIVENESS_MAX_PROBE_GAP_S

    def _v2_note_liveness_probe(
        self, place_number: int, generation: int, now: float,
    ) -> None:
        with self.condition:
            self.v2_last_liveness_probe[place_number] = (now, generation)

    def _v2_note_liveness_miss(self, place_number: int, sidecar: Any) -> bool:
        """Count one unanswered liveness poll; true while it stays 'slow'.

        Returns false only once a timeout has stopped being explainable as
        latency: several consecutive misses, spanning real time, with no
        evidence the process is still running.  A live process is always
        'slow', never 'gone', no matter how many samples it drops -- the seat
        it owns cannot be retaken from underneath it anyway, and killing it is
        strictly worse than waiting.
        """
        now = time.monotonic()
        with self.condition:
            first, count = self.v2_liveness_misses.get(place_number, (now, 0))
            count += 1
            self.v2_liveness_misses[place_number] = (first, count)
            elapsed = now - first
        if count < V2_LIVENESS_MISS_THRESHOLD or elapsed < V2_LIVENESS_MISS_WINDOW_S:
            return True
        try:
            forensics = sidecar.private_exit_forensics()
        except Exception:
            forensics = {}
        if isinstance(forensics, dict) and forensics.get("process_alive") is True:
            # The client is running and merely unresponsive.  Say so on health
            # rather than declaring a death that did not happen.
            return True
        with self.condition:
            self.v2_liveness_misses.pop(place_number, None)
        return False

    def _poll_v2_sidecars_once(self) -> bool:
        """Poll every current seat once; fail closed on ownership loss."""
        with self.condition:
            if (
                self.state in TERMINAL_STATES or self.sidecars_stopping
                or self.server_exit_observed
            ):
                return False
            sidecars = tuple(self.sidecars.items())
            game_state = self.state
            startup_deadline = self.sidecar_start_deadline
        all_running = bool(sidecars)
        all_over = bool(sidecars)
        phase_evidence: list[dict[str, Any]] = []
        for place_number, sidecar in sidecars:
            with self.condition:
                generation = self.sidecar_generations.get(place_number, 0)
                current = (
                    self.sidecars.get(place_number) is sidecar
                    and self.sidecar_ready_generations.get(place_number)
                    == generation
                )
            if not current:
                all_running = False
                all_over = False
                continue
            now = time.monotonic()
            if game_state == "running" and self._v2_liveness_probe_may_yield(
                place_number, generation, now,
            ):
                # The agent is playing this seat and its commands are being
                # answered.  Take the free evidence and leave the command
                # stream to it.  ``all_over`` cannot be concluded from a
                # sample that was never taken, so this seat withholds it; the
                # probe-gap ceiling bounds how long that can delay the verdict.
                all_over = False
                evidence = self._collect_v2_phase_evidence(
                    place_number, generation, sidecar,
                )
                if evidence is not None:
                    phase_evidence.append(evidence)
                continue
            self._v2_note_liveness_probe(place_number, generation, now)
            try:
                fields = self._parse_sidecar_status(
                    sidecar.status(timeout_s=V2_LIVENESS_POLL_TIMEOUT_S),
                )
            except SidecarError as exc:
                if exc.code in {"command_in_progress", "native_busy"}:
                    # STATUS shares the sidecar's single command stream.  An
                    # accepted action temporarily opens a callback barrier so
                    # its durable receipt can be recorded without deadlock;
                    # polling that exact window is a skipped sample, not seat
                    # loss.  A stream still busy with somebody else's command
                    # says the same thing and no more.  Restart the whole poll
                    # on the next timer tick so partial phase evidence is
                    # never reconciled.
                    return True
                if exc.code == "deadline_exceeded":
                    # A timeout says the client did not answer in time.  It
                    # does NOT say the client is gone, and this poll is the
                    # only place in the system that used to conflate the two:
                    # one latency tail SIGKILLed a healthy, seat-owning
                    # client and spent a recovery attempt on it.  Demand
                    # corroboration instead.
                    if self._v2_note_liveness_miss(place_number, sidecar):
                        return True
                # A loss that entered recovery must keep this poller alive:
                # nothing restarts the status thread once it returns, and the
                # recovered generation still needs its phase evidence sampled.
                return self._on_sidecar_exit(place_number, generation, {
                    "state": "failed",
                    "error_code": "status_unavailable",
                })
            except Exception:
                return self._on_sidecar_exit(place_number, generation, {
                    "state": "failed",
                    "error_code": "status_unavailable",
                })
            self._v2_clear_liveness_misses(place_number)
            try:
                with self.condition:
                    self._record_v2_native_identity_locked(
                        self.places[place_number - 1], generation, fields,
                    )
            except SidecarError:
                return self._on_sidecar_exit(place_number, generation, {
                    "state": "failed",
                    "error_code": "wrong_player",
                })
            clean = self._sanitized_sidecar_health(sidecar, generation)
            if "state" in fields:
                clean["client_state"] = fields["state"]
            clean["server_connected"] = fields.get("server") == "1"
            if "seat" in fields:
                clean["seat_state"] = fields["seat"]
            with self.condition:
                if (
                    self.sidecar_generations.get(place_number) != generation
                    or self.sidecars.get(place_number) is not sidecar
                ):
                    all_running = False
                    continue
                self.sidecar_health[place_number] = clean
                game_state = self.state
                startup_deadline = self.sidecar_start_deadline

            client_state = fields.get("state")
            owns_seat = (
                fields.get("server") == "1" and fields.get("seat") == "ready"
            )
            within_startup_grace = (
                game_state == "starting"
                and startup_deadline is not None
                and time.monotonic() < startup_deadline
            )
            healthy = owns_seat and client_state == "running"
            preparing = (
                owns_seat and client_state == "preparing"
                and (game_state == "lobby" or within_startup_grace)
            )
            if healthy:
                all_over = False
                evidence = self._collect_v2_phase_evidence(
                    place_number, generation, sidecar,
                )
                if evidence is not None:
                    phase_evidence.append(evidence)
                continue
            if owns_seat and client_state == "over":
                # With --exit-on-end, OVER is the expected short transition
                # before the server monitor observes process completion.
                all_running = False
                continue
            if preparing:
                all_running = False
                all_over = False
                continue
            failure_code = (
                "seat_lost" if not owns_seat else "startup_timeout"
            )
            clean.update({"state": "failed", "error_code": failure_code})
            return self._on_sidecar_exit(place_number, generation, clean)

        if all_over:
            # Freeciv reports OVER to every connected client after the game is
            # authoritatively finished, but --exit-on-end does not complete
            # until those clients disconnect. Stop the now-terminal sidecars
            # before the reconciliation watchdog can mistake the absence of a
            # next phase for a stuck phase. The server monitor still owns the
            # final completed/invalid/failed classification from its exit code
            # and score artifact.
            should_stop = False
            with self.condition:
                if (
                    self.state in {"starting", "running"}
                    and self.start_sent
                    and not self.cancel_requested
                    and self.error is None
                    and not self.sidecars_stopping
                    and not self.server_exit_observed
                ):
                    self._terminalize_v2_phase_locked("terminalizing")
                    should_stop = True
            if should_stop:
                self._stop_all_sidecars()
            return False

        with self.condition:
            if (
                all_running and self.state == "starting"
                and not self.cancel_requested and self.error is None
                and not self.sidecars_stopping
            ):
                if not self.start_sent:
                    self.start_sent = True
                    self.start_count += 1
                    self.started_at = self.started_at or time.time()
                self.state = "running"
                self._write_manifest()
                self.condition.notify_all()
            running = self.state == "running"
        if running:
            claim, failed = self._update_v2_phase_ledger(
                phase_evidence, time.monotonic(),
            )
            if failed:
                self._stop_all_sidecars()
                self._terminate_child()
                return False
            if claim is not None:
                self._start_v2_timeout_phase_end(claim)
        return True

    def _v2_game_live(self) -> bool:
        """Whether a game is still being played, sampled without the lock held.

        The latches below are the only ways a full-control-v2 game stops
        needing to be watched: it reached a terminal state, its owner
        cancelled it, its seats are being torn down, its server has already
        exited and the monitor owns what happens next, or the whole service is
        shutting down and nothing may be brought up behind it.
        """
        if self.supervisor.shutdown_event.is_set():
            return False
        with self.condition:
            return not (
                self.state in TERMINAL_STATES
                or self.cancel_requested
                or self.sidecars_stopping
                or self.server_exit_observed
            )

    def _fail_v2_status_polling(self, exc: BaseException) -> None:
        """End a game whose seats can no longer be watched, naming why.

        A game nobody polls is not a game: its phase ledger never advances
        again, its deadlines never fire, and its seats wait on a boundary that
        has stopped being sampled.  Ending it here is worse than continuing
        and better than the alternative this replaces, which was a thread that
        disappeared leaving a live game with no explanation anywhere.  Only
        the exception's type is published: its text can carry paths.
        """
        with self.condition:
            if self.state in TERMINAL_STATES:
                return
            self._fail_v2_phase_locked(
                "v2_status_poll_failed",
                "full-control-v2 seat status polling stopped after "
                f"{V2_STATUS_POLL_FAULT_LIMIT} consecutive faults "
                f"({type(exc).__name__})",
            )
        self._stop_all_sidecars()
        self._terminate_child()

    def _poll_v2_sidecars(self) -> None:
        faults = 0
        while True:
            try:
                keep_polling = self._poll_v2_sidecars_once()
            except Exception as exc:
                # One fault can be a transient the next sample recovers from;
                # an uninterrupted run of them is this thread failing, and it
                # must fail the game rather than vanish from it.
                faults += 1
                if faults >= V2_STATUS_POLL_FAULT_LIMIT:
                    self._fail_v2_status_polling(exc)
                    return
                keep_polling = True
            else:
                faults = 0
            # Only the game's own state may end this thread, because nothing
            # restarts it.  A poll that classified one seat's loss as somebody
            # else's business -- a retired generation, a completion grace, a
            # recovery -- must not take the poller with it: the ledger would
            # freeze with the game still live, no phase would ever advance
            # again, and nothing anywhere would say why.
            if not keep_polling and not self._v2_game_live():
                return
            if self.supervisor.shutdown_event.wait(0.25):
                return

    def _start_if_ready(self) -> None:
        """Start once after every external seat has a current READY sidecar."""
        if self.config["control_protocol"] == FULL_CONTROL_V2:
            status_thread: threading.Thread | None = None
            with self.condition:
                if (
                    self.state != "lobby" or self.cancel_requested
                    or self.error is not None or self.sidecars_stopping
                    or len(self.place_agents) != self.max_agents
                    or any(
                        self.sidecar_ready_generations.get(place.number)
                        != self.sidecar_generations.get(place.number)
                        for place in self.joinable_places
                    )
                ):
                    return
                # Joining a player resets Freeciv's ready bits. Release the
                # explicit barrier only after every expected external sidecar
                # owns its exact seat; native PLAYER_READY packets start it.
                self.v2_pregame_gate_open = True
                if self.sidecar_status_thread is None:
                    status_thread = threading.Thread(
                        target=self._poll_v2_sidecars,
                        name=f"freeciv-agent-status-{self.game_id}",
                        daemon=True,
                    )
                    self.sidecar_status_thread = status_thread
                self._write_manifest()
                self.condition.notify_all()
            if status_thread is not None:
                status_thread.start()
            self._start_v2_replay_keepwarm()
            return
        failure = False
        with self.console_lock:
            with self.condition:
                if self.start_sent or len(self.place_agents) != self.max_agents:
                    return
                if (
                    self.state != "lobby" or self.cancel_requested
                    or self.error is not None
                    or (
                        self.config["control_protocol"] == FULL_CONTROL_V2
                        and self.sidecars_stopping
                    )
                ):
                    return
                if self.config["control_protocol"] == FULL_CONTROL_V2 and any(
                    self.sidecar_ready_generations.get(place.number)
                    != self.sidecar_generations.get(place.number)
                    for place in self.joinable_places
                ):
                    return
                self.start_sent = True
                self.start_count += 1
                self.state = "starting"
                self.started_at = time.time()
                if self.config["control_protocol"] == FULL_CONTROL_V2:
                    self.sidecar_start_deadline = (
                        time.monotonic() + V2_SIDECAR_STARTUP_GRACE_S
                    )
            try:
                self._send_commands(["start"], wait_for_prompt=False)
                with (self.episode / "server.commands").open(
                    "a", encoding="utf-8",
                ) as stream:
                    stream.write("start\n")
                    stream.flush()
                    os.fsync(stream.fileno())
            except Exception as exc:
                failure = True
                with self.condition:
                    self.error = f"could not start game: {exc}"
                    self.state = "failed"
                    self.finished_at = time.time()
                    self._terminalize_v2_phase_locked("failed")
                    self._write_manifest()
                    self.condition.notify_all()
            else:
                with self.condition:
                    self._write_manifest()
                    self.condition.notify_all()
        if failure:
            self._stop_all_sidecars()
            self._terminate_child()
            return
        if self.config["control_protocol"] == FULL_CONTROL_V2:
            with self.condition:
                if self.sidecar_status_thread is None:
                    self.sidecar_status_thread = threading.Thread(
                        target=self._poll_v2_sidecars,
                        name=f"freeciv-agent-status-{self.game_id}",
                        daemon=True,
                    )
                    self.sidecar_status_thread.start()
            self._start_v2_replay_keepwarm()

    def authorize_owner(self, token: str | None) -> None:
        if token is None:
            raise APIProblem(HTTPStatus.UNAUTHORIZED, "bearer token required")
        if not _same_token(token, self.owner_token_hash):
            raise APIProblem(HTTPStatus.FORBIDDEN, "owner token is not authorized")

    def authorize_internal(self, token: str | None) -> None:
        if token is None:
            raise APIProblem(HTTPStatus.UNAUTHORIZED, "bearer token required")
        if not _same_token(token, self.internal_token_hash):
            raise APIProblem(
                HTTPStatus.FORBIDDEN, "internal token is not authorized",
            )

    def _reconcile_native_timeout_locked(self) -> bool:
        """Clear a stale override only from post-activation output evidence."""
        override_sequence = self.native_timeout_override_sequence
        if (
            self.native_viewer is None
            and self.socket_polling_enabled
            and override_sequence is not None
            and self.observed_timeout == -1
            and self.observed_timeout_sequence > override_sequence
        ):
            self.socket_polling_enabled = False
            self.native_timeout_override_sequence = None
            return True
        return False

    def _remember_native_viewer_locked(self, lease: dict[str, Any]) -> None:
        self.native_viewer_leases[lease["lease_id"]] = lease
        while len(self.native_viewer_leases) > 32:
            oldest = next(iter(self.native_viewer_leases))
            if self.native_viewer is self.native_viewer_leases[oldest]:
                break
            del self.native_viewer_leases[oldest]

    def _native_viewer_public_locked(
        self, lease: dict[str, Any],
    ) -> dict[str, Any]:
        result = {
            "schema_version": 1,
            "game_id": self.game_id,
            "lease_id": lease["lease_id"],
            "username": lease["username"],
            "state": lease["state"],
            "game_state": self.state,
            "activation_mode": lease.get("activation_mode"),
            "activation_timeout_s": lease["activation_timeout_s"],
            "connect_timeout_s": lease["connect_timeout_s"],
            "active": self.native_viewer is lease,
            "ready": lease["state"] == "game_ready",
        }
        for key in ("error", "timeout_restored"):
            if key in lease:
                result[key] = lease[key]
        return result

    def native_viewer_status(self, lease_id: Any) -> dict[str, Any]:
        if self.config["control_protocol"] == FULL_CONTROL_V2:
            raise APIProblem(
                HTTPStatus.CONFLICT,
                "native viewer is unavailable for full-control-v2 games",
            )
        if not isinstance(lease_id, str) or not lease_id:
            raise APIProblem(
                HTTPStatus.BAD_REQUEST,
                "native viewer status requires a non-empty lease_id",
            )
        with self.condition:
            lease = self.native_viewer_leases.get(lease_id)
            if lease is None:
                raise APIProblem(HTTPStatus.NOT_FOUND, "native viewer lease not found")
            return self._native_viewer_public_locked(lease)

    def native_viewer_turn_response_started(self) -> int:
        """Mark Lua's interrupt-ignoring internal HTTP response as active."""
        with self.condition:
            self.native_turn_responses_in_flight += 1
            self.native_turn_response_generation += 1
            generation = self.native_turn_response_generation
            self.native_turn_response_pending[generation] = None
            lease = self.native_viewer
            if (
                lease is not None
                and lease.get("state") == "enabling_server"
                and not lease.get("signal_sent")
            ):
                lease["required_turn_response_generation"] = max(
                    generation,
                    int(lease.get("required_turn_response_generation", 0)),
                )
            self.condition.notify_all()
        if lease is not None and not lease.get("signal_sent"):
            self._schedule_native_viewer_activation_signal(lease)
        return generation

    def native_viewer_turn_response_identified(
        self, generation: int, turn: Any,
    ) -> None:
        """Associate an internal response generation with its public-safe turn."""
        if isinstance(turn, bool) or not isinstance(turn, int) or turn < 0:
            return
        with self.condition:
            if generation in self.native_turn_response_pending:
                self.native_turn_response_pending[generation] = turn
            self.condition.notify_all()

    def _schedule_native_viewer_activation_signal(
        self, lease: dict[str, Any],
    ) -> None:
        """Schedule one lease-scoped SIGINT after the bridge acknowledges curl."""
        with self.condition:
            if (
                self.native_viewer is not lease
                or lease.get("state") != "enabling_server"
                or lease.get("activation_signal_scheduled")
            ):
                return
            lease["activation_signal_scheduled"] = True

        def signal_after_curl() -> None:
            try:
                with self.condition:
                    while True:
                        current = self.native_viewer
                        if (
                            current is not lease
                            or lease.get("state") != "enabling_server"
                        ):
                            return
                        now = time.monotonic()
                        last = self.last_native_viewer_sigint_at
                        guard_until = (
                            0.0 if last is None
                            else last + NATIVE_VIEWER_SIGNAL_GUARD_S
                        )
                        required_generation = max(
                            int(lease.get("required_turn_response_generation", 0)),
                            self.native_turn_response_generation,
                        )
                        if (
                            self.native_turn_responses_in_flight > 0
                            or self.native_turn_response_completed_generation
                            < required_generation
                        ):
                            lease["required_turn_response_generation"] = (
                                required_generation
                            )
                        turn_callback_active = bool(
                            self.current_turn is not None
                            and not self.current_turn.get("resolved")
                        )
                        if (
                            self.native_turn_responses_in_flight > 0
                            or self.native_turn_response_completed_generation
                            < required_generation
                            or turn_callback_active
                        ):
                            self.condition.wait(0.1)
                            continue
                        if now < guard_until:
                            self.condition.wait(min(guard_until - now, 0.1))
                            continue
                        process = self.process
                        if process is None or process.poll() is not None:
                            return
                        try:
                            process.send_signal(signal.SIGINT)
                        except OSError:
                            return
                        lease["signal_sent"] = True
                        self.last_native_viewer_sigint_at = now
                        self.condition.notify_all()
                        return
            finally:
                with self.condition:
                    if self.native_viewer is lease:
                        lease["activation_signal_scheduled"] = False
                    self.condition.notify_all()

        threading.Thread(
            target=signal_after_curl,
            name=f"freeciv-viewer-turn-signal-{self.game_id}",
            daemon=True,
        ).start()

    def native_viewer_turn_response_sent(self) -> None:
        """Record HTTP completion; Lua stdout separately marks curl completion."""
        with self.condition:
            if self.native_turn_responses_in_flight > 0:
                self.native_turn_responses_in_flight -= 1
            self.condition.notify_all()
            lease = self.native_viewer
        if lease is not None:
            self._schedule_native_viewer_activation_signal(lease)

    def request_native_viewer(self) -> dict[str, Any]:
        if self.config["control_protocol"] == FULL_CONTROL_V2:
            raise APIProblem(
                HTTPStatus.CONFLICT,
                "native viewer is unavailable for full-control-v2 games",
            )
        """Open one short-lived, owner-authorized local observer lease."""
        signal_sent = False
        timeout_override_set = False
        try:
            with self.console_lock:
                with self.condition:
                    if self.state in TERMINAL_STATES:
                        raise APIProblem(
                            HTTPStatus.CONFLICT,
                            "native live viewing is unavailable after a game "
                            "ends; use the replay URL instead",
                        )
                    if self.state not in {"lobby", "starting", "running"}:
                        raise APIProblem(
                            HTTPStatus.CONFLICT,
                            f"native live viewing is unavailable while {self.state}",
                        )
                    if self.native_viewer is not None:
                        raise APIProblem(
                            HTTPStatus.CONFLICT,
                            "a native viewer is already active for this game",
                        )
                    self._reconcile_native_timeout_locked()
                    if self.socket_polling_enabled:
                        raise APIProblem(
                            HTTPStatus.CONFLICT,
                            "native viewer timeout override could not be reset "
                            "safely; restart this game instead of retrying",
                        )
                    process = self.process
                    if process is None or process.poll() is not None:
                        raise APIProblem(
                            HTTPStatus.CONFLICT,
                            "the Freeciv server is not running",
                        )
                    activation_state = self.state
                    now = time.monotonic()
                    if (
                        activation_state in {"starting", "running"}
                        and self.last_native_viewer_sigint_at is not None
                        and now - self.last_native_viewer_sigint_at
                        < NATIVE_VIEWER_SIGNAL_GUARD_S
                    ):
                        retry_after = NATIVE_VIEWER_SIGNAL_GUARD_S - (
                            now - self.last_native_viewer_sigint_at
                        )
                        raise APIProblem(
                            HTTPStatus.CONFLICT,
                            "native viewer was just released; retry in "
                            f"{retry_after:.1f}s so Freeciv cannot receive a "
                            "second SIGINT unsafely",
                        )
                    username = f"Watch-{secrets.token_hex(6)}"
                    action_timeout_s = self.config["action_timeout_s"]
                    lease = {
                        "lease_id": f"viewer_{secrets.token_urlsafe(12)}",
                        "username": username,
                        "requested_at": time.time(),
                        "state": (
                            "waiting_for_client"
                            if activation_state == "lobby"
                            else "enabling_server"
                        ),
                        "activation_mode": None,
                        "signal_sent": False,
                        "activation_signal_scheduled": False,
                        "release_requested": False,
                        "restore_started": False,
                        "restore_finished": False,
                        "activation_timeout_s": (
                            None
                            if action_timeout_s is None
                            else max(15, math.ceil(action_timeout_s + 15))
                        ),
                        "connect_timeout_s": NATIVE_VIEWER_CONNECT_TIMEOUT_S,
                    }
                    self.native_viewer = lease
                    self._remember_native_viewer_locked(lease)
                if activation_state == "lobby":
                    # Lobby servers already poll sockets. An explicit zero
                    # timeout also makes a concurrent start remain paused
                    # until the observer has connected and been promoted.
                    timeout_override_set = True
                    with self.condition:
                        lease["activation_mode"] = "lobby_timeout"
                        self.socket_polling_enabled = True
                        self.native_timeout_override_sequence = (
                            self.server_output_sequence
                        )
                    self._send_timeout(0)
                else:
                    # In S_S_RUNNING, timeout=-1 bypasses socket polling. The
                    # first SIGINT changes it to zero; a second can exit. Lua's
                    # synchronous turn HTTP command temporarily ignores
                    # SIGINT, so defer until that response if one is active.
                    with self.condition:
                        lease["activation_mode"] = "running_signal"
                        self.socket_polling_enabled = True
                        self.native_timeout_override_sequence = (
                            self.server_output_sequence
                        )
                        turn_callback_active = bool(
                            self.native_turn_responses_in_flight > 0
                            or self.native_turn_response_completed_generation
                            < self.native_turn_response_generation
                            or (
                                self.current_turn is not None
                                and not self.current_turn.get("resolved")
                            )
                        )
                        if turn_callback_active:
                            lease["required_turn_response_generation"] = (
                                self.native_turn_response_generation
                            )
                    if not turn_callback_active:
                        process.send_signal(signal.SIGINT)
                        signal_sent = True
                        with self.condition:
                            lease["signal_sent"] = True
                            self.last_native_viewer_sigint_at = now
                    else:
                        self._schedule_native_viewer_activation_signal(lease)
        except APIProblem:
            raise
        except Exception as exc:
            if timeout_override_set or signal_sent:
                self._restore_native_timeout(lease)
            else:
                with self.condition:
                    if "lease" in locals() and self.native_viewer is lease:
                        self.native_viewer = None
                    if not signal_sent:
                        self.socket_polling_enabled = False
                        self.native_timeout_override_sequence = None
                    self.condition.notify_all()
            raise APIProblem(
                HTTPStatus.CONFLICT,
                f"could not enable the native viewer: {exc}",
            ) from exc

        threading.Thread(
            target=self._manage_native_viewer,
            args=(lease,),
            name=f"freeciv-viewer-{self.game_id}",
            daemon=True,
        ).start()
        return {
            "schema_version": 1,
            "game_id": self.game_id,
            "lease_id": lease["lease_id"],
            "host": "127.0.0.1",
            "port": self.freeciv_port,
            "username": username,
            "activation_timeout_s": lease["activation_timeout_s"],
            "connect_timeout_s": lease["connect_timeout_s"],
            "state": lease["state"],
            "game_state": activation_state,
            "local_only": True,
        }

    def release_native_viewer(self, lease_id: Any) -> dict[str, Any]:
        """Idempotently release only the matching native viewer lease."""
        if self.config["control_protocol"] == FULL_CONTROL_V2:
            raise APIProblem(
                HTTPStatus.CONFLICT,
                "native viewer is unavailable for full-control-v2 games",
            )
        if not isinstance(lease_id, str) or not lease_id:
            raise APIProblem(
                HTTPStatus.BAD_REQUEST, "lease_id must be a non-empty string",
            )
        with self.console_lock:
            with self.condition:
                lease = self.native_viewer
                if lease is None:
                    return {
                        "schema_version": 1,
                        "game_id": self.game_id,
                        "lease_id": lease_id,
                        "released": False,
                        "state": "inactive",
                    }
                if lease.get("lease_id") != lease_id:
                    return {
                        "schema_version": 1,
                        "game_id": self.game_id,
                        "lease_id": lease_id,
                        "released": False,
                        "state": "stale_lease",
                    }
                lease["release_requested"] = True
                self.condition.notify_all()
            restored = self._restore_native_timeout(lease)
        return {
            "schema_version": 1,
            "game_id": self.game_id,
            "lease_id": lease_id,
            "released": True,
            "state": "released",
            "timeout_restored": restored,
        }

    def _manage_native_viewer(self, lease: dict[str, Any]) -> None:
        username = lease["username"]
        connected_marker = f"{username} has connected"
        disconnected_marker = f"Lost connection: {username}"
        connected = False
        try:
            if lease.get("activation_mode") == "running_signal":
                activation_timeout_s = lease["activation_timeout_s"]
                activation_deadline = (
                    None
                    if activation_timeout_s is None
                    else time.monotonic() + activation_timeout_s
                )
                with self.condition:
                    override_sequence = self.native_timeout_override_sequence
                    while self.native_viewer is lease:
                        if (
                            override_sequence is not None
                            and self.observed_timeout == 0
                            and self.observed_timeout_sequence > override_sequence
                        ):
                            lease["state"] = "waiting_for_client"
                            self.condition.notify_all()
                            break
                        process = self.process
                        if self.state in TERMINAL_STATES:
                            lease["state"] = "game_ended"
                            break
                        if process is None or process.poll() is not None:
                            lease["state"] = "server_disconnected"
                            lease["error"] = "Freeciv server stopped"
                            break
                        if activation_deadline is None:
                            self.condition.wait(0.25)
                        else:
                            remaining = activation_deadline - time.monotonic()
                            if remaining <= 0:
                                lease["state"] = "error"
                                lease["error"] = (
                                    "Freeciv did not pause autogame socket "
                                    "polling before the viewer activation "
                                    "deadline"
                                )
                                break
                            self.condition.wait(min(remaining, 0.25))
                if lease.get("state") != "waiting_for_client":
                    return

            deadline = time.monotonic() + lease["connect_timeout_s"]
            with self.condition:
                while self.native_viewer is lease:
                    if connected_marker in self.server_output_tail:
                        connected = True
                        lease["state"] = "connected"
                        self.condition.notify_all()
                        break
                    process = self.process
                    if self.state in TERMINAL_STATES:
                        lease["state"] = "game_ended"
                        self.condition.notify_all()
                        break
                    if process is None or process.poll() is not None:
                        lease["state"] = "server_disconnected"
                        lease["error"] = "Freeciv server stopped"
                        self.condition.notify_all()
                        break
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        lease["state"] = "connect_timeout"
                        lease["error"] = (
                            "Freeciv client did not connect before the viewer "
                            "lease expired"
                        )
                        self.condition.notify_all()
                        break
                    self.condition.wait(min(remaining, 0.25))
            if not connected:
                return

            with self.console_lock:
                with self.condition:
                    if (
                        self.native_viewer is not lease
                        or lease.get("release_requested")
                    ):
                        return
                self._send_commands([f"observe {username}"])
                self._send_timeout(1)
            with self.condition:
                lease["state"] = "observing"
                self.condition.notify_all()
                while self.native_viewer is lease:
                    if disconnected_marker in self.server_output_tail:
                        lease["state"] = "disconnected"
                        lease["error"] = "Freeciv viewer connection closed"
                        break
                    if self.state == "running":
                        lease["state"] = "game_ready"
                    process = self.process
                    if self.state in TERMINAL_STATES:
                        lease["state"] = "game_ended"
                        break
                    if process is None or process.poll() is not None:
                        lease["state"] = "server_disconnected"
                        lease["error"] = "Freeciv server stopped"
                        break
                    self.condition.notify_all()
                    self.condition.wait(0.25)
        except Exception as exc:
            with self.condition:
                lease["state"] = "error"
                lease["error"] = str(exc)
                self.condition.notify_all()
        finally:
            self._restore_native_timeout(lease)

    def _restore_native_timeout(self, lease: dict[str, Any]) -> bool:
        with self.console_lock:
            with self.condition:
                if lease.get("restore_finished"):
                    return bool(lease.get("timeout_restored"))
                lease["restore_started"] = True
                process = self.process
                activation_never_signaled = bool(
                    lease.get("activation_mode") == "running_signal"
                    and not lease.get("signal_sent")
                )
                should_write = (
                    self.native_viewer is lease
                    and self.socket_polling_enabled
                    and not activation_never_signaled
                    and self.state not in TERMINAL_STATES
                    and process is not None
                    and process.poll() is None
                )
            restored = not should_write
            if should_write:
                try:
                    self._send_timeout(-1)
                    restored = True
                except Exception as exc:
                    with self.condition:
                        override_sequence = self.native_timeout_override_sequence
                        restored = bool(
                            override_sequence is not None
                            and self.observed_timeout == -1
                            and self.observed_timeout_sequence > override_sequence
                        )
                        if not restored:
                            lease["restore_error"] = str(exc)
            with self.condition:
                if self.native_viewer is lease:
                    self.native_viewer = None
                if restored:
                    self.socket_polling_enabled = False
                    self.native_timeout_override_sequence = None
                if lease.get("release_requested") and lease.get("state") in {
                    "enabling_server", "waiting_for_client", "connected",
                    "observing", "game_ready",
                }:
                    lease["state"] = "released"
                lease["timeout_restored"] = restored
                lease["restore_finished"] = True
                self.condition.notify_all()
            return restored

    def authenticate_agent(self, token: str | None) -> tuple[str, dict[str, Any]]:
        if token is None:
            raise APIProblem(HTTPStatus.UNAUTHORIZED, "bearer token required")
        digest = _digest(token)
        with self.condition:
            for agent_id, agent in self.agents.items():
                if hmac.compare_digest(digest, agent["token_hash"]):
                    return agent_id, agent
        raise APIProblem(HTTPStatus.FORBIDDEN, "agent token is not authorized")

    def join(
        self, token: str | None, selected_place: int | str | None = None,
        controller_label: str | None = None, metadata: Any = None,
        supported_control_protocols: Any = None,
    ) -> dict[str, Any]:
        if token is None:
            raise APIProblem(HTTPStatus.UNAUTHORIZED, "join bearer token required")
        clean_metadata = _validate_metadata(metadata)
        clean_supported: tuple[str, ...] | None = None
        capability_error: FullControlSchemaError | None = None
        if supported_control_protocols is not None:
            try:
                clean_supported = validate_supported_control_protocols(
                    supported_control_protocols,
                )
            except FullControlSchemaError as exc:
                capability_error = exc
        if self.config["control_protocol"] == FULL_CONTROL_V2 and (
            capability_error is not None
            or clean_supported is None
            or FULL_CONTROL_V2 not in clean_supported
        ):
            message = (
                "this game requires supported_control_protocols to contain "
                "full-control-v2"
            )
            if capability_error is not None:
                message += f"; {capability_error}"
            raise APIProblem(
                HTTPStatus.UPGRADE_REQUIRED,
                message,
                structured_error(
                    "unsupported_protocol", message, retryable=False,
                    details={"required_control_protocol": FULL_CONTROL_V2},
                ),
            ) from capability_error
        if capability_error is not None:
            raise APIProblem(
                HTTPStatus.BAD_REQUEST, str(capability_error),
            ) from capability_error
        token_digest = _digest(token)
        sidecar: Any | None = None
        generation: int | None = None
        with self.condition:
            for agent_id, agent in self.agents.items():
                if hmac.compare_digest(token_digest, agent["token_hash"]):
                    if (
                        controller_label is not None
                        and controller_label != agent["controller_label"]
                    ) or (
                        metadata is not None
                        and clean_metadata != agent["metadata"]
                    ) or (
                        clean_supported is not None
                        and clean_supported != agent[
                            "supported_control_protocols"
                        ]
                    ):
                        raise APIProblem(
                            HTTPStatus.CONFLICT,
                            "controller identity is immutable after join",
                        )
                    if self.config["control_protocol"] == FULL_CONTROL_V2:
                        place_number = agent["place"]
                        current_generation = self.sidecar_generations.get(
                            place_number,
                        )
                        current_sidecar = self.sidecars.get(place_number)
                        health = self._sanitized_sidecar_health(
                            current_sidecar, current_generation or 0,
                        )
                        if (
                            current_generation is None
                            or self.sidecar_ready_generations.get(place_number)
                            != current_generation
                            or health.get("state") != "ready"
                        ):
                            message = (
                                "the current full-control-v2 sidecar is not READY"
                            )
                            raise APIProblem(
                                HTTPStatus.SERVICE_UNAVAILABLE,
                                message,
                                structured_error(
                                    "sidecar_unavailable", message,
                                    retryable=self.state not in TERMINAL_STATES,
                                    details={"seat_id": agent["seat_id"]},
                                ),
                            )
                    return self._join_result(agent_id, agent, token, True)
            if (
                not isinstance(controller_label, str)
                or not CONTROLLER_LABEL_RE.fullmatch(controller_label)
                or "-" not in controller_label
                or controller_label.startswith("-")
                or controller_label.endswith("-")
                or controller_label.casefold() in {"agent", "harness-model"}
            ):
                raise APIProblem(
                    HTTPStatus.BAD_REQUEST,
                    "controller_label is required in non-generic "
                    "harness-model form (for example codex-gpt-5.6-sol)",
                )
            if not hmac.compare_digest(token_digest, self.join_token_hash):
                raise APIProblem(HTTPStatus.FORBIDDEN, "join token is not authorized")
            if (
                self.state != "lobby" or self.cancel_requested
                or self.error is not None
                or (
                    self.config["control_protocol"] == FULL_CONTROL_V2
                    and self.sidecars_stopping
                )
            ):
                raise APIProblem(HTTPStatus.CONFLICT, "game is no longer joinable")
            open_places = [
                place for place in self.joinable_places
                if place.number not in self.place_agents
            ]
            if not open_places:
                raise APIProblem(HTTPStatus.CONFLICT, "all agent places are claimed")
            chosen: Place | None = None
            if selected_place is None:
                chosen = open_places[0]
            elif isinstance(selected_place, int) and not isinstance(selected_place, bool):
                chosen = next(
                    (place for place in open_places
                     if place.number == selected_place),
                    None,
                )
            elif isinstance(selected_place, str):
                chosen = next(
                    (place for place in open_places
                     if place.seat_id == selected_place),
                    None,
                )
            if chosen is None:
                raise APIProblem(
                    HTTPStatus.CONFLICT,
                    "selected agent place is unavailable",
                )
            agent_id = f"agent_{secrets.token_urlsafe(12)}"
            agent_token = _token()
            resolved_label = controller_label or "Agent"
            agent = {
                "place": chosen.number,
                "seat_id": chosen.seat_id,
                "player_name": chosen.player_name,
                "token_hash": _digest(agent_token),
                "joined_at": time.time(),
                "controller_label": resolved_label,
                "metadata": clean_metadata,
                "supported_control_protocols": clean_supported or (),
                "controller_fingerprint": _controller_identity_fingerprint(
                    resolved_label, clean_metadata,
                ),
            }
            self.agents[agent_id] = agent
            self.place_agents[chosen.number] = agent_id
            if self.config["control_protocol"] == FULL_CONTROL_V2:
                generation = self.sidecar_generations.get(chosen.number, 0) + 1
                self.sidecar_generations[chosen.number] = generation
                self.v2_native_player_identities.pop(chosen.number, None)
                self.v2_pregame_ready_places.discard(chosen.number)
                self.v2_pregame_gate_open = False
                try:
                    sidecar = self._make_sidecar(chosen, generation)
                except Exception as exc:
                    del self.agents[agent_id]
                    del self.place_agents[chosen.number]
                    self._write_auth()
                    self._write_manifest()
                    message = "could not prepare the full-control-v2 sidecar"
                    raise APIProblem(
                        HTTPStatus.SERVICE_UNAVAILABLE,
                        message,
                        structured_error(
                            "sidecar_unavailable", message, retryable=True,
                            details={"seat_id": chosen.seat_id},
                        ),
                    ) from exc
                self.sidecars[chosen.number] = sidecar
                self.sidecar_health[chosen.number] = self._sanitized_sidecar_health(
                    sidecar, generation,
                )
            self._write_auth()
            self._write_manifest()
            self._append_trace(
                {
                    "event": "join",
                    "joined_at": agent["joined_at"],
                    "seat_id": chosen.seat_id,
                    "player_name": chosen.player_name,
                    "controller_label": resolved_label,
                    "controller_metadata": clean_metadata,
                    "supported_control_protocols": list(clean_supported or ()),
                    "controller_fingerprint": agent[
                        "controller_fingerprint"
                    ],
                }
            )
        if self.config["control_protocol"] == FULL_CONTROL_V2:
            assert sidecar is not None and generation is not None
            try:
                sidecar.start_and_take()
                # READY proves native acquisition, but connection metadata is
                # hydrated only by STATUS. Sample it before publishing this
                # generation so the first seat can use pregame immediately.
                initial_status = self._parse_sidecar_status(
                    sidecar.status(timeout_s=2.0),
                )
                if (
                    initial_status.get("server") != "1"
                    or initial_status.get("seat") != "ready"
                    or not initial_status.get("state")
                ):
                    raise SidecarError(
                        "seat_lost",
                        "native sidecar did not retain the requested seat",
                    )
                with self.condition:
                    self._record_v2_native_identity_locked(
                        chosen, generation, initial_status,
                    )
            except Exception as exc:
                try:
                    sidecar.stop()
                except Exception:
                    pass
                with self.condition:
                    if (
                        self.sidecar_generations.get(chosen.number) == generation
                        and self.sidecars.get(chosen.number) is sidecar
                    ):
                        self.sidecars.pop(chosen.number, None)
                        self.sidecar_ready_generations.pop(chosen.number, None)
                        self.v2_native_player_identities.pop(chosen.number, None)
                        self.sidecar_health[chosen.number] = (
                            self._sanitized_sidecar_health(sidecar, generation)
                        )
                        if self.place_agents.get(chosen.number) == agent_id:
                            self.place_agents.pop(chosen.number, None)
                        self.agents.pop(agent_id, None)
                        self._write_auth()
                        self._write_manifest()
                        self._append_trace({
                            "event": "join_failed",
                            "seat_id": chosen.seat_id,
                            "reason": "sidecar_unavailable",
                            "generation": generation,
                            "failed_at": time.time(),
                        })
                        self.condition.notify_all()
                message = (
                    "the headless Freeciv sidecar could not acquire the "
                    "requested human seat"
                )
                raise APIProblem(
                    HTTPStatus.SERVICE_UNAVAILABLE,
                    message,
                    structured_error(
                        "sidecar_unavailable", message,
                        retryable=self.state == "lobby",
                        details={"seat_id": chosen.seat_id},
                    ),
                ) from exc
            accepted = False
            committed_control: V2SeatControl | None = None
            with self.condition:
                if (
                    self.state == "lobby"
                    and not self.cancel_requested
                    and self.error is None
                    and not self.sidecars_stopping
                    and self.sidecar_generations.get(chosen.number) == generation
                    and self.sidecars.get(chosen.number) is sidecar
                    and self.place_agents.get(chosen.number) == agent_id
                ):
                    health = self._sanitized_sidecar_health(sidecar, generation)
                    if health.get("state") == "ready":
                        try:
                            committed_control = V2SeatControl(
                                self.game_id, agent_id, generation,
                            )
                            self.sidecar_health[chosen.number] = health
                            self.sidecar_ready_generations[chosen.number] = generation
                            self.v2_controls[chosen.number] = committed_control
                            self.v2_execution_locks[chosen.number] = (
                                generation, committed_control, threading.Lock(),
                            )
                            self._write_manifest()
                        except Exception:
                            self.sidecar_ready_generations.pop(chosen.number, None)
                            if (
                                committed_control is not None
                                and self.v2_controls.get(chosen.number)
                                is committed_control
                            ):
                                self.v2_controls.pop(chosen.number, None)
                            lock_record = self.v2_execution_locks.get(
                                chosen.number,
                            )
                            if (
                                lock_record is not None
                                and lock_record[0] == generation
                                and lock_record[1] is committed_control
                            ):
                                self.v2_execution_locks.pop(chosen.number, None)
                            if committed_control is not None:
                                committed_control.close()
                            committed_control = None
                        else:
                            self.condition.notify_all()
                            accepted = True
                if not accepted:
                    self.sidecars.pop(chosen.number, None)
                    self.sidecar_ready_generations.pop(chosen.number, None)
                    self.v2_native_player_identities.pop(chosen.number, None)
                    lock_record = self.v2_execution_locks.get(chosen.number)
                    if lock_record is not None and lock_record[0] == generation:
                        self.v2_execution_locks.pop(chosen.number, None)
                    if self.place_agents.get(chosen.number) == agent_id:
                        self.place_agents.pop(chosen.number, None)
                    self.agents.pop(agent_id, None)
                    self._write_auth()
                    self._write_manifest()
            if not accepted:
                if committed_control is not None:
                    committed_control.close()
                try:
                    sidecar.stop()
                except Exception:
                    pass
                message = "the sidecar was no longer READY when the seat join committed"
                raise APIProblem(
                    HTTPStatus.SERVICE_UNAVAILABLE,
                    message,
                    structured_error(
                        "sidecar_unavailable", message,
                        retryable=self.state == "lobby",
                        details={"seat_id": chosen.seat_id},
                    ),
                )
        self._start_if_ready()
        with self.condition:
            return self._join_result(agent_id, agent, agent_token, False)

    def _join_result(
        self, agent_id: str, agent: dict[str, Any], token: str,
        reconnected: bool,
    ) -> dict[str, Any]:
        value = {
            "schema_version": 1,
            "game_id": self.game_id,
            "agent_id": agent_id,
            "agent_token": token,
            "place": agent["place"],
            "seat_id": agent["seat_id"],
            "player_name": agent["player_name"],
            "controller_label": agent["controller_label"],
            "controller_metadata": agent["metadata"],
            "controller_fingerprint": agent["controller_fingerprint"],
            "state": self.state,
            "control_protocol": self.config["control_protocol"],
            "supported_control_protocols": list(
                agent["supported_control_protocols"]
            ),
            "timing_mode": self.config["timing_mode"],
            "action_timeout_s": self.config["action_timeout_s"],
            "reconnected": reconnected,
        }
        if self.config["control_protocol"] == STRATEGIC_V1:
            value.update({
                "next_url": (
                    f"{self.supervisor.service_url}/v1/games/"
                    f"{self.game_id}/me/next"
                ),
                "actions_url": (
                    f"{self.supervisor.service_url}/v1/games/"
                    f"{self.game_id}/me/actions"
                ),
            })
        else:
            prefix = (
                f"{self.supervisor.service_url}/v2/games/"
                f"{self.game_id}/me"
            )
            value.update({
                **self._v2_evaluation_context_locked(),
                "v2_transport_available": (
                    self.sidecar_ready_generations.get(agent["place"])
                    == self.sidecar_generations.get(agent["place"])
                    and self.sidecar_health.get(agent["place"], {}).get("state")
                    == "ready"
                ),
                "health_url": f"{prefix}/health",
                "state_url": f"{prefix}/state",
                "legal_actions_url": f"{prefix}/legal-actions",
                "batches_url": f"{prefix}/batches",
                "receipts_url": f"{prefix}/receipts/{{batch_id}}",
                "wait_url": f"{prefix}/wait",
                "openapi_url": (
                    f"{self.supervisor.service_url}/v2/openapi.json"
                ),
            })
        if self.error is not None:
            value["error"] = self.error
        return value

    def v2_health(self, agent_id: str) -> dict[str, Any]:
        if self.config["control_protocol"] != FULL_CONTROL_V2:
            raise APIProblem(
                HTTPStatus.CONFLICT,
                "full-control-v2 health is unavailable for strategic-v1 games",
            )
        with self.condition:
            agent = self.agents.get(agent_id)
            if agent is None:
                raise APIProblem(HTTPStatus.FORBIDDEN, "agent is not authorized")
            place_number = agent["place"]
            generation = self.sidecar_generations.get(place_number, 0)
            sidecar = self.sidecars.get(place_number)
            health = dict(self.sidecar_health.get(place_number, {}))
            if sidecar is not None:
                current = self._sanitized_sidecar_health(sidecar, generation)
                # Native STATUS fields may lag a terminal process transition.
                # Retain them only while both the sidecar itself and the
                # generation-scoped cached health still say READY.
                if (
                    current.get("state") == "ready"
                    and health.get("generation") == generation
                    and health.get("state") == "ready"
                ):
                    current.update({
                        key: value for key, value in health.items()
                        if key in {
                            "client_state", "server_connected", "seat_state",
                        }
                    })
                elif (
                    health.get("generation") == generation
                    and health.get("state") != "ready"
                ):
                    current = health
                health = current
            wedged = self.v2_wedged_places.get(place_number)
            if wedged is not None:
                # Every component below still reports itself healthy, which is
                # exactly the failure being reported.  Say the boundary is
                # wedged rather than repeating their answer.
                health = dict(health)
                health["state"] = "wedged"
                # Name which fault took the seat.  A projector that refuses
                # the boundary's observations and a native client that
                # stopped existing are different bugs with different owners,
                # and this field is where the loss is read first; one shared
                # code would make every seat loss look like the same one.
                health["error_code"] = (
                    "native_client_exited"
                    if wedged.get("trigger") == "sidecar_exit"
                    else "native_boundary_wedged"
                )
                health["generation"] = generation
            control = self.v2_controls.get(place_number)
            controller_current = (
                control is not None
                and sidecar is not None
                and self._v2_context_current_locked(
                    agent_id, place_number, generation, sidecar, control,
                )
            )
            observation_available = bool(
                controller_current and control is not None
            )
            terminalized = bool(
                self.state in TERMINAL_STATES or self.cancel_requested
                or self.server_exit_observed
            )
            public_phase = self._public_v2_phase()
            own_phase = None
            if not terminalized:
                own_phase = {
                    "state": public_phase["state"],
                    "turn": public_phase["turn"],
                    "phase": public_phase["phase"],
                    "active": self.v2_phase_ledger.get("active_place")
                    == place_number,
                    "timing": public_phase["timing"],
                    "waiting_on": self._v2_waiting_on_locked(place_number),
                }
            try:
                last_phase_end = (
                    self.v2_phase_event_journal.last_for_place(place_number)
                    if self.v2_phase_event_journal is not None else None
                )
            except V2PhaseEventJournalError:
                self._invalidate_v2_phase_event_journal_locked()
                last_phase_end = None
            return {
                "schema_version": 2,
                "control_protocol": FULL_CONTROL_V2,
                "game_id": self.game_id,
                **self._v2_evaluation_context_locked(),
                "agent": {
                    "agent_id": agent_id,
                    "controller_label": agent["controller_label"],
                },
                "game_state": (
                    "cancelled" if self.cancel_requested else self.state
                ),
                "seat": {
                    "place": place_number,
                    "seat_id": agent["seat_id"],
                    "player_name": agent["player_name"],
                    "standing": self._v2_seat_standing_locked(place_number),
                },
                "sidecar": health,
                "observation_available": observation_available,
                "legal_actions_available": observation_available,
                "phase": own_phase,
                "last_phase_end": last_phase_end,
                "last_recovery": (
                    dict(self.v2_last_recovery[place_number])
                    if place_number in self.v2_last_recovery else None
                ),
            }

    def _v2_wait_response(
        self,
        agent_id: str,
        wake_reason: str,
        health: dict[str, Any],
        state_revision: dict[str, Any] | None,
    ) -> dict[str, Any]:
        if wake_reason not in V2_WAIT_REASONS:
            raise RuntimeError("invalid private wait wake reason")
        return {
            "schema_version": 2,
            "control_protocol": FULL_CONTROL_V2,
            "game_id": self.game_id,
            "agent_id": agent_id,
            "wake_reason": wake_reason,
            "health": health,
            "state_revision": (
                None if state_revision is None else dict(state_revision)
            ),
        }

    def v2_wait(
        self,
        agent_id: str,
        wait_s: float,
        *,
        until: str = "phase",
        after_state_token: str | None = None,
    ) -> dict[str, Any]:
        """Long-poll caller-private readiness without spectator state."""
        if (
            isinstance(wait_s, bool)
            or not isinstance(wait_s, (int, float))
            or not math.isfinite(wait_s)
            or not 0 <= wait_s <= 300
            or until not in {"phase", "revision"}
            or until == "phase" and after_state_token is not None
            or until == "revision" and (
                not isinstance(after_state_token, str)
                or V2_STATE_TOKEN_RE.fullmatch(after_state_token) is None
            )
        ):
            raise self._v2_problem(
                HTTPStatus.BAD_REQUEST,
                "invalid_request",
                "the full-control-v2 wait request is invalid",
                retryable=False,
            )
        deadline = time.monotonic() + float(wait_s)
        latest_revision: dict[str, Any] | None = None
        entry_generation = health_generation = None
        with self.condition:
            agent = self.agents.get(agent_id)
            if agent is not None:
                entry_generation = self.sidecar_generations.get(agent["place"])
        while True:
            health = self.v2_health(agent_id)
            if health["game_state"] in TERMINAL_STATES:
                return self._v2_wait_response(
                    agent_id, "game_terminal", health, latest_revision,
                )
            sidecar_health = health.get("sidecar")
            health_generation = (
                sidecar_health.get("generation")
                if isinstance(sidecar_health, dict) else None
            )
            if (
                entry_generation is not None
                and isinstance(health_generation, int)
                and health_generation > entry_generation
                and health["observation_available"] is True
            ):
                # The seat this caller was waiting on was recovered underneath
                # it.  Say so instead of reporting a phase or revision change,
                # because every id it holds expired with the old generation.
                return self._v2_wait_response(
                    agent_id, "boundary_recovered", health, latest_revision,
                )
            if until == "phase":
                phase = health["phase"]
                if (
                    isinstance(phase, dict)
                    and phase.get("active") is True
                    and phase.get("state") == "awaiting_agent"
                    and health["observation_available"] is True
                ):
                    return self._v2_wait_response(
                        agent_id, "phase_active", health, latest_revision,
                    )
            elif health["observation_available"] is True:
                try:
                    overview = self.v2_get_page(
                        agent_id, "state", "section=overview&limit=16",
                    )
                except APIProblem as exc:
                    error = (
                        exc.payload.get("error")
                        if isinstance(exc.payload, dict) else None
                    )
                    if not (
                        isinstance(error, dict)
                        and error.get("code") in {
                            "rate_limited", "sidecar_unavailable",
                        }
                        and error.get("retryable") is True
                    ):
                        raise
                else:
                    latest_revision = overview["state_revision"]
                    if latest_revision["state_token"] != after_state_token:
                        return self._v2_wait_response(
                            agent_id,
                            "revision_changed",
                            health,
                            latest_revision,
                        )
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return self._v2_wait_response(
                    agent_id, "timeout", health, latest_revision,
                )
            with self.condition:
                self.condition.wait(min(remaining, 0.25))

    def _v2_problem(
        self, status: int, code: str, message: str, *, retryable: bool,
        details: dict[str, Any] | None = None,
    ) -> APIProblem:
        return APIProblem(
            status,
            message,
            structured_error(
                code, message, retryable=retryable, details=details,
            ),
        )

    def _v2_unavailable(self) -> APIProblem:
        with self.condition:
            retryable = self.state not in TERMINAL_STATES
        return self._v2_problem(
            HTTPStatus.SERVICE_UNAVAILABLE,
            "sidecar_unavailable",
            "the full-control-v2 sidecar is unavailable",
            retryable=retryable,
            details={"rejection": rejection("runtime", "seat_unavailable")},
        )

    def _v2_context_current_locked(
        self,
        agent_id: str,
        place_number: int,
        generation: int,
        sidecar: Any,
        control: V2SeatControl,
    ) -> bool:
        return bool(
            self._v2_seat_runtime_active_locked(
                place_number, generation, sidecar,
                agent_id=agent_id, control=control,
            )
            and getattr(sidecar, "generation", None) == generation
        )

    def _resolve_v2_control(
        self, agent_id: str,
    ) -> tuple[int, int, Any, V2SeatControl]:
        """Capture one exact seat generation without performing sidecar I/O."""
        with self.condition:
            agent = self.agents.get(agent_id)
            if agent is None:
                raise self._v2_problem(
                    HTTPStatus.FORBIDDEN,
                    "invalid_request",
                    "agent authentication failed",
                    retryable=False,
                )
            place_number = agent["place"]
            generation = self.sidecar_generations.get(place_number, 0)
            sidecar = self.sidecars.get(place_number)
            control = self.v2_controls.get(place_number)
            if (
                sidecar is None or control is None
                or not self._v2_context_current_locked(
                    agent_id, place_number, generation, sidecar, control,
                )
            ):
                raise self._v2_unavailable()
            ready_allowed = (
                self.state != "lobby"
                or self._v2_pregame_gate_current_locked()
            )
        control.set_pregame_ready_allowed(ready_allowed)
        self._require_v2_context(
            agent_id, place_number, generation, sidecar, control,
        )
        return place_number, generation, sidecar, control

    def _require_v2_context(
        self,
        agent_id: str,
        place_number: int,
        generation: int,
        sidecar: Any,
        control: V2SeatControl,
    ) -> None:
        with self.condition:
            current = self._v2_context_current_locked(
                agent_id, place_number, generation, sidecar, control,
            )
        if not current:
            raise self._v2_unavailable()

    @staticmethod
    def _v2_query(
        raw_query: str, endpoint: str,
    ) -> tuple[
        str | None, str, int, str | None, str | None, str | None,
        str | None, int | None,
    ]:
        if not isinstance(raw_query, str) or not raw_query.isascii():
            raise V2ControlError("invalid_request")
        query: dict[str, str] = {}
        if raw_query:
            components = raw_query.split("&")
            if (
                len(components) > 5
                or any(not component for component in components)
            ):
                raise V2ControlError("invalid_request")
            for component in components:
                if component.count("=") != 1:
                    raise V2ControlError("invalid_request")
                name, value = component.split("=", 1)
                if (
                    name not in {
                        "actor_id", "target_id", "relation_id", "center_id", "radius",
                        "cursor", "section", "limit",
                    }
                    or name in query
                    or not value
                ):
                    raise V2ControlError("invalid_request")
                query[name] = value
        if "cursor" in query:
            if (
                set(query) != {"cursor"}
                or V2_CURSOR_RE.fullmatch(query["cursor"]) is None
            ):
                raise V2ControlError("invalid_request")
            return (
                query["cursor"], "", MAX_PAGE_ITEMS, None, None, None,
                None, None,
            )

        if endpoint == "legal_actions" and set(query) in ({
            "actor_id", "target_id",
        }, {
            "actor_id", "target_id", "limit",
        }):
            actor_id = query["actor_id"]
            target_id = query["target_id"]
            if (
                V2_ACTOR_ID_RE.fullmatch(actor_id) is None
                or (
                    V2_TILE_ID_RE.fullmatch(target_id) is None
                    and V2_RELATION_ID_RE.fullmatch(target_id) is None
                )
            ):
                raise V2ControlError("invalid_request")
            if (
                "limit" in query
                and V2_RELATION_ID_RE.fullmatch(target_id) is not None
            ):
                raise V2ControlError("invalid_request")
            raw_limit = query.get("limit", str(MAX_PAGE_ITEMS))
            if re.fullmatch(r"(?:[1-9]|1[0-6])", raw_limit) is None:
                raise V2ControlError("invalid_request")
            return (
                None, "legal_actions", int(raw_limit), actor_id, target_id,
                None, None, None,
            )

        section = query.get("section", "overview")
        if endpoint == "state" and section not in V2_STATE_SECTIONS:
            raise V2ControlError("invalid_request")
        raw_limit = query.get("limit", str(MAX_PAGE_ITEMS))
        if re.fullmatch(r"(?:[1-9]|1[0-6])", raw_limit) is None:
            raise V2ControlError("invalid_request")
        actor_id = query.get("actor_id")
        if actor_id is not None and V2_ACTOR_ID_RE.fullmatch(actor_id) is None:
            raise V2ControlError("invalid_request")
        if endpoint == "legal_actions":
            if set(query) - {"actor_id", "limit"}:
                raise V2ControlError("invalid_request")
            return None, section, int(raw_limit), actor_id, None, None, None, None

        if section == "diplomacy_clauses":
            relation_id = query.get("relation_id")
            if (
                set(query) - {"section", "relation_id", "limit"}
                or relation_id is None
                or V2_RELATION_ID_RE.fullmatch(relation_id) is None
            ):
                raise V2ControlError("invalid_request")
            return (
                None, section, int(raw_limit), None, None, relation_id,
                None, None,
            )

        if section in V2_CITY_STATE_SECTIONS:
            if (
                set(query) - {"section", "actor_id", "limit"}
                or actor_id is None or not actor_id.startswith("city_")
            ):
                raise V2ControlError("invalid_request")
            return (
                None, section, int(raw_limit), actor_id, None, None,
                None, None,
            )
        if section == "unit_route":
            if (
                set(query) - {"section", "actor_id", "limit"}
                or actor_id is None or not actor_id.startswith("unit_")
            ):
                raise V2ControlError("invalid_request")
            return (
                None, section, int(raw_limit), actor_id, None, None,
                None, None,
            )
        if section == "tile_window":
            center_id = query.get("center_id")
            raw_radius = query.get("radius")
            if (
                set(query) - {"section", "center_id", "radius", "limit"}
                or center_id is None
                or V2_TILE_ID_RE.fullmatch(center_id) is None
                or raw_radius is None
                or re.fullmatch(r"[0-8]", raw_radius) is None
            ):
                raise V2ControlError("invalid_request")
            return (
                None, section, int(raw_limit), None, None, None,
                center_id, int(raw_radius),
            )
        if set(query) - {"section", "limit"}:
            raise V2ControlError("invalid_request")
        return None, section, int(raw_limit), None, None, None, None, None

    def _raise_v2_get_error(self, exc: Exception) -> None:
        if isinstance(exc, APIProblem):
            raise exc
        if isinstance(exc, V2ControlError):
            if exc.code == "cursor_expired":
                raise self._v2_problem(
                    HTTPStatus.GONE,
                    "cursor_expired",
                    "the full-control-v2 cursor expired; restart its query",
                    retryable=True,
                    details=exc.details,
                ) from exc
            if exc.code == "cursor_in_progress":
                raise self._v2_problem(
                    HTTPStatus.TOO_MANY_REQUESTS,
                    "rate_limited",
                    "the full-control-v2 cursor continuation is in progress",
                    retryable=True,
                ) from exc
            if exc.code == "rate_limited":
                wait = exc.details.get("retry_after_seconds")
                raise self._v2_problem(
                    HTTPStatus.TOO_MANY_REQUESTS,
                    "rate_limited",
                    "the full-control-v2 cursor registry is at capacity; "
                    + (
                        f"retry in {wait}s, when the earliest cursor expires"
                        if isinstance(wait, int)
                        else "retry after an existing cursor expires"
                    ),
                    retryable=True,
                    details=exc.details,
                ) from exc
            if exc.code == "scope_too_large":
                raise self._v2_problem(
                    HTTPStatus.REQUEST_ENTITY_TOO_LARGE,
                    "scope_too_large",
                    "the full-control-v2 page exceeds the public byte limit",
                    retryable=False,
                ) from exc
            if exc.code == "invalid_request":
                raise self._v2_problem(
                    HTTPStatus.BAD_REQUEST,
                    "invalid_request",
                    "the full-control-v2 request is invalid",
                    retryable=False,
                ) from exc
            if exc.code == "sidecar_unavailable":
                raise self._v2_unavailable() from exc
            if exc.code == "stale_revision":
                raise self._v2_problem(
                    HTTPStatus.CONFLICT,
                    "stale_revision",
                    "the full-control-v2 state revision is stale",
                    retryable=True,
                    details=exc.details,
                ) from exc
            raise self._v2_problem(
                HTTPStatus.INTERNAL_SERVER_ERROR,
                "internal_error",
                "the full-control-v2 request could not be completed",
                retryable=False,
            ) from exc
        if isinstance(exc, SidecarError):
            if exc.code == "native_busy":
                raise self._v2_problem(
                    HTTPStatus.TOO_MANY_REQUESTS,
                    "rate_limited",
                    "the full-control-v2 sidecar is busy",
                    retryable=True,
                ) from exc
            if exc.code == "protocol_error":
                raise self._v2_problem(
                    HTTPStatus.BAD_GATEWAY,
                    "internal_error",
                    "the native control channel returned an invalid frame; "
                    "private diagnostics were recorded",
                    retryable=False,
                ) from exc
            if exc.code in {
                "sidecar_unavailable", "native_not_ready", "deadline_exceeded",
                "snapshot_gone", "observation_too_large", "unexpected_eof",
                "ipc_read_failed", "ipc_write_failed", "process_exited",
            }:
                raise self._v2_unavailable() from exc
            if exc.code in {"invalid_actor", "invalid_relation"}:
                raise self._v2_problem(
                    HTTPStatus.BAD_REQUEST,
                    "invalid_request",
                    "the full-control-v2 request is invalid",
                    retryable=False,
                ) from exc
            if exc.code == "stale_revision":
                raise self._v2_problem(
                    HTTPStatus.CONFLICT,
                    "stale_revision",
                    "the full-control-v2 state revision is stale",
                    retryable=True,
                ) from exc
            if exc.code == "actor_scope_too_large":
                raise self._v2_problem(
                    HTTPStatus.REQUEST_ENTITY_TOO_LARGE,
                    "scope_too_large",
                    "the actor legal-action scope exceeds the bounded limit",
                    retryable=False,
                ) from exc
            if exc.code == "relation_scope_too_large":
                raise self._v2_problem(
                    HTTPStatus.REQUEST_ENTITY_TOO_LARGE,
                    "scope_too_large",
                    "the diplomatic relation legal-action scope exceeds the bounded limit",
                    retryable=False,
                ) from exc
            if exc.code == "state_scope_too_large":
                raise self._v2_problem(
                    HTTPStatus.REQUEST_ENTITY_TOO_LARGE,
                    "scope_too_large",
                    "the native state scope exceeds the bounded limit",
                    retryable=False,
                ) from exc
            if exc.code == "scope_gone":
                raise self._v2_problem(
                    HTTPStatus.CONFLICT,
                    "stale_revision",
                    "the legal-action scope expired; restart the scoped query",
                    retryable=True,
                ) from exc
        raise self._v2_problem(
            HTTPStatus.INTERNAL_SERVER_ERROR,
            "internal_error",
            "the full-control-v2 request could not be completed",
            retryable=False,
        ) from exc

    @staticmethod
    def _read_v2_observation_bundle(
        sidecar: Any, control: V2SeatControl,
        *, on_terminal_error: Callable[[Exception], None] | None = None,
    ) -> Mapping[str, Any]:
        """Read compact OBS and fully drain its same-revision entity scopes."""
        read_arguments: dict[str, Any] = {
            "timeout_s": V2_OBSERVATION_TIMEOUT_S,
        }
        if on_terminal_error is not None:
            read_arguments["on_terminal_error"] = on_terminal_error
        observation = sidecar.read_observation(
            f"obs_{secrets.token_urlsafe(18)}", **read_arguments,
        )
        catalogs: dict[str, Mapping[str, Any]] = {}
        for request in control.prepare_observation_scopes(observation):
            catalogs[request.section] = sidecar.read_state_scope_catalog(
                f"state_{secrets.token_urlsafe(18)}",
                request.native_revision,
                request.section,
                request.selector,
                timeout_s=V2_SCOPE_MATERIALIZATION_TIMEOUT_S,
            )
        return control.materialize_observation_catalogs(
            observation, catalogs,
        )

    def _read_v2_post_result_observation_bundle(
        self, sidecar: Any, control: V2SeatControl,
        *, on_terminal_error: Callable[[Exception], None] | None = None,
    ) -> Mapping[str, Any]:
        """Retry only the brief native-AI handoff after an action result."""
        deadline = time.monotonic() + V2_POST_RESULT_OBSERVATION_RETRY_S
        while True:
            try:
                return self._read_v2_observation_bundle(sidecar, control)
            except SidecarError as exc:
                if (
                    exc.code == "native_not_ready"
                    and time.monotonic() < deadline
                ):
                    time.sleep(V2_POST_RESULT_OBSERVATION_RETRY_INTERVAL_S)
                    continue
                if on_terminal_error is not None:
                    on_terminal_error(exc)
                raise

    @staticmethod
    def _acquire_v2_read_lock(execution_lock: threading.Lock) -> None:
        """Wait briefly for the seat, and only then refuse retryably.

        Reads and mutations are both strictly serialized through this lock
        already, so a read arriving second has to wait either way; the only
        question is whether it waits or is turned away.  Refusing on sight
        made every overlap -- an agent's own follow-up call, a background
        probe, a receipt being finished -- a 429 the agent had to notice and
        retry, and the work it was refused for typically completed in single
        milliseconds.  Waiting a bounded moment for it is strictly cheaper
        than a round trip.  The bound is what keeps this from queueing stale
        work behind a seat that has genuinely stopped answering: past it, the
        answer would be too old to be worth the wait, so say busy.
        """
        if not execution_lock.acquire(timeout=V2_READ_LOCK_WAIT_S):
            raise SidecarError("native_busy")

    @staticmethod
    def _v2_boundary_failure_is_unattributable(exc: Exception) -> bool:
        """Whether one failure carries the signature of a wedged boundary.

        Every refusal this service can attribute names its layer and says
        whether to retry: a stale revision, an expired capability, a busy or
        departed sidecar, an illegal action.  Only a projection that the
        boundary keeps producing and the projector keeps refusing arrives with
        no attribution at all, as a bare internal error.  Those, and only
        those, are evidence.
        """
        if isinstance(exc, V2ControlError):
            return exc.code == "internal_error"
        return not isinstance(exc, (APIProblem, SidecarError))

    def _v2_place_for_agent(self, agent_id: str) -> int | None:
        with self.condition:
            agent = self.agents.get(agent_id)
            return agent["place"] if agent is not None else None

    def _note_v2_boundary_outcome(
        self,
        place: int | None,
        *,
        ok: bool,
        trigger: str = "boundary_internal_error",
    ) -> None:
        """Record one boundary outcome and start recovery once it is wedged."""
        detected: dict[str, Any] | None = None
        with self.condition:
            if (
                place is None
                or self.config["control_protocol"] != FULL_CONTROL_V2
            ):
                return
            if ok:
                # A command the client answered is the same liveness fact a
                # STATUS probe would have gone to fetch, sampled at no cost.
                # It is evidence about this generation of the seat only.
                self.v2_last_agent_command[place] = (
                    time.monotonic(), self.sidecar_generations.get(place, 0),
                )
                self.v2_wedge_detector.note_success(place)
                return
            if (
                place in self.v2_wedged_places
                or place in self.v2_recovery_in_flight
                or self.state in TERMINAL_STATES
                or self.cancel_requested
                or self.sidecars_stopping
                or self.server_exit_observed
            ):
                return
            if not self.v2_wedge_detector.note_failure(place):
                return
            detected = {
                "trigger": trigger,
                "turn": self._current_turn_locked() or 1,
                "detected_at": time.time(),
                "generation": self.sidecar_generations.get(place, 0),
            }
            self.v2_wedged_places[place] = detected
            self.v2_wedge_detector.clear(place)
            self.condition.notify_all()
        self._start_v2_boundary_recovery(place, detected)

    def _note_v2_ambiguous_observation(self, place: int | None) -> None:
        """Credit an unavailable post-result read toward a wedge proof.

        The ambiguous receipt itself is correct behaviour and must never be
        enough on its own, so this only shortens the proof: the next boundary
        failure with no successful read in between completes it.
        """
        with self.condition:
            if (
                place is None
                or self.config["control_protocol"] != FULL_CONTROL_V2
                or place in self.v2_wedged_places
            ):
                return
            self.v2_wedge_detector.note_ambiguous_observation(place)

    def _v2_recovery_journal_handle(self) -> V2RecoveryJournal | None:
        with self.condition:
            if self.v2_recovery_journal is not None:
                return self.v2_recovery_journal
            if self.v2_recovery_journal_failed:
                return None
        try:
            journal = V2RecoveryJournal(self.episode, game_id=self.game_id)
        except V2RecoveryError:
            with self.condition:
                self.v2_recovery_journal_failed = True
            return None
        with self.condition:
            if self.v2_recovery_journal is None:
                self.v2_recovery_journal = journal
                return journal
        journal.close()
        with self.condition:
            return self.v2_recovery_journal

    def _record_v2_recovery_event(self, **fields: Any) -> None:
        """Journal one rollback so scoring can flag a recovered game."""
        journal = self._v2_recovery_journal_handle()
        if journal is None:
            return
        try:
            record = journal.record(**fields)
        except V2RecoveryError:
            with self.condition:
                self.v2_recovery_journal_failed = True
            return
        with self.condition:
            self.v2_last_recovery[record["place"]] = record
            # Accumulate only; the manifest is rewritten by whichever thread
            # next changes game state, and always at termination.  Writing it
            # from this daemon thread would race an episode directory that is
            # being finalized.
            self._note_v2_recovery_in_summary_locked(record)
            self.condition.notify_all()

    def _note_v2_recovery_in_summary_locked(self, record: dict[str, Any]) -> None:
        """Carry the fact of a recovery all the way to the scorer.

        A game that discarded real applied turns must never be ranked against
        one that never faulted.  The journal alone could not say so: nothing
        in the manifest, the result or the episode summary read it.
        """
        summary = self.v2_recovery_summary
        summary["attempts"] += 1
        kind = record["kind"]
        summary["by_kind"][kind] = summary["by_kind"].get(kind, 0) + 1
        summary["by_outcome"][record["outcome"]] = (
            summary["by_outcome"].get(record["outcome"], 0) + 1
        )
        if record["outcome"] != "recovered":
            return
        target = record["recovered_to_turn"]
        if isinstance(target, int) and target not in summary["recovered_to_turns"]:
            summary["recovered_to_turns"].append(target)
            summary["recovered_to_turns"].sort()
        if record["rewound_applied_actions"]:
            summary["rewound_applied_actions"] = True
            # Turns of real play were discarded.  That is not an invalid
            # harness run, but it is not a clean game either, and a scorer
            # that cannot see the difference is being lied to.
            if "v2_game_rewound" not in self.invalid_reasons:
                self.invalid_reasons.append("v2_game_rewound")

    def _v2_recovery_manifest_locked(self) -> dict[str, Any] | None:
        summary = self.v2_recovery_summary
        if not summary["attempts"]:
            return None
        return {
            "attempts": summary["attempts"],
            "by_kind": dict(summary["by_kind"]),
            "by_outcome": dict(summary["by_outcome"]),
            "rewound_applied_actions": summary["rewound_applied_actions"],
            "recovered_to_turns": list(summary["recovered_to_turns"]),
        }

    def _start_v2_boundary_recovery(
        self, place: int, detected: dict[str, Any],
    ) -> None:
        threading.Thread(
            target=self._run_v2_boundary_recovery,
            args=(place, detected),
            name=f"freeciv-v2-recovery-{self.game_id}-{place}",
            daemon=True,
        ).start()

    def _fail_v2_wedged_game(self, place: int, reason: str) -> None:
        """End a game whose boundary cannot be recovered, naming why."""
        with self.condition:
            if self.state in TERMINAL_STATES:
                return
            # A cancel or a normal game-over teardown latches long before the
            # monitor classifies it, so "not terminal yet" is not the same as
            # "still playable".  Overwriting a cancelled game's reason with a
            # wedge would rewrite an owner's cancel, or a completed game, as a
            # harness failure -- the exact mis-attribution this campaign is
            # about.
            if self.cancel_requested or self.sidecars_stopping:
                return
            self.error = reason
            if "v2_boundary_wedged" not in self.invalid_reasons:
                self.invalid_reasons.append("v2_boundary_wedged")
            self.state = "failed"
            self.finished_at = time.time()
            self._terminalize_v2_phase_locked("failed")
            self._write_manifest()
            self.condition.notify_all()
        self._stop_all_sidecars()
        self._terminate_child()

    def _run_v2_boundary_recovery(
        self, place: int, detected: dict[str, Any],
    ) -> None:
        """Escape one wedged boundary, or end the game saying it could not.

        Two tiers, in cost order.  A boundary that is broken only inside its
        client is fixed by a fresh sidecar generation against the same live
        server, which discards no play at all.  Only once that has been tried
        is it worth rewinding the game to the last autosave that predates
        whatever produced the wedge.
        """
        turn = detected["turn"]
        trigger = detected["trigger"]
        forensics = detected.get("forensics") or {}
        # Bounded scalars only: the journal record is size-capped, so the log
        # tails stay in the owner-private exit diagnostic beside it.
        exit_evidence = {
            "exit_code": (
                forensics.get("exit_code")
                if isinstance(forensics.get("exit_code"), int) else None
            ),
            "exit_signal": (
                forensics.get("exit_signal")
                if isinstance(forensics.get("exit_signal"), int) else None
            ),
            "client_state": (
                forensics.get("client_state")
                if isinstance(forensics.get("client_state"), str) else None
            ),
        }
        seat_id = self.places[place - 1].seat_id
        with self.condition:
            if place not in self.v2_wedged_places:
                return
            attempt = self.v2_recovery_budget.next_attempt(turn, place)
            if attempt is None:
                reason = self.v2_recovery_budget.exhausted_reason(turn, place)
            else:
                reason = None
                kind = recovery_kind_for_attempt(attempt)
                self.v2_recovery_in_flight[place] = {
                    "kind": kind,
                    "attempt": attempt,
                    "turn": turn,
                    "started_at": time.time(),
                    "target_turn": None,
                }
                self.condition.notify_all()
        if reason is not None:
            self._record_v2_recovery_event(
                place=place, seat_id=seat_id, turn=turn,
                attempt=max(
                    1, self.v2_recovery_budget.attempts_for_turn(turn, place),
                ),
                kind=recovery_kind_for_attempt(2), trigger=trigger,
                outcome="abandoned",
                sidecar_generation=max(
                    1, self.sidecar_generations.get(place, 1),
                ),
                recovered_to_turn=None, rewound_applied_actions=False,
                **exit_evidence,
            )
            if trigger == "sidecar_exit":
                reason += (
                    "; the seat's client "
                    + self._v2_forensic_summary(forensics)
                    + ", lost at "
                    + self._v2_death_context_summary(
                        detected.get("death_context") or {},
                    )
                )
            self._fail_v2_wedged_game(place, reason)
            return

        recovered_to_turn: int | None = None
        rewound = False
        outcome = "failed"
        try:
            # One seat at a time.  Two concurrent rebuilds can have one seat
            # calling start_and_take() against the very server the other is
            # terminating, and two concurrent rollbacks would each replace the
            # server the other just launched.
            with self.v2_recovery_lock:
                advanced, recovered_to_turn, rewound = (
                    self._v2_recovery_run_tier(place, kind, turn)
                )
            if advanced is RECOVERY_ABANDONED:
                outcome = "abandoned"
            else:
                outcome = "recovered" if advanced else "failed"
        except Exception:
            outcome = "failed"
        finally:
            with self.condition:
                self.v2_recovery_in_flight.pop(place, None)
                generation = max(1, self.sidecar_generations.get(place, 1))
                if outcome == "recovered":
                    self.v2_wedged_places.pop(place, None)
                    self.v2_wedge_detector.clear(place)
                # An attempt that discarded no play must not consume a budget
                # whose whole purpose is to bound discarded play.
                self.v2_recovery_budget.release(
                    turn, place, kind=kind, outcome=outcome,
                )
                self.condition.notify_all()
            self._record_v2_recovery_event(
                place=place, seat_id=seat_id, turn=turn, attempt=attempt,
                kind=kind, trigger=trigger, outcome=outcome,
                sidecar_generation=generation,
                recovered_to_turn=recovered_to_turn,
                rewound_applied_actions=rewound,
                **exit_evidence,
            )
        if outcome == "recovered":
            return
        if outcome == "abandoned":
            # The attempt never ran: the game is cancelled, finished, or being
            # torn down.  Recursing here would burn the turn's attempts in
            # microseconds and rewrite an owner's cancel -- or a completed
            # game -- as a wedge failure.  The journal already says so.
            return
        # The seat is still wedged.  Re-arm detection so the next tier runs,
        # and drive it immediately rather than waiting for the agent to
        # rediscover a boundary that has already been proven dead.
        with self.condition:
            still_wedged = place in self.v2_wedged_places
        if still_wedged and self._v2_game_live():
            self._run_v2_boundary_recovery(place, detected)

    def _v2_recovery_run_tier(
        self, place: int, kind: str, turn: int,
    ) -> tuple[Any, int | None, bool]:
        """Run one escalation tier; report what it advanced and what it cost."""
        if kind != "autosave_rollback":
            return self._v2_recovery_rebuild_seat(place, kind), None, False
        selected = select_rollback_save(
            self.episode / "saves", at_or_before_turn=turn,
        )
        if selected is None:
            return False, None, False
        save_path, recovered_to_turn = selected
        with self.condition:
            in_flight = self.v2_recovery_in_flight.get(place)
            if in_flight is not None:
                in_flight["target_turn"] = recovered_to_turn
            # Freeciv writes each autosave at the start of its turn, so
            # reloading the save named for a turn in which this seat already
            # applied an action discards that action.  Its receipt stays
            # terminal and is never replayed, so the divergence has to be
            # recorded.
            rewound = self.v2_applied_turns.get(place, 0) >= recovered_to_turn
            self.condition.notify_all()

        def reload_and_rewind() -> bool:
            """Replace the server, then rewind the phase ledger.

            Both halves belong to the same window: the game moves back to
            ``recovered_to_turn`` and the consensus that describes it has to
            move back at the same time, while no seat is registered to sample
            anything in between.
            """
            if not self._v2_recovery_reload_server(save_path):
                return False
            with self.condition:
                self._v2_rewind_phase_ledger_locked(recovered_to_turn)
            return True

        # The server is replaced between tearing the old seat down and taking
        # the new one, so no registered sidecar ever outlives the server it
        # was connected to.
        advanced = self._v2_recovery_rebuild_seat(
            place, kind, before_attach=reload_and_rewind,
        )
        if advanced is True:
            advanced = self._v2_recovery_start_loaded_game()
        return advanced, recovered_to_turn, rewound

    def _v2_recovery_rebuild_seat(
        self,
        place: int,
        kind: str,
        *,
        before_attach: Callable[[], bool] | None = None,
    ) -> Any:
        """Republish one seat on the next sidecar generation.

        Action ids and cursors are generation-scoped, so every handle the agent
        cached against the wedged generation fails closed on its own once this
        returns; nothing here has to invalidate them.

        ``before_attach`` runs after the old seat is torn down and before the
        new one is taken.  Replacing the server has to happen in exactly that
        window: a sidecar still registered while its server dies is read by the
        status poller as an unexpected seat loss, which fails the whole game.
        """
        generation = self._v2_recovery_detach_seat(place)
        if generation is RECOVERY_ABANDONED:
            return RECOVERY_ABANDONED
        if generation is None:
            return False
        if before_attach is not None and not before_attach():
            # A reload that refused because the game is going away is the same
            # abandonment as a refused detach, only observed one step later.
            return False if self._v2_game_live() else RECOVERY_ABANDONED
        return self._v2_recovery_attach_seat(place, generation)

    def _v2_recovery_detach_seat(self, place: int) -> Any:
        """Retire the wedged generation and reserve the one that replaces it.

        Returns the new generation, ``RECOVERY_ABANDONED`` when the game is
        going away and this attempt must not run at all, or ``None`` when the
        seat itself cannot be detached.
        """
        with self.condition:
            if (
                self.state in TERMINAL_STATES
                or self.cancel_requested
                # A server that has already exited is being finalized by its
                # monitor, and there is nothing to come back to.
                or self.server_exit_observed
                # Neither entry point starts a recovery while the seats are
                # being torn down, so a latch set here belongs to a teardown
                # that began after this recovery did.  Taking the seat again
                # would reconnect a client to a server that is waiting for its
                # clients to leave before it can exit, and the tear-down
                # already sampled the sidecars it means to stop.
                or self.sidecars_stopping
            ):
                # None of these is a failed recovery.  They are all "there is
                # no game left to recover", and reporting them as failures is
                # what turned a normal game-over into a wedged one.
                return RECOVERY_ABANDONED
            if self.config["control_protocol"] != FULL_CONTROL_V2:
                return None
            agent_id = self.place_agents.get(place)
            if agent_id is None or place - 1 >= len(self.places):
                return None
            previous = self.sidecars.pop(place, None)
            control = self.v2_controls.pop(place, None)
            self.v2_execution_locks.pop(place, None)
            self.sidecar_ready_generations.pop(place, None)
            self.sidecar_exit_grace_generations.pop(place, None)
            generation = self.sidecar_generations.get(place, 0) + 1
            self.sidecar_generations[place] = generation
            self.v2_native_player_identities.pop(place, None)
            self.condition.notify_all()
        if control is not None:
            control.close()
        if previous is not None:
            try:
                previous.stop()
            except Exception:
                pass
        return generation

    def _v2_recovery_attach_seat(self, place: int, generation: int) -> Any:
        """Take the seat again on ``generation`` and republish it."""
        with self.condition:
            agent_id = self.place_agents.get(place)
            if (
                self.state in TERMINAL_STATES
                or self.cancel_requested
                or self.sidecars_stopping
                or self.server_exit_observed
            ):
                return RECOVERY_ABANDONED
            if (
                agent_id is None
                or place - 1 >= len(self.places)
                or self.sidecar_generations.get(place) != generation
            ):
                return False
        chosen = self.places[place - 1]
        try:
            sidecar = self._make_sidecar(chosen, generation)
        except Exception:
            return False
        try:
            sidecar.start_and_take()
            fields = self._parse_sidecar_status(sidecar.status(timeout_s=5.0))
            if (
                fields.get("server") != "1" or fields.get("seat") != "ready"
                or not fields.get("state")
            ):
                raise SidecarError("seat_lost")
        except Exception:
            try:
                sidecar.stop()
            except Exception:
                pass
            return False
        health = self._sanitized_sidecar_health(sidecar, generation)
        health["client_state"] = fields["state"]
        health["server_connected"] = True
        health["seat_state"] = fields["seat"]
        with self.condition:
            abandoned = (
                self.state in TERMINAL_STATES or self.cancel_requested
                or self.sidecars_stopping
            )
            if abandoned or self.sidecar_generations.get(place) != generation:
                stale = True
            else:
                stale = False
                try:
                    self._record_v2_native_identity_locked(
                        chosen, generation, fields,
                    )
                except SidecarError:
                    stale = True
            if not stale:
                self.sidecars[place] = sidecar
                self.sidecar_health[place] = health
                self.sidecar_ready_generations[place] = generation
                new_control = V2SeatControl(
                    self.game_id, agent_id, generation,
                )
                self.v2_controls[place] = new_control
                self.v2_execution_locks[place] = (
                    generation, new_control, threading.Lock(),
                )
                if self.state == "starting":
                    # Without its own grace the new generation is measured
                    # against the old deadline and trips startup_timeout at
                    # once, spending a recovery attempt on nothing.
                    self.sidecar_start_deadline = (
                        time.monotonic() + V2_SIDECAR_STARTUP_GRACE_S
                    )
                # The rebuilt boundary has to re-agree on the current turn and
                # phase before any seat may act on it again.
                self.v2_phase_ledger["evidence"].pop(place, None)
                self.v2_phase_ledger["state"] = "synchronizing"
                self.v2_phase_ledger["synchronizing_started_monotonic"] = (
                    time.monotonic()
                )
                # The reconcile clock has to restart here for the same reason,
                # and here specifically: a phase end whose seat died mid-
                # transition has been unreconciled for the whole detection and
                # recovery window, and the rebuilt boundary deserves the full
                # allowance to reconcile it rather than whatever is left of an
                # allowance the dead client spent.  Holding the clock only
                # while recovery is registered is not enough, because nothing
                # guarantees a poll lands inside that window.
                end = self.v2_phase_ledger.get("end")
                if isinstance(end, dict) and end.get(
                    "reconcile_started_monotonic",
                ) is not None:
                    end["reconcile_started_monotonic"] = time.monotonic()
                self.condition.notify_all()
        if stale:
            try:
                sidecar.stop()
            except Exception:
                pass
            return RECOVERY_ABANDONED if abandoned else False
        return True

    def _v2_recovery_start_loaded_game(self) -> bool:
        """Resume a reloaded save, which Freeciv leaves sitting in pregame.

        Loading a savegame restores its players and their turn but returns the
        server to pregame, where it waits for an explicit ``start``.  Nothing
        else in this service will send one: a full-control-v2 game is started
        once, by its seats' own native ready packets, and that happens during
        the lobby and never again.  So recovery has to start the game itself,
        without consulting the original start latch, which is why a second
        rollback attempt can still start the game after a first one failed.
        """
        with self.console_lock:
            with self.condition:
                if (
                    self.config["control_protocol"] != FULL_CONTROL_V2
                    or self.state in TERMINAL_STATES
                    or self.cancel_requested
                    or self.server_exit_observed
                    or self.process is None
                ):
                    return False
                self.start_sent = True
                self.start_count += 1
            try:
                self._send_commands(["start"], wait_for_prompt=False)
                self._append_server_commands(["start"])
            except Exception:
                return False
        with self.condition:
            self._write_manifest()
            self.condition.notify_all()
        return True

    def _append_server_commands(self, commands: list[str]) -> None:
        """Keep the console audit trail complete across a server replacement."""
        with (self.episode / "server.commands").open(
            "a", encoding="utf-8",
        ) as stream:
            for command in commands:
                stream.write(command + "\n")
            stream.flush()
            os.fsync(stream.fileno())

    def _v2_recovery_reload_server(self, save_path: Path) -> bool:
        """Restart Freeciv on a saved turn, discarding the wedging state.

        Only reached when a fresh boundary against the live server has already
        failed, which means the state the server is serving is itself what the
        boundary cannot project.  The game is otherwise already lost, so the
        cost of restarting is bounded by an outcome that was going to be a
        failure either way.
        """
        with self.condition:
            if (
                self.config["control_protocol"] != FULL_CONTROL_V2
                or self.state not in {"running", "starting"}
                or self.cancel_requested or self.server_exit_observed
            ):
                return False
            previous = self.process
            previous_output = self.output_thread
            # Every OTHER seat's client is about to lose its server.  That is
            # this recovery working, not a seat loss, so say so explicitly:
            # `process is None` is an absence that the exit path reads as "the
            # server already went away on its own", which skips the completion
            # grace and starts a competing recovery per surviving seat.
            self.v2_server_replacing = True
            # Disowning the process first makes the running monitor thread
            # recognize its server as intentionally replaced, so the restart
            # cannot be mistaken for the end of the game.
            self.process = None
            self.output_thread = None
            self.monitor_thread = None
            # The retired server's last prompt is not the new server's prompt.
            # Leaving the flag set would let the reload's first wait return on
            # a console that no longer exists.
            self.at_prompt = False
            self.condition.notify_all()
        if previous is not None:
            try:
                if previous.stdin is not None:
                    previous.stdin.close()
            except OSError:
                pass
            try:
                if previous.poll() is None:
                    previous.terminate()
            except OSError:
                pass
            try:
                previous.wait(timeout=10)
            except Exception:
                try:
                    previous.kill()
                except Exception:
                    pass
            # Exactly one output pump may own the shared console state.  The
            # retired pump is still draining whatever the dead server left in
            # the pipe, and every byte of it would otherwise be recorded as
            # the new server's output: its prompts, its turn markers, its
            # timeout acknowledgements.
            if previous_output is not None:
                previous_output.join(timeout=5)
        try:
            try:
                self._launch_from_save(save_path)
            except Exception:
                return False
            # The game can be failed by any other thread while a server is
            # being brought up, and the terminalization that did it ran when
            # there was no process to terminate.  Nothing would ever reap this
            # one.
            if not self._v2_game_live():
                self._terminate_child()
                return False
            return True
        finally:
            # Cleared on every path, including failure: leaving the latch set
            # would suppress a genuine seat loss for the rest of the game.
            with self.condition:
                self.v2_server_replacing = False
                self.condition.notify_all()

    def _launch_from_save(self, save_path: Path) -> None:
        """Bring up a Freeciv server on an existing savegame.

        A loaded save already carries its players, their human/AI assignment
        and its ruleset, so none of the pregame construction commands are
        replayed.  Only the settings this harness depends on are re-asserted.
        """
        commands = [
            "set timeout 0",
            "set first_timeout 0",
            "set autotoggle disabled",
            "set phasemode PLAYER",
            "set fixedlength disabled",
            "set turnblock disabled",
            f"set endturn {self.config['turns']}",
            "set saveturns 1",
            "set autosaves turn|gameover",
            "set savename turn-%04T-%R",
        ]
        command = [
            str(self.supervisor.binary),
            "--Announce", "none",
            "--bind", "127.0.0.1",
            "--port", str(self.freeciv_port),
            "--exit-on-end",
            "--file", str(save_path),
            "--saves", str(self.episode / "saves"),
            "--log", str(self.episode / "server.log"),
        ]
        process = self.supervisor.process_factory(
            command,
            cwd=self.episode,
            env=self._process_environment(""),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=0,
        )
        with self.condition:
            self.process = process
            self.server_exit_observed = False
            self.condition.notify_all()
        self.output_thread = threading.Thread(
            target=self._pump_output,
            name=f"freeciv-output-{self.game_id}",
            daemon=True,
        )
        self.output_thread.start()
        self._wait_for_prompt()
        self._send_commands(commands)
        # Appended, never rewritten: the journal has to show the original
        # pregame construction and every command a recovery sent after it.
        self._append_server_commands(
            [f"# reload {save_path.name}", *commands],
        )
        self.monitor_thread = threading.Thread(
            target=self._monitor, args=(process,),
            name=f"freeciv-monitor-{self.game_id}",
            daemon=True,
        )
        self.monitor_thread.start()

    def v2_get_page(
        self, agent_id: str, endpoint: str, raw_query: str,
    ) -> dict[str, Any]:
        """Return one authenticated public page without leaking native state."""
        try:
            if endpoint not in {"state", "legal_actions"}:
                raise V2ControlError("invalid_request")
            (
                cursor, section, limit, actor_id, target_id, relation_id,
                center_id, radius,
            ) = self._v2_query(
                raw_query, endpoint,
            )
            if target_id is not None:
                batch_context = self._resolve_v2_batch_context(agent_id)
                place_number, generation, sidecar, control, execution_lock = (
                    batch_context
                )
                self._acquire_v2_read_lock(execution_lock)
                try:
                    assert actor_id is not None
                    observation = self._read_v2_observation_bundle(
                        sidecar, control,
                    )
                    if V2_RELATION_ID_RE.fullmatch(target_id) is not None:
                        support_request = control.prepare_relation_support_scope(
                            observation, target_id,
                        )
                        support_catalog = sidecar.read_state_scope_catalog(
                            f"state_{secrets.token_urlsafe(18)}",
                            support_request.native_revision,
                            support_request.section,
                            support_request.selector,
                            timeout_s=V2_SCOPE_MATERIALIZATION_TIMEOUT_S,
                        )
                        control.hydrate_state_scope(
                            support_request, support_catalog,
                        )
                        relation_request = control.prepare_relation_scope(
                            observation, actor_id, target_id,
                        )
                        native_result = sidecar.read_relation_scope_catalog(
                            f"rel_{secrets.token_urlsafe(18)}",
                            relation_request.native_revision,
                            relation_request.native_actor_ref,
                            relation_request.native_counterpart_ref,
                            timeout_s=V2_SCOPE_MATERIALIZATION_TIMEOUT_S,
                        )
                        page = control.materialize_relation_scope(
                            relation_request, native_result,
                        )
                    else:
                        target_request = control.prepare_target_action(
                            observation, actor_id, target_id, limit,
                        )
                        if (
                            target_request.actor_kind == "player"
                            or target_request.action_decision
                        ):
                            tile_request = control.prepare_target_tile_support(
                                target_request,
                            )
                            tile_catalog = sidecar.read_state_scope_catalog(
                                f"state_{secrets.token_urlsafe(18)}",
                                tile_request.native_revision,
                                tile_request.section,
                                tile_request.selector,
                                timeout_s=V2_SCOPE_MATERIALIZATION_TIMEOUT_S,
                            )
                            control.hydrate_state_scope(
                                tile_request, tile_catalog,
                            )
                        native_result = sidecar.read_target_action(
                            f"tgt_{secrets.token_urlsafe(18)}",
                            target_request.native_revision,
                            target_request.native_actor_ref,
                            target_request.native_target_tile,
                            timeout_s=V2_SCOPE_MATERIALIZATION_TIMEOUT_S,
                        )
                        page = control.target_action_page(
                            target_request, native_result,
                        )
                finally:
                    execution_lock.release()
            elif endpoint == "state" and cursor is None:
                if section in {
                    "known_tiles", "map_tiles", "tile_window",
                    "diplomacy_clauses",
                    "city_citizens",
                    "city_build_choices", "city_worklist",
                    "city_improvements", "city_trade_routes",
                    "city_governor",
                    "pregame_nations", "pregame_styles", "pregame_teams",
                    "chat_recipients", "unit_route",
                }:
                    batch_context = self._resolve_v2_batch_context(agent_id)
                    (
                        place_number, generation, sidecar, control,
                        execution_lock,
                    ) = batch_context
                    self._acquire_v2_read_lock(execution_lock)
                    try:
                        observation = self._read_v2_observation_bundle(
                            sidecar, control,
                        )
                        state_request = control.prepare_state_scope(
                            observation, section, limit, actor_id=actor_id,
                            relation_id=relation_id,
                            center_id=center_id, radius=radius,
                        )
                        if section == "city_build_choices":
                            worklist_request = control.prepare_state_scope(
                                observation, "city_worklist", MAX_PAGE_ITEMS,
                                actor_id=actor_id,
                            )
                            worklist_catalog = sidecar.read_state_scope_catalog(
                                f"state_{secrets.token_urlsafe(18)}",
                                worklist_request.native_revision,
                                worklist_request.section,
                                worklist_request.selector,
                                timeout_s=V2_SCOPE_MATERIALIZATION_TIMEOUT_S,
                            )
                            control.hydrate_state_scope(
                                worklist_request, worklist_catalog,
                            )
                        native_catalog = sidecar.read_state_scope_catalog(
                            f"state_{secrets.token_urlsafe(18)}",
                            state_request.native_revision,
                            state_request.section,
                            state_request.selector,
                            timeout_s=V2_SCOPE_MATERIALIZATION_TIMEOUT_S,
                        )
                        page = control.materialize_state_scope(
                            state_request, native_catalog,
                        )
                    finally:
                        execution_lock.release()
                else:
                    batch_context = self._resolve_v2_batch_context(agent_id)
                    (
                        place_number, generation, sidecar, control,
                        execution_lock,
                    ) = batch_context
                    self._acquire_v2_read_lock(execution_lock)
                    try:
                        observation = self._read_v2_observation_bundle(
                            sidecar, control,
                        )
                        page = control.state_page(
                            observation, section, limit, actor_id=actor_id,
                            relation_id=relation_id,
                            center_id=center_id, radius=radius,
                        )
                    finally:
                        execution_lock.release()
            elif actor_id is None and cursor is None:
                batch_context = self._resolve_v2_batch_context(agent_id)
                place_number, generation, sidecar, control, execution_lock = (
                    batch_context
                )
                self._acquire_v2_read_lock(execution_lock)
                try:
                    observation = self._read_v2_observation_bundle(
                        sidecar, control,
                    )
                    page = control.legal_actions_page(observation, limit)
                finally:
                    execution_lock.release()
            elif endpoint != "legal_actions":
                context = self._resolve_v2_control(agent_id)
                place_number, generation, sidecar, control = context
                assert cursor is not None
                page = control.continue_page(cursor, endpoint=endpoint)
            elif cursor is not None:
                context = self._resolve_v2_control(agent_id)
                place_number, generation, sidecar, control = context
                if control.is_relation_scope_cursor(
                    cursor, endpoint=endpoint,
                ):
                    batch_context = self._resolve_v2_batch_context(agent_id)
                    (
                        place_number, generation, sidecar, control,
                        execution_lock,
                    ) = batch_context
                    self._acquire_v2_read_lock(execution_lock)
                    try:
                        relation_request = control.take_relation_scope_cursor(
                            cursor, endpoint=endpoint,
                        )
                        if relation_request is None:
                            raise V2ControlError("invalid_request")
                        if isinstance(relation_request, dict):
                            page = relation_request
                        else:
                            try:
                                native_page = sidecar.read_relation_scope_page(
                                    f"rel_{secrets.token_urlsafe(18)}",
                                    relation_request.native_view_id,
                                    relation_request.native_revision,
                                    relation_request.native_actor_ref,
                                    relation_request.native_counterpart_ref,
                                    relation_request.total_count,
                                    relation_request.offset,
                                    relation_request.limit,
                                    timeout_s=V2_OBSERVATION_TIMEOUT_S,
                                )
                                projected = control.relation_scope_page(
                                    relation_request, native_page,
                                )
                                page = control.commit_scope_cursor(
                                    cursor, relation_request, projected,
                                )
                            except Exception:
                                control.abort_scope_cursor(cursor)
                                raise
                    finally:
                        execution_lock.release()
                elif not control.is_actor_scope_cursor(
                    cursor, endpoint=endpoint,
                ):
                    page = control.continue_page(cursor, endpoint=endpoint)
                else:
                    batch_context = self._resolve_v2_batch_context(agent_id)
                    (
                        place_number, generation, sidecar, control,
                        execution_lock,
                    ) = batch_context
                    self._acquire_v2_read_lock(execution_lock)
                    try:
                        scope_request = control.take_actor_scope_cursor(
                            cursor, endpoint=endpoint,
                        )
                        if scope_request is None:
                            raise V2ControlError("invalid_request")
                        if isinstance(scope_request, dict):
                            page = scope_request
                        else:
                            try:
                                native_page = sidecar.read_actor_scope_page(
                                    f"scp_{secrets.token_urlsafe(18)}",
                                    scope_request.native_view_id,
                                    scope_request.native_revision,
                                    scope_request.native_actor_ref,
                                    scope_request.total_count,
                                    scope_request.offset,
                                    scope_request.limit,
                                    timeout_s=V2_OBSERVATION_TIMEOUT_S,
                                )
                                projected = control.actor_scope_page(
                                    scope_request, native_page,
                                )
                                page = control.commit_scope_cursor(
                                    cursor, scope_request, projected,
                                )
                            except Exception:
                                control.abort_scope_cursor(cursor)
                                raise
                    finally:
                        execution_lock.release()
            else:
                batch_context = self._resolve_v2_batch_context(agent_id)
                place_number, generation, sidecar, control, execution_lock = (
                    batch_context
                )
                self._acquire_v2_read_lock(execution_lock)
                try:
                    assert actor_id is not None
                    observation = self._read_v2_observation_bundle(
                        sidecar, control,
                    )
                    scope_request = control.prepare_actor_scope(
                        observation, actor_id, limit,
                    )
                    if scope_request.actor_kind == "city":
                        for support_request in control.prepare_city_support_scopes(
                            observation, actor_id,
                        ):
                            support_catalog = sidecar.read_state_scope_catalog(
                                f"state_{secrets.token_urlsafe(18)}",
                                support_request.native_revision,
                                support_request.section,
                                support_request.selector,
                                timeout_s=V2_SCOPE_MATERIALIZATION_TIMEOUT_S,
                            )
                            control.hydrate_state_scope(
                                support_request, support_catalog,
                            )
                    elif scope_request.actor_kind == "unit":
                        for support_request in control.prepare_unit_support_scopes(
                            observation, actor_id,
                        ):
                            support_catalog = sidecar.read_state_scope_catalog(
                                f"state_{secrets.token_urlsafe(18)}",
                                support_request.native_revision,
                                support_request.section,
                                support_request.selector,
                                timeout_s=V2_SCOPE_MATERIALIZATION_TIMEOUT_S,
                            )
                            control.hydrate_state_scope(
                                support_request, support_catalog,
                            )
                    native_page = sidecar.read_actor_scope_catalog(
                        f"scp_{secrets.token_urlsafe(18)}",
                        scope_request.native_revision,
                        scope_request.native_actor_ref,
                        timeout_s=V2_SCOPE_MATERIALIZATION_TIMEOUT_S,
                    )
                    page = control.materialize_actor_scope(
                        scope_request, native_page,
                    )
                finally:
                    execution_lock.release()
            self._require_v2_context(
                agent_id, place_number, generation, sidecar, control,
            )
            self._note_v2_boundary_outcome(
                self._v2_place_for_agent(agent_id), ok=True,
            )
            return page
        except Exception as exc:
            if self._v2_boundary_failure_is_unattributable(exc):
                self._note_v2_boundary_outcome(
                    self._v2_place_for_agent(agent_id), ok=False,
                )
            self._raise_v2_get_error(exc)
            raise AssertionError("unreachable")

    @staticmethod
    def _v2_receipt_status(receipt: dict[str, Any]) -> int:
        state = receipt.get("receipt_state")
        if state == "applied":
            return HTTPStatus.OK
        if state in {"accepted", "ambiguous"}:
            return HTTPStatus.ACCEPTED
        if state != "rejected":
            return HTTPStatus.INTERNAL_SERVER_ERROR
        error = receipt.get("error")
        code = (
            error.get("error", {}).get("code")
            if isinstance(error, dict) else None
        )
        return {
            "rate_limited": HTTPStatus.TOO_MANY_REQUESTS,
            "action_expired": HTTPStatus.GONE,
            "stale_revision": HTTPStatus.CONFLICT,
            "sidecar_unavailable": HTTPStatus.SERVICE_UNAVAILABLE,
            "illegal_action": HTTPStatus.UNPROCESSABLE_ENTITY,
            "internal_error": HTTPStatus.INTERNAL_SERVER_ERROR,
        }.get(code, HTTPStatus.UNPROCESSABLE_ENTITY)

    def _v2_receipt(
        self,
        agent_id: str,
        batch_id: str,
        state: str,
        state_revision: dict[str, Any],
        *,
        error_code: str | None = None,
        retryable: bool = False,
        observation: dict[str, Any] | None = None,
        rejection: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        error = None
        if error_code is not None:
            details: dict[str, Any] = {}
            if rejection is not None:
                details["rejection"] = rejection
                message = rejection_message(rejection)
            else:
                message = (
                    "The action outcome is unknown and the command will not "
                    "be replayed."
                    if state == "ambiguous" else "The command was rejected."
                )
            error = structured_error(
                error_code,
                message,
                retryable=retryable,
                details=details,
                state_revision=state_revision,
            )
        return {
            "schema_version": 2,
            "control_protocol": FULL_CONTROL_V2,
            "game_id": self.game_id,
            "agent_id": agent_id,
            "batch_id": batch_id,
            "receipt_state": state,
            "idempotent": False,
            "state_revision": dict(state_revision),
            "error": error,
            "observation": observation,
        }

    def _v2_receipt_store(self) -> V2ReceiptStore:
        if self.v2_receipt_store_failed:
            raise self._v2_problem(
                HTTPStatus.INTERNAL_SERVER_ERROR,
                "internal_error",
                "the command receipt store is unavailable",
                retryable=False,
            )
        store = self.v2_receipt_store
        if store is None:
            raise self._v2_problem(
                HTTPStatus.CONFLICT,
                "unsupported_protocol",
                "this game uses strategic-v1",
                retryable=False,
            )
        return store

    def _raise_v2_store_error(self, exc: Exception) -> None:
        if isinstance(exc, V2ReceiptConflict):
            raise self._v2_problem(
                HTTPStatus.CONFLICT,
                "conflict",
                "the batch ID is already bound to a different request",
                retryable=False,
                details={
                    "rejection": rejection("store", "receipt_conflict"),
                },
            ) from exc
        if isinstance(exc, V2ReceiptInvalidBatch):
            raise self._v2_problem(
                HTTPStatus.BAD_REQUEST,
                "invalid_batch",
                "the full-control-v2 command batch is invalid",
                retryable=False,
                details={"rejection": rejection("schema", "batch_malformed")},
            ) from exc
        raise self._v2_problem(
            HTTPStatus.INTERNAL_SERVER_ERROR,
            "internal_error",
            "the command receipt store is unavailable",
            retryable=False,
            details={"rejection": rejection("store", "internal_failure")},
        ) from exc

    def _v2_batch_context_current_locked(
        self,
        agent_id: str,
        place_number: int,
        generation: int,
        sidecar: Any,
        control: V2SeatControl,
        execution_lock: threading.Lock,
    ) -> bool:
        lock_record = self.v2_execution_locks.get(place_number)
        return bool(
            self._v2_context_current_locked(
                agent_id, place_number, generation, sidecar, control,
            )
            and lock_record is not None
            and lock_record[0] == generation
            and lock_record[1] is control
            and lock_record[2] is execution_lock
        )

    def _v2_pregame_gate_current_locked(self) -> bool:
        if (
            not self.v2_pregame_gate_open or self.state != "lobby"
            or len(self.place_agents) != self.max_agents
        ):
            return False
        for place in self.joinable_places:
            agent_id = self.place_agents.get(place.number)
            sidecar = self.sidecars.get(place.number)
            control = self.v2_controls.get(place.number)
            generation = self.sidecar_generations.get(place.number, 0)
            if (
                agent_id is None or sidecar is None or control is None
                or not self._v2_seat_runtime_active_locked(
                    place.number, generation, sidecar,
                    agent_id=agent_id, control=control,
                )
            ):
                return False
        return True

    def _resolve_v2_batch_context(
        self, agent_id: str,
    ) -> tuple[int, int, Any, V2SeatControl, threading.Lock]:
        with self.condition:
            agent = self.agents.get(agent_id)
            if agent is None:
                raise self._v2_problem(
                    HTTPStatus.FORBIDDEN,
                    "invalid_request",
                    "agent authentication failed",
                    retryable=False,
                )
            place_number = agent["place"]
            generation = self.sidecar_generations.get(place_number, 0)
            sidecar = self.sidecars.get(place_number)
            control = self.v2_controls.get(place_number)
            lock_record = self.v2_execution_locks.get(place_number)
            if (
                sidecar is None or control is None or lock_record is None
                or lock_record[0] != generation or lock_record[1] is not control
                or not self._v2_batch_context_current_locked(
                    agent_id, place_number, generation, sidecar, control,
                    lock_record[2],
                )
            ):
                raise self._v2_unavailable()
            ready_allowed = (
                self.state != "lobby"
                or self._v2_pregame_gate_current_locked()
            )
            result = (
                place_number, generation, sidecar, control, lock_record[2],
            )
        control.set_pregame_ready_allowed(ready_allowed)
        if not self._v2_batch_context_current(
            agent_id, place_number, generation, sidecar, control,
            lock_record[2],
        ):
            raise self._v2_unavailable()
        return result

    def _v2_batch_context_current(
        self,
        agent_id: str,
        place_number: int,
        generation: int,
        sidecar: Any,
        control: V2SeatControl,
        execution_lock: threading.Lock,
    ) -> bool:
        with self.condition:
            return self._v2_batch_context_current_locked(
                agent_id, place_number, generation, sidecar, control,
                execution_lock,
            )

    @staticmethod
    def _v2_control_rejection(
        exc: V2ControlError, layer: str, default_reason: str,
    ) -> dict[str, Any]:
        """Attribute a resolver refusal, preferring the resolver's own reason.

        ``V2SeatControl`` names the specific contract it refused in
        ``details["rejection_reason"]`` where it can.  Anything it does not
        name still gets the layer plus this call site's default, so no
        pre-dispatch refusal reaches an agent unattributed.
        """
        reason = exc.details.get("rejection_reason")
        if reason not in REJECTION_REASONS:
            reason = default_reason
        return rejection(layer, reason)

    def _raise_v2_pre_batch_error(self, exc: Exception) -> None:
        if isinstance(exc, APIProblem):
            raise exc
        if isinstance(exc, V2ControlError):
            if exc.code == "stale_revision":
                raise self._v2_problem(
                    HTTPStatus.CONFLICT,
                    "stale_revision",
                    "the requested state revision is no longer current",
                    retryable=True,
                    details={
                        "rejection": self._v2_control_rejection(
                            exc, "revision", "revision_stale",
                        ),
                    },
                ) from exc
            if exc.code == "action_expired":
                raise self._v2_problem(
                    HTTPStatus.GONE,
                    "action_expired",
                    "the requested action capability has expired",
                    retryable=True,
                    details={
                        "rejection": self._v2_control_rejection(
                            exc, "catalog", "action_not_advertised",
                        ),
                    },
                ) from exc
            if exc.code == "invalid_request":
                attribution = self._v2_control_rejection(
                    exc, "arguments", "arguments_invalid",
                )
                raise self._v2_problem(
                    HTTPStatus.UNPROCESSABLE_ENTITY,
                    "illegal_action",
                    rejection_message(attribution),
                    retryable=False,
                    details={"rejection": attribution},
                ) from exc
            if exc.code == "sidecar_unavailable":
                raise self._v2_unavailable() from exc
            raise self._v2_problem(
                HTTPStatus.INTERNAL_SERVER_ERROR,
                "internal_error",
                "the full-control-v2 command could not be resolved",
                retryable=False,
                details={
                    "rejection": rejection("preflight", "internal_failure"),
                },
            ) from exc
        if isinstance(exc, SidecarError):
            if exc.code == "native_busy":
                raise self._v2_problem(
                    HTTPStatus.TOO_MANY_REQUESTS,
                    "rate_limited",
                    "the full-control-v2 sidecar is busy",
                    retryable=True,
                    details={
                        "rejection": self._v2_native_rejection(
                            exc.code, error_code="rate_limited",
                        ),
                    },
                ) from exc
            if exc.code in {
                "sidecar_unavailable", "native_not_ready", "native_not_sent",
                "deadline_exceeded", "snapshot_gone", "observation_too_large",
                "unexpected_eof", "ipc_read_failed", "ipc_write_failed",
                "process_exited",
            }:
                raise self._v2_unavailable() from exc
        raise self._v2_problem(
            HTTPStatus.INTERNAL_SERVER_ERROR,
            "internal_error",
            "the full-control-v2 command could not be completed",
            retryable=False,
            details={"rejection": rejection("runtime", "internal_failure")},
        ) from exc

    @staticmethod
    def _v2_batch_safe_next(problem: APIProblem) -> str:
        payload = problem.payload if isinstance(problem.payload, dict) else {}
        error = payload.get("error") if isinstance(payload, dict) else None
        code = error.get("code") if isinstance(error, dict) else None
        retryable = error.get("retryable") if isinstance(error, dict) else False
        if code in {"conflict", "internal_error"}:
            return "receipt_first"
        if code in {"rate_limited", "sidecar_unavailable"} and retryable is True:
            return "retry_exact"
        return "refresh"

    def _v2_not_accepted_problem(
        self, problem: APIProblem, batch_id: str,
    ) -> APIProblem:
        """Annotate a proved pre-reservation failure without making a receipt."""
        payload = problem.payload if isinstance(problem.payload, dict) else None
        error = payload.get("error") if isinstance(payload, dict) else None
        if not isinstance(error, dict):
            return self._v2_problem(
                problem.status,
                "internal_error",
                "the full-control-v2 command could not be completed",
                retryable=False,
                details={
                    "batch_id": batch_id,
                    "acceptance": "not_accepted",
                    "safe_next": "receipt_first",
                },
            )
        details = error.get("details")
        clean_details = dict(details) if isinstance(details, dict) else {}
        clean_details.update({
            "batch_id": batch_id,
            "acceptance": "not_accepted",
            "safe_next": self._v2_batch_safe_next(problem),
        })
        return self._v2_problem(
            problem.status,
            str(error.get("code") or "internal_error"),
            str(error.get("message") or "the command was not accepted"),
            retryable=error.get("retryable") is True,
            details=clean_details,
        )

    def _raise_v2_not_accepted(
        self, exc: Exception, batch_id: str,
    ) -> None:
        try:
            self._raise_v2_pre_batch_error(exc)
        except APIProblem as problem:
            raise self._v2_not_accepted_problem(problem, batch_id) from exc
        raise AssertionError("unreachable")

    @staticmethod
    def _v2_definitive_rejection(
        code: str, *, correlated_native_rejection: bool = False,
    ) -> tuple[int, str]:
        if code == "native_busy":
            return HTTPStatus.TOO_MANY_REQUESTS, "rate_limited"
        if code in {"stale_slot", "stale_entity"}:
            return HTTPStatus.GONE, "action_expired"
        if code == "stale_revision":
            return HTTPStatus.CONFLICT, "stale_revision"
        if code in {
            "sidecar_unavailable", "native_not_ready", "native_not_sent",
            "deadline_exceeded", "command_in_progress",
        }:
            return HTTPStatus.SERVICE_UNAVAILABLE, "sidecar_unavailable"
        if not correlated_native_rejection and code not in {
            "invalid_request", "invalid_action", "invalid_argument",
            "native_bad_request", "native_bad_argument", "native_error",
        }:
            return HTTPStatus.INTERNAL_SERVER_ERROR, "internal_error"
        return HTTPStatus.UNPROCESSABLE_ENTITY, "illegal_action"

    @staticmethod
    def _v2_native_rejection(
        code: str, *, error_code: str, dispatched: bool = False,
    ) -> dict[str, Any]:
        """Attribute one native-boundary refusal to a layer and a reason.

        ``code`` is the sidecar's mapped native error token, already a closed
        vocabulary.  It is echoed as ``native_code`` so an operator can tell a
        ``BAD_ARGUMENT`` refusal from a ``NOT_READY`` one even where both
        become ``illegal_action`` on the wire.
        """
        layer = "native_dispatch" if dispatched else "native_preflight"
        reason = _V2_NATIVE_REJECTION_REASONS.get(code)
        if reason is None:
            reason = (
                "internal_failure" if error_code == "internal_error"
                else "native_refused"
            )
        native_code = code if _NATIVE_CODE_TOKEN.fullmatch(code) else None
        return rejection(layer, reason, native_code=native_code)

    def _v2_transition(
        self,
        store: V2ReceiptStore,
        reservation: ReceiptReservation,
        receipt: dict[str, Any],
    ) -> dict[str, Any]:
        try:
            return store.transition(reservation, receipt)
        except Exception as exc:
            self._raise_v2_store_error(exc)
            raise AssertionError("unreachable")

    def _v2_terminal_transition(
        self,
        store: V2ReceiptStore,
        reservation: ReceiptReservation,
        receipt: dict[str, Any],
    ) -> dict[str, Any]:
        """Persist a terminal receipt or fail closed without leaving accepted."""
        if receipt.get("receipt_state") not in {
            "applied", "rejected", "ambiguous",
        }:
            raise AssertionError("terminal receipt required")
        last_error: Exception | None = None
        for _attempt in range(2):
            try:
                return store.transition(reservation, receipt)
            except Exception as exc:
                last_error = exc
        try:
            recovered = store.recover_incomplete(reservation)
            if recovered.get("receipt_state") == receipt["receipt_state"]:
                recovered = dict(recovered)
                recovered["idempotent"] = False
            return recovered
        except Exception as exc:
            last_error = exc
        with self.condition:
            self.v2_receipt_store_failed = True
            self.error = "full-control-v2 receipt durability failed"
            if "receipt_store_failure" not in self.invalid_reasons:
                self.invalid_reasons.append("receipt_store_failure")
            self.state = "failed"
            self.finished_at = time.time()
            self._terminalize_v2_phase_locked("failed")
            try:
                self._write_manifest()
            except Exception:
                pass
            self.condition.notify_all()
        assert last_error is not None
        self._raise_v2_store_error(last_error)
        raise AssertionError("unreachable")

    def _v2_ambiguous(
        self,
        store: V2ReceiptStore,
        reservation: ReceiptReservation,
        agent_id: str,
        batch_id: str,
        state_revision: dict[str, Any],
        *,
        place: int,
        generation: int,
        sidecar: Any,
        stage: str,
        ambiguity_reason: str,
        acceptance_known: bool,
        record_trace: bool = True,
    ) -> tuple[int, dict[str, Any]]:
        receipt = self._v2_terminal_transition(
            store,
            reservation,
            self._v2_receipt(
                agent_id,
                batch_id,
                "ambiguous",
                state_revision,
                error_code="action_outcome_ambiguous",
            ),
        )
        if record_trace:
            self._record_v2_ambiguity(
                agent_id=agent_id,
                batch_id=batch_id,
                place=place,
                generation=generation,
                sidecar=sidecar,
                stage=stage,
                ambiguity_reason=ambiguity_reason,
                acceptance_known=acceptance_known,
            )
        return HTTPStatus.ACCEPTED, receipt

    def _record_v2_ambiguity(
        self,
        *,
        agent_id: str,
        batch_id: str,
        place: int,
        generation: int,
        sidecar: Any,
        stage: str,
        ambiguity_reason: str,
        acceptance_known: bool,
    ) -> None:
        trace = self.v2_ambiguity_trace
        if trace is None:
            self.v2_ambiguity_trace_warning_count += 1
            return
        health_state = "unknown"
        try:
            health = sidecar.public_health()
            candidate = health.get("state") if isinstance(health, dict) else None
            if candidate in {
                "new", "starting", "handshaking", "taking", "ready",
                "stopping", "stopped", "failed",
            }:
                health_state = candidate
        except Exception:
            pass
        try:
            seat_id = self.places[place - 1].seat_id
            trace.record(
                agent_id=agent_id,
                batch_id=batch_id,
                seat_id=seat_id,
                stage=stage,
                ambiguity_reason=ambiguity_reason,
                sidecar_generation=generation,
                sidecar_health_state=health_state,
                acceptance_known=acceptance_known,
            )
        except Exception:
            # The reservation (and acceptance when known) is already fsync'd.
            # Keep this warning private and sanitized, and never change the
            # public receipt or resend because diagnostics failed.
            self.v2_ambiguity_trace_warning_count += 1

    def _begin_v2_receipt_operation(self) -> None:
        with self.condition:
            if self.v2_receipts_closing:
                raise self._v2_problem(
                    HTTPStatus.SERVICE_UNAVAILABLE,
                    "sidecar_unavailable",
                    "the full-control-v2 service is shutting down",
                    retryable=False,
                )
            self.v2_active_receipt_operations += 1

    def _end_v2_receipt_operation(self) -> None:
        with self.condition:
            if self.v2_active_receipt_operations <= 0:
                raise RuntimeError("v2 receipt operation accounting underflow")
            self.v2_active_receipt_operations -= 1
            self.condition.notify_all()

    def v2_submit_batch(
        self, agent_id: str, batch: Any,
    ) -> tuple[int, dict[str, Any]]:
        try:
            clean_batch = validate_initial_command_batch(batch)
        except FullControlSchemaError as exc:
            raise self._v2_problem(
                HTTPStatus.BAD_REQUEST,
                "invalid_batch",
                "the full-control-v2 command batch is invalid",
                retryable=False,
                details={"rejection": rejection("schema", "batch_malformed")},
            ) from exc
        if (
            clean_batch["game_id"] != self.game_id
            or clean_batch["agent_id"] != agent_id
        ):
            raise self._v2_problem(
                HTTPStatus.BAD_REQUEST,
                "invalid_batch",
                "the full-control-v2 command batch is invalid",
                retryable=False,
                details={"rejection": rejection("schema", "batch_malformed")},
            )
        batch_id = clean_batch["batch_id"]
        try:
            self._begin_v2_receipt_operation()
        except APIProblem as problem:
            raise self._v2_not_accepted_problem(problem, batch_id) from problem
        try:
            return self._v2_submit_batch_active(agent_id, clean_batch)
        finally:
            self._end_v2_receipt_operation()

    def _v2_submit_batch_active(
        self, agent_id: str, batch: Any,
        *, internal_phase_claim: dict[str, Any] | None = None,
    ) -> tuple[int, dict[str, Any]]:
        """Resolve and execute one durable, generation-scoped v2 command."""
        build_timeout_batch = batch is None and internal_phase_claim is not None
        if not build_timeout_batch and (
            not isinstance(batch, dict) or batch.get("agent_id") != agent_id
        ):
            raise self._v2_problem(
                HTTPStatus.BAD_REQUEST,
                "invalid_batch",
                "the full-control-v2 command batch is invalid",
                retryable=False,
            )
        try:
            store = self._v2_receipt_store()
        except APIProblem as problem:
            if build_timeout_batch:
                raise
            raise self._v2_not_accepted_problem(
                problem, batch["batch_id"],
            ) from problem
        if not build_timeout_batch:
            batch_id = batch["batch_id"]
            try:
                duplicate = store.probe(batch)
            except Exception as exc:
                if isinstance(exc, V2ReceiptConflict):
                    problem = self._v2_problem(
                        HTTPStatus.CONFLICT,
                        "conflict",
                        "the batch ID is already bound to a different request",
                        retryable=False,
                    )
                    raise self._v2_not_accepted_problem(
                        problem, batch_id,
                    ) from exc
                try:
                    self._raise_v2_store_error(exc)
                except APIProblem as problem:
                    raise self._v2_not_accepted_problem(
                        problem, batch_id,
                    ) from exc
                raise AssertionError("unreachable")
            if duplicate is not None and duplicate.receipt is not None:
                return self._v2_receipt_status(duplicate.receipt), duplicate.receipt
            if duplicate is not None:
                raise self._v2_problem(
                    HTTPStatus.INTERNAL_SERVER_ERROR,
                    "internal_error",
                    "the command receipt is incomplete",
                    retryable=False,
                )

        try:
            context = self._resolve_v2_batch_context(agent_id)
        except Exception as exc:
            if build_timeout_batch:
                self._raise_v2_pre_batch_error(exc)
            self._raise_v2_not_accepted(exc, batch["batch_id"])
            raise AssertionError("unreachable")
        place, generation, sidecar, control, execution_lock = context

        # The seat lock serializes action resolution through final durability.
        # No Game condition, receipt-store lock, or control lock is held here.
        if not execution_lock.acquire(timeout=V2_EXECUTION_LOCK_TIMEOUT_S):
            problem = self._v2_unavailable()
            if build_timeout_batch:
                raise problem
            raise self._v2_not_accepted_problem(
                problem, batch["batch_id"],
            )
        with self.condition:
            pregame_lock_needed = self.state == "lobby"
        pregame_lock_held = False
        if pregame_lock_needed:
            pregame_lock_held = self.v2_pregame_execution_lock.acquire(
                timeout=V2_EXECUTION_LOCK_TIMEOUT_S,
            )
            if not pregame_lock_held:
                execution_lock.release()
                problem = self._v2_unavailable()
                if build_timeout_batch:
                    raise problem
                raise self._v2_not_accepted_problem(
                    problem, batch["batch_id"],
                )
        phase_claim: dict[str, Any] | None = None
        timeout_observation: dict[str, Any] | None = None
        phase_failure_cleanup = False
        try:
            if build_timeout_batch:
                if not self._v2_batch_context_current(
                    agent_id, place, generation, sidecar, control,
                    execution_lock,
                ):
                    raise self._v2_unavailable()
                with self.condition:
                    current_claim = self.v2_phase_ledger.get("end")
                    claim_current = bool(
                        current_claim is not None
                        and current_claim.get("claim_id")
                        == internal_phase_claim.get("claim_id")
                        and current_claim.get("key")
                        == internal_phase_claim.get("key")
                        and current_claim.get("place") == place
                        and current_claim.get("generation") == generation
                    )
                if not claim_current:
                    raise self._v2_unavailable()
                try:
                    timeout_observation = self._read_v2_observation_bundle(
                        sidecar, control,
                    )
                    batch = self._v2_phase_end_batch_from_observation(
                        self.game_id, internal_phase_claim, control,
                        timeout_observation,
                    )
                except Exception as exc:
                    if self._v2_boundary_failure_is_unattributable(exc):
                        self._note_v2_boundary_outcome(place, ok=False)
                    self._raise_v2_pre_batch_error(exc)
                    raise AssertionError("unreachable")
                if not self._v2_batch_context_current(
                    agent_id, place, generation, sidecar, control,
                    execution_lock,
                ):
                    raise self._v2_unavailable()

            try:
                duplicate = store.probe(batch)
            except Exception as exc:
                if build_timeout_batch:
                    self._raise_v2_store_error(exc)
                if isinstance(exc, V2ReceiptConflict):
                    problem = self._v2_problem(
                        HTTPStatus.CONFLICT,
                        "conflict",
                        "the batch ID is already bound to a different request",
                        retryable=False,
                    )
                    raise self._v2_not_accepted_problem(
                        problem, batch["batch_id"],
                    ) from exc
                try:
                    self._raise_v2_store_error(exc)
                except APIProblem as problem:
                    raise self._v2_not_accepted_problem(
                        problem, batch["batch_id"],
                    ) from exc
                raise AssertionError("unreachable")
            if duplicate is not None and duplicate.receipt is not None:
                return self._v2_receipt_status(duplicate.receipt), duplicate.receipt
            if duplicate is not None:
                # Only the creator can transition a reservation.  Reaching this
                # after owning the exact seat lock means durability was lost.
                raise self._v2_problem(
                    HTTPStatus.INTERNAL_SERVER_ERROR,
                    "internal_error",
                    "the command receipt is incomplete",
                    retryable=False,
                )
            if not self._v2_batch_context_current(
                agent_id, place, generation, sidecar, control, execution_lock,
            ):
                problem = self._v2_unavailable()
                if build_timeout_batch:
                    raise problem
                raise self._v2_not_accepted_problem(
                    problem, batch["batch_id"],
                )

            try:
                observation = timeout_observation
                if observation is None:
                    observation = self._read_v2_observation_bundle(
                        sidecar, control,
                    )
                command = batch["commands"][0]
                resolution = control.resolve_action(
                    observation,
                    batch["state_revision"],
                    command["action_id"],
                    command["arguments"],
                )
                if (
                    resolution.public_kind == "pregame.set_ready"
                    and resolution.native_arguments == "ready=1"
                ):
                    with self.condition:
                        gate_open = self._v2_pregame_gate_current_locked()
                    if not gate_open:
                        raise V2ControlError("stale_revision")
                phase_overview = None
                if resolution.public_kind == "phase.end":
                    phase_page = control.state_page(
                        observation, "overview", 1,
                    )
                    phase_overview = phase_page["page"]["items"][0]
            except Exception as exc:
                if self._v2_boundary_failure_is_unattributable(exc):
                    self._note_v2_boundary_outcome(place, ok=False)
                if build_timeout_batch:
                    self._raise_v2_pre_batch_error(exc)
                self._raise_v2_not_accepted(exc, batch["batch_id"])
                raise AssertionError("unreachable")
            self._note_v2_boundary_outcome(place, ok=True)
            if not self._v2_batch_context_current(
                agent_id, place, generation, sidecar, control, execution_lock,
            ):
                problem = self._v2_unavailable()
                if build_timeout_batch:
                    raise problem
                raise self._v2_not_accepted_problem(
                    problem, batch["batch_id"],
                )

            if resolution.public_kind == "phase.end":
                try:
                    phase_claim = self._phase_end_claim_for_action(
                        place, generation, batch["batch_id"], resolution,
                        phase_overview, internal_phase_claim,
                    )
                except Exception as exc:
                    if build_timeout_batch:
                        self._raise_v2_pre_batch_error(exc)
                    self._raise_v2_not_accepted(exc, batch["batch_id"])
                    raise AssertionError("unreachable")

            try:
                reservation = store.reserve(batch)
            except Exception as exc:
                if isinstance(exc, (V2ReceiptConflict, V2ReceiptInvalidBatch)):
                    self._release_phase_end_claim(phase_claim)
                else:
                    phase_failure_cleanup = self._fail_phase_end_durability(
                        phase_claim,
                    )
                self._raise_v2_store_error(exc)
                raise AssertionError("unreachable")
            if not reservation.created:
                if reservation.receipt is not None:
                    self._note_phase_end_receipt(
                        phase_claim, reservation.receipt["receipt_state"],
                    )
                    if reservation.receipt["receipt_state"] == "rejected":
                        phase_failure_cleanup = (
                            self._handle_rejected_phase_end(phase_claim)
                            or phase_failure_cleanup
                        )
                    return (
                        self._v2_receipt_status(reservation.receipt),
                        reservation.receipt,
                    )
                phase_failure_cleanup = self._fail_phase_end_durability(
                    phase_claim,
                )
                raise self._v2_problem(
                    HTTPStatus.INTERNAL_SERVER_ERROR,
                    "internal_error",
                    "the command receipt is incomplete",
                    retryable=False,
                )
            self._note_phase_end_receipt(phase_claim, "reserved")

            requested_revision = dict(batch["state_revision"])
            batch_id = batch["batch_id"]
            trace_recorded = False

            def record_ambiguity_trace(
                stage: str,
                reason: str,
                *,
                acceptance_known: bool,
            ) -> None:
                nonlocal trace_recorded
                if trace_recorded:
                    return
                if (
                    stage == "post_result_observation"
                    and reason == "observation_unavailable"
                ):
                    self._note_v2_ambiguous_observation(place)
                self._record_v2_ambiguity(
                    agent_id=agent_id,
                    batch_id=batch_id,
                    place=place,
                    generation=generation,
                    sidecar=sidecar,
                    stage=stage,
                    ambiguity_reason=reason,
                    acceptance_known=acceptance_known,
                )
                trace_recorded = True

            def ambiguous(
                stage: str,
                reason: str,
                *,
                acceptance_known: bool,
            ) -> tuple[int, dict[str, Any]]:
                if (
                    stage == "post_result_observation"
                    and reason == "observation_unavailable"
                ):
                    self._note_v2_ambiguous_observation(place)
                return self._v2_ambiguous(
                    store,
                    reservation,
                    agent_id,
                    batch_id,
                    requested_revision,
                    place=place,
                    generation=generation,
                    sidecar=sidecar,
                    stage=stage,
                    ambiguity_reason=reason,
                    acceptance_known=acceptance_known,
                    record_trace=not trace_recorded,
                )

            if not self._v2_batch_context_current(
                agent_id, place, generation, sidecar, control, execution_lock,
            ):
                result = ambiguous(
                    "pre_accept", "context_lost", acceptance_known=False,
                )
                self._note_phase_end_receipt(phase_claim, "ambiguous")
                return result

            acceptance_recorded = False

            def accepted(_acceptance: dict[str, Any]) -> None:
                nonlocal acceptance_recorded
                self._v2_transition(
                    store,
                    reservation,
                    self._v2_receipt(
                        agent_id,
                        batch_id,
                        "accepted",
                        requested_revision,
                    ),
                )
                self._note_phase_end_receipt(phase_claim, "accepted")
                acceptance_recorded = True

            def normalized_sidecar_ambiguity(
                exc: SidecarActionAmbiguous,
            ) -> tuple[str, str, bool]:
                known = exc.acceptance is not None
                stage = (
                    exc.stage
                    if exc.stage in {
                        "pre_accept", "post_accept", "correlated_terminal",
                    }
                    else ("post_accept" if known else "pre_accept")
                )
                reason = (
                    exc.ambiguity_reason
                    if exc.ambiguity_reason in {
                        "acceptance_unavailable",
                        "acceptance_callback_failed",
                        "accepted_revision_mismatch",
                        "result_unavailable",
                        "processing_boundary_mismatch",
                        "seat_epoch_changed",
                        "processing_timeout",
                    }
                    else "unexpected_failure"
                )
                return stage, reason, known

            def before_terminal_ambiguity(
                exc: SidecarActionAmbiguous,
            ) -> None:
                stage, reason, known = normalized_sidecar_ambiguity(exc)
                record_ambiguity_trace(
                    stage, reason, acceptance_known=known,
                )

            try:
                action_request = f"act_{secrets.token_urlsafe(18)}"
                if resolution.relation_scoped:
                    if (
                        resolution.native_actor_ref is None
                        or resolution.native_counterpart_ref is None
                    ):
                        raise V2ControlError("internal_error")
                    result = sidecar.execute_relation_scoped_action(
                        action_request,
                        resolution.native_revision,
                        resolution.native_actor_ref,
                        resolution.native_counterpart_ref,
                        resolution.native_slot,
                        resolution.native_arguments,
                        timeout_s=V2_ACTION_TIMEOUT_S,
                        on_accepted=accepted,
                        on_ambiguous=before_terminal_ambiguity,
                    )
                elif resolution.scoped:
                    if resolution.native_actor_ref is None:
                        raise V2ControlError("internal_error")
                    result = sidecar.execute_scoped_action(
                        action_request,
                        resolution.native_revision,
                        resolution.native_actor_ref,
                        resolution.native_slot,
                        resolution.native_arguments,
                        timeout_s=V2_ACTION_TIMEOUT_S,
                        on_accepted=accepted,
                        on_ambiguous=before_terminal_ambiguity,
                    )
                else:
                    result = sidecar.execute_action(
                        action_request,
                        resolution.native_slot,
                        resolution.native_arguments,
                        timeout_s=V2_ACTION_TIMEOUT_S,
                        expected_revision=resolution.native_revision,
                        on_accepted=accepted,
                        on_ambiguous=before_terminal_ambiguity,
                    )
            except SidecarActionAmbiguous as exc:
                trace_stage, trace_reason, trace_known = (
                    normalized_sidecar_ambiguity(exc)
                )
                response = ambiguous(
                    trace_stage,
                    trace_reason,
                    acceptance_known=trace_known,
                )
                self._note_phase_end_receipt(phase_claim, "ambiguous")
                return response
            except SidecarActionNotAccepted as exc:
                if not self._v2_batch_context_current(
                    agent_id, place, generation, sidecar, control,
                    execution_lock,
                ):
                    response = ambiguous(
                        "pre_accept", "context_lost",
                        acceptance_known=False,
                    )
                    self._note_phase_end_receipt(phase_claim, "ambiguous")
                    return response
                status, code = self._v2_definitive_rejection(
                    exc.code, correlated_native_rejection=True,
                )
                receipt = self._v2_terminal_transition(
                    store,
                    reservation,
                    self._v2_receipt(
                        agent_id,
                        batch_id,
                        "rejected",
                        requested_revision,
                        error_code=code,
                        retryable=code == "stale_revision",
                        rejection=self._v2_native_rejection(
                            exc.code, error_code=code,
                        ),
                    ),
                )
                self._note_phase_end_receipt(phase_claim, "rejected")
                phase_failure_cleanup = (
                    self._handle_rejected_phase_end(phase_claim)
                    or phase_failure_cleanup
                )
                return status, receipt
            except SidecarError as exc:
                if not self._v2_batch_context_current(
                    agent_id, place, generation, sidecar, control,
                    execution_lock,
                ):
                    response = ambiguous(
                        "pre_accept", "context_lost",
                        acceptance_known=False,
                    )
                    self._note_phase_end_receipt(phase_claim, "ambiguous")
                    return response
                status, code = self._v2_definitive_rejection(exc.code)
                receipt = self._v2_terminal_transition(
                    store,
                    reservation,
                    self._v2_receipt(
                        agent_id,
                        batch_id,
                        "rejected",
                        requested_revision,
                        error_code=code,
                        retryable=code == "stale_revision",
                        rejection=self._v2_native_rejection(
                            exc.code, error_code=code,
                        ),
                    ),
                )
                self._note_phase_end_receipt(phase_claim, "rejected")
                phase_failure_cleanup = (
                    self._handle_rejected_phase_end(phase_claim)
                    or phase_failure_cleanup
                )
                return status, receipt
            except Exception:
                response = ambiguous(
                    (
                        "post_accept"
                        if acceptance_recorded else "pre_accept"
                    ),
                    "unexpected_failure",
                    acceptance_known=acceptance_recorded,
                )
                self._note_phase_end_receipt(phase_claim, "ambiguous")
                return response

            if (
                not isinstance(result, dict)
                or result.get("accepted") is not True
                or isinstance(result.get("result_revision"), bool)
                or not isinstance(result.get("result_revision"), int)
                or result["result_revision"] < 1
                or result.get("status") not in {"applied", "rejected"}
                or result.get("applied") is not (
                    result.get("status") == "applied"
                )
                or (
                    resolution.operation == "investigate_city"
                    and result.get("status") == "applied"
                ) != isinstance(result.get("observation_selector"), str)
                or (
                    resolution.operation != "investigate_city"
                    or result.get("status") != "applied"
                ) and result.get("observation_selector") is not None
            ):
                accepted_from_result = bool(
                    isinstance(result, dict)
                    and result.get("accepted") is True
                )
                response = ambiguous(
                    (
                        "post_accept"
                        if acceptance_recorded or accepted_from_result
                        else "pre_accept"
                    ),
                    "invalid_result",
                    acceptance_known=(
                        acceptance_recorded or accepted_from_result
                    ),
                )
                self._note_phase_end_receipt(phase_claim, "ambiguous")
                return response
            vote_native_fallback = (
                resolution.public_kind == "player.cast_vote"
                and result["status"] == "applied"
                and result.get("applied") is True
            )
            if (
                resolution.public_kind == "phase.end"
                and result["status"] == "applied"
                and result.get("applied") is True
            ):
                # A successful phase end intentionally removes this seat's
                # active private observation. Its correlated native result is
                # already the authoritative proof.
                receipt = self._v2_terminal_transition(
                    store,
                    reservation,
                    self._v2_receipt(
                        agent_id, batch_id, "applied", requested_revision,
                    ),
                )
                receipt_state = receipt["receipt_state"]
                self._note_phase_end_receipt(phase_claim, receipt_state)
                return self._v2_receipt_status(receipt), receipt
            receipt_observation = None
            try:
                fresh = self._read_v2_post_result_observation_bundle(
                    sidecar, control,
                    on_terminal_error=(
                        None if vote_native_fallback else
                        lambda _exc: record_ambiguity_trace(
                            "post_result_observation",
                            "observation_unavailable",
                            acceptance_known=True,
                        )
                    ),
                )
                if (
                    isinstance(fresh.get("native_revision"), bool)
                    or not isinstance(fresh.get("native_revision"), int)
                    or fresh["native_revision"] < result["result_revision"]
                    or (
                        resolution.operation == "investigate_city"
                        and result["status"] == "applied"
                        and fresh["native_revision"]
                            != result["result_revision"]
                    )
                ):
                    raise SidecarError("snapshot_gone")
                public = control.state_page(fresh, "overview", 1)
                result_revision = dict(public["state_revision"])
                if (
                    resolution.operation == "investigate_city"
                    and result["status"] == "applied"
                ):
                    investigation_request = control.prepare_investigation_scope(
                        fresh, result["observation_selector"],
                    )
                    investigation_catalog = sidecar.read_state_scope_catalog(
                        f"investigation_{secrets.token_urlsafe(18)}",
                        investigation_request.native_revision,
                        investigation_request.section,
                        investigation_request.selector,
                        timeout_s=V2_SCOPE_MATERIALIZATION_TIMEOUT_S,
                    )
                    receipt_observation = (
                        control.project_investigation_observation(
                            fresh, investigation_request,
                            investigation_catalog,
                        )
                    )
            except SidecarError:
                if vote_native_fallback:
                    # A decisive ballot can resolve, disappear, or start the
                    # game before a fresh normal-player snapshot is readable.
                    # The exact request-correlated native UPDATE remains
                    # authoritative, while ordinary votes still use the
                    # fresh-revision path above.
                    receipt = self._v2_terminal_transition(
                        store,
                        reservation,
                        self._v2_receipt(
                            agent_id, batch_id, "applied", requested_revision,
                        ),
                    )
                    receipt_state = receipt["receipt_state"]
                    self._note_phase_end_receipt(phase_claim, receipt_state)
                    return self._v2_receipt_status(receipt), receipt
                response = ambiguous(
                    "post_result_observation", "observation_unavailable",
                    acceptance_known=True,
                )
                self._note_phase_end_receipt(phase_claim, "ambiguous")
                return response
            except Exception:
                response = ambiguous(
                    "post_result_observation", "observation_unavailable",
                    acceptance_known=True,
                )
                self._note_phase_end_receipt(phase_claim, "ambiguous")
                return response
            if not self._v2_batch_context_current(
                agent_id, place, generation, sidecar, control, execution_lock,
            ):
                response = ambiguous(
                    "post_result_observation", "post_result_context_lost",
                    acceptance_known=True,
                )
                self._note_phase_end_receipt(phase_claim, "ambiguous")
                return response

            if result["status"] == "applied" and result.get("applied") is True:
                receipt = self._v2_terminal_transition(
                    store,
                    reservation,
                    self._v2_receipt(
                        agent_id, batch_id, "applied", result_revision,
                        observation=receipt_observation,
                    ),
                )
                receipt_state = receipt["receipt_state"]
                self._note_phase_end_receipt(phase_claim, receipt_state)
                if receipt_state == "applied":
                    applied_turn = result_revision.get("turn")
                    if isinstance(applied_turn, int):
                        with self.condition:
                            self.v2_applied_turns[place] = max(
                                self.v2_applied_turns.get(place, 0),
                                applied_turn,
                            )
                if (
                    receipt_state == "applied"
                    and resolution.public_kind == "player.surrender"
                ):
                    with self.condition:
                        self.v2_surrendered_places.add(place)
                        self.condition.notify_all()
                if (
                    receipt_state == "applied"
                    and resolution.public_kind == "pregame.set_ready"
                ):
                    desired_ready = resolution.native_arguments == "ready=1"
                    with self.condition:
                        if desired_ready:
                            self.v2_pregame_ready_places.add(place)
                        else:
                            self.v2_pregame_ready_places.discard(place)
                        expected = {
                            item.number for item in self.joinable_places
                        }
                        if (
                            desired_ready
                            and self.v2_pregame_ready_places == expected
                            and self.state == "lobby"
                            and self.error is None
                            and not self.cancel_requested
                            and not self.sidecars_stopping
                            and self._v2_pregame_gate_current_locked()
                        ):
                            self.start_sent = True
                            self.start_count += 1
                            self.started_at = self.started_at or time.time()
                            self.state = "starting"
                            self.sidecar_start_deadline = (
                                time.monotonic() + V2_SIDECAR_STARTUP_GRACE_S
                            )
                            self._write_manifest()
                            self.condition.notify_all()
                return self._v2_receipt_status(receipt), receipt
            if (
                result["status"] == "rejected"
                and result.get("applied") is False
                and result.get("reason") == "POSTCONDITION_NOT_MET"
            ):
                receipt = self._v2_terminal_transition(
                    store,
                    reservation,
                    self._v2_receipt(
                        agent_id,
                        batch_id,
                        "rejected",
                        result_revision,
                        error_code="illegal_action",
                        rejection=rejection(
                            "native_dispatch",
                            "postcondition_not_met",
                            native_reason=result["reason"],
                        ),
                    ),
                )
                self._note_phase_end_receipt(phase_claim, "rejected")
                phase_failure_cleanup = (
                    self._handle_rejected_phase_end(phase_claim)
                    or phase_failure_cleanup
                )
                return HTTPStatus.UNPROCESSABLE_ENTITY, receipt
            response = ambiguous(
                "post_result_observation", "invalid_result",
                acceptance_known=True,
            )
            self._note_phase_end_receipt(phase_claim, "ambiguous")
            return response
        finally:
            if pregame_lock_held:
                self.v2_pregame_execution_lock.release()
            execution_lock.release()
            if phase_failure_cleanup or self.v2_receipt_store_failed:
                self._stop_all_sidecars()
                self._terminate_child()

    def v2_get_receipt(
        self, agent_id: str, batch_id: str,
    ) -> tuple[int, dict[str, Any]]:
        self._begin_v2_receipt_operation()
        try:
            return self._v2_get_receipt_active(agent_id, batch_id)
        finally:
            self._end_v2_receipt_operation()

    def _v2_get_receipt_active(
        self, agent_id: str, batch_id: str,
    ) -> tuple[int, dict[str, Any]]:
        store = self._v2_receipt_store()
        try:
            receipt = store.lookup(agent_id, batch_id)
        except V2ReceiptInvalidBatch as exc:
            receipt = None
        except Exception as exc:
            self._raise_v2_store_error(exc)
            raise AssertionError("unreachable")
        if receipt is None:
            raise self._v2_problem(
                HTTPStatus.NOT_FOUND,
                "invalid_request",
                "the command receipt was not found",
                retryable=False,
            )
        return HTTPStatus.OK, receipt

    def close_v2_receipts(self) -> None:
        with self.condition:
            self.v2_receipts_closing = True
            while self.v2_active_receipt_operations:
                self.condition.wait()
            store = self.v2_receipt_store
            trace = self.v2_ambiguity_trace
            phase_events = self.v2_phase_event_journal
        if store is not None:
            store.close()
        if trace is not None:
            trace.close()
        if phase_events is not None:
            phase_events.close()

    def v2_unimplemented(self, agent_id: str, resource: str) -> APIProblem:
        health = self.v2_health(agent_id)
        sidecar_ready = health["sidecar"].get("state") == "ready"
        if not sidecar_ready:
            code = "sidecar_unavailable"
            status = HTTPStatus.SERVICE_UNAVAILABLE
            message = "the full-control-v2 sidecar is not READY"
            retryable = self.state not in TERMINAL_STATES
        else:
            # The protocol foundation has intentionally not claimed these as
            # authoritative until native observations/actions are wired.
            code = "not_implemented"
            status = HTTPStatus.NOT_IMPLEMENTED
            message = f"full-control-v2 {resource} is not implemented yet"
            retryable = False
        payload = structured_error(
            code, message, retryable=retryable,
            details={"resource": resource},
        )
        return APIProblem(status, message, payload)

    def _barrier_progress(
        self, record: dict[str, Any], agent_id: str,
    ) -> dict[str, Any]:
        """Return the private, action-free progress contract for one seat."""
        turn = record["turn"]
        missing = [
            current_agent_id for current_agent_id in record["agents"]
            if (turn, current_agent_id) not in self.submissions
        ]
        action_received = (turn, agent_id) in self.submissions
        return {
            "current_turn": turn,
            "action_received": action_received,
            "waiting_for_others": action_received and bool(missing),
            "seats_remaining": len(missing),
            "pending_duration_s": round(
                max(0.0, time.time() - record["published_at"]), 3,
            ),
        }

    def _waiting_for_agent(
        self, agent_id: str, after_turn: int,
        record: dict[str, Any] | None,
    ) -> dict[str, Any]:
        value: dict[str, Any] = {
            "schema_version": 1,
            "state": "waiting",
            "game_state": self.state,
            "game_id": self.game_id,
            "after_turn": after_turn,
        }
        agent = self.agents[agent_id]
        value.update({
            "agent_id": agent_id,
            "place": agent["place"],
            "seat_id": agent["seat_id"],
            "controller_label": agent["controller_label"],
        })
        if record is None or agent_id not in record["agents"]:
            value.update({
                "current_turn": (
                    self.latest_turn["turn"] if self.latest_turn else None
                ),
                "action_received": None,
                "waiting_for_others": False,
                "seats_remaining": None,
                "pending_duration_s": None,
            })
            return value
        value.update(self._barrier_progress(record, agent_id))
        if value["action_received"]:
            value["message"] = (
                f"Your action for turn {record['turn']} was received; "
                f"waiting for {value['seats_remaining']} other seat(s)."
            )
        return value

    def _action_ack(
        self, agent_id: str, turn: int, *, idempotent: bool,
    ) -> dict[str, Any]:
        agent = self.agents[agent_id]
        record = self.current_turn
        if record is not None and record["turn"] == turn:
            progress = self._barrier_progress(record, agent_id)
        else:
            progress = {
                "current_turn": turn,
                "action_received": True,
                "waiting_for_others": False,
                "seats_remaining": 0,
                "pending_duration_s": None,
            }
        return {
            "schema_version": 1,
            "accepted": True,
            "idempotent": idempotent,
            "status": "already_accepted" if idempotent else "accepted",
            "game_id": self.game_id,
            "agent_id": agent_id,
            "place": agent["place"],
            "seat_id": agent["seat_id"],
            "player_name": agent["player_name"],
            "controller_label": agent["controller_label"],
            "turn": turn,
            **progress,
        }

    def next_for_agent(
        self, agent_id: str, after_turn: int, wait_s: float,
    ) -> dict[str, Any]:
        if self.config["control_protocol"] != STRATEGIC_V1:
            raise APIProblem(
                HTTPStatus.CONFLICT,
                "the strategic-v1 next route is unavailable for a "
                "full-control-v2 session",
            )
        deadline = time.monotonic() + wait_s
        reminder_deadline = (
            time.monotonic() + BARRIER_REMINDER_INTERVAL_S
        )
        with self.condition:
            while True:
                if self.state in TERMINAL_STATES:
                    agent = self.agents[agent_id]
                    return {
                        "schema_version": 1,
                        "state": self.state,
                        "game_id": self.game_id,
                        "agent_id": agent_id,
                        "place": agent["place"],
                        "seat_id": agent["seat_id"],
                        "controller_label": agent["controller_label"],
                        "turn": (
                            self.latest_turn["turn"] if self.latest_turn else None
                        ),
                    }
                record = self.current_turn
                key = (
                    (record["turn"], agent_id) if record is not None else None
                )
                if (
                    record is not None
                    and agent_id in record["agents"]
                    and key not in self.submissions
                ):
                    private = record["agents"][agent_id]
                    delivered_at = time.time()
                    delivery_count = int(
                        private.get("observation_delivery_count", 0)
                    ) + 1
                    private["observation_delivery_count"] = delivery_count
                    private.setdefault("observation_delivered_at", delivered_at)
                    progress = self._barrier_progress(record, agent_id)
                    result = {
                        "schema_version": 1,
                        "state": self.state,
                        "game_id": self.game_id,
                        "agent_id": agent_id,
                        "place": private["place"],
                        "seat_id": private["seat_id"],
                        "controller_label": private["controller_label"],
                        "turn": record["turn"],
                        "year": record["year"],
                        "observation_id": private["observation_id"],
                        "observation": private["observation"],
                        "objective": self.config["objective"],
                        "timing_mode": self.config["timing_mode"],
                        "action_timeout_s": self.config["action_timeout_s"],
                        "deadline_at": record["deadline_at"],
                        "action_schema": {
                            "type": "set_traits",
                            "traits": {
                                name: {
                                    "type": "integer",
                                    "minimum": TRAIT_MIN,
                                    "maximum": TRAIT_MAX,
                                }
                                for name in TRAITS
                            },
                            "required_traits": list(TRAITS),
                        },
                        "redelivered": (
                            record["turn"] <= after_turn or delivery_count > 1
                        ),
                        **progress,
                    }
                    if (
                        progress["pending_duration_s"]
                        >= BARRIER_REMINDER_INTERVAL_S
                    ):
                        result["reminder"] = (
                            f"No action has been received for your seat on turn "
                            f"{record['turn']}. Submit this observation again "
                            "and do not advance LAST_TURN until accepted=true."
                        )
                    return result
                remaining = deadline - time.monotonic()
                reminder_remaining = reminder_deadline - time.monotonic()
                action_received = bool(
                    record is not None
                    and agent_id in record["agents"]
                    and key in self.submissions
                )
                if remaining <= 0 or (
                    action_received and reminder_remaining <= 0
                ):
                    return self._waiting_for_agent(
                        agent_id, after_turn, record,
                    )
                self.condition.wait(
                    min(remaining, reminder_remaining)
                    if action_received else remaining
                )

    def submit_action(
        self, agent_id: str, payload: Any,
    ) -> tuple[int, dict[str, Any]]:
        if self.config["control_protocol"] != STRATEGIC_V1:
            raise APIProblem(
                HTTPStatus.CONFLICT,
                "the strategic-v1 trait action route is unavailable for a "
                "full-control-v2 session",
            )
        if not isinstance(payload, dict):
            raise APIProblem(HTTPStatus.BAD_REQUEST, "action request must be an object")
        unknown = set(payload) - {
            "turn", "observation_id", "action", "telemetry",
        }
        if unknown:
            raise APIProblem(
                HTTPStatus.BAD_REQUEST,
                f"action request has unknown fields: {sorted(unknown)}",
            )
        turn = payload.get("turn")
        if isinstance(turn, bool) or not isinstance(turn, int):
            raise APIProblem(HTTPStatus.BAD_REQUEST, "turn must be an integer")
        observation_id = payload.get("observation_id")
        if not isinstance(observation_id, str) or not observation_id:
            raise APIProblem(
                HTTPStatus.BAD_REQUEST,
                "observation_id must be a non-empty string",
            )
        try:
            action = validate_action(payload.get("action"))
        except ActionError as exc:
            raise APIProblem(HTTPStatus.BAD_REQUEST, str(exc)) from exc
        telemetry = payload.get("telemetry")
        request_value = {
            "turn": turn,
            "observation_id": observation_id,
            "action": action,
            "telemetry": telemetry,
        }
        request_hash = hashlib.sha256(
            _canonical(request_value).encode("utf-8")
        ).hexdigest()
        key = (turn, agent_id)
        with self.condition:
            previous = self.submissions.get(key)
            if previous is not None:
                if hmac.compare_digest(previous["request_hash"], request_hash):
                    return HTTPStatus.OK, self._action_ack(
                        agent_id, turn, idempotent=True,
                    )
                raise APIProblem(
                    HTTPStatus.CONFLICT,
                    "conflicting action was already submitted for this turn",
                )
            record = self.current_turn
            if record is None or record["turn"] != turn:
                raise APIProblem(HTTPStatus.CONFLICT, "action is stale or not pending")
            private = record["agents"].get(agent_id)
            if (
                private is None
                or private["observation_id"] != observation_id
            ):
                raise APIProblem(
                    HTTPStatus.CONFLICT,
                    "observation_id is stale or does not belong to this agent",
                )
            submitted_at = time.time()
            self.submissions[key] = {
                **request_value,
                "request_hash": request_hash,
                "submitted_at": submitted_at,
                "latency_ms": round(
                    (submitted_at - record["published_at"]) * 1000, 3,
                ),
            }
            self.condition.notify_all()
            return HTTPStatus.ACCEPTED, self._action_ack(
                agent_id, turn, idempotent=False,
            )

    def _validate_turn(self, payload: Any) -> tuple[int, int, list[dict[str, Any]]]:
        if self.config["control_protocol"] != STRATEGIC_V1:
            raise APIProblem(
                HTTPStatus.CONFLICT,
                "the internal strategic-v1 bridge is unavailable for a "
                "full-control-v2 session",
            )
        if not isinstance(payload, dict):
            raise APIProblem(HTTPStatus.BAD_REQUEST, "turn request must be an object")
        if payload.get("game_id") != self.game_id:
            raise APIProblem(HTTPStatus.CONFLICT, "game_id does not match route")
        turn = payload.get("turn")
        year = payload.get("year")
        if isinstance(turn, bool) or not isinstance(turn, int):
            raise APIProblem(HTTPStatus.BAD_REQUEST, "turn must be an integer")
        if isinstance(year, bool) or not isinstance(year, int):
            raise APIProblem(HTTPStatus.BAD_REQUEST, "year must be an integer")
        observations = payload.get("observations")
        if not isinstance(observations, list):
            raise APIProblem(HTTPStatus.BAD_REQUEST, "observations must be an array")
        expected = {place.seat_id for place in self.joinable_places}
        seen: set[str] = set()
        clean: list[dict[str, Any]] = []
        for index, observation in enumerate(observations):
            if not isinstance(observation, dict):
                raise APIProblem(
                    HTTPStatus.BAD_REQUEST,
                    f"observations[{index}] must be an object",
                )
            seat_id = observation.get("seat_id")
            if seat_id not in expected or seat_id in seen:
                raise APIProblem(
                    HTTPStatus.BAD_REQUEST,
                    f"observations[{index}].seat_id is unknown or duplicated",
                )
            if observation.get("turn") != turn or observation.get("year") != year:
                raise APIProblem(
                    HTTPStatus.BAD_REQUEST,
                    f"observations[{index}] turn/year mismatch",
                )
            seen.add(seat_id)
            clean.append(dict(observation))
        if seen != expected:
            raise APIProblem(
                HTTPStatus.BAD_REQUEST,
                f"observations must contain every agent seat: {sorted(expected)}",
            )
        return turn, year, clean

    def process_turn(self, payload: Any) -> dict[str, Any]:
        turn, year, observations = self._validate_turn(payload)
        request_hash = hashlib.sha256(
            _canonical(payload).encode("utf-8")
        ).hexdigest()
        with self.condition:
            previous_hash = self.turn_request_hashes.get(turn)
            if previous_hash is not None:
                if not hmac.compare_digest(previous_hash, request_hash):
                    raise APIProblem(
                        HTTPStatus.CONFLICT,
                        "duplicate turn payload differs from the original",
                    )
                while (
                    turn not in self.turn_responses
                    and self.state not in TERMINAL_STATES
                ):
                    self.condition.wait()
                response = self.turn_responses.get(turn)
                if response is None:
                    raise APIProblem(
                        HTTPStatus.CONFLICT, "game ended before turn resolved",
                    )
                return response
            if self.state not in {"starting", "running"}:
                raise APIProblem(
                    HTTPStatus.CONFLICT,
                    f"game cannot accept turns while {self.state}",
                )
            if self.current_turn is not None:
                raise APIProblem(
                    HTTPStatus.CONFLICT, "another turn barrier is active",
                )
            if len(self.agents) != self.max_agents:
                raise APIProblem(
                    HTTPStatus.CONFLICT, "not every agent place has joined",
                )
            by_seat = {item["seat_id"]: item for item in observations}
            published_at = time.time()
            record_agents: dict[str, dict[str, Any]] = {}
            for place in self.joinable_places:
                agent_id = self.place_agents[place.number]
                agent = self.agents[agent_id]
                record_agents[agent_id] = {
                    "seat_id": place.seat_id,
                    "place": place.number,
                    # Prefixing keeps the opaque value safe as a CLI option
                    # argument even when token_urlsafe() starts with "-".
                    "observation_id": f"obs_{secrets.token_urlsafe(18)}",
                    "observation": by_seat[place.seat_id],
                    "controller_label": agent["controller_label"],
                    "controller_metadata": agent["metadata"],
                    "controller_fingerprint": agent[
                        "controller_fingerprint"
                    ],
                }
            action_timeout_s = self.config["action_timeout_s"]
            deadline_at = (
                published_at + action_timeout_s
                if action_timeout_s is not None else None
            )
            record = {
                "turn": turn,
                "year": year,
                "published_at": published_at,
                "deadline_at": deadline_at,
                "agents": record_agents,
                "resolved": False,
            }
            self.turn_request_hashes[turn] = request_hash
            self.current_turn = record
            self.latest_turn = record
            if self.state == "starting":
                self.state = "running"
            self._write_manifest()
            self.condition.notify_all()

            deadline_mono = (
                time.monotonic() + action_timeout_s
                if action_timeout_s is not None else None
            )
            while True:
                missing = [
                    agent_id for agent_id in record_agents
                    if (turn, agent_id) not in self.submissions
                ]
                if not missing or self.cancel_requested:
                    break
                if deadline_mono is None:
                    self.condition.wait()
                else:
                    remaining = deadline_mono - time.monotonic()
                    if remaining <= 0:
                        break
                    self.condition.wait(remaining)

            missing = [
                agent_id for agent_id in record_agents
                if (turn, agent_id) not in self.submissions
            ]
            if (
                missing and not self.cancel_requested
                and action_timeout_s is not None
            ):
                reason = (
                    f"turn {turn} timed out waiting for "
                    + ", ".join(sorted(missing))
                )
                self.invalid_reasons.append(reason)
            actions: list[dict[str, Any]] = []
            responded: list[str] = []
            timed_out: list[str] = []
            for agent_id, private in record_agents.items():
                submission = self.submissions.get((turn, agent_id))
                if submission is None:
                    timed_out.append(private["seat_id"])
                    action = None
                    telemetry = None
                    latency_ms = (
                        round(action_timeout_s * 1000, 3)
                        if action_timeout_s is not None else None
                    )
                    source = (
                        "external_timeout"
                        if action_timeout_s is not None
                        else "external_cancelled"
                    )
                    error = (
                        "action timed out; previous traits retained"
                        if action_timeout_s is not None
                        else "game cancelled before action was submitted"
                    )
                else:
                    responded.append(private["seat_id"])
                    action = submission["action"]
                    telemetry = submission["telemetry"]
                    latency_ms = submission["latency_ms"]
                    source = "external"
                    error = None
                    actions.append(
                        {
                            "seat_id": private["seat_id"],
                            "traits": action["traits"],
                        }
                    )
                self._append_trace(
                    {
                        "event": "decision",
                        "turn": turn,
                        "year": year,
                        "seat_id": private["seat_id"],
                        "agent_id": agent_id,
                        "player_name": private["observation"].get("player_name"),
                        "seat_type": "external",
                        "controller_label": private["controller_label"],
                        "controller_metadata": private[
                            "controller_metadata"
                        ],
                        "controller_fingerprint": private[
                            "controller_fingerprint"
                        ],
                        "source": source,
                        "fallback": False,
                        "error": error,
                        "latency_ms": latency_ms,
                        "input_tokens": 0,
                        "output_tokens": 0,
                        "observation": private["observation"],
                        "telemetry": telemetry,
                        "action": action,
                    }
                )
            response = {
                "schema_version": 1,
                "turn": turn,
                "actions": actions,
                "timed_out_seats": timed_out,
                # The bridge requires any response with uncovered seats to be
                # non-valid. Cancellation is not an evaluation timeout and
                # does not add an invalid reason, but it still cannot produce
                # a benchmark-valid partial turn response.
                "benchmark_valid": not self.invalid_reasons and not timed_out,
            }
            record["resolved"] = True
            self.turn_responses[turn] = response
            self.timeline.append(
                {
                    "turn": turn,
                    "year": year,
                    "responded_seats": sorted(responded),
                    "timed_out_seats": sorted(timed_out),
                    "resolved_at": time.time(),
                }
            )
            self.current_turn = None
            self._write_manifest()
            self.condition.notify_all()
            return response

    def _append_trace(self, event: dict[str, Any]) -> None:
        with (self.episode / "decisions.jsonl").open(
            "a", encoding="utf-8",
        ) as stream:
            stream.write(_canonical(event) + "\n")
            stream.flush()

    def cancel(self) -> dict[str, Any]:
        with self.condition:
            if self.state in TERMINAL_STATES:
                return self.status()
            self.cancel_requested = True
            self.error = "cancelled by owner"
            self._terminalize_v2_phase_locked("cancelled")
            self.condition.notify_all()
        self._stop_all_sidecars()
        self._terminate_child()
        return self.status()

    def urls(self) -> dict[str, str]:
        base = self.supervisor.service_url
        value = {
            "join_url": f"{base}/v1/games/{self.game_id}/join",
            "status_url": f"{base}/v1/games/{self.game_id}/status",
            "result_url": f"{base}/v1/games/{self.game_id}/result",
            "watch_url": f"{base}/watch/{self.game_id}",
            "watch_json_url": f"{base}/v1/games/{self.game_id}/watch.json",
            "replay_url": f"{base}/v1/games/{self.game_id}/replay.json",
            "frames_url": f"{base}/v1/games/{self.game_id}/frames",
            "video_url": f"{base}/v1/games/{self.game_id}/video.mp4",
        }
        if self.config["control_protocol"] == FULL_CONTROL_V2:
            value["phase_events_url"] = (
                f"{base}/v1/games/{self.game_id}/phase-events"
            )
        return value

    def _configured_score_snapshot(self) -> dict[str, Any]:
        """Return a strict authoritative snapshot for every configured seat."""
        score_path = self.episode / "score.log"
        parsed = parse_scorelog(score_path)
        rows_by_name: dict[str, dict[str, Any]] = {}
        configured_names = {place.player_name for place in self.places}
        for row in parsed.get("players", []):
            name = row.get("name")
            if name not in configured_names:
                continue
            previous = rows_by_name.get(name)
            if previous is None or (
                row.get("last_score_turn") or -1,
                row.get("added_turn") or -1,
            ) > (
                previous.get("last_score_turn") or -1,
                previous.get("added_turn") or -1,
            ):
                rows_by_name[name] = row
        rows = [
            dict(rows_by_name[place.player_name])
            for place in self.places
            if place.player_name in rows_by_name
        ]
        if (
            len(rows) != len(self.places)
            or any("score" not in row.get("metrics", {}) for row in rows)
            or any(
                row.get("alive") is True
                and row.get("last_score_turn") != parsed.get("final_turn")
                for row in rows
            )
        ):
            raise ScorelogError("score snapshot is incomplete")
        rows.sort(key=lambda row: (-row["score"], row["player_id"]))
        last_score = None
        rank = 0
        for index, row in enumerate(rows, 1):
            if row["score"] != last_score:
                rank = index
                last_score = row["score"]
            row["rank"] = rank
        return {**parsed, "players": rows}

    def _leaderboard(self) -> list[dict[str, Any]]:
        """Return the last complete all-player score snapshot."""
        try:
            parsed = self._configured_score_snapshot()
            self.score_snapshot = parsed
        except (OSError, ScorelogError, TypeError, ValueError):
            parsed = self.score_snapshot
        if not parsed:
            return []

        by_name = {place.player_name: place for place in self.places}
        leaderboard = []
        for score_row in parsed.get("players", []):
            place = by_name.get(score_row.get("name"))
            if place is None:
                continue
            agent_id = self.place_agents.get(place.number)
            agent = self.agents.get(agent_id) if agent_id else None
            if place.joinable:
                label = (
                    agent["controller_label"] if agent is not None
                    else "Unclaimed agent place"
                )
                metadata = agent["metadata"] if agent is not None else {}
                model = (
                    metadata.get("model")
                    if isinstance(metadata, dict)
                    and isinstance(metadata.get("model"), str)
                    else None
                )
                controller_type = "external"
            else:
                label = "Freeciv Classic AI"
                model = "classic"
                controller_type = "native"
            leaderboard.append(
                {
                    "rank": score_row["rank"],
                    "score": score_row["score"],
                    "score_turn": score_row.get("last_score_turn"),
                    "alive": score_row.get("alive"),
                    "place": place.number,
                    "seat_id": place.seat_id,
                    "player_name": place.player_name,
                    "player_color": place.player_color,
                    "controller_label": label,
                    "controller_type": controller_type,
                    "model": model,
                }
            )
        return leaderboard

    # Stable victory codes emitted by check_for_game_over() in srv_main.c.
    VICTORY_LABELS = {
        "spacerace": "spaceship victory",
        "conquest": "conquest victory",
        "team": "team victory",
        "allied": "allied victory",
        "culture": "cultural domination victory",
        "world_peace": "world peace victory",
        "scenario": "scenario victory",
        "all_defeated": "all civilizations defeated",
        # No in-game condition fired; the winner is decided on final score.
        "turn_limit": "score victory",
    }

    def _victory(self) -> dict[str, Any] | None:
        """Read the engine's machine-readable game-over record, if written."""
        try:
            record = json.loads(
                self.victory_path.read_text(encoding="utf-8")
            )
        except (OSError, json.JSONDecodeError):
            return None
        if not isinstance(record, dict):
            return None
        code = record.get("victory")
        if not isinstance(code, str) or not code:
            return None
        winners = record.get("winners")
        return {
            "code": code,
            "label": self.VICTORY_LABELS.get(code, code),
            "winners": (
                [name for name in winners if isinstance(name, str)]
                if isinstance(winners, list) else []
            ),
            "turn": record.get("turn"),
            "year": record.get("year"),
        }

    def _outcome(
        self, leaderboard: list[dict[str, Any]],
    ) -> dict[str, Any]:
        """Score-based outcome, annotated with how the game actually ended."""
        outcome = self._score_outcome(leaderboard)
        victory = self._victory()
        outcome["victory"] = victory
        if victory is not None and outcome["status"] in {"won", "tied"}:
            # The score margin alone does not say why play stopped; a
            # spaceship or conquest ends the game on the winner's timing.
            outcome["summary"] = f"{outcome['summary']} ({victory['label']})"
        return outcome

    def _score_outcome(
        self, leaderboard: list[dict[str, Any]],
    ) -> dict[str, Any]:
        if not leaderboard:
            if self.state in TERMINAL_STATES:
                return {
                    "status": "invalid",
                    "summary": (
                        "No valid winner; no complete score snapshot is available"
                    ),
                    "leaders": [],
                    "margin": None,
                    "score_turn": None,
                }
            return {
                "status": "pending",
                "summary": "Scores are not available yet",
                "leaders": [],
                "margin": None,
                "score_turn": None,
            }
        best_rank = min(row["rank"] for row in leaderboard)
        leaders = [
            row for row in leaderboard if row["rank"] == best_rank
        ]
        score_turn = leaderboard[0].get("score_turn")
        if len(leaders) > 1:
            labels = [row["controller_label"] for row in leaders]
            if self.state == "completed":
                status = "tied"
                summary = f"{' and '.join(labels)} finished tied"
            elif self.state in TERMINAL_STATES:
                status = "invalid"
                summary = (
                    "No valid winner; "
                    f"{' and '.join(labels)} were tied at the last complete score"
                )
            else:
                status = "tie"
                summary = f"{' and '.join(labels)} are tied"
            return {
                "status": status,
                "summary": summary,
                "leaders": labels,
                "margin": 0,
                "score_turn": score_turn,
            }
        leader = leaders[0]
        others = [row for row in leaderboard if row is not leader]
        margin = (
            leader["score"] - max(row["score"] for row in others)
            if others else None
        )
        if self.state == "completed":
            status = "won"
            verb = "won"
        elif self.state in TERMINAL_STATES:
            status = "invalid"
            suffix = f" by {margin}" if margin is not None else ""
            return {
                "status": status,
                "summary": (
                    f"No valid winner; {leader['controller_label']} led{suffix} "
                    "at the last complete score"
                ),
                "leaders": [leader["controller_label"]],
                "margin": margin,
                "score_turn": score_turn,
            }
        else:
            status = "leads"
            verb = "leads"
        suffix = f" by {margin}" if margin is not None else ""
        return {
            "status": status,
            "summary": f"{leader['controller_label']} {verb}{suffix}",
            "leaders": [leader["controller_label"]],
            "margin": margin,
            "score_turn": score_turn,
        }

    def _public_barrier(self) -> dict[str, Any] | None:
        """Expose progress states without private observations or actions."""
        record = self.current_turn
        if record is None or record.get("resolved"):
            return None
        now = time.time()
        turn = record["turn"]
        controllers: list[dict[str, Any]] = []
        seats_remaining = 0
        for place in self.joinable_places:
            agent_id = self.place_agents.get(place.number)
            agent = self.agents.get(agent_id) if agent_id else None
            private = record["agents"].get(agent_id) if agent_id else None
            submission = (
                self.submissions.get((turn, agent_id)) if agent_id else None
            )
            if submission is not None:
                barrier_state = "submitted"
                state_since = submission["submitted_at"]
            elif private and private.get("observation_delivered_at") is not None:
                barrier_state = "thinking"
                state_since = private["observation_delivered_at"]
                seats_remaining += 1
            else:
                barrier_state = "waiting_for_observation"
                state_since = record["published_at"]
                seats_remaining += 1
            controllers.append({
                "place": place.number,
                "seat_id": place.seat_id,
                "player_name": place.player_name,
                "player_color": place.player_color,
                "controller_label": (
                    agent["controller_label"] if agent else None
                ),
                "state": barrier_state,
                "action_received": submission is not None,
                "state_duration_s": round(max(0.0, now - state_since), 3),
            })
        return {
            "turn": turn,
            "year": record["year"],
            "pending_duration_s": round(
                max(0.0, now - record["published_at"]), 3,
            ),
            "seats_remaining": seats_remaining,
            "controllers": controllers,
        }

    def _v2_seat_standing_locked(self, place_number: int) -> str:
        """Report one seat's own standing in the game.

        ``surrender`` is applied by the native client long before Freeciv
        reaps the player, and a seat that only sees ``applied`` plus a still
        running game cannot tell that apart from a no-op. These four values
        are derived from state the supervisor already holds: the seat's own
        applied surrender receipt, the liveness bit on its phase evidence, and
        whether a termination is already in flight.
        """
        row = self.v2_phase_ledger.get("evidence", {}).get(place_number)
        surrendered = place_number in self.v2_surrendered_places
        if row is not None and not row["alive"]:
            return "eliminated"
        if not surrendered:
            return "active"
        terminating = bool(
            self.cancel_requested
            or self.sidecars_stopping
            or self.server_exit_observed
            or self.state in TERMINAL_STATES
        )
        return "termination_pending" if terminating else "surrendered"

    def _v2_waiting_on_locked(self, place_number: int) -> dict[str, Any] | None:
        """Name what the phase loop is blocked on, from this seat's view.

        ``None`` means nothing is blocking: it is this seat's phase and the
        native boundary will accept its phase end. Every other case names a
        blocker, so a control loop that sees ``phase_not_ready`` or a ``wait``
        timeout can say why rather than guessing.
        """
        ledger = self.v2_phase_ledger
        state = ledger["state"]
        active_place = ledger.get("active_place")
        evidence = ledger.get("evidence", {})
        terminalized = bool(
            self.state in TERMINAL_STATES or self.cancel_requested
            or self.server_exit_observed
        )
        wedged = self.v2_wedged_places.get(place_number)
        if not terminalized and wedged is not None:
            # This has to precede the "nothing is blocking you" answer below:
            # the ledger can still say the phase is this seat's while the
            # boundary that would carry its actions is dead.
            in_flight = self.v2_recovery_in_flight.get(place_number)
            # Say which fault took the seat, for the same reason health does:
            # a client that no longer exists and a boundary that answers
            # unusably are recovered the same way but caused differently.
            lost = (
                "its native client exited"
                if wedged.get("trigger") == "sidecar_exit"
                else "wedged"
            )
            if in_flight is None:
                summary = (
                    "The native control boundary for this seat "
                    + lost
                    + " and is being recovered; no request against it "
                    "can succeed until it is republished on a new generation."
                )
            elif in_flight["kind"] == "autosave_rollback":
                target = in_flight.get("target_turn")
                summary = (
                    f"The native control boundary for this seat {lost}. The "
                    "game is being rolled back to the "
                    + (
                        f"turn {target} autosave"
                        if isinstance(target, int) else "last readable autosave"
                    )
                    + " and this seat will return on sidecar generation "
                    f"{self.sidecar_generations.get(place_number, 0) + 1}. "
                    "Every action id and cursor cached against the previous "
                    "generation is already expired."
                )
            else:
                summary = (
                    f"The native control boundary for this seat {lost} and is "
                    "being re-attached on sidecar generation "
                    f"{self.sidecar_generations.get(place_number, 0) + 1}. "
                    "Every action id and cursor cached against the previous "
                    "generation is already expired."
                )
            started = ledger.get("progress_started_monotonic")
            return {
                "kind": "boundary_recovery",
                "summary": summary,
                "seats": [],
                "waiting_s": (
                    round(max(0.0, time.monotonic() - started), 3)
                    if started is not None else None
                ),
            }
        if not terminalized and state == "awaiting_agent" and (
            active_place == place_number
        ):
            return None

        def seat_rows(places: Iterable[int]) -> list[dict[str, Any]]:
            rows = []
            for number in sorted(places):
                if number < 1 or number > len(self.places):
                    continue
                place = self.places[number - 1]
                agent_id = self.place_agents.get(number)
                agent = self.agents.get(agent_id) if agent_id is not None else None
                rows.append({
                    "place": number,
                    "seat_id": place.seat_id,
                    "player_name": place.player_name,
                    "controller_label": (
                        agent["controller_label"] if agent is not None else None
                    ),
                    "standing": self._v2_seat_standing_locked(number),
                    "is_self": number == place_number,
                })
            return rows

        if terminalized:
            kind, seats, summary = (
                "termination",
                [],
                "The game is being terminalized; no further phase will open.",
            )
        elif state == "synchronizing":
            missing = {
                place.number for place in self.joinable_places
            } - set(evidence)
            seats = seat_rows(missing or set(evidence))
            kind = "phase_synchronization"
            summary = (
                "Seat phase reports have not agreed on the current turn and "
                "phase yet."
                if not missing else
                "Waiting for phase evidence from seats that have not reported."
            )
        elif state in {"ending", "ambiguous_ending"}:
            end = ledger.get("end") or {}
            kind = "phase_end"
            seats = seat_rows(
                {end["place"]} if isinstance(end.get("place"), int) else set()
            )
            summary = (
                "A phase end is in flight and its outcome is not yet known."
                if state == "ambiguous_ending" else
                "A phase end is in flight and has not been reconciled yet."
            )
        elif state == "native_phase" or active_place is None:
            kind = "native_phase"
            seats = []
            summary = (
                "No seat holds the phase; the native server is between "
                "phases."
            )
        else:
            seats = seat_rows({active_place})
            active_standing = self._v2_seat_standing_locked(active_place)
            mine = active_place == place_number
            if state == "inactive_done" and active_standing in {
                "surrendered", "termination_pending",
            }:
                kind = "seat_surrendered"
                summary = (
                    "The seat holding the phase has surrendered and is "
                    "waiting for the server to reap it; it will not end its "
                    "phase."
                    if not mine else
                    "You surrendered and are waiting for the server to reap "
                    "this seat; this seat will not end its phase."
                )
            elif state == "inactive_done":
                kind = "seat_inactive"
                summary = (
                    "The seat holding the phase is finished or no longer "
                    "alive; the native server advances next."
                )
            elif state == "phase_not_ready":
                kind = "seat_not_ready"
                summary = (
                    "The native client has not announced that this seat may "
                    "end its phase. Freeciv withholds that permission while "
                    "the server setting fixedlength is enabled, while the "
                    "server is busy, and until a phase-timing setting change "
                    "takes effect at the next turn boundary."
                    if mine else
                    "The native client has not announced that the seat "
                    "holding the phase may end it."
                )
            else:
                kind = "other_seat"
                summary = "Another seat holds the phase and has not ended it."
        started = ledger.get("progress_started_monotonic")
        return {
            "kind": kind,
            "summary": summary,
            "seats": seats,
            "waiting_s": (
                round(max(0.0, time.monotonic() - started), 3)
                if started is not None else None
            ),
        }

    def _public_v2_phase(self) -> dict[str, Any]:
        ledger = self.v2_phase_ledger
        key = ledger.get("key")
        evidence = ledger.get("evidence", {})
        active_place = ledger.get("active_place")
        end = ledger.get("end")
        started = ledger.get("deadline_started_monotonic")
        timeout = self.config["action_timeout_s"]
        now = time.monotonic()
        terminalized = bool(
            self.state in TERMINAL_STATES or self.cancel_requested
            or self.server_exit_observed
        )
        elapsed = max(0.0, now - started) if started is not None else None
        remaining = (
            max(0.0, timeout - elapsed)
            if elapsed is not None and timeout is not None else None
        )
        controllers = []
        active = None
        for place in self.joinable_places:
            agent_id = self.place_agents.get(place.number)
            agent = self.agents.get(agent_id) if agent_id is not None else None
            row = evidence.get(place.number)
            if terminalized:
                controller_state = ledger["state"]
            elif ledger["state"] == "synchronizing" or row is None:
                controller_state = "synchronizing"
            elif place.number != active_place:
                controller_state = "inactive_done"
            elif end is not None:
                controller_state = (
                    "ambiguous_ending"
                    if end.get("receipt_state") == "ambiguous" else "ending"
                )
            elif (
                not row["alive"] or row["done"]
                or place.number in self.v2_surrendered_places
            ):
                controller_state = "inactive_done"
            elif row["ready"]:
                controller_state = "awaiting_agent"
            else:
                controller_state = "phase_not_ready"
            controller = {
                "place": place.number,
                "seat_id": place.seat_id,
                "player_name": place.player_name,
                "player_color": place.player_color,
                "controller_label": (
                    agent["controller_label"] if agent is not None else None
                ),
                "state": controller_state,
            }
            controllers.append(controller)
            if place.number == active_place and not terminalized:
                active = dict(controller)
        started_at = ledger.get("deadline_started_at")
        return {
            "state": ledger["state"],
            "turn": key[0] if key is not None else None,
            "phase": key[1] if key is not None else None,
            "reported_phase_counts": list(
                ledger.get("reported_phase_counts", [])
            ),
            "phase_mode": "players_alternate" if key is not None else None,
            "active_controller": active,
            "timing": {
                "mode": self.config["timing_mode"],
                "timeout_s": timeout,
                "deadline_started_at": started_at,
                "deadline_at": (
                    started_at + timeout
                    if started_at is not None and timeout is not None else None
                ),
                "elapsed_s": round(elapsed, 3) if elapsed is not None else None,
                "remaining_s": (
                    round(remaining, 3) if remaining is not None else None
                ),
            },
            "end": (
                {
                    "source": end["source"],
                    "receipt_state": end["receipt_state"],
                }
                if end is not None else None
            ),
            "controllers": controllers,
        }

    def status(self) -> dict[str, Any]:
        with self.condition:
            leaderboard = self._leaderboard()
            value = {
                "schema_version": 1,
                "game_id": self.game_id,
                "state": self.state,
                "benchmark_valid": (
                    self.state == "completed"
                    if self.state in TERMINAL_STATES
                    else (False if self.invalid_reasons else None)
                ),
                "mode": self.config["mode"],
                "control_protocol": self.config["control_protocol"],
                "places": self.config["places"],
                "max_agents": self.max_agents,
                "joined_agents": len(self.agents),
                "turns": self.config["turns"],
                "current_turn": (
                    self._current_turn_locked()
                ),
                "objective": self.config["objective"],
                "timing_mode": self.config["timing_mode"],
                "action_timeout_s": self.config["action_timeout_s"],
                "error": self.error,
                "invalid_reasons": list(self.invalid_reasons),
                "resolved_places": self._public_places(),
                "barrier": self._public_barrier(),
                "leaderboard": leaderboard,
                "outcome": self._outcome(leaderboard),
                **self.urls(),
            }
            if self.config["control_protocol"] == FULL_CONTROL_V2:
                value["phase"] = self._public_v2_phase()
            return value

    def picker_state(self) -> dict[str, Any]:
        """Return a compact public row without replay or artifact I/O."""
        # SCORELOG parsing can grow with the match. Keep it outside the turn
        # condition so a picker poll cannot delay agent submissions.
        leaderboard = self._leaderboard()
        with self.condition:
            resolved_places = []
            for row in self._public_places():
                resolved_places.append({
                    key: row.get(key)
                    for key in (
                        "place", "seat_id", "player_name", "player_color",
                        "controller", "joined", "controller_label",
                        "controller_type", "model",
                    )
                })
            current_turn = self._current_turn_locked()
            public_prefix = urlparse(
                self.supervisor.service_url,
            ).path.rstrip("/")
            return {
                "game_id": self.game_id,
                "state": self.state,
                "created_at": self.created_at,
                "current_turn": current_turn,
                "turns": self.config["turns"],
                "benchmark_valid": (
                    self.state == "completed"
                    if self.state in TERMINAL_STATES
                    else (False if self.invalid_reasons else None)
                ),
                "mode": self.config["mode"],
                "control_protocol": self.config["control_protocol"],
                "timing_mode": self.config["timing_mode"],
                "action_timeout_s": self.config["action_timeout_s"],
                "places": self.config["places"],
                "max_agents": self.max_agents,
                "joined_agents": len(self.agents),
                "resolved_places": resolved_places,
                "leaderboard": leaderboard,
                "outcome": self._outcome(leaderboard),
                "watch_path": f"{public_prefix}/watch/{self.game_id}",
            }

    def watch_state(self) -> dict[str, Any]:
        with self.condition:
            status = self.status()
            timeline = list(self.timeline)
        frame_manifest = self.frame_manifest()
        replay = self._replay_data()
        return {
            "schema_version": 1,
            "label": "Omniscient Freeciv agent match replay",
            "game": status,
            "timeline": timeline,
            "frames": frame_manifest["frames"],
            "replay": {
                "available": replay["available"],
                "url": self.urls()["replay_url"],
            },
            "video": {
                "available": bool(frame_manifest["frames"]),
                "url": self.urls()["video_url"],
                "kind": "video-so-far",
            },
        }

    @staticmethod
    def _replay_int(value: Any, default: int = 0) -> int:
        if (
            isinstance(value, bool) or not isinstance(value, (int, float))
            or not math.isfinite(value) or int(value) != value
        ):
            return default
        return int(value)

    @staticmethod
    def _replay_text(value: Any, limit: int = 160) -> str:
        if not isinstance(value, str):
            return ""
        public = "".join(
            character for character in value
            if character in "\t " or ord(character) >= 0x20
        )
        return _normalize_ruleset_name(public[:limit])

    @staticmethod
    def _file_signature(path: Path) -> tuple[int, int] | None:
        try:
            stat = path.stat()
        except OSError:
            return None
        return stat.st_size, stat.st_mtime_ns

    def _sanitize_replay_player(
        self,
        raw: Any,
        catalog_ids: set[int],
        place_identities: dict[str, tuple[Place, dict[str, Any]]],
        player_identities: dict[int, tuple[Place, dict[str, Any]]] | None = None,
    ) -> dict[str, Any] | None:
        if not isinstance(raw, dict):
            return None
        player_id = self._replay_int(raw.get("player_id"), -1)
        player_name = self._replay_text(raw.get("player_name"), 80)
        if player_id < 0 or not player_name:
            return None
        configured = place_identities.get(player_name)
        if configured is None and player_identities is not None:
            # A full-control-v2 seat is played through a native client, which
            # renames its player to a ruleset leader name.  Those rows are
            # matched by native player number instead, the same handle the
            # episode report attributes by.
            configured = player_identities.get(player_id)
        if configured is not None:
            place, identity = configured
            seat_id = place.seat_id
            place_number: int | None = place.number
            player_color: str | None = place.player_color
            scored = True
        else:
            identity = {
                "controller_label": "Freeciv dynamic faction",
                "controller_type": "dynamic",
                "model": None,
            }
            seat_id = f"dynamic-player-{player_id}"
            place_number = None
            player_color = None
            scored = False

        known = raw.get("known_tech_ids")
        known_ids = sorted({
            normalized for value in known
            if (normalized := self._replay_int(value, -1)) >= 0
            and normalized <= 511
            and (not catalog_ids or normalized in catalog_ids)
        }) if isinstance(known, list) else []
        research_raw = raw.get("research")
        if not isinstance(research_raw, dict):
            research_raw = {}
        raw_research_id = research_raw.get("tech_id")
        research_id = self._replay_int(raw_research_id, -1)
        if (
            raw_research_id is None or research_id < 0 or research_id > 511
            or (catalog_ids and research_id not in catalog_ids)
        ):
            research_id = None
        citizens = self._replay_int(
            raw.get("citizens", raw.get("population")), 0,
        )
        return {
            "seat_id": seat_id,
            "place": place_number,
            "player_id": player_id,
            "player_name": player_name,
            "player_color": player_color,
            **identity,
            "nation": self._replay_text(raw.get("nation"), 80),
            "government": self._replay_text(raw.get("government"), 80),
            "alive": raw.get("alive") is True,
            "score": self._replay_int(raw.get("score"), 0),
            "cities": self._replay_int(raw.get("cities"), 0),
            "citizens": citizens,
            "population": citizens,
            "units": self._replay_int(raw.get("units"), 0),
            "gold": self._replay_int(raw.get("gold"), 0),
            "culture": self._replay_int(raw.get("culture"), 0),
            "known_tech_ids": known_ids,
            "gained_tech_ids": [],
            "lost_tech_ids": [],
            "research": {
                "tech_id": research_id,
                "name": self._replay_text(research_raw.get("name"), 80),
                "bulbs": self._replay_int(research_raw.get("bulbs"), 0),
                "cost": self._replay_int(research_raw.get("cost"), 0),
            },
            "future_techs": self._replay_int(raw.get("future_techs"), 0),
            "scored": scored,
        }

    def _sanitize_replay_snapshot(
        self,
        raw: Any,
        catalog_ids: set[int],
        place_identities: dict[str, tuple[Place, dict[str, Any]]],
        player_identities: dict[int, tuple[Place, dict[str, Any]]] | None = None,
    ) -> dict[str, Any] | None:
        if not isinstance(raw, dict):
            return None
        turn = self._replay_int(raw.get("turn"), -1)
        if turn < 0 or raw.get("game_id") not in {None, self.game_id}:
            return None
        raw_players = raw.get("players")
        if not isinstance(raw_players, list):
            return None
        players = [
            player for player in (
                self._sanitize_replay_player(
                    value, catalog_ids, place_identities, player_identities,
                )
                for value in raw_players
            )
            if player is not None
        ]
        players.sort(key=lambda player: (
            player["place"] is None,
            player["place"] if player["place"] is not None else player["player_id"],
            player["player_name"],
        ))
        return {
            "schema_version": 1,
            "game_id": self.game_id,
            "turn": turn,
            "year": self._replay_int(raw.get("year"), 0),
            "players": players,
        }

    def _start_v2_replay_keepwarm(self) -> None:
        """Keep replay telemetry converted in the background.

        Reconstruction otherwise happens only on the viewer's read path, a
        bounded batch per request — so opening the viewer cold on a
        300-turn game meant watching the scores catch up poll by poll.
        This thread does the same bounded work off the hot paths (never the
        liveness poller), so the viewer opens warm.
        """
        with self.condition:
            if (
                self.v2_replay_producer is None
                or self.v2_replay_keepwarm_thread is not None
            ):
                return
            thread = threading.Thread(
                target=self._keep_v2_replay_warm,
                name=f"freeciv-agent-replay-warm-{self.game_id}",
                daemon=True,
            )
            self.v2_replay_keepwarm_thread = thread
        thread.start()

    def _keep_v2_replay_warm(self) -> None:
        while True:
            with self.condition:
                if self.state in TERMINAL_STATES or self.cancel_requested:
                    return
            try:
                # Converge on backlog (bounded batches), then heartbeat.
                while True:
                    with self.replay_lock:
                        producer = self.v2_replay_producer
                        if producer is None:
                            return
                        appended = producer.refresh()
                    if appended <= 0:
                        break
                    with self.condition:
                        if (
                            self.state in TERMINAL_STATES
                            or self.cancel_requested
                        ):
                            return
            except Exception:
                # Spectator telemetry must never take a thread down; the
                # producer disables itself when it cannot make progress.
                pass
            with self.condition:
                if self.state in TERMINAL_STATES or self.cancel_requested:
                    return
            time.sleep(V2_REPLAY_KEEPWARM_INTERVAL_S)

    def _refresh_v2_replay(self) -> None:
        """Convert any autosave a full-control-v2 game has finished writing."""
        producer = self.v2_replay_producer
        if producer is None:
            return
        try:
            producer.refresh()
        except Exception:
            # Spectator telemetry never fails a read, and the producer already
            # disables itself once it cannot make progress.
            pass

    def _drain_v2_replay(self) -> None:
        """Convert every remaining autosave once the game has finished."""
        producer = self.v2_replay_producer
        if producer is None:
            return
        try:
            with self.replay_lock:
                producer.drain()
        except Exception:
            pass

    def _replay_data(self) -> dict[str, Any]:
        with self.replay_lock:
            self._refresh_v2_replay()
            with self.condition:
                public_places = self._public_places()
                place_identities, player_identities = (
                    self._place_identity_indexes_locked()
                )
                if self.config["control_protocol"] != FULL_CONTROL_V2:
                    # Only a v2 game plays through a renaming native client.
                    # Elsewhere a number match could claim a dynamic faction.
                    player_identities = {}
                identity_signature = sorted(
                    (number, place.seat_id)
                    for number, (place, _identity) in player_identities.items()
                )
            signature = (
                self._file_signature(self.replay_path),
                self._file_signature(self.replay_catalog_path),
                self._file_signature(self.replay_warnings_path),
                _canonical(public_places),
                _canonical(identity_signature),
            )
            if (
                signature == self.replay_cache_signature
                and self.replay_cache is not None
            ):
                return self.replay_cache

            warnings: list[dict[str, Any]] = []
            catalog = None
            catalog_ids: set[int] = set()
            try:
                raw_catalog = json.loads(
                    self.replay_catalog_path.read_text(encoding="utf-8")
                )
                catalog = _classic_technology_catalog(raw_catalog)
                catalog_ids = {
                    technology["id"] for technology in catalog["technologies"]
                }
            except (OSError, UnicodeError, json.JSONDecodeError, ValueError):
                if self.replay_catalog_path.exists():
                    warnings.append({
                        "turn": None,
                        "message": "Technology telemetry is temporarily unavailable.",
                    })

            snapshots_by_turn: dict[int, dict[str, Any]] = {}
            try:
                replay_lines = self.replay_path.read_text(
                    encoding="utf-8", errors="replace",
                ).splitlines()
            except OSError:
                replay_lines = []
            for index, line in enumerate(replay_lines):
                if not line.strip():
                    continue
                try:
                    raw_snapshot = json.loads(line)
                except json.JSONDecodeError:
                    warnings.append({
                        "turn": None,
                        "message": (
                            "Replay telemetry contains an incomplete trailing record."
                            if index == len(replay_lines) - 1 else
                            "Replay telemetry contains an unreadable record."
                        ),
                    })
                    continue
                snapshot = self._sanitize_replay_snapshot(
                    raw_snapshot, catalog_ids, place_identities,
                    player_identities,
                )
                if snapshot is None:
                    warnings.append({
                        "turn": None,
                        "message": "Replay telemetry contains an unreadable record.",
                    })
                    continue
                snapshots_by_turn[snapshot["turn"]] = snapshot

            try:
                warning_lines = self.replay_warnings_path.read_text(
                    encoding="utf-8", errors="replace",
                ).splitlines()
            except OSError:
                warning_lines = []
            for line in warning_lines:
                if not line.strip():
                    continue
                try:
                    warning = json.loads(line)
                except json.JSONDecodeError:
                    warning = None
                turn = (
                    self._replay_int(warning.get("turn"), -1)
                    if isinstance(warning, dict) else -1
                )
                warnings.append({
                    "turn": turn if turn >= 0 else None,
                    "message": "Replay telemetry was unavailable for this turn.",
                })

            snapshots = [
                snapshots_by_turn[turn] for turn in sorted(snapshots_by_turn)
            ]
            previous_known: dict[str, set[int]] = {}
            for snapshot in snapshots:
                for player in snapshot["players"]:
                    current = set(player["known_tech_ids"])
                    previous = previous_known.get(player["seat_id"], set())
                    player["gained_tech_ids"] = sorted(current - previous)
                    player["lost_tech_ids"] = sorted(previous - current)
                    previous_known[player["seat_id"]] = current

            unique_warnings = {
                (warning["turn"], warning["message"]): warning
                for warning in warnings
            }
            sanitized_warnings = sorted(
                unique_warnings.values(),
                key=lambda warning: (
                    warning["turn"] is None,
                    warning["turn"] if warning["turn"] is not None else 0,
                    warning["message"],
                ),
            )[-100:]
            result = {
                "available": bool(snapshots),
                "catalog": catalog,
                "snapshots": snapshots,
                "warnings": sanitized_warnings,
            }
            self.replay_cache_signature = signature
            self.replay_cache = result
            return result

    def phase_events(
        self, after_sequence: int, limit: int,
    ) -> dict[str, Any]:
        """Return the bounded public-safe v2 phase-end provenance feed."""
        with self.condition:
            if self.config["control_protocol"] != FULL_CONTROL_V2:
                raise APIProblem(HTTPStatus.NOT_FOUND, "not found")
            journal = self.v2_phase_event_journal
            if journal is None or self.v2_phase_event_journal_failed:
                raise APIProblem(
                    HTTPStatus.SERVICE_UNAVAILABLE,
                    "phase event provenance is unavailable",
                )
            try:
                page = journal.page(after_sequence, limit)
            except V2PhaseEventJournalError:
                self._invalidate_v2_phase_event_journal_locked()
                raise APIProblem(
                    HTTPStatus.SERVICE_UNAVAILABLE,
                    "phase event provenance is unavailable",
                ) from None
            complete = (
                self.state in TERMINAL_STATES
                and not self.v2_pending_phase_ends
            )
        return {
            "schema_version": 2,
            "control_protocol": FULL_CONTROL_V2,
            "game_id": self.game_id,
            "phase_events": page["items"],
            "next_after_sequence": page["next_after_sequence"],
            "has_more": page["has_more"],
            "complete": complete,
        }

    def replay_state(self, after_turn: int, limit: int) -> dict[str, Any]:
        replay = self._replay_data()
        remaining = [
            snapshot for snapshot in replay["snapshots"]
            if snapshot["turn"] > after_turn
        ]
        page = remaining[:limit]
        return {
            "schema_version": 1,
            "game_id": self.game_id,
            "available": replay["available"],
            "catalog": replay["catalog"],
            "snapshots": page,
            "next_after_turn": page[-1]["turn"] if page else after_turn,
            "has_more": len(remaining) > len(page),
            "complete": self.state in TERMINAL_STATES,
            "replay_warnings": replay["warnings"],
        }

    def result(self) -> dict[str, Any]:
        with self.condition:
            if self.state not in TERMINAL_STATES:
                raise APIProblem(
                    HTTPStatus.CONFLICT, "game result is not ready",
                )
        report_path = self.episode / "report.json"
        if not report_path.exists():
            raise APIProblem(HTTPStatus.NOT_FOUND, "game report is unavailable")
        value = json.loads(report_path.read_text(encoding="utf-8"))
        if isinstance(value, dict):
            value.pop("episode", None)
            with self.condition:
                leaderboard = self._leaderboard()
                value.update(
                    {
                        "state": self.state,
                        "benchmark_valid": self.state == "completed",
                        "invalid_reasons": list(self.invalid_reasons),
                        "leaderboard": leaderboard,
                        "outcome": self._outcome(leaderboard),
                    }
                )
            value["artifact_id"] = self.game_id
            value["artifact_urls"] = {
                "status": self.urls()["status_url"],
                "watch": self.urls()["watch_url"],
                "replay": self.urls()["replay_url"],
                "frames": self.urls()["frames_url"],
                "video": self.urls()["video_url"],
            }
        return value

    def _ppm_frames(self) -> list[Path]:
        return sorted(self.episode.rglob("*.ppm"))

    def _stable_ppm_frames(self) -> list[Path]:
        candidates = self._ppm_frames()
        if not candidates:
            return []
        before: dict[Path, tuple[int, int]] = {}
        for path in candidates:
            try:
                stat = path.stat()
            except OSError:
                continue
            before[path] = (stat.st_size, stat.st_mtime_ns)
        time.sleep(0.02)
        stable = []
        for path, signature in before.items():
            try:
                stat = path.stat()
            except OSError:
                continue
            if signature == (stat.st_size, stat.st_mtime_ns) and stat.st_size:
                stable.append(path)
        return stable

    def _save_files(self) -> list[Path]:
        suffixes = (".sav", ".sav.gz", ".sav.bz2", ".sav.xz", ".sav.zst")
        return sorted(
            path for path in (self.episode / "saves").iterdir()
            if path.is_file() and path.name.endswith(suffixes)
        )

    @staticmethod
    def _frame_turn(path: Path) -> int | None:
        match = FRAME_TURN_RE.search(path.name)
        return int(match.group(1)) if match else None

    def _ppm_map_players(
        self,
        path: Path,
        place_identities: dict[str, tuple[Place, dict[str, Any]]],
    ) -> list[dict[str, Any]]:
        parsed: list[dict[str, Any]] = []
        try:
            with path.open("r", encoding="utf-8", errors="replace") as stream:
                for line_number, line in enumerate(stream):
                    if line_number > 512:
                        break
                    if line_number > 0 and not line.startswith("#"):
                        if parsed or line.strip() and line.strip() != "P3":
                            break
                        continue
                    match = PPM_PLAYER_RE.fullmatch(line.rstrip("\r\n"))
                    if not match:
                        continue
                    red, green, blue = (
                        int(match.group(index)) for index in (2, 3, 4)
                    )
                    if any(not 0 <= component <= 255 for component in (
                        red, green, blue,
                    )):
                        continue
                    player_id = int(match.group(1))
                    player_name = self._replay_text(
                        match.group(5).replace(r'\"', '"').replace(r"\\", "\\"),
                        80,
                    )
                    if not player_name:
                        continue
                    configured = place_identities.get(player_name)
                    row: dict[str, Any] = {
                        "player_id": player_id,
                        "player_name": player_name,
                        "player_color": f"#{red:02X}{green:02X}{blue:02X}",
                    }
                    if configured is not None:
                        place, identity = configured
                        row.update({
                            "seat_id": place.seat_id,
                            "place": place.number,
                            **identity,
                            "scored": True,
                        })
                    else:
                        row.update({
                            "seat_id": f"dynamic-player-{player_id}",
                            "place": None,
                            "controller_label": "Freeciv dynamic faction",
                            "controller_type": "dynamic",
                            "scored": False,
                        })
                    parsed.append(row)
        except OSError:
            return []
        parsed.sort(key=lambda player: (player["player_id"], player["player_name"]))
        return parsed

    def _frame_metadata(
        self,
        path: Path,
        identity_signature: str,
        place_identities: dict[str, tuple[Place, dict[str, Any]]],
    ) -> dict[str, Any]:
        try:
            stat = path.stat()
            signature = (stat.st_size, stat.st_mtime_ns, identity_signature)
        except OSError:
            signature = (0, 0, identity_signature)
        with self.frame_metadata_lock:
            cached = self.frame_metadata_cache.get(path)
            if cached is not None and cached[0] == signature:
                return cached[1]
            metadata = {
                "turn": self._frame_turn(path),
                "map_players": self._ppm_map_players(path, place_identities),
            }
            self.frame_metadata_cache[path] = (signature, metadata)
            return metadata

    def frame_manifest(self) -> dict[str, Any]:
        frames = self._ppm_frames()
        with self.condition:
            public_places = self._public_places()
            place_identities = {
                place.player_name: (place, self._place_identity(place))
                for place in self.places
            }
        identity_signature = _canonical(public_places)
        with self.frame_metadata_lock:
            current_paths = set(frames)
            self.frame_metadata_cache = {
                path: value for path, value in self.frame_metadata_cache.items()
                if path in current_paths
            }
        return {
            "schema_version": 1,
            "game_id": self.game_id,
            "label": "Omniscient strategic map snapshots (not GUI video)",
            "frames": [
                {
                    "index": index,
                    **self._frame_metadata(
                        frame, identity_signature, place_identities,
                    ),
                    "source_name": frame.name,
                    "png_url": (
                        f"{self.supervisor.service_url}/v1/games/"
                        f"{self.game_id}/frames/{index}.png"
                    ),
                }
                for index, frame in enumerate(frames)
            ],
            "latest_png_url": (
                f"{self.supervisor.service_url}/v1/games/"
                f"{self.game_id}/frames/latest.png"
                if frames else None
            ),
        }

    def png_frame(self, index: int | None) -> Path:
        with self.frame_lock:
            frames = self._ppm_frames()
            if not frames:
                raise APIProblem(HTTPStatus.NOT_FOUND, "no map frames are available")
            resolved = len(frames) - 1 if index is None else index
            if not 0 <= resolved < len(frames):
                raise APIProblem(HTTPStatus.NOT_FOUND, "map frame does not exist")
            destination = self.episode / "watch_frames" / f"{resolved:06d}.png"
            if destination.exists() and destination.stat().st_mtime >= frames[resolved].stat().st_mtime:
                return destination
            ffmpeg = shutil.which("ffmpeg")
            if ffmpeg is None:
                raise APIProblem(
                    HTTPStatus.SERVICE_UNAVAILABLE,
                    "ffmpeg is required to convert map snapshots",
                )
            temporary = destination.with_name(
                f".{destination.stem}.{secrets.token_hex(5)}.tmp.png"
            )
            try:
                subprocess.run(
                    [
                        ffmpeg, "-loglevel", "error", "-y",
                        "-i", str(frames[resolved]), "-frames:v", "1",
                        str(temporary),
                    ],
                    check=True,
                    capture_output=True,
                )
                os.replace(temporary, destination)
            except subprocess.CalledProcessError as exc:
                raise APIProblem(
                    HTTPStatus.NOT_FOUND,
                    "map frame is still being written",
                ) from exc
            finally:
                temporary.unlink(missing_ok=True)
            return destination

    def _render_video(self, *, force: bool = False) -> Path:
        with self.video_lock:
            frames = self._stable_ppm_frames()
            if not frames:
                raise SupervisorError("no stable map frames are available")
            signature_value = [
                {
                    "name": str(frame.relative_to(self.episode)),
                    "size": frame.stat().st_size,
                    "mtime_ns": frame.stat().st_mtime_ns,
                }
                for frame in frames
            ]
            signature = hashlib.sha256(
                _canonical(signature_value).encode("utf-8")
            ).hexdigest()
            destination = self.episode / "game.mp4"
            cache_path = self.episode / "video-cache.json"
            cached_signature = self.video_frame_signature
            if cached_signature is None and cache_path.exists():
                try:
                    cached_signature = json.loads(
                        cache_path.read_text(encoding="utf-8")
                    ).get("frame_signature")
                except (OSError, json.JSONDecodeError, AttributeError):
                    cached_signature = None
            if (
                not force and destination.exists()
                and cached_signature == signature
            ):
                return destination
            ffmpeg = shutil.which("ffmpeg")
            if ffmpeg is None:
                raise SupervisorError("ffmpeg is required to render video")
            temporary = self.episode / f".game.{secrets.token_hex(5)}.tmp.mp4"
            concat = self.episode / f".frames.{secrets.token_hex(5)}.ffconcat"
            concat.write_text(
                "ffconcat version 1.0\n"
                + "".join(
                    f"file '{str(frame).replace(chr(39), chr(39) + chr(92) + chr(39) + chr(39))}'\n"
                    "duration 0.250000\n"
                    for frame in frames
                )
                + f"file '{frames[-1]}'\n",
                encoding="utf-8",
            )
            try:
                subprocess.run(
                    [
                        ffmpeg, "-loglevel", "error", "-y", "-safe", "0",
                        "-i", str(concat),
                        "-vf", "pad=ceil(iw/2)*2:ceil(ih/2)*2",
                        "-pix_fmt", "yuv420p", str(temporary),
                    ],
                    check=True,
                    capture_output=True,
                )
                os.replace(temporary, destination)
                self.video_frame_signature = signature
                _atomic_json(
                    cache_path,
                    {
                        "schema_version": 1,
                        "frame_signature": signature,
                        "frame_count": len(frames),
                        "updated_at": time.time(),
                    },
                )
            except subprocess.CalledProcessError as exc:
                raise SupervisorError(
                    "could not render stable map frames"
                ) from exc
            finally:
                concat.unlink(missing_ok=True)
                temporary.unlink(missing_ok=True)
            return destination

    def video(self) -> Path:
        try:
            return self._render_video(
                force=self.state in TERMINAL_STATES
                and self.video_frame_signature is None,
            )
        except SupervisorError as exc:
            raise APIProblem(HTTPStatus.NOT_FOUND, str(exc)) from exc

    def watch_html(self) -> str:
        """Return the committed React entrypoint without injecting game data."""
        return _viewer_html("index.html")


class Supervisor:
    """Registry and process owner for many concurrent games."""

    def __init__(
        self,
        runs_root: str | Path,
        admin_token: str,
        *,
        binary: str | Path | None = None,
        process_factory: Callable[..., Any] = subprocess.Popen,
        agent_binary: str | Path | None = None,
        sidecar_factory: Callable[..., Any] | None = None,
    ):
        if not admin_token:
            raise SupervisorError("admin bearer token must not be empty")
        self.admin_token_hash = _digest(admin_token)
        self.runs_root = Path(runs_root).resolve()
        self.runs_root.mkdir(parents=True, exist_ok=True)
        # Where reconstructed autosave telemetry is cached.  This is the same
        # location the replay gateway is given, so a live full-control-v2 game
        # and the archived viewer parse each autosave once between them.
        self.replay_cache_root = self.runs_root.parent / "replay-cache"
        configured = Path(binary) if binary else REPO_ROOT / "build-agent" / "freeciv-server"
        if not configured.is_absolute():
            configured = (REPO_ROOT / configured).resolve()
        if process_factory is subprocess.Popen and (
            not configured.is_file() or not os.access(configured, os.X_OK)
        ):
            raise SupervisorError(f"Freeciv server is not executable: {configured}")
        self.binary = configured
        self.process_factory = process_factory
        configured_agent = (
            Path(agent_binary) if agent_binary
            else REPO_ROOT / "build-control-v2" / "freeciv-agent"
        )
        if not configured_agent.is_absolute():
            configured_agent = (REPO_ROOT / configured_agent).resolve()
        self.agent_binary = configured_agent
        self.sidecar_factory = sidecar_factory or HeadlessSidecar
        self._default_sidecar_factory = sidecar_factory is None
        self.service_url = "http://127.0.0.1:8765"
        self.internal_service_url = "http://127.0.0.1:8765"
        self.games: dict[str, Game] = {}
        self.reserved_game_ids: set[str] = set()
        self.reserved_game_ports: set[int] = set()
        self.lock = threading.RLock()
        self.registry_condition = threading.Condition(self.lock)
        self.closing = False
        self.shutdown_event = threading.Event()
        self.started_at = time.time()
        self.finalize_orphaned_runs()

    def finalize_orphaned_runs(self) -> list[str]:
        """Give a terminal record to runs a dead supervisor left mid-game.

        A supervisor that exits without finalizing leaves a run whose
        manifest still says ``running``: not live anywhere, not terminal on
        disk, so the 596-turn game_a8 replay vanished from every index while
        sitting complete in its run directory. On startup, any run that is
        non-terminal, not one of this process's games, and quiet for
        ``ORPHAN_RUN_QUIET_S`` (a live game writes telemetry constantly, so
        stale means dead — and the margin keeps a second supervisor sharing
        this runs root from finalizing its neighbour's game) is closed out as
        ``cancelled`` with a real report, exactly as if it had been ended on
        purpose.
        """
        finalized: list[str] = []
        try:
            candidates = sorted(self.runs_root.iterdir())
        except OSError:
            return finalized
        now = time.time()
        for run in candidates:
            if run.is_symlink() or not run.is_dir():
                continue
            game_id = run.name
            if not GAME_ID_RE.fullmatch(game_id):
                continue
            with self.lock:
                if game_id in self.games or game_id in self.reserved_game_ids:
                    continue
            try:
                manifest = json.loads(
                    (run / "manifest.json").read_text(encoding="utf-8"),
                )
            except (OSError, UnicodeError, json.JSONDecodeError):
                continue
            if not isinstance(manifest, dict):
                continue
            if manifest.get("game_id") != game_id:
                continue
            state = manifest.get("state", manifest.get("status"))
            if state in TERMINAL_STATES:
                continue
            newest = 0.0
            for name in ("replay.jsonl", "phase-events.jsonl",
                         "score.log", "manifest.json"):
                try:
                    newest = max(newest, (run / name).stat().st_mtime)
                except OSError:
                    continue
            if now - newest < ORPHAN_RUN_QUIET_S:
                continue
            manifest["state"] = manifest["status"] = "cancelled"
            manifest["finished_at"] = newest or now
            if manifest.get("error") is None:
                manifest["error"] = (
                    "supervisor exited before this game finished; "
                    "finalized at startup from the last recorded state"
                )
            turn = _last_recorded_turn(run)
            if turn is not None:
                manifest["current_turn"] = turn
            # The manifest lands first: summarize_episode reads it back from
            # disk, and a run with a broken scorelog must still end up
            # terminal rather than vanish again.
            try:
                _atomic_json(run / "manifest.json", manifest)
            except OSError:
                continue
            try:
                summary = summarize_episode(
                    run,
                    private_player_seats=_orphan_player_seats(run, manifest),
                )
                _atomic_json(run / "report.json", summary)
            except Exception:
                pass
            finalized.append(game_id)
        return finalized

    def reserve_game_port(self) -> int:
        """Reserve an explicit loopback port until a child has bound it."""
        with self.lock:
            for _ in range(100):
                with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
                    probe.bind(("127.0.0.1", 0))
                    port = int(probe.getsockname()[1])
                if port not in self.reserved_game_ports:
                    self.reserved_game_ports.add(port)
                    return port
        raise SupervisorError("could not allocate a loopback Freeciv port")

    def release_game_port(self, port: int) -> None:
        with self.lock:
            self.reserved_game_ports.discard(port)

    def authorize_admin(self, token: str | None) -> None:
        if token is None:
            raise APIProblem(HTTPStatus.UNAUTHORIZED, "admin bearer token required")
        if not _same_token(token, self.admin_token_hash):
            raise APIProblem(HTTPStatus.FORBIDDEN, "admin token is not authorized")

    def _config(self, payload: Any) -> dict[str, Any]:
        if not isinstance(payload, dict):
            raise APIProblem(HTTPStatus.BAD_REQUEST, "game request must be an object")
        known = {
            "mode", "places", "turns", "seed", "ruleset", "objective",
            "timing_mode", "action_timeout_s", "lobby_timeout_s", "frame_interval",
            "frame_zoom", "control_protocol", "difficulty",
        }
        unknown = sorted(set(payload) - known)
        if unknown:
            raise APIProblem(
                HTTPStatus.BAD_REQUEST,
                f"game request has unknown fields: {unknown}",
            )
        mode = payload.get("mode", "single")
        if mode not in {"single", "multiplayer"}:
            raise APIProblem(
                HTTPStatus.BAD_REQUEST,
                "mode must be single or multiplayer",
            )
        places = _integer(
            payload.get("places", 2), "places", minimum=2, maximum=16,
        )
        turns = _integer(
            payload.get("turns", 5000), "turns", minimum=1, maximum=5000,
        )
        seed = _integer(
            payload.get("seed", secrets.randbelow(2_147_483_647) + 1),
            "seed", minimum=1, maximum=2_147_483_647,
        )
        ruleset = payload.get("ruleset", "classic")
        if ruleset != "classic":
            raise APIProblem(
                HTTPStatus.BAD_REQUEST,
                "this vertical slice requires ruleset classic",
            )
        objective = payload.get(
            "objective", "Maximize final Freeciv civilization score.",
        )
        if not isinstance(objective, str) or not objective.strip():
            raise APIProblem(
                HTTPStatus.BAD_REQUEST,
                "objective must be a non-empty string",
            )
        try:
            control_protocol = validate_control_protocol(
                payload.get("control_protocol", STRATEGIC_V1),
            )
        except FullControlSchemaError as exc:
            raise APIProblem(HTTPStatus.BAD_REQUEST, str(exc)) from exc
        timing_timeouts = (
            V2_TIMING_MODE_TIMEOUTS
            if control_protocol == FULL_CONTROL_V2
            else TIMING_MODE_TIMEOUTS
        )
        raw_timing_mode = payload.get("timing_mode")
        timeout_supplied = "action_timeout_s" in payload
        if (
            raw_timing_mode is not None
            and raw_timing_mode not in timing_timeouts
        ):
            raise APIProblem(
                HTTPStatus.BAD_REQUEST,
                "timing_mode must be "
                + " or ".join(sorted(timing_timeouts))
                + (
                    " for full-control-v2 (blitz is strategic-v1 only)"
                    if control_protocol == FULL_CONTROL_V2 else ""
                ),
            )
        if raw_timing_mode is None:
            if not timeout_supplied:
                timing_mode = "default"
                action_timeout_s = timing_timeouts[timing_mode]
            elif payload.get("action_timeout_s") is None:
                timing_mode = "infinite"
                action_timeout_s = None
            else:
                action_timeout_s = _finite_number(
                    payload.get("action_timeout_s"),
                    "action_timeout_s", minimum=0.1,
                )
                timing_mode = next((
                    name for name, timeout in timing_timeouts.items()
                    if timeout == action_timeout_s
                ), "custom")
        else:
            timing_mode = raw_timing_mode
            expected_timeout = timing_timeouts[timing_mode]
            if timeout_supplied:
                supplied_timeout = payload.get("action_timeout_s")
                if expected_timeout is None:
                    if supplied_timeout is not None:
                        raise APIProblem(
                            HTTPStatus.BAD_REQUEST,
                            "infinite timing requires action_timeout_s null",
                        )
                else:
                    normalized_timeout = _finite_number(
                        supplied_timeout,
                        "action_timeout_s", minimum=0.1,
                    )
                    if normalized_timeout != expected_timeout:
                        raise APIProblem(
                            HTTPStatus.BAD_REQUEST,
                            f"{timing_mode} timing requires "
                            f"action_timeout_s {expected_timeout:g}",
                        )
            action_timeout_s = expected_timeout
        lobby_timeout_s = _finite_number(
            payload.get("lobby_timeout_s", 300),
            "lobby_timeout_s", minimum=0.1, allow_zero=True,
        )
        frame_interval = _integer(
            payload.get("frame_interval", 1),
            "frame_interval", minimum=0, maximum=99,
        )
        frame_zoom = _integer(
            payload.get("frame_zoom", 1),
            "frame_zoom", minimum=1, maximum=1,
        )
        difficulty = payload.get("difficulty", "hard")
        if difficulty not in AI_DIFFICULTY_LEVELS:
            raise APIProblem(
                HTTPStatus.BAD_REQUEST,
                "difficulty must be one of "
                + ", ".join(AI_DIFFICULTY_LEVELS),
            )
        return {
            "mode": mode,
            "places": places,
            "turns": turns,
            "seed": seed,
            "ruleset": ruleset,
            "objective": objective.strip(),
            "control_protocol": control_protocol,
            "difficulty": difficulty,
            "timing_mode": timing_mode,
            "action_timeout_s": action_timeout_s,
            "lobby_timeout_s": lobby_timeout_s,
            "frame_interval": frame_interval,
            "frame_zoom": frame_zoom,
        }

    def create_game(self, payload: Any) -> dict[str, Any]:
        config = self._config(payload)
        if (
            config["control_protocol"] == FULL_CONTROL_V2
            and self._default_sidecar_factory
            and (
                not self.agent_binary.is_file()
                or not os.access(self.agent_binary, os.X_OK)
            )
        ):
            raise APIProblem(
                HTTPStatus.SERVICE_UNAVAILABLE,
                f"full-control-v2 sidecar is not executable: {self.agent_binary}",
                structured_error(
                    "sidecar_unavailable",
                    "the full-control-v2 native sidecar is unavailable",
                    retryable=False,
                ),
            )
        with self.registry_condition:
            if self.closing:
                raise APIProblem(
                    HTTPStatus.SERVICE_UNAVAILABLE,
                    "supervisor is shutting down",
                )
            while True:
                game_id = f"game_{secrets.token_urlsafe(18)}"
                if (
                    game_id not in self.games
                    and game_id not in self.reserved_game_ids
                    and not (self.runs_root / game_id).exists()
                ):
                    break
            self.reserved_game_ids.add(game_id)
            owner_token = _token()
            join_token = _token()
            internal_token = _token()
        try:
            game = Game(
                self, game_id, config, owner_token, join_token, internal_token,
            )
        except Exception:
            with self.registry_condition:
                self.reserved_game_ids.discard(game_id)
                self.registry_condition.notify_all()
            raise
        with self.registry_condition:
            if self.closing:
                register = False
            else:
                self.games[game_id] = game
                self.reserved_game_ids.discard(game_id)
                self.registry_condition.notify_all()
                register = True
        if not register:
            try:
                self._stop_game_and_wait(game)
            finally:
                game.close_v2_receipts()
                with self.registry_condition:
                    self.reserved_game_ids.discard(game_id)
                    self.registry_condition.notify_all()
            raise APIProblem(
                HTTPStatus.SERVICE_UNAVAILABLE,
                "supervisor shut down while the game was starting",
            )
        return {
            "schema_version": 1,
            "game_id": game_id,
            "state": "lobby",
            "owner_token": owner_token,
            "join_token": join_token,
            "mode": config["mode"],
            "control_protocol": config["control_protocol"],
            "timing_mode": config["timing_mode"],
            "action_timeout_s": config["action_timeout_s"],
            "places": config["places"],
            "max_agents": game.max_agents,
            "resolved_places": game._public_places(),
            "artifact_dir": str(game.episode),
            **game.urls(),
        }

    def game(self, game_id: str) -> Game:
        if not GAME_ID_RE.fullmatch(game_id):
            raise APIProblem(HTTPStatus.NOT_FOUND, "game not found")
        with self.lock:
            game = self.games.get(game_id)
        if game is None:
            raise APIProblem(HTTPStatus.NOT_FOUND, "game not found")
        return game

    def games_index(self) -> dict[str, Any]:
        """Snapshot the current in-memory registry for the public picker."""
        with self.lock:
            games = tuple(self.games.values())
        rows = [game.picker_state() for game in games]
        rows.sort(
            key=lambda row: (row["created_at"], row["game_id"]),
            reverse=True,
        )
        return {"schema_version": 1, "games": rows}

    def close(self) -> None:
        with self.registry_condition:
            self.closing = True
            self.shutdown_event.set()
            games = list(self.games.values())
        for game in games:
            try:
                self._stop_game_and_wait(game)
            finally:
                game.close_v2_receipts()
        with self.registry_condition:
            while self.reserved_game_ids:
                self.registry_condition.wait(0.1)

    @staticmethod
    def _stop_game_and_wait(game: Game) -> None:
        if game.state not in TERMINAL_STATES:
            game.cancel()
        process = game.process
        if process is not None and process.poll() is None:
            try:
                process.wait(timeout=6)
            except subprocess.TimeoutExpired:
                try:
                    process.kill()
                except OSError:
                    pass
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    pass
        if game.monitor_thread is not None:
            game.monitor_thread.join(timeout=5)


class SupervisorHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(
        self, address: tuple[str, int], supervisor: Supervisor,
        public_url: str | None = None,
    ):
        self.supervisor = supervisor
        if ":" in address[0]:
            self.address_family = socket.AF_INET6
        super().__init__(address, SupervisorHandler)
        host, port = self.server_address[:2]
        if host in {"", "0.0.0.0"}:
            reachable_host = "127.0.0.1"
        elif host == "::":
            reachable_host = "::1"
        else:
            reachable_host = host
        url_host = (
            f"[{reachable_host}]" if ":" in reachable_host
            else reachable_host
        )
        supervisor.internal_service_url = f"http://{url_host}:{port}"
        supervisor.service_url = (
            public_url.rstrip("/") if public_url
            else f"http://{url_host}:{port}"
        )
        self.public_path_prefix = (
            urlparse(public_url).path.rstrip("/") if public_url else ""
        )


class SupervisorHandler(BaseHTTPRequestHandler):
    server: SupervisorHTTPServer

    def log_message(self, format: str, *args: object) -> None:
        return

    def _bearer(self) -> str | None:
        value = self.headers.get("Authorization", "")
        if not value.startswith("Bearer "):
            return None
        token = value[len("Bearer "):]
        return token if token else None

    def _authenticate_v2(self, game: Game) -> tuple[str, dict[str, Any]]:
        try:
            return game.authenticate_agent(self._bearer())
        except APIProblem as exc:
            message = "agent authentication failed"
            raise APIProblem(
                exc.status,
                message,
                structured_error(
                    "invalid_request", message, retryable=False,
                ),
            ) from exc

    def _body(self) -> Any:
        length_text = self.headers.get("Content-Length", "0")
        try:
            length = int(length_text)
        except ValueError as exc:
            raise APIProblem(
                HTTPStatus.BAD_REQUEST, "invalid Content-Length",
            ) from exc
        if length < 0 or length > 1_000_000:
            raise APIProblem(HTTPStatus.BAD_REQUEST, "request body is too large")
        if length == 0:
            return {}
        try:
            return json.loads(self.rfile.read(length).decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise APIProblem(HTTPStatus.BAD_REQUEST, "request body must be JSON") from exc

    def _v2_batch_body(self, game: Game) -> Any:
        """Read one strict JSON body while keeping all parse details private."""
        invalid = lambda: game._v2_problem(
            HTTPStatus.BAD_REQUEST,
            "invalid_batch",
            "the full-control-v2 command batch is invalid",
            retryable=False,
        )
        try:
            lengths = self.headers.get_all("Content-Length") or []
            if (
                len(lengths) != 1
                or self.headers.get("Transfer-Encoding") is not None
                or re.fullmatch(r"[1-9][0-9]{0,6}", lengths[0]) is None
            ):
                raise invalid()
            length = int(lengths[0])
            if length > 1_000_000:
                raise invalid()
            encoded = self.rfile.read(length)
            if len(encoded) != length:
                raise invalid()

            def exact_pairs(items: list[tuple[str, Any]]) -> dict[str, Any]:
                value: dict[str, Any] = {}
                for key, item in items:
                    if key in value:
                        raise ValueError("duplicate key")
                    value[key] = item
                return value

            return json.loads(
                encoded.decode("utf-8", "strict"),
                object_pairs_hook=exact_pairs,
                parse_constant=lambda _value: (_ for _ in ()).throw(
                    ValueError("non-finite number")
                ),
            )
        except APIProblem:
            raise
        except Exception:
            raise invalid() from None

    def _send(
        self, status: int, body: bytes, content_type: str,
        headers: dict[str, str] | None = None,
    ) -> None:
        response_headers = {
            "Cache-Control": "no-store",
            "X-Content-Type-Options": "nosniff",
        }
        if headers:
            response_headers.update(headers)
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        for name, value in response_headers.items():
            self.send_header(name, value)
        self.end_headers()
        try:
            self.wfile.write(body)
        except BrokenPipeError:
            pass

    def _json(self, status: int, value: Any) -> None:
        self._send(
            status,
            _canonical(value).encode("utf-8"),
            "application/json; charset=utf-8",
        )

    def _file(self, path: Path, content_type: str) -> None:
        self._send(
            HTTPStatus.OK, path.read_bytes(), content_type,
            {"Cache-Control": "public, max-age=2"},
        )

    def _v2_openapi(self) -> None:
        try:
            body = V2_OPENAPI_PATH.read_bytes()
            value = json.loads(body.decode("utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise APIProblem(
                HTTPStatus.SERVICE_UNAVAILABLE,
                "the full-control-v2 OpenAPI contract is unavailable",
            ) from exc
        if not isinstance(value, dict):
            raise APIProblem(
                HTTPStatus.SERVICE_UNAVAILABLE,
                "the full-control-v2 OpenAPI contract is unavailable",
            )
        self._send(
            HTTPStatus.OK,
            _canonical(value).encode("utf-8"),
            "application/json; charset=utf-8",
            {"Cache-Control": "no-store"},
        )

    def _viewer_asset(self, name: str) -> None:
        suffix = Path(name).suffix.lower()
        if (
            not VIEWER_ASSET_NAME_RE.fullmatch(name)
            or suffix not in VIEWER_ASSET_CONTENT_TYPES
        ):
            raise APIProblem(HTTPStatus.NOT_FOUND, "viewer asset not found")
        root = (VIEWER_DIST_ROOT / "assets").resolve()
        path = (root / name).resolve()
        if path.parent != root or not path.is_file():
            raise APIProblem(HTTPStatus.NOT_FOUND, "viewer asset not found")
        try:
            body = path.read_bytes()
        except OSError as exc:
            raise APIProblem(
                HTTPStatus.NOT_FOUND, "viewer asset not found",
            ) from exc
        self._send(
            HTTPStatus.OK,
            body,
            VIEWER_ASSET_CONTENT_TYPES[suffix],
            {
                "Cache-Control": "public, max-age=31536000, immutable",
                "Content-Security-Policy": VIEWER_CONTENT_SECURITY_POLICY,
            },
        )

    def _route_path(self, path: str) -> str:
        """Strip only the configured public mount, preserving internal routes."""
        prefix = self.server.public_path_prefix
        if prefix and path.startswith(prefix + "/"):
            return path[len(prefix):]
        return path

    def _route_game(self, parts: list[str], offset: int = 3) -> Game:
        if len(parts) <= offset:
            raise APIProblem(HTTPStatus.NOT_FOUND, "not found")
        return self.server.supervisor.game(parts[offset])

    def do_GET(self) -> None:
        try:
            parsed = urlparse(self.path)
            prefix = self.server.public_path_prefix
            if prefix and parsed.path == prefix:
                location = prefix + "/"
                if parsed.query:
                    location += f"?{parsed.query}"
                self._send(
                    HTTPStatus.PERMANENT_REDIRECT,
                    b"",
                    "text/plain; charset=utf-8",
                    {"Location": location},
                )
                return
            route_path = self._route_path(parsed.path)
            parts = route_path.strip("/").split("/") if route_path != "/" else []
            if route_path == "/":
                self._send(
                    HTTPStatus.OK,
                    _viewer_html("arena.html").encode("utf-8"),
                    "text/html; charset=utf-8",
                    {
                        "Content-Security-Policy": VIEWER_CONTENT_SECURITY_POLICY,
                        "Referrer-Policy": "no-referrer",
                    },
                )
                return
            if len(parts) == 3 and parts[:2] == ["viewer", "assets"]:
                self._viewer_asset(parts[2])
                return
            if route_path == "/health":
                self._json(
                    HTTPStatus.OK,
                    {
                        "ok": True,
                        "native_viewer_protocol": NATIVE_VIEWER_PROTOCOL,
                        "uptime_s": round(
                            time.time() - self.server.supervisor.started_at, 3,
                        ),
                        "games": len(self.server.supervisor.games),
                    },
                )
                return
            if route_path == "/v2/openapi.json":
                if parsed.query:
                    raise APIProblem(HTTPStatus.BAD_REQUEST, "invalid request")
                self._v2_openapi()
                return
            if parts == ["v1", "games"]:
                self._json(
                    HTTPStatus.OK, self.server.supervisor.games_index(),
                )
                return
            if len(parts) == 2 and parts[0] == "watch":
                game = self.server.supervisor.game(parts[1])
                if route_path.endswith("/"):
                    location = f"../{parts[1]}"
                    if parsed.query:
                        location += f"?{parsed.query}"
                    self._send(
                        HTTPStatus.PERMANENT_REDIRECT,
                        b"",
                        "text/plain; charset=utf-8",
                        {"Location": location},
                    )
                    return
                self._send(
                    HTTPStatus.OK, game.watch_html().encode("utf-8"),
                    "text/html; charset=utf-8",
                    {
                        "Content-Security-Policy": VIEWER_CONTENT_SECURITY_POLICY,
                        "Referrer-Policy": "no-referrer",
                    },
                )
                return
            if len(parts) >= 5 and parts[:2] == ["v2", "games"]:
                game = self.server.supervisor.game(parts[2])
                if game.config["control_protocol"] != FULL_CONTROL_V2:
                    raise APIProblem(
                        HTTPStatus.CONFLICT,
                        "full-control-v2 routes are unavailable for this game",
                        structured_error(
                            "unsupported_protocol",
                            "this game uses strategic-v1",
                            retryable=False,
                        ),
                    )
                if parts[3] != "me":
                    raise APIProblem(HTTPStatus.NOT_FOUND, "not found")
                agent_id, _ = self._authenticate_v2(game)
                suffix = parts[4:]
                if suffix == ["health"]:
                    self._json(HTTPStatus.OK, game.v2_health(agent_id))
                    return
                if suffix == ["wait"]:
                    if not parsed.query.isascii():
                        raise game._v2_problem(
                            HTTPStatus.BAD_REQUEST,
                            "invalid_request",
                            "the full-control-v2 wait request is invalid",
                            retryable=False,
                        )
                    query: dict[str, str] = {}
                    if parsed.query:
                        components = parsed.query.split("&")
                        if len(components) > 3 or any(
                            not component or component.count("=") != 1
                            for component in components
                        ):
                            raise game._v2_problem(
                                HTTPStatus.BAD_REQUEST,
                                "invalid_request",
                                "the full-control-v2 wait request is invalid",
                                retryable=False,
                            )
                        for component in components:
                            name, value = component.split("=", 1)
                            if (
                                name not in {
                                    "wait_s", "until", "after_state_token",
                                }
                                or name in query or not value
                            ):
                                raise game._v2_problem(
                                    HTTPStatus.BAD_REQUEST,
                                    "invalid_request",
                                    "the full-control-v2 wait request is invalid",
                                    retryable=False,
                                )
                            try:
                                value = unquote(value, errors="strict")
                            except UnicodeDecodeError as exc:
                                raise game._v2_problem(
                                    HTTPStatus.BAD_REQUEST,
                                    "invalid_request",
                                    "the full-control-v2 wait request is invalid",
                                    retryable=False,
                                ) from exc
                            if not value or "%" in value:
                                raise game._v2_problem(
                                    HTTPStatus.BAD_REQUEST,
                                    "invalid_request",
                                    "the full-control-v2 wait request is invalid",
                                    retryable=False,
                                )
                            query[name] = value
                    until = query.get("until", "phase")
                    wait_text = query.get("wait_s", "120")
                    if re.fullmatch(
                        r"(?:0|[1-9][0-9]{0,2})(?:\.[0-9]{1,3})?",
                        wait_text,
                    ) is None:
                        raise game._v2_problem(
                            HTTPStatus.BAD_REQUEST,
                            "invalid_request",
                            "the full-control-v2 wait request is invalid",
                            retryable=False,
                        )
                    wait_s = float(wait_text)
                    if (
                        wait_s > 300
                        or until not in {"phase", "revision"}
                        or until == "phase" and "after_state_token" in query
                        or until == "revision"
                        and set(query) not in (
                            {"until", "after_state_token"},
                            {"wait_s", "until", "after_state_token"},
                        )
                    ):
                        raise game._v2_problem(
                            HTTPStatus.BAD_REQUEST,
                            "invalid_request",
                            "the full-control-v2 wait request is invalid",
                            retryable=False,
                        )
                    self._json(
                        HTTPStatus.OK,
                        game.v2_wait(
                            agent_id,
                            wait_s,
                            until=until,
                            after_state_token=query.get("after_state_token"),
                        ),
                    )
                    return
                if suffix == ["state"]:
                    self._json(
                        HTTPStatus.OK,
                        game.v2_get_page(agent_id, "state", parsed.query),
                    )
                    return
                if suffix == ["legal-actions"]:
                    self._json(
                        HTTPStatus.OK,
                        game.v2_get_page(
                            agent_id, "legal_actions", parsed.query,
                        ),
                    )
                    return
                if suffix and suffix[0] == "receipts":
                    if (
                        len(suffix) != 2
                        or route_path.endswith("/")
                    ):
                        raise game._v2_problem(
                            HTTPStatus.NOT_FOUND,
                            "invalid_request",
                            "the command receipt was not found",
                            retryable=False,
                        )
                    if parsed.query:
                        raise game._v2_problem(
                            HTTPStatus.BAD_REQUEST,
                            "invalid_request",
                            "the full-control-v2 request is invalid",
                            retryable=False,
                        )
                    status, receipt = game.v2_get_receipt(
                        agent_id, suffix[1],
                    )
                    self._json(status, receipt)
                    return
            if len(parts) >= 3 and parts[:2] == ["v1", "games"]:
                game = self.server.supervisor.game(parts[2])
                if len(parts) == 3:
                    self._json(HTTPStatus.OK, game.status())
                    return
                suffix = parts[3:]
                if suffix == ["status"]:
                    self._json(HTTPStatus.OK, game.status())
                    return
                if suffix == ["phase-events"] and not route_path.endswith("/"):
                    if game.config["control_protocol"] != FULL_CONTROL_V2:
                        raise APIProblem(HTTPStatus.NOT_FOUND, "not found")
                    query = parse_qs(parsed.query, keep_blank_values=True)
                    if set(query) - {"after_sequence", "limit"} or any(
                        len(values) != 1 for values in query.values()
                    ):
                        raise APIProblem(
                            HTTPStatus.BAD_REQUEST,
                            "phase-events query accepts one after_sequence "
                            "and one limit",
                        )
                    after_text = query.get("after_sequence", ["0"])[0]
                    limit_text = query.get("limit", ["100"])[0]
                    integer = re.compile(r"(?:0|[1-9][0-9]{0,18})")
                    if (
                        integer.fullmatch(after_text) is None
                        or integer.fullmatch(limit_text) is None
                    ):
                        raise APIProblem(
                            HTTPStatus.BAD_REQUEST,
                            "after_sequence and limit must be canonical integers",
                        )
                    after_sequence = int(after_text)
                    limit = int(limit_text)
                    if (
                        after_sequence > (1 << 63) - 1
                        or not 1 <= limit <= 250
                    ):
                        raise APIProblem(
                            HTTPStatus.BAD_REQUEST,
                            "after_sequence must be in [0, 2^63-1] and limit "
                            "must be in [1, 250]",
                        )
                    self._json(
                        HTTPStatus.OK,
                        game.phase_events(after_sequence, limit),
                    )
                    return
                if suffix == ["result"]:
                    self._json(HTTPStatus.OK, game.result())
                    return
                if suffix == ["native-viewer"]:
                    game.authorize_owner(self._bearer())
                    query = parse_qs(parsed.query)
                    if set(query) != {"lease_id"} or len(query["lease_id"]) != 1:
                        raise APIProblem(
                            HTTPStatus.BAD_REQUEST,
                            "native viewer status requires only lease_id",
                        )
                    self._json(
                        HTTPStatus.OK,
                        game.native_viewer_status(query["lease_id"][0]),
                    )
                    return
                if suffix == ["watch.json"]:
                    self._json(HTTPStatus.OK, game.watch_state())
                    return
                if suffix == ["replay.json"]:
                    query = parse_qs(parsed.query, keep_blank_values=True)
                    if set(query) - {"after_turn", "limit"} or any(
                        len(values) != 1 for values in query.values()
                    ):
                        raise APIProblem(
                            HTTPStatus.BAD_REQUEST,
                            "replay query accepts one after_turn and one limit",
                        )
                    try:
                        after_turn = int(query.get("after_turn", ["0"])[0])
                        limit = int(query.get("limit", ["250"])[0])
                    except ValueError as exc:
                        raise APIProblem(
                            HTTPStatus.BAD_REQUEST,
                            "after_turn and limit must be integers",
                        ) from exc
                    if after_turn < 0 or not 1 <= limit <= 250:
                        raise APIProblem(
                            HTTPStatus.BAD_REQUEST,
                            "after_turn must be >= 0 and limit must be in [1, 250]",
                        )
                    self._json(
                        HTTPStatus.OK, game.replay_state(after_turn, limit),
                    )
                    return
                if suffix == ["frames"]:
                    self._json(HTTPStatus.OK, game.frame_manifest())
                    return
                if suffix == ["frames", "latest.png"]:
                    self._file(game.png_frame(None), "image/png")
                    return
                if (
                    len(suffix) == 2 and suffix[0] == "frames"
                    and suffix[1].endswith(".png")
                ):
                    try:
                        index = int(suffix[1][:-4])
                    except ValueError as exc:
                        raise APIProblem(
                            HTTPStatus.NOT_FOUND, "frame not found",
                        ) from exc
                    self._file(game.png_frame(index), "image/png")
                    return
                if suffix == ["video.mp4"]:
                    self._file(game.video(), "video/mp4")
                    return
                if suffix == ["me", "next"]:
                    agent_id, _ = game.authenticate_agent(self._bearer())
                    query = parse_qs(parsed.query)
                    try:
                        after_turn = int(query.get("after_turn", ["0"])[0])
                        wait_s = float(query.get("wait_s", ["30"])[0])
                    except ValueError as exc:
                        raise APIProblem(
                            HTTPStatus.BAD_REQUEST,
                            "after_turn and wait_s must be numeric",
                        ) from exc
                    if after_turn < 0 or not math.isfinite(wait_s) or not 0 <= wait_s <= 300:
                        raise APIProblem(
                            HTTPStatus.BAD_REQUEST,
                            "after_turn must be >= 0 and wait_s must be in [0, 300]",
                        )
                    self._json(
                        HTTPStatus.OK,
                        game.next_for_agent(agent_id, after_turn, wait_s),
                    )
                    return
            raise APIProblem(HTTPStatus.NOT_FOUND, "not found")
        except APIProblem as exc:
            self._json(exc.status, exc.payload or {"error": str(exc)})
        except Exception as exc:
            self._json(
                HTTPStatus.INTERNAL_SERVER_ERROR, {"error": str(exc)},
            )

    def do_POST(self) -> None:
        try:
            parsed = urlparse(self.path)
            route_path = self._route_path(parsed.path)
            parts = route_path.strip("/").split("/")
            if parts == ["v1", "games"]:
                self.server.supervisor.authorize_admin(self._bearer())
                result = self.server.supervisor.create_game(self._body())
                self._json(HTTPStatus.CREATED, result)
                return
            if (
                len(parts) == 5
                and parts[:3] == ["internal", "v1", "games"]
                and parts[4:] == ["turns"]
            ):
                game = self.server.supervisor.game(parts[3])
                if game.config["control_protocol"] != STRATEGIC_V1:
                    raise APIProblem(
                        HTTPStatus.CONFLICT,
                        "the internal strategic-v1 bridge is unavailable for "
                        "full-control-v2 games",
                    )
                game.authorize_internal(self._bearer())
                body = self._body()
                turn, _year, _observations = game._validate_turn(body)
                generation = game.native_viewer_turn_response_started()
                game.native_viewer_turn_response_identified(generation, turn)
                try:
                    result = game.process_turn(body)
                    self._json(HTTPStatus.OK, result)
                finally:
                    game.native_viewer_turn_response_sent()
                return
            if (
                len(parts) == 5
                and parts[:2] == ["v2", "games"]
                and parts[3:] == ["me", "batches"]
            ):
                game = self.server.supervisor.game(parts[2])
                if game.config["control_protocol"] != FULL_CONTROL_V2:
                    raise APIProblem(
                        HTTPStatus.CONFLICT,
                        "full-control-v2 routes are unavailable for this game",
                        structured_error(
                            "unsupported_protocol",
                            "this game uses strategic-v1",
                            retryable=False,
                        ),
                )
                agent_id, _ = self._authenticate_v2(game)
                if parsed.query or route_path.endswith("/"):
                    raise game._v2_problem(
                        HTTPStatus.BAD_REQUEST,
                        "invalid_batch",
                        "the full-control-v2 command batch is invalid",
                        retryable=False,
                    )
                status, receipt = game.v2_submit_batch(
                    agent_id, self._v2_batch_body(game),
                )
                self._json(status, receipt)
                return
            if len(parts) >= 4 and parts[:2] == ["v1", "games"]:
                game = self.server.supervisor.game(parts[2])
                suffix = parts[3:]
                if suffix == ["join"]:
                    body = self._body()
                    if not isinstance(body, dict):
                        raise APIProblem(
                            HTTPStatus.BAD_REQUEST,
                            "join request must be an object",
                        )
                    unknown = set(body) - {
                        "join_token", "place", "seat_id",
                        "controller_label", "name", "metadata",
                        "supported_control_protocols",
                    }
                    if unknown:
                        raise APIProblem(
                            HTTPStatus.BAD_REQUEST,
                            f"join request has unknown fields: {sorted(unknown)}",
                        )
                    label = body.get("controller_label", body.get("name"))
                    if (
                        "controller_label" in body and "name" in body
                        and body["controller_label"] != body["name"]
                    ):
                        raise APIProblem(
                            HTTPStatus.BAD_REQUEST,
                            "name and controller_label must match when both are provided",
                        )
                    token = self._bearer() or body.get("join_token")
                    selected = body.get("place", body.get("seat_id"))
                    self._json(
                        HTTPStatus.OK,
                        game.join(
                            token, selected, label, body.get("metadata"),
                            body.get("supported_control_protocols"),
                        ),
                    )
                    return
                if suffix == ["me", "actions"]:
                    agent_id, _ = game.authenticate_agent(self._bearer())
                    status, result = game.submit_action(
                        agent_id, self._body(),
                    )
                    self._json(status, result)
                    return
                if suffix == ["cancel"]:
                    game.authorize_owner(self._bearer())
                    self._json(HTTPStatus.ACCEPTED, game.cancel())
                    return
                if suffix == ["native-viewer"]:
                    game.authorize_owner(self._bearer())
                    self._json(
                        HTTPStatus.CREATED, game.request_native_viewer(),
                    )
                    return
                if suffix == ["native-viewer", "release"]:
                    game.authorize_owner(self._bearer())
                    body = self._body()
                    if not isinstance(body, dict):
                        raise APIProblem(
                            HTTPStatus.BAD_REQUEST,
                            "native viewer release must be an object",
                        )
                    if set(body) != {"lease_id"}:
                        raise APIProblem(
                            HTTPStatus.BAD_REQUEST,
                            "native viewer release requires only lease_id",
                        )
                    self._json(
                        HTTPStatus.OK,
                        game.release_native_viewer(body["lease_id"]),
                    )
                    return
            raise APIProblem(HTTPStatus.NOT_FOUND, "not found")
        except APIProblem as exc:
            self._json(exc.status, exc.payload or {"error": str(exc)})
        except Exception as exc:
            self._json(
                HTTPStatus.INTERNAL_SERVER_ERROR, {"error": str(exc)},
            )


def make_supervisor_server(
    supervisor: Supervisor, host: str = "127.0.0.1", port: int = 8765,
    public_url: str | None = None,
) -> SupervisorHTTPServer:
    return SupervisorHTTPServer((host, port), supervisor, public_url)
