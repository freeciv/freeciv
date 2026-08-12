/**
 * `play start` — configure this lobby seat and ready it.
 *
 * Ports `command_start` (client.py:11013-11155) and the argparse surface at
 * 11674-11682.
 *
 * Two wire commands, in this order and never fused: `pregame.configure`, then a
 * mandatory re-enumeration, then `pregame.set_ready`.  Configuring the seat
 * bumps the state revision, which expires every handle enumerated before it —
 * naming the readiness capability from the pre-configure catalog would submit
 * an action id the server has already retired.
 *
 * Everything the flags do not say is resolved rather than asked for: the nation
 * is drawn from the sorted catalog, the leader from the controller label (or
 * failing that the lobby's own record for this seat), and the sex from the
 * lobby, or failing that from a hash of the resolved leader — so two runs of
 * the same zero-argument command configure the same seat.
 */
import { Command, Options } from '@effect/cli';
import { Console, Effect, Layer } from 'effect';
import { playerError, type PlayError } from 'src/errors';
import { exitWith, type ExitCodeSignal } from 'src/exit';
import { V2_PROTOCOL_CARD } from 'src/render/join';
import {
  NOT_READIED_LINE,
  READY_INTENT,
  configureIntent,
  startJson,
  startingLine,
} from 'src/render/pregame';
import { render } from 'src/render/primitives';
import { renderDisposition } from 'src/render/receipt';
import type { BatchDisposition } from 'src/schema/batch';
import { field, type JsonObject } from 'src/schema/primitives';
import { fetchHealth } from 'src/commands/health.cmd';
import { liveTurnSeams, turnCtx } from 'src/commands/turn.cmd';
import { submitBatch } from 'src/services/batch';
import { persistBatchForAction } from 'src/services/batch-persist';
import { jsonRequested, printV2Json } from 'src/services/json-output';
import type { CompactAction } from 'src/services/legal-compact';
import { compactText } from 'src/services/orders';
import { drainLegal } from 'src/services/legal-drain';
import type { LegalCtx } from 'src/services/legal-query';
import { mirrorHealth } from 'src/services/mirror';
// U04's barrel does not re-export the page bridge (NOTES §U12.3).
import { mirrorPage } from 'src/services/mirror/update-page';
import {
  V2_LEADER_MAX_BYTES,
  cachedStyleName,
  checkPregameArguments,
  defaultArguments,
  defaultSex,
  orderReceiptOk,
  pregameCatalog,
  pregameChoice,
  pregameDefaultNation,
  pregameSeatDefaults,
  sanitizedLeader,
  type PregameAction,
  type PregameCtx,
  type PregameItem,
  type StartHooks,
  type SubmitOutcome,
} from 'src/services/pregame';
import { PrivateFs } from 'src/services/private-fs';
import { SessionStore, type Session } from 'src/services/session-store';
import { resolveKindAction } from 'src/services/turn-end';
import { V2Client } from 'src/services/v2-client';

// ---------------------------------------------------------------------------
// The cross-unit hooks
// ---------------------------------------------------------------------------

/**
 * How `start` reaches the units it does not own.
 *
 * Same shape as U05's `WaitHooksFor`: the services are captured here, inside
 * the command, where the CLI's Layer stack has already supplied them, so the
 * engine stays usable from a test that has no filesystem.
 */
export type StartHooksFor = (
  sessionPath: string,
  session: Session
) => Effect.Effect<StartHooks, never, PrivateFs | SessionStore | V2Client>;

/** `random.choice` — a uniform draw over the already-sorted catalog. */
const uniformChoice = (items: ReadonlyArray<PregameItem>): PregameItem => {
  const picked = items[Math.floor(Math.random() * items.length)];
  if (picked === undefined) {
    // `pregameDefaultNation` refuses an empty catalog before it ever draws, so
    // this is unreachable; it is here because the index type says it is not.
    throw new Error('the pregame catalog was empty at the draw');
  }
  return picked;
};

/**
 * `_resolve_kind_action` hands back `_compact_legal_action`'s object; U18 reads
 * three of its keys.
 *
 * The projection is total rather than validating: CPython indexes the same
 * three keys straight off the compacted dict, and every one of them is written
 * by `_compact_legal_action` from a descriptor the page validator already
 * accepted.  An empty `action_id` would be refused by `_persist_batch_for_action`
 * and an empty `kind` only names the action in a refusal sentence.
 */
const pregameActionOf = (compact: CompactAction): PregameAction => ({
  action_id: compactText(compact, 'action_id'),
  kind: compactText(compact, 'kind'),
  argument_schema: field(compact, 'argument_schema'),
});

/**
 * The real seams.
 *
 * Every one is another unit's landed entry point: U07's `mirrorPage`, U11's
 * `drainLegal`, U12's `resolveKindAction`, U13's `persistBatchForAction` and
 * `submitBatch`, U14's `renderDisposition` and `orderReceiptOk`.  Nothing here
 * re-implements a line of any of them; the indirection exists so every
 * ordering guarantee in this unit stays testable without a network.
 *
 * This is the swap PORT_MAP's U18 addendum asked for ("rebuild it as
 * `startCommandWith(realHooks)` when they land").  Until it happened, the
 * shipped `play start` refused at the first seam it reached — `configure` —
 * and so the one command that exists to claim a lobby seat could not claim one
 * at all.
 */
export const liveStartHooks: StartHooksFor = (sessionPath, session) =>
  Effect.gen(function* () {
    const files = yield* PrivateFs;
    const store = yield* SessionStore;
    const client = yield* V2Client;
    const provided = Layer.mergeAll(
      Layer.succeed(PrivateFs, files),
      Layer.succeed(SessionStore, store),
      Layer.succeed(V2Client, client)
    );
    const give = <A, E>(
      body: Effect.Effect<A, E, PrivateFs | SessionStore | V2Client>
    ): Effect.Effect<A, E> => Effect.provide(body, provided);
    const legalCtx: LegalCtx = { sessionPath, session };
    // `_resolve_kind_action` (client.py:6675) is U12's, and it is written
    // against a `TurnCtx`; `turn`'s own live seams build one from exactly the
    // services captured above, so `start` borrows the assembled context rather
    // than restating the cache-then-drain-then-cache dance.
    const turn = turnCtx(sessionPath, session, yield* liveTurnSeams(sessionPath, session));
    return {
      mirrorPage: (page, aliases, command) =>
        Effect.provideService(
          mirrorPage(sessionPath, page, command, { aliases }),
          PrivateFs,
          files
        ),
      choose: uniformChoice,
      receiptOk: orderReceiptOk,
      resolveKindAction: (kind, remedy) =>
        give(
          Effect.map(resolveKindAction(turn, kind, remedy), (resolved) =>
            pregameActionOf(resolved.compact)
          )
        ),
      drainLegal: () => give(Effect.asVoid(drainLegal(legalCtx))),
      persistBatchForAction: (actionId, argumentValues) =>
        give(persistBatchForAction(sessionPath, session, actionId, argumentValues)),
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
      renderDisposition: (disposition, intent) =>
        Effect.succeed(renderDisposition(disposition, intent)),
    };
  });

// ---------------------------------------------------------------------------
// Seat resolution
// ---------------------------------------------------------------------------

export interface StartOptions {
  readonly session: string;
  readonly nation: string;
  readonly leader: string;
  readonly style: string;
  readonly male: boolean;
  readonly female: boolean;
  readonly json: boolean;
}

/** What the flags asked for, already stripped and checked against each other. */
interface StartRequest {
  readonly nation: string;
  readonly leader: string;
  readonly style: string;
  readonly male: boolean;
  readonly female: boolean;
}

/** Every choice `start` had to make, made. */
export interface ResolvedSeat {
  readonly nation: PregameItem;
  readonly leader: string;
  readonly male: boolean;
  readonly styleId: string;
  readonly styleName: string;
}

const utf8Length = (text: string): number => new TextEncoder().encode(text).length;

const resolveStyle = (
  ctx: PregameCtx,
  wanted: string,
  chosen: PregameItem
): Effect.Effect<
  { readonly styleId: string; readonly styleName: string },
  PlayError,
  V2Client | SessionStore | PrivateFs
> =>
  Effect.gen(function* () {
    if (wanted !== '') {
      const styles = yield* pregameCatalog(ctx, 'pregame_styles');
      const entry = yield* pregameChoice(styles, wanted, 'style');
      return { styleId: entry.id, styleName: entry.name };
    }
    const styleId = chosen.default_style_id ?? '';
    if (styleId === '') {
      return yield* Effect.fail(
        playerError(
          `nation ${chosen.name} carries no default style; pass --style NAME ` +
            '(see `just state --section pregame_styles`)'
        )
      );
    }
    // The id is already what goes on the wire, but the SERVER only honours it
    // when this seat has read pregame_styles at the validating revision — the
    // fresh drain plants that overlay (see pregameCatalog).  The read also
    // resolves the label, with the mirror as the fallback.
    const styles = yield* pregameCatalog(ctx, 'pregame_styles');
    const fresh = styles.find((item) => item.id === styleId);
    return {
      styleId,
      styleName: fresh?.name ?? (yield* cachedStyleName(ctx.sessionPath, styleId)),
    };
  });

const resolveLeaderAndSex = (
  ctx: PregameCtx,
  request: StartRequest
): Effect.Effect<
  { readonly leader: string; readonly male: boolean },
  PlayError,
  V2Client | SessionStore | PrivateFs
> =>
  Effect.gen(function* () {
    const named =
      request.leader === '' ? sanitizedLeader(ctx.session.controllerLabel) : request.leader;
    const chose = request.male || request.female;
    if (named !== '' && chose) return { leader: named, male: request.male };
    // Only what is still missing costs a request.
    const defaults = yield* pregameSeatDefaults(ctx);
    const leader = named === '' ? sanitizedLeader(defaults.leader) : named;
    if (chose) return { leader, male: request.male };
    // The catalog's own default first; failing that a deterministic pick over
    // the resolved name.
    const sex = defaults.sex === '' ? defaultSex(leader) : defaults.sex;
    return { leader, male: sex === 'male' };
  });

/**
 * Resolve every choice the flags left open, spending a request only on what is
 * still missing: a named nation never draws, and a named sex never reads the
 * lobby overview.
 */
export const resolveSeat = (
  ctx: PregameCtx,
  request: StartRequest
): Effect.Effect<ResolvedSeat, PlayError, V2Client | SessionStore | PrivateFs> =>
  Effect.gen(function* () {
    const nations = yield* pregameCatalog(ctx, 'pregame_nations');
    const nation =
      request.nation === ''
        ? yield* pregameDefaultNation(nations, ctx.hooks.choose)
        : yield* pregameChoice(nations, request.nation, 'nation');
    const style = yield* resolveStyle(ctx, request.style, nation);
    const seat = yield* resolveLeaderAndSex(ctx, request);
    if (seat.leader === '') {
      return yield* Effect.fail(
        playerError(
          'no leader name could be resolved for this seat; pass ' +
            '`just start --leader NAME`'
        )
      );
    }
    return { nation, leader: seat.leader, male: seat.male, ...style };
  });

// ---------------------------------------------------------------------------
// The two submissions
// ---------------------------------------------------------------------------

const CONFIGURE_REMEDY =
  'this seat may already be ready -- run `just legal --kind pregame.set_ready ' +
  '--all` and withdraw readiness before configuring again';

const READY_REMEDY =
  'run `just legal --kind pregame.set_ready --all` once the lobby offers ' +
  'readiness, then `just batch` its action_id';

interface Submitted {
  readonly disposition: BatchDisposition;
  readonly exitCode: number;
  readonly lines: ReadonlyArray<string>;
}

/**
 * A refusal in the catalog-freshness class: the submitted nation/style id was
 * read at a revision the server no longer validates against.  Only this class
 * retries — every other refusal keeps its own remedy.
 */
const isCatalogFreshnessRefusal = (disposition: BatchDisposition): boolean => {
  const spelled = JSON.stringify([disposition.error, disposition.receipt]);
  return (
    spelled.includes('pregame_nation_unknown') ||
    spelled.includes('pregame_style_unknown') ||
    spelled.includes('is not one the pregame_nations section offers') ||
    spelled.includes('is not one of the IDs the pregame_styles section')
  );
};

/** Persist, submit, render — and let the warning reach stderr, never stdout. */
const submit = (
  ctx: PregameCtx,
  actionId: string,
  argumentValues: JsonObject,
  intent: string
): Effect.Effect<Submitted, PlayError> =>
  Effect.gen(function* () {
    const batchId = yield* ctx.hooks.persistBatchForAction(actionId, argumentValues);
    const outcome = yield* ctx.hooks.submitPersistedBatch(batchId);
    const lines = yield* ctx.hooks.renderDisposition(outcome.disposition, intent);
    if (outcome.warning !== null) yield* Console.error(outcome.warning);
    return { disposition: outcome.disposition, exitCode: outcome.exitCode, lines };
  });

/**
 * `_default_arguments` narrowed to the one shape readiness may have.
 *
 * A `pregame.set_ready` whose only legal `ready` value is `false` is the
 * *withdrawal* action: submitting it would un-ready the seat that just
 * configured itself, so `start` refuses rather than issue it.
 */
const readyArgumentsOf = (ready: PregameAction): JsonObject | null => {
  const filled = defaultArguments(ready);
  return filled !== null && filled['ready'] === true ? filled : null;
};

// ---------------------------------------------------------------------------
// The command body
// ---------------------------------------------------------------------------

interface StartResult {
  readonly seat: ResolvedSeat;
  readonly lines: ReadonlyArray<string>;
  readonly records: ReadonlyArray<BatchDisposition>;
  readonly exitCode: number;
}

const configureAndReady = (
  ctx: PregameCtx,
  request: StartRequest
): Effect.Effect<StartResult, PlayError, V2Client | SessionStore | PrivateFs> =>
  Effect.gen(function* () {
    const health = yield* fetchHealth(ctx.session);
    // `_mirror_health` (client.py:3062-3072) hardcodes `commands=V2_PROTOCOL_CARD`
    // on every call, so the `state/header.txt` a `just start` writes ends in the
    // full protocol card, not `updateFromHealth`'s five-line `DEFAULT_COMMAND_CARD`
    // fallback.  `just show` prints that file verbatim and it is on the offline
    // byte-diff oracle's read-only path (PLAN §The oracle item 2), so the argument
    // is load-bearing rather than decorative (NOTES §16.9 under U18).
    yield* mirrorHealth(ctx.sessionPath, health, 'start', { commands: V2_PROTOCOL_CARD });
    if (health.game_state !== 'lobby') {
      return yield* Effect.fail(
        playerError(
          'just start configures a lobby seat; this game is ' +
            `${String(health.game_state)} -- run \`just turn\``
        )
      );
    }
    // The lobby's native revision advances in the background, and the server
    // honours configure ids only when their sections were read at the
    // validating revision — so a refused configure whose refusal names that
    // freshness class is re-resolved from fresh catalogs and retried, bounded.
    // (Live finding, game_Dn9l…: without this, two seats retrying by hand
    // livelock each other out of the lobby entirely.)
    const CONFIGURE_ATTEMPTS = 3;
    let seat = yield* resolveSeat(ctx, request);
    const lines: string[] = [];
    const records: BatchDisposition[] = [];
    let configured: Submitted;
    for (let attempt = 1; ; attempt += 1) {
      lines.push(startingLine(seat.nation.name, seat.leader, seat.male, seat.styleName));
      const argumentValues: JsonObject = {
        nation_id: seat.nation.id,
        leader_name: seat.leader,
        is_male: seat.male,
        style_id: seat.styleId,
      };
      const configure = yield* ctx.hooks.resolveKindAction(
        'pregame.configure',
        CONFIGURE_REMEDY
      );
      yield* checkPregameArguments(configure, argumentValues);
      configured = yield* submit(
        ctx,
        configure.action_id,
        argumentValues,
        configureIntent(seat.nation.name, seat.leader, seat.male)
      );
      lines.push(...configured.lines);
      records.push(configured.disposition);
      if (
        ctx.hooks.receiptOk(configured.disposition) ||
        attempt >= CONFIGURE_ATTEMPTS ||
        !isCatalogFreshnessRefusal(configured.disposition)
      ) {
        break;
      }
      lines.push(
        'the lobby catalog moved under the configure; re-reading and ' +
          `retrying (attempt ${attempt + 1} of ${CONFIGURE_ATTEMPTS})`
      );
      seat = yield* resolveSeat(ctx, request);
    }

    if (!ctx.hooks.receiptOk(configured.disposition)) {
      lines.push(NOT_READIED_LINE);
      return { seat, lines, records, exitCode: configured.exitCode };
    }
    // Configuring the seat bumps the revision, so the readiness capability
    // enumerated before it is now expired: re-enumerate before naming it,
    // exactly as the doc requires.
    yield* ctx.hooks.drainLegal();
    const ready = yield* ctx.hooks.resolveKindAction('pregame.set_ready', READY_REMEDY);
    const readyArguments = readyArgumentsOf(ready);
    if (readyArguments === null) {
      return yield* Effect.fail(
        playerError(
          'the enumerated pregame.set_ready would withdraw readiness rather ' +
            'than set it; this seat is already ready'
        )
      );
    }
    const readied = yield* submit(ctx, ready.action_id, readyArguments, READY_INTENT);
    lines.push(...readied.lines);
    records.push(readied.disposition);
    return { seat, lines, records, exitCode: readied.exitCode };
  });

export const runStart = (
  options: StartOptions,
  makeHooks: StartHooksFor = liveStartHooks
): Effect.Effect<
  void,
  PlayError | ExitCodeSignal,
  SessionStore | V2Client | PrivateFs
> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const loaded = yield* store.resolveV2(options.session);
    // Both refusals precede the first request: an impossible seat is never
    // half-configured.
    if (options.male && options.female) {
      return yield* Effect.fail(
        playerError('just start takes at most one of --male or --female')
      );
    }
    const request: StartRequest = {
      nation: options.nation.trim(),
      leader: options.leader.trim(),
      style: options.style.trim(),
      male: options.male,
      female: options.female,
    };
    if (utf8Length(request.leader) > V2_LEADER_MAX_BYTES) {
      return yield* Effect.fail(
        playerError(`--leader must be at most ${V2_LEADER_MAX_BYTES} UTF-8 bytes`)
      );
    }
    const ctx: PregameCtx = {
      sessionPath: loaded.path,
      session: loaded.session,
      hooks: yield* makeHooks(loaded.path, loaded.session),
    };
    const result = yield* store.withRequestLock(
      loaded.path,
      configureAndReady(ctx, request)
    );
    if (jsonRequested('start', options.json)) {
      yield* printV2Json(
        startJson({
          nation: result.seat.nation.name,
          leader: result.seat.leader,
          male: result.seat.male,
          styleId: result.seat.styleId,
          dispositions: result.records,
        })
      );
    } else {
      yield* render(result.lines);
    }
    // CPython returns `_submit_persisted_batch`'s status.  A non-zero one has
    // already printed its own stderr warning, so the port signals it with
    // `exitWith` — which prints nothing — rather than a second `error: …`.
    if (result.exitCode !== 0) {
      return yield* Effect.fail(exitWith(result.exitCode));
    }
    return;
  });

// ---------------------------------------------------------------------------
// The flag surface
// ---------------------------------------------------------------------------

const sessionOption = Options.text('session').pipe(
  Options.withDefault(''),
  Options.withDescription('the private session file this command acts against')
);

const textOption = (name: string, description: string) =>
  Options.text(name).pipe(Options.withDefault(''), Options.withDescription(description));

const jsonOption = Options.boolean('json').pipe(
  Options.withDescription('print the full-fidelity JSON payload instead of text')
);

/** Build the command against a given set of cross-unit hooks. */
export const startCommandWith = (makeHooks: StartHooksFor) =>
  Command.make(
    'start',
    {
      session: sessionOption,
      nation: textOption('nation', 'the nation to play, by catalog name'),
      leader: textOption('leader', "this seat's leader name"),
      style: textOption('style', 'the city style, by catalog name'),
      male: Options.boolean('male').pipe(Options.withDescription('pick a male leader')),
      female: Options.boolean('female').pipe(
        Options.withDescription('pick a female leader')
      ),
      json: jsonOption,
    },
    (options) => runStart(options, makeHooks)
  );

export const startCommand = startCommandWith(liveStartHooks);
