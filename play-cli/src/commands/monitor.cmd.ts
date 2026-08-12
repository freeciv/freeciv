/**
 * `play monitor` — watch this seat's phase and say, once, every time it opens.
 *
 * Ports `_monitor_status` (client.py:10457-10476), `_monitor_stop` (10479-10510),
 * `command_monitor` (10513-10587) and the argparse surface at 11824-11878.
 *
 * Two bindings, and the choice between them is mechanical rather than a matter
 * of taste.  `--once` blocks, announces and exits: started in the background by
 * a harness that re-invokes on process exit, it is a wake-up call with no tool
 * timeout over it and no polling loop.  Persistent mode is one long-lived
 * process for a harness, a cron agent or a human that wants a line per turn
 * instead.
 *
 * Neither is required.  `do "…" --end --await --brief` remains one call per turn
 * for any harness that can hold a block, and nothing in the turn loop depends on
 * a monitor being alive — it is an ergonomic layer over the same bounded wait,
 * and a safety net under `--await` when it is running.
 */
import { Command, Options } from '@effect/cli';
import { Effect, Option } from 'effect';
import { playerError, type PlayerError, type SessionMissingError , attemptOr } from 'src/errors';
import { exitWith, isExitCode, V2_WAIT_EXIT_RETRY } from 'src/exit';
import { dualFloat, resolveDual } from 'src/options';
import {
  MONITOR_NOT_RUNNING,
  MONITOR_STATUS_IDLE,
  monitorAlreadyRunningLine,
  monitorRunningLine,
  monitorStoppedLine,
} from 'src/render/monitor';
import { render } from 'src/render/primitives';
import type { JsonObject } from 'src/schema/primitives';
import { jsonRequested } from 'src/services/json-output';
import { mirrorPath, readPhaseMarker } from 'src/services/mirror';
import { monitorExecRefusal, monitorExecRefusalMessage, V2_MONITOR_MAX_EXIT_CODE } from 'src/services/monitor-hook';
import { monitorHolder, withMonitorLock } from 'src/services/monitor-lock';
import { liveMonitorSeams, monitorLoop, type MonitorSeams } from 'src/services/monitor-loop';
import { PrivateFs } from 'src/services/private-fs';
import { SessionStore, type Session } from 'src/services/session-store';
import { V2Client } from 'src/services/v2-client';
import { systemWaitClock, waitArgs, type WaitClock } from 'src/services/wait';

// ---------------------------------------------------------------------------
// The seams a test replaces
// ---------------------------------------------------------------------------

/** What `os.kill(pid, SIGTERM)` did. */
export type KillOutcome = 'signalled' | 'gone' | 'forbidden';

export const systemKill = (pid: number): Effect.Effect<KillOutcome> =>
  Effect.sync(() =>
    attemptOr(
      () => {
        process.kill(pid, 'SIGTERM');
        return 'signalled';
      },
      (cause) => {
        const code = (cause as NodeJS.ErrnoException).code;
        if (code === 'ESRCH') return 'gone';
        if (code === 'EPERM') return 'forbidden';
        return 'signalled';
      }
    )
  );

/**
 * `sys.exit(status)` for a status the 0/2/75/66 contract cannot carry.
 *
 * Only `--exit-code N` produces one, and only for `N ∉ {0, 2, 75, 66}`; see
 * `monitorCommandWith` for why the alternative — clamping — is not available
 * here.  Writing this way is safe because on POSIX `process.stdout` is
 * synchronous for TTYs, files and pipes alike, so the announcement is already
 * out by the time the status is set.
 */
export const systemProcessExit = (status: number): Effect.Effect<never> =>
  Effect.sync(() => process.exit(status));

/** Everything the command reaches outside its own logic. */
export interface MonitorHarness {
  readonly seams: (sessionPath: string, session: Session) => MonitorSeams;
  readonly kill: (pid: number) => Effect.Effect<KillOutcome>;
  readonly clock: WaitClock;
  /** `os.getpid()` and `datetime.now()`, as the holder record records them. */
  readonly pid: () => number;
  readonly since: () => string;
  /** Finish the process with a status `ExitCodeSignal` is not allowed to hold. */
  readonly exitProcess: (status: number) => Effect.Effect<never>;
}

const pad = (value: number): string => String(value).padStart(2, '0');

/** CPython's `datetime.now().strftime("%H:%M:%S")`. */
export const holderSince = (at: Date = new Date()): string =>
  `${pad(at.getHours())}:${pad(at.getMinutes())}:${pad(at.getSeconds())}`;

export const liveMonitorHarness: MonitorHarness = {
  seams: (sessionPath, session) => liveMonitorSeams(sessionPath, session),
  kill: systemKill,
  clock: systemWaitClock,
  pid: () => process.pid,
  since: () => holderSince(),
  exitProcess: systemProcessExit,
};

// ---------------------------------------------------------------------------
// --status
// ---------------------------------------------------------------------------

const markerOf = (
  sessionPath: string
): Effect.Effect<JsonObject | null, never, PrivateFs> =>
  Effect.flatMap(
    Effect.orElseSucceed(mirrorPath(sessionPath), (): string | null => null),
    (dir) => (dir === null ? Effect.succeed(null) : readPhaseMarker(dir))
  );

/** `_monitor_status` — is one running, since when, and on what. */
export const monitorStatus = (
  sessionPath: string
): Effect.Effect<number, never, PrivateFs> =>
  Effect.gen(function* () {
    const holder = yield* monitorHolder(sessionPath);
    if (holder === null) {
      yield* render(MONITOR_STATUS_IDLE);
      // "no monitor" is EX_TEMPFAIL, not success: a supervisor asking whether
      // its watcher is alive wants a status it can branch on.
      return V2_WAIT_EXIT_RETRY;
    }
    yield* render([monitorRunningLine(holder, yield* markerOf(sessionPath))]);
    return 0;
  });

// ---------------------------------------------------------------------------
// --stop
// ---------------------------------------------------------------------------

/** How long `--stop` waits for the lock to be released before refusing. */
export const MONITOR_STOP_TIMEOUT_S = 5.0;
const MONITOR_STOP_POLL_S = 0.1;

/** `_monitor_stop` — ask the running monitor to exit, and confirm that it did. */
export const monitorStop = (
  sessionPath: string,
  harness: MonitorHarness = liveMonitorHarness
): Effect.Effect<number, PlayerError, PrivateFs> =>
  Effect.gen(function* () {
    const holder = yield* monitorHolder(sessionPath);
    if (holder === null) {
      yield* render([MONITOR_NOT_RUNNING]);
      return 0;
    }
    const pid = holder['pid'];
    if (typeof pid !== 'number' || !Number.isInteger(pid) || pid <= 1) {
      return yield* Effect.fail(
        playerError(
          'the monitor lock does not name a process to stop; if a monitor is ' +
            'still running, stop it where it was started'
        )
      );
    }
    const outcome = yield* harness.kill(pid);
    if (outcome === 'gone') {
      // The kernel released the flock when it died; nothing to stop.
      yield* render([MONITOR_NOT_RUNNING]);
      return 0;
    }
    if (outcome === 'forbidden') {
      return yield* Effect.fail(playerError(`the monitor (pid ${pid}) belongs to another user`));
    }
    const deadline = (yield* harness.clock.monotonic()) + MONITOR_STOP_TIMEOUT_S;
    const stopped = yield* Effect.iterate(
      { done: false, released: false },
      {
        while: (state) => !state.done,
        body: () =>
          Effect.gen(function* () {
            if ((yield* harness.clock.monotonic()) >= deadline) {
              return { done: true, released: false };
            }
            if ((yield* monitorHolder(sessionPath)) === null) {
              return { done: true, released: true };
            }
            yield* harness.clock.sleep(MONITOR_STOP_POLL_S);
            return { done: false, released: false };
          }),
      }
    );
    if (stopped.released) {
      yield* render([monitorStoppedLine(pid)]);
      return 0;
    }
    return yield* Effect.fail(
      playerError(
        `the monitor (pid ${pid}) did not stop within 5s; it is mid-request ` +
          'and will exit at the end of it — re-run `just monitor --stop`'
      )
    );
  });

// ---------------------------------------------------------------------------
// command_monitor
// ---------------------------------------------------------------------------

export interface MonitorOptions {
  readonly session: string;
  readonly waitS: number;
  readonly pollS: number;
  readonly once: boolean;
  readonly stop: boolean;
  readonly status: boolean;
  readonly exec: string;
  readonly exitCode: number;
  readonly maxS: number | null;
  readonly json: boolean;
}

export const monitorDefaults: MonitorOptions = {
  session: '',
  waitS: 120,
  pollS: 1,
  once: false,
  stop: false,
  status: false,
  exec: '',
  exitCode: 0,
  maxS: null,
  json: false,
};

/**
 * `command_monitor` — dispatch the five bindings and report the exit status.
 *
 * The `--exec` refusal is checked *before* the session is resolved, exactly as
 * the Python does: a hook that would play the game is a contract violation
 * whether or not this workspace has a seat.
 */
export const commandMonitor = (
  options: MonitorOptions,
  harness: MonitorHarness = liveMonitorHarness,
  environment?: Readonly<Record<string, string | undefined>>
): Effect.Effect<number, PlayerError | SessionMissingError, PrivateFs | SessionStore | V2Client> =>
  Effect.gen(function* () {
    const exitCode = Math.trunc(options.exitCode || 0);
    if (!(exitCode >= 0 && exitCode <= V2_MONITOR_MAX_EXIT_CODE)) {
      return yield* Effect.fail(
        playerError(`--exit-code must be in [0, ${V2_MONITOR_MAX_EXIT_CODE}]`)
      );
    }
    const hook = options.exec.trim();
    const refused = hook === '' ? '' : monitorExecRefusal(hook);
    if (refused !== '') {
      return yield* Effect.fail(playerError(monitorExecRefusalMessage(refused)));
    }

    const store = yield* SessionStore;
    const loaded = yield* store.resolveV2(options.session);
    const sessionPath = loaded.path;

    if (options.status) return yield* monitorStatus(sessionPath);
    if (options.stop) return yield* monitorStop(sessionPath, harness);

    const seams = harness.seams(sessionPath, loaded.session);
    const loopOptions = {
      once: options.once,
      hook,
      exitCode,
      json: jsonRequested('monitor', options.json, environment ?? process.env),
      maxS: options.maxS,
      waitArgs: waitArgs({ waitS: options.waitS, pollS: options.pollS, until: 'phase' }),
    };
    if (options.once) {
      // `--once` takes no lock, so any number may run alongside the persistent
      // one; that is what lets the two bindings compose.
      return yield* monitorLoop(sessionPath, seams, loopOptions);
    }

    const holder: JsonObject = {
      pid: harness.pid(),
      since: harness.since(),
      game_id: loaded.session.gameId,
      seat_id: loaded.session.seatId,
    };
    return yield* withMonitorLock(sessionPath, holder, (running) =>
      running === null
        ? monitorLoop(sessionPath, seams, loopOptions)
        : Effect.gen(function* () {
            // Idempotent by construction: starting a second one is not an
            // error, it is a no-op that reports the first.
            yield* render([monitorAlreadyRunningLine(running, yield* markerOf(sessionPath))]);
            return 0;
          })
    );
  });

// ---------------------------------------------------------------------------
// The @effect/cli surface
// ---------------------------------------------------------------------------

const sessionOption = Options.text('session').pipe(
  Options.withDefault(''),
  Options.withDescription('the private session file this command acts against')
);

const onceOption = Options.boolean('once').pipe(
  Options.withDescription(
    'block until your phase opens, announce it, and exit; takes no lock, so any ' +
      'number may run alongside a persistent monitor'
  )
);

const stopOption = Options.boolean('stop').pipe(
  Options.withDescription('ask the persistent monitor to exit')
);

const statusOption = Options.boolean('status').pipe(
  Options.withDescription('report whether a monitor is running, since when, and on what')
);

const execOption = Options.text('exec').pipe(
  Options.withDefault(''),
  Options.withDescription(
    'run this shell command on every announcement, with ' +
      'FREECIV_GAME_ID/TURN/PHASE/YOUR_TURN/DEADLINE_S/HOLDER_LABEL in its ' +
      'environment. It may notify your harness; it may never issue game commands'
  )
);

const exitCodeOption = Options.integer('exit-code').pipe(
  Options.withDefault(0),
  Options.withDescription(
    'exit with this status on an announcement (default 0), for a harness that ' +
      'only escalates on a particular status'
  )
);

const maxSOption = Options.optional(
  Options.float('max-s').pipe(
    Options.withDescription('give up after SECONDS of not being handed the phase (exit 75)')
  )
);

const jsonOption = Options.boolean('json').pipe(
  Options.withDescription('print the full-fidelity JSON payload instead of text')
);

/** Build the command against a given harness, so a test can script the wait. */
export const monitorCommandWith = (harness: MonitorHarness) =>
  Command.make(
    'monitor',
    {
      session: sessionOption,
      waitS: dualFloat('wait-s'),
      pollS: dualFloat('poll-s'),
      once: onceOption,
      stop: stopOption,
      status: statusOption,
      exec: execOption,
      exitCode: exitCodeOption,
      maxS: maxSOption,
      json: jsonOption,
    },
    (options) =>
      Effect.gen(function* () {
        const waitS = yield* resolveDual('wait-s', options.waitS, 120);
        const pollS = yield* resolveDual('poll-s', options.pollS, 1);
        const status = yield* commandMonitor(
          {
            session: options.session,
            waitS,
            pollS,
            once: options.once,
            stop: options.stop,
            status: options.status,
            exec: options.exec,
            exitCode: options.exitCode,
            maxS: Option.getOrNull(options.maxS),
            json: options.json,
          },
          harness
        );
        if (status === 0) return;
        if (isExitCode(status)) return yield* Effect.fail(exitWith(status));
        // `--exit-code N` promises "exit with this status", `N ∈ [0, 255]`, and
        // CPython's `sys.exit(int(handler(args)))` hands it to the process
        // verbatim.  The CLI's own channel is `ExitCodeSignal`, typed
        // `0 | 2 | 75 | 66`, and it clamps everything else to `2` — the refusal
        // status.  A harness that escalates on `--exit-code 1` would then read
        // "your invocation was rejected" beneath stdout saying the phase
        // opened, which is exactly the incident the flag exists to prevent.  So
        // the out-of-contract statuses leave here rather than through
        // `cli-main`; NOTES.md §18.2 has the core-side fix that retires this.
        return yield* harness.exitProcess(status);
      })
  );

export const monitorCommand = monitorCommandWith(liveMonitorHarness);
