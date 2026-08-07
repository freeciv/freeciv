/**
 * Sessions, the seat binding and the `.v2-state` cache.
 *
 * "One workspace plays one seat" is the whole design, so the resolution order
 * (explicit → `PLAY_SESSION` → binding → sole session → pointer) and the refusal
 * to guess between two unbound seats are the tests that matter.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either, Option } from 'effect';
import {
  controllerName,
  emptyV2ClientState,
  gameId,
  sessionKey,
  sessionStoreFor,
  type SessionStoreApi,
} from 'src/services/session-store';
import { FIXTURE_GAME_ID, scratchWorkspace, sessionFile, type Scratch } from 'test/_fixtures';

const scratches: Scratch[] = [];

afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

/**
 * The core placeholder for the U03 seam.  When U03 lands its real validators
 * these tests should be re-pointed at that layer, and the alias-table proofs
 * belong there rather than here.
 */
const schema = {
  empty: emptyV2ClientState,
  validate: () => Effect.void,
  cursorExpired: (expiresAt: string | null): boolean =>
    expiresAt === null ? false : Date.parse(expiresAt) <= Date.now(),
};

interface Fixture {
  readonly scratch: Scratch;
  readonly store: SessionStoreApi;
  readonly write: (relative: string, value: unknown) => string;
}

const fresh = (environment: Record<string, string | undefined> = {}): Fixture => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const store = sessionStoreFor(scratch.workspace, scratch.files, schema, environment);
  return {
    scratch,
    store,
    write: (relative, value) => {
      const target = path.join(scratch.workspace.stateRoot, relative);
      Effect.runSync(scratch.files.writeJson(target, value));
      return target;
    },
  };
};

const run = <A, Err>(effect: Effect.Effect<A, Err>): Either.Either<A, Err> =>
  Effect.runSync(Effect.either(effect));

const message = <A>(either: Either.Either<A, { readonly message: string }>): string => {
  expect(Either.isLeft(either)).toBe(true);
  return Either.isLeft(either) ? either.left.message : '';
};

const right = <A, Err>(either: Either.Either<A, Err>): A => {
  expect(Either.isRight(either)).toBe(true);
  if (Either.isLeft(either)) throw new Error('expected success');
  return either.right;
};

describe('name validation', () => {
  test('a game ID must carry the assigned shape', () => {
    expect(right(run(gameId(FIXTURE_GAME_ID)))).toBe(FIXTURE_GAME_ID);
    expect(message(run(gameId('game_short')))).toBe('a valid assigned game ID is required');
  });

  test('a controller must be a truthful, non-generic harness-model label', () => {
    expect(right(run(controllerName('codex-gpt-5.6-sol')))).toBe('codex-gpt-5.6-sol');
    for (const bad of ['agent', 'harness-model', 'nodash', '-leading', 'trailing-']) {
      expect(message(run(controllerName(bad)))).toContain('truthful non-generic');
    }
  });

  test('the session key is a stable slug plus a digest of the exact label', () => {
    const key = sessionKey('codex-gpt-5.6-sol');
    expect(key).toMatch(/^codex-gpt-5-6-sol-[0-9a-f]{12}$/);
    expect(sessionKey('codex-gpt-5.6-sol')).toBe(key);
    expect(sessionKey('codex-gpt-5.6-SOL')).not.toBe(key);
  });
});

describe('session resolution', () => {
  test('a sole private session needs no --session', () => {
    const fixture = fresh();
    const target = fixture.write(`${FIXTURE_GAME_ID}/seat.json`, sessionFile());
    expect(right(run(fixture.store.sessionPath('')))).toBe(target);
  });

  test('two unbound sessions are refused rather than guessed between', () => {
    const fixture = fresh();
    fixture.write(`${FIXTURE_GAME_ID}/one.json`, sessionFile());
    fixture.write(`${FIXTURE_GAME_ID}/two.json`, sessionFile());
    expect(message(run(fixture.store.sessionPath('')))).toContain(
      'multiple private sessions exist'
    );
  });

  test('a bound seat wins over the count', () => {
    const fixture = fresh();
    fixture.write(`${FIXTURE_GAME_ID}/one.json`, sessionFile());
    const two = fixture.write(`${FIXTURE_GAME_ID}/two.json`, sessionFile());
    Effect.runSync(fixture.store.bindWorkspaceSeat(two, FIXTURE_GAME_ID));
    expect(right(run(fixture.store.sessionPath('')))).toBe(two);
  });

  test('a binding whose seat file is gone is stale, not authoritative', () => {
    const fixture = fresh();
    const one = fixture.write(`${FIXTURE_GAME_ID}/one.json`, sessionFile());
    const two = fixture.write(`${FIXTURE_GAME_ID}/two.json`, sessionFile());
    Effect.runSync(fixture.store.bindWorkspaceSeat(two, FIXTURE_GAME_ID));
    fs.unlinkSync(two);
    expect(right(run(fixture.store.sessionPath('')))).toBe(one);
  });

  test('PLAY_SESSION is honoured when no --session is given', () => {
    // A relative PLAY_SESSION is workspace-relative, not state-relative — the
    // Python joins it onto `ROOT`, so it has to name `.sessions/` itself.
    const fixture = fresh({ PLAY_SESSION: `.sessions/${FIXTURE_GAME_ID}/one.json` });
    const one = fixture.write(`${FIXTURE_GAME_ID}/one.json`, sessionFile());
    fixture.write(`${FIXTURE_GAME_ID}/two.json`, sessionFile());
    expect(right(run(fixture.store.sessionPath('')))).toBe(one);
  });

  test('a PLAY_SESSION outside PLAY_STATE_DIR is refused, not followed', () => {
    const fixture = fresh({ PLAY_SESSION: `${FIXTURE_GAME_ID}/one.json` });
    fixture.write(`${FIXTURE_GAME_ID}/one.json`, sessionFile());
    expect(message(run(fixture.store.sessionPath('')))).toBe(
      'private state files must stay inside PLAY_STATE_DIR'
    );
  });

  test('with nothing at all, the remedy names `just join`', () => {
    const fixture = fresh();
    expect(message(run(fixture.store.sessionPath('')))).toBe(
      'no current session; run `just join --game_id ... --name ...` first'
    );
  });

  test('a pre-configured workspace gets the argument-free remedy instead', () => {
    const fixture = fresh();
    fs.writeFileSync(
      path.join(fixture.scratch.workspace.root, '.playconfig.json'),
      JSON.stringify({ game_id: FIXTURE_GAME_ID })
    );
    expect(message(run(fixture.store.sessionPath('')))).toBe(
      'run `just join` first — this workspace is pre-configured for ' +
        `${FIXTURE_GAME_ID}, and every other command needs the seat it creates`
    );
  });
});

describe('seat binding', () => {
  test('binding writes a pointer, never a credential', () => {
    const fixture = fresh();
    const target = fixture.write(`${FIXTURE_GAME_ID}/seat.json`, sessionFile());
    Effect.runSync(fixture.store.bindWorkspaceSeat(target, FIXTURE_GAME_ID));
    const raw = fs.readFileSync(fixture.store.seatBindingPath, 'utf8');
    expect(raw).not.toContain('secret-token');
    expect(JSON.parse(raw)).toMatchObject({
      schema_version: 1,
      game_id: FIXTURE_GAME_ID,
      session: path.join(FIXTURE_GAME_ID, 'seat.json'),
    });
  });

  test('re-binding the same seat reports no replacement', () => {
    const fixture = fresh();
    const target = fixture.write(`${FIXTURE_GAME_ID}/seat.json`, sessionFile());
    Effect.runSync(fixture.store.bindWorkspaceSeat(target, FIXTURE_GAME_ID));
    const replaced = Effect.runSync(fixture.store.bindWorkspaceSeat(target, FIXTURE_GAME_ID));
    expect(Option.isNone(replaced)).toBe(true);
  });

  test('re-binding a different seat reports the one it replaced', () => {
    const fixture = fresh();
    const one = fixture.write(`${FIXTURE_GAME_ID}/one.json`, sessionFile());
    const two = fixture.write(`${FIXTURE_GAME_ID}/two.json`, sessionFile());
    Effect.runSync(fixture.store.bindWorkspaceSeat(one, FIXTURE_GAME_ID));
    const replaced = Effect.runSync(fixture.store.bindWorkspaceSeat(two, FIXTURE_GAME_ID));
    expect(Option.isSome(replaced)).toBe(true);
    if (Option.isSome(replaced)) {
      expect(replaced.value.relative).toBe(path.join(FIXTURE_GAME_ID, 'one.json'));
    }
  });

  test('an unreadable binding names the repair, not the parser', () => {
    const fixture = fresh();
    Effect.runSync(
      fixture.scratch.files.writeJson(fixture.store.seatBindingPath, { game_id: 'nope' })
    );
    expect(message(run(fixture.store.readSeatBinding()))).toContain('just use GAME_ID');
  });
});

describe('v2 sessions', () => {
  test('a strategic-v1 session is refused by a v2 command', () => {
    const fixture = fresh();
    fixture.write(
      `${FIXTURE_GAME_ID}/seat.json`,
      sessionFile({ control_protocol: 'strategic-v1' })
    );
    expect(message(run(fixture.store.resolveV2('')))).toBe('this command is full-control-v2 only');
  });

  test('a v2 session normalizes its own service URL on every load', () => {
    const fixture = fresh();
    fixture.write(
      `${FIXTURE_GAME_ID}/seat.json`,
      sessionFile({ service_url: 'HTTP://127.0.0.1:8765/' })
    );
    const loaded = right(run(fixture.store.resolveV2('')));
    expect(loaded.session.serviceUrl).toBe('http://127.0.0.1:8765');
  });

  test('a session smuggling credentials into the URL is refused', () => {
    const fixture = fresh();
    fixture.write(
      `${FIXTURE_GAME_ID}/seat.json`,
      sessionFile({ service_url: 'http://user:pass@127.0.0.1:8765' })
    );
    expect(message(run(fixture.store.resolveV2('')))).toContain('without credentials');
  });

  test('a missing controller label is an incomplete v2 session', () => {
    const fixture = fresh();
    fixture.write(`${FIXTURE_GAME_ID}/seat.json`, sessionFile({ controller_label: '' }));
    expect(message(run(fixture.store.resolveV2('')))).toBe(
      'the private full-control-v2 session is incomplete'
    );
  });
});

describe('.v2-state', () => {
  test('a missing cache reads as the empty schema-5 shape', async () => {
    const fixture = fresh();
    const target = fixture.write(`${FIXTURE_GAME_ID}/seat.json`, sessionFile());
    const loaded = right(run(fixture.store.resolveV2('')));
    const state = await Effect.runPromise(fixture.store.readState(target, loaded.session));
    expect(state.schema_version).toBe(5);
    expect(state.drained_actors).toEqual([]);
    expect(state.action_aliases).toEqual({ state_revision: null, by_alias: {} });
  });

  test('a written cache round-trips', async () => {
    const fixture = fresh();
    const target = fixture.write(`${FIXTURE_GAME_ID}/seat.json`, sessionFile());
    const loaded = right(run(fixture.store.resolveV2('')));
    const next = {
      ...emptyV2ClientState(loaded.session),
      batches: { batch_one: '{"a":1}' },
    };
    await Effect.runPromise(fixture.store.writeState(target, next));
    const read = await Effect.runPromise(fixture.store.readState(target, loaded.session));
    expect(read.batches).toEqual({ batch_one: '{"a":1}' });
  });

  test('a schema-1 cache is migrated and every executable action is dropped', async () => {
    const fixture = fresh();
    const target = fixture.write(`${FIXTURE_GAME_ID}/seat.json`, sessionFile());
    const loaded = right(run(fixture.store.resolveV2('')));
    Effect.runSync(
      fixture.scratch.files.writeJson(fixture.store.statePath(target), {
        schema_version: 1,
        game_id: FIXTURE_GAME_ID,
        agent_id: loaded.session.agentId,
        last_revision: null,
        actions: { action_old: { anything: true } },
        batches: { batch_one: '{"a":1}' },
        receipts: { batch_one: { kept: true } },
      })
    );
    const state = await Effect.runPromise(fixture.store.readState(target, loaded.session));
    expect(state.schema_version).toBe(5);
    expect(state.actions).toEqual({});
    expect(state.batches).toEqual({ batch_one: '{"a":1}' });
    expect(state.receipts).toEqual({ batch_one: { kept: true } });
  });

  test('a persisted batch body must be the exact bytes, not a re-parsed object', async () => {
    const fixture = fresh();
    const target = fixture.write(`${FIXTURE_GAME_ID}/seat.json`, sessionFile());
    const loaded = right(run(fixture.store.resolveV2('')));
    Effect.runSync(
      fixture.scratch.files.writeJson(fixture.store.statePath(target), {
        ...emptyV2ClientState(loaded.session),
        batches: { batch_one: { a: 1 } },
      })
    );
    const either = await Effect.runPromise(
      Effect.either(fixture.store.readState(target, loaded.session))
    );
    expect(message(either)).toContain('is invalid');
  });

  test('a cache belonging to another agent is refused', async () => {
    const fixture = fresh();
    const target = fixture.write(`${FIXTURE_GAME_ID}/seat.json`, sessionFile());
    const loaded = right(run(fixture.store.resolveV2('')));
    Effect.runSync(
      fixture.scratch.files.writeJson(fixture.store.statePath(target), {
        ...emptyV2ClientState(loaded.session),
        agent_id: 'agent_someoneelse',
      })
    );
    const either = await Effect.runPromise(
      Effect.either(fixture.store.readState(target, loaded.session))
    );
    expect(message(either)).toContain('is invalid');
  });
});
