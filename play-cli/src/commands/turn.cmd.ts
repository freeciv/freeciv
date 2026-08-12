/**
 * `play turn` — the agent's per-turn home screen.
 *
 * Ports `command_turn` (client.py:7564-7598), `_emit_turn` (6649-6665),
 * `_command_turn_decisions` (7516-7562) and the argparse surface at
 * 11650-11672.
 *
 * Four shapes, one command:
 *
 * - bare `turn` fetches `V2_TURN_SECTIONS` at `V2_TURN_PAGE_LIMIT`, mirrors
 *   them and prints the briefing;
 * - `--decisions` prints one row per actor that can still act;
 * - `--end` ends the phase through the cached `phase.end` capability;
 * - `--end --await [--brief]` blocks on the wait engine and prints the next
 *   phase's header, or its whole briefing.
 *
 * The flag matrix is checked before anything opens a socket, because each
 * refusal names the form that works and a refusal that costs a round trip is a
 * refusal that costs a model turn.
 */
import { Command, Options } from '@effect/cli';
import { Console, Effect, Layer } from 'effect';
import { playerError, type PlayError } from 'src/errors';
import { exitWith, type ExitCodeSignal } from 'src/exit';
import { dualFloat, resolveDual } from 'src/options';
import { batchFocusCommand, decisionLine } from 'src/render/decisions';
import { render, revisionLabel } from 'src/render/primitives';
import { V2_ONE_CALL_END, renderTurn, type RenderTurnDeps, type TurnResult } from 'src/render/turn';
import { holderSeat } from 'src/render/wait';
import { economyText, researchText, scoreText } from 'src/render/state/overview';
import { renderSectionItems } from 'src/render/state/page';
import { renderTiles } from 'src/render/state/tiles';
import { routeSummary } from 'src/render/state/units';
import type { PageEnvelope } from 'src/schema/page';
import { V2_PROTOCOL_CARD } from 'src/render/join';
import { aliasMap, rememberPage } from 'src/services/aliases';
import { submitBatch } from 'src/services/batch';
import { persistBatchForAction } from 'src/services/batch-persist';
import { decisionRows, liveDecisionDeps, type DecisionDeps } from 'src/services/decisions';
import { orderReceiptOk } from 'src/services/disposition';
import { jsonRequested, printV2Json } from 'src/services/json-output';
import { pyDumps } from 'src/services/canonical-body';
import { healthFloatPathsUnder, pyValueWithFloats } from 'src/services/health-json';
import { drainLegal } from 'src/services/legal-drain';
import type { LegalCtx } from 'src/services/legal-query';
import { mirrorHealth } from 'src/services/mirror';
// U04's barrel does not re-export the page bridge (NOTES §U12.3); the module
// that owns `update_from_page` does, and its own tests import it the same way.
import { mirrorPage } from 'src/services/mirror/update-page';
import { defaultArguments } from 'src/services/orders';
import { fetchStateSection, type PregameItem, type StartHooks } from 'src/services/pregame';
import { PrivateFs, type PrivateFsApi } from 'src/services/private-fs';
import { renderDisposition } from 'src/render/receipt';
import { SessionStore, type Session } from 'src/services/session-store';
import {
  commandTurnEnd,
  turnBriefingLocked,
  type PhaseEndDeps,
  type TurnCtx,
} from 'src/services/turn-end';
import { mirrorFile, mirrorIsFresh, type TurnHooks } from 'src/services/turn-pages';
import { V2Client } from 'src/services/v2-client';
import type { WaitHooks } from 'src/services/wait';

// ---------------------------------------------------------------------------
// The cross-unit seams
// ---------------------------------------------------------------------------

/** Everything `turn` needs that lives in another unit, built per invocation. */
export interface TurnSeams {
  readonly hooks: TurnHooks;
  readonly waitHooks: WaitHooks;
  readonly decisions: DecisionDeps;
  readonly renderers: RenderTurnDeps;
  readonly phaseEnd: PhaseEndDeps;
}

export type TurnSeamsFor = (
  sessionPath: string,
  session: Session
) => Effect.Effect<TurnSeams, never, PrivateFs | SessionStore>;

/**
 * The `StartHooks` bundle `fetchStateSection` is handed.
 *
 * U18's drain reads exactly one member of it — `mirrorPage` — so the other
 * seven are unreachable from this call site and refuse rather than pretend.
 * (`start` itself supplies the real ones; `turn --decisions` only wants the
 * section drain.)
 *
 * `mirrorPage` is the one that matters: `_fetch_state_section`
 * (client.py:10867-10889) projects every page it drains under the command name
 * `"start"`, whoever called it, and `--decisions` reads exactly those TSVs back.
 * A drain that does not write them is a drain `_mirror_is_fresh` can never
 * satisfy, so every invocation would re-fetch both sections and still find no
 * actors.
 */
const unreachableStartHook = (name: string) =>
  Effect.fail(playerError(`turn does not reach start's ${name} hook`));

/** Never read: `choose` is `start`'s nation picker, which the drain never calls. */
const UNREACHABLE_ITEM: PregameItem = { id: '', name: '' };

const sectionDrainHooks = (sessionPath: string, files: PrivateFsApi): StartHooks => ({
  mirrorPage: (page, aliases, command) =>
    Effect.provideService(
      mirrorPage(sessionPath, page, command, { aliases }),
      PrivateFs,
      files
    ),
  choose: (items) => items[0] ?? UNREACHABLE_ITEM,
  resolveKindAction: () => unreachableStartHook('resolveKindAction'),
  drainLegal: () => unreachableStartHook('drainLegal'),
  persistBatchForAction: () => unreachableStartHook('persistBatchForAction'),
  submitPersistedBatch: () => unreachableStartHook('submitPersistedBatch'),
  renderDisposition: () => unreachableStartHook('renderDisposition'),
  receiptOk: () => false,
});

/** U10's live-state renderers, which the briefing reuses verbatim. */
export const liveRenderTurnDeps: RenderTurnDeps = {
  routeSummary,
  economyText,
  researchText,
  scoreText,
  renderSectionItems,
  renderTiles,
};

/**
 * The real seams.
 *
 * Every one is another unit's landed entry point: U03's `rememberPage`, U04's
 * `mirrorHealth`, U06's `holderSeat`, U10's renderers, U11's drain and
 * `readLegalPage`, U13's batch persistence and submission, U14's disposition
 * rendering and `orderReceiptOk`, U15's matcher and U18's `fetchStateSection`.
 * Nothing below re-implements a line of any of them; the indirection exists so
 * every projection in this unit stays testable against a hand-built cache.
 */
export const liveTurnSeams: TurnSeamsFor = (sessionPath, session) =>
  Effect.gen(function* () {
    const files = yield* PrivateFs;
    const store = yield* SessionStore;
    const provided = Layer.merge(
      Layer.succeed(PrivateFs, files),
      Layer.succeed(SessionStore, store)
    );
    const remember = (page: PageEnvelope): Effect.Effect<void, PlayError> =>
      Effect.provide(
        Effect.asVoid(rememberPage(sessionPath, session, { legal: false, page })),
        provided
      );
    /**
     * `_mirror_page` (client.py:3016-3027).
     *
     * CPython names the page's rows from the state its `_remember_page` calls
     * have already folded back into the caller's dict, so the alias column of
     * every row carries the handle this seat just learned.  Reading the state
     * back here is the same value: the ingestion is persisted, and the request
     * lock is held for the whole briefing.
     */
    const mirror = (page: PageEnvelope, command: string): Effect.Effect<void, PlayError> =>
      Effect.gen(function* () {
        const current = yield* store.readState(sessionPath, session);
        const aliases = yield* aliasMap(current);
        yield* Effect.provideService(
          mirrorPage(sessionPath, page, command, { aliases }),
          PrivateFs,
          files
        );
      });
    const legalCtx: LegalCtx = { sessionPath, session };
    const hooks: TurnHooks = {
      rememberPage: remember,
      mirrorPage: mirror,
      fetchStateSection: (section) =>
        Effect.asVoid(
          fetchStateSection(
            { sessionPath, session, hooks: sectionDrainHooks(sessionPath, files) },
            section
          )
        ),
    };
    const waitHooks: WaitHooks = {
      rememberPage: remember,
      mirrorPage: mirror,
      // `_mirror_health` (client.py:3062-3072) *always* names the protocol
      // card; without it `update_from_health` falls back to its five-line
      // default and every wait tick rewrites `state/header.txt` shorter than
      // CPython leaves it.
      mirrorHealth: (health, command) =>
        Effect.provide(
          mirrorHealth(sessionPath, health, command, { commands: V2_PROTOCOL_CARD }),
          provided
        ),
      holderSeat,
    };
    const phaseEnd: PhaseEndDeps = {
      drainLegal: Effect.asVoid(drainLegal(legalCtx)),
      defaultArguments,
      persistBatchForAction: (actionId, args) =>
        persistBatchForAction(sessionPath, session, actionId, args),
      submitPersistedBatch: (batchId) =>
        Effect.map(submitBatch(sessionPath, session, batchId), (submission) => ({
          disposition: submission.disposition,
          warning: submission.warning ?? '',
          exitCode: submission.exitCode,
        })),
      renderDisposition,
      orderReceiptOk,
    };
    return {
      hooks,
      waitHooks,
      decisions: liveDecisionDeps,
      renderers: liveRenderTurnDeps,
      phaseEnd,
    };
  });

/** Assemble the context every entry point in this unit acts against. */
export const turnCtx = (
  sessionPath: string,
  session: Session,
  seams: TurnSeams
): TurnCtx => ({
  sessionPath,
  session,
  hooks: seams.hooks,
  waitHooks: seams.waitHooks,
  decisions: seams.decisions,
  renderers: seams.renderers,
  phaseEnd: seams.phaseEnd,
});

// ---------------------------------------------------------------------------
// _emit_turn (client.py:6649-6665)
// ---------------------------------------------------------------------------

export interface EmitTurnOptions {
  readonly json: boolean;
  readonly aliases?: Readonly<Record<string, string>> | null | undefined;
  readonly decisions?: ReadonlyArray<string> | undefined;
  readonly events?: string | undefined;
}

/**
 * Print one briefing.
 *
 * The decision summaries and the events line are projections of pages this
 * command already fetched and of files it already wrote, so they belong to the
 * rendering only: `--json` stays the byte-identical wire result.
 */
export const emitTurn = (
  result: TurnResult,
  renderers: RenderTurnDeps,
  options: EmitTurnOptions
): Effect.Effect<void, PlayError> =>
  options.json
    ? // NOT `printV2Json`: `result.context` is `_turn_health_context`, which
      // copies `phase` and `last_phase_end` in by reference and so carries all
      // twelve supervisor floats.  Core's encoder prints `600` where CPython
      // prints `600.0`.  NOTES §10.6.
      Console.log(pyDumps(pyValueWithFloats(result, healthFloatPathsUnder('context')), true))
    : Effect.flatMap(
        renderTurn(result, renderers, {
          aliases: options.aliases ?? null,
          ...(options.decisions === undefined ? {} : { decisions: options.decisions }),
          ...(options.events === undefined ? {} : { events: options.events }),
        }),
        render
      );

// ---------------------------------------------------------------------------
// _command_turn_decisions (client.py:7516-7562)
// ---------------------------------------------------------------------------

const DECISION_SECTIONS = ['units', 'cities'] as const;

/** Print one row per actor that can act this phase. */
export const commandTurnDecisions = (
  ctx: TurnCtx,
  options: { readonly json: boolean }
): Effect.Effect<void, PlayError, PrivateFs | SessionStore | V2Client> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const walked = yield* store.withRequestLock(
      ctx.sessionPath,
      Effect.gen(function* () {
        for (const section of DECISION_SECTIONS) {
          const current = yield* store.readState(ctx.sessionPath, ctx.session);
          const fresh = yield* mirrorIsFresh(
            ctx.sessionPath,
            mirrorFile(section),
            current.last_revision
          );
          if (!fresh) yield* ctx.hooks.fetchStateSection(section);
        }
        let state = yield* store.readState(ctx.sessionPath, ctx.session);
        if ((yield* ctx.decisions.orderPool(state)).length === 0) {
          // One unscoped drain enumerates every actor at once.  A catalog this
          // client already holds at this revision is never re-fetched.
          yield* ctx.phaseEnd.drainLegal;
          state = yield* store.readState(ctx.sessionPath, ctx.session);
        }
        return {
          rows: yield* decisionRows(ctx.sessionPath, state, ctx.decisions),
          revision: state.last_revision,
        };
      })
    );

    const { rows, revision } = walked;
    if (options.json) {
      yield* printV2Json({
        schema_version: 1,
        command: 'turn',
        status: 'decisions',
        state_revision: revision,
        decisions: rows.map((row) => ({
          alias: row.alias,
          actor_id: row.actor_id,
          state: row.state,
          options: row.options,
          option_count: row.option_count,
        })),
      });
      return;
    }
    const label = revision === null ? 'no revision' : revisionLabel(revision);
    if (rows.length === 0) {
      yield* render([`${label} decisions 0 — no actors need orders; ${V2_ONE_CALL_END}`]);
      return;
    }
    const lines = [`${label} decisions ${rows.length}`, ...rows.map((row) => decisionLine(row))];
    // The list closes with the batch it implies, so reading who needs orders
    // and writing the one command that orders them is a single call.
    const batched = batchFocusCommand(rows);
    if (batched !== '') lines.push(batched);
    yield* render(lines);
  });

// ---------------------------------------------------------------------------
// command_turn (client.py:7564-7598)
// ---------------------------------------------------------------------------

export const AWAIT_WITHOUT_END =
  'just turn --await blocks after ending the phase; use ' +
  '`just turn --end --await`, or `just wait` on its own';

export const BRIEF_WITHOUT_WAKE =
  'just turn --brief prints the briefing of the phase it just woke ' +
  'into, so it needs the wake: use `just turn --end --await --brief`';

export const DECISIONS_WITH_END =
  'just turn --decisions lists what still needs orders; run it ' +
  'before `just turn --end`, not with it';

export interface TurnOptions {
  readonly session: string;
  readonly end: boolean;
  readonly awaitPhase: boolean;
  readonly brief: boolean;
  readonly decisions: boolean;
  readonly json: boolean;
  readonly waitS: number;
  readonly pollS: number;
  readonly until: string;
}

/** The flag matrix, refused before anything opens a socket. */
export const checkTurnFlags = (options: {
  readonly end: boolean;
  readonly awaitPhase: boolean;
  readonly brief: boolean;
  readonly decisions: boolean;
}): Effect.Effect<void, PlayError> => {
  if (options.awaitPhase && !options.end) return Effect.fail(playerError(AWAIT_WITHOUT_END));
  if (options.brief && !(options.end && options.awaitPhase)) {
    return Effect.fail(playerError(BRIEF_WITHOUT_WAKE));
  }
  if (options.decisions && options.end) return Effect.fail(playerError(DECISIONS_WITH_END));
  return Effect.void;
};

export const runTurn = (
  options: TurnOptions,
  makeSeams: TurnSeamsFor
): Effect.Effect<void, PlayError | ExitCodeSignal, PrivateFs | SessionStore | V2Client> =>
  Effect.gen(function* () {
    yield* checkTurnFlags(options);
    const store = yield* SessionStore;
    const loaded = yield* store.resolveV2(options.session);
    const json = jsonRequested('turn', options.json);
    const ctx = turnCtx(
      loaded.path,
      loaded.session,
      yield* makeSeams(loaded.path, loaded.session)
    );

    if (options.decisions) return yield* commandTurnDecisions(ctx, { json });
    if (options.end) {
      const outcome = yield* commandTurnEnd(ctx, {
        awaitNext: options.awaitPhase,
        brief: options.brief,
        json,
        wait: { waitS: options.waitS, pollS: options.pollS, until: options.until },
      });
      // An applied phase end never exits non-zero because of how the wait
      // after it turned out; only the end's own status reaches here.
      if (outcome.exitCode !== 0) return yield* Effect.fail(exitWith(outcome.exitCode));
      return;
    }

    const briefing = yield* store.withRequestLock(loaded.path, turnBriefingLocked(ctx));
    yield* emitTurn(briefing.result, ctx.renderers, {
      json,
      aliases: briefing.aliases,
      decisions: briefing.decisions,
      events: briefing.events,
    });
  });

// ---------------------------------------------------------------------------
// The @effect/cli surface (client.py:11650-11672)
// ---------------------------------------------------------------------------

const sessionOption = Options.text('session').pipe(
  Options.withDefault(''),
  Options.withDescription('the private session file this command acts against')
);

/**
 * `turn.add_argument("--until", choices=("phase", "revision"), default="phase")`
 * (client.py:11670).
 *
 * A *choice*, not free text.  `wait` can afford `Options.text` because a bad
 * value there only decides which sentence refuses a command that has sent
 * nothing; `turn --end` cannot, because it ends the phase *before* the wait
 * engine ever reads `--until`.  With free text, `turn --end --await --until
 * phse` resolved `phase.end`, submitted the batch, and only then failed inside
 * `waitValue` — printing the `phase ended: the receipt above is authoritative`
 * arm and exiting 2 on a run where argparse exits 2 from the parser having
 * touched nothing.  A rejected flag would have ended a phase that CPython never
 * ends, which is irreversible.  `cli-main` maps `ValidationError` to the same 2
 * after `@effect/cli` prints the usage document, so the refusal lands where
 * argparse's did: before anything opens a socket.  (`do` fixed the identical
 * case; see `src/commands/do.cmd.ts`.)
 */
const untilOption = Options.choice('until', ['phase', 'revision']).pipe(
  Options.withDefault('phase'),
  Options.withDescription('what the wait is waiting for: phase or revision')
);

const jsonOption = Options.boolean('json').pipe(
  Options.withDescription('print the full-fidelity JSON payload instead of text')
);

const flag = (name: string, description: string) =>
  Options.boolean(name).pipe(Options.withDescription(description));

/** Build the command against a given set of cross-unit seams. */
export const turnCommandWith = (makeSeams: TurnSeamsFor) =>
  Command.make(
    'turn',
    {
      session: sessionOption,
      end: flag('end', 'end this phase using the cached phase.end capability'),
      awaitPhase: flag('await', 'with --end: block until the next phase, then print its header'),
      brief: flag('brief', 'with --end --await: print the whole next briefing on waking'),
      decisions: flag('decisions', 'one row per actor that can still act, with its best options'),
      waitS: dualFloat('wait-s'),
      pollS: dualFloat('poll-s'),
      until: untilOption,
      json: jsonOption,
    },
    (options) =>
      Effect.gen(function* () {
        const waitS = yield* resolveDual('wait-s', options.waitS, 120);
        const pollS = yield* resolveDual('poll-s', options.pollS, 1);
        yield* runTurn(
          {
            session: options.session,
            end: options.end,
            awaitPhase: options.awaitPhase,
            brief: options.brief,
            decisions: options.decisions,
            json: options.json,
            waitS,
            pollS,
            until: options.until,
          },
          makeSeams
        );
      })
  );

export const turnCommand = turnCommandWith(liveTurnSeams);
