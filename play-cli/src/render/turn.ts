/**
 * The turn briefing — the agent's per-turn home screen.
 *
 * Ports `play/client.py` 5552-5770:
 *
 * - 5552-5575 `_unit_status`
 * - 5577-5604 `_briefing_unit_lines`
 * - 5606-5640 `_briefing_needs_decision`
 * - 5642-5738 `_render_turn`
 * - 5740-5756 `_researchable_names`
 * - 5758-5770 `_briefing_truncation`
 *
 * Nothing here fetches.  `turn` (and `do --end --await --brief`, through U16)
 * builds one {@link TurnResult} and hands it to {@link renderTurn}, so the
 * briefing an agent reads after a phase end is byte-identical to the one bare
 * `turn` prints.
 *
 * The state-page renderers this leans on (`_economy_text`, `_research_text`,
 * `_score_text`, `_route_summary`, `_render_section_items`, `_render_tiles`)
 * belong to U10, so they arrive as {@link RenderTurnDeps} rather than as
 * imports — see NOTES §U12.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { phaseText } from 'src/render/phase';
import {
  coordinates,
  drift,
  isJsonObject,
  named,
  needText,
  revisionLabel,
  rowAlias,
  scalar,
  type AliasMap,
  type JsonObject,
  type JsonValue,
} from 'src/render/primitives';
import type { PhaseBlock } from 'src/schema/health';
import type { Revision } from 'src/schema/revision';
import type { TurnHealthContext } from 'src/services/health-context';

// ---------------------------------------------------------------------------
// The result shape `turn` builds and `--json` prints verbatim
// ---------------------------------------------------------------------------

/** `_turn_compact_page` — one section of the briefing, already windowed. */
export interface TurnCompactPage {
  readonly shown: number;
  readonly total: number;
  readonly truncated: boolean;
  readonly items: ReadonlyArray<JsonValue>;
  readonly next_cursor: string | null;
  readonly cursor_expires_at: string | null;
}

export interface TurnReadyResult {
  readonly schema_version: 1;
  readonly command: 'turn';
  readonly status: 'ready';
  readonly context: TurnHealthContext;
  readonly state_revision: Revision;
  readonly overview: JsonValue;
  readonly cities: TurnCompactPage;
  readonly units: TurnCompactPage;
  readonly research: TurnCompactPage;
  readonly next_commands: ReadonlyArray<string>;
}

/** The three non-`ready` shapes: `terminal`, `lobby`, `not_ready`. */
export interface TurnStatusResult {
  readonly schema_version: 1;
  readonly command: 'turn';
  readonly status: 'terminal' | 'lobby' | 'not_ready';
  readonly context: TurnHealthContext;
  readonly next_commands: ReadonlyArray<string>;
}

export type TurnResult = TurnReadyResult | TurnStatusResult;

export const isTurnReady = (result: TurnResult): result is TurnReadyResult =>
  result.status === 'ready';

// ---------------------------------------------------------------------------
// The U10 seam
// ---------------------------------------------------------------------------

/**
 * The live-state renderers the briefing reuses, injected.
 *
 * U10 owns every one of them (`src/render/state/*`), and it lands in wave 2
 * alongside this unit.  Taking them as functions is what lets `turn` be written,
 * tested and reviewed without waiting on `state` — and it is the same shape U05
 * used for its own cross-unit hooks.
 */
export interface RenderTurnDeps {
  /** `_route_summary` (client.py:4249-4273). */
  readonly routeSummary: (route: JsonValue) => Effect.Effect<string, PlayerError>;
  /** `_economy_text` (client.py:4545-4569). */
  readonly economyText: (player: JsonValue) => Effect.Effect<string, PlayerError>;
  /** `_research_text` (client.py:4570-4589). */
  readonly researchText: (research: JsonValue) => Effect.Effect<string, PlayerError>;
  /** `_score_text` (client.py:4590-4619). */
  readonly scoreText: (score: JsonValue) => string;
  /** `_render_section_items` (client.py:5159-5169). */
  readonly renderSectionItems: (
    section: string,
    items: ReadonlyArray<JsonValue>,
    aliases: AliasMap | null
  ) => Effect.Effect<ReadonlyArray<string>, PlayerError>;
  /** `_render_tiles` (client.py:4479-4544). */
  readonly renderTiles: (
    items: ReadonlyArray<JsonValue>,
    aliases: AliasMap | null
  ) => Effect.Effect<ReadonlyArray<string>, PlayerError>;
}

// ---------------------------------------------------------------------------
// _unit_status (client.py:5552-5575)
// ---------------------------------------------------------------------------

const field = (item: JsonValue, key: string): JsonValue =>
  isJsonObject(item) ? (item[key] ?? null) : null;

const hasKey = (item: JsonValue, key: string): boolean => isJsonObject(item) && key in item;

/** What one unit is doing, in the words the units table already uses. */
export const unitStatus = (
  item: JsonValue,
  deps: RenderTurnDeps
): Effect.Effect<string, PlayerError> =>
  Effect.gen(function* () {
    const activity = field(item, 'activity');
    const activityName = isJsonObject(activity) ? activity['name'] : undefined;
    let status = typeof activityName === 'string' ? activityName : 'unknown';

    const summary = yield* deps.routeSummary(field(item, 'route'));
    if (summary !== '') status += ` ${summary}`;

    const automation = field(item, 'automation');
    if (isJsonObject(automation)) {
      const controller = automation['controller'] ?? null;
      if (controller !== null && controller !== 'player') {
        status += ` !controller=${scalar(controller)}`;
      }
    }

    if (hasKey(item, 'moves')) {
      const stats = field(item, 'type_stats');
      const rate =
        isJsonObject(stats) && 'move_rate' in stats ? scalar(stats['move_rate'] ?? null) : '?';
      status += ` mv${scalar(field(item, 'moves'))}/${rate}`;
    }
    return status;
  });

// ---------------------------------------------------------------------------
// _briefing_unit_lines (client.py:5577-5604)
// ---------------------------------------------------------------------------

const UNIT_LINE_KEYS = ['id', 'type', 'x', 'y'] as const;

/** Group units that share a type, tile and status onto one line. */
export const briefingUnitLines = (
  items: ReadonlyArray<JsonValue>,
  aliases: AliasMap | null,
  deps: RenderTurnDeps
): Effect.Effect<ReadonlyArray<string>, PlayerError> =>
  Effect.gen(function* () {
    // The same precondition `_STATE_RENDERERS["units"]` declares: a grouped line
    // names each unit by alias, so an item without an ID cannot be one.
    const groupable = items.every(
      (item) => isJsonObject(item) && UNIT_LINE_KEYS.every((key) => key in item)
    );
    if (!groupable) {
      const rendered = yield* deps.renderSectionItems('units', items, aliases);
      return rendered.map((line) => `  ${line}`);
    }

    const groups = new Map<string, { readonly parts: readonly [string, string, string]; readonly aliases: string[] }>();
    for (const [offset, item] of items.entries()) {
      const unitType = yield* needText(item, 'type', 'unit');
      const position = (yield* coordinates(item)) ?? '@?';
      const status = yield* unitStatus(item, deps);
      const key = JSON.stringify([unitType, position, status]);
      const alias = yield* rowAlias(aliases, item, 'id', 'u', offset + 1);
      const found = groups.get(key);
      if (found === undefined) {
        groups.set(key, { parts: [unitType, position, status], aliases: [alias] });
      } else {
        found.aliases.push(alias);
      }
    }
    return [...groups.values()].map(
      (group) => `  ${group.aliases.join(',')} ${group.parts[0]} ${group.parts[1]} ${group.parts[2]}`
    );
  });

// ---------------------------------------------------------------------------
// _briefing_needs_decision (client.py:5606-5640)
// ---------------------------------------------------------------------------

/** The one-call ending every "nothing left to do" line teaches. */
export const V2_ONE_CALL_END = 'just turn --end --await --brief';

export interface NeedsDecisionOptions {
  /** A truncated briefing has not seen every unit or city; say so. */
  readonly partial?: boolean | undefined;
}

/** CPython's truthiness, for the one value this file tests it on. */
const truthy = (value: JsonValue | undefined): boolean => {
  if (value === undefined || value === null || value === false) return false;
  if (value === '' || value === 0) return false;
  if (Array.isArray(value)) return value.length > 0;
  if (isJsonObject(value)) return Object.keys(value).length > 0;
  return true;
};

export const briefingNeedsDecision = (
  overview: JsonValue,
  units: ReadonlyArray<JsonValue>,
  cities: ReadonlyArray<JsonValue>,
  options: NeedsDecisionOptions = {}
): string => {
  const notes: string[] = [];
  const idle = units.filter((item) => {
    if (!isJsonObject(item)) return false;
    const automation = item['automation'];
    const activity = item['activity'];
    const hasOrders = isJsonObject(automation) ? automation['has_orders'] === true : false;
    const idleActivity = isJsonObject(activity) && activity['name'] === 'idle';
    return idleActivity && !hasOrders;
  }).length;
  if (idle > 0) notes.push(`${idle} idle unit(s)`);

  const research = field(overview, 'research');
  if (isJsonObject(research) && !truthy(research['target'])) notes.push('no research target');
  for (const item of cities) {
    // `dict.get` returns None for an absent key too, so a city page that never
    // carried `production` counts exactly like one that carried a null.
    if (isJsonObject(item) && (item['production'] ?? null) === null) {
      notes.push(`${named(item)} has no production`);
    }
  }
  if (notes.length === 0) notes.push(`nothing idle; ${V2_ONE_CALL_END}`);
  return (
    `needs decision: ${notes.join(', ')}` +
    // The count is honest about its own window: a truncated briefing has not
    // seen every unit or city, so it never claims to be the empire's complete
    // decision list.
    (options.partial === true ? ' (counted over the shown page only)' : '')
  );
};

// ---------------------------------------------------------------------------
// _researchable_names (client.py:5740-5756)
// ---------------------------------------------------------------------------

/** Name the technologies this seat could target right now. */
export const researchableNames = (items: ReadonlyArray<JsonValue>): ReadonlyArray<string> => {
  const names: string[] = [];
  for (const item of items) {
    if (!isJsonObject(item)) continue;
    if (item['can_target'] !== true) continue;
    const name = item['name'];
    if (typeof name !== 'string' || name === '') continue;
    const cost = item['path_cost'] ?? null;
    names.push(cost === null ? name : `${name}/${scalar(cost)}`);
  }
  return names;
};

// ---------------------------------------------------------------------------
// _briefing_truncation (client.py:5758-5770)
// ---------------------------------------------------------------------------

const TRUNCATION_SECTIONS = ['units', 'cities', 'research'] as const;

/** Name the exact continuation for every truncated briefing section. */
export const briefingTruncation = (result: TurnReadyResult): ReadonlyArray<string> => {
  const lines: string[] = [];
  for (const section of TRUNCATION_SECTIONS) {
    const page = result[section];
    if (!page.truncated) continue;
    const cursor = page.next_cursor;
    if (typeof cursor !== 'string' || cursor === '') continue;
    lines.push(`next: just state --cursor ${cursor}   # rest of ${section}`);
  }
  return lines;
};

// ---------------------------------------------------------------------------
// _render_turn (client.py:5642-5738)
// ---------------------------------------------------------------------------

export interface RenderTurnOptions {
  /** A tiles page, when the caller fetched one.  `turn` never does. */
  readonly tiles?: TurnCompactPage | null | undefined;
  readonly aliases?: AliasMap | null | undefined;
  /** U12's own decision projection, already rendered. */
  readonly decisions?: ReadonlyArray<string> | undefined;
  /** The typed-event line, empty when nothing arrived. */
  readonly events?: string | undefined;
}

const pageLine = (label: string, page: TurnCompactPage): string =>
  `${label} ${page.shown}/${page.total}${page.truncated ? ' (truncated)' : ''}`;

/** Render one whole turn briefing. */
export const renderTurn = (
  result: TurnResult,
  deps: RenderTurnDeps,
  options: RenderTurnOptions = {}
): Effect.Effect<ReadonlyArray<string>, PlayerError> =>
  Effect.gen(function* () {
    const context = result.context;
    const phase: PhaseBlock | null = context.phase;
    if (!isTurnReady(result)) {
      return [
        `turn ${result.status} | ${context.game_state} | ` +
          `${phaseText(phase)} | ` +
          `obs ${context.observation_available ? 'yes' : 'no'} ` +
          `legal ${context.legal_actions_available ? 'yes' : 'no'}`,
        ...result.next_commands.map((command) => `next: ${command}`),
      ];
    }

    const overview = result.overview;
    if (!isJsonObject(overview) || !('turn' in overview)) {
      return yield* Effect.fail(drift('turn briefing overview'));
    }
    const aliases = options.aliases ?? null;
    const header = [
      `T${scalar(overview['turn'] ?? null)} ${revisionLabel(result.state_revision)}`,
      context.game_state,
      phaseText(phase),
    ];
    if (context.turns_remaining !== null && context.turns_remaining !== undefined) {
      header.push(`${scalar(context.turns_remaining)} turns left`);
    }
    // The number this seat is graded on belongs on the line it reads first: a
    // decision cannot be credited to an objective it never sees.
    const score = deps.scoreText(overview['score'] ?? null);
    if (score !== '') header.push(score);

    const lines: string[] = [header.join(' | ')];
    lines.push(
      `${yield* deps.economyText(overview['player'] ?? null)} | ` +
        `${yield* deps.researchText(overview['research'] ?? null)}`
    );

    const cities = result.cities;
    const units = result.units;
    const research = result.research;

    lines.push(pageLine('units', units));
    lines.push(...(yield* briefingUnitLines(units.items, aliases, deps)));
    lines.push(pageLine('cities', cities));
    if (cities.items.length > 0) {
      const rendered = yield* deps.renderSectionItems('cities', cities.items, aliases);
      lines.push(...rendered.map((line) => `  ${line}`));
    }
    lines.push(pageLine('research page', research));
    // The briefing already paid for this page on the wire, so it shows the
    // names an agent needs to pick a target instead of discarding them.
    const researchable = researchableNames(research.items);
    if (researchable.length > 0) lines.push(`  researchable: ${researchable.join(' ')}`);

    const tiles = options.tiles ?? null;
    if (tiles !== null && tiles.items.length > 0) {
      lines.push('terrain');
      const rendered = yield* deps.renderTiles(tiles.items, aliases);
      lines.push(...rendered.map((line) => `  ${line}`));
    }

    const truncated = briefingTruncation(result);
    lines.push(
      briefingNeedsDecision(overview, units.items, cities.items, {
        partial: truncated.length > 0,
      })
    );
    // One line per actor that still needs orders, carrying the option aliases
    // `do` accepts.  It is read from local caches only.
    lines.push(...(options.decisions ?? []));
    const events = options.events ?? '';
    if (events !== '') lines.push(events);
    // A truncated section is never a dead end: the continuation cursor is the
    // only way to reach the rest, so it is printed, not hidden behind --json.
    lines.push(...truncated);
    return lines;
  });

export type { JsonObject };
