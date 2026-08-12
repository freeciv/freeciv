/**
 * The full-control-v2 **page envelope** — the shape every paginated agent-side
 * read arrives in.
 *
 * Clean-room Effect Schema re-expression of `_validate_page`
 * (`play/client.py:1426-1556`) and `_validate_cursor_expiry`
 * (`play/client.py:1404-1413`), read a second time through
 * `play-cli/src/schema/page.ts`.  Nothing here imports from `play-cli`.
 *
 * One Python function serves both flavours of page, switched by a `legal`
 * flag.  The shape rules it applies are the same for both, so they live here
 * once — {@link pageBodyIssues} — and `./legal-page.ts` supplies only what the
 * flag actually changes: the section literal, the item schema, and permission
 * to carry a `scope`.
 *
 * ## What tolerance changes, and what it must not
 *
 * The Python builds every envelope out of `_exact`, which refuses a payload
 * whose key set is not *equal* to the expected one.  `_exact` therefore
 * enforces two different things at once:
 *
 * 1. **closedness** — an unknown key is a hard error, and
 * 2. **co-occurrence** — which optional keys may appear *together*.
 *
 * `@arena/wire` drops (1) and keeps (2).  Excess properties are preserved and
 * re-encoded (`../tolerant.ts`), because a supervisor that gains a field must
 * not break every reader.  Every other refusal is preserved exactly: missing
 * keys, the two header literals, the ID patterns, the pagination arithmetic,
 * the cursor/expiry pairing, and the catalog-metadata agreement.
 *
 * Concretely, `_validate_page` admits exactly four key sets for `page`
 * (`:1439-1450`):
 *
 * ```text
 * {section, items, total_items, next_cursor}                     legacy bare
 * {…, cursor_expires_at}                                         current bare
 * {…legacy bare, scope}                                          legacy scoped
 * {…current bare, scope, catalog_id, catalog_complete}           current scoped
 * ```
 *
 * Read as co-occurrence, that says: catalog metadata comes as a pair, only
 * alongside a `scope`, and only on a page modern enough to declare
 * `cursor_expires_at`; a scoped page that declares `cursor_expires_at` but no
 * catalog metadata is not a shape any supervisor emits.  {@link pageBodyIssues}
 * enforces exactly that, and nothing about unknown keys.
 *
 * ## Two deliberate divergences from the Python's *output*
 *
 * Both follow from decoding for round-trips rather than for printing, and both
 * come with a helper that recovers the Python's reading:
 *
 * - `_validate_page` **normalizes** an absent `cursor_expires_at` to `null`
 *   (`:1470-1478`).  Here the key stays absent, so `encode(decode(x))` is `x`;
 *   {@link pageCursorExpiry} gives the normalized view.
 * - `_validate_page` **synthesizes** `catalog_id` and `catalog_complete` for a
 *   legacy scoped page (`:1546-1552`).  The Python says at `:1547-1549` that
 *   this inferred identity is private to the client's own catalog cache, so it
 *   is a client fact, not a wire fact: this module exposes the pieces
 *   ({@link legacyCatalogSeed}, {@link legacyCatalogId},
 *   {@link catalogCompleteFor}) instead of inventing keys during decode.
 *
 * @module
 */

import { Either, Schema } from 'effect';
import { CANON_ASCII, canonicalText, type CanonError, type CanonValue } from '../canon.ts';
import {
  decodeTolerant,
  encodeTolerant,
  type TolerantDecoder,
  type TolerantEncoder,
  type WireDecodeError,
} from '../tolerant.ts';
import {
  ActorId,
  ActorType,
  CatalogId,
  Cursor,
  RELATION_ID_RE,
  RelationId,
  TILE_ID_RE,
  TileId,
} from './ids.ts';
import { BoundedJsonValue, hasOwnKey } from './primitives.ts';
import {
  ControlProtocolV2,
  SchemaVersionV2,
  sessionMismatch,
  type V2Session,
} from './protocol.ts';
import { revisionCanonRecord, type Revision, StateRevision } from './revision.ts';

// ---------------------------------------------------------------------------
// Vocabulary — play/client.py:56-67, 1461
// ---------------------------------------------------------------------------

/**
 * `V2_SECTIONS` — `play/client.py:56-67`.  The state sections a page may name.
 *
 * `legal_actions` is deliberately absent: a legal-actions page is a different
 * envelope with a different item type, and the Python branches on the `legal`
 * flag before it ever consults this set (`:1454-1457`).
 *
 * Closed, unlike most vocabularies in this package.  The Python refuses an
 * unknown section outright, and a section this build cannot name is a section
 * whose items it cannot render — tolerating it would hand the caller a page it
 * has no code for.
 */
export const V2_SECTIONS = [
  'overview',
  'pregame_nations',
  'pregame_styles',
  'pregame_teams',
  'chat_recipients',
  'votes',
  'research',
  'governments',
  'diplomacy',
  'diplomacy_clauses',
  'known_tiles',
  'map_tiles',
  'infrastructure',
  'cities',
  'city_sites',
  'units',
  'multipliers',
  'spaceship',
  'tombstones',
  'chat',
  'city_detail',
  'city_citizens',
  'city_build_choices',
  'city_worklist',
  'city_improvements',
  'city_worker_tasks',
  'city_trade_routes',
  'tile_window',
  'city_governor',
  'unit_route',
] as const;

/**
 * `len(items) > 16` refuses (`play/client.py:1461`).
 *
 * A ceiling on the *page*, not on the section: the server pages a 4096-row
 * unit catalog 16 rows at a time, and a reply with more is not a generous
 * server but a different one.
 */
export const V2_PAGE_MAX_ITEMS = 16;

/**
 * The `…Z` ISO-8601 spellings `_validate_cursor_expiry` accepts.
 *
 * The Python rewrites the trailing `Z` to `+00:00`, hands the rest to
 * `datetime.fromisoformat` (`:1408`), and then insists the result really is
 * UTC (`:1411-1412`).  A pattern is the honest way to say that here: it admits
 * exactly the subset `fromisoformat` takes — date, `T` or space, `HH:MM`,
 * optional `:SS`, optional 1-6 fractional digits — and nothing else.  A
 * `+00:00` offset names the same instant and is still refused, because the
 * Python demands the `Z` spelling before it looks at the value at all
 * (`:1405`).
 */
export const CURSOR_EXPIRY_RE: RegExp = /^\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}(:\d{2}(\.\d{1,6})?)?Z$/;

// ---------------------------------------------------------------------------
// _validate_cursor_expiry — play/client.py:1404-1413
// ---------------------------------------------------------------------------

/**
 * The instant a cursor stops being usable: a UTC ISO-8601 timestamp spelled
 * with a trailing `Z`.
 *
 * The pattern is the shape test and the filter is the calendar test, so
 * `2026-13-01T00:00:00Z` is refused here exactly as `fromisoformat` refuses it
 * there.  The string is kept verbatim rather than parsed into a `Date`: it is
 * a wire value that must re-encode byte-identically, and
 * {@link cursorExpiryMillis} is one call away when an instant is what is
 * wanted.
 */
export const CursorExpiry = Schema.String.pipe(
  Schema.pattern(CURSOR_EXPIRY_RE, {
    identifier: 'CursorExpiryPattern',
    description: 'UTC ISO-8601 timestamp ending in Z (client.py _validate_cursor_expiry)',
  }),
  Schema.filter((value) =>
    Number.isNaN(Date.parse(value)) ? 'timestamp names no real instant' : undefined,
  ),
  Schema.brand('CursorExpiry'),
);
/** A validated cursor expiry timestamp. */
export type CursorExpiry = typeof CursorExpiry.Type;

/** Decode a cursor expiry timestamp. */
export const decodeCursorExpiry: TolerantDecoder<CursorExpiry> = decodeTolerant(
  CursorExpiry,
  'CursorExpiry',
);

/**
 * Milliseconds since the epoch for an already-validated cursor expiry.
 *
 * Total on a {@link CursorExpiry}: the brand is only issued to strings
 * `Date.parse` accepts.
 */
export const cursorExpiryMillis = (value: CursorExpiry): number => Date.parse(value);

// ---------------------------------------------------------------------------
// Page scope — play/client.py:1521-1544
// ---------------------------------------------------------------------------

/** The structural view {@link scopeIssues} reads, so the filter types itself. */
interface ScopeShape {
  readonly actor_id: string;
  readonly actor_type: ActorType;
  readonly target_id?: string;
  readonly target_type?: 'diplomatic_relation' | 'tile';
}

const scopeIssues = (scope: ScopeShape): ReadonlyArray<Schema.FilterIssue> => {
  const hasId = hasOwnKey(scope, 'target_id');
  const hasType = hasOwnKey(scope, 'target_type');
  if (!hasId && !hasType) {
    return [];
  }
  if (hasId !== hasType) {
    // The Python's two accepted key sets (`:1523`, `:1531`) make the target a
    // pair; half of one is neither shape.
    return [
      {
        path: [hasId ? 'target_type' : 'target_id'],
        message: 'a scope target needs both target_id and target_type',
      },
    ];
  }
  const paired =
    (scope.actor_type === 'player' &&
      scope.target_type === 'diplomatic_relation' &&
      RELATION_ID_RE.test(scope.target_id ?? '')) ||
    (scope.target_type === 'tile' && TILE_ID_RE.test(scope.target_id ?? ''));
  return paired
    ? []
    : [
        {
          path: ['target_id'],
          message:
            'a diplomatic_relation target is a relation id on a player actor; a tile target is a tile id',
        },
      ];
};

/**
 * What a legal-actions page is *about*: one actor, optionally narrowed to one
 * target.
 *
 * Ports the `scope` branch of `_validate_page` (`:1521-1544`).  Two shapes,
 * and the target pairing is not free-form: a `diplomatic_relation` target is
 * only meaningful for a `player` actor, while a `tile` target is legal for any
 * actor species.
 *
 * `actor_id`'s prefix is deliberately **not** required to agree with
 * `actor_type`.  `ACTOR_ID_RE` accepts all three prefixes and the Python never
 * cross-checks them, so a port that did would refuse payloads the shipped
 * client accepts.
 */
export const PageScope = Schema.Struct({
  actor_id: ActorId,
  actor_type: ActorType,
  target_id: Schema.optionalWith(Schema.Union(RelationId, TileId), { exact: true }),
  target_type: Schema.optionalWith(Schema.Literal('diplomatic_relation', 'tile'), {
    exact: true,
  }),
})
  // Wrapped rather than passed by name: `Schema.filter` infers the schema it
  // refines from its predicate's parameter, and a pre-annotated function would
  // widen that inference to `Schema.Any`.
  .pipe(Schema.filter((scope) => scopeIssues(scope)))
  .annotations({
    identifier: 'PageScope',
    description: 'The actor, and optional target, a legal-actions page is about',
  });
/** A decoded page scope. */
export type PageScope = typeof PageScope.Type;
/** The wire form of a {@link PageScope}. */
export type PageScopeEncoded = typeof PageScope.Encoded;

/** Decode a page scope. */
export const decodePageScope: TolerantDecoder<PageScope> = decodeTolerant(
  PageScope,
  'PageScope',
);

// ---------------------------------------------------------------------------
// The page body — play/client.py:1439-1552
// ---------------------------------------------------------------------------

/** `total_items`: a non-negative count of the rows behind the whole cursor. */
export const PageCount = Schema.Number.pipe(Schema.int(), Schema.nonNegative()).annotations({
  identifier: 'PageCount',
  description: 'A non-negative item count (client.py _validate_page)',
});

/**
 * The pagination triple every page body carries, as a field group both bodies
 * spread.  Shared rather than repeated so the two flavours of page cannot come
 * to disagree about what a cursor is.
 */
export const pageBodyPaginationFields = {
  total_items: PageCount,
  next_cursor: Schema.NullOr(Cursor),
  cursor_expires_at: Schema.optionalWith(Schema.NullOr(CursorExpiry), { exact: true }),
};

/**
 * The scope and catalog keys.
 *
 * A state page declares them too — not because it may carry them, but so that
 * carrying one is a *refusal* (`:1520-1521`) instead of an unknown key quietly
 * preserved by tolerance.  This is the one place where naming a field in a
 * schema makes the schema stricter rather than laxer.
 */
export const pageBodyCatalogFields = {
  scope: Schema.optionalWith(PageScope, { exact: true }),
  catalog_id: Schema.optionalWith(CatalogId, { exact: true }),
  catalog_complete: Schema.optionalWith(Schema.Boolean, { exact: true }),
};

/** The structural view {@link pageBodyIssues} reads, shared by both bodies. */
export interface PageBodyShape {
  readonly items: ReadonlyArray<unknown>;
  readonly total_items: number;
  readonly next_cursor: string | null;
  readonly cursor_expires_at?: string | null;
  readonly scope?: PageScope;
  readonly catalog_id?: string;
  readonly catalog_complete?: boolean;
}

const OPTIONAL_KEYS = ['catalog_complete', 'catalog_id', 'cursor_expires_at', 'scope'] as const;

/** The optional-key sets `_validate_page` admits, as sorted, joined keys. */
const LEGAL_COMBINATIONS: ReadonlySet<string> = new Set([
  '',
  'cursor_expires_at',
  'scope',
  'catalog_complete|catalog_id|cursor_expires_at|scope',
]);

/** The same, for a state page: `scope` and the catalog keys are not its shape. */
const STATE_COMBINATIONS: ReadonlySet<string> = new Set(['', 'cursor_expires_at']);

/**
 * Every cross-field rule `_validate_page` applies to a page body, for either
 * flavour of page.
 *
 * A plain function over a structural view, so the state page and the
 * legal-actions page cannot drift apart: the Python has one implementation and
 * a flag, and so does this.
 */
export const pageBodyIssues = (
  body: PageBodyShape,
  legal: boolean,
): ReadonlyArray<Schema.FilterIssue> => {
  // `:1439-1450` — which optional keys may appear together.
  const present = OPTIONAL_KEYS.filter((key) => hasOwnKey(body, key));
  const allowed = legal ? LEGAL_COMBINATIONS : STATE_COMBINATIONS;
  const shape: ReadonlyArray<Schema.FilterIssue> = allowed.has(present.join('|'))
    ? []
    : !legal && hasOwnKey(body, 'scope')
      ? // `:1520-1521` — a state page never carries a scope.
        [{ path: ['scope'], message: 'a state page carries no scope' }]
      : [
          {
            path: [],
            message:
              'catalog_id and catalog_complete come as a pair, with a scope, on a page that declares cursor_expires_at',
          },
        ];

  // `:1459-1469` — the pagination arithmetic.  `len(items) > 16` is the
  // schema's own `maxItems`; these two need both fields at once.
  const pagination: ReadonlyArray<Schema.FilterIssue> = [
    ...(body.total_items < body.items.length
      ? [
          {
            path: ['total_items'],
            message: `total_items ${String(body.total_items)} is below the ${String(body.items.length)} items on this page`,
          },
        ]
      : []),
    ...(body.next_cursor !== null && body.total_items <= body.items.length
      ? // A cursor with nothing left to fetch would loop a drain forever.
        [{ path: ['next_cursor'], message: 'a next_cursor promises items beyond total_items' }]
      : []),
  ];

  // `:1470-1478` — the expiry is present exactly when the cursor is.
  const expiry: ReadonlyArray<Schema.FilterIssue> =
    hasOwnKey(body, 'cursor_expires_at') &&
    (body.next_cursor === null) !== ((body.cursor_expires_at ?? null) === null)
      ? [
          {
            path: ['cursor_expires_at'],
            message: 'cursor_expires_at is non-null exactly when next_cursor is',
          },
        ]
      : [];

  // `:1537-1545` — `catalog_complete` restates "there is no next page".
  const catalog: ReadonlyArray<Schema.FilterIssue> =
    hasOwnKey(body, 'catalog_complete') && body.catalog_complete !== (body.next_cursor === null)
      ? [
          {
            path: ['catalog_complete'],
            message: 'catalog_complete is true exactly when next_cursor is null',
          },
        ]
      : [];

  return [...shape, ...pagination, ...expiry, ...catalog];
};

/** The section vocabulary a state page may name ({@link V2_SECTIONS}). */
export const StatePageSection = Schema.Literal(...V2_SECTIONS).annotations({
  identifier: 'StatePageSection',
  description: 'A full-control-v2 state section (client.py V2_SECTIONS)',
});
/** A state section name. */
export type StatePageSection = typeof StatePageSection.Type;

/**
 * The body of a state page: one window onto one section, plus the cursor that
 * asks for the next window.
 *
 * Items are opaque: the Python copies each through `_json_value` (`:1518`) and
 * never looks inside, because their shape is per-section and documented by the
 * section contract rather than by the page.  {@link BoundedJsonValue} is that
 * copy's bounds — depth 12, 8192 items, 2048 keys — as a schema.
 */
export const StatePageBody = Schema.Struct({
  section: StatePageSection,
  items: Schema.Array(BoundedJsonValue).pipe(Schema.maxItems(V2_PAGE_MAX_ITEMS)),
  ...pageBodyPaginationFields,
  ...pageBodyCatalogFields,
})
  .pipe(Schema.filter((body) => pageBodyIssues(body, false)))
  .annotations({
    identifier: 'StatePageBody',
    description: 'One page of one v2 state section (client.py _validate_page)',
  });
/** A decoded state page body. */
export type StatePageBody = typeof StatePageBody.Type;

// ---------------------------------------------------------------------------
// The envelope — play/client.py:1316-1327, 1553-1556
// ---------------------------------------------------------------------------

/**
 * The four header fields `_validate_v2_header` reads
 * (`play/client.py:1316-1327`), as a field group every page envelope spreads.
 *
 * `game_id` and `agent_id` are plain strings here and are compared against the
 * session by the `…For(session)` factories: whether a payload belongs to *this*
 * seat is knowledge the payload does not contain.  They are typed `String`
 * rather than `PlayGameId`/`OpaqueId` because the Python compares them for
 * equality with the session's own values and never re-validates their shape —
 * a session whose ids are well-formed makes the pattern check redundant, and a
 * session whose ids are not would fail the comparison anyway.
 */
export const v2PageHeaderFields = {
  schema_version: SchemaVersionV2,
  control_protocol: ControlProtocolV2,
  game_id: Schema.String,
  agent_id: Schema.String,
};

/** A state page: the v2 header, the revision it was read at, and the body. */
export const StatePageEnvelope = Schema.Struct({
  ...v2PageHeaderFields,
  state_revision: StateRevision,
  page: StatePageBody,
}).annotations({
  identifier: 'StatePageEnvelope',
  description: 'A full-control-v2 state page (client.py _validate_page, legal=False)',
});
/** A decoded state page envelope. */
export type StatePageEnvelope = typeof StatePageEnvelope.Type;
/** The wire form of a {@link StatePageEnvelope}. */
export type StatePageEnvelopeEncoded = typeof StatePageEnvelope.Encoded;

/** Decode a state page, without checking which session it belongs to. */
export const decodeStatePage: TolerantDecoder<StatePageEnvelope> = decodeTolerant(
  StatePageEnvelope,
  'StatePageEnvelope',
);

/** Re-encode a state page, unknown server fields included. */
export const encodeStatePage: TolerantEncoder<StatePageEnvelope, StatePageEnvelopeEncoded> =
  encodeTolerant(StatePageEnvelope, 'StatePageEnvelope');

/**
 * A state page schema bound to one session: a page for another game or another
 * agent is drift, not data (`play/client.py:1321-1326`).
 */
export const statePageFor = (
  session: V2Session,
): Schema.Schema<StatePageEnvelope, StatePageEnvelopeEncoded> =>
  StatePageEnvelope.pipe(
    Schema.filter((envelope) => sessionMismatch(session, envelope)),
  ).annotations({ identifier: 'StatePageEnvelope' });

/** Decode a state page that must belong to `session`. */
export const decodeStatePageFor = (session: V2Session): TolerantDecoder<StatePageEnvelope> =>
  decodeTolerant(statePageFor(session), 'StatePageEnvelope');

// ---------------------------------------------------------------------------
// The Python's normalized readings
// ---------------------------------------------------------------------------

/**
 * `clean_page["cursor_expires_at"]` (`play/client.py:1470-1478`): the declared
 * expiry, or `null` when the key was absent.
 */
export const pageCursorExpiry = (body: {
  readonly cursor_expires_at?: string | null;
}): string | null => body.cursor_expires_at ?? null;

/**
 * `complete = cursor is None` (`play/client.py:1551`): a page with no next
 * cursor is the last one, so the catalog it belongs to is whole.
 */
export const catalogCompleteFor = (nextCursor: string | null): boolean => nextCursor === null;

// ---------------------------------------------------------------------------
// _legacy_catalog_id — play/client.py:1416-1423
// ---------------------------------------------------------------------------

/**
 * The exact bytes `_legacy_catalog_id` hashes, as text:
 *
 * ```python
 * json.dumps([session["game_id"], session["agent_id"], revision, scope],
 *            sort_keys=True, separators=(",", ":"), ensure_ascii=True)
 * ```
 *
 * Worth spelling out rather than reaching for `JSON.stringify` twice over.
 * The counters are Python `int`s, so they go through
 * {@link revisionCanonRecord} to be spelled `bigint` and printed `5`, not
 * `5.0`.  And only the *declared* scope keys enter the seed: the Python hashes
 * a dict `_exact` has already proven carries nothing else, so a supervisor
 * that adds a field to `scope` must not silently change an identity this
 * client already cached.
 *
 * Hashing itself is the caller's: `@arena/wire` depends on `effect` alone and
 * ships no SHA-256.  Feed this text to one and hand the hex digest to
 * {@link legacyCatalogId}.
 */
export const legacyCatalogSeed = (
  session: V2Session,
  revision: Revision,
  scope: PageScope,
): Either.Either<string, CanonError> => {
  const scopeRecord: Record<string, CanonValue> = {
    actor_id: scope.actor_id,
    actor_type: scope.actor_type,
    ...(scope.target_id === undefined ? {} : { target_id: scope.target_id }),
    ...(scope.target_type === undefined ? {} : { target_type: scope.target_type }),
  };
  const seed: CanonValue = [
    session.gameId,
    session.agentId,
    revisionCanonRecord(revision),
    scopeRecord,
  ];
  return canonicalText(seed, CANON_ASCII);
};

const decodeCatalogId: TolerantDecoder<CatalogId> = decodeTolerant(CatalogId, 'CatalogId');

/**
 * `"catalog_" + hashlib.sha256(canonical).hexdigest()[:32]`
 * (`play/client.py:1423`), given the digest of {@link legacyCatalogSeed}.
 *
 * The result is *decoded* as a {@link CatalogId} rather than asserted, so a
 * caller who passes something other than a hex digest gets an error value
 * instead of a well-formatted lie.
 */
export const legacyCatalogId = (
  sha256Hex: string,
): Either.Either<CatalogId, WireDecodeError> =>
  decodeCatalogId(`catalog_${sha256Hex.slice(0, 32)}`);
