/**
 * Plain JSON values.
 *
 * This is the number-based JSON model: one `number` type, no int/float
 * distinction.  It is the right model for the arena's *payload* fields — the
 * gateway's `manifest`, `report`, `config` and `metrics` blobs, which are
 * forwarded, diffed and re-serialized but never arithmetically interpreted by
 * this package.
 *
 * Where the distinction *does* matter — `current_turn`, `place`, a frame index
 * — the schema for that field says so (`Schema.Int`, {@link FrameIndex} in
 * `./ids.ts`).  Do not reach for {@link JsonValue} there: "it's a number" is
 * exactly the information those fields need to keep.
 *
 * Round-tripping is the point.  A value decoded through {@link JsonValue}
 * re-serializes to `JSON.stringify(JSON.parse(text))` — the same keys in the
 * same order, nothing dropped — because `JSON.parse` output is already the
 * schema's own representation and the shared parse options preserve property
 * order.  The one thing not preserved is number *spelling*: `JSON.parse`
 * itself canonicalizes, so `1.7e9` comes back as `1700000000` and `1.50` as
 * `1.5`.  That is the JSON reader's doing, not this schema's, and it is
 * equally true of Python's `json` module — so it is not a parity risk, only a
 * reason never to compare raw payload bytes.
 *
 * ## What is deliberately *not* accepted
 *
 * - `undefined`, functions, symbols — `JSON.parse` never produces them, and
 *   accepting them would let a value through that `JSON.stringify` then drops.
 * - `NaN` / `±Infinity` — not representable in JSON; {@link Schema.JsonNumber}
 *   rejects them, so an unserializable value cannot survive a decode.
 *
 * ## One known lossiness: `"__proto__"`
 *
 * `JSON.parse` keeps a `"__proto__"` key as an ordinary own property, and so
 * does Python's `json` — but Effect's `Schema` drops it while rebuilding the
 * decoded object.  Nothing is polluted (the decoded value's prototype is
 * still `Object.prototype`), but the key does not survive a round trip, so a
 * hand-edited manifest carrying one would come back out without it.  Pinned
 * by a test in `test/json.test.ts` so an `effect` upgrade that changes the
 * behaviour is caught rather than discovered in a parity diff.
 *
 * ## One known looseness
 *
 * {@link JsonObject} is a structural record: any non-array object whose own
 * enumerable string-keyed properties are JSON values matches, including class
 * instances (`new Date()` decodes to `{}` — its fields are not enumerable own
 * properties).  Wire inputs come from `JSON.parse`, where such a value cannot
 * arise, so this is a non-issue in practice.  It is documented rather than
 * patched so the exported guards ({@link isJsonValue} and friends), being
 * derived from these very schemas, agree with the decoders exactly.
 *
 * @module
 */

import { Schema } from 'effect';
import { decodeTolerant, isTolerant, type TolerantDecoder } from './tolerant.ts';

/** Any value `JSON.parse` can produce. */
export type JsonValue =
  | null
  | boolean
  | number
  | string
  | JsonArray
  | JsonObject;

/** A JSON array. Readonly: wire values are never mutated in place. */
export type JsonArray = ReadonlyArray<JsonValue>;

/** A JSON object: string keys, JSON values, no `undefined`. */
export type JsonObject = { readonly [key: string]: JsonValue };

/**
 * Schema for {@link JsonValue}.  Recursive, so it is built with
 * `Schema.suspend`; the explicit annotation is what lets the recursion type.
 */
export const JsonValue: Schema.Schema<JsonValue> = Schema.suspend(
  (): Schema.Schema<JsonValue> =>
    Schema.Union(
      Schema.Null,
      Schema.Boolean,
      Schema.JsonNumber,
      Schema.String,
      JsonArray,
      JsonObject,
    ),
).annotations({
  identifier: 'JsonValue',
  description: 'A value produced by JSON.parse: null, boolean, finite number, string, array or object',
});

/** Schema for {@link JsonArray}. */
export const JsonArray: Schema.Schema<JsonArray> = Schema.Array(
  Schema.suspend((): Schema.Schema<JsonValue> => JsonValue),
).annotations({ identifier: 'JsonArray' });

/** Schema for {@link JsonObject}. */
export const JsonObject: Schema.Schema<JsonObject> = Schema.Record({
  key: Schema.String,
  value: Schema.suspend((): Schema.Schema<JsonValue> => JsonValue),
}).annotations({ identifier: 'JsonObject' });

/** Decode an unknown input as a {@link JsonValue}. */
export const decodeJsonValue: TolerantDecoder<JsonValue> = decodeTolerant(JsonValue);

/** Decode an unknown input as a {@link JsonObject}. */
export const decodeJsonObject: TolerantDecoder<JsonObject> = decodeTolerant(JsonObject);

/** Decode an unknown input as a {@link JsonArray}. */
export const decodeJsonArray: TolerantDecoder<JsonArray> = decodeTolerant(JsonArray);

/** Refinement for {@link JsonValue}; agrees with {@link decodeJsonValue}. */
export const isJsonValue: (input: unknown) => input is JsonValue = isTolerant(JsonValue);

/** Refinement for {@link JsonObject}; agrees with {@link decodeJsonObject}. */
export const isJsonObject: (input: unknown) => input is JsonObject = isTolerant(JsonObject);

/** Refinement for {@link JsonArray}; agrees with {@link decodeJsonArray}. */
export const isJsonArray: (input: unknown) => input is JsonArray = isTolerant(JsonArray);

/**
 * Read one property of a JSON value, with `undefined` for "not an object" and
 * "absent" alike.
 *
 * The gateway reads its untrusted manifests exactly this way
 * (`manifest.get("state", manifest.get("status"))` in
 * `agent_eval/replay_gateway.py`), and a port that has to reproduce that
 * behaviour should not be re-deriving the "is it even a mapping?" check at
 * every call site.
 *
 * Own properties only.  A plain `value[key]` would answer `"constructor"`,
 * `"toString"` or `"__proto__"` from `Object.prototype` and hand back a
 * function — a value the return type says is impossible, from a payload an
 * attacker controls the keys of.  Python's `dict.get` has no such inherited
 * lookup, so `Object.hasOwn` is also what parity requires.
 */
export const jsonField = (value: JsonValue, key: string): JsonValue | undefined =>
  isJsonObject(value) && Object.hasOwn(value, key) ? value[key] : undefined;

/**
 * JSON text as a {@link JsonValue}: the parse step, as a schema, so a payload
 * that arrives as a string composes with the rest of the port.  Encoding
 * stringifies.
 */
export const JsonValueFromString: Schema.Schema<JsonValue, string> = Schema.parseJson(
  JsonValue,
).annotations({ identifier: 'JsonValueFromString' });

/** Parse JSON text into a {@link JsonValue}; errors are values, not throws. */
export const decodeJsonValueFromString: TolerantDecoder<JsonValue> =
  decodeTolerant(JsonValueFromString);
