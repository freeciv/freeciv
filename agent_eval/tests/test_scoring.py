import tempfile
import unittest
from pathlib import Path

from agent_eval.scoring import (
    aggregate_leaderboard,
    parse_scorelog,
    summarize_episode,
)


class ScoringTests(unittest.TestCase):
    def test_final_score_and_competition_rank(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "score.log"
            path.write_text("""#FREECIV SCORELOG2 test
tag 0 score
turn 1 -4000 4000 BC
addplayer 1 0 Alpha
addplayer 1 1 Beta
data 1 0 0 7
data 1 0 1 7
turn 2 -3960 3960 BC
data 2 0 0 20
data 2 0 1 10
""", encoding="utf-8")
            parsed = parse_scorelog(path)
            self.assertEqual(parsed["final_turn"], 2)
            self.assertEqual([(row["name"], row["score"], row["rank"]) for row in parsed["players"]], [("Alpha", 20, 1), ("Beta", 10, 2)])

    def test_eliminated_player_keeps_last_score_while_survivor_advances(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "score.log"
            path.write_text("""#FREECIV SCORELOG2 test
tag 0 score
tag 1 cities
turn 1 -4000 4000 BC
addplayer 1 0 Alpha
addplayer 1 1 Beta
data 1 0 0 10
data 1 1 0 1
data 1 0 1 12
data 1 1 1 1
turn 2 -3960 3960 BC
data 2 0 0 25
data 2 1 0 2
data 2 0 1 30
data 2 1 1 2
turn 3 -3920 3920 BC
delplayer 2 1
data 3 0 0 50
data 3 1 0 3
turn 4 -3880 3880 BC
data 4 0 0 70
data 4 1 0 4
""", encoding="utf-8")
            parsed = parse_scorelog(path)
            self.assertEqual(parsed["final_turn"], 4)
            rows = {row["name"]: row for row in parsed["players"]}
            self.assertEqual(rows["Alpha"]["score"], 70)
            self.assertTrue(rows["Alpha"]["alive"])
            self.assertEqual(rows["Alpha"]["last_score_turn"], 4)
            self.assertEqual(rows["Beta"]["score"], 30)
            self.assertFalse(rows["Beta"]["alive"])
            self.assertEqual(rows["Beta"]["removed_turn"], 2)
            self.assertEqual(rows["Beta"]["last_score_turn"], 2)
            self.assertEqual(rows["Alpha"]["rank"], 1)
            self.assertEqual(rows["Beta"]["rank"], 2)

    def test_summary_uses_private_player_number_after_rename(self):
        with tempfile.TemporaryDirectory() as directory:
            episode = Path(directory)
            (episode / "manifest.json").write_text(
                '{"config":{"seats":[{"id":"agent","name":"Old Name"}]}}',
                encoding="utf-8",
            )
            (episode / "score.log").write_text(
                """#FREECIV SCORELOG2 test
tag 0 score
turn 1 -4000 4000 BC
addplayer 1 0 Old Name
data 1 0 0 5
turn 2 -3960 3960 BC
delplayer 2 0
addplayer 2 0 New Name
data 2 0 0 11
""",
                encoding="utf-8",
            )
            seat = {
                "id": "agent",
                "name": "Old Name",
                "type": "external",
                "model": "test-model",
            }

            summary = summarize_episode(
                episode,
                private_player_seats={0: seat},
            )

            self.assertEqual(len(summary["score"]["players"]), 1)
            row = summary["score"]["players"][0]
            self.assertEqual(row["name"], "New Name")
            self.assertEqual(row["score"], 11)
            self.assertEqual(row["seat_id"], "agent")

    def test_summary_without_private_mapping_keeps_name_attribution(self):
        with tempfile.TemporaryDirectory() as directory:
            episode = Path(directory)
            (episode / "manifest.json").write_text(
                '{"config":{"seats":[{"id":"agent","name":"Alpha"}]}}',
                encoding="utf-8",
            )
            (episode / "score.log").write_text(
                """#FREECIV SCORELOG2 test
tag 0 score
turn 1 -4000 4000 BC
addplayer 1 0 Alpha
data 1 0 0 5
""",
                encoding="utf-8",
            )

            summary = summarize_episode(episode)

            self.assertEqual(summary["score"]["players"][0]["seat_id"], "agent")

    def test_aggregate_leaderboard(self):
        summaries = [
            {
                "manifest": {"benchmark_valid": True},
                "score": {"players": [
                    {"seat_id": "a", "score": 20, "rank": 1},
                    {"seat_id": "b", "score": 10, "rank": 2},
                ]},
                "seat_stats": {
                    "a": {"decisions": 2, "fallbacks": 0, "input_tokens": 5, "output_tokens": 2, "mean_latency_ms": 10},
                    "b": {"decisions": 2, "fallbacks": 1, "input_tokens": 0, "output_tokens": 0, "mean_latency_ms": 2},
                },
            },
            {
                "manifest": {"benchmark_valid": False},
                "score": {"players": [
                    {"seat_id": "b", "score": 30, "rank": 1},
                    {"seat_id": "a", "score": 10, "rank": 2},
                ]},
                "seat_stats": {
                    "a": {"decisions": 1, "fallbacks": 0, "input_tokens": 1, "output_tokens": 1, "mean_latency_ms": 4},
                    "b": {"decisions": 1, "fallbacks": 1, "input_tokens": 0, "output_tokens": 0, "mean_latency_ms": 2},
                },
            },
        ]
        board = {row["seat_id"]: row for row in aggregate_leaderboard(summaries)}
        self.assertEqual(board["a"]["episodes"], 2)
        self.assertEqual(board["a"]["valid_episodes"], 1)
        self.assertEqual(board["a"]["average_score"], 20)
        self.assertEqual(board["a"]["invalid_episodes"], 1)
        self.assertEqual(board["b"]["fallbacks"], 2)
        self.assertEqual(board["a"]["input_tokens"], 6)

    def test_same_seat_id_different_models_do_not_merge(self):
        def summary(model, score):
            return {
                "manifest": {
                    "benchmark_valid": True,
                    "config": {"seats": [{
                        "id": "agent", "type": "openai_responses",
                        "model": model, "instructions": None,
                        "base_url": None, "options": {},
                    }]},
                },
                "score": {"players": [
                    {"seat_id": "agent", "score": score, "rank": 1}
                ]},
                "seat_stats": {},
            }

        board = aggregate_leaderboard([summary("model-a", 10), summary("model-b", 20)])
        self.assertEqual(len(board), 2)
        self.assertEqual({row["model"] for row in board}, {"model-a", "model-b"})
        self.assertEqual({row["seat_id"] for row in board}, {"agent"})
        self.assertEqual(len({row["controller_fingerprint"] for row in board}), 2)


if __name__ == "__main__":
    unittest.main()
