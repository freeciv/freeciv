/**
 * The watch itself: absorb transport faults, announce each activation.
 *
 * Ports `V2_MONITOR_BACKOFF_START_S` / `V2_MONITOR_BACKOFF_MAX_S`
 * (client.py:10260-10261), `_announced_tuple` (10303-10315), `_missed_phase`
 * (10318-10336) and `_monitor_loop` (10590-10696).
 *
 * **Strictly read-only.**  The loop never ends a phase, never submits a batch
 * and never touches `.v2-state`: its wait runs `stateless`, so the revision
 * cursor is neither read nor written and a process that may be alive for the
 * whole game can never race a real command.  Its only writes are
 * `state/phase.json` and the append-only `state/monitor.log`, which is why the
 * mirror hook is `mirrorPhaseMarker` and not `mirrorHealth` — the header, the
 * tables and the alias cache all belong to commands that actually play.
 *
 * The one alarm no other command can raise lives here: a phase of this seat's
 * that opened and was ended by timeout while nothing reached the agent.
 */
import { Console, Effect } from 'effect';
import type { PlayError, PlayerError } from 'src/errors';
import { V2_WAIT_EXIT_RETRY, V2_WAIT_EXIT_TERMINAL } from 'src/exit';
import {
  MONITOR_MAX_S_LINE,
  MONITOR_REBOUND_LINE,
  missedLine,
  monitorAnnounceLine,
  monitorTerminalLine,
} from 'src/render/monitor';
import { holderSeat } from 'src/render/phase';
import { formatG, render } from 'src/render/primitives';
import { waitingTickLine } from 'src/render/wait';
import type { HealthEnvelope } from 'src/schema/health';
import { isJsonObject, type JsonObject, type JsonValue } from 'src/schema/primitives';
import type { WaitEnvelope } from 'src/schema/wait';
import { printV2Json } from 'src/services/json-output';
import {
  mirrorMonitorLog,
  mirrorPath,
  mirrorPhaseMarker,
  readPhaseMarker,
} from 'src/services/mirror';
import { runMonitorHook, shellHookRunner, type HookRunner } from 'src/services/monitor-hook';
import { PrivateFs, type PrivateFsApi } from 'src/services/private-fs';
import { SessionStore, type Session } from 'src/services/session-store';
import { V2Client } from 'src/services/v2-client';
import {
  phaseIsMine,
  seatRebound,
  systemWaitClock,
  waitUntilTurn,
  type WaitArgs,
  type WaitClock,
  type WaitCtx,
  type WaitUntilTurnOptions,
} from 'src/services/wait';

// ---------------------------------------------------------------------------
// The two bounds on a service that is down
// ---------------------------------------------------------------------------

export const V2_MONITOR_BACKOFF_START_S = 1.0;
export const V2_MONITOR_BACKOFF_MAX_S = 30.0;

// ---------------------------------------------------------------------------
// _announced_tuple / _missed_phase
// ---------------------------------------------------------------------------

const wholeOrZero = (value: JsonValue | undefined): number =>
  typeof value === 'number' && Number.isInteger(value) ? value : 0;

/** `_announced_tuple` — the `[incarnation, turn, phase]` identity now open. */
export const announcedTuple = (health: HealthEnvelope): ReadonlyArray<number> | null => {
  const phase = health.phase;
  if (phase === null) return null;
  const turn = phase.turn;
  const number = phase.phase;
  if (turn === null || number === null) return null;
  if (!Number.isInteger(turn) || !Number.isInteger(number)) return null;
  const event = health.last_phase_end;
  return [event === null ? 0 : wholeOrZero(event['incarnation']), turn, number];
};

/** CPython's list `<=`: element-wise, then by length. */
const notAfter = (left: ReadonlyArray<number>, right: ReadonlyArray<JsonValue>): boolean => {
  const width = Math.min(left.length, right.length);
  for (let index = 0; index < width; index += 1) {
    const a = left[index] as number;
    const b = right[index];
    if (typeof b !== 'number') return false;
    if (a !== b) return a < b;
  }
  return left.length <= right.length;
};

/** The recorded announcement, when it is a list at all. */
const announcedBound = (value: JsonValue | undefined): ReadonlyArray<JsonValue> | null =>
  Array.isArray(value) ? value : null;

/** The `[incarnation, turn, phase]` a phase-end event names. */
const endedTuple = (event: JsonObject): ReadonlyArray<number> => [
  wholeOrZero(event['incarnation']),
  wholeOrZero(event['turn']),
  wholeOrZero(event['phase']),
];

/**
 * `_missed_phase` — a phase of this seat's that ended without the monitor
 * announcing it.
 *
 * The one thing a monitor can see that nothing else can.  A phase this seat
 * itself ended (`source=agent`) is never a miss however it was noticed — the
 * agent plainly knew about it, and `--await` opening a turn the monitor never
 * announced is normal composition, not an alarm.
 */
export const missedPhase = (
  health: HealthEnvelope,
  announced: JsonValue | undefined
): JsonObject | null => {
  const event = health.last_phase_end;
  if (event === null || !isJsonObject(event) || event['source'] === 'agent') return null;
  const bound = announcedBound(announced);
  if (bound !== null && notAfter(endedTuple(event), bound)) return null;
  return event;
};

const sameAnnouncement = (
  current: ReadonlyArray<number>,
  announced: JsonValue | undefined
): boolean => {
  const bound = announcedBound(announced);
  if (bound === null || bound.length !== current.length) return false;
  return current.every((item, index) => bound[index] === item);
};

// ---------------------------------------------------------------------------
// The seams the Python's tests patch
// ---------------------------------------------------------------------------

/** `_wait_until_turn`, as the loop calls it. Scripted by the ported tests. */
export type WaitUntilTurnFn = (
  args: WaitArgs,
  options: WaitUntilTurnOptions
) => Effect.Effect<WaitEnvelope, PlayError, V2Client | SessionStore>;

/** The three things `test_client.py` patches around `_monitor_loop`. */
export interface MonitorSeams {
  readonly waitUntilTurn: WaitUntilTurnFn;
  readonly runHook: HookRunner;
  readonly clock: WaitClock;
}

/**
 * The real seams: U05's engine, a real shell, the wall clock.
 *
 * Note what the wait context does *not* carry.  `rememberPage` and
 * `mirrorPage` are inert and `mirrorHealth` is inert, because the monitor
 * always waits `stateless` and always supplies its own `mirror`; the three
 * write paths are therefore unreachable from this loop by construction rather
 * than by discipline, which is what `test/monitor.test.ts` asserts.
 */
export const liveMonitorSeams = (
  sessionPath: string,
  session: Session,
  clock: WaitClock = systemWaitClock
): MonitorSeams => {
  const ctx: WaitCtx = {
    sessionPath,
    session,
    clock,
    hooks: {
      rememberPage: () => Effect.void,
      mirrorPage: () => Effect.void,
      mirrorHealth: () => Effect.void,
      holderSeat,
    },
  };
  return {
    waitUntilTurn: (args, options) => waitUntilTurn(ctx, args, options),
    runHook: shellHookRunner,
    clock,
  };
};

// ---------------------------------------------------------------------------
// The loop
// ---------------------------------------------------------------------------

export interface MonitorLoopOptions {
  readonly once: boolean;
  readonly hook: string;
  readonly exitCode: number;
  readonly json: boolean;
  readonly maxS: number | null;
  readonly waitArgs: WaitArgs;
}

/**
 * Run the watch until it has an answer, and report the status it exits with.
 *
 * The status is a plain number rather than an `ExitCodeSignal` because
 * `--exit-code` may name any value in `[0, 255]`, and `ExitCodeSignal` holds
 * only the four contract codes; `monitorCommandWith` is what decides which
 * channel a given status leaves by (NOTES.md §18.2).
 */
export const monitorLoop = (
  sessionPath: string,
  seams: MonitorSeams,
  options: MonitorLoopOptions
): Effect.Effect<number, PlayerError, PrivateFs | SessionStore | V2Client> =>
  Effect.gen(function* () {
    const files: PrivateFsApi = yield* PrivateFs;
    const mirrorRoot = yield* mirrorPath(sessionPath);
    const marker = yield* readPhaseMarker(mirrorRoot);
    interface LoopState {
      readonly announced: JsonValue | undefined;
      readonly backoff: number;
      readonly misses: number;
      readonly playedTurn: JsonValue | undefined;
      readonly exit: number | null;
    }
    const initial: LoopState = {
      announced: marker === null ? undefined : marker['announced'],
      backoff: V2_MONITOR_BACKOFF_START_S,
      misses: 0,
      playedTurn: undefined,
      exit: null,
    };
    const started = yield* seams.clock.monotonic();
    const deadline = options.maxS === null ? null : started + options.maxS;

    const log = (line: string): Effect.Effect<void, never, PrivateFs> =>
      mirrorMonitorLog(sessionPath, line);
    /** `_monitor_mirror` — the monitor's only write, with the current tuple. */
    const writeMarker = (
      health: HealthEnvelope,
      announced: JsonValue | undefined
    ): Effect.Effect<void> =>
      Effect.provideService(
        mirrorPhaseMarker(sessionPath, health, announced ?? null),
        PrivateFs,
        files
      );

    yield* log(`monitor started${options.once ? ' --once' : ''}`);

    const iteration = (
      state: LoopState
    ): Effect.Effect<LoopState, PlayerError, PrivateFs | SessionStore | V2Client> =>
      Effect.gen(function* () {
        if (yield* seatRebound(sessionPath)) {
          // `just use` pointed this workspace at another game.  A monitor must
          // never keep watching a seat the workspace has left.
          yield* render([MONITOR_REBOUND_LINE]);
          yield* log('stopped: workspace rebound');
          return { ...state, exit: options.once ? options.exitCode : 0 };
        }
        const cap = deadline === null ? null : deadline - (yield* seams.clock.monotonic());
        if (cap !== null && cap <= 0) {
          yield* Console.error(MONITOR_MAX_S_LINE);
          yield* log('stopped: --max-s elapsed');
          return { ...state, exit: V2_WAIT_EXIT_RETRY };
        }

        const waitOptions: WaitUntilTurnOptions = {
          forTurn: true,
          capS: cap,
          stateless: true,
          mirror: (health) => writeMarker(health, state.announced),
          echo: (health) => Console.error(waitingTickLine(health)),
        };
        const outcome = yield* Effect.either(seams.waitUntilTurn(options.waitArgs, waitOptions));
        if (outcome._tag === 'Left') {
          // Absorbed, never raised: a dropped socket is exactly what this
          // component exists to survive.  Backoff so a service that is
          // genuinely down is not hammered.
          const message = outcome.left.message;
          yield* Console.error(`monitor: retrying after ${message}`);
          yield* log(`transport error, retrying in ${formatG(state.backoff)}s: ${message}`);
          yield* seams.clock.sleep(state.backoff);
          return {
            ...state,
            backoff: Math.min(state.backoff * 2, V2_MONITOR_BACKOFF_MAX_S),
          };
        }
        const wait = outcome.right;
        const health = wait.health;

        const missed = missedPhase(health, state.announced);
        const afterMiss = yield* Effect.gen(function* () {
          if (missed === null) return { ...state, backoff: V2_MONITOR_BACKOFF_START_S };
          const misses = state.misses + 1;
          const line = missedLine(missed, misses, state.playedTurn);
          yield* render([line]);
          yield* log(line);
          const announced: JsonValue = [...endedTuple(missed)];
          yield* writeMarker(health, announced);
          return {
            ...state,
            backoff: V2_MONITOR_BACKOFF_START_S,
            misses,
            announced,
          };
        });

        const event = health.last_phase_end;
        const played =
          event !== null && isJsonObject(event) && event['source'] === 'agent'
            ? { ...afterMiss, playedTurn: event['turn'], misses: 0 }
            : afterMiss;

        if (wait.wake_reason === 'game_terminal') {
          const line = monitorTerminalLine(wait);
          yield* (options.json ? printV2Json(wait) : render([line]));
          yield* log(line);
          if (options.hook !== '') {
            yield* runMonitorHook(sessionPath, options.hook, wait, seams.runHook);
          }
          return { ...played, exit: V2_WAIT_EXIT_TERMINAL };
        }

        if (!phaseIsMine(health)) {
          // Their deadline passed and the phase did not move: nothing to
          // announce and nothing to do but ask again, which is the job.
          return played;
        }

        const current = announcedTuple(health);
        if (current !== null && sameAnnouncement(current, played.announced) && !options.once) {
          // A persistent monitor restarted mid-phase must not re-announce the
          // turn the one before it already announced.  `--once` is exempt: it
          // is a bounded question from a caller that wants an answer now, and
          // staying silent would hang a wake-up call for ever on a phase that
          // is already open.  That is also what lets the two bindings run side
          // by side.
          return played;
        }
        const announced: JsonValue | undefined = current === null ? undefined : [...current];
        const line = monitorAnnounceLine(wait);
        yield* (options.json ? printV2Json(wait) : render([line]));
        yield* log(line);
        yield* writeMarker(health, announced);
        if (options.hook !== '') {
          yield* runMonitorHook(sessionPath, options.hook, wait, seams.runHook);
        }
        return options.once
          ? { ...played, announced, exit: options.exitCode }
          : { ...played, announced };
      });

    const finished = yield* Effect.iterate(initial, {
      while: (state) => state.exit === null,
      body: iteration,
    });
    return finished.exit ?? 0;
  });
