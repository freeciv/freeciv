/**
 * Resolving one order, then the whole batch.
 *
 * Ports `play/client.py` 9042-9171 (`_resolve_order`), 9266-9278
 * (`_order_outcomes`), 9281-9301 (`_order_fetch_targets`), 9304-9311
 * (`_resolve_orders`) and 9314-9355 (`_resolve_orders_fetching`).
 *
 * Resolution reads the cache and nothing else: `resolveOrder` never touches the
 * network, and `resolveOrders` refuses the whole batch if any one order cannot
 * be bound.  `resolveOrdersFetching` is the single exception, and it is a
 * *pre*-fetch, not a retry: an actor whose catalog this seat has never read is
 * drained once, up front, and then the batch is re-resolved whole — so a
 * genuinely bad verb still refuses with the per-order table and no order
 * reaches the wire.
 */
import { Effect } from 'effect';
import { ACTION_ALIAS_RE, ACTOR_ID_RE, ENTITY_ALIAS_RE } from 'src/constants';
import { playerError, type LockTimeoutError, type PlayerError } from 'src/errors';
import { field, type JsonObject, type JsonValue } from 'src/schema/primitives';
import { aliasMap } from 'src/services/aliases';
import { expandAlias, expandActionAlias } from 'src/services/alias-expand';
import type { LegalPageFetcher } from 'src/services/alias-refresh';
import { PrivateFs } from 'src/services/private-fs';
import {
  SessionStore,
  type Session,
  type V2ClientState,
} from 'src/services/session-store';
import {
  casefold,
  orderUnresolved,
  pySplit,
  V2_ACTION_FAMILIES,
  V2_TIER1_VERBS,
  type OrderUnresolved,
} from 'src/services/orders/parse';
import {
  compactText,
  kindHead,
  orderActor,
  orderOperation,
  orderPool,
  orderResolution,
  orderTargetKeys,
  orderVerbs,
  type OrderOutcome,
  type OrdersDeps,
  type ResolvedOrder,
} from 'src/services/orders/match';
import {
  orderElementResolver,
  orderMatch,
  poolIsListed,
} from 'src/services/orders/arguments';
import { actionTargetKey } from 'src/services/aliases';
import { unresolvedReport } from 'src/services/orders/report';

/** `OrdersDeps` plus the drain the pre-fetch path needs.  U11 supplies both. */
export interface OrdersFetchDeps extends OrdersDeps {
  /** `_drain_legal_unlocked` — enumerate one actor's catalog, taking no lock. */
  readonly drainLegal: LegalPageFetcher;
}

/** What `resolveOrders`/`resolveOrdersFetching` hand back to `do`. */
export interface OrderOutcomes {
  readonly resolved: ReadonlyArray<ResolvedOrder>;
  /** The cache as it stands after any pre-fetch; unchanged when none ran. */
  readonly state: V2ClientState;
  /** The `fetched u1 options (rev7)` lines, appended to the caller's own. */
  readonly notes: ReadonlyArray<string>;
}

// ---------------------------------------------------------------------------
// _resolve_order
// ---------------------------------------------------------------------------

/** Resolve one order against the cache only; never touch the network. */
export const resolveOrder = (
  deps: OrdersDeps,
  state: V2ClientState,
  sessionPath: string,
  text: string
): Effect.Effect<ResolvedOrder, OrderUnresolved | PlayerError, SessionStore> =>
  Effect.gen(function* () {
    const tokens = pySplit(text);
    const first = tokens[0] ?? '';
    let pool = yield* orderPool(state, deps);
    let actorId = '';
    let verb = '';
    let defaults: Readonly<Record<string, JsonValue>> = {};
    let values: ReadonlyArray<string>;

    if (ACTION_ALIAS_RE.test(first)) {
      // A bare alias already names one exact capability.
      const actionId = yield* expandActionAlias(state, first, sessionPath);
      pool = pool.filter((item) => compactText(item, 'action_id') === actionId);
      if (pool.length === 0) {
        return yield* Effect.fail(
          orderUnresolved(`${first} names an action this seat no longer holds`)
        );
      }
      values = tokens.slice(1);
    } else {
      let rest = tokens.slice(1);
      if (ENTITY_ALIAS_RE.test(first) || ACTOR_ID_RE.test(first)) {
        actorId = yield* expandAlias(state, first, sessionPath);
        if (!ACTOR_ID_RE.test(actorId)) {
          return yield* Effect.fail(
            orderUnresolved(`${first} is not a unit, city, or player this seat can act as`)
          );
        }
        if (rest.length === 0) {
          return yield* Effect.fail(
            orderUnresolved(
              `${first} names an actor but no verb; write \`${first} <verb> [arguments]\``,
              actorId
            )
          );
        }
        verb = rest[0] as string;
        rest = rest.slice(1);
        pool = pool.filter((item) => orderActor(item) === actorId);
        if (pool.length === 0) {
          return yield* Effect.fail(
            orderUnresolved(`no cached action belongs to ${first}`, actorId)
          );
        }
      } else if (V2_ACTION_FAMILIES.has(casefold(first)) && rest.length > 0) {
        const family = casefold(first);
        verb = rest[0] as string;
        rest = rest.slice(1);
        pool = pool.filter((item) => kindHead(compactText(item, 'kind')) === family);
        if (pool.length === 0) {
          return yield* Effect.fail(orderUnresolved(`no cached ${family} action`));
        }
      } else {
        verb = first;
      }
      const selector = casefold(verb);
      // `selector` is a word the agent typed, so the lookup must be a dict
      // lookup and not a prototype walk: CPython's `.get("toString")` is a
      // miss, and the miss is what routes the word to the catalog's own verbs.
      const tier1 = Object.hasOwn(V2_TIER1_VERBS, selector)
        ? V2_TIER1_VERBS[selector]
        : undefined;
      if (tier1 !== undefined) {
        pool = pool.filter(
          (item) =>
            compactText(item, 'kind') === tier1.kind && orderOperation(item) === tier1.operation
        );
        if (pool.length === 0) {
          return yield* Effect.fail(
            orderUnresolved(
              `\`${verb}\` names ${tier1.kind}/${tier1.operation}, ` +
                "which this seat's cached catalog does not advertise " +
                (actorId !== '' ? `for ${first}` : 'here'),
              actorId
            )
          );
        }
        defaults = tier1.arguments;
      } else {
        pool = pool.filter((item) => orderVerbs(item).has(selector));
        if (pool.length === 0) {
          return yield* Effect.fail(
            orderUnresolved(`no cached action advertises the verb \`${verb}\``, actorId)
          );
        }
      }
      values = rest;
      // An action whose arguments are an ordered list reads its coordinates as
      // list elements, not as the one target that selects it among siblings, so
      // the first word is never eaten as a target key.
      const listed = poolIsListed(pool);
      const head = values[0];
      if (head !== undefined && !listed) {
        const keys = orderTargetKeys(head);
        if (keys.length > 0) {
          const narrowed: JsonObject[] = [];
          for (const item of pool) {
            const key = yield* actionTargetKey(field(item, 'target'));
            if (keys.includes(key)) narrowed.push(item);
          }
          if (narrowed.length === 0) {
            return yield* Effect.fail(
              orderUnresolved(`no cached \`${verb}\` action targets ${head}`, actorId)
            );
          }
          pool = narrowed;
          values = values.slice(1);
        }
      }
    }

    const element = orderElementResolver(
      state,
      actorId,
      verb !== '' ? verb : first,
      { strict: pool.length === 1 },
      deps
    );
    const matches: Array<readonly [JsonObject, JsonObject]> = [];
    for (const item of pool) {
      const args = yield* orderMatch(item, values, defaults, element);
      if (args !== null) matches.push([item, args] as const);
    }
    if (matches.length === 0) {
      return yield* Effect.fail(
        orderUnresolved(
          `no cached action takes those arguments; ` +
            `${pool.length} candidate(s) matched the verb`,
          actorId
        )
      );
    }
    if (matches.length > 1) {
      const aliases = yield* aliasMap(state);
      const named = matches
        .slice(0, 8)
        .map(([item]) => aliases[compactText(item, 'action_id')] ?? '')
        .filter((alias) => alias !== '')
        .join(' ');
      return yield* Effect.fail(
        orderUnresolved(
          `${matches.length} cached actions match; name exactly one by its ` +
            `alias${named !== '' ? `: ${named}` : ''}`,
          actorId
        )
      );
    }
    const [compact, args] = matches[0] as readonly [JsonObject, JsonObject];
    return yield* orderResolution(compact, text, args);
  });

// ---------------------------------------------------------------------------
// _order_outcomes
// ---------------------------------------------------------------------------

/** Resolve every order against the cache, keeping each one's verdict. */
export const orderOutcomes = (
  deps: OrdersDeps,
  state: V2ClientState,
  sessionPath: string,
  orders: ReadonlyArray<string>
): Effect.Effect<ReadonlyArray<OrderOutcome>, never, SessionStore> =>
  Effect.forEach(orders, (text) =>
    Effect.match(resolveOrder(deps, state, sessionPath, text), {
      onSuccess: (resolved): OrderOutcome => ({ text, resolved, reason: '', actor: '' }),
      onFailure: (error): OrderOutcome =>
        error._tag === 'OrderUnresolved'
          ? { text, resolved: null, reason: error.reason, actor: error.actorId }
          : { text, resolved: null, reason: error.message, actor: '' },
    })
  );

// ---------------------------------------------------------------------------
// _order_fetch_targets
// ---------------------------------------------------------------------------

/**
 * Name the actors an unresolved order asked for and this seat never read.
 *
 * Only a *never drained* actor qualifies.  An actor whose complete catalog is
 * already cached at this revision has been asked and answered: a verb it does
 * not offer is a real refusal, not a cache miss, and re-fetching it would turn
 * one bad word into a silent round trip every turn.
 */
export const orderFetchTargets = (
  state: V2ClientState,
  outcomes: ReadonlyArray<OrderOutcome>
): ReadonlyArray<string> => {
  const drained = new Set(
    state.drained_actors.filter((value): value is string => typeof value === 'string')
  );
  const wanted: string[] = [];
  for (const outcome of outcomes) {
    if (
      outcome.resolved !== null ||
      outcome.actor === '' ||
      drained.has(outcome.actor) ||
      wanted.includes(outcome.actor)
    ) {
      continue;
    }
    wanted.push(outcome.actor);
  }
  return wanted;
};

// ---------------------------------------------------------------------------
// _resolve_orders
// ---------------------------------------------------------------------------

/** Resolve every order up front; refuse the whole batch if any cannot be. */
export const resolveOrders = (
  deps: OrdersDeps,
  state: V2ClientState,
  sessionPath: string,
  orders: ReadonlyArray<string>
): Effect.Effect<ReadonlyArray<ResolvedOrder>, PlayerError, SessionStore | PrivateFs> =>
  Effect.gen(function* () {
    const outcomes = yield* orderOutcomes(deps, state, sessionPath, orders);
    if (outcomes.some((outcome) => outcome.resolved === null)) {
      const report = yield* unresolvedReport(sessionPath, state, outcomes);
      return yield* Effect.fail(playerError(report.join('\n')));
    }
    return outcomes.map((outcome) => outcome.resolved as ResolvedOrder);
  });

// ---------------------------------------------------------------------------
// _resolve_orders_fetching
// ---------------------------------------------------------------------------

/**
 * Resolve the batch, drawing each unread actor's catalog once first.
 *
 * This is what makes the printed loop — `turn`, then `do` — run as printed: a
 * briefing does not enumerate capabilities, so the first `do` of a turn names
 * actors whose catalog this seat has never read.  Every such actor is fetched
 * *before* anything is sent, and the batch is then re-resolved whole, so a
 * genuinely bad verb still refuses with the per-order table and no order
 * reaches the wire.  `--no-refresh` keeps the plain refusal.
 */
export const resolveOrdersFetching = (
  deps: OrdersFetchDeps,
  sessionPath: string,
  session: Session,
  state: V2ClientState,
  orders: ReadonlyArray<string>,
  notes: ReadonlyArray<string> = []
): Effect.Effect<
  OrderOutcomes,
  PlayerError | LockTimeoutError,
  SessionStore | PrivateFs
> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const outcomes = yield* orderOutcomes(deps, state, sessionPath, orders);
    const targets = orderFetchTargets(state, outcomes);
    if (targets.length === 0) {
      if (outcomes.some((outcome) => outcome.resolved === null)) {
        const report = yield* unresolvedReport(sessionPath, state, outcomes);
        return yield* Effect.fail(playerError(report.join('\n')));
      }
      return {
        resolved: outcomes.map((outcome) => outcome.resolved as ResolvedOrder),
        state,
        notes,
      };
    }
    const aliases = new Map<string, string>();
    for (const [alias, identifier] of Object.entries(state.entity_aliases)) {
      if (typeof identifier === 'string') aliases.set(identifier, alias);
    }
    let current = state;
    const grown = [...notes];
    for (const actorId of targets) {
      yield* deps.drainLegal(sessionPath, session, actorId);
      current = yield* store.readState(sessionPath, session);
      const revision = current.last_revision;
      grown.push(
        `fetched ${aliases.get(actorId) ?? actorId} options` +
          (revision !== null ? ` (rev${revision.revision})` : '')
      );
    }
    // The fetch already happened; a refusal that hid it would leave the agent
    // unable to tell a cold cache from a wrong word.
    const resolved = yield* Effect.mapError(
      resolveOrders(deps, current, sessionPath, orders),
      (error) => playerError([...grown, error.message].join('\n'))
    );
    return { resolved, state: current, notes: grown };
  });
