/**
 * Carrying orders across a revision bump, and the drains that feed them.
 *
 * Ports `play/client.py` 9174-9188 (`_rebind_order`), 9236-9263
 * (`_refresh_stale_order_aliases`), 9358-9386 (`_drain_legal_unlocked`) and
 * 9389-9399 (`_refresh_orders`).
 *
 * Two different things are re-bound here and they must not be confused:
 *
 * - **A stale `aN` before resolution.**  An alias the agent typed from a
 *   catalog it read one revision ago keeps its number when the action itself is
 *   unchanged; that decision is U03's `refreshStaleAlias`, and this file only
 *   decides *which* orders ask for it and collects the note it produces.
 * - **An already-resolved order after a receipt moved the game on.**  The
 *   handle it holds is now expired, so it is re-pointed at the newest
 *   revision's handle for the *same* action — same actor, kind, operation,
 *   target and label.  Anything less exact than one match is `null`, and `do`
 *   turns that into "rev9/t3 no longer offers …" rather than sending a guess.
 */
import { Effect, Either, Option } from 'effect';
import { ACTION_ALIAS_RE, V2_LEGAL_DRAIN_MAX_PAGES } from 'src/constants';
import { playerError, type LockTimeoutError, type PlayerError } from 'src/errors';
import { field, type JsonObject } from 'src/schema/primitives';
import type { LegalActionPageEnvelope } from 'src/schema/page';
import type { Revision } from 'src/schema/revision';
import { actionTargetKey } from 'src/services/aliases';
import { expandActionAlias } from 'src/services/alias-expand';
import { refreshStaleAlias, type RefreshError } from 'src/services/alias-refresh';
import { PrivateFs } from 'src/services/private-fs';
import {
  SessionStore,
  type Session,
  type V2ClientState,
} from 'src/services/session-store';
import { pySplit } from 'src/services/orders/parse';
import {
  compactText,
  orderActor,
  orderOperation,
  orderPool,
  orderResolution,
  type OrdersDeps,
  type ResolvedOrder,
} from 'src/services/orders/match';
import type { OrdersFetchDeps } from 'src/services/orders/resolve';

// ---------------------------------------------------------------------------
// _rebind_order
// ---------------------------------------------------------------------------

/**
 * Re-point one already-resolved order at the newest revision's handle.
 *
 * Identity is the five fields that survive a revision: the actor, the public
 * kind, the operation, the target by coordinate or name, and the label.  Two
 * candidates — or none — is `null`, because either would be this client
 * choosing the agent's move for it.
 */
export const rebindOrder = (
  deps: OrdersDeps,
  state: V2ClientState,
  resolved: ResolvedOrder
): Effect.Effect<ResolvedOrder | null, PlayerError> =>
  Effect.gen(function* () {
    const matches: JsonObject[] = [];
    for (const item of yield* orderPool(state, deps)) {
      if (orderActor(item) !== resolved.actor_id) continue;
      if (compactText(item, 'kind') !== resolved.kind) continue;
      if (orderOperation(item) !== resolved.operation) continue;
      if (compactText(item, 'label') !== resolved.label) continue;
      const targetKey = yield* actionTargetKey(field(item, 'target'));
      if (targetKey !== resolved.target_key) continue;
      matches.push(item);
    }
    const only = matches[0];
    if (matches.length !== 1 || only === undefined) return null;
    return yield* orderResolution(only, resolved.order, resolved.arguments);
  });

// ---------------------------------------------------------------------------
// _refresh_stale_order_aliases
// ---------------------------------------------------------------------------

/** The cache after any stale alias was re-bound, plus the lines to print. */
export interface RefreshedOrderAliases {
  readonly state: V2ClientState;
  readonly notes: ReadonlyArray<string>;
}

/**
 * Re-bind any stale action alias these orders name before resolving them.
 *
 * This is the same mechanism `do` already runs between its own orders, moved
 * one step earlier.  A `refreshStaleAlias` that yields `none` — the meaning
 * vanished, or the alias never carried one — leaves the plain refusal to stand,
 * which resolution will then produce with its own wording.
 */
export const refreshStaleOrderAliases = (
  deps: OrdersFetchDeps,
  sessionPath: string,
  session: Session,
  state: V2ClientState,
  orders: ReadonlyArray<string>,
  notes: ReadonlyArray<string> = []
): Effect.Effect<RefreshedOrderAliases, RefreshError, SessionStore | PrivateFs> =>
  Effect.gen(function* () {
    let current = state;
    const grown = [...notes];
    for (const text of orders) {
      const words = pySplit(text);
      const first = words[0];
      if (first === undefined || !ACTION_ALIAS_RE.test(first)) continue;
      const expanded = yield* Effect.either(expandActionAlias(current, first, sessionPath));
      if (Either.isRight(expanded)) continue;
      const rebound = yield* refreshStaleAlias(sessionPath, session, current, first, {
        locked: true,
        fetch: deps.drainLegal,
      });
      if (Option.isNone(rebound)) continue;
      current = rebound.value.state;
      grown.push(rebound.value.note);
    }
    return { state: current, notes: grown };
  });

// ---------------------------------------------------------------------------
// _drain_legal_unlocked
// ---------------------------------------------------------------------------

/**
 * `_read_legal_page` (client.py:7863-7896), U11's.
 *
 * The drain loop below is this unit's, but the single page read it repeats is
 * U11's — it validates the envelope, ingests it into `.v2-state` and mirrors
 * it, and it drops the pending catalog on a refusal.  It arrives injected, the
 * same way U03 takes its `LegalPageFetcher`; see NOTES.md.
 */
export type LegalPageReader = (
  sessionPath: string,
  session: Session,
  query: string,
  options: {
    readonly cursor: string;
    readonly actorId: string;
    readonly targetId: string;
  }
) => Effect.Effect<LegalActionPageEnvelope, PlayerError, SessionStore | PrivateFs>;

/** CPython `urllib.parse.urlencode({key: value})`. */
const urlEncode = (key: string, value: string): string =>
  new URLSearchParams([[key, value]]).toString();

/**
 * Enumerate one catalog into the local cache, holding no new lock.
 *
 * Callers already hold this seat's request lock — the advisory lock is not
 * reentrant — so this is the internal drain that the intent commands use to
 * obtain fresh capabilities without printing a catalog into agent context.
 */
export const drainLegalUnlocked = (
  read: LegalPageReader,
  sessionPath: string,
  session: Session,
  actorId = ''
): Effect.Effect<Revision | null, PlayerError, SessionStore | PrivateFs> =>
  Effect.gen(function* () {
    let query = actorId !== '' ? urlEncode('actor_id', actorId) : '';
    let cursor = '';
    const seen = new Set<string>();
    for (let page = 0; page < V2_LEGAL_DRAIN_MAX_PAGES; page += 1) {
      const value = yield* read(sessionPath, session, query, {
        cursor,
        actorId,
        targetId: '',
      });
      const revision: Revision | null = value.state_revision;
      cursor = value.page.next_cursor ?? '';
      if (cursor === '') return revision;
      if (seen.has(cursor)) {
        return yield* Effect.fail(playerError('the legal catalog repeated a cursor'));
      }
      seen.add(cursor);
      query = urlEncode('cursor', cursor);
    }
    return yield* Effect.fail(
      playerError('the legal catalog exceeded the safe 512-page drain limit')
    );
  });

// ---------------------------------------------------------------------------
// _refresh_orders
// ---------------------------------------------------------------------------

/**
 * Re-enumerate exactly the catalogs the not-yet-sent orders name.
 *
 * One drain per distinct actor, in first-seen order.  Never a blanket
 * re-enumeration: the orders already sent are recorded, and re-reading a
 * catalog nothing pending names would spend a round trip on nothing.
 */
export const refreshOrders = (
  deps: OrdersFetchDeps,
  sessionPath: string,
  session: Session,
  pending: ReadonlyArray<ResolvedOrder>
): Effect.Effect<void, PlayerError | LockTimeoutError, SessionStore | PrivateFs> =>
  Effect.gen(function* () {
    const drained = new Set<string>();
    for (const resolved of pending) {
      if (drained.has(resolved.actor_id)) continue;
      drained.add(resolved.actor_id);
      yield* deps.drainLegal(sessionPath, session, resolved.actor_id);
    }
  });
