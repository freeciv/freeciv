# Player commands

Run these commands from `freeciv/play/`. `just` by itself prints the short
workflow; `just --list` lists every player-safe recipe.

## Bootstrap and join

```sh
just prompt --game_id GAME_ID --name HARNESS-MODEL
just join --game_id GAME_ID --name HARNESS-MODEL
```

The controller name must truthfully identify the harness and model, such as
`codex-gpt-5.6-sol`, `pi-gpt-5.6-sol`, or
`claude-code-claude-opus`. Multiplayer assignments may select a numbered seat:

```sh
just join --game_id GAME_ID --name claude-code-claude-opus --place 2
```

The player Just recipe reads the join token from the mode-0600
`.invites/GAME_ID.json`; it has no join-token command-line option. It may also
select a staged file with `--invite PATH`. The lower-level `client.py` retains
`AGENT_EVAL_JOIN_TOKEN` and a direct CLI option for controlled integrations,
but neither is part of the recommended Just argv path; never put a bearer in a
shared command line. On success, the client creates a
mode-0600 session beneath `.sessions/GAME_ID/` and prints its exact
`session_file` path. It also prints and saves the exact timing contract:
`default` is 180 seconds per agent
turn, `blitz` is 60 seconds, and `infinite` has no agent deadline.

Root `just single` and `just multi` stage the invitation automatically. If the
client reports that it is missing, unreadable, or stale, ask the owner to run
`just invite GAME_ID` from the repository root, then retry the join once.
That command cannot revive a lobby that has already failed or become terminal,
and it never creates a replacement game.

The staged bearer is shared by the open seats in one game rather than being
seat-scoped or single-use. This workspace is a policy boundary. Hostile model
isolation requires separate OS/container workspaces and controlled credential
delivery, but that separation alone does not make the current shared bearer
seat-scoped. Normal same-user directory permissions and this join protocol are
not a hostile multi-tenant security boundary.

If the supervisor is unreachable, the game is unknown, or the lobby rejects
the join, stop and report the error. Do not retry blindly or create another
game.

You—the assigned harness/model—must inspect the private observation and choose
the action directly. Do not write, launch, or delegate to an automated bot
solely to beat the clock.

## Observe

Start at turn zero:

```sh
just next --session SESSION_FILE --after_turn 0
```

Then pass the last observed turn:

```sh
just next --session SESSION_FILE --after_turn 42
```

The default long-poll is 120 seconds. A `waiting` response is not a game turn
and does not change `LAST_TURN`; poll again with the same value and exact
session file. Always select the path returned by your join:

```sh
just next --session .sessions/GAME_ID/SESSION.json --after_turn 42
```

The client permits an omitted `--session` only when exactly one private
session exists in the whole player workspace. With two or more sessions it
fails before any request, even if `.sessions/current` exists, because that
shared pointer cannot identify which harness is calling.

## Act

Copy the exact top-level `turn` and `observation_id` from `next`, then submit
all four integer trait targets:

```sh
just act \
  --session SESSION_FILE \
  --turn 43 \
  --observation_id OBSERVATION_ID \
  --action '{"type":"set_traits","traits":{"aggressive":0,"builder":20,"expansionist":30,"trader":10}}'
```

Targets must be integers from `-49` through `50`. Submit once. An exact retry
is safe; a conflicting revision is rejected. Advance `LAST_TURN` only after
the response contains `accepted: true`. On every error keep the old value and
poll again with the same explicit session.

## Terminal result

```sh
just result GAME_ID
```

Read `result` only after the private loop reports a terminal state. There is no
live public-status command in this workspace because public standings can
contain opponent information that is absent from the private observation.

## Deliberately unavailable here

This player workspace has no recipes for starting/creating/cancelling games,
watching, replay, frames, video, saves, scorelogs, server logs, owner actions,
or internal bridge calls. Those belong to the separate owner/evaluation
surface and must not be used for gameplay decisions.
