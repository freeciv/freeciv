/**
 * `state/options/<alias>.txt` — the projected legal-action catalog.
 *
 * Ports `OptionTests` from `play/tests/test_state_mirror.py:521-841`.
 *
 * Two invariants carry the safety argument and are asserted here rather than
 * implied: the mirror never *mints* an `aN` name (a fabricated one would be
 * typable and would resolve to a different capability), and two actions this
 * table renders alike stay two addressable rows.
 */
import * as path from 'node:path';
import * as fs from 'node:fs';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either } from 'effect';
import type { JsonObject, JsonValue } from 'src/schema/primitives';
import type { PrivateFs } from 'src/services/private-fs';
import { mirrorDir, readMirror } from 'src/services/mirror';
import { updateFromPage } from 'src/services/mirror/update-page';
import { actionFlags, argumentNames, targetText } from 'src/render/mirror/actions';
import { scratchWorkspace, type Scratch } from 'test/_fixtures';

const GAME_ID = 'game_12345678901234567890';
const AGENT_ID = 'agent_test-controller';
const UNIT_A = `unit_${'a'.repeat(32)}`;
const UNIT_B = `unit_${'b'.repeat(32)}`;

const revision = (number = 7, turn = 3): JsonObject => ({
  turn,
  revision: number,
  state_token: `state_token_${String(number).padStart(32, '0')}`,
});

interface PageOptions {
  readonly cursor?: string | null;
  readonly total?: number;
  readonly scope?: JsonObject;
  readonly complete?: boolean;
}

/** The CPython test module's `page()` builder, including its scope extras. */
const legalPage = (
  rev: JsonObject,
  items: ReadonlyArray<JsonValue>,
  options: PageOptions = {}
): JsonObject => ({
  schema_version: 2,
  control_protocol: 'full-control-v2',
  game_id: GAME_ID,
  agent_id: AGENT_ID,
  state_revision: rev,
  page: {
    section: 'legal_actions',
    items,
    total_items: options.total ?? items.length,
    next_cursor: options.cursor ?? null,
    cursor_expires_at: null,
    ...(options.scope === undefined
      ? {}
      : {
          scope: options.scope,
          catalog_id: `catalog_${'d'.repeat(32)}`,
          catalog_complete: options.complete ?? (options.cursor ?? null) === null,
        }),
  },
});

const descriptor = (
  rev: JsonObject,
  actionId: string,
  overrides: { readonly kind?: string; readonly subject?: JsonObject; readonly schema?: JsonObject } = {}
): JsonObject => ({
  action_id: actionId,
  kind: overrides.kind ?? 'unit.found_city',
  label: 'Found city',
  subject: overrides.subject ?? {
    actor: { type: 'unit', id: UNIT_A },
    target: { type: 'tile', id: 'tile_x', x: 31, y: 72 },
    operation: 'found_city',
    variant: null,
    consuming: false,
    legality: 'legal',
    probability: { kind: 'exact', minimum_percent: 100.0, maximum_percent: 100.0 },
  },
  arguments_schema: overrides.schema ?? {},
  state_revision: rev,
});

const scratches: Scratch[] = [];
afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

interface Mirror {
  readonly dir: string;
  readonly write: (
    payload: JsonObject,
    aliases?: Readonly<Record<string, string>> | null
  ) => ReadonlyArray<string>;
  readonly options: (name: string) => string;
  readonly optionFiles: () => ReadonlyArray<string>;
}

const freshMirror = (): Mirror => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const dir = Effect.runSync(
    mirrorDir(path.join(scratch.workspace.stateRoot, GAME_ID, 'codex-test.json'))
  );
  const run = <A, E>(effect: Effect.Effect<A, E, PrivateFs>): Either.Either<A, E> =>
    Effect.runSync(Effect.either(Effect.provide(effect, scratch.layer)));
  return {
    dir,
    write: (payload, aliases) =>
      right(
        run(
          updateFromPage(
            dir,
            'legal',
            payload,
            aliases === undefined || aliases === null ? {} : { aliases }
          )
        )
      ),
    options: (name) => right(run(readMirror(dir, ['state', 'options', name]))) ?? '',
    optionFiles: () => fs.readdirSync(path.join(dir, 'state', 'options')).sort(),
  };
};

const right = <A, E>(either: Either.Either<A, E>): A => {
  if (Either.isLeft(either)) {
    const failure: unknown = either.left;
    throw new Error(
      `expected success, got: ${
        typeof failure === 'object' && failure !== null && 'message' in failure
          ? String((failure as { message: unknown }).message)
          : String(failure)
      }`
    );
  }
  return either.right;
};

/** Every row of a catalog file, split on tabs, headers and notes dropped. */
const rows = (text: string): ReadonlyArray<ReadonlyArray<string>> =>
  text
    .split('\n')
    .filter((line) => line !== '' && !line.startsWith('#') && !line.startsWith('alias'))
    .map((line) => line.split('\t').map((cell) => cell.trim()));

describe('the flags column', () => {
  test('defaults are omitted and non-defaults are always visible', () => {
    const mirror = freshMirror();
    const rev = revision(9);
    const certain = descriptor(rev, 'action_1');
    const gamble = descriptor(rev, 'action_2', {
      kind: 'unit.spy_steal_tech',
      subject: {
        actor: { type: 'unit', id: UNIT_A },
        target: { type: 'city', name: 'Paris' },
        operation: 'spy_steal_tech',
        variant: 'targeted',
        consuming: true,
        legality: 'conditional',
        probability: { kind: 'unknown', minimum_percent: 0.0, maximum_percent: 100.0 },
      },
      schema: {
        type: 'object',
        properties: { name: { type: 'string' } },
        required: ['name'],
      },
    });
    mirror.write(
      legalPage(rev, [certain, gamble], {
        scope: { actor_id: UNIT_A, actor_type: 'unit' },
      }),
      { [UNIT_A]: 'u1', action_1: 'a1', action_2: 'a2' }
    );
    const text = mirror.options('u1.txt');
    const named = text.split('\n').filter((line) => /^a\d/.test(line));
    const [first, second] = [named[0] ?? '', named[1] ?? ''];
    expect(first.startsWith('a1')).toBe(true);
    // exact 100/100, legal, non-consuming, no variant, actor == scope.
    expect(first.split('\t')[5]?.trim()).toBe('-');
    expect(first.split('\t')[3]?.trim()).toBe('-');
    expect(second.startsWith('a2')).toBe(true);
    const flags = second.split('\t')[5] ?? '';
    expect(flags).toContain('p=unknown:0-100%');
    expect(flags).toContain('legality=conditional');
    expect(flags).toContain('consuming');
    expect(flags).toContain('variant=targeted');
    expect(second).toContain('{name*}');
    expect(text).toContain('# actor unit u1');
    expect(text).toContain('actions 2/2 complete');
  });

  test('an actor outside the page scope is always named', () => {
    const mirror = freshMirror();
    const rev = revision(9);
    const item = descriptor(rev, 'action_1');
    (item['subject'] as Record<string, unknown>)['actor'] = { type: 'unit', id: UNIT_B };
    mirror.write(legalPage(rev, [item], { scope: { actor_id: UNIT_A, actor_type: 'unit' } }), {
      [UNIT_A]: 'u1',
    });
    expect(mirror.options('u1.txt')).toContain(`actor=unit:${UNIT_B}`);
  });

  test('a gold cost the target carries is priced on the row', () => {
    expect(
      actionFlags({ target: { type: 'unit', gold_cost: 120 }, actor: { type: 'unit', id: 'x' } }, 'x')
    ).toBe('gold=120');
    expect(actionFlags({ gold_cost: 7 }, null)).toBe('gold=7');
  });

  test('a probability with no bounds prints its kind alone', () => {
    expect(
      actionFlags({ probability: { kind: 'unknown', minimum_percent: null, maximum_percent: null } }, null)
    ).toBe('p=unknown');
    expect(
      actionFlags(
        { probability: { kind: 'exact', minimum_percent: 30, maximum_percent: 30 } },
        null
      )
    ).toBe('p=exact:30%');
  });

  test('a subject with nothing but defaults renders a dash', () => {
    expect(actionFlags({ legality: 'legal', consuming: false, variant: null }, null)).toBe('-');
  });
});

describe('the target and args columns', () => {
  test('coordinates lead, then a name, then the bare type', () => {
    expect(targetText({ target: { type: 'tile', x: 31, y: 72 } })).toBe('31,72');
    expect(targetText({ target: { type: 'city', name: 'Paris' } })).toBe('Paris');
    expect(targetText({ target: { type: 'city' } })).toBe('city');
    expect(targetText({ target: null })).toBe('-');
    expect(targetText({})).toBe('-');
  });

  test('required argument names are starred and an empty schema is a dash', () => {
    expect(
      argumentNames({ properties: { name: {}, size: {} }, required: ['name'] })
    ).toBe('{name* size}');
    expect(argumentNames({ properties: {} })).toBe('-');
    expect(argumentNames({})).toBe('-');
    expect(argumentNames(null)).toBe('-');
  });
});

describe('the catalog file', () => {
  test('an alias can never escape the options directory', () => {
    const mirror = freshMirror();
    const rev = revision(9);
    mirror.write(
      legalPage(rev, [descriptor(rev, 'action_1')], {
        scope: { actor_id: UNIT_A, actor_type: 'unit' },
      }),
      { [UNIT_A]: '../../escape' }
    );
    expect(mirror.optionFiles()).toEqual(['escape.txt']);
  });

  test('unscoped catalogs accumulate at one revision', () => {
    const mirror = freshMirror();
    const rev = revision(9);
    mirror.write(legalPage(rev, [descriptor(rev, 'action_1', { kind: 'phase.end' })]), {
      action_1: 'a1',
    });
    mirror.write(
      legalPage(rev, [descriptor(rev, 'action_2', { kind: 'research.set_target' })]),
      { action_2: 'a2' }
    );
    const text = mirror.options('unscoped.txt');
    expect(text).toContain('phase.end');
    expect(text).toContain('research.set_target');
    expect(text).toContain('a1');
    expect(text).toContain('a2');
    expect(text).toContain('# unscoped catalog (union of the kinds fetched at this rev)');
  });

  test('a complete actor catalog replaces the pages staged before it', () => {
    const mirror = freshMirror();
    const rev = revision(9);
    const items = [1, 2, 3, 4].map((index) =>
      descriptor(rev, `action_${index}`, { kind: 'unit.order' })
    );
    const scope = { actor_id: UNIT_A, actor_type: 'unit' };
    // Page one, staged: the CLI has assigned no action alias yet.
    mirror.write(
      legalPage(rev, items.slice(0, 2), {
        scope,
        cursor: `cursor_${'c'.repeat(32)}`,
        total: 4,
      }),
      { [UNIT_A]: 'u1' }
    );
    const staged = mirror.options('u1.txt');
    expect(rows(staged).map((row) => row[0])).toEqual(['-', '-']);
    expect(staged).toContain('no action alias resolves at this revision');
    // The promoting page carries the whole catalog and its names.
    mirror.write(legalPage(rev, items, { scope, total: 4 }), {
      [UNIT_A]: 'u1',
      action_1: 'a1',
      action_2: 'a2',
      action_3: 'a3',
      action_4: 'a4',
    });
    const text = mirror.options('u1.txt');
    expect(rows(text)).toHaveLength(4);
    expect(rows(text).map((row) => row[0])).toEqual(['a1', 'a2', 'a3', 'a4']);
    expect(text).toContain('actions 4/4 complete');
    expect(text).not.toContain('no action alias resolves');
  });

  test('a target-scoped catalog never erases the actor’s full menu', () => {
    const mirror = freshMirror();
    const rev = revision(9);
    const aliases = { [UNIT_A]: 'u1', action_1: 'a1', action_2: 'a2', action_3: 'a3' };
    mirror.write(
      legalPage(
        rev,
        [
          descriptor(rev, 'action_1', { kind: 'unit.found_city' }),
          descriptor(rev, 'action_2', { kind: 'unit.order' }),
        ],
        { scope: { actor_id: UNIT_A, actor_type: 'unit' }, total: 2 }
      ),
      aliases
    );
    mirror.write(
      legalPage(rev, [descriptor(rev, 'action_3', { kind: 'unit.bribe' })], {
        scope: { actor_id: UNIT_A, actor_type: 'unit', target_id: UNIT_B },
        total: 1,
      }),
      aliases
    );
    const text = mirror.options('u1.txt');
    for (const alias of ['a1', 'a2', 'a3']) expect(text).toContain(alias);
  });

  test('identical-looking capabilities stay separate rows', () => {
    const mirror = freshMirror();
    const rev = revision(9);
    const scope = { actor_id: UNIT_A, actor_type: 'unit' };
    const twins = [
      descriptor(rev, 'action_1', {
        kind: 'unit.steal_tech',
        subject: { operation: 'steal', subtarget: 'Alphabet' },
      }),
      descriptor(rev, 'action_2', {
        kind: 'unit.steal_tech',
        subject: { operation: 'steal', subtarget: 'Pottery' },
      }),
    ];
    mirror.write(legalPage(rev, twins, { scope, total: 2 }), {
      [UNIT_A]: 'u1',
      action_1: 'a1',
      action_2: 'a2',
    });
    const text = mirror.options('u1.txt');
    expect(rows(text).map((row) => row[0])).toEqual(['a1', 'a2']);
    expect(text).toContain('actions 2/2 complete');
  });

  test('no options file ever mints an action alias', () => {
    const cases: ReadonlyArray<{
      readonly aliases: Readonly<Record<string, string>> | null;
      readonly unnamed: number;
    }> = [
      // No alias map at all: every action row must stay unnamed.
      { aliases: null, unnamed: 3 },
      // A partial map: only the middle action carries a real alias.
      { aliases: { [UNIT_A]: 'u1', action_2: 'a9' }, unnamed: 2 },
      // A map that names the actor but no action at all.
      { aliases: { [UNIT_A]: 'u1' }, unnamed: 3 },
    ];
    for (const { aliases, unnamed } of cases) {
      const mirror = freshMirror();
      const rev = revision(9);
      const items = [
        descriptor(rev, 'action_1', { kind: 'unit.found_city' }),
        descriptor(rev, 'action_2', { kind: 'unit.order' }),
        descriptor(rev, 'action_3', { kind: 'unit.fortify' }),
      ];
      mirror.write(
        legalPage(rev, items, { scope: { actor_id: UNIT_A, actor_type: 'unit' } }),
        aliases
      );
      const name = aliases === null ? `u.${UNIT_A.slice(5, 11)}.txt` : 'u1.txt';
      const text = mirror.options(name);
      const known = new Set(Object.values(aliases ?? {}));
      const minted = (text.match(/\ba\d+\b/g) ?? []).filter((token) => !known.has(token));
      expect(minted).toEqual([]);
      expect(rows(text)).toHaveLength(items.length);
      expect(rows(text).filter((row) => row[0] === '-')).toHaveLength(unnamed);
    }
  });

  test('an unexecutable catalog names the command that fixes it', () => {
    const mirror = freshMirror();
    const rev = revision(9);
    mirror.write(
      legalPage(rev, [descriptor(rev, 'action_1')], {
        scope: { actor_id: UNIT_A, actor_type: 'unit' },
      }),
      { [UNIT_A]: 'u1' }
    );
    const text = mirror.options('u1.txt');
    expect(text).toContain('no action alias resolves at this revision');
    expect(text).toContain('just legal --actor_id u1 --all');
  });

  test('an unexecutable unscoped catalog names the kind enumeration instead', () => {
    const mirror = freshMirror();
    const rev = revision(9);
    mirror.write(legalPage(rev, [descriptor(rev, 'action_1')]), null);
    expect(mirror.options('unscoped.txt')).toContain('just legal --kind KIND --all');
  });

  test('a named catalog carries no remedy note', () => {
    const mirror = freshMirror();
    const rev = revision(9);
    mirror.write(
      legalPage(rev, [descriptor(rev, 'action_1')], {
        scope: { actor_id: UNIT_A, actor_type: 'unit' },
      }),
      { [UNIT_A]: 'u1', action_1: 'a1' }
    );
    const text = mirror.options('u1.txt');
    expect(text).not.toContain('no action alias resolves');
    expect(text).toContain('a1');
  });

  test('the catalog refreshes the delta digest with the file it wrote', () => {
    const mirror = freshMirror();
    const rev = revision(9);
    const written = mirror.write(
      legalPage(rev, [descriptor(rev, 'action_1')], {
        scope: { actor_id: UNIT_A, actor_type: 'unit' },
      }),
      { [UNIT_A]: 'u1', action_1: 'a1' }
    );
    expect(written.map((file) => path.relative(mirror.dir, file))).toEqual([
      path.join('state', 'options', 'u1.txt'),
      path.join('state', 'delta.md'),
    ]);
  });

  test('an older catalog page never overwrites a newer one', () => {
    const mirror = freshMirror();
    const scope = { actor_id: UNIT_A, actor_type: 'unit' };
    mirror.write(
      legalPage(revision(9), [descriptor(revision(9), 'action_1', { kind: 'unit.found_city' })], {
        scope,
      }),
      { [UNIT_A]: 'u1', action_1: 'a1' }
    );
    expect(
      mirror.write(
        legalPage(revision(7), [descriptor(revision(7), 'action_2', { kind: 'unit.disband' })], {
          scope,
        }),
        { [UNIT_A]: 'u1', action_2: 'a2' }
      )
    ).toEqual([]);
    const text = mirror.options('u1.txt');
    expect(text.startsWith('# rev 9 turn 3')).toBe(true);
    expect(text).not.toContain('unit.disband');
  });
});
