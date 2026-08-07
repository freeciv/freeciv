/**
 * The wait engine.
 *
 * Ports the `test_v2_wait_*` cases from `play/tests/test_client.py` — the
 * request shape (`/me/wait?…`, never `/state?…`), the three refusals, the
 * legacy fallback for supervisors predating the private route, and the
 * deadline-bounded `--for-turn` loop with its per-tick marker write and
 * transcript line.
 *
 * Wall-clock time is the thing under test in the `--for-turn` cases — the whole
 * point of a deadline-bounded wait is how long it blocks for — so the tests own
 * the clock rather than sleeping through it, exactly as the Python's `clocked`
 * context manager does.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either, Layer } from 'effect';
import { decodeHealth, type HealthEnvelope } from 'src/schema/health';
import type { JsonObject } from 'src/schema/primitives';
import { decodeWait, type WaitEnvelope } from 'src/schema/wait';
import { renderHealth } from 'src/render/health';
import { awaitLine, renderWait, waitingTickLine } from 'src/render/wait';
import { httpFor } from 'src/services/http';
import {
  SessionStore,
  emptyV2ClientState,
  sessionStoreFor,
  type Session,
  type SessionStoreApi,
} from 'src/services/session-store';
import { V2Client, v2ClientFor } from 'src/services/v2-client';
import {
  V2_FOR_TURN_GRACE_S,
  V2_WAIT_S_MAX,
  V2_WAIT_TICK_S,
  holderRemainingS,
  legacyWaitValue,
  localWaitResponse,
  seatRebound,
  waitArgs,
  waitCommandValue,
  waitCtx,
  waitUntilTurn,
  waitValue,
  type HolderSeatFn,
  type WaitClock,
  type WaitCtx,
  type WaitHooks,
} from 'src/services/wait';
import {
  FIXTURE_AGENT_ID,
  FIXTURE_GAME_ID,
  healthPayload,
  jsonResponse,
  pagePayload,
  scratchWorkspace,
  sessionFile,
  waitPayload,
  type Scratch,
} from 'test/_fixtures';

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------

const scratches: Scratch[] = [];

afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

/** The core placeholder for the U03 seam; alias behaviour is U03's to prove. */
const stateSchema = {
  empty: emptyV2ClientState,
  validate: () => Effect.void,
  cursorExpired: (): boolean => false,
};

interface Bench {
  readonly scratch: Scratch;
  readonly sessionPath: string;
  readonly session: Session;
  readonly store: SessionStoreApi;
}

const bench = (): Bench => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const sessionPath = path.join(scratch.workspace.stateRoot, 'session-codex-gpt-5.6-sol.json');
  Effect.runSync(scratch.files.writeJson(sessionPath, sessionFile()));
  const store = sessionStoreFor(scratch.workspace, scratch.files, stateSchema, {});
  const loaded = Effect.runSync(store.resolveV2(sessionPath));
  return { scratch, sessionPath, session: loaded.session, store };
};

/**
 * `_holder_seat` (client.py:5350-5367).
 *
 * U06 owns the real one; the engine takes it as a hook so wave 1 can be tested
 * without it, and this restatement is the hook these tests inject.
 */
const holderSeat: HolderSeatFn = (phase) => {
  if (phase === null || phase.active === true) return null;
  const waitingOn = phase.waiting_on;
  if (waitingOn === undefined || waitingOn === null) return null;
  const others = waitingOn.seats.filter((row) => row.is_self === false);
  return others.length === 1 ? (others[0] ?? null) : null;
};

interface Recorded {
  readonly pages: unknown[];
  readonly mirroredPages: unknown[];
  readonly mirroredHealth: HealthEnvelope[];
  readonly echoed: HealthEnvelope[];
  readonly order: string[];
}

interface Kit {
  readonly hooks: WaitHooks;
  readonly log: Recorded;
  readonly echo: (health: HealthEnvelope) => Effect.Effect<void>;
}

const recorder = (): Kit => {
  const log: Recorded = {
    pages: [],
    mirroredPages: [],
    mirroredHealth: [],
    echoed: [],
    order: [],
  };
  return {
    log,
    echo: (health) =>
      Effect.sync(() => {
        log.echoed.push(health);
        log.order.push('echo');
      }),
    hooks: {
      rememberPage: (page) =>
        Effect.sync(() => {
          log.pages.push(page);
          log.order.push('remember');
        }),
      mirrorPage: (page) =>
        Effect.sync(() => {
          log.mirroredPages.push(page);
          log.order.push('mirror-page');
        }),
      mirrorHealth: (health) =>
        Effect.sync(() => {
          log.mirroredHealth.push(health);
          log.order.push('mirror-health');
        }),
      holderSeat,
    },
  };
};

const layers = (
  fetchImpl: typeof fetch,
  store: SessionStoreApi
): Layer.Layer<V2Client | SessionStore> =>
  Layer.mergeAll(
    Layer.succeed(V2Client, v2ClientFor(httpFor(fetchImpl), () => Effect.void)),
    Layer.succeed(SessionStore, store)
  );

const run = <A, E>(
  effect: Effect.Effect<A, E, V2Client | SessionStore>,
  fetchImpl: typeof fetch,
  store: SessionStoreApi
): Promise<Either.Either<A, E>> =>
  Effect.runPromise(Effect.either(Effect.provide(effect, layers(fetchImpl, store))));

const right = <A, E>(either: Either.Either<A, E>): A => {
  expect(Either.isRight(either)).toBe(true);
  if (Either.isLeft(either)) throw new Error(`expected success, got ${JSON.stringify(either.left)}`);
  return either.right;
};

const message = <A>(either: Either.Either<A, { readonly message: string }>): string => {
  expect(Either.isLeft(either)).toBe(true);
  return Either.isLeft(either) ? either.left.message : '';
};

const urlOf = (input: Parameters<typeof fetch>[0]): string =>
  typeof input === 'string' ? input : input instanceof URL ? input.href : input.url;

interface Reply {
  readonly status?: number;
  readonly body: unknown;
}

/** A fetch that records every URL and answers from a per-URL script. */
const scripted = (
  answer: (url: string) => Reply
): { readonly fetch: typeof fetch; readonly urls: string[] } => {
  const urls: string[] = [];
  const impl = (async (input: Parameters<typeof fetch>[0]): Promise<Response> => {
    const url = urlOf(input);
    const reply = answer(url);
    urls.push(url);
    return jsonResponse(reply.body, reply.status ?? 200);
  }) as typeof fetch;
  return { fetch: impl, urls };
};

// ---------------------------------------------------------------------------
// The PvP payloads: one seat holding the phase, one waiting on it
// ---------------------------------------------------------------------------

const OPPONENT: JsonObject = {
  place: 2,
  seat_id: 'place-2',
  player_name: 'AgentPlace2',
  controller_label: 'pi-gpt-5.6-sol',
  standing: 'active',
  is_self: false,
};

interface PvpShape {
  readonly mine: boolean;
  readonly remainingS?: number | null;
  readonly elapsedS?: number | null;
  readonly gameState?: string;
  readonly selfHeld?: boolean;
}

const pvpHealth = (shape: PvpShape): JsonObject => {
  const elapsedS = shape.elapsedS === undefined ? 13 : shape.elapsedS;
  const remainingS = shape.remainingS === undefined ? 587 : shape.remainingS;
  const waitingOn: JsonObject = {
    kind: 'other_seat',
    summary: 'Seat 2 AgentPlace2 (pi-gpt-5.6-sol) holds turn 3 phase 1 and has not ended it.',
    waiting_s: elapsedS,
    seats: shape.selfHeld === true ? [{ ...OPPONENT, is_self: true }] : [OPPONENT],
  };
  return healthPayload({
    game_state: shape.gameState ?? 'running',
    phase: {
      state: 'awaiting_agent',
      turn: 3,
      phase: 1,
      active: shape.mine,
      timing: {
        mode: 'default',
        timeout_s: 600,
        deadline_started_at: 1000,
        deadline_at: 1600,
        elapsed_s: elapsedS,
        remaining_s: remainingS,
      },
      ...(shape.mine ? {} : { waiting_on: waitingOn }),
    },
  });
};

const pvpWake = (reason: string, shape: PvpShape): JsonObject =>
  waitPayload({ wake_reason: reason, health: pvpHealth(shape) });

// ---------------------------------------------------------------------------
// _wait_args
// ---------------------------------------------------------------------------

describe('waitArgs', () => {
  test('a missing knob takes the default, a given one is untouched', () => {
    expect(waitArgs({})).toEqual({ waitS: 120, pollS: 1, until: 'phase' });
    expect(waitArgs({ waitS: 30, pollS: 0.5, until: 'revision' })).toEqual({
      waitS: 30,
      pollS: 0.5,
      until: 'revision',
    });
  });

  test('--wait-s 0 is a legal non-blocking poll, not a missing value', () => {
    expect(waitArgs({ waitS: 0 }).waitS).toBe(0);
    expect(waitArgs({ pollS: 0 }).pollS).toBe(0);
  });

  test('the override replaces wait-s for one internal poll and nothing else', () => {
    expect(waitArgs({ waitS: 120, pollS: 2, until: 'revision' }, 15)).toEqual({
      waitS: 15,
      pollS: 2,
      until: 'revision',
    });
  });
});

// ---------------------------------------------------------------------------
// _wait_value
// ---------------------------------------------------------------------------

describe('waitValue', () => {
  test('uses the server actionable phase without ever fetching state', async () => {
    const seat = bench();
    const server = scripted(() => ({ body: waitPayload() }));
    const ctx = waitCtx({ sessionPath: seat.sessionPath, session: seat.session, hooks: recorder().hooks });
    const wake = right(await run(waitValue(ctx, waitArgs({})), server.fetch, seat.store));

    expect(wake.wake_reason).toBe('phase_active');
    expect(server.urls).toHaveLength(1);
    expect(server.urls[0]).toContain('/me/wait?');
    expect(server.urls[0]).not.toContain('/state?');
    expect(server.urls[0]).toContain('wait_s=120');
    expect(server.urls[0]).toContain('until=phase');
    expect(server.urls[0]).not.toContain('after_state_token');
  });

  test('wait_s reaches the wire in CPython %g', async () => {
    const seat = bench();
    const ctx = waitCtx({ sessionPath: seat.sessionPath, session: seat.session, hooks: recorder().hooks });
    for (const [given, encoded] of [
      [615, 'wait_s=615'],
      [0, 'wait_s=0'],
      [0.5, 'wait_s=0.5'],
      [1.5, 'wait_s=1.5'],
    ] as const) {
      const server = scripted(() => ({ body: waitPayload() }));
      right(await run(waitValue(ctx, waitArgs({ waitS: given })), server.fetch, seat.store));
      expect(server.urls[0]).toContain(encoded);
    }
  });

  test('the ceiling covers a whole opponent phase and refuses one second more', async () => {
    expect(V2_WAIT_S_MAX).toBe(615);
    const seat = bench();
    const server = scripted(() => ({ body: waitPayload() }));
    const ctx = waitCtx({ sessionPath: seat.sessionPath, session: seat.session, hooks: recorder().hooks });
    expect(message(await run(waitValue(ctx, waitArgs({ waitS: 616 })), server.fetch, seat.store))).toBe(
      'wait-s must be in [0, 615]'
    );
    expect(message(await run(waitValue(ctx, waitArgs({ waitS: -1 })), server.fetch, seat.store))).toBe(
      'wait-s must be in [0, 615]'
    );
    expect(server.urls).toHaveLength(0);
  });

  test('poll-s is bounded too', async () => {
    const seat = bench();
    const server = scripted(() => ({ body: waitPayload() }));
    const ctx = waitCtx({ sessionPath: seat.sessionPath, session: seat.session, hooks: recorder().hooks });
    expect(message(await run(waitValue(ctx, waitArgs({ pollS: 0.04 })), server.fetch, seat.store))).toBe(
      'poll-s must be in [0.05, 30]'
    );
    expect(message(await run(waitValue(ctx, waitArgs({ pollS: 31 })), server.fetch, seat.store))).toBe(
      'poll-s must be in [0.05, 30]'
    );
  });

  test('--until takes exactly two values', async () => {
    const seat = bench();
    const server = scripted(() => ({ body: waitPayload() }));
    const ctx = waitCtx({ sessionPath: seat.sessionPath, session: seat.session, hooks: recorder().hooks });
    expect(
      message(await run(waitValue(ctx, waitArgs({ until: 'forever' })), server.fetch, seat.store))
    ).toBe('wait --until must be phase or revision');
  });

  test('--until revision without a validated state page is refused', async () => {
    const seat = bench();
    const server = scripted(() => ({ body: waitPayload() }));
    const ctx = waitCtx({ sessionPath: seat.sessionPath, session: seat.session, hooks: recorder().hooks });
    expect(
      message(await run(waitValue(ctx, waitArgs({ until: 'revision' })), server.fetch, seat.store))
    ).toBe('wait --until revision requires a previously validated state page');
  });

  test('a stateless wait is refused outside phase mode', async () => {
    const seat = bench();
    const server = scripted(() => ({ body: waitPayload() }));
    const ctx = waitCtx({ sessionPath: seat.sessionPath, session: seat.session, hooks: recorder().hooks });
    expect(
      message(
        await run(
          waitValue(ctx, waitArgs({ until: 'revision' }), { stateless: true }),
          server.fetch,
          seat.store
        )
      )
    ).toBe('a stateless wait is phase-mode only');
  });

  test('a stateless wait never reads .v2-state', async () => {
    const seat = bench();
    const server = scripted(() => ({ body: waitPayload() }));
    const ctx = waitCtx({ sessionPath: seat.sessionPath, session: seat.session, hooks: recorder().hooks });
    right(await run(waitValue(ctx, waitArgs({}), { stateless: true }), server.fetch, seat.store));
    expect(fs.existsSync(seat.store.statePath(seat.sessionPath))).toBe(false);
    expect(server.urls[0]).toContain('until=phase');
    expect(server.urls[0]).not.toContain('after_state_token');
  });

  test('--until revision sends the cached baseline as after_state_token', async () => {
    const seat = bench();
    Effect.runSync(
      seat.store.writeState(seat.sessionPath, {
        ...emptyV2ClientState(seat.session),
        last_revision: { turn: 5, revision: 12, state_token: 'token_5_12' },
      })
    );
    const server = scripted(() => ({
      body: waitPayload({
        wake_reason: 'revision_changed',
        state_revision: { turn: 5, revision: 13, state_token: 'token_5_13' },
      }),
    }));
    const ctx = waitCtx({ sessionPath: seat.sessionPath, session: seat.session, hooks: recorder().hooks });
    const wake = right(
      await run(waitValue(ctx, waitArgs({ until: 'revision' })), server.fetch, seat.store)
    );
    expect(wake.wake_reason).toBe('revision_changed');
    expect(server.urls[0]).toContain('until=revision');
    expect(server.urls[0]).toContain('after_state_token=token_5_12');
  });

  test('a structured non-2xx refusal is raised, never downgraded to polling', async () => {
    const seat = bench();
    const server = scripted(() => ({
      status: 404,
      body: {
        schema_version: 2,
        control_protocol: 'full-control-v2',
        error: { code: 'not_found', message: 'no such seat', retryable: false, details: {} },
        state_revision: null,
      },
    }));
    const ctx = waitCtx({ sessionPath: seat.sessionPath, session: seat.session, hooks: recorder().hooks });
    const either = await run(waitValue(ctx, waitArgs({})), server.fetch, seat.store);
    expect(Either.isLeft(either)).toBe(true);
    // One request: the legacy fallback must not have started polling /health.
    expect(server.urls).toHaveLength(1);
  });
});

// ---------------------------------------------------------------------------
// _legacy_wait_value
// ---------------------------------------------------------------------------

describe('the legacy fallback', () => {
  const missingRoute: Reply = { status: 404, body: { error: 'Not Found' } };

  test('a bare-string 404 downgrades to polling /health', async () => {
    const seat = bench();
    const server = scripted((url) =>
      url.includes('/me/wait?') ? missingRoute : { body: healthPayload() }
    );
    const ctx = waitCtx({ sessionPath: seat.sessionPath, session: seat.session, hooks: recorder().hooks });
    const wake = right(await run(waitValue(ctx, waitArgs({})), server.fetch, seat.store));
    expect(wake.wake_reason).toBe('phase_active');
    expect(server.urls[1]).toContain('/me/health');
    expect(server.urls.filter((url) => url.includes('/state?'))).toHaveLength(0);
  });

  test('a terminal game answers game_terminal without waiting out the clock', async () => {
    const seat = bench();
    const server = scripted((url) =>
      url.includes('/me/wait?')
        ? missingRoute
        : { body: healthPayload({ game_state: 'completed', phase: null }) }
    );
    const ctx = waitCtx({ sessionPath: seat.sessionPath, session: seat.session, hooks: recorder().hooks });
    const wake = right(await run(waitValue(ctx, waitArgs({})), server.fetch, seat.store));
    expect(wake.wake_reason).toBe('game_terminal');
    expect(wake.state_revision).toBeNull();
  });

  test('an inactive phase polls until the budget runs out, then times out', async () => {
    const seat = bench();
    const server = scripted((url) =>
      url.includes('/me/wait?') ? missingRoute : { body: pvpHealth({ mine: false }) }
    );
    const now = { seconds: 0 };
    const clock: WaitClock = {
      monotonic: () => Effect.sync(() => now.seconds),
      sleep: (seconds) =>
        Effect.sync(() => {
          now.seconds += seconds;
        }),
    };
    const ctx = waitCtx({
      sessionPath: seat.sessionPath,
      session: seat.session,
      hooks: recorder().hooks,
      clock,
    });
    const wake = right(
      await run(waitValue(ctx, waitArgs({ waitS: 3, pollS: 1 })), server.fetch, seat.store)
    );
    expect(wake.wake_reason).toBe('timeout');
    // One `/wait` 404, then a `/health` poll at t=0, 1, 2 and 3.
    expect(server.urls.filter((url) => url.includes('/me/health'))).toHaveLength(4);
    expect(now.seconds).toBe(3);
  });

  test('revision mode fetches the overview page, remembers it and mirrors it', async () => {
    const seat = bench();
    const kit = recorder();
    const bumped = pagePayload([{ id: 'unit_0', name: 'Warriors' }], {
      state_revision: { turn: 5, revision: 13, state_token: 'token_5_13' },
    });
    const server = scripted((url) => {
      if (url.includes('/me/health')) return { body: pvpHealth({ mine: false }) };
      return { body: bumped };
    });
    const ctx = waitCtx({ sessionPath: seat.sessionPath, session: seat.session, hooks: kit.hooks });
    const wake = right(
      await run(
        legacyWaitValue(ctx, waitArgs({ waitS: 5 }), {
          until: 'revision',
          baseline: { turn: 5, revision: 12, state_token: 'token_5_12' },
        }),
        server.fetch,
        seat.store
      )
    );
    expect(wake.wake_reason).toBe('revision_changed');
    expect(wake.state_revision?.state_token).toBe('token_5_13');
    expect(kit.log.pages).toHaveLength(1);
    expect(kit.log.mirroredPages).toHaveLength(1);
    // Remembered before mirrored, exactly as the Python orders them.
    expect(kit.log.order).toEqual(['remember', 'mirror-page']);
    expect(server.urls.some((url) => url.includes('section=overview&limit=16'))).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// _local_wait_response
// ---------------------------------------------------------------------------

describe('localWaitResponse', () => {
  test('carries this seat identity and nothing the server did not say', async () => {
    const seat = bench();
    const health = await Effect.runPromise(decodeHealth(healthPayload(), seat.session));
    expect(localWaitResponse(seat.session, 'timeout', health, null)).toEqual({
      schema_version: 2,
      control_protocol: 'full-control-v2',
      game_id: FIXTURE_GAME_ID,
      agent_id: FIXTURE_AGENT_ID,
      wake_reason: 'timeout',
      health,
      state_revision: null,
    });
  });
});

// ---------------------------------------------------------------------------
// _holder_remaining_s
// ---------------------------------------------------------------------------

describe('holderRemainingS', () => {
  const decodedHealth = (shape: PvpShape): Promise<HealthEnvelope> =>
    Effect.runPromise(decodeHealth(pvpHealth(shape), bench().session));

  test('names the deadline when exactly one other seat holds the phase', async () => {
    expect(holderRemainingS(await decodedHealth({ mine: false, remainingS: 40 }), holderSeat)).toBe(40);
  });

  test('an active phase has no holder to name', async () => {
    expect(holderRemainingS(await decodedHealth({ mine: true }), holderSeat)).toBeNull();
  });

  test('a seat waiting on itself is not a holder', async () => {
    expect(
      holderRemainingS(await decodedHealth({ mine: false, selfHeld: true }), holderSeat)
    ).toBeNull();
  });

  test('a spent deadline is zero and an absent one is unknown', async () => {
    // A negative `remaining_s` never survives `_validate_health`, so the
    // `max(0, …)` floor is defensive; zero is the value a pinned holder sends.
    expect(holderRemainingS(await decodedHealth({ mine: false, remainingS: 0 }), holderSeat)).toBe(0);
    expect(
      holderRemainingS(await decodedHealth({ mine: false, remainingS: null }), holderSeat)
    ).toBeNull();
  });
});

// ---------------------------------------------------------------------------
// _wait_until_turn, on a clock the test owns
// ---------------------------------------------------------------------------

interface Clocked {
  readonly fetch: typeof fetch;
  readonly clock: WaitClock;
  readonly blocked: number[];
  readonly now: { seconds: number };
}

/**
 * A fake `/wait` on a clock that only advances by what it blocked for.
 *
 * The Python's `PvPWaitInteropTests.clocked`, restated: the responder reads
 * `wait_s` out of the query, advances the clock by exactly that, and answers
 * from the caller's script at the new time.
 */
const clocked = (script: (elapsed: number) => unknown): Clocked => {
  const now = { seconds: 0 };
  const blocked: number[] = [];
  const impl = (async (input: Parameters<typeof fetch>[0]): Promise<Response> => {
    const url = new URL(urlOf(input));
    const waited = Number(url.searchParams.get('wait_s') ?? '0');
    blocked.push(waited);
    now.seconds += waited;
    return jsonResponse(script(now.seconds));
  }) as typeof fetch;
  return {
    fetch: impl,
    blocked,
    now,
    clock: {
      monotonic: () => Effect.sync(() => now.seconds),
      sleep: (seconds) =>
        Effect.sync(() => {
          now.seconds += seconds;
        }),
    },
  };
};

const clockedCtx = (kit: Kit, fake: Clocked, seat: Bench): WaitCtx =>
  waitCtx({
    sessionPath: seat.sessionPath,
    session: seat.session,
    hooks: kit.hooks,
    clock: fake.clock,
  });

describe('waitUntilTurn', () => {
  test('is bounded by the holder deadline plus exactly one grace window', async () => {
    const seat = bench();
    const kit = recorder();
    const fake = clocked((elapsed) =>
      pvpWake('timeout', {
        mine: false,
        remainingS: Math.max(0, 40 - elapsed),
        elapsedS: Math.min(600, 560 + elapsed),
      })
    );
    const wake = right(
      await run(
        waitUntilTurn(clockedCtx(kit, fake, seat), waitArgs({}), {
          forTurn: true,
          echo: kit.echo,
        }),
        fake.fetch,
        seat.store
      )
    );

    expect(wake.wake_reason).toBe('timeout');
    // Short internal polls, never one 120 s block.
    expect(fake.blocked.every((item) => item <= V2_WAIT_TICK_S)).toBe(true);
    // It waited out the 40 s deadline plus one 15 s grace, and stopped rather
    // than rolling the grace forward against a holder pinned at zero.
    expect(fake.now.seconds).toBeGreaterThanOrEqual(40);
    expect(fake.now.seconds).toBeLessThanOrEqual(40 + V2_FOR_TURN_GRACE_S);
    expect(kit.log.echoed).toHaveLength(fake.blocked.length - 1);
  });

  test('the marker is refreshed on every tick, before the transcript line', async () => {
    const seat = bench();
    const kit = recorder();
    const fake = clocked((elapsed) =>
      pvpWake(elapsed >= 45 ? 'phase_active' : 'timeout', {
        mine: elapsed >= 45,
        remainingS: Math.max(0, 300 - elapsed),
        elapsedS: Math.min(600, 300 + elapsed),
      })
    );
    right(
      await run(
        waitUntilTurn(clockedCtx(kit, fake, seat), waitArgs({}), {
          forTurn: true,
          echo: kit.echo,
        }),
        fake.fetch,
        seat.store
      )
    );
    // One marker write per poll; one transcript line per poll but the last.
    expect(kit.log.mirroredHealth).toHaveLength(fake.blocked.length);
    expect(kit.log.echoed).toHaveLength(fake.blocked.length - 1);
    // Ordering: the watcher's file is fresh before the transcript says so.
    expect(kit.log.order).toEqual([
      'mirror-health',
      'echo',
      'mirror-health',
      'echo',
      'mirror-health',
    ]);
    // The marker sees a live deadline, not one frozen for the whole block.
    expect(kit.log.mirroredHealth.map((item) => item.phase?.timing.remaining_s)).toEqual([
      285, 270, 255,
    ]);
  });

  test('returns the moment the phase is genuinely ours', async () => {
    const seat = bench();
    const kit = recorder();
    const fake = clocked((elapsed) =>
      pvpWake(elapsed >= 30 ? 'phase_active' : 'timeout', {
        mine: elapsed >= 30,
        remainingS: Math.max(0, 300 - elapsed),
      })
    );
    const wake = right(
      await run(
        waitUntilTurn(clockedCtx(kit, fake, seat), waitArgs({}), { forTurn: true }),
        fake.fetch,
        seat.store
      )
    );
    expect(wake.wake_reason).toBe('phase_active');
    expect(fake.now.seconds).toBe(30);
  });

  test('a cap is a hard ceiling over the holder deadline', async () => {
    const seat = bench();
    const kit = recorder();
    const fake = clocked((elapsed) =>
      pvpWake('timeout', { mine: false, remainingS: Math.max(0, 600 - elapsed) })
    );
    const wake = right(
      await run(
        waitUntilTurn(clockedCtx(kit, fake, seat), waitArgs({}), { forTurn: true, capS: 45 }),
        fake.fetch,
        seat.store
      )
    );
    expect(wake.wake_reason).toBe('timeout');
    expect(fake.now.seconds).toBe(45);
  });

  test('without --for-turn the first poll is the caller own --wait-s', async () => {
    const seat = bench();
    const kit = recorder();
    // A holder whose deadline has already run out: the loop continues only
    // while the server names a seat that still has time left.
    const fake = clocked((elapsed) =>
      pvpWake('timeout', { mine: false, remainingS: Math.max(0, 20 - elapsed) })
    );
    right(
      await run(
        waitUntilTurn(clockedCtx(kit, fake, seat), waitArgs({ waitS: 30 }), { forTurn: false }),
        fake.fetch,
        seat.store
      )
    );
    expect(fake.blocked[0]).toBe(30);
    expect(fake.blocked).toHaveLength(1);
  });

  test('without --for-turn and no holder named, one poll and out', async () => {
    const seat = bench();
    const kit = recorder();
    // No `waiting_on`: the server named nobody, so a single-seat game behaves
    // exactly as it always did.
    const fake = clocked(() => waitPayload({ wake_reason: 'timeout', health: healthPayload() }));
    const wake = right(
      await run(
        waitUntilTurn(clockedCtx(kit, fake, seat), waitArgs({}), { forTurn: false }),
        fake.fetch,
        seat.store
      )
    );
    expect(wake.wake_reason).toBe('timeout');
    expect(fake.blocked).toHaveLength(1);
  });

  test('the mirror override replaces the default marker write', async () => {
    const seat = bench();
    const kit = recorder();
    const own: HealthEnvelope[] = [];
    const fake = clocked(() => pvpWake('phase_active', { mine: true }));
    right(
      await run(
        waitUntilTurn(clockedCtx(kit, fake, seat), waitArgs({}), {
          forTurn: true,
          mirror: (health) =>
            Effect.sync(() => {
              own.push(health);
            }),
        }),
        fake.fetch,
        seat.store
      )
    );
    expect(own).toHaveLength(1);
    expect(kit.log.mirroredHealth).toHaveLength(0);
  });
});

// ---------------------------------------------------------------------------
// _wait_command_value
// ---------------------------------------------------------------------------

describe('waitCommandValue', () => {
  test('--max without --for-turn is refused rather than ignored', async () => {
    const seat = bench();
    const kit = recorder();
    const server = scripted(() => ({ body: waitPayload() }));
    const ctx = waitCtx({ sessionPath: seat.sessionPath, session: seat.session, hooks: kit.hooks });
    expect(
      message(await run(waitCommandValue(ctx, waitArgs({}), { maxS: 30 }), server.fetch, seat.store))
    ).toBe('wait --max bounds --for-turn; pass both or neither');
    expect(server.urls).toHaveLength(0);
  });

  test('--max is bounded by the same ceiling the wait is', async () => {
    const seat = bench();
    const kit = recorder();
    const server = scripted(() => ({ body: waitPayload() }));
    const ctx = waitCtx({ sessionPath: seat.sessionPath, session: seat.session, hooks: kit.hooks });
    expect(
      message(
        await run(
          waitCommandValue(ctx, waitArgs({}), { forTurn: true, maxS: 616 }),
          server.fetch,
          seat.store
        )
      )
    ).toBe('max must be in [0, 615]');
  });

  test('a plain wait makes exactly one request and never loops', async () => {
    const seat = bench();
    const kit = recorder();
    const server = scripted(() => ({ body: pvpWake('timeout', { mine: false }) }));
    const ctx = waitCtx({ sessionPath: seat.sessionPath, session: seat.session, hooks: kit.hooks });
    right(await run(waitCommandValue(ctx, waitArgs({})), server.fetch, seat.store));
    expect(server.urls).toHaveLength(1);
    expect(server.urls[0]).toContain('wait_s=120');
    // No `--for-turn`, so nothing mirrors and nothing ticks.
    expect(kit.log.mirroredHealth).toHaveLength(0);
    expect(kit.log.echoed).toHaveLength(0);
  });
});

// ---------------------------------------------------------------------------
// _seat_rebound
// ---------------------------------------------------------------------------

const reboundOf = (seat: Bench): Promise<boolean> =>
  Effect.runPromise(Effect.provide(seatRebound(seat.sessionPath), Layer.succeed(SessionStore, seat.store)));

describe('seatRebound', () => {
  test('is false when no current session can be resolved at all', async () => {
    expect(await reboundOf(bench())).toBe(false);
  });

  test('is false while the workspace still points at this seat', async () => {
    const seat = bench();
    Effect.runSync(seat.store.setCurrentSession(seat.sessionPath));
    expect(await reboundOf(seat)).toBe(false);
  });

  test('is true once `use` has pointed the workspace at another seat', async () => {
    const seat = bench();
    const other = path.join(seat.scratch.workspace.stateRoot, 'session-other-harness.json');
    Effect.runSync(
      seat.scratch.files.writeJson(other, sessionFile({ controller_label: 'other-harness' }))
    );
    Effect.runSync(seat.store.setCurrentSession(other));
    expect(await reboundOf(seat)).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// The renderers
// ---------------------------------------------------------------------------

const decodedWake = (payload: JsonObject, session: Session): Promise<WaitEnvelope> =>
  Effect.runPromise(decodeWait(payload, session, { until: 'phase', afterStateToken: null }));

describe('the wait renderers', () => {
  test('a timeout names the holder instead of calling itself a wake', async () => {
    const seat = bench();
    const wake = await decodedWake(pvpWake('timeout', { mine: false }), seat.session);
    const line = awaitLine(wake);
    expect(line).toContain('still seat 2 AgentPlace2 (pi-gpt-5.6-sol)');
    expect(line).toContain('held 13s');
    expect(line).toContain('9m47s left');
    // The old tail pointed at a command that can only be refused.
    expect(line).not.toContain('next: just turn');
    expect(line).toContain('just wait --for-turn');
    expect(line).toContain('[exit 75]');
  });

  test('an actionable wake keeps the plain briefing tail', async () => {
    const seat = bench();
    const wake = await decodedWake(pvpWake('phase_active', { mine: true }), seat.session);
    const line = awaitLine(wake);
    expect(line.startsWith('T3 | ')).toBe(true);
    expect(line).toContain('next: just turn');
    expect(line).not.toContain('woke phase_active');
  });

  test('a non-phase_active wake on our own phase says which wake it was', async () => {
    const seat = bench();
    const wake = await decodedWake(pvpWake('boundary_recovered', { mine: true }), seat.session);
    expect(awaitLine(wake)).toContain('woke boundary_recovered');
  });

  test('an empty follow drops the tail entirely, on both branches', async () => {
    const seat = bench();
    const mine = await decodedWake(pvpWake('phase_active', { mine: true }), seat.session);
    const theirs = await decodedWake(pvpWake('timeout', { mine: false }), seat.session);
    expect(awaitLine(mine, '')).not.toContain('next:');
    expect(awaitLine(theirs, '')).not.toContain('next:');
  });

  test('the tick line names the seat, how long it has held and what is left', async () => {
    const seat = bench();
    const wake = await decodedWake(pvpWake('timeout', { mine: false }), seat.session);
    const line = waitingTickLine(wake.health);
    expect(line.startsWith('… waiting on ')).toBe(true);
    expect(line).toContain('seat 2 AgentPlace2 (pi-gpt-5.6-sol)');
    expect(line).toContain('held 13s');
    expect(line).toContain('9m47s left');
  });

  test('with nobody named, the tick line says only that the phase is shut', async () => {
    const seat = bench();
    const wake = await decodedWake(pvpWake('timeout', { mine: true }), seat.session);
    expect(waitingTickLine(wake.health)).toBe("… waiting for this seat's phase to open");
  });

  test('renderWait is the await header over the health block', async () => {
    const seat = bench();
    const wake = await decodedWake(pvpWake('phase_active', { mine: true }), seat.session);
    expect(renderWait(wake)).toEqual([
      awaitLine(wake),
      ...renderHealth(wake.health),
    ]);
  });
});
