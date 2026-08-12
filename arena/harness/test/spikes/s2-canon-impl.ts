/**
 * Spike S2 — throwaway canonical-JSON writer with CPython `json.dumps` bytes.
 *
 * The target is byte parity with
 * `json.dumps(v, ensure_ascii=?, allow_nan=False, sort_keys=True, separators=(",",":"))`
 * followed by `.encode(...)`, because `NATIVE_OBSERVATION_ACTION_SCHEMA_ID` is a
 * sha256 over exactly those bytes.  Two things JS gets wrong by default and this
 * writer gets right:
 *
 *   - integers.  The hashed structure contains 14695981039346656037, which is
 *     larger than `Number.MAX_SAFE_INTEGER`; a `number` silently lands 805
 *     short of it.  So the value model uses `bigint` for Python `int`
 *     and `number` for Python `float` — the type carries the JSON spelling,
 *     which is the only way `1` and `1.0` can round-trip distinctly.
 *   - floats.  Python emits `repr(x)`: `1.0`, `1e-05`, `1e+16`, `-0.0`, where JS
 *     emits `1`, `0.00001`, `10000000000000000`, `0`.
 *
 * The real implementation lands in `@arena/wire` as `canon.ts`; this file exists
 * to prove the shape is reachable before that package commits to it.
 */
import { Either } from 'effect';

/** A value with an unambiguous CPython JSON spelling. */
export type CanonValue =
  | null
  | boolean
  | string
  | bigint // Python int  -> 1
  | number // Python float -> 1.0
  | readonly CanonValue[]
  | { readonly [key: string]: CanonValue };

/** Why a document has no canonical encoding. */
export type CanonError =
  | { readonly _tag: 'NonFiniteFloat'; readonly path: string }
  | { readonly _tag: 'LoneSurrogate'; readonly path: string; readonly unit: number };

const nonFinite = (path: string): CanonError => ({ _tag: 'NonFiniteFloat', path });

/**
 * Compare by code point, the way Python orders `str`.
 *
 * JS `<` compares UTF-16 code units, so `"\uffff" < "\u{10000}"` is false in JS
 * and true in Python.  `sort_keys=True` over any astral key would diverge.
 */
export const compareCodePoints = (left: string, right: string): number => {
  const a = Array.from(left).map((c) => c.codePointAt(0) ?? 0);
  const b = Array.from(right).map((c) => c.codePointAt(0) ?? 0);
  const firstDiff = a.findIndex((cp, i) => cp !== b[i]);
  if (firstDiff === -1) return a.length - b.length;
  const other = b[firstDiff];
  return other === undefined ? 1 : a[firstDiff]! - other;
};

const SHORT_ESCAPES: Readonly<Record<string, string>> = {
  '"': '\\"',
  '\\': '\\\\',
  '\b': '\\b',
  '\f': '\\f',
  '\n': '\\n',
  '\r': '\\r',
  '\t': '\\t',
};

const hex4 = (unit: number): string => `\\u${unit.toString(16).padStart(4, '0')}`;

const escapeChunk = (chunk: string): string => {
  const short = SHORT_ESCAPES[chunk];
  if (short !== undefined) return short;
  // With the `u` flag an astral char arrives whole; Python emits its surrogate
  // pair as two \uXXXX escapes, which is what the UTF-16 units already are.
  return Array.from({ length: chunk.length }, (_, i) => hex4(chunk.charCodeAt(i))).join('');
};

const ASCII_ESCAPABLE = /[^\x20-\x7e]|["\\]/gu;
// Matching C0 controls is the job here: `json.dumps` escapes exactly this set
// when ensure_ascii is False, so heeding no-control-regex would break parity.
// oxlint-disable-next-line no-control-regex
const UNICODE_ESCAPABLE = /["\\\u0000-\u001f]/gu;

/** `json.encoder.py_encode_basestring{,_ascii}` for one string. */
export const encodeString = (value: string, ensureAscii: boolean): string =>
  `"${value.replace(ensureAscii ? ASCII_ESCAPABLE : UNICODE_ESCAPABLE, escapeChunk)}"`;

/**
 * `repr(float)` — shortest round-tripping digits, CPython's layout rules.
 *
 * CPython (`format_float_short`, mode `'r'`) picks exponential notation when the
 * decimal point position `decpt` satisfies `decpt <= -4 || decpt > 16`, pads the
 * exponent to two digits with a sign, and always leaves a fractional part in
 * fixed notation (`1.0`, never `1`).
 */
export const formatFloat = (value: number): Either.Either<string, CanonError> => {
  if (!Number.isFinite(value)) return Either.left(nonFinite(''));
  const negative = value < 0 || Object.is(value, -0);
  const parts = Math.abs(value).toExponential().split('e');
  const digits = (parts[0] ?? '0').replace('.', '');
  const decpt = Number(parts[1] ?? '0') + 1;
  const sign = negative ? '-' : '';
  if (decpt <= -4 || decpt > 16) {
    const mantissa = digits.length > 1 ? `${digits[0]}.${digits.slice(1)}` : digits;
    const exp = decpt - 1;
    const expSign = exp < 0 ? '-' : '+';
    return Either.right(
      `${sign}${mantissa}e${expSign}${Math.abs(exp).toString().padStart(2, '0')}`,
    );
  }
  if (decpt <= 0) return Either.right(`${sign}0.${'0'.repeat(-decpt)}${digits}`);
  if (decpt >= digits.length) {
    return Either.right(`${sign}${digits}${'0'.repeat(decpt - digits.length)}.0`);
  }
  return Either.right(`${sign}${digits.slice(0, decpt)}.${digits.slice(decpt)}`);
};

const joinAll = (
  encoded: readonly Either.Either<string, CanonError>[],
): Either.Either<readonly string[], CanonError> => Either.all(encoded);

/** A JSON object, as opposed to the array arm of the union. */
const isCanonRecord = (
  value: readonly CanonValue[] | { readonly [key: string]: CanonValue },
): value is { readonly [key: string]: CanonValue } => !Array.isArray(value);

const write = (
  value: CanonValue,
  ensureAscii: boolean,
  path: string,
): Either.Either<string, CanonError> => {
  if (value === null) return Either.right('null');
  switch (typeof value) {
    case 'boolean':
      return Either.right(value ? 'true' : 'false');
    case 'bigint':
      return Either.right(value.toString(10));
    case 'number':
      return Either.mapLeft(formatFloat(value), () => nonFinite(path));
    case 'string':
      return Either.right(encodeString(value, ensureAscii));
    default:
      break;
  }
  // `Array.isArray` does not narrow the negative branch of a `ReadonlyArray`
  // union, so the object case gets the explicit guard.
  if (!isCanonRecord(value)) {
    return Either.map(
      joinAll(value.map((item, i) => write(item, ensureAscii, `${path}[${i}]`))),
      (parts) => `[${parts.join(',')}]`,
    );
  }
  const entries = Object.keys(value)
    .toSorted(compareCodePoints)
    .map((key) =>
      Either.map(write(value[key]!, ensureAscii, `${path}.${key}`), (encoded) => {
        return `${encodeString(key, ensureAscii)}:${encoded}`;
      }),
    );
  return Either.map(joinAll(entries), (parts) => `{${parts.join(',')}}`);
};

/** Canonical text: `sort_keys=True`, `separators=(",",":")`, `allow_nan=False`. */
export const canonicalText = (
  value: CanonValue,
  ensureAscii: boolean,
): Either.Either<string, CanonError> => write(value, ensureAscii, '$');

const findLoneSurrogate = (text: string): number =>
  Array.from({ length: text.length }, (_, i) => i).findIndex((i) => {
    const unit = text.charCodeAt(i);
    if (unit < 0xd800 || unit > 0xdfff) return false;
    if (unit >= 0xdc00) return text.charCodeAt(i - 1) < 0xd800 || text.charCodeAt(i - 1) > 0xdbff;
    const next = text.charCodeAt(i + 1);
    return !(next >= 0xdc00 && next <= 0xdfff);
  });

/**
 * Canonical bytes.  `ensureAscii` picks the encoding too, mirroring the Python
 * call sites: `.encode("ascii")` for the schema id, `.encode("utf-8")` for the
 * wire.  A lone surrogate is an error rather than a silent U+FFFD, because
 * `str.encode("utf-8")` raises on one.
 */
export const canonicalBytes = (
  value: CanonValue,
  ensureAscii: boolean,
): Either.Either<Uint8Array, CanonError> =>
  Either.flatMap(canonicalText(value, ensureAscii), (text) => {
    const lone = ensureAscii ? -1 : findLoneSurrogate(text);
    return lone === -1
      ? Either.right(new TextEncoder().encode(text))
      : Either.left({ _tag: 'LoneSurrogate', path: '$', unit: text.charCodeAt(lone) } as const);
  });

/** `f"sha256-{hashlib.sha256(encoded).hexdigest()}"`. */
export const canonicalSha256 = (value: CanonValue): Either.Either<string, CanonError> =>
  Either.map(canonicalBytes(value, true), (bytes) => {
    const digest = new Bun.CryptoHasher('sha256').update(bytes).digest('hex');
    return `sha256-${digest}`;
  });
