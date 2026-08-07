/**
 * `use` — bind this workspace to one seat, or report the seat it already plays.
 *
 * Ports `_workspace_relative`, `_resolve_use_target` and `command_use`
 * (client.py:6418-6512).
 *
 * Bare `use` is the only command that prints a session path, and it prints it
 * in the exact form `use` accepts back: relative to the workspace.  That is the
 * whole reason `join` does not print one — an agent that can read the path off
 * every join card re-types it on every command, and an operator who genuinely
 * needs it runs `just use`.
 *
 * A target is either a game ID (bound when that game holds exactly one seat,
 * refused with both `just use …` spellings when it holds two) or one session
 * path.  Nothing is rebound until the file has been proved to be a session this
 * workspace joined, so a refused rebind leaves the previous binding intact.
 */
import * as path from 'node:path';
import { Args, Command, Options } from '@effect/cli';
import { Effect, Option } from 'effect';
import { PlayerError, playerError } from 'src/errors';
import { GAME_ID_RE } from 'src/constants';
import { seatBindingLine } from 'src/render/join';
import { render } from 'src/render/primitives';
import { pyStrip } from 'src/services/invites';
import { jsonRequested, printJson } from 'src/services/json-output';
import { PrivateFs, Workspace, type WorkspacePaths } from 'src/services/private-fs';
import { SessionStore, type SessionStoreApi } from 'src/services/session-store';
import type { PrivateFsApi } from 'src/services/private-fs';
import type { JsonObject } from 'src/schema/primitives';

// ---------------------------------------------------------------------------
// client.py:6418-6423 — _workspace_relative
// ---------------------------------------------------------------------------

/** Spell a state path the way `use` accepts it: relative to the workspace. */
export const workspaceRelative = (workspace: WorkspacePaths, target: string): string => {
  const root = workspace.root;
  return target === root || target.startsWith(root + path.sep)
    ? path.relative(root, target)
    : target;
};

// ---------------------------------------------------------------------------
// client.py:6425-6452 — _resolve_use_target
// ---------------------------------------------------------------------------

/** Resolve `use`'s argument: a game ID, or one session path. */
export const resolveUseTarget = (
  workspace: WorkspacePaths,
  files: PrivateFsApi,
  store: SessionStoreApi,
  value: string
): Effect.Effect<string, PlayerError> =>
  Effect.gen(function* () {
    if (!GAME_ID_RE.test(value)) {
      const expanded = path.isAbsolute(value) ? value : path.join(workspace.root, value);
      const resolved = yield* files.resolve(expanded);
      return resolved.destination;
    }
    const sessions = yield* store.listSessions(value);
    if (sessions.length === 0) {
      return yield* Effect.fail(
        playerError(
          `this workspace holds no joined seat for ${value}. Join one ` +
            `with \`just join --game_id ${value} --name HARNESS-MODEL\`.`
        )
      );
    }
    if (sessions.length > 1) {
      const candidates = sessions
        .map((session) => `\`just use ${workspaceRelative(workspace, session)}\``)
        .join(', ');
      return yield* Effect.fail(
        playerError(
          `${value} has ${sessions.length} joined seats in this ` +
            `workspace; name the one you are playing: ${candidates}`
        )
      );
    }
    return sessions[0] as string;
  });

// ---------------------------------------------------------------------------
// client.py:6455-6512 — command_use
// ---------------------------------------------------------------------------

export interface UseArgs {
  readonly target: string;
  readonly json: boolean;
}

const UNBOUND =
  'this workspace is not bound to a seat. Join one with ' +
  '`just join --game_id GAME_ID --name HARNESS-MODEL`, or bind ' +
  'a seat you already joined with `just use GAME_ID`.';

const preconfigured = (gameId: string): string =>
  'run `just join` first — this workspace is ' +
  `pre-configured for ${gameId}, and joining binds the ` +
  'seat this command reports';

export const commandUse = (
  args: UseArgs,
  environment: Readonly<Record<string, string | undefined>> = process.env
): Effect.Effect<void, PlayerError, Workspace | PrivateFs | SessionStore> =>
  Effect.gen(function* () {
    const workspace = yield* Workspace;
    const files = yield* PrivateFs;
    const store = yield* SessionStore;
    const json = jsonRequested('use', args.json, environment);
    // `(getattr(args, "target", "") or "").strip()` — CPython's strip, whose
    // whitespace class is not `String.prototype.trim`'s (see `pyStrip`).
    const target = pyStrip(args.target);

    if (target === '') {
      const found = yield* store.readSeatBinding();
      if (Option.isNone(found)) {
        const pinned = yield* store.preconfiguredGameId();
        return yield* Effect.fail(
          playerError(Option.isSome(pinned) ? preconfigured(pinned.value) : UNBOUND)
        );
      }
      const binding = found.value;
      if (json) {
        return yield* printJson({
          game_id: binding.gameId,
          session_file: binding.session,
          bound_at: binding.boundAt,
          rebound_from: null,
        });
      }
      const bound = binding.boundAt === '' ? '' : ` | bound ${binding.boundAt}`;
      return yield* render([
        `playing ${binding.gameId} | seat ` +
          `${workspaceRelative(workspace, binding.session)}${bound}`,
        'commands need no --session; `just use GAME_ID` rebinds this workspace',
      ]);
    }

    const sessionPath = yield* resolveUseTarget(workspace, files, store, target);
    const session: JsonObject = yield* files.loadObject(sessionPath, 'session');
    const game = session['game_id'];
    if (
      typeof game !== 'string' ||
      !GAME_ID_RE.test(game) ||
      typeof session['agent_token'] !== 'string'
    ) {
      return yield* Effect.fail(
        playerError(
          `${target} is not a session this workspace joined. Bind a seat ` +
            'by its game with `just use GAME_ID`.'
        )
      );
    }
    const replaced = yield* store.bindWorkspaceSeat(sessionPath, game);
    yield* store.setCurrentSession(sessionPath);
    if (json) {
      const binding = yield* store.readSeatBinding();
      return yield* printJson({
        game_id: game,
        session_file: sessionPath,
        bound_at: Option.isSome(binding) ? binding.value.boundAt : '',
        rebound_from: Option.isNone(replaced) ? null : replaced.value.gameId,
      });
    }
    return yield* render([seatBindingLine(game, replaced)]);
  });

// ---------------------------------------------------------------------------
// The CLI surface
// ---------------------------------------------------------------------------

export const useCommand = Command.make(
  'use',
  {
    target: Args.text({ name: 'target' }).pipe(Args.optional),
    json: Options.boolean('json').pipe(
      Options.withDescription('print the full-fidelity JSON payload instead of text')
    ),
  },
  ({ target, json }) =>
    commandUse({ target: Option.getOrElse(target, () => ''), json })
);
