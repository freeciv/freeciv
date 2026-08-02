# Quick start with `just`

The root `justfile` is the preferred local interface. Run every command below
from the repository root.

## Prerequisites

Install `just` and the normal Freeciv build dependencies. On macOS:

```sh
brew install just
just --version
```

Meson and Ninja build the headless server. `curl` is used by the Freeciv bridge
and by the join preflight; `ffmpeg` is needed for MP4 rendering. The native
viewer uses SDL2, SDL2_image, and SDL2_ttf. On macOS, `just watch` installs
missing viewer libraries with Homebrew and builds the exact client revision in
`build-viewer/`. The Python supervisor and public clients otherwise use the
standard library.

Run `just` for the four-line workflow or `just --list` for every recipe.

## One agent versus native AI

Start the complete local stack in terminal 1 and leave it running:

```sh
just start
```

`start` builds `build-agent/freeciv-server` if necessary and foreground-runs
the supervisor, read-only replay gateway, and Vite arena. The public URLs are
`https://freeciv-api.localhost` and `https://freeciv.localhost`; internal
gateway, bridge, and Freeciv TCP listeners remain random loopback ports. The
launcher never adopts or stops an unrelated listener. Ctrl-C restores any
pre-existing authorized aliases and stops only children from this invocation.
Runtime files remain under `.agent-eval/`.

Create a two-place game in terminal 2:

```sh
just single
```

The returned JSON contains `game_id`. In single-player mode, one place is
joinable by an external harness and every remaining place is a native hard
Classic AI. The defaults are two total places, 5000 turns, and a 180-second
deadline for each agent turn. Select one of the three timing modes with:

```sh
just single            # default: 180 seconds per turn
just single 2 blitz    # 2 places, 60 seconds per turn
just single 2 infinite # 2 places, no agent action deadline
```

`infinite` keeps the shared turn barrier open until every agent submits or the
owner cancels the game. Set a named turn limit with:

```sh
just single --max-turns 200
```

The older positional form remains available when changing both values:

```sh
just single 4 200
just single 4 blitz 200
```

The first example creates one agent place, three native-AI places, and a
200-turn limit using default timing. The second creates the same game in blitz
mode.

The timing mode may also come first (`just single blitz 4 200`) for
compatibility, but the documented order is places, timing mode, then turns.

Generate the prompt to paste into Codex, Claude Code, Pi, or another harness:

```sh
just prompt --game_id GAME_ID --name codex-MODEL
```

The prompt tells the harness to `cd` into the player-only `play/` workspace and
use its player-only join recipe, where to read the rules, how to observe and
act, and when to stop. Its private session remains beneath `play/.sessions/`.
Use a truthful public `harness-model` identity, such as
`codex-gpt-5.6-sol`, `pi-gpt-5.6-sol`, or `claude-code-claude-opus`; never the
generic `Agent`. The generated workflow is equivalent to:

```sh
cd /ABSOLUTE/PATH/TO/freeciv/play
just join --game_id GAME_ID --name codex-MODEL
```

Do not give the harness the repository-root `just join`; that owner-side
convenience command reads owner credentials and is not the player-only
workflow.

`just single` and `just multi` also stage a mode-0600, game-scoped invitation
for separate harness workspaces in `play/.invites/GAME_ID.json`. The file is
ignored by Git and contains no owner or admin credential. These base recipes disable lobby
expiry so the owner has time to start each harness; the turn action deadline
still applies after play begins. If an invitation is missing or stale, rebuild
it without displaying its token:

```sh
just invite GAME_ID
```

This recovery command only works while the original game is still in its
lobby. It does not revive a failed/terminal game and never creates a
replacement silently.

Bare `just prompt` prints a reusable template with a `GAME_ID` placeholder.
Bare `just join` prints that same bootstrap prompt; it does not join a game.

To watch without giving the player omniscient information:

```sh
just watch GAME_ID
just replay
just replay GAME_ID
just status GAME_ID
```

`watch` builds and launches the real same-revision Freeciv SDL2 client as a
global observer. It is owner-only, local-machine-only, and may be opened in the
lobby before the final agent joins or while the game is running. Lobby attach
uses a console `timeout 0` command and does not signal the Freeciv process;
the pregame screen remains visible until the match starts and then switches to
the live map automatically. During a running agent turn, `watch` waits to start
SDL until Freeciv has enabled its observer socket. It then reports connection,
observation, and live-map readiness. A failed or disconnected client is closed
with its verbose log path instead of being left at the main menu. After the
viewer connects it observes globally with a one-second turn clock. Closing it
(or failing to connect) restores the benchmark's no-clock timeout from a fresh
server acknowledgement.
Only one native viewer may attach at a time. The two-second SIGINT safety delay
applies only when reopening a viewer activated after the game started.
Every place has a fixed high-contrast color assigned inside Freeciv. The replay
legend and leaderboard show that same color beside the controller label and
Freeciv player name, so `AgentPlace1` in the native GUI can be mapped back to
the Codex, Claude, Pi, baseline, or native controller without guessing.
`start` runs the Python supervisor, loopback-only read-only replay gateway, and
Vite picker as one foreground-owned stack. Bare `replay` only health-checks
`https://freeciv.localhost` and opens it; it never starts, reuses, or stops a
process. Passing a game ID checks that it is known and opens
`https://freeciv.localhost/watch/GAME_ID`. If the arena is unavailable, the
command tells you to run `just start`. New supervisors'
journal data is forwarded, while older or offline terminal games are rebuilt
from stable autosaves. Derived data lives in `.agent-eval/replay-cache`, never
inside the run directory. A long game's first backfill may take time; later
reads use that cache. The dashboard combines strategic-map playback with
controller scores, economy metrics, technology progression, and an exact color
legend for configured and dynamic factions.

Save reconstruction provides score, cities, citizens, units, gold, culture,
nation, government, alive state, faction colors, research target/bulbs, future
technologies, known/gained/lost technologies, and the Classic technology graph.
The raw autosave does not persist exact current research cost, so that field is
`0` for reconstructed snapshots.
`status` prints lifecycle, controller leaderboard, current leader or valid
final winner, and validity separately.

## Codex versus Claude

Start one supervisor, then create an all-agent two-place game:

```sh
just start
```

In another terminal:

```sh
just multi
```

Use the returned game ID to make one prompt per harness:

```sh
just prompt --game_id GAME_ID --name codex-MODEL --place 1
just prompt --game_id GAME_ID --name claude-code-MODEL --place 2
```

Paste each output into the corresponding fresh agent conversation. Each
harness runs the `just join` command in its prompt and receives a distinct
private session. The last required join starts the match once. Both agents
receive turn N before the server resolves turn N, so they choose concurrently.

For three agents and 150 turns, create the lobby with:

```sh
just multi 3 blitz 150
```

Then assign places 1, 2, and 3 explicitly. Use distinct `--name` values so
session files and stable controller identities do not collide.

## Model-free reference player

After joining a seat, run the deterministic reference client against that
session:

```sh
just join --game_id GAME_ID --name baseline-model
just bot GAME_ID baseline-model
```

Use the same non-generic name for join and bot (for example,
`just join --game_id GAME_ID --name baseline-model`). The bot resolves that
game-scoped session and is an ordinary public-API client; it is not embedded
in the supervisor and is never substituted for a missing model.

## Base recipe reference

| Recipe | Purpose | Common forms |
| --- | --- | --- |
| `prompt` | Print a prompt for a fresh harness and require a public `harness-model` identity. | `just prompt`; `just prompt --game_id ID --name codex-MODEL --place 1` |
| `start` | Build and foreground-run supervisor, replay gateway, and Vite behind Portless. | `just start` |
| `single` | Create one external-agent place plus native Classic AIs. | `just single`; `just single 2 blitz`; `just single 2 infinite`; `just single 4 200` |
| `multi` | Create an all-external-agent lobby. | `just multi`; `just multi 2 blitz`; `just multi 2 infinite`; `just multi 3 blitz 150` |
| `invite` | Rebuild one mode-0600 player invitation from its owner credentials. | `just invite ID` |
| `join` | Owner-side convenience join with an ID; without an ID, print the player-workspace bootstrap prompt. Do not use it as the generated harness workflow. | `just join`; `just join --game_id ID --name codex-MODEL --place 1` |
| `bot` | Run the deterministic client using one game-scoped controller session. | `just bot ID baseline-model` |
| `watch` | Build and launch the owner-only native SDL2 global observer. | `just watch ID` |
| `replay` | Health-check and open the already-running arena or one replay; start nothing. | `just replay`; `just replay ID` |
| `status` | Print public game status JSON. | `just status ID` |
| `build` | Configure if needed and compile the headless Freeciv server. | `just build` |
| `build-viewer` | Configure and compile the same-revision SDL2 client. | `just build-viewer` |
| `replay-build` | Typecheck and build the React replay dashboard. | `just replay-build` |
| `replay-dev` | Start Vite for replay UI development. | `just replay-dev` |
| `replay-check` | Run replay typechecks, tests, and production build. | `just replay-check` |
| `test` | Run tests with real Freeciv E2Es by default. Pass positional `0` for unit tests only. | `just test`; `just test 0` |

`prompt` uses long options because it produces a harness handoff. The root
`join` keeps long options as an owner-side convenience. The
optional values for `single`, `multi`, and `test`, and the required game ID and
controller name for `bot`, are positional. `watch` and `status` also take a
positional game ID.

## Defaults and files

The `justfile` supplies these local defaults:

| Setting | Default |
| --- | --- |
| `AGENT_EVAL_SERVICE_URL` | `http://127.0.0.1:8765` |
| `AGENT_EVAL_ADMIN_TOKEN` | `freeciv-local-dev` |
| `AGENT_EVAL_STATE_DIR` | `.agent-eval` |

You may export a different value before running `just`. The local development
token is suitable only for a trusted loopback service. Put TLS and appropriate
access controls in front of any remotely exposed supervisor.

The state directory contains one `.agent-eval/games/GAME_ID/` directory per
match. Its `owner.json`, controller session JSON, join responses, and temporary
viewer lease cannot overwrite another game's files. Credentials and sessions
use mode 0600. Runtime artifacts remain under `.agent-eval/runs/GAME_ID/`.
Do not paste private files into prompts, logs, or chat. Native viewing resolves
the owner file for that exact game; public replay does not need it.

Controller filenames combine a readable lowercase slug with a 12-hex digest
of the exact public label. Labels that differ only by case or punctuation
therefore remain distinct. Pass the same exact label to
`just bot GAME_ID CONTROLLER_NAME`; it computes the identical session path.

## Game-ID lifetime and troubleshooting

A game ID is registered only inside the supervisor process that created it.
Stopping `just start`, crashing that process, or starting a new supervisor
makes the old live ID unusable; the current implementation does not recover
live sessions after restart.

- **Connection refused or supervisor unreachable:** the original supervisor
  is not running or `AGENT_EVAL_SERVICE_URL` points to the wrong host. Ask the
  owner to keep the original supervisor running for joining or live play. If it
  is gone, live play needs a new game ID, but `just replay` can still open a
  safe terminal run preserved under `.agent-eval/runs`.
- **Offline archive not found:** the requested game is not a validated terminal
  run in the configured runs root. The error prints the original service, runs
  root, requested ID, and gateway log; it does not invent or mutate an archive.
- **404 or `game not found`:** the server is reachable but the ID is wrong,
  stale, or belongs to another supervisor. Ask for the current game ID and
  service URL.
- **Portless alias belongs to another target:** `just start` fails closed and
  leaves it unchanged. Resolve that named-route collision explicitly; it never
  uses force or kills the listener.
- **Other join rejection:** check that the lobby is open, the requested place
  is unclaimed, and this checkout has the matching owner/join credentials.

`just join` performs a `/health` preflight and prints these cases in plain
language. A harness must stop and tell the user. It must not blindly retry a
stale ID and must not create a replacement game unless the user requests one.

For the complete CLI and HTTP surfaces, continue to
[Command and API reference](commands.md). For play semantics, see the
[Gameplay guide](gameplay.md).
