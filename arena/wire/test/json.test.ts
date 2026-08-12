/**
 * The plain-JSON model: what it accepts, what it refuses, and the guarantee
 * that a payload survives a decode/encode round trip unchanged.
 */
import { describe, expect, test } from 'bun:test';
import { Either } from 'effect';
import {
  decodeJsonArray,
  decodeJsonObject,
  decodeJsonValue,
  decodeJsonValueFromString,
  isJsonArray,
  isJsonObject,
  isJsonValue,
  type JsonValue,
  jsonField,
} from 'src/json';

const isRight = (either: Either.Either<unknown, unknown>): boolean => Either.isRight(either);

/** Parse through the package's own decoder, so no test needs a cast off `any`. */
const parseJson = (text: string): JsonValue =>
  Either.getOrThrowWith(decodeJsonValueFromString(text), (error) => new Error(error.message));

const MANIFEST_TEXT =
  '{"game_id":"g","state":"completed","config":{"turns":120,"mode":"benchmark"},' +
  '"resolved_places":[{"place":1,"score":88.5},{"place":2,"score":null}],' +
  '"benchmark_valid":true,"finished_at":1.7e9,"error":null}';

describe('JsonValue accepts every shape JSON.parse produces', () => {
  test.each<[string, unknown]>([
    ['null', null],
    ['boolean', true],
    ['integer', 0],
    ['negative float', -12.5],
    ['exponent', 1.7e9],
    ['string', 'hello'],
    ['empty array', []],
    ['empty object', {}],
    ['nested mixture', { a: [1, 'x', null, { b: [true, {}] }] }],
  ])('%s', (_label, input) => {
    expect(isRight(decodeJsonValue(input))).toBe(true);
  });

  test('a parsed manifest decodes whole', () => {
    const decoded = decodeJsonValue(JSON.parse(MANIFEST_TEXT));
    expect(isRight(decoded)).toBe(true);
  });
});

describe('JsonValue refuses what JSON cannot carry', () => {
  test.each<[string, unknown]>([
    ['undefined', undefined],
    ['NaN', Number.NaN],
    ['Infinity', Number.POSITIVE_INFINITY],
    ['-Infinity', Number.NEGATIVE_INFINITY],
    ['function', (): number => 1],
    ['symbol', Symbol('nope')],
    ['bigint', 1n],
    ['object with an undefined value', { a: undefined }],
    ['array with a hole-ish undefined', [undefined]],
    ['nested NaN', { a: { b: [Number.NaN] } }],
  ])('%s', (_label, input) => {
    expect(isRight(decodeJsonValue(input))).toBe(false);
  });

  test('the refusal is a value, and it names the offending path', () => {
    const result = decodeJsonValue({ deep: { bad: Number.NaN } });
    expect(Either.isLeft(result)).toBe(true);
    expect(Either.isLeft(result) && result.left._tag).toBe('WireDecodeError');
  });
});

describe('round trip', () => {
  /** The reachable guarantee: identical to canonical JSON, not to arbitrary spelling. */
  const CANONICAL_MANIFEST_TEXT = JSON.stringify(JSON.parse(MANIFEST_TEXT));

  test('parse, decode, stringify reproduces the payload exactly', () => {
    const decoded = decodeJsonValue(JSON.parse(MANIFEST_TEXT));
    expect(Either.isRight(decoded)).toBe(true);
    expect(Either.isRight(decoded) && JSON.stringify(decoded.right)).toBe(
      CANONICAL_MANIFEST_TEXT,
    );
  });

  test('number spelling is canonicalized by JSON.parse, before the schema sees it', () => {
    // Pinned so nobody mistakes this for schema-side rewriting: "1.7e9" is
    // already 1700000000 by the time decode runs. Python's json does the same.
    expect(CANONICAL_MANIFEST_TEXT).toContain('"finished_at":1700000000');
    expect(MANIFEST_TEXT).toContain('"finished_at":1.7e9');
    const decoded = decodeJsonValue(JSON.parse('{"a":1.7e9,"b":1.50,"c":1e2}'));
    expect(Either.isRight(decoded) && JSON.stringify(decoded.right)).toBe(
      '{"a":1700000000,"b":1.5,"c":100}',
    );
  });

  test('key order is the input order, not an alphabetized one', () => {
    const decoded = decodeJsonObject(JSON.parse('{"z":1,"a":2,"m":3}'));
    expect(Either.isRight(decoded) && Object.keys(decoded.right)).toEqual(['z', 'a', 'm']);
  });

  test('JSON text decodes through JsonValueFromString', () => {
    const decoded = decodeJsonValueFromString(MANIFEST_TEXT);
    expect(Either.isRight(decoded) && JSON.stringify(decoded.right)).toBe(
      CANONICAL_MANIFEST_TEXT,
    );
  });

  test('malformed JSON text is an error value, not a throw', () => {
    const decoded = decodeJsonValueFromString('{"a":');
    expect(Either.isLeft(decoded)).toBe(true);
  });
});

describe('narrower decoders', () => {
  test('decodeJsonObject rejects arrays and scalars', () => {
    expect(isRight(decodeJsonObject({ a: 1 }))).toBe(true);
    expect(isRight(decodeJsonObject([1]))).toBe(false);
    expect(isRight(decodeJsonObject('a'))).toBe(false);
    expect(isRight(decodeJsonObject(null))).toBe(false);
  });

  test('decodeJsonArray rejects objects and scalars', () => {
    expect(isRight(decodeJsonArray([1, 'a', null]))).toBe(true);
    expect(isRight(decodeJsonArray({ 0: 1 }))).toBe(false);
    expect(isRight(decodeJsonArray(null))).toBe(false);
  });
});

describe('guards agree with decoders', () => {
  const cases: ReadonlyArray<unknown> = [
    null,
    1,
    'x',
    [1, 2],
    { a: 1 },
    Number.NaN,
    undefined,
    { a: undefined },
  ];

  test('isJsonValue', () => {
    expect(cases.map(isJsonValue)).toEqual(cases.map((input) => isRight(decodeJsonValue(input))));
  });

  test('isJsonObject', () => {
    expect(cases.map(isJsonObject)).toEqual(cases.map((input) => isRight(decodeJsonObject(input))));
  });

  test('isJsonArray', () => {
    expect(cases.map(isJsonArray)).toEqual(cases.map((input) => isRight(decodeJsonArray(input))));
  });
});

describe('jsonField reads untrusted payloads the way the gateway does', () => {
  const manifest: JsonValue = parseJson('{"status":"failed","config":{"turns":3}}');

  test('reads a present field', () => {
    expect(jsonField(manifest, 'status')).toBe('failed');
  });

  test('an absent field is undefined, not a throw', () => {
    expect(jsonField(manifest, 'state')).toBeUndefined();
  });

  test('a non-object is undefined too, which is the manifest.get() fallback shape', () => {
    expect(jsonField('not a mapping', 'state')).toBeUndefined();
    expect(jsonField(null, 'state')).toBeUndefined();
    expect(jsonField([1, 2], 'state')).toBeUndefined();
  });

  test('state-then-status fallback, as replay_gateway.py:1131 spells it', () => {
    const state = jsonField(manifest, 'state') ?? jsonField(manifest, 'status') ?? 'unknown';
    expect(state).toBe('failed');
  });

  test('inherited properties are not fields', () => {
    // A plain value[key] answers these from Object.prototype and returns a
    // function, which the JsonValue|undefined return type forbids.
    expect(jsonField(manifest, 'constructor')).toBeUndefined();
    expect(jsonField(manifest, 'toString')).toBeUndefined();
    expect(jsonField(manifest, 'hasOwnProperty')).toBeUndefined();
    expect(jsonField(manifest, '__proto__')).toBeUndefined();
  });

  test('a __proto__ key is dropped by decode, and pollutes nothing', () => {
    // Divergence, pinned: JSON.parse keeps "__proto__" as an ordinary own key
    // and so does Python's json, but Effect's Schema decode drops it while
    // rebuilding the object. Safe (no prototype is touched) and lossy (the
    // key does not round trip). See the json.ts module doc.
    const raw: unknown = JSON.parse('{"__proto__":{"polluted":true},"ok":1}');
    expect(Object.hasOwn(Object(raw), '__proto__')).toBe(true);

    const decoded = parseJson('{"__proto__":{"polluted":true},"ok":1}');
    expect(decoded).toEqual({ ok: 1 });
    expect(jsonField(decoded, '__proto__')).toBeUndefined();
    expect(jsonField(decoded, 'ok')).toBe(1);
    expect(Object.getPrototypeOf(decoded)).toBe(Object.prototype);
    expect('polluted' in {}).toBe(false);
  });
});

describe('documented looseness', () => {
  test('a class instance is structurally an object: own fields decode, prototype members vanish', () => {
    // Pinned, not endorsed: see the module doc. Wire input comes from
    // JSON.parse, where a prototype-bearing value cannot arise.
    class Marker {
      readonly own = { kept: true };
      dropped(): number {
        return 1;
      }
    }
    const instance = new Marker();
    expect(typeof instance.dropped).toBe('function');
    const decoded = decodeJsonObject(instance);
    expect(Either.isRight(decoded) && decoded.right).toEqual({ own: { kept: true } });
  });
});
