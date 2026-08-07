/**
 * `state/monitor.log`.
 *
 * Ports `play/state_mirror.py:1631-1646` (`append_monitor_log`) and the client
 * bridge `_monitor_log` (client.py:10441-10442).
 *
 * A backgrounded monitor's stdout is lost to log rotation, to a closed pane, or
 * to the agent's own context being compacted.  This is what lets a harness
 * answer "when did my turns actually open" after the fact — and it is the
 * record of every `--exec` string this workspace ran.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import type { PrivateFs } from 'src/services/private-fs';
import {
  CONTROL_RE,
  MAX_LOG_LINE,
  MONITOR_LOG_FILE,
  appendMirror,
  codeSlice,
  mirrorDir,
  mirrorGuard,
} from 'src/services/mirror/store';

const pad = (value: number, width: number): string => String(value).padStart(width, '0');

/** CPython's `time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime())`. */
export const localTimestamp = (at: Date = new Date()): string =>
  `${pad(at.getFullYear(), 4)}-${pad(at.getMonth() + 1, 2)}-${pad(at.getDate(), 2)}T${pad(
    at.getHours(),
    2
  )}:${pad(at.getMinutes(), 2)}:${pad(at.getSeconds(), 2)}`;

/** `append_monitor_log` — append one stamped, sanitized line. */
export const appendMonitorLog = (
  dir: string,
  line: string,
  at?: Date
): Effect.Effect<string, PlayerError, PrivateFs> =>
  Effect.suspend(() => {
    // CPython's `[:_MAX_LOG_LINE]` counts code points; a code-unit slice would
    // split a surrogate pair in half in every logged `--exec` string.
    const sanitized = codeSlice(String(line).replace(CONTROL_RE, ' '), 0, MAX_LOG_LINE);
    return appendMirror(
      dir,
      MONITOR_LOG_FILE,
      `${localTimestamp(at ?? new Date())} ${sanitized}\n`
    );
  });

/** `_monitor_log` — append, warning instead of failing. */
export const mirrorMonitorLog = (
  sessionPath: string,
  line: string
): Effect.Effect<void, never, PrivateFs> =>
  mirrorGuard(Effect.flatMap(mirrorDir(sessionPath), (dir) => appendMonitorLog(dir, line)));
