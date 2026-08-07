/**
 * Refusal rendering — what the agent reads when the supervisor says no.
 *
 * Ports `_error_text` (client.py:5802-5804), `_ERROR_REMEDIES` (5807-5813),
 * `_restart_command` (5825-5838), `_retry_after_text` (5841-5850) and
 * `_render_error_payload` (5853-5910).
 *
 * This is the top of the recovery funnel: `cli-main` renders every
 * `V2ResponseError` through {@link renderErrorPayload} (refusal body on stdout,
 * `error: …` on stderr, exit 2), so a remedy line that is wrong here is a
 * remedy line that is wrong everywhere.  Two rules the Python encodes and this
 * port keeps:
 *
 * - **A remedy is never guessed at.** A restart query carrying an option this
 *   client cannot spell prints no `next: just …` line at all, because a command
 *   that does not run as printed is worse than no command.
 * - **A standing offer that never pays teaches the agent to burn a call.**
 *   `full payload: … --json` is offered only when some part of `details` is a
 *   nested value this compact form actually had to elide — never after a rate
 *   limit, a restart query or a transport failure, all of which print whole.
 */
import { Layer } from 'effect';
import { V2_RESTART_COMMANDS, V2_RESTART_OPTIONS } from 'src/constants';
import { RefusalRender, type RefusalRenderApi } from 'src/render/refusal-seam';
import { flat, scalar } from 'src/render/primitives';
import { isJsonObject, type JsonObject, type JsonValue } from 'src/schema/primitives';
import type { StructuredError } from 'src/schema/error';
import { compactJson } from 'src/services/json-output';

// ---------------------------------------------------------------------------
// _error_text (client.py:5802-5804)
// ---------------------------------------------------------------------------

/** `"{code}: {message}"` — the one line a receipt or disposition inlines. */
export const errorText = (error: StructuredError): string =>
  `${error.error.code}: ${error.error.message}`;

// ---------------------------------------------------------------------------
// _ERROR_REMEDIES (client.py:5807-5813)
// ---------------------------------------------------------------------------

/**
 * What to do next, per `details.safe_next`.
 *
 * `receipt_first` is the safety-critical one: it says resolve the outcome
 * *before* any replay, which is the only correct move when the server will not
 * say whether the batch was accepted.
 */
export const ERROR_REMEDIES: ReadonlyMap<string, string> = new Map([
  ['refresh', 're-read the actor with `just legal --actor_id ID --all`, then re-issue the order'],
  ['retry_exact', 'retry the same batch with `just retry --batch_id ID`'],
  ['receipt_first', 'resolve the outcome with `just receipt --batch_id ID` before any replay'],
]);

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

/**
 * `_scalar` over a value that has not been through the schema layer.
 *
 * `renderErrorPayload` takes `unknown` because `cli-main` hands it whatever a
 * refusal carried, so the JSON domain has to be re-established here rather than
 * assumed.  Everything outside it falls back to the compact JSON encoding,
 * which is what CPython's `json.dumps` tail did.
 */
const scalarText = (value: unknown): string => {
  if (value === null || value === undefined) return '-';
  if (typeof value === 'boolean') return value ? 'yes' : 'no';
  if (typeof value === 'number' || typeof value === 'string') return scalar(value);
  return compactJson(value);
};

/** Python's `sorted(details.items())`, by key. */
const sortedEntries = (value: JsonObject): ReadonlyArray<readonly [string, JsonValue]> =>
  Object.entries(value).sort(([left], [right]) => (left < right ? -1 : left > right ? 1 : 0));

// ---------------------------------------------------------------------------
// _restart_command (client.py:5825-5838)
// ---------------------------------------------------------------------------

/**
 * Spell the page a retired cursor says to ask for again.
 *
 * A query key outside {@link V2_RESTART_OPTIONS} means the server described a
 * page this client cannot spell; the remedy is withheld rather than guessed at,
 * and the raw `restart=` detail still prints so nothing is silently dropped.
 */
export const restartCommand = (value: unknown): string => {
  if (!isJsonObject(value)) return '';
  const query = value['query'];
  const endpoint = value['endpoint'];
  const base = typeof endpoint === 'string' ? V2_RESTART_COMMANDS.get(endpoint) : undefined;
  if (base === undefined || !isJsonObject(query) || Object.keys(query).length === 0) return '';
  const spellable = Object.keys(query).every((key) =>
    (V2_RESTART_OPTIONS as ReadonlyArray<string>).includes(key)
  );
  if (!spellable) return '';
  return V2_RESTART_OPTIONS.reduce((command, key) => {
    const argument = query[key];
    return argument === undefined || argument === null
      ? command
      : `${command} --${key} ${scalar(argument)}`;
  }, base);
};

// ---------------------------------------------------------------------------
// _retry_after_text (client.py:5841-5850)
// ---------------------------------------------------------------------------

/** Say when a rate-limited request may be sent again, in seconds. */
export const retryAfterText = (details: JsonObject): string => {
  const seconds = details['retry_after_seconds'];
  if (typeof seconds !== 'number' || !Number.isFinite(seconds)) return '';
  const text = `retry the same command in ${scalar(seconds)}s`;
  const at = details['retry_after'];
  return typeof at === 'string' && at !== '' ? `${text} (not before ${at})` : text;
};

// ---------------------------------------------------------------------------
// _render_error_payload (client.py:5853-5910)
// ---------------------------------------------------------------------------

/** `rev{revision}/t{turn}`, read the way CPython's f-string read it. */
const revisionSuffix = (value: JsonValue | undefined): string => {
  if (!isJsonObject(value) || !('revision' in value)) return '';
  return `  rev${scalarText(value['revision'])}/t${scalarText(value['turn'])}`;
};

/**
 * Render a server refusal compactly, remedy first.
 *
 * The payload is the one this client already validated, so the fields are read
 * positionally rather than probed; anything unexpected falls back to the raw
 * payload so nothing is ever silently dropped.
 */
export const renderErrorPayload = (payload: unknown): ReadonlyArray<string> => {
  if (!isJsonObject(payload) || !isJsonObject(payload['error'])) {
    return [scalarText(payload)];
  }
  const body = payload['error'];
  const headline =
    `error ${scalarText(body['code'])}: ${scalarText(body['message'])}` +
    revisionSuffix(payload['state_revision']);
  const lines: string[] = [headline];

  const details = body['details'];
  // Every detail key this renderer spells out in full, so the `--json` offer
  // below can tell "there is more" from "you have already seen it".
  const expressed = new Set(['safe_next', 'batch_id']);
  if (isJsonObject(details)) {
    const safeNext = details['safe_next'];
    const remedy = typeof safeNext === 'string' ? ERROR_REMEDIES.get(safeNext) : undefined;
    if (remedy !== undefined) {
      const batchId = details['batch_id'];
      const spelled =
        typeof batchId === 'string' && batchId !== ''
          ? remedy.replace('--batch_id ID', `--batch_id ${batchId}`)
          : remedy;
      lines.push(`next (${scalarText(safeNext)}): ${spelled}`);
    }
    // A rate limit has exactly one remedy and it is a clock, not a flag.
    const retry = retryAfterText(details);
    if (retry !== '') {
      lines.push(`next: ${retry}`);
      expressed.add('retry_after_seconds');
      expressed.add('retry_after');
    }
    // A retired cursor already names the page to ask for again.
    const restart = restartCommand(details['restart']);
    if (restart !== '') {
      lines.push(`next: ${restart}`);
      expressed.add('restart');
    }
    const rest = sortedEntries(details)
      .filter(([key, value]) => !expressed.has(key) && value !== null)
      .map(([key, value]) => `${key}=${flat(value)}`);
    if (rest.length > 0) lines.push(`  ${rest.join(' ')}`);
  }
  if (body['retryable'] === true) {
    lines.push('retryable: the same request may be sent again');
  }
  // A standing remedy that never pays teaches the agent to burn a call after
  // every server error.  `--json` adds exactly the untruncated `details`
  // object, so it is offered only when some part of that object is a nested
  // value this compact form had to elide.
  const elided =
    isJsonObject(details) &&
    Object.entries(details).some(
      ([key, value]) => !expressed.has(key) && (isJsonObject(value) || Array.isArray(value))
    );
  if (elided) lines.push('full payload: re-run the same command with --json');
  return lines;
};

// ---------------------------------------------------------------------------
// The seam cli-main asks for (PORT_MAP §4.6)
// ---------------------------------------------------------------------------

export const refusalRender: RefusalRenderApi = { renderErrorPayload };

/** The integrator swaps `RefusalRenderDefault` in `cli-main`'s `AppLayer` for this. */
export const RefusalRenderLive: Layer.Layer<RefusalRender> = Layer.succeed(
  RefusalRender,
  refusalRender
);
