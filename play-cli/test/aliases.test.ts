/**
 * The `.v2-state` alias tables.
 *
 * Ports `test_v2_aliases_are_assigned_once_in_first_seen_order`,
 * `test_v2_action_aliases_die_with_their_revision` and
 * `test_v2_alias_tables_fail_closed_on_private_cache_drift` from
 * `play/tests/test_client.py`, plus a boundary case for every `V2_MAX_*` cap and
 * a property run over N revisions.
 *
 * The two lifetimes are the whole point and are asserted against each other
 * everywhere: an action alias dies with its revision, an entity alias does not.
 */
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either, Layer } from 'effect';
import {
  ACTION_ALIAS_RE,
  FULL_CONTROL_V2,
  V2_MAX_ACTION_ALIASES,
  V2_MAX_ALIAS_SEMANTICS,
  V2_MAX_DRAINED_ACTORS,
  V2_MAX_ENTITY_ALIASES,
  V2_MAX_PENDING_CATALOGS,
  V2_MAX_TILE_ALIASES,
} from 'src/constants';
import { decodeLegalPage, decodePage } from 'src/schema/page';
import { decodeReceipt } from 'src/schema/receipt';
import type { JsonObject, JsonValue } from 'src/schema/primitives';
import { isJsonObject } from 'src/schema/primitives';
import { PrivateFs } from 'src/services/private-fs';
import {
  SessionStore,
  sessionStoreFor,
  type Session,
  type SessionStoreApi,
  type V2ClientState,
} from 'src/services/session-store';
import {
  actionSemantics,
  aliasMap,
  entityAliasPrefix,
  freshActionAliases,
  jsonEquals,
  parseDrainedActors,
  rebindActionAliases,
  rememberDrainedActor,
  rememberPage,
  rememberReceipt,
  tileReference,
  v2StateSchema,
  type ActionAliasEntry,
} from 'src/services/aliases';
import { closestAliases, expandAlias } from 'src/services/alias-expand';
import {
  cursorExpired,
  dropPendingForCursor,
  dropPendingForScope,
  validatePendingCatalogs,
} from 'src/services/pending-catalogs';
import {
  FIXTURE_GAME_ID,
  identity,
  scratchWorkspace,
  sessionFile,
  type Scratch,
} from 'test/_fixtures';

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

const scratches: Scratch[] = [];
afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

interface Fixture {
  readonly scratch: Scratch;
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
    scratch,
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

const failure = <A>(either: Either.Either<A, { readonly message: string }>): string => {
  expect(Either.isLeft(either)).toBe(true);
  return Either.isLeft(either) ? either.left.message : '';
};

// ---------------------------------------------------------------------------
// Wire builders — the same shapes `test_client.py` hands the validators
// ---------------------------------------------------------------------------

/** Assignable both to a wire `JsonObject` and to the decoded `Revision`. */
interface TestRevision {
  readonly turn: number;
  readonly revision: number;
  readonly state_token: string;
  readonly [key: string]: JsonValue;
}

const revision = (number = 7, turn = 3): TestRevision => ({
  turn,
  revision: number,
  state_token: `state_${String(number).padStart(32, '0')}`,
});

const envelope = (body: JsonObject, stateRevision: TestRevision): JsonObject => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  game_id: FIXTURE_GAME_ID,
  agent_id: 'agent_0123456789abcdef',
  state_revision: stateRevision,
  page: body,
});

const sectionPage = (
  section: string,
  stateRevision: TestRevision,
  items: ReadonlyArray<JsonValue>,
  cursor: string | null = null
): JsonObject =>
  envelope(
    {
      section,
      items,
      total_items: items.length + (cursor === null ? 0 : 1),
      next_cursor: cursor,
    },
    stateRevision
  );

const scopedLegalPage = (
  stateRevision: TestRevision,
  items: ReadonlyArray<JsonValue>,
  actorId: string,
  options: { catalog?: string; cursor?: string | null; total?: number } = {}
): JsonObject => {
  const cursor = options.cursor ?? null;
  return envelope(
    {
      section: 'legal_actions',
      items,
      total_items: options.total ?? items.length + (cursor === null ? 0 : 1),
      next_cursor: cursor,
      cursor_expires_at: cursor === null ? null : '2999-01-01T00:00:00.000Z',
      scope: { actor_id: actorId, actor_type: actorId.split('_')[0] ?? 'unit' },
      catalog_id: options.catalog ?? `catalog_${'e'.repeat(32)}`,
      catalog_complete: cursor === null,
    },
    stateRevision
  );
};

const descriptor = (stateRevision: TestRevision, actionId = 'action_opaque'): JsonObject => ({
  action_id: actionId,
  kind: 'phase.end',
  label: 'End phase',
  subject: { operation: 'end' },
  arguments_schema: { type: 'object' },
  state_revision: stateRevision,
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
      probability: { kind: 'exact', minimum_percent: 100, maximum_percent: 100 },
    },
    arguments_schema: { type: 'object' },
    state_revision: stateRevision,
  };
};

const unitItem = (identifier: string, tile: string, x: number, y: number): JsonObject => ({
  id: identifier,
  scope: 'own',
  type: 'Settlers',
  tile_id: tile,
  x,
  y,
  hp: 20,
  moves: 3,
  type_stats: { max_hp: 20, move_rate: 3 },
  activity: { name: 'idle' },
  automation: { controller: 'player', has_orders: false },
  route: null,
});

const receiptPayloadFor = (
  batchId: string,
  state: string,
  stateRevision: TestRevision
): JsonObject => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  game_id: FIXTURE_GAME_ID,
  agent_id: 'agent_0123456789abcdef',
  batch_id: batchId,
  receipt_state: state,
  idempotent: false,
  state_revision: stateRevision,
  error:
    state === 'rejected' || state === 'ambiguous'
      ? {
          schema_version: 2,
          control_protocol: FULL_CONTROL_V2,
          error: {
            code: state === 'ambiguous' ? 'action_outcome_ambiguous' : 'illegal_action',
            message: 'no',
            retryable: false,
            details: {},
          },
          state_revision: stateRevision,
        }
      : null,
  observation: null,
});

const ingestState = (
  fx: Fixture,
  page: JsonObject
): Either.Either<{ readonly state: V2ClientState }, { readonly message: string }> =>
  fx.run(
    Effect.flatMap(
      Effect.mapError(decodePage(page, fx.session), (error) => ({ message: error.message })),
      (decoded) => rememberPage(fx.sessionPath, fx.session, { legal: false, page: decoded })
    )
  );

const ingestLegal = (
  fx: Fixture,
  page: JsonObject
): Either.Either<
  { readonly state: V2ClientState; readonly promoted: ReadonlyArray<unknown> | null },
  { readonly message: string }
> =>
  fx.run(
    Effect.flatMap(
      Effect.mapError(decodeLegalPage(page, fx.session), (error) => ({ message: error.message })),
      (decoded) => rememberPage(fx.sessionPath, fx.session, { legal: true, page: decoded })
    )
  );

const aliasEntries = (state: V2ClientState): Record<string, string> => {
  const table = state.action_aliases['by_alias'];
  const mapped: Record<string, string> = {};
  if (!isJsonObject(table)) return mapped;
  for (const [alias, entry] of Object.entries(table)) {
    if (isJsonObject(entry) && typeof entry['action_id'] === 'string') {
      mapped[alias] = entry['action_id'];
    }
  }
  return mapped;
};

const TILES = ['a', 'b', 'c'].map((character) => `tile_${character.repeat(32)}`);
const UNITS = ['a', 'b', 'c'].map((character) => `unit_${character.repeat(32)}`);
const CITY = `city_${'d'.repeat(32)}`;
const ACTOR = `unit_${'a'.repeat(32)}`;

// ---------------------------------------------------------------------------

describe('entity and tile aliases are assigned once, in first-seen order', () => {
  test('a second page continues the count instead of restarting at u1', () => {
    const fx = fixture();
    const first = revision(7);
    const second = revision(9);
    ok(
      ingestState(
        fx,
        sectionPage(
          'units',
          first,
          [
            unitItem(UNITS[0] ?? '', TILES[0] ?? '', 31, 72),
            unitItem(UNITS[1] ?? '', TILES[1] ?? '', 30, 72),
          ],
          `cursor_${'1'.repeat(32)}`
        )
      )
    );
    ok(
      ingestState(
        fx,
        sectionPage('units', first, [unitItem(UNITS[2] ?? '', TILES[2] ?? '', 29, 72)])
      )
    );
    const after = ok(
      ingestState(
        fx,
        sectionPage('cities', second, [
          {
            id: CITY,
            name: 'London',
            x: 31,
            y: 72,
            size: 1,
            tile_id: TILES[0] ?? '',
            surplus: { food: 2 },
            production: { kind: 'unit', name: 'Warriors' },
          },
        ])
      )
    ).state;

    expect(after.entity_aliases).toEqual({
      u1: UNITS[0] ?? '',
      u2: UNITS[1] ?? '',
      u3: UNITS[2] ?? '',
      c1: CITY,
    });
    expect(after.tile_aliases).toEqual({
      '31,72': TILES[0] ?? '',
      '30,72': TILES[1] ?? '',
      '29,72': TILES[2] ?? '',
    });
    // Entity aliases are game-stable: the revision bump that wiped the action
    // cache left them untouched.
    expect(after.last_revision).toEqual({ turn: 3, revision: 9, state_token: revision(9)['state_token'] });
    expect(after.actions).toEqual({});

    // Re-reading the same unit at the newer revision re-points nothing.
    const again = ok(
      ingestState(
        fx,
        sectionPage('units', second, [unitItem(UNITS[1] ?? '', TILES[1] ?? '', 30, 72)])
      )
    ).state;
    expect(again.entity_aliases['u2']).toBe(UNITS[1] ?? '');
    expect(Object.keys(again.entity_aliases)).toHaveLength(4);
  });

  test('the alias map addresses units, cities and tiles at once', () => {
    const fx = fixture();
    ok(
      ingestState(
        fx,
        sectionPage('units', revision(7), [unitItem(UNITS[0] ?? '', TILES[0] ?? '', 31, 72)])
      )
    );
    const state = ok(fx.run(fx.store.readState(fx.sessionPath, fx.session)));
    const map = ok(fx.run(aliasMap(state)));
    expect(map[UNITS[0] ?? '']).toBe('u1');
    expect(map[TILES[0] ?? '']).toBe('T(31,72)');
  });

  test('a tile ID that changed under a coordinate drops rather than lies', () => {
    const fx = fixture();
    ok(
      ingestState(
        fx,
        sectionPage('units', revision(7), [unitItem(UNITS[0] ?? '', TILES[0] ?? '', 31, 72)])
      )
    );
    const state = ok(
      ingestState(
        fx,
        sectionPage('units', revision(7), [unitItem(UNITS[1] ?? '', TILES[1] ?? '', 31, 72)])
      )
    ).state;
    expect(state.tile_aliases['31,72']).toBeUndefined();
  });
});

describe('action aliases die with their revision', () => {
  test('a1 fails closed after a bump and only re-enumeration re-uses it', () => {
    const fx = fixture();
    const first = revision(7);
    const second = revision(9);
    const oldOne = descriptor(first, `action_${'1'.repeat(32)}`);
    const oldTwo = descriptor(first, `action_${'2'.repeat(32)}`);
    const newOne = descriptor(second, `action_${'9'.repeat(32)}`);

    const staged = ok(ingestLegal(fx, scopedLegalPage(first, [oldOne, oldTwo], ACTOR))).state;
    expect(aliasEntries(staged)).toEqual({
      a1: oldOne['action_id'] as string,
      a2: oldTwo['action_id'] as string,
    });
    expect(ok(fx.run(expandAlias(staged, 'a2', fx.sessionPath)))).toBe(
      oldTwo['action_id'] as string
    );

    // The agent's own action bumps the revision.  The bucket still names the
    // revision it came from, so a1 fails closed instead of re-pointing.
    const bumped = ok(ingestState(fx, sectionPage('overview', second, []))).state;
    expect(ok(fx.run(freshActionAliases(bumped)))).toEqual({});
    const message = failure(fx.run(expandAlias(bumped, 'a1', fx.sessionPath)));
    expect(message).toContain('rev7/t3');
    expect(message).toContain('rev9/t3');
    expect(message).toContain('die with their revision');
    // The remedy is bare: this workspace resolves its sole session by itself.
    expect(message).toContain('`just legal --actor_id ');
    expect(message).toContain(' --all`');
    expect(message).not.toContain('--session');

    const reenumerated = ok(
      ingestLegal(
        fx,
        scopedLegalPage(second, [newOne], ACTOR, { catalog: `catalog_${'f'.repeat(32)}` })
      )
    ).state;
    expect(ok(fx.run(expandAlias(reenumerated, 'a1', fx.sessionPath)))).toBe(
      newOne['action_id'] as string
    );
    expect(failure(fx.run(expandAlias(reenumerated, 'a2', fx.sessionPath)))).toContain(
      'unknown action alias a2'
    );
    const reloaded = ok(fx.run(fx.store.readState(fx.sessionPath, fx.session)));
    expect(reloaded.action_aliases['state_revision']).toEqual(second);
  });

  test('an alias that was never assigned is a typo, whatever the bucket age', () => {
    const fx = fixture();
    const cold = ok(fx.run(fx.store.readState(fx.sessionPath, fx.session)));
    expect(failure(fx.run(expandAlias(cold, 'a3', fx.sessionPath)))).toContain(
      'no legal-action catalog has been read yet'
    );

    const first = revision(7);
    const staged = ok(
      ingestLegal(fx, scopedLegalPage(first, [descriptor(first, `action_${'1'.repeat(32)}`)], ACTOR))
    ).state;
    expect(failure(fx.run(expandAlias(staged, 'a9', fx.sessionPath)))).toBe(
      'unknown action alias a9; this revision enumerated a1'
    );

    const bumped = ok(ingestState(fx, sectionPage('overview', revision(9), []))).state;
    const stale = failure(fx.run(expandAlias(bumped, 'a9', fx.sessionPath)));
    expect(stale).toContain('it was never enumerated, and the aliases that were (a1) died with');
    expect(stale).toContain('rev7/t3');
  });
});

describe('the alias vocabulary', () => {
  test('closest aliases are numbered neighbours, capped at eight', () => {
    expect(closestAliases([], 'a4')).toBe('none are known yet');
    expect(closestAliases(['a1', 'a2', 'a3'], 'a2')).toBe('a1 a2 a3');
    const many = Array.from({ length: 12 }, (_value, index) => `a${index + 1}`);
    const shown = closestAliases(many, 'a6');
    expect(shown.endsWith(' …')).toBe(true);
    expect(shown.slice(0, -2).split(' ')).toHaveLength(8);
    // The tie at distance 4 is broken by the alias *string*, so `a10` beats
    // `a2` — exactly what CPython's `(distance, alias)` sort key does.
    expect(shown).toBe('a3 a4 a5 a6 a7 a8 a9 a10 …');
  });

  test('an unknown entity alias names its own kind and its neighbours', () => {
    const fx = fixture();
    ok(
      ingestState(
        fx,
        sectionPage('units', revision(7), [unitItem(UNITS[0] ?? '', TILES[0] ?? '', 31, 72)])
      )
    );
    const state = ok(fx.run(fx.store.readState(fx.sessionPath, fx.session)));
    expect(failure(fx.run(expandAlias(state, 'u7', fx.sessionPath)))).toBe(
      'unknown unit alias u7; known unit aliases: u1'
    );
    expect(failure(fx.run(expandAlias(state, 'c1', fx.sessionPath)))).toBe(
      'unknown city alias c1; known city aliases: none are known yet'
    );
  });

  test('an uncached tile names the nearest six by manhattan distance', () => {
    const fx = fixture();
    ok(
      ingestState(
        fx,
        sectionPage('units', revision(7), [
          unitItem(UNITS[0] ?? '', TILES[0] ?? '', 31, 72),
          unitItem(UNITS[1] ?? '', TILES[1] ?? '', 30, 72),
        ])
      )
    );
    const state = ok(fx.run(fx.store.readState(fx.sessionPath, fx.session)));
    const message = failure(fx.run(expandAlias(state, 'T(40,72)', fx.sessionPath)));
    expect(message).toBe(
      'unknown tile T(40,72): no page this seat has read named that coordinate. ' +
        'Nearest cached tiles: T(31,72) T(30,72)'
    );
    const empty = fixture();
    const cold = ok(empty.run(empty.store.readState(empty.sessionPath, empty.session)));
    expect(failure(empty.run(expandAlias(cold, 'T(0,0)', empty.sessionPath)))).toContain(
      'none are cached yet'
    );
  });

  test('text that is not alias-shaped passes straight through', () => {
    const fx = fixture();
    const state = ok(fx.run(fx.store.readState(fx.sessionPath, fx.session)));
    expect(ok(fx.run(expandAlias(state, ACTOR, fx.sessionPath)))).toBe(ACTOR);
    expect(ok(fx.run(expandAlias(state, '', fx.sessionPath)))).toBe('');
  });

  test('an entity alias prefix is only offered for its own ID shape', () => {
    expect(entityAliasPrefix(ACTOR)).toBe('u');
    expect(entityAliasPrefix(CITY)).toBe('c');
    expect(entityAliasPrefix(`relation_${'a'.repeat(32)}`)).toBe('r');
    expect(entityAliasPrefix(`unit_short`)).toBeNull();
    expect(entityAliasPrefix(TILES[0] ?? '')).toBeNull();
    expect(entityAliasPrefix(7)).toBeNull();
  });

  test('a tile reference needs an ID and two integer coordinates', () => {
    expect(tileReference({ id: TILES[0] ?? '', x: 3, y: 4 }, 'id')).toEqual({
      identifier: TILES[0] ?? '',
      x: 3,
      y: 4,
    });
    expect(tileReference({ id: TILES[0] ?? '', x: 3.5, y: 4 }, 'id')).toBeNull();
    expect(tileReference({ id: TILES[0] ?? '', x: true, y: 4 }, 'id')).toBeNull();
    expect(tileReference({ id: ACTOR, x: 3, y: 4 }, 'id')).toBeNull();
  });
});

describe('the private tables fail closed on cache drift', () => {
  const broken: ReadonlyArray<readonly [string, JsonObject]> = [
    ['a unit alias that names no ID', { entity_aliases: { u1: 'not-an-id' } }],
    ['two aliases for one entity', { entity_aliases: { u1: ACTOR, u2: ACTOR } }],
    ['an alias outside the dialect', { entity_aliases: { x1: ACTOR } }],
    ['a tile alias that names a unit', { tile_aliases: { '31,72': ACTOR } }],
    [
      'an entry with no semantics field',
      {
        action_aliases: {
          state_revision: null,
          by_alias: { a1: { action_id: 'action_x', actor_id: '' } },
        },
      },
    ],
    [
      'an entry that is not an object',
      { action_aliases: { state_revision: revision(7), by_alias: { a1: 'action_x' } } },
    ],
    [
      'a numbered alias with no revision behind it',
      {
        action_aliases: {
          state_revision: null,
          by_alias: { a1: { action_id: 'action_x', actor_id: '', semantics: '' } },
        },
      },
    ],
    [
      'two aliases for one action handle',
      {
        action_aliases: {
          state_revision: revision(7),
          by_alias: {
            a1: { action_id: 'action_x', actor_id: '', semantics: 'one' },
            a2: { action_id: 'action_x', actor_id: '', semantics: 'two' },
          },
        },
      },
    ],
  ];

  for (const [name, patch] of broken) {
    test(name, () => {
      const fx = fixture();
      const empty = v2StateSchema.empty(fx.session);
      Effect.runSync(
        fx.scratch.files.writeJson(fx.store.statePath(fx.sessionPath), { ...empty, ...patch })
      );
      expect(failure(fx.run(fx.store.readState(fx.sessionPath, fx.session)))).toContain(
        'aliases are invalid'
      );
    });
  }

  // CPython calls `_validate_revision` / `_validate_cursor_expiry` /
  // `_validate_descriptor` UNWRAPPED from `_validate_alias_state` (client.py:1655)
  // and `_validate_pending_catalogs` (1573, 1615, 1620), so a drifted *field*
  // gets named instead of the table's generic sentence.  Every string below was
  // read off `python3 client.py`'s own validators on the same input.
  describe('a drifted field is named, not swallowed by the generic sentence', () => {
    const REVISION_DRIFT =
      'invalid state revision: missing state_token. ' +
      'Expected exactly revision, state_token, turn';
    const DESCRIPTOR_DRIFT =
      'invalid legal action descriptor: missing action_id, arguments_schema, kind, ' +
      'label, state_revision, subject; unexpected nope. Expected exactly action_id, ' +
      'arguments_schema, kind, label, state_revision, subject';

    const catalogId = `catalog_${'a'.repeat(32)}`;
    const staged = (patch: Record<string, JsonValue>): JsonValue => {
      const stateRevision = revision(7);
      return {
        [catalogId]: {
          state_revision: stateRevision,
          scope: { actor_id: ACTOR, actor_type: 'unit' },
          total_items: 4,
          items: { action_one: descriptor(stateRevision, 'action_one') },
          next_cursor: `cursor_${'1'.repeat(32)}`,
          cursor_expires_at: null,
          ...patch,
        },
      };
    };

    test('the control fixture is accepted, so every refusal below is the patch', () => {
      expect(
        Either.isRight(Effect.runSync(Effect.either(validatePendingCatalogs(staged({})))))
      ).toBe(true);
    });

    test('an action-alias revision missing a field prints the revision sentence', () => {
      const fx = fixture();
      const empty = v2StateSchema.empty(fx.session);
      Effect.runSync(
        fx.scratch.files.writeJson(fx.store.statePath(fx.sessionPath), {
          ...empty,
          action_aliases: { state_revision: { turn: 3, revision: 7 }, by_alias: {} },
        })
      );
      expect(failure(fx.run(fx.store.readState(fx.sessionPath, fx.session)))).toBe(REVISION_DRIFT);
    });

    test('the same drift inside a staged catalog prints the same revision sentence', () => {
      expect(
        failure(
          Effect.runSync(
            Effect.either(validatePendingCatalogs(staged({ state_revision: { turn: 3, revision: 7 } })))
          )
        )
      ).toBe(REVISION_DRIFT);
    });

    test('a drifted staged descriptor prints the descriptor sentence', () => {
      expect(
        failure(
          Effect.runSync(
            Effect.either(validatePendingCatalogs(staged({ items: { action_one: { nope: 1 } } })))
          )
        )
      ).toBe(DESCRIPTOR_DRIFT);
    });

    test('an unparseable cursor expiry prints the cursor-expiry sentence', () => {
      expect(
        failure(
          Effect.runSync(
            Effect.either(validatePendingCatalogs(staged({ cursor_expires_at: 'not-a-timeZ' })))
          )
        )
      ).toBe('invalid v2 page cursor expiry');
    });

    // The generic sentences are still the answer for structural drift — the
    // field-level diagnosis only replaces them where CPython delegated.
    test('structural drift keeps the generic sentences', () => {
      expect(
        failure(Effect.runSync(Effect.either(validatePendingCatalogs(staged({ total_items: 0 })))))
      ).toBe('private v2 pending catalogs are invalid');
      const fx = fixture();
      const empty = v2StateSchema.empty(fx.session);
      Effect.runSync(
        fx.scratch.files.writeJson(fx.store.statePath(fx.sessionPath), {
          ...empty,
          action_aliases: { by_alias: {} },
        })
      );
      expect(failure(fx.run(fx.store.readState(fx.sessionPath, fx.session)))).toBe(
        'private v2 action aliases are invalid'
      );
    });
  });

  test('a drained-actor record that is not a list of actor IDs is refused', () => {
    expect(failure(Effect.runSync(Effect.either(parseDrainedActors([ACTOR, ACTOR]))))).toBe(
      'private v2 drained catalogs are invalid'
    );
    expect(failure(Effect.runSync(Effect.either(parseDrainedActors([TILES[0] ?? '']))))).toBe(
      'private v2 drained catalogs are invalid'
    );
    expect(ok(Effect.runSync(Effect.either(parseDrainedActors([ACTOR]))))).toEqual([ACTOR]);
  });
});

describe('every V2_MAX_* cap is a boundary, not a suggestion', () => {
  test('entity aliases stop at V2_MAX_ENTITY_ALIASES', () => {
    const fx = fixture();
    const full: Record<string, string> = {};
    for (let index = 1; index <= V2_MAX_ENTITY_ALIASES; index += 1) {
      full[`u${index}`] = `unit_${index.toString(16).padStart(32, '0')}`;
    }
    // The cap itself is legal…
    const empty = v2StateSchema.empty(fx.session);
    Effect.runSync(
      fx.scratch.files.writeJson(fx.store.statePath(fx.sessionPath), {
        ...empty,
        entity_aliases: full,
      })
    );
    expect(Object.keys(ok(fx.run(fx.store.readState(fx.sessionPath, fx.session))).entity_aliases))
      .toHaveLength(V2_MAX_ENTITY_ALIASES);
    // …one past it is not.
    full[`u${V2_MAX_ENTITY_ALIASES + 1}`] = `unit_${'f'.repeat(32)}`;
    Effect.runSync(
      fx.scratch.files.writeJson(fx.store.statePath(fx.sessionPath), {
        ...empty,
        entity_aliases: full,
      })
    );
    expect(failure(fx.run(fx.store.readState(fx.sessionPath, fx.session)))).toBe(
      'private v2 entity aliases are invalid'
    );
  });

  test('a full entity table learns nothing more from a page', () => {
    const fx = fixture();
    const full: Record<string, string> = {};
    for (let index = 1; index <= V2_MAX_ENTITY_ALIASES; index += 1) {
      full[`u${index}`] = `unit_${index.toString(16).padStart(32, '0')}`;
    }
    const empty = v2StateSchema.empty(fx.session);
    Effect.runSync(
      fx.scratch.files.writeJson(fx.store.statePath(fx.sessionPath), {
        ...empty,
        entity_aliases: full,
      })
    );
    const after = ok(
      ingestState(
        fx,
        sectionPage('units', revision(7), [unitItem(ACTOR, TILES[0] ?? '', 31, 72)])
      )
    ).state;
    expect(Object.keys(after.entity_aliases)).toHaveLength(V2_MAX_ENTITY_ALIASES);
    expect(Object.values(after.entity_aliases)).not.toContain(ACTOR);
    // The tile table was not full, so it still learned.
    expect(after.tile_aliases['31,72']).toBe(TILES[0] ?? '');
  });

  test('tile aliases stop at V2_MAX_TILE_ALIASES', () => {
    const fx = fixture();
    const full: Record<string, string> = {};
    for (let index = 0; index < V2_MAX_TILE_ALIASES; index += 1) {
      full[`${index % 9999},${Math.floor(index / 9999)}`] =
        `tile_${index.toString(16).padStart(32, '0')}`;
    }
    const empty = v2StateSchema.empty(fx.session);
    Effect.runSync(
      fx.scratch.files.writeJson(fx.store.statePath(fx.sessionPath), {
        ...empty,
        tile_aliases: full,
      })
    );
    expect(
      Object.keys(ok(fx.run(fx.store.readState(fx.sessionPath, fx.session))).tile_aliases)
    ).toHaveLength(V2_MAX_TILE_ALIASES);
    full['9998,9998'] = `tile_${'f'.repeat(32)}`;
    Effect.runSync(
      fx.scratch.files.writeJson(fx.store.statePath(fx.sessionPath), {
        ...empty,
        tile_aliases: full,
      })
    );
    expect(failure(fx.run(fx.store.readState(fx.sessionPath, fx.session)))).toBe(
      'private v2 tile aliases are invalid'
    );
  });

  test('a tile outside ±9999 is never cached', () => {
    const fx = fixture();
    const after = ok(
      ingestState(
        fx,
        sectionPage('units', revision(7), [unitItem(ACTOR, TILES[0] ?? '', 10000, 0)])
      )
    ).state;
    expect(after.tile_aliases).toEqual({});
  });

  test('action aliases stop at V2_MAX_ACTION_ALIASES', () => {
    const fx = fixture();
    const empty = v2StateSchema.empty(fx.session);
    const byAlias: Record<string, JsonValue> = {};
    for (let index = 1; index <= V2_MAX_ACTION_ALIASES; index += 1) {
      byAlias[`a${index}`] = {
        action_id: `action_${index.toString(16).padStart(24, '0')}`,
        actor_id: '',
        semantics: '',
      };
    }
    Effect.runSync(
      fx.scratch.files.writeJson(fx.store.statePath(fx.sessionPath), {
        ...empty,
        action_aliases: { state_revision: revision(7), by_alias: byAlias },
      })
    );
    expect(ok(fx.run(fx.store.readState(fx.sessionPath, fx.session)))).toBeTruthy();
    byAlias[`a${V2_MAX_ACTION_ALIASES + 1}`] = {
      action_id: 'action_overflow',
      actor_id: '',
      semantics: '',
    };
    Effect.runSync(
      fx.scratch.files.writeJson(fx.store.statePath(fx.sessionPath), {
        ...empty,
        action_aliases: { state_revision: revision(7), by_alias: byAlias },
      })
    );
    expect(failure(fx.run(fx.store.readState(fx.sessionPath, fx.session)))).toBe(
      'private v2 action aliases are invalid'
    );
  });

  test('semantics longer than V2_MAX_ALIAS_SEMANTICS are refused on load', () => {
    const fx = fixture();
    const empty = v2StateSchema.empty(fx.session);
    Effect.runSync(
      fx.scratch.files.writeJson(fx.store.statePath(fx.sessionPath), {
        ...empty,
        action_aliases: {
          state_revision: revision(7),
          by_alias: {
            a1: {
              action_id: 'action_x',
              actor_id: '',
              semantics: 'x'.repeat(V2_MAX_ALIAS_SEMANTICS + 1),
            },
          },
        },
      })
    );
    expect(failure(fx.run(fx.store.readState(fx.sessionPath, fx.session)))).toBe(
      'private v2 action aliases are invalid'
    );
  });

  test('a computed semantics string is truncated to the cap, never past it', () => {
    const stateRevision = revision(7);
    const properties: Record<string, JsonValue> = {};
    for (let index = 0; index < 400; index += 1) {
      properties[`property_${String(index).padStart(4, '0')}`] = { type: 'string' };
    }
    const wide: JsonObject = {
      action_id: 'action_wide',
      kind: 'unit.order',
      label: 'Move',
      subject: { operation: 'move', actor: { id: ACTOR }, target: null },
      arguments_schema: { type: 'object', properties },
      state_revision: stateRevision,
    };
    const decoded = Effect.runSync(
      Effect.orDie(
        Effect.map(
          decodeLegalPage(scopedLegalPage(stateRevision, [wide], ACTOR), identity()),
          (page) => page.page.items
        )
      )
    );
    const first = decoded[0];
    expect(first).toBeDefined();
    if (first === undefined) return;
    const semantics = Effect.runSync(Effect.orDie(actionSemantics(first)));
    expect(semantics.length).toBe(V2_MAX_ALIAS_SEMANTICS);
  });

  test('a semantics capped at 1024 code points reloads, even past 1024 UTF-16 units', () => {
    // CPython caps by `len()`, which counts code points.  A single non-BMP
    // character anywhere in the surviving prefix makes the UTF-16 measurement
    // read 1025 for a string CPython calls 1024 — so a port that measures with
    // `.length` writes a `.v2-state` its own loader then refuses, bricking
    // every v2 command until a human deletes the private cache.  This is the
    // exact page that catches it.
    const fx = fixture();
    const stateRevision = revision(7);
    const astral: JsonObject = {
      action_id: 'action_astral',
      kind: 'unit.order',
      label: 'Move',
      subject: {
        operation: 'move',
        actor: { id: ACTOR },
        // The astral character sits at the head of the target name, so it is
        // inside the first 1024 code points and survives truncation.
        target: { name: `\u{1F3DB}${'x'.repeat(2000)}` },
      },
      arguments_schema: { type: 'object' },
      state_revision: stateRevision,
    };
    const page = scopedLegalPage(stateRevision, [astral], ACTOR);

    const decoded = Effect.runSync(
      Effect.orDie(Effect.map(decodeLegalPage(page, fx.session), (each) => each.page.items))
    );
    const first = decoded[0];
    expect(first).toBeDefined();
    if (first === undefined) return;
    const semantics = Effect.runSync(Effect.orDie(actionSemantics(first)));
    // The producer's cap is code points; the divergence this test guards is
    // real only when the two measurements actually disagree.
    expect([...semantics].length).toBe(V2_MAX_ALIAS_SEMANTICS);
    expect(semantics.length).toBeGreaterThan(V2_MAX_ALIAS_SEMANTICS);

    // Ingest writes it, and the cold reload must accept what ingest just wrote.
    const ingested = ok(ingestLegal(fx, page));
    const stored = ingested.state.action_aliases['by_alias'];
    expect(isJsonObject(stored) && isJsonObject(stored['a1'])).toBe(true);
    const reloaded = ok(fx.run(fx.store.readState(fx.sessionPath, fx.session)));
    const table = reloaded.action_aliases['by_alias'];
    expect(isJsonObject(table)).toBe(true);
    if (!isJsonObject(table)) return;
    const entry = table['a1'];
    expect(isJsonObject(entry)).toBe(true);
    if (!isJsonObject(entry)) return;
    expect(entry['action_id']).toBe('action_astral');
    expect(entry['semantics']).toBe(semantics);
  });

  test('a semantics of 1024 code points hand-written into the cache reloads', () => {
    const fx = fixture();
    const empty = v2StateSchema.empty(fx.session);
    // 1024 code points exactly, 1025 UTF-16 units — the load must accept it.
    const semantics = `\u{1F3DB}${'x'.repeat(V2_MAX_ALIAS_SEMANTICS - 1)}`;
    expect([...semantics].length).toBe(V2_MAX_ALIAS_SEMANTICS);
    Effect.runSync(
      fx.scratch.files.writeJson(fx.store.statePath(fx.sessionPath), {
        ...empty,
        action_aliases: {
          state_revision: revision(7),
          by_alias: { a1: { action_id: 'action_x', actor_id: '', semantics } },
        },
      })
    );
    expect(ok(fx.run(fx.store.readState(fx.sessionPath, fx.session)))).toBeTruthy();
  });

  test('a semantics of 1025 code points is still refused, however it is encoded', () => {
    const fx = fixture();
    const empty = v2StateSchema.empty(fx.session);
    // 1025 code points, so over the cap by CPython's own measure.
    const semantics = `\u{1F3DB}${'x'.repeat(V2_MAX_ALIAS_SEMANTICS)}`;
    expect([...semantics].length).toBe(V2_MAX_ALIAS_SEMANTICS + 1);
    Effect.runSync(
      fx.scratch.files.writeJson(fx.store.statePath(fx.sessionPath), {
        ...empty,
        action_aliases: {
          state_revision: revision(7),
          by_alias: { a1: { action_id: 'action_x', actor_id: '', semantics } },
        },
      })
    );
    expect(failure(fx.run(fx.store.readState(fx.sessionPath, fx.session)))).toBe(
      'private v2 action aliases are invalid'
    );
  });

  test('drained actors stop at V2_MAX_DRAINED_ACTORS', () => {
    const fx = fixture();
    const empty = v2StateSchema.empty(fx.session);
    const drained = Array.from(
      { length: V2_MAX_DRAINED_ACTORS },
      (_value, index) => `unit_${index.toString(16).padStart(32, '0')}`
    );
    const full: V2ClientState = { ...empty, drained_actors: drained };
    expect(ok(Effect.runSync(Effect.either(rememberDrainedActor(full, ACTOR)))).drained_actors)
      .toHaveLength(V2_MAX_DRAINED_ACTORS);
    const nearlyFull: V2ClientState = { ...empty, drained_actors: drained.slice(0, -1) };
    expect(ok(Effect.runSync(Effect.either(rememberDrainedActor(nearlyFull, ACTOR)))).drained_actors)
      .toHaveLength(V2_MAX_DRAINED_ACTORS);
    // A record one past the cap never loads at all.
    Effect.runSync(
      fx.scratch.files.writeJson(fx.store.statePath(fx.sessionPath), {
        ...empty,
        drained_actors: [...drained, `unit_${'f'.repeat(32)}`],
      })
    );
    expect(failure(fx.run(fx.store.readState(fx.sessionPath, fx.session)))).toBe(
      'private v2 drained catalogs are invalid'
    );
  });

  test('pending catalogs stop at V2_MAX_PENDING_CATALOGS', () => {
    const staged: Record<string, JsonValue> = {};
    const stateRevision = revision(7);
    for (let index = 0; index <= V2_MAX_PENDING_CATALOGS; index += 1) {
      staged[`catalog_${index.toString(16).padStart(32, '0')}`] = {
        state_revision: stateRevision,
        scope: { actor_id: ACTOR, actor_type: 'unit' },
        total_items: 4,
        items: { action_one: descriptor(stateRevision, 'action_one') },
        next_cursor: `cursor_${'1'.repeat(32)}`,
        cursor_expires_at: null,
      };
    }
    expect(failure(Effect.runSync(Effect.either(validatePendingCatalogs(staged))))).toBe(
      'private v2 pending catalogs are invalid'
    );
  });

  test('a staged catalog must hold fewer items than its own total', () => {
    const stateRevision = revision(7);
    const entry = (total: number): JsonValue => ({
      state_revision: stateRevision,
      scope: { actor_id: ACTOR, actor_type: 'unit' },
      total_items: total,
      items: { action_one: descriptor(stateRevision, 'action_one') },
      next_cursor: `cursor_${'1'.repeat(32)}`,
      cursor_expires_at: null,
    });
    const catalogId = `catalog_${'a'.repeat(32)}`;
    expect(
      Either.isRight(Effect.runSync(Effect.either(validatePendingCatalogs({ [catalogId]: entry(2) }))))
    ).toBe(true);
    expect(
      Either.isLeft(Effect.runSync(Effect.either(validatePendingCatalogs({ [catalogId]: entry(1) }))))
    ).toBe(true);
  });

  test('a cursor expiry in the past is expired and one in the future is not', () => {
    expect(cursorExpired(null)).toBe(false);
    expect(cursorExpired('2000-01-01T00:00:00.000Z')).toBe(true);
    expect(cursorExpired('2999-01-01T00:00:00.000Z')).toBe(false);
  });
});

describe('a scoped catalog only earns its numbers when it is promoted', () => {
  test('the staging page numbers nothing and the final page numbers all of it', () => {
    const fx = fixture();
    const stateRevision = revision(7);
    const catalog = `catalog_${'a'.repeat(32)}`;
    const cursor = `cursor_${'c'.repeat(32)}`;
    const items = ['Alpha', 'Bravo', 'Charlie', 'Delta'].map((label, index) =>
      actorAction(stateRevision, `action_${String(index + 1).repeat(26)}`, ACTOR, {
        label,
        x: 31 + index,
        y: 72,
      })
    );
    const staged = ok(
      ingestLegal(
        fx,
        scopedLegalPage(stateRevision, items.slice(0, 2), ACTOR, {
          catalog,
          cursor,
          total: items.length,
        })
      )
    );
    expect(staged.promoted).toBeNull();
    expect(aliasEntries(staged.state)).toEqual({});
    expect(Object.keys(staged.state.pending_catalogs)).toEqual([catalog]);

    const promoted = ok(
      ingestLegal(
        fx,
        scopedLegalPage(stateRevision, items.slice(2), ACTOR, { catalog, total: items.length })
      )
    );
    expect(promoted.promoted).toHaveLength(4);
    expect(Object.keys(aliasEntries(promoted.state))).toEqual(['a1', 'a2', 'a3', 'a4']);
    expect(promoted.state.pending_catalogs).toEqual({});
    // Only a complete, actor-wide catalog is remembered as drained.
    expect(promoted.state.drained_actors).toEqual([ACTOR]);
  });

  test('an expired cursor discards the staged catalog and names the restart', () => {
    const fx = fixture();
    const stateRevision = revision(7);
    const catalog = `catalog_${'a'.repeat(32)}`;
    const fresh = scopedLegalPage(stateRevision, [descriptor(stateRevision)], ACTOR, {
      catalog,
      cursor: `cursor_${'e'.repeat(32)}`,
      total: 4,
    });
    const body = fresh['page'];
    const page: JsonObject = isJsonObject(body)
      ? { ...fresh, page: { ...body, cursor_expires_at: '2000-01-01T00:00:00.000Z' } }
      : fresh;
    expect(failure(ingestLegal(fx, page))).toBe(
      'legal-action catalog cursor expired; restart the scoped query'
    );
    expect(ok(fx.run(fx.store.readState(fx.sessionPath, fx.session))).pending_catalogs).toEqual({});
  });

  test('a catalog that changed its metadata mid-drain is discarded', () => {
    const fx = fixture();
    const stateRevision = revision(7);
    const catalog = `catalog_${'a'.repeat(32)}`;
    const cursor = `cursor_${'c'.repeat(32)}`;
    ok(
      ingestLegal(
        fx,
        scopedLegalPage(stateRevision, [descriptor(stateRevision, 'action_one')], ACTOR, {
          catalog,
          cursor,
          total: 4,
        })
      )
    );
    expect(
      failure(
        ingestLegal(
          fx,
          scopedLegalPage(stateRevision, [descriptor(stateRevision, 'action_two')], ACTOR, {
            catalog,
            cursor,
            total: 6,
          })
        )
      )
    ).toBe('legal-action catalog metadata changed');
    expect(ok(fx.run(fx.store.readState(fx.sessionPath, fx.session))).pending_catalogs).toEqual({});
  });

  test('a final page that arrives short of its total is refused', () => {
    const fx = fixture();
    const stateRevision = revision(7);
    const catalog = `catalog_${'a'.repeat(32)}`;
    expect(
      failure(
        ingestLegal(
          fx,
          scopedLegalPage(stateRevision, [descriptor(stateRevision, 'action_one')], ACTOR, {
            catalog,
            total: 4,
          })
        )
      )
    ).toBe('legal-action catalog completed before every item arrived');
  });

  test('one action ID describing two different actions is refused', () => {
    const fx = fixture();
    const stateRevision = revision(7);
    const catalog = `catalog_${'a'.repeat(32)}`;
    const cursor = `cursor_${'c'.repeat(32)}`;
    ok(
      ingestLegal(
        fx,
        scopedLegalPage(
          stateRevision,
          [actorAction(stateRevision, 'action_one', ACTOR, { label: 'Move' })],
          ACTOR,
          { catalog, cursor, total: 4 }
        )
      )
    );
    expect(
      failure(
        ingestLegal(
          fx,
          scopedLegalPage(
            stateRevision,
            [actorAction(stateRevision, 'action_one', ACTOR, { label: 'Fortify' })],
            ACTOR,
            { catalog, cursor, total: 4 }
          )
        )
      )
    ).toBe('one catalog action ID described two different actions');
  });

  test('a target-scoped catalog never claims to be a drained actor catalog', () => {
    const fx = fixture();
    const stateRevision = revision(7);
    const tile = TILES[0] ?? '';
    const page = envelope(
      {
        section: 'legal_actions',
        items: [descriptor(stateRevision)],
        total_items: 1,
        next_cursor: null,
        cursor_expires_at: null,
        scope: { actor_id: ACTOR, actor_type: 'unit', target_id: tile, target_type: 'tile' },
        catalog_id: `catalog_${'b'.repeat(32)}`,
        catalog_complete: true,
      },
      stateRevision
    );
    const after = ok(ingestLegal(fx, page)).state;
    expect(after.drained_actors).toEqual([]);
    expect(Object.keys(aliasEntries(after))).toEqual(['a1']);
  });
});

describe('dropping a staged catalog', () => {
  const stage = (fx: Fixture, catalog: string, cursor: string, actor: string): void => {
    const stateRevision = revision(7);
    ok(
      ingestLegal(
        fx,
        scopedLegalPage(stateRevision, [descriptor(stateRevision, `action_${catalog.slice(-3)}`)], actor, {
          catalog,
          cursor,
          total: 4,
        })
      )
    );
  };

  test('a refused cursor discards exactly the catalog behind it', () => {
    const fx = fixture();
    const one = `catalog_${'a'.repeat(32)}`;
    const two = `catalog_${'b'.repeat(32)}`;
    const cursorOne = `cursor_${'1'.repeat(32)}`;
    const cursorTwo = `cursor_${'2'.repeat(32)}`;
    stage(fx, one, cursorOne, ACTOR);
    stage(fx, two, cursorTwo, `unit_${'b'.repeat(32)}`);
    const after = ok(fx.run(dropPendingForCursor(fx.sessionPath, fx.session, cursorOne)));
    expect(Object.keys(after.pending_catalogs)).toEqual([two]);
    // The drop is durable, not just in memory.
    expect(
      Object.keys(ok(fx.run(fx.store.readState(fx.sessionPath, fx.session))).pending_catalogs)
    ).toEqual([two]);
  });

  test('a cursor nothing is staged behind leaves the cache alone', () => {
    const fx = fixture();
    const one = `catalog_${'a'.repeat(32)}`;
    stage(fx, one, `cursor_${'1'.repeat(32)}`, ACTOR);
    const after = ok(
      fx.run(dropPendingForCursor(fx.sessionPath, fx.session, `cursor_${'9'.repeat(32)}`))
    );
    expect(Object.keys(after.pending_catalogs)).toEqual([one]);
  });

  test('dropping by scope takes every catalog of one actor', () => {
    const fx = fixture();
    const one = `catalog_${'a'.repeat(32)}`;
    const two = `catalog_${'b'.repeat(32)}`;
    const other = `unit_${'b'.repeat(32)}`;
    stage(fx, one, `cursor_${'1'.repeat(32)}`, ACTOR);
    stage(fx, two, `cursor_${'2'.repeat(32)}`, other);
    const after = ok(fx.run(dropPendingForScope(fx.sessionPath, fx.session, ACTOR, '')));
    expect(Object.keys(after.pending_catalogs)).toEqual([two]);
  });

  test('an actor plus a target only drops that narrower question', () => {
    const fx = fixture();
    const one = `catalog_${'a'.repeat(32)}`;
    stage(fx, one, `cursor_${'1'.repeat(32)}`, ACTOR);
    const kept = ok(
      fx.run(dropPendingForScope(fx.sessionPath, fx.session, ACTOR, TILES[0] ?? ''))
    );
    // The staged catalog is actor-wide, so a target-scoped drop does not name it.
    expect(Object.keys(kept.pending_catalogs)).toEqual([one]);
  });
});

describe('revision ordering', () => {
  test('an older page is displayed but never revives a dead capability', () => {
    const fx = fixture();
    const newer = revision(9);
    ok(ingestLegal(fx, scopedLegalPage(newer, [descriptor(newer)], ACTOR)));
    const older = revision(7);
    const after = ok(ingestLegal(fx, scopedLegalPage(older, [descriptor(older)], ACTOR))).state;
    expect(after.last_revision).toEqual(newer);
    expect(Object.keys(after.actions)).toEqual(['action_opaque']);
  });

  test('a state token that changed without a newer revision is refused', () => {
    const fx = fixture();
    ok(ingestState(fx, sectionPage('overview', revision(7), [])));
    const forged: TestRevision = { turn: 3, revision: 7, state_token: 'state_forged' };
    expect(failure(ingestState(fx, sectionPage('overview', forged, [])))).toBe(
      'state token changed without a newer revision'
    );
  });
});

describe('receipts retire capabilities the way a newer page does', () => {
  test('an applied receipt clears actions but leaves the alias bucket to refuse', () => {
    const fx = fixture();
    const first = revision(7);
    const staged = ok(ingestLegal(fx, scopedLegalPage(first, [descriptor(first)], ACTOR))).state;
    expect(Object.keys(aliasEntries(staged))).toEqual(['a1']);

    const applied = ok(
      fx.run(
        Effect.flatMap(
          Effect.mapError(
            decodeReceipt(receiptPayloadFor(`batch_${'A'.repeat(24)}`, 'applied', revision(9)), fx.session),
            (error) => ({ message: error.message })
          ),
          (receipt) => rememberReceipt(fx.sessionPath, fx.session, receipt)
        )
      )
    );
    expect(applied.actions).toEqual({});
    expect(applied.drained_actors).toEqual([]);
    // The bucket survives so the refusal can name the revision it came from.
    expect(Object.keys(aliasEntries(applied))).toEqual(['a1']);
    expect(failure(fx.run(expandAlias(applied, 'a1', fx.sessionPath)))).toContain(
      'die with their revision'
    );
  });

  test('a receipt may never regress or change terminal state', () => {
    const fx = fixture();
    const batchId = `batch_${'A'.repeat(24)}`;
    const remember = (state: string, at: TestRevision) =>
      fx.run(
        Effect.flatMap(
          Effect.mapError(decodeReceipt(receiptPayloadFor(batchId, state, at), fx.session), (error) => ({
            message: error.message,
          })),
          (receipt) => rememberReceipt(fx.sessionPath, fx.session, receipt)
        )
      );
    ok(remember('accepted', revision(7)));
    ok(remember('applied', revision(8)));
    expect(failure(remember('accepted', revision(9)))).toBe(
      'a command receipt regressed or changed terminal state'
    );
    expect(failure(remember('rejected', revision(9)))).toBe(
      'a command receipt regressed or changed terminal state'
    );
  });
});

describe('renumbering across revisions', () => {
  /**
   * The property: over N revisions, a live `aN` never names two actions, an
   * entity alias never re-points, and the action bucket always names exactly
   * the revision it was built from.
   */
  test('N revisions never re-use a live number and never move an entity alias', () => {
    const fx = fixture();
    const seenEntities = new Map<string, string>();
    for (let round = 0; round < 12; round += 1) {
      const stateRevision = revision(7 + round);
      const actor = `unit_${round.toString(16).padStart(32, '0')}`;
      ok(
        ingestState(
          fx,
          sectionPage('units', stateRevision, [
            unitItem(actor, `tile_${round.toString(16).padStart(32, '0')}`, round, 5),
          ])
        )
      );
      const actions = Array.from({ length: 3 }, (_value, index) =>
        actorAction(
          stateRevision,
          `action_${round}_${index}`.padEnd(24, 'z'),
          actor,
          { x: index, y: round }
        )
      );
      const state = ok(
        ingestLegal(
          fx,
          scopedLegalPage(stateRevision, actions, actor, {
            catalog: `catalog_${round.toString(16).padStart(32, '0')}`,
          })
        )
      ).state;

      // Action aliases restart at a1 for every fresh revision and each number
      // is live exactly once.
      const live = aliasEntries(state);
      expect(Object.keys(live)).toEqual(['a1', 'a2', 'a3']);
      expect(new Set(Object.values(live)).size).toBe(3);
      expect(state.action_aliases['state_revision']).toEqual(stateRevision);
      expect(Object.keys(ok(fx.run(freshActionAliases(state))))).toEqual(['a1', 'a2', 'a3']);

      // Entity aliases survive every bump and never re-point.
      for (const [alias, identifier] of Object.entries(state.entity_aliases)) {
        expect(typeof identifier).toBe('string');
        if (typeof identifier !== 'string') continue;
        const previous = seenEntities.get(alias);
        if (previous !== undefined) expect(identifier).toBe(previous);
        seenEntities.set(alias, identifier);
      }
      expect(Object.keys(state.entity_aliases)).toHaveLength(round + 1);
      for (const alias of Object.keys(live)) expect(ACTION_ALIAS_RE.test(alias)).toBe(true);
    }
  });

  test('rebinding gives an unchanged action its previous number back', () => {
    const previous: Record<string, ActionAliasEntry> = {
      a1: { action_id: 'action_old_found', actor_id: ACTOR, semantics: 'found' },
      a2: { action_id: 'action_old_move', actor_id: ACTOR, semantics: 'move' },
    };
    // Fresh numbering alone would swap what a1 and a2 mean.
    const fresh = {
      state_revision: null,
      by_alias: {
        a1: { action_id: 'action_new_move', actor_id: ACTOR, semantics: 'move' },
        a2: { action_id: 'action_new_found', actor_id: ACTOR, semantics: 'found' },
      },
    };
    const { table, rebound } = rebindActionAliases(fresh, previous);
    expect(rebound).toBe(2);
    expect(Object.keys(table)).toEqual(['a1', 'a2']);
    expect(table['a1']?.action_id).toBe('action_new_found');
    expect(table['a2']?.action_id).toBe('action_new_move');
  });

  test('an identity that now names two actions keeps the fresh numbering', () => {
    const previous: Record<string, ActionAliasEntry> = {
      a1: { action_id: 'action_old', actor_id: ACTOR, semantics: 'found' },
    };
    const fresh = {
      state_revision: null,
      by_alias: {
        a1: { action_id: 'action_twin_one', actor_id: ACTOR, semantics: 'found' },
        a2: { action_id: 'action_twin_two', actor_id: ACTOR, semantics: 'found' },
      },
    };
    const { table, rebound } = rebindActionAliases(fresh, previous);
    expect(rebound).toBe(0);
    expect(table['a1']?.action_id).toBe('action_twin_one');
    expect(table['a2']?.action_id).toBe('action_twin_two');
  });

  test('a new action fills the first free number around the re-bound ones', () => {
    const previous: Record<string, ActionAliasEntry> = {
      a2: { action_id: 'action_old_move', actor_id: ACTOR, semantics: 'move' },
    };
    const fresh = {
      state_revision: null,
      by_alias: {
        a1: { action_id: 'action_new_move', actor_id: ACTOR, semantics: 'move' },
        a2: { action_id: 'action_new_sentry', actor_id: ACTOR, semantics: 'sentry' },
      },
    };
    const { table, rebound } = rebindActionAliases(fresh, previous);
    expect(rebound).toBe(1);
    expect(Object.keys(table)).toEqual(['a1', 'a2']);
    expect(table['a2']?.action_id).toBe('action_new_move');
    expect(table['a1']?.action_id).toBe('action_new_sentry');
  });

  test('an entry with no semantics is never carried across', () => {
    const previous: Record<string, ActionAliasEntry> = {
      a1: { action_id: 'action_old', actor_id: ACTOR, semantics: '' },
    };
    const fresh = {
      state_revision: null,
      by_alias: { a1: { action_id: 'action_new', actor_id: ACTOR, semantics: '' } },
    };
    expect(rebindActionAliases(fresh, previous).rebound).toBe(0);
  });
});

describe('semantics is identity with nothing revision-bound inside it', () => {
  test('two enumerations of the same move produce the same string', () => {
    const seat = identity();
    const semanticsOf = (at: TestRevision, actionId: string): string => {
      const page = scopedLegalPage(at, [actorAction(at, actionId, ACTOR, { x: 32, y: 73 })], ACTOR);
      const items = Effect.runSync(
        Effect.orDie(Effect.map(decodeLegalPage(page, seat), (decoded) => decoded.page.items))
      );
      const first = items[0];
      if (first === undefined) throw new Error('no descriptor');
      return Effect.runSync(Effect.orDie(actionSemantics(first)));
    };
    const older = semanticsOf(revision(7), `action_${'1'.repeat(26)}`);
    const newer = semanticsOf(revision(9), `action_${'9'.repeat(26)}`);
    expect(newer).toBe(older);
    // The target is named by coordinate, never by its hash.
    expect(older).toContain('T(32,73)');
    expect(older).not.toContain('action_');
  });

  test('a different board position is a different identity', () => {
    const seat = identity();
    const at = revision(7);
    const page = scopedLegalPage(
      at,
      [
        actorAction(at, `action_${'1'.repeat(26)}`, ACTOR, { x: 32, y: 73 }),
        actorAction(at, `action_${'2'.repeat(26)}`, ACTOR, { x: 33, y: 73 }),
      ],
      ACTOR
    );
    const items = Effect.runSync(
      Effect.orDie(Effect.map(decodeLegalPage(page, seat), (decoded) => decoded.page.items))
    );
    const [left, right] = items;
    if (left === undefined || right === undefined) throw new Error('no descriptors');
    expect(Effect.runSync(Effect.orDie(actionSemantics(left)))).not.toBe(
      Effect.runSync(Effect.orDie(actionSemantics(right)))
    );
  });
});

describe('json equality is CPython dict equality', () => {
  test('key order does not matter but key sets do', () => {
    expect(jsonEquals({ a: 1, b: 2 }, { b: 2, a: 1 })).toBe(true);
    expect(jsonEquals({ a: 1 }, { a: 1, b: null })).toBe(false);
    expect(jsonEquals([1, 2], [1, 2])).toBe(true);
    expect(jsonEquals([1, 2], [2, 1])).toBe(false);
    expect(jsonEquals(null, null)).toBe(true);
    expect(jsonEquals({ a: [1, { b: 'c' }] }, { a: [1, { b: 'c' }] })).toBe(true);
  });
});
