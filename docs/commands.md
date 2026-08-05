# Command and API reference

The `just` recipes cover the normal workflow. This page documents the deeper
session-first CLI and HTTP surfaces. Run commands from the repository root.

## Lifecycle

```text
supervisor starts
  -> owner creates game (Freeciv child starts in lobby)
  -> harnesses join and receive private agent sessions
  -> last required join sends the one Freeciv start command
  -> each turn publishes all agent observations
  -> all actions arrive or the shared deadline expires
  -> Freeciv continues until completed, invalid, failed, or cancelled
  -> status, result, frames, replay page, and video remain available
```

The nonterminal states are `lobby`, `starting`, and `running`. Terminal states
are `completed`, `invalid`, `failed`, and `cancelled`.

A game ID is not durable across supervisor processes. The current supervisor
owns an in-memory live-game registry and does not reattach to an interrupted
Freeciv child after restart. Keep it running for the whole match.

## Supervisor

The normal command is `just start`. It owns the supervisor, read-only replay
gateway, and Vite arena together. The stable public routes are
`https://freeciv-api.localhost` and `https://freeciv.localhost`; all child
listeners use fresh loopback ports. Existing listeners and games are never
signalled or replaced.

Start a policy-free game service directly:

```sh
export AGENT_EVAL_ADMIN_TOKEN='replace-with-a-long-random-token'
python3 -B -m agent_eval supervisor \
  --host 127.0.0.1 \
  --port 8765 \
  --runs-root session-runs
```

For raw API integration, `just start-supervisor 8765` starts only this fixed-
port supervisor. It is an advanced command, not the normal local workflow.

Options:

- `--host` and `--port` select the bind address; defaults are
  `127.0.0.1:8765`.
- `--public-url` changes advertised public links for a reverse proxy. The
  bridge still calls the bound service through loopback.
- `--runs-root` selects the per-game artifact root; the CLI default is
  `session-runs`.
- `--binary` selects a particular `freeciv-server` binary.
- `--admin-token` supplies the create-game bearer directly.
- `--admin-token-env` changes the environment variable read for that token;
  it defaults to `AGENT_EVAL_ADMIN_TOKEN`.

If neither token source is present, the supervisor generates an admin token
and includes it once in its ready JSON. Only its digest is retained. Progress
goes to stderr and stable machine-readable output goes to stdout.

## Game commands

### Create

```sh
python3 -B -m agent_eval game create \
  --service-url http://127.0.0.1:8765 \
  --mode single \
  --places 2 \
  --turns 100 \
  --seed 101 \
  --ruleset classic \
  --objective 'Maximize final Freeciv civilization score.' \
  --timing-mode default \
  --lobby-timeout-s 300 \
  --frame-interval 1 \
  --frame-zoom 1 \
  --credentials /tmp/freeciv-owner.json
```

The create bearer comes from `--admin-token` or
`AGENT_EVAL_ADMIN_TOKEN`. Valid request bounds are:

- `mode`: `single` or `multiplayer`.
- `places`: 2 through 16.
- `turns`: 1 through 5000; omitted defaults to 5000.
- `seed`: 1 through 2,147,483,647; omitted means a generated seed.
- `ruleset`: currently only `classic`.
- `objective`: any nonempty string delivered verbatim to the agents.
- `timing-mode`: `default` (180 seconds on `strategic-v1`, 600 seconds on
  `full-control-v2`), `blitz` (60 seconds), or `infinite` (no agent deadline).
- `action-timeout-s`: advanced custom deadline of at least 0.1 seconds. Omit
  it when selecting a named timing mode.
- `lobby-timeout-s`: at least 0.1 seconds, or 0 to disable lobby expiry.
- `frame-interval`: 0 through 99; 0 disables map snapshots.
- `frame-zoom`: currently fixed at 1.

Creation starts and configures a Freeciv child but leaves it in the lobby.
`--credentials` writes the owner and shared join tokens to a mode-0600 file.
The path may contain a literal `{game_id}` placeholder, such as
`.agent-eval/games/{game_id}/owner.json`, or name an existing/trailing-slash
directory, in which case the CLI writes `GAME_ID/owner.json` below it. This
lets concurrent games retain independent credentials;
when used, raw tokens are removed from stdout. The response includes the game
ID, immutable roster shape, public URLs, `timing_mode`, and
`action_timeout_s`. The timeout is JSON `null` in infinite mode. Status,
watch, join, and private turn responses repeat this contract; older archived
games that predate it omit the fields instead of being relabeled.

`--player-invite PATH` additionally writes only the game ID, service URL, and
game-scoped join token to a separate mode-0600 JSON file. `PATH` may contain a
literal `{game_id}` placeholder. Raw credentials are omitted from stdout when
either private output option is used. The base `just single` and `just multi`
recipes use this to stage `play/.invites/GAME_ID.json` automatically.
They also pass `--lobby-timeout-s 0`, because starting external harness
conversations is owner-paced; direct CLI callers retain the 300-second default.
The simple recipe order is places, timing mode, then optional turns—for
example, `just multi 2 infinite` or `just multi 3 blitz 150`. Mode-first forms
such as `just multi infinite 2 150` and the legacy `just multi 2 150` remain
accepted. `--max-turns` overrides any positional turn value.

Rebuild a missing or stale player invitation from the owner credentials
without printing either token:

```sh
python3 -B -m agent_eval game stage-invite GAME_ID \
  --credentials .agent-eval/games/GAME_ID/owner.json \
  --output play/.invites/GAME_ID.json \
  --require-open-lobby
```

The shorthand owner command is `just invite GAME_ID`. It refuses to stage for
a running or terminal game because an invitation cannot revive a closed lobby;
it never silently creates a replacement match.

In `single` mode exactly one place is joinable and the remaining places are
native Classic AIs. In `multiplayer` every place is joinable and `places` is
also the immutable external-agent cap.

### Join

```sh
python3 -B -m agent_eval game join GAME_ID \
  --service-url http://127.0.0.1:8765 \
  --credentials /tmp/freeciv-owner.json \
  --place 1 \
  --name codex-MODEL \
  --metadata '{"client":"codex","model":"gpt-5.6"}' \
  --session /tmp/freeciv-codex.json
```

Provide the shared join bearer using one of:

- `--join-token TOKEN`
- `--credentials FILE`
- `AGENT_EVAL_JOIN_TOKEN`

This is the deep owner/integration CLI. Generated harness prompts instead
enter `play/` and use its player-only Just recipe, which reads the mode-0600
invitation file and never expands the bearer into command arguments. The
current join bearer is shared by all open seats in one game; it is neither
seat-scoped nor single-use.

`--place` accepts a numeric place or seat ID. If omitted, the first open place
is selected. `--name`/`--controller-label` is required and is a stable public label. Use a
truthful `harness-model` form such as `codex-gpt-5.6-sol`,
`pi-gpt-5.6-sol`, or `claude-code-claude-opus`, not `Agent`. Metadata
may be inline JSON, `@path/to/file.json`, or `-` for stdin. It is public
identity data, so secret-looking metadata keys are rejected.

`--session` is required. It writes the service URL, game ID, agent ID, agent
token, assigned place, and controller identity as mode-0600 JSON. The raw
agent token is never echoed. Store a different session file for each harness.
The last required join starts the game exactly once.

The join response reports the exact timing mode. The assigned harness/model
must inspect each private observation and choose the action itself; it must not
write, launch, or delegate to an automated bot solely to beat the clock.

The Just workflow derives each session filename from a readable slug plus the
first 12 hex characters of SHA-256 over the exact controller label. This keeps
case- or punctuation-distinct labels from colliding after slug normalization;
`just bot GAME_ID EXACT_CONTROLLER_LABEL` uses the same derivation.

At the HTTP level, presenting an existing agent token reconnects the same
identity. A new join to a full lobby, an occupied requested place, or a join
after start returns 409.

### Inspect, watch, replay, cancel, and result

```sh
python3 -B -m agent_eval game status GAME_ID
python3 -B -m agent_eval game watch GAME_ID
python3 -B -m agent_eval game native-viewer GAME_ID \
  --credentials /tmp/freeciv-owner.json \
  --lease-file /tmp/freeciv-viewer-lease.json
python3 -B -m agent_eval game native-viewer-status GAME_ID \
  --credentials /tmp/freeciv-owner.json \
  --lease-file /tmp/freeciv-viewer-lease.json
python3 -B -m agent_eval game native-viewer-release GAME_ID \
  --credentials /tmp/freeciv-owner.json \
  --lease-file /tmp/freeciv-viewer-lease.json
python3 -B -m agent_eval game result GAME_ID
python3 -B -m agent_eval game cancel GAME_ID \
  --credentials /tmp/freeciv-owner.json
```

`status` prints current lifecycle, public roster, complete score snapshot,
controller leaderboard, lead/outcome summary, validity information, and
links. Public roster and leaderboard rows include `player_color`, the exact
fixed color assigned to that place inside Freeciv. The replay renders these as
text-labeled swatches mapping controller label to Freeciv player name; color is
never inferred from captured pixels. `game watch` prints the browser replay
URLs; `just replay` opens that page. `native-viewer` creates a short-lived
connection lease for the real
Freeciv client and requires the owner bearer from `--owner-token`, a
credentials file, or `AGENT_EVAL_OWNER_TOKEN`; normally use `just watch` to
build and launch it. The native connection is loopback-only, permits one
viewer, and is available in `lobby`, `starting`, or `running`. In a lobby the
lease sends `set timeout 0` through the server console without SIGINT, so it
can safely pause a concurrent final join until viewer activation is ordered.
After the client connects, the server promotes it to global observer and uses
`timeout 1`; release or connection timeout restores `timeout -1`. Timeout
commands are accepted only from a newly ordered Freeciv acknowledgement, so an
old setup message cannot falsely confirm cleanup. `native-viewer-status`
reports `enabling_server`, `waiting_for_client`, `connected`, `observing`,
`game_ready`, and terminal disconnect/error states. `just watch` waits through
`enabling_server` before launching SDL, prints useful lobby/map-ready messages,
and writes a verbose client log beside the game's local credentials.
For infinite-timing games, `activation_timeout_s` is `null`: an observer may
remain in `enabling_server` for the full agent barrier, until release or game
termination. After activation, the normal finite SDL connection timeout still
applies. Finite default and blitz games retain their action-timeout-derived
activation deadline.
Before requesting a lease, the command checks the supervisor's advertised
native-viewer protocol and its lease-status, bridge-acknowledgement, and
release-during-activation guarantees. For a supervisor process started from
older code, `just watch` does not touch that process. Instead it copies the
newest stable turn save into a temporary directory, loads the copy in a new
loopback-only Freeciv server on an ephemeral port, and opens the same-revision
SDL client as global observer. The terminal labels this a **snapshot watch
room**, including its source save and turn: it is frozen at that checkpoint,
not continuously live. Closing SDL terminates the clone and removes its
temporary directory. An absent, changing, or invalid save is rejected with a
`just replay GAME_ID` fallback. Updating files cannot add live-view guarantees
to an already-running supervisor; new games created after a safe supervisor
restart use the normal continuously live observer path.

The browser stack is unified: `just start` foreground-supervises the Python
game API, a loopback-only read-only replay gateway, and Vite. Vite proxies
`/v1` directly to that invocation's raw gateway URL. Collision-checked
Portless aliases expose the arena and API; the gateway's random port is never
advertised in archive payloads. Existing legacy listeners on 5173/8765 remain
untouched. `just replay [GAME_ID]` starts nothing: it health-checks the running
Portless arena/gateway and opens its stable URL, or tells the user to run
`just start`.

The gateway forwards a new supervisor's existing replay journal. When an old
supervisor returns 404—or a safe terminal archive is used after that supervisor
exits—it reconstructs spectator telemetry from stable autosaves. Its derived
cache is `.agent-eval/replay-cache`, outside `.agent-eval/runs`; the source run
is never rewritten. The first scan of a long game may take time. Bare
`just replay` opens the live/archive picker, and a supplied ID opens that match
directly.

Save-derived player rows contain score, cities, citizens, units, gold, culture,
nation, government, alive state, exact colors, current research name and bulbs,
future technologies, and known/gained/lost technology IDs. The response also
contains the 87-node Classic technology catalog. Freeciv autosaves do not store
the exact current research cost, so the gateway returns `research.cost: 0`
instead of estimating it.

`--lease-file`
durably records the lease before the connection JSON is printed. Release is
owner-authenticated, idempotent, and lease-scoped, so a stale cleanup cannot
close a newer viewer. `just watch` installs an EXIT trap before requesting the
lease and releases it even if JSON parsing or client launch fails. For a viewer
activated after game start, a two-second SIGINT safety window rejects an
immediate reopen with a clear retry message; lobby-only leases use no signal
and can reopen immediately.
`result` is available
only after a terminal state and reports outcome separately from benchmark
validity. `cancel` uses the same owner credential sources.

## Agent commands

Long-poll for a turn using only the private session file:

```sh
python3 -B -m agent_eval agent next \
  --session /tmp/freeciv-codex.json \
  --after-turn 0 \
  --wait-s 120
```

`wait-s` is accepted from 0 through 300 by the HTTP service. A poll that
expires returns `state: waiting`; it is not an action timeout. Poll again with
the same `after-turn` and exact session file. If this seat has no accepted
action for the active turn, the server returns the same private observation
even when `after-turn` already equals that turn. A submitted seat receives a
periodic acknowledgement with `action_received: true`,
`waiting_for_others: true`, the current turn, pending duration, and the number
of seats remaining. A terminal response contains the final lifecycle state
instead of a new observation.

Submit the matching turn and observation ID:

```sh
python3 -B -m agent_eval agent act \
  --session /tmp/freeciv-codex.json \
  --turn TURN \
  --observation-id=OBSERVATION_ID \
  --action '{"type":"set_traits","traits":{"aggressive":0,"builder":20,"expansionist":30,"trader":10}}' \
  --telemetry '{"harness":"codex","model":"MODEL","reasoning":"brief rationale"}'
```

`--action` accepts inline JSON, `@file.json`, or `-` for stdin. Telemetry uses
the same JSON forms and is opaque private-trace data. It may carry model,
usage, latency, or rationale information; the supervisor does not interpret
it as a policy.

`OBSERVATION_ID` is the nonempty top-level `observation_id` returned by
`agent next`. The `=` form is safe even for opaque IDs. Run only one active
observe/act loop per session. An exact duplicate action, including telemetry, is idempotent. Conflicting
duplicates, a stale turn, or the wrong observation ID return 409. Treat every
non-2xx response as an error and advance `LAST_TURN` only when the response
contains `accepted: true`. Accepted responses echo the game, agent, place,
seat, public controller label, turn, idempotency status, and barrier progress.

Public game status exposes only sanitized barrier telemetry: each controller
is `waiting_for_observation`, `thinking`, or `submitted`, with durations and a
remaining-seat count. It never exposes observation IDs or values, actions,
reasoning/telemetry, credentials, fingerprints, or controller metadata.

Run the optional deterministic client with:

```sh
python3 -B -m agent_eval bot --session /tmp/freeciv-codex.json
```

## HTTP API and authentication

The service has four credential scopes:

| Credential | Purpose |
| --- | --- |
| Admin bearer | Create games. |
| Owner bearer | Cancel one game, open its native viewer, or release its matching viewer lease. |
| Shared join bearer | Claim one of that game's open places. |
| Agent bearer | Read and act for exactly one joined agent identity. |

The bridge uses a fifth, game-scoped internal bearer that is never exposed to
harnesses or public URLs.

### Authenticated public lifecycle routes

| Method and path | Authentication | Body or query |
| --- | --- | --- |
| `POST /v1/games` | Admin bearer | Create fields listed above. |
| `POST /v1/games/{id}/join` | Join bearer, or `join_token` in body | Required non-generic `controller_label`/`name`; optional `place`/`seat_id` and `metadata`. |
| `GET /v1/games/{id}/me/next` | Agent bearer | `after_turn=N&wait_s=120` |
| `POST /v1/games/{id}/me/actions` | Agent bearer | `turn`, `observation_id`, `action`, optional `telemetry`. |
| `POST /v1/games/{id}/cancel` | Owner bearer | Empty object. |
| `POST /v1/games/{id}/native-viewer` | Owner bearer | Empty object; returns local host, port, and unique observer username. |
| `GET /v1/games/{id}/native-viewer?lease_id=...` | Owner bearer | Lease lifecycle, game readiness, error, and timeout-restoration state. |
| `POST /v1/games/{id}/native-viewer/release` | Owner bearer | Exact `lease_id`; idempotently restores no-clock mode only for that lease. |

Example create request:

```sh
curl -sS http://127.0.0.1:8765/v1/games \
  -H "Authorization: Bearer $AGENT_EVAL_ADMIN_TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"mode":"single","places":2,"turns":100}'
```

Example join and agent loop requests:

```sh
curl -sS "http://127.0.0.1:8765/v1/games/$GAME_ID/join" \
  -H "Authorization: Bearer $JOIN_TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"place":1,"controller_label":"codex-MODEL","metadata":{"client":"codex","model":"MODEL"}}'

curl -sS \
  "http://127.0.0.1:8765/v1/games/$GAME_ID/me/next?after_turn=0&wait_s=120" \
  -H "Authorization: Bearer $AGENT_TOKEN"

curl -sS "http://127.0.0.1:8765/v1/games/$GAME_ID/me/actions" \
  -H "Authorization: Bearer $AGENT_TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"turn":1,"observation_id":"OBSERVATION_ID","action":{"type":"set_traits","traits":{"aggressive":0,"builder":20,"expansionist":30,"trader":10}}}'
```

### Public unlisted read routes

These routes require no bearer and expose no private observations, actions,
telemetry, or tokens:

| Method and path | Purpose |
| --- | --- |
| `GET /` | Committed production game picker shell. |
| `GET /health` | Supervisor liveness and game count. |
| `GET /v1/games` | Newest-first public picker index for games in this supervisor process. |
| `GET /v1/games/{id}` | Public game status. |
| `GET /v1/games/{id}/status` | Same public game status. |
| `GET /v1/games/{id}/result` | Terminal SCORELOG2 result and artifact URLs. |
| `GET /watch/{id}` | React strategic-map, score, metrics, and technology dashboard. |
| `GET /v1/games/{id}/watch.json` | Viewer state and accumulated timeline. |
| `GET /v1/games/{id}/replay.json?after_turn=N&limit=250` | Paginated public replay telemetry and Classic technology catalog. |
| `GET /v1/games/{id}/frames` | Frame manifest. |
| `GET /v1/games/{id}/frames/latest.png` | Latest stable map frame. |
| `GET /v1/games/{id}/frames/{index}.png` | Indexed map frame. |
| `GET /v1/games/{id}/video.mp4` | Cached video-so-far after a stable frame exists. |

They are unlisted, not an access-control boundary. Treat the game ID as a
shareable spectator locator, not a secret. Players must not use these
omniscient routes as gameplay perception.

With a path-bearing `--public-url`, Python accepts these routes and immutable
viewer assets under that exact mount as well as on its unprefixed internal
loopback routes. A slashless mount root receives a 308 to its trailing-slash
form, preserving the query; browsers retain the original fragment. This is
required for `arena.html`'s `./viewer/assets/...` URLs to resolve under the
mount instead of at its parent.

`GET /v1/games` returns `{"schema_version":1,"games":[...]}`. Every game row
contains `game_id`, `state`, `created_at`, `current_turn`, `turns`,
`benchmark_valid`, `mode`, `places`, `max_agents`, `joined_agents`, public-safe
`resolved_places`, `leaderboard`, `outcome`, and `watch_path`. The index omits
credentials, artifact paths, observations/actions, and detailed replay/player
telemetry; those are loaded only after opening a game.

### Internal bridge route

`POST /internal/v1/games/{id}/turns` is reserved for the Freeciv Lua bridge
and requires the game-scoped internal bearer. It atomically publishes every
controlled seat's observation, waits at the collective action barrier, and
returns actions or timed-out seat IDs. Harnesses must never call it.

## Failure diagnosis

- A transport error such as connection refused means no supervisor is
  reachable at the configured service URL.
- A 404 `game not found` from a reachable supervisor means the ID is unknown
  to that process. It may be mistyped, stale, or from another service URL.
- A 409 generally means a lifecycle conflict: full/started lobby, occupied
  place, stale action, wrong observation ID, or conflicting retry.

An agent encountering an unreachable supervisor or unknown game ID must stop
and ask the user for the current server/ID. It must not retry indefinitely or
create a new game on its own. Starting a new supervisor with the same artifact
directory does not recover the live session.

## Legacy config-first commands

These remain for compatibility but are secondary to session-first harnesses:

```sh
python3 -B -m agent_eval serve CONFIG.json --host 127.0.0.1 --port 8765 --trace decisions.jsonl
python3 -B -m agent_eval run CONFIG.json --seed 101 --rotation 0 --output runs/episode
python3 -B -m agent_eval eval CONFIG.json --runs-root runs
python3 -B -m agent_eval report runs/episode-a runs/episode-b --output report.json
python3 -B -m agent_eval render runs/episode --output game.mp4 --fps 4
```

`serve` requires `AGENT_EVAL_INTERNAL_TOKEN`. `run` executes one configured
episode; `eval` runs configured seeds and seat rotations; `report` parses one
or more episode directories; `render` converts captured PPM frames to MP4.
Optional provider helpers live outside the supervisor dependency path.
