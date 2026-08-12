/**
 * The gateway's *game* payloads: the `/v1/games` index, the `GameStatus`
 * served by `/v1/games/{id}` and `/v1/games/{id}/status`, and the two
 * unrelated documents that both answer `/v1/games/{id}/result`.
 *
 * Line numbers cite `agent_eval/replay_gateway.py` (the producer being
 * ported) and `agent_eval/supervisor.py` (the upstream service whose bodies
 * the gateway relays verbatim) at the commit this file was written against.
 * `test/gateway/games.test.ts` re-reads both Python files and fails if a
 * literal drifts, so a stale citation cannot go unnoticed.
 *
 * ## Every payload here has two producers
 *
 * The gateway is a read-only merging proxy (`_games`, `:1600`). When the
 * upstream supervisor answers 2xx the body is relayed **verbatim** — the
 * gateway does not reshape or revalidate it. When upstream answers 404/405 or
 * is unreachable, the gateway *synthesizes* the payload from the on-disk run
 * archive. The two shapes differ in a dozen small ways, and a single response
 * can contain both (the index concatenates live upstream rows with disk rows).
 * Each schema below is therefore the **union** of what the two producers emit,
 * and the field docs say which side contributes each variation.
 *
 * The three presence modes that fall out of that, and how they are spelled:
 *
 * | Wire behaviour | Spelling |
 * |---|---|
 * | always present | `Schema.X` |
 * | present, may be `null` | `Schema.NullOr(X)` |
 * | omitted by one producer, `null` by the other | `Schema.optional(Schema.NullOr(X))` |
 *
 * **Never `Schema.optionalWith(X, { nullable: true })`** even though it looks
 * like the natural fit for the third row. It *maps `null` to `undefined`* on
 * decode, so `{"model": null}` re-encodes as an absent key — silently turning
 * an upstream row into a gateway row. It also moves the field to the end of
 * the key order, breaking `propertyOrder: "original"`. Both are proven in
 * `test/gateway/games.test.ts`.
 *
 * ## Integers are `bigint`
 *
 * Per `src/canon.ts`, `bigint` is a Python `int` and `number` is a Python
 * `float`: canonical output spells `77n` as `77` and `77` as `77.0`. Every
 * field the gateway builds with `_public_int` decodes through {@link WireInt}
 * (`bigint`), and every field it builds with `_public_number` — `created_at`,
 * `finished_at`, `action_timeout_s` — stays a `number`. Feeding a decoded
 * value straight to `canonicalBytes` then reproduces Python's bytes, which is
 * the whole point of the split (dossier T1).
 *
 * @module
 */

import { Option, Schema } from 'effect';
import { CONTROL_PROTOCOLS } from '../control-protocol.ts';
import { GameId, RunStateTolerant } from '../ids.ts';
import { JsonObject, JsonValue } from '../json.ts';
import { WireInt } from '../numeric.ts';
import {
  decodeTolerant,
  encodeTolerant,
  isTolerant,
  type TolerantDecoder,
  type TolerantEncoder,
} from '../tolerant.ts';

// ---------------------------------------------------------------------------
// Numeric primitives
// ---------------------------------------------------------------------------

/**
 * `"schema_version": 1` — hardcoded by every gateway-built payload
 * (`:836`, `:1096`, `:1049`, `:1305`).  It has never been bumped and is not
 * yet a discriminator, but it is always present, so requiring it keeps a
 * non-gateway document from decoding as one.
 */
export const SchemaVersion1 = WireInt.pipe(
  Schema.filter((value) => value === 1n, {
    identifier: 'SchemaVersion1',
    description: 'Gateway payload schema version, always the literal 1',
  }),
);

/**
 * A field one producer omits and the other sends as `null`.
 *
 * See the module doc for why this is not `Schema.optionalWith(..., {
 * nullable: true })`.
 */
const omittedOrNull = <A, I>(
  schema: Schema.Schema<A, I>,
): Schema.optional<Schema.NullOr<Schema.Schema<A, I>>> =>
  Schema.optional(Schema.NullOr(schema));

/**
 * The `timing_mode` / `action_timeout_s` jointness rule, as something a caller
 * can actually run.
 *
 * `_public_timing` (`replay_gateway.py:443-464`) returns `{}` or a two-key
 * dict — it cannot emit one key without the other — and {@link GameRow},
 * {@link GameStatus} and {@link ArchiveResult} all say so in prose.  Prose was
 * *all* they said: both fields are independently optional, so a payload with
 * `timing_mode` and no `action_timeout_s` decoded happily, and a port that
 * *builds* one of these rows had nothing to check itself against.
 *
 * This is deliberately **not** a `Schema.filter`.  House policy is decode
 * tolerance — a half-present pair from some future gateway should still decode,
 * so a consumer can read the other thirty fields — and the corpus test pins
 * that no captured row has ever been half-present.  This is the check a
 * *producer* runs, and the one a consumer runs when it wants the strict answer.
 *
 * Presence, not nullness, is what matters: `action_timeout_s` is legitimately
 * `null` when `timing_mode` is `"infinite"`, and the key is still there.
 *
 * @returns `undefined` when the pair is whole (both keys, or neither).
 */
export const timingPairIssue = (row: {
  readonly timing_mode?: string | null | undefined;
  readonly action_timeout_s?: number | null | undefined;
}): string | undefined => {
  const hasMode = Object.hasOwn(row, 'timing_mode');
  const hasTimeout = Object.hasOwn(row, 'action_timeout_s');
  if (hasMode === hasTimeout) return undefined;
  return hasMode
    ? 'timing_mode is present without action_timeout_s; _public_timing emits both or neither'
    : 'action_timeout_s is present without timing_mode; _public_timing emits both or neither';
};

/** True when `timing_mode` and `action_timeout_s` are both present or both absent. */
export const hasJointTiming = (row: {
  readonly timing_mode?: string | null | undefined;
  readonly action_timeout_s?: number | null | undefined;
}): boolean => timingPairIssue(row) === undefined;

// ---------------------------------------------------------------------------
// Closed vocabularies (`replay_gateway.py:43-70`)
// ---------------------------------------------------------------------------

/**
 * `PUBLIC_CONTROL_PROTOCOLS` — `:52`.  Anything else is *omitted* (`:468`).
 *
 * The upstream name for the vocabulary `../control-protocol.ts` owns; kept as
 * an alias so the citation above still resolves to something in this file.
 */
export const PUBLIC_CONTROL_PROTOCOLS = CONTROL_PROTOCOLS;

/** `PUBLIC_AI_DIFFICULTY_LEVELS` — `:57`. */
export const PUBLIC_AI_DIFFICULTY_LEVELS = [
  'novice',
  'easy',
  'normal',
  'hard',
  'cheating',
] as const;

/** A native-AI level the gateway will publish; anything else becomes `null`. */
export const AiDifficulty = Schema.Literal(...PUBLIC_AI_DIFFICULTY_LEVELS).annotations({
  identifier: 'AiDifficulty',
});
/** A native-AI level the gateway will publish. */
export type AiDifficulty = typeof AiDifficulty.Type;

const AI_DIFFICULTY_SET: ReadonlySet<string> = new Set<string>(PUBLIC_AI_DIFFICULTY_LEVELS);

/**
 * `_public_ai_difficulty` (`:480`) narrows to this set or returns `None`.
 *
 * The *upstream* row does not narrow — it forwards `config["difficulty"]`
 * raw (supervisor `:9592`) — which is why the schemas below type
 * `ai_difficulty` as a plain string and expose this guard separately.
 */
export const isAiDifficulty = (value: string): value is AiDifficulty =>
  AI_DIFFICULTY_SET.has(value);

/** The four timing presets `_public_timing` will publish (`:447`). */
export const PUBLIC_TIMING_MODES = ['default', 'blitz', 'infinite', 'custom'] as const;

/** A timing preset. */
export const TimingMode = Schema.Literal(...PUBLIC_TIMING_MODES).annotations({
  identifier: 'TimingMode',
});
/** A timing preset. */
export type TimingMode = typeof TimingMode.Type;

const TIMING_MODE_SET: ReadonlySet<string> = new Set<string>(PUBLIC_TIMING_MODES);

/** The `mode not in {...}` gate of `_public_timing` (`:447`). */
export const isTimingMode = (value: string): value is TimingMode => TIMING_MODE_SET.has(value);

/**
 * `PUBLIC_SCORE_METRICS` — `:63`. Thirty-one names, sorted here as they are
 * written there.
 *
 * The archive `/result` filters `score.players[].metrics` to exactly these
 * keys (`:1074`); the upstream `/result` relays the scorer's map unfiltered
 * (supervisor `:10054`). {@link ScoreMetrics} therefore accepts any key, and
 * {@link isPublicScoreMetric} is what a port applies when *building* the
 * archive document.
 */
export const PUBLIC_SCORE_METRICS = [
  'bnp',
  'cities',
  'contentpop',
  'corruption',
  'culture',
  'gold',
  'gov',
  'happypop',
  'landarea',
  'literacy',
  'luxrate',
  'mfg',
  'munits',
  'pollution',
  'pop',
  'riots',
  'scirate',
  'score',
  'settledarea',
  'settlers',
  'spaceship',
  'specialists',
  'taxrate',
  'techout',
  'techs',
  'unhappypop',
  'unitsbuilt',
  'unitskilled',
  'unitslost',
  'unitsused',
  'wonders',
] as const;

/** One of the 31 publishable score metrics. */
export const PublicScoreMetric = Schema.Literal(...PUBLIC_SCORE_METRICS).annotations({
  identifier: 'PublicScoreMetric',
});
/** One of the 31 publishable score metrics. */
export type PublicScoreMetric = typeof PublicScoreMetric.Type;

const PUBLIC_SCORE_METRIC_SET: ReadonlySet<string> = new Set<string>(PUBLIC_SCORE_METRICS);

/** The `key in PUBLIC_SCORE_METRICS` filter of `_archive_result` (`:1076`). */
export const isPublicScoreMetric = (key: string): key is PublicScoreMetric =>
  PUBLIC_SCORE_METRIC_SET.has(key);

/**
 * A player's score metrics: an open map of counters.
 *
 * Keys are unconstrained because the upstream document does not filter them;
 * values are `_public_int`-ed on the archive path and raw scorer integers
 * upstream, so both sides are Python ints.
 */
export const ScoreMetrics = Schema.Record({ key: Schema.String, value: WireInt }).annotations({
  identifier: 'ScoreMetrics',
});
/** A player's score metrics. */
export type ScoreMetrics = typeof ScoreMetrics.Type;

// ---------------------------------------------------------------------------
// Fixed strings the gateway emits verbatim
// ---------------------------------------------------------------------------

/**
 * The *only* error text an archive status can carry (`:859`).
 *
 * Any truthy `manifest["error"]` collapses to this sentence, so the real
 * failure message never leaves the machine. A port that forwards the archived
 * text instead is a data leak, not a nicety (dossier T6).
 */
export const ARCHIVE_ERROR_MESSAGE = 'Archived game ended with an error.' as const;

/**
 * `_archive_reasons` (`:602`) replaces an *entire* reason with this string
 * when it contains `/`, `\`, `token`, `password`, `credential` or `secret` —
 * the offending token is not excised, the whole reason is dropped.
 */
export const REDACTED_INVALID_REASON = 'Archived evaluation was marked invalid.' as const;

/** `_public_text(config["objective"], ..., 240)`'s fallback (`:853`). */
export const DEFAULT_OBJECTIVE = 'Maximize final Freeciv civilization score.' as const;

/** `controller` value that switches a place onto the native-AI defaults (`:517`). */
export const NATIVE_CONTROLLER = 'native_classic_ai' as const;

/** Native-place defaults applied by `_public_places` (`:518`). */
export const NATIVE_CONTROLLER_LABEL = 'Freeciv Classic AI' as const;
/** Native-place defaults applied by `_public_places` (`:519`). */
export const NATIVE_CONTROLLER_TYPE = 'native' as const;
/** Native-place defaults applied by `_public_places` (`:520`). */
export const NATIVE_CONTROLLER_MODEL = 'classic' as const;

/** `controller_type` a joined external agent falls back to (`:536`). */
export const EXTERNAL_CONTROLLER_TYPE = 'external' as const;

/** `controller_label` a leaderboard row falls back to when its place has none (`:650`). */
export const UNCLAIMED_CONTROLLER_LABEL = 'Unclaimed agent place' as const;

// ---------------------------------------------------------------------------
// MatchOutcome
// ---------------------------------------------------------------------------

/** Statuses `_archive_score_outcome` can return for a terminal archive (`:720`). */
export const ARCHIVE_OUTCOME_STATUSES = ['invalid', 'tied', 'won'] as const;

/**
 * Statuses `_disk_game_row` writes before the archive overwrites them
 * (`:1136`).  `pending` is reachable — it is what a non-terminal row keeps on
 * the upstream-404/405 branch (`:1650`, dossier T10) — while `complete` and
 * `no_valid_winner` are **dead**: every terminal row's outcome is replaced at
 * `:1262`. They are listed so a decoder tolerates them, not so a port emits
 * them.
 */
export const DISK_ROW_OUTCOME_STATUSES = ['pending', 'complete', 'no_valid_winner'] as const;

/** Statuses the supervisor's `_score_outcome` can return (supervisor `:8982`). */
export const UPSTREAM_OUTCOME_STATUSES = [
  'invalid',
  'leads',
  'pending',
  'tie',
  'tied',
  'won',
] as const;

/** `_as_interrupted`'s synthesized status (`:1228`), and its `state` (`:1227`). */
export const INTERRUPTED_STATUS = 'interrupted' as const;

/** Every outcome status either producer is known to emit. */
export const MATCH_OUTCOME_STATUSES = [
  'complete',
  'interrupted',
  'invalid',
  'leads',
  'no_valid_winner',
  'pending',
  'tie',
  'tied',
  'won',
] as const;

/**
 * The outcome status vocabulary.
 *
 * Unlike `state` — which `_public_text` will pass through from an untrusted
 * manifest, and which is therefore decoded as
 * {@link RunStateTolerant} — every value here is a literal in the gateway's or
 * the supervisor's own source. A status outside this set means the Python side
 * grew a case this build does not model, which is exactly what a parity run
 * should fail on. Use {@link MatchOutcomeStatusTolerant} at boundaries that
 * must survive that instead of failing.
 */
export const MatchOutcomeStatus = Schema.Literal(...MATCH_OUTCOME_STATUSES).annotations({
  identifier: 'MatchOutcomeStatus',
});
/** An outcome status this build understands. */
export type MatchOutcomeStatus = typeof MatchOutcomeStatus.Type;

/** Decode an outcome status, failing on unknown vocabulary. */
export const decodeMatchOutcomeStatus: TolerantDecoder<MatchOutcomeStatus> = decodeTolerant(
  MatchOutcomeStatus,
  'MatchOutcomeStatus',
);

/** A status string outside {@link MATCH_OUTCOME_STATUSES}, kept as a brand. */
export const UnrecognizedOutcomeStatus = Schema.String.pipe(
  Schema.brand('UnrecognizedOutcomeStatus'),
).annotations({ identifier: 'UnrecognizedOutcomeStatus' });
/** A status string outside {@link MATCH_OUTCOME_STATUSES}. */
export type UnrecognizedOutcomeStatus = typeof UnrecognizedOutcomeStatus.Type;

/** A known status, or an unknown one kept as a distinct brand. */
export const MatchOutcomeStatusTolerant = Schema.Union(
  MatchOutcomeStatus,
  UnrecognizedOutcomeStatus,
).annotations({ identifier: 'MatchOutcomeStatusTolerant' });
/** A known status, or an unknown one kept as a distinct brand. */
export type MatchOutcomeStatusTolerant = typeof MatchOutcomeStatusTolerant.Type;

/** Decode any status string, keeping unknown vocabulary rather than failing. */
export const decodeMatchOutcomeStatusTolerant: TolerantDecoder<MatchOutcomeStatusTolerant> =
  decodeTolerant(MatchOutcomeStatusTolerant);

const MATCH_OUTCOME_STATUS_SET: ReadonlySet<string> = new Set<string>(MATCH_OUTCOME_STATUSES);

/** True when a status string is one this build knows by name. */
export const isKnownOutcomeStatus = (status: string): status is MatchOutcomeStatus =>
  MATCH_OUTCOME_STATUS_SET.has(status);

/**
 * The engine's game-over record, read from `victory.json` (`_archive_victory`,
 * `:681`; supervisor `_victory`).
 *
 * `turn` and `year` are `record.get(...)` with **no validation at all**
 * (`:704`) — whatever the file holds reaches the wire, so they are typed as
 * raw JSON rather than integers. `code` is non-empty by construction (`:695`)
 * and `label` falls back to `code` when it is outside `VICTORY_LABELS`
 * (`:667`), so an unknown victory code is a label, never a decode failure.
 */
export const MatchVictory = Schema.Struct({
  code: Schema.String,
  label: Schema.String,
  winners: Schema.Array(Schema.String),
  turn: Schema.optional(JsonValue),
  year: Schema.optional(JsonValue),
}).annotations({ identifier: 'MatchVictory' });
/** The engine's game-over record. */
export type MatchVictory = typeof MatchVictory.Type;

/** Decode a {@link MatchVictory}. */
export const decodeMatchVictory: TolerantDecoder<MatchVictory> = decodeTolerant(
  MatchVictory,
  'MatchVictory',
);

/**
 * How a match ended, as both producers compute it
 * (`_archive_outcome` `:709`; supervisor `_outcome` `:8969`).
 *
 * - `margin` is `0` on a tie and `null` when there is exactly one leaderboard
 *   row (nobody to be ahead of).
 * - `score_turn` is `leaderboard[0].score_turn`, which on the archive path is
 *   `final_turn or None` — so a `final_turn` of `0` arrives as `null` (`:644`).
 * - `victory` is always present and usually `null`; when it is non-null *and*
 *   the status is `won`/`tied`, ` ({victory.label})` is appended to `summary`
 *   (`:716`).
 */
export const MatchOutcome = Schema.Struct({
  status: MatchOutcomeStatus,
  summary: Schema.String,
  leaders: Schema.Array(Schema.String),
  margin: Schema.NullOr(WireInt),
  score_turn: Schema.NullOr(WireInt),
  victory: Schema.NullOr(MatchVictory),
}).annotations({ identifier: 'MatchOutcome' });
/** How a match ended. */
export type MatchOutcome = typeof MatchOutcome.Type;

/** Decode a {@link MatchOutcome}. */
export const decodeMatchOutcome: TolerantDecoder<MatchOutcome> = decodeTolerant(
  MatchOutcome,
  'MatchOutcome',
);

// ---------------------------------------------------------------------------
// GamePlace
// ---------------------------------------------------------------------------

/**
 * One seat of a match, as published on `resolved_places`
 * (`_public_places`, `:492`; supervisor `picker_state` `:9574`).
 *
 * The gateway **omits** a blank `controller_label` / `controller_type` /
 * `model` (`:544`) while the supervisor emits all three as
 * present-and-possibly-`null` (`:9576`). `ai_difficulty` is the sharpest case:
 * the gateway adds it only for a native place whose level resolves (`:524`),
 * so it is *never* `null` there, while every upstream place carries it as
 * `null`. All four therefore need omitted-or-null (dossier T4), and the live
 * corpus contains both spellings of each.
 *
 * Rows that fail `_public_places`' filters — non-dict, `place < 1`, blank
 * `seat_id` or blank `player_name` (`:505`) — are dropped before they reach
 * the wire, which is why those three fields are required and non-null here.
 * `player_color` is *not*: it is `_public_text(..., "", 16)`, and an archived
 * run with no colour publishes `""`.
 */
export const GamePlace = Schema.Struct({
  place: WireInt,
  seat_id: Schema.String,
  player_name: Schema.String,
  player_color: Schema.String,
  /**
   * Free-form: `_public_text(raw["controller"], "agent", 32)` (`:516`). The
   * viewer's `'agent' | 'native_classic_ai'` union is a hopeful narrowing of
   * a field that passes any 32-character string through.
   */
  controller: Schema.String,
  /** Strict `raw.get("joined") is True` (`:517`) — never null, never absent. */
  joined: Schema.Boolean,
  controller_label: omittedOrNull(Schema.String),
  controller_type: omittedOrNull(Schema.String),
  model: omittedOrNull(Schema.String),
  ai_difficulty: omittedOrNull(Schema.String),
  /**
   * Upstream `status` only (supervisor `_public_places`): a SHA-256 of the
   * controller identity. Absent from every gateway-built place and from the
   * upstream *index* row, whose 10-key comprehension drops it (`:9574`).
   */
  controller_fingerprint: omittedOrNull(Schema.String),
  /** Upstream `status` only; `_public_places` reads it (`:538`) but never emits it. */
  controller_metadata: Schema.optional(JsonObject),
}).annotations({ identifier: 'GamePlace' });
/** One seat of a match. */
export type GamePlace = typeof GamePlace.Type;

/** Decode a {@link GamePlace}. */
export const decodeGamePlace: TolerantDecoder<GamePlace> = decodeTolerant(GamePlace, 'GamePlace');

// ---------------------------------------------------------------------------
// LeaderboardEntry
// ---------------------------------------------------------------------------

/**
 * A scored seat (`_archive_leaderboard`, `:623`; supervisor `_leaderboard`
 * `:8912`).
 *
 * Built by joining `report.score.players[]` to the resolved places by
 * `seat_id`, then by `player_name`; a score row that joins to nothing is
 * dropped. Sorted by `(-score, place, seat_id)` and then dense-ranked, so
 * **`rank` is not unique** — a tie gives two rank-1 rows, and the corpus
 * contains one.
 *
 * `model` is `place.get("model")` (`:653`) — i.e. `null` on a row whose place
 * *omitted* the same field. One value, two spellings, in the same response.
 *
 * The two producers disagree on the tail: the gateway adds `ai_difficulty`
 * (nullable) and no `alive`; the supervisor adds `alive` (nullable) and no
 * `ai_difficulty` (`:8914`). Its `score_turn` is also per-player
 * (`last_score_turn`) rather than the game-wide `final_turn`.
 */
export const LeaderboardEntry = Schema.Struct({
  rank: WireInt,
  score: WireInt,
  score_turn: Schema.NullOr(WireInt),
  place: WireInt,
  seat_id: Schema.String,
  player_name: Schema.String,
  player_color: Schema.String,
  controller_label: Schema.String,
  controller_type: Schema.String,
  model: Schema.NullOr(Schema.String),
  /** Gateway rows only. */
  ai_difficulty: omittedOrNull(Schema.String),
  /** Upstream rows only. */
  alive: omittedOrNull(Schema.Boolean),
}).annotations({ identifier: 'LeaderboardEntry' });
/** A scored seat. */
export type LeaderboardEntry = typeof LeaderboardEntry.Type;

/** Decode a {@link LeaderboardEntry}. */
export const decodeLeaderboardEntry: TolerantDecoder<LeaderboardEntry> = decodeTolerant(
  LeaderboardEntry,
  'LeaderboardEntry',
);

// ---------------------------------------------------------------------------
// GET /v1/games
// ---------------------------------------------------------------------------

/**
 * One row of the games index (`_disk_game_row`, `:1123`; supervisor
 * `Game.picker_state`, `:9567`).
 *
 * A single response mixes both producers: on the upstream-2xx branch the
 * gateway emits `[*upstream rows, *disk rows not live]` (`:1612`). The live
 * corpus capture is exactly that — 5 upstream rows followed by 26 disk rows.
 *
 * Divergences this union has to absorb:
 *
 * - `created_at` / `finished_at`: `_public_number` (`:348`) **cannot return
 *   `null`**, so a disk row for a still-running game carries `0.0`, not
 *   `null`; the upstream row carries a real `null` (dossier T2). Both are
 *   floats — never ints — which is why they are `Schema.Number`.
 * - `control_protocol`: omitted by the gateway when unrecognized (`:1158`),
 *   always present upstream (`:9587`). Absent on every strategic-v1 archive
 *   in the corpus, because those manifests predate the key.
 * - `timing_mode` / `action_timeout_s`: a strictly **joint pair**. The gateway
 *   emits both or neither (`_public_timing`, `:443`), and since it hardcodes
 *   `default == 180.0` while full-control-v2 actually runs at `600.0`
 *   (supervisor `:125`), most v2 archives publish neither (dossier T3). The
 *   upstream row always has both. `action_timeout_s` is `null` only for
 *   `timing_mode: "infinite"`.
 * - `benchmark_valid`: `bool | null` here — the manifest value only if it is a
 *   real bool (`:1133`). Contrast {@link GameStatus}, where the archive path
 *   narrows it to a strict `bool` (dossier T5).
 * - `state`: an open string. `_public_text(..., "unknown", 32)` passes through
 *   whatever a manifest says, so this decodes through
 *   {@link RunStateTolerant} and an unfamiliar state is branded, not rejected.
 */
export const GameRow = Schema.Struct({
  game_id: GameId,
  state: RunStateTolerant,
  created_at: Schema.NullOr(Schema.Number),
  finished_at: Schema.NullOr(Schema.Number),
  current_turn: Schema.NullOr(WireInt),
  turns: WireInt,
  benchmark_valid: Schema.NullOr(Schema.Boolean),
  mode: Schema.String,
  control_protocol: omittedOrNull(Schema.String),
  /** Narrowed to {@link PUBLIC_AI_DIFFICULTY_LEVELS} by the gateway, raw upstream. */
  ai_difficulty: Schema.NullOr(Schema.String),
  timing_mode: omittedOrNull(Schema.String),
  action_timeout_s: omittedOrNull(Schema.Number),
  places: WireInt,
  max_agents: WireInt,
  joined_agents: WireInt,
  resolved_places: Schema.Array(GamePlace),
  /** Literal `[]` on a fresh disk row; replaced from the archive for terminal rows (`:1268`). */
  leaderboard: Schema.Array(LeaderboardEntry),
  outcome: MatchOutcome,
  /** Root-relative `/watch/{id}` from the gateway; may carry the service path prefix upstream. */
  watch_path: Schema.String,
}).annotations({ identifier: 'GameRow' });
/** One row of the games index. */
export type GameRow = typeof GameRow.Type;

/** Decode a {@link GameRow}. */
export const decodeGameRow: TolerantDecoder<GameRow> = decodeTolerant(GameRow, 'GameRow');

/** Re-encode a {@link GameRow}, unknown fields included. */
export const encodeGameRow: TolerantEncoder<GameRow, typeof GameRow.Encoded> = encodeTolerant(
  GameRow,
  'GameRow',
);

/**
 * `GET /v1/games` (`_games`, `:1600`).
 *
 * Always `{schema_version: 1, games: [...]}`, sorted by
 * `(created_at, game_id)` descending on the disk path (`:1274`) with the live
 * upstream rows spliced in front. A non-2xx, non-404/405 upstream status
 * answers with {@link GatewayProblem} instead of this shape.
 *
 * One deliberate strictness: upstream rows are relayed with **zero**
 * validation (`:1627` only filters for dicts while collecting live ids), so a
 * non-object element in the upstream array reaches the wire. This schema
 * refuses it rather than modelling `games` as a heterogeneous array — a port
 * that re-serves such a row would be propagating corruption, and the
 * supervisor has no code path that produces one.
 */
export const GamesIndexResponse = Schema.Struct({
  schema_version: SchemaVersion1,
  games: Schema.Array(GameRow),
}).annotations({ identifier: 'GamesIndexResponse' });
/** The `GET /v1/games` body. */
export type GamesIndexResponse = typeof GamesIndexResponse.Type;

/** Decode a {@link GamesIndexResponse}. */
export const decodeGamesIndexResponse: TolerantDecoder<GamesIndexResponse> = decodeTolerant(
  GamesIndexResponse,
  'GamesIndexResponse',
);

/** Re-encode a {@link GamesIndexResponse}, unknown fields included. */
export const encodeGamesIndexResponse: TolerantEncoder<
  GamesIndexResponse,
  typeof GamesIndexResponse.Encoded
> = encodeTolerant(GamesIndexResponse, 'GamesIndexResponse');

// ---------------------------------------------------------------------------
// The interrupted relabel
// ---------------------------------------------------------------------------

/**
 * The summary `_as_interrupted` writes (`:1229`).
 *
 * Note it interpolates the **already-updated** `current_turn`, not the turn
 * that was read from the replay — they differ whenever the manifest recorded a
 * higher turn than the telemetry did.
 */
export const interruptedSummary = (currentTurn: bigint): string =>
  `Interrupted at turn ${String(currentTurn)} without a terminal result; the replay is available.`;

/** The outcome `_as_interrupted` substitutes wholesale (`:1228`). */
export const interruptedOutcome = (currentTurn: bigint): MatchOutcome => ({
  status: INTERRUPTED_STATUS,
  summary: interruptedSummary(currentTurn),
  leaders: [],
  margin: null,
  score_turn: null,
  victory: null,
});

/**
 * A row after `_as_interrupted` (`:1211`) has rewritten it.
 *
 * Two fields change *type*, not just value: `state` narrows to the literal
 * `"interrupted"` — a state that exists nowhere else in the system — and
 * `current_turn` stops being nullable, because the relabel only happens when a
 * turn was found. Everything else is left exactly as it was, including
 * `leaderboard: []`, `benchmark_valid`, and a `finished_at` of `0.0` that
 * still says "never finished".
 */
export const InterruptedGameRow = Schema.Struct({
  ...GameRow.fields,
  state: Schema.Literal(INTERRUPTED_STATUS),
  current_turn: WireInt,
  outcome: Schema.Struct({
    ...MatchOutcome.fields,
    status: Schema.Literal(INTERRUPTED_STATUS),
    leaders: Schema.Array(Schema.String),
    margin: Schema.Null,
    score_turn: Schema.Null,
    victory: Schema.Null,
  }),
}).annotations({ identifier: 'InterruptedGameRow' });
/** A row relabelled as interrupted. */
export type InterruptedGameRow = typeof InterruptedGameRow.Type;

/** Decode an {@link InterruptedGameRow} from its JSON form. */
export const decodeInterruptedGameRow: TolerantDecoder<InterruptedGameRow> = decodeTolerant(
  InterruptedGameRow,
  'InterruptedGameRow',
);

/**
 * Re-encode an {@link InterruptedGameRow} — the gateway *serves* relabelled
 * rows, so a port needs to write one back out, unknown fields and all.
 */
export const encodeInterruptedGameRow: TolerantEncoder<
  InterruptedGameRow,
  typeof InterruptedGameRow.Encoded
> = encodeTolerant(InterruptedGameRow, 'InterruptedGameRow');

/**
 * Check a *decoded* row (bigint turns, not JSON numbers) against the
 * interrupted shape.  {@link decodeInterruptedGameRow} reads the wire form;
 * this reads the form {@link asInterruptedRow} produces.
 */
export const isInterruptedGameRow: (input: unknown) => input is InterruptedGameRow =
  isTolerant(InterruptedGameRow);

const maxBigInt = (left: bigint, right: bigint): bigint => (left > right ? left : right);

/**
 * Apply the interrupted relabel — `_as_interrupted` (`:1211`) as a value.
 *
 * `lastReplayTurn` is the result of `_last_replay_turn` (`:1178`), which reads
 * only the last 64 KiB of `replay.jsonl`, scans backwards and **stops at the
 * first line that parses as JSON** — a parseable line without a positive int
 * `turn` yields `None` rather than making it keep looking. `None` means the
 * row is *dropped*, not kept: a run with no recorded turns is a lobby husk
 * with nothing to watch. That is why this returns an `Option` rather than a
 * row.
 *
 * `current_turn` becomes `max(existing, turn)`, so a manifest that got further
 * than the telemetry wins. Python mutates the row in place; this returns a new
 * one, carrying every other field — including the unknown ones a decode
 * preserved — through the spread.
 *
 * The rewritten row is checked against {@link InterruptedGameRow} rather than
 * asserted into it, so the relabel cannot invent a shape the contract does not
 * allow. For a row that came out of {@link decodeGameRow} that check cannot
 * fail, which is why `None` means "no recorded turn" in every reachable case.
 */
export const asInterruptedRow = (
  row: GameRow,
  lastReplayTurn: Option.Option<bigint>,
): Option.Option<InterruptedGameRow> =>
  Option.flatMap(lastReplayTurn, (turn) => {
    const currentTurn = row.current_turn === null ? turn : maxBigInt(row.current_turn, turn);
    const relabelled = {
      ...row,
      current_turn: currentTurn,
      state: INTERRUPTED_STATUS,
      outcome: interruptedOutcome(currentTurn),
    };
    return isInterruptedGameRow(relabelled) ? Option.some(relabelled) : Option.none();
  });

/** True when a row carries the gateway-only `interrupted` state. */
export const isInterruptedRow = (row: GameRow): boolean => row.state === INTERRUPTED_STATUS;

// ---------------------------------------------------------------------------
// GET /v1/games/{id} and /v1/games/{id}/status
// ---------------------------------------------------------------------------

/** The eight absolute artifact URLs `_archive_urls` spreads into a status (`:809`). */
export const ArchiveUrls = Schema.Struct({
  join_url: Schema.String,
  status_url: Schema.String,
  result_url: Schema.String,
  /** Root-relative `/watch/{id}` unless `--viewer-public-url` is configured (`:817`). */
  watch_url: Schema.String,
  watch_json_url: Schema.String,
  replay_url: Schema.String,
  frames_url: Schema.String,
  video_url: Schema.String,
}).annotations({ identifier: 'ArchiveUrls' });
/** The eight artifact URLs on a status payload. */
export type ArchiveUrls = typeof ArchiveUrls.Type;

/**
 * `GET /v1/games/{id}` and `GET /v1/games/{id}/status`
 * (`_archive_status`, `:827`; supervisor `Game.status`, `:9527`).
 *
 * The disk path requires a **terminal** archive: `_terminal_archive` (`:773`)
 * 404s with `terminal archive not found` when the state is not one of
 * `completed/invalid/failed/cancelled`, when `report.json` is unreadable, or
 * when the report's manifest names a different game. So a non-terminal status
 * can only ever come from upstream.
 *
 * Divergences on top of {@link GameRow}'s:
 *
 * - `created_at` / `finished_at` are **absent entirely** upstream (`Game.status`
 *   never emits them) and always present as floats on the archive path. This
 *   is the one place the pair is optional rather than nullable.
 * - `benchmark_valid` is a strict `bool` on the archive path — it is
 *   `state == "completed" and manifest.benchmark_valid is True` (`:790`), not
 *   the manifest value — and `bool | null` upstream. The viewer's type says
 *   nullable; the archive narrows it (dossier T5).
 * - `error` is the constant {@link ARCHIVE_ERROR_MESSAGE} or `null` on the
 *   archive path (`:859`); upstream it is the real message.
 * - `invalid_reasons` is redacted, de-duplicated and capped at 100 by
 *   `_archive_reasons`; upstream it is the raw list.
 * - `barrier`, `phase` and `phase_events_url` are upstream-only. `phase` is
 *   emitted only for full-control-v2 games (`:9563`) and is left as an opaque
 *   object here — the v2 phase block has its own module.
 */
export const GameStatus = Schema.Struct({
  schema_version: SchemaVersion1,
  game_id: GameId,
  state: RunStateTolerant,
  created_at: Schema.optional(Schema.NullOr(Schema.Number)),
  finished_at: Schema.optional(Schema.NullOr(Schema.Number)),
  benchmark_valid: Schema.NullOr(Schema.Boolean),
  mode: Schema.String,
  control_protocol: omittedOrNull(Schema.String),
  ai_difficulty: Schema.NullOr(Schema.String),
  places: WireInt,
  max_agents: WireInt,
  joined_agents: WireInt,
  turns: WireInt,
  current_turn: Schema.NullOr(WireInt),
  objective: Schema.String,
  timing_mode: omittedOrNull(Schema.String),
  action_timeout_s: omittedOrNull(Schema.Number),
  error: Schema.NullOr(Schema.String),
  invalid_reasons: Schema.Array(Schema.String),
  resolved_places: Schema.Array(GamePlace),
  leaderboard: Schema.Array(LeaderboardEntry),
  outcome: MatchOutcome,
  ...ArchiveUrls.fields,
  /** Upstream only: `_public_barrier()`, an object or `null`. */
  barrier: Schema.optional(JsonValue),
  /** Upstream, full-control-v2 only. */
  phase: Schema.optional(Schema.NullOr(JsonObject)),
  /** Upstream, full-control-v2 only. */
  phase_events_url: Schema.optional(Schema.String),
}).annotations({ identifier: 'GameStatus' });
/** The `/status` body. */
export type GameStatus = typeof GameStatus.Type;

/** Decode a {@link GameStatus}. */
export const decodeGameStatus: TolerantDecoder<GameStatus> = decodeTolerant(
  GameStatus,
  'GameStatus',
);

/** Re-encode a {@link GameStatus}, unknown fields included. */
export const encodeGameStatus: TolerantEncoder<GameStatus, typeof GameStatus.Encoded> =
  encodeTolerant(GameStatus, 'GameStatus');

// ---------------------------------------------------------------------------
// GET /v1/games/{id}/result — two unrelated documents
// ---------------------------------------------------------------------------

/**
 * A scored player as the **archive** `/result` publishes it (`:1085`).
 *
 * Every key is always present, and the set is a deliberate subset: the
 * scorer's `alive`, `added_turn`, `removed_turn`, `last_score_turn` and
 * `controller_fingerprint` are dropped, and `metrics` is filtered to
 * {@link PUBLIC_SCORE_METRICS}. The upstream document keeps all of them, which
 * this schema tolerates as unknown fields.
 */
export const ResultPlayer = Schema.Struct({
  seat_id: Schema.String,
  player_id: WireInt,
  name: Schema.String,
  rank: WireInt,
  score: WireInt,
  metrics: ScoreMetrics,
}).annotations({ identifier: 'ResultPlayer' });
/** A scored player on a `/result` body. */
export type ResultPlayer = typeof ResultPlayer.Type;

/**
 * `score` on a `/result` body (`:1107`).
 *
 * `final_turn` is `_public_int(...)` — so a missing value becomes `0`, not
 * `null` — but the whole field is `null` when `report["score"]` is not a dict
 * at all.
 */
export const ResultScore = Schema.Struct({
  final_turn: Schema.NullOr(WireInt),
  players: Schema.Array(ResultPlayer),
}).annotations({ identifier: 'ResultScore' });
/** The `score` block of a `/result` body. */
export type ResultScore = typeof ResultScore.Type;

/** The five-key, suffix-less URL map unique to `/result` (`:1113`). */
export const ArtifactUrls = Schema.Struct({
  status: Schema.String,
  watch: Schema.String,
  replay: Schema.String,
  frames: Schema.String,
  video: Schema.String,
}).annotations({ identifier: 'ArtifactUrls' });
/** The `artifact_urls` map. */
export type ArtifactUrls = typeof ArtifactUrls.Type;

/**
 * `GET /v1/games/{id}/result` from the **archive** (`_archive_result`,
 * `:1066`) — the curated document, and the one a port must reproduce.
 *
 * It is keyed `artifact_id`, not `game_id`; it carries `schema_version`, which
 * the upstream document does not; and its `artifact_urls` renames the five
 * URLs it keeps. The viewer never fetches this route, so nothing in
 * `viewer/src/types.ts` describes it.
 */
export const ArchiveResult = Schema.Struct({
  schema_version: SchemaVersion1,
  artifact_id: GameId,
  state: RunStateTolerant,
  /** Strict bool, as on {@link GameStatus} (`:790`). */
  benchmark_valid: Schema.Boolean,
  timing_mode: omittedOrNull(Schema.String),
  action_timeout_s: omittedOrNull(Schema.Number),
  invalid_reasons: Schema.Array(Schema.String),
  leaderboard: Schema.Array(LeaderboardEntry),
  outcome: MatchOutcome,
  score: ResultScore,
  artifact_urls: ArtifactUrls,
}).annotations({ identifier: 'ArchiveResult' });
/** The archive `/result` body. */
export type ArchiveResult = typeof ArchiveResult.Type;

/** Decode an {@link ArchiveResult}. */
export const decodeArchiveResult: TolerantDecoder<ArchiveResult> = decodeTolerant(
  ArchiveResult,
  'ArchiveResult',
);

/** Re-encode an {@link ArchiveResult}, unknown fields included. */
export const encodeArchiveResult: TolerantEncoder<ArchiveResult, typeof ArchiveResult.Encoded> =
  encodeTolerant(ArchiveResult, 'ArchiveResult');

/**
 * `GET /v1/games/{id}/result` relayed from **upstream** (supervisor `:10054`)
 * — the whole `report.json` minus `episode`, plus the live fields.
 *
 * This is the largest divergence in the API (dossier T7): where the archive
 * document is ten curated keys, this one carries the entire `manifest`
 * (including `config.seats`), `seat_stats`, `recovery`, and an **unfiltered**
 * `score.players[]`. It also has **no `schema_version`**, which together with
 * the required `manifest` makes the two documents cleanly discriminable — see
 * {@link GameResult}.
 *
 * `manifest`, `seat_stats` and `recovery` are left as opaque JSON: they are
 * `report.json`'s shapes, not the gateway's, and belong to the module that
 * models the run archive. Note the gateway does not read them either — it
 * relays these bytes untouched.
 *
 * For a non-terminal game upstream answers 409 `game result is not ready`,
 * which the gateway relays as HTTP 409
 * `{"error": "upstream returned HTTP 409"}` — a `GatewayProblem` from
 * `./problem.ts`.
 */
export const UpstreamResult = Schema.Struct({
  artifact_id: GameId,
  state: RunStateTolerant,
  benchmark_valid: Schema.NullOr(Schema.Boolean),
  invalid_reasons: Schema.Array(Schema.String),
  leaderboard: Schema.Array(LeaderboardEntry),
  outcome: MatchOutcome,
  artifact_urls: ArtifactUrls,
  /** The discriminator: present here, absent from every archive result. */
  manifest: JsonObject,
  score: Schema.optional(ResultScore),
  seat_stats: Schema.optional(JsonObject),
  recovery: Schema.optional(JsonObject),
}).annotations({ identifier: 'UpstreamResult' });
/** The upstream `/result` body. */
export type UpstreamResult = typeof UpstreamResult.Type;

/** Decode an {@link UpstreamResult}. */
export const decodeUpstreamResult: TolerantDecoder<UpstreamResult> = decodeTolerant(
  UpstreamResult,
  'UpstreamResult',
);

/** Re-encode an {@link UpstreamResult}, unknown fields included. */
export const encodeUpstreamResult: TolerantEncoder<UpstreamResult, typeof UpstreamResult.Encoded> =
  encodeTolerant(UpstreamResult, 'UpstreamResult');

/**
 * Either document `/result` can answer with, discriminated by structure.
 *
 * {@link UpstreamResult} is tried first because it *requires* `manifest`,
 * which no archive document has; {@link ArchiveResult} requires
 * `schema_version: 1`, which no upstream document has. Order matters: with
 * `onExcessProperty: "preserve"` an over-wide document would otherwise satisfy
 * the narrower schema and lose nothing but its identity.
 */
export const GameResult = Schema.Union(UpstreamResult, ArchiveResult).annotations({
  identifier: 'GameResult',
});
/** Either `/result` document. */
export type GameResult = typeof GameResult.Type;

/** Decode either `/result` document. */
export const decodeGameResult: TolerantDecoder<GameResult> = decodeTolerant(
  GameResult,
  'GameResult',
);

/** Re-encode either `/result` document, unknown fields included. */
export const encodeGameResult: TolerantEncoder<GameResult, typeof GameResult.Encoded> =
  encodeTolerant(GameResult, 'GameResult');

/** True when a decoded `/result` is the upstream document rather than the archive one. */
export const isUpstreamResult = (result: GameResult): result is UpstreamResult =>
  'manifest' in result;

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------
//
// Every 4xx/5xx on these routes is the same `{"error": "<message>"}` document,
// modelled once in `./problem.ts` as `GatewayProblem` — including the
// `upstream returned HTTP {status}` template that `/result` answers with for a
// game that is still running. Nothing game-specific is added here.
