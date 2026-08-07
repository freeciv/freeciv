/**
 * The phase lines every command shares.
 *
 * Ports the phase-header assertions embedded in the Python's turn/wait/monitor
 * tests — `test_health_leads_with_whose_turn_it_is_and_names_them`,
 * `test_health_on_your_own_turn_says_so_without_a_holder`,
 * `test_an_active_phase_that_cannot_be_ended_is_not_called_your_turn`,
 * `test_a_timeout_with_no_orders_says_they_issued_no_orders`
 * (test_client.py:11398-11480) and
 * `test_a_timeout_wake_names_the_holder_instead_of_calling_it_a_wake`
 * (11145-11162).
 *
 * Every expectation reads a constant from `test/_fixtures/phase-goldens`, which
 * is the point: U05/U12/U16/U17 assert against the same constants, so a change
 * to one unit's header cannot silently diverge from the other four.
 */
import { describe, expect, test } from 'bun:test';
import { Effect } from 'effect';
import { holderSeat, phaseHeadline, phaseText, priorEndLine, seatLabel } from 'src/render/phase';
import {
  turnHealthContext,
  turnHealthEpoch,
  turnHealthEpochsEqual,
} from 'src/services/health-context';
import { decodeHealth, type HealthEnvelope, type JsonObject } from 'src/schema/index';
import { identity } from 'test/_fixtures';
import {
  OPPONENT_CONTROLLER,
  PHASE_HEADLINE,
  PHASE_TEXT,
  PRIOR_END,
  opponentSeat,
  phaseHealthPayload,
  priorEndPayload,
} from 'test/_fixtures/phase-goldens';

const decode = (payload: JsonObject): HealthEnvelope =>
  Effect.runSync(decodeHealth(payload, identity()));

const headlineOf = (payload: JsonObject): string => phaseHeadline(decode(payload).phase);

describe('holderSeat', () => {
  test('a phase this seat holds never names a holder', () => {
    const health = decode(phaseHealthPayload({ mine: true }));
    expect(holderSeat(health.phase)).toBeNull();
  });

  test('a phase with no waiting_on block names nobody', () => {
    // `mine: true` is the only shape the fixture builds without `waiting_on`;
    // flip `active` afterwards so the *only* difference is the missing block.
    const health = decode(phaseHealthPayload({ mine: true }));
    const phase = health.phase;
    expect(phase).not.toBeNull();
    expect(holderSeat(phase === null ? null : { ...phase, active: false })).toBeNull();
  });

  test('exactly one other seat is a holder; two is not', () => {
    const one = decode(phaseHealthPayload({ mine: false }));
    expect(holderSeat(one.phase)?.player_name).toBe('AgentPlace2');
    const two = decode(
      phaseHealthPayload({ mine: false, waitingSeats: [opponentSeat(2), opponentSeat(3)] })
    );
    expect(holderSeat(two.phase)).toBeNull();
  });

  test('is_self must be exactly false, not merely falsy', () => {
    const drifted = decode(
      phaseHealthPayload({
        mine: false,
        waitingSeats: [{ ...opponentSeat(2), is_self: null }],
      })
    );
    expect(holderSeat(drifted.phase)).toBeNull();
  });

  test('an absent phase names nobody', () => {
    expect(holderSeat(null)).toBeNull();
  });
});

describe('seatLabel', () => {
  test('the controller label is parenthesised when it is a non-empty string', () => {
    expect(seatLabel({ place: 2, player_name: 'AgentPlace2', controller_label: OPPONENT_CONTROLLER }))
      .toBe('seat 2 AgentPlace2 (pi-gpt-5.6-sol)');
  });

  test('an absent or empty controller label leaves the seat bare', () => {
    expect(seatLabel({ place: 2, player_name: 'AgentPlace2', controller_label: null })).toBe(
      'seat 2 AgentPlace2'
    );
    expect(seatLabel({ place: 2, player_name: 'AgentPlace2', controller_label: '' })).toBe(
      'seat 2 AgentPlace2'
    );
  });

  test('a missing scalar renders as the never-empty cell, not as blank', () => {
    expect(seatLabel({ place: null, player_name: null, controller_label: null })).toBe('seat - -');
  });
});

describe('phaseHeadline', () => {
  test('no phase at all', () => {
    expect(phaseHeadline(null)).toBe(PHASE_HEADLINE.none);
    expect(headlineOf(phaseHealthPayload({ mine: false, gameState: 'completed' }))).toBe(
      PHASE_HEADLINE.none
    );
  });

  test('your own turn leads with YOUR TURN and prints your deadline', () => {
    const line = headlineOf(phaseHealthPayload({ mine: true, remainingS: 592, elapsedS: 8 }));
    expect(line).toBe(PHASE_HEADLINE.yourTurn);
    expect(line).not.toContain('NOT YOUR TURN');
  });

  test('an active phase that cannot be ended is YOUR PHASE, not YOUR TURN', () => {
    // `active` and `actionable` are not the same fact, and never were.
    const line = headlineOf(
      phaseHealthPayload({
        mine: true,
        remainingS: 592,
        elapsedS: 8,
        phaseState: 'phase_not_ready',
      })
    );
    expect(line).toBe(PHASE_HEADLINE.yourPhaseNotReady);
    expect(line).not.toContain('YOUR TURN');
  });

  test('another seat holding it is named, with how long it has held and how long is left', () => {
    expect(headlineOf(phaseHealthPayload({ mine: false, remainingS: 13, elapsedS: 587 }))).toBe(
      PHASE_HEADLINE.holder
    );
  });

  test('a phase held by nobody the supervisor will name says only that it is waiting', () => {
    expect(
      headlineOf(phaseHealthPayload({ mine: false, remainingS: 13, elapsedS: 587, waitingSeats: [] }))
    ).toBe(PHASE_HEADLINE.unnamedHolder);
  });

  test('an untimed phase says "no deadline" instead of inventing one', () => {
    expect(
      headlineOf(
        phaseHealthPayload({
          mine: false,
          mode: 'infinite',
          remainingS: null,
          timeoutS: null,
          waitingSeats: [],
        })
      )
    ).toBe(PHASE_HEADLINE.infiniteUnnamed);
  });

  test('a held phase with no elapsed reading drops the held clause, not the whole line', () => {
    expect(
      headlineOf(
        phaseHealthPayload({ mine: false, remainingS: 13, elapsedS: null, timeoutS: null })
      )
    ).toBe('NOT YOUR TURN · seat 2 AgentPlace2 (pi-gpt-5.6-sol) holds t3/p1 · 13s left');
  });

  test('your turn without a deadline prints no budget, because there is none to spend', () => {
    expect(
      headlineOf(
        phaseHealthPayload({ mine: true, mode: 'infinite', remainingS: null, timeoutS: 600 })
      )
    ).toBe('YOUR TURN · t3/p1 · no deadline');
  });
});

describe('phaseText', () => {
  test('the flat form the mirror and the older surfaces carry', () => {
    expect(phaseText(null)).toBe(PHASE_TEXT.none);
    expect(phaseText(decode(phaseHealthPayload({ mine: true, remainingS: 592 })).phase)).toBe(
      PHASE_TEXT.yourTurn
    );
    expect(
      phaseText(decode(phaseHealthPayload({ mine: false, remainingS: 13, elapsedS: 587 })).phase)
    ).toBe(PHASE_TEXT.holder);
    expect(
      phaseText(
        decode(
          phaseHealthPayload({ mine: false, mode: 'infinite', remainingS: null, timeoutS: null })
        ).phase
      )
    ).toBe(PHASE_TEXT.infinite);
  });
});

describe('priorEndLine', () => {
  test('a phase with no prior_end says nothing at all', () => {
    expect(priorEndLine(null)).toBe('');
    expect(priorEndLine(decode(phaseHealthPayload({ mine: true })).phase)).toBe('');
  });

  /**
   * The difference between thinking for ten minutes and not being there.
   *
   * In the match this came from one seat submitted nothing on nine of thirteen
   * turns and every surface reported a played turn.
   */
  test.each([
    ['timeout', 0, 600.0, PRIOR_END.timeoutNoOrders],
    ['timeout', 6, 600.0, PRIOR_END.timeoutSomeOrders],
    ['timeout', null, 600.0, PRIOR_END.timeoutUnknownOrders],
    ['auto_idle', 0, 600.0, PRIOR_END.autoIdle],
    ['agent', 7, 32.0, PRIOR_END.agentSevenOrders],
    ['agent', 1, 32.0, PRIOR_END.agentOneOrder],
  ] as ReadonlyArray<readonly [string, number | null, number, string]>)(
    'source=%s orders=%p',
    (source, orders, elapsedS, expected) => {
      const health = decode(
        phaseHealthPayload({ mine: true, priorEnd: priorEndPayload(source, orders, elapsedS) })
      );
      expect(priorEndLine(health.phase)).toBe(expected);
    }
  );

  test('an agent end with no order count still names the agent', () => {
    const health = decode(
      phaseHealthPayload({ mine: true, priorEnd: priorEndPayload('agent', null, 32.0) })
    );
    expect(priorEndLine(health.phase)).toBe(
      'opponent seat 2 AgentPlace2 (pi-gpt-5.6-sol) ended t3/p0 in 32s (agent)'
    );
  });
});

describe('the turn health epoch', () => {
  test('the seven fields that decide "still the same phase"', () => {
    const health = decode(phaseHealthPayload({ mine: true }));
    expect(turnHealthEpoch(health)).toEqual(['running', true, true, 'awaiting_agent', 3, 1, true]);
  });

  test('a phaseless health reads as four nulls, not as a crash', () => {
    const health = decode(phaseHealthPayload({ mine: false, gameState: 'completed' }));
    expect(turnHealthEpoch(health)).toEqual([
      'completed',
      false,
      false,
      null,
      null,
      null,
      null,
    ]);
  });

  test('two reads of one phase are equal; a re-read after the phase flips is not', () => {
    const before = decode(phaseHealthPayload({ mine: true, remainingS: 592 }));
    // Only the deadline moved: the same phase, so the header is not reprinted.
    const later = decode(phaseHealthPayload({ mine: true, remainingS: 41 }));
    const other = decode(phaseHealthPayload({ mine: false }));
    expect(turnHealthEpochsEqual(turnHealthEpoch(before), turnHealthEpoch(later))).toBe(true);
    expect(turnHealthEpochsEqual(turnHealthEpoch(before), turnHealthEpoch(other))).toBe(false);
  });
});

describe('the turn health context', () => {
  test('it carries exactly the eleven keys `--json` embeds', () => {
    const health = decode(phaseHealthPayload({ mine: true }));
    expect(Object.keys(turnHealthContext(health)).sort()).toEqual([
      'agent',
      'game_state',
      'last_phase_end',
      'legal_actions_available',
      'max_turns',
      'objective',
      'observation_available',
      'phase',
      'seat',
      'sidecar',
      'turns_remaining',
    ]);
  });

  test('an unevaluated game carries nulls, not missing keys', () => {
    const context = turnHealthContext(decode(phaseHealthPayload({ mine: true })));
    expect(context.objective).toBeNull();
    expect(context.max_turns).toBeNull();
    expect(context.turns_remaining).toBeNull();
    expect(context.phase?.state).toBe('awaiting_agent');
  });

  test('the evaluation triple passes through when the server sent it', () => {
    const evaluation = {
      objective: 'Maximize final score',
      max_turns: 5000,
      turns_remaining: 4997,
    };
    const health = Effect.runSync(
      decodeHealth(
        { ...phaseHealthPayload({ mine: true }), ...evaluation },
        identity({ evaluation })
      )
    );
    const context = turnHealthContext(health);
    expect(context.objective).toBe('Maximize final score');
    expect(context.max_turns).toBe(5000);
    expect(context.turns_remaining).toBe(4997);
  });
});
