/**
 * The `full-control-v2` wait envelope.
 *
 * Clean-room Effect Schema re-expression of `_validate_wait_response`
 * (`play/client.py`:2298-2355).  Nothing here imports from `play-cli`; its
 * `src/schema/wait.ts` was read for semantics only.
 *
 * The wake *contract* is the interesting half, and it is why this module
 * exposes a schema factory rather than a plain decoder.  `wait` is the one
 * command whose exit code is a claim about the world — "your phase is live",
 * "the game is over", "the state moved" — and a server that answers
 * `wake_reason: "phase_active"` while its own embedded health says the phase is
 * `synchronizing` has told the caller a lie that no later command can detect.
 * Checking the answer against the question that was asked
 * (`client.py:2326-2345`) is what keeps that exit code honest, so the check
 * lives inside the parser and not at a call site that might forget it.
 *
 * @module
 */

import { Schema } from 'effect';
import type { SessionIdentity } from './primitives.ts';
import { ControlProtocolV2, SchemaVersionV2, sessionMismatch } from './protocol.ts';
import {
  AWAITING_AGENT,
  HealthEnvelope,
  healthEnvelopeFor,
  isTerminalGameState,
} from './health.ts';
import { StateRevision } from './revision.ts';
import { decodeTolerant, type TolerantDecoder } from '../tolerant.ts';

// ---------------------------------------------------------------------------
// Vocabulary — client.py:141-151
// ---------------------------------------------------------------------------

/**
 * `V2_WAKE_REASONS` — `play/client.py:141-151`.
 *
 * `boundary_recovered` is the youngest: it answers any wait that lands while
 * this seat's native boundary is being republished, so a caller is told "come
 * back, the world you were waiting on is being rebuilt" instead of timing out
 * against it.
 */
export const V2_WAKE_REASONS = [
  'phase_active',
  'game_terminal',
  'revision_changed',
  'timeout',
  'boundary_recovered',
] as const;

/** Why the wait returned. */
export const WakeReason = Schema.Literal(...V2_WAKE_REASONS).annotations({
  identifier: 'WakeReason',
  description: 'Why a full-control-v2 wait returned (client.py V2_WAKE_REASONS)',
});
/** Why the wait returned. */
export type WakeReason = typeof WakeReason.Type;

/**
 * `V2_SATISFIED_WAKE_REASONS` — the wake reasons that mean the wait got what
 * it asked for.  Everything else is either the game ending or the caller being
 * told to come back.
 */
export const V2_SATISFIED_WAKE_REASONS = [
  'phase_active',
  'revision_changed',
  'boundary_recovered',
] as const;

const SATISFIED: ReadonlySet<string> = new Set<string>(V2_SATISFIED_WAKE_REASONS);

/** True when the wake reason means the wait was satisfied rather than cut short. */
export const isSatisfiedWake = (
  reason: WakeReason,
): reason is (typeof V2_SATISFIED_WAKE_REASONS)[number] => SATISFIED.has(reason);

/** What a wait was asked to wait *for* — the `--until` selector. */
export const WAIT_UNTILS = ['phase', 'revision'] as const;

/** What a wait was asked to wait for. */
export type WaitUntil = (typeof WAIT_UNTILS)[number];

/**
 * The question the caller asked, which the answer is judged against.
 *
 * `afterStateToken` is the token the caller already knew about; a
 * `revision_changed` wake is only honest if the returned revision carries a
 * *different* one (`client.py:2343-2344`).
 */
export interface WaitContract {
  readonly until: WaitUntil;
  readonly afterStateToken: string | null;
}

// ---------------------------------------------------------------------------
// The envelope
// ---------------------------------------------------------------------------

/**
 * Build the wait envelope around a health schema.
 *
 * The health block carries the session binding when there is a session, so the
 * two schemas have to be assembled together rather than stitched afterwards.
 */
const waitStruct = <A, I>(health: Schema.Schema<A, I>) =>
  Schema.Struct({
    schema_version: SchemaVersionV2,
    control_protocol: ControlProtocolV2,
    game_id: Schema.String,
    agent_id: Schema.String,
    wake_reason: WakeReason,
    health,
    state_revision: Schema.NullOr(StateRevision),
  });

const WaitEnvelopeStruct = waitStruct(HealthEnvelope);

type WaitEnvelopeShape = typeof WaitEnvelopeStruct.Type;

/**
 * The wait envelope, checked against itself.
 *
 * No contract and no session: use {@link waitEnvelopeFor} wherever the
 * question that was asked is known.  This one exists for a recorder or a
 * parity harness, which sees answers to questions it did not ask.
 */
export const WaitEnvelope = WaitEnvelopeStruct.annotations({
  identifier: 'WaitEnvelope',
  description: 'The full-control-v2 wait envelope (client.py:2298)',
});
/** A decoded wait envelope. */
export type WaitEnvelope = typeof WaitEnvelope.Type;
/** The wire form of a {@link WaitEnvelope}. */
export type WaitEnvelopeEncoded = typeof WaitEnvelope.Encoded;

/** Decode an unknown value as a {@link WaitEnvelope}, contract aside. */
export const decodeWait: TolerantDecoder<WaitEnvelope> = decodeTolerant(
  WaitEnvelope,
  'WaitEnvelope',
);

/**
 * Every way the answer can fail to match the question — `client.py:2326-2345`,
 * clause for clause.
 *
 * 1. A `--until phase` wait never carries a revision: it was not watching one,
 *    so a revision in the answer means the server answered a different wait.
 * 2. `phase_active` claims this seat may act *now*, which is true only when the
 *    caller asked for a phase, a phase block came back, it is `active`, its
 *    state is `awaiting_agent`, and an observation is actually available to act
 *    on.  All five, or the wake is a lie.
 * 3. `game_terminal` claims the game is over; the embedded health has to agree.
 * 4. `revision_changed` claims the state moved past `afterStateToken`, which
 *    needs a revision-shaped wait, a revision in the answer, a token to compare
 *    against, and a genuinely different token.
 *
 * `timeout` and `boundary_recovered` claim nothing beyond rule 1, and are
 * deliberately unconstrained here.
 */
const contractIssues = (
  value: WaitEnvelopeShape,
  contract: WaitContract,
): ReadonlyArray<Schema.FilterIssue> => {
  const { until, afterStateToken } = contract;
  const revision = value.state_revision;
  const phase = value.health.phase;
  const reason = value.wake_reason;
  const phaseActiveHonest =
    until === 'phase' &&
    phase !== null &&
    phase.active &&
    phase.state === AWAITING_AGENT &&
    value.health.observation_available;
  const revisionChangedHonest =
    until === 'revision' &&
    revision !== null &&
    typeof afterStateToken === 'string' &&
    revision.state_token !== afterStateToken;
  return [
    ...(until === 'phase' && revision !== null
      ? [{ path: ['state_revision'], message: 'a phase wait does not carry a revision' }]
      : []),
    ...(reason === 'phase_active' && !phaseActiveHonest
      ? [
          {
            path: ['wake_reason'],
            message: 'phase_active woke a wait whose phase is not this seat’s to play',
          },
        ]
      : []),
    ...(reason === 'game_terminal' && !isTerminalGameState(value.health.game_state)
      ? [{ path: ['wake_reason'], message: 'game_terminal woke on a game that is not terminal' }]
      : []),
    ...(reason === 'revision_changed' && !revisionChangedHonest
      ? [
          {
            path: ['wake_reason'],
            message: 'revision_changed woke without a revision past the requested token',
          },
        ]
      : []),
  ];
};

/**
 * The wait envelope, bound to a session and to the question that was asked.
 *
 * `agent_id` is compared twice, exactly as the Python does it: once by the
 * shared header check (`client.py:1327-1328`) and once by `_validate_wait_response`
 * itself (`client.py:2314-2315`).  The second is not redundant — the header skips
 * the comparison for an envelope that has no `agent_id`, and a wait envelope
 * always has one.
 *
 * Build it once per session and reuse it; each call constructs a new AST.
 */
export const waitEnvelopeFor = (
  session: SessionIdentity,
  contract: WaitContract,
): Schema.Schema<WaitEnvelope, WaitEnvelopeEncoded> =>
  waitStruct(healthEnvelopeFor(session))
    .pipe(
      Schema.filter((value) => {
        // The Python performs this comparison twice — `_validate_v2_header`
        // (client.py:1325-1328) and then `_validate_wait_response` itself
        // (client.py:2315) — because the header skips an envelope that has no
        // `agent_id`, and a wait envelope always has one.  One comparison here
        // covers both, reported once, at whichever field actually mismatched.
        const header = sessionMismatch(session, {
          game_id: value.game_id,
          agent_id: value.agent_id,
        });
        const headerIssues: ReadonlyArray<Schema.FilterIssue> =
          header === undefined
            ? []
            : [
                {
                  path: value.game_id === session.gameId ? ['agent_id'] : ['game_id'],
                  message: header,
                },
              ];
        return [...headerIssues, ...contractIssues(value, contract)];
      }),
    )
    .annotations({ identifier: 'WaitEnvelope' });

/** Decode a wait envelope, binding it to `session` and to `contract`. */
export const decodeWaitFor = (
  session: SessionIdentity,
  contract: WaitContract,
): TolerantDecoder<WaitEnvelope> =>
  decodeTolerant(waitEnvelopeFor(session, contract), 'WaitEnvelope');

/** Re-exported for callers that only import this module. */
export type { SessionIdentity };
