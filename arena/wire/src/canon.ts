/**
 * Canonical JSON — byte parity with CPython.
 *
 * Every identity the native control layer checks is a hash over the *bytes* of
 *
 * ```python
 * json.dumps(value, ensure_ascii=?, allow_nan=False,
 *            sort_keys=True, separators=(",", ":")).encode(?)
 * ```
 *
 * so this module reproduces those bytes rather than something merely
 * equivalent.  `NATIVE_OBSERVATION_ACTION_SCHEMA_ID` is the load-bearing
 * example: the native client refuses to talk to a control layer whose id
 * differs, and the hashed structure contains 14695981039346656037.
 *
 * Four places where a naive `JSON.stringify` diverges, each handled below:
 *
 *   - **integers.**  14695981039346656037 > `Number.MAX_SAFE_INTEGER`; the
 *     nearest double is 805 short and prints shorter still.  So {@link
 *     CanonValue} spells Python `int` as `bigint` and Python `float` as
 *     `number` — the JS type carries the JSON spelling, which is the only way
 *     `1` and `1.0` can round-trip distinctly.
 *   - **floats.**  Python emits `repr(x)`: `1.0`, `1e-05`, `1e+16`, `-0.0`
 *     where JS emits `1`, `0.00001`, `10000000000000000`, `0`.
 *   - **key order.**  `sort_keys=True` orders by code point; JS `<` orders by
 *     UTF-16 code unit, and the two disagree above U+E000.
 *   - **encoding.**  A lone surrogate is a `UnicodeEncodeError` in Python but
 *     a silent U+FFFD from `TextEncoder`, which would be a silent digest
 *     divergence.  Here it is an error value.
 *
 * Nothing in this module throws: every refusal is an `Either.left` carrying the
 * path of the offending node.
 */
import { Data, Either, Option } from 'effect';

// ---------------------------------------------------------------------------
// The value model
// ---------------------------------------------------------------------------

/** A JSON object.  Named so {@link CanonValue} can refer to itself. */
export interface CanonRecord {
  readonly [key: string]: CanonValue;
}

/**
 * A value with an unambiguous CPython JSON spelling.
 *
 * `bigint` is a Python `int` (`1`), `number` is a Python `float` (`1.0`).
 * Schema decoders in this package must therefore produce `bigint` for every
 * integer field; a `number` there is a bug that only shows up as a hash
 * mismatch far away.
 */
export type CanonValue =
  | null
  | boolean
  | string
  | bigint
  | number
  | readonly CanonValue[]
  | CanonRecord;

/** How a canonical writer is told which escaping policy to use. */
export interface CanonOptions {
  /**
   * `ensure_ascii`.  `true` escapes every non-ASCII character and the result
   * is `.encode("ascii")`-able; `false` keeps them literal for
   * `.encode("utf-8")`.  There is no default on purpose: the two produce
   * different bytes and therefore different digests.
   */
  readonly ensureAscii: boolean;
}

/** `ensure_ascii=True` + `.encode("ascii")` — schema ids and native handshakes. */
export const CANON_ASCII: CanonOptions = { ensureAscii: true };

/** `ensure_ascii=False` + `.encode("utf-8")` — idempotency bodies and digests. */
export const CANON_UTF8: CanonOptions = { ensureAscii: false };

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

/** One step into a document: an object key or an array index. */
export type CanonPathSegment = string | number;

/** Where in a document a value lives, from the root outwards. */
export type CanonPath = readonly CanonPathSegment[];

/**
 * A path for humans: `$["cities"][0]["name"]`.
 *
 * `JSON.stringify` is fine here precisely because this string is diagnostic —
 * it never reaches the wire, so its escaping does not have to be CPython's.
 */
export const formatCanonPath = (path: CanonPath): string =>
  path.reduce<string>(
    (rendered, segment) =>
      typeof segment === 'number'
        ? `${rendered}[${segment}]`
        : `${rendered}[${JSON.stringify(segment)}]`,
    '$',
  );

/**
 * The path as a spine, built while descending.
 *
 * Sharing the parent instead of copying an array keeps the happy path at one
 * small allocation per visited child; the array is materialized only when an
 * error is actually constructed.
 */
interface PathLink {
  readonly segment: CanonPathSegment;
  readonly parent: PathLink | null;
}

const pathOf = (link: PathLink | null): CanonPath =>
  link === null ? [] : [...pathOf(link.parent), link.segment];

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

/** `allow_nan=False`: `NaN`, `Infinity` and `-Infinity` have no JSON spelling. */
export class NonFiniteFloatError extends Data.TaggedError('NonFiniteFloat')<{
  readonly path: CanonPath;
  readonly value: number;
  readonly message: string;
}> {}

/**
 * A string carrying an unpaired UTF-16 surrogate.
 *
 * Legal in `ensure_ascii=True` output (Python escapes it as `\ud800` and the
 * bytes stay ASCII), fatal for UTF-8 because `str.encode("utf-8")` raises
 * where `TextEncoder` would quietly substitute U+FFFD.
 *
 * {@link LoneSurrogateError.index} is a UTF-16 **code unit** offset, while the
 * `.start` of the `UnicodeEncodeError` CPython raises for the same string is a
 * **code point** offset.  For `"𐀀\ud800"` CPython says 1 and this says 2.  The
 * two disagree on any string carrying an astral character ahead of the
 * offending surrogate, and that is fine: both sides refuse the string, so no
 * bytes ever differ — only the diagnostic integer, which never reaches the
 * wire.  JS has no cheap code-point offset, and reporting one would make the
 * number useless for indexing back into the JS string that produced it.
 */
export class LoneSurrogateError extends Data.TaggedError('LoneSurrogate')<{
  readonly path: CanonPath;
  /**
   * Index of the offending code unit within the string — a UTF-16 offset, not
   * CPython's code-point offset.  See the class docstring.
   */
  readonly index: number;
  /** The code unit itself, in 0xD800..0xDFFF. */
  readonly unit: number;
  readonly message: string;
}> {}

/**
 * Nesting deeper than {@link CANON_MAX_DEPTH}.
 *
 * A rail against hostile input, and — unlike every other error here — **not** a
 * parity claim.  It used to be documented as one, on the theory that CPython's
 * encoder raises `RecursionError` at a comparable depth.  That was true before
 * 3.12; since 3.12 the C recursion limit is decoupled from
 * `sys.getrecursionlimit()`, and measurement on CPython 3.14.6 (this repo's
 * interpreter, `sys.getrecursionlimit() == 1000`, no `setrecursionlimit` call)
 * says otherwise:
 *
 * | nesting | CPython 3.14.6      | canon             |
 * | ------- | ------------------- | ----------------- |
 * | 1000    | 2000 bytes          | 2000 bytes        |
 * | 1001    | 2002 bytes          | `MaxDepthExceeded`|
 * | 100000  | 200000 bytes        | `MaxDepthExceeded`|
 * | 150000  | `RecursionError`    | `MaxDepthExceeded`|
 *
 * So documents nested 1001..~150000 deep *do* have a CPython encoding that this
 * writer refuses.  That is deliberate.  {@link write} is genuinely recursive on
 * the JS stack, and the stack is where it ends: under Bun 1.4 a
 * `RangeError: Maximum call stack size exceeded` — a throw, not a value —
 * arrives somewhere around 8000 nested objects, and the exact figure moves with
 * the engine, the shape of the document and how much stack the caller already
 * burned.  A limit an order of magnitude below the shallowest measured ceiling
 * is the only way this module can keep its promise never to throw.  1000 is
 * also far above anything the protocol carries: the deepest packet in the
 * fixture corpus nests single digits.
 *
 * The bound is therefore canon's own, not Python's, and the two disagree above
 * 1000 by design.  Raising it is not free — {@link pathOf} and the error
 * messages built from it are linear in the depth, so the rail keeps the
 * *failure* path bounded too.
 */
export class MaxDepthExceededError extends Data.TaggedError('MaxDepthExceeded')<{
  readonly path: CanonPath;
  readonly limit: number;
  readonly message: string;
}> {}

/**
 * A runtime value outside {@link CanonValue} — `undefined`, a function, a
 * symbol.  Unreachable through the types, reachable through `JSON.parse` of
 * something unexpected, so it is an error value rather than a bad encoding.
 */
export class UnsupportedValueError extends Data.TaggedError('UnsupportedValue')<{
  readonly path: CanonPath;
  /** The offending `typeof`. */
  readonly kind: string;
  readonly message: string;
}> {}

/** Why a document has no canonical encoding. */
export type CanonError =
  | NonFiniteFloatError
  | LoneSurrogateError
  | MaxDepthExceededError
  | UnsupportedValueError;

/** Deepest nesting this writer will encode. */
export const CANON_MAX_DEPTH = 1000;

const nonFiniteFloat = (link: PathLink | null, value: number): NonFiniteFloatError => {
  const path = pathOf(link);
  const name = Number.isNaN(value) ? 'NaN' : value > 0 ? 'Infinity' : '-Infinity';
  return new NonFiniteFloatError({
    path,
    value,
    message: `${formatCanonPath(path)}: ${name} has no JSON spelling under allow_nan=False`,
  });
};

const loneSurrogate = (
  link: PathLink | null,
  index: number,
  unit: number,
): LoneSurrogateError => {
  const path = pathOf(link);
  const hex = unit.toString(16).padStart(4, '0');
  return new LoneSurrogateError({
    path,
    index,
    unit,
    message: `${formatCanonPath(path)}: unpaired surrogate U+${hex.toUpperCase()} at index ${index} cannot be encoded as UTF-8`,
  });
};

const maxDepthExceeded = (link: PathLink | null): MaxDepthExceededError => {
  const path = pathOf(link);
  return new MaxDepthExceededError({
    path,
    limit: CANON_MAX_DEPTH,
    message: `${formatCanonPath(path)}: nesting deeper than ${CANON_MAX_DEPTH}`,
  });
};

const unsupportedValue = (link: PathLink | null, kind: string): UnsupportedValueError => {
  const path = pathOf(link);
  return new UnsupportedValueError({
    path,
    kind,
    message: `${formatCanonPath(path)}: ${kind} is not a JSON value`,
  });
};

// ---------------------------------------------------------------------------
// Key order
// ---------------------------------------------------------------------------

const ANY_SURROGATE = /[\ud800-\udfff]/;

const compareByCodePoint = (left: string, right: string): number => {
  const mine = Array.from(left, (character) => character.codePointAt(0) ?? 0);
  const theirs = Array.from(right, (character) => character.codePointAt(0) ?? 0);
  const at = mine.findIndex((point, index) => point !== theirs[index]);
  if (at === -1) return mine.length - theirs.length;
  const other = theirs[at];
  // `at` came from `mine`, so the `?? 0` is unreachable padding for the
  // compiler; `other` really can be absent, when `right` is a prefix.
  return other === undefined ? 1 : (mine[at] ?? 0) - other;
};

/**
 * Compare the way Python orders `str`: by code point.
 *
 * JS compares UTF-16 code units, so `"\uffff" < "\u{10000}"` is `false` in JS
 * and `True` in Python — `sort_keys=True` over any astral key would diverge.
 * Below U+D800 the two orders agree, so strings without surrogates take the
 * native comparison and only astral keys pay for the decode.
 */
export const compareCodePoints = (left: string, right: string): number => {
  if (left === right) return 0;
  return ANY_SURROGATE.test(left) || ANY_SURROGATE.test(right)
    ? compareByCodePoint(left, right)
    : left < right
      ? -1
      : 1;
};

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

const SHORT_ESCAPES: ReadonlyMap<string, string> = new Map([
  ['"', '\\"'],
  ['\\', '\\\\'],
  ['\b', '\\b'],
  ['\f', '\\f'],
  ['\n', '\\n'],
  ['\r', '\\r'],
  ['\t', '\\t'],
]);

const hex4 = (unit: number): string => `\\u${unit.toString(16).padStart(4, '0')}`;

const escapeMatch = (chunk: string): string => {
  const short = SHORT_ESCAPES.get(chunk);
  if (short !== undefined) return short;
  // With the `u` flag an astral character arrives whole; CPython emits its
  // surrogate pair as two `\uXXXX` escapes, which is what the UTF-16 units
  // already are.  Everything else is a single unit.
  return chunk.length === 1
    ? hex4(chunk.charCodeAt(0))
    : `${hex4(chunk.charCodeAt(0))}${hex4(chunk.charCodeAt(1))}`;
};

/**
 * `json.encoder.py_encode_basestring_ascii` — everything outside 0x20..0x7E,
 * DEL included: CPython's fast path stops at `~`.
 */
const ASCII_ESCAPABLE = /["\\]|[^\x20-\x7e]/gu;

/**
 * `json.encoder.py_encode_basestring` — quote, backslash and the C0 controls,
 * nothing else.  Matching control characters is the entire job, so the
 * `no-control-regex` advice does not apply.
 */
// oxlint-disable-next-line no-control-regex
const UTF8_ESCAPABLE = /["\\\u0000-\u001f]/gu;

/** An unpaired high or low surrogate, or `null`.  Deliberately not global. */
const LONE_SURROGATE = /[\ud800-\udbff](?![\udc00-\udfff])|(?<![\ud800-\udbff])[\udc00-\udfff]/;

const escapedText = (value: string, ensureAscii: boolean): string =>
  `"${value.replace(ensureAscii ? ASCII_ESCAPABLE : UTF8_ESCAPABLE, escapeMatch)}"`;

const encodeStringAt = (
  value: string,
  ensureAscii: boolean,
  link: PathLink | null,
): Either.Either<string, LoneSurrogateError> => {
  if (ensureAscii) return Either.right(escapedText(value, true));
  // UTF-8 output only: in ASCII mode a lone surrogate leaves as `\udXXX` and
  // the bytes stay legal, exactly as CPython does it.
  const found = LONE_SURROGATE.exec(value);
  return found === null
    ? Either.right(escapedText(value, false))
    : Either.left(loneSurrogate(link, found.index, value.charCodeAt(found.index)));
};

/**
 * One JSON string literal, quotes included.
 *
 * Exported because keys, digest inputs and error messages all need CPython's
 * escaping without going through a whole document.
 */
export const encodeString = (
  value: string,
  options: CanonOptions,
): Either.Either<string, LoneSurrogateError> =>
  encodeStringAt(value, options.ensureAscii, null);

/** The first unpaired surrogate in `text`, if any. */
export const findLoneSurrogate = (
  text: string,
): Option.Option<{ readonly index: number; readonly unit: number }> => {
  const found = LONE_SURROGATE.exec(text);
  return found === null
    ? Option.none()
    : Option.some({ index: found.index, unit: text.charCodeAt(found.index) });
};

/**
 * `str.encode("utf-8", "strict")`: bytes, or the unpaired surrogate that makes
 * the string unencodable.  `TextEncoder` alone would substitute U+FFFD and
 * silently change the digest.
 */
export const encodeUtf8 = (text: string): Either.Either<Uint8Array, LoneSurrogateError> =>
  Option.match(findLoneSurrogate(text), {
    onNone: () => Either.right(ENCODER.encode(text)),
    onSome: ({ index, unit }) => Either.left(loneSurrogate(null, index, unit)),
  });

// ---------------------------------------------------------------------------
// Floats
// ---------------------------------------------------------------------------

/**
 * `repr(float)` — shortest round-tripping digits in CPython's layout.
 *
 * CPython's `format_float_short` (mode `'r'`) picks exponential notation when
 * the decimal point position `decpt` satisfies `decpt <= -4 || decpt > 16`,
 * pads the exponent to two digits with an explicit sign, and always leaves a
 * fractional part in fixed notation (`1.0`, never `1`).  The digits themselves
 * come from `toExponential()`, whose no-argument form is specified to produce
 * the shortest string that identifies the double — the same contract as
 * CPython's shortest-repr.
 *
 * `None` for `NaN` and the infinities, which `allow_nan=False` rejects.
 */
export const formatFloat = (value: number): Option.Option<string> => {
  if (!Number.isFinite(value)) return Option.none();
  const negative = value < 0 || Object.is(value, -0);
  const parts = Math.abs(value).toExponential().split('e');
  const digits = (parts[0] ?? '0').replace('.', '');
  const decpt = Number(parts[1] ?? '0') + 1;
  const sign = negative ? '-' : '';
  if (decpt <= -4 || decpt > 16) {
    const mantissa = digits.length > 1 ? `${digits[0]}.${digits.slice(1)}` : digits;
    const exponent = decpt - 1;
    const exponentSign = exponent < 0 ? '-' : '+';
    const magnitude = Math.abs(exponent).toString().padStart(2, '0');
    return Option.some(`${sign}${mantissa}e${exponentSign}${magnitude}`);
  }
  if (decpt <= 0) return Option.some(`${sign}0.${'0'.repeat(-decpt)}${digits}`);
  if (decpt >= digits.length) {
    return Option.some(`${sign}${digits}${'0'.repeat(decpt - digits.length)}.0`);
  }
  return Option.some(`${sign}${digits.slice(0, decpt)}.${digits.slice(decpt)}`);
};

// ---------------------------------------------------------------------------
// The writer
// ---------------------------------------------------------------------------

const ENCODER = new TextEncoder();

const RIGHT_NULL: Either.Either<string, CanonError> = Either.right('null');
const RIGHT_TRUE: Either.Either<string, CanonError> = Either.right('true');
const RIGHT_FALSE: Either.Either<string, CanonError> = Either.right('false');

/**
 * `Array.isArray` does not narrow the negative branch of a `ReadonlyArray`
 * union, so the object case gets an explicit guard.
 */
const isCanonRecord = (value: readonly CanonValue[] | CanonRecord): value is CanonRecord =>
  !Array.isArray(value);

/** `typeof` of something the types say cannot exist. */
const kindOf = (value: unknown): string => typeof value;

const write = (
  value: CanonValue,
  ensureAscii: boolean,
  link: PathLink | null,
  depth: number,
): Either.Either<string, CanonError> => {
  if (value === null) return RIGHT_NULL;
  switch (typeof value) {
    case 'boolean':
      return value ? RIGHT_TRUE : RIGHT_FALSE;
    case 'bigint':
      return Either.right(value.toString(10));
    case 'number':
      return Option.match(formatFloat(value), {
        onNone: () => Either.left(nonFiniteFloat(link, value)),
        onSome: (text) => Either.right(text),
      });
    case 'string':
      return encodeStringAt(value, ensureAscii, link);
    case 'object':
      return writeContainer(value, ensureAscii, link, depth);
    default:
      return Either.left(unsupportedValue(link, kindOf(value)));
  }
};

const writeContainer = (
  value: readonly CanonValue[] | CanonRecord,
  ensureAscii: boolean,
  link: PathLink | null,
  depth: number,
): Either.Either<string, CanonError> => {
  if (depth >= CANON_MAX_DEPTH) return Either.left(maxDepthExceeded(link));
  if (isCanonRecord(value)) {
    const entries = Object.entries(value)
      .toSorted((left, right) => compareCodePoints(left[0], right[0]))
      .map(([key, item]) => {
        const child: PathLink = { segment: key, parent: link };
        return Either.zipWith(
          encodeStringAt(key, ensureAscii, child),
          write(item, ensureAscii, child, depth + 1),
          (encodedKey, encodedValue) => `${encodedKey}:${encodedValue}`,
        );
      });
    return Either.map(Either.all(entries), (parts) => `{${parts.join(',')}}`);
  }
  // `Array.from`, not `.map`: `map` *skips* holes and reproduces them in the
  // result, so a sparse array (`[1, , 3]`) would leave `undefined` slots for
  // `Either.all` to dereference — a throw, not a value.  `Array.from`
  // materializes each hole as `undefined`, which lands in the
  // `UnsupportedValue` branch of `write` the same way an explicit `undefined`
  // does.  Python has no hole to be identical to; refusing is the parity.
  const items = Array.from(value, (item, index) =>
    write(item, ensureAscii, { segment: index, parent: link }, depth + 1),
  );
  return Either.map(Either.all(items), (parts) => `[${parts.join(',')}]`);
};

/**
 * The canonical text of a document:
 * `json.dumps(value, ensure_ascii=?, allow_nan=False, sort_keys=True,
 * separators=(",", ":"))`.
 *
 * In `ensureAscii` mode the result is pure ASCII by construction.
 */
export const canonicalText = (
  value: CanonValue,
  options: CanonOptions,
): Either.Either<string, CanonError> => write(value, options.ensureAscii, null, 0);

/**
 * The canonical bytes: {@link canonicalText} through `.encode("ascii")` or
 * `.encode("utf-8")`, whichever the mode implies.  These are the bytes every
 * digest in the protocol is taken over.
 */
export const canonicalBytes = (
  value: CanonValue,
  options: CanonOptions,
): Either.Either<Uint8Array, CanonError> =>
  Either.map(canonicalText(value, options), (text) => ENCODER.encode(text));
