/**
 * Command batches and the CLI-side disposition.
 *
 * Types come from `play/docs/full-control-v2.openapi.json` (`Command`,
 * `CommandBatch`, `CliBatchDisposition`); the decoder ports the shape check
 * `_submit_persisted_batch` (client.py:8474-8500) performs when it re-reads a
 * batch body written before the first POST.
 *
 * The canonical *serialization* is `canonicalJson` in `services/json-output.ts`;
 * building a batch body is U13's job, not the schema layer's.
 */
import { Effect } from 'effect';
import { DriftError, invalid } from 'src/errors';
import { FULL_CONTROL_V2, V2_DISPOSITIONS } from 'src/constants';
import {
  exact,
  field,
  isJsonObject,
  jsonObject,
  opaque,
  type JsonObject,
  type SessionIdentity,
} from 'src/schema/primitives';
import { decodeRevision, type Revision } from 'src/schema/revision';
import { decodeError, type StructuredError } from 'src/schema/error';
import { decodeReceipt, type CommandReceipt } from 'src/schema/receipt';

export interface Command {
  readonly action_id: string;
  readonly arguments: JsonObject;
}

export interface CommandBatch {
  readonly schema_version: 2;
  readonly control_protocol: typeof FULL_CONTROL_V2;
  readonly game_id: string;
  readonly agent_id: string;
  readonly batch_id: string;
  readonly state_revision: Revision;
  readonly commands: ReadonlyArray<Command>;
}

export type Disposition =
  | 'receipt_terminal'
  | 'receipt_poll'
  | 'receipt_first'
  | 'retry_exact'
  | 'refresh';

export interface BatchDisposition {
  readonly schema_version: 2;
  readonly control_protocol: typeof FULL_CONTROL_V2;
  readonly game_id: string;
  readonly agent_id: string;
  readonly batch_id: string;
  readonly disposition: Disposition;
  readonly receipt: CommandReceipt | null;
  readonly error: StructuredError | null;
}

const BATCH_FIELDS: ReadonlySet<string> = new Set([
  'schema_version',
  'control_protocol',
  'game_id',
  'agent_id',
  'batch_id',
  'state_revision',
  'commands',
]);

const COMMAND_FIELDS: ReadonlySet<string> = new Set(['action_id', 'arguments']);

const DISPOSITION_FIELDS: ReadonlySet<string> = new Set([
  'schema_version',
  'control_protocol',
  'game_id',
  'agent_id',
  'batch_id',
  'disposition',
  'receipt',
  'error',
]);

export const decodeCommand = (value: unknown): Effect.Effect<Command, DriftError> =>
  Effect.gen(function* () {
    const raw = yield* exact(value, COMMAND_FIELDS, 'command');
    const actionId = yield* opaque(field(raw, 'action_id'), 'command action ID');
    const args = field(raw, 'arguments');
    if (!isJsonObject(args)) {
      return yield* Effect.fail(invalid('command arguments'));
    }
    return { action_id: actionId, arguments: yield* jsonObject(args, 'command arguments') };
  });

export const decodeCommandBatch = (
  value: unknown,
  session: SessionIdentity
): Effect.Effect<CommandBatch, DriftError> =>
  Effect.gen(function* () {
    const raw = yield* exact(value, BATCH_FIELDS, 'command batch');
    if (
      field(raw, 'schema_version') !== 2 ||
      field(raw, 'control_protocol') !== FULL_CONTROL_V2
    ) {
      return yield* Effect.fail(invalid('command batch', 'protocol mismatch'));
    }
    if (field(raw, 'game_id') !== session.gameId) {
      return yield* Effect.fail(invalid('command batch', 'batch belongs to another game'));
    }
    if (field(raw, 'agent_id') !== session.agentId) {
      return yield* Effect.fail(invalid('command batch', 'batch belongs to another agent'));
    }
    const batchId = yield* opaque(field(raw, 'batch_id'), 'command batch ID');
    const revision = yield* decodeRevision(field(raw, 'state_revision'));
    const commands = field(raw, 'commands');
    if (!Array.isArray(commands) || commands.length !== 1) {
      return yield* Effect.fail(invalid('command batch commands'));
    }
    const decoded: Command[] = [];
    for (const command of commands) {
      decoded.push(yield* decodeCommand(command));
    }
    return {
      schema_version: 2,
      control_protocol: FULL_CONTROL_V2,
      game_id: session.gameId,
      agent_id: session.agentId,
      batch_id: batchId,
      state_revision: revision,
      commands: decoded,
    } as const;
  });

export const decodeBatchDisposition = (
  value: unknown,
  session: SessionIdentity
): Effect.Effect<BatchDisposition, DriftError> =>
  Effect.gen(function* () {
    const raw = yield* exact(value, DISPOSITION_FIELDS, 'cli batch disposition');
    const batchId = yield* opaque(field(raw, 'batch_id'), 'batch disposition ID');
    const disposition = field(raw, 'disposition');
    if (typeof disposition !== 'string' || !V2_DISPOSITIONS.has(disposition)) {
      return yield* Effect.fail(invalid('batch disposition'));
    }
    const rawReceipt = field(raw, 'receipt');
    const receipt =
      rawReceipt === null ? null : yield* decodeReceipt(rawReceipt, session, { batchId });
    const rawError = field(raw, 'error');
    const error = rawError === null ? null : yield* decodeError(rawError);
    return {
      schema_version: 2,
      control_protocol: FULL_CONTROL_V2,
      game_id: session.gameId,
      agent_id: session.agentId,
      batch_id: batchId,
      disposition: disposition as Disposition,
      receipt,
      error,
    } as const;
  });
