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
For `full-control-v2`, it also prints and saves the evaluation objective and
maximum turn budget. Join and private health expose `turns_remaining`; it is
`null` until native play has an authoritative current turn and then decreases
from that turn without relying on spectator state.

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

Join reports either `strategic-v1` or `full-control-v2`. Do not mix their
commands. The sections below document both negotiated loops.

## Strategic-v1: observe

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

## Strategic-v1: act

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

## Full-control-v2: health and state

```sh
just health --session SESSION_FILE
just turn --session SESSION_FILE
just state --session SESSION_FILE --section overview
just state --session SESSION_FILE --section votes
just state --session SESSION_FILE --section cities --limit 16
just state --session SESSION_FILE --section city_detail --actor_id CITY_ID
just state --session SESSION_FILE --section city_citizens --actor_id CITY_ID --limit 16
just state --session SESSION_FILE --section city_build_choices --actor_id CITY_ID --limit 16
just state --session SESSION_FILE --section city_worklist --actor_id CITY_ID --limit 16
just state --session SESSION_FILE --section city_improvements --actor_id CITY_ID --limit 16
just state --session SESSION_FILE --section tile_window --center_id TILE_ID --radius 4
just state --session SESSION_FILE --section city_sites --limit 16
just state --session SESSION_FILE --section diplomacy --limit 16
just state --session SESSION_FILE --section diplomacy_clauses --relation_id RELATION_ID --limit 16
just state --session SESSION_FILE --section chat --limit 16
just state --session SESSION_FILE --cursor CURSOR
```

Use `turn` as the first command for each running-game decision. It makes
sequential authenticated reads and returns one bounded JSON briefing containing
health/evaluation context, overview, and the first 16 owned cities, owned units,
and technologies. All four state pages must have the exact same
`state_revision`; if the game advances during the read, the client restarts the
whole briefing once. Each truncated section includes its opaque continuation
cursor and `next_commands` shows the exact continuation. Economy and current
government are already in `overview.player`; available government choices are
in the `governments` section—`economy` and `government` are not state sections.

`health` reports only this seat's game, sidecar, phase, timing state, and its
latest durable `last_phase_end` attribution. A `source: "timeout"` event means
the supervisor auto-ended this exact seat's phase; no other seat's event is
returned.
`state` returns a bounded authenticated page. A cursor is exclusive: do not
combine it with a section or limit. Cursors remain valid for at least five
minutes, are safe to repeat after a lost response, and publish the RFC 3339
expiry of their next continuation as `cursor_expires_at`. An authentic expired
cursor returns retryable `cursor_expired` plus `details.restart`; an invented
cursor remains `invalid_request`. Every canonical page is at most 65,536 JSON
bytes, so a byte-bounded page may be shorter than its item limit. `cities`
contains compact owned-city summaries; its five actor-scoped child sections
carry details and independently pageable collections. `tile_window` is a
radius-0-through-8, topology/wrap-aware view centered on a known opaque tile;
unknown rows remain terrain/owner-redacted. `city_detail` and all city child
sections require exactly one current owned `--actor_id`;
`city_sites` is the separate fog-safe catalog used by city-target unit actions
and may include own, visible, or remembered foreign cities without internals.
`diplomacy_clauses` requires exactly one opaque `--relation_id` copied from the
matching `diplomacy` row; each relation is paged independently.

## Full-control-v2: enumerate and execute

```sh
just legal --session SESSION_FILE --kind research.set_target --all
just legal --session SESSION_FILE --kind phase.end --all
just legal --session SESSION_FILE --kind economy.set_rates --all
just legal --session SESSION_FILE --kind player.send_chat --all
just legal --session SESSION_FILE --kind player.cast_vote --all
just legal --session SESSION_FILE
just legal --session SESSION_FILE --actor_id ACTOR_ID --limit 16
just legal --session SESSION_FILE --actor_id ACTOR_ID --target_id TILE_ID
just legal --session SESSION_FILE --actor_id SELF_PLAYER_ID --target_id RELATION_ID
just batch --session SESSION_FILE --action_id ACTION_ID --arguments '{}'
just batch --session SESSION_FILE --action_id CHAT_ACTION_ID --arguments '{"channel":"global","message":"Hello"}'
just batch --session SESSION_FILE --action_id VOTE_ACTION_ID --arguments '{"vote_id":"VOTE_ID","vote":"yes"}'
```

Use only an action ID returned by `legal` at the latest exact state revision.
Use `--kind ACTION_KIND --all` when choosing one class of action. The pair is
required together so a matching action cannot be silently hidden on a later
page. The client drains the selected global, actor, or exact-target catalog
sequentially under the session request lock, validates and caches every full
descriptor exactly as normal `legal` does, then prints a compact projection.
That projection repeats the revision only once and retains each matching
action's opaque ID, exact kind, target, argument schema, and only useful
non-default probability or gold bounds. It shows at most 64 matches and at
most 48 KiB of action projections; `matched`, `shown`, and `truncated` make any
omission explicit. A catalog over the defensive 512-page drain ceiling fails
instead of returning a partial result.
Actor scopes enumerate one owned actor. Actor plus a tile performs an exact
known-target lookup. Self player plus an opaque `diplomacy[].relation_id`
enumerates that pair's semantic meeting, clause, acceptance, relation, and
withdrawal actions. Continue every returned relation cursor before executing
any item. Actor and relation pages share a stable `catalog_id`; only a final
page with `catalog_complete: true` atomically promotes its staged descriptors
for execution. V2 accepts exactly one command per batch. The player
client writes the exact canonical request to its mode-0600 sibling
`.v2-state` file before sending it.

## Full-control-v2: receipts, recovery, and waiting

```sh
just receipt --session SESSION_FILE --batch_id BATCH_ID
just retry --session SESSION_FILE --batch_id BATCH_ID
just wait --session SESSION_FILE
just wait --session SESSION_FILE --wait_s 120
just wait --session SESSION_FILE --until revision
```

If a POST has an uncertain transport outcome, check `receipt` first. `retry`
also checks the receipt first and resends only the exact persisted request when
the server proves that batch ID is absent. `applied`, `rejected`, and
`ambiguous` are terminal receipt states. Never resend `ambiguous`. After local
persistence, `batch` emits exactly one compact disposition with its batch ID:
`receipt_terminal`, `receipt_poll`, `receipt_first`, `retry_exact`, or
`refresh`.

`wait` defaults to the caller's actionable phase and returns a `wake_reason`;
an opponent revision cannot wake it. Use `--until revision` only when
deliberately following any private state change. The machine-readable contract
is [`full-control-v2.openapi.json`](full-control-v2.openapi.json), with custom
harness guidance in [`custom-harness-v2.md`](custom-harness-v2.md).
The Just recipe accepts both agent-friendly underscore options (`--wait_s`,
`--poll_s`) and their dashed spellings (`--wait-s`, `--poll-s`). Pass only one
spelling of each option. A local wait-command error does not end the game;
correct it and continue the same play loop until the game is terminal.

## Terminal result

```sh
just result GAME_ID
just result --game_id GAME_ID
```

The positional form is preferred; the named form remains compatible. Read
`result` only after the private loop reports a terminal state. There is no
live public-status command in this workspace because public standings can
contain opponent information that is absent from the private observation.

## Deliberately unavailable here

This player workspace has no recipes for starting/creating/cancelling games,
watching, replay, frames, video, saves, scorelogs, server logs, owner actions,
or internal bridge calls. Those belong to the separate owner/evaluation
surface and must not be used for gameplay decisions.
