from __future__ import annotations

import os
import json
import re
import tempfile
import unittest
from contextlib import contextmanager
from pathlib import Path
from unittest.mock import patch

import client
import state_mirror


GAME_ID = "game_12345678901234567890"
AGENT_ID = "agent_test-controller"
UNIT_A = "unit_" + "a" * 32
UNIT_B = "unit_" + "b" * 32
CITY_A = "city_" + "c" * 32
SECRET = "v2-agent-secret-bearer-token"


def revision(number: int = 7, *, turn: int = 3) -> dict:
    return {
        "turn": turn,
        "revision": number,
        "state_token": f"state_token_{number:032d}",
    }


def page(
    section: str,
    rev: dict,
    items: list,
    *,
    cursor: str | None = None,
    total: int | None = None,
    scope: dict | None = None,
    complete: bool | None = None,
) -> dict:
    inner = {
        "section": section,
        "items": items,
        "total_items": len(items) if total is None else total,
        "next_cursor": cursor,
        "cursor_expires_at": None,
    }
    if scope is not None:
        inner["scope"] = scope
        inner["catalog_id"] = "catalog_" + "d" * 32
        inner["catalog_complete"] = (
            cursor is None if complete is None else complete
        )
    return {
        "schema_version": 2,
        "control_protocol": "full-control-v2",
        "game_id": GAME_ID,
        "agent_id": AGENT_ID,
        "state_revision": rev,
        "page": inner,
    }


def unit(
    identifier: str = UNIT_A,
    *,
    kind: str = "Settlers",
    x: int = 31,
    y: int = 72,
    moves: int = 3,
    hp: int = 20,
    extra: dict | None = None,
) -> dict:
    item = {
        "id": identifier,
        "scope": "own",
        "owner_player_id": "player_" + "e" * 32,
        "type": kind,
        "type_id": "unit_type_1",
        "tile_id": "tile_" + "f" * 32,
        "x": x,
        "y": y,
        "hp": hp,
        "moves": moves,
        "type_stats": {"max_hp": 20, "move_rate": 3},
        "activity": {"name": "idle", "progress": 0, "target": None},
        "route": None,
    }
    if extra:
        item.update(extra)
    return item


def city(identifier: str = CITY_A, *, name: str = "London", size: int = 1) -> dict:
    return {
        "id": identifier,
        "name": name,
        "x": 31,
        "y": 72,
        "size": size,
        "surplus": {"food": 2, "shields": 1, "trade": 0},
        "production": {
            "kind": "unit",
            "name": "Warriors",
            "shield_stock": 2,
            "shield_cost": 10,
            "buy_cost": 44,
            "can_buy": True,
        },
    }


def descriptor(
    rev: dict,
    action_id: str,
    *,
    kind: str = "unit.found_city",
    subject: dict | None = None,
    schema: dict | None = None,
) -> dict:
    return {
        "action_id": action_id,
        "kind": kind,
        "label": "Found city",
        "subject": subject if subject is not None else {
            "actor": {"type": "unit", "id": UNIT_A},
            "target": {"type": "tile", "id": "tile_x", "x": 31, "y": 72},
            "operation": "found_city",
            "variant": None,
            "consuming": False,
            "legality": "legal",
            "probability": {
                "kind": "exact",
                "minimum_percent": 100.0,
                "maximum_percent": 100.0,
            },
        },
        "arguments_schema": schema if schema is not None else {},
        "state_revision": rev,
    }


def health(*, active: bool = True) -> dict:
    return {
        "schema_version": 2,
        "control_protocol": "full-control-v2",
        "game_id": GAME_ID,
        "agent": {
            "agent_id": AGENT_ID, "controller_label": "codex-test-model",
        },
        "game_state": "running",
        "seat": {"place": 1, "seat_id": "place-1", "player_name": "AgentPlace1"},
        "sidecar": {
            "state": "running", "generation": 1, "server_connected": True,
        },
        "observation_available": True,
        "legal_actions_available": True,
        "phase": {
            "state": "awaiting_agent",
            "turn": 3,
            "phase": 0,
            "active": active,
            "timing": {
                "mode": "default",
                "timeout_s": 180,
                "deadline_started_at": 10.0,
                "deadline_at": 190.0,
                "elapsed_s": 139.0,
                "remaining_s": 41.0,
            },
        },
        "last_phase_end": None,
        "objective": "Maximize final Freeciv civilization score.",
        "max_turns": 5000,
        "turns_remaining": 4997,
    }


def receipt(state: str = "applied", *, rev: dict | None = None) -> dict:
    current = rev if rev is not None else revision(8)
    error = None
    if state == "rejected":
        error = {
            "schema_version": 2,
            "control_protocol": "full-control-v2",
            "error": {
                "code": "illegal_action",
                "message": "that action is not legal now",
                "retryable": False,
                "details": {},
            },
            "state_revision": current,
        }
    return {
        "schema_version": 2,
        "control_protocol": "full-control-v2",
        "game_id": GAME_ID,
        "agent_id": AGENT_ID,
        "batch_id": "batch_" + "9" * 32,
        "receipt_state": state,
        "idempotent": False,
        "state_revision": current,
        "error": error,
        "observation": None,
    }


@contextmanager
def workspace():
    """A patched player workspace with one private session file."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory) / "play"
        root.mkdir()
        with patch.object(client, "ROOT", root), patch.dict(
            os.environ, {"PLAY_STATE_DIR": ".sessions"}, clear=False,
        ):
            session_path = root / ".sessions" / GAME_ID / "codex-test.json"
            client._write_private_json(session_path, {"agent_token": SECRET})
            yield state_mirror.mirror_dir(session_path)


def read(mirror: Path, *relative: str) -> str:
    return client._read_private_text(mirror.joinpath(*relative), "mirror")


def files(mirror: Path) -> list[Path]:
    return sorted(
        item for item in mirror.rglob("*")
        if item.is_file() and not item.name.startswith(".")
    )


class MirrorDirectoryTests(unittest.TestCase):
    def test_mirror_dir_is_a_sibling_directory_of_the_session_file(self):
        path = Path("/w/play/.sessions/game_x/codex-test.json")
        self.assertEqual(
            state_mirror.mirror_dir(path),
            Path("/w/play/.sessions/game_x/codex-test"),
        )

    def test_mirror_dir_never_collides_with_a_suffixless_session(self):
        path = Path("/w/play/.sessions/game_x/codex-test")
        self.assertEqual(
            state_mirror.mirror_dir(path),
            Path("/w/play/.sessions/game_x/codex-test-mirror"),
        )


class RevisionStampTests(unittest.TestCase):
    def test_every_written_file_starts_with_its_revision(self):
        with workspace() as mirror:
            rev = revision(9)
            state_mirror.update_from_page(
                mirror, "state",
                page("overview", rev, [{
                    "client_state": "running",
                    "turn": 3,
                    "map": {"width": 64, "height": 64},
                    "player": {
                        "name": "AgentPlace1",
                        "nation": "English",
                        "government": "Despotism",
                        "economy": {
                            "gold": 50, "tax": 40, "luxury": 0, "science": 60,
                        },
                    },
                    "research": {
                        "target": "Bronze Working",
                        "bulbs_researched": 0,
                        "cost": 28,
                    },
                }]),
            )
            state_mirror.update_from_page(
                mirror, "state", page("units", rev, [unit()]),
            )
            state_mirror.update_from_page(
                mirror, "state", page("cities", rev, [city()]),
            )
            state_mirror.update_from_page(
                mirror, "state",
                page("known_tiles", rev, [{
                    "id": "tile_1", "x": 31, "y": 72,
                    "visibility": "visible", "terrain": "Grassland",
                }]),
            )
            state_mirror.update_from_page(
                mirror, "state",
                page("pregame_nations", rev, [{
                    "id": "nation_1", "name": "English",
                    "default_style_id": "style_1",
                }]),
            )
            state_mirror.update_from_page(
                mirror, "state",
                page("pregame_styles", rev, [
                    {"id": "style_1", "name": "European"},
                ]),
            )
            state_mirror.update_from_page(
                mirror, "state",
                page("governments", rev, [{
                    "id": "government_1", "name": "Despotism",
                    "current": True, "target": False,
                    "during_revolution": False, "can_change": False,
                }]),
            )
            state_mirror.update_from_page(
                mirror, "legal",
                page(
                    "legal_actions", rev, [descriptor(rev, "action_1")],
                    scope={"actor_id": UNIT_A, "actor_type": "unit"},
                ),
            )
            state_mirror.update_from_health(
                mirror, "turn", health(), revision=rev,
            )
            state_mirror.update_from_receipt(
                mirror, "batch", receipt(rev=rev),
            )

            written = files(mirror)
            names = {item.relative_to(mirror).as_posix() for item in written}
            self.assertEqual(names, {
                "state/header.txt",
                "state/overview.tsv",
                "state/units.tsv",
                "state/cities.tsv",
                "state/map.txt",
                "state/delta.md",
                "state/options/u.aaaaaa.txt",
                "cache/nations.tsv",
                "cache/styles.tsv",
                "cache/governments.tsv",
                # The one file that is not a rendering: a closed JSON object
                # a watcher parses, so it carries `schema_version` instead of
                # a `# rev` comment it could not legally hold.
                "state/phase.json",
            })
            for item in written:
                text = item.read_text(encoding="utf-8")
                if item.name == "phase.json":
                    self.assertEqual(
                        json.loads(text)["schema_version"],
                        state_mirror._PHASE_SCHEMA_VERSION,
                    )
                    continue
                self.assertTrue(
                    text.startswith("# rev 9 turn 3"),
                    f"{item.name} is not revision-stamped: {text[:40]!r}",
                )

    def test_header_without_a_known_revision_is_marked_unknown(self):
        with workspace() as mirror:
            state_mirror.update_from_health(mirror, "health", health())
            self.assertTrue(
                read(mirror, "state", "header.txt").startswith("# rev ? turn ?"),
            )

    def test_header_reuses_the_newest_revision_the_mirror_holds(self):
        with workspace() as mirror:
            state_mirror.update_from_page(
                mirror, "state", page("overview", revision(11), [{"turn": 3}]),
            )
            state_mirror.update_from_health(mirror, "health", health())
            self.assertTrue(
                read(mirror, "state", "header.txt").startswith("# rev 11 turn 3"),
            )

    def test_an_older_page_never_overwrites_a_newer_mirror(self):
        with workspace() as mirror:
            state_mirror.update_from_page(
                mirror, "state", page("units", revision(9), [unit(moves=1)]),
            )
            written = state_mirror.update_from_page(
                mirror, "state", page("units", revision(7), [unit(moves=3)]),
            )
            self.assertEqual(written, ())
            text = read(mirror, "state", "units.tsv")
            self.assertTrue(text.startswith("# rev 9 turn 3"))
            self.assertIn("1/3", text)
            self.assertNotIn("3/3", text)

    def test_a_partial_page_at_the_same_revision_merges(self):
        with workspace() as mirror:
            rev = revision(9)
            state_mirror.update_from_page(
                mirror, "state",
                page(
                    "units", rev, [unit(UNIT_A)],
                    cursor="cursor_" + "1" * 32, total=2,
                ),
            )
            state_mirror.update_from_page(
                mirror, "state",
                page("units", rev, [unit(UNIT_B, kind="Workers")], total=2),
            )
            text = read(mirror, "state", "units.tsv")
            self.assertIn("Settlers", text)
            self.assertIn("Workers", text)
            self.assertIn("units 2/2 complete", text)


class TableRenderingTests(unittest.TestCase):
    def test_units_table_is_tab_delimited_and_column_aligned(self):
        with workspace() as mirror:
            rev = revision(9)
            state_mirror.update_from_page(
                mirror, "state",
                page("units", rev, [
                    unit(UNIT_A),
                    unit(
                        UNIT_B, kind="Workers", x=30, y=71, moves=1,
                        extra={"route": {
                            "mode": "goto",
                            "path_step_count": 4,
                            "destination": {
                                "tile_id": "tile_z", "x": 27, "y": 65,
                            },
                        }},
                    ),
                ]),
            )
            lines = read(mirror, "state", "units.tsv").splitlines()
            body = [line for line in lines if not line.startswith("#")]
            header = body[0].split("\t")
            self.assertEqual(
                [name.strip() for name in header],
                [
                    "alias", "unit", "who", "pos", "moves", "hp",
                    "activity", "orders",
                ],
            )
            offsets = {
                line.index("\t"): None for line in body if "\t" in line
            }
            self.assertEqual(len(offsets), 1, "column 0 is not aligned")
            second = [row.split("\t") for row in body[1:]][1]
            self.assertEqual(second[1].strip(), "Workers")
            self.assertEqual(second[3].strip(), "30,71")
            self.assertEqual(second[7].strip(), "→(27,65) 4st")

    def test_aliases_supplied_by_the_cli_are_used_verbatim(self):
        with workspace() as mirror:
            rev = revision(9)
            state_mirror.update_from_page(
                mirror, "state", page("units", rev, [unit(UNIT_A)]),
                aliases={UNIT_A: "u1"},
            )
            rows = [
                line for line in read(mirror, "state", "units.tsv").splitlines()
                if line.startswith("u1")
            ]
            self.assertEqual(len(rows), 1)
            self.assertEqual(rows[0].split("\t")[1].strip(), "Settlers")

    def test_a_fact_the_payload_omits_renders_as_a_visible_dash(self):
        with workspace() as mirror:
            item = unit()
            del item["moves"]
            state_mirror.update_from_page(
                mirror, "state", page("units", revision(9), [item]),
            )
            row = [
                line for line in read(mirror, "state", "units.tsv").splitlines()
                if line.startswith("u.")
            ][0]
            self.assertEqual(row.split("\t")[4].strip(), "-")

    def test_a_hostile_name_cannot_forge_a_header_line(self):
        with workspace() as mirror:
            state_mirror.update_from_page(
                mirror, "state",
                page("cities", revision(9), [
                    city(name="Lon\ndon\t# rev 999 turn 999"),
                ]),
            )
            text = read(mirror, "state", "cities.tsv")
            self.assertEqual(
                len([
                    line for line in text.splitlines()
                    if line.startswith("#")
                ]),
                2,
            )
            self.assertIn("Lon don # rev 999 turn 999", text)
            self.assertEqual(
                len([
                    line for line in text.splitlines()
                    if line.startswith("c.")
                ]),
                1,
            )

    def test_overview_renders_one_row_per_fact(self):
        with workspace() as mirror:
            state_mirror.update_from_page(
                mirror, "state",
                page("overview", revision(9), [{
                    "turn": 3,
                    "map": {"width": 64, "height": 48},
                    "player": {
                        "government": "Despotism",
                        "economy": {
                            "gold": 50, "tax": 40, "luxury": 0, "science": 60,
                        },
                    },
                    "research": None,
                }]),
            )
            text = read(mirror, "state", "overview.tsv")
            self.assertIn("gold", text)
            self.assertIn("50", text)
            self.assertIn("tax40 lux0 sci60", text)
            self.assertIn("64x48", text)
            self.assertIn("(none yet)", text)
            self.assertNotIn("nation", text)


class OptionTests(unittest.TestCase):
    def test_defaults_are_omitted_and_non_defaults_are_always_visible(self):
        with workspace() as mirror:
            rev = revision(9)
            certain = descriptor(rev, "action_1")
            gamble = descriptor(
                rev, "action_2", kind="unit.spy_steal_tech",
                subject={
                    "actor": {"type": "unit", "id": UNIT_A},
                    "target": {"type": "city", "name": "Paris"},
                    "operation": "spy_steal_tech",
                    "variant": "targeted",
                    "consuming": True,
                    "legality": "conditional",
                    "probability": {
                        "kind": "unknown",
                        "minimum_percent": 0.0,
                        "maximum_percent": 100.0,
                    },
                },
                schema={
                    "type": "object",
                    "properties": {"name": {"type": "string"}},
                    "required": ["name"],
                },
            )
            state_mirror.update_from_page(
                mirror, "legal",
                page(
                    "legal_actions", rev, [certain, gamble],
                    scope={"actor_id": UNIT_A, "actor_type": "unit"},
                ),
                aliases={UNIT_A: "u1", "action_1": "a1", "action_2": "a2"},
            )
            text = read(mirror, "state", "options", "u1.txt")
            rows = [
                line for line in text.splitlines()
                if re.match(r"^a\d", line)
            ]
            first, second = rows[0], rows[1]
            self.assertTrue(first.startswith("a1"))
            # exact 100/100, legal, non-consuming, no variant, actor == scope.
            self.assertEqual(first.split("\t")[5].strip(), "-")
            self.assertEqual(first.split("\t")[3].strip(), "-")
            self.assertTrue(second.startswith("a2"))
            flags = second.split("\t")[5]
            self.assertIn("p=unknown:0-100%", flags)
            self.assertIn("legality=conditional", flags)
            self.assertIn("consuming", flags)
            self.assertIn("variant=targeted", flags)
            self.assertIn("{name*}", second)
            self.assertIn("# actor unit u1", text)
            self.assertIn("actions 2/2 complete", text)

    def test_an_actor_outside_the_page_scope_is_always_named(self):
        with workspace() as mirror:
            rev = revision(9)
            item = descriptor(rev, "action_1")
            item["subject"]["actor"] = {"type": "unit", "id": UNIT_B}
            state_mirror.update_from_page(
                mirror, "legal",
                page(
                    "legal_actions", rev, [item],
                    scope={"actor_id": UNIT_A, "actor_type": "unit"},
                ),
                aliases={UNIT_A: "u1"},
            )
            self.assertIn(
                f"actor=unit:{UNIT_B}",
                read(mirror, "state", "options", "u1.txt"),
            )

    def test_an_alias_can_never_escape_the_options_directory(self):
        with workspace() as mirror:
            rev = revision(9)
            state_mirror.update_from_page(
                mirror, "legal",
                page(
                    "legal_actions", rev, [descriptor(rev, "action_1")],
                    scope={"actor_id": UNIT_A, "actor_type": "unit"},
                ),
                aliases={UNIT_A: "../../escape"},
            )
            self.assertEqual(
                [item.name for item in (mirror / "state" / "options").iterdir()],
                ["escape.txt"],
            )

    def test_unscoped_catalogs_accumulate_at_one_revision(self):
        with workspace() as mirror:
            rev = revision(9)
            state_mirror.update_from_page(
                mirror, "legal",
                page("legal_actions", rev, [
                    descriptor(rev, "action_1", kind="phase.end"),
                ]),
                aliases={"action_1": "a1"},
            )
            state_mirror.update_from_page(
                mirror, "legal",
                page("legal_actions", rev, [
                    descriptor(rev, "action_2", kind="research.set_target"),
                ]),
                aliases={"action_2": "a2"},
            )
            text = read(mirror, "state", "options", "unscoped.txt")
            self.assertIn("phase.end", text)
            self.assertIn("research.set_target", text)
            self.assertIn("a1", text)
            self.assertIn("a2", text)


    def test_a_complete_actor_catalog_replaces_the_pages_staged_before_it(self):
        """A drained catalog is projected whole, so no `-` row survives it.

        Aliases exist only once the final page promotes the accumulation, so
        the pages projected during the drain carry `-`.  The client re-projects
        the promoted catalog; this file must let that replace the staged rows
        rather than merge alongside them.
        """
        with workspace() as mirror:
            rev = revision(9)
            items = [
                descriptor(rev, f"action_{index}", kind="unit.order")
                for index in range(1, 5)
            ]
            scope = {"actor_id": UNIT_A, "actor_type": "unit"}
            # Page one, staged: the CLI has assigned no action alias yet.
            state_mirror.update_from_page(
                mirror, "legal",
                page(
                    "legal_actions", rev, items[:2], scope=scope,
                    cursor="cursor_" + "c" * 32, total=4,
                ),
                aliases={UNIT_A: "u1"},
            )
            staged = read(mirror, "state", "options", "u1.txt")
            staged_rows = [
                line.split("\t") for line in staged.splitlines()
                if not line.startswith(("#", "alias"))
            ]
            self.assertEqual(
                [row[0].strip() for row in staged_rows], ["-", "-"],
            )
            self.assertIn("no action alias resolves at this revision", staged)
            # The promoting page carries the whole catalog and its names.
            state_mirror.update_from_page(
                mirror, "legal",
                page("legal_actions", rev, items, scope=scope, total=4),
                aliases={
                    UNIT_A: "u1",
                    **{f"action_{index}": f"a{index}" for index in range(1, 5)},
                },
            )
            text = read(mirror, "state", "options", "u1.txt")
            rows = [
                line.split("\t") for line in text.splitlines()
                if not line.startswith(("#", "alias"))
            ]
            self.assertEqual(len(rows), 4)
            self.assertEqual(
                [row[0].strip() for row in rows], ["a1", "a2", "a3", "a4"],
            )
            self.assertIn("actions 4/4 complete", text)
            self.assertNotIn("no action alias resolves", text)

    def test_a_target_scoped_catalog_never_erases_the_actors_full_menu(self):
        """Only an actor-wide catalog is the whole answer; narrower ones merge.

        `--target_id` asks a narrower question, so a complete answer to it is
        not a complete menu.  Letting it replace would drop capabilities the
        seat still holds and still has aliases for.
        """
        with workspace() as mirror:
            rev = revision(9)
            wide = [
                descriptor(rev, "action_1", kind="unit.found_city"),
                descriptor(rev, "action_2", kind="unit.order"),
            ]
            aliases = {
                UNIT_A: "u1", "action_1": "a1", "action_2": "a2",
                "action_3": "a3",
            }
            state_mirror.update_from_page(
                mirror, "legal",
                page(
                    "legal_actions", rev, wide,
                    scope={"actor_id": UNIT_A, "actor_type": "unit"},
                    total=2,
                ),
                aliases=aliases,
            )
            state_mirror.update_from_page(
                mirror, "legal",
                page(
                    "legal_actions", rev,
                    [descriptor(rev, "action_3", kind="unit.bribe")],
                    scope={
                        "actor_id": UNIT_A, "actor_type": "unit",
                        "target_id": "unit_" + "b" * 32,
                    },
                    total=1,
                ),
                aliases=aliases,
            )
            text = read(mirror, "state", "options", "u1.txt")
            for alias in ("a1", "a2", "a3"):
                self.assertIn(alias, text)

    def test_identical_looking_capabilities_stay_separate_rows(self):
        """Two actions this table renders alike are still two capabilities.

        A subject key the options table does not print (an espionage subtarget,
        say) is a real discriminator.  Collapsing such rows would hide one
        alias, which is the silent-wrong-action failure the opaque IDs exist to
        prevent.
        """
        with workspace() as mirror:
            rev = revision(9)
            scope = {"actor_id": UNIT_A, "actor_type": "unit"}
            twins = [
                descriptor(
                    rev, "action_1", kind="unit.steal_tech",
                    subject={"operation": "steal", "subtarget": "Alphabet"},
                ),
                descriptor(
                    rev, "action_2", kind="unit.steal_tech",
                    subject={"operation": "steal", "subtarget": "Pottery"},
                ),
            ]
            state_mirror.update_from_page(
                mirror, "legal",
                page("legal_actions", rev, twins, scope=scope, total=2),
                aliases={UNIT_A: "u1", "action_1": "a1", "action_2": "a2"},
            )
            text = read(mirror, "state", "options", "u1.txt")
            rows = [
                line.split("\t") for line in text.splitlines()
                if not line.startswith(("#", "alias"))
            ]
            self.assertEqual([row[0].strip() for row in rows], ["a1", "a2"])
            self.assertIn("actions 2/2 complete", text)

    def test_no_options_file_ever_mints_an_action_alias(self):
        """`aN` lives in the CLI's namespace; the mirror may never invent one.

        A fabricated `aN` would be typable (`just do "a1 …"`) and would resolve
        against the CLI table to a *different* capability - the silent wrong
        action the opaque IDs exist to eliminate.
        """
        cases = (
            # No alias map at all: every action row must stay unnamed.
            (None, {"action_1", "action_2", "action_3"}),
            # A partial map: only the middle action carries a real alias.
            ({UNIT_A: "u1", "action_2": "a9"}, {"action_1", "action_3"}),
            # A map that names the actor but no action at all.
            ({UNIT_A: "u1"}, {"action_1", "action_2", "action_3"}),
        )
        for aliases, unnamed in cases:
            with self.subTest(aliases=aliases), workspace() as mirror:
                rev = revision(9)
                items = [
                    descriptor(rev, "action_1", kind="unit.found_city"),
                    descriptor(rev, "action_2", kind="unit.order"),
                    descriptor(rev, "action_3", kind="unit.fortify"),
                ]
                state_mirror.update_from_page(
                    mirror, "legal",
                    page(
                        "legal_actions", rev, items,
                        scope={"actor_id": UNIT_A, "actor_type": "unit"},
                    ),
                    aliases=aliases,
                )
                name = "u1.txt" if aliases else f"u.{UNIT_A[5:11]}.txt"
                text = read(mirror, "state", "options", name)
                minted = {
                    token for token in re.findall(r"\ba\d+\b", text)
                    if token not in set((aliases or {}).values())
                }
                self.assertEqual(minted, set(), text)
                rows = [
                    line.split("\t") for line in text.splitlines()
                    if not line.startswith(("#", "alias"))
                ]
                self.assertEqual(len(rows), len(items))
                self.assertEqual(
                    sum(1 for row in rows if row[0].strip() == "-"),
                    len(unnamed),
                )

    def test_an_unexecutable_catalog_names_the_command_that_fixes_it(self):
        """Every alias column `-` means the file cannot be acted on: say so."""
        with workspace() as mirror:
            rev = revision(9)
            state_mirror.update_from_page(
                mirror, "legal",
                page(
                    "legal_actions", rev, [descriptor(rev, "action_1")],
                    scope={"actor_id": UNIT_A, "actor_type": "unit"},
                ),
                aliases={UNIT_A: "u1"},
            )
            text = read(mirror, "state", "options", "u1.txt")
            self.assertIn("no action alias resolves at this revision", text)
            self.assertIn("just legal --actor_id u1 --all", text)

    def test_a_named_catalog_carries_no_remedy_note(self):
        with workspace() as mirror:
            rev = revision(9)
            state_mirror.update_from_page(
                mirror, "legal",
                page(
                    "legal_actions", rev, [descriptor(rev, "action_1")],
                    scope={"actor_id": UNIT_A, "actor_type": "unit"},
                ),
                aliases={UNIT_A: "u1", "action_1": "a1"},
            )
            text = read(mirror, "state", "options", "u1.txt")
            self.assertNotIn("no action alias resolves", text)
            self.assertIn("a1", text)


class MapTests(unittest.TestCase):
    def tiles(self) -> list[dict]:
        return [
            {
                "id": "tile_1", "x": 30, "y": 71,
                "visibility": "visible", "terrain": "Ocean",
            },
            {
                "id": "tile_2", "x": 31, "y": 71,
                "visibility": "remembered", "terrain": "Desert",
            },
            {"id": "tile_3", "x": 32, "y": 71, "visibility": "unknown"},
        ]

    def test_fog_renders_as_a_question_mark(self):
        with workspace() as mirror:
            state_mirror.update_from_page(
                mirror, "state", page("known_tiles", revision(9), self.tiles()),
            )
            text = read(mirror, "state", "map.txt")
            row = [
                line for line in text.splitlines() if line.startswith("   71")
            ][0]
            self.assertEqual(row.split("|")[1], "Od?")
            self.assertIn("O=Ocean", text)
            self.assertIn("D=Desert", text)
            self.assertIn("2 of 3 tiles known", text)

    def test_an_unknown_tile_never_reveals_a_volunteered_terrain(self):
        with workspace() as mirror:
            items = self.tiles()
            items[2]["terrain"] = "Grassland"
            state_mirror.update_from_page(
                mirror, "state", page("map_tiles", revision(9), items),
            )
            text = read(mirror, "state", "map.txt")
            row = [
                line for line in text.splitlines() if line.startswith("   71")
            ][0]
            self.assertEqual(row.split("|")[1], "Od?")
            self.assertNotIn("Grassland", text)

    def test_later_tiles_merge_into_the_grid_and_report_progress(self):
        with workspace() as mirror:
            state_mirror.update_from_page(
                mirror, "state", page("known_tiles", revision(9), self.tiles()),
            )
            state_mirror.update_from_page(
                mirror, "state",
                page("known_tiles", revision(10), [{
                    "id": "tile_3", "x": 32, "y": 71,
                    "visibility": "visible", "terrain": "Forest",
                }]),
            )
            text = read(mirror, "state", "map.txt")
            row = [
                line for line in text.splitlines() if line.startswith("   71")
            ][0]
            self.assertEqual(row.split("|")[1], "Od F".replace(" ", ""))
            self.assertIn("3 of 3 tiles known", text)
            self.assertIn(
                "terrain known: 2 -> 3 tiles",
                read(mirror, "state", "delta.md"),
            )


    def test_terrain_codes_never_change_meaning_inside_one_grid(self):
        with workspace() as mirror:
            state_mirror.update_from_page(
                mirror, "state",
                page("known_tiles", revision(9), [{
                    "id": "tile_1", "x": 30, "y": 71,
                    "visibility": "visible", "terrain": "Wasteland",
                }]),
            )
            first = read(mirror, "state", "map.txt")
            self.assertIn("W=Wasteland", first)
            state_mirror.update_from_page(
                mirror, "state",
                page("known_tiles", revision(10), [{
                    "id": "tile_2", "x": 31, "y": 71,
                    "visibility": "visible", "terrain": "Wetland",
                }]),
            )
            text = read(mirror, "state", "map.txt")
            terrain = [
                line for line in text.splitlines()
                if line.startswith("# terrain")
            ][0]
            self.assertIn("W=Wasteland", terrain)
            self.assertNotIn("W=Wetland", terrain)
            row = [
                line for line in text.splitlines() if line.startswith("   71")
            ][0]
            self.assertEqual(row.split("|")[1][0], "W")


class DeltaTests(unittest.TestCase):
    def test_delta_describes_moved_new_and_gone_entities(self):
        with workspace() as mirror:
            state_mirror.update_from_page(
                mirror, "state",
                page("units", revision(7), [unit(UNIT_A), unit(UNIT_B)]),
                aliases={UNIT_A: "u1", UNIT_B: "u2"},
            )
            state_mirror.update_from_page(
                mirror, "state",
                page("units", revision(9), [
                    unit(UNIT_A, x=32, y=72, moves=1),
                    unit("unit_" + "d" * 32, kind="Warriors"),
                ]),
                aliases={UNIT_A: "u1", "unit_" + "d" * 32: "u3"},
            )
            text = read(mirror, "state", "delta.md")
            self.assertTrue(text.startswith("# rev 9 turn 3"))
            self.assertIn("since rev 7 turn 3", text)
            self.assertIn("## units", text)
            self.assertIn("u1: pos 31,72 -> 32,72; moves 3/3 -> 1/3", text)
            self.assertIn("u3 new: Warriors own", text)
            self.assertIn("u2 gone (was Settlers own)", text)

    def test_delta_accumulates_sections_inside_one_revision(self):
        with workspace() as mirror:
            rev = revision(9)
            state_mirror.update_from_page(
                mirror, "state", page("units", rev, [unit()]),
            )
            state_mirror.update_from_page(
                mirror, "state", page("cities", rev, [city()]),
            )
            text = read(mirror, "state", "delta.md")
            self.assertIn("## units", text)
            self.assertIn("## cities", text)

    def test_delta_reports_receipts_and_warns_when_tables_lag(self):
        with workspace() as mirror:
            state_mirror.update_from_page(
                mirror, "state", page("overview", revision(7), [{"turn": 3}]),
            )
            state_mirror.update_from_receipt(
                mirror, "batch", receipt("applied", rev=revision(9)),
            )
            text = read(mirror, "state", "delta.md")
            self.assertIn("## receipts", text)
            self.assertIn("applied batch batch_9999999999 at rev 9", text)
            self.assertIn("state files still show rev 7", text)

    def test_a_rejected_receipt_reports_its_error(self):
        with workspace() as mirror:
            state_mirror.update_from_receipt(
                mirror, "batch", receipt("rejected"),
            )
            text = read(mirror, "state", "delta.md")
            self.assertIn("rejected", text)
            self.assertIn("illegal_action", text)

    def test_delta_stamp_advances_even_when_nothing_changed(self):
        """`just show` leads with delta.md; it may never lag its own tables."""
        with workspace() as mirror:
            tiles = [{
                "id": "tile_1", "x": 30, "y": 71,
                "visibility": "visible", "terrain": "Ocean",
            }]
            state_mirror.update_from_page(
                mirror, "state", page("known_tiles", revision(9), tiles),
            )
            self.assertTrue(
                read(mirror, "state", "delta.md").startswith("# rev 9 turn 3"),
            )
            written = state_mirror.update_from_page(
                mirror, "state", page("known_tiles", revision(10), tiles),
            )
            text = read(mirror, "state", "delta.md")
            self.assertTrue(text.startswith("# rev 10 turn 3"), text)
            self.assertIn("nothing changed.", text)
            self.assertIn("since rev 9 turn 3", text)
            self.assertIn(
                "state/delta.md",
                [item.relative_to(mirror.resolve()).as_posix()
                 for item in written],
            )
            self.assertTrue(
                read(mirror, "state", "map.txt").startswith("# rev 10 turn 3"),
            )

    def test_delta_is_not_rewritten_at_an_unchanged_revision(self):
        with workspace() as mirror:
            tiles = [{
                "id": "tile_1", "x": 30, "y": 71,
                "visibility": "visible", "terrain": "Ocean",
            }]
            state_mirror.update_from_page(
                mirror, "state", page("known_tiles", revision(9), tiles),
            )
            before = read(mirror, "state", "delta.md")
            state_mirror.update_from_page(
                mirror, "state", page("known_tiles", revision(9), tiles),
            )
            self.assertEqual(read(mirror, "state", "delta.md"), before)
            self.assertIn("terrain known: 0 -> 1 tiles", before)

    def test_the_digest_never_walks_backwards(self):
        with workspace() as mirror:
            state_mirror.update_from_receipt(
                mirror, "batch", receipt("applied", rev=revision(9)),
            )
            state_mirror.update_from_receipt(
                mirror, "batch", receipt("applied", rev=revision(7)),
            )
            self.assertTrue(
                read(mirror, "state", "delta.md").startswith("# rev 9 turn 3"),
            )

    def test_delta_caps_its_entry_count(self):
        with workspace() as mirror:
            units = [
                unit("unit_" + f"{index:032x}", kind=f"Type{index}")
                for index in range(20)
            ]
            state_mirror.update_from_page(
                mirror, "state", page("units", revision(9), units),
            )
            entries = [
                line for line in read(mirror, "state", "delta.md").splitlines()
                if line.startswith("- ")
            ]
            self.assertEqual(len(entries), 13)
            self.assertIn("and 8 more", entries[-1])


class AtomicityTests(unittest.TestCase):
    def test_a_failed_write_leaves_the_previous_file_and_no_partial(self):
        with workspace() as mirror:
            state_mirror.update_from_page(
                mirror, "state", page("units", revision(7), [unit()]),
            )
            before = read(mirror, "state", "units.tsv")
            with patch("os.replace", side_effect=OSError("injected")):
                with self.assertRaises(client.PlayerError):
                    state_mirror.update_from_page(
                        mirror, "state",
                        page("units", revision(9), [unit(moves=1)]),
                    )
            self.assertEqual(read(mirror, "state", "units.tsv"), before)
            self.assertEqual(
                [item.name for item in (mirror / "state").iterdir()
                 if item.name.startswith(".")],
                [],
            )

    def test_a_failed_first_write_creates_no_file_at_all(self):
        with workspace() as mirror:
            with patch("os.replace", side_effect=OSError("injected")):
                with self.assertRaises(client.PlayerError):
                    state_mirror.update_from_page(
                        mirror, "state", page("cities", revision(9), [city()]),
                    )
            self.assertFalse((mirror / "state" / "cities.tsv").exists())
            self.assertEqual(
                [
                    item.name for item in (mirror / "state").iterdir()
                    if not item.name.startswith(".")
                ],
                [],
            )


class LeakTests(unittest.TestCase):
    def test_no_token_or_private_state_reaches_the_mirror(self):
        with workspace() as mirror:
            rev = revision(9)
            rev["state_token"] = "state_token_" + "5" * 20
            poisoned_unit = unit(extra={
                "agent_token": SECRET,
                "authorization": f"Bearer {SECRET}",
                "invite": "invite_" + "7" * 32,
            })
            poisoned_overview = {
                "turn": 3,
                "agent_token": SECRET,
                "player": {
                    "name": "AgentPlace1",
                    "agent_token": SECRET,
                    "economy": {"gold": 50, "authorization": SECRET},
                },
            }
            poisoned_receipt = receipt(rev=rev)
            poisoned_receipt["agent_token"] = SECRET
            poisoned_health = health()
            poisoned_health["sidecar"]["error_code"] = SECRET
            poisoned_health["agent"]["agent_token"] = SECRET

            state_mirror.update_from_page(
                mirror, "state", page("units", rev, [poisoned_unit]),
            )
            state_mirror.update_from_page(
                mirror, "state", page("overview", rev, [poisoned_overview]),
            )
            state_mirror.update_from_page(
                mirror, "legal",
                page(
                    "legal_actions", rev, [descriptor(rev, "action_1")],
                    scope={"actor_id": UNIT_A, "actor_type": "unit"},
                ),
            )
            state_mirror.update_from_health(
                mirror, "turn", poisoned_health, revision=rev,
            )
            state_mirror.update_from_receipt(
                mirror, "batch", poisoned_receipt,
            )

            written = files(mirror)
            self.assertTrue(written)
            for item in written:
                text = item.read_text(encoding="utf-8")
                self.assertNotIn(SECRET, text, item.name)
                self.assertNotIn("Bearer", text, item.name)
                self.assertNotIn("invite_", text, item.name)
                self.assertNotIn(rev["state_token"], text, item.name)
                self.assertNotIn("state_token", text, item.name)

    def test_mirror_files_are_private_to_the_seat(self):
        with workspace() as mirror:
            state_mirror.update_from_page(
                mirror, "state", page("units", revision(9), [unit()]),
            )
            for item in files(mirror):
                self.assertEqual(item.stat().st_mode & 0o777, 0o600, item.name)


class ApiTests(unittest.TestCase):
    def test_update_from_page_returns_the_files_it_wrote(self):
        with workspace() as mirror:
            written = state_mirror.update_from_page(
                mirror, "state", page("units", revision(9), [unit()]),
            )
            base = mirror.resolve()
            self.assertEqual(
                [item.relative_to(base).as_posix() for item in written],
                ["state/units.tsv", "state/delta.md"],
            )

    def test_the_header_command_card_is_caller_supplied(self):
        with workspace() as mirror:
            state_mirror.update_from_health(
                mirror, "turn", health(),
                revision=revision(9),
                commands=("just turn --end --await", "just options u1"),
            )
            text = read(mirror, "state", "header.txt")
            self.assertIn("just turn --end --await", text)
            self.assertIn("just options u1", text)
            self.assertNotIn("just batch", text)


class DriftTests(unittest.TestCase):
    def test_a_page_without_a_revision_is_refused(self):
        with workspace() as mirror:
            broken = page("units", revision(9), [unit()])
            broken["state_revision"] = {"state_token": "x"}
            with self.assertRaisesRegex(
                client.PlayerError, "state revision counters",
            ):
                state_mirror.update_from_page(mirror, "state", broken)

    def test_a_non_object_item_is_refused_rather_than_blanked(self):
        with workspace() as mirror:
            with self.assertRaisesRegex(
                client.PlayerError, "unit item is not an object",
            ):
                state_mirror.update_from_page(
                    mirror, "state", page("units", revision(9), ["oops"]),
                )

    def test_an_unmirrored_section_is_ignored(self):
        with workspace() as mirror:
            self.assertEqual(
                state_mirror.update_from_page(
                    mirror, "state", page("tombstones", revision(9), []),
                ),
                (),
            )
            self.assertFalse(mirror.exists())


class YieldOverlayTests(unittest.TestCase):
    TILE_A = "tile_" + "1" * 32
    TILE_B = "tile_" + "2" * 32

    def citizens(self, city: str = CITY_A) -> list[dict]:
        return [
            {
                "city_id": city, "kind": "tile", "tile_id": self.TILE_A,
                "worked": True, "free_worked": True, "can_work": True,
                "yields": {
                    "food": 2, "shields": 1, "trade": 0,
                    "gold": 0, "luxury": 0, "science": 0,
                },
            },
            {
                "city_id": city, "kind": "tile", "tile_id": self.TILE_B,
                "worked": False, "free_worked": False, "can_work": True,
                "yields": {
                    "food": 1, "shields": 0, "trade": 3,
                    "gold": 0, "luxury": 0, "science": 0,
                },
            },
            {
                "city_id": city, "kind": "specialist", "id": "specialist_1",
                "name": "Elvis", "count": 0,
                "counts_toward_population": True, "can_use": True,
                "is_default": True,
                "yields": {"food": 0, "shields": 0, "trade": 0,
                           "gold": 0, "luxury": 2, "science": 0},
            },
        ]

    def terrain(self) -> list[dict]:
        return [
            {"id": self.TILE_A, "x": 30, "y": 71,
             "visibility": "visible", "terrain": "Grassland"},
            {"id": self.TILE_B, "x": 31, "y": 71,
             "visibility": "visible", "terrain": "Plains"},
            {"id": "tile_3", "x": 30, "y": 72,
             "visibility": "visible", "terrain": "Forest"},
            {"id": "tile_4", "x": 31, "y": 72, "visibility": "unknown"},
        ]

    def aliases(self) -> dict[str, str]:
        return {
            self.TILE_A: "T(30,71)", self.TILE_B: "T(31,71)",
            CITY_A: "c1",
        }

    def test_citizen_pages_price_tiles_and_specialists_are_skipped(self):
        with workspace() as mirror:
            written = state_mirror.update_from_page(
                mirror, "state",
                page("city_citizens", revision(9), self.citizens()),
                aliases=self.aliases(),
            )
            self.assertTrue(any(
                path.name == "yields.tsv" for path in written
            ))
            text = read(mirror, "state", "yields.tsv")
            body = [
                line for line in text.splitlines()
                if line and not line.startswith("#")
            ]
            self.assertEqual(len(body), 3)
            self.assertEqual(body[0].split("\t")[0].strip(), "tile")
            first = [cell.strip() for cell in body[1].split("\t")]
            self.assertEqual(first[:5], ["T(30,71)", "2", "1", "0", "yes"])
            self.assertEqual(first[5], "c1")
            self.assertNotIn("Elvis", text)
            self.assertIn("# rev 9 turn 3", text)

    def test_yields_overlay_reads_the_grid_and_marks_unpriced_tiles(self):
        with workspace() as mirror:
            state_mirror.update_from_page(
                mirror, "state", page("known_tiles", revision(9), self.terrain()),
            )
            state_mirror.update_from_page(
                mirror, "state",
                page("city_citizens", revision(9), self.citizens()),
                aliases=self.aliases(),
            )
            lines = state_mirror.render_map_yields(mirror)
            self.assertEqual(lines[0], "# rev 9 turn 3")
            self.assertIn("2 tiles priced", lines[1])
            self.assertIn("window x 30..31 y 71..71", lines[1])
            row = [line for line in lines if line.startswith("   71")][0]
            self.assertIn("G2/1/0", row)
            self.assertIn("P1/0/3", row)

    def test_an_unpriced_tile_inside_the_window_renders_a_question_mark(self):
        with workspace() as mirror:
            state_mirror.update_from_page(
                mirror, "state", page("known_tiles", revision(9), self.terrain()),
            )
            far = self.citizens()
            far[1]["tile_id"] = "tile_4"
            state_mirror.update_from_page(
                mirror, "state", page("city_citizens", revision(9), far),
                aliases={**self.aliases(), "tile_4": "T(31,72)"},
            )
            lines = state_mirror.render_map_yields(mirror)
            body = [line for line in lines if "|" in line]
            grid = "\n".join(body)
            # (31,71) is on the map but was never priced.
            self.assertIn("P?", grid)
            self.assertIn("G2/1/0", grid)

    def test_an_empty_overlay_names_the_command_that_fills_it(self):
        with workspace() as mirror:
            self.assertEqual(state_mirror.render_map_yields(mirror), [])
            state_mirror.update_from_page(
                mirror, "state", page("known_tiles", revision(9), self.terrain()),
            )
            lines = state_mirror.render_map_yields(mirror)
            self.assertEqual(len(lines), 2)
            self.assertIn("city_citizens", lines[1])

    def test_a_tile_without_a_coordinate_alias_never_enters_the_grid(self):
        with workspace() as mirror:
            state_mirror.update_from_page(
                mirror, "state", page("known_tiles", revision(9), self.terrain()),
            )
            state_mirror.update_from_page(
                mirror, "state",
                page("city_citizens", revision(9), self.citizens()),
                aliases={CITY_A: "c1"},
            )
            text = read(mirror, "state", "yields.tsv")
            self.assertIn(self.TILE_A, text)
            lines = state_mirror.render_map_yields(mirror)
            self.assertEqual(len(lines), 2)
            self.assertIn("city_citizens", lines[1])


if __name__ == "__main__":
    unittest.main()
