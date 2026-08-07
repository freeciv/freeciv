/**
 * `play show` — read this seat's mirror files. This command never opens a socket.
 *
 * Ports `command_show` (client.py:11438-11495).
 *
 * The whole value of `show` is what it does **not** do: no request, no wire
 * budget, no widening of what the seat knows.  Everything it prints was written
 * by a command that already ingested a page, which is why the staleness banner
 * exists at all — a projection is a record of a past revision, not a claim about
 * now, and the banner says so in the one place the agent is looking.
 *
 * Flag precedence is CPython's, and it is load-bearing: "name and --grep" is
 * rejected before "--regex without --grep", which is rejected before `--yields`
 * is even considered.  `just show --grep X --yields` therefore earns the
 * `--yields` refusal, not the grep one.
 */
import { Args, Command, Options } from '@effect/cli';
import { Effect, Option } from 'effect';
import type { LockTimeoutError, PlayerError, SessionMissingError } from 'src/errors';
import { playerError } from 'src/errors';
import { render } from 'src/render/primitives';
import { renderMapYields } from 'src/render/mirror/yields-overlay';
import {
  showDefault,
  showGrep,
  showNamed,
  showSources,
  showStaleness,
} from 'src/render/show';
import { jsonRequested, printV2Json } from 'src/services/json-output';
import { mirrorDir, strip } from 'src/services/mirror';
import type { PrivateFs } from 'src/services/private-fs';
import { SessionStore } from 'src/services/session-store';

export interface ShowOptions {
  readonly session: string;
  readonly name: string;
  readonly grep: string;
  readonly regex: boolean;
  readonly yields: boolean;
  readonly json: boolean;
  /** Monotonic seconds, injected so the `--regex` budget refusal is testable. */
  readonly clock?: () => number;
}

const BOTH = playerError('just show takes either a name or --grep PATTERN, not both');

const REGEX_WITHOUT_GREP = playerError('just show --regex needs a --grep PATTERN to apply to');

const YIELDS_ELSEWHERE = playerError(
  '--yields overlays the terrain grid; run `just show map --yields`'
);

const NO_MAP = playerError(
  'this seat has no map projection yet; run `just turn` or ' +
    '`just state --section map_tiles` to write one'
);

/** The `--json` envelope, whose `selection` is `null` for the bare listing. */
const showJson = (selection: string, lines: ReadonlyArray<string>): unknown => ({
  schema_version: 1,
  command: 'show',
  selection: selection === '' ? null : selection,
  lines,
});

const emit = (
  json: boolean,
  selection: string,
  lines: ReadonlyArray<string>
): Effect.Effect<void> =>
  jsonRequested('show', json) ? printV2Json(showJson(selection, lines)) : render(lines);

export const runShow = (
  options: ShowOptions
): Effect.Effect<
  void,
  PlayerError | SessionMissingError | LockTimeoutError,
  SessionStore | PrivateFs
> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const { path, session } = yield* store.resolveV2(options.session);
    // U04's `strip` is CPython's `str.strip()`, and it is not
    // `String.prototype.trim()`: `str.strip()` removes U+0085 and
    // U+001C-U+001F, which `trim` keeps, and keeps U+FEFF, which `trim`
    // removes.  Both directions reach stdout — a name led by U+0085 prints
    // `state/units.tsv` and one led by U+FEFF is refused by `SHOW_NAME_RE`.
    const pattern = strip(options.grep);
    const name = strip(options.name);
    if (pattern !== '' && name !== '') return yield* Effect.fail(BOTH);
    if (options.regex && pattern === '') return yield* Effect.fail(REGEX_WITHOUT_GREP);
    if (options.yields) {
      if (name !== 'map' || pattern !== '') return yield* Effect.fail(YIELDS_ELSEWHERE);
      const overlay = yield* Effect.flatMap(mirrorDir(path), renderMapYields);
      if (overlay.length === 0) return yield* Effect.fail(NO_MAP);
      return yield* emit(options.json, 'map --yields', overlay);
    }
    const selection = pattern !== '' ? `grep ${pattern}` : name;
    const lines =
      pattern !== ''
        ? yield* showGrep(path, pattern, {
            regex: options.regex,
            ...(options.clock === undefined ? {} : { clock: options.clock }),
          })
        : name !== ''
          ? yield* showNamed(path, name)
          : yield* showDefault(path);
    // The banner is computed at read time and never written back: rewriting a
    // projection to say it is old would change the very stamp being judged.
    const stale = yield* showStaleness(path, session, yield* showSources(path, name, pattern));
    return yield* emit(options.json, selection, stale === '' ? lines : [stale, ...lines]);
  });

const sessionOption = Options.text('session').pipe(
  Options.withDefault(''),
  Options.withDescription('the private session file this command acts against')
);

export const showCommand = Command.make(
  'show',
  {
    session: sessionOption,
    name: Args.text({ name: 'name' }).pipe(Args.optional),
    grep: Options.text('grep').pipe(
      Options.withDefault(''),
      Options.withDescription('print every mirror line that matches this pattern')
    ),
    regex: Options.boolean('regex').pipe(
      Options.withDescription('read --grep as a regular expression instead of literal text')
    ),
    yields: Options.boolean('yields').pipe(
      Options.withDescription('with `map`: overlay per-tile food/shields/trade on the grid')
    ),
    json: Options.boolean('json').pipe(
      Options.withDescription('print the full-fidelity JSON payload instead of text')
    ),
  },
  ({ session, name, grep, regex, yields, json }) =>
    runShow({
      session,
      name: Option.getOrElse(name, () => ''),
      grep,
      regex,
      yields,
      json,
    })
);
