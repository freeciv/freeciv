# Custom harness contract for full-control-v2

This is the versioned HTTP contract for Codex, Claude Code, Pi, or any custom
harness that does not use `play/client.py`. The checked-in OpenAPI 3.1 document
is [`full-control-v2.openapi.json`](full-control-v2.openapi.json); a running
supervisor serves the same document from `GET /v2/openapi.json` without
credentials.

## Bootstrap and authentication

Join through the documented game-scoped invitation flow with
`supported_control_protocols: ["full-control-v2"]`. The join response returns
an agent ID, an agent bearer, authoritative same-origin endpoint URLs, and the
evaluation's `objective`, `max_turns`, and initial `turns_remaining`. Use
that exact bearer only on `/v2/games/{game_id}/me/*`. Never put a token in a
URL, log, prompt, batch body, telemetry value, or OpenAPI example. Owner,
admin, invitation, spectator, replay, score, frame, video, save, and log access
are outside the player contract.

Every private route authenticates before parsing query or command details. A
bearer can read or act only as its exact joined agent. There is no spectator
form of `health`, `state`, `legal-actions`, `wait`, `batches`, or `receipts`.
Every health response repeats the objective and maximum turn unchanged and
derives `turns_remaining` from the supervisor's current turn. Before a native
turn exists it is `null`; wait exposes the same values inside `health`.

## Bounded loop

Before the normal machine loop, branch on health. While `game_state` is
`lobby`, do not wait for a phase. Fetch `overview`, `pregame_nations`,
`pregame_styles`, `pregame_teams`, and `votes`; optionally post the enumerated
`pregame.configure` or `pregame.set_team`; refresh after each;
then post the enumerated `pregame.set_ready` with the exact desired
`{"ready": true}` argument. The server does not advertise readiness until
every external seat and its exact sidecar generation are present. Opening that
barrier changes the state token and invalidates older action capabilities. A
seat may withdraw readiness with the newly enumerated `{"ready": false}`
capability until the game begins. The final native ready packet starts the game
without a console start command.

After the game starts, the normal machine loop is:

1. `GET .../wait?until=phase&wait_s=120`.
2. Branch on `wake_reason`. Continue only on `phase_active`; stop on
   `game_terminal`; call wait again after `timeout`.
3. `GET .../health`, then fetch bounded `state` pages.
4. Enumerate `legal-actions`. Continue every cursor. For actor/relation scopes,
   do not execute a prefix page: wait for `catalog_complete: true`.
5. Persist the exact canonical command batch and its `batch_id` locally.
6. `POST .../batches` exactly once.
7. Resolve `accepted` through `GET .../receipts/{batch_id}`. Treat `applied`,
   `rejected`, and `ambiguous` as terminal. Never replay `ambiguous`.

State and action IDs are opaque. Do not derive native IDs, packet enums, or
new action IDs. A cursor is exclusive and must be sent without any other query
option. The OpenAPI operations carry `x-freeciv-query-forms`; any combination
not listed there is invalid. Page size is 1 through 16, and every public page
is bounded to 65,536 canonical JSON bytes.

## Exact target action catalogs

Query `legal-actions` with the self player, an owned unit, or an owned city
`actor_id` and a known tile `target_id`; an optional `limit` from 1 through 16
is allowed. The response is
a frozen scoped catalog, not merely a pathfinding answer. A unit target catalog
can include goto and semantic `unit.perform_action` descriptors discovered from
Freeciv's read-only action-probability reply. Native action numbers, rule names,
target numbers, subtargets, and packet fields never cross the HTTP boundary.
Exhaust the cursor through `catalog_complete: true` before submitting any
descriptor from that catalog.

The self-player form discovers infrastructure placement, the city form
discovers rally routing, and the unit form discovers goto plus safe ruleset
actions. The player-plus-diplomatic-relation form remains a distinct exact
two-parameter query and does not accept a limit.

Classic unit bribery, stack bribery, and both city-incitement variants expose an authenticated
`subject.gold_cost` maximum. The harness still submits `{}`: the native client
freshly re-quotes and the server atomically refuses any higher price. Complex
subtargets remain absent except for the exact Classic targeted-technology-theft
escape and targeted-building-sabotage contracts.
Closed Classic contracts admit the five shipped random city/production-sabotage
and technology-theft variants, `Targeted Steal Tech Escape Expected`, `Conquer
Extras` variants 1/2, and `Enter Hut`/`Frighten Hut` variants 1/2. Random
espionage, targeted theft, and hut actions retain unresolved probability; hut
actions require exactly their native enter/frighten subresult. Targeted theft
is visible only with embassy/team-authorized opponent research. Exhaust the
catalog and choose one action by its readable `technology_choice.name` and
opaque `technology_choice.id`; never invent or send a native technology ID.
The selected choice is frozen to the victim research state and is revalidated
around the fresh native preflight. For exact Classic building sabotage,
exhaust the catalog and choose one action by its readable
`building_choice.name` and opaque `building_choice.id`; never invent or send a
native improvement ID. The selected improvement, action variant, actor, city,
discovery request, native revision, and exact eligible-list digest are frozen
and revalidated immediately before dispatch; command arguments remain `{}`.
Targeted future technology, the plain/non-Classic targeted-theft
variant, the non-Classic non-escape production-sabotage action, and generic ruleset
actions stay absent and fail closed. An `ambiguous` receipt is terminal: never infer rejection from
unchanged state and never submit the action again automatically.

## State scopes

Unscoped sections are `overview`, `pregame_nations`, `pregame_styles`,
`pregame_teams`, `votes`, `research`, `governments`, `multipliers`, `spaceship`,
`diplomacy`, `known_tiles`, `map_tiles`, `infrastructure`, `cities`,
`city_sites`, `units`, `tombstones`, and `chat`. The three pregame catalogs are available only while preparing;
nation/style IDs are inputs to `pregame.configure`, while a noncurrent team ID
is the sole input to `pregame.set_team`. `cities` is a
compact summary catalog. `diplomacy_clauses` is relation-scoped and requires
the opaque `relation_id` from the corresponding `diplomacy` row.

Detailed city collections are independently pageable through `city_detail`,
`city_citizens`, `city_worker_tasks`, `city_build_choices`, `city_worklist`, and
`city_improvements`; each initial query requires the opaque city `actor_id`.
Spatial inspection uses `section=tile_window`, an opaque known-tile
`center_id`, and a radius from 0 through 8. The server applies map topology,
wrapping, and fog-of-war rules. Initial scoped queries may also include a
page `limit`; continuation sends only the returned cursor.

`unit.set_route` uses exact `{mode, waypoints}` arguments, where `mode` is
`goto` or `patrol` and `waypoints` contains one through 64 opaque IDs from the
current known-tile state. Infrastructure placement is discovered with the
self player actor plus a seen target tile and accepts one opaque `extra_id`
from that exact descriptor's advertised choices.

## Batch recovery

Receipt responses are durable and authoritative. A validated failure before
receipt reservation is a normal structured error whose `error.details`
contains:

```json
{
  "batch_id": "the validated submitted batch ID",
  "acceptance": "not_accepted",
  "safe_next": "refresh"
}
```

`safe_next` is closed:

- `refresh`: fetch current state and legal actions, then choose again.
- `retry_exact`: the server proved this attempt was not accepted; resend only
  the byte-identical persisted body with the same batch ID.
- `receipt_first`: query the receipt before deciding anything else.

Malformed JSON, invalid schemas, the wrong game, and the wrong agent do not
echo an untrusted batch ID. Once reservation may have begun, the server never
relabels the outcome `not_accepted`: a durable receipt is authoritative when
available, and any persistence uncertainty is receipt-first. If the connection
fails, the response is malformed, or the recovery contract is missing, query
the receipt first. Never infer rejection from an absent or timed-out HTTP
response.

The bundled CLI emits exactly one compact JSON disposition after it has
persisted a batch. `disposition` is one of `receipt_terminal`, `receipt_poll`,
`receipt_first`, `retry_exact`, or `refresh`, and every disposition includes
the public `batch_id`.

## Long polling

`GET .../wait` defaults to `until=phase`. It wakes only when this caller has an
actionable `awaiting_agent` phase, the game becomes terminal, or `wait_s`
expires. An opponent action or newer inactive revision cannot wake it. Every
response has `wake_reason`: `phase_active`, `game_terminal`, or `timeout`.

For tooling that deliberately follows any private revision, use
`until=revision&after_state_token=OPAQUE_TOKEN`. Only that explicit mode may
return `revision_changed`. `wait_s` is a canonical decimal in `[0,300]`; the
default is 120 seconds. Long poll is an authenticated convenience, not proof
that a prior batch arrived—use its receipt for that.

## Errors and compatibility

All v2 failures after route selection use the version-2 structured error
envelope with a closed error-code enum. Follow `retryable` only together with
the stronger batch `safe_next` contract. `429` and retryable `503` before
reservation yield `retry_exact`; stale, expired, or invalid action arguments
yield `refresh`; conflicts and persistence uncertainty yield `receipt_first`.

The checked-in OpenAPI file is the machine contract. The prose in
[`full-control-v2.md`](full-control-v2.md) describes gameplay semantics and
[`commands.md`](commands.md) describes the bundled CLI.
