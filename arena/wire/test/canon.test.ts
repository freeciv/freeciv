/**
 * Canonical JSON — the acceptance suite for the byte-level foundation.
 *
 * The gate at the bottom is the one that matters: reproduce
 *
 *   sha256-3471520648d923f16fda4e1b58858301f343a64165b7e6cd2e3dd93af79cd3f4
 *
 * from TypeScript values alone.  Everything above it explains what would break
 * on the way there, and the differential suite re-derives the answers from a
 * live CPython instead of trusting this file's transcription of them.
 *
 * Every non-ASCII character in this file's string literals is written as an
 * escape on purpose: a canonicalization suite whose own source encoding could
 * drift would be testing the wrong thing.  The Python side is built from code
 * points too, never from text this file wrote out.
 */
import { describe, expect, test } from 'bun:test';
import { Either, Option } from 'effect';
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
  compareCodePoints,
  encodeString,
  encodeUtf8,
  formatCanonPath,
  formatFloat,
} from 'src/index';
import { NATIVE_SCHEMA_CANONICAL } from 'test/native-schema-fixture';

/** The oracle constant, from `agent_eval.v2_control`. */
const ORACLE_SCHEMA_ID =
  'sha256-3471520648d923f16fda4e1b58858301f343a64165b7e6cd2e3dd93af79cd3f4';

const FNV_OFFSET_BASIS = 14695981039346656037n;

const REPO_ROOT = `${import.meta.dir}/../../..`;

const decoder = new TextDecoder();
const encoder = new TextEncoder();

/** Failures surface as a visible mismatch rather than a thrown assertion. */
const textOf = (value: CanonValue, options: CanonOptions = CANON_ASCII): string =>
  Either.getOrElse(canonicalText(value, options), (error) => `<${error._tag}: ${error.message}>`);

const bytesOf = (value: CanonValue, options: CanonOptions = CANON_ASCII): Uint8Array =>
  Either.getOrElse(canonicalBytes(value, options), (error) => encoder.encode(`<${error._tag}>`));

const errorOf = <A>(either: Either.Either<A, CanonError>): CanonError | null =>
  Either.getOrElse(Either.flip(either), () => null);

const sha256Hex = (bytes: Uint8Array): string =>
  new Bun.CryptoHasher('sha256').update(bytes).digest('hex');

// ---------------------------------------------------------------------------
// The oracle
// ---------------------------------------------------------------------------

interface PythonRun {
  readonly ok: boolean;
  readonly stdout: Uint8Array;
  readonly stderr: string;
}

const python = (source: string, input = ''): PythonRun => {
  const run = Bun.spawnSync(['python3', '-c', source], {
    cwd: REPO_ROOT,
    stdin: encoder.encode(input),
  });
  return {
    ok: run.success,
    stdout: new Uint8Array(run.stdout),
    stderr: decoder.decode(run.stderr),
  };
};

/** CPython's answer, or a marker string that will fail whatever compares it. */
const pythonText = (source: string, input = ''): string => {
  const run = python(source, input);
  return run.ok ? decoder.decode(run.stdout) : `<python3 failed: ${run.stderr.trim()}>`;
};

const pythonAvailable = pythonText('print("ok")').trim() === 'ok';

const dumpsSource = (expression: string, options: CanonOptions): string =>
  [
    'import json,sys,struct',
    'data = sys.stdin.read()',
    `value = ${expression}`,
    `text = json.dumps(value, ensure_ascii=${options.ensureAscii ? 'True' : 'False'},`,
    '  allow_nan=False, sort_keys=True, separators=(",",":"))',
    `sys.stdout.buffer.write(text.encode(${options.ensureAscii ? '"ascii"' : '"utf-8"'}))`,
  ].join('\n');

/**
 * `json.dumps(<expr>, ...).encode(...)`, decoded back for a readable diff.
 * Both sides are valid UTF-8, so equal text means equal bytes.
 */
const pythonDumps = (expression: string, options: CanonOptions, input = ''): string =>
  pythonText(dumpsSource(expression, options), input);

// ---------------------------------------------------------------------------
// Deterministic pseudo-random inputs (splitmix64 keyed by index — no state)
// ---------------------------------------------------------------------------

const MASK64 = 0xffff_ffff_ffff_ffffn;

const splitmix64 = (seed: bigint): bigint => {
  const a = (seed + 0x9e3779b97f4a7c15n) & MASK64;
  const b = ((a ^ (a >> 30n)) * 0xbf58476d1ce4e5b9n) & MASK64;
  const c = ((b ^ (b >> 27n)) * 0x94d049bb133111ebn) & MASK64;
  return c ^ (c >> 31n);
};

const randomBits = (index: number, salt: bigint): bigint =>
  splitmix64((BigInt(index) * 0x2545f4914f6cdd1dn + salt) & MASK64);

const doubleFromBits = (bits: bigint): number => {
  const view = new DataView(new ArrayBuffer(8));
  view.setBigUint64(0, bits & MASK64);
  return view.getFloat64(0);
};

const hex64 = (bits: bigint): string => (bits & MASK64).toString(16).padStart(16, '0');

/** Random bit patterns that denote a finite double — NaN and the infinities dropped. */
const RANDOM_DOUBLE_BITS: readonly bigint[] = Array.from({ length: 2400 }, (_, index) =>
  randomBits(index, 0x5eedn),
).filter((bits) => Number.isFinite(doubleFromBits(bits)));

const randomCodePoint = (bits: bigint): number => {
  const raw = Number(bits % 0x110000n);
  // Surrogates are not characters; a lone one is tested explicitly elsewhere.
  return raw >= 0xd800 && raw <= 0xdfff ? raw - 0x800 : raw;
};

const randomString = (index: number): string =>
  Array.from({ length: 1 + Number(randomBits(index, 0xa11cen) % 9n) }, (_, position) =>
    String.fromCodePoint(randomCodePoint(randomBits(index * 32 + position, 0xfeedn))),
  ).join('');

/** Random strings, plus the characters that sit on every escaping boundary. */
const SAMPLE_STRINGS: readonly string[] = [
  ...new Set([
    ...Array.from({ length: 320 }, (_, index) => randomString(index)),
    '',
    ' ',
    '~',
    '\u007f',
    '\u0080',
    '\u0000\u001f',
    '"\\/',
    '\b\f\n\r\t',
    'é',
    '\u00ad',
    '\u2028\u2029',
    '\ufeff',
    '\ud7ff',
    '\uffff',
    '\ue000',
    '\u{10000}',
    '\u{1f600}',
    '\u{10ffff}',
    'A',
    'a',
    '0',
  ]),
];

const codePointsOf = (text: string): readonly number[] =>
  Array.from(text, (character) => character.codePointAt(0) ?? 0);

const SAMPLE_CODE_POINTS = JSON.stringify(SAMPLE_STRINGS.map(codePointsOf));

/** Rebuild the sample strings inside Python from code points, never from text. */
const PYTHON_SAMPLE_STRINGS = '["".join(chr(c) for c in row) for row in json.loads(data)]';

/**
 * Keys around the surrogate range.  Python `str` admits unpaired surrogates
 * and orders them by code point like anything else, so ASCII output is defined
 * for this document even though UTF-8 output cannot exist.
 */
const SURROGATE_KEY_DOCUMENT: CanonValue = {
  '\ud800': 1n,
  '\ue000': 2n,
  '\uffff': 3n,
  '\u{10000}': 4n,
  '\udfff': 5n,
  z: 6n,
};

const PYTHON_SURROGATE_KEY_DOCUMENT = String.raw`{"\ud800":1,"\ue000":2,"\uffff":3,"\U00010000":4,"\udfff":5,"z":6}`;

// ---------------------------------------------------------------------------
// 1. integers
// ---------------------------------------------------------------------------

describe('canon / bigint is Python int', () => {
  test('the FNV-1a-64 offset basis survives byte-exactly', () => {
    expect(textOf(FNV_OFFSET_BASIS)).toBe('14695981039346656037');
  });

  test('a JS number would have corrupted it', () => {
    expect(Number.isSafeInteger(Number('14695981039346656037'))).toBe(false);
    expect(FNV_OFFSET_BASIS - BigInt(Number('14695981039346656037'))).toBe(805n);
    expect(textOf(FNV_OFFSET_BASIS)).not.toBe(String(Number('14695981039346656037')));
  });

  test('the i64 and u64 rails round-trip', () => {
    expect(textOf([-9223372036854775808n, 9223372036854775807n, 18446744073709551615n])).toBe(
      '[-9223372036854775808,9223372036854775807,18446744073709551615]',
    );
  });

  test('bigint and number are different JSON spellings of one value', () => {
    expect(textOf(1n)).toBe('1');
    expect(textOf(1)).toBe('1.0');
    expect(textOf([0n, -0n, 0, -0])).toBe('[0,0,0.0,-0.0]');
  });

  test('an arbitrarily large integer is not truncated', () => {
    expect(textOf(10n ** 40n + 7n)).toBe('10000000000000000000000000000000000000007');
  });
});

// ---------------------------------------------------------------------------
// 2. floats
// ---------------------------------------------------------------------------

/** `[double, repr(x)]`.  Every repr also round-trips back to the same double. */
const FLOAT_CASES: ReadonlyArray<readonly [number, string]> = [
  [1.0, '1.0'],
  [-0.0, '-0.0'],
  [0.0, '0.0'],
  [0.1, '0.1'],
  [1 / 3, '0.3333333333333333'],
  [1e-4, '0.0001'],
  [1e-5, '1e-05'],
  [1.5e-7, '1.5e-07'],
  [1e15, '1000000000000000.0'],
  [1e16, '1e+16'],
  [1.25e16, '1.25e+16'],
  [1e100, '1e+100'],
  [-2.5e-300, '-2.5e-300'],
  [5e-324, '5e-324'],
  [2.2250738585072014e-308, '2.2250738585072014e-308'],
  [1.7976931348623157e308, '1.7976931348623157e+308'],
  [123456.789, '123456.789'],
  [-3.0, '-3.0'],
  [2 ** 53, '9007199254740992.0'],
  // Seventeen significant digits, still fixed notation: decpt is exactly 16.
  [1234567890123456.8, '1234567890123456.8'],
];

describe('canon / float formatting is repr(), not String()', () => {
  test.each(FLOAT_CASES)('formatFloat(%p) === %p', (value, expected) => {
    expect(Option.getOrNull(formatFloat(value))).toBe(expected);
    // ...and the spelling denotes the very double we started from.
    expect(Object.is(Number(expected), value)).toBe(true);
  });

  test('String() diverges on exactly the cases repr() reshapes', () => {
    expect(
      FLOAT_CASES.filter(([value, text]) => String(value) !== text).map(([, text]) => text),
    ).toEqual([
      '1.0',
      '-0.0',
      '0.0',
      '1e-05',
      '1.5e-07',
      '1000000000000000.0',
      '1e+16',
      '1.25e+16',
      '-3.0',
      '9007199254740992.0',
    ]);
  });

  test('the exponential threshold sits exactly where CPython puts it', () => {
    expect(textOf([1e16 - 2, 1e16, 1e-4, 1e-5])).toBe('[9999999999999998.0,1e+16,0.0001,1e-05]');
  });

  test('non-finite floats are error values, matching allow_nan=False', () => {
    expect(Option.isNone(formatFloat(Number.NaN))).toBe(true);
    expect(Either.isLeft(canonicalText(Number.NaN, CANON_ASCII))).toBe(true);
    expect(Either.isLeft(canonicalText(Number.POSITIVE_INFINITY, CANON_ASCII))).toBe(true);
    expect(Either.isLeft(canonicalText({ a: [Number.NEGATIVE_INFINITY] }, CANON_UTF8))).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// 3. unicode and surrogates
// ---------------------------------------------------------------------------

describe('canon / ensure_ascii=true', () => {
  test('escapes astral planes as surrogate pairs, and DEL with them', () => {
    expect(textOf('a\u{1f600}bé\u007f')).toBe('"a\\ud83d\\ude00b\\u00e9\\u007f"');
  });

  test('the short escapes are the ones CPython uses, not \\u000a', () => {
    expect(textOf('"\\/\b\f\n\r\t')).toBe('"\\"\\\\/\\b\\f\\n\\r\\t"');
  });

  test('C1 and the format characters are escaped too', () => {
    expect(textOf('\u0080\u00ad\u2028\u2029\ufeff')).toBe(
      '"\\u0080\\u00ad\\u2028\\u2029\\ufeff"',
    );
  });

  test('a lone surrogate is an escape, and the bytes stay ASCII', () => {
    expect(textOf('\ud800')).toBe('"\\ud800"');
    expect(textOf('\udfff')).toBe('"\\udfff"');
    expect(bytesOf('\ud800\ud800').every((byte) => byte < 0x80)).toBe(true);
  });
});

describe('canon / ensure_ascii=false', () => {
  test('keeps non-ASCII literal and escapes only the mandatory set', () => {
    expect(textOf('a\u{1f600}bé\u007f', CANON_UTF8)).toBe('"a\u{1f600}bé\u007f"');
    expect(textOf('q"\\\n\t\u0001', CANON_UTF8)).toBe('"q\\"\\\\\\n\\t\\u0001"');
  });

  test('the bytes are UTF-8, not escapes', () => {
    expect(Array.from(bytesOf('\u{1f600}', CANON_UTF8))).toEqual([34, 240, 159, 152, 128, 34]);
  });

  test('a lone surrogate is an error, never a silent U+FFFD', () => {
    const failure = errorOf(canonicalBytes(['ok', '\ud800'], CANON_UTF8));
    expect(failure?._tag).toBe('LoneSurrogate');
    // What the naive route would have produced instead: a replacement char.
    expect(Array.from(encoder.encode('\ud800'))).toEqual([239, 191, 189]);
  });

  test('a paired surrogate is fine', () => {
    expect(Either.isRight(canonicalBytes('\u{1f600}', CANON_UTF8))).toBe(true);
    expect(Either.isRight(canonicalBytes('😀', CANON_UTF8))).toBe(true);
  });

  test('encodeString and encodeUtf8 agree with the writer', () => {
    expect(Either.getOrNull(encodeString('é', CANON_UTF8))).toBe('"é"');
    expect(Either.getOrNull(encodeString('é', CANON_ASCII))).toBe('"\\u00e9"');
    expect(Either.isLeft(encodeUtf8('\udc00'))).toBe(true);
    expect(Array.from(Either.getOrElse(encodeUtf8('é'), () => new Uint8Array()))).toEqual([
      195, 169,
    ]);
  });
});

// ---------------------------------------------------------------------------
// 4. key order
// ---------------------------------------------------------------------------

describe('canon / sort_keys orders by code point', () => {
  test('JS default sort disagrees above U+E000', () => {
    expect(['\u{10000}', '\uffff'].toSorted()[0]).toBe('\u{10000}');
    expect(compareCodePoints('\uffff', '\u{10000}')).toBeLessThan(0);
  });

  test('an astral key therefore sorts last, not first', () => {
    expect(textOf({ '\u{10000}': 1n, '\uffff': 2n })).toBe('{"\\uffff":2,"\\ud800\\udc00":1}');
  });

  test('ASCII keys keep the obvious order', () => {
    expect(textOf({ b: 1n, A: 2n, a: 3n, '': 4n, '0': 5n })).toBe(
      '{"":4,"0":5,"A":2,"a":3,"b":1}',
    );
  });

  test('unpaired surrogate keys sort by their own code point', () => {
    expect(textOf(SURROGATE_KEY_DOCUMENT)).toBe(
      '{"z":6,"\\ud800":1,"\\udfff":5,"\\ue000":2,"\\uffff":3,"\\ud800\\udc00":4}',
    );
  });

  test('a prefix sorts before its extension', () => {
    expect(compareCodePoints('ab', 'abc')).toBeLessThan(0);
    expect(compareCodePoints('abc', 'ab')).toBeGreaterThan(0);
    expect(compareCodePoints('ab', 'ab')).toBe(0);
  });

  test('the comparator is a total order on the sample', () => {
    const sorted = [...SAMPLE_STRINGS].toSorted(compareCodePoints);
    expect(sorted.length).toBe(SAMPLE_STRINGS.length);
    expect(
      sorted.filter(
        (value, index) => index > 0 && compareCodePoints(sorted[index - 1] ?? '', value) > 0,
      ),
    ).toEqual([]);
  });
});

// ---------------------------------------------------------------------------
// 5. shape, and refusals as values
// ---------------------------------------------------------------------------

describe('canon / separators and containers', () => {
  test('compact separators, no whitespace anywhere', () => {
    expect(textOf({ b: 1n, a: [1n, 2n], c: {} })).toBe('{"a":[1,2],"b":1,"c":{}}');
  });

  test('empty containers and nesting', () => {
    expect(textOf([[[[1n]]], [], {}, [{}], { a: {} }])).toBe('[[[[1]]],[],{},[{}],{"a":{}}]');
  });

  test('the scalar spellings', () => {
    expect(textOf([null, true, false])).toBe('[null,true,false]');
  });
});

/** `depth` nested single-element arrays around `1`. */
const nest = (depth: number): CanonValue =>
  Array.from({ length: depth }).reduce<CanonValue>((inner) => [inner], 1n);

describe('canon / refusals are values, never throws', () => {
  test('the error names the path of the offending node', () => {
    const error = errorOf(canonicalText({ a: [{ b: Number.NaN }] }, CANON_ASCII));
    expect(error?._tag).toBe('NonFiniteFloat');
    expect(error?.path).toEqual(['a', 0, 'b']);
    expect(formatCanonPath(error?.path ?? [])).toBe('$["a"][0]["b"]');
    expect(error?.message).toContain('NaN');
  });

  test('a lone surrogate in a key is reported at that key', () => {
    const error = errorOf(canonicalText({ ok: { '\ud800x': 1n } }, CANON_UTF8));
    expect(error?._tag).toBe('LoneSurrogate');
    expect(error?.path).toEqual(['ok', '\ud800x']);
  });

  test('nesting up to the limit encodes, beyond it is an error value', () => {
    expect(textOf(nest(CANON_MAX_DEPTH))).toContain('[[[');
    const error = errorOf(canonicalText(nest(CANON_MAX_DEPTH + 1), CANON_ASCII));
    expect(error?._tag).toBe('MaxDepthExceeded');
    expect(error?.path.length).toBe(CANON_MAX_DEPTH);
  });

  test('a value outside the model is rejected rather than mis-encoded', () => {
    // Untyped input really can carry `undefined`; this is the one place the
    // suite fabricates a value the types say cannot exist, because refusing it
    // is precisely what the writer is being tested for.
    // oxlint-disable-next-line no-unsafe-type-assertion
    const rogue = { a: undefined } as unknown as CanonValue;
    const error = errorOf(canonicalText(rogue, CANON_ASCII));
    expect(error?._tag).toBe('UnsupportedValue');
    expect(error?.path).toEqual(['a']);
  });

  test('the presets are the two modes and nothing else', () => {
    expect(CANON_ASCII.ensureAscii).toBe(true);
    expect(CANON_UTF8.ensureAscii).toBe(false);
  });
});

// ---------------------------------------------------------------------------
// 6. differential against a live CPython
// ---------------------------------------------------------------------------

/** `[name, TS value, the equivalent Python expression]`. */
const PARITY_DOCUMENTS: ReadonlyArray<readonly [string, CanonValue, string]> = [
  ['the bare offset basis', FNV_OFFSET_BASIS, '14695981039346656037'],
  [
    'mixed nesting with an empty key',
    { z: [1n, true, null, 'x'], a: { '': [], 'é': -0.5 }, m: false },
    String.raw`{"z":[1,True,None,"x"],"a":{"":[],"é":-0.5},"m":False}`,
  ],
  [
    'control characters, quotes, solidus, DEL',
    ['\u0000\u001f"\\\b\f\n\r\t', '/', '\u007f'],
    String.raw`["\u0000\u001f\"\\\b\f\n\r\t", "/", "\u007f"]`,
  ],
  [
    'astral and BMP keys sorted together',
    { '\u{1f600}': 'é', 'ü': 1n, A: 2n, a: 3n, '\u007f': 4n },
    String.raw`{"\U0001f600":"é","ü":1,"A":2,"a":3,"\u007f":4}`,
  ],
  [
    'floats beside integers',
    [0.0, -0.0, 1e16, 1e-5, 1n, -1n, 2.5, 1e15],
    '[0.0, -0.0, 1e16, 1e-5, 1, -1, 2.5, 1e15]',
  ],
  ['deep and empty containers', [[[[[1n]]]], [], {}, [{}]], '[[[[[1]]]], [], {}, [{}]]'],
  [
    'the u64 and i64 rails',
    [18446744073709551615n, -9223372036854775808n],
    '[18446744073709551615, -9223372036854775808]',
  ],
  [
    'keys that differ only across the BMP boundary',
    { '\uffff': 1n, '\u{10000}': 2n, '\ue000': 3n, '\ud7ff': 4n },
    String.raw`{"\uffff":1,"\U00010000":2,"\ue000":3,"\ud7ff":4}`,
  ],
  [
    'strings that are only invisible characters',
    ['\u0301', '\u200b', '\u3000', '\ufeff'],
    String.raw`["\u0301", "\u200b", "\u3000", "\ufeff"]`,
  ],
];

describe('canon / differential against python3', () => {
  test('python3 is present — ungated, so a missing oracle fails instead of skipping', () => {
    expect(pythonAvailable).toBe(true);
  });

  test('the oracle is really being consulted', () => {
    // Guards every check below from passing because python3 silently no-ops.
    expect(pythonDumps('{"b":1,"a":2}', CANON_ASCII)).toBe('{"a":2,"b":1}');
    expect(pythonDumps(String.raw`"é"`, CANON_ASCII)).toBe('"\\u00e9"');
    expect(pythonDumps(String.raw`"é"`, CANON_UTF8)).toBe('"é"');
  });

  test('every tricky document matches json.dumps bytes in both modes', () => {
    const mismatches = PARITY_DOCUMENTS.flatMap(([name, document, expression]) =>
      [CANON_ASCII, CANON_UTF8].flatMap((options) => {
        const mine = textOf(document, options);
        const theirs = pythonDumps(expression, options);
        return mine === theirs
          ? []
          : [`${name} (ensureAscii=${options.ensureAscii}): ${mine} != ${theirs}`];
      }),
    );
    expect(mismatches).toEqual([]);
  });

  test('two thousand random doubles all print the way repr() does', () => {
    expect(RANDOM_DOUBLE_BITS.length).toBeGreaterThan(2000);
    const mine = textOf(RANDOM_DOUBLE_BITS.map(doubleFromBits));
    const theirs = pythonDumps(
      '[struct.unpack(">d", bytes.fromhex(h))[0] for h in json.loads(data)]',
      CANON_ASCII,
      JSON.stringify(RANDOM_DOUBLE_BITS.map(hex64)),
    );
    expect(mine).toBe(theirs);
  });

  test('random strings escape identically in both modes', () => {
    const mismatches = [CANON_ASCII, CANON_UTF8].flatMap((options) => {
      const mine = textOf(SAMPLE_STRINGS, options);
      const theirs = pythonDumps(PYTHON_SAMPLE_STRINGS, options, SAMPLE_CODE_POINTS);
      return mine === theirs ? [] : [`ensureAscii=${options.ensureAscii}: ${mine} != ${theirs}`];
    });
    expect(mismatches).toEqual([]);
  });

  test('random keys sort identically to python sorted()', () => {
    const document: CanonRecord = Object.fromEntries(
      SAMPLE_STRINGS.map((key, index) => [key, BigInt(index)]),
    );
    const expression =
      '{k: i for i, k in enumerate("".join(chr(c) for c in row) for row in json.loads(data))}';
    const mismatches = [CANON_ASCII, CANON_UTF8].flatMap((options) => {
      const mine = textOf(document, options);
      const theirs = pythonDumps(expression, options, SAMPLE_CODE_POINTS);
      return mine === theirs ? [] : [`ensureAscii=${options.ensureAscii} diverged`];
    });
    expect(mismatches).toEqual([]);
  });

  test('surrogate keys match in ascii mode, and have no utf-8 encoding at all', () => {
    expect(textOf(SURROGATE_KEY_DOCUMENT)).toBe(
      pythonDumps(PYTHON_SURROGATE_KEY_DOCUMENT, CANON_ASCII),
    );
    // The same document in the other mode: an error here, an exception there.
    expect(Either.isLeft(canonicalBytes(SURROGATE_KEY_DOCUMENT, CANON_UTF8))).toBe(true);
    expect(pythonDumps(PYTHON_SURROGATE_KEY_DOCUMENT, CANON_UTF8)).toContain('python3 failed');
  });

  test('str.encode("utf-8") really does refuse the lone surrogate we refuse', () => {
    const refused = pythonText(
      [
        'import sys',
        'try:',
        '    "\\ud800".encode("utf-8")',
        '    sys.stdout.write("encoded")',
        'except UnicodeEncodeError:',
        '    sys.stdout.write("refused")',
      ].join('\n'),
    );
    expect(refused).toBe('refused');
    expect(Either.isLeft(canonicalBytes('\ud800', CANON_UTF8))).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// THE GATE
// ---------------------------------------------------------------------------

const isRecord = (value: CanonValue | undefined): value is CanonRecord =>
  typeof value === 'object' && value !== null && !Array.isArray(value);

const field = (value: CanonValue | undefined, key: string): CanonValue | undefined =>
  isRecord(value) ? value[key] : undefined;

describe('canon / THE GATE: NATIVE_OBSERVATION_ACTION_SCHEMA_ID', () => {
  test('the hashed structure carries the > 2^53 offset basis as an integer', () => {
    expect(field(field(NATIVE_SCHEMA_CANONICAL, 'research_choices_digest'), 'offset_basis')).toBe(
      FNV_OFFSET_BASIS,
    );
    expect(field(field(NATIVE_SCHEMA_CANONICAL, 'treaty_clauses_digest'), 'offset_basis')).toBe(
      FNV_OFFSET_BASIS,
    );
  });

  test('the canonical bytes are pure ASCII and carry the literal basis', () => {
    const bytes = bytesOf(NATIVE_SCHEMA_CANONICAL);
    expect(bytes.every((byte) => byte < 0x80)).toBe(true);
    expect(decoder.decode(bytes)).toContain('"offset_basis":14695981039346656037');
    expect(bytes.byteLength).toBe(50038);
  });

  test('sha256 of those bytes IS the oracle schema id', () => {
    expect(`sha256-${sha256Hex(bytesOf(NATIVE_SCHEMA_CANONICAL))}`).toBe(ORACLE_SCHEMA_ID);
  });

  test('the naive JS route does not reach this digest by accident', () => {
    const naive = JSON.stringify(NATIVE_SCHEMA_CANONICAL, (_key, value: unknown) =>
      typeof value === 'bigint' ? Number(value) : value,
    );
    expect(naive).not.toContain('14695981039346656037');
    expect(`sha256-${sha256Hex(encoder.encode(naive))}`).not.toBe(ORACLE_SCHEMA_ID);
  });

  test('the whole document matches v2_control byte for byte', () => {
    const theirs = pythonText(
      [
        'import json,sys,inspect',
        'from agent_eval import v2_control as V',
        'src = inspect.getsource(V._derive_native_schema_id)',
        'body = src.split(chr(34)*3 + chr(10), 2)[-1].rsplit("    encoded = json.dumps", 1)[0]',
        'ns = dict(vars(V))',
        'exec("if True:\\n" + body, ns)',
        'sys.stdout.buffer.write(json.dumps(ns["canonical"], ensure_ascii=True,',
        '  allow_nan=False, sort_keys=True, separators=(",",":")).encode("ascii"))',
      ].join('\n'),
    );
    expect(decoder.decode(bytesOf(NATIVE_SCHEMA_CANONICAL))).toBe(theirs);
  });

  test('the oracle constant is still what v2_control exports today', () => {
    const exported = pythonText(
      'from agent_eval.v2_control import NATIVE_OBSERVATION_ACTION_SCHEMA_ID as s; print(s)',
    );
    expect(exported.trim()).toBe(ORACLE_SCHEMA_ID);
  });
});
