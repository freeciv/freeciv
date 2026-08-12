/**
 * Binding the order's remaining words to the action's own argument schema.
 *
 * Ports `play/client.py` 8764-8798 (`_ORDER_BAD`, `_order_value`), 8801-8834
 * (`_order_properties`, `_order_array_bounds`, `_is_array_property`),
 * 8836-8901 (`_order_arguments`), 8924-8943 (`_order_match`), 8946-8957
 * (`_default_arguments`), 8960-8985 (`_named_target_id`) and 8988-9024
 * (`_order_element_resolver`).
 *
 * Nothing here invents a value.  A word is coerced only to the type the
 * *action's own* JSON schema declares, an enum value must be one the catalog
 * printed (in either the human or the JSON spelling), and a list element is
 * either a coordinate this seat already read on a page or a target name this
 * seat was already offered.  Everything else is `_ORDER_BAD`, which is a
 * non-match — never a guess.
 */
import { Effect, Either } from 'effect';
import { TILE_ALIAS_RE } from 'src/constants';
import type { PlayerError } from 'src/errors';
import { jsonLiteral, scalar } from 'src/render/primitives';
import {
  field,
  hasField,
  isJsonArray,
  isJsonObject,
  type JsonObject,
  type JsonValue,
} from 'src/schema/primitives';
import { expandTileAlias } from 'src/services/alias-expand';
import type { V2ClientState } from 'src/services/session-store';
import {
  casefold,
  ORDER_COORDINATE_RE,
  orderUnresolved,
  V2_MAX_ORDER_WORDS,
  type OrderUnresolved,
} from 'src/services/orders/parse';
import {
  orderActor,
  orderDiscriminators,
  orderPool,
  type OrdersDeps,
} from 'src/services/orders/match';

// ---------------------------------------------------------------------------
// _ORDER_BAD
// ---------------------------------------------------------------------------

/** The sentinel CPython used for "this word cannot be this argument". */
export const ORDER_BAD: unique symbol = Symbol('_ORDER_BAD');

/** A coerced order word, or the sentinel that says it did not coerce. */
export type OrderValue = JsonValue | typeof ORDER_BAD;

/** One list element resolver; `null` means "leave every element as text". */
export type ElementResolver =
  | ((text: string) => Effect.Effect<OrderValue, OrderUnresolved | PlayerError>)
  | null;

// ---------------------------------------------------------------------------
// _order_value
// ---------------------------------------------------------------------------

const INTEGER_RE = /^-?(?:0|[1-9][0-9]{0,9})$/;

/**
 * CPython's `float()` grammar: sign, digits with optional `_` separators, an
 * optional fraction and exponent, or the `inf`/`infinity`/`nan` words.
 *
 * `Number()` is not a substitute — it takes `0x10` and `""`, and it rejects
 * `1_000`, all three of which CPython disagrees about.
 */
const FLOAT_RE =
  /^[+-]?(?:(?:[0-9](?:_?[0-9])*(?:\.(?:[0-9](?:_?[0-9])*)?)?|\.[0-9](?:_?[0-9])*)(?:[eE][+-]?[0-9](?:_?[0-9])*)?|inf(?:inity)?|nan)$/i;

const pyFloat = (text: string): number | null => {
  if (!FLOAT_RE.test(text)) return null;
  const cleaned = text.replace(/_/g, '');
  if (/^[+-]?nan$/i.test(cleaned)) return Number.NaN;
  const infinite = /^([+-]?)inf(?:inity)?$/i.exec(cleaned);
  if (infinite !== null) return infinite[1] === '-' ? -Infinity : Infinity;
  return Number(cleaned);
};

const TRUE_WORDS: ReadonlySet<string> = new Set(['true', 'yes', 'on', '1']);
const FALSE_WORDS: ReadonlySet<string> = new Set(['false', 'no', 'off', '0']);

/** Coerce one order word to the type this action's own schema declares. */
export const orderValue = (specification: JsonValue, text: string): OrderValue => {
  if (!isJsonObject(specification)) return text;
  const choices = field(specification, 'enum');
  if (isJsonArray(choices) && choices.length > 0) {
    for (const choice of choices) {
      // Both spellings resolve: the JSON literal the catalog prints and the
      // human word `just do` has always accepted.
      const wanted = casefold(text);
      if (casefold(scalar(choice)) === wanted || casefold(jsonLiteral(choice)) === wanted) {
        return choice;
      }
    }
    return ORDER_BAD;
  }
  const declared = field(specification, 'type');
  if (declared === 'integer') {
    return INTEGER_RE.test(text) ? Number.parseInt(text, 10) : ORDER_BAD;
  }
  if (declared === 'number') {
    const value = pyFloat(text);
    return value === null ? ORDER_BAD : value;
  }
  if (declared === 'boolean') {
    const lowered = casefold(text);
    if (TRUE_WORDS.has(lowered)) return true;
    if (FALSE_WORDS.has(lowered)) return false;
    return ORDER_BAD;
  }
  return text;
};

// ---------------------------------------------------------------------------
// _order_properties / _order_array_bounds / _is_array_property
// ---------------------------------------------------------------------------

/** One action's declared properties plus the required names among them. */
export interface OrderProperties {
  readonly properties: JsonObject;
  readonly required: ReadonlyArray<string>;
}

/** Read the argument schema off a compact action, defaulting to "none". */
export const orderProperties = (compact: JsonObject): OrderProperties => {
  const schema = field(compact, 'argument_schema');
  const properties = isJsonObject(schema) ? field(schema, 'properties') : null;
  if (!isJsonObject(properties) || Object.keys(properties).length === 0) {
    return { properties: {}, required: [] };
  }
  const declared = isJsonObject(schema) ? field(schema, 'required') : null;
  const required = (isJsonArray(declared) ? declared : []).filter(
    (name): name is string => typeof name === 'string' && hasField(properties, name)
  );
  return { properties, required };
};

/** Read one array property's own declared element bounds. */
export const orderArrayBounds = (specification: JsonValue): readonly [number, number] => {
  if (!isJsonObject(specification)) return [0, V2_MAX_ORDER_WORDS];
  const minimum = field(specification, 'minItems');
  const maximum = field(specification, 'maxItems');
  return [
    typeof minimum === 'number' && Number.isInteger(minimum) ? minimum : 0,
    typeof maximum === 'number' && Number.isInteger(maximum) ? maximum : V2_MAX_ORDER_WORDS,
  ];
};

/** Does this property take an ordered list rather than one scalar? */
export const isArrayProperty = (specification: JsonValue): boolean =>
  isJsonObject(specification) && field(specification, 'type') === 'array';

/** Does every action in this pool take its arguments as an ordered list? */
export const poolIsListed = (pool: ReadonlyArray<JsonObject>): boolean =>
  pool.every((item) =>
    Object.values(orderProperties(item).properties).some((specification) =>
      isArrayProperty(specification)
    )
  );

// ---------------------------------------------------------------------------
// _order_arguments
// ---------------------------------------------------------------------------

/**
 * Bind the order's remaining words to this action's own argument schema.
 *
 * Scalars bind one word each in required-then-optional order.  A single
 * array-typed property instead takes the whole remaining tail, one element per
 * word, because that is the only shape in which an ordered list can be written
 * on a command line — `u2 route T(38,34) T(39,35)`.  `defaults` are the values
 * a Tier-1 verb itself fixed, so they are never asked of the agent and never
 * overridden by position.
 */
export const orderArguments = (
  compact: JsonObject,
  values: ReadonlyArray<string>,
  defaults: Readonly<Record<string, JsonValue>> | null = null,
  element: ElementResolver = null
): Effect.Effect<JsonObject | null, OrderUnresolved | PlayerError> =>
  Effect.gen(function* () {
    const { properties, required } = orderProperties(compact);
    const supplied = defaults ?? {};
    // Null-prototype throughout: every key here is server-supplied (the action's
    // own `argument_schema`), and CPython's dicts have no inherited names.  A
    // property called `toString` must bind like any other word, and a property
    // called `__proto__` must be assignable as data rather than reparenting the
    // object we are about to put on the wire.
    const fixed = Object.create(null) as Record<string, JsonValue>;
    for (const [name, value] of Object.entries(supplied)) {
      if (hasField(properties, name)) fixed[name] = value;
    }
    const hasDefaults = Object.keys(supplied).length > 0;
    if (Object.keys(properties).length === 0) {
      return values.length === 0 && !hasDefaults ? {} : null;
    }
    const names = [
      ...required.filter((name) => !Object.hasOwn(fixed, name)),
      ...Object.keys(properties).filter(
        (name) => !required.includes(name) && !Object.hasOwn(fixed, name)
      ),
    ];
    const arrays = names.filter((name) => isArrayProperty(field(properties, name)));
    if (arrays.length > 1) {
      // Two open-ended lists cannot be told apart on one line; the raw
      // `just batch --arguments JSON` form still expresses them exactly.
      return null;
    }
    const args = Object.assign(Object.create(null), fixed) as Record<string, JsonValue>;
    const tailName = arrays[0];
    if (tailName !== undefined) {
      const head = names.filter((name) => name !== tailName);
      if (values.length < head.length) return null;
      for (let index = 0; index < head.length; index += 1) {
        const name = head[index] as string;
        const converted = orderValue(field(properties, name), values[index] as string);
        if (converted === ORDER_BAD) return null;
        args[name] = converted;
      }
      const words = values.slice(head.length);
      const [minimum, maximum] = orderArrayBounds(field(properties, tailName));
      if (!required.includes(tailName) && words.length === 0) return args;
      if (!(minimum <= words.length && words.length <= maximum)) return null;
      const items: JsonValue[] = [];
      for (const word of words) {
        const value = element === null ? word : yield* element(word);
        if (value === ORDER_BAD) return null;
        items.push(value);
      }
      args[tailName] = items;
      return args;
    }
    const needed = required.filter((name) => !Object.hasOwn(fixed, name)).length;
    if (!(needed <= values.length && values.length <= names.length)) return null;
    for (let index = 0; index < Math.min(names.length, values.length); index += 1) {
      const name = names[index] as string;
      const converted = orderValue(field(properties, name), values[index] as string);
      if (converted === ORDER_BAD) return null;
      args[name] = converted;
    }
    return args;
  });

// ---------------------------------------------------------------------------
// _order_match
// ---------------------------------------------------------------------------

/** Bind one action's arguments, or select it by what distinguishes it. */
export const orderMatch = (
  compact: JsonObject,
  values: ReadonlyArray<string>,
  defaults: Readonly<Record<string, JsonValue>> | null = null,
  element: ElementResolver = null
): Effect.Effect<JsonObject | null, OrderUnresolved | PlayerError> =>
  Effect.gen(function* () {
    const args = yield* orderArguments(compact, values, defaults, element);
    if (args !== null) return args;
    const { properties } = orderProperties(compact);
    // A Tier-1 word fixes arguments; an action that cannot take them is not the
    // action that word names, however well its label reads.
    if (
      Object.keys(properties).length > 0 ||
      values.length === 0 ||
      (defaults !== null && Object.keys(defaults).length > 0)
    ) {
      return null;
    }
    // An action that takes no arguments is selected by what distinguishes it
    // from its siblings: its label, its named target, or a subject value the
    // catalog page already showed.
    const discriminators = yield* orderDiscriminators(compact);
    return discriminators.has(casefold(values.join(' '))) ? {} : null;
  });

// ---------------------------------------------------------------------------
// _default_arguments
// ---------------------------------------------------------------------------

/** Fill an action whose required arguments have exactly one legal value. */
export const defaultArguments = (compact: JsonObject): JsonObject | null => {
  const { properties, required } = orderProperties(compact);
  // Null-prototype for the same reason as `_order_arguments`: `required` is the
  // server's own list, and a required property named `__proto__` must become a
  // key rather than silently reparenting the arguments object.
  const args = Object.create(null) as Record<string, JsonValue>;
  for (const name of required) {
    const specification = field(properties, name);
    const choices = isJsonObject(specification) ? field(specification, 'enum') : null;
    if (!isJsonArray(choices) || choices.length !== 1) return null;
    args[name] = choices[0] as JsonValue;
  }
  return args;
};

// ---------------------------------------------------------------------------
// _named_target_id
// ---------------------------------------------------------------------------

/**
 * Find the opaque ID this actor's own catalog gave a named target.
 *
 * A worklist item is a production this seat has already been offered by name:
 * the `city.set_production` rows for the same city carry exactly the IDs
 * `set_worklist` accepts.  Nothing is invented — an unlisted name has no ID
 * here and the order fails closed.
 */
export const namedTargetId = (
  state: V2ClientState,
  actorId: string,
  text: string,
  deps: OrdersDeps
): Effect.Effect<string | null, PlayerError> =>
  Effect.gen(function* () {
    const found = new Set<string>();
    const wanted = casefold(text);
    for (const compact of yield* orderPool(state, deps)) {
      if (actorId !== '' && orderActor(compact) !== actorId) continue;
      const target = field(compact, 'target');
      if (!isJsonObject(target)) continue;
      const name = field(target, 'name');
      const identifier = field(target, 'id');
      if (
        typeof name === 'string' &&
        casefold(name) === wanted &&
        typeof identifier === 'string' &&
        identifier !== ''
      ) {
        found.add(identifier);
      }
    }
    if (found.size !== 1) return null;
    const [only] = found;
    return only ?? null;
  });

// ---------------------------------------------------------------------------
// _order_element_resolver
// ---------------------------------------------------------------------------

/**
 * Expand one list element to the opaque ID the wire requires.
 *
 * Coordinates resolve through the tile alias cache this seat already built from
 * pages it read; every other word is looked up by name in the same actor's
 * catalog.  `strict` is set only when exactly one capability is in play, so the
 * refusal can name the word that failed instead of degrading to "no cached
 * action takes those arguments".
 */
export const orderElementResolver = (
  state: V2ClientState,
  actorId: string,
  verb: string,
  options: { readonly strict: boolean },
  deps: OrdersDeps
): NonNullable<ElementResolver> =>
  (text: string): Effect.Effect<OrderValue, OrderUnresolved | PlayerError> =>
    Effect.gen(function* () {
      const tile = TILE_ALIAS_RE.exec(text) ?? ORDER_COORDINATE_RE.exec(text);
      if (tile !== null) {
        const x = Number.parseInt(tile[1] ?? '', 10);
        const y = Number.parseInt(tile[2] ?? '', 10);
        const outcome = yield* Effect.either(expandTileAlias(state, x, y));
        if (Either.isRight(outcome)) return outcome.right;
        if (options.strict) {
          return yield* Effect.fail(orderUnresolved(outcome.left.message, actorId));
        }
        return ORDER_BAD;
      }
      const identifier = yield* namedTargetId(state, actorId, text, deps);
      if (identifier !== null) return identifier;
      if (options.strict) {
        return yield* Effect.fail(
          orderUnresolved(
            `\`${verb}\` takes items this seat has been offered by name; ` +
              `no cached target is named ${text}`,
            actorId
          )
        );
      }
      return ORDER_BAD;
    });
