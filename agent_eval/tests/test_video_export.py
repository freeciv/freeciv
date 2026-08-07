from __future__ import annotations

import gzip
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from agent_eval.video_export import VideoExportError, export_run

from .test_save_replay import GAME_ID, board_save_text


def replay_row(turn: int, *, scores: tuple[int, int]) -> dict[str, object]:
    return {
        "schema_version": 1,
        "game_id": GAME_ID,
        "turn": turn,
        "year": -4000 + (turn - 1) * 50,
        "players": [
            {
                "seat_id": "place-1", "player_id": 0, "player_name": "Ada",
                "nation": "English", "government": "Despotism", "alive": True,
                "score": scores[0], "cities": 1, "citizens": 2, "population": 2,
                "units": 3, "gold": 50, "culture": 0, "known_tech_ids": [2, 4],
                "research": {"tech_id": 2, "name": "Alphabet"}, "future_techs": 0,
            },
            {
                "seat_id": "place-2", "player_id": 1, "player_name": "NativePlace2",
                "nation": "Spanish", "government": "Despotism", "alive": True,
                "score": scores[1], "cities": 2, "citizens": 5, "population": 5,
                "units": 4, "gold": 60, "culture": 0, "known_tech_ids": [2],
                "research": {"tech_id": 3, "name": "Bronze Working"}, "future_techs": 0,
            },
        ],
    }


class VideoExportTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.runs = self.root / "runs"
        self.run = self.runs / GAME_ID
        self.saves = self.run / "saves"
        self.saves.mkdir(parents=True)
        self.output = self.root / "export"
        (self.run / "manifest.json").write_text(json.dumps({
            "game_id": GAME_ID,
            "control_protocol": "full-control-v2",
            "state": "completed",
            "status": "completed",
            "started_at": 100.0,
            "finished_at": 3700.0,
            "config": {
                "ruleset": "classic",
                "objective": "Maximize final Freeciv civilization score.",
                "seeds": [7],
                "seats": [
                    {"id": "place-1", "type": "external",
                     "controller_label": "pi-gpt-test"},
                    {"id": "place-2", "type": "native", "model": "classic"},
                ],
            },
            "resolved_places": [
                {"place": 1, "player_color": "#0067A5",
                 "controller_label": "pi-gpt-test", "controller_type": "external"},
                {"place": 2, "player_color": "#F38400",
                 "controller_label": "Freeciv Classic AI", "controller_type": "native"},
            ],
        }), encoding="utf-8")

    def tearDown(self):
        self.temporary.cleanup()

    def write_board(self, turn: int) -> None:
        path = self.saves / f"turn-{turn:04d}-auto.sav.gz"
        with gzip.open(path, "wb") as stream:
            stream.write(board_save_text(turn).encode("utf-8"))

    def write_replay(self, turns: list[int]) -> None:
        lines = [
            json.dumps(replay_row(turn, scores=(turn, turn * 2))) for turn in turns
        ]
        (self.run / "replay.jsonl").write_text("\n".join(lines) + "\n", encoding="utf-8")

    def export(self) -> dict[str, object]:
        return export_run(self.runs, GAME_ID, self.output)

    def load(self, name: str) -> dict[str, object]:
        return json.loads((self.output / name).read_text(encoding="utf-8"))

    def test_export_writes_meta_and_one_frame_per_replay_turn(self):
        self.write_board(1)
        self.write_board(2)
        self.write_replay([1, 2])

        summary = self.export()

        self.assertEqual(summary["frame_count"], 2)
        self.assertEqual(summary["interpolated_turn_count"], 0)
        meta = self.load("meta.json")
        self.assertEqual(meta["width"], 3)
        self.assertEqual(meta["height"], 2)
        self.assertEqual(meta["topology"], "ISO|HEX")
        self.assertEqual(meta["board_density"], 1.0)
        # Both benchmarked seats plus the save's dynamic barbarian faction, which
        # scores but never occupies a place.
        self.assertEqual(
            [player["nation"] for player in meta["players"]],
            ["English", "Spanish", "Pirate"],
        )
        self.assertEqual(
            [player["seat"] for player in meta["players"]], [True, True, False],
        )
        self.assertEqual(
            [player["controller_label"] for player in meta["players"]],
            ["pi-gpt-test", "Freeciv Classic AI", "Freeciv dynamic faction"],
        )
        frames = self.load("frames.json")["frames"]
        self.assertEqual([frame["turn"] for frame in frames], [1, 2])
        self.assertEqual(frames[0]["owners"], ["0:2,-:1", "1:2,0:1"])
        self.assertEqual([stat["score"] for stat in frames[0]["stats"]], [1, 2])
        # The standings cards show what each seat is researching, per turn.
        self.assertEqual(
            [(stat["researching"], stat["bulbs"]) for stat in frames[0]["stats"]],
            [("Alphabet", 0), ("Bronze Working", 0)],
        )

    def test_ai_difficulty_reaches_the_native_player_and_the_meta(self):
        self.write_board(1)
        self.write_replay([1])
        manifest_path = self.run / "manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["config"]["difficulty"] = "cheating"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

        self.export()

        meta = self.load("meta.json")
        self.assertEqual(meta["ai_difficulty"], "cheating")
        # The level is the server's, so only the seat the server drives
        # carries it -- the agent seat and the dynamic faction do not.
        self.assertEqual(
            [player["ai_difficulty"] for player in meta["players"]],
            [None, "cheating", None],
        )

    def test_a_manifest_without_a_difficulty_exports_a_null_one(self):
        # Every run archived before the field existed takes this path; it must
        # export a null rather than inventing a level or failing the export.
        self.write_board(1)
        self.write_replay([1])

        self.export()

        meta = self.load("meta.json")
        self.assertIsNone(meta["ai_difficulty"])
        self.assertEqual(
            [player["ai_difficulty"] for player in meta["players"]],
            [None, None, None],
        )

    def test_terrain_and_infrastructure_ship_only_when_they_change(self):
        self.write_board(1)
        self.write_board(2)
        self.write_replay([1, 2])

        self.export()

        frames = self.load("frames.json")["frames"]
        self.assertEqual(frames[0]["terrain"], ["g d", "ddg"])
        self.assertIsNone(frames[1]["terrain"])
        # The fixture's row 0 holds Irrigation, Road, River across three tiles;
        # only Road (bit 1) and River (bit 4) survive into the render layer.
        self.assertEqual(frames[0]["infrastructure"], ["014", "000"])
        self.assertIsNone(frames[1]["infrastructure"])

    def test_turns_without_a_save_are_flagged_and_hold_the_prior_board(self):
        self.write_board(1)
        self.write_board(3)
        self.write_replay([1, 2, 3])

        summary = self.export()

        self.assertEqual(summary["interpolated_turn_count"], 1)
        self.assertEqual(summary["interpolated_turns"], [2])
        frames = self.load("frames.json")["frames"]
        self.assertTrue(frames[1]["interpolated"])
        self.assertEqual(frames[1]["board_turn"], 1)
        self.assertNotIn("owners", frames[1])
        # Scores still come from replay.jsonl on an interpolated turn.
        self.assertEqual([stat["score"] for stat in frames[1]["stats"]], [2, 4])
        self.assertAlmostEqual(self.load("meta.json")["board_density"], 2 / 3, places=5)

    def test_float_player_ids_from_the_lua_bridge_still_resolve_to_seats(self):
        self.write_board(1)
        rows = [replay_row(1, scores=(3, 6))]
        for player in rows[0]["players"]:
            player["player_id"] = float(player["player_id"])
        (self.run / "replay.jsonl").write_text(
            json.dumps(rows[0]) + "\n", encoding="utf-8",
        )

        self.export()

        meta = self.load("meta.json")
        self.assertEqual(
            [player["seat"] for player in meta["players"]], [True, True, False],
        )
        frames = self.load("frames.json")["frames"]
        self.assertEqual(
            [(stat["player_id"], stat["score"]) for stat in frames[0]["stats"]],
            [(0, 3), (1, 6)],
        )

    def test_a_leading_gap_is_seeded_from_the_first_readable_board(self):
        self.write_board(2)
        self.write_replay([1, 2])

        self.export()

        frames = self.load("frames.json")["frames"]
        self.assertTrue(frames[0]["interpolated"])
        self.assertEqual(frames[0]["board_turn"], 2)

    def test_an_event_log_is_always_written_even_when_derivation_fails(self):
        # The fixture saves are minimal, so extraction may find nothing. The
        # film still has to render, so the log degrades instead of raising.
        self.write_board(1)
        self.write_replay([1])

        summary = self.export()

        self.assertTrue(Path(summary["events_path"]).is_file())
        events = self.load("events.json")
        self.assertIsInstance(events["events"], list)
        self.assertIn("available", events)
        self.assertEqual(events["game_id"], GAME_ID)
        meta = self.load("meta.json")
        self.assertIn("event_counts", meta)
        self.assertIn("total_events", meta)

    def test_a_broken_event_extractor_does_not_take_the_export_down(self):
        self.write_board(1)
        self.write_replay([1])

        with patch(
            "agent_eval.game_events.events_from_autosaves",
            side_effect=RuntimeError("extractor exploded"),
        ):
            summary = self.export()

        self.assertEqual(summary["frame_count"], 1)
        self.assertEqual(summary["event_count"], 0)
        self.assertFalse(summary["events_available"])
        self.assertEqual(self.load("events.json")["events"], [])

    def test_a_run_without_any_readable_board_fails_loudly(self):
        self.write_replay([1])

        with self.assertRaises(VideoExportError):
            self.export()

    def test_a_missing_run_directory_fails_loudly(self):
        with self.assertRaises(VideoExportError):
            export_run(self.runs, "game_missingmissingmissing", self.output)

    def test_the_run_directory_is_not_written_to(self):
        self.write_board(1)
        self.write_replay([1])
        before = sorted(path.name for path in self.run.rglob("*"))

        self.export()

        self.assertEqual(sorted(path.name for path in self.run.rglob("*")), before)


if __name__ == "__main__":
    unittest.main()
