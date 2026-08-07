/**
 * `play health` — the four-second answer to "where am I and can I act".
 *
 * Ports `command_health` (client.py:6544-6569).
 *
 * The command itself is thin on purpose: one GET, one validation, one render.
 * Everything that decides what the text says lives in `render/health.ts` and
 * `render/phase.ts`, because `turn`, `do --end --await`, `wait` and `monitor`
 * print the same lines and none of them goes through here.
 */
import { Command, Options } from '@effect/cli';
import { Effect } from 'effect';
import type { DriftError, PlayerError, SessionMissingError, V2ResponseError } from 'src/errors';
import { BLOCKED_NEXT_LINE, renderHealth } from 'src/render/health';
import { V2_PROTOCOL_CARD } from 'src/render/join';
import { holderSeat } from 'src/render/phase';
import { render } from 'src/render/primitives';
import { decodeHealth, type HealthEnvelope } from 'src/schema/index';
import { printHealthJson } from 'src/services/health-json';
import { jsonRequested } from 'src/services/json-output';
import { mirrorHealth } from 'src/services/mirror';
import type { PrivateFs } from 'src/services/private-fs';
import { SessionStore, credentialsOf, type Session } from 'src/services/session-store';
import { V2Client } from 'src/services/v2-client';

/**
 * Ten seconds.
 *
 * `/health` is the probe every other command's remedy sentence tells the agent
 * to run when something is wrong; a probe that can hang for the default request
 * timeout is not a probe.
 */
export const HEALTH_TIMEOUT_S = 10;

/** GET `/health` and validate it against this session's identity. */
export const fetchHealth = (
  session: Session
): Effect.Effect<HealthEnvelope, V2ResponseError | DriftError | PlayerError, V2Client> =>
  Effect.gen(function* () {
    const client = yield* V2Client;
    const credentials = credentialsOf(session);
    const url = yield* client.url(credentials, '/health');
    const response = yield* client.response('GET', url, credentials, {
      timeout: HEALTH_TIMEOUT_S,
    });
    if (response.status < 200 || response.status >= 300) {
      return yield* client.raiseValidated(response);
    }
    return yield* decodeHealth(response.value, session);
  });

/**
 * The whole printed block: the render, plus the remedy a blocked seat needs.
 *
 * Split out from the command so `--json`'s sibling path is testable without a
 * session on disk, and so the "only when somebody else holds it" condition is
 * asserted against the same `holderSeat` the headline used.
 */
export const healthLines = (health: HealthEnvelope): ReadonlyArray<string> => {
  const lines = renderHealth(health);
  return holderSeat(health.phase) === null ? lines : [...lines, BLOCKED_NEXT_LINE];
};

export interface HealthOptions {
  readonly session: string;
  readonly json: boolean;
}

export const runHealth = (
  options: HealthOptions
): Effect.Effect<
  void,
  V2ResponseError | DriftError | PlayerError | SessionMissingError,
  PrivateFs | SessionStore | V2Client
> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const { path, session } = yield* store.resolveV2(options.session);
    const health = yield* fetchHealth(session);
    // `_mirror_health(path, value, "health")` (client.py:6552), between the
    // validation and the render.  It writes `state/header.txt` and
    // `state/phase.json` — the two files `show`'s header and `wait`'s
    // cached-phase note read — and prints nothing on either path, which is why
    // it runs before the `--json` fork rather than inside the text branch.
    // `_mirror_health` hardcodes `commands=V2_PROTOCOL_CARD` (3062-3072).
    yield* mirrorHealth(path, health, 'health', { commands: V2_PROTOCOL_CARD });
    if (jsonRequested('health', options.json)) {
      // Not core's `printV2Json`: the envelope's timings are Python floats and
      // `compactJson` would print `600.0` as `600`.  See `health-json.ts`.
      yield* printHealthJson(health);
    } else {
      yield* render(healthLines(health));
    }
  });

const sessionOption = Options.text('session').pipe(
  Options.withDefault(''),
  Options.withDescription('the private session file this command acts against')
);

const jsonOption = Options.boolean('json').pipe(
  Options.withDescription('print the full-fidelity JSON payload instead of text')
);

export const healthCommand = Command.make(
  'health',
  { session: sessionOption, json: jsonOption },
  runHealth
);
