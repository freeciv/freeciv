from __future__ import annotations

import gzip
import json
import os
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from agent_eval import save_replay
from agent_eval.save_replay import (
    SaveReplayError,
    board_from_autosave,
    replay_from_autosaves,
)


GAME_ID = "game_abcdefghijklmnop"


def classic_vector() -> list[str]:
    return ["A_NONE", *save_replay._classic_requirements()]


def bits(vector: list[str], *known: str) -> str:
    selected = {"A_NONE", *known}
    return "".join("1" if name in selected else "0" for name in vector)


def player_sections(
    player_id: int,
    *,
    name: str,
    color: tuple[int, int, int],
    nation: str,
    alive: bool,
    gold: int,
    cities: int,
    units: int,
    score: int,
    culture: int,
    citizens: tuple[int, int, int, int, int, int],
) -> str:
    happy, content, unhappy, angry, scientists, taxmen = citizens
    return f"""
[player{player_id}]
name="{name}"
color.r={color[0]}
color.g={color[1]}
color.b={color[2]}
nation="{nation}"
team_no={player_id}
government_name="Despotism"
is_alive={'TRUE' if alive else 'FALSE'}
gold={gold}
ncities={cities}
nunits={units}

[score{player_id}]
happy={happy}
content={content}
unhappy={unhappy}
angry={angry}
specialists0={scientists}
specialists1={taxmen}
culture={culture}
total={score}
"""


def save_text(
    turn: int,
    *,
    agent_known: tuple[str, ...] = ("Alphabet",),
    agent_score: int = 12,
    revision: str = "3.3.90.13-dev",
) -> str:
    vector = classic_vector()
    vector_value = ",".join(json.dumps(name) for name in vector)
    research_header = (
        'r={"number","goal_name","futuretech","bulbs_before",'
        '"saved_name","bulbs","now_name","free_bulbs","done"\n'
    )
    research_rows = [
        f'0,"A_UNSET",0,0,"",{turn * 3},"Industrialization",0,"{bits(vector, *agent_known)}"',
        f'1,"A_UNSET",0,0,"",{turn * 2},"Alphabet",0,"{bits(vector, "Alphabet")}"',
        f'2,"A_UNSET",0,0,"",0,"A_UNSET",0,"{bits(vector)}"',
    ]
    return (
        "[savefile]\n"
        'options=" +version3"\n'
        "version=80\n"
        f'revision="{revision}"\n'
        'rulesetdir="classic"\n'
        f"technology_size={len(vector)}\n"
        f"technology_vector={vector_value}\n\n"
        "[game]\n"
        f"turn={turn}\n"
        f"year={-4000 + turn * 50}\n\n"
        "[settings]\n"
        'set={"name","value","gamestart","gamesetdef"\n'
        '"team_pooled_research",TRUE,TRUE,"Changed"\n}\n\n'
        "[players]\n"
        "nplayers=3\n"
        + player_sections(
            0, name="AgentPlace1", color=(0, 103, 165), nation="Japanese",
            alive=True, gold=40 + turn, cities=1, units=3,
            score=agent_score, culture=5,
            citizens=(1, 2, 3, 4, 5, 6),
        )
        + player_sections(
            1, name="NativePlace2", color=(243, 132, 0), nation="Zulu",
            alive=True, gold=30, cities=2, units=4, score=10,
            culture=2, citizens=(0, 7, 0, 0, 1, 0),
        )
        + player_sections(
            2, name=r'Blackbeard', color=(255, 20, 147), nation="Pirate",
            alive=False, gold=100, cities=0, units=0, score=0,
            culture=0, citizens=(0, 0, 0, 0, 0, 0),
        )
        + "\n[research]\n"
        + research_header
        + "\n".join(research_rows)
        + "\n}\ncount=3\n\n[history]\n"
        + f"turn={turn - 1}\n"
    )


def board_save_text(turn: int) -> str:
    text = save_text(turn)
    text = text.replace(
        "\n\n[game]\n",
        '\nextras_size=5\n'
        'extras_vector="Irrigation","Road","River","Gold","Fish"\n'
        'terrident={"name","identifier"\n'
        '"Ocean"," "\n'
        '"Desert","d"\n'
        '"Grassland","g"\n'
        '}\n\n[game]\n',
        1,
    )
    text = text.replace(
        '"team_pooled_research",TRUE,TRUE,"Changed"\n}\n\n',
        '"team_pooled_research",TRUE,TRUE,"Changed"\n'
        '"topology","ISO|HEX","ISO|HEX","Internal"\n'
        '"wrap","WRAPX","WRAPX","Internal"\n'
        '"xsize",3,3,"Internal"\n'
        '"ysize",2,2,"Internal"\n}\n\n',
        1,
    )
    text = text.replace(
        "ncities=1\nnunits=3\n",
        'ncities=1\nnunits=3\n'
        'c={"y","x","id","size","capital","name"\n'
        '1,2,44,7,"Primary","Test City"\n}\n'
        'u={"x","y","type_by_name","orders_list","action_decision"\n'
        '2,1,"Warrior","private-order","private-action"\n'
        '2,1,"Warrior","private-order","private-action"\n'
        '0,0,"Settlers","private-order","private-action"\n}\n',
        1,
    )
    text = text.replace(
        "ncities=2\nnunits=4\n",
        'ncities=2\nnunits=4\n'
        'c={"y","x","id","size","capital","name"\n'
        '0,1,45,3,"Not","Native One"\n'
        '1,1,46,2,"Not","Native Two"\n}\n'
        'u={"x","y","type_by_name"\n'
        '1,0,"Horsemen"\n1,0,"Horsemen"\n1,0,"Warrior"\n1,1,"Warrior"\n}\n',
        1,
    )
    return text + (
        "\n[map]\n"
        't0000="g d"\n'
        't0001="ddg"\n'
        'alt0000="10,0,20"\n'
        'alt0001="30,40,50"\n'
        'owner0000="0,0,-"\n'
        'owner0001="1,1,0"\n'
        'e00_0000="124"\n'
        'e00_0001="800"\n'
        'e01_0000="100"\n'
        'e01_0001="010"\n'
    )


class SaveReplayTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.runs = self.root / ".agent-eval" / "runs"
        self.saves = self.runs / GAME_ID / "saves"
        self.saves.mkdir(parents=True)
        self.cache = self.root / ".agent-eval" / "replay-cache"
        self.places = [
            {
                "player_name": "AgentPlace1", "seat_id": "seat-1", "place": 1,
                "player_color": "#0067A5", "controller_label": "pi-gpt-test",
                "controller_type": "external", "model": "gpt-test",
            },
            {
                "player_name": "NativePlace2", "seat_id": "seat-2", "place": 2,
                "player_color": "#F38400", "controller_label": "Freeciv Classic AI",
                "controller_type": "native", "model": "classic",
            },
        ]

    def tearDown(self):
        self.temporary.cleanup()

    def write_save(self, turn: int, *, compressed: bool = False, **kwargs) -> Path:
        suffix = ".sav.gz" if compressed else ".sav"
        path = self.saves / f"turn-{turn:04d}-auto{suffix}"
        text = save_text(turn, **kwargs)
        if compressed:
            with gzip.open(path, "wb") as stream:
                stream.write(text.encode("utf-8"))
        else:
            path.write_text(text, encoding="utf-8")
        return path

    def write_board_save(self, turn: int, *, kind: str = "auto") -> Path:
        path = self.saves / f"turn-{turn:04d}-{kind}.sav.gz"
        with gzip.open(path, "wb") as stream:
            stream.write(board_save_text(turn).encode("utf-8"))
        return path

    def replay(self, **kwargs):
        return replay_from_autosaves(
            self.runs, GAME_ID, self.places, cache_root=self.cache, **kwargs,
        )

    def test_plain_and_gzip_reconstruct_players_techs_and_exact_citizens(self):
        self.write_save(1)
        self.write_save(
            2, compressed=True,
            agent_known=("Alphabet", "Railroad", "Industrialization"),
            agent_score=22,
        )
        (self.saves / "turn-0002-M-test.map.ppm").write_text(
            "P3\n"
            '# playerno:0:color:(  1,   2,   3):name:"AgentPlace1"\n'
            '# playerno:2:color:(255,  20, 147):name:"Blackbeard"\n'
            "1 1\n255\n0 0 0\n",
            encoding="utf-8",
        )

        response = self.replay(after_turn=1, limit=1)
        self.assertTrue(response["available"])
        self.assertFalse(response["has_more"])
        self.assertEqual(response["next_after_turn"], 2)
        snapshot = response["snapshots"][0]
        agent, native, pirate = snapshot["players"]
        self.assertEqual(agent["score"], 22)
        self.assertEqual(agent["citizens"], 21)
        self.assertEqual(agent["population"], 21)
        self.assertEqual(agent["player_color"], "#010203")
        self.assertEqual(agent["controller_label"], "pi-gpt-test")
        self.assertEqual(agent["research"]["name"], "Industrialization")
        self.assertEqual(agent["research"]["cost"], 0)
        self.assertEqual(agent["gained_tech_ids"], sorted([
            classic_vector().index("Railroad"),
            classic_vector().index("Industrialization"),
        ]))
        self.assertTrue(agent["scored"])
        self.assertEqual(native["citizens"], 8)
        self.assertFalse(pirate["scored"])
        self.assertEqual(pirate["controller_type"], "dynamic")
        self.assertEqual(pirate["player_color"], "#FF1493")

        technologies = response["catalog"]["technologies"]
        self.assertEqual(len(technologies), 87)
        by_name = {technology["name"]: technology for technology in technologies}
        self.assertIn(
            by_name["Railroad"]["id"], by_name["Industrialization"]["requires"],
        )
        self.assertGreater(
            by_name["Industrialization"]["depth"], by_name["Railroad"]["depth"],
        )

    def test_semantic_board_uses_save_tiles_without_ppm_or_private_unit_fields(self):
        self.write_board_save(7)

        board = board_from_autosave(
            self.runs, GAME_ID, self.places, turn=7, cache_root=self.cache,
        )

        self.assertEqual((board["width"], board["height"]), (3, 2))
        self.assertEqual(board["topology"], "ISO|HEX")
        self.assertEqual(board["wrap"], "WRAPX")
        self.assertEqual(board["terrain_rows"], ["g d", "ddg"])
        self.assertEqual(
            {row["code"]: row["name"] for row in board["terrain_catalog"]}[" "],
            "Ocean",
        )
        self.assertEqual(board["owner_rows"], ["0:2,-:1", "1:2,0:1"])
        self.assertEqual(board["altitude_rows"], ["10,0,20", "30,40,50"])
        self.assertEqual(len(board["extra_layers"]), 2)
        self.assertEqual(board["extra_layers"][1][0], "100")
        self.assertEqual(board["cities"][0]["name"], "Native One")
        self.assertEqual(sum(stack["count"] for stack in board["unit_stacks"]), 7)
        agent_stack = next(
            stack for stack in board["unit_stacks"]
            if stack["x"] == 2 and stack["y"] == 1 and stack["player_id"] == 0
        )
        self.assertEqual(agent_stack["types"], [{"name": "Warrior", "count": 2}])
        serialized = json.dumps(board)
        self.assertNotIn("private-order", serialized)
        self.assertNotIn("private-action", serialized)
        self.assertNotIn(str(self.root), serialized)
        self.assertEqual(list(self.saves.glob("*.ppm")), [])

    def test_semantic_board_requires_an_exact_saved_turn(self):
        self.write_board_save(7)
        with self.assertRaises(FileNotFoundError):
            board_from_autosave(
                self.runs, GAME_ID, self.places, turn=8,
                cache_root=self.cache,
            )

    def test_terminal_final_save_is_available_to_board_and_replay(self):
        self.write_board_save(8, kind="final")
        board = board_from_autosave(
            self.runs, GAME_ID, self.places, turn=8, cache_root=self.cache,
        )
        replay = self.replay(after_turn=7, limit=1, complete=True)
        self.assertEqual(board["turn"], 8)
        self.assertEqual([snapshot["turn"] for snapshot in replay["snapshots"]], [8])

    def test_corrupt_newest_is_skipped_and_prior_stable_snapshot_is_returned(self):
        self.write_save(1, compressed=True)
        corrupt = self.write_save(2, compressed=True)
        corrupt.write_bytes(corrupt.read_bytes()[:-8] + b"/private/secret")
        before = sorted(path.name for path in self.saves.iterdir())

        response = self.replay(after_turn=0, limit=250)

        self.assertEqual([row["turn"] for row in response["snapshots"]], [1])
        self.assertFalse(response["has_more"])
        self.assertEqual(sorted(path.name for path in self.saves.iterdir()), before)
        warning = json.dumps(response["replay_warnings"])
        self.assertIn("incomplete or unreadable", warning)
        self.assertNotIn("/private", warning)

    def test_secfile_escaped_player_name_is_decoded(self):
        source = self.write_save(1)
        source.write_text(
            source.read_text(encoding="utf-8").replace(
                'name="AgentPlace1"', 'name="Agent \\"One\\""', 1,
            ),
            encoding="utf-8",
        )
        places = [{
            **self.places[0],
            "player_name": 'Agent "One"',
        }, self.places[1]]
        response = replay_from_autosaves(
            self.runs, GAME_ID, places, cache_root=self.cache,
            after_turn=0, limit=1,
        )
        self.assertEqual(
            response["snapshots"][0]["players"][0]["player_name"],
            'Agent "One"',
        )

    def test_ruler_renamed_agent_recovers_its_seat_from_the_journal(self):
        # The native save renames agent players to their rulers, so the
        # name join misses exactly the configured seats. The run's live
        # replay journal knows every player's real seat; a renamed agent
        # must come back scored with its own controller, in replay AND
        # board, and a torn journal tail must not break the recovery.
        source = self.write_save(1)
        source.write_text(
            source.read_text(encoding="utf-8").replace(
                'name="AgentPlace1"', 'name="Ada"', 1,
            ),
            encoding="utf-8",
        )
        journal = self.runs / GAME_ID / "replay.jsonl"
        # Older journals recorded player ids as floats (0.0); the join
        # must treat an integral float as the same player.
        journal.write_text(
            json.dumps({
                "schema_version": 1, "game_id": GAME_ID, "turn": 1,
                "players": [
                    {"player_id": 0.0, "seat_id": "seat-1",
                     "player_name": "Ada"},
                    {"player_id": 1, "seat_id": "seat-2",
                     "player_name": "NativePlace2"},
                ],
            }) + "\n" + '{"schema_version":1,"turn":2',
            encoding="utf-8",
        )
        response = self.replay(after_turn=0, limit=1)
        agent = response["snapshots"][0]["players"][0]
        self.assertEqual(agent["player_name"], "Ada")
        self.assertEqual(agent["seat_id"], "seat-1")
        self.assertEqual(agent["controller_label"], "pi-gpt-test")
        self.assertTrue(agent["scored"])

    def test_cache_is_separate_atomic_and_invalidated_by_source_signature(self):
        source = self.write_save(1, agent_score=12)
        first = self.replay(after_turn=0, limit=1)
        self.assertEqual(first["snapshots"][0]["players"][0]["score"], 12)
        cache_files = list((self.cache / GAME_ID).glob("*.json"))
        self.assertEqual(len(cache_files), 1)
        first_cache = json.loads(cache_files[0].read_text(encoding="utf-8"))
        self.assertNotIn(str(self.root), json.dumps(first_cache))
        self.assertEqual(list(self.saves.glob("*.json")), [])

        time.sleep(0.002)
        source.write_text(save_text(1, agent_score=1234), encoding="utf-8")
        os.utime(source, None)
        second = self.replay(after_turn=0, limit=1)
        self.assertEqual(second["snapshots"][0]["players"][0]["score"], 1234)
        second_cache = json.loads(cache_files[0].read_text(encoding="utf-8"))
        self.assertNotEqual(first_cache["source"], second_cache["source"])

    def test_pagination_parses_only_predecessor_page_and_lookahead(self):
        for turn in range(1, 7):
            self.write_save(turn)
        original = save_replay._load_candidate
        calls: list[int] = []

        def counted(source, turn, game_id, cache_directory):
            calls.append(turn)
            return original(source, turn, game_id, cache_directory)

        with patch.object(save_replay, "_load_candidate", side_effect=counted):
            response = self.replay(after_turn=3, limit=1)
        self.assertEqual([row["turn"] for row in response["snapshots"]], [4])
        self.assertTrue(response["has_more"])
        self.assertEqual(calls, [3, 4, 5])

    def test_pagination_advances_across_a_bounded_corrupt_window(self):
        for turn in range(1, 35):
            (self.saves / f"turn-{turn:04d}-auto.sav").write_text(
                "incomplete", encoding="utf-8",
            )
        self.write_save(35)
        first = self.replay(after_turn=0, limit=1)
        self.assertEqual(first["snapshots"], [])
        self.assertEqual(first["next_after_turn"], 34)
        self.assertTrue(first["has_more"])
        second = self.replay(after_turn=first["next_after_turn"], limit=1)
        self.assertEqual([row["turn"] for row in second["snapshots"]], [35])

    def test_symlink_sources_and_path_traversal_are_rejected(self):
        outside = self.root / "outside.sav"
        outside.write_text(save_text(1), encoding="utf-8")
        (self.saves / "turn-0001-auto.sav").symlink_to(outside)
        response = self.replay(after_turn=0, limit=1)
        self.assertFalse(response["available"])
        self.assertEqual(response["snapshots"], [])
        with self.assertRaises(SaveReplayError):
            replay_from_autosaves(self.runs, "../not-a-game", self.places)

        linked_id = "game_qrstuvwxyzabcdef"
        (self.runs / linked_id).symlink_to(self.runs / GAME_ID, target_is_directory=True)
        with self.assertRaises(SaveReplayError):
            replay_from_autosaves(self.runs, linked_id, self.places)

        forbidden_cache = self.saves / "must-not-be-created"
        with self.assertRaises(SaveReplayError):
            replay_from_autosaves(
                self.runs, GAME_ID, self.places, cache_root=forbidden_cache,
            )
        self.assertFalse(forbidden_cache.exists())

    def test_unsupported_or_partial_save_returns_only_public_warning(self):
        self.write_save(1, revision="2.6.0")
        (self.saves / "turn-0002-auto.sav").write_text(
            "[savefile]\noptions=\" +version3\"\nversion=80\n",
            encoding="utf-8",
        )
        response = self.replay(after_turn=0, limit=10)
        self.assertFalse(response["available"])
        text = json.dumps(response["replay_warnings"])
        self.assertIn("not supported", text)
        self.assertIn("incomplete or unreadable", text)
        self.assertNotIn(str(self.root), text)


if __name__ == "__main__":
    unittest.main()
