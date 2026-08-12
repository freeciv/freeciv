/**
 * `play do "u1 found_city London; c1 build Warriors"` — the L2 intent surface.
 *
 * Ports `command_do` (client.py:9491-9744), `_do_phase_end` (9745-9776) and the
 * argparse surface at 11684-11717.  The two allowed PLAN upgrades land here:
 * the per-actor catalog drains run with bounded concurrency
 * (`src/services/do-drain.ts`) and every receipt is streamed to
 * `state/receipts.log` the moment it applies
 * (`src/services/receipt-ledger.ts`).  Neither changes a byte of stdout.
 *
 * The orders may arrive as `--orders` or positionally, because `just do "…"`
 * passes them positionally and `./play do "…"` must be the same command.
 *
 * The whole body runs under this seat's request lock and the loop is written
 * to be interruption-tolerant in one specific way: **an outcome this client
 * has already been told is never discarded.**  Every receipt becomes a
 * rendered line and a ledger record as soon as it arrives, and a failure
 * anywhere below turns into one more line rather than an exception that would
 * take the applied `batch_id`s with it.  Losing a `batch_id` tells the agent
 * its command failed while the server holds an applied batch, and the agent
 * then re-issues a real duplicate action.
 *
 * ### Where the cross-unit seams are
 *
 * `do` is the confluence of six other units.  Each arrives through
 * {@link DoHooks} rather than a direct import, following the seam pattern
 * PORT_MAP §6 established for `CatalogRenderDeps` and §"Addendum — U05" for
 * `WaitHooks`: the orchestration, its ordering guarantees and its ledger stay
 * testable without a network, and {@link liveDoHooks} binds every one of them
 * to the landed unit that owns it.
 */
import { Args, Command, Options } from '@effect/cli';
import { Console, Effect, Either, Layer } from 'effect';
import { playerError, type PlayError, type PlayerError } from 'src/errors';
import { exitWith, passThroughExit, type ExitCodeSignal } from 'src/exit';
import { dualFloat, resolveDual } from 'src/options';
import type { BatchDisposition } from 'src/schema/batch';
import type { JsonObject } from 'src/schema/primitives';
import { compareRevisions, revisionsEqual, type Revision } from 'src/schema/revision';
import { render, revisionLabel } from 'src/render/primitives';
import {
  refusedActorOptions,
  type ActorOptionsDeps,
  type RefusedActorOptionsIo,
} from 'src/render/actor-options';
import {
  actorAliases,
  distinctActors,
  drainActors,
  fetchedOptionsNote,
  firstDrainFailure,
  type DrainGate,
} from 'src/services/do-drain';
import { liveTurnSeams, turnCtx } from 'src/commands/turn.cmd';
import { orderReceiptOk } from 'src/services/disposition';
import { liveDecisionDeps, nextFocusLine } from 'src/services/decisions';
import { persistBatchForAction } from 'src/services/batch-persist';
import { submitBatch } from 'src/services/batch';
import { compactLegalAction } from 'src/services/legal-compact';
import { drainLegal } from 'src/services/legal-drain';
import type { LegalCtx } from 'src/services/legal-query';
import { legalRows } from 'src/render/legal/rows';
import { awaitAndBrief, phaseEnd } from 'src/services/turn-end';
import type { RawWaitArgs } from 'src/services/wait';
import { jsonRequested } from 'src/services/json-output';
import { pyDumps } from 'src/services/canonical-body';
import { healthFloatPathsUnder, pyValueWithFloats } from 'src/services/health-json';
import {
  orderFetchTargets,
  orderOutcomes,
  parseOrders,
  rebindOrder,
  refreshStaleOrderAliases,
  unresolvedReport,
  type OrderOutcome,
  type OrdersDeps,
  type OrdersFetchDeps,
  type ResolvedOrder,
} from 'src/services/orders';
import { PrivateFs } from 'src/services/private-fs';
import { ledgerEntry, recordReceipt } from 'src/services/receipt-ledger';
import { renderDisposition } from 'src/render/receipt';
import { V2_ONE_CALL_END } from 'src/render/turn';
import { SessionStore, type Session, type V2ClientState } from 'src/services/session-store';
import { V2Client } from 'src/services/v2-client';

export { V2_ONE_CALL_END };
export type { OrderOutcome, ResolvedOrder };

// ---------------------------------------------------------------------------
// The cross-unit contract
// ---------------------------------------------------------------------------

/**
 * `_submit_persisted_batch`'s 3-tuple, as U13 landed it (`BatchSubmission`).
 */
export interface SubmitOutcome {
  readonly disposition: BatchDisposition;
  /** A sentence for stderr, or `null`.  Never part of stdout. */
  readonly warning: string | null;
  readonly exitCode: number;
}

/** `_phase_end_locked`'s 4-tuple, as U12 landed it (`PhaseEndResult`). */
export interface PhaseEndOutcome {
  readonly disposition: BatchDisposition;
  readonly warning: string;
  readonly exitCode: number;
  readonly lines: ReadonlyArray<string>;
}

/**
 * `_await_and_brief_locked`'s 4-tuple, as U12 landed it.
 *
 * `wait` and `briefing` are `unknown` on purpose: `do` never reads inside
 * them, it only forwards them into `--json`'s composite under the part names
 * `_composite_json` fixed.  Typing them as U12's envelopes here would make
 * `do` a consumer of a shape it has no opinion about.
 */
export interface AwaitBriefOutcome {
  readonly wait: unknown;
  readonly briefing: unknown;
  readonly briefError: string;
  readonly lines: ReadonlyArray<string>;
}

/** The cache after any stale alias was re-bound, plus the lines to print. */
export interface RefreshedAliases {
  readonly state: V2ClientState;
  readonly notes: ReadonlyArray<string>;
}

/**
 * Everything `do` needs from the units it composes.
 *
 * `parseOrders` (U15), `orderReceiptOk` and `renderDisposition` (U14) are
 * *not* here: they are pure and landed, so they are imported directly.  What
 * remains is exactly the set that talks to the wire or to the turn, which is
 * also exactly the set a test has to be able to script.
 */
export interface DoHooks extends ActorOptionsDeps {
  // --- U15, the order engine -----------------------------------------------
  /** `_order_outcomes` — resolve every order against the cache only. */
  readonly orderOutcomes: (
    state: V2ClientState,
    orders: ReadonlyArray<string>
  ) => Effect.Effect<ReadonlyArray<OrderOutcome>, PlayError>;
  /** `_order_fetch_targets` — the actors an unresolved order named and this
   * seat has never drained. */
  readonly orderFetchTargets: (
    state: V2ClientState,
    outcomes: ReadonlyArray<OrderOutcome>
  ) => ReadonlyArray<string>;
  /** `_unresolved_report` — the per-order table and its enumeration commands. */
  readonly unresolvedReport: (
    state: V2ClientState,
    outcomes: ReadonlyArray<OrderOutcome>
  ) => Effect.Effect<ReadonlyArray<string>, PlayError>;
  /** `_refresh_stale_order_aliases` — re-bind an `aN` the orders name. */
  readonly refreshStaleOrderAliases: (
    state: V2ClientState,
    orders: ReadonlyArray<string>
  ) => Effect.Effect<RefreshedAliases, PlayError>;
  /** `_rebind_order` — re-point one resolved order at the newest handle. */
  readonly rebindOrder: (
    state: V2ClientState,
    resolved: ResolvedOrder
  ) => Effect.Effect<ResolvedOrder | null, PlayError>;

  // --- U11, the catalog ----------------------------------------------------
  /**
   * `_drain_legal_unlocked` — the caller holds the request lock already.
   *
   * `gate` is the ordered-commit barrier `drainActors` hands each drain; the
   * live wiring passes it straight through as U11's `LegalCtx.gate`, so the
   * page fetches overlap and the ingest still numbers `a1..aN` in the orders'
   * own sequence.
   */
  readonly drainLegalUnlocked: (
    actorId: string,
    gate: DrainGate
  ) => Effect.Effect<Revision | null, PlayError>;

  // --- U13, the batch ------------------------------------------------------
  /** `_persist_batch_for_action` — write the batch before the POST. */
  readonly persistBatchForAction: (
    actionId: string,
    args: JsonObject
  ) => Effect.Effect<string, PlayError>;
  /** `_submit_persisted_batch` — POST it and derive the disposition. */
  readonly submitPersistedBatch: (batchId: string) => Effect.Effect<SubmitOutcome, PlayError>;

  // --- U12, the turn -------------------------------------------------------
  /** `_phase_end_locked` — the one path that ends a phase. */
  readonly phaseEndLocked: () => Effect.Effect<PhaseEndOutcome, PlayError>;
  /**
   * `_await_and_brief_locked` — block for the next phase, optionally brief.
   *
   * `prelude` is the receipt buffer this command already produced, passed as a
   * *mutable* array exactly as CPython passed its `lines` list: the wait's
   * first progress line flushes it and empties it, so a transcript never shows
   * a phase end arriving after the ten minutes spent waiting for the phase
   * that end opened.  `null` (every `--json` caller) keeps the whole rendering
   * for one atomic payload at the end.
   */
  readonly awaitAndBriefLocked: (options: {
    readonly brief: boolean;
    readonly prelude: Array<string> | null;
  }) => Effect.Effect<AwaitBriefOutcome, PlayError>;
  /** `_next_focus_line` — one focus line for the whole batch, cache only. */
  readonly nextFocusLine: (
    state: V2ClientState,
    ordered: ReadonlySet<string>
  ) => Effect.Effect<string, PlayError>;
}

/** How a `do` invocation builds its hooks once the session is known. */
export type DoHooksFor = (
  sessionPath: string,
  session: Session,
  wait: WaitArguments
) => Effect.Effect<DoHooks, PlayError, PrivateFs | SessionStore | V2Client>;

/** The `--wait-s`/`--poll-s`/`--until` triple `_wait_args` reads. */
export interface WaitArguments {
  readonly waitS: number;
  readonly pollS: number;
  readonly until: string;
}

// ---------------------------------------------------------------------------
// The command's own arguments
// ---------------------------------------------------------------------------

export interface DoArguments {
  readonly session: string;
  readonly orders: string;
  readonly positionalOrders: ReadonlyArray<string>;
  readonly continueOnError: boolean;
  readonly noRefresh: boolean;
  readonly endPhase: boolean;
  readonly awaitPhase: boolean;
  readonly brief: boolean;
  readonly json: boolean;
  readonly wait: WaitArguments;
}

/** One order's full record, as `--json` carries it. */
export interface DoRecord {
  readonly order: string;
  readonly action_id: string;
  readonly arguments: JsonObject;
  readonly disposition: BatchDisposition;
}

const failureText = (error: { readonly message: string }): string => error.message;

const newer = (candidate: Revision, bound: Revision | null): boolean =>
  bound === null || compareRevisions(candidate, bound) > 0;

/**
 * `if receipt["state_revision"] == state["last_revision"]`.
 *
 * CPython compared dicts, so a `None` cache revision is simply unequal and the
 * re-enumeration runs — which is the safe direction.
 */
const matchesCached = (receipt: Revision, cached: Revision | null): boolean =>
  cached !== null && revisionsEqual(receipt, cached);

// ---------------------------------------------------------------------------
// The refusals argparse could not express
// ---------------------------------------------------------------------------

export const ORDERS_TWICE =
  'orders were given twice; pass them once, either as ' +
  '`just do "u1 found_city London"` or as --orders';

export const AWAIT_WITHOUT_END =
  'just do --await blocks after ending the phase; use `just do "…" ' +
  '--end --await`, or `just wait` on its own';

export const BRIEF_WITHOUT_WAKE =
  'just do --brief prints the briefing of the phase it just woke ' +
  'into, so it needs the wake: use `just do "…" --end --await ' +
  '--brief`';

/** The orders text, from whichever spelling supplied it. */
export const ordersText = (
  written: string,
  positional: ReadonlyArray<string>
): Effect.Effect<string, PlayerError> => {
  const one = written.trim();
  const other = positional.join(' ').trim();
  if (one !== '' && other !== '') return Effect.fail(playerError(ORDERS_TWICE));
  return Effect.succeed(one !== '' ? one : other);
};

// ---------------------------------------------------------------------------
// The order resolution front half
// ---------------------------------------------------------------------------

const resolvedOf = (outcomes: ReadonlyArray<OrderOutcome>): ReadonlyArray<ResolvedOrder> => {
  const out: Array<ResolvedOrder> = [];
  for (const outcome of outcomes) {
    if (outcome.resolved !== null) out.push(outcome.resolved);
  }
  return out;
};

/** `_resolve_orders` — resolve every order up front, or refuse the batch. */
const resolveOrders = (
  hooks: DoHooks,
  state: V2ClientState,
  orders: ReadonlyArray<string>
): Effect.Effect<ReadonlyArray<ResolvedOrder>, PlayError> =>
  Effect.gen(function* () {
    const outcomes = yield* hooks.orderOutcomes(state, orders);
    if (outcomes.some((outcome) => outcome.resolved === null)) {
      const report = yield* hooks.unresolvedReport(state, outcomes);
      return yield* Effect.fail(playerError(report.join('\n')));
    }
    return resolvedOf(outcomes);
  });

/**
 * `_resolve_orders_fetching` — draw each unread actor's catalog once first.
 *
 * A briefing does not enumerate capabilities, so the first `do` of a turn names
 * actors whose catalog this seat has never read.  Every such actor is fetched
 * *before* anything is sent, and the batch is then re-resolved whole, so a
 * genuinely bad verb still refuses with the per-order table and no order
 * reaches the wire.
 *
 * The drains are the concurrent region.  `notes` is appended to only after
 * every drain has finished, in the actors' own order, so the refusal text is
 * byte-identical to the serial one.
 */
const resolveOrdersFetching = (
  hooks: DoHooks,
  readState: Effect.Effect<V2ClientState, PlayError>,
  state: V2ClientState,
  orders: ReadonlyArray<string>,
  notes: Array<string>
): Effect.Effect<
  { readonly pending: ReadonlyArray<ResolvedOrder>; readonly state: V2ClientState },
  PlayError
> =>
  Effect.gen(function* () {
    const outcomes = yield* hooks.orderOutcomes(state, orders);
    const targets = hooks.orderFetchTargets(state, outcomes);
    if (targets.length === 0) {
      if (outcomes.some((outcome) => outcome.resolved === null)) {
        const report = yield* hooks.unresolvedReport(state, outcomes);
        return yield* Effect.fail(playerError(report.join('\n')));
      }
      return { pending: resolvedOf(outcomes), state };
    }
    // CPython built this table *before* the loop, from the pre-drain aliases.
    const aliases = actorAliases(state.entity_aliases);
    // `stopOnFailure` is CPython's `for` loop raising: the actors after the
    // failing one are never ingested, so `drained_actors` and the `a1..aN`
    // numbering are left exactly where the raise left them and the agent's
    // retry of the same batch prints the same notes.
    const drains = yield* drainActors(targets, hooks.drainLegalUnlocked, {
      stopOnFailure: true,
    });
    const failure = firstDrainFailure(drains);
    // A drain that CPython would have raised on is raised on here too, before
    // any note is printed — the fetch notes below only describe drains that
    // actually happened.
    if (failure !== null) return yield* Effect.fail(failure);
    for (const drain of drains) {
      notes.push(fetchedOptionsNote(aliases[drain.actorId] ?? drain.actorId, drain.revision));
    }
    const fresh = yield* readState;
    const retried = yield* Effect.either(resolveOrders(hooks, fresh, orders));
    if (Either.isLeft(retried)) {
      // The fetch already happened; a refusal that hid it would leave the
      // agent unable to tell a cold cache from a wrong word.
      return yield* Effect.fail(
        playerError([...notes, failureText(retried.left)].join('\n'))
      );
    }
    return { pending: retried.right, state: fresh };
  });

/**
 * `_refresh_orders` — re-enumerate exactly the catalogs the rest name.
 *
 * One drain per distinct `actor_id` in first-seen order, **including the empty
 * one**: an order whose capability carries no `subject.actor` — `end`,
 * `research set …`, anything in the player family — resolves with
 * `actor_id == ""`, and CPython drains it as a full-catalog re-enumeration
 * rather than skipping it.  Skipping it is how such an order becomes
 * `revN/tM no longer offers …` after the previous receipt wiped the cache.
 */
const refreshOrders = (
  hooks: DoHooks,
  pending: ReadonlyArray<ResolvedOrder>
): Effect.Effect<void, PlayError> =>
  Effect.gen(function* () {
    const actors = distinctActors(pending.map((item) => item.actor_id));
    // The same raise, mid-batch: `_refresh_orders` (client.py:9389-9398) stops
    // at the first failing actor, so none of the later ones is re-enumerated
    // and none of them renumbers its aliases behind the refusal.
    const drains = yield* drainActors(actors, hooks.drainLegalUnlocked, {
      stopOnFailure: true,
    });
    const failure = firstDrainFailure(drains);
    if (failure !== null) return yield* Effect.fail(failure);
  });

// ---------------------------------------------------------------------------
// The command
// ---------------------------------------------------------------------------

/** What one `do` invocation produced, before it is printed. */
interface DoOutcome {
  readonly lines: ReadonlyArray<string>;
  readonly options: ReadonlyArray<string>;
  readonly records: ReadonlyArray<DoRecord>;
  readonly applied: number;
  readonly bound: Revision | null;
  readonly stopped: string;
  readonly ended: boolean;
  readonly ending: BatchDisposition | null;
  readonly wait: unknown;
  readonly briefing: unknown;
  readonly briefError: string;
  readonly ordered: ReadonlySet<string>;
  readonly exitCode: number;
}

export const runDo = (
  args: DoArguments,
  makeHooks: DoHooksFor
): Effect.Effect<void, PlayError | ExitCodeSignal, PrivateFs | SessionStore | V2Client> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const loaded = yield* store.resolveV2(args.session);
    const sessionPath = loaded.path;
    const session = loaded.session;
    // Every refusal above this line happens before a single request leaves the
    // client — `blocked.assert_not_called()` in the Python tests is the whole
    // point of the ordering.
    const text = yield* ordersText(args.orders, args.positionalOrders);
    const orders = yield* parseOrders(text);
    const keepGoing = args.continueOnError;
    const endPhase = args.endPhase;
    const awaitNext = args.awaitPhase;
    const brief = args.brief;
    if (awaitNext && !endPhase) return yield* Effect.fail(playerError(AWAIT_WITHOUT_END));
    if (brief && !(endPhase && awaitNext)) {
      return yield* Effect.fail(playerError(BRIEF_WITHOUT_WAKE));
    }
    const hooks = yield* makeHooks(sessionPath, session, args.wait);
    const json = jsonRequested('do', args.json);
    const readState = store.readState(sessionPath, session);

    const outcome = yield* store.withRequestLock(
      sessionPath,
      doLocked({
        hooks,
        readState,
        sessionPath,
        orders,
        keepGoing,
        noRefresh: args.noRefresh,
        endPhase,
        awaitNext,
        brief,
        json,
      })
    );

    const lines = [...outcome.lines];
    if (json) {
      const payload: Record<string, unknown> = {
        schema_version: 1,
        command: 'do',
        orders: outcome.records,
        requested: orders.length,
        applied: outcome.applied,
        state_revision: outcome.bound,
        stopped: outcome.stopped === '' ? null : outcome.stopped,
      };
      if (endPhase) {
        // A new surface: the composite only ever appears when the new flag
        // asked for it, so the shape `just do --json` has always returned is
        // untouched.
        payload['end'] = outcome.ending;
        payload['wait'] = outcome.wait;
        payload['turn'] = outcome.briefing;
        payload['turn_error'] = outcome.briefError === '' ? null : outcome.briefError;
      }
      // NOT `printV2Json`: with `--end` the payload embeds a `WaitEnvelope`
      // under `"wait"` and the next briefing under `"turn"`, so the twelve
      // supervisor floats ride along; core's encoder prints `600` where
      // CPython prints `600.0`.  Without `--end` neither key exists and the
      // projection is a no-op.  NOTES §10.6.
      yield* Console.log(
        pyDumps(
          pyValueWithFloats(payload, healthFloatPathsUnder('wait.health', 'turn.context')),
          true
        )
      );
    } else {
      // The options go above the focus line: the tail is what to run next, and
      // it stays the last word of the command.
      lines.push(...outcome.options);
      if (outcome.applied > 0 && !outcome.ended) {
        const focus = yield* hooks.nextFocusLine(yield* readState, outcome.ordered);
        if (focus !== '') lines.push(focus);
      }
      yield* render(lines);
    }
    if (outcome.exitCode !== 0) {
      return yield* Effect.fail(exitWith(passThroughExit(outcome.exitCode)));
    }
    return;
  });

interface LockedInput {
  readonly hooks: DoHooks;
  readonly readState: Effect.Effect<V2ClientState, PlayError>;
  readonly sessionPath: string;
  readonly orders: ReadonlyArray<string>;
  readonly keepGoing: boolean;
  readonly noRefresh: boolean;
  readonly endPhase: boolean;
  readonly awaitNext: boolean;
  readonly brief: boolean;
  readonly json: boolean;
}

/** Everything `command_do` does while holding this seat's request lock. */
const doLocked = (input: LockedInput): Effect.Effect<DoOutcome, PlayError, PrivateFs> =>
  Effect.gen(function* () {
    const { hooks, readState, orders, keepGoing, endPhase, awaitNext, brief, json } = input;
    const lines: Array<string> = [];
    const records: Array<DoRecord> = [];
    const refused: Array<string> = [];
    const ordered = new Set<string>();
    let exitCode = 0;
    let applied = 0;
    let ending: BatchDisposition | null = null;
    let wait: unknown = null;
    let briefing: unknown = null;
    let briefError = '';
    let ended = false;
    let options: ReadonlyArray<string> = [];

    let state = yield* readState;
    let pending: Array<ResolvedOrder>;
    if (input.noRefresh) {
      pending = [...(yield* resolveOrders(hooks, state, orders))];
    } else {
      const refreshed = yield* hooks.refreshStaleOrderAliases(state, orders);
      state = refreshed.state;
      lines.push(...refreshed.notes);
      const fetched = yield* resolveOrdersFetching(hooks, readState, state, orders, lines);
      pending = [...fetched.pending];
      state = fetched.state;
    }

    let bound: Revision | null = state.last_revision;
    let stopped = '';

    while (pending.length > 0) {
      const resolved = pending[0];
      if (resolved === undefined) break;
      pending = pending.slice(1);

      const persisted = yield* Effect.either(
        hooks.persistBatchForAction(resolved.action_id, resolved.arguments)
      );
      if (Either.isLeft(persisted)) {
        lines.push(`${resolved.order} → not sent: ${failureText(persisted.left)}`);
        stopped = `stopped after order ${records.length}`;
        exitCode = Math.max(exitCode, 2);
        break;
      }
      const batchId = persisted.right;
      const sent = yield* Effect.either(hooks.submitPersistedBatch(batchId));
      if (Either.isLeft(sent)) {
        lines.push(`${resolved.order} → not sent: ${failureText(sent.left)}`);
        stopped =
          `stopped after order ${records.length}` +
          (batchId === ''
            ? ''
            : `; resolve ${batchId} with \`just receipt --batch_id ${batchId}\``);
        exitCode = Math.max(exitCode, 2);
        break;
      }
      const { disposition, warning, exitCode: code } = sent.right;
      records.push({
        order: resolved.order,
        action_id: resolved.action_id,
        arguments: resolved.arguments,
        disposition,
      });
      if (resolved.actor_id !== '') ordered.add(resolved.actor_id);
      lines.push(...renderDisposition(disposition, resolved.order));
      // PLAN upgrade 2.  The record is durable before the next order is even
      // persisted, so a SIGKILL between two orders still leaves the applied
      // one readable.  It cannot fail this command.
      yield* recordReceipt(
        input.sessionPath,
        ledgerEntry(resolved.order, resolved.action_id, resolved.arguments, disposition)
      );
      if (warning !== null && warning !== '') yield* Console.error(warning);
      const receipt = disposition.receipt;
      // The summary must never contradict the receipt printed above it: take
      // the newest revision this command has actually been shown.
      if (receipt !== null && newer(receipt.state_revision, bound)) {
        bound = receipt.state_revision;
      }
      if (orderReceiptOk(disposition)) {
        applied += 1;
      } else {
        if (resolved.actor_id !== '' && !refused.includes(resolved.actor_id)) {
          refused.push(resolved.actor_id);
        }
        exitCode = Math.max(exitCode, code === 0 ? 2 : code);
        if (!keepGoing) {
          stopped =
            `stopped after order ${records.length}; ` +
            `${pending.length} not sent (pass --continue-on-error to keep going)`;
          break;
        }
      }
      if (pending.length === 0 || receipt === null) continue;
      if (matchesCached(receipt.state_revision, state.last_revision)) continue;
      // The order that just landed moved the game on, so every remaining
      // handle is a stale capability.  Re-enumerate exactly what the remaining
      // orders name and re-bind them; never send a handle this client already
      // knows is expired.
      const reread = yield* Effect.either(refreshOrders(hooks, pending));
      if (Either.isLeft(reread)) {
        lines.push(`could not re-enumerate the remaining orders: ${failureText(reread.left)}`);
        stopped = `stopped after order ${records.length}; ${pending.length} not sent`;
        exitCode = Math.max(exitCode, 2);
        break;
      }
      state = yield* readState;
      if (state.last_revision !== null && newer(state.last_revision, bound)) {
        bound = state.last_revision;
      }
      const bindings = state;
      const rebound = yield* Effect.forEach(pending, (item) =>
        hooks.rebindOrder(bindings, item)
      );
      if (rebound.some((item) => item === null)) {
        const lost = pending
          .filter((_item, index) => rebound[index] === null)
          .map((item) => item.order)
          .join(', ');
        stopped =
          `${bound === null ? 'no revision' : revisionLabel(bound)} no longer offers ` +
          `${lost}; re-read the actor and re-issue those orders`;
        exitCode = Math.max(exitCode, 2);
        break;
      }
      pending = rebound.filter((item): item is ResolvedOrder => item !== null);
    }

    // The batch summary is complete the moment the order loop is: nothing
    // below changes what applied.  Appending it here rather than after the
    // phase end keeps the rendering in the order it always had while letting a
    // blocking `--await` flush everything above it first.
    const summary =
      `${applied}/${orders.length} applied` + (bound === null ? '' : ` ${revisionLabel(bound)}`);
    lines.push(summary);
    if (stopped !== '') lines.push(stopped);

    let tail: Array<string> = [];
    if (endPhase) {
      // A batch that did not finish leaves the phase open and says so — ending
      // a phase whose orders were refused would spend the turn on an empire
      // the agent never gave.
      const finished = applied === orders.length || (keepGoing && stopped === '');
      if (!finished) {
        tail.push(
          `phase NOT ended: ${applied}/${orders.length} orders ` +
            'applied, so the turn is still yours; fix the refused ' +
            'order and re-issue it, or end anyway with ' +
            `\`${V2_ONE_CALL_END}\``
        );
        exitCode = Math.max(exitCode, 2);
      } else {
        const end = yield* doPhaseEnd(hooks, awaitNext, exitCode);
        tail = [...end.lines];
        ending = end.ending;
        ended = end.ended;
        exitCode = end.exitCode;
        if (ended && awaitNext) {
          // CPython handed `lines` itself to the wait, which flushed and
          // emptied it on the first progress line.  Same array, same effect.
          let prelude: Array<string> | null = null;
          if (!json) {
            lines.push(...tail);
            tail = [];
            prelude = lines;
          }
          const woken = yield* Effect.either(hooks.awaitAndBriefLocked({ brief, prelude }));
          if (Either.isLeft(woken)) {
            if (json) return yield* Effect.fail(woken.left);
            const message = failureText(woken.left);
            tail.push(
              'phase ended: the receipts above are authoritative',
              `await failed: ${message}`,
              'next: just wait — the end already applied; ' +
                'do not re-run `turn --end` for this phase'
            );
            briefError = message;
          } else {
            wait = woken.right.wait;
            briefing = woken.right.briefing;
            briefError = woken.right.briefError;
            tail.push(...woken.right.lines);
          }
          if (briefError !== '') exitCode = Math.max(exitCode, 2);
        } else if (ended) {
          tail.push(
            'next: just wait — or add --await --brief to spend one call on the whole turn'
          );
        }
      }
    }

    // A refusal that left the turn open is answered in place: the refused
    // actor's own options print under it, so the next call is the fixed order
    // and not another enumeration.  `--json` is untouched, and so is the wire.
    if (refused.length > 0 && !ended && !json) {
      const io: RefusedActorOptionsIo = {
        readState,
        drainActor: hooks.drainLegalUnlocked,
        compactLegalAction: hooks.compactLegalAction,
        legalRows: hooks.legalRows,
      };
      options = yield* refusedActorOptions(refused, io);
    }

    return {
      lines: [...lines, ...tail],
      options,
      records,
      applied,
      bound,
      stopped,
      ended,
      ending,
      wait,
      briefing,
      briefError,
      ordered,
      exitCode,
    };
  });

// ---------------------------------------------------------------------------
// _do_phase_end
// ---------------------------------------------------------------------------

/** `_do_phase_end`'s 4-tuple. */
export interface DoPhaseEndResult {
  readonly lines: ReadonlyArray<string>;
  readonly ending: BatchDisposition | null;
  readonly ended: boolean;
  readonly exitCode: number;
}

/**
 * Run `do --end`'s phase end, turning any failure into printable lines.
 *
 * The applied orders are already receipted, so an end that cannot run must
 * never leave as an exception: it becomes one more line, with the command that
 * finishes the turn by hand.
 */
export const doPhaseEnd = (
  hooks: DoHooks,
  awaitNext: boolean,
  exitCode: number
): Effect.Effect<DoPhaseEndResult> =>
  Effect.gen(function* () {
    const attempt = yield* Effect.either(hooks.phaseEndLocked());
    if (Either.isLeft(attempt)) {
      // The refusal carries its own remedy, and the focus tail below still
      // names what the turn has left; nothing more is added here.
      return {
        lines: [`phase NOT ended: ${failureText(attempt.left)}`],
        ending: null,
        ended: false,
        exitCode: Math.max(exitCode, 2),
      };
    }
    const { disposition, warning, exitCode: code, lines } = attempt.right;
    if (warning !== '') yield* Console.error(warning);
    const ended = orderReceiptOk(disposition);
    const out = [...lines];
    if (!ended) {
      out.push(
        'phase NOT ended: the phase end above was not accepted' + (awaitNext ? '; not awaited' : '')
      );
    }
    return { lines: out, ending: disposition, ended, exitCode: Math.max(exitCode, code) };
  });

// ---------------------------------------------------------------------------
// The live seams
// ---------------------------------------------------------------------------

/**
 * Every hook bound to the landed unit that owns it.
 *
 * Nothing here re-implements a line of another unit: `orderOutcomes`,
 * `orderFetchTargets`, `unresolvedReport`, `refreshStaleOrderAliases` and
 * `rebindOrder` are U15's; `drainLegal`, `compactLegalAction` and `legalRows`
 * are U11's; `persistBatchForAction` and `submitBatch` are U13's; `phaseEnd`,
 * `awaitAndBrief` and `nextFocusLine` are U12's.  The indirection exists so
 * every ordering guarantee above stays testable without a network.
 *
 * The services are captured once here and provided back per call, the way
 * `liveWaitHooks` does it, so each hook is an `Effect` with no requirements
 * and `refusedActorOptions` can stay requirement-free.
 */
export const liveDoHooks: DoHooksFor = (sessionPath, session, wait) =>
  Effect.gen(function* () {
    const files = yield* PrivateFs;
    const store = yield* SessionStore;
    const client = yield* V2Client;
    const provided = Layer.mergeAll(
      Layer.succeed(PrivateFs, files),
      Layer.succeed(SessionStore, store),
      Layer.succeed(V2Client, client)
    );
    const give = <A, E>(body: Effect.Effect<A, E, PrivateFs | SessionStore | V2Client>) =>
      Effect.provide(body, provided);

    const orderDeps: OrdersDeps = { compactLegalAction };
    const legalCtx: LegalCtx = { sessionPath, session };
    // U15's `drainLegal` seam is `LegalPageFetcher`-shaped: the fetch is void
    // and the revision comes back through `.v2-state`.  `do` wants the
    // revision itself for its `fetched uN options (revM)` note, so the drain
    // is called directly and the fetcher shape is only what U15 is handed.
    const fetchOnly: OrdersFetchDeps = {
      ...orderDeps,
      drainLegal: (path, current, actorId) =>
        Effect.provide(
          Effect.mapError(
            Effect.asVoid(drainLegal({ sessionPath: path, session: current }, actorId)),
            (error) => playerError(error.message)
          ),
          provided
        ),
    };
    const turn = turnCtx(sessionPath, session, yield* liveTurnSeams(sessionPath, session));
    const waitArgs: RawWaitArgs = {
      waitS: wait.waitS,
      pollS: wait.pollS,
      until: wait.until,
    };

    const hooks: DoHooks = {
      orderOutcomes: (state, orders) =>
        give(orderOutcomes(orderDeps, state, sessionPath, orders)),
      orderFetchTargets,
      unresolvedReport: (state, outcomes) =>
        give(unresolvedReport(sessionPath, state, outcomes)),
      refreshStaleOrderAliases: (state, orders) =>
        give(refreshStaleOrderAliases(fetchOnly, sessionPath, session, state, orders)),
      rebindOrder: (state, resolved) => rebindOrder(orderDeps, state, resolved),

      // No `mapError` here, deliberately.  `drainLegal` fails with U11's
      // `LegalError`, which includes `V2ResponseError`, and CPython called
      // `_drain_legal_unlocked` from `_resolve_orders_fetching` *outside* any
      // try/except: a supervisor refusal during `do`'s pre-send catalog drain
      // reached `main` (client.py:11891-11900), which renders
      // `_render_error_payload(exc.payload)` on stdout — and under `--json`
      // the byte-identical wire payload.  Collapsing it into a `PlayerError`
      // would put a bare `error: …` on stderr and nothing on stdout, on the
      // hottest pre-send path this command has.  `DoHooks.drainLegalUnlocked`
      // declares `PlayError` precisely so the payload survives to `cli-main`.
      drainLegalUnlocked: (actorId, gate) =>
        give(Effect.map(drainLegal({ ...legalCtx, gate }, actorId), (drained) => drained.revision)),

      persistBatchForAction: (actionId, args) =>
        give(persistBatchForAction(sessionPath, session, actionId, args)),
      submitPersistedBatch: (batchId) =>
        give(
          Effect.map(
            submitBatch(sessionPath, session, batchId),
            (submission): SubmitOutcome => ({
              disposition: submission.disposition,
              warning: submission.warning,
              exitCode: submission.exitCode,
            })
          )
        ),

      phaseEndLocked: () => give(phaseEnd(turn)),
      awaitAndBriefLocked: (options) =>
        give(
          Effect.map(
            awaitAndBrief(turn, {
              brief: options.brief,
              prelude: options.prelude,
              wait: waitArgs,
            }),
            (result): AwaitBriefOutcome => ({
              wait: result.wait,
              briefing: result.briefing,
              briefError: result.briefError,
              lines: result.lines,
            })
          )
        ),
      nextFocusLine: (state, ordered) =>
        give(nextFocusLine(sessionPath, state, ordered, liveDecisionDeps)),

      compactLegalAction,
      legalRows,
    };
    return hooks;
  });

// ---------------------------------------------------------------------------
// The @effect/cli surface
// ---------------------------------------------------------------------------

const sessionOption = Options.text('session').pipe(
  Options.withDefault(''),
  Options.withDescription('the private session file this command acts against')
);

const ordersOption = Options.text('orders').pipe(
  Options.withDefault(''),
  Options.withDescription('1..8 semicolon-separated orders, e.g. "u1 found_city London"')
);

const continueOption = Options.boolean('continue-on-error').pipe(
  Options.withDescription('keep issuing later orders after one is rejected')
);

const noRefreshOption = Options.boolean('no-refresh').pipe(
  Options.withDescription('refuse a stale action alias instead of re-binding it')
);

const endOption = Options.boolean('end').pipe(
  Options.withDescription('end this phase once every order in the batch has applied')
);

const awaitOption = Options.boolean('await').pipe(
  Options.withDescription('with --end: block until the next phase, then print its header')
);

const briefOption = Options.boolean('brief').pipe(
  Options.withDescription('with --end --await: print the whole next briefing on waking')
);

/**
 * `do.add_argument("--until", choices=("phase", "revision"), default="phase")`.
 *
 * A *choice*, not free text — `wait` could afford `Options.text` because its
 * only effect is which sentence refuses the run, but `do` submits the whole
 * batch before the wait engine ever sees `--until`.  argparse rejected a bad
 * value at parse time with a usage dump and status 2, having sent nothing, and
 * so does this: `cli-main` maps `ValidationError` to the same 2 after
 * `@effect/cli` has printed the usage document.
 */
const untilOption = Options.choice('until', ['phase', 'revision']).pipe(
  Options.withDefault('phase'),
  Options.withDescription('what the wait is waiting for: phase or revision')
);

const jsonOption = Options.boolean('json').pipe(
  Options.withDescription('print the full-fidelity JSON payload instead of text')
);

/**
 * `just do "…"` passes the orders positionally, so `./play do "…"` must too,
 * or the two spellings are not one command.
 */
const positionalOrders = Args.text({ name: 'orders' }).pipe(Args.repeated);

/** Build the command against a given set of cross-unit hooks. */
export const doCommandWith = (makeHooks: DoHooksFor) =>
  Command.make(
    'do',
    {
      session: sessionOption,
      orders: ordersOption,
      positional: positionalOrders,
      continueOnError: continueOption,
      noRefresh: noRefreshOption,
      endPhase: endOption,
      awaitPhase: awaitOption,
      brief: briefOption,
      waitS: dualFloat('wait-s'),
      pollS: dualFloat('poll-s'),
      until: untilOption,
      json: jsonOption,
    },
    (options) =>
      Effect.gen(function* () {
        const waitS = yield* resolveDual('wait-s', options.waitS, 120);
        const pollS = yield* resolveDual('poll-s', options.pollS, 1);
        yield* runDo(
          {
            session: options.session,
            orders: options.orders,
            positionalOrders: options.positional,
            continueOnError: options.continueOnError,
            noRefresh: options.noRefresh,
            endPhase: options.endPhase,
            awaitPhase: options.awaitPhase,
            brief: options.brief,
            json: options.json,
            wait: { waitS, pollS, until: options.until },
          },
          makeHooks
        );
      })
  );

export const doCommand = doCommandWith(liveDoHooks);
