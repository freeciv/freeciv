/**
 * The `.v2-state` alias tables and the page/receipt ingest that fills them.
 *
 * Ports `play/client.py` 1085-1113 (`_empty_action_aliases`,
 * `_empty_v2_client_state`), 1639-1713 (`_validate_alias_state`,
 * `_validate_drained_actors`), 2465-2941 (`_entity_alias_prefix`,
 * `_action_semantics`, `_alias_entries`, `_assign_action_aliases`,
 * `_rebind_action_aliases`, `_assign_entity_aliases`, `_assign_tile_aliases`,
 * `_tile_reference`, `_learn_state_aliases`, `_learn_descriptor_aliases`,
 * `_remember_drained_actor`, `_remember_page_unlocked`, `_remember_page`,
 * `_remember_receipt`) and 3117-3138 (`_fresh_action_aliases`, `_alias_map`).
 *
 * Two lifetimes live in one file because one ingest maintains both:
 *
 * - **`a1..aN` action aliases are revision-scoped.**  The bucket carries the
 *   revision it was built from, and resolution refuses every alias whose
 *   recorded revision is not the newest this client knows, so an alias can
 *   never outlive the capability it names.
 * - **`u1/c1/p1/r1` entity aliases and `T(x,y)` tile aliases are game-stable.**
 *   Entity IDs survive revisions, so they are numbered once in first-seen order
 *   and never re-point at a different entity.
 *
 * Both are capped (`V2_MAX_*`), and a table that cannot be proved closed is
 * refused rather than repaired: an alias that cannot be proved to name exactly
 * one opaque ID must not be able to expand into a request.
 */
import { Effect, Layer } from 'effect';
import {
  ACTION_ALIAS_RE,
  ACTOR_ID_RE,
  ALIAS_ENTITY_KEYS,
  ALIAS_ENTITY_PREFIXES,
  ALIAS_ENTITY_TYPES,
  ENTITY_ALIAS_RE,
  RELATION_ID_RE,
  TILE_ID_RE,
  TILE_KEY_RE,
  V2_MAX_ACTION_ALIASES,
  V2_MAX_ALIAS_SEMANTICS,
  V2_MAX_DRAINED_ACTORS,
  V2_MAX_ENTITY_ALIASES,
  V2_MAX_TILE_ALIASES,
} from 'src/constants';
import { playerError, type LockTimeoutError, type PlayerError } from 'src/errors';
import { coordinates, named } from 'src/render/primitives';
import { decodeDescriptor, type LegalActionDescriptor } from 'src/schema/descriptor';
import { entityAliasIdMatches, isActorId, isOpaqueId } from 'src/schema/ids';
import { scopesEqual } from 'src/schema/legal-page';
import type {
  LegalActionPageEnvelope,
  PageEnvelope,
  PageScope,
} from 'src/schema/page';
import {
  field,
  hasField,
  isJsonObject,
  isWholeNumber,
  type JsonObject,
  type JsonValue,
} from 'src/schema/primitives';
import { decodeRevision, revisionsEqual, type Revision } from 'src/schema/revision';
import type { CommandReceipt } from 'src/schema/receipt';
import { codeLength, codeSlice } from 'src/services/mirror';
import { PrivateFs } from 'src/services/private-fs';
import {
  cursorExpired,
  validatePendingCatalogs,
  PENDING_CATALOG_FIELDS,
} from 'src/services/pending-catalogs';
import {
  SessionStore,
  V2StateSchema,
  emptyV2ClientState,
  type Session,
  type V2ClientState,
  type V2StateSchemaApi,
} from 'src/services/session-store';

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

/** One numbered action: the handle, whose actor it belongs to, what it means. */
export interface ActionAliasEntry {
  readonly action_id: string;
  readonly actor_id: string;
  /** `_action_semantics` — identity with nothing revision-bound inside it. */
  readonly semantics: string;
}

/** The revision-scoped `a1..aN` bucket. */
export interface ActionAliasTable {
  readonly state_revision: Revision | null;
  readonly by_alias: Readonly<Record<string, ActionAliasEntry>>;
}

/** `opaque id → short alias`, the map every renderer prints rows through. */
export type AliasMap = Readonly<Record<string, string>>;

/** One `(tile_id, x, y)` triple read out of an already-validated item. */
export interface TileReference {
  readonly identifier: string;
  readonly x: number;
  readonly y: number;
}

/** What `rememberPage` learned, plus the catalog it promoted (if any). */
export interface RememberedPage {
  readonly state: V2ClientState;
  /**
   * Every descriptor of a catalog this response promoted, or `null`.
   *
   * A scoped catalog only earns its `aN` names when its final page promotes
   * the whole accumulation, so the pages ingested before that must not be the
   * last word the mirror hears about those rows.
   */
  readonly promoted: ReadonlyArray<LegalActionDescriptor> | null;
}

/** `rememberPage`'s input, discriminated so `items` is typed on both arms. */
export type RememberPageInput =
  | { readonly legal: false; readonly page: PageEnvelope }
  | { readonly legal: true; readonly page: LegalActionPageEnvelope };

const ACTION_ALIASES_INVALID = 'private v2 action aliases are invalid';
const PENDING_INVALID = 'private v2 pending catalogs are invalid';
const ENTITY_ALIASES_INVALID = 'private v2 entity aliases are invalid';
const TILE_ALIASES_INVALID = 'private v2 tile aliases are invalid';
const DRAINED_INVALID = 'private v2 drained catalogs are invalid';

// ---------------------------------------------------------------------------
// JSON plumbing
// ---------------------------------------------------------------------------

/** Deep, key-order-insensitive equality — CPython's `dict == dict`. */
export const jsonEquals = (left: JsonValue, right: JsonValue): boolean => {
  if (left === right) return true;
  if (Array.isArray(left) || Array.isArray(right)) {
    if (!Array.isArray(left) || !Array.isArray(right)) return false;
    if (left.length !== right.length) return false;
    return left.every((item, index) => jsonEquals(item, right[index] ?? null));
  }
  if (isJsonObject(left) && isJsonObject(right)) {
    const keys = Object.keys(left);
    if (keys.length !== Object.keys(right).length) return false;
    return keys.every(
      (key) => hasField(right, key) && jsonEquals(field(left, key), field(right, key))
    );
  }
  return false;
};

const revisionJson = (revision: Revision): JsonObject => ({
  turn: revision.turn,
  revision: revision.revision,
  state_token: revision.state_token,
});

/** The exact key order `_validate_descriptor` returned. */
const descriptorJson = (descriptor: LegalActionDescriptor): JsonObject => ({
  action_id: descriptor.action_id,
  kind: descriptor.kind,
  label: descriptor.label,
  subject: descriptor.subject,
  arguments_schema: descriptor.arguments_schema,
  state_revision: revisionJson(descriptor.state_revision),
});

const scopeJson = (scope: PageScope): JsonObject => {
  const base: Record<string, JsonValue> = {
    actor_id: scope.actor_id,
    actor_type: scope.actor_type,
  };
  if (scope.target_id !== undefined) base['target_id'] = scope.target_id;
  if (scope.target_type !== undefined) base['target_type'] = scope.target_type;
  return base;
};

const aliasEntryJson = (entry: ActionAliasEntry): JsonObject => ({
  action_id: entry.action_id,
  actor_id: entry.actor_id,
  semantics: entry.semantics,
});

/**
 * Narrow an already-decoded wire object to plain JSON, cast-free.
 *
 * Every decoder in `src/schema/` builds its result out of JSON the wire
 * carried, so this walk only ever meets JSON — but it *proves* that at runtime
 * instead of asserting it, which is what keeps the private cache honest.
 */
const toJsonValue = (value: unknown): JsonValue => {
  if (value === null || value === undefined) return null;
  if (typeof value === 'string' || typeof value === 'boolean') return value;
  if (typeof value === 'number') return Number.isFinite(value) ? value : 0;
  if (Array.isArray(value)) return value.map((item) => toJsonValue(item));
  if (typeof value === 'object') {
    const copied: Record<string, JsonValue> = {};
    for (const [key, item] of Object.entries(value)) {
      if (item !== undefined) copied[key] = toJsonValue(item);
    }
    return copied;
  }
  return null;
};

// ---------------------------------------------------------------------------
// The working draft
// ---------------------------------------------------------------------------

interface PendingDraft {
  readonly state_revision: Revision;
  readonly scope: PageScope;
  readonly total_items: number;
  /** Still raw JSON: a staged descriptor is only decoded once its revision is
   *  proved to be the one the incoming page carries. */
  readonly items: Readonly<Record<string, JsonObject>>;
  readonly next_cursor: string;
  readonly cursor_expires_at: string | null;
}

interface Draft {
  schema_version: number;
  game_id: string;
  agent_id: string;
  last_revision: Revision | null;
  actions: Record<string, JsonValue>;
  pending_catalogs: Record<string, JsonValue>;
  batches: Record<string, JsonValue>;
  receipts: Record<string, JsonValue>;
  action_aliases: { state_revision: Revision | null; by_alias: Record<string, ActionAliasEntry> };
  entity_aliases: Record<string, string>;
  tile_aliases: Record<string, string>;
  drained_actors: string[];
}

// ---------------------------------------------------------------------------
// _validate_alias_state / _validate_drained_actors
// ---------------------------------------------------------------------------

const sameKeys = (value: JsonObject, expected: ReadonlySet<string>): boolean => {
  const present = Object.keys(value);
  return present.length === expected.size && present.every((key) => expected.has(key));
};

/**
 * Prove the private alias tables are closed, and hand back the typed bucket.
 *
 * A drifted table is never repaired in place: an alias that cannot be proved to
 * name exactly one opaque ID must not be able to expand into a request.
 */
export const parseActionAliases = (
  value: JsonValue
): Effect.Effect<{ state_revision: Revision | null; by_alias: Record<string, ActionAliasEntry> }, PlayerError> =>
  Effect.gen(function* () {
    const fail = Effect.fail(playerError(ACTION_ALIASES_INVALID));
    if (!isJsonObject(value) || !sameKeys(value, new Set(['state_revision', 'by_alias']))) {
      return yield* fail;
    }
    const rawTable = field(value, 'by_alias');
    const rawRevision = field(value, 'state_revision');
    if (
      !isJsonObject(rawTable) ||
      Object.keys(rawTable).length > V2_MAX_ACTION_ALIASES ||
      (rawRevision === null && Object.keys(rawTable).length > 0)
    ) {
      return yield* fail;
    }
    // CPython calls `_validate_revision` UNWRAPPED here (client.py:1655), so the
    // revision's own sentence — `invalid state revision: missing state_token.
    // Expected exactly revision, state_token, turn` — is what the user reads,
    // not the generic `private v2 action aliases are invalid`.  Naming the drift
    // is the whole diagnosis; the port carries the message across verbatim.
    const revision =
      rawRevision === null
        ? null
        : yield* Effect.mapError(decodeRevision(rawRevision), (drift) =>
            playerError(drift.message)
          );
    const byAlias: Record<string, ActionAliasEntry> = {};
    const actionIds = new Set<string>();
    for (const [alias, entry] of Object.entries(rawTable)) {
      if (!ACTION_ALIAS_RE.test(alias) || !isJsonObject(entry)) return yield* fail;
      if (!sameKeys(entry, new Set(['action_id', 'actor_id', 'semantics']))) return yield* fail;
      const actionId = field(entry, 'action_id');
      const actorId = field(entry, 'actor_id');
      const semantics = field(entry, 'semantics');
      if (
        !isOpaqueId(actionId) ||
        typeof actorId !== 'string' ||
        typeof semantics !== 'string' ||
        // CPython's `len()` counts code points, and so must this cap — the
        // producer below truncates to 1024 *code points*, so measuring the
        // loader's side in UTF-16 units would make this client refuse a
        // `.v2-state` it just wrote itself the moment one target name carries
        // a non-BMP character (NOTES §12.1).
        codeLength(semantics) > V2_MAX_ALIAS_SEMANTICS ||
        (actorId !== '' && !ACTOR_ID_RE.test(actorId)) ||
        actionIds.has(actionId)
      ) {
        return yield* fail;
      }
      actionIds.add(actionId);
      byAlias[alias] = { action_id: actionId, actor_id: actorId, semantics };
    }
    return { state_revision: revision, by_alias: byAlias };
  });

export const parseEntityAliases = (
  value: JsonValue
): Effect.Effect<Record<string, string>, PlayerError> => {
  const fail = Effect.fail(playerError(ENTITY_ALIASES_INVALID));
  if (!isJsonObject(value) || Object.keys(value).length > V2_MAX_ENTITY_ALIASES) return fail;
  const entries = Object.entries(value);
  const identifiers = new Set<JsonValue>(entries.map(([, identifier]) => identifier));
  if (identifiers.size !== entries.length) return fail;
  const table: Record<string, string> = {};
  for (const [alias, identifier] of entries) {
    if (!entityAliasIdMatches(alias, identifier) || typeof identifier !== 'string') return fail;
    table[alias] = identifier;
  }
  return Effect.succeed(table);
};

export const parseTileAliases = (
  value: JsonValue
): Effect.Effect<Record<string, string>, PlayerError> => {
  const fail = Effect.fail(playerError(TILE_ALIASES_INVALID));
  if (!isJsonObject(value) || Object.keys(value).length > V2_MAX_TILE_ALIASES) return fail;
  const entries = Object.entries(value);
  const identifiers = new Set<JsonValue>(entries.map(([, identifier]) => identifier));
  if (identifiers.size !== entries.length) return fail;
  const table: Record<string, string> = {};
  for (const [key, identifier] of entries) {
    if (!TILE_KEY_RE.test(key) || typeof identifier !== 'string' || !TILE_ID_RE.test(identifier)) {
      return fail;
    }
    table[key] = identifier;
  }
  return Effect.succeed(table);
};

/**
 * Prove the drained-catalog record is a closed list of actor IDs.
 *
 * The record only ever says "this actor's complete catalog is in `actions` at
 * `last_revision`".  A drifted record could make the renderer claim two
 * catalogs are equivalent when one of them was never fully read.
 */
export const parseDrainedActors = (
  value: ReadonlyArray<JsonValue> | JsonValue
): Effect.Effect<string[], PlayerError> => {
  const fail = Effect.fail(playerError(DRAINED_INVALID));
  if (!Array.isArray(value) || value.length > V2_MAX_DRAINED_ACTORS) return fail;
  const seen = new Set<JsonValue>(value);
  if (seen.size !== value.length) return fail;
  const actors: string[] = [];
  for (const actorId of value) {
    if (!isActorId(actorId)) return fail;
    actors.push(actorId);
  }
  return Effect.succeed(actors);
};

/** `_validate_alias_state`, in the order CPython ran its three halves. */
export const validateAliasState = (state: V2ClientState): Effect.Effect<void, PlayerError> =>
  Effect.gen(function* () {
    yield* parseActionAliases(state.action_aliases);
    yield* parseEntityAliases(state.entity_aliases);
    yield* parseTileAliases(state.tile_aliases);
  });

// ---------------------------------------------------------------------------
// The U03 seam: `V2StateSchema`
// ---------------------------------------------------------------------------

export const v2StateSchema: V2StateSchemaApi = {
  empty: emptyV2ClientState,
  validate: (state) =>
    Effect.gen(function* () {
      yield* validateAliasState(state);
      yield* parseDrainedActors(state.drained_actors);
      yield* validatePendingCatalogs(state.pending_catalogs);
    }),
  cursorExpired: (expiresAt) => cursorExpired(expiresAt),
};

/** The real `.v2-state` schema; `cli-main` swaps this for `V2StateSchemaDefault`. */
export const V2StateSchemaLive: Layer.Layer<V2StateSchema> = Layer.succeed(
  V2StateSchema,
  v2StateSchema
);

// ---------------------------------------------------------------------------
// Draft round-trip
// ---------------------------------------------------------------------------

const readPending = (value: JsonValue): Effect.Effect<PendingDraft | null, PlayerError> =>
  Effect.gen(function* () {
    if (!isJsonObject(value) || !sameKeys(value, PENDING_CATALOG_FIELDS)) return null;
    const revision = yield* Effect.mapError(
      decodeRevision(field(value, 'state_revision')),
      () => playerError(PENDING_INVALID)
    );
    const scope = field(value, 'scope');
    const total = field(value, 'total_items');
    const items = field(value, 'items');
    const cursor = field(value, 'next_cursor');
    const expiry = field(value, 'cursor_expires_at');
    if (
      !isJsonObject(scope) ||
      !isWholeNumber(total) ||
      !isJsonObject(items) ||
      typeof cursor !== 'string'
    ) {
      return null;
    }
    const actorId = field(scope, 'actor_id');
    const actorType = field(scope, 'actor_type');
    if (typeof actorId !== 'string') return null;
    if (actorType !== 'player' && actorType !== 'city' && actorType !== 'unit') return null;
    const targetId = field(scope, 'target_id');
    const targetType = field(scope, 'target_type');
    const staged: Record<string, JsonObject> = {};
    for (const [actionId, item] of Object.entries(items)) {
      if (!isJsonObject(item)) return null;
      staged[actionId] = item;
    }
    return {
      state_revision: revision,
      scope: {
        actor_id: actorId,
        actor_type: actorType,
        ...(typeof targetId === 'string' ? { target_id: targetId } : {}),
        ...(typeof targetType === 'string' ? { target_type: targetType } : {}),
      },
      total_items: total,
      items: staged,
      next_cursor: cursor,
      cursor_expires_at: typeof expiry === 'string' ? expiry : null,
    };
  });

interface PendingWrite {
  readonly state_revision: Revision;
  readonly scope: PageScope;
  readonly total_items: number;
  readonly items: ReadonlyMap<string, LegalActionDescriptor>;
  readonly next_cursor: string;
  readonly cursor_expires_at: string | null;
}

const pendingJson = (pending: PendingWrite): JsonObject => ({
  state_revision: revisionJson(pending.state_revision),
  scope: scopeJson(pending.scope),
  total_items: pending.total_items,
  items: Object.fromEntries(
    [...pending.items].map(([actionId, descriptor]) => [actionId, descriptorJson(descriptor)])
  ),
  next_cursor: pending.next_cursor,
  cursor_expires_at: pending.cursor_expires_at,
});

const toDraft = (state: V2ClientState): Effect.Effect<Draft, PlayerError> =>
  Effect.gen(function* () {
    const actionAliases = yield* parseActionAliases(state.action_aliases);
    const entityAliases = yield* parseEntityAliases(state.entity_aliases);
    const tileAliases = yield* parseTileAliases(state.tile_aliases);
    const drained = yield* parseDrainedActors(state.drained_actors);
    return {
      schema_version: state.schema_version,
      game_id: state.game_id,
      agent_id: state.agent_id,
      last_revision: state.last_revision,
      actions: { ...state.actions },
      pending_catalogs: { ...state.pending_catalogs },
      batches: { ...state.batches },
      receipts: { ...state.receipts },
      action_aliases: {
        state_revision: actionAliases.state_revision,
        by_alias: { ...actionAliases.by_alias },
      },
      entity_aliases: entityAliases,
      tile_aliases: tileAliases,
      drained_actors: drained,
    };
  });

const fromDraft = (draft: Draft): V2ClientState => ({
  schema_version: draft.schema_version,
  game_id: draft.game_id,
  agent_id: draft.agent_id,
  last_revision: draft.last_revision,
  actions: { ...draft.actions },
  pending_catalogs: { ...draft.pending_catalogs },
  batches: { ...draft.batches },
  receipts: { ...draft.receipts },
  action_aliases: {
    state_revision:
      draft.action_aliases.state_revision === null
        ? null
        : revisionJson(draft.action_aliases.state_revision),
    by_alias: Object.fromEntries(
      Object.entries(draft.action_aliases.by_alias).map(([alias, entry]) => [
        alias,
        aliasEntryJson(entry),
      ])
    ),
  },
  entity_aliases: { ...draft.entity_aliases },
  tile_aliases: { ...draft.tile_aliases },
  drained_actors: [...draft.drained_actors],
});

// ---------------------------------------------------------------------------
// _tile_reference / _action_target_key / _action_semantics
// ---------------------------------------------------------------------------

/** Read one `(tile_id, x, y)` triple out of an already-validated item. */
export const tileReference = (value: JsonValue, identifierKey: string): TileReference | null => {
  if (!isJsonObject(value)) return null;
  const identifier = field(value, identifierKey);
  const x = field(value, 'x');
  const y = field(value, 'y');
  if (typeof identifier !== 'string' || !TILE_ID_RE.test(identifier)) return null;
  if (!isWholeNumber(x) || !isWholeNumber(y)) return null;
  return { identifier, x, y };
};

/**
 * Name one action's target by coordinate or name, never by its hash.
 *
 * Ports `_action_target_key` (client.py:3970-3981).  PORT_MAP put that helper
 * on U11's row, but `_action_semantics` — the identity an `aN` is re-bound by —
 * needs it, so it lands here and U11 imports it (see NOTES.md).
 */
export const actionTargetKey = (target: JsonValue): Effect.Effect<string, PlayerError> =>
  Effect.gen(function* () {
    const tile = tileReference(target, 'id');
    if (tile !== null) return `T(${tile.x},${tile.y})`;
    const coordinate = yield* coordinates(target);
    if (coordinate !== null) return coordinate;
    if (target === null) return '';
    return named(target);
  });

/**
 * CPython slices strings by code point, not by UTF-16 code unit.
 *
 * Shares `codeSlice` with the loader's `codeLength` check above so the producer
 * and the validator can never disagree about how long 1024 is.  The `.length`
 * fast path is sound in one direction only: a string's UTF-16 length is never
 * below its code point count, so `<= limit` here proves `codeLength <= limit`.
 */
const truncateCodePoints = (value: string, limit: number): string =>
  value.length <= limit ? value : codeSlice(value, 0, limit);

/**
 * Name what an action *is*, with nothing revision-bound inside it.
 *
 * Identity is the actor, the public kind, the operation, the target by
 * coordinate or name, and the *shape* of the argument schema — never the HMAC
 * handle, never a hash, never an enum's current members.  Two enumerations of
 * the same board position therefore produce the same string, which is what lets
 * `a3` keep meaning "that same move" across a revision bump while the wire
 * still carries the fresh handle.
 *
 * CPython read these five fields off `_compact_legal_action(descriptor)`.  Every
 * one of them is copied through that projection unchanged — `subject.actor`,
 * `kind`, `subject.operation`, `subject.target` and `arguments_schema` are
 * never withheld and never rewritten — so the port reads the descriptor
 * directly and stays in wave 1.
 */
export const actionSemantics = (
  descriptor: LegalActionDescriptor
): Effect.Effect<string, PlayerError> =>
  Effect.gen(function* () {
    const subject = descriptor.subject;
    const actor = field(subject, 'actor');
    const actorId = isJsonObject(actor) ? field(actor, 'id') : null;
    const operation = field(subject, 'operation');
    const targetKey = yield* actionTargetKey(field(subject, 'target'));
    const schema = descriptor.arguments_schema;
    const rawProperties = field(schema, 'properties');
    const properties =
      isJsonObject(rawProperties) && Object.keys(rawProperties).length > 0 ? rawProperties : null;
    const declared = field(schema, 'required');
    const required =
      properties === null || !Array.isArray(declared)
        ? []
        : declared.filter(
            (name): name is string => typeof name === 'string' && hasField(properties, name)
          );
    const sortText = (values: ReadonlyArray<string>): string =>
      [...values].sort((left, right) => (left < right ? -1 : left > right ? 1 : 0)).join(',');
    const joined = [
      typeof actorId === 'string' ? actorId : '',
      descriptor.kind,
      typeof operation === 'string' ? operation : '',
      targetKey,
      properties === null ? '' : sortText(Object.keys(properties)),
      sortText(required),
    ].join('\n');
    return truncateCodePoints(joined, V2_MAX_ALIAS_SEMANTICS);
  });

/** `_alias_entries` — one `(action_id, actor_id, semantics)` per descriptor. */
export const aliasEntries = (
  descriptors: ReadonlyArray<LegalActionDescriptor>,
  actorId: string
): Effect.Effect<ReadonlyArray<ActionAliasEntry>, PlayerError> =>
  Effect.forEach(descriptors, (descriptor) =>
    Effect.map(actionSemantics(descriptor), (semantics) => ({
      action_id: descriptor.action_id,
      actor_id: actorId,
      semantics,
    }))
  );

// ---------------------------------------------------------------------------
// Table mutators
// ---------------------------------------------------------------------------

/**
 * Number newly enumerated actions a1..aN inside their own revision.
 *
 * The bucket carries the revision it was built from.  Resolution refuses every
 * alias whose recorded revision is not the newest revision this client knows,
 * so an alias can never outlive the capability it names.
 */
const assignActionAliases = (
  draft: Draft,
  revision: Revision,
  entries: ReadonlyArray<ActionAliasEntry>
): void => {
  const current = draft.action_aliases;
  if (current.state_revision === null || !revisionsEqual(current.state_revision, revision)) {
    draft.action_aliases = { state_revision: revision, by_alias: {} };
  }
  const table = draft.action_aliases;
  const known = new Set(Object.values(table.by_alias).map((entry) => entry.action_id));
  let number =
    Object.keys(table.by_alias).reduce(
      (highest, alias) => Math.max(highest, Number.parseInt(alias.slice(1), 10)),
      0
    ) + 1;
  for (const entry of entries) {
    if (known.has(entry.action_id)) continue;
    if (Object.keys(table.by_alias).length >= V2_MAX_ACTION_ALIASES || number > 9999) return;
    table.by_alias[`a${number}`] = entry;
    known.add(entry.action_id);
    number += 1;
  }
};

/**
 * Give every unchanged action its previous number back after a bump.
 *
 * Continuity is by semantic identity alone, so `a3` keeps naming the same move
 * while the wire carries only the handle the fresh enumeration issued.  An
 * identity that now names two actions, or none, keeps the number the
 * enumeration gave it and fails closed when asked for by its old name.
 *
 * Returns how many numbers were carried across.
 */
export const rebindActionAliases = (
  fresh: ActionAliasTable,
  previous: Readonly<Record<string, ActionAliasEntry>>
): { readonly table: Record<string, ActionAliasEntry>; readonly rebound: number } => {
  const bySemantics = new Map<string, string[]>();
  for (const [alias, entry] of Object.entries(fresh.by_alias)) {
    if (entry.semantics === '') continue;
    const bucket = bySemantics.get(entry.semantics);
    if (bucket === undefined) bySemantics.set(entry.semantics, [alias]);
    else bucket.push(alias);
  }
  const rebound = new Map<string, ActionAliasEntry>();
  const taken = new Set<string>();
  for (const [alias, entry] of Object.entries(previous)) {
    const candidates = bySemantics.get(entry.semantics) ?? [];
    if (entry.semantics === '' || candidates.length !== 1) continue;
    const candidate = candidates[0];
    if (candidate === undefined || taken.has(candidate) || rebound.has(alias)) continue;
    const found = fresh.by_alias[candidate];
    if (found === undefined) continue;
    rebound.set(alias, found);
    taken.add(candidate);
  }
  const merged = new Map(rebound);
  let number = 1;
  for (const [alias, entry] of Object.entries(fresh.by_alias)) {
    if (taken.has(alias)) continue;
    while (merged.has(`a${number}`)) number += 1;
    if (number > 9999) break;
    merged.set(`a${number}`, entry);
    number += 1;
  }
  const ordered = [...merged.keys()].sort(
    (left, right) => Number.parseInt(left.slice(1), 10) - Number.parseInt(right.slice(1), 10)
  );
  const table: Record<string, ActionAliasEntry> = {};
  for (const alias of ordered) {
    const entry = merged.get(alias);
    if (entry !== undefined) table[alias] = entry;
  }
  return { table, rebound: rebound.size };
};

/** Return the alias prefix an opaque entity ID may be numbered under. */
export const entityAliasPrefix = (identifier: JsonValue): string | null => {
  if (typeof identifier !== 'string') return null;
  for (const [kind, prefix] of Object.entries(ALIAS_ENTITY_PREFIXES)) {
    if (!identifier.startsWith(`${kind}_`)) continue;
    const pattern = kind === 'relation' ? RELATION_ID_RE : ACTOR_ID_RE;
    return pattern.test(identifier) ? prefix : null;
  }
  return null;
};

/** Give each newly seen entity a game-stable u1/c1/p1/r1 name. */
const assignEntityAliases = (draft: Draft, identifiers: ReadonlyArray<JsonValue>): void => {
  const entities = draft.entity_aliases;
  const known = new Set(Object.values(entities));
  const numbers = new Map<string, number>(
    Object.keys(ALIAS_ENTITY_TYPES).map((prefix) => [prefix, 0])
  );
  for (const alias of Object.keys(entities)) {
    const prefix = alias.slice(0, 1);
    const digits = Number.parseInt(alias.slice(1), 10);
    numbers.set(prefix, Math.max(numbers.get(prefix) ?? 0, Number.isNaN(digits) ? 0 : digits));
  }
  for (const identifier of identifiers) {
    const prefix = entityAliasPrefix(identifier);
    if (prefix === null || typeof identifier !== 'string' || known.has(identifier)) continue;
    const used = numbers.get(prefix) ?? 0;
    if (Object.keys(entities).length >= V2_MAX_ENTITY_ALIASES || used >= 9999) continue;
    numbers.set(prefix, used + 1);
    entities[`${prefix}${used + 1}`] = identifier;
    known.add(identifier);
  }
};

/** Cache tile IDs by coordinate so `T(x,y)` resolves without the wire. */
const assignTileAliases = (draft: Draft, tiles: ReadonlyArray<TileReference>): void => {
  const cache = draft.tile_aliases;
  for (const tile of tiles) {
    if (tile.x < -9999 || tile.x > 9999 || tile.y < -9999 || tile.y > 9999) continue;
    const key = `${tile.x},${tile.y}`;
    const current = cache[key];
    if (current === tile.identifier) continue;
    if (current !== undefined) {
      // Tile IDs are game-stable, so a changed ID is contract drift.  Drop the
      // entry: an unknown alias fails closed, a wrong one does not.
      delete cache[key];
      continue;
    }
    if (
      Object.keys(cache).length >= V2_MAX_TILE_ALIASES ||
      Object.values(cache).includes(tile.identifier)
    ) {
      continue;
    }
    cache[key] = tile.identifier;
  }
};

/** Learn entity and tile aliases from one already-validated state page. */
const learnStateAliases = (draft: Draft, items: ReadonlyArray<JsonValue>): void => {
  const identifiers: JsonValue[] = [];
  const tiles: TileReference[] = [];
  for (const item of items) {
    if (!isJsonObject(item)) continue;
    for (const key of ALIAS_ENTITY_KEYS) {
      if (hasField(item, key)) identifiers.push(field(item, key));
    }
    const player = field(item, 'player');
    if (isJsonObject(player) && hasField(player, 'id')) identifiers.push(field(player, 'id'));
    for (const reference of [tileReference(item, 'id'), tileReference(item, 'tile_id')]) {
      if (reference !== null) tiles.push(reference);
    }
  }
  assignEntityAliases(draft, identifiers);
  assignTileAliases(draft, tiles);
};

/**
 * Learn tile and entity aliases from validated action descriptors.
 *
 * An enumerated action names its own actor and target, so a catalog read on its
 * own is enough to give the agent `u3`/`c1` to type back — exactly the entities
 * this page already showed it.
 */
const learnDescriptorAliases = (
  draft: Draft,
  descriptors: ReadonlyArray<LegalActionDescriptor>,
  scope: PageScope | undefined
): void => {
  const identifiers: JsonValue[] = [];
  const tiles: TileReference[] = [];
  if (scope !== undefined) {
    identifiers.push(scope.actor_id);
    identifiers.push(scope.target_id ?? null);
  }
  for (const descriptor of descriptors) {
    const subject = descriptor.subject;
    for (const key of ['actor', 'target'] as const) {
      const value = field(subject, key);
      if (isJsonObject(value)) identifiers.push(field(value, 'id'));
    }
    const reference = tileReference(field(subject, 'target'), 'id');
    if (reference !== null) tiles.push(reference);
  }
  assignEntityAliases(draft, identifiers);
  assignTileAliases(draft, tiles);
};

/** Record that this actor's whole catalog is cached at this revision. */
const rememberDrainedActorDraft = (draft: Draft, actorId: string): void => {
  if (
    !isActorId(actorId) ||
    draft.drained_actors.includes(actorId) ||
    draft.drained_actors.length >= V2_MAX_DRAINED_ACTORS
  ) {
    return;
  }
  draft.drained_actors.push(actorId);
};

/**
 * `_remember_drained_actor` on a whole client state, for callers outside the
 * page ingest.  Returns the state unchanged when the record is already full or
 * already names this actor.
 */
export const rememberDrainedActor = (
  state: V2ClientState,
  actorId: string
): Effect.Effect<V2ClientState, PlayerError> =>
  Effect.map(toDraft(state), (draft) => {
    rememberDrainedActorDraft(draft, actorId);
    return fromDraft(draft);
  });

// ---------------------------------------------------------------------------
// _fresh_action_aliases / _alias_map
// ---------------------------------------------------------------------------

/** The action alias bucket, but only while it names the newest revision. */
export const freshActionAliases = (
  state: V2ClientState
): Effect.Effect<Readonly<Record<string, ActionAliasEntry>>, PlayerError> =>
  Effect.map(parseActionAliases(state.action_aliases), (table) => {
    const recorded = table.state_revision;
    const current = state.last_revision;
    if (recorded === null) return {};
    if (current === null || !revisionsEqual(recorded, current)) return {};
    return table.by_alias;
  });

/** Map every opaque ID this seat can address to its short alias. */
export const aliasMap = (state: V2ClientState): Effect.Effect<AliasMap, PlayerError> =>
  Effect.gen(function* () {
    const entities = yield* parseEntityAliases(state.entity_aliases);
    const tiles = yield* parseTileAliases(state.tile_aliases);
    const actions = yield* freshActionAliases(state);
    const aliases: Record<string, string> = {};
    for (const [alias, identifier] of Object.entries(entities)) aliases[identifier] = alias;
    for (const [key, identifier] of Object.entries(tiles)) aliases[identifier] = `T(${key})`;
    for (const [alias, entry] of Object.entries(actions)) aliases[entry.action_id] = alias;
    return aliases;
  });

// ---------------------------------------------------------------------------
// _remember_page
// ---------------------------------------------------------------------------

const revisionIsNewer = (candidate: Revision, prior: Revision): boolean =>
  candidate.turn > prior.turn ||
  (candidate.turn === prior.turn && candidate.revision > prior.revision);

const revisionIsSameOrder = (candidate: Revision, prior: Revision): boolean =>
  candidate.turn === prior.turn && candidate.revision === prior.revision;

/**
 * Ingest one validated page; yield the promoted catalog worth re-mirroring.
 *
 * The whole body runs under the `.v2-state` lock and re-reads the cache from
 * disk first, exactly as `_remember_page` did, so two concurrent commands can
 * never interleave a half-staged catalog.
 */
export const rememberPage = (
  sessionPath: string,
  session: Session,
  input: RememberPageInput
): Effect.Effect<RememberedPage, PlayerError | LockTimeoutError, SessionStore | PrivateFs> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const files = yield* PrivateFs;
    return yield* store.withStateLock(sessionPath, session, (current) =>
      Effect.gen(function* () {
        const draft = yield* toDraft(current);
        const statePath = store.statePath(sessionPath);
        const save = (): Effect.Effect<V2ClientState, PlayerError> => {
          const next = fromDraft(draft);
          return Effect.as(files.writeJson(statePath, next), next);
        };
        const saveAndFail = (
          message: string
        ): Effect.Effect<never, PlayerError> =>
          Effect.flatMap(save(), () => Effect.fail(playerError(message)));

        const revision = input.page.state_revision;
        const prior = draft.last_revision;
        if (prior === null || revisionIsNewer(revision, prior)) {
          draft.last_revision = revision;
          draft.actions = {};
          draft.pending_catalogs = {};
          // A drained catalog names capabilities of exactly one revision, so
          // the record dies with the actions it describes.
          draft.drained_actors = [];
        } else if (revisionIsSameOrder(revision, prior)) {
          if (!revisionsEqual(revision, prior)) {
            return yield* Effect.fail(
              playerError('state token changed without a newer revision')
            );
          }
        } else {
          // An older authenticated page can be displayed but can never revive
          // an expired action capability in local state.
          return { state: fromDraft(draft), promoted: null };
        }

        if (!input.legal) {
          learnStateAliases(draft, input.page.page.items);
          return { state: yield* save(), promoted: null };
        }

        const body = input.page.page;
        learnDescriptorAliases(draft, body.items, body.scope);
        if (body.scope === undefined) {
          for (const descriptor of body.items) {
            const existing = draft.actions[descriptor.action_id];
            if (existing !== undefined && !jsonEquals(existing, descriptorJson(descriptor))) {
              return yield* Effect.fail(
                playerError('one action ID described two different actions')
              );
            }
            draft.actions[descriptor.action_id] = descriptorJson(descriptor);
          }
          const unique = new Map<string, LegalActionDescriptor>();
          for (const descriptor of body.items) unique.set(descriptor.action_id, descriptor);
          assignActionAliases(draft, revision, yield* aliasEntries([...unique.values()], ''));
          return { state: yield* save(), promoted: null };
        }

        const scope = body.scope;
        const catalogId = body.catalog_id ?? '';
        const complete = body.catalog_complete === true;
        const total = body.total_items;
        const expiry = body.cursor_expires_at;

        for (const [otherId, entry] of Object.entries(draft.pending_catalogs)) {
          const other = yield* readPending(entry);
          if (other !== null && scopesEqual(other.scope, scope) && otherId !== catalogId) {
            delete draft.pending_catalogs[otherId];
          }
        }
        if (!complete && cursorExpired(expiry)) {
          delete draft.pending_catalogs[catalogId];
          return yield* saveAndFail(
            'legal-action catalog cursor expired; restart the scoped query'
          );
        }
        const descriptors = new Map<string, LegalActionDescriptor>();
        for (const descriptor of body.items) descriptors.set(descriptor.action_id, descriptor);
        if (descriptors.size !== body.items.length) {
          delete draft.pending_catalogs[catalogId];
          return yield* saveAndFail('legal-action catalog repeated an action ID');
        }

        // Aliases are assigned only once the catalog is promoted below: a staged
        // descriptor is not executable yet, and numbering it would advertise an
        // `aN` that `persistBatchForAction` must refuse.
        const stagedValue = draft.pending_catalogs[catalogId];
        const pending = stagedValue === undefined ? null : yield* readPending(stagedValue);
        if (
          pending !== null &&
          (!revisionsEqual(pending.state_revision, revision) ||
            !scopesEqual(pending.scope, scope) ||
            pending.total_items !== total)
        ) {
          delete draft.pending_catalogs[catalogId];
          return yield* saveAndFail('legal-action catalog metadata changed');
        }
        // The staged prefix is only decoded now that its revision is proved to
        // be the one this page carries — a descriptor never crosses revisions.
        const accumulated = new Map<string, LegalActionDescriptor>();
        if (pending !== null) {
          for (const [actionId, item] of Object.entries(pending.items)) {
            accumulated.set(
              actionId,
              yield* Effect.mapError(decodeDescriptor(item, revision), () =>
                playerError(PENDING_INVALID)
              )
            );
          }
        }
        for (const [actionId, descriptor] of descriptors) {
          const existing = accumulated.get(actionId);
          if (
            existing !== undefined &&
            !jsonEquals(descriptorJson(existing), descriptorJson(descriptor))
          ) {
            delete draft.pending_catalogs[catalogId];
            return yield* saveAndFail('one catalog action ID described two different actions');
          }
          accumulated.set(actionId, descriptor);
        }
        if (accumulated.size > total) {
          delete draft.pending_catalogs[catalogId];
          return yield* saveAndFail('legal-action catalog exceeded its total');
        }
        if (!complete) {
          // An idempotently replayed older page cannot roll a catalog's
          // continuation cursor backward after later pages were staged.
          const stagedIds =
            pending === null ? new Set<string>() : new Set(Object.keys(pending.items));
          const replay =
            pending !== null &&
            [...descriptors.keys()].every((actionId) => stagedIds.has(actionId)) &&
            accumulated.size === stagedIds.size;
          // `catalog_complete == (next_cursor is None)` is proved by the page
          // validator, so an incomplete catalog always names where to resume.
          const nextCursor = replay && pending !== null ? pending.next_cursor : body.next_cursor;
          const nextExpiry = replay && pending !== null ? pending.cursor_expires_at : expiry;
          if (nextCursor === null) {
            return yield* saveAndFail('invalid legal-actions catalog metadata');
          }
          draft.pending_catalogs[catalogId] = pendingJson({
            state_revision: revision,
            scope,
            total_items: total,
            items: accumulated,
            next_cursor: nextCursor,
            cursor_expires_at: nextExpiry,
          });
          return { state: yield* save(), promoted: null };
        }
        if (
          pending === null &&
          [...descriptors.values()].every((descriptor) =>
            jsonEquals(
              draft.actions[descriptor.action_id] ?? null,
              descriptorJson(descriptor)
            )
          )
        ) {
          // Idempotent replay of an already-promoted final page.
          assignActionAliases(
            draft,
            revision,
            yield* aliasEntries([...descriptors.values()], scope.actor_id)
          );
          return { state: yield* save(), promoted: [...accumulated.values()] };
        }
        if (accumulated.size !== total) {
          delete draft.pending_catalogs[catalogId];
          return yield* saveAndFail('legal-action catalog completed before every item arrived');
        }
        const promoted: Record<string, JsonValue> = { ...draft.actions };
        for (const [actionId, descriptor] of accumulated) {
          const existing = promoted[actionId];
          if (existing !== undefined && !jsonEquals(existing, descriptorJson(descriptor))) {
            delete draft.pending_catalogs[catalogId];
            return yield* saveAndFail('one action ID described two different actions');
          }
          promoted[actionId] = descriptorJson(descriptor);
        }
        draft.actions = promoted;
        assignActionAliases(
          draft,
          revision,
          yield* aliasEntries([...accumulated.values()], scope.actor_id)
        );
        delete draft.pending_catalogs[catalogId];
        // Only a complete, promoted, actor-wide catalog may later be rendered as
        // equivalent to another actor's: a target-scoped catalog is a narrower
        // question, and a partial one proves nothing.
        if (scope.target_id === undefined) rememberDrainedActorDraft(draft, scope.actor_id);
        return { state: yield* save(), promoted: [...accumulated.values()] };
      })
    );
  });

// ---------------------------------------------------------------------------
// _remember_receipt
// ---------------------------------------------------------------------------

const RECEIPT_TRANSITIONS: Readonly<Record<string, ReadonlySet<string>>> = {
  accepted: new Set(['accepted', 'applied', 'rejected', 'ambiguous']),
  applied: new Set(['applied']),
  rejected: new Set(['rejected']),
  ambiguous: new Set(['ambiguous']),
};

const receiptJson = (receipt: CommandReceipt): JsonObject => {
  const value = toJsonValue(receipt);
  return isJsonObject(value) ? value : {};
};

/**
 * Record one validated receipt and retire every capability it outlived.
 *
 * A receipt proves the game moved on, so it retires outstanding actions exactly
 * as a newer page would.  The alias bucket is deliberately left in place: it
 * still names the revision it was built from, which is what lets
 * `expandActionAlias` refuse a stale `aN` **by name** instead of resolving it
 * to an expired handle.
 */
export const rememberReceipt = (
  sessionPath: string,
  session: Session,
  receipt: CommandReceipt
): Effect.Effect<V2ClientState, PlayerError | LockTimeoutError, SessionStore | PrivateFs> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const files = yield* PrivateFs;
    return yield* store.withStateLock(sessionPath, session, (current) =>
      Effect.gen(function* () {
        const draft = yield* toDraft(current);
        const prior = draft.receipts[receipt.batch_id];
        if (prior !== undefined) {
          const priorState = isJsonObject(prior) ? field(prior, 'receipt_state') : null;
          const allowed =
            typeof priorState === 'string'
              ? (RECEIPT_TRANSITIONS[priorState] ?? new Set<string>())
              : new Set<string>();
          if (!allowed.has(receipt.receipt_state)) {
            return yield* Effect.fail(
              playerError('a command receipt regressed or changed terminal state')
            );
          }
        }
        draft.receipts[receipt.batch_id] = receiptJson(receipt);
        const revision = receipt.state_revision;
        const last = draft.last_revision;
        if (last === null || revisionIsNewer(revision, last)) {
          draft.last_revision = revision;
          draft.actions = {};
          draft.pending_catalogs = {};
          draft.drained_actors = [];
        }
        const next = fromDraft(draft);
        yield* files.writeJson(store.statePath(sessionPath), next);
        return next;
      })
    );
  });
