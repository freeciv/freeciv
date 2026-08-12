/**
 * Legal-actions pages, against the Python that still owns the rules.
 *
 * `play/client.py:_validate_page(..., legal=True)` is the oracle. The payload
 * literals are the golden builders from `play-cli/test/_fixtures/wire.ts:86-113`
 * (`descriptor`, `legalPagePayload`), copied rather than imported —
 * `@arena/wire` depends on `effect` alone.
 *
 * The state page's own rules are exercised in `./page.test.ts`; what is proven
 * here is only what the `legal` flag changes: the section literal, the
 * validated items and their revision agreement, and the scope/catalog shapes a
 * state page is refused for carrying.
 */
import { describe, expect, test } from 'bun:test';
import { Either } from 'effect';
import {
  decodeLegalActionDescriptor,
  decodeLegalPage,
  decodeLegalPageFor,
  descriptorLabelText,
  encodeLegalPage,
  LEGAL_ACTIONS_SECTION,
  scopesEqual,
} from 'src/agent/legal-page';
import { catalogCompleteFor, decodePageScope } from 'src/agent/page';

// --- play-cli/test/_fixtures/wire.ts:12-21, 86-113 ------------------------

const FIXTURE_GAME_ID = 'game_Hsit9YEuBjKdJPPouFoGVYlk';
const FIXTURE_AGENT_ID = 'agent_0123456789abcdef';
const FIXTURE_CURSOR = 'cursor_abcdefghijklmnopqrstuvwxyz012345';
const FIXTURE_REVISION = { turn: 5, revision: 12, state_token: 'token_5_12' };
const FIXTURE_CATALOG = `catalog_${'a'.repeat(32)}`;
const ACTOR_ID = `unit_${'a'.repeat(32)}`;

const SESSION = { gameId: FIXTURE_GAME_ID, agentId: FIXTURE_AGENT_ID };

/** `descriptor` (`play-cli/test/_fixtures/wire.ts:86-94`). */
const descriptor = (overrides: Record<string, unknown> = {}): Record<string, unknown> => ({
  action_id: 'action_found_city_0',
  kind: 'unit.found_city',
  label: 'Found a city',
  subject: { operation: 'found_city' },
  arguments_schema: { properties: { name: { type: 'string' } }, required: ['name'] },
  state_revision: { ...FIXTURE_REVISION },
  ...overrides,
});

/** `legalPagePayload` (`play-cli/test/_fixtures/wire.ts:96-113`). */
const legalPagePayload = (page: Record<string, unknown>): Record<string, unknown> => ({
  schema_version: 2,
  control_protocol: 'full-control-v2',
  game_id: FIXTURE_GAME_ID,
  agent_id: FIXTURE_AGENT_ID,
  state_revision: { ...FIXTURE_REVISION },
  page,
});

const body = (
  items: ReadonlyArray<Record<string, unknown>> = [descriptor()],
  extra: Record<string, unknown> = {},
): Record<string, unknown> => ({
  section: 'legal_actions',
  items,
  total_items: items.length,
  next_cursor: null,
  ...extra,
});

const SCOPE = { actor_id: ACTOR_ID, actor_type: 'unit' };

const rightOrThrow = <A, E>(either: Either.Either<A, E>): A =>
  Either.getOrThrowWith(either, (error) => new Error(`expected Right: ${String(error)}`));

const refusal = (either: Either.Either<unknown, { readonly message: string }>): string => {
  expect(Either.isLeft(either)).toBe(true);
  return Either.isLeft(either) ? either.left.message : '';
};

describe('the section', () => {
  test('a legal page names exactly one section', () => {
    expect(LEGAL_ACTIONS_SECTION).toBe('legal_actions');
    expect(Either.isRight(decodeLegalPage(legalPagePayload(body())))).toBe(true);
    expect(
      Either.isLeft(decodeLegalPage(legalPagePayload({ ...body(), section: 'units' }))),
    ).toBe(true);
  });
});

describe('descriptors', () => {
  test('a descriptor decodes, and its label is kept verbatim', () => {
    const decoded = rightOrThrow(
      decodeLegalPage(legalPagePayload(body([descriptor({ label: '  Found a city  ' })]))),
    );
    const item = decoded.page.items[0];
    expect(item).toBeDefined();
    if (item !== undefined) {
      expect(item.label).toBe('  Found a city  ');
      expect(descriptorLabelText(item)).toBe('Found a city');
      expect<string>(item.kind).toBe('unit.found_city');
    }
  });

  test('a blank label is no choice at all', () => {
    expect(Either.isLeft(decodeLegalActionDescriptor(descriptor({ label: '   ' })))).toBe(true);
  });

  test('a label longer than 240 characters is refused before it reaches an agent', () => {
    expect(
      Either.isLeft(decodeLegalActionDescriptor(descriptor({ label: 'x'.repeat(241) }))),
    ).toBe(true);
    expect(
      Either.isRight(decodeLegalActionDescriptor(descriptor({ label: 'x'.repeat(240) }))),
    ).toBe(true);
  });

  test('kind is a namespace.action pattern, not a free string', () => {
    expect(Either.isLeft(decodeLegalActionDescriptor(descriptor({ kind: 'unit-move' })))).toBe(
      true,
    );
    expect(Either.isRight(decodeLegalActionDescriptor(descriptor({ kind: 'phase.end' })))).toBe(
      true,
    );
  });

  test('a descriptor minted at another revision poisons its whole page', () => {
    const stale = descriptor({
      state_revision: { ...FIXTURE_REVISION, revision: 11 },
    });
    const either = decodeLegalPage(legalPagePayload(body([descriptor(), stale])));
    expect(refusal(either)).toContain('different state revision');
  });

  test('same counters, new token is a republished state, not the same one', () => {
    const republished = descriptor({
      state_revision: { ...FIXTURE_REVISION, state_token: 'token_5_12b' },
    });
    expect(Either.isLeft(decodeLegalPage(legalPagePayload(body([republished]))))).toBe(true);
  });
});

describe('scope and catalog metadata (client.py:1520-1552)', () => {
  test('a legacy scoped page carries a scope and no catalog identity', () => {
    const decoded = rightOrThrow(
      decodeLegalPage(legalPagePayload(body([], { total_items: 0, scope: { ...SCOPE } }))),
    );
    expect<string | undefined>(decoded.page.scope?.actor_id).toBe(ACTOR_ID);
    expect(decoded.page.catalog_id).toBeUndefined();
    // What the client would infer for it: see `legacyCatalogSeed` in page.ts.
    expect(catalogCompleteFor(decoded.page.next_cursor)).toBe(true);
  });

  test('a current scoped page carries the pair, agreeing with the cursor', () => {
    const decoded = rightOrThrow(
      decodeLegalPage(
        legalPagePayload(
          body([], {
            total_items: 0,
            cursor_expires_at: null,
            scope: { ...SCOPE },
            catalog_id: FIXTURE_CATALOG,
            catalog_complete: true,
          }),
        ),
      ),
    );
    expect<string | undefined>(decoded.page.catalog_id).toBe(FIXTURE_CATALOG);
    expect(decoded.page.catalog_complete).toBe(true);
  });

  test('catalog_complete restates "there is no next page", and must not lie', () => {
    const either = decodeLegalPage(
      legalPagePayload(
        body([], {
          total_items: 0,
          cursor_expires_at: null,
          scope: { ...SCOPE },
          catalog_id: FIXTURE_CATALOG,
          catalog_complete: false,
        }),
      ),
    );
    expect(refusal(either)).toContain('catalog_complete is true exactly when next_cursor is null');
  });

  test('a mid-catalog page is complete: false, with a live cursor', () => {
    const decoded = rightOrThrow(
      decodeLegalPage(
        legalPagePayload(
          body([descriptor()], {
            total_items: 9,
            next_cursor: FIXTURE_CURSOR,
            cursor_expires_at: '2026-08-07T12:00:00Z',
            scope: { ...SCOPE },
            catalog_id: FIXTURE_CATALOG,
            catalog_complete: false,
          }),
        ),
      ),
    );
    expect(decoded.page.catalog_complete).toBe(false);
  });

  test('the catalog keys come as a pair', () => {
    const either = decodeLegalPage(
      legalPagePayload(
        body([], {
          total_items: 0,
          cursor_expires_at: null,
          scope: { ...SCOPE },
          catalog_id: FIXTURE_CATALOG,
        }),
      ),
    );
    expect(refusal(either)).toContain('catalog_id and catalog_complete come as a pair');
  });

  test('a scoped page that declares cursor_expires_at must declare the catalog too', () => {
    // The Python's `current_scoped` key set (`:1443-1445`) has no smaller form:
    // scope + cursor_expires_at without the catalog pair is nobody's shape.
    const either = decodeLegalPage(
      legalPagePayload(body([], { total_items: 0, cursor_expires_at: null, scope: { ...SCOPE } })),
    );
    expect(Either.isLeft(either)).toBe(true);
  });

  test('catalog metadata without a scope is refused', () => {
    const either = decodeLegalPage(
      legalPagePayload(
        body([], {
          total_items: 0,
          cursor_expires_at: null,
          catalog_id: FIXTURE_CATALOG,
          catalog_complete: true,
        }),
      ),
    );
    expect(Either.isLeft(either)).toBe(true);
  });
});

describe('session binding and tolerance', () => {
  test('a page for another agent is refused', () => {
    const decode = decodeLegalPageFor(SESSION);
    expect(Either.isRight(decode(legalPagePayload(body())))).toBe(true);
    expect(refusal(decode({ ...legalPagePayload(body()), agent_id: 'agent_other' }))).toContain(
      'response belongs to another agent',
    );
  });

  test('an unknown field on a descriptor survives decode and re-encode', () => {
    const payload = legalPagePayload(
      body([descriptor({ future_descriptor_field: ['kept'] })], { future_body_field: 1 }),
    );
    const encoded: unknown = rightOrThrow(encodeLegalPage(rightOrThrow(decodeLegalPage(payload))));
    expect(encoded).toEqual(payload);
    expect(JSON.stringify(encoded)).toBe(JSON.stringify(payload));
  });
});

describe('scope identity', () => {
  const scopeOf = (raw: Record<string, unknown>) => rightOrThrow(decodePageScope(raw));

  test('two scopes name the same catalog when every protocol key matches', () => {
    expect(scopesEqual(scopeOf({ ...SCOPE }), scopeOf({ ...SCOPE }))).toBe(true);
    expect(
      scopesEqual(scopeOf({ ...SCOPE }), scopeOf({ ...SCOPE, actor_type: 'city' })),
    ).toBe(false);
  });

  test('a fifth key a supervisor added must not split one catalog in two', () => {
    expect(scopesEqual(scopeOf({ ...SCOPE }), scopeOf({ ...SCOPE, future_key: 'x' }))).toBe(true);
  });

  test('a targeted scope differs from the same actor untargeted', () => {
    const targeted = scopeOf({
      ...SCOPE,
      target_id: `tile_${'0'.repeat(32)}`,
      target_type: 'tile',
    });
    expect(scopesEqual(targeted, scopeOf({ ...SCOPE }))).toBe(false);
    expect(scopesEqual(targeted, targeted)).toBe(true);
  });

  test('absent and null are the same absence', () => {
    expect(scopesEqual(null, undefined)).toBe(true);
    expect(scopesEqual(null, scopeOf({ ...SCOPE }))).toBe(false);
  });
});
