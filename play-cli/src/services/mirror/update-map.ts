/**
 * Project a `known_tiles` / `map_tiles` / `tile_window` page into
 * `state/map.txt`.
 *
 * Ports `play/state_mirror.py:1300-1321` (`_update_map`).
 *
 * The delta entry is derived by re-parsing the text that was just written
 * rather than by counting the page's own items: the page is a window merged
 * over a grid, so "how many tiles do I now know" is only answerable from the
 * merged file.  A page older than the file on disk is ignored outright, which
 * is what keeps a mirror file from ever walking backwards.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { renderMap } from 'src/render/mirror/map';
import type { PrivateFs } from 'src/services/private-fs';
import {
  MAP_FILE,
  isStale,
  knownTiles,
  parseMap,
  readMirror,
  updateDelta,
  writeMirror,
  type MirrorRevision,
} from 'src/services/mirror';

/** The digest title `_update_map` files its one entry under. */
export const MAP_DELTA_TITLE = 'map';

/**
 * `_update_map` — returns the files written, in CPython's order
 * (`state/map.txt` first, `state/delta.md` when the digest moved), or nothing
 * at all when the page is older than the mirror.
 */
export const updateMap = (
  dir: string,
  command: string,
  revision: MirrorRevision,
  items: ReadonlyArray<unknown>
): Effect.Effect<ReadonlyArray<string>, PlayerError, PrivateFs> =>
  Effect.gen(function* () {
    const prior = parseMap(yield* readMirror(dir, MAP_FILE));
    if (isStale(revision, prior.revision)) return [];
    const knownBefore = knownTiles(prior);
    const text = yield* renderMap(dir, revision, prior, items);
    const written = [yield* writeMirror(dir, MAP_FILE, text)];
    const knownAfter = knownTiles(parseMap(text));
    const entries =
      knownAfter === knownBefore
        ? []
        : [`terrain known: ${knownBefore} -> ${knownAfter} tiles`];
    const delta = yield* updateDelta(dir, revision, command, MAP_DELTA_TITLE, entries);
    return delta === null ? written : [...written, delta];
  });
