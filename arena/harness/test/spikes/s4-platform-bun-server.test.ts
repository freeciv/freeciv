/**
 * SPIKE S4 — @effect/platform-bun HTTP server semantics for the gateway port.
 *
 * The gateway we are porting from Python has three load-bearing wire promises:
 *
 *   1. it binds an ephemeral port and publishes the real port in a ready-file
 *      *before* anyone is allowed to send traffic,
 *   2. every method is answered by OUR code — non-GET gets `405` plus
 *      `Allow: GET`, and no framework may auto-answer `OPTIONS` on our behalf,
 *   3. a `GET` carrying a body is a client bug and must be rejected `400`.
 *
 * This spike pins what @effect/platform-bun actually does, on the wire, with a
 * raw TCP client — `fetch` refuses to send a GET body or a bare HEAD, so it
 * cannot be the instrument here.
 *
 * Findings that shape the real implementation are asserted, not just narrated:
 * see `the port is bound and answering 404 before serve attaches the app`.
 */
import { describe, expect, test } from 'bun:test';
import { HttpServerRequest, HttpServerResponse } from '@effect/platform';
import { BunHttpServer } from '@effect/platform-bun';
import { Data, Effect } from 'effect';
import type { Scope } from 'effect';

const HOST = '127.0.0.1';
const EXCHANGE_TIMEOUT = '5 seconds';

// ---------------------------------------------------------------------------
// The app under test: a faithful stand-in for the gateway's method contract.
// ---------------------------------------------------------------------------

/** Does the request line claim a body, per RFC 9112 message framing? */
const framingOf = (
  headers: Readonly<Record<string, string | undefined>>,
): 'content-length' | 'chunked' | 'none' =>
  headers['transfer-encoding'] !== undefined
    ? 'chunked'
    : headers['content-length'] !== undefined &&
        headers['content-length'] !== '0'
      ? 'content-length'
      : 'none';

const gatewayApp = Effect.gen(function* () {
  const request = yield* HttpServerRequest.HttpServerRequest;
  const framing = framingOf(request.headers);

  return request.method !== 'GET' && request.method !== 'HEAD'
    ? // Non-GET: 405 from OUR handler, with OUR Allow header.
      HttpServerResponse.text(`method=${request.method}`, {
        status: 405,
        headers: { allow: 'GET' },
      })
    : request.method === 'HEAD'
      ? HttpServerResponse.text('method=HEAD', {
          status: 405,
          headers: { allow: 'GET' },
        })
      : framing !== 'none'
        ? HttpServerResponse.text(`get-with-body:${framing}`, { status: 400 })
        : HttpServerResponse.text('ok');
});

// ---------------------------------------------------------------------------
// Raw HTTP/1.1 client. Promise-based on purpose: a Promise settles once, which
// makes the socket's close/error race harmless without a mutable guard flag.
// ---------------------------------------------------------------------------

class WireError extends Data.TaggedError('WireError')<{
  readonly cause: unknown;
}> {}

const exchangeBytes = (port: number, payload: string): Promise<string> =>
  new Promise<string>((resolve, reject) => {
    const chunks: Array<Uint8Array> = [];
    Bun.connect({
      hostname: HOST,
      port,
      socket: {
        open: (socket) => {
          socket.write(payload);
        },
        data: (_socket, received) => {
          chunks.push(new Uint8Array(received));
        },
        close: () => {
          resolve(Buffer.concat(chunks).toString('utf8'));
        },
        error: (_socket, cause) => {
          reject(cause);
        },
      },
    }).then(undefined, reject);
  });

interface WireResponse {
  readonly status: number;
  readonly headers: ReadonlyMap<string, string>;
  readonly body: string;
  readonly raw: string;
}

const parseWire = (raw: string): WireResponse => {
  const separator = raw.indexOf('\r\n\r\n');
  const head = separator === -1 ? raw : raw.slice(0, separator);
  const body = separator === -1 ? '' : raw.slice(separator + 4);
  const [statusLine = '', ...headerLines] = head.split('\r\n');
  const [, statusText = '0'] = statusLine.split(' ');
  return {
    status: Number.parseInt(statusText, 10),
    headers: new Map(
      headerLines
        .filter((line) => line.includes(':'))
        .map((line): readonly [string, string] => [
          line.slice(0, line.indexOf(':')).trim().toLowerCase(),
          line.slice(line.indexOf(':') + 1).trim(),
        ]),
    ),
    body,
    raw,
  };
};

/** Send a hand-rolled request so method and body framing are fully ours. */
const request = (
  port: number,
  options: {
    readonly method: string;
    readonly path?: string;
    readonly headers?: ReadonlyArray<readonly [string, string]>;
    readonly body?: string;
  },
): Effect.Effect<WireResponse> => {
  const lines = [
    `${options.method} ${options.path ?? '/state'} HTTP/1.1`,
    `Host: ${HOST}:${port}`,
    'Connection: close',
    ...(options.headers ?? []).map(([name, value]) => `${name}: ${value}`),
  ];
  const payload = `${lines.join('\r\n')}\r\n\r\n${options.body ?? ''}`;
  return Effect.tryPromise({
    try: () => exchangeBytes(port, payload),
    catch: (cause) => new WireError({ cause }),
  }).pipe(Effect.timeout(EXCHANGE_TIMEOUT), Effect.orDie, Effect.map(parseWire));
};

/** 'open' if something still accepts on `port`, 'refused' otherwise. */
const probeConnect = (port: number): Effect.Effect<'open' | 'refused'> =>
  Effect.tryPromise(() =>
    Bun.connect({
      hostname: HOST,
      port,
      socket: {
        open: (socket) => {
          socket.end();
        },
        data: () => {},
        close: () => {},
        error: () => {},
      },
    }),
  ).pipe(
    Effect.timeout(EXCHANGE_TIMEOUT),
    Effect.match({
      onSuccess: () => 'open' as const,
      onFailure: () => 'refused' as const,
    }),
  );

// ---------------------------------------------------------------------------
// Server lifecycle: bind :0, learn the port, attach the app.
// ---------------------------------------------------------------------------

interface Bound {
  readonly portBeforeServe: number;
  readonly portAfterServe: number;
}

/** Bind on port 0 and attach `gatewayApp`; the port is read from the server. */
const boundGateway: Effect.Effect<Bound, never, Scope.Scope> = Effect.gen(
  function* () {
    const server = yield* BunHttpServer.make({ port: 0, hostname: HOST });
    const before = server.address;
    // (a) the ephemeral port is known here — no request has been made yet.
    const portBeforeServe = before._tag === 'TcpAddress' ? before.port : -1;
    yield* server.serve(gatewayApp);
    const after = server.address;
    return {
      portBeforeServe,
      portAfterServe: after._tag === 'TcpAddress' ? after.port : -1,
    };
  },
);

const runScoped = <A, E>(
  effect: Effect.Effect<A, E, Scope.Scope>,
): Promise<A> => Effect.runPromise(Effect.scoped(effect));

// ---------------------------------------------------------------------------

describe('S4 (a) the real ephemeral port is knowable before serving traffic', () => {
  test('BunHttpServer.make on port 0 exposes a TcpAddress with the real port', async () => {
    const bound = await runScoped(boundGateway);
    expect(bound.portBeforeServe).toBeGreaterThan(0);
    expect(bound.portBeforeServe).not.toBe(0);
    expect(bound.portAfterServe).toBe(bound.portBeforeServe);
  });

  test('the port is bound and answering 404 before serve attaches the app', async () => {
    // THE GOTCHA: BunHttpServer.make calls Bun.serve immediately, with a
    // built-in `404 not found` handler. The socket is live and reachable the
    // instant the address is readable — so the ready-file must be written
    // AFTER server.serve(app) returns, never merely after the address is read,
    // or a fast client can race in and be told 404 by code that is not ours.
    const program = Effect.gen(function* () {
      const server = yield* BunHttpServer.make({ port: 0, hostname: HOST });
      const address = server.address;
      const port = address._tag === 'TcpAddress' ? address.port : -1;
      const beforeServe = yield* request(port, { method: 'GET' });
      yield* server.serve(gatewayApp);
      const afterServe = yield* request(port, { method: 'GET' });
      return { beforeServe, afterServe };
    });
    const { beforeServe, afterServe } = await runScoped(program);

    expect(beforeServe.status).toBe(404);
    expect(beforeServe.body).toBe('not found');
    expect(afterServe.status).toBe(200);
    expect(afterServe.body).toBe('ok');
  });
});

describe('S4 (b) every method reaches our handler', () => {
  const nonGet: Array<string> = ['POST', 'OPTIONS', 'PUT', 'DELETE', 'PATCH'];

  test.each(nonGet)(
    '%s is answered 405 + Allow: GET by our code, on the wire',
    async (method: string) => {
      const response = await runScoped(
        Effect.flatMap(boundGateway, (bound) =>
          request(bound.portAfterServe, { method }),
        ),
      );
      expect(response.status).toBe(405);
      // The Allow header survives to the wire bytes, not just the object.
      expect(response.headers.get('allow')).toBe('GET');
      expect(response.raw).toContain('allow: GET');
      // The echoed method proves OUR handler ran; nothing auto-answered.
      expect(response.body).toBe(`method=${method}`);
    },
  );

  test('OPTIONS is NOT intercepted by Bun (no CORS preflight auto-answer)', async () => {
    const response = await runScoped(
      Effect.flatMap(boundGateway, (bound) =>
        request(bound.portAfterServe, {
          method: 'OPTIONS',
          headers: [
            ['Origin', 'http://example.test'],
            ['Access-Control-Request-Method', 'GET'],
          ],
        }),
      ),
    );
    // A framework preflight would be 204/200 with access-control-allow-*.
    expect(response.status).toBe(405);
    expect(response.body).toBe('method=OPTIONS');
    expect(response.headers.has('access-control-allow-origin')).toBe(false);
  });

  test('HEAD reaches the handler; Bun strips the body but keeps our headers', async () => {
    const response = await runScoped(
      Effect.flatMap(boundGateway, (bound) =>
        request(bound.portAfterServe, { method: 'HEAD' }),
      ),
    );
    expect(response.status).toBe(405);
    expect(response.headers.get('allow')).toBe('GET');
    // Body stripped (correct per RFC 9110), while content-length still
    // advertises what the GET-shaped body would have been: 'method=HEAD'.
    expect(response.body).toBe('');
    expect(response.headers.get('content-length')).toBe(
      String(Buffer.byteLength('method=HEAD')),
    );
  });

  test('GET is ours too', async () => {
    const response = await runScoped(
      Effect.flatMap(boundGateway, (bound) =>
        request(bound.portAfterServe, { method: 'GET' }),
      ),
    );
    expect(response.status).toBe(200);
    expect(response.body).toBe('ok');
  });
});

describe('S4 (c) a GET that carries a body is detectable', () => {
  test('the framing headers reach the handler, so we can 400 it', async () => {
    const [contentLength, chunked] = await runScoped(
      Effect.flatMap(boundGateway, (bound) =>
        Effect.all([
          request(bound.portAfterServe, {
            method: 'GET',
            headers: [
              ['Content-Type', 'application/json'],
              ['Content-Length', '13'],
            ],
            body: '{"hello":"x"}',
          }),
          request(bound.portAfterServe, {
            method: 'GET',
            headers: [['Transfer-Encoding', 'chunked']],
            body: 'd\r\n{"hello":"x"}\r\n0\r\n\r\n',
          }),
        ]),
      ),
    );
    expect(contentLength.status).toBe(400);
    expect(contentLength.body).toBe('get-with-body:content-length');
    expect(chunked.status).toBe(400);
    expect(chunked.body).toBe('get-with-body:chunked');
  });

  test('but the BYTES are gone: Bun nulls the body of a GET request', async () => {
    // Definitive answer to (c): detection yes, payload no. Bun's HTTP parser
    // discards a GET entity-body, so `request.text` yields '' and the
    // underlying Request has `body === null`. The Python gateway's 400 is
    // reproducible from headers alone; a design that needs to READ a GET body
    // is not implementable on this server.
    const observed = await runScoped(
      Effect.gen(function* () {
        const server = yield* BunHttpServer.make({ port: 0, hostname: HOST });
        const address = server.address;
        const port = address._tag === 'TcpAddress' ? address.port : -1;
        yield* server.serve(
          Effect.gen(function* () {
            const req = yield* HttpServerRequest.HttpServerRequest;
            const text = yield* req.text.pipe(
              Effect.orElseSucceed(() => '<unreadable>'),
            );
            const source = req.source;
            const bodyNull =
              source instanceof Request ? String(source.body === null) : 'n/a';
            return HttpServerResponse.text(`text=<${text}> bodyNull=${bodyNull}`);
          }),
        );
        return yield* Effect.all([
          request(port, {
            method: 'GET',
            headers: [['Content-Length', '13']],
            body: '{"hello":"x"}',
          }),
          request(port, {
            method: 'POST',
            headers: [['Content-Length', '13']],
            body: '{"hello":"y"}',
          }),
        ]);
      }),
    );
    const [get, post] = observed;
    expect(get?.body).toBe('text=<> bodyNull=true');
    // Contrast: an identical POST body IS readable, so this is a GET-specific
    // parser behaviour and not a broken test client.
    expect(post?.body).toBe('text=<{"hello":"y"}> bodyNull=false');
  });
});

describe('S4 (d) closing the scope releases the port', () => {
  test('the socket is refused after the scope closes, and rebinding works', async () => {
    const first = await runScoped(
      Effect.flatMap(boundGateway, (bound) =>
        Effect.map(request(bound.portAfterServe, { method: 'GET' }), (res) => ({
          port: bound.portAfterServe,
          status: res.status,
        })),
      ),
    );
    expect(first.status).toBe(200);

    // Scope closed by runScoped: nothing should still be accepting there.
    expect(await Effect.runPromise(probeConnect(first.port))).toBe('refused');

    // And the identical flow binds again, cleanly, on a fresh port.
    const second = await runScoped(
      Effect.flatMap(boundGateway, (bound) =>
        Effect.map(request(bound.portAfterServe, { method: 'GET' }), (res) => ({
          port: bound.portAfterServe,
          status: res.status,
          body: res.body,
        })),
      ),
    );
    expect(second.status).toBe(200);
    expect(second.body).toBe('ok');
    expect(second.port).toBeGreaterThan(0);
    expect(await Effect.runPromise(probeConnect(second.port))).toBe('refused');
  });
});
