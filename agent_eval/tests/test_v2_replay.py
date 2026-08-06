from __future__ import annotations

import gzip
import json
import re
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from agent_eval import save_replay
from agent_eval.supervisor import (
    Game,
    Supervisor,
    _classic_technology_catalog,
)
from agent_eval.tests.test_save_replay import save_text
from agent_eval.tests.test_supervisor import FakeSidecarFactory
from agent_eval.v2_replay import MAX_UNREADABLE_ATTEMPTS, V2ReplayProducer


GAME_ID = "game_" + "v" * 24
BRIDGE_PATH = Path(__file__).resolve().parent.parent / "bridge.lua"


def leader_named_save_text(turn: int, **kwargs) -> str:
    """A save written the way a full-control-v2 game writes one.

    A v2 seat is played through a native client, so Freeciv replaces the
    configured player name with a ruleset leader name.  Only the player number
    still identifies the seat.
    """
    text = save_text(turn, **kwargs)
    return text.replace('name="AgentPlace1"', 'name="Ada"', 1).replace(
        'name="NativePlace2"', 'name="Shaka"', 1,
    )


def bridge_player_fields() -> set[str]:
    """Return the field names the strategic-v1 Lua bridge writes per player."""
    source = BRIDGE_PATH.read_text(encoding="utf-8")
    start = source.index("local function replay_player(")
    end = source.index("local function capture_replay(")
    return set(re.findall(r'"([a-z_]+)":', source[start:end]))


class V2ReplayProducerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.runs = self.root / "runs"
        self.episode = self.runs / GAME_ID
        self.saves = self.episode / "saves"
        self.saves.mkdir(parents=True)
        self.cache = self.root / "replay-cache"
        self.replay_path = self.episode / "replay.jsonl"
        self.replay_path.write_text("", encoding="utf-8")
        self.producer = self.make_producer()

    def tearDown(self):
        self.temporary.cleanup()

    def make_producer(self, **overrides) -> V2ReplayProducer:
        arguments = {
            "seat_ids": lambda: {0: "place-1", 1: "place-2"},
            "cache_root": self.cache,
            **overrides,
        }
        return V2ReplayProducer(self.runs, GAME_ID, self.episode, **arguments)

    def write_save(self, turn: int, *, text: str | None = None, **kwargs) -> Path:
        path = self.saves / f"turn-{turn:04d}-auto.sav.gz"
        body = leader_named_save_text(turn, **kwargs) if text is None else text
        with gzip.open(path, "wb") as stream:
            stream.write(body.encode("utf-8"))
        return path

    def write_unreadable_save(self, turn: int) -> Path:
        path = self.saves / f"turn-{turn:04d}-auto.sav.gz"
        path.write_bytes(gzip.compress(
            leader_named_save_text(turn).encode("utf-8"),
        )[:64])
        return path

    def rows(self) -> list[dict]:
        return [
            json.loads(line)
            for line in self.replay_path.read_text(encoding="utf-8").splitlines()
            if line.strip()
        ]

    def warnings(self) -> list[dict]:
        path = self.episode / "replay-warnings.jsonl"
        if not path.exists():
            return []
        return [
            json.loads(line)
            for line in path.read_text(encoding="utf-8").splitlines()
            if line.strip()
        ]

    def test_a_new_autosave_becomes_one_replay_row(self):
        self.assertEqual(self.producer.refresh(), 0)
        self.assertEqual(self.rows(), [])

        self.write_save(1)
        self.assertEqual(self.producer.refresh(), 1)
        self.write_save(2, agent_score=31)
        self.assertEqual(self.producer.refresh(), 1)

        rows = self.rows()
        self.assertEqual([row["turn"] for row in rows], [1, 2])
        self.assertEqual([row["game_id"] for row in rows], [GAME_ID] * 2)
        self.assertEqual(rows[1]["year"], -4000 + 2 * 50)
        self.assertEqual(
            [player["player_id"] for player in rows[1]["players"]], [0, 1, 2],
        )
        agent = rows[1]["players"][0]
        self.assertEqual(agent["player_name"], "Ada")
        self.assertEqual(agent["score"], 31)
        self.assertEqual(agent["citizens"], 21)
        self.assertEqual(agent["population"], 21)
        self.assertEqual(agent["nation"], "Japanese")
        self.assertEqual(agent["government"], "Despotism")
        self.assertTrue(agent["alive"])
        self.assertEqual(agent["research"]["name"], "Industrialization")
        self.assertEqual(agent["research"]["bulbs"], 6)
        self.assertEqual(agent["turn"], 2)

    def test_rows_carry_the_strategic_v1_bridge_field_names(self):
        self.write_save(1)
        self.producer.refresh()
        row = self.rows()[0]
        player = row["players"][0]

        self.assertEqual(
            set(row) - {"source"},
            {"schema_version", "game_id", "turn", "year", "players"},
        )
        self.assertEqual(row["source"], "autosave")
        self.assertEqual(
            set(player) | set(player["research"]),
            bridge_player_fields(),
        )

    def test_seat_ids_follow_the_native_player_number(self):
        self.write_save(1)
        self.producer.refresh()
        players = self.rows()[0]["players"]
        self.assertEqual(
            [player["seat_id"] for player in players],
            ["place-1", "place-2", "dynamic-player-2"],
        )

    def test_the_catalog_is_written_once_and_the_supervisor_accepts_it(self):
        catalog_path = self.episode / "replay-catalog.json"
        self.write_save(1)
        self.producer.refresh()
        catalog = _classic_technology_catalog(
            json.loads(catalog_path.read_text(encoding="utf-8")),
        )
        self.assertEqual(len(catalog["technologies"]), 87)

        signature = catalog_path.stat().st_mtime_ns
        self.write_save(2)
        self.producer.refresh()
        self.assertEqual(catalog_path.stat().st_mtime_ns, signature)

    def test_a_converted_autosave_is_never_parsed_again(self):
        parsed: list[str] = []
        original = save_replay._read_stable_save

        def counting(path):
            parsed.append(path.name)
            return original(path)

        with patch.object(save_replay, "_read_stable_save", counting):
            self.write_save(1)
            self.producer.refresh()
            self.write_save(2)
            for _ in range(4):
                self.assertLessEqual(self.producer.refresh(), 1)

        self.assertEqual(
            sorted(parsed), ["turn-0001-auto.sav.gz", "turn-0002-auto.sav.gz"],
        )
        self.assertEqual([row["turn"] for row in self.rows()], [1, 2])

    def test_an_unchanged_saves_directory_costs_no_reconstruction(self):
        calls: list[int] = []

        def loader(*args, **kwargs):
            calls.append(kwargs["after_turn"])
            return save_replay.replay_from_autosaves(*args, **kwargs)

        producer = self.make_producer(replay_loader=loader)
        self.write_save(1)
        self.assertEqual(producer.refresh(), 1)
        self.assertEqual(producer.refresh(), 0)
        self.assertEqual(producer.refresh(), 0)
        self.assertEqual(calls, [0])

    def test_a_partially_written_autosave_is_retried_not_skipped(self):
        self.write_save(1)
        self.write_unreadable_save(2)
        self.write_save(3)

        self.assertEqual(self.producer.refresh(), 1)
        self.assertEqual([row["turn"] for row in self.rows()], [1])
        self.assertFalse(self.producer.disabled)
        self.assertEqual(self.warnings(), [])

        self.write_save(2)
        self.assertEqual(self.producer.refresh(), 2)
        self.assertEqual([row["turn"] for row in self.rows()], [1, 2, 3])
        self.assertEqual(self.warnings(), [])

    def test_a_permanently_unreadable_autosave_is_skipped_with_one_warning(self):
        self.write_save(1)
        self.write_unreadable_save(2)
        self.write_save(3)

        for _ in range(MAX_UNREADABLE_ATTEMPTS + 2):
            self.producer.refresh()

        self.assertEqual([row["turn"] for row in self.rows()], [1, 3])
        self.assertEqual(
            self.warnings(),
            [{"turn": 2, "message": "replay capture unavailable"}],
        )
        self.assertFalse(self.producer.disabled)

    def test_an_existing_journal_is_resumed_not_rewritten(self):
        self.write_save(1)
        self.write_save(2)
        self.replay_path.write_text(
            json.dumps({"schema_version": 1, "turn": 1, "players": []}) + "\n",
            encoding="utf-8",
        )

        self.assertEqual(self.producer.refresh(), 1)
        self.assertEqual([row["turn"] for row in self.rows()], [1, 2])

    def test_a_journal_this_producer_cannot_read_stays_untouched(self):
        self.write_save(1)
        self.replay_path.write_text("not a replay journal\n", encoding="utf-8")

        self.assertEqual(self.producer.refresh(), 0)
        self.assertTrue(self.producer.disabled)
        self.assertEqual(
            self.replay_path.read_text(encoding="utf-8"),
            "not a replay journal\n",
        )

    def test_a_failing_reconstruction_never_raises_and_finally_stops(self):
        def loader(*_args, **_kwargs):
            raise RuntimeError("reconstruction exploded")

        producer = self.make_producer(replay_loader=loader)
        self.write_save(1)
        for _ in range(9):
            self.assertEqual(producer.refresh(), 0)
        self.assertFalse(producer.disabled)
        self.assertEqual(producer.refresh(), 0)
        self.assertTrue(producer.disabled)
        self.assertEqual(self.rows(), [])

    def test_drain_retries_and_then_skips_an_unreadable_autosave(self):
        self.write_save(1)
        self.write_unreadable_save(2)
        self.write_save(3)

        self.assertEqual(self.producer.drain(), 2)
        self.assertEqual([row["turn"] for row in self.rows()], [1, 3])
        self.assertEqual(
            self.warnings(),
            [{"turn": 2, "message": "replay capture unavailable"}],
        )

    def test_drain_converts_every_remaining_autosave(self):
        for turn in range(1, 20):
            self.write_save(turn)
        self.assertEqual(self.producer.drain(), 19)
        self.assertEqual(
            [row["turn"] for row in self.rows()], list(range(1, 20)),
        )

    def test_unusable_seat_ids_fall_back_to_dynamic_players(self):
        producer = self.make_producer(seat_ids=lambda: {"place-1": 0})
        self.write_save(1)
        producer.refresh()
        self.assertEqual(
            [player["seat_id"] for player in self.rows()[0]["players"]],
            ["dynamic-player-0", "dynamic-player-1", "dynamic-player-2"],
        )


class V2ReplayGameTests(unittest.TestCase):
    """The live supervisor endpoints served from reconstructed autosaves."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.launch = patch.object(Game, "_launch", autospec=True)
        self.send = patch.object(Game, "_send_commands", autospec=True)
        self.launch.start()
        self.send.start()
        self.supervisor = Supervisor(
            Path(self.temporary.name) / "runs", "admin-secret",
            binary="/unused/freeciv-server",
            process_factory=lambda *args, **kwargs: None,
            sidecar_factory=FakeSidecarFactory(),
        )
        self.supervisor.service_url = "http://127.0.0.1:9876"
        self.assertEqual(
            self.supervisor.replay_cache_root,
            Path(self.temporary.name).resolve() / "replay-cache",
        )

    def tearDown(self):
        self.supervisor.close()
        self.send.stop()
        self.launch.stop()
        self.temporary.cleanup()

    def create(self, **overrides):
        return self.supervisor.create_game({
            "mode": "single",
            "places": 2,
            "turns": 2,
            "seed": 7,
            "objective": "win cleanly",
            "action_timeout_s": 1,
            "lobby_timeout_s": 0,
            "frame_interval": 1,
            "frame_zoom": 1,
            **overrides,
        })

    def write_save(self, game, turn: int, **kwargs) -> None:
        path = game.episode / "saves" / f"turn-{turn:04d}-auto.sav.gz"
        with gzip.open(path, "wb") as stream:
            stream.write(leader_named_save_text(turn, **kwargs).encode("utf-8"))

    def test_a_v2_game_serves_replay_rows_rebuilt_from_its_autosaves(self):
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        game.join(
            created["join_token"], controller_label="codex-gpt-test",
            metadata={"model": "gpt-test"},
            supported_control_protocols=["full-control-v2"],
        )
        self.assertFalse(game.replay_state(0, 10)["available"])

        self.write_save(game, 1)
        self.write_save(game, 2, agent_score=44)

        first = game.replay_state(0, 1)
        self.assertTrue(first["available"])
        self.assertTrue(first["has_more"])
        self.assertEqual(first["next_after_turn"], 1)
        self.assertEqual(len(first["catalog"]["technologies"]), 87)
        self.assertEqual(first["replay_warnings"], [])

        agent = first["snapshots"][0]["players"][0]
        self.assertEqual(agent["seat_id"], "place-1")
        self.assertEqual(agent["place"], 1)
        self.assertEqual(agent["player_name"], "Ada")
        self.assertEqual(agent["player_color"], "#0067A5")
        self.assertEqual(agent["controller_label"], "codex-gpt-test")
        self.assertEqual(agent["model"], "gpt-test")
        self.assertTrue(agent["scored"])

        native = first["snapshots"][0]["players"][1]
        self.assertEqual(native["seat_id"], "place-2")
        self.assertEqual(native["controller_type"], "native")

        pirate = first["snapshots"][0]["players"][2]
        self.assertEqual(pirate["seat_id"], "dynamic-player-2")
        self.assertIsNone(pirate["place"])
        self.assertFalse(pirate["scored"])

        second = game.replay_state(first["next_after_turn"], 10)
        self.assertEqual(second["snapshots"][0]["players"][0]["score"], 44)
        self.assertFalse(second["has_more"])

    def test_watch_state_reports_the_reconstructed_replay_as_available(self):
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        self.assertFalse(game.watch_state()["replay"]["available"])
        self.write_save(game, 1)
        self.assertTrue(game.watch_state()["replay"]["available"])

    def test_a_strategic_v1_game_keeps_the_lua_bridge_as_the_only_writer(self):
        created = self.create()
        game = self.supervisor.game(created["game_id"])
        self.assertIsNone(game.v2_replay_producer)

        self.write_save(game, 1)
        state = game.replay_state(0, 10)

        self.assertFalse(state["available"])
        self.assertEqual(state["snapshots"], [])
        self.assertEqual(game.replay_path.read_text(encoding="utf-8"), "")
        self.assertFalse((game.episode / "replay-catalog.json").exists())

    def test_finalization_drains_every_autosave_into_the_journal(self):
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        for turn in range(1, 20):
            self.write_save(game, turn)

        game._drain_v2_replay()

        rows = [
            json.loads(line)
            for line in game.replay_path.read_text(
                encoding="utf-8",
            ).splitlines() if line.strip()
        ]
        self.assertEqual([row["turn"] for row in rows], list(range(1, 20)))

    def test_replay_reconstruction_does_not_hold_the_turn_condition(self):
        created = self.create(control_protocol="full-control-v2")
        game = self.supervisor.game(created["game_id"])
        self.write_save(game, 1)
        owned: list[bool] = []
        producer = game.v2_replay_producer
        original = producer.refresh

        def observed():
            owned.append(game.condition._is_owned())
            return original()

        with patch.object(producer, "refresh", observed):
            game.replay_state(0, 10)
        self.assertEqual(owned, [False])


if __name__ == "__main__":
    unittest.main()


class V2ReplayKeepWarmTests(unittest.TestCase):
    """The background keep-warm loop converges and respects game state."""

    class _Stub:
        def __init__(self, refresh_returns):
            import threading as _threading

            from agent_eval import supervisor as _supervisor

            self.condition = _threading.Condition()
            self.replay_lock = _threading.Lock()
            self.state = "running"
            self.cancel_requested = False
            self.game_id = "game_" + "k" * 24
            self.v2_replay_keepwarm_thread = None
            self._supervisor = _supervisor
            stub = self

            class _Producer:
                def __init__(self):
                    self.calls = 0

                def refresh(self):
                    self.calls += 1
                    if refresh_returns:
                        return refresh_returns.pop(0)
                    # Backlog exhausted: flip the game terminal so the
                    # loop's next state check exits instead of sleeping.
                    stub.state = "completed"
                    return 0

            self.v2_replay_producer = _Producer()

    def test_keepwarm_converges_on_backlog_then_exits_at_terminal(self):
        from agent_eval import supervisor as supervisor_module

        stub = self._Stub(refresh_returns=[12, 12, 7])
        with patch.object(
            supervisor_module.time, "sleep",
            side_effect=AssertionError(
                "keep-warm slept before converging on the backlog"
            ),
        ):
            supervisor_module.Game._keep_v2_replay_warm(stub)
        # Three backlog batches plus the final zero-append probe.
        self.assertEqual(stub.v2_replay_producer.calls, 4)

    def test_keepwarm_never_refreshes_a_terminal_game(self):
        from agent_eval import supervisor as supervisor_module

        stub = self._Stub(refresh_returns=[1])
        stub.state = "failed"
        supervisor_module.Game._keep_v2_replay_warm(stub)
        self.assertEqual(stub.v2_replay_producer.calls, 0)

    def test_start_keepwarm_is_idempotent_and_needs_a_producer(self):
        from agent_eval import supervisor as supervisor_module

        stub = self._Stub(refresh_returns=[])
        stub._keep_v2_replay_warm = lambda: None
        started = []
        with patch.object(supervisor_module.threading, "Thread") as thread:
            thread.side_effect = lambda **kw: started.append(kw) or type(
                "T", (), {"start": lambda self: None},
            )()
            supervisor_module.Game._start_v2_replay_keepwarm(stub)
            supervisor_module.Game._start_v2_replay_keepwarm(stub)
        self.assertEqual(len(started), 1)
        bare = self._Stub(refresh_returns=[])
        bare.v2_replay_producer = None
        with patch.object(supervisor_module.threading, "Thread") as thread:
            supervisor_module.Game._start_v2_replay_keepwarm(bare)
            thread.assert_not_called()
