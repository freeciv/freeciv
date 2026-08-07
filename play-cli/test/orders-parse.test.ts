/**
 * The order line and the words inside it.
 *
 * Ports the shape half of `test_v2_do_refuses_every_order_when_one_cannot_be_
 * resolved` (the "1 through 8 orders" / "at least one order" bounds block) plus
 * the enum and coercion assertions inside
 * `test_v2_order_grammar_uses_only_what_the_catalog_advertised`.
 *
 * Nothing here reads the cache: this file is the grammar, and every case is a
 * pure function of the words.
 */
import { describe, expect, test } from 'bun:test';
import { Effect, Either } from 'effect';
import type { JsonObject, JsonValue } from 'src/schema/primitives';
import {
  ORDER_BAD,
  V2_MAX_ORDERS,
  V2_MAX_ORDER_WORDS,
  V2_TIER1_VERBS,
  casefold,
  defaultArguments,
  isArrayProperty,
  kindHead,
  kindTail,
  orderArguments,
  orderArrayBounds,
  orderProperties,
  orderTargetKeys,
  orderValue,
  orderVerbs,
  parseOrders,
  pyRepr,
  pySplit,
} from 'src/services/orders';

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const parsed = (text: unknown): Either.Either<ReadonlyArray<string>, { readonly message: string }> =>
  Effect.runSync(Effect.either(parseOrders(text)));

const refusal = (text: unknown): string => {
  const outcome = parsed(text);
  expect(Either.isLeft(outcome)).toBe(true);
  return Either.isLeft(outcome) ? outcome.left.message : '';
};

const orders = (text: unknown): ReadonlyArray<string> => {
  const outcome = parsed(text);
  if (Either.isLeft(outcome)) throw new Error(`expected orders, got ${outcome.left.message}`);
  return outcome.right;
};

const compact = (
  kind: string,
  options: {
    readonly operation?: string;
    readonly label?: string;
    readonly schema?: JsonValue;
    readonly target?: JsonValue;
  } = {}
): JsonObject => ({
  action_id: 'action_test',
  kind,
  label: options.label ?? 'Do it',
  subject: { operation: options.operation ?? '', actor: { id: 'unit_x', type: 'unit' } },
  target: options.target ?? null,
  argument_schema: options.schema ?? { type: 'object' },
});

const bind = (
  action: JsonObject,
  values: ReadonlyArray<string>,
  defaults: Readonly<Record<string, JsonValue>> | null = null
): JsonObject | null => Effect.runSync(orderArguments(action, values, defaults));

// ---------------------------------------------------------------------------
// _parse_orders
// ---------------------------------------------------------------------------

describe('_parse_orders', () => {
  test('anything but a string is refused before the line is read', () => {
    expect(refusal(undefined)).toBe('just do needs one quoted, semicolon-separated string');
    expect(refusal(null)).toBe('just do needs one quoted, semicolon-separated string');
    expect(refusal(7)).toBe('just do needs one quoted, semicolon-separated string');
    expect(refusal(['u1 sentry'])).toBe(
      'just do needs one quoted, semicolon-separated string'
    );
  });

  test('a line of nothing but separators names the example order', () => {
    const message = 'just do needs at least one order, for example `just do "u1 found_city London"`';
    expect(refusal('  ;  ')).toBe(message);
    expect(refusal('')).toBe(message);
    expect(refusal(';;;')).toBe(message);
  });

  test('nine orders are refused with the count that was written', () => {
    const line = Array.from({ length: 9 }, () => 'u1 move 32,72').join('; ');
    expect(refusal(line)).toBe(`just do accepts 1 through ${V2_MAX_ORDERS} orders; this line has 9`);
    // Eight is the last accepted line.
    expect(orders(Array.from({ length: 8 }, () => 'u1 move 32,72').join('; '))).toHaveLength(8);
  });

  test('a thirteen-word order is quoted back with CPython repr', () => {
    const long = `u1 route ${Array.from({ length: 12 }, (_unused, index) => `3${index},7`).join(' ')}`;
    expect(pySplit(long).length).toBeGreaterThan(V2_MAX_ORDER_WORDS);
    expect(refusal(long)).toBe(`order ${pyRepr(long)} has more than ${V2_MAX_ORDER_WORDS} words`);
    expect(refusal(long)).toContain(`'${long}'`);
  });

  test('empty segments are dropped and every order is stripped', () => {
    expect(orders('  u1 sentry ;; c1 build Warriors  ;')).toEqual([
      'u1 sentry',
      'c1 build Warriors',
    ]);
  });

  test('repr switches quotes exactly where CPython does', () => {
    expect(pyRepr('u1 found_city London')).toBe("'u1 found_city London'");
    expect(pyRepr("u1 found_city O'Hare")).toBe(`"u1 found_city O'Hare"`);
    expect(pyRepr(`u1 "a" 'b'`)).toBe(`'u1 "a" \\'b\\''`);
    expect(pyRepr('a\nb')).toBe("'a\\nb'");
  });

  test('pySplit is CPython str.split(), not String.split', () => {
    expect(pySplit('')).toEqual([]);
    expect(pySplit('   ')).toEqual([]);
    expect(pySplit(' u1   move\t32,72 ')).toEqual(['u1', 'move', '32,72']);
  });
});

// ---------------------------------------------------------------------------
// casefold — every one of the thirteen `.casefold()` calls in the span
// ---------------------------------------------------------------------------

describe('casefold is CPython str.casefold(), not String.toLowerCase()', () => {
  test('the full folds, captured from python3 -c "print(...casefold())"', () => {
    expect(casefold('Große STRASSE ﬀ ﬃ ſ ΑΣ ΣΣ ς µ İ ı Ꭰ ẞ')).toBe(
      'grosse strasse ff ffi s ασ σσ σ μ i̇ ı Ꭰ ss'
    );
  });

  test('toLowerCase answers differently on every one of them', () => {
    for (const sample of ['Große', 'ﬀ', 'ﬃ', 'ſ', 'ΑΣ', 'ς', 'µ', 'Ꭰ', 'ẞ']) {
      expect(casefold(sample)).not.toBe(sample.toLowerCase());
    }
  });

  test('folding is idempotent, as CPython’s is', () => {
    for (const sample of ['Große', 'ﬀ', 'ΑΣ', 'ẞ', 'u1 found_city London']) {
      expect(casefold(casefold(sample))).toBe(casefold(sample));
    }
  });

  test('the city names this repository ships fold onto their ASCII spelling', () => {
    // `data/nation/{alsatian,eastgerman,anhaltian,badian,...}.ruleset`; these
    // reach the matcher as `target.name` and as compact-catalog subject words,
    // and CPython answers True for each pair.
    for (const [ascii, native] of [
      ['Strassburg', 'Straßburg'],
      ['Meissen', 'Meißen'],
      ['Weissenburg', 'Weißenburg'],
      ['Rosslau', 'Roßlau'],
    ] as const) {
      expect(casefold(ascii)).toBe(casefold(native));
    }
  });
});

// ---------------------------------------------------------------------------
// _order_value
// ---------------------------------------------------------------------------

describe('_order_value', () => {
  test('an enum matches either spelling, case-insensitively, and returns the advertised value', () => {
    const specification: JsonObject = { type: 'string', enum: ['Currency', 'Alphabet'] };
    expect(orderValue(specification, 'currency')).toBe('Currency');
    expect(orderValue(specification, 'CURRENCY')).toBe('Currency');
    // The JSON literal the catalog prints resolves too.
    expect(orderValue(specification, '"Alphabet"')).toBe('Alphabet');
  });

  test('a value outside the advertised enum is refused, not coerced', () => {
    expect(orderValue({ type: 'string', enum: ['Currency'] }, 'Pottery')).toBe(ORDER_BAD);
  });

  test('an enum member folds the way CPython folds it, not the way toLowerCase does', () => {
    // A localised server advertises `Schießpulver` for Gunpowder
    // (`translations/core/de.po`); CPython's `casefold` folds `ß` onto `ss`, so
    // the ASCII spelling an agent can actually type binds the advertised value.
    const specification: JsonObject = { type: 'string', enum: ['Schießpulver', 'Alphabet'] };
    for (const spelling of ['Schiesspulver', 'SCHIESSPULVER', 'schießpulver', 'Schießpulver']) {
      expect(orderValue(specification, spelling)).toBe('Schießpulver');
    }
    // A word that folds onto nothing advertised is still refused, not guessed.
    expect(orderValue(specification, 'Schiesspulverr')).toBe(ORDER_BAD);
  });

  test('a non-string enum member is matched through its scalar rendering', () => {
    expect(orderValue({ enum: [true, false] }, 'yes')).toBe(true);
    expect(orderValue({ enum: [true, false] }, 'false')).toBe(false);
    expect(orderValue({ enum: [7, 9] }, '9')).toBe(9);
  });

  test('integers are canonical only', () => {
    expect(orderValue({ type: 'integer' }, '42')).toBe(42);
    expect(orderValue({ type: 'integer' }, '-7')).toBe(-7);
    expect(orderValue({ type: 'integer' }, '0')).toBe(0);
    expect(orderValue({ type: 'integer' }, '007')).toBe(ORDER_BAD);
    expect(orderValue({ type: 'integer' }, '1.0')).toBe(ORDER_BAD);
    expect(orderValue({ type: 'integer' }, '')).toBe(ORDER_BAD);
    expect(orderValue({ type: 'integer' }, '12345678901')).toBe(ORDER_BAD);
  });

  test('numbers follow CPython float(), not Number()', () => {
    expect(orderValue({ type: 'number' }, '1.5')).toBe(1.5);
    expect(orderValue({ type: 'number' }, '1e3')).toBe(1000);
    // CPython takes underscore separators and rejects hex; Number() does the reverse.
    expect(orderValue({ type: 'number' }, '1_000')).toBe(1000);
    expect(orderValue({ type: 'number' }, '0x10')).toBe(ORDER_BAD);
    expect(orderValue({ type: 'number' }, '')).toBe(ORDER_BAD);
    expect(orderValue({ type: 'number' }, 'Infinity')).toBe(Infinity);
  });

  test('booleans take the four spellings each and nothing else', () => {
    for (const word of ['true', 'YES', 'on', '1']) {
      expect(orderValue({ type: 'boolean' }, word)).toBe(true);
    }
    for (const word of ['false', 'No', 'off', '0']) {
      expect(orderValue({ type: 'boolean' }, word)).toBe(false);
    }
    expect(orderValue({ type: 'boolean' }, 'maybe')).toBe(ORDER_BAD);
  });

  test('an undeclared or unshaped specification passes the word through', () => {
    expect(orderValue({ type: 'string' }, 'London')).toBe('London');
    expect(orderValue(null, 'London')).toBe('London');
    expect(orderValue({ enum: [] }, 'London')).toBe('London');
  });
});

// ---------------------------------------------------------------------------
// _order_verbs / _order_target_keys / kind splitting
// ---------------------------------------------------------------------------

describe('_order_verbs', () => {
  test('a descriptor answers only to the four words it advertises', () => {
    const verbs = orderVerbs(compact('research.set_goal', { operation: 'set_goal' }));
    expect([...verbs].sort()).toEqual([
      'research.set_goal',
      'research.set_goal/set_goal',
      'set_goal',
      'set_goal/set_goal',
    ]);
    expect(verbs.has('target')).toBe(false);
  });

  test('an operation-free descriptor answers to its kind and tail alone', () => {
    expect([...orderVerbs(compact('unit.sentry'))].sort()).toEqual(['sentry', 'unit.sentry']);
  });

  test('kind splitting matches CPython split(".", 1)', () => {
    expect(kindHead('city.set_production')).toBe('city');
    expect(kindTail('city.set_production')).toBe('set_production');
    expect(kindTail('player.set_multiplier.value')).toBe('set_multiplier.value');
    expect(kindHead('sentry')).toBe('sentry');
    expect(kindTail('sentry')).toBe('sentry');
  });
});

describe('_order_target_keys', () => {
  test('both coordinate spellings name the same two keys', () => {
    expect(orderTargetKeys('T(31,72)')).toEqual(['T(31,72)', '@31,72']);
    expect(orderTargetKeys('t(31, 72)')).toEqual(['T(31,72)', '@31,72']);
    expect(orderTargetKeys('31,72')).toEqual(['T(31,72)', '@31,72']);
    expect(orderTargetKeys('-3,-4')).toEqual(['T(-3,-4)', '@-3,-4']);
  });

  test('anything else names no target key', () => {
    for (const token of ['London', '31', 'T(31)', '31,72,3', '']) {
      expect(orderTargetKeys(token)).toEqual([]);
    }
  });
});

// ---------------------------------------------------------------------------
// _order_properties / _order_array_bounds / _order_arguments
// ---------------------------------------------------------------------------

describe('_order_arguments', () => {
  test('an action with no properties binds only an empty word list', () => {
    const action = compact('unit.sentry', { operation: 'sentry' });
    expect(bind(action, [])).toEqual({});
    expect(bind(action, ['now'])).toBeNull();
    // A Tier-1 word that fixes arguments cannot name an action that takes none.
    expect(bind(action, [], V2_TIER1_VERBS['route']?.arguments ?? null)).toBeNull();
    expect(bind(action, [], {})).toEqual({});
  });

  test('required scalars bind by position and optional ones may be omitted', () => {
    const action = compact('unit.found_city', {
      operation: 'found_city',
      schema: {
        type: 'object',
        properties: { city_name: { type: 'string' }, note: { type: 'string' } },
        required: ['city_name'],
      },
    });
    expect(bind(action, ['London'])).toEqual({ city_name: 'London' });
    expect(bind(action, ['London', 'first'])).toEqual({ city_name: 'London', note: 'first' });
    expect(bind(action, [])).toBeNull();
    expect(bind(action, ['London', 'first', 'extra'])).toBeNull();
  });

  test('a fixed default is never asked of the agent and never bound by position', () => {
    const action = compact('unit.order', {
      operation: 'set_route',
      schema: {
        type: 'object',
        properties: { mode: { type: 'string' }, tiles: { type: 'array' } },
        required: ['mode', 'tiles'],
      },
    });
    expect(bind(action, ['a', 'b'], { mode: 'goto' })).toEqual({
      mode: 'goto',
      tiles: ['a', 'b'],
    });
  });

  test('one array property takes the whole tail and honours its own bounds', () => {
    const action = compact('unit.order', {
      operation: 'set_route',
      schema: {
        type: 'object',
        properties: { tiles: { type: 'array', minItems: 2, maxItems: 3 } },
        required: ['tiles'],
      },
    });
    expect(bind(action, ['a', 'b'])).toEqual({ tiles: ['a', 'b'] });
    expect(bind(action, ['a', 'b', 'c'])).toEqual({ tiles: ['a', 'b', 'c'] });
    expect(bind(action, ['a'])).toBeNull();
    expect(bind(action, ['a', 'b', 'c', 'd'])).toBeNull();
  });

  test('an optional array simply stays absent when no words are left', () => {
    const action = compact('city.set_worklist', {
      operation: 'set_worklist',
      schema: { type: 'object', properties: { items: { type: 'array', minItems: 1 } } },
    });
    expect(bind(action, [])).toEqual({});
  });

  test('two open-ended lists cannot be told apart on one line', () => {
    const action = compact('unit.order', {
      schema: {
        type: 'object',
        properties: { first: { type: 'array' }, second: { type: 'array' } },
      },
    });
    expect(bind(action, ['a'])).toBeNull();
  });

  test('a word that will not coerce is a non-match, never a coercion', () => {
    const action = compact('city.buy', {
      schema: { type: 'object', properties: { gold: { type: 'integer' } }, required: ['gold'] },
    });
    expect(bind(action, ['12'])).toEqual({ gold: 12 });
    expect(bind(action, ['lots'])).toBeNull();
  });

  test('required names outside the property map are ignored, as CPython did', () => {
    const action = compact('unit.found_city', {
      schema: {
        type: 'object',
        properties: { city_name: { type: 'string' } },
        required: ['city_name', 'ghost'],
      },
    });
    expect(orderProperties(action).required).toEqual(['city_name']);
    expect(bind(action, ['London'])).toEqual({ city_name: 'London' });
  });

  test('array bounds default to the word cap when undeclared', () => {
    expect(orderArrayBounds({ type: 'array' })).toEqual([0, V2_MAX_ORDER_WORDS]);
    expect(orderArrayBounds({ type: 'array', minItems: 1, maxItems: 4 })).toEqual([1, 4]);
    // `True` is an int in CPython but never a bound.
    expect(orderArrayBounds({ type: 'array', minItems: true })).toEqual([0, V2_MAX_ORDER_WORDS]);
    expect(orderArrayBounds(null)).toEqual([0, V2_MAX_ORDER_WORDS]);
    expect(isArrayProperty({ type: 'array' })).toBe(true);
    expect(isArrayProperty({ type: 'string' })).toBe(false);
  });

  test('_order_properties reads nothing off a schema without properties', () => {
    expect(orderProperties(compact('unit.sentry'))).toEqual({ properties: {}, required: [] });
    expect(orderProperties(compact('unit.sentry', { schema: null }))).toEqual({
      properties: {},
      required: [],
    });
  });

  test('_default_arguments fills only a required enum with exactly one member', () => {
    const single = compact('pregame.configure', {
      schema: {
        type: 'object',
        properties: { nation: { enum: ['nation_a'] } },
        required: ['nation'],
      },
    });
    expect(defaultArguments(single)).toEqual({ nation: 'nation_a' });
    const two = compact('pregame.configure', {
      schema: {
        type: 'object',
        properties: { nation: { enum: ['nation_a', 'nation_b'] } },
        required: ['nation'],
      },
    });
    expect(defaultArguments(two)).toBeNull();
    expect(defaultArguments(compact('phase.end', { operation: 'end' }))).toEqual({});
  });
});

// ---------------------------------------------------------------------------
// The argument schema is the server's key set, not JavaScript's
// ---------------------------------------------------------------------------

/**
 * The wire form is the assertion here, because the wire form is the divergence.
 * `toEqual` cannot tell a bound `toString` from an inherited one, and it cannot
 * tell an own `__proto__` key from a reparented object — `JSON.stringify` can,
 * and it is exactly the bytes `just batch` would send.
 *
 * Every expectation below is the output of the real CPython `_order_arguments`
 * / `_default_arguments` on the same schema (see NOTES.md §18.10).  A property
 * schema is drift-shaped input: the server names these keys, nothing validates
 * them against `Object.prototype`, and PLAN.md's "Known traps" records schema
 * drift as the cause of three incidents.
 */
describe('an argument named for an inherited property still binds', () => {
  const wire = (bound: JsonObject | null): string | null =>
    bound === null ? null : JSON.stringify(bound);

  test('an optional property named `toString` is a name, not a method', () => {
    const action = compact('unit.order', {
      schema: {
        type: 'object',
        properties: { toString: { type: 'string' }, z: { type: 'string' } },
        required: [],
      },
    });
    // CPython binds the first declared property; `toString` must not be
    // filtered out of `names` and hand the word to `z` instead.
    expect(wire(bind(action, ['hi']))).toBe('{"toString":"hi"}');
    expect(wire(bind(action, ['hi', 'there']))).toBe('{"toString":"hi","z":"there"}');
  });

  test('a required property named `constructor` is required, not inherited', () => {
    const action = compact('unit.order', {
      schema: {
        type: 'object',
        properties: { constructor: { type: 'string' }, z: { type: 'string' } },
        required: ['constructor'],
      },
    });
    expect(wire(bind(action, ['hi']))).toBe('{"constructor":"hi"}');
    // It counts toward `needed`, so no words at all is still a non-match.
    expect(bind(action, [])).toBeNull();
  });

  test('a property named `__proto__` becomes a key, never a prototype', () => {
    const action = compact('unit.order', {
      schema: {
        type: 'object',
        // Computed, because a bare `__proto__:` in an object literal is the
        // prototype setter and would build a schema with no properties at all.
        properties: { ['__proto__']: { type: 'string' } },
        required: ['__proto__'],
      },
    });
    const bound = bind(action, ['hi']);
    expect(wire(bound)).toBe('{"__proto__":"hi"}');
    expect(Object.keys(bound ?? {})).toEqual(['__proto__']);
  });

  test('every other inherited name binds too, rather than vanishing', () => {
    for (const name of [
      'valueOf',
      'hasOwnProperty',
      'isPrototypeOf',
      'propertyIsEnumerable',
      'toLocaleString',
    ]) {
      const action = compact('unit.order', {
        schema: {
          type: 'object',
          properties: { [name]: { type: 'string' } },
          required: [name],
        },
      });
      expect(wire(bind(action, ['hi']))).toBe(`{${JSON.stringify(name)}:"hi"}`);
    }
  });

  test('a fixed default keyed by an inherited name is still fixed', () => {
    const action = compact('unit.order', {
      operation: 'set_route',
      schema: {
        type: 'object',
        properties: { toString: { type: 'string' }, tiles: { type: 'array' } },
        required: ['toString', 'tiles'],
      },
    });
    // `toString` is supplied, so it leaves `names` and does not count toward
    // `needed`; the whole tail belongs to `tiles`.
    expect(wire(bind(action, ['a', 'b'], { toString: 'goto' }))).toBe(
      '{"toString":"goto","tiles":["a","b"]}'
    );
  });

  test('_default_arguments fills an inherited name like any other', () => {
    const action = compact('pregame.configure', {
      schema: {
        type: 'object',
        properties: { ['__proto__']: { enum: ['only'] } },
        required: ['__proto__'],
      },
    });
    expect(wire(defaultArguments(action))).toBe('{"__proto__":"only"}');
  });
});
