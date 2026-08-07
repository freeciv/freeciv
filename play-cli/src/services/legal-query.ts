/**
 * Building one `/legal-actions` query, and reading the page it returns.
 *
 * Ports `_legal_query` (client.py:7817-7860) and `_read_legal_page`
 * (7863-7896), plus `_limit` (6513-6518), which PORT_MAP left unassigned and
 * both `state` and `legal` need (see NOTES.md §U11.2).
 *
 * The read is not a plain GET: a page that fails validation, and a cursor the
 * server has expired, both leave a staged catalog in `.v2-state` that can never
 * complete.  Dropping it here is what keeps a failed enumeration from silently
 * poisoning the next one.
 */
import { Effect } from 'effect';
import {
  playerError,
  type DriftError,
  type LockTimeoutError,
  type PlayerError,
  type V2ResponseError,
} from 'src/errors';
import { ACTOR_ID_RE, CURSOR_RE, RELATION_ID_RE, TILE_ID_RE } from 'src/constants';
import type { AliasMap } from 'src/render/primitives';
import { decodeLegalPage, type LegalActionPageEnvelope } from 'src/schema/page';
import { field, isJsonObject } from 'src/schema/primitives';
import { aliasMap, rememberPage } from 'src/services/aliases';
import { promotedCatalogPage } from 'src/services/catalog-cache';
// U04's barrel does not re-export the page bridge (NOTES §U12.3); the module
// that owns `update_from_page` does.
import { mirrorPage } from 'src/services/mirror/update-page';
import { dropPendingForCursor, dropPendingForScope } from 'src/services/pending-catalogs';
import { PrivateFs } from 'src/services/private-fs';
import { SessionStore, credentialsOf, type Session } from 'src/services/session-store';
import { V2Client } from 'src/services/v2-client';

/** Ten seconds, as CPython passed to `_v2_response`. */
export const LEGAL_TIMEOUT_S = 10;

export type LegalError = PlayerError | V2ResponseError | DriftError | LockTimeoutError;

/**
 * What one legal read is about.
 *
 * `query` is the already-encoded query string; the three scope fields are
 * carried alongside it because the *failure* paths need to know which staged
 * catalog a bad response invalidated, and the query string alone cannot say.
 */
export interface LegalQuery {
  readonly query: string;
  readonly cursor: string;
  readonly actorId: string;
  readonly targetId: string;
}

/** The session this command acts against, plus the mirror seam. */
export interface LegalCtx {
  readonly sessionPath: string;
  readonly session: Session;
  /**
   * `_mirror_page` — U07's `update_from_page`, overridable for a test.
   *
   * **Absent means the real one**, not a no-op: `_read_legal_page`
   * (client.py:7895) calls `_mirror_page` unconditionally, and it is the only
   * path that reaches `_update_options`, so a `LegalCtx` that forgot to wire it
   * would leave `state/options/<alias>.txt` and the actions projection frozen
   * at whatever the workspace shipped with while CPython's stayed current —
   * a `show options/u1` / `show` / `show --grep` divergence on every later
   * call, in files `V2_PROTOCOL_CARD` tells the agent to read.  Every drain
   * (`legal`, `do`, `start`, `turn --decisions`, U03's alias refresh) reaches
   * the wire through here, so defaulting it is what makes them all correct at
   * once.  A projection still never fails a command: `mirrorPage` swallows its
   * own failures into one stderr warning.
   */
  readonly mirrorPage?: (
    page: LegalActionPageEnvelope,
    aliases: AliasMap,
    label: string
  ) => Effect.Effect<void, never, PrivateFs>;
  /**
   * Serialize every `.v2-state` critical section this read enters.
   *
   * The advisory lock is `flock(2)` and its retry loop parks the thread, so two
   * fibers of one process contending for it would deadlock rather than queue.
   * When pages are fetched concurrently (see `legal-drain.ts`) the caller
   * passes a one-permit semaphore here; the network round trips then overlap
   * while every state write stays single-file.  Absent, it is the identity.
   */
  readonly gate?: <A, E, R>(body: Effect.Effect<A, E, R>) => Effect.Effect<A, E, R>;
}

const identityGate = <A, E, R>(body: Effect.Effect<A, E, R>): Effect.Effect<A, E, R> => body;

/** `_mirror_page(path, state, page, "legal")`, as the default {@link LegalCtx} seam. */
const liveMirrorPage =
  (sessionPath: string) =>
  (
    page: LegalActionPageEnvelope,
    aliases: AliasMap,
    label: string
  ): Effect.Effect<void, never, PrivateFs> =>
    mirrorPage(sessionPath, page, label, { aliases });

// ---------------------------------------------------------------------------
// _limit
// ---------------------------------------------------------------------------

const PAGE_LIMIT_RE = /^(?:[1-9]|1[0-6])$/;

export const PAGE_LIMIT_REFUSAL = 'limit must be a canonical integer from 1 through 16';

/** `--limit` in its *server page size* sense: 1..16, canonical spelling only. */
export const pageLimit = (
  value: string | null | undefined,
  defaultLimit = 16
): Effect.Effect<number, PlayerError> => {
  if (value === null || value === undefined) return Effect.succeed(defaultLimit);
  if (!PAGE_LIMIT_RE.test(value)) return Effect.fail(playerError(PAGE_LIMIT_REFUSAL));
  return Effect.succeed(Number.parseInt(value, 10));
};

// ---------------------------------------------------------------------------
// _legal_query
// ---------------------------------------------------------------------------

const encode = (params: ReadonlyArray<readonly [string, string]>): string => {
  const search = new URLSearchParams();
  for (const [key, value] of params) search.append(key, value);
  return search.toString();
};

/**
 * A relation is the one ID an agent reaches for as an actor and never can be:
 * diplomacy is enumerated by this seat's own player bound to the relation.
 * Refusing without naming that form is how an open meeting reads as an
 * unreachable one.
 */
const relationAsActor = (relationId: string): string =>
  'a relation ID is a diplomacy target, not an actor; enumerate the meeting ' +
  `with \`just legal --actor_id YOUR_PLAYER_ID --target_id ${relationId} ` +
  '--all`, taking YOUR_PLAYER_ID from `just state --section overview`';

export interface LegalQueryArgs {
  readonly cursor: string;
  readonly actorId: string;
  readonly targetId: string;
  readonly limit: string | null;
}

/** The `/legal-actions` query string this invocation's flags describe. */
export const legalQuery = (
  args: LegalQueryArgs,
  options: { readonly ignoreLimit?: boolean } = {}
): Effect.Effect<string, PlayerError> => {
  const cursor = args.cursor.trim();
  const actor = args.actorId.trim();
  const target = args.targetId.trim();
  const limit = options.ignoreLimit === true ? null : args.limit;
  if (cursor !== '') {
    if (actor !== '' || target !== '' || limit !== null || !CURSOR_RE.test(cursor)) {
      return Effect.fail(playerError('legal cursor must be the only page option'));
    }
    return Effect.succeed(encode([['cursor', cursor]]));
  }
  if (target !== '') {
    if (actor === '') return Effect.fail(playerError('legal target requires actor'));
    if (!ACTOR_ID_RE.test(actor) || (!TILE_ID_RE.test(target) && !RELATION_ID_RE.test(target))) {
      return Effect.fail(playerError('actor or target ID has the wrong v2 ID type'));
    }
    if (limit !== null && RELATION_ID_RE.test(target)) {
      return Effect.fail(playerError('legal relation target does not accept a limit'));
    }
    return Effect.map(
      limit === null ? Effect.succeed(null) : pageLimit(limit),
      (bound) =>
        encode(
          bound === null
            ? [
                ['actor_id', actor],
                ['target_id', target],
              ]
            : [
                ['actor_id', actor],
                ['target_id', target],
                ['limit', String(bound)],
              ]
        )
    );
  }
  if (actor !== '' && !ACTOR_ID_RE.test(actor)) {
    if (RELATION_ID_RE.test(actor)) {
      return Effect.fail(playerError(relationAsActor(actor)));
    }
    return Effect.fail(playerError('actor ID has the wrong v2 ID type'));
  }
  return Effect.map(limit === null ? Effect.succeed(null) : pageLimit(limit), (bound) => {
    const params: Array<readonly [string, string]> = [];
    if (actor !== '') params.push(['actor_id', actor]);
    if (bound !== null) params.push(['limit', String(bound)]);
    return encode(params);
  });
};

// ---------------------------------------------------------------------------
// _read_legal_page
// ---------------------------------------------------------------------------

const isCursorExpired = (payload: unknown): boolean => {
  if (!isJsonObject(payload)) return false;
  const error = field(payload, 'error');
  return isJsonObject(error) && field(error, 'code') === 'cursor_expired';
};

/** Forget whichever staged catalog a failed read has just invalidated. */
const dropStaged = (
  ctx: LegalCtx,
  query: LegalQuery
): Effect.Effect<void, PlayerError | LockTimeoutError, SessionStore | PrivateFs> => {
  if (query.cursor !== '') {
    return Effect.asVoid(dropPendingForCursor(ctx.sessionPath, ctx.session, query.cursor));
  }
  if (query.actorId !== '') {
    return Effect.asVoid(
      dropPendingForScope(ctx.sessionPath, ctx.session, query.actorId, query.targetId)
    );
  }
  return Effect.void;
};

/**
 * GET one legal-actions page, ingest it, and mirror it.
 *
 * The alias assignment and the atomic catalog promotion both happen inside
 * `rememberPage`, so every caller — the single-page command, the `--all` drain
 * and U03's stale-alias refresh — learns exactly the same thing from a page.
 */
export const readLegalPage = (
  ctx: LegalCtx,
  query: LegalQuery
): Effect.Effect<
  LegalActionPageEnvelope,
  LegalError,
  SessionStore | PrivateFs | V2Client
> =>
  Effect.gen(function* () {
    const gate = ctx.gate ?? identityGate;
    const client = yield* V2Client;
    const credentials = credentialsOf(ctx.session);
    const base = yield* client.url(credentials, '/legal-actions');
    const url = query.query === '' ? base : `${base}?${query.query}`;
    const response = yield* client.response('GET', url, credentials, {
      timeout: LEGAL_TIMEOUT_S,
    });
    if (response.status < 200 || response.status >= 300) {
      return yield* Effect.catchTag(
        client.raiseValidated(response),
        'V2ResponseError',
        (error) =>
          Effect.flatMap(
            isCursorExpired(error.payload) && query.cursor !== ''
              ? gate(
                  Effect.asVoid(
                    dropPendingForCursor(ctx.sessionPath, ctx.session, query.cursor)
                  )
                )
              : Effect.void,
            () => Effect.fail(error)
          )
      );
    }
    const value = yield* Effect.catchTag(
      decodeLegalPage(response.value, ctx.session),
      'DriftError',
      (error) => Effect.flatMap(gate(dropStaged(ctx, query)), () => Effect.fail(error))
    );
    return yield* gate(
      Effect.gen(function* () {
        const remembered = yield* rememberPage(ctx.sessionPath, ctx.session, {
          legal: true,
          page: value,
        });
        const mirror = ctx.mirrorPage ?? liveMirrorPage(ctx.sessionPath);
        // CPython names the rows from the state `_remember_page` has already
        // folded back into the caller's dict, so the alias column carries the
        // handles this very page taught the seat.
        yield* mirror(
          promotedCatalogPage(value, remembered.promoted),
          yield* aliasMap(remembered.state),
          'legal'
        );
        return value;
      })
    );
  });
