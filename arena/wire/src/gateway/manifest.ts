/**
 * The four JSON documents a run directory keeps on disk, as the replay
 * gateway reads them when the supervisor is gone.
 *
 * ```text
 * .agent-eval/runs/{game_id}/
 *   manifest.json         <- supervisor._manifest            (supervisor.py:1080)
 *   report.json           <- scoring.summarize_episode       (scoring.py:164)
 *   victory.json          <- bridge.lua write_victory        (bridge.lua:307)
 *   replay-catalog.json   <- bridge.lua ensure_replay_catalog (bridge.lua:367)
 *                            or v2_replay._write_catalog     (v2_replay.py:349)
 * ```
 *
 * Every gateway route that answers from disk starts by reading one of these.
 * `_read_manifest` (`replay_gateway.py:557`) is the front door: it rejects a
 * symlinked run directory, refuses anything but a regular file under 8 MiB,
 * and requires `manifest["game_id"]` to equal the requested id — everything
 * after that is `dict.get` with a fallback, which is why so much here is
 * optional rather than required.
 *
 * ## The number doctrine: `bigint` is a Python `int`, `number` is a `float`
 *
 * Every gateway response body is written by `_canonical`
 * (`replay_gateway.py:123`) — `json.dumps(sort_keys=True, separators=(",", ":"),
 * ensure_ascii=False)`.  Python spells a float `600.0` and an int `600`, and
 * `JSON.parse` collapses both to the JavaScript number `600`.  A port that
 * forgets which one it read cannot reproduce the bytes, and byte parity is the
 * whole point of this package (see `src/canon.ts`, whose `CanonValue` takes
 * `bigint` for a Python int and `number` for a Python float).
 *
 * So the classification is a *schema* decision, made once, here, per field:
 * {@link WireInt} for a field Python guarantees is an `int`, {@link WireFloat}
 * for one it guarantees is a `float`, {@link WireNumber} for the handful that
 * are genuinely either.  The corpus proves the classification — the raw bytes
 * of every fixture say `"action_timeout_s": 600.0` and `"places": 2`, and
 * `test/gateway/manifest.test.ts` walks each file's number literals in
 * document order and asserts float-spelled ⟺ decoded `number`,
 * int-spelled ⟺ decoded `bigint`.
 *
 * Three residual gaps are unrecoverable from parsed JSON and are documented
 * where they occur rather than papered over: {@link WireNumber} on
 * `mean_latency_ms` (see {@link SeatStats}), on `cost_base`, and the Lua
 * writer's float-spelled technology ids (see {@link TechnologyEntry}).
 *
 * ## Optionality
 *
 * Two distinct absences exist on disk and a round trip must preserve which one
 * it saw, so this module spells "absent or null" as
 * `Schema.optional(Schema.NullOr(x))` rather than
 * `Schema.optionalWith(x, { nullable: true })`: the latter decodes an explicit
 * `null` to `undefined` and re-encodes it as a *missing key*, silently
 * rewriting the document.  `manifest.recovery` is the field that makes this
 * concrete — it is absent on strategic-v1 runs, `null` on a v2 run that never
 * faulted, and an object on one that did.
 *
 * ## Credentials
 *
 * Nothing in these four documents is credential-shaped; the run's secrets live
 * in `auth.json`, which the gateway never opens.  Two fields are still not for
 * publication: `report.episode` is an absolute local path (the gateway reads it
 * and pops it before serving `/result`, `supervisor.py:10069`), and
 * `controller_fingerprint` is a SHA-256 of a controller identity — public by
 * design, but not a name.  `seat_stats.*.input_tokens` / `output_tokens` are
 * LLM accounting counters, not secrets.
 *
 * @module
 */

import { Schema } from 'effect';
import { ControlProtocol } from '../control-protocol.ts';
import { GameId, RunStateTolerant } from '../ids.ts';
import { JsonValue } from '../json.ts';
import { WireFloat, WireInt, WireNumber } from '../numeric.ts';
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
 * to `bigint`), {@link WireFloat} (a Python `float`, kept a `number`) and
 * {@link WireNumber} (a field that is either) — lives in `../numeric.ts`, one
 * home for the three gateway modules that each declared it.  Import them from
 * there, not from here: one name, one home, so a change to the doctrine cannot
 * reach one copy and miss another.
 */

// ---------------------------------------------------------------------------
// Vocabularies
// ---------------------------------------------------------------------------

/**
 * `CONTROL_PROTOCOLS` (`full_control_v2.py:19`), the {@link ControlProtocol}
 * schema over it, and the `candidate in PUBLIC_CONTROL_PROTOCOLS` membership
 * test (`replay_gateway.py:475`) all live in `../control-protocol.ts`.  This
 * module and `./games.ts` had declared them independently and identically.
 *
 * What stays here is what is specific to *this* producer: the manifest is
 * written by the supervisor, not the gateway, so an unrecognized protocol has
 * to survive the round trip rather than be dropped — hence
 * {@link UnrecognizedControlProtocol} and {@link ControlProtocolTolerant}
 * below.
 */

/**
 * A `control_protocol` string outside {@link CONTROL_PROTOCOLS}.  Branded for
 * the same reason `UnrecognizedRunState` is: the gateway's response to an
 * unknown protocol is to drop the field, and that decision has to be taken in
 * the open rather than fall out of a `===` comparison.
 */
export const UnrecognizedControlProtocol = Schema.String.pipe(
  Schema.brand('UnrecognizedControlProtocol'),
).annotations({ identifier: 'UnrecognizedControlProtocol' });
/** A `control_protocol` string outside {@link CONTROL_PROTOCOLS}. */
export type UnrecognizedControlProtocol = typeof UnrecognizedControlProtocol.Type;

/** A known {@link ControlProtocol}, or an unknown one kept as a distinct brand. */
export const ControlProtocolTolerant = Schema.Union(
  ControlProtocol,
  UnrecognizedControlProtocol,
).annotations({ identifier: 'ControlProtocolTolerant' });
/** A known {@link ControlProtocol}, or an unknown one kept as a distinct brand. */
export type ControlProtocolTolerant = typeof ControlProtocolTolerant.Type;

/**
 * Every `config.timing_mode` the supervisor can write (`supervisor.py:10601`).
 *
 * `default` and `blitz` come from `TIMING_MODE_TIMEOUTS` (`:117`), `infinite`
 * means `action_timeout_s` is null, and `custom` is what a caller-supplied
 * timeout that matches no preset is named.  `blitz` is strategic-v1 only;
 * `V2_TIMING_MODE_TIMEOUTS` (`:125`) offers `default` (600.0) and `infinite`.
 *
 * Not a schema: the gateway's `_public_timing` (`:443`) re-derives the pair
 * from hardcoded 180.0/60.0 constants and therefore drops v2 timing entirely,
 * so a port must read what is on disk rather than trust the published field.
 */
export const MANIFEST_TIMING_MODES = ['default', 'blitz', 'infinite', 'custom'] as const;

/** `AI_DIFFICULTY_LEVELS` — `supervisor.py:129`.  Validated at game creation. */
export const AI_DIFFICULTY_LEVELS = ['novice', 'easy', 'normal', 'hard', 'cheating'] as const;

/** `config.mode` — validated to this pair at creation (`supervisor.py:10546`). */
export const GAME_MODES = ['single', 'multiplayer'] as const;

/** `config.seats[].type` — `"external"` when a place is joinable, else `"native"`. */
export const SEAT_TYPES = ['external', 'native'] as const;

/** `resolved_places[].controller` — `Place.public()`, `supervisor.py:667`. */
export const PLACE_CONTROLLERS = ['agent', 'native_classic_ai'] as const;

// ---------------------------------------------------------------------------
// manifest.json
// ---------------------------------------------------------------------------

/**
 * One configured seat — `Game._seat_config`, `supervisor.py:955`.
 *
 * The gateway never reads this array (it works from `resolved_places`), but
 * `scoring.summarize_episode` does: it indexes seats by `name` to attach
 * `seat_id` and `controller_fingerprint` to each scored player
 * (`scoring.py:173`), so a malformed `seats` breaks report generation rather
 * than a route.
 *
 * `controller_metadata` is whatever JSON the joining agent sent — validated
 * only for JSON-serializability and a 16 KiB canonical size
 * (`supervisor.py:489`), so it is not necessarily an object.  It and `options`
 * feed `controller_fingerprint` (`config.py:271`), a SHA-256 over
 * `json.dumps(..., sort_keys=True, separators=(",", ":"), ensure_ascii=False)`.
 * **A port that recomputes that fingerprint cannot do it from this decoded
 * value**: {@link JsonValue} is number-based, so an int `1` inside the metadata
 * is indistinguishable from a float `1.0` and would hash differently.  Hash the
 * raw bytes, or read the metadata with an int-preserving parser.
 */
export const ManifestSeatConfig = Schema.Struct({
  /** Seat id, e.g. `"place-1"`. */
  id: Schema.String,
  /** In-game player name; the join key into `score.players[].name`. */
  name: Schema.String,
  /** One of {@link SEAT_TYPES}. */
  type: Schema.String,
  /** The agent's self-reported model, when its metadata carried a string one. */
  model: Schema.NullOr(Schema.String),
  base_url: Schema.NullOr(Schema.String),
  /** The objective text, on joinable seats only; `null` on native seats. */
  instructions: Schema.NullOr(Schema.String),
  /** Always `{}` from this writer; part of the fingerprint identity. */
  options: JsonValue,
  controller_label: Schema.NullOr(Schema.String),
  /** Arbitrary agent-supplied JSON — not necessarily an object. */
  controller_metadata: JsonValue,
  /**
   * The server AI level, on native seats only (`null` on joinable ones).
   * Absent on runs archived before the per-seat field existed, which is why
   * the gateway falls back to the game-wide difficulty (`:529`).
   */
  ai_difficulty: Schema.optional(Schema.NullOr(Schema.String)),
  /** SHA-256 of the seat identity; `null` on an unjoined external seat. */
  controller_fingerprint: Schema.NullOr(Schema.String),
}).annotations({ identifier: 'ManifestSeatConfig' });
/** One configured seat. */
export type ManifestSeatConfig = typeof ManifestSeatConfig.Type;

/** Frame-capture knobs — `_integer`-validated to 0..99 and exactly 1. */
export const ManifestServerConfig = Schema.Struct({
  frame_interval: WireInt,
  frame_zoom: WireInt,
}).annotations({ identifier: 'ManifestServerConfig' });
/** Frame-capture knobs. */
export type ManifestServerConfig = typeof ManifestServerConfig.Type;

/**
 * The frozen game configuration — `supervisor.py:1098`.
 *
 * `control_protocol`, `difficulty` and `timing_mode` are all optional *here*,
 * not only at the top level: six of the twelve archived manifest shapes are
 * strategic-v1 runs written before those keys existed.
 *
 * `action_timeout_s` and `lobby_timeout_s` are Python floats in every branch
 * (`_finite_number` returns `float(value)`, `supervisor.py:627`; the presets
 * are `180.0`/`60.0`/`600.0`), which the raw fixture bytes confirm —
 * `"action_timeout_s": 600.0`, never `600`.
 */
export const ManifestConfig = Schema.Struct({
  schema_version: WireInt,
  /** `f"session-{game_id[:12]}"`. */
  name: Schema.String,
  /** One of {@link GAME_MODES}. */
  mode: Schema.String,
  /** Configured places, 2..16. */
  places: WireInt,
  /** Places an external agent may claim. */
  max_agents: WireInt,
  /** Turn limit, 1..5000. */
  turns: WireInt,
  /** `[seed]` — a one-element list, 1..2_147_483_647. */
  seeds: Schema.Array(WireInt),
  /** `"classic"`; the slice refuses anything else (`supervisor.py:10562`). */
  ruleset: Schema.String,
  objective: Schema.String,
  control_protocol: Schema.optional(ControlProtocolTolerant),
  /** One of {@link AI_DIFFICULTY_LEVELS}. */
  difficulty: Schema.optional(Schema.String),
  /** One of {@link MANIFEST_TIMING_MODES}. */
  timing_mode: Schema.optional(Schema.String),
  /** Seconds per action; `null` under infinite timing. */
  action_timeout_s: Schema.NullOr(WireFloat),
  /** Seconds to wait for agents to join; `0.0` disables the wait. */
  lobby_timeout_s: WireFloat,
  server: ManifestServerConfig,
  seats: Schema.Array(ManifestSeatConfig),
}).annotations({ identifier: 'ManifestConfig' });
/** The frozen game configuration. */
export type ManifestConfig = typeof ManifestConfig.Type;

/**
 * A place as resolved at run time — `Game._public_places`, `supervisor.py:992`,
 * over `Place.public()`, `:662`.
 *
 * This is the omit-vs-null field that trips every consumer.  The writer adds
 * `controller_label` / `controller_type` / `model` / `controller_metadata` /
 * `controller_fingerprint` **only** when the place has an agent, and a
 * different four when it is an unjoined native place; the archived corpus shows
 * rows with as few as five keys.  The gateway then re-omits blank ones on its
 * own output (`replay_gateway.py:544`) while the supervisor nulls them
 * (`supervisor.py:9574`) — so a port must be able to say "absent" as distinctly
 * from "null" as it says either from a value.
 */
export const ResolvedPlace = Schema.Struct({
  /** 1-based place number. */
  place: WireInt,
  seat_id: Schema.String,
  player_name: Schema.String,
  /** One of {@link PLACE_CONTROLLERS} — not narrowed: the gateway passes any string through. */
  controller: Schema.String,
  /** `#RRGGBB`, absent on the oldest archived runs. */
  player_color: Schema.optional(Schema.String),
  /** Whether an external agent ever claimed this place. */
  joined: Schema.Boolean,
  controller_label: Schema.optional(Schema.NullOr(Schema.String)),
  /** `"external"` or `"native"`. */
  controller_type: Schema.optional(Schema.NullOr(Schema.String)),
  /** `"classic"` on native rows; the agent's model, or `null`, on joined ones. */
  model: Schema.optional(Schema.NullOr(Schema.String)),
  controller_metadata: Schema.optional(JsonValue),
  controller_fingerprint: Schema.optional(Schema.NullOr(Schema.String)),
  /** Written on unjoined native rows by current builds (`supervisor.py:1022`). */
  ai_difficulty: Schema.optional(Schema.NullOr(Schema.String)),
}).annotations({ identifier: 'ResolvedPlace' });
/** A place as resolved at run time. */
export type ResolvedPlace = typeof ResolvedPlace.Type;

/**
 * What a full-control-v2 run had to recover from.
 *
 * Written into the manifest by `_v2_recovery_manifest_locked`
 * (`supervisor.py:5922`), which returns `None` — a JSON `null` — when nothing
 * was ever attempted, and independently rebuilt from the durable journal by
 * `scoring.summarize_recovery` (`scoring.py:121`) for `report.recovery`, which
 * is always an object.
 *
 * `by_kind` and `by_outcome` are open counters keyed by whatever the journal
 * recorded (`autosave_rollback`, `sidecar_reattach`; `abandoned`, `failed`,
 * `recovered`), so they are records, not structs.  `rewound_applied_actions`
 * is the load-bearing one: a `true` here makes the run non-comparable and
 * `summarize_episode` appends `v2_game_rewound` to `invalid_reasons` because
 * of it (`scoring.py:259`).
 */
export const RecoverySummary = Schema.Struct({
  attempts: WireInt,
  by_kind: Schema.Record({ key: Schema.String, value: WireInt }),
  by_outcome: Schema.Record({ key: Schema.String, value: WireInt }),
  rewound_applied_actions: Schema.Boolean,
  recovered_to_turns: Schema.Array(WireInt),
}).annotations({ identifier: 'RecoverySummary' });
/** What a full-control-v2 run had to recover from. */
export type RecoverySummary = typeof RecoverySummary.Type;

/**
 * `manifest.json` — `Game._manifest`, `supervisor.py:1080`, written through
 * `_atomic_json` (`:605`: `indent=2`, `sort_keys=True`, trailing newline).
 *
 * Notes that cost a debugging session each:
 *
 * - **`state` and `status` are the same value, written twice** (`:1084`).
 *   Nothing keeps them in sync afterwards except that both come from one
 *   expression; the gateway reads `state` and falls back to `status` (`:1131`).
 *   Neither is an enum — `_public_text(..., "unknown", 32)` passes any string
 *   through, so this decodes as {@link RunStateTolerant}.
 * - **`benchmark_valid` is tri-state** (`:1087`): `true`/`false` once the run
 *   is terminal, `null` while it is not (unless a reason was already
 *   recorded, which forces `false`).  The gateway hardens it to a strict bool
 *   for archive status and to `bool | null` for an index row.
 * - **`error` is free text here** but collapses to the constant
 *   `"Archived game ended with an error."` on the wire (`:859`).
 * - **`invalid_reasons` mixes enum-ish slugs with prose** — `v2_boundary_wedged`
 *   and `sidecar_exited` sit beside whole sentences in the corpus.  Never a
 *   literal union.
 * - **timestamps are float epoch seconds, never ISO-8601**; `created_at` is
 *   always present, the other two are `null` until they happen.
 * - **`current_turn` is an int or null**, from `_current_turn_locked`
 *   (`:2625`), which is the v2 consensus turn under full-control-v2.
 * - **`control_protocol` and `recovery` are absent, not null, on strategic-v1
 *   runs**; current builds always write both keys.
 */
export const Manifest = Schema.Struct({
  /** `1` in every archived run; present but not yet a discriminator. */
  schema_version: WireInt,
  /** Must equal the requested id or the gateway 404s (`replay_gateway.py:568`). */
  game_id: GameId,
  state: RunStateTolerant,
  /** The same value as {@link state}, written a second time. */
  status: RunStateTolerant,
  control_protocol: Schema.optional(ControlProtocolTolerant),
  benchmark_valid: Schema.NullOr(Schema.Boolean),
  error: Schema.NullOr(Schema.String),
  invalid_reasons: Schema.Array(Schema.String),
  /** Float epoch seconds; the supervisor sets it at construction. */
  created_at: WireFloat,
  started_at: Schema.NullOr(WireFloat),
  finished_at: Schema.NullOr(WireFloat),
  /** The freeciv-server exit status, once it has one. */
  returncode: Schema.NullOr(WireInt),
  config: ManifestConfig,
  resolved_places: Schema.Array(ResolvedPlace),
  joined_agents: WireInt,
  /** How many times the match was started; `0` on a run that never began. */
  start_count: WireInt,
  current_turn: Schema.NullOr(WireInt),
  /** Relative file names inside the run directory. */
  commands_file: Schema.String,
  trace_file: Schema.String,
  bridge_status_file: Schema.String,
  scorelog_file: Schema.String,
  /** Count of captured PPM frames. */
  frames: WireInt,
  /** Count of autosaves on disk. */
  checkpoints: WireInt,
  /** `"game.mp4"` once rendered, else `null`. */
  video_file: Schema.NullOr(Schema.String),
  recovery: Schema.optional(Schema.NullOr(RecoverySummary)),
}).annotations({ identifier: 'Manifest' });
/** A run's `manifest.json`. */
export type Manifest = typeof Manifest.Type;

/** Decode a run's `manifest.json`. */
export const decodeManifest: TolerantDecoder<Manifest> = decodeTolerant(Manifest, 'Manifest');

/** Re-encode a decoded manifest, unknown fields and key order intact. */
export const encodeManifest: TolerantEncoder<Manifest, Schema.Schema.Encoded<typeof Manifest>> =
  encodeTolerant(Manifest, 'Manifest');

// ---------------------------------------------------------------------------
// report.json
// ---------------------------------------------------------------------------

/**
 * One scored player — `scoring.parse_scorelog` (`scoring.py:90`) plus the two
 * identity fields `summarize_episode` grafts on (`:206`).
 *
 * `rank` is dense and **not unique**: equal scores share a rank, so two rank-1
 * players is a normal tie, not corruption (`:103`).  `metrics` is whatever
 * SCORELOG2 tags the server emitted — 31 integer counters in the classic
 * ruleset, `{}` for a player that was removed before its first score row.  The
 * gateway filters it to a 32-name allow-list before publishing it on `/result`;
 * on disk it is unfiltered.
 *
 * `alive` / `added_turn` / `removed_turn` / `last_score_turn` are absent —
 * not null — on runs scored before those fields existed.  `alive` is `false`
 * exactly when the player was removed from the game.
 */
export const ScorePlayer = Schema.Struct({
  /** Freeciv player slot number. */
  player_id: WireInt,
  /** In-game player name; joins to `manifest.config.seats[].name`. */
  name: Schema.String,
  /** `metrics["score"]`, or `0` when the player never scored. */
  score: WireInt,
  /** SCORELOG2 tag → integer value, at `last_score_turn`. */
  metrics: Schema.Record({ key: Schema.String, value: WireInt }),
  alive: Schema.optional(Schema.NullOr(Schema.Boolean)),
  added_turn: Schema.optional(WireInt),
  removed_turn: Schema.optional(Schema.NullOr(WireInt)),
  last_score_turn: Schema.optional(Schema.NullOr(WireInt)),
  /** Dense rank, 1-based, shared on ties. */
  rank: WireInt,
  /** The seat this player was configured as, or its name when unmatched. */
  seat_id: Schema.String,
  controller_fingerprint: Schema.String,
}).annotations({ identifier: 'ScorePlayer' });
/** One scored player. */
export type ScorePlayer = typeof ScorePlayer.Type;

/**
 * The parsed scorelog — `{"final_turn": None, "players": []}` when there is no
 * `score.log` at all (`scoring.py:172`), and `final_turn: null` with an empty
 * roster when the log exists but recorded no turn.
 */
export const Score = Schema.Struct({
  final_turn: Schema.NullOr(WireInt),
  players: Schema.Array(ScorePlayer),
}).annotations({ identifier: 'Score' });
/** The parsed scorelog. */
export type Score = typeof Score.Type;

/**
 * Per-seat decision accounting — `scoring.summarize_episode`, `scoring.py:213`.
 *
 * Two shapes, and the discriminator is `decisions`:
 *
 * ```python
 * current["mean_latency_ms"] = round(current.pop("latency_ms") / count, 3) if count else 0
 * ```
 *
 * Python evaluates the `if` first, so when `decisions == 0` the `pop` never
 * runs: the accumulator **survives** as `latency_ms: 0.0` and `mean_latency_ms`
 * is the *integer* `0`.  When `decisions > 0` there is no `latency_ms` and
 * `mean_latency_ms` is a float.  The corpus matches on all thirteen rows.
 *
 * That is also the recipe for byte parity, since {@link WireNumber} cannot tell
 * an integral float from an int: spell `mean_latency_ms` as an int when
 * `decisions === 0n`, and as a float otherwise.
 *
 * `turns` counts decision events for an agent seat, but for a native seat with
 * no events it is backfilled as `final_turn - 1` (`scoring.py:242`) — which is
 * why a seat can show `turns: 331` and `decisions: 0`.
 */
export const SeatStats = Schema.Struct({
  /** `null` when the decision events carried no fingerprint. */
  controller_fingerprint: Schema.NullOr(Schema.String),
  turns: WireInt,
  decisions: WireInt,
  fallbacks: WireInt,
  input_tokens: WireInt,
  output_tokens: WireInt,
  /** Integer `0` when `decisions` is 0, a float otherwise. */
  mean_latency_ms: WireNumber,
  /** The un-popped accumulator; present exactly when `decisions` is 0. */
  latency_ms: Schema.optional(WireFloat),
}).annotations({ identifier: 'SeatStats' });
/** Per-seat decision accounting. */
export type SeatStats = typeof SeatStats.Type;

/**
 * The `{}` branch of `report.manifest`.
 *
 * `summarize_episode` reads the manifest back off disk and substitutes an
 * empty dict when the file is missing (`scoring.py:169`), so a report can
 * legitimately carry no manifest at all.  The filter is what keeps the union
 * honest: without it, `Schema.Struct({})` under `onExcessProperty: "preserve"`
 * accepts *any* object and a half-broken manifest would decode as "empty"
 * instead of failing.
 */
export const EmptyManifest = Schema.Struct({}).pipe(
  Schema.filter((value) =>
    Object.keys(value).length === 0 ? undefined : 'expected an empty object',
  ),
).annotations({ identifier: 'EmptyManifest' });
/** The `{}` branch of `report.manifest`. */
export type EmptyManifest = typeof EmptyManifest.Type;

/**
 * `report.json` — `scoring.summarize_episode`, `scoring.py:164`, written by the
 * supervisor at finalization (`supervisor.py:1680`) and again by the orphan
 * sweeper on startup (`:10502`).
 *
 * The gateway reads exactly three things out of it — `score.final_turn`,
 * `score.players[]` and (via the manifest it embeds) nothing else — to build
 * the archive leaderboard (`replay_gateway.py:781`).  Everything else here
 * exists for scorers and for `/result`.
 *
 * `episode` is an absolute local path.  It is read but never emitted: the
 * upstream `/result` pops it (`supervisor.py:10069`) and the archive `/result`
 * builds a curated payload that never mentions it.  A port must keep that
 * property — it is the one field in this file that leaks the host filesystem.
 */
export const Report = Schema.Struct({
  /** Absolute path of the run directory.  Read, never published. */
  episode: Schema.String,
  /** The manifest as it stood at finalization, or `{}` if there was none. */
  manifest: Schema.Union(Manifest, EmptyManifest),
  score: Score,
  /** Seat id → accounting; `{}`, partial, or complete. */
  seat_stats: Schema.Record({ key: Schema.String, value: SeatStats }),
  /** Always written by current builds; absent on older reports. */
  recovery: Schema.optional(RecoverySummary),
}).annotations({ identifier: 'Report' });
/** A run's `report.json`. */
export type Report = typeof Report.Type;

/** Decode a run's `report.json`. */
export const decodeReport: TolerantDecoder<Report> = decodeTolerant(Report, 'Report');

/** Re-encode a decoded report, unknown fields and key order intact. */
export const encodeReport: TolerantEncoder<Report, Schema.Schema.Encoded<typeof Report>> =
  encodeTolerant(Report, 'Report');

// ---------------------------------------------------------------------------
// victory.json
// ---------------------------------------------------------------------------

/**
 * `VICTORY_LABELS` — `replay_gateway.py:667`, mirrored at
 * `supervisor.py:8930`.  Maps the engine's game-over reason to the phrase the
 * outcome summary is annotated with.
 *
 * `turn_limit` is the interesting one: no in-game condition fired, the match
 * simply ran out of turns and is decided on final civilization score — hence
 * `"score victory"` rather than anything about conquest.
 */
export const VICTORY_LABELS = {
  spacerace: 'spaceship victory',
  conquest: 'conquest victory',
  team: 'team victory',
  allied: 'allied victory',
  culture: 'cultural domination victory',
  world_peace: 'world peace victory',
  scenario: 'scenario victory',
  all_defeated: 'all civilizations defeated',
  turn_limit: 'score victory',
} as const satisfies Readonly<Record<string, string>>;

/** A victory code this build has a phrase for. */
export type VictoryCode = keyof typeof VICTORY_LABELS;

// A Map, not the object literal: `VICTORY_LABELS["constructor"]` would find an
// inherited member and answer where Python's `dict.get` answers the default.
const VICTORY_LABEL_LOOKUP: ReadonlyMap<string, string> = new Map<string, string>(
  Object.entries(VICTORY_LABELS),
);

/**
 * `VICTORY_LABELS.get(code, code)` (`replay_gateway.py:697`) — an unknown code
 * is its own label, so a newer engine's condition still narrates rather than
 * disappearing.
 */
export const victoryLabel = (code: string): string => VICTORY_LABEL_LOOKUP.get(code) ?? code;

/**
 * `victory.json` — written by `write_victory` in `bridge.lua:307` when the
 * engine fires `agent_game_over`, as one literal line:
 *
 * ```text
 * {"schema_version":1,"victory":"<reason>","winners":["<name>",...],"turn":<n>,"year":<n>}
 * ```
 *
 * No run in the archived corpus has one — victory is otherwise *derived*, from
 * `score.players[].rank` — so this schema is transcribed from the writer and
 * the reader rather than from captured bytes.
 *
 * The reader, `_archive_victory` (`replay_gateway.py:681`), is much more
 * forgiving than the writer and every one of its concessions is a real
 * failure mode:
 *
 * - the file is read inside `try/except (OSError, JSONDecodeError)` and a
 *   failure means "no victory record", silently.  That is not hypothetical:
 *   `write_victory` interpolates `tostring(turn)`, so a nil turn would emit a
 *   bare `nil` and make the file unparseable.
 * - a non-dict document, or a `victory` that is missing, non-string or empty,
 *   also yields no record — hence {@link Schema.NonEmptyString} here.
 * - `winners` is filtered to its string elements, and a non-list becomes `[]`.
 * - `turn` and `year` are passed through **unvalidated**, straight into the
 *   published `outcome.victory`.  Modelled as {@link WireNumber} because Lua's
 *   `tostring` spells an integer without a fractional part; a port that sees
 *   anything else there should keep it verbatim rather than reject the file,
 *   since the gateway would.
 *
 * A decoded record only annotates the summary when the outcome is `won` or
 * `tied` (`:716`); an `invalid` run still reports its victory for diagnostics
 * but is never phrased as having won.
 */
export const VictoryRecord = Schema.Struct({
  /** `1` from the writer; the reader never looks at it. */
  schema_version: Schema.optional(WireInt),
  /** The engine's reason code — a {@link VictoryCode} or something newer. */
  victory: Schema.NonEmptyString,
  /** Winning player names; empty on a turn-limit finish. */
  winners: Schema.Array(Schema.String),
  /** Game turn the match ended on. */
  turn: Schema.optional(WireNumber),
  /** In-game year; negative before year 0. */
  year: Schema.optional(WireNumber),
}).annotations({ identifier: 'VictoryRecord' });
/** A run's `victory.json`. */
export type VictoryRecord = typeof VictoryRecord.Type;

/** Decode a run's `victory.json`. */
export const decodeVictoryRecord: TolerantDecoder<VictoryRecord> = decodeTolerant(
  VictoryRecord,
  'VictoryRecord',
);

/** Re-encode a decoded victory record. */
export const encodeVictoryRecord: TolerantEncoder<
  VictoryRecord,
  Schema.Schema.Encoded<typeof VictoryRecord>
> = encodeTolerant(VictoryRecord, 'VictoryRecord');

// ---------------------------------------------------------------------------
// replay-catalog.json
// ---------------------------------------------------------------------------

/** Highest technology id the Lua writer scans, and the reader accepts (`supervisor.py:427`). */
export const MAX_TECHNOLOGY_ID = 511;

/**
 * One technology.
 *
 * Two writers produce this file and they disagree about the optional half:
 *
 * - `bridge.lua:350` (strategic-v1) writes `{id, rule_name, name, cost_base}`
 *   with the ruleset's real research costs, and spells the id with Lua's
 *   `tostring`, which yields `1.0` for a float-typed id — the archived
 *   strategic-v1 catalog is full of `"id":1.0`.
 * - `v2_replay._write_catalog` (`v2_replay.py:349`) writes what
 *   `save_replay._technology_catalog` (`save_replay.py:322`) reconstructed from
 *   an autosave, which adds `requires` and `depth` and, because an autosave
 *   cannot answer research cost, sets `cost_base` to the integer `0`.
 *
 * Both are `schema_version: 1`; the version does not distinguish them.
 *
 * `id` decodes to `bigint` for both spellings, matching the reader's
 * `int(tech_id)` normalization (`supervisor.py:435`) — the `1.0` spelling
 * cannot survive a round trip and does not need to, because the reader
 * normalizes it away before anything is published.
 *
 * `cost_base` is {@link WireNumber} and *is* passed through unchanged, so its
 * spelling is observable on the wire.  Lua types every cost as a float, and 9
 * of the 87 classic technologies have a round one (`640.0`, `3430.0`, ...),
 * which `JSON.parse` renders indistinguishable from an int; those decode to
 * `bigint` and would re-encode a fraction narrower than Lua wrote them.  The
 * ported gateway does not hit this — its disk path reconstructs the catalog
 * from autosaves rather than republishing this file — but a port that does
 * must take the spelling from the writer's identity, not from the value.
 *
 * The row-local filters are the reader's (`supervisor.py:424`): finite,
 * integral, 0..511 id and a finite non-negative cost.  The *closure* checks it
 * also applies — 87 unique Classic technologies, acyclic, every prerequisite
 * present — belong to the loader, not to the file format, and are deliberately
 * not encoded here.
 */
export const TechnologyEntry = Schema.Struct({
  /** Freeciv technology id, 0..511. */
  id: WireInt.pipe(Schema.betweenBigInt(0n, BigInt(MAX_TECHNOLOGY_ID))),
  /** Ruleset rule name, e.g. `"Bronze Working"`. */
  rule_name: Schema.String,
  /** Translated display name. */
  name: Schema.String,
  /** Base research cost; the integer `0` when reconstructed from an autosave. */
  cost_base: WireNumber,
  /** Prerequisite technology ids.  Absent from the Lua-written catalog. */
  requires: Schema.optional(Schema.Array(WireInt)),
  /** Longest prerequisite chain to this technology.  Absent from the Lua-written catalog. */
  depth: Schema.optional(WireInt),
}).annotations({ identifier: 'TechnologyEntry' });
/** One technology. */
export type TechnologyEntry = typeof TechnologyEntry.Type;

/**
 * `replay-catalog.json` — the technology tree a replay's research numbers are
 * read against.
 *
 * The gateway does not serve this file directly; the supervisor validates and
 * closes it with `_classic_technology_catalog` (`supervisor.py:408`) and
 * republishes the result as `catalog` inside `replay.json`.  A `ValueError`
 * there is not fatal — it degrades to the warning "Technology telemetry is
 * temporarily unavailable." and a `catalog: null` (`supervisor.py:9903`).
 */
export const ReplayCatalog = Schema.Struct({
  schema_version: WireInt,
  technologies: Schema.Array(TechnologyEntry),
}).annotations({ identifier: 'ReplayCatalog' });
/** A run's `replay-catalog.json`. */
export type ReplayCatalog = typeof ReplayCatalog.Type;

/** Decode a run's `replay-catalog.json`. */
export const decodeReplayCatalog: TolerantDecoder<ReplayCatalog> = decodeTolerant(
  ReplayCatalog,
  'ReplayCatalog',
);

/** Re-encode a decoded technology catalog. */
export const encodeReplayCatalog: TolerantEncoder<
  ReplayCatalog,
  Schema.Schema.Encoded<typeof ReplayCatalog>
> = encodeTolerant(ReplayCatalog, 'ReplayCatalog');
