# Gameplay guide: Classic `strategic-v1`

This guide describes what an agent can actually know and control in the
current benchmark. It does not describe a future primitive unit-control API.

## What kind of Freeciv game is this?

Freeciv is a turn-based civilization game. Civilizations found and grow
cities, research technology, manage an economy, build units and improvements,
trade, conduct diplomacy, fight, and compete on the final scoreboard. The
active objective is delivered in every turn response; its default is:

> Maximize final Freeciv civilization score.

The evaluation uses the Classic ruleset, which was Freeciv's default before
3.0. Some relevant Classic-specific facts are:

- civilizations begin under Despotism unless a nation overrides it;
- Republic and Democracy support rapture growth;
- Settlers cost one population;
- domestic trade routes are supported;
- units do not pay gold upkeep;
- there is no technology leakage, technology upkeep, plague risk, or food
  waste;
- terrain outputs, governments, technologies, wonders, and unit behavior may
  differ from the newer `civ2civ3` defaults.

The authoritative delta list is
[`data/classic/README.classic`](../data/classic/README.classic). The server's
ruleset data and in-game help are authoritative for detailed costs and effects.

## The strategic-v1 control boundary

The harness is not a human Freeciv client. Every civilization—including an
agent-controlled civilization—remains a hard-difficulty Classic AI. Freeciv's
AI chooses all legal unit moves, city production, research, government,
diplomacy, combat, trade, and settlement actions.

Once per turn, the external agent reads an aggregate observation of only its
own civilization and chooses four target AI trait modifiers. Those modifiers
shape the Classic AI's priorities for its next phase. There are no direct
commands to move a unit, choose city production, declare war, set research, or
inspect the map.

Single-player mode has exactly one externally controlled strategic seat; all
remaining seats are native Classic AIs whose traits are never changed by the
supervisor. Multiplayer mode makes every configured place an external
strategic seat.

## Observation

`agent next` returns an envelope containing:

- `state`, `game_id`, and `agent_id`;
- `turn`, `year`, and a turn-scoped `observation_id`;
- `objective` and the shared `deadline_at`;
- the exact `action_schema`;
- `observation`, containing the own-civilization fields below.

| Observation field | Meaning |
| --- | --- |
| `seat_id`, `player_id`, `player_name` | Stable seat and Freeciv player identity for this match. |
| `turn`, `year` | Current Freeciv turn and year. |
| `alive` | Whether this civilization is still alive. |
| `civilization_score` | Current Freeciv civilization score. |
| `gold` | Current treasury. |
| `num_cities` | Number of owned cities. |
| `num_units` | Number of owned units. |
| `bulbs` | Current research-bulb value exposed by Freeciv. |
| `culture` | Current culture value. |
| `government` | Current government rule name. |
| `research` | Current research target rule name, when present. |
| `traits` | Effective Classic-AI trait values currently in force. |
| `trait_bases` | Fixed base values initialized by the ruleset. |
| `trait_modifiers` | Current external modifiers applied to those bases. |

The observation intentionally contains no opponent state, terrain/map view,
individual cities, units, diplomatic relations, legal primitive actions, or
spectator frames. Use score and economy trends across your own observations;
do not assume information that is absent.

## Action and trait meanings

Every action must have exactly this shape and all four integer fields:

```json
{
  "type": "set_traits",
  "traits": {
    "aggressive": 0,
    "builder": 20,
    "expansionist": 30,
    "trader": 10
  }
}
```

Each submitted number is the **target modifier**, not the final effective
trait and not a relative increment. Its allowed range is `-49` through `50`.
The bridge changes the current modifier to that target each turn.

Classic uses fixed base 50 values for these four traits in this setup. Thus:

- modifier `-49` produces effective value 1;
- modifier `0` produces effective value 50;
- modifier `50` produces effective value 100.

The observation exposes base, modifier, and effective values separately so an
agent does not need to infer them.

| Trait | Classic AI interpretation | Direction |
| --- | --- | --- |
| `aggressive` | How easily the AI declares war. | Higher is more willing to become aggressive; lower is more restrained. |
| `builder` | How much the AI wants to build city improvements. | Higher weights improvements more strongly. |
| `expansionist` | How much the AI wants to settle new territory. | Higher increases founder/settler desire. |
| `trader` | How much the AI wants to establish trade routes. | Higher increases trade-route priorities and related valuations. |

These are tendencies inside the Classic AI, not guaranteed primitive actions.
For example, maximum expansionism cannot create a city without a legal site
and suitable resources. A sensible policy adapts to trends: early city growth
may favor expansion, a growing empire may benefit from building and trade, and
military pressure may justify aggression. The correct balance depends on the
objective and observed state; there is no hidden supervisor policy.

## Turn loop

Start with `LAST_TURN=0`:

```sh
python3 -B -m agent_eval agent next \
  --session SESSION.json \
  --after-turn LAST_TURN \
  --wait-s 120
```

Then:

1. If `state` is `waiting`, no new turn arrived within the long-poll window.
   Poll again with the same `LAST_TURN`.
2. If `state` is `completed`, `invalid`, `failed`, or `cancelled`, stop.
3. Otherwise read the objective, own observation, deadline, and schema.
4. Choose all four modifier targets as integers in `[-49, 50]`.
5. Submit the exact turn and observation ID once:

```sh
python3 -B -m agent_eval agent act \
  --session SESSION.json \
  --turn TURN \
  --observation-id=OBSERVATION_ID \
  --action '{"type":"set_traits","traits":{"aggressive":0,"builder":20,"expansionist":30,"trader":10}}'
```

6. Set `LAST_TURN=TURN` only if the action response contains
   `accepted: true`, then repeat. On any HTTP/client error or response that is
   not accepted, keep `LAST_TURN` unchanged and poll again with the same
   explicit session path. The supervisor redelivers an active observation for
   which this authenticated seat has no accepted action.

`OBSERVATION_ID` is the nonempty top-level `observation_id` returned by the
preceding `agent next` call. The `=` form is safe for every opaque value. Run
only one active observe/act loop for a session. Exact retries of the same
request are safe. Do not submit a revised duplicate:
conflicting retries, stale turns, and mismatched observation IDs are rejected.
Never use another harness's session or a workspace-global current-session
pointer. Every `next` and `act` command must use the exact session file returned
by that harness's join.

The agent session file contains a bearer token. Never print it, paste it into
chat, expose it in telemetry, or share it with another player.

## Multiplayer concurrency and timeouts

At the start of a Freeciv turn, the bridge sends every controlled seat's
observation in one request. The supervisor publishes all private observations
atomically and wakes all waiting harnesses. It then waits for the collective
barrier, so Codex and Claude can both read turn N before either action is
applied. When every action arrives, the bridge applies them together before
the Classic AI phase.

The named timing modes are:

| Mode | Agent action deadline |
| --- | --- |
| `default` | 180 seconds per turn |
| `blitz` | 60 seconds per turn |
| `infinite` | None; the barrier waits for every action or owner cancellation |

The join, status, watch, prompt, and private turn surfaces report the selected
mode. In infinite mode both `action_timeout_s` and `deadline_at` are JSON
`null`; this disables the transport deadline as well as the supervisor clock
without bypassing action validation or the shared turn barrier.

If the action deadline expires, a missing seat uses `hold_invalid`: its prior
trait modifiers remain in force. The supervisor does not invent an action and
does not run the deterministic bot as a fallback. The timeout is traced and
the match becomes benchmark-invalid, although Freeciv continues so scores and
captures can still be produced.

A long-poll returning `state: waiting` is different from an action timeout.
Only missing the shared `deadline_at` after an observation has been published
invalidates the match.

While a barrier remains open, private polling is self-correcting: a seat with
no accepted action gets the same observation again even if its client advanced
`after-turn` incorrectly. A seat whose action was accepted gets a periodic
`action_received: true` acknowledgement and remaining-seat count. After 30
seconds an unsubmitted redelivery includes a concise reminder. The service can
only answer a harness that continues polling; it cannot push into or restart a
process that stopped calling the API.

The assigned harness/model must read each observation and choose the submitted
action directly. Do not write, launch, or delegate to an automated bot solely
to beat the clock; use a longer named timing mode instead.

## Spectators and anti-cheating

The native Freeciv observer, replay page, watch JSON, accumulated PNG frames,
video, saves, scorelog, server logs, and decision traces are
evaluation/spectator artifacts. They are explicitly omniscient and are not
valid agent perception.

During play, an agent must use only its private `agent next` response plus
static rules and its own prior observations. It must not:

- query `/watch`, `watch.json`, frame, video, or result routes for decisions;
- inspect another controller's session or token;
- read live saves, `decisions.jsonl`, scorelogs, map captures, or server logs;
- call the internal bridge endpoint;
- ask a spectator or another harness for hidden state.

Public status may be useful to the game owner, but the agent loop already
reports terminal state. Keep player reasoning on the private observation
surface so comparisons remain fair.

## Results and evaluation

The terminal states mean:

- `completed`: Freeciv ended normally and every required action arrived;
  `benchmark_valid` is true.
- `invalid`: Freeciv produced a result, but at least one validity condition
  such as an action timeout failed.
- `failed`: an operational, bridge, validation, or Freeciv failure prevented a
  valid completion.
- `cancelled`: the owner stopped the game.

Status, replay, and terminal result map Freeciv player names to the exact
public `harness-model` controller labels supplied at join, with native seats
shown as `Freeciv Classic AI`. They show who leads or won and by how much.
Benchmark validity is a separate field and banner: an invalid completed game
can still have a recorded winner, but it is excluded from valid aggregate
performance. The terminal result parses Freeciv's `SCORELOG2` output. Players
are sorted by final score and receive competition ranks; equal scores share a rank. Per-seat
statistics record turns, received decisions, fallback count, mean latency, and
standard token counters. The session supervisor does not interpret opaque
telemetry, so its standardized token counters remain zero; model usage stored
inside telemetry remains in the private decision trace. Stable controller
fingerprints derive from supplied controller label and public metadata rather
than random agent ID or physical place, allowing results across seeds and seat
rotations to group under Codex, Claude, or another controller identity.
New joins require the exact non-generic `harness-model` label shown in these
views; the service does not infer identity from the harness process.

Aggregated leaderboards report valid/invalid episode counts, wins, win rate,
average score, average rank, decision counts, and average latency. Invalid
episodes are counted but excluded from wins, score, and rank averages.

`just watch GAME_ID` launches the same-revision native Freeciv SDL2 client as
an owner-authorized global observer in the lobby or while the game is running.
Opening it before the final join uses `timeout 0` without SIGINT; after the
client connects, it observes globally with a one-second turn clock. A lobby
viewer waits at Freeciv's pregame screen and switches to the map on game start.
A midgame viewer is not launched until the current synchronous agent turn has
released Freeciv's socket loop; the launcher then verifies `game_ready` and
closes the GUI on a real disconnect instead of leaving a misleading main menu.
Closing it or failing to connect returns the server to its benchmark no-clock
mode using a fresh timeout acknowledgement. Its Python and EXIT cleanup release
only the matching lease, including when client startup fails. Only post-start
activation uses SIGINT, so only that path delays an immediate reopen with the
safety guard. `just replay GAME_ID` opens the separate React spectator
dashboard with strategic-map playback, controller scores, metric history,
technology progression, and all map factions. Replay never mutates game state
and remains available after the match. The cached `video.mp4` route is an
optional artifact, not the primary viewer.

Every place has a fixed `player_color` that is assigned inside Freeciv before
the match starts. Public status and replay use that exact value—not image
inference—to label each swatch with both controller identity and the Freeciv
player name. Color is supplementary; the text label remains authoritative.

## Game lifetime and failure behavior

The game exists only while the supervisor process that created its ID remains
alive. Live child processes and harness sessions are not recovered after a
supervisor restart.

If an agent sees connection refused/unreachable, it must stop and ask the user
to restore the correct supervisor or create a new game. If it receives 404
`game not found`, it must ask for the current game ID and service URL. It must
not retry indefinitely, invent an ID, or create a replacement game without
authorization.

## Current limitations

- `strategic-v1` controls four AI trait modifiers only; it has no primitive
  city, unit, combat, diplomacy, research, government, or map actions.
- The observation is an own-civilization aggregate, not a fog-of-war map or
  entity/legal-action state.
- Controlled seats remain Classic AIs; this measures strategic steering of
  that AI, not human-equivalent Freeciv control.
- Only the Classic ruleset is accepted by the session supervisor.
- Frame zoom is fixed at 1, and capture is a strategic map timeline rather
  than GUI video.
- Active sessions are not durable across supervisor restart.
- Model execution belongs to external harnesses. No vLLM is required, and the
  supervisor does not call OpenAI, Anthropic, vLLM, or the deterministic bot.

A future primitive protocol would need its own versioned observation and legal
action schema. It should not silently expand or change `strategic-v1`.
