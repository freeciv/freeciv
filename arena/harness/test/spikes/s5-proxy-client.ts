/**
 * Spike S5 support: the bounded-memory read loop the real UpstreamClient will use.
 *
 * The whole point is that nothing here ever calls `response.json()`,
 * `response.arrayBuffer()` or `response.text()` — those buffer the entire body
 * before you can decide you did not want it. Instead we take the reader, count
 * bytes as they arrive, and cancel the reader the moment a cap is crossed. The
 * cancel is in an acquireUseRelease release, so it also runs on failure and on
 * Effect interruption.
 */
import { Data, Effect } from 'effect';

export class UpstreamRequestError extends Data.TaggedError('UpstreamRequestError')<{
  readonly url: string;
  readonly cause: unknown;
}> {}

export class UpstreamBodyError extends Data.TaggedError('UpstreamBodyError')<{
  readonly cause: unknown;
}> {}

/** The 502 the gateway returns when a JSON body blows past the cap. */
export class UpstreamTooLarge extends Data.TaggedError('UpstreamTooLarge')<{
  readonly capBytes: number;
  readonly bytesRead: number;
}> {}

export class UpstreamEmptyBody extends Data.TaggedError('UpstreamEmptyBody')<{
  readonly url: string;
}> {}

export const JSON_CAP_BYTES = 8 * 1024 * 1024;
export const ERROR_DRAIN_CAP_BYTES = 64 * 1024;

export const fetchUpstream = (
  url: string,
  init: RequestInit = {},
): Effect.Effect<Response, UpstreamRequestError> =>
  Effect.tryPromise({
    try: (signal) =>
      fetch(url, { redirect: 'manual', signal, ...init } satisfies RequestInit),
    catch: (cause) => new UpstreamRequestError({ url, cause }),
  });

export const upstreamBody = (
  url: string,
  response: Response,
): Effect.Effect<ReadableStream<Uint8Array>, UpstreamEmptyBody> =>
  response.body === null
    ? Effect.fail(new UpstreamEmptyBody({ url }))
    : Effect.succeed(response.body);

export interface DrainOutcome {
  readonly bytesRead: number;
  readonly chunkCount: number;
  /** performance.now() of the first chunk handed to the consumer. */
  readonly firstChunkAt: number;
  readonly lastChunkAt: number;
  /** True when a chunk pushed us past the cap and we cancelled the reader. */
  readonly truncated: boolean;
}

interface DrainState extends DrainOutcome {
  readonly finished: boolean;
}

const EMPTY: DrainState = {
  bytesRead: 0,
  chunkCount: 0,
  firstChunkAt: Number.NaN,
  lastChunkAt: Number.NaN,
  truncated: false,
  finished: false,
};

const advance = (
  state: DrainState,
  chunk: Uint8Array,
  capBytes: number,
  onChunk: ((chunk: Uint8Array) => void) | undefined,
): DrainState => {
  const now = performance.now();
  onChunk?.(chunk);
  const bytesRead = state.bytesRead + chunk.byteLength;
  return {
    bytesRead,
    chunkCount: state.chunkCount + 1,
    firstChunkAt: state.chunkCount === 0 ? now : state.firstChunkAt,
    lastChunkAt: now,
    truncated: bytesRead > capBytes,
    finished: false,
  };
};

export interface DrainOptions {
  readonly capBytes: number;
  /** Called with each chunk as it lands. Must not retain it. */
  readonly onChunk?: (chunk: Uint8Array) => void;
}

/**
 * Read a body incrementally, stopping (and cancelling the reader) as soon as
 * the running byte count crosses `capBytes`. Peak retention is one chunk.
 */
export const drainBounded = (
  body: ReadableStream<Uint8Array>,
  options: DrainOptions,
): Effect.Effect<DrainOutcome, UpstreamBodyError> =>
  Effect.acquireUseRelease(
    // The reader type is left inferred: `getReader()` is overloaded (BYOB vs
    // default) and naming the type explicitly picks the wrong overload.
    Effect.sync(() => body.getReader()),
    (reader) => {
      const step = (state: DrainState): Effect.Effect<DrainState, UpstreamBodyError> =>
        Effect.tryPromise({
          try: () => reader.read(),
          catch: (cause) => new UpstreamBodyError({ cause }),
        }).pipe(
          Effect.map((result): DrainState =>
            result.done || result.value === undefined
              ? { ...state, finished: true }
              : advance(state, result.value, options.capBytes, options.onChunk),
          ),
        );
      return Effect.iterate(EMPTY, {
        while: (state) => !state.finished && !state.truncated,
        body: step,
      });
    },
    // Runs on success, failure and interruption: this is what actually closes
    // the socket instead of letting the rest of the body drain into memory.
    (reader) => Effect.ignore(Effect.tryPromise(() => reader.cancel())),
  ).pipe(Effect.map(({ finished: _finished, ...outcome }): DrainOutcome => outcome));

/** JSON path: over the cap is a 502, not a buffered 9MiB string. */
export const readJsonCapped = (
  url: string,
  capBytes: number = JSON_CAP_BYTES,
): Effect.Effect<
  DrainOutcome,
  UpstreamRequestError | UpstreamBodyError | UpstreamEmptyBody | UpstreamTooLarge
> =>
  fetchUpstream(url).pipe(
    Effect.flatMap((response) => upstreamBody(url, response)),
    Effect.flatMap((body) => drainBounded(body, { capBytes })),
    Effect.flatMap((outcome) =>
      outcome.truncated
        ? Effect.fail(new UpstreamTooLarge({ capBytes, bytesRead: outcome.bytesRead }))
        : Effect.succeed(outcome),
    ),
  );

export interface CappedCollector {
  readonly onChunk: (chunk: Uint8Array) => void;
  readonly retainedBytes: () => number;
  readonly text: () => string;
}

/** Retains at most `capBytes` of what flows past — for error bodies. */
export const cappedCollector = (capBytes: number): CappedCollector => {
  const state: { parts: ReadonlyArray<Uint8Array>; retained: number } = {
    parts: [],
    retained: 0,
  };
  return {
    onChunk: (chunk) => {
      const room = capBytes - state.retained;
      if (room <= 0) return;
      const keep = chunk.byteLength <= room ? chunk.slice() : chunk.slice(0, room);
      state.parts = [...state.parts, keep];
      state.retained += keep.byteLength;
    },
    retainedBytes: () => state.retained,
    text: () => state.parts.map((part) => new TextDecoder().decode(part)).join(''),
  };
};
