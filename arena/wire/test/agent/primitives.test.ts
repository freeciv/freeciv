/**
 * Agent-side wire primitives, checked against `play/client.py`.
 *
 * The assertions that matter most are the *refusal* sentences.  `play/tests/`
 * checks them verbatim on the Python side and
 * `play-cli/test/schema.test.ts:52-105` checks them on the TypeScript side, so
 * a reworded message is a behavioural break even when the happy path is
 * untouched.  Where a case is lifted from one of those suites, the line is
 * cited next to it.
 */
import { describe, expect, test } from 'bun:test';
import { Either } from 'effect';
import {
  BoundedJsonValue,
  boundedJson,
  boundedJsonObject,
  boundedText,
  checkEvaluationDrift,
  decodeBoundedJsonObject,
  decodeEvaluationContext,
  decodeEvaluationSlot,
  decodeSessionIdentity,
  driftError,
  EvaluationContext,
  exactFields,
  fieldDrift,
  isRecord,
  JSON_MAX_DEPTH,
  JSON_MAX_ITEMS,
  JSON_MAX_KEY_LENGTH,
  JSON_MAX_KEYS,
  jsonNodeDepth,
  MAX_TURNS_CEILING,
  NonEmptyText,
  NonNegativeWireInt,
  PositiveWireInt,
  safeNumber,
  SessionIdentity,
  sortedNames,
  V2_EVALUATION_FIELDS,
  WireInt,
} from 'src/agent/primitives';
import { OpaqueId, PlayGameId } from 'src/agent/ids';
import { encodeTolerant, isTolerant } from 'src/tolerant';

const accepts = (either: Either.Either<unknown, unknown>): boolean => Either.isRight(either);

const message = <A>(either: Either.Either<A, { readonly message: string }>): string => {
  expect(Either.isLeft(either)).toBe(true);
  return Either.isLeft(either) ? either.left.message : '';
};

const value = <A, E>(either: Either.Either<A, E>): A => {
  expect(Either.isRight(either)).toBe(true);
  if (Either.isLeft(either)) throw new Error('expected a decoded value');
  return either.right;
};

// play-cli/test/_fixtures/wire.ts:12-16.  Built through the brand
// constructors so a fixture that stopped matching its own pattern fails here
// rather than silently becoming an untyped string.
const FIXTURE_GAME_ID = PlayGameId.make('game_Hsit9YEuBjKdJPPouFoGVYlk');
const FIXTURE_AGENT_ID = OpaqueId.make('agent_0123456789abcdef');
const FIXTURE_CONTROLLER = 'codex-gpt-5.6-sol';

const EVALUATION = {
  objective: 'maximise score by turn 120',
  max_turns: 120,
  turns_remaining: 40,
} as const;

// ---------------------------------------------------------------------------

describe('driftError', () => {
  test('rebuilds both of the Python PlayerError spellings', () => {
    expect(driftError('thing').message).toBe('invalid thing');
    expect(driftError('thing', 'because').message).toBe('invalid thing: because');
  });

  test('carries the label as the schema name so a switch can branch on it', () => {
    expect(driftError('state token').schemaName).toBe('state token');
    expect(driftError('x')._tag).toBe('WireDecodeError');
  });
});

describe('sortedNames', () => {
  test('orders by code point, the way Python sorted() does', () => {
    // JS `<` compares UTF-16 code units, where "￿" > "\u{10000}"; Python
    // compares code points, where it is smaller.  Field names come from the
    // server, so the parity-safe order is the one that costs nothing.
    expect(sortedNames(['\u{10000}', '￿'])).toEqual(['￿', '\u{10000}']);
    expect(['\u{10000}', '￿'].toSorted()).toEqual(['\u{10000}', '￿']);
  });

  test('agrees with the plain sort below U+D800', () => {
    expect(sortedNames(['c', 'a', 'B', '_'])).toEqual(['B', '_', 'a', 'c']);
  });
});

describe('exactFields', () => {
  test('names the drift rather than dumping the field list', () => {
    // play-cli/test/schema.test.ts:53-57
    expect(message(exactFields({ a: 1, c: 3 }, new Set(['a', 'b']), 'thing'))).toBe(
      'invalid thing: missing b; unexpected c. Expected exactly a, b',
    );
  });

  test('refuses a non-object by naming what it wanted', () => {
    // play-cli/test/schema.test.ts:60-64
    expect(message(exactFields(7, new Set(['a', 'b']), 'thing'))).toBe(
      'invalid thing: expected a JSON object with exactly a, b',
    );
    expect(message(exactFields(null, new Set(['a']), 'thing'))).toBe(
      'invalid thing: expected a JSON object with exactly a',
    );
    expect(message(exactFields([], new Set(['a']), 'thing'))).toBe(
      'invalid thing: expected a JSON object with exactly a',
    );
  });

  test('passes an object whose keys match', () => {
    // play-cli/test/schema.test.ts:66-68
    expect(value(exactFields({ a: 1 }, new Set(['a']), 'thing'))).toEqual({ a: 1 });
  });

  test('reports only the half that drifted', () => {
    expect(message(exactFields({ a: 1 }, new Set(['a', 'b']), 'thing'))).toBe(
      'invalid thing: missing b. Expected exactly a, b',
    );
    expect(message(exactFields({ a: 1, z: 2 }, new Set(['a']), 'thing'))).toBe(
      'invalid thing: unexpected z. Expected exactly a',
    );
  });

  test('a null value still counts as a present key', () => {
    // Python's `set(value)` is over keys; `None` does not hide one.
    expect(accepts(exactFields({ a: null }, new Set(['a']), 'thing'))).toBe(true);
  });

  test('inherited properties are not present keys', () => {
    // Python's `set(dict)` never walks a base class; `Object.keys` never walks
    // the prototype chain.  A payload cannot smuggle a key in from up there.
    const child = { __proto__: { b: 1 }, a: 1 };
    expect(Object.keys(child)).toEqual(['a']);
    expect(accepts(exactFields(child, new Set(['a']), 'thing'))).toBe(true);
  });

  test('the structured issues mirror the sentence', () => {
    const drift = fieldDrift(['a', 'c'], new Set(['a', 'b']));
    expect(drift).toEqual({ missing: ['b'], unexpected: ['c'] });
    const failure = exactFields({ a: 1, c: 3 }, new Set(['a', 'b']), 'thing');
    expect(Either.isLeft(failure)).toBe(true);
    if (Either.isLeft(failure)) {
      expect(failure.left.issues.map((issue) => [issue.kind, issue.path])).toEqual([
        ['Missing', ['b']],
        ['Unexpected', ['c']],
      ]);
    }
  });
});

/** `levels` nested single-key objects wrapped around `core`. */
const nest = (levels: number, core: unknown = 1): unknown =>
  Array.from({ length: levels }).reduce<unknown>((acc) => ({ n: acc }), core);

const items = (count: number): ReadonlyArray<number> => Array.from({ length: count }, () => 0);

const keyed = (count: number): Record<string, number> =>
  Object.fromEntries(Array.from({ length: count }, (_, index) => [`k${String(index)}`, 0]));

describe('boundedJson', () => {
  test('refuses non-finite numbers and over-deep nesting', () => {
    // play-cli/test/schema.test.ts:75-82
    expect(message(boundedJson(Number.POSITIVE_INFINITY, 'x'))).toBe(
      'invalid x: number is not finite',
    );
    expect(message(boundedJson(Number.NaN, 'x'))).toBe('invalid x: number is not finite');
    expect(message(boundedJson(nest(14), 'x'))).toBe('invalid x: JSON is nested too deeply');
  });

  test('the depth ceiling is exactly 12 ancestors', () => {
    expect(accepts(boundedJson(nest(JSON_MAX_DEPTH), 'x'))).toBe(true);
    expect(accepts(boundedJson(nest(JSON_MAX_DEPTH + 1), 'x'))).toBe(false);
    expect(jsonNodeDepth(value(boundedJson(nest(JSON_MAX_DEPTH), 'x')))).toBe(JSON_MAX_DEPTH);
  });

  test('an empty container has depth zero and survives at the ceiling', () => {
    expect(jsonNodeDepth([])).toBe(0);
    expect(jsonNodeDepth({})).toBe(0);
    expect(jsonNodeDepth(1)).toBe(0);
    expect(accepts(boundedJson(nest(JSON_MAX_DEPTH, {}), 'x'))).toBe(true);
  });

  test('array and object sizes are capped, inclusively', () => {
    expect(accepts(boundedJson(items(JSON_MAX_ITEMS), 'x'))).toBe(true);
    expect(message(boundedJson(items(JSON_MAX_ITEMS + 1), 'x'))).toBe('invalid x: too many items');

    expect(accepts(boundedJson(keyed(JSON_MAX_KEYS), 'x'))).toBe(true);
    expect(message(boundedJson(keyed(JSON_MAX_KEYS + 1), 'x'))).toBe('invalid x: invalid object');
  });

  test('an empty key is refused', () => {
    expect(message(boundedJson({ '': 1 }, 'x'))).toBe('invalid x: invalid object');
  });

  test('key length is counted in code points, as Python len() does', () => {
    // DIVERGENCE from play-cli, which measures key.length (UTF-16 units) and
    // therefore refuses a 128-emoji key the Python accepts.
    const emoji = '\u{1F600}'.repeat(JSON_MAX_KEY_LENGTH);
    expect(emoji.length).toBe(JSON_MAX_KEY_LENGTH * 2);
    expect([...emoji]).toHaveLength(JSON_MAX_KEY_LENGTH);
    expect(accepts(boundedJson({ [emoji]: 1 }, 'x'))).toBe(true);

    expect(accepts(boundedJson({ ['a'.repeat(JSON_MAX_KEY_LENGTH)]: 1 }, 'x'))).toBe(true);
    expect(message(boundedJson({ ['a'.repeat(JSON_MAX_KEY_LENGTH + 1)]: 1 }, 'x'))).toBe(
      'invalid x: invalid object',
    );
  });

  test('a value JSON cannot carry is refused, not coerced', () => {
    expect(message(boundedJson(undefined, 'x'))).toBe('invalid x: non-JSON value');
    expect(message(boundedJson(() => 1, 'x'))).toBe('invalid x: non-JSON value');
    expect(message(boundedJson(Symbol('s'), 'x'))).toBe('invalid x: non-JSON value');
    expect(message(boundedJson(1n, 'x'))).toBe('invalid x: non-JSON value');
  });

  test('the bound is checked before the contents, deepest sentence wins', () => {
    // Depth is the Python's first test, so a deep tree that also holds an
    // infinity reports the depth.
    expect(message(boundedJson(nest(14, Number.POSITIVE_INFINITY), 'x'))).toBe(
      'invalid x: JSON is nested too deeply',
    );
  });

  test('the result is a copy, so a payload cannot alias into decoded state', () => {
    const source = { nested: { list: [1, 2] } };
    const copied = value(boundedJson(source, 'x'));
    expect(copied).toEqual(source);
    expect(copied).not.toBe(source);
    expect(isRecord(copied)).toBe(true);
    if (isRecord(copied)) {
      expect(copied['nested']).not.toBe(source.nested);
    }
  });

  test('boundedJsonObject narrows, and says so when it cannot', () => {
    expect(value(boundedJsonObject({ a: 1 }, 'x'))).toEqual({ a: 1 });
    expect(message(boundedJsonObject([1], 'x'))).toBe('invalid x');
    expect(message(boundedJsonObject('text', 'x'))).toBe('invalid x');
  });

  test('the schema face agrees with the function face', () => {
    const isBounded = isTolerant(BoundedJsonValue);
    expect(isBounded({ a: [1, 'two', null] })).toBe(true);
    expect(isBounded(nest(14))).toBe(false);
    expect(accepts(decodeBoundedJsonObject({ a: 1 }))).toBe(true);
    expect(accepts(decodeBoundedJsonObject(nest(14)))).toBe(false);
    expect(accepts(decodeBoundedJsonObject([1]))).toBe(false);
  });
});

describe('safeNumber', () => {
  test('refuses negatives and honours nullable', () => {
    // play-cli/test/schema.test.ts:85-90
    expect(value(safeNumber(0, 'x'))).toBe(0);
    expect(message(safeNumber(-1, 'x'))).toBe('invalid x');
    expect(value(safeNumber(null, 'x', { nullable: true }))).toBeNull();
    expect(message(safeNumber(null, 'x'))).toBe('invalid x');
  });

  test('accepts floats — these are seconds, not counters', () => {
    expect(value(safeNumber(1.5, 'x'))).toBe(1.5);
    expect(value(safeNumber(0.0, 'x'))).toBe(0);
  });

  test('refuses the non-numbers Python refuses', () => {
    expect(message(safeNumber(Number.NaN, 'x'))).toBe('invalid x');
    expect(message(safeNumber(Number.POSITIVE_INFINITY, 'x'))).toBe('invalid x');
    expect(message(safeNumber(true, 'x'))).toBe('invalid x');
    expect(message(safeNumber('1', 'x'))).toBe('invalid x');
    expect(message(safeNumber(undefined, 'x'))).toBe('invalid x');
  });

  test('nullable does not also admit a bad number', () => {
    expect(message(safeNumber(-1, 'x', { nullable: true }))).toBe('invalid x');
  });
});

describe('EvaluationContext', () => {
  test('decodes the three-field shape', () => {
    expect(value(decodeEvaluationContext({ ...EVALUATION }))).toEqual(EVALUATION);
  });

  test('turns_remaining may be null but max_turns may not', () => {
    expect(
      value(decodeEvaluationContext({ ...EVALUATION, turns_remaining: null })).turns_remaining,
    ).toBeNull();
    expect(accepts(decodeEvaluationContext({ ...EVALUATION, max_turns: null }))).toBe(false);
  });

  test('the objective must be non-empty and already trimmed', () => {
    expect(accepts(decodeEvaluationContext({ ...EVALUATION, objective: '' }))).toBe(false);
    expect(accepts(decodeEvaluationContext({ ...EVALUATION, objective: ' pad' }))).toBe(false);
    expect(accepts(decodeEvaluationContext({ ...EVALUATION, objective: 'pad ' }))).toBe(false);
    expect(accepts(decodeEvaluationContext({ ...EVALUATION, objective: '   ' }))).toBe(false);
  });

  test('max_turns is an integer in [1, 5000]', () => {
    const withTurns = (max: number): unknown => ({
      objective: EVALUATION.objective,
      max_turns: max,
      turns_remaining: 0,
    });
    expect(accepts(decodeEvaluationContext(withTurns(1)))).toBe(true);
    expect(accepts(decodeEvaluationContext(withTurns(MAX_TURNS_CEILING)))).toBe(true);
    expect(accepts(decodeEvaluationContext(withTurns(0)))).toBe(false);
    expect(accepts(decodeEvaluationContext(withTurns(MAX_TURNS_CEILING + 1)))).toBe(false);
    expect(accepts(decodeEvaluationContext(withTurns(1.5)))).toBe(false);
    expect(accepts(decodeEvaluationContext(withTurns(-1)))).toBe(false);
  });

  test('turns_remaining is bounded by max_turns, not by 5000', () => {
    expect(accepts(decodeEvaluationContext({ ...EVALUATION, turns_remaining: 120 }))).toBe(true);
    expect(accepts(decodeEvaluationContext({ ...EVALUATION, turns_remaining: 121 }))).toBe(false);
    expect(accepts(decodeEvaluationContext({ ...EVALUATION, turns_remaining: 0 }))).toBe(true);
    expect(accepts(decodeEvaluationContext({ ...EVALUATION, turns_remaining: -1 }))).toBe(false);
  });

  test('booleans are not integers here either', () => {
    expect(accepts(decodeEvaluationContext({ ...EVALUATION, max_turns: true }))).toBe(false);
    expect(accepts(decodeEvaluationContext({ ...EVALUATION, turns_remaining: true }))).toBe(false);
  });

  test('an added field is preserved, not refused — the tolerance rule', () => {
    const payload = { ...EVALUATION, scoring_profile: 'v3' };
    const decoded = value(decodeEvaluationContext(payload));
    expect(decoded.max_turns).toBe(120);
    const roundTripped = value(encodeTolerant(EvaluationContext)(decoded));
    expect(roundTripped).toEqual(payload);
    expect(Object.keys(roundTripped)).toEqual(Object.keys(payload));
  });
});

describe('decodeEvaluationSlot', () => {
  const host = { game_id: FIXTURE_GAME_ID, ...EVALUATION };

  test('reads the context out of an object that carries other fields', () => {
    expect(value(decodeEvaluationSlot(host, 'private v2 session'))).toEqual(EVALUATION);
  });

  test('none of the three keys is null, or a refusal when required', () => {
    expect(value(decodeEvaluationSlot({ game_id: FIXTURE_GAME_ID }, 'x'))).toBeNull();
    expect(message(decodeEvaluationSlot({}, 'x', { required: true }))).toBe(
      'invalid x: evaluation context is missing',
    );
  });

  test('some but not all is incomplete', () => {
    expect(message(decodeEvaluationSlot({ objective: 'win' }, 'x'))).toBe(
      'invalid x: evaluation context is incomplete',
    );
    expect(
      message(decodeEvaluationSlot({ objective: 'win', max_turns: 10 }, 'x')),
    ).toBe('invalid x: evaluation context is incomplete');
  });

  test('all three but one is bad is malformed', () => {
    // play-cli/test/join.test.ts:734 asserts this sentence.
    expect(message(decodeEvaluationSlot({ ...host, max_turns: 0 }, 'v2 join result'))).toBe(
      'invalid v2 join result: evaluation context is malformed',
    );
    expect(message(decodeEvaluationSlot({ ...host, objective: ' pad' }, 'x'))).toBe(
      'invalid x: evaluation context is malformed',
    );
  });

  test('the malformed sentence keeps the per-field detail in issues', () => {
    const failure = decodeEvaluationSlot({ ...host, max_turns: 0 }, 'x');
    expect(Either.isLeft(failure)).toBe(true);
    if (Either.isLeft(failure)) {
      expect(failure.left.issues.map((issue) => issue.path)).toEqual([['max_turns']]);
    }
  });

  test('a non-object host is the bare refusal', () => {
    expect(message(decodeEvaluationSlot(7, 'x'))).toBe('invalid x');
    expect(message(decodeEvaluationSlot(null, 'x'))).toBe('invalid x');
  });

  test('the three field names are the ones the Python intersects on', () => {
    expect(sortedNames(V2_EVALUATION_FIELDS)).toEqual([
      'max_turns',
      'objective',
      'turns_remaining',
    ]);
  });
});

describe('checkEvaluationDrift', () => {
  const expected = { ...EVALUATION };

  test('turns_remaining is allowed to move — it is the only field that should', () => {
    const later = { ...EVALUATION, turns_remaining: 39 };
    expect(value(checkEvaluationDrift(later, expected, 'x'))).toEqual(later);
  });

  test('a changed objective or max_turns is a refusal', () => {
    expect(
      message(checkEvaluationDrift({ ...EVALUATION, objective: 'other' }, expected, 'x')),
    ).toBe('invalid x: evaluation objective changed');
    expect(message(checkEvaluationDrift({ ...EVALUATION, max_turns: 200 }, expected, 'x'))).toBe(
      'invalid x: evaluation max_turns changed',
    );
  });

  test('the objective is reported first when both changed', () => {
    const both = { objective: 'other', max_turns: 200, turns_remaining: null };
    expect(message(checkEvaluationDrift(both, expected, 'x'))).toBe(
      'invalid x: evaluation objective changed',
    );
  });
});

describe('SessionIdentity', () => {
  const identity = {
    gameId: FIXTURE_GAME_ID,
    agentId: FIXTURE_AGENT_ID,
    controllerLabel: FIXTURE_CONTROLLER,
    place: 1,
    seatId: 'seat_one',
    playerName: 'Alice',
    evaluation: null,
  };

  test('decodes the fixture identity', () => {
    // Shape from play-cli/test/_fixtures/wire.ts:24-33.
    expect(value(decodeSessionIdentity(identity))).toEqual(identity);
  });

  test('the two ids the Python validates are validated', () => {
    expect(accepts(decodeSessionIdentity({ ...identity, gameId: 'not-a-game-id' }))).toBe(false);
    expect(accepts(decodeSessionIdentity({ ...identity, agentId: 'has space' }))).toBe(false);
  });

  test('the controller label is only checked for non-emptiness', () => {
    expect(accepts(decodeSessionIdentity({ ...identity, controllerLabel: 'ab' }))).toBe(true);
    expect(accepts(decodeSessionIdentity({ ...identity, controllerLabel: '' }))).toBe(false);
  });

  test('place, seat and player name are nullable', () => {
    const anonymous = { ...identity, place: null, seatId: null, playerName: null };
    expect(value(decodeSessionIdentity(anonymous))).toEqual(anonymous);
  });

  test('an evaluation context rides along when there is one', () => {
    const joined = { ...identity, evaluation: { ...EVALUATION } };
    expect(value(decodeSessionIdentity(joined)).evaluation).toEqual(EVALUATION);
  });

  test('the schema is the source of truth for the type', () => {
    // Assigning to the exported type is the assertion: it is `typeof
    // SessionIdentity.Type`, so this line stops compiling if the two drift.
    const typed: SessionIdentity = value(decodeSessionIdentity(identity));
    expect(typed.gameId).toBe(FIXTURE_GAME_ID);
    expect(typed.evaluation).toBeNull();
  });
});

describe('integers and text', () => {
  test('WireInt is an integer, and a boolean is not one', () => {
    const isInt = isTolerant(WireInt);
    expect(isInt(5)).toBe(true);
    expect(isInt(-5)).toBe(true);
    expect(isInt(5.5)).toBe(false);
    expect(isInt(true)).toBe(false);
    expect(isInt(Number.NaN)).toBe(false);
  });

  test('the non-negative and positive variants differ exactly at zero', () => {
    const nonNegative = isTolerant(NonNegativeWireInt);
    const positive = isTolerant(PositiveWireInt);
    expect(nonNegative(0)).toBe(true);
    expect(positive(0)).toBe(false);
    expect(nonNegative(-1)).toBe(false);
    expect(positive(1)).toBe(true);
  });

  test('NonEmptyText refuses only the empty string, padding included', () => {
    const isText = isTolerant(NonEmptyText);
    expect(isText('a')).toBe(true);
    expect(isText(' ')).toBe(true);
    expect(isText('')).toBe(false);
  });

  test('boundedText refuses blanks and counts the ceiling in code points', () => {
    const isMessage = isTolerant(boundedText(500, 'V2ErrorMessage'));
    expect(isMessage('that unit cannot found a city here')).toBe(true);
    expect(isMessage('')).toBe(false);
    expect(isMessage('   ')).toBe(false);
    expect(isMessage('a'.repeat(500))).toBe(true);
    expect(isMessage('a'.repeat(501))).toBe(false);
    // 500 emoji are 1000 UTF-16 units and 500 Python characters.
    expect(isMessage('\u{1F600}'.repeat(500))).toBe(true);
    expect(isMessage('\u{1F600}'.repeat(501))).toBe(false);
  });

  test('boundedText measures the ceiling on the unstripped string', () => {
    // Python: `len(message) > 500` runs on the raw value, so padding counts.
    const isMessage = isTolerant(boundedText(5, 'Short'));
    expect(isMessage('abcde')).toBe(true);
    expect(isMessage(' abcde')).toBe(false);
  });
});

describe('isRecord', () => {
  test('is Python isinstance(value, dict), not a JSON validation', () => {
    expect(isRecord({})).toBe(true);
    expect(isRecord({ fn: () => 1 })).toBe(true);
    expect(isRecord([])).toBe(false);
    expect(isRecord(null)).toBe(false);
    expect(isRecord('text')).toBe(false);
  });
});
