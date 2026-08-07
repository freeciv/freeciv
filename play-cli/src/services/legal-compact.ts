/**
 * The compact projection of one legal action, and the two bounds that window it.
 *
 * Ports `_compact_legal_action` (client.py:7899-7947), `_compact_legal_offset`
 * (7950-7962) and `_compact_legal_limit` (7965-7974).
 *
 * The projection is the only shape agent context ever sees.  Two rules govern
 * it and both are load-bearing:
 *
 * 1. **Omit-when-default, never omit-by-type.**  `probability` is dropped only
 *    when the payload carries the exact certain-probability envelope.  An
 *    espionage gamble that rendered as certain is the one over-compaction that
 *    would corrupt a decision, so anything this client cannot interpret is
 *    still a probability and still prints.
 * 2. **The leak guard hides a value, never a key.**  A subject key that names
 *    an internal/native/packet/private/wire term survives as
 *    {@link V2_WITHHELD}, so a row still shows that a discriminator existed —
 *    and `--json` plus the cache still carry the payload verbatim.
 */
import { Effect } from 'effect';
import { playerError, type PlayerError } from 'src/errors';
import {
  V2_LEGAL_DRAIN_MAX_PAGES,
  V2_LEGAL_MATCH_LIMIT,
  V2_WITHHELD,
} from 'src/constants';
import { drift } from 'src/render/primitives';
import { compactJson } from 'src/services/json-output';
import { jsonEquals } from 'src/services/aliases';
import {
  field,
  hasField,
  isJsonObject,
  type JsonObject,
  type JsonValue,
} from 'src/schema/primitives';
import type { LegalActionDescriptor } from 'src/schema/descriptor';
import type { Revision } from 'src/schema/revision';

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

/**
 * One compacted action.
 *
 * Deliberately a bare {@link JsonObject}: `--json` prints it, the renderers
 * read it by key, and the byte budget measures its serialization — a nominal
 * wrapper type would have to be unwrapped at all three sites.
 */
export type CompactAction = JsonObject;

/** The `legal --all` result object, which is also its own `--json` payload. */
export interface LegalCompactResult {
  readonly schema_version: 1;
  readonly command: 'legal';
  readonly kind: string | null;
  readonly state_revision: Revision;
  readonly catalog_total: number;
  readonly pages_read: number;
  readonly matched: number;
  readonly offset: number;
  readonly limit: number;
  readonly shown: number;
  readonly truncated: boolean;
  readonly has_more: boolean;
  readonly next_offset: number | null;
  readonly byte_limited: boolean;
  readonly oversized_single: boolean;
  readonly hidden_kinds: Readonly<Record<string, number>>;
  readonly actions: ReadonlyArray<CompactAction>;
}

// ---------------------------------------------------------------------------
// _compact_legal_action
// ---------------------------------------------------------------------------

/** Subject keys the compact projection carries under their own rendering. */
const SUBJECT_DROPPED: ReadonlySet<string> = new Set(['target', 'probability', 'gold_cost']);

/** A subject key naming any of these is a value that must not reach an agent. */
const RESERVED_SUBJECT_TERMS: ReadonlySet<string> = new Set([
  'internal',
  'native',
  'packet',
  'private',
  'wire',
]);

/** The probability envelope that means "certain", and therefore renders away. */
const CERTAIN_PROBABILITY: JsonValue = {
  kind: 'exact',
  minimum_percent: 100,
  maximum_percent: 100,
};

/** CPython's `re.split(r"[^a-z0-9]+", key.casefold())`, empty parts dropped. */
const keyTerms = (key: string): ReadonlyArray<string> =>
  key
    .toLowerCase()
    .split(/[^a-z0-9]+/)
    .filter((part) => part !== '');

const isWithheldKey = (key: string): boolean =>
  key.startsWith('_') || keyTerms(key).some((part) => RESERVED_SUBJECT_TERMS.has(part));

/**
 * Re-key one validated descriptor as the plain JSON object the cache stores.
 *
 * `.v2-state` holds descriptors as opaque JSON; the renderers and the drain
 * both work over that shape, so there is exactly one projection input.
 */
export const descriptorToJson = (descriptor: LegalActionDescriptor): JsonObject => ({
  action_id: descriptor.action_id,
  kind: descriptor.kind,
  label: descriptor.label,
  subject: descriptor.subject,
  arguments_schema: descriptor.arguments_schema,
  state_revision: {
    turn: descriptor.state_revision.turn,
    revision: descriptor.state_revision.revision,
    state_token: descriptor.state_revision.state_token,
  },
});

/** Project one descriptor down to the row-and-`--json` shape. */
export const compactLegalAction = (
  descriptor: JsonObject
): Effect.Effect<CompactAction, PlayerError> =>
  Effect.suspend(() => {
    const subject = field(descriptor, 'subject');
    const schema = field(descriptor, 'arguments_schema');
    if (!isJsonObject(subject) || !isJsonObject(schema)) {
      return Effect.fail(drift('legal action subject'));
    }
    const compactSubject: Record<string, JsonValue> = {};
    for (const [key, value] of Object.entries(subject)) {
      if (SUBJECT_DROPPED.has(key)) continue;
      compactSubject[key] = isWithheldKey(key) ? V2_WITHHELD : value;
    }
    const target = field(subject, 'target');
    const result: Record<string, JsonValue> = {
      action_id: field(descriptor, 'action_id'),
      kind: field(descriptor, 'kind'),
      label: field(descriptor, 'label'),
      subject: compactSubject,
      target,
      argument_schema: schema,
    };
    // Omit-when-default, never omit-by-type.
    const probability = field(subject, 'probability');
    if (hasField(subject, 'probability') && !jsonEquals(probability, CERTAIN_PROBABILITY)) {
      result['probability'] = probability;
    }
    const declaredGold = field(subject, 'gold_cost');
    const targetGold = isJsonObject(target) ? field(target, 'gold_cost') : null;
    const goldCost = declaredGold === null ? targetGold : declaredGold;
    if (goldCost !== null) result['gold_cost'] = goldCost;
    const properties = field(schema, 'properties');
    const gold = isJsonObject(properties) ? field(properties, 'gold') : null;
    if (isJsonObject(gold)) {
      const range: Record<string, JsonValue> = {};
      for (const bound of ['minimum', 'maximum'] as const) {
        if (hasField(gold, bound)) range[bound] = field(gold, bound);
      }
      result['gold_range'] = range;
    }
    return Effect.succeed(result);
  });

/** The byte budget one compacted action spends: `json.dumps(...).encode()`. */
export const compactActionBytes = (compact: CompactAction): number =>
  Buffer.byteLength(compactJson(compact), 'utf8');

// ---------------------------------------------------------------------------
// _compact_legal_offset / _compact_legal_limit
// ---------------------------------------------------------------------------

const CANONICAL_INTEGER_RE = /^(?:0|[1-9][0-9]*)$/;
const COMPACT_LIMIT_RE = /^(?:[1-9]|[1-5][0-9]|6[0-4])$/;

const OFFSET_MAX = V2_LEGAL_DRAIN_MAX_PAGES * 16;

export const OFFSET_REFUSAL =
  'legal --offset must be a canonical integer from 0 through 8192';

export const COMPACT_LIMIT_REFUSAL =
  'legal --kind/--all --limit must be a canonical integer from 1 through 64';

/** `--offset`: 0..8192, canonical spelling only. */
export const compactLegalOffset = (
  value: string | null | undefined
): Effect.Effect<number, PlayerError> => {
  if (value === null || value === undefined || value === '') return Effect.succeed(0);
  if (!CANONICAL_INTEGER_RE.test(value) || Number.parseInt(value, 10) > OFFSET_MAX) {
    return Effect.fail(playerError(OFFSET_REFUSAL));
  }
  return Effect.succeed(Number.parseInt(value, 10));
};

/**
 * `--limit` in its compact sense: the result window, 1..64.
 *
 * The default differs by form.  `--kind` is a filtered window over every actor,
 * so 64 matches is a sensible ceiling; `--actor_id … --all` promises one
 * actor's whole menu, which routinely exceeds 64 rows, so only the byte cap
 * bounds it.
 */
export const compactLegalLimit = (
  value: string | null | undefined,
  defaultLimit: number = V2_LEGAL_MATCH_LIMIT
): Effect.Effect<number, PlayerError> => {
  if (value === null || value === undefined || value === '') return Effect.succeed(defaultLimit);
  if (!COMPACT_LIMIT_RE.test(value)) return Effect.fail(playerError(COMPACT_LIMIT_REFUSAL));
  return Effect.succeed(Number.parseInt(value, 10));
};
