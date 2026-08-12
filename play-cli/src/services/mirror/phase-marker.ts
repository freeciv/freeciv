/**
 * `state/phase.json` and `state/header.txt`.
 *
 * Ports `play/state_mirror.py:1517-1628` (`_phase_marker`, `_announced`,
 * `read_phase_marker`, `update_phase_marker`) and 1662-1757
 * (`update_from_health`), plus the client bridge `_mirror_health`
 * (client.py:3062-3072) and `_monitor_mirror` (10445-10454).
 *
 * Every other projection is prose or a table, which means a watcher has to
 * regex it.  The one question a multiplayer harness asks most — "is it my turn
 * yet, and if not, whose is it and how long have they had it" — gets a
 * sanctioned JSON answer instead.  Its shape is closed and versioned, and like
 * every file here the write is atomic, so a watcher polling it never reads a
 * half-written object.
 */
import { attemptOr } from 'src/errors';
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { isJsonObject, type JsonObject } from 'src/schema/primitives';
import { indentedJson } from 'src/services/json-output';
import type { PrivateFs } from 'src/services/private-fs';
import {
  DELTA_FILE,
  DEFAULT_COMMAND_CARD,
  CONTROL_RE,
  HEADER_FILE,
  MAP_FILE,
  MISSING,
  OVERVIEW_FILE,
  PHASE_FILE,
  PHASE_SCHEMA_VERSION,
  cell,
  dig,
  isNewer,
  mirrorDir,
  mirrorGuard,
  numberValue,
  readMirror,
  revLine,
  revisionPair,
  textValue,
  writeMirror,
  type MirrorRevision,
} from 'src/services/mirror/store';
import { parseTable } from 'src/services/mirror/table';

/** The monitor's `[incarnation, turn, phase]` record of its last announcement. */
export type Announced = ReadonlyArray<number>;

/** `_announced` — accept only a well-formed triple, else nothing. */
export const announcedTuple = (value: unknown): ReadonlyArray<number> | null => {
  if (!Array.isArray(value) || value.length !== 3) return null;
  const items: ReadonlyArray<unknown> = value;
  return items.every(
    (item) => typeof item === 'number' && Number.isInteger(item) && item >= 0
  )
    ? (items as ReadonlyArray<number>)
    : null;
};

/** The clock `_phase_marker` stamps `updated_at` with. Injectable for tests. */
export type MirrorClock = () => number;

const systemClock: MirrorClock = () => Date.now() / 1000;

const holderOf = (phase: unknown): JsonObject | null => {
  if (!isJsonObject(phase)) return null;
  const waiting = dig(phase, 'waiting_on');
  const seats = isJsonObject(waiting) ? waiting['seats'] : undefined;
  const rows: ReadonlyArray<unknown> = Array.isArray(seats) ? seats : [];
  const others = rows.filter((row) => isJsonObject(row) && row['is_self'] === false);
  const row = others[0];
  if (others.length !== 1 || phase['active'] === true || !isJsonObject(row)) return null;
  return {
    place: numberValue(row['place']),
    seat_id: textValue(row['seat_id']),
    player_name: textValue(row['player_name']),
    controller_label: textValue(row['controller_label']),
  };
};

/**
 * `_phase_marker` — render `state/phase.json` from one validated health payload.
 *
 * Closed shape, closed value types: every field is copied from a payload the
 * client already validated, or is `null`.  A watcher may therefore branch on
 * `active` without a schema check, and `schema_version` tells it when it may
 * not.
 */
export const phaseMarker = (
  health: unknown,
  announced?: unknown,
  clock: MirrorClock = systemClock
): string => {
  const phase = dig(health, 'phase');
  const phaseObject = isJsonObject(phase) ? phase : null;
  const timing = phaseObject === null ? undefined : phaseObject['timing'];
  const timingObject = isJsonObject(timing) ? timing : null;
  return indentedJson({
    schema_version: PHASE_SCHEMA_VERSION,
    updated_at: Math.round(clock() * 1000) / 1000,
    game_state: textValue(dig(health, 'game_state')),
    turn: numberValue(phaseObject?.['turn']),
    phase: numberValue(phaseObject?.['phase']),
    state: textValue(phaseObject?.['state']),
    active: phaseObject?.['active'] === true,
    held_s: numberValue(timingObject?.['elapsed_s']),
    deadline_s_left: numberValue(timingObject?.['remaining_s']),
    holder: holderOf(phase),
    announced: announcedTuple(announced),
  });
};

/**
 * `read_phase_marker` — read the marker back, or `null` when it is absent or
 * unusable.  A marker this module cannot parse is treated as absent rather
 * than as an error: it is a projection, and a watcher's own bad file must
 * never stop the client that writes it.
 */
export const readPhaseMarker = (
  dir: string
): Effect.Effect<JsonObject | null, never, PrivateFs> =>
  Effect.map(readMirror(dir, PHASE_FILE), (text) => {
    if (text === null) return null;
    const parsed = ((): unknown => {
      return attemptOr(
          () => JSON.parse(text) as unknown,
          () => null
      );
    })();
    return isJsonObject(parsed) && parsed['schema_version'] === PHASE_SCHEMA_VERSION
      ? parsed
      : null;
  });

/**
 * `update_phase_marker` — write `state/phase.json` alone, touching no other
 * projection.  This is the monitor's write: it deliberately does *not* rewrite
 * `state/header.txt`, because a long-lived read-only observer rewriting the
 * projections a real command owns would be a second writer for no gain.
 */
export const updatePhaseMarker = (
  dir: string,
  health: unknown,
  options?: { readonly announced?: unknown; readonly clock?: MirrorClock }
): Effect.Effect<ReadonlyArray<string>, PlayerError, PrivateFs> =>
  Effect.gen(function* () {
    const supplied = options?.announced;
    const announced =
      supplied === undefined || supplied === null
        ? (yield* readPhaseMarker(dir))?.['announced']
        : supplied;
    const written = yield* writeMirror(
      dir,
      PHASE_FILE,
      phaseMarker(health, announced, options?.clock ?? systemClock)
    );
    return [written];
  });

// ---------------------------------------------------------------------------
// update_from_health
// ---------------------------------------------------------------------------

export interface HealthMirrorOptions {
  /** The page revision the same command observed, when one is known. */
  readonly revision?: unknown;
  /** The command card the header prints. `_DEFAULT_COMMAND_CARD` when absent. */
  readonly commands?: ReadonlyArray<string>;
  readonly clock?: MirrorClock;
}

/**
 * `update_from_health` — write `state/header.txt` and `state/phase.json`.
 *
 * Health carries no state revision, so pass `revision` when one is known;
 * otherwise the header reuses the newest revision already recorded in the
 * mirror, and stamps `# rev ?` when the mirror has never seen a page.
 */
export const updateFromHealth = (
  dir: string,
  command: string,
  health: unknown,
  options?: HealthMirrorOptions
): Effect.Effect<ReadonlyArray<string>, PlayerError, PrivateFs> =>
  Effect.gen(function* () {
    const stamp =
      options?.revision === undefined || options.revision === null
        ? yield* newestStamp(dir)
        : yield* revisionPair(options.revision);
    const lines: string[] = [revLine(stamp)];
    const add = (name: string, value: string): void => {
      lines.push(`${name.padEnd(9)} ${value}`);
    };
    add(
      'game',
      `${cell(dig(health, 'game_id'))} · seat ${cell(dig(health, 'seat', 'place'))} ${cell(
        dig(health, 'seat', 'player_name')
      )} · controller ${cell(dig(health, 'agent', 'controller_label'))}`
    );
    const objective = dig(health, 'objective');
    if (objective !== MISSING) {
      add('objective', cell(objective));
      add(
        'budget',
        `turn ${cell(dig(health, 'phase', 'turn'))} of ${cell(
          dig(health, 'max_turns')
        )} · ${cell(dig(health, 'turns_remaining'))} remaining`
      );
    }
    const phase = dig(health, 'phase');
    if (isJsonObject(phase)) {
      add(
        'phase',
        `${cell(phase['state'])} · turn ${cell(phase['turn'])} phase ${cell(
          phase['phase']
        )} · active ${cell(phase['active'])}`
      );
      const timing = phase['timing'];
      if (isJsonObject(timing)) {
        add(
          'timing',
          `${cell(timing['mode'])} · ${cell(timing['remaining_s'])}s left of ${cell(
            timing['timeout_s']
          )}s`
        );
      }
    }
    add(
      'game_state',
      `${cell(dig(health, 'game_state'))} · observation ${cell(
        dig(health, 'observation_available')
      )} · legal_actions ${cell(dig(health, 'legal_actions_available'))}`
    );
    add(
      'sidecar',
      `${cell(dig(health, 'sidecar', 'state'))} · server_connected ${cell(
        dig(health, 'sidecar', 'server_connected')
      )}`
    );
    lines.push('');
    lines.push(
      'protocol  full-control-v2 · every action is an opaque server-issued capability;'
    );
    lines.push(
      '          aliases are revision-scoped and stop resolving once a newer revision arrives.'
    );
    lines.push('files     state/overview.tsv state/units.tsv state/cities.tsv state/map.txt');
    lines.push(
      '          state/options/<alias>.txt state/delta.md cache/*.tsv  (projections; read them, they cost no network)'
    );
    lines.push('');
    for (const entry of options?.commands ?? DEFAULT_COMMAND_CARD) {
      lines.push(String(entry).replace(CONTROL_RE, ' '));
    }
    // The marker is a read-modify-write like every other file here: the
    // monitor's `announced` tuple must survive an unrelated `just health`, or a
    // restarted monitor would re-announce a turn already announced.
    const current = yield* readPhaseMarker(dir);
    const header = yield* writeMirror(dir, HEADER_FILE, lines.join('\n'));
    const marker = yield* writeMirror(
      dir,
      PHASE_FILE,
      phaseMarker(health, current?.['announced'], options?.clock ?? systemClock)
    );
    return [header, marker];
  });

const newestStamp = (dir: string): Effect.Effect<MirrorRevision | null, never, PrivateFs> =>
  Effect.reduce(
    [OVERVIEW_FILE, MAP_FILE, DELTA_FILE],
    null as MirrorRevision | null,
    (stamp, target) =>
      Effect.map(readMirror(dir, target), (text) => {
        const found = parseTable(text).revision;
        return found !== null && isNewer(found, stamp) ? found : stamp;
      })
  );

// ---------------------------------------------------------------------------
// The client.py bridge
// ---------------------------------------------------------------------------

/** `_mirror_health` — project one health payload, warning instead of failing. */
export const mirrorHealth = (
  sessionPath: string,
  health: unknown,
  command: string,
  options?: HealthMirrorOptions
): Effect.Effect<void, never, PrivateFs> =>
  mirrorGuard(
    Effect.flatMap(mirrorDir(sessionPath), (dir) =>
      updateFromHealth(dir, command, health, options)
    )
  );

/** `_monitor_mirror` — the monitor's only write: `state/phase.json`, nothing else. */
export const mirrorPhaseMarker = (
  sessionPath: string,
  health: unknown,
  announced?: unknown
): Effect.Effect<void, never, PrivateFs> =>
  mirrorGuard(
    Effect.flatMap(mirrorDir(sessionPath), (dir) =>
      updatePhaseMarker(dir, health, announced === undefined ? {} : { announced })
    )
  );
