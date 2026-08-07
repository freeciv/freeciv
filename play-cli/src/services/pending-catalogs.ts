/**
 * Pending scoped legal-action catalogs.
 *
 * Ports `_cursor_expired` (client.py:2411-2416), `_drop_pending_for_cursor`
 * (2418-2436), `_drop_pending_for_scope` (2438-2464) and
 * `_validate_pending_catalogs` (1559-1622).
 *
 * A scoped `legal --actor_id … --all` drain arrives one page at a time.  Every
 * page before the terminal one is *staged* here rather than promoted into
 * `actions`, because a staged descriptor is not executable yet and numbering it
 * would advertise an `aN` that `persistBatchForAction` must refuse.  The whole
 * accumulation is promoted in one step when the final page says the catalog is
 * complete — see `rememberPage` in `src/services/aliases.ts`.
 */
import { Effect } from 'effect';
import {
  ACTOR_ID_RE,
  CATALOG_RE,
  CURSOR_RE,
  RELATION_ID_RE,
  TILE_ID_RE,
  V2_MAX_PENDING_CATALOGS,
} from 'src/constants';
import { playerError, type LockTimeoutError, type PlayerError } from 'src/errors';
import { decodeDescriptor } from 'src/schema/descriptor';
import { cursorExpiry, type PageScope } from 'src/schema/page';
import {
  field,
  isJsonObject,
  isWholeNumber,
  type JsonObject,
  type JsonValue,
} from 'src/schema/primitives';
import { decodeRevision, type Revision } from 'src/schema/revision';
import { isOpaqueId } from 'src/schema/ids';
import { PrivateFs } from 'src/services/private-fs';
import {
  SessionStore,
  type Session,
  type V2ClientState,
} from 'src/services/session-store';

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

/** One staged catalog: everything read so far plus where to resume. */
export interface PendingCatalog {
  readonly state_revision: Revision;
  readonly scope: PageScope;
  readonly total_items: number;
  /** `action_id` → the descriptor exactly as it was validated off the wire. */
  readonly items: Readonly<Record<string, JsonObject>>;
  readonly next_cursor: string;
  readonly cursor_expires_at: string | null;
}

export const PENDING_CATALOG_FIELDS: ReadonlySet<string> = new Set([
  'state_revision',
  'scope',
  'total_items',
  'items',
  'next_cursor',
  'cursor_expires_at',
]);

const INVALID = 'private v2 pending catalogs are invalid';

// ---------------------------------------------------------------------------
// _cursor_expired
// ---------------------------------------------------------------------------

/**
 * Report whether a staged catalog's continuation cursor has already retired.
 *
 * `null` means "the server named no expiry", which is never expired.  CPython
 * rewrote the trailing `Z` to `+00:00` before parsing; `Date.parse` accepts the
 * `Z` spelling directly and `_validate_cursor_expiry` already proved the string
 * is one of those spellings.
 */
export const cursorExpired = (expiresAt: string | null, now = Date.now()): boolean => {
  if (expiresAt === null) return false;
  const parsed = Date.parse(expiresAt);
  return Number.isNaN(parsed) ? false : parsed <= now;
};

// ---------------------------------------------------------------------------
// _validate_pending_catalogs
// ---------------------------------------------------------------------------

const sameKeys = (value: JsonObject, expected: ReadonlySet<string>): boolean => {
  const present = Object.keys(value);
  return present.length === expected.size && present.every((key) => expected.has(key));
};

/** The scope half of `_validate_pending_catalogs`, kept verbatim. */
const scopeIsValid = (scope: JsonValue): boolean => {
  if (!isJsonObject(scope)) return false;
  const actorId = field(scope, 'actor_id');
  const actorType = field(scope, 'actor_type');
  if (sameKeys(scope, new Set(['actor_id', 'actor_type']))) {
    return (
      typeof actorId === 'string' &&
      ACTOR_ID_RE.test(actorId) &&
      (actorType === 'player' || actorType === 'city' || actorType === 'unit')
    );
  }
  if (sameKeys(scope, new Set(['actor_id', 'actor_type', 'target_id', 'target_type']))) {
    const targetId = field(scope, 'target_id');
    const targetType = field(scope, 'target_type');
    if (typeof actorId !== 'string' || !ACTOR_ID_RE.test(actorId)) return false;
    if (actorType !== 'player' && actorType !== 'unit' && actorType !== 'city') return false;
    if (typeof targetId !== 'string') return false;
    // CPython's `and`/`or` precedence: a relation target is player-only, and a
    // tile target is open to every actor type.
    return (
      (actorType === 'player' &&
        RELATION_ID_RE.test(targetId) &&
        targetType === 'diplomatic_relation') ||
      (TILE_ID_RE.test(targetId) && targetType === 'tile')
    );
  }
  return false;
};

/**
 * Prove the staged-catalog record is closed before anything resolves against
 * it.  A drifted record could promote descriptors that were never proved to
 * belong to one complete catalog, so it is refused rather than repaired.
 */
export const validatePendingCatalogs = (
  value: JsonValue
): Effect.Effect<void, PlayerError> =>
  Effect.gen(function* () {
    const fail = Effect.fail(playerError(INVALID));
    if (!isJsonObject(value) || Object.keys(value).length > V2_MAX_PENDING_CATALOGS) {
      return yield* fail;
    }
    for (const [catalogId, pending] of Object.entries(value)) {
      if (
        !CATALOG_RE.test(catalogId) ||
        !isJsonObject(pending) ||
        !sameKeys(pending, PENDING_CATALOG_FIELDS)
      ) {
        return yield* fail;
      }
      // `_validate_revision` is called UNWRAPPED at client.py:1573, so its own
      // sentence reaches the user instead of the generic pending one.  Same for
      // `_validate_cursor_expiry` (1615) and `_validate_descriptor` (1620)
      // below: naming the drifted field is the whole diagnosis.
      const revision = yield* Effect.mapError(
        decodeRevision(field(pending, 'state_revision')),
        (drift) => playerError(drift.message)
      );
      const scope = field(pending, 'scope');
      const total = field(pending, 'total_items');
      const items = field(pending, 'items');
      const cursor = field(pending, 'next_cursor');
      const expiry = field(pending, 'cursor_expires_at');
      if (
        !scopeIsValid(scope) ||
        !isWholeNumber(total) ||
        total < 1 ||
        total > 8192 ||
        !isJsonObject(items) ||
        Object.keys(items).length === 0 ||
        Object.keys(items).length >= total ||
        typeof cursor !== 'string' ||
        !CURSOR_RE.test(cursor) ||
        (expiry !== null && typeof expiry !== 'string')
      ) {
        return yield* fail;
      }
      if (expiry !== null) {
        yield* Effect.mapError(cursorExpiry(expiry), (drift) => playerError(drift.message));
      }
      for (const [actionId, descriptor] of Object.entries(items)) {
        if (!isOpaqueId(actionId)) return yield* fail;
        const decoded = yield* Effect.mapError(
          decodeDescriptor(descriptor, revision),
          (drift) => playerError(drift.message)
        );
        if (decoded.action_id !== actionId) return yield* fail;
      }
    }
  });

// ---------------------------------------------------------------------------
// _drop_pending_for_cursor / _drop_pending_for_scope
// ---------------------------------------------------------------------------

const pendingField = (pending: JsonValue, key: string): JsonValue =>
  isJsonObject(pending) ? field(pending, key) : null;

const rewrite = (
  sessionPath: string,
  session: Session,
  keep: (pending: JsonValue) => boolean
): Effect.Effect<V2ClientState, PlayerError | LockTimeoutError, SessionStore | PrivateFs> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const files = yield* PrivateFs;
    return yield* store.withStateLock(sessionPath, session, (current) =>
      Effect.gen(function* () {
        const surviving: Record<string, JsonValue> = {};
        let removed = false;
        for (const [catalogId, pending] of Object.entries(current.pending_catalogs)) {
          if (keep(pending)) surviving[catalogId] = pending;
          else removed = true;
        }
        if (!removed) return current;
        const next: V2ClientState = { ...current, pending_catalogs: surviving };
        yield* files.writeJson(store.statePath(sessionPath), next);
        return next;
      })
    );
  });

/**
 * Forget every catalog staged behind one continuation cursor.
 *
 * The server refusing a cursor is the one proof the staged prefix can never be
 * completed, so it is dropped rather than left to expire.
 */
export const dropPendingForCursor = (
  sessionPath: string,
  session: Session,
  cursor: string
): Effect.Effect<V2ClientState, PlayerError | LockTimeoutError, SessionStore | PrivateFs> =>
  rewrite(sessionPath, session, (pending) => pendingField(pending, 'next_cursor') !== cursor);

/** Forget every catalog staged for one actor (optionally one target of it). */
export const dropPendingForScope = (
  sessionPath: string,
  session: Session,
  actorId: string,
  targetId: string
): Effect.Effect<V2ClientState, PlayerError | LockTimeoutError, SessionStore | PrivateFs> =>
  rewrite(sessionPath, session, (pending) => {
    const scope = pendingField(pending, 'scope');
    const scopeActor = isJsonObject(scope) ? field(scope, 'actor_id') : null;
    const scopeTarget = isJsonObject(scope) ? field(scope, 'target_id') : null;
    const matches = scopeActor === actorId && (targetId === '' || scopeTarget === targetId);
    return !matches;
  });
