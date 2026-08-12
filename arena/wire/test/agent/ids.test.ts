/**
 * The full-control-v2 identifier alphabet, checked against `play/client.py`.
 *
 * Sample identifiers are the ones `play-cli/test/_fixtures/wire.ts:12-16` uses,
 * copied as literals so this suite depends on nothing outside `@arena/wire`.
 */
import { describe, expect, test } from 'bun:test';
import { Either } from 'effect';
import {
  ACTOR_TYPES,
  ALIAS_ENTITY_TYPES,
  ActorType,
  decodeActorId,
  decodeActorType,
  decodeCatalogId,
  decodeCityId,
  decodeControllerName,
  decodeCursor,
  decodeEntityAlias,
  decodeOpaqueId,
  decodePlayGameId,
  decodeRelationId,
  decodeTileId,
  entityAliasIdMatches,
  idPrefix,
  isActorId,
  isActorType,
  isControllerName,
  isCursor,
  isOpaqueId,
  isPlayGameId,
  OPAQUE_ID_RE,
  PLAY_GAME_ID_RE,
} from 'src/agent/ids';
import { decodeGameId, GAME_ID_RE } from 'src/ids';

const accepts = (either: Either.Either<unknown, unknown>): boolean => Either.isRight(either);

// play-cli/test/_fixtures/wire.ts:12-16
const FIXTURE_GAME_ID = 'game_Hsit9YEuBjKdJPPouFoGVYlk';
const FIXTURE_AGENT_ID = 'agent_0123456789abcdef';
const FIXTURE_CONTROLLER = 'codex-gpt-5.6-sol';
const FIXTURE_CURSOR = 'cursor_abcdefghijklmnopqrstuvwxyz012345';

const HEX32 = '0123456789abcdef0123456789abcdef';

describe('OpaqueId', () => {
  test('accepts the fixture agent id and the shapes client.py signs off on', () => {
    expect(accepts(decodeOpaqueId(FIXTURE_AGENT_ID))).toBe(true);
    // play-cli/test/schema.test.ts:69 — the punctuation the alphabet allows.
    expect(accepts(decodeOpaqueId('unit_0.a:b-c'))).toBe(true);
    expect(accepts(decodeOpaqueId('A'))).toBe(true);
  });

  test('the first character must be alphanumeric', () => {
    expect(accepts(decodeOpaqueId('_leading'))).toBe(false);
    expect(accepts(decodeOpaqueId('.leading'))).toBe(false);
    expect(accepts(decodeOpaqueId('-leading'))).toBe(false);
    expect(accepts(decodeOpaqueId(':leading'))).toBe(false);
  });

  test('1 to 128 characters, inclusive at both ends', () => {
    expect(accepts(decodeOpaqueId(''))).toBe(false);
    expect(accepts(decodeOpaqueId('a'.repeat(128)))).toBe(true);
    expect(accepts(decodeOpaqueId('a'.repeat(129)))).toBe(false);
  });

  test('whitespace, slashes and non-ASCII are refused', () => {
    // play-cli/test/schema.test.ts:70 asserts `has space` is refused.
    expect(accepts(decodeOpaqueId('has space'))).toBe(false);
    expect(accepts(decodeOpaqueId('a/b'))).toBe(false);
    expect(accepts(decodeOpaqueId('aé'))).toBe(false);
    expect(accepts(decodeOpaqueId('a\n'))).toBe(false);
  });

  test('a trailing newline cannot sneak past the anchors', () => {
    // Python's `$` would match before a trailing newline; `re.fullmatch` and
    // JavaScript's `$` (no `m` flag) both refuse it, which is the parity.
    expect(OPAQUE_ID_RE.test('abc\n')).toBe(false);
    expect(PLAY_GAME_ID_RE.test(`${FIXTURE_GAME_ID}\n`)).toBe(false);
  });

  test('non-strings are refused, not coerced', () => {
    expect(accepts(decodeOpaqueId(7))).toBe(false);
    expect(accepts(decodeOpaqueId(null))).toBe(false);
    expect(isOpaqueId(7)).toBe(false);
  });
});

describe('PlayGameId', () => {
  test('accepts the fixture id and demands the game_ prefix', () => {
    expect(accepts(decodePlayGameId(FIXTURE_GAME_ID))).toBe(true);
    expect(accepts(decodePlayGameId('Hsit9YEuBjKdJPPouFoGVYlk'))).toBe(false);
    expect(accepts(decodePlayGameId(`GAME_${'a'.repeat(20)}`))).toBe(false);
  });

  test('20 to 80 characters follow the prefix', () => {
    expect(accepts(decodePlayGameId(`game_${'a'.repeat(19)}`))).toBe(false);
    expect(accepts(decodePlayGameId(`game_${'a'.repeat(20)}`))).toBe(true);
    expect(accepts(decodePlayGameId(`game_${'a'.repeat(80)}`))).toBe(true);
    expect(accepts(decodePlayGameId(`game_${'a'.repeat(81)}`))).toBe(false);
  });

  test('path separators are refused', () => {
    expect(accepts(decodePlayGameId(`game_${'a'.repeat(20)}/manifest.json`))).toBe(false);
    expect(accepts(decodePlayGameId('game_../../etc/passwd0000'))).toBe(false);
  });

  test('the agent-side and gateway-side game-id rules are NOT the same set', () => {
    // Documented in src/agent/ids.ts: the two patterns agree on the ids in
    // circulation and part company at the top of the range, because the
    // gateway's 20-80 bound covers the whole string and includes the prefix.
    expect(accepts(decodePlayGameId(FIXTURE_GAME_ID))).toBe(true);
    expect(accepts(decodeGameId(FIXTURE_GAME_ID))).toBe(true);

    const long = `game_${'a'.repeat(78)}`; // 83 characters in total
    expect(accepts(decodePlayGameId(long))).toBe(true);
    expect(accepts(decodeGameId(long))).toBe(false);

    // And in the other direction: a bare 20-character id is fine to the
    // gateway and meaningless to the agent client.
    const bare = 'a'.repeat(20);
    expect(accepts(decodeGameId(bare))).toBe(true);
    expect(accepts(decodePlayGameId(bare))).toBe(false);

    expect(PLAY_GAME_ID_RE.source).not.toBe(GAME_ID_RE.source);
  });
});

describe('ControllerName', () => {
  test('accepts the fixture controller label', () => {
    expect(accepts(decodeControllerName(FIXTURE_CONTROLLER))).toBe(true);
    expect(isControllerName(FIXTURE_CONTROLLER)).toBe(true);
  });

  test('3 to 96 characters, alphanumeric-initial', () => {
    expect(accepts(decodeControllerName('ab'))).toBe(false);
    expect(accepts(decodeControllerName('abc'))).toBe(true);
    expect(accepts(decodeControllerName('a'.repeat(96)))).toBe(true);
    expect(accepts(decodeControllerName('a'.repeat(97)))).toBe(false);
    expect(accepts(decodeControllerName('-abc'))).toBe(false);
  });

  test('the shape rule alone lets through what join-time refuses', () => {
    // `_controller_name` (client.py:743-755) additionally demands a `-`, no
    // leading/trailing `-`, and refuses the generic labels.  Those live at the
    // join command, so the decoder must still accept them off the wire.
    expect(accepts(decodeControllerName('agent'))).toBe(true);
    expect(accepts(decodeControllerName('harness-model'))).toBe(true);
    expect(accepts(decodeControllerName('nodashes'))).toBe(true);
  });

  test('a colon is in OPAQUE_ID_RE but not in CONTROLLER_RE', () => {
    expect(accepts(decodeOpaqueId('a:b'))).toBe(true);
    expect(accepts(decodeControllerName('a:b'))).toBe(false);
  });
});

describe('the hex-suffixed identifiers', () => {
  test('each demands its own prefix and 32 lowercase hex characters', () => {
    expect(accepts(decodeCityId(`city_${HEX32}`))).toBe(true);
    expect(accepts(decodeTileId(`tile_${HEX32}`))).toBe(true);
    expect(accepts(decodeRelationId(`relation_${HEX32}`))).toBe(true);
    expect(accepts(decodeActorId(`unit_${HEX32}`))).toBe(true);
    expect(accepts(decodeActorId(`city_${HEX32}`))).toBe(true);
    expect(accepts(decodeActorId(`player_${HEX32}`))).toBe(true);

    expect(accepts(decodeActorId(`relation_${HEX32}`))).toBe(false);
    expect(accepts(decodeCityId(`tile_${HEX32}`))).toBe(false);
  });

  test('uppercase hex is refused — the server emits lowercase only', () => {
    expect(accepts(decodeCityId(`city_${HEX32.toUpperCase()}`))).toBe(false);
    expect(accepts(decodeActorId(`unit_${HEX32.toUpperCase()}`))).toBe(false);
  });

  test('the digit count is exact', () => {
    expect(accepts(decodeActorId(`unit_${HEX32.slice(1)}`))).toBe(false);
    expect(accepts(decodeActorId(`unit_${HEX32}0`))).toBe(false);
  });
});

describe('Cursor and CatalogId', () => {
  test('accept the fixture cursor and a 32-character catalog id', () => {
    expect(accepts(decodeCursor(FIXTURE_CURSOR))).toBe(true);
    expect(isCursor(FIXTURE_CURSOR)).toBe(true);
    expect(accepts(decodeCatalogId(`catalog_${'a'.repeat(32)}`))).toBe(true);
  });

  test('the suffix alphabet is URL-safe base64, not hex', () => {
    expect(accepts(decodeCursor(`cursor_${'-'.repeat(32)}`))).toBe(true);
    expect(accepts(decodeCursor(`cursor_${'.'.repeat(32)}`))).toBe(false);
    expect(accepts(decodeCursor(`cursor_${'a'.repeat(31)}`))).toBe(false);
    expect(accepts(decodeCursor(`cursor_${'a'.repeat(33)}`))).toBe(false);
  });
});

describe('ActorType', () => {
  test('is closed over the three ACTOR_ID_RE prefixes', () => {
    expect(accepts(decodeActorType('player'))).toBe(true);
    expect(accepts(decodeActorType('city'))).toBe(true);
    expect(accepts(decodeActorType('unit'))).toBe(true);
    expect(accepts(decodeActorType('relation'))).toBe(false);
    expect(accepts(decodeActorType('tile'))).toBe(false);
    expect(ActorType.literals).toEqual(['player', 'city', 'unit']);
    expect(ACTOR_TYPES.size).toBe(3);
  });

  test('isActorType agrees with the schema and tolerates non-strings', () => {
    expect(isActorType('unit')).toBe(true);
    expect(isActorType('relation')).toBe(false);
    expect(isActorType(null)).toBe(false);
  });
});

describe('idPrefix', () => {
  test('splits at the first underscore, like Python split("_", 1)[0]', () => {
    expect(idPrefix(`unit_${HEX32}`)).toBe('unit');
    expect(idPrefix('a_b_c')).toBe('a');
  });

  test('an id with no underscore is its own prefix', () => {
    expect(idPrefix('bare')).toBe('bare');
    expect(idPrefix('')).toBe('');
  });

  test('a leading underscore yields the empty prefix, not the whole id', () => {
    expect(idPrefix('_x')).toBe('');
  });
});

describe('EntityAlias and entityAliasIdMatches', () => {
  test('the alias dialect is one letter and a 1-4 digit number from 1', () => {
    expect(accepts(decodeEntityAlias('u1'))).toBe(true);
    expect(accepts(decodeEntityAlias('c9999'))).toBe(true);
    expect(accepts(decodeEntityAlias('u0'))).toBe(false);
    expect(accepts(decodeEntityAlias('u01'))).toBe(false);
    expect(accepts(decodeEntityAlias('u10000'))).toBe(false);
    expect(accepts(decodeEntityAlias('x1'))).toBe(false);
    expect(accepts(decodeEntityAlias('U1'))).toBe(false);
  });

  test('the prefix table covers exactly the four alias letters', () => {
    expect(ALIAS_ENTITY_TYPES).toEqual({
      u: 'unit',
      c: 'city',
      p: 'player',
      r: 'relation',
    });
  });

  test('an alias matches only an id of its own species', () => {
    expect(entityAliasIdMatches('u1', `unit_${HEX32}`)).toBe(true);
    expect(entityAliasIdMatches('c1', `city_${HEX32}`)).toBe(true);
    expect(entityAliasIdMatches('p1', `player_${HEX32}`)).toBe(true);
    expect(entityAliasIdMatches('r1', `relation_${HEX32}`)).toBe(true);
  });

  test('a cross-species pair is the whole point of the check', () => {
    // ACTOR_ID_RE accepts all three actor species, so the pattern alone would
    // let `u1` resolve to a city.  The startsWith half is what refuses it.
    expect(entityAliasIdMatches('u1', `city_${HEX32}`)).toBe(false);
    expect(entityAliasIdMatches('c1', `unit_${HEX32}`)).toBe(false);
    expect(entityAliasIdMatches('r1', `unit_${HEX32}`)).toBe(false);
    expect(entityAliasIdMatches('u1', `relation_${HEX32}`)).toBe(false);
  });

  test('the pattern half refuses a well-prefixed id with the wrong body', () => {
    expect(entityAliasIdMatches('p1', 'player_zzz')).toBe(false);
    expect(entityAliasIdMatches('u1', `unit_${HEX32.toUpperCase()}`)).toBe(false);
  });

  test('a malformed alias or a non-string id is false, never a throw', () => {
    expect(entityAliasIdMatches('x1', `unit_${HEX32}`)).toBe(false);
    expect(entityAliasIdMatches('', `unit_${HEX32}`)).toBe(false);
    expect(entityAliasIdMatches('u1', 7)).toBe(false);
    expect(entityAliasIdMatches('u1', null)).toBe(false);
    expect(entityAliasIdMatches('u1', undefined)).toBe(false);
  });

  test('ENTITY_ALIAS_RE has no lastIndex state to leak between calls', () => {
    // A `/g` regex would answer differently on the second identical call.
    expect(entityAliasIdMatches('u1', `unit_${HEX32}`)).toBe(true);
    expect(entityAliasIdMatches('u1', `unit_${HEX32}`)).toBe(true);
  });
});

describe('the guards agree with the decoders', () => {
  const samples: ReadonlyArray<string> = [
    FIXTURE_GAME_ID,
    FIXTURE_AGENT_ID,
    FIXTURE_CONTROLLER,
    FIXTURE_CURSOR,
    `unit_${HEX32}`,
    'has space',
    '',
    '_x',
  ];

  test('every sample gets the same verdict from both faces', () => {
    samples.forEach((sample) => {
      expect(isOpaqueId(sample)).toBe(accepts(decodeOpaqueId(sample)));
      expect(isPlayGameId(sample)).toBe(accepts(decodePlayGameId(sample)));
      expect(isControllerName(sample)).toBe(accepts(decodeControllerName(sample)));
      expect(isCursor(sample)).toBe(accepts(decodeCursor(sample)));
      expect(isActorId(sample)).toBe(accepts(decodeActorId(sample)));
    });
  });
});
