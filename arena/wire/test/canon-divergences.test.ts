/**
 * Canonical JSON — the five findings the adversarial hunt returned, pinned.
 *
 * Three of them are *not* byte divergences and the point of pinning them is to
 * stop a future reader from "fixing" them: the hunt proved that in every mode
 * where CPython produces bytes at all, canon produces the same bytes.  Two were
 * real, and their fixes live in `src/canon.ts`:
 *
 *   1. **lone-surrogate offsets** — `LoneSurrogateError.index` counts UTF-16
 *      code units, `UnicodeEncodeError.start` counts code points.  Both sides
 *      refuse the string, so only the diagnostic integer differs.  Documented
 *      on the error class; asserted below so the docstring cannot rot.
 *   2. **a Python two-character surrogate pair** — no JS value denotes it, so
 *      the disagreement is representational and confined to the one mode where
 *      CPython emits nothing.
 *   3. **sparse arrays** — was a `TypeError` thrown out of `canonicalText`,
 *      breaking the module's "nothing here throws" contract.  Fixed by
 *      `Array.from`, which materializes holes instead of skipping them.
 *   4. **`CANON_MAX_DEPTH`** — a real, deliberate, now-honestly-documented
 *      divergence: CPython 3.12+ encodes far deeper than canon will.
 *   5. **`__proto__`** — a caller hazard, not a canon bug.  Canon never sees
 *      the key, because the JS value does not have it.
 *
 * Every CPython figure below was produced by running CPython 3.14.6 on this
 * box, not derived from this package.  Non-ASCII characters are written as
 * `\uXXXX` escapes so the file's own encoding cannot influence the assertions.
 */
import { describe, expect, test } from 'bun:test';
import { Either } from 'effect';
import {
  CANON_ASCII,
  CANON_MAX_DEPTH,
  CANON_UTF8,
  type CanonError,
  type CanonOptions,
  type CanonRecord,
  type CanonValue,
  canonicalBytes,
  canonicalText,
} from 'src/index';

const hexOf = (value: CanonValue, options: CanonOptions): string =>
  Either.getOrElse(
    Either.map(canonicalBytes(value, options), (bytes) => Buffer.from(bytes).toString('hex')),
    (error) => `<${error._tag}>`,
  );

const errorOf = (result: Either.Either<unknown, CanonError>): CanonError | undefined =>
  Either.getOrUndefined(Either.flip(result));

/** A string built from code points, never from text this file wrote out. */
const fromCodePoints = (points: readonly number[]): string =>
  points.map((point) => String.fromCodePoint(point)).join('');

/**
 * An array with *holes* — the runtime value {@link CanonValue} cannot express,
 * which is precisely what is under test.
 *
 * Built by assigning `length` last onto an empty array rather than written as
 * `[1n, , 3n]`: a sparse literal is a lint error (rightly — it is almost always
 * a typo), and `new Array(3)` is another.  `Object.assign([], {0: 1n, 2: 3n},
 * {length: 3})` gives an array of length 3 whose index 1 is genuinely absent,
 * with no loop and no sparse syntax.
 */
const sparseArray = (length: number, filled: Readonly<Record<number, unknown>>): CanonValue =>
  // A hole is not a `CanonValue`; reaching one is the point of the helper.
  // oxlint-disable-next-line no-unsafe-type-assertion
  Object.assign([] as unknown[], filled, { length }) as CanonValue;

/** `nest(3)` is `[[[]]]` — `depth` nested arrays, innermost empty. */
const nest = (depth: number): CanonValue =>
  Array.from({ length: depth - 1 }).reduce<CanonValue>((inner) => [inner], []);

describe('finding 1: the lone-surrogate offset is UTF-16, CPython’s is code points', () => {
  // `python3 -c "...; s.encode('utf-8','strict')"` → UnicodeEncodeError.start
  interface OffsetCase {
    readonly points: readonly number[];
    readonly pythonStart: number;
    readonly tsIndex: number;
    readonly unit: number;
  }

  const cases: OffsetCase[] = [
    { points: [0x10000, 0xd800], pythonStart: 1, tsIndex: 2, unit: 0xd800 },
    { points: [0x20, 0x10000, 0x100000, 0xd801], pythonStart: 3, tsIndex: 5, unit: 0xd801 },
    { points: [0x61, 0x7e, 0x10001, 0xdbff], pythonStart: 3, tsIndex: 4, unit: 0xdbff },
  ];

  test.each(cases)(
    'points %j: CPython refuses at code point $pythonStart, canon at code unit $tsIndex',
    ({ points, pythonStart, tsIndex, unit }) => {
      const text = fromCodePoints(points);
      const error = errorOf(canonicalText(text, CANON_UTF8));
      expect(error?._tag).toBe('LoneSurrogate');
      expect(error).toMatchObject({ index: tsIndex, unit });
      // The offsets disagree precisely because JS counts code units: the gap is
      // the number of astral characters ahead of the offending surrogate.
      const astralAhead = points
        .slice(0, pythonStart)
        .filter((point) => point > 0xffff).length;
      expect(tsIndex).toBe(pythonStart + astralAhead);
      // ...and it is only ever a diagnostic, because neither side emits bytes.
      expect(Either.isLeft(canonicalBytes(text, CANON_UTF8))).toBe(true);
    },
  );

  test('ASCII mode escapes the same surrogate instead, exactly as CPython does', () => {
    // python3: json.dumps(chr(0xD800), ensure_ascii=True, ...).encode('ascii').hex()
    expect(hexOf(fromCodePoints([0xd800]), CANON_ASCII)).toBe('225c756438303022');
  });
});

describe('finding 2: a Python two-character surrogate pair has no JS counterpart', () => {
  // Python `chr(0xD800) + chr(0xDC00)` has len 2.  The identical JS string is
  // ONE astral character (len 2 in code *units*), so canon cannot refuse it
  // without refusing every legitimate astral character.
  const pairAsJs = fromCodePoints([0xd800, 0xdc00]);

  test('JS collapses the pair to U+10000 — the values are genuinely different', () => {
    expect(pairAsJs).toBe(fromCodePoints([0x10000]));
    expect(pairAsJs.length).toBe(2);
    expect([...pairAsJs].length).toBe(1);
  });

  test('ASCII mode: byte parity holds anyway, for the pair and for U+10000 alike', () => {
    // python3, BOTH of Python's two distinct values, ensure_ascii=True:
    //   [chr(0xD800)+chr(0xDC00)] -> 5b225c75643830305c7564633030225d
    //   [chr(0x10000)]            -> 5b225c75643830305c7564633030225d
    const expected = '5b225c75643830305c7564633030225d';
    expect(hexOf([pairAsJs], CANON_ASCII)).toBe(expected);
    expect(hexOf([fromCodePoints([0x10000])], CANON_ASCII)).toBe(expected);
  });

  test('the ASCII wire form round-trips byte-stably through JSON.parse', () => {
    const parsed: unknown = JSON.parse('["\\ud800\\udc00"]');
    // A one-string array of strings; `JSON.parse` cannot say so in its type.
    // oxlint-disable-next-line no-unsafe-type-assertion
    const document = parsed as CanonValue;
    expect(hexOf(document, CANON_ASCII)).toBe('5b225c75643830305c7564633030225d');
  });

  test('UTF-8 mode disagrees only where CPython emits no bytes at all', () => {
    // python3: chr(0xD800)+chr(0xDC00) -> UnicodeEncodeError; chr(0x10000) -> f0908080
    expect(hexOf([pairAsJs], CANON_UTF8)).toBe('5b22f0908080225d');
  });

  test('the surrogate rail is not simply off — real unpaired units still refuse', () => {
    expect(errorOf(canonicalText(fromCodePoints([0xdc00, 0xd800]), CANON_UTF8))).toMatchObject({
      _tag: 'LoneSurrogate',
      index: 0,
      unit: 0xdc00,
    });
    expect(errorOf(canonicalText(fromCodePoints([0xd800, 0xd800]), CANON_UTF8))).toMatchObject({
      _tag: 'LoneSurrogate',
      index: 0,
      unit: 0xd800,
    });
  });
});

describe('finding 3: a sparse array is refused, not thrown (regression)', () => {
  // `Array.prototype.map` SKIPS holes and reproduces them, which left
  // `undefined` slots for `Either.all` to dereference:
  //   TypeError: undefined is not an object (evaluating 'ma._tag')
  // thrown straight out of canonicalText, in both modes.
  const holed = sparseArray(3, { 0: 1n, 2: 3n });

  const sparse: { readonly label: string; readonly value: CanonValue }[] = [
    { label: 'a hole in the middle', value: holed },
    { label: 'a trailing hole', value: sparseArray(2, { 0: 1n }) },
    { label: 'all holes', value: sparseArray(3, {}) },
    { label: 'a hole nested under a key', value: { a: sparseArray(1, {}) } },
  ];

  test.each(sparse)('$label refuses as a value in both modes', ({ value }) => {
    for (const options of [CANON_ASCII, CANON_UTF8]) {
      const result = canonicalText(value, options);
      expect(Either.isLeft(result)).toBe(true);
      expect(errorOf(result)?._tag).toBe('UnsupportedValue');
    }
  });

  test('the hole reports the path CPython would have filled', () => {
    expect(errorOf(canonicalText(holed, CANON_ASCII))?.message).toBe(
      '$[1]: undefined is not a JSON value',
    );
    expect(errorOf(canonicalText({ a: sparseArray(1, {}) }, CANON_ASCII))?.message).toBe(
      '$["a"][0]: undefined is not a JSON value',
    );
  });

  test('an explicit undefined has always behaved this way, and still does', () => {
    // The one place this file fabricates a value the types say cannot exist,
    // because refusing it is precisely what the writer is being tested for.
    // oxlint-disable-next-line no-unsafe-type-assertion
    const dense = [1n, undefined] as readonly CanonValue[];
    expect(errorOf(canonicalText(dense, CANON_ASCII))).toMatchObject({
      _tag: 'UnsupportedValue',
      kind: 'undefined',
    });
  });

  test('the dense control still encodes — the fix did not disturb it', () => {
    // python3: json.dumps([1,None,3], ...) -> '[1,null,3]' -> 5b312c6e756c6c2c335d
    expect(hexOf([1n, null, 3n], CANON_ASCII)).toBe('5b312c6e756c6c2c335d');
  });
});

describe('finding 4: CANON_MAX_DEPTH is canon’s own bound, not CPython’s', () => {
  test('the last accepted depth is exactly CANON_MAX_DEPTH', () => {
    const result = canonicalText(nest(CANON_MAX_DEPTH), CANON_ASCII);
    expect(Either.isRight(result)).toBe(true);
    // python3 at depth 1000: 2000 bytes, `[`x1000 + `]`x1000.
    expect(Either.getOrThrow(result).length).toBe(2 * CANON_MAX_DEPTH);
  });

  test('one deeper is a value, not a RangeError — which is the whole point', () => {
    const error = errorOf(canonicalText(nest(CANON_MAX_DEPTH + 1), CANON_ASCII));
    expect(error?._tag).toBe('MaxDepthExceeded');
    expect(error).toMatchObject({ limit: CANON_MAX_DEPTH });
  });

  test('CPython would have encoded it, and that gap is documented, not accidental', () => {
    // Measured on CPython 3.14.6, bare `python3`, sys.getrecursionlimit() == 1000
    // and no setrecursionlimit call:
    //   depth 1001   -> 2002 bytes
    //   depth 100000 -> 200000 bytes
    //   depth 150000 -> RecursionError
    // canon refuses everything past 1000 because its writer is recursive on the
    // JS stack, which under Bun 1.4 gives out around 8000 nested objects.  The
    // rail is an order of magnitude below the shallowest measured ceiling so
    // that a deep document is an error value rather than a thrown RangeError.
    for (const depth of [CANON_MAX_DEPTH + 1, 2000, 20000]) {
      for (const options of [CANON_ASCII, CANON_UTF8]) {
        expect(errorOf(canonicalText(nest(depth), options))?._tag).toBe('MaxDepthExceeded');
      }
    }
  });
});

describe('finding 5: __proto__ is a caller hazard, never a canon bug', () => {
  test('an object literal never gives canon the key, so canon must not emit it', () => {
    // Python's {'__proto__': 1, 'a': 2} -> {"__proto__":1,"a":2} (21 bytes).
    // The JS literal below has ONE own property; the other went to the
    // prototype setter (and was dropped, 1n not being an object).
    const literal = { __proto__: 1n, a: 2n };
    expect(Object.keys(literal)).toEqual(['a']);
    expect(Either.getOrThrow(canonicalText(literal, CANON_ASCII))).toBe('{"a":2}');
  });

  test('when the key is really an own property, canon matches CPython exactly', () => {
    const owned = Object.fromEntries([
      ['__defineGetter__', 3n],
      ['__proto__', 1n],
      ['a', 2n],
    ]) as CanonValue;
    // python3: {"__defineGetter__":3,"__proto__":1,"a":2}
    expect(Either.getOrThrow(canonicalText(owned, CANON_ASCII))).toBe(
      '{"__defineGetter__":3,"__proto__":1,"a":2}',
    );
    // `JSON.parse` also creates a real own `__proto__`.  Strings, because
    // `JSON.parse` yields `number` where the wire spelling here is a Python
    // `int` — a distinction canon takes seriously and this test does not.
    // oxlint-disable-next-line no-unsafe-type-assertion
    const parsed = JSON.parse('{"__proto__":"x","a":"y"}') as CanonRecord;
    expect(Object.keys(parsed)).toEqual(['__proto__', 'a']);
    expect(Either.getOrThrow(canonicalText(parsed, CANON_ASCII))).toBe(
      '{"__proto__":"x","a":"y"}',
    );
  });

  test('a null-prototype record encodes like any other', () => {
    // `Object.create(null)` is typed `any`; the assigned shape is the real one.
    // oxlint-disable-next-line no-unsafe-type-assertion
    const bare = Object.assign(Object.create(null) as object, { b: 1n, a: 2n }) as CanonRecord;
    expect(Either.getOrThrow(canonicalText(bare, CANON_ASCII))).toBe('{"a":2,"b":1}');
  });
});
