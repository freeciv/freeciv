/**
 * `play wait` — block until this seat is wanted again.
 *
 * Ports `command_wait` (client.py:10708-10723) and the argparse surface at
 * 11794-11822.
 *
 * The exit status carries the outcome, which is the whole of P1: every wake
 * used to exit 0, so a harness whose job supervisor escalates on non-zero —
 * the correct configuration, and the one a live opponent already had — was
 * never told its turn had started.
 *
 *   0   your phase is active
 *   75  another seat still holds it (call wait again)
 *   66  the game is over (stop looping)
 */
import { Command, Options } from '@effect/cli';
import { Console, Effect, Layer, Option } from 'effect';
import { exitWith } from 'src/exit';
import { dualFloat, resolveDual } from 'src/options';
import { V2_PROTOCOL_CARD } from 'src/render/join';
import { echo, render } from 'src/render/primitives';
import { holderSeat, renderWait, waitingTickLine } from 'src/render/wait';
import { aliasMap, rememberPage } from 'src/services/aliases';
import { jsonRequested } from 'src/services/json-output';
import { pyDumps } from 'src/services/canonical-body';
import { healthFloatPathsUnder, pyValueWithFloats } from 'src/services/health-json';
import { mirrorHealth } from 'src/services/mirror';
import { mirrorPage } from 'src/services/mirror/update-page';
import { PrivateFs } from 'src/services/private-fs';
import { SessionStore, type Session } from 'src/services/session-store';
import {
  V2_WAIT_S_MAX,
  waitArgs,
  waitCommandValue,
  waitCtx,
  waitExitCode,
  type WaitHooks,
} from 'src/services/wait';

// ---------------------------------------------------------------------------
// The cross-unit hooks
// ---------------------------------------------------------------------------

/**
 * How the wait engine reaches the units it does not own.
 *
 * The engine's hooks are `Effect`s with no requirements, so the services are
 * captured here — inside the command, where the CLI's Layer stack has already
 * supplied them — and provided back to each call.  That keeps `waitValue` and
 * `waitUntilTurn` usable from a test that has no filesystem.
 */
export type WaitHooksFor = (
  sessionPath: string,
  session: Session
) => Effect.Effect<WaitHooks, never, PrivateFs | SessionStore>;

/**
 * The real seams.
 *
 * `holderSeat` is U06's, `mirrorHealth` is U04's, `rememberPage` is U03's and
 * `mirrorPage` is U07's `_mirror_page` bridge.
 */
export const liveWaitHooks: WaitHooksFor = (sessionPath, session) =>
  Effect.gen(function* () {
    const files = yield* PrivateFs;
    const store = yield* SessionStore;
    const provided = Layer.merge(
      Layer.succeed(PrivateFs, files),
      Layer.succeed(SessionStore, store)
    );
    return {
      rememberPage: (page) =>
        Effect.provide(
          Effect.asVoid(rememberPage(sessionPath, session, { legal: false, page })),
          provided
        ),
      /**
       * `_mirror_page` (client.py:3016-3027), reached from `_legacy_wait_value`
       * (client.py:9976-9977) as `_mirror_page(path, cached, overview, "wait")`.
       *
       * CPython names the page's rows from the state dict its `_remember_page`
       * has already folded the overview back into (`_remember_page` ends with
       * `state.clear(); state.update(current)`), so the aliases are the ones
       * this seat learned from *this* page.  Reading the persisted state back
       * here is that same value: the ingestion above is written to disk before
       * the engine calls this hook.
       *
       * Leaving it inert cost the legacy `--until revision` fallback its mirror
       * refresh: the wake was identical, but the next `show`/`state` read a
       * `state/*.tsv` stamped at the *old* revision.
       */
      mirrorPage: (page, command) =>
        Effect.gen(function* () {
          const current = yield* store.readState(sessionPath, session);
          const aliases = yield* aliasMap(current);
          yield* Effect.provideService(
            mirrorPage(sessionPath, page, command, { aliases }),
            PrivateFs,
            files
          );
        }),
      // `_mirror_health` (client.py:3068-3072) passes `commands=V2_PROTOCOL_CARD`
      // unconditionally, so `state/header.txt` carries the same 20-line card the
      // join output printed.  Drop the option and the mirror falls back to
      // `_DEFAULT_COMMAND_CARD`, losing the ALIASES/ERRORS/ONE CALL PER
      // TURN/MULTIPLAYER/WHICH BINDING block — and since `wait --for-turn`
      // rewrites the header on every tick, a later `play show header` would
      // diverge from the Python byte-for-byte.
      mirrorHealth: (health, command) =>
        Effect.provide(
          mirrorHealth(sessionPath, health, command, { commands: V2_PROTOCOL_CARD }),
          provided
        ),
      holderSeat,
    };
  });

// ---------------------------------------------------------------------------
// The command
// ---------------------------------------------------------------------------

const sessionOption = Options.text('session').pipe(
  Options.withDefault(''),
  Options.withDescription('the private session file this command acts against')
);

const untilOption = Options.text('until').pipe(
  Options.withDefault('phase'),
  Options.withDescription('what the wait is waiting for: phase or revision')
);

const forTurnOption = Options.boolean('for-turn').pipe(
  Options.withDescription(
    'block until the phase is genuinely yours or the seat holding it runs out of deadline, ' +
      'instead of for a fixed --wait-s'
  )
);

const maxOption = Options.optional(
  Options.float('max').pipe(
    Options.withDescription(
      `with --for-turn: cap the whole wait at SECONDS (0..${V2_WAIT_S_MAX}); ` +
        "the default cap is the current holder's remaining deadline"
    )
  )
);

const jsonOption = Options.boolean('json').pipe(
  Options.withDescription('print the full-fidelity JSON payload instead of text')
);

/** Build the command against a given set of cross-unit hooks. */
export const waitCommandWith = (makeHooks: WaitHooksFor) =>
  Command.make(
    'wait',
    {
      session: sessionOption,
      waitS: dualFloat('wait-s'),
      pollS: dualFloat('poll-s'),
      until: untilOption,
      forTurn: forTurnOption,
      max: maxOption,
      json: jsonOption,
    },
    (options) =>
      Effect.gen(function* () {
        const waitS = yield* resolveDual('wait-s', options.waitS, 120);
        const pollS = yield* resolveDual('poll-s', options.pollS, 1);
        const store = yield* SessionStore;
        const loaded = yield* store.resolveV2(options.session);
        const json = jsonRequested('wait', options.json);
        const ctx = waitCtx({
          sessionPath: loaded.path,
          session: loaded.session,
          hooks: yield* makeHooks(loaded.path, loaded.session),
        });
        const value = yield* waitCommandValue(
          ctx,
          waitArgs({ waitS, pollS, until: options.until }),
          {
            forTurn: options.forTurn,
            maxS: Option.getOrNull(options.max),
            // The tick lines are prose, and `--json` is a byte-identical wire
            // payload: a JSON consumer gets the same single object it always
            // got.
            ...(json ? {} : { echo: (health) => echo(waitingTickLine(health)) }),
          }
        );
        if (json) {
          // NOT `printV2Json`: the envelope embeds the health block under
          // `"health"`, and core's encoder prints CPython's `600.0` as `600`
          // because `http.ts` already lost the lexeme to `JSON.parse`.
          // NOTES §10.6 — the twelve supervisor floats, re-rooted.
          yield* Console.log(
            pyDumps(pyValueWithFloats(value, healthFloatPathsUnder('health')), true)
          );
        } else {
          yield* render(renderWait(value));
        }
        // P1: the wake reason is now readable from the exit status, which is
        // the one channel every harness's job supervisor already watches.
        const code = waitExitCode(value);
        if (code !== 0) return yield* Effect.fail(exitWith(code));
        return;
      })
  );

export const waitCommand = waitCommandWith(liveWaitHooks);
