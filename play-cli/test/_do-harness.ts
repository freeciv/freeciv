/**
 * The `do` bench: a real workspace, a real `.v2-state`, and scripted stand-ins
 * for the units `do` composes.
 *
 * U16's job is orchestration — what is printed, in what order, with what exit
 * status, and what reaches `state/receipts.log`.  Every other unit it needs
 * (U11 legal, U12 turn, U13 batch, U14 receipt, U15 orders) is reached through
 * `DoHooks`, so this file supplies a small, deterministic implementation of
 * that contract.  Two things are deliberately *not* faked:
 *
 * - the session store and `.v2-state`, so `rememberPage` (U03) does the real
 *   `aN`/`uN` alias numbering and the real revision-bump cache wipe; and
 * - the row table, which is built with core's `table` primitive, so the
 *   refusal's inline options are aligned the way `just legal` aligns them.
 *
 * Named with a leading underscore so `bun test`'s `*.test.ts` glob does not
 * pick it up (PORT_MAP §7.1 records the file).
 */
import * as path from 'node:path';
import { Effect, Layer } from 'effect';
import { FULL_CONTROL_V2, V2_PAGE_MAX_ITEMS } from 'src/constants';
import { playerError, type PlayError } from 'src/errors';
import type { BatchDisposition } from 'src/schema/batch';
import { decodeLegalPage } from 'src/schema/legal-page';
import type { PageScope } from 'src/schema/page';
import { field, isJsonObject, type JsonObject, type JsonValue } from 'src/schema/primitives';
import type { ReceiptState } from 'src/schema/receipt';
import type { Revision } from 'src/schema/revision';
import { revisionLabel, scalar, table } from 'src/render/primitives';
import {
  runDo,
  type AwaitBriefOutcome,
  type DoArguments,
  type DoHooks,
  type DoHooksFor,
  type OrderOutcome,
  type PhaseEndOutcome,
  type ResolvedOrder,
  type SubmitOutcome,
} from 'src/commands/do.cmd';
import {
  aliasMap,
  rememberDrainedActor,
  rememberPage,
  rememberReceipt,
} from 'src/services/aliases';
import { PrivateFs, type PrivateFsApi } from 'src/services/private-fs';
import { httpFor } from 'src/services/http';
import { V2Client, v2ClientFor } from 'src/services/v2-client';
import {
  SessionStore,
  emptyV2ClientState,
  sessionStoreFor,
  type Session,
  type SessionStoreApi,
  type V2ClientState,
} from 'src/services/session-store';
import {
  FIXTURE_AGENT_ID,
  FIXTURE_GAME_ID,
  fakeFetch,
  scratchWorkspace,
  sessionFile,
  type Scratch,
} from 'test/_fixtures';
import { legalPagePayload } from 'test/_fixtures/wire';

// ---------------------------------------------------------------------------
// The board
// ---------------------------------------------------------------------------

export const UNIT_ONE = `unit_${'a'.repeat(32)}`;
export const UNIT_TWO = `unit_${'b'.repeat(32)}`;
export const CITY_ONE = `city_${'c'.repeat(32)}`;

export const rev = (revision: number, turn = 3): Revision => ({
  turn,
  revision,
  state_token: `token_${turn}_${revision}`,
});

/** One capability, in the shape the bench's catalogs are written in. */
export interface FakeAction {
  readonly actionId: string;
  readonly actorId: string;
  readonly kind: string;
  readonly operation: string;
  readonly label: string;
  /** The target tile, when the capability names one. */
  readonly x?: number;
  readonly y?: number;
  /** A required string argument the order's trailing word binds. */
  readonly argument?: string;
}

export const move = (actionId: string, actorId: string, x: number, y: number): FakeAction => ({
  actionId,
  actorId,
  kind: 'unit.order',
  operation: 'move',
  label: 'Move',
  x,
  y,
});

export const foundCity = (actionId: string, actorId: string, x: number, y: number): FakeAction => ({
  actionId,
  actorId,
  kind: 'unit.found_city',
  operation: 'found_city',
  label: 'Found city',
  x,
  y,
  argument: 'city_name',
});

export const buildCity = (actionId: string, actorId: string): FakeAction => ({
  actionId,
  actorId,
  kind: 'city.set_production',
  operation: 'set_production',
  label: 'Set production',
  argument: 'item',
});

/**
 * A capability with **no `subject.actor` at all**.
 *
 * `actor` is not a declared property of `LegalActionSubject`
 * (`full-control-v2.openapi.json`), so the whole phase/research/government/
 * player family arrives this way and `_order_actor` resolves them with
 * `actor_id == ""`.  They live in the *global* catalog — the one
 * `_drain_legal_unlocked(actor_id="")` fetches — which is why `command_do`
 * guards on `if resolved["actor_id"]` in four places and why `_refresh_orders`
 * must not skip the empty id.
 */
export const actorless = (
  actionId: string,
  kind: string,
  operation: string,
  label: string
): FakeAction => ({ actionId, actorId: '', kind, operation, label });

/** `phase.end`, the actorless capability `do "…; end"` names. */
export const phaseEndAction = (actionId: string): FakeAction =>
  actorless(actionId, 'phase.end', 'end', 'End phase');

const targetOf = (action: FakeAction): JsonValue =>
  action.x === undefined || action.y === undefined
    ? null
    : { type: 'tile', id: `tile_${'0'.repeat(32)}`, x: action.x, y: action.y };

export const targetKey = (action: FakeAction): string =>
  action.x === undefined || action.y === undefined ? '' : `${action.x},${action.y}`;

const schemaOf = (action: FakeAction): JsonObject =>
  action.argument === undefined
    ? { type: 'object', properties: {} }
    : {
        type: 'object',
        properties: { [action.argument]: { type: 'string' } },
        required: [action.argument],
      };

/** The wire descriptor one `FakeAction` is served as. */
export const descriptorOf = (action: FakeAction, revision: Revision): JsonObject => ({
  action_id: action.actionId,
  kind: action.kind,
  label: action.label,
  subject: {
    operation: action.operation,
    // An actorless capability carries no actor key's worth of information at
    // all; `_order_actor` reads that back as "".
    actor:
      action.actorId === ''
        ? null
        : { id: action.actorId, type: action.actorId.split('_')[0] ?? 'unit', name: 'Warriors' },
    target: targetOf(action),
    variant: null,
    consuming: false,
    legality: 'legal',
    probability: { kind: 'exact', minimum_percent: 100, maximum_percent: 100 },
  },
  arguments_schema: schemaOf(action),
  state_revision: { ...revision },
});

/** A global drain's page carries no `scope` key; an actor's page does. */
const scopeOf = (actorId: string): JsonObject | undefined =>
  actorId === ''
    ? undefined
    : { actor_id: actorId, actor_type: actorId.split('_')[0] ?? 'unit' };

const catalogId = ((): (() => string) => {
  let counter = 0;
  return () => {
    counter += 1;
    return `catalog_${String(counter).padStart(32, '0')}`;
  };
})();

// ---------------------------------------------------------------------------
// The scripted world
// ---------------------------------------------------------------------------

export interface ReceiptScript {
  readonly state: ReceiptState;
  readonly revision: Revision;
}

/** Everything the bench's stand-ins read; mutate it before running `do`. */
export interface World {
  /** What a drain of each actor would return, and at which revision. */
  catalogs: Map<string, ReadonlyArray<FakeAction>>;
  revision: Revision;
  /** Per-request artificial latency in milliseconds, keyed by request kind. */
  latency: (kind: string, subject: string) => number;
  /** Every request the run made, in completion order. */
  readonly trace: Array<string>;
  /** Every request the run made, in the order it was *issued*. */
  readonly issued: Array<string>;
  /** The receipt the Nth (0-based) submitted batch gets. */
  receipt: (index: number, batchId: string) => ReceiptScript;
  /** A submit that fails outright instead of receipting. */
  submitFailure: (index: number, batchId: string) => PlayError | null;
  /** A persist that fails before anything is sent. */
  persistFailure: (index: number, actionId: string) => PlayError | null;
  /** A drain that fails. */
  drainFailure: (actorId: string) => PlayError | null;
  /** Notes `_refresh_stale_order_aliases` would have produced. */
  aliasNotes: ReadonlyArray<string>;
  /** `_phase_end_locked`'s outcome. */
  phaseEnd: () => Effect.Effect<PhaseEndOutcome, PlayError>;
  /** `_await_and_brief_locked`'s outcome. */
  awaitBrief: (options: {
    readonly brief: boolean;
    readonly prelude: Array<string> | null;
  }) => Effect.Effect<AwaitBriefOutcome, PlayError>;
  /** `_next_focus_line`. */
  focus: (state: V2ClientState, ordered: ReadonlySet<string>) => string;
  /** Kills the process the moment the Nth (1-based) receipt has been recorded. */
  killAfterReceipt: number;
}

/** One disposition, for scripting `phaseEndLocked` and the `--json` composite. */
export const dispositionOf = (
  batchId: string,
  state: ReceiptState,
  revision: Revision
): BatchDisposition => ({
  schema_version: 2,
  control_protocol: FULL_CONTROL_V2,
  game_id: FIXTURE_GAME_ID,
  agent_id: FIXTURE_AGENT_ID,
  batch_id: batchId,
  disposition: 'receipt_first',
  receipt: {
    schema_version: 2,
    control_protocol: FULL_CONTROL_V2,
    game_id: FIXTURE_GAME_ID,
    agent_id: FIXTURE_AGENT_ID,
    batch_id: batchId,
    receipt_state: state,
    idempotent: false,
    state_revision: revision,
    error: null,
    observation: null,
  },
  error: null,
});

export class SimulatedKill extends Error {}

export const world = (): World => ({
  catalogs: new Map(),
  revision: rev(7),
  latency: () => 0,
  trace: [],
  issued: [],
  receipt: () => ({ state: 'applied', revision: rev(7) }),
  submitFailure: () => null,
  persistFailure: () => null,
  drainFailure: () => null,
  aliasNotes: [],
  phaseEnd: () => Effect.fail(playerError('no phase.end capability')),
  awaitBrief: () => Effect.fail(playerError('no wake')),
  focus: () => '',
  killAfterReceipt: 0,
});

// ---------------------------------------------------------------------------
// The bench
// ---------------------------------------------------------------------------

export interface Bench {
  readonly scratch: Scratch;
  readonly sessionPath: string;
  readonly session: Session;
  readonly store: SessionStoreApi;
  readonly layer: Layer.Layer<PrivateFs | SessionStore | V2Client>;
  readonly world: World;
  readonly hooks: DoHooksFor;
  /** Drain one actor into `.v2-state` up front, as `just legal --all` would. */
  readonly seed: (actorId: string, actions: ReadonlyArray<FakeAction>, at?: Revision) => void;
  /**
   * Retire every cached capability at a newer revision, exactly as a receipt
   * does — the entity aliases survive, the catalogs and the drain record do
   * not.  That is the state a first `do` of a turn is really in.
   */
  readonly bump: (to: Revision) => void;
  readonly readState: () => V2ClientState;
}

const stateSchema = {
  empty: emptyV2ClientState,
  validate: () => Effect.void,
  cursorExpired: (): boolean => false,
};

export interface BenchOptions {
  /**
   * Wrap the workspace's `PrivateFs` before it is put in the layer.
   *
   * The one thing a test cannot provoke portably is a *defect* out of the file
   * layer — `PrivateFsApi.appendText` runs `fstat`/`fchmod`/`write` in a bare
   * `try/finally`, so a real `ENOSPC` throws rather than failing, and no
   * temporary directory can be made to run out of space on demand.
   */
  readonly files?: (api: PrivateFsApi) => PrivateFsApi;
}

export const bench = (options: BenchOptions = {}): Bench => {
  const scratch = scratchWorkspace();
  const sessionPath = path.join(scratch.workspace.stateRoot, 'session-codex-gpt-5.6-sol.json');
  Effect.runSync(scratch.files.writeJson(sessionPath, sessionFile()));
  // The store captures its file api and re-provides it inside the request
  // lock, so the wrapper has to reach it too or `do`'s whole locked body would
  // silently run against the unwrapped one.
  const files = options.files === undefined ? scratch.files : options.files(scratch.files);
  const store = sessionStoreFor(scratch.workspace, files, stateSchema, {});
  const loaded = Effect.runSync(store.resolveV2(sessionPath));
  const session = loaded.session;
  const state = world();
  // The bench never reaches the wire — every hook that would is scripted — so
  // the client is present only to satisfy `runDo`'s requirement, and refuses
  // loudly if anything ever does reach it.
  const refusingFetch = fakeFetch([]);
  const layer = Layer.mergeAll(
    Layer.succeed(SessionStore, store),
    Layer.succeed(V2Client, v2ClientFor(httpFor(refusingFetch))),
    scratch.layer,
    // Last: `Layer.mergeAll` folds left to right and the later context wins, so
    // this is what overrides `scratch.layer`'s own `PrivateFs`.
    Layer.succeed(PrivateFs, files)
  );

  /**
   * One whole catalog, served the way the wire serves it: `V2_PAGE_MAX_ITEMS`
   * descriptors a page, cursors followed to the end, and only the terminal
   * page promoting the accumulation into `.v2-state`.  A single-page shortcut
   * would skip the staged-catalog path that `refusedActorOptions` depends on.
   */
  const ingest = (
    actorId: string,
    actions: ReadonlyArray<FakeAction>,
    at: Revision
  ): Effect.Effect<Revision, PlayError, PrivateFs | SessionStore> =>
    Effect.gen(function* () {
      const all = actions.map((action) => descriptorOf(action, at));
      const identifier = catalogId();
      const pages = Math.max(1, Math.ceil(all.length / V2_PAGE_MAX_ITEMS));
      for (let index = 0; index < pages; index += 1) {
        const items = all.slice(index * V2_PAGE_MAX_ITEMS, (index + 1) * V2_PAGE_MAX_ITEMS);
        const last = index + 1 === pages;
        const cursor = last ? null : `cursor_${String(index + 1).padStart(32, 'c')}`;
        const scope = scopeOf(actorId);
        const payload = legalPagePayload(items, {
          state_revision: { ...at },
          page: {
            section: 'legal_actions',
            items,
            total_items: all.length,
            next_cursor: cursor,
            cursor_expires_at: cursor === null ? null : '2099-01-01T00:00:00Z',
            // A global drain's page carries none of the scoped-catalog keys:
            // the envelope's *field set* is validated, and `scope`,
            // `catalog_id` and `catalog_complete` only exist together
            // (`decodePageShape`).  So the actorless catalog is learned
            // descriptor by descriptor, exactly as the wire serves it.
            ...(scope === undefined
              ? {}
              : { scope, catalog_id: identifier, catalog_complete: last }),
          },
        });
        const page = yield* Effect.mapError(decodeLegalPage(payload, session), (drift) =>
          playerError(drift.message)
        );
        yield* rememberPage(sessionPath, session, { legal: true, page });
      }
      const current = yield* store.readState(sessionPath, session);
      const next = yield* rememberDrainedActor(current, actorId);
      yield* store.writeState(sessionPath, next);
      return at;
    });

  const seed = (actorId: string, actions: ReadonlyArray<FakeAction>, at?: Revision): void => {
    Effect.runSync(Effect.provide(ingest(actorId, actions, at ?? state.revision), layer));
  };

  const readState = (): V2ClientState => Effect.runSync(store.readState(sessionPath, session));

  const bump = (to: Revision): void => {
    const current = readState();
    Effect.runSync(
      store.writeState(sessionPath, {
        ...current,
        last_revision: to,
        actions: {},
        pending_catalogs: {},
        drained_actors: [],
      })
    );
    state.revision = to;
  };

  return {
    scratch,
    sessionPath,
    session,
    store,
    layer,
    world: state,
    hooks: makeHooks(sessionPath, session, store, state, ingest),
    seed,
    bump,
    readState,
  };
};

// ---------------------------------------------------------------------------
// The stand-ins
// ---------------------------------------------------------------------------

const sameRevision = (left: JsonValue, right: Revision): boolean =>
  isJsonObject(left) &&
  field(left, 'turn') === right.turn &&
  field(left, 'revision') === right.revision &&
  field(left, 'state_token') === right.state_token;

const textOf = (value: JsonValue): string => (typeof value === 'string' ? value : '');

/** `_order_actor` — `""` whenever the subject names no actor at all. */
const descriptorActor = (descriptor: JsonObject): string => {
  const subject = field(descriptor, 'subject');
  const actor = isJsonObject(subject) ? field(subject, 'actor') : null;
  return isJsonObject(actor) ? textOf(field(actor, 'id')) : '';
};

/**
 * Every cached descriptor of one actor at the newest revision.
 *
 * Not `cachedActorCatalog`: that answers `null` for an actorless subject and
 * therefore matches nothing for `actorId === ''`, while `_order_actor` reads
 * the same subject as `""`.  The bench needs the `_order_*` spelling, because
 * the actorless order is the case under test.
 */
const cachedFor = (state: V2ClientState, actorId: string): ReadonlyArray<JsonObject> => {
  const revision = state.last_revision;
  if (revision === null) return [];
  return Object.values(state.actions).filter(
    (descriptor): descriptor is JsonObject =>
      isJsonObject(descriptor) &&
      descriptorActor(descriptor) === actorId &&
      sameRevision(field(descriptor, 'state_revision'), revision)
  );
};

const operationOf = (descriptor: JsonObject): string => {
  const subject = field(descriptor, 'subject');
  return isJsonObject(subject) ? textOf(field(subject, 'operation')) : '';
};

const targetTextOf = (descriptor: JsonObject): string => {
  const subject = field(descriptor, 'subject');
  const target = isJsonObject(subject) ? field(subject, 'target') : null;
  if (!isJsonObject(target)) return '';
  return `${scalar(field(target, 'x'))},${scalar(field(target, 'y'))}`;
};

const requiredArgument = (descriptor: JsonObject): string => {
  const schema = field(descriptor, 'arguments_schema');
  if (!isJsonObject(schema)) return '';
  const required = field(schema, 'required');
  if (!Array.isArray(required)) return '';
  const first = required[0];
  return typeof first === 'string' ? first : '';
};

const resolutionOf = (descriptor: JsonObject, order: string, args: JsonObject): ResolvedOrder => ({
  order,
  action_id: textOf(field(descriptor, 'action_id')),
  kind: textOf(field(descriptor, 'kind')),
  operation: operationOf(descriptor),
  label: textOf(field(descriptor, 'label')),
  actor_id: descriptorActor(descriptor),
  target_key: targetTextOf(descriptor),
  arguments: args,
});

/** `_expand_alias` over the real entity-alias table `rememberPage` wrote. */
const expandActor = (state: V2ClientState, token: string): string => {
  const identifier = state.entity_aliases[token];
  return typeof identifier === 'string' ? identifier : '';
};

const makeHooks =
  (
    sessionPath: string,
    session: Session,
    store: SessionStoreApi,
    state: World,
    ingest: (
      actorId: string,
      actions: ReadonlyArray<FakeAction>,
      at: Revision
    ) => Effect.Effect<Revision, PlayError, PrivateFs | SessionStore>
  ): DoHooksFor =>
  () =>
    Effect.gen(function* () {
      const files = yield* PrivateFs;
      const provided = Layer.merge(
        Layer.succeed(PrivateFs, files),
        Layer.succeed(SessionStore, store)
      );
      let submitted = 0;
      let batches = 0;

      const delay = (kind: string, subject: string): Effect.Effect<void> => {
        state.issued.push(`${kind}:${subject}`);
        const millis = state.latency(kind, subject);
        return Effect.flatMap(millis === 0 ? Effect.void : Effect.sleep(`${millis} millis`), () =>
          Effect.sync(() => {
            state.trace.push(`${kind}:${subject}`);
          })
        );
      };

      const outcomeOf = (current: V2ClientState, text: string): OrderOutcome => {
        const tokens = text.split(/\s+/).filter((part) => part !== '');
        const first = tokens[0] ?? '';
        const actorId = expandActor(current, first);
        if (actorId === '') {
          // No actor alias, so the order may still name an *actorless*
          // capability directly — `end`, `research …`, the player family.
          // Those resolve with `actor_id == ""`, live in the global catalog,
          // and are the case `_refresh_orders` re-drains with `actor_id=""`.
          const global = cachedFor(current, '').filter(
            (descriptor) => operationOf(descriptor) === first
          );
          const only = global.length === 1 ? global[0] : undefined;
          if (only !== undefined) {
            const wanted = requiredArgument(only);
            const rest = tokens.slice(1);
            const bound: JsonObject =
              wanted === '' || rest.length === 0 ? {} : { [wanted]: rest[rest.length - 1] ?? '' };
            return { text, resolved: resolutionOf(only, text, bound), reason: '', actor: '' };
          }
          return {
            text,
            resolved: null,
            reason: `${first} is not a unit, city, or player this seat can act as`,
            actor: '',
          };
        }
        const verb = tokens[1] ?? '';
        if (verb === '') {
          return {
            text,
            resolved: null,
            reason: `${first} names an actor but no verb; write \`${first} <verb> [arguments]\``,
            actor: actorId,
          };
        }
        const pool = cachedFor(current, actorId).filter(
          (descriptor) => operationOf(descriptor) === verb
        );
        if (pool.length === 0) {
          return {
            text,
            resolved: null,
            reason: `no cached action for ${first} answers to ${verb}`,
            actor: actorId,
          };
        }
        const rest = tokens.slice(2);
        const narrowed =
          pool.length > 1 && rest.length > 0
            ? pool.filter((descriptor) => targetTextOf(descriptor) === rest[0])
            : pool;
        const chosen = (narrowed.length === 1 ? narrowed[0] : pool[0]);
        if (chosen === undefined) {
          return { text, resolved: null, reason: `${verb} is ambiguous`, actor: actorId };
        }
        const wanted = requiredArgument(chosen);
        const bound: JsonObject =
          wanted === '' || rest.length === 0 ? {} : { [wanted]: rest[rest.length - 1] ?? '' };
        return { text, resolved: resolutionOf(chosen, text, bound), reason: '', actor: actorId };
      };

      const hooks: DoHooks = {
        orderOutcomes: (current, orders) =>
          Effect.succeed(orders.map((text) => outcomeOf(current, text))),

        orderFetchTargets: (current, outcomes) => {
          const drained = new Set(current.drained_actors.map((value) => scalar(value)));
          const wanted: Array<string> = [];
          for (const outcome of outcomes) {
            if (outcome.resolved !== null) continue;
            if (outcome.actor === '' || drained.has(outcome.actor)) continue;
            if (wanted.includes(outcome.actor)) continue;
            wanted.push(outcome.actor);
          }
          return wanted;
        },

        unresolvedReport: (current, outcomes) =>
          Effect.gen(function* () {
            const revision = current.last_revision;
            const label = revision === null ? 'no revision' : revisionLabel(revision);
            const failed = outcomes.filter((outcome) => outcome.resolved === null).length;
            const aliases = yield* aliasMap(current);
            const lines = [
              `${failed} of ${outcomes.length} orders did not resolve against the ` +
                `cached ${label} catalog; nothing was sent`,
            ];
            const remedies: Array<string> = [];
            outcomes.forEach((outcome, index) => {
              const number = index + 1;
              if (outcome.resolved !== null) {
                lines.push(
                  `  ${number} resolved    ${outcome.text}  ->  ` +
                    `${outcome.resolved.kind} ${outcome.resolved.label}`
                );
                return;
              }
              lines.push(`  ${number} unresolved  ${outcome.text}  --  ${outcome.reason}`);
              const named = aliases[outcome.actor] ?? outcome.actor;
              const remedy =
                named === ''
                  ? 'just legal --all'
                  : `just legal --actor_id ${named} --all`;
              if (!remedies.includes(remedy)) remedies.push(remedy);
            });
            for (const remedy of remedies) lines.push(`enumerate with: ${remedy}`);
            return lines;
          }),

        refreshStaleOrderAliases: (current) =>
          Effect.succeed({ state: current, notes: state.aliasNotes }),

        rebindOrder: (current, resolved) => {
          // eslint-disable-next-line no-unused-expressions
          const matches = cachedFor(current, resolved.actor_id).filter(
            (descriptor) =>
              textOf(field(descriptor, 'kind')) === resolved.kind &&
              operationOf(descriptor) === resolved.operation &&
              targetTextOf(descriptor) === resolved.target_key &&
              textOf(field(descriptor, 'label')) === resolved.label
          );
          const only = matches.length === 1 ? matches[0] : undefined;
          return Effect.succeed(
            only === undefined ? null : resolutionOf(only, resolved.order, resolved.arguments)
          );
        },

        drainLegalUnlocked: (actorId, gate) =>
          Effect.gen(function* () {
            // The fetch is outside the gate; the ingest is inside it, exactly
            // as U11's `readLegalPage` places `LegalCtx.gate`.
            yield* delay('drain', actorId);
            const failure = state.drainFailure(actorId);
            if (failure !== null) return yield* Effect.fail(failure);
            const actions = state.catalogs.get(actorId) ?? [];
            return yield* gate(
              Effect.provide(ingest(actorId, actions, state.revision), provided)
            );
          }),

        persistBatchForAction: (actionId) =>
          Effect.gen(function* () {
            batches += 1;
            // The simulated `SIGKILL`: the process dies between one order's
            // receipt and the next order's batch, which is exactly the window
            // the streamed ledger exists for.
            if (state.killAfterReceipt !== 0 && batches > state.killAfterReceipt) {
              return yield* Effect.die(new SimulatedKill('SIGKILL'));
            }
            const failure = state.persistFailure(batches - 1, actionId);
            if (failure !== null) return yield* Effect.fail(failure);
            return `batch_${String(batches).padStart(8, '0')}`;
          }),

        submitPersistedBatch: (batchId) =>
          Effect.gen(function* () {
            const index = submitted;
            submitted += 1;
            yield* delay('submit', batchId);
            const failure = state.submitFailure(index, batchId);
            if (failure !== null) return yield* Effect.fail(failure);
            const script = state.receipt(index, batchId);
            const disposition: BatchDisposition = {
              schema_version: 2,
              control_protocol: FULL_CONTROL_V2,
              game_id: session.gameId,
              agent_id: session.agentId,
              batch_id: batchId,
              disposition: 'receipt_first',
              receipt: {
                schema_version: 2,
                control_protocol: FULL_CONTROL_V2,
                game_id: session.gameId,
                agent_id: session.agentId,
                batch_id: batchId,
                receipt_state: script.state,
                idempotent: false,
                state_revision: script.revision,
                error: null,
                observation: null,
              },
              error: null,
            };
            // `_submit_persisted_batch` ends in `_remember_receipt`, which is
            // what retires every cached capability when the receipt carries a
            // newer revision.  The real one runs here, so the refusal path's
            // "is this actor still drained" question is answered honestly.
            const receipt = disposition.receipt;
            if (receipt !== null) {
              yield* Effect.provide(rememberReceipt(sessionPath, session, receipt), provided);
            }
            if (script.revision.revision !== state.revision.revision) {
              state.revision = script.revision;
            }
            const outcome: SubmitOutcome = {
              disposition,
              warning: null,
              exitCode: script.state === 'applied' || script.state === 'accepted' ? 0 : 2,
            };
            return outcome;
          }),

        phaseEndLocked: () => state.phaseEnd(),
        awaitAndBriefLocked: (options) => state.awaitBrief(options),
        nextFocusLine: (current, ordered) => Effect.succeed(state.focus(current, ordered)),

        compactLegalAction: (descriptor) =>
          Effect.succeed({
            action_id: field(descriptor, 'action_id'),
            kind: field(descriptor, 'kind'),
            label: field(descriptor, 'label'),
            operation: operationOf(descriptor),
            target: targetTextOf(descriptor),
          }),

        legalRows: (compacts, _scope: PageScope | null, aliases) =>
          Effect.succeed(
            table(
              compacts.map((compact, index) => {
                const actionId = textOf(field(compact, 'action_id'));
                const alias = aliases?.[actionId] ?? `a${index + 1}`;
                const target = textOf(field(compact, 'target'));
                return [
                  alias,
                  `${textOf(field(compact, 'kind'))}/${textOf(field(compact, 'operation'))}`,
                  textOf(field(compact, 'label')),
                  target === '' ? '-' : `T(${target})`,
                ];
              })
            )
          ),
      };
      return hooks;
    });

// ---------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------

export const doArgs = (orders: string, overrides: Partial<DoArguments> = {}): DoArguments => ({
  session: '',
  orders,
  positionalOrders: [],
  continueOnError: false,
  noRefresh: false,
  endPhase: false,
  awaitPhase: false,
  brief: false,
  json: false,
  wait: { waitS: 120, pollS: 1, until: 'phase' },
  ...overrides,
});

export interface Captured {
  readonly out: ReadonlyArray<string>;
  readonly err: ReadonlyArray<string>;
  /** The exit status `cli-main` would have produced: 0, or 2 for a refusal. */
  readonly code: number;
  /** The `error: …` sentence a refusal put on stderr; `''` on success. */
  readonly error: string;
  /** True when the run died the way a `SIGKILL` does, mid-command. */
  readonly killed: boolean;
}

const property = (value: unknown, key: string): unknown =>
  typeof value === 'object' && value !== null ? Reflect.get(value, key) : undefined;

/**
 * Run one `do` with stdout and stderr collected and the exit status mapped the
 * way `cli-main.handleError` maps it: `ExitCodeSignal` is the status, anything
 * else is `error: …` on stderr and status 2.
 */
export const runDoCaptured = async (
  target: Bench,
  args: DoArguments
): Promise<Captured> => {
  const out: Array<string> = [];
  const err: Array<string> = [];
  const originalLog = console.log;
  const originalError = console.error;
  console.log = (...parts: ReadonlyArray<unknown>) => out.push(parts.join(' '));
  console.error = (...parts: ReadonlyArray<unknown>) => err.push(parts.join(' '));
  try {
    const result = await Effect.runPromise(
      Effect.either(
        Effect.provide(runDo({ ...args, session: target.sessionPath }, target.hooks), target.layer)
      )
    ).catch((cause: unknown) => {
      if (String(cause).includes('SIGKILL')) return 'killed' as const;
      throw cause;
    });
    if (result === 'killed') return { out, err, code: 137, error: '', killed: true };
    if (result._tag === 'Right') return { out, err, code: 0, error: '', killed: false };
    const failure: unknown = result.left;
    if (property(failure, '_tag') === 'ExitCodeSignal') {
      const code = property(failure, 'code');
      return { out, err, code: typeof code === 'number' ? code : 2, error: '', killed: false };
    }
    return {
      out,
      err,
      code: 2,
      error: String(property(failure, 'message') ?? failure),
      killed: false,
    };
  } finally {
    console.log = originalLog;
    console.error = originalError;
  }
};
