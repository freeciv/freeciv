/**
 * Spike S3 — can a Bun process share `flock(2)` with the Python player?
 *
 * `play/client.py` makes the monitor a singleton and the gateway ready-file
 * safe with `fcntl.flock(fd, LOCK_EX | LOCK_NB)` (play/client.py:557, :636,
 * :701). If the TypeScript arena cannot take *that same* lock, the two
 * runtimes can run two monitors over one session and neither will notice.
 *
 * The claim under test is mutual exclusion in BOTH directions on darwin:
 *   (a) Bun holds  -> Python's non-blocking acquire raises BlockingIOError;
 *   (b) Python holds -> Bun's `flock` returns -1 with errno EWOULDBLOCK;
 *   (c) after either side releases, the other side acquires cleanly.
 */
import { describe, expect, test } from 'bun:test';
import { Data, Effect, type Scope } from 'effect';
import { FileSystem } from '@effect/platform';
import { BunFileSystem } from '@effect/platform-bun';
import { mkdtempSync, rmSync, rmdirSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import {
  EWOULDBLOCK,
  LOCK_EX,
  LOCK_FILE_MODE,
  LOCK_NB,
  LOCK_PATH_VARIABLE,
  LOCK_UN,
  ffiAvailable,
  fileMode,
  flock,
  lockFd,
  pythonAttempt,
  pythonHolder,
  pythonSharedProbe,
} from 'test/spikes/s3-flock';

class ScratchError extends Data.TaggedError('ScratchError')<{
  readonly reason: string;
  readonly cause: unknown;
}> {}

/** A private lock file that is deleted with the scope that made it. */
const scratchLockPath: Effect.Effect<string, ScratchError, Scope.Scope> =
  Effect.map(
    Effect.acquireRelease(
      Effect.try({
        try: () => mkdtempSync(join(tmpdir(), 's3-flock-')),
        catch: (cause) => new ScratchError({ reason: 'mkdtemp failed', cause }),
      }),
      (directory) =>
        Effect.ignore(
          Effect.try(() => {
            rmSync(join(directory, 'monitor.lock'), { force: true });
            rmdirSync(directory);
          }),
        ),
    ),
    (directory) => join(directory, 'monitor.lock'),
  );

const run = <A, E>(effect: Effect.Effect<A, E, Scope.Scope>): Promise<A> =>
  Effect.runPromise(Effect.scoped(effect));

/** A Bun process that takes LOCK_EX|LOCK_NB and never lets go on its own. */
const HOLD_FOREVER_JS = [
  'import { FFIType, dlopen } from "bun:ffi";',
  'import { openSync } from "node:fs";',
  'const libc = dlopen("libSystem.B.dylib", { flock: { args: [FFIType.i32, FFIType.i32], returns: FFIType.i32 } });',
  `const fd = openSync(process.env["${LOCK_PATH_VARIABLE}"], "a+");`,
  'process.stdout.write(String(libc.symbols.flock(fd, 2 | 4)) + "\\n");',
  'await new Promise(() => {});',
].join('\n');

describe('S3: flock interop between Bun and Python', () => {
  test('bun:ffi can bind flock out of libSystem', () => {
    expect(ffiAvailable()).toBe(true);
  });

  test('a Bun-created lock file passes the mode-0600 guard play/client.py applies', () =>
    run(
      Effect.gen(function* () {
        const path = yield* scratchLockPath;
        const fd = yield* lockFd(path);
        expect(fd).toBeGreaterThan(2);
        expect(yield* fileMode(fd)).toBe(LOCK_FILE_MODE);
      }),
    ));

  test('(a) Bun holds LOCK_EX|LOCK_NB -> python3 fcntl.flock reports BlockingIOError(35)', () =>
    run(
      Effect.gen(function* () {
        const path = yield* scratchLockPath;
        const fd = yield* lockFd(path);

        const taken = yield* flock(fd, LOCK_EX | LOCK_NB);
        expect(taken.returnCode).toBe(0);

        // The Python side is the real `fcntl.flock`, in a real other process.
        expect(yield* pythonAttempt(path)).toBe(`BLOCKED ${EWOULDBLOCK}`);

        // (c), first direction: releasing hands the lock straight to Python.
        const released = yield* flock(fd, LOCK_UN);
        expect(released.returnCode).toBe(0);
        expect(yield* pythonAttempt(path)).toBe('ACQUIRED');
      }),
    ));

  test('(b) python3 holds the lock -> Bun flock returns -1 with errno EWOULDBLOCK', () =>
    run(
      Effect.gen(function* () {
        const path = yield* scratchLockPath;
        const holder = yield* pythonHolder(path);
        const fd = yield* lockFd(path);

        const blocked = yield* flock(fd, LOCK_EX | LOCK_NB);
        expect(blocked.returnCode).toBe(-1);
        expect(blocked.errno).toBe(EWOULDBLOCK);

        // (c), other direction: Python unlocks, Bun acquires on the next try.
        yield* holder.release;
        const acquired = yield* flock(fd, LOCK_EX | LOCK_NB);
        expect(acquired.returnCode).toBe(0);
        expect(acquired.errno).toBe(0);

        expect((yield* flock(fd, LOCK_UN)).returnCode).toBe(0);
      }),
    ));

  test('the lock is per-open-file-description, so a second Bun fd is excluded too', () =>
    run(
      Effect.gen(function* () {
        const path = yield* scratchLockPath;
        const first = yield* lockFd(path);
        const second = yield* lockFd(path);

        expect((yield* flock(first, LOCK_EX | LOCK_NB)).returnCode).toBe(0);

        const contended = yield* flock(second, LOCK_EX | LOCK_NB);
        expect(contended.returnCode).toBe(-1);
        expect(contended.errno).toBe(EWOULDBLOCK);

        yield* flock(first, LOCK_UN);
        expect((yield* flock(second, LOCK_EX | LOCK_NB)).returnCode).toBe(0);
        yield* flock(second, LOCK_UN);
      }),
    ));

  test("a Bun holder is visible to python's LOCK_SH probe, so --status sees it", () =>
    run(
      Effect.gen(function* () {
        const path = yield* scratchLockPath;
        const fd = yield* lockFd(path);

        expect((yield* flock(fd, LOCK_EX | LOCK_NB)).returnCode).toBe(0);
        // `_monitor_holder` reads "blocked" as "a monitor is running".
        expect(yield* pythonSharedProbe(path)).toBe(`BLOCKED ${EWOULDBLOCK}`);

        yield* flock(fd, LOCK_UN);
        expect(yield* pythonSharedProbe(path)).toBe('ACQUIRED');
      }),
    ));

  test('the fd from @effect/platform FileSystem.open works, so no node:fs is needed', () =>
    run(
      Effect.gen(function* () {
        const path = yield* scratchLockPath;
        const fileSystem = yield* FileSystem.FileSystem;
        // `open` hands back a branded but genuinely numeric descriptor.
        const file = yield* fileSystem.open(path, { flag: 'a+', mode: 0o600 });

        expect((yield* flock(file.fd, LOCK_EX | LOCK_NB)).returnCode).toBe(0);
        expect(yield* pythonAttempt(path)).toBe(`BLOCKED ${EWOULDBLOCK}`);
        expect((yield* flock(file.fd, LOCK_UN)).returnCode).toBe(0);
      }).pipe(Effect.provide(BunFileSystem.layer)),
    ));

  test('a crashed Bun holder frees the lock, exactly as the monitor comment promises', () =>
    run(
      Effect.gen(function* () {
        const path = yield* scratchLockPath;

        // A child Bun process takes the lock and is killed without unlocking.
        const child = yield* Effect.acquireRelease(
          Effect.sync(() =>
            Bun.spawn(['bun', '-e', HOLD_FOREVER_JS], {
              stdout: 'pipe',
              stderr: 'pipe',
              env: { ...process.env, [LOCK_PATH_VARIABLE]: path },
            }),
          ),
          (spawned) => Effect.ignore(Effect.sync(() => spawned.kill())),
        );

        const reader = child.stdout.getReader();
        const chunk = yield* Effect.tryPromise({
          try: () => reader.read(),
          catch: (cause) => new ScratchError({ reason: 'child read', cause }),
        });
        expect(new TextDecoder().decode(chunk.value).trim()).toBe('0');

        expect(yield* pythonAttempt(path)).toBe(`BLOCKED ${EWOULDBLOCK}`);

        child.kill(9);
        yield* Effect.promise(() => child.exited);

        expect(yield* pythonAttempt(path)).toBe('ACQUIRED');
      }),
    ));
});
