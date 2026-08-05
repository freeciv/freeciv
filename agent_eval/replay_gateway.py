"""Local read-only gateway for the browser replay viewer.

The gateway deliberately exposes only public spectator GET routes.  It keeps
the Vite proxy pointed at one stable local target while an older supervisor is
augmented with replay telemetry reconstructed from its on-disk autosaves.
"""

from __future__ import annotations

import argparse
import fcntl
import hashlib
import http.client
import ipaddress
import json
import math
import os
import re
import secrets
import socket
import stat
import sys
import threading
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Callable, Mapping


REPO_ROOT = Path(__file__).resolve().parent.parent
GAME_ID_RE = re.compile(r"^[A-Za-z0-9_-]{20,80}$")
FRAME_INDEX_RE = re.compile(r"^(?:0|[1-9][0-9]*)\.png$")
ARCHIVE_PNG_RE = re.compile(r"^([0-9]{6})\.png$")
ARCHIVE_PPM_RE = re.compile(r"^turn-(\d+)-.*\.map\.ppm$")
PPM_PLAYER_RE = re.compile(
    r'^#\s*playerno:(\d+):color:\(\s*(\d+),\s*(\d+),\s*(\d+)\)'
    r':name:"(.*)"\s*$'
)
TERMINAL_STATES = {"completed", "invalid", "failed", "cancelled"}
GATEWAY_KIND = "freeciv-replay-gateway"
GATEWAY_PROTOCOL_VERSION = 1
MAX_PROXY_ERROR_BYTES = 64 * 1024
MAX_PROXY_JSON_BYTES = 8 * 1024 * 1024
PROXY_RESPONSE_HEADERS = (
    "Content-Type",
    "Content-Length",
    "Cache-Control",
    "ETag",
    "Last-Modified",
)
PUBLIC_SCORE_METRICS = {
    "bnp", "cities", "contentpop", "corruption", "culture", "gold",
    "gov", "happypop", "landarea", "literacy", "luxrate", "mfg",
    "munits", "pollution", "pop", "riots", "scirate", "score",
    "settledarea", "settlers", "spaceship", "specialists", "taxrate",
    "techout", "techs", "unhappypop", "unitsbuilt", "unitskilled",
    "unitslost", "unitsused", "wonders",
}

ReplayLoader = Callable[..., Mapping[str, Any]]
BoardLoader = Callable[..., Mapping[str, Any]]


class GatewayProblem(RuntimeError):
    """A public, intentionally non-sensitive gateway error."""

    def __init__(self, status: int, message: str):
        super().__init__(message)
        self.status = int(status)


class UpstreamUnavailable(GatewayProblem):
    """The configured service could not be reached at all."""


@dataclass(frozen=True)
class TerminalArchive:
    game_id: str
    run_root: Path
    manifest: dict[str, Any]
    report: dict[str, Any]
    state: str
    benchmark_valid: bool
    places: list[dict[str, Any]]
    leaderboard: list[dict[str, Any]]
    outcome: dict[str, Any]


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    """Keep the fixed upstream target from escaping through redirects."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


_NO_REDIRECT_OPENER = urllib.request.build_opener(_NoRedirect)


@dataclass(frozen=True)
class GatewayConfig:
    repo_root: Path
    upstream_service_url: str
    runs_root: Path
    cache_root: Path
    identity: str
    upstream_timeout_s: float
    viewer_public_url: str | None = None


def _canonical(value: Any) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False,
    ).encode("utf-8")


def _normalize_service_url(value: str) -> str:
    """Return one credential-free HTTP(S) origin plus optional path prefix."""
    try:
        parsed = urllib.parse.urlsplit(value.strip())
        port = parsed.port
    except ValueError as exc:
        raise ValueError("service URL has an invalid port") from exc
    scheme = parsed.scheme.lower()
    if scheme not in {"http", "https"} or not parsed.hostname:
        raise ValueError("service URL must be an http(s) URL")
    if parsed.username or parsed.password or parsed.query or parsed.fragment:
        raise ValueError(
            "service URL cannot contain credentials, query, or fragment"
        )
    if any(ord(character) < 0x20 for character in parsed.path):
        raise ValueError("service URL path contains control characters")
    segments = parsed.path.split("/")
    if any(segment in {".", ".."} for segment in segments):
        raise ValueError("service URL path cannot contain dot segments")
    hostname = parsed.hostname.lower()
    rendered_host = f"[{hostname}]" if ":" in hostname else hostname
    if port is not None and not (
        scheme == "http" and port == 80
        or scheme == "https" and port == 443
    ):
        rendered_host += f":{port}"
    return urllib.parse.urlunsplit((
        scheme,
        rendered_host,
        parsed.path.rstrip("/"),
        "",
        "",
    ))


def _loopback_host(value: str) -> str:
    """Require a literal loopback address so the gateway is never public."""
    try:
        address = ipaddress.ip_address(value)
    except ValueError as exc:
        raise ValueError("gateway host must be a literal loopback address") from exc
    if not address.is_loopback:
        raise ValueError("gateway host must be a loopback address")
    return address.compressed


def _identity(
    repo_root: Path, service_url: str, runs_root: Path, cache_root: Path,
    viewer_public_url: str | None = None,
) -> str:
    material = "\0".join((
        str(repo_root), service_url, str(runs_root), str(cache_root),
        viewer_public_url or "",
    )).encode("utf-8")
    return hashlib.sha256(material).hexdigest()[:20]


def gateway_config(
    service_url: str,
    runs_root: str | Path,
    cache_root: str | Path,
    *,
    repo_root: str | Path = REPO_ROOT,
    upstream_timeout_s: float = 10,
    viewer_public_url: str | None = None,
) -> GatewayConfig:
    if (
        isinstance(upstream_timeout_s, bool)
        or not isinstance(upstream_timeout_s, (int, float))
        or not math.isfinite(upstream_timeout_s)
        or upstream_timeout_s <= 0
    ):
        raise ValueError("upstream timeout must be greater than zero")
    checkout = Path(repo_root).expanduser().resolve()
    runs = Path(runs_root).expanduser().resolve()
    cache = Path(cache_root).expanduser().resolve()
    upstream = _normalize_service_url(service_url)
    viewer = (
        _normalize_service_url(viewer_public_url)
        if viewer_public_url is not None else None
    )
    return GatewayConfig(
        repo_root=checkout,
        upstream_service_url=upstream,
        runs_root=runs,
        cache_root=cache,
        identity=_identity(checkout, upstream, runs, cache, viewer),
        upstream_timeout_s=float(upstream_timeout_s),
        viewer_public_url=viewer,
    )


def _write_private_json(path: str | Path, value: Mapping[str, Any]) -> Path:
    destination = Path(path).expanduser().resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(
        f".{destination.name}.{secrets.token_hex(6)}.tmp"
    )
    descriptor = os.open(
        temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600,
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, destination)
        os.chmod(destination, 0o600)
    finally:
        temporary.unlink(missing_ok=True)
    return destination


def _acquire_ready_lock(path: Path) -> int:
    """Serialize publishers so cleanup cannot unlink a replacement record."""
    lock_path = path.with_name(path.name + ".lock")
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    descriptor = os.open(lock_path, os.O_RDWR | os.O_CREAT, 0o600)
    try:
        os.chmod(lock_path, 0o600)
        fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BaseException:
        os.close(descriptor)
        raise
    return descriptor


def _release_ready_lock(descriptor: int) -> None:
    try:
        fcntl.flock(descriptor, fcntl.LOCK_UN)
    finally:
        os.close(descriptor)


def _default_replay_loader(
    runs_root: Path,
    game_id: str,
    resolved_places: list[dict[str, Any]],
    *,
    after_turn: int,
    limit: int,
    cache_root: Path,
    complete: bool,
) -> Mapping[str, Any]:
    # Keep this import late: the gateway remains usable against a modern
    # supervisor even when optional legacy-save reconstruction is unavailable.
    from .save_replay import replay_from_autosaves

    return replay_from_autosaves(
        runs_root,
        game_id,
        resolved_places,
        after_turn=after_turn,
        limit=limit,
        cache_root=cache_root,
        complete=complete,
    )


def _default_board_loader(
    runs_root: Path,
    game_id: str,
    resolved_places: list[dict[str, Any]],
    *,
    turn: int,
    cache_root: Path,
) -> Mapping[str, Any]:
    from .save_replay import board_from_autosave

    return board_from_autosave(
        runs_root,
        game_id,
        resolved_places,
        turn=turn,
        cache_root=cache_root,
    )


def _public_text(value: Any, fallback: str, limit: int = 160) -> str:
    if not isinstance(value, str):
        return fallback
    rendered = "".join(
        character for character in value
        if character in "\t " or ord(character) >= 0x20
    ).strip()
    return rendered[:limit] or fallback


def _public_int(value: Any, default: int = 0, minimum: int = 0) -> int:
    if (
        isinstance(value, bool)
        or not isinstance(value, (int, float))
        or not math.isfinite(value)
        or int(value) != value
    ):
        return default
    return max(minimum, int(value))


def _public_number(value: Any, default: float = 0.0) -> float:
    if (
        isinstance(value, bool)
        or not isinstance(value, (int, float))
        or not math.isfinite(value)
    ):
        return default
    return float(value)


def _public_timing(config: Mapping[str, Any]) -> dict[str, Any]:
    """Preserve explicit timing only; never relabel legacy archives."""
    mode = config.get("timing_mode")
    timeout = config.get("action_timeout_s")
    if mode not in {"default", "blitz", "infinite", "custom"}:
        return {}
    if mode == "infinite":
        return (
            {"timing_mode": mode, "action_timeout_s": None}
            if timeout is None else {}
        )
    if (
        isinstance(timeout, (int, float)) and not isinstance(timeout, bool)
        and math.isfinite(timeout) and timeout > 0
    ):
        normalized = float(timeout)
        if (
            mode == "default" and normalized != 180.0
            or mode == "blitz" and normalized != 60.0
        ):
            return {}
        return {"timing_mode": mode, "action_timeout_s": normalized}
    return {}


def _public_places(value: Any) -> list[dict[str, Any]]:
    if not isinstance(value, list):
        return []
    places: list[dict[str, Any]] = []
    for raw in value:
        if not isinstance(raw, dict):
            continue
        place = _public_int(raw.get("place"), -1, -1)
        seat_id = _public_text(raw.get("seat_id"), "", 80)
        player_name = _public_text(raw.get("player_name"), "", 80)
        color = _public_text(raw.get("player_color"), "", 16)
        if place < 1 or not seat_id or not player_name:
            continue
        controller = _public_text(raw.get("controller"), "agent", 32)
        joined = raw.get("joined") is True
        result: dict[str, Any] = {
            "place": place,
            "seat_id": seat_id,
            "player_name": player_name,
            "player_color": color,
            "controller": controller,
            "joined": joined,
        }
        label = raw.get("controller_label")
        controller_type = raw.get("controller_type")
        model = raw.get("model")
        if controller == "native_classic_ai":
            label = label or "Freeciv Classic AI"
            controller_type = controller_type or "native"
            model = model or "classic"
        elif joined:
            controller_type = controller_type or "external"
            metadata = raw.get("controller_metadata")
            if (
                model is None and isinstance(metadata, dict)
                and isinstance(metadata.get("model"), str)
            ):
                model = metadata["model"]
        if isinstance(label, str) and label.strip():
            result["controller_label"] = _public_text(label, "", 80)
        if isinstance(controller_type, str) and controller_type.strip():
            result["controller_type"] = _public_text(
                controller_type, "", 32,
            )
        if isinstance(model, str) and model.strip():
            result["model"] = _public_text(model, "", 80)
        places.append(result)
    places.sort(key=lambda item: (item["place"], item["seat_id"]))
    return places


def _read_manifest(runs_root: Path, game_id: str) -> dict[str, Any]:
    if not GAME_ID_RE.fullmatch(game_id):
        raise GatewayProblem(HTTPStatus.NOT_FOUND, "game not found")
    candidate = runs_root / game_id
    if candidate.is_symlink():
        raise GatewayProblem(HTTPStatus.NOT_FOUND, "game not found")
    run_root = candidate.resolve()
    if run_root.parent != runs_root or not run_root.is_dir():
        raise GatewayProblem(HTTPStatus.NOT_FOUND, "game not found")
    manifest_path = run_root / "manifest.json"
    value = _read_archive_json(manifest_path, "game manifest")
    if not isinstance(value, dict) or value.get("game_id") != game_id:
        raise GatewayProblem(HTTPStatus.NOT_FOUND, "game not found")
    return value


def _read_archive_json(path: Path, label: str) -> dict[str, Any]:
    flags = os.O_RDONLY
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        descriptor = os.open(path, flags)
    except OSError as exc:
        raise GatewayProblem(HTTPStatus.NOT_FOUND, f"{label} not found") from exc
    try:
        info = os.fstat(descriptor)
        if not stat.S_ISREG(info.st_mode) or not 0 < info.st_size <= MAX_PROXY_JSON_BYTES:
            raise GatewayProblem(
                HTTPStatus.SERVICE_UNAVAILABLE, f"{label} is unavailable",
            )
        with os.fdopen(descriptor, "r", encoding="utf-8", closefd=False) as stream:
            value = json.load(stream)
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise GatewayProblem(
            HTTPStatus.SERVICE_UNAVAILABLE, f"{label} is unavailable",
        ) from exc
    finally:
        os.close(descriptor)
    if not isinstance(value, dict):
        raise GatewayProblem(
            HTTPStatus.SERVICE_UNAVAILABLE, f"{label} is unavailable",
        )
    return value


def _archive_reasons(value: Any) -> list[str]:
    if not isinstance(value, list):
        return []
    reasons: list[str] = []
    for raw in value[:100]:
        reason = _public_text(raw, "", 240)
        lowered = reason.lower()
        if not reason:
            continue
        if (
            "/" in reason or "\\" in reason
            or any(word in lowered for word in (
                "token", "password", "credential", "secret",
            ))
        ):
            reason = "Archived evaluation was marked invalid."
        if reason not in reasons:
            reasons.append(reason)
    return reasons


def _archive_leaderboard(
    report: Mapping[str, Any], places: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    score = report.get("score")
    raw_players = score.get("players") if isinstance(score, dict) else None
    if not isinstance(raw_players, list):
        return []
    final_turn = _public_int(score.get("final_turn"))
    by_seat = {place["seat_id"]: place for place in places}
    by_name = {place["player_name"]: place for place in places}
    rows: list[dict[str, Any]] = []
    for raw in raw_players:
        if not isinstance(raw, dict):
            continue
        seat_id = _public_text(raw.get("seat_id"), "", 80)
        player_name = _public_text(raw.get("name"), "", 80)
        place = by_seat.get(seat_id) or by_name.get(player_name)
        if place is None:
            continue
        rows.append({
            "score": _public_int(raw.get("score")),
            "score_turn": final_turn or None,
            "place": place["place"],
            "seat_id": place["seat_id"],
            "player_name": place["player_name"],
            "player_color": place.get("player_color", ""),
            "controller_label": place.get(
                "controller_label", "Unclaimed agent place",
            ),
            "controller_type": place.get("controller_type", "external"),
            "model": place.get("model"),
        })
    rows.sort(key=lambda row: (-row["score"], row["place"], row["seat_id"]))
    previous_score: int | None = None
    rank = 0
    for index, row in enumerate(rows, 1):
        if row["score"] != previous_score:
            rank = index
            previous_score = row["score"]
        row["rank"] = rank
    return rows


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


def _archive_victory(run_root: Path | None) -> dict[str, Any] | None:
    """Read the engine's game-over record from a terminal archive."""
    if run_root is None:
        return None
    try:
        record = json.loads(
            (run_root / "victory.json").read_text(encoding="utf-8")
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
        "label": VICTORY_LABELS.get(code, code),
        "winners": (
            [name for name in winners if isinstance(name, str)]
            if isinstance(winners, list) else []
        ),
        "turn": record.get("turn"),
        "year": record.get("year"),
    }


def _archive_outcome(
    state: str, leaderboard: list[dict[str, Any]],
    run_root: Path | None = None,
) -> dict[str, Any]:
    outcome = _archive_score_outcome(state, leaderboard)
    victory = _archive_victory(run_root)
    outcome["victory"] = victory
    if victory is not None and outcome["status"] in {"won", "tied"}:
        outcome["summary"] = f"{outcome['summary']} ({victory['label']})"
    return outcome


def _archive_score_outcome(
    state: str, leaderboard: list[dict[str, Any]],
) -> dict[str, Any]:
    if not leaderboard:
        return {
            "status": "invalid",
            "summary": "No valid winner; no complete score snapshot is available",
            "leaders": [],
            "margin": None,
            "score_turn": None,
        }
    best_rank = leaderboard[0]["rank"]
    leaders = [row for row in leaderboard if row["rank"] == best_rank]
    labels = [row["controller_label"] for row in leaders]
    score_turn = leaderboard[0].get("score_turn")
    if len(leaders) > 1:
        return {
            "status": "tied" if state == "completed" else "invalid",
            "summary": (
                f"{' and '.join(labels)} finished tied"
                if state == "completed" else
                "No valid winner; "
                f"{' and '.join(labels)} were tied at the last complete score"
            ),
            "leaders": labels,
            "margin": 0,
            "score_turn": score_turn,
        }
    others = [row for row in leaderboard if row is not leaders[0]]
    margin = (
        leaders[0]["score"] - max(row["score"] for row in others)
        if others else None
    )
    suffix = f" by {margin}" if margin is not None else ""
    if state == "completed":
        summary = f"{leaders[0]['controller_label']} won{suffix}"
        status = "won"
    else:
        summary = (
            f"No valid winner; {leaders[0]['controller_label']} led{suffix} "
            "at the last complete score"
        )
        status = "invalid"
    return {
        "status": status,
        "summary": summary,
        "leaders": labels,
        "margin": margin,
        "score_turn": score_turn,
    }


def _terminal_archive(runs_root: Path, game_id: str) -> TerminalArchive:
    manifest = _read_manifest(runs_root, game_id)
    state = _public_text(
        manifest.get("state", manifest.get("status")), "unknown", 32,
    )
    if state not in TERMINAL_STATES:
        raise GatewayProblem(HTTPStatus.NOT_FOUND, "terminal archive not found")
    run_root = (runs_root / game_id).resolve()
    report = _read_archive_json(run_root / "report.json", "game report")
    report_manifest = report.get("manifest")
    if (
        not isinstance(report_manifest, dict)
        or report_manifest.get("game_id") != game_id
    ):
        raise GatewayProblem(HTTPStatus.NOT_FOUND, "terminal archive not found")
    places = _public_places(manifest.get("resolved_places"))
    leaderboard = _archive_leaderboard(report, places)
    benchmark_valid = state == "completed" and manifest.get(
        "benchmark_valid",
    ) is True
    return TerminalArchive(
        game_id=game_id,
        run_root=run_root,
        manifest=manifest,
        report=report,
        state=state,
        benchmark_valid=benchmark_valid,
        places=places,
        leaderboard=leaderboard,
        outcome=_archive_outcome(
            "completed" if benchmark_valid else "invalid", leaderboard,
            run_root,
        ),
    )


def _archive_urls(
    base: str, game_id: str, *, absolute_watch: bool = False,
) -> dict[str, str]:
    game = f"{base}/v1/games/{game_id}"
    return {
        "join_url": f"{game}/join",
        "status_url": f"{game}/status",
        "result_url": f"{game}/result",
        "watch_url": (
            f"{base}/watch/{game_id}" if absolute_watch else f"/watch/{game_id}"
        ),
        "watch_json_url": f"{game}/watch.json",
        "replay_url": f"{game}/replay.json",
        "frames_url": f"{game}/frames",
        "video_url": f"{game}/video.mp4",
    }


def _archive_status(
    archive: TerminalArchive, base: str, *, absolute_watch: bool = False,
) -> dict[str, Any]:
    manifest = archive.manifest
    config = manifest.get("config")
    if not isinstance(config, dict):
        config = {}
    state = archive.state
    return {
        "schema_version": 1,
        "game_id": archive.game_id,
        "state": state,
        "benchmark_valid": archive.benchmark_valid,
        "mode": _public_text(config.get("mode"), "unknown", 32),
        "places": _public_int(config.get("places")),
        "max_agents": _public_int(config.get("max_agents")),
        "joined_agents": _public_int(manifest.get("joined_agents")),
        "turns": _public_int(config.get("turns")),
        "current_turn": (
            _public_int(manifest.get("current_turn"))
            if manifest.get("current_turn") is not None else None
        ),
        "objective": _public_text(
            config.get("objective"),
            "Maximize final Freeciv civilization score.",
            240,
        ),
        **_public_timing(config),
        "error": (
            "Archived game ended with an error."
            if manifest.get("error") else None
        ),
        "invalid_reasons": _archive_reasons(manifest.get("invalid_reasons")),
        "resolved_places": archive.places,
        "leaderboard": archive.leaderboard,
        "outcome": archive.outcome,
        **_archive_urls(base, archive.game_id, absolute_watch=absolute_watch),
    }


def _safe_archive_directory(root: Path, name: str) -> Path:
    path = root / name
    try:
        info = path.lstat()
    except OSError as exc:
        raise GatewayProblem(HTTPStatus.NOT_FOUND, "archive data not found") from exc
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode):
        raise GatewayProblem(HTTPStatus.NOT_FOUND, "archive data not found")
    resolved = path.resolve()
    if resolved.parent != root:
        raise GatewayProblem(HTTPStatus.NOT_FOUND, "archive data not found")
    return resolved


def _archive_regular_files(
    directory: Path, pattern: re.Pattern[str],
) -> list[tuple[re.Match[str], Path]]:
    try:
        candidates = tuple(directory.iterdir())
    except OSError:
        return []
    values: list[tuple[re.Match[str], Path]] = []
    for path in candidates:
        match = pattern.fullmatch(path.name)
        if match is None:
            continue
        try:
            info = path.lstat()
        except OSError:
            continue
        if stat.S_ISREG(info.st_mode) and not stat.S_ISLNK(info.st_mode) and info.st_size:
            values.append((match, path))
    return values


def _archive_ppm_players(
    path: Path, places: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    identities = {place["player_name"]: place for place in places}
    parsed: list[dict[str, Any]] = []
    try:
        info = path.lstat()
        if not stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode):
            return []
        with path.open("r", encoding="utf-8", errors="replace") as stream:
            for line_number, line in enumerate(stream):
                if line_number > 512:
                    break
                if line_number > 0 and not line.startswith("#"):
                    if parsed or line.strip() and line.strip() != "P3":
                        break
                    continue
                match = PPM_PLAYER_RE.fullmatch(line.rstrip("\r\n"))
                if match is None:
                    continue
                red, green, blue = (
                    int(match.group(index)) for index in (2, 3, 4)
                )
                if any(not 0 <= value <= 255 for value in (red, green, blue)):
                    continue
                player_id = int(match.group(1))
                player_name = _public_text(
                    match.group(5).replace(r'\"', '"').replace(r"\\", "\\"),
                    "",
                    80,
                )
                if not player_name:
                    continue
                place = identities.get(player_name)
                row: dict[str, Any] = {
                    "player_id": player_id,
                    "player_name": player_name,
                    "player_color": f"#{red:02X}{green:02X}{blue:02X}",
                }
                if place is None:
                    row.update({
                        "seat_id": f"dynamic-player-{player_id}",
                        "place": None,
                        "controller_label": "Freeciv dynamic faction",
                        "controller_type": "dynamic",
                        "scored": False,
                    })
                else:
                    row.update({
                        key: place.get(key) for key in (
                            "seat_id", "place", "controller_label",
                            "controller_type", "model",
                        )
                    })
                    row["scored"] = True
                parsed.append(row)
    except OSError:
        return []
    parsed.sort(key=lambda row: (row["player_id"], row["player_name"]))
    return parsed


def _archive_frames(archive: TerminalArchive, base: str) -> dict[str, Any]:
    png_directory = _safe_archive_directory(archive.run_root, "watch_frames")
    ppm_directory = _safe_archive_directory(archive.run_root, "saves")
    pngs = {
        int(match.group(1)): path
        for match, path in _archive_regular_files(png_directory, ARCHIVE_PNG_RE)
    }
    ppms = sorted(
        _archive_regular_files(ppm_directory, ARCHIVE_PPM_RE),
        key=lambda value: value[1].name,
    )
    frames: list[dict[str, Any]] = []
    for index in sorted(pngs):
        source = ppms[index][1] if 0 <= index < len(ppms) else None
        turn = int(ppms[index][0].group(1)) if source is not None else None
        frames.append({
            "index": index,
            "turn": turn,
            "map_players": (
                _archive_ppm_players(source, archive.places)
                if source is not None else []
            ),
            "source_name": source.name if source is not None else pngs[index].name,
            "png_url": (
                f"{base}/v1/games/{archive.game_id}/frames/{index}.png"
            ),
        })
    return {
        "schema_version": 1,
        "game_id": archive.game_id,
        "label": "Omniscient strategic map snapshots (not GUI video)",
        "frames": frames,
        "latest_png_url": (
            f"{base}/v1/games/{archive.game_id}/frames/latest.png"
            if frames else None
        ),
    }


def _archive_frame_path(archive: TerminalArchive, index: int | None) -> Path:
    directory = _safe_archive_directory(archive.run_root, "watch_frames")
    values = {
        int(match.group(1)): path
        for match, path in _archive_regular_files(directory, ARCHIVE_PNG_RE)
    }
    if not values:
        raise GatewayProblem(HTTPStatus.NOT_FOUND, "no map frames are available")
    resolved = max(values) if index is None else index
    try:
        return values[resolved]
    except KeyError as exc:
        raise GatewayProblem(HTTPStatus.NOT_FOUND, "map frame does not exist") from exc


def _archive_video_path(archive: TerminalArchive) -> Path:
    path = archive.run_root / "game.mp4"
    try:
        info = path.lstat()
    except OSError as exc:
        raise GatewayProblem(HTTPStatus.NOT_FOUND, "replay video not found") from exc
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode) or not info.st_size:
        raise GatewayProblem(HTTPStatus.NOT_FOUND, "replay video not found")
    return path


def _archive_watch(
    archive: TerminalArchive, base: str, *, absolute_watch: bool = False,
) -> dict[str, Any]:
    status = _archive_status(archive, base, absolute_watch=absolute_watch)
    frame_manifest = _archive_frames(archive, base)
    timeline = [
        {"turn": frame["turn"]}
        for frame in frame_manifest["frames"]
        if frame["turn"] is not None
    ]
    try:
        _archive_video_path(archive)
        video_available = True
    except GatewayProblem:
        video_available = False
    return {
        "schema_version": 1,
        "label": "Omniscient Freeciv agent match replay",
        "game": status,
        "timeline": timeline,
        "frames": frame_manifest["frames"],
        "replay": {
            "available": True,
            "url": _archive_urls(base, archive.game_id)["replay_url"],
        },
        "video": {
            "available": video_available,
            "url": _archive_urls(base, archive.game_id)["video_url"],
            "kind": "archived-video",
        },
    }


def _archive_result(
    archive: TerminalArchive, base: str, *, absolute_watch: bool = False,
) -> dict[str, Any]:
    config = archive.manifest.get("config")
    if not isinstance(config, dict):
        config = {}
    raw_score = archive.report.get("score")
    raw_players = raw_score.get("players") if isinstance(raw_score, dict) else []
    players: list[dict[str, Any]] = []
    if isinstance(raw_players, list):
        for raw in raw_players:
            if not isinstance(raw, dict):
                continue
            metrics = raw.get("metrics")
            public_metrics = {
                key: _public_int(value)
                for key, value in metrics.items()
                if key in PUBLIC_SCORE_METRICS
            } if isinstance(metrics, dict) else {}
            players.append({
                "seat_id": _public_text(raw.get("seat_id"), "", 80),
                "player_id": _public_int(raw.get("player_id")),
                "name": _public_text(raw.get("name"), "", 80),
                "rank": _public_int(raw.get("rank")),
                "score": _public_int(raw.get("score")),
                "metrics": public_metrics,
            })
    state = archive.state
    urls = _archive_urls(base, archive.game_id, absolute_watch=absolute_watch)
    return {
        "schema_version": 1,
        "artifact_id": archive.game_id,
        "state": state,
        "benchmark_valid": archive.benchmark_valid,
        **_public_timing(config),
        "invalid_reasons": _archive_reasons(
            archive.manifest.get("invalid_reasons"),
        ),
        "leaderboard": archive.leaderboard,
        "outcome": archive.outcome,
        "score": {
            "final_turn": (
                _public_int(raw_score.get("final_turn"))
                if isinstance(raw_score, dict) else None
            ),
            "players": players,
        },
        "artifact_urls": {
            "status": urls["status_url"],
            "watch": urls["watch_url"],
            "replay": urls["replay_url"],
            "frames": urls["frames_url"],
            "video": urls["video_url"],
        },
    }


def _disk_game_row(manifest: Mapping[str, Any]) -> dict[str, Any] | None:
    game_id = manifest.get("game_id")
    config = manifest.get("config")
    if not isinstance(game_id, str) or not GAME_ID_RE.fullmatch(game_id):
        return None
    if not isinstance(config, dict):
        return None
    state = _public_text(
        manifest.get("state", manifest.get("status")), "unknown", 32,
    )
    validity = manifest.get("benchmark_valid")
    if not isinstance(validity, bool):
        validity = None
    if state in TERMINAL_STATES:
        outcome_summary = (
            "Terminal result available in this replay."
            if validity else "No valid winner."
        )
        outcome_status = "complete" if validity else "no_valid_winner"
    else:
        outcome_summary = "Match in progress."
        outcome_status = "pending"
    places = _public_places(manifest.get("resolved_places"))
    return {
        "game_id": game_id,
        "state": state,
        "created_at": _public_number(manifest.get("created_at")),
        "current_turn": (
            _public_int(manifest.get("current_turn"))
            if manifest.get("current_turn") is not None else None
        ),
        "turns": _public_int(config.get("turns")),
        "benchmark_valid": validity,
        "mode": _public_text(config.get("mode"), "unknown", 32),
        **_public_timing(config),
        "places": _public_int(config.get("places")),
        "max_agents": _public_int(config.get("max_agents")),
        "joined_agents": _public_int(manifest.get("joined_agents")),
        "resolved_places": places,
        "leaderboard": [],
        "outcome": {
            "status": outcome_status,
            "summary": outcome_summary,
            "leaders": [],
            "margin": None,
            "score_turn": None,
            "victory": None,
        },
        "watch_path": f"/watch/{game_id}",
    }


def _disk_games_index(
    runs_root: Path, *, terminal_only: bool = False,
) -> dict[str, Any]:
    rows: list[dict[str, Any]] = []
    try:
        candidates = tuple(runs_root.iterdir())
    except OSError:
        candidates = ()
    for candidate in candidates:
        if (
            candidate.is_symlink()
            or not candidate.is_dir()
            or not GAME_ID_RE.fullmatch(candidate.name)
        ):
            continue
        try:
            manifest = _read_manifest(runs_root, candidate.name)
            row = _disk_game_row(manifest)
        except GatewayProblem:
            continue
        if row is not None and row["state"] in TERMINAL_STATES:
            try:
                archive = _terminal_archive(runs_root, candidate.name)
            except GatewayProblem:
                row = None
            else:
                row["leaderboard"] = archive.leaderboard
                row["outcome"] = archive.outcome
        elif terminal_only:
            row = None
        if row is not None:
            rows.append(row)
    rows.sort(
        key=lambda row: (row["created_at"], row["game_id"]), reverse=True,
    )
    return {"schema_version": 1, "games": rows}


class ReplayGatewayHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(
        self,
        address: tuple[str, int],
        config: GatewayConfig,
        replay_loader: ReplayLoader | None = None,
        board_loader: BoardLoader | None = None,
    ):
        if ":" in address[0]:
            self.address_family = socket.AF_INET6
        self.gateway_config = config
        self.replay_loader = replay_loader or _default_replay_loader
        self.board_loader = board_loader or _default_board_loader
        self.replay_lock = threading.RLock()
        super().__init__(address, ReplayGatewayHandler)

    def identity_payload(self) -> dict[str, Any]:
        host, port = self.server_address[:2]
        rendered_host = f"[{host}]" if ":" in host else host
        payload = {
            "schema_version": 1,
            "ok": True,
            "kind": GATEWAY_KIND,
            "protocol_version": GATEWAY_PROTOCOL_VERSION,
            "identity": self.gateway_config.identity,
            "pid": os.getpid(),
            "host": host,
            "port": int(port),
            "url": f"http://{rendered_host}:{port}",
            "repo_root": str(self.gateway_config.repo_root),
            "upstream_service_url": (
                self.gateway_config.upstream_service_url
            ),
            "runs_root": str(self.gateway_config.runs_root),
            "cache_root": str(self.gateway_config.cache_root),
        }
        if self.gateway_config.viewer_public_url is not None:
            payload["viewer_public_url"] = (
                self.gateway_config.viewer_public_url
            )
        return payload


class ReplayGatewayHandler(BaseHTTPRequestHandler):
    server: ReplayGatewayHTTPServer
    protocol_version = "HTTP/1.0"

    def log_message(self, format: str, *args: object) -> None:
        return

    def _send_headers(
        self,
        status: int,
        content_type: str,
        *,
        content_length: int | None = None,
        headers: Mapping[str, str] | None = None,
    ) -> None:
        self.send_response(int(status))
        self.send_header("Content-Type", content_type)
        if content_length is not None:
            self.send_header("Content-Length", str(content_length))
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        if headers:
            for name, value in headers.items():
                if name.lower() not in {"content-type", "content-length"}:
                    self.send_header(name, value)
        elif status >= 400:
            self.send_header("Cache-Control", "no-store")
        self.end_headers()

    def _send(self, status: int, body: bytes, content_type: str) -> None:
        self._send_headers(
            status, content_type, content_length=len(body),
            headers={"Cache-Control": "no-store"},
        )
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _json(self, status: int, value: Any) -> None:
        self._send(
            status, _canonical(value), "application/json; charset=utf-8",
        )

    def _bounded_json(self, status: int, value: Any) -> None:
        body = _canonical(value)
        if len(body) > MAX_PROXY_JSON_BYTES:
            raise GatewayProblem(
                HTTPStatus.SERVICE_UNAVAILABLE,
                "archive JSON response is too large",
            )
        self._send(status, body, "application/json; charset=utf-8")

    def _problem(self, status: int, message: str) -> None:
        self._json(status, {"error": message})

    def _reject_body(self) -> None:
        transfer_encoding = self.headers.get("Transfer-Encoding")
        length = self.headers.get("Content-Length")
        try:
            has_length = length is not None and int(length) != 0
        except ValueError as exc:
            raise GatewayProblem(
                HTTPStatus.BAD_REQUEST, "invalid Content-Length",
            ) from exc
        if transfer_encoding or has_length:
            raise GatewayProblem(
                HTTPStatus.BAD_REQUEST, "GET request bodies are not accepted",
            )

    def _upstream_url(self, path: str, query: str = "") -> str:
        url = self.server.gateway_config.upstream_service_url + path
        return f"{url}?{query}" if query else url

    def _open_upstream(
        self, path: str, query: str = "", accept: str = "application/json",
    ) -> Any:
        # No inbound headers are copied.  In particular, authorization,
        # cookies, forwarding headers, ranges, and bodies never cross the
        # gateway boundary.
        request = urllib.request.Request(
            self._upstream_url(path, query),
            headers={"Accept": accept},
            method="GET",
        )
        try:
            return _NO_REDIRECT_OPENER.open(
                request, timeout=self.server.gateway_config.upstream_timeout_s,
            )
        except urllib.error.HTTPError as exc:
            content_type = exc.headers.get("Content-Type", "")
            if (
                exc.code == HTTPStatus.BAD_GATEWAY
                and exc.headers.get("X-Portless") == "1"
                and content_type.split(";", 1)[0].strip().lower()
                == "text/html"
            ):
                try:
                    exc.read(MAX_PROXY_ERROR_BYTES)
                finally:
                    exc.close()
                raise UpstreamUnavailable(
                    HTTPStatus.BAD_GATEWAY,
                    "the upstream Freeciv service is unavailable",
                ) from exc
            return exc
        except (OSError, http.client.HTTPException) as exc:
            raise UpstreamUnavailable(
                HTTPStatus.BAD_GATEWAY,
                "the upstream Freeciv service is unavailable",
            ) from exc

    @staticmethod
    def _upstream_status(response: Any) -> int:
        return int(getattr(response, "status", getattr(response, "code", 500)))

    def _proxy(self, path: str, query: str = "", *, binary: bool = False) -> int:
        response = self._open_upstream(
            path, query, "*/*" if binary else "application/json",
        )
        with response:
            return self._stream_upstream(response, binary=binary)

    def _stream_upstream(self, response: Any, *, binary: bool) -> int:
        status = self._upstream_status(response)
        if not 200 <= status < 300:
            response.read(MAX_PROXY_ERROR_BYTES)
            downstream = HTTPStatus.BAD_GATEWAY if 300 <= status < 400 else status
            self._problem(
                downstream,
                "upstream redirects are not allowed"
                if 300 <= status < 400 else
                f"upstream returned HTTP {status}",
            )
            return status
        headers: dict[str, str] = {}
        for name in PROXY_RESPONSE_HEADERS:
            value = response.headers.get(name)
            if value is not None:
                headers[name] = value
        content_type = headers.get(
            "Content-Type",
            "application/octet-stream" if binary
            else "application/json; charset=utf-8",
        )
        content_length = None
        if (raw_length := headers.get("Content-Length")) is not None:
            try:
                content_length = int(raw_length)
            except ValueError:
                content_length = None
        self._send_headers(
            status, content_type, content_length=content_length,
            headers=headers,
        )
        try:
            while chunk := response.read(64 * 1024):
                self.wfile.write(chunk)
        except (BrokenPipeError, ConnectionResetError):
            pass
        except (OSError, http.client.HTTPException):
            self.close_connection = True
        return status

    def _send_local_file(self, path: Path, content_type: str) -> None:
        flags = os.O_RDONLY
        if hasattr(os, "O_NOFOLLOW"):
            flags |= os.O_NOFOLLOW
        try:
            descriptor = os.open(path, flags)
        except OSError as exc:
            raise GatewayProblem(HTTPStatus.NOT_FOUND, "archive file not found") from exc
        try:
            info = os.fstat(descriptor)
            if not stat.S_ISREG(info.st_mode) or info.st_size <= 0:
                raise GatewayProblem(
                    HTTPStatus.NOT_FOUND, "archive file not found",
                )
            self._send_headers(
                HTTPStatus.OK,
                content_type,
                content_length=info.st_size,
                headers={"Cache-Control": "public, max-age=31536000, immutable"},
            )
            with os.fdopen(descriptor, "rb", closefd=False) as stream:
                try:
                    while chunk := stream.read(64 * 1024):
                        self.wfile.write(chunk)
                except (BrokenPipeError, ConnectionResetError):
                    pass
        finally:
            os.close(descriptor)

    @staticmethod
    def _read_json_response(response: Any) -> bytes:
        raw_length = response.headers.get("Content-Length")
        if raw_length is not None:
            try:
                if int(raw_length) > MAX_PROXY_JSON_BYTES:
                    raise GatewayProblem(
                        HTTPStatus.BAD_GATEWAY,
                        "the upstream JSON response is too large",
                    )
            except ValueError:
                pass
        value = response.read(MAX_PROXY_JSON_BYTES + 1)
        if len(value) > MAX_PROXY_JSON_BYTES:
            raise GatewayProblem(
                HTTPStatus.BAD_GATEWAY,
                "the upstream JSON response is too large",
            )
        return value

    def _upstream_json_or_status(
        self, path: str, query: str = "",
    ) -> tuple[int, bytes | None]:
        response = self._open_upstream(path, query)
        with response:
            status = self._upstream_status(response)
            if 200 <= status < 300:
                body = self._read_json_response(response)
            else:
                response.read(MAX_PROXY_ERROR_BYTES)
                body = None
        return status, body

    @staticmethod
    def _replay_query(query: str) -> tuple[int, int, str]:
        values = urllib.parse.parse_qs(query, keep_blank_values=True)
        if set(values) - {"after_turn", "limit"} or any(
            len(items) != 1 for items in values.values()
        ):
            raise GatewayProblem(
                HTTPStatus.BAD_REQUEST,
                "replay query accepts one after_turn and one limit",
            )
        try:
            after_turn = int(values.get("after_turn", ["0"])[0])
            limit = int(values.get("limit", ["250"])[0])
        except ValueError as exc:
            raise GatewayProblem(
                HTTPStatus.BAD_REQUEST,
                "after_turn and limit must be integers",
            ) from exc
        if after_turn < 0 or not 1 <= limit <= 250:
            raise GatewayProblem(
                HTTPStatus.BAD_REQUEST,
                "after_turn must be >= 0 and limit must be in [1, 250]",
            )
        normalized = urllib.parse.urlencode({
            "after_turn": after_turn, "limit": limit,
        })
        return after_turn, limit, normalized

    def _games(self) -> None:
        try:
            status, body = self._upstream_json_or_status("/v1/games")
        except UpstreamUnavailable:
            self._json(
                HTTPStatus.OK,
                _disk_games_index(
                    self.server.gateway_config.runs_root, terminal_only=True,
                ),
            )
            return
        if 200 <= status < 300:
            assert body is not None
            try:
                upstream = json.loads(body)
            except (UnicodeError, json.JSONDecodeError) as exc:
                raise GatewayProblem(
                    HTTPStatus.BAD_GATEWAY,
                    "the upstream games index is invalid",
                ) from exc
            upstream_rows = upstream.get("games") if isinstance(upstream, dict) else None
            if not isinstance(upstream_rows, list):
                raise GatewayProblem(
                    HTTPStatus.BAD_GATEWAY,
                    "the upstream games index is invalid",
                )
            live_ids = {
                row.get("game_id") for row in upstream_rows
                if isinstance(row, dict)
                and isinstance(row.get("game_id"), str)
                and GAME_ID_RE.fullmatch(row["game_id"])
            }
            archived = _disk_games_index(
                self.server.gateway_config.runs_root, terminal_only=True,
            )["games"]
            self._bounded_json(HTTPStatus.OK, {
                "schema_version": 1,
                "games": [
                    *upstream_rows,
                    *(row for row in archived if row["game_id"] not in live_ids),
                ],
            })
            return
        if status not in {HTTPStatus.NOT_FOUND, HTTPStatus.METHOD_NOT_ALLOWED}:
            downstream = HTTPStatus.BAD_GATEWAY if 300 <= status < 400 else status
            self._problem(
                downstream,
                "upstream redirects are not allowed"
                if 300 <= status < 400 else
                f"upstream returned HTTP {status}",
            )
            return
        self._json(
            HTTPStatus.OK,
            _disk_games_index(self.server.gateway_config.runs_root),
        )

    def _replay(self, game_id: str, query: str) -> None:
        after_turn, limit, normalized_query = self._replay_query(query)
        path = f"/v1/games/{game_id}/replay.json"
        upstream_offline = False
        try:
            status, body = self._upstream_json_or_status(path, normalized_query)
        except UpstreamUnavailable:
            upstream_offline = True
            status, body = HTTPStatus.BAD_GATEWAY, None
        if 200 <= status < 300:
            assert body is not None
            self._send(
                status, body, "application/json; charset=utf-8",
            )
            return
        if (
            not upstream_offline
            and status not in {HTTPStatus.NOT_FOUND, HTTPStatus.METHOD_NOT_ALLOWED}
        ):
            downstream = HTTPStatus.BAD_GATEWAY if 300 <= status < 400 else status
            self._problem(
                downstream,
                "upstream redirects are not allowed"
                if 300 <= status < 400 else
                f"upstream returned HTTP {status}",
            )
            return

        if upstream_offline:
            archive = _terminal_archive(
                self.server.gateway_config.runs_root, game_id,
            )
            manifest = archive.manifest
        else:
            manifest = _read_manifest(self.server.gateway_config.runs_root, game_id)
        places = _public_places(manifest.get("resolved_places"))
        state = _public_text(
            manifest.get("state", manifest.get("status")), "unknown", 32,
        )
        try:
            with self.server.replay_lock:
                replay = self.server.replay_loader(
                    self.server.gateway_config.runs_root,
                    game_id,
                    places,
                    after_turn=after_turn,
                    limit=limit,
                    cache_root=self.server.gateway_config.cache_root,
                    complete=state in TERMINAL_STATES,
                )
        except FileNotFoundError as exc:
            raise GatewayProblem(
                HTTPStatus.NOT_FOUND, "game replay artifacts not found",
            ) from exc
        except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as exc:
            raise GatewayProblem(
                HTTPStatus.SERVICE_UNAVAILABLE,
                "replay telemetry is temporarily unavailable",
            ) from exc
        if not isinstance(replay, Mapping):
            raise GatewayProblem(
                HTTPStatus.SERVICE_UNAVAILABLE,
                "replay telemetry is temporarily unavailable",
            )
        self._bounded_json(HTTPStatus.OK, replay)

    @staticmethod
    def _board_query(query: str) -> tuple[int, str]:
        values = urllib.parse.parse_qs(query, keep_blank_values=True)
        if set(values) != {"turn"} or len(values["turn"]) != 1:
            raise GatewayProblem(
                HTTPStatus.BAD_REQUEST,
                "board query requires exactly one turn",
            )
        try:
            turn = int(values["turn"][0])
        except ValueError as exc:
            raise GatewayProblem(
                HTTPStatus.BAD_REQUEST, "turn must be an integer",
            ) from exc
        if turn <= 0:
            raise GatewayProblem(
                HTTPStatus.BAD_REQUEST, "turn must be greater than zero",
            )
        return turn, urllib.parse.urlencode({"turn": turn})

    def _board(self, game_id: str, query: str) -> None:
        turn, normalized_query = self._board_query(query)
        path = f"/v1/games/{game_id}/board.json"
        upstream_offline = False
        try:
            status, body = self._upstream_json_or_status(path, normalized_query)
        except UpstreamUnavailable:
            upstream_offline = True
            status, body = HTTPStatus.BAD_GATEWAY, None
        if 200 <= status < 300:
            assert body is not None
            self._send(status, body, "application/json; charset=utf-8")
            return
        if (
            not upstream_offline
            and status not in {HTTPStatus.NOT_FOUND, HTTPStatus.METHOD_NOT_ALLOWED}
        ):
            downstream = HTTPStatus.BAD_GATEWAY if 300 <= status < 400 else status
            self._problem(
                downstream,
                "upstream redirects are not allowed"
                if 300 <= status < 400 else
                f"upstream returned HTTP {status}",
            )
            return
        if upstream_offline:
            manifest = _terminal_archive(
                self.server.gateway_config.runs_root, game_id,
            ).manifest
        else:
            manifest = _read_manifest(self.server.gateway_config.runs_root, game_id)
        places = _public_places(manifest.get("resolved_places"))
        try:
            with self.server.replay_lock:
                board = self.server.board_loader(
                    self.server.gateway_config.runs_root,
                    game_id,
                    places,
                    turn=turn,
                    cache_root=self.server.gateway_config.cache_root,
                )
        except FileNotFoundError as exc:
            raise GatewayProblem(
                HTTPStatus.NOT_FOUND, "board snapshot not found",
            ) from exc
        except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as exc:
            raise GatewayProblem(
                HTTPStatus.SERVICE_UNAVAILABLE,
                "board snapshot is temporarily unavailable",
            ) from exc
        if not isinstance(board, Mapping):
            raise GatewayProblem(
                HTTPStatus.SERVICE_UNAVAILABLE,
                "board snapshot is temporarily unavailable",
            )
        self._bounded_json(HTTPStatus.OK, board)

    def _archive_base(self) -> str:
        return (
            self.server.gateway_config.viewer_public_url
            or str(self.server.identity_payload()["url"])
        )

    def _archive_json_route(
        self, path: str, game_id: str, kind: str,
    ) -> None:
        upstream_offline = False
        try:
            status, body = self._upstream_json_or_status(path)
        except UpstreamUnavailable:
            upstream_offline = True
            status, body = HTTPStatus.BAD_GATEWAY, None
        if 200 <= status < 300:
            assert body is not None
            self._send(status, body, "application/json; charset=utf-8")
            return
        if (
            not upstream_offline
            and status not in {HTTPStatus.NOT_FOUND, HTTPStatus.METHOD_NOT_ALLOWED}
        ):
            downstream = HTTPStatus.BAD_GATEWAY if 300 <= status < 400 else status
            self._problem(
                downstream,
                "upstream redirects are not allowed"
                if 300 <= status < 400 else
                f"upstream returned HTTP {status}",
            )
            return
        archive = _terminal_archive(
            self.server.gateway_config.runs_root, game_id,
        )
        base = self._archive_base()
        absolute_watch = self.server.gateway_config.viewer_public_url is not None
        if kind == "status":
            value = _archive_status(
                archive, base, absolute_watch=absolute_watch,
            )
        elif kind == "watch":
            value = _archive_watch(
                archive, base, absolute_watch=absolute_watch,
            )
        elif kind == "frames":
            value = _archive_frames(archive, base)
        elif kind == "result":
            value = _archive_result(
                archive, base, absolute_watch=absolute_watch,
            )
        else:
            raise GatewayProblem(HTTPStatus.NOT_FOUND, "not found")
        self._bounded_json(HTTPStatus.OK, value)

    def _archive_binary_route(
        self, path: str, game_id: str, *, index: int | None = None,
        video: bool = False,
    ) -> None:
        response = None
        upstream_offline = False
        try:
            response = self._open_upstream(path, accept="*/*")
        except UpstreamUnavailable:
            upstream_offline = True
        if response is not None:
            with response:
                status = self._upstream_status(response)
                if 200 <= status < 300:
                    self._stream_upstream(response, binary=True)
                    return
                if status not in {
                    HTTPStatus.NOT_FOUND, HTTPStatus.METHOD_NOT_ALLOWED,
                }:
                    self._stream_upstream(response, binary=True)
                    return
        archive = _terminal_archive(
            self.server.gateway_config.runs_root, game_id,
        )
        local = (
            _archive_video_path(archive)
            if video else _archive_frame_path(archive, index)
        )
        self._send_local_file(local, "video/mp4" if video else "image/png")

    def do_GET(self) -> None:
        try:
            self._reject_body()
            parsed = urllib.parse.urlsplit(self.path)
            path = parsed.path
            if path == "/health":
                if parsed.query:
                    raise GatewayProblem(
                        HTTPStatus.BAD_REQUEST,
                        "health does not accept query parameters",
                    )
                self._json(HTTPStatus.OK, self.server.identity_payload())
                return
            if path == "/v1/games":
                if parsed.query:
                    raise GatewayProblem(
                        HTTPStatus.BAD_REQUEST,
                        "games index does not accept query parameters",
                    )
                self._games()
                return

            parts = path.strip("/").split("/")
            if (
                len(parts) < 3
                or parts[:2] != ["v1", "games"]
                or not GAME_ID_RE.fullmatch(parts[2])
            ):
                raise GatewayProblem(HTTPStatus.NOT_FOUND, "not found")
            game_id = parts[2]
            suffix = parts[3:]
            if suffix == ["replay.json"]:
                self._replay(game_id, parsed.query)
                return
            if suffix == ["board.json"]:
                self._board(game_id, parsed.query)
                return
            if parsed.query:
                raise GatewayProblem(
                    HTTPStatus.BAD_REQUEST,
                    "this viewer route does not accept query parameters",
                )
            if not suffix or suffix == ["status"]:
                self._archive_json_route(path, game_id, "status")
                return
            if suffix == ["result"]:
                self._archive_json_route(path, game_id, "result")
                return
            if suffix == ["watch.json"]:
                self._archive_json_route(path, game_id, "watch")
                return
            if suffix == ["frames"]:
                self._archive_json_route(path, game_id, "frames")
                return
            if suffix == ["frames", "latest.png"]:
                self._archive_binary_route(path, game_id, index=None)
                return
            if suffix == ["video.mp4"]:
                self._archive_binary_route(path, game_id, video=True)
                return
            if (
                len(suffix) == 2
                and suffix[0] == "frames"
                and FRAME_INDEX_RE.fullmatch(suffix[1])
            ):
                self._archive_binary_route(
                    path, game_id, index=int(suffix[1][:-4]),
                )
                return
            raise GatewayProblem(HTTPStatus.NOT_FOUND, "not found")
        except GatewayProblem as exc:
            self._problem(exc.status, str(exc))
        except Exception:
            self._problem(
                HTTPStatus.INTERNAL_SERVER_ERROR,
                "the replay gateway encountered an internal error",
            )

    def _method_not_allowed(self) -> None:
        body = _canonical({"error": "method not allowed"})
        self._send_headers(
            HTTPStatus.METHOD_NOT_ALLOWED,
            "application/json; charset=utf-8",
            content_length=len(body),
            headers={"Allow": "GET", "Cache-Control": "no-store"},
        )
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    do_HEAD = _method_not_allowed
    do_POST = _method_not_allowed
    do_PUT = _method_not_allowed
    do_PATCH = _method_not_allowed
    do_DELETE = _method_not_allowed
    do_OPTIONS = _method_not_allowed


def make_replay_gateway_server(
    host: str,
    port: int,
    service_url: str,
    runs_root: str | Path,
    cache_root: str | Path,
    *,
    repo_root: str | Path = REPO_ROOT,
    upstream_timeout_s: float = 10,
    viewer_public_url: str | None = None,
    replay_loader: ReplayLoader | None = None,
    board_loader: BoardLoader | None = None,
) -> ReplayGatewayHTTPServer:
    loopback = _loopback_host(host)
    if isinstance(port, bool) or not isinstance(port, int) or not 0 <= port <= 65535:
        raise ValueError("gateway port must be in [0, 65535]")
    config = gateway_config(
        service_url,
        runs_root,
        cache_root,
        repo_root=repo_root,
        upstream_timeout_s=upstream_timeout_s,
        viewer_public_url=viewer_public_url,
    )
    config.cache_root.mkdir(parents=True, exist_ok=True)
    return ReplayGatewayHTTPServer(
        (loopback, port), config,
        replay_loader=replay_loader,
        board_loader=board_loader,
    )


def _remove_owned_ready_file(path: Path, identity: str, pid: int) -> None:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        return
    if (
        isinstance(value, dict)
        and value.get("identity") == identity
        and value.get("pid") == pid
    ):
        path.unlink(missing_ok=True)


def run_replay_gateway(
    host: str,
    port: int,
    service_url: str,
    runs_root: str | Path,
    cache_root: str | Path,
    *,
    repo_root: str | Path = REPO_ROOT,
    ready_file: str | Path | None = None,
    upstream_timeout_s: float = 10,
    viewer_public_url: str | None = None,
    replay_loader: ReplayLoader | None = None,
    board_loader: BoardLoader | None = None,
    stop_event: threading.Event | None = None,
    on_ready: Callable[[dict[str, Any]], None] | None = None,
) -> int:
    """Run one local gateway until interrupted or ``stop_event`` is set."""
    server = make_replay_gateway_server(
        host,
        port,
        service_url,
        runs_root,
        cache_root,
        repo_root=repo_root,
        upstream_timeout_s=upstream_timeout_s,
        viewer_public_url=viewer_public_url,
        replay_loader=replay_loader,
        board_loader=board_loader,
    )
    ready = server.identity_payload()
    ready_path = (
        Path(ready_file).expanduser().resolve() if ready_file is not None
        else None
    )
    ready_lock: int | None = None
    try:
        if ready_path is not None:
            ready_lock = _acquire_ready_lock(ready_path)
            _write_private_json(ready_path, ready)
        if on_ready is not None:
            on_ready(ready)
        if stop_event is None:
            server.serve_forever()
        else:
            server.timeout = 0.1
            while not stop_event.is_set():
                server.handle_request()
    finally:
        server.server_close()
        if ready_path is not None:
            _remove_owned_ready_file(
                ready_path, server.gateway_config.identity, os.getpid(),
            )
        if ready_lock is not None:
            _release_ready_lock(ready_lock)
    return 0


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="python3 -m agent_eval.replay_gateway")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--service-url", required=True)
    parser.add_argument("--runs-root", required=True)
    parser.add_argument("--cache-root", required=True)
    parser.add_argument("--repo-root", default=str(REPO_ROOT))
    parser.add_argument("--ready-file", required=True)
    parser.add_argument("--upstream-timeout-s", type=float, default=10)
    parser.add_argument("--viewer-public-url")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        return run_replay_gateway(
            args.host,
            args.port,
            args.service_url,
            args.runs_root,
            args.cache_root,
            repo_root=args.repo_root,
            ready_file=args.ready_file,
            upstream_timeout_s=args.upstream_timeout_s,
            viewer_public_url=args.viewer_public_url,
            on_ready=lambda value: print(
                _canonical(value).decode("utf-8"), flush=True,
            ),
        )
    except KeyboardInterrupt:
        return 0
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
