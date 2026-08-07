/**
 * Legal-action descriptors.
 *
 * Ports `_validate_descriptor` (client.py:1364-1394).
 */
import { Effect } from 'effect';
import { DriftError, invalid } from 'src/errors';
import { ACTION_KIND_RE } from 'src/constants';
import {
  exact,
  field,
  isJsonObject,
  jsonObject,
  opaque,
  type JsonObject,
} from 'src/schema/primitives';
import { decodeRevision, revisionsEqual, type Revision } from 'src/schema/revision';

export interface LegalActionDescriptor {
  readonly action_id: string;
  readonly kind: string;
  readonly label: string;
  readonly subject: JsonObject;
  readonly arguments_schema: JsonObject;
  readonly state_revision: Revision;
}

const DESCRIPTOR_FIELDS: ReadonlySet<string> = new Set([
  'action_id',
  'kind',
  'label',
  'subject',
  'arguments_schema',
  'state_revision',
]);

const LABEL_MAX = 240;

/**
 * `label` names the *envelope* the descriptor arrived in, so a drift message
 * points at the page rather than at an anonymous object.  The Python raised a
 * fixed sentence; the port keeps that sentence and uses `label` only for the
 * `DriftError.label` field renderers key remedies off.
 */
export const decodeDescriptor = (
  value: unknown,
  revision: Revision,
  label = 'legal action descriptor'
): Effect.Effect<LegalActionDescriptor, DriftError> =>
  Effect.gen(function* () {
    const raw = yield* exact(value, DESCRIPTOR_FIELDS, label);
    const actionId = yield* opaque(field(raw, 'action_id'), 'action ID');
    const kind = field(raw, 'kind');
    const descriptorLabel = field(raw, 'label');
    const subject = field(raw, 'subject');
    const argumentsSchema = field(raw, 'arguments_schema');
    if (
      typeof kind !== 'string' ||
      !ACTION_KIND_RE.test(kind) ||
      typeof descriptorLabel !== 'string' ||
      descriptorLabel.trim().length === 0 ||
      descriptorLabel.length > LABEL_MAX ||
      !isJsonObject(subject) ||
      !isJsonObject(argumentsSchema)
    ) {
      return yield* Effect.fail(invalid('legal action descriptor'));
    }
    const own = yield* decodeRevision(field(raw, 'state_revision'));
    if (!revisionsEqual(own, revision)) {
      return yield* Effect.fail(invalid('legal action descriptor'));
    }
    return {
      action_id: actionId,
      kind,
      label: descriptorLabel.trim(),
      subject: yield* jsonObject(subject, 'action subject'),
      arguments_schema: yield* jsonObject(argumentsSchema, 'action arguments schema'),
      state_revision: revision,
    };
  });
