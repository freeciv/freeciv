/**
 * Carrying an `aN` across a revision bump.
 *
 * Ports `_refresh_stale_alias` (client.py:3275-3324),
 * `_expand_action_alias_refreshing` (3327-3341) and `_resolve_alias_arguments`
 * (3344-3383).
 *
 * The drain is the same scoped drain a manual `just legal --actor_id … --all`
 * performs, and the wire still only ever carries the fresh revision-bound
 * handle it returns.  Two rules make this safe:
 *
 * 1. **Continuity is by semantic identity alone.**  `a3` keeps naming the same
 *    move because the re-enumeration produced one action with the same
 *    `actionSemantics` string — never because the handle looked similar.
 * 2. **Ambiguity fails closed.**  An identity that now names two actions is a
 *    refusal, because picking either would be this client choosing the agent's
 *    move for it.
 *
 * The drain itself belongs to U11.  It arrives as an injected
 * {@link LegalPageFetcher} so this unit stays in wave 1; the *request-lock*
 * policy stays here, exactly where CPython kept it.
 */
import { Effect, Option } from 'effect';
import { ACTION_ALIAS_RE } from 'src/constants';
import { playerError, type LockTimeoutError, type PlayerError, type SessionMissingError } from 'src/errors';
import { revisionLabel } from 'src/render/primitives';
import { revisionsEqual } from 'src/schema/revision';
import {
  parseActionAliases,
  rebindActionAliases,
  type ActionAliasEntry,
} from 'src/services/aliases';
import { expandActionAlias, expandAlias, looksLikeAlias } from 'src/services/alias-expand';
import { PrivateFs } from 'src/services/private-fs';
import {
  SessionStore,
  type Session,
  type V2ClientState,
} from 'src/services/session-store';

// ---------------------------------------------------------------------------
// The U11 seam
// ---------------------------------------------------------------------------

/**
 * Enumerate one actor's whole catalog into `.v2-state`, holding no new lock.
 *
 * U11's `_drain_legal_unlocked`.  The caller already holds this seat's request
 * lock — the advisory lock is not reentrant — so the fetcher must never take
 * one.  Only the side effect matters: the refreshed aliases are read back off
 * the cache, never out of the return value.
 */
export type LegalPageFetcher = (
  sessionPath: string,
  session: Session,
  actorId: string
) => Effect.Effect<void, PlayerError | LockTimeoutError, SessionStore | PrivateFs>;

export interface RefreshOptions {
  /** True when this seat's request lock is already held by the caller. */
  readonly locked: boolean;
  readonly fetch: LegalPageFetcher;
}

/** A re-bound alias: the reloaded cache plus the one line to print. */
export interface AliasRebound {
  readonly state: V2ClientState;
  readonly note: string;
}

export type RefreshError = PlayerError | LockTimeoutError | SessionMissingError;

// ---------------------------------------------------------------------------
// _refresh_stale_alias
// ---------------------------------------------------------------------------

const byCodePoint = (left: string, right: string): number =>
  left < right ? -1 : left > right ? 1 : 0;

/**
 * Re-enumerate one stale action alias and re-bind it by what it means.
 *
 * Yields `none` when the plain refusal must stand: the semantic action is gone,
 * or the alias never carried an identity to re-resolve.
 */
export const refreshStaleAlias = (
  sessionPath: string,
  session: Session,
  state: V2ClientState,
  alias: string,
  options: RefreshOptions
): Effect.Effect<Option.Option<AliasRebound>, RefreshError, SessionStore | PrivateFs> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const table = yield* parseActionAliases(state.action_aliases);
    const entry = table.by_alias[alias];
    const recorded = table.state_revision;
    if (
      entry === undefined ||
      entry.semantics === '' ||
      recorded === null ||
      (state.last_revision !== null && revisionsEqual(recorded, state.last_revision))
    ) {
      return Option.none<AliasRebound>();
    }
    const previous: Readonly<Record<string, ActionAliasEntry>> = { ...table.by_alias };
    const semantics = entry.semantics;
    const drain = options.fetch(sessionPath, session, entry.actor_id);
    yield* options.locked ? drain : store.withRequestLock(sessionPath, drain);

    const fresh = yield* store.readState(sessionPath, session);
    const freshTable = yield* parseActionAliases(fresh.action_aliases);
    const matches = Object.entries(freshTable.by_alias)
      .filter(([, item]) => item.semantics === semantics)
      .map(([name]) => name)
      .sort(byCodePoint);
    if (matches.length === 0) return Option.none<AliasRebound>();
    if (matches.length > 1) {
      // Fail closed: two actions now answer to one identity, and picking either
      // would be this client choosing the agent's move for it.
      const label = fresh.last_revision === null ? 'no revision' : revisionLabel(fresh.last_revision);
      return yield* Effect.fail(
        playerError(
          `action alias ${alias} names ${matches.length} actions at ` +
            `${label} ` +
            `(${matches.join(' ')}); name exactly one of them`
        )
      );
    }
    const { table: rebound } = rebindActionAliases(freshTable, previous);
    const next: V2ClientState = {
      ...fresh,
      action_aliases: {
        state_revision:
          freshTable.state_revision === null
            ? null
            : {
                turn: freshTable.state_revision.turn,
                revision: freshTable.state_revision.revision,
                state_token: freshTable.state_revision.state_token,
              },
        by_alias: Object.fromEntries(
          Object.entries(rebound).map(([name, item]) => [
            name,
            { action_id: item.action_id, actor_id: item.actor_id, semantics: item.semantics },
          ])
        ),
      },
    };
    yield* store.writeState(sessionPath, next);
    const revision = next.last_revision;
    return Option.some({
      state: next,
      note: `${alias} rebound at rev${revision === null ? '?' : revision.revision}`,
    });
  });

// ---------------------------------------------------------------------------
// _expand_action_alias_refreshing
// ---------------------------------------------------------------------------

export interface ExpandedActionAlias {
  readonly identifier: string;
  readonly state: V2ClientState;
  /** `"a1 rebound at rev9"`, or `""` when nothing had to move. */
  readonly note: string;
}

/** Expand one action alias, carrying it across a revision bump if it can. */
export const expandActionAliasRefreshing = (
  sessionPath: string,
  session: Session,
  state: V2ClientState,
  alias: string,
  options: RefreshOptions
): Effect.Effect<ExpandedActionAlias, RefreshError, SessionStore | PrivateFs> =>
  Effect.catchTag(
    Effect.map(expandActionAlias(state, alias, sessionPath), (identifier) => ({
      identifier,
      state,
      note: '',
    })),
    'PlayerError',
    (original) =>
      Effect.gen(function* () {
        const rebound = yield* refreshStaleAlias(sessionPath, session, state, alias, options);
        if (Option.isNone(rebound)) return yield* Effect.fail(original);
        const identifier = yield* expandActionAlias(rebound.value.state, alias, sessionPath);
        return { identifier, state: rebound.value.state, note: rebound.value.note };
      })
  );

// ---------------------------------------------------------------------------
// _resolve_alias_arguments
// ---------------------------------------------------------------------------

export interface ResolveAliasOptions {
  /** `--no-refresh`: a stale action alias keeps its plain refusal. */
  readonly noRefresh?: boolean;
  /** Absent means the same thing as `--no-refresh`: no drain is possible. */
  readonly fetch?: LegalPageFetcher | undefined;
}

export interface ResolvedAliasArguments {
  /** The same field map with every alias-shaped value replaced by its ID. */
  readonly values: Readonly<Record<string, string>>;
  /** `"a1 rebound at rev9"` lines, in the order the fields were resolved. */
  readonly notes: ReadonlyArray<string>;
}

/**
 * Expand every alias-shaped ID argument before a request is built.
 *
 * Nothing downstream of this call can see an alias.  The cache is only opened
 * when an alias is actually present, so a command that already names opaque IDs
 * behaves exactly as it did before — and makes exactly as many requests.  A
 * stale *action* alias is re-bound by semantic identity unless the caller
 * passed `--no-refresh`, in which case the plain refusal stands.
 */
export const resolveAliasArguments = (
  sessionPath: string,
  session: Session,
  values: Readonly<Record<string, string>>,
  options: ResolveAliasOptions = {}
): Effect.Effect<ResolvedAliasArguments, RefreshError, SessionStore | PrivateFs> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const raw = Object.fromEntries(
      Object.entries(values).map(([name, text]) => [name, (text ?? '').trim()])
    );
    if (!Object.values(raw).some((text) => looksLikeAlias(text))) {
      return { values, notes: [] };
    }
    const fetch = options.fetch;
    const refresh = options.noRefresh !== true && fetch !== undefined;
    const resolved: Record<string, string> = { ...values };
    const notes: string[] = [];
    let state = yield* store.readState(sessionPath, session);
    for (const [name, text] of Object.entries(raw)) {
      if (!looksLikeAlias(text)) continue;
      if (refresh && fetch !== undefined && ACTION_ALIAS_RE.test(text)) {
        const outcome = yield* expandActionAliasRefreshing(sessionPath, session, state, text, {
          locked: false,
          fetch,
        });
        if (outcome.note !== '') notes.push(outcome.note);
        state = outcome.state;
        resolved[name] = outcome.identifier;
        continue;
      }
      resolved[name] = yield* expandAlias(state, text, sessionPath);
    }
    return { values: resolved, notes };
  });
