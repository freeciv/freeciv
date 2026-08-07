/**
 * The `state` page renderer and its section dispatch.
 *
 * Ports `_STATE_RENDERERS` (client.py:5068-5084), `_render_section_items`
 * (5159-5168) and `_render_state_page` (5303-5332).
 *
 * A section keeps its bespoke renderer only while every item on the page
 * carries the keys that renderer reads; one item short of the contract and the
 * whole page falls through to the generic flattener rather than half-rendering.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { V2_BUILD_CHOICE_KEYS } from 'src/constants';
import {
  isJsonObject,
  pageStatus,
  revisionLabel,
  type AliasMap,
  type JsonValue,
} from 'src/render/primitives';
import { renderCityBuildChoices, cityBuildStock } from 'src/render/state/build-choices';
import { renderCities } from 'src/render/state/cities';
import { renderCityDetail } from 'src/render/state/city-detail';
import { renderCityCitizens } from 'src/render/state/citizens';
import { renderDiplomacy } from 'src/render/state/diplomacy';
import { renderGenericItems } from 'src/render/state/generic';
import { governmentScopeLines } from 'src/render/state/government';
import { renderOverview } from 'src/render/state/overview';
import { renderResearch } from 'src/render/state/research';
import { renderTiles } from 'src/render/state/tiles';
import { renderUnits } from 'src/render/state/units';
import type { PageBody } from 'src/schema/page';
import type { Revision } from 'src/schema/revision';
import { PrivateFs } from 'src/services/private-fs';
import type { V2ClientState } from 'src/services/session-store';

export type SectionRenderer = (
  items: ReadonlyArray<JsonValue>,
  aliases: AliasMap | null
) => Effect.Effect<ReadonlyArray<string>, PlayerError>;

interface SectionEntry {
  readonly required: ReadonlyArray<string>;
  readonly render: SectionRenderer;
}

const TILE_KEYS = ['id', 'x', 'y', 'visibility'] as const;

export const STATE_RENDERERS: ReadonlyMap<string, SectionEntry> = new Map([
  ['units', { required: ['id', 'type', 'x', 'y'], render: renderUnits }],
  ['cities', { required: ['id', 'name', 'x', 'y', 'size'], render: renderCities }],
  ['research', { required: ['id', 'name', 'state'], render: renderResearch }],
  ['tile_window', { required: TILE_KEYS, render: renderTiles }],
  ['known_tiles', { required: TILE_KEYS, render: renderTiles }],
  ['map_tiles', { required: TILE_KEYS, render: renderTiles }],
  ['overview', { required: ['turn', 'player', 'research', 'counts'], render: renderOverview }],
  [
    'city_detail',
    {
      required: ['id', 'name', 'size', 'citizens', 'food_storage', 'outputs'],
      render: renderCityDetail,
    },
  ],
  ['city_citizens', { required: ['city_id', 'kind', 'yields'], render: renderCityCitizens }],
  [
    'diplomacy',
    { required: ['relation_id', 'player_name', 'state'], render: renderDiplomacy },
  ],
]);

const covers = (items: ReadonlyArray<JsonValue>, keys: ReadonlyArray<string>): boolean =>
  items.every((item) => isJsonObject(item) && keys.every((key) => key in item));

export const renderSectionItems = (
  section: string,
  items: ReadonlyArray<JsonValue>,
  aliases: AliasMap | null = null
): Effect.Effect<ReadonlyArray<string>, PlayerError> => {
  const entry = STATE_RENDERERS.get(section);
  if (entry !== undefined && covers(items, entry.required)) {
    return entry.render(items, aliases);
  }
  return Effect.succeed(renderGenericItems(items));
};

/** The shape `_render_state_page` reads: a validated page envelope, or its twin. */
export interface StatePageValue {
  readonly state_revision: Revision;
  readonly page: PageBody<JsonValue>;
}

export interface StatePageOptions {
  readonly aliases?: AliasMap | null;
  readonly sessionPath?: string | null;
  readonly state?: V2ClientState | null;
}

export const renderStatePage = (
  value: StatePageValue,
  options: StatePageOptions = {}
): Effect.Effect<ReadonlyArray<string>, PlayerError, PrivateFs> =>
  Effect.gen(function* () {
    const aliases = options.aliases ?? null;
    const sessionPath = options.sessionPath ?? null;
    const state = options.state ?? null;
    const page = value.page;
    const section = page.section;
    const lines: string[] = [
      `${revisionLabel(value.state_revision)} ${section} ${pageStatus(page)}`,
    ];
    const items = page.items;
    if (items.length === 0) {
      lines.push(`(no ${section} items on this page)`);
      return lines;
    }
    if (section === 'city_build_choices' && covers(items, V2_BUILD_CHOICE_KEYS)) {
      const stock = yield* cityBuildStock({
        sessionPath,
        revision: value.state_revision,
        items,
        aliases,
      });
      lines.push(...(yield* renderCityBuildChoices(items, aliases, stock)));
      return lines;
    }
    lines.push(...(yield* renderSectionItems(section, items, aliases)));
    if (section === 'governments') {
      lines.push(...(yield* governmentScopeLines(items, state)));
    }
    return lines;
  });
