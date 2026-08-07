/**
 * Resolving one order, and the whole batch, against the cached catalog.
 *
 * Ports `test_v2_order_grammar_uses_only_what_the_catalog_advertised` in full,
 * the resolution half of `test_v2_do_refuses_every_order_when_one_cannot_be_
 * resolved`, the fetch/no-fetch matrix of
 * `test_v2_do_fetches_every_unread_actor_before_it_sends_anything`, the
 * `--no-refresh` half of `test_v2_no_refresh_keeps_the_plain_refusal_and_sends_
 * nothing`, the rebind step `test_v2_do_rebinds_a_stale_alias_before_it_
 * resolves_the_order` performs before it resolves, and the identity rule
 * `test_v2_two_actions_with_one_meaning_refuse_to_be_rebound` enforces —
 * expressed here as `_rebind_order`, which is `do`'s own version of it.
 *
 * `_compact_legal_action` is U11's and has not landed; the fixture below is a
 * line-for-line port of client.py:7899-7947 so this unit is tested against the
 * projection it will actually receive.  See NOTES.md §15.1.
 */
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either, Layer } from 'effect';
import { FULL_CONTROL_V2, V2_WITHHELD } from 'src/constants';
import { playerError, type PlayerError } from 'src/errors';
import { decodeLegalPage, decodePage } from 'src/schema/page';
import { isJsonObject, type JsonObject, type JsonValue } from 'src/schema/primitives';
import { rememberPage, v2StateSchema } from 'src/services/aliases';
import type { LegalPageFetcher } from 'src/services/alias-refresh';
import { PrivateFs } from 'src/services/private-fs';
import {
  SessionStore,
  sessionStoreFor,
  type Session,
  type SessionStoreApi,
  type V2ClientState,
} from 'src/services/session-store';
import {
  drainLegalUnlocked,
  orderFetchTargets,
  orderOutcomes,
  rebindOrder,
  refreshOrders,
  refreshStaleOrderAliases,
  resolveOrder,
  resolveOrders,
  resolveOrdersFetching,
  type LegalPageReader,
  type OrdersDeps,
  type OrdersFetchDeps,
  type ResolvedOrder,
} from 'src/services/orders';
import { FIXTURE_GAME_ID, scratchWorkspace, sessionFile, type Scratch } from 'test/_fixtures';

// ---------------------------------------------------------------------------
// `_compact_legal_action` (client.py:7899-7947) — U11's, ported for the seam
// ---------------------------------------------------------------------------

const RESERVED_SUBJECT_TERMS: ReadonlySet<string> = new Set([
  'internal',
  'native',
  'packet',
  'private',
  'wire',
]);

const CERTAIN: JsonObject = { kind: 'exact', minimum_percent: 100, maximum_percent: 100 };

const sameJson = (left: JsonValue, right: JsonValue): boolean =>
  JSON.stringify(left) === JSON.stringify(right);

const compactLegalAction = (descriptor: JsonObject): Effect.Effect<JsonObject, PlayerError> =>
  Effect.sync(() => {
    const raw = descriptor['subject'];
    const subject = isJsonObject(raw) ? raw : {};
    const compactSubject: Record<string, JsonValue> = {};
    for (const [key, value] of Object.entries(subject)) {
      if (key === 'target' || key === 'probability' || key === 'gold_cost') continue;
      const withheld =
        key.startsWith('_') ||
        key
          .toLowerCase()
          .split(/[^a-z0-9]+/)
          .filter((part) => part !== '')
          .some((part) => RESERVED_SUBJECT_TERMS.has(part));
      compactSubject[key] = withheld ? V2_WITHHELD : (value as JsonValue);
    }
    const schema = (descriptor['arguments_schema'] ?? null) as JsonValue;
    const target = (subject['target'] ?? null) as JsonValue;
    const result: Record<string, JsonValue> = {
      action_id: (descriptor['action_id'] ?? null) as JsonValue,
      kind: (descriptor['kind'] ?? null) as JsonValue,
      label: (descriptor['label'] ?? null) as JsonValue,
      subject: compactSubject,
      target,
      argument_schema: schema,
    };
    const probability = subject['probability'];
    if (probability !== undefined && !sameJson(probability as JsonValue, CERTAIN)) {
      result['probability'] = probability as JsonValue;
    }
    let goldCost = (subject['gold_cost'] ?? null) as JsonValue;
    if (goldCost === null && isJsonObject(target)) goldCost = target['gold_cost'] ?? null;
    if (goldCost !== null) result['gold_cost'] = goldCost;
    const properties = isJsonObject(schema) ? schema['properties'] : null;
    const gold = isJsonObject(properties) ? properties['gold'] : null;
    if (isJsonObject(gold)) {
      const range: Record<string, JsonValue> = {};
      for (const key of ['minimum', 'maximum'] as const) {
        if (Object.hasOwn(gold, key)) range[key] = gold[key] as JsonValue;
      }
      result['gold_range'] = range;
    }
    return result;
  });

const deps: OrdersDeps = { compactLegalAction };

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

const failure = <A>(either: Either.Either<A, { readonly message?: string; readonly reason?: string }>): string => {
  expect(Either.isLeft(either)).toBe(true);
  if (!Either.isLeft(either)) return '';
  return either.left.reason ?? either.left.message ?? '';
};

// ---------------------------------------------------------------------------
// Wire builders (the Python tests' `actor_action` / `pregame_action` / pages)
// ---------------------------------------------------------------------------

const revision = (number = 7, turn = 3): TestRevision => ({
  turn,
  revision: number,
  state_token: `state_${String(number).padStart(32, '0')}`,
});

const UNIT_ONE = `unit_${'a'.repeat(32)}`;
const UNIT_TWO = `unit_${'b'.repeat(32)}`;
const PLAYER = `player_${'f'.repeat(32)}`;
const CITY_ONE = `city_${'c'.repeat(32)}`;

const tileId = (x: number, y: number): string =>
  `tile_${`${String(x).padStart(4, '0')}${String(y).padStart(4, '0')}`.padStart(32, '0')}`;

const actorAction = (
  stateRevision: TestRevision,
  actionId: string,
  actorId: string,
  options: {
    kind?: string;
    operation?: string;
    label?: string;
    x?: number;
    y?: number;
    schema?: JsonValue;
    actorType?: string;
  } = {}
): JsonObject => {
  const x = options.x ?? 31;
  const y = options.y ?? 72;
  return {
    action_id: actionId,
    kind: options.kind ?? 'unit.order',
    label: options.label ?? 'Move',
    subject: {
      operation: options.operation ?? 'move',
      actor: { id: actorId, type: options.actorType ?? 'unit', name: 'Settlers' },
      target: { id: tileId(x, y), x, y },
      probability: { kind: 'exact', minimum_percent: 100, maximum_percent: 100 },
    },
    arguments_schema: options.schema ?? { type: 'object' },
    state_revision: stateRevision,
  };
};

const pregameAction = (
  stateRevision: TestRevision,
  actionId: string,
  kind: string,
  operation: string,
  label: string,
  schema: JsonValue,
  target: JsonValue
): JsonObject => ({
  action_id: actionId,
  kind,
  label,
  subject: {
    operation,
    actor: { id: PLAYER, type: 'player', name: 'AgentPlace1' },
    target,
    variant: null,
    consuming: false,
    legality: 'legal',
    probability: { kind: 'exact', minimum_percent: 100, maximum_percent: 100 },
  },
  arguments_schema: schema,
  state_revision: stateRevision,
});

const legalPage = (
  stateRevision: TestRevision,
  items: ReadonlyArray<JsonValue>
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
  },
});

const scopedLegalPage = (
  stateRevision: TestRevision,
  items: ReadonlyArray<JsonValue>,
  actorId: string,
  catalog: string,
  actorType = 'unit'
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
    scope: { actor_id: actorId, actor_type: actorType },
    catalog_id: catalog,
    catalog_complete: true,
  },
});

const sectionPage = (
  section: string,
  stateRevision: TestRevision,
  items: ReadonlyArray<JsonValue>
): JsonObject => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  game_id: FIXTURE_GAME_ID,
  agent_id: 'agent_0123456789abcdef',
  state_revision: stateRevision,
  page: { section, items, total_items: items.length, next_cursor: null },
});

const ingestLegal = (fx: Fixture, page: JsonObject): V2ClientState =>
  ok(
    fx.run(
      Effect.flatMap(
        Effect.mapError(decodeLegalPage(page, fx.session), (error) => playerError(error.message)),
        (decoded) => rememberPage(fx.sessionPath, fx.session, { legal: true, page: decoded })
      )
    )
  ).state;

const ingestState = (fx: Fixture, page: JsonObject): V2ClientState =>
  ok(
    fx.run(
      Effect.flatMap(
        Effect.mapError(decodePage(page, fx.session), (error) => playerError(error.message)),
        (decoded) => rememberPage(fx.sessionPath, fx.session, { legal: false, page: decoded })
      )
    )
  ).state;

/** A `LegalPageFetcher` that ingests one page per actor and counts its calls. */
const fetcherFor = (
  fx: Fixture,
  pages: Readonly<Record<string, JsonObject>>
): { readonly fetch: LegalPageFetcher; readonly calls: string[] } => {
  const calls: string[] = [];
  const fetch: LegalPageFetcher = (sessionPath, session, actorId) =>
    Effect.gen(function* () {
      calls.push(actorId);
      const page = pages[actorId];
      if (page === undefined) return yield* Effect.fail(playerError(`no catalog for ${actorId}`));
      const decoded = yield* Effect.mapError(decodeLegalPage(page, session), (error) =>
        playerError(error.message)
      );
      yield* rememberPage(sessionPath, session, { legal: true, page: decoded });
    });
  return { fetch, calls };
};

const never: LegalPageFetcher = () => Effect.fail(playerError('nothing may be fetched'));

const resolve = (
  fx: Fixture,
  state: V2ClientState,
  text: string
): Either.Either<ResolvedOrder, { readonly message?: string; readonly reason?: string }> =>
  fx.run(resolveOrder(deps, state, fx.sessionPath, text));

// ---------------------------------------------------------------------------
// test_v2_order_grammar_uses_only_what_the_catalog_advertised
// ---------------------------------------------------------------------------

const GOAL_ID = `action_${'g'.repeat(26)}`;

const withGoal = (): { readonly fx: Fixture; readonly state: V2ClientState } => {
  const fx = fixture();
  const at = revision(7);
  const goal = pregameAction(
    at,
    GOAL_ID,
    'research.set_goal',
    'set_goal',
    'Set research goal',
    {
      type: 'object',
      properties: { tech: { type: 'string', enum: ['Currency', 'Alphabet'] } },
      required: ['tech'],
    },
    null
  );
  return { fx, state: ingestLegal(fx, legalPage(at, [goal])) };
};

describe('the order grammar uses only what the catalog advertised', () => {
  test('family form, full kind, bare verb, bare alias and the Tier-1 word all name it', () => {
    const { fx, state } = withGoal();
    for (const [text, tech] of [
      ['research set_goal currency', 'Currency'],
      ['research.set_goal Currency', 'Currency'],
      ['set_goal Alphabet', 'Alphabet'],
      ['a1 Alphabet', 'Alphabet'],
      // The one documented Tier-1 word for this capability.
      ['research goal Currency', 'Currency'],
    ] as const) {
      const resolved = ok(resolve(fx, state, text));
      expect(resolved.action_id).toBe(GOAL_ID);
      expect(resolved.arguments).toEqual({ tech });
      expect(resolved.order).toBe(text);
      expect(resolved.kind).toBe('research.set_goal');
      expect(resolved.operation).toBe('set_goal');
      expect(resolved.actor_id).toBe(PLAYER);
    }
  });

  test('a verb the catalog never advertised is never guessed at', () => {
    const { fx, state } = withGoal();
    expect(failure(resolve(fx, state, 'research target Currency'))).toBe(
      'no cached action advertises the verb `target`'
    );
  });

  test('a verb spelling an inherited property name is not a Tier-1 word', () => {
    const { fx, state } = withGoal();
    // CPython's `V2_TIER1_VERBS.get("constructor")` is a miss, so the word falls
    // through to the catalog's own verbs and gets the ordinary refusal.  A
    // prototype walk would instead report a capability named `undefined`.
    for (const word of ['constructor', 'toString', 'valueOf', 'hasOwnProperty']) {
      expect(failure(resolve(fx, state, `research ${word} Currency`))).toBe(
        `no cached action advertises the verb \`${word}\``
      );
    }
  });

  test('a Tier-1 word still needs its capability in scope', () => {
    const { fx, state } = withGoal();
    // `u1` is not an entity this seat learned, so it never reaches the verb.
    expect(failure(resolve(fx, state, 'u1 set_goal Currency'))).toContain('unknown unit alias u1');
  });

  test('a value outside the advertised enum is refused, not coerced', () => {
    const { fx, state } = withGoal();
    expect(failure(resolve(fx, state, 'research set_goal Pottery'))).toBe(
      'no cached action takes those arguments; 1 candidate(s) matched the verb'
    );
  });

  test('two cached actions that answer the same words are ambiguous, and name their aliases', () => {
    const { fx } = withGoal();
    const at = revision(7);
    const state = ingestLegal(
      fx,
      scopedLegalPage(
        at,
        [
          actorAction(at, `action_${'1'.repeat(26)}`, UNIT_ONE, { x: 32, y: 72 }),
          actorAction(at, `action_${'2'.repeat(26)}`, UNIT_ONE, { x: 31, y: 71 }),
        ],
        UNIT_ONE,
        `catalog_${'a'.repeat(32)}`
      )
    );
    const message = failure(resolve(fx, state, 'u1 move'));
    expect(message).toContain('2 cached actions match');
    expect(message).toContain('name exactly one by its alias');
    expect(message).toMatch(/a\d+ a\d+/);

    // Naming the target disambiguates without a request.
    expect(ok(resolve(fx, state, 'u1 move 31,71')).action_id).toBe(`action_${'2'.repeat(26)}`);
    expect(ok(resolve(fx, state, 'u1 move T(32,72)')).action_id).toBe(`action_${'1'.repeat(26)}`);
  });
});

// ---------------------------------------------------------------------------
// The pool narrowing steps
// ---------------------------------------------------------------------------

describe('the pool is narrowed only by what the order named', () => {
  test('a bare action alias names one exact capability and takes no verb', () => {
    const { fx, state } = withGoal();
    const resolved = ok(resolve(fx, state, 'a1 Currency'));
    expect(resolved.action_id).toBe(GOAL_ID);
  });

  test('an actor with no verb says what to write instead', () => {
    const fx = fixture();
    const at = revision(7);
    const state = ingestLegal(
      fx,
      scopedLegalPage(
        at,
        [actorAction(at, `action_${'1'.repeat(26)}`, UNIT_ONE)],
        UNIT_ONE,
        `catalog_${'a'.repeat(32)}`
      )
    );
    expect(failure(resolve(fx, state, 'u1'))).toBe(
      'u1 names an actor but no verb; write `u1 <verb> [arguments]`'
    );
  });

  test('an entity that is not an actor cannot be acted as', () => {
    const fx = fixture();
    const at = revision(7);
    // A tiles page teaches `T(x,y)`; a units page teaches `u1`.  Neither makes
    // a relation alias an actor.
    ingestState(fx, sectionPage('units', at, [{ id: UNIT_ONE, tile_id: tileId(31, 72), x: 31, y: 72 }]));
    const state = ingestState(
      fx,
      sectionPage('diplomacy', revision(7), [{ relation_id: `relation_${'e'.repeat(32)}` }])
    );
    expect(failure(resolve(fx, state, 'r1 move'))).toBe(
      'r1 is not a unit, city, or player this seat can act as'
    );
  });

  test('an actor with no cached action says so by the name the agent typed', () => {
    const fx = fixture();
    const at = revision(7);
    ingestLegal(
      fx,
      scopedLegalPage(
        at,
        [actorAction(at, `action_${'1'.repeat(26)}`, UNIT_ONE)],
        UNIT_ONE,
        `catalog_${'a'.repeat(32)}`
      )
    );
    const state = ingestState(fx, sectionPage('units', at, [{ id: UNIT_TWO, x: 4, y: 4 }]));
    expect(failure(resolve(fx, state, 'u2 move'))).toBe('no cached action belongs to u2');
  });

  test('a family word with nothing cached names the family', () => {
    const { fx, state } = withGoal();
    expect(failure(resolve(fx, state, 'spaceship launch'))).toBe('no cached spaceship action');
  });

  test('a Tier-1 word names the capability it routes to when the catalog lacks it', () => {
    const { fx, state } = withGoal();
    expect(failure(resolve(fx, state, 'build Warriors'))).toBe(
      '`build` names city.set_production/set_production, ' +
        "which this seat's cached catalog does not advertise here"
    );
  });

  test('the same Tier-1 refusal names the actor when one was given', () => {
    const fx = fixture();
    const at = revision(7);
    const state = ingestLegal(
      fx,
      scopedLegalPage(
        at,
        [actorAction(at, `action_${'1'.repeat(26)}`, UNIT_ONE)],
        UNIT_ONE,
        `catalog_${'a'.repeat(32)}`
      )
    );
    expect(failure(resolve(fx, state, 'u1 build Warriors'))).toBe(
      '`build` names city.set_production/set_production, ' +
        "which this seat's cached catalog does not advertise for u1"
    );
  });

  test('a coordinate that no cached action targets is refused by that coordinate', () => {
    const fx = fixture();
    const at = revision(7);
    const state = ingestLegal(
      fx,
      scopedLegalPage(
        at,
        [actorAction(at, `action_${'1'.repeat(26)}`, UNIT_ONE, { x: 32, y: 72 })],
        UNIT_ONE,
        `catalog_${'a'.repeat(32)}`
      )
    );
    expect(failure(resolve(fx, state, 'u1 move 99,99'))).toBe(
      'no cached `move` action targets 99,99'
    );
  });

  test('a listed action reads its coordinates as elements, not as a target key', () => {
    const fx = fixture();
    const at = revision(7);
    const state = ingestLegal(
      fx,
      scopedLegalPage(
        at,
        [
          actorAction(at, `action_${'r'.repeat(26)}`, UNIT_ONE, {
            operation: 'set_route',
            label: 'Set route',
            x: 31,
            y: 72,
            schema: {
              type: 'object',
              properties: {
                mode: { type: 'string' },
                tiles: { type: 'array', minItems: 1, maxItems: 8 },
              },
              required: ['mode', 'tiles'],
            },
          }),
        ],
        UNIT_ONE,
        `catalog_${'a'.repeat(32)}`
      )
    );
    const resolved = ok(resolve(fx, state, 'u1 route T(31,72)'));
    // `mode` is the Tier-1 word's own fixed value; the coordinate became the
    // one list element, resolved through the tile alias cache.
    expect(resolved.arguments).toEqual({ mode: 'goto', tiles: [tileId(31, 72)] });
    expect(ok(resolve(fx, state, 'u1 patrol 31,72')).arguments).toEqual({
      mode: 'patrol',
      tiles: [tileId(31, 72)],
    });
  });

  test('one capability in play names the element that failed', () => {
    const fx = fixture();
    const at = revision(7);
    const state = ingestLegal(
      fx,
      scopedLegalPage(
        at,
        [
          actorAction(at, `action_${'q'.repeat(26)}`, CITY_ONE, {
            kind: 'city.set_worklist',
            operation: 'set_worklist',
            label: 'Set worklist',
            actorType: 'city',
            schema: {
              type: 'object',
              properties: { items: { type: 'array', minItems: 1, maxItems: 6 } },
              required: ['items'],
            },
          }),
        ],
        CITY_ONE,
        `catalog_${'a'.repeat(32)}`,
        'city'
      )
    );
    expect(failure(resolve(fx, state, 'c1 queue Colossus'))).toBe(
      '`queue` takes items this seat has been offered by name; ' +
        'no cached target is named Colossus'
    );
    // An uncached coordinate names the tile refusal verbatim.
    expect(failure(resolve(fx, state, 'c1 queue 88,88'))).toContain('unknown tile T(88,88)');
  });
});

// ---------------------------------------------------------------------------
// _order_outcomes / _resolve_orders / _order_fetch_targets
// ---------------------------------------------------------------------------

describe('the batch resolves whole or refuses whole', () => {
  const withMove = (): { readonly fx: Fixture; readonly state: V2ClientState } => {
    const fx = fixture();
    const at = revision(7);
    return {
      fx,
      state: ingestLegal(
        fx,
        scopedLegalPage(
          at,
          [actorAction(at, `action_${'1'.repeat(26)}`, UNIT_ONE, { x: 32, y: 72 })],
          UNIT_ONE,
          `catalog_${'a'.repeat(32)}`
        )
      ),
    };
  };

  test('one unresolvable order refuses the whole line and names every verdict', () => {
    const { fx, state } = withMove();
    const message = failure(
      fx.run(
        resolveOrders(deps, state, fx.sessionPath, [
          'u1 move 32,72',
          'u1 teleport 99,99',
          'c9 build X',
        ])
      )
    );
    const lines = message.split('\n');
    expect(lines[0]).toContain('2 of 3 orders did not resolve');
    expect(lines[0]).toContain('rev7/t3');
    expect(lines[0]).toContain('nothing was sent');
    expect(lines[1]).toContain('1 resolved');
    expect(lines[1]).toContain('u1 move 32,72');
    expect(lines[2]).toContain('2 unresolved');
    expect(lines[2]).toContain('teleport');
    expect(lines[3]).toContain('3 unresolved');
    expect(message).toContain('enumerate with: just legal --actor_id u1 --all');
  });

  test('a batch that resolves whole hands back one resolution per order', () => {
    const { fx, state } = withMove();
    const resolved = ok(
      fx.run(resolveOrders(deps, state, fx.sessionPath, ['u1 move 32,72', 'u1 move T(32,72)']))
    );
    expect(resolved).toHaveLength(2);
    expect(resolved.map((item) => item.action_id)).toEqual([
      `action_${'1'.repeat(26)}`,
      `action_${'1'.repeat(26)}`,
    ]);
    expect(resolved[0]?.target_key).toBe('T(32,72)');
  });

  test('_order_outcomes keeps the actor an unresolved order asked for', () => {
    const { fx, state } = withMove();
    const outcomes = ok(
      fx.run(orderOutcomes(deps, state, fx.sessionPath, ['u1 teleport 9,9', 'set_goal X']))
    );
    expect(outcomes[0]?.resolved).toBeNull();
    expect(outcomes[0]?.actor).toBe(UNIT_ONE);
    expect(outcomes[1]?.actor).toBe('');
  });

  test('_order_fetch_targets names only actors this seat never drained', () => {
    const { fx, state } = withMove();
    const outcomes = ok(
      fx.run(orderOutcomes(deps, state, fx.sessionPath, ['u1 teleport 9,9']))
    );
    // u1's whole catalog is cached, so a bad verb is a real refusal.
    expect(orderFetchTargets(state, outcomes)).toEqual([]);
    const cold: V2ClientState = { ...state, drained_actors: [] };
    expect(orderFetchTargets(cold, outcomes)).toEqual([UNIT_ONE]);
    // Named once, however many orders asked for it.
    expect(
      orderFetchTargets(cold, [...outcomes, ...outcomes])
    ).toEqual([UNIT_ONE]);
  });
});

// ---------------------------------------------------------------------------
// test_v2_do_fetches_every_unread_actor_before_it_sends_anything
// ---------------------------------------------------------------------------

describe('every unread actor is fetched before anything is sent', () => {
  const withUnitAliases = (): { readonly fx: Fixture; readonly state: V2ClientState } => {
    const fx = fixture();
    const at = revision(7);
    // Learn the entity aliases without learning any capability.
    const state = ingestState(fx, sectionPage('units', at, [
      { id: UNIT_ONE, tile_id: tileId(31, 72), x: 31, y: 72 },
      { id: UNIT_TWO, tile_id: tileId(30, 72), x: 30, y: 72 },
    ]));
    return { fx, state };
  };

  const catalogs = (at: TestRevision): Readonly<Record<string, JsonObject>> => ({
    [UNIT_ONE]: scopedLegalPage(
      at,
      [
        actorAction(at, `action_sentry${'1'.repeat(19)}`, UNIT_ONE, {
          kind: 'unit.sentry',
          operation: 'sentry',
          label: 'Sentry',
        }),
      ],
      UNIT_ONE,
      `catalog_${'1'.repeat(32)}`
    ),
    [UNIT_TWO]: scopedLegalPage(
      at,
      [
        actorAction(at, `action_sentry${'2'.repeat(19)}`, UNIT_TWO, {
          kind: 'unit.sentry',
          operation: 'sentry',
          label: 'Sentry',
        }),
      ],
      UNIT_TWO,
      `catalog_${'2'.repeat(32)}`
    ),
  });

  test('both actors are fetched first, then the batch refuses whole', () => {
    const { fx, state } = withUnitAliases();
    const { fetch, calls } = fetcherFor(fx, catalogs(revision(7)));
    const fetching: OrdersFetchDeps = { compactLegalAction, drainLegal: fetch };
    const message = failure(
      fx.run(
        resolveOrdersFetching(fetching, fx.sessionPath, fx.session, state, [
          'u1 teleport 9,9',
          'u2 sentry',
        ])
      )
    );
    expect(calls).toEqual([UNIT_ONE, UNIT_TWO]);
    expect(message).toContain('fetched u1 options (rev7)');
    expect(message).toContain('fetched u2 options (rev7)');
    expect(message).toContain('1 of 2 orders did not resolve');
    expect(message).toContain('2 resolved');
    expect(message).toContain('enumerate with: just legal --actor_id u1 --all');
  });

  test('the pre-fetch is what makes the printed turn → do loop run as printed', () => {
    const { fx, state } = withUnitAliases();
    const { fetch, calls } = fetcherFor(fx, catalogs(revision(7)));
    const fetching: OrdersFetchDeps = { compactLegalAction, drainLegal: fetch };
    const outcome = ok(
      fx.run(
        resolveOrdersFetching(fetching, fx.sessionPath, fx.session, state, ['u1 sentry', 'u2 sentry'])
      )
    );
    expect(calls).toEqual([UNIT_ONE, UNIT_TWO]);
    expect(outcome.notes).toEqual(['fetched u1 options (rev7)', 'fetched u2 options (rev7)']);
    expect(outcome.resolved.map((item) => item.action_id)).toEqual([
      `action_sentry${'1'.repeat(19)}`,
      `action_sentry${'2'.repeat(19)}`,
    ]);
  });

  test('an actor already read is never re-fetched for a bad verb', () => {
    const { fx } = withUnitAliases();
    const at = revision(7);
    const state = ingestLegal(fx, catalogs(at)[UNIT_ONE] as JsonObject);
    const fetching: OrdersFetchDeps = { compactLegalAction, drainLegal: never };
    const message = failure(
      fx.run(resolveOrdersFetching(fetching, fx.sessionPath, fx.session, state, ['u1 teleport 9,9']))
    );
    expect(message).toContain('did not resolve');
    // The plain report, with no fetch note in front of it.
    expect(message.startsWith('1 of 1 orders did not resolve')).toBe(true);
  });

  test('--no-refresh keeps the plain refusal and fetches nothing', () => {
    const { fx, state } = withUnitAliases();
    const message = failure(fx.run(resolveOrders(deps, state, fx.sessionPath, ['u1 sentry'])));
    expect(message).toContain('did not resolve');
  });

  test('the caller\'s earlier notes survive in front of the refusal', () => {
    const { fx, state } = withUnitAliases();
    const { fetch } = fetcherFor(fx, catalogs(revision(7)));
    const fetching: OrdersFetchDeps = { compactLegalAction, drainLegal: fetch };
    const message = failure(
      fx.run(
        resolveOrdersFetching(
          fetching,
          fx.sessionPath,
          fx.session,
          state,
          ['u1 teleport 9,9'],
          ['a1 rebound at rev7']
        )
      )
    );
    expect(message.split('\n')[0]).toBe('a1 rebound at rev7');
    expect(message.split('\n')[1]).toBe('fetched u1 options (rev7)');
  });
});

// ---------------------------------------------------------------------------
// _refresh_stale_order_aliases
// ---------------------------------------------------------------------------

describe('a stale alias is re-bound before the order resolves', () => {
  const stage = (): { readonly fx: Fixture; readonly state: V2ClientState } => {
    const fx = fixture();
    const old = revision(7);
    ingestLegal(
      fx,
      scopedLegalPage(
        old,
        [
          actorAction(old, `action_found${'7'.repeat(20)}`, UNIT_ONE, {
            kind: 'unit.found_city',
            operation: 'found_city',
            label: 'Found city',
          }),
        ],
        UNIT_ONE,
        `catalog_${'a'.repeat(32)}`
      )
    );
    return { fx, state: ingestState(fx, sectionPage('overview', revision(9), [])) };
  };

  test('a1 keeps its number and the order resolves at the new revision', () => {
    const { fx, state } = stage();
    const at = revision(9);
    const fresh = actorAction(at, `action_found${'9'.repeat(20)}`, UNIT_ONE, {
      kind: 'unit.found_city',
      operation: 'found_city',
      label: 'Found city',
    });
    const { fetch, calls } = fetcherFor(fx, {
      [UNIT_ONE]: scopedLegalPage(at, [fresh], UNIT_ONE, `catalog_${'b'.repeat(32)}`),
    });
    const fetching: OrdersFetchDeps = { compactLegalAction, drainLegal: fetch };
    const refreshed = ok(
      fx.run(refreshStaleOrderAliases(fetching, fx.sessionPath, fx.session, state, ['a1']))
    );
    expect(calls).toEqual([UNIT_ONE]);
    expect(refreshed.notes).toEqual(['a1 rebound at rev9']);
    expect(ok(resolve(fx, refreshed.state, 'a1')).action_id).toBe(`action_found${'9'.repeat(20)}`);
  });

  test('an order that names no alias is left entirely alone', () => {
    const { fx, state } = stage();
    const fetching: OrdersFetchDeps = { compactLegalAction, drainLegal: never };
    const refreshed = ok(
      fx.run(refreshStaleOrderAliases(fetching, fx.sessionPath, fx.session, state, ['u1 found_city X']))
    );
    expect(refreshed.notes).toEqual([]);
    expect(refreshed.state).toEqual(state);
  });

  test('two actions with one meaning refuse to be re-bound', () => {
    const { fx, state } = stage();
    const at = revision(9);
    const twins = [0, 1].map((index) =>
      actorAction(at, `action_twin${index}${'9'.repeat(20)}`, UNIT_ONE, {
        kind: 'unit.found_city',
        operation: 'found_city',
        label: 'Found city',
      })
    );
    const { fetch } = fetcherFor(fx, {
      [UNIT_ONE]: scopedLegalPage(at, twins, UNIT_ONE, `catalog_${'b'.repeat(32)}`),
    });
    const fetching: OrdersFetchDeps = { compactLegalAction, drainLegal: fetch };
    const message = failure(
      fx.run(refreshStaleOrderAliases(fetching, fx.sessionPath, fx.session, state, ['a1']))
    );
    expect(message).toContain('a1 names 2 actions at rev9/t3');
    expect(message).toMatch(/\(a\d+ a\d+\)/);
  });

  test('a stale alias with no refresh keeps the plain refusal', () => {
    const { fx, state } = stage();
    expect(failure(resolve(fx, state, 'a1'))).toContain('die with their revision');
  });
});

// ---------------------------------------------------------------------------
// _rebind_order
// ---------------------------------------------------------------------------

describe('_rebind_order re-points a resolved order at the newest handle', () => {
  const found = (at: TestRevision, actionId: string): JsonObject =>
    actorAction(at, actionId, UNIT_ONE, {
      kind: 'unit.found_city',
      operation: 'found_city',
      label: 'Found city',
      schema: {
        type: 'object',
        properties: { city_name: { type: 'string' } },
        required: ['city_name'],
      },
    });

  test('the same action at a new revision keeps the order and its arguments', () => {
    const fx = fixture();
    const old = revision(7);
    const before = ingestLegal(
      fx,
      scopedLegalPage(old, [found(old, `action_${'7'.repeat(26)}`)], UNIT_ONE, `catalog_${'a'.repeat(32)}`)
    );
    const resolved = ok(resolve(fx, before, 'u1 found_city London'));
    expect(resolved.action_id).toBe(`action_${'7'.repeat(26)}`);

    const at = revision(9);
    const after = ingestLegal(
      fx,
      scopedLegalPage(at, [found(at, `action_${'9'.repeat(26)}`)], UNIT_ONE, `catalog_${'b'.repeat(32)}`)
    );
    const rebound = ok(fx.run(rebindOrder(deps, after, resolved)));
    expect(rebound?.action_id).toBe(`action_${'9'.repeat(26)}`);
    expect(rebound?.order).toBe('u1 found_city London');
    expect(rebound?.arguments).toEqual({ city_name: 'London' });
  });

  test('an action the new revision no longer offers rebinds to nothing', () => {
    const fx = fixture();
    const old = revision(7);
    const before = ingestLegal(
      fx,
      scopedLegalPage(old, [found(old, `action_${'7'.repeat(26)}`)], UNIT_ONE, `catalog_${'a'.repeat(32)}`)
    );
    const resolved = ok(resolve(fx, before, 'u1 found_city London'));
    const after = ingestState(fx, sectionPage('overview', revision(9), []));
    expect(ok(fx.run(rebindOrder(deps, after, resolved)))).toBeNull();
  });

  test('two candidates with one identity rebind to nothing rather than guess', () => {
    const fx = fixture();
    const old = revision(7);
    const before = ingestLegal(
      fx,
      scopedLegalPage(old, [found(old, `action_${'7'.repeat(26)}`)], UNIT_ONE, `catalog_${'a'.repeat(32)}`)
    );
    const resolved = ok(resolve(fx, before, 'u1 found_city London'));
    const at = revision(9);
    const after = ingestLegal(
      fx,
      scopedLegalPage(
        at,
        [found(at, `action_${'8'.repeat(26)}`), found(at, `action_${'9'.repeat(26)}`)],
        UNIT_ONE,
        `catalog_${'b'.repeat(32)}`
      )
    );
    expect(ok(fx.run(rebindOrder(deps, after, resolved)))).toBeNull();
  });
});

// ---------------------------------------------------------------------------
// _drain_legal_unlocked / _refresh_orders
// ---------------------------------------------------------------------------

describe('_drain_legal_unlocked walks the cursor chain and nothing else', () => {
  /** A `LegalPageReader` over a scripted cursor chain, recording every query. */
  const readerFor = (
    fx: Fixture,
    pages: ReadonlyArray<JsonObject>
  ): { readonly read: LegalPageReader; readonly queries: string[] } => {
    const queries: string[] = [];
    let index = 0;
    const read: LegalPageReader = (_sessionPath, session, query) =>
      Effect.gen(function* () {
        queries.push(query);
        const page = pages[Math.min(index, pages.length - 1)];
        index += 1;
        if (page === undefined) return yield* Effect.fail(playerError('no page'));
        return yield* Effect.mapError(decodeLegalPage(page, session), (error) =>
          playerError(error.message)
        );
      });
    return { read, queries };
  };

  const paged = (
    stateRevision: TestRevision,
    items: ReadonlyArray<JsonValue>,
    cursor: string | null
  ): JsonObject => ({
    schema_version: 2,
    control_protocol: FULL_CONTROL_V2,
    game_id: FIXTURE_GAME_ID,
    agent_id: 'agent_0123456789abcdef',
    state_revision: stateRevision,
    page: {
      section: 'legal_actions',
      items,
      total_items: items.length + (cursor === null ? 0 : 1),
      next_cursor: cursor,
      cursor_expires_at: cursor === null ? null : '2030-01-01T00:00:00Z',
    },
  });

  test('a scoped drain asks by actor, then by cursor, and stops at the last page', () => {
    const fx = fixture();
    const at = revision(7);
    const first = `cursor_${'1'.repeat(32)}`;
    const { read, queries } = readerFor(fx, [
      paged(at, [actorAction(at, `action_${'1'.repeat(26)}`, UNIT_ONE)], first),
      paged(at, [actorAction(at, `action_${'2'.repeat(26)}`, UNIT_ONE, { x: 30, y: 70 })], null),
    ]);
    const revisionRead = ok(fx.run(drainLegalUnlocked(read, fx.sessionPath, fx.session, UNIT_ONE)));
    expect(revisionRead?.revision).toBe(7);
    expect(queries).toEqual([`actor_id=${UNIT_ONE}`, `cursor=${first}`]);
  });

  test('a global drain sends no query at all', () => {
    const fx = fixture();
    const at = revision(7);
    const { read, queries } = readerFor(fx, [paged(at, [], null)]);
    ok(fx.run(drainLegalUnlocked(read, fx.sessionPath, fx.session)));
    expect(queries).toEqual(['']);
  });

  test('a repeated cursor is refused rather than looped on', () => {
    const fx = fixture();
    const at = revision(7);
    const stuck = `cursor_${'9'.repeat(32)}`;
    const { read } = readerFor(fx, [paged(at, [], stuck), paged(at, [], stuck)]);
    expect(failure(fx.run(drainLegalUnlocked(read, fx.sessionPath, fx.session)))).toBe(
      'the legal catalog repeated a cursor'
    );
  });

  test('a catalog that never ends stops at the safe page limit', () => {
    const fx = fixture();
    const at = revision(7);
    let issued = 0;
    const read: LegalPageReader = (_sessionPath, session) =>
      Effect.gen(function* () {
        issued += 1;
        const page = paged(at, [], `cursor_${String(issued).padStart(32, '0')}`);
        return yield* Effect.mapError(decodeLegalPage(page, session), (error) =>
          playerError(error.message)
        );
      });
    expect(failure(fx.run(drainLegalUnlocked(read, fx.sessionPath, fx.session)))).toBe(
      'the legal catalog exceeded the safe 512-page drain limit'
    );
    expect(issued).toBe(512);
  });
});

describe('_refresh_orders re-reads exactly the catalogs the pending orders name', () => {
  test('one drain per distinct actor, in first-seen order', () => {
    const fx = fixture();
    const at = revision(7);
    const { fetch, calls } = fetcherFor(fx, {
      [UNIT_ONE]: scopedLegalPage(
        at,
        [actorAction(at, `action_${'1'.repeat(26)}`, UNIT_ONE)],
        UNIT_ONE,
        `catalog_${'1'.repeat(32)}`
      ),
      [UNIT_TWO]: scopedLegalPage(
        at,
        [actorAction(at, `action_${'2'.repeat(26)}`, UNIT_TWO)],
        UNIT_TWO,
        `catalog_${'2'.repeat(32)}`
      ),
    });
    const fetching: OrdersFetchDeps = { compactLegalAction, drainLegal: fetch };
    const pending: ReadonlyArray<ResolvedOrder> = [
      {
        order: 'u1 move',
        action_id: 'a',
        kind: 'unit.order',
        operation: 'move',
        label: 'Move',
        actor_id: UNIT_ONE,
        target_key: 'T(32,72)',
        arguments: {},
      },
      {
        order: 'u2 move',
        action_id: 'b',
        kind: 'unit.order',
        operation: 'move',
        label: 'Move',
        actor_id: UNIT_TWO,
        target_key: 'T(32,72)',
        arguments: {},
      },
      {
        order: 'u1 move again',
        action_id: 'c',
        kind: 'unit.order',
        operation: 'move',
        label: 'Move',
        actor_id: UNIT_ONE,
        target_key: 'T(31,71)',
        arguments: {},
      },
    ];
    ok(fx.run(refreshOrders(fetching, fx.sessionPath, fx.session, pending)));
    expect(calls).toEqual([UNIT_ONE, UNIT_TWO]);
  });
});

// ---------------------------------------------------------------------------
// casefold on the two paths that read catalog text: the named target and the
// argument-free discriminator
// ---------------------------------------------------------------------------

/**
 * `_named_target_id` and `_order_discriminators` compare with
 * `str.casefold()`, and CPython's fold is not `toLowerCase`.  This repository's
 * own data is what makes the difference reachable: `data/nation/*.ruleset`
 * ships `Straßburg` and `Meißen` as city names, and `translations/core/de.po`
 * ships `Floß` for the Raft unit — both arrive as `target.name`.  CPython
 * answers `True` for `"Strassburg".casefold() == "Straßburg".casefold()`, so
 * the ASCII spelling an agent can type binds; a `toLowerCase` port would refuse
 * with `no cached target is named Strassburg`.
 */
describe('a cached target name folds the way CPython folds it', () => {
  const CITY_STRASSBURG = `city_${'d'.repeat(32)}`;
  const CITY_WITTENBERG = `city_${'e'.repeat(32)}`;
  const RAFT = `unit_type_${'f'.repeat(22)}`;
  const TRIREME = `unit_type_${'0'.repeat(22)}`;

  const joinCity = (
    at: TestRevision,
    actionId: string,
    cityId: string,
    cityName: string
  ): JsonObject => ({
    action_id: actionId,
    kind: 'unit.join_city',
    label: 'Join city',
    subject: {
      operation: 'join_city',
      actor: { id: UNIT_ONE, type: 'unit', name: 'Migrants' },
      target: { id: cityId, type: 'city', name: cityName },
      probability: CERTAIN,
    },
    arguments_schema: { type: 'object' },
    state_revision: at,
  });

  const setProduction = (
    at: TestRevision,
    actionId: string,
    productionId: string,
    productionName: string
  ): JsonObject => ({
    action_id: actionId,
    kind: 'city.set_production',
    label: `Build ${productionName}`,
    subject: {
      operation: 'set_production',
      actor: { id: CITY_ONE, type: 'city', name: 'Aachen' },
      target: { id: productionId, type: 'unit_type', name: productionName },
      probability: CERTAIN,
    },
    arguments_schema: { type: 'object' },
    state_revision: at,
  });

  const withJoinCity = (
    ...cities: ReadonlyArray<readonly [string, string, string]>
  ): { readonly fx: Fixture; readonly state: V2ClientState } => {
    const fx = fixture();
    const at = revision(7);
    return {
      fx,
      state: ingestLegal(
        fx,
        scopedLegalPage(
          at,
          cities.map(([actionId, cityId, cityName]) => joinCity(at, actionId, cityId, cityName)),
          UNIT_ONE,
          `catalog_${'a'.repeat(32)}`
        )
      ),
    };
  };

  test('the ASCII spelling of a ruleset city name selects the argument-free action', () => {
    const { fx, state } = withJoinCity([
      `action_${'j'.repeat(26)}`,
      CITY_STRASSBURG,
      'Straßburg',
    ]);
    for (const spelling of ['Straßburg', 'Strassburg', 'STRASSBURG', 'strassburg']) {
      expect(ok(resolve(fx, state, `u1 join_city ${spelling}`)).action_id).toBe(
        `action_${'j'.repeat(26)}`
      );
    }
    // A name that folds onto nothing cached is still refused, not guessed.
    expect(failure(resolve(fx, state, 'u1 join_city Strasbourg'))).toBe(
      'no cached action takes those arguments; 1 candidate(s) matched the verb'
    );
  });

  test('the fold disambiguates rather than collapsing two named targets', () => {
    const { fx, state } = withJoinCity(
      [`action_${'j'.repeat(26)}`, CITY_STRASSBURG, 'Straßburg'],
      [`action_${'k'.repeat(26)}`, CITY_WITTENBERG, 'Wittenberg']
    );
    expect(ok(resolve(fx, state, 'u1 join_city Strassburg')).action_id).toBe(
      `action_${'j'.repeat(26)}`
    );
    expect(ok(resolve(fx, state, 'u1 join_city Wittenberg')).action_id).toBe(
      `action_${'k'.repeat(26)}`
    );
    // Naming neither is still the ambiguity refusal, not a guess.
    expect(failure(resolve(fx, state, 'u1 join_city'))).toContain('2 cached actions match');
  });

  test('_named_target_id binds a worklist element through the same fold', () => {
    const fx = fixture();
    const at = revision(7);
    const state = ingestLegal(
      fx,
      scopedLegalPage(
        at,
        [
          actorAction(at, `action_${'q'.repeat(26)}`, CITY_ONE, {
            kind: 'city.set_worklist',
            operation: 'set_worklist',
            label: 'Set worklist',
            actorType: 'city',
            schema: {
              type: 'object',
              properties: { items: { type: 'array', minItems: 1, maxItems: 6 } },
              required: ['items'],
            },
          }),
          setProduction(at, `action_${'r'.repeat(26)}`, RAFT, 'Floß'),
          setProduction(at, `action_${'s'.repeat(26)}`, TRIREME, 'Trireme'),
        ],
        CITY_ONE,
        `catalog_${'b'.repeat(32)}`,
        'city'
      )
    );
    // `translations/core/de.po` renders Raft as `Floß`; `Floss` is the spelling
    // an agent types, and CPython folds the two together.
    expect(ok(resolve(fx, state, 'c1 queue Floss')).arguments).toEqual({ items: [RAFT] });
    expect(ok(resolve(fx, state, 'c1 queue Floß Trireme')).arguments).toEqual({
      items: [RAFT, TRIREME],
    });
    // And the refusal still fires for a word no cached target is named.
    expect(failure(resolve(fx, state, 'c1 queue Flotte'))).toBe(
      '`queue` takes items this seat has been offered by name; ' +
        'no cached target is named Flotte'
    );
  });
});
