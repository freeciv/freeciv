/**
 * The health envelope, checked against `_validate_health`
 * (`play/client.py:2021-2295`) clause by clause.
 *
 * Two kinds of assertion live here and they are deliberately different in
 * kind.  Most tests pin a *refusal*: the Python rejects this payload, so the
 * schema must too, or a drifted supervisor plays on undetected.  A smaller set
 * pins an *acceptance* — the excess-field and optional-if-present cases —
 * because those are exactly where a hand-rolled closed validator would break a
 * play surface that an additive server field should not have touched.
 */

import { describe, expect, test } from 'bun:test';
import { Either, Schema } from 'effect';
import {
  AWAITING_AGENT,
  decodeHealth,
  decodeHealthFor,
  decodePhaseEndEvent,
  decodeRecoveryEvent,
  evaluationArity,
  HealthEnvelope,
  inlineEvaluation,
  isTerminalGameState,
  PHASE_END_RECEIPT_STATES,
  PHASE_END_RESOLUTIONS,
  PHASE_END_SOURCES,
  PLAYER_COLOR_RE,
  SEAT_STANDINGS,
  unexpectedSidecarFields,
  V2_GAME_STATES,
  V2_PHASE_STATES,
  V2_RECOVERY_FIELDS,
  V2_RECOVERY_KINDS,
  V2_RECOVERY_OUTCOMES,
  V2_SIDECAR_FIELDS,
  V2_TERMINAL_GAME_STATES,
  V2_TIMING_MODES,
  WAITING_ON_KINDS,
  type SidecarBlock,
} from 'src/agent/health';
import { decodeEvaluationContext, V2_EVALUATION_FIELDS } from 'src/agent/primitives';
import { LIVE_RUN_STATES, TERMINAL_RUN_STATES } from 'src/ids';
import { encodeTolerant, formatIssuePath, type WireDecodeError } from 'src/tolerant';
import {
  AGENT_ID,
  autoEndWire,
  CONTROLLER,
  GAME_ID,
  healthWire,
  phaseEndWire,
  phaseWire,
  PLAYER_NAME,
  priorEndWire,
  recoveryWire,
  SEAT_ID,
  session,
  timingWire,
  waitingOnSeatWire,
  waitingOnWire,
  type WirePayload,
} from './health-fixtures.ts';

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const accepted = (either: Either.Either<unknown, WireDecodeError>): boolean =>
  Either.isRight(either);

const refusal = (either: Either.Either<unknown, WireDecodeError>): WireDecodeError =>
  Either.isLeft(either)
    ? either.left
    : (() => {
        throw new Error('expected the payload to be refused, but it decoded');
      })();

/** Unwrap a decode *or* an encode result; both errors carry `message`. */
const decoded = <A>(either: Either.Either<A, { readonly message: string }>): A =>
  Either.getOrThrowWith(either, (error) => new Error(error.message));

/** Every issue as `path: message`, for asserting *which* rule fired. */
const complaints = (error: WireDecodeError): ReadonlyArray<string> =>
  error.issues.map((issue) => `${formatIssuePath(issue.path)}: ${issue.message}`);

const mentions = (error: WireDecodeError, fragment: string): boolean =>
  complaints(error).some((line) => line.includes(fragment));

const bound = decodeHealthFor(session());

// Payload builders, at module scope so every `describe` shares one definition.
const withSeat = (overrides: WirePayload): WirePayload =>
  healthWire({ seat: { place: 1, seat_id: SEAT_ID, player_name: PLAYER_NAME, ...overrides } });

const withAgentLabel = (controller_label: unknown): WirePayload =>
  healthWire({ agent: { agent_id: AGENT_ID, controller_label } });

const withSidecar = (extra: WirePayload): WirePayload =>
  healthWire({ sidecar: { state: 'ready', generation: 3, ...extra } });

const withPhase = (overrides: WirePayload): WirePayload =>
  healthWire({ phase: phaseWire(overrides) });

const withTiming = (overrides: WirePayload): WirePayload =>
  withPhase({ timing: timingWire(overrides) });

const withWaitingOn = (waiting_on: unknown): WirePayload => withPhase({ waiting_on });

const withAutoEnd = (auto_end: unknown): WirePayload => withPhase({ auto_end });

const withPriorEnd = (prior_end: unknown): WirePayload => withPhase({ prior_end });

const withEnd = (last_phase_end: unknown): WirePayload => healthWire({ last_phase_end });

const withRecovery = (last_recovery: unknown): WirePayload => healthWire({ last_recovery });

/** The golden envelope plus a complete, consistent evaluation context. */
const evaluated = (overrides: WirePayload = {}): WirePayload =>
  healthWire({
    phase: phaseWire({ turn: 5 }),
    objective: 'maximize score',
    max_turns: 50,
    turns_remaining: 45,
    ...overrides,
  });

// ---------------------------------------------------------------------------
// Vocabularies
// ---------------------------------------------------------------------------

describe('vocabularies', () => {
  test('the game states are the gateway live + terminal states, no more', () => {
    expect([...V2_GAME_STATES].toSorted()).toEqual(
      [...LIVE_RUN_STATES, ...TERMINAL_RUN_STATES].toSorted(),
    );
    // The gateway synthesizes these two for archives; a live supervisor never
    // puts one on a health envelope.
    expect(V2_GAME_STATES).not.toContain('interrupted');
    expect(V2_GAME_STATES).not.toContain('unknown');
  });

  test('the terminal states match src/ids and drive isTerminalGameState', () => {
    expect([...V2_TERMINAL_GAME_STATES].toSorted()).toEqual([...TERMINAL_RUN_STATES].toSorted());
    expect(V2_TERMINAL_GAME_STATES.every(isTerminalGameState)).toBe(true);
    expect(LIVE_RUN_STATES.some(isTerminalGameState)).toBe(false);
    expect(isTerminalGameState('interrupted')).toBe(false);
  });

  test('the closed vocabularies have the sizes client.py declares', () => {
    expect(V2_PHASE_STATES).toHaveLength(8);
    expect(V2_PHASE_STATES).toContain(AWAITING_AGENT);
    expect(V2_TIMING_MODES).toHaveLength(4);
    expect(WAITING_ON_KINDS).toHaveLength(9);
    expect(SEAT_STANDINGS).toHaveLength(4);
    expect(PHASE_END_SOURCES).toHaveLength(3);
    expect(PHASE_END_RESOLUTIONS).toHaveLength(3);
    expect(V2_SIDECAR_FIELDS).toHaveLength(15);
    expect(V2_RECOVERY_FIELDS).toHaveLength(17);
    expect(V2_RECOVERY_KINDS).toHaveLength(2);
    expect(V2_RECOVERY_OUTCOMES).toHaveLength(3);
  });

  test('accepted is not a phase-end receipt state: a queued batch never ended a phase', () => {
    expect(PHASE_END_RECEIPT_STATES).toEqual(['applied', 'rejected', 'ambiguous']);
    expect([...PHASE_END_RECEIPT_STATES]).not.toContain('accepted');
  });

  test('PLAYER_COLOR_RE is upper case only and fully anchored', () => {
    expect(PLAYER_COLOR_RE.test('#3F7FBF')).toBe(true);
    expect(PLAYER_COLOR_RE.test('#3f7fbf')).toBe(false);
    expect(PLAYER_COLOR_RE.test('#3F7FB')).toBe(false);
    expect(PLAYER_COLOR_RE.test('x#3F7FBF')).toBe(false);
    expect(PLAYER_COLOR_RE.test('#3F7FBF\n')).toBe(false);
  });
});

// ---------------------------------------------------------------------------
// The golden envelope, and tolerance
// ---------------------------------------------------------------------------

describe('the golden envelope', () => {
  test('decodes unbound and bound', () => {
    expect(accepted(decodeHealth(healthWire()))).toBe(true);
    expect(accepted(bound(healthWire()))).toBe(true);
  });

  test('a field this build has never heard of survives decode and re-encode', () => {
    const payload = healthWire({ future_field: { nested: [1, 'two', null] } });
    const value = decoded(decodeHealth(payload));
    expect(JSON.stringify(decoded(encodeTolerant(HealthEnvelope, 'HealthEnvelope')(value)))).toBe(JSON.stringify(payload));
  });

  test('key order survives a decode/encode round trip', () => {
    const payload = healthWire();
    const value = decoded(decodeHealth(payload));
    const round = Either.getOrThrowWith(
      encodeTolerant(HealthEnvelope, 'HealthEnvelope')(value),
      (error) => new Error(error.message),
    );
    expect(JSON.stringify(round)).toBe(JSON.stringify(payload));
  });

  test('a missing field is still fatal — tolerance is about excess only', () => {
    const { last_phase_end: _dropped, ...withoutPhaseEnd } = healthWire();
    expect(mentions(refusal(decodeHealth(withoutPhaseEnd)), 'last_phase_end')).toBe(true);
  });

  test('errors: "all" reports every violated rule, not just the first', () => {
    const error = refusal(
      decodeHealth(healthWire({ schema_version: 1, game_state: 'imaginary' })),
    );
    expect(mentions(error, 'schema_version')).toBe(true);
    expect(mentions(error, 'game_state')).toBe(true);
  });

  test('the protocol header is a literal pair, not a version range', () => {
    expect(accepted(decodeHealth(healthWire({ schema_version: 1 })))).toBe(false);
    expect(accepted(decodeHealth(healthWire({ schema_version: 3 })))).toBe(false);
    expect(accepted(decodeHealth(healthWire({ control_protocol: 'strategic-v1' })))).toBe(false);
  });
});

// ---------------------------------------------------------------------------
// Seat and agent
// ---------------------------------------------------------------------------

describe('the seat and agent blocks', () => {
  test('place is a positive integer', () => {
    expect(accepted(decodeHealth(withSeat({ place: 1 })))).toBe(true);
    expect(accepted(decodeHealth(withSeat({ place: 0 })))).toBe(false);
    expect(accepted(decodeHealth(withSeat({ place: -1 })))).toBe(false);
    expect(accepted(decodeHealth(withSeat({ place: 1.5 })))).toBe(false);
    // Python's `isinstance(place, bool)` guard has no JavaScript counterpart —
    // `true` is simply not a number — but the verdict has to be the same.
    expect(accepted(decodeHealth(withSeat({ place: true })))).toBe(false);
  });

  test('seat_id and player_name must be non-empty strings', () => {
    expect(accepted(decodeHealth(withSeat({ seat_id: '' })))).toBe(false);
    expect(accepted(decodeHealth(withSeat({ player_name: '' })))).toBe(false);
    expect(accepted(decodeHealth(withSeat({ seat_id: 7 })))).toBe(false);
  });

  test('standing is optional-if-present and closed when present', () => {
    expect(accepted(decodeHealth(healthWire()))).toBe(true);
    expect(
      SEAT_STANDINGS.every((standing) => accepted(decodeHealth(withSeat({ standing })))),
    ).toBe(true);
    expect(accepted(decodeHealth(withSeat({ standing: 'retired' })))).toBe(false);
  });

  test('an absent standing stays absent, and a present one is kept', () => {
    expect(decoded(decodeHealth(healthWire())).seat.standing).toBeUndefined();
    expect(decoded(decodeHealth(withSeat({ standing: 'eliminated' }))).seat.standing).toBe(
      'eliminated',
    );
  });

  test('controller_label must be a non-empty string', () => {
    expect(accepted(decodeHealth(withAgentLabel('')))).toBe(false);
    expect(accepted(decodeHealth(withAgentLabel(null)))).toBe(false);
  });
});

// ---------------------------------------------------------------------------
// Sidecar
// ---------------------------------------------------------------------------

describe('the sidecar block', () => {
  test('state and generation are required by name', () => {
    expect(accepted(decodeHealth(healthWire({ sidecar: { generation: 3 } })))).toBe(false);
    expect(accepted(decodeHealth(healthWire({ sidecar: { state: 'ready' } })))).toBe(false);
  });

  test('every value must be a scalar or null', () => {
    expect(accepted(decodeHealth(withSidecar({ exit_code: null })))).toBe(true);
    expect(accepted(decodeHealth(withSidecar({ exit_code: 0 })))).toBe(true);
    expect(accepted(decodeHealth(withSidecar({ process_alive: false })))).toBe(true);
    expect(accepted(decodeHealth(withSidecar({ client_state: { nested: true } })))).toBe(false);
    expect(accepted(decodeHealth(withSidecar({ client_state: ['a'] })))).toBe(false);
  });

  test('an unknown sidecar key is PRESERVED, not fatal — and reported on request', () => {
    // client.py:2093-2100 refuses this payload outright.  @arena/wire keeps the
    // field so a proxy can forward it, and hands the same verdict to a play
    // surface through unexpectedSidecarFields.
    const payload = withSidecar({ zeta_probe: 1, alpha_probe: 'x' });
    const value = decoded(decodeHealth(payload));
    expect(unexpectedSidecarFields(value.sidecar)).toEqual(['alpha_probe', 'zeta_probe']);
    expect(JSON.stringify(decoded(encodeTolerant(HealthEnvelope, 'HealthEnvelope')(value)))).toBe(JSON.stringify(payload));
  });

  test('a sidecar of only known fields reports nothing unexpected', () => {
    const known: SidecarBlock = decoded(decodeHealth(healthWire())).sidecar;
    expect(unexpectedSidecarFields(known)).toEqual([]);
    expect(V2_SIDECAR_FIELDS).toContain('exit_signal_name');
  });
});

// ---------------------------------------------------------------------------
// The phase block
// ---------------------------------------------------------------------------

describe('the phase block', () => {
  test('a terminal game may not retain a phase', () => {
    const stale = healthWire({ game_state: 'completed' });
    expect(mentions(refusal(decodeHealth(stale)), 'stale phase state')).toBe(true);
  });

  test('a terminal game with phase: null is the expected shape', () => {
    expect(
      V2_TERMINAL_GAME_STATES.every((state) =>
        accepted(decodeHealth(healthWire({ game_state: state, phase: null }))),
      ),
    ).toBe(true);
  });

  test('a live game may also report phase: null — a clean native exit is observable', () => {
    expect(accepted(decodeHealth(healthWire({ phase: null })))).toBe(true);
  });

  test('the phase state vocabulary is closed', () => {
    expect(
      V2_PHASE_STATES.every((state) => accepted(decodeHealth(withPhase({ state })))),
    ).toBe(true);
    expect(accepted(decodeHealth(withPhase({ state: 'waiting' })))).toBe(false);
  });

  test('turn and phase are nullable non-negative integers', () => {
    expect(accepted(decodeHealth(withPhase({ turn: null })))).toBe(true);
    expect(accepted(decodeHealth(withPhase({ turn: 0 })))).toBe(true);
    expect(accepted(decodeHealth(withPhase({ turn: -1 })))).toBe(false);
    expect(accepted(decodeHealth(withPhase({ turn: 2.5 })))).toBe(false);
    expect(accepted(decodeHealth(withPhase({ phase: null })))).toBe(true);
    expect(accepted(decodeHealth(withPhase({ phase: -1 })))).toBe(false);
  });

  test('timing mode is closed and every clock is a nullable safe number', () => {
    expect(
      V2_TIMING_MODES.every((mode) => accepted(decodeHealth(withTiming({ mode })))),
    ).toBe(true);
    expect(accepted(decodeHealth(withTiming({ mode: 'turbo' })))).toBe(false);
    expect(accepted(decodeHealth(withTiming({ timeout_s: null })))).toBe(true);
    expect(accepted(decodeHealth(withTiming({ remaining_s: -1 })))).toBe(false);
    expect(accepted(decodeHealth(withTiming({ elapsed_s: Number.POSITIVE_INFINITY })))).toBe(
      false,
    );
    expect(accepted(decodeHealth(withTiming({ elapsed_s: Number.NaN })))).toBe(false);
    // A float duration is the normal case; _safe_number admits int and float.
    expect(accepted(decodeHealth(withTiming({ elapsed_s: 4.25 })))).toBe(true);
  });

  test('a timing block missing one clock is refused', () => {
    const { remaining_s: _dropped, ...partial } = timingWire();
    expect(accepted(decodeHealth(withPhase({ timing: partial })))).toBe(false);
  });
});

describe('waiting_on', () => {
  test('absent, null and populated are all valid', () => {
    expect(accepted(decodeHealth(healthWire()))).toBe(true);
    expect(accepted(decodeHealth(withWaitingOn(null)))).toBe(true);
    expect(accepted(decodeHealth(withWaitingOn(waitingOnWire())))).toBe(true);
  });

  test('the kind vocabulary is closed — an unknown kind means a stale client', () => {
    expect(
      WAITING_ON_KINDS.every((kind) =>
        accepted(decodeHealth(withWaitingOn(waitingOnWire({ kind })))),
      ),
    ).toBe(true);
    expect(accepted(decodeHealth(withWaitingOn(waitingOnWire({ kind: 'moon_phase' }))))).toBe(
      false,
    );
  });

  test('the summary must be non-empty and waiting_s a nullable safe number', () => {
    expect(accepted(decodeHealth(withWaitingOn(waitingOnWire({ summary: '' }))))).toBe(false);
    expect(accepted(decodeHealth(withWaitingOn(waitingOnWire({ waiting_s: null }))))).toBe(true);
    expect(accepted(decodeHealth(withWaitingOn(waitingOnWire({ waiting_s: -1 }))))).toBe(false);
  });

  test('seat rows have their keys checked and their values left alone', () => {
    const odd = waitingOnSeatWire({ place: 'first', is_self: null, standing: 42 });
    expect(accepted(decodeHealth(withWaitingOn(waitingOnWire({ seats: [odd] }))))).toBe(true);
    const { is_self: _dropped, ...missingKey } = waitingOnSeatWire();
    expect(
      accepted(decodeHealth(withWaitingOn(waitingOnWire({ seats: [missingKey] })))),
    ).toBe(false);
    expect(accepted(decodeHealth(withWaitingOn(waitingOnWire({ seats: {} }))))).toBe(false);
  });
});

describe('auto_end', () => {
  test('absent, null and populated are all valid', () => {
    expect(accepted(decodeHealth(withAutoEnd(null)))).toBe(true);
    expect(accepted(decodeHealth(withAutoEnd(autoEndWire())))).toBe(true);
  });

  test('enabled and armed are strictly boolean; the clocks are nullable', () => {
    expect(accepted(decodeHealth(withAutoEnd(autoEndWire({ armed: 'yes' }))))).toBe(false);
    expect(accepted(decodeHealth(withAutoEnd(autoEndWire({ enabled: 1 }))))).toBe(false);
    expect(
      accepted(decodeHealth(withAutoEnd(autoEndWire({ grace_s: null, remaining_s: null })))),
    ).toBe(true);
  });

  test('a present-but-null auto_end is distinguishable from an absent one', () => {
    expect(decoded(decodeHealth(withAutoEnd(null))).phase?.auto_end).toBeNull();
    expect(decoded(decodeHealth(healthWire())).phase?.auto_end).toBeUndefined();
  });
});

describe('prior_end', () => {
  test('it must name a seat other than this one', () => {
    expect(accepted(decodeHealth(withPriorEnd(priorEndWire())))).toBe(true);
    const selfReport = withPriorEnd(priorEndWire({ place: 1 }));
    expect(mentions(refusal(decodeHealth(selfReport)), 'seat other than this one')).toBe(true);
  });

  test('the three closed vocabularies are enforced', () => {
    expect(accepted(decodeHealth(withPriorEnd(priorEndWire({ source: 'cron' }))))).toBe(false);
    expect(
      accepted(decodeHealth(withPriorEnd(priorEndWire({ receipt_state: 'accepted' })))),
    ).toBe(false);
    expect(
      accepted(decodeHealth(withPriorEnd(priorEndWire({ resolution: 'retried' })))),
    ).toBe(false);
  });

  test('orders_submitted is null-or-non-negative, and null is not zero', () => {
    expect(
      accepted(decodeHealth(withPriorEnd(priorEndWire({ orders_submitted: 0 })))),
    ).toBe(true);
    expect(
      accepted(decodeHealth(withPriorEnd(priorEndWire({ orders_submitted: -1 })))),
    ).toBe(false);
    const value = decoded(decodeHealth(withPriorEnd(priorEndWire())));
    expect(value.phase?.prior_end?.orders_submitted).toBeNull();
  });

  test('controller_label may be null, and elapsed_s may not', () => {
    expect(
      accepted(decodeHealth(withPriorEnd(priorEndWire({ controller_label: null })))),
    ).toBe(true);
    expect(
      accepted(decodeHealth(withPriorEnd(priorEndWire({ elapsed_s: null })))),
    ).toBe(false);
  });
});

// ---------------------------------------------------------------------------
// last_phase_end
// ---------------------------------------------------------------------------

describe('last_phase_end', () => {
  test('null and a well-formed event are both valid', () => {
    expect(accepted(decodeHealth(healthWire()))).toBe(true);
    expect(accepted(decodeHealth(withEnd(phaseEndWire())))).toBe(true);
    expect(accepted(bound(withEnd(phaseEndWire())))).toBe(true);
  });

  test('it must name the envelope’s own seat', () => {
    expect(mentions(refusal(decodeHealth(withEnd(phaseEndWire({ place: 2 })))), 'another seat')).toBe(
      true,
    );
    expect(
      mentions(refusal(decodeHealth(withEnd(phaseEndWire({ seat_id: 'seat_two' })))), 'another seat'),
    ).toBe(true);
    expect(
      mentions(refusal(decodeHealth(withEnd(phaseEndWire({ player_name: 'Bob' })))), 'another seat'),
    ).toBe(true);
  });

  test('controller_type is the literal "external"', () => {
    expect(
      accepted(decodeHealth(withEnd(phaseEndWire({ controller_type: 'native_classic_ai' })))),
    ).toBe(false);
  });

  test('player_color is upper-case #RRGGBB', () => {
    expect(accepted(decodeHealth(withEnd(phaseEndWire({ player_color: '#3f7fbf' }))))).toBe(false);
    expect(accepted(decodeHealth(withEnd(phaseEndWire({ player_color: 'blue' }))))).toBe(false);
  });

  test('a rejected receipt must have resolved as failed', () => {
    const contradiction = phaseEndWire({ receipt_state: 'rejected', resolution: 'advanced' });
    expect(mentions(refusal(decodeHealth(withEnd(contradiction))), 'resolve as failed')).toBe(true);
    const consistent = phaseEndWire({ receipt_state: 'rejected', resolution: 'failed' });
    expect(accepted(decodeHealth(withEnd(consistent)))).toBe(true);
  });

  test('a phase cannot have ended before its deadline started', () => {
    const backwards = phaseEndWire({ deadline_started_at: 1000, ended_at: 999 });
    expect(mentions(refusal(decodeHealth(withEnd(backwards))), 'before its deadline')).toBe(true);
    // Equal is fine: a zero-length phase is legal, if odd.
    expect(
      accepted(decodeHealth(withEnd(phaseEndWire({ deadline_started_at: 1000, ended_at: 1000 })))),
    ).toBe(true);
  });

  test('sequence starts at one', () => {
    expect(accepted(decodeHealth(withEnd(phaseEndWire({ sequence: 0 }))))).toBe(false);
    expect(accepted(decodeHealth(withEnd(phaseEndWire({ sequence: 1 }))))).toBe(true);
  });

  test('incarnation and orders_submitted are optional-if-present', () => {
    expect(accepted(decodePhaseEndEvent(phaseEndWire()))).toBe(true);
    expect(accepted(decodePhaseEndEvent(phaseEndWire({ incarnation: 0 })))).toBe(true);
    expect(accepted(decodePhaseEndEvent(phaseEndWire({ incarnation: -1 })))).toBe(false);
    expect(accepted(decodePhaseEndEvent(phaseEndWire({ orders_submitted: null })))).toBe(true);
    expect(accepted(decodePhaseEndEvent(phaseEndWire({ orders_submitted: 3 })))).toBe(true);
    expect(accepted(decodePhaseEndEvent(phaseEndWire({ orders_submitted: -1 })))).toBe(false);
  });

  test('the standalone decoder does not know about a seat', () => {
    // Seat binding is an envelope-level rule, because that is where the seat is.
    expect(accepted(decodePhaseEndEvent(phaseEndWire({ place: 9 })))).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// last_recovery
// ---------------------------------------------------------------------------

describe('last_recovery', () => {
  test('absent, null and populated are all valid', () => {
    expect(decoded(decodeHealth(healthWire())).last_recovery).toBeUndefined();
    expect(decoded(decodeHealth(withRecovery(null))).last_recovery).toBeNull();
    expect(accepted(bound(withRecovery(recoveryWire())))).toBe(true);
  });

  test('the five typed fields are enforced', () => {
    expect(accepted(decodeRecoveryEvent(recoveryWire({ kind: 'restart' })))).toBe(false);
    expect(accepted(decodeRecoveryEvent(recoveryWire({ outcome: 'partial' })))).toBe(false);
    expect(accepted(decodeRecoveryEvent(recoveryWire({ trigger: '' })))).toBe(false);
    expect(accepted(decodeRecoveryEvent(recoveryWire({ timestamp: '' })))).toBe(false);
    expect(
      accepted(decodeRecoveryEvent(recoveryWire({ rewound_applied_actions: 'no' }))),
    ).toBe(false);
  });

  test('the twelve untyped fields really are untyped', () => {
    const odd = recoveryWire({
      attempt: 'third',
      exit_code: { signal: 9 },
      recovered_to_turn: null,
      sidecar_generation: [1, 2],
      turn: false,
    });
    expect(accepted(decodeRecoveryEvent(odd))).toBe(true);
  });

  test('all seventeen keys are required', () => {
    const { format: _dropped, ...partial } = recoveryWire();
    expect(accepted(decodeRecoveryEvent(partial))).toBe(false);
  });

  test('it must name this seat, and (bound) this game', () => {
    expect(
      mentions(refusal(decodeHealth(withRecovery(recoveryWire({ place: 2 })))), 'another seat'),
    ).toBe(true);
    expect(
      mentions(
        refusal(decodeHealth(withRecovery(recoveryWire({ seat_id: 'seat_two' })))),
        'another seat',
      ),
    ).toBe(true);
    const otherGame = withRecovery(recoveryWire({ game_id: `${GAME_ID}_other` }));
    expect(accepted(decodeHealth(otherGame))).toBe(true);
    expect(mentions(refusal(bound(otherGame)), 'another game')).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// The inline evaluation context
// ---------------------------------------------------------------------------

describe('the inline evaluation context', () => {
  test('the three keys are the ones primitives.ts names', () => {
    expect([...V2_EVALUATION_FIELDS].toSorted()).toEqual([
      'max_turns',
      'objective',
      'turns_remaining',
    ]);
  });

  test('a complete, consistent triple decodes', () => {
    expect(accepted(decodeHealth(evaluated()))).toBe(true);
    expect(inlineEvaluation(decoded(decodeHealth(evaluated())))).toEqual({
      objective: 'maximize score',
      max_turns: 50,
      turns_remaining: 45,
    });
  });

  test('a partial triple is drift, not an older server', () => {
    expect(evaluationArity(decoded(decodeHealth(healthWire())))).toBe(0);
    expect(evaluationArity(decoded(decodeHealth(evaluated())))).toBe(3);
    const partial = healthWire({
      phase: phaseWire({ turn: 5 }),
      objective: 'maximize score',
      max_turns: 50,
    });
    expect(mentions(refusal(decodeHealth(partial)), 'incomplete')).toBe(true);
  });

  test('turns_remaining must equal max(0, max_turns - turn)', () => {
    expect(mentions(refusal(decodeHealth(evaluated({ turns_remaining: 44 }))), 'inconsistent')).toBe(
      true,
    );
    // A turn past the budget clamps at zero rather than going negative.
    expect(
      accepted(decodeHealth(evaluated({ max_turns: 3, turns_remaining: 0 }))),
    ).toBe(true);
  });

  test('a null turns_remaining is inconsistent, not exempt, when the turn is known', () => {
    // Python compares `None != max(0, ...)`, which is True — the payload is
    // refused rather than skipped.
    expect(
      mentions(refusal(decodeHealth(evaluated({ turns_remaining: null }))), 'inconsistent'),
    ).toBe(true);
    // With no phase turn to compare against, null is fine.
    expect(
      accepted(decodeHealth(evaluated({ phase: phaseWire({ turn: null }), turns_remaining: null }))),
    ).toBe(true);
  });

  test('the field rules match the nested EvaluationContext in primitives.ts', () => {
    const samples: ReadonlyArray<Record<string, unknown>> = [
      { objective: 'maximize score', max_turns: 50, turns_remaining: 45 },
      { objective: ' padded', max_turns: 50, turns_remaining: 45 },
      { objective: '', max_turns: 50, turns_remaining: 45 },
      { objective: 'ok', max_turns: 0, turns_remaining: 0 },
      { objective: 'ok', max_turns: 5001, turns_remaining: 0 },
      { objective: 'ok', max_turns: 50, turns_remaining: 51 },
      { objective: 'ok', max_turns: 50, turns_remaining: -1 },
      { objective: 'ok', max_turns: 50.5, turns_remaining: 0 },
    ];
    const inline = samples.map((sample) =>
      accepted(
        decodeHealth(
          healthWire({ ...sample, phase: phaseWire({ turn: null }) }),
        ),
      ),
    );
    const nested = samples.map((sample) => accepted(decodeEvaluationContext(sample)));
    expect(inline).toEqual(nested);
  });

  test('a session with an evaluation makes the context mandatory and pins its terms', () => {
    const evaluation = { objective: 'maximize score', max_turns: 50, turns_remaining: 45 };
    const strict = decodeHealthFor(session({ evaluation }));
    expect(accepted(strict(evaluated()))).toBe(true);
    expect(mentions(refusal(strict(healthWire())), 'missing')).toBe(true);
    expect(
      mentions(refusal(strict(evaluated({ objective: 'survive' }))), 'objective changed'),
    ).toBe(true);
    expect(
      mentions(
        refusal(strict(evaluated({ max_turns: 60, turns_remaining: 55 }))),
        'max_turns changed',
      ),
    ).toBe(true);
    // turns_remaining is the one term that is meant to move.
    expect(
      accepted(strict(evaluated({ phase: phaseWire({ turn: 10 }), turns_remaining: 40 }))),
    ).toBe(true);
  });

  test('without a session evaluation, a context is optional but still checked', () => {
    expect(accepted(decodeHealthFor(session())(healthWire()))).toBe(true);
    expect(accepted(decodeHealthFor(session())(evaluated()))).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// Session binding
// ---------------------------------------------------------------------------

describe('session binding', () => {
  test('an envelope for another game is refused', () => {
    const other = healthWire({ game_id: 'game_someone_elses_run_00000' });
    expect(accepted(decodeHealth(other))).toBe(true);
    expect(mentions(refusal(bound(other)), 'another game')).toBe(true);
  });

  test('an envelope for another agent or controller is refused', () => {
    const otherAgent = healthWire({
      agent: { agent_id: 'agent_ffffffffffffffff', controller_label: CONTROLLER },
    });
    expect(mentions(refusal(bound(otherAgent)), 'another agent')).toBe(true);
    const otherController = healthWire({
      agent: { agent_id: AGENT_ID, controller_label: 'someone-else' },
    });
    expect(mentions(refusal(bound(otherController)), 'another controller')).toBe(true);
  });

  test('a phase end attributed to another controller is refused', () => {
    const payload = healthWire({ last_phase_end: phaseEndWire({ controller_label: 'other' }) });
    expect(accepted(decodeHealth(payload))).toBe(true);
    expect(mentions(refusal(bound(payload)), 'another controller')).toBe(true);
  });

  test('each seat field is compared only when the session knows it', () => {
    const moved = healthWire({
      seat: { place: 2, seat_id: 'seat_two', player_name: 'Bob' },
      phase: phaseWire(),
    });
    expect(accepted(bound(moved))).toBe(false);
    const unseated = decodeHealthFor(session({ place: null, seatId: null, playerName: null }));
    expect(accepted(unseated(moved))).toBe(true);
  });

  test('the bound schema is a schema, so it composes and re-encodes', () => {
    const schema = Schema.asSchema(HealthEnvelope);
    expect(accepted(decodeHealth(healthWire()))).toBe(true);
    expect(Schema.is(schema)(decoded(decodeHealth(healthWire())))).toBe(true);
  });
});
