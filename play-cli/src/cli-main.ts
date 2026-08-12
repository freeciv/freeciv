/**
 * The root command, the Layer stack, and the single error → exit-code site.
 *
 * Ports `parser()` (client.py:11594-11884) and `main()` (11887-11905).
 *
 * Every subcommand below is a *stub* owned by cli-main only until its unit
 * lands: the flag surface is real (it is the `--help` contract and the
 * dual-spelling contract), the handler is not.  A worker replaces one by
 * exporting a `Command` from its own `src/commands/<name>.cmd.ts` and swapping
 * the single entry in {@link subcommands}; nothing else moves.
 */
import process from 'node:process';
import { Command, ValidationError } from '@effect/cli';
import { BunContext, BunRuntime } from '@effect/platform-bun';
import { Cause, Console, Effect, Exit, Layer } from 'effect';
import {
  AliasStaleError,
  DriftError,
  LockTimeoutError,
  PlayerError,
  SessionMissingError,
  V2ResponseError,
  playerError,
} from 'src/errors';
import {
  EXIT_OK,
  EXIT_REFUSED,
  ExitCodeSignal,
  passThroughExit,
  type ExitCode,
} from 'src/exit';
import { type CommandName } from 'src/constants';
import { HttpLive } from 'src/services/http';
import { V2ClientLive } from 'src/services/v2-client';
import { PrivateFsLive, WorkspaceLive } from 'src/services/private-fs';
import { SessionStoreLive } from 'src/services/session-store';
import { jsonRequested, printV2Json } from 'src/services/json-output';
import { RefusalRender } from 'src/render/refusal-seam';
import { RefusalRenderLive } from 'src/render/refusal';
import { V2StateSchemaLive } from 'src/services/aliases';
import { render } from 'src/render/primitives';

// --- the real subcommands, one per unit ------------------------------------
import { promptCommand } from 'src/commands/prompt.cmd';
import { helpCommand } from 'src/commands/help.cmd';
import { rulesCommand } from 'src/commands/rules.cmd';
import { nextCommand } from 'src/commands/next.cmd';
import { actCommand } from 'src/commands/act.cmd';
import { resultCommand } from 'src/commands/result.cmd';
import { joinCommand } from 'src/commands/join.cmd';
import { useCommand } from 'src/commands/use.cmd';
import { healthCommand } from 'src/commands/health.cmd';
import { turnCommand } from 'src/commands/turn.cmd';
import { startCommand } from 'src/commands/start.cmd';
import { doCommand } from 'src/commands/do.cmd';
import { showCommand } from 'src/commands/show.cmd';
import { stateCommand } from 'src/commands/state.cmd';
import { legalCommand } from 'src/commands/legal.cmd';
import { batchCommand } from 'src/commands/batch.cmd';
import { receiptCommand } from 'src/commands/receipt.cmd';
import { retryCommand } from 'src/commands/retry.cmd';
import { waitCommand } from 'src/commands/wait.cmd';
import { monitorCommand } from 'src/commands/monitor.cmd';

// ---------------------------------------------------------------------------
// The dual-spelling flag helper
// ---------------------------------------------------------------------------

/**
 * `--wait-s` and `--wait_s` are one flag with two spellings.
 *
 * PORT_MAP §4.8 promised these from here; NOTES §11.4 explains why the
 * *implementation* had to move to `src/options.ts` — a command module cannot
 * import from `cli-main`, which imports it.  One implementation, two import
 * paths: this file re-exports so §4.8's promise still holds.
 */
export {
  dualText,
  dualFloat,
  dualInteger,
  resolveDual,
  resolveDualOption,
  resolveDualRequired,
  type DualSpelling,
} from 'src/options';

// ---------------------------------------------------------------------------
// Ownership
// ---------------------------------------------------------------------------

/** Which unit owns the implementation of each command (PORT_MAP §0). */
export const COMMAND_OWNERS: ReadonlyMap<CommandName, string> = new Map<CommandName, string>([
  ['prompt', 'U01'],
  ['help', 'U01'],
  ['rules', 'U01'],
  ['next', 'U01'],
  ['act', 'U01'],
  ['result', 'U01'],
  ['join', 'U02'],
  ['use', 'U02'],
  ['health', 'U06'],
  ['turn', 'U12'],
  ['start', 'U18'],
  ['do', 'U16'],
  ['show', 'U09'],
  ['state', 'U10'],
  ['legal', 'U11'],
  ['batch', 'U13'],
  ['receipt', 'U14'],
  ['retry', 'U14'],
  ['wait', 'U05'],
  ['monitor', 'U17'],
]);

// ---------------------------------------------------------------------------
// The 20 subcommands (18 from argparse + the two doc surfaces)
//
// Every one is imported from its owning unit; nothing here is a stub.
// ---------------------------------------------------------------------------

/**
 * The registry, in the Python's `parser()` order.
 *
 * The name is carried alongside the command rather than read back off the
 * descriptor: `@effect/cli`'s descriptor is an internal structure, and the
 * registry is a contract worth stating in one readable place.
 */
export const SUBCOMMAND_REGISTRY = [
  ['prompt', promptCommand],
  ['join', joinCommand],
  ['use', useCommand],
  ['next', nextCommand],
  ['act', actCommand],
  ['health', healthCommand],
  ['turn', turnCommand],
  ['start', startCommand],
  ['do', doCommand],
  ['show', showCommand],
  ['state', stateCommand],
  ['legal', legalCommand],
  ['batch', batchCommand],
  ['receipt', receiptCommand],
  ['retry', retryCommand],
  ['wait', waitCommand],
  ['monitor', monitorCommand],
  ['result', resultCommand],
  ['help', helpCommand],
  ['rules', rulesCommand],
] as const satisfies ReadonlyArray<readonly [CommandName, unknown]>;

export const SUBCOMMAND_NAMES: ReadonlyArray<CommandName> = SUBCOMMAND_REGISTRY.map(
  ([name]) => name
);

export const subcommands = [
  promptCommand,
  joinCommand,
  useCommand,
  nextCommand,
  actCommand,
  healthCommand,
  turnCommand,
  startCommand,
  doCommand,
  showCommand,
  stateCommand,
  legalCommand,
  batchCommand,
  receiptCommand,
  retryCommand,
  waitCommand,
  monitorCommand,
  resultCommand,
  helpCommand,
  rulesCommand,
] as const;

export const rootCommand = Command.make('play', {}, () =>
  Effect.fail(playerError('a subcommand is required; run `play --help`'))
).pipe(Command.withSubcommands(subcommands));

// ---------------------------------------------------------------------------
// The Layer stack
// ---------------------------------------------------------------------------

const WorkspaceLayer = WorkspaceLive;
const PrivateFsLayer = Layer.provide(PrivateFsLive, WorkspaceLayer);
const V2ClientLayer = Layer.provide(V2ClientLive, HttpLive);
// The two inversion seams (PORT_MAP §4.6) are closed: U03 supplies the real
// `.v2-state` validators, U14 the real refusal renderer.  The `*Default`
// layers exist only so core could be built before either unit landed.
const SessionStoreLayer = Layer.provide(
  SessionStoreLive,
  Layer.mergeAll(WorkspaceLayer, PrivateFsLayer, V2StateSchemaLive)
);

export const AppLayer = Layer.mergeAll(
  BunContext.layer,
  HttpLive,
  V2ClientLayer,
  WorkspaceLayer,
  PrivateFsLayer,
  V2StateSchemaLive,
  SessionStoreLayer,
  RefusalRenderLive
);

// ---------------------------------------------------------------------------
// argv introspection, for the error site only
// ---------------------------------------------------------------------------

/** The subcommand name this invocation carries, `""` when there is none. */
export const commandFromArgv = (argv: ReadonlyArray<string>): string => {
  for (const token of argv.slice(2)) {
    if (!token.startsWith('-')) return token;
  }
  return '';
};

export const jsonFlagFromArgv = (argv: ReadonlyArray<string>): boolean =>
  argv.slice(2).includes('--json');

// ---------------------------------------------------------------------------
// THE error → exit-code site
// ---------------------------------------------------------------------------

export type MappedError =
  | PlayerError
  | V2ResponseError
  | DriftError
  | SessionMissingError
  | AliasStaleError
  | LockTimeoutError
  | ExitCodeSignal
  | ValidationError.ValidationError;

const stderr = (line: string): Effect.Effect<void> => Console.error(line);

/**
 * One mapping, one place.
 *
 * - `V2ResponseError` — a refusal is the most decision-relevant payload the
 *   agent ever reads, so it renders like every success path; `--json` keeps the
 *   byte-identical wire payload.  Then `error: …` on stderr, status 2.
 * - `ExitCodeSignal` — `wait`/`monitor` pass 0/75/66 straight through, printing
 *   nothing: an applied phase end never reports an error because of how the
 *   wait after it turned out.
 * - everything else — `error: {message}` on stderr, status 2.
 */
export const handleError = (
  error: MappedError,
  argv: ReadonlyArray<string> = process.argv
): Effect.Effect<ExitCode, never, RefusalRender> =>
  Effect.gen(function* () {
    if (error._tag === 'ExitCodeSignal') {
      return error.code;
    }
    if (error._tag === 'V2ResponseError') {
      const command = commandFromArgv(argv);
      if (jsonRequested(command, jsonFlagFromArgv(argv))) {
        yield* printV2Json(error.payload);
      } else {
        const renderer = yield* RefusalRender;
        yield* render(renderer.renderErrorPayload(error.payload));
      }
      yield* stderr(`error: ${error.message}`);
      return EXIT_REFUSED;
    }
    if (ValidationError.isValidationError(error)) {
      // @effect/cli has already printed the usage document; argparse exited 2
      // for the same class of failure and so does this.
      return EXIT_REFUSED;
    }
    yield* stderr(`error: ${error.message}`);
    return EXIT_REFUSED;
  });

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

export const runCli = (
  argv: ReadonlyArray<string> = process.argv
): Effect.Effect<ExitCode, never> =>
  Command.run(rootCommand, {
    name: 'play',
    version: '0.1.0',
  })(argv).pipe(
    Effect.map((): ExitCode => EXIT_OK),
    Effect.catchAll((error) => handleError(error, argv)),
    Effect.provide(AppLayer),
    Effect.catchAllCause((cause): Effect.Effect<ExitCode> =>
      Effect.map(stderr(`error: ${Cause.pretty(cause)}`), (): ExitCode => EXIT_REFUSED)
    )
  );

export const main = (argv: ReadonlyArray<string> = process.argv): Effect.Effect<void> =>
  Effect.flatMap(runCli(argv), (code) =>
    Effect.sync(() => {
      process.exitCode = passThroughExit(code);
    })
  );

/** `BunRuntime.runMain`'s teardown: never turn a mapped status into a 1. */
export const teardown = <E, A>(exit: Exit.Exit<E, A>, onExit: (code: number) => void): void => {
  const failed = Exit.isFailure(exit) && !Cause.isInterruptedOnly(exit.cause);
  onExit(failed ? EXIT_REFUSED : passThroughExit(Number(process.exitCode ?? EXIT_OK)));
};

export const runMain = (): void => {
  BunRuntime.runMain(main(), { disableErrorReporting: true, teardown });
};
