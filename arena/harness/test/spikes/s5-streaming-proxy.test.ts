/**
 * SPIKE S5 — bounded-memory streaming proxy.
 *
 * Question: can the gateway enforce body caps and proxy a 50MiB binary without
 * ever holding the body in memory, using plain `fetch(redirect:"manual")` +
 * incremental reader reads, wrapped in Effect?
 *
 * Three things have to be true:
 *   (a) a 9MiB JSON body is abandoned just past the 8MiB cap (502 UpstreamTooLarge)
 *       and repeating that five times does not grow process RSS;
 *   (b) a 50MiB binary reaches the consumer *while* the upstream is still
 *       sending it (interleaving), with an exact byte total and flat RSS;
 *   (c) a consumer that disconnects mid-stream neither crashes nor hangs the
 *       process, and the next request still works.
 *
 * The stub upstream runs in this same process, so the RSS numbers below cover
 * both sides of the proxy — a stricter bound than a real deployment would face.
 */
import { afterAll, beforeAll, describe, expect, test } from 'bun:test';
import { Effect } from 'effect';
import {
  BINARY_TOTAL_BYTES,
  CHUNK_BYTES,
  startUpstreamStub,
  type UpstreamStub,
} from './s5-upstream-stub.ts';
import {
  cappedCollector,
  drainBounded,
  ERROR_DRAIN_CAP_BYTES,
  fetchUpstream,
  JSON_CAP_BYTES,
  readJsonCapped,
  UpstreamBodyError,
  upstreamBody,
} from './s5-proxy-client.ts';

const MiB = 1024 * 1024;

/** Full sync GC first, so we measure retention rather than uncollected garbage. */
const rssMiB = (): number => {
  Bun.gc(true);
  return process.memoryUsage.rss() / MiB;
};

const round = (value: number): number => Math.round(value * 100) / 100;

const report = (label: string, value: string): void => {
  console.log(`  [S5] ${label}: ${value}`);
};

const stub: { current: UpstreamStub | null } = { current: null };
const upstream = (): UpstreamStub => {
  const current = stub.current;
  if (current === null) throw new Error('upstream stub not started');
  return current;
};

beforeAll(() => {
  stub.current = startUpstreamStub();
});

afterAll(() => {
  stub.current?.stop();
});

describe('S5 (a) 8MiB JSON cap aborts without buffering', () => {
  test(
    'five 9MiB reads each stop just past the cap and RSS stays flat',
    async () => {
      const url = `${upstream().url}/json-9mib`;
      const window = () =>
        Effect.runPromise(
          Effect.forEach(
            Array.from({ length: 5 }, (_, index) => index),
            () => Effect.flip(readJsonCapped(url)),
            { concurrency: 1 },
          ),
        );

      // Warm-up reps: pay for JIT and Bun's socket/allocator arenas up front, so
      // the measured windows show per-request retention rather than heap growth.
      await window();

      // Two identical 5-rep windows. If anything accumulated, the second window
      // would cost the same as the first; it does not.
      const before = rssMiB();
      const failures = await window();
      const mid = rssMiB();
      await window();
      const after = rssMiB();
      const delta = mid - before;
      const secondDelta = after - mid;

      expect(failures.map((failure) => failure._tag)).toEqual([
        'UpstreamTooLarge',
        'UpstreamTooLarge',
        'UpstreamTooLarge',
        'UpstreamTooLarge',
        'UpstreamTooLarge',
      ]);

      const bytesRead = failures.flatMap((failure) =>
        failure._tag === 'UpstreamTooLarge' ? [failure.bytesRead] : [],
      );
      const overshoot = bytesRead.map((bytes) => bytes - JSON_CAP_BYTES);

      report('json cap bytesRead', bytesRead.join(', '));
      report(
        'json overshoot past 8MiB (bytes)',
        `${overshoot.join(', ')} (max one chunk = ${CHUNK_BYTES})`,
      );
      report(
        'RSS across 5 reps',
        `${round(before)}MiB -> ${round(mid)}MiB (delta ${round(delta)}MiB)`,
      );
      report(
        'RSS across 5 more reps',
        `${round(mid)}MiB -> ${round(after)}MiB (delta ${round(secondDelta)}MiB) ` +
          `— buffering all 5 would have cost ~42MiB`,
      );

      // Crossed the cap...
      expect(bytesRead.every((bytes) => bytes > JSON_CAP_BYTES)).toBe(true);
      // ...and stopped there, rather than draining all ~9MiB.
      expect(bytesRead.every((bytes) => bytes < 9 * MiB)).toBe(true);
      expect(Math.max(...overshoot)).toBeLessThanOrEqual(MiB);
      // No accumulation across repetitions: 5 buffered 8.4MiB bodies would be
      // ~42MiB of live heap, and a second window would cost the same again.
      expect(delta).toBeLessThan(32);
      expect(secondDelta).toBeLessThan(16);
    },
    30_000,
  );
});

describe('S5 (b) 50MiB binary streams through incrementally', () => {
  test(
    'the consumer sees its first chunk before the upstream finishes sending',
    async () => {
      const url = `${upstream().url}/binary-50mib`;
      const sink = { checksum: 0, maxChunk: 0 };

      const before = rssMiB();
      const startedAt = performance.now();
      const outcome = await Effect.runPromise(
        fetchUpstream(url).pipe(
          Effect.flatMap((response) =>
            upstreamBody(url, response).pipe(
              Effect.tap(() =>
                Effect.sync(() => {
                  expect(response.status).toBe(200);
                  expect(response.headers.get('content-type')).toBe('video/mp4');
                }),
              ),
            ),
          ),
          Effect.flatMap((body) =>
            drainBounded(body, {
              capBytes: Number.POSITIVE_INFINITY,
              onChunk: (chunk) => {
                // A real consumer (disk, S3, downstream socket). Never retains.
                sink.checksum = (sink.checksum + (chunk[0] ?? 0)) % 65_536;
                sink.maxChunk = Math.max(sink.maxChunk, chunk.byteLength);
              },
            }),
          ),
        ),
      );
      const after = rssMiB();
      const serverFinishedAt = upstream().binaryFinishedAt();

      report('binary bytes', `${outcome.bytesRead} (expected ${BINARY_TOTAL_BYTES})`);
      report('binary chunks', `${outcome.chunkCount}, largest ${sink.maxChunk} bytes`);
      report(
        'interleave',
        `consumer first chunk at +${round(outcome.firstChunkAt - startedAt)}ms, ` +
          `upstream finished sending at +${round(serverFinishedAt - startedAt)}ms`,
      );
      report(
        'RSS across the 50MiB transfer',
        `${round(before)}MiB -> ${round(after)}MiB (delta ${round(after - before)}MiB)`,
      );

      expect(outcome.bytesRead).toBe(BINARY_TOTAL_BYTES);
      expect(outcome.truncated).toBe(false);
      // Interleaving proof: the consumer was already being fed while the
      // upstream still had chunks to enqueue.
      expect(Number.isNaN(serverFinishedAt)).toBe(false);
      expect(outcome.firstChunkAt).toBeLessThan(serverFinishedAt);
      // Nothing ever held the whole body.
      expect(sink.maxChunk).toBeLessThan(BINARY_TOTAL_BYTES / 10);
      expect(after - before).toBeLessThan(32);
    },
    60_000,
  );
});

describe('S5 (c) consumer disconnect mid-stream', () => {
  test(
    'aborting an endless stream leaves the process healthy',
    async () => {
      const url = `${upstream().url}/endless`;
      const controller = new AbortController();
      const seen = { chunks: 0 };

      const failure = await Effect.runPromise(
        Effect.flip(
          fetchUpstream(url, { signal: controller.signal }).pipe(
            Effect.flatMap((response) => upstreamBody(url, response)),
            Effect.flatMap((body) =>
              drainBounded(body, {
                capBytes: Number.POSITIVE_INFINITY,
                onChunk: () => {
                  seen.chunks += 1;
                  if (seen.chunks === 3) controller.abort();
                },
              }),
            ),
          ),
        ),
      );

      report('disconnect', `${failure._tag} after ${seen.chunks} chunks`);
      expect(failure._tag).toBe('UpstreamBodyError');
      expect(seen.chunks).toBe(3);

      // The upstream noticed the disconnect and stopped producing.
      await Bun.sleep(250);
      report('server endless cancels', String(upstream().endlessCancels()));
      expect(upstream().endlessCancels()).toBeGreaterThanOrEqual(1);

      // ...and the process is still serving.
      const pong = await Effect.runPromise(
        fetchUpstream(`${upstream().url}/ping`).pipe(
          Effect.flatMap((response) =>
            Effect.tryPromise({
              try: () => response.text(),
              catch: (cause) => new UpstreamBodyError({ cause }),
            }),
          ),
        ),
      );
      expect(pong).toBe('pong');

      const afterFailure = await Effect.runPromise(
        Effect.flip(readJsonCapped(`${upstream().url}/json-9mib`)),
      );
      expect(afterFailure._tag).toBe('UpstreamTooLarge');
    },
    30_000,
  );
});

describe('S5 (extra) error bodies drained to 64KiB', () => {
  test('a 2MiB 500 body keeps at most 64KiB', async () => {
    const url = `${upstream().url}/error-2mib`;
    const collector = cappedCollector(ERROR_DRAIN_CAP_BYTES);

    const outcome = await Effect.runPromise(
      fetchUpstream(url).pipe(
        Effect.flatMap((response) =>
          upstreamBody(url, response).pipe(
            Effect.tap(() => Effect.sync(() => expect(response.status).toBe(500))),
          ),
        ),
        Effect.flatMap((body) =>
          drainBounded(body, {
            capBytes: ERROR_DRAIN_CAP_BYTES,
            onChunk: collector.onChunk,
          }),
        ),
      ),
    );

    report(
      'error drain',
      `read ${outcome.bytesRead} bytes off the socket, retained ${collector.retainedBytes()}`,
    );

    expect(outcome.truncated).toBe(true);
    expect(collector.retainedBytes()).toBe(ERROR_DRAIN_CAP_BYTES);
    expect(collector.text().length).toBe(ERROR_DRAIN_CAP_BYTES);
    expect(outcome.bytesRead).toBeLessThan(2 * MiB);
  }, 30_000);
});
