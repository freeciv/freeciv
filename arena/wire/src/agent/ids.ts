/**
 * The full-control-v2 identifier alphabet.
 *
 * Clean-room Effect Schema re-expression of `play-cli/src/schema/ids.ts` (the
 * typed vocabulary) plus the patterns it reads from
 * `play-cli/src/constants.ts`, both of which port `play/client.py`:
 *
 * | schema           | pattern                        | Python           |
 * | ---------------- | ------------------------------ | ---------------- |
 * | {@link OpaqueId} | `OPAQUE_ID_RE`                 | `client.py:98`  |
 * | {@link PlayGameId} | `GAME_ID_RE`                 | `client.py:40`   |
 * | {@link ControllerName} | `CONTROLLER_RE`          | `client.py:41`   |
 * | {@link ActorId}  | `ACTOR_ID_RE`                  | `client.py:111`  |
 * | {@link RelationId} | `RELATION_ID_RE`             | `client.py:112`  |
 * | {@link TileId}   | `TILE_ID_RE`                   | `client.py:110`  |
 * | {@link CityId}   | `CITY_ID_RE`                   | `client.py:109`  |
 * | {@link Cursor}   | `CURSOR_RE`                    | `client.py:107`  |
 * | {@link CatalogId} | `CATALOG_RE`                  | `client.py:108`  |
 * | {@link EntityAlias} | `ENTITY_ALIAS_RE`           | `client.py:117`  |
 *
 * Python applies every one of them with `re.fullmatch`.  The patterns are
 * written with explicit `^…$` anchors, and Python's `$` also matches before a
 * trailing newline while `fullmatch` does not — so the anchored form plus
 * `fullmatch` is what makes the two languages agree, and a JavaScript
 * `RegExp.test` on the same anchored pattern is the exact equivalent.
 *
 * ## Branded, and separately branded
 *
 * Every identifier is a branded string.  The point is not decoration: an
 * `ActorId` and a `TileId` are both 32 hex characters behind a prefix, and the
 * only thing stopping one being passed where the other belongs is the brand.
 *
 * ## The two `GAME_ID_RE`s are not the same regex
 *
 * `play/client.py:40` is `^game_[A-Za-z0-9_-]{20,80}$`; the replay gateway's
 * (`agent_eval/replay_gateway.py:35`, `../ids.ts`) is
 * `^[A-Za-z0-9_-]{20,80}$` — no prefix, and the length bound covers the *whole*
 * string.  They agree on the ids in circulation (`game_` + 24 characters is 29,
 * inside both bounds) and disagree at the top of the range: a 78-character
 * agent-side id is 83 characters to the gateway, which refuses it.  Hence
 * {@link PlayGameId} rather than a second `GameId`; converting between the two
 * is a decode, never a cast.
 *
 * @module
 */

import { Schema } from 'effect';
import { decodeTolerant, isTolerant, type TolerantDecoder } from '../tolerant.ts';

// ---------------------------------------------------------------------------
// Patterns — play/client.py:40-41 and 98-128
// ---------------------------------------------------------------------------

/**
 * `OPAQUE_ID_RE` — `play/client.py:98`:
 * `re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")`.
 *
 * The widest identifier shape the client accepts: 1-128 characters, starting
 * alphanumeric.  Every narrower id below is a subset of it, which is why
 * `_opaque` (`client.py:1272-1275`) is the fallback validator for any id whose
 * exact species the client does not need to know.
 */
export const OPAQUE_ID_RE: RegExp = /^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$/;

/** `GAME_ID_RE` — `play/client.py:40`. See the module doc on the two spellings. */
export const PLAY_GAME_ID_RE: RegExp = /^game_[A-Za-z0-9_-]{20,80}$/;

/**
 * `CONTROLLER_RE` — `play/client.py:41`:
 * `re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{2,95}$")` — 3-96 characters.
 *
 * Shape only.  `--name` is held to more than this at join time
 * (`_controller_name`, `client.py:743-755`): it must also contain a `-`, must
 * not start or end with one, and must not casefold to `agent` or
 * `harness-model`.  Those rules police the *operator's* honesty about which
 * model is playing, so they belong to the join command, not to the decoder that
 * reads a label back off the wire.
 */
export const CONTROLLER_RE: RegExp = /^[A-Za-z0-9][A-Za-z0-9._-]{2,95}$/;

/** `CURSOR_RE` — `play/client.py:107`. Opaque pagination cursor. */
export const CURSOR_RE: RegExp = /^cursor_[A-Za-z0-9_-]{32}$/;

/**
 * `CATALOG_RE` — `play/client.py:108`.
 *
 * The 32 characters are the first half of a SHA-256 hex digest over canonical
 * JSON (`_legacy_catalog_id`, `client.py:1419-1423`), so the alphabet is wider
 * than the digest needs — a server that switches to a base64url id stays valid.
 */
export const CATALOG_RE: RegExp = /^catalog_[A-Za-z0-9_-]{32}$/;

/** `CITY_ID_RE` — `play/client.py:109`. Lowercase hex only. */
export const CITY_ID_RE: RegExp = /^city_[0-9a-f]{32}$/;

/** `TILE_ID_RE` — `play/client.py:110`. Lowercase hex only. */
export const TILE_ID_RE: RegExp = /^tile_[0-9a-f]{32}$/;

/**
 * `ACTOR_ID_RE` — `play/client.py:111`:
 * `re.compile(r"^(?:player|city|unit)_[0-9a-f]{32}$")`.
 *
 * The prefix is load-bearing: it is how a bare id announces its own type, which
 * is what {@link idPrefix} and {@link entityAliasIdMatches} read.
 */
export const ACTOR_ID_RE: RegExp = /^(?:player|city|unit)_[0-9a-f]{32}$/;

/** `RELATION_ID_RE` — `play/client.py:112`. Not an actor: relations are edges. */
export const RELATION_ID_RE: RegExp = /^relation_[0-9a-f]{32}$/;

/**
 * `ENTITY_ALIAS_RE` — `play/client.py:117`: `re.compile(r"^([ucpr])([1-9][0-9]{0,3})$")`.
 *
 * A client-local shorthand (`u12`, `c3`) that never leaves the process: every
 * request still carries the server's opaque id.  Numbering starts at 1 and the
 * leading digit cannot be `0`, so one entity has exactly one alias spelling.
 */
export const ENTITY_ALIAS_RE: RegExp = /^([ucpr])([1-9][0-9]{0,3})$/;

// ---------------------------------------------------------------------------
// Identifier schemas
// ---------------------------------------------------------------------------

const idSchema = <B extends string>(
  brand: B,
  pattern: RegExp,
  description: string,
): Schema.brand<Schema.filter<Schema.Schema<string, string>>, B> =>
  Schema.String.pipe(
    Schema.pattern(pattern, { identifier: brand, description }),
    Schema.brand(brand),
  );

/** Any identifier the v2 wire carries: 1-128 chars, alphanumeric-initial. */
export const OpaqueId = idSchema(
  'OpaqueId',
  OPAQUE_ID_RE,
  'Any v2 wire identifier (OPAQUE_ID_RE)',
);
/** Any identifier the v2 wire carries. */
export type OpaqueId = typeof OpaqueId.Type;

/** The agent-side game id: `game_` plus 20-80 id characters. */
export const PlayGameId = idSchema(
  'PlayGameId',
  PLAY_GAME_ID_RE,
  'Agent-side game id: game_ + 20-80 chars (client.py GAME_ID_RE)',
);
/** The agent-side game id. */
export type PlayGameId = typeof PlayGameId.Type;

/** A controller label's *shape*; the truthfulness rules live at join time. */
export const ControllerName = idSchema(
  'ControllerName',
  CONTROLLER_RE,
  'Controller label shape: 3-96 chars (CONTROLLER_RE)',
);
/** A controller label's shape. */
export type ControllerName = typeof ControllerName.Type;

/** A pagination cursor. */
export const Cursor = idSchema('Cursor', CURSOR_RE, 'Pagination cursor (CURSOR_RE)');
/** A pagination cursor. */
export type Cursor = typeof Cursor.Type;

/** A legal-actions catalog id. */
export const CatalogId = idSchema(
  'CatalogId',
  CATALOG_RE,
  'Legal-actions catalog id (CATALOG_RE)',
);
/** A legal-actions catalog id. */
export type CatalogId = typeof CatalogId.Type;

/** A city id. */
export const CityId = idSchema('CityId', CITY_ID_RE, 'City id (CITY_ID_RE)');
/** A city id. */
export type CityId = typeof CityId.Type;

/** A tile id. */
export const TileId = idSchema('TileId', TILE_ID_RE, 'Tile id (TILE_ID_RE)');
/** A tile id. */
export type TileId = typeof TileId.Type;

/** A player, city or unit id. */
export const ActorId = idSchema(
  'ActorId',
  ACTOR_ID_RE,
  'Player, city or unit id (ACTOR_ID_RE)',
);
/** A player, city or unit id. */
export type ActorId = typeof ActorId.Type;

/** A relation id — an edge between actors, never an actor itself. */
export const RelationId = idSchema(
  'RelationId',
  RELATION_ID_RE,
  'Relation id (RELATION_ID_RE)',
);
/** A relation id. */
export type RelationId = typeof RelationId.Type;

/** A client-local entity alias, e.g. `u12`. */
export const EntityAlias = idSchema(
  'EntityAlias',
  ENTITY_ALIAS_RE,
  'Client-local entity alias, e.g. u12 (ENTITY_ALIAS_RE)',
);
/** A client-local entity alias. */
export type EntityAlias = typeof EntityAlias.Type;

/**
 * A controller label as it appears on the wire.
 *
 * Only non-emptiness is checked, matching what `_v2_session` demands of a
 * loaded session (`play/client.py:1070-1071`).  {@link ControllerName} is the
 * stricter shape a *new* label must satisfy.
 */
export const ControllerLabel = Schema.String.pipe(
  Schema.nonEmptyString(),
).annotations({
  identifier: 'ControllerLabel',
  description: 'A non-empty controller label as carried on the wire',
});
/** A controller label as it appears on the wire. */
export type ControllerLabel = typeof ControllerLabel.Type;

// ---------------------------------------------------------------------------
// Decoders
// ---------------------------------------------------------------------------

/** Decode an unknown value as an {@link OpaqueId}. */
export const decodeOpaqueId: TolerantDecoder<OpaqueId> = decodeTolerant(OpaqueId);
/** Decode an unknown value as a {@link PlayGameId}. */
export const decodePlayGameId: TolerantDecoder<PlayGameId> = decodeTolerant(PlayGameId);
/** Decode an unknown value as a {@link ControllerName}. */
export const decodeControllerName: TolerantDecoder<ControllerName> =
  decodeTolerant(ControllerName);
/** Decode an unknown value as a {@link Cursor}. */
export const decodeCursor: TolerantDecoder<Cursor> = decodeTolerant(Cursor);
/** Decode an unknown value as a {@link CatalogId}. */
export const decodeCatalogId: TolerantDecoder<CatalogId> = decodeTolerant(CatalogId);
/** Decode an unknown value as a {@link CityId}. */
export const decodeCityId: TolerantDecoder<CityId> = decodeTolerant(CityId);
/** Decode an unknown value as a {@link TileId}. */
export const decodeTileId: TolerantDecoder<TileId> = decodeTolerant(TileId);
/** Decode an unknown value as an {@link ActorId}. */
export const decodeActorId: TolerantDecoder<ActorId> = decodeTolerant(ActorId);
/** Decode an unknown value as a {@link RelationId}. */
export const decodeRelationId: TolerantDecoder<RelationId> = decodeTolerant(RelationId);
/** Decode an unknown value as an {@link EntityAlias}. */
export const decodeEntityAlias: TolerantDecoder<EntityAlias> = decodeTolerant(EntityAlias);

// ---------------------------------------------------------------------------
// Predicates — play-cli/src/schema/ids.ts:25-59
// ---------------------------------------------------------------------------

/** True when `value` is an {@link OpaqueId}. */
export const isOpaqueId: (value: unknown) => value is OpaqueId = isTolerant(OpaqueId);
/** True when `value` is a {@link PlayGameId}. */
export const isPlayGameId: (value: unknown) => value is PlayGameId = isTolerant(PlayGameId);
/** True when `value` is a well-shaped {@link ControllerName}. */
export const isControllerName: (value: unknown) => value is ControllerName =
  isTolerant(ControllerName);
/** True when `value` is a {@link Cursor}. */
export const isCursor: (value: unknown) => value is Cursor = isTolerant(Cursor);
/** True when `value` is a {@link CatalogId}. */
export const isCatalogId: (value: unknown) => value is CatalogId = isTolerant(CatalogId);
/** True when `value` is a {@link CityId}. */
export const isCityId: (value: unknown) => value is CityId = isTolerant(CityId);
/** True when `value` is a {@link TileId}. */
export const isTileId: (value: unknown) => value is TileId = isTolerant(TileId);
/** True when `value` is an {@link ActorId}. */
export const isActorId: (value: unknown) => value is ActorId = isTolerant(ActorId);
/** True when `value` is a {@link RelationId}. */
export const isRelationId: (value: unknown) => value is RelationId = isTolerant(RelationId);

// ---------------------------------------------------------------------------
// Actor types and the alias dialect
// ---------------------------------------------------------------------------

/** The three things an {@link ActorId} can name. */
export const ActorType = Schema.Literal('player', 'city', 'unit').annotations({
  identifier: 'ActorType',
  description: 'The three actor species an ACTOR_ID_RE prefix can name',
});
/** The three things an {@link ActorId} can name. */
export type ActorType = typeof ActorType.Type;

/** `{"player", "city", "unit"}`, for membership tests over untyped strings. */
export const ACTOR_TYPES: ReadonlySet<string> = new Set<ActorType>(['player', 'city', 'unit']);

/** Decode an unknown value as an {@link ActorType}. */
export const decodeActorType: TolerantDecoder<ActorType> = decodeTolerant(ActorType);

/** True when `value` is one of the three actor species. */
export const isActorType = (value: unknown): value is ActorType =>
  typeof value === 'string' && ACTOR_TYPES.has(value);

/**
 * `"unit_0123…"` → `"unit"`.  Python spells this `actor_id.split("_", 1)[0]`.
 *
 * Total: an id with no `_` is its own prefix, which is what `split` returns.
 */
export const idPrefix = (identifier: string): string => {
  const cut = identifier.indexOf('_');
  return cut < 0 ? identifier : identifier.slice(0, cut);
};

/** The four alias prefix letters (`ENTITY_ALIAS_RE` group 1). */
export type AliasPrefix = 'u' | 'c' | 'p' | 'r';

/** What an alias prefix names.  `relation` is not an {@link ActorType}. */
export type AliasEntityKind = ActorType | 'relation';

/**
 * `ALIAS_ENTITY_TYPES` — `play-cli/src/constants.ts:217-222`, from
 * `play/client.py:123-125`.
 *
 * Typed as a total map over {@link AliasPrefix} rather than
 * `Record<string, string>`, so the lookup after an `ENTITY_ALIAS_RE` match
 * cannot be `undefined` and needs no defensive branch.  The Python indexes it
 * directly (`ALIAS_ENTITY_TYPES[match.group(1)]`) for the same reason.
 */
export const ALIAS_ENTITY_TYPES: Readonly<Record<AliasPrefix, AliasEntityKind>> = {
  u: 'unit',
  c: 'city',
  p: 'player',
  r: 'relation',
};

const isAliasPrefix = (value: string): value is AliasPrefix =>
  value === 'u' || value === 'c' || value === 'p' || value === 'r';

/**
 * Report whether a stored entity alias still names its own id type.
 * Ports `_entity_alias_id_matches` (`play/client.py:1626-1636`).
 *
 * Both halves are needed.  The pattern alone would let `u…` point at
 * `city_…`, because `ACTOR_ID_RE` accepts all three species; the `startsWith`
 * alone would accept `player_zzz`.  A cached alias table that survived a
 * version change is exactly where a mismatched pair shows up, and resolving one
 * would send a request about the wrong entity.
 */
export const entityAliasIdMatches = (alias: string, identifier: unknown): boolean => {
  const match = ENTITY_ALIAS_RE.exec(alias);
  const prefix = match?.[1];
  if (prefix === undefined || !isAliasPrefix(prefix) || typeof identifier !== 'string') {
    return false;
  }
  const kind = ALIAS_ENTITY_TYPES[prefix];
  const pattern = kind === 'relation' ? RELATION_ID_RE : ACTOR_ID_RE;
  return pattern.test(identifier) && identifier.startsWith(`${kind}_`);
};
