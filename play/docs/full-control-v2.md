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

## Core loop

### Lobby bootstrap

When health reports `game_state: lobby`, do not call `wait`: there is no active
game phase yet. Read `overview`, `pregame_nations`, `pregame_styles`, and
`pregame_teams`, and `votes`, then enumerate global legal actions. You may execute
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

The exact configuration arguments are `nation_id`, `leader_name`, boolean
`is_male`, and `style_id`, as advertised by that descriptor's schema. Team
selection requires exactly `team_id` from the same-revision `pregame_teams`
catalog and rejects the already selected row.

The `votes` section contains only active vote records visible to this normal
player. Each row has a revision-bound opaque `vote_id`, description, yes/no/
abstain tallies, voter count, required percentage, team-only flag, current own
vote, and `can_vote`. Execute only an enumerated `player.cast_vote`, passing
exactly that `vote_id` and `vote: "yes"`, `"no"`, or `"abstain"`.

### Running game

1. Keep the exact `SESSION_FILE` printed by join.
2. Run `just turn --session SESSION_FILE`. It returns a bounded briefing with
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
   `city_detail`, `city_citizens`, `city_worker_tasks`, `city_build_choices`,
   `city_worklist`, `city_improvements`, and `city_governor` with `--actor_id`.
   `city_worker_tasks` exposes standing worker requests; enumerate
   `city.manage_worker_task` to request, change, or remove one. Use
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
4. Enumerate current global actions with `just legal`. Use
   `just legal --kind ACTION_KIND --all` to auto-drain and compact one exact
   action kind such as `research.set_target`, `phase.end`, or
   `economy.set_rates`; `--kind` and `--all` are required together so later
   pages cannot be hidden. The original descriptors remain cached for `batch`.
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
   through 64 current opaque tile IDs. A player-plus-tile infrastructure action
   takes exactly one `extra_id` advertised by that descriptor's target choices.
   `player.cast_vote` takes the current descriptor's exact opaque `vote_id`
   plus `vote: "yes"|"no"|"abstain"`. `player.send_chat` takes exactly
   `channel: "global"|"allied"` and a
   1–512-byte UTF-8 `message`. Commands, private-target colons, control text,
   and caller-supplied allied prefixes are rejected.
5. Choose one enumerated opaque action at its exact `state_revision` and run
   `just batch --session SESSION_FILE --action_id ACTION_ID --arguments JSON`.
6. Treat the returned durable receipt as authoritative. On an uncertain
   transport outcome, run `just receipt` first. Use `just retry` only for that
   locally persisted batch; it is receipt-first and never reconstructs or
   mutates the request.
7. Run `just wait --session SESSION_FILE`, then repeat from health/state. This
   wait step is for a started game, never for the lobby bootstrap above. Begin
   the next decision with `just turn`. Keep the same harness conversation
   active and repeat until health reports a terminal game state. Completing
   one phase is not completing the game: do not emit a final answer after
   `phase.end`. If the wait command fails locally, correct the command and
   continue; a local command error is not a terminal Freeciv result.

End your active player phase only with the currently enumerated `phase.end`
action. Do not end it while owned cities or units still need deliberate work.
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
`rate_limited` never invalidates an existing unexpired cursor; retry later.

Scoped legal pages share a stable opaque `catalog_id`. Prefix pages report
`catalog_complete: false`, and the player client stages their descriptors in
its private state instead of making them executable. It promotes the entire
catalog atomically only on the validated final page. Restart the scoped query
after any newer revision, catalog mismatch, expiry, or contract failure.

Server-discovered target actions omit any result requiring a gold quote or
complex subtarget, except the exact Classic targeted-technology-theft escape
and targeted-building-sabotage contracts. The five shipped Classic random city/production-sabotage and
technology-theft variants, `Targeted Steal Tech Escape Expected`, `Conquer
Extras` variants 1/2, and `Enter Hut`/`Frighten Hut` variants 1/2 are closed
native contracts. Classic `Bribe Unit`, `Incite City`, and `Incite City Escape`
also appear with a request-correlated `gold_cost` maximum, a fresh pre-dispatch
   quote, and an atomic server ceiling guard. Classic Bribe Stack uses the same
   fresh maximum-price guard and an exact request-bound completion receipt.
Targeted theft appears only with embassy/team-authorized
victim research visibility. Each selectable regular technology is a separate
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
from unknown to known. Targeted future technology and the plain/non-Classic
targeted variant remain absent. Visibility loss or a changed visible
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
normal self echo arrived on the requested global/allied channel with the exact
message suffix and no same-request chat error. It does not mean a console
command ran; this action has no console or private-message path.

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

`default` allows 180 seconds for the whole active player phase, `blitz` allows
60 seconds, and `infinite` has no model deadline. A separate generous native
progress watchdog detects a stuck control plane; it does not choose actions or
shorten the model's configured time.

`health.last_phase_end` is either `null` or this seat's latest durable,
public-safe phase-end attribution. Compare its `sequence`, `turn`, and `phase`
when diagnosing a wait. Never infer that a missing current `phase` means your
last command was received; use the receipt and `last_phase_end` fields.

Stop on `completed`, `invalid`, `failed`, or `cancelled`. Never expose the
session, invitation, bearer, or `.v2-state` contents.
