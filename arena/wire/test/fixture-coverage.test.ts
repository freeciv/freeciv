/**
 * Two claims the corpus suite was making without measuring.
 *
 * ## 1. "The corpus covers every scenario family the port has to decode"
 *
 * `test/fixtures-corpus.test.ts` asserts that, and lists `runs/sidecar/` among
 * the families — but it only checks that `index.json` contains an *entry* whose
 * path starts with that prefix.  Three of the fixtures, including that whole
 * family, were never read by any test at all: nothing decoded them, so nothing
 * knew whether the port could.  {@link DECODERS} below names a decoder for
 * every fixture, and {@link NO_SCHEMA_YET} is the explicit, justified list of
 * the ones that still have none — so the gap is tracked rather than silent, and
 * a new fixture cannot be added without landing in one column or the other.
 *
 * ## 2. Strictness was audited for `runs/` and nowhere else
 *
 * `test/gateway/manifest.test.ts` decodes the run-dir corpus a second time with
 * `onExcessProperty: 'error'` and explains exactly why: it proves "the schema
 * actually names every field the corpus carries, so nothing is passing only
 * because excess properties are tolerated".  That reasoning applies verbatim to
 * the live captures, which had no such audit — and the audit, once run, found
 * that `WatchResponse.game` was a two-field stub while the real block carried
 * 31.  That stub is fixed (it is `GameStatus` now); this test is what keeps the
 * next accidental omission from being indistinguishable from a deliberate one.
 */
import { describe, expect, test } from 'bun:test';
import { readdirSync, readFileSync } from 'node:fs';
import { join, relative } from 'node:path';
import { Either, Schema } from 'effect';
import {
  decodeBoardResponse,
  decodeGameEventsResponse,
  decodeGamesIndexResponse,
  decodeGameStatus,
  decodeGatewayIdentity,
  decodeGatewayProblem,
  decodeManifest,
  decodeReplayCatalog,
  decodeReplayResponse,
  decodeReport,
  decodeWatchResponse,
} from 'src/gateway/index';
import {
  BoardResponse,
  GameEventsResponse,
  GamesIndexResponse,
  GameStatus,
  GatewayIdentity,
  GatewayProblem,
  ReplayResponse,
  WatchResponse,
} from 'src/gateway/index';

const FIXTURES = join(import.meta.dir, 'fixtures');

const walk = (dir: string): ReadonlyArray<string> =>
  readdirSync(dir, { withFileTypes: true }).flatMap((entry) => {
    const full = join(dir, entry.name);
    return entry.isDirectory() ? walk(full) : [relative(FIXTURES, full)];
  });

const parse = (path: string): unknown => JSON.parse(readFileSync(join(FIXTURES, path), 'utf8'));

type Decode = (input: unknown) => Either.Either<unknown, { readonly message: string }>;

/**
 * Every fixture that has a schema, and the decoder that owns it.
 *
 * Keyed by path prefix or exact path; the longest match wins, so a specific
 * file can override its directory.
 */
const DECODERS: ReadonlyArray<readonly [prefix: string, decode: Decode]> = [
  ['runs/manifest/', decodeManifest],
  ['runs/report/', decodeReport],
  ['runs/replay-catalog/', decodeReplayCatalog],
  ['live/gateway-health.json', decodeGatewayIdentity],
  ['live/gateway-games-index.json', decodeGamesIndexResponse],
  ['live/supervisor-games-index.json', decodeGamesIndexResponse],
  ['live/gateway-board-turn1.json', decodeBoardResponse],
  ['live/gateway-board-400.json', decodeGatewayProblem],
  ['live/gateway-events.json', decodeGameEventsResponse],
  ['live/gateway-events-400.json', decodeGatewayProblem],
  ['live/gateway-replay-404.json', decodeGatewayProblem],
  ['live/gateway-replay-running-limit5.json', decodeReplayResponse],
  ['live/gateway-replay-terminal-limit5.json', decodeReplayResponse],
  ['live/gateway-watch-running.json', decodeWatchResponse],
  ['live/gateway-watch-terminal.json', decodeWatchResponse],
  ['live/supervisor-watch.json', decodeWatchResponse],
  ['live/supervisor-status-404.json', decodeGatewayProblem],
  ['live/supervisor-status-running.json', decodeGameStatus],
  ['live/supervisor-status-terminal.json', decodeGameStatus],
];

/**
 * Fixtures whose coverage is a **rejection**: the port must refuse them, and
 * that refusal is the thing worth pinning.
 */
const REJECTED_BY: ReadonlyArray<readonly [path: string, decode: Decode, why: string]> = [
  [
    'live/supervisor-health.json',
    decodeGatewayIdentity,
    "the supervisor's own /health carries no `kind`, so it must not decode as the gateway's",
  ],
];

/**
 * Fixtures this package has no schema for yet, and why.
 *
 * Being on this list is a *tracked* gap: the fixture is committed so the port
 * has the bytes when the schema is written, and this file names the reason so
 * nobody mistakes the absence for coverage.  Removing an entry is what landing
 * the schema looks like.
 */
const NO_SCHEMA_YET: Readonly<Record<string, string>> = {
  'runs/sidecar/exit-diagnostic.json':
    'sidecar exit forensics: written by the supervisor, not served by any gateway route, ' +
    'and no module models it yet. The bytes are here for when one does.',
  'runs/sidecar/exit-history.json':
    'the array-of-diagnostics form of the same document; same reason.',
  'live/supervisor-phase-events.json':
    'the full-control-v2 phase-event stream behind GameStatus.phase_events_url. ' +
    'GameStatus deliberately leaves `phase` an opaque object (`games.ts:763-765`) ' +
    'because the v2 phase block belongs to its own module, which is not written yet.',
};

/** Files that describe the corpus rather than belonging to it. */
const METADATA: ReadonlySet<string> = new Set(['README.md', 'index.json']);

const decoderFor = (path: string): Decode | undefined =>
  DECODERS.filter(([prefix]) => path.startsWith(prefix)).toSorted(
    ([left], [right]) => right.length - left.length,
  )[0]?.[1];

const corpus = walk(FIXTURES).filter((path) => !METADATA.has(path));

describe('fixture coverage / every fixture is decoded or explicitly waived', () => {
  test('the walk found the corpus', () => {
    expect(corpus.length).toBeGreaterThan(40);
  });

  test('no fixture is silently uncovered', () => {
    const rejected = new Set(REJECTED_BY.map(([path]) => path));
    const orphans = corpus
      .filter((path) => !path.startsWith('invalid/'))
      .filter((path) => decoderFor(path) === undefined)
      .filter((path) => !rejected.has(path))
      .filter((path) => !Object.hasOwn(NO_SCHEMA_YET, path));
    expect(orphans).toEqual([]);
  });

  test.each(REJECTED_BY.map((row) => [row[0], row] as const))(
    '%s is refused, which is what covering it means',
    (path, [, decode]) => {
      expect(Either.isLeft(decode(parse(path)))).toBe(true);
    },
  );

  test('the waiver list names only fixtures that really exist and really lack a decoder', () => {
    const onDisk = new Set(corpus);
    const stale = Object.keys(NO_SCHEMA_YET).filter(
      (path) => !onDisk.has(path) || decoderFor(path) !== undefined,
    );
    expect(stale).toEqual([]);
  });

  test('every fixture with a decoder actually decodes', () => {
    const failures = corpus
      .filter((path) => !path.startsWith('invalid/'))
      .flatMap((path) => {
        const decode = decoderFor(path);
        if (decode === undefined) return [];
        const result = decode(parse(path));
        return Either.isLeft(result) ? [`${path}: ${result.left.message}`] : [];
      });
    expect(failures).toEqual([]);
  });

  test('the waived fixtures are at least well-formed JSON, so the bytes stay usable', () => {
    const broken = Object.keys(NO_SCHEMA_YET).filter(
      (path) => !Either.isRight(Either.try(() => parse(path))),
    );
    expect(broken).toEqual([]);
  });
});

// ---------------------------------------------------------------------------
// The strictness audit the live family never had
// ---------------------------------------------------------------------------

/**
 * The same schema with `onExcessProperty: 'error'`.  Production never decodes
 * this way — it is the inverse of what the package does, and its only job is to
 * prove the schema *names* what the corpus carries rather than tolerating it.
 */
const strictly = <A, I>(schema: Schema.Schema<A, I>): Decode =>
  Schema.decodeUnknownEither(schema, {
    onExcessProperty: 'error',
    errors: 'all',
    propertyOrder: 'original',
  });

const STRICT: ReadonlyArray<readonly [path: string, decode: Decode]> = [
  ['live/gateway-health.json', strictly(GatewayIdentity)],
  ['live/gateway-games-index.json', strictly(GamesIndexResponse)],
  ['live/supervisor-games-index.json', strictly(GamesIndexResponse)],
  ['live/gateway-board-turn1.json', strictly(BoardResponse)],
  ['live/gateway-board-400.json', strictly(GatewayProblem)],
  ['live/gateway-events.json', strictly(GameEventsResponse)],
  ['live/gateway-events-400.json', strictly(GatewayProblem)],
  ['live/gateway-replay-404.json', strictly(GatewayProblem)],
  ['live/gateway-replay-running-limit5.json', strictly(ReplayResponse)],
  ['live/gateway-replay-terminal-limit5.json', strictly(ReplayResponse)],
  ['live/gateway-watch-running.json', strictly(WatchResponse)],
  ['live/gateway-watch-terminal.json', strictly(WatchResponse)],
  ['live/supervisor-watch.json', strictly(WatchResponse)],
  ['live/supervisor-status-404.json', strictly(GatewayProblem)],
  ['live/supervisor-status-running.json', strictly(GameStatus)],
  ['live/supervisor-status-terminal.json', strictly(GameStatus)],
];

describe('fixture coverage / the live schemas name every field the captures carry', () => {
  test('the audit covers every live fixture that has a decoder', () => {
    const audited = new Set(STRICT.map(([path]) => path));
    const live = corpus
      .filter((path) => path.startsWith('live/'))
      .filter((path) => decoderFor(path) !== undefined);
    expect(live.filter((path) => !audited.has(path))).toEqual([]);
  });

  test.each(STRICT.map(([path, decode]) => [path, decode] as const))(
    '%s carries no field its schema does not name',
    (path, decode) => {
      const result = decode(parse(path));
      const message = Either.isLeft(result) ? result.left.message : '';
      expect(message).toBe('');
    },
  );
});
