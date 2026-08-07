/**
 * Every line `play monitor` prints.
 *
 * Ports `_monitor_announce_line` (client.py:10271-10290), `_monitor_terminal_line`
 * (10293-10300), `_missed_line` (10339-10378) and the two status sentences
 * `_monitor_status` (10457-10476) and `command_monitor` (10564-10584) share.
 *
 * The announce line is deliberately byte-compatible with the header `just wait`
 * printed *before* the PvP legibility work: one harness converged on parsing
 * exactly this, and a notification channel is the wrong place to break a
 * parser.  The enrichment lives on the waiting and missed lines, where nothing
 * was parsing anything.
 */
import { phaseText } from 'src/render/phase';
import { revisionLabel, scalar } from 'src/render/primitives';
import { isJsonObject, type JsonObject, type JsonValue } from 'src/schema/primitives';
import type { WaitEnvelope } from 'src/schema/wait';

/** CPython truthiness over a JSON value — `{}` and `null` are both false. */
export const pythonTruthy = (value: JsonValue | undefined): boolean => {
  if (value === null || value === undefined || value === false || value === '' || value === 0) {
    return false;
  }
  if (Array.isArray(value)) return value.length > 0;
  if (isJsonObject(value)) return Object.keys(value).length > 0;
  return true;
};

const cell = (value: JsonValue | undefined): string => scalar(value === undefined ? null : value);

// ---------------------------------------------------------------------------
// The wake lines
// ---------------------------------------------------------------------------

/** `_monitor_announce_line` — the wake line, in the shape harnesses parse. */
export const monitorAnnounceLine = (wait: WaitEnvelope): string => {
  const health = wait.health;
  const phase = health.phase;
  const turn = phase === null ? null : phase.turn;
  const revision = wait.state_revision;
  const header =
    (turn === null ? 'T?' : `T${scalar(turn)}`) +
    (revision === null ? '' : ` ${revisionLabel(revision)}`);
  return (
    `${header} | woke ${wait.wake_reason} | ${health.game_state} | ` +
    `${phaseText(phase)} | next: just turn`
  );
};

/** `_monitor_terminal_line` — the game is over; stop looping. */
export const monitorTerminalLine = (wait: WaitEnvelope): string => {
  const phase = wait.health.phase;
  const turn = phase === null ? null : phase.turn;
  const header = turn === null ? 'T?' : `T${scalar(turn)}`;
  return `${header} | GAME OVER | ${wait.health.game_state} | next: just result`;
};

// ---------------------------------------------------------------------------
// The missed-phase alarm
// ---------------------------------------------------------------------------

/**
 * `_missed_line` — say that a turn was lost, and after the second one say it
 * louder.
 *
 * Two phases lost in a row means the notification path itself is broken, and
 * the wording stops being a note and becomes an alarm.
 */
export const missedLine = (
  event: JsonObject,
  consecutive: number,
  since: JsonValue | undefined
): string => {
  const orders = event['orders_submitted'];
  const elapsed = cell(event['elapsed_s']);
  const cause = ((): string => {
    if (event['source'] === 'auto_idle') {
      return `ended for you after ${elapsed}s — nothing was idle and no decision was pending`;
    }
    if (orders === 0) {
      return `was ended by timeout after ${elapsed}s — you issued no orders`;
    }
    if (typeof orders === 'number' && Number.isInteger(orders)) {
      return (
        `was ended by timeout after ${elapsed}s — you issued ${orders} ` +
        `order${orders === 1 ? '' : 's'} but never ended the phase`
      );
    }
    return `was ended by timeout after ${elapsed}s`;
  })();
  const head =
    `T${cell(event['turn'])} | MISSED` + (consecutive > 1 ? ` ×${consecutive}` : '');
  const line =
    `${head} | your phase t${cell(event['turn'])}/p${cell(event['phase'])} opened and ${cause}`;
  if (consecutive <= 1) return line;
  const played =
    typeof since === 'number' && Number.isInteger(since)
      ? `you have not issued an order since t${scalar(since)}. `
      : '';
  return (
    `${line} | ${played}Your monitor is not reaching you — check it now; ` +
    'the game is advancing without you.'
  );
};

// ---------------------------------------------------------------------------
// --status, and the second persistent monitor
// ---------------------------------------------------------------------------

/** "t3/p1", or "no phase yet" when the marker has never seen one. */
export const watchingText = (marker: JsonObject | null): string => {
  const turn = marker?.['turn'];
  return turn === undefined || turn === null
    ? 'no phase yet'
    : `t${scalar(turn)}/p${cell(marker?.['phase'])}`;
};

/** "your phase is open", "seat N holds it", or "waiting". */
export const whoseText = (marker: JsonObject | null): string => {
  if (pythonTruthy(marker?.['active'])) return 'your phase is open';
  const holder = marker?.['holder'];
  if (!pythonTruthy(holder)) return 'waiting';
  return `seat ${cell(isJsonObject(holder) ? holder['place'] : null)} holds it`;
};

/** `_monitor_status`'s single line. */
export const monitorRunningLine = (holder: JsonObject, marker: JsonObject | null): string =>
  `monitor running (pid ${cell(holder['pid'])}, since ${cell(holder['since'])}, ` +
  `watching ${watchingText(marker)} · ${whoseText(marker)})`;

/**
 * The line a second `play monitor` prints instead of starting.
 *
 * Idempotent by construction: starting a second one is not an error, it is a
 * no-op that reports the first.
 */
export const monitorAlreadyRunningLine = (
  running: JsonObject,
  marker: JsonObject | null
): string =>
  `monitor already running (pid ${cell(running['pid'])}, since ` +
  `${cell(running['since'])}, watching ${watchingText(marker)} · ${whoseText(marker)})`;

export const MONITOR_NOT_RUNNING = 'monitor not running';

/** `--status` with nothing running: the remedy names the command that starts one. */
export const MONITOR_STATUS_IDLE: ReadonlyArray<string> = [
  MONITOR_NOT_RUNNING,
  'next: just monitor',
];

export const monitorStoppedLine = (pid: number): string => `monitor stopped (pid ${pid})`;

/** A monitor must never keep watching a seat the workspace has left. */
export const MONITOR_REBOUND_LINE =
  'monitor stopping: this workspace was rebound to another seat';

/** `--max-s` gave up: stderr, because it is not an announcement. */
export const MONITOR_MAX_S_LINE = 'monitor: --max-s elapsed and the phase is still not yours';
