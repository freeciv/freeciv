/**
 * Wire-payload primitives.
 *
 * Ports `play/client.py` 1247-1300 (`_exact`, `_opaque`, `_json_value`),
 * 2295-2305 (`_safe_number`) and 1010-1056 (`_validate_evaluation_context`,
 * which `_validate_health` and `_v2_session` both need and so cannot live in a
 * command unit).
 *
 * Decoded values keep the wire's own snake_case keys.  That is deliberate:
 * `--json` prints the *validated* envelope, so the decoded object has to be
 * the object that gets serialized, byte for byte.
 */
import { Effect } from 'effect';
import { DriftError, invalid } from 'src/errors';
import { OPAQUE_ID_RE, V2_EVALUATION_FIELDS } from 'src/constants';

// ---------------------------------------------------------------------------
// JSON value domain
// ---------------------------------------------------------------------------

export type JsonValue = null | boolean | number | string | JsonArray | JsonObject;
export interface JsonArray extends ReadonlyArray<JsonValue> {}
export interface JsonObject {
  readonly [key: string]: JsonValue;
}

/** A mutable object under construction, narrowed to {@link JsonObject} on return. */
export type MutableJsonObject = Record<string, JsonValue>;

export const isJsonObject = (value: unknown): value is JsonObject =>
  typeof value === 'object' && value !== null && !Array.isArray(value);

export const isJsonArray = (value: unknown): value is JsonArray => Array.isArray(value);

/** Python's `sorted()` over ASCII field names. */
export const sortedNames = (names: Iterable<string>): ReadonlyArray<string> =>
  [...names].sort((left, right) => (left < right ? -1 : left > right ? 1 : 0));

/** Read one property without tripping `noUncheckedIndexedAccess` noise. */
export const field = (value: JsonObject, key: string): JsonValue =>
  Object.prototype.hasOwnProperty.call(value, key) ? (value[key] as JsonValue) : null;

/** Report whether a key is physically present, `null` values included. */
export const hasField = (value: JsonObject, key: string): boolean =>
  Object.prototype.hasOwnProperty.call(value, key);

/** Python's `isinstance(x, int) and not isinstance(x, bool)`. */
export const isWholeNumber = (value: unknown): value is number =>
  typeof value === 'number' && Number.isInteger(value);

/** Python's `isinstance(x, (int, float)) and not isinstance(x, bool)`. */
export const isFiniteNumber = (value: unknown): value is number =>
  typeof value === 'number' && Number.isFinite(value);

export const isNonEmptyString = (value: unknown): value is string =>
  typeof value === 'string' && value.length > 0;

// ---------------------------------------------------------------------------
// _exact (client.py:1247-1269)
// ---------------------------------------------------------------------------

/**
 * Require a JSON object whose key set is exactly `fields`.
 *
 * Naming the drift is the whole diagnosis: a server that gained one field
 * otherwise fails every command with a wall of field names and no way to tell
 * which of them is the problem.
 *
 * NOTE (see NOTES.md §1): this validator is *closed* because the Python's is,
 * and `play/tests/test_client.py` asserts its drift sentences verbatim.  New
 * decoding must never add closedness the Python did not have — every
 * present-if-optional field below widens `fields` before calling this.
 */
export const exact = (
  value: unknown,
  fields: ReadonlySet<string>,
  label: string
): Effect.Effect<JsonObject, DriftError> => {
  const expected = sortedNames(fields).join(', ');
  if (!isJsonObject(value)) {
    return Effect.fail(
      invalid(label, `expected a JSON object with exactly ${expected}`)
    );
  }
  const present = new Set(Object.keys(value));
  const missing = sortedNames([...fields].filter((name) => !present.has(name)));
  const unexpected = sortedNames([...present].filter((name) => !fields.has(name)));
  if (missing.length === 0 && unexpected.length === 0) {
    return Effect.succeed(value);
  }
  const parts = [
    missing.length > 0 ? `missing ${missing.join(', ')}` : '',
    unexpected.length > 0 ? `unexpected ${unexpected.join(', ')}` : '',
  ].filter((part) => part !== '');
  return Effect.fail(
    invalid(label, `${parts.join('; ')}. Expected exactly ${expected}`)
  );
};

// ---------------------------------------------------------------------------
// _opaque (client.py:1271-1274)
// ---------------------------------------------------------------------------

export const opaque = (value: unknown, label: string): Effect.Effect<string, DriftError> =>
  typeof value === 'string' && OPAQUE_ID_RE.test(value)
    ? Effect.succeed(value)
    : Effect.fail(invalid(label));

// ---------------------------------------------------------------------------
// _json_value (client.py:1277-1300)
// ---------------------------------------------------------------------------

const JSON_MAX_DEPTH = 12;
const JSON_MAX_ITEMS = 8192;
const JSON_MAX_KEYS = 2048;
const JSON_MAX_KEY_BYTES = 128;

const jsonValueSync = (
  value: unknown,
  label: string,
  depth: number
): { readonly ok: true; readonly value: JsonValue } | { readonly ok: false; readonly error: DriftError } => {
  if (depth > JSON_MAX_DEPTH) {
    return { ok: false, error: invalid(label, 'JSON is nested too deeply') };
  }
  if (value === null || typeof value === 'string' || typeof value === 'boolean') {
    return { ok: true, value };
  }
  if (typeof value === 'number') {
    return Number.isFinite(value)
      ? { ok: true, value }
      : { ok: false, error: invalid(label, 'number is not finite') };
  }
  if (Array.isArray(value)) {
    if (value.length > JSON_MAX_ITEMS) {
      return { ok: false, error: invalid(label, 'too many items') };
    }
    const items: JsonValue[] = [];
    for (const item of value) {
      const decoded = jsonValueSync(item, label, depth + 1);
      if (!decoded.ok) return decoded;
      items.push(decoded.value);
    }
    return { ok: true, value: items };
  }
  if (isJsonObject(value)) {
    const keys = Object.keys(value);
    if (
      keys.length > JSON_MAX_KEYS ||
      keys.some((key) => key.length === 0 || key.length > JSON_MAX_KEY_BYTES)
    ) {
      return { ok: false, error: invalid(label, 'invalid object') };
    }
    const out: MutableJsonObject = {};
    for (const key of keys) {
      const decoded = jsonValueSync(value[key], label, depth + 1);
      if (!decoded.ok) return decoded;
      out[key] = decoded.value;
    }
    return { ok: true, value: out };
  }
  return { ok: false, error: invalid(label, 'non-JSON value') };
};

/** Copy an arbitrary JSON value, refusing anything the wire may not carry. */
export const jsonValue = (
  value: unknown,
  label: string,
  depth = 0
): Effect.Effect<JsonValue, DriftError> => {
  const decoded = jsonValueSync(value, label, depth);
  return decoded.ok ? Effect.succeed(decoded.value) : Effect.fail(decoded.error);
};

/** {@link jsonValue} narrowed to an object, for payload sub-trees. */
export const jsonObject = (
  value: unknown,
  label: string
): Effect.Effect<JsonObject, DriftError> =>
  Effect.flatMap(jsonValue(value, label), (copied) =>
    isJsonObject(copied) ? Effect.succeed(copied) : Effect.fail(invalid(label))
  );

// ---------------------------------------------------------------------------
// _safe_number (client.py:2295-2305)
// ---------------------------------------------------------------------------

export interface SafeNumberOptions {
  readonly nullable?: boolean;
}

/** A finite, non-negative number — optionally `null`. */
export const safeNumber = (
  value: unknown,
  label: string,
  options: SafeNumberOptions = {}
): Effect.Effect<number | null, DriftError> => {
  if (value === null && options.nullable === true) {
    return Effect.succeed(null);
  }
  if (typeof value !== 'number' || !Number.isFinite(value) || value < 0) {
    return Effect.fail(invalid(label));
  }
  return Effect.succeed(value);
};

// ---------------------------------------------------------------------------
// _validate_evaluation_context (client.py:1010-1056)
// ---------------------------------------------------------------------------

export interface EvaluationContext {
  readonly objective: string;
  readonly max_turns: number;
  readonly turns_remaining: number | null;
}

export interface EvaluationOptions {
  readonly expected?: EvaluationContext | null;
  readonly required?: boolean;
}

const MAX_TURNS_CEILING = 5000;

export const decodeEvaluationContext = (
  value: unknown,
  label: string,
  options: EvaluationOptions = {}
): Effect.Effect<EvaluationContext | null, DriftError> => {
  if (!isJsonObject(value)) {
    return Effect.fail(invalid(label));
  }
  const present = Object.keys(value).filter((key) => V2_EVALUATION_FIELDS.has(key));
  if (present.length === 0) {
    return options.required === true
      ? Effect.fail(invalid(label, 'evaluation context is missing'))
      : Effect.succeed(null);
  }
  if (present.length !== V2_EVALUATION_FIELDS.size) {
    return Effect.fail(invalid(label, 'evaluation context is incomplete'));
  }
  const objective = field(value, 'objective');
  const maxTurns = field(value, 'max_turns');
  const turnsRemaining = field(value, 'turns_remaining');
  const malformed =
    typeof objective !== 'string' ||
    objective.length === 0 ||
    objective.trim() !== objective ||
    !isWholeNumber(maxTurns) ||
    maxTurns < 1 ||
    maxTurns > MAX_TURNS_CEILING ||
    (turnsRemaining !== null &&
      (!isWholeNumber(turnsRemaining) ||
        turnsRemaining < 0 ||
        turnsRemaining > maxTurns));
  if (malformed) {
    return Effect.fail(invalid(label, 'evaluation context is malformed'));
  }
  const expected = options.expected ?? null;
  if (expected !== null) {
    if (objective !== expected.objective) {
      return Effect.fail(invalid(label, 'evaluation objective changed'));
    }
    if (maxTurns !== expected.max_turns) {
      return Effect.fail(invalid(label, 'evaluation max_turns changed'));
    }
  }
  return Effect.succeed({
    objective,
    max_turns: maxTurns,
    turns_remaining: turnsRemaining as number | null,
  });
};

// ---------------------------------------------------------------------------
// The identity every wire decoder checks a response against.
// ---------------------------------------------------------------------------

/**
 * The subset of a loaded session the schema layer reads.
 *
 * `Session` (src/services/session-store.ts) extends this, so a decoder can be
 * called with a real session or with a fixture identity in a test.
 */
export interface SessionIdentity {
  readonly gameId: string;
  readonly agentId: string;
  readonly controllerLabel: string;
  readonly place: number | null;
  readonly seatId: string | null;
  readonly playerName: string | null;
  readonly evaluation: EvaluationContext | null;
}
