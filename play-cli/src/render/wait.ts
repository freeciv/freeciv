/**
 * How one wake reads.
 *
 * Ports `_await_line` (client.py:6698-6743), `_waiting_tick_line`
 * (10220-10236) and `_render_wait` (10238-10248).
 *
 * `awaitLine` is the single header `wait`, `turn --end --await` and
 * `do --end --await` all print, which is the whole reason it lives apart from
 * the command: a blocking wait and a blocking phase end have to read
 * identically or a harness parses one of them wrong.
 *
 * Everything below the header is `just health`'s own rendering, so this file
 * imports U06 (`src/render/health.ts`) rather than restating it.  Nothing here
 * is a field the `--json` form does not carry.
 */
import { V2_WAIT_EXIT_RETRY } from 'src/exit';
import { duration, revisionLabel, scalar } from 'src/render/primitives';
import { holderSeat, phaseHeadline, renderHealth, seatLabel } from 'src/render/health';
import type { HealthEnvelope } from 'src/schema/health';
import type { WaitEnvelope } from 'src/schema/wait';

/**
 * What the agent should run next.
 *
 * Empty exactly when the caller is about to print that next thing itself
 * (`--brief`), because a header pointing at a briefing three lines below it is
 * noise.
 */
export const AWAIT_FOLLOW_DEFAULT = 'just turn';

/**
 * The tail a *blocked* wake carries instead.
 *
 * A timeout is not a wake at all, and calling it one is how a timeout came to
 * print a full briefing for a phase the caller did not hold, under the tail
 * `next: just turn` — a command that can only be refused.  The spacing is
 * load-bearing: eight spaces before the bracket.
 */
export const AWAIT_FOLLOW_BLOCKED = `just wait --for-turn        [exit ${V2_WAIT_EXIT_RETRY}]`;

/** Render the next briefing header from one validated wake. */
export const awaitLine = (wait: WaitEnvelope, follow: string = AWAIT_FOLLOW_DEFAULT): string => {
  const health = wait.health;
  const phase = health.phase;
  const turn = phase === null ? null : phase.turn;
  const revision = wait.state_revision;
  const header =
    (turn === null ? 'T?' : `T${scalar(turn)}`) +
    (revision === null ? '' : ` ${revisionLabel(revision)}`);

  const holder = phase === null ? null : holderSeat(phase);
  if (holder !== null && phase !== null) {
    const timing = phase.timing;
    const tail = follow === '' ? '' : AWAIT_FOLLOW_BLOCKED;
    return (
      `${header} | still ${seatLabel(holder)} · ` +
      `t${scalar(phase.turn)}/p${scalar(phase.phase)}` +
      (timing.elapsed_s === null ? '' : ` · held ${duration(timing.elapsed_s)}`) +
      (timing.remaining_s === null ? '' : ` · ${duration(timing.remaining_s)} left`) +
      (tail === '' ? '' : ` | next: ${tail}`)
    );
  }

  const line =
    `${header} | ${phaseHeadline(phase)} | ${health.game_state}` +
    (wait.wake_reason === 'phase_active' ? '' : ` | woke ${wait.wake_reason}`);
  return line + (follow === '' ? '' : ` | next: ${follow}`);
};

/** One line per internal poll, so a long block is never a silent gap. */
export const waitingTickLine = (health: HealthEnvelope): string => {
  const phase = health.phase;
  const holder = phase === null ? null : holderSeat(phase);
  if (holder === null || phase === null) {
    return "… waiting for this seat's phase to open";
  }
  const timing = phase.timing;
  return (
    `… waiting on ${seatLabel(holder)}` +
    (timing.elapsed_s === null ? '' : ` · held ${duration(timing.elapsed_s)}`) +
    (timing.remaining_s === null ? '' : ` · ${duration(timing.remaining_s)} left`)
  );
};

/**
 * Render one wake the way every other v2 command renders: compactly.
 *
 * The first line is the same header `turn --end --await` prints; the health
 * one-liners below it are what `just health` prints.
 */
export const renderWait = (wait: WaitEnvelope): ReadonlyArray<string> => [
  awaitLine(wait),
  ...renderHealth(wait.health),
];

/** `holderSeat`, re-exported so the wait engine's hooks have one import. */
export { holderSeat };
