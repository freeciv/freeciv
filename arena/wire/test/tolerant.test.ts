/**
 * The decode entry point's two promises: additive server fields never break a
 * consumer, and never get lost by one either.
 */
import { describe, expect, test } from 'bun:test';
import { Effect, Either, ParseResult, Schema } from 'effect';
import {
  decodeTolerant,
  encodeTolerant,
  formatIssuePath,
  isTolerant,
  schemaLabel,
  TOLERANT_PARSE_OPTIONS,
  WireDecodeError,
  WireEncodeError,
} from 'src/tolerant';

const Row = Schema.Struct({
  game_id: Schema.String,
  current_turn: Schema.Number,
}).annotations({ identifier: 'Row' });
type Row = typeof Row.Type;

const decodeRow = decodeTolerant(Row);
const encodeRow = encodeTolerant(Row);

/** Payload as the gateway of tomorrow might send it: two fields we know, three we don't. */
const FUTURE_ROW_TEXT =
  '{"game_id":"abc","tomorrow":{"nested":[1,null,{"deep":true}]},"current_turn":7,"zed":"trailing","nulled":null}';

const rightOrThrow = <A, E>(either: Either.Either<A, E>): A =>
  Either.getOrThrowWith(either, (error) => new Error(`expected Right, got ${String(error)}`));

const leftOrThrow = <A, E>(either: Either.Either<A, E>): E =>
  Either.isLeft(either)
    ? either.left
    : ((): never => {
        throw new Error(`expected Left, got ${JSON.stringify(either.right)}`);
      })();

describe('decodeTolerant: accepts what it does not know', () => {
  test('unknown server fields do not fail the decode', () => {
    const decoded = rightOrThrow(decodeRow(JSON.parse(FUTURE_ROW_TEXT)));
    expect(decoded.game_id).toBe('abc');
    expect(decoded.current_turn).toBe(7);
  });

  test('unknown server fields survive on the decoded value at runtime', () => {
    const decoded = rightOrThrow(decodeRow(JSON.parse(FUTURE_ROW_TEXT)));
    expect(Object.keys(decoded)).toEqual([
      'game_id',
      'tomorrow',
      'current_turn',
      'zed',
      'nulled',
    ]);
    expect(JSON.parse(JSON.stringify(decoded))).toEqual(JSON.parse(FUTURE_ROW_TEXT));
  });

  test('a nested struct preserves its own excess properties', () => {
    const Outer = Schema.Struct({ inner: Schema.Struct({ known: Schema.String }) });
    const decoded = rightOrThrow(
      decodeTolerant(Outer)({ inner: { known: 'k', extra: 1 }, top: 2 }),
    );
    expect(JSON.parse(JSON.stringify(decoded))).toEqual({
      inner: { known: 'k', extra: 1 },
      top: 2,
    });
  });
});

describe('round trip: what came in comes back out', () => {
  test('decode then encode is byte-identical, key order included', () => {
    const roundTripped = rightOrThrow(encodeRow(rightOrThrow(decodeRow(JSON.parse(FUTURE_ROW_TEXT)))));
    expect(JSON.stringify(roundTripped)).toBe(FUTURE_ROW_TEXT);
  });

  test('a modified field re-encodes without disturbing the unknown ones', () => {
    const decoded = rightOrThrow(decodeRow(JSON.parse(FUTURE_ROW_TEXT)));
    const bumped: Row = { ...decoded, current_turn: 8 };
    const encoded = rightOrThrow(encodeRow(bumped));
    expect(JSON.parse(JSON.stringify(encoded))).toEqual({
      ...JSON.parse(FUTURE_ROW_TEXT),
      current_turn: 8,
    });
  });

  test('a transform round-trips to the encoded representation, extras intact', () => {
    const Turned = Schema.Struct({ turn: Schema.NumberFromString });
    const decoded = rightOrThrow(decodeTolerant(Turned)({ turn: '42', ghost: 'boo' }));
    expect(decoded.turn).toBe(42);
    const encoded = rightOrThrow(encodeTolerant(Turned)(decoded));
    expect(JSON.parse(JSON.stringify(encoded))).toEqual({ turn: '42', ghost: 'boo' });
  });

  test('why encodeTolerant exists: a plain Schema.encodeEither drops the extras', () => {
    const decoded = rightOrThrow(decodeRow(JSON.parse(FUTURE_ROW_TEXT)));
    const naive = rightOrThrow(Schema.encodeEither(Row)(decoded));
    expect(Object.keys(naive)).toEqual(['game_id', 'current_turn']);
  });

  test('why decodeTolerant exists: a default decode drops them at the door', () => {
    const naive = rightOrThrow(Schema.decodeUnknownEither(Row)(JSON.parse(FUTURE_ROW_TEXT)));
    expect(Object.keys(naive)).toEqual(['game_id', 'current_turn']);
  });
});

describe('errors are values', () => {
  test('a bad payload yields a Left, never a throw', () => {
    const result = decodeRow({ game_id: 1 });
    expect(Either.isLeft(result)).toBe(true);
  });

  test('the failure is a tagged WireDecodeError naming the schema', () => {
    const error = leftOrThrow(decodeRow({ game_id: 1 }));
    expect(error).toBeInstanceOf(WireDecodeError);
    expect(error._tag).toBe('WireDecodeError');
    expect(error.schemaName).toBe('Row');
    expect(error.message).toContain('Expected string, actual 1');
  });

  test('errors: "all" reports every issue, not just the first', () => {
    const error = leftOrThrow(decodeRow({ game_id: 1 }));
    expect(error.issues.map((issue) => [formatIssuePath(issue.path), issue.kind])).toEqual([
      ['game_id', 'Type'],
      ['current_turn', 'Missing'],
    ]);
  });

  test('issue paths point into nested payloads', () => {
    const Nested = Schema.Struct({ games: Schema.Array(Row) });
    const error = leftOrThrow(decodeTolerant(Nested, 'Nested')({ games: [{ game_id: 'a' }] }));
    expect(error.issues.map((issue) => formatIssuePath(issue.path))).toEqual([
      'games.0.current_turn',
    ]);
  });

  test('non-object garbage fails cleanly', () => {
    expect(Either.isLeft(decodeRow(null))).toBe(true);
    expect(Either.isLeft(decodeRow('a string'))).toBe(true);
    expect(Either.isLeft(decodeRow(undefined))).toBe(true);
    expect(Either.isLeft(decodeRow([]))).toBe(true);
  });

  test('an encode failure is a distinct tag, so a proxy can tell whose fault it is', () => {
    const Refused = Schema.transformOrFail(Schema.String, Schema.String, {
      strict: true,
      decode: (text) => ParseResult.succeed(text),
      encode: (value, _options, ast) =>
        ParseResult.fail(new ParseResult.Type(ast, value, 'this value cannot be sent')),
    });
    const error = leftOrThrow(encodeTolerant(Refused, 'Refused')('nope'));
    expect(error).toBeInstanceOf(WireEncodeError);
    expect(error._tag).toBe('WireEncodeError');
    expect(error.schemaName).toBe('Refused');
    expect(error.message).toContain('this value cannot be sent');
  });
});

describe('composition', () => {
  test('a decoder is already an Effect: yield* it, no lift', () => {
    const program = Effect.gen(function* () {
      const row = yield* decodeRow({ game_id: 'g', current_turn: 3, extra: true });
      return row.current_turn;
    });
    expect(Effect.runSync(program)).toBe(3);
  });

  test('the failure channel carries the typed error', () => {
    const program = Effect.gen(function* () {
      const row = yield* decodeRow({});
      return row.game_id;
    });
    const failure = leftOrThrow(Effect.runSync(Effect.either(program)));
    expect(failure._tag).toBe('WireDecodeError');
  });
});

describe('helpers', () => {
  test('isTolerant agrees with the decoder when the schema does not transform', () => {
    // `Row` has `Type === Encoded`, so `Schema.is` and a decode ask the same
    // question and must answer it identically — including the tolerance to an
    // excess property, which is the shared option doing the work.
    const isRow = isTolerant(Row);
    const cases: ReadonlyArray<unknown> = [
      { game_id: 'g', current_turn: 1 },
      { game_id: 'g', current_turn: 1, extra: 'ok' },
      { game_id: 'g' },
      null,
    ];
    expect(cases.map(isRow)).toEqual(cases.map((input) => Either.isRight(decodeRow(input))));
  });

  test('isTolerant and the decoder deliberately disagree when the schema transforms', () => {
    // This is the half the old test could not see. `Schema.is` tests the
    // **Type** side; a decode consumes the **Encoded** side. For a schema with
    // a transformation the two are different types, so they answer different
    // questions — and the package's own `Gateway` bodies are exactly this
    // shape, since a `WireInt` is `number` on the wire and `bigint` decoded.
    //
    // Two thin wrappers over one AST cannot show this; that is why the old
    // assertion held while the distinction went undocumented and bit
    // `isGatewayIdentity`.
    const Transformed = Schema.Struct({ turn: Schema.BigIntFromNumber });
    const isTransformed = isTolerant(Transformed);
    const decodeTransformed = decodeTolerant(Transformed);

    // Wire form: decodes, but is not a decoded value.
    expect(Either.isRight(decodeTransformed({ turn: 7 }))).toBe(true);
    expect(isTransformed({ turn: 7 })).toBe(false);

    // Decoded form: is a decoded value, but is not the wire form.
    expect(isTransformed({ turn: 7n })).toBe(true);
    expect(Either.isRight(decodeTransformed({ turn: 7n }))).toBe(false);
  });

  test('the tolerance options are actually applied, not merely declared', () => {
    // `TOLERANT_PARSE_OPTIONS` is pinned literally below, but a literal object
    // proves nothing if it is never passed. A default-options `Schema.is`
    // refuses the excess property that `isTolerant` must accept.
    const withExtra = { game_id: 'g', current_turn: 1, extra: 'ok' };
    expect(isTolerant(Row)(withExtra)).toBe(true);
    expect(Schema.is(Row, { onExcessProperty: 'error' })(withExtra)).toBe(false);
  });

  test('schemaLabel prefers the caller name, then the identifier annotation', () => {
    expect(schemaLabel(Row)).toBe('Row');
    expect(schemaLabel(Row, 'Override')).toBe('Override');
    expect(schemaLabel(Schema.Struct({ a: Schema.String }))).toBe('{ readonly a: string }');
  });

  test('formatIssuePath names the root explicitly', () => {
    expect(formatIssuePath([])).toBe('<root>');
    expect(formatIssuePath(['games', '0'])).toBe('games.0');
  });

  test('the shared parse options are the ones the promises rest on', () => {
    expect(TOLERANT_PARSE_OPTIONS).toEqual({
      onExcessProperty: 'preserve',
      errors: 'all',
      propertyOrder: 'original',
    });
  });
});
