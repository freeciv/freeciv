/**
 * The fake supervisor every unit's tests talk to.
 *
 * It is a `fetch` implementation, not an HTTP listener: the port's only network
 * surface is `services/http.ts`, so injecting `fetch` gives byte-level control
 * over statuses, headers and bodies without binding a port — which also means a
 * test suite never races another agent's live server.
 */
export interface RecordedRequest {
  readonly method: string;
  readonly url: string;
  readonly headers: Readonly<Record<string, string>>;
  readonly body: string | null;
}

export interface FakeRoute {
  readonly status?: number;
  readonly body: unknown;
  readonly headers?: Readonly<Record<string, string>>;
  /** Seconds of simulated latency, for the concurrency-determinism tests. */
  readonly delayS?: number;
}

/** The first parameter `fetch` accepts, without depending on lib.dom. */
type FetchInput = Parameters<typeof fetch>[0];

export const jsonResponse = (body: unknown, status = 200): Response =>
  new Response(JSON.stringify(body), {
    status,
    headers: { 'content-type': 'application/json' },
  });

const urlOf = (input: FetchInput): string =>
  typeof input === 'string' ? input : input instanceof URL ? input.href : input.url;

const requestBody = async (init: RequestInit | undefined): Promise<string | null> => {
  const body = init?.body;
  if (body === undefined || body === null) return null;
  return typeof body === 'string' ? body : new Response(body).text();
};

const headerRecord = (init: RequestInit | undefined): Readonly<Record<string, string>> => {
  const out: Record<string, string> = {};
  const headers = init?.headers;
  if (headers === undefined) return out;
  new Headers(headers).forEach((value, name) => {
    out[name.toLowerCase()] = value;
  });
  return out;
};

/**
 * Serve a queue of responses, or one response per matching URL substring.
 *
 * A queue drains in order and is how a busy-retry test proves the second GET
 * happened; a map is how a multi-route command test stays readable.
 */
export const fakeFetch = (
  plan: ReadonlyArray<FakeRoute> | ReadonlyMap<string, FakeRoute>
): typeof fetch => {
  const queue = Array.isArray(plan) ? [...(plan as ReadonlyArray<FakeRoute>)] : null;
  const routes = queue === null ? (plan as ReadonlyMap<string, FakeRoute>) : null;
  return (async (input: FetchInput, init?: RequestInit): Promise<Response> => {
    const url = urlOf(input);
    const route =
      queue !== null
        ? queue.shift()
        : [...(routes ?? new Map<string, FakeRoute>())].find(([match]) => url.includes(match))?.[1];
    if (route === undefined) {
      return jsonResponse({ error: { code: 'not_implemented', message: `no route for ${url}` } }, 404);
    }
    const delayS = route.delayS ?? 0;
    if (delayS > 0) {
      await new Promise((resolve) => setTimeout(resolve, delayS * 1000));
    }
    return new Response(JSON.stringify(route.body), {
      status: route.status ?? 200,
      headers: { 'content-type': 'application/json', ...(route.headers ?? {}) },
    });
  }) as typeof fetch;
};

/** The same, plus a log of everything that was sent. */
export const recordingFetch = (
  plan: ReadonlyArray<FakeRoute> | ReadonlyMap<string, FakeRoute>
): { readonly fetch: typeof fetch; readonly requests: ReadonlyArray<RecordedRequest> } => {
  const requests: RecordedRequest[] = [];
  const inner = fakeFetch(plan);
  const wrapped = (async (input: FetchInput, init?: RequestInit): Promise<Response> => {
    const url = urlOf(input);
    requests.push({
      method: init?.method ?? 'GET',
      url,
      headers: headerRecord(init),
      body: await requestBody(init),
    });
    return inner(input, init);
  }) as typeof fetch;
  return { fetch: wrapped, requests };
};
