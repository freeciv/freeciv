/**
 * Alias expansion — the only place an `aN`/`u1`/`T(x,y)` becomes an opaque ID.
 *
 * Ports `_closest_aliases` (client.py:3140-3153), `_alias_refresh_command`
 * (3156-3169), `_expand_action_alias` (3172-3216), `_expand_entity_alias`
 * (3219-3232), `_expand_tile_alias` (3235-3250), `_looks_like_alias`
 * (3253-3258) and `_expand_alias` (3261-3272).
 *
 * Nothing here ever puts an alias on the wire: expansion happens before a
 * request is built, and a refusal is preferred to a guess.  The refusal text is
 * the agent's primary recovery surface, so every sentence is ported verbatim —
 * an alias that was never assigned is a typo whatever the bucket's age, and
 * telling an agent that `a99` "was enumerated at rev7" is a false statement in
 * the one message whose purpose is to teach.
 */
import { Effect } from 'effect';
import {
  ACTION_ALIAS_RE,
  ALIAS_ENTITY_TYPES,
  ENTITY_ALIAS_RE,
  TILE_ALIAS_RE,
} from 'src/constants';
import { playerError, type PlayerError } from 'src/errors';
import { revisionLabel } from 'src/render/primitives';
import { revisionsEqual } from 'src/schema/revision';
import { resolveExisting } from 'src/services/private-fs';
import { parseActionAliases, parseEntityAliases, parseTileAliases } from 'src/services/aliases';
import { SessionStore, type V2ClientState } from 'src/services/session-store';

// ---------------------------------------------------------------------------
// _looks_like_alias
// ---------------------------------------------------------------------------

/** Report whether one token is alias-shaped at all. */
export const looksLikeAlias = (text: string): boolean =>
  ACTION_ALIAS_RE.test(text) || ENTITY_ALIAS_RE.test(text) || TILE_ALIAS_RE.test(text);

// ---------------------------------------------------------------------------
// _closest_aliases
// ---------------------------------------------------------------------------

const aliasNumber = (alias: string): number => {
  const digits = alias.replace(/^[A-Za-z]+/, '');
  const parsed = Number.parseInt(digits, 10);
  return digits === '' || Number.isNaN(parsed) ? 0 : parsed;
};

const byCodePoint = (left: string, right: string): number =>
  left < right ? -1 : left > right ? 1 : 0;

/** Name the nearest known aliases so a typo has an obvious repair. */
export const closestAliases = (known: ReadonlyArray<string>, wanted: string): string => {
  if (known.length === 0) return 'none are known yet';
  const index = aliasNumber(wanted);
  const nearest = [...known].sort((left, right) => {
    const distance = Math.abs(aliasNumber(left) - index) - Math.abs(aliasNumber(right) - index);
    return distance !== 0 ? distance : byCodePoint(left, right);
  });
  const shown = nearest
    .slice(0, 8)
    .sort((left, right) => aliasNumber(left) - aliasNumber(right))
    .join(' ');
  return shown + (nearest.length > 8 ? ' …' : '');
};

// ---------------------------------------------------------------------------
// _alias_refresh_command
// ---------------------------------------------------------------------------

/**
 * Name the exact command that re-enumerates aliases.
 *
 * The session path is printed only when this workspace cannot resolve the seat
 * by itself, so the remedy always runs exactly as it reads.
 */
export const aliasRefreshCommand = (
  sessionPath: string,
  actorId: string
): Effect.Effect<string, never, SessionStore> =>
  Effect.gen(function* () {
    const store = yield* SessionStore;
    const ambiguous = yield* Effect.match(store.sessionPath(''), {
      onFailure: () => true,
      onSuccess: (resolved) => resolveExisting(resolved) !== resolveExisting(sessionPath),
    });
    const suffix = actorId === '' ? '' : ` --actor_id ${actorId} --all`;
    return `just legal${ambiguous ? ` --session ${sessionPath}` : ''}${suffix}`;
  });

// ---------------------------------------------------------------------------
// _expand_action_alias
// ---------------------------------------------------------------------------

/**
 * Expand one `aN`, or refuse with the sentence that repairs it.
 *
 * Action aliases die with their revision: the bucket names the revision it was
 * built from, and an alias recorded against an older one is refused **by name**
 * rather than resolved to an expired handle.
 */
export const expandActionAlias = (
  state: V2ClientState,
  alias: string,
  sessionPath: string
): Effect.Effect<string, PlayerError, SessionStore> =>
  Effect.gen(function* () {
    const table = yield* parseActionAliases(state.action_aliases);
    const entry = table.by_alias[alias];
    const current = state.last_revision;
    const recorded = table.state_revision;
    const stale =
      recorded !== null && (current === null || !revisionsEqual(recorded, current));
    if (entry === undefined) {
      const known = Object.keys(table.by_alias);
      if (known.length === 0) {
        const remedy = yield* aliasRefreshCommand(sessionPath, '');
        return yield* Effect.fail(
          playerError(
            `unknown action alias ${alias}: no legal-action catalog has ` +
              `been read yet; run \`${remedy}\``
          )
        );
      }
      const nearest = closestAliases(known, alias);
      if (stale && recorded !== null) {
        const remedy = yield* aliasRefreshCommand(sessionPath, '');
        return yield* Effect.fail(
          playerError(
            `unknown action alias ${alias}: it was never enumerated, and ` +
              `the aliases that were (${nearest}) died with ` +
              `${revisionLabel(recorded)}; re-enumerate ` +
              `with \`${remedy}\``
          )
        );
      }
      return yield* Effect.fail(
        playerError(`unknown action alias ${alias}; this revision enumerated ${nearest}`)
      );
    }
    if (stale && recorded !== null) {
      const entities = yield* parseEntityAliases(state.entity_aliases);
      const byIdentifier = new Map(
        Object.entries(entities).map(([name, identifier]) => [identifier, name])
      );
      const scope = byIdentifier.get(entry.actor_id) ?? entry.actor_id;
      const remedy = yield* aliasRefreshCommand(sessionPath, scope);
      return yield* Effect.fail(
        playerError(
          `action alias ${alias} was enumerated at ` +
            `${revisionLabel(recorded)} but this seat now ` +
            `knows ${current === null ? 'no revision' : revisionLabel(current)}` +
            '; action aliases die with their revision. Re-enumerate with ' +
            `\`${remedy}\` and use the new a1..aN`
        )
      );
    }
    return entry.action_id;
  });

// ---------------------------------------------------------------------------
// _expand_entity_alias
// ---------------------------------------------------------------------------

/** Expand one `u1`/`c1`/`p1`/`r1`; these are stable for the whole game. */
export const expandEntityAlias = (
  state: V2ClientState,
  alias: string
): Effect.Effect<string, PlayerError> =>
  Effect.gen(function* () {
    const entities = yield* parseEntityAliases(state.entity_aliases);
    const identifier = entities[alias];
    if (identifier !== undefined) return identifier;
    const prefix = alias.slice(0, 1);
    const kind = ALIAS_ENTITY_TYPES[prefix] ?? prefix;
    const known = Object.keys(entities).filter((candidate) => candidate.startsWith(prefix));
    return yield* Effect.fail(
      playerError(
        `unknown ${kind} alias ${alias}; known ${kind} aliases: ` +
          closestAliases(known, alias)
      )
    );
  });

// ---------------------------------------------------------------------------
// _expand_tile_alias
// ---------------------------------------------------------------------------

/** Expand one `T(x,y)` out of the coordinate cache, or name the nearest. */
export const expandTileAlias = (
  state: V2ClientState,
  x: number,
  y: number
): Effect.Effect<string, PlayerError> =>
  Effect.gen(function* () {
    const tiles = yield* parseTileAliases(state.tile_aliases);
    const identifier = tiles[`${x},${y}`];
    if (identifier !== undefined) return identifier;
    const distance = (key: string): number => {
      const [left = '0', right = '0'] = key.split(',');
      return Math.abs(Number.parseInt(left, 10) - x) + Math.abs(Number.parseInt(right, 10) - y);
    };
    const known = Object.keys(tiles)
      .sort((left, right) => {
        const spread = distance(left) - distance(right);
        return spread !== 0 ? spread : byCodePoint(left, right);
      })
      .slice(0, 6);
    const nearest =
      known.length === 0 ? 'none are cached yet' : known.map((key) => `T(${key})`).join(' ');
    return yield* Effect.fail(
      playerError(
        `unknown tile T(${x},${y}): no page this seat has read named that ` +
          `coordinate. Nearest cached tiles: ${nearest}`
      )
    );
  });

// ---------------------------------------------------------------------------
// _expand_alias
// ---------------------------------------------------------------------------

/** Expand one alias to its opaque ID; pass any other text through. */
export const expandAlias = (
  state: V2ClientState,
  text: string,
  sessionPath: string
): Effect.Effect<string, PlayerError, SessionStore> => {
  if (ACTION_ALIAS_RE.test(text)) return expandActionAlias(state, text, sessionPath);
  if (ENTITY_ALIAS_RE.test(text)) return expandEntityAlias(state, text);
  const tile = TILE_ALIAS_RE.exec(text);
  if (tile !== null) {
    const x = Number.parseInt(tile[1] ?? '', 10);
    const y = Number.parseInt(tile[2] ?? '', 10);
    if (!Number.isNaN(x) && !Number.isNaN(y)) return expandTileAlias(state, x, y);
  }
  return Effect.succeed(text);
};
