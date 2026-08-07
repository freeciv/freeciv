/**
 * The full-control-v2 request surface.
 *
 * Two properties carry safety weight: a busy 429 is retried on reads and *never*
 * on writes, and a non-2xx is turned into a `V2ResponseError` whose payload has
 * already been validated — because `cli-main` prints that payload to the agent.
 */
import { describe, expect, test } from 'bun:test';
import { Effect, Either } from 'effect';
import { httpFor } from 'src/services/http';
import { isBusyRefusal, v2ClientFor, v2Url, type V2Credentials } from 'src/services/v2-client';
import { FIXTURE_GAME_ID, errorPayload, healthPayload, recordingFetch } from 'test/_fixtures';

const credentials: V2Credentials = {
  gameId: FIXTURE_GAME_ID,
  agentToken: 'secret-token',
  serviceUrl: 'http://127.0.0.1:8765',
};

const runPromise = <A, E>(effect: Effect.Effect<A, E>): Promise<Either.Either<A, E>> =>
  Effect.runPromise(Effect.either(effect));

const busyBody = {
  error: {
    code: 'rate_limited',
    message: 'the native sidecar is busy',
    details: {},
  },
};

describe('v2Url', () => {
  test('builds the only route shape v2 has', () => {
    expect(Effect.runSync(v2Url(credentials, '/health'))).toBe(
      `http://127.0.0.1:8765/v2/games/${FIXTURE_GAME_ID}/me/health`
    );
  });

  test.each([['no leading slash', 'health'], ['a query', '/health?x=1'], ['a fragment', '/h#f']])(
    'refuses %s',
    (_label, suffix) => {
      expect(Either.isLeft(Effect.runSync(Effect.either(v2Url(credentials, suffix))))).toBe(true);
    }
  );
});

describe('busy retry', () => {
  test('a busy 429 on a GET is retried up to V2_BUSY_RETRIES', async () => {
    const recorder = recordingFetch([
      { status: 429, body: busyBody },
      { status: 429, body: busyBody },
      { body: healthPayload() },
    ]);
    const client = v2ClientFor(httpFor(recorder.fetch), () => Effect.void);
    const either = await runPromise(client.get(credentials, '/health'));
    expect(Either.isRight(either)).toBe(true);
    expect(recorder.requests).toHaveLength(3);
  });

  test('a mutation is never retried — its contract is receipt-first', async () => {
    const recorder = recordingFetch([
      { status: 429, body: busyBody },
      { body: healthPayload() },
    ]);
    const client = v2ClientFor(httpFor(recorder.fetch), () => Effect.void);
    const either = await runPromise(client.post(credentials, '/batches', {}));
    expect(Either.isLeft(either)).toBe(true);
    expect(recorder.requests).toHaveLength(1);
  });

  test('a 429 carrying retry_after_seconds is a real rate limit, not busy', () => {
    expect(
      isBusyRefusal({
        status: 429,
        headers: {},
        value: {
          error: {
            code: 'rate_limited',
            message: 'busy',
            details: { retry_after_seconds: 5 },
          },
        },
      })
    ).toBe(false);
  });
});

describe('refusals', () => {
  test('a non-2xx becomes a V2ResponseError with a validated payload', async () => {
    const recorder = recordingFetch([{ status: 409, body: errorPayload() }]);
    const client = v2ClientFor(httpFor(recorder.fetch), () => Effect.void);
    const either = await runPromise(client.get(credentials, '/state'));
    expect(Either.isLeft(either)).toBe(true);
    if (Either.isLeft(either)) {
      const failure = either.left;
      expect(failure._tag).toBe('V2ResponseError');
      if (failure._tag === 'V2ResponseError') {
        expect(failure.status).toBe(409);
        expect(failure.message).toBe(
          'HTTP 409: that unit cannot found a city here (illegal_action)'
        );
      }
    }
  });

  test('a refusal body that is not a v2 error is drift, not a refusal', async () => {
    const recorder = recordingFetch([{ status: 500, body: { oops: true } }]);
    const client = v2ClientFor(httpFor(recorder.fetch), () => Effect.void);
    const either = await runPromise(client.get(credentials, '/state'));
    expect(Either.isLeft(either)).toBe(true);
    if (Either.isLeft(either)) expect(either.left._tag).toBe('DriftError');
  });

  test('query parameters are appended after the route is validated', async () => {
    const recorder = recordingFetch([{ body: healthPayload() }]);
    const client = v2ClientFor(httpFor(recorder.fetch), () => Effect.void);
    await runPromise(client.get(credentials, '/state', { section: 'units', limit: '16' }));
    expect(recorder.requests[0]?.url).toBe(
      `http://127.0.0.1:8765/v2/games/${FIXTURE_GAME_ID}/me/state?section=units&limit=16`
    );
  });
});
