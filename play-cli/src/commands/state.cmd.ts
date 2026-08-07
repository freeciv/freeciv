/**
 * `play state` — one page of the live game state, rendered.
 *
 * Ports `command_state` (client.py:7790-7814) and the `state` parser block
 * (11734-11744).
 *
 * The flow is fixed and every step of it is observable: expand aliases, build
 * the query (refusing a combination the section does not accept), GET `/state`,
 * validate, ingest into the alias/catalog cache, project into the mirror, then
 * render.  The ingest happens before the render because the render addresses
 * rows by the aliases this very page just taught.
 */
import { Command, Options } from '@effect/cli';
import { Effect, Option } from 'effect';
import type {
  DriftError,
  LockTimeoutError,
  PlayerError,
  SessionMissingError,
  V2ResponseError,
} from 'src/errors';
import { dualText, resolveDual, type DualSpelling } from 'src/options';
import { render } from 'src/render/primitives';
import { renderStatePage } from 'src/render/state/page';
import { decodePage, type PageEnvelope } from 'src/schema/page';
import { isJsonObject } from 'src/schema/primitives';
import { aliasMap, rememberPage } from 'src/services/aliases';
import { resolveAliasArguments } from 'src/services/alias-refresh';
import { jsonRequested, printV2Json } from 'src/services/json-output';
import { mirrorPage } from 'src/services/mirror/update-page';
import { dropPendingForCursor } from 'src/services/pending-catalogs';
import { PrivateFs } from 'src/services/private-fs';
import { SessionStore, credentialsOf, type Session } from 'src/services/session-store';
import { stateQuery } from 'src/services/state-query';
import { V2Client } from 'src/services/v2-client';

/** Ten seconds, exactly as `command_state` passes to `_v2_response`. */
export const STATE_TIMEOUT_S = 10;

export interface StateOptions {
  readonly session: string;
  readonly section: string;
  readonly actorId: DualSpelling<string>;
  readonly relationId: DualSpelling<string>;
  readonly centerId: DualSpelling<string>;
  readonly radius: Option.Option<number>;
  readonly limit: string;
  readonly cursor: string;
  readonly json: boolean;
}

export type StateError =
  | PlayerError
  | V2ResponseError
  | DriftError
  | SessionMissingError
  | LockTimeoutError;

/** Did this refusal say the cursor expired?  Then the staged catalog is dead. */
const isCursorExpired = (error: V2ResponseError): boolean => {
  const payload = error.payload;
  const body = isJsonObject(payload) ? (payload['error'] ?? null) : null;
  return isJsonObject(body) && body['code'] === 'cursor_expired';
};

/** GET `/state`, forgetting the staged catalog when the cursor is refused. */
export const fetchStatePage = (
  sessionPath: string,
  session: Session,
  query: string,
  cursor: string
): Effect.Effect<
  PageEnvelope,
  StateError,
  V2Client | SessionStore | PrivateFs
> =>
  Effect.gen(function* () {
    const client = yield* V2Client;
    const credentials = credentialsOf(session);
    const base = yield* client.url(credentials, '/state');
    const url = query === '' ? base : `${base}?${query}`;
    const response = yield* client.response('GET', url, credentials, {
      timeout: STATE_TIMEOUT_S,
    });
    if (response.status < 200 || response.status >= 300) {
      return yield* Effect.catchTag(
        client.raiseValidated(response),
        'V2ResponseError',
        (error) =>
          isCursorExpired(error) && cursor !== ''
            ? Effect.zipRight(
                dropPendingForCursor(sessionPath, session, cursor),
                Effect.fail(error)
              )
            : Effect.fail(error)
      );
    }
    return yield* decodePage(response.value, session);
  });

export const runState = (
  options: StateOptions
): Effect.Effect<void, StateError, SessionStore | V2Client | PrivateFs> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const { path, session } = yield* store.resolveV2(options.session);
    const actorId = yield* resolveDual('actor-id', options.actorId, '');
    const relationId = yield* resolveDual('relation-id', options.relationId, '');
    const centerId = yield* resolveDual('center-id', options.centerId, '');
    // NOTE(U11): `_resolve_alias_arguments` re-binds a stale `aN` by draining a
    // legal page.  `state` never names an action alias, and U11's fetcher has
    // not landed, so no fetcher is injected: an `aN` here keeps its plain
    // refusal.  See NOTES §U10.
    const resolved = yield* resolveAliasArguments(path, session, {
      actor_id: actorId,
      relation_id: relationId,
      center_id: centerId,
    });
    const query = yield* stateQuery({
      section: options.section,
      actorId: resolved.values['actor_id'] ?? '',
      relationId: resolved.values['relation_id'] ?? '',
      centerId: resolved.values['center_id'] ?? '',
      radius: Option.getOrNull(options.radius),
      limit: options.limit === '' ? null : options.limit,
      cursor: options.cursor,
    });
    const value = yield* fetchStatePage(path, session, query, options.cursor.trim());
    const remembered = yield* rememberPage(path, session, { legal: false, page: value });
    // `_mirror_page(path, state, value, "state")` (client.py:7809).  The alias
    // map is read from the state this page has already been remembered into, so
    // the mirror files name rows exactly the way the render below does.  It runs
    // before the `--json` branch because CPython projects on both paths, and
    // before the render because `city_build_choices` reads back the `shields`
    // cell a preceding `--section cities` wrote.
    const aliases = yield* aliasMap(remembered.state);
    yield* mirrorPage(path, value, 'state', { aliases });
    if (jsonRequested('state', options.json)) {
      yield* printV2Json(value);
    } else {
      yield* render(
        yield* renderStatePage(value, {
          aliases,
          sessionPath: path,
          state: remembered.state,
        })
      );
    }
  });

const textOption = (name: string, fallback = ''): Options.Options<string> =>
  Options.text(name).pipe(Options.withDefault(fallback));

export const stateCommand = Command.make(
  'state',
  {
    session: textOption('session'),
    section: textOption('section'),
    actorId: dualText('actor-id'),
    relationId: dualText('relation-id'),
    centerId: dualText('center-id'),
    radius: Options.optional(Options.integer('radius')),
    limit: textOption('limit'),
    cursor: textOption('cursor'),
    json: Options.boolean('json').pipe(
      Options.withDescription('print the full-fidelity JSON payload instead of text')
    ),
  },
  runState
);
