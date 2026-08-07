/**
 * `play legal` — what this seat may actually do, and the `a1..aN` names for it.
 *
 * Ports `command_legal` (client.py:8280-8283) and `_legal_command`
 * (8288-8325), plus the argparse surface at 11746-11769.
 *
 * Three forms, and the flag matrix is the whole spec:
 *
 * - bare / `--actor_id` / `--cursor` — one server page, `--limit` 1..16;
 * - `--kind K --all` — every actor's catalog filtered to one class of action;
 * - `--actor_id A --all` — one actor's complete menu.
 *
 * The two `--all` forms read `--limit` as a *result window* 1..64 and accept
 * `--offset`; the single-page form accepts neither.  A `--kind` that matched
 * nothing is a refusal naming what does exist, never an affirmative empty page.
 */
import { Command, Options } from '@effect/cli';
import { Effect } from 'effect';
import {
  playerError,
  type DriftError,
  type LockTimeoutError,
  type PlayerError,
  type SessionMissingError,
  type V2ResponseError,
} from 'src/errors';
import { ACTION_KIND_SELECTOR_RE } from 'src/constants';
import { dualText, resolveDual, type DualSpelling } from 'src/options';
import { render, requestedScope } from 'src/render/primitives';
import { renderLegalCompact, renderLegalPage, catalogRenderDeps } from 'src/render/legal/page';
import { aliasMap } from 'src/services/aliases';
import { catalogEquivalence } from 'src/services/catalog-cache';
import { resolveAliasArguments } from 'src/services/alias-refresh';
import {
  drainLegalAll,
  kindMatchedNothing,
  legalPageFetcher,
  unknownKind,
} from 'src/services/legal-drain';
import { legalQuery, readLegalPage, type LegalCtx } from 'src/services/legal-query';
import { jsonRequested, printV2Json } from 'src/services/json-output';
import { cachedPhaseNote } from 'src/services/mirror';
import { PrivateFs } from 'src/services/private-fs';
import { SessionStore } from 'src/services/session-store';
import { V2Client } from 'src/services/v2-client';

export interface LegalOptions {
  readonly session: string;
  readonly actorId: DualSpelling<string>;
  readonly targetId: DualSpelling<string>;
  readonly limit: string;
  readonly cursor: string;
  readonly kind: string;
  readonly all: boolean;
  readonly offset: string;
  readonly full: boolean;
  readonly json: boolean;
}

export type LegalCommandError =
  | PlayerError
  | V2ResponseError
  | DriftError
  | LockTimeoutError
  | SessionMissingError;

const KIND_NEEDS_ALL = 'use --kind ACTION_KIND and --all together';

const ALL_NEEDS_SCOPE =
  'legal --all needs a scope: use `--kind ACTION_KIND --all` for one class of ' +
  'action, or `--actor_id ACTOR_ID [--target_id TARGET_ID] --all` for one ' +
  "actor's complete catalog";

const OFFSET_NEEDS_ALL = 'legal --offset requires --all with --kind or --actor_id';

/**
 * Lead a refusal with the phase fact when this seat's turn is not live.
 *
 * `_phase_aware_refusal` (client.py:10802-10817) sits on U18's row and has not
 * landed; the wrapper is private here so `legal` behaves as CPython does today.
 * Acting after the phase has ended looks exactly like a cold cache from inside
 * the resolver, and the cache-miss remedy then succeeds without fixing
 * anything — the agent re-enumerates, retries, and loops.
 */
const withPhaseAwareRefusal = <A, E extends { readonly message: string }, R>(
  sessionPath: string,
  body: Effect.Effect<A, E, R>
): Effect.Effect<A, E | PlayerError, R | PrivateFs> =>
  Effect.catchAll(
    body,
    (error): Effect.Effect<never, E | PlayerError, PrivateFs> =>
      Effect.flatMap(cachedPhaseNote(sessionPath), (note) =>
        Effect.fail<E | PlayerError>(
          note === '' || error.message.startsWith(note)
            ? error
            : playerError(`${note}\n${error.message}`)
        )
      )
  );

/** The `--limit` flag as CPython's argparse delivered it: a string or `None`. */
const optionalText = (value: string): string | null => (value === '' ? null : value);

export const runLegal = (
  options: LegalOptions
): Effect.Effect<
  void,
  LegalCommandError,
  SessionStore | PrivateFs | V2Client
> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const client = yield* V2Client;
    const { path: sessionPath, session } = yield* store.resolveV2(options.session);
    const ctx: LegalCtx = { sessionPath, session };

    return yield* withPhaseAwareRefusal(
      sessionPath,
      Effect.gen(function* () {
        const rawActor = yield* resolveDual('actor-id', options.actorId, '');
        const rawTarget = yield* resolveDual('target-id', options.targetId, '');
        const resolved = yield* resolveAliasArguments(
          sessionPath,
          session,
          { actor_id: rawActor, target_id: rawTarget },
          { fetch: legalPageFetcher(client) }
        );
        const actorId = (resolved.values['actor_id'] ?? '').trim();
        const targetId = (resolved.values['target_id'] ?? '').trim();
        const kind = options.kind.trim();

        if (kind !== '' && !options.all) {
          return yield* Effect.fail(playerError(KIND_NEEDS_ALL));
        }
        if (options.all && kind === '' && actorId === '') {
          return yield* Effect.fail(playerError(ALL_NEEDS_SCOPE));
        }
        if (kind !== '' && !ACTION_KIND_SELECTOR_RE.test(kind)) {
          return yield* Effect.flatMap(unknownKind(ctx, kind), Effect.fail);
        }
        if (options.all) {
          return yield* runLegalAll(ctx, options, actorId, targetId, kind);
        }
        if (options.offset !== '') {
          return yield* Effect.fail(playerError(OFFSET_NEEDS_ALL));
        }
        const cursor = options.cursor.trim();
        const query = yield* legalQuery({
          cursor: options.cursor,
          actorId,
          targetId,
          limit: optionalText(options.limit),
        });
        const value = yield* readLegalPage(ctx, { query, cursor, actorId, targetId });
        if (jsonRequested('legal', options.json)) {
          return yield* printV2Json(value);
        }
        const state = yield* store.readState(sessionPath, session);
        const aliases = yield* aliasMap(state);
        return yield* render(
          yield* renderLegalPage(value, aliases, { full: options.full })
        );
      })
    );
  });

const runLegalAll = (
  ctx: LegalCtx,
  options: LegalOptions,
  actorId: string,
  targetId: string,
  kind: string
): Effect.Effect<void, LegalCommandError, SessionStore | PrivateFs | V2Client> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const outcome = yield* store.withRequestLock(
      ctx.sessionPath,
      drainLegalAll(
        ctx,
        {
          cursor: options.cursor,
          actorId,
          targetId,
          limit: optionalText(options.limit),
          offset: options.offset,
        },
        kind
      )
    );
    const scope = requestedScope(actorId, targetId);
    if (outcome.matchedNothing) {
      return yield* Effect.flatMap(
        kindMatchedNothing(
          ctx,
          kind,
          scope,
          outcome.present,
          outcome.catalogTotal,
          outcome.revision
        ),
        Effect.fail
      );
    }
    const result = outcome.result;
    if (result === null) return yield* Effect.fail(playerError('legal drained nothing'));
    if (jsonRequested('legal', options.json)) {
      return yield* printV2Json(result);
    }
    const state = yield* store.readState(ctx.sessionPath, ctx.session);
    const aliases = yield* aliasMap(state);
    // An actor's whole catalog is the one that repeats across identical units;
    // a kind-filtered window is not a catalog and is never deduped.
    const equivalence =
      kind !== ''
        ? null
        : yield* catalogEquivalence(state, result, scope, aliases, catalogRenderDeps);
    return yield* render(
      yield* renderLegalCompact(result, scope, aliases, equivalence, { full: options.full })
    );
  });

// ---------------------------------------------------------------------------
// The flag surface
// ---------------------------------------------------------------------------

const textOption = (name: string, description: string): Options.Options<string> =>
  Options.text(name).pipe(Options.withDefault(''), Options.withDescription(description));

export const legalCommand = Command.make(
  'legal',
  {
    session: textOption('session', 'the private session file this command acts against'),
    actorId: dualText('actor-id'),
    targetId: dualText('target-id'),
    limit: textOption(
      'limit',
      'server page size 1..16, or compact result window 1..64 with --kind/--all'
    ),
    cursor: textOption('cursor', 'continue a paged enumeration'),
    kind: textOption('kind', 'one action kind, exactly as the kind column prints it'),
    all: Options.boolean('all').pipe(
      Options.withDescription('drain the whole catalog instead of one page')
    ),
    offset: textOption('offset', 'compact match offset 0..8192; requires --kind/--all'),
    full: Options.boolean('full').pipe(
      Options.withDescription('print the global catalog flat instead of grouped by kind')
    ),
    json: Options.boolean('json').pipe(
      Options.withDescription('print the full-fidelity JSON payload instead of text')
    ),
  },
  runLegal
);
