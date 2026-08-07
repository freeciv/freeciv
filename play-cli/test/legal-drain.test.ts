/**
 * `legal --all`: the drain, the bounds, the refusals, and determinism.
 *
 * Ports `test_v2_legal_kind_all_drains_compacts_and_caches_full_actions`
 * (test_client.py:510), `test_v2_compact_legal_pages_resume_after_byte_bound`
 * (637), `test_v2_legal_all_drains_one_actor_catalog_without_a_kind` (4833),
 * `test_v2_identical_actor_catalogs_render_once_per_revision` (4990),
 * `test_v2_legal_kind_accepts_the_taxonomy_its_own_column_prints` (9270) and
 * `test_v2_legal_kind_names_the_actor_scope_that_holds_the_kind` (9343).
 *
 * Plus the one test the Python has no counterpart for, because the Python had
 * no concurrency: a randomized-latency run whose printed bytes must not move.
 */
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either, Layer, Option } from 'effect';
import { FULL_CONTROL_V2, V2_LEGAL_COMPACT_MAX_BYTES } from 'src/constants';
import type { DualSpelling } from 'src/options';
import { runLegal, type LegalOptions } from 'src/commands/legal.cmd';
import type { JsonObject, JsonValue } from 'src/schema/primitives';
import { httpFor } from 'src/services/http';
import { v2StateSchema } from 'src/services/aliases';
import { drainLegal, drainLegalActors } from 'src/services/legal-drain';
import type { LegalCtx } from 'src/services/legal-query';
import { PrivateFs } from 'src/services/private-fs';
import {
  SessionStore,
  sessionStoreFor,
  type Session,
  type V2ClientState,
} from 'src/services/session-store';
import { V2Client, v2ClientFor } from 'src/services/v2-client';
import { FIXTURE_AGENT_ID, FIXTURE_GAME_ID, scratchWorkspace, sessionFile, type Scratch } from 'test/_fixtures';

const scratches: Scratch[] = [];
afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

// ---------------------------------------------------------------------------
// Wire fixtures
// ---------------------------------------------------------------------------

const revision = (number: number, turn = 3): JsonObject => ({
  turn,
  revision: number,
  state_token: `state_${String(number).padStart(32, '0')}`,
});

const EXACT: JsonObject = { kind: 'exact', minimum_percent: 100, maximum_percent: 100 };
const UNCERTAIN: JsonObject = { kind: 'unknown', minimum_percent: 0, maximum_percent: 100 };
const PLAYER = `player_${'f'.repeat(32)}`;

const legalPage = (
  items: ReadonlyArray<JsonValue>,
  rev: JsonObject,
  cursor: string | null = null,
  totalItems?: number
): JsonObject => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  game_id: FIXTURE_GAME_ID,
  agent_id: FIXTURE_AGENT_ID,
  state_revision: rev,
  page: {
    section: 'legal_actions',
    items,
    total_items: totalItems ?? items.length + (cursor === null ? 0 : 1),
    next_cursor: cursor,
  },
});

const scopedLegalPage = (
  items: ReadonlyArray<JsonValue>,
  rev: JsonObject,
  actorId: string,
  options: {
    readonly catalog?: string;
    readonly cursor?: string | null;
    readonly totalItems?: number;
  } = {}
): JsonObject => {
  const cursor = options.cursor ?? null;
  const base = legalPage(items, rev, cursor, options.totalItems);
  const page = base['page'] as JsonObject;
  return {
    ...base,
    page: {
      ...page,
      cursor_expires_at: cursor === null ? null : '2999-01-01T00:00:00.000Z',
      scope: { actor_id: actorId, actor_type: actorId.split('_', 1)[0] ?? '' },
      catalog_id: options.catalog ?? `catalog_${'e'.repeat(32)}`,
      catalog_complete: cursor === null,
    },
  };
};

const tileId = (x: number, y: number): string =>
  `tile_${`${String(x).padStart(4, '0')}${String(y).padStart(4, '0')}`.padStart(32, '0')}`;

const actorAction = (
  rev: JsonObject,
  actionId: string,
  actorId: string,
  overrides: {
    readonly kind?: string;
    readonly operation?: string;
    readonly label?: string;
    readonly x?: number;
    readonly y?: number;
    readonly probability?: JsonObject;
  } = {}
): JsonObject => {
  const x = overrides.x ?? 31;
  const y = overrides.y ?? 72;
  return {
    action_id: actionId,
    kind: overrides.kind ?? 'unit.order',
    label: overrides.label ?? 'Move',
    subject: {
      operation: overrides.operation ?? 'move',
      actor: { id: actorId, type: 'unit', name: 'Settlers' },
      target: { id: tileId(x, y), x, y },
      probability: overrides.probability ?? EXACT,
    },
    arguments_schema: { type: 'object' },
    state_revision: rev,
  };
};

const researchAction = (
  rev: JsonObject,
  actionId: string,
  name: string | null,
  probability: JsonObject,
  kind = 'research.set_target'
): JsonObject => ({
  action_id: actionId,
  kind,
  label: 'End phase',
  subject: {
    operation: 'set_target',
    target: name === null ? null : { type: 'technology', id: `tech_${actionId}`, name },
    probability,
    internal_detail_kept_only_in_cache: true,
  },
  arguments_schema: { type: 'object' },
  state_revision: rev,
});

const pregameAction = (
  rev: JsonObject,
  actionId: string,
  kind: string,
  operation: string,
  label: string,
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
    probability: EXACT,
  },
  arguments_schema: { type: 'object' },
  state_revision: rev,
});

// ---------------------------------------------------------------------------
// A supervisor that answers whatever the test queued
// ---------------------------------------------------------------------------

interface Route {
  readonly body: JsonValue;
  readonly status?: number;
  readonly delayS?: number;
}

const urlOf = (input: Parameters<typeof fetch>[0]): string =>
  typeof input === 'string' ? input : input instanceof URL ? input.href : input.url;

const respond = async (route: Route): Promise<Response> => {
  const delay = route.delayS ?? 0;
  if (delay > 0) await new Promise((resolve) => setTimeout(resolve, delay * 1000));
  return new Response(JSON.stringify(route.body), {
    status: route.status ?? 200,
    headers: { 'content-type': 'application/json' },
  });
};

const notFound = (url: string): Response =>
  new Response(
    JSON.stringify({ error: { code: 'not_implemented', message: `no route for ${url}` } }),
    { status: 404, headers: { 'content-type': 'application/json' } }
  );

interface Fixture {
  readonly layer: Layer.Layer<SessionStore | PrivateFs | V2Client>;
  readonly urls: ReadonlyArray<string>;
  readonly serve: (...routes: ReadonlyArray<Route>) => void;
  readonly repeat: (route: Route) => void;
  readonly sessionPath: string;
  readonly session: Session;
  readonly readState: () => V2ClientState;
}

const fixture = (
  router?: (url: string) => Route | undefined
): Fixture => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const sessionPath = path.join(scratch.workspace.stateRoot, FIXTURE_GAME_ID, 'seat.json');
  Effect.runSync(scratch.files.writeJson(sessionPath, sessionFile()));
  const store = sessionStoreFor(scratch.workspace, scratch.files, v2StateSchema, {});
  const session = Effect.runSync(store.resolveV2(sessionPath)).session;

  const queue: Route[] = [];
  const urls: string[] = [];
  let standing: Route | null = null;
  const fetchImpl = (async (input: Parameters<typeof fetch>[0]): Promise<Response> => {
    const url = urlOf(input);
    urls.push(url);
    const routed = router?.(url);
    if (routed !== undefined) return respond(routed);
    const next = queue.shift() ?? standing;
    return next === null || next === undefined ? notFound(url) : respond(next);
  }) as typeof fetch;

  return {
    layer: Layer.mergeAll(
      Layer.succeed(SessionStore, store),
      Layer.succeed(PrivateFs, scratch.files),
      Layer.succeed(V2Client, v2ClientFor(httpFor(fetchImpl), () => Effect.void))
    ),
    urls,
    serve: (...routes) => {
      queue.push(...routes);
    },
    repeat: (route) => {
      standing = route;
    },
    sessionPath,
    session,
    readState: () => Effect.runSync(store.readState(sessionPath, session)),
  };
};

const none = (): DualSpelling<string> => ({ dashed: Option.none(), underscored: Option.none() });
const some = (value: string): DualSpelling<string> => ({
  dashed: Option.some(value),
  underscored: Option.none(),
});

const legalOptions = (overrides: Partial<LegalOptions> = {}): LegalOptions => ({
  session: '',
  actorId: none(),
  targetId: none(),
  limit: '',
  cursor: '',
  kind: '',
  all: false,
  offset: '',
  full: false,
  json: false,
  ...overrides,
});

interface Captured {
  readonly out: ReadonlyArray<string>;
  readonly error: string | null;
}

const capture = async (options: LegalOptions, target: Fixture): Promise<Captured> => {
  const out: string[] = [];
  const original = console.log;
  console.log = (...parts: ReadonlyArray<unknown>) => out.push(parts.join(' '));
  try {
    const either = await Effect.runPromise(
      Effect.either(Effect.provide(runLegal(options), target.layer))
    );
    return {
      out,
      error: Either.isLeft(either) ? (either.left as { message: string }).message : null,
    };
  } finally {
    console.log = original;
  }
};

const succeeded = (captured: Captured): ReadonlyArray<string> => {
  if (captured.error !== null) throw new Error(captured.error);
  return captured.out;
};

// ---------------------------------------------------------------------------
// --kind --all
// ---------------------------------------------------------------------------

describe('legal --kind --all', () => {
  test('it drains every page, compacts, and caches the full descriptors', async () => {
    const rev = revision(11);
    const one = researchAction(rev, 'action_target_one', 'Alphabet', EXACT);
    const phaseEnd = researchAction(rev, 'action_phase_end', null, EXACT, 'phase.end');
    const two = researchAction(rev, 'action_target_two', 'Bronze Working', UNCERTAIN);
    const three = researchAction(rev, 'action_target_three', 'Ceremonial Burial', EXACT);
    const cursorOne = `cursor_${'a'.repeat(32)}`;
    const cursorTwo = `cursor_${'b'.repeat(32)}`;
    const target = fixture();
    target.serve(
      { body: legalPage([one, phaseEnd], rev, cursorOne, 4) },
      { body: legalPage([two], rev, cursorTwo, 4) },
      { body: legalPage([three], rev, null, 4) }
    );

    const out = succeeded(
      await capture(
        legalOptions({ kind: 'research.set_target', all: true, json: true }),
        target
      )
    );
    const result = JSON.parse(out[0] ?? '') as Record<string, unknown>;
    expect(result['state_revision']).toEqual(rev);
    expect(result['catalog_total']).toBe(4);
    expect(result['pages_read']).toBe(3);
    expect(result['matched']).toBe(3);
    expect(result['offset']).toBe(0);
    expect(result['limit']).toBe(64);
    expect(result['shown']).toBe(3);
    expect(result['truncated']).toBe(false);
    expect(result['has_more']).toBe(false);
    expect(result['next_offset']).toBeNull();
    expect(result['byte_limited']).toBe(false);
    expect(result['oversized_single']).toBe(false);

    const actions = result['actions'] as ReadonlyArray<JsonObject>;
    expect(new Set(Object.keys(actions[0] ?? {}))).toEqual(
      new Set(['action_id', 'kind', 'label', 'subject', 'target', 'argument_schema'])
    );
    // The leak guard hides the internal *value* but never the fact that a
    // discriminator existed.
    expect(actions[0]?.['subject']).toEqual({
      operation: 'set_target',
      internal_detail_kept_only_in_cache: '<withheld>',
    });
    expect(actions[1]?.['probability']).toEqual(UNCERTAIN);

    expect(target.urls[1]).toContain(`cursor=${cursorOne}`);
    expect(target.urls[2]).toContain(`cursor=${cursorTwo}`);
    const cached = target.readState().actions;
    expect(cached['action_target_one']).toEqual(one);
    expect(cached['action_phase_end']).toEqual(phaseEnd);
  });

  test('a kind that matched nothing refuses, naming the kinds that do exist', async () => {
    const rev = revision(7);
    const move = actorAction(rev, `action_${'m'.repeat(26)}`, `unit_${'a'.repeat(32)}`, {
      x: 32,
    });
    const goal = pregameAction(
      rev,
      `action_${'g'.repeat(26)}`,
      'research.set_goal',
      'set_goal',
      'Set research goal',
      { type: 'technology', id: 'tech_1', name: 'Currency' }
    );
    const target = fixture();
    target.repeat({ body: legalPage([move, goal], rev) });

    // The kind column prints `unit.order/move`; both spellings select it.
    for (const kind of ['unit.order', 'unit.order/move', 'research.set_goal']) {
      const out = succeeded(await capture(legalOptions({ kind, all: true }), target));
      expect(out.join('\n')).toContain('1/1 matched');
    }
    expect(
      succeeded(await capture(legalOptions({ kind: 'unit.order', all: true }), target)).join('\n')
    ).toContain('unit.order/move');

    const refused = await capture(
      legalOptions({ kind: 'unit.order/goto', all: true }),
      target
    );
    expect(refused.error).toContain('matched none of the 2 actions');
    expect(refused.error).toContain('unit.order/move');
    expect(refused.error).toContain('research.set_goal');
  });

  test('the refusal names the actor scope this seat already holds the kind in', async () => {
    const rev = revision(7);
    const actor = `unit_${'a'.repeat(32)}`;
    const target = fixture();
    // Cache one actor catalog first, exactly as `cache_actor_catalog` does.
    target.serve({
      body: scopedLegalPage(
        [actorAction(rev, `action_${'m'.repeat(26)}`, actor, { x: 32 })],
        rev,
        actor,
        { catalog: `catalog_${'a'.repeat(32)}` }
      ),
    });
    succeeded(await capture(legalOptions({ actorId: some(actor), all: true }), target));

    // The unscoped catalog carries no unit rows at all.
    target.serve({
      body: legalPage(
        [
          pregameAction(rev, `action_${'g'.repeat(26)}`, 'research.set_goal', 'set_goal', 'Set research goal', {
            type: 'technology',
            id: 'tech_1',
            name: 'Currency',
          }),
        ],
        rev
      ),
    });
    const refused = await capture(legalOptions({ kind: 'unit.order', all: true }), target);
    expect(refused.error).toContain('actor-scoped kind');
    expect(refused.error).toContain('just legal --actor_id u1 --all');
  });

  test('a player-scoped kind is named from the taxonomy, not from the cache', async () => {
    const rev = revision(7);
    const target = fixture();
    target.repeat({
      body: legalPage(
        [
          pregameAction(rev, `action_${'g'.repeat(26)}`, 'research.set_goal', 'set_goal', 'Set research goal', null),
        ],
        rev
      ),
    });
    const refused = await capture(
      legalOptions({ kind: 'government.change', all: true }),
      target
    );
    expect(refused.error).toContain('enumerated only in your own player scope');
    expect(refused.error).toContain('--kind government.change --all');

    const diplomacy = await capture(
      legalOptions({ kind: 'diplomacy.propose', all: true }),
      fixtureWith(rev)
    );
    expect(diplomacy.error).toContain('enumerated only against one relation');
    expect(diplomacy.error).toContain('just state --section diplomacy');
  });

  const fixtureWith = (rev: JsonObject): Fixture => {
    const target = fixture();
    target.repeat({
      body: legalPage(
        [pregameAction(rev, `action_${'g'.repeat(26)}`, 'research.set_goal', 'set_goal', 'Goal', null)],
        rev
      ),
    });
    return target;
  };
});

// ---------------------------------------------------------------------------
// The byte bound
// ---------------------------------------------------------------------------

describe('the compact byte bound', () => {
  /** One action whose compact projection alone overruns the 48 KiB window. */
  const heavyAction = (rev: JsonObject, index: number, order: string): JsonObject => ({
    action_id: `action_order_${index}`,
    kind: 'unit.order',
    label: `Order ${order}`,
    subject: {
      operation: 'order',
      order,
      // Well past V2_LEGAL_COMPACT_MAX_BYTES, well under the 64 KiB
      // single-action ceiling, so the bounded fallback prints exactly one.
      filler: 'x'.repeat(V2_LEGAL_COMPACT_MAX_BYTES + 2048),
    },
    arguments_schema: { type: 'object' },
    state_revision: rev,
  });

  test('each offset resumes at the next match and says it was byte-limited', async () => {
    const rev = revision(13);
    const actions = ['sentry', 'fortify', 'wake'].map((order, index) =>
      heavyAction(rev, index, order)
    );
    const results: Array<Record<string, unknown>> = [];
    const seenUrls: string[] = [];
    for (const offset of [0, 1, 2]) {
      const target = fixture();
      target.repeat({ body: legalPage(actions, rev) });
      const out = succeeded(
        await capture(
          legalOptions({
            kind: 'unit.order',
            all: true,
            limit: '2',
            offset: String(offset),
            json: true,
          }),
          target
        )
      );
      seenUrls.push(...target.urls);
      results.push(JSON.parse(out[0] ?? '') as Record<string, unknown>);
    }
    // `--limit` is a result window here, never a server page size.
    for (const url of seenUrls) expect(url).not.toContain('limit=');
    expect(
      results.map((result) => (result['actions'] as ReadonlyArray<JsonObject>)[0]?.['action_id'])
    ).toEqual(actions.map((action) => action['action_id']));
    expect(results.map((result) => result['next_offset'])).toEqual([1, 2, null]);
    expect(results.map((result) => result['has_more'])).toEqual([true, true, false]);
    expect(results.map((result) => result['byte_limited'])).toEqual([true, true, true]);
    expect(results.every((result) => result['oversized_single'] === true)).toBe(true);
    expect(results.every((result) => result['matched'] === 3)).toBe(true);
  });

  test('a byte-limited window names the kinds it kept back, and how to read them', async () => {
    const rev = revision(13);
    const actions = ['sentry', 'fortify'].map((order, index) => heavyAction(rev, index, order));
    const target = fixture();
    target.repeat({ body: legalPage(actions, rev) });
    const out = succeeded(
      await capture(legalOptions({ kind: 'unit.order', all: true }), target)
    );
    expect(out[0]).toContain('1/2 matched');
    expect(out[0]).toContain('byte_limited');
    expect(out[0]).toContain('oversized_single');
    expect(out[0]).toContain('more: just legal --kind unit.order --all --offset 1');
    // `unit.order` prints without a suffix: its operation is already the
    // kind's own tail, so `unit.order/order` would say nothing extra.
    expect(out.at(-1)).toBe(
      'not shown: unit.order (1) — just legal --kind unit.order --all --offset 1'
    );
  });
});

// ---------------------------------------------------------------------------
// --actor_id --all
// ---------------------------------------------------------------------------

describe('legal --actor_id --all', () => {
  test('one drain, no cursor ceremony, and the whole catalog promoted at once', async () => {
    const rev = revision(7);
    const actor = `unit_${'a'.repeat(32)}`;
    const first = actorAction(rev, `action_${'1'.repeat(26)}`, actor, { x: 31 });
    const second = actorAction(rev, `action_${'2'.repeat(26)}`, actor, { x: 32 });
    const third = actorAction(rev, `action_${'3'.repeat(26)}`, actor, {
      kind: 'unit.found_city',
      operation: 'found',
      label: 'Found city',
    });
    const cursor = `cursor_${'a'.repeat(32)}`;
    const catalog = `catalog_${'1'.repeat(32)}`;
    const target = fixture();
    target.serve(
      { body: scopedLegalPage([first, second], rev, actor, { catalog, cursor, totalItems: 3 }) },
      { body: scopedLegalPage([third], rev, actor, { catalog, totalItems: 3 }) }
    );

    const out = succeeded(
      await capture(legalOptions({ actorId: some(actor), all: true }), target)
    );
    expect(target.urls).toHaveLength(2);
    expect(target.urls[0]).toContain(`actor_id=${actor}`);
    expect(target.urls[1]).toContain(`cursor=${cursor}`);
    expect(out).toHaveLength(4);
    expect(out[0]).toContain('rev7/t3 legal scope=unit u1');
    expect(out[0]).toContain('3/3 matched');
    expect(out[0]).toContain('catalog 3 complete, pages 2');
    expect(out[0]).not.toContain('kind=');
    expect(out[0]).not.toContain('--cursor');
    [first, second, third].forEach((action, index) => {
      const row = out[index + 1] ?? '';
      // An aliased row drops the 32-hex opaque ID: the alias is the handle.
      expect(row.startsWith(`a${index + 1} `)).toBe(true);
      expect(row).not.toContain(action['action_id'] as string);
    });
    expect(out[1]).toContain('unit.order/move');
    expect(out[1]).toContain('T(31,72)');
    expect(out[3]).toContain('unit.found_city/found');

    const state = target.readState();
    expect(new Set(Object.keys(state.actions))).toEqual(
      new Set([first, second, third].map((action) => action['action_id'] as string))
    );
    expect(state.pending_catalogs).toEqual({});
    expect(state.drained_actors).toEqual([actor]);
  });

  test('an actor whose catalog repeats another prints one line, not the rows', async () => {
    const rev = revision(7);
    const actors = ['a', 'b', 'c', 'd'].map((letter) => `unit_${letter.repeat(32)}`);
    const target = fixture();
    const catalog = (
      actorId: string,
      tag: string,
      probability?: JsonObject,
      atRevision: JsonObject = rev
    ): JsonObject =>
      scopedLegalPage(
        [
          actorAction(atRevision, `action_${tag}${'0'.repeat(25)}`, actorId, { x: 31 }),
          actorAction(atRevision, `action_${tag}${'1'.repeat(25)}`, actorId, {
            kind: 'unit.found_city',
            operation: 'found',
            label: 'Found city',
            x: 31,
            ...(probability === undefined ? {} : { probability }),
          }),
        ],
        atRevision,
        actorId,
        { catalog: `catalog_${tag.repeat(32)}` }
      );

    const drain = async (actorId: string, page: JsonObject): Promise<ReadonlyArray<string>> => {
      target.serve({ body: page });
      return succeeded(await capture(legalOptions({ actorId: some(actorId), all: true }), target));
    };

    const first = await drain(actors[0] ?? '', catalog(actors[0] ?? '', 'a'));
    expect(first).toHaveLength(3);
    expect(first.join('\n')).not.toContain('==');

    const second = await drain(actors[1] ?? '', catalog(actors[1] ?? '', 'b'));
    expect(second).toHaveLength(2);
    expect(second[0]).toContain('rev7/t3 legal scope=unit u2');
    expect(second[1]).toBe('u2 == u1 (rev7) a3..a4');

    // A differing row is never hidden by the equivalence claim.
    const third = await drain(actors[2] ?? '', catalog(actors[2] ?? '', 'c', UNCERTAIN));
    expect(third).toHaveLength(3);
    expect(third[1]).toBe('u3 == u1 (rev7) a5..a6 except 1 row');
    expect(third[2]).toContain('!prob=0-100%/unknown');
    expect(third[2]).toContain('unit.found_city/found');
    expect(target.readState().drained_actors).toEqual(actors.slice(0, 3));

    // The same options in a different order are not claimed equivalent.
    const reorderedActor = `unit_${'e'.repeat(32)}`;
    const reordered = catalog(reorderedActor, 'e');
    const body = reordered['page'] as JsonObject;
    const reversed: JsonObject = {
      ...reordered,
      page: { ...body, items: [...(body['items'] as ReadonlyArray<JsonValue>)].reverse() },
    };
    const reorderedLines = await drain(reorderedActor, reversed);
    expect(reorderedLines).toHaveLength(3);
    expect(reorderedLines.join('\n')).not.toContain('==');

    // A newer revision expires every cached catalog.
    const later = revision(9);
    const fourth = catalog(actors[3] ?? '', 'd', undefined, later);
    const lines = await drain(actors[3] ?? '', fourth);
    expect(lines).toHaveLength(3);
    expect(lines.join('\n')).not.toContain('==');
    expect(lines[0]).toContain('rev9/t3 legal scope=unit u5');
    expect(target.readState().drained_actors).toEqual([actors[3] ?? '']);

    // Re-reading the same actor at the new revision cannot borrow the expired
    // equivalence either.
    const repeat = await drain(actors[3] ?? '', fourth);
    expect(repeat.join('\n')).not.toContain('==');
  });

  test('a catalog that changed mid-drain is a refusal, not a blended page', async () => {
    const rev = revision(7);
    const actor = `unit_${'a'.repeat(32)}`;
    const cursor = `cursor_${'a'.repeat(32)}`;
    const target = fixture();
    // Same revision, different `total_items`: the catalog grew underneath the
    // drain, so the two pages describe two different menus.
    target.serve(
      { body: legalPage([actorAction(rev, 'action_one', actor)], rev, cursor, 3) },
      { body: legalPage([actorAction(rev, 'action_two', actor)], rev, null, 5) }
    );
    const refused = await capture(legalOptions({ kind: 'unit.order', all: true }), target);
    expect(refused.error).toBe(
      'the legal catalog changed while it was being drained; run the same command again'
    );
  });
});

// ---------------------------------------------------------------------------
// Determinism under latency
// ---------------------------------------------------------------------------

describe('determinism', () => {
  const rev = revision(7);
  const actor = `unit_${'a'.repeat(32)}`;
  const cursors = [`cursor_${'a'.repeat(32)}`, `cursor_${'b'.repeat(32)}`];

  const pages = (): ReadonlyArray<JsonObject> => {
    const catalog = `catalog_${'1'.repeat(32)}`;
    return [
      scopedLegalPage(
        [
          actorAction(rev, `action_${'1'.repeat(26)}`, actor, { x: 31 }),
          actorAction(rev, `action_${'2'.repeat(26)}`, actor, { x: 32 }),
        ],
        rev,
        actor,
        { catalog, cursor: cursors[0] ?? null, totalItems: 4 }
      ),
      scopedLegalPage([actorAction(rev, `action_${'3'.repeat(26)}`, actor, { x: 33 })], rev, actor, {
        catalog,
        cursor: cursors[1] ?? null,
        totalItems: 4,
      }),
      scopedLegalPage(
        [
          actorAction(rev, `action_${'4'.repeat(26)}`, actor, {
            kind: 'unit.found_city',
            operation: 'found',
            label: 'Found city',
          }),
        ],
        rev,
        actor,
        { catalog, totalItems: 4 }
      ),
    ];
  };

  test('randomized per-page latency never moves a printed byte', async () => {
    const runs: string[] = [];
    for (let attempt = 0; attempt < 5; attempt += 1) {
      const target = fixture();
      target.serve(
        ...pages().map((body) => ({ body, delayS: Math.random() * 0.02 }))
      );
      runs.push(succeeded(await capture(legalOptions({ actorId: some(actor), all: true }), target)).join('\n'));
    }
    expect(new Set(runs).size).toBe(1);
    expect(runs[0]).toContain('4/4 matched');
    expect(runs[0]).toContain('pages 3');
  });

  test('concurrent per-actor drains emit in input order, whatever finishes first', async () => {
    const actors = ['a', 'b', 'c', 'd'].map((letter) => `unit_${letter.repeat(32)}`);
    const orders: string[] = [];
    for (let attempt = 0; attempt < 4; attempt += 1) {
      const delays = new Map(actors.map((id) => [id, Math.random() * 0.03]));
      const target = fixture((url) => {
        const found = actors.find((id) => url.includes(id));
        if (found === undefined) return undefined;
        return {
          body: scopedLegalPage(
            [actorAction(rev, `action_${found.slice(5, 6).repeat(26)}`, found, { x: 31 })],
            rev,
            found,
            { catalog: `catalog_${found.slice(5, 6).repeat(32)}` }
          ),
          delayS: delays.get(found) ?? 0,
        };
      });
      const ctx: LegalCtx = { sessionPath: target.sessionPath, session: target.session };
      const drained = await Effect.runPromise(
        Effect.provide(drainLegalActors(ctx, actors), target.layer)
      );
      orders.push(
        drained
          .map((catalog) => catalog.actions.map((action) => action['action_id']).join(','))
          .join('|')
      );
      expect(drained).toHaveLength(4);
    }
    expect(new Set(orders).size).toBe(1);
    expect(orders[0]).toBe(
      actors.map((id) => `action_${id.slice(5, 6).repeat(26)}`).join('|')
    );
  });

  test('a single-actor drain is the sequential path and still ingests everything', async () => {
    const target = fixture();
    target.serve(...pages().map((body) => ({ body })));
    const ctx: LegalCtx = { sessionPath: target.sessionPath, session: target.session };
    const drained = await Effect.runPromise(Effect.provide(drainLegal(ctx, actor), target.layer));
    expect(drained.revision).toEqual(rev as unknown as typeof drained.revision);
    expect(drained.actions).toHaveLength(4);
    expect(Object.keys(target.readState().actions)).toHaveLength(4);
  });
});
