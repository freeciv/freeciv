/**
 * Opaque-ID shape predicates.
 *
 * The regexes live in `src/constants.ts`; this module is the typed vocabulary
 * every other decoder and every alias table reads them through, so no unit
 * re-derives "is this an actor ID" from a raw pattern.
 */
import {
  ACTOR_ID_RE,
  ALIAS_ENTITY_TYPES,
  CATALOG_RE,
  CITY_ID_RE,
  CONTROLLER_RE,
  CURSOR_RE,
  ENTITY_ALIAS_RE,
  GAME_ID_RE,
  OPAQUE_ID_RE,
  RELATION_ID_RE,
  TILE_ID_RE,
} from 'src/constants';

export type ActorType = 'player' | 'city' | 'unit';
export const ACTOR_TYPES: ReadonlySet<string> = new Set<ActorType>(['player', 'city', 'unit']);

export const isOpaqueId = (value: unknown): value is string =>
  typeof value === 'string' && OPAQUE_ID_RE.test(value);

export const isGameId = (value: unknown): value is string =>
  typeof value === 'string' && GAME_ID_RE.test(value);

export const isControllerName = (value: unknown): value is string =>
  typeof value === 'string' && CONTROLLER_RE.test(value);

export const isActorId = (value: unknown): value is string =>
  typeof value === 'string' && ACTOR_ID_RE.test(value);

export const isRelationId = (value: unknown): value is string =>
  typeof value === 'string' && RELATION_ID_RE.test(value);

export const isTileId = (value: unknown): value is string =>
  typeof value === 'string' && TILE_ID_RE.test(value);

export const isCityId = (value: unknown): value is string =>
  typeof value === 'string' && CITY_ID_RE.test(value);

export const isCursor = (value: unknown): value is string =>
  typeof value === 'string' && CURSOR_RE.test(value);

export const isCatalogId = (value: unknown): value is string =>
  typeof value === 'string' && CATALOG_RE.test(value);

/** `"unit_0123…"` → `"unit"`. Python spells this `actor_id.split("_", 1)[0]`. */
export const idPrefix = (identifier: string): string => {
  const cut = identifier.indexOf('_');
  return cut < 0 ? identifier : identifier.slice(0, cut);
};

export const isActorType = (value: unknown): value is ActorType =>
  typeof value === 'string' && ACTOR_TYPES.has(value);

/**
 * Report whether a stored entity alias still names its own ID type.
 * Ports `_entity_alias_id_matches` (client.py:1625-1634).
 */
export const entityAliasIdMatches = (alias: string, identifier: unknown): boolean => {
  const match = ENTITY_ALIAS_RE.exec(alias);
  if (match === null || typeof identifier !== 'string') {
    return false;
  }
  const prefix = match[1];
  if (prefix === undefined) {
    return false;
  }
  const kind = ALIAS_ENTITY_TYPES[prefix];
  if (kind === undefined) {
    return false;
  }
  const pattern = kind === 'relation' ? RELATION_ID_RE : ACTOR_ID_RE;
  return pattern.test(identifier) && identifier.startsWith(`${kind}_`);
};
