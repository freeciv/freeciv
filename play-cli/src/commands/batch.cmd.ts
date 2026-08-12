/**
 * `play batch` — issue exactly one advertised action as a `CommandBatch`.
 *
 * Ports `command_batch` (client.py:8547-8550), `_batch_command` (8553-8616) and
 * the argparse surface at 11771-11780.
 *
 * This is the L1 surface: the agent names one server-issued `action_id` (or the
 * `aN` alias the catalog printed for it) and a JSON argument object, and the
 * client persists, sends and reports it.  `do` (U16) is the same machinery with
 * an order parser in front of it.
 *
 * Three things about the shape of this command are deliberate:
 *
 * 1. **Persist, then send.**  `persistBatchForAction` commits the canonical
 *    bytes before the POST leaves, so a kill between the two still leaves a
 *    batch `retry` can resolve.
 * 2. **The actor is read before the send.**  Applying an action bumps the
 *    revision, which wipes the descriptor the "what else can this actor do"
 *    section needs; reading it afterwards would silently print nothing.
 * 3. **The whole mutation runs under this seat's request lock**, and the
 *    rendering runs outside it — a second `play` process must not be able to
 *    interleave a send between the persist and the response, but neither should
 *    it wait on a terminal write.
 */
import { Command, Options } from '@effect/cli';
import { Console, Effect, Layer } from 'effect';
import { exitWith, type ExitCodeSignal } from 'src/exit';
import {
  type DriftError,
  type LockTimeoutError,
  type PlayerError,
  type SessionMissingError,
} from 'src/errors';
import { dualText, resolveDualRequired } from 'src/options';
import { refusedActorOptions, type RefusedActorOptionsIo } from 'src/render/actor-options';
import { legalRows } from 'src/render/legal/rows';
import { render } from 'src/render/primitives';
import { renderDisposition } from 'src/render/receipt';
import type { BatchDisposition } from 'src/schema/batch';
import { isJsonObject, opaque, type JsonObject } from 'src/schema/primitives';
import { resolveAliasArguments, type LegalPageFetcher } from 'src/services/alias-refresh';
import {
  batchIntent,
  batchDisposition,
  parseJsonObject,
  submitBatch,
  type BatchSubmission,
} from 'src/services/batch';
import { persistBatchForAction } from 'src/services/batch-persist';
import type { PyObject } from 'src/services/canonical-body';
import { liveDecisionDeps, nextFocusLine } from 'src/services/decisions';
import { orderReceiptOk } from 'src/services/disposition';
import { jsonRequested, printV2Json } from 'src/services/json-output';
import { compactLegalAction } from 'src/services/legal-compact';
import { drainLegal, legalPageFetcher } from 'src/services/legal-drain';
import { orderActor } from 'src/services/orders';
import { phaseAwareRefusal } from 'src/services/pregame';
import { PrivateFs } from 'src/services/private-fs';
import { SessionStore, type Session, type V2ClientState } from 'src/services/session-store';
import { V2Client } from 'src/services/v2-client';

// ---------------------------------------------------------------------------
// The cross-unit seams
// ---------------------------------------------------------------------------

/** Everything `batch` needs that lives in a unit U13 does not own. */
export type BatchEnv = SessionStore | PrivateFs | V2Client;

/**
 * Every failure `batch` can produce, including the quiet exit-2 signal.
 *
 * `ExitCodeSignal` is in the union because a refused batch is *reported*, not
 * *raised*: the disposition prints on stdout and the process finishes 2 without
 * `cli-main` adding an `error: …` line the Python never printed.
 */
export type BatchError =
  | PlayerError
  | DriftError
  | LockTimeoutError
  | SessionMissingError
  | ExitCodeSignal;

export interface BatchHooks {
  /**
   * U11's scoped drain, used to re-bind a stale `aN`.
   *
   * **`undefined` means the ambient `V2Client`'s**, not "never refresh":
   * `_resolve_alias_arguments(..., ("action_id",))` (client.py:8557) re-binds a
   * stale `aN` by draining its actor's catalog on every `batch` that did not
   * pass `--no-refresh`, and that is the hot path after any revision bump.  The
   * seam exists so a test can pin the drain, and `--no-refresh` is how a caller
   * asks for the plain refusal.
   */
  readonly fetchLegal: LegalPageFetcher | undefined;
  /** U15's `_order_actor` over U11's `_compact_legal_action`. */
  readonly orderActor: (descriptor: JsonObject) => Effect.Effect<string, PlayerError>;
  /** U14's `_order_receipt_ok`. */
  readonly orderReceiptOk: (disposition: BatchDisposition) => boolean;
  /** U14's `_render_disposition`. */
  readonly renderDisposition: (
    disposition: BatchDisposition,
    intent: string
  ) => ReadonlyArray<string>;
  /** U16's `_refused_actor_options`. */
  readonly refusedActorOptions: (
    sessionPath: string,
    session: Session,
    actors: ReadonlyArray<string>
  ) => Effect.Effect<ReadonlyArray<string>, PlayerError, BatchEnv>;
  /** U12's `_next_focus_line`. */
  readonly nextFocusLine: (
    sessionPath: string,
    state: V2ClientState,
    actors: ReadonlySet<string>
  ) => Effect.Effect<string, PlayerError, BatchEnv>;
  /** Injected so a test can pin the batch ID, as CPython's tests patch `secrets`. */
  readonly token: (() => string) | undefined;
}

// ---------------------------------------------------------------------------
// The cross-unit hooks, as far as the landed units go
// ---------------------------------------------------------------------------

/**
 * The seams `batch` fills from the units that own them.
 *
 * Nothing here re-implements a line of another unit: `renderDisposition` and
 * `orderReceiptOk` are U14's, `orderActor` is U15's over U11's
 * `_compact_legal_action`, `refusedActorOptions` is U16's over U11's
 * `_legal_rows`, `nextFocusLine` is U12's, and the alias re-bind reaches U11's
 * `legalPageFetcher` through {@link runBatch}.  This closes PORT_MAP §8's
 * integrator checklist steps 2 and 4: until it landed, `batch` behaved as
 * though `--no-refresh` were always passed, and a refused batch answered with
 * neither the actor's own menu nor the focus tail `_batch_command`
 * (client.py:8595-8608) prints.
 *
 * The bundle is a plain value rather than a factory because every seam either
 * is pure or takes its services from the `BatchEnv` the command already runs
 * in — the one that cannot, the catalog drain, is bound per invocation where
 * the `V2Client` instance is in scope.
 */
export const liveBatchHooks: BatchHooks = {
  fetchLegal: undefined,
  orderActor: (descriptor) => Effect.map(compactLegalAction(descriptor), orderActor),
  orderReceiptOk,
  renderDisposition,
  refusedActorOptions: (sessionPath, session, actors) =>
    Effect.gen(function* () {
      const files = yield* PrivateFs;
      const store = yield* SessionStore;
      const client = yield* V2Client;
      const provided = Layer.mergeAll(
        Layer.succeed(PrivateFs, files),
        Layer.succeed(SessionStore, store),
        Layer.succeed(V2Client, client)
      );
      const io: RefusedActorOptionsIo = {
        readState: Effect.provide(store.readState(sessionPath, session), provided),
        // The caller holds this seat's request lock, so the drain must take no
        // new one — `drainLegal` is exactly `_drain_legal_unlocked`.
        drainActor: (actorId, gate) =>
          Effect.provide(
            Effect.map(
              drainLegal({ sessionPath, session, gate }, actorId),
              (drained) => drained.revision
            ),
            provided
          ),
        compactLegalAction,
        legalRows,
      };
      return yield* refusedActorOptions(actors, io);
    }),
  nextFocusLine: (sessionPath, state, actors) =>
    nextFocusLine(sessionPath, state, actors, liveDecisionDeps),
  token: undefined,
};

// ---------------------------------------------------------------------------
// _phase_aware_refusal (client.py:10802-10817) is U18's
//
// PORT_MAP §7 is explicit that `command_legal` (U11) and `command_batch` (U13)
// import it from `src/services/pregame` rather than reimplement it, and the
// reason is visible in what U13's own copy got wrong: it matched
// `_tag === 'PlayerError'`, while CPython catches `PlayerError` — the class
// `_opaque` (client.py:1272-1275, the port's `DriftError`) and
// `_private_advisory_lock` (532-563, the port's `LockTimeoutError`) also raise.
// `play batch --action-id ""` after the phase ended is exactly the anti-loop
// case the Python comment names, and the copy printed one stderr line where
// CPython prints two.
//
// U18's version catches every `PlayError` and re-raises a plain `PlayerError`
// carrying the joined text, which is literally what CPython does.  The one
// thing it must never see is `ExitCodeSignal` — not a refusal but the quiet
// exit code of a *reported* disposition — and it cannot: `runBatch` returns
// that code out of the wrapped block, exactly as `_batch_command` returns it
// out of the `with`.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// The command body
// ---------------------------------------------------------------------------

export interface BatchArgs {
  readonly session: string;
  readonly actionId: string;
  readonly arguments: string;
  readonly noRefresh: boolean;
  readonly json: boolean;
}

interface Issued extends BatchSubmission {
  readonly intent: string;
  readonly actor: string;
  readonly notes: ReadonlyArray<string>;
  readonly options: ReadonlyArray<string>;
}

const LOCAL_RECOVERY_LOST =
  'local recovery state became unavailable after persistence; resolve this batch ' +
  'by receipt before any retry';

const AMBIGUOUS_IS_TERMINAL = 'Ambiguous is terminal; never replay this batch.';

const issue = (
  sessionPath: string,
  session: Session,
  actionId: string,
  args: PyObject,
  notes: ReadonlyArray<string>,
  json: boolean,
  hooks: BatchHooks
): Effect.Effect<Issued, PlayerError | DriftError | LockTimeoutError, BatchEnv> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const batchId = yield* persistBatchForAction(sessionPath, session, actionId, args, (hooks.token === undefined ? {} : { token: hooks.token }));
    const cached = yield* store.readState(sessionPath, session);
    const intent = batchIntent(cached, batchId);
    // Read the actor before the send: applying the action bumps the revision,
    // which wipes the descriptor this lookup needs.
    const descriptor = cached.actions[actionId];
    const actor = isJsonObject(descriptor) ? yield* hooks.orderActor(descriptor) : '';
    const submission = yield* Effect.catchAll(
      submitBatch(sessionPath, session, batchId),
      () =>
        Effect.map(
          batchDisposition(session, batchId, 'receipt_first'),
          (disposition): BatchSubmission => ({
            disposition,
            warning: LOCAL_RECOVERY_LOST,
            exitCode: 2,
          })
        )
    );
    // Same bargain as `do`: a refused action prints what its actor can do
    // instead of costing a whole extra command to find out.
    const options =
      actor !== '' && !hooks.orderReceiptOk(submission.disposition) && !json
        ? yield* hooks.refusedActorOptions(sessionPath, session, [actor])
        : [];
    return { ...submission, intent, actor, notes, options };
  });

export const runBatch = (
  args: BatchArgs,
  hooks: BatchHooks = liveBatchHooks
): Effect.Effect<void, BatchError, BatchEnv> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const client = yield* V2Client;
    const loaded = yield* store.resolveV2(args.session);
    const sessionPath = loaded.path;
    const session = loaded.session;
    const json = jsonRequested('batch', args.json);
    // `LegalPageFetcher` is frozen with `V2Client` absent from its requirements
    // (PORT_MAP §6), so the client is supplied here, where the Layer stack has
    // already provided it, rather than inside the hook bundle.
    const fetchLegal = hooks.fetchLegal ?? legalPageFetcher(client);
    // `_batch_command` *returns* its exit code out of the `with` block; only a
    // refusal is raised through it.  Keeping the code a return value is what
    // makes the quiet exit-2 of a reported disposition unprefixable.
    const exitCode = yield* phaseAwareRefusal(
      sessionPath,
      Effect.gen(function* () {
        const resolved = yield* resolveAliasArguments(
          sessionPath,
          session,
          { action_id: args.actionId },
          { noRefresh: args.noRefresh, fetch: fetchLegal }
        );
        const actionId = yield* opaque(
          (resolved.values['action_id'] ?? '').trim(),
          'action ID'
        );
        const parsed = yield* parseJsonObject(args.arguments, '--arguments');
        const issued = yield* store.withRequestLock(
          sessionPath,
          issue(sessionPath, session, actionId, parsed, resolved.notes, json, hooks)
        );
        if (json) {
          yield* printV2Json(issued.disposition);
        } else {
          const lines = [
            ...issued.notes,
            ...hooks.renderDisposition(issued.disposition, issued.intent),
          ];
          if (hooks.orderReceiptOk(issued.disposition)) {
            const state = yield* store.readState(sessionPath, session);
            const focus = yield* hooks.nextFocusLine(
              sessionPath,
              state,
              new Set(issued.actor === '' ? [] : [issued.actor])
            );
            if (focus !== '') lines.push(focus);
          } else {
            lines.push(...issued.options);
          }
          yield* render(lines);
        }
        if (issued.warning !== null) yield* Console.error(issued.warning);
        const receipt = issued.disposition.receipt;
        if (receipt !== null && receipt.receipt_state === 'ambiguous') {
          yield* Console.error(AMBIGUOUS_IS_TERMINAL);
        }
        return issued.exitCode;
      })
    );
    if (exitCode !== 0) return yield* Effect.fail(exitWith(exitCode));
    return;
  });

// ---------------------------------------------------------------------------
// The argparse surface (client.py:11771-11780)
// ---------------------------------------------------------------------------

const sessionOption = Options.text('session').pipe(
  Options.withDefault(''),
  Options.withDescription('the private session file this command acts against')
);

const argumentsOption = Options.text('arguments').pipe(
  Options.withDefault('{}'),
  Options.withDescription("the action's arguments, as one strict JSON object")
);

const noRefreshOption = Options.boolean('no-refresh').pipe(
  Options.withDescription('refuse a stale action alias instead of re-binding it')
);

const jsonOption = Options.boolean('json').pipe(
  Options.withDescription('print the full-fidelity JSON payload instead of text')
);

/** Build the command against a given set of cross-unit hooks. */
export const batchCommandWith = (hooks: BatchHooks) =>
  Command.make(
    'batch',
    {
      session: sessionOption,
      actionId: dualText('action-id'),
      arguments: argumentsOption,
      noRefresh: noRefreshOption,
      json: jsonOption,
    },
    (options) =>
      Effect.gen(function* () {
        const actionId = yield* resolveDualRequired('action-id', options.actionId);
        return yield* runBatch(
          {
            session: options.session,
            actionId,
            arguments: options.arguments,
            noRefresh: options.noRefresh,
            json: options.json,
          },
          hooks
        );
      })
  );

export const batchCommand = batchCommandWith(liveBatchHooks);
