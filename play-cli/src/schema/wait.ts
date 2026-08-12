/**
 * The wait envelope.
 *
 * Ports `_validate_wait_response` (client.py:2291-2355).  The wake contract is
 * the interesting half: a `phase_active` wake that is not actually this seat's
 * active, awaiting-agent phase is a drifted server, not a wake, and refusing it
 * here is what keeps `wait`'s exit code honest.
 */
import { Effect } from 'effect';
import { DriftError, invalid } from 'src/errors';
import { FULL_CONTROL_V2, TERMINAL_STATES, V2_WAKE_REASONS } from 'src/constants';
import { field, type SessionIdentity } from 'src/schema/primitives';
import { decodeV2Header } from 'src/schema/error';
import { decodeRevision, type Revision } from 'src/schema/revision';
import { decodeHealth, type HealthEnvelope } from 'src/schema/health';

export type WakeReason =
  | 'phase_active'
  | 'game_terminal'
  | 'revision_changed'
  | 'timeout'
  | 'boundary_recovered';

export type WaitUntil = 'phase' | 'revision';

export interface WaitEnvelope {
  readonly schema_version: 2;
  readonly control_protocol: typeof FULL_CONTROL_V2;
  readonly game_id: string;
  readonly agent_id: string;
  readonly wake_reason: WakeReason;
  readonly health: HealthEnvelope;
  readonly state_revision: Revision | null;
}

export interface WaitContract {
  readonly until: WaitUntil;
  readonly afterStateToken: string | null;
}

const WAIT_FIELDS: ReadonlySet<string> = new Set([
  'schema_version',
  'control_protocol',
  'game_id',
  'agent_id',
  'wake_reason',
  'health',
  'state_revision',
]);

export const decodeWait = (
  value: unknown,
  session: SessionIdentity,
  contract: WaitContract
): Effect.Effect<WaitEnvelope, DriftError> =>
  Effect.gen(function* () {
    const raw = yield* decodeV2Header(value, session, WAIT_FIELDS, 'v2 wait response');
    if (field(raw, 'agent_id') !== session.agentId) {
      return yield* Effect.fail(invalid('v2 wait response agent'));
    }
    const wakeReason = field(raw, 'wake_reason');
    if (typeof wakeReason !== 'string' || !V2_WAKE_REASONS.has(wakeReason)) {
      return yield* Effect.fail(invalid('v2 wait wake reason'));
    }
    const health = yield* decodeHealth(field(raw, 'health'), session);
    const rawRevision = field(raw, 'state_revision');
    const revision = rawRevision === null ? null : yield* decodeRevision(rawRevision);
    const phase = health.phase;
    const { until, afterStateToken } = contract;

    const broken =
      (until === 'phase' && revision !== null) ||
      (wakeReason === 'phase_active' &&
        !(
          until === 'phase' &&
          phase !== null &&
          
          phase.active &&
          phase.state === 'awaiting_agent' &&
          
          health.observation_available
        )) ||
      (wakeReason === 'game_terminal' && !TERMINAL_STATES.has(health.game_state)) ||
      (wakeReason === 'revision_changed' &&
        !(
          until === 'revision' &&
          revision !== null &&
          typeof afterStateToken === 'string' &&
          revision.state_token !== afterStateToken
        ));
    if (broken) {
      return yield* Effect.fail(invalid('v2 wait wake contract'));
    }

    return {
      schema_version: 2,
      control_protocol: FULL_CONTROL_V2,
      game_id: session.gameId,
      agent_id: session.agentId,
      wake_reason: wakeReason as WakeReason,
      health,
      state_revision: revision,
    } as const;
  });
