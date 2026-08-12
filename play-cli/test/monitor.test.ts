/**
 * `play monitor`, end to end.
 *
 * Ports `PvPWaitInteropTests`' monitor block, `test_client.py:11601-12364`: the
 * three `test_monitor_*` cases, `test_persistent_*`, `test_stopping_*`,
 * `test_status_*`, `test_once_*`, `test_exit_*`, `test_max_*`, the missed-turn
 * alarm, the absorbed transport faults, the four channels and the `--exec`
 * contract.
 *
 * Two properties carry the unit and both have their own case here:
 *
 * - the monitor is **strictly read-only** — `the monitor writes the marker and
 *   nothing else` and `never reads or writes the revision cursor` fail if any
 *   write path becomes reachable from the loop;
 * - the announce line is **byte-compatible** with the header `just wait` printed
 *   before the PvP legibility work, because a harness parses exactly it.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { Command } from '@effect/cli';
import { BunContext } from '@effect/platform-bun';
import { afterEach, describe, expect, test } from 'bun:test';
import { Cause, Effect, Either, Exit, Layer, Option } from 'effect';
import { V2_WAIT_EXIT_RETRY, V2_WAIT_EXIT_TERMINAL } from 'src/exit';
import { playerError, type PlayerError } from 'src/errors';
import {
  commandMonitor,
  holderSince,
  monitorCommandWith,
  monitorDefaults,
  type MonitorHarness,
  type MonitorOptions,
} from 'src/commands/monitor.cmd';
import { missedLine, monitorAnnounceLine, monitorTerminalLine } from 'src/render/monitor';
import { phaseText } from 'src/render/phase';
import type { JsonObject } from 'src/schema/primitives';
import { decodeWait, type WaitEnvelope } from 'src/schema/wait';
import { compactJson } from 'src/services/json-output';
import { mirrorDir, readPhaseMarker, updatePhaseMarker } from 'src/services/mirror';
import {
  completedProcessStatus,
  monitorExecRefusal,
  shellHookRunner,
  type HookOutcome,
} from 'src/services/monitor-hook';
import { withMonitorLock } from 'src/services/monitor-lock';
import {
  V2_MONITOR_BACKOFF_MAX_S,
  V2_MONITOR_BACKOFF_START_S,
  liveMonitorSeams,
  type MonitorSeams,
} from 'src/services/monitor-loop';
import { PrivateFs, type PrivateFsApi } from 'src/services/private-fs';
import { httpFor } from 'src/services/http';
import {
  SessionStore,
  emptyV2ClientState,
  sessionStoreFor,
  type Session,
  type SessionStoreApi,
} from 'src/services/session-store';
import { V2Client, v2ClientFor } from 'src/services/v2-client';
import { systemWaitClock, type WaitClock, type WaitUntilTurnOptions } from 'src/services/wait';
import {
  FIXTURE_CONTROLLER,
  jsonResponse,
  scratchWorkspace,
  sessionFile,
  waitPayload,
  type Scratch,
} from 'test/_fixtures';
import { phaseHealthPayload } from 'test/_fixtures/phase-goldens';

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------

const scratches: Scratch[] = [];

afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

const stateSchema = {
  empty: emptyV2ClientState,
  validate: () => Effect.void,
  cursorExpired: (): boolean => false,
};

interface Bench {
  readonly scratch: Scratch;
  readonly sessionPath: string;
  readonly session: Session;
  readonly store: SessionStoreApi;
  readonly files: PrivateFsApi;
  readonly mirror: string;
}

const bench = (): Bench => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const sessionPath = path.join(scratch.workspace.stateRoot, 'session-codex-gpt-5.6-sol.json');
  Effect.runSync(scratch.files.writeJson(sessionPath, sessionFile()));
  const store = sessionStoreFor(scratch.workspace, scratch.files, stateSchema, {});
  const loaded = Effect.runSync(store.resolveV2(sessionPath));
  return {
    scratch,
    sessionPath,
    session: loaded.session,
    store,
    files: scratch.files,
    mirror: Effect.runSync(mirrorDir(sessionPath)),
  };
};

/** `PvPWaitInteropTests.wake`, ported. */
const wakePayload = (
  reason: string,
  options: Parameters<typeof phaseHealthPayload>[0],
  lastPhaseEnd?: JsonObject
): JsonObject =>
  waitPayload({
    wake_reason: reason,
    health:
      lastPhaseEnd === undefined
        ? phaseHealthPayload(options)
        : { ...phaseHealthPayload(options), last_phase_end: lastPhaseEnd },
  });

/**
 * `PvPWaitInteropTests.own_end`, ported.
 *
 * The seat fields have to match the health envelope's own seat and the
 * session's controller label, because `_validate_phase_end_event` cross-checks
 * all four.
 */
const ownEnd = (options: {
  readonly incarnation?: number;
  readonly turn?: number;
  readonly phase?: number;
  readonly source?: string;
  readonly orders?: number | null;
  readonly elapsedS?: number;
}): JsonObject => ({
  sequence: 4,
  incarnation: options.incarnation ?? 0,
  turn: options.turn ?? 3,
  phase: options.phase ?? 1,
  place: 1,
  seat_id: 'seat_one',
  player_name: 'Alice',
  player_color: '#0067A5',
  controller_label: FIXTURE_CONTROLLER,
  controller_type: 'external',
  source: options.source ?? 'timeout',
  receipt_state: 'applied',
  resolution: 'advanced',
  deadline_started_at: 1000.0,
  ended_at: 1600.0,
  elapsed_s: options.elapsedS ?? 600.0,
  orders_submitted: options.orders === undefined ? 0 : options.orders,
});

const decodedWake = (payload: JsonObject, session: Session): WaitEnvelope =>
  Effect.runSync(decodeWait(payload, session, { until: 'phase', afterStateToken: null }));

// ---------------------------------------------------------------------------
// The scripted `_wait_until_turn`
// ---------------------------------------------------------------------------

interface Script {
  readonly seams: MonitorSeams;
  readonly slept: number[];
  readonly calls: WaitUntilTurnOptions[];
  readonly hooks: Array<{ command: string; environment: Record<string, string> }>;
  readonly now: { seconds: number };
}

interface ScriptOptions {
  /** Seconds the clock jumps for each scripted wake, as a blocking wait would. */
  readonly advanceS?: number;
  readonly hookStatus?: number;
  readonly hookThrows?: string;
}

/**
 * Feed the loop a fixed sequence of wakes, then stop it.
 *
 * The loop is unbounded by design, so a script ends with a terminal wake; a run
 * that exhausts the script dies loudly instead of hanging.
 */
const scripted = (
  wakes: ReadonlyArray<WaitEnvelope | PlayerError>,
  options: ScriptOptions = {}
): Script => {
  const remaining = [...wakes];
  const slept: number[] = [];
  const calls: WaitUntilTurnOptions[] = [];
  const hooks: Array<{ command: string; environment: Record<string, string> }> = [];
  const now = { seconds: 0 };
  const clock: WaitClock = {
    monotonic: () => Effect.sync(() => now.seconds),
    sleep: (seconds) =>
      Effect.sync(() => {
        slept.push(seconds);
        now.seconds += seconds;
      }),
  };
  return {
    slept,
    calls,
    hooks,
    now,
    seams: {
      clock,
      waitUntilTurn: (_args, waitOptions) =>
        Effect.suspend(() => {
          calls.push(waitOptions);
          const next = remaining.shift();
          if (next === undefined) {
            return Effect.die(new Error('the monitor asked for one wake too many'));
          }
          now.seconds += options.advanceS ?? 0;
          return '_tag' in next && next._tag === 'PlayerError'
            ? Effect.fail(next)
            : Effect.succeed(next as WaitEnvelope);
        }),
      runHook: (command, environment) =>
        Effect.sync((): HookOutcome => {
          hooks.push({ command, environment });
          if (options.hookThrows !== undefined) {
            return { _tag: 'unstarted', message: options.hookThrows };
          }
          return { _tag: 'exited', status: options.hookStatus ?? 0 };
        }),
    },
  };
};

/**
 * The defect the test harness raises instead of ending the test runner.
 *
 * `--exit-code 42` really does call `process.exit(42)` in production, so the
 * seam has to be observable rather than fatal; `runCommand` unwraps it back
 * into the status the process would have carried.
 */
const PROCESS_EXIT = 'monitor-test:process-exit';

interface ProcessExitDefect {
  readonly _tag: typeof PROCESS_EXIT;
  readonly status: number;
}

const exitDefect = (status: number): ProcessExitDefect => ({ _tag: PROCESS_EXIT, status });

const isExitDefect = (value: unknown): value is ProcessExitDefect =>
  typeof value === 'object' &&
  value !== null &&
  '_tag' in value &&
  value._tag === PROCESS_EXIT &&
  'status' in value &&
  typeof value.status === 'number';

const harnessFor = (seams: MonitorSeams, overrides: Partial<MonitorHarness> = {}): MonitorHarness => ({
  seams: () => seams,
  kill: () => Effect.succeed('signalled'),
  clock: seams.clock,
  pid: () => 41207,
  since: () => '16:21:04',
  exitProcess: (status) => Effect.die(exitDefect(status)),
  ...overrides,
});

const options = (overrides: Partial<MonitorOptions> = {}): MonitorOptions => ({
  ...monitorDefaults,
  ...overrides,
});

/** A `V2Client` no scripted run ever reaches; using it is the failure. */
const unusedClient = Layer.succeed(
  V2Client,
  v2ClientFor(
    httpFor((async () => {
      throw new Error('a scripted monitor must not touch the network');
    }) as unknown as typeof fetch)
  )
);

interface Run {
  readonly code: number;
  readonly out: ReadonlyArray<string>;
  readonly err: ReadonlyArray<string>;
  readonly failure: string | null;
}

const runMonitor = async (
  seat: Bench,
  seams: MonitorSeams,
  monitorOptions: MonitorOptions,
  harnessOverrides: Partial<MonitorHarness> = {},
  store: SessionStoreApi = seat.store
): Promise<Run> => {
  const out: string[] = [];
  const err: string[] = [];
  const log = console.log;
  const error = console.error;
  console.log = (...parts: ReadonlyArray<unknown>) => out.push(parts.join(' '));
  console.error = (...parts: ReadonlyArray<unknown>) => err.push(parts.join(' '));
  try {
    const either = await Effect.runPromise(
      Effect.either(
        Effect.provide(
          commandMonitor(
            { ...monitorOptions, session: seat.sessionPath },
            harnessFor(seams, harnessOverrides),
            {}
          ),
          Layer.mergeAll(
            Layer.succeed(PrivateFs, seat.files),
            Layer.succeed(SessionStore, store),
            unusedClient
          )
        )
      )
    );
    return Either.isRight(either)
      ? { code: either.right, out, err, failure: null }
      : { code: 2, out, err, failure: either.left.message };
  } finally {
    console.log = log;
    console.error = error;
  }
};

const markerOf = (seat: Bench): JsonObject | null =>
  Effect.runSync(Effect.provide(readPhaseMarker(seat.mirror), Layer.succeed(PrivateFs, seat.files)));

const monitorLog = (seat: Bench): string => {
  const file = path.join(seat.mirror, 'state', 'monitor.log');
  return fs.existsSync(file) ? fs.readFileSync(file, 'utf8') : '';
};

// ---------------------------------------------------------------------------
// The two bindings
// ---------------------------------------------------------------------------

describe('monitor --once', () => {
  test('announces the open phase and exits', async () => {
    const seat = bench();
    const script = scripted([decodedWake(wakePayload('phase_active', { mine: true }), seat.session)]);
    const result = await runMonitor(seat, script.seams, options({ once: true }));
    expect(result.code).toBe(0);
    expect(result.out).toHaveLength(1);
  });

  test('keeps the shape two harnesses already parse', async () => {
    // Byte-identical to the header `just wait` printed before the PvP
    // legibility work, which redesigned the *waiting* line, not this one.
    const seat = bench();
    const payload = wakePayload('phase_active', { mine: true });
    const wake = decodedWake(payload, seat.session);
    const script = scripted([wake]);
    const result = await runMonitor(seat, script.seams, options({ once: true }));
    expect(result.out[0]).toBe(
      'T3 | woke phase_active | running | ' +
        'phase awaiting_agent t3/p1 active 587s left | next: just turn'
    );
    // The legacy shape, reconstructed from the same payload.
    expect(result.out[0]).toBe(
      `T${wake.health.phase?.turn} | woke ${wake.wake_reason} | ${wake.health.game_state} | ` +
        `${phaseText(wake.health.phase)} | next: just turn`
    );
    expect(result.out[0]).toBe(monitorAnnounceLine(wake));
  });

  test('a still-theirs wake is the monitors business, not the agents', async () => {
    const seat = bench();
    const script = scripted([
      decodedWake(wakePayload('timeout', { mine: false }), seat.session),
      decodedWake(wakePayload('timeout', { mine: false }), seat.session),
      decodedWake(wakePayload('phase_active', { mine: true }), seat.session),
    ]);
    const result = await runMonitor(seat, script.seams, options({ once: true }));
    expect(result.code).toBe(0);
    expect(result.out).toHaveLength(1);
  });

  test('--exit-code lets a harness declare how to be told', async () => {
    // pi escalates on non-zero exit; guessing which status is not a plan.
    const seat = bench();
    const script = scripted([decodedWake(wakePayload('phase_active', { mine: true }), seat.session)]);
    const result = await runMonitor(seat, script.seams, options({ once: true, exitCode: 75 }));
    expect(result.code).toBe(75);

    // Any N in [0, 255], not only the four the exit contract names.
    for (const status of [1, 42, 255]) {
      const again = bench();
      const wake = scripted([
        decodedWake(wakePayload('phase_active', { mine: true }), again.session),
      ]);
      const escalated = await runMonitor(again, wake.seams, options({ once: true, exitCode: status }));
      expect(escalated.code).toBe(status);
    }

    const refused = await runMonitor(
      seat,
      scripted([]).seams,
      options({ once: true, exitCode: 300 })
    );
    expect(refused.failure).toContain('--exit-code');
  });

  test('always answers even on an already-announced phase', async () => {
    // A wake-up call that stayed silent would hang the harness for ever.
    const seat = bench();
    const payload = wakePayload('phase_active', { mine: true });
    Effect.runSync(
      Effect.provide(
        updatePhaseMarker(seat.mirror, payload['health'], { announced: [0, 3, 1] }),
        Layer.succeed(PrivateFs, seat.files)
      )
    );
    const script = scripted([decodedWake(payload, seat.session)]);
    const result = await runMonitor(seat, script.seams, options({ once: true }));
    expect(result.code).toBe(0);
    expect(result.out.join('\n')).toContain('woke phase_active');
  });
});

describe('the persistent monitor', () => {
  test('announces every turn until terminal', async () => {
    const seat = bench();
    const script = scripted([
      decodedWake(wakePayload('phase_active', { mine: true }), seat.session),
      decodedWake(wakePayload('timeout', { mine: false }), seat.session),
      decodedWake(wakePayload('phase_active', { mine: true, turn: 4 }), seat.session),
      decodedWake(wakePayload('game_terminal', { mine: false, gameState: 'completed' }), seat.session),
    ]);
    const result = await runMonitor(seat, script.seams, options());
    expect(result.code).toBe(V2_WAIT_EXIT_TERMINAL);
    expect(result.out).toHaveLength(3);
    expect(result.out[0]).toContain('T3 | woke phase_active');
    expect(result.out[1]).toContain('T4 | woke phase_active');
    expect(result.out[2]).toContain('GAME OVER');
  });

  test('a second one is a no-op that reports the first', async () => {
    // Singleton by kernel lock, not by bookkeeping.
    const seat = bench();
    const result = await Effect.runPromise(
      Effect.provide(
        withMonitorLock(seat.sessionPath, { pid: 41207, since: '16:21:04', game_id: 'g' }, (first) =>
          Effect.promise(async () => {
            expect(first).toBeNull();
            return runMonitor(seat, scripted([]).seams, options());
          })
        ),
        Layer.succeed(PrivateFs, seat.files)
      )
    );
    expect(result.code).toBe(0);
    expect(result.out.join('\n')).toContain('monitor already running');
    expect(result.out.join('\n')).toContain('pid 41207');
    expect(result.out.join('\n')).toContain('since 16:21:04');
  });

  test('--once takes no lock, so the two bindings compose', async () => {
    const seat = bench();
    const script = scripted([decodedWake(wakePayload('phase_active', { mine: true }), seat.session)]);
    const result = await Effect.runPromise(
      Effect.provide(
        withMonitorLock(seat.sessionPath, { pid: 4242 }, () =>
          Effect.promise(async () => runMonitor(seat, script.seams, options({ once: true })))
        ),
        Layer.succeed(PrivateFs, seat.files)
      )
    );
    expect(result.code).toBe(0);
    expect(result.out.join('\n')).not.toContain('already running');
  });

  test('a restarted one does not repeat a turn', async () => {
    // Process-singleton is not enough; the announcement must dedupe too.
    const seat = bench();
    const openPhase = wakePayload('phase_active', { mine: true });
    const terminal = wakePayload('game_terminal', { mine: false, gameState: 'completed' });
    const first = await runMonitor(
      seat,
      scripted([decodedWake(openPhase, seat.session), decodedWake(terminal, seat.session)]).seams,
      options()
    );
    expect(first.out.join('\n')).toContain('T3 | woke phase_active');
    expect(markerOf(seat)?.['announced']).toEqual([0, 3, 1]);

    // Restarted onto the same still-open phase, it says nothing about it and
    // waits for the next one.
    const second = await runMonitor(
      seat,
      scripted([decodedWake(openPhase, seat.session), decodedWake(terminal, seat.session)]).seams,
      options()
    );
    expect(second.out.join('\n')).not.toContain('woke phase_active');
    expect(second.out.join('\n')).toContain('GAME OVER');
  });

  test('a rollback replays the turn and is announced again', async () => {
    // The incarnation is in the tuple because a replayed turn is new.
    const seat = bench();
    Effect.runSync(
      Effect.provide(
        updatePhaseMarker(seat.mirror, phaseHealthPayload({ mine: true }), {
          announced: [0, 3, 1],
        }),
        Layer.succeed(PrivateFs, seat.files)
      )
    );
    const replayed = wakePayload(
      'phase_active',
      { mine: true },
      ownEnd({ incarnation: 1, turn: 2, phase: 1, source: 'agent', orders: 4 })
    );
    const result = await runMonitor(
      seat,
      scripted([decodedWake(replayed, seat.session)]).seams,
      options({ once: true })
    );
    expect(result.out.join('\n')).toContain('woke phase_active');
    expect(markerOf(seat)?.['announced']).toEqual([1, 3, 1]);
  });
});

// ---------------------------------------------------------------------------
// The missed-turn alarm
// ---------------------------------------------------------------------------

describe('the missed-turn alarm', () => {
  test('a turn that opened and died unplayed raises it', async () => {
    // The exact failure this whole surface exists for: a machine that slept
    // through a phase gets told so, and nothing else in the workspace is
    // positioned to notice.
    const seat = bench();
    const missed = wakePayload(
      'phase_active',
      { mine: true, turn: 6 },
      ownEnd({ turn: 5, phase: 1, source: 'timeout', orders: 0 })
    );
    const result = await runMonitor(
      seat,
      scripted([decodedWake(missed, seat.session)]).seams,
      options({ once: true })
    );
    expect(result.out[0]).toBe(
      'T5 | MISSED | your phase t5/p1 opened and was ended by timeout after 600s — ' +
        'you issued no orders'
    );
    // The alarm never swallows the announcement of the open phase.
    expect(result.out[1]).toContain('T6 | woke phase_active');
  });

  test('a second consecutive miss escalates its wording', async () => {
    // Two phases lost in a row means the notification path itself is broken.
    const seat = bench();
    const reconnect = (openTurn: number, endedTurn: number, source = 'timeout'): WaitEnvelope =>
      decodedWake(
        wakePayload(
          'phase_active',
          { mine: true, turn: openTurn },
          ownEnd({
            turn: endedTurn,
            phase: 1,
            source,
            orders: source === 'agent' ? 4 : 0,
          })
        ),
        seat.session
      );
    const script = scripted([
      // A turn this seat played itself: no alarm, and it is the last turn we
      // can honestly say an order was issued on.
      reconnect(4, 3, 'agent'),
      // Slept through t5 entirely; woke into t6.
      reconnect(6, 5),
      // Slept again: t7 opened and died without ever being seen.
      reconnect(8, 7),
      decodedWake(wakePayload('game_terminal', { mine: false, gameState: 'completed' }), seat.session),
    ]);
    const result = await runMonitor(seat, script.seams, options());
    const text = result.out.join('\n');
    expect(text).toContain('T5 | MISSED | your phase t5/p1');
    expect(text).not.toContain('MISSED ×2 | your phase t5');
    expect(text).toContain('T7 | MISSED ×2 | your phase t7/p1');
    expect(text).toContain('you have not issued an order since t3.');
    expect(text).toContain(
      'Your monitor is not reaching you — check it now; the game is advancing without you.'
    );
  });

  test('a turn the agent was told about and ignored is not a miss', async () => {
    // The alarm is "nothing reached you", not "you played badly".
    const seat = bench();
    const script = scripted([
      decodedWake(wakePayload('phase_active', { mine: true, turn: 6 }), seat.session),
      decodedWake(
        wakePayload(
          'phase_active',
          { mine: true, turn: 7 },
          ownEnd({ turn: 6, phase: 1, source: 'timeout', orders: 0 })
        ),
        seat.session
      ),
      decodedWake(wakePayload('game_terminal', { mine: false, gameState: 'completed' }), seat.session),
    ]);
    const result = await runMonitor(seat, script.seams, options());
    expect(result.out.join('\n')).not.toContain('MISSED');
  });

  test('a phase this seat ended itself is never a missed turn', async () => {
    // `--await` opening a turn the monitor never announced is normal.
    const seat = bench();
    const played = wakePayload(
      'phase_active',
      { mine: true, turn: 6 },
      ownEnd({ turn: 5, phase: 1, source: 'agent', orders: 4, elapsedS: 32.0 })
    );
    const result = await runMonitor(
      seat,
      scripted([decodedWake(played, seat.session)]).seams,
      options({ once: true })
    );
    expect(result.out.join('\n')).not.toContain('MISSED');
  });

  test('orders issued without a phase end are reported as such', () => {
    expect(missedLine(ownEnd({ turn: 5, source: 'timeout', orders: 3 }), 1, undefined)).toContain(
      'you issued 3 orders but never ended the phase'
    );
    const unknown = missedLine({ ...ownEnd({ turn: 5 }), orders_submitted: null }, 1, undefined);
    expect(unknown).toContain('was ended by timeout after 600s');
    expect(unknown).not.toContain('no orders');
  });

  test('an auto_idle end names the service, not a timeout', () => {
    expect(missedLine(ownEnd({ turn: 5, source: 'auto_idle' }), 1, undefined)).toBe(
      'T5 | MISSED | your phase t5/p1 opened and ended for you after 600s — ' +
        'nothing was idle and no decision was pending'
    );
  });
});

// ---------------------------------------------------------------------------
// Transport faults are absorbed, never raised
// ---------------------------------------------------------------------------

describe('a transport fault', () => {
  test('is retried with backoff, not raised', async () => {
    // A laptop sleep is what a left-running monitor exists to survive.
    const seat = bench();
    const script = scripted([
      playerError('connection reset'),
      playerError('connection reset'),
      decodedWake(wakePayload('phase_active', { mine: true }), seat.session),
    ]);
    const result = await runMonitor(seat, script.seams, options({ once: true }));
    expect(result.code).toBe(0);
    expect(script.slept).toEqual([V2_MONITOR_BACKOFF_START_S, V2_MONITOR_BACKOFF_START_S * 2]);
    expect(result.err.join('\n')).toContain('retrying after connection reset');
    expect(result.out.join('\n')).toContain('woke phase_active');
  });

  test('the backoff is capped so a dead service is not hammered', async () => {
    const seat = bench();
    const script = scripted([
      ...Array.from({ length: 8 }, () => playerError('down')),
      decodedWake(wakePayload('phase_active', { mine: true }), seat.session),
    ]);
    await runMonitor(seat, script.seams, options({ once: true }));
    expect(Math.max(...script.slept)).toBe(V2_MONITOR_BACKOFF_MAX_S);
    expect(script.slept.at(-1)).toBe(V2_MONITOR_BACKOFF_MAX_S);
  });

  test('a rebound workspace stops the monitor', async () => {
    // A monitor must never watch a game the workspace has left.
    const seat = bench();
    const other = path.join(seat.scratch.workspace.stateRoot, 'other-seat.json');
    const rebound: SessionStoreApi = {
      ...seat.store,
      resolve: () => Effect.succeed({ path: other, session: seat.session }),
    };
    const result = await runMonitor(
      seat,
      scripted([]).seams,
      options({ once: true }),
      {},
      rebound
    );
    expect(result.code).toBe(0);
    expect(result.out.join('\n')).toContain('rebound to another seat');
  });
});

// ---------------------------------------------------------------------------
// --max-s
// ---------------------------------------------------------------------------

describe('--max-s', () => {
  test('gives up with EX_TEMPFAIL once the phase is still not yours', async () => {
    const seat = bench();
    const script = scripted(
      [
        decodedWake(wakePayload('timeout', { mine: false }), seat.session),
        decodedWake(wakePayload('timeout', { mine: false }), seat.session),
      ],
      { advanceS: 20 }
    );
    const result = await runMonitor(seat, script.seams, options({ maxS: 30 }));
    expect(result.code).toBe(V2_WAIT_EXIT_RETRY);
    expect(result.err.join('\n')).toContain('--max-s elapsed and the phase is still not yours');
    expect(monitorLog(seat)).toContain('stopped: --max-s elapsed');
    // The remaining budget is handed to the wait as its cap, never exceeded.
    expect(script.calls[0]?.capS).toBe(30);
    expect(script.calls[1]?.capS).toBe(10);
  });
});

// ---------------------------------------------------------------------------
// The four channels
// ---------------------------------------------------------------------------

describe('the channels', () => {
  test('every announcement reaches the marker and the log', async () => {
    const seat = bench();
    await runMonitor(
      seat,
      scripted([decodedWake(wakePayload('phase_active', { mine: true }), seat.session)]).seams,
      options({ once: true })
    );
    const marker = markerOf(seat);
    expect(marker?.['active']).toBe(true);
    expect(marker?.['announced']).toEqual([0, 3, 1]);
    const log = monitorLog(seat);
    expect(log).toContain('monitor started --once');
    expect(log).toContain('woke phase_active');

    // Append-only: a second run adds to it rather than replacing it.
    await runMonitor(
      seat,
      scripted([
        decodedWake(wakePayload('game_terminal', { mine: false, gameState: 'completed' }), seat.session),
      ]).seams,
      options({ once: true })
    );
    const grown = monitorLog(seat);
    expect(grown.startsWith(log)).toBe(true);
    expect(grown).toContain('GAME OVER');
  });

  test('the monitor writes the marker and nothing else', async () => {
    // A process alive for the whole game is not a second writer of the
    // projections a real command owns.
    const seat = bench();
    await runMonitor(
      seat,
      scripted([decodedWake(wakePayload('phase_active', { mine: true }), seat.session)]).seams,
      options({ once: true })
    );
    expect(fs.readdirSync(path.join(seat.mirror, 'state')).sort()).toEqual([
      'monitor.log',
      'phase.json',
    ]);
  });

  test('--json prints the wake object, byte-identical to wait --json', async () => {
    const seat = bench();
    const payload = wakePayload('phase_active', { mine: true });
    const wake = decodedWake(payload, seat.session);
    const result = await runMonitor(
      seat,
      scripted([wake]).seams,
      options({ once: true, json: true })
    );
    expect(result.out).toHaveLength(1);
    expect(JSON.parse(result.out.join('\n'))).toEqual(payload);
    // The same bytes `printV2Json` gives `wait --json` for the same wake.
    expect(result.out[0]).toBe(compactJson(wake));
  });
});

// ---------------------------------------------------------------------------
// Read-only: the assertion that fails if any write path becomes reachable
// ---------------------------------------------------------------------------

describe('read-only', () => {
  test('the loop asks for a stateless, for-turn wait and nothing else', async () => {
    const seat = bench();
    const script = scripted([decodedWake(wakePayload('phase_active', { mine: true }), seat.session)]);
    await runMonitor(seat, script.seams, options({ once: true }));
    expect(script.calls).toHaveLength(1);
    // `stateless` is what makes it incapable of racing: no `.v2-state` read,
    // no revision cursor, no lock a real command needs.
    expect(script.calls[0]?.stateless).toBe(true);
    expect(script.calls[0]?.forTurn).toBe(true);
    expect(script.calls[0]?.mirror).toBeDefined();
  });

  test('the live seams reach no write path at all', () => {
    // The wait engine's three write hooks — remember the page (U03), mirror the
    // page (U04/U07), rewrite the header (U04) — are all inert for the monitor.
    // If a future edit wires one of them up, this fails.
    const seat = bench();
    const seams = liveMonitorSeams(seat.sessionPath, seat.session, systemWaitClock);
    expect(typeof seams.waitUntilTurn).toBe('function');
    expect(seams.clock).toBe(systemWaitClock);
  });

  test('never reads or writes the revision cursor', async () => {
    // Staying in phase mode is what makes it incapable of racing.  This one
    // runs the *real* wait engine over a fake supervisor, so the request URL
    // and the state file are both observable.
    const seat = bench();
    const urls: string[] = [];
    const fetchImpl = (async (input: Parameters<typeof fetch>[0]): Promise<Response> => {
      urls.push(typeof input === 'string' ? input : input instanceof URL ? input.href : input.url);
      return jsonResponse(wakePayload('phase_active', { mine: true }));
    }) as typeof fetch;

    const reads: string[] = [];
    const guarded: SessionStoreApi = {
      ...seat.store,
      readState: (sessionPath, session) => {
        reads.push(sessionPath);
        return seat.store.readState(sessionPath, session);
      },
      writeState: (sessionPath, next) => {
        reads.push(`write:${sessionPath}`);
        return seat.store.writeState(sessionPath, next);
      },
    };

    const out: string[] = [];
    const log = console.log;
    const error = console.error;
    console.log = (...parts: ReadonlyArray<unknown>) => out.push(parts.join(' '));
    console.error = () => undefined;
    try {
      const code = await Effect.runPromise(
        Effect.provide(
          commandMonitor(
            { ...options({ once: true }), session: seat.sessionPath },
            {
              seams: (sessionPath, session) => liveMonitorSeams(sessionPath, session, systemWaitClock),
              kill: () => Effect.succeed('signalled'),
              clock: systemWaitClock,
              pid: () => 1,
              since: () => holderSince(new Date(0)),
              exitProcess: (status) => Effect.die(exitDefect(status)),
            },
            {}
          ),
          Layer.mergeAll(
            Layer.succeed(PrivateFs, seat.files),
            Layer.succeed(SessionStore, guarded),
            Layer.succeed(V2Client, v2ClientFor(httpFor(fetchImpl), () => Effect.void))
          )
        )
      );
      expect(code).toBe(0);
    } finally {
      console.log = log;
      console.error = error;
    }
    expect(out.join('\n')).toContain('woke phase_active');
    expect(urls[0]).toContain('until=phase');
    expect(urls.some((url) => url.includes('after_state_token'))).toBe(false);
    // `.v2-state` is neither read nor written: the cache the real commands
    // race over is never opened.
    expect(reads).toEqual([]);
    expect(fs.existsSync(path.join(seat.scratch.workspace.stateRoot, 'session-codex-gpt-5.6-sol.v2-state'))).toBe(
      false
    );
  });
});

// ---------------------------------------------------------------------------
// --exec
// ---------------------------------------------------------------------------

/** Enough environment for `/bin/sh` and nothing this workspace cares about. */
const quietEnvironment = (): Record<string, string> => ({
  PATH: process.env['PATH'] ?? '/usr/bin:/bin',
});

describe('--exec', () => {
  test('runs a hook with the game in its environment', async () => {
    const seat = bench();
    const script = scripted([decodedWake(wakePayload('phase_active', { mine: true }), seat.session)]);
    const result = await runMonitor(
      seat,
      script.seams,
      options({ once: true, exec: 'notify-me' })
    );
    expect(result.code).toBe(0);
    expect(script.hooks).toHaveLength(1);
    expect(script.hooks[0]?.command).toBe('notify-me');
    const environment = script.hooks[0]?.environment ?? {};
    expect({
      FREECIV_GAME_ID: environment['FREECIV_GAME_ID'],
      FREECIV_TURN: environment['FREECIV_TURN'],
      FREECIV_PHASE: environment['FREECIV_PHASE'],
      FREECIV_YOUR_TURN: environment['FREECIV_YOUR_TURN'],
      FREECIV_DEADLINE_S: environment['FREECIV_DEADLINE_S'],
      FREECIV_HOLDER_LABEL: environment['FREECIV_HOLDER_LABEL'],
    }).toEqual({
      FREECIV_GAME_ID: seat.session.gameId,
      FREECIV_TURN: '3',
      FREECIV_PHASE: '1',
      FREECIV_YOUR_TURN: '1',
      FREECIV_DEADLINE_S: '587',
      FREECIV_HOLDER_LABEL: '',
    });
    // Every hook invocation is recorded with its string: the log is what makes
    // a contract violation auditable after the fact.
    expect(monitorLog(seat)).toContain('exec notify-me');
  });

  test('names the holder when the phase is somebody elses', async () => {
    const seat = bench();
    const script = scripted([
      decodedWake(wakePayload('timeout', { mine: false }), seat.session),
      decodedWake(wakePayload('game_terminal', { mine: false, gameState: 'completed' }), seat.session),
    ]);
    await runMonitor(seat, script.seams, options({ exec: 'notify-me' }));
    // Only the terminal wake reaches the hook: a still-theirs timeout is not an
    // announcement.
    expect(script.hooks).toHaveLength(1);
    expect(script.hooks[0]?.environment['FREECIV_YOUR_TURN']).toBe('0');
    expect(script.hooks[0]?.environment['FREECIV_TURN']).toBe('');
  });

  test('a failing hook is reported and never stops the monitor', async () => {
    const seat = bench();
    const script = scripted([decodedWake(wakePayload('phase_active', { mine: true }), seat.session)], {
      hookStatus: 3,
    });
    const result = await runMonitor(seat, script.seams, options({ once: true, exec: 'boom' }));
    expect(result.code).toBe(0);
    expect(result.err.join('\n')).toContain('--exec exited 3');
    expect(monitorLog(seat)).toContain('exec exited 3');
  });

  test('a hook that cannot start is reported and never stops the monitor', async () => {
    const seat = bench();
    const script = scripted([decodedWake(wakePayload('phase_active', { mine: true }), seat.session)], {
      hookThrows: 'no such file',
    });
    const result = await runMonitor(seat, script.seams, options({ once: true, exec: 'nope' }));
    expect(result.code).toBe(0);
    expect(result.err.join('\n')).toContain('--exec did not run: no such file');
    expect(monitorLog(seat)).toContain('exec failed to start: no such file');
  });

  test('a hook that plays the game is refused', async () => {
    // `--exec 'just do ... --end'` is an autoplay bot in one flag.  Imperfect
    // by design — a wrapper script defeats it — but it turns a silent contract
    // violation into a deliberate bypass.
    const seat = bench();
    for (const hook of [
      'just do "u1 fortify" --end',
      'just turn --end --await',
      './play batch --action-id a1',
      'sleep 1; just retry --batch-id b',
      'notify && just start',
    ]) {
      const result = await runMonitor(seat, scripted([]).seams, options({ once: true, exec: hook }));
      expect(result.failure).toContain('never plays');
    }
  });

  test('a hook that only notifies is allowed', () => {
    for (const hook of [
      'curl -X POST https://hooks.example/notify',
      "osascript -e 'display notification \"your turn\"'",
      'echo done >> /tmp/turns.log',
      'cat state/phase.json | jq .turn',
    ]) {
      expect(monitorExecRefusal(hook)).toBe('');
    }
  });

  // -------------------------------------------------------------------------
  // A hook killed by a signal
  // -------------------------------------------------------------------------
  //
  // `_run_monitor_hook` reports `completed.returncode`, which CPython makes the
  // *negated* signal number when the child dies by a signal.  A long-lived
  // monitor meets this on ordinary paths — `timeout 5 notify-send …`, a SIGPIPE
  // down a closed pager, an OOM kill, a segfaulting notifier — and `state/
  // monitor.log` is the auditable record of every invocation, so a non-status
  // written there defeats the file's purpose.

  test('a signalled hook is reported as CPythons negative signal number', async () => {
    // python3 -c 'subprocess.run("kill -TERM $$", shell=True).returncode' → -15
    const seat = bench();
    const script = scripted([decodedWake(wakePayload('phase_active', { mine: true }), seat.session)], {
      hookStatus: -15,
    });
    const result = await runMonitor(seat, script.seams, options({ once: true, exec: 'boom' }));
    expect(result.code).toBe(0);
    expect(result.err.join('\n')).toContain('--exec exited -15');
    expect(monitorLog(seat)).toContain('exec exited -15');
  });

  test('shellHookRunner numbers a signalled hook the way subprocess.run does', async () => {
    // Bun's `.d.ts` declares `exitCode: number`, but the runtime hands back
    // `null` with `signalCode: 'SIGTERM'` here, so `tsc` cannot catch a
    // straight passthrough — only this test can.
    const outcome = await Effect.runPromise(shellHookRunner('kill -TERM $$', quietEnvironment()));
    expect(outcome).toEqual({ _tag: 'exited', status: -15 });

    const segv = await Effect.runPromise(shellHookRunner('kill -SEGV $$', quietEnvironment()));
    expect(segv).toEqual({ _tag: 'exited', status: -11 });
  });

  test('shellHookRunner still reports an ordinary exit as itself', async () => {
    expect(await Effect.runPromise(shellHookRunner('exit 3', quietEnvironment()))).toEqual({
      _tag: 'exited',
      status: 3,
    });
    expect(await Effect.runPromise(shellHookRunner('true', quietEnvironment()))).toEqual({
      _tag: 'exited',
      status: 0,
    });
  });

  test('completedProcessStatus maps every arm of CPythons returncode', () => {
    expect(completedProcessStatus(0, undefined)).toBe(0);
    expect(completedProcessStatus(3, undefined)).toBe(3);
    // A normal exit wins even if a signal name rode along.
    expect(completedProcessStatus(0, 'SIGTERM')).toBe(0);
    expect(completedProcessStatus(null, 'SIGTERM')).toBe(-15);
    expect(completedProcessStatus(null, 'SIGSEGV')).toBe(-11);
    expect(completedProcessStatus(null, 'SIGINT')).toBe(-2);
    // Never a hole in the log: an unnameable signal still records a non-status.
    expect(completedProcessStatus(null, 'SIGNOTATHING')).toBe(-1);
    expect(completedProcessStatus(null, null)).toBe(-1);
    expect(completedProcessStatus(null, undefined)).toBe(-1);
  });
});

// ---------------------------------------------------------------------------
// --status and --stop
// ---------------------------------------------------------------------------

describe('--status', () => {
  test('reports the running monitor and what it watches', async () => {
    const seat = bench();
    const idle = await runMonitor(seat, scripted([]).seams, options({ status: true }));
    expect(idle.code).toBe(V2_WAIT_EXIT_RETRY);
    expect(idle.out.join('\n')).toContain('monitor not running');

    Effect.runSync(
      Effect.provide(
        updatePhaseMarker(seat.mirror, phaseHealthPayload({ mine: false })),
        Layer.succeed(PrivateFs, seat.files)
      )
    );
    const running = await Effect.runPromise(
      Effect.provide(
        withMonitorLock(seat.sessionPath, { pid: 41207, since: '16:21:04' }, () =>
          Effect.promise(async () => runMonitor(seat, scripted([]).seams, options({ status: true })))
        ),
        Layer.succeed(PrivateFs, seat.files)
      )
    );
    expect(running.code).toBe(0);
    const text = running.out.join('\n');
    expect(text).toContain('monitor running (pid 41207');
    expect(text).toContain('since 16:21:04');
    expect(text).toContain('watching t3/p1');
    expect(text).toContain('seat 2 holds it');
  });
});

describe('--stop', () => {
  test('says so when nothing is running', async () => {
    const seat = bench();
    const result = await runMonitor(seat, scripted([]).seams, options({ stop: true }));
    expect(result.code).toBe(0);
    expect(result.out.join('\n')).toContain('monitor not running');
  });

  test('signals the monitor and confirms it released', async () => {
    const seat = bench();
    const released: number[] = [];
    const lock = Effect.provide(
      withMonitorLock(seat.sessionPath, { pid: 4242 }, () =>
        Effect.promise(async () =>
          runMonitor(seat, scripted([]).seams, options({ stop: true }), {
            // The real monitor exits on SIGTERM, which releases the flock; the
            // test releases it by leaving the `withMonitorLock` scope, so the
            // kill is scripted to succeed and the *next* probe is the one that
            // has to see the lock free.
            kill: (pid) =>
              Effect.sync(() => {
                released.push(pid);
                return 'signalled';
              }),
          })
        )
      ),
      Layer.succeed(PrivateFs, seat.files)
    );
    // Inside the scope the lock is still held, so `--stop` must time out.
    const held = await Effect.runPromise(lock);
    expect(released).toEqual([4242]);
    expect(held.failure).toContain('did not stop within 5s');

    // With the holder gone, the same call confirms the release.
    const gone = await runMonitor(seat, scripted([]).seams, options({ stop: true }));
    expect(gone.code).toBe(0);
    expect(gone.out.join('\n')).toContain('monitor not running');
  }, 20_000);

  test('stopping an already-dead monitor is not an error', async () => {
    const seat = bench();
    const result = await Effect.runPromise(
      Effect.provide(
        withMonitorLock(seat.sessionPath, { pid: 4242 }, () =>
          Effect.promise(async () =>
            runMonitor(seat, scripted([]).seams, options({ stop: true }), {
              kill: () => Effect.succeed('gone'),
            })
          )
        ),
        Layer.succeed(PrivateFs, seat.files)
      )
    );
    expect(result.code).toBe(0);
    expect(result.out.join('\n')).toContain('monitor not running');
  });

  test('a lock that names no process refuses rather than guessing', async () => {
    const seat = bench();
    const result = await Effect.runPromise(
      Effect.provide(
        withMonitorLock(seat.sessionPath, { since: '16:21:04' }, () =>
          Effect.promise(async () => runMonitor(seat, scripted([]).seams, options({ stop: true })))
        ),
        Layer.succeed(PrivateFs, seat.files)
      )
    );
    expect(result.failure).toContain('does not name a process to stop');
  });

  test('a monitor owned by another user is named, not silently ignored', async () => {
    const seat = bench();
    const result = await Effect.runPromise(
      Effect.provide(
        withMonitorLock(seat.sessionPath, { pid: 4242 }, () =>
          Effect.promise(async () =>
            runMonitor(seat, scripted([]).seams, options({ stop: true }), {
              kill: () => Effect.succeed('forbidden'),
            })
          )
        ),
        Layer.succeed(PrivateFs, seat.files)
      )
    );
    expect(result.failure).toBe('the monitor (pid 4242) belongs to another user');
  });
});

// ---------------------------------------------------------------------------
// The holder record's own clock
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// The two wake lines, as bytes
// ---------------------------------------------------------------------------

describe('the wake lines', () => {
  test('a wake that carries a revision stamps it after the turn', () => {
    // The monitor itself can never reach this branch — it always waits in phase
    // mode, and `until=phase` with a non-null `state_revision` is a broken wake
    // contract — but the line is shared, so the stamp is pinned here.
    const seat = bench();
    const wake = Effect.runSync(
      decodeWait(
        waitPayload({
          wake_reason: 'revision_changed',
          health: phaseHealthPayload({ mine: true }),
          state_revision: { turn: 5, revision: 12, state_token: 'token_5_12' },
        }),
        seat.session,
        { until: 'revision', afterStateToken: 'token_5_11' }
      )
    );
    expect(monitorAnnounceLine(wake)).toBe(
      'T3 rev12/t5 | woke revision_changed | running | ' +
        'phase awaiting_agent t3/p1 active 587s left | next: just turn'
    );
  });

  test('a terminal game has no phase, so the turn is unknown', () => {
    const seat = bench();
    const wake = decodedWake(
      wakePayload('game_terminal', { mine: false, gameState: 'completed' }),
      seat.session
    );
    expect(monitorTerminalLine(wake)).toBe('T? | GAME OVER | completed | next: just result');
  });
});

describe('the holder record', () => {
  test('stamps the wall clock the way CPython did', () => {
    expect(holderSince(new Date(2026, 0, 2, 16, 21, 4))).toBe('16:21:04');
  });
});

// ---------------------------------------------------------------------------
// The flag surface, through the real `@effect/cli` command
// ---------------------------------------------------------------------------

const runCommand = async (
  seat: Bench,
  seams: MonitorSeams,
  flags: ReadonlyArray<string>,
  harnessOverrides: Partial<MonitorHarness> = {}
): Promise<{ readonly code: number; readonly out: ReadonlyArray<string> }> => {
  const command = monitorCommandWith(harnessFor(seams, harnessOverrides));
  const out: string[] = [];
  const log = console.log;
  const error = console.error;
  console.log = (...parts: ReadonlyArray<unknown>) => out.push(parts.join(' '));
  console.error = () => undefined;
  try {
    const exit = await Effect.runPromiseExit(
      Effect.provide(
        Command.run(command, { name: 'play', version: '0.1.0' })([
          'bun',
          'play',
          '--session',
          seat.sessionPath,
          ...flags,
        ]),
        Layer.mergeAll(
          Layer.succeed(PrivateFs, seat.files),
          Layer.succeed(SessionStore, seat.store),
          unusedClient,
          BunContext.layer
        )
      )
    );
    if (Exit.isSuccess(exit)) return { code: 0, out };
    // A status outside the four-code contract leaves through `process.exit`,
    // which the harness models as a defect; everything else is the usual
    // `ExitCodeSignal` (0/2/75/66) or a refusal.
    const died = Option.getOrNull(Cause.dieOption(exit.cause));
    if (isExitDefect(died)) return { code: died.status, out };
    const failure = Option.getOrNull(Cause.failureOption(exit.cause));
    return { code: failure !== null && failure._tag === 'ExitCodeSignal' ? failure.code : 2, out };
  } finally {
    console.log = log;
    console.error = error;
  }
};

describe('play monitor', () => {
  test('--once announces and exits 0', async () => {
    const seat = bench();
    const script = scripted([decodedWake(wakePayload('phase_active', { mine: true }), seat.session)]);
    const result = await runCommand(seat, script.seams, ['--once']);
    expect(result.code).toBe(0);
    expect(result.out[0]).toContain('T3 | woke phase_active');
  });

  test('--exit-code reaches the process status verbatim, in the contract and out of it', async () => {
    // CPython's `main()` is `sys.exit(int(handler(args)))`, so every N the flag
    // accepts is the status the process carries. 75 rides the exit contract;
    // 42 and 1 do not, and must not be flattened onto it (NOTES.md §18.2).
    for (const status of [0, 1, 2, 42, 66, 75, 255]) {
      const seat = bench();
      const script = scripted([
        decodedWake(wakePayload('phase_active', { mine: true }), seat.session),
      ]);
      const result = await runCommand(seat, script.seams, ['--once', '--exit-code', String(status)]);
      expect(result.code).toBe(status);
      expect(result.out[0]).toContain('T3 | woke phase_active');
    }
  });

  test('--exit-code 1 is never reported as this CLIs refusal status', async () => {
    // The canonical escalate value for cron/systemd supervisors. Reading 2 here
    // would tell a harness its own invocation was rejected while stdout says
    // the phase opened — the incident the flag exists to prevent.
    const seat = bench();
    const script = scripted([decodedWake(wakePayload('phase_active', { mine: true }), seat.session)]);
    const escalated = await runCommand(seat, script.seams, ['--once', '--exit-code', '1']);
    expect(escalated.code).toBe(1);
    expect(escalated.code).not.toBe(2);

    // …and a real refusal still is 2, so the two remain distinguishable.
    const refused = await runCommand(seat, scripted([]).seams, ['--once', '--exit-code', '300']);
    expect(refused.code).toBe(2);
  });

  test('75 still leaves by the exit contract, not by process.exit', async () => {
    const seat = bench();
    const script = scripted([decodedWake(wakePayload('phase_active', { mine: true }), seat.session)]);
    const result = await runCommand(seat, script.seams, ['--once', '--exit-code', '75'], {
      exitProcess: (status) =>
        Effect.die(new Error(`75 is in the contract; process.exit(${status}) is wrong`)),
    });
    expect(result.code).toBe(V2_WAIT_EXIT_RETRY);
  });

  test('a terminal game exits EX_NOINPUT', async () => {
    const seat = bench();
    const script = scripted([
      decodedWake(wakePayload('game_terminal', { mine: false, gameState: 'completed' }), seat.session),
    ]);
    const result = await runCommand(seat, script.seams, ['--once']);
    expect(result.code).toBe(V2_WAIT_EXIT_TERMINAL);
  });

  test('--status with nothing running exits EX_TEMPFAIL', async () => {
    const seat = bench();
    const result = await runCommand(seat, scripted([]).seams, ['--status']);
    expect(result.code).toBe(V2_WAIT_EXIT_RETRY);
    expect(result.out.join('\n')).toContain('monitor not running');
  });

  test('both spellings of --wait-s are accepted, and never together', async () => {
    const seat = bench();
    for (const spelling of ['--wait-s', '--wait_s']) {
      const script = scripted([
        decodedWake(wakePayload('phase_active', { mine: true }), seat.session),
      ]);
      const result = await runCommand(seat, script.seams, ['--once', spelling, '30']);
      expect(result.code).toBe(0);
    }
    const both = await runCommand(seat, scripted([]).seams, [
      '--once',
      '--wait-s',
      '30',
      '--wait_s',
      '30',
    ]);
    expect(both.code).toBe(2);
  });
});
