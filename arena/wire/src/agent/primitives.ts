/**
 * Agent-side wire primitives — the full-control-v2 vocabulary every request
 * and response decoder in `src/agent/` is built out of.
 *
 * Clean-room Effect Schema re-expression of `play-cli/src/schema/primitives.ts`,
 * which is itself a port of `play/client.py`:
 *
 * - `_exact` — `play/client.py:1247-1269`
 * - `_opaque` — `play/client.py:1272-1275` (the schema lives in `./ids.ts`)
 * - `_json_value` — `play/client.py:1278-1301`
 * - `_safe_number` — `play/client.py:1921-1929`
 * - `_validate_evaluation_context` — `play/client.py:1010-1054`
 * - the session identity a decoder checks a response against —
 *   `play/client.py:1057-1078` (`_v2_session`)
 *
 * Nothing here imports from `play-cli`; the Python is the authority and the
 * TypeScript CLI is a second reading of it, so both were read and any place the
 * two disagree is called out in a `DIVERGENCE` note below.
 *
 * ## Strict where the Python is strict, tolerant only about excess
 *
 * `@arena/wire` decodes tolerantly: an unknown field a newer server added is
 * preserved, never rejected (see `../tolerant.ts`).  That is the *only*
 * relaxation.  Required fields stay required, literals stay closed, ranges stay
 * enforced, and every refusal the Python spells out is reachable here.
 *
 * The one place the Python is stricter than tolerance allows is `_exact`, which
 * rejects an object that gained a field.  Rather than lose that behaviour, it is
 * exported as {@link exactFields}: an explicit, opt-in closed-key check that
 * reproduces the Python's drift sentence byte for byte (`play-cli`'s
 * `test/schema.test.ts:52-66` asserts those sentences verbatim, and
 * `play/tests/test_client.py` asserts them on the Python side).  The default
 * `decodeX` functions in this package do *not* call it.
 *
 * ## Verbatim refusal sentences
 *
 * Where the Python's message is load-bearing — it is printed after `error: `
 * and asserted by tests on both sides — {@link driftError} rebuilds it exactly,
 * and the structured `issues` carry the detail Effect's parser found.  A caller
 * therefore gets the Python sentence *and* the machine-readable path, instead
 * of having to pick one.
 *
 * @module
 */

import { Either, Schema } from 'effect';
import { compareCodePoints } from '../canon.ts';
import { JsonObject, JsonValue } from '../json.ts';
import {
  decodeTolerant,
  WireDecodeError,
  type TolerantDecoder,
  type WireIssue,
} from '../tolerant.ts';
import { ControllerLabel, OpaqueId, PlayGameId } from './ids.ts';

// ---------------------------------------------------------------------------
// Refusals, as values
// ---------------------------------------------------------------------------

/**
 * The Python's `PlayerError(f"invalid {label}")` / `f"invalid {label}: {detail}"`,
 * as a {@link WireDecodeError}.
 *
 * `play-cli` spells this `invalid()` (`play-cli/src/errors.ts:70-74`) over its
 * own `DriftError`.  This package keeps a single error channel for the whole
 * wire layer instead, so a decoder that mixes a hand-written refusal with a
 * schema refusal still fails with one type — `schemaName` carries the label and
 * `issues` carries whatever structure was available.
 */
export const driftError = (
  label: string,
  detail?: string,
  issues: ReadonlyArray<WireIssue> = [],
): WireDecodeError =>
  new WireDecodeError({
    schemaName: label,
    message: detail === undefined ? `invalid ${label}` : `invalid ${label}: ${detail}`,
    issues,
  });

// ---------------------------------------------------------------------------
// Records and name ordering
// ---------------------------------------------------------------------------

/** An object read off the wire before its values have been validated. */
export type UnknownRecord = { readonly [key: string]: unknown };

/**
 * Python's `isinstance(value, dict)`.
 *
 * Deliberately *not* `isJsonObject` from `../json.ts`: that one validates the
 * values recursively, which would refuse a payload the Python accepts and would
 * therefore attach the wrong refusal sentence to it.  Key-set checks come
 * first, value checks after — same order as the Python.
 */
export const isRecord = (value: unknown): value is UnknownRecord =>
  typeof value === 'object' && value !== null && !Array.isArray(value);

/**
 * Python's `sorted()` over field names: by code point, not by UTF-16 code unit.
 *
 * The two orders disagree from U+E000 up (see `compareCodePoints` in
 * `../canon.ts`), and field names are attacker-influenced — an `unexpected …`
 * list is built from keys the *server* chose — so the parity-safe comparison is
 * the one that costs nothing here.
 */
export const sortedNames = (names: Iterable<string>): ReadonlyArray<string> =>
  [...names].toSorted(compareCodePoints);

// ---------------------------------------------------------------------------
// _exact — play/client.py:1247-1269
// ---------------------------------------------------------------------------

/** How an object's key set differs from the one a decoder demanded. */
export interface FieldDrift {
  /** Demanded, absent.  Sorted. */
  readonly missing: ReadonlyArray<string>;
  /** Present, not demanded.  Sorted. */
  readonly unexpected: ReadonlyArray<string>;
}

/** Compare a present key set against the demanded one. */
export const fieldDrift = (
  present: Iterable<string>,
  fields: ReadonlySet<string>,
): FieldDrift => {
  const keys = new Set(present);
  return {
    missing: sortedNames([...fields].filter((name) => !keys.has(name))),
    unexpected: sortedNames([...keys].filter((name) => !fields.has(name))),
  };
};

/** True when a key set matches exactly. */
export const isExactMatch = (drift: FieldDrift): boolean =>
  drift.missing.length === 0 && drift.unexpected.length === 0;

/**
 * The Python's drift clause: `"missing b; unexpected c"`.
 *
 * Naming the drift is the whole diagnosis — a server that gained one field
 * otherwise fails every command with a wall of field names and no way to tell
 * which of them is the problem (`play/client.py:1254-1256`).
 */
export const formatFieldDrift = (drift: FieldDrift): string =>
  [
    drift.missing.length > 0 ? `missing ${drift.missing.join(', ')}` : '',
    drift.unexpected.length > 0 ? `unexpected ${drift.unexpected.join(', ')}` : '',
  ]
    .filter((part) => part !== '')
    .join('; ');

const driftIssues = (drift: FieldDrift): ReadonlyArray<WireIssue> => [
  ...drift.missing.map((name) => ({
    kind: 'Missing',
    path: [name],
    message: `is missing`,
  })),
  ...drift.unexpected.map((name) => ({
    kind: 'Unexpected',
    path: [name],
    message: `is not expected`,
  })),
];

/**
 * Require an object whose key set is *exactly* `fields`.
 *
 * **Opt-in.**  This is the one check in the package that refuses a payload for
 * gaining a field, so no `decodeX` here calls it: the tolerant decoders accept
 * and preserve excess (`../tolerant.ts`).  Reach for it only where byte parity
 * with `play/client.py`'s closed validators is the point — a parity harness
 * diffing this port against the Python, or a strict mode a caller opts into.
 *
 * The refusal text is the Python's, character for character:
 *
 * ```text
 * invalid thing: expected a JSON object with exactly a, b
 * invalid thing: missing b; unexpected c. Expected exactly a, b
 * ```
 */
export const exactFields = (
  value: unknown,
  fields: ReadonlySet<string>,
  label: string,
): Either.Either<UnknownRecord, WireDecodeError> => {
  const expected = sortedNames(fields).join(', ');
  if (!isRecord(value)) {
    return Either.left(
      driftError(label, `expected a JSON object with exactly ${expected}`, [
        { kind: 'Type', path: [], message: `Expected a JSON object with exactly ${expected}` },
      ]),
    );
  }
  const drift = fieldDrift(Object.keys(value), fields);
  return isExactMatch(drift)
    ? Either.right(value)
    : Either.left(
        driftError(
          label,
          `${formatFieldDrift(drift)}. Expected exactly ${expected}`,
          driftIssues(drift),
        ),
      );
};

// ---------------------------------------------------------------------------
// _json_value — play/client.py:1278-1301
// ---------------------------------------------------------------------------

/** `depth > 12` refuses (`play/client.py:1279`). */
export const JSON_MAX_DEPTH = 12;
/** `len(value) > 8192` refuses an array (`play/client.py:1288`). */
export const JSON_MAX_ITEMS = 8192;
/** `len(value) > 2048` refuses an object (`play/client.py:1292`). */
export const JSON_MAX_KEYS = 2048;
/**
 * `len(key) > 128` refuses an object (`play/client.py:1293`).
 *
 * DIVERGENCE: `play-cli` names this `JSON_MAX_KEY_BYTES` and measures
 * `key.length`, i.e. UTF-16 code units.  Python's `len()` counts *code points*,
 * so a 100-emoji key is 100 to Python and 200 to `key.length` — the Python
 * accepts it and `play-cli` refuses it.  This port counts code points, because
 * the Python is the authority on what the wire carries.
 */
export const JSON_MAX_KEY_LENGTH = 128;

/**
 * Python's `len()` over a string: **code points**, not UTF-16 code units.
 *
 * The one home for the rule the DIVERGENCE note above states.  `../agent/` had
 * three private copies of this line — in `primitives.ts`, `error.ts` and
 * `descriptor.ts` — which is exactly the triplicate `../numeric.ts` and
 * `../control-protocol.ts` exist to collapse: a bound measured in code units
 * in one module and code points in another is a parity break that no test in
 * either module can see.
 */
export const codePointLength = (text: string): number => [...text].length;

/**
 * `Object.hasOwn`, named once.
 *
 * Own-key-ness is load-bearing here rather than incidental: `_exact` and the
 * evaluation-context arity check both decide "did the payload physically carry
 * this key?", and an inherited or prototype key must never answer yes.
 */
export const hasOwnKey = (value: object, key: string): boolean => Object.hasOwn(value, key);

const isBoundedKey = (key: string): boolean =>
  key.length > 0 && codePointLength(key) <= JSON_MAX_KEY_LENGTH;

/**
 * Copy an arbitrary JSON value, refusing anything the wire may not carry.
 *
 * A *copy*, as in the Python: the result shares no array or object with the
 * input, so a payload cannot alias into decoded state.  Key order is preserved
 * as `Object.keys` reports it — which is already how `JSON.parse` built the
 * input, integer-like keys hoisted and all, so the rebuild loses nothing that
 * was not lost before it.
 *
 * Refusal order matches the Python exactly: depth, then type, then array
 * length, then object size and key shape.
 */
export const boundedJson = (
  value: unknown,
  label: string,
  depth = 0,
): Either.Either<JsonValue, WireDecodeError> => {
  if (depth > JSON_MAX_DEPTH) {
    return Either.left(driftError(label, 'JSON is nested too deeply'));
  }
  if (value === null || typeof value === 'string' || typeof value === 'boolean') {
    return Either.right(value);
  }
  if (typeof value === 'number') {
    return Number.isFinite(value)
      ? Either.right(value)
      : Either.left(driftError(label, 'number is not finite'));
  }
  if (Array.isArray(value)) {
    return value.length > JSON_MAX_ITEMS
      ? Either.left(driftError(label, 'too many items'))
      : Either.all(value.map((item) => boundedJson(item, label, depth + 1)));
  }
  if (isRecord(value)) {
    const keys = Object.keys(value);
    return keys.length > JSON_MAX_KEYS || !keys.every(isBoundedKey)
      ? Either.left(driftError(label, 'invalid object'))
      : Either.map(
          Either.all(
            keys.map((key) =>
              Either.map(
                boundedJson(value[key], label, depth + 1),
                (decoded) => [key, decoded] as const,
              ),
            ),
          ),
          (entries): JsonValue => Object.fromEntries(entries),
        );
  }
  return Either.left(driftError(label, 'non-JSON value'));
};

/** {@link boundedJson} narrowed to an object, for payload sub-trees. */
export const boundedJsonObject = (
  value: unknown,
  label: string,
): Either.Either<JsonObject, WireDecodeError> =>
  Either.flatMap(boundedJson(value, label), (copied) =>
    isRecord(copied) ? Either.right(copied) : Either.left(driftError(label)),
  );

/**
 * Node depth of a JSON value: `0` for a scalar or an empty container, and one
 * more than its deepest child otherwise.
 *
 * Equivalent to the Python's recursive `depth` parameter — a tree is accepted
 * when every node sits at most {@link JSON_MAX_DEPTH} ancestors below the root,
 * which is exactly `jsonNodeDepth(root) <= JSON_MAX_DEPTH`.
 */
export const jsonNodeDepth = (value: JsonValue): number => {
  if (Array.isArray(value)) {
    return value.length === 0 ? 0 : 1 + Math.max(...value.map(jsonNodeDepth));
  }
  if (isRecord(value)) {
    const children = Object.values(value);
    return children.length === 0 ? 0 : 1 + Math.max(...children.map(jsonNodeDepth));
  }
  return 0;
};

/**
 * {@link boundedJson} as a schema, so a bounded payload field composes into a
 * `Schema.Struct` instead of forcing the containing decoder into a manual step.
 *
 * The verdict is identical to {@link boundedJson} — it *is* {@link boundedJson}
 * — but the message a failure carries is the schema's, not the Python's
 * sentence.  Where the sentence matters, call {@link boundedJson} with the
 * label the Python would have used.
 */
export const BoundedJsonValue: Schema.Schema<JsonValue> = JsonValue.pipe(
  Schema.filter((value) => {
    const checked = boundedJson(value, 'json value');
    return Either.isLeft(checked) ? checked.left.message : undefined;
  }),
).annotations({
  identifier: 'BoundedJsonValue',
  description:
    'A JSON value within the v2 wire bounds: depth <= 12, <= 8192 items, <= 2048 keys, keys 1-128 code points',
});

/** Decode an unknown input as a bounded JSON value. */
export const decodeBoundedJsonValue: TolerantDecoder<JsonValue> =
  decodeTolerant(BoundedJsonValue);

/**
 * {@link BoundedJsonValue} narrowed to an object — the shape every `details`,
 * `arguments` and `metadata` sub-tree on the v2 wire has.
 */
export const BoundedJsonObject: Schema.Schema<JsonObject> = JsonObject.pipe(
  Schema.filter((value) => {
    const checked = boundedJson(value, 'json object');
    return Either.isLeft(checked) ? checked.left.message : undefined;
  }),
).annotations({
  identifier: 'BoundedJsonObject',
  description: 'A JSON object within the v2 wire bounds',
});

/** Decode an unknown input as a bounded JSON object. */
export const decodeBoundedJsonObject: TolerantDecoder<JsonObject> =
  decodeTolerant(BoundedJsonObject);

// ---------------------------------------------------------------------------
// Integers and text
// ---------------------------------------------------------------------------

/**
 * A Python `int` on the wire.
 *
 * The client's integer test is always the same three-part one —
 * `isinstance(x, bool) or not isinstance(x, int)` (`play/client.py:1306-1307`,
 * `:1033-1034`, `:1038-1039`) — whose `bool` exclusion has no JavaScript
 * counterpart: `Schema.Number` already refuses `true`.  What is left is
 * `Schema.int()`, which also refuses the `5.5` that `isinstance(x, int)`
 * refuses.
 *
 * Spelled `number`, not `bigint`: these are counters and sizes the port does
 * arithmetic and comparisons on.  Anything that will be *hashed* has to cross
 * into `bigint` first, or `../canon.ts` will print `5.0` where Python printed
 * `5` — see `revisionCanonRecord` in `./revision.ts` for the pattern.
 *
 * **This is not `../numeric.ts`'s `WireInt`**, which the gateway payloads use
 * and which decodes to `bigint` for exactly the reason above.  Same name,
 * different type, on purpose; the package barrel keeps them apart as
 * `Wire.Agent.WireInt` and `Wire.WireInt` and never flattens them together.
 * `../numeric.ts` has the long version under "a deliberate namesake", and
 * `test/barrel.test.ts` pins the difference so neither can quietly become the
 * other.
 */
export const WireInt = Schema.Number.pipe(Schema.int()).annotations({
  identifier: 'WireInt',
  description: 'A Python int on the v2 wire (number-spelled; see canon.ts before hashing)',
});
/** A Python `int` on the wire. */
export type WireInt = typeof WireInt.Type;

/** {@link WireInt}, `>= 0` — counts, stocks, indices, elapsed turns. */
export const NonNegativeWireInt = Schema.Number.pipe(
  Schema.int(),
  Schema.nonNegative(),
).annotations({
  identifier: 'NonNegativeWireInt',
  description: 'A non-negative integer on the v2 wire',
});
/** {@link WireInt}, `>= 0`. */
export type NonNegativeWireInt = typeof NonNegativeWireInt.Type;

/**
 * {@link WireInt}, `>= 1` — the fields where zero is not a small value but a
 * contradiction: a city's `size`, a seat's `place`, `max_turns`.
 */
export const PositiveWireInt = Schema.Number.pipe(
  Schema.int(),
  Schema.positive(),
).annotations({
  identifier: 'PositiveWireInt',
  description: 'A positive integer on the v2 wire',
});
/** {@link WireInt}, `>= 1`. */
export type PositiveWireInt = typeof PositiveWireInt.Type;

/** A non-empty string.  Python's `isinstance(x, str) and x`. */
export const NonEmptyText = Schema.String.pipe(Schema.nonEmptyString()).annotations({
  identifier: 'NonEmptyText',
  description: 'A non-empty string',
});
/** A non-empty string. */
export type NonEmptyText = typeof NonEmptyText.Type;

/**
 * Human-readable text: non-*blank*, and at most `max` code points.
 *
 * Both halves are the Python's, and they are deliberately asymmetric
 * (`_validate_error`, `play/client.py:1346-1348`):
 *
 * ```python
 * not isinstance(error["message"], str)
 * or not error["message"].strip()
 * or len(error["message"]) > 500
 * ```
 *
 * Emptiness is judged *after* stripping — a message of three spaces is a
 * refusal, because a blank remedy is no remedy — while the length ceiling is
 * measured on the raw string, so padding counts against the budget.  The value
 * is kept unstripped; callers that display it strip at the edge, exactly as the
 * Python does when it builds its return value (`:1361`).
 *
 * `max` counts code points, matching Python's `len()`.  A UTF-16 `.length`
 * would charge double for every astral character and refuse a message Python
 * accepts.
 */
export const boundedText = (max: number, identifier: string): Schema.Schema<string> =>
  Schema.String.pipe(
    Schema.filter((value) => {
      if (value.trim().length === 0) return `${identifier} must not be blank`;
      return codePointLength(value) > max
        ? `${identifier} must be at most ${String(max)} code points`
        : undefined;
    }),
  ).annotations({
    identifier,
    description: `Non-blank text of at most ${String(max)} code points`,
  });

/**
 * Re-exported so a decoder that already imports the primitives does not need a
 * second import for the commonest id shape.  The definition, and the reasoning,
 * live in `./ids.ts`.
 */
export { OpaqueId } from './ids.ts';

// ---------------------------------------------------------------------------
// _safe_number — play/client.py:1921-1929
// ---------------------------------------------------------------------------

/**
 * A finite, non-negative number.
 *
 * Python admits `int` and `float` alike and refuses `bool`, `NaN`, `±inf` and
 * anything below zero.  JavaScript has no `bool`-is-an-`int` problem, and
 * `Schema.Number` already refuses a boolean, so `finite` + `nonNegative` is the
 * whole rule.
 *
 * Note this is a *number*, not an integer: the fields it guards are elapsed
 * seconds and timing budgets, which arrive as Python floats.
 */
export const SafeNumber = Schema.Number.pipe(
  Schema.finite(),
  Schema.nonNegative(),
).annotations({
  identifier: 'SafeNumber',
  description: 'A finite, non-negative number (client.py _safe_number)',
});
/** A finite, non-negative number. */
export type SafeNumber = typeof SafeNumber.Type;

/** {@link SafeNumber}, or `null` — the `nullable=True` call sites. */
export const NullableSafeNumber: Schema.Schema<SafeNumber | null> = Schema.NullOr(
  SafeNumber,
).annotations({ identifier: 'NullableSafeNumber' });

/** Decode a finite, non-negative number. */
export const decodeSafeNumber: TolerantDecoder<SafeNumber> = decodeTolerant(SafeNumber);

/** Decode a finite, non-negative number, or `null`. */
export const decodeNullableSafeNumber: TolerantDecoder<SafeNumber | null> =
  decodeTolerant(NullableSafeNumber);

/** Options for {@link safeNumber}. */
export interface SafeNumberOptions {
  /** Accept `null` and return it unchanged.  Python's `nullable=True`. */
  readonly nullable?: boolean;
}

/**
 * {@link SafeNumber} with the Python's refusal sentence (`invalid {label}`).
 *
 * `nullable` defaults to `false`, so a `null` where a number was promised is a
 * refusal rather than a silently accepted hole.
 */
export const safeNumber = (
  value: unknown,
  label: string,
  options: SafeNumberOptions = {},
): Either.Either<number | null, WireDecodeError> => {
  if (value === null && options.nullable === true) {
    return Either.right(null);
  }
  return Either.mapLeft(decodeSafeNumber(value), (error) =>
    driftError(label, undefined, error.issues),
  );
};

// ---------------------------------------------------------------------------
// _validate_evaluation_context — play/client.py:1010-1054
// ---------------------------------------------------------------------------

/**
 * `V2_EVALUATION_FIELDS` — `play/client.py:165`, read a second time through
 * `play-cli/src/constants.ts:277-281`.
 *
 * All three or none: the context is carried *inline* on several different
 * envelopes, so its presence is decided by intersection with the host object's
 * keys rather than by a container field.
 */
export const V2_EVALUATION_FIELDS: ReadonlySet<string> = new Set([
  'objective',
  'max_turns',
  'turns_remaining',
]);

/** `1 <= max_turns <= 5000` — `play/client.py:1035`. */
export const MAX_TURNS_CEILING = 5000;

/**
 * The evaluation contract a joined agent is playing under.
 *
 * - `objective` — non-empty and already trimmed.  A server that pads the
 *   objective is drifting, and silently trimming it here would hide a change to
 *   the thing the run is scored against.
 * - `max_turns` — an integer in `[1, 5000]`.
 * - `turns_remaining` — `null`, or an integer in `[0, max_turns]`.
 *
 * DIVERGENCE (documented, not fixed): Python's `objective.strip()` and
 * JavaScript's `String.prototype.trim()` disagree on a handful of characters —
 * Python also strips U+001C-U+001F and U+0085, JavaScript also strips U+FEFF.
 * An objective whose only padding is one of those five characters is judged
 * differently by the two implementations.  `play-cli` has the same gap; it is
 * recorded here rather than papered over, because "matches the Python" is the
 * contract and a bespoke strip would be a third behaviour.
 */
export const EvaluationContext = Schema.Struct({
  objective: Schema.String.pipe(Schema.nonEmptyString(), Schema.trimmed()).annotations({
    identifier: 'EvaluationObjective',
  }),
  max_turns: Schema.Number.pipe(
    Schema.int(),
    Schema.between(1, MAX_TURNS_CEILING),
  ).annotations({ identifier: 'EvaluationMaxTurns' }),
  turns_remaining: Schema.NullOr(Schema.Number.pipe(Schema.int(), Schema.nonNegative())),
}).pipe(
  Schema.filter((context) =>
    context.turns_remaining !== null && context.turns_remaining > context.max_turns
      ? {
          path: ['turns_remaining'],
          message: `turns_remaining ${String(context.turns_remaining)} exceeds max_turns ${String(context.max_turns)}`,
        }
      : undefined,
  ),
).annotations({
  identifier: 'EvaluationContext',
  description: 'The evaluation contract carried inline on v2 envelopes',
});
/** The evaluation contract a joined agent is playing under. */
export type EvaluationContext = typeof EvaluationContext.Type;

/**
 * Decode an evaluation context from an object that carries exactly its three
 * keys.  Excess keys are preserved, per the package's tolerance rule; use
 * {@link decodeEvaluationSlot} for the inline case, where the context shares an
 * object with unrelated envelope fields.
 */
export const decodeEvaluationContext: TolerantDecoder<EvaluationContext> = decodeTolerant(
  EvaluationContext,
  'EvaluationContext',
);

/** Options for {@link decodeEvaluationSlot}. */
export interface EvaluationSlotOptions {
  /** Refuse a host object that carries none of the three fields. */
  readonly required?: boolean;
}

/**
 * The inline form: read an evaluation context out of a *host* object that also
 * carries unrelated fields.
 *
 * Tri-state, exactly as `play/client.py:1017-1025`:
 *
 * - none of the three keys present → `null` (or a refusal when `required`),
 * - some but not all → `evaluation context is incomplete`,
 * - all three, one of them bad → `evaluation context is malformed`.
 *
 * The single `malformed` sentence is the Python's; the per-field detail Effect
 * found travels in `issues`, so nothing is lost by matching the wording.
 */
export const decodeEvaluationSlot = (
  value: unknown,
  label: string,
  options: EvaluationSlotOptions = {},
): Either.Either<EvaluationContext | null, WireDecodeError> => {
  if (!isRecord(value)) {
    return Either.left(driftError(label));
  }
  const present = Object.keys(value).filter((key) => V2_EVALUATION_FIELDS.has(key));
  if (present.length === 0) {
    return options.required === true
      ? Either.left(driftError(label, 'evaluation context is missing'))
      : Either.right(null);
  }
  if (present.length !== V2_EVALUATION_FIELDS.size) {
    return Either.left(driftError(label, 'evaluation context is incomplete'));
  }
  return Either.mapLeft(
    decodeEvaluationContext({
      objective: value['objective'],
      max_turns: value['max_turns'],
      turns_remaining: value['turns_remaining'],
    }),
    (error) => driftError(label, 'evaluation context is malformed', error.issues),
  );
};

/**
 * Refuse a context whose terms changed mid-run — `play/client.py:1044-1049`.
 *
 * `turns_remaining` is deliberately *not* compared: it is the one field that is
 * meant to move.  `objective` is checked before `max_turns`, so a server that
 * changed both narrates the objective, which is the more alarming of the two.
 */
export const checkEvaluationDrift = (
  actual: EvaluationContext,
  expected: EvaluationContext,
  label: string,
): Either.Either<EvaluationContext, WireDecodeError> => {
  if (actual.objective !== expected.objective) {
    return Either.left(driftError(label, 'evaluation objective changed'));
  }
  if (actual.max_turns !== expected.max_turns) {
    return Either.left(driftError(label, 'evaluation max_turns changed'));
  }
  return Either.right(actual);
};

// ---------------------------------------------------------------------------
// The identity every wire decoder checks a response against
// ---------------------------------------------------------------------------

/**
 * The subset of a loaded session the schema layer reads.
 *
 * Not a wire packet: these are the private session file's fields, in the
 * camelCase the port uses locally, and no server ever sends this object.  It is
 * a schema anyway because every response decoder is handed one and a fixture
 * identity that drifted from the real thing would make a decoder's parity test
 * lie.
 *
 * The two branded fields are the two `_v2_session` actually validates
 * (`play/client.py:1063-1067`): `game_id` against `GAME_ID_RE` and `agent_id`
 * against `OPAQUE_ID_RE`.  `controller_label` is only checked for
 * non-emptiness there (`play/client.py:1070-1071`) — `CONTROLLER_RE` and the truthfulness
 * rules are enforced once, at join time, by `_controller_name`
 * (`play/client.py:743-755`), so requiring the full pattern here would refuse a
 * session the Python happily loads.  `place`, `seat_id` and `player_name` are
 * not validated by the Python at all.
 */
export const SessionIdentity = Schema.Struct({
  gameId: PlayGameId,
  agentId: OpaqueId,
  controllerLabel: ControllerLabel,
  place: Schema.NullOr(Schema.Number.pipe(Schema.int())),
  seatId: Schema.NullOr(Schema.String),
  playerName: Schema.NullOr(Schema.String),
  evaluation: Schema.NullOr(EvaluationContext),
}).annotations({
  identifier: 'SessionIdentity',
  description: 'The local session projection a v2 response is checked against',
});
/** The subset of a loaded session the schema layer reads. */
export type SessionIdentity = typeof SessionIdentity.Type;

/** Decode a session identity — used on fixtures and on the session file. */
export const decodeSessionIdentity: TolerantDecoder<SessionIdentity> = decodeTolerant(
  SessionIdentity,
  'SessionIdentity',
);
