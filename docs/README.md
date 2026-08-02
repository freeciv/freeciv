# Freeciv agent documentation

This directory documents the session-first Freeciv agent extension. It is
separate from Freeciv's upstream `doc/` directory: these pages describe the
local supervisor, harness protocol, evaluation workflow, and `just` shortcuts.

## Start here

- [Quick start](quickstart.md) — the short, opinionated `just` workflow for a
  single agent, model-versus-model game, reference bot, and live viewer.
- [Command and API reference](commands.md) — the full
  `python3 -m agent_eval` CLI, lifecycle, HTTP routes, authentication, and
  legacy evaluation commands.
- [Gameplay guide](gameplay.md) — Classic Freeciv context, the exact
  `strategic-v1` observation/action contract, validity rules, and limitations.
- [Full-control v2 protocol](full-control-v2.md) — the compatibility-preserving
  headless-client architecture and strict foundation for human-level control.

Related source documentation:

- [Agent session contract](../agent_eval/README.md)
- [Classic ruleset notes](../data/classic/README.classic)
- [Root justfile](../justfile)

The normal path is deliberately small:

```sh
just start
just single
just prompt --game_id GAME_ID
just replay
```

Keep `just start` running for the lifetime of the game. A game ID belongs only
to the supervisor process that created it; restarting the supervisor does not
recover an interrupted live game.

`just start` foreground-supervises the Python API, read-only replay gateway,
and Vite arena behind `https://freeciv-api.localhost` and
`https://freeciv.localhost`. `just replay GAME_ID` only checks that running
stack and opens one live or saved match; it never launches a process. An
unrelated Portless route is reported and left alone. Legacy terminal archives remain viewable
from stable autosaves after their original supervisor exits.
