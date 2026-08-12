/**
 * The wait envelope, checked against `_validate_wait_response`
 * (`play/client.py:2298-2355`).
 *
 * The shape is the easy half.  What these tests are really pinning is the wake
 * *contract*: `wait` is the one command whose exit code is a claim about the
 * world, and a server that says `phase_active` while its own embedded health
 * says the phase is still synchronizing has told the caller something no later
 * command can catch.  Every clause of `client.py:2326-2345` gets a test that
 * fails the moment the clause stops being enforced.
 */

import { describe, expect, test } from 'bun:test';
import { Either } from 'effect';
import { decodeWait, decodeWaitFor, isSatisfiedWake, V2_WAKE_REASONS, WAIT_UNTILS } from 'src/agent/wait';
import { encodeTolerant, formatIssuePath, type WireDecodeError } from 'src/tolerant';
import { WaitEnvelope } from 'src/agent/wait';
import {
  AGENT_ID,
  GAME_ID,
  healthWire,
  OTHER_STATE_TOKEN,
  phaseWire,
  revisionWire,
  session,
  STATE_TOKEN,
  waitWire,
  type WirePayload,
} from './health-fixtures.ts';

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

const mentions = (error: WireDecodeError, fragment: string): boolean =>
  error.issues.some(
    (issue) => `${formatIssuePath(issue.path)}: ${issue.message}`.includes(fragment),
  );

/** The two questions a caller can ask, as ready-made decoders. */
const untilPhase = decodeWaitFor(session(), { until: 'phase', afterStateToken: null });
const untilRevision = decodeWaitFor(session(), {
  until: 'revision',
  afterStateToken: STATE_TOKEN,
});

/** A `revision_changed` answer carrying a token the caller had not seen. */
const changed = (overrides: WirePayload = {}): WirePayload =>
  waitWire({
    wake_reason: 'revision_changed',
    state_revision: revisionWire({ state_token: OTHER_STATE_TOKEN }),
    ...overrides,
  });

/** A contract-neutral `timeout` answer carrying a revision. */
const withRevision = (overrides: WirePayload): WirePayload =>
  waitWire({ wake_reason: 'timeout', state_revision: revisionWire(overrides) });

// ---------------------------------------------------------------------------
// Vocabulary and shape
// ---------------------------------------------------------------------------

describe('the wake vocabulary', () => {
  test('five reasons, three of which mean the wait was satisfied', () => {
    expect(V2_WAKE_REASONS).toHaveLength(5);
    expect(V2_WAKE_REASONS.filter(isSatisfiedWake)).toEqual([
      'phase_active',
      'revision_changed',
      'boundary_recovered',
    ]);
    expect(isSatisfiedWake('timeout')).toBe(false);
    expect(isSatisfiedWake('game_terminal')).toBe(false);
  });

  test('a wait is asked for a phase or for a revision, and nothing else', () => {
    expect(WAIT_UNTILS).toEqual(['phase', 'revision']);
  });

  test('an unknown wake reason is refused outright', () => {
    expect(accepted(decodeWait(waitWire({ wake_reason: 'because' })))).toBe(false);
  });
});

describe('the golden envelope', () => {
  test('decodes unbound, and bound to the question that produced it', () => {
    expect(accepted(decodeWait(waitWire()))).toBe(true);
    expect(accepted(untilPhase(waitWire()))).toBe(true);
  });

  test('the protocol header is a literal pair', () => {
    expect(accepted(decodeWait(waitWire({ schema_version: 1 })))).toBe(false);
    expect(accepted(decodeWait(waitWire({ control_protocol: 'strategic-v1' })))).toBe(false);
  });

  test('a missing field is fatal; an unknown one is preserved and re-encoded', () => {
    const { state_revision: _dropped, ...partial } = waitWire();
    expect(accepted(decodeWait(partial))).toBe(false);

    const payload = waitWire({ server_hint: 'come back sooner' });
    const value = decoded(decodeWait(payload));
    expect(JSON.stringify(decoded(encodeTolerant(WaitEnvelope, 'WaitEnvelope')(value)))).toBe(JSON.stringify(payload));
  });

  test('a malformed embedded health sinks the whole envelope, with a path', () => {
    const broken = waitWire({ health: healthWire({ game_state: 'imaginary' }) });
    expect(mentions(refusal(decodeWait(broken)), 'health.game_state')).toBe(true);
  });

  test('the embedded health is session-bound only in the bound decoder', () => {
    const foreign = waitWire({ health: healthWire({ game_id: 'game_someone_elses_run_00000' }) });
    expect(accepted(decodeWait(foreign))).toBe(true);
    expect(mentions(refusal(untilPhase(foreign)), 'another game')).toBe(true);
  });

  test('an envelope addressed to another agent is refused', () => {
    const other = waitWire({ agent_id: 'agent_ffffffffffffffff' });
    expect(accepted(decodeWait(other))).toBe(true);
    expect(mentions(refusal(untilPhase(other)), 'another agent')).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// The wake contract — client.py:2326-2345
// ---------------------------------------------------------------------------

describe('rule 1: a phase wait never carries a revision', () => {
  test('a revision on an --until phase answer is a different wait’s answer', () => {
    const payload = waitWire({ wake_reason: 'timeout', state_revision: revisionWire() });
    expect(accepted(decodeWait(payload))).toBe(true);
    expect(mentions(refusal(untilPhase(payload)), 'does not carry a revision')).toBe(true);
  });

  test('the same answer is fine for an --until revision wait', () => {
    const payload = waitWire({ wake_reason: 'timeout', state_revision: revisionWire() });
    expect(accepted(untilRevision(payload))).toBe(true);
  });
});

describe('rule 2: phase_active must mean the seat may act now', () => {
  test('the golden phase_active wake is honest', () => {
    expect(accepted(untilPhase(waitWire()))).toBe(true);
  });

  test('a phase that is not awaiting_agent is not this seat’s to play', () => {
    const payload = waitWire({
      health: healthWire({ phase: phaseWire({ state: 'synchronizing' }) }),
    });
    expect(mentions(refusal(untilPhase(payload)), 'not this seat')).toBe(true);
  });

  test('an inactive phase is not playable either', () => {
    const payload = waitWire({ health: healthWire({ phase: phaseWire({ active: false }) }) });
    expect(mentions(refusal(untilPhase(payload)), 'not this seat')).toBe(true);
  });

  test('a null phase cannot be active', () => {
    const payload = waitWire({ health: healthWire({ phase: null }) });
    expect(mentions(refusal(untilPhase(payload)), 'not this seat')).toBe(true);
  });

  test('a phase with no observation to act on is not actionable', () => {
    const payload = waitWire({ health: healthWire({ observation_available: false }) });
    expect(mentions(refusal(untilPhase(payload)), 'not this seat')).toBe(true);
  });

  test('phase_active never answers an --until revision wait', () => {
    expect(mentions(refusal(untilRevision(waitWire())), 'not this seat')).toBe(true);
  });
});

describe('rule 3: game_terminal must mean the game is terminal', () => {
  test('a terminal game_state with no phase satisfies it', () => {
    const payload = waitWire({
      wake_reason: 'game_terminal',
      health: healthWire({ game_state: 'completed', phase: null }),
    });
    expect(accepted(untilPhase(payload))).toBe(true);
  });

  test('a running game does not', () => {
    const payload = waitWire({ wake_reason: 'game_terminal' });
    expect(mentions(refusal(untilPhase(payload)), 'not terminal')).toBe(true);
  });

  test('the claim is checked for every non-terminal state', () => {
    const lobby = waitWire({
      wake_reason: 'game_terminal',
      health: healthWire({ game_state: 'lobby' }),
    });
    expect(accepted(untilPhase(lobby))).toBe(false);
  });
});

describe('rule 4: revision_changed must show a revision past the requested token', () => {
  test('a different token on an --until revision wait is honest', () => {
    expect(accepted(untilRevision(changed()))).toBe(true);
  });

  test('the same token the caller already knew is not a change', () => {
    const payload = changed({ state_revision: revisionWire({ state_token: STATE_TOKEN }) });
    expect(mentions(refusal(untilRevision(payload)), 'past the requested token')).toBe(true);
  });

  test('a null revision cannot have changed', () => {
    expect(
      mentions(refusal(untilRevision(changed({ state_revision: null }))), 'past the requested token'),
    ).toBe(true);
  });

  test('with no token to compare against, the claim is unverifiable and so refused', () => {
    const blind = decodeWaitFor(session(), { until: 'revision', afterStateToken: null });
    expect(mentions(refusal(blind(changed())), 'past the requested token')).toBe(true);
  });

  test('revision_changed never answers an --until phase wait', () => {
    const error = refusal(untilPhase(changed()));
    // Both rule 1 and rule 4 fire, and `errors: "all"` reports both.
    expect(mentions(error, 'does not carry a revision')).toBe(true);
    expect(mentions(error, 'past the requested token')).toBe(true);
  });
});

describe('timeout and boundary_recovered claim nothing beyond rule 1', () => {
  test('either may answer a phase wait whose phase is not playable', () => {
    const quiet = healthWire({ phase: phaseWire({ state: 'native_phase', active: false }) });
    expect(accepted(untilPhase(waitWire({ wake_reason: 'timeout', health: quiet })))).toBe(true);
    expect(
      accepted(untilPhase(waitWire({ wake_reason: 'boundary_recovered', health: quiet }))),
    ).toBe(true);
  });

  test('either may answer a revision wait with or without a revision', () => {
    expect(accepted(untilRevision(waitWire({ wake_reason: 'timeout' })))).toBe(true);
    expect(
      accepted(
        untilRevision(
          waitWire({ wake_reason: 'boundary_recovered', state_revision: revisionWire() }),
        ),
      ),
    ).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// state_revision
// ---------------------------------------------------------------------------

describe('the state_revision block', () => {
  test('counters are non-negative integers and the token is opaque', () => {
    expect(accepted(untilRevision(withRevision({ turn: 0, revision: 0 })))).toBe(true);
    expect(accepted(untilRevision(withRevision({ turn: -1 })))).toBe(false);
    expect(accepted(untilRevision(withRevision({ revision: 1.5 })))).toBe(false);
    expect(accepted(untilRevision(withRevision({ state_token: '' })))).toBe(false);
    expect(accepted(untilRevision(withRevision({ state_token: '/etc/passwd' })))).toBe(false);
  });

  test('the decoded envelope keeps the wire’s own key names', () => {
    const value = decoded(
      untilRevision(waitWire({ wake_reason: 'timeout', state_revision: revisionWire() })),
    );
    expect(value.game_id).toBe(GAME_ID);
    expect(value.agent_id).toBe(AGENT_ID);
    expect(value.state_revision?.state_token).toBe(STATE_TOKEN);
    expect(value.health.seat.place).toBe(1);
  });
});
