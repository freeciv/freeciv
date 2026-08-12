/**
 * `do`'s catalog drains, run concurrently and reported deterministically.
 *
 * PLAN.md §"The two behavioral upgrades allowed in phase 1", upgrade 1.
 *
 * CPython drained one actor's catalog at a time, in three separate places:
 * `_resolve_orders_fetching` reads every unread actor before anything is sent,
 * `_refresh_orders` re-reads the not-yet-sent orders' actors after a receipt
 * bumps the revision, and `_refused_actor_options` reads up to three refused
 * actors' menus.  Each drain is a full round trip and CPython paid them
 * serially — an eight-order `do` across eight actors spent eight round trips
 * before the first byte reached the wire.  That is the 15 s `do` that put
 * gpt-5.6-sol into a timeout death loop.
 *
 * The fix is bounded concurrency and *nothing else*.  The functions here
 * return results in the caller's input order and never print, because the one
 * property `do` cannot lose is that its notes, receipts and refusals appear in
 * exactly the order CPython printed them.  Collect here; emit in `do.cmd.ts`.
 *
 * Two of CPython's three loops also *stop* at the first failing actor, and that
 * is not a printing property — it is a commit property.  See
 * {@link DrainOptions.stopOnFailure}.
 */
import { Cause, Chunk, Deferred, Effect, Option } from 'effect';
import type { PlayError } from 'src/errors';
import type { Revision } from 'src/schema/revision';
import type { JsonValue } from 'src/schema/primitives';

/**
 * PLAN.md fixes this at 4.
 *
 * The supervisor serves one seat from one process; four in flight saturates
 * the round-trip window an eight-order batch has without turning a drain
 * storm into the rate-limit refusal the busy-retry path exists to absorb.
 */
export const DO_DRAIN_CONCURRENCY = 4;

// ---------------------------------------------------------------------------
// Actor lists
// ---------------------------------------------------------------------------

/**
 * `_refresh_orders`' `drained` set, as a list.
 *
 * The order matters twice over: it is the order the drains are *reported* in,
 * and it is the order CPython would have hit the first failure in.
 *
 * **The empty actor id is a value, not a gap.**  `_order_actor`
 * (client.py:8721-8725) reads `subject.actor.id`, and `actor` is not a
 * declared property of `LegalActionSubject` at all — every research,
 * government, phase and player-family capability therefore resolves with
 * `actor_id == ""`.  `_refresh_orders` (client.py:9389-9398) keeps it and
 * calls `_drain_legal_unlocked(actor_id="")`, which is a *global* catalog
 * re-enumeration: exactly what `end` or `research set Alphabet` needs after an
 * earlier receipt bumped the revision and wiped the cache.  Dropping it here
 * would leave `rebindOrder` nothing to bind and turn a re-issuable order into
 * `revN/tM no longer offers …`.
 *
 * `_order_fetch_targets` (client.py:9281-9301) is the loop that *does* skip the
 * actorless order (`or not actor`), and U15 owns that filter — it is not this
 * function's job to apply it a second time.
 */
export const distinctActors = (ids: Iterable<string>): ReadonlyArray<string> => {
  const seen = new Set<string>();
  const out: Array<string> = [];
  for (const id of ids) {
    if (seen.has(id)) continue;
    seen.add(id);
    out.push(id);
  }
  return out;
};

/** `{identifier: alias}` — `_resolve_orders_fetching`'s inverted alias table. */
export const actorAliases = (
  entityAliases: Readonly<Record<string, JsonValue>>
): Readonly<Record<string, string>> => {
  const out: Record<string, string> = {};
  for (const [alias, identifier] of Object.entries(entityAliases)) {
    if (typeof identifier === 'string') out[identifier] = alias;
  }
  return out;
};

// ---------------------------------------------------------------------------
// The concurrent drain
// ---------------------------------------------------------------------------

/** One actor's drain, whether it worked, failed, or never happened. */
export interface DrainOutcome {
  readonly actorId: string;
  /** The revision the last drained page carried, when the drain succeeded. */
  readonly revision: Revision | null;
  /** The error CPython would have raised at this point in its loop. */
  readonly failure: PlayError | null;
  /**
   * True when a lower-indexed drain failed first, so CPython never issued this
   * one and nothing of it was committed.  Never true together with
   * {@link DrainOutcome.failure}.
   */
  readonly skipped: boolean;
}

export interface DrainOptions {
  readonly concurrency?: number;
  /**
   * Reproduce the `for` loop that *raises* on the first failing actor.
   *
   * `_resolve_orders_fetching` (client.py:9339-9348) and `_refresh_orders`
   * (client.py:9389-9398) both let `_drain_legal_unlocked` throw, so every
   * later actor in their list is never fetched and never ingested — its
   * catalog does not enter `.v2-state`, it does not join `drained_actors`, and
   * it consumes none of `a1..aN`.  That last part is the one that bites: an
   * uncommanded commit is *not* "only a warm cache entry".  `drained_actors`
   * is exactly what `_order_fetch_targets` (client.py:9281-9301) skips on, so
   * the agent's retry of the identical batch silently drops a
   * `fetched uN options (revM)` line it printed under CPython, and the alias
   * digits `just legal` prints afterwards are renumbered — NOTES §20.2's
   * silent-renumbering hazard, arriving through the failure path.
   *
   * So the commits are refused, not merely ordered: once the drain at index
   * *i* has failed, the gate at every index above *i* declines to admit its
   * body and that drain reports {@link DrainOutcome.skipped}.  The redundant
   * *fetch* may already have gone out — a GET that ingests nothing is
   * invisible — but nothing it read is kept.
   *
   * `_refused_actor_options` (client.py:9433-9440) is the opposite loop: it
   * catches per actor and `continue`s, so it passes `false`.
   *
   * Defaults to `true`, the majority and the safe direction.
   */
  readonly stopOnFailure?: boolean;
}

/**
 * The ordered-commit barrier every concurrent drain must run its writes inside.
 *
 * This is the part concurrency gets wrong if it is left alone.  A drain does
 * two things — it fetches pages, and it *commits* them into `.v2-state`, which
 * is where `a1..aN` are numbered.  Overlap the fetches and the numbering starts
 * following whichever response happened to land first, so `just legal` prints
 * `a1 a2 a3` against three different actions on three identical runs.  That is
 * not a stdout diff on `do` itself, which is exactly why it would have survived
 * to a live match.
 *
 * So the fetches overlap and the commits do not: gate *i* admits its body only
 * once every lower-indexed drain has finished, which reproduces CPython's
 * sequential numbering exactly while still paying one round trip for the first
 * page of every actor instead of N.  U11's `LegalCtx.gate` has precisely this
 * shape, so `drainLegal` takes one as-is.
 */
export type DrainGate = <A, E, R>(body: Effect.Effect<A, E, R>) => Effect.Effect<A, E, R>;

/** The gate a caller with nothing to order passes. */
export const openGate: DrainGate = (body) => body;

const succeeded = (actorId: string, revision: Revision | null): DrainOutcome => ({
  actorId,
  revision,
  failure: null,
  skipped: false,
});

const failed = (actorId: string, failure: PlayError): DrainOutcome => ({
  actorId,
  revision: null,
  failure,
  skipped: false,
});

const skipped = (actorId: string): DrainOutcome => ({
  actorId,
  revision: null,
  failure: null,
  skipped: true,
});

/**
 * How a closed gate says "CPython never got here" without inventing an error.
 *
 * The gate's type is U11's — `<A, E, R>(body) => Effect<A, E, R>` — so it may
 * not widen the drain's error channel, and a *typed* refusal would be
 * indistinguishable from a real drain failure at the seam that reads it.  A
 * private defect is neither: it cannot collide with a `PlayError`, it is not
 * catchable by any `catchAll` on the way out, and it is recognised and
 * converted back into an ordinary `skipped` row a few lines below, so it never
 * escapes this module.
 */
const DRAIN_NOT_ISSUED: unique symbol = Symbol.for('play-cli/do-drain/not-issued');

const wasSkipped = (cause: Cause.Cause<PlayError>): boolean =>
  Chunk.some(Cause.defects(cause), (defect) => defect === DRAIN_NOT_ISSUED);

/**
 * Drain every actor with bounded concurrency; report them in input order.
 *
 * No failure escapes through the error channel: each drain's error is captured
 * on its own row, so the caller decides whether CPython's loop would have
 * raised it (`_refresh_orders`, `_resolve_orders_fetching`) or swallowed it
 * (`_refused_actor_options`).  Defects and interruption are *not* captured —
 * they are the caller's to see, exactly as `Effect.match` left them.
 *
 * With {@link DrainOptions.stopOnFailure} (the default) a failure at index *i*
 * closes the gate for every index above it, so those drains commit nothing and
 * report {@link DrainOutcome.skipped}; the state CPython's raise left behind is
 * the state this leaves behind.
 */
export const drainActors = <R>(
  actors: ReadonlyArray<string>,
  drainOne: (actorId: string, gate: DrainGate) => Effect.Effect<Revision | null, PlayError, R>,
  options: DrainOptions = {}
): Effect.Effect<ReadonlyArray<DrainOutcome>, never, R> =>
  Effect.gen(function* () {
    const stop = options.stopOnFailure ?? true;
    /** Capture the typed error, hand back the defect a closed gate raised. */
    const run = (
      actorId: string,
      gate: DrainGate
    ): Effect.Effect<DrainOutcome, never, R> =>
      Effect.matchCauseEffect(drainOne(actorId, gate), {
        onFailure: (cause): Effect.Effect<DrainOutcome> => {
          if (wasSkipped(cause)) return Effect.succeed(skipped(actorId));
          const failure = Cause.failureOption(cause);
          return Option.isSome(failure)
            ? Effect.succeed(failed(actorId, failure.value))
            : // No typed failure in here at all: a genuine defect, or an
              // interruption.  `stripFailures` keeps both and drops nothing.
              Effect.failCause(Cause.stripFailures(cause));
        },
        onSuccess: (revision): Effect.Effect<DrainOutcome> =>
          Effect.succeed(succeeded(actorId, revision)),
      });

    if (actors.length <= 1) {
      return yield* Effect.forEach(actors, (actorId) => run(actorId, openGate));
    }
    // One latch per drain, completed when that drain finishes however it
    // finished, and carrying "everything from here on is uncommanded".  Drain
    // `i` may not commit until latch `i-1` is open, so the commits — and
    // therefore the alias numbering — happen in the order the orders named
    // their actors, whatever order the responses arrive in; and when that
    // latch says the batch has already failed, drain `i` does not commit at
    // all.
    const latches = yield* Effect.forEach(actors, () => Deferred.make<boolean>());
    return yield* Effect.forEach(
      actors,
      (actorId, index) => {
        const previous = index === 0 ? null : (latches[index - 1] ?? null);
        const own = latches[index];
        const gate: DrainGate = (body) =>
          previous === null
            ? body
            : Effect.flatMap(Deferred.await(previous), (aborted) =>
                aborted && stop ? Effect.die(DRAIN_NOT_ISSUED) : body
              );
        const body = Effect.gen(function* () {
          const outcome = yield* run(actorId, gate);
          const aborted = previous === null ? false : yield* Deferred.await(previous);
          if (own !== undefined) {
            yield* Deferred.succeed(own, aborted || outcome.failure !== null);
          }
          // A `drainOne` that ignores its gate cannot be allowed to report a
          // commit CPython never made, so the abort decides the row too.
          return aborted && stop ? skipped(actorId) : outcome;
        });
        // If this drain is interrupted before it ever answers, the latch still
        // has to open or every later drain waits forever — and it opens
        // closed, because an interrupted drain committed nothing either.
        return own === undefined
          ? body
          : Effect.ensuring(body, Deferred.succeed(own, true));
      },
      { concurrency: options.concurrency ?? DO_DRAIN_CONCURRENCY }
    );
  });

/**
 * The first failure in *input* order, which is the one CPython would have
 * raised — not the first one the concurrent region happened to finish.
 */
export const firstDrainFailure = (outcomes: ReadonlyArray<DrainOutcome>): PlayError | null => {
  for (const outcome of outcomes) {
    if (outcome.failure !== null) return outcome.failure;
  }
  return null;
};

// ---------------------------------------------------------------------------
// The note
// ---------------------------------------------------------------------------

/**
 * `_resolve_orders_fetching`'s per-actor note.
 *
 * CPython re-read `.v2-state` after each drain and stamped the note with
 * `state["last_revision"]`; the port stamps it with the revision that actor's
 * own last page carried.  In the sequential case they are the same value, and
 * in the concurrent case the per-drain revision is the one that stays true to
 * "this is what I just fetched for you".
 */
export const fetchedOptionsNote = (named: string, revision: Revision | null): string =>
  `fetched ${named} options` + (revision === null ? '' : ` (rev${revision.revision})`);
