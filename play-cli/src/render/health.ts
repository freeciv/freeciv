/**
 * The rendered `health` block.
 *
 * Ports `_render_health` (client.py:5472-5551).
 *
 * Four facts in two lines, then one line each for whatever is true right now:
 * who is blocking, whether the supervisor is about to end the phase for you,
 * how the last phase ended, how the phase before it ended, and whether anything
 * was rolled back underneath you.  A line that would say nothing is not
 * printed — an agent reads this every turn and every constant line is noise it
 * learns to skip.
 */
import { duration, scalar } from 'src/render/primitives';
import { phaseHeadline, priorEndLine } from 'src/render/phase';
import {
  field,
  isJsonObject,
  type HealthEnvelope,
  type JsonObject,
  type JsonValue,
  type PhaseBlock,
  type PhaseEndEvent,
} from 'src/schema/index';

/**
 * The phase lines, re-exported.
 *
 * They live in `render/phase.ts` — one file per surface — but PORT_MAP §1 lists
 * them beside `renderHealth` under "U06 health", and U05 imports them from
 * here. Both paths resolve to the same functions; there is no second copy.
 */
export {
  holderSeat,
  phaseHeadline,
  phaseText,
  priorEndLine,
  seatLabel,
  type SeatLabelSource,
} from 'src/render/phase';

/**
 * The one useful next command for a seat that is blocked.
 *
 * Until this line existed `health` named the problem and no remedy, and the
 * agent that inspired it looped `wait; turn` fourteen times a turn because
 * nothing ever said there was a bounded, monitorable way to block.  The eight
 * spaces are the column the rest of the play card aligns its glosses on.
 */
export const BLOCKED_NEXT_LINE =
  'next: just wait --for-turn        blocks until your phase ' +
  'opens (exit 0 = go, 75 = still theirs, 66 = game over)';

const headline = (health: HealthEnvelope): string =>
  `health ${health.game_state} | ` +
  `${phaseHeadline(health.phase)} | ` +
  `obs ${health.observation_available ? 'yes' : 'no'} ` +
  `legal ${health.legal_actions_available ? 'yes' : 'no'} | ` +
  `sidecar ${scalar(field(health.sidecar, 'state'))} ` +
  `gen ${scalar(field(health.sidecar, 'generation'))}`;

const identityLine = (health: HealthEnvelope): string => {
  const seat = health.seat;
  let identity =
    `seat ${scalar(seat.place)} ${seat.player_name} ` + `(${health.agent.controller_label})`;
  const standing = seat.standing;
  if (standing !== undefined && standing !== 'active') {
    identity += ` | standing ${standing}`;
  }
  // The evaluation context is all-or-nothing: the decoder only sets `objective`
  // when the server sent the whole triple, so one presence check covers three.
  if (health.objective !== undefined) {
    identity +=
      ` | objective ${health.objective}` +
      ` | turns ${scalar(health.turns_remaining ?? null)}` +
      `/${scalar(health.max_turns ?? null)} remaining`;
  }
  return identity;
};

const waitingLine = (phase: PhaseBlock): string | null => {
  const blocked = phase.waiting_on;
  if (blocked === null || blocked === undefined) return null;
  let waiting = `waiting on ${blocked.kind}: ${blocked.summary}`;
  if (blocked.waiting_s !== null) {
    // Labelled.  Unlabelled, `(587.004s)` was read as anything from a timeout
    // to a latency, and three digits of precision on a number meaning "about
    // ten minutes" helped nobody.
    waiting += ` (blocked ${duration(blocked.waiting_s)})`;
  }
  return waiting;
};

const autoEndLine = (phase: PhaseBlock): string | null => {
  const autoEnd = phase.auto_end;
  if (autoEnd === null || autoEnd === undefined || !autoEnd.armed) return null;
  const remaining = autoEnd.remaining_s;
  return (
    'auto-end armed: nothing idle and no decision pending' +
    (remaining !== null ? `, this phase ends in ${scalar(remaining)}s` : '') +
    ' unless you act'
  );
};

const phaseEndLine = (event: PhaseEndEvent): string => {
  let line =
    `last phase end t${scalar(field(event, 'turn'))}` +
    `/p${scalar(field(event, 'phase'))} ` +
    `source=${String(field(event, 'source'))} ${String(field(event, 'receipt_state'))} ` +
    `${String(field(event, 'resolution'))} ${scalar(field(event, 'elapsed_s'))}s`;
  if (field(event, 'source') === 'auto_idle') {
    line += ' (ended for you: nothing idle, no decision pending)';
  }
  return line;
};

const recoveryLine = (recovery: JsonObject): string => {
  let line =
    `last recovery t${scalar(field(recovery, 'turn'))} ` +
    `${String(field(recovery, 'kind'))} ${String(field(recovery, 'outcome'))} ` +
    `trigger=${String(field(recovery, 'trigger'))}`;
  const rewoundTo: JsonValue = field(recovery, 'recovered_to_turn');
  if (rewoundTo !== null) {
    line += ` rewound to t${scalar(rewoundTo)}`;
  }
  if (field(recovery, 'rewound_applied_actions') === true) {
    line += ' — applied actions were rolled back; re-read `just turn`';
  }
  return line;
};

/** The `health` block, without the blocked-seat remedy the command appends. */
export const renderHealth = (health: HealthEnvelope): ReadonlyArray<string> => {
  const lines: string[] = [headline(health), identityLine(health)];
  const phase = health.phase;
  if (phase !== null) {
    const waiting = waitingLine(phase);
    if (waiting !== null) lines.push(waiting);
    const autoEnd = autoEndLine(phase);
    if (autoEnd !== null) lines.push(autoEnd);
  }
  const event = health.last_phase_end;
  if (event !== null) lines.push(phaseEndLine(event));
  const prior = priorEndLine(phase);
  if (prior !== '') lines.push(prior);
  const recovery = health.last_recovery;
  if (recovery !== null && recovery !== undefined && isJsonObject(recovery)) {
    lines.push(recoveryLine(recovery));
  }
  return lines;
};
