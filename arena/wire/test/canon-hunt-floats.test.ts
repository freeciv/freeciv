/**
 * Adversarial float hunt — regression pins for `repr(float)` byte parity.
 *
 * Every expectation in this file was produced by a live CPython 3.14 and
 * transcribed verbatim:
 *
 * ```python
 * json.dumps({"v": x}, ensure_ascii=?, allow_nan=False,
 *            sort_keys=True, separators=(",", ":")).encode(?)
 * ```
 *
 * The hunt behind it swept 1,509,923 doubles through {@link formatFloat} and
 * compared each against `repr(x)` in a spawned `python3` — uniform random bit
 * patterns, subnormals only, contiguous 60k-ulp blocks straddling every
 * `decpt` boundary (1e16, 1e17, 1e-4, 1e-5, 5e-324, 1.0, 1e23, the smallest
 * normal, MAX_VALUE), the `n * 10**k` grid, and ulp-nudges around round
 * decimals.  Zero divergences.  What survives here is the set of cases that
 * would each have caught a distinct plausible bug, so a future edit to
 * `formatFloat` cannot quietly regress one of them.
 *
 * Floats never emit a non-ASCII byte, so `ensure_ascii=True` and
 * `ensure_ascii=False` must produce *identical* bytes for every case below —
 * a property the table asserts rather than assumes.
 */
import { describe, expect, test } from 'bun:test';
import { Either, Option } from 'effect';
import {
  CANON_ASCII,
  CANON_UTF8,
  canonicalBytes,
  canonicalText,
  formatFloat,
  fnv1a64,
  formatFnv1a64,
} from 'src/index';

const hex = (bytes: Uint8Array): string =>
  Array.from(bytes, (byte) => byte.toString(16).padStart(2, '0')).join('');

const bitsOf = (value: number): string => {
  const view = new DataView(new ArrayBuffer(8));
  view.setFloat64(0, value, false);
  return view.getBigUint64(0, false).toString(16).padStart(16, '0');
};

/** `[label, value, repr(value), json.dumps({"v": value}) bytes as hex]`. */
type Row = readonly [string, number, string, string];

/**
 * The pins.  Column 3 is CPython's `repr`; column 4 is the whole document's
 * bytes, which is what a digest is actually taken over.
 */
const ROWS: readonly Row[] = [
  ['zero', 0.0, '0.0', '7b2276223a302e307d'],
  ['negative zero keeps its sign', -0.0, '-0.0', '7b2276223a2d302e307d'],
  [
    '0.1 + 0.2 needs all 17 digits',
    0.30000000000000004,
    '0.30000000000000004',
    '7b2276223a302e33303030303030303030303030303030347d',
  ],
  [
    'one third, 16 digits',
    0.3333333333333333,
    '0.3333333333333333',
    '7b2276223a302e333333333333333333333333333333337d',
  ],
  [
    'decpt 16 stays fixed and keeps ".0"',
    9999999999999998.0,
    '9999999999999998.0',
    '7b2276223a393939393939393939393939393939382e307d',
  ],
  [
    '2**53 is fixed, not exponential',
    9007199254740992.0,
    '9007199254740992.0',
    '7b2276223a393030373139393235343734303939322e307d',
  ],
  [
    '2**53 + 2 is the next representable',
    9007199254740994.0,
    '9007199254740994.0',
    '7b2276223a393030373139393235343734303939342e307d',
  ],
  ['decpt 17 flips to exponential', 1e16, '1e+16', '7b2276223a31652b31367d'],
  ['1e17', 1e17, '1e+17', '7b2276223a31652b31377d'],
  ['1e23 is not the decimal 1e23', 1e23, '1e+23', '7b2276223a31652b32337d'],
  ['decpt -3 stays fixed', 1e-4, '0.0001', '7b2276223a302e303030317d'],
  [
    'decpt -4 flips to exponential, two-digit exponent',
    1e-5,
    '1e-05',
    '7b2276223a31652d30357d',
  ],
  ['negative, small, exponential', -1.5e-7, '-1.5e-07', '7b2276223a2d312e35652d30377d'],
  ['smallest subnormal', 5e-324, '5e-324', '7b2276223a35652d3332347d'],
  ['subnormal 1e-323', 1e-323, '1e-323', '7b2276223a31652d3332337d'],
  ['subnormal 4.94e-321', 4.94e-321, '4.94e-321', '7b2276223a342e3934652d3332317d'],
  ['subnormal 1e-320', 1e-320, '1e-320', '7b2276223a31652d3332307d'],
  [
    'largest subnormal',
    2.225073858507201e-308,
    '2.225073858507201e-308',
    '7b2276223a322e323235303733383538353037323031652d3330387d',
  ],
  [
    'smallest normal',
    2.2250738585072014e-308,
    '2.2250738585072014e-308',
    '7b2276223a322e32323530373338353835303732303134652d3330387d',
  ],
  [
    'max double, three-digit exponent',
    1.7976931348623157e308,
    '1.7976931348623157e+308',
    '7b2276223a312e37393736393331333438363233313537652b3330387d',
  ],
  [
    'most negative double',
    -1.7976931348623157e308,
    '-1.7976931348623157e+308',
    '7b2276223a2d312e37393736393331333438363233313537652b3330387d',
  ],
  [
    'machine epsilon',
    2.220446049250313e-16,
    '2.220446049250313e-16',
    '7b2276223a322e323230343436303439323530333133652d31367d',
  ],
  ['a plain decimal', 123.456, '123.456', '7b2276223a3132332e3435367d'],
  // `Math.PI` would defeat the point: the literal is the transcription being
  // tested, so it has to be spelled out the way CPython printed it.
  // oxlint-disable-next-line approx-constant
  ['pi', 3.141592653589793, '3.141592653589793', '7b2276223a332e3134313539323635333538393739337d'],
];

describe('formatFloat matches repr(float)', () => {
  test.each(ROWS)('%s', (_label, value, repr) => {
    expect(Option.getOrNull(formatFloat(value))).toBe(repr);
  });
});

describe('a document of one float matches json.dumps bytes', () => {
  test.each(ROWS)('%s', (_label, value, repr, expected) => {
    const document = { v: value };

    expect(Either.getOrNull(canonicalText(document, CANON_ASCII))).toBe(`{"v":${repr}}`);
    expect(Either.getOrNull(canonicalText(document, CANON_UTF8))).toBe(`{"v":${repr}}`);

    const ascii = canonicalBytes(document, CANON_ASCII);
    const utf8 = canonicalBytes(document, CANON_UTF8);
    expect(Either.map(ascii, hex)).toStrictEqual(Either.right(expected));
    // Floats are ASCII either way, so the two encodings cannot disagree.
    expect(Either.map(utf8, hex)).toStrictEqual(Either.right(expected));
  });
});

describe('the pinned literals are the doubles CPython saw', () => {
  // Guards the table itself: a typo in a literal that still parses would
  // otherwise silently retarget the test at a neighbouring double.
  test.each([
    ['zero', 0.0, '0000000000000000'],
    ['negative zero', -0.0, '8000000000000000'],
    ['0.1 + 0.2', 0.30000000000000004, '3fd3333333333334'],
    ['1e16', 1e16, '4341c37937e08000'],
    ['9999999999999998.0', 9999999999999998.0, '4341c37937e07fff'],
    ['smallest subnormal', 5e-324, '0000000000000001'],
    ['largest subnormal', 2.225073858507201e-308, '000fffffffffffff'],
    ['smallest normal', 2.2250738585072014e-308, '0010000000000000'],
    ['max double', 1.7976931348623157e308, '7fefffffffffffff'],
  ] as const)('%s', (_label, value, bits) => {
    expect(bitsOf(value)).toBe(bits);
  });
});

test('every pinned float in one document reproduces CPython byte for byte', () => {
  const document = Object.fromEntries(
    ROWS.map(([, value], index) => [`k${index.toString().padStart(2, '0')}`, value]),
  );

  // json.dumps(agg, ensure_ascii=True, allow_nan=False, sort_keys=True,
  //            separators=(",", ":")) from CPython 3.14, verbatim.
  const expectedText =
    '{"k00":0.0,"k01":-0.0,"k02":0.30000000000000004,"k03":0.3333333333333333,' +
    '"k04":9999999999999998.0,"k05":9007199254740992.0,"k06":9007199254740994.0,' +
    '"k07":1e+16,"k08":1e+17,"k09":1e+23,"k10":0.0001,"k11":1e-05,"k12":-1.5e-07,' +
    '"k13":5e-324,"k14":1e-323,"k15":4.94e-321,"k16":1e-320,' +
    '"k17":2.225073858507201e-308,"k18":2.2250738585072014e-308,' +
    '"k19":1.7976931348623157e+308,"k20":-1.7976931348623157e+308,' +
    '"k21":2.220446049250313e-16,"k22":123.456,"k23":3.141592653589793}';

  expect(Either.getOrNull(canonicalText(document, CANON_ASCII))).toBe(expectedText);

  const bytes = canonicalBytes(document, CANON_ASCII);
  expect(Either.map(bytes, (encoded) => encoded.length)).toStrictEqual(Either.right(465));
  // The digest is the point: one wrong digit anywhere changes it.
  expect(Either.map(bytes, (encoded) => formatFnv1a64(fnv1a64(encoded)))).toStrictEqual(
    Either.right('fnv1a64-af58970d2dca8830'),
  );
});

describe('allow_nan=False refuses what CPython refuses', () => {
  // CPython raises ValueError("Out of range float values are not JSON
  // compliant") for each of these; canon must return the matching left rather
  // than inventing `NaN` / `Infinity` tokens.
  const view = new DataView(new ArrayBuffer(8));
  const fromBits = (bits: bigint): number => {
    view.setBigUint64(0, bits, false);
    return view.getFloat64(0, false);
  };

  test.each([
    ['quiet NaN', 0x7ff8_0000_0000_0000n],
    ['signalling NaN', 0x7ff0_0000_0000_0001n],
    ['negative NaN', 0xfff8_0000_0000_0000n],
    ['NaN with a payload', 0x7ff8_dead_beef_cafen],
    ['positive infinity', 0x7ff0_0000_0000_0000n],
    ['negative infinity', 0xfff0_0000_0000_0000n],
  ] as const)('%s', (_label, bits) => {
    const value = fromBits(bits);

    expect(formatFloat(value)).toStrictEqual(Option.none());

    const nested = canonicalText({ z: 1n, a: [{ deep: value }] }, CANON_ASCII);
    expect(Either.isLeft(nested)).toBe(true);
    expect(
      Either.match(nested, {
        onLeft: (error) => ({ tag: error._tag, path: error.path }),
        onRight: () => ({ tag: 'right', path: [] as readonly (string | number)[] }),
      }),
    ).toStrictEqual({ tag: 'NonFiniteFloat', path: ['a', 0, 'deep'] });
  });
});
