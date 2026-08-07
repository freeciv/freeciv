/**
 * `state/map.txt` — the ASCII terrain grid the seat has proven.
 *
 * Ports `play/state_mirror.py:854-892` (`_render_map`).
 *
 * Every character here is byte-diff surface, so the shape is deliberately
 * literal:
 *
 * * The grid is `prior` merged under the page's own tiles, so a window can only
 *   ever add to the board — a tiles page is a window, never the whole map.
 * * The bounding box is the *merged* grid's, and a coordinate inside it that no
 *   page has proven renders a **space**, not a `?`.  `?` means "this seat has
 *   looked and seen nothing"; a space means "no page has covered this square".
 * * Row lines are **not** right-stripped: a trailing unproven column keeps its
 *   space so column N of every row lines up under the same x.
 * * The terrain legend is ordered by terrain *name* (CPython's
 *   `sorted(legend.items())`), filtered to the codes the grid actually shows,
 *   and the filter compares uppercase, so a remembered `d` still prints
 *   `D=Desert`.
 *
 * The character assignment itself is U04's `tileChars`/`terrainChars`: it is
 * seeded with the legend already on disk so a merged grid never changes the
 * meaning of a character mid-file.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import type { PrivateFs } from 'src/services/private-fs';
import {
  LEGEND_SEPARATOR,
  knownTiles,
  mapSize,
  parseTileKey,
  revLine,
  tileChars,
  tileKey,
  type MirrorMap,
  type MirrorRevision,
} from 'src/services/mirror';

/** The fixed third header line, verbatim from `state_mirror.py:883`. */
export const MAP_LEGEND_LINE =
  "# legend '?'=never seen · UPPERCASE=visible now · lowercase=remembered";

/** The whole body of an empty grid, verbatim from `state_mirror.py:864`. */
export const MAP_EMPTY_NOTE = '# no tiles known yet';

/** CPython compares strings by code point; JS `<` is close enough for ASCII. */
const codePointSort = (left: string, right: string): number =>
  left < right ? -1 : left > right ? 1 : 0;

interface Bounds {
  readonly minX: number;
  readonly maxX: number;
  readonly minY: number;
  readonly maxY: number;
}

/** The merged grid's bounding box, `min`/`max` without a spread. */
const boundsOf = (grid: ReadonlyMap<string, string>): Bounds | null =>
  [...grid.keys()].reduce<Bounds | null>((box, key) => {
    const point = parseTileKey(key);
    if (point === null) return box;
    const [x, y] = point;
    return box === null
      ? { minX: x, maxX: x, minY: y, maxY: y }
      : {
          minX: Math.min(box.minX, x),
          maxX: Math.max(box.maxX, x),
          minY: Math.min(box.minY, y),
          maxY: Math.max(box.maxY, y),
        };
  }, null);

/**
 * `# terrain D=Desert · O=Ocean` — the legend, name-ordered and filtered to the
 * codes this grid shows.  Empty when nothing in the grid carries a code, in
 * which case `_render_map` omits the line entirely.
 */
export const terrainLegendLine = (
  legend: ReadonlyMap<string, string>,
  grid: ReadonlyMap<string, string>
): string => {
  const seen = new Set([...grid.values()].map((char) => char.toUpperCase()));
  return [...legend]
    .sort(([left], [right]) => codePointSort(left, right))
    .filter(([, char]) => seen.has(char))
    .map(([name, char]) => `${char}=${name}`)
    .join(LEGEND_SEPARATOR);
};

/**
 * `_render_map` — the full text of `state/map.txt` for one tiles page merged
 * over the grid the mirror already holds.
 *
 * Reads `state/overview.tsv` for the board size, which is why it needs the
 * mirror directory and the filesystem: an overview page the seat has not
 * fetched yet renders `size unknown` rather than inventing a dimension.
 */
export const renderMap = (
  dir: string,
  revision: MirrorRevision,
  prior: MirrorMap,
  items: ReadonlyArray<unknown>
): Effect.Effect<string, PlayerError, PrivateFs> =>
  Effect.gen(function* () {
    const fresh = yield* tileChars(items, prior.legend);
    const grid = new Map(prior.grid);
    for (const [key, char] of fresh.grid) grid.set(key, char);
    const bounds = boundsOf(grid);
    if (bounds === null) return `${revLine(revision)}\n${MAP_EMPTY_NOTE}\n`;
    const legend = terrainLegendLine(fresh.chars, grid);
    const size = yield* mapSize(dir);
    const known = knownTiles({ revision, legend: fresh.chars, grid });
    const rows: string[] = [];
    for (let y = bounds.minY; y <= bounds.maxY; y += 1) {
      const cells: string[] = [];
      for (let x = bounds.minX; x <= bounds.maxX; x += 1) {
        cells.push(grid.get(tileKey(x, y)) ?? ' ');
      }
      rows.push(`${String(y).padStart(5)} |${cells.join('')}`);
    }
    return `${[
      revLine(revision),
      // CPython's `size or "size unknown"`: an empty cell is falsy too.
      `# map ${size === null || size === '' ? 'size unknown' : size} · ` +
        `${known} of ${grid.size} tiles known`,
      `# window x ${bounds.minX}..${bounds.maxX} y ${bounds.minY}..${bounds.maxY}`,
      MAP_LEGEND_LINE,
      ...(legend === '' ? [] : [`# terrain ${legend}`]),
      ...rows,
    ].join('\n')}\n`;
  });
