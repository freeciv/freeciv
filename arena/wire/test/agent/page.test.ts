/**
 * State page envelopes, against the Python that still owns the rules.
 *
 * `play/client.py:_validate_page` is the oracle. The payload literals are the
 * golden builders from `play-cli/test/_fixtures/wire.ts:12-84` (`pagePayload`,
 * `FIXTURE_GAME_ID`, `FIXTURE_AGENT_ID`, `FIXTURE_CURSOR`, `FIXTURE_REVISION`),
 * copied rather than imported: `@arena/wire` depends on `effect` alone and must
 * never reach into `play-cli`.
 *
 * The refusals are the interesting half. Where this port is *deliberately*
 * laxer than the Python — unknown keys are preserved, not refused — that is
 * asserted too, next to a case proving the co-occurrence half of `_exact`
 * survived.
 */
import { createHash } from 'node:crypto';
import { describe, expect, test } from 'bun:test';
import { Either } from 'effect';
import {
  catalogCompleteFor,
  CURSOR_EXPIRY_RE,
  cursorExpiryMillis,
  decodeCursorExpiry,
  decodePageScope,
  decodeStatePage,
  decodeStatePageFor,
  encodeStatePage,
  legacyCatalogId,
  legacyCatalogSeed,
  pageCursorExpiry,
  StatePageEnvelope,
  V2_PAGE_MAX_ITEMS,
  V2_SECTIONS,
} from 'src/agent/page';
import { decodeRevision } from 'src/agent/revision';
import type { JsonValue } from 'src/json';

// --- play-cli/test/_fixtures/wire.ts:12-21 --------------------------------

const FIXTURE_GAME_ID = 'game_Hsit9YEuBjKdJPPouFoGVYlk';
const FIXTURE_AGENT_ID = 'agent_0123456789abcdef';
const FIXTURE_CURSOR = 'cursor_abcdefghijklmnopqrstuvwxyz012345';
const FIXTURE_REVISION = { turn: 5, revision: 12, state_token: 'token_5_12' };

const SESSION = { gameId: FIXTURE_GAME_ID, agentId: FIXTURE_AGENT_ID };

const ACTOR_ID = `unit_${'a'.repeat(32)}`;

/** `pagePayload` (`play-cli/test/_fixtures/wire.ts:67-84`), page body overridable. */
const pagePayload = (page: Record<string, unknown>): Record<string, unknown> => ({
  schema_version: 2,
  control_protocol: 'full-control-v2',
  game_id: FIXTURE_GAME_ID,
  agent_id: FIXTURE_AGENT_ID,
  state_revision: { ...FIXTURE_REVISION },
  page,
});

const bareBody = (items: ReadonlyArray<JsonValue> = [{ id: 'unit_0', name: 'Warriors' }]) => ({
  section: 'units',
  items,
  total_items: items.length,
  next_cursor: null,
});

/** `count` distinct items, for the page ceiling. */
const many = (count: number): ReadonlyArray<JsonValue> =>
  Array.from({ length: count }, (_unused, index) => ({ id: index }));

/** A `{n: {n: …}}` chain `depth` levels deep, for the `_json_value` ceiling. */
const nest = (depth: number): JsonValue =>
  Array.from({ length: depth }).reduce<JsonValue>((inner) => ({ n: inner }), 1);

const rightOrThrow = <A, E>(either: Either.Either<A, E>): A =>
  Either.getOrThrowWith(either, (error) => new Error(`expected Right: ${String(error)}`));

const refusal = (either: Either.Either<unknown, { readonly message: string }>): string => {
  expect(Either.isLeft(either)).toBe(true);
  return Either.isLeft(either) ? either.left.message : '';
};

describe('the four page shapes', () => {
  test('a legacy bare page decodes, and declares no expiry at all', () => {
    const decoded = rightOrThrow(decodeStatePage(pagePayload(bareBody())));
    expect(decoded.page.section).toBe('units');
    expect(decoded.page.items).toHaveLength(1);
    // The Python normalizes the absent key to null; this port keeps it absent
    // so the round trip is exact, and offers the normalized reading.
    expect(Object.hasOwn(decoded.page, 'cursor_expires_at')).toBe(false);
    expect(pageCursorExpiry(decoded.page)).toBeNull();
    expect(decoded.page.catalog_id).toBeUndefined();
  });

  test('a current bare page carries a cursor and its expiry', () => {
    const decoded = rightOrThrow(
      decodeStatePage(
        pagePayload({
          ...bareBody(),
          total_items: 3,
          next_cursor: FIXTURE_CURSOR,
          cursor_expires_at: '2026-08-07T12:00:00Z',
        }),
      ),
    );
    expect<string | null>(decoded.page.next_cursor).toBe(FIXTURE_CURSOR);
    expect(pageCursorExpiry(decoded.page)).toBe('2026-08-07T12:00:00Z');
    expect(catalogCompleteFor(decoded.page.next_cursor)).toBe(false);
  });

  test('a scope on a state page is refused — it is the legal page that is scoped', () => {
    const either = decodeStatePage(
      pagePayload({
        ...bareBody([]),
        total_items: 0,
        scope: { actor_id: ACTOR_ID, actor_type: 'unit' },
      }),
    );
    expect(refusal(either)).toContain('a state page carries no scope');
  });

  test('catalog metadata is refused on a state page even when it is well formed', () => {
    const either = decodeStatePage(
      pagePayload({
        ...bareBody([]),
        total_items: 0,
        cursor_expires_at: null,
        catalog_id: `catalog_${'a'.repeat(32)}`,
        catalog_complete: true,
      }),
    );
    expect(Either.isLeft(either)).toBe(true);
  });
});

describe('required fields stay required', () => {
  test('a page missing total_items is refused, tolerance or not', () => {
    const body: Record<string, unknown> = { ...bareBody() };
    delete body['total_items'];
    expect(refusal(decodeStatePage(pagePayload(body)))).toContain('total_items');
  });

  test('an unknown section is refused: this build could not render its items', () => {
    expect(V2_SECTIONS).toContain('units');
    expect(V2_SECTIONS).not.toContain('legal_actions');
    expect(Either.isLeft(decodeStatePage(pagePayload({ ...bareBody(), section: 'moons' })))).toBe(
      true,
    );
  });

  test('the two header literals are closed', () => {
    expect(
      Either.isLeft(decodeStatePage({ ...pagePayload(bareBody()), schema_version: 1 })),
    ).toBe(true);
    expect(
      Either.isLeft(
        decodeStatePage({ ...pagePayload(bareBody()), control_protocol: 'strategic-v1' }),
      ),
    ).toBe(true);
  });
});

describe('pagination arithmetic (client.py:1459-1469)', () => {
  test('total_items may not be below the items on the page', () => {
    const either = decodeStatePage(
      pagePayload({ ...bareBody([{ id: 'a' }, { id: 'b' }]), total_items: 1 }),
    );
    expect(refusal(either)).toContain('is below the 2 items');
  });

  test('a cursor with nothing left to fetch is drift, not a last page', () => {
    const either = decodeStatePage(
      pagePayload({
        ...bareBody(),
        total_items: 1,
        next_cursor: FIXTURE_CURSOR,
        cursor_expires_at: '2026-08-07T12:00:00Z',
      }),
    );
    expect(refusal(either)).toContain('promises items beyond total_items');
  });

  test('a page may carry sixteen items and not seventeen', () => {
    expect(V2_PAGE_MAX_ITEMS).toBe(16);
    expect(Either.isRight(decodeStatePage(pagePayload(bareBody(many(16)))))).toBe(true);
    expect(Either.isLeft(decodeStatePage(pagePayload(bareBody(many(17)))))).toBe(true);
  });

  test('next_cursor must look like a cursor', () => {
    const either = decodeStatePage(
      pagePayload({ ...bareBody(), total_items: 9, next_cursor: 'cursor_short' }),
    );
    expect(Either.isLeft(either)).toBe(true);
  });
});

describe('cursor expiry (client.py:1404-1413)', () => {
  test('only the Z spelling is accepted, and only if it names a real instant', () => {
    expect(Either.isRight(decodeCursorExpiry('2026-08-07T12:00:00Z'))).toBe(true);
    expect(Either.isRight(decodeCursorExpiry('2026-08-07T12:00Z'))).toBe(true);
    expect(Either.isRight(decodeCursorExpiry('2026-08-07T12:00:00.123456Z'))).toBe(true);
    // Same instant, spelled the way `fromisoformat` is never asked to read it.
    expect(Either.isLeft(decodeCursorExpiry('2026-08-07T12:00:00+00:00'))).toBe(true);
    expect(Either.isLeft(decodeCursorExpiry('2026-08-07T12:00:00'))).toBe(true);
    // Shape-legal, calendar-illegal: the pattern passes and the filter does not.
    expect(CURSOR_EXPIRY_RE.test('2026-13-01T00:00:00Z')).toBe(true);
    expect(Either.isLeft(decodeCursorExpiry('2026-13-01T00:00:00Z'))).toBe(true);
  });

  test('an expiry is non-null exactly when the cursor is', () => {
    const withCursorNoExpiry = decodeStatePage(
      pagePayload({
        ...bareBody(),
        total_items: 4,
        next_cursor: FIXTURE_CURSOR,
        cursor_expires_at: null,
      }),
    );
    expect(refusal(withCursorNoExpiry)).toContain('cursor_expires_at is non-null exactly when');

    const noCursorWithExpiry = decodeStatePage(
      pagePayload({ ...bareBody(), cursor_expires_at: '2026-08-07T12:00:00Z' }),
    );
    expect(Either.isLeft(noCursorWithExpiry)).toBe(true);

    // The legacy pairing: no cursor, and the key present as null.
    expect(
      Either.isRight(decodeStatePage(pagePayload({ ...bareBody(), cursor_expires_at: null }))),
    ).toBe(true);
  });

  test('the millisecond reading is total on a decoded expiry', () => {
    const expiry = rightOrThrow(decodeCursorExpiry('2026-08-07T12:00:00Z'));
    expect(cursorExpiryMillis(expiry)).toBe(Date.parse('2026-08-07T12:00:00Z'));
  });
});

describe('page scope (client.py:1521-1544)', () => {
  const scope = (extra: Record<string, unknown>): Record<string, unknown> => ({
    actor_id: ACTOR_ID,
    actor_type: 'unit',
    ...extra,
  });

  test('an actor-only scope is one of the two accepted shapes', () => {
    expect(Either.isRight(decodePageScope(scope({})))).toBe(true);
  });

  test('a tile target is legal for any actor species', () => {
    const target = { target_id: `tile_${'0'.repeat(32)}`, target_type: 'tile' };
    expect(Either.isRight(decodePageScope(scope(target)))).toBe(true);
    expect(
      Either.isRight(
        decodePageScope({ actor_id: `player_${'b'.repeat(32)}`, actor_type: 'player', ...target }),
      ),
    ).toBe(true);
  });

  test('a diplomatic_relation target is only meaningful for a player', () => {
    const target = {
      target_id: `relation_${'c'.repeat(32)}`,
      target_type: 'diplomatic_relation',
    };
    expect(
      Either.isRight(
        decodePageScope({ actor_id: `player_${'b'.repeat(32)}`, actor_type: 'player', ...target }),
      ),
    ).toBe(true);
    expect(Either.isLeft(decodePageScope(scope(target)))).toBe(true);
  });

  test('a target is a pair; half of one is neither shape', () => {
    const either = decodePageScope(scope({ target_id: `tile_${'0'.repeat(32)}` }));
    expect(refusal(either)).toContain('needs both target_id and target_type');
  });

  test('a tile id under a diplomatic_relation type is refused', () => {
    expect(
      Either.isLeft(
        decodePageScope(
          scope({ target_id: `tile_${'0'.repeat(32)}`, target_type: 'diplomatic_relation' }),
        ),
      ),
    ).toBe(true);
  });

  test("an actor_id prefix need not agree with actor_type — the Python never checked", () => {
    expect(
      Either.isRight(decodePageScope({ actor_id: `city_${'d'.repeat(32)}`, actor_type: 'unit' })),
    ).toBe(true);
  });
});

describe('session binding (client.py:1321-1326)', () => {
  test('a page for another game, or another agent, is refused', () => {
    const decode = decodeStatePageFor(SESSION);
    expect(Either.isRight(decode(pagePayload(bareBody())))).toBe(true);
    expect(
      refusal(decode({ ...pagePayload(bareBody()), game_id: 'game_someoneelsesgamehandle00' })),
    ).toContain('response belongs to another game');
    expect(refusal(decode({ ...pagePayload(bareBody()), agent_id: 'agent_other' }))).toContain(
      'response belongs to another agent',
    );
  });
});

describe('tolerance', () => {
  test('a field a newer supervisor added survives decode and re-encode', () => {
    const payload = {
      ...pagePayload({ ...bareBody(), future_page_field: 'kept' }),
      future_envelope_field: 7,
    };
    const decoded = rightOrThrow(decodeStatePage(payload));
    const encoded: unknown = rightOrThrow(encodeStatePage(decoded));
    expect(encoded).toEqual(payload);
    expect(JSON.stringify(encoded)).toBe(JSON.stringify(payload));
  });

  test('an item is opaque, but not unbounded (client.py _json_value)', () => {
    expect(Either.isRight(decodeStatePage(pagePayload(bareBody([nest(11)]))))).toBe(true);
    expect(Either.isLeft(decodeStatePage(pagePayload(bareBody([nest(14)]))))).toBe(true);
  });

  test('the schema names itself, so a refusal says which packet drifted', () => {
    const either = decodeStatePage(pagePayload({ ...bareBody(), section: 'moons' }));
    expect(Either.isLeft(either)).toBe(true);
    if (Either.isLeft(either)) {
      expect(either.left.schemaName).toBe('StatePageEnvelope');
      expect(either.left.issues.length).toBeGreaterThan(0);
    }
    expect(StatePageEnvelope.ast.toString()).toContain('StatePageEnvelope');
  });
});

describe('the legacy catalog identity (client.py:1416-1423)', () => {
  // Decoded, not asserted: the seed takes validated values, so the test builds
  // them the only way a caller can.
  const revision = rightOrThrow(decodeRevision({ ...FIXTURE_REVISION }));
  const scope = rightOrThrow(decodePageScope({ actor_id: ACTOR_ID, actor_type: 'unit' }));

  test('the seed is the bytes CPython hashes, key order and all', () => {
    const seed = rightOrThrow(legacyCatalogSeed(SESSION, revision, scope));
    // Golden from CPython:
    //   json.dumps([game_id, agent_id, revision, scope],
    //              sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    expect(seed).toBe(
      '["game_Hsit9YEuBjKdJPPouFoGVYlk","agent_0123456789abcdef",' +
        '{"revision":12,"state_token":"token_5_12","turn":5},' +
        `{"actor_id":"${ACTOR_ID}","actor_type":"unit"}]`,
    );
    // The counters are Python ints: `12`, never `12.0`.
    expect(seed).toContain('"revision":12');
  });

  test('the digest is the identity the Python would have inferred', () => {
    const seed = rightOrThrow(legacyCatalogSeed(SESSION, revision, scope));
    const digest = createHash('sha256').update(seed, 'ascii').digest('hex');
    const catalogId = rightOrThrow(legacyCatalogId(digest));
    // Golden: "catalog_" + hashlib.sha256(canonical).hexdigest()[:32]
    expect<string>(catalogId).toBe('catalog_f6560b3497b2d9eb89c2872c806bf94a');
    expect<string>(catalogId).toMatch(/^catalog_[A-Za-z0-9_-]{32}$/);
  });

  test('a digest that is not one is an error value, not a well-formatted lie', () => {
    expect(Either.isLeft(legacyCatalogId('not a digest'))).toBe(true);
  });
});
