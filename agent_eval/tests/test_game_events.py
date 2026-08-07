from __future__ import annotations

import gzip
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from agent_eval import game_events, save_replay
from agent_eval.game_events import events_from_autosaves
from agent_eval.save_replay import SaveReplayError


GAME_ID = "game_abcdefghijklmnop"
REAL_RUNS = Path(__file__).resolve().parents[2] / ".agent-eval" / "runs"
IMPROVEMENTS = ("Palace", "Pyramids", "Great Library")
PLACES = [
    {
        "player_name": "AgentPlace1", "seat_id": "place-1", "place": 1,
        "player_color": "#0067A5", "controller_label": "pi-gpt-test",
        "controller_type": "external", "model": "gpt-test",
    },
    {
        "player_name": "NativePlace2", "seat_id": "place-2", "place": 2,
        "player_color": "#F38400", "controller_label": "Freeciv Classic AI",
        "controller_type": "native", "model": "classic",
    },
]


def classic_vector() -> list[str]:
    return ["A_NONE", *save_replay._classic_requirements()]


def bits(vector: list[str]) -> str:
    return "".join("1" if name == "A_NONE" else "0" for name in vector)


class Player:
    """One synthetic player's turn state."""

    def __init__(
        self,
        name: str,
        *,
        nation: str,
        government: str = "Despotism",
        alive: bool = True,
        diplomacy: tuple[str, ...] = (),
        cities: tuple[tuple[int, int, int, str, bool, str], ...] = (),
        barbarian: str = "None",
        score: int = 10,
        spaceship: int = 0,
        spaceship_parts: int = 0,
        launch_year: int | None = None,
    ):
        self.name = name
        self.nation = nation
        self.government = government
        self.alive = alive
        self.diplomacy = diplomacy
        # (id, x, y, name, capital, improvement bits)
        self.cities = cities
        self.barbarian = barbarian
        self.score = score
        self.spaceship = spaceship
        self.spaceship_parts = spaceship_parts
        self.launch_year = launch_year

    def section(self, player_id: int) -> str:
        diplomacy = "".join(
            f'"{state}","{state}",0,0,0,0,FALSE,FALSE,FALSE\n'
            for state in self.diplomacy
        )
        city_rows = "".join(
            f'{y},{x},{city_id},{index + 1},'
            f'"{"Primary" if capital else "Not"}","{name}","{improvements}"\n'
            for index, (city_id, x, y, name, capital, improvements)
            in enumerate(self.cities)
        )
        spaceship = f"spaceship.state={self.spaceship}\n"
        if self.spaceship_parts:
            spaceship += (
                f"spaceship.structurals={self.spaceship_parts}\n"
                "spaceship.components=0\nspaceship.modules=0\n"
            )
        if self.launch_year is not None:
            spaceship += f"spaceship.launch_year={self.launch_year}\n"
        return (
            f"\n[player{player_id}]\n"
            f'name="{self.name}"\n'
            "color.r=0\ncolor.g=103\ncolor.b=165\n"
            f'nation="{self.nation}"\n'
            f"team_no={player_id}\n"
            f'government_name="{self.government}"\n'
            f"is_alive={'TRUE' if self.alive else 'FALSE'}\n"
            f'ai.barb_type="{self.barbarian}"\n'
            'diplstate={"current","closest","first_contact_turn","turns_left",'
            '"has_reason_to_cancel","contact_turns_left","embassy",'
            '"gives_shared_vision","gives_shared_tiles"\n'
            f"{diplomacy}}}\n"
            f"{spaceship}"
            "gold=40\n"
            f"ncities={len(self.cities)}\n"
            + (
                '\nc={"y","x","id","size","capital","name","improvements"\n'
                f"{city_rows}}}\n" if self.cities else ""
            )
            + "nunits=0\n"
            f"\n[score{player_id}]\n"
            "happy=1\ncontent=0\nunhappy=0\nangry=0\n"
            "specialists0=0\nspecialists1=0\nculture=0\n"
            f"total={self.score}\n"
        )


def save_text(turn: int, players: list[Player], *, reason: str = "Autosave") -> str:
    vector = classic_vector()
    vector_value = ",".join(json.dumps(name) for name in vector)
    improvement_value = ",".join(json.dumps(name) for name in IMPROVEMENTS)
    research_rows = "\n".join(
        f'{index},"A_UNSET",0,0,"",0,"Alphabet",0,"{bits(vector)}"'
        for index in range(len(players))
    )
    return (
        "[savefile]\n"
        'options=" +version3"\n'
        "version=80\n"
        f'reason="{reason}"\n'
        'revision="3.3.90.13-dev"\n'
        'rulesetdir="classic"\n'
        f"technology_size={len(vector)}\n"
        f"technology_vector={vector_value}\n"
        f"improvement_size={len(IMPROVEMENTS)}\n"
        f"improvement_vector={improvement_value}\n"
        "extras_size=1\n"
        'extras_vector="Irrigation"\n'
        'terrident={"name","identifier"\n'
        '"Ocean"," "\n'
        '"Grassland","g"\n'
        "}\n\n"
        "[game]\n"
        f"turn={turn}\n"
        f"year={-4000 + turn * 50}\n\n"
        "[settings]\n"
        'set={"name","value","gamestart","gamesetdef"\n'
        '"team_pooled_research",TRUE,TRUE,"Changed"\n'
        '"topology","ISO|HEX","ISO|HEX","Internal"\n'
        '"wrap","WRAPX","WRAPX","Internal"\n'
        '"xsize",3,3,"Internal"\n'
        '"ysize",2,2,"Internal"\n}\n\n'
        "[players]\n"
        f"nplayers={len(players)}\n"
        + "".join(
            player.section(player_id) for player_id, player in enumerate(players)
        )
        + "\n[research]\n"
        'r={"number","goal_name","futuretech","bulbs_before",'
        '"saved_name","bulbs","now_name","free_bulbs","done"\n'
        + research_rows
        + "\n}\n\n[map]\n"
        't0000="ggg"\n'
        't0001="ggg"\n'
        'alt0000="1,1,1"\n'
        'alt0001="1,1,1"\n'
        'owner0000="0,0,-"\n'
        'owner0001="-,-,-"\n'
        'e00_0000="000"\n'
        'e00_0001="000"\n'
    )


def agent(**kwargs) -> Player:
    kwargs.setdefault("nation", "English")
    return Player("Elizabeth", **kwargs)


def native(**kwargs) -> Player:
    kwargs.setdefault("nation", "Italian")
    return Player("NativePlace2", **kwargs)


class GameEventsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.runs = self.root / ".agent-eval" / "runs"
        self.saves = self.runs / GAME_ID / "saves"
        self.saves.mkdir(parents=True)
        self.cache = self.root / ".agent-eval" / "replay-cache"

    def tearDown(self):
        self.temporary.cleanup()

    def write_save(
        self, turn: int, players: list[Player], *, kind: str = "auto",
        reason: str = "Autosave",
    ) -> Path:
        path = self.saves / f"turn-{turn:04d}-{kind}.sav.gz"
        with gzip.open(path, "wb") as stream:
            stream.write(save_text(turn, players, reason=reason).encode("utf-8"))
        return path

    def write_journal(self, *seat_ids: str) -> None:
        (self.runs / GAME_ID / "replay.jsonl").write_text(
            json.dumps({
                "turn": 1,
                "players": [
                    {"player_id": index, "seat_id": seat_id}
                    for index, seat_id in enumerate(seat_ids)
                ],
            }) + "\n",
            encoding="utf-8",
        )

    def events(self, **kwargs) -> dict:
        return events_from_autosaves(
            self.runs, GAME_ID, PLACES, cache_root=self.cache, **kwargs,
        )

    def summaries(self, kind: str | None = None) -> list[tuple[int, str, str]]:
        return [
            (event["turn"], event["kind"], event["summary"])
            for event in self.events()["events"]
            if kind is None or event["kind"] == kind
        ]

    def test_seat_labels_survive_the_ruler_rename_via_the_journal(self):
        # The save renames the configured seat to its ruler, so a name join
        # would demote place-1 to a dynamic faction.
        self.write_journal("place-1", "place-2")
        self.write_save(1, [
            agent(diplomacy=("Never met", "Never met")),
            native(diplomacy=("Never met", "Never met")),
        ])
        self.write_save(2, [
            agent(diplomacy=("Never met", "War")),
            native(diplomacy=("War", "Never met")),
        ])
        event = self.events()["events"][0]
        self.assertEqual(event["kind"], "war_declared")
        self.assertEqual(
            event["summary"],
            "pi-gpt-test met Italian (CPU) — no treaty, at war",
        )
        self.assertEqual(event["actors"], ["place-1", "place-2"])
        self.assertTrue(event["data"]["first_contact"])

    def test_the_native_side_is_named_by_nation_in_prose(self):
        # The same nation-first shape every other surface uses, minus the
        # difficulty: an event log is read many times, so "(CPU: Hard)" is
        # left to the title card and the standings.
        self.assertEqual(game_events._native_ai_label("Italian"), "Italian (CPU)")
        # With no nation recorded a summary must not open on a parenthesis.
        self.assertEqual(game_events._native_ai_label(""), "CPU")
        # And the derivation really does emit it, with the old name gone.
        self.write_journal("place-1", "place-2")
        self.write_save(1, [
            agent(diplomacy=("Never met", "Never met")),
            native(diplomacy=("Never met", "Never met")),
        ])
        self.write_save(2, [
            agent(diplomacy=("Never met", "War")),
            native(diplomacy=("War", "Never met")),
        ])
        prose = " ".join(summary for _, _, summary in self.summaries())
        self.assertIn("Italian (CPU)", prose)
        self.assertNotIn("Deity", prose)

    def test_diplomacy_transitions_are_named_and_first_contact_is_honest(self):
        self.write_journal("place-1", "place-2")
        states = ("Never met", "Peace", "War", "Cease-fire", "Alliance")
        for turn, state in enumerate(states, start=1):
            mirror = "Never met" if state == "Never met" else state
            self.write_save(turn, [
                agent(diplomacy=("Never met", state)),
                native(diplomacy=(mirror, "Never met")),
            ])
        self.assertEqual(self.summaries(), [
            (2, "first_contact", "pi-gpt-test and Italian (CPU) made first contact"),
            (
                3, "war_declared",
                "pi-gpt-test and Italian (CPU) broke their peace — war",
            ),
            (4, "ceasefire_agreed", "pi-gpt-test and Italian (CPU) agreed a cease-fire"),
            (5, "alliance_formed", "pi-gpt-test and Italian (CPU) formed an alliance"),
        ])
        broken = next(
            row for row in self.events()["events"] if row["kind"] == "war_declared"
        )
        self.assertEqual(broken["data"]["broke_pact"], "Peace")
        # Betraying a pact outranks a war that was always coming.
        self.assertGreater(broken["weight"], game_events._BASE_WEIGHT["war_declared"])

    def test_cities_are_founded_captured_and_destroyed(self):
        self.write_journal("place-1", "place-2")
        london = (7, 0, 0, "London", True, "100")
        york = (8, 1, 0, "York", False, "100")
        self.write_save(1, [agent(cities=()), native(cities=())])
        self.write_save(2, [agent(cities=(london, york)), native(cities=())])
        self.write_save(3, [
            agent(cities=()),
            native(cities=(london, york)),
        ])
        self.write_save(4, [agent(cities=()), native(cities=())])
        self.assertEqual(self.summaries(), [
            (2, "city_founded", "pi-gpt-test founded 2 cities: London, York"),
            (
                3, "city_captured",
                "Italian (CPU) captured 2 cities from pi-gpt-test: London, York",
            ),
            (
                4, "city_destroyed",
                "2 of Italian (CPU)'s cities were destroyed: London, York",
            ),
        ])
        captured = next(
            event for event in self.events()["events"]
            if event["kind"] == "city_captured"
        )
        self.assertEqual(captured["data"]["capital_cities"], ["London"])
        self.assertEqual(captured["actors"], ["place-2", "place-1"])

    def test_a_single_capture_of_a_capital_says_so(self):
        self.write_journal("place-1", "place-2")
        london = (7, 0, 0, "London", True, "100")
        self.write_save(1, [agent(cities=(london,)), native(cities=())])
        self.write_save(2, [agent(cities=()), native(cities=(london,))])
        self.assertEqual(self.summaries("city_captured"), [(
            2, "city_captured",
            "Italian (CPU) captured the capital London from pi-gpt-test",
        )])

    def test_governments_revolutions_and_adoptions_read_as_prose(self):
        self.write_journal("place-1", "place-2")
        for turn, government in enumerate(
            ("Despotism", "Anarchy", "Republic", "Democracy"), start=1,
        ):
            self.write_save(turn, [
                agent(government=government), native(),
            ])
        self.assertEqual(self.summaries("government_changed"), [
            (2, "government_changed", "pi-gpt-test began a revolution"),
            (3, "government_changed", "pi-gpt-test adopted Republic"),
            (
                4, "government_changed",
                "pi-gpt-test switched from Republic to Democracy",
            ),
        ])

    def test_eliminations_and_barbarian_uprisings_are_not_confused(self):
        self.write_journal("place-1", "place-2")
        pirates = Player(
            "Calico Jack", nation="Pirate", barbarian="Sea",
            diplomacy=("War", "War", "Never met"),
        )
        self.write_save(1, [agent(), native()])
        self.write_save(2, [
            agent(diplomacy=("Never met", "Never met", "War")),
            native(diplomacy=("Never met", "Never met", "War")),
            pirates,
        ])
        dead_pirates = Player(
            "Calico Jack", nation="Pirate", barbarian="Sea", alive=False,
            diplomacy=("War", "War", "Never met"),
        )
        self.write_save(3, [
            agent(alive=False, diplomacy=("Never met", "Never met", "War")),
            native(diplomacy=("Never met", "Never met", "War")),
            dead_pirates,
        ])
        self.assertEqual(
            [row for row in self.summaries() if "raider" in row[2] or "eliminat" in row[2]],
            [
                (2, "barbarian_uprising", "Pirate raiders rose up"),
                (3, "player_eliminated", "pi-gpt-test was eliminated"),
                (3, "barbarians_cleared", "Pirate raiders were wiped out"),
            ],
        )

    def test_a_great_wonder_is_reported_once_and_ordinary_buildings_never(self):
        self.write_journal("place-1", "place-2")
        # Bit 0 is the Palace (a small wonder); bits 1 and 2 are great wonders.
        self.write_save(1, [
            agent(cities=((7, 0, 0, "London", True, "100"),)), native(),
        ])
        self.write_save(2, [
            agent(cities=((7, 0, 0, "London", True, "110"),)), native(),
        ])
        self.write_save(3, [
            agent(cities=((7, 0, 0, "London", True, "111"),)), native(),
        ])
        self.write_save(4, [
            agent(cities=((7, 0, 0, "London", True, "111"),)), native(),
        ])
        self.assertEqual(self.summaries("wonder_completed"), [
            (2, "wonder_completed", "pi-gpt-test completed Pyramids in London"),
            (3, "wonder_completed", "pi-gpt-test completed Great Library in London"),
        ])

    def test_spaceship_progress_launch_and_arrival(self):
        self.write_journal("place-1", "place-2")
        self.write_save(1, [agent(), native()])
        self.write_save(2, [agent(spaceship=1, spaceship_parts=1), native()])
        self.write_save(3, [agent(spaceship=1, spaceship_parts=32), native()])
        self.write_save(4, [
            agent(spaceship=2, spaceship_parts=32, launch_year=1999), native(),
        ])
        self.write_save(
            5, [agent(spaceship=3, spaceship_parts=32), native()],
            kind="final", reason="Game over",
        )
        self.assertEqual(self.summaries(), [
            (2, "spaceship_started", "pi-gpt-test began building a spaceship"),
            (
                3, "spaceship_progress",
                "pi-gpt-test's spaceship reached 50% of its parts",
            ),
            (
                4, "spaceship_launched",
                "pi-gpt-test launched their spaceship (launch year 1999)",
            ),
            (5, "match_ended", "The match ended on turn 5"),
            (
                5, "spaceship_arrived",
                "pi-gpt-test's spaceship reached Alpha Centauri",
            ),
        ])

    def test_the_terminal_gameover_save_wins_its_turn(self):
        # The final save carries the aftermath the turn-boundary autosave for
        # the same turn cannot know about.
        self.write_journal("place-1", "place-2")
        self.write_save(1, [agent(), native()])
        self.write_save(2, [agent(), native()])
        self.write_save(
            2, [agent(alive=False), native()], kind="final", reason="Game over",
        )
        self.assertEqual(self.summaries(), [
            (2, "match_ended", "The match ended on turn 2"),
            (2, "player_eliminated", "pi-gpt-test was eliminated"),
        ])

    def test_output_is_deterministic_and_cached_without_rereading_saves(self):
        self.write_journal("place-1", "place-2")
        self.write_save(1, [agent(), native()])
        self.write_save(2, [agent(government="Anarchy"), native()])
        first = self.events()
        reads = []
        original = save_replay._read_stable_save

        def counting_read(path):
            reads.append(path.name)
            return original(path)

        with patch.object(save_replay, "_read_stable_save", counting_read):
            second = self.events()
        self.assertEqual(first, second)
        self.assertEqual(reads, [])
        self.assertEqual(first["last_turn"], 2)
        self.assertTrue(first["available"])
        self.assertEqual(first["event_counts"], {"government_changed": 1})

    def test_a_new_turn_extends_the_cache_instead_of_rereading_the_corpus(self):
        self.write_journal("place-1", "place-2")
        self.write_save(1, [agent(), native()])
        self.write_save(2, [agent(government="Anarchy"), native()])
        self.events()
        self.write_save(3, [agent(government="Republic"), native()])
        reads = []
        original = save_replay._read_stable_save

        def counting_read(path):
            reads.append(path.name)
            return original(path)

        with patch.object(save_replay, "_read_stable_save", counting_read):
            value = self.events()
        self.assertEqual(reads, ["turn-0003-auto.sav.gz"])
        self.assertEqual(
            [event["summary"] for event in value["events"]],
            [
                "pi-gpt-test began a revolution",
                "pi-gpt-test adopted Republic",
            ],
        )

    def test_a_rewritten_save_rederives_the_whole_log(self):
        self.write_journal("place-1", "place-2")
        self.write_save(1, [agent(), native()])
        self.write_save(2, [agent(government="Anarchy"), native()])
        self.assertEqual(len(self.events()["events"]), 1)
        os.utime(
            self.saves / "turn-0002-auto.sav.gz", ns=(10**18, 10**18),
        )
        self.write_save(2, [agent(government="Democracy"), native()])
        self.assertEqual(self.summaries("government_changed"), [(
            2, "government_changed",
            "pi-gpt-test switched from Despotism to Democracy",
        )])

    def test_a_derivation_change_invalidates_the_cache(self):
        # The cache is keyed on the saves and the seat labels; neither notices
        # that this module now weights or words them differently, so the
        # version stamp is the only thing standing between a changed
        # derivation and a stale log.
        self.write_journal("place-1", "place-2")
        self.write_save(1, [agent(), native()])
        self.write_save(2, [agent(government="Anarchy"), native()])
        self.events()
        cache_path = self.cache / GAME_ID / "events.json"
        stale = json.loads(cache_path.read_text(encoding="utf-8"))
        stale["cache_version"] = game_events.CACHE_VERSION - 1
        stale["events"] = [{
            "turn": 2, "kind": "city_founded", "summary": "stale row",
            "actors": [], "weight": 8, "data": {},
        }]
        cache_path.write_text(json.dumps(stale), encoding="utf-8")
        self.assertEqual(self.summaries(), [
            (2, "government_changed", "pi-gpt-test began a revolution"),
        ])

    def test_relabelled_seats_invalidate_the_cached_summaries(self):
        self.write_journal("place-1", "place-2")
        self.write_save(1, [agent(), native()])
        self.write_save(2, [agent(government="Anarchy"), native()])
        self.assertIn("pi-gpt-test", self.events()["events"][0]["summary"])
        relabelled = [dict(PLACES[0], controller_label="pi-claude-test"), PLACES[1]]
        value = events_from_autosaves(
            self.runs, GAME_ID, relabelled, cache_root=self.cache,
        )
        self.assertIn("pi-claude-test", value["events"][0]["summary"])

    def test_the_derivation_never_writes_beside_the_saves(self):
        self.write_journal("place-1", "place-2")
        self.write_save(1, [agent(), native()])
        self.write_save(2, [agent(government="Anarchy"), native()])
        before = sorted(path.name for path in self.saves.iterdir())
        self.events()
        self.assertEqual(sorted(path.name for path in self.saves.iterdir()), before)
        self.assertTrue((self.cache / GAME_ID / "events.json").is_file())

    def test_an_unreadable_save_is_skipped_with_a_public_warning(self):
        self.write_journal("place-1", "place-2")
        self.write_save(1, [agent(), native()])
        (self.saves / "turn-0002-auto.sav.gz").write_bytes(b"not a save")
        self.write_save(3, [agent(government="Anarchy"), native()])
        value = self.events()
        self.assertEqual(value["event_warnings"][0]["turn"], 2)
        self.assertNotIn(str(self.saves), value["event_warnings"][0]["message"])
        self.assertEqual(
            [event["turn"] for event in value["events"]], [3],
        )

    def test_a_capped_response_reports_what_it_dropped(self):
        self.write_journal("place-1", "place-2")
        self.write_save(1, [agent(cities=()), native(cities=())])
        cities = tuple(
            (index, index % 3, index // 3, f"City{index}", False, "000")
            for index in range(6)
        )
        self.write_save(2, [agent(cities=cities[:1]), native(cities=())])
        self.write_save(3, [
            agent(cities=cities[:1]),
            native(cities=cities[1:2], government="Republic"),
        ])
        value = self.events(limit=1)
        self.assertEqual(value["total_events"], 3)
        self.assertTrue(value["truncated"])
        self.assertEqual(len(value["events"]), 1)
        self.assertEqual(value["events"][0]["kind"], "government_changed")
        self.assertEqual(value["omitted_counts"], {"city_founded": 2})
        self.assertEqual(value["event_counts"]["city_founded"], 2)

    def test_every_event_carries_an_ordered_weight(self):
        self.write_journal("place-1", "place-2")
        london = (7, 0, 0, "London", True, "100")
        york = (8, 1, 0, "York", False, "100")
        self.write_save(1, [agent(cities=(london, york)), native(cities=())])
        self.write_save(2, [
            agent(cities=(york,)),
            native(cities=(london,), government="Anarchy"),
        ])
        events = self.events()["events"]
        weights = {event["kind"]: event["weight"] for event in events}
        self.assertTrue(all(
            1 <= event["weight"] <= game_events.MAX_WEIGHT for event in events
        ))
        # Taking a capital outranks the routine kinds around it.
        self.assertGreater(weights["city_captured"], weights["government_changed"])
        self.assertGreater(
            weights["city_captured"], game_events._BASE_WEIGHT["city_captured"],
        )
        # Inside a turn the heaviest event sorts first, so a consumer that
        # keeps one beat per window gets the right one.
        same_turn = [event for event in events if event["turn"] == 2]
        self.assertEqual(
            [event["weight"] for event in same_turn],
            sorted((event["weight"] for event in same_turn), reverse=True),
        )

    def test_a_first_city_outweighs_the_ones_that_follow(self):
        self.write_journal("place-1", "place-2")
        self.write_save(1, [agent(cities=()), native(cities=())])
        self.write_save(2, [
            agent(cities=((7, 0, 0, "London", True, "100"),)), native(),
        ])
        self.write_save(3, [
            agent(cities=(
                (7, 0, 0, "London", True, "100"), (8, 1, 0, "York", False, "100"),
            )), native(),
        ])
        first, second = self.events()["events"]
        self.assertTrue(first["data"]["first_city"])
        self.assertFalse(second["data"]["first_city"])
        self.assertGreater(first["weight"], second["weight"])

    def test_a_moved_capital_is_its_own_event(self):
        self.write_journal("place-1", "place-2")
        london = (7, 0, 0, "London", True, "100")
        york = (8, 1, 0, "York", False, "100")
        self.write_save(1, [agent(cities=(london, york)), native()])
        self.write_save(2, [
            agent(cities=((8, 1, 0, "York", True, "100"),)), native(),
        ])
        self.assertEqual(self.summaries("capital_moved"), [(
            2, "capital_moved",
            "pi-gpt-test moved their capital from London to York",
        )])

    def test_a_wonder_follows_its_city_and_dies_with_it(self):
        self.write_journal("place-1", "place-2")
        london_agent = (7, 0, 0, "London", True, "110")
        self.write_save(1, [agent(cities=(london_agent,)), native(cities=())])
        self.write_save(2, [
            agent(cities=()), native(cities=(london_agent,)),
        ])
        self.write_save(3, [agent(cities=()), native(cities=())])
        self.assertEqual(
            [row for row in self.summaries() if "Pyramids" in row[2]],
            [
                (
                    2, "wonder_captured",
                    "Italian (CPU) took Pyramids in London from pi-gpt-test",
                ),
                (
                    3, "wonder_destroyed",
                    "Pyramids was destroyed with Italian (CPU)'s London",
                ),
            ],
        )

    def test_a_scrapped_spaceship_programme_is_reported(self):
        self.write_journal("place-1", "place-2")
        self.write_save(1, [agent(spaceship=1, spaceship_parts=8), native()])
        self.write_save(2, [agent(spaceship=0), native()])
        self.assertEqual(self.summaries("spaceship_lost"), [(
            2, "spaceship_lost", "pi-gpt-test lost their spaceship programme",
        )])

    def test_a_decisive_lead_change_is_reported_and_flapping_is_not(self):
        self.write_journal("place-1", "place-2")

        def turn(number, agent_score, native_score):
            players = [agent(), native()]
            players[0].score = agent_score
            players[1].score = native_score
            self.write_save(number, players)

        turn(1, 100, 40)
        # A one-point overtake inside the reporting interval is noise.
        turn(2, 100, 101)
        turn(3, 101, 100)
        turn(40, 100, 400)
        self.assertEqual(self.summaries("lead_changed"), [(
            40, "lead_changed",
            "Italian (CPU) took the score lead from pi-gpt-test (400 to 100)",
        )])

    def test_a_score_surge_marks_a_step_change(self):
        self.write_journal("place-1", "place-2")

        def turn(number, agent_score):
            players = [agent(), native()]
            players[0].score = agent_score
            self.write_save(number, players)

        turn(1, 200)
        turn(2, 203)
        turn(3, 400)
        self.assertEqual(self.summaries("score_surge"), [(
            3, "score_surge", "pi-gpt-test's score jumped from 203 to 400",
        )])

    def test_invalid_arguments_are_rejected(self):
        for game_id in ("short", "../escape", 7):
            with self.subTest(game_id=game_id):
                with self.assertRaises(SaveReplayError):
                    events_from_autosaves(
                        self.runs, game_id, PLACES, cache_root=self.cache,
                    )
        for limit in (0, True, game_events.MAX_EVENTS + 1):
            with self.subTest(limit=limit):
                with self.assertRaises(SaveReplayError):
                    self.events(limit=limit)
        with self.assertRaises(SaveReplayError):
            events_from_autosaves(
                self.runs, GAME_ID, "not-a-sequence", cache_root=self.cache,
            )

    def test_a_game_without_saves_reports_nothing_available(self):
        value = self.events()
        self.assertFalse(value["available"])
        self.assertEqual(value["events"], [])
        self.assertEqual(value["total_events"], 0)


@unittest.skipUnless(
    (REAL_RUNS / "game_lnjNGLt9pq2ieqQjUqvSBE0x" / "saves").is_dir(),
    "recorded arena runs are not present in this checkout",
)
class RecordedRunEventsTests(unittest.TestCase):
    """The derivation must agree with the board data of two real matches."""

    GAME_ID = "game_lnjNGLt9pq2ieqQjUqvSBE0x"

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.cache = Path(self.temporary.name) / "cache"
        manifest = json.loads(
            (REAL_RUNS / self.GAME_ID / "manifest.json").read_text(encoding="utf-8"),
        )
        self.places = manifest["resolved_places"]
        self.value = events_from_autosaves(
            REAL_RUNS, self.GAME_ID, self.places, cache_root=self.cache,
            complete=True,
        )

    def tearDown(self):
        self.temporary.cleanup()

    def test_the_agent_seat_keeps_its_controller_label_all_match(self):
        self.assertTrue(self.value["available"])
        self.assertGreater(self.value["total_events"], 100)
        self.assertTrue(any(
            "pi-gpt-5.6-sol" in event["summary"] for event in self.value["events"]
        ))
        self.assertFalse(any(
            "Elizabeth" in event["summary"] for event in self.value["events"]
        ))

    def test_every_capture_matches_the_board_at_that_turn(self):
        captures = [
            event for event in self.value["events"]
            if event["kind"] == "city_captured"
        ]
        self.assertGreaterEqual(len(captures), 20)
        for event in captures[:4] + captures[-4:]:
            turn = event["turn"]
            before = save_replay.board_from_autosave(
                REAL_RUNS, self.GAME_ID, self.places, turn=turn - 1,
                cache_root=self.cache,
            )
            after = save_replay.board_from_autosave(
                REAL_RUNS, self.GAME_ID, self.places, turn=turn,
                cache_root=self.cache,
            )
            for name in event["data"]["cities"]:
                owner_before = next(
                    city["player_id"] for city in before["cities"]
                    if city["name"] == name
                )
                owner_after = next(
                    city["player_id"] for city in after["cities"]
                    if city["name"] == name
                )
                self.assertNotEqual(
                    owner_before, owner_after,
                    f"{name} did not change hands on turn {turn}",
                )

    def test_the_spaceship_launch_outweighs_every_founding(self):
        """The one number the panel and the film both rank by has to hold up.

        A 500-turn match founds hundreds of cities; if any of them scored as
        high as the launch, a density selection would open on expansion
        instead of on the moment the match was decided.
        """
        events = self.value["events"]
        launches = [
            event for event in events if event["kind"] == "spaceship_launched"
        ]
        foundings = [
            event["weight"] for event in events if event["kind"] == "city_founded"
        ]
        self.assertEqual(len(launches), 1)
        self.assertGreater(len(foundings), 100)
        self.assertGreater(launches[0]["weight"], max(foundings))
        # And the launch is the heaviest thing in its window of the match.
        late = [event for event in events if event["turn"] >= 450]
        self.assertEqual(
            max(late, key=lambda event: event["weight"])["kind"], "spaceship_launched",
        )

    def test_every_recorded_event_carries_a_usable_weight(self):
        weights = [event["weight"] for event in self.value["events"]]
        self.assertTrue(all(
            isinstance(weight, int) and 1 <= weight <= game_events.MAX_WEIGHT
            for weight in weights
        ))
        # Captures of a capital are the data-driven bump over ordinary ones.
        captures = [
            event for event in self.value["events"]
            if event["kind"] == "city_captured"
        ]
        capital = [
            event["weight"] for event in captures if event["data"]["capital_cities"]
        ]
        ordinary = [
            event["weight"] for event in captures if not event["data"]["capital_cities"]
        ]
        self.assertTrue(capital and ordinary)
        self.assertGreater(min(capital), max(ordinary))

    def test_the_derivation_is_stable_across_runs(self):
        repeated = events_from_autosaves(
            REAL_RUNS, self.GAME_ID, self.places, cache_root=self.cache,
            complete=True,
        )
        self.assertEqual(repeated, self.value)


if __name__ == "__main__":
    unittest.main()
