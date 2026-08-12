/**
 * Legal-action descriptors — one same-revision, opaque capability.
 *
 * Clean-room Effect Schema re-expression of `_validate_descriptor`
 * (`play/client.py:1369-1401`), read a second time through
 * `play-cli/src/schema/descriptor.ts`, and cross-checked against
 * `LegalActionDescriptor` in `play/docs/full-control-v2.openapi.json`.
 * Nothing here imports from `play-cli`.
 *
 * ## What a descriptor is for
 *
 * The supervisor computes the set of things a seat may legally do *at one
 * state*, and hands each of them back as a descriptor.  A caller chooses by
 * reading `kind` and `label`, fills in whatever `arguments_schema` demands,
 * and submits **only** `action_id` (`./batch.ts`).  It never constructs an
 * action, never edits an id, and never infers arguments from the kind — the
 * contract says so in as many words: "cache the full descriptor and submit
 * only its action_id with arguments matching arguments_schema".
 *
 * That is why `subject` and `arguments_schema` are bounded but unshaped here:
 * they are data the client renders and echoes, not data it interprets.
 *
 * ## The revision is half the capability
 *
 * `_validate_descriptor` takes the enclosing revision as an argument
 * (`:1370`) and refuses any descriptor that names a different one (`:1388`).
 * A descriptor is only spendable against the state it was computed for, so a
 * cached one replayed after the state moved is a stale capability. There are
 * two call sites and both are covered:
 *
 * - inside a page, where the revision is the page's — `./legal-page.ts`
 *   checks every item against the envelope, and can name the offending index
 *   where the Python's fixed sentence could not;
 * - standalone, where the revision is whatever the caller holds —
 *   {@link legalActionDescriptorAt} below.
 *
 * ## Two deliberate divergences
 *
 * **Excess is preserved, not refused.**  `_exact` (`:1371`) rejects a
 * descriptor that gained a field; `@arena/wire` keeps unknown fields through
 * decode and re-encode instead (`../tolerant.ts`), because a supervisor that
 * adds a key must not strand a seat mid-game.  Every other refusal survives.
 *
 * **The label is kept verbatim.**  The Python *stores* `raw["label"].strip()`
 * (`:1395`).  Trimming inside a decoder would break `encode(decode(x)) === x`,
 * so the validation stays (blank-once-trimmed is refused, over 240 characters
 * is refused) and the trimmed reading moves to
 * {@link descriptorLabelText}.
 *
 * @module
 */

import { Schema } from 'effect';
import { OpaqueId } from './ids.ts';
import { codePointLength } from './primitives.ts';
import { V2JsonObject } from './protocol.ts';
import { Revision, revisionsEqual } from './revision.ts';
import { decodeTolerant, type TolerantDecoder } from '../tolerant.ts';

// ---------------------------------------------------------------------------
// Kind and label
// ---------------------------------------------------------------------------

/**
 * `ACTION_KIND_RE` — `play/client.py:99`:
 * `re.compile(r"^[a-z][a-z0-9_]*\.[a-z][a-z0-9_]*$")`, applied with
 * `fullmatch`; the OpenAPI's `ActionKind` carries the same pattern.
 */
export const ACTION_KIND_RE: RegExp = /^[a-z][a-z0-9_]*\.[a-z][a-z0-9_]*$/;

/**
 * The public capability family, in `namespace.action` form — `unit.move`,
 * `phase.end`, `city.investigate`.
 *
 * A **pattern, never an enum.**  The contract is explicit that "new conforming
 * kinds may be added without a protocol version change", so a closed list here
 * would turn every new server capability into a failed decode.  This is the
 * value a caller is meant to branch on; the action id beside it is opaque and
 * carries no information at all.
 */
export const ActionKind = Schema.String.pipe(
  Schema.pattern(ACTION_KIND_RE, {
    identifier: 'ActionKind',
    description: 'Public capability family in namespace.action form (ACTION_KIND_RE)',
  }),
  Schema.brand('ActionKind'),
);
/** A validated action kind. */
export type ActionKind = typeof ActionKind.Type;

/** Decode an unknown value as an {@link ActionKind}. */
export const decodeActionKind: TolerantDecoder<ActionKind> = decodeTolerant(
  ActionKind,
  'ActionKind',
);

/** True when `value` is a well-formed action kind. */
export const isActionKind = (value: string): value is ActionKind => ACTION_KIND_RE.test(value);

/** `len(raw["label"]) > 240` — `play/client.py:1384`, measured *before* trimming. */
export const DESCRIPTOR_LABEL_MAX = 240;

/** Python's `len()` counts code points, not UTF-16 code units. */
/**
 * The human-readable name of an action.
 *
 * Both halves of the check matter.  The label is what an agent reads in order
 * to choose, so `"   "` is not a choice; and an unbounded label is a
 * context-window attack against whoever renders the page.
 */
export const DescriptorLabel: Schema.Schema<string> = Schema.String.pipe(
  Schema.filter((value) => {
    if (value.trim().length === 0) return 'descriptor label is blank';
    return codePointLength(value) > DESCRIPTOR_LABEL_MAX
      ? `descriptor label is longer than ${String(DESCRIPTOR_LABEL_MAX)} characters`
      : undefined;
  }),
).annotations({
  identifier: 'DescriptorLabel',
  description: 'A non-blank action label of at most 240 characters',
});

/** `raw["label"].strip()` — `play/client.py:1395`: the Python's stored reading. */
export const descriptorLabelText = (descriptor: { readonly label: string }): string =>
  descriptor.label.trim();

// ---------------------------------------------------------------------------
// The descriptor — play/client.py:1369-1401
// ---------------------------------------------------------------------------

/**
 * One executable capability, as offered by the supervisor.
 *
 * `subject` is what the action is about (its `operation`, target, probability,
 * costs) and `arguments_schema` is the exact JSON Schema for the command's
 * arguments.  Both are opaque JSON objects capped by `_json_value`
 * (`:1396-1399`) and neither is shaped here: the Python never shaped them, and
 * inventing a shape would refuse a descriptor a live supervisor is offering.
 */
export const LegalActionDescriptor = Schema.Struct({
  /** The whole capability.  Echoed into a command; never parsed, never built. */
  action_id: OpaqueId,
  kind: ActionKind,
  label: DescriptorLabel,
  /** Caller-visible semantic subject; open by contract. */
  subject: V2JsonObject,
  /** The exact JSON Schema this action's arguments must match. */
  arguments_schema: V2JsonObject,
  /** The state this capability was computed at, and is only valid at. */
  state_revision: Revision,
}).annotations({
  identifier: 'LegalActionDescriptor',
  description: 'One same-revision opaque capability (client.py:1369)',
});
/** A decoded legal-action descriptor. */
export type LegalActionDescriptor = typeof LegalActionDescriptor.Type;
/** The wire form of a {@link LegalActionDescriptor}. */
export type LegalActionDescriptorEncoded = typeof LegalActionDescriptor.Encoded;

/** Decode a descriptor on its own, without checking which revision it names. */
export const decodeLegalActionDescriptor: TolerantDecoder<LegalActionDescriptor> = decodeTolerant(
  LegalActionDescriptor,
  'LegalActionDescriptor',
);

/**
 * A descriptor that must have been minted at `revision` — the
 * `_validate_revision(raw["state_revision"]) != revision` half of
 * `_validate_descriptor` (`play/client.py:1388`).
 *
 * All three revision fields are compared, `state_token` included: the same
 * `(turn, revision)` under a new token is a republished state, and a
 * capability computed against the old one may no longer be legal.  Refusing is
 * the point — the alternative is spending an action against a world the caller
 * has not seen.
 */
export const legalActionDescriptorAt = (
  revision: Revision,
): Schema.Schema<LegalActionDescriptor, LegalActionDescriptorEncoded> =>
  LegalActionDescriptor.pipe(
    Schema.filter((descriptor) =>
      revisionsEqual(descriptor.state_revision, revision)
        ? undefined
        : 'the descriptor was minted at a different state revision',
    ),
  ).annotations({ identifier: 'LegalActionDescriptor' });

/** Decode a standalone descriptor and require it to name `revision`. */
export const decodeDescriptorAt = (
  revision: Revision,
): TolerantDecoder<LegalActionDescriptor> =>
  decodeTolerant(legalActionDescriptorAt(revision), 'LegalActionDescriptor');
