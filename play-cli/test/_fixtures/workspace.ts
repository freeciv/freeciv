/**
 * A throwaway player workspace on disk.
 *
 * The private-state sandbox refuses anything outside `PLAY_STATE_DIR`, so a
 * test that touches state needs a real directory tree.  On macOS the temp root
 * is reached through `/var` → `/private/var`, which is exactly the symlink the
 * Python's `_state_relative_path` comment is about — using the platform temp
 * dir keeps that case under test rather than around it.
 */
import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';
import { Effect, Layer } from 'effect';
import {
  PrivateFs,
  Workspace,
  privateFsFor,
  workspaceLayer,
  type PrivateFsApi,
  type WorkspacePaths,
} from 'src/services/private-fs';

export interface Scratch {
  readonly workspace: WorkspacePaths;
  readonly files: PrivateFsApi;
  readonly layer: Layer.Layer<Workspace | PrivateFs>;
  readonly cleanup: () => void;
}

const tempRoot = (): string =>
  process.env['PLAY_CLI_TEST_TMP'] ?? os.tmpdir();

export const scratchWorkspace = (stateDir = '.sessions'): Scratch => {
  const root = fs.realpathSync.native(
    fs.mkdtempSync(path.join(tempRoot(), 'play-cli-'))
  );
  const stateRoot = path.join(root, stateDir);
  fs.mkdirSync(stateRoot, { mode: 0o700, recursive: true });
  const workspace: WorkspacePaths = { root, stateRoot };
  const files = privateFsFor(workspace);
  return {
    workspace,
    files,
    layer: Layer.merge(workspaceLayer(root, stateDir), Layer.succeed(PrivateFs, files)),
    cleanup: () => {
      fs.rmSync(root, { recursive: true, force: true });
    },
  };
};

/** Run one effect against a fresh workspace and always clean it up. */
export const withScratchWorkspace = <A, E>(
  body: (scratch: Scratch) => Effect.Effect<A, E>
): Effect.Effect<A, E> =>
  Effect.acquireUseRelease(
    Effect.sync(() => scratchWorkspace()),
    body,
    (scratch) => Effect.sync(scratch.cleanup)
  );
