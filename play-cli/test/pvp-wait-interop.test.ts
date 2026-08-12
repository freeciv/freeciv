/**
 * The multiplayer wait surface, end to end.
 *
 * Ports `PvPWaitInteropTests` from `play/tests/test_client.py:10959-11407`,
 * minus the cases that belong to other units (the phase-marker file is U04's,
 * the health one-liners and `prior_end` are U06's).
 *
 * Every case reproduces something a live two-agent match actually did: a wake
 * reason the client could not parse, an exit status that said "success" for
 * "still not your turn", a briefing printed for a phase the caller did not
 * hold, and a marker file frozen for the whole of somebody else's ten minutes.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { Command } from '@effect/cli';
import { BunContext } from '@effect/platform-bun';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either, Layer } from 'effect';
import { V2_SATISFIED_WAKE_REASONS, V2_WAKE_REASONS } from 'src/constants';
import { V2_WAIT_EXIT_ACTIVE, V2_WAIT_EXIT_RETRY, V2_WAIT_EXIT_TERMINAL } from 'src/exit';
import { decodeHealth, type HealthEnvelope } from 'src/schema/health';
import type { JsonObject } from 'src/schema/primitives';
import { decodeWait, type WaitEnvelope } from 'src/schema/wait';
import { liveWaitHooks, waitCommandWith, type WaitHooksFor } from 'src/commands/wait.cmd';
import { V2_PROTOCOL_CARD } from 'src/render/join';
import { renderWait } from 'src/render/wait';
import { httpFor } from 'src/services/http';
import { DEFAULT_COMMAND_CARD, mirrorDir } from 'src/services/mirror';
import { PrivateFs, type PrivateFsApi } from 'src/services/private-fs';
import { pyJsonDumps } from 'src/services/json-output';
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
  waitArgs,
  waitCommandValue,
  waitCtx,
  waitExitCode,
  type HolderSeatFn,
  type WaitClock,
  type WaitHooks,
} from 'src/services/wait';
import {
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

const stateSchema = {
  empty: emptyV2ClientState,
  validate: () => Effect.void,
  cursorExpired: (): boolean => false,
};

interface Bench {
  readonly sessionPath: string;
  readonly session: Session;
  readonly store: SessionStoreApi;
  readonly files: PrivateFsApi;
}

const bench = (): Bench => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const sessionPath = path.join(scratch.workspace.stateRoot, 'session-codex-gpt-5.6-sol.json');
  Effect.runSync(scratch.files.writeJson(sessionPath, sessionFile()));
  const store = sessionStoreFor(scratch.workspace, scratch.files, stateSchema, {});
  const loaded = Effect.runSync(store.resolveV2(sessionPath));
  return { sessionPath, session: loaded.session, store, files: scratch.files };
};

/** `_holder_seat`; U06 owns the real one, the engine takes it as a hook. */
const holderSeat: HolderSeatFn = (phase) => {
  if (phase === null ||  phase.active) return null;
  const waitingOn = phase.waiting_on;
  if (waitingOn === undefined || waitingOn === null) return null;
  const others = waitingOn.seats.filter((row) => row.is_self === false);
  return others.length === 1 ? (others[0] ?? null) : null;
};

interface Kit {
  readonly hooks: WaitHooks;
  readonly mirrored: HealthEnvelope[];
  readonly ticks: HealthEnvelope[];
  readonly echo: (health: HealthEnvelope) => Effect.Effect<void>;
}

const recorder = (): Kit => {
  const mirrored: HealthEnvelope[] = [];
  const ticks: HealthEnvelope[] = [];
  return {
    mirrored,
    ticks,
    echo: (health) =>
      Effect.sync(() => {
        ticks.push(health);
      }),
    hooks: {
      rememberPage: () => Effect.void,
      mirrorPage: () => Effect.void,
      mirrorHealth: (health) =>
        Effect.sync(() => {
          mirrored.push(health);
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

const answering = (body: unknown): { readonly fetch: typeof fetch; readonly urls: string[] } => {
  const urls: string[] = [];
  const impl = (async (input: Parameters<typeof fetch>[0]): Promise<Response> => {
    urls.push(urlOf(input));
    return jsonResponse(body);
  }) as typeof fetch;
  return { fetch: impl, urls };
};

// ---------------------------------------------------------------------------
// The PvP payloads
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
  readonly remainingS?: number;
  readonly elapsedS?: number;
  readonly gameState?: string;
}

const pvpHealth = (shape: PvpShape): JsonObject => {
  const elapsedS = shape.elapsedS ?? 13;
  const remainingS = shape.remainingS ?? 587;
  const terminal = shape.gameState !== undefined && shape.gameState !== 'running';
  if (terminal) {
    return healthPayload({
      game_state: shape.gameState ?? 'completed',
      phase: null,
      observation_available: false,
      legal_actions_available: false,
    });
  }
  return healthPayload({
    game_state: 'running',
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
      ...(shape.mine
        ? {}
        : {
            waiting_on: {
              kind: 'other_seat',
              summary:
                'Seat 2 AgentPlace2 (pi-gpt-5.6-sol) holds turn 3 phase 1 and has not ended it.',
              waiting_s: elapsedS,
              seats: [OPPONENT],
            },
          }),
    },
  });
};

const pvpWake = (reason: string, shape: PvpShape): JsonObject =>
  waitPayload({ wake_reason: reason, health: pvpHealth(shape) });

const decodedWake = (payload: JsonObject, session: Session): Promise<WaitEnvelope> =>
  Effect.runPromise(decodeWait(payload, session, { until: 'phase', afterStateToken: null }));

// ---------------------------------------------------------------------------
// P0a: the wake reason the server could always send
// ---------------------------------------------------------------------------

describe('boundary_recovered', () => {
  test('is a wake reason the client accepts, and a satisfied one', async () => {
    // It arrives when this seat's native boundary was republished under a wait
    // — and on the `--end --await` path it surfaced as `await failed:` *after*
    // the phase end had applied, which is the one moment a client must not be
    // telling the agent it does not understand the server.
    expect(V2_WAKE_REASONS.has('boundary_recovered')).toBe(true);
    const seat = bench();
    const wake = await decodedWake(pvpWake('boundary_recovered', { mine: true }), seat.session);
    expect(wake.wake_reason).toBe('boundary_recovered');
    expect(waitExitCode(wake)).toBe(V2_WAIT_EXIT_ACTIVE);
    expect(V2_SATISFIED_WAKE_REASONS.has('boundary_recovered')).toBe(true);
  });

  test('the served OpenAPI lists every wake reason the client takes', () => {
    const contract = JSON.parse(
      fs.readFileSync(
        path.join(import.meta.dir, '..', '..', 'play', 'docs', 'full-control-v2.openapi.json'),
        'utf-8'
      )
    ) as {
      components: {
        schemas: {
          WaitEnvelope: { properties: { wake_reason: { enum: ReadonlyArray<string> } } };
        };
      };
    };
    const enumerated = contract.components.schemas.WaitEnvelope.properties.wake_reason.enum;
    expect([...enumerated].sort()).toEqual([...V2_WAKE_REASONS].sort());
  });
});

// ---------------------------------------------------------------------------
// P1: the exit status carries the wake reason
// ---------------------------------------------------------------------------

describe('the exit status', () => {
  const CASES = [
    ['phase_active', { mine: true }, V2_WAIT_EXIT_ACTIVE],
    ['boundary_recovered', { mine: true }, V2_WAIT_EXIT_ACTIVE],
    ['timeout', { mine: false }, V2_WAIT_EXIT_RETRY],
    ['game_terminal', { mine: false, gameState: 'completed' }, V2_WAIT_EXIT_TERMINAL],
  ] as const;

  test.each(CASES.map(([reason, shape, code]) => [reason, code, shape] as const))(
    'a real %s wake exits %p',
    async (reason, code, shape) => {
      const seat = bench();
      const kit = recorder();
      const server = answering(pvpWake(reason, shape));
      const ctx = waitCtx({
        sessionPath: seat.sessionPath,
        session: seat.session,
        hooks: kit.hooks,
      });
      const wake = right(await run(waitCommandValue(ctx, waitArgs({})), server.fetch, seat.store));
      expect(waitExitCode(wake)).toBe(code);
    }
  );

  test('a lobby timeout is EX_TEMPFAIL, not success, and calls no state route', async () => {
    const seat = bench();
    const kit = recorder();
    const server = answering(
      waitPayload({
        wake_reason: 'timeout',
        health: healthPayload({
          game_state: 'lobby',
          phase: {
            state: 'awaiting_agent',
            turn: 0,
            phase: 0,
            active: false,
            timing: {
              mode: 'default',
              timeout_s: null,
              deadline_started_at: null,
              deadline_at: null,
              elapsed_s: null,
              remaining_s: null,
            },
          },
        }),
      })
    );
    const ctx = waitCtx({ sessionPath: seat.sessionPath, session: seat.session, hooks: kit.hooks });
    const wake = right(
      await run(waitCommandValue(ctx, waitArgs({ waitS: 0 })), server.fetch, seat.store)
    );
    // A timeout means "still not yours, call me again", which is EX_TEMPFAIL
    // and not success — and the lobby never costs a `/state` round trip.
    expect(server.urls).toHaveLength(1);
    expect(server.urls[0]).toContain('/me/wait?');
    expect(server.urls.some((url) => url.includes('/state'))).toBe(false);
    expect(waitExitCode(wake)).toBe(V2_WAIT_EXIT_RETRY);
  });

  test('a terminal game stops the loop with EX_NOINPUT', async () => {
    const seat = bench();
    const kit = recorder();
    const server = answering(pvpWake('game_terminal', { mine: false, gameState: 'completed' }));
    const ctx = waitCtx({ sessionPath: seat.sessionPath, session: seat.session, hooks: kit.hooks });
    const wake = right(await run(waitCommandValue(ctx, waitArgs({})), server.fetch, seat.store));
    expect(waitExitCode(wake)).toBe(V2_WAIT_EXIT_TERMINAL);
  });

  test('the JSON payload is unchanged by the exit status', async () => {
    const seat = bench();
    const kit = recorder();
    const payload = pvpWake('timeout', { mine: false });
    const server = answering(payload);
    const ctx = waitCtx({ sessionPath: seat.sessionPath, session: seat.session, hooks: kit.hooks });
    const wake = right(await run(waitCommandValue(ctx, waitArgs({})), server.fetch, seat.store));
    expect(waitExitCode(wake)).toBe(V2_WAIT_EXIT_RETRY);
    // What `--json` prints is the *validated* envelope, and it must round-trip
    // to the wire payload byte for byte.
    expect(JSON.parse(pyJsonDumps(wake, { indent: 2, sortKeys: true }))).toEqual(payload);
  });
});

// ---------------------------------------------------------------------------
// P2: bounds and --for-turn
// ---------------------------------------------------------------------------

interface Clocked {
  readonly fetch: typeof fetch;
  readonly clock: WaitClock;
  readonly blocked: number[];
  readonly now: { seconds: number };
}

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

describe('the wait ceiling', () => {
  test('covers a whole opponent phase', async () => {
    expect(V2_WAIT_S_MAX).toBe(615);
    const seat = bench();
    const kit = recorder();
    const server = answering(pvpWake('phase_active', { mine: true }));
    const ctx = waitCtx({ sessionPath: seat.sessionPath, session: seat.session, hooks: kit.hooks });
    right(await run(waitCommandValue(ctx, waitArgs({ waitS: 615 })), server.fetch, seat.store));
    expect(server.urls[0]).toContain('wait_s=615');
    expect(
      message(await run(waitCommandValue(ctx, waitArgs({ waitS: 616 })), server.fetch, seat.store))
    ).toContain('[0, 615]');
  });
});

describe('--for-turn', () => {
  test('is bounded by the holder remaining deadline', async () => {
    const seat = bench();
    const kit = recorder();
    const fake = clocked((elapsed) =>
      pvpWake('timeout', {
        mine: false,
        remainingS: Math.max(0, 40 - elapsed),
        elapsedS: Math.min(600, 560 + elapsed),
      })
    );
    const ctx = waitCtx({
      sessionPath: seat.sessionPath,
      session: seat.session,
      hooks: kit.hooks,
      clock: fake.clock,
    });
    const wake = right(
      await run(
        waitCommandValue(ctx, waitArgs({}), { forTurn: true, echo: kit.echo }),
        fake.fetch,
        seat.store
      )
    );
    expect(waitExitCode(wake)).toBe(V2_WAIT_EXIT_RETRY);
    // Short internal polls, never one 120 s block.
    expect(fake.blocked.every((item) => item <= V2_WAIT_TICK_S)).toBe(true);
    expect(fake.now.seconds).toBeGreaterThanOrEqual(40);
    expect(fake.now.seconds).toBeLessThanOrEqual(40 + V2_FOR_TURN_GRACE_S);
    // Every tick said what it was waiting on.
    expect(kit.ticks).toHaveLength(fake.blocked.length - 1);
    expect(kit.ticks[0]?.phase?.waiting_on?.seats[0]?.player_name).toBe('AgentPlace2');
  });

  test('returns the moment the phase is ours', async () => {
    const seat = bench();
    const kit = recorder();
    const fake = clocked((elapsed) =>
      pvpWake(elapsed >= 30 ? 'phase_active' : 'timeout', {
        mine: elapsed >= 30,
        remainingS: Math.max(0, 300 - elapsed),
      })
    );
    const ctx = waitCtx({
      sessionPath: seat.sessionPath,
      session: seat.session,
      hooks: kit.hooks,
      clock: fake.clock,
    });
    const wake = right(
      await run(waitCommandValue(ctx, waitArgs({}), { forTurn: true }), fake.fetch, seat.store)
    );
    expect(waitExitCode(wake)).toBe(V2_WAIT_EXIT_ACTIVE);
    expect(fake.now.seconds).toBe(30);
  });

  test('--max is a hard ceiling over the holder deadline', async () => {
    const seat = bench();
    const kit = recorder();
    const fake = clocked((elapsed) =>
      pvpWake('timeout', { mine: false, remainingS: Math.max(0, 600 - elapsed) })
    );
    const ctx = waitCtx({
      sessionPath: seat.sessionPath,
      session: seat.session,
      hooks: kit.hooks,
      clock: fake.clock,
    });
    const wake = right(
      await run(
        waitCommandValue(ctx, waitArgs({}), { forTurn: true, maxS: 45 }),
        fake.fetch,
        seat.store
      )
    );
    expect(waitExitCode(wake)).toBe(V2_WAIT_EXIT_RETRY);
    expect(fake.now.seconds).toBe(45);
  });

  test('--max without --for-turn is refused rather than ignored', async () => {
    const seat = bench();
    const kit = recorder();
    const server = answering(pvpWake('phase_active', { mine: true }));
    const ctx = waitCtx({ sessionPath: seat.sessionPath, session: seat.session, hooks: kit.hooks });
    expect(
      message(
        await run(waitCommandValue(ctx, waitArgs({}), { maxS: 30 }), server.fetch, seat.store)
      )
    ).toContain('--for-turn');
  });

  test('a plain wait still makes exactly one request', async () => {
    const seat = bench();
    const kit = recorder();
    const server = answering(pvpWake('timeout', { mine: false }));
    const ctx = waitCtx({ sessionPath: seat.sessionPath, session: seat.session, hooks: kit.hooks });
    right(await run(waitCommandValue(ctx, waitArgs({})), server.fetch, seat.store));
    expect(server.urls).toHaveLength(1);
    expect(server.urls[0]).toContain('wait_s=120');
  });
});

// ---------------------------------------------------------------------------
// P3: the marker file, refreshed on every tick
// ---------------------------------------------------------------------------

describe('the phase marker', () => {
  test('is written on every tick of a wait, not once at the end', async () => {
    const seat = bench();
    const kit = recorder();
    const fake = clocked((elapsed) =>
      pvpWake(elapsed >= 45 ? 'phase_active' : 'timeout', {
        mine: elapsed >= 45,
        remainingS: Math.max(0, 300 - elapsed),
        elapsedS: Math.min(600, 300 + elapsed),
      })
    );
    const ctx = waitCtx({
      sessionPath: seat.sessionPath,
      session: seat.session,
      hooks: kit.hooks,
      clock: fake.clock,
    });
    right(
      await run(
        waitCommandValue(ctx, waitArgs({}), { forTurn: true, echo: kit.echo }),
        fake.fetch,
        seat.store
      )
    );
    // One write per request: refreshed between every pair of polls.
    expect(kit.mirrored).toHaveLength(fake.blocked.length);
    expect(kit.mirrored.map((item) => item.phase?.timing.remaining_s)).toEqual([285, 270, 255]);
    // The last write is the wake itself, and it is this seat's own phase.
    expect(kit.mirrored.at(-1)?.phase?.active).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// P4a: the strings
// ---------------------------------------------------------------------------

describe('the rendered wake', () => {
  test('a timeout wake names the holder instead of calling it a wake', async () => {
    const seat = bench();
    const wake = await decodedWake(pvpWake('timeout', { mine: false }), seat.session);
    const lines = renderWait(wake);
    expect(lines[0]).toContain('still seat 2 AgentPlace2 (pi-gpt-5.6-sol)');
    expect(lines[0]).toContain('held 13s');
    expect(lines[0]).toContain('9m47s left');
    // The old tail pointed at a command that can only be refused.
    expect(lines[0]).not.toContain('next: just turn');
    expect(lines[0]).toContain('just wait --for-turn');
    expect(lines[0]).toContain(`[exit ${V2_WAIT_EXIT_RETRY}]`);
    expect(lines[1]).toContain('NOT YOUR TURN · seat 2 AgentPlace2');
  });

  test('text is the default and nothing raw leaks into it', async () => {
    const seat = bench();
    const wake = await decodedWake(pvpWake('phase_active', { mine: true }), seat.session);
    const text = renderWait(wake).join('\n');
    expect(text.startsWith('{')).toBe(false);
    expect(text).toContain('YOUR TURN · t3/p1');
    expect(text).toContain('next: just turn');
    expect(renderWait(wake)[1]?.startsWith('health running')).toBe(true);
    expect(text).not.toContain('deadline_started_at');
  });
});

// ---------------------------------------------------------------------------
// `play wait`, end to end
// ---------------------------------------------------------------------------

/**
 * Run the real command over a fake supervisor and report status + stdout.
 *
 * `makeHooks` is the seam the command takes: the recorder for cases that only
 * care about the wire and the exit status, and `liveWaitHooks` for the cases
 * that must see what the command actually writes into the mirror.
 */
const runCommandWith = async (
  seat: Bench,
  makeHooks: WaitHooksFor,
  fetchImpl: typeof fetch,
  flags: ReadonlyArray<string>
): Promise<{ readonly code: number; readonly out: ReadonlyArray<string> }> => {
  const command = waitCommandWith(makeHooks);
  const out: string[] = [];
  const originalLog = console.log;
  console.log = (...parts: ReadonlyArray<unknown>) => out.push(parts.join(' '));
  try {
    const either = await Effect.runPromise(
      Effect.either(
        Effect.provide(
          Command.run(command, { name: 'play', version: '0.1.0' })([
            'bun',
            'play',
            '--session',
            seat.sessionPath,
            ...flags,
          ]),
          Layer.mergeAll(
            layers(fetchImpl, seat.store),
            BunContext.layer,
            Layer.succeed(PrivateFs, seat.files)
          )
        )
      )
    );
    if (Either.isRight(either)) return { code: 0, out };
    const failure = either.left;
    return {
      code: failure._tag === 'ExitCodeSignal' ? failure.code : 2,
      out,
    };
  } finally {
    console.log = originalLog;
  }
};

/** Blocked once, then ours: exactly one internal tick, then the wake. */
const oneTickThenOurs = (): typeof fetch => {
  const calls = { count: 0 };
  return (async (_input: Parameters<typeof fetch>[0]): Promise<Response> => {
    calls.count += 1;
    return jsonResponse(
      calls.count === 1
        ? pvpWake('timeout', { mine: false, remainingS: 30 })
        : pvpWake('phase_active', { mine: true })
    );
  }) as typeof fetch;
};

/** The recorder seam: the wire and the exit status, no filesystem writes. */
const runWaitCommand = (
  seat: Bench,
  kit: Kit,
  fetchImpl: typeof fetch,
  flags: ReadonlyArray<string>
): Promise<{ readonly code: number; readonly out: ReadonlyArray<string> }> =>
  runCommandWith(seat, () => Effect.succeed(kit.hooks), fetchImpl, flags);

describe('play wait', () => {
  const CASES = [
    ['phase_active', { mine: true }, V2_WAIT_EXIT_ACTIVE],
    ['boundary_recovered', { mine: true }, V2_WAIT_EXIT_ACTIVE],
    ['timeout', { mine: false }, V2_WAIT_EXIT_RETRY],
    ['game_terminal', { mine: false, gameState: 'completed' }, V2_WAIT_EXIT_TERMINAL],
  ] as const;

  test.each(CASES.map(([reason, shape, code]) => [reason, code, shape] as const))(
    'exits %p → %p on a real wake',
    async (reason, code, shape) => {
      const seat = bench();
      const kit = recorder();
      const result = await runWaitCommand(seat, kit, answering(pvpWake(reason, shape)).fetch, []);
      expect(result.code).toBe(code);
    }
  );

  test('prints compact text and keeps JSON behind the flag', async () => {
    const seat = bench();
    const payload = pvpWake('phase_active', { mine: true });

    const text = await runWaitCommand(seat, recorder(), answering(payload).fetch, []);
    expect(text.code).toBe(V2_WAIT_EXIT_ACTIVE);
    expect(text.out[0]?.startsWith('{')).toBe(false);
    expect(text.out[0]).toContain('YOUR TURN · t3/p1');
    expect(text.out[0]).toContain('next: just turn');
    expect(text.out[1]?.startsWith('health running')).toBe(true);
    expect(text.out.join('\n')).not.toContain('deadline_started_at');

    const json = await runWaitCommand(seat, recorder(), answering(payload).fetch, ['--json']);
    expect(json.code).toBe(V2_WAIT_EXIT_ACTIVE);
    expect(JSON.parse(json.out.join('\n'))).toEqual(payload);
  });

  test('the JSON payload is unchanged by the exit status', async () => {
    const seat = bench();
    const payload = pvpWake('timeout', { mine: false });
    const json = await runWaitCommand(seat, recorder(), answering(payload).fetch, ['--json']);
    expect(json.code).toBe(V2_WAIT_EXIT_RETRY);
    expect(JSON.parse(json.out.join('\n'))).toEqual(payload);
  });

  test('a --for-turn tick is prose on stdout, and the wake follows it', async () => {
    const seat = bench();
    const result = await runWaitCommand(seat, recorder(), oneTickThenOurs(), ['--for-turn']);
    expect(result.code).toBe(V2_WAIT_EXIT_ACTIVE);
    expect(result.out[0]).toContain('… waiting on seat 2 AgentPlace2 (pi-gpt-5.6-sol)');
    expect(result.out[1]).toContain('YOUR TURN · t3/p1');
  });

  test('--json prints one object and no tick prose', async () => {
    const seat = bench();
    const result = await runWaitCommand(seat, recorder(), oneTickThenOurs(), [
      '--json',
      '--for-turn',
    ]);
    expect(result.code).toBe(V2_WAIT_EXIT_ACTIVE);
    expect(result.out.join('\n')).not.toContain('… waiting');
    expect(JSON.parse(result.out.join('\n')).wake_reason).toBe('phase_active');
  });

  test('both spellings of --wait-s are accepted, and never together', async () => {
    const seat = bench();
    const payload = pvpWake('phase_active', { mine: true });
    for (const spelling of ['--wait-s', '--wait_s']) {
      const server = answering(payload);
      const result = await runWaitCommand(seat, recorder(), server.fetch, [spelling, '30']);
      expect(result.code).toBe(V2_WAIT_EXIT_ACTIVE);
      expect(server.urls[0]).toContain('wait_s=30');
    }
    const both = await runWaitCommand(seat, recorder(), answering(payload).fetch, [
      '--wait-s',
      '30',
      '--wait_s',
      '30',
    ]);
    expect(both.code).toBe(2);
  });

  test('--max without --for-turn refuses instead of ignoring the flag', async () => {
    const seat = bench();
    const server = answering(pvpWake('phase_active', { mine: true }));
    const result = await runWaitCommand(seat, recorder(), server.fetch, ['--max', '30']);
    expect(result.code).toBe(2);
    expect(server.urls).toHaveLength(0);
  });
});

// ---------------------------------------------------------------------------
// The live hook record
// ---------------------------------------------------------------------------

/**
 * Everything above drives the command through the recorder seam, which proves
 * the engine and the exit contract but never touches `liveWaitHooks` — the
 * record the shipped binary actually runs.  These cases run the real hooks
 * against a real scratch mirror.
 *
 * The one that matters is `mirrorHealth`.  `_mirror_health`
 * (client.py:3068-3072) passes `commands=V2_PROTOCOL_CARD` unconditionally, and
 * `_wait_until_turn` calls it on every tick — so a `--for-turn` wait rewrites
 * `state/header.txt`, and `just show header` (client.py:11170 maps it to that
 * file) must still print the whole card afterwards.  Forwarding no options
 * silently downgrades the header to the 5-line `_DEFAULT_COMMAND_CARD`, which
 * costs the agent the ALIASES/ERRORS/ONE CALL PER TURN/MULTIPLAYER/WHICH
 * BINDING block it is told to read.  This is the TS half of
 * `test_v2_join_card_and_state_header_carry_the_same_contract`
 * (tests/test_client.py:7194-7254).
 */
describe('liveWaitHooks', () => {
  const headerText = (seat: Bench): string => {
    const dir = Effect.runSync(mirrorDir(seat.sessionPath));
    return fs.readFileSync(path.join(dir, 'state', 'header.txt'), 'utf-8');
  };

  test('mirrorHealth writes the full protocol card into state/header.txt', async () => {
    const seat = bench();
    const health = await Effect.runPromise(
      decodeHealth(pvpHealth({ mine: true }), seat.session)
    );
    const hooks = await Effect.runPromise(
      Effect.provide(
        liveWaitHooks(seat.sessionPath, seat.session),
        Layer.merge(Layer.succeed(PrivateFs, seat.files), Layer.succeed(SessionStore, seat.store))
      )
    );
    await Effect.runPromise(hooks.mirrorHealth(health, 'wait'));

    const header = headerText(seat);
    for (const line of V2_PROTOCOL_CARD) expect(header).toContain(line);
    // The default card is what a dropped option falls back to; none of its
    // lines belong in a header written by the client.
    for (const line of DEFAULT_COMMAND_CARD) expect(header).not.toContain(line);
    // And the secrets stay out, exactly as the Python asserts.
    expect(header).not.toContain(seat.session.agentToken);
    expect(header).not.toContain('state_token');
  });

  test('a --for-turn wait leaves a header a later `show header` can still read', async () => {
    const seat = bench();
    const result = await runCommandWith(seat, liveWaitHooks, oneTickThenOurs(), ['--for-turn']);
    expect(result.code).toBe(V2_WAIT_EXIT_ACTIVE);
    // Two ticks wrote the header; the last write must still carry the card.
    const header = headerText(seat);
    for (const line of V2_PROTOCOL_CARD) expect(header).toContain(line);
    expect(header).toContain('ONE CALL PER TURN');
    expect(header).toContain('MULTIPLAYER');
    expect(header).toContain('WHICH BINDING');
  });

  test('the marker file is refreshed on the way through, not only at the wake', async () => {
    const seat = bench();
    const result = await runCommandWith(seat, liveWaitHooks, oneTickThenOurs(), ['--for-turn']);
    expect(result.code).toBe(V2_WAIT_EXIT_ACTIVE);
    const dir = Effect.runSync(mirrorDir(seat.sessionPath));
    const marker = JSON.parse(
      fs.readFileSync(path.join(dir, 'state', 'phase.json'), 'utf-8')
    ) as { readonly turn: number; readonly active: boolean };
    expect(marker.turn).toBe(3);
    expect(marker.active).toBe(true);
  });

  /**
   * `_legacy_wait_value` (client.py:9976-9977) runs `_remember_page` *and*
   * `_mirror_page(path, cached, overview, "wait")` on the overview page it
   * polled.  The wake is identical either way, which is exactly why an inert
   * `mirrorPage` was invisible: the divergence only shows up in the *next*
   * command, which reads a `state/*.tsv` still stamped at the old revision.
   *
   * The route this reaches is the pre-private-`/wait` supervisor's bare
   * `{"error": "..."}` 404 — the shape `isMissingRouteRefusal` detects.
   */
  const legacyRevisionServer = (
    overview: JsonObject
  ): { readonly fetch: typeof fetch; readonly urls: string[] } => {
    const urls: string[] = [];
    const impl = (async (input: Parameters<typeof fetch>[0]): Promise<Response> => {
      const url = urlOf(input);
      urls.push(url);
      if (url.includes('/me/wait?')) return jsonResponse({ error: 'Not Found' }, 404);
      if (url.includes('/me/health')) return jsonResponse(pvpHealth({ mine: false }));
      return jsonResponse(overview);
    }) as typeof fetch;
    return { fetch: impl, urls };
  };

  const OVERVIEW_PAGE: JsonObject = pagePayload([], {
    state_revision: { turn: 5, revision: 13, state_token: 'token_5_13' },
    page: {
      section: 'overview',
      items: [
        {
          client_state: 'running',
          turn: 5,
          map: { width: 64, height: 48 },
          player: {
            government: 'Despotism',
            economy: { gold: 50, tax: 40, luxury: 0, science: 60 },
          },
          counts: { cities: 1, units: 2, known_tiles: 40, legal_actions: 9, chat: 4 },
        },
      ],
      total_items: 1,
      next_cursor: null,
      cursor_expires_at: null,
    },
  });

  test('the legacy --until revision fallback projects the page it woke on', async () => {
    const seat = bench();
    Effect.runSync(
      seat.store.writeState(seat.sessionPath, {
        ...emptyV2ClientState(seat.session),
        last_revision: { turn: 5, revision: 12, state_token: 'token_5_12' },
      })
    );
    const server = legacyRevisionServer(OVERVIEW_PAGE);
    const result = await runCommandWith(seat, liveWaitHooks, server.fetch, [
      '--until',
      'revision',
    ]);

    // `revision_changed` is a satisfied wake, so the status is 0 either way —
    // the mirror is the only place the missing projection was ever visible.
    expect(result.code).toBe(V2_WAIT_EXIT_ACTIVE);
    expect(server.urls.some((url) => url.includes('section=overview&limit=16'))).toBe(true);

    const dir = Effect.runSync(mirrorDir(seat.sessionPath));
    const overview = fs.readFileSync(path.join(dir, 'state', 'overview.tsv'), 'utf-8');
    // Stamped at the revision the wake carried, not the baseline it started at.
    expect(overview.split('\n')[0]).toBe('# rev 13 turn 5');
    expect(overview).toContain('64x48');
    expect(overview).toContain('tax40 lux0 sci60');
    expect(overview).toContain('count_chat');
    // `state/delta.md` moves with it, exactly as `update_from_page` writes it.
    expect(fs.existsSync(path.join(dir, 'state', 'delta.md'))).toBe(true);
  });

  test('the projection uses the aliases this seat just learned from the page', async () => {
    const seat = bench();
    Effect.runSync(
      seat.store.writeState(seat.sessionPath, {
        ...emptyV2ClientState(seat.session),
        last_revision: { turn: 5, revision: 12, state_token: 'token_5_12' },
      })
    );
    const result = await runCommandWith(
      seat,
      liveWaitHooks,
      legacyRevisionServer(OVERVIEW_PAGE).fetch,
      ['--until', 'revision']
    );
    expect(result.code).toBe(V2_WAIT_EXIT_ACTIVE);
    // `_remember_page` runs before `_mirror_page` and CPython passes
    // `_alias_map(cached)` — the state the ingestion just folded the page into.
    // Reading it back must therefore see the wake's own revision.
    const state = Effect.runSync(seat.store.readState(seat.sessionPath, seat.session));
    expect(state.last_revision?.state_token).toBe('token_5_13');
  });
});
