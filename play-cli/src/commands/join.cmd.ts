/**
 * `join` — the one command that creates a seat, and binds this workspace to it.
 *
 * Ports `_apply_play_defaults` (client.py:6179-6220) and `command_join`
 * (6221-6417).
 *
 * The shape is a preflight, a POST, and three writes:
 *
 *   GET  {base}/health                     timeout 3   — is the supervisor up
 *   GET  {base}/v1/games/{id}/status       timeout 10  — which protocol
 *   POST {base}/v1/games/{id}/join         timeout 30  — the seat
 *   → session file (mode 0600) → current-session pointer → workspace seat
 *
 * Nothing is written until every field of the join result has been proved, so a
 * refused join leaves no `.sessions` directory behind at all.  Joining *is* the
 * binding: from here every command in this workspace resolves this seat by
 * itself, which is why the card never prints the session path.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { Command, Options } from '@effect/cli';
import { Console, Effect, Either } from 'effect';
import { PlayerError, playerError } from 'src/errors';
import { FULL_CONTROL_V2, GAME_ID_RE, STRATEGIC_V1, TERMINAL_STATES } from 'src/constants';
import { dualText, resolveDual } from 'src/options';
import { joinGuidance, renderJoin, seatBindingLine } from 'src/render/join';
import { render } from 'src/render/primitives';
import { compactJson, jsonRequested, printJson } from 'src/services/json-output';
import { validateEvaluationContext } from 'src/services/evaluation-context';
import { loadInvitation, pyStrip } from 'src/services/invites';
import { Http } from 'src/services/http';
import { Workspace, type WorkspacePaths } from 'src/services/private-fs';
import {
  SessionStore,
  controllerName,
  gameId as validGameId,
  sessionKey,
} from 'src/services/session-store';
import { isJsonObject, type JsonObject, type JsonValue } from 'src/schema/primitives';

// ---------------------------------------------------------------------------
// Arguments
// ---------------------------------------------------------------------------

export interface JoinArgs {
  readonly gameId: string;
  readonly name: string;
  readonly place: string;
  readonly invite: string;
  readonly joinToken: string;
  readonly json: boolean;
}

export interface PlayIdentity {
  readonly gameId: string;
  readonly name: string;
  readonly place: string;
}

// ---------------------------------------------------------------------------
// client.py:6179-6220 — .playconfig.json
// ---------------------------------------------------------------------------

const PLAYCONFIG_INVALID =
  'invalid .playconfig.json: expected schema_version 1 with ' +
  'game_id, name, and optional place; re-run `just play` from ' +
  'the repository root';

const isPlace = (value: JsonValue | undefined): boolean =>
  value === null ||
  (typeof value === 'number' && Number.isInteger(value) && value >= 1);

/**
 * Fill omitted join identity from `.playconfig.json` when present.
 *
 * `just play` (repository root) pre-configures a per-player workspace with the
 * assigned game and controller name so `just join` needs no arguments there.
 * Explicit arguments always win; a malformed config fails closed rather than
 * guessing.
 */
export const applyPlayDefaults = (
  workspace: WorkspacePaths,
  args: PlayIdentity
): Effect.Effect<PlayIdentity, PlayerError> =>
  Effect.suspend(() => {
    const target = path.join(workspace.root, '.playconfig.json');
    const present = Either.getOrElse(
      Either.try(() => fs.statSync(target).isFile()),
      () => false
    );
    if (!present) return Effect.succeed(args);
    // `read_text(encoding="utf-8")` is strict, and `except ValueError` catches
    // the `UnicodeDecodeError` it raises.  Node's `'utf8'` reader would
    // substitute U+FFFD and accept a config CPython refuses, so the decode is
    // `fatal` here too (NOTES §12.8).
    const raw = Either.mapLeft(
      Either.try(
        (): unknown =>
          JSON.parse(
            new TextDecoder('utf-8', { fatal: true, ignoreBOM: true }).decode(
              fs.readFileSync(target)
            )
          )
      ),
      (cause) => (cause instanceof Error ? cause.message : String(cause))
    );
    if (Either.isLeft(raw)) {
      return Effect.fail(playerError(`invalid .playconfig.json: ${raw.left}`));
    }
    const value = raw.right;
    if (!isJsonObject(value)) return Effect.fail(playerError(PLAYCONFIG_INVALID));
    const configuredGame = value['game_id'];
    const configuredName = value['name'];
    const configuredPlace = value['place'];
    if (
      value['schema_version'] !== 1 ||
      typeof configuredGame !== 'string' ||
      !GAME_ID_RE.test(configuredGame) ||
      typeof configuredName !== 'string' ||
      pyStrip(configuredName) === '' ||
      !isPlace(configuredPlace === undefined ? null : configuredPlace)
    ) {
      return Effect.fail(playerError(PLAYCONFIG_INVALID));
    }
    return Effect.succeed({
      gameId: pyStrip(args.gameId) === '' ? configuredGame : args.gameId,
      name: pyStrip(args.name) === '' ? configuredName : args.name,
      place:
        pyStrip(args.place) === '' && typeof configuredPlace === 'number'
          ? String(configuredPlace)
          : args.place,
    });
  });

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

/** `dict.get(key, default)`: a key present with `None` is `None`, not the default. */
const pick = (value: JsonObject, key: string, fallback: JsonValue = null): JsonValue => {
  const found = value[key];
  return found === undefined ? fallback : found;
};

/** CPython's `repr()`, for the one refusal that interpolates `{value!r}`. */
const pyRepr = (value: JsonValue): string => {
  if (value === null) return 'None';
  if (value === true) return 'True';
  if (value === false) return 'False';
  if (typeof value !== 'string') return compactJson(value);
  const escaped = value.replace(/\\/g, '\\\\');
  return escaped.includes("'") && !escaped.includes('"')
    ? `"${escaped}"`
    : `'${escaped.replace(/'/g, "\\'")}'`;
};

const isFilledString = (value: JsonValue): boolean => typeof value === 'string' && value !== '';

const staleInvite = (gameId: string): string =>
  '\nThe game invitation may be stale. Ask the game ' +
  `owner to run \`just invite ${gameId}\` from the ` +
  'repository root, then retry once.';

// ---------------------------------------------------------------------------
// command_join
// ---------------------------------------------------------------------------

export const commandJoin = (
  args: JoinArgs,
  environment: Readonly<Record<string, string | undefined>> = process.env
): Effect.Effect<void, PlayerError, Workspace | Http | SessionStore> =>
  Effect.gen(function* () {
    const workspace = yield* Workspace;
    const http = yield* Http;
    const store = yield* SessionStore;

    const identity = yield* applyPlayDefaults(workspace, args);
    const game = yield* validGameId(identity.gameId);
    const controller = yield* controllerName(identity.name);

    // DIVERGENCE (NOTES §12.13, deliberate): `join` is idempotent per
    // workspace.  CPython re-claimed on every call — the session filename is
    // deterministic, so a re-join whose first response was lost claimed a
    // SECOND seat server-side and silently overwrote the local session with
    // it, leaving one workspace holding two places (the game_vkNE incident:
    // seat 1 orphaned, opponent's join refused with 409).  A workspace that
    // already holds a session for the assigned game re-binds it and reports,
    // rather than claiming again; claiming fresh is a deliberate act.
    const held = yield* store.listSessions(game);
    const first = held[0];
    if (first !== undefined) {
      // A held-but-unreadable session still refuses: silently claiming a
      // fresh seat over a corrupt file is the same double-claim in disguise.
      const loaded = yield* Effect.mapError(store.resolve(first), (cause) =>
        playerError(
          `this workspace already holds a session for ${game} but it cannot ` +
            `be read (${cause.message}); delete .sessions/${game}/ to claim ` +
            'a fresh seat'
        )
      );
      yield* store.setCurrentSession(loaded.path);
      yield* store.bindWorkspaceSeat(loaded.path, game);
      const seat =
        loaded.session.place === null
          ? ''
          : ` | seat ${loaded.session.place} ${loaded.session.playerName ?? ''}`.trimEnd();
      yield* Console.log(
        `already joined ${game} as ${loaded.session.controllerLabel}${seat} — ` +
          'this workspace holds its seat; run `just turn`'
      );
      yield* Console.error(
        'joining again would claim a second seat. To claim a fresh seat ' +
          `deliberately, delete .sessions/${game}/ first.`
      );
      return;
    }

    const invitation = yield* loadInvitation(
      workspace,
      { gameId: game, invite: args.invite, joinToken: args.joinToken },
      environment
    );
    const base = invitation.base;

    yield* Effect.catchAll(
      http.requestJson('GET', `${base}/health`, { timeout: 3 }),
      (error) =>
        Effect.fail(
          playerError(
            `${error.message}\nThe assigned game cannot be joined. Stop and tell the user.`
          )
        )
    );

    const status = yield* http.requestJson('GET', `${base}/v1/games/${game}/status`, {
      timeout: 10,
    });
    const declared = pick(status, 'control_protocol');
    const controlProtocol = declared === null ? STRATEGIC_V1 : declared;
    if (controlProtocol !== STRATEGIC_V1 && controlProtocol !== FULL_CONTROL_V2) {
      return yield* Effect.fail(
        playerError(`game requires unsupported control protocol ${pyRepr(controlProtocol)}`)
      );
    }

    const body: JsonObject = {
      controller_label: controller,
      ...(controlProtocol === FULL_CONTROL_V2
        ? { supported_control_protocols: [FULL_CONTROL_V2] }
        : {}),
      ...(identity.place === ''
        ? {}
        : {
            place: /^[0-9]+$/.test(identity.place)
              ? Number.parseInt(identity.place, 10)
              : identity.place,
          }),
    };

    const result = yield* Effect.catchAll(
      http.requestJson('POST', `${base}/v1/games/${game}/join`, {
        token: invitation.token,
        body,
        timeout: 30,
      }),
      (error) =>
        Effect.fail(
          error.message.startsWith('HTTP 401:') || error.message.startsWith('HTTP 403:')
            ? playerError(`${error.message}${staleInvite(game)}`)
            : error
        )
    );

    const core: JsonObject = {
      schema_version: 1,
      service_url: base,
      game_id: pick(result, 'game_id'),
      agent_id: pick(result, 'agent_id'),
      agent_token: pick(result, 'agent_token'),
      place: pick(result, 'place'),
      seat_id: pick(result, 'seat_id'),
      player_name: pick(result, 'player_name'),
      controller_label: pick(result, 'controller_label'),
      controller_metadata: pick(result, 'controller_metadata', {}),
      controller_fingerprint: pick(result, 'controller_fingerprint'),
      control_protocol: pick(result, 'control_protocol', STRATEGIC_V1),
      supported_control_protocols: pick(result, 'supported_control_protocols', []),
      timing_mode: pick(result, 'timing_mode'),
      action_timeout_s: pick(result, 'action_timeout_s'),
    };

    if (!(['game_id', 'agent_id', 'agent_token'] as const).every((key) => isFilledString(pick(core, key)))) {
      return yield* Effect.fail(playerError('the supervisor returned an incomplete join response'));
    }
    if (core['game_id'] !== game) {
      return yield* Effect.fail(playerError('the join response belongs to a different game'));
    }
    if (core['controller_label'] !== controller) {
      return yield* Effect.fail(
        playerError(
          'the join response controller label does not match the requested ' +
            'harness-model identity'
        )
      );
    }
    if (core['control_protocol'] !== controlProtocol) {
      return yield* Effect.fail(
        playerError('the join result changed the preflight control protocol')
      );
    }

    const evaluation =
      controlProtocol === FULL_CONTROL_V2
        ? yield* validateEvaluationContext(result, 'v2 join result')
        : null;
    const session: JsonObject =
      evaluation === null
        ? core
        : {
            ...core,
            objective: evaluation.objective,
            max_turns: evaluation.max_turns,
            turns_remaining: evaluation.turns_remaining,
          };

    if (controlProtocol === FULL_CONTROL_V2) {
      const supported = session['supported_control_protocols'];
      if (
        !Array.isArray(supported) ||
        !supported.includes(FULL_CONTROL_V2) ||
        supported.some((item) => !isFilledString(item))
      ) {
        return yield* Effect.fail(
          playerError('the v2 join result omitted the negotiated protocol')
        );
      }
      const available = result['v2_transport_available'];
      if (typeof available !== 'boolean') {
        return yield* Effect.fail(
          playerError('the v2 join result omitted transport availability')
        );
      }
      const state = pick(result, 'state');
      if (
        available !== true ||
        (typeof state === 'string' && TERMINAL_STATES.has(state)) ||
        pick(result, 'error') !== null
      ) {
        return yield* Effect.fail(
          playerError(
            'the full-control-v2 transport did not become playable; ' +
              'stop and tell the game owner'
          )
        );
      }
      const prefix = `${base}/v2/games/${game}/me`;
      const endpoints: ReadonlyArray<readonly [string, string]> = [
        ['health_url', `${prefix}/health`],
        ['state_url', `${prefix}/state`],
        ['legal_actions_url', `${prefix}/legal-actions`],
        ['batches_url', `${prefix}/batches`],
        ['receipts_url', `${prefix}/receipts/{batch_id}`],
        ['wait_url', `${prefix}/wait`],
        ['openapi_url', `${base}/v2/openapi.json`],
      ];
      for (const [name, expected] of endpoints) {
        if (result[name] !== expected) {
          return yield* Effect.fail(
            playerError(`the v2 join result has an invalid same-origin ${name}`)
          );
        }
      }
    }

    const sessionPath = path.join(
      store.workspace.stateRoot,
      game,
      `${sessionKey(controller)}.json`
    );
    yield* store.writeSession(sessionPath, session);
    yield* store.setCurrentSession(sessionPath);
    // Joining *is* the binding: from here every command in this workspace
    // resolves this seat by itself, so nothing downstream has to re-type a
    // path.  `--json` stays byte-identical, so the binding is reported in the
    // human renderings only.
    const replaced = yield* store.bindWorkspaceSeat(sessionPath, game);
    const binding = seatBindingLine(game, replaced);

    const publicResult: JsonObject = {
      ...Object.fromEntries(
        Object.entries(result).filter(([key]) => key !== 'agent_token')
      ),
      session_saved: true,
      session_file: sessionPath,
    };

    yield* (jsonRequested('join', args.json, environment)
      ? printJson(publicResult)
      : render(renderJoin(session, publicResult, replaced)));

    yield* Console.error(joinGuidance(session, result, binding));
  });

// ---------------------------------------------------------------------------
// The CLI surface
// ---------------------------------------------------------------------------

const textOption = (name: string): Options.Options<string> =>
  Options.text(name).pipe(Options.withDefault(''));

/**
 * DIVERGENCE (NOTES.md §12.1): argparse marked `--game-id` and `--name`
 * `required=True`, but the justfile recipe this CLI folds in always passed both
 * (as `""` when the caller omitted them) precisely so `.playconfig.json` could
 * fill them in.  Requiring them here would break `just join` in a
 * pre-configured workspace, so both default to `""` and the config — or
 * `_game_id`/`_controller_name` — decides.
 *
 * `--game_id` and `--join_token` are accepted alongside the dashed spellings
 * (NOTES §11.5): `just join --game_id GAME_ID --name HARNESS-MODEL` is the
 * spelling every refusal in this port prints, so the CLI that replaces `just`
 * has to answer to it.
 */
export const joinCommand = Command.make(
  'join',
  {
    gameId: dualText('game-id'),
    name: textOption('name'),
    place: textOption('place'),
    invite: textOption('invite'),
    joinToken: dualText('join-token'),
    json: Options.boolean('json').pipe(
      Options.withDescription('print the full-fidelity JSON payload instead of text')
    ),
  },
  (parsed) =>
    Effect.gen(function* () {
      const gameId = yield* resolveDual('game-id', parsed.gameId, '');
      const joinToken = yield* resolveDual('join-token', parsed.joinToken, '');
      yield* commandJoin({ ...parsed, gameId, joinToken });
    })
);
