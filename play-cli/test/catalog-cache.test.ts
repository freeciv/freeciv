/**
 * What the cached catalog answers without the wire.
 *
 * `catalogSignature`, `catalogEquivalence`, `cachedDescriptors`,
 * `cachedKindScopes`, `kindList` and `promotedCatalogPage`.
 *
 * The three row renderers are U11's and arrive as {@link CatalogRenderDeps};
 * the stubs below are the smallest faithful stand-ins for `_compact_legal_action`,
 * `_action_kind_key`, `_legal_row` and `_kind_selector_matches`, so what is under
 * test here is the *comparison*: which catalogs are eligible, what "the same
 * options in the same order" means, and which rows are reported as differing.
 */
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either, Layer } from 'effect';
import { FULL_CONTROL_V2 } from 'src/constants';
import { decodeLegalPage, type LegalActionPageEnvelope, type PageScope } from 'src/schema/page';
import { field, isJsonObject, type JsonObject, type JsonValue } from 'src/schema/primitives';
import { scalar } from 'src/render/primitives';
import { PrivateFs } from 'src/services/private-fs';
import {
  SessionStore,
  sessionStoreFor,
  type Session,
  type SessionStoreApi,
  type V2ClientState,
} from 'src/services/session-store';
import { aliasMap, rememberPage, v2StateSchema, type AliasMap } from 'src/services/aliases';
import {
  V2_KIND_LIST_MAX,
  cachedActorCatalog,
  cachedDescriptors,
  cachedKindScopes,
  catalogEquivalence,
  catalogSignature,
  kindList,
  promotedCatalogPage,
  type CatalogRenderDeps,
  type CompactLegalResult,
} from 'src/services/catalog-cache';
import { FIXTURE_GAME_ID, scratchWorkspace, sessionFile, type Scratch } from 'test/_fixtures';

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

const scratches: Scratch[] = [];
afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

interface TestRevision {
  readonly turn: number;
  readonly revision: number;
  readonly state_token: string;
  readonly [key: string]: JsonValue;
}

interface Fixture {
  readonly store: SessionStoreApi;
  readonly sessionPath: string;
  readonly session: Session;
  readonly run: <A, E>(
    effect: Effect.Effect<A, E, SessionStore | PrivateFs>
  ) => Either.Either<A, E>;
}

const fixture = (): Fixture => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const store = sessionStoreFor(scratch.workspace, scratch.files, v2StateSchema, {});
  const sessionPath = path.join(scratch.workspace.stateRoot, FIXTURE_GAME_ID, 'codex-test.json');
  Effect.runSync(
    scratch.files.writeJson(sessionPath, sessionFile({ control_protocol: FULL_CONTROL_V2 }))
  );
  const loaded = Effect.runSync(store.resolveV2(sessionPath));
  const layer = Layer.merge(
    Layer.succeed(SessionStore, store),
    Layer.succeed(PrivateFs, scratch.files)
  );
  return {
    store,
    sessionPath,
    session: loaded.session,
    run: (effect) => Effect.runSync(Effect.either(Effect.provide(effect, layer))),
  };
};

const ok = <A, E>(either: Either.Either<A, E>): A => {
  if (Either.isLeft(either)) {
    throw new Error(`expected success, got ${JSON.stringify(either.left)}`);
  }
  return either.right;
};

const revision = (number = 7, turn = 3): TestRevision => ({
  turn,
  revision: number,
  state_token: `state_${String(number).padStart(32, '0')}`,
});

const actorAction = (
  stateRevision: TestRevision,
  actionId: string,
  actorId: string,
  options: { kind?: string; operation?: string; label?: string; x?: number; y?: number } = {}
): JsonObject => {
  const x = options.x ?? 31;
  const y = options.y ?? 72;
  return {
    action_id: actionId,
    kind: options.kind ?? 'unit.order',
    label: options.label ?? 'Move',
    subject: {
      operation: options.operation ?? 'move',
      actor: { id: actorId, type: 'unit', name: 'Settlers' },
      target: {
        id: `tile_${`${String(x).padStart(4, '0')}${String(y).padStart(4, '0')}`.padStart(32, '0')}`,
        x,
        y,
      },
    },
    arguments_schema: { type: 'object' },
    state_revision: stateRevision,
  };
};

const scopedLegalPage = (
  stateRevision: TestRevision,
  items: ReadonlyArray<JsonValue>,
  actorId: string,
  catalog: string
): JsonObject => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  game_id: FIXTURE_GAME_ID,
  agent_id: 'agent_0123456789abcdef',
  state_revision: stateRevision,
  page: {
    section: 'legal_actions',
    items,
    total_items: items.length,
    next_cursor: null,
    cursor_expires_at: null,
    scope: { actor_id: actorId, actor_type: 'unit' },
    catalog_id: catalog,
    catalog_complete: true,
  },
});

const ingest = (fx: Fixture, page: JsonObject): V2ClientState =>
  ok(
    fx.run(
      Effect.flatMap(
        Effect.mapError(decodeLegalPage(page, fx.session), (error) => ({
          message: error.message,
        })),
        (decoded) => rememberPage(fx.sessionPath, fx.session, { legal: true, page: decoded })
      )
    )
  ).state;

// ---------------------------------------------------------------------------
// The U11 stand-ins
// ---------------------------------------------------------------------------

const RESERVED = new Set(['target', 'probability', 'gold_cost']);

const compactLegalAction = (descriptor: JsonObject): JsonObject => {
  const subject = field(descriptor, 'subject');
  const compactSubject: Record<string, JsonValue> = {};
  if (isJsonObject(subject)) {
    for (const [key, value] of Object.entries(subject)) {
      if (!RESERVED.has(key)) compactSubject[key] = value;
    }
  }
  return {
    action_id: field(descriptor, 'action_id'),
    kind: field(descriptor, 'kind'),
    label: field(descriptor, 'label'),
    subject: compactSubject,
    target: isJsonObject(subject) ? field(subject, 'target') : null,
    argument_schema: field(descriptor, 'arguments_schema'),
  };
};

const kindKeyOf = (kind: JsonValue, operation: JsonValue): string => {
  if (typeof kind !== 'string') return '';
  if (typeof operation !== 'string' || operation === '' || kind.endsWith(`.${operation}`)) {
    return kind;
  }
  return `${kind}/${operation}`;
};

const deps: CatalogRenderDeps = {
  compactLegalAction: (descriptor) => Effect.succeed(compactLegalAction(descriptor)),
  actionKindKey: (compact) => {
    const subject = field(compact, 'subject');
    return Effect.succeed(
      kindKeyOf(field(compact, 'kind'), isJsonObject(subject) ? field(subject, 'operation') : null)
    );
  },
  legalRow: (alias, compact, scope, aliases) => {
    const subject = field(compact, 'subject');
    const target = field(compact, 'target');
    const detail: string[] = [];
    if (isJsonObject(target)) {
      const x = field(target, 'x');
      const y = field(target, 'y');
      detail.push(`T(${scalar(x)},${scalar(y)})`);
    }
    const actor = isJsonObject(subject) ? field(subject, 'actor') : null;
    const actorId = isJsonObject(actor) ? field(actor, 'id') : null;
    if (typeof actorId === 'string' && actorId !== (scope === null ? null : scope.actor_id)) {
      detail.push(`actor=${aliases?.[actorId] ?? actorId}`);
    }
    const label = field(compact, 'label');
    return Effect.succeed([
      alias,
      kindKeyOf(field(compact, 'kind'), isJsonObject(subject) ? field(subject, 'operation') : null),
      typeof label === 'string' ? label : '',
      detail.join(' '),
    ]);
  },
  kindSelectorMatches: (descriptor, selector) => {
    const [kind = '', operation = ''] = selector.split('/');
    if (field(descriptor, 'kind') !== kind) return false;
    if (operation === '') return true;
    const subject = field(descriptor, 'subject');
    const found = isJsonObject(subject) ? field(subject, 'operation') : null;
    if (found === operation) return true;
    return found === null && (kind.split('.').at(-1) ?? kind) === operation;
  },
};

const UNIT_ONE = `unit_${'a'.repeat(32)}`;
const UNIT_TWO = `unit_${'b'.repeat(32)}`;

const compactsOf = (state: V2ClientState, actorId: string): ReadonlyArray<JsonObject> =>
  cachedActorCatalog(state, actorId).map((descriptor) => compactLegalAction(descriptor));

const resultOf = (
  state: V2ClientState,
  actorId: string,
  overrides: Partial<CompactLegalResult> = {}
): CompactLegalResult => ({
  state_revision: state.last_revision ?? { turn: 0, revision: 0, state_token: 'none' },
  offset: 0,
  truncated: false,
  byte_limited: false,
  actions: compactsOf(state, actorId),
  ...overrides,
});

const scopeOf = (actorId: string): PageScope => ({ actor_id: actorId, actor_type: 'unit' });

/** Two units offered exactly the same menu at one revision. */
const twoIdenticalActors = (fx: Fixture): { state: V2ClientState; aliases: AliasMap } => {
  const at = revision(7);
  const menu = (actor: string, prefix: string): ReadonlyArray<JsonObject> => [
    actorAction(at, `action_${prefix}1`.padEnd(24, 'z'), actor, { x: 31, y: 72 }),
    actorAction(at, `action_${prefix}2`.padEnd(24, 'z'), actor, {
      x: 32,
      y: 72,
      kind: 'unit.sentry',
      operation: 'sentry',
      label: 'Sentry',
    }),
  ];
  ingest(fx, scopedLegalPage(at, menu(UNIT_ONE, 'a'), UNIT_ONE, `catalog_${'a'.repeat(32)}`));
  const state = ingest(
    fx,
    scopedLegalPage(at, menu(UNIT_TWO, 'b'), UNIT_TWO, `catalog_${'b'.repeat(32)}`)
  );
  return { state, aliases: ok(fx.run(aliasMap(state))) };
};

// ---------------------------------------------------------------------------

describe('promotedCatalogPage', () => {
  test('a still-staged page is projected exactly as it arrived', () => {
    const fx = fixture();
    const at = revision(7);
    const page = ok(
      fx.run(
        Effect.mapError(
          decodeLegalPage(
            scopedLegalPage(at, [actorAction(at, 'action_one', UNIT_ONE)], UNIT_ONE, `catalog_${'a'.repeat(32)}`),
            fx.session
          ),
          (error) => ({ message: error.message })
        )
      )
    );
    expect(promotedCatalogPage(page, null)).toBe(page);
  });

  test('a promoting page widens only the item list', () => {
    const fx = fixture();
    const at = revision(7);
    const page: LegalActionPageEnvelope = ok(
      fx.run(
        Effect.mapError(
          decodeLegalPage(
            scopedLegalPage(at, [actorAction(at, 'action_one', UNIT_ONE)], UNIT_ONE, `catalog_${'a'.repeat(32)}`),
            fx.session
          ),
          (error) => ({ message: error.message })
        )
      )
    );
    const extra = [...page.page.items, ...page.page.items];
    const projected = promotedCatalogPage(page, extra);
    expect(projected.page.items).toHaveLength(2);
    expect(projected.state_revision).toEqual(page.state_revision);
    expect(projected.page.total_items).toBe(page.page.total_items);
    expect(projected.page.catalog_id).toBe(page.page.catalog_id);
    // The original is untouched — the mirror gets a projection, not a mutation.
    expect(page.page.items).toHaveLength(1);
  });
});

describe('cachedDescriptors and cachedActorCatalog', () => {
  test('only descriptors at the newest revision are still held', () => {
    const fx = fixture();
    const { state } = twoIdenticalActors(fx);
    expect(cachedDescriptors(state)).toHaveLength(4);
    expect(cachedActorCatalog(state, UNIT_ONE)).toHaveLength(2);
    expect(cachedActorCatalog(state, `unit_${'f'.repeat(32)}`)).toHaveLength(0);
  });

  test('a revision bump empties the cache the descriptors lived in', () => {
    const fx = fixture();
    const { state } = twoIdenticalActors(fx);
    expect(cachedDescriptors(state)).toHaveLength(4);
    const at = revision(9);
    const bumped = ingest(
      fx,
      scopedLegalPage(at, [actorAction(at, 'action_new', UNIT_ONE)], UNIT_ONE, `catalog_${'c'.repeat(32)}`)
    );
    expect(cachedDescriptors(bumped)).toHaveLength(1);
    expect(bumped.drained_actors).toEqual([UNIT_ONE]);
  });
});

describe('kindList', () => {
  test('it stops at V2_KIND_LIST_MAX and says so', () => {
    expect(kindList([])).toBe('');
    expect(kindList(['unit.order', 'unit.sentry'])).toBe('unit.order unit.sentry');
    const many = Array.from({ length: V2_KIND_LIST_MAX }, (_value, index) => `kind.k${index}`);
    expect(kindList(many).endsWith('…')).toBe(false);
    expect(kindList([...many, 'kind.extra']).endsWith(' …')).toBe(true);
    expect(kindList([...many, 'kind.extra']).split(' ')).toHaveLength(V2_KIND_LIST_MAX + 1);
  });
});

describe('cachedKindScopes', () => {
  test('it names the other actor that already offers the kind, by alias', () => {
    const fx = fixture();
    const { state } = twoIdenticalActors(fx);
    const found = ok(
      fx.run(cachedKindScopes(state, 'unit.sentry', UNIT_ONE, deps.kindSelectorMatches))
    );
    // The scope that was just searched is never named back.
    expect(found).toEqual(['u2']);
  });

  test('an operation-qualified selector picks exactly the row it was copied from', () => {
    const fx = fixture();
    const { state } = twoIdenticalActors(fx);
    expect(
      ok(fx.run(cachedKindScopes(state, 'unit.order/move', '', deps.kindSelectorMatches)))
    ).toEqual(['u1', 'u2']);
    expect(
      ok(fx.run(cachedKindScopes(state, 'unit.order/fortify', '', deps.kindSelectorMatches)))
    ).toEqual([]);
  });

  test('an actor is named once however many rows it matched', () => {
    const fx = fixture();
    const at = revision(7);
    const state = ingest(
      fx,
      scopedLegalPage(
        at,
        [
          actorAction(at, 'action_one', UNIT_ONE, { x: 1, y: 1 }),
          actorAction(at, 'action_two', UNIT_ONE, { x: 2, y: 2 }),
        ],
        UNIT_ONE,
        `catalog_${'a'.repeat(32)}`
      )
    );
    expect(ok(fx.run(cachedKindScopes(state, 'unit.order', '', deps.kindSelectorMatches)))).toEqual([
      'u1',
    ]);
  });
});

describe('catalogSignature', () => {
  test('it signs by choice offered and by rendered row', () => {
    const fx = fixture();
    const { state, aliases } = twoIdenticalActors(fx);
    const signature = ok(
      fx.run(catalogSignature(compactsOf(state, UNIT_ONE), scopeOf(UNIT_ONE), aliases, deps))
    );
    expect(signature.choices).toEqual([
      ['unit.order/move', 'T(31,72)'],
      ['unit.sentry', 'T(32,72)'],
    ]);
    // The row signature is cells 1..3 — kind, label, detail — never the handle.
    expect(signature.rows[0]).toEqual(['unit.order/move', 'Move', 'T(31,72)']);
    for (const row of signature.rows) {
      for (const cell of row) expect(cell).not.toContain('action_');
    }
  });
});

describe('catalogEquivalence', () => {
  test('two units offering the same menu are reported as equivalent', () => {
    const fx = fixture();
    const { state, aliases } = twoIdenticalActors(fx);
    const found = ok(
      fx.run(catalogEquivalence(state, resultOf(state, UNIT_ONE), scopeOf(UNIT_ONE), aliases, deps))
    );
    expect(found?.actorId).toBe(UNIT_TWO);
    // Same options, same order, same rows: nothing differs.
    expect(found?.differing).toEqual([]);
  });

  test('a differing row is reported even when the choices match', () => {
    const fx = fixture();
    const at = revision(7);
    ingest(
      fx,
      scopedLegalPage(
        at,
        [actorAction(at, 'action_a1'.padEnd(24, 'z'), UNIT_ONE, { x: 31, y: 72, label: 'Move' })],
        UNIT_ONE,
        `catalog_${'a'.repeat(32)}`
      )
    );
    const state = ingest(
      fx,
      scopedLegalPage(
        at,
        [
          actorAction(at, 'action_b1'.padEnd(24, 'z'), UNIT_TWO, {
            x: 31,
            y: 72,
            label: 'Move (last)',
          }),
        ],
        UNIT_TWO,
        `catalog_${'b'.repeat(32)}`
      )
    );
    const aliases = ok(fx.run(aliasMap(state)));
    const found = ok(
      fx.run(catalogEquivalence(state, resultOf(state, UNIT_ONE), scopeOf(UNIT_ONE), aliases, deps))
    );
    expect(found?.actorId).toBe(UNIT_TWO);
    expect(found?.differing).toHaveLength(1);
  });

  test('a differently ordered menu is not the same menu', () => {
    const fx = fixture();
    const at = revision(7);
    const move = (actor: string, prefix: string): JsonObject =>
      actorAction(at, `action_${prefix}move`.padEnd(24, 'z'), actor, { x: 31, y: 72 });
    const sentry = (actor: string, prefix: string): JsonObject =>
      actorAction(at, `action_${prefix}sentry`.padEnd(24, 'z'), actor, {
        x: 32,
        y: 72,
        kind: 'unit.sentry',
        operation: 'sentry',
        label: 'Sentry',
      });
    ingest(
      fx,
      scopedLegalPage(at, [move(UNIT_ONE, 'a'), sentry(UNIT_ONE, 'a')], UNIT_ONE, `catalog_${'a'.repeat(32)}`)
    );
    const state = ingest(
      fx,
      scopedLegalPage(at, [sentry(UNIT_TWO, 'b'), move(UNIT_TWO, 'b')], UNIT_TWO, `catalog_${'b'.repeat(32)}`)
    );
    const aliases = ok(fx.run(aliasMap(state)));
    expect(
      ok(fx.run(catalogEquivalence(state, resultOf(state, UNIT_ONE), scopeOf(UNIT_ONE), aliases, deps)))
    ).toBeNull();
  });

  const rejected: ReadonlyArray<readonly [string, Partial<CompactLegalResult>]> = [
    ['a truncated window proves nothing', { truncated: true }],
    ['an offset window is not the whole catalog', { offset: 16 }],
    ['a byte-limited window is not the whole catalog', { byte_limited: true }],
  ];
  for (const [name, overrides] of rejected) {
    test(name, () => {
      const fx = fixture();
      const { state, aliases } = twoIdenticalActors(fx);
      expect(
        ok(
          fx.run(
            catalogEquivalence(
              state,
              resultOf(state, UNIT_ONE, overrides),
              scopeOf(UNIT_ONE),
              aliases,
              deps
            )
          )
        )
      ).toBeNull();
    });
  }

  test('a target-scoped question is narrower and is never deduped', () => {
    const fx = fixture();
    const { state, aliases } = twoIdenticalActors(fx);
    const scope: PageScope = {
      actor_id: UNIT_ONE,
      actor_type: 'unit',
      target_id: `tile_${'0'.repeat(32)}`,
      target_type: 'tile',
    };
    expect(
      ok(fx.run(catalogEquivalence(state, resultOf(state, UNIT_ONE), scope, aliases, deps)))
    ).toBeNull();
  });

  test('a comparison never crosses a revision', () => {
    const fx = fixture();
    const { state, aliases } = twoIdenticalActors(fx);
    const stale = resultOf(state, UNIT_ONE, {
      state_revision: { turn: 3, revision: 5, state_token: 'state_old' },
    });
    expect(ok(fx.run(catalogEquivalence(state, stale, scopeOf(UNIT_ONE), aliases, deps)))).toBeNull();
  });

  test('an actor whose catalog was never drained whole is not eligible', () => {
    const fx = fixture();
    const { state, aliases } = twoIdenticalActors(fx);
    const notDrained: V2ClientState = { ...state, drained_actors: [UNIT_TWO] };
    expect(
      ok(
        fx.run(
          catalogEquivalence(
            notDrained,
            resultOf(state, UNIT_ONE),
            scopeOf(UNIT_ONE),
            aliases,
            deps
          )
        )
      )
    ).toBeNull();
  });

  test('a scope-less result has no actor to compare', () => {
    const fx = fixture();
    const { state, aliases } = twoIdenticalActors(fx);
    expect(
      ok(fx.run(catalogEquivalence(state, resultOf(state, UNIT_ONE), null, aliases, deps)))
    ).toBeNull();
  });

  test('catalogs of different sizes are never equivalent', () => {
    const fx = fixture();
    const at = revision(7);
    ingest(
      fx,
      scopedLegalPage(
        at,
        [
          actorAction(at, 'action_a1'.padEnd(24, 'z'), UNIT_ONE, { x: 31, y: 72 }),
          actorAction(at, 'action_a2'.padEnd(24, 'z'), UNIT_ONE, { x: 32, y: 72 }),
        ],
        UNIT_ONE,
        `catalog_${'a'.repeat(32)}`
      )
    );
    const state = ingest(
      fx,
      scopedLegalPage(
        at,
        [actorAction(at, 'action_b1'.padEnd(24, 'z'), UNIT_TWO, { x: 31, y: 72 })],
        UNIT_TWO,
        `catalog_${'b'.repeat(32)}`
      )
    );
    const aliases = ok(fx.run(aliasMap(state)));
    expect(
      ok(fx.run(catalogEquivalence(state, resultOf(state, UNIT_ONE), scopeOf(UNIT_ONE), aliases, deps)))
    ).toBeNull();
  });
});
