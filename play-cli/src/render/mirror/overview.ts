/**
 * `state/overview.tsv` — economy, research, government, one row per fact.
 *
 * Ports `play/state_mirror.py:441-526` (`_render_overview`).
 *
 * This is the only section that projects *one* item into *many* rows, which is
 * why `update_from_page` counts its page note in items rather than rows and why
 * it is in `_REPLACE_ONLY`: a second overview page is a fresh answer to the same
 * question, never a window onto a longer list.
 *
 * A fact the payload did not carry is omitted, never defaulted — `add` writes a
 * row only for a key the payload actually held.  A carried `null` is different
 * from an absent key and stays visible (`player (none yet)`), because "the
 * server says you have no player yet" and "this page did not mention players"
 * are different states of the world.
 */
import { Effect } from 'effect';
import { type PlayerError } from 'src/errors';
import { isJsonObject } from 'src/schema/primitives';
import {
  MISSING,
  cell,
  dig,
  mirrorError,
  pair,
  type MirrorAliases,
} from 'src/services/mirror';
import type { RenderedSection } from 'src/render/mirror/section';

export const OVERVIEW_COLUMNS: ReadonlyArray<string> = ['fact', 'value'];

/** The count keys the projection records, in CPython's order. */
const COUNT_NAMES: ReadonlyArray<string> = [
  'cities',
  'units',
  'known_tiles',
  'legal_actions',
  'chat',
];

/** CPython truthiness, for `score.components`' `if value` filter. */
const truthy = (value: unknown): boolean => {
  if (Array.isArray(value)) return value.length > 0;
  if (isJsonObject(value)) return Object.keys(value).length > 0;
  return Boolean(value);
};

/** `isinstance(value, int) and not isinstance(value, bool)`. */
const isInteger = (value: unknown): value is number =>
  typeof value === 'number' && Number.isInteger(value);

/** `_render_overview` — project the overview item into `fact`/`value` rows. */
export const renderOverview = (
  items: ReadonlyArray<unknown>,
  _aliases?: MirrorAliases | null
): Effect.Effect<RenderedSection, PlayerError> => {
  const item = items[0];
  if (items.length === 0 || item === undefined) {
    return Effect.succeed({ columns: OVERVIEW_COLUMNS, rows: [] });
  }
  if (!isJsonObject(item)) {
    return Effect.fail(mirrorError('overview item is not an object'));
  }
  const rows: Array<ReadonlyArray<string>> = [];
  const add = (name: string, value: unknown): void => {
    if (value !== MISSING) rows.push([name, cell(value)]);
  };

  add('client_state', dig(item, 'client_state'));
  add('turn', dig(item, 'turn'));
  const phase = dig(item, 'phase');
  if (phase !== MISSING) rows.push(['phase', pair(phase, dig(item, 'phase_count'))]);
  add('phase_ready', dig(item, 'phase_ready'));
  const width = dig(item, 'map', 'width');
  if (width !== MISSING) {
    rows.push(['map', pair(width, dig(item, 'map', 'height'), 'x')]);
  }
  add('topology', dig(item, 'map', 'topology'));

  const player = dig(item, 'player');
  if (player === null) {
    rows.push(['player', '(none yet)']);
  } else if (player !== MISSING) {
    add('player', dig(player, 'name'));
    add('nation', dig(player, 'nation'));
    add('government', dig(player, 'government'));
    add('government_status', dig(player, 'government_state', 'status'));
    add('alive', dig(player, 'alive'));
    add('phase_done', dig(player, 'phase_done'));
    add('gold', dig(player, 'economy', 'gold'));
    const tax = dig(player, 'economy', 'tax');
    if (tax !== MISSING) {
      rows.push([
        'rates',
        `tax${cell(tax)} lux${cell(dig(player, 'economy', 'luxury'))} sci${cell(
          dig(player, 'economy', 'science')
        )}`,
      ]);
    }
    add('max_rate', dig(player, 'economy', 'max_rate'));
  }

  const research = dig(item, 'research');
  if (research === null) {
    rows.push(['research', '(none yet)']);
  } else if (research !== MISSING) {
    add('research', dig(research, 'target'));
    const bulbs = dig(research, 'bulbs_researched');
    if (bulbs !== MISSING) rows.push(['bulbs', pair(bulbs, dig(research, 'cost'))]);
    add('research_output', dig(research, 'output'));
    add('research_goal', dig(research, 'goal'));
    add('techs_researched', dig(research, 'techs_researched'));
    add('future_tech', dig(research, 'future_tech'));
  }

  const score = dig(item, 'score');
  if (isJsonObject(score)) {
    // The evaluated objective belongs in the projection the delta digest diffs,
    // so a turn's effect on it shows up as `score 17 -> 19` for free.  `exact`
    // leads when the boundary carries it; otherwise the provable lower bound
    // does, marked so it cannot be misread.
    const exact = dig(score, 'exact');
    add('score', isInteger(exact) ? cell(exact) : `>=${cell(dig(score, 'lower_bound'))}`);
    const components = dig(score, 'components');
    if (isJsonObject(components)) {
      add(
        'score_from',
        Object.entries(components)
          .filter(([, value]) => truthy(value))
          .map(([name, value]) => `${name} ${cell(value)}`)
          .join(' ') || '-'
      );
    }
  }

  const counts = dig(item, 'counts');
  if (isJsonObject(counts)) {
    // `chat` is the typed event feed (tech learned, city growth, huts).
    // Recording its total is what lets the next briefing say how many events
    // arrived without reading the feed itself.
    for (const name of COUNT_NAMES) add(`count_${name}`, dig(counts, name));
  }
  return Effect.succeed({ columns: OVERVIEW_COLUMNS, rows });
};
