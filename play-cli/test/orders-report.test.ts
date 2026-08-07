/**
 * `_unresolved_report` — the refusal that is the agent's recovery surface.
 *
 * Ports the refusal half of `test_v2_do_refuses_every_order_when_one_cannot_be_
 * resolved` and `test_v2_do_fetches_every_unread_actor_before_it_sends_
 * anything`, and table-drives the failure matrix the two of them sample: every
 * class of unresolvable order, asserted as the exact sentence it produces, plus
 * the enumeration command that would repair it.
 *
 * These strings are load-bearing.  An agent that reads "no cached action
 * advertises the verb `teleport`" learns the catalog is authoritative; an agent
 * that reads a paraphrase learns to retry.  Every case below asserts the whole
 * sentence, never a substring, for exactly that reason.
 */
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either, Layer } from 'effect';
import { FULL_CONTROL_V2, V2_WITHHELD } from 'src/constants';
import { playerError, type PlayerError } from 'src/errors';
import { decodeLegalPage, decodePage } from 'src/schema/page';
import { isJsonObject, type JsonObject, type JsonValue } from 'src/schema/primitives';
import { rememberPage, v2StateSchema } from 'src/services/aliases';
import { HEADER_FILE, mirrorDir, writeMirror } from 'src/services/mirror/store';
import { PrivateFs } from 'src/services/private-fs';
import {
  SessionStore,
  sessionStoreFor,
  type Session,
  type V2ClientState,
} from 'src/services/session-store';
import {
  orderEnumerationCommand,
  orderOutcomes,
  unresolvedReport,
  type OrderOutcome,
  type OrdersDeps,
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

const CERTAIN = JSON.stringify({ kind: 'exact', minimum_percent: 100, maximum_percent: 100 });

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
    const result: Record<string, JsonValue> = {
      action_id: (descriptor['action_id'] ?? null) as JsonValue,
      kind: (descriptor['kind'] ?? null) as JsonValue,
      label: (descriptor['label'] ?? null) as JsonValue,
      subject: compactSubject,
      target: (subject['target'] ?? null) as JsonValue,
      argument_schema: (descriptor['arguments_schema'] ?? null) as JsonValue,
    };
    const probability = subject['probability'];
    if (probability !== undefined && JSON.stringify(probability) !== CERTAIN) {
      result['probability'] = probability as JsonValue;
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

// ---------------------------------------------------------------------------
// Wire builders
// ---------------------------------------------------------------------------

const revision = (number = 7, turn = 3): TestRevision => ({
  turn,
  revision: number,
  state_token: `state_${String(number).padStart(32, '0')}`,
});

const UNIT_ONE = `unit_${'a'.repeat(32)}`;
const UNIT_TWO = `unit_${'b'.repeat(32)}`;
const CITY_ONE = `city_${'c'.repeat(32)}`;
const RELATION = `relation_${'e'.repeat(32)}`;

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
  const x = options.x ?? 32;
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

/**
 * The world every matrix case shares: `u1` holds one move onto T(32,72) and one
 * onto T(31,71); `c1` holds one worklist action; `u2` and `r1` are known
 * entities this seat never enumerated.
 */
const world = (): { readonly fx: Fixture; readonly state: V2ClientState } => {
  const fx = fixture();
  const at = revision(7);
  ingestLegal(
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
  ingestLegal(
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
            properties: { items: { type: 'array', minItems: 1, maxItems: 2 } },
            required: ['items'],
          },
        }),
      ],
      CITY_ONE,
      `catalog_${'b'.repeat(32)}`,
      'city'
    )
  );
  const state = ingestState(
    fx,
    sectionPage('units', at, [
      { id: UNIT_TWO, tile_id: tileId(30, 70), x: 30, y: 70 },
      { relation_id: RELATION },
    ])
  );
  return { fx, state };
};

const reasonOf = (fx: Fixture, state: V2ClientState, order: string): string => {
  const [outcome] = ok(fx.run(orderOutcomes(deps, state, fx.sessionPath, [order])));
  if (outcome === undefined) throw new Error('no outcome');
  expect(outcome.resolved).toBeNull();
  return outcome.reason;
};

// ---------------------------------------------------------------------------
// The failure matrix
// ---------------------------------------------------------------------------

describe('every class of unresolvable order says exactly one sentence', () => {
  const cases: ReadonlyArray<readonly [string, string, string]> = [
    [
      'ambiguous verb',
      'u1 move',
      '2 cached actions match; name exactly one by its alias: a1 a2',
    ],
    [
      'unadvertised verb',
      'u1 teleport 9,9',
      'no cached action advertises the verb `teleport`',
    ],
    [
      'a target name that matches nothing',
      'c1 queue Colossus',
      '`queue` takes items this seat has been offered by name; ' +
        'no cached target is named Colossus',
    ],
    [
      'a coordinate no cached action targets',
      'u1 move 99,99',
      'no cached `move` action targets 99,99',
    ],
    [
      'a coordinate no page ever named',
      'c1 queue 88,88',
      'unknown tile T(88,88): no page this seat has read named that coordinate. ' +
        'Nearest cached tiles: T(32,72) T(31,71) T(30,70)',
    ],
    [
      'an actor with no verb',
      'u1',
      'u1 names an actor but no verb; write `u1 <verb> [arguments]`',
    ],
    [
      'an actor this seat never enumerated',
      'u2 move',
      'no cached action belongs to u2',
    ],
    [
      'an entity that is not an actor',
      'r1 move',
      'r1 is not a unit, city, or player this seat can act as',
    ],
    [
      'a family with nothing cached',
      'spaceship launch',
      'no cached spaceship action',
    ],
    [
      'a Tier-1 word the catalog does not advertise, with no actor',
      'build Warriors',
      '`build` names city.set_production/set_production, ' +
        "which this seat's cached catalog does not advertise here",
    ],
    [
      'a Tier-1 word the catalog does not advertise, with an actor',
      'u1 build Warriors',
      '`build` names city.set_production/set_production, ' +
        "which this seat's cached catalog does not advertise for u1",
    ],
    [
      'an argument the action cannot take',
      'u1 move 32,72 twice',
      'no cached action takes those arguments; 1 candidate(s) matched the verb',
    ],
    [
      'an unknown alias',
      'u9 move',
      'unknown unit alias u9; known unit aliases: u1 u2',
    ],
  ];

  for (const [name, order, sentence] of cases) {
    test(name, () => {
      const { fx, state } = world();
      expect(reasonOf(fx, state, order)).toBe(sentence);
    });
  }

  test('an array bound the words overrun is a non-match, not a truncation', () => {
    const { fx, state } = world();
    // `items` declares maxItems 2, and the bound is checked before any element
    // is resolved, so three words never reach the per-word refusal — the whole
    // action simply does not match, which is the sentence the agent gets.
    expect(reasonOf(fx, state, 'c1 queue a b c')).toBe(
      'no cached action takes those arguments; 1 candidate(s) matched the verb'
    );
    // Under the bound, the failure is per word and names the word.
    expect(reasonOf(fx, state, 'c1 queue a b')).toBe(
      '`queue` takes items this seat has been offered by name; no cached target is named a'
    );
  });

  test('a stale action alias keeps its own refusal, with the re-enumeration command', () => {
    const { fx } = world();
    const state = ingestState(fx, sectionPage('overview', revision(9), []));
    expect(reasonOf(fx, state, 'a1')).toBe(
      'action alias a1 was enumerated at rev7/t3 but this seat now knows rev9/t3; ' +
        'action aliases die with their revision. Re-enumerate with ' +
        '`just legal --actor_id u1 --all` and use the new a1..aN'
    );
  });

  test('an alias that survived but names a retired action says so', () => {
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
    // Drop the descriptor the alias names but keep the alias table intact: the
    // handle is still numbered, the capability is gone.
    const orphaned: V2ClientState = { ...state, actions: {} };
    expect(reasonOf(fx, orphaned, 'a1')).toBe(
      'a1 names an action this seat no longer holds'
    );
  });

  test('no catalog read at all names the plain enumeration command', () => {
    const fx = fixture();
    const state = ingestState(fx, sectionPage('overview', revision(7), []));
    expect(reasonOf(fx, state, 'a1')).toBe(
      'unknown action alias a1: no legal-action catalog has been read yet; ' +
        'run `just legal`'
    );
  });
});

// ---------------------------------------------------------------------------
// _order_enumeration_command
// ---------------------------------------------------------------------------

describe('_order_enumeration_command names the drain in the agent dialect', () => {
  test('a known actor is named by its alias, an unknown one by its ID', () => {
    const { fx, state } = world();
    expect(ok(fx.run(orderEnumerationCommand(fx.sessionPath, state, UNIT_ONE)))).toBe(
      'just legal --actor_id u1 --all'
    );
    const stranger = `unit_${'9'.repeat(32)}`;
    expect(ok(fx.run(orderEnumerationCommand(fx.sessionPath, state, stranger)))).toBe(
      `just legal --actor_id ${stranger} --all`
    );
  });

  test('an order that named no actor asks for the whole catalog', () => {
    const { fx, state } = world();
    expect(ok(fx.run(orderEnumerationCommand(fx.sessionPath, state, '')))).toBe('just legal');
  });

  test('an actor ID spelling an inherited name is still just an unknown ID', () => {
    const { fx, state } = world();
    // CPython's `aliases.get(actor_id, actor_id)`: the fallback is the raw ID,
    // never whatever `Object.prototype` carries under that name.  A prototype
    // walk here would put a function body into the remedy line.
    for (const stranger of ['constructor', 'toString', '__proto__', 'valueOf']) {
      expect(ok(fx.run(orderEnumerationCommand(fx.sessionPath, state, stranger)))).toBe(
        `just legal --actor_id ${stranger} --all`
      );
    }
  });
});

// ---------------------------------------------------------------------------
// _unresolved_report
// ---------------------------------------------------------------------------

describe('_unresolved_report', () => {
  const report = (
    fx: Fixture,
    state: V2ClientState,
    orders: ReadonlyArray<string>
  ): ReadonlyArray<string> => {
    const outcomes = ok(fx.run(orderOutcomes(deps, state, fx.sessionPath, orders)));
    return ok(fx.run(unresolvedReport(fx.sessionPath, state, outcomes)));
  };

  test('the header counts the failures against the revision the cache holds', () => {
    const { fx, state } = world();
    const lines = report(fx, state, ['u1 move 32,72', 'u1 teleport 9,9', 'c9 build X']);
    expect(lines[0]).toBe(
      '2 of 3 orders did not resolve against the cached rev7/t3 catalog; nothing was sent'
    );
  });

  test('a resolved order is shown with the capability it bound to', () => {
    const { fx, state } = world();
    const lines = report(fx, state, ['u1 move 32,72', 'u1 teleport 9,9']);
    expect(lines[1]).toBe('  1 resolved    u1 move 32,72  ->  unit.order Move');
    expect(lines[2]).toBe(
      '  2 unresolved  u1 teleport 9,9  --  no cached action advertises the verb `teleport`'
    );
  });

  test('every unresolved order contributes its remedy, deduplicated in first-seen order', () => {
    const { fx, state } = world();
    const lines = report(fx, state, ['u1 teleport 9,9', 'u1 warp 1,1', 'c1 teleport 2,2']);
    expect(lines.filter((line) => line.startsWith('enumerate with:'))).toEqual([
      'enumerate with: just legal --actor_id u1 --all',
      'enumerate with: just legal --actor_id c1 --all',
    ]);
  });

  test('an order that named no actor asks for the whole catalog', () => {
    const { fx, state } = world();
    const lines = report(fx, state, ['set_goal Currency']);
    expect(lines[lines.length - 1]).toBe('enumerate with: just legal');
  });

  test('an empty cache says "no revision" rather than inventing one', () => {
    const fx = fixture();
    const empty: V2ClientState = {
      schema_version: 5,
      game_id: FIXTURE_GAME_ID,
      agent_id: fx.session.agentId,
      last_revision: null,
      actions: {},
      pending_catalogs: {},
      batches: {},
      receipts: {},
      action_aliases: { state_revision: null, by_alias: {} },
      entity_aliases: {},
      tile_aliases: {},
      drained_actors: [],
    };
    const lines = report(fx, empty, ['u1 sentry']);
    expect(lines[0]).toBe(
      '1 of 1 orders did not resolve against the cached no revision catalog; nothing was sent'
    );
  });

  test('a phase that is already over suppresses the remedies and leads with the note', () => {
    const { fx, state } = world();
    const dir = ok(fx.run(Effect.mapError(mirrorDir(fx.sessionPath), (error) => error)));
    ok(
      fx.run(
        writeMirror(
          dir,
          HEADER_FILE,
          'phase inactive_done · turn 3 phase movement · active no\n'
        )
      )
    );
    const lines = report(fx, state, ['u1 teleport 9,9']);
    expect(lines[0]).toBe('your phase is not active (state inactive_done) — just wait');
    expect(lines[1]).toContain('1 of 1 orders did not resolve');
    // Re-enumerating would only produce a fresh catalog this seat cannot act on.
    expect(lines.some((line) => line.startsWith('enumerate with:'))).toBe(false);
  });

  test('a batch that fully resolved still renders every row', () => {
    const { fx, state } = world();
    const outcomes: ReadonlyArray<OrderOutcome> = ok(
      fx.run(orderOutcomes(deps, state, fx.sessionPath, ['u1 move 32,72', 'u1 move 31,71']))
    );
    const lines = ok(fx.run(unresolvedReport(fx.sessionPath, state, outcomes)));
    expect(lines).toEqual([
      '0 of 2 orders did not resolve against the cached rev7/t3 catalog; nothing was sent',
      '  1 resolved    u1 move 32,72  ->  unit.order Move',
      '  2 resolved    u1 move 31,71  ->  unit.order Move',
    ]);
  });
});
