/**
 * The `full-control-v2` health envelope, the phase-end event and the recovery
 * event.
 *
 * Clean-room Effect Schema re-expression of `_validate_health`
 * (`play/client.py:2021-2295`), `_validate_phase_end_event`
 * (`play/client.py:1932-1987`) and `_validate_recovery_event`
 * (`play/client.py:2000-2018`).  Nothing here imports from `play-cli`; its
 * `src/schema/health.ts` was read for semantics only, and every line citation
 * was re-derived against `play/client.py` rather than copied — several of the
 * CLI's had drifted by ~370 lines.
 *
 * Health is the envelope every other v2 command is judged against: `wait`
 * embeds one, `turn` renders one, and the phase block is the only thing that
 * says whether this seat may act *right now*.  Three invariants are therefore
 * load-bearing, and are enforced here rather than at a call site that might
 * forget them.
 *
 * **1. A terminal game may not carry a phase.**  `client.py:2106-2107` refuses
 * a health envelope whose `game_state` is terminal but whose `phase` survived,
 * because a stale actionable phase is how a client keeps playing a finished
 * game.  `phase: null` on a terminal game is fine and expected — a clean native
 * exit is observable just before the supervisor classifies its result.
 *
 * **2. Every seat-shaped sub-block must name the right seat.**
 * `last_phase_end` and `last_recovery` are checked against the envelope's own
 * `seat` (`client.py:1967-1969`, `:2005-2008`), and `phase.prior_end` is
 * checked to name a **different** one (`client.py:2168`) — it describes who
 * held the phase before this one.
 *
 * **3. The evaluation context is all-or-nothing.**  `objective`, `max_turns`
 * and `turns_remaining` are spliced into the envelope's *top level*, not
 * nested, and a partial triple is drift rather than an older server
 * (`client.py:2039-2040`).  When a phase turn is known, `turns_remaining` must
 * equal `max(0, max_turns - turn)` exactly (`client.py:2286-2292`).
 *
 * ## Tolerance, and the one visible consequence
 *
 * Per `./primitives.ts`: `_exact`'s closedness is the single axis that gives.
 * Unknown fields are preserved instead of fatal; every literal vocabulary,
 * bound, pattern and cross-field invariant survives unchanged, and a *missing*
 * field is still a decode failure.
 *
 * The one place that shows is `V2_SIDECAR_FIELDS`, which the Python uses to
 * **reject** an unknown sidecar key outright (`client.py:2091-2099`).  Here the
 * key is preserved and the verdict is offered as {@link unexpectedSidecarFields},
 * so a play surface can still refuse while a proxy can forward a field it was
 * built too early to name.
 *
 * ## Two divergences worth naming
 *
 * **`_json_value`'s size bounds are not re-imposed here.**  The Python copies
 * `sidecar` and the whole recovery event through `_json_value`
 * (`client.py:2282`, `:2018`), which caps nesting at 12, arrays at 8192 items,
 * objects at 2048 keys and keys at 128 characters.  Those are a
 * denial-of-service guard on a *response body*, not a statement about this
 * packet's shape, and this package leaves them to whoever reads the socket —
 * `boundedJson` in `./primitives.ts` is that guard, applied where a free-form
 * sub-tree is actually declared.  Every field this module names is either
 * scalar or a fixed-depth struct, so the caps are unreachable through them.
 *
 * **A wire `5.0` decodes where Python refuses it.**  `type(raw["turn"]) is not
 * int` rejects a JSON float; `JSON.parse` produces one `number` for `5` and
 * `5.0` alike, so `Schema.int()` accepts both.  This widens acceptance and
 * never narrows it — no payload the Python admits is refused here.
 *
 * @module
 */

import { Schema } from 'effect';
import {
  checkEvaluationDrift,
  hasOwnKey,
  MAX_TURNS_CEILING,
  NonEmptyText,
  NonNegativeWireInt,
  NullableSafeNumber,
  PositiveWireInt,
  SafeNumber,
  sortedNames,
  V2_EVALUATION_FIELDS,
  type EvaluationContext,
  type SessionIdentity,
} from './primitives.ts';
import { ControlProtocolV2, SchemaVersionV2, sessionMismatch } from './protocol.ts';
import { JsonValue } from '../json.ts';
import { decodeTolerant, type TolerantDecoder } from '../tolerant.ts';

// ---------------------------------------------------------------------------
// Scalars
// ---------------------------------------------------------------------------

/**
 * `PLAYER_COLOR_RE` — `play-cli/src/constants.ts:199`, from `play/client.py`,
 * applied with `fullmatch` at `client.py:1971`: `^#[0-9A-F]{6}$`.
 *
 * **Upper case only.**  `#ff0000` is a drifted server, not a colour;
 * normalizing the case here would hide exactly the drift the check exists to
 * surface, and would break the package's round-trip guarantee besides.
 */
export const PLAYER_COLOR_RE: RegExp = /^#[0-9A-F]{6}$/;

/** A player colour, `#RRGGBB`, hex digits upper case. */
export const PlayerColor = Schema.String.pipe(
  Schema.pattern(PLAYER_COLOR_RE, {
    identifier: 'PlayerColor',
    description: 'Player colour #RRGGBB with upper-case hex digits (PLAYER_COLOR_RE)',
  }),
);
/** A player colour. */
export type PlayerColor = typeof PlayerColor.Type;

// ---------------------------------------------------------------------------
// Vocabularies
// ---------------------------------------------------------------------------

/**
 * The game states a health envelope may report — `client.py:2077-2079`:
 * `{"lobby", "starting", "running", *TERMINAL_STATES}`.
 *
 * Member for member the union of `LIVE_RUN_STATES` and `TERMINAL_RUN_STATES`
 * in `../ids.ts`, which transcribe the same vocabulary from the replay
 * gateway.  The two are written out separately rather than imported across the
 * agent/archive seam — they are the same words for a reason that could stop
 * being true — and `test/agent/health.test.ts` asserts the equality so a
 * silent divergence is a test failure rather than a discovery.
 *
 * The gateway's `"interrupted"` and `"unknown"` are absent on purpose: they are
 * labels the gateway synthesizes for an archive it can no longer ask about, and
 * a live supervisor never puts one on a health envelope.
 */
export const V2_GAME_STATES = [
  'lobby',
  'starting',
  'running',
  'completed',
  'invalid',
  'failed',
  'cancelled',
] as const;

/** A game state a health envelope may report. */
export const V2GameState = Schema.Literal(...V2_GAME_STATES).annotations({
  identifier: 'V2GameState',
  description: 'lobby | starting | running | completed | invalid | failed | cancelled',
});
/** A game state a health envelope may report. */
export type V2GameState = typeof V2GameState.Type;

/**
 * `TERMINAL_STATES` — `play/client.py:42`.  A game in one of these is
 * finished: no phase may survive, and `wait`'s `game_terminal` wake is honest
 * only here.
 */
export const V2_TERMINAL_GAME_STATES = ['completed', 'invalid', 'failed', 'cancelled'] as const;

const TERMINAL_GAME_STATE_SET: ReadonlySet<string> = new Set<string>(V2_TERMINAL_GAME_STATES);

/**
 * `state in TERMINAL_STATES` — `client.py:2101`, `:2107`, `:2340`.
 *
 * Takes any string so it can be asked about a value that has not been narrowed
 * yet; an unrecognized state is, correctly, not terminal.
 */
export const isTerminalGameState = (
  state: string,
): state is (typeof V2_TERMINAL_GAME_STATES)[number] => TERMINAL_GAME_STATE_SET.has(state);

/**
 * How a seat is standing in the game — `client.py:2064-2066`.
 *
 * `termination_pending` is the interesting one: the seat is still seated and
 * still owes a phase end, but the supervisor has already decided it is leaving.
 */
export const SEAT_STANDINGS = [
  'active',
  'surrendered',
  'eliminated',
  'termination_pending',
] as const;

/** A seat standing. */
export const SeatStanding = Schema.Literal(...SEAT_STANDINGS).annotations({
  identifier: 'SeatStanding',
  description: 'active | surrendered | eliminated | termination_pending (client.py:2064)',
});
/** A seat standing. */
export type SeatStanding = typeof SeatStanding.Type;

/**
 * `V2_PHASE_STATES` — `play-cli/src/constants.ts:146`, from `play/client.py`.
 *
 * Only `awaiting_agent` with `active: true` means "this seat may act"; the
 * other seven are all flavours of "wait", and `wait`'s wake contract
 * (`./wait.ts`) is built out of that one distinction.
 */
export const V2_PHASE_STATES = [
  'synchronizing',
  'native_phase',
  'phase_not_ready',
  'inactive_done',
  'awaiting_agent',
  'ending',
  'ambiguous_ending',
  'terminalizing',
] as const;

/** The one phase state that means this seat may act. */
export const AWAITING_AGENT = 'awaiting_agent' as const;

/** A phase state. */
export const V2PhaseState = Schema.Literal(...V2_PHASE_STATES).annotations({
  identifier: 'V2PhaseState',
  description: 'One of the eight full-control-v2 phase states (V2_PHASE_STATES)',
});
/** A phase state. */
export type V2PhaseState = typeof V2PhaseState.Type;

/** Phase timing modes — `client.py:2249`. */
export const V2_TIMING_MODES = ['default', 'blitz', 'infinite', 'custom'] as const;

/** A phase timing mode. */
export const TimingMode = Schema.Literal(...V2_TIMING_MODES).annotations({
  identifier: 'TimingMode',
  description: 'default | blitz | infinite | custom (client.py:2249)',
});
/** A phase timing mode. */
export type TimingMode = typeof TimingMode.Type;

/** Why the current phase is not this seat's to play — `client.py:2192-2196`. */
export const WAITING_ON_KINDS = [
  'seat_not_ready',
  'seat_surrendered',
  'seat_inactive',
  'other_seat',
  'native_phase',
  'phase_synchronization',
  'phase_end',
  'termination',
  'boundary_recovery',
] as const;

/**
 * A `waiting_on.kind`.
 *
 * A **closed** vocabulary, and the Python's own refusal says why
 * (`client.py:2197-2202`): an unknown kind means this workspace's client
 * predates the server, and the remedy is to re-materialize the client — not to
 * guess at a reason and keep playing.  That sentence is a CLI concern and is
 * not reproduced here; the rejection is.
 */
export const WaitingOnKind = Schema.Literal(...WAITING_ON_KINDS).annotations({
  identifier: 'WaitingOnKind',
  description: 'Why the current phase is not this seat’s to play (client.py:2192)',
});
/** A `waiting_on.kind`. */
export type WaitingOnKind = typeof WaitingOnKind.Type;

/**
 * What ended a phase — `client.py:1974`.
 *
 * `auto_idle` is the youngest of the three: the service ended a phase in which
 * this seat had no idle actor and no pending decision, and had gone quiet.
 */
export const PHASE_END_SOURCES = ['agent', 'timeout', 'auto_idle'] as const;

/** What ended a phase. */
export const PhaseEndSource = Schema.Literal(...PHASE_END_SOURCES).annotations({
  identifier: 'PhaseEndSource',
  description: 'agent | timeout | auto_idle (client.py:1974)',
});
/** What ended a phase. */
export type PhaseEndSource = typeof PhaseEndSource.Type;

/** What the phase end did to the game — `client.py:1976`. */
export const PHASE_END_RESOLUTIONS = ['advanced', 'terminal', 'failed'] as const;

/** What the phase end did to the game. */
export const PhaseEndResolution = Schema.Literal(...PHASE_END_RESOLUTIONS).annotations({
  identifier: 'PhaseEndResolution',
  description: 'advanced | terminal | failed (client.py:1976)',
});
/** What the phase end did to the game. */
export type PhaseEndResolution = typeof PhaseEndResolution.Type;

/**
 * `V2_TERMINAL_RECEIPTS` — `play/client.py:49`:
 * `{"applied", "rejected", "ambiguous"}`, named here for its use: the receipt
 * state a phase can have *ended* on.
 *
 * `accepted` is missing on purpose — it means "queued", and a phase that ended
 * while a batch was merely accepted is a supervisor bug, not a shape to
 * normalize away.
 */
export const PHASE_END_RECEIPT_STATES = ['applied', 'rejected', 'ambiguous'] as const;

/** A receipt state a phase can have ended on. */
export const PhaseEndReceiptState = Schema.Literal(...PHASE_END_RECEIPT_STATES).annotations({
  identifier: 'PhaseEndReceiptState',
  description: 'applied | rejected | ambiguous (client.py V2_TERMINAL_RECEIPTS)',
});
/** A receipt state a phase can have ended on. */
export type PhaseEndReceiptState = typeof PhaseEndReceiptState.Type;

/**
 * `V2_SIDECAR_FIELDS` — `play-cli/src/constants.ts:157`, from `play/client.py`:
 * every key the sidecar block is known to carry.
 *
 * The last three are death forensics: they separate "the client crashed" from
 * "the client is alive and merely slow", which is the difference between a
 * recoverable seat and a lost one.
 *
 * The Python treats an unlisted key as fatal drift.  This port does not — see
 * the module doc — but keeps the list so {@link unexpectedSidecarFields} can
 * hand a caller the same verdict.
 */
export const V2_SIDECAR_FIELDS = [
  'state',
  'generation',
  'player_name',
  'started_at',
  'ready_at',
  'last_seen_at',
  'stopped_at',
  'exit_code',
  'error_code',
  'client_state',
  'server_connected',
  'seat_state',
  'exit_signal',
  'exit_signal_name',
  'process_alive',
] as const;

/** `V2_RECOVERY_KINDS` — `play/client.py:1996`. */
export const V2_RECOVERY_KINDS = ['sidecar_reattach', 'autosave_rollback'] as const;

/** How the supervisor tried to put a wedged seat back together. */
export const RecoveryKind = Schema.Literal(...V2_RECOVERY_KINDS).annotations({
  identifier: 'RecoveryKind',
  description: 'sidecar_reattach | autosave_rollback (client.py:1996)',
});
/** A recovery kind. */
export type RecoveryKind = typeof RecoveryKind.Type;

/** `V2_RECOVERY_OUTCOMES` — `play/client.py:1997`. */
export const V2_RECOVERY_OUTCOMES = ['recovered', 'failed', 'abandoned'] as const;

/** How a recovery attempt turned out. */
export const RecoveryOutcome = Schema.Literal(...V2_RECOVERY_OUTCOMES).annotations({
  identifier: 'RecoveryOutcome',
  description: 'recovered | failed | abandoned (client.py:1997)',
});
/** A recovery outcome. */
export type RecoveryOutcome = typeof RecoveryOutcome.Type;

/**
 * The seventeen keys `_validate_recovery_event` demands, exactly
 * (`V2_RECOVERY_FIELDS`, `play/client.py:1990-1995`).
 *
 * Kept as data because it is the *complete* set: a recovery event is one of the
 * few v2 sub-objects whose key list is a fact worth asserting in a test.
 */
export const V2_RECOVERY_FIELDS = [
  'attempt',
  'client_state',
  'exit_code',
  'exit_signal',
  'format',
  'game_id',
  'kind',
  'outcome',
  'place',
  'recovered_to_turn',
  'rewound_applied_actions',
  'schema_version',
  'seat_id',
  'sidecar_generation',
  'timestamp',
  'trigger',
  'turn',
] as const;

// ---------------------------------------------------------------------------
// The inline evaluation context — _validate_evaluation_context (client.py:1010)
// ---------------------------------------------------------------------------

/**
 * The evaluation triple as *optional* struct fields, for splicing into an
 * envelope that carries the context inline rather than nested.
 *
 * The field rules mirror `EvaluationContext` in `./primitives.ts` exactly —
 * that schema is the nested form and cannot be reused here, because a
 * refinement does not expose its underlying `fields`.  The shared bound
 * ({@link MAX_TURNS_CEILING}) is imported rather than repeated so the two
 * cannot drift on the number that actually matters.
 *
 * `exact: true` because the wire is JSON: a key is present or it is not, and an
 * explicit `undefined` is not something a server can send.
 */
export const OptionalEvaluationFields = {
  objective: Schema.optionalWith(
    Schema.String.pipe(Schema.nonEmptyString(), Schema.trimmed()).annotations({
      identifier: 'EvaluationObjective',
    }),
    { exact: true },
  ),
  max_turns: Schema.optionalWith(
    Schema.Number.pipe(Schema.int(), Schema.between(1, MAX_TURNS_CEILING)).annotations({
      identifier: 'EvaluationMaxTurns',
    }),
    { exact: true },
  ),
  turns_remaining: Schema.optionalWith(Schema.NullOr(NonNegativeWireInt), { exact: true }),
} as const;

/** A value that may or may not carry the evaluation triple. */
export interface PartialEvaluation {
  readonly objective?: string;
  readonly max_turns?: number;
  readonly turns_remaining?: number | null;
}

/** How many of the three evaluation keys a value physically carries. */
export const evaluationArity = (value: PartialEvaluation): number =>
  [...V2_EVALUATION_FIELDS].filter((name) => hasOwnKey(value, name)).length;

/**
 * The complete evaluation context a value carries inline, or `null` when it
 * carries none of it.
 *
 * A *partial* triple reads as `null` too — {@link inlineEvaluationIssues} is
 * what rejects that; this reader only reads.
 */
export const inlineEvaluation = (value: PartialEvaluation): EvaluationContext | null =>
  value.objective !== undefined &&
  value.max_turns !== undefined &&
  value.turns_remaining !== undefined
    ? {
        objective: value.objective,
        max_turns: value.max_turns,
        turns_remaining: value.turns_remaining,
      }
    : null;

/**
 * Every complaint `_validate_evaluation_context` (`client.py:1010-1054`) would
 * raise about a value's *inline* evaluation triple.
 *
 * The order follows the Python: *incomplete* beats *missing* beats *changed*,
 * so a server that dropped one key is diagnosed as having dropped a key rather
 * than as having changed the objective.  The *malformed* branch is absent
 * because the field schemas above already caught it — including the
 * `turns_remaining <= max_turns` bound, which is checked here since it is the
 * one rule that spans two fields.
 *
 * Reuses `checkEvaluationDrift` from `./primitives.ts` for the comparison
 * against a session's own context, so the two decoders cannot disagree about
 * what "the objective changed" means.
 */
export const inlineEvaluationIssues = (
  value: PartialEvaluation,
  expected: EvaluationContext | null,
): ReadonlyArray<Schema.FilterIssue> => {
  const arity = evaluationArity(value);
  if (arity > 0 && arity !== V2_EVALUATION_FIELDS.size) {
    return [{ path: [], message: 'evaluation context is incomplete' }];
  }
  const context = inlineEvaluation(value);
  if (context === null) {
    return expected === null ? [] : [{ path: [], message: 'evaluation context is missing' }];
  }
  const overBudget: ReadonlyArray<Schema.FilterIssue> =
    context.turns_remaining !== null && context.turns_remaining > context.max_turns
      ? [{ path: ['turns_remaining'], message: 'evaluation context is malformed' }]
      : [];
  if (expected === null) return overBudget;
  const drift = checkEvaluationDrift(context, expected, 'v2 health');
  return drift._tag === 'Left'
    ? [...overBudget, { path: [], message: drift.left.message }]
    : overBudget;
};

// ---------------------------------------------------------------------------
// Sub-blocks
// ---------------------------------------------------------------------------

/**
 * The agent block — `client.py:2054-2058`.
 *
 * `controller_label` is checked non-empty by the schema and checked *equal to
 * the session's* by {@link healthEnvelopeFor}: a label that changed mid-game
 * means the supervisor re-seated this agent under another controller, which no
 * command may quietly play through.
 */
export const HealthAgent = Schema.Struct({
  agent_id: Schema.String,
  controller_label: NonEmptyText,
}).annotations({
  identifier: 'HealthAgent',
  description: 'The agent identity block of a health envelope (client.py:2054)',
});
/** The agent block. */
export type HealthAgent = typeof HealthAgent.Type;

/**
 * The seat block — `client.py:2059-2076`.
 *
 * `standing` is optional-if-present: a supervisor that reports standings and
 * one that does not are both valid peers.  The three identity fields are what
 * every other seat-shaped sub-block in the envelope is checked against.
 */
export const HealthSeat = Schema.Struct({
  place: PositiveWireInt,
  seat_id: NonEmptyText,
  player_name: NonEmptyText,
  standing: Schema.optionalWith(SeatStanding, { exact: true }),
}).annotations({
  identifier: 'HealthSeat',
  description: 'The seat block of a health envelope (client.py:2059)',
});
/** The seat block. */
export type HealthSeat = typeof HealthSeat.Type;

/**
 * A sidecar field's value.
 *
 * `client.py:2088-2091` requires every value to be `None` or a
 * `str`/`int`/`float`/`bool` and checks nothing else — so the numbers here stay
 * plain `number` rather than {@link NonNegativeWireInt}: there is no declared
 * field to tell an int from a float, and guessing would be worse than
 * declining.
 */
export const SidecarValue = Schema.NullOr(
  Schema.Union(Schema.String, Schema.Number, Schema.Boolean),
).annotations({ identifier: 'SidecarValue', description: 'A sidecar scalar, or null' });
/** A sidecar field's value. */
export type SidecarValue = typeof SidecarValue.Type;

/**
 * The sidecar block — `client.py:2085-2100`.
 *
 * `state` and `generation` are required by name (`"state" not in raw["sidecar"]`)
 * and every other key is admitted as a scalar through the index signature —
 * which is also what validates the values a tolerant decode preserves.
 * `generation` is what makes a reattached sidecar distinguishable from the one
 * it replaced, which is why a recovery event carries it too.
 */
export const SidecarBlock = Schema.Struct(
  {
    state: SidecarValue,
    generation: SidecarValue,
  },
  Schema.Record({ key: Schema.String, value: SidecarValue }),
).annotations({
  identifier: 'SidecarBlock',
  description: 'The sidecar status block of a health envelope (client.py:2085)',
});
/** The sidecar block. */
export type SidecarBlock = typeof SidecarBlock.Type;

const SIDECAR_FIELD_SET: ReadonlySet<string> = new Set<string>(V2_SIDECAR_FIELDS);

/**
 * The sidecar keys this build has never heard of, sorted the way Python's
 * `sorted()` sorts them — which is by **code point**, so this must go through
 * `sortedNames` (`./primitives.ts`) and not a hand-rolled `<` comparison.
 * Sidecar keys are server-chosen and free-form (the block carries a
 * `Schema.Record` index signature), so an astral-plane key is representable on
 * the wire, and JS's UTF-16 code-unit order disagrees with Python's from
 * U+E000 up — it would report the offending keys in a different order than
 * `client.py:2091-2099` reports them.
 *
 * `client.py:2091-2099` turns a non-empty result into a hard refusal naming
 * every offending key.  This package preserves the fields instead (see the
 * module doc) and hands the verdict to the caller, so a play surface can still
 * refuse while a proxy can forward.
 */
export const unexpectedSidecarFields = (sidecar: SidecarBlock): ReadonlyArray<string> =>
  sortedNames(Object.keys(sidecar).filter((key) => !SIDECAR_FIELD_SET.has(key)));

/** The phase's timing block — `client.py:2240-2256`. */
export const PhaseTiming = Schema.Struct({
  mode: TimingMode,
  timeout_s: NullableSafeNumber,
  deadline_started_at: NullableSafeNumber,
  deadline_at: NullableSafeNumber,
  elapsed_s: NullableSafeNumber,
  remaining_s: NullableSafeNumber,
}).annotations({
  identifier: 'PhaseTiming',
  description: 'The timing block of a phase (client.py:2240)',
});
/** The phase timing block. */
export type PhaseTiming = typeof PhaseTiming.Type;

/**
 * One row of `waiting_on.seats` — `client.py:2216-2226`.
 *
 * Every field is free-form: the Python runs `_exact` over the key set and
 * validates not one value, because these rows exist for the human reading a
 * `turn` render rather than for a decision.  Reproduced exactly — inventing
 * types here would refuse payloads today's client accepts.
 */
export const WaitingOnSeat = Schema.Struct({
  place: JsonValue,
  seat_id: JsonValue,
  player_name: JsonValue,
  controller_label: JsonValue,
  standing: JsonValue,
  is_self: JsonValue,
}).annotations({
  identifier: 'WaitingOnSeat',
  description: 'A waiting_on.seats row; keys checked, values free-form (client.py:2216)',
});
/** A `waiting_on.seats` row. */
export type WaitingOnSeat = typeof WaitingOnSeat.Type;

/** Why the current phase is not this seat's — `client.py:2186-2229`. */
export const WaitingOn = Schema.Struct({
  kind: WaitingOnKind,
  summary: NonEmptyText,
  waiting_s: NullableSafeNumber,
  seats: Schema.Array(WaitingOnSeat),
}).annotations({
  identifier: 'WaitingOn',
  description: 'The waiting_on block of a phase (client.py:2186)',
});
/** The `waiting_on` block. */
export type WaitingOn = typeof WaitingOn.Type;

/**
 * The auto-end block — `client.py:2126-2145`.
 *
 * `armed` says the supervisor *will* end this phase for the seat when the grace
 * runs out; `remaining_s` is how long the seat has to act instead if it would
 * rather.  Both counters are nullable: a disabled auto-end has no clock.
 */
export const AutoEnd = Schema.Struct({
  enabled: Schema.Boolean,
  armed: Schema.Boolean,
  grace_s: NullableSafeNumber,
  remaining_s: NullableSafeNumber,
}).annotations({
  identifier: 'AutoEnd',
  description: 'The auto_end block of a phase (client.py:2126)',
});
/** The `auto_end` block. */
export type AutoEnd = typeof AutoEnd.Type;

/**
 * How the phase *before* this one ended — `client.py:2146-2184`.
 *
 * All eleven keys are required.  `place` names a **different** seat than the
 * envelope's own (`client.py:2168`): this block says who held the previous
 * phase and whether they played it at all, so a `place` equal to this seat's is
 * drift, not a self-report.  `orders_submitted: null` means the supervisor was
 * not watching, which is not the same claim as zero.
 */
export const PriorEnd = Schema.Struct({
  place: PositiveWireInt,
  seat_id: Schema.String,
  player_name: Schema.String,
  controller_label: Schema.NullOr(Schema.String),
  turn: NonNegativeWireInt,
  phase: NonNegativeWireInt,
  source: PhaseEndSource,
  receipt_state: PhaseEndReceiptState,
  resolution: PhaseEndResolution,
  elapsed_s: SafeNumber,
  orders_submitted: Schema.NullOr(NonNegativeWireInt),
}).annotations({
  identifier: 'PriorEnd',
  description: 'How the phase before this one ended (client.py:2146)',
});
/** The `prior_end` block. */
export type PriorEnd = typeof PriorEnd.Type;

/**
 * The phase block — `client.py:2110-2262`.
 *
 * `turn` and `phase` are nullable: a phase that is still synchronizing has no
 * number yet.  `waiting_on`, `auto_end` and `prior_end` are each
 * optional-if-present *and* nullable when present — the shape an additive
 * server field takes on this envelope, and the reason a present-but-`null`
 * `auto_end` is meaningfully different from an absent one.
 */
export const PhaseBlock = Schema.Struct({
  state: V2PhaseState,
  turn: Schema.NullOr(NonNegativeWireInt),
  phase: Schema.NullOr(NonNegativeWireInt),
  active: Schema.Boolean,
  timing: PhaseTiming,
  waiting_on: Schema.optionalWith(Schema.NullOr(WaitingOn), { exact: true }),
  auto_end: Schema.optionalWith(Schema.NullOr(AutoEnd), { exact: true }),
  prior_end: Schema.optionalWith(Schema.NullOr(PriorEnd), { exact: true }),
}).annotations({
  identifier: 'PhaseBlock',
  description: 'The phase block of a health envelope (client.py:2110)',
});
/** The phase block. */
export type PhaseBlock = typeof PhaseBlock.Type;

const PhaseEndEventFields = Schema.Struct({
  sequence: PositiveWireInt,
  turn: NonNegativeWireInt,
  phase: NonNegativeWireInt,
  place: PositiveWireInt,
  seat_id: Schema.String,
  player_name: Schema.String,
  player_color: PlayerColor,
  controller_label: Schema.String,
  controller_type: Schema.Literal('external'),
  source: PhaseEndSource,
  receipt_state: PhaseEndReceiptState,
  resolution: PhaseEndResolution,
  deadline_started_at: SafeNumber,
  ended_at: SafeNumber,
  elapsed_s: SafeNumber,
  incarnation: Schema.optionalWith(NonNegativeWireInt, { exact: true }),
  orders_submitted: Schema.optionalWith(Schema.NullOr(NonNegativeWireInt), { exact: true }),
});

type PhaseEndEventShape = typeof PhaseEndEventFields.Type;

/**
 * The two invariants `_validate_phase_end_event` checks *between* its fields.
 *
 * A `rejected` receipt that resolved as anything but `failed`
 * (`client.py:1977-1978`) would mean the supervisor advanced a turn on a batch
 * it refused; an `ended_at` before `deadline_started_at` (`client.py:1985`)
 * would mean the phase ended before it began.  Both are impossible states, not
 * rare ones.
 */
const phaseEndIssues = (value: PhaseEndEventShape): ReadonlyArray<Schema.FilterIssue> => [
  ...(value.receipt_state === 'rejected' && value.resolution !== 'failed'
    ? [{ path: ['resolution'], message: 'a rejected receipt must resolve as failed' }]
    : []),
  ...(value.ended_at < value.deadline_started_at
    ? [{ path: ['ended_at'], message: 'phase ended before its deadline started' }]
    : []),
];

/**
 * The phase-end event — `_validate_phase_end_event`, `client.py:1932-1987`.
 *
 * `controller_type` is the literal `"external"`: this block only ever describes
 * *this* agent's own seat ending its own phase, and a native seat's phase end is
 * not reported here.  `incarnation` and `orders_submitted` are
 * optional-if-present — the recovery-era supervisor stamps which receipt
 * incarnation ended the phase, and how many batches the seat submitted.
 *
 * The binding to a *particular* seat lives on the envelope
 * ({@link HealthEnvelope}), because that is where the seat block is.
 */
export const PhaseEndEvent = PhaseEndEventFields.pipe(
  Schema.filter((value) => phaseEndIssues(value)),
).annotations({
  identifier: 'PhaseEndEvent',
  description: 'The last_phase_end event of a health envelope (client.py:1932)',
});
/** The phase-end event. */
export type PhaseEndEvent = typeof PhaseEndEvent.Type;

/** Decode an unknown value as a {@link PhaseEndEvent}, seat binding aside. */
export const decodePhaseEndEvent: TolerantDecoder<PhaseEndEvent> = decodeTolerant(
  PhaseEndEvent,
  'PhaseEndEvent',
);

/**
 * A recovery event — `_validate_recovery_event`, `client.py:2000-2018`.
 *
 * Seventeen keys, of which the Python types only five (`kind`, `outcome`,
 * `trigger`, `rewound_applied_actions`, `timestamp`) and *compares* three more
 * (`game_id`, `seat_id`, `place`) against the session and seat.  The rest are
 * copied through `_json_value` untyped, and are {@link JsonValue} here for the
 * same reason: a schema that named their types would start refusing payloads
 * today's client accepts.
 *
 * `place` and `seat_id` are typed anyway.  The Python leaves them free-form but
 * then requires them to equal an integer place and a string seat id, so the
 * types below are exactly what survives that comparison — the check is moved,
 * not added.
 *
 * `rewound_applied_actions` is the field that matters: `true` means the
 * supervisor rolled the world back past actions this agent had already been
 * told were applied, so any plan built on them is void.
 */
export const RecoveryEvent = Schema.Struct({
  attempt: JsonValue,
  client_state: JsonValue,
  exit_code: JsonValue,
  exit_signal: JsonValue,
  format: JsonValue,
  game_id: Schema.String,
  kind: RecoveryKind,
  outcome: RecoveryOutcome,
  place: PositiveWireInt,
  recovered_to_turn: JsonValue,
  rewound_applied_actions: Schema.Boolean,
  schema_version: JsonValue,
  seat_id: Schema.String,
  sidecar_generation: JsonValue,
  timestamp: NonEmptyText,
  trigger: NonEmptyText,
  turn: JsonValue,
}).annotations({
  identifier: 'RecoveryEvent',
  description: 'The last_recovery event of a health envelope (client.py:2000)',
});
/** A recovery event. */
export type RecoveryEvent = typeof RecoveryEvent.Type;

/** Decode an unknown value as a {@link RecoveryEvent}, seat binding aside. */
export const decodeRecoveryEvent: TolerantDecoder<RecoveryEvent> = decodeTolerant(
  RecoveryEvent,
  'RecoveryEvent',
);

// ---------------------------------------------------------------------------
// The envelope
// ---------------------------------------------------------------------------

const HealthEnvelopeFields = Schema.Struct({
  schema_version: SchemaVersionV2,
  control_protocol: ControlProtocolV2,
  game_id: Schema.String,
  agent: HealthAgent,
  game_state: V2GameState,
  seat: HealthSeat,
  sidecar: SidecarBlock,
  observation_available: Schema.Boolean,
  legal_actions_available: Schema.Boolean,
  phase: Schema.NullOr(PhaseBlock),
  last_phase_end: Schema.NullOr(PhaseEndEvent),
  last_recovery: Schema.optionalWith(Schema.NullOr(RecoveryEvent), { exact: true }),
  ...OptionalEvaluationFields,
});

type HealthEnvelopeShape = typeof HealthEnvelopeFields.Type;

/** `max(0, max_turns - turn)` — `client.py:2291`. */
const turnsLeft = (maxTurns: number, turn: number): number => Math.max(0, maxTurns - turn);

const terminalPhaseIssues = (value: HealthEnvelopeShape): ReadonlyArray<Schema.FilterIssue> =>
  value.phase !== null && isTerminalGameState(value.game_state)
    ? [{ path: ['phase'], message: 'terminal v2 health retained stale phase state' }]
    : [];

/**
 * `client.py:2286-2292`.
 *
 * Note the Python compares with `!=` against an `int`, so a `turns_remaining`
 * of `None` on an envelope whose phase turn is known is *inconsistent*, not
 * exempt.  Reproduced: `null` fails this check rather than skipping it.
 */
const turnsRemainingIssues = (value: HealthEnvelopeShape): ReadonlyArray<Schema.FilterIssue> => {
  const evaluation = inlineEvaluation(value);
  const turn = value.phase === null ? null : value.phase.turn;
  if (evaluation === null || turn === null) return [];
  return evaluation.turns_remaining === turnsLeft(evaluation.max_turns, turn)
    ? []
    : [{ path: ['turns_remaining'], message: 'turns_remaining is inconsistent' }];
};

const seatBindingIssues = (value: HealthEnvelopeShape): ReadonlyArray<Schema.FilterIssue> => {
  const seat = value.seat;
  const end = value.last_phase_end;
  const recovery = value.last_recovery ?? null;
  const prior = value.phase === null ? null : (value.phase.prior_end ?? null);
  const endNamesThisSeat =
    end === null ||
    (end.place === seat.place &&
      end.seat_id === seat.seat_id &&
      end.player_name === seat.player_name);
  const recoveryNamesThisSeat =
    recovery === null || (recovery.seat_id === seat.seat_id && recovery.place === seat.place);
  return [
    ...(endNamesThisSeat
      ? []
      : [{ path: ['last_phase_end'], message: 'last phase end names another seat' }]),
    ...(prior === null || prior.place !== seat.place
      ? []
      : [
          {
            path: ['phase', 'prior_end', 'place'],
            message: 'prior_end must name a seat other than this one',
          },
        ]),
    ...(recoveryNamesThisSeat
      ? []
      : [{ path: ['last_recovery'], message: 'last recovery names another seat' }]),
  ];
};

/**
 * The health envelope, checked against itself.
 *
 * This schema knows nothing about *which* seat the local workspace plays — it
 * enforces only the invariants a payload can violate on its own.  Reach for
 * {@link healthEnvelopeFor} wherever a session exists; use this one for a
 * proxy, a recorder or a parity harness, which must be able to decode a health
 * envelope for a game it is not playing.
 */
export const HealthEnvelope = HealthEnvelopeFields.pipe(
  Schema.filter((value) => [
    ...inlineEvaluationIssues(value, null),
    ...terminalPhaseIssues(value),
    ...turnsRemainingIssues(value),
    ...seatBindingIssues(value),
  ]),
).annotations({
  identifier: 'HealthEnvelope',
  description: 'The full-control-v2 health envelope (client.py:2021)',
});
/** A decoded health envelope. */
export type HealthEnvelope = typeof HealthEnvelope.Type;
/** The wire form of a {@link HealthEnvelope}. */
export type HealthEnvelopeEncoded = typeof HealthEnvelope.Encoded;

/** Decode an unknown value as a {@link HealthEnvelope}, without session binding. */
export const decodeHealth: TolerantDecoder<HealthEnvelope> = decodeTolerant(
  HealthEnvelope,
  'HealthEnvelope',
);

const identityIssues = (
  value: HealthEnvelopeShape,
  session: SessionIdentity,
): ReadonlyArray<Schema.FilterIssue> => {
  // The health envelope has no top-level `agent_id`; `_validate_v2_header`
  // reads it with `"agent_id" in raw` and therefore skips it here, checking the
  // nested `agent.agent_id` a few lines later instead (client.py:2055-2057).
  const header = sessionMismatch(session, { game_id: value.game_id });
  const lastRecovery = value.last_recovery ?? null;
  return [
    ...(header === undefined ? [] : [{ path: ['game_id'], message: header }]),
    ...(value.agent.agent_id === session.agentId
      ? []
      : [{ path: ['agent', 'agent_id'], message: 'health names another agent' }]),
    ...(value.agent.controller_label === session.controllerLabel
      ? []
      : [{ path: ['agent', 'controller_label'], message: 'health names another controller' }]),
    ...(value.last_phase_end === null ||
    value.last_phase_end.controller_label === session.controllerLabel
      ? []
      : [
          {
            path: ['last_phase_end', 'controller_label'],
            message: 'last phase end names another controller',
          },
        ]),
    ...(lastRecovery === null || lastRecovery.game_id === session.gameId
      ? []
      : [{ path: ['last_recovery', 'game_id'], message: 'last recovery names another game' }]),
  ];
};

/**
 * `client.py:2067-2072` — each seat field is compared only when the session
 * knows it.  A session exists before its seat is assigned, and `place`,
 * `seat_id` and `player_name` are all `None` until `join` fills them in.
 */
const seatIdentityIssues = (
  value: HealthEnvelopeShape,
  session: SessionIdentity,
): ReadonlyArray<Schema.FilterIssue> => [
  ...(session.place === null || value.seat.place === session.place
    ? []
    : [{ path: ['seat', 'place'], message: 'health seat place changed' }]),
  ...(session.seatId === null || value.seat.seat_id === session.seatId
    ? []
    : [{ path: ['seat', 'seat_id'], message: 'health seat seat_id changed' }]),
  ...(session.playerName === null || value.seat.player_name === session.playerName
    ? []
    : [{ path: ['seat', 'player_name'], message: 'health seat player_name changed' }]),
];

/**
 * The health envelope, bound to a session.
 *
 * A schema factory rather than a decoder plus a follow-up check, so the session
 * comparisons run inside the parser and report as ordinary decode issues with
 * real paths — a caller gets one `WireDecodeError` whether the envelope was
 * malformed or merely somebody else's.
 *
 * Build it once per session and reuse it; each call constructs a fresh AST.
 */
export const healthEnvelopeFor = (
  session: SessionIdentity,
): Schema.Schema<HealthEnvelope, HealthEnvelopeEncoded> =>
  HealthEnvelope.pipe(
    Schema.filter((value) => [
      ...identityIssues(value, session),
      ...seatIdentityIssues(value, session),
      ...inlineEvaluationIssues(value, session.evaluation),
    ]),
  ).annotations({ identifier: 'HealthEnvelope' });

/** Decode a health envelope and bind it to `session`. */
export const decodeHealthFor = (session: SessionIdentity): TolerantDecoder<HealthEnvelope> =>
  decodeTolerant(healthEnvelopeFor(session), 'HealthEnvelope');
