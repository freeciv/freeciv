/**
 * U12 — `turn --end`, `--await` and `--brief`.
 *
 * Ports `test_v2_turn_end_await_ends_the_phase_then_blocks_then_heads`,
 * `test_v2_await_failure_never_hides_an_applied_phase_end` and the composite
 * `--json` shape of `_command_turn_end` from `play/tests/test_client.py`, plus
 * the invariant PORT_MAP calls out explicitly and CPython only implies:
 *
 *   **an applied phase end never exits non-zero because of how the wait after
 *   it turned out.**
 *
 * A timed-out wake, a wake into somebody else's phase and a briefing that
 * cannot be built are three different disappointments; none of them is the end
 * failing, and a job supervisor that reads a non-zero status re-ends a phase
 * that is already over.
 */
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Command, ValidationError } from '@effect/cli';
import { BunContext } from '@effect/platform-bun';
import { Cause, Effect, Exit, Layer, Option } from 'effect';
import { PHASE_END_REMEDY } from 'src/constants';
import { playerError } from 'src/errors';
import type { RenderTurnDeps } from 'src/render/turn';
import type { BatchDisposition } from 'src/schema/batch';
import type { JsonObject, JsonValue } from 'src/schema/primitives';
import type { Revision } from 'src/schema/revision';
import { liveTurnSeams, turnCommandWith, turnCtx, type TurnSeamsFor } from 'src/commands/turn.cmd';
import type { DecisionDeps } from 'src/services/decisions';
import { httpFor } from 'src/services/http';
import { compactLegalAction } from 'src/services/legal-compact';
import { DEFAULT_COMMAND_CARD, HEADER_FILE, mirrorText } from 'src/services/mirror';
import { PrivateFs, Workspace } from 'src/services/private-fs';
import {
  SessionStore,
  emptyV2ClientState,
  sessionStoreFor,
  type Session,
  type SessionStoreApi,
  type V2ClientState,
} from 'src/services/session-store';
import {
  awaitAndBrief,
  cachedKindAction,
  commandTurnEnd,
  phaseEnd,
  resolveKindAction,
  turnBriefingLocked,
  turnEndJson,
  type PhaseEndDeps,
  type TurnCtx,
} from 'src/services/turn-end';
import { mirrorFile, mirrorIsFresh, type TurnHooks } from 'src/services/turn-pages';
import { V2Client, v2ClientFor } from 'src/services/v2-client';
import type { WaitHooks } from 'src/services/wait';
import {
  FIXTURE_AGENT_ID,
  FIXTURE_GAME_ID,
  fakeFetch,
  healthPayload,
  jsonResponse,
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

const REVISION: Revision = { turn: 3, revision: 7, state_token: 'token_3_7' };
const ENDED: Revision = { turn: 3, revision: 8, state_token: 'token_3_8' };

const PHASE_END_ID = `action_${'e'.repeat(26)}`;

const phaseEndDescriptor = (revision: Revision): JsonObject => ({
  action_id: PHASE_END_ID,
  kind: 'phase.end',
  label: 'End phase',
  subject: {
    operation: 'end',
    actor: { id: `player_${'f'.repeat(32)}`, type: 'player', name: 'AgentPlace1' },
    target: null,
    probability: { kind: 'exact', minimum_percent: 100, maximum_percent: 100 },
  },
  arguments_schema: { type: 'object' },
  state_revision: { ...revision },
});

const disposition = (state: string): BatchDisposition =>
  ({
    schema_version: 2,
    control_protocol: 'full-control-v2',
    game_id: FIXTURE_GAME_ID,
    agent_id: FIXTURE_AGENT_ID,
    batch_id: 'batch_end',
    disposition: 'receipt_terminal',
    receipt: {
      schema_version: 2,
      control_protocol: 'full-control-v2',
      game_id: FIXTURE_GAME_ID,
      agent_id: FIXTURE_AGENT_ID,
      batch_id: 'batch_end',
      receipt_state: state,
      idempotent: true,
      state_revision: ENDED,
      error: null,
      observation: null,
    },
    error: null,
  }) as unknown as BatchDisposition;

/** The order engine, as far as `phase.end` reaches it. */
const decisionDeps = (): DecisionDeps => ({
  orderPool: (state) =>
    Effect.forEach(Object.values(state.actions), (descriptor) =>
      compactLegalAction(descriptor as JsonObject)
    ),
  orderActor: () => '',
  orderOperation: () => 'end',
  orderTargetKeys: () => [],
  orderMatch: () => Effect.succeed({}),
  tier1Verbs: {},
  playerScopeAlias: () => Effect.succeed('p1'),
});

const renderers: RenderTurnDeps = {
  routeSummary: () => Effect.succeed(''),
  economyText: () => Effect.succeed('economy'),
  researchText: () => Effect.succeed('research'),
  scoreText: () => '',
  renderSectionItems: () => Effect.succeed([]),
  renderTiles: () => Effect.succeed([]),
};

const hooks: TurnHooks = {
  rememberPage: () => Effect.void,
  mirrorPage: () => Effect.void,
  fetchStateSection: () => Effect.void,
};

const waitHooks: WaitHooks = {
  rememberPage: () => Effect.void,
  mirrorPage: () => Effect.void,
  mirrorHealth: () => Effect.void,
  holderSeat: () => null,
};

interface Log {
  readonly order: string[];
  drained: number;
  readonly stdout: string[];
}

interface PhaseEndOverrides {
  readonly submit?: (batchId: string) => Effect.Effect<never, never> | undefined;
  readonly receiptOk?: boolean;
  readonly defaultArguments?: JsonObject | null;
}

const phaseEndDeps = (log: Log, overrides: PhaseEndOverrides = {}): PhaseEndDeps => ({
  drainLegal: Effect.sync(() => {
    log.order.push('legal');
    log.drained += 1;
  }),
  defaultArguments: () =>
    overrides.defaultArguments === undefined ? {} : overrides.defaultArguments,
  persistBatchForAction: (actionId) =>
    Effect.sync(() => {
      log.order.push(`persist:${actionId}`);
      return 'batch_end';
    }),
  submitPersistedBatch: () =>
    Effect.sync(() => {
      log.order.push('batch');
      return { disposition: disposition('applied'), warning: '', exitCode: 0 };
    }),
  renderDisposition: () => ['phase end → applied rev8/t3  batch_end'],
  orderReceiptOk: () => overrides.receiptOk ?? true,
});

interface Bench {
  readonly ctx: TurnCtx;
  readonly log: Log;
  readonly store: SessionStoreApi;
  readonly session: Session;
  readonly sessionPath: string;
  // The scratch workspace is part of the merge, so the mirror writes the
  // briefing makes are real files; naming `PrivateFs` here lets the whole
  // command run against this bench, not just the pieces that take a context.
  readonly layer: Layer.Layer<V2Client | SessionStore | Workspace | PrivateFs>;
  readonly run: <A, E>(
    effect: Effect.Effect<A, E, V2Client | SessionStore | import('src/services/private-fs').PrivateFs>
  ) => Promise<import('effect/Either').Either<A, E>>;
}

const bench = (
  fetchImpl: typeof fetch,
  options: { readonly actions?: Readonly<Record<string, JsonValue>>; readonly overrides?: PhaseEndOverrides } = {}
): Bench => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const sessionPath = path.join(scratch.workspace.stateRoot, 'session-codex-gpt-5.6-sol.json');
  Effect.runSync(scratch.files.writeJson(sessionPath, sessionFile()));
  const store = sessionStoreFor(scratch.workspace, scratch.files, stateSchema, {});
  const loaded = Effect.runSync(store.resolveV2(sessionPath));
  const seeded: V2ClientState = {
    ...emptyV2ClientState(loaded.session),
    last_revision: REVISION,
    actions: options.actions ?? {},
  };
  Effect.runSync(Effect.provide(store.writeState(sessionPath, seeded), scratch.layer));

  const log: Log = { order: [], drained: 0, stdout: [] };
  const ctx: TurnCtx = {
    sessionPath,
    session: loaded.session,
    hooks,
    waitHooks,
    decisions: decisionDeps(),
    renderers,
    phaseEnd: phaseEndDeps(log, options.overrides ?? {}),
  };
  const layer = Layer.mergeAll(
    Layer.succeed(V2Client, v2ClientFor(httpFor(fetchImpl), () => Effect.void)),
    Layer.succeed(SessionStore, store),
    scratch.layer
  );
  return {
    ctx,
    log,
    store,
    session: loaded.session,
    sessionPath,
    layer,
    run: (effect) => Effect.runPromise(Effect.either(Effect.provide(effect, layer))),
  };
};

/** Capture stdout for one effect: the receipt lines are the contract. */
const captured = async <A, E>(
  body: () => Promise<import('effect/Either').Either<A, E>>
): Promise<{ readonly result: import('effect/Either').Either<A, E>; readonly lines: string[] }> => {
  const printed: string[] = [];
  const log = console.log;
  const error = console.error;
  console.log = (...parts: ReadonlyArray<unknown>): void => {
    printed.push(parts.map((part) => String(part)).join(' '));
  };
  console.error = (): void => undefined;
  try {
    const result = await body();
    return {
      result,
      lines: printed
        .join('\n')
        .split('\n')
        .filter((line) => line !== ''),
    };
  } finally {
    console.log = log;
    console.error = error;
  }
};

const wakeFetch = (payload: JsonObject): typeof fetch =>
  fakeFetch(new Map([['/wait', { body: payload }]]));

const activeWake = (): JsonObject =>
  waitPayload({
    wake_reason: 'phase_active',
    health: healthPayload({
      phase: {
        state: 'awaiting_agent',
        turn: 3,
        phase: 1,
        active: true,
        timing: {
          mode: 'default',
          timeout_s: 180,
          deadline_started_at: 1000,
          deadline_at: 1180,
          elapsed_s: 0,
          remaining_s: 180,
        },
      },
    }),
    // A `--until phase` wake never carries a revision; the envelope validator
    // refuses one that does, so the header reads `T3` with no `revN/tN`.
    state_revision: null,
  });

const timedOutWake = (): JsonObject =>
  waitPayload({
    wake_reason: 'timeout',
    health: healthPayload({
      phase: {
        state: 'native_phase',
        turn: 3,
        phase: 1,
        active: false,
        timing: {
          mode: 'default',
          timeout_s: 180,
          deadline_started_at: 1000,
          deadline_at: 1180,
          elapsed_s: 180,
          remaining_s: 0,
        },
      },
    }),
    state_revision: null,
  });

// ---------------------------------------------------------------------------

describe('_cached_kind_action / _resolve_kind_action', () => {
  test('exactly one cached action of the kind resolves; two are ambiguous', async () => {
    const state = (actions: Readonly<Record<string, JsonValue>>): V2ClientState => ({
      ...emptyV2ClientState({ gameId: FIXTURE_GAME_ID, agentId: FIXTURE_AGENT_ID } as Session),
      last_revision: REVISION,
      actions,
    });
    const deps = decisionDeps();
    const one = Effect.runSync(
      cachedKindAction(state({ a: phaseEndDescriptor(REVISION) }), 'phase.end', deps)
    );
    expect(one?.['action_id']).toBe(PHASE_END_ID);
    const two = Effect.runSync(
      cachedKindAction(
        state({
          a: phaseEndDescriptor(REVISION),
          b: { ...phaseEndDescriptor(REVISION), action_id: 'action_other' },
        }),
        'phase.end',
        deps
      )
    );
    expect(two).toBeNull();
  });

  test('a cold cache drains once, then refuses naming the remedy', async () => {
    const kit = bench(wakeFetch(activeWake()));
    const outcome = await kit.run(resolveKindAction(kit.ctx, 'phase.end', PHASE_END_REMEDY));
    expect(outcome._tag).toBe('Left');
    if (outcome._tag === 'Left') {
      expect((outcome.left as { message: string }).message).toBe(
        `no phase.end action is enumerable for this seat right now; ${PHASE_END_REMEDY}`
      );
    }
    expect(kit.log.drained).toBe(1);
  });
});

describe('_phase_end_locked', () => {
  test('resolves the capability, submits it and renders the receipt', async () => {
    const kit = bench(wakeFetch(activeWake()), {
      actions: { [PHASE_END_ID]: phaseEndDescriptor(REVISION) },
    });
    const outcome = await kit.run(phaseEnd(kit.ctx));
    expect(outcome._tag).toBe('Right');
    if (outcome._tag !== 'Right') return;
    expect(outcome.right.exitCode).toBe(0);
    expect(outcome.right.lines).toEqual(['phase end → applied rev8/t3  batch_end']);
    // The cache already held the handle, so nothing was enumerated.
    expect(kit.log.order).toEqual([`persist:${PHASE_END_ID}`, 'batch']);
  });

  test('a phase.end that takes arguments refuses and names `just batch`', async () => {
    const kit = bench(wakeFetch(activeWake()), {
      actions: { [PHASE_END_ID]: phaseEndDescriptor(REVISION) },
      overrides: { defaultArguments: null },
    });
    const outcome = await kit.run(phaseEnd(kit.ctx));
    expect(outcome._tag).toBe('Left');
    if (outcome._tag === 'Left') {
      expect((outcome.left as { message: string }).message).toContain(
        'just legal --kind phase.end --all'
      );
      expect((outcome.left as { message: string }).message).toContain('just batch');
    }
  });
});

describe('turn --end --await', () => {
  test('ends the phase, blocks, then heads the next one', async () => {
    const kit = bench(wakeFetch(activeWake()), {
      actions: { [PHASE_END_ID]: phaseEndDescriptor(REVISION) },
    });
    const { result, lines } = await captured(() =>
      kit.run(commandTurnEnd(kit.ctx, { awaitNext: true, json: false, wait: { waitS: 0 } }))
    );
    expect(result._tag).toBe('Right');
    if (result._tag === 'Right') expect(result.right.exitCode).toBe(0);
    expect(kit.log.order).toEqual([`persist:${PHASE_END_ID}`, 'batch']);
    expect(lines).toHaveLength(2);
    expect(lines[0]?.startsWith('phase end → applied')).toBe(true);
    expect(lines[1]?.startsWith('T3 | ')).toBe(true);
    expect(lines[1]).toContain('YOUR TURN · t3/p1');
    expect(lines[1]).toContain('next: just turn');
  });

  /**
   * The invariant, stated as a test: the end applied, the wait timed out, and
   * the status is still the end's.
   */
  test('an applied phase end exits 0 even when the wait times out', async () => {
    const kit = bench(wakeFetch(timedOutWake()), {
      actions: { [PHASE_END_ID]: phaseEndDescriptor(REVISION) },
    });
    const { result, lines } = await captured(() =>
      kit.run(commandTurnEnd(kit.ctx, { awaitNext: true, json: false, wait: { waitS: 0 } }))
    );
    expect(result._tag).toBe('Right');
    if (result._tag === 'Right') expect(result.right.exitCode).toBe(0);
    expect(lines[0]?.startsWith('phase end → applied')).toBe(true);
    // The header says the wake was a timeout; the status does not.
    expect(lines[1]).toContain('woke timeout');
  });

  test('a wake into somebody else\'s phase is reported, not briefed, and still exits 0', async () => {
    const kit = bench(wakeFetch(timedOutWake()), {
      actions: { [PHASE_END_ID]: phaseEndDescriptor(REVISION) },
    });
    const { result, lines } = await captured(() =>
      kit.run(
        commandTurnEnd(kit.ctx, { awaitNext: true, brief: true, json: false, wait: { waitS: 0 } })
      )
    );
    expect(result._tag).toBe('Right');
    if (result._tag === 'Right') expect(result.right.exitCode).toBe(0);
    expect(lines).toContain(
      'not briefed: the phase is not yours yet, so there is nothing to order in it'
    );
    expect(lines).toContain('next: just wait --for-turn');
  });

  test('a receipt that did not apply is never awaited', async () => {
    const kit = bench(wakeFetch(activeWake()), {
      actions: { [PHASE_END_ID]: phaseEndDescriptor(REVISION) },
      overrides: { receiptOk: false },
    });
    const { result, lines } = await captured(() =>
      kit.run(commandTurnEnd(kit.ctx, { awaitNext: true, json: false, wait: { waitS: 0 } }))
    );
    expect(result._tag).toBe('Right');
    expect(lines).toContain('not awaited: the phase end was not accepted');
  });

  test('a wait that fails outright never hides the applied receipt', async () => {
    const kit = bench(
      fakeFetch(new Map([['/wait', { status: 500, body: { error: 'boom' } }]])),
      { actions: { [PHASE_END_ID]: phaseEndDescriptor(REVISION) } }
    );
    const { result, lines } = await captured(() =>
      kit.run(
        commandTurnEnd(kit.ctx, { awaitNext: true, brief: true, json: false, wait: { waitS: 0 } })
      )
    );
    expect(result._tag).toBe('Right');
    if (result._tag === 'Right') expect(result.right.exitCode).toBe(2);
    expect(lines.join('\n')).toContain('phase end');
    expect(lines).toContain('phase ended: the receipt above is authoritative');
    expect(lines.some((line) => line.startsWith('await failed: '))).toBe(true);
    expect(lines.some((line) => line.includes('do not') && line.includes('re-run'))).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// The argparse surface (client.py:11650-11672)
// ---------------------------------------------------------------------------

describe('turn --until is a choice', () => {
  /** The bench's seams, handed to the real command. */
  const seamsOf = (kit: Bench): TurnSeamsFor => () =>
    Effect.succeed({
      hooks: kit.ctx.hooks,
      waitHooks: kit.ctx.waitHooks,
      decisions: kit.ctx.decisions,
      renderers: kit.ctx.renderers,
      phaseEnd: kit.ctx.phaseEnd,
    });

  /** Did the *parser* refuse this argv, as argparse's `choices` did? */
  const parse = async (kit: Bench, argv: ReadonlyArray<string>): Promise<boolean> => {
    const log = console.log;
    const error = console.error;
    console.log = (): void => undefined;
    console.error = (): void => undefined;
    try {
      const exit = await Effect.runPromiseExit(
        Effect.provide(
          Command.run(turnCommandWith(seamsOf(kit)), { name: 'play', version: '0.1.0' })([
            'bun',
            'play',
            '--session',
            kit.sessionPath,
            ...argv,
          ]),
          Layer.mergeAll(kit.layer, BunContext.layer)
        )
      );
      if (Exit.isSuccess(exit)) return false;
      const failure = Option.getOrNull(Cause.failureOption(exit.cause));
      return failure !== null && ValidationError.isValidationError(failure);
    } finally {
      console.log = log;
      console.error = error;
    }
  };

  /**
   * `turn.add_argument("--until", choices=("phase", "revision"))` refuses a bad
   * value inside argparse, having touched nothing:
   *
   *     $ python3 play/client.py turn --end --await --until phse
   *     client.py turn: error: argument --until: invalid choice: 'phse'
   *     exit=2
   *
   * With `Options.text` the port instead reached `commandTurnEnd`, resolved
   * `phase.end`, **submitted the batch**, and only then failed inside the wait
   * engine — printing `phase ended: the receipt above is authoritative` and
   * exiting 2 on a run CPython never lets start.  The phase-end log is the
   * assertion that matters: a rejected flag must not end a phase.
   */
  test('a bad value is refused by the parser, so no phase is ended', async () => {
    const kit = bench(wakeFetch(activeWake()), {
      actions: { [PHASE_END_ID]: phaseEndDescriptor(REVISION) },
    });
    expect(await parse(kit, ['--end', '--await', '--until', 'phse'])).toBe(true);
    expect(kit.log.order).toEqual([]);
  });

  test('both declared values reach the handler and end the phase', async () => {
    for (const value of ['phase', 'revision']) {
      const kit = bench(wakeFetch(activeWake()), {
        actions: { [PHASE_END_ID]: phaseEndDescriptor(REVISION) },
      });
      expect(await parse(kit, ['--end', '--await', '--until', value, '--wait-s', '0'])).toBe(false);
      expect(kit.log.order).toContain('batch');
    }
    // …and the default, which argparse also spells `phase`.
    const kit = bench(wakeFetch(activeWake()), {
      actions: { [PHASE_END_ID]: phaseEndDescriptor(REVISION) },
    });
    expect(await parse(kit, ['--end', '--await', '--wait-s', '0'])).toBe(false);
    expect(kit.log.order).toContain('batch');
  });
});

describe('_await_and_brief_locked', () => {
  test('the prelude flushes before the wait begins, even on an instant wake', async () => {
    // "Execution is done" prints while the opponent is still thinking: the
    // receipts flush ahead of the first poll, not on the first unwoken tick
    // -- a wait that wakes immediately used to keep them buffered to the end.
    const kit = bench(wakeFetch(activeWake()));
    const captured: string[] = [];
    const original = console.log;
    console.log = (...parts: ReadonlyArray<unknown>) => captured.push(parts.join(' '));
    try {
      const prelude = ['u1 found_city London → applied rev7/t3'];
      const outcome = await kit.run(
        awaitAndBrief(kit.ctx, { brief: false, wait: { waitS: 0 }, prelude })
      );
      expect(outcome._tag).toBe('Right');
      expect(prelude).toHaveLength(0);
      expect(captured).toContain('u1 found_city London → applied rev7/t3');
    } finally {
      console.log = original;
    }
  });

  test('without --brief the header carries the follow-up command', async () => {
    const kit = bench(wakeFetch(activeWake()));
    const outcome = await kit.run(
      awaitAndBrief(kit.ctx, { brief: false, wait: { waitS: 0 } })
    );
    expect(outcome._tag).toBe('Right');
    if (outcome._tag !== 'Right') return;
    expect(outcome.right.lines).toHaveLength(1);
    expect(outcome.right.lines[0]).toContain('next: just turn');
    expect(outcome.right.briefing).toBeNull();
  });

  test('a briefing that cannot be built is one more line, not a swallowed wake', async () => {
    const kit = bench(
      fakeFetch(
        new Map([
          ['/wait', { body: activeWake() }],
          ['/health', { status: 500, body: { error: { code: 'internal_error', message: 'no' } } }],
        ])
      )
    );
    const outcome = await kit.run(awaitAndBrief(kit.ctx, { brief: true, wait: { waitS: 0 } }));
    expect(outcome._tag).toBe('Right');
    if (outcome._tag !== 'Right') return;
    expect(outcome.right.briefError).not.toBe('');
    expect(outcome.right.lines.some((line) => line.startsWith('briefing failed: '))).toBe(true);
    expect(outcome.right.lines[outcome.right.lines.length - 1]).toBe('next: just turn');
  });
});

describe('_composite_json', () => {
  test('--brief publishes the parts under their exact names', () => {
    const payload = turnEndJson(true, disposition('applied'), null, null, '');
    expect(Object.keys(payload)).toEqual([
      'schema_version',
      'command',
      'status',
      'end',
      'wait',
      'turn',
      'turn_error',
    ]);
    expect(payload['status']).toBe('briefed');
    expect(payload['turn_error']).toBeNull();
  });

  test('a briefing error is carried as a string, not dropped', () => {
    expect(turnEndJson(true, disposition('applied'), null, null, 'boom')['turn_error']).toBe('boom');
  });

  test('without --brief the payload is the plain "ended" shape', () => {
    const payload = turnEndJson(false, disposition('applied'), null, null, '');
    expect(Object.keys(payload)).toEqual([
      'schema_version',
      'command',
      'status',
      'disposition',
      'wait',
    ]);
    expect(payload['status']).toBe('ended');
  });

  test('--json prints one object and nothing else', async () => {
    const kit = bench(wakeFetch(activeWake()), {
      actions: { [PHASE_END_ID]: phaseEndDescriptor(REVISION) },
    });
    const { result, lines } = await captured(() =>
      kit.run(commandTurnEnd(kit.ctx, { awaitNext: true, json: true, wait: { waitS: 0 } }))
    );
    expect(result._tag).toBe('Right');
    const payload: unknown = JSON.parse(lines.join('\n'));
    expect((payload as JsonObject)['command']).toBe('turn');
    expect((payload as JsonObject)['status']).toBe('ended');
  });
});

describe('_turn_briefing_locked', () => {
  const ACTIVE_HEALTH = (): JsonObject =>
    healthPayload({
      phase: {
        state: 'awaiting_agent',
        turn: 3,
        phase: 0,
        active: true,
        timing: {
          mode: 'default',
          timeout_s: 180,
          deadline_started_at: 1000,
          deadline_at: 1180,
          elapsed_s: 1,
          remaining_s: 179,
        },
      },
    });

  const statePage = (
    section: string,
    revision: Revision,
    items: ReadonlyArray<JsonValue>,
    cursor: string | null = null
  ): JsonObject => ({
    schema_version: 2,
    control_protocol: 'full-control-v2',
    game_id: FIXTURE_GAME_ID,
    agent_id: FIXTURE_AGENT_ID,
    state_revision: { ...revision },
    page: {
      section,
      items,
      total_items: cursor === null ? items.length : items.length + 1,
      next_cursor: cursor,
      cursor_expires_at: cursor === null ? null : '2099-01-01T00:00:00Z',
    },
  });

  test('a revision that drifts mid-fetch restarts once, then returns one revision', async () => {
    const first: Revision = { turn: 3, revision: 7, state_token: 'token_3_7' };
    const drifted: Revision = { turn: 3, revision: 8, state_token: 'token_3_8' };
    const stable: Revision = { turn: 3, revision: 9, state_token: 'token_3_9' };
    const cursor = `cursor_${'c'.repeat(32)}`;
    const overviewItem: JsonValue = {
      turn: 3,
      player: { government: 'Despotism', economy: { gold: 25 } },
    };
    const kit = bench(
      fakeFetch([
        { body: ACTIVE_HEALTH() },
        { body: statePage('overview', first, [overviewItem]) },
        { body: statePage('cities', first, [{ id: `city_${'a'.repeat(32)}` }]) },
        { body: statePage('units', drifted, [{ id: `unit_${'b'.repeat(32)}` }]) },
        { body: statePage('research', drifted, [{ name: 'Alphabet' }]) },
        { body: ACTIVE_HEALTH() },
        { body: ACTIVE_HEALTH() },
        { body: statePage('overview', stable, [overviewItem]) },
        { body: statePage('cities', stable, [{ id: `city_${'a'.repeat(32)}` }], cursor) },
        { body: statePage('units', stable, [{ id: `unit_${'b'.repeat(32)}` }]) },
        { body: statePage('research', stable, [{ name: 'Alphabet' }]) },
        { body: ACTIVE_HEALTH() },
      ])
    );
    const outcome = await kit.run(turnBriefingLocked(kit.ctx));
    expect(outcome._tag).toBe('Right');
    if (outcome._tag !== 'Right') return;
    const result = outcome.right.result;
    expect(result.status).toBe('ready');
    if (result.status !== 'ready') return;
    expect(result.state_revision).toEqual(stable);
    expect(result.cities.next_cursor).toBe(cursor);
    expect(result.cities.cursor_expires_at).toBe('2099-01-01T00:00:00Z');
    expect(result.cities.shown).toBe(1);
    expect(result.cities.total).toBe(2);
    expect(result.cities.truncated).toBe(true);
    expect(result.next_commands).toContain(`just state --cursor ${cursor}`);
  });

  test('a second disagreement refuses, naming the command that retries it', async () => {
    const a: Revision = { turn: 3, revision: 7, state_token: 'token_3_7' };
    const b: Revision = { turn: 3, revision: 8, state_token: 'token_3_8' };
    const item: JsonValue = { turn: 3, player: null };
    const drifting = [
      { body: ACTIVE_HEALTH() },
      { body: statePage('overview', a, [item]) },
      { body: statePage('cities', a, []) },
      { body: statePage('units', b, []) },
      { body: statePage('research', b, []) },
      { body: ACTIVE_HEALTH() },
    ];
    const kit = bench(fakeFetch([...drifting, ...drifting]));
    const outcome = await kit.run(turnBriefingLocked(kit.ctx));
    expect(outcome._tag).toBe('Left');
    if (outcome._tag === 'Left') {
      expect((outcome.left as { message: string }).message).toBe(
        'the game changed twice while building the turn briefing; run `just turn` again'
      );
    }
  });

  test('a terminal game names `just result`, and never fetches a page', async () => {
    const kit = bench(
      fakeFetch([{ body: healthPayload({ game_state: 'completed', phase: null }) }])
    );
    const outcome = await kit.run(turnBriefingLocked(kit.ctx));
    expect(outcome._tag).toBe('Right');
    if (outcome._tag !== 'Right') return;
    expect(outcome.right.result.status).toBe('terminal');
    expect(outcome.right.result.next_commands).toEqual([`just result ${FIXTURE_GAME_ID}`]);
    expect(outcome.right.decisions).toEqual([]);
  });

  test('the lobby names the pregame reads; anything else names `just wait`', async () => {
    const lobby = bench(
      fakeFetch([{ body: healthPayload({ game_state: 'lobby', phase: null }) }])
    );
    const first = await lobby.run(turnBriefingLocked(lobby.ctx));
    expect(first._tag).toBe('Right');
    if (first._tag !== 'Right') return;
    expect(first.right.result.status).toBe('lobby');
    expect(first.right.result.next_commands[1]).toBe('just state --section pregame_nations');

    const notReady = bench(
      fakeFetch([{ body: healthPayload({ observation_available: false }) }])
    );
    const second = await notReady.run(turnBriefingLocked(notReady.ctx));
    expect(second._tag).toBe('Right');
    if (second._tag !== 'Right') return;
    expect(second.right.result.status).toBe('not_ready');
    expect(second.right.result.next_commands).toEqual(['just wait']);
  });
});

// ---------------------------------------------------------------------------
// The live seams (src/commands/turn.cmd.ts)
// ---------------------------------------------------------------------------

/**
 * `liveTurnSeams` against a real workspace, a real `SessionStore` and a fake
 * wire.
 *
 * Everything above this line drives `TurnCtx` through hand-built hooks, which
 * is what makes the projections testable — and is also exactly how a hook wired
 * to `Effect.void` stays green forever.  These four cases run the seams the
 * command actually builds, so the mirror files the briefing is *supposed* to
 * write are asserted on disk rather than assumed.
 */
interface BriefingPlan {
  readonly revision: Revision;
  readonly chat: number | null;
}

const BRIEFING_HEALTH = (): JsonObject =>
  healthPayload({
    phase: {
      state: 'awaiting_agent',
      turn: 3,
      phase: 0,
      active: true,
      timing: {
        mode: 'default',
        timeout_s: 180,
        deadline_started_at: 1000,
        deadline_at: 1180,
        elapsed_s: 1,
        remaining_s: 179,
      },
    },
  });

const UNIT_ONE = `unit_${'1'.repeat(32)}`;
const UNIT_TWO = `unit_${'2'.repeat(32)}`;
const UNIT_THREE = `unit_${'3'.repeat(32)}`;
const CITY_ONE = `city_${'a'.repeat(32)}`;
const TILE_ONE = `tile_${'7'.repeat(32)}`;

/** `focus_units` (test_client.py:7432-7463). */
const briefingUnits = (): ReadonlyArray<JsonValue> => [
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
  {
    id: UNIT_TWO,
    scope: 'own',
    type: 'Workers',
    tile_id: TILE_ONE,
    x: 31,
    y: 72,
    hp: 10,
    moves: 2,
    type_stats: { max_hp: 10, move_rate: 2 },
    activity: { name: 'idle' },
    automation: { controller: 'player', has_orders: false },
    route: null,
  },
  {
    // Walking somewhere, so it needs no decision this phase.
    id: UNIT_THREE,
    scope: 'own',
    type: 'Explorer',
    tile_id: TILE_ONE,
    x: 31,
    y: 72,
    hp: 10,
    moves: 3,
    type_stats: { max_hp: 10, move_rate: 3 },
    activity: { name: 'idle' },
    automation: { controller: 'player', has_orders: true },
    route: {
      mode: 'goto',
      order_count: 4,
      path_available: true,
      path_step_count: 4,
      destination: { tile_id: TILE_ONE, x: 40, y: 60 },
    },
  },
];

/** `focus_cities` (test_client.py:7466-7471) — no production, so it decides. */
const briefingCities = (): ReadonlyArray<JsonValue> => [
  {
    id: CITY_ONE,
    name: 'London',
    x: 31,
    y: 72,
    size: 1,
    production: null,
    surplus: { food: 2, shields: 1, trade: 0 },
  },
];

/** `briefing_overview` (test_client.py:8923-8936). */
const briefingOverview = (chat: number | null): JsonValue => ({
  turn: 3,
  player: { government: 'Despotism', economy: { gold: 25 } },
  research: { target: 'Bronze Working', bulbs_researched: 0, cost: 28 },
  counts:
    chat === null
      ? { cities: 1, units: 3, legal_actions: 0 }
      : { cities: 1, units: 3, legal_actions: 0, chat },
});

const sectionPayload = (
  section: string,
  revision: Revision,
  items: ReadonlyArray<JsonValue>
): JsonObject => ({
  schema_version: 2,
  control_protocol: 'full-control-v2',
  game_id: FIXTURE_GAME_ID,
  agent_id: FIXTURE_AGENT_ID,
  state_revision: { ...revision },
  page: {
    section,
    items: [...items],
    total_items: items.length,
    next_cursor: null,
    cursor_expires_at: null,
  },
});

const urlOf = (input: Parameters<typeof fetch>[0]): string =>
  typeof input === 'string' ? input : input instanceof URL ? input.href : input.url;

/**
 * `briefing_responder` (test_client.py:8938-8966).
 *
 * One plan per briefing; the `overview` fetch is the first of the four, so it
 * is what advances the plan.  `--decisions`' drain never asks for `overview`,
 * so it keeps reading the plan the last briefing established.
 */
const briefingFetch = (plans: ReadonlyArray<BriefingPlan>): typeof fetch => {
  const cursor = { index: -1 };
  return (async (input: Parameters<typeof fetch>[0]): Promise<Response> => {
    const url = urlOf(input);
    if (url.includes('/health')) return jsonResponse(BRIEFING_HEALTH());
    const section = /section=(\w+)/.exec(url)?.[1] ?? '';
    if (section === 'overview') cursor.index += 1;
    const plan = plans[Math.max(0, Math.min(cursor.index, plans.length - 1))];
    if (plan === undefined) return jsonResponse({ error: 'no plan' }, 500);
    const items: Readonly<Record<string, ReadonlyArray<JsonValue>>> = {
      overview: [briefingOverview(plan.chat)],
      units: briefingUnits(),
      cities: briefingCities(),
      research: [],
    };
    const page = items[section];
    if (page === undefined) return jsonResponse({ error: `no section ${section}` }, 500);
    return jsonResponse(sectionPayload(section, plan.revision, page));
  }) as typeof fetch;
};

interface LiveBench {
  readonly ctx: TurnCtx;
  readonly sessionPath: string;
  readonly scratch: Scratch;
  readonly run: <A, E>(
    effect: Effect.Effect<A, E, V2Client | SessionStore | PrivateFs>
  ) => Promise<import('effect/Either').Either<A, E>>;
  readonly read: (parts: ReadonlyArray<string>) => string | null;
}

const liveBench = (fetchImpl: typeof fetch): LiveBench => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const sessionPath = path.join(scratch.workspace.stateRoot, 'session-live.json');
  Effect.runSync(scratch.files.writeJson(sessionPath, sessionFile()));
  const store = sessionStoreFor(scratch.workspace, scratch.files, stateSchema, {});
  const loaded = Effect.runSync(store.resolveV2(sessionPath));
  const layer = Layer.mergeAll(
    Layer.succeed(V2Client, v2ClientFor(httpFor(fetchImpl), () => Effect.void)),
    Layer.succeed(SessionStore, store),
    scratch.layer
  );
  const seams = Effect.runSync(
    Effect.provide(liveTurnSeams(sessionPath, loaded.session), layer)
  );
  return {
    ctx: turnCtx(sessionPath, loaded.session, seams),
    sessionPath,
    scratch,
    run: (effect) => Effect.runPromise(Effect.either(Effect.provide(effect, layer))),
    read: (parts) =>
      Effect.runSync(Effect.provide(mirrorText(sessionPath, parts), scratch.layer)),
  };
};

describe('the live seams a bare `turn` is built from', () => {
  const REV = (revision: number): Revision => ({
    turn: 3,
    revision,
    state_token: `token_3_${revision}`,
  });

  test('the four pages reach the mirror, so the units and cities tables exist', async () => {
    const kit = liveBench(briefingFetch([{ revision: REV(7), chat: 5 }]));
    const outcome = await kit.run(turnBriefingLocked(kit.ctx));
    expect(outcome._tag).toBe('Right');
    const units = kit.read(['state', 'units.tsv']);
    const cities = kit.read(['state', 'cities.tsv']);
    expect(units).not.toBeNull();
    expect(cities).not.toBeNull();
    // The alias column is the one `just legal --actor_id u1` accepts, which is
    // only true because the mirror was written from the state `rememberPage`
    // had already learned.
    expect(units ?? '').toContain('u1');
    expect(units ?? '').toContain('Settlers');
    expect(cities ?? '').toContain('London');
    // …and `--decisions` now finds them fresh, so it re-fetches nothing.
    const fresh = Effect.runSync(
      Effect.provide(
        Effect.all([
          mirrorIsFresh(kit.sessionPath, mirrorFile('units'), REV(7)),
          mirrorIsFresh(kit.sessionPath, mirrorFile('cities'), REV(7)),
        ]),
        kit.scratch.layer
      )
    );
    expect(fresh).toEqual([true, true]);
  });

  /** `test_v2_briefing_counts_new_events_and_names_the_feed`. */
  test('the events line counts the delta, because `count_chat` was written', async () => {
    const kit = liveBench(
      briefingFetch([
        { revision: REV(7), chat: 5 },
        { revision: REV(9), chat: 7 },
        { revision: REV(11), chat: 7 },
      ])
    );
    const first = await kit.run(turnBriefingLocked(kit.ctx));
    expect(first._tag).toBe('Right');
    if (first._tag !== 'Right') return;
    expect(first.right.events).toBe('events: 5 new — just state --section chat');
    expect(kit.read(['state', 'overview.tsv']) ?? '').toContain('count_chat');

    // The next briefing counts only what arrived since this one.
    const second = await kit.run(turnBriefingLocked(kit.ctx));
    expect(second._tag).toBe('Right');
    if (second._tag !== 'Right') return;
    expect(second.right.events).toBe('events: 2 new — just state --section chat');

    // An unchanged feed is not news and costs no line.
    const third = await kit.run(turnBriefingLocked(kit.ctx));
    expect(third._tag).toBe('Right');
    if (third._tag !== 'Right') return;
    expect(third.right.events).toBe('');
  });

  test('the decision block reads the tables the same briefing just wrote', async () => {
    const kit = liveBench(briefingFetch([{ revision: REV(7), chat: null }]));
    const outcome = await kit.run(turnBriefingLocked(kit.ctx));
    expect(outcome._tag).toBe('Right');
    if (outcome._tag !== 'Right') return;
    const decisions = outcome.right.decisions;
    // Two idle units and one city with no production; the third unit is
    // walking, so it needs no decision.
    expect(decisions.length).toBe(3);
    expect(decisions[0]?.startsWith('u1 ')).toBe(true);
    expect(decisions[1]?.startsWith('u2 ')).toBe(true);
    expect(decisions[2]?.startsWith('c1 ')).toBe(true);
    expect(decisions.some((line) => line.includes('u3'))).toBe(false);
  });

  test('every wait tick writes the protocol card, not the five-line default', async () => {
    const kit = liveBench(
      fakeFetch(new Map([['/wait', { body: activeWake() }]]))
    );
    const outcome = await kit.run(awaitAndBrief(kit.ctx, { brief: false, wait: { waitS: 0 } }));
    expect(outcome._tag).toBe('Right');
    const header = kit.read(HEADER_FILE) ?? '';
    // `_mirror_health` always passes `commands=V2_PROTOCOL_CARD`; the default
    // card `update_from_health` falls back to has none of this.
    expect(header).toContain('ONE CALL PER TURN');
    expect(header).toContain('just turn --end --await --brief');
    expect(header).not.toContain(DEFAULT_COMMAND_CARD[0] ?? 'just turn  turn briefing');
  });
});

describe('the refusal that must never be swallowed', () => {
  test('--json re-raises the wait failure rather than printing half a payload', async () => {
    const kit = bench(
      fakeFetch(new Map([['/wait', { status: 500, body: { error: 'boom' } }]])),
      { actions: { [PHASE_END_ID]: phaseEndDescriptor(REVISION) } }
    );
    const outcome = await kit.run(
      commandTurnEnd(kit.ctx, { awaitNext: true, json: true, wait: { waitS: 0 } })
    );
    expect(outcome._tag).toBe('Left');
    expect(playerError('x')._tag).toBe('PlayerError');
  });
});
