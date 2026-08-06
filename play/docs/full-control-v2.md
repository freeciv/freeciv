# Full-control-v2 gameplay

This page applies only when join reports `full-control-v2`. The game exposes a
fog-limited private client state and an enumerated legal-action catalog. Never
use spectator, replay, score, save, log, or parent-workspace data to play.

The exact HTTP envelopes and closed enums are documented in
[`full-control-v2.openapi.json`](full-control-v2.openapi.json); harness authors
should also read [`custom-harness-v2.md`](custom-harness-v2.md).

The v2 join result records the immutable `objective` and `max_turns`, plus
`turns_remaining`. The same three top-level fields appear in every private
health response and therefore under `wait.health`. `turns_remaining` is `null`
before Freeciv publishes an authoritative current turn; afterward it is
`max(0, max_turns - phase.turn)`. The player client saves these fields in the
private session and prints the objective and turn budget at join.

## The agent surface

Nothing below changes the wire. Every action is still a server-issued opaque
capability, bound to one state revision, chosen from a catalog the server
materialized; the client never sends an alias, a verb, or a coordinate it
invented. What changed is what you read and type.

- **Compact text by default, `--json` for the wire.** `join`, `health`,
  `turn`, `state`, `legal`, `batch`, `receipt`, `retry`, `start`, `do`,
  `show`, and `wait` print aligned text; add `--json` to any of them for the
  full-fidelity JSON payload, which for every command that predates the text
  renderer is byte-identical to what it used to print.
  The text is a projection of the same validated
  page, never a different capability. The envelope — revision, turn, scope,
  pagination — prints once in a header line, and only *default* values are
  omitted: `probability` only at exactly 100/100, `legality` only at `legal`,
  `consuming` only at false, `variant` only at null. Every non-default value
  renders with a leading `!`, so a gamble never reads as a certainty.
- **Aliases.** Anywhere an ID is accepted you may type the short alias the
  text output prints: `a1..aN` for one enumerated action, `u1`/`c1`/`p1`/`r1`
  for a unit, city, player, or diplomatic relation, and `T(x,y)` for a tile
  this seat has already seen. The client expands them against its private
  cache before it builds the request. Entity and tile aliases are stable for
  the whole game. An action alias is scoped to the revision it was enumerated
  at, and its number dies with that revision. Its *meaning* — actor, kind,
  operation, normalized target, argument-schema shape — is recorded alongside
  it, so `just do` and `just batch` re-enumerate and re-bind the alias to the
  same action when the action is unchanged, printing one `a3 rebound at rev14`
  line. The wire only ever carries the fresh revision-bound `action_id`. A
  vanished or now-ambiguous action fails closed exactly like the expired
  opaque ID it stands for, naming the `just legal` command that re-enumerates
  it; `--no-refresh` keeps that refusal without the extra request.
- **Fast paths.** `just start`, `just turn`, `just do "…"`, and
  `just turn --end --await` cover an ordinary turn, and `--end --await
  --brief` composes the whole of one onto `just do`, so a steady-state turn is
  a single command: orders, phase end, the block, and the next briefing. Each
  is sugar over the same enumerated capabilities and resolves entirely against
  the local cache; none is a separate channel and none is a ceiling. Anything
  they do not cover stays reachable with `just legal` plus `just batch`,
  unrestrained.
- **A local state mirror.** The client rewrites `state/` and `cache/` files
  beside the session file from every response it ingests. `just show`,
  `grep`, or any file read gets units, cities, the terrain grid, per-actor
  option catalogs, and what changed since your last read at zero network cost
  and zero context cost until you look. The files are projections of pages
  this seat already received, so reading them can never see past fog; the
  private `.v2-state` cache is not part of them and is never exposed.
- **Errors carry their own remedy.** A refusal names the exact command that
  fixes it — the query to restart after `cursor_expired`, the enumeration
  that re-issues an expired alias, the narrower scope after
  `scope_too_large`. Read the error rather than re-reading this page.
- **One workspace, one seat.** Join binds this workspace to the seat it
  joined, so no command takes a session argument. The client uses an explicit
  `--session` first, then `PLAY_SESSION`, then that binding, then a sole
  unbound session. `just use` prints the bound seat and `just use GAME_ID`
  rebinds it. Never print or paste a session file's contents.

Join prints a protocol card summarizing all of this, and `state/header.txt`
carries the same card, so the contract is re-readable from a file.

## Core loop

### Lobby bootstrap

When health reports `game_state: lobby`, do not call `wait`: there is no active
game phase yet. Read `overview`, `pregame_nations`, `pregame_styles`,
`pregame_teams`, `votes`, `chat`, and `chat_recipients`, then enumerate global
legal actions. You may use the enumerated `player.send_chat` immediately for
normal lobby chat, including a private message to a same-revision opaque
recipient whose `chat_recipients` row has `can_message: true`. You may also execute
`pregame.configure` with opaque nation/style IDs and a leader name/sex, or
`pregame.set_team` with one noncurrent opaque `team_id`; refresh state, all
needed catalogs, and legal actions after either because every capability is
revision-bound. Team rows identify the selected team and list members by opaque
player ID, leader name, and `self`; native team slots are never public. Finally execute
the currently enumerated `pregame.set_ready` with `{"ready": true}`. That
descriptor is absent until every expected external seat has joined with a
healthy sidecar; opening the all-seats barrier changes the state token, so
refresh after the final join. Setting `ready` back to `false` remains available
until the game starts. The last ready action uses Freeciv's native ready packet
to start the game; the supervisor never substitutes a console `start` command.
Continue the normal loop below once health leaves `lobby`.

`just start` is that whole sequence in one command, and needs no arguments:
an omitted `--nation` draws one from the pregame catalog, `--leader` comes
from the seat's controller label (or the lobby's own leader name),
`--male`/`--female` from the lobby's own default, and `--style` from the
nation's `default_style_id`. It prints the one line it resolved, executes the
enumerated `pregame.configure`, re-enumerates because configuring bumps the
revision, then executes the freshly enumerated `pregame.set_ready`. It uses the same capabilities and the
same one-command batches described above; when readiness is not enumerable it
stops and names the `just legal` command that would show it. Team selection,
lobby chat, and votes stay on the explicit path.

The exact configuration arguments are `nation_id`, `leader_name`, boolean
`is_male`, and `style_id`, as advertised by that descriptor's schema. Team
selection requires exactly `team_id` from the same-revision `pregame_teams`
catalog and rejects the already selected row.

The `votes` section contains active records visible to this normal player plus
the latest 64 structured outcomes in the current seat epoch. Each row has a
stable `vote_ref`, revision-bound `vote_id`, caller, description, tallies,
threshold, team-only flag, request-confirmed own vote, `can_vote`, and
`status: "active"|"passed"|"failed"|"removed"`. Outcome rows also carry their
turn and phase and are never actionable. Execute only an enumerated
`player.cast_vote`; its live argument enum omits the already-confirmed ballot,
because an identical re-vote has no reliable server update to acknowledge.

### Running game

The short form of one turn is one command:
`just do "…" --end --await --brief`, which orders every actor the last
briefing named, ends the phase, blocks, and prints the next briefing.
`just turn`, `just do "…"` and `just turn --end --await` are the same steps
apart. `just show` answers a follow-up question without a request. The numbered steps below are the same
loop written out in full; use them whenever the short form is not enough, and
for every capability the fast paths do not name.

1. Join bound this workspace to your seat, so no step below names one;
   `just use` reports which seat that is.
2. Run `just turn`. It returns a bounded briefing with
   health/evaluation context plus overview, owned cities, owned units, and
   research from one exact revision. The client retries the whole sequential
   read once if the revision advances. Follow any returned continuation command
   when a section has more than 16 items. Economy and current government are in
   `overview.player`; do not query nonexistent `economy` or `government`
   sections.
3. Read additional useful private sections with `just state`. The
   `city_sites` section is the fog-safe destination catalog for unit actions:
   it exposes only opaque identity, owner, name, tile, coordinates, cached
   size, and `own`, `visible`, or `known` visibility.
   `cities` is deliberately compact. For each owned city ID, request
   `city_detail`, `city_citizens`, `city_worker_tasks`, `city_trade_routes`,
   `city_build_choices`, `city_worklist`, `city_improvements`, and
   `city_governor` with `--actor_id`.
   If an owned unit's `route.path_available` is true, reconstruct its exact
   remaining queued movement with
   `just state --section unit_route --actor_id UNIT_ID`.
   The ordered rows use opaque tiles and semantic `move`, `action_move`, or
   `wait` kinds; no native tile IDs or directions are exposed.
   Build choices include city-context cost, post-change shield stock, turn
   estimates, six-output upkeep, and structured unit/building stats, so choose
   production from that page instead of guessing Classic ruleset values.
   `city_worker_tasks` exposes standing worker requests; enumerate
   `city.manage_worker_task` to request, change, or remove one. Use
   `city_trade_routes` to inspect each established route's base/effective
   value, direction, and opaque goods before choosing `establish_trade`.
   A partner is linked by opaque city ID and name only while it is owned or
   currently visible; otherwise `partner.available` is false and no native
   partner identity is exposed. Use
   `map_tiles` for the complete fog-safe board and `tile_window` with a known
   `--center_id` and `--radius 0..8` for a bounded topology-aware local map;
   visible/remembered rows include cached resource, label, generic yields, and
   opaque extras with semantic causes, while unknown rows stay fully redacted.
   `city_detail` includes citizen totals, food/growth, pollution, and output
   accounting; `city_citizens` includes six signed yields per tile/specialist
   row and marks whether each specialist count contributes to population.
   Read `infrastructure` for the bounded extra catalog; overview reports the
   player's current infrastructure availability and point balance.
   For each diplomatic meeting, read `diplomacy_clauses` with the opaque
   `--relation_id` from its `diplomacy` row; clauses are not one global list.
   Read `chat` for the latest 64 normal-client chat/event packets. Rows contain
   plain visible text plus sender, channel, event, self, and truncation fields.
   Read `chat_recipients` for the visible normal-player roster used by private
   chat. Each row exposes only a same-revision opaque player ID, display name,
   `self`, `connected`, and `can_message`; it exposes no native player or
   connection number. `can_message` is true only when that player is connected
   and its current name is safe and unambiguous for Freeciv's `PlayerName:`
   syntax.
   Before spending a request, check whether the answer is already in the
   local mirror: `just show units`, `just show cities`, `just show map`,
   `just show u1`, or `just show --grep PATTERN` reads what earlier responses
   already wrote, with no request and no server load. It is as fresh as your
   last read and never fresher, so re-read the section itself whenever
   freshness matters.
4. Enumerate current global actions with `just legal`. Use
   `just legal --kind ACTION_KIND --all` to auto-drain and compact one exact
   action kind such as `research.set_target`, `phase.end`, or
   `economy.set_rates`; `--kind` and `--all` are required together so later
   pages cannot be hidden. The original descriptors remain cached for `batch`.
   Each compact row keeps the action label plus the public semantic `subject`
   discriminators, including `subject.operation`; use those fields to tell
   aggregated `unit.order`, `unit.perform_action`, citizen-assignment, and
   worker-task choices apart.
   If the result says `has_more: true`, repeat the same query with
   `--offset NEXT_OFFSET` (and optionally `--limit 1..64`) until `has_more` is
   false. This also resumes safely when the compact byte bound is reached;
   never assume a merely `truncated` result contains every matching action. If
   a later window reports a different `state_revision`, restart from offset 0;
   do not combine descriptors from different revisions. `oversized_single`
   means one descriptor needed the bounded single-item fallback, not that the
   catalog is unbounded; continue from `next_offset` normally.
   Use
   `--actor_id ACTOR_ID` for every owned city or unit that needs decisions;
   continue every returned cursor and do not execute a scoped descriptor until
   its final page reports `catalog_complete: true`. Use actor plus
   `--target_id TILE_ID` for an exact bound-target lookup when needed; this
   form also accepts `--limit 1..16`, and its cursor must be exhausted too.
   A fully redacted unknown `map_tiles` row may be used for far `unit.goto`;
   remembered paradrop is available only when the live catalog advertises it.
   A unit target lookup can include goto (including a legal final action move),
   `goto_and_perform`, native road/irrigation `connect_route`, plus bounded,
   argument-free semantic spy, sabotage, bombardment, nuclear, conquest,
   healing, and related immediate actions. Native variants, route orders,
   action/subtarget IDs, and packet fields stay opaque.
   For diplomacy, take the self actor
   from `overview.player.id`, a target from `diplomacy[].relation_id`, run the
   same actor-plus-target query, and exhaust every returned cursor.
   Relation target queries remain exact two-parameter queries and do not accept
   `--limit`.
   `unit.set_route` takes `{"mode":"goto|patrol","waypoints":[...]}` with one
   through 64 current opaque tile IDs. `unit.attack_route` takes exactly
   `{"destination_id":"TILE_ID"}` and queues an action-capable move at every
   step; Freeciv may pause the queue for a normal action decision. A
   player-plus-tile infrastructure action
   takes exactly one `extra_id` advertised by that descriptor's target choices.
   `player.cast_vote` takes the current descriptor's exact opaque `vote_id`
   plus one `vote` value from that descriptor's current enum.
   `player.send_chat` takes exactly
   `channel: "global"|"allied"|"private"` and a strict UTF-8 `message` of
   1–512 encoded bytes. Global and allied messages omit `recipient_id`; private
   messages require the same-revision opaque `recipient_id` from a
   `chat_recipients` row with `can_message: true`; native code revalidates that
   recipient, connection, and name immediately before sending.
   Leading/trailing ASCII U+0020 and Unicode `Cc`/`Cf` code points are rejected.
   Colons and command-looking text are allowed as message text: the native
   client prepends one protective ASCII space for global, `.` for allied, or
   the exact `PlayerName:` for private. The server parses the protective space
   as global chat and trims it before display, so callers cannot reach the
   server console, select a different player by text, or use direct connection
   messaging.
5. Choose one enumerated opaque action at its exact `state_revision` and run
   `just batch --action_id ACTION_ID --arguments JSON`. `just do "ORDER;
   ORDER"` issues one through eight of these in a row, resolving each order
   against the cached catalog before it sends anything and re-enumerating
   between orders because each landed order bumps the revision. It is the
   same wire traffic — one command per batch — with one receipt line each.
6. Treat the returned durable receipt as authoritative. On an uncertain
   transport outcome, run `just receipt` first. Use `just retry` only for that
   locally persisted batch; it is receipt-first and never reconstructs or
   mutates the request.
7. Run `just wait`, then repeat from health/state. This
   wait step is for a started game, never for the lobby bootstrap above. Begin
   the next decision with `just turn`. Keep the same harness conversation
   active and repeat until health reports a terminal game state. Completing
   one phase is not completing the game: do not emit a final answer after
   `phase.end`. If the wait command fails locally, correct the command and
   continue; a local command error is not a terminal Freeciv result.

End your active player phase only with the currently enumerated `phase.end`
action. `just turn --end --await` is that action plus the wait plus the next
phase's header line (`--brief` prints the next briefing in full instead);
`just do "…" --end` composes the same action onto a batch, and never runs it
when the batch did not finish. It enumerates `phase.end` itself when the
capability is not already cached, and `--await` without `--end` is refused. Do not end the
phase while owned cities or units still need deliberate work.
If the supervisor ends your phase at its configured deadline, the next health
response reports a caller-scoped `last_phase_end` with `source: "timeout"`.
That is authoritative confirmation that the timeout end was received; it
contains no opponent or spectator state.

## Revisions, pages, and receipts

Action IDs are opaque, seat-scoped, generation-scoped, and revision-scoped.
Never invent, edit, or reuse one after a newer state revision appears. A state
or legal cursor is also opaque and exclusive; continue it without other query
options. Cursors remain valid for at least five minutes. A successful repeat
returns the identical authenticated page, and each successful continuation
refreshes the lifetime of its next cursor. `cursor_expires_at` is that next
cursor's RFC 3339 expiry. On retryable `cursor_expired`, use the structured
`details.restart` query; a forged cursor is always `invalid_request`.
Every public page is also capped at 65,536 canonical JSON bytes. A page may
therefore contain fewer items than the requested limit while still returning a
cursor. `scope_too_large` means one item cannot fit. Cursor-capacity
`rate_limited` carries `details.retry_after_seconds` and an RFC 3339
`details.retry_after`; retry then. It never touches a page chain that still
owes a continuation. A chain that is fully **drained** — its terminal page
already served, nothing outstanding — may be retired under capacity pressure,
and a later continuation of it replays as retryable `cursor_expired` with a
`details.restart` query. A scoped catalog's cursor records are also released
as soon as a newer revision lands, since they could only refuse with
`stale_revision` from that point on.

Scoped legal pages share a stable opaque `catalog_id`. Prefix pages report
`catalog_complete: false`, and the player client stages their descriptors in
its private state instead of making them executable. It promotes the entire
catalog atomically only on the validated final page. Restart the scoped query
after any newer revision, catalog mismatch, expiry, or contract failure.

Server-discovered target actions omit any result requiring an unmodeled cost,
subtarget, or subresult. Supported technology/building subtargets become
request-bound opaque named choices. Supported fixed subresults become ordered
public `effects`; they are metadata of the opaque action rather than caller
arguments. The five shipped Classic random city/production-sabotage and
technology-theft variants, both targeted technology-theft variants, targeted
building sabotage/strike, `Conquer Extras` variants 1/2, and the supported hut,
paradrop, teleport, and non-lethal action variants are closed native contracts.
Classic `Bribe Unit`, `Incite City`, and `Incite City Escape`
also appear with a request-correlated `gold_cost` maximum, a fresh pre-dispatch
   quote, and an atomic server ceiling guard. Classic Bribe Stack uses the same
   fresh maximum-price guard and an exact request-bound completion receipt.
Targeted theft appears only with embassy/team-authorized
victim research visibility. Each selectable regular or Future technology is a separate
action with a readable name and opaque choice ID; its native technology number
stays private and its slot is bound to the exact victim research state. Random
espionage, targeted theft, and hut probability remains unresolved. Immediately before
dispatch the native boundary rechecks the actor and target lifetimes and runs a
fresh read-only action-catalog preflight, revalidating the selected technology
both before and after it. Exact building sabotage similarly exposes one
readable `building_choice.name` and opaque choice ID per normal-GUI-authorized
improvement. The selection is bound to the actor, city, action variant,
discovery request, native revision, and exact catalog digest, then the detail
query is repeated immediately before exact-subtarget dispatch. The harness
still sends `{}`. A request-bound structured action receipt proves success; an
externally visible building must also transition from present to absent, while
hidden cached building state is never required. `applied` requires that exact technology to change
from unknown to known. The four Freeciv user-action slots are supported only
when they have no complex subtarget or subresult. Direct extra/extra-not-there
routing, specialist subtargets, and other unmodeled result/subresult families
remain absent. Visibility loss or a changed visible
stack alone never proves success; without a correlated positive postcondition,
the durable receipt is terminal `ambiguous` and must not be retried.

Classic `Investigate City` is also a target-scoped action. A successful applied
receipt includes `observation.type: city_investigation` with exactly the normal
client's request-bound city production, shield stock/surplus, installed
improvements, citizen-feeling stages, and specialists. Its freshness is only
`captured_at_receipt_revision`; treat it as a historical capture after any
newer state revision. Every other receipt has `observation: null`. Missing
positive investigation proof is terminal `ambiguous` and must not be replayed.

At most eight actor-plus-target catalogs are accepted in one native revision.
Accepted catalogs remain immutable until that revision changes; a ninth pair
fails atomically with `scope_too_large`. Native response delivery emits one
checked IPC frame per event-loop tick, which reduces queue pressure but does
not guarantee capacity. Any queue refusal terminally poisons that stream and
the incomplete scope is never committed.

Relation actions are semantic: they name the clause type, giver, receiver,
and opaque city/technology value without exposing native IDs. Gold proposal
descriptors require `{"gold": N}` within their advertised bound; every other
diplomacy action takes `{}`. Build the whole desired treaty before choosing
`diplomacy.acceptance`. It sets the desired acceptance state, so never retry
or replace an ambiguous acceptance as if it were rejected. Refresh state and
the complete relation scope after any stale revision or proved rejection.
An existing city clause remains removable after that city leaves your visible
city catalog; it appears as an opaque `available: false` city and does not
reveal the hidden native city ID or name. Technology proposal candidates use
the same knowledge rule as the normal GTK client: without a team embassy the
receiver's prerequisite eligibility is not locally knowable, so the server
performs the final legality check.

V2 batches contain exactly one command. The local player client stores the
canonical request body before POST. Receipt states mean:

- `accepted`: native processing began; poll the receipt until it becomes
  terminal.
- `applied`: the exact postcondition was verified.
- `rejected`: the server proved the action did not apply. Refresh state and
  legal actions before choosing again.
- `ambiguous`: acceptance or outcome cannot be proved. This is terminal. Never
  replay or replace that batch as though it were rejected.

For `player.send_chat`, `applied` specifically means the same native request's
normal self echo arrived on the requested channel with no same-request chat
error. Global/allied echoes have the exact message suffix. A private echo is
exactly `->{PlayerName} message`, binding the receipt to the selected
same-revision recipient; that recipient's normal client sees
`{SenderName} message`. This is sender-side proof of normal-player routing, not
a separate remote-delivery acknowledgement. No form invokes the server console
or permits direct connection messaging.

For consuming `join_city`, `establish_trade`, `marketplace`, and
`help_wonder` actions, a disappearing unit is not enough by itself. The
receipt is applied only when the bound city semantics are also proved; actor
disappearance with a source/destination mismatch is terminal ambiguous.

## Decision order

At each active phase, inspect research and government choices, economy rates,
every owned city, and every owned unit. Prefer actions that establish cities,
keep useful production queued, improve worked terrain, explore safely, and
advance research toward the game objective. Use only what the private state
actually reveals; absence of an action means it is not currently legal through
this control surface.

An owned unit scope can also enumerate `upgrade`, `rehome`, `join_city`,
`establish_trade`, `marketplace`, and `help_wonder` when Freeciv currently
permits them. Their city target is already bound into the opaque action; trade
and marketplace also bind the unit's own home city as source. Never substitute
a city ID or native action name in the arguments—these descriptors accept
exactly `{}`.

The native game server remains authoritative. The client performs normal
Freeciv requests and verifies exact postconditions; the harness never sends
raw packets or native IDs.

## Timing and stopping

`default` allows 600 seconds (10 minutes) for the whole active player phase
and `infinite` has no model deadline; `blitz` exists only on `strategic-v1`
and is never negotiated for a `full-control-v2` game. A separate generous native
progress watchdog detects a stuck control plane; it does not choose actions or
shorten the model's configured time.

`health.last_phase_end` is either `null` or this seat's latest durable,
public-safe phase-end attribution. Compare its `sequence`, `turn`, and `phase`
when diagnosing a wait. Never infer that a missing current `phase` means your
last command was received; use the receipt and `last_phase_end` fields.

Stop on `completed`, `invalid`, `failed`, or `cancelled`. Never expose the
session, invitation, bearer, or `.v2-state` contents.
