"""Derived game-event log reconstructed from a run's autosaves.

The event log answers "what actually happened this match" — wars, captured
cities, eliminations, revolutions, wonders, spaceships — as a list of
turn-stamped, viewer-ready rows.

There is exactly one savefile reader in this package.  Player rows and the
city ledger come from :mod:`agent_eval.save_replay` (its parser and its
per-turn cache), so this module never re-derives anything that reader
already publishes.  Three facts the replay payload deliberately omits are
read from the same section primitives on the same text: pairwise diplomatic
state, per-city improvement bits (great wonders), and spaceship progress.

The derived list is cached beside the replay cache under the same
source-signature discipline: every contributing save is fingerprinted, and a
run that only grew new turns resumes from the carried-over diff state instead
of re-reading the corpus.  Nothing is ever written into the run directory.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import stat
import tempfile
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence

from . import save_replay
from .save_replay import SaveReplayError, _UnreadableSave


SCHEMA_VERSION = 1
# Bump whenever the derivation changes — the weights, the taxonomy, the
# summaries, or the carried state shape.  The cache is keyed on the saves and
# the seat labels, neither of which notices that this module now reads them
# differently.
CACHE_VERSION = 5
MAX_EVENTS = 2000
MAX_NAMED_CITIES = 6
MAX_WARNINGS = 100
MIN_WEIGHT = 1
MAX_WEIGHT = 100

# How much of the match's story one event carries, on a 1-100 scale.  This is
# the field consumers select density with: a 4x film that wants one beat every
# 20 turns keeps the highest-weighted event in each window, a panel dims the
# low end, and an overflowing response drops from the bottom.  The base is per
# kind; the emitters adjust it for what actually happened (a captured capital
# outranks an ordinary city, a war that broke an alliance outranks a war that
# was always coming).
_BASE_WEIGHT = {
    "match_ended": 100,
    "player_eliminated": 96,
    "spaceship_arrived": 94,
    "spaceship_launched": 88,
    "war_declared": 80,
    "alliance_formed": 76,
    "spaceship_lost": 74,
    "peace_agreed": 72,
    "ceasefire_agreed": 66,
    "armistice_agreed": 64,
    "diplomacy_changed": 60,
    "city_captured": 52,
    "wonder_destroyed": 48,
    "wonder_captured": 44,
    "wonder_completed": 40,
    "lead_changed": 38,
    "capital_moved": 34,
    "spaceship_started": 32,
    "spaceship_progress": 30,
    # Razing a city is a real loss, and sits deliberately on the notable side
    # of the 30 boundary the viewer's "Key moments" stop selects with.
    "city_destroyed": 30,
    "barbarian_uprising": 24,
    "government_changed": 22,
    "first_contact": 20,
    "player_joined": 18,
    "barbarians_cleared": 16,
    "score_surge": 14,
    "city_founded": 8,
}

# A lead that flips back and forth while both sides score single digits is not
# a story beat.  A change is reported only once the leader is worth leading,
# has a real margin, and has not already been reported recently.
MIN_LEAD_SCORE = 50
MIN_LEAD_MARGIN = 5
MIN_LEAD_INTERVAL = 25
# One turn's score gain that is both large and a real fraction of the total.
MIN_SURGE_POINTS = 15
MIN_SURGE_RATIO = 1.08

# The diplomatic-state names Freeciv writes (common/player.h).
_DIPLOMACY_KIND = {
    "War": "war_declared",
    "Peace": "peace_agreed",
    "Cease-fire": "ceasefire_agreed",
    "Armistice": "armistice_agreed",
    "Alliance": "alliance_formed",
}
_UNMET = {"", "Never met", "No Contact"}
# States a player has to agree to; leaving one for war is a betrayal, and the
# weight says so.
_PACTS = {"Peace": 8, "Armistice": 8, "Cease-fire": 6, "Alliance": 12}

_SPACESHIP_KIND = {
    1: "spaceship_started",
    2: "spaceship_launched",
    3: "spaceship_arrived",
}
_SPACESHIP_PARTS = ("structurals", "components", "modules")
_SPACESHIP_PART_TOTAL = 32 + 16 + 12
_SPACESHIP_MILESTONES = (25, 50, 75, 100)

# Classic-ruleset great wonders (data/classic/buildings.ruleset,
# genus="GreatWonder").  The save records improvements as a bit vector against
# ``improvement_vector`` and never says which entries are wonders, so — as with
# the technology requirements in save_replay — the classic set is stated here
# and every other ruleset simply reports no wonder events.
_CLASSIC_GREAT_WONDERS = frozenset({
    "A.Smith's Trading Co.", "Apollo Program", "Colossus",
    "Copernicus' Observatory", "Cure For Cancer", "Darwin's Voyage",
    "Eiffel Tower", "Great Library", "Great Wall", "Hanging Gardens",
    "Hoover Dam", "Isaac Newton's College", "J.S. Bach's Cathedral",
    "King Richard's Crusade", "Leonardo's Workshop", "Lighthouse",
    "Magellan's Expedition", "Manhattan Project", "Marco Polo's Embassy",
    "Michelangelo's Chapel", "Oracle", "Pyramids", "SETI Program",
    "Shakespeare's Theater", "Statue of Liberty", "Sun Tzu's War Academy",
    "United Nations", "Women's Suffrage",
})


class _Faction:
    """One player's public identity for a single turn."""

    __slots__ = ("player_id", "actor", "label", "nation", "barbarian", "place")

    def __init__(
        self,
        player_id: int,
        actor: str,
        label: str,
        nation: str,
        barbarian: bool,
        place: int | None,
    ):
        self.player_id = player_id
        self.actor = actor
        self.label = label
        self.nation = nation
        self.barbarian = barbarian
        self.place = place

    @property
    def order(self) -> tuple[int, int, int]:
        return (
            0 if self.place is not None else 1,
            self.place if self.place is not None else 0,
            self.player_id,
        )


def _native_ai_label(nation: str) -> str:
    """What prose calls the built-in AI: "Spanish (CPU)".

    The same nation-first shape the viewer and the film use, so one side is
    named one way everywhere. The difficulty is the only part dropped: an
    event log is a short surface read many times, and "Spanish (CPU: Hard)"
    belongs where it is read once -- the title card and the standings.

    With no nation recorded this is a bare "CPU", never "(CPU)", so a summary
    cannot open on a parenthesis.
    """
    return f"{nation} (CPU)" if nation else "CPU"


def _faction_label(player: Mapping[str, Any], barbarian: bool) -> str:
    """The name a spectator should read, matching the viewer's naming."""
    controller = save_replay._public_text(player.get("controller_label"), 80).strip()
    nation = save_replay._public_text(player.get("nation"), 80).strip()
    name = save_replay._public_text(player.get("player_name"), 80).strip()
    if re.search(r"classic ai|deity ai", controller, re.IGNORECASE):
        return _native_ai_label(nation)
    if controller and not re.search(r"dynamic faction", controller, re.IGNORECASE):
        return controller
    if barbarian:
        return f"{nation} raiders" if nation else name or "Barbarians"
    return nation or name or f"Player {player.get('player_id')}"


def _faction_actor(player: Mapping[str, Any]) -> str:
    seat_id = player.get("seat_id")
    if isinstance(seat_id, str) and not seat_id.startswith(("dynamic-player-", "raw-")):
        return seat_id
    return save_replay._public_text(player.get("player_name"), 80) or str(
        player.get("player_id"),
    )


def _named(cities: Sequence[str]) -> str:
    shown = ", ".join(cities[:MAX_NAMED_CITIES])
    remainder = len(cities) - MAX_NAMED_CITIES
    return f"{shown} and {remainder} more" if remainder > 0 else shown


def _pair_key(first: int, second: int) -> str:
    low, high = sorted((first, second))
    return f"{low}:{high}"


def _supplement(
    sections: Mapping[str, Sequence[str]], player_count: int,
) -> dict[str, Any]:
    """Read the public facts the replay payload does not carry.

    Diplomatic state, per-city great-wonder bits, and spaceship progress are
    all parsed with save_replay's own section/table primitives, on text that
    reader validated, so no second understanding of the save format exists.
    """
    savefile = save_replay._scalars(sections["savefile"])
    ruleset = save_replay._normalize_ruleset_name(savefile.get("rulesetdir", ""))
    improvements: list[str] = []
    if ruleset == "classic":
        try:
            improvements = [
                save_replay._normalize_ruleset_name(name)
                for name in save_replay._csv_row(savefile.get("improvement_vector", ""))
            ]
        except _UnreadableSave:
            improvements = []
        if len(improvements) != save_replay._integer(
            savefile.get("improvement_size"), -1,
        ):
            improvements = []

    players: dict[int, dict[str, Any]] = {}
    for player_id in range(player_count):
        lines = sections.get(f"player{player_id}")
        if lines is None:
            continue
        scalars = save_replay._scalars(lines)
        diplomacy: list[str] = []
        try:
            diplomacy = [
                save_replay._public_text(row.get("current", ""), 32)
                for row in save_replay._rows_by_header(
                    save_replay._table(lines, "diplstate"),
                )
            ]
        except _UnreadableSave:
            diplomacy = []
        spaceship = {
            "state": save_replay._integer(scalars.get("spaceship.state"), 0) or 0,
            "launch_year": save_replay._integer(scalars.get("spaceship.launch_year")),
            "parts": sum(
                save_replay._integer(scalars.get(f"spaceship.{part}"), 0) or 0
                for part in _SPACESHIP_PARTS
            ),
        }
        players[player_id] = {
            "barbarian": save_replay._public_text(
                scalars.get("ai.barb_type", "None"), 32,
            ) not in {"", "None"},
            "diplomacy": diplomacy,
            "spaceship": spaceship,
            "wonders": _player_wonders(lines, scalars, improvements),
        }
    return {
        "reason": save_replay._public_text(savefile.get("reason", ""), 40),
        # Without a readable improvement catalog every wonder would look
        # destroyed this turn, so the wonder diff is skipped instead.
        "wonders_readable": bool(improvements),
        "players": players,
    }


def _player_wonders(
    lines: Sequence[str],
    scalars: Mapping[str, str],
    improvements: Sequence[str],
) -> list[list[str]]:
    """[[city name, wonder name], ...] for great wonders standing this turn."""
    if not improvements or not save_replay._integer(scalars.get("ncities"), 0):
        return []
    try:
        rows = save_replay._rows_by_header(save_replay._table(lines, "c"))
    except _UnreadableSave:
        return []
    standing: list[list[str]] = []
    for row in rows:
        bits = row.get("improvements", "")
        city = save_replay._public_text(row.get("name", ""), 80)
        if not isinstance(bits, str) or len(bits) != len(improvements) or not city:
            continue
        standing.extend(
            [city, improvements[index]]
            for index, bit in enumerate(bits)
            if bit == "1" and improvements[index] in _CLASSIC_GREAT_WONDERS
        )
    return standing


def _empty_state() -> dict[str, Any]:
    return {
        "cities": {},
        "capitals": {},
        "diplomacy": {},
        "government": {},
        "alive": {},
        "scores": {},
        "lead": [],
        # wonder name -> [holding player id, holding city]
        "wonders": {},
        "spaceship": {},
        "turn": 0,
    }


def _event(
    turn: int,
    kind: str,
    summary: str,
    actors: Iterable[str],
    data: Mapping[str, Any],
    *,
    weight: int | None = None,
) -> dict[str, Any]:
    base = _BASE_WEIGHT.get(kind, 10) if weight is None else weight
    return {
        "turn": turn,
        "kind": kind,
        "summary": save_replay._public_text(summary, 240),
        "actors": [save_replay._public_text(actor, 80) for actor in actors],
        "weight": max(MIN_WEIGHT, min(MAX_WEIGHT, base)),
        "data": dict(data),
    }


def _diplomacy_events(
    turn: int,
    factions: Mapping[int, _Faction],
    supplement: Mapping[str, Any],
    state: dict[str, Any],
    seeding: bool,
) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    previous: dict[str, str] = state["diplomacy"]
    for player_id, faction in sorted(factions.items()):
        rows = supplement["players"].get(player_id, {}).get("diplomacy", [])
        for other_id, current in enumerate(rows):
            other = factions.get(other_id)
            if other is None or other_id <= player_id:
                continue
            key = _pair_key(player_id, other_id)
            before = previous.get(key)
            previous[key] = current
            if seeding or before is None or before == current:
                continue
            first, second = sorted((faction, other), key=lambda item: item.order)
            data = {
                "from": before,
                "to": current,
                "factions": [first.label, second.label],
            }
            actors = [first.actor, second.actor]
            if before in _UNMET:
                # Classic rules open every relationship at war, so a contact
                # that lands on War is the declaration; say so rather than
                # filing the match's opening hostilities under "first contact".
                data["first_contact"] = True
                if current == "War":
                    events.append(_event(
                        turn, "war_declared",
                        f"{first.label} met {second.label} — no treaty, at war",
                        actors, data,
                    ))
                else:
                    events.append(_event(
                        turn, "first_contact",
                        f"{first.label} and {second.label} made first contact",
                        actors, data,
                    ))
                continue
            kind = _DIPLOMACY_KIND.get(current, "diplomacy_changed")
            # A pact the two sides had to agree to is now over.  The kind still
            # names the state they moved into — that is what a consumer filters
            # on — and the weight and the prose carry the betrayal.
            broken = _PACTS.get(before) if current != before else None
            if broken is not None:
                data["broke_pact"] = before
            bonus = broken or 0
            if kind == "war_declared":
                summary = (
                    f"{first.label} and {second.label} broke their "
                    f"{before.lower()} — war"
                    if broken is not None else
                    f"War broke out between {first.label} and {second.label}"
                )
            elif kind == "peace_agreed":
                summary = f"{first.label} and {second.label} signed peace"
            elif kind == "ceasefire_agreed":
                summary = f"{first.label} and {second.label} agreed a cease-fire"
            elif kind == "armistice_agreed":
                summary = f"{first.label} and {second.label} entered an armistice"
            elif kind == "alliance_formed":
                summary = f"{first.label} and {second.label} formed an alliance"
            else:
                summary = (
                    f"{first.label} and {second.label} moved from "
                    f"{before} to {current}"
                )
            events.append(_event(
                turn, kind, summary, actors, data,
                weight=_BASE_WEIGHT[kind] + bonus,
            ))
    return events


def _life_events(
    turn: int,
    factions: Mapping[int, _Faction],
    alive_now: Mapping[int, bool],
    state: dict[str, Any],
    seeding: bool,
) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    alive: dict[str, bool] = state["alive"]
    for player_id, faction in sorted(factions.items()):
        current = bool(alive_now.get(player_id, False))
        key = str(player_id)
        before = alive.get(key)
        alive[key] = current
        if seeding or before == current:
            continue
        data = {"faction": faction.label, "nation": faction.nation}
        if current and faction.barbarian:
            events.append(_event(
                turn, "barbarian_uprising", f"{faction.label} rose up",
                [faction.actor], data,
            ))
        elif current:
            events.append(_event(
                turn, "player_joined", f"{faction.label} entered the world",
                [faction.actor], data,
            ))
        elif faction.barbarian:
            events.append(_event(
                turn, "barbarians_cleared", f"{faction.label} were wiped out",
                [faction.actor], data,
            ))
        else:
            events.append(_event(
                turn, "player_eliminated",
                f"{faction.label} was eliminated", [faction.actor], data,
            ))
    return events


def _government_events(
    turn: int,
    factions: Mapping[int, _Faction],
    governments: Mapping[int, str],
    state: dict[str, Any],
    seeding: bool,
) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    known: dict[str, str] = state["government"]
    for player_id, faction in sorted(factions.items()):
        current = governments.get(player_id, "")
        key = str(player_id)
        before = known.get(key)
        known[key] = current
        if seeding or before is None or before == current or not current:
            continue
        if current == "Anarchy":
            summary = f"{faction.label} began a revolution"
        elif before == "Anarchy":
            summary = f"{faction.label} adopted {current}"
        else:
            summary = f"{faction.label} switched from {before} to {current}"
        events.append(_event(
            turn, "government_changed", summary, [faction.actor],
            {"from": before, "to": current, "faction": faction.label},
            weight=_BASE_WEIGHT["government_changed"] - (4 if current == "Anarchy" else 0),
        ))
    return events


def _city_events(
    turn: int,
    factions: Mapping[int, _Faction],
    cities: Sequence[Mapping[str, Any]],
    state: dict[str, Any],
    seeding: bool,
) -> list[dict[str, Any]]:
    previous: dict[str, list[Any]] = state["cities"]
    current = {
        str(city["id"]): [
            int(city["player_id"]),
            save_replay._public_text(city.get("name", ""), 80),
            bool(city.get("capital")),
        ]
        for city in cities
        if isinstance(city.get("id"), int) and isinstance(city.get("player_id"), int)
    }
    state["cities"] = current
    # Seed the palace positions even on the first turn, so the first real move
    # is a move and not a first sighting.
    capital_events = _capital_events(turn, factions, current, state, seeding)
    if seeding:
        return []

    founded: dict[int, list[str]] = {}
    captured: dict[tuple[int, int], list[tuple[str, bool]]] = {}
    razed: dict[int, list[str]] = {}
    for city_id, (owner, name, _capital) in current.items():
        before = previous.get(city_id)
        if before is None:
            founded.setdefault(owner, []).append(name)
        elif before[0] != owner:
            captured.setdefault((owner, int(before[0])), []).append(
                (name, bool(before[2])),
            )
    for city_id, (owner, name, _capital) in previous.items():
        if city_id not in current:
            razed.setdefault(int(owner), []).append(name)

    held_before: dict[int, int] = {}
    for owner, _name, _capital in previous.values():
        held_before[int(owner)] = held_before.get(int(owner), 0) + 1

    events: list[dict[str, Any]] = []
    for owner, names in sorted(founded.items()):
        faction = factions.get(owner)
        if faction is None:
            continue
        names.sort()
        first_city = not held_before.get(owner)
        summary = (
            f"{faction.label} founded {names[0]}" if len(names) == 1
            else f"{faction.label} founded {len(names)} cities: {_named(names)}"
        )
        events.append(_event(
            turn, "city_founded", summary, [faction.actor],
            {"cities": names, "faction": faction.label, "first_city": first_city},
            weight=_BASE_WEIGHT["city_founded"] + (10 if first_city else 0),
        ))
    for (owner, victim), taken in sorted(captured.items()):
        winner = factions.get(owner)
        loser = factions.get(victim)
        if winner is None or loser is None:
            continue
        taken.sort()
        names = [name for name, _capital in taken]
        taken_capital = [name for name, capital in taken if capital]
        if len(names) == 1:
            noun = "the capital " if taken_capital else ""
            summary = f"{winner.label} captured {noun}{names[0]} from {loser.label}"
        else:
            summary = (
                f"{winner.label} captured {len(names)} cities from "
                f"{loser.label}: {_named(names)}"
            )
        events.append(_event(
            turn, "city_captured", summary, [winner.actor, loser.actor],
            {
                "cities": names,
                "capital_cities": taken_capital,
                "captor": winner.label,
                "loser": loser.label,
            },
            weight=(
                _BASE_WEIGHT["city_captured"]
                + (14 if taken_capital else 0)
                + min(8, 2 * (len(names) - 1))
            ),
        ))
    for owner, names in sorted(razed.items()):
        faction = factions.get(owner)
        label = faction.label if faction else "an unknown faction"
        names.sort()
        summary = (
            f"{label}'s city {names[0]} was destroyed" if len(names) == 1
            else f"{len(names)} of {label}'s cities were destroyed: {_named(names)}"
        )
        events.append(_event(
            turn, "city_destroyed", summary,
            [faction.actor] if faction else [], {"cities": names, "faction": label},
            weight=_BASE_WEIGHT["city_destroyed"] + min(8, 2 * (len(names) - 1)),
        ))
    events.extend(capital_events)
    return events


def _capital_events(
    turn: int,
    factions: Mapping[int, _Faction],
    cities: Mapping[str, list[Any]],
    state: dict[str, Any],
    seeding: bool,
) -> list[dict[str, Any]]:
    """A palace that moved — usually because the old capital just fell."""
    seats: dict[str, str] = {}
    for owner, name, capital in cities.values():
        if capital:
            seats.setdefault(str(int(owner)), name)
    known: dict[str, str] = state["capitals"]
    events: list[dict[str, Any]] = []
    for player_id, faction in sorted(factions.items()):
        key = str(player_id)
        current = seats.get(key)
        before = known.get(key)
        if current is None:
            # A player with no capital right now (eliminated, or between
            # palaces) keeps its last known seat rather than reporting a move.
            continue
        known[key] = current
        if seeding or before is None or before == current:
            continue
        events.append(_event(
            turn, "capital_moved",
            f"{faction.label} moved their capital from {before} to {current}",
            [faction.actor],
            {"from": before, "to": current, "faction": faction.label},
        ))
    return events


def _wonder_events(
    turn: int,
    factions: Mapping[int, _Faction],
    supplement: Mapping[str, Any],
    state: dict[str, Any],
    seeding: bool,
) -> list[dict[str, Any]]:
    """Every great wonder built, taken with its city, or lost with it."""
    if not supplement.get("wonders_readable"):
        return []
    known: dict[str, list[Any]] = state["wonders"]
    standing_now: dict[str, list[Any]] = {}
    for player_id in sorted(factions):
        for entry in supplement["players"].get(player_id, {}).get("wonders", []):
            if len(entry) == 2:
                standing_now.setdefault(entry[1], [player_id, entry[0]])

    events: list[dict[str, Any]] = []
    for wonder, (player_id, city) in sorted(standing_now.items()):
        faction = factions.get(player_id)
        before = known.get(wonder)
        if before is None:
            if not seeding and faction is not None:
                events.append(_event(
                    turn, "wonder_completed",
                    f"{faction.label} completed {wonder} in {city}", [faction.actor],
                    {"wonder": wonder, "city": city, "faction": faction.label},
                ))
        elif int(before[0]) != player_id and not seeding:
            loser = factions.get(int(before[0]))
            if faction is not None:
                events.append(_event(
                    turn, "wonder_captured",
                    f"{faction.label} took {wonder} in {city}"
                    + (f" from {loser.label}" if loser is not None else ""),
                    [faction.actor] + ([loser.actor] if loser is not None else []),
                    {
                        "wonder": wonder, "city": city, "captor": faction.label,
                        "loser": loser.label if loser is not None else None,
                    },
                ))
    for wonder, (player_id, city) in sorted(known.items()):
        if wonder in standing_now or seeding:
            continue
        faction = factions.get(int(player_id))
        label = faction.label if faction is not None else "an unknown faction"
        events.append(_event(
            turn, "wonder_destroyed",
            f"{wonder} was destroyed with {label}'s {city}",
            [faction.actor] if faction is not None else [],
            {"wonder": wonder, "city": city, "faction": label},
        ))
    state["wonders"] = standing_now
    return events


def _spaceship_events(
    turn: int,
    factions: Mapping[int, _Faction],
    supplement: Mapping[str, Any],
    state: dict[str, Any],
    seeding: bool,
) -> list[dict[str, Any]]:
    tracked: dict[str, list[int]] = state["spaceship"]
    events: list[dict[str, Any]] = []
    for player_id, faction in sorted(factions.items()):
        ship = supplement["players"].get(player_id, {}).get("spaceship")
        if not isinstance(ship, Mapping):
            continue
        key = str(player_id)
        before = tracked.get(key, [0, 0])
        ship_state = int(ship.get("state") or 0)
        percent = min(
            100, round(100 * int(ship.get("parts") or 0) / _SPACESHIP_PART_TOTAL),
        )
        milestone = max(
            (value for value in _SPACESHIP_MILESTONES if percent >= value),
            default=0,
        )
        tracked[key] = [ship_state, max(int(before[1]), milestone)]
        if seeding:
            continue
        if ship_state > int(before[0]) and ship_state in _SPACESHIP_KIND:
            kind = _SPACESHIP_KIND[ship_state]
            launch_year = ship.get("launch_year")
            if kind == "spaceship_started":
                summary = f"{faction.label} began building a spaceship"
            elif kind == "spaceship_launched":
                summary = f"{faction.label} launched their spaceship"
                if isinstance(launch_year, int):
                    summary += f" (launch year {launch_year})"
            else:
                summary = f"{faction.label}'s spaceship reached Alpha Centauri"
            events.append(_event(
                turn, kind, summary, [faction.actor],
                {
                    "faction": faction.label,
                    "state": ship_state,
                    "percent_complete": percent,
                    "launch_year": launch_year,
                },
            ))
        elif ship_state == 0 and int(before[0]) > 0:
            # Losing the capital scraps the programme outright.
            events.append(_event(
                turn, "spaceship_lost",
                f"{faction.label} lost their spaceship programme", [faction.actor],
                {"faction": faction.label, "from_state": int(before[0])},
            ))
        elif milestone > int(before[1]) and ship_state == 1:
            events.append(_event(
                turn, "spaceship_progress",
                f"{faction.label}'s spaceship reached {milestone}% of its parts",
                [faction.actor],
                {
                    "faction": faction.label,
                    "percent_complete": percent,
                    "milestone": milestone,
                },
                weight=_BASE_WEIGHT["spaceship_progress"] + (6 if milestone == 100 else 0),
            ))
    return events


def _score_events(
    turn: int,
    factions: Mapping[int, _Faction],
    players: Sequence[Mapping[str, Any]],
    state: dict[str, Any],
    seeding: bool,
) -> list[dict[str, Any]]:
    """Synthetic markers over the score curve: who leads, and who surged."""
    scores: dict[str, int] = state["scores"]
    events: list[dict[str, Any]] = []
    standings: list[tuple[int, int]] = []
    for player in players:
        player_id = player.get("player_id")
        score = player.get("score")
        if not isinstance(player_id, int) or isinstance(player_id, bool):
            continue
        if not isinstance(score, int) or isinstance(score, bool):
            continue
        faction = factions.get(player_id)
        key = str(player_id)
        before = scores.get(key)
        scores[key] = score
        if player.get("alive") and score > 0:
            standings.append((score, player_id))
        if (
            seeding or faction is None or before is None or before <= 0
            or score - before < MIN_SURGE_POINTS
            or score < before * MIN_SURGE_RATIO
        ):
            continue
        events.append(_event(
            turn, "score_surge",
            f"{faction.label}'s score jumped from {before} to {score}",
            [faction.actor],
            {"from": before, "to": score, "faction": faction.label},
        ))

    lead: list[Any] = state["lead"]
    if not standings:
        return events
    top_score, top_id = max(standings, key=lambda row: (row[0], -row[1]))
    runner_up = max(
        (score for score, player_id in standings if player_id != top_id), default=0,
    )
    before_id = lead[0] if lead else None
    reported_at = int(lead[1]) if len(lead) > 1 else -MIN_LEAD_INTERVAL
    changed = (
        not seeding and before_id is not None and before_id != top_id
        and top_score >= MIN_LEAD_SCORE
        and top_score - runner_up >= MIN_LEAD_MARGIN
        and turn - reported_at >= MIN_LEAD_INTERVAL
    )
    state["lead"] = [top_id, turn if changed else reported_at]
    faction = factions.get(top_id)
    if changed and faction is not None:
        passed = factions.get(int(before_id)) if before_id is not None else None
        events.append(_event(
            turn, "lead_changed",
            f"{faction.label} took the score lead"
            + (f" from {passed.label}" if passed is not None else "")
            + f" ({top_score} to {runner_up})",
            [faction.actor] + ([passed.actor] if passed is not None else []),
            {
                "faction": faction.label, "score": top_score,
                "runner_up": runner_up,
                "passed": passed.label if passed is not None else None,
            },
        ))
    return events


def _turn_factions(
    snapshot: Mapping[str, Any], supplement: Mapping[str, Any],
) -> dict[int, _Faction]:
    factions: dict[int, _Faction] = {}
    for player in snapshot.get("players", []):
        player_id = player.get("player_id")
        if not isinstance(player_id, int) or isinstance(player_id, bool):
            continue
        barbarian = bool(
            supplement["players"].get(player_id, {}).get("barbarian", False),
        )
        place = player.get("place")
        factions[player_id] = _Faction(
            player_id=player_id,
            actor=_faction_actor(player),
            label=_faction_label(player, barbarian),
            nation=save_replay._public_text(player.get("nation"), 80),
            barbarian=barbarian,
            place=place if isinstance(place, int) and not isinstance(place, bool) else None,
        )
    return factions


def _turn_events(
    turn: int,
    parsed: Mapping[str, Any],
    supplement: Mapping[str, Any],
    snapshot: Mapping[str, Any],
    state: dict[str, Any],
    seeding: bool,
) -> list[dict[str, Any]]:
    factions = _turn_factions(snapshot, supplement)
    players = snapshot.get("players", [])
    alive_now = {
        player["player_id"]: bool(player.get("alive"))
        for player in players if isinstance(player.get("player_id"), int)
    }
    governments = {
        player["player_id"]: save_replay._public_text(player.get("government"), 40)
        for player in players if isinstance(player.get("player_id"), int)
    }
    board = parsed.get("board")
    cities = board.get("cities", []) if isinstance(board, Mapping) else None

    events: list[dict[str, Any]] = []
    events.extend(_life_events(turn, factions, alive_now, state, seeding))
    events.extend(_diplomacy_events(turn, factions, supplement, state, seeding))
    events.extend(_government_events(turn, factions, governments, state, seeding))
    if cities is not None:
        events.extend(_city_events(turn, factions, cities, state, seeding))
    events.extend(_wonder_events(turn, factions, supplement, state, seeding))
    events.extend(_spaceship_events(turn, factions, supplement, state, seeding))
    events.extend(_score_events(turn, factions, players, state, seeding))
    if supplement.get("reason") == "Game over":
        events.append(_event(
            turn, "match_ended", f"The match ended on turn {turn}", [],
            {"reason": "Game over"},
        ))
    state["turn"] = turn
    return events


def _sort_events(events: Sequence[Mapping[str, Any]]) -> list[dict[str, Any]]:
    """Chronological, heaviest first inside a turn, then stable by text."""
    return [dict(event) for event in sorted(events, key=lambda event: (
        event["turn"],
        -event["weight"],
        event["kind"],
        event["summary"],
    ))]


def _places_digest(resolved_places: Sequence[Mapping[str, Any]]) -> str:
    rows = [
        {
            key: row.get(key) for key in (
                "seat_id", "place", "player_name", "controller_label",
                "controller_type", "model",
            )
        }
        for row in resolved_places if isinstance(row, Mapping)
    ]
    payload = json.dumps(rows, sort_keys=True, ensure_ascii=False, default=str)
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def _events_cache_path(cache_directory: Path) -> Path:
    return cache_directory / "events.json"


def _load_events_cache(
    path: Path, game_id: str, places_digest: str,
) -> dict[str, Any] | None:
    try:
        info = path.lstat()
        if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
            return None
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        return None
    if (
        not isinstance(value, dict)
        or value.get("cache_version") != CACHE_VERSION
        or value.get("schema_version") != SCHEMA_VERSION
        or value.get("game_id") != game_id
        or value.get("places_digest") != places_digest
        or not isinstance(value.get("events"), list)
        or not isinstance(value.get("state"), dict)
        or not isinstance(value.get("sources"), list)
    ):
        return None
    if any(
        not isinstance(row, list) or len(row) != 8
        or not isinstance(row[0], int) or not isinstance(row[1], str)
        or any(not isinstance(item, int) for item in row[2:])
        for row in value["sources"]
    ):
        return None
    if any(
        not isinstance(event, dict)
        or not isinstance(event.get("turn"), int)
        or not isinstance(event.get("kind"), str)
        or not isinstance(event.get("summary"), str)
        or not isinstance(event.get("actors"), list)
        or not isinstance(event.get("data"), dict)
        or not isinstance(event.get("weight"), int)
        or isinstance(event.get("weight"), bool)
        or not MIN_WEIGHT <= event["weight"] <= MAX_WEIGHT
        for event in value["events"]
    ):
        return None
    state = value["state"]
    if any(key not in state for key in _empty_state()):
        return None
    return value


def _write_events_cache(path: Path, payload: Mapping[str, Any]) -> None:
    temporary_name: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=path.parent,
            prefix=".events-", suffix=".tmp", delete=False,
        ) as stream:
            temporary_name = stream.name
            os.chmod(stream.name, 0o600)
            json.dump(
                payload, stream, ensure_ascii=False, separators=(",", ":"),
                sort_keys=True,
            )
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_name, path)
        temporary_name = None
    except OSError:
        pass
    finally:
        if temporary_name is not None:
            try:
                os.unlink(temporary_name)
            except OSError:
                pass


def _read_turn(
    sources: Sequence[Path], turn: int, game_id: str, cache_directory: Path,
) -> tuple[
    dict[str, Any] | None, dict[str, Any] | None, list[Any] | None, str | None,
]:
    """(parsed, supplement, source signature, warning) for one turn.

    This walks save_replay's own cache the way its loader does rather than
    calling that loader, so a save is decompressed exactly once: the same text
    feeds the shared parser (on a cache miss) and the supplement below.
    """
    warning: str | None = None
    for source in sources:
        try:
            info = source.lstat()
            if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
                raise _UnreadableSave()
            cache_path = save_replay._cache_path(cache_directory, turn, source.name)
            parsed = save_replay._load_cache(
                cache_path, source.name, save_replay._stat_signature(info), game_id,
            )
            text, signature = save_replay._read_stable_save(source)
            if parsed is None:
                parsed = save_replay._parse_save(text, game_id, turn)
                save_replay._write_cache(
                    cache_path, source.name, signature, game_id, parsed,
                )
            sections = save_replay._sections(text)
            player_count = save_replay._integer(
                save_replay._scalars(sections["players"]).get("nplayers"), 0,
            ) or 0
            supplement = _supplement(sections, player_count)
        except (_UnreadableSave, OSError, KeyError) as exc:
            warning = getattr(
                exc, "public_message",
                "An autosave was incomplete or unreadable and was skipped.",
            )
            continue
        return parsed, supplement, [turn, source.name, *signature], None
    return None, None, None, (
        warning or "An autosave was incomplete or unreadable and was skipped."
    )


def events_from_autosaves(
    runs_root: str | os.PathLike[str],
    game_id: str,
    resolved_places: Sequence[Mapping[str, Any]] = (),
    *,
    limit: int = MAX_EVENTS,
    cache_root: str | os.PathLike[str] | None = None,
    complete: bool = False,
) -> dict[str, Any]:
    """Return the derived, public, deterministic event log for one game.

    A run that only gained turns since the cached derivation resumes from the
    carried diff state; anything else (a rewritten save, a new terminal
    ``-final`` save, different seat labels) re-derives the whole log.
    """
    if not isinstance(game_id, str) or not save_replay.GAME_ID_RE.fullmatch(game_id):
        raise SaveReplayError("Invalid game id.")
    if isinstance(limit, bool) or not isinstance(limit, int) or not 1 <= limit <= MAX_EVENTS:
        raise SaveReplayError(f"limit must be in [1, {MAX_EVENTS}].")
    if not isinstance(resolved_places, Sequence) or isinstance(resolved_places, (str, bytes)):
        raise SaveReplayError("resolved_places must be a sequence.")

    runs_path = Path(runs_root)
    source_directory = save_replay._source_directory(runs_path, game_id)
    cache_directory = save_replay._cache_directory(
        runs_path, game_id, source_directory,
        Path(cache_root) if cache_root is not None else None,
    )
    sources_by_turn = save_replay._discover_saves(source_directory)
    journal_seats = save_replay._journal_seat_ids(source_directory)
    places_digest = _places_digest(resolved_places)
    cache_path = _events_cache_path(cache_directory)
    cached = _load_events_cache(cache_path, game_id, places_digest)

    events: list[dict[str, Any]] = []
    sources: list[list[Any]] = []
    warnings: list[dict[str, Any]] = []
    state = _empty_state()
    pending = list(sources_by_turn)
    if cached is not None:
        cached_sources = [list(row) for row in cached["sources"]]
        turns = [int(row[0]) for row in cached_sources]
        if turns == pending[:len(turns)]:
            # Every cached turn still resolves to the byte-identical save it
            # was derived from: resume instead of re-reading the corpus.
            events = [dict(event) for event in cached["events"]]
            sources = cached_sources
            state = dict(cached["state"])
            pending = pending[len(turns):]
            warnings = [
                row for row in cached.get("warnings", [])
                if isinstance(row, dict) and isinstance(row.get("message"), str)
            ]

    seeded = bool(sources)
    verified = 0
    for row in sources:
        turn = int(row[0])
        candidates = sources_by_turn.get(turn, [])
        current = next(
            (item for item in candidates if item.name == row[1]), None,
        )
        if current is None:
            break
        try:
            signature = save_replay._stat_signature(current.lstat())
        except OSError:
            break
        if list(signature) != [int(item) for item in row[2:]]:
            break
        verified += 1
    if verified != len(sources):
        # A contributing save changed underneath the derivation.
        events, sources, warnings, state, seeded = [], [], [], _empty_state(), False
        pending = list(sources_by_turn)

    for turn in pending:
        parsed, supplement, signature, warning = _read_turn(
            sources_by_turn[turn], turn, game_id, cache_directory,
        )
        if parsed is None or supplement is None or signature is None:
            warnings.append({"turn": turn, "message": warning})
            continue
        snapshot = save_replay._enrich_snapshot(
            parsed["snapshot"], resolved_places, {}, journal_seats,
        )
        events.extend(_turn_events(
            turn, parsed, supplement, snapshot, state, seeding=not seeded,
        ))
        sources.append(signature)
        seeded = True

    ordered = _sort_events(events)
    unique_warnings = {
        (row.get("turn"), row["message"]): row for row in warnings
    }
    sanitized_warnings = sorted(
        unique_warnings.values(),
        key=lambda row: (
            row.get("turn") is None,
            row.get("turn") if row.get("turn") is not None else 0,
            row["message"],
        ),
    )[-MAX_WARNINGS:]

    _write_events_cache(cache_path, {
        "cache_version": CACHE_VERSION,
        "schema_version": SCHEMA_VERSION,
        "game_id": game_id,
        "places_digest": places_digest,
        "sources": sources,
        "events": ordered,
        "state": state,
        "warnings": sanitized_warnings,
    })

    counts: dict[str, int] = {}
    for event in ordered:
        counts[event["kind"]] = counts.get(event["kind"], 0) + 1
    selected, omitted = _apply_limit(ordered, limit)
    return {
        "schema_version": SCHEMA_VERSION,
        "game_id": game_id,
        "available": bool(sources),
        "events": selected,
        "event_counts": counts,
        "total_events": len(ordered),
        "truncated": bool(omitted),
        "omitted_counts": omitted,
        # The lightest weight that survived the cap, so a consumer can say what
        # the response is a view of rather than guessing.
        "min_included_weight": min(
            (event["weight"] for event in selected), default=0,
        ),
        "last_turn": int(state["turn"]),
        "complete": bool(complete),
        "event_warnings": sanitized_warnings,
    }


def _apply_limit(
    events: Sequence[Mapping[str, Any]], limit: int,
) -> tuple[list[dict[str, Any]], dict[str, int]]:
    """Keep the heaviest ``limit`` rows, reporting what was dropped."""
    if len(events) <= limit:
        return [dict(event) for event in events], {}
    ranked = sorted(enumerate(events), key=lambda item: (
        -item[1]["weight"],
        item[0],
    ))
    keep = {index for index, _event in ranked[:limit]}
    omitted: dict[str, int] = {}
    for index, event in ranked[limit:]:
        omitted[event["kind"]] = omitted.get(event["kind"], 0) + 1
    return [dict(event) for index, event in enumerate(events) if index in keep], omitted


__all__ = ["MAX_EVENTS", "SCHEMA_VERSION", "events_from_autosaves"]
