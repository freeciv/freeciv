/**
 * Canonical JSON — fixtures from the `structure-ints` adversarial hunt.
 *
 * Every expectation below was produced by a live CPython 3.14 and transcribed
 * here, not derived from this package.  The hunt drove 156 documents through
 * both `json.dumps` modes and compared 310 (document, mode) pairs byte for
 * byte; these are the ~20 that would have caught something real had the writer
 * been wrong, kept as regression pins so a future refactor cannot quietly lose
 * them.  The oracle was
 *
 * ```python
 * json.dumps(value, ensure_ascii=?, allow_nan=False,
 *            sort_keys=True, separators=(",", ":")).encode(?)
 * ```
 *
 * Two families dominate:
 *
 *   - **integers.**  A Python `int` has no width, so every rail a `double`
 *     would round off — 2^53, 2^63, 2^64, 10^100, 2^1000 — has to survive as a
 *     `bigint`, and it has to stay distinguishable from a `float` that prints
 *     the same magnitude.  `{"f":1e+21,"i":1000000000000000000000}` is the
 *     compact statement of that.
 *   - **key order.**  JavaScript hands out object keys with integer-like ones
 *     first in numeric order, and compares strings by UTF-16 code unit.  Python
 *     does neither.  `sort_keys=True` has to erase the first difference and
 *     `compareCodePoints` the second; the astral and lone-surrogate documents
 *     below are the ones where a naive `sort()` diverges.
 *
 * Non-ASCII characters are written as escapes throughout, so the file's own
 * encoding cannot influence what is being asserted.
 */
import { describe, expect, test } from 'bun:test';
import { Either } from 'effect';
import {
  CANON_ASCII,
  CANON_UTF8,
  type CanonError,
  type CanonOptions,
  type CanonValue,
  canonicalBytes,
  canonicalText,
  compareCodePoints,
} from 'src/index';

/** Failures surface as a visible mismatch rather than a thrown assertion. */
const textOf = (value: CanonValue, options: CanonOptions): string =>
  Either.getOrElse(
    canonicalText(value, options),
    (error) => `<${error._tag}: ${error.message}>`,
  );

const sha256Of = (value: CanonValue, options: CanonOptions): string =>
  Either.getOrElse(
    Either.map(canonicalBytes(value, options), (bytes) =>
      new Bun.CryptoHasher('sha256').update(bytes).digest('hex'),
    ),
    (error) => `<${error._tag}>`,
  );

const byteLengthOf = (value: CanonValue, options: CanonOptions): number =>
  Either.getOrElse(
    Either.map(canonicalBytes(value, options), (bytes) => bytes.length),
    () => -1,
  );

const errorOf = <A>(either: Either.Either<A, CanonError>): CanonError | null =>
  Either.getOrElse(Either.flip(either), () => null);

const record = (entries: ReadonlyArray<readonly [string, CanonValue]>): CanonValue =>
  Object.fromEntries(entries);

/** `n` nested arrays with `[]` at the bottom, built without recursion. */
const nestedArrays = (depth: number): CanonValue =>
  Array.from({ length: depth - 1 }).reduce<CanonValue>((node) => [node], []);

// ---------------------------------------------------------------------------
// 1. integers no double can hold
// ---------------------------------------------------------------------------

describe('canon hunt / integers at the rails', () => {
  test('the four values either side of 2^64 keep all twenty digits', () => {
    expect(
      textOf(
        [
          18446744073709551613n,
          18446744073709551614n,
          18446744073709551615n,
          18446744073709551616n,
        ],
        CANON_ASCII,
      ),
    ).toBe(
      '[18446744073709551613,18446744073709551614,18446744073709551615,18446744073709551616]',
    );
  });

  test('2^53+1 as an int and 2^53 as a float coexist in one object', () => {
    expect(
      textOf(record([['i', 9007199254740993n], ['f', 9007199254740992]]), CANON_ASCII),
    ).toBe('{"f":9007199254740992.0,"i":9007199254740993}');
  });

  test('1 and 1.0 are different documents; every zero is "0" or "0.0"', () => {
    expect(textOf([1n, 1], CANON_ASCII)).toBe('[1,1.0]');
    expect(textOf([0n, -0n, BigInt('-0')], CANON_ASCII)).toBe('[0,0,0]');
  });

  test('2^1000 prints all 302 digits', () => {
    expect(textOf(2n ** 1000n, CANON_ASCII)).toBe(
      '10715086071862673209484250490600018105614048117055336074437503883703510511249361' +
        '22493198378815695858127594672917553146825187145285692314043598457757469857480393' +
        '45677748242309854210746050623711418779541821530464749835819412673987675591655439' +
        '46077062914571196477686542167660429831652624386837205668069376',
    );
  });

  test('1e21 as a float and as an int are unrelated spellings', () => {
    expect(textOf(record([['i', 10n ** 21n], ['f', 1e21]]), CANON_ASCII)).toBe(
      '{"f":1e+21,"i":1000000000000000000000}',
    );
  });

  test('a mixed int/float object hits every float layout at once', () => {
    expect(
      textOf(
        record([
          ['a', 1n],
          ['b', 1],
          ['c', -0.0],
          ['d', 1e16],
          ['e', 1e-5],
          ['f', 0n],
          ['g', 0],
        ]),
        CANON_ASCII,
      ),
    ).toBe('{"a":1,"b":1.0,"c":-0.0,"d":1e+16,"e":1e-05,"f":0,"g":0.0}');
  });

  test('the fixed/exponential switch sits between decpt 16 and 17', () => {
    expect(
      // The last is 12345678901234567 rounded to the nearest double; written as
      // its own repr because the integer literal is not representable.
      textOf([1e15, 1e16, 1e17, 1234567890123456.0, 1.2345678901234568e16], CANON_ASCII),
    ).toBe('[1000000000000000.0,1e+16,1e+17,1234567890123456.0,1.2345678901234568e+16]');
  });
});

// ---------------------------------------------------------------------------
// 2. empty containers
// ---------------------------------------------------------------------------

describe('canon hunt / empty containers', () => {
  test('every arrangement of nothing', () => {
    expect(
      textOf(
        [[], {}, [[]], [{}], record([['a', {}]]), record([['a', []]])],
        CANON_ASCII,
      ),
    ).toBe('[[],{},[[]],[{}],{"a":{}},{"a":[]}]');
  });

  test('the empty string is a key like any other', () => {
    expect(textOf(record([['', record([['', record([['', []]])]])]]), CANON_ASCII)).toBe(
      '{"":{"":{"":[]}}}',
    );
  });
});

// ---------------------------------------------------------------------------
// 3. key order JavaScript would get wrong
// ---------------------------------------------------------------------------

describe('canon hunt / key order', () => {
  test('integer-like keys come back in string order, not insertion order', () => {
    // `Object.keys` yields 0, 1, 2, 10 first, ascending numerically, then b, a
    // in insertion order.  Python sorts the strings.
    expect(
      textOf(
        record([
          ['b', 1n],
          ['2', 2n],
          ['1', 3n],
          ['a', 4n],
          ['10', 5n],
          ['01', 6n],
          ['0', 7n],
        ]),
        CANON_ASCII,
      ),
    ).toBe('{"0":7,"01":6,"1":3,"10":5,"2":2,"a":4,"b":1}');
  });

  test('keys past the array-index limit are still just strings', () => {
    expect(
      textOf(
        record([
          ['4294967294', 1n],
          ['4294967295', 2n],
          ['4294967296', 3n],
          ['9007199254740993', 4n],
        ]),
        CANON_ASCII,
      ),
    ).toBe('{"4294967294":1,"4294967295":2,"4294967296":3,"9007199254740993":4}');
  });

  test('numeric-looking keys sort lexicographically, u64 rails included', () => {
    expect(
      textOf(
        record([
          ['18446744073709551615', 1n],
          ['18446744073709551616', 2n],
          ['9007199254740993', 3n],
          ['007', 4n],
          ['0', 5n],
          ['-1', 6n],
        ]),
        CANON_ASCII,
      ),
    ).toBe(
      '{"-1":6,"0":5,"007":4,"18446744073709551615":1,"18446744073709551616":2,"9007199254740993":3}',
    );
  });

  test('the printable ASCII spread, empty key first', () => {
    expect(
      textOf(
        record([
          ['a', 1n],
          ['A', 2n],
          ['0', 3n],
          ['_', 4n],
          ['~', 5n],
          [' ', 6n],
          ['', 7n],
          ['!', 8n],
          ['{', 9n],
          ['[', 10n],
        ]),
        CANON_ASCII,
      ),
    ).toBe('{"":7," ":6,"!":8,"0":3,"A":2,"[":10,"_":4,"a":1,"{":9,"~":5}');
  });

  test('a prefix sorts before its extensions, DEL included', () => {
    const value = record([
      ['a', 1n],
      ['ab', 2n],
      ['abc', 3n],
      ['a\u0000', 4n],
      ['a\u007f', 5n],
      ['a~', 6n],
    ]);
    expect(textOf(value, CANON_ASCII)).toBe(
      '{"a":1,"a\\u0000":4,"ab":2,"abc":3,"a~":6,"a\\u007f":5}',
    );
    // DEL is escaped only under ensure_ascii; UTF-8 keeps the raw byte.
    expect(textOf(value, CANON_UTF8)).toBe(
      '{"a":1,"a\\u0000":4,"ab":2,"abc":3,"a~":6,"a\u007f":5}',
    );
  });

  test('`__proto__` is an ordinary own key when the record has one', () => {
    expect(
      textOf(
        record([['__proto__', 1n], ['a', 2n], ['__defineGetter__', 3n]]),
        CANON_ASCII,
      ),
    ).toBe('{"__defineGetter__":3,"__proto__":1,"a":2}');
  });

  test('a null-prototype record encodes like any other', () => {
    const bare: CanonValue = { b: 1n, a: 2n };
    Object.setPrototypeOf(bare, null);
    expect(Object.getPrototypeOf(bare)).toBeNull();
    expect(textOf(bare, CANON_ASCII)).toBe('{"a":2,"b":1}');
  });
});

// ---------------------------------------------------------------------------
// 4. key order above U+D800, where UTF-16 and code points disagree
// ---------------------------------------------------------------------------

describe('canon hunt / astral and surrogate key order', () => {
  test('U+FFFF sorts before U+10000, the opposite of JavaScript', () => {
    const value = record([
      ['\ud7ff', 1n],
      ['\ue000', 2n],
      ['\uffff', 3n],
      ['\u{10000}', 4n],
      ['\u{10ffff}', 5n],
      ['\ufffd', 6n],
    ]);
    expect(textOf(value, CANON_ASCII)).toBe(
      '{"\\ud7ff":1,"\\ue000":2,"\\ufffd":6,"\\uffff":3,"\\ud800\\udc00":4,"\\udbff\\udfff":5}',
    );
    expect(textOf(value, CANON_UTF8)).toBe(
      '{"\ud7ff":1,"\ue000":2,"\ufffd":6,"\uffff":3,"\u{10000}":4,"\u{10ffff}":5}',
    );
    // The trap, stated: by code unit U+10000 looks smaller, by code point it is
    // larger, and only the second answer is Python's.
    const bmpMax = '\uffff';
    const astralMin = '\u{10000}';
    expect(bmpMax < astralMin).toBe(false);
    expect(compareCodePoints(bmpMax, astralMin)).toBeLessThan(0);
  });

  test('an astral prefix beats a longer all-U+FFFF key', () => {
    const value = record([
      ['\u{10000}', 1n],
      ['\u{10000} ', 2n],
      ['\uffff\uffff\uffff', 3n],
      ['\u{10ffff}', 4n],
      ['\u{effff}', 5n],
    ]);
    expect(textOf(value, CANON_ASCII)).toBe(
      '{"\\uffff\\uffff\\uffff":3,"\\ud800\\udc00":1,"\\ud800\\udc00 ":2,"\\udb7f\\udfff":5,"\\udbff\\udfff":4}',
    );
    expect(textOf(value, CANON_UTF8)).toBe(
      '{"\uffff\uffff\uffff":3,"\u{10000}":1,"\u{10000} ":2,"\u{effff}":5,"\u{10ffff}":4}',
    );
  });

  test('two lone surrogates sort below the pair they would have formed', () => {
    const value = record([
      ['\ud800\ud800', 1n],
      ['\u{10000}', 2n],
      ['\ud800z', 3n],
      ['\udc00\ud800', 4n],
    ]);
    // Python reads these as [D800,D800], [10000], [D800,7A], [DC00,D800].
    expect(textOf(value, CANON_ASCII)).toBe(
      '{"\\ud800z":3,"\\ud800\\ud800":1,"\\udc00\\ud800":4,"\\ud800\\udc00":2}',
    );
    // `str.encode("utf-8")` raises on the unpaired halves.
    expect(errorOf(canonicalText(value, CANON_UTF8))?._tag).toBe('LoneSurrogate');
  });

  test('the whole surrogate block sorts between U+D7FF and U+E000', () => {
    const value = record([
      ['\ud7ff', 1n],
      ['\ud800', 2n],
      ['\udbff', 3n],
      ['\udc00', 4n],
      ['\udfff', 5n],
      ['\ue000', 6n],
    ]);
    expect(textOf(value, CANON_ASCII)).toBe(
      '{"\\ud7ff":1,"\\ud800":2,"\\udbff":3,"\\udc00":4,"\\udfff":5,"\\ue000":6}',
    );
    expect(errorOf(canonicalBytes(value, CANON_UTF8))?._tag).toBe('LoneSurrogate');
  });

  test('a combining mark, a precomposed char and two emoji sort by code point', () => {
    const value = record([
      ['\u{1f600}', 1n],
      ['\u{1f3f4}\u{e0067}', 2n],
      ['\u00e9', 3n],
      ['e\u0301', 4n],
      ['\u0301', 5n],
    ]);
    expect(textOf(value, CANON_ASCII)).toBe(
      '{"e\\u0301":4,"\\u00e9":3,"\\u0301":5,"\\ud83c\\udff4\\udb40\\udc67":2,"\\ud83d\\ude00":1}',
    );
    expect(textOf(value, CANON_UTF8)).toBe(
      '{"e\u0301":4,"\u00e9":3,"\u0301":5,"\u{1f3f4}\u{e0067}":2,"\u{1f600}":1}',
    );
  });

  test('DEL and Latin-1 escape under ensure_ascii and stay literal without it', () => {
    const value = record([
      ['~', 1n],
      ['\u007f', 2n],
      ['\u0080', 3n],
      ['\u00ff', 4n],
      ['\u00e9', 5n],
      ['e\u0301', 6n],
    ]);
    expect(textOf(value, CANON_ASCII)).toBe(
      '{"e\\u0301":6,"~":1,"\\u007f":2,"\\u0080":3,"\\u00e9":5,"\\u00ff":4}',
    );
    expect(byteLengthOf(value, CANON_ASCII)).toBe(63);
    expect(byteLengthOf(value, CANON_UTF8)).toBe(42);
  });
});

// ---------------------------------------------------------------------------
// 5. shape
// ---------------------------------------------------------------------------

describe('canon hunt / shape', () => {
  test('a subtree referenced twice is written twice', () => {
    const shared: CanonValue = record([['a', 1n]]);
    expect(textOf([shared, shared], CANON_ASCII)).toBe('[{"a":1},{"a":1}]');
  });

  test('nesting right up to the depth rail still encodes', () => {
    expect(textOf(nestedArrays(1000), CANON_ASCII)).toBe(
      `${'['.repeat(1000)}${']'.repeat(1000)}`,
    );
    expect(byteLengthOf(nestedArrays(1000), CANON_ASCII)).toBe(2000);
  });
});

// ---------------------------------------------------------------------------
// 6. scale — pinned by digest, since the text runs to hundreds of kilobytes
// ---------------------------------------------------------------------------

describe('canon hunt / documents too large to read', () => {
  test('100k integers in one array', () => {
    const value = Array.from({ length: 100000 }, (_, index) => BigInt(index));
    expect(byteLengthOf(value, CANON_ASCII)).toBe(588891);
    expect(sha256Of(value, CANON_ASCII)).toBe(
      'ef440f29f9463eac65fda8b2e1214628852802516a2b06ae1a1b020743b78a20',
    );
  });

  test('10k numeric-string keys, where insertion order is most misleading', () => {
    const value = Object.fromEntries(
      Array.from({ length: 10000 }, (_, index) => [String(index), BigInt(index)]),
    );
    expect(byteLengthOf(value, CANON_ASCII)).toBe(117781);
    expect(sha256Of(value, CANON_ASCII)).toBe(
      '24e6d43f5c36d712c999f67268db18a7eac04f9aa8e5c3d83502b29beb1ed1bd',
    );
  });

  test('2k astral keys, which is 2k comparisons the naive sort gets wrong', () => {
    const value = Object.fromEntries(
      Array.from({ length: 2000 }, (_, index) => [
        String.fromCodePoint(0x10000 + index * 37),
        BigInt(index),
      ]),
    );
    expect(sha256Of(value, CANON_ASCII)).toBe(
      'e501437135153477575a4043a89fcdb4f6e3e0b0940ae30f4faa451cd3773455',
    );
    expect(sha256Of(value, CANON_UTF8)).toBe(
      '3f5071508304d6690dff07ef01c249047e711212197eba8f6d2d70f2498539af',
    );
    expect(byteLengthOf(value, CANON_ASCII)).toBe(38891);
    expect(byteLengthOf(value, CANON_UTF8)).toBe(22891);
  });
});
