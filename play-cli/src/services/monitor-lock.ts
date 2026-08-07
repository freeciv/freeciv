/**
 * The persistent monitor's singleton lock, and the holder record inside it.
 *
 * Ports `_monitor_lock_path` (client.py:592-594), `_monitor_lock` (596-662),
 * `_read_monitor_holder` (665-673) and `_monitor_holder` (676-711).
 *
 * Two properties come free from the kernel rather than from bookkeeping, and
 * both matter for a process meant to outlive individual turns:
 *
 * - **idempotency by construction** — a second `play monitor` cannot start, so
 *   there is no PID file, no liveness heuristic and no reaping;
 * - **crash recovery by construction** — an `flock` is released when the holder
 *   dies, so a monitor killed by `kill -9`, a crash or a laptop shutdown leaves
 *   nothing stale behind and the next one simply acquires.
 *
 * The lock file doubles as the holder record, written *after* the lock is won,
 * so `--status` and `--stop` read it without a second file to keep in step
 * with it.
 *
 * DIVERGENCE (NOTES.md §15.1): `src/services/locks.ts` binds `flock(2)` through
 * `bun:ffi` for its own use and exports only `hasNativeFlock`, so the binding is
 * repeated here rather than reached into.  Core owns that file; U17 does not
 * edit it.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { Effect } from 'effect';
import { PlayerError, playerError } from 'src/errors';
import { isJsonObject, type JsonObject } from 'src/schema/primitives';
import { pyJsonDumps } from 'src/services/json-output';
import { monitorLockPath } from 'src/services/locks';
import { PrivateFs, type PrivateFsApi } from 'src/services/private-fs';

export { monitorLockPath };

const LOCK_EX = 2;
const LOCK_SH = 1;
const LOCK_NB = 4;
const LOCK_UN = 8;

/** The most of the holder record CPython ever read back. */
const HOLDER_BYTES = 4096;

type FlockFn = (descriptor: number, operation: number) => number;

const LIBC_CANDIDATES = ['libc.dylib', 'libc.so.6', 'libc.so', 'libSystem.dylib'] as const;

const flockBinding = ((): (() => FlockFn | null) => {
  const cache: { resolved: boolean; value: FlockFn | null } = { resolved: false, value: null };
  return () => {
    if (cache.resolved) return cache.value;
    cache.resolved = true;
    for (const candidate of LIBC_CANDIDATES) {
      try {
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
        if (typeof symbol === 'function') {
          cache.value = symbol;
          return cache.value;
        }
      } catch {
        /* try the next candidate */
      }
    }
    return cache.value;
  };
})();

const openFlag = (name: string): number => {
  const table = fs.constants as unknown as Readonly<Record<string, number | undefined>>;
  return table[name] ?? 0;
};

const NOFOLLOW = openFlag('O_NOFOLLOW');
const CLOEXEC = openFlag('O_CLOEXEC');

// ---------------------------------------------------------------------------
// _read_monitor_holder
// ---------------------------------------------------------------------------

/**
 * Read the running monitor's own record of itself, or an empty one.
 *
 * Anything unreadable is `{}` rather than an error: the record is a courtesy
 * for `--status`, and a monitor that is demonstrably alive must not be reported
 * as absent because its own JSON was truncated by a crash mid-write.
 */
export const readMonitorHolder = (descriptor: number): JsonObject => {
  try {
    const buffer = Buffer.alloc(HOLDER_BYTES);
    const read = fs.readSync(descriptor, buffer, 0, HOLDER_BYTES, 0);
    const value = JSON.parse(buffer.subarray(0, read).toString('utf8')) as unknown;
    return isJsonObject(value) ? value : {};
  } catch {
    return {};
  }
};

// ---------------------------------------------------------------------------
// The lock file, as a path inside the private-state sandbox
// ---------------------------------------------------------------------------

/**
 * Resolve the lock file, creating its directory only when asked.
 *
 * `_monitor_lock` walks with `create=True`; `_monitor_holder` walks with
 * `create=False` and answers "nobody" when the walk fails, because a workspace
 * with no state directory plainly has no monitor in it.
 */
const lockFile = (
  files: PrivateFsApi,
  sessionPath: string,
  create: boolean
): Effect.Effect<string, PlayerError> =>
  Effect.gen(function* () {
    const { relative } = yield* files.resolve(monitorLockPath(sessionPath));
    const parent = yield* files.openDirectory(relative.slice(0, -1), { create });
    return path.join(parent, relative[relative.length - 1] as string);
  });

const sentinelOf = (file: string): string => `${file}.held`;

const isLive = (pid: number): boolean => {
  try {
    process.kill(pid, 0);
    return true;
  } catch (cause) {
    return (cause as NodeJS.ErrnoException).code === 'EPERM';
  }
};

// ---------------------------------------------------------------------------
// _monitor_lock
// ---------------------------------------------------------------------------

interface HeldMonitorLock {
  /** The recorded holder when the lock was already taken; `null` when we own it. */
  readonly running: JsonObject | null;
  readonly release: () => void;
}

const writeHolder = (descriptor: number, holder: JsonObject): void => {
  const record = Buffer.from(pyJsonDumps(holder, { sortKeys: true }), 'utf8');
  fs.ftruncateSync(descriptor, 0);
  fs.writeSync(descriptor, record, 0, record.length, 0);
  fs.fsyncSync(descriptor);
};

const openLockFile = (file: string): number =>
  fs.openSync(file, fs.constants.O_RDWR | fs.constants.O_CREAT | NOFOLLOW | CLOEXEC, 0o600);

const acquireNative = (
  flock: FlockFn,
  file: string,
  holder: JsonObject
): Effect.Effect<HeldMonitorLock, PlayerError> =>
  Effect.suspend<HeldMonitorLock, PlayerError, never>(() => {
    const opened = ((): number | PlayerError => {
      try {
        return openLockFile(file);
      } catch {
        return playerError('cannot safely lock the monitor');
      }
    })();
    if (typeof opened !== 'number') return Effect.fail(opened);
    const stat = fs.fstatSync(opened);
    if (!stat.isFile() || (stat.mode & 0o777) !== 0o600) {
      fs.closeSync(opened);
      return Effect.fail(playerError('the monitor lock must be a mode-0600 file'));
    }
    if (flock(opened, LOCK_EX | LOCK_NB) !== 0) {
      // Somebody else holds it; say who, and take nothing.
      const running = readMonitorHolder(opened);
      return Effect.succeed({
        running,
        release: () => {
          fs.closeSync(opened);
        },
      });
    }
    try {
      writeHolder(opened, holder);
    } catch {
      try {
        flock(opened, LOCK_UN);
      } catch {
        /* the close below releases it anyway */
      }
      fs.closeSync(opened);
      return Effect.fail(playerError('cannot safely lock the monitor'));
    }
    return Effect.succeed({
      running: null,
      release: () => {
        try {
          fs.ftruncateSync(opened, 0);
          flock(opened, LOCK_UN);
        } catch {
          /* the kernel releases on close anyway */
        }
        fs.closeSync(opened);
      },
    });
  });

/**
 * The `flock`-less fallback, for a platform where `bun:ffi` cannot bind libc.
 *
 * Mutual exclusion survives (an `O_EXCL` sentinel), crash recovery does not —
 * it is paid for with the liveness probe the Python was deliberately avoiding.
 * NOTES.md §4 already records the same divergence for the state locks.
 */
const acquireSentinel = (file: string, holder: JsonObject): Effect.Effect<HeldMonitorLock, PlayerError> =>
  Effect.suspend<HeldMonitorLock, PlayerError, never>(() => {
    const sentinel = sentinelOf(file);
    const opened = ((): number | PlayerError => {
      try {
        return openLockFile(file);
      } catch {
        return playerError('cannot safely lock the monitor');
      }
    })();
    if (typeof opened !== 'number') return Effect.fail(opened);
    const stat = fs.fstatSync(opened);
    if (!stat.isFile() || (stat.mode & 0o777) !== 0o600) {
      fs.closeSync(opened);
      return Effect.fail(playerError('the monitor lock must be a mode-0600 file'));
    }
    for (let attempt = 0; attempt < 2; attempt += 1) {
      try {
        const guard = fs.openSync(
          sentinel,
          fs.constants.O_WRONLY | fs.constants.O_CREAT | fs.constants.O_EXCL | NOFOLLOW | CLOEXEC,
          0o600
        );
        fs.writeSync(guard, Buffer.from(`${process.pid}\n`, 'utf8'));
        fs.closeSync(guard);
        writeHolder(opened, holder);
        return Effect.succeed({
          running: null,
          release: () => {
            try {
              fs.ftruncateSync(opened, 0);
              fs.unlinkSync(sentinel);
            } catch {
              /* already gone */
            }
            fs.closeSync(opened);
          },
        });
      } catch {
        const stale = ((): boolean => {
          try {
            const pid = Number.parseInt(fs.readFileSync(sentinel, 'utf8').trim(), 10);
            return Number.isInteger(pid) && !isLive(pid);
          } catch {
            return false;
          }
        })();
        if (!stale) break;
        try {
          fs.unlinkSync(sentinel);
        } catch {
          /* another process reaped it first */
        }
      }
    }
    const running = readMonitorHolder(opened);
    return Effect.succeed({
      running,
      release: () => {
        fs.closeSync(opened);
      },
    });
  });

/**
 * Hold the persistent monitor's singleton lock, or report who does.
 *
 * `body` is handed the lock file's recorded holder when the lock is already
 * taken, and `null` when this process now owns it — the exact shape of the
 * Python contextmanager's yield.
 */
export const withMonitorLock = <A, E, R>(
  sessionPath: string,
  holder: JsonObject,
  body: (running: JsonObject | null) => Effect.Effect<A, E, R>
): Effect.Effect<A, E | PlayerError, R | PrivateFs> =>
  Effect.gen(function* () {
    const files = yield* PrivateFs;
    const file = yield* lockFile(files, sessionPath, true);
    const flock = flockBinding();
    return yield* Effect.acquireUseRelease(
      flock === null ? acquireSentinel(file, holder) : acquireNative(flock, file, holder),
      (held) => body(held.running),
      (held) => Effect.sync(held.release)
    );
  });

// ---------------------------------------------------------------------------
// _monitor_holder
// ---------------------------------------------------------------------------

/**
 * Report the running monitor, or `null` when the lock is free.
 *
 * Probing is the same non-blocking `flock`: if a shared lock can be taken then
 * nobody holds it exclusively, which is a stronger answer than any PID file
 * could give.
 */
export const monitorHolder = (
  sessionPath: string
): Effect.Effect<JsonObject | null, never, PrivateFs> =>
  Effect.gen(function* () {
    const files = yield* PrivateFs;
    const resolved = yield* Effect.either(lockFile(files, sessionPath, false));
    if (resolved._tag === 'Left') return null;
    const file = resolved.right;
    return yield* Effect.sync(() => {
      const opened = ((): number | null => {
        try {
          return fs.openSync(file, fs.constants.O_RDONLY | NOFOLLOW | CLOEXEC);
        } catch {
          return null;
        }
      })();
      if (opened === null) return null;
      try {
        const flock = flockBinding();
        if (flock === null) {
          const sentinel = sentinelOf(file);
          const pid = ((): number | null => {
            try {
              const value = Number.parseInt(fs.readFileSync(sentinel, 'utf8').trim(), 10);
              return Number.isInteger(value) ? value : null;
            } catch {
              return null;
            }
          })();
          if (pid === null || !isLive(pid)) return null;
          return readMonitorHolder(opened);
        }
        if (flock(opened, LOCK_SH | LOCK_NB) !== 0) return readMonitorHolder(opened);
        flock(opened, LOCK_UN);
        return null;
      } catch {
        return null;
      } finally {
        fs.closeSync(opened);
      }
    });
  });
