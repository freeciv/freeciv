/**
 * Spike S3 support: `flock(2)` from Bun, as an errors-are-values API.
 *
 * The monitor singleton and the gateway ready-file in `play/client.py` are
 * guarded by `fcntl.flock` on a mode-0600 file opened `O_RDWR | O_CREAT |
 * O_NOFOLLOW | O_CLOEXEC` (see `_monitor_lock` around play/client.py:596).
 * A TypeScript arena has to interoperate with that exact lock, so this module
 * reaches the same libc call through `bun:ffi` rather than inventing a second
 * lock protocol beside it.
 *
 * Throwaway: it exists to be measured by `s3-flock.test.ts`.
 */
import { Data, Effect, Either, Ref, type Scope } from 'effect';
import { FFIType, dlopen, read } from 'bun:ffi';
import { closeSync, constants, fstatSync, openSync } from 'node:fs';

/** darwin `sys/file.h` operations — identical values to Linux, checked below. */
export const LOCK_SH = 1;
export const LOCK_EX = 2;
export const LOCK_NB = 4;
export const LOCK_UN = 8;

/** darwin `EAGAIN === EWOULDBLOCK === 35`; this is what Python raises as `BlockingIOError`. */
export const EWOULDBLOCK = 35;

/** Reported when libc will not tell us where `errno` lives. */
export const UNKNOWN_ERRNO = -1;

/**
 * darwin `O_CLOEXEC`. It is spelled out because `node:fs`'s `constants` does
 * not carry it under Bun — it reads back `undefined`, which would silently
 * contribute 0 to the flag word instead of failing.
 */
export const O_CLOEXEC = 0x0100_0000;

/** The mode `play/client.py` insists on before it will trust a lock file. */
export const LOCK_FILE_MODE = 0o600;

export class FfiUnavailable extends Data.TaggedError('FfiUnavailable')<{
  readonly reason: string;
  readonly cause: unknown;
}> {}

export class LockFileError extends Data.TaggedError('LockFileError')<{
  readonly reason: string;
  readonly path: string;
  readonly cause: unknown;
}> {}

/** What `flock(2)` actually told us: the return code and, on failure, `errno`. */
export interface FlockOutcome {
  readonly returnCode: number;
  readonly errno: number;
}

interface Libc {
  readonly flock: (fd: number, operation: number) => number;
  readonly currentErrno: () => number;
}

const loadLibc: Effect.Effect<Libc, FfiUnavailable> = Effect.try({
  try: (): Libc => {
    const library = dlopen('libSystem.B.dylib', {
      flock: { args: [FFIType.i32, FFIType.i32], returns: FFIType.i32 },
      // `errno` is a macro over a per-thread location; libc exposes it as `__error()`.
      __error: { args: [], returns: FFIType.ptr },
    });
    return {
      flock: (fd, operation) => library.symbols.flock(fd, operation),
      currentErrno: () => {
        const location = library.symbols.__error();
        // A null `__error()` cannot happen on a live libc, but the binding is
        // typed `Pointer | null`, and guessing an errno would be worse.
        return location === null ? UNKNOWN_ERRNO : read.i32(location, 0);
      },
    };
  },
  catch: (cause) =>
    new FfiUnavailable({ reason: 'dlopen(libSystem.B.dylib) failed', cause }),
});

/**
 * Loaded once, as a value: a failure to `dlopen` is reported by every call
 * rather than thrown at import time, which is what lets the test assert the
 * fallback story instead of crashing the file.
 */
const libc: Either.Either<Libc, FfiUnavailable> = Effect.runSync(
  Effect.either(loadLibc),
);

export const ffiAvailable = (): boolean => Either.isRight(libc);

/** `flock(fd, operation)` — never throws, never blocks when `LOCK_NB` is set. */
export const flock = (
  fd: number,
  operation: number,
): Effect.Effect<FlockOutcome, FfiUnavailable> =>
  Effect.map(libc, (lib) => {
    const returnCode = lib.flock(fd, operation);
    return {
      returnCode,
      errno: returnCode === 0 ? 0 : lib.currentErrno(),
    };
  });

/**
 * Open a lock file exactly as `play/client.py` does, and hand back the numeric
 * fd that `flock` needs. `O_TRUNC` is deliberately absent: the monitor lock
 * file doubles as the holder record, so opening it must not erase it.
 */
const openLockFd = (path: string): Effect.Effect<number, LockFileError> =>
  Effect.try({
    try: () =>
      openSync(
        path,
        constants.O_RDWR | constants.O_CREAT | constants.O_NOFOLLOW | O_CLOEXEC,
        LOCK_FILE_MODE,
      ),
    catch: (cause) => new LockFileError({ reason: 'open failed', path, cause }),
  });

/** A lock-file fd scoped to the surrounding `Effect.scoped`, closed on exit. */
export const lockFd = (
  path: string,
): Effect.Effect<number, LockFileError, Scope.Scope> =>
  Effect.acquireRelease(openLockFd(path), (fd) =>
    Effect.ignore(Effect.try(() => closeSync(fd))),
  );

export const fileMode = (fd: number): Effect.Effect<number, LockFileError> =>
  Effect.try({
    try: () => fstatSync(fd).mode & 0o777,
    catch: (cause) =>
      new LockFileError({ reason: 'fstat failed', path: `fd:${fd}`, cause }),
  });

export class PythonError extends Data.TaggedError('PythonError')<{
  readonly reason: string;
  readonly detail: string;
}> {}

/** The lock path travels in the environment, so no path is ever interpolated into source. */
export const LOCK_PATH_VARIABLE = 'S3_LOCK_PATH';

const childEnvironment = (
  path: string,
): Record<string, string | undefined> => ({
  ...process.env,
  [LOCK_PATH_VARIABLE]: path,
});

const OPEN_LOCK_PY = `
import fcntl, os, sys
fd = os.open(os.environ["${LOCK_PATH_VARIABLE}"], os.O_RDWR | os.O_CREAT, 0o600)
`;

/**
 * The Python side of the experiment: it takes the same non-blocking exclusive
 * lock the monitor takes, and reports in the same vocabulary either way.
 */
const ATTEMPT_PY = `${OPEN_LOCK_PY}
try:
    fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
except BlockingIOError as exc:
    print("BLOCKED", exc.errno)
else:
    print("ACQUIRED")
    fcntl.flock(fd, fcntl.LOCK_UN)
`;

/**
 * The shared-lock probe `_monitor_holder` uses to answer "is a monitor
 * running?" (play/client.py:701) — it must see an exclusive holder.
 */
const PROBE_PY = `${OPEN_LOCK_PY}
try:
    fcntl.flock(fd, fcntl.LOCK_SH | fcntl.LOCK_NB)
except BlockingIOError as exc:
    print("BLOCKED", exc.errno)
else:
    print("ACQUIRED")
    fcntl.flock(fd, fcntl.LOCK_UN)
`;

const pythonRun = (
  source: string,
  path: string,
): Effect.Effect<string, PythonError> =>
  Effect.flatMap(
    Effect.sync(() =>
      Bun.spawnSync(['python3', '-c', source], {
        stdout: 'pipe',
        stderr: 'pipe',
        env: childEnvironment(path),
      }),
    ),
    (result) =>
      result.exitCode === 0
        ? Effect.succeed(result.stdout.toString().trim())
        : Effect.fail(
            new PythonError({
              reason: 'python3 attempt exited non-zero',
              detail: result.stderr.toString().trim(),
            }),
          ),
  );

/** One-shot: what does Python see when it tries to take the lock right now? */
export const pythonAttempt = (
  path: string,
): Effect.Effect<string, PythonError> => pythonRun(ATTEMPT_PY, path);

/** The same question asked with `LOCK_SH`, the way `--status` asks it. */
export const pythonSharedProbe = (
  path: string,
): Effect.Effect<string, PythonError> => pythonRun(PROBE_PY, path);

const HOLDER_PY = `${OPEN_LOCK_PY}
fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
print("HELD")
sys.stdin.readline()
fcntl.flock(fd, fcntl.LOCK_UN)
print("RELEASED")
sys.stdin.readline()
`;

/** A live Python process that is holding the lock until told to let go. */
export interface PythonHolder {
  /** Unlock in Python and wait for it to confirm; the fd stays open. */
  readonly release: Effect.Effect<void, PythonError>;
}

/** Just the part of a stream reader this file needs — Bun's and the DOM's differ. */
interface ChunkReader {
  readonly read: () => Promise<{
    readonly done?: boolean;
    readonly value?: Uint8Array | undefined;
  }>;
}

const readLine = (
  reader: ChunkReader,
  buffer: Ref.Ref<string>,
  decoder: TextDecoder,
): Effect.Effect<string, PythonError> =>
  Effect.flatMap(Ref.get(buffer), (pending) => {
    const newline = pending.indexOf('\n');
    return newline >= 0
      ? Effect.as(
          Ref.set(buffer, pending.slice(newline + 1)),
          pending.slice(0, newline).trim(),
        )
      : Effect.flatMap(
          Effect.tryPromise({
            try: () => reader.read(),
            catch: (cause) =>
              new PythonError({
                reason: 'reading holder stdout failed',
                detail: String(cause),
              }),
          }),
          (chunk) =>
            chunk.done === true || chunk.value === undefined
              ? Effect.fail(
                  new PythonError({
                    reason: 'holder stdout closed before it reported',
                    detail: pending,
                  }),
                )
              : Effect.flatMap(
                  Ref.set(buffer, pending + decoder.decode(chunk.value)),
                  () => readLine(reader, buffer, decoder),
                ),
        );
  });

/**
 * Spawn Python, wait until it confirms it holds the lock, and guarantee the
 * process is gone when the scope closes — a leaked holder would wedge every
 * later test on the same file.
 */
export const pythonHolder = (
  path: string,
): Effect.Effect<PythonHolder, PythonError, Scope.Scope> =>
  Effect.flatMap(
    Effect.acquireRelease(
      Effect.sync(() =>
        Bun.spawn(['python3', '-u', '-c', HOLDER_PY], {
          stdin: 'pipe',
          stdout: 'pipe',
          stderr: 'pipe',
          env: childEnvironment(path),
        }),
      ),
      (child) => Effect.ignore(Effect.sync(() => child.kill())),
    ),
    (child) =>
      Effect.flatMap(Ref.make(''), (buffer) => {
        const reader = child.stdout.getReader();
        const decoder = new TextDecoder();
        const expect = (
          word: string,
        ): Effect.Effect<void, PythonError> =>
          Effect.flatMap(
            Effect.timeoutFail(readLine(reader, buffer, decoder), {
              duration: '10 seconds',
              onTimeout: () =>
                new PythonError({
                  reason: `timed out waiting for ${word}`,
                  detail: path,
                }),
            }),
            (line) =>
              line === word
                ? Effect.void
                : Effect.fail(
                    new PythonError({
                      reason: `expected ${word} from the holder`,
                      detail: line,
                    }),
                  ),
          );
        // `write`/`flush` are sync-or-async depending on the sink; resolving
        // whatever they return keeps this one shape either way.
        const sink = (
          operation: () => number | Promise<number>,
        ): Effect.Effect<number, PythonError> =>
          Effect.tryPromise({
            try: () => Promise.resolve(operation()),
            catch: (cause) =>
              new PythonError({
                reason: 'writing to the holder failed',
                detail: String(cause),
              }),
          });
        const nudge = Effect.zipRight(
          sink(() => child.stdin.write('go\n')),
          sink(() => child.stdin.flush()),
        );
        return Effect.as(expect('HELD'), {
          release: Effect.flatMap(nudge, () => expect('RELEASED')),
        });
      }),
  );
