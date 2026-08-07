/**
 * Carrying an `aN` across a revision bump.
 *
 * Ports `test_v2_a_stale_alias_is_rebound_by_meaning_and_keeps_its_number`,
 * `test_v2_a_vanished_or_ambiguous_alias_still_fails_closed`,
 * `test_v2_two_actions_with_one_meaning_refuse_to_be_rebound` and
 * `test_v2_no_refresh_keeps_the_plain_refusal_and_sends_nothing`.
 *
 * The drain is U11's, so it arrives here as a `LegalPageFetcher` stub that
 * ingests one scoped catalog — which is exactly what
 * `_drain_legal_unlocked` does to `.v2-state`.  What is under test is the
 * decision: rebind by meaning, refuse when the meaning vanished, refuse when it
 * became ambiguous, and never spend a request under `--no-refresh`.
 */
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either, Layer, Option } from 'effect';
import { FULL_CONTROL_V2 } from 'src/constants';
import { playerError } from 'src/errors';
import { decodeLegalPage, decodePage } from 'src/schema/page';
import { isJsonObject, type JsonObject, type JsonValue } from 'src/schema/primitives';
import { PrivateFs } from 'src/services/private-fs';
import {
  SessionStore,
  sessionStoreFor,
  type Session,
  type SessionStoreApi,
  type V2ClientState,
} from 'src/services/session-store';
import { rememberPage, v2StateSchema } from 'src/services/aliases';
import { looksLikeAlias } from 'src/services/alias-expand';
import {
  expandActionAliasRefreshing,
  refreshStaleAlias,
  resolveAliasArguments,
  type LegalPageFetcher,
} from 'src/services/alias-refresh';
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

const failure = <A>(either: Either.Either<A, { readonly message: string }>): string => {
  expect(Either.isLeft(either)).toBe(true);
  return Either.isLeft(either) ? either.left.message : '';
};

// ---------------------------------------------------------------------------
// Wire builders
// ---------------------------------------------------------------------------

const revision = (number = 7, turn = 3): TestRevision => ({
  turn,
  revision: number,
  state_token: `state_${String(number).padStart(32, '0')}`,
});

const UNIT_ONE = `unit_${'a'.repeat(32)}`;
const TILE_ONE = `tile_${'b'.repeat(32)}`;

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

const foundCity = (stateRevision: TestRevision, actionId: string): JsonObject =>
  actorAction(stateRevision, actionId, UNIT_ONE, {
    kind: 'unit.found_city',
    operation: 'found_city',
    label: 'Found city',
    x: 31,
    y: 72,
  });

const moveAction = (stateRevision: TestRevision, actionId: string): JsonObject =>
  actorAction(stateRevision, actionId, UNIT_ONE, { x: 32, y: 73 });

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
        Effect.mapError(decodeLegalPage(page, fx.session), (error) => ({ message: error.message })),
        (decoded) => rememberPage(fx.sessionPath, fx.session, { legal: true, page: decoded })
      )
    )
  ).state;

const ingestState = (fx: Fixture, page: JsonObject): V2ClientState =>
  ok(
    fx.run(
      Effect.flatMap(
        Effect.mapError(decodePage(page, fx.session), (error) => ({ message: error.message })),
        (decoded) => rememberPage(fx.sessionPath, fx.session, { legal: false, page: decoded })
      )
    )
  ).state;

/**
 * `stage_stale_aliases`: cache one actor's catalog at rev7 (a1 = found city,
 * a2 = move), then bump the revision so both aliases go stale.
 */
const stageStaleAliases = (fx: Fixture): V2ClientState => {
  const old = revision(7);
  ingestLegal(
    fx,
    scopedLegalPage(
      old,
      [foundCity(old, `action_found${'7'.repeat(20)}`), moveAction(old, `action_move${'7'.repeat(21)}`)],
      UNIT_ONE,
      `catalog_${'a'.repeat(32)}`
    )
  );
  return ingestState(fx, sectionPage('overview', revision(9), []));
};

/** A `LegalPageFetcher` that ingests one page and counts its own calls. */
const fetcherFor = (
  fx: Fixture,
  page: JsonObject
): { readonly fetch: LegalPageFetcher; readonly calls: string[] } => {
  const calls: string[] = [];
  const fetch: LegalPageFetcher = (sessionPath, session, actorId) =>
    Effect.gen(function* () {
      calls.push(actorId);
      const decoded = yield* Effect.mapError(decodeLegalPage(page, session), (error) =>
        playerError(error.message)
      );
      yield* rememberPage(sessionPath, session, { legal: true, page: decoded });
    });
  return { fetch, calls };
};

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

// ---------------------------------------------------------------------------

describe('a stale alias is rebound by meaning and keeps its number', () => {
  test('fresh numbering would swap a1 and a2; rebinding puts them back', () => {
    const fx = fixture();
    const stale = stageStaleAliases(fx);
    const at = revision(9);
    // The same two actions at the new revision, in the opposite order and with
    // new handles.
    const freshMove = moveAction(at, `action_move${'9'.repeat(21)}`);
    const freshFound = foundCity(at, `action_found${'9'.repeat(20)}`);
    const { fetch, calls } = fetcherFor(
      fx,
      scopedLegalPage(at, [freshMove, freshFound], UNIT_ONE, `catalog_${'b'.repeat(32)}`)
    );

    const outcome = ok(
      fx.run(
        expandActionAliasRefreshing(fx.sessionPath, fx.session, stale, 'a1', {
          locked: false,
          fetch,
        })
      )
    );
    expect(outcome.note).toBe('a1 rebound at rev9');
    expect(outcome.identifier).toBe(freshFound['action_id'] as string);
    expect(calls).toEqual([UNIT_ONE]);

    // a1 still means "found this city"; a2 still means that move.
    const reloaded = ok(fx.run(fx.store.readState(fx.sessionPath, fx.session)));
    expect(aliasEntries(reloaded)).toEqual({
      a1: freshFound['action_id'] as string,
      a2: freshMove['action_id'] as string,
    });
    // The expired handle never survives the rebind.
    expect(Object.values(aliasEntries(reloaded))).not.toContain(`action_found${'7'.repeat(20)}`);
  });

  test('an alias that was already fresh costs nothing and notes nothing', () => {
    const fx = fixture();
    const old = revision(7);
    const state = ingestLegal(
      fx,
      scopedLegalPage(old, [foundCity(old, 'action_found_one')], UNIT_ONE, `catalog_${'a'.repeat(32)}`)
    );
    const { fetch, calls } = fetcherFor(fx, scopedLegalPage(old, [], UNIT_ONE, `catalog_${'b'.repeat(32)}`));
    const outcome = ok(
      fx.run(
        expandActionAliasRefreshing(fx.sessionPath, fx.session, state, 'a1', { locked: false, fetch })
      )
    );
    expect(outcome.note).toBe('');
    expect(outcome.identifier).toBe('action_found_one');
    expect(calls).toEqual([]);
  });
});

describe('a vanished or ambiguous alias still fails closed', () => {
  test('a meaning that no longer exists keeps the plain refusal', () => {
    const fx = fixture();
    const stale = stageStaleAliases(fx);
    const at = revision(9);
    const gone = actorAction(at, `action_other${'9'.repeat(20)}`, UNIT_ONE, {
      kind: 'unit.sentry',
      operation: 'sentry',
      label: 'Sentry',
    });
    const { fetch } = fetcherFor(
      fx,
      scopedLegalPage(at, [gone], UNIT_ONE, `catalog_${'b'.repeat(32)}`)
    );
    expect(
      failure(
        fx.run(
          expandActionAliasRefreshing(fx.sessionPath, fx.session, stale, 'a1', {
            locked: false,
            fetch,
          })
        )
      )
    ).toContain('die with their revision');
  });

  test('two actions with one meaning refuse to be rebound and name both', () => {
    const fx = fixture();
    const stale = stageStaleAliases(fx);
    const at = revision(9);
    const twins = [0, 1].map((index) => foundCity(at, `action_twin${index}${'9'.repeat(20)}`));
    const { fetch } = fetcherFor(
      fx,
      scopedLegalPage(at, twins, UNIT_ONE, `catalog_${'b'.repeat(32)}`)
    );
    const message = failure(
      fx.run(
        expandActionAliasRefreshing(fx.sessionPath, fx.session, stale, 'a1', {
          locked: false,
          fetch,
        })
      )
    );
    expect(message).toContain('a1 names 2 actions at rev9/t3');
    // Both candidates are named so the agent can pick one.
    expect(message).toMatch(/\(a\d+ a\d+\)/);
    expect(message).toContain('name exactly one of them');
  });

  test('an alias with no semantics is never re-resolved', () => {
    const fx = fixture();
    const stale = stageStaleAliases(fx);
    const empty = v2StateSchema.empty(fx.session);
    const stripped: V2ClientState = {
      ...stale,
      action_aliases: {
        state_revision: revision(7),
        by_alias: { a1: { action_id: 'action_x', actor_id: UNIT_ONE, semantics: '' } },
      },
    };
    expect(empty.action_aliases['by_alias']).toEqual({});
    const { fetch, calls } = fetcherFor(
      fx,
      scopedLegalPage(revision(9), [], UNIT_ONE, `catalog_${'b'.repeat(32)}`)
    );
    const outcome = ok(
      fx.run(
        refreshStaleAlias(fx.sessionPath, fx.session, stripped, 'a1', { locked: false, fetch })
      )
    );
    expect(Option.isNone(outcome)).toBe(true);
    expect(calls).toEqual([]);
  });

  test('an alias that was never numbered is never re-resolved', () => {
    const fx = fixture();
    const stale = stageStaleAliases(fx);
    const { fetch, calls } = fetcherFor(
      fx,
      scopedLegalPage(revision(9), [], UNIT_ONE, `catalog_${'b'.repeat(32)}`)
    );
    const outcome = ok(
      fx.run(refreshStaleAlias(fx.sessionPath, fx.session, stale, 'a9', { locked: false, fetch }))
    );
    expect(Option.isNone(outcome)).toBe(true);
    expect(calls).toEqual([]);
  });
});

describe('resolveAliasArguments', () => {
  test('--no-refresh keeps the plain refusal and sends nothing', () => {
    const fx = fixture();
    stageStaleAliases(fx);
    const { fetch, calls } = fetcherFor(
      fx,
      scopedLegalPage(revision(9), [], UNIT_ONE, `catalog_${'b'.repeat(32)}`)
    );
    expect(
      failure(
        fx.run(
          resolveAliasArguments(
            fx.sessionPath,
            fx.session,
            { action_id: 'a1' },
            { noRefresh: true, fetch }
          )
        )
      )
    ).toContain('die with their revision');
    expect(calls).toEqual([]);
  });

  test('with no fetcher at all the refusal is identical', () => {
    const fx = fixture();
    stageStaleAliases(fx);
    expect(
      failure(fx.run(resolveAliasArguments(fx.sessionPath, fx.session, { action_id: 'a1' }, {})))
    ).toContain('die with their revision');
  });

  test('a stale action alias is rebound and its note is collected', () => {
    const fx = fixture();
    stageStaleAliases(fx);
    const at = revision(9);
    const freshFound = foundCity(at, `action_found${'9'.repeat(20)}`);
    const { fetch, calls } = fetcherFor(
      fx,
      scopedLegalPage(at, [freshFound], UNIT_ONE, `catalog_${'b'.repeat(32)}`)
    );
    const resolved = ok(
      fx.run(
        resolveAliasArguments(fx.sessionPath, fx.session, { action_id: ' a1 ' }, { fetch })
      )
    );
    expect(resolved.notes).toEqual(['a1 rebound at rev9']);
    expect(resolved.values['action_id']).toBe(freshFound['action_id'] as string);
    expect(calls).toEqual([UNIT_ONE]);
  });

  test('a field that names no alias is left exactly as it was typed', () => {
    const fx = fixture();
    const untouched = ok(
      fx.run(
        resolveAliasArguments(fx.sessionPath, fx.session, {
          actor_id: UNIT_ONE,
          target_id: '',
        })
      )
    );
    expect(untouched.values).toEqual({ actor_id: UNIT_ONE, target_id: '' });
    expect(untouched.notes).toEqual([]);
  });

  test('entity and tile aliases expand without any refresh', () => {
    const fx = fixture();
    const at = revision(7);
    ingestState(
      fx,
      sectionPage('units', at, [
        {
          id: UNIT_ONE,
          scope: 'own',
          type: 'Settlers',
          tile_id: TILE_ONE,
          x: 31,
          y: 72,
          hp: 20,
          moves: 3,
          type_stats: { max_hp: 20, move_rate: 3 },
          activity: { name: 'idle' },
          automation: { controller: 'player', has_orders: false },
          route: null,
        },
      ])
    );
    const resolved = ok(
      fx.run(
        resolveAliasArguments(fx.sessionPath, fx.session, {
          actor_id: 'u1',
          center_id: 'T(31,72)',
          section: 'units',
        })
      )
    );
    expect(resolved.values).toEqual({
      actor_id: UNIT_ONE,
      center_id: TILE_ONE,
      section: 'units',
    });
    expect(resolved.notes).toEqual([]);
  });

  test('an unknown alias is a refusal, never a pass-through', () => {
    const fx = fixture();
    expect(
      failure(fx.run(resolveAliasArguments(fx.sessionPath, fx.session, { actor_id: 'u1' })))
    ).toBe('unknown unit alias u1; known unit aliases: none are known yet');
  });

  test('the alias shapes are exactly the three the dialect defines', () => {
    for (const good of ['a1', 'a9999', 'u1', 'c12', 'p3', 'r7', 'T(1,2)', 'T(-1, -2)', 't(0,0)']) {
      expect(looksLikeAlias(good)).toBe(true);
    }
    for (const bad of ['a0', 'a10000', 'x1', 'u', 'T(1)', 'T(1,2,3)', UNIT_ONE, '']) {
      expect(looksLikeAlias(bad)).toBe(false);
    }
  });
});
