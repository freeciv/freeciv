/**
 * The three *derived-telemetry* payloads of the replay gateway:
 * `replay.json`, `board.json` and `events.json`.
 *
 * These three routes share a property none of the other gateway routes have:
 * **the supervisor cannot answer them**.  `board.json` and `events.json` have
 * no upstream handler at all (`agent_eval/supervisor.py`'s dispatcher has no
 * such suffix), and `replay.json` is the one route where the proxied and the
 * on-disk documents agree key-for-key — the upstream `Game.replay_state`
 * (`agent_eval/supervisor.py:10035-10052`) is a live mirror of the same nine
 * keys `save_replay.replay_from_autosaves` builds
 * (`agent_eval/save_replay.py:1327-1337`).  So one schema per payload is
 * enough here; no `Archive*`/`Upstream*` split is needed.
 *
 * Producers, in the order the bytes are built:
 *
 * | Route | Body built by | Re-shaped by the gateway? |
 * |---|---|---|
 * | `GET /v1/games/{id}/replay.json` | `save_replay.replay_from_autosaves` (`save_replay.py:1197-1337`) | no — emitted **unvalidated** through `_bounded_json` (`replay_gateway.py:1655-1725`) |
 * | `GET /v1/games/{id}/board.json` | `save_replay.board_from_autosave` (`save_replay.py:1340-1391`) | no (`replay_gateway.py:1819-1878`) |
 * | `GET /v1/games/{id}/events.json` | the events loader | **yes** — `_public_events` (`replay_gateway.py:400-440`) defines the wire shape, not the loader |
 *
 * ## Integers are `bigint`
 *
 * Every integer field decodes to `bigint`, per the rule stated in `../canon.ts`:
 * a Python `int` prints as `5` and a Python `float` as `5.0`, and the JS type
 * is the only place that distinction can live once `JSON.parse` has run.  A
 * gateway port that re-canonicalizes one of these payloads and hands a
 * `number` to `canonicalBytes` produces `"turn":1.0` where CPython produced
 * `"turn":1` — a silent digest divergence far from its cause.  See
 * {@link WireInt}.
 *
 * ## Absent is not null
 *
 * Optional-and-nullable fields are modelled `Schema.optional(Schema.NullOr(X))`
 * and **not** `Schema.optionalWith(X, { nullable: true })`, which the gateway
 * dossier's checklist suggests.  `optionalWith({ nullable: true })` decodes an
 * explicit `null` to a *missing key* and re-encodes it as a missing key, so
 * `{"turn":null}` comes back out as `{}`.  That is fatal for a package whose
 * job is to hand back what the server said, byte for byte; it is pinned by a
 * test in `test/gateway/replay.test.ts`.
 *
 * ## What is *not* enforced
 *
 * `_public_event` and `_public_events` truncate strings and cap lists
 * (`kind` to 40 chars, `summary` to 240, each actor to 80, `actors` to 8,
 * warning `message` to 200, `event_warnings` to 100).  Those caps are exported
 * as constants — a port emitting these payloads must apply them — but they are
 * deliberately **not** schema refinements: they are presentation limits that a
 * later gateway may raise, and a consumer that rejected a whole payload
 * because one summary grew to 241 characters would be worse than useless.
 * Semantic bounds that *define* a field's meaning (a weight outside 1-100, a
 * negative turn) are enforced, because a row violating one of those did not
 * come from `_public_event` at all.
 *
 * @module
 */

import { Option, Schema } from 'effect';
import { GameId } from '../ids.ts';
import { JsonObject } from '../json.ts';
import { WireInt, WireNonNegativeInt, WireNumber } from '../numeric.ts';
import { GATEWAY_PROBLEM_MESSAGES } from './problem.ts';
import {
  decodeTolerant,
  encodeTolerant,
  type TolerantDecoder,
  type TolerantEncoder,
} from '../tolerant.ts';

// ---------------------------------------------------------------------------
// Numbers
// ---------------------------------------------------------------------------

/**
 * The int/float spelling doctrine — {@link WireInt} (a Python `int`, decoded
 * to `bigint`), {@link WireNonNegativeInt} (one `_public_int` clamps to `>= 0`)
 * and {@link WireNumber} (a field that is either an int or a float) — lives in
 * `../numeric.ts`.
 *
 * This module used to declare its own copies and say so: *"`src/gateway/games.ts`
 * declares the same schema independently, reached by the same reasoning; the
 * two must be merged into one shared module rather than allowed to drift."*
 * That merge is `../numeric.ts`, and it took `./manifest.ts`'s third copy with
 * it.  `WireFloat` came along too, unused here but part of the same doctrine.
 */

// ---------------------------------------------------------------------------
// Shared sub-objects
// ---------------------------------------------------------------------------

/**
 * One telemetry warning: `{turn, message}`.
 *
 * Shared by `replay.json`'s `replay_warnings` (`save_replay.py:1309-1320`,
 * deduped, sorted with `turn is None` last, capped to the **last** 100) and by
 * `events.json`'s `event_warnings` (`replay_gateway.py:429-439`, `message`
 * truncated to 200, capped to the **first** 100).
 *
 * `turn` is optional *and* nullable: the events publicizer always emits the
 * key and nulls a non-int turn, while a replay warning may omit it entirely.
 */
export const ReplayWarning = Schema.Struct({
  turn: Schema.optional(Schema.NullOr(WireInt)),
  message: Schema.String,
}).annotations({ identifier: 'ReplayWarning' });
/** One telemetry warning. */
export type ReplayWarning = typeof ReplayWarning.Type;

/** Decode one {@link ReplayWarning}. */
export const decodeReplayWarning: TolerantDecoder<ReplayWarning> = decodeTolerant(
  ReplayWarning,
  'ReplayWarning',
);

// ---------------------------------------------------------------------------
// replay.json
// ---------------------------------------------------------------------------

/**
 * One technology in the ruleset catalog (`save_replay.py:322-366`).
 *
 * `requires` and `depth` are added only for the bundled `classic` ruleset
 * (`save_replay.py:355-364`); every other ruleset — and every catalog cached
 * before that code landed — omits both keys under the *same*
 * `schema_version: 1`.  Both shapes are in the corpus.
 */
export const Technology = Schema.Struct({
  id: WireNonNegativeInt,
  rule_name: Schema.String,
  name: Schema.String,
  cost_base: WireNumber,
  requires: Schema.optional(Schema.Array(WireNonNegativeInt)),
  depth: Schema.optional(WireNonNegativeInt),
}).annotations({ identifier: 'Technology' });
/** One technology in the ruleset catalog. */
export type Technology = typeof Technology.Type;

/**
 * The technology catalog embedded in a replay page (`save_replay.py:334`,
 * `:365`).
 *
 * `schema_version` is `1` on every catalog ever written, but it is typed as an
 * open integer rather than `Schema.Literal(1)`: it is not a discriminator yet,
 * and a bump must not turn every consumer into a hard failure.
 */
export const TechnologyCatalog = Schema.Struct({
  schema_version: WireNonNegativeInt,
  technologies: Schema.Array(Technology),
}).annotations({ identifier: 'TechnologyCatalog' });
/** The technology catalog embedded in a replay page. */
export type TechnologyCatalog = typeof TechnologyCatalog.Type;

/** Decode a {@link TechnologyCatalog} — also the on-disk `replay-catalog.json`. */
export const decodeTechnologyCatalog: TolerantDecoder<TechnologyCatalog> = decodeTolerant(
  TechnologyCatalog,
  'TechnologyCatalog',
);

/** Re-encode a {@link TechnologyCatalog}, unknown fields included. */
export const encodeTechnologyCatalog: TolerantEncoder<
  TechnologyCatalog,
  typeof TechnologyCatalog.Encoded
> = encodeTolerant(TechnologyCatalog, 'TechnologyCatalog');

/**
 * What a player is researching (`save_replay.py:749-754`).
 *
 * `tech_id` is `null` when the current research name is absent or one of the
 * pseudo-technologies `A_NONE` / `A_UNSET` / `A_FUTURE`; `cost` is hardcoded
 * `0` and carries no information.
 */
export const ResearchState = Schema.Struct({
  tech_id: Schema.NullOr(WireNonNegativeInt),
  name: Schema.String,
  bulbs: WireInt,
  cost: WireInt,
}).annotations({ identifier: 'ResearchState' });
/** What a player is researching. */
export type ResearchState = typeof ResearchState.Type;

/** Decode one {@link ResearchState}. */
export const decodeResearchState: TolerantDecoder<ResearchState> = decodeTolerant(
  ResearchState,
  'ResearchState',
);

/**
 * One player inside a replay snapshot: the save-file parse
 * (`save_replay.py:726-756`) plus the identity graft `_enrich_snapshot`
 * applies at response time (`save_replay.py:1141-1194`).
 *
 * Three groups of fields, and the difference matters:
 *
 * - **Parsed and cached.**  Everything from `seat_id` through `team_no` is
 *   written into the replay cache by the version of the parser that ran.
 *   `known_tech_names` and `team_no` are therefore *optional*: they are always
 *   emitted today, but a cache written before they were added has neither, and
 *   the corpus contains exactly such a page
 *   (`live/gateway-replay-running-limit5.json`) next to a current one.  The
 *   viewer's `ReplayPlayer` declares neither key at all.
 * - **Grafted per response.**  `seat_id`, `place`, `player_color`,
 *   `controller_label`, `controller_type`, `model` and `scored` are
 *   overwritten by `_enrich_snapshot` on every request, so they are always
 *   present — but each of the identity fields is `identity.get(...)`, i.e.
 *   nullable, and an unmatched player gets the fixed
 *   `dynamic-player-{id}` / `"Freeciv dynamic faction"` / `"dynamic"` /
 *   `place: null` / `model: null` / `scored: false` treatment.
 * - **`population` is a duplicate of `citizens`** (`save_replay.py:735-736`),
 *   not an independent statistic.
 *
 * `ai_difficulty` is declared optional-nullable because the viewer's type
 * declares it (`agent_eval/viewer/src/types.ts:169`); no producer path emits
 * it on a replay player today.
 */
export const ReplayPlayer = Schema.Struct({
  seat_id: Schema.String,
  place: Schema.NullOr(WireInt),
  player_id: WireNonNegativeInt,
  player_name: Schema.String,
  player_color: Schema.NullOr(Schema.String),
  controller_label: Schema.NullOr(Schema.String),
  controller_type: Schema.NullOr(Schema.String),
  model: Schema.NullOr(Schema.String),
  ai_difficulty: Schema.optional(Schema.NullOr(Schema.String)),
  nation: Schema.String,
  government: Schema.String,
  alive: Schema.Boolean,
  score: WireInt,
  cities: WireInt,
  citizens: WireInt,
  population: WireInt,
  units: WireInt,
  gold: WireInt,
  culture: WireInt,
  known_tech_ids: Schema.Array(WireNonNegativeInt),
  known_tech_names: Schema.optional(Schema.Array(Schema.String)),
  gained_tech_ids: Schema.Array(WireNonNegativeInt),
  lost_tech_ids: Schema.Array(WireNonNegativeInt),
  research: ResearchState,
  future_techs: WireInt,
  team_no: Schema.optional(Schema.NullOr(WireInt)),
  scored: Schema.Boolean,
}).annotations({ identifier: 'ReplayPlayer' });
/** One player inside a replay snapshot. */
export type ReplayPlayer = typeof ReplayPlayer.Type;

/**
 * One turn of the replay (`save_replay.py:757-763`).
 *
 * `year` is the in-game year and is routinely **negative** (`-4000` on turn 1),
 * which is why it is a plain {@link WireInt} rather than a non-negative one.
 * Players are sorted `(place is None, place ?? player_id, player_name)`.
 */
export const ReplaySnapshot = Schema.Struct({
  schema_version: WireNonNegativeInt,
  game_id: GameId,
  turn: WireNonNegativeInt,
  year: WireInt,
  players: Schema.Array(ReplayPlayer),
}).annotations({ identifier: 'ReplaySnapshot' });
/** One turn of the replay. */
export type ReplaySnapshot = typeof ReplaySnapshot.Type;

/** Decode one {@link ReplaySnapshot}. */
export const decodeReplaySnapshot: TolerantDecoder<ReplaySnapshot> = decodeTolerant(
  ReplaySnapshot,
  'ReplaySnapshot',
);

/**
 * `GET /v1/games/{id}/replay.json` — one page of snapshots plus the cursor
 * for the next (`save_replay.py:1327-1337`, mirrored upstream at
 * `supervisor.py:10042-10052`).
 *
 * Paging, precisely:
 *
 * - `next_after_turn` is the last returned turn, or the request's `after_turn`
 *   when the page is empty — and is bumped to the last *probed* turn when the
 *   scan was cut short, so a client can step over a window of corrupt saves
 *   instead of requesting it forever (`save_replay.py:1321-1326`).
 * - `has_more` is `len(forward) > limit or scan_limited`: it can be `true`
 *   with a *full* page and also `true` because the scan gave up, not because
 *   more turns exist.
 * - `complete` is whether the run was terminal *at request time*, not whether
 *   the page is the last one.
 *
 * `catalog` is **nullable**, not merely optional — the viewer types it
 * `catalog?: TechnologyCatalog` (`viewer/src/types.ts:205`) and a strict
 * `Schema.optional` port of that type would reject the wire's `null`.
 *
 * `warnings` is the viewer's legacy alias (`viewer/src/types.ts:211`, read at
 * `viewer/src/api.ts:128`).  Neither producer has ever emitted it — the
 * supervisor renames `replay["warnings"]` to `replay_warnings` on the way out
 * (`supervisor.py:10051`) — so it is accepted on decode and never required.
 */
export const ReplayResponse = Schema.Struct({
  schema_version: WireNonNegativeInt,
  game_id: GameId,
  available: Schema.Boolean,
  catalog: Schema.NullOr(TechnologyCatalog),
  snapshots: Schema.Array(ReplaySnapshot),
  next_after_turn: WireNonNegativeInt,
  has_more: Schema.Boolean,
  complete: Schema.Boolean,
  replay_warnings: Schema.Array(ReplayWarning),
  warnings: Schema.optional(Schema.Array(ReplayWarning)),
}).annotations({ identifier: 'ReplayResponse' });
/** One page of a game's replay. */
export type ReplayResponse = typeof ReplayResponse.Type;

/** Decode a {@link ReplayResponse}. */
export const decodeReplayResponse: TolerantDecoder<ReplayResponse> = decodeTolerant(
  ReplayResponse,
  'ReplayResponse',
);

/** Re-encode a {@link ReplayResponse}, unknown fields included. */
export const encodeReplayResponse: TolerantEncoder<
  ReplayResponse,
  typeof ReplayResponse.Encoded
> = encodeTolerant(ReplayResponse, 'ReplayResponse');

// ---------------------------------------------------------------------------
// replay.json — the query envelope
// ---------------------------------------------------------------------------

/** `after_turn` when the client sends none (`replay_gateway.py:1566`). */
export const REPLAY_DEFAULT_AFTER_TURN = 0;

/** `limit` when the client sends none (`replay_gateway.py:1567`). */
export const REPLAY_DEFAULT_LIMIT = 250;

/** Smallest accepted `limit` (`replay_gateway.py:1573`). */
export const REPLAY_MIN_LIMIT = 1;

/** Largest accepted `limit`; also the default (`replay_gateway.py:1573`). */
export const REPLAY_MAX_LIMIT = 250;

/**
 * The validated `?after_turn=&limit=` pair (`_replay_query`,
 * `replay_gateway.py:1554-1580`).
 *
 * Plain `number`s, not {@link WireInt}: these are URL text on the way out, not
 * canonical JSON, so no int/float spelling is at stake.
 *
 * Two behaviours a port must keep and this schema deliberately does not model,
 * because they belong to the *string* layer: each parameter may appear at most
 * once and no other parameter may appear at all (400 otherwise), and Python's
 * `int()` accepts spellings JS `Number()` does not — `"+5"`, `" 5 "`, and
 * `"5_0"`, which is **50**.
 */
export const ReplayQuery = Schema.Struct({
  after_turn: Schema.Int.pipe(Schema.nonNegative()),
  limit: Schema.Int.pipe(Schema.between(REPLAY_MIN_LIMIT, REPLAY_MAX_LIMIT)),
}).annotations({ identifier: 'ReplayQuery' });
/** The validated replay paging query. */
export type ReplayQuery = typeof ReplayQuery.Type;

/** Decode a {@link ReplayQuery} from already-parsed values. */
export const decodeReplayQuery: TolerantDecoder<ReplayQuery> = decodeTolerant(
  ReplayQuery,
  'ReplayQuery',
);

/**
 * The canonical query string the gateway forwards upstream —
 * `urlencode({"after_turn": …, "limit": …})` at `replay_gateway.py:1577-1579`.
 * Key order is the dict's, so `after_turn` always comes first, and upstream
 * never sees the client's raw spelling.
 */
export const formatReplayQuery = (query: ReplayQuery): string =>
  `after_turn=${String(query.after_turn)}&limit=${String(query.limit)}`;

/**
 * The query that fetches the next page, or `Option.none()` when the server
 * says there is nothing after this one.
 *
 * Uses `next_after_turn` rather than the last snapshot's turn: the two differ
 * exactly when the scan hit a corrupt window, which is the case where trusting
 * the snapshots would loop forever.
 */
export const nextReplayQuery = (
  response: ReplayResponse,
  limit: number = REPLAY_DEFAULT_LIMIT,
): Option.Option<ReplayQuery> =>
  response.has_more
    ? Option.some({ after_turn: Number(response.next_after_turn), limit })
    : Option.none();

// ---------------------------------------------------------------------------
// board.json
// ---------------------------------------------------------------------------

/** One terrain code and its display name; the catalog is sorted by `code`. */
export const BoardTerrain = Schema.Struct({
  code: Schema.String,
  name: Schema.String,
}).annotations({ identifier: 'BoardTerrain' });
/** One terrain code and its display name. */
export type BoardTerrain = typeof BoardTerrain.Type;

/**
 * One tile extra.  `id` is the extra's index in the save's `extras_vector`,
 * which is also its bit position in {@link BoardResponse}'s `extra_layers`
 * (`save_replay.py:611-614`).
 */
export const BoardExtra = Schema.Struct({
  id: WireNonNegativeInt,
  name: Schema.String,
}).annotations({ identifier: 'BoardExtra' });
/** One tile extra. */
export type BoardExtra = typeof BoardExtra.Type;

/**
 * One city (`save_replay.py:559-567`).  `capital` is truthiness-of-a-string:
 * the save's `capital` column is treated as `true` unless it is absent or one
 * of `"Not"`, `"None"`, `"FALSE"`, `"0"`.  Sorted `(y, x, id)`.
 */
export const BoardCity = Schema.Struct({
  id: WireNonNegativeInt,
  x: WireNonNegativeInt,
  y: WireNonNegativeInt,
  player_id: WireNonNegativeInt,
  name: Schema.String,
  size: WireNonNegativeInt,
  capital: Schema.Boolean,
}).annotations({ identifier: 'BoardCity' });
/** One city on the board. */
export type BoardCity = typeof BoardCity.Type;

/** One unit type within a stack, ranked by `(-count, name)`. */
export const BoardUnitType = Schema.Struct({
  name: Schema.String,
  count: WireNonNegativeInt,
}).annotations({ identifier: 'BoardUnitType' });
/** One unit type within a stack. */
export type BoardUnitType = typeof BoardUnitType.Type;

/**
 * All of one player's units on one tile (`save_replay.py:589-601`).
 *
 * **`types` is truncated to the top three while `count` stays the full stack
 * total**, so `count` routinely exceeds `sum(types[].count)`.  A renderer that
 * derives the total from `types` undercounts every stack of four or more.
 * Stacks are sorted `(y, x, player_id)`.
 */
export const BoardUnitStack = Schema.Struct({
  x: WireNonNegativeInt,
  y: WireNonNegativeInt,
  player_id: WireNonNegativeInt,
  count: WireNonNegativeInt,
  types: Schema.Array(BoardUnitType),
}).annotations({ identifier: 'BoardUnitStack' });
/** All of one player's units on one tile. */
export type BoardUnitStack = typeof BoardUnitStack.Type;

/**
 * One player identity on the board (`save_replay.py:1378-1389`).
 *
 * Grafted onto the cached board *after* it is loaded — `_parse_board` itself
 * ends at `unit_stacks` (`save_replay.py:627-643`) — by projecting ten keys
 * out of the same `_enrich_snapshot` output the replay route uses.  Every key
 * is present; the identity ones are nullable.  `nation` defaults to `""`
 * rather than `null`, and `scored` is `player.get("scored") is True`, so it is
 * a strict boolean even if the snapshot carried something else.
 */
export const BoardPlayer = Schema.Struct({
  player_id: WireNonNegativeInt,
  player_name: Schema.String,
  player_color: Schema.NullOr(Schema.String),
  seat_id: Schema.NullOr(Schema.String),
  place: Schema.NullOr(WireInt),
  controller_label: Schema.NullOr(Schema.String),
  controller_type: Schema.NullOr(Schema.String),
  model: Schema.NullOr(Schema.String),
  nation: Schema.String,
  scored: Schema.Boolean,
}).annotations({ identifier: 'BoardPlayer' });
/** One player identity on the board. */
export type BoardPlayer = typeof BoardPlayer.Type;

/**
 * `GET /v1/games/{id}/board.json?turn=N` — one semantic map snapshot
 * (`save_replay.py:627-643` plus the `players` graft at `:1378-1389`).
 *
 * The row arrays are all `height` long and are three different encodings, none
 * of them interchangeable:
 *
 * - `terrain_rows[y]` — one character per tile, `width` characters, keyed by
 *   {@link BoardTerrain}`.code`.
 * - `altitude_rows[y]` — a **comma-joined string of decimal integers**, not an
 *   array of numbers (`save_replay.py:531`).  Values are clamped to
 *   ±100000 and may be negative.
 * - `owner_rows[y]` — run-length encoded, `"-"` for unowned; the fixture's
 *   `"-:54"` is "54 unowned tiles".
 * - `extra_layers[layer][y]` — a lowercase hex-nibble string, four extras per
 *   layer, bit `i` of nibble `x` meaning extra `layer * 4 + i` on tile `x`.
 *
 * `topology` and `wrap` are free-form save-file settings (`"ISO|HEX"`,
 * `"WRAPX"`) and may be `""`.
 */
export const BoardResponse = Schema.Struct({
  schema_version: WireNonNegativeInt,
  game_id: GameId,
  turn: WireNonNegativeInt,
  width: WireNonNegativeInt,
  height: WireNonNegativeInt,
  topology: Schema.String,
  wrap: Schema.String,
  terrain_catalog: Schema.Array(BoardTerrain),
  terrain_rows: Schema.Array(Schema.String),
  altitude_rows: Schema.Array(Schema.String),
  owner_rows: Schema.Array(Schema.String),
  extras_catalog: Schema.Array(BoardExtra),
  extra_layers: Schema.Array(Schema.Array(Schema.String)),
  cities: Schema.Array(BoardCity),
  unit_stacks: Schema.Array(BoardUnitStack),
  players: Schema.Array(BoardPlayer),
}).annotations({ identifier: 'BoardResponse' });
/** One semantic map snapshot. */
export type BoardResponse = typeof BoardResponse.Type;

/** Decode a {@link BoardResponse}. */
export const decodeBoardResponse: TolerantDecoder<BoardResponse> = decodeTolerant(
  BoardResponse,
  'BoardResponse',
);

/** Re-encode a {@link BoardResponse}, unknown fields included. */
export const encodeBoardResponse: TolerantEncoder<
  BoardResponse,
  typeof BoardResponse.Encoded
> = encodeTolerant(BoardResponse, 'BoardResponse');

/**
 * The validated `?turn=` parameter (`_board_query`,
 * `replay_gateway.py:1799-1817`).
 *
 * **Strictly positive.**  `turn=0` is a 400 (`turn must be greater than
 * zero`) even though `board_from_autosave` itself accepts `turn >= 0`
 * (`save_replay.py:1357`) — the route is the stricter of the two, and the
 * route is what the wire sees.  Exactly one `turn` must be present: none, two,
 * or any additional parameter is a 400.
 */
export const BoardQuery = Schema.Struct({
  turn: Schema.Int.pipe(Schema.positive()),
}).annotations({ identifier: 'BoardQuery' });
/** The validated board query. */
export type BoardQuery = typeof BoardQuery.Type;

/** Decode a {@link BoardQuery} from an already-parsed value. */
export const decodeBoardQuery: TolerantDecoder<BoardQuery> = decodeTolerant(
  BoardQuery,
  'BoardQuery',
);

/** The canonical board query string (`replay_gateway.py:1817`). */
export const formatBoardQuery = (query: BoardQuery): string => `turn=${String(query.turn)}`;

// ---------------------------------------------------------------------------
// events.json
// ---------------------------------------------------------------------------

/** `kind` is truncated to 40 characters (`replay_gateway.py:377`). */
export const GAME_EVENT_KIND_MAX_LENGTH = 40;

/** `summary` is truncated to 240 characters (`replay_gateway.py:378`). */
export const GAME_EVENT_SUMMARY_MAX_LENGTH = 240;

/** Each actor is truncated to 80 characters (`replay_gateway.py:384`). */
export const GAME_EVENT_ACTOR_MAX_LENGTH = 80;

/** At most 8 actors survive (`replay_gateway.py:386`). */
export const GAME_EVENT_ACTORS_MAX = 8;

/** The lightest event weight (`replay_gateway.py:370`). */
export const GAME_EVENT_MIN_WEIGHT = 1;

/** The heaviest event weight (`replay_gateway.py:370`). */
export const GAME_EVENT_MAX_WEIGHT = 100;

/** A warning `message` is truncated to 200 characters (`replay_gateway.py:437`). */
export const GAME_EVENT_WARNING_MESSAGE_MAX_LENGTH = 200;

/** At most 100 warnings survive (`replay_gateway.py:439`). */
export const GAME_EVENT_WARNINGS_MAX = 100;

/**
 * One derived match event (`_public_event`, `replay_gateway.py:358-389`).
 *
 * A row is **silently dropped** — not repaired, not reported — unless `turn`
 * is a non-bool `int >= 0`, `kind` and `summary` are non-empty strings, and
 * `weight` is a non-bool `int` in `[1, 100]`.  Those bounds are the only ones
 * enforced here; see the module doc for why the truncation caps are not.
 *
 * `data` is an **unbounded, unsanitized passthrough** of whatever the deriver
 * put there (`data if isinstance(data, dict) else {}`), so it is typed as an
 * open JSON object rather than anything narrower.
 */
export const GameEvent = Schema.Struct({
  turn: WireNonNegativeInt,
  kind: Schema.NonEmptyString,
  summary: Schema.String,
  weight: WireInt.pipe(
    Schema.betweenBigInt(BigInt(GAME_EVENT_MIN_WEIGHT), BigInt(GAME_EVENT_MAX_WEIGHT)),
  ),
  actors: Schema.Array(Schema.String),
  data: JsonObject,
}).annotations({ identifier: 'GameEvent' });
/** One derived match event. */
export type GameEvent = typeof GameEvent.Type;

/** Decode one {@link GameEvent}. */
export const decodeGameEvent: TolerantDecoder<GameEvent> = decodeTolerant(GameEvent, 'GameEvent');

/**
 * `GET /v1/games/{id}/events.json` — the spectator event log
 * (`_public_events`, `replay_gateway.py:400-440`).
 *
 * The only gateway payload the gateway itself reshapes, and it lies in three
 * documented ways:
 *
 * - `schema_version` is **hardcoded `1`** and `game_id` is the **requested**
 *   id; whatever the loader said for either is discarded
 *   (`replay_gateway.py:415-416`).
 * - `total_events` defaults to the count of *surviving* rows
 *   (`replay_gateway.py:420`), so it can legitimately disagree with both
 *   `events.length` and the loader's own total.
 * - `min_included_weight` is recomputed over the already-capped list
 *   (`replay_gateway.py:423-426`): it describes this response, not the log,
 *   and is `0` — not `1` — when `events` is empty.
 *
 * `event_counts` and `omitted_counts` are keyed by event kind through the same
 * 40-character truncation, so two kinds sharing a 40-character prefix
 * **collide into a single count**, last write wins
 * (`_public_counts`, `replay_gateway.py:392-397`).  The maps are therefore not
 * guaranteed to be keyed by any `kind` that actually appears in `events`.
 */
export const GameEventsResponse = Schema.Struct({
  schema_version: WireNonNegativeInt,
  game_id: GameId,
  available: Schema.Boolean,
  events: Schema.Array(GameEvent),
  event_counts: Schema.Record({ key: Schema.String, value: WireNonNegativeInt }),
  total_events: WireNonNegativeInt,
  truncated: Schema.Boolean,
  omitted_counts: Schema.Record({ key: Schema.String, value: WireNonNegativeInt }),
  min_included_weight: WireNonNegativeInt,
  last_turn: WireNonNegativeInt,
  complete: Schema.Boolean,
  event_warnings: Schema.Array(ReplayWarning),
}).annotations({ identifier: 'GameEventsResponse' });
/** The spectator event log for one game. */
export type GameEventsResponse = typeof GameEventsResponse.Type;

/** Decode a {@link GameEventsResponse}. */
export const decodeGameEventsResponse: TolerantDecoder<GameEventsResponse> = decodeTolerant(
  GameEventsResponse,
  'GameEventsResponse',
);

/** Re-encode a {@link GameEventsResponse}, unknown fields included. */
export const encodeGameEventsResponse: TolerantEncoder<
  GameEventsResponse,
  typeof GameEventsResponse.Encoded
> = encodeTolerant(GameEventsResponse, 'GameEventsResponse');

// ---------------------------------------------------------------------------
// The problem messages these three routes can produce
// ---------------------------------------------------------------------------

/**
 * Which of the gateway's problem messages each of these three routes can
 * actually produce.
 *
 * The strings themselves live in `./problem.ts` — this is a *view* over that
 * inventory, never a second copy of it, so a message that changes there
 * changes here or fails the build.
 *
 * A game id that fails `GAME_ID_RE` never reaches any of these handlers: the
 * router answers `notFound` (`"not found"`) first
 * (`replay_gateway.py:1988-1993`), and a well-formed id with no run directory
 * gets `gameNotFound` from the manifest reader
 * (`replay_gateway.py:559-569`).  Three different 404 bodies for what a caller
 * experiences as one condition — which is why the captured `replay.json` 404
 * fixture says `"not found"` and not `"game replay artifacts not found"`.
 */
export const REPLAY_ROUTE_PROBLEMS = {
  /** 400 — a repeated or unexpected query parameter. */
  queryShape: GATEWAY_PROBLEM_MESSAGES.replayQueryDuplicates,
  /** 400 — `int()` refused a value. */
  queryNotIntegers: GATEWAY_PROBLEM_MESSAGES.replayQueryNotIntegers,
  /** 400 — a value was in range for `int` but not for the route. */
  queryRange: GATEWAY_PROBLEM_MESSAGES.replayQueryOutOfRange,
  /** 404 — the run exists but has no replay artifacts. */
  notFound: GATEWAY_PROBLEM_MESSAGES.replayArtifactsNotFound,
  /** 503 — the artifacts exist but could not be read. */
  unavailable: GATEWAY_PROBLEM_MESSAGES.replayTelemetryUnavailable,
} as const;

/** The problems `_board` can emit (`replay_gateway.py:1799-1878`). */
export const BOARD_ROUTE_PROBLEMS = {
  /** 400 — zero, two, or foreign query parameters. */
  queryShape: GATEWAY_PROBLEM_MESSAGES.boardQueryTurn,
  /** 400 — `int()` refused the value. */
  queryNotInteger: GATEWAY_PROBLEM_MESSAGES.boardTurnNotInteger,
  /** 400 — `turn <= 0`. */
  queryNotPositive: GATEWAY_PROBLEM_MESSAGES.boardTurnNotPositive,
  /** 404 — no save for that turn. */
  notFound: GATEWAY_PROBLEM_MESSAGES.boardSnapshotNotFound,
  /** 503 — the save exists but could not be read. */
  unavailable: GATEWAY_PROBLEM_MESSAGES.boardSnapshotUnavailable,
} as const;

/**
 * The problems `_events` can emit (`replay_gateway.py:1727-1796`).  Note the
 * route rejects **any** query string, including one it would otherwise
 * understand.
 */
export const EVENTS_ROUTE_PROBLEMS = {
  /** 400 — any query string at all. */
  query: GATEWAY_PROBLEM_MESSAGES.eventsQuery,
  /** 404 — the run exists but has no event artifacts. */
  notFound: GATEWAY_PROBLEM_MESSAGES.eventArtifactsNotFound,
  /** 503 — the artifacts exist but could not be read. */
  unavailable: GATEWAY_PROBLEM_MESSAGES.eventsUnavailable,
} as const;
