/**
 * Page envelopes — state pages and legal-action pages share one validator.
 *
 * Ports `_validate_cursor_expiry` (client.py:1397-1409), `_legacy_catalog_id`
 * (1412-1418) and `_validate_page` (1421-1556).
 */
import { createHash } from 'node:crypto';
import { Effect } from 'effect';
import { DriftError, invalid } from 'src/errors';
import {
  CURSOR_RE,
  FULL_CONTROL_V2,
  V2_PAGE_MAX_ITEMS,
  V2_SECTIONS,
} from 'src/constants';
import { compactJson } from 'src/services/json-output';
import {
  exact,
  field,
  hasField,
  isJsonObject,
  isWholeNumber,
  jsonValue,
  type JsonValue,
  type SessionIdentity,
} from 'src/schema/primitives';
import { decodeV2Header } from 'src/schema/error';
import { decodeRevision, type Revision } from 'src/schema/revision';
import { decodeDescriptor, type LegalActionDescriptor } from 'src/schema/descriptor';
import { isActorId, isCatalogId, isRelationId, isTileId, type ActorType } from 'src/schema/ids';

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export interface PageScope {
  readonly actor_id: string;
  readonly actor_type: ActorType;
  readonly target_id?: string;
  readonly target_type?: string;
}

export interface PageBody<Item> {
  readonly section: string;
  readonly items: ReadonlyArray<Item>;
  readonly total_items: number;
  readonly next_cursor: string | null;
  readonly cursor_expires_at: string | null;
  readonly scope?: PageScope;
  readonly catalog_id?: string;
  readonly catalog_complete?: boolean;
}

export interface PageEnvelopeOf<Item> {
  readonly schema_version: 2;
  readonly control_protocol: typeof FULL_CONTROL_V2;
  readonly game_id: string;
  readonly agent_id: string;
  readonly state_revision: Revision;
  readonly page: PageBody<Item>;
}

export type PageEnvelope = PageEnvelopeOf<JsonValue>;
export type LegalActionPageEnvelope = PageEnvelopeOf<LegalActionDescriptor>;

// ---------------------------------------------------------------------------
// _validate_cursor_expiry
// ---------------------------------------------------------------------------

const ISO_UTC_RE = /^\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}(:\d{2}(\.\d{1,6})?)?Z$/;

/**
 * Accept exactly the `…Z` ISO-8601 spellings `datetime.fromisoformat` takes
 * once the trailing `Z` is rewritten to `+00:00`, and nothing else.
 */
export const cursorExpiry = (value: unknown): Effect.Effect<string, DriftError> => {
  if (typeof value !== 'string' || !value.endsWith('Z') || !ISO_UTC_RE.test(value)) {
    return Effect.fail(invalid('v2 page cursor expiry'));
  }
  const parsed = Date.parse(value);
  return Number.isNaN(parsed)
    ? Effect.fail(invalid('v2 page cursor expiry'))
    : Effect.succeed(value);
};

/** Milliseconds since the epoch for an already-validated cursor expiry. */
export const cursorExpiryMillis = (value: string): number => Date.parse(value);

// ---------------------------------------------------------------------------
// _legacy_catalog_id
// ---------------------------------------------------------------------------

/**
 * The catalog identity this client infers when talking to a supervisor that
 * predates `catalog_id`.  Private to the cache; a prefix is still staged and
 * cannot execute until the terminal page arrives.
 */
export const legacyCatalogId = (
  session: SessionIdentity,
  revision: Revision,
  scope: PageScope
): string => {
  const canonical = compactJson([session.gameId, session.agentId, revision, scope]);
  const digest = createHash('sha256').update(canonical, 'utf8').digest('hex');
  return `catalog_${digest.slice(0, 32)}`;
};

// ---------------------------------------------------------------------------
// scope
// ---------------------------------------------------------------------------

const SCOPE_ACTOR_FIELDS: ReadonlySet<string> = new Set(['actor_id', 'actor_type']);
const SCOPE_TARGET_FIELDS: ReadonlySet<string> = new Set([
  'actor_id',
  'actor_type',
  'target_id',
  'target_type',
]);

const setsEqual = (left: ReadonlySet<string>, right: ReadonlySet<string>): boolean =>
  left.size === right.size && [...left].every((name) => right.has(name));

const decodeScope = (raw: JsonValue): Effect.Effect<PageScope, DriftError> =>
  Effect.gen(function* () {
    const present = isJsonObject(raw) ? new Set(Object.keys(raw)) : new Set<string>();
    if (setsEqual(present, SCOPE_ACTOR_FIELDS)) {
      const scope = yield* exact(raw, SCOPE_ACTOR_FIELDS, 'page scope');
      const actorId = field(scope, 'actor_id');
      const actorType = field(scope, 'actor_type');
      const valid =
        isActorId(actorId) &&
        (actorType === 'player' || actorType === 'city' || actorType === 'unit');
      if (!valid) {
        return yield* Effect.fail(invalid('legal-actions page scope'));
      }
      return { actor_id: actorId, actor_type: actorType };
    }
    if (setsEqual(present, SCOPE_TARGET_FIELDS)) {
      const scope = yield* exact(raw, SCOPE_TARGET_FIELDS, 'page scope');
      const actorId = field(scope, 'actor_id');
      const actorType = field(scope, 'actor_type');
      const targetId = field(scope, 'target_id');
      const targetType = field(scope, 'target_type');
      const valid =
        isActorId(actorId) &&
        (actorType === 'player' || actorType === 'unit' || actorType === 'city') &&
        typeof targetId === 'string' &&
        ((actorType === 'player' &&
          isRelationId(targetId) &&
          targetType === 'diplomatic_relation') ||
          ((actorType === 'player' || actorType === 'unit' || actorType === 'city') &&
            isTileId(targetId) &&
            targetType === 'tile'));
      if (!valid) {
        return yield* Effect.fail(invalid('legal-actions page scope'));
      }
      return {
        actor_id: actorId,
        actor_type: actorType,
        target_id: targetId,
        target_type: targetType,
      };
    }
    return yield* Effect.fail(invalid('legal-actions page scope'));
  });

// ---------------------------------------------------------------------------
// _validate_page
// ---------------------------------------------------------------------------

const ENVELOPE_FIELDS: ReadonlySet<string> = new Set([
  'schema_version',
  'control_protocol',
  'game_id',
  'agent_id',
  'state_revision',
  'page',
]);

const LEGACY_BASE = ['section', 'items', 'total_items', 'next_cursor'] as const;

const decodePageShape = <Item>(
  value: unknown,
  session: SessionIdentity,
  legal: boolean,
  decodeItem: (item: JsonValue, revision: Revision) => Effect.Effect<Item, DriftError>
): Effect.Effect<PageEnvelopeOf<Item>, DriftError> =>
  Effect.gen(function* () {
    const raw = yield* decodeV2Header(value, session, ENVELOPE_FIELDS, 'v2 page');
    const revision = yield* decodeRevision(field(raw, 'state_revision'));
    const page = field(raw, 'page');
    if (!isJsonObject(page)) {
      return yield* Effect.fail(invalid('v2 page envelope'));
    }
    const present = new Set(Object.keys(page));
    const legacyBase = new Set<string>(LEGACY_BASE);
    const currentBase = new Set<string>([...LEGACY_BASE, 'cursor_expires_at']);
    const legacyScoped = setsEqual(present, new Set<string>([...legacyBase, 'scope']));
    const currentScoped = setsEqual(
      present,
      new Set<string>([...currentBase, 'scope', 'catalog_id', 'catalog_complete'])
    );
    const bare = setsEqual(present, legacyBase) || setsEqual(present, currentBase);
    if (!bare && !legacyScoped && !currentScoped) {
      return yield* Effect.fail(invalid('v2 page envelope'));
    }

    const section = field(page, 'section');
    if (legal) {
      if (section !== 'legal_actions') {
        return yield* Effect.fail(invalid('legal-actions page section'));
      }
    } else if (typeof section !== 'string' || !V2_SECTIONS.has(section)) {
      return yield* Effect.fail(invalid('state page section'));
    }

    const items = field(page, 'items');
    const total = field(page, 'total_items');
    const cursor = field(page, 'next_cursor');
    if (
      !Array.isArray(items) ||
      items.length > V2_PAGE_MAX_ITEMS ||
      !isWholeNumber(total) ||
      total < items.length ||
      (cursor !== null && (typeof cursor !== 'string' || !CURSOR_RE.test(cursor))) ||
      (cursor !== null && total <= items.length)
    ) {
      return yield* Effect.fail(invalid('v2 page pagination'));
    }

    let expiry: string | null = null;
    if (hasField(page, 'cursor_expires_at')) {
      const declared = field(page, 'cursor_expires_at');
      if (cursor === null) {
        if (declared !== null) {
          return yield* Effect.fail(invalid('v2 page cursor expiry'));
        }
      } else {
        expiry = yield* cursorExpiry(declared);
      }
    }

    const cleanItems: Item[] = [];
    for (const item of items) {
      cleanItems.push(yield* decodeItem(item, revision));
    }

    const body: {
      -readonly [K in keyof PageBody<Item>]: PageBody<Item>[K];
    } = {
      section: section,
      items: cleanItems,
      total_items: total,
      next_cursor: cursor,
      cursor_expires_at: expiry,
    };

    if (hasField(page, 'scope')) {
      if (!legal) {
        return yield* Effect.fail(invalid('state page scope'));
      }
      const scope = yield* decodeScope(field(page, 'scope'));
      body.scope = scope;
      if (currentScoped) {
        const catalogId = field(page, 'catalog_id');
        const complete = field(page, 'catalog_complete');
        if (
          !isCatalogId(catalogId) ||
          typeof complete !== 'boolean' ||
          complete !== (cursor === null)
        ) {
          return yield* Effect.fail(invalid('legal-actions catalog metadata'));
        }
        body.catalog_id = catalogId;
        body.catalog_complete = complete;
      } else {
        body.catalog_id = legacyCatalogId(session, revision, scope);
        body.catalog_complete = cursor === null;
      }
    }

    return {
      schema_version: 2,
      control_protocol: FULL_CONTROL_V2,
      game_id: session.gameId,
      agent_id: session.agentId,
      state_revision: revision,
      page: body,
    } as const;
  });

/** A state page: items are opaque JSON, copied through unchanged. */
export const decodePage = (
  value: unknown,
  session: SessionIdentity
): Effect.Effect<PageEnvelope, DriftError> =>
  decodePageShape<JsonValue>(value, session, false, (item) =>
    jsonValue(item, 'state page item')
  );

/** A legal-actions page: every item is a validated descriptor. */
export const decodeLegalPage = (
  value: unknown,
  session: SessionIdentity
): Effect.Effect<LegalActionPageEnvelope, DriftError> =>
  decodePageShape<LegalActionDescriptor>(value, session, true, (item, revision) =>
    decodeDescriptor(item, revision)
  );

export { decodeScope };
