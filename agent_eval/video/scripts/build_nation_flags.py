#!/usr/bin/env python3
"""Regenerate the film's nation-to-flag map and stage the flags it needs.

A replay names its factions the way the ruleset does -- `English`, `Greek` --
and the flag art is filed under a different name entirely: `england.svg`,
`greece_ancient.svg`. Only `data/nation/*.ruleset` knows both halves, so this
script reads the mapping out of the rulesets rather than anyone guessing it.

Freeciv ships each flag three ways. `<slug>.png` is 29x20, `-large.png` is
44x30, `-shield.png` is 19x19: in-game sprites, useless at 1920x1080. Only
`<slug>.svg` scales, so that is the only form staged here.

The whole set is 582 files and 16.6 MB, and any one film needs three or four of
them, so this stages only the flags the checked-in datasets under
`video/public/exports/` actually name. Run it after adding a dataset.

    python3 agent_eval/video/scripts/build_nation_flags.py          # write
    python3 agent_eval/video/scripts/build_nation_flags.py --check  # verify only

`--check` is the useful one in review: it re-derives everything and fails if the
committed map or the staged flags have drifted from the rulesets.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import sys
from pathlib import Path

VIDEO = Path(__file__).resolve().parent.parent
REPO_ROOT = VIDEO.parent.parent
NATION_DIR = REPO_ROOT / "data" / "nation"
FLAG_DIR = REPO_ROOT / "data" / "flags"
MAP_PATH = VIDEO / "src" / "dataset" / "nation-flags.json"
# Which flags are actually on disk. The map covers all 572 nations, but only a
# handful are staged, and a component that renders an `<img>` at a missing file
# draws an empty framed box. Knowing the staged set at build time lets it render
# nothing instead, with no load-failure round trip.
ASSETS_PATH = VIDEO / "src" / "dataset" / "flag-assets.json"
STAGED_DIR = VIDEO / "public" / "flags"
EXPORTS_DIR = VIDEO / "public" / "exports"

# Top-level keys only. A ruleset's `[leaders]` block has its own `name`s, and
# they are indented, so anchoring to the start of the line is what separates the
# nation's name from its rulers'.
NAME_RE = re.compile(r'^name\s*=\s*_?\("?([^")]+)"?\)?', re.MULTILINE)
FLAG_RE = re.compile(r'^flag\s*=\s*"([^"]+)"', re.MULTILINE)


def read_nation_flags() -> tuple[dict[str, str], list[str]]:
    """Every nation that names a flag with real `.svg` art, and what was skipped."""
    mapping: dict[str, str] = {}
    skipped: list[str] = []
    for path in sorted(NATION_DIR.glob("*.ruleset")):
        text = path.read_text(encoding="utf-8", errors="replace")
        name = NAME_RE.search(text)
        flag = FLAG_RE.search(text)
        if not name or not flag:
            skipped.append(f"{path.name}: no name/flag pair")
            continue
        # `?plural:` and friends are gettext qualifiers, not part of the name.
        nation = name.group(1).split(":")[-1].strip()
        slug = flag.group(1).strip()
        if not (FLAG_DIR / f"{slug}.svg").is_file():
            skipped.append(f"{path.name}: {nation} -> {slug}.svg missing")
            continue
        if nation in mapping and mapping[nation] != slug:
            skipped.append(f"{path.name}: {nation} already maps to {mapping[nation]}")
            continue
        mapping[nation] = slug
    return dict(sorted(mapping.items())), skipped


def nations_in_exports() -> set[str]:
    """Every nation named by a checked-in dataset."""
    nations: set[str] = set()
    for meta in sorted(EXPORTS_DIR.glob("*/meta.json")):
        players = json.loads(meta.read_text(encoding="utf-8")).get("players") or []
        for player in players:
            nation = player.get("nation")
            if isinstance(nation, str) and nation:
                nations.add(nation)
    return nations


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check", action="store_true",
        help="fail instead of writing if the committed files have drifted",
    )
    args = parser.parse_args()

    mapping, skipped = read_nation_flags()
    for note in skipped:
        print(f"skipped {note}", file=sys.stderr)

    wanted = {mapping[n] for n in nations_in_exports() if n in mapping}
    unmapped = sorted(n for n in nations_in_exports() if n not in mapping)
    if unmapped:
        print(f"no flag for: {', '.join(unmapped)}", file=sys.stderr)

    serialized = "{\n" + ",\n".join(
        f'{json.dumps(nation)}: {json.dumps(slug)}' for nation, slug in mapping.items()
    ) + "\n}\n"
    serialized_assets = json.dumps(sorted(wanted), indent=1) + "\n"

    staged = {path.stem for path in STAGED_DIR.glob("*.svg")}
    if args.check:
        drift = []
        if not MAP_PATH.is_file() or MAP_PATH.read_text(encoding="utf-8") != serialized:
            drift.append(f"{MAP_PATH.relative_to(REPO_ROOT)} is stale")
        if not ASSETS_PATH.is_file() or ASSETS_PATH.read_text(encoding="utf-8") != serialized_assets:
            drift.append(f"{ASSETS_PATH.relative_to(REPO_ROOT)} is stale")
        if staged != wanted:
            missing = ", ".join(sorted(wanted - staged)) or "none"
            extra = ", ".join(sorted(staged - wanted)) or "none"
            drift.append(f"public/flags: missing [{missing}] unexpected [{extra}]")
        for problem in drift:
            print(problem, file=sys.stderr)
        print(f"{len(mapping)} nations mapped, {len(wanted)} flags wanted")
        return 1 if drift else 0

    MAP_PATH.write_text(serialized, encoding="utf-8")
    ASSETS_PATH.write_text(serialized_assets, encoding="utf-8")
    STAGED_DIR.mkdir(parents=True, exist_ok=True)
    for slug in sorted(staged - wanted):
        (STAGED_DIR / f"{slug}.svg").unlink()
    for slug in sorted(wanted):
        shutil.copyfile(FLAG_DIR / f"{slug}.svg", STAGED_DIR / f"{slug}.svg")
    print(f"{len(mapping)} nations -> {MAP_PATH.relative_to(REPO_ROOT)}")
    print(f"{len(wanted)} flags -> {STAGED_DIR.relative_to(REPO_ROOT)}: {', '.join(sorted(wanted))}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
