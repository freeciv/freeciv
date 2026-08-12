/**
 * Every phase-derived line the CLI shares.
 *
 * Ports `_holder_seat`, `_seat_label`, `_phase_headline`, `_phase_text` and
 * `_prior_end_line` (client.py:5350-5471).
 *
 * These five functions are the reason this unit exists as a unit: `health`,
 * `turn`, `do --end --await`, `wait` and `monitor` all print the *same bytes*
 * for "whose phase is this" and "how did the last one end".  Deriving them five
 * times is how the Python's ancestors ended up saying `awaiting_agent` to a seat
 * that was not the one being awaited — a misreading that cost a live match nine
 * turns.  One implementation, one golden fixture set, five callers.
 */
import { duration, scalar } from 'src/render/primitives';
import type { JsonValue, PhaseBlock, PriorEnd, WaitingOnSeat } from 'src/schema/index';

/**
 * Anything that can be named as a seat on a line.
 *
 * Both `waiting_on.seats[]` rows and `phase.prior_end` satisfy it, which is not
 * a coincidence: the supervisor carries the same four identity fields in both
 * places and the reader wants the same sentence out of both.
 */
export interface SeatLabelSource {
  readonly place: JsonValue;
  readonly player_name: JsonValue;
  readonly controller_label: JsonValue;
}

/**
 * Name the other seat this phase is waiting on, when there is exactly one.
 *
 * `waiting_on.seats[]` has carried `player_name` and `controller_label` since
 * the field existed; it was simply never rendered, so "another seat" was the
 * whole disclosure a blocked agent got.
 */
export const holderSeat = (phase: PhaseBlock | null | undefined): WaitingOnSeat | null => {
  if (phase === null || phase === undefined ||  phase.active) return null;
  const waitingOn = phase.waiting_on;
  if (waitingOn === null || waitingOn === undefined) return null;
  // `is_self is False`, not "falsy": a row that omits the flag is not a claim
  // that the seat is somebody else.
  const others = waitingOn.seats.filter((row) => row.is_self === false);
  return others.length === 1 ? (others[0] as WaitingOnSeat) : null;
};

export const seatLabel = (row: SeatLabelSource): string => {
  const label = row.controller_label;
  return (
    `seat ${scalar(row.place)} ${scalar(row.player_name)}` +
    (typeof label === 'string' && label !== '' ? ` (${label})` : '')
  );
};

/**
 * Answer "is it my turn" first, and name whoever holds it if it is not.
 *
 * The old line read `phase awaiting_agent t5/p0 inactive 12.9s left`, which
 * invites exactly one misreading: `awaiting_agent` sounds like *awaiting me*,
 * and the deadline printed beside it belongs to the seat that is actually
 * playing.
 */
export const phaseHeadline = (phase: PhaseBlock | null | undefined): string => {
  if (phase === null || phase === undefined) return 'phase none';
  const key = `t${scalar(phase.turn)}/p${scalar(phase.phase)}`;
  const timing = phase.timing;
  const held = duration(timing.elapsed_s);
  const budget = timing.timeout_s !== null ? ` of ${duration(timing.timeout_s)}` : '';
  const left =
    timing.remaining_s !== null
      ? ` · ${duration(timing.remaining_s)} left`
      : timing.mode === 'infinite'
        ? ' · no deadline'
        : '';
  // A phase can be *this seat's* and still not be actionable — the ledger names
  // the place while the native client withholds permission to end it.  Only
  // `awaiting_agent` earns the unqualified promise; every other state keeps its
  // own name so the waiting_on line below explains a fact the headline already
  // admitted.
  const state = phase.state === 'awaiting_agent' ? '' : ` ${scalar(phase.state)}`;
  if (phase.active) {
    // On your own turn the deadline is yours, so it is the whole story: how
    // long you have, out of how long the phase gets.
    return (
      `${state === '' ? 'YOUR TURN' : 'YOUR PHASE'} ·${state} ${key}${left}` +
      (timing.remaining_s !== null ? budget : '')
    );
  }
  const holder = holderSeat(phase);
  if (holder !== null) {
    return (
      `NOT YOUR TURN · ${seatLabel(holder)} holds${state} ${key}` +
      (timing.elapsed_s !== null ? ` · held ${held}${budget}` : '') +
      left
    );
  }
  return `NOT YOUR TURN ·${state === '' ? ' waiting' : state} ${key}${left}`;
};

export const phaseText = (phase: PhaseBlock | null | undefined): string => {
  if (phase === null || phase === undefined) return 'phase none';
  let text =
    `phase ${scalar(phase.state)} t${scalar(phase.turn)}` +
    `/p${scalar(phase.phase)}` +
    ` ${phase.active ? 'active' : 'inactive'}`;
  const timing = phase.timing;
  if (timing.remaining_s !== null) {
    text += ` ${scalar(timing.remaining_s)}s left`;
  } else if (timing.mode === 'infinite') {
    text += ' no deadline';
  }
  return text;
};

/** The cause clause of {@link priorEndLine}, split out so it stays readable. */
const priorCause = (prior: PriorEnd): string => {
  const orders = prior.orders_submitted;
  if (prior.source === 'timeout') {
    return orders === 0 ? 'timeout — they issued no orders' : 'timeout — their deadline passed';
  }
  if (prior.source === 'auto_idle') {
    return 'auto_idle — the service ended their idle phase';
  }
  return (
    'agent' +
    (typeof orders === 'number' ? `, ${orders} order${orders === 1 ? '' : 's'}` : '')
  );
};

/**
 * Say how the phase before this one ended, and whether anyone played it.
 *
 * A seat that thought for its whole ten minutes and a seat that was not there
 * at all end their phase identically as far as the board is concerned:
 * `source=timeout`, the supervisor advances, the game goes on.  They are not
 * the same event, and the difference is the single most decision-relevant fact
 * an opponent can be told — in the run this came from, one seat issued zero
 * orders on nine of thirteen turns and nothing anywhere said so.
 */
export const priorEndLine = (phase: PhaseBlock | null | undefined): string => {
  if (phase === null || phase === undefined) return '';
  const prior = phase.prior_end;
  if (prior === null || prior === undefined) return '';
  return (
    `opponent ${seatLabel(prior)} ended ` +
    `t${scalar(prior.turn)}/p${scalar(prior.phase)} in ` +
    `${duration(prior.elapsed_s)} (${priorCause(prior)})`
  );
};
