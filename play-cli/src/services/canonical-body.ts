/**
 * The byte-stable idempotency payload, and the Python number model it needs.
 *
 * Ports `_canonical_body` (client.py:8328-8335), the `json.loads` half of
 * `_parse_json_object` (6521-6541), `_json_value` (1277-1300) as it applies to
 * a parsed argument tree, and the `float`/`int` branches of `_scalar`
 * (3442-3452).
 *
 * A `CommandBatch` is persisted into `.v2-state` *before* it is sent, and the
 * bytes written there are the bytes `retry` re-sends — never a re-serialization
 * of a re-decoded object.  That only works if serialization is a function of
 * the value alone, so four settings are load-bearing and none of them may
 * drift:
 *
 * - `sort_keys=True` — the same order built from two differently-ordered input
 *   objects must produce the same bytes, so the server sees one idempotency
 *   key, not two.
 * - `separators=(",", ":")` — no incidental whitespace to diff.
 * - `ensure_ascii=False` — a city named `München` is carried as UTF-8, not as
 *   `ü`.  Either policy is stable on its own; changing between them after
 *   a batch was persisted would change its bytes, which is why it is pinned
 *   here rather than left to a caller.
 * - **`int` is not `float`.**  CPython carries the two apart all the way from
 *   `json.loads` to `json.dumps`: `{"tax":40.0}` is persisted and POSTed as
 *   `{"tax":40.0}`, `1e16` as `1e+16`, `1e-7` as `1e-07`, and
 *   `10000000000000000001` exactly.  JavaScript has one number type and
 *   `String(value)` produces `40`, `10000000000000000`, `1e-7` and
 *   `10000000000000000000` for those four — a different idempotency key on the
 *   wire, a different record in `.v2-state`, and in the last case silently a
 *   different *value* on a mutation payload.  `--arguments` is agent input, so
 *   this is reachable from the command line; {@link PyInt} / {@link PyFloat}
 *   and {@link pyFloatRepr} keep the distinction CPython keeps.  See
 *   NOTES.md §17.9.
 *
 * There are **two** refusals, and both are `ValueError`s that `_canonical_body`
 * catches and re-raises as `command batch is not canonical JSON: …`:
 *
 * - `allow_nan=False`, because CPython would otherwise emit the non-standard
 *   `NaN`/`Infinity` tokens and a server that rejects them would turn a local
 *   encoding slip into an unresolved batch;
 * - the `.encode("utf-8")` that `_canonical_body` applies to the dumped text.
 *   `ensure_ascii=False` leaves a lone surrogate — which `--arguments` can carry,
 *   because `"\ud800"` is strict JSON and `json.loads` decodes it to one — in the
 *   string, and UTF-8 cannot encode one.  CPython raises `UnicodeEncodeError`
 *   (a `ValueError`) and the batch is neither persisted nor sent.  JavaScript's
 *   `TextEncoder` instead substitutes `U+FFFD` *silently*, which would send a
 *   mutation CPython refused, with a value the agent did not write, and leave a
 *   `.v2-state` record whose string is not the bytes that went out — the exact
 *   invariant this file exists to hold.  {@link canonicalText} refuses first, and
 *   the encoder is not exported, so there is no way to reach the substitution.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { playerError } from 'src/errors';
import { formatG } from 'src/render/primitives';
import { encodeStringAscii } from 'src/services/json-output';

// ---------------------------------------------------------------------------
// The Python JSON value model
// ---------------------------------------------------------------------------

/** A CPython `int`: arbitrary precision, and never spelled with a `.0`. */
export class PyInt {
  readonly _tag = 'PyInt';
  constructor(readonly value: bigint) {}
}

/** A CPython `float`: an IEEE double that always keeps its float spelling. */
export class PyFloat {
  readonly _tag = 'PyFloat';
  constructor(readonly value: number) {}
}

export type PyNumber = PyInt | PyFloat;

/**
 * Any value the wire can carry, with CPython's two numeric types kept apart.
 *
 * Plain `number` stays in the union so every `JsonValue` — every structure the
 * port itself builds, and every argument object a unit that has no Python
 * number model hands to {@link canonicalText} — is a `PyValue` already.
 */
export type PyValue =
  | null
  | boolean
  | number
  | string
  | PyNumber
  | ReadonlyArray<PyValue>
  | PyObject;

export interface PyObject {
  readonly [key: string]: PyValue;
}

type MutablePyObject = Record<string, PyValue>;

/** `int(text)` for a JSON integer literal — exact at any width. */
export const pyInt = (value: bigint): PyInt => new PyInt(value);

/** `float(text)` for a JSON literal that carried a fraction or an exponent. */
export const pyFloat = (value: number): PyFloat => new PyFloat(value);

const isPyNumber = (value: PyValue): value is PyNumber =>
  value instanceof PyInt || value instanceof PyFloat;

/** True for a mapping, and never for one of the two numeric wrappers. */
export const isPyMapping = (value: PyValue): value is PyObject =>
  typeof value === 'object' &&
  value !== null &&
  !Array.isArray(value) &&
  !isPyNumber(value);

// ---------------------------------------------------------------------------
// repr(float) — CPython's `float.__repr__`, which is what `json.dumps` emits
// ---------------------------------------------------------------------------

/**
 * `repr(value)` for a finite CPython float.
 *
 * The digits are the shortest that round-trip (JavaScript's own
 * `toExponential()` picks exactly those); everything else is CPython's
 * `format_float_short` layout, and every part of it differs from
 * `String(value)` somewhere:
 *
 * - fixed notation whenever the decimal point lands in `(-4, 16]`, always with
 *   at least one digit after it (`3.0`, never `3`);
 * - exponential notation outside that window, with a signed exponent of at
 *   least two digits (`1e+16`, `1e-05`) where JavaScript writes
 *   `10000000000000000` and `0.00001`.
 */
export const pyFloatRepr = (value: number): string => {
  if (Number.isNaN(value)) return 'NaN';
  if (!Number.isFinite(value)) return value > 0 ? 'Infinity' : '-Infinity';
  if (value === 0) return Object.is(value, -0) ? '-0.0' : '0.0';
  const sign = value < 0 ? '-' : '';
  const scientific = Math.abs(value).toExponential();
  const marker = scientific.indexOf('e');
  const exponent = Number.parseInt(scientific.slice(marker + 1), 10);
  const digits = scientific.slice(0, marker).replace('.', '');
  // `value = 0.digits × 10 ** point`, which is CPython's `decpt`.
  const point = exponent + 1;
  if (point <= -4 || point > 16) {
    const mantissa =
      digits.length === 1 ? digits : `${digits.charAt(0)}.${digits.slice(1)}`;
    const exponentSign = exponent < 0 ? '-' : '+';
    const magnitude = String(Math.abs(exponent)).padStart(2, '0');
    return `${sign}${mantissa}e${exponentSign}${magnitude}`;
  }
  if (point <= 0) return `${sign}0.${'0'.repeat(-point)}${digits}`;
  if (point >= digits.length) {
    return `${sign}${digits}${'0'.repeat(point - digits.length)}.0`;
  }
  return `${sign}${digits.slice(0, point)}.${digits.slice(point)}`;
};

/**
 * The number token for a value that carries no Python type of its own.
 *
 * Everything the port builds itself — `schema_version`, a revision's `turn`,
 * an order's integral coordinates — is an `int` in CPython, so an exact
 * integer prints as one.  Anything else can only have come from a float, and
 * gets `repr`.
 */
const plainNumberText = (value: number): string =>
  Number.isInteger(value) && Math.abs(value) <= Number.MAX_SAFE_INTEGER
    ? String(value)
    : pyFloatRepr(value);

const numberText = (value: PyNumber | number): string => {
  if (value instanceof PyInt) return value.value.toString();
  if (value instanceof PyFloat) return pyFloatRepr(value.value);
  return plainNumberText(value);
};

// ---------------------------------------------------------------------------
// json.dumps over the model
// ---------------------------------------------------------------------------

const sortKeysAscii = (keys: ReadonlyArray<string>): ReadonlyArray<string> =>
  [...keys].sort((left, right) => (left < right ? -1 : left > right ? 1 : 0));

/**
 * `json.dumps(value, sort_keys=True, separators=(",", ":"))`.
 *
 * Both call sites sort and pack; only `ensure_ascii` differs between the
 * canonical body (`False`) and `_scalar`'s container branch (the default).
 * String escaping is `json-output`'s `encodeStringAscii`, so there is exactly
 * one port of CPython's escape table.
 */
export const pyDumps = (value: PyValue, ensureAscii: boolean): string => {
  if (value === null) return 'null';
  if (typeof value === 'boolean') return value ? 'true' : 'false';
  if (typeof value === 'number' || isPyNumber(value)) return numberText(value);
  if (typeof value === 'string') return encodeStringAscii(value, ensureAscii);
  if (Array.isArray(value)) {
    return `[${value.map((item) => pyDumps(item, ensureAscii)).join(',')}]`;
  }
  if (isPyMapping(value)) {
    const parts = sortKeysAscii(Object.keys(value)).map((key) => {
      const item = value[key];
      const encoded = item === undefined ? 'null' : pyDumps(item, ensureAscii);
      return `${encodeStringAscii(key, ensureAscii)}:${encoded}`;
    });
    return `{${parts.join(',')}}`;
  }
  return 'null';
};

/** `_scalar` (client.py:3442-3452), over a value that knows its Python type. */
export const pyScalar = (value: PyValue): string => {
  if (value === null) return '-';
  if (typeof value === 'boolean') return value ? 'yes' : 'no';
  if (value instanceof PyFloat) return formatG(value.value);
  if (value instanceof PyInt) return value.value.toString();
  if (typeof value === 'number') {
    return Number.isInteger(value) ? String(value) : formatG(value);
  }
  if (typeof value === 'string') return value;
  return pyDumps(value, true);
};

// ---------------------------------------------------------------------------
// json.loads, with the two numeric types kept apart
// ---------------------------------------------------------------------------

const UNESCAPES: ReadonlyMap<string, string> = new Map([
  ['"', '"'],
  ['\\', '\\'],
  ['/', '/'],
  ['b', '\b'],
  ['f', '\f'],
  ['n', '\n'],
  ['r', '\r'],
  ['t', '\t'],
]);

const WHITESPACE: ReadonlySet<string> = new Set([' ', '\t', '\n', '\r']);

const HEX_RE = /^[0-9A-Fa-f]{4}$/;

/**
 * How deep a document the parser will descend before it stops.
 *
 * `_json_value` refuses anything past 12, so every document this cap can
 * reject was already refused with *this* sentence — the cap only decides
 * whether the refusal arrives before or after a stack that CPython's own
 * scanner would answer with `RecursionError`.
 */
const PARSE_MAX_DEPTH = 200;

/**
 * Why a parse produced nothing, in the shape the caller has to print.
 *
 * `decode` and `constant` both become `… must be valid strict JSON: {message}`
 * because CPython raises a `JSONDecodeError` (a `ValueError`) for the first and
 * the `parse_constant` hook's own `ValueError` for the second, and
 * `_parse_json_object` catches the pair under one `except`.  `duplicate` is the
 * `object_pairs_hook`'s `PlayerError`, which is a `RuntimeError` and therefore
 * escapes that `except` with its own sentence.
 */
export type PyParseFailure =
  | { readonly _tag: 'decode'; readonly message: string }
  | { readonly _tag: 'constant'; readonly message: string }
  | { readonly _tag: 'duplicate' }
  | { readonly _tag: 'too_deep' };

/** What a parse produced, and what was wrong with the text that produced it. */
export interface PyParse {
  readonly value: PyValue;
  readonly failure: PyParseFailure | null;
}

export interface PyParseOptions {
  /**
   * Install `_parse_json_object`'s two hooks: `object_pairs_hook`, which refuses
   * a repeated key, and `parse_constant`, which refuses `NaN`/`Infinity`.
   *
   * `_batch_intent` calls a *bare* `json.loads`, so it leaves them off: a
   * repeated key is last-wins and the three non-standard tokens decode to
   * floats, because refusing either would only lose a name it could have
   * printed.
   */
  readonly hooks?: boolean;
}

interface Scan {
  readonly value: PyValue;
  readonly end: number;
}

const isDigit = (character: string | undefined): boolean =>
  character !== undefined && character >= '0' && character <= '9';

/**
 * `json.loads`, ported down to its error text.
 *
 * Three things the host `JSON.parse` cannot do are the reason this exists:
 *
 * 1. **It collapses `int` and `float`.**  The literal is the only place the
 *    distinction survives, so the scan reads it: a fraction or an exponent
 *    makes a `float`, and anything else an exact `int` — CPython's own rule,
 *    from `json.scanner`.
 * 2. **It keeps the last of two identical keys**, which would make
 *    `{"name":"A","name":"B"}` a *silent* choice of `B` — exactly the kind of
 *    guess this client never makes.
 * 3. **Its syntax complaint is V8's sentence, not CPython's.**  `--arguments`
 *    is agent input, so a typo is the single most reachable refusal in the
 *    unit, and `{"a":}` has to answer `Expecting value: line 1 column 6
 *    (char 5)` — not `JSON Parse error: Unexpected token '}'`.  Every message,
 *    and every `line`/`column`/`char` in it, is produced here.
 *
 * The messages and positions are the **C** `_json` accelerator's, which is what
 * `import json` actually runs: it reports `Invalid \escape` at the backslash
 * (the pure-Python fallback says `Invalid \escape: 'q'` one character later) and
 * `Invalid control character at` without the character's `repr`.  Positions are
 * *code point* offsets, as they are in CPython, so an astral character in the
 * document counts once and not twice — hence the scan runs over `[...text]`.
 *
 * See NOTES.md §17.1 for the CPython version this is pinned to.
 */
export const parsePython = (text: string, options: PyParseOptions = {}): PyParse => {
  const hooks = options.hooks === true;
  const points = [...text];
  const size = points.length;
  let failure: PyParseFailure | null = null;

  /** `JSONDecodeError.__init__`'s `line %d column %d (char %d)` tail. */
  const errorText = (message: string, position: number): string => {
    let line = 1;
    let lastNewline = -1;
    for (let scan = 0; scan < position && scan < size; scan += 1) {
      if (points[scan] === '\n') {
        line += 1;
        lastNewline = scan;
      }
    }
    return `${message}: line ${line} column ${position - lastNewline} (char ${position})`;
  };

  /** The first failure wins, exactly as the first raised exception would. */
  const fail = (next: PyParseFailure): void => {
    if (failure === null) failure = next;
  };

  const decodeFail = (message: string, position: number): void =>
    fail({ _tag: 'decode', message: errorText(message, position) });

  const skip = (from: number): number => {
    let scan = from;
    for (;;) {
      const character = points[scan];
      if (character === undefined || !WHITESPACE.has(character)) return scan;
      scan += 1;
    }
  };

  const matches = (word: string, position: number): boolean => {
    for (let offset = 0; offset < word.length; offset += 1) {
      if (points[position + offset] !== word.charAt(offset)) return false;
    }
    return true;
  };

  /**
   * `_decode_uXXXX` — `position` is the index of the `u`, as CPython's is.
   *
   * The bound is `>=`, not `>`: the C scanner wants a character *after* the four
   * digits, so a document that ends on `"A` is an `Invalid \uXXXX escape`
   * and not the `Unterminated string` the digits alone would suggest.
   */
  const decodeHex = (position: number): number | null => {
    const digits = points.slice(position + 1, position + 5).join('');
    if (position + 5 >= size || !HEX_RE.test(digits)) {
      decodeFail('Invalid \\uXXXX escape', position);
      return null;
    }
    return Number.parseInt(digits, 16);
  };

  /** `scanstring` — `begin` is the index of the opening quote. */
  const scanString = (begin: number): Scan => {
    let end = begin + 1;
    let out = '';
    for (;;) {
      // `STRINGCHUNK`: run to the next quote, backslash or control character.
      let cursor = end;
      for (;;) {
        const character = points[cursor];
        if (character === undefined) break;
        if (character === '"' || character === '\\') break;
        if ((character.codePointAt(0) ?? 0) < 0x20) break;
        cursor += 1;
      }
      if (cursor >= size) {
        decodeFail('Unterminated string starting at', begin);
        return { value: null, end: size };
      }
      out += points.slice(end, cursor).join('');
      const terminator = points[cursor];
      end = cursor + 1;
      if (terminator === '"') return { value: out, end };
      if (terminator !== '\\') {
        decodeFail('Invalid control character at', cursor);
        return { value: null, end };
      }
      const escape = points[end];
      if (escape === undefined) {
        decodeFail('Unterminated string starting at', begin);
        return { value: null, end: size };
      }
      if (escape !== 'u') {
        const plain = UNESCAPES.get(escape);
        if (plain === undefined) {
          decodeFail('Invalid \\escape', cursor);
          return { value: null, end };
        }
        out += plain;
        end += 1;
        continue;
      }
      const lead = decodeHex(end);
      if (lead === null) return { value: null, end };
      end += 5;
      if (lead >= 0xd800 && lead <= 0xdbff && points[end] === '\\' && points[end + 1] === 'u') {
        const trail = decodeHex(end + 1);
        if (trail === null) return { value: null, end };
        if (trail >= 0xdc00 && trail <= 0xdfff) {
          out += String.fromCodePoint(0x10000 + (((lead - 0xd800) << 10) | (trail - 0xdc00)));
          end += 6;
          continue;
        }
      }
      // `chr(uni)` — a lead with no trail stays the lone surrogate it is, which
      // is why `canonicalText` has to refuse one later.
      out += String.fromCharCode(lead);
    }
  };

  /** `NUMBER_RE`, split so the `int`/`float` choice is the literal's own. */
  const scanNumber = (start: number): Scan | null => {
    let cursor = start;
    if (points[cursor] === '-') cursor += 1;
    const lead = points[cursor];
    if (lead === '0') {
      cursor += 1;
    } else if (isDigit(lead) && lead !== '0') {
      while (isDigit(points[cursor])) cursor += 1;
    } else {
      return null;
    }
    let isFloat = false;
    if (points[cursor] === '.' && isDigit(points[cursor + 1])) {
      cursor += 2;
      while (isDigit(points[cursor])) cursor += 1;
      isFloat = true;
    }
    const marker = points[cursor];
    if (marker === 'e' || marker === 'E') {
      let scan = cursor + 1;
      const sign = points[scan];
      if (sign === '+' || sign === '-') scan += 1;
      if (isDigit(points[scan])) {
        while (isDigit(points[scan])) scan += 1;
        cursor = scan;
        isFloat = true;
      }
    }
    const literal = points.slice(start, cursor).join('');
    return {
      value: isFloat ? pyFloat(Number(literal)) : pyInt(BigInt(literal)),
      end: cursor,
    };
  };

  /** `parse_constant` — a refusal with the hooks on, a float with them off. */
  const scanConstant = (token: string, value: number, end: number): Scan => {
    if (hooks) {
      fail({ _tag: 'constant', message: `non-finite number ${token}` });
      return { value: null, end };
    }
    return { value: pyFloat(value), end };
  };

  const scanArray = (start: number, depth: number): Scan => {
    if (depth > PARSE_MAX_DEPTH) {
      fail({ _tag: 'too_deep' });
      return { value: null, end: start };
    }
    const items: PyValue[] = [];
    let end = skip(start);
    if (points[end] === ']') return { value: items, end: end + 1 };
    for (;;) {
      const item = scanValue(end, depth + 1);
      if (failure !== null) return { value: null, end: item.end };
      items.push(item.value);
      end = skip(item.end);
      const next = points[end];
      end += 1;
      if (next === ']') return { value: items, end };
      if (next !== ',') {
        decodeFail("Expecting ',' delimiter", end - 1);
        return { value: null, end };
      }
      const comma = end - 1;
      end = skip(end);
      if (points[end] === ']') {
        decodeFail('Illegal trailing comma before end of array', comma);
        return { value: null, end };
      }
    }
  };

  const scanObject = (start: number, depth: number): Scan => {
    if (depth > PARSE_MAX_DEPTH) {
      fail({ _tag: 'too_deep' });
      return { value: null, end: start };
    }
    // `defineProperty`, not assignment: a literal `__proto__` key is an
    // argument name like any other, and must not become a prototype.
    const out: MutablePyObject = {};
    const seen = new Set<string>();
    let repeated = false;
    let end = skip(start);
    if (points[end] === '}') return { value: out, end: end + 1 };
    if (points[end] !== '"') {
      decodeFail('Expecting property name enclosed in double quotes', end);
      return { value: null, end };
    }
    for (;;) {
      const scanned = scanString(end);
      if (failure !== null) return { value: null, end: scanned.end };
      const key = typeof scanned.value === 'string' ? scanned.value : '';
      end = skip(scanned.end);
      if (points[end] !== ':') {
        decodeFail("Expecting ':' delimiter", end);
        return { value: null, end };
      }
      end = skip(end + 1);
      const item = scanValue(end, depth + 1);
      if (failure !== null) return { value: null, end: item.end };
      if (seen.has(key)) repeated = true;
      seen.add(key);
      Object.defineProperty(out, key, {
        value: item.value,
        enumerable: true,
        writable: true,
        configurable: true,
      });
      end = skip(item.end);
      const next = points[end];
      end += 1;
      if (next === '}') {
        // `object_pairs_hook` runs when the object *closes*, which is why
        // `{"a":1,"a":2,}` is a trailing comma and not a duplicate key.
        if (hooks && repeated) {
          fail({ _tag: 'duplicate' });
          return { value: null, end };
        }
        return { value: out, end };
      }
      if (next !== ',') {
        decodeFail("Expecting ',' delimiter", end - 1);
        return { value: null, end };
      }
      const comma = end - 1;
      end = skip(end);
      const following = points[end];
      end += 1;
      if (following !== '"') {
        if (following === '}') {
          decodeFail('Illegal trailing comma before end of object', comma);
        } else {
          decodeFail('Expecting property name enclosed in double quotes', end - 1);
        }
        return { value: null, end };
      }
      end -= 1;
    }
  };

  const scanValue = (start: number, depth: number): Scan => {
    const character = points[start];
    if (character === undefined) {
      decodeFail('Expecting value', start);
      return { value: null, end: start };
    }
    if (character === '"') return scanString(start);
    if (character === '{') return scanObject(start + 1, depth);
    if (character === '[') return scanArray(start + 1, depth);
    if (character === 'n' && matches('null', start)) return { value: null, end: start + 4 };
    if (character === 't' && matches('true', start)) return { value: true, end: start + 4 };
    if (character === 'f' && matches('false', start)) return { value: false, end: start + 5 };
    const number = scanNumber(start);
    if (number !== null) return number;
    if (character === 'N' && matches('NaN', start)) {
      return scanConstant('NaN', Number.NaN, start + 3);
    }
    if (character === 'I' && matches('Infinity', start)) {
      return scanConstant('Infinity', Number.POSITIVE_INFINITY, start + 8);
    }
    if (character === '-' && matches('-Infinity', start)) {
      return scanConstant('-Infinity', Number.NEGATIVE_INFINITY, start + 9);
    }
    decodeFail('Expecting value', start);
    return { value: null, end: start };
  };

  const scanned = scanValue(skip(0), 0);
  if (failure === null) {
    // `JSONDecoder.decode`: trailing anything but whitespace is `Extra data`.
    const end = skip(scanned.end);
    if (end !== size) decodeFail('Extra data', end);
  }
  const settled: PyParseFailure | null = failure;
  return settled === null
    ? { value: scanned.value, failure: null }
    : { value: null, failure: settled };
};

// ---------------------------------------------------------------------------
// _json_value (client.py:1277-1300)
// ---------------------------------------------------------------------------

const JSON_MAX_DEPTH = 12;
const JSON_MAX_ITEMS = 8192;
const JSON_MAX_KEYS = 2048;
const JSON_MAX_KEY_BYTES = 128;

type PyCheck =
  | { readonly ok: true; readonly value: PyValue }
  | { readonly ok: false; readonly detail: string };

const checked = (value: PyValue, depth: number): PyCheck => {
  if (depth > JSON_MAX_DEPTH) return { ok: false, detail: 'JSON is nested too deeply' };
  if (value === null || typeof value === 'string' || typeof value === 'boolean') {
    return { ok: true, value };
  }
  if (value instanceof PyInt) return { ok: true, value };
  if (value instanceof PyFloat || typeof value === 'number') {
    const raw = value instanceof PyFloat ? value.value : value;
    return Number.isFinite(raw)
      ? { ok: true, value }
      : { ok: false, detail: 'number is not finite' };
  }
  if (Array.isArray(value)) {
    if (value.length > JSON_MAX_ITEMS) return { ok: false, detail: 'too many items' };
    const items: PyValue[] = [];
    for (const item of value) {
      const decoded = checked(item, depth + 1);
      if (!decoded.ok) return decoded;
      items.push(decoded.value);
    }
    return { ok: true, value: items };
  }
  if (isPyMapping(value)) {
    const keys = Object.keys(value);
    if (
      keys.length > JSON_MAX_KEYS ||
      keys.some((key) => key.length === 0 || key.length > JSON_MAX_KEY_BYTES)
    ) {
      return { ok: false, detail: 'invalid object' };
    }
    const out: MutablePyObject = {};
    for (const key of keys) {
      const item = value[key];
      const decoded = checked(item === undefined ? null : item, depth + 1);
      if (!decoded.ok) return decoded;
      Object.defineProperty(out, key, {
        value: decoded.value,
        enumerable: true,
        writable: true,
        configurable: true,
      });
    }
    return { ok: true, value: out };
  }
  return { ok: false, detail: 'non-JSON value' };
};

/** `_json_value` — copy a value, refusing anything the wire may not carry. */
export const pyJsonValue = (
  value: PyValue,
  label: string
): Effect.Effect<PyValue, PlayerError> => {
  const decoded = checked(value, 0);
  return decoded.ok
    ? Effect.succeed(decoded.value)
    : Effect.fail(playerError(`invalid ${label}: ${decoded.detail}`));
};

/** {@link pyJsonValue} narrowed to an object, for an argument tree. */
export const pyJsonObject = (
  value: PyValue,
  label: string
): Effect.Effect<PyObject, PlayerError> =>
  Effect.flatMap(pyJsonValue(value, label), (copied) =>
    isPyMapping(copied) ? Effect.succeed(copied) : Effect.fail(playerError(`invalid ${label}`))
  );

// ---------------------------------------------------------------------------
// _canonical_body
// ---------------------------------------------------------------------------

/** CPython's `ValueError` text for `allow_nan=False`, before its value. */
export const NON_FINITE_DETAIL = 'Out of range float values are not JSON compliant';

/** `repr()` for the three floats `allow_nan=False` refuses. */
const nonFiniteRepr = (value: number): string =>
  Number.isNaN(value) ? 'nan' : value > 0 ? 'inf' : '-inf';

/**
 * The first non-finite float `json.dumps` would reach, in *emission* order.
 *
 * Which one it is shows up in the message, and `sort_keys=True` decides the
 * order the encoder walks a mapping in, so the walk sorts too.
 */
const firstNonFinite = (value: PyValue): number | null => {
  if (value instanceof PyInt) return null;
  const raw =
    value instanceof PyFloat ? value.value : typeof value === 'number' ? value : null;
  if (raw !== null) return Number.isFinite(raw) ? null : raw;
  if (Array.isArray(value)) {
    for (const item of value) {
      const bad = firstNonFinite(item);
      if (bad !== null) return bad;
    }
    return null;
  }
  if (isPyMapping(value)) {
    for (const key of sortKeysAscii(Object.keys(value))) {
      const bad = firstNonFinite(value[key] ?? null);
      if (bad !== null) return bad;
    }
    return null;
  }
  return null;
};

interface SurrogateRun {
  readonly start: number;
  readonly length: number;
}

/**
 * The first run of surrogates in the dumped text, as UTF-8's encoder finds it.
 *
 * Iterating code points is what makes the position CPython's: a valid pair is
 * one astral character and never a surrogate, a lone one is a surrogate and
 * counts once, and the codec batches a *maximal run* of them into one error.
 */
const surrogateRun = (text: string): SurrogateRun | null => {
  const points = [...text];
  const isSurrogate = (index: number): boolean => {
    const point = points[index]?.codePointAt(0);
    return point !== undefined && point >= 0xd800 && point <= 0xdfff;
  };
  for (let index = 0; index < points.length; index += 1) {
    if (!isSurrogate(index)) continue;
    let length = 1;
    while (isSurrogate(index + length)) length += 1;
    return { start: index, length };
  }
  return null;
};

/** `UnicodeEncodeError.__str__`, which has a singular and a plural form. */
const surrogateDetail = (text: string, run: SurrogateRun): string => {
  const tail = 'surrogates not allowed';
  if (run.length > 1) {
    return `'utf-8' codec can't encode characters in position ${run.start}-${
      run.start + run.length - 1
    }: ${tail}`;
  }
  const point = [...text][run.start]?.codePointAt(0) ?? 0;
  const escaped = `\\u${point.toString(16).padStart(4, '0')}`;
  return `'utf-8' codec can't encode character '${escaped}' in position ${run.start}: ${tail}`;
};

/**
 * `_canonical_body(...).decode("utf-8")` — the string form kept in `.v2-state`.
 *
 * The persisted string *is* the request body: `submitBatch` sends it verbatim,
 * so nothing between here and the socket may re-encode it.  Both of
 * `_canonical_body`'s `ValueError`s are raised here rather than at the encoder,
 * because everything downstream — the `.v2-state` write, the POST — must not
 * happen at all when either does.  `allow_nan` is checked first because the
 * encoder reaches it first: `json.dumps` raises while walking the value, and the
 * `.encode("utf-8")` that finds a surrogate only runs on a string it produced.
 */
export const canonicalText = (value: PyValue): Effect.Effect<string, PlayerError> => {
  const refuse = (detail: string): Effect.Effect<string, PlayerError> =>
    Effect.fail(playerError(`command batch is not canonical JSON: ${detail}`));
  const nonFinite = firstNonFinite(value);
  if (nonFinite !== null) {
    return refuse(`${NON_FINITE_DETAIL}: ${nonFiniteRepr(nonFinite)}`);
  }
  const text = pyDumps(value, false);
  const run = surrogateRun(text);
  return run === null ? Effect.succeed(text) : refuse(surrogateDetail(text, run));
};

/**
 * `_canonical_body` — the exact bytes an idempotent resend must reproduce.
 *
 * `TextEncoder` is deliberately not exported on its own: it answers a lone
 * surrogate with `U+FFFD` instead of refusing, so the only way to reach it is
 * behind {@link canonicalText}, which has already refused one.
 */
export const canonicalBody = (value: PyValue): Effect.Effect<Uint8Array, PlayerError> =>
  Effect.map(canonicalText(value), (text) => new TextEncoder().encode(text));
