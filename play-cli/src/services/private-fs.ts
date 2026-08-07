/**
 * The private-state sandbox.
 *
 * Ports `_state_relative_path` (client.py:311-341), `_open_state_directory`
 * (344-405), `_write_private_text` (408-470), `_write_private_json` (473-475),
 * `_read_private_text` (478-509), `_load_private_object` (512-520),
 * `_append_private_text` (523-566) and `_state_root` / `_state_regular_file`
 * (762-800).
 *
 * DIVERGENCE (NOTES.md §3): CPython walks the workspace with `openat` +
 * `O_NOFOLLOW` through directory file descriptors; neither Node nor Bun exposes
 * the `*at` family.  The port keeps every property the Python was buying —
 * refuse any symlinked component, refuse a non-directory component, create with
 * mode 0700, write through a temp file and `rename`, enforce mode 0600 on read
 * — by `lstat`-walking each component from the workspace root and opening the
 * final component with `O_NOFOLLOW`.  The difference is TOCTOU width, not
 * reachability: nothing outside `PLAY_STATE_DIR` becomes writable.
 */
import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';
import { Context, Effect, Layer } from 'effect';
import { PlayerError, playerError } from 'src/errors';
import { isJsonObject, type JsonObject } from 'src/schema/primitives';
import { indentedJson } from 'src/services/json-output';

// ---------------------------------------------------------------------------
// Workspace
// ---------------------------------------------------------------------------

export interface WorkspacePaths {
  /** The player workspace — CPython's `ROOT`, the directory `client.py` sits in. */
  readonly root: string;
  /** `PLAY_STATE_DIR`, resolved and proved to be inside {@link root}. */
  readonly stateRoot: string;
}

export class Workspace extends Context.Tag('Workspace')<Workspace, WorkspacePaths>() {}

export const expandUser = (value: string): string =>
  value === '~' || value.startsWith('~/') ? path.join(os.homedir(), value.slice(1)) : value;

/**
 * `Path.resolve()` — resolve symlinks without requiring the path to exist, by
 * realpath-ing the longest existing prefix and re-appending the rest.
 */
export const resolveExisting = (target: string): string => {
  const absolute = path.resolve(expandUser(target));
  const tail: string[] = [];
  let head = absolute;
  for (;;) {
    try {
      return path.join(fs.realpathSync.native(head), ...tail.reverse());
    } catch {
      const parent = path.dirname(head);
      if (parent === head) return absolute;
      tail.push(path.basename(head));
      head = parent;
    }
  }
};

const isInside = (parent: string, child: string): boolean =>
  child === parent || child.startsWith(parent + path.sep);

/**
 * Build the workspace paths.  `root` defaults to the current directory, which
 * is where `just` always invoked `client.py` from; `PLAY_ROOT` overrides it so
 * the CLI can live outside the workspace it plays (NOTES.md §3).
 */
export const workspacePaths = (
  environment: Readonly<Record<string, string | undefined>> = process.env,
  cwd: string = process.cwd()
): Effect.Effect<WorkspacePaths, PlayerError> =>
  Effect.suspend(() => {
    const root = path.resolve(expandUser(environment['PLAY_ROOT'] ?? cwd));
    const configured = environment['PLAY_STATE_DIR'] ?? '.sessions';
    const expanded = expandUser(configured);
    const candidate = path.isAbsolute(expanded) ? expanded : path.join(root, expanded);
    const resolvedRoot = resolveExisting(root);
    const stateRoot = resolveExisting(candidate);
    return isInside(resolvedRoot, stateRoot)
      ? Effect.succeed({ root: resolvedRoot, stateRoot })
      : Effect.fail(playerError('PLAY_STATE_DIR must stay inside the player workspace'));
  });

export const WorkspaceLive = Layer.effect(Workspace, workspacePaths());

/** A fixed workspace, for tests and for the offline byte-diff harness. */
export const workspaceLayer = (root: string, stateDir = '.sessions'): Layer.Layer<Workspace> =>
  Layer.succeed(Workspace, {
    root: path.resolve(root),
    stateRoot: path.isAbsolute(stateDir)
      ? path.resolve(stateDir)
      : path.join(path.resolve(root), stateDir),
  });

// ---------------------------------------------------------------------------
// Path containment
// ---------------------------------------------------------------------------

export interface StatePath {
  /** The absolute destination inside `stateRoot`. */
  readonly destination: string;
  /** Its components relative to `stateRoot`, at least one long. */
  readonly relative: ReadonlyArray<string>;
}

const BAD_PARTS: ReadonlySet<string> = new Set(['', '.', '..']);

export const stateRelativePath = (
  workspace: WorkspacePaths,
  target: string
): Effect.Effect<StatePath, PlayerError> =>
  Effect.suspend(() => {
    const lexical = path.resolve(expandUser(target));
    // macOS presents the same temporary directory as both /var and
    // /private/var.  Canonicalize only the already-trusted workspace prefix;
    // resolving the complete destination would follow the very nested symlink
    // this routine exists to reject.
    const lexicalWorkspace = path.resolve(workspace.root);
    const destination = isInside(lexicalWorkspace, lexical)
      ? path.join(workspace.root, path.relative(lexicalWorkspace, lexical))
      : lexical;
    if (!isInside(workspace.stateRoot, destination)) {
      return Effect.fail(playerError('private state files must stay inside PLAY_STATE_DIR'));
    }
    const relative = path
      .relative(workspace.stateRoot, destination)
      .split(path.sep)
      .filter((part) => part !== '');
    if (relative.length === 0 || relative.some((part) => BAD_PARTS.has(part))) {
      return Effect.fail(playerError('private state path is invalid'));
    }
    return Effect.succeed({ destination, relative });
  });

// ---------------------------------------------------------------------------
// Directory walk
// ---------------------------------------------------------------------------

const REAL_DIR_ERROR =
  'private state directories must be real directories inside PLAY_STATE_DIR';

/**
 * Walk into a state directory, refusing every symlink, and return its absolute
 * path.  The Python returned a directory file descriptor; a path is the closest
 * the JavaScript runtimes allow (see the module docstring).
 */
export const openStateDirectory = (
  workspace: WorkspacePaths,
  parts: ReadonlyArray<string>,
  options: { readonly create: boolean }
): Effect.Effect<string, PlayerError> =>
  Effect.suspend(() => {
    const rootParts = path
      .relative(workspace.root, workspace.stateRoot)
      .split(path.sep)
      .filter((part) => part !== '');
    if (!isInside(workspace.root, workspace.stateRoot)) {
      return Effect.fail(playerError('PLAY_STATE_DIR must stay inside the player workspace'));
    }
    let current = workspace.root;
    try {
      const rootStat = fs.lstatSync(current);
      if (!rootStat.isDirectory()) {
        return Effect.fail(playerError('the player workspace is not a safe directory'));
      }
    } catch {
      return Effect.fail(playerError('the player workspace is not a safe directory'));
    }
    for (const part of [...rootParts, ...parts]) {
      if (BAD_PARTS.has(part)) {
        return Effect.fail(playerError('private state path is invalid'));
      }
      const child = path.join(current, part);
      const stat = ((): fs.Stats | null => {
        try {
          return fs.lstatSync(child);
        } catch {
          return null;
        }
      })();
      if (stat === null) {
        if (!options.create) {
          return Effect.fail(playerError('private state directory does not exist'));
        }
        try {
          fs.mkdirSync(child, { mode: 0o700 });
        } catch (cause) {
          const code = (cause as NodeJS.ErrnoException).code;
          if (code !== 'EEXIST') {
            return Effect.fail(playerError(REAL_DIR_ERROR));
          }
        }
        let created: fs.Stats;
        try {
          created = fs.lstatSync(child);
        } catch {
          return Effect.fail(playerError(REAL_DIR_ERROR));
        }
        if (created.isSymbolicLink() || !created.isDirectory()) {
          return Effect.fail(playerError(REAL_DIR_ERROR));
        }
      } else if (stat.isSymbolicLink() || !stat.isDirectory()) {
        return Effect.fail(playerError(REAL_DIR_ERROR));
      }
      current = child;
    }
    return Effect.succeed(current);
  });

// ---------------------------------------------------------------------------
// The service
// ---------------------------------------------------------------------------

export interface PrivateFsApi {
  readonly workspace: WorkspacePaths;
  readonly resolve: (target: string) => Effect.Effect<StatePath, PlayerError>;
  readonly writeText: (target: string, text: string) => Effect.Effect<string, PlayerError>;
  readonly writeJson: (target: string, value: unknown) => Effect.Effect<string, PlayerError>;
  readonly readText: (target: string, label: string) => Effect.Effect<string, PlayerError>;
  readonly loadObject: (target: string, label: string) => Effect.Effect<JsonObject, PlayerError>;
  readonly appendText: (target: string, text: string) => Effect.Effect<string, PlayerError>;
  /** `_state_regular_file` — the state-relative path of an existing regular file. */
  readonly regularFile: (target: string, label: string) => Effect.Effect<string, PlayerError>;
  readonly exists: (target: string) => Effect.Effect<boolean>;
  readonly openDirectory: (
    parts: ReadonlyArray<string>,
    options: { readonly create: boolean }
  ) => Effect.Effect<string, PlayerError>;
}

export class PrivateFs extends Context.Tag('PrivateFs')<PrivateFs, PrivateFsApi>() {}

const openFlag = (name: string): number => {
  const table = fs.constants as unknown as Readonly<Record<string, number | undefined>>;
  return table[name] ?? 0;
};

const NOFOLLOW = openFlag('O_NOFOLLOW');
const CLOEXEC = openFlag('O_CLOEXEC');

const randomSuffix = (): string =>
  [...crypto.getRandomValues(new Uint8Array(6))]
    .map((byte) => byte.toString(16).padStart(2, '0'))
    .join('');

const makeApi = (workspace: WorkspacePaths): PrivateFsApi => {
  const resolve = (target: string): Effect.Effect<StatePath, PlayerError> =>
    stateRelativePath(workspace, target);

  const parentOf = (
    relative: ReadonlyArray<string>,
    create: boolean
  ): Effect.Effect<string, PlayerError> =>
    openStateDirectory(workspace, relative.slice(0, -1), { create });

  const leafOf = (relative: ReadonlyArray<string>): string =>
    relative[relative.length - 1] as string;

  const writeText = (target: string, text: string): Effect.Effect<string, PlayerError> =>
    Effect.gen(function* () {
      const { destination, relative } = yield* resolve(target);
      const parent = yield* parentOf(relative, true);
      const name = leafOf(relative);
      const temporary = path.join(parent, `.${name}.${randomSuffix()}.tmp`);
      const write = Effect.try({
        try: () => {
          const descriptor = fs.openSync(
            temporary,
            fs.constants.O_WRONLY | fs.constants.O_CREAT | fs.constants.O_EXCL | NOFOLLOW | CLOEXEC,
            0o600
          );
          try {
            fs.fchmodSync(descriptor, 0o600);
            fs.writeFileSync(descriptor, text, { encoding: 'utf8' });
            fs.fsyncSync(descriptor);
          } finally {
            fs.closeSync(descriptor);
          }
          fs.renameSync(temporary, path.join(parent, name));
          return destination;
        },
        catch: () =>
          playerError('cannot safely write private state inside PLAY_STATE_DIR'),
      });
      return yield* Effect.onError(write, () =>
        Effect.sync(() => {
          try {
            fs.unlinkSync(temporary);
          } catch {
            /* the rename already consumed it */
          }
        })
      );
    });

  const readText = (target: string, label: string): Effect.Effect<string, PlayerError> =>
    Effect.gen(function* () {
      const { relative } = yield* resolve(target);
      const parent = yield* parentOf(relative, false);
      const file = path.join(parent, leafOf(relative));
      const opened = yield* Effect.try({
        try: () => fs.openSync(file, fs.constants.O_RDONLY | NOFOLLOW | CLOEXEC),
        catch: () => playerError(`cannot safely read private ${label}`),
      });
      try {
        const stat = fs.fstatSync(opened);
        if (!stat.isFile() || (stat.mode & 0o777) !== 0o600) {
          return yield* Effect.fail(
            playerError(`private ${label} must be a mode-0600 file`)
          );
        }
        return fs.readFileSync(opened, { encoding: 'utf8' });
      } finally {
        fs.closeSync(opened);
      }
    });

  const appendText = (target: string, text: string): Effect.Effect<string, PlayerError> =>
    Effect.gen(function* () {
      const { destination, relative } = yield* resolve(target);
      const parent = yield* parentOf(relative, true);
      const file = path.join(parent, leafOf(relative));
      const opened = yield* Effect.try({
        try: () =>
          fs.openSync(
            file,
            fs.constants.O_WRONLY | fs.constants.O_CREAT | fs.constants.O_APPEND | NOFOLLOW | CLOEXEC,
            0o600
          ),
        catch: () => playerError('cannot safely append to private state'),
      });
      try {
        const stat = fs.fstatSync(opened);
        if (!stat.isFile()) {
          return yield* Effect.fail(playerError('a private log must be a regular file'));
        }
        fs.fchmodSync(opened, 0o600);
        fs.writeSync(opened, Buffer.from(text, 'utf8'));
        return destination;
      } finally {
        fs.closeSync(opened);
      }
    });

  return {
    workspace,
    resolve,
    writeText,
    writeJson: (target, value) => writeText(target, `${indentedJson(value)}\n`),
    readText,
    loadObject: (target, label) =>
      Effect.gen(function* () {
        const text = yield* readText(target, label);
        const parsed = yield* Effect.try({
          try: () => JSON.parse(text) as unknown,
          catch: () => playerError(`cannot read ${label}: invalid JSON`),
        });
        return isJsonObject(parsed)
          ? parsed
          : yield* Effect.fail(playerError(`${label} must contain a JSON object`));
      }),
    appendText,
    regularFile: (target, label) =>
      Effect.gen(function* () {
        const { relative } = yield* resolve(target);
        const parent = yield* parentOf(relative, false);
        const file = path.join(parent, leafOf(relative));
        const ok = yield* Effect.sync(() => {
          try {
            const stat = fs.lstatSync(file);
            return stat.isFile();
          } catch {
            return false;
          }
        });
        return ok
          ? relative.join(path.sep)
          : yield* Effect.fail(
              playerError(`the ${label} must be a real file inside PLAY_STATE_DIR`)
            );
      }),
    exists: (target) =>
      Effect.sync(() => {
        try {
          const lexical = path.resolve(expandUser(target));
          return fs.lstatSync(lexical).isFile();
        } catch {
          return false;
        }
      }),
    openDirectory: (parts, options) => openStateDirectory(workspace, parts, options),
  };
};

export const PrivateFsLive: Layer.Layer<PrivateFs, never, Workspace> = Layer.effect(
  PrivateFs,
  Effect.map(Workspace, makeApi)
);

/** Build the API directly, for tests that do not want a Layer. */
export const privateFsFor = (workspace: WorkspacePaths): PrivateFsApi => makeApi(workspace);
