/**
 * Map parsing primitives.
 *
 * Ports `play/state_mirror.py:177-194` (`_TERRAIN_CHARS`, `_TERRAIN_FALLBACK`),
 * 721-727 (the `map.txt` regexes) and 730-851 (`_terrain_chars`, `_Map`,
 * `_parse_map`, `_map_size`, `_tile_chars`).
 *
 * The write side — `_render_map`, `_update_map`, `_update_yields` — is U08's.
 * Everything here is the read half plus the character assignment both halves
 * share, because a merged grid may never change the meaning of a character
 * mid-file: `terrainChars` is always seeded with the legend already on disk.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { isJsonObject } from 'src/schema/primitives';
import type { PrivateFs } from 'src/services/private-fs';
import {
  OVERVIEW_FILE,
  PY_SPACE_CLASS,
  dig,
  mirrorError,
  readMirror,
  splitLines,
  strip,
  type MirrorRevision,
} from 'src/services/mirror/store';
import { parseRevLine, parseTable } from 'src/services/mirror/table';

/**
 * `_TERRAIN_CHARS` — stable, documented one-character terrain codes.
 * Uppercase = currently visible, lowercase = remembered through fog,
 * `?` = never seen.
 */
export const TERRAIN_CHARS: ReadonlyMap<string, string> = new Map([
  ['Arctic', 'A'],
  ['Deep Ocean', 'B'],
  ['Desert', 'D'],
  ['Forest', 'F'],
  ['Glacier', 'C'],
  ['Grassland', 'G'],
  ['Hills', 'H'],
  ['Inaccessible', 'I'],
  ['Jungle', 'J'],
  ['Lake', 'L'],
  ['Mountains', 'M'],
  ['Ocean', 'O'],
  ['Plains', 'P'],
  ['Swamp', 'S'],
  ['Tundra', 'T'],
]);

/** `_TERRAIN_FALLBACK` — the alphabet a ruleset-invented terrain draws from. */
export const TERRAIN_FALLBACK = 'ENRUVWXYZ123456789';

export const MAP_WINDOW_RE = /^# window x (-?\d+)\.\.(-?\d+) y (-?\d+)\.\.(-?\d+)\b/;

/**
 * `_MAP_ROW_RE` and `_MAP_LEGEND_PAIR_RE`, with Python's `\s` / `\S` spelled
 * out.  JavaScript's `\s` matches U+FEFF (Python's does not) and misses
 * U+001C-U+001F and U+0085 (Python's match them), and `_render_map` writes the
 * server's terrain name into `# terrain …` unsanitized — a name holding a
 * newline plus U+001F therefore forges a line that CPython reads as a grid row
 * and `\s*` would not.
 */
export const MAP_ROW_RE = new RegExp(`^[${PY_SPACE_CLASS}]*(-?\\d+) \\|(.*)$`, 'u');
export const MAP_TERRAIN_RE = /^# terrain (.*)$/;
export const MAP_LEGEND_PAIR_RE = new RegExp(`^([^${PY_SPACE_CLASS}])=(.+)$`, 'u');
export const LEGEND_SEPARATOR = ' · ';

const codePointSort = (left: string, right: string): number =>
  left < right ? -1 : left > right ? 1 : 0;

/**
 * `_terrain_chars` — assign one stable character per terrain name.
 *
 * `existing` carries the codes the mirror already used for this map, so a
 * merged grid never changes the meaning of a character mid-file.
 */
export const terrainChars = (
  names: ReadonlyArray<string>,
  existing?: ReadonlyMap<string, string> | null
): ReadonlyMap<string, string> => {
  const assigned = new Map(existing ?? []);
  const used = new Set(assigned.values());
  const unique = [...new Set(names)].sort(codePointSort);
  for (const name of unique) {
    const char = TERRAIN_CHARS.get(name);
    if (char !== undefined && !used.has(char)) {
      assigned.set(name, char);
      used.add(char);
    }
  }
  for (const name of unique) {
    if (assigned.has(name)) continue;
    const initials = name
      .split(/[^A-Za-z]+/)
      .filter((part) => part !== '')
      .map((part) => (part[0] ?? '').toUpperCase());
    const candidate = [...initials, ...TERRAIN_FALLBACK].find((char) => !used.has(char));
    // The alphabet outlasts a ruleset, so the '*' branch is unreachable.
    assigned.set(name, candidate ?? '*');
    used.add(candidate ?? '*');
  }
  return assigned;
};

// ---------------------------------------------------------------------------
// The parsed grid
// ---------------------------------------------------------------------------

/** The grid key CPython spells as the tuple `(x, y)`. */
export const tileKey = (x: number, y: number): string => `${x},${y}`;

/** Read a {@link tileKey} back. Returns `null` for anything else. */
export const parseTileKey = (key: string): readonly [number, number] | null => {
  const match = /^(-?\d+),(-?\d+)$/.exec(key);
  return match === null ? null : [Number(match[1]), Number(match[2])];
};

/** `_Map` — a parsed `map.txt`: its revision, terrain legend and grid. */
export interface MirrorMap {
  readonly revision: MirrorRevision | null;
  /** Terrain name → its one-character code, exactly `_terrain_chars`' shape. */
  readonly legend: ReadonlyMap<string, string>;
  /** {@link tileKey} → the character that tile renders as. */
  readonly grid: ReadonlyMap<string, string>;
}

/** `_Map.known` — how many tiles in the grid are not `?`. */
export const knownTiles = (map: MirrorMap): number =>
  [...map.grid.values()].filter((char) => char !== '?').length;

/** `_parse_map` — read `state/map.txt` back into a grid the writer can merge. */
export const parseMap = (text: string | null | undefined): MirrorMap => {
  const grid = new Map<string, string>();
  const legend = new Map<string, string>();
  if (text === null || text === undefined || text === '') {
    return { revision: null, legend, grid };
  }
  const lines = splitLines(text);
  const revision = parseRevLine(lines[0]);
  const originX = lines.reduce<number | null>((found, line) => {
    if (found !== null) return found;
    const window = MAP_WINDOW_RE.exec(line);
    return window === null ? null : Number(window[1]);
  }, null);
  for (const line of lines) {
    const terrain = MAP_TERRAIN_RE.exec(line);
    if (terrain === null) continue;
    for (const part of (terrain[1] ?? '').split(LEGEND_SEPARATOR)) {
      // `strip`, never `.trim()` — a terrain name carrying U+FEFF must keep it,
      // or the legend loses continuity and `_render_map` reassigns its code.
      const entry = MAP_LEGEND_PAIR_RE.exec(strip(part));
      if (entry !== null && entry[1] !== undefined && entry[2] !== undefined) {
        legend.set(entry[2], entry[1]);
      }
    }
  }
  if (originX === null) return { revision, legend, grid };
  for (const line of lines) {
    const row = MAP_ROW_RE.exec(line);
    if (row === null) continue;
    const y = Number(row[1]);
    [...(row[2] ?? '')].forEach((char, offset) => {
      if (char !== ' ') grid.set(tileKey(originX + offset, y), char);
    });
  }
  return { revision, legend, grid };
};

/** `_map_size` — the `64x64` the overview table recorded, if it has one. */
export const mapSize = (dir: string): Effect.Effect<string | null, never, PrivateFs> =>
  Effect.map(readMirror(dir, OVERVIEW_FILE), (text) => {
    for (const row of parseTable(text).rows) {
      const size = row[1];
      if (row[0] === 'map' && size !== undefined && size !== '-') return size;
    }
    return null;
  });

/** What one tiles page proves: the characters it fixes, plus its legend. */
export interface TileChars {
  readonly grid: ReadonlyMap<string, string>;
  readonly chars: ReadonlyMap<string, string>;
}

/**
 * `_tile_chars` — the `{(x, y): char}` grid one page proves, plus its legend.
 *
 * Fog is decided by visibility alone: a tile the seat has never seen renders
 * `?` even if the payload volunteered a terrain for it.
 */
export const tileChars = (
  items: ReadonlyArray<unknown>,
  legend: ReadonlyMap<string, string>
): Effect.Effect<TileChars, PlayerError> =>
  Effect.suspend(() => {
    const names: string[] = [];
    for (const item of items) {
      if (!isJsonObject(item)) {
        return Effect.fail(mirrorError('tile item is not an object'));
      }
      if (dig(item, 'visibility') !== 'unknown') {
        const terrain = dig(item, 'terrain');
        if (typeof terrain === 'string') names.push(terrain);
      }
    }
    const chars = terrainChars(names, legend);
    const grid = new Map<string, string>();
    for (const item of items) {
      const x = dig(item, 'x');
      const y = dig(item, 'y');
      if (!Number.isInteger(x) || !Number.isInteger(y)) {
        return Effect.fail(mirrorError('tile item carries no coordinates'));
      }
      const key = tileKey(x as number, y as number);
      const visibility = dig(item, 'visibility');
      const terrain = dig(item, 'terrain');
      if (visibility === 'unknown' || typeof terrain !== 'string') {
        grid.set(key, '?');
        continue;
      }
      const char = chars.get(terrain) ?? '?';
      grid.set(key, visibility === 'visible' ? char : char.toLowerCase());
    }
    return Effect.succeed({ grid, chars });
  });
