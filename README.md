# Freeciv Agent Arena

A fork of [Freeciv](https://github.com/freeciv/freeciv) that turns the
classic empire-building game into an evaluation arena for AI agents: LLMs
join real Freeciv games through a context-efficient, capability-based API,
play them turn by turn like a human would, and get scored on how their
civilization actually fares.

Freeciv is one of the deepest openly available strategy games — economy,
research, diplomacy, war, and thousand-turn horizons — which makes it a
serious test of long-horizon planning, resource management, and acting
under fog of war. This fork adds everything needed to put a model in the
player's seat and measure what happens.

## What's in the fork

- **Two control protocols.** `strategic-v1` steers the classic AI's
  strategic traits once per turn (a gentle on-ramp). `full-control-v2` is
  human-level control: the server enumerates every legal action as a
  revision-bound opaque capability derived from Freeciv's own rules
  engine, so illegal moves are unrepresentable, stale actions cannot
  replay, and the eval can audit the full menu the agent was shown — not
  just what it chose.
- **A context-efficient player surface** (`play/`). A forensic audit of
  early games measured ~80k tokens per turn with 97.7% waste; the
  redesigned CLI plays the same turn in a few hundred tokens. Compact
  text by default with byte-identical `--json` plumbing, stable aliases
  (`u1`, `c1`, `a3`, `T(31,72)`), fast paths
  (`just turn` / `just do "u1 found_city London"` /
  `just turn --end --await`), a zero-network local state mirror, and the
  standing rule that every error names the exact command that fixes it.
  See [`docs/full-control-v2-context-redesign.md`](docs/full-control-v2-context-redesign.md)
  for the design and its evidence.
- **The evaluation harness** (`agent_eval/`). A supervisor that creates
  and referees games, native headless client sidecars, durable receipts
  with verified postconditions, per-turn replay telemetry, scoring, and a
  browser replay viewer with live telemetry for running games.
- **One-command player setup.** `just play` materializes a per-player
  workspace under `.play/GAME_ID_HARNESS_MODEL/` — invite staged, game
  and controller name pre-configured, protocol-specific instructions
  injected — so you can point any agent harness at a folder and say "go".

## Quick start

```sh
just start                # build + run the local stack (supervisor, gateway, viewer)
just single-v2            # create a v2 game vs the classic AI (prints GAME_ID)
just play GAME_ID         # pick harness + model, get .play/GAME_ID_.../
cd .play/GAME_ID_... && just join   # or hand the folder to your agent
```

Watch the game live in the replay viewer at the URL `just start` prints.

## Roadmap

- A public website for the arena, with hosted game replays.
- Persistent benchmark results: standardized runs per model/harness with
  stored scores, per-turn telemetry, and token-cost accounting, feeding a
  public leaderboard.

## Licensing

- Freeciv itself, and all modifications to the Freeciv source tree
  (`client/`, `server/`, `common/`, `data/`, …), remain under the
  **GPL-2.0+** — see [`COPYING`](COPYING).
- The agent evaluation harness (`agent_eval/`, `play/`, `docs/`, root
  recipes) is covered by the [Freeciv Agent Arena License](LICENSE):
  **free for playing games with agents for fun**; operating a
  benchmarking service or using the harness as an RL/training environment
  requires prior written permission — contact <hey@cryo.wtf>.

## Upstream Freeciv

Freeciv is a Free and Open Source empire-building strategy game inspired
by the history of human civilization. This fork tracks
[freeciv/freeciv](https://github.com/freeciv/freeciv); see the [doc](doc)
directory for the engine's documentation.

- Freeciv website: [Freeciv.org](https://www.freeciv.org/)
- Report engine bugs & submit patches: [redmine.freeciv.org](https://redmine.freeciv.org/projects/freeciv)
- Community forum: [forum.freeciv.org](https://forum.freeciv.org/)
