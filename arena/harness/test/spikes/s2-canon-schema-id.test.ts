/**
 * Spike S2 — canonical JSON and schema-id byte parity.
 *
 * The native client refuses to talk to a control layer whose
 * `NATIVE_OBSERVATION_ACTION_SCHEMA_ID` differs, and that id is a sha256 over
 * CPython's `json.dumps(..., ensure_ascii=True, allow_nan=False,
 * sort_keys=True, separators=(",",":"))` bytes of a structure containing
 * 14695981039346656037.  So the port needs a canonical writer that is
 * byte-identical to CPython, not merely semantically equivalent.  The gate at
 * the bottom of this file is the one that matters: reproduce
 *
 *   sha256-3471520648d923f16fda4e1b58858301f343a64165b7e6cd2e3dd93af79cd3f4
 *
 * from TypeScript values alone.  The tests above it explain what would break.
 *
 * Control characters below are written as escapes on purpose: a
 * canonicalization test whose own source encoding could drift would be
 * testing the wrong thing.
 */
import { describe, expect, test } from 'bun:test';
import { Either } from 'effect';
import {
  type CanonValue,
  canonicalBytes,
  canonicalSha256,
  canonicalText,
  compareCodePoints,
  formatFloat,
} from './s2-canon-impl.ts';
import { NATIVE_SCHEMA_CANONICAL } from './s2-native-schema-fixture.ts';

/** The oracle, from `agent_eval.v2_control`. */
const ORACLE_SCHEMA_ID =
  'sha256-3471520648d923f16fda4e1b58858301f343a64165b7e6cd2e3dd93af79cd3f4';

const FNV_OFFSET_BASIS = 14695981039346656037n;

const REPO_ROOT = `${import.meta.dir}/../../../..`;

const decoder = new TextDecoder();

const textOf = (value: CanonValue, ensureAscii = true): string =>
  Either.getOrThrowWith(canonicalText(value, ensureAscii), (error) => new Error(error._tag));

const bytesOf = (value: CanonValue, ensureAscii = true): Uint8Array =>
  Either.getOrThrowWith(canonicalBytes(value, ensureAscii), (error) => new Error(error._tag));

/** CPython, spawned as the oracle.  `null` when it is unavailable or errored. */
const python = (source: string): Uint8Array | null => {
  const run = Bun.spawnSync(['python3', '-c', source], { cwd: REPO_ROOT });
  return run.success ? new Uint8Array(run.stdout) : null;
};

const pythonAvailable = decoder.decode(python('print("ok")') ?? new Uint8Array()).trim() === 'ok';

/** `json.dumps(<expr>, ...).encode(...)` — the exact bytes we must match. */
const pythonDumps = (expr: string, ensureAscii: boolean): Uint8Array | null =>
  python(
    [
      'import json,sys',
      `b = json.dumps(${expr}, ensure_ascii=${ensureAscii ? 'True' : 'False'},`,
      '  allow_nan=False, sort_keys=True, separators=(",",":"))',
      `sys.stdout.buffer.write(b.encode(${ensureAscii ? '"ascii"' : '"utf-8"'}))`,
    ].join('\n'),
  );

describe('S2 - integers survive as bytes', () => {
  test('the FNV-1a-64 offset basis is byte-exact', () => {
    expect(textOf(FNV_OFFSET_BASIS)).toBe('14695981039346656037');
  });

  test('a JS number would have corrupted it', () => {
    // The failure this whole value model exists to prevent.
    expect(Number.isSafeInteger(Number('14695981039346656037'))).toBe(false);
    // The double nearest the basis is 805 short of it, and prints shorter still.
    expect(BigInt(Number('14695981039346656037'))).toBe(14695981039346655232n);
    expect(FNV_OFFSET_BASIS - BigInt(Number('14695981039346656037'))).toBe(805n);
    expect(String(Number('14695981039346656037'))).toBe('14695981039346655000');
    expect(textOf(FNV_OFFSET_BASIS)).not.toBe(String(Number('14695981039346656037')));
  });

  test('the i64/u64 rails round-trip', () => {
    expect(textOf([-9223372036854775808n, 9223372036854775807n, 18446744073709551615n])).toBe(
      '[-9223372036854775808,9223372036854775807,18446744073709551615]',
    );
  });

  test('bigint and number are different JSON spellings of one value', () => {
    expect(textOf(1n)).toBe('1');
    expect(textOf(1)).toBe('1.0');
  });
});

/** `[js double, repr(x)]`.  Each repr also round-trips back to the same double. */
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
  [1.7976931348623157e308, '1.7976931348623157e+308'],
  [123456.789, '123456.789'],
  [-3.0, '-3.0'],
  [2 ** 53, '9007199254740992.0'],
];

describe('S2 - float formatting is repr(), not String()', () => {
  test.each(FLOAT_CASES)('formatFloat(%p) === %p', (value, expected) => {
    expect(Either.getOrThrow(formatFloat(value))).toBe(expected);
    // ...and the spelling denotes the very double we started from.
    expect(Object.is(Number(expected), value)).toBe(true);
  });

  test('String() diverges on exactly the cases repr() reshapes', () => {
    expect(FLOAT_CASES.filter(([v, s]) => String(v) !== s).map(([, s]) => s)).toEqual([
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

  test.skipIf(!pythonAvailable)('a live python3 agrees on every repr', () => {
    const out = python(
      [
        'import json,sys',
        `xs = json.loads(${JSON.stringify(JSON.stringify(FLOAT_CASES.map(([, s]) => s)))})`,
        'print("\\n".join(repr(float(x)) for x in xs))',
      ].join('\n'),
    );
    expect(out).not.toBeNull();
    expect(decoder.decode(out!).trimEnd().split('\n')).toEqual(FLOAT_CASES.map(([, s]) => s));
  });

  test('non-finite floats are errors, matching allow_nan=False', () => {
    expect(Either.isLeft(canonicalText(Number.NaN, true))).toBe(true);
    expect(Either.isLeft(canonicalText(Number.POSITIVE_INFINITY, true))).toBe(true);
    expect(Either.isLeft(canonicalText({ a: [Number.NEGATIVE_INFINITY] }, true))).toBe(true);
  });
});

describe('S2 - separators, sort_keys, ensure_ascii', () => {
  test('compact separators, no whitespace', () => {
    expect(textOf({ b: 1n, a: [1n, 2n], c: {} })).toBe('{"a":[1,2],"b":1,"c":{}}');
  });

  test('keys sort by code point, not UTF-16 code unit', () => {
    // Python: "￿" < "\U00010000".  JS default sort disagrees.
    const doc: CanonValue = { '\u{10000}': 1n, '￿': 2n };
    expect(Object.keys(doc).toSorted()[0]).toBe('\u{10000}');
    expect(compareCodePoints('￿', '\u{10000}')).toBeLessThan(0);
    expect(textOf(doc)).toBe('{"\\uffff":2,"\\ud800\\udc00":1}');
  });

  test('ensure_ascii=True escapes astral planes as surrogate pairs', () => {
    // DEL is escaped too: CPython's S_CHAR fast path stops at '~'.
    expect(textOf('a\u{1f600}bé\u007f')).toBe('"a\\ud83d\\ude00b\\u00e9\\u007f"');
  });

  test('ensure_ascii=False keeps them literal and escapes only the mandatory set', () => {
    expect(textOf('a\u{1f600}bé\u007f', false)).toBe('"a\u{1f600}bé\u007f"');
    expect(textOf('q"\\\n\t\u0001', false)).toBe('"q\\"\\\\\\n\\t\\u0001"');
    // ...and the utf-8 bytes are the encoded emoji, not escapes.
    expect(Array.from(bytesOf('\u{1f600}', false))).toEqual([34, 240, 159, 152, 128, 34]);
  });

  test('a lone surrogate is an escape in ascii mode and an error in utf-8 mode', () => {
    expect(textOf('\ud800', true)).toBe('"\\ud800"');
    expect(Either.isLeft(canonicalBytes('\ud800', false))).toBe(true);
    expect(Either.isRight(canonicalBytes('\u{1f600}', false))).toBe(true);
  });
});

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
];

describe('S2 - property-style parity against a live python3', () => {
  test.skipIf(!pythonAvailable)('every document matches json.dumps bytes in both modes', () => {
    const mismatches = PARITY_DOCUMENTS.flatMap(([name, doc, expr]) =>
      [true, false].flatMap((ensureAscii) => {
        const oracle = pythonDumps(expr, ensureAscii);
        const mine = decoder.decode(bytesOf(doc, ensureAscii));
        const theirs = oracle === null ? '<python failed>' : decoder.decode(oracle);
        return mine === theirs
          ? []
          : [`${name} (ensure_ascii=${ensureAscii}): ${mine} != ${theirs}`];
      }),
    );
    expect(mismatches).toEqual([]);
  });

  test.skipIf(!pythonAvailable)('the oracle really is being consulted', () => {
    // Guards the check above from passing because python3 silently no-ops.
    expect(decoder.decode(pythonDumps('{"b":1,"a":2}', true) ?? new Uint8Array())).toBe(
      '{"a":2,"b":1}',
    );
  });
});

const isRecord = (value: CanonValue | undefined): value is { readonly [k: string]: CanonValue } =>
  typeof value === 'object' && value !== null && !Array.isArray(value);

const field = (value: CanonValue | undefined, key: string): CanonValue | undefined =>
  isRecord(value) ? value[key] : undefined;

describe('S2 - THE GATE: NATIVE_OBSERVATION_ACTION_SCHEMA_ID', () => {
  test('the hashed structure carries the > 2^53 offset basis as an integer', () => {
    expect(field(field(NATIVE_SCHEMA_CANONICAL, 'research_choices_digest'), 'offset_basis')).toBe(
      FNV_OFFSET_BASIS,
    );
    expect(field(field(NATIVE_SCHEMA_CANONICAL, 'treaty_clauses_digest'), 'offset_basis')).toBe(
      FNV_OFFSET_BASIS,
    );
  });

  test('the naive JS route does not reach this digest by accident', () => {
    // `JSON.stringify` with bigints coerced to numbers: the basis is gone, and
    // so is the id.  This is why the writer above exists.
    const naive = JSON.stringify(NATIVE_SCHEMA_CANONICAL, (_key, value: unknown) =>
      typeof value === 'bigint' ? Number(value) : value,
    );
    expect(naive).not.toContain('14695981039346656037');
    expect(`sha256-${new Bun.CryptoHasher('sha256').update(naive).digest('hex')}`).not.toBe(
      ORACLE_SCHEMA_ID,
    );
  });

  test('the canonical bytes are pure ascii and carry the literal basis', () => {
    const bytes = bytesOf(NATIVE_SCHEMA_CANONICAL, true);
    expect(bytes.every((b) => b < 0x80)).toBe(true);
    expect(decoder.decode(bytes)).toContain('"offset_basis":14695981039346656037');
    expect(bytes.byteLength).toBe(50038);
  });

  test('sha256 of those bytes IS the oracle schema id', () => {
    expect(Either.getOrThrow(canonicalSha256(NATIVE_SCHEMA_CANONICAL))).toBe(ORACLE_SCHEMA_ID);
  });

  test.skipIf(!pythonAvailable)('the whole document matches v2_control byte for byte', () => {
    const oracle = python(
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
    expect(oracle).not.toBeNull();
    expect(decoder.decode(bytesOf(NATIVE_SCHEMA_CANONICAL, true))).toBe(decoder.decode(oracle!));
  });

  test.skipIf(!pythonAvailable)('the oracle constant is still what v2_control exports today', () => {
    const out = python(
      'from agent_eval.v2_control import NATIVE_OBSERVATION_ACTION_SCHEMA_ID as s; print(s)',
    );
    expect(out).not.toBeNull();
    expect(decoder.decode(out!).trim()).toBe(ORACLE_SCHEMA_ID);
  });
});
