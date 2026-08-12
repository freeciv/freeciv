/**
 * The `legal` renderers and the `legal` command's flag matrix.
 *
 * Ports `test_v2_text_legal_page_prints_the_envelope_exactly_once`
 * (test_client.py:809), `test_v2_global_catalog_groups_families_and_full_
 * prints_them_flat` (7339), `test_v2_scoped_catalog_rendering_is_untouched_by_
 * grouping` (7380), the `legal` arm of `test_v2_text_commands_keep_a_byte_
 * identical_json_escape_hatch` (885), `test_v2_legal_all_requires_a_scope_and_
 * keeps_the_kind_form` (4957) and the legal-actions half of
 * `test_v2_openapi_contract_has_closed_routes_refs_and_enums` (4496).
 *
 * The drain itself, the byte bound and the equivalence claim live in
 * `test/legal-drain.test.ts`; what is under test here is what one page *reads*
 * like and which flag combinations never reach the wire at all.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either, Layer, Option } from 'effect';
import { ACTION_KIND_RE, FULL_CONTROL_V2 } from 'src/constants';
import type { DualSpelling } from 'src/options';
import { runLegal, type LegalOptions } from 'src/commands/legal.cmd';
import { aliasSpan } from 'src/render/legal/equivalence';
import { descriptorKindKey, kindSelectorMatches } from 'src/render/legal/kinds';
import { renderLegalCompact, renderLegalPage } from 'src/render/legal/page';
import type { LegalCompactResult } from 'src/services/legal-compact';
import { decodeLegalPage, type LegalActionPageEnvelope } from 'src/schema/page';
import type { JsonObject, JsonValue } from 'src/schema/primitives';
import { httpFor } from 'src/services/http';
import { v2StateSchema } from 'src/services/aliases';
import { mirrorDir } from 'src/services/mirror';
import { PrivateFs } from 'src/services/private-fs';
import { SessionStore, sessionStoreFor } from 'src/services/session-store';
import { V2Client, v2ClientFor } from 'src/services/v2-client';
import {
  FIXTURE_AGENT_ID,
  FIXTURE_GAME_ID,
  identity,
  recordingFetch,
  scratchWorkspace,
  sessionFile,
  type FakeRoute,
  type RecordedRequest,
  type Scratch,
} from 'test/_fixtures';

const scratches: Scratch[] = [];
afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

// ---------------------------------------------------------------------------
// Wire fixtures, ported from PlayerClientTests
// ---------------------------------------------------------------------------

const revision = (number: number, turn = 3): JsonObject => ({
  turn,
  revision: number,
  state_token: `state_${String(number).padStart(32, '0')}`,
});

const UNIT = `unit_${'a'.repeat(32)}`;
const PLAYER = `player_${'f'.repeat(32)}`;
const EXACT: JsonObject = { kind: 'exact', minimum_percent: 100, maximum_percent: 100 };

const legalPage = (
  items: ReadonlyArray<JsonValue>,
  rev: JsonObject,
  cursor: string | null = null
): JsonObject => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  game_id: FIXTURE_GAME_ID,
  agent_id: FIXTURE_AGENT_ID,
  state_revision: rev,
  page: {
    section: 'legal_actions',
    items,
    total_items: items.length + (cursor === null ? 0 : 1),
    next_cursor: cursor,
  },
});

const scopedLegalPage = (
  items: ReadonlyArray<JsonValue>,
  rev: JsonObject,
  actorId: string,
  options: { readonly catalog?: string; readonly cursor?: string | null } = {}
): JsonObject => {
  const cursor = options.cursor ?? null;
  const base = legalPage(items, rev, cursor);
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

const playerAction = (
  rev: JsonObject,
  actionId: string,
  spec: {
    readonly kind: string;
    readonly operation: string;
    readonly label: string;
    readonly target?: JsonValue;
    readonly probability?: JsonObject;
    readonly subjectExtra?: JsonObject;
  }
): JsonObject => ({
  action_id: actionId,
  kind: spec.kind,
  label: spec.label,
  subject: {
    operation: spec.operation,
    actor: { id: PLAYER, type: 'player' },
    target: spec.target ?? null,
    probability: spec.probability ?? EXACT,
    ...spec.subjectExtra,
  },
  arguments_schema: { type: 'object' },
  state_revision: rev,
});

const decode = (payload: JsonObject): LegalActionPageEnvelope =>
  Effect.runSync(decodeLegalPage(payload, identity()));

const lines = (
  effect: Effect.Effect<ReadonlyArray<string>, { readonly message: string }>
): ReadonlyArray<string> => {
  const either = Effect.runSync(Effect.either(effect));
  if (Either.isLeft(either)) throw new Error(either.left.message);
  return either.right;
};

// ---------------------------------------------------------------------------
// _render_legal_page
// ---------------------------------------------------------------------------

describe('renderLegalPage', () => {
  const rev = revision(11);
  const certain: JsonObject = {
    action_id: `action_${'1'.repeat(32)}`,
    kind: 'unit.order',
    label: 'Sentry Warriors',
    subject: {
      actor: { type: 'unit', id: UNIT },
      target: null,
      operation: 'order',
      order: 'sentry',
      variant: null,
      consuming: false,
      legality: 'legal',
      probability: EXACT,
    },
    arguments_schema: {},
    state_revision: rev,
  };
  const gamble: JsonObject = {
    action_id: `action_${'2'.repeat(32)}`,
    kind: 'unit.perform_action',
    label: 'Steal technology',
    subject: {
      actor: { type: 'unit', id: UNIT },
      target: { type: 'city', id: `city_${'c'.repeat(32)}`, name: 'Paris' },
      operation: 'perform_action',
      variant: 'targeted_steal_tech',
      consuming: true,
      legality: 'possibly_legal',
      probability: { kind: 'unknown', minimum_percent: 0, maximum_percent: 100 },
    },
    arguments_schema: {
      type: 'object',
      properties: { name: { type: 'string' } },
      required: ['name'],
    },
    state_revision: rev,
  };

  const scopedPage = ((): LegalActionPageEnvelope => {
    const raw = legalPage([certain, gamble], rev, `cursor_${'9'.repeat(32)}`);
    const page = raw['page'] as JsonObject;
    return decode({
      ...raw,
      page: {
        ...page,
        total_items: 3,
        cursor_expires_at: '2999-01-01T00:00:00.000Z',
        scope: { actor_id: UNIT, actor_type: 'unit' },
        catalog_id: `catalog_${'e'.repeat(32)}`,
        catalog_complete: false,
      },
    });
  })();

  test('the envelope is printed once and never repeats inside the body', () => {
    const out = lines(renderLegalPage(scopedPage));
    expect(out).toHaveLength(3);
    const [header, first, second] = out as [string, string, string];
    expect(header).toContain('rev11/t3');
    expect(header).toContain(`scope=unit ${UNIT}`);
    expect(header).toContain('2/3');
    expect(header).toContain(`more --cursor cursor_${'9'.repeat(32)}`);
    for (const row of [first, second]) {
      for (const repeated of [
        'rev11',
        'state_token',
        'state_revision',
        'schema_version',
        'control_protocol',
        FIXTURE_AGENT_ID,
        FIXTURE_GAME_ID,
      ]) {
        expect(row).not.toContain(repeated);
      }
    }
  });

  test('omit-when-default hides four fields; a non-default is always marked', () => {
    const [, first, second] = lines(renderLegalPage(scopedPage)) as [string, string, string];
    expect(first.startsWith('a1 ')).toBe(true);
    expect(first.endsWith(`action_${'1'.repeat(32)}`)).toBe(true);
    expect(second.endsWith(`action_${'2'.repeat(32)}`)).toBe(true);
    for (const absent of ['prob', 'legality', 'consuming', 'variant', '{']) {
      expect(first).not.toContain(absent);
    }
    expect(first).toContain('order=sentry');
    expect(second).toContain('!prob=0-100%/unknown');
    expect(second).toContain('!legality=possibly_legal');
    expect(second).toContain('!consuming');
    expect(second).toContain('!variant=targeted_steal_tech');
    expect(second).toContain('{name:string}');
    expect(second).toContain('→Paris');
  });

  test('an empty page says so instead of printing a bare header', () => {
    const out = lines(renderLegalPage(decode(legalPage([], rev))));
    expect(out).toEqual([
      'rev11/t3 legal scope=all 0/0 complete',
      '(no legal actions on this page)',
    ]);
  });

  test('an alias cache drops the opaque handle column', () => {
    const out = lines(renderLegalPage(scopedPage, { [UNIT]: 'u1' }));
    expect(out[0]).toContain('scope=unit u1');
    // With a cache in hand column 0 addresses the row, so the trailing 39-char
    // handle column is gone; an un-aliased action still shows its own ID there
    // rather than inventing a positional `a1` that names something else.
    expect(out[1]?.startsWith(`action_${'1'.repeat(32)} `)).toBe(true);
    expect(out[1]?.endsWith('order=sentry')).toBe(true);
    const aliased = lines(renderLegalPage(scopedPage, { [`action_${'1'.repeat(32)}`]: 'a7' }));
    expect(aliased[1]?.startsWith('a7 ')).toBe(true);
    expect(aliased[1]).not.toContain(`action_${'1'.repeat(32)}`);
  });
});

// ---------------------------------------------------------------------------
// Grouping the global catalog (§9.1)
// ---------------------------------------------------------------------------

const globalCatalog = (): LegalActionPageEnvelope => {
  const rev = revision(11);
  const items: JsonObject[] = [];
  for (let index = 0; index < 6; index += 1) {
    items.push(
      playerAction(rev, `action_setting${String(index).padStart(25, '0')}`, {
        kind: 'player.propose_server_setting',
        operation: 'propose_server_setting',
        label: `Propose setting ${index}`,
        subjectExtra: { setting: `setting${index}` },
      })
    );
  }
  // One proposal is a gamble; it must survive the collapse individually.
  items.push(
    playerAction(rev, `action_gamble${'0'.repeat(20)}`, {
      kind: 'player.propose_server_setting',
      operation: 'propose_server_setting',
      label: 'Propose fixedlength',
      subjectExtra: { setting: 'fixedlength' },
      probability: { kind: 'unknown', minimum_percent: 0, maximum_percent: 100 },
    })
  );
  ['Alphabet', 'Bronze Working', 'Currency'].forEach((name, index) => {
    items.push(
      playerAction(rev, `action_goal${String(index).padStart(28, '0')}`, {
        kind: 'research.set_goal',
        operation: 'set_goal',
        label: `Goal ${name}`,
        target: { type: 'research', id: `research_${String(index).padStart(32, '0')}`, name },
      })
    );
  });
  items.push(
    playerAction(rev, `action_rates${'0'.repeat(21)}`, {
      kind: 'economy.set_rates',
      operation: 'set_rates',
      label: 'Set tax rates',
    })
  );
  return decode(legalPage(items, rev));
};

describe('the global catalog', () => {
  test('housekeeping collapses to one line that names its own drill-down', () => {
    const body = lines(renderLegalPage(globalCatalog())).slice(1);
    const governance = body.filter((line) => line.startsWith('governance:'));
    expect(governance).toEqual([
      'governance: 6 setting proposals — just legal --kind ' +
        'player.propose_server_setting --all',
    ]);
    const gamble = body.filter((line) => line.includes('!prob=0-100%/unknown'));
    expect(gamble).toHaveLength(1);
    expect(gamble[0]).toContain('Propose fixedlength');
  });

  test('a choice family collapses to one row that still names every choice', () => {
    const body = lines(renderLegalPage(globalCatalog())).slice(1);
    const goals = body.filter((line) => line.startsWith('research.set_goal:'));
    expect(goals).toHaveLength(1);
    for (const name of ['Alphabet', 'Bronze Working', 'Currency']) {
      expect(goals[0]).toContain(name);
    }
    expect(goals[0]).toContain('3 choices');
  });

  test('a gameplay family is never collapsed, and the tally closes the block', () => {
    const body = lines(renderLegalPage(globalCatalog())).slice(1);
    expect(
      body.some((line) => line.includes('economy.set_rates') && !line.includes('choices'))
    ).toBe(true);
    expect(body.at(-1)).toBe(
      '9 rows collapsed into 2 kinds; add --full for the flat list'
    );
  });

  test('--full prints the same rows flat, and grouping only ever saves bytes', () => {
    const grouped = lines(renderLegalPage(globalCatalog()));
    const flat = lines(renderLegalPage(globalCatalog(), null, { full: true }));
    expect(flat).toHaveLength(12);
    flat.slice(1).forEach((line, index) => {
      expect(line.startsWith(`a${index + 1} `)).toBe(true);
    });
    expect(flat.join('\n')).not.toContain('governance:');
    expect(grouped.join('\n').length).toBeLessThan(flat.join('\n').length);
  });

  test('a scoped catalog is untouched by grouping', () => {
    const rev = revision(11);
    const items = [0, 1, 2].map((index) =>
      actorAction(rev, `action_move${String(index).padStart(27, '0')}`, UNIT, {
        x: 30 + index,
      })
    );
    const out = lines(renderLegalPage(decode(scopedLegalPage(items, rev, UNIT))));
    expect(out).toHaveLength(4);
    out.slice(1).forEach((line, index) => {
      expect(line.startsWith(`a${index + 1} `)).toBe(true);
    });
  });
});

// ---------------------------------------------------------------------------
// _render_legal_compact — the branches the command cannot reach on its own
// ---------------------------------------------------------------------------

describe('renderLegalCompact', () => {
  const rev = { turn: 3, revision: 7, state_token: `state_${'0'.repeat(31)}7` };
  const base = {
    schema_version: 1,
    command: 'legal',
    kind: null,
    state_revision: rev,
    catalog_total: 3,
    pages_read: 1,
    matched: 3,
    offset: 0,
    limit: 64,
    shown: 0,
    truncated: true,
    has_more: false,
    next_offset: null,
    byte_limited: false,
    oversized_single: false,
    hidden_kinds: {},
    actions: [],
  } as const satisfies LegalCompactResult;

  test('an offset past the end says so, and names the flag to drop', () => {
    const out = lines(renderLegalCompact({ ...base, offset: 9 }, null));
    expect(out[1]).toBe('(offset 9 is past the 3 matched actions; drop --offset)');
  });

  test('an empty scope says the catalog is empty, not that an offset overran', () => {
    const out = lines(renderLegalCompact({ ...base, matched: 0, catalog_total: 0 }, null));
    expect(out[0]).toBe('rev7/t3 legal scope=all 0/0 matched (catalog 0 complete, pages 1)');
    expect(out[1]).toBe('(no legal actions in this catalog)');
  });

  test('a relation window names both parameters in the not-shown remedy', () => {
    const scope = {
      actor_id: PLAYER,
      actor_type: 'player',
      target_id: `relation_${'c'.repeat(32)}`,
      target_type: 'diplomatic_relation',
    } as const;
    const result: LegalCompactResult = {
      ...base,
      kind: 'diplomacy.propose',
      shown: 1,
      hidden_kinds: { 'diplomacy.cancel': 2 },
      actions: [
        {
          action_id: 'action_one',
          kind: 'diplomacy.propose',
          label: 'Propose peace',
          subject: { operation: 'propose' },
          target: null,
          argument_schema: {},
        },
      ],
    };
    const out = lines(
      renderLegalCompact(result, scope, { [PLAYER]: 'p1', [scope.target_id]: 'r1' })
    );
    expect(out.at(-1)).toBe(
      'not shown (earlier in this catalog): diplomacy.cancel (2) — ' +
        'just legal --kind diplomacy.propose --actor_id p1 --target_id r1 --all'
    );
  });

  test('more than eight withheld kinds are elided with a single ellipsis', () => {
    const hidden = Object.fromEntries(
      Array.from({ length: 9 }, (_value, index) => [`unit.kind${index}`, index + 1])
    );
    const result: LegalCompactResult = {
      ...base,
      shown: 1,
      hidden_kinds: hidden,
      actions: [
        {
          action_id: 'action_one',
          kind: 'unit.order',
          label: 'Move',
          subject: { operation: 'move' },
          target: null,
          argument_schema: {},
        },
      ],
    };
    const line = lines(renderLegalCompact(result, null)).at(-1) ?? '';
    expect(line).toContain('unit.kind0 (1)');
    expect(line).toContain('unit.kind7 (8)');
    expect(line).not.toContain('unit.kind8');
    expect(line).toContain('… — just legal --all');
  });
});

// ---------------------------------------------------------------------------
// The kind taxonomy
// ---------------------------------------------------------------------------

describe('the kind selector', () => {
  const rev = revision(7);

  test('the column prints kind/operation only when it adds something', () => {
    expect(descriptorKindKey(actorAction(rev, 'a', UNIT))).toBe('unit.order/move');
    expect(
      descriptorKindKey(actorAction(rev, 'a', UNIT, { kind: 'unit.sentry', operation: 'sentry' }))
    ).toBe('unit.sentry');
  });

  test('a bare kind selects every operation; the suffixed form selects one', () => {
    const move = actorAction(rev, 'a', UNIT);
    expect(kindSelectorMatches(move, 'unit.order')).toBe(true);
    expect(kindSelectorMatches(move, 'unit.order/move')).toBe(true);
    expect(kindSelectorMatches(move, 'unit.order/goto')).toBe(false);
    expect(kindSelectorMatches(move, 'unit.found_city')).toBe(false);
  });

  test('an operation-less subject still answers to the kind own tail', () => {
    const sentry: JsonObject = {
      action_id: 'a',
      kind: 'unit.sentry',
      label: 'Sentry',
      subject: {},
      arguments_schema: {},
      state_revision: rev,
    };
    expect(kindSelectorMatches(sentry, 'unit.sentry/sentry')).toBe(true);
    expect(kindSelectorMatches(sentry, 'unit.sentry/fortify')).toBe(false);
  });
});

describe('aliasSpan', () => {
  test('a consecutive run is a span; anything ragged is listed', () => {
    expect(aliasSpan(['a3', 'a4', 'a5'])).toBe('a3..a5');
    expect(aliasSpan(['a3'])).toBe('a3');
    expect(aliasSpan(['a3', 'a5'])).toBe('a3 a5');
    expect(aliasSpan(['a3', `action_${'1'.repeat(32)}`])).toBe(
      `a3 action_${'1'.repeat(32)}`
    );
  });
});

// ---------------------------------------------------------------------------
// The command
// ---------------------------------------------------------------------------

interface Fixture {
  readonly layer: Layer.Layer<SessionStore | PrivateFs | V2Client>;
  readonly requests: ReadonlyArray<RecordedRequest>;
  /** Where `_mirror_page` projects this seat's files. */
  readonly mirror: string;
}

const commandFixture = (plan: ReadonlyArray<FakeRoute>): Fixture => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const target = path.join(scratch.workspace.stateRoot, FIXTURE_GAME_ID, 'seat.json');
  Effect.runSync(scratch.files.writeJson(target, sessionFile()));
  const store = sessionStoreFor(scratch.workspace, scratch.files, v2StateSchema, {});
  const recorder = recordingFetch(plan);
  const client = v2ClientFor(httpFor(recorder.fetch), () => Effect.void);
  return {
    layer: Layer.mergeAll(
      Layer.succeed(SessionStore, store),
      Layer.succeed(PrivateFs, scratch.files),
      Layer.succeed(V2Client, client)
    ),
    requests: recorder.requests,
    mirror: Effect.runSync(mirrorDir(target)),
  };
};

const none = (): DualSpelling<string> => ({
  dashed: Option.none(),
  underscored: Option.none(),
});

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

const capture = async (
  options: LegalOptions,
  fixture: Fixture
): Promise<{ readonly out: ReadonlyArray<string>; readonly error: string | null }> => {
  const out: string[] = [];
  const original = console.log;
  console.log = (...parts: ReadonlyArray<unknown>) => out.push(parts.join(' '));
  try {
    const either = await Effect.runPromise(
      Effect.either(Effect.provide(runLegal(options), fixture.layer))
    );
    return {
      out,
      error: Either.isLeft(either) ? (either.left as { message: string }).message : null,
    };
  } finally {
    console.log = original;
  }
};

describe('play legal', () => {
  const rev = revision(7);

  test('one page renders as text and --json prints the validated envelope', async () => {
    const payload = legalPage(
      [
        {
          action_id: 'action_opaque',
          kind: 'phase.end',
          label: 'End phase',
          subject: { operation: 'end' },
          arguments_schema: { type: 'object' },
          state_revision: rev,
        },
      ],
      rev
    );
    const text = await capture(legalOptions(), commandFixture([{ body: payload }]));
    expect(text.error).toBeNull();
    expect(text.out[0]).toBe('rev7/t3 legal scope=all 1/1 complete');
    expect(text.out[0]?.startsWith('{')).toBe(false);

    const json = await capture(
      legalOptions({ json: true }),
      commandFixture([{ body: payload }])
    );
    expect(json.error).toBeNull();
    expect(json.out).toHaveLength(1);
    expect(JSON.parse(json.out[0] ?? '') as unknown).toEqual(
      JSON.parse(JSON.stringify(decode(payload))) as unknown
    );
  });

  test('the flag matrix refuses before any authenticated request is sent', async () => {
    for (const [options, expected] of [
      [legalOptions({ all: true }), 'legal --all needs a scope'],
      [legalOptions({ kind: 'phase.end' }), 'use --kind ACTION_KIND and --all together'],
      [legalOptions({ offset: '4' }), 'legal --offset requires --all'],
    ] as ReadonlyArray<readonly [LegalOptions, string]>) {
      const fixture = commandFixture([]);
      const { error } = await capture(options, fixture);
      expect(error).toContain(expected);
      expect(fixture.requests).toHaveLength(0);
    }
  });

  test('the unscoped --all refusal names both scoped forms', async () => {
    const { error } = await capture(legalOptions({ all: true }), commandFixture([]));
    expect(error).toContain('--kind ACTION_KIND --all');
    expect(error).toContain('--actor_id ACTOR_ID');
  });

  test('a malformed kind lists the kinds this seat has read, without a request', async () => {
    const fixture = commandFixture([]);
    const { error } = await capture(
      legalOptions({ kind: 'bogus', all: true }),
      fixture
    );
    expect(error).toContain('is not an action kind');
    expect(error).toContain('this seat has read no catalog yet');
    expect(fixture.requests).toHaveLength(0);
  });

  test('--cursor is passed through and is the only page option', async () => {
    const cursor = `cursor_${'a'.repeat(32)}`;
    const fixture = commandFixture([{ body: legalPage([], rev) }]);
    const { error } = await capture(legalOptions({ cursor }), fixture);
    expect(error).toBeNull();
    expect(fixture.requests[0]?.url).toContain(`cursor=${cursor}`);

    const clash = commandFixture([]);
    const refused = await capture(
      legalOptions({ cursor, actorId: some(UNIT) }),
      clash
    );
    expect(refused.error).toBe('legal cursor must be the only page option');
    expect(clash.requests).toHaveLength(0);
  });

  /**
   * `_read_legal_page` (client.py:7895) calls `_mirror_page` unconditionally,
   * and that is the *only* path that reaches `_update_options`.  A port that
   * skipped it left `state/options/<alias>.txt` and the actions projection
   * frozen at whatever the workspace shipped with, however many `legal --all`
   * and `do` calls followed — a `show options/u1` divergence that grows with
   * every command, in a file `V2_PROTOCOL_CARD` tells the agent to read.
   */
  test('every page read projects itself into state/options, as _read_legal_page does', async () => {
    const action = actorAction(rev, `action_${'7'.repeat(32)}`, UNIT, {
      operation: 'sentry',
      label: 'Sentry',
    });
    const fixture = commandFixture([
      { body: scopedLegalPage([action], rev, UNIT) },
      { body: scopedLegalPage([action], rev, UNIT) },
    ]);
    const { error } = await capture(
      legalOptions({ actorId: some(UNIT), all: true }),
      fixture
    );
    expect(error).toBeNull();
    const projected = path.join(fixture.mirror, 'state', 'options', 'u1.txt');
    expect(fs.existsSync(projected)).toBe(true);
    const text = fs.readFileSync(projected, 'utf8');
    expect(text).toContain('# rev 7 turn 3');
    expect(text).toContain('# actor unit u1');
    expect(text).toContain('Sentry');
  });
});

// ---------------------------------------------------------------------------
// The served contract
// ---------------------------------------------------------------------------

describe('the legal-actions OpenAPI contract', () => {
  const contract = JSON.parse(
    fs.readFileSync(
      path.join(import.meta.dir, '..', '..', 'play', 'docs', 'full-control-v2.openapi.json'),
      'utf8'
    )
  ) as Record<string, never>;

  const at = (...parts: ReadonlyArray<string>): unknown =>
    parts.reduce<unknown>(
      (value, part) =>
        typeof value === 'object' && value !== null
          ? (value as Record<string, unknown>)[part]
          : undefined,
      contract
    );

  test('the legal-actions route returns the page envelope, items closed', () => {
    expect(
      at(
        'paths',
        '/v2/games/{game_id}/me/legal-actions',
        'get',
        'responses',
        '200',
        'content',
        'application/json',
        'schema',
        '$ref'
      )
    ).toBe('#/components/schemas/LegalActionPageEnvelope');
    expect(
      at('components', 'schemas', 'LegalActionPage', 'properties', 'items', 'items', '$ref')
    ).toBe('#/components/schemas/LegalActionDescriptor');
  });

  test('a descriptor requires exactly the six fields this unit reads', () => {
    const required = at('components', 'schemas', 'LegalActionDescriptor', 'required');
    expect(new Set(required as ReadonlyArray<string>)).toEqual(
      new Set(['action_id', 'kind', 'label', 'subject', 'arguments_schema', 'state_revision'])
    );
    expect(
      at('components', 'schemas', 'LegalActionDescriptor', 'properties', 'kind', '$ref')
    ).toBe('#/components/schemas/ActionKind');
    // The client's own kind regex is the served pattern, so `--kind` cannot
    // drift away from what the supervisor will actually emit.
    expect(at('components', 'schemas', 'ActionKind', 'pattern')).toBe(ACTION_KIND_RE.source);
    expect(at('components', 'schemas', 'LegalActionSubject', 'required')).toContain(
      'operation'
    );
  });
});
