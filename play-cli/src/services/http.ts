/**
 * The HTTP floor, shared by strategic-v1 (U01, U02) and full-control-v2.
 *
 * Ports `service_url` (client.py:197-222), `request_json` (225-243),
 * `request_json_response` (246-306) and `_RejectRedirects` (185-190).
 *
 * Two properties are load-bearing:
 *
 * - **No redirects, ever.** A bearer credential must stay on the exact
 *   configured supervisor origin, so a 3xx is a failure and not a hop.
 * - **Every status returns its body.** `requestJsonResponse` does not raise on
 *   4xx/5xx, because a valid rejected receipt or structured error must not be
 *   destroyed by the HTTP status.  `requestJson` is the v1 wrapper that does
 *   raise, with the Python's exact message shape.
 */
import { Context, Effect, Layer } from 'effect';
import { PlayerError, playerError } from 'src/errors';
import { DEFAULT_SERVICE_URL } from 'src/constants';
import { compactJson, pyJsonDumps } from 'src/services/json-output';
import { isJsonObject, type JsonObject } from 'src/schema/primitives';

// ---------------------------------------------------------------------------
// service_url
// ---------------------------------------------------------------------------

const SERVICE_URL_ERROR =
  'AGENT_EVAL_SERVICE_URL must be an http(s) URL without credentials, query, or fragment';

/**
 * Normalize the one origin every request is allowed to reach.
 *
 * `URL` rejects a malformed port outright where CPython's `urlsplit` defers it
 * to `.port`; both end at the same refusal.
 */
export const serviceUrl = (
  value?: string | undefined,
  environment: Readonly<Record<string, string | undefined>> = process.env
): Effect.Effect<string, PlayerError> =>
  Effect.suspend(() => {
    const raw = (value || environment['AGENT_EVAL_SERVICE_URL'] || DEFAULT_SERVICE_URL).trim();
    const parsed = ((): URL | null => {
      try {
        return new URL(raw);
      } catch {
        return null;
      }
    })();
    if (parsed === null) return Effect.fail(playerError(SERVICE_URL_ERROR));
    const scheme = parsed.protocol.replace(/:$/, '').toLowerCase();
    // `URL` drops a default port (`https://h:443` → host `h`); CPython's
    // `urlsplit().netloc` keeps every byte the caller typed, and the session
    // file records this string, so the raw authority is what gets normalized.
    const separator = raw.indexOf('://');
    const rawAuthority =
      separator < 0
        ? parsed.host
        : (raw.slice(separator + 3).split(/[/?#]/, 1)[0] ?? parsed.host);
    const portText = /:(\d*)$/.exec(rawAuthority.replace(/^\[[^\]]*\]/, ''))?.[1];
    const port = portText === undefined || portText === '' ? null : Number.parseInt(portText, 10);
    if (
      (scheme !== 'http' && scheme !== 'https') ||
      parsed.hostname === '' ||
      parsed.username !== '' ||
      parsed.password !== '' ||
      parsed.search !== '' ||
      parsed.hash !== '' ||
      (port !== null && (!Number.isInteger(port) || port < 1 || port > 65535))
    ) {
      return Effect.fail(playerError(SERVICE_URL_ERROR));
    }
    const netloc = rawAuthority.toLowerCase();
    const trimmedPath = parsed.pathname.replace(/\/+$/, '');
    return Effect.succeed(`${scheme}://${netloc}${trimmedPath}`);
  });

// ---------------------------------------------------------------------------
// The service
// ---------------------------------------------------------------------------

export interface JsonResponse {
  readonly status: number;
  readonly value: JsonObject;
  readonly headers: Readonly<Record<string, string>>;
}

export interface RequestOptions {
  readonly token?: string | undefined;
  readonly body?: unknown;
  /** Pre-serialized bytes, for the retry path that must resend exact bytes. */
  readonly encodedBody?: string | undefined;
  /** Seconds. CPython's default is 60. */
  readonly timeout?: number;
}

export interface HttpApi {
  readonly requestJson: (
    method: string,
    url: string,
    options?: RequestOptions
  ) => Effect.Effect<JsonObject, PlayerError>;
  readonly requestJsonResponse: (
    method: string,
    url: string,
    options?: RequestOptions
  ) => Effect.Effect<JsonResponse, PlayerError>;
}

export class Http extends Context.Tag('Http')<Http, HttpApi>() {}

const DEFAULT_TIMEOUT_S = 60;

const originOf = (url: string): string => {
  try {
    const parsed = new URL(url);
    return `${parsed.protocol}//${parsed.host}`;
  } catch {
    return url;
  }
};

const unreachable = (url: string, reason: string): PlayerError =>
  playerError(
    `cannot reach the Freeciv supervisor at ${originOf(url)}: ${reason}. ` +
      'Stop and tell the user; do not retry in a loop.'
  );

/** The message `request_json` raises for a non-2xx v1 response. */
export const v1ErrorMessage = (status: number, value: JsonObject): string => {
  const error = value['error'];
  if (isJsonObject(error)) {
    const message = error['message'] ?? error['code'];
    return `HTTP ${status}: ${message === null || message === undefined ? 'None' : String(message)}`;
  }
  if (error === undefined || error === null) {
    return `HTTP ${status}: HTTP ${status}`;
  }
  return `HTTP ${status}: ${typeof error === 'string' ? error : compactJson(error)}`;
};

/** The bytes a request body serializes to. CPython: sorted keys, no spaces. */
export const encodeRequestBody = (body: unknown): string =>
  pyJsonDumps(body, { sortKeys: true, separators: [',', ':'] });

const makeApi = (fetchImpl: typeof fetch): HttpApi => {
  const requestJsonResponse = (
    method: string,
    url: string,
    options: RequestOptions = {}
  ): Effect.Effect<JsonResponse, PlayerError> =>
    Effect.gen(function* () {
      if (options.body !== undefined && options.encodedBody !== undefined) {
        return yield* Effect.fail(playerError('a request cannot contain two JSON bodies'));
      }
      const data =
        options.encodedBody !== undefined
          ? options.encodedBody
          : options.body !== undefined
            ? encodeRequestBody(options.body)
            : null;
      const headers: Record<string, string> = { Accept: 'application/json' };
      if (data !== null) headers['Content-Type'] = 'application/json';
      if (options.token !== undefined && options.token !== '') {
        headers['Authorization'] = `Bearer ${options.token}`;
      }
      const timeoutMillis = (options.timeout ?? DEFAULT_TIMEOUT_S) * 1000;

      const response = yield* Effect.tryPromise({
        try: (signal) =>
          fetchImpl(url, {
            method,
            headers,
            ...(data === null ? {} : { body: data }),
            // Keep bearer credentials on the exact configured supervisor
            // origin: a redirect is a refusal, not a hop.
            redirect: 'error',
            signal,
          }),
        catch: (cause) => unreachable(url, cause instanceof Error ? cause.message : String(cause)),
      }).pipe(
        Effect.timeoutFail({
          duration: `${timeoutMillis} millis`,
          onTimeout: () => unreachable(url, `timed out after ${timeoutMillis / 1000}s`),
        })
      );

      const text = yield* Effect.tryPromise({
        try: () => response.text(),
        catch: (cause) =>
          playerError(
            `invalid supervisor response: ${cause instanceof Error ? cause.message : String(cause)}`
          ),
      });
      const parsed = yield* Effect.try({
        try: () => JSON.parse(text) as unknown,
        catch: (cause) =>
          playerError(
            `invalid supervisor response: ${cause instanceof Error ? cause.message : String(cause)}`
          ),
      });
      if (!isJsonObject(parsed)) {
        return yield* Effect.fail(
          playerError('the supervisor returned a non-object JSON response')
        );
      }
      const headerRecord: Record<string, string> = {};
      response.headers.forEach((headerValue, name) => {
        headerRecord[name.toLowerCase()] = headerValue;
      });
      return { status: response.status, value: parsed, headers: headerRecord };
    });

  return {
    requestJsonResponse,
    requestJson: (method, url, options) =>
      Effect.flatMap(requestJsonResponse(method, url, options), (response) =>
        response.status >= 200 && response.status < 300
          ? Effect.succeed(response.value)
          : Effect.fail(playerError(v1ErrorMessage(response.status, response.value)))
      ),
  };
};

export const HttpLive: Layer.Layer<Http> = Layer.succeed(Http, makeApi(fetch));

/** Build the API over an injected `fetch`, for the fake-server test harness. */
export const httpLayer = (fetchImpl: typeof fetch): Layer.Layer<Http> =>
  Layer.succeed(Http, makeApi(fetchImpl));

export const httpFor = (fetchImpl: typeof fetch): HttpApi => makeApi(fetchImpl);
