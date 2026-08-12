/**
 * Advisory locks.
 *
 * Ports `_private_advisory_lock` (client.py:531-595), `_v2_state_lock_path` /
 * `_v2_request_lock_path` (580-589) and `_v2_state_lock` / `_v2_request_lock`
 * (714-726).  The monitor's singleton lock is U17's; it is built on
 * {@link withAdvisoryLock}'s primitives but has its own holder-record contract.
 *
 * Two kernel properties are the whole point and are why this is `flock(2)` and
 * not a PID file:
 *
 * - idempotency by construction — a second holder simply cannot acquire;
 * - crash recovery by construction — the kernel releases on process death, so a
 *   `kill -9` leaves nothing stale behind.
 *
 * Neither Node nor Bun exposes `flock`, so it is bound through `bun:ffi`
 * against libc.  If that binding cannot be made (an unexpected platform), the
 * fallback below keeps mutual exclusion via an `O_EXCL` sentinel and pays for
 * it with a liveness probe — the exact bookkeeping the Python was avoiding, and
 * a divergence recorded in NOTES.md §4.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { Effect } from 'effect';
import { LockTimeoutError, PlayerError, playerError, attemptOr } from 'src/errors';
import { V2_REQUEST_LOCK_TIMEOUT_S, V2_STATE_LOCK_TIMEOUT_S } from 'src/constants';
import { PrivateFs, type PrivateFsApi } from 'src/services/private-fs';

export const LOCK_EX = 2;
export const LOCK_UN = 8;
export const LOCK_NB = 4;

export const LOCK_BUSY_MESSAGE =
  'another player command is updating this session; retry once after it finishes';

type FlockFn = (descriptor: number, operation: number) => number;

const LIBC_CANDIDATES = ['libc.dylib', 'libc.so.6', 'libc.so', 'libSystem.dylib'] as const;

/** Bind `flock(2)` once per process; `null` means "no binding on this platform". */
const flockBinding = ((): (() => FlockFn | null) => {
  const cache: { resolved: boolean; value: FlockFn | null } = { resolved: false, value: null };
  return () => {
    if (cache.resolved) return cache.value;
    cache.resolved = true;
    const bound = LIBC_CANDIDATES.flatMap((candidate) =>
      attemptOr(
        (): ReadonlyArray<FlockFn> => {
          // eslint-disable-next-line @typescript-eslint/no-require-imports -- bun:ffi is a builtin
          const ffi = require('bun:ffi') as {
            readonly dlopen: (
              name: string,
              symbols: Record<string, { args: ReadonlyArray<unknown>; returns: unknown }>
            ) => { readonly symbols: Record<string, FlockFn> };
            readonly FFIType: Record<string, unknown>;
          };
          const library = ffi.dlopen(candidate, {
            flock: { args: [ffi.FFIType['i32'], ffi.FFIType['i32']], returns: ffi.FFIType['i32'] },
          });
          const symbol = library.symbols['flock'];
          return typeof symbol === 'function' ? [symbol] : [];
        },
        (): ReadonlyArray<FlockFn> => [] /* this libc spelling is absent here */
      )
    );
    cache.value = bound[0] ?? null;
    if (cache.value !== null) return cache.value;
    return cache.value;
  };
})();

/** Report whether the real `flock(2)` is available. Tests assert this is true. */
export const hasNativeFlock = (): boolean => flockBinding() !== null;

const openFlag = (name: string): number => {
  const table = fs.constants as unknown as Readonly<Record<string, number | undefined>>;
  return table[name] ?? 0;
};

const NOFOLLOW = openFlag('O_NOFOLLOW');
const CLOEXEC = openFlag('O_CLOEXEC');

const sleepMillis = (millis: number): void => {
  // A busy-wait would spin a core for up to 45s; `Atomics.wait` blocks this
  // thread exactly as CPython's `time.sleep` did inside the retry loop.
  const buffer = new Int32Array(new SharedArrayBuffer(4));
  Atomics.wait(buffer, 0, 0, millis);
};

const POLL_MILLIS = 50;

interface HeldLock {
  readonly descriptor: number;
  readonly release: () => void;
}

const acquireNative = (
  flock: FlockFn,
  file: string,
  timeoutS: number
): Effect.Effect<HeldLock, PlayerError | LockTimeoutError> =>
  Effect.suspend<HeldLock, PlayerError | LockTimeoutError, never>(() => {
    const descriptor = attemptOr(
      (): number | PlayerError =>
        fs.openSync(file, fs.constants.O_RDWR | fs.constants.O_CREAT | NOFOLLOW | CLOEXEC, 0o600),
      () => playerError('cannot safely lock private player state')
    );
    if (typeof descriptor !== 'number') return Effect.fail(descriptor);
    const stat = fs.fstatSync(descriptor);
    if (!stat.isFile() || (stat.mode & 0o777) !== 0o600) {
      fs.closeSync(descriptor);
      return Effect.fail(playerError('private state lock must be a mode-0600 file'));
    }
    const deadline = Date.now() + timeoutS * 1000;
    const poll = (): Effect.Effect<HeldLock, LockTimeoutError> => {
      if (flock(descriptor, LOCK_EX | LOCK_NB) === 0) {
        return Effect.succeed({
          descriptor,
          release: () => {
            attemptOr(
              () => flock(descriptor, LOCK_UN),
              () => 0 /* the kernel releases on close anyway */
            );
            fs.closeSync(descriptor);
          },
        });
      }
      if (Date.now() >= deadline) {
        fs.closeSync(descriptor);
        return Effect.fail(new LockTimeoutError({ message: LOCK_BUSY_MESSAGE, path: file }));
      }
      sleepMillis(POLL_MILLIS);
      return Effect.suspend(poll);
    };
    return poll();
  });

const acquireSentinel = (
  file: string,
  timeoutS: number
): Effect.Effect<HeldLock, PlayerError | LockTimeoutError> =>
  Effect.suspend<HeldLock, PlayerError | LockTimeoutError, never>(() => {
    const sentinel = `${file}.held`;
    const deadline = Date.now() + timeoutS * 1000;
    const claim = (): HeldLock | null =>
      attemptOr(
        (): HeldLock => {
          const descriptor = fs.openSync(
            sentinel,
            fs.constants.O_WRONLY | fs.constants.O_CREAT | fs.constants.O_EXCL | NOFOLLOW | CLOEXEC,
            0o600
          );
          fs.writeSync(descriptor, Buffer.from(`${process.pid}\n`, 'utf8'));
          return {
            descriptor,
            release: () => {
              fs.closeSync(descriptor);
              attemptOr(
                () => fs.unlinkSync(sentinel),
                () => undefined /* already gone */
              );
            },
          };
        },
        () => null
      );
    // A sentinel whose writer is gone is stale; the liveness probe is the
    // bookkeeping `flock` would have made unnecessary.
    const reapedStale = (): boolean =>
      attemptOr(
        () => {
          const holder = Number.parseInt(fs.readFileSync(sentinel, 'utf8').trim(), 10);
          if (!Number.isInteger(holder)) return false;
          const alive = attemptOr(
            () => {
              process.kill(holder, 0);
              return true;
            },
            () => false
          );
          if (alive) return false;
          fs.unlinkSync(sentinel);
          return true;
        },
        () => false /* the sentinel vanished between open and read */
      );
    const poll = (): Effect.Effect<HeldLock, LockTimeoutError> => {
      const held = claim();
      if (held !== null) return Effect.succeed(held);
      if (reapedStale()) return Effect.suspend(poll);
      if (Date.now() >= deadline) {
        return Effect.fail(new LockTimeoutError({ message: LOCK_BUSY_MESSAGE, path: file }));
      }
      sleepMillis(POLL_MILLIS);
      return Effect.suspend(poll);
    };
    return poll();
  });

/**
 * Hold `file` exclusively for the duration of `body`.
 *
 * The lock file is created inside the private-state sandbox (mode 0600, no
 * symlinked component), and is released whatever `body` does — including
 * interruption, which is what makes `monitor` and `do` safe to Ctrl-C.
 */
export const withAdvisoryLock = <A, E, R>(
  target: string,
  timeoutS: number,
  body: Effect.Effect<A, E, R>
): Effect.Effect<A, E | PlayerError | LockTimeoutError, R | PrivateFs> =>
  Effect.gen(function* () {
    const files = yield* PrivateFs;
    const { relative } = yield* files.resolve(target);
    const parent = yield* files.openDirectory(relative.slice(0, -1), { create: true });
    const file = path.join(parent, relative[relative.length - 1] as string);
    const flock = flockBinding();
    const acquire = flock === null ? acquireSentinel(file, timeoutS) : acquireNative(flock, file, timeoutS);
    return yield* Effect.acquireUseRelease(
      acquire,
      () => body,
      (held) => Effect.sync(held.release)
    );
  });

// ---------------------------------------------------------------------------
// The two named session locks
// ---------------------------------------------------------------------------

/** `pathlib.Path.with_suffix`: replace the final extension, or append one. */
export const withSuffix = (target: string, suffix: string): string => {
  const parsed = path.parse(target);
  return path.join(parsed.dir, parsed.name + suffix);
};

/** `_v2_state_path` — the `.v2-state` sibling of a session file. */
export const v2StatePath = (sessionPath: string): string => withSuffix(sessionPath, '.v2-state');

export const v2StateLockPath = (sessionPath: string): string => `${v2StatePath(sessionPath)}.lock`;

export const v2RequestLockPath = (sessionPath: string): string =>
  withSuffix(sessionPath, '.v2-request.lock');

export const monitorLockPath = (sessionPath: string): string =>
  withSuffix(sessionPath, '.monitor.lock');

export const withV2StateLock = <A, E, R>(
  sessionPath: string,
  body: Effect.Effect<A, E, R>
): Effect.Effect<A, E | PlayerError | LockTimeoutError, R | PrivateFs> =>
  withAdvisoryLock(v2StateLockPath(sessionPath), V2_STATE_LOCK_TIMEOUT_S, body);

export const withV2RequestLock = <A, E, R>(
  sessionPath: string,
  body: Effect.Effect<A, E, R>
): Effect.Effect<A, E | PlayerError | LockTimeoutError, R | PrivateFs> =>
  withAdvisoryLock(v2RequestLockPath(sessionPath), V2_REQUEST_LOCK_TIMEOUT_S, body);

/** Escape hatch for U17, whose lock file doubles as the holder record. */
export const lockFilePath = (
  files: PrivateFsApi,
  target: string
): Effect.Effect<string, PlayerError> =>
  Effect.gen(function* () {
    const { relative } = yield* files.resolve(target);
    const parent = yield* files.openDirectory(relative.slice(0, -1), { create: true });
    return path.join(parent, relative[relative.length - 1] as string);
  });
