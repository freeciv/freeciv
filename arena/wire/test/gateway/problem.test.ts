/**
 * Decode parity for the gateway's problem shape.
 *
 * Three claims are under test, in order of how expensive they are to get
 * wrong:
 *
 * 1. **The captured 4xx bodies decode and survive a round trip.**  Every
 *    `{"error": ...}` fixture in the corpus is a real response from the live
 *    stack; it decodes, re-encodes to the same bytes, and classifies to the
 *    status the route actually returned.
 * 2. **The catalogue is verbatim.**  The Python source is re-read and its
 *    `GatewayProblem` messages extracted, in both directions: a message the
 *    gateway gained, lost, or reworded fails here.
 * 3. **The shape does not over-claim.**  A success payload carrying a
 *    top-level `error` string decodes too — that is the trap — so the test
 *    pins exactly how much a successful decode is worth.
 */
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { describe, expect, test } from 'bun:test';
import { Either, Option } from 'effect';
import {
  ARCHIVE_JSON_LABELS,
  archiveJsonNotFound,
  archiveJsonUnavailable,
  classifyGatewayProblemMessage,
  decodeGatewayProblem,
  decodeGatewayProblemFromString,
  encodeGatewayProblem,
  GATEWAY_METHOD_NOT_ALLOWED_ALLOW,
  GATEWAY_PROBLEM_CACHE_CONTROL,
  GATEWAY_PROBLEM_CONTENT_TYPE,
  GATEWAY_PROBLEM_MESSAGES,
  type GatewayProblemMessage,
  GATEWAY_PROBLEM_STATUS,
  gatewayProblemBody,
  gatewayProblemBytes,
  isBareGatewayProblem,
  isGatewayProblem,
  isKnownGatewayProblemMessage,
  parseUpstreamHttpStatus,
  upstreamProblem,
  upstreamReturnedHttp,
} from 'src/gateway/problem';

const FIXTURES = join(import.meta.dir, '..', 'fixtures');

const fixtureText = (path: string): string => readFileSync(join(FIXTURES, path), 'utf8');

const rightOrThrow = <A, E>(either: Either.Either<A, E>): A =>
  Either.getOrThrowWith(either, (error) => new Error(`expected Right, got ${String(error)}`));

// ---------------------------------------------------------------------------
// The captured problem bodies
// ---------------------------------------------------------------------------

interface ProblemFixture {
  readonly path: string;
  readonly message: GatewayProblemMessage;
  readonly status: number;
}

/**
 * Every `{"error": ...}` capture in the corpus, with the status it arrived as.
 * `bun:test`'s `each` takes a mutable table, hence the plain array.
 */
const PROBLEM_FIXTURES: ProblemFixture[] = [
  { path: 'live/gateway-replay-404.json', message: 'not found', status: 404 },
  { path: 'live/supervisor-status-404.json', message: 'game not found', status: 404 },
  {
    path: 'live/gateway-events-400.json',
    message: 'game events do not accept query parameters',
    status: 400,
  },
  {
    path: 'live/gateway-board-400.json',
    message: 'board query requires exactly one turn',
    status: 400,
  },
];

describe('captured gateway problem bodies', () => {
  test.each(PROBLEM_FIXTURES)('$path decodes', ({ path, message }) => {
    const decoded = rightOrThrow(decodeGatewayProblem(JSON.parse(fixtureText(path))));
    expect(decoded.error).toBe(message);
  });

  test.each(PROBLEM_FIXTURES)('$path round-trips byte-identically', ({ path }) => {
    const text = fixtureText(path);
    const decoded = rightOrThrow(decodeGatewayProblem(JSON.parse(text)));
    const encoded = rightOrThrow(encodeGatewayProblem(decoded));
    expect(JSON.stringify(encoded)).toBe(text);
  });

  test.each(PROBLEM_FIXTURES)('$path decodes straight from response text', ({ path, message }) => {
    expect(rightOrThrow(decodeGatewayProblemFromString(fixtureText(path))).error).toBe(message);
  });

  test.each(PROBLEM_FIXTURES)(
    '$path is a bare problem body, and classifies to its own status',
    ({ path, message, status }) => {
      const payload: unknown = JSON.parse(fixtureText(path));
      expect(isBareGatewayProblem(payload)).toBe(true);
      expect(rightOrThrow(decodeGatewayProblem(payload)).error).toBe(message);
      expect(classifyGatewayProblemMessage(message)).toEqual({ _tag: 'Known', message, status });
    },
  );

  test.each(PROBLEM_FIXTURES)(
    '$path is exactly what the canonical writer produces',
    ({ path, message }) => {
      const text = fixtureText(path);
      expect(rightOrThrow(gatewayProblemBody(message))).toBe(text);
      expect(rightOrThrow(gatewayProblemBytes(message)).byteLength).toBe(
        Buffer.byteLength(text, 'utf8'),
      );
    },
  );

  test('the supervisor answers this route with the gateway shape, not a v2 envelope', () => {
    // Both services 404 a missing game with the same one-key body; the wire
    // difference is only the message.  The v2 structured error (`error` as an
    // object) never appears on these routes.
    const supervisor: unknown = JSON.parse(fixtureText('live/supervisor-status-404.json'));
    const gateway: unknown = JSON.parse(fixtureText('live/gateway-replay-404.json'));
    expect(isBareGatewayProblem(supervisor)).toBe(true);
    expect(isBareGatewayProblem(gateway)).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// The shape's limits
// ---------------------------------------------------------------------------

describe('GatewayProblem shape', () => {
  test('unknown keys survive decode and re-encode, in their original order', () => {
    const text = '{"error":"not found","code":"gone","detail":{"upstream":502}}';
    const decoded = rightOrThrow(decodeGatewayProblem(JSON.parse(text)));
    expect(decoded.error).toBe('not found');
    expect(Object.keys(decoded)).toEqual(['error', 'code', 'detail']);
    expect(JSON.stringify(rightOrThrow(encodeGatewayProblem(decoded)))).toBe(text);
  });

  test('a missing or non-string error is not a problem body', () => {
    expect(Either.isRight(decodeGatewayProblem({}))).toBe(false);
    expect(Either.isRight(decodeGatewayProblem({ error: null }))).toBe(false);
    expect(Either.isRight(decodeGatewayProblem({ error: 404 }))).toBe(false);
    expect(Either.isRight(decodeGatewayProblem([{ error: 'not found' }]))).toBe(false);
    expect(Either.isRight(decodeGatewayProblem('not found'))).toBe(false);
    expect(Either.isRight(decodeGatewayProblem(null))).toBe(false);
  });

  test('an empty message still decodes: the gateway never promises non-empty', () => {
    expect(rightOrThrow(decodeGatewayProblem({ error: '' })).error).toBe('');
  });

  test('the full-control-v2 structured error is a different envelope and is rejected', () => {
    // agent_eval/full_control_v2.py:522-541, carried as APIProblem.payload by
    // the supervisor (supervisor.py:11411).  `error` is an object there.
    const structured = {
      schema_version: 1,
      control_protocol: 'full-control-v2',
      error: {
        code: 'sidecar_unavailable',
        message: 'the full-control-v2 native sidecar is unavailable',
        retryable: false,
        details: {},
      },
      state_revision: null,
    };
    expect(Either.isRight(decodeGatewayProblem(structured))).toBe(false);
    expect(isGatewayProblem(structured)).toBe(false);
    expect(isBareGatewayProblem(structured)).toBe(false);
  });

  test('response text that is not JSON fails at the parse step, as a value', () => {
    const failure = decodeGatewayProblemFromString(fixtureText('invalid/not-json.txt'));
    expect(Either.isLeft(failure)).toBe(true);
    expect(Either.isLeft(decodeGatewayProblemFromString('<html>502 Bad Gateway</html>'))).toBe(
      true,
    );
  });
});

/**
 * The trap: `error` is a *success*-payload field too.  A manifest, a report and
 * a live status doc all carry one, so "it decoded as a GatewayProblem" says
 * nothing on its own.
 */
describe('success payloads that also carry a top-level error string', () => {
  const CARRIES_ERROR_STRING: string[] = [
    'runs/manifest/cancelled-strategic-v1-never-started.json',
    'runs/manifest/cancelled-v2-benchmark-null.json',
    'runs/manifest/failed-v2-sidecar-exited.json',
    'live/supervisor-status-terminal.json',
  ];

  test.each(CARRIES_ERROR_STRING)('%s decodes as a problem body but is not bare', (path) => {
    const payload: unknown = JSON.parse(fixtureText(path));
    expect(Either.isRight(decodeGatewayProblem(payload))).toBe(true);
    expect(isBareGatewayProblem(payload)).toBe(false);
  });

  test.each(['runs/manifest/running-v2-multiplayer.json', 'live/supervisor-status-running.json'])(
    '%s has error: null — tri-state, and not a problem body at all',
    (path) => {
      const payload: unknown = JSON.parse(fixtureText(path));
      expect(Either.isRight(decodeGatewayProblem(payload))).toBe(false);
      expect(isBareGatewayProblem(payload)).toBe(false);
    },
  );

  test.each(['live/gateway-watch-running.json', 'runs/report/empty-score-no-recovery.json'])(
    '%s carries its error nested, so the top level is not a problem body',
    (path) => {
      const payload: unknown = JSON.parse(fixtureText(path));
      expect(Either.isRight(decodeGatewayProblem(payload))).toBe(false);
      expect(isBareGatewayProblem(payload)).toBe(false);
    },
  );

  test('a success payload never round-trips through the problem schema intact', () => {
    // It decodes, and the unknown-field preservation keeps it whole — which is
    // exactly why the status, not the schema, decides what a response means.
    const text = fixtureText('runs/manifest/cancelled-strategic-v1-never-started.json');
    const decoded = rightOrThrow(decodeGatewayProblem(JSON.parse(text)));
    const encoded = rightOrThrow(encodeGatewayProblem(decoded));
    expect(JSON.stringify(encoded)).toBe(JSON.stringify(JSON.parse(text)));
  });
});

// ---------------------------------------------------------------------------
// The catalogue
// ---------------------------------------------------------------------------

describe('the message catalogue', () => {
  const messages = Object.values(GATEWAY_PROBLEM_MESSAGES);

  test('every message is distinct', () => {
    expect(new Set<string>(messages).size).toBe(messages.length);
  });

  test('every message has a status, and every status has a message', () => {
    expect(new Set<string>(Object.keys(GATEWAY_PROBLEM_STATUS))).toEqual(new Set<string>(messages));
    const statuses = new Set(Object.values(GATEWAY_PROBLEM_STATUS));
    expect([...statuses].toSorted((a, b) => a - b)).toEqual([400, 404, 405, 500, 502, 503]);
  });

  test('messages are ASCII and unpadded, so the canonical body is their bytes', () => {
    const suspicious = messages.filter(
      (message) => message.trim() !== message || /[^\x20-\x7e]/.test(message),
    );
    expect(suspicious).toEqual([]);
  });

  test('every message is recognized, and nothing else is', () => {
    expect(messages.filter((message) => !isKnownGatewayProblemMessage(message))).toEqual([]);
    expect(isKnownGatewayProblemMessage('Not Found')).toBe(false);
    expect(isKnownGatewayProblemMessage('not found ')).toBe(false);
    expect(isKnownGatewayProblemMessage('upstream returned HTTP 409')).toBe(false);
  });

  test('the pagination bounds are spelled exactly as the gateway spells them', () => {
    expect(GATEWAY_PROBLEM_MESSAGES.replayQueryOutOfRange).toBe(
      'after_turn must be >= 0 and limit must be in [1, 250]',
    );
  });

  test('the archive-label templates expand to catalogue entries', () => {
    expect(ARCHIVE_JSON_LABELS).toEqual(['game manifest', 'game report']);
    expect(archiveJsonNotFound('game manifest')).toBe('game manifest not found');
    expect(archiveJsonNotFound('game report')).toBe('game report not found');
    expect(archiveJsonUnavailable('game manifest')).toBe('game manifest is unavailable');
    expect(archiveJsonUnavailable('game report')).toBe('game report is unavailable');
    expect(GATEWAY_PROBLEM_STATUS[archiveJsonNotFound('game report')]).toBe(404);
    expect(GATEWAY_PROBLEM_STATUS[archiveJsonUnavailable('game report')]).toBe(503);
  });

  test('transport constants match the headers the gateway sets', () => {
    expect(GATEWAY_PROBLEM_CONTENT_TYPE).toBe('application/json; charset=utf-8');
    expect(GATEWAY_PROBLEM_CACHE_CONTROL).toBe('no-store');
    expect(GATEWAY_METHOD_NOT_ALLOWED_ALLOW).toBe('GET');
  });
});

describe('the upstream-status template', () => {
  test('is formatted the way an int formats in an f-string', () => {
    expect(upstreamReturnedHttp(409)).toBe('upstream returned HTTP 409');
    expect(upstreamReturnedHttp(500)).toBe('upstream returned HTTP 500');
  });

  test('round-trips through the parser', () => {
    expect(parseUpstreamHttpStatus(upstreamReturnedHttp(409))).toEqual(Option.some(409));
    expect(parseUpstreamHttpStatus('upstream returned HTTP 0')).toEqual(Option.some(0));
    expect(parseUpstreamHttpStatus('not found')).toEqual(Option.none());
    expect(parseUpstreamHttpStatus('upstream returned HTTP 4xx')).toEqual(Option.none());
    expect(parseUpstreamHttpStatus(' upstream returned HTTP 409')).toEqual(Option.none());
  });

  test('classification tells the three cases apart', () => {
    expect(classifyGatewayProblemMessage('method not allowed')).toEqual({
      _tag: 'Known',
      message: 'method not allowed',
      status: 405,
    });
    expect(classifyGatewayProblemMessage('upstream returned HTTP 409')).toEqual({
      _tag: 'UpstreamStatus',
      status: 409,
    });
    expect(classifyGatewayProblemMessage('teapot')).toEqual({
      _tag: 'Unrecognized',
      message: 'teapot',
    });
  });

  test('a non-2xx upstream is relayed with its own status; a 3xx becomes a 502', () => {
    expect(upstreamProblem(409)).toEqual({ status: 409, message: 'upstream returned HTTP 409' });
    expect(upstreamProblem(500)).toEqual({ status: 500, message: 'upstream returned HTTP 500' });
    expect(upstreamProblem(301)).toEqual({
      status: 502,
      message: 'upstream redirects are not allowed',
    });
    expect(upstreamProblem(399)).toEqual({
      status: 502,
      message: 'upstream redirects are not allowed',
    });
    expect(upstreamProblem(400)).toEqual({ status: 400, message: 'upstream returned HTTP 400' });
  });
});

// ---------------------------------------------------------------------------
// Parity with the Python that owns these strings
// ---------------------------------------------------------------------------

const AGENT_EVAL = `${import.meta.dir}/../../../../agent_eval`;

/**
 * Read a Python source file the parity assertions below are written against.
 *
 * **Ungated on purpose.**  A missing `agent_eval/` used to make these tests
 * *skip*, so a checkout without the Python side reported the whole parity story
 * green while checking nothing.  This throws at module load instead, which is
 * the standard `test/canon.test.ts` and `test/fnv1a64.test.ts` already set for
 * the python3 oracle: a missing authority fails, it does not disappear.
 */
const readPythonSource = (path: string): Promise<string> => Bun.file(path).text();

const source = await readPythonSource(`${AGENT_EVAL}/replay_gateway.py`);

/** `HTTPStatus` members the gateway names, and what they are on the wire. */
const HTTP_STATUS: Readonly<Record<string, number>> = {
  BAD_REQUEST: 400,
  NOT_FOUND: 404,
  METHOD_NOT_ALLOWED: 405,
  INTERNAL_SERVER_ERROR: 500,
  BAD_GATEWAY: 502,
  SERVICE_UNAVAILABLE: 503,
};

/**
 * `raise GatewayProblem(HTTPStatus.X, "msg")`, its `UpstreamUnavailable`
 * subclass (`:85`, which carries the same status/message pair), and the
 * `self._problem` twin used by the catch-all.
 */
const RAISE_RE =
  /(?:raise GatewayProblem|raise UpstreamUnavailable|self\._problem)\(\s*HTTPStatus\.([A-Z_]+),\s*(f?)"([^"]*)"/g;

interface PythonProblem {
  readonly status: number;
  readonly message: string;
}

const expand = (template: string): ReadonlyArray<string> =>
  template.includes('{label}')
    ? ARCHIVE_JSON_LABELS.map((label) => template.replace('{label}', label))
    : [template];

const extracted: ReadonlyArray<PythonProblem> = [...source.matchAll(RAISE_RE)].flatMap((match) => {
  const status = HTTP_STATUS[match[1] ?? ''];
  return status === undefined
    ? []
    : expand(match[3] ?? '').map((message) => ({ status, message }));
});

const fStringTemplates: ReadonlyArray<string> = [...source.matchAll(RAISE_RE)]
  .filter((match) => match[2] === 'f')
  .map((match) => match[3] ?? '');

describe('parity with agent_eval/replay_gateway.py', () => {
  test('the Python source is present — ungated, so a missing authority fails instead of skipping', () => {
    expect(source.length).toBeGreaterThan(0);
  });

  test('the extraction found the raises it is supposed to find', () => {
    expect(extracted.length).toBeGreaterThan(40);
  });

  test('the only templated messages are the two archive labels', () => {
    expect(new Set(fStringTemplates)).toEqual(
      new Set(['{label} not found', '{label} is unavailable']),
    );
  });

  test('every message Python raises is in the catalogue, with the same status', () => {
    const wrong = extracted.filter(
      (problem) =>
        !isKnownGatewayProblemMessage(problem.message) ||
        GATEWAY_PROBLEM_STATUS[problem.message] !== problem.status,
    );
    expect(wrong).toEqual([]);
  });

  test('the catalogue invents nothing Python does not raise', () => {
    const raised = new Set(extracted.map((problem) => problem.message));
    // Two messages are built outside a `raise`: the 405 body is assembled by
    // hand (:2047) and the redirect message rides a conditional expression
    // (:1456 and peers).  Both are asserted verbatim below.
    raised.add(GATEWAY_PROBLEM_MESSAGES.methodNotAllowed);
    raised.add(GATEWAY_PROBLEM_MESSAGES.upstreamRedirect);
    const invented = Object.values(GATEWAY_PROBLEM_MESSAGES).filter(
      (message) => !raised.has(message),
    );
    expect(invented).toEqual([]);
  });

  test('UpstreamUnavailable is still a GatewayProblem, so its bodies are this shape', () => {
    expect(source).toContain('class UpstreamUnavailable(GatewayProblem):');
  });

  test('the 405 body is still built by hand, with Allow: GET', () => {
    expect(source).toContain('_canonical({"error": "method not allowed"})');
    expect(source).toContain('HTTPStatus.METHOD_NOT_ALLOWED,');
    expect(source).toContain('headers={"Allow": "GET", "Cache-Control": "no-store"}');
    expect(source).toContain('do_HEAD = _method_not_allowed');
  });

  test('the redirect/relay branch is still spelled the way this module models it', () => {
    expect(source).toContain('downstream = HTTPStatus.BAD_GATEWAY if 300 <= status < 400 else status');
    expect(source).toContain('"upstream redirects are not allowed"');
    expect(source).toContain('f"upstream returned HTTP {status}"');
  });

  test('the problem body is still one key, canonically serialized, no-store', () => {
    expect(source).toContain('self._json(status, {"error": message})');
    expect(source).toContain('"Cache-Control": "no-store"');
    expect(source).toContain('"application/json; charset=utf-8"');
  });

  test('the two archive labels are still the only ones', () => {
    const labels = [...source.matchAll(/_read_archive_json\([^,]+,\s*"([^"]+)"\)/g)].map(
      (match) => match[1],
    );
    expect(new Set(labels)).toEqual(new Set<string | undefined>(ARCHIVE_JSON_LABELS));
  });
});
