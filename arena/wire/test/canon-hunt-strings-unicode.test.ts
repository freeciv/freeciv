/**
 * Canonical JSON — regression pins from the strings/unicode adversarial hunt.
 *
 * `canon.test.ts` re-derives its answers from a live CPython.  This file does
 * the opposite: it hardcodes bytes that a CPython 3.14 oracle already produced,
 * so the escaping contract stays pinned on a machine with no `python3`, and so
 * a future "simplification" of the escape regexes has to argue with a literal.
 *
 * The bytes come from a differential run of 1,114,112 code points (every
 * Unicode scalar and every surrogate, each in three string roles), 20,000
 * random documents over a hostile alphabet, and a 1,553-key ordering probe,
 * all against
 *
 * ```python
 * json.dumps(v, ensure_ascii=?, allow_nan=False, sort_keys=True,
 *            separators=(",", ":")).encode("ascii" | "utf-8")
 * ```
 *
 * Every string here is built from code points rather than written as text: a
 * suite about byte-level escaping must not depend on how its own source file
 * is encoded, and lone surrogates have no source-text spelling at all.
 */
import { describe, expect, test } from 'bun:test';
import { Either } from 'effect';
import {
  CANON_ASCII,
  CANON_UTF8,
  type CanonOptions,
  type CanonValue,
  canonicalBytes,
  compareCodePoints,
} from 'src/index';

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

/** A string from code points, the way the Python oracle built its own. */
const str = (...points: readonly number[]): string =>
  points.map((point) => String.fromCodePoint(point)).join('');

const record = (...entries: readonly (readonly [string, CanonValue])[]): CanonValue =>
  Object.fromEntries(entries);

/** What CPython did: bytes, or the refusal `str.encode("utf-8")` raised. */
type Expected =
  | { readonly kind: 'bytes'; readonly hex: string }
  | { readonly kind: 'lone'; readonly index: number; readonly unit: number };

const bytes = (hex: string): Expected => ({ kind: 'bytes', hex });
const lone = (index: number, unit: number): Expected => ({ kind: 'lone', index, unit });

interface Fixture {
  readonly name: string;
  readonly value: CanonValue;
  /** `ensure_ascii=True` + `.encode("ascii")`. */
  readonly ascii: Expected;
  /** `ensure_ascii=False` + `.encode("utf-8")`. */
  readonly utf8: Expected;
}

const FIXTURES: readonly Fixture[] = [
  {
    // No short escape exists for NUL; CPython spells it as a six-character escape in both modes.
    name: 'nul is \\u0000 in both modes',
    value: str(0x00),
    ascii: bytes('225c753030303022'),
    utf8: bytes('225c753030303022'),
  },
  {
    // \v is a C0 control with no JSON short form, unlike its neighbours.
    name: 'vertical tab has no short escape',
    value: str(0x0b),
    ascii: bytes('225c753030306222'),
    utf8: bytes('225c753030306222'),
  },
  {
    name: 'the five short escapes stay short',
    value: str(0x08, 0x0c, 0x0a, 0x0d, 0x09),
    ascii: bytes('225c625c665c6e5c725c7422'),
    utf8: bytes('225c625c665c6e5c725c7422'),
  },
  {
    // The boundary the ASCII fast path stops at: `~` literal, DEL escaped.
    name: 'DEL is escaped in ascii mode and literal in utf-8 mode',
    value: str(0x7e, 0x7f, 0x80),
    ascii: bytes('227e5c75303037665c753030383022'),
    utf8: bytes('227e7fc28022'),
  },
  {
    // JSON's line separators are legal raw in Python's non-ascii output even
    // though they break JavaScript's own source grammar.
    name: 'U+2028 and U+2029 are not escaped in utf-8 mode',
    value: str(0x41, 0x2028, 0x42, 0x2029, 0x43),
    ascii: bytes('22415c7532303238425c75323032394322'),
    utf8: bytes('2241e280a842e280a94322'),
  },
  {
    name: 'an astral character becomes a surrogate pair in ascii mode',
    value: str(0x1f600),
    ascii: bytes('225c75643833645c756465303022'),
    utf8: bytes('22f09f988022'),
  },
  {
    name: 'the last code point round-trips',
    value: str(0x10ffff),
    ascii: bytes('225c75646266665c756466666622'),
    utf8: bytes('22f48fbfbf22'),
  },
  {
    name: 'a ZWJ emoji sequence keeps its joiners',
    value: str(0x1f468, 0x200d, 0x1f469, 0x200d, 0x1f467),
    ascii: bytes(
      '225c75643833645c75646336385c75323030645c75643833645c75646336' +
        '395c75323030645c75643833645c756463363722',
    ),
    utf8: bytes('22f09f91a8e2808df09f91a9e2808df09f91a722'),
  },
  {
    // Canonicalization is not normalization: NFD and NFC keys stay distinct
    // and sort by their code points, decomposed first.
    name: 'combining marks are neither normalized nor reordered',
    value: record([str(0x65, 0x301), str(0x64)], [str(0xe9), str(0x63)]),
    ascii: bytes('7b22655c7530333031223a2264222c225c7530306539223a2263227d'),
    utf8: bytes('7b2265cc81223a2264222c22c3a9223a2263227d'),
  },
  {
    // The headline key-order trap: JS `<` puts U+10000 before U+FFFF because
    // it compares UTF-16 units; Python puts U+FFFF first.
    name: 'U+FFFF sorts before U+10000',
    value: record([str(0x10000), str(0x61)], [str(0xffff), str(0x62)]),
    ascii: bytes('7b225c7566666666223a2262222c225c75643830305c7564633030223a2261227d'),
    utf8: bytes('7b22efbfbf223a2262222c22f0908080223a2261227d'),
  },
  {
    name: 'keys across six planes sort by code point',
    value: record(
      [str(0x1f600), str(0x31)],
      [str(0xfffd), str(0x32)],
      [str(0x41), str(0x33)],
      [str(0x00), str(0x34)],
      [str(0x10ffff), str(0x35)],
      [str(0xe000), str(0x36)],
    ),
    ascii: bytes(
      '7b225c7530303030223a2234222c2241223a2233222c225c7565303030223a2236222c22' +
        '5c7566666664223a2232222c225c75643833645c7564653030223a2231222c225c7564626' +
        '6665c7564666666223a2235227d',
    ),
    utf8: bytes(
      '7b225c7530303030223a2234222c2241223a2233222c22ee8080223a2236222c22efbfbd' +
        '223a2232222c22f09f9880223a2231222c22f48fbfbf223a2235227d',
    ),
  },
  {
    name: 'the empty key sorts before U+0000',
    value: record([str(), str(0x31)], [str(0x00), str(0x32)]),
    ascii: bytes('7b22223a2231222c225c7530303030223a2232227d'),
    utf8: bytes('7b22223a2231222c225c7530303030223a2232227d'),
  },
  {
    name: 'a NUL key is escaped like any other string',
    value: record([str(0x00), str(0x41)], [str(0x41), str(0x42)]),
    ascii: bytes('7b225c7530303030223a2241222c2241223a2242227d'),
    utf8: bytes('7b225c7530303030223a2241222c2241223a2242227d'),
  },
  {
    name: 'an empty key with an empty value is a two-string object',
    value: record([str(), str()]),
    ascii: bytes('7b22223a22227d'),
    utf8: bytes('7b22223a22227d'),
  },
  {
    // One string touching every escaping rule at once.
    name: 'every escaping rule in one string',
    value: str(0x41, 0x00, 0x7f, 0x80, 0xff, 0x2028, 0x1f600, 0x5c, 0x22, 0x0a),
    ascii: bytes(
      '22415c75303030305c75303037665c75303038305c75303066665c7532303238' +
        '5c75643833645c75646530305c5c5c225c6e22',
    ),
    utf8: bytes('22415c75303030307fc280c3bfe280a8f09f98805c5c5c225c6e22'),
  },
  {
    // Legal ASCII output, impossible UTF-8 output — the asymmetry that makes
    // the encoding mode part of the identity rather than a detail.
    name: 'a lone high surrogate escapes in ascii and refuses in utf-8',
    value: str(0xd800),
    ascii: bytes('225c756438303022'),
    utf8: lone(0, 0xd800),
  },
  {
    // The reported index is a UTF-16 offset, so the astral character ahead of
    // it counts twice; CPython's UnicodeEncodeError.start would say 1.
    name: 'a lone surrogate after an astral character reports a UTF-16 index',
    value: str(0x10000, 0xd800),
    ascii: bytes('225c75643830305c75646330305c756438303022'),
    utf8: lone(2, 0xd800),
  },
  {
    // Sorting happens before encoding, so the surrogate key is visited first
    // and its refusal is the one that surfaces.
    name: 'a surrogate key sorts below U+FFFF and refuses utf-8 first',
    value: record([str(0xd800), str(0x31)], [str(0xffff), str(0x32)], [str(0x10000), str(0x33)]),
    ascii: bytes(
      '7b225c7564383030223a2231222c225c7566666666223a2232222c225c756438' +
        '30305c7564633030223a2233227d',
    ),
    utf8: lone(0, 0xd800),
  },
];

// ---------------------------------------------------------------------------
// The comparison
// ---------------------------------------------------------------------------

const hexOf = (raw: Uint8Array): string =>
  Array.from(raw, (byte) => byte.toString(16).padStart(2, '0')).join('');

/** `"OK:<hex>"` or `"LoneSurrogate:<index>:<unit>"` — one string to diff. */
const outcome = (value: CanonValue, options: CanonOptions): string =>
  Either.match(canonicalBytes(value, options), {
    onLeft: (error) =>
      error._tag === 'LoneSurrogate'
        ? `LoneSurrogate:${error.index}:${error.unit.toString(16)}`
        : `${error._tag}:${error.message}`,
    onRight: (raw) => `OK:${hexOf(raw)}`,
  });

const rendered = (expected: Expected): string =>
  expected.kind === 'bytes'
    ? `OK:${expected.hex}`
    : `LoneSurrogate:${expected.index}:${expected.unit.toString(16)}`;

describe('canon vs CPython — pinned strings and unicode', () => {
  FIXTURES.forEach((fixture) => {
    test(`${fixture.name} (ascii)`, () => {
      expect(outcome(fixture.value, CANON_ASCII)).toBe(rendered(fixture.ascii));
    });

    test(`${fixture.name} (utf-8)`, () => {
      expect(outcome(fixture.value, CANON_UTF8)).toBe(rendered(fixture.utf8));
    });
  });

  test('ascii output is ASCII by construction', () => {
    const offenders = FIXTURES.filter(
      (fixture) =>
        fixture.ascii.kind === 'bytes' &&
        Either.match(canonicalBytes(fixture.value, CANON_ASCII), {
          onLeft: () => true,
          onRight: (raw) => raw.some((byte) => byte > 0x7e || byte < 0x20),
        }),
    ).map((fixture) => fixture.name);
    expect(offenders).toEqual([]);
  });
});

// ---------------------------------------------------------------------------
// Ordering
// ---------------------------------------------------------------------------

/**
 * `sorted()` in CPython over the same strings.  Every adjacent pair here is one
 * JS `<` would get wrong or one that pins a boundary of the surrogate block.
 */
const PYTHON_SORTED: readonly (readonly number[])[] = [
  [],
  [0x00],
  [0x41],
  [0x41, 0x41],
  [0x7e],
  [0x7f],
  [0x80],
  [0xd7ff],
  [0xd800],
  [0xdbff],
  [0xdc00],
  [0xdfff],
  [0xe000],
  [0xfffd],
  [0xffff],
  [0x10000],
  [0x1f600],
  [0x10ffff],
];

describe('key order follows Python str, not UTF-16', () => {
  test('the pinned list is already in canon order', () => {
    const strings = PYTHON_SORTED.map((points) => str(...points));
    const sorted = strings.toSorted(compareCodePoints);
    expect(sorted.map((text) => Array.from(text, (c) => c.charCodeAt(0)))).toEqual(
      strings.map((text) => Array.from(text, (c) => c.charCodeAt(0))),
    );
  });

  test('reversing the input does not change the result', () => {
    const strings = PYTHON_SORTED.map((points) => str(...points));
    expect(strings.toReversed().toSorted(compareCodePoints).join('')).toBe(
      strings.join(''),
    );
  });

  test('the comparator is a strict total order', () => {
    const strings = PYTHON_SORTED.map((points) => str(...points));
    const broken = strings.flatMap((left, i) =>
      strings.flatMap((right, j) => {
        const forward = compareCodePoints(left, right);
        const backward = compareCodePoints(right, left);
        const want = i === j ? 0 : i < j ? -1 : 1;
        return Math.sign(forward) === want && Math.sign(backward) === -want ? [] : [`${i},${j}`];
      }),
    );
    expect(broken).toEqual([]);
  });

  test('JS default order really is wrong here, so the comparator earns its keep', () => {
    // If this ever passes with the native comparison, the trap moved.
    expect(str(0xffff) < str(0x10000)).toBe(false);
    expect(compareCodePoints(str(0xffff), str(0x10000))).toBeLessThan(0);
  });
});

// ---------------------------------------------------------------------------
// The one place the two value models cannot meet
// ---------------------------------------------------------------------------

/**
 * A Python `str` may hold a high surrogate followed by a low surrogate as two
 * separate characters; the same JS string is one astral character.  No JS value
 * denotes the Python one, so this is a boundary rather than a bug — and it is
 * invisible on the wire, because the mode where the two disagree is exactly the
 * mode where CPython produces no bytes at all.
 */
describe('the UTF-16 pairing boundary', () => {
  const paired = str(0xd800, 0xdc00);

  test('ascii output matches CPython even though the values differ', () => {
    // CPython on the two-character str emits the same two escapes.
    expect(outcome(paired, CANON_ASCII)).toBe('OK:225c75643830305c756463303022');
  });

  test('the ascii wire form round-trips back to the same bytes', () => {
    const reparsed: CanonValue = JSON.parse('"\\ud800\\udc00"');
    expect(outcome(reparsed, CANON_ASCII)).toBe(outcome(paired, CANON_ASCII));
  });

  test('utf-8 succeeds here while CPython raises, and that is the whole gap', () => {
    // CPython: UnicodeEncodeError.  canon sees one astral character.
    expect(outcome(paired, CANON_UTF8)).toBe('OK:22f090808022');
    // Unpaired neighbours are still refused, so the rail is not simply off.
    expect(outcome(str(0xdc00, 0xd800), CANON_UTF8)).toBe('LoneSurrogate:0:dc00');
    expect(outcome(str(0xd800, 0xd800), CANON_UTF8)).toBe('LoneSurrogate:0:d800');
  });
});
