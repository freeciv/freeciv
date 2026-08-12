/**
 * The wait engine.
 *
 * Ports `play/client.py`:
 *
 * - 6744-6770 `_wait_args`
 * - 9916-9936 `_local_wait_response`
 * - 9938-9990 `_legacy_wait_value`
 * - 9992-10007 `V2_WAIT_S_MAX` / `V2_WAIT_TICK_S` / `V2_FOR_TURN_GRACE_S`
 * - 10009-10079 `_wait_value`
 * - 10081-10088 `_phase_is_mine`
 * - 10090-10098 `_wait_exit_code`
 * - 10100-10109 `_holder_remaining_s`
 * - 10111-10218 `_wait_until_turn`
 * - 10240-10256 `_wait_command_value`
 * - 10698-10705 `_seat_rebound`
 *
 * The exit-code contract is the point of the whole unit: a wake is only worth
 * anything to a job supervisor if the status says which wake it was.  Every
 * outcome used to exit 0, so a harness whose monitor escalates on non-zero —
 * the correct configuration — was never told its turn had started.
 *
 * Rendering lives in `src/render/wait.ts`; nothing here formats a line.  The
 * per-tick transcript line is emitted through the `echo` callback, which takes
 * the *health envelope* rather than the finished string precisely so this file
 * never reaches into a renderer (and so it never has to import U06).
 */
import { Effect } from 'effect';
import * as fs from 'node:fs';
import * as path from 'node:path';
import { FULL_CONTROL_V2, TERMINAL_STATES, V2_SATISFIED_WAKE_REASONS } from 'src/constants';
import { playerError, type PlayError } from 'src/errors';
import { V2_WAIT_EXIT_ACTIVE, V2_WAIT_EXIT_RETRY, V2_WAIT_EXIT_TERMINAL } from 'src/exit';
import { formatG } from 'src/render/primitives';
import { decodeHealth, type HealthEnvelope, type PhaseBlock, type WaitingOnSeat } from 'src/schema/health';
import { decodePage, type PageEnvelope } from 'src/schema/page';
import type { Revision } from 'src/schema/revision';
import { decodeWait, type WaitEnvelope, type WaitUntil, type WakeReason } from 'src/schema/wait';
import { V2Client } from 'src/services/v2-client';
import { SessionStore, credentialsOf, type Session } from 'src/services/session-store';

// ---------------------------------------------------------------------------
// client.py:9992-10007 — the three bounds
// ---------------------------------------------------------------------------

/**
 * The longest single blocking wait, on both sides of the wire.
 *
 * It used to be 300 s against a 600 s agent phase, which made "one call per
 * turn" arithmetic impossible in multiplayer: the maximum wait was half the
 * maximum opponent phase, so every harness had to loop and one of them looped
 * wrong for nine turns.  615 covers a full 600 s phase plus the boundary either
 * side of it.  The supervisor is the authority; this is the client's copy.
 */
export const V2_WAIT_S_MAX = 615.0;

/**
 * How long one internal long-poll of a `--for-turn` wait blocks.  Short enough
 * that the marker file and the transcript stay fresh, long enough that a whole
 * opponent phase costs tens of requests rather than hundreds.
 */
export const V2_WAIT_TICK_S = 15.0;

/**
 * Slack added to the holder's own deadline before a `--for-turn` wait gives up:
 * the supervisor still has to notice the deadline and end the phase.
 */
export const V2_FOR_TURN_GRACE_S = 15.0;

// ---------------------------------------------------------------------------
// _wait_args (client.py:6744-6770)
// ---------------------------------------------------------------------------

/** The three blocking knobs, normalised, whichever command carried them. */
export interface WaitArgs {
  readonly waitS: number;
  readonly pollS: number;
  /** Not narrowed to `WaitUntil` here: `_wait_value` owns that refusal. */
  readonly until: string;
}

/** Whatever the caller's flag surface produced, before defaults are applied. */
export interface RawWaitArgs {
  readonly waitS?: number | undefined;
  readonly pollS?: number | undefined;
  readonly until?: string | undefined;
}

/**
 * Normalise the three blocking knobs, whichever command carried them.
 *
 * A knob the caller did give is passed through untouched — `--wait-s 0` is a
 * legal non-blocking poll, so only a missing value takes the default.
 * `override` replaces it for one internal poll of a deadline-bounded wait.
 */
export const waitArgs = (raw: RawWaitArgs, override?: number): WaitArgs => ({
  waitS: override === undefined ? (raw.waitS === undefined ? 120 : raw.waitS) : override,
  pollS: raw.pollS === undefined ? 1 : raw.pollS,
  until: raw.until === undefined ? 'phase' : raw.until,
});

// ---------------------------------------------------------------------------
// The cross-unit seams
// ---------------------------------------------------------------------------

/** `_holder_seat` (client.py:5350-5367), owned by U06 `src/render/health.ts`. */
export type HolderSeatFn = (phase: PhaseBlock | null) => WaitingOnSeat | null;

/**
 * What the wait engine needs from units it does not own.
 *
 * `_legacy_wait_value` remembers and mirrors the overview page it fetched
 * (U03 `_remember_page`, U04 `_mirror_page`), `_wait_until_turn` refreshes
 * `state/phase.json` on every tick (U04 `_mirror_health`), and both the tick
 * bound and the tick line need U06's `_holder_seat`.  They arrive as functions
 * so this unit stays wave-1 and testable on its own.
 */
export interface WaitHooks {
  readonly rememberPage: (page: PageEnvelope) => Effect.Effect<void, PlayError>;
  readonly mirrorPage: (page: PageEnvelope, source: string) => Effect.Effect<void, PlayError>;
  readonly mirrorHealth: (health: HealthEnvelope, source: string) => Effect.Effect<void, PlayError>;
  readonly holderSeat: HolderSeatFn;
}

/** Wall-clock, injectable — the thing a deadline-bounded wait is measured in. */
export interface WaitClock {
  readonly monotonic: () => Effect.Effect<number>;
  readonly sleep: (seconds: number) => Effect.Effect<void>;
}

export const systemWaitClock: WaitClock = {
  monotonic: () => Effect.sync(() => performance.now() / 1000),
  sleep: (seconds) => Effect.sleep(`${Math.max(0, Math.round(seconds * 1000))} millis`),
};

/** Everything one `wait` acts against. */
export interface WaitCtx {
  readonly sessionPath: string;
  readonly session: Session;
  readonly hooks: WaitHooks;
  readonly clock: WaitClock;
}

export interface WaitCtxInput {
  readonly sessionPath: string;
  readonly session: Session;
  readonly hooks: WaitHooks;
  readonly clock?: WaitClock | undefined;
}

export const waitCtx = (input: WaitCtxInput): WaitCtx => ({
  sessionPath: input.sessionPath,
  session: input.session,
  hooks: input.hooks,
  clock: input.clock ?? systemWaitClock,
});

// ---------------------------------------------------------------------------
// _local_wait_response (client.py:9916-9936)
// ---------------------------------------------------------------------------

export const localWaitResponse = (
  session: Session,
  wakeReason: WakeReason,
  health: HealthEnvelope,
  revision: Revision | null
): WaitEnvelope => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  game_id: session.gameId,
  agent_id: session.agentId,
  wake_reason: wakeReason,
  health,
  state_revision: revision,
});

// ---------------------------------------------------------------------------
// _phase_is_mine (client.py:10081-10088)
// ---------------------------------------------------------------------------

/** Whether this seat may act right now, by the same test the server uses. */
export const phaseIsMine = (health: HealthEnvelope): boolean => {
  const phase = health.phase;
  return (
    phase !== null &&
    phase.active === true &&
    phase.state === 'awaiting_agent' &&
    health.observation_available === true
  );
};

// ---------------------------------------------------------------------------
// _wait_exit_code (client.py:10090-10098)
// ---------------------------------------------------------------------------

export type WaitExitCode =
  | typeof V2_WAIT_EXIT_ACTIVE
  | typeof V2_WAIT_EXIT_RETRY
  | typeof V2_WAIT_EXIT_TERMINAL;

/**
 * Map one wake onto the exit status a job supervisor can act on.
 *
 * `game_terminal` outranks everything: there is nothing left to wait for even
 * if the seat's last observed phase looked actionable.  Then an actually-mine
 * phase, then the satisfied wake reasons, then "come back".
 */
export const waitExitCode = (wait: WaitEnvelope): WaitExitCode => {
  if (wait.wake_reason === 'game_terminal') return V2_WAIT_EXIT_TERMINAL;
  if (phaseIsMine(wait.health)) return V2_WAIT_EXIT_ACTIVE;
  if (V2_SATISFIED_WAKE_REASONS.has(wait.wake_reason)) return V2_WAIT_EXIT_ACTIVE;
  return V2_WAIT_EXIT_RETRY;
};

// ---------------------------------------------------------------------------
// _holder_remaining_s (client.py:10100-10109)
// ---------------------------------------------------------------------------

/** How long the seat that holds this phase has left, when one does. */
export const holderRemainingS = (
  health: HealthEnvelope,
  holderSeat: HolderSeatFn
): number | null => {
  const phase = health.phase;
  if (phase === null || holderSeat(phase) === null) return null;
  const remaining = phase.timing.remaining_s;
  if (typeof remaining !== 'number' || !Number.isFinite(remaining)) return null;
  return Math.max(0, remaining);
};

// ---------------------------------------------------------------------------
// _legacy_wait_value (client.py:9938-9990)
// ---------------------------------------------------------------------------

/** The Python's `not 200 <= status < 300`, spelled once. */
const notOk = (status: number): boolean => status < 200 || status >= 300;

/** Correct local fallback for supervisors predating the private `/wait` route. */
export const legacyWaitValue = (
  ctx: WaitCtx,
  args: WaitArgs,
  options: { readonly until: WaitUntil; readonly baseline: Revision | null }
): Effect.Effect<WaitEnvelope, PlayError, V2Client> =>
  Effect.gen(function* () {
    const client = yield* V2Client;
    const credentials = credentialsOf(ctx.session);
    const deadline = (yield* ctx.clock.monotonic()) + args.waitS;
    const healthUrl = yield* client.url(credentials, '/health');
    const stateUrl = `${yield* client.url(credentials, '/state')}?section=overview&limit=16`;

    // A `while (true)` with an explicit return is the honest shape of the
    // Python loop; `Effect.loop` would need the whole body as a fold.
    for (;;) {
      const healthResponse = yield* client.response('GET', healthUrl, credentials, {
        timeout: 10,
      });
      if (notOk(healthResponse.status)) return yield* client.raiseValidated(healthResponse);
      const health = yield* decodeHealth(healthResponse.value, ctx.session);
      if (TERMINAL_STATES.has(health.game_state)) {
        return localWaitResponse(ctx.session, 'game_terminal', health, null);
      }
      const phase = health.phase;
      if (
        options.until === 'phase' &&
        phase !== null &&
        phase.active === true &&
        phase.state === 'awaiting_agent' &&
        health.observation_available === true
      ) {
        return localWaitResponse(ctx.session, 'phase_active', health, null);
      }

      let revision: Revision | null = null;
      if (options.until === 'revision' && health.observation_available === true) {
        const stateResponse = yield* client.response('GET', stateUrl, credentials, {
          timeout: 10,
        });
        if (notOk(stateResponse.status)) return yield* client.raiseValidated(stateResponse);
        const overview = yield* decodePage(stateResponse.value, ctx.session);
        revision = overview.state_revision;
        yield* ctx.hooks.rememberPage(overview);
        yield* ctx.hooks.mirrorPage(overview, 'wait');
        const baseline = options.baseline;
        if (baseline === null) {
          // `_legacy_wait_value` asserts this; the assertion is a real
          // invariant (`_wait_value` refuses `--until revision` without a
          // baseline), so a value error here is a client bug, not a refusal.
          return yield* Effect.fail(
            playerError('wait --until revision requires a previously validated state page')
          );
        }
        if (revision.state_token !== baseline.state_token) {
          return localWaitResponse(ctx.session, 'revision_changed', health, revision);
        }
      }

      const remaining = deadline - (yield* ctx.clock.monotonic());
      if (remaining <= 0) {
        return localWaitResponse(ctx.session, 'timeout', health, revision);
      }
      yield* ctx.clock.sleep(Math.min(args.pollS, remaining));
    }
  });

// ---------------------------------------------------------------------------
// _wait_value (client.py:10009-10079)
// ---------------------------------------------------------------------------

const isWaitUntil = (value: string): value is WaitUntil =>
  value === 'phase' || value === 'revision';

/**
 * Detect the "this supervisor has no private `/wait`" 404.
 *
 * `{"error": "..."}` with a *string* body and no other key is the pre-v2
 * router's own not-found shape; a structured v2 refusal is an object and must
 * be raised, not silently downgraded to polling.
 */
const isMissingRouteRefusal = (status: number, value: Readonly<Record<string, unknown>>): boolean => {
  if (status !== 404) return false;
  const names = Object.keys(value);
  return names.length === 1 && names[0] === 'error' && typeof value['error'] === 'string';
};

export interface WaitValueOptions {
  /**
   * Skip the `.v2-state` read entirely.  Legal only in phase mode, where the
   * baseline is never sent and never checked — the server answers from this
   * seat's own phase and an opponent's revision cannot wake it.  The monitor
   * uses it so that a process which may be alive for the whole game never takes
   * the state lock a real command needs.
   */
  readonly stateless?: boolean | undefined;
}

/**
 * Block until the seat is wanted again, returning the validated wake.
 *
 * This is the whole of `just wait` minus its printing, so `turn --end --await`
 * blocks on exactly the same contract the standalone command does.
 */
export const waitValue = (
  ctx: WaitCtx,
  args: WaitArgs,
  options: WaitValueOptions = {}
): Effect.Effect<WaitEnvelope, PlayError, V2Client | SessionStore> =>
  Effect.gen(function* () {
    const stateless = options.stateless === true;
    if (!(args.waitS >= 0 && args.waitS <= V2_WAIT_S_MAX)) {
      return yield* Effect.fail(
        playerError(`wait-s must be in [0, ${formatG(V2_WAIT_S_MAX)}]`)
      );
    }
    if (!(args.pollS >= 0.05 && args.pollS <= 30)) {
      return yield* Effect.fail(playerError('poll-s must be in [0.05, 30]'));
    }
    const until = args.until;
    if (stateless && until !== 'phase') {
      return yield* Effect.fail(playerError('a stateless wait is phase-mode only'));
    }
    const store = yield* SessionStore;
    const baseline = stateless
      ? null
      : (yield* store.readState(ctx.sessionPath, ctx.session)).last_revision;
    if (!isWaitUntil(until)) {
      return yield* Effect.fail(playerError('wait --until must be phase or revision'));
    }
    if (until === 'revision' && baseline === null) {
      return yield* Effect.fail(
        playerError('wait --until revision requires a previously validated state page')
      );
    }

    const client = yield* V2Client;
    const credentials = credentialsOf(ctx.session);
    const parameters = new URLSearchParams();
    parameters.append('wait_s', formatG(args.waitS));
    parameters.append('until', until);
    if (until === 'revision' && baseline !== null) {
      parameters.append('after_state_token', baseline.state_token);
    }
    const url = `${yield* client.url(credentials, '/wait')}?${parameters.toString()}`;
    const response = yield* client.response('GET', url, credentials, {
      timeout: Math.max(10, args.waitS + 10),
    });
    if (isMissingRouteRefusal(response.status, response.value)) {
      return yield* legacyWaitValue(ctx, args, { until, baseline });
    }
    if (notOk(response.status)) return yield* client.raiseValidated(response);
    return yield* decodeWait(response.value, ctx.session, {
      until,
      afterStateToken: baseline === null ? null : baseline.state_token,
    });
  });

// ---------------------------------------------------------------------------
// _wait_until_turn (client.py:10111-10218)
// ---------------------------------------------------------------------------

export interface WaitUntilTurnOptions {
  /** The caller's request to wait out a holder that has not appeared yet. */
  readonly forTurn: boolean;
  /** `--max`: the caller's hard ceiling, never exceeded. */
  readonly capS?: number | null | undefined;
  /**
   * One line per internal poll, so a long block is never a silent gap.
   *
   * It takes the health envelope rather than the rendered string: the engine
   * must not depend on a renderer, and `src/render/wait.ts` turns this into
   * `_waiting_tick_line`'s exact text.
   */
  readonly echo?: ((health: HealthEnvelope) => Effect.Effect<void>) | undefined;
  readonly stateless?: boolean | undefined;
  /** Overrides `hooks.mirrorHealth` — the monitor writes its own marker. */
  readonly mirror?: ((health: HealthEnvelope) => Effect.Effect<void, PlayError>) | undefined;
}

/**
 * Block until the phase is genuinely this seat's, or its holder's time is up.
 *
 * The bound is the game's, not a constant: whoever holds the phase has a
 * deadline the server already computes and publishes, so a wait that covers it
 * is exactly one call per opponent turn.  Inside that bound this loops short
 * server long-polls, which keeps it resumable, keeps `state/phase.json` fresh
 * while it blocks, and gives the transcript a line per tick instead of an
 * unexplained ten-minute silence.
 *
 * Three bounds, in order of authority.  `capS` (`--max`) is the caller's hard
 * ceiling.  Under it, once the server names a seat that holds the phase, the
 * bound becomes that seat's own remaining deadline plus a grace — granted once,
 * while the deadline is still in the future, so a holder pinned at zero cannot
 * roll it forward for ever.  With no holder named at all, the bound is the
 * caller's `--wait-s`.
 */
export const waitUntilTurn = (
  ctx: WaitCtx,
  args: WaitArgs,
  options: WaitUntilTurnOptions
): Effect.Effect<WaitEnvelope, PlayError, V2Client | SessionStore> =>
  Effect.gen(function* () {
    const capS = options.capS ?? null;
    const forTurn = options.forTurn;
    const started = yield* ctx.clock.monotonic();
    let budget = capS === null ? args.waitS : Math.min(args.waitS, capS);
    let wait: WaitEnvelope | null = null;
    let first = true;

    for (;;) {
      const elapsed = (yield* ctx.clock.monotonic()) - started;
      const remaining = budget - elapsed;
      if (wait !== null && remaining <= 0) return wait;
      // An interactive composite (options.echo set) keeps every poll short so
      // its "… waiting on seat N" lines actually tick while the opponent
      // thinks; a silent wait spends the whole budget on one long poll.  The
      // first long poll used to swallow the entire wait either way, so a
      // 49-second `do --end --await --brief` printed nothing until the wake.
      const tick = Math.min(
        Math.max(remaining, 0),
        first && !forTurn && options.echo === undefined ? args.waitS : V2_WAIT_TICK_S
      );
      wait = yield* waitValue(ctx, waitArgs(args, tick), {
        stateless: options.stateless === true,
      });
      // P3: the marker is written on every tick, not only on the wake, so a
      // watcher reading `state/phase.json` sees a live file rather than one
      // frozen for the whole of somebody else's ten-minute phase.
      yield* options.mirror === undefined
        ? ctx.hooks.mirrorHealth(wait.health, 'wait')
        : options.mirror(wait.health);
      first = false;
      if (wait.wake_reason !== 'timeout' || phaseIsMine(wait.health)) return wait;

      const hold = holderRemainingS(wait.health, ctx.hooks.holderSeat);
      if (hold === null) {
        if (!forTurn) return wait;
      } else if (hold > 0) {
        budget = (yield* ctx.clock.monotonic()) - started + hold + V2_FOR_TURN_GRACE_S;
        if (capS !== null) budget = Math.min(budget, capS);
      }
      if (budget - ((yield* ctx.clock.monotonic()) - started) <= 0) return wait;
      if (options.echo !== undefined) yield* options.echo(wait.health);
    }
  });

// ---------------------------------------------------------------------------
// _wait_command_value (client.py:10240-10256)
// ---------------------------------------------------------------------------

export interface WaitCommandOptions {
  readonly forTurn?: boolean | undefined;
  readonly maxS?: number | null | undefined;
  readonly echo?: ((health: HealthEnvelope) => Effect.Effect<void>) | undefined;
}

/** Run one `wait` the way its flags asked for it, and return the wake. */
export const waitCommandValue = (
  ctx: WaitCtx,
  args: WaitArgs,
  options: WaitCommandOptions = {}
): Effect.Effect<WaitEnvelope, PlayError, V2Client | SessionStore> =>
  Effect.gen(function* () {
    const forTurn = options.forTurn === true;
    const maximum = options.maxS ?? null;
    if (maximum !== null && !forTurn) {
      return yield* Effect.fail(
        playerError('wait --max bounds --for-turn; pass both or neither')
      );
    }
    if (!forTurn) return yield* waitValue(ctx, args);
    if (!(maximum === null || (maximum >= 0 && maximum <= V2_WAIT_S_MAX))) {
      return yield* Effect.fail(playerError(`max must be in [0, ${formatG(V2_WAIT_S_MAX)}]`));
    }
    return yield* waitUntilTurn(ctx, waitArgs(args), {
      forTurn: true,
      capS: maximum,
      ...(options.echo === undefined ? {} : { echo: options.echo }),
    });
  });

// ---------------------------------------------------------------------------
// _seat_rebound (client.py:10698-10705)
// ---------------------------------------------------------------------------

const realPath = (target: string): string => {
  try {
    return fs.realpathSync.native(target);
  } catch {
    return path.resolve(target);
  }
};

/** Whether `just use` has pointed this workspace at a different seat. */
export const seatRebound = (
  sessionPath: string
): Effect.Effect<boolean, never, SessionStore> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const current = yield* Effect.either(store.resolve(''));
    if (current._tag === 'Left') return false;
    return realPath(current.right.path) !== realPath(sessionPath);
  });
