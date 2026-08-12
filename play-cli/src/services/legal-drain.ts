/**
 * Draining a whole catalog, and the refusals a drain can end in.
 *
 * Ports `_command_legal_all`'s drain half (client.py:8121-8287),
 * `_drain_legal_unlocked` (9358-9387), `_player_scope_alias` (8032-8040),
 * `_kind_matched_nothing` (8043-8121) and `_unknown_kind` (8124-8151).
 *
 * ## The concurrency upgrade, and where it can honestly land
 *
 * PLAN §"two behavioural upgrades" allows bounded-concurrency page fetching.
 * Inside *one* catalog it cannot apply: page N+1's URL is page N's opaque
 * `next_cursor`, so the chain is causal and no prefetch is possible.  It
 * applies across *catalogs*, which is exactly what `do` needs — one drain per
 * actor.  {@link drainLegalActors} runs those with `Effect.forEach` at
 * concurrency {@link LEGAL_DRAIN_CONCURRENCY}, and two properties make it safe
 * to print from:
 *
 * - **Order is by input, not by completion.** `Effect.forEach` returns results
 *   in the order the actors were given, so the caller emits rows in exactly the
 *   Python's order however the network interleaves.
 * - **State writes stay single-file.** The `.v2-state` lock is `flock(2)` whose
 *   retry loop parks the thread; two fibers contending for it would deadlock,
 *   not queue.  A one-permit semaphore is threaded through `LegalCtx.gate`, so
 *   the HTTP round trips overlap and the ingest does not.
 */
import { Effect } from 'effect';
import { playerError, type LockTimeoutError, type PlayerError } from 'src/errors';
import {
  V2_LEGAL_ACTOR_MATCH_LIMIT,
  V2_LEGAL_COMPACT_MAX_BYTES,
  V2_LEGAL_DRAIN_MAX_PAGES,
  V2_LEGAL_MATCH_LIMIT,
  V2_LEGAL_SINGLE_ACTION_MAX_BYTES,
} from 'src/constants';
import { revisionLabel, scalar, scopeText } from 'src/render/primitives';
import { revisionsEqual, type Revision } from 'src/schema/revision';
import { field, isJsonObject, sortedNames, type JsonObject } from 'src/schema/primitives';
import type { PageScope } from 'src/schema/page';
import { aliasMap } from 'src/services/aliases';
import {
  cachedDescriptors,
  cachedKindScopes,
  kindList,
} from 'src/services/catalog-cache';
import type { LegalPageFetcher } from 'src/services/alias-refresh';
import { PrivateFs } from 'src/services/private-fs';
import { SessionStore, type V2ClientState } from 'src/services/session-store';
import { V2Client, type V2ClientApi } from 'src/services/v2-client';
import { descriptorKindKey, kindSelectorMatches } from 'src/render/legal/kinds';
import {
  compactActionBytes,
  compactLegalAction,
  compactLegalLimit,
  compactLegalOffset,
  descriptorToJson,
  type CompactAction,
  type LegalCompactResult,
} from 'src/services/legal-compact';
import {
  legalQuery,
  readLegalPage,
  type LegalCtx,
  type LegalError,
  type LegalQuery,
} from 'src/services/legal-query';

export const LEGAL_DRAIN_CONCURRENCY = 4;

export type DrainError = LegalError;

/**
 * Kinds the native protocol enumerates only inside this seat's own player
 * scope.  They are absent from the global catalog by construction, so a global
 * `--kind` search for one of them is never evidence that the rules forbid it —
 * and the government controls in particular are the ones a player goes looking
 * for by name after a report says a change is possible.
 *
 * client.py:8016-8025; core's `src/constants.ts` does not carry them.
 */
export const V2_PLAYER_SCOPED_KIND_PREFIXES: ReadonlyArray<string> = [
  'government.',
  'spaceship.',
  'player.set_multiplier',
];

/** Kinds enumerated only against a relation. No actor-only query reaches them. */
export const V2_RELATION_SCOPED_KIND_PREFIX = 'diplomacy.';

// ---------------------------------------------------------------------------
// _drain_legal_unlocked
// ---------------------------------------------------------------------------

/** What one bare drain learned: the revision it read, and the menu it saw. */
export interface DrainedCatalog {
  readonly revision: Revision | null;
  readonly actions: ReadonlyArray<CompactAction>;
}

const cursorQuery = (cursor: string): string =>
  new URLSearchParams({ cursor }).toString();

const REPEATED_CURSOR = 'the legal catalog repeated a cursor';
const DRAIN_LIMIT = 'the legal catalog exceeded the safe 512-page drain limit';

/**
 * Enumerate one catalog into the local cache, holding no new lock.
 *
 * Callers already hold this seat's request lock — the advisory lock is not
 * reentrant — so this is the internal drain the intent commands use to obtain
 * fresh capabilities without printing a catalog into agent context.
 */
export const drainLegal = (
  ctx: LegalCtx,
  actorId = ''
): Effect.Effect<DrainedCatalog, DrainError, SessionStore | PrivateFs | V2Client> =>
  Effect.gen(function* () {
    const seen = new Set<string>();
    const actions: CompactAction[] = [];

    interface Drain {
      readonly query: string;
      readonly cursor: string;
      readonly page: number;
      readonly done: DrainedCatalog | null;
    }
    const turnPage = (state: Drain) =>
      Effect.gen(function* () {
        if (state.page >= V2_LEGAL_DRAIN_MAX_PAGES) {
          return yield* Effect.fail(playerError(DRAIN_LIMIT));
        }
        const value = yield* readLegalPage(ctx, {
          query: state.query,
          cursor: state.cursor,
          actorId,
          targetId: '',
        });
        for (const item of value.page.items) {
          actions.push(yield* compactLegalAction(descriptorToJson(item)));
        }
        const cursor = value.page.next_cursor ?? '';
        if (cursor === '') {
          return { ...state, done: { revision: value.state_revision, actions } };
        }
        if (seen.has(cursor)) return yield* Effect.fail(playerError(REPEATED_CURSOR));
        seen.add(cursor);
        return { query: cursorQuery(cursor), cursor, page: state.page + 1, done: null };
      });
    const drained = yield* Effect.iterate(
      {
        query: actorId === '' ? '' : new URLSearchParams({ actor_id: actorId }).toString(),
        cursor: '',
        page: 0,
        done: null,
      } satisfies Drain as Drain,
      { while: (state) => state.done === null, body: turnPage }
    );
    return drained.done ?? (yield* Effect.dieMessage('unreachable: drain settled without a catalog'));
  });

/**
 * Drain several actors' catalogs at bounded concurrency, in input order.
 *
 * The result array is indexed by the actor list the caller passed, whatever
 * order the responses arrived in — which is what lets `do` print its per-actor
 * notes deterministically while the fetches overlap.
 */
export const drainLegalActors = (
  ctx: LegalCtx,
  actorIds: ReadonlyArray<string>
): Effect.Effect<
  ReadonlyArray<DrainedCatalog>,
  DrainError,
  SessionStore | PrivateFs | V2Client
> =>
  Effect.gen(function* () {
    if (actorIds.length <= 1) {
      return yield* Effect.forEach(actorIds, (actorId) => drainLegal(ctx, actorId));
    }
    const semaphore = yield* Effect.makeSemaphore(1);
    const gated: LegalCtx = { ...ctx, gate: semaphore.withPermits(1) };
    return yield* Effect.forEach(actorIds, (actorId) => drainLegal(gated, actorId), {
      concurrency: LEGAL_DRAIN_CONCURRENCY,
    });
  });

/**
 * The callback U03's stale-alias refresh takes.
 *
 * `LegalPageFetcher` is frozen in PORT_MAP §6 with `V2Client` absent from its
 * requirements and `PlayerError | LockTimeoutError` as its error channel, so
 * the client is supplied here and the two wider failures are re-raised as
 * `PlayerError` carrying the identical message.  Exit code and stderr are
 * unchanged; see NOTES.md §U11.4 for the one stdout line that is not.
 */
export const legalPageFetcher =
  (client: V2ClientApi): LegalPageFetcher =>
  (sessionPath, session, actorId) =>
    Effect.asVoid(
      drainLegal({ sessionPath, session }, actorId).pipe(
        Effect.catchTags({
          V2ResponseError: (error) => Effect.fail(playerError(error.message)),
          DriftError: (error) => Effect.fail(playerError(error.message)),
        }),
        Effect.provideService(V2Client, client)
      )
    );

// ---------------------------------------------------------------------------
// _command_legal_all — the drain
// ---------------------------------------------------------------------------

export interface DrainAllArgs {
  readonly cursor: string;
  readonly actorId: string;
  readonly targetId: string;
  readonly limit: string | null;
  readonly offset: string;
}

const CATALOG_CHANGED =
  'the legal catalog changed while it was being drained; run the same command again';

const OVERSIZED_SINGLE =
  'one compact legal action exceeds the bounded 64 KiB single-action contract';

/** Everything the drain saw, including what a bounded window did not print. */
export interface DrainAllOutcome {
  readonly result: LegalCompactResult | null;
  /** Every kind selector this drain actually printed a row for. */
  readonly present: ReadonlyArray<string>;
  readonly catalogTotal: number;
  readonly revision: Revision | null;
  /** True when `--kind` matched nothing at all; the caller then refuses. */
  readonly matchedNothing: boolean;
}

/**
 * Drain one catalog completely and compact it once.
 *
 * `kind` selects one class of action; an empty `kind` keeps every action the
 * drained scope enumerated, which is the `--actor_id … --all` form.  Either way
 * the drain, the validation, and the atomic promotion of the complete catalog
 * into local state are exactly the same work.
 */
export const drainLegalAll = (
  ctx: LegalCtx,
  args: DrainAllArgs,
  kind: string
): Effect.Effect<DrainAllOutcome, DrainError, SessionStore | PrivateFs | V2Client> =>
  Effect.gen(function* () {
    if (args.cursor.trim() !== '') {
      return yield* Effect.fail(playerError('legal --all starts a catalog; omit --cursor'));
    }
    const offset = yield* compactLegalOffset(args.offset);
    // `--kind` is a filtered window over every actor, so 64 matches is a
    // sensible ceiling.  `--actor_id … --all` promises one actor's whole menu,
    // which routinely exceeds 64 rows; only the byte cap bounds it.
    const compactLimit = yield* compactLegalLimit(
      args.limit,
      kind !== '' ? V2_LEGAL_MATCH_LIMIT : V2_LEGAL_ACTOR_MATCH_LIMIT
    );
    const encoded = yield* legalQuery(
      { cursor: args.cursor, actorId: args.actorId, targetId: args.targetId, limit: args.limit },
      { ignoreLimit: true }
    );
    const actorId = args.actorId.trim();
    const targetId = args.targetId.trim();

    // The scan state, carried immutably across pages.  The per-item pass is a
    // reduce over the same record; `hidden` counts the kinds a window kept
    // back, keyed exactly as `present` spells them.
    interface Scan {
      readonly query: string;
      readonly cursor: string;
      readonly revision: Revision | null;
      readonly catalogTotal: number | null;
      readonly matched: number;
      readonly compactActions: ReadonlyArray<CompactAction>;
      readonly compactBytes: number;
      readonly byteLimited: boolean;
      readonly oversizedSingle: boolean;
      readonly pagesRead: number;
      readonly present: ReadonlyArray<string>;
      readonly hidden: ReadonlyMap<string, number>;
      readonly exhausted: boolean;
      readonly done: boolean;
    }
    const hide = (hidden: ReadonlyMap<string, number>, name: string): ReadonlyMap<string, number> =>
      new Map(hidden).set(name, (hidden.get(name) ?? 0) + 1);
    const unhide = (hidden: ReadonlyMap<string, number>, name: string): ReadonlyMap<string, number> => {
      const remaining = (hidden.get(name) ?? 0) - 1;
      const next = new Map(hidden);
      if (remaining === 0) next.delete(name);
      else next.set(name, remaining);
      return next;
    };

    const takeItem = (
      state: Scan,
      descriptor: JsonObject
    ): Effect.Effect<Scan, PlayerError, SessionStore> =>
      Effect.gen(function* () {
        const presentKind = descriptorKindKey(descriptor);
        const present = state.present.includes(presentKind)
          ? state.present
          : [...state.present, presentKind];
        if (kind !== '' && !kindSelectorMatches(descriptor, kind)) {
          return { ...state, present };
        }
        const matchOffset = state.matched;
        const matched = state.matched + 1;
        if (matchOffset < offset || state.byteLimited) {
          return { ...state, present, matched, hidden: hide(state.hidden, presentKind) };
        }
        if (state.compactActions.length >= compactLimit) {
          return { ...state, present, matched, hidden: hide(state.hidden, presentKind) };
        }
        const compact = yield* compactLegalAction(descriptor);
        const encodedSize = compactActionBytes(compact);
        if (state.compactBytes + encodedSize > V2_LEGAL_COMPACT_MAX_BYTES) {
          if (state.compactActions.length === 0) {
            if (encodedSize > V2_LEGAL_SINGLE_ACTION_MAX_BYTES) {
              return yield* Effect.fail(playerError(OVERSIZED_SINGLE));
            }
            // The bounded fallback printed it after all, so it is not one of
            // the rows this window kept back.
            return {
              ...state,
              present,
              matched,
              byteLimited: true,
              oversizedSingle: true,
              compactActions: [compact],
              compactBytes: state.compactBytes + encodedSize,
            };
          }
          return {
            ...state,
            present,
            matched,
            byteLimited: true,
            hidden: hide(state.hidden, presentKind),
          };
        }
        return {
          ...state,
          present,
          matched,
          compactActions: [...state.compactActions, compact],
          compactBytes: state.compactBytes + encodedSize,
        };
      });

    const seenCursors = new Set<string>();
    const takePage = (
      state: Scan
    ): Effect.Effect<Scan, DrainError, SessionStore | PrivateFs | V2Client> =>
      Effect.gen(function* () {
        if (state.pagesRead >= V2_LEGAL_DRAIN_MAX_PAGES) {
          return yield* Effect.fail(playerError(DRAIN_LIMIT));
        }
        const request: LegalQuery = {
          query: state.query,
          cursor: state.cursor,
          actorId,
          targetId,
        };
        const value = yield* readLegalPage(ctx, request);
        if (
          state.revision !== null &&
          state.catalogTotal !== null &&
          (!revisionsEqual(value.state_revision, state.revision) ||
            value.page.total_items !== state.catalogTotal)
        ) {
          return yield* Effect.fail(playerError(CATALOG_CHANGED));
        }
        const paged: Scan = {
          ...state,
          revision: state.revision ?? value.state_revision,
          catalogTotal: state.catalogTotal ?? value.page.total_items,
          pagesRead: state.pagesRead + 1,
        };
        const scanned = yield* Effect.reduce(
          value.page.items.map((item) => descriptorToJson(item)),
          paged,
          (accumulated, descriptor) => takeItem(accumulated, descriptor)
        );
        const nextCursor = value.page.next_cursor ?? '';
        if (nextCursor === '') return { ...scanned, exhausted: false, done: true };
        if (seenCursors.has(nextCursor)) {
          return yield* Effect.fail(playerError(REPEATED_CURSOR));
        }
        seenCursors.add(nextCursor);
        return { ...scanned, query: cursorQuery(nextCursor), cursor: nextCursor };
      });

    const scan = yield* Effect.iterate(
      {
        query: encoded,
        cursor: '',
        revision: null,
        catalogTotal: null,
        matched: 0,
        compactActions: [],
        compactBytes: 0,
        byteLimited: false,
        oversizedSingle: false,
        pagesRead: 0,
        present: [],
        hidden: new Map<string, number>(),
        exhausted: true,
        done: false,
      } satisfies Scan as Scan,
      { while: (state) => !state.done, body: takePage }
    );
    const {
      revision,
      catalogTotal,
      matched,
      compactActions,
      byteLimited,
      oversizedSingle,
      pagesRead,
      present,
      hidden,
    } = scan;

    if (kind !== '' && matched === 0) {
      return {
        result: null,
        present,
        catalogTotal: catalogTotal ?? 0,
        revision,
        matchedNothing: true,
      };
    }
    const nextOffset = offset + compactActions.length;
    const hasMore = nextOffset < matched;
    const hiddenKinds: Record<string, number> = {};
    for (const name of sortedNames(hidden.keys())) hiddenKinds[name] = hidden.get(name) ?? 0;
    const result: LegalCompactResult = {
      schema_version: 1,
      command: 'legal',
      kind: kind !== '' ? kind : null,
      // Unreachable: the loop ran at least once, so a revision was read.
      state_revision: revision ?? { turn: 0, revision: 0, state_token: '' },
      catalog_total: catalogTotal ?? 0,
      pages_read: pagesRead,
      matched,
      offset,
      limit: compactLimit,
      shown: compactActions.length,
      truncated: compactActions.length < matched,
      has_more: hasMore,
      next_offset: hasMore ? nextOffset : null,
      byte_limited: byteLimited,
      oversized_single: oversizedSingle,
      hidden_kinds: hiddenKinds,
      actions: compactActions,
    };
    return { result, present, catalogTotal: catalogTotal ?? 0, revision, matchedNothing: false };
  });

// ---------------------------------------------------------------------------
// _player_scope_alias / _kind_matched_nothing / _unknown_kind
// ---------------------------------------------------------------------------

/** Name this seat's own player actor from whatever the cache already holds. */
export const playerScopeAlias = (
  state: V2ClientState
): Effect.Effect<string, PlayerError> =>
  Effect.gen(function* () {
    const aliases = yield* aliasMap(state);
    for (const descriptor of cachedDescriptors(state)) {
      const subject = field(descriptor, 'subject');
      const actor = isJsonObject(subject) ? field(subject, 'actor') : null;
      const actorId = isJsonObject(actor) ? field(actor, 'id') : null;
      if (typeof actorId === 'string' && actorId.startsWith('player_')) {
        return aliases[actorId] ?? actorId;
      }
    }
    return 'YOUR_PLAYER_ID';
  });

/**
 * Refuse a kind filter that matched nothing, naming what does exist.
 *
 * An empty page an agent believes is more expensive than a refusal: it teaches
 * that a whole class of action does not exist.  So a filter that matched none
 * of a non-empty catalog fails, and the failure prints the kinds that catalog
 * really carries plus, when the local cache knows better, the actor scope where
 * this kind does live.
 */
export const kindMatchedNothing = (
  ctx: LegalCtx,
  selector: string,
  scope: PageScope | null,
  present: ReadonlyArray<string>,
  total: number,
  revision: Revision | null
): Effect.Effect<PlayerError, PlayerError | LockTimeoutError, SessionStore> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const label = revision === null ? 'no revision' : revisionLabel(revision);
    const lines = [
      `legal --kind ${selector} matched none of the ${scalar(total)} actions ` +
        `in the ${scopeText(scope)} catalog at ${label}`,
    ];
    if (present.length > 0) lines.push(`kinds in that catalog: ${kindList(present)}`);
    const state = yield* store.readState(ctx.sessionPath, ctx.session);
    const scopes = yield* cachedKindScopes(
      state,
      selector,
      scope === null ? '' : scope.actor_id,
      kindSelectorMatches
    );
    if (scopes.length > 0) {
      lines.push(
        `${selector} is an actor-scoped kind; this seat holds it for ` +
          `${scopes.slice(0, 8).join(' ')} — read one with ` +
          `\`just legal --actor_id ${scopes[0]} --all\``
      );
    } else if (
      scope === null &&
      V2_PLAYER_SCOPED_KIND_PREFIXES.some((prefix) => selector.startsWith(prefix))
    ) {
      // The cache knowing nothing about this kind is exactly the state an agent
      // is in the first time it looks for it, so the scope is named from the
      // taxonomy rather than from what happens to be cached.
      const named = yield* playerScopeAlias(state);
      lines.push(
        `${selector} is enumerated only in your own player scope, never in ` +
          `the global catalog — read it with \`just legal --actor_id ${named} ` +
          `--kind ${selector} --all\``
      );
    } else if (scope === null && selector.startsWith(V2_RELATION_SCOPED_KIND_PREFIX)) {
      const named = yield* playerScopeAlias(state);
      lines.push(
        `${selector} is enumerated only against one relation — read it with ` +
          `\`just legal --actor_id ${named} --target_id RELATION_ID --all\`, ` +
          'taking RELATION_ID from a `just state --section diplomacy` row'
      );
    } else if (scope === null) {
      lines.push(
        'unit and city kinds are enumerated per actor: ' +
          '`just legal --actor_id ACTOR_ID --all`, or ' +
          '`just turn --decisions` for every actor at once'
      );
    }
    return playerError(lines.join('\n'));
  });

/** Refuse a malformed kind, naming the kinds this seat has actually read. */
export const unknownKind = (
  ctx: LegalCtx,
  selector: string
): Effect.Effect<PlayerError, PlayerError | LockTimeoutError, SessionStore> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const state = yield* store.readState(ctx.sessionPath, ctx.session);
    const kinds: string[] = [];
    for (const descriptor of cachedDescriptors(state)) {
      const key = descriptorKindKey(descriptor);
      if (!kinds.includes(key)) kinds.push(key);
    }
    const lines = [
      `legal --kind ${selector} is not an action kind; write the kind column ` +
        'exactly as it prints, either `family.kind` or `family.kind/operation`',
    ];
    lines.push(
      kinds.length > 0
        ? `kinds in this seat's cached catalog: ${kindList(kinds)}`
        : 'this seat has read no catalog yet; run `just turn --decisions` ' +
            'or `just legal --actor_id ACTOR_ID --all` first'
    );
    return playerError(lines.join('\n'));
  });
