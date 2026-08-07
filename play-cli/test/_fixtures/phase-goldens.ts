/**
 * The shared truth for every phase-derived line the CLI prints.
 *
 * `health`, `turn`, `do --end --await`, `wait` and `monitor` all emit
 * `phaseHeadline` and `priorEndLine` **verbatim** (PORT_MAP §2, U06).  If each
 * of those units wrote its own expected strings, four of them could drift from
 * the fifth and every suite would still be green — which is exactly how the
 * Python's ancestor shipped a `wait` that disagreed with `health` about whose
 * turn it was.  So the bytes live here once and U05/U12/U16/U17 assert against
 * these constants rather than re-deriving them.
 *
 * Ownership: this file is U06's (see NOTES.md §U06 and PORT_MAP §0's addendum
 * row).  Add a golden here; do not copy one into a unit's test file.
 */
import type { JsonObject } from 'src/schema/index';
import { healthPayload } from 'test/_fixtures/wire';

// ---------------------------------------------------------------------------
// Payload builders — the Python's PvPWaitInteropTests.health, ported
// ---------------------------------------------------------------------------

/** The controller label the other seat plays under in every PvP fixture. */
export const OPPONENT_CONTROLLER = 'pi-gpt-5.6-sol';
export const OPPONENT_PLAYER = 'AgentPlace2';

/** One `waiting_on.seats[]` row for a seat that is not this one. */
export const opponentSeat = (place = 2): JsonObject => ({
  place,
  seat_id: `place-${place}`,
  player_name: OPPONENT_PLAYER,
  controller_label: OPPONENT_CONTROLLER,
  standing: 'active',
  is_self: false,
});

export interface PhaseHealthOptions {
  /** Is the phase this seat's?  `false` adds the `waiting_on` block. */
  readonly mine: boolean;
  readonly remainingS?: number | null;
  readonly elapsedS?: number | null;
  readonly timeoutS?: number | null;
  readonly mode?: string;
  readonly gameState?: string;
  readonly phaseState?: string;
  readonly turn?: number;
  readonly priorEnd?: JsonObject;
  readonly autoEnd?: JsonObject;
  readonly waitingSeats?: ReadonlyArray<JsonObject>;
}

/**
 * A ten-minute two-seat phase, held by this seat or by the other one.
 *
 * Ports `PvPWaitInteropTests.health` (test_client.py:10984-11017): turn 3,
 * phase 1, a 600s budget, and a `waiting_on` block naming seat 2 whenever the
 * phase is not this seat's.
 */
export const phaseHealthPayload = (options: PhaseHealthOptions): JsonObject => {
  const gameState = options.gameState ?? 'running';
  const base = healthPayload({
    game_state: gameState,
    observation_available: gameState === 'running',
    legal_actions_available: gameState === 'running',
  });
  // A terminal game has no phase at all; the decoder refuses one that survives.
  if (gameState !== 'running' && gameState !== 'lobby' && gameState !== 'starting') {
    return { ...base, phase: null };
  }
  const elapsedS = options.elapsedS === undefined ? 13.0 : options.elapsedS;
  const remainingS = options.remainingS === undefined ? 587.0 : options.remainingS;
  const timeoutS = options.timeoutS === undefined ? 600.0 : options.timeoutS;
  const phase: JsonObject = {
    state: options.phaseState ?? 'awaiting_agent',
    turn: options.turn ?? 3,
    phase: 1,
    active: options.mine,
    timing: {
      mode: options.mode ?? 'default',
      timeout_s: timeoutS,
      deadline_started_at: 1000.0,
      deadline_at: 1600.0,
      elapsed_s: elapsedS,
      remaining_s: remainingS,
    },
    ...(options.mine
      ? {}
      : {
          waiting_on: {
            kind: 'other_seat',
            summary:
              'Seat 2 AgentPlace2 (pi-gpt-5.6-sol) holds turn 3 phase 1 and has not ended it.',
            waiting_s: elapsedS,
            seats: options.waitingSeats ?? [opponentSeat()],
          },
        }),
    ...(options.priorEnd === undefined ? {} : { prior_end: options.priorEnd }),
    ...(options.autoEnd === undefined ? {} : { auto_end: options.autoEnd }),
  };
  return { ...base, phase };
};

/**
 * A `phase.prior_end` block.
 *
 * Ports the `prior_end` staticmethod at test_client.py:11388-11397.
 */
export const priorEndPayload = (
  source: string,
  ordersSubmitted: number | null,
  elapsedS = 600.0
): JsonObject => ({
  place: 2,
  seat_id: 'place-2',
  player_name: OPPONENT_PLAYER,
  controller_label: OPPONENT_CONTROLLER,
  turn: 3,
  phase: 0,
  source,
  receipt_state: 'applied',
  resolution: 'advanced',
  elapsed_s: elapsedS,
  orders_submitted: ordersSubmitted,
});

// ---------------------------------------------------------------------------
// The golden lines
// ---------------------------------------------------------------------------

/** What `phaseHeadline` prints, for each shape the supervisor can send. */
export const PHASE_HEADLINE = {
  /** No phase at all — a terminal game, or a lobby before the first turn. */
  none: 'phase none',
  /** `phaseHealthPayload({ mine: true, remainingS: 592, elapsedS: 8 })`. */
  yourTurn: 'YOUR TURN · t3/p1 · 9m52s left of 10m0s',
  /** The same phase in a state that is not `awaiting_agent`. */
  yourPhaseNotReady: 'YOUR PHASE · phase_not_ready t3/p1 · 9m52s left of 10m0s',
  /** `phaseHealthPayload({ mine: false, remainingS: 13, elapsedS: 587 })`. */
  holder:
    'NOT YOUR TURN · seat 2 AgentPlace2 (pi-gpt-5.6-sol) holds t3/p1 · held 9m47s of 10m0s · 13s left',
  /** Not this seat's, and the supervisor named nobody. */
  unnamedHolder: 'NOT YOUR TURN · waiting t3/p1 · 13s left',
  /** An untimed phase this seat does not hold. */
  infiniteUnnamed: 'NOT YOUR TURN · waiting t3/p1 · no deadline',
} as const;

/** What `phaseText` prints for the same phases. */
export const PHASE_TEXT = {
  none: 'phase none',
  yourTurn: 'phase awaiting_agent t3/p1 active 592s left',
  holder: 'phase awaiting_agent t3/p1 inactive 13s left',
  infinite: 'phase awaiting_agent t3/p1 inactive no deadline',
} as const;

/** The `opponent … ended …` line, one per `prior_end.source`. */
export const PRIOR_END_LEAD = 'opponent seat 2 AgentPlace2 (pi-gpt-5.6-sol) ended t3/p0 in ';

export const PRIOR_END = {
  timeoutNoOrders: `${PRIOR_END_LEAD}10m0s (timeout — they issued no orders)`,
  timeoutSomeOrders: `${PRIOR_END_LEAD}10m0s (timeout — their deadline passed)`,
  timeoutUnknownOrders: `${PRIOR_END_LEAD}10m0s (timeout — their deadline passed)`,
  autoIdle: `${PRIOR_END_LEAD}10m0s (auto_idle — the service ended their idle phase)`,
  agentSevenOrders: 'opponent seat 2 AgentPlace2 (pi-gpt-5.6-sol) ended t3/p0 in 32s (agent, 7 orders)',
  agentOneOrder: 'opponent seat 2 AgentPlace2 (pi-gpt-5.6-sol) ended t3/p0 in 32s (agent, 1 order)',
} as const;
