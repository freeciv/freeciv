/**
 * `use` — one workspace, one seat.
 *
 * Ports `test_use_binds_by_path_or_game_id_and_fails_closed_when_ambiguous`,
 * `test_use_on_an_unbound_workspace_names_join`, the `use` row of
 * `test_a_preconfigured_workspace_answers_every_v2_command_with_join`, and the
 * shim's `not bound to a seat` assertion from
 * `test_the_play_shim_is_the_same_cli_as_client_py`.
 *
 * `use` is the only command that prints a session path, and it prints it in the
 * exact workspace-relative form it accepts back — every refusal below is
 * checked for the command that repairs it, because that string is the agent's
 * whole recovery surface.
 */
import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Command } from '@effect/cli';
import { BunContext } from '@effect/platform-bun';
import { Effect, Either, Layer } from 'effect';
import { FULL_CONTROL_V2, SEAT_BINDING_NAME } from 'src/constants';
import {
  commandUse,
  resolveUseTarget,
  useCommand,
  workspaceRelative,
} from 'src/commands/use.cmd';
import {
  PrivateFs,
  Workspace,
  privateFsFor,
  type PrivateFsApi,
  type WorkspacePaths,
} from 'src/services/private-fs';
import {
  SessionStore,
  emptyV2ClientState,
  sessionStoreFor,
  type SessionStoreApi,
} from 'src/services/session-store';
import type { JsonObject } from 'src/schema/primitives';

const FIRST = 'game_Hsit9YEuBjKdJPPouFoGVYlk';
const SECOND = 'game_9SecondBoundGame00000000';
const MISSING = 'game_NeverJoinedByThisSeat00';

const roots: string[] = [];

afterEach(() => {
  while (roots.length > 0) {
    const root = roots.pop();
    if (root !== undefined) fs.rmSync(root, { recursive: true, force: true });
  }
});

const schema = {
  empty: emptyV2ClientState,
  validate: () => Effect.void,
  cursorExpired: (expiresAt: string | null): boolean =>
    expiresAt === null ? false : Date.parse(expiresAt) <= Date.now(),
};

interface Bench {
  readonly root: string;
  readonly workspace: WorkspacePaths;
  readonly files: PrivateFsApi;
  readonly store: SessionStoreApi;
  readonly layer: Layer.Layer<Workspace | PrivateFs | SessionStore>;
  /** Write one mode-0600 session file and return its absolute path. */
  readonly seat: (gameId: string, name: string, overrides?: JsonObject) => string;
}

const bench = (): Bench => {
  const root = fs.realpathSync.native(fs.mkdtempSync(path.join(os.tmpdir(), 'play-cli-u02-')));
  roots.push(root);
  const workspace: WorkspacePaths = { root, stateRoot: path.join(root, '.sessions') };
  fs.mkdirSync(workspace.stateRoot, { mode: 0o700, recursive: true });
  const files = privateFsFor(workspace);
  const store = sessionStoreFor(workspace, files, schema, {});
  return {
    root,
    workspace,
    files,
    store,
    layer: Layer.mergeAll(
      Layer.succeed(Workspace, workspace),
      Layer.succeed(PrivateFs, files),
      Layer.succeed(SessionStore, store)
    ),
    seat: (gameId, name, overrides = {}) => {
      const target = path.join(workspace.stateRoot, gameId, `${name}.json`);
      Effect.runSync(
        files.writeJson(target, {
          schema_version: 1,
          control_protocol: FULL_CONTROL_V2,
          game_id: gameId,
          agent_id: 'agent-v2',
          agent_token: 'agent-v2-secret',
          service_url: 'http://127.0.0.1:8765',
          controller_label: name,
          ...overrides,
        })
      );
      return target;
    },
  };
};

interface Captured {
  readonly out: string;
  readonly result: Either.Either<void, { readonly message: string }>;
}

const use = async (
  fixture: Bench,
  target = '',
  options: { readonly json?: boolean; readonly env?: Record<string, string | undefined> } = {}
): Promise<Captured> => {
  const lines: string[] = [];
  const originalLog = console.log;
  console.log = (...parts: ReadonlyArray<unknown>) => lines.push(parts.join(' '));
  try {
    const result = await Effect.runPromise(
      Effect.either(
        Effect.provide(
          commandUse({ target, json: options.json ?? false }, options.env ?? {}),
          fixture.layer
        )
      )
    );
    return { out: lines.join('\n'), result };
  } finally {
    console.log = originalLog;
  }
};

const failure = <A>(either: Either.Either<A, { readonly message: string }>): string => {
  expect(Either.isLeft(either)).toBe(true);
  return Either.isLeft(either) ? either.left.message : '';
};

const ok = <A, E>(either: Either.Either<A, E>): void => {
  if (Either.isLeft(either)) {
    throw new Error(`expected success, got ${JSON.stringify(either.left)}`);
  }
};

const boundGame = (fixture: Bench): string => {
  const value = JSON.parse(
    fs.readFileSync(path.join(fixture.workspace.stateRoot, SEAT_BINDING_NAME), 'utf8')
  ) as JsonObject;
  return String(value['game_id']);
};

// ---------------------------------------------------------------------------

describe('_workspace_relative', () => {
  test('a state path is spelled the way `use` accepts it back', () => {
    const fixture = bench();
    const seat = fixture.seat(FIRST, 'codex-first-model');
    expect(workspaceRelative(fixture.workspace, seat)).toBe(
      path.join('.sessions', FIRST, 'codex-first-model.json')
    );
  });

  test('a path outside the workspace is left absolute rather than mangled', () => {
    const fixture = bench();
    expect(workspaceRelative(fixture.workspace, '/elsewhere/session.json')).toBe(
      '/elsewhere/session.json'
    );
  });
});

describe('_resolve_use_target', () => {
  const resolve = (fixture: Bench, value: string) =>
    Effect.runSync(
      Effect.either(resolveUseTarget(fixture.workspace, fixture.files, fixture.store, value))
    );

  test('a game ID with exactly one seat resolves to that seat', () => {
    const fixture = bench();
    const seat = fixture.seat(FIRST, 'codex-first-model');
    expect(resolve(fixture, FIRST)).toEqual(Either.right(seat));
  });

  test('a game this workspace never joined names the join command', () => {
    expect(failure(resolve(bench(), MISSING))).toBe(
      `this workspace holds no joined seat for ${MISSING}. Join one with ` +
        `\`just join --game_id ${MISSING} --name HARNESS-MODEL\`.`
    );
  });

  test('two seats in one game fail closed, listing both commands', () => {
    const fixture = bench();
    const first = fixture.seat(FIRST, 'codex-first-model');
    const second = fixture.seat(FIRST, 'codex-sibling-model');
    const message = failure(resolve(fixture, FIRST));
    expect(message).toContain(`${FIRST} has 2 joined seats in this workspace;`);
    expect(message).toContain('name the one you are playing:');
    expect(message).toContain(`\`just use ${workspaceRelative(fixture.workspace, first)}\``);
    expect(message).toContain(`\`just use ${workspaceRelative(fixture.workspace, second)}\``);
  });

  test('a workspace-relative path resolves without being a game ID', () => {
    const fixture = bench();
    const seat = fixture.seat(FIRST, 'codex-first-model');
    expect(resolve(fixture, path.join('.sessions', FIRST, 'codex-first-model.json'))).toEqual(
      Either.right(seat)
    );
  });

  test('a path outside PLAY_STATE_DIR is refused', () => {
    const fixture = bench();
    expect(failure(resolve(fixture, '/etc/passwd'))).toBe(
      'private state files must stay inside PLAY_STATE_DIR'
    );
  });
});

describe('bare `use`', () => {
  /**
   * `(getattr(args, "target", "") or "").strip()` uses CPython's whitespace
   * class, which is not `String.prototype.trim`'s: the four ASCII separators
   * and NEL are blank to Python and not to JavaScript, and `﻿` is the
   * reverse.  Getting it wrong decides whether `use` reports the bound seat or
   * goes looking for a session file named after a control character.
   */
  for (const [character, label] of [
    ['', 'FS'],
    ['', 'US'],
    ['', 'NEL'],
  ] as const) {
    test(`a target of a lone ${label} is blank, so bare \`use\` answers`, async () => {
      const message = failure((await use(bench(), character)).result);
      expect(message).toContain('this workspace is not bound to a seat.');
    });
  }

  test('a target of a lone ZWNBSP is NOT blank, so it is resolved as a path', async () => {
    const message = failure((await use(bench(), '﻿')).result);
    expect(message).not.toContain('this workspace is not bound to a seat.');
  });

  test('an unbound workspace names the join command', async () => {
    const { result } = await use(bench());
    const message = failure(result);
    expect(message).toBe(
      'this workspace is not bound to a seat. Join one with ' +
        '`just join --game_id GAME_ID --name HARNESS-MODEL`, or bind ' +
        'a seat you already joined with `just use GAME_ID`.'
    );
    expect(message).toContain('not bound to a seat');
    expect(message).toContain('just join --game_id GAME_ID');
  });

  test('a pre-configured workspace names bare `just join`, never the generic form', async () => {
    const fixture = bench();
    fs.writeFileSync(
      path.join(fixture.root, '.playconfig.json'),
      JSON.stringify({
        schema_version: 1,
        game_id: FIRST,
        name: 'codex-test-model',
        place: null,
      }),
      'utf8'
    );
    const message = failure((await use(fixture)).result);
    expect(message).toBe(
      'run `just join` first — this workspace is ' +
        `pre-configured for ${FIRST}, and joining binds the ` +
        'seat this command reports'
    );
    expect(message).toContain('`just join`');
    expect(message).toContain(FIRST);
    expect(message).not.toContain('--game_id');
    expect(message).not.toContain('multiple private sessions');
  });

  test('a bound workspace reports the seat, never a token', async () => {
    const fixture = bench();
    const seat = fixture.seat(FIRST, 'codex-first-model');
    ok((await use(fixture, seat)).result);
    const report = await use(fixture);
    ok(report.result);
    expect(report.out).toContain(`playing ${FIRST} | seat `);
    expect(report.out).toContain(workspaceRelative(fixture.workspace, seat));
    expect(report.out).toContain(
      'commands need no --session; `just use GAME_ID` rebinds this workspace'
    );
    expect(report.out).not.toContain('agent-v2-secret');
  });

  test('--json reports the binding as a payload with no rebind', async () => {
    const fixture = bench();
    const seat = fixture.seat(FIRST, 'codex-first-model');
    ok((await use(fixture, seat)).result);
    const report = await use(fixture, '', { json: true });
    ok(report.result);
    const payload = JSON.parse(report.out) as JsonObject;
    expect(payload['game_id']).toBe(FIRST);
    expect(payload['session_file']).toBe(seat);
    expect(payload['rebound_from']).toBeNull();
    expect(String(payload['bound_at'])).toMatch(/^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$/);
    expect(report.out).not.toContain('agent-v2-secret');
  });
});

describe('`use TARGET`', () => {
  test('it binds by exact path, then by game ID, saying what it left each time', async () => {
    const fixture = bench();
    const first = fixture.seat(FIRST, 'codex-first-model');
    fixture.seat(SECOND, 'codex-second-model');

    const initial = await use(fixture, SECOND);
    ok(initial.result);
    expect(initial.out).toBe(
      `this workspace is now playing ${SECOND} — commands need no --session`
    );

    const byPath = await use(fixture, workspaceRelative(fixture.workspace, first));
    ok(byPath.result);
    expect(byPath.out).toContain(
      `this workspace is now playing ${FIRST}, rebound from ${SECOND}`
    );
    expect(boundGame(fixture)).toBe(FIRST);

    const byGame = await use(fixture, SECOND);
    ok(byGame.result);
    expect(byGame.out).toContain(
      `this workspace is now playing ${SECOND}, rebound from ${FIRST}`
    );
    expect(boundGame(fixture)).toBe(SECOND);
  });

  test('rebinding to another seat in the same game says exactly that', async () => {
    const fixture = bench();
    const first = fixture.seat(FIRST, 'codex-first-model');
    const sibling = fixture.seat(FIRST, 'codex-sibling-model');
    ok((await use(fixture, workspaceRelative(fixture.workspace, first))).result);
    const rebound = await use(fixture, workspaceRelative(fixture.workspace, sibling));
    ok(rebound.result);
    expect(rebound.out).toBe(
      `this workspace is now playing ${FIRST}, rebound to another seat in ` +
        'the same game — commands need no --session'
    );
  });

  test('re-binding the seat already bound reports no rebind at all', async () => {
    const fixture = bench();
    const seat = fixture.seat(FIRST, 'codex-first-model');
    ok((await use(fixture, seat)).result);
    const again = await use(fixture, seat);
    ok(again.result);
    expect(again.out).toBe(
      `this workspace is now playing ${FIRST} — commands need no --session`
    );
  });

  test('an ambiguous game leaves the previous binding untouched', async () => {
    const fixture = bench();
    fixture.seat(FIRST, 'codex-first-model');
    fixture.seat(FIRST, 'codex-sibling-model');
    fixture.seat(SECOND, 'codex-second-model');
    ok((await use(fixture, SECOND)).result);
    expect(failure((await use(fixture, FIRST)).result)).toContain(
      'name the one you are playing'
    );
    expect(boundGame(fixture)).toBe(SECOND);
  });

  test('a file that is not a session this workspace joined is refused by name', async () => {
    const fixture = bench();
    const stray = path.join(fixture.workspace.stateRoot, FIRST, 'not-a-session.json');
    Effect.runSync(fixture.files.writeJson(stray, { hello: 'world' }));
    const target = workspaceRelative(fixture.workspace, stray);
    expect(failure((await use(fixture, target)).result)).toBe(
      `${target} is not a session this workspace joined. Bind a seat ` +
        'by its game with `just use GAME_ID`.'
    );
  });

  test('a session without a bearer token is refused too', async () => {
    const fixture = bench();
    const seat = fixture.seat(FIRST, 'codex-first-model', { agent_token: null });
    expect(failure((await use(fixture, seat)).result)).toContain(
      'is not a session this workspace joined'
    );
  });

  test('--json reports the game it rebound from', async () => {
    const fixture = bench();
    fixture.seat(FIRST, 'codex-first-model');
    const second = fixture.seat(SECOND, 'codex-second-model');
    ok((await use(fixture, FIRST)).result);
    const rebound = await use(fixture, second, { json: true });
    ok(rebound.result);
    const payload = JSON.parse(rebound.out) as JsonObject;
    expect(payload['game_id']).toBe(SECOND);
    expect(payload['session_file']).toBe(second);
    expect(payload['rebound_from']).toBe(FIRST);
  });

  test('binding also repoints the current-session pointer', async () => {
    const fixture = bench();
    const seat = fixture.seat(FIRST, 'codex-first-model');
    ok((await use(fixture, seat)).result);
    expect(
      fs.readFileSync(path.join(fixture.workspace.stateRoot, 'current'), 'utf8')
    ).toBe(`${path.join(FIRST, 'codex-first-model.json')}\n`);
    expect(
      Effect.runSync(Effect.either(fixture.store.sessionPath('')))
    ).toEqual(Either.right(seat));
  });

  test('a whitespace-only target is bare `use`, not a path', async () => {
    expect(failure((await use(bench(), '   ')).result)).toContain('not bound to a seat');
  });
});

describe('the CLI surface', () => {
  const cli = async (
    fixture: Bench,
    argv: ReadonlyArray<string>
  ): Promise<{ readonly failure: string | null; readonly out: string }> => {
    const root = Command.make('play', {}, () => Effect.void).pipe(
      Command.withSubcommands([useCommand])
    );
    const lines: string[] = [];
    const originalLog = console.log;
    console.log = (...parts: ReadonlyArray<unknown>) => lines.push(parts.join(' '));
    try {
      const outcome = await Effect.runPromise(
        Command.run(root, { name: 'play', version: '0.1.0' })(['bun', 'play', ...argv]).pipe(
          Effect.provide(Layer.merge(fixture.layer, BunContext.layer)),
          Effect.either
        )
      );
      return {
        failure:
          outcome._tag === 'Left'
            ? ((outcome.left as { readonly message?: string }).message ?? String(outcome.left))
            : null,
        out: lines.join('\n'),
      };
    } finally {
      console.log = originalLog;
    }
  };

  test('the positional target is optional — bare `play use` reports the seat', async () => {
    const fixture = bench();
    const seat = fixture.seat(FIRST, 'codex-first-model');
    expect((await cli(fixture, ['use', seat])).failure).toBeNull();
    const report = await cli(fixture, ['use']);
    expect(report.failure).toBeNull();
    expect(report.out).toContain(`playing ${FIRST} | seat`);
  });

  test('`play use GAME_ID --json` takes the flag after the positional', async () => {
    const fixture = bench();
    fixture.seat(FIRST, 'codex-first-model');
    const run = await cli(fixture, ['use', FIRST, '--json']);
    expect(run.failure).toBeNull();
    expect((JSON.parse(run.out) as JsonObject)['game_id']).toBe(FIRST);
  });
});

describe('PLAY_JSON', () => {
  test('it turns the report into a payload without a flag', async () => {
    const fixture = bench();
    const seat = fixture.seat(FIRST, 'codex-first-model');
    ok((await use(fixture, seat)).result);
    const report = await use(fixture, '', { env: { PLAY_JSON: 'yes' } });
    ok(report.result);
    expect((JSON.parse(report.out) as JsonObject)['game_id']).toBe(FIRST);
  });
});
