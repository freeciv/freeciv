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
`claude-code-claude-opus`. In a workspace materialized by the repository
root's `play` launcher recipe, a mode-0600 `.playconfig.json` pre-records
the assigned game ID, controller name, and optional seat, so bare
`just join` works with no arguments; explicit arguments always override it.
Multiplayer assignments may select a numbered seat:

```sh
just join --game_id GAME_ID --name claude-code-claude-opus --place 2
```

The player Just recipe reads the join token from the mode-0600
`.invites/GAME_ID.json`; it has no join-token command-line option. It may also
select a staged file with `--invite PATH`. The lower-level `client.py` retains
`AGENT_EVAL_JOIN_TOKEN` and a direct CLI option for controlled integrations,
but neither is part of the recommended Just argv path; never put a bearer in a
shared command line. On success, the client creates a
mode-0600 session beneath `.sessions/GAME_ID/`, binds this workspace to it
(see "One workspace, one seat"), and returns its `session_file` path under
`--json` for harnesses that need it. It also prints and saves the exact
timing contract:
`default` is 180 seconds per agent
turn on `strategic-v1` and 600 seconds (10 minutes) on `full-control-v2`,
`blitz` is 60 seconds (`strategic-v1` only), and `infinite` has no agent
deadline.
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
just next --after_turn 0
```

Then pass the last observed turn:

```sh
just next --after_turn 42
```

The default long-poll is 120 seconds. A `waiting` response is not a game turn
and does not change `LAST_TURN`; poll again with the same value. Both commands
run against the seat join bound this workspace to ("One workspace, one seat"
below); `--session SESSION_FILE` still overrides it for a harness that drives
several workspaces from one process.

## Strategic-v1: act

Copy the exact top-level `turn` and `observation_id` from `next`, then submit
all four integer trait targets:

```sh
just act \
  --turn 43 \
  --observation_id OBSERVATION_ID \
  --action '{"type":"set_traits","traits":{"aggressive":0,"builder":20,"expansionist":30,"trader":10}}'
```

Targets must be integers from `-49` through `50`. Submit once. An exact retry
is safe; a conflicting revision is rejected. Advance `LAST_TURN` only after
the response contains `accepted: true`. On every error keep the old value and
poll again for the same seat.

## Full-control-v2: output format

An ordinary turn is **one command** — `just do "…" --end --await --brief` —
documented under "Full-control-v2: fast paths" below, with `just turn` and
`just start` for the first turn and the lobby. Everything else on this page is
the deep path underneath them: the same capabilities, spelled out.

`join`, `health`, `turn`, `state`, `legal`, `batch`, `receipt`, and `retry`
print compact text by default and the full wire payload with `--json`. That
`--json` output is byte-identical to what these commands printed before the
text renderer existed, so machine consumers keep working unchanged. The
text-first `start`, `do`, `show`, and `wait` accept `--json` too: `wait`
prints its wake reason, phase, and health as compact lines like every other
command, with the wire envelope behind the flag. Refusals follow the same rule
as successes: compact text by default, the byte-identical error payload under
`--json`.

A rendered refusal leads with its remedy, and the remedy is whatever the
payload can actually support. `error.details.safe_next` becomes the
receipt-first recovery command. A `rate_limited` refusal carries
`details.retry_after_seconds` and an RFC 3339 `details.retry_after`, and prints
`next: retry the same command in 12s (not before …)` — a rate limit's only
remedy is a clock. A retryable `cursor_expired` or `stale_revision` on a paged
GET carries `details.restart`, and that query is printed as the command that
restarts the chain (`next: just state --section known_tiles --limit 16`); a
restart naming an option this CLI cannot spell prints no command rather than
an unrunnable one. The standing `full payload: re-run the same command with
--json` line is appended only when some detail is a nested value the compact
form had to elide — never after a rate limit, a restart query, a scalar
detail, or a transport failure, all of which are already printed whole.

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

### One workspace, one seat

A successful join binds this workspace to the seat it joined, so no command
below takes a session argument. The binding lives in the mode-0600
`.sessions/current-seat.json` and holds a workspace-relative session path and
a game ID — never a token.

The client resolves the seat in this order: an explicit `--session`, then
`PLAY_SESSION`, then the workspace binding, then a sole unbound session. Only
an *unbound* workspace holding two or more sessions is refused, before any
request, and the refusal names `just use`.

```sh
just use                 # the seat this workspace plays
just use GAME_ID         # rebind it to another game you joined
just use SESSION_FILE    # rebind it to an exact session
```

Joining a second game rebinds the workspace and says so in one line. Two
seats played from one workspace is unsupported: copy the workspace per seat,
which is what the e2e harnesses already do. The session file is private:
never print it, paste it, or copy its contents.

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
  stored with the revision they came from **and with what they mean** — actor,
  kind, operation, normalized target, and argument-schema shape. **The number
  dies with its revision**, but the meaning survives: given a stale alias,
  `just do` and `just batch` re-run the same scoped drain they would have run
  anyway, re-resolve the alias by that semantic identity, restore the old
  numbers for every action that is unchanged, print one line
  (`a3 rebound at rev14`), and proceed. A semantic action that is gone, or that
  now names two actions, fails closed naming the `just legal` command that
  re-enumerates. The wire only ever carries the fresh revision-bound
  `action_id`. Pass `--no-refresh` for the bare refusal with no extra request.
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
just start                                           # lobby: configure + ready
just turn                                            # the briefing
just do "u1 found_city London; u2 move 32,73"        # 1..8 orders
just turn --end --await                              # end phase, block, header
just show u1                                         # read files, zero network
```

#### Fast turns: one call per turn

`--end`, `--await` and `--brief` compose those steps into a single command, so
a steady-state turn costs one model round trip instead of four or five:

```sh
just do "u3 route 40,60; c2 build Temple" --end --await --brief
```

That orders every actor, ends the phase, blocks for the next one, and prints
its whole briefing — including the `next N actors: just do "…"` line that
writes the following turn's command. With nothing left to order, the same turn
is `just turn --end --await --brief`. The batching is the point: the briefing
and every receipt tail name every actor that needs orders in one composed
`just do` line, because one call carrying eight orders costs what one call
carrying one order costs.

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
- **Six Tier-1 words are the only sugar**, each pinned to one advertised
  capability and to the arguments the word itself fixes: `route` and `patrol`
  are `unit.order/set_route` with `mode` goto and patrol, `build` is
  `city.set_production`, `queue` is `city.set_worklist`, `rally` is
  `city.set_rally` (non-persistent), and `goal` is `research.set_goal`. A
  Tier-1 word whose capability the actor's cached catalog does not advertise
  fails closed naming the enumeration command. Nothing else is added: the
  vocabulary is fixed, documented in `play.md`, and never inferred.
- **An ordered list takes the whole tail.** An action whose schema declares a
  single array property (`set_route`'s `waypoints`, `set_worklist`'s `items`)
  binds one element per remaining word, each resolved to the opaque ID the
  wire needs: coordinates through the tile alias cache, every other word by
  name against the same actor's own catalog. A word the catalog never named
  is refused, not guessed.
- **Resolution is entirely local**, with exactly one exception. Orders are
  matched against the cached catalog for the newest revision this seat knows.
  When an order names an actor whose catalog this seat has *never* read at
  this revision, `do` drains that one actor's scoped catalog — the same drain
  `just legal --actor_id X --all` runs — prints `fetched u1 options (rev9)`,
  and re-resolves. Every unread actor in the batch is fetched before anything
  is sent, so the whole-batch pre-flight still holds: a bad verb refuses with
  no order on the wire. An actor whose complete catalog is already cached is
  never re-fetched — a verb it does not offer is a real refusal — and
  `--no-refresh` keeps the plain refusal in every case. Nothing else is
  guessed: if any order still does not select exactly one cached capability,
  the whole batch is refused before a single request, with one line per order
  saying resolved or unresolved and the exact `just legal` command to run.
- **A refusal raised while the cached phase header says this seat is not
  active** leads with `your phase is not active (state X) — just wait` and
  drops the re-enumeration remedy, which could not have helped. `just batch`
  and `just legal` carry the same prefix from the same local header.
- Orders execute sequentially as one single-command wire batch each — the wire
  rule is unchanged — and print one receipt line each. Execution stops at the
  first order that is not accepted; pass `--continue-on-error` to keep going.
- **A refused order prints what its actor can do instead**, so fixing it
  costs no extra command. Under the receipts, each refused actor gets one
  section — `u24 can (rev41/t18): 12 of 37 shown — all: just legal --actor_id
  u24 --all`, then the same rows `just legal --actor_id u24 --all` prints,
  alias first — bounded to 12 rows and the first 3 refused actors. It costs
  no round trip when the refusal did not move the revision, because the
  cached catalog still stands; when it did, that one actor's catalog is
  re-drained. A lookup that fails prints nothing at all: a refusal never
  reads worse because the help beside it could not be built. `--json` output
  is unaffected, and in `--json` mode nothing is fetched.
- A successful `just do` or `just batch` ends its text output with one tail
  line naming what still needs orders, from the same heuristic as
  `just turn --decisions`. With more than one actor left it is the composed
  batch — `next 3 actors: just do "u2 road T(31,72); u5 goto T(40,60); c3
  build Temple"` — each order being that actor's top-ranked cached option,
  ready to run as printed and ready to edit, capped at 200 characters and
  falling back to `next N actors need orders — just turn --decisions` when
  even two orders will not fit. With exactly one actor left it is that
  actor's row and its options; with none it is
  `next: no actors need orders — just turn --end --await --brief`. It is
  computed from local files only — the receipt path never opens an extra
  socket — so caches too stale to name options degrade to the actor and its
  `just legal` command. `--json` output is unaffected.
- **`--end` composes the phase end onto the batch.** After every order in the
  batch applies, `do` resolves and executes `phase.end` through the same
  cached-or-enumerated path `just turn --end` uses, still as its own
  single-command wire batch. A batch that did not finish never ends the
  phase: without `--continue-on-error` a refused order leaves the turn open
  and the receipt says `phase NOT ended: N/M orders applied…`; with it, the
  end runs once the batch has finished. An end that fails after orders have
  applied never swallows them — the orders' receipts print first, then
  `phase NOT ended: …` with its own remedy. `--await` then blocks exactly as
  `just wait` does, and `--brief` prints the whole next briefing; both are
  refused without `--end`, naming the form that works.
- An order that lands bumps the revision and expires every outstanding handle.
  The client re-enumerates exactly what the remaining orders name and re-binds
  them before sending; it never submits a handle it already knows is stale. If
  the new revision no longer offers a remaining order, execution stops and
  says so.

### `just turn --decisions`

The freeciv focus loop as a projection: one row per owned actor that can still
act this phase — a unit with moves left, idle and carrying no standing route; a
city with an empty or completing build queue; a relation with an unanswered
meeting — each with its state and its most relevant options as aliases,
bounded to 120 characters a row.

A meeting row does not wait for a diplomacy action to be cached: an open
meeting the seat has not accepted is read straight out of the `diplomacy`
mirror table, so `r1 meeting pending: Isabella, cease-fire, 2 clauses` appears
whether or not this seat has ever enumerated that relation. Its remedy is the
only query that reaches a clause — this seat's own player bound to the
relation, `just legal --actor_id p1 --target_id r1 --all` — because diplomacy
is never in the global catalog and a relation is never an actor. A meeting is
never folded into the composed batch line for the same reason: its actor is
your player, not the relation the row is named after. When the `diplomacy`
section has never been read the row still appears, saying only what it knows
and naming the read that would say more:
`meeting pending (unread: just state --section diplomacy --limit 16)`.

Options are ranked by what a player reaches for: found/build/route/work verbs
first, then any verb this client does not recognise, then the housekeeping that
merely parks an actor (sentry, fortify, disband, cancel). Rows come from the
local mirror and the revision-scoped action cache; only what is stale is
fetched, and a catalog already drained at this revision is never re-fetched.
Refused with `--end`: list what needs orders before ending the phase, not with
it.

The list closes with the composed batch it implies — the same
`next N actors: just do "…"` line the receipt tail prints — so reading who
needs orders and writing the one command that orders them is a single call.
With no actor left it prints `no actors need orders; just turn --end --await
--brief`.

### `just turn --end --await [--brief]`

Ends this phase with the cached `phase.end` capability (enumerating it
internally when it is not cached), blocks exactly as `just wait` does, then
prints the next phase's header line. `--wait_s` behaves as it does on
`just wait`, except that the block runs to the holder's deadline when another
seat holds the phase, so one call carries one turn in multiplayer. Plain
`just turn` is unchanged. `--await` without `--end` is refused: use
`just wait` to block without ending.

`--brief` renders the whole next-turn briefing after the wake — byte for byte
what plain `just turn` would print, decisions lines and events line included —
so the next turn starts with zero read calls. It runs the same briefing fetch
and renderer in process: one command, two logical steps. The extra state reads
are the ones `just turn` would have made anyway; the round trip saved is the
model's. `--brief` is refused without `--end --await`, naming that form.

A briefing that cannot be built after a successful end is never swallowed: the
end receipt and the wake header print, then `briefing failed: …` and
`next: just turn`, with a non-zero exit.

`--json` on `--brief` returns the composite the two commands would have
returned, as one object:

```json
{
  "schema_version": 1, "command": "turn", "status": "briefed",
  "end":  { "…": "the disposition `turn --end --json` puts under `disposition`" },
  "wait": { "…": "the wake `just wait --json` returns, or null" },
  "turn": { "…": "the result `just turn --json` returns, or null" },
  "turn_error": null
}
```

`turn` is null exactly when `turn_error` carries the message. This is a new
surface: `just turn --end --await --json` without `--brief` still returns the
`{"status": "ended", "disposition", "wait"}` shape it always has.

`just do "…" --end --await --brief --json` returns the same three keys added
beside the `do` payload's own fields, which are untouched.

### `just start [--nation NAME --leader NAME --male|--female --style NAME]`

Lobby only, and **every flag is optional** — bare `just start` means "get me
into the game". What each omitted flag resolves to:

- `--nation`: a random nation from the pregame catalog. Nations are cosmetic
  under this eval's fixed-trait setup. A lobby offering none fails closed
  naming `just state --section pregame_nations`.
- `--leader`: this seat's controller label, reduced to what the boundary
  accepts (letters, digits, spaces, `'`, `.`, `-`; at most 47 UTF-8 bytes),
  falling back to the leader name the lobby `overview` already holds.
- `--male`/`--female`: the seat's current sex from that same `overview`, and
  failing that a deterministic pick over the resolved leader name, so two runs
  of the same bare command configure the same seat.
- `--style`: the chosen nation's `default_style_id`.

One line reports what was resolved before anything is submitted —
`starting as English — Ada (female), style European`.

Named nations and styles resolve case-insensitively from the pregame catalogs
(read from the local mirror when it holds them, fetched internally once
otherwise). The command then submits `pregame.configure` and — after the
mandatory re-enumeration, because configuring bumps the revision — the freshly
enumerated `pregame.set_ready`. When readiness is not enumerable the command
stops and names the `just legal` command that would show it.

### `just show [NAME|--grep PATTERN]`

Reads this seat's local state mirror. **This command never opens a socket**,
so it costs no request budget and no server load.

```sh
just show                    # header card, what changed, and the file list
just show units              # one projection: header overview units cities
                             # diplomacy map yields delta nations styles
                             # governments
just show map --yields       # terrain grid overlaid with food/shields/trade
just show u1                 # that entity's rows plus its option catalog
just show r1                 # one relation: counterpart, state, meeting
just show --grep found_city  # search every mirror file, file:line
just show --grep 'u[0-9]+ Settlers' --regex
```

`just show map --yields` overlays each tile's `food/shields/trade` on the
terrain grid, using only yields already ingested from a `city_citizens` page —
it fetches nothing, and a tile whose yields this seat has not read renders `?`.

Every projection stamps the revision it was rendered at. When the seat has
since learned a newer one, `show` leads with `stale: rendered at rev9, now
rev12 — aliases will be re-verified by meaning on use`. The banner is computed
when the file is read and is never written back: rewriting a projection to say
it is old would change the very stamp being judged, and an option file read
this way still opens with its own `# rev 9 turn 3`.

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
  state/header.txt overview.tsv units.tsv cities.tsv map.txt yields.tsv
  state/diplomacy.tsv
  state/delta.md
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

The header carries this seat's own civilization score — the value the
evaluation optimizes — as `score 23 (citizens 7 techs 5)` when the boundary
can prove the exact number, or `score >=17 (…)` when it can only prove a lower
bound from the seat's own rows. Every term the private observation does not
carry is non-negative and named in `overview.score.unobserved`, so the bound
never reads high. The same projection leads the `overview` state page and is
recorded in the mirror, which means `state/delta.md` shows the score moving
between revisions. A server that does not emit `overview.score` prints no
score line, and the lobby has none.

Below `needs decision:` the text briefing prints the same per-actor rows
`just turn --decisions` renders — one line per actor that still needs orders,
with its most relevant options as aliases — for up to eight actors, then
`+N more actors — just turn --decisions`. These rows are built from the
mirror tables this briefing just wrote and the revision-scoped action cache;
they never fetch, so an actor whose catalog is not cached degrades to its own
`just legal --actor_id X --all`. The briefing also closes with
`events: N new — just state --section chat` when the typed event feed has
grown since the last briefing, counted from the event total the overview
projection recorded. Both are rendering only: `--json` is unchanged.

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

`diplomacy` renders one row per relation — counterpart, nation, relation state,
embassy, treaty turns left — and marks an open meeting on the row itself
(`!meeting open 2 clauses accepted by them, awaiting you`), then names the one
page that carries the clauses: `just state --section diplomacy_clauses
--relation_id r1`. It is also mirrored, so `just show diplomacy` and
`just show r1` re-read it without a request.

A `governments` row that reports `can_change yes` closes with
`government actions are player-scoped: just legal --actor_id p1 --all`.
`government.*` is enumerated only inside this seat's own player scope and
never in the global catalog, so a `--kind government.` search of the global
catalog matching nothing is not evidence that the rules forbid the change.

### Reading a city

`city_detail` prints every number it carries rather than eliding the nested
ones. A city is a head line (alias, name, position, size, `f/s/t` surplus, and
`!pollution N` when there is any), then:

- `granary 14/20 food +2/turn grows in 3t` — the growth counter verbatim:
  positive turns to the next citizen, `!full, growth blocked` when the granary
  is full but the city may not grow, `!starving, famine in Nt` when food is
  negative, `no growth` when the surplus is exactly zero.
- `citizens 7: 1 happy, 2 content, 3 unhappy, 1 angry !disorder` — the mood
  split, and `!disorder` on exactly the server's own test,
  `happy < unhappy + 2 × angry`, over the same final-feeling counters.
- `build City Walls improvement 0/60 shields +1/turn done in 60t · buy 240 gold`
  — what the shields are for and what finishing now costs, plus `!locked` when
  this city has already bought this turn.
- an `output base gross waste unhappy net used surplus` table. `base` is what
  the worked tiles and specialists yield, `gross` what the multipliers make of
  it, `waste` corruption, `unhappy` the disorder penalty, `used` upkeep, and
  `surplus` what is left. A column that is zero on every row, or a step that
  merely repeats the step before it, is dropped, so a calm city prints three
  columns and a besieged one prints the terms that explain where its shields
  went.
- one line per collection that lives in a child section, as the exact command
  that prints it: `19 tile yields: just state --section city_citizens
  --actor_id c1`. There is no ellipsis to guess past.

`city_citizens` leads with `worked 2 of 16 rows on this page: f4 s3 t1`, which
is the `base` row of `city_detail` restricted to this page, and names any
specialists placed.

### Emergency buys and production switches

These are ruleset mechanics, stated from the rules code rather than from
experience, because each one is expensive to discover during a siege:

- **A rush buy from an empty shield stock costs double.** The price is
  `2 × missing shields`, plus `missing² / 20` for a unit, and that total is
  doubled when the stock is exactly zero. One turn of production, or a single
  shield, therefore roughly halves the bill. The `city.buy_production` action
  carries no price, so the price you plan against is `buy=N` on the `cities`
  row and `buy N gold` in `city_detail`.
- **One buy per city per turn, and the buy locks that city's build.**
  Afterwards `production.can_change` is false until next turn, so choose what
  to build before you rush it; `city_detail` prints `!locked` when it happens.
- **Selling is also once per city per turn, and it does not block a buy** —
  what a sale blocks is a second sale in that city. `city_detail` prints
  `!sold here this turn`.
- **Changing production class forfeits half the accumulated shields.** The
  classes are unit, ordinary improvement, and wonder; a change within a class
  is free, and so is a change made on the turn right after the city produced
  something. `city_build_choices` prices each switch against this city's real
  stock and marks it `!forfeits 13 of 25 shields` — and prints nothing of the
  sort unless the stock is known at this page's own revision.
- A city founded this turn cannot buy, `Coinage` cannot be bought, and a unit
  cannot be bought while the city is in disorder — which is the `!disorder`
  flag above, from the same counters the server tests.

## Full-control-v2: enumerate and execute

```sh
just legal --kind research.set_target --all
just legal --kind phase.end --all
just legal --kind economy.set_rates --all
just legal --kind player.send_chat --all
just legal --kind player.cast_vote --all
just legal --kind unit.order --all --offset 0 --limit 16
just legal
just legal --full
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

The default **text** rendering of the *global* (unscoped, unfiltered) catalog
groups rows by action kind and collapses two classes of family to one line
each: bulk housekeeping (`player.propose_server_setting*`, votes, chat
recipients) to `governance: 50 setting proposals — just legal --kind
player.propose_server_setting --all`, and choice families (`research.set_goal`,
`research.set_target`) to one row listing the choice names inline with their
aliases. A row carrying a non-default probability, legality, consuming flag or
variant, or any gold field, is never collapsed — it always prints individually,
even inside a collapsed family. `--full` restores the flat list. Scoped
(`--actor_id`/`--kind`) renderings and `--json` are unchanged, and so is the
staged descriptor cache: this is rendering only.

Use only an action ID returned by `legal` at the latest exact state revision.
Use `--kind ACTION_KIND --all` when choosing one class of action. The pair is
required together so a matching action cannot be silently hidden on a later
page. `--kind` accepts exactly what the rendered kind column prints: the
public kind (`unit.order`), which selects every operation under it, or the
`kind/operation` form (`unit.order/move`), which selects the one row it was
copied from. A kind that matches nothing in the drained catalog is an error
listing the kinds that catalog really carries, and — when the local cache
knows better — the actor scope that holds it (`unit.order is an actor-scoped
kind; this seat holds it for u1 …`). An empty page is never printed about a
catalog that was not searched, and a malformed kind lists the kinds this seat
has read rather than refusing bare. Use `--actor_id ACTOR_ID --all` (optionally with `--target_id`) to read
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

`wait` prints the same compact header `turn --end --await` does — whose phase
it is, the game state, then the `health` one-liners — and carries the full
wake envelope under `--json`. It defaults to the caller's actionable phase and
returns a `wake_reason`; an opponent revision cannot wake it. Use
`--until revision` only when deliberately following any private state change.
The machine-readable contract is
[`full-control-v2.openapi.json`](full-control-v2.openapi.json), with custom
harness guidance in [`custom-harness-v2.md`](custom-harness-v2.md).
The Just recipe accepts both agent-friendly underscore options (`--wait_s`)
and their dashed spellings (`--wait-s`). Pass only one spelling of each
option. A local wait-command error does not end the game; correct it and
continue the same play loop until the game is terminal.

`--poll_s` is accepted and forwarded, but it does not reach the server: the
long-poll ticks on the supervisor's own schedule and only `wait_s`, `until`
and `after_state_token` are sent. It survives solely for the pre-`/wait`
fallback path. Do not tune it.

### `just wait` exit codes

The wake reason is on the exit status, which is the one channel every job
supervisor, `&&` chain, cron loop and CI runner can already read without
parsing prose:

| exit | meaning | what to do |
|---|---|---|
| `0` | your phase is active | act now |
| `75` | woke, still another seat's phase (`EX_TEMPFAIL`) | call `wait` again |
| `66` | the game is terminal (`EX_NOINPUT`) | stop looping, read `just result` |

Text output is unchanged apart from naming the seat that holds the phase, and
`--json` is byte-identical. Only the bare `just wait` command carries these
codes. Every composite that embeds a wait — `turn --end --await`,
`do "…" --end --await` — keeps its own exit contract for the work it applied:
an applied phase end never exits non-zero because of how the wait after it
turned out.

### `just wait --for-turn [--max SECONDS]`

Blocks until the phase is genuinely this seat's, bounded by the deadline of
whichever seat currently holds it (plus a short grace) rather than by a fixed
`--wait_s`. Because an agent phase is at most `action_timeout_s`, and the
*remaining* budget is by construction less than that, one `--for-turn` call
covers one opponent turn — which is what makes "one call per turn" true in
multiplayer instead of aspirational. Internally it long-polls in short ticks,
so it stays resumable, refreshes `state/phase.json` as it goes, and prints one
`… waiting on seat N NAME · held … · … left` line per tick rather than going
silent for ten minutes. `--max SECONDS` caps the whole wait; `wait_s` is
bounded to `[0, 615]` on both sides of the wire.

### `just monitor`

The notification component. Two harnesses in the match this came from each
hand-rolled one and each got it wrong — one burned ten calls a turn looping
`just wait`, the other wired it to a background monitor that escalated on
non-zero exit, which `just wait` never produced. It is a strictly **read-only
observer**: it never ends a phase, never submits a batch, and never touches
cached state.

```sh
just monitor            # persistent: announce every activation, for the game
just monitor --once     # block until your phase opens, announce, exit
just monitor --stop     # release the persistent monitor
just monitor --status   # is one running, since when, watching what
```

**`--once`** is the binding for a harness that re-invokes when a background
command exits: started in the background it is a wake-up call with no tool
timeout over it and no polling loop. It takes no lock, so any number may run
alongside a persistent monitor, and it always answers — even on a phase
another monitor already announced, because a wake-up call that stayed silent
would hang forever.

**Persistent** mode is one long-lived process for a cron agent, a job monitor
or a human watching a pane. It is a singleton by `flock`, so starting a second
is a no-op that reports the first:

```
monitor already running (pid 41207, since 16:21:04, watching t12/p0 · seat 1 holds it)
```

That also means crash recovery is free — the kernel releases the lock when the
holder dies, so a `kill -9`, a crash or a shutdown leaves nothing stale and the
next `just monitor` simply acquires. Nothing reaps, and there is no PID file.

#### Four channels, on every activation

1. **One line on stdout**, in the shape harnesses already parse:
   ```
   T12 | woke phase_active | running | phase awaiting_agent t12/p1 active 600s left | next: just turn
   ```
   This line is deliberately *not* the enriched PvP rendering `just wait` uses;
   a notification channel is the wrong place to break a parser.
2. **`state/phase.json`**, atomically rewritten (below). The monitor writes
   this file and nothing else.
3. **`--exec 'CMD'`**, a shell hook with `FREECIV_GAME_ID`, `FREECIV_TURN`,
   `FREECIV_PHASE`, `FREECIV_YOUR_TURN`, `FREECIV_DEADLINE_S` and
   `FREECIV_HOLDER_LABEL` in its environment. A hook that fails is reported on
   stderr and never stops the monitor.
4. **`state/monitor.log`**, append-only. A backgrounded monitor's stdout is
   lost to log rotation or to a compacted context; this is what answers "when
   did my turns actually open" after the fact. Every `--exec` string and every
   invocation is recorded in it.

#### The missed-turn alarm

This is what the monitor is for. Transport errors are absorbed with capped
backoff (1 s → 30 s) and never raised — a laptop sleep that kills the long-poll
is the monitor's problem, not the agent's. On reconnect it compares health's
last phase end against what it actually announced, and says so when a turn
opened and died unseen:

```
T5 | MISSED | your phase t5/p1 opened and was ended by timeout after 600s — you issued no orders
```

Two in a row means the notification path itself is broken, and the wording
escalates:

```
T7 | MISSED ×2 | your phase t7/p1 opened and was ended by timeout after 600s — you issued no orders | you have not issued an order since t3. Your monitor is not reaching you — check it now; the game is advancing without you.
```

A phase this seat ended itself is never a miss, however it was noticed —
`--await` opening a turn the monitor never announced is normal composition. A
turn that *was* announced and then timed out is not a miss either: the alarm
is "nothing reached you", not "you played badly".

#### Exit codes and the rest

`--exit-code N` (default `0`) sets the status used for an announcement, so a
harness that only escalates on a particular status can say which. `66` means
the game ended; `75` means `--max-s` elapsed with the phase still not yours.
A terminal game and a workspace rebound by `just use` both stop the monitor,
so one never outlives the game it watches. Per-tick progress goes to stderr,
leaving stdout as pure signal.

#### What `--exec` may not do

The workspace contract is that the assigned model chooses every action itself.
A hook pointed at this workspace's own order-issuing command is an autoplay
bot in a single flag, reachable by accident, so hooks invoking any of its
mutating verbs (`do`, `batch`, `turn`, `start`, `retry`) are refused. The
refusal names the verb, and the check is imperfect
by design — a wrapper script defeats it — but it turns a silent contract
violation into a deliberate bypass, and every hook invocation is recorded in
`state/monitor.log`. **A hook may notify your harness. It may never play.**

### `state/phase.json`

A sanctioned machine-readable projection, written next to `state/header.txt`
by every command that reads health *and* by every internal tick of a blocking
wait, so it stays fresh while the client is blocked:

```json
{"schema_version": 1, "updated_at": 1786058960.4, "turn": 5, "phase": 0,
 "active": false, "state": "awaiting_agent", "game_state": "running",
 "held_s": 587.0, "deadline_s_left": 13.0,
 "holder": {"place": 1, "seat_id": "place-1", "player_name": "AgentPlace1",
            "controller_label": "pi-gpt-5.6-sol"}}
```

`holder` is `null` when this seat holds the phase or no seat does. The write
is atomic, so a watcher polling the file never reads a half-written object.

## Multiplayer: alternating phases

When another agent seat shares the game, seats alternate: exactly one phase is
open at a time, and while it is not yours nothing you can do shortens it. The
whole steady-state loop is one command:

```sh
just do "u1 VERB ARGS; c1 VERB ARGS" --end --await --brief
```

`--await` blocks to the holder's deadline rather than to a fixed 120 s, and
when it comes back with the phase still theirs it says so and prints no
briefing — a briefing for a phase you do not hold, tailed `next: just turn`,
only invites an action that will be refused. A harness that would rather split
the block out uses the monitorable primitive instead:

```sh
until just monitor; do :; done      # or: just wait --for-turn; echo $?
```

`just health` leads with `NOT YOUR TURN · seat 1 NAME (LABEL) holds t5/p0 ·
held 9m47s of 10m0s · 13s left` while you are blocked, and on your own turn
reports how the previous phase ended — including
`(timeout — they issued no orders)` when the seat before you submitted
nothing at all, which is not the same event as thinking for ten minutes.

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
