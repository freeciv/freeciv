# Full-control v2 protocol

`full-control-v2` is the compatibility-preserving path from the current
four-trait `strategic-v1` interface to play at the same control boundary as a
human Freeciv client. It is a separate negotiated protocol. It does not change
the observations, action schema, Lua bridge, turn barrier, or routes of an
existing `strategic-v1` game.

The player-facing machine contract is versioned in
[`play/docs/full-control-v2.openapi.json`](../play/docs/full-control-v2.openapi.json),
with a concise custom-harness loop in
[`play/docs/custom-harness-v2.md`](../play/docs/custom-harness-v2.md).

This document is both the target contract and a record of the currently landed
boundary. The supervisor owns a same-checkout `freeciv-agent` sidecar per
external seat, performs the private HELLO/TAKE/READY handshake, opens pregame
only after every exact generation is present, lets the final native
`PLAYER_READY` packet start Freeciv, publishes caller-scoped native
connection health, and fails closed without replacing a lost human seat with
Classic AI.

The initial playable vertical slice is now landed. Authenticated agents can
read native, fog-projected state and legal-action pages, submit exactly one
opaque action per batch, and retrieve durable receipts. The native action
catalog is deliberately bounded and now includes `pregame.configure`,
`pregame.set_ready`, `player.send_chat`, `phase.end`,
`unit.move`, `unit.attack`, `city.found`, `research.set_target`,
`research.set_goal`, `economy.set_rates`, `city.set_production`,
`city.buy_production`, `city.work_tile`, `city.unwork_tile`,
`city.set_specialist`, `unit.start_activity`, `unit.cancel_activity`,
`unit.sentry`, `unit.auto_work`, `unit.auto_explore`,
`unit.cancel_automation`, `unit.cancel_orders`, `unit.goto`,
`unit.goto_and_perform`, `unit.connect_route`, `unit.set_route`,
`unit.attack_route`,
`unit.fortify`, `unit.convert`,
`unit.disband`, `unit.homeless`, `unit.board`, `unit.deboard`, `unit.embark`,
`unit.disembark`, `unit.load`, `unit.unload`, `government.revolution`,
`government.change`, `player.set_multiplier`, `spaceship.place_component`,
`spaceship.launch`, `city.set_worklist`, `city.set_options`, `city.rename`,
`city.sell_improvement`, `city.set_rally`, `city.clear_rally`,
`city.set_governor`, `city.clear_governor`, `unit.airlift`,
`unit.paradrop`, `unit.teleport`, `unit.upgrade`, `unit.rehome`,
`unit.join_city`, `unit.establish_trade`, `unit.marketplace`, and
`unit.help_wonder`, `player.cast_vote`, `player.propose_server_setting`,
`player.cancel_vote`, `player.surrender`, bounded server-discovered `unit.special`,
`player.place_infrastructure`, plus relation-scoped meeting open/close, clause proposal
and removal, desired treaty acceptance/withdrawal, relation cancellation, and
outgoing vision/shared-tile withdrawal. Fine-grained capabilities are exposed
through exhaustive actor, target, and diplomatic-relation scopes. It is
playable but not yet human-control complete. The
completeness matrix below
remains a target, not a claim about current coverage. `strategic-v1` remains
the default and is unchanged.

The live authenticated surface is:

| Method and path | Current behavior |
| --- | --- |
| `GET /v2/games/{game_id}/me/health` | Caller seat, sanitized sidecar health, state/action availability, and only that seat's latest durable phase-end event. |
| `GET /v2/games/{game_id}/me/state` | One caller-private state page: bounded pregame overview/nation/style/team catalogs, visible chat, and opaque chat-recipient rows in the lobby, or one fog-safe runtime section after start. |
| `GET /v2/games/{game_id}/me/legal-actions` | One page of current opaque action capabilities: pregame configure/readiness and normal-player chat globally in the lobby, then runtime capabilities optionally filtered to one current owned actor, known target tile, infrastructure tile, or diplomatic relation. |
| `POST /v2/games/{game_id}/me/batches` | Validate, durably reserve, and execute one command. |
| `GET /v2/games/{game_id}/me/receipts/{batch_id}` | Read the caller's durable receipt. |
| `GET /v1/games/{game_id}/phase-events?after_sequence=0&limit=100` | Public, bounded, sequence-paginated phase-end attribution for a v2 evaluation. |

The five `/v2/.../me` routes require the joined agent bearer and are scoped to
that exact seat. State/action routes require the caller's current healthy
sidecar generation; the lobby exposes only the pregame surface, and runtime
sections/actions do not appear until start. They never fall back to
strategic-v1 or fabricate placeholder data. The `phase-events` feed is an
authentication-free spectator
surface and does not exist for `strategic-v1` games.

## Durable phase-end attribution

Each v2 episode has a mode-`0600`, append-only, fsync-backed
`phase-events.jsonl` journal. An event is finalized exactly once after a
phase-end claim has a durable receipt and the supervisor has independently
proved that the old phase advanced or that the game terminated/failed. Its
contiguous `sequence` is the public pagination cursor. The public record is
closed to `sequence`, `turn`, `phase`, `place`, public seat/player/color and
controller identity, `source` (`agent` or `timeout`), `receipt_state`,
`resolution` (`advanced`, `terminal`, or `failed`), `deadline_started_at`,
`ended_at`, and `elapsed_s`.

The journal never stores or exposes agent IDs, batch IDs, sidecar generations,
state revisions, action IDs or slots, hashes, bearers, native references,
filesystem paths, or exception details. Journal creation, append, fsync, or
validation failure invalidates and fails the evaluation with the stable
`v2_phase_event_journal_unavailable` reason; provenance loss is never treated
as a successful evaluation. Authenticated health exposes the same safe event
as `last_phase_end`, filtered to the caller's place, so a timed-out harness can
distinguish a server-applied timeout end from a turn that was never received.
Status remains bounded and does not embed the event history.

## Process boundary

```text
agent harness
    | bounded v2 state pages + opaque legal action IDs
    | one-command batches guarded by a state revision
    v
supervisor HTTP API
    | authenticated, seat-scoped sidecar channel
    v
headless Freeciv client sidecar
    | native client packets and client-maintained rules state
    v
freeciv-server (normal protocol; GUI_AGENT spaceship-autoplace opt-out only)
```

The sidecar is a real Freeciv client built from the same checkout and owns one
player connection while consuming normal server packets. Its landed native
export turns a bounded subset of that player's fog-limited client cache and
live action probabilities into legal-action descriptors. It translates an
accepted opaque action ID into the normal client request path and verifies a
native processing boundary plus an action-specific postcondition.

Before `TAKE` or `READY`, the private native handshake pins protocol version,
the exact capability list, percent-tab encoding, maximum frame size, and a
SHA-256 schema ID derived from the ordered native row fields, parser value and
sentinel domains, scalar bounds, and every current action-rule contract. A
missing or different schema ID fails sidecar startup
transactionally. This prevents a stale `freeciv-agent` binary from reaching a
later state read with a grammar that the Python projection cannot interpret.
The current schema ID is
`sha256-3471520648d923f16fda4e1b58858301f343a64165b7e6cd2e3dd93af79cd3f4`.

The Freeciv server remains authoritative for legality, movement, combat,
research prerequisites, diplomacy, turn boundaries, and victory. The
supervisor owns credentials, evaluation timing, idempotency, telemetry, and
durable receipts. No v2 behavior belongs in `bridge.lua`, and the core game
server has no v2 action endpoint or v2-specific action policy. Its sole
agent-client exception is a `GUI_AGENT` client-kind check that disables the
legacy automatic spaceship placement for a seat under explicit agent control;
non-agent clients are unchanged.

## Current action slice

The native sidecar currently enumerates a bounded action catalog across global,
exact actor, target, and diplomatic-relation scopes. Several
Freeciv ruleset variants can produce multiple descriptors of the same family:

| Native kind | Public descriptor | Current operation and arguments |
| --- | --- | --- |
| `pregame.configure` | `pregame.configure` | Select one live nation/style pair plus a bounded leader name and sex while unready. Opaque choice IDs come only from the two bounded pregame state catalogs. Freeciv canonicalizes the first ASCII leader character to uppercase, and the public postcondition reports that canonical spelling. |
| `pregame.set_team` | `pregame.set_team` | Select one noncurrent team while unready using an opaque ID from the same-revision `pregame_teams` catalog. Rows identify the selected team and its members without exposing native team slots. |
| `pregame.set_ready` | `pregame.set_ready` | Set the exact opposite desired readiness state. `ready: true` is not advertised until every expected external sidecar generation is healthy. The last external ready packet starts the native game; no console start shortcut is used. |
| `player.cast_vote` | `player.cast_vote` | Cast a different yes, no, or abstain ballot on one active normal-client vote using exactly its current revision-bound opaque `vote_id`. The descriptor omits the already-confirmed choice. The native queue number and full queue binding remain private; dispatch uses only `voteinfo_do_vote`. |
| `player.propose_server_setting` | `player.propose_server_setting` | Propose a change to one exact server-advertised, currently changeable normal-client setting. Boolean and enum descriptors bind one noncurrent value and take `{}`. Integer, bitwise, and string descriptors take exactly `{"value": ...}` with the type and bounds in the live `arguments_schema`. Dispatch uses the typed Freeciv option API, not caller-supplied command text. Available in the lobby and while running when the normal client marks the setting changeable. |
| `player.cancel_vote` | `player.cancel_vote` | Cancel one exact active vote created by the caller. The opaque target binds the vote and arguments are exactly `{}`; another player's vote, all votes, and stale or resolved votes are never advertised. Available in the lobby and while running. |
| `player.surrender` | `player.surrender` | Surrender only the caller's own live player through the normal player command path. The player target is self, arguments are exactly `{}`, and the capability is available only after the game is running while that player is alive and has not already surrendered. |
| `phase.end` | `phase.end` | End the active player's phase; exact empty arguments. |
| `unit.move` | `unit.order` | Move an owned unit to one adjacent target tile; exact empty arguments. |
| `unit.attack` | `unit.perform_action` | Normal or suicide attack variants against a visible target stack; exact empty arguments. |
| `unit.special` | `unit.perform_action` | Server-discovered spy, sabotage, bombardment, nuclear, conquest, healing, and related ruleset actions for one exact actor and target tile. Native rule and target IDs remain private. Supported technology and building subtargets become request-bound opaque named choices, and supported fixed action subresults become ordered public `effects`. Classic Bribe Unit, Bribe Stack, and both Incite City variants expose an authenticated `gold_cost` maximum, re-quote immediately before dispatch, and carry that ceiling into a server-side guard. Bribe Stack additionally freezes the exact visible target-stack signature. The four Freeciv user-action slots are accepted only in their simple form with no complex subtarget or subresult. Direct extra/extra-not-there routing, specialist subtargets, and unmodeled subresult families remain omitted. |
| `city.found` | `unit.perform_action` | Found a city on the unit's current tile; requires a bounded `city_name`. |
| `research.set_target` | `research.set_target` | Select an enumerated opaque immediate-research choice; exact empty arguments. |
| `research.set_goal` | `research.set_goal` | Select or clear an enumerated opaque long-term research goal; exact empty arguments. |
| `economy.set_rates` | `economy.set_rates` | Set exact tax, luxury, and science integers within the current Freeciv rate constraint. |
| `player.send_chat` | `player.send_chat` | During both `PREPARING` and `RUNNING`, send one normal-player `global`, `allied`, or `private` message. Private messages require a same-revision opaque `recipient_id` from a `chat_recipients` row whose `can_message` is true; global/allied messages omit it. The message must be strict UTF-8 of 1–512 encoded bytes, may contain colons and command-looking text, must not begin or end with ASCII space U+0020, and must not contain Unicode `Cc` or `Cf` code points. The native client—not the caller—generates one leading ASCII space for global, `.` for allied, or the exact `PlayerName:` routing prefix for private. The server parses the protective global space as public chat and trims it before display, so message text cannot become a server command or choose a different recipient. Direct connection messaging remains unavailable. |
| `city.set_production` | `city.set_production` | Select one noncurrent ruleset production target Freeciv says this owned city can build now; exact empty arguments. Actor-scoped only. |
| `city.buy_production` | `city.buy_production` | Buy the owned city's exact current production when Freeciv permits it and the player can afford its cached cost; exact empty arguments. Actor-scoped only. |
| `city.work_tile` | `city.assign_citizen` | Move the first positive normal specialist to one exact, seen, nonfree, currently unworked tile that Freeciv says the owned city can work; exact empty arguments. Actor-scoped only. |
| `city.unwork_tile` | `city.assign_citizen` | Stop working one exact, nonfree tile and create one default specialist; exact empty arguments. Actor-scoped only. |
| `city.set_specialist` | `city.set_specialist` | Convert one citizen from an exact positive normal specialist type to one distinct normal specialist type Freeciv says the owned city can use; exact empty arguments. Actor-scoped only. |
| `city.set_worklist` | `city.set_worklist` | Replace the owned city's exact ordered worklist with zero through 64 opaque production IDs. Repeated IDs are allowed. A current stale item may be preserved or reordered only up to its existing multiplicity; new occurrences require `can_queue`. Semantic no-ops are rejected. Actor-scoped only, and omitted when no non-noop invocation exists. |
| `city.set_options` | `city.set_options` | Set exact `allow_disband` and normalized `new_citizens` (`default`, `science`, or `gold`) values while preserving unrelated option bits. Repairs the legacy simultaneous science/gold-bit conflict; semantic no-ops are rejected. Actor-scoped only. |
| `city.rename` | `city.rename` | Rename the exact owned city with a strict UTF-8, control-free name of 1 through 119 bytes. Semantic no-ops are rejected. Actor-scoped only. |
| `city.sell_improvement` | `city.sell_improvement` | Sell one exact currently installed, currently sellable improvement before the city's once-per-turn sale has been used. The opaque target exposes the current sale price. Exact empty arguments; actor-scoped only. |
| `city.set_rally` | `city.set_rally` | Replace an owned city's rally plan with one normal-client route to an exact remembered or visible target tile selected through target-on-demand discovery. Requires exact `persistent: boolean`; route steps, native target, and digest remain private. Actor-plus-target-scoped only. |
| `city.clear_rally` | `city.set_rally` | Clear one exact owned city's active rally plan to the canonical inactive state. Exact empty arguments; actor-scoped only and absent when no plan is active. |
| `city.set_governor` | `city.set_governor` | Install or replace the exact owned city's client CMA goal. Requires bounded minimum-surplus and weight vectors for food, production, trade, gold, luxury, and science plus celebration weight, `require_happy`, and `maximize_growth`; semantic no-ops are rejected. Actor-scoped only. |
| `city.clear_governor` | `city.set_governor` | Release one exact owned city from client CMA control. Exact empty arguments; actor-scoped only and absent when no governor is active. |
| `unit.start_activity` | `unit.perform_action` | Start cultivate, mine, irrigate, pillage, transform, clean, base, road, or plant through one exact ruleset action and, when required, one concrete opaque extra target. Actor-scoped only. |
| `unit.cancel_activity` | `unit.order` | Cancel the owned unit's current non-idle activity and return it to idle; exact empty arguments. Actor-scoped only. |
| `unit.sentry` | `unit.order` | Put one owned nonsentry unit on sentry through the normal client activity request; exact empty arguments. Actor-scoped only. |
| `unit.auto_work` | `unit.order` | Hand one exact owned idle unit with no queued orders or goto plan to Freeciv's native autoworker when the server predicate permits it. Exact empty arguments; actor-scoped only. |
| `unit.auto_explore` | `unit.order` | Hand one exact owned idle unit with no queued orders or goto plan to Freeciv's native auto-explorer when the exact Explore activity predicate permits it. Exact empty arguments; actor-scoped only. |
| `unit.cancel_automation` | `unit.order` | Take an exact auto-working or auto-exploring unit back under manual control through the normal two-request client activity helper. Exact empty arguments; actor-scoped only. |
| `unit.cancel_orders` | `unit.order` | Clear an exact owned manual unit's queued route when it is idle and has queued orders. A private goto destination may be present for a direct goto or absent for a route inherited from a city rally point; route details remain private. Exact empty arguments; actor-scoped only. |
| `unit.clear_action_decision` | `unit.order` | Dismiss one exact pending active or passive action decision for an owned unit. Own-unit state exposes the decision kind and an opaque decision-tile ID; that tile can be used only with the same actor to discover the server-advertised target actions, even when it is still unknown. Selecting one of those exact target actions sends the action and clears the client decision transactionally. Exact empty arguments; actor-scoped only. |
| `unit.goto` | `unit.order` | Queue an exact normal-client goto route to one of at most 64 deterministic nearest reachable visible or remembered destinations within real map distance 8. A legal occupied destination may freeze a final action-move order. The opaque target exposes only its public tile ID and known coordinates; route steps and costs remain private. Exact empty arguments; actor-scoped or actor-plus-target-scoped. |
| `unit.goto_and_perform` | `unit.order` | Queue a normal-client route with one frozen permitted, nonconsuming native action as its final order. The destination tile, semantic action, exact city lifecycle or visible stack signature, private subtarget, order list, and digest are bound by the actor-plus-target lease; queued unit-target actions are excluded because the native order carries only a tile. No native action/order IDs or packet fields are public. Exact empty arguments. |
| `unit.connect_route` | `unit.order` | Queue a native road or irrigation connect route to an exact remembered or visible tile, including recursive road prerequisites and interleaved construction/movement orders. The semantic activity and opaque extra are public; native orders, action IDs, directions, and digest stay private. Exact empty arguments; actor-plus-target-scoped only. |
| `unit.set_route` | `unit.order` | Queue a caller-selected `goto` or closed `patrol` through one through 64 ordered opaque remembered/visible tile waypoint IDs. The normal client pathfinder materializes every segment; native directions, costs, and the exact route digest stay private. Actor-scoped only. |
| `unit.attack_route` | `unit.order` | Queue the normal client's attack-capable path to one caller-selected remembered or visible opaque destination. Every movement step uses Freeciv's action-move order; the server still chooses legality and may pause at any step for a player action decision. Exact `{destination_id}` arguments; actor-scoped only. |
| `unit.fortify` | `unit.perform_action` | Start a legal Freeciv self-target fortify action for one owned unit that is not already fortifying or fortified; exact empty arguments. Actor-scoped only. |
| `unit.convert` | `unit.perform_action` | Start a legal Freeciv self-target conversion to the exact ruleset-defined opaque unit type; exact empty arguments. Actor-scoped only. |
| `unit.disband` | `unit.perform_action` | Execute a legal consuming Freeciv self-target disband action; exact empty arguments. Actor-scoped only. |
| `unit.homeless` | `unit.perform_action` | Execute a legal Freeciv self-target make-homeless action for a unit with a current home city; exact empty arguments. Actor-scoped only. |
| `unit.upgrade` | `unit.perform_action` | Upgrade one exact owned unit to the exact opaque ruleset successor in one exact visible or remembered city where Freeciv advertises `Upgrade Unit`; exact empty arguments. Actor-scoped only. |
| `unit.rehome` | `unit.perform_action` | Change one exact owned unit's home to one exact visible or remembered city through `Home City`; exact empty arguments. Actor-scoped only. |
| `unit.join_city` | `unit.perform_action` | Consume one exact owned unit to add its exact ruleset population value to one exact visible or remembered city through `Join City`; exact empty arguments. Actor-scoped only. |
| `unit.establish_trade` | `unit.perform_action` | Consume one exact owned caravan whose exact owned home city is bound as the source to establish a route with one distinct exact visible or remembered city; exact empty arguments. Actor-scoped only. |
| `unit.marketplace` | `unit.perform_action` | Consume one exact owned caravan whose exact owned home city is bound as the source to enter one distinct exact visible or remembered city's marketplace; exact empty arguments. Actor-scoped only. |
| `unit.help_wonder` | `unit.perform_action` | Consume one exact owned unit to contribute its ruleset shield value to production in one exact visible or remembered city through `Help Wonder`; exact empty arguments. Actor-scoped only. |
| `unit.airlift` | `unit.perform_action` | Airlift one owned unit from its exact current owned city to one distinct exact cached own or allied city through `Airlift Unit` when Freeciv's normal client probability permits it; exact empty arguments. The source and destination city sites, tiles, and lifetimes are frozen. Actor-scoped only. |
| `unit.paradrop` | `unit.perform_action` | Paradrop one owned unit to one exact visible, remembered, or fully redacted unknown nonorigin tile through `Paradrop Unit`, `Paradrop Unit Frighten`, or `Paradrop Unit Enter`; exact empty arguments. Actor-plus-target-scoped only. |
| `unit.teleport` | `unit.perform_action` | Teleport one owned unit to one exact visible, remembered, or fully redacted unknown nonorigin tile through `Teleport`, `Teleport2`, `Teleport3`, `Teleport Frighten`, or `Teleport Enter`; exact empty arguments. Actor-plus-target-scoped only. |
| `unit.board` | `unit.perform_action` | Board owned cargo onto one exact visible domestic or allied transporter on the same tile, including a native-legal direct transporter switch. Actor-scoped only. |
| `unit.deboard` | `unit.perform_action` | Detach owned cargo from its exact current visible domestic or allied transporter without moving either unit. Actor-scoped only. |
| `unit.embark` | `unit.perform_action` | Move owned cargo onto one exact visible domestic or allied transporter on an adjacent currently seen tile, including a native-legal direct transporter switch. Actor-scoped only. |
| `unit.disembark` | `unit.perform_action` | Detach and move owned cargo to one exact adjacent tile, including a currently seen, remembered, or truly unknown destination, while binding its current visible domestic or allied transporter. Unknown destinations expose only opaque topology and `visibility: "unknown"`; cached terrain, ownership, extras, resources, labels, and yields remain absent. Actor-scoped only. |
| `unit.load` | `unit.perform_action` | Use an owned transporter to load or reparent one exact visible domestic or allied cargo unit on the same tile, only when the ruleset advertises that action. Actor-scoped only. |
| `unit.unload` | `unit.perform_action` | Use an owned transporter to detach one exact directly carried visible domestic or allied cargo unit without moving either unit. Actor-scoped only. |
| `government.revolution` | `government.revolution` | Start an untargeted revolution when the ruleset permits it and the transition can be acknowledged without ambiguity; exact empty arguments. Player-actor-scoped only. |
| `government.change` | `government.change` | Select one noncurrent, nontarget, non-Anarchy government that Freeciv says is legal and whose immediate transition can be acknowledged safely; exact empty arguments. Player-actor-scoped only. |
| `player.set_multiplier` | `player.set_multiplier` | Select one exact legal ruleset policy target while preserving every other multiplier target and all current values; exact empty arguments. Player-actor-scoped only. |
| `player.place_infrastructure` | `player.set_infrastructure` | Spend current infrastructure points to begin one exact advertised infrastructure extra on an exact seen target tile. The target advertises opaque extra IDs with name, cost, and build turns; execution accepts one advertised `extra_id`. Player-plus-target-scoped only. |
| `spaceship.place_component` | `spaceship.place_component` | Place one exact available structural slot or the next fuel, propulsion, habitation, life-support, or solar-panel part. Player-actor-scoped only. |
| `spaceship.launch` | `spaceship.launch` | Launch the exact own spaceship only when Freeciv reports a positive success rate and an available capital. Player-actor-scoped only. |

Vote application is positive only after the normal client receives the exact
request-correlated structured `PACKET_VOTE_UPDATE` for that vote. The local
`client_vote` field is optimistic and is never treated as server authority.
Because accepted non-no-op ballots update the tallies before any resolve and
remove packets, a decisive vote can return `applied` even when it disappears
or starts the game in the same request. Missing or mismatched updates remain
terminal `ambiguous` and nonretryable. The state surface retains the latest 64
structured outcomes (`passed`, `failed`, or `removed`) under a stable
generation-scoped `vote_ref`; `vote_id` remains revision-bound and actionable
only while `status` is `active`.

A setting proposal is
`applied` only when the typed option has the requested value or the exact
request-correlated new-vote notification proves the proposal was created;
the vote need not remain active because it may resolve immediately. Vote
cancellation requires the request-correlated vote-aborted notification and
disappearance of the bound own vote. Surrender
requires the request-correlated game-end notification. If any of these
governance actions crosses the native processing boundary without its exact
postcondition, the receipt is terminal `ambiguous` and nonretryable; a vote
that resolves quickly is never mislabeled as a clean rejection.

These actions are a closed projection of the normal-player governance
surface, not a command console. `player.propose_server_setting` is generated
only from the connection-specific option catalog and sends only the selected
typed option value through Freeciv's option setter. The API accepts no slash
command, option name, native vote text, or arbitrary server-command string.
The other control-level families -- `cut`, `debug`, `rulesetdir`, `aitoggle`,
`create`, `restricted`, `novice`, `easy`, `normal`, `hard`, `cheating`,
`experimental`, `timeoutincrease`, `remove`, `load`, `read`, `reset`,
`default`, and `kick` -- are not modeled and remain absent. The admin/hack
`quit`, `wall`, `connectmsg`, `metaconnection`, `metaserver`, `cmdlevel`,
`playercolor`, `playernation`, `endgame`, `save`, `scensave`, `write`, `lua`,
and `aicmd` families are also outside the player API. Lobby team choice is
available only through the separate closed `pregame.set_team` descriptor.

| `diplomacy.open_meeting` / `diplomacy.close_meeting` | `diplomacy.meeting` | Open or close negotiations with one exact opaque relation. Player-plus-relation-scoped only. |
| `diplomacy.propose_clause` | `diplomacy.clause` | Propose an exact directional technology, map, sea map, city, pact, vision, embassy, or shared-tiles clause. Technology candidates match the normal GTK client: when the giver's team lacks an embassy with the receiver, the client cannot apply the receiver-prerequisite filter and leaves final validation to the server. Gold takes exact `{gold: N}` within the descriptor's current bound. Player-plus-relation-scoped only. |
| `diplomacy.remove_clause` | `diplomacy.clause` | Remove one exact currently projected clause by opaque treaty identity; exact empty arguments. A city clause remains removable if its city is no longer in the caller's visible city-site catalog; the public value is then an opaque `available: false` placeholder and exposes neither the hidden native city ID nor its name. Player-plus-relation-scoped only. |
| `diplomacy.accept` / `diplomacy.withdraw_acceptance` | `diplomacy.acceptance` | Set the caller's desired acceptance state for the exact current clause digest. Exact empty arguments; never replay an ambiguous acceptance. Player-plus-relation-scoped only. |
| `diplomacy.break_relation` | `diplomacy.relation` | Lower the current pact only when Freeciv's native cancellation predicate is exactly allowed. Player-plus-relation-scoped only. |
| `diplomacy.withdraw_vision` / `diplomacy.withdraw_shared_tiles` | `diplomacy.withdraw` | Withdraw one exact outgoing benefit; exact empty arguments. Player-plus-relation-scoped only. |

Worker capabilities are canonicalized to the semantic activity plus concrete
extra target. If multiple private ruleset action variants produce that same
outcome, only one deterministic capability is retained; the public descriptor
does not promise a distinguishable ruleset variant. A same-activity restart,
including switching to another extra, is omitted until the caller first uses
`unit.cancel_activity`, because the current client cache cannot acknowledge
that transition unambiguously. Pillage is exported only with a concrete
ruleset action and concrete extra target, and its receipt verifies the exact
installed `pillage` activity and target; it is not a server-selected target.

Self-target capabilities are also canonicalized by their provable result and,
for conversion, the exact target unit type. When multiple private ruleset
actions install the same result, the sidecar chooses one deterministically by
probability quality and native action order. The public `variant` is an opaque
seat-scoped identifier: private action names and numeric action/type IDs do not
cross the boundary. `not_implemented` probabilities are omitted. Reissuing
sentry, fortify/fortified, or conversion while the matching activity is
already installed is omitted because it cannot produce a new exact receipt.
Returning a nonidle unit to idle/wake continues to use the existing
`unit.cancel_activity` order rather than a second overlapping capability.

City-target unit capabilities bind an exact destination city-site lifetime;
trade-route and marketplace capabilities also bind the unit's exact owned
home city as their source. The public target contains only the city's opaque
identity, owner, name, tile, coordinates, cached size, and `own`, `visible`, or
`known` visibility. Foreign city internals never cross the boundary. Incapable
unit types are rejected before city discovery. Finite-range actions enumerate
only city tiles in the widest capable native action radius; an all-city scan is
reserved for a ruleset variant whose action distance is explicitly unlimited.

Transport capabilities require an exact owned actor and exact-certain cached
legality. Unit targets and current-transporter context may be domestic or
allied, but must be currently visible with nonzero lifetimes and exact direct
links. A private component signature binds every caller-visible unit in the
touched cargo chains: unit and owner lifetimes, both directions of the cached
diplomatic/contact relation, tile, type, capacity, occupancy, parent link, and
bounded nesting. The same signature predicts the one direct link transition
and, for embark/disembark, movement of the actor's recursive cargo subtree.
It is rechecked with the live probability immediately before the normal
client request and against the resulting cache before an applied receipt.
Freeciv's occupancy packet is only empty/nonempty. An owned transporter's
cargo is fully visible by transport alliance authority; for an allied
transporter, an empty list or a visible list equal to capacity is exact, but a
positive partial list could hide third-party cargo and therefore fails closed.
Enumeration also rejects a transition whose predicted allied-transporter
poststate would become such a partial list, because that result could not earn
an exact applied receipt.

Allied units are targets or transporter context only. They are deliberately
never actors: the normal server resolves a player command's actor from that
player's own unit list, so inventing allied-actor authority would create a
command the Classic client cannot issue. Hidden units, unresolved links, and
multistep plans remain absent. A remembered or truly unknown adjacent
disembark tile is eligible when the native action probability is exact; an
unknown target publishes only its opaque ID, coordinates, and unknown
visibility, with no cached or authoritative terrain, ownership, extra,
resource, label, yield, occupant, or other hidden target fact. Classic
does not advertise `Transport Load`, so `unit.load` is absent there even
though rulesets that enable it can expose owned-transporter/allied-cargo load
and switch capabilities. Stock Classic unit classes cannot naturally form a
nested transport chain; custom rulesets can, up to Freeciv's native depth
limit, when the entire involved cached component is visible and exact.
Embark/disembark adjacency is a native topology fact: the Python boundary can
independently require opaque references, closed redacted target rows, distinct
origins, and fixed target grammar, but does not reconstruct Freeciv's wrap/hex
topology from public coordinates. The native catalog enumerates only `adjc_iterate`
targets and execution repeats `is_tiles_adjacent` plus the exact live
action-probability and component-signature checks before dispatch.

Noncombat mobility retains every exact allowed ruleset variant as a distinct
opaque capability. Airlift requires an exact owned source plus an exact cached
own or allied destination city lifetime and tile; the normal client action
probability remains authoritative about the ruleset's allied-destination
style. Paradrop and teleport destinations are discovered only through one
explicit actor-plus-target query. Visible and remembered tiles preserve the
normal client's cached probability. A truly unknown tile is accepted only
when the same client-side movement check is possible, and its probability is
replaced with the public `unknown` range so hidden terrain, ownership, extras,
units, or cities cannot be inferred. The server still enforces final legality.
Keeping all eight destination variants out of the ordinary actor catalog makes
catalog size independent of map area; one target catalog remains capped at
256 actions. Airlift, paradrop, and teleport conquer variants remain reserved
for the combat slice and are excluded by exact name.

This list is closed fail-safe in the current projector: an unrecognized native
rule or contradictory target/result contract invalidates that observation
instead of becoming a model-visible action. It does **not** mean all
`unit.order` or `unit.perform_action` operations are implemented.

Research choices are capabilities, not caller-supplied technology names or
native IDs. The state catalog publishes opaque technology IDs, descriptive
states (`known`, `available`, `reachable`, `future`, or `unset`), and explicit
`can_target`/`can_goal` flags. An immediate target capability is emitted only
for a noncurrent choice Freeciv says can be researched now. A goal capability
is emitted only for a noncurrent choice flagged `can_goal`, including the
synthetic `unset` choice when the current goal can be cleared. The flags, not
an agent's interpretation of the descriptive state, are authoritative.

| Research state | `can_target` | `can_goal` |
| --- | --- | --- |
| `known` | false | false |
| `available` | true | true |
| `reachable` | false | true |
| `future` | true or false | true or false |
| `unset` | false | true |

The two `future` flags are always equal. They are normally both true, but may
both be false when Freeciv retains Future Tech as the current target or goal
after a technology-loss transition makes another Future Tech unavailable.
That false/false row is valid only while it is referenced by the current
target and/or goal. The current target/goal is not re-emitted as an action even
when its catalog flag is true.

The research summary also carries `choices_count` and `choices_digest`. The
catalog contains exactly one synthetic `Unset` row and is digested in ascending
native technology-ID order, including the optional `Future Tech` row and the
final `Unset` row. The digest is standard FNV-1a-64 (offset basis
`14695981039346656037`, prime `1099511628211`). Each choice contributes, in
order: its native ID as unsigned 32-bit big-endian; its canonical decoded rule
name as a 32-bit big-endian byte length followed by UTF-8 bytes; its state as a
one-byte length followed by ASCII bytes; then one byte each for `can_target`
and `can_goal` (`0` or `1`). The text form is
`fnv1a64-` followed by exactly 16 lowercase hexadecimal digits. The Python
boundary independently recomputes both count and digest before accepting the
catalog and then checks the exact research action sets.

The economy capability is emitted only when Freeciv reports rates as
changeable. Its schema requires exactly `tax`, `luxury`, and `science`; each is
an integer from zero through the advertised `max_rate`, and the three values
must sum to 100. The compact schema publishes those per-field bounds plus
`metadata.exact_sum` and a server step of one; the projector enforces the sum
semantically instead of enumerating every combination. The same current
constraint is checked again before the normal client packet is sent.

Government choices are native capabilities, not caller-supplied names or
numeric IDs. The catalog is complete and bounded to the Freeciv protocol limit
of 127 governments; an empty, oversized, noncontiguous, duplicate, or
cross-linked catalog invalidates the observation. Native government numbers
never cross the projection. A revolution capability is present only when
untargeted revolution is allowed, the player can change to the ruleset's
during-revolution government, and the player is not already in that government
with it selected as the target. A change capability excludes the current,
selected, and during-revolution governments, requires Freeciv's requirements
predicate, and is emitted only when the result can be observed unambiguously:
either the no-Anarchy effect is active, the revolution finish is in a future
turn, or it is a nonpositive finish.

The sidecar deliberately exports no direct change while a positive revolution
finish is due at or before the current turn unless no-Anarchy is active. A
zero-turn no-Anarchy choice is legal: Freeciv records the target immediately
and enacts it at the end of the player phase. Because that path intentionally
does not echo the pending target in a player-state packet, the sidecar proves
acceptance from the exact request-correlated `E_REVOLT_START` event rather than
guessing from a later cache state. The public status is one of `stable`,
`anarchy`, `anarchy_targeted`, `choice_required`, or `enactment_pending`;
revolution methods are `fixed`, `random`, `quickening`, or
`random_quickening`.

Movement, transport disembark, paradrop, and teleport are the landed
unknown-target exceptions. Their target metadata contains only an opaque tile
ID, coordinates, and `visibility: "unknown"`; terrain, ownership, extras,
resources, labels, and yields are omitted. Matching relocation actions remain
unknown/possibly-legal. Disembark is exported only when its actor-only native
legality is exact and its full transport component transition is receiptable.
Unknown-target attacks and all other unknown-target action families are
rejected from the export. Visible or remembered known tiles retain their
client-cache classification.

## Target human-control completeness matrix

The sidecar will be human-control complete only when every normal client
operation below is represented and tested against server legality. The
listed kinds above are the current bounded slice; other controls in
these rows are requirements, not available commands:

| Domain | Required controls | Initial action kinds |
| --- | --- | --- |
| Pregame | landed: choose nation, leader, sex, style, and team from bounded live catalogs; exchange normal global/allied/private lobby messages using the visible roster; set or withdraw desired readiness; start only after every external seat is joined and ready. | `pregame.configure`, `pregame.set_team`, `pregame.set_ready`, `player.send_chat` |
| Unit orders | landed: adjacent move, bounded goto discovery, arbitrary remembered/visible target goto lookup, sentry, fortify, native automation, cancel automation, cancel queued routes, transport, and disband; target: patrol/waypoints, wait/skip, wake, repeat, and vigilant routes | `unit.order` plus the landed actor-scoped transport action families |
| Ruleset unit actions | attack, conquer, found city, all worker activities, pillage, upgrade, rehome, caravan trade/help-wonder, diplomat/spy actions, bombard, paradrop, airlift, nuke, convert, and every action/target Freeciv advertises | landed for the bounded result set documented below, including guarded Classic Bribe Unit, Bribe Stack, both Incite City variants, five exact Classic random espionage variants, both targeted technology-theft variants with regular/Future choices, targeted building sabotage/strike, supported fixed subresult effects, Classic extra conquest and hut variants, and the four simple user-action slots; direct extra/extra-not-there routing, specialist subtargets, and other unmodeled result/subresult families remain targets |
| Cities | choose/change production and buy production | `city.set_production`, `city.buy_production` |
| Citizens | work/unwork tiles and assign specialists | landed as `city.assign_citizen` and `city.set_specialist` |
| Automation | set, inspect, and clear city governor choices | `city.set_governor` |
| City management | worklists, rename, options, sell improvements, and rally points | landed: `city.set_worklist`, `city.rename`, `city.set_options`, `city.sell_improvement`, and target-on-demand `city.set_rally` set/clear |
| Research | choose immediate research and a longer-term goal | `research.set_target`, `research.set_goal` |
| Economy | choose legal tax/science/luxury totals | `economy.set_rates` |
| Government | start revolution and select a legal government | `government.revolution`, `government.change` |
| Diplomacy | landed: open/close meetings, add/remove every server-advertised clause (maps, gold, cities, technologies, shared vision, embassies, shared tiles, and pacts), set/withdraw acceptance, lower cancellable pacts, and withdraw outgoing vision/shared tiles | `diplomacy.meeting`, `diplomacy.clause`, `diplomacy.acceptance`, `diplomacy.relation`, `diplomacy.withdraw` |
| Player controls | select legal infrastructure and ruleset multipliers/policies exposed by the client | `player.set_infrastructure`, `player.set_multiplier` |
| Votes and player governance | inspect visible caller/text/tallies, threshold, team-only status, request-confirmed own ballot, and a bounded structured outcome history; cast a different yes/no/abstain ballot when permitted; propose typed changes to currently changeable server settings; cancel only the caller's own active vote; surrender only the caller's running player | `player.cast_vote`, `player.propose_server_setting`, `player.cancel_vote`, `player.surrender` |
| Spaceship | inspect inventory, place structural/component/module parts in legal slots, and launch when legal (part construction is city production) | `spaceship.place_component`, `spaceship.launch` |
| Phase | explicitly finish the seat's action phase | `phase.end` |

Ruleset-defined unit actions, extras, governments, specialists, production
targets, and diplomatic clauses must be discovered from live client state;
hard-coded Classic lists are not a completeness mechanism.

## Negotiation and compatibility

Game creation accepts `control_protocol` with exactly two current values:

- omitted or `strategic-v1`: existing behavior and existing routes;
- `full-control-v2`: requires every joining harness to send
  `supported_control_protocols` containing `full-control-v2`.

Missing or incompatible v2 capability advertisement returns HTTP 426. A
client may advertise additional protocol names for forward compatibility.
Capability order is insignificant; the server normalizes it before immutable
identity comparison. Empty, malformed, duplicate, or incompatible v2 lists
also return HTTP 426 before a seat is claimed. The chosen `control_protocol`
is immutable and appears in the create response,
manifest config, public status, join response, and saved agent session.

## State and context budget

Every v2 observation is tied to a strict state revision:

```json
{"turn": 27, "revision": 4, "state_token": "state_opaque-token"}
```

`revision` advances whenever the native sidecar's exported rows or action
catalog change, including changes within one Freeciv turn. `state_token` is an
opaque, seat-generation-scoped MAC and does not encode a trusted client
command. A command issued against any noncurrent revision fails as
`stale_revision`; an action ID not bound in that current revision fails as
`action_expired`.

The landed state endpoint is sectioned and cursor-paged. Without a query it
returns the `overview` section. A caller may request exactly one of:

- `overview`
- `pregame_nations` (lobby only)
- `pregame_styles` (lobby only)
- `pregame_teams` (lobby only)
- `votes` (active visible votes plus the latest 64 structured outcomes in the current seat epoch)
- `research`
- `governments`
- `multipliers`
- `spaceship`
- `diplomacy`
- `diplomacy_clauses` (requires `relation_id` from a `diplomacy` row)
- `known_tiles`
- `map_tiles` (the complete fog-safe map; unknown rows expose coordinates only)
- `infrastructure`
- `cities`
- `city_detail`
- `city_citizens`
- `city_worker_tasks`
- `city_trade_routes`
- `city_build_choices`
- `city_worklist`
- `city_improvements`
- `city_governor`
- `tile_window`
- `city_sites`
- `units`
- `unit_route` (requires `actor_id` for an owned unit with a reconstructable queued route)
- `tombstones`
- `chat` (the latest 64 normal-client chat/event packets)
- `chat_recipients` (the visible normal-player roster for private chat, available
  both in the lobby and after start; rows expose only an opaque player ID,
  display name, `self`, `connected`, and `can_message`)

Use `?section=SECTION&limit=N`, where `N` is 1 through 16. City child sections
also require `actor_id=CURRENT_OWN_CITY_ID`; `unit_route` requires
`actor_id=CURRENT_OWN_UNIT_ID`. `diplomacy_clauses` requires
`relation_id=CURRENT_RELATION_ID`; clauses are paged per meeting rather than as
one global catalog. `tile_window` instead requires a
known `center_id=TILE_ID` and `radius=0..8`; it uses Freeciv's map topology and
wrap axes and never makes an unknown tile's terrain or ownership visible. A
response contains
`page.items`, `page.total_items`, and an opaque `page.next_cursor`. Continue
with `?cursor=CURSOR` alone. Cursors are caller/endpoint/revision scoped,
retry-safe, and valid for at least five minutes. Repeating a successful
continuation returns the byte-equivalent authenticated page without repeating
native I/O. `page.cursor_expires_at` is the RFC 3339 expiry for
`next_cursor`, or `null` on the final page, and every successful continuation
gives its next cursor a fresh lifetime. An authentic expired cursor returns
retryable `cursor_expired` with a public `details.restart` query; a forged
cursor remains nonretryable `invalid_request`. Unexpired cursors are never
evicted to admit a new cursor: capacity pressure returns retryable
`rate_limited`, while authentic expired-cursor tombstones remain recognizable
for their retention window. In addition to the sixteen-item cap, every
canonical public page is capped at 65,536 UTF-8 JSON bytes. Pagination stops
early at that byte boundary; a single oversized item fails closed as
`scope_too_large`. The current implementation retains two projected revisions
and does not implement `since_revision` or an event-log section. Research rows
include the reachable dependency graph, next-step/path cost, prerequisites,
and unlocks.

The overview contains native client/turn/phase facts, semantic map width,
height, topology and wrap axes, own player identity,
current government and revolution state, current economy values and rate
constraints, a research summary, section counts, and legal-action counts. The
own economy projection includes gold,
current tax/science/luxury percentages, `changeable_tax`, and `max_rate`. The
research summary includes progress plus opaque IDs for the current immediate
target and long-term goal. The detail sections currently project:

- while the client is preparing, one bounded nation catalog and one bounded
  city-style catalog. Their opaque IDs are the only valid configuration inputs;
  `overview` reports `client_state: preparing`, turn zero, no active phase, the
  exact current leader attributes, and the caller's desired readiness;
- the caller's known, available, and reachable technologies plus bounded
  `future` and `unset` choices, with opaque technology IDs and explicit
  `can_target`/`can_goal` flags;
- the complete bounded ruleset government catalog (at most 127 entries), with
  opaque IDs, canonical names, current/target/Anarchy markers, and
  `can_change`; the own player summary carries current/target/Anarchy opaque
  IDs, revolution status and timing, method, and safe revolution availability;
- the complete bounded ruleset multiplier catalog (at most 50 entries), with
  opaque IDs, current and next-turn target values, legal grid bounds, cooldown
  state, and exact `can_change` flags;
- the caller's diplomatic relation with other players, including each opaque
  `relation_id`, meeting generation/acceptance state, and native cancellation
  eligibility/reason;
- exact current treaty clauses cross-linked to their opaque relation, meeting,
  giver, semantic type, and opaque technology/city value where applicable;
- `known_tiles` remains the compact visible/remembered client-cache catalog.
  Bounded `tile_window` and private unit `target_tiles` scopes additionally
  expose cached terrain ownership, an optional label and resource, generic
  food/shields/trade yields, and ruleset extras with opaque `extra_id`, name,
  and semantic `causes` (`special` denotes a zero native cause mask). Their
  native row count includes extra children, while public `total_items` counts
  tile parents only. Unknown parents remain redacted to ID, coordinates,
  visibility, and window distance and never carry extra children;
- compact owned-city summaries with identity, location, size, surpluses,
  current production/buy facts, and airlift capacity. Larger internals are
  reached through independently pageable, owned-city-scoped sections:
  `city_detail` exposes management options, rally state and child counts;
  `city_citizens` exposes exact citizen state:
  every emitted city-radius tile's opaque ID, worked/free-center/can-work
  flags and six signed output yields, plus the complete specialist catalog
  with opaque IDs, rule names, counts, a `counts_toward_population` flag,
  human usability, the single default type, and six signed per-specialist
  yields. Superspecialists contribute to citizen-base output but not city size
  or assignable citizen counts. `city_detail` adds final
  happy/content/unhappy/angry
  counts, worker/specialist counts, food stock and granary size, nullable
  `growth_turns` (`null` means never), pollution, and per-output accounting.
  Each output publishes citizen base, gross, net, surplus, usage, waste, and
  unhappy penalty; `gross` is derived as net plus waste plus unhappy penalty.
  Seen radius tiles are
  complete; remembered tiles appear only when still worked or the free center,
  and cannot claim current workability. The boundary verifies row counts,
  population-counting specialist conservation, a common contiguous full
  specialist catalog, and one population-counting default type before exposing
  the section. `city_worklist`,
  `city_build_choices`, and `city_improvements` expose the exact ordered
  worklist, the union of current entries and currently queueable opaque
  production choices, and installed improvements with sellability and price.
  `city_trade_routes` exposes the own-city trade-route packet/cache records:
  a stable opaque route ID, position, raw and effective value, direction, and
  opaque goods. A partner city is linked only when it is owned or currently
  visible and its identity/name/visibility exactly cross-link to `city_sites`;
  remembered or unavailable partners project only `available: false`, without
  a native city number or correlatable substitute ID. Compact city rows expose
  the current route count and ruleset-derived capacity so the scoped catalog
  is cardinality checked.
  `city_detail` exposes whether native client-governor control is enabled;
  the full CMA minimum-surplus/weight goal is available only through the
  bounded, owned-city `city_governor` section.
  Each build choice carries `can_queue`, `can_build_now`,
  `preservable_count`, city-context shield cost and post-change stock, turn
  estimates with and without existing stock, six-output upkeep, and the
  applicable unit or building details. Unit details include combat, movement,
  hit-point, transport, fuel, population, bombard, city-founding, vision, and
  paradrop values from the active ruleset. Building details include genus,
  current obsolescence/redundancy, conversion status, and the ruleset cache's
  unit/extra/disaster/action capability flags. A turn estimate of `null` means
  Freeciv reports `never`; conversion projects have `null` cost and turn
  fields because their shield output is continuous. A nonqueueable choice
  exists only because `preservable_count` occurrences remain in the current
  worklist. `city_detail` also carries
  the once-per-turn `did_sell` flag and normalized options. Legacy simultaneous
  science/gold new-citizen bits project with science precedence and
  `options.conflict: true` so `city.set_options` can repair them;
- all currently own, visible, or remembered city sites, including only an
  opaque city and owner ID, cached name, tile and coordinates, cached size,
  and `own`, `visible`, or `known` visibility. Every own city site must exactly
  match its full owned-city row; foreign sites expose no production, citizen,
  economy, improvement, rally, or other internal state;
- own units, including an opaque unit-type ID, opaque home-city reference,
  optional opaque conversion-target type, semantic activity, progress, an
  opaque current extra target when present, and exact transport state,
  capacity, occupancy, an opaque transporter reference when resolved, and any
  pending action-decision kind plus its same-actor-only opaque tile reference.
  Own units also expose semantic automation controller state (`none`,
  `auto_work`, or `auto_explore`), the exact `has_orders` boolean, and an
  opaque route identity with `path_available` and `path_step_count`. When the
  route is reconstructable, `unit_route` pages its remaining ordered
  `move`, `action_move`, or `wait` steps as opaque tile IDs and safe map
  coordinates. Native tile IDs, directions, costs, digests, and controller
  IDs remain private;
  currently visible foreign units
  include an opaque unit-type ID but no private ownership-only state;
- type-only tombstones for tracked player/city/unit lifetimes that disappeared
  from the allowed projection.

Unit and city incarnation tracking are bound to private client lifetime tokens.
If Freeciv removes either entity and reuses its numeric ID between two
observations, the next observation contains the old opaque incarnation's
tombstone and a new opaque live ID in the same snapshot. Invisible worked-city
placeholders retain lifetime zero and are not projectable; promotion by a real
city packet mints the lifetime. Direct real creation also mints, and an
ownership-change remove/recreate rotates it. Production, buy, and citizen
actions all bind that exact nonzero city lifetime at enumeration, processing
start, and acknowledgement. Private lifetime tokens are never serialized.
Player incarnation semantics are unchanged.

Unknown movement-target tiles expose only ID, coordinates, and
`visibility: "unknown"`. Visible/remembered tiles may expose cached terrain
and owner. Own units expose moves; visible foreign units do not. Native numeric
entity IDs, native action slots, native packet fields, hidden units, unseen
terrain/ownership, credentials, and another agent's reasoning never cross the
public projection. Any unknown, malformed, contradictory, oversized, or
cross-linked native row fails closed instead of being partially returned.

## Legal actions

The sidecar, not the model, enumerates legality. Each descriptor is bound to a
state revision and contains:

```json
{
  "action_id": "act_opaque",
  "kind": "unit.order",
  "label": "Move unit to (12, 8)",
  "subject": {
    "actor": {"type": "unit", "id": "unit_opaque"},
    "target": {"type": "tile", "id": "tile_opaque", "x": 12, "y": 8},
    "operation": "move",
    "variant": "standard",
    "consuming": false,
    "legality": "legal",
    "probability": {
      "kind": "exact",
      "minimum_percent": 100.0,
      "maximum_percent": 100.0
    }
  },
  "arguments_schema": {
    "type": "object",
    "properties": {},
    "additionalProperties": false
  },
  "state_revision": {"turn": 27, "revision": 4, "state_token": "state_opaque-token"}
}
```

`action_id` is an opaque, short-lived, seat-and-revision-scoped capability. The
harness cannot invent IDs or infer native packet fields from them. The current
move, attack, phase-end, research, city-production, city-buy, city-citizen,
worker-activity, self-unit, transport, government, multiplier, spaceship, and
city-governor
descriptors accept `{}`
because their target is already bound into the opaque action. Found-city
descriptors require the `city_name` described by their bounded
JSON-schema-shaped `arguments_schema`. The economy descriptor requires exact
`tax`, `luxury`, and `science` integer fields bounded by the current
`max_rate`, with a semantic sum of 100. The projector and sidecar both validate
arguments before translation.

In both the lobby and the running game, `player.send_chat` requires `channel`
and `message`. `global` and `allied` take no recipient; `private` additionally
requires the exact same-revision opaque `recipient_id` from
`chat_recipients`, and that row must report `can_message: true`. This flag is
true only when the recipient is connected and its current player name is safe
and unambiguous for Freeciv's `PlayerName:` syntax. Fetch that unscoped catalog
before choosing a private recipient and refresh it together with legal actions
after any revision change. The public row never exposes a native player number,
connection ID, or server command target. Immediately before sending, native
code revalidates the recipient, connection, and name. It prepends one ASCII
space for global chat, `.` for allied chat, and the selected exact
`PlayerName:` for private chat. The server parses the protective global space
as public chat and trims it before display. Consequently leading `/`, `.`, or
`:` text and embedded colons remain message text rather than changing the
routing mode or entering the server console. Messages are
strict UTF-8 of 1 through 512 encoded bytes; leading/trailing ASCII U+0020 and
all Unicode `Cc` and `Cf` code points are rejected identically at both
validation boundaries. Direct connection messaging is not exposed.

In the lobby, `pregame.configure` instead requires exactly the currently
enumerated opaque `nation_id` and `style_id`, a bounded `leader_name`, and the
advertised boolean `is_male`. `pregame.set_team` requires exactly one opaque
`team_id` from the same-revision lobby catalog and rejects the current team.
`pregame.set_ready` requires exactly one boolean
`ready` whose value is the opposite of the current desired state. Either action
invalidates the old capability and must be followed by a fresh state/legal read.

City management is also state-derived. `city.set_worklist` requires an exact
`items` array of opaque IDs from `city.management.build_choices`, preserves
order, permits repeats, and allows a nonqueueable ID only up to its published
`preservable_count`; it cannot add a new stale occurrence. `city.set_options`
requires exactly a boolean `allow_disband` and one normalized `new_citizens`
enum. `city.rename` uses the same bounded, control-free `city_name` shape as
found-city. `city.sell_improvement` binds its concrete opaque improvement
target and accepts `{}`.
`city.set_governor` requires the exact nested goal advertised by its schema;
`city.clear_governor` accepts `{}`. While a governor is active, direct
work/unwork/specialist capabilities are omitted so a manual citizen edit
cannot silently fight the client agent.

Descriptors also include public operation/variant, actor/target references,
whether the action always consumes the actor, and a normalized legality and
probability classification. Research descriptors have no actor and carry an
opaque technology target with its name and descriptive state; the matching
research state row carries the `can_target`/`can_goal` flags. The economy
descriptor has the caller's opaque player as actor and no target. Government
descriptors have that same player actor and one concrete opaque government
target. Self-target unit descriptors bind either the unit itself, its home city, an
activity, or its exact opaque conversion type. Their variants are opaque rather
than exposing a private Freeciv action name or number. Legal-action pages use
`?limit=1..16` and the same retry-safe cursor continuation contract
as state pages. Unchanged descriptors retain stable IDs within one revision;
every revision change expires them. Multiplier targets remain opaque and the
native client sends the complete packet-242 vector; an applied receipt requires
the selected target to change while every other target and every current value
remain exact. Spaceship placement and launch likewise require exact client-cache
postconditions and disable automatic placement only for the distinct agent GUI.

An agent can request one exhaustive actor catalog with
`?actor_id=OPAQUE_ACTOR_ID&limit=1..16`. The actor ID must be the current
caller's opaque player, city, or owned-unit ID from the same state revision;
foreign, stale, malformed, and cross-seat IDs fail closed. Actor-filtered
responses include the public actor ID and type in `page.scope`. Continue them
with `?cursor=CURSOR` alone: a cursor is exclusive, retry-safe, valid for at
least five minutes, and bound to the endpoint, seat generation, revision,
actor, native scope
view, next offset, and page size. It cannot be substituted into an unfiltered
catalog or another actor's catalog.

Every scoped page also carries one stable opaque `page.catalog_id` and a
boolean `page.catalog_complete`. Prefix pages have `catalog_complete: false`
and their descriptors are deliberately non-executable. Native bindings and the
player client's local action cache are promoted atomically only after the final
page validates the complete catalog and reports `catalog_complete: true`.
Clients discard a pending catalog on a newer revision, catalog/scope mismatch,
cursor expiry, or contract failure.

An owned unit, city, or the self player can query one exact bound tile with
`?actor_id=OPAQUE_ACTOR_ID&target_id=OPAQUE_TILE_ID`, optionally adding a page
`limit` from 1 through 16. No section or other parameter is valid. Both IDs
must come from the caller's current snapshot. `map_tiles` may bind a fully
redacted unknown coordinate for far `unit.goto`, `unit.paradrop`, or
`unit.teleport`; other target families require the visibility stated by their
live descriptor. Foreign, stale, malformed, and
cross-seat IDs fail before the private native query. The city's own source tile also fails before the
native query: explicit actor-scoped `clear_rally`, not source-tile set, is the
v2 clear operation. A valid target with no safe human-client action returns an
ordinary legal-actions page with zero items; it does not expose a private
route- or action-failure reason. City target catalogs can contain
`city.set_rally`; self-player target catalogs can contain infrastructure
placement; unit target catalogs can contain `unit.goto`,
`unit.goto_and_perform`, `unit.connect_route`, the three paradrop variants,
the five teleport variants, plus a bounded set of
server-advertised immediate actions. Every target catalog uses the same atomic cursor
contract as other scoped catalogs and must reach `catalog_complete: true`
before any descriptor can execute.

Server-discovered actions are projected as semantic `unit.perform_action`
operations. Native action numbers, rule names, target numbers, and subtarget
numbers never cross the HTTP boundary. The descriptor carries an opaque
variant, opaque city/unit/tile target identities, and Freeciv's current
probability range. Supported results are the argument-free subset of embassy
establishment, investigation, poisoning, gold/map theft, random city or
production sabotage, random and targeted technology theft, unit sabotage/capture,
bombardment, suitcase nuclear attack, distinct city/tile nuclear attack, stack
nuclear attack, city destruction, expulsion, production strikes, city
conquest, healing, ransom, plague, spy attack/escape, wipe,
paradrop-conquer, teleport-conquer, Classic extra conquest variants 1/2, and
Classic hut entry/frightening variants 1/2. Fixed native subresults are accepted
only for the enumerated action contracts; their canonical order is preserved as
public `effects` such as `enter_huts`, `frighten_huts`, `may_embark`, and
`non_lethal_to_target_units`. Technology and building subtargets are accepted
only by the targeted-theft and targeted-building contracts below. The four
ruleset user-action slots are included only when they carry neither a complex
subtarget nor a subresult. Random city sabotage is
limited to Classic `Sabotage City` and `Sabotage City Escape`; production
sabotage to `Sabotage City Production Escape`; and random technology theft to
`Steal Tech` and `Steal Tech Escape Expected`. These five, targeted technology
theft, and the hut actions retain Freeciv's unresolved probability instead of
fabricating odds.

Targeted technology theft is exposed only when the normal client is authorized
to see the victim's research through an embassy or team relationship. Each
normal-GUI-valid regular or Future technology becomes a separate action carrying a
human-readable name and an opaque `technology_choice.id`; the native technology
number never crosses the HTTP boundary. The frozen native slot binds the actor,
city, action, selected technology, native revision, and a digest of the victim's
research state. The client rechecks that exact choice before and after the fresh
server action-probability preflight, dispatches the selected technology as the
native subtarget, and reports `applied` only when that same technology changes
from unknown to known. Missing positive proof is terminal `ambiguous` and is
never replayed automatically.

Classic `Bribe Unit`, `Bribe Stack`, `Incite City`, and `Incite City Escape`
are exposed only after a request-correlated server quote that is no greater
than current gold. The descriptor publishes that quote as its maximum
`gold_cost`; execution revalidates it immediately before dispatch and the
server refuses before any ownership, treasury, or other action mutation if its
recomputed price exceeds the frozen ceiling. Unit bribery requires the old
target to disappear, one uniquely correlated owned replacement, and the
request-bound success event. Incitement requires the same city ID and tile
with a new owned lifecycle plus the success event. Stack bribery binds the
exact visible target-stack signature and actor lifetime. Because fog can hide
members, its exact request/action/actor/target-bound server completion receipt
is authoritative execution evidence. Request-local old-visible-unit
disappearance and owned replacement lifetimes are optional corroboration when
no members were visible; an exact visible baseline that conflicts with those
mappings fails closed. Any missing positive proof or correlated failure after
dispatch is terminal `ambiguous` and is never replayed. Exact Classic targeted
building sabotage and surgical building strikes are exposed as one frozen
action per normal-GUI-authorized improvement with an opaque
`building_choice.id`; the harness submits `{}` and the eligible-building list
is re-queried immediately before dispatch. Both targeted-theft variants accept
regular and Future technology choices. The non-Classic non-escape
production-sabotage action, direct extra/extra-not-there routing, specialist
subtargets, additional unmodeled ruleset results, and complex user actions are
deferred until every cost, extra, subtarget, or subresult can be safely bound
into the frozen catalog.

Classic `Investigate City` and its consuming alternate are ordinary
target-scoped `unit.perform_action` choices. An applied investigation requires
the exact request-bound `INVESTIGATE_STARTED`, full `CITY_INFO`, and
`INVESTIGATE_FINISHED` sequence for the frozen city incarnation and lifecycle;
actor movement or disappearance is not proof. The normal-client packet subset
is copied into a one-use native scope tied to that exact native revision and is
materialized only into the terminal receipt. The public `observation` contains
production, shield stock/surplus, installed improvements, six citizen-feeling
stages, and specialists. It contains no units, nationalities, routes, rally,
worklist, options, or any data that the normal client packet did not deliver.
Its `freshness` is `captured_at_receipt_revision`: it is an immutable historical
capture and never claims to describe a later revision. A missing or mismatched
packet boundary remains terminal `ambiguous` and is never replayed.

At most eight distinct actor-plus-target catalogs are accepted in one native
revision. Accepted catalogs are immutable and executable until that revision
changes; an accepted pair replays its frozen catalog. A ninth distinct pair
fails atomically with `scope_too_large` and does not evict prior catalogs. The
native response emits one checked IPC frame per event-loop tick, which reduces
queue pressure but does not guarantee transport capacity. A queue refusal
terminally poisons the stream, and the incomplete scope is never committed.
Python publishes action bindings only after the terminal page is consumed and
will not publish an old cursor after a newer revision arrives.

Immediately before dispatch, the native boundary rechecks the exact actor and
target lifetimes, then performs a second read-only server action-catalog
preflight for the same actor, tile, and resolved target. The current
server-authoritative probability must exactly match the frozen capability.
Visibility loss or a changed visible stack signature alone cannot prove a
stack action succeeded. Without a correlated positive postcondition, the
durable receipt is terminal `ambiguous` and is never automatically retried.

Action-discovery and action-preflight replies do not carry a unique request
token. If either exchange exceeds its native deadline, that sidecar process is
marked failed for that exchange type and must be replaced before a similar
request; an uncorrelated late reply can never populate a later catalog or
authorize an action.

Diplomacy uses the same exact two-parameter form with different opaque types:
`?actor_id=OPAQUE_SELF_PLAYER_ID&target_id=OPAQUE_RELATION_ID`. Take the actor
from `overview.player.id` and the target from a row's
`diplomacy[].relation_id`; a counterpart `player_id` is descriptive and is not
the relation target. The response scope is `player` plus
`diplomatic_relation`. Exhaust every returned cursor before executing any
descriptor: pair catalogs are validated and activated atomically only after
the final page proves the complete relation capability set.

Clause actions identify semantic type, giver, receiver, and an opaque
technology or city value where applicable. Gold proposals alone require
`{"gold": N}` within the advertised integer minimum/maximum; every other
diplomacy action uses `{}`. Construct the complete desired deal before using
`diplomacy.acceptance`. Acceptance is a desired-state operation, not a blind
toggle: the catalog emits `accept` or `withdraw_acceptance` only when that
transition is currently meaningful. A meeting generation, clause digest,
acceptance bit, relation state, or outgoing-benefit change after enumeration
fails closed at the processing boundary. Refresh state and re-enumerate after
a proved rejection or stale revision. Never replay or substitute an ambiguous
acceptance command, because the server may already have processed its native
toggle.

Target-bound native slots are stateless capabilities of the exact form
`t` plus eight uppercase hexadecimal selector digits plus sixteen uppercase
hexadecimal MAC digits. The MAC is domain-separated as `target-slot-v1` and
binds the current revision, target selector, and complete action semantic.
Execution extracts the selector, rebuilds only that actor-target route through
the normal human-client pathfinder, and compares the recomputed slot in
constant time before using the unchanged action/receipt path. Existing
actor-scope `a` slots and their bounded radius-eight goto catalog are unchanged.

For a player scope that spans multiple pages, government bindings are staged
until the caller reaches the final page and the projector has verified the
exact complete semantic capability set. Every city binding is staged the same
way, including production and buy, because citizen completeness is known only
after the final page. An earlier staged descriptor is not executable before
that final validation and remains expired if any continuation is missing,
substituted, duplicated, or rejected. Single-page scopes activate atomically
with their response; independently validated global player actions retain
their normal behavior.

The current player actor catalog contains `phase.end`, research target and
goal, economy-rate, revolution, government-change, active vote-casting,
changeable-setting proposal, own-vote cancellation, and surrender
capabilities. Vote and server-setting governance capabilities remain
available outside the player's action phase when the normal client permits
them; surrender is running-only. An owned
unit catalog contains its current move, attack, and found-city capabilities;
worker activity starts and a cancel capability when applicable; sentry; and
legal self-target fortify, convert, disband, and make-homeless capabilities
when their exact preconditions are present; exact upgrade, rehome, join-city,
trade-route, marketplace, and help-wonder capabilities for bound city sites
when their exact preconditions are present; plus exact board, deboard, embark,
disembark, load, and unload capabilities when their bounded transport
preconditions are present. An owned city catalog contains every currently
legal noncurrent unit/improvement production target, a buy capability when its
current production can be bought and afforded, every exact nonfree worked tile
that can be unworked, every exact workable unworked tile when a specialist can
be consumed, and every positive-source to distinct-usable-target specialist
conversion. Before activating the final scope page, the Python boundary
independently derives that complete citizen and management capability set
from projected state and rejects missing, duplicate, contradictory, or extra
semantics. The management set contains exactly one options and rename
capability, one worklist capability when at least one non-noop invocation is
possible, and one sale capability for each currently sellable installed
improvement before `did_sell`. These city/worker/self-unit/government
capabilities remain scoped-only and do not
enlarge the legacy global catalog. The sidecar validates every scoped row against the
independently parsed current observation before exposing it. At execution time
it re-enumerates that exact actor, requires one exact current native slot
match, and then uses the same one-pending native execution and durable-receipt
semantics as an unfiltered capability; execution never trusts a pinned paging
view.

### Current catalog bounds

Each actor scope has a fixed 2,048-action cap. The native sidecar builds and
validates the complete actor catalog before opening a bounded pinned paging
view; overflow returns `scope_too_large` and exposes no prefix. At most eight
such views are pinned, and HTTP pages contain at most 16 capabilities.

Citizen enumeration itself is finite: at most 91 city tiles plus all ordered
pairs among at most 20 normal specialist types (471 citizen actions before the
city's production/buy actions). The whole observation still fails closed if
its 8,192 combined row/action bound is exceeded; it never exposes a partial
citizen catalog.

The base observation and unfiltered legal-action route still use the legacy
global snapshot path: its action catalog is capped at 2,048, and rows plus
actions must fit the 8,192-entry snapshot bound. An actor filter therefore
does not yet make a seat whose base snapshot already exceeds those limits
readable. Exceeding a legacy bound invalidates the whole observation rather
than truncating it. Removing that safe, fail-closed limitation requires a
future scoped state transport, not weaker validation of the current snapshot.

## Command and barrier semantics

The initial v2 batch contains exactly one command. This keeps action failure,
revision advancement, model attribution, and replay deterministic while the
sidecar is proven. A later protocol version may add explicitly transactional
or ordered multi-command batches; v2 must not silently reinterpret them.

```json
{
  "schema_version": 2,
  "control_protocol": "full-control-v2",
  "game_id": "game_example",
  "agent_id": "agent_example",
  "batch_id": "batch_opaque",
  "state_revision": {"turn": 27, "revision": 4, "state_token": "state_opaque-token"},
  "commands": [{"action_id": "act_opaque", "arguments": {}}]
}
```

The client supplies only an opaque `batch_id`, never a request hash. After
strict validation, the server derives a private hash from its own normalized
Python representation. It durably reserves that agent-scoped batch ID before
native dispatch. Reusing a `batch_id` with the same derived hash returns the
stored receipt with `idempotent: true`; a different derived hash is
`conflict`. This is an internal server rule, not a cross-language
canonical-JSON requirement.

Receipts carry the same game, agent, and batch identities, an `idempotent`
boolean, a state revision, and an `observation` field. `observation` is null for
all non-investigation receipts and is the bounded capture described above only
for an applied investigation at the identical revision. Their strict states are:

- `accepted`: native processing accepted the request, but final outcome is
  still in flight;
- `applied`: processing completed and the action-specific postcondition was
  verified;
- `rejected`: a correlated rejection or unmet postcondition proved the action
  was not applied;
- `ambiguous`: native acceptance or dispatch may have occurred, but the
  service cannot prove applied versus rejected.

`applied`, `rejected`, and `ambiguous` are terminal. Rejected and ambiguous
receipts carry a structured error; ambiguous uses
`action_outcome_ambiguous`, is never replayed, and preserves uncertainty.
Crash recovery converts any durably reserved/accepted incomplete receipt to
terminal ambiguous rather than issuing the command again. The receipt store is
durable, but that does not make a live game recoverable after supervisor
restart.

Native request correlation uses an exhaustive cardinality table for every
current action kind. Normal helpers that first send `SSA_NONE` and then their
action/activity request are tracked as exact two-request groups: move,
attack, found city, worker start/cancel, sentry, fortify, convert, disband,
homeless, upgrade, rehome, join city, establish trade, marketplace, help
wonder, airlift, paradrop, teleport, all six transport operations,
cancel automation, cancel orders, goto, and server-discovered unit actions.
The remaining kinds are exact one-request groups. Missing, extra,
nonconsecutive, or unknown groups fail closed; request-ID deltas are checked
against this table rather than used to infer expected cardinality after
dispatch.

Ambiguity does not by itself imply that the native transport is unusable. A
fully parsed, request-correlated terminal result with
`PROCESSING_BOUNDARY_MISMATCH`, `SEAT_EPOCH_CHANGED`, or
`PROCESSING_TIMEOUT` proves that native pending state was cleared and the IPC
stream remains synchronized. The receipt is still terminal ambiguous and is
never replayed, but the sidecar and game may continue when the same seat and
generation remain valid. Missing acceptance, EOF, timeout without a correlated
result, malformed or out-of-order framing, acceptance callback failure, and
accepted-revision mismatch remain terminal sidecar failures.

Each live supervisor ambiguity also attempts to append one private normalized
diagnostic record beneath the episode's mode-0700 `v2-ambiguity-trace`
directory. Its mode-0600 JSONL contains only the game/agent/batch/seat
identities, timestamp, normalized stage and reason enums, acceptance-known
boolean, and sidecar generation/health state. It never contains native
references, slots, action arguments, observations, controller metadata,
filesystem paths, secrets, or native detail text. The file has a hard size
bound and rotates atomically. Diagnostic failure is sanitized and cannot alter
the already durable public receipt, authorize a replay, or leak through status,
watch, receipt, or other public APIs.

The three research/economy families use this same receipt path. An `applied`
receipt requires an exact native postcondition: the requested current research
or goal is observed (including immediate completion of a newly selected
research target), or all three economic rates equal the requested values.
Native processing without a provable outcome is never reported as applied.

For `player.send_chat`, `applied` has a narrower request-bound meaning: the
normal packet handler received the same request's `E_CHAT_MSG` self echo on the
requested channel with no same-request `E_CHAT_ERROR`. Global and allied echoes
must contain the exact message suffix. A private echo must be exactly
`->{PlayerName} message`, binding the receipt to the same resolved recipient;
the recipient's normal client sees `{SenderName} message`. This is sender-side
proof of the exact normal-player routing result, not a separate remote-delivery
acknowledgement. The capability never invokes the server console or direct
connection messaging. The `chat` state section contains only the plain text
visible to the normal client, with sender, channel, event, turn/phase, self
attribution, and explicit truncation metadata.

City-production, management, citizen, and worker-activity receipts use the same exact rule. Here,
`applied` means that the immediate command effect was proved inside the native
request-processing boundary; it never claims that a multi-turn build or tile
improvement has finished:

- `city.set_production` requires the same city incarnation and private lifetime
  plus the exact requested production universal to be current.
- `city.buy_production` requires the same city incarnation and private
  lifetime, the exact current production universal, `did_buy` to change from
  false to true, shields to reach the production cost and increase, a positive
  cached buy cost, and gold to decrease by exactly that cached cost.
- `city.assign_citizen` work requires the same city incarnation and private
  lifetime, the exact tile to change from unworked to worked, total specialists
  to fall by one, and the bound source-specialist count to fall by one.
- `city.assign_citizen` unwork requires the same city incarnation and private
  lifetime, the exact tile to change from worked to unworked, total specialists
  to rise by one, and the default-specialist count to rise by one.
- `city.set_specialist` requires the same city incarnation and private
  lifetime, unchanged total specialists, the bound source count to fall by
  one, and the distinct bound target count to rise by one.
- `city.set_worklist` requires the same city incarnation and private lifetime,
  a distinct processing-start worklist, and an exact ordered echo of the
  requested list at the completed boundary.
- `city.set_options` requires the same city incarnation and private lifetime,
  a distinct processing-start option state, and exact requested option and
  worklist-cancellation bitvectors at the completed boundary.
- `city.rename` requires the same city incarnation and private lifetime, a
  distinct processing-start name, and an exact requested-name echo. A server
  uniqueness rewrite is therefore rejected rather than guessed as success.
- `city.sell_improvement` requires the same city incarnation and private
  lifetime, the exact improvement to transition present to absent, and
  `did_sell` to transition false to true. The universal success gate does not
  require an exact gold delta because synchronous ruleset Lua can also change
  gold; Classic end-to-end coverage separately checks the advertised sale
  price against the observed gold increase.
- Every unit-order receipt first requires the exact nonzero internal client
  lifetime bound when the capability was enumerated to be present at processing
  start. This lifetime identity is never serialized into public state or action
  rows. Nonconsuming orders also require that exact lifetime at finish.
- `unit.start_activity` then requires the exact activity plus concrete extra
  target (or no extra for an untargeted activity) to be installed.
- `unit.cancel_activity` then requires exact idle activity and no activity
  target.
- `unit.sentry` then requires exact sentry activity and no activity target.
- `unit.auto_work` and `unit.auto_explore` are published only from a clean
  manual baseline: controller `none`, idle activity with no target, no queued
  orders, and no goto plan. Mode switching requires `unit.cancel_automation`
  first, and all overlapping manual unit capabilities are suppressed while a
  native controller is active.
- Auto-work requires the same exact unit lifetime with the autoworker
  controller at the completed boundary. With exact request boundaries, the
  exact clean baseline, and the same lifetime still present, a different final
  controller is a clean rejection; boundary or lifetime uncertainty remains
  ambiguous. Auto-explore may move or complete during its request, so a
  synchronous full-unit packet latch can instead prove that the same seat
  epoch, unit incarnation, private lifetime, and request installed both the
  auto-explore controller and Explore activity. Short packets and mismatched
  request, seat, incarnation, or lifetime values cannot satisfy the latch.
- `unit.cancel_automation` uses the normal client helper's exact two-request
  group. Baseline capture occurs at processing-start for the first consecutive
  request and verification only after processing-finished for the second.
  Applied requires the same exact unit lifetime with controller `none`, idle
  activity, and no activity target. Missing, nonconsecutive, partial, or
  inconclusive groups are ambiguous because the first request may already have
  changed control; they are never reported as a clean rejection.
- `unit.cancel_orders` is published only for an exact owned lifetime with
  manual controller, idle activity, no activity target, and queued orders.
  The private goto destination may be present for a direct goto or absent for
  a route inherited from a city rally point. It uses
  `request_orders_cleared()` as the normal
  client does: consecutive `SSA_NONE` and empty `UNIT_ORDERS` requests.
  Processing-start captures the exact source tile and private route baseline;
  applied requires the same incarnation and lifetime with both `has_orders`
  false and `goto_tile` null after the second request. Any partial, missing,
  nonconsecutive, lifetime-changing, or otherwise inconclusive group is
  ambiguous because either request may already have cleared the route.
  The real HTTP end-to-end case creates the queued route through public
  `unit.goto`, then clears it through this same product boundary without save
  surgery or a test-only order backdoor.
- `unit.goto` is a bounded foundation. It is published only from an exact
  owned, manual, idle, untransported, cargo-free lifetime with no current
  orders or goto plan. One shared normal-client pathfinder evaluates a radius
  eight neighborhood, sorted by real map distance then native tile index, and
  retains at most 64 reachable visible or remembered destinations. A known
  non-allied city or unit may convert the final step to the native action-move
  order. The private source, destination, full order count and digest,
  action-move flag, and path signature are bound, rematerialized immediately
  before dispatch, and the exact frozen packet is sent. Only an opaque tile ID
  and known coordinates cross the public boundary. `unit.goto_and_perform`
  similarly appends one frozen permitted native final action and binds its
  exact city lifecycle or visible stack signature. Queued unit-target actions
  are excluded because the native order does not encode a unit ID.
  `unit.connect_route`
  uses the native road/irrigation pathfinder and freezes its construction extra
  plus every interleaved order. The server echoes the exact installed list and
  destination before execution can consume it. Applied requires the
  request-correlated count, full-list digest, destination, repeat, and vigilant
  proof; destination-only completion never proves these three route families.
  Missing, partial, nonconsecutive, replaced, wrong-destination, or otherwise
  inconclusive groups are ambiguous once any request escapes.
- Arbitrary remembered/visible tile target-on-demand is landed through the
  exact actor-plus-target legal-actions query. The bounded actor catalog above
  remains the fallback discovery surface. The same query installs a city
  rally route for an exact city and target with caller-selected persistence.
  Its public city state exposes only active/persistent/vigilant flags, an order
  count, and an opaque plan ID. Replacement binds and revalidates the complete
  private source, production, and route plan immediately before one forced
  normal-client request; clearing is offered only for an active plan and
  verifies the canonical inactive state. Ordered waypoint goto and closed
  patrol routes are available through `unit.set_route`; independently editing
  repeat or vigilant flags remains deferred.
- `unit.attack_route` uses the normal client's dedicated attack-path
  pathfinder settings and freezes every step as an action move. The exact
  immutable order list is sent through the same two-request route boundary.
  Applied can be proved by an exact remaining suffix, arrival at the frozen
  destination, or a request-correlated active decision on one of the frozen
  action-move tiles.
- `unit.fortify` then requires no activity target and either fortifying or
  already-completed fortified activity.
- `unit.convert` then requires either the exact conversion activity with no
  activity target or the exact bound conversion type already installed. A
  same-type baseline cannot satisfy this proof.
- `unit.disband` requires that exact lifetime at processing start and the unit
  ID to be absent at finish. A unit already absent at processing start cannot
  apply; any same-ID replacement remains present and fails the receipt. The
  refresh that first observes disappearance emits the old public incarnation's
  tombstone.
- `unit.homeless` then requires a nonzero home-city baseline and the home city
  to become empty.
- `unit.upgrade` requires the same exact unit incarnation and private lifetime
  to remain present while its type changes from a distinct baseline to the
  exact opaque successor bound by the capability.
- `unit.rehome` requires the same exact unit incarnation and private lifetime
  to remain present while its home changes from a distinct baseline to the
  exact bound destination city lifetime.
- `unit.join_city` requires the exact actor to disappear while the exact bound
  destination city lifetime remains and its size increases by the actor's
  exact ruleset population value.
- `unit.establish_trade` requires the exact actor to disappear, both exact
  source and destination city lifetimes to remain, and a previously absent
  trade route between them to become present.
- `unit.marketplace` requires the exact actor to disappear while both its
  exact source-home-city lifetime and exact destination-city lifetime remain.
- `unit.help_wonder` requires the exact actor to disappear and the exact
  destination city lifetime to remain. When city internals are visible, its
  shield stock must increase by the actor's exact help value; for a foreign
  visible or remembered city, actor consumption plus destination lifetime is
  the maximum fog-safe proof.
- For all four consuming city-target actions, complete semantic proof plus
  exact actor absence is applied. If the exact actor is still present after
  exact request boundaries, the action is cleanly rejected. Actor absence with
  any source/destination lifetime or semantic mismatch is ambiguous, never
  guessed as rejection or success.
- `unit.airlift` binds the actor plus exact source and destination city
  incarnations and nonzero private lifetimes. Applied requires both cities to
  retain their exact baseline tiles and lifetimes while that exact actor
  lifetime moves from the source-city tile to the destination-city tile.
- `unit.paradrop` and `unit.teleport` require the exact actor lifetime to move
  from a distinct baseline tile to the exact revision-bound target tile.
  Visible and remembered targets preserve the normal client's probability;
  unknown targets bind only an opaque tile identity and public unknown range.
  Paradrop additionally requires `paradropped` to transition from false to
  true; an already-paradropped baseline cannot satisfy the proof.
- The Enter variants are exposed, but disappearance of the actor at the
  destination never satisfies the current relocation postcondition and is
  never reported as applied. A correlated unmet postcondition is rejected;
  uncertain processing follows the general terminal-ambiguous rule above.
  Exact lethal/consuming landing acknowledgement is deferred.
- Transport receipts bind the owned actor, visible unit target when present,
  current transporter context when present, and the complete caller-visible
  component signature to exact nonzero lifetimes at processing start and
  finish. Board/embark/load require the exact new bidirectional parent link;
  a switch also requires the old transporter lifetime and stationary tile.
  Deboard/unload require only the exact old direct link to disappear, so a
  transport-capable cargo keeps its recursive descendants. Disembark moves
  that whole visible cargo subtree to the exact requested tile while the old
  transporter component remains on its baseline tile. Any extra/missing link,
  capacity or nesting change, replacement, relation/contact change,
  unresolved cache state, or mismatched tile/lifetime cannot produce an
  applied receipt.

The catalog omits a same-activity restart, so switching a worker to another
target of that activity requires a separately acknowledged cancel first. If
any postcondition cannot be proved, the service reports rejected or ambiguous
according to the processing evidence and never automatically replays the
command.

Work/unwork uses Freeciv's normal human client packets. When no governor is
active this matches interactive client semantics. CMA configuration and clear
run through the native client's synchronous CMA API, which may issue zero or
many internal city requests. The sidecar therefore gives them a dedicated
local correlation ID, keeps acceptance/result frames contiguous, and proves
the exact local city lifetime and CMA parameter after the call. A mismatched
postcondition after any request escaped is reported as processing-boundary
ambiguous, never replayed or guessed as a clean rejection.

Government receipts use the same normal client request-processing boundary.
For `government.revolution`, `applied` requires both current and selected
government to become the during-revolution government and that pair to differ
from the baseline. For `government.change`, `applied` requires either the
during-revolution government with the requested target selected, or the
requested government already current with the target cleared when enactment
raced the acknowledgement. No other intermediate state is accepted as proof.

Freeciv turns commonly require many commands. The harness repeats
state/legal-action/one-command cycles until it chooses the separate
`phase.end` capability.

### Sequential PLAYER phases and timing

Full-control-v2 games are configured with Freeciv `phasemode PLAYER`. A turn
therefore contains sequential player phases, not a simultaneous multiplayer
submission barrier. Every sidecar reports native evidence for turn, phase,
reported phase count, mode, whether its seat is active/alive/done, and whether Freeciv
currently permits phase end. The supervisor reconciles all exact-generation
sidecars and requires `players_alternate` consensus with at most one active
seat. Contradiction, regression, or stalled phase-end reconciliation fails
closed.

The authoritative phase key is exactly `(turn, phase)`. Native `phase_count`
is advisory telemetry because clients can briefly report different counts
during roster changes; count skew neither resets the active-phase deadline nor
releases or replaces an end claim. Only a real turn/phase advance resets phase
authority.

Only the active seat receives action capabilities and only that seat can end
the phase. Inactive seats expose their scoped state but no legal actions. A
single `phase.end` capability is present only when the active, alive,
not-already-done client reports that Freeciv can end the phase.

The deadline covers the **whole active player phase**, including every
state/legal/one-command cycle up to `phase.end`; it is not reset per command.
It starts only after all seat evidence agrees and the active seat is ready.
Named timing modes are:

| Mode | Active-phase deadline |
| --- | --- |
| `default` | 180 seconds |
| `blitz` | 60 seconds |
| `infinite` | No deadline (`null`) |

The timeout auto-end path is present in the current source: when a finite
named-mode deadline expires, the supervisor resolves the current enumerated
`phase.end` capability and submits it through the same one-command durable
receipt path with timeout source attribution. Infinite mode never auto-ends.
Ambiguous or nonreconciling end outcomes remain fail-closed. An independent
300-second native-progress watchdog covers a coherent but stuck native,
not-ready, or inactive-done phase. It remains enabled in infinite model timing,
does not choose an action, and is separate from the model deadline. Terminal,
cancellation, and server-exit paths clear actionable evidence and claims while
retaining the last authoritative turn for status.

## Errors

Every v2 failure uses a structured envelope with schema version, protocol,
machine code, human message, retryability, JSON details, and the newest known
state revision (or `null`). Current codes include `invalid_request`,
`stale_revision`, `action_expired`, `illegal_action`, `invalid_batch`,
`conflict`, `rate_limited`, `sidecar_unavailable`, `unsupported_protocol`,
`scope_too_large`, `cursor_expired`, `action_outcome_ambiguous`, and
`internal_error`. HTTP status communicates
transport class; the structured code communicates recovery behavior.

An empty legal-action page means the current seat has no actions in this phase
(commonly because another PLAYER phase is active), not that the endpoint is
missing. Action kinds outside the current bounded slice are simply not
promised or enumerated yet.

## Security and evaluation invariants

- Join capability is game-scoped; sidecar capability is game-and-seat scoped.
- The public viewer may use omniscient telemetry, but agent state may not.
- Action IDs expire on revision change and are unusable by another seat.
- Native packets, filesystem paths, tokens, and supervisor internals never
  appear in model-visible state.
- Every command batch receives a server-derived retry hash and a durable
  receipt transition. Native action handles and command bodies do not cross
  the receipt durability boundary. Model-private reasoning is not required
  telemetry.
- Replays must distinguish model commands, server consequences, automatic
  client behavior explicitly requested by the model, and native AI behavior.

## Delivery status and remaining gates

The following foundations are landed:

1. a real headless client sidecar authenticates and owns one human player;
2. authenticated, cursor-paged fog projection and revision-scoped opaque IDs;
3. the bounded native action kinds listed above through normal client
   requests, including bounded research, economy, production purchase/change,
   exact citizen allocation, worker activity control, sentry/idle, bounded
   goto, target-on-demand city rally set/replace/clear, plus exact queued-route
   cancellation including rally-inherited orders,
   self-target, city-target upgrade/rehome/join/trade/marketplace/help-wonder,
   owned-actor transport with visible allied targets, own/allied-destination
   airlift, target-bounded visible/remembered/unknown paradrop and teleport
   unit actions, and safe
   government/revolution control, exact ruleset multiplier targets, and
   spaceship placement/launch, plus exact city CMA governor inspection,
   install/replace, and clear;
4. one-command validation, idempotency, durable receipts, and ambiguous
   no-replay recovery;
5. sidecar phase evidence plus sequential PLAYER-phase reconciliation and
   finite timeout auto-end source paths;
6. additive negotiation that leaves strategic-v1 behavior and routes intact.

Remaining delivery gates include the rest of the completeness matrix,
ruleset-wide legality/native-acknowledgement coverage, deeper state needed by
those families, scoped state transport to replace the fail-closed legacy
global snapshot/action caps, and full end-to-end hardening of phase timing,
cancellation, save/load, reconnect, process loss, and long-game behavior. The
durable receipt store can reopen safely, but the current supervisor does not
recover an active game session after restart.

Until those gates land, call this a playable full-control-v2 vertical slice,
not complete human-equivalent control. Missing action families must remain
absent and fail-closed rather than being guessed, emulated by strategic-v1, or
represented with placeholder descriptors.
