/**
 * The structured v2 error envelope.
 *
 * Ports `_validate_error` (client.py:1332-1360) and the v2 response header
 * check `_validate_v2_header` (client.py:1318-1329) that every other envelope
 * decoder shares.
 */
import { Effect } from 'effect';
import { DriftError, invalid } from 'src/errors';
import { FULL_CONTROL_V2, V2_ERROR_CODES } from 'src/constants';
import {
  exact,
  field,
  hasField,
  isJsonObject,
  jsonObject,
  type JsonObject,
  type SessionIdentity,
} from 'src/schema/primitives';
import { decodeRevision, type Revision } from 'src/schema/revision';

export interface StructuredErrorBody {
  readonly code: string;
  readonly message: string;
  readonly retryable: boolean;
  readonly details: JsonObject;
}

export interface StructuredError {
  readonly schema_version: 2;
  readonly control_protocol: typeof FULL_CONTROL_V2;
  readonly error: StructuredErrorBody;
  readonly state_revision: Revision | null;
}

const ENVELOPE_FIELDS: ReadonlySet<string> = new Set([
  'schema_version',
  'control_protocol',
  'error',
  'state_revision',
]);

const ERROR_FIELDS: ReadonlySet<string> = new Set([
  'code',
  'message',
  'retryable',
  'details',
]);

const MESSAGE_MAX = 500;

/**
 * Validate the header every v2 envelope carries.
 *
 * `fields` is the *complete* key set the caller expects, already widened for
 * every present-if-optional field it tolerates.
 */
export const decodeV2Header = (
  value: unknown,
  session: SessionIdentity,
  fields: ReadonlySet<string>,
  label: string
): Effect.Effect<JsonObject, DriftError> =>
  Effect.gen(function* () {
    const raw = yield* exact(value, fields, label);
    if (field(raw, 'schema_version') !== 2 || field(raw, 'control_protocol') !== FULL_CONTROL_V2) {
      return yield* Effect.fail(invalid(label, 'protocol mismatch'));
    }
    if (field(raw, 'game_id') !== session.gameId) {
      return yield* Effect.fail(invalid(label, 'response belongs to another game'));
    }
    if (hasField(raw, 'agent_id') && field(raw, 'agent_id') !== session.agentId) {
      return yield* Effect.fail(invalid(label, 'response belongs to another agent'));
    }
    return raw;
  });

export const decodeError = (value: unknown): Effect.Effect<StructuredError, DriftError> =>
  Effect.gen(function* () {
    const raw = yield* exact(value, ENVELOPE_FIELDS, 'v2 error response');
    if (
      field(raw, 'schema_version') !== 2 ||
      field(raw, 'control_protocol') !== FULL_CONTROL_V2
    ) {
      return yield* Effect.fail(invalid('v2 error response', 'protocol mismatch'));
    }
    const error = yield* exact(field(raw, 'error'), ERROR_FIELDS, 'v2 error');
    const code = field(error, 'code');
    const message = field(error, 'message');
    const retryable = field(error, 'retryable');
    const details = field(error, 'details');
    if (
      typeof code !== 'string' ||
      !V2_ERROR_CODES.has(code) ||
      typeof message !== 'string' ||
      message.trim().length === 0 ||
      message.length > MESSAGE_MAX ||
      typeof retryable !== 'boolean' ||
      !isJsonObject(details)
    ) {
      return yield* Effect.fail(invalid('v2 error response'));
    }
    const cleanDetails = yield* jsonObject(details, 'error details');
    const rawRevision = field(raw, 'state_revision');
    const revision = rawRevision === null ? null : yield* decodeRevision(rawRevision);
    return {
      schema_version: 2,
      control_protocol: FULL_CONTROL_V2,
      error: {
        code,
        message: message.trim(),
        retryable,
        details: cleanDetails,
      },
      state_revision: revision,
    } as const;
  });

/** The `HTTP {status}: {message} ({code})` sentence `V2ResponseError` carries. */
export const v2ErrorMessage = (status: number, payload: StructuredError): string =>
  `HTTP ${status}: ${payload.error.message} (${payload.error.code})`;
