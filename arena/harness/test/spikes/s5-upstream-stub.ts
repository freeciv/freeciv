/**
 * Spike S5 support: a stub upstream that can misbehave the way a real one does.
 *
 * Throwaway. Everything here exists to give the proxy something to stream from:
 * an oversized JSON body, a 50MiB binary that is deliberately slow to send, an
 * endless stream, and an error response with a body far larger than we want to
 * keep. Bodies are produced chunk-by-chunk from a `pull` source so the server
 * only ever holds one chunk — backpressure, not a 50MiB buffer.
 */

export const CHUNK_BYTES = 256 * 1024;
export const SMALL_CHUNK_BYTES = 8 * 1024;

export const JSON_CHUNKS = 36; // 36 * 256KiB = 9MiB of payload
export const BINARY_CHUNKS = 200; // 200 * 256KiB = 50MiB exactly
export const BINARY_TOTAL_BYTES = BINARY_CHUNKS * CHUNK_BYTES;
export const ERROR_CHUNKS = 256; // 256 * 8KiB = 2MiB error body

interface ChunkedBodyParams {
  readonly head: string | undefined;
  readonly tail: string | undefined;
  readonly chunk: Uint8Array;
  readonly chunkCount: number;
  readonly delayMs: number;
  readonly onFinish: (() => void) | undefined;
}

const encoder = new TextEncoder();

const chunkedBody = (params: ChunkedBodyParams): ReadableStream<Uint8Array> => {
  const cursor = { sent: 0, headSent: false };
  return new ReadableStream<Uint8Array>({
    async pull(controller) {
      if (!cursor.headSent) {
        cursor.headSent = true;
        if (params.head !== undefined) {
          controller.enqueue(encoder.encode(params.head));
          return;
        }
      }
      if (cursor.sent >= params.chunkCount) {
        if (params.tail !== undefined) {
          controller.enqueue(encoder.encode(params.tail));
        }
        params.onFinish?.();
        controller.close();
        return;
      }
      cursor.sent += 1;
      // slice() so each enqueue owns its buffer; the template is reused, the
      // copies are short-lived garbage.
      controller.enqueue(params.chunk.slice());
      if (params.delayMs > 0) {
        await Bun.sleep(params.delayMs);
      }
    },
  });
};

/** An endless, slow stream that stops the moment the consumer goes away. */
const endlessBody = (onCancel: () => void): ReadableStream<Uint8Array> => {
  const live = { cancelled: false };
  const chunk = new Uint8Array(SMALL_CHUNK_BYTES).fill(0x2e);
  return new ReadableStream<Uint8Array>({
    async pull(controller) {
      if (live.cancelled) return;
      controller.enqueue(chunk.slice());
      await Bun.sleep(15);
    },
    cancel() {
      live.cancelled = true;
      onCancel();
    },
  });
};

export interface UpstreamStub {
  readonly url: string;
  /** performance.now() at which the server enqueued the final binary chunk. */
  readonly binaryFinishedAt: () => number;
  /** How many times a client disconnect cancelled the endless stream. */
  readonly endlessCancels: () => number;
  readonly stop: () => void;
}

export const startUpstreamStub = (): UpstreamStub => {
  const state = { binaryFinishedAt: Number.NaN, endlessCancels: 0 };

  const jsonChunk = new Uint8Array(CHUNK_BYTES).fill(0x61); // 'a'
  const binaryChunk = crypto.getRandomValues(new Uint8Array(CHUNK_BYTES));
  const errorChunk = new Uint8Array(SMALL_CHUNK_BYTES).fill(0x45); // 'E'

  const handlers: Record<string, () => Response> = {
    // ~9MiB of syntactically valid JSON, streamed in 256KiB chunks.
    '/json-9mib': () =>
      new Response(
        chunkedBody({
          head: '{"items":["',
          tail: '"]}',
          chunk: jsonChunk,
          chunkCount: JSON_CHUNKS,
          delayMs: 0,
          onFinish: undefined,
        }),
        { headers: { 'content-type': 'application/json' } },
      ),

    // 50MiB "mp4", deliberately slow so send and receive must interleave.
    '/binary-50mib': () =>
      new Response(
        chunkedBody({
          head: undefined,
          tail: undefined,
          chunk: binaryChunk,
          chunkCount: BINARY_CHUNKS,
          delayMs: 2,
          onFinish: () => {
            state.binaryFinishedAt = performance.now();
          },
        }),
        { headers: { 'content-type': 'video/mp4' } },
      ),

    '/endless': () =>
      new Response(
        endlessBody(() => {
          state.endlessCancels += 1;
        }),
        { headers: { 'content-type': 'application/octet-stream' } },
      ),

    // A 2MiB error body: we want at most 64KiB of it.
    '/error-2mib': () =>
      new Response(
        chunkedBody({
          head: undefined,
          tail: undefined,
          chunk: errorChunk,
          chunkCount: ERROR_CHUNKS,
          delayMs: 0,
          onFinish: undefined,
        }),
        { status: 500, headers: { 'content-type': 'text/plain' } },
      ),

    '/ping': () => new Response('pong', { headers: { 'content-type': 'text/plain' } }),
  };

  const server = Bun.serve({
    port: 0,
    idleTimeout: 0,
    fetch(request) {
      const path = new URL(request.url).pathname;
      const handler = handlers[path];
      return handler === undefined ? new Response('not found', { status: 404 }) : handler();
    },
  });

  return {
    url: server.url.origin,
    binaryFinishedAt: () => state.binaryFinishedAt,
    endlessCancels: () => state.endlessCancels,
    stop: () => {
      void server.stop(true);
    },
  };
};
