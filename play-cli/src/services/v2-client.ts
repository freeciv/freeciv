/**
 * The full-control-v2 request surface.
 *
 * Ports `_v2_url` (client.py:2358-2364), `_v2_response` (2374-2402) and
 * `_raise_validated_v2_error` (2405-2406).
 *
 * A refusal is data, not an exception: `get`/`post` fail with
 * `V2ResponseError` carrying the *validated* error payload, which is exactly
 * what `cli-main` renders on stdout before exiting 2.
 */
import { Context, Effect, Layer } from 'effect';
import { DriftError, PlayerError, V2ResponseError, playerError } from 'src/errors';
import { V2_BUSY_BACKOFF_S, V2_BUSY_RETRIES } from 'src/constants';
import { Http, type JsonResponse, type RequestOptions } from 'src/services/http';
import { decodeError, v2ErrorMessage } from 'src/schema/error';
import { isJsonObject, type JsonObject } from 'src/schema/primitives';

/** The identity and credential a v2 request is made with. */
export interface V2Credentials {
  readonly gameId: string;
  readonly agentToken: string;
  readonly serviceUrl: string;
}

/** `{service}/v2/games/{game}/me{suffix}` — the only route shape v2 has. */
export const v2Url = (
  credentials: V2Credentials,
  suffix: string
): Effect.Effect<string, PlayerError> =>
  suffix.startsWith('/') && !suffix.includes('?') && !suffix.includes('#')
    ? Effect.succeed(`${credentials.serviceUrl}/v2/games/${credentials.gameId}/me${suffix}`)
    : Effect.fail(playerError('invalid local v2 route'));

/**
 * Report whether a 429 is the "native sidecar is busy" refusal, which clears in
 * well under a second and is worth retrying inside the same command.
 */
export const isBusyRefusal = (response: JsonResponse): boolean => {
  if (response.status !== 429) return false;
  const error = response.value['error'];
  if (!isJsonObject(error)) return false;
  const details = error['details'];
  const hasRetryAfter = isJsonObject(details) && 'retry_after_seconds' in details;
  return (
    error['code'] === 'rate_limited' &&
    typeof error['message'] === 'string' &&
    error['message'].includes('busy') &&
    !hasRetryAfter
  );
};

export interface V2RequestOptions {
  readonly body?: unknown;
  readonly encodedBody?: string | undefined;
  readonly timeout?: number;
}

export interface V2ClientApi {
  readonly url: (credentials: V2Credentials, suffix: string) => Effect.Effect<string, PlayerError>;
  /** The raw response, every status included — mutations need the refusal body. */
  readonly response: (
    method: string,
    url: string,
    credentials: V2Credentials,
    options?: V2RequestOptions
  ) => Effect.Effect<JsonResponse, PlayerError>;
  /** Turn a non-2xx response into a `V2ResponseError` with a validated payload. */
  readonly raiseValidated: (
    response: JsonResponse
  ) => Effect.Effect<never, V2ResponseError | DriftError>;
  readonly get: (
    credentials: V2Credentials,
    suffix: string,
    query?: Readonly<Record<string, string>>
  ) => Effect.Effect<JsonObject, V2ResponseError | DriftError | PlayerError>;
  readonly post: (
    credentials: V2Credentials,
    suffix: string,
    body: unknown
  ) => Effect.Effect<JsonObject, V2ResponseError | DriftError | PlayerError>;
}

export class V2Client extends Context.Tag('V2Client')<V2Client, V2ClientApi>() {}

const queryString = (query: Readonly<Record<string, string>>): string => {
  const parameters = new URLSearchParams();
  for (const [key, value] of Object.entries(query)) parameters.append(key, value);
  const encoded = parameters.toString();
  return encoded === '' ? '' : `?${encoded}`;
};

const makeApi = (
  http: Context.Tag.Service<Http>,
  sleep: (seconds: number) => Effect.Effect<void>
): V2ClientApi => {
  const response = (
    method: string,
    url: string,
    credentials: V2Credentials,
    options: V2RequestOptions = {}
  ): Effect.Effect<JsonResponse, PlayerError> =>
    Effect.gen(function* () {
      // Reads are retried; mutations never are — their contract is
      // receipt-first, and a resend could apply an order twice.
      const attempts = method === 'GET' ? V2_BUSY_RETRIES : 1;
      const request: RequestOptions = {
        token: credentials.agentToken,
        ...(options.body === undefined ? {} : { body: options.body }),
        ...(options.encodedBody === undefined ? {} : { encodedBody: options.encodedBody }),
        ...(options.timeout === undefined ? {} : { timeout: options.timeout }),
      };
      let last = yield* http.requestJsonResponse(method, url, request);
      for (let attempt = 1; attempt < attempts; attempt += 1) {
        if (!isBusyRefusal(last)) return last;
        yield* sleep(V2_BUSY_BACKOFF_S * attempt);
        last = yield* http.requestJsonResponse(method, url, request);
      }
      return last;
    });

  const raiseValidated = (
    raw: JsonResponse
  ): Effect.Effect<never, V2ResponseError | DriftError> =>
    Effect.flatMap(decodeError(raw.value), (payload) =>
      Effect.fail(
        new V2ResponseError({
          message: v2ErrorMessage(raw.status, payload),
          status: raw.status,
          payload,
        })
      )
    );

  const send = (
    method: string,
    credentials: V2Credentials,
    suffix: string,
    options: V2RequestOptions
  ): Effect.Effect<JsonObject, V2ResponseError | DriftError | PlayerError> =>
    Effect.gen(function* () {
      const url = yield* v2Url(credentials, suffix);
      const raw = yield* response(method, url, credentials, options);
      if (raw.status < 200 || raw.status >= 300) {
        return yield* raiseValidated(raw);
      }
      return raw.value;
    });

  return {
    url: v2Url,
    response,
    raiseValidated,
    get: (credentials, suffix, query) =>
      Effect.gen(function* () {
        // The shape check runs on the caller's suffix, before the query is
        // joined on: `?` is illegal in a route and mandatory in a query.
        const base = yield* v2Url(credentials, suffix);
        const raw = yield* response(
          'GET',
          query === undefined ? base : base + queryString(query),
          credentials
        );
        if (raw.status < 200 || raw.status >= 300) {
          return yield* raiseValidated(raw);
        }
        return raw.value;
      }),
    post: (credentials, suffix, body) => send('POST', credentials, suffix, { body }),
  };
};

export const V2ClientLive: Layer.Layer<V2Client, never, Http> = Layer.effect(
  V2Client,
  Effect.map(Http, (http) =>
    makeApi(http, (seconds) => Effect.sleep(`${Math.round(seconds * 1000)} millis`))
  )
);

/** Build the API directly, with an injectable sleep, for tests. */
export const v2ClientFor = (
  http: Context.Tag.Service<Http>,
  sleep: (seconds: number) => Effect.Effect<void> = (seconds) =>
    Effect.sleep(`${Math.round(seconds * 1000)} millis`)
): V2ClientApi => makeApi(http, sleep);
