/**
 * `monitor --exec`: the notification hook, and the verbs it may never run.
 *
 * Ports `V2_MONITOR_FORBIDDEN_VERBS` / `V2_MONITOR_MAX_EXIT_CODE`
 * (client.py:10262-10268), `_monitor_exec_refusal` (10381-10390) and
 * `_run_monitor_hook` (10393-10438).
 *
 * `--exec` reaches a shell.  The forbidden verbs are this workspace's own
 * mutating ones, and a hook that invokes one is an autoplay bot in a single
 * flag, reachable by accident.  Refusing them is deliberately imperfect — a
 * wrapper script gets past it — but it converts a silent contract violation
 * into a deliberate bypass, and for a benchmark that difference is the whole
 * point.
 *
 * A hook's failure is reported and never fatal: a monitor that died because a
 * hook exited non-zero would lose the turn it was watching for, which is
 * precisely the failure this exists to prevent.  Every invocation is logged
 * with its command string — the log is what makes a violation auditable after
 * the fact.
 */
import { attemptOr } from 'src/errors';
import { constants as osConstants } from 'node:os';
import { Console, Effect } from 'effect';
import { formatG } from 'src/render/primitives';
import { holderSeat } from 'src/render/phase';
import { isJsonObject, type JsonValue } from 'src/schema/primitives';
import type { WaitEnvelope } from 'src/schema/wait';
import { mirrorMonitorLog } from 'src/services/mirror';
import type { PrivateFs } from 'src/services/private-fs';
import { phaseIsMine } from 'src/services/wait';

/** client.py:10267 — this workspace's own mutating verbs. */
export const V2_MONITOR_FORBIDDEN_VERBS: ReadonlyArray<string> = [
  'do',
  'batch',
  'turn',
  'start',
  'retry',
];

/** client.py:10268 — the widest status a POSIX shell can report. */
export const V2_MONITOR_MAX_EXIT_CODE = 255;

const escapeVerb = (verb: string): string => verb.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');

/** CPython's `x or ""`: every falsy JSON value collapses to the empty string. */
const orEmpty = (value: JsonValue | undefined): string => {
  if (value === null || value === undefined || value === false || value === '' || value === 0) {
    return '';
  }
  if (Array.isArray(value) && value.length === 0) return '';
  if (isJsonObject(value) && Object.keys(value).length === 0) return '';
  return typeof value === 'string' ? value : String(value);
};

/**
 * `_monitor_exec_refusal` — name the mutating verb a hook must not invoke, or
 * return the empty string.
 *
 * The word has to stand alone: `just do "…"` is refused, `echo done >> log` is
 * not, because `done` is not the token `do`.
 */
export const monitorExecRefusal = (command: string): string => {
  for (const verb of V2_MONITOR_FORBIDDEN_VERBS) {
    if (new RegExp(`(?:^|[;&|(\\s])${escapeVerb(verb)}(?:\\s|$)`).test(command)) return verb;
  }
  return '';
};

/** The refusal sentence `command_monitor` raises for a hook that plays. */
export const monitorExecRefusalMessage = (verb: string): string =>
  `just monitor --exec must not run \`${verb}\`: the monitor watches and ` +
  'notifies, it never plays. You choose every action yourself; a hook that ' +
  'issues game commands is an automated bot and a contract violation. Notify ' +
  'your harness instead and let it decide.';

// ---------------------------------------------------------------------------
// The environment one wake exports
// ---------------------------------------------------------------------------

/** The six variables a hook is promised, in the Python's own order. */
export const MONITOR_HOOK_VARIABLES: ReadonlyArray<string> = [
  'FREECIV_GAME_ID',
  'FREECIV_TURN',
  'FREECIV_PHASE',
  'FREECIV_YOUR_TURN',
  'FREECIV_DEADLINE_S',
  'FREECIV_HOLDER_LABEL',
];

/**
 * Build the hook's environment from one wake.
 *
 * Every value is a string, and an absent fact is the empty string rather than
 * an unset variable: a shell hook that tests `[ -n "$FREECIV_TURN" ]` reads the
 * same thing whether the supervisor omitted the phase or the turn was `null`.
 */
export const monitorHookEnvironment = (
  wait: WaitEnvelope,
  base: Readonly<Record<string, string | undefined>> = process.env
): Record<string, string> => {
  const health = wait.health;
  const phase = health.phase;
  const holder = holderSeat(phase);
  const remaining = phase === null ? null : phase.timing.remaining_s;
  const environment: Record<string, string> = {};
  for (const [name, value] of Object.entries(base)) {
    if (value !== undefined) environment[name] = value;
  }
  environment['FREECIV_GAME_ID'] = String(health.game_id);
  environment['FREECIV_TURN'] = phase === null || phase.turn === null ? '' : String(phase.turn);
  environment['FREECIV_PHASE'] = phase === null || phase.phase === null ? '' : String(phase.phase);
  environment['FREECIV_YOUR_TURN'] = phaseIsMine(health) ? '1' : '0';
  environment['FREECIV_DEADLINE_S'] = remaining === null ? '' : formatG(remaining);
  environment['FREECIV_HOLDER_LABEL'] = holder === null ? '' : orEmpty(holder.controller_label);
  return environment;
};

// ---------------------------------------------------------------------------
// Running it
// ---------------------------------------------------------------------------

/**
 * What one hook invocation did. `unstarted` is CPython's `OSError` arm.
 *
 * `status` is `subprocess.CompletedProcess.returncode`, so it is negative when
 * the hook died by a signal — see {@link completedProcessStatus}.
 */
export type HookOutcome =
  | { readonly _tag: 'exited'; readonly status: number }
  | { readonly _tag: 'unstarted'; readonly message: string };

/** This platform's signal numbers, by name: `SIGTERM` → 15, `SIGSEGV` → 11. */
const SIGNAL_NUMBERS: ReadonlyMap<string, number> = new Map(
  Object.entries(osConstants.signals)
);

/**
 * A signalled hook with a name we cannot number. Unreachable in practice —
 * every signal that can kill a child is in `os.constants.signals` — but the
 * log records a non-zero status rather than a hole.
 */
const UNKNOWN_SIGNAL_STATUS = -1;

/**
 * CPython's `CompletedProcess.returncode` from Bun's `SyncSubprocess`.
 *
 * A normal exit reports its status; a death by signal reports the *negated*
 * signal number, which is the number `monitor: --exec exited …` and the
 * `state/monitor.log` line must both carry. Bun's own `.d.ts` declares
 * `exitCode: number`, but the runtime returns `null` for a signalled child
 * (`--exec 'kill -TERM $$'` yields `exitCode === null, signalCode ===
 * 'SIGTERM'`), so the read is widened here and the null arm is explicit —
 * `tsc` cannot catch this one for us.
 */
export const completedProcessStatus = (
  exitCode: number | null,
  signalCode: string | null | undefined
): number => {
  if (exitCode !== null) return exitCode;
  const number = signalCode === null || signalCode === undefined
    ? undefined
    : SIGNAL_NUMBERS.get(signalCode);
  return number === undefined ? UNKNOWN_SIGNAL_STATUS : -number;
};

export type HookRunner = (
  command: string,
  environment: Record<string, string>
) => Effect.Effect<HookOutcome>;

/**
 * `subprocess.run(command, shell=True, env=…)` — the hook inherits this
 * process's stdio, so a hook that prints goes where the monitor's output goes.
 */
export const shellHookRunner: HookRunner = (command, environment) =>
  Effect.sync(() =>
    attemptOr(
      () => {
        const result = Bun.spawnSync({
          cmd: ['/bin/sh', '-c', command],
          env: environment,
          stdin: 'inherit',
          stdout: 'inherit',
          stderr: 'inherit',
        });
        const exitCode: number | null = result.exitCode;
        return {
          _tag: 'exited',
          status: completedProcessStatus(exitCode, result.signalCode),
        } as const;
      },
      (cause) =>
        ({
          _tag: 'unstarted',
          message: cause instanceof Error ? cause.message : String(cause),
        }) as const
    )
  );

/**
 * `_run_monitor_hook` — run `--exec` for one wake; its failure is reported,
 * never fatal.
 */
export const runMonitorHook = (
  sessionPath: string,
  command: string,
  wait: WaitEnvelope,
  runner: HookRunner = shellHookRunner,
  base?: Readonly<Record<string, string | undefined>>
): Effect.Effect<void, never, PrivateFs> =>
  Effect.gen(function* () {
    const environment = monitorHookEnvironment(wait, base ?? process.env);
    yield* mirrorMonitorLog(sessionPath, `exec ${command}`);
    const outcome = yield* runner(command, environment);
    if (outcome._tag === 'unstarted') {
      yield* Console.error(`monitor: --exec did not run: ${outcome.message}`);
      yield* mirrorMonitorLog(sessionPath, `exec failed to start: ${outcome.message}`);
      return;
    }
    if (outcome.status !== 0) {
      yield* Console.error(`monitor: --exec exited ${outcome.status}`);
      yield* mirrorMonitorLog(sessionPath, `exec exited ${outcome.status}`);
    }
  });
