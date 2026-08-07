/**
 * The order line: what `just do "u1 found_city London"` is allowed to say.
 *
 * Ports `play/client.py` 8633-8676 (`V2_MAX_ORDERS`, `V2_MAX_ORDER_WORDS`,
 * `V2_ACTION_FAMILIES`, `ORDER_COORDINATE_RE`, `V2_TIER1_VERBS`), 8679-8687
 * (`_OrderUnresolved`) and 8688-8708 (`_parse_orders`).
 *
 * This is the only file in the unit with no dependencies: it fixes the
 * vocabulary and the shape of the line, and nothing here consults the cache.
 * The four refusals are the agent's first contact with the grammar, so their
 * wording is ported byte for byte — including CPython's `repr()` quoting of the
 * offending order in the word-count refusal.
 */
import { Data, Effect } from 'effect';
import { playerError, type PlayerError } from 'src/errors';
import type { JsonValue } from 'src/schema/primitives';

// ---------------------------------------------------------------------------
// Vocabulary (client.py:8633-8676)
// ---------------------------------------------------------------------------

/** One line carries at most eight orders; more is a typo, not a plan. */
export const V2_MAX_ORDERS = 8;

/** One order carries at most twelve words. */
export const V2_MAX_ORDER_WORDS = 12;

/**
 * The public kind families a first word may name (`unit sentry`, `city build`).
 *
 * These are the heads of the `kind` strings the catalog itself prints, so a
 * family word never invents a capability — it only narrows the pool.
 */
export const V2_ACTION_FAMILIES: ReadonlySet<string> = new Set([
  'city',
  'diplomacy',
  'economy',
  'government',
  'phase',
  'player',
  'pregame',
  'research',
  'spaceship',
  'unit',
]);

/** `x,y` written bare, without the `T(...)` the alias cache prints. */
export const ORDER_COORDINATE_RE = /^(-?[0-9]{1,4}),(-?[0-9]{1,4})$/;

/** One Tier-1 word: the capability it routes to and the arguments it fixes. */
export interface Tier1Verb {
  readonly kind: string;
  readonly operation: string;
  readonly arguments: Readonly<Record<string, JsonValue>>;
}

/**
 * Tier-1 vocabulary (redesign doc §4), client.py:8651-8676.
 *
 * These six words are the only ones `do` accepts that the catalog does not
 * print itself, and each is a fixed route to one advertised capability.  A verb
 * whose capability this seat's catalog does not advertise fails closed naming
 * the enumeration command — nothing here invents a synonym, and nothing here
 * can name an action the server did not offer.
 */
export const V2_TIER1_VERBS: Readonly<Record<string, Tier1Verb>> = {
  route: { kind: 'unit.order', operation: 'set_route', arguments: { mode: 'goto' } },
  patrol: { kind: 'unit.order', operation: 'set_route', arguments: { mode: 'patrol' } },
  build: { kind: 'city.set_production', operation: 'set_production', arguments: {} },
  queue: { kind: 'city.set_worklist', operation: 'set_worklist', arguments: {} },
  rally: { kind: 'city.set_rally', operation: 'set_rally', arguments: { persistent: false } },
  goal: { kind: 'research.set_goal', operation: 'set_goal', arguments: {} },
};

// ---------------------------------------------------------------------------
// _OrderUnresolved
// ---------------------------------------------------------------------------

/**
 * One order named no single cached capability; the batch must not run.
 *
 * CPython raised this; here it is a value in the error channel, because
 * `_order_outcomes` catches it per order and keeps the verdict.  `actorId` is
 * the actor the failed order asked for — it is what selects the enumeration
 * command the refusal names, and what tells `_order_fetch_targets` whose
 * catalog was never read.
 */
export class OrderUnresolved extends Data.TaggedError('OrderUnresolved')<{
  readonly reason: string;
  readonly actorId: string;
}> {}

/** Build one {@link OrderUnresolved}; the actor defaults to "none named". */
export const orderUnresolved = (reason: string, actorId = ''): OrderUnresolved =>
  new OrderUnresolved({ reason, actorId });

// ---------------------------------------------------------------------------
// CPython string primitives
// ---------------------------------------------------------------------------

/**
 * `str.split()` with no separator: split on runs of whitespace, drop the empty
 * edges.  `"".split()` is `[]` in CPython and `[""]` in JavaScript, and the
 * word count depends on the difference.
 */
export const pySplit = (text: string): ReadonlyArray<string> => {
  const trimmed = text.trim();
  return trimmed === '' ? [] : trimmed.split(/\s+/u);
};

/**
 * `repr()` of a `str`, for the one refusal that quotes the order back.
 *
 * CPython prefers single quotes and switches to double only when the string
 * contains a single quote and no double quote; printable non-ASCII is left
 * alone in Python 3.
 */
export const pyRepr = (text: string): string => {
  const quote = text.includes("'") && !text.includes('"') ? '"' : "'";
  const escaped = [...text]
    .map((character) => {
      if (character === '\\') return '\\\\';
      if (character === quote) return `\\${quote}`;
      if (character === '\n') return '\\n';
      if (character === '\r') return '\\r';
      if (character === '\t') return '\\t';
      const code = character.codePointAt(0) ?? 0;
      if (code < 0x20 || code === 0x7f) {
        return `\\x${code.toString(16).padStart(2, '0')}`;
      }
      return character;
    })
    .join('');
  return `${quote}${escaped}${quote}`;
};

/**
 * CPython `str.casefold()` — the real full fold, not `toLowerCase()`.
 *
 * `toLowerCase` is not a case fold: it leaves `ß` and `ﬀ` alone, keeps final
 * `ς` distinct from `σ`, and lowers Cherokee the wrong way.  That is not
 * theoretical here — this repository's own rulesets ship city names like
 * `Straßburg`, `Meißen` and `Weißenburg` (`data/nation/{alsatian,eastgerman,
 * anhaltian,badian,...}.ruleset`), and those arrive as `target.name` and as
 * compact-catalog subject words.  CPython answers `True` for
 * `"Strassburg".casefold() == "Straßburg".casefold()`, so `_named_target_id`
 * binds the worklist element and `_order_discriminators` selects the
 * argument-free action; a `toLowerCase` port compares `strassburg` to
 * `straßburg`, misses, and refuses with `no cached target is named Strassburg`.
 * The refusal surface is the agent's only recovery path, so the leniency
 * CPython chose is not ours to drop.
 *
 * The table lives in `src/render/show-unicode.ts` (U09), which PORT_MAP §"U09
 * gains two files" names as the one place a Python-compatible fold is stated;
 * re-exported here because `casefold` is part of this unit's published surface
 * (PORT_MAP §"Addendum — U15 orders engine") and U15's other files, plus
 * `src/render/decisions.ts`, import it from here.
 */
export { casefold } from 'src/render/show-unicode';

// ---------------------------------------------------------------------------
// _parse_orders
// ---------------------------------------------------------------------------

/** Why one order line was refused before anything touched the cache. */
export type ParseOrdersFailure =
  | { readonly _tag: 'NotAString' }
  | { readonly _tag: 'Empty' }
  | { readonly _tag: 'TooMany'; readonly count: number }
  | { readonly _tag: 'TooManyWords'; readonly order: string };

/** The exact CPython sentence for one parse failure. */
export const parseOrdersMessage = (failure: ParseOrdersFailure): string => {
  switch (failure._tag) {
    case 'NotAString':
      return 'just do needs one quoted, semicolon-separated string';
    case 'Empty':
      return 'just do needs at least one order, for example `just do "u1 found_city London"`';
    case 'TooMany':
      return `just do accepts 1 through ${V2_MAX_ORDERS} orders; this line has ${failure.count}`;
    case 'TooManyWords':
      return `order ${pyRepr(failure.order)} has more than ${V2_MAX_ORDER_WORDS} words`;
  }
};

/**
 * Split one quoted line into its orders, or say why it is not one.
 *
 * Pure and total: the caller maps the failure onto `PlayerError`.  Nothing
 * about the line is repaired — an empty segment is dropped (`"a; ; b"` is two
 * orders, exactly as CPython read it), but a line that overruns either bound is
 * refused whole rather than truncated.
 */
export const parseOrdersEither = (
  text: unknown
): { readonly ok: true; readonly orders: ReadonlyArray<string> } | {
  readonly ok: false;
  readonly failure: ParseOrdersFailure;
} => {
  if (typeof text !== 'string') return { ok: false, failure: { _tag: 'NotAString' } };
  const orders = text
    .split(';')
    .map((part) => part.trim())
    .filter((part) => part !== '');
  if (orders.length === 0) return { ok: false, failure: { _tag: 'Empty' } };
  if (orders.length > V2_MAX_ORDERS) {
    return { ok: false, failure: { _tag: 'TooMany', count: orders.length } };
  }
  for (const order of orders) {
    if (pySplit(order).length > V2_MAX_ORDER_WORDS) {
      return { ok: false, failure: { _tag: 'TooManyWords', order } };
    }
  }
  return { ok: true, orders };
};

/**
 * `_parse_orders` — the exported form U16 calls.
 *
 * The failure is a `PlayerError`, which `cli-main` renders as `error: …` on
 * stderr with exit 2, exactly as CPython's `PlayerError` did.
 */
export const parseOrders = (text: unknown): Effect.Effect<ReadonlyArray<string>, PlayerError> => {
  const outcome = parseOrdersEither(text);
  return outcome.ok
    ? Effect.succeed(outcome.orders)
    : Effect.fail(playerError(parseOrdersMessage(outcome.failure)));
};
