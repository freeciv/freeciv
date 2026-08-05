"""Materialize per-player game workspaces under .play/.

``just play`` (repository root) drives this module. It asks for a game ID
and one or more harness/model players, then creates one self-contained
player workspace per player:

    .play/GAME_ID_HARNESS_MODEL/

Each workspace is a copy of ``play/`` with the game invitation staged, a
``.playconfig.json`` recording the pre-selected game and controller name
(so ``just join`` inside the workspace needs no arguments), and an
AGENTS.md/CLAUDE.md note telling the playing agent the folder is also its
private scratchpad. The workspace never receives owner or admin
credentials — only the game-scoped join invitation.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
GAME_ID_RE = re.compile(r"^game_[A-Za-z0-9_-]{20,80}$")
SLUG_SAFE_RE = re.compile(r"[^A-Za-z0-9._-]+")
COPY_IGNORE = (
    "__pycache__", "*.pyc", "*.jsonl", "*.out", "*.log",
    ".sessions", ".invites", ".play", ".playconfig.json",
)

HARNESS_CHOICES = ("codex", "pi", "claude-code", "opencode")
MODEL_CHOICES = (
    "gpt-5.6-sol",
    "gpt-5.5",
    "claude-opus-5",
    "claude-fable-5",
    "gemini-3-pro",
)

V2_LOOP_NOTE = """This is a `full-control-v2` game. The loop:

    just join                    # prints the protocol card
    just start                   # lobby: configure + ready (no arguments needed)
    just turn                    # each turn: briefing, options, needs-decision list
    just do "u1 found_city London; u2 route 32,73"
    just turn --end --await      # end the phase and wait for the next

`just show` reads your local state mirror at zero network cost, and every
error names the exact command that fixes it."""

V1_LOOP_NOTE = """This is a `strategic-v1` game. The loop:

    just join
    just next --after_turn 0     # then pass the last observed turn each time
    just act --turn T --observation_id ID \\
      --action '{"type":"set_traits","traits":{"aggressive":0,"builder":20,"expansionist":30,"trader":10}}'

Read `docs/gameplay.md` for the trait contract before your first `act`."""

SCRATCHPAD_NOTE = """
## Your workspace and scratchpad

This folder is your private player workspace for {game_id}, playing as
`{name}`. It is also your scratchpad: write plans, notes, and helper
scripts anywhere inside it (for example `notes.md`, `plan.md`, or a
`scripts/` directory) to think through the game between turns. Keep
everything you create inside this folder; never read or write parent
directories or sibling player folders.

The game and your controller name are pre-configured, so `just join`
needs no arguments. {loop_note}
"""

CLAUDE_NOTE = """# Playing Freeciv from this workspace

Read `AGENTS.md` first — it is the workspace contract. This folder is
pre-configured for `{game_id}` as `{name}`; bare `just` reprints the short
workflow at any time.

{loop_note}

This folder is also your scratchpad. Write plans, notes, and helper
scripts here (`notes.md`, `plan.md`, `scripts/`) to reason about the game;
keep everything inside this folder.
"""

CONTROL_PROTOCOLS = {"strategic-v1", "full-control-v2"}


def _fetch_protocol(invite_path: Path) -> str:
    """Ask the supervisor which control protocol the game negotiates."""
    try:
        invite = json.loads(invite_path.read_text(encoding="utf-8"))
        service_url = invite["service_url"].rstrip("/")
        game_id = invite["game_id"]
    except (OSError, ValueError, KeyError, AttributeError) as exc:
        raise SetupError(f"unreadable invitation {invite_path}: {exc}") from exc
    url = f"{service_url}/v1/games/{game_id}/status"
    try:
        with urllib.request.urlopen(url, timeout=10) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except (urllib.error.URLError, OSError, ValueError) as exc:
        raise SetupError(
            f"could not read the game's control protocol from {url}: {exc}\n"
            "is the local stack running? start it with `just start` from "
            "the repository root"
        ) from exc
    protocol = payload.get("control_protocol") or "strategic-v1"
    if protocol not in CONTROL_PROTOCOLS:
        raise SetupError(
            f"game {game_id} negotiates unsupported protocol {protocol!r}"
        )
    return protocol


class SetupError(Exception):
    """A user-facing setup failure with its remedy in the message."""


def _valid_game_id(value: str) -> str:
    value = value.strip()
    if GAME_ID_RE.fullmatch(value) is None:
        raise SetupError(
            f"{value!r} is not a game ID; expected game_… as printed by "
            "`just single-v2` / `just multi-v2`"
        )
    return value


def _slug(value: str) -> str:
    return SLUG_SAFE_RE.sub("-", value).strip("-")


def _choose(prompt: str, choices: tuple[str, ...]) -> str:
    """Numbered single choice with a free-text escape."""
    print(prompt)
    for index, choice in enumerate(choices, 1):
        print(f"  {index}) {choice}")
    print(f"  {len(choices) + 1}) other (type it)")
    while True:
        raw = input("> ").strip()
        if raw.isdigit() and 1 <= int(raw) <= len(choices):
            return choices[int(raw) - 1]
        if raw.isdigit() and int(raw) == len(choices) + 1:
            typed = input("enter value: ").strip()
            if typed:
                return typed
        elif raw:
            # Typing the value directly also works.
            return raw
        print("pick a number from the list, or type a value")


def _interactive_players() -> list[tuple[str, str]]:
    players: list[tuple[str, str]] = []
    while True:
        harness = _choose("harness:", HARNESS_CHOICES)
        model = _choose("model:", MODEL_CHOICES)
        players.append((harness, model))
        print(f"added {harness}-{model}")
        again = input("add another player? [y/N] ").strip().lower()
        if again not in {"y", "yes"}:
            return players


def _parse_player(value: str) -> tuple[str, str]:
    harness, sep, model = value.partition(":")
    if not sep or not harness.strip() or not model.strip():
        raise SetupError(
            f"--player takes HARNESS:MODEL (for example codex:gpt-5.6-sol), "
            f"got {value!r}"
        )
    return harness.strip(), model.strip()


def _stage_invite(
    game_id: str, destination: Path, *, explicit: Path | None,
    repo_root: Path,
) -> str:
    """Place the game invitation into the workspace. Returns how."""
    destination.parent.mkdir(mode=0o700, exist_ok=True)
    if explicit is not None:
        if not explicit.is_file():
            raise SetupError(f"--invite {explicit} does not exist")
        shutil.copy(explicit, destination)
        destination.chmod(0o600)
        return f"copied from {explicit}"
    state_dir = Path(
        os.environ.get("AGENT_EVAL_STATE_DIR", str(repo_root / ".agent-eval"))
    )
    if not state_dir.is_absolute():
        state_dir = repo_root / state_dir
    credentials = state_dir / "games" / game_id / "owner.json"
    if credentials.is_file():
        completed = subprocess.run(
            (
                sys.executable, "-B", "-m", "agent_eval", "game",
                "stage-invite", game_id,
                "--credentials", str(credentials),
                "--output", str(destination),
                "--require-open-lobby",
            ),
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=False,
        )
        if completed.returncode == 0 and destination.is_file():
            return "staged fresh from owner credentials"
        detail = (completed.stderr or completed.stdout).strip()
        raise SetupError(
            f"staging the invitation failed: {detail}\n"
            f"run `just invite {game_id}` from the repository root, then "
            "retry `just play`"
        )
    existing = repo_root / "play" / ".invites" / f"{game_id}.json"
    if existing.is_file():
        shutil.copy(existing, destination)
        destination.chmod(0o600)
        return f"copied from {existing.relative_to(repo_root)}"
    raise SetupError(
        f"no invitation found for {game_id}: run `just invite {game_id}` "
        "from the repository root first (or pass --invite PATH)"
    )


def _materialize(
    game_id: str, harness: str, model: str, *, place: int | None,
    repo_root: Path, invite: Path | None, force: bool,
    protocol_cache: dict[str, str],
) -> Path:
    name = f"{harness}-{model}"
    workspace = repo_root / ".play" / _slug(f"{game_id}_{harness}_{model}")
    if workspace.exists():
        if not force:
            raise SetupError(
                f"{workspace.relative_to(repo_root)} already exists; rerun "
                "with --force to replace it (its sessions and notes are "
                "deleted), or open it as is"
            )
        shutil.rmtree(workspace)
    workspace.parent.mkdir(mode=0o700, exist_ok=True)
    shutil.copytree(
        repo_root / "play", workspace,
        ignore=shutil.ignore_patterns(*COPY_IGNORE),
    )
    (workspace / ".sessions").mkdir(mode=0o700)
    (workspace / ".invites").mkdir(mode=0o700)
    staged_invite = workspace / ".invites" / f"{game_id}.json"
    how = _stage_invite(
        game_id, staged_invite, explicit=invite, repo_root=repo_root,
    )
    if game_id not in protocol_cache:
        protocol_cache[game_id] = _fetch_protocol(staged_invite)
    protocol = protocol_cache[game_id]
    config = {
        "schema_version": 1,
        "game_id": game_id,
        "name": name,
        "place": place,
        "control_protocol": protocol,
    }
    config_path = workspace / ".playconfig.json"
    config_path.write_text(
        json.dumps(config, indent=2, sort_keys=True) + "\n", encoding="utf-8",
    )
    config_path.chmod(0o600)
    loop_note = V2_LOOP_NOTE if protocol == "full-control-v2" else V1_LOOP_NOTE
    note = SCRATCHPAD_NOTE.format(
        game_id=game_id, name=name, loop_note=loop_note,
    )
    agents = workspace / "AGENTS.md"
    with agents.open("a", encoding="utf-8") as handle:
        handle.write(note)
    if harness == "claude-code":
        (workspace / "CLAUDE.md").write_text(
            CLAUDE_NOTE.format(
                game_id=game_id, name=name, loop_note=loop_note,
            ),
            encoding="utf-8",
        )
    print(
        f"created {workspace.relative_to(repo_root)} "
        f"({name}, {protocol}"
        f"{f', place {place}' if place else ''}; invite {how})"
    )
    return workspace


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="just play",
        description="Materialize per-player game workspaces under .play/",
    )
    parser.add_argument("game_id", nargs="?", default="")
    parser.add_argument(
        "--player", action="append", default=[],
        metavar="HARNESS:MODEL",
        help="non-interactive player selection; repeatable",
    )
    parser.add_argument("--invite", default="", help="explicit invite file")
    parser.add_argument(
        "--force", action="store_true",
        help="replace an existing workspace for the same player",
    )
    parser.add_argument(
        "--repo-root", default=str(REPO_ROOT), help=argparse.SUPPRESS,
    )
    args = parser.parse_args(argv)
    repo_root = Path(args.repo_root).resolve()
    try:
        game_id = args.game_id.strip()
        if not game_id:
            game_id = input("game id (game_…): ").strip()
        game_id = _valid_game_id(game_id)
        if args.player:
            players = [_parse_player(value) for value in args.player]
        else:
            players = _interactive_players()
        invite = Path(args.invite) if args.invite else None
        workspaces = []
        protocol_cache: dict[str, str] = {}
        for index, (harness, model) in enumerate(players):
            place = index + 1 if len(players) > 1 else None
            workspaces.append(_materialize(
                game_id, harness, model, place=place, repo_root=repo_root,
                invite=invite, force=args.force,
                protocol_cache=protocol_cache,
            ))
        print()
        print("Point each model at its folder and say go, for example:")
        for workspace in workspaces:
            print(f"  cd {workspace.relative_to(repo_root)} && just join")
        return 0
    except SetupError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    except (KeyboardInterrupt, EOFError):
        print("\ncancelled", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
