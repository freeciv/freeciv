/**
 * The exit-code contract, exhaustively.
 *
 * Ports `_wait_exit_code` (client.py:10090-10098) and `_phase_is_mine`
 * (10081-10088).  This is the whole of P1: every outcome used to exit 0, so a
 * harness whose monitor escalates on non-zero — the correct configuration, and
 * the one a live opponent already had — was never told its turn had started.
 *
 * The table below is required to cover `V2_WAKE_REASONS` completely; a reason
 * the server can send and this table does not name is a failing test, not a
 * silent 75.
 */
import { describe, expect, test } from 'bun:test';
import {
  FULL_CONTROL_V2,
  TERMINAL_STATES,
  V2_SATISFIED_WAKE_REASONS,
  V2_WAKE_REASONS,
} from 'src/constants';
import { V2_WAIT_EXIT_ACTIVE, V2_WAIT_EXIT_RETRY, V2_WAIT_EXIT_TERMINAL } from 'src/exit';
import type { HealthEnvelope, PhaseBlock } from 'src/schema/health';
import type { WaitEnvelope, WakeReason } from 'src/schema/wait';
import { phaseIsMine, waitExitCode, type WaitExitCode } from 'src/services/wait';
import { FIXTURE_AGENT_ID, FIXTURE_CONTROLLER, FIXTURE_GAME_ID } from 'test/_fixtures';

// ---------------------------------------------------------------------------
// Envelopes built by hand: these two functions are pure, and a decoded payload
// cannot express the drifted combinations they still have to be total over.
// ---------------------------------------------------------------------------

const timing = {
  mode: 'default',
  timeout_s: 600,
  deadline_started_at: 1000,
  deadline_at: 1600,
  elapsed_s: 13,
  remaining_s: 587,
} as const;

interface PhaseShape {
  readonly state?: string;
  readonly active?: boolean;
}

const phaseBlock = (shape: PhaseShape = {}): PhaseBlock => ({
  state: shape.state ?? 'awaiting_agent',
  turn: 3,
  phase: 1,
  active: shape.active ?? true,
  timing,
});

interface HealthShape {
  readonly phase?: PhaseBlock | null;
  readonly observation?: boolean;
  readonly gameState?: string;
}

const health = (shape: HealthShape = {}): HealthEnvelope => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  game_id: FIXTURE_GAME_ID,
  agent: { agent_id: FIXTURE_AGENT_ID, controller_label: FIXTURE_CONTROLLER },
  game_state: shape.gameState ?? 'running',
  seat: { place: 1, seat_id: 'seat_one', player_name: 'Alice' },
  sidecar: { state: 'ready', generation: 3 },
  observation_available: shape.observation ?? true,
  legal_actions_available: true,
  phase: shape.phase === undefined ? phaseBlock() : shape.phase,
  last_phase_end: null,
});

const wake = (reason: WakeReason, envelope: HealthEnvelope): WaitEnvelope => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  game_id: FIXTURE_GAME_ID,
  agent_id: FIXTURE_AGENT_ID,
  wake_reason: reason,
  health: envelope,
  state_revision: null,
});

/** A phase that is genuinely this seat's, and one that is somebody else's. */
const mine = health();
const theirs = health({ phase: phaseBlock({ active: false }), observation: true });

// ---------------------------------------------------------------------------
// _phase_is_mine
// ---------------------------------------------------------------------------

describe('phaseIsMine', () => {
  test.each([
    ['active, awaiting_agent, observable', health(), true],
    ['inactive', health({ phase: phaseBlock({ active: false }) }), false],
    ['active but phase_not_ready', health({ phase: phaseBlock({ state: 'phase_not_ready' }) }), false],
    ['active but no observation', health({ observation: false }), false],
    ['no phase at all', health({ phase: null }), false],
  ] as const)('%s → %p', (_label, envelope, expected) => {
    expect(phaseIsMine(envelope)).toBe(expected);
  });

  test('active and actionable are not the same fact', () => {
    // The ledger can name this seat's place while the native client withholds
    // permission to end the phase; only `awaiting_agent` earns the promise.
    const notReady = health({ phase: phaseBlock({ state: 'phase_end_pending' }) });
    expect(notReady.phase?.active).toBe(true);
    expect(phaseIsMine(notReady)).toBe(false);
  });
});

// ---------------------------------------------------------------------------
// The wake-reason → exit-code table
// ---------------------------------------------------------------------------

interface Row {
  readonly reason: WakeReason;
  readonly label: string;
  readonly envelope: HealthEnvelope;
  readonly code: WaitExitCode;
}

const terminalHealth = health({ phase: null, gameState: 'completed', observation: false });

const TABLE: ReadonlyArray<Row> = [
  // A satisfied wake is 0 whether or not the phase reads as this seat's.
  { reason: 'phase_active', label: 'phase mine', envelope: mine, code: V2_WAIT_EXIT_ACTIVE },
  { reason: 'phase_active', label: 'phase theirs', envelope: theirs, code: V2_WAIT_EXIT_ACTIVE },
  { reason: 'revision_changed', label: 'phase mine', envelope: mine, code: V2_WAIT_EXIT_ACTIVE },
  { reason: 'revision_changed', label: 'phase theirs', envelope: theirs, code: V2_WAIT_EXIT_ACTIVE },
  { reason: 'boundary_recovered', label: 'phase mine', envelope: mine, code: V2_WAIT_EXIT_ACTIVE },
  { reason: 'boundary_recovered', label: 'phase theirs', envelope: theirs, code: V2_WAIT_EXIT_ACTIVE },
  // A timeout is the one reason that reads the phase to decide.
  { reason: 'timeout', label: 'phase mine', envelope: mine, code: V2_WAIT_EXIT_ACTIVE },
  { reason: 'timeout', label: 'phase theirs', envelope: theirs, code: V2_WAIT_EXIT_RETRY },
  // Terminal outranks everything, including a phase that still looks active.
  { reason: 'game_terminal', label: 'game over', envelope: terminalHealth, code: V2_WAIT_EXIT_TERMINAL },
  { reason: 'game_terminal', label: 'stale actionable phase', envelope: mine, code: V2_WAIT_EXIT_TERMINAL },
];

describe('waitExitCode', () => {
  test.each(TABLE.map((row) => [row.reason, row.label, row.envelope, row.code] as const))(
    '%s (%s) → %p',
    (reason, _label, envelope, code) => {
      expect(waitExitCode(wake(reason, envelope))).toBe(code);
    }
  );

  test('the table is total over every wake reason the server can send', () => {
    const covered: ReadonlyArray<string> = TABLE.map((row) => row.reason);
    expect([...new Set(covered)].sort()).toEqual([...V2_WAKE_REASONS].sort());
  });

  test('every satisfied wake reason exits 0 even on a phase that is not ours', () => {
    const satisfied = TABLE.filter((row) => V2_SATISFIED_WAKE_REASONS.has(row.reason));
    for (const row of satisfied) {
      expect(waitExitCode(wake(row.reason, theirs))).toBe(V2_WAIT_EXIT_ACTIVE);
    }
    // …and the reasons the table calls satisfied are exactly the constant's.
    const named: ReadonlyArray<string> = satisfied.map((row) => row.reason);
    expect([...new Set(named)].sort()).toEqual([...V2_SATISFIED_WAKE_REASONS].sort());
  });

  test('the three statuses are the sysexits spellings, not invented numbers', () => {
    expect(V2_WAIT_EXIT_ACTIVE).toBe(0);
    expect(V2_WAIT_EXIT_RETRY).toBe(75);
    expect(V2_WAIT_EXIT_TERMINAL).toBe(66);
  });

  test('a terminal game stops a `until play wait; do :; done` loop', () => {
    for (const state of TERMINAL_STATES) {
      const envelope = health({ phase: null, gameState: state, observation: false });
      expect(waitExitCode(wake('game_terminal', envelope))).toBe(V2_WAIT_EXIT_TERMINAL);
    }
  });
});
