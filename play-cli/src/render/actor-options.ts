/**
 * Inline options beside a refusal.
 *
 * Ports the rendering half of `_refused_actor_options` and
 * `_actor_options_section` (client.py:9410-9478), plus the two bounds above
 * them.
 *
 * A refused order leaves the turn open, and the only thing the agent needs
 * next is the menu it should have chosen from.  Printing it here is what turns
 * a refusal into one command instead of three: the observed loop was `do` →
 * refusal → `legal --actor_id uNN --all` → `do` again, two of which are pure
 * overhead.  The rows are the same rows `just legal --actor_id uNN --all`
 * prints, bounded, and the drill-down is named whenever they are cut short.
 *
 * Every failure here degrades to silence.  A refusal already carries its own
 * remedy, and help that could not be built must never make the refusal beside
 * it read worse than it did before this existed.
 */
import { Effect } from 'effect';
import { playerError, type PlayError, type PlayerError } from 'src/errors';
import type { PageScope } from 'src/schema/page';
import { field, type JsonObject, type JsonValue } from 'src/schema/primitives';
import type { Revision } from 'src/schema/revision';
import { requestedScope, revisionLabel } from 'src/render/primitives';
import { aliasMap, jsonEquals, type AliasMap } from 'src/services/aliases';
import { cachedActorCatalog } from 'src/services/catalog-cache';
import { drainActors, DO_DRAIN_CONCURRENCY, type DrainGate } from 'src/services/do-drain';
import type { V2ClientState } from 'src/services/session-store';

/** One screen of options is a choice; a whole catalog is another problem. */
export const V2_REFUSAL_LEGAL_ROWS = 12;

/**
 * A batch can refuse up to `V2_MAX_ORDERS` actors, but a refusal that answers
 * with eight menus is no longer an answer.  Beyond this, the remaining actors
 * keep the refusal they always had and the `just legal` command that reads
 * them, which is exactly what this replaced for the first three.
 */
export const V2_REFUSAL_LEGAL_ACTORS = 3;

// ---------------------------------------------------------------------------
// The U11 seam
// ---------------------------------------------------------------------------

/**
 * The two U11 renderers a bounded catalog is drawn with.
 *
 * They arrive as dependencies for the same reason `CatalogRenderDeps` does
 * (PORT_MAP §6): the row renderers are `src/render/legal/*`, which this unit
 * does not own and which had not landed when this file was written.
 */
export interface ActorOptionsDeps {
  /** `_compact_legal_action` — the leak-guarded projection rows are read from. */
  readonly compactLegalAction: (descriptor: JsonObject) => Effect.Effect<JsonObject, PlayerError>;
  /** `_legal_rows` — the aligned `aN  kind/op  Label  target` table. */
  readonly legalRows: (
    compacts: ReadonlyArray<JsonObject>,
    scope: PageScope | null,
    aliases: AliasMap | null
  ) => Effect.Effect<ReadonlyArray<string>, PlayerError>;
}

// ---------------------------------------------------------------------------
// _actor_options_section
// ---------------------------------------------------------------------------

const revisionValue = (revision: Revision): JsonValue => ({
  turn: revision.turn,
  revision: revision.revision,
  state_token: revision.state_token,
});

/**
 * Does this actor's menu still have to be fetched before it can be printed?
 *
 * A rejected order changes nothing, so the revision usually stands still and
 * this seat still holds the catalog it read — exactly the condition
 * `drained_actors` records.  When it does, the section costs no round trip at
 * all; when the refusal moved the game on, the wiped cache is re-drained once,
 * the same way the not-yet-sent orders are.
 */
export const actorNeedsDrain = (state: V2ClientState, actorId: string): boolean =>
  !state.drained_actors.includes(actorId);

/** Render one refused actor's bounded catalog from the cache alone. */
export const actorOptionsSection = (
  state: V2ClientState,
  actorId: string,
  deps: ActorOptionsDeps
): Effect.Effect<ReadonlyArray<string>, PlayerError> =>
  Effect.gen(function* () {
    const revision = state.last_revision;
    if (revision === null) return [];
    const stamp = revisionValue(revision);
    const descriptors = cachedActorCatalog(state, actorId).filter((descriptor) =>
      jsonEquals(field(descriptor, 'state_revision'), stamp)
    );
    if (descriptors.length === 0) return [];
    const aliases = yield* aliasMap(state);
    const named = aliases[actorId] ?? actorId;
    const shown = yield* Effect.forEach(
      descriptors.slice(0, V2_REFUSAL_LEGAL_ROWS),
      deps.compactLegalAction
    );
    const header =
      `${named} can (${revisionLabel(revision)}): ` +
      (shown.length < descriptors.length
        ? `${shown.length} of ${descriptors.length} shown — all: ` +
          `just legal --actor_id ${named} --all`
        : `${descriptors.length} options`);
    const rows = yield* deps.legalRows(shown, requestedScope(actorId, ''), aliases);
    return [header, ...rows];
  });

// ---------------------------------------------------------------------------
// _refused_actor_options
// ---------------------------------------------------------------------------

/** What the refusal path needs from the units that own state and the wire. */
export interface RefusedActorOptionsIo extends ActorOptionsDeps {
  /** `_load_v2_client_state` — re-read after the drains, as CPython does. */
  readonly readState: Effect.Effect<V2ClientState, PlayError>;
  /** `_drain_legal_unlocked` — the caller already holds the request lock. */
  readonly drainActor: (
    actorId: string,
    gate: DrainGate
  ) => Effect.Effect<Revision | null, PlayError>;
}

/**
 * `except (PlayerError, V2ResponseError, OSError)`, in Effect's two channels.
 *
 * CPython's `except OSError` is the whole of the port's *defect* channel here:
 * `PrivateFsApi.readText` runs `fstat`/`readFileSync` inside a bare
 * `try/finally` rather than an `Effect.try` (NOTES §20.10), so an `EIO`,
 * `EACCES` or `EPERM` while re-reading `.v2-state` — or inside the ingest a
 * drain does — arrives as a defect, and a defect sails straight past
 * `orElseSucceed`.  It would then escape `doLocked` and `runDo` before
 * `render(lines)` ever ran and reach `cli-main`'s `catchAllCause` as
 * `error: <Cause.pretty>` on stderr with an **empty stdout** and status 2 —
 * losing the applied orders' server-issued `batch_id`s from a command CPython
 * completed, which is the duplicate-action incident this unit exists to
 * prevent.  Optional help must never be able to do that.
 *
 * Interruption is deliberately left alone: `catchAllDefect` does not catch it,
 * so a `SIGINT` still cancels the command rather than being absorbed by a
 * menu.
 */
const silent = <A, E, R>(body: Effect.Effect<A, E, R>, fallback: A): Effect.Effect<A, never, R> =>
  Effect.catchAllDefect(Effect.orElseSucceed(body, () => fallback), () =>
    Effect.succeed(fallback)
  );

/** The same guard for a drain, whose failure `drainActors` records per actor. */
const survivable = (
  drain: RefusedActorOptionsIo['drainActor']
): RefusedActorOptionsIo['drainActor'] =>
  (actorId, gate) =>
    Effect.catchAllDefect(drain(actorId, gate), () =>
      Effect.fail(playerError(`cannot read ${actorId} options`))
    );

const NO_LINES: ReadonlyArray<string> = [];

/**
 * Print what each refused actor can still do, inline in the refusal.
 *
 * PLAN upgrade 1 applies here: CPython drained up to three catalogs one after
 * another, three serial round trips inside a refusal.  The drains run with
 * bounded concurrency instead and the sections are still rendered in the order
 * the orders were given — the whole point of collecting before emitting.
 *
 * `stopOnFailure: false` is this loop's own shape, and the only place in the
 * port that passes it: `_refused_actor_options` (client.py:9433-9440) catches
 * per actor and `continue`s to the next one, so a drain that fails takes only
 * its own section with it and the later actors are still fetched — the
 * opposite of `_resolve_orders_fetching`'s raise.
 */
export const refusedActorOptions = (
  actors: ReadonlyArray<string>,
  io: RefusedActorOptionsIo
): Effect.Effect<ReadonlyArray<string>> =>
  Effect.gen(function* () {
    const wanted = actors.slice(0, V2_REFUSAL_LEGAL_ACTORS);
    if (wanted.length === 0) return NO_LINES;
    const before = yield* silent<V2ClientState | null, PlayError, never>(io.readState, null);
    if (before === null) return NO_LINES;
    const cold = wanted.filter((actorId) => actorNeedsDrain(before, actorId));
    const drains = yield* drainActors(cold, survivable(io.drainActor), {
      concurrency: DO_DRAIN_CONCURRENCY,
      stopOnFailure: false,
    });
    const broken = new Set(
      drains.filter((outcome) => outcome.failure !== null).map((outcome) => outcome.actorId)
    );
    const state = cold.length === 0 ? before : yield* silent(io.readState, before);
    const sections = yield* Effect.forEach(wanted, (actorId) =>
      broken.has(actorId)
        ? Effect.succeed(NO_LINES)
        : silent(actorOptionsSection(state, actorId, io), NO_LINES)
    );
    return sections.flat();
  });
