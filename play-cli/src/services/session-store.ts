/**
 * Sessions, the workspace seat binding and the `.v2-state` cache.
 *
 * Ports `_load_object` (client.py:727-734), `_game_id` (737-740),
 * `_controller_name` (743-755), `_session_key` (758-761),
 * `_set_current_session` (803-806), `_seat_binding_path` (809-810),
 * `_private_sessions` (813-825), `_read_seat_binding` (828-865),
 * `_bind_workspace_seat` (868-895), `_preconfigured_game_id` (914-932),
 * `_no_session_error` (935-950), `_session_path` (953-1007),
 * `_load_session` (1000-1007), `_v2_session` (1059-1082), `_v2_state_path`
 * (1085-1086) and the `.v2-state` load/save/migrate block (1115-1246).
 *
 * `_seat_binding_line` and `_validate_evaluation_context` belong to U02 and to
 * the schema layer respectively; the alias/pending/drained validators and the
 * empty-state shape belong to U03 and arrive through {@link V2StateSchema}, so
 * this file never reaches into a unit.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { Context, Effect, Layer, Option } from 'effect';
import {
  LockTimeoutError,
  PlayerError,
  SessionMissingError,
  playerError,
  attemptOr,
} from 'src/errors';
import {
  CONTROLLER_RE,
  FULL_CONTROL_V2,
  GAME_ID_RE,
  OPAQUE_ID_RE,
  SEAT_BINDING_NAME,
  STRATEGIC_V1,
} from 'src/constants';
import {
  decodeEvaluationContext,
  isJsonObject,
  type EvaluationContext,
  type JsonObject,
  type JsonValue,
  type SessionIdentity,
} from 'src/schema/primitives';
import { decodeRevision, type Revision } from 'src/schema/revision';
import { serviceUrl } from 'src/services/http';
import type { V2Credentials } from 'src/services/v2-client';
import {
  PrivateFs,
  Workspace,
  type PrivateFsApi,
  type WorkspacePaths,
} from 'src/services/private-fs';
import {
  v2StatePath,
  withV2RequestLock,
  withV2StateLock,
} from 'src/services/locks';

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export interface Session extends SessionIdentity {
  readonly gameId: string;
  readonly agentId: string;
  readonly agentToken: string;
  readonly serviceUrl: string;
  readonly controllerLabel: string;
  readonly controlProtocol: string;
  readonly place: number | null;
  readonly seatId: string | null;
  readonly playerName: string | null;
  readonly evaluation: EvaluationContext | null;
  /** The exact on-disk object, so a rewrite never drops a field it did not read. */
  readonly raw: JsonObject;
}

export interface LoadedSession {
  readonly path: string;
  readonly session: Session;
}

export interface SeatBinding {
  readonly gameId: string;
  /** Absolute path of the bound session file. */
  readonly session: string;
  /** Its path relative to the state root, exactly as written on disk. */
  readonly relative: string;
  readonly boundAt: string;
}

export interface V2ClientState {
  readonly schema_version: number;
  readonly game_id: string;
  readonly agent_id: string;
  readonly last_revision: Revision | null;
  readonly actions: Record<string, JsonValue>;
  readonly pending_catalogs: Record<string, JsonValue>;
  /** batch_id → the canonical request body, as the exact bytes first written. */
  readonly batches: Record<string, JsonValue>;
  readonly receipts: Record<string, JsonValue>;
  readonly action_aliases: JsonObject;
  readonly entity_aliases: Record<string, JsonValue>;
  readonly tile_aliases: Record<string, JsonValue>;
  readonly drained_actors: ReadonlyArray<JsonValue>;
}

/** The v2 credentials a session authorizes. */
export const credentialsOf = (session: Session): V2Credentials => ({
  gameId: session.gameId,
  agentToken: session.agentToken,
  serviceUrl: session.serviceUrl,
});

// ---------------------------------------------------------------------------
// The U03 seam
// ---------------------------------------------------------------------------

/**
 * The parts of `.v2-state` U03 owns.
 *
 * Core loads, migrates and saves the file; U03 decides what an empty cache
 * looks like and proves the alias, pending-catalog and drained-actor tables are
 * closed before anything is allowed to resolve against them.  Until U03 lands,
 * {@link V2StateSchemaDefault} supplies the shape and skips the alias proofs —
 * which is safe only because nothing reads aliases yet.
 */
export interface V2StateSchemaApi {
  readonly empty: (session: Session) => V2ClientState;
  readonly validate: (state: V2ClientState) => Effect.Effect<void, PlayerError>;
  /** `_cursor_expired` — a pending catalog whose cursor has retired. */
  readonly cursorExpired: (expiresAt: string | null) => boolean;
}

export class V2StateSchema extends Context.Tag('V2StateSchema')<
  V2StateSchema,
  V2StateSchemaApi
>() {}

const emptyActionAliases = (): JsonObject => ({ state_revision: null, by_alias: {} });

export const emptyV2ClientState = (session: Session): V2ClientState => ({
  schema_version: 5,
  game_id: session.gameId,
  agent_id: session.agentId,
  last_revision: null,
  actions: {},
  pending_catalogs: {},
  batches: {},
  receipts: {},
  // Revision-scoped: an action alias is only ever resolvable while its recorded
  // revision is still the newest revision this client knows.
  action_aliases: emptyActionAliases(),
  // Game-stable: entity IDs survive revisions, so u1/c1 are assigned once in
  // first-seen order and never re-point at a different entity.
  entity_aliases: {},
  tile_aliases: {},
  // Revision-scoped, like `actions`: the actors whose complete catalog this
  // client drained and promoted at `last_revision`.
  drained_actors: [],
});

export const V2StateSchemaDefault: Layer.Layer<V2StateSchema> = Layer.succeed(V2StateSchema, {
  empty: emptyV2ClientState,
  validate: () => Effect.void,
  cursorExpired: (expiresAt) =>
    expiresAt === null ? false : Date.parse(expiresAt) <= Date.now(),
});

// ---------------------------------------------------------------------------
// Small validators
// ---------------------------------------------------------------------------

export const gameId = (value: string): Effect.Effect<string, PlayerError> =>
  GAME_ID_RE.test(value)
    ? Effect.succeed(value)
    : Effect.fail(playerError('a valid assigned game ID is required'));

const GENERIC_CONTROLLERS: ReadonlySet<string> = new Set(['agent', 'harness-model']);

export const controllerName = (value: string): Effect.Effect<string, PlayerError> =>
  CONTROLLER_RE.test(value) &&
  value.includes('-') &&
  !value.startsWith('-') &&
  !value.endsWith('-') &&
  !GENERIC_CONTROLLERS.has(value.toLowerCase())
    ? Effect.succeed(value)
    : Effect.fail(
        playerError(
          '--name must be a truthful non-generic harness-model label, ' +
            'for example codex-gpt-5.6-sol or claude-code-claude-opus'
        )
      );

/** `_session_key` — the stable file name a controller's session lives under. */
export const sessionKey = (controller: string): string => {
  const slug = controller
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '');
  const digest = new Bun.CryptoHasher('sha256').update(controller, 'utf8').digest('hex');
  return `${slug === '' ? 'controller' : slug}-${digest.slice(0, 12)}`;
};

// ---------------------------------------------------------------------------
// Session decoding
// ---------------------------------------------------------------------------

const readNumber = (raw: JsonObject, key: string): number | null => {
  const value = raw[key];
  return typeof value === 'number' ? value : null;
};

const readString = (raw: JsonObject, key: string): string | null => {
  const value = raw[key];
  return typeof value === 'string' ? value : null;
};

const decodeSession = (raw: JsonObject, url: string): Session => ({
  gameId: readString(raw, 'game_id') ?? '',
  agentId: readString(raw, 'agent_id') ?? '',
  agentToken: readString(raw, 'agent_token') ?? '',
  serviceUrl: url,
  controllerLabel: readString(raw, 'controller_label') ?? '',
  controlProtocol: readString(raw, 'control_protocol') ?? STRATEGIC_V1,
  place: readNumber(raw, 'place'),
  seatId: readString(raw, 'seat_id'),
  playerName: readString(raw, 'player_name'),
  evaluation: null,
  raw,
});

// ---------------------------------------------------------------------------
// The service
// ---------------------------------------------------------------------------

export interface SessionStoreApi {
  readonly workspace: WorkspacePaths;
  readonly seatBindingPath: string;
  /** `_session_path` — which session file this invocation acts against. */
  readonly sessionPath: (explicit: string) => Effect.Effect<string, PlayerError | SessionMissingError>;
  /** `_load_session` — a strategic-v1-or-later session, minimally checked. */
  readonly resolve: (explicit: string) => Effect.Effect<LoadedSession, PlayerError | SessionMissingError>;
  /** `_v2_session` — a session proved to be full-control-v2 and complete. */
  readonly resolveV2: (explicit: string) => Effect.Effect<LoadedSession, PlayerError | SessionMissingError>;
  readonly listSessions: (gameId?: string) => Effect.Effect<ReadonlyArray<string>, PlayerError>;
  readonly readSeatBinding: () => Effect.Effect<Option.Option<SeatBinding>, PlayerError>;
  /** Bind this workspace to one seat; yields the binding it replaced. */
  readonly bindWorkspaceSeat: (
    sessionPath: string,
    gameId: string
  ) => Effect.Effect<Option.Option<SeatBinding>, PlayerError>;
  readonly setCurrentSession: (sessionPath: string) => Effect.Effect<void, PlayerError>;
  /** The game `just play` pinned this workspace to, if it did. */
  readonly preconfiguredGameId: () => Effect.Effect<Option.Option<string>>;
  readonly writeSession: (
    sessionPath: string,
    value: JsonObject
  ) => Effect.Effect<string, PlayerError>;
  readonly statePath: (sessionPath: string) => string;
  readonly readState: (
    sessionPath: string,
    session: Session
  ) => Effect.Effect<V2ClientState, PlayerError | LockTimeoutError>;
  readonly writeState: (
    sessionPath: string,
    next: V2ClientState
  ) => Effect.Effect<void, PlayerError | LockTimeoutError>;
  readonly withStateLock: <A, E, R>(
    sessionPath: string,
    session: Session,
    body: (state: V2ClientState) => Effect.Effect<A, E, R>
  ) => Effect.Effect<A, E | PlayerError | LockTimeoutError, R>;
  readonly withRequestLock: <A, E, R>(
    sessionPath: string,
    body: Effect.Effect<A, E, R>
  ) => Effect.Effect<A, E | PlayerError | LockTimeoutError, R>;
}

export class SessionStore extends Context.Tag('SessionStore')<
  SessionStore,
  SessionStoreApi
>() {}

const SEAT_BINDING_MALFORMED =
  'the workspace seat binding is unreadable. Rebind this workspace ' +
  `with \`just use GAME_ID\`, or delete .sessions/${SEAT_BINDING_NAME}.`;

const MULTIPLE_SESSIONS =
  'multiple private sessions exist in this player workspace and none of them ' +
  'is bound to it. Bind the seat you are playing with `just use GAME_ID` ' +
  '(or `just use SESSION_FILE`) and no later command needs --session; ' +
  '`just join` binds it for you.';

const LEGACY_FIELDS = [
  'schema_version',
  'game_id',
  'agent_id',
  'last_revision',
  'actions',
  'batches',
  'receipts',
] as const;

const sameKeys = (value: JsonObject, expected: ReadonlySet<string>): boolean => {
  const present = Object.keys(value);
  return present.length === expected.size && present.every((key) => expected.has(key));
};

const makeApi = (
  workspace: WorkspacePaths,
  files: PrivateFsApi,
  schema: V2StateSchemaApi,
  runLocked: <A, E, R>(
    sessionPath: string,
    body: Effect.Effect<A, E, R>
  ) => Effect.Effect<A, E | PlayerError | LockTimeoutError, R>,
  runRequestLocked: <A, E, R>(
    sessionPath: string,
    body: Effect.Effect<A, E, R>
  ) => Effect.Effect<A, E | PlayerError | LockTimeoutError, R>,
  environment: Readonly<Record<string, string | undefined>>
): SessionStoreApi => {
  const seatBindingPath = path.join(workspace.stateRoot, SEAT_BINDING_NAME);
  const currentPointerPath = path.join(workspace.stateRoot, 'current');

  const listSessions = (
    game = ''
  ): Effect.Effect<ReadonlyArray<string>, PlayerError> =>
    Effect.sync(() => {
      const directories = ((): ReadonlyArray<string> => {
        if (game !== '') return [game];
        return attemptOr(
          (): ReadonlyArray<string> =>
            fs
              .readdirSync(workspace.stateRoot, { withFileTypes: true })
              .filter((entry) => entry.isDirectory() && entry.name.startsWith('game_'))
              .map((entry) => entry.name),
          (): ReadonlyArray<string> => []
        );
      })();
      const found = directories.flatMap((directory) => {
        const absolute = path.join(workspace.stateRoot, directory);
        const names = attemptOr(
          (): ReadonlyArray<string> => fs.readdirSync(absolute),
          (): ReadonlyArray<string> => []
        );
        return names
          .filter((name) => name.endsWith('.json'))
          .map((name) => path.join(absolute, name))
          .filter((candidate) =>
            attemptOr(
              () => {
                const stat = fs.lstatSync(candidate);
                return stat.isFile() && (stat.mode & 0o777) === 0o600;
              },
              // A session that vanished mid-scan is simply not a session.
              () => false
            )
          );
      });
      return [...found].sort();
    });

  const readSeatBinding = (): Effect.Effect<Option.Option<SeatBinding>, PlayerError> =>
    Effect.gen(function* () {
      const present = yield* files.exists(seatBindingPath);
      if (!present) return Option.none<SeatBinding>();
      const value = yield* Effect.catchAll(
        files.loadObject(seatBindingPath, 'workspace seat binding'),
        (error) => Effect.fail(playerError(`${error.message}. ${SEAT_BINDING_MALFORMED}`))
      );
      const game = value['game_id'];
      const relative = value['session'];
      if (
        typeof game !== 'string' ||
        !GAME_ID_RE.test(game) ||
        typeof relative !== 'string' ||
        relative === ''
      ) {
        return yield* Effect.fail(playerError(SEAT_BINDING_MALFORMED));
      }
      const parts = relative.split(/[\\/]/);
      if (path.isAbsolute(relative) || parts.includes('..')) {
        return yield* Effect.fail(playerError(SEAT_BINDING_MALFORMED));
      }
      const resolved = yield* Effect.catchAll(
        files.resolve(path.join(workspace.stateRoot, relative)),
        () => Effect.fail(playerError(SEAT_BINDING_MALFORMED))
      );
      const boundAt = value['bound_at'];
      return Option.some({
        gameId: game,
        session: resolved.destination,
        relative,
        boundAt: typeof boundAt === 'string' ? boundAt : '',
      });
    });

  const preconfiguredGameId = (): Effect.Effect<Option.Option<string>> =>
    Effect.sync(() =>
      attemptOr(
        () => {
          const raw: unknown = JSON.parse(
            fs.readFileSync(path.join(workspace.root, '.playconfig.json'), 'utf8')
          );
          const value = isJsonObject(raw) ? raw['game_id'] : null;
          return typeof value === 'string' && GAME_ID_RE.test(value)
            ? Option.some(value)
            : Option.none<string>();
        },
        () => Option.none<string>()
      )
    );

  const noSessionError = (): Effect.Effect<never, SessionMissingError> =>
    Effect.flatMap(preconfiguredGameId(), (game) =>
      Effect.fail(
        new SessionMissingError({
          message: Option.isSome(game)
            ? `run \`just join\` first — this workspace is pre-configured for ${game.value}, ` +
              'and every other command needs the seat it creates'
            : 'no current session; run `just join --game_id ... --name ...` first',
        })
      )
    );

  const sessionPath = (
    explicit: string
  ): Effect.Effect<string, PlayerError | SessionMissingError> =>
    Effect.gen(function* () {
      const value = explicit.trim() || (environment['PLAY_SESSION'] ?? '').trim();
      if (value !== '') {
        const candidate = path.isAbsolute(value) ? value : path.join(workspace.root, value);
        const resolved = yield* files.resolve(candidate);
        return resolved.destination;
      }
      const binding = yield* readSeatBinding();
      if (Option.isSome(binding)) {
        // A binding whose seat file is gone is stale, not authoritative: fall
        // through, so the rules below either find the one real seat or name the
        // command that rebinds this workspace.
        const alive = yield* files.exists(binding.value.session);
        if (alive) return binding.value.session;
      }
      const sessions = yield* listSessions();
      if (sessions.length > 1) {
        return yield* Effect.fail(playerError(MULTIPLE_SESSIONS));
      }
      if (sessions.length === 1) return sessions[0] as string;
      const pointer = yield* Effect.sync(() => {
        return attemptOr(
            () => fs.readFileSync(currentPointerPath, 'utf8').trim(),
            () => null
        );
      });
      if (pointer === null) return yield* noSessionError();
      if (path.isAbsolute(pointer) || pointer.split(/[\\/]/).includes('..')) {
        return yield* Effect.fail(playerError('the current-session pointer is invalid'));
      }
      const resolved = yield* files.resolve(path.join(workspace.stateRoot, pointer));
      return resolved.destination;
    });

  const loadSessionAt = (target: string): Effect.Effect<Session, PlayerError> =>
    Effect.gen(function* () {
      const raw = yield* files.loadObject(target, 'session');
      const required = ['game_id', 'agent_id', 'agent_token', 'service_url'] as const;
      if (
        required.some((name) => !(name in raw)) ||
        typeof raw['agent_token'] !== 'string'
      ) {
        return yield* Effect.fail(playerError(`session ${target} is incomplete`));
      }
      const url = yield* serviceUrl(
        typeof raw['service_url'] === 'string' ? raw['service_url'] : undefined,
        environment
      );
      return decodeSession(raw, url);
    });

  const resolve = (
    explicit: string
  ): Effect.Effect<LoadedSession, PlayerError | SessionMissingError> =>
    Effect.gen(function* () {
      const target = yield* sessionPath(explicit);
      return { path: target, session: yield* loadSessionAt(target) };
    });

  const resolveV2 = (
    explicit: string
  ): Effect.Effect<LoadedSession, PlayerError | SessionMissingError> =>
    Effect.gen(function* () {
      const loaded = yield* resolve(explicit);
      const raw = loaded.session.raw;
      if (loaded.session.controlProtocol !== FULL_CONTROL_V2) {
        return yield* Effect.fail(playerError('this command is full-control-v2 only'));
      }
      if (raw['schema_version'] !== 1) {
        return yield* Effect.fail(playerError('the private session has an unsupported schema'));
      }
      const session = loaded.session;
      if (
        !GAME_ID_RE.test(session.gameId) ||
        !OPAQUE_ID_RE.test(session.agentId) ||
        session.agentToken === '' ||
        session.controllerLabel === ''
      ) {
        return yield* Effect.fail(
          playerError('the private full-control-v2 session is incomplete')
        );
      }
      const evaluation = yield* Effect.mapError(
        decodeEvaluationContext(raw, 'private v2 session'),
        (error) => playerError(error.message)
      );
      return { path: loaded.path, session: { ...session, evaluation } };
    });

  // -------------------------------------------------------------------------
  // .v2-state
  // -------------------------------------------------------------------------

  const statePath = (target: string): string => v2StatePath(target);

  const asState = (value: JsonObject): V2ClientState => value as unknown as V2ClientState;

  const readStateUnlocked = (
    target: string,
    session: Session
  ): Effect.Effect<V2ClientState, PlayerError> =>
    Effect.gen(function* () {
      const file = statePath(target);
      const present = yield* files.exists(file);
      if (!present) return schema.empty(session);
      const value = yield* files.loadObject(file, 'v2 client state');

      const legacyFields = new Set<string>(LEGACY_FIELDS);
      const stagedFields = new Set<string>([...legacyFields, 'pending_catalogs']);
      const aliasedFields = new Set<string>([
        ...stagedFields,
        'action_aliases',
        'entity_aliases',
        'tile_aliases',
      ]);
      const currentFields = new Set<string>([...aliasedFields, 'drained_actors']);
      const version = value['schema_version'];
      const legacy = sameKeys(value, legacyFields) && version === 1;
      const staged = sameKeys(value, stagedFields) && version === 2;
      const aliased = sameKeys(value, aliasedFields) && version === 3;
      const drained = sameKeys(value, currentFields) && version === 4;
      const current = sameKeys(value, currentFields) && version === 5;
      const invalidState = playerError(`private v2 client state ${file} is invalid`);
      if (
        !(legacy || staged || aliased || drained || current) ||
        value['game_id'] !== session.gameId ||
        value['agent_id'] !== session.agentId ||
        !isJsonObject(value['actions']) ||
        !isJsonObject(value['batches']) ||
        !isJsonObject(value['receipts']) ||
        ((staged || aliased || drained || current) && !isJsonObject(value['pending_catalogs']))
      ) {
        return yield* Effect.fail(invalidState);
      }
      if (value['last_revision'] !== null) {
        yield* Effect.mapError(decodeRevision(value['last_revision']), () => invalidState);
      }
      // Persisted request bodies are strings specifically so retry can send the
      // exact bytes written before the first POST.
      const batches = value['batches'] as JsonObject;
      const badBatch = Object.entries(batches).some(
        ([batchId, body]) => !OPAQUE_ID_RE.test(batchId) || typeof body !== 'string'
      );
      if (badBatch) return yield* Effect.fail(invalidState);

      if (legacy) {
        // A v1 cache cannot prove whether a scoped descriptor came from a
        // complete native catalog.  Preserve durable batches and receipts, but
        // fail closed by dropping every executable action during migration.
        const migrated: V2ClientState = {
          ...schema.empty(session),
          last_revision: (value['last_revision'] ?? null) as Revision | null,
          batches: { ...batches },
          receipts: { ...(value['receipts'] as JsonObject) },
        };
        yield* files.writeJson(file, migrated);
        return migrated;
      }

      const upgraded = ((): JsonObject => {
        if (!(staged || aliased || drained)) return value;
        // A v2 cache predates the alias dialect, a v3 cache predates the
        // drained-catalog record, and a v4 cache numbered its aliases without
        // recording what each one *means*.  None holds anything unsound.
        const next: Record<string, JsonValue> = { ...value, schema_version: 5 };
        if (staged) {
          next['action_aliases'] = emptyActionAliases();
          next['entity_aliases'] = {};
          next['tile_aliases'] = {};
        } else {
          const table = value['action_aliases'];
          if (
            isJsonObject(table) &&
            sameKeys(table, new Set(['state_revision', 'by_alias'])) &&
            isJsonObject(table['by_alias']) &&
            Object.values(table['by_alias']).every((entry) => isJsonObject(entry))
          ) {
            const byAlias = table['by_alias'];
            const rebuilt: Record<string, JsonValue> = {};
            for (const [alias, entry] of Object.entries(byAlias)) {
              rebuilt[alias] = { semantics: '', ...(entry as JsonObject) };
            }
            next['action_aliases'] = {
              state_revision: table['state_revision'] ?? null,
              by_alias: rebuilt,
            };
          }
        }
        if (!drained) next['drained_actors'] = [];
        return next;
      })();
      if (upgraded !== value) {
        yield* files.writeJson(file, upgraded);
      }

      const state = asState(upgraded);
      yield* schema.validate(state);

      const pending = upgraded['pending_catalogs'];
      if (isJsonObject(pending)) {
        const expired = Object.entries(pending).filter(([, entry]) => {
          const expiry = isJsonObject(entry) ? entry['cursor_expires_at'] : null;
          return schema.cursorExpired(typeof expiry === 'string' ? expiry : null);
        });
        if (expired.length > 0) {
          const kept: Record<string, JsonValue> = {};
          for (const [catalogId, entry] of Object.entries(pending)) {
            if (!expired.some(([expiredId]) => expiredId === catalogId)) kept[catalogId] = entry;
          }
          const next: V2ClientState = { ...state, pending_catalogs: kept };
          yield* files.writeJson(file, next);
          return next;
        }
      }
      return state;
    });

  const writeStateUnlocked = (
    target: string,
    next: V2ClientState
  ): Effect.Effect<void, PlayerError> =>
    Effect.asVoid(files.writeJson(statePath(target), next));

  return {
    workspace,
    seatBindingPath,
    sessionPath,
    resolve,
    resolveV2,
    listSessions,
    readSeatBinding,
    bindWorkspaceSeat: (target, game) =>
      Effect.gen(function* () {
        const relative = yield* files.regularFile(target, 'session');
        // A binding this client cannot read is exactly what rebinding repairs,
        // so it must never block the command that repairs it.
        const previous = yield* Effect.orElseSucceed(readSeatBinding(), () =>
          Option.none<SeatBinding>()
        );
        const validated = yield* gameId(game);
        yield* files.writeJson(seatBindingPath, {
          schema_version: 1,
          game_id: validated,
          session: relative,
          bound_at: new Date().toISOString().replace(/\.\d{3}Z$/, 'Z'),
        });
        if (Option.isSome(previous) && previous.value.relative === relative) {
          return Option.none<SeatBinding>();
        }
        return previous;
      }),
    setCurrentSession: (target) =>
      Effect.gen(function* () {
        const relative = yield* files.regularFile(target, 'current session');
        yield* files.writeText(currentPointerPath, `${relative}\n`);
      }),
    preconfiguredGameId,
    writeSession: (target, value) => files.writeJson(target, value),
    statePath,
    readState: (target, session) => runLocked(target, readStateUnlocked(target, session)),
    writeState: (target, next) => runLocked(target, writeStateUnlocked(target, next)),
    withStateLock: (target, session, body) =>
      runLocked(
        target,
        Effect.flatMap(readStateUnlocked(target, session), (state) => body(state))
      ),
    withRequestLock: (target, body) => runRequestLocked(target, body),
  };
};

export const SessionStoreLive: Layer.Layer<
  SessionStore,
  never,
  Workspace | PrivateFs | V2StateSchema
> = Layer.effect(
  SessionStore,
  Effect.gen(function* () {
    const workspace = yield* Workspace;
    const files = yield* PrivateFs;
    const schema = yield* V2StateSchema;
    const provided = Layer.succeed(PrivateFs, files);
    return makeApi(
      workspace,
      files,
      schema,
      (target, body) => Effect.provide(withV2StateLock(target, body), provided),
      (target, body) => Effect.provide(withV2RequestLock(target, body), provided),
      process.env
    );
  })
);

/** Build the API directly, for tests that supply their own workspace. */
export const sessionStoreFor = (
  workspace: WorkspacePaths,
  files: PrivateFsApi,
  schema: V2StateSchemaApi,
  environment: Readonly<Record<string, string | undefined>> = process.env
): SessionStoreApi => {
  const provided = Layer.succeed(PrivateFs, files);
  return makeApi(
    workspace,
    files,
    schema,
    (target, body) => Effect.provide(withV2StateLock(target, body), provided),
    (target, body) => Effect.provide(withV2RequestLock(target, body), provided),
    environment
  );
};
