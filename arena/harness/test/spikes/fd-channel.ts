/**
 * Spike support: the parent half of a `socketpair(AF_UNIX, SOCK_STREAM)` IPC
 * channel, expressed in Bun primitives.
 *
 * This is the piece the Python sidecar gets for free from `socket.socketpair`
 * + `subprocess(pass_fds=...)` (see `agent_eval/headless_sidecar.py:1156`).
 * Bun ships no `socketpair`, so we reach for libc through `bun:ffi`; the
 * spawn side needs nothing special because `Bun.spawn`'s `stdio` array
 * accepts raw fds past index 2 and remaps them onto the child's fd 3, 4, ...
 *
 * Throwaway: this exists to be measured by `s1-fd-inheritance.test.ts`, not
 * to be imported by the harness.  The real thing lives behind an Effect
 * service with a `Scope`d fd lifetime.
 */
import { dlopen, FFIType, suffix } from 'bun:ffi';

/** `AF_UNIX` — 1 on both darwin and linux. */
const AF_UNIX = 1;

/** `SOCK_STREAM` — 1 on both darwin and linux. */
const SOCK_STREAM = 1;

const libc = dlopen(`libc.${suffix}`, {
  socketpair: {
    args: [FFIType.int, FFIType.int, FFIType.int, FFIType.ptr],
    returns: FFIType.int,
  },
  close: { args: [FFIType.int], returns: FFIType.int },
});

/** The two ends of one connected socket pair, as raw file descriptors. */
export interface FdPair {
  /** Stays in this process; the harness reads and writes here. */
  readonly parent: number;
  /** Handed to `Bun.spawn` and dropped immediately after. */
  readonly child: number;
}

/** Failure to create the pair, as a value rather than a throw. */
export interface FdPairError {
  readonly _tag: 'FdPairError';
  readonly errno: number;
}

/**
 * `socket.socketpair(AF_UNIX, SOCK_STREAM)`, via libc.
 *
 * Returns the errno as a value on failure — libc's -1 return is not
 * exceptional enough to throw over.
 */
export const openFdPair = (): FdPair | FdPairError => {
  const out = new Int32Array(2);
  const rc = libc.symbols.socketpair(AF_UNIX, SOCK_STREAM, 0, out);
  return rc === 0
    ? { parent: out[0] ?? -1, child: out[1] ?? -1 }
    : { _tag: 'FdPairError', errno: rc };
};

/** Narrowing helper for {@link openFdPair}'s union. */
export const isFdPairError = (value: FdPair | FdPairError): value is FdPairError =>
  '_tag' in value;

/** `close(2)`. Returns libc's status so callers may assert on it. */
export const closeFd = (fd: number): number => libc.symbols.close(fd);

/**
 * Bun's byte reader, named by inference rather than by hand: the stream is
 * `ReadableStream<Uint8Array<ArrayBuffer>>` and its reader carries Bun's
 * `readMany` extension, which no hand-written annotation reproduces.
 */
const readerFor = (fd: number) => Bun.file(fd).stream().getReader();

/** The reader type {@link openCursor} holds. */
export type ByteReader = ReturnType<typeof readerFor>;

/**
 * A position in the byte stream arriving on one fd.
 *
 * Immutable on purpose: `readExactly` hands back the next cursor rather than
 * mutating a buffer, so there is no module- or closure-level mutable state.
 */
export interface ReadCursor {
  readonly reader: ByteReader;
  readonly residue: Uint8Array;
  /** True once the peer closed its end and the stream reported EOF. */
  readonly ended: boolean;
}

/** Start reading the given fd. */
export const openCursor = (fd: number): ReadCursor => ({
  reader: readerFor(fd),
  residue: new Uint8Array(0),
  ended: false,
});

const concat = (left: Uint8Array, right: Uint8Array): Uint8Array => {
  const joined = new Uint8Array(left.length + right.length);
  joined.set(left, 0);
  joined.set(right, left.length);
  return joined;
};

/**
 * Pull exactly `length` bytes, or fewer if the peer hangs up first.
 *
 * Recursive rather than looped, per house style; the recursion depth is one
 * frame per stream chunk, which for the IPC frames in play is single digits.
 */
export const readExactly = async (
  cursor: ReadCursor,
  length: number,
): Promise<readonly [Uint8Array, ReadCursor]> => {
  if (cursor.residue.length >= length || cursor.ended) {
    return [
      cursor.residue.slice(0, length),
      { ...cursor, residue: cursor.residue.slice(length) },
    ];
  }
  const chunk = await cursor.reader.read();
  return chunk.done
    ? readExactly({ ...cursor, ended: true }, length)
    : readExactly({ ...cursor, residue: concat(cursor.residue, chunk.value) }, length);
};

/** A sink that writes to one fd. `Bun.file(fd).writer()` is already this. */
export interface FdWriter {
  readonly send: (bytes: Uint8Array) => Promise<number>;
}

/**
 * Open the write half of the same fd a {@link ReadCursor} is reading.
 *
 * `write` and `flush` each return `number | Promise<number>` depending on
 * whether the pipe took the bytes immediately, so both are awaited — that is
 * also what keeps backpressure honest.
 */
export const openWriter = (fd: number): FdWriter => {
  const sink = Bun.file(fd).writer();
  return {
    send: async (bytes: Uint8Array): Promise<number> => {
      await sink.write(bytes);
      return await sink.flush();
    },
  };
};

/** UTF-8 helpers, so the test reads as text rather than as byte plumbing. */
export const encode = (text: string): Uint8Array => new TextEncoder().encode(text);

/** Decode bytes read off the channel. */
export const decode = (bytes: Uint8Array): string => new TextDecoder().decode(bytes);
