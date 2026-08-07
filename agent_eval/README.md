# Freeciv agent sessions (strategic-v1)

This directory turns an authoritative freeciv-server into a persistent,
session-first evaluation service. Start one supervisor, create any number of
isolated games, hand session files to arbitrary agent harnesses, and compare
their final Freeciv scores and traces.

## Documentation

- [Quick start with `just`](../docs/quickstart.md)
- [Full command and HTTP API reference](../docs/commands.md)
- [Classic strategic-v1 gameplay guide](../docs/gameplay.md)

## Fast local workflow

The repository root has an opinionated `justfile`. After installing
[`just`](https://just.systems), the normal path is only:

~~~sh
just start                 # terminal 1; Python game/replay API on :8765
just single                # terminal 2; copy the returned game_id
just prompt --game_id GAME_ID --name codex-MODEL # paste into the harness
just bot GAME_ID baseline-model # optional model-free player
just watch GAME_ID         # real Freeciv SDL2 global observer (owner-only)
just replay                # read-only archive/live picker on :5173
just replay GAME_ID        # open one live or saved game directly
~~~

For a two-model game:

~~~sh
just multi
just prompt --game_id GAME_ID --name codex-MODEL --place 1
just prompt --game_id GAME_ID --name claude-code-MODEL --place 2
~~~

The generated prompt tells the harness to `cd` into the player-only `play/`
workspace and run that directory's player-only `just join`. It explicitly
forbids the repository-root owner join, reads the staged mode-0600 invitation,
and saves its private session beneath `play/.sessions/`. Bare root `just join`
prints this bootstrap prompt; root `just join --game_id ...` remains an
owner-side convenience and must not be used as the harness workflow.

`just prompt` is the dedicated copy/paste prompt for starting a fresh harness.
Use `just prompt --game_id GAME_ID` to embed a known ID. The bootstrap prompt
requires the harness to identify itself with a truthful public `harness-model`
label, such as `codex-gpt-5.6-sol`, `pi-gpt-5.6-sol`, or
`claude-code-claude-opus`. Add `--name codex-MODEL` and `--place 1` for a
specific multiplayer assignment; do not use the generic `Agent` label.

This uses stable Portless URLs for the arena and API, a local-development admin
token, 5000 turns, two places, and ignored `.agent-eval/`
credentials/artifacts. Each match keeps
owner and controller files under `.agent-eval/games/GAME_ID/`, so concurrent
games cannot overwrite one another. Override defaults with
`AGENT_EVAL_SERVICE_URL`, `AGENT_EVAL_ADMIN_TOKEN`, or
`AGENT_EVAL_STATE_DIR`. Set the horizon explicitly with
`just single --max-turns 200` or `just multi --max-turns 150`. The positional
forms `just single 4 200` and `just multi 3 150` remain available for
compatibility.

The primary architecture is:

~~~text
Codex / Claude Code / Pi / custom loop
               |
     public session HTTP API
               |
  policy-free Python supervisor
        |                 |
  freeciv-server A   freeciv-server B  ...
        |
unsafe strategic-v1 Lua bridge
~~~

The supervisor owns the registry, one child and artifact directory per game,
authentication, lobby lifecycle, concurrent turn barriers, watch state,
reports, and capture conversion. It does not import provider adapters or call
OpenAI, Anthropic, vLLM, or a deterministic policy. Models and policies are
ordinary clients of the same public API.

Strategic-v1 is intentionally high level. Every player remains a
hard-difficulty Classic AI that handles legal city, unit, diplomacy, and combat
actions. An agent gets its own player's strategic observation once per turn and
sets four integer Classic-AI trait modifiers: aggressive, builder,
expansionist, and trader, each in [-49, 50]. A native place receives no trait
changes. This is not a human-equivalent primitive-action benchmark.

## Build and test

On macOS, install the normal Freeciv dependencies plus Meson, Ninja, curl, and
ffmpeg. From the repository root:

~~~sh
meson setup build-agent -Dclients=[] -Dtools=[] -Dfcmp=[]
meson compile -C build-agent freeciv-server
python3 -B -W error::ResourceWarning -m unittest discover -s agent_eval/tests -v
~~~

The native observer additionally uses SDL2, SDL2_image, and SDL2_ttf.
`just watch GAME_ID` installs missing SDL libraries with Homebrew, configures
`build-viewer` with server/tools/audio/NLS disabled, and compiles
`freeciv-sdl2` from this checkout before connecting.

If build-agent exists, only compile it. The Python service and clients use the
standard library. The real server suite is opt-in:

~~~sh
FREECIV_AGENT_E2E=1 python3 -B -W error::ResourceWarning -m unittest discover -s agent_eval/tests -v
~~~

## Quick start: one agent against native Classic AI

Terminal 1 starts the long-lived supervisor. Use a high-entropy admin token and
keep the service on loopback unless you add a trusted reverse proxy:

~~~sh
export AGENT_EVAL_ADMIN_TOKEN='replace-with-a-long-random-token'
python3 -B -m agent_eval supervisor --host 127.0.0.1 --port 8765 --runs-root session-runs
~~~

The first stdout value is stable JSON with state, service_url, and runs_root;
progress goes to stderr. If the environment variable is absent, the supervisor
generates admin_token and prints it once in the ready JSON. Only its SHA-256
digest is retained.

When a reverse proxy needs different links, pass --public-url. Public API,
watch, frame, and video links use that advertised URL, but every Freeciv bridge
always calls the bound supervisor port through a separate loopback URL. The
game-scoped internal token is never placed in a public URL or response.

Terminal 2 creates a two-place lobby. In single mode exactly one place is
joinable and every remaining place is native Classic AI:

~~~sh
python3 -B -m agent_eval game create --mode single --places 2 --turns 100 --seed 101 --objective 'Maximize final civilization score.' --action-timeout-s 120 --frame-interval 1 --credentials /tmp/freeciv-owner.json
~~~

Creation immediately launches one Freeciv child and flushes deterministic
pregame setup, but deliberately does not send start. The returned JSON has a
high-entropy game_id, an owner bearer and game-shared join bearer, immutable
resolved roster, artifact directory, and join/watch/video URLs. The join
bearer remains reusable for every unclaimed seat; it is not seat-scoped or
single-use. The credentials file is
mode 0600. When --credentials is used, the CLI reports the saved file but
removes both raw tokens from stdout. Omit --credentials only when a program
intentionally needs the one-time raw create response.

For concurrent games, `--credentials` accepts a literal `{game_id}` in the
path or a destination directory. The Just recipes use
`.agent-eval/games/{game_id}/owner.json`; joined controller sessions live in
that same game directory and cannot overwrite sessions from another match.
Their filenames use a readable slug plus a 12-hex SHA-256 prefix of the exact
controller label, so labels that normalize to the same slug still remain
distinct. `just bot GAME_ID EXACT_CONTROLLER_LABEL` computes the same path.

Copy game_id from the JSON, claim the agent place, and save the returned
session:

~~~sh
python3 -B -m agent_eval game join GAME_ID --controller-label codex-MODEL --metadata '{"client":"codex","model":"MODEL"}' --credentials /tmp/freeciv-owner.json --session /tmp/freeciv-agent.json
~~~

The last required join flushes exactly one start. The agent session is mode
0600 and contains service_url, game_id, agent_id, agent bearer token, and its
place. Repeating join with that agent token reconnects to the same identity.
New joins, occupied selections, or joins beyond the immutable cap return 409.
The CLI never echoes agent_token because --session is required.

Controller label and metadata are public identity, not secret storage. A new
join requires a non-generic `harness-model` label; omitted labels, `Agent`,
`HARNESS-MODEL`, and one-word labels are rejected. Use the truthful label
because status, replay, and results display it
verbatim; the service never guesses that a generic agent is Codex. A stable
fingerprint hashes only that supplied label and metadata—not random agent ID or
physical place—so the same controller groups across seeds/rotations while
different models do not. Secret-looking metadata keys are rejected.

Inspect public state or obtain watch links:

~~~sh
python3 -B -m agent_eval game status GAME_ID
python3 -B -m agent_eval game watch GAME_ID
~~~

Status includes a complete-score controller leaderboard and an outcome summary
such as `codex-MODEL leads by 42`. Winner/leader and benchmark validity are
separate: a finished invalid run retains its last complete scores but explicitly
reports that there is no valid winner.

For the live game, `just watch GAME_ID` builds the exact checkout's SDL2 client
and opens it as a global observer. This requires the matching owner credentials
and a local game in `lobby`, `starting`, or `running`; only one native viewer is
supported. It can be opened before the final agent joins. Lobby activation uses
`set timeout 0` without signaling the process; a connected observer uses a
one-second clock. For a running match, the launcher first waits for the current
agent turn response to finish, then opens SDL only after Freeciv has enabled its
observer socket. The terminal reports `connected`, `observing`, and
`game_ready`, so a rejected or disconnected client is closed with a useful
error instead of being left at Freeciv's main menu. SDL verbose logs are saved
under `.agent-eval/games/GAME_ID/viewer-LEASE_ID.log`. Closing the GUI or
failing to connect restores no-clock benchmark operation from a fresh ordered
Freeciv timeout acknowledgement. A late acknowledgement can safely repair
stale supervisor bookkeeping without sending another signal. `just watch`
records the lease before launch and releases that exact lease from both the
Python launcher and an EXIT trap, including client-launch failures. Stale
release attempts cannot close a newer lease. A two-second SIGINT guard rejects
unsafe immediate reopens only for viewers activated after game start; lobby
leases can reopen immediately.
`just start` foreground-supervises the Python game API, loopback-only read-only
replay gateway, and Vite arena at `https://freeciv.localhost`; the API is
`https://freeciv-api.localhost`. Unrelated listeners are reported and never
stopped. `just replay` only health-checks that running stack and opens it; it
never starts or reuses processes. Supplying a game ID opens `/watch/GAME_ID`
directly. Bare replay lists the
available games, including safe terminal archives when the original supervisor
is gone. New supervisors' replay journals are forwarded unchanged. For older
or offline games, the gateway reconstructs detailed telemetry from stable
autosaves and stores derived data under `.agent-eval/replay-cache`, outside the
immutable run directory. The first backfill can take a while for a long match;
later reads use the external cache. The dashboard
provides strategic-map playback, scored competitors, metric charts,
technology progression, and an exact map-faction color legend.

Save-derived snapshots include score, cities, citizens, units, gold, culture,
nation, government, alive state, exact faction colors, research target and
bulbs, future technologies, known/gained/lost technologies, and the Classic
technology graph. Autosaves do not persist the exact current research cost, so
save-derived `research.cost` is `0` rather than a guessed value.
Each place receives a fixed high-contrast Freeciv player color. Public status
and leaderboard rows expose it as `player_color`, and replay shows a labeled
swatch for `controller label · Freeciv player · color`. The native SDL client,
captured maps, and replay legend therefore use the same authoritative mapping.

## Ready-to-paste instructions for Codex, Claude Code, or Pi

Give each harness a different session file and paste this prompt, replacing the
path and harness name. The commands emit only JSON on stdout.

~~~text
You are playing Freeciv through the strategic-v1 session API.
Your private session file is /tmp/freeciv-agent.json.

Repeat until state is completed, invalid, failed, or cancelled:
1. Run:
   python3 -B -m agent_eval agent next --session /tmp/freeciv-agent.json --after-turn LAST_TURN --wait-s 120
   Start with LAST_TURN=0.
2. Read objective, observation, deadline_at, and action_schema. Do not assume
   access to other players' private observations.
3. Choose all four integer trait targets in the documented range.
4. Submit exactly once; exact retries are safe:
   Confirm OBSERVATION_ID is the nonempty top-level observation_id from step 1,
   then run:
   python3 -B -m agent_eval agent act --session /tmp/freeciv-agent.json --turn TURN --observation-id=OBSERVATION_ID --action '{"type":"set_traits","traits":{"aggressive":0,"builder":20,"expansionist":30,"trader":10}}' --telemetry '{"harness":"codex","model":"YOUR_MODEL","reasoning":"brief rationale"}'
5. Set LAST_TURN to TURN only after act returns accepted=true. On any act
   error, keep LAST_TURN unchanged and call next again with this exact session
   file; the server redelivers an unsubmitted active turn.

Never print or share the session token. Watch endpoints are omniscient and are
for spectators/evaluation, not additional agent perception. Run exactly one
active observe/act loop for this session.
Never use a shared current-session pointer; every next and act command must
name this exact session file.
~~~

For Claude Code change telemetry harness to claude-code; for Pi use pi. The
supervisor treats them identically. Telemetry is opaque private-trace JSON, so a
harness can record model name, usage, latency, or rationale without the game
service understanding a provider.

The optional model-free demo is also a normal public client:

~~~sh
python3 -B -m agent_eval bot --session /tmp/freeciv-agent.json
~~~

Its implementation is in bot.py; the supervisor never imports or invokes it.

## Multiplayer model-vs-model games

In multiplayer mode every place is joinable and places is the immutable
max_agents:

~~~sh
python3 -B -m agent_eval game create --mode multiplayer --places 2 --turns 100 --seed 202 --objective 'Maximize score against the other model.' --credentials /tmp/freeciv-versus-owner.json

python3 -B -m agent_eval game join GAME_ID --place 1 --controller-label codex-MODEL --metadata '{"client":"codex","model":"MODEL"}' --credentials /tmp/freeciv-versus-owner.json --session /tmp/freeciv-codex.json

python3 -B -m agent_eval game join GAME_ID --place 2 --controller-label claude-code-MODEL --metadata '{"client":"claude-code","model":"MODEL"}' --credentials /tmp/freeciv-versus-owner.json --session /tmp/freeciv-claude.json
~~~

The server publishes every agent observation and pending slot atomically at the
start of a turn, wakes all long polls, then waits for the collective barrier.
Thus Codex and Claude can both obtain turn N before either submits. Actions are
applied together after all arrive.

If a harness polls with `after_turn=N` before its turn-N action was accepted,
the server redelivers that harness's same private turn-N observation. If its
action was accepted, polling periodically returns a private acknowledgement
with the current turn, pending duration, and remaining-seat count. Public
status reports only `waiting_for_observation`, `thinking`, or `submitted` per
controller and never includes observations, actions, reasoning, credentials,
fingerprints, or controller metadata.

An exact action retry, including identical telemetry, is idempotent. A
conflicting retry, stale turn, wrong observation ID, or differing duplicate
bridge payload returns 409.

If action_timeout_s expires, the policy is hold_invalid: missing seats are
omitted from the bridge response, so their prior trait modifiers remain
unchanged. No deterministic policy is substituted. Each timeout is traced, the
match is marked invalid, and Freeciv continues to final scores and captures.

## HTTP API

Supervisor admin authentication is required for creation:

- POST /v1/games — admin bearer; creates child and lobby.
- POST /v1/games/{game_id}/join — shared join bearer or join_token body;
  required non-generic controller label and optional place number or seat_id.
- POST /v1/games/{game_id}/cancel — owner bearer; terminates, then kills if
  needed.
- POST /v1/games/{game_id}/native-viewer — owner bearer; briefly enables local
  socket polling and returns a unique global-observer connection lease.
- GET /v1/games/{game_id}/native-viewer?lease_id=... — owner bearer; reports
  enabling, connection, observation, map-ready, disconnect, and error state.
- POST /v1/games/{game_id}/native-viewer/release — owner bearer; idempotently
  restores no-clock operation only for the supplied lease ID.

An agent bearer determines identity; callers never supply a seat:

- GET /v1/games/{id}/me/next?after_turn=N&wait_s=120
- POST /v1/games/{id}/me/actions with turn, observation_id, strategic-v1
  action, and optional opaque telemetry.

The game-scoped internal bearer is available only to the bridge:

- POST /internal/v1/games/{id}/turns

Public, unlisted, read-only routes expose no tokens, observations, actions, or
telemetry:

- GET / — committed production game picker
- GET /v1/games — newest-first current-supervisor picker index; the local
  replay gateway can fall back to validated terminal disk archives
- GET /v1/games/{id} and /status
- GET /v1/games/{id}/result after terminal state
- GET /watch/{id} and /v1/games/{id}/watch.json
- GET /v1/games/{id}/replay.json?after_turn=N&limit=250
- GET /v1/games/{id}/frames
- GET /v1/games/{id}/frames/latest.png and /frames/{index}.png
- GET /v1/games/{id}/video.mp4 after the first stable frame

When `--public-url` has a path prefix, the same picker, API, watch, and asset
routes are also accepted below that exact prefix. Its slashless root redirects
to the trailing-slash form so the picker's relative hashed assets stay below
the mount.

The game index envelope is `{"schema_version":1,"games":[...]}`. Each game
contains `game_id`, lifecycle state, creation/current/configured turn fields,
benchmark validity, mode/place/join counts, public resolved-place identity and
colors, leaderboard, outcome, and `watch_path`. It intentionally omits tokens,
artifact paths, replay snapshots, and detailed city or technology telemetry;
the selected game's watch/replay endpoints provide those details.

The React replay page and PNG timeline are explicitly labeled omniscient
strategic map snapshots, not GUI video. Mapimg writes authoritative PPM
snapshots; the service converts them atomically to cached browser-viewable
PNGs. The dashboard adds play/pause, scrubbing, live follow, score and economy
charts, technology state, and a text-plus-swatch legend for every map faction.
`video.mp4` remains an optional cached artifact/API for export; it is not the
primary viewer. Capture never replays or mutates game state.

Creation fields are mode, places (2..16), turns (default 5000), seed, Classic
ruleset, objective, timing_mode, action_timeout_s, lobby_timeout_s,
frame_interval (default 1, Freeciv range 0..99), and frame_zoom (currently
fixed at 1). Named timing modes are default (180 seconds), blitz (60 seconds),
and infinite (no agent deadline, represented by a null action timeout). An
explicit positive action timeout remains available to advanced callers as a
custom mode. A lobby timeout of 0 disables automatic lobby expiry.

`auto_end_idle_phase` (boolean, default true for full-control-v2, rejected for
strategic-v1) ends a phase in which the seat has provably nothing left to
decide. The service asks the seat's own projection — over every unit and city,
not a page of them — for the number of actors still owing a decision: units
awaiting orders that can still move, pending action decisions, cities whose
shield box is full, an unset research target, and unanswered treaty meetings.
Only a zero arms a 20-second grace window, and any state read, capability read,
receipt read or batch from that seat cancels it; health polling and waits do
not. The verdict is taken again at the moment of firing, and the end travels
the same durable path an agent's own `turn --end` takes, recorded with
`source: "auto_idle"` so replays and phase-event feeds can tell it from an
agent's end and from a deadline. It never fires during recovery, during a phase
transition, while a phase end is in flight or unjournaled, or for a seat whose
sidecar is not ready, and a failed auto-end releases the phase back to its agent
rather than ending the game. Set it to false to make the action deadline the
only thing that ever ends a phase.

## Artifacts, results, and security

Every game gets an isolated directory containing:

- manifest.json and immutable resolved roster/config
- auth.json with token SHA-256 digests only, mode 0600
- exact server.commands; start appears only when the lobby fills
- private decisions.jsonl observations/actions/telemetry
- Freeciv score.log, server logs, and synchronous per-turn saves
- omniscient PPM snapshots, cached PNGs, report.json, and final game.mp4

Owner, join, internal, and agent token values are never written to public
artifacts or watch responses. Raw owner/join and agent values live only in
their private credential, invitation, and session surfaces; artifact auth data
contains digests. Treat those files as secrets. The `play/` workspace is a
policy boundary, not hostile same-user isolation, and the game-shared join
bearer can claim any open seat. Strong adversarial use requires separate
OS/container workspaces and controlled credential delivery, but that is not
sufficient to make the current shared bearer seat-scoped. The built-in
server is intended for trusted loopback use; expose it remotely only behind
TLS and appropriate network controls.

game result returns top-level lifecycle validity and reasons, mapped controller
leaderboard, lead/winner summary, SCORELOG2 final scores, ranks, and per-agent barrier stats.
A completed match is valid only when every required action arrived. Timeout
holds end invalid; operational or Freeciv failures end failed.
The public result replaces the absolute host episode path with a game artifact
ID and public status/watch/frame/video URLs.

~~~sh
python3 -B -m agent_eval game cancel GAME_ID --credentials /tmp/freeciv-owner.json
python3 -B -m agent_eval game result GAME_ID
~~~

## Minimal Freeciv boundary

The only game-core extension is frozen to:

- server/scripting/script_server.c/.h exposes signal.connect in the explicitly
  unsafe VM, registers unsafe-only agent_turn_begin(turn, year), and adds its
  emitter.
- server/srv_main.c emits the signal after scores are current and before the
  Classic AI phase.

With no unsafe bridge loaded, the signal is a no-op. No packets, combat, saves,
turn rules, or AI implementation are changed.

## Legacy config-first evaluation and provider helpers

The earlier run, eval, serve, report, and render commands and example configs
remain available:

~~~sh
python3 -B -m agent_eval run agent_eval/examples/native-vs-deterministic.json --seed 101 --output runs/legacy-smoke
python3 -B -m agent_eval eval agent_eval/examples/claude-vs-openai-vs-native.json --runs-root runs/legacy-models
~~~

providers.py is an optional legacy/client helper for OpenAI Responses,
Anthropic Messages, and OpenAI-compatible/vLLM calls. It is not a supervisor
dependency. New comparisons should use separate model harnesses and public
session files so Codex, Claude, Pi, hosted APIs, local vLLM, and model-free
baselines all compete through one protocol.

Primitive-v1 remains future work. It would expose legal primitive actions and
fog-safe map/entity state as a separately versioned protocol rather than
silently expanding strategic-v1.
