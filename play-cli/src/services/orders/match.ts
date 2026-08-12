/**
 * What one cached action answers to.
 *
 * Ports `play/client.py` 8711-8762 (`_order_pool`, `_order_actor`,
 * `_order_operation`, `_order_verbs`, `_order_target_keys`), 8904-8921
 * (`_order_discriminators`) and 9027-9039 (`_order_resolution`).
 *
 * Every word this file accepts is a word the descriptor advertises about
 * itself: its public kind, the tail of that kind, its operation, the
 * `kind/operation` form the compact renderer prints, its label, its named
 * target, or a subject value the catalog page already showed.  No synonym is
 * invented, so a verb that resolves is a verb the agent read on a page.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { named } from 'src/render/primitives';
import {
  field,
  isJsonObject,
  type JsonObject,
} from 'src/schema/primitives';
import { actionTargetKey } from 'src/services/aliases';
import { cachedDescriptors } from 'src/services/catalog-cache';
import type { V2ClientState } from 'src/services/session-store';
import { casefold, ORDER_COORDINATE_RE } from 'src/services/orders/parse';
import { TILE_ALIAS_RE } from 'src/constants';

// ---------------------------------------------------------------------------
// The U11 seam
// ---------------------------------------------------------------------------

/**
 * The one U11 renderer the order matcher is defined in terms of.
 *
 * `_compact_legal_action` is the leak-guarded projection every row, alias and
 * order match reads: it is what drops `subject.target` down to `target`, folds
 * `arguments_schema` to `argument_schema`, and withholds an internal
 * discriminator's value while keeping its key.  U11 owns it; it arrives here
 * injected so this unit can be written and tested against the frozen contract.
 */
export interface OrdersDeps {
  readonly compactLegalAction: (descriptor: JsonObject) => Effect.Effect<JsonObject, PlayerError>;
}

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

/** One order bound to exactly one cached capability. */
export interface ResolvedOrder {
  /** The order text as the agent wrote it. */
  readonly order: string;
  readonly action_id: string;
  readonly kind: string;
  readonly operation: string;
  readonly label: string;
  readonly actor_id: string;
  readonly target_key: string;
  readonly arguments: JsonObject;
}

/** One order's verdict: resolved, or refused with the reason and its actor. */
export interface OrderOutcome {
  readonly text: string;
  readonly resolved: ResolvedOrder | null;
  readonly reason: string;
  readonly actor: string;
}

/**
 * `_LEGAL_SUBJECT_RESERVED` (client.py:3590-3593).
 *
 * The subject keys that are structure rather than discrimination.  Everything
 * else on a compact subject is a value the catalog page printed, so it can name
 * the action it distinguishes.  PORT_MAP scoped U11's span from 3596, one line
 * below this set, so it lands here; see NOTES.md.
 */
export const LEGAL_SUBJECT_RESERVED: ReadonlySet<string> = new Set([
  'operation',
  'actor',
  'target',
  'probability',
  'legality',
  'consuming',
  'variant',
  'gold_cost',
]);

// ---------------------------------------------------------------------------
// Small readers over one compact action
// ---------------------------------------------------------------------------

/** Read one string field off a compact action, or `""`. */
export const compactText = (compact: JsonObject, key: string): string => {
  const value = field(compact, key);
  return typeof value === 'string' ? value : '';
};

// ---------------------------------------------------------------------------
// _order_pool
// ---------------------------------------------------------------------------

/** Compact every cached descriptor that still names the newest revision. */
export const orderPool = (
  state: V2ClientState,
  deps: OrdersDeps
): Effect.Effect<ReadonlyArray<JsonObject>, PlayerError> =>
  Effect.forEach(cachedDescriptors(state), (descriptor) => deps.compactLegalAction(descriptor));

// ---------------------------------------------------------------------------
// _order_actor / _order_operation
// ---------------------------------------------------------------------------

/** Name the actor this compact action belongs to, or `""`. */
export const orderActor = (compact: JsonObject): string => {
  const subject = field(compact, 'subject');
  const actor = isJsonObject(subject) ? field(subject, 'actor') : null;
  const identifier = isJsonObject(actor) ? field(actor, 'id') : null;
  return typeof identifier === 'string' ? identifier : '';
};

/** Name the operation this compact action performs, or `""`. */
export const orderOperation = (compact: JsonObject): string => {
  const subject = field(compact, 'subject');
  const operation = isJsonObject(subject) ? field(subject, 'operation') : null;
  return typeof operation === 'string' ? operation : '';
};

/** CPython `kind.split(".", 1)[-1]` — everything after the first dot. */
export const kindTail = (kind: string): string => {
  const dot = kind.indexOf('.');
  return dot === -1 ? kind : kind.slice(dot + 1);
};

/** CPython `kind.split(".", 1)[0]` — the family head. */
export const kindHead = (kind: string): string => {
  const dot = kind.indexOf('.');
  return dot === -1 ? kind : kind.slice(0, dot);
};

// ---------------------------------------------------------------------------
// _order_verbs
// ---------------------------------------------------------------------------

/**
 * Name every word this cached action answers to, as it advertises itself.
 *
 * Only what the descriptor carries is accepted — its public kind, the tail of
 * that kind, its operation, and the `kind/operation` form the compact renderer
 * prints.
 */
export const orderVerbs = (compact: JsonObject): ReadonlySet<string> => {
  const kind = compactText(compact, 'kind');
  const tail = kindTail(kind);
  const verbs = new Set([kind, tail]);
  const operation = orderOperation(compact);
  if (operation !== '') {
    verbs.add(operation);
    verbs.add(`${kind}/${operation}`);
    verbs.add(`${tail}/${operation}`);
  }
  return new Set([...verbs].map(casefold));
};

// ---------------------------------------------------------------------------
// _order_target_keys
// ---------------------------------------------------------------------------

/** Return the target keys a coordinate word could name, or `[]`. */
export const orderTargetKeys = (token: string): ReadonlyArray<string> => {
  const tile = TILE_ALIAS_RE.exec(token) ?? ORDER_COORDINATE_RE.exec(token);
  if (tile === null) return [];
  const x = Number.parseInt(tile[1] ?? '', 10);
  const y = Number.parseInt(tile[2] ?? '', 10);
  if (Number.isNaN(x) || Number.isNaN(y)) return [];
  return [`T(${x},${y})`, `@${x},${y}`];
};

// ---------------------------------------------------------------------------
// _order_discriminators
// ---------------------------------------------------------------------------

/** Name the words that identify one argument-free action among its peers. */
export const orderDiscriminators = (
  compact: JsonObject
): Effect.Effect<ReadonlySet<string>, PlayerError> =>
  Effect.gen(function* () {
    const words = new Set([casefold(compactText(compact, 'label'))]);
    const target = field(compact, 'target');
    const targetKey = yield* actionTargetKey(target);
    if (targetKey !== '') words.add(casefold(targetKey));
    if (isJsonObject(target)) {
      const name = field(target, 'name');
      if (typeof name === 'string' && name !== '') words.add(casefold(name));
    }
    const subject = field(compact, 'subject');
    if (isJsonObject(subject)) {
      for (const [key, value] of Object.entries(subject)) {
        if (LEGAL_SUBJECT_RESERVED.has(key)) continue;
        words.add(casefold(named(value)));
      }
    }
    return words;
  });

// ---------------------------------------------------------------------------
// _order_resolution
// ---------------------------------------------------------------------------

/** Freeze one matched action plus its bound arguments into a sendable order. */
export const orderResolution = (
  compact: JsonObject,
  text: string,
  args: JsonObject
): Effect.Effect<ResolvedOrder, PlayerError> =>
  Effect.map(actionTargetKey(field(compact, 'target')), (targetKey) => ({
    order: text,
    action_id: compactText(compact, 'action_id'),
    kind: compactText(compact, 'kind'),
    operation: orderOperation(compact),
    label: compactText(compact, 'label'),
    actor_id: orderActor(compact),
    target_key: targetKey,
    arguments: args,
  }));
