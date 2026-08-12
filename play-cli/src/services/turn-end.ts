/**
 * Ending a phase, blocking on the next one, and briefing into it.
 *
 * Ports `play/client.py`:
 *
 * - 6667-6675 `_cached_kind_action`
 * - 6677-6696 `_resolve_kind_action`
 * - 6771-6799 `_phase_end_locked`
 * - 6802-6866 `_await_and_brief_locked`
 * - 6868-6937 `_command_turn_end`
 * - 7599-7706 `_turn_briefing_locked`
 *
 * One invariant governs the whole file: **an applied phase end never exits
 * non-zero because of how the wait after it turned out.**  A validation error
 * in the wake once hid three consecutive applied phase-ends from a live agent,
 * which is how a seat came to re-end a phase it had already ended.  The receipt
 * is authoritative and is printed first; everything after it is commentary.
 *
 * `phaseEnd` and `awaitAndBrief` are exported because `do --end/--await/--brief`
 * (U16) must print the identical bytes — one implementation, two commands.
 */
import { Console, Effect } from 'effect';
import { PHASE_END_REMEDY, TERMINAL_STATES, V2_TURN_SECTIONS } from 'src/constants';
import { playerError, type PlayError, type PlayerError } from 'src/errors';
import { V2_PROTOCOL_CARD } from 'src/render/join';
import { echo, render, type AliasMap, type JsonObject, type JsonValue } from 'src/render/primitives';
import {
  renderTurn,
  type RenderTurnDeps,
  type TurnCompactPage,
  type TurnResult,
} from 'src/render/turn';
import { awaitLine, waitingTickLine } from 'src/render/wait';
import type { BatchDisposition } from 'src/schema/batch';
import type { HealthEnvelope } from 'src/schema/health';
import type { PageEnvelope } from 'src/schema/page';
import { revisionsEqual } from 'src/schema/revision';
import type { WaitEnvelope } from 'src/schema/wait';
import { aliasMap } from 'src/services/aliases';
import { compositeJson } from 'src/services/composite-json';
import { briefingDecisionLines, type DecisionDeps } from 'src/services/decisions';
import type { CompactAction } from 'src/services/legal-compact';
import { compactText } from 'src/services/orders';
import { turnHealthContext, turnHealthEpoch, turnHealthEpochsEqual } from 'src/services/health-context';
import { pyDumps } from 'src/services/canonical-body';
import { healthFloatPathsUnder, pyValueWithFloats } from 'src/services/health-json';
import { mirrorHealth } from 'src/services/mirror';
import { PrivateFs } from 'src/services/private-fs';
import { SessionStore, type Session, type V2ClientState } from 'src/services/session-store';
import {
  briefingEventsLine,
  mirrorEventCount,
  turnCompactPage,
  turnHealth,
  turnNextCommands,
  turnPage,
  type TurnHooks,
} from 'src/services/turn-pages';
import { V2Client } from 'src/services/v2-client';
import {
  waitArgs,
  waitUntilTurn,
  phaseIsMine,
  waitCtx,
  type RawWaitArgs,
  type WaitHooks,
} from 'src/services/wait';

// ---------------------------------------------------------------------------
// The cross-unit seams
// ---------------------------------------------------------------------------

/** What `_submit_persisted_batch` (U13) hands back. */
export interface SubmitOutcome {
  readonly disposition: BatchDisposition;
  /** A line for stderr, or `""`. */
  readonly warning: string;
  readonly exitCode: number;
}

/**
 * Everything ending a phase needs from units this one does not own.
 *
 * `drainLegal` is U11's `_drain_legal_unlocked`, `defaultArguments` is U15's,
 * `persistBatchForAction`/`submitPersistedBatch` are U13's and
 * `renderDisposition`/`orderReceiptOk` are U14's.  All wave-2, all written in
 * parallel, so they arrive as functions rather than imports.
 */
export interface PhaseEndDeps {
  /** `_drain_legal_unlocked` — enumerate the whole catalog, printing nothing. */
  readonly drainLegal: Effect.Effect<void, PlayError, PrivateFs | SessionStore | V2Client>;
  /** `_default_arguments` — the arguments an action fixes for itself, or `null`. */
  readonly defaultArguments: (compact: CompactAction) => JsonObject | null;
  /** `_persist_batch_for_action` — write the batch before the POST; yields its ID. */
  readonly persistBatchForAction: (
    actionId: string,
    args: JsonObject
  ) => Effect.Effect<string, PlayError, PrivateFs | SessionStore | V2Client>;
  /** `_submit_persisted_batch` — POST it and derive the disposition. */
  readonly submitPersistedBatch: (
    batchId: string
  ) => Effect.Effect<SubmitOutcome, PlayError, PrivateFs | SessionStore | V2Client>;
  /** `_render_disposition` (U14). */
  readonly renderDisposition: (
    disposition: BatchDisposition,
    intent: string
  ) => ReadonlyArray<string>;
  /** `_order_receipt_ok` (U14) — whether the end actually applied. */
  readonly orderReceiptOk: (disposition: BatchDisposition) => boolean;
}

/** Everything one `turn`/`do --end` acts against. */
export interface TurnCtx {
  readonly sessionPath: string;
  readonly session: Session;
  readonly hooks: TurnHooks;
  readonly waitHooks: WaitHooks;
  readonly decisions: DecisionDeps;
  readonly renderers: RenderTurnDeps;
  readonly phaseEnd: PhaseEndDeps;
}

type TurnEffect<A> = Effect.Effect<A, PlayError, PrivateFs | SessionStore | V2Client>;

// ---------------------------------------------------------------------------
// _cached_kind_action / _resolve_kind_action (client.py:6667-6696)
// ---------------------------------------------------------------------------

/** The single cached action of one kind, when the cache holds exactly one. */
export const cachedKindAction = (
  state: V2ClientState,
  kind: string,
  deps: DecisionDeps
): Effect.Effect<CompactAction | null, PlayerError> =>
  Effect.map(deps.orderPool(state), (pool) => {
    const matches = pool.filter((compact) => compactText(compact, 'kind') === kind);
    return matches.length === 1 ? (matches[0] ?? null) : null;
  });

export interface ResolvedKindAction {
  readonly compact: CompactAction;
  readonly state: V2ClientState;
}

/**
 * Return one cached action of this kind, enumerating it if it is absent.
 *
 * The enumeration is the ordinary drain this client already performs; it
 * happens inside the CLI so the agent never pays for a catalog it only wanted a
 * single handle from.
 */
export const resolveKindAction = (
  ctx: TurnCtx,
  kind: string,
  remedy: string
): TurnEffect<ResolvedKindAction> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const lookup = Effect.gen(function* () {
      const state = yield* store.readState(ctx.sessionPath, ctx.session);
      const compact = yield* cachedKindAction(state, kind, ctx.decisions);
      return { compact, state };
    });
    const cached = yield* lookup;
    const found =
      cached.compact !== null
        ? cached
        : yield* Effect.zipRight(ctx.phaseEnd.drainLegal, lookup);
    if (found.compact === null) {
      return yield* Effect.fail(
        playerError(`no ${kind} action is enumerable for this seat right now; ${remedy}`)
      );
    }
    return { compact: found.compact, state: found.state };
  });

// ---------------------------------------------------------------------------
// _phase_end_locked (client.py:6771-6799)
// ---------------------------------------------------------------------------

export interface PhaseEndResult {
  readonly disposition: BatchDisposition;
  readonly warning: string;
  readonly exitCode: number;
  readonly lines: ReadonlyArray<string>;
}

/**
 * Execute `phase.end` from the cached capability; the caller holds the lock.
 *
 * This is the one path that ends a phase.  `turn --end` and `do --end` both run
 * it, so the capability is resolved, enumerated when cold, and submitted
 * identically no matter which command composed it.
 */
export const phaseEnd = (ctx: TurnCtx): TurnEffect<PhaseEndResult> =>
  Effect.gen(function* () {
    const { compact } = yield* resolveKindAction(ctx, 'phase.end', PHASE_END_REMEDY);
    const args = ctx.phaseEnd.defaultArguments(compact);
    if (args === null) {
      return yield* Effect.fail(
        playerError(
          'this phase.end action takes arguments; run ' +
            '`just legal --kind phase.end --all` and submit it with ' +
            '`just batch`'
        )
      );
    }
    const batchId = yield* ctx.phaseEnd.persistBatchForAction(compactText(compact, 'action_id'), args);
    const outcome = yield* ctx.phaseEnd.submitPersistedBatch(batchId);
    return {
      disposition: outcome.disposition,
      warning: outcome.warning,
      exitCode: outcome.exitCode,
      lines: ctx.phaseEnd.renderDisposition(outcome.disposition, 'phase end'),
    };
  });

// ---------------------------------------------------------------------------
// _turn_briefing_locked (client.py:7599-7706)
// ---------------------------------------------------------------------------

/** The four parts one briefing is made of; the caller renders or prints them. */
export interface TurnBriefing {
  readonly result: TurnResult;
  readonly aliases: AliasMap | null;
  readonly decisions: ReadonlyArray<string>;
  readonly events: string;
}

const LOBBY_NEXT_COMMANDS: ReadonlyArray<string> = [
  'just state --section overview',
  'just state --section pregame_nations',
  'just state --section pregame_styles',
  'just state --section pregame_teams',
  'just legal',
];

const statusBriefing = (
  health: HealthEnvelope,
  status: 'terminal' | 'lobby' | 'not_ready',
  nextCommands: ReadonlyArray<string>
): TurnBriefing => ({
  result: {
    schema_version: 1,
    command: 'turn',
    status,
    context: turnHealthContext(health),
    next_commands: nextCommands,
  },
  aliases: null,
  decisions: [],
  events: '',
});

/**
 * Build one turn briefing; the caller already holds this seat's lock.
 *
 * Returning the parts instead of printing them is what lets `--brief` compose:
 * the same fetch, the same consistency retry and the same rendering serve
 * `just turn` and the tail of a one-call turn alike.
 *
 * The consistency check is the point of the double health read.  Four pages and
 * two health payloads have to describe one phase at one revision, or the
 * briefing is a composite of two different games; one restart is allowed, and a
 * second disagreement is a refusal naming the command that retries it.
 */
export const turnBriefingLocked = (ctx: TurnCtx): TurnEffect<TurnBriefing> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    // Two attempts at most: the retry only absorbs one revision bump landing
    // mid-briefing; `null` means "the pages moved, try once more".
    const attemptBriefing = (
      attempt: number
    ): TurnEffect<TurnBriefing | null> => Effect.gen(function* () {
      const health = yield* turnHealth(ctx.session);
      if (TERMINAL_STATES.has(health.game_state)) {
        return statusBriefing(health, 'terminal', [`just result ${ctx.session.gameId}`]);
      }
      const phase = health.phase;
      const actionable =
        health.game_state === 'running' &&
        health.observation_available === true &&
        phase !== null &&
        phase.active === true &&
        phase.state === 'awaiting_agent';
      if (!actionable) {
        return health.game_state === 'lobby'
          ? statusBriefing(health, 'lobby', LOBBY_NEXT_COMMANDS)
          : statusBriefing(health, 'not_ready', ['just wait']);
      }

      const pages: Record<string, PageEnvelope> = {};
      for (const section of V2_TURN_SECTIONS) {
        pages[section] = yield* turnPage(ctx.session, section);
      }
      const finalHealth = yield* turnHealth(ctx.session);
      const first = pages[V2_TURN_SECTIONS[0]];
      if (first === undefined) return yield* Effect.fail(playerError('the turn briefing lost a page'));
      const baseline = first.state_revision;
      const finalPhase = finalHealth.phase;
      const consistent =
        V2_TURN_SECTIONS.every((section) => {
          const page = pages[section];
          return page !== undefined && revisionsEqual(page.state_revision, baseline);
        }) &&
        turnHealthEpochsEqual(turnHealthEpoch(health), turnHealthEpoch(finalHealth)) &&
        (finalPhase === null || finalPhase.turn === null || finalPhase.turn === baseline.turn);
      if (!consistent) {
        if (attempt === 0) return null;
        return yield* Effect.fail(
          playerError(
            'the game changed twice while building the turn briefing; run `just turn` again'
          )
        );
      }

      // Read before the mirror is overwritten: the event total it still holds
      // is the one this seat has already been shown.
      const seenEvents = yield* mirrorEventCount(ctx.sessionPath);
      for (const section of V2_TURN_SECTIONS) {
        const page = pages[section];
        if (page !== undefined) yield* ctx.hooks.rememberPage(page);
      }
      for (const section of V2_TURN_SECTIONS) {
        const page = pages[section];
        if (page !== undefined) yield* ctx.hooks.mirrorPage(page, 'turn');
      }
      yield* mirrorHealth(ctx.sessionPath, finalHealth, 'turn', {
        revision: baseline,
        commands: V2_PROTOCOL_CARD,
      });

      const overviewPage = pages['overview'];
      const overviewItems = overviewPage === undefined ? [] : overviewPage.page.items;
      if (overviewItems.length !== 1) {
        return yield* Effect.fail(playerError('the turn briefing has no current overview'));
      }
      const overview = overviewItems[0];
      if (overview === undefined) {
        return yield* Effect.fail(playerError('the turn briefing has no current overview'));
      }
      const compact = (section: string): TurnCompactPage => {
        const page = pages[section];
        return page === undefined
          ? { shown: 0, total: 0, truncated: false, items: [], next_cursor: null, cursor_expires_at: null }
          : turnCompactPage(page);
      };
      const result: TurnResult = {
        schema_version: 1,
        command: 'turn',
        status: 'ready',
        context: turnHealthContext(finalHealth),
        state_revision: baseline,
        overview,
        cities: compact('cities'),
        units: compact('units'),
        research: compact('research'),
        next_commands: turnNextCommands(pages),
      };
      const state = yield* store.readState(ctx.sessionPath, ctx.session);
      return {
        result,
        aliases: yield* aliasMap(state),
        decisions: yield* briefingDecisionLines(ctx.sessionPath, state, ctx.decisions),
        events: briefingEventsLine(overview, seenEvents),
      };
    });
    const first = yield* attemptBriefing(0);
    return first ?? (yield* attemptBriefing(1)) ??
      (yield* Effect.fail(playerError('unreachable turn briefing state')));
  });

// ---------------------------------------------------------------------------
// _await_and_brief_locked (client.py:6802-6866)
// ---------------------------------------------------------------------------

export interface AwaitAndBriefOptions {
  readonly brief: boolean;
  /**
   * The receipt this command already produced, as a *mutable* buffer.
   *
   * It is flushed ahead of the first progress line and emptied, so a transcript
   * never shows a phase end arriving after the ten minutes spent waiting for
   * the phase that end opened.  `null` (every `--json` caller) keeps the whole
   * rendering for one atomic payload at the end.
   */
  readonly prelude?: string[] | null | undefined;
  readonly wait?: RawWaitArgs | undefined;
}

export interface AwaitAndBriefResult {
  readonly wait: WaitEnvelope;
  readonly briefing: TurnResult | null;
  /** The message a failed briefing carried, or `""`. */
  readonly briefError: string;
  readonly lines: ReadonlyArray<string>;
}

/**
 * Block for the next phase and, with `--brief`, render its whole briefing.
 *
 * One command, two logical steps: the extra state reads the briefing costs are
 * the same reads `just turn` would have made, and the round trip they save is
 * the model's, which is the expensive one.  A briefing that cannot be built
 * never swallows the wake — it is reported as one more line with the command
 * that retries it.
 *
 * `--await` means what it says: the wait runs to the *holder's* deadline rather
 * than to a fixed 120 s, so in multiplayer one call really does carry one turn.
 * And when it does come back with the phase still someone else's, no briefing is
 * printed — a full briefing for a phase the caller does not hold, tailed
 * `next: just turn`, invites an out-of-turn action that can only be refused.
 */
export const awaitAndBrief = (
  ctx: TurnCtx,
  options: AwaitAndBriefOptions
): TurnEffect<AwaitAndBriefResult> =>
  Effect.gen(function* () {
    const prelude = options.prelude ?? null;
    // The receipts this command already earned print BEFORE the wait begins,
    // not on its first tick: execution is done, and the transcript should say
    // so while the opponent is still thinking.
    if (prelude !== null && prelude.length > 0) {
      for (const text of [...prelude]) yield* echo(text);
      prelude.length = 0;
    }
    const tick = (health: HealthEnvelope): Effect.Effect<void> =>
      Effect.gen(function* () {
        if (prelude !== null && prelude.length > 0) {
          for (const text of [...prelude]) yield* echo(text);
          prelude.length = 0;
        }
        yield* echo(waitingTickLine(health));
      });

    const wait = yield* waitUntilTurn(
      waitCtx({
        sessionPath: ctx.sessionPath,
        session: ctx.session,
        hooks: ctx.waitHooks,
      }),
      waitArgs(options.wait ?? {}),
      {
        forTurn: false,
        ...(prelude === null ? {} : { echo: tick }),
      }
    );
    const lines: string[] = [awaitLine(wait, options.brief ? '' : 'just turn')];
    if (!options.brief) return { wait, briefing: null, briefError: '', lines };
    if (!phaseIsMine(wait.health)) {
      return {
        wait,
        briefing: null,
        briefError: '',
        lines: [
          ...lines,
          'not briefed: the phase is not yours yet, so there is nothing to order in it',
          'next: just wait --for-turn',
        ],
      };
    }
    const built = yield* Effect.either(turnBriefingLocked(ctx));
    if (built._tag === 'Left') {
      const message = built.left.message;
      return {
        wait,
        briefing: null,
        briefError: message,
        lines: [...lines, `briefing failed: ${message}`, 'next: just turn'],
      };
    }
    const briefing = built.right;
    lines.push(
      ...(yield* renderTurn(briefing.result, ctx.renderers, {
        aliases: briefing.aliases,
        decisions: briefing.decisions,
        events: briefing.events,
      }))
    );
    return { wait, briefing: briefing.result, briefError: '', lines };
  });

// ---------------------------------------------------------------------------
// _command_turn_end (client.py:6868-6937)
// ---------------------------------------------------------------------------

export interface TurnEndOptions {
  readonly awaitNext: boolean;
  readonly brief?: boolean | undefined;
  readonly json: boolean;
  readonly wait?: RawWaitArgs | undefined;
}

export interface TurnEndOutcome {
  /** What the process should exit with; `0` unless the *end itself* refused. */
  readonly exitCode: number;
}

/** The `--json` payloads `_command_turn_end` prints, kept where they are read. */
export const turnEndJson = (
  brief: boolean,
  disposition: BatchDisposition,
  wait: WaitEnvelope | null,
  briefing: TurnResult | null,
  briefError: string
): Readonly<Record<string, unknown>> =>
  brief
    ? compositeJson('turn', {
        end: disposition,
        wait,
        turn: briefing,
        turn_error: briefError === '' ? null : briefError,
      })
    : {
        schema_version: 1,
        command: 'turn',
        status: 'ended',
        disposition,
        wait,
      };

/**
 * End this phase from the cached capability, optionally blocking after.
 *
 * The exit status is the *end's*, never the wake's.  `--await` that times out,
 * wakes into somebody else's phase, or cannot build a briefing still leaves an
 * applied receipt behind, and the status has to say so — otherwise a harness's
 * job supervisor reads "the phase end failed" and re-ends a phase that is
 * already over.  Only a briefing that *errored* raises it to 2, and only
 * because that error is printed on the very same call.
 */
export const commandTurnEnd = (
  ctx: TurnCtx,
  options: TurnEndOptions
): TurnEffect<TurnEndOutcome> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const brief = options.brief === true;

    const inLock: TurnEffect<TurnEndState> = Effect.gen(function* () {
      const ended = yield* phaseEnd(ctx);
      const lines: string[] = [...ended.lines];
      const unawaited: TurnEndState = {
        disposition: ended.disposition,
        lines,
        exitCode: ended.exitCode,
        wait: null,
        briefing: null,
        briefError: '',
        rendered: false,
      };

      if (ended.warning !== '') yield* Console.error(ended.warning);

      if (!options.awaitNext) return unawaited;
      if (!ctx.phaseEnd.orderReceiptOk(ended.disposition)) {
        lines.push('not awaited: the phase end was not accepted');
        return unawaited;
      }
      const woke = yield* Effect.either(
        awaitAndBrief(ctx, {
          brief,
          prelude: options.json ? null : lines,
          ...(options.wait === undefined ? {} : { wait: options.wait }),
        })
      );
      if (woke._tag === 'Left') {
        // The end already applied; a wait/brief failure must never swallow
        // that receipt (a validation error here once hid three consecutive
        // applied phase-ends from a live agent).
        if (options.json) return yield* Effect.fail(woke.left);
        lines.push(
          'phase ended: the receipt above is authoritative',
          `await failed: ${woke.left.message}`,
          'next: just wait — the end already applied; do not ' +
            're-run `turn --end` for this phase'
        );
        yield* render(lines);
        return { ...unawaited, rendered: true };
      }
      lines.push(...woke.right.lines);
      return {
        ...unawaited,
        exitCode:
          woke.right.briefError !== '' ? Math.max(ended.exitCode, 2) : ended.exitCode,
        wait: woke.right.wait,
        briefing: woke.right.briefing,
        briefError: woke.right.briefError,
      };
    });

    const outcome = yield* store.withRequestLock(ctx.sessionPath, inLock);
    // The "await failed" arm printed inside the lock and exits 2 on the
    // *reporting*, not on the end: the receipt above it is authoritative.
    if (outcome.rendered) return { exitCode: 2 };

    if (options.json) {
      // NOT `printV2Json`.  Both shapes embed a `WaitEnvelope` under `"wait"`
      // (health under `wait.health`), and `--brief` adds the briefing under
      // `"turn"` (its `_turn_health_context` under `turn.context`).  Core's
      // encoder prints CPython's `600.0` as `600` on all twelve.  NOTES §10.6.
      yield* Console.log(
        pyDumps(
          pyValueWithFloats(
            turnEndJson(
              brief,
              outcome.disposition,
              outcome.wait,
              outcome.briefing,
              outcome.briefError
            ),
            healthFloatPathsUnder('wait.health', 'turn.context')
          ),
          true
        )
      );
    } else {
      yield* render(outcome.lines);
    }
    return { exitCode: outcome.exitCode };
  });

interface TurnEndState {
  readonly disposition: BatchDisposition;
  readonly lines: ReadonlyArray<string>;
  readonly exitCode: number;
  readonly wait: WaitEnvelope | null;
  readonly briefing: TurnResult | null;
  readonly briefError: string;
  /** The "await failed" arm already printed; the caller must not print again. */
  readonly rendered: boolean;
}
