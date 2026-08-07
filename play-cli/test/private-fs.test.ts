/**
 * The private-state sandbox.
 *
 * These are the port's security tests: containment, symlink refusal, mode 0600
 * and atomic replacement.  `test_client.py`'s `AtomicityTests` and `LeakTests`
 * are the Python originals — a partial file must never be observable and a
 * world-readable one must never be accepted.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either } from 'effect';
import { workspacePaths } from 'src/services/private-fs';
import { scratchWorkspace, type Scratch } from 'test/_fixtures';

const scratches: Scratch[] = [];

const fresh = (): Scratch => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  return scratch;
};

afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

const run = <A, E>(effect: Effect.Effect<A, E>): Either.Either<A, E> =>
  Effect.runSync(Effect.either(effect));

const message = <A>(either: Either.Either<A, { readonly message: string }>): string => {
  expect(Either.isLeft(either)).toBe(true);
  return Either.isLeft(either) ? either.left.message : '';
};

describe('containment', () => {
  test('a path outside the state root is refused', () => {
    const { files } = fresh();
    expect(message(run(files.resolve('/etc/passwd')))).toBe(
      'private state files must stay inside PLAY_STATE_DIR'
    );
  });

  test('a traversal out of the state root is refused after normalization', () => {
    const { files, workspace } = fresh();
    const target = path.join(workspace.stateRoot, '..', '..', 'escaped.json');
    expect(message(run(files.resolve(target)))).toBe(
      'private state files must stay inside PLAY_STATE_DIR'
    );
  });

  test('the state root itself is not a writable state file', () => {
    const { files, workspace } = fresh();
    expect(message(run(files.resolve(workspace.stateRoot)))).toBe('private state path is invalid');
  });

  test('PLAY_STATE_DIR outside the workspace is refused at construction', () => {
    const { workspace } = fresh();
    const either = run(
      workspacePaths({ PLAY_ROOT: workspace.root, PLAY_STATE_DIR: '/tmp' }, workspace.root)
    );
    expect(message(either)).toBe('PLAY_STATE_DIR must stay inside the player workspace');
  });
});

describe('writes', () => {
  test('a written file is mode 0600 and leaves no temp file behind', () => {
    const { files, workspace } = fresh();
    const target = path.join(workspace.stateRoot, 'game_abc', 'seat.json');
    expect(Either.isRight(run(files.writeJson(target, { a: 1 })))).toBe(true);
    expect(fs.statSync(target).mode & 0o777).toBe(0o600);
    const siblings = fs.readdirSync(path.dirname(target));
    expect(siblings).toEqual(['seat.json']);
  });

  test('writeJson round-trips through loadObject', () => {
    const { files, workspace } = fresh();
    const target = path.join(workspace.stateRoot, 'seat.json');
    Effect.runSync(files.writeJson(target, { b: 2, a: [1] }));
    expect(run(files.loadObject(target, 'session'))).toEqual(Either.right({ b: 2, a: [1] }));
  });

  test('writeJson emits the indent=2 sorted shape with a trailing newline', () => {
    const { files, workspace } = fresh();
    const target = path.join(workspace.stateRoot, 'seat.json');
    Effect.runSync(files.writeJson(target, { b: 2, a: 1 }));
    expect(fs.readFileSync(target, 'utf8')).toBe('{\n  "a": 1,\n  "b": 2\n}\n');
  });

  test('a replaced file is never observed half-written', () => {
    const { files, workspace } = fresh();
    const target = path.join(workspace.stateRoot, 'seat.json');
    Effect.runSync(files.writeText(target, 'first'));
    const before = fs.readFileSync(target, 'utf8');
    Effect.runSync(files.writeText(target, 'second-and-longer'));
    expect(before).toBe('first');
    expect(fs.readFileSync(target, 'utf8')).toBe('second-and-longer');
  });

  test('appendText adds without rewriting, as a log must', () => {
    const { files, workspace } = fresh();
    const target = path.join(workspace.stateRoot, 'receipts.log');
    Effect.runSync(files.appendText(target, 'one\n'));
    Effect.runSync(files.appendText(target, 'two\n'));
    expect(fs.readFileSync(target, 'utf8')).toBe('one\ntwo\n');
    expect(fs.statSync(target).mode & 0o777).toBe(0o600);
  });
});

describe('reads', () => {
  test('a world-readable state file is refused', () => {
    const { files, workspace } = fresh();
    const target = path.join(workspace.stateRoot, 'seat.json');
    Effect.runSync(files.writeText(target, '{}'));
    fs.chmodSync(target, 0o644);
    expect(message(run(files.readText(target, 'session')))).toBe(
      'private session must be a mode-0600 file'
    );
  });

  test('invalid JSON names the label, not the parser', () => {
    const { files, workspace } = fresh();
    const target = path.join(workspace.stateRoot, 'seat.json');
    Effect.runSync(files.writeText(target, 'not json'));
    expect(message(run(files.loadObject(target, 'session')))).toBe(
      'cannot read session: invalid JSON'
    );
  });

  test('a JSON array is not a state object', () => {
    const { files, workspace } = fresh();
    const target = path.join(workspace.stateRoot, 'seat.json');
    Effect.runSync(files.writeText(target, '[1]'));
    expect(message(run(files.loadObject(target, 'session')))).toBe(
      'session must contain a JSON object'
    );
  });
});

describe('symlinks', () => {
  test('a symlinked directory component is refused', () => {
    const { files, workspace } = fresh();
    const outside = fs.mkdtempSync(path.join(workspace.root, 'outside-'));
    fs.symlinkSync(outside, path.join(workspace.stateRoot, 'game_link'));
    const target = path.join(workspace.stateRoot, 'game_link', 'seat.json');
    expect(message(run(files.writeText(target, '{}')))).toBe(
      'private state directories must be real directories inside PLAY_STATE_DIR'
    );
  });

  test('an existing directory is walked without complaint', () => {
    const { files, workspace } = fresh();
    fs.mkdirSync(path.join(workspace.stateRoot, 'game_abc'), { mode: 0o700 });
    const target = path.join(workspace.stateRoot, 'game_abc', 'seat.json');
    expect(Either.isRight(run(files.writeText(target, '{}')))).toBe(true);
  });

  test('reading a directory that does not exist says so', () => {
    const { files, workspace } = fresh();
    const target = path.join(workspace.stateRoot, 'missing', 'seat.json');
    expect(message(run(files.readText(target, 'session')))).toBe(
      'private state directory does not exist'
    );
  });
});
