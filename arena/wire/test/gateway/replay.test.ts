/**
 * Decode parity for the three derived-telemetry payloads: `replay.json`,
 * `board.json` and `events.json`.
 *
 * The Python producer is the oracle.  Every captured fixture must decode, must
 * survive a decode/encode round trip byte-for-byte (including fields this
 * build has never heard of), and the subtleties the gateway dossier flags are
 * spot-asserted here rather than left as prose — a schema that quietly drops
 * `catalog: null`, flattens a `bigint` turn into a float, or loses a stack's
 * true unit count fails in this file.
 */
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { describe, expect, test } from 'bun:test';
import { Either, Option, Schema } from 'effect';
import {
  BOARD_ROUTE_PROBLEMS,
  decodeResearchState,
  decodeBoardQuery,
  decodeBoardResponse,
  decodeGameEventsResponse,
  decodeReplayQuery,
  decodeReplayResponse,
  decodeTechnologyCatalog,
  encodeBoardResponse,
  encodeGameEventsResponse,
  encodeReplayResponse,
  encodeTechnologyCatalog,
  EVENTS_ROUTE_PROBLEMS,
  formatBoardQuery,
  formatReplayQuery,
  GAME_EVENT_MAX_WEIGHT,
  nextReplayQuery,
  REPLAY_DEFAULT_LIMIT,
  REPLAY_ROUTE_PROBLEMS,
} from 'src/gateway/replay';
import { WireInt, WireNumber } from 'src/numeric';
import {
  decodeJsonObject,
  isJsonArray,
  isJsonObject,
  type JsonObject,
  type JsonValue,
  jsonField,
} from 'src/json';
import {
  formatIssuePath,
  type TolerantDecoder,
  type TolerantEncoder,
  type WireDecodeError,
  type WireEncodeError,
} from 'src/tolerant';

const FIXTURES_DIR = join(import.meta.dir, '..', 'fixtures');

const readJson = (relative: string): unknown => {
  const parsed: unknown = JSON.parse(readFileSync(join(FIXTURES_DIR, relative), 'utf8'));
  return parsed;
};

/** A fixture as a typed JSON object, so tests can rebuild it without a cast. */
const fixture = (relative: string): JsonObject =>
  Either.getOrThrowWith(
    decodeJsonObject(readJson(relative)),
    (error) => new Error(`${relative}: ${error.message}`),
  );

const REPLAY_RUNNING = 'live/gateway-replay-running-limit5.json';
const REPLAY_TERMINAL = 'live/gateway-replay-terminal-limit5.json';
const BOARD_TURN1 = 'live/gateway-board-turn1.json';
const EVENTS = 'live/gateway-events.json';
const CATALOG_MODERN = 'runs/replay-catalog/tech-tree-with-depth-and-requires.json';
const CATALOG_LEGACY = 'runs/replay-catalog/tech-tree-without-depth.json';

/**
 * Decode, re-encode, and render — the whole parity claim in one string, so a
 * failure prints the parse error instead of `false !== true`.
 */
const roundTripText = <A, I>(
  decode: TolerantDecoder<A>,
  encode: TolerantEncoder<A, I>,
  input: unknown,
): string =>
  Either.match(Either.flatMap(decode(input), encode), {
    onLeft: (error: WireDecodeError | WireEncodeError) =>
      `FAILED ${error.schemaName}: ${error.message}`,
    onRight: (value) => JSON.stringify(value),
  });

const decodeErrorOf = <A>(decode: TolerantDecoder<A>, input: unknown): WireDecodeError =>
  Either.match(decode(input), {
    onLeft: (error) => error,
    onRight: () => {
      throw new Error('expected a decode failure, but the payload decoded');
    },
  });

const decodedValue = <A>(decode: TolerantDecoder<A>, input: unknown): A =>
  Either.getOrThrowWith(decode(input), (error) => new Error(error.message));

const issuePaths = (error: WireDecodeError): ReadonlyArray<string> =>
  error.issues.map((issue) => `${formatIssuePath(issue.path)}:${issue.kind}`);

// ---------------------------------------------------------------------------
// Every captured fixture decodes and round-trips
// ---------------------------------------------------------------------------

describe('the captured payloads decode and survive a round trip', () => {
  test.each([REPLAY_RUNNING, REPLAY_TERMINAL])('replay.json — %s', (relative) => {
    const input = readJson(relative);
    expect(roundTripText(decodeReplayResponse, encodeReplayResponse, input)).toBe(
      JSON.stringify(input),
    );
  });

  test('board.json', () => {
    const input = readJson(BOARD_TURN1);
    expect(roundTripText(decodeBoardResponse, encodeBoardResponse, input)).toBe(
      JSON.stringify(input),
    );
  });

  test('events.json', () => {
    const input = readJson(EVENTS);
    expect(roundTripText(decodeGameEventsResponse, encodeGameEventsResponse, input)).toBe(
      JSON.stringify(input),
    );
  });

  test.each([CATALOG_MODERN, CATALOG_LEGACY])('replay-catalog.json — %s', (relative) => {
    const input = readJson(relative);
    expect(roundTripText(decodeTechnologyCatalog, encodeTechnologyCatalog, input)).toBe(
      JSON.stringify(input),
    );
  });
});

// ---------------------------------------------------------------------------
// Fields the server added after this build was written
// ---------------------------------------------------------------------------

/** The events payload with two fields no schema names: one top level, one nested. */
const eventsWithFutureFields: JsonObject = ((): JsonObject => {
  const base = fixture(EVENTS);
  const rows = jsonField(base, 'events');
  const events: ReadonlyArray<JsonValue> = isJsonArray(rows) ? rows : [];
  const first = events[0];
  const patched: ReadonlyArray<JsonValue> = isJsonObject(first)
    ? [{ ...first, future_row_field: 'kept' }, ...events.slice(1)]
    : events;
  return { ...base, events: patched, future_top_field: { nested: [1, null, true] } };
})();

describe('unknown server fields survive both directions', () => {
  test('a re-encoded payload still carries fields no schema names', () => {
    expect(
      roundTripText(decodeGameEventsResponse, encodeGameEventsResponse, eventsWithFutureFields),
    ).toBe(JSON.stringify(eventsWithFutureFields));
  });

  test('the decoded value carries them at runtime, even though the type cannot name them', () => {
    const decoded = decodedValue(decodeGameEventsResponse, eventsWithFutureFields);
    expect(Object.hasOwn(decoded, 'future_top_field')).toBe(true);
    expect(Object.hasOwn(decoded.events[0] ?? {}, 'future_row_field')).toBe(true);
  });

  test('a board payload keeps an unknown field too', () => {
    const board: JsonObject = { ...fixture(BOARD_TURN1), future_projection: 'gnomonic' };
    expect(roundTripText(decodeBoardResponse, encodeBoardResponse, board)).toBe(
      JSON.stringify(board),
    );
  });
});

// ---------------------------------------------------------------------------
// replay.json
// ---------------------------------------------------------------------------

describe('replay.json — the paging envelope', () => {
  const running = decodedValue(decodeReplayResponse, readJson(REPLAY_RUNNING));
  const terminal = decodedValue(decodeReplayResponse, readJson(REPLAY_TERMINAL));

  test('the cursor is an int, not a float', () => {
    expect(running.next_after_turn).toBe(5n);
    expect(typeof running.next_after_turn).toBe('bigint');
  });

  test('has_more is true even on a full page; complete tracks the run, not the page', () => {
    expect(running.snapshots).toHaveLength(5);
    expect(running.has_more).toBe(true);
    // The live game was still running when this was captured...
    expect(running.complete).toBe(false);
    // ...while the archived one is terminal and *still* has more pages.
    expect(terminal.complete).toBe(true);
    expect(terminal.has_more).toBe(true);
  });

  test('the next page is asked for by cursor, not by the last snapshot', () => {
    expect(nextReplayQuery(running)).toEqual(
      Option.some({ after_turn: 5, limit: REPLAY_DEFAULT_LIMIT }),
    );
    expect(nextReplayQuery({ ...running, has_more: false })).toEqual(Option.none());
  });

  test('the forwarded query string is the canonical one', () => {
    expect(formatReplayQuery({ after_turn: 5, limit: 100 })).toBe('after_turn=5&limit=100');
    expect(formatBoardQuery({ turn: 1 })).toBe('turn=1');
  });

  test('catalog is nullable on the wire, not merely optional', () => {
    const withoutCatalog: JsonObject = { ...fixture(REPLAY_RUNNING), catalog: null };
    const decoded = decodedValue(decodeReplayResponse, withoutCatalog);
    expect(decoded.catalog).toBeNull();
    expect(roundTripText(decodeReplayResponse, encodeReplayResponse, withoutCatalog)).toBe(
      JSON.stringify(withoutCatalog),
    );
  });

  test('the viewer’s legacy `warnings` alias decodes but is never emitted', () => {
    const aliased: JsonObject = {
      ...fixture(REPLAY_RUNNING),
      warnings: [{ turn: 3, message: 'legacy alias' }],
    };
    expect(decodedValue(decodeReplayResponse, aliased).warnings).toEqual([
      { turn: 3n, message: 'legacy alias' },
    ]);
    expect(running.warnings).toBeUndefined();
    expect(terminal.warnings).toBeUndefined();
  });

  test('a warning turn may be present-and-null as well as absent', () => {
    const warned: JsonObject = {
      ...fixture(REPLAY_RUNNING),
      replay_warnings: [{ turn: null, message: 'unpaired' }, { message: 'no turn at all' }],
    };
    const decoded = decodedValue(decodeReplayResponse, warned);
    expect(decoded.replay_warnings[0]?.turn).toBeNull();
    expect(decoded.replay_warnings[1]?.turn).toBeUndefined();
    // Absent and null are different bytes and must stay different bytes.
    expect(roundTripText(decodeReplayResponse, encodeReplayResponse, warned)).toBe(
      JSON.stringify(warned),
    );
  });
});

describe('replay.json — snapshots and players', () => {
  const running = decodedValue(decodeReplayResponse, readJson(REPLAY_RUNNING));
  const terminal = decodedValue(decodeReplayResponse, readJson(REPLAY_TERMINAL));
  const firstPlayer = running.snapshots[0]?.players[0];

  test('the in-game year is a negative integer', () => {
    expect(running.snapshots[0]?.turn).toBe(1n);
    expect(running.snapshots[0]?.year).toBe(-4000n);
  });

  test('the identity graft is present but nullable on every player', () => {
    expect(firstPlayer?.seat_id).toBe('place-1');
    expect(firstPlayer?.place).toBe(1n);
    expect(firstPlayer?.controller_label).toBe('pi-gpt-5.6-sol');
    expect(firstPlayer?.model).toBeNull();
    expect(firstPlayer?.scored).toBe(true);
  });

  test('population is a duplicate of citizens, not a second statistic', () => {
    const mismatched = running.snapshots.flatMap((snapshot) =>
      snapshot.players.filter((player) => player.population !== player.citizens),
    );
    expect(mismatched).toEqual([]);
  });

  test('research.tech_id is a nullable int and cost carries no information', () => {
    expect(firstPlayer?.research.tech_id).toBe(63n);
    expect(firstPlayer?.research.cost).toBe(0n);
    expect(decodedValue(decodeResearchState, { tech_id: null, name: '', bulbs: 0, cost: 0 })).toEqual(
      { tech_id: null, name: '', bulbs: 0n, cost: 0n },
    );
  });

  test('a cache written before known_tech_names/team_no existed still decodes', () => {
    // Both pages are schema_version 1; the live game's cache predates the two
    // keys, the archived game's does not.  Neither may be required.
    expect(running.snapshots[0]?.players[0]?.known_tech_names).toBeUndefined();
    expect(running.snapshots[0]?.players[0]?.team_no).toBeUndefined();
    expect(terminal.snapshots[0]?.players[0]?.known_tech_names).toEqual([]);
    expect(terminal.snapshots[0]?.players[0]?.team_no).toBe(0n);
  });

  test('every numeric player field decodes as a Python int', () => {
    const nonInteger = running.snapshots.flatMap((snapshot) =>
      snapshot.players.flatMap((player) =>
        [player.score, player.cities, player.gold, player.culture, player.units].filter(
          (value) => typeof value !== 'bigint',
        ),
      ),
    );
    expect(nonInteger).toEqual([]);
  });
});

describe('replay.json — the technology catalog', () => {
  test('requires/depth are optional under the same schema_version 1', () => {
    const modern = decodedValue(decodeTechnologyCatalog, readJson(CATALOG_MODERN));
    const legacy = decodedValue(decodeTechnologyCatalog, readJson(CATALOG_LEGACY));
    expect(modern.schema_version).toBe(1n);
    expect(legacy.schema_version).toBe(1n);
    expect(modern.technologies[0]?.requires).toEqual([64n, 43n]);
    expect(modern.technologies[0]?.depth).toBe(13n);
    expect(legacy.technologies[0]?.requires).toBeUndefined();
    expect(legacy.technologies[0]?.depth).toBeUndefined();
  });

  test('cost_base keeps its Python spelling: int stays int, float stays float', () => {
    const modern = decodedValue(decodeTechnologyCatalog, readJson(CATALOG_MODERN));
    const legacy = decodedValue(decodeTechnologyCatalog, readJson(CATALOG_LEGACY));
    // The current builder hardcodes the int 0 ...
    expect(modern.technologies[0]?.cost_base).toBe(0n);
    // ... while an older cached catalog carries a genuine float.
    expect(legacy.technologies[0]?.cost_base).toBe(4303.405628104328);
  });

  test('the embedded catalog and the on-disk one are the same document', () => {
    const embedded = decodedValue(decodeReplayResponse, readJson(REPLAY_RUNNING)).catalog;
    const onDisk = decodedValue(decodeTechnologyCatalog, readJson(CATALOG_MODERN));
    expect(embedded?.technologies).toHaveLength(onDisk.technologies.length);
    expect(embedded?.technologies[0]).toEqual(onDisk.technologies[0]);
  });
});

// ---------------------------------------------------------------------------
// board.json
// ---------------------------------------------------------------------------

describe('board.json', () => {
  const board = decodedValue(decodeBoardResponse, readJson(BOARD_TURN1));

  test('the three row encodings are all strings, and none of them is an array of numbers', () => {
    expect(board.terrain_rows).toHaveLength(Number(board.height));
    expect(board.terrain_rows[0]).toHaveLength(Number(board.width));
    // Altitudes are comma-joined decimal strings — the trap.
    expect(board.altitude_rows[0]?.split(',')).toHaveLength(Number(board.width));
    expect(board.altitude_rows[0]?.startsWith('30,22,10,')).toBe(true);
    // Owners are run-length encoded, "-" meaning unowned.
    expect(board.owner_rows[0]).toBe('-:54');
  });

  test('extra_layers packs four extras per layer, one hex nibble per tile', () => {
    expect(board.extra_layers).toHaveLength(Math.ceil(board.extras_catalog.length / 4));
    expect(board.extra_layers[0]).toHaveLength(Number(board.height));
    expect(board.extra_layers[0]?.[0]).toHaveLength(Number(board.width));
    expect(/^[0-9a-f]+$/.test(board.extra_layers[0]?.[0] ?? '')).toBe(true);
  });

  test('a stack’s count is the whole stack; types is only the top three', () => {
    const overflowing = board.unit_stacks.filter((stack) => stack.types.length > 3);
    expect(overflowing).toEqual([]);
    const understated = board.unit_stacks.filter(
      (stack) =>
        stack.count < stack.types.reduce((total, type) => total + type.count, 0n),
    );
    expect(understated).toEqual([]);
    expect(board.unit_stacks[0]?.count).toBe(5n);
  });

  test('players are grafted on with every key present and the identity ones nullable', () => {
    expect(board.players).toHaveLength(2);
    expect(board.players[0]?.seat_id).toBe('place-1');
    expect(board.players[0]?.model).toBeNull();
    expect(board.players[0]?.nation).toBe('Mayan');
    expect(board.players[0]?.scored).toBe(true);
  });

  test('topology and wrap are free-form strings that may be empty', () => {
    expect(board.topology).toBe('ISO|HEX');
    const blank: JsonObject = { ...fixture(BOARD_TURN1), topology: '', wrap: '' };
    expect(Either.isRight(decodeBoardResponse(blank))).toBe(true);
  });

  test('turn is strictly positive at the route, though the loader allows zero', () => {
    expect(Either.isRight(decodeBoardQuery({ turn: 1 }))).toBe(true);
    expect(Either.isLeft(decodeBoardQuery({ turn: 0 }))).toBe(true);
    expect(Either.isLeft(decodeBoardQuery({ turn: -1 }))).toBe(true);
    expect(Either.isLeft(decodeBoardQuery({ turn: 1.5 }))).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// events.json
// ---------------------------------------------------------------------------

describe('events.json', () => {
  const events = decodedValue(decodeGameEventsResponse, readJson(EVENTS));

  test('game_id is the requested id and schema_version is the gateway’s own', () => {
    expect(String(events.game_id)).toBe('game_QAoITB7qSmKNSwsXX6LaZG8H');
    expect(events.schema_version).toBe(1n);
  });

  test('min_included_weight describes the response, not the log', () => {
    const weights = events.events.map((event) => event.weight);
    expect(events.min_included_weight).toBe(weights.reduce((a, b) => (a < b ? a : b)));
    expect(events.min_included_weight).toBe(8n);
  });

  test('min_included_weight is 0 — not 1 — when no event survived', () => {
    const empty: JsonObject = {
      ...fixture(EVENTS),
      events: [],
      event_counts: {},
      min_included_weight: 0,
    };
    expect(decodedValue(decodeGameEventsResponse, empty).min_included_weight).toBe(0n);
  });

  test('total_events is allowed to disagree with events.length', () => {
    expect(events.total_events).toBe(19n);
    expect(events.events).toHaveLength(19);
    const truncatedPage: JsonObject = { ...fixture(EVENTS), total_events: 400, truncated: true };
    expect(decodedValue(decodeGameEventsResponse, truncatedPage).total_events).toBe(400n);
  });

  test('the count maps are open records of ints, keyed by nothing in particular', () => {
    expect(events.event_counts['city_founded']).toBe(15n);
    expect(events.omitted_counts).toEqual({});
    const total = Object.values(events.event_counts).reduce((a, b) => a + b, 0n);
    expect(total).toBe(19n);
  });

  test('event.data is an unbounded passthrough, preserved exactly', () => {
    expect(events.events[0]?.data).toEqual({
      cities: ['Cahokia'],
      faction: 'claude-code-claude-opus-5',
      first_city: true,
    });
  });

  test('an empty summary is legal; an empty kind is not', () => {
    // `_public_text(kind, "event", 40)` falls back to "event", so a kind can
    // never be blank — but `_public_text(summary, "", 240)` falls back to "",
    // which an all-control-character summary really does produce.
    const withRow = (row: JsonObject): JsonObject => ({ ...fixture(EVENTS), events: [row] });
    const base: JsonObject = { turn: 1, kind: 'k', summary: 's', weight: 5, actors: [], data: {} };
    expect(Either.isRight(decodeGameEventsResponse(withRow({ ...base, summary: '' })))).toBe(true);
    expect(issuePaths(decodeErrorOf(decodeGameEventsResponse, withRow({ ...base, kind: '' })))).toContain(
      'events.0.kind:Refinement',
    );
  });

  test('weight is bounded 1..100 — a row outside that never came from _public_event', () => {
    const withBadWeight: JsonObject = {
      ...fixture(EVENTS),
      events: [
        { turn: 1, kind: 'k', summary: 's', weight: GAME_EVENT_MAX_WEIGHT + 1, actors: [], data: {} },
      ],
    };
    expect(issuePaths(decodeErrorOf(decodeGameEventsResponse, withBadWeight))).toContain(
      'events.0.weight:Refinement',
    );
  });
});

// ---------------------------------------------------------------------------
// Integers, the way CPython spells them
// ---------------------------------------------------------------------------

describe('integers decode as bigint so canonical JSON stays byte-identical', () => {
  const decodeInt = Schema.decodeUnknownEither(WireInt);

  test.each<[string, unknown, boolean]>([
    ['zero', 0, true],
    ['a positive int', 77, true],
    ['a negative int', -4000, true],
    ['a fractional number', 1.5, false],
    ['a boolean', true, false],
    ['a numeric string', '5', false],
    ['null', null, false],
    ['beyond the safe-integer range', 1e30, false],
  ])('%s', (_label, input, accepted) => {
    expect(Either.isRight(decodeInt(input))).toBe(accepted);
  });

  test('a fractional turn is rejected where the producer only ever wrote an int', () => {
    const fractional: JsonObject = { ...fixture(BOARD_TURN1), turn: 1.5 };
    // `Transformation`, not `Type`: the number arrived fine and then failed on
    // the way to `bigint`, which is exactly where a Python `int` field breaks.
    expect(issuePaths(decodeErrorOf(decodeBoardResponse, fractional))).toContain(
      'turn:Transformation',
    );
  });

  test('WireNumber keeps a float a float and an int an int', () => {
    const decodeNumber = Schema.decodeUnknownEither(WireNumber);
    expect(decodeNumber(0)).toEqual(Either.right(0n));
    expect(decodeNumber(1.25)).toEqual(Either.right(1.25));
  });
});

// ---------------------------------------------------------------------------
// The regression this package exists to prevent
// ---------------------------------------------------------------------------

describe('optional-and-nullable is spelled optional(NullOr), never optionalWith({nullable})', () => {
  test('optionalWith({ nullable: true }) silently turns an explicit null into a missing key', () => {
    const Wrong = Schema.Struct({ turn: Schema.optionalWith(Schema.Number, { nullable: true }) });
    const encodeWrong = Schema.encodeEither(Wrong, { onExcessProperty: 'preserve' });
    const wrong = Either.flatMap(
      Schema.decodeUnknownEither(Wrong, { onExcessProperty: 'preserve' })({ turn: null }),
      encodeWrong,
    );
    expect(Either.map(wrong, (value) => JSON.stringify(value))).toEqual(Either.right('{}'));

    // What this module does instead: null in, null out.
    const Right = Schema.Struct({ turn: Schema.optional(Schema.NullOr(Schema.Number)) });
    const kept = Either.flatMap(
      Schema.decodeUnknownEither(Right, { onExcessProperty: 'preserve' })({ turn: null }),
      Schema.encodeEither(Right, { onExcessProperty: 'preserve' }),
    );
    expect(Either.map(kept, (value) => JSON.stringify(value))).toEqual(
      Either.right('{"turn":null}'),
    );
  });
});

// ---------------------------------------------------------------------------
// Negatives — the schema catches *that* defect, not an incidental difference
// ---------------------------------------------------------------------------

describe('the synthetic negatives fail on exactly the defect they carry', () => {
  test('a technology with no id', () => {
    const error = decodeErrorOf(
      decodeTechnologyCatalog,
      readJson('invalid/replay-catalog-tech-missing-id.json'),
    );
    expect(issuePaths(error)).toContain('technologies.0.id:Missing');
    expect(error.issues).toHaveLength(1);
  });

  test('requires holding technology names instead of ids', () => {
    const error = decodeErrorOf(
      decodeTechnologyCatalog,
      readJson('invalid/replay-catalog-requires-names-not-ids.json'),
    );
    expect(issuePaths(error)).toContain('technologies.0.requires.0:Type');
  });

  test('the valid catalogs prove those two are the only differences', () => {
    expect(Either.isRight(decodeTechnologyCatalog(readJson(CATALOG_MODERN)))).toBe(true);
    expect(Either.isRight(decodeTechnologyCatalog(readJson(CATALOG_LEGACY)))).toBe(true);
  });
});

describe('a problem body is never mistaken for a payload', () => {
  test.each<[string, string]>([
    ['live/gateway-replay-404.json', 'not found'],
    ['live/gateway-board-400.json', BOARD_ROUTE_PROBLEMS.queryShape],
    ['live/gateway-events-400.json', EVENTS_ROUTE_PROBLEMS.query],
  ])('%s carries only {error}', (relative, message) => {
    const body = fixture(relative);
    expect(Object.keys(body)).toEqual(['error']);
    expect(body['error']).toBe(message);
    expect(Either.isLeft(decodeReplayResponse(body))).toBe(true);
    expect(Either.isLeft(decodeBoardResponse(body))).toBe(true);
    expect(Either.isLeft(decodeGameEventsResponse(body))).toBe(true);
  });

  test('the captured replay 404 is the router’s message, not the handler’s', () => {
    // A game id that fails GAME_ID_RE is rejected before `_replay` runs, so the
    // body is the generic "not found" rather than either handler message.
    const body = fixture('live/gateway-replay-404.json');
    expect(body['error']).not.toBe(REPLAY_ROUTE_PROBLEMS.notFound);
    expect(REPLAY_ROUTE_PROBLEMS.notFound).toBe('game replay artifacts not found');
  });
});

describe('the replay query is validated the way _replay_query validates it', () => {
  test.each<[string, unknown, boolean]>([
    ['the defaults', { after_turn: 0, limit: 250 }, true],
    ['the smallest limit', { after_turn: 0, limit: 1 }, true],
    ['limit 0', { after_turn: 0, limit: 0 }, false],
    ['limit 251', { after_turn: 0, limit: 251 }, false],
    ['a negative after_turn', { after_turn: -1, limit: 10 }, false],
    ['a fractional limit', { after_turn: 0, limit: 10.5 }, false],
  ])('%s', (_label, input, accepted) => {
    expect(Either.isRight(decodeReplayQuery(input))).toBe(accepted);
  });
});
