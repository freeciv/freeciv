# Player commands

Run these commands from `freeciv/play/`. `just` by itself prints the short
workflow; `just --list` lists every player-safe recipe.

**This page is the full reference, written for harness authors.** An agent
playing a game does not need it: `just help` prints
[`play.md`](play.md) — the four fast paths, the aliases, and the two standing
rules — and a `full-control-v2` join prints the same contract as a protocol
card, which the client also writes to `state/header.txt`. Every doc character
an agent reads each game is part of its per-turn budget, so read this page
when you are building or debugging a harness, not when you are choosing a
move.

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

A `full-control-v2` join also prints a protocol card: the commands that exist,
what each alias form means and how long it lives, and the standing rule that
every error names the exact command that fixes it. The client writes the same
card to `state/header.txt` beside the session file, so the contract stays
re-readable without another join and without re-reading this page.

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

## Full-control-v2: output format

An ordinary turn is four commands — `just turn`, `just do "…"`,
`just turn --end --await`, and `just start` once in the lobby — documented
under "Full-control-v2: fast paths" below. Everything else on this page is
the deep path underneath them: the same capabilities, spelled out.

`join`, `health`, `turn`, `state`, `legal`, `batch`, `receipt`, and `retry`
print compact text by default and the full wire payload with `--json`. That
`--json` output is byte-identical to what these commands printed before the
text renderer existed, so machine consumers keep working unchanged. The
text-first `start`, `do`, and `show` accept `--json` too; `wait` has no text
form and always prints JSON, refusals included. Refusals otherwise follow the
same rule as successes: compact text by default, the byte-identical error
payload under `--json`.

A machine consumer that builds one shared argument vector for every subcommand
can set `PLAY_JSON=1` in the environment instead of appending the flag: it is
exactly `--json` on every command that declares one, and it never changes what
a command does, only which of its two renderings it prints. Any of `1`,
`true`, `yes`, or `on` (case-insensitive) selects it; anything else, including
an empty value, leaves the text default in place. Harness suites that shell
out to `client.py` and `json.loads` the result need either the flag at each
call site or this variable in the child environment.

The text form is a projection, never a different capability:

- One header line carries the envelope once — revision, turn, scope, and
  pagination or catalog status (`rev13/t1 legal scope=unit unit_… 16/22 more
  --cursor cursor_…`). `state_revision`, `agent_id`, `game_id`,
  `control_protocol`, and `schema_version` never repeat inside the body.
- `legal` prints one row per action, in this column order: its alias
  `a1..aN`, the action kind (plus `/operation` when the operation is not
  already the kind's suffix), the human label, and then one detail column
  holding, in order, the target (`T(x,y)` for a tile, `x,y` for bare
  coordinates, `→name` otherwise), `actor=` when the actor is not the page's
  own scope, every other public subject discriminator as `key=value`
  (`order=sentry`, `action=sabotage_city`) so aggregated kinds such as
  `unit.order` stay distinguishable, each `!`-marked non-default (below),
  `gold=`/`gold_range=`, and the argument schema as `{name:type,…}` when it is
  non-empty. A subject key the contract reserves for internals keeps its name
  and renders `<withheld>`, so a discriminator is never silently dropped.
  A row whose alias resolves omits the 32-hex `action_id` entirely — the alias
  *is* the handle — while a row the alias layer could not name still prints
  its opaque ID so it stays executable. `--json` always carries every field
  verbatim, including the full `action_id` of every row.
- Only *default* values are omitted: `probability` disappears only at exactly
  100/100, `legality` only at `legal`, `consuming` only at false, `variant`
  only at null. A non-default value always renders, prefixed with `!`
  (`!prob=0-100%/unknown`, `!legality=possibly_legal`, `!consuming`,
  `!variant=targeted_steal_tech`), because those are what turn a certain move
  into a gamble.
- `state` renders aligned tables; `tile_window` and `known_tiles` render a
  coordinate grid with two-letter terrain codes: uppercase for a tile this
  seat can currently see, lowercase for terrain remembered through fog, `?`
  for a tile whose terrain the seat has never been told, and `.` for a tile
  not on this page. The grid's `legend` line names every code it used.
  A tile is written `T(x,y)` — exactly the form `--center_id` and
  `--target_id` accept — while `@x,y` is just where a unit or city stands.
- Receipts render as one line: what was executed, the outcome and receipt
  state, the resulting revision, and the batch ID last.

A payload that does not match the documented contract is a rendering error
naming `--json`; the client never prints a blank in place of a value.

### Errors carry their own remedy

Every refusal names the exact command that fixes it: the restart query for an
expired cursor, the `just legal` form that re-issues an expired action alias,
the narrower scope after `scope_too_large`, the enumeration behind an order
`just do` could not resolve. Read the error and run what it names; do not
re-read this page, and do not guess a replacement ID.

### The session is implicit

`--session` is optional on every command, including `batch`, `retry`, and
`wait`. The client uses the explicit path when you pass one, then
`PLAY_SESSION`, then the single private session in this workspace. Nothing the
client prints repeats the session path, so no command below needs it.

With two or more joined seats in one workspace the client fails before any
request instead of guessing which seat is calling; pass the exact
`--session SESSION_FILE` your join printed, or export `PLAY_SESSION` once. The
session file is private: never print it, paste it, or copy its contents.

## Full-control-v2: aliases

Every place the CLI accepts an opaque ID — `--action_id`, `--actor_id`,
`--target_id`, `--center_id`, `--relation_id` — it also accepts the short
alias printed in the text output. The client expands the alias against the
private cache it already keeps *before* it builds the request, so the wire
still carries only the server-issued opaque ID. Nothing about the protocol
changes; this is typing relief, not a second way to act.

| Alias | Names | Lifetime |
|---|---|---|
| `a1 a2 …` | one enumerated action | the revision it was enumerated at |
| `u1 c1 p1 r1` | a unit, city, player, or diplomatic relation | the whole game |
| `T(31,72)` | a tile the seat has already seen | the whole game |

```sh
just legal --actor_id u1
just legal --actor_id u1 --target_id "T(31,72)"
just state --section city_worklist --actor_id c1
just state --section tile_window --center_id "T(31,72)" --radius 4
just batch --action_id a3 --arguments '{"name":"London"}'
```

Quote `T(x,y)` in a shell; the parentheses are shell syntax otherwise.

- Action aliases are numbered in enumeration order as pages arrive, and are
  stored with the revision they came from. **They die on the next revision.**
  Any action you take bumps the revision, so after every executed command the
  old `a1..aN` are refused with an error naming the exact `just legal` command
  that re-enumerates them. A stale alias never resolves to a new action: only
  a fresh enumeration re-issues the numbers.
- Entity aliases are assigned in first-seen order and are stable for the whole
  game, because the underlying entity IDs are. `u3` on turn 40 is the same unit
  it was on turn 3; an alias is never re-used for a different entity.
- Tile aliases resolve from the tiles this seat has already been shown
  (`tile_window`, `known_tiles`, `map_tiles`, unit and city positions, and
  action targets). A coordinate the seat has not seen is refused and the error
  names the nearest cached tiles — reading a tile alias can never see past fog.
- An unknown alias is refused with the closest known ones listed. Aliases only
  ever *narrow* what you can do: whatever an alias would expand to, you can
  always type the opaque ID instead, and `--json` always shows the full IDs.

## Full-control-v2: fast paths

Four commands cover the ordinary turn, and a fifth reads local files instead
of the network. Each one is sugar over the same capabilities `just legal`
enumerates and `just batch` submits — never a separate channel, never a
ceiling. Anything they do not cover stays reachable with `just legal` +
`just batch`, unrestrained.

```sh
just start --nation English --leader Ada --female     # lobby: configure + ready
just turn                                            # the briefing
just do "u1 found_city London; u2 move 32,73"        # 1..8 orders
just turn --end --await                              # end phase, block, header
just show u1                                         # read files, zero network
```

### `just do "ORDER; ORDER; …"`

One to eight semicolon-separated orders. An order is `<alias> <verb> [args]`,
a family form (`research set_goal Currency`, `phase end`), or a bare action
alias (`a7 London`).

- **The verb must be one the catalog advertised**: an action's public kind
  (`unit.found_city`), the tail of that kind (`found_city`), its operation
  (`found`), or the `kind/operation` form the option table prints. No synonym
  is invented, so every verb that resolves is a verb you read on a page.
- A leading `31,72` or `T(31,72)` after the verb selects the action's target.
  The remaining words fill that action's own argument schema in order —
  required properties first — converted to the declared type. An action that
  takes no arguments can instead be selected by its label, its named target,
  or a subject value the option table showed.
- **Resolution is entirely local.** Orders are matched against the cached
  catalog for the newest revision this seat knows. Nothing is guessed: if any
  order does not select exactly one cached capability, the whole batch is
  refused before a single request, with one line per order saying resolved or
  unresolved and the exact `just legal` command to run.
- Orders execute sequentially as one single-command wire batch each — the wire
  rule is unchanged — and print one receipt line each. Execution stops at the
  first order that is not accepted; pass `--continue-on-error` to keep going.
- An order that lands bumps the revision and expires every outstanding handle.
  The client re-enumerates exactly what the remaining orders name and re-binds
  them before sending; it never submits a handle it already knows is stale. If
  the new revision no longer offers a remaining order, execution stops and
  says so.

### `just turn --end --await`

Ends this phase with the cached `phase.end` capability (enumerating it
internally when it is not cached), blocks exactly as `just wait` does, then
prints the next phase's header line. `--wait_s` and `--poll_s` behave as they
do on `just wait`. Plain `just turn` is unchanged. `--await` without `--end`
is refused: use `just wait` to block without ending.

### `just start --nation NAME --leader NAME --male|--female [--style NAME]`

Lobby only. Resolves the nation and style by name, case-insensitively, from
the pregame catalogs (read from the local mirror when it holds them, fetched
internally once otherwise), then submits `pregame.configure` and — after the
mandatory re-enumeration, because configuring bumps the revision — the freshly
enumerated `pregame.set_ready`. Without `--style` the nation's own default
style is used. When readiness is not enumerable the command stops and names
the `just legal` command that would show it.

### `just show [NAME|--grep PATTERN]`

Reads this seat's local state mirror. **This command never opens a socket**,
so it costs no request budget and no server load.

```sh
just show                    # header card, what changed, and the file list
just show units              # one projection: header overview units cities
                             # map delta nations styles governments
just show u1                 # that entity's rows plus its option catalog
just show --grep found_city  # search every mirror file, file:line
just show --grep 'u[0-9]+ Settlers' --regex
```

`--grep` matches literal text, case-insensitively; `--regex` reads the pattern
as a regular expression instead. The literal form cannot backtrack, so it is
the default: a regular expression that overlaps with itself can take
exponential time on one long `map.txt` line, and `--regex` is therefore
refused for an already-quantified group and abandoned with a remedy if the
search exceeds its wall-clock budget.

The mirror lives beside the session file, is rewritten on every response the
client ingests, and is a projection of pages this seat already received — so
reading it can never see past fog. The private `.v2-state` cache is not part
of it and is never readable through `just show`.

```
.sessions/GAME_ID/SEAT/
  state/header.txt overview.tsv units.tsv cities.tsv map.txt delta.md
  state/options/<alias>.txt
  cache/nations.tsv styles.tsv governments.tsv
```

Files are TSV and fixed-width text with a `# rev N turn T` first line. Read
them with `just show`, `grep`, or any file read — they cost no context until
you look.

## Full-control-v2: health and state

```sh
just health
just turn
just state --section overview
just state --section votes
just state --section cities --limit 16
just state --section city_detail --actor_id CITY_ID
just state --section city_citizens --actor_id CITY_ID --limit 16
just state --section city_build_choices --actor_id CITY_ID --limit 16
just state --section city_worklist --actor_id CITY_ID --limit 16
just state --section city_improvements --actor_id CITY_ID --limit 16
just state --section city_trade_routes --actor_id CITY_ID --limit 16
just state --section tile_window --center_id TILE_ID --radius 4
just state --section city_sites --limit 16
just state --section diplomacy --limit 16
just state --section diplomacy_clauses --relation_id RELATION_ID --limit 16
just state --section chat --limit 16
just state --section chat_recipients --limit 16
just state --cursor CURSOR
```

Use `turn` as the first command for each running-game decision. It makes
sequential authenticated reads and returns one bounded briefing built from
health/evaluation context, overview, and the first 16 owned cities, owned units,
and technologies: a header line, an economy/research line, unit lines grouped by
identical type, tile, and status, city lines, and a closing `needs decision:`
line. `--json` returns the same briefing as the full JSON object. All four state pages must have the exact same
`state_revision`; if the game advances during the read, the client restarts the
whole briefing once. Each truncated section includes its opaque continuation
cursor; the text briefing prints it as a `next:` line and `--json` carries the
same continuation in `next_commands`, so a truncated section is never a dead
end. A `needs decision:` count taken over a truncated section says so. Economy and current
government are already in `overview.player`; available government choices are
in the `governments` section—`economy` and `government` are not state sections.

Every page these commands return is also projected into the local mirror, so a
follow-up that only needs what you already fetched costs no request:
`just show units`, `just show map`, `just show c1`, or
`just show --grep PATTERN`. The mirror is exactly as fresh as your last read
and never fresher — when freshness is what you need, read the section again.

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
just legal --kind research.set_target --all
just legal --kind phase.end --all
just legal --kind economy.set_rates --all
just legal --kind player.send_chat --all
just legal --kind player.cast_vote --all
just legal --kind unit.order --all --offset 0 --limit 16
just legal
just legal --actor_id ACTOR_ID --all
just legal --actor_id ACTOR_ID --target_id TILE_ID --all
just legal --actor_id ACTOR_ID --limit 16
just legal --actor_id ACTOR_ID --target_id TILE_ID
just legal --actor_id SELF_PLAYER_ID --target_id RELATION_ID
just batch --action_id ACTION_ID --arguments '{}'
just batch --action_id CHAT_ACTION_ID --arguments '{"channel":"global","message":"Hello"}'
just batch --action_id CHAT_ACTION_ID --arguments '{"channel":"private","recipient_id":"PLAYER_ID","message":"Hello privately"}'
just batch --action_id VOTE_ACTION_ID --arguments '{"vote_id":"VOTE_ID","vote":"yes"}'
```

`player.send_chat` is available both before the game starts and while it is
running. For private chat, read `chat_recipients`, choose an opaque `id` whose
row has `can_message: true`, then enumerate the same-revision chat action and
submit that ID as `recipient_id`. `can_message` is true only when the player is
connected and its current name is safe and unambiguous for Freeciv's
`PlayerName:` syntax; native code revalidates that fact immediately before
sending. Rows expose only player IDs, names, `self`, `connected`, and
`can_message`; native player and connection numbers stay private. Global/allied
chat omits `recipient_id`.
Messages are strict UTF-8 of 1–512 encoded bytes and reject leading/trailing
ASCII U+0020 plus Unicode `Cc`/`Cf` code points. Colons and command-looking
text are safe literal message text because the native client prepends one ASCII
space, `.`, or the selected exact `PlayerName:` routing prefix for global,
allied, or private chat respectively. The server parses the protective global
space as public chat and trims it before display. There is no server console or
direct connection-message action.

Use only an action ID returned by `legal` at the latest exact state revision.
Use `--kind ACTION_KIND --all` when choosing one class of action. The pair is
required together so a matching action cannot be silently hidden on a later
page. Use `--actor_id ACTOR_ID --all` (optionally with `--target_id`) to read
one actor's complete catalog in one command: same drain, same validation, same
atomic promotion, no kind filter and no cursor to follow. `--all` always needs
one of those two scopes. The client drains the selected global, actor, or exact-target catalog
sequentially under the session request lock, validates and caches every full
descriptor exactly as normal `legal` does, then prints a compact projection.
That projection repeats the revision only once and retains each matching
action's opaque ID, exact kind, human-readable label, compact semantic
`subject`, target, argument schema, and only useful non-default probability or
gold bounds. `subject.operation` and the descriptor's other public discriminator
fields are preserved, so aggregated kinds such as `unit.order`,
`unit.perform_action`, `city.assign_citizen`, and `city.manage_worker_task`
remain distinguishable without fetching one full page at a time. Target,
probability, and gold cost stay in their existing top-level projection fields;
an implementation-reserved subject key keeps its name and renders `<withheld>`,
so a discriminator is never silently dropped. `--kind KIND --all` shows at most
64 matches. `--actor_id ID --all` promises one actor's *whole* menu — a real
unit catalog runs past 64 rows — so it is bounded by the 48 KiB projection cap
alone unless you ask for a window with `--limit`. `matched`, `shown`, and
`truncated` make any omission explicit, and the header prints the exact
`just legal … --offset N` command that continues it. A catalog over the
defensive 512-page drain ceiling fails instead of returning a partial result.
When `has_more` is true, repeat the exact same catalog query (same scope, same
kind) with `--offset NEXT_OFFSET`; `next_offset` identifies the first omitted matching
descriptor even when `byte_limited` is true. `--limit 1..64` selects the compact
window size; it defaults to 64 for the `--kind` form and is uncapped for the
`--actor_id` form. Offset windows follow the fully drained
catalog's deterministic order, and every drain still validates and caches all
full descriptors. If one compact descriptor alone exceeds the normal 48 KiB
window, the client returns that one descriptor (and `oversized_single: true`)
under the legal-page 64 KiB hard bound so `next_offset` always makes progress.
All windows must report the same `state_revision`; if the
revision changes between calls, discard the partial sequence and restart at
offset 0 because the old action IDs are expired. Without `--all`, `--limit`
keeps its existing server-page meaning and remains restricted to 1..16, and
`--offset` is invalid.
When an actor's freshly drained catalog offers exactly the same choices in the
same order — same kinds, same operations, same targets by coordinate — as
another actor's complete catalog already read *at this same revision*, the text
output says so in one line instead of reprinting it:

```
rev13/t1 legal scope=unit u4 22/22 matched (catalog 22 complete, pages 2)
u4 == u3 (rev13) a23..a44
```

The trailing run is this actor's own action aliases, row for row against the
catalog it matched, so every option stays executable without reprinting it:
`a23` is u4's version of u3's first row. Any row whose own detail differs
(probability, legality, cost, arguments) is still printed in full under that
line, and the line then ends `except 2 rows`. The comparison never crosses a
revision, never compares a windowed (`--offset`/`--limit`/`byte_limited`)
result, and never applies to `--json`. Both catalogs are fully cached either
way, so `batch` works exactly as if both had been printed.

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
just receipt --batch_id BATCH_ID
just retry --batch_id BATCH_ID
just wait
just wait --wait_s 120
just wait --until revision
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
