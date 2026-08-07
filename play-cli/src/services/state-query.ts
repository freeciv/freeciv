/**
 * `state`'s query string, and the refusals that keep a section honest.
 *
 * Ports `_state_query` (client.py:7707-7787) and `_limit` (6513-6518).
 *
 * Every flag named in a refusal is spelled the way `just` accepts it: the
 * wrapper takes only the underscore form, so a hyphenated remedy fails as
 * printed, at the moment the agent is already lost.  `test_v2_state_refusals_
 * spell_every_flag_the_way_just_accepts` asserts exactly that.
 */
import { Effect } from 'effect';
import { PlayerError, playerError } from 'src/errors';
import {
  ACTOR_ID_RE,
  CITY_ID_RE,
  CURSOR_RE,
  RELATION_ID_RE,
  TILE_ID_RE,
  V2_CITY_SECTIONS,
  V2_SECTIONS,
} from 'src/constants';

/** `_limit` — a canonical integer from 1 through 16, or the default. */
export const stateLimit = (value: string | null, fallback = 16): Effect.Effect<number, PlayerError> => {
  if (value === null) return Effect.succeed(fallback);
  if (!/^(?:[1-9]|1[0-6])$/.test(value)) {
    return Effect.fail(playerError('limit must be a canonical integer from 1 through 16'));
  }
  return Effect.succeed(Number(value));
};

/** The `state` flag surface, after `_resolve_alias_arguments` has run. */
export interface StateQueryArgs {
  readonly section: string;
  readonly actorId: string;
  readonly relationId: string;
  readonly centerId: string;
  readonly radius: number | null;
  readonly limit: string | null;
  readonly cursor: string;
}

const encode = (parameters: ReadonlyArray<readonly [string, string]>): string =>
  parameters
    .map(([key, value]) => `${encodeURIComponent(key)}=${encodeURIComponent(value)}`)
    .join('&');

const unsupported = (section: string): PlayerError => {
  const available = [...V2_SECTIONS].sort().join(', ');
  return playerError(
    `state section '${section}' is not supported; valid sections: ` +
      `${available}. Economy and current government are in overview; ` +
      'government choices are in governments'
  );
};

export const stateQuery = (args: StateQueryArgs): Effect.Effect<string, PlayerError> =>
  Effect.gen(function* () {
    const cursor = args.cursor.trim();
    const section = args.section.trim();
    const actorId = args.actorId.trim();
    const relationId = args.relationId.trim();
    const centerId = args.centerId.trim();
    const radius = args.radius;
    const limit = args.limit;
    if (cursor !== '') {
      if (
        section !== '' ||
        actorId !== '' ||
        relationId !== '' ||
        centerId !== '' ||
        radius !== null ||
        limit !== null ||
        !CURSOR_RE.test(cursor)
      ) {
        return yield* Effect.fail(playerError('state cursor must be the only page option'));
      }
      return encode([['cursor', cursor]]);
    }
    if (limit !== null && section === '') {
      return yield* Effect.fail(playerError('state --limit requires --section'));
    }
    if (section !== '' && !V2_SECTIONS.has(section)) {
      return yield* Effect.fail(unsupported(section));
    }
    if (section === '') {
      if (actorId !== '' || relationId !== '' || centerId !== '' || radius !== null) {
        return yield* Effect.fail(playerError('state scope options require --section'));
      }
      return '';
    }
    const parameters: Array<readonly [string, string]> = [
      ['section', section],
      ['limit', String(yield* stateLimit(limit))],
    ];
    if (V2_CITY_SECTIONS.has(section)) {
      if (
        !CITY_ID_RE.test(actorId) ||
        relationId !== '' ||
        centerId !== '' ||
        radius !== null
      ) {
        return yield* Effect.fail(
          playerError('city state sections require exactly one opaque --actor_id')
        );
      }
      parameters.push(['actor_id', actorId]);
    } else if (section === 'diplomacy_clauses') {
      if (
        actorId !== '' ||
        !RELATION_ID_RE.test(relationId) ||
        centerId !== '' ||
        radius !== null
      ) {
        return yield* Effect.fail(
          playerError(
            'diplomacy_clauses requires exactly one opaque --relation_id ' +
              'from a `just state --section diplomacy` row'
          )
        );
      }
      parameters.push(['relation_id', relationId]);
    } else if (section === 'tile_window') {
      if (
        actorId !== '' ||
        relationId !== '' ||
        !TILE_ID_RE.test(centerId) ||
        radius === null ||
        !Number.isInteger(radius) ||
        radius < 0 ||
        radius > 8
      ) {
        return yield* Effect.fail(
          playerError('tile_window requires --center_id and --radius from 0 through 8')
        );
      }
      parameters.push(['center_id', centerId], ['radius', String(radius)]);
    } else if (section === 'unit_route') {
      if (
        !ACTOR_ID_RE.test(actorId) ||
        !actorId.startsWith('unit_') ||
        relationId !== '' ||
        centerId !== '' ||
        radius !== null
      ) {
        return yield* Effect.fail(
          playerError('unit_route requires exactly one opaque unit --actor_id')
        );
      }
      parameters.push(['actor_id', actorId]);
    } else if (actorId !== '' || relationId !== '' || centerId !== '' || radius !== null) {
      return yield* Effect.fail(
        playerError('state scope options are not valid for this section')
      );
    }
    return encode(parameters);
  });
