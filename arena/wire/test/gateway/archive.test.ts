/**
 * Decode parity for the archive routes, against captured gateway bytes.
 *
 * The Python producers are the oracle: if a fixture does not decode, the
 * schema is wrong.  Three captures cover both producers of the same document —
 * `gateway-watch-terminal.json` is the gateway's own archive reader,
 * `supervisor-watch.json` is the live supervisor, and
 * `gateway-watch-running.json` is that supervisor payload relayed verbatim
 * through the gateway (which is why it must decode identically).
 *
 * There is no capture of `GET /v1/games/{id}/frames` in the corpus, so the
 * frame-listing envelope is assembled here from the watch fixture's own
 * `frames` array exactly as `_archive_frames` (`replay_gateway.py:995-1004`)
 * builds it.  That is called out where it happens; every other payload in this
 * file is bytes a real gateway sent.
 */
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { describe, expect, test } from 'bun:test';
import { Either, Option } from 'effect';
import {
  archiveBinaryContentType,
  ARCHIVE_BINARY_CACHE_CONTROL,
  ARCHIVE_PNG_RE,
  ARCHIVE_PPM_RE,
  ARCHIVE_SCHEMA_VERSION,
  ARCHIVED_VIDEO_KIND,
  archiveFramePngPath,
  archiveFramesPath,
  archiveLatestFramePath,
  archivePpmTurn,
  archiveVideoPath,
  decodeArchivePngName,
  decodeFrameManifest,
  decodeMapPlayer,
  decodeReplayFrame,
  encodeReplayFrame,
  decodeWatchResponse,
  DYNAMIC_CONTROLLER_LABEL,
  DYNAMIC_CONTROLLER_TYPE,
  DYNAMIC_SEAT_ID_PREFIX,
  encodeArchivePngName,
  encodeFrameManifest,
  encodeWatchResponse,
  FRAME_MANIFEST_LABEL,
  isDynamicMapPlayer,
  LEGACY_FRAME_TURN_RE,
  legacyFrameTurn,
  LIVE_VIDEO_KIND,
  MAP_PLAYER_COLOR_RE,
  type MapPlayer,
  type ReplayFrame,
  SUPERVISOR_FRAME_TURN_RE,
  WATCH_LABEL,
  type WatchResponse,
  watchProducer,
} from 'src/gateway/archive';
import { FrameIndex, GameId } from 'src/ids';
import { decodeJsonObject, isJsonArray, isJsonObject, type JsonObject, jsonField } from 'src/json';

const FIXTURES = join(import.meta.dir, '..', 'fixtures');

const rightOrThrow = <A, E>(either: Either.Either<A, E>): A =>
  Either.getOrThrowWith(
    either,
    (error) => new Error(`expected Right, got ${JSON.stringify(error, null, 2)}`),
  );

/**
 * Fixtures are read as `JsonObject` rather than cast: the raw-shape assertions
 * below have to talk about keys the schema deliberately does not name, and a
 * cast would be a claim instead of a check.
 */
const readFixture = (relative: string): JsonObject =>
  rightOrThrow(
    decodeJsonObject(JSON.parse(readFileSync(join(FIXTURES, relative), 'utf8')) as unknown),
  );

/** The objects in `root[key]`, or `[]` when it is not an array of objects. */
const objectsAt = (root: JsonObject, key: string): ReadonlyArray<JsonObject> => {
  const value = jsonField(root, key);
  return isJsonArray(value) ? value.filter(isJsonObject) : [];
};

/** The `game` block of a raw `watch.json`, or `{}` if it is not an object. */
const gameOf = (payload: JsonObject): JsonObject => {
  const game = jsonField(payload, 'game');
  return isJsonObject(game) ? game : {};
};

/** Every `map_players` row across every raw frame of a captured payload. */
const rawMapPlayers = (payload: JsonObject): ReadonlyArray<JsonObject> =>
  objectsAt(payload, 'frames').flatMap((frame) => objectsAt(frame, 'map_players'));

/** The three captures of `watch.json`, one per producer path. */
const WATCH_FIXTURES = [
  'live/gateway-watch-terminal.json',
  'live/gateway-watch-running.json',
  'live/supervisor-watch.json',
];

const watchOf = (relative: string): WatchResponse =>
  rightOrThrow(decodeWatchResponse(readFixture(relative)));

const terminalRaw = readFixture('live/gateway-watch-terminal.json');
const runningRaw = readFixture('live/gateway-watch-running.json');
const terminal = watchOf('live/gateway-watch-terminal.json');
const running = watchOf('live/gateway-watch-running.json');

const everyMapPlayer = (watch: WatchResponse): ReadonlyArray<MapPlayer> =>
  watch.frames.flatMap((frame) => [...frame.map_players]);

describe('watch.json decodes from both producers', () => {
  WATCH_FIXTURES.forEach((relative) => {
    test(`${relative} decodes`, () => {
      const decoded = decodeWatchResponse(readFixture(relative));
      expect(Either.isRight(decoded)).toBe(true);
    });

    test(`${relative} round-trips with key order and unknown fields intact`, () => {
      const raw = readFixture(relative);
      const encoded = rightOrThrow(encodeWatchResponse(watchOf(relative)));
      expect(JSON.stringify(encoded)).toBe(JSON.stringify(raw));
    });
  });

  test('the gateway relays the supervisor payload verbatim', () => {
    // Both were captured from the same live game seconds apart; only the
    // clock-derived phase timings differ, so the *shapes* must be identical.
    const supervisor = watchOf('live/supervisor-watch.json');
    expect(supervisor.frames.length).toBe(running.frames.length);
    expect(watchProducer(supervisor)).toBe(watchProducer(running));
  });

  test('the envelope constants are the ones Python hardcodes', () => {
    // A gateway int decodes to bigint, so the canonical writer spells it
    // without a fraction — see `src/numeric.ts`.
    expect(terminal.schema_version).toBe(BigInt(ARCHIVE_SCHEMA_VERSION));
    expect(running.schema_version).toBe(BigInt(ARCHIVE_SCHEMA_VERSION));
    expect(terminal.label).toBe(WATCH_LABEL);
    expect(running.label).toBe(WATCH_LABEL);
  });

  test('video.kind is the only field that names the producer', () => {
    expect(terminal.video.kind).toBe(ARCHIVED_VIDEO_KIND);
    expect(running.video.kind).toBe(LIVE_VIDEO_KIND);
    expect(watchProducer(terminal)).toBe('archive');
    expect(watchProducer(running)).toBe('live');
    expect(watchProducer({ ...terminal, video: { ...terminal.video, kind: 'video-next' } })).toBe(
      'unknown',
    );
  });

  test('an unknown kind decodes rather than failing (no Literal lock)', () => {
    const drifted = {
      ...terminalRaw,
      video: { available: true, url: 'https://x/video.mp4', kind: 'video-so-far-v2' },
      label: 'Renamed by a future gateway',
      schema_version: 2,
    };
    expect(Either.isRight(decodeWatchResponse(drifted))).toBe(true);
  });
});

describe('T9 lie #1: replay.available is hardcoded on the archive path', () => {
  test('the archived capture claims a replay unconditionally', () => {
    expect(terminal.replay.available).toBe(true);
    // ...while its timeline proves the archive kept only two frames, i.e. the
    // claim is a constant at `replay_gateway.py:1055`, not a probe.
    expect(terminal.frames).toHaveLength(2);
  });
});

describe('T9 lie #2: timeline is not an index into frames', () => {
  test('the archive drops every frame whose turn could not be paired', () => {
    expect(terminal.timeline.map((entry) => entry.turn)).toEqual([1n, 753n]);
    expect(terminal.timeline.length).toBeLessThanOrEqual(terminal.frames.length);
  });

  test('archive timeline rows carry only `turn`', () => {
    const rows = objectsAt(terminalRaw, 'timeline');
    expect(rows.map((row) => Object.keys(row))).toEqual([['turn'], ['turn']]);
    expect(terminal.timeline.every((entry) => entry.year === undefined)).toBe(true);
  });

  test('the live supervisor answers 77 frames with an empty timeline', () => {
    // Nothing had resolved a turn through the barrier yet; the timeline is
    // appended at `supervisor.py:8783`, the frames come from the disk.
    expect(running.frames).toHaveLength(77);
    expect(running.timeline).toHaveLength(0);
  });

  test('a live timeline row with the full record still decodes', () => {
    const live = {
      ...terminalRaw,
      timeline: [
        {
          turn: 12,
          year: -3000,
          responded_seats: ['place-1'],
          timed_out_seats: [],
          resolved_at: 1786519729.533039,
        },
      ],
    };
    const decoded = rightOrThrow(decodeWatchResponse(live));
    expect(decoded.timeline[0]?.year).toBe(-3000n);
    expect(decoded.timeline[0]?.responded_seats).toEqual(['place-1']);
  });
});

describe('T9 lie #3: a frame index is a file name, not a turn or a position', () => {
  test('archived indices are sparse — 753 turns, two surviving frames', () => {
    expect(terminal.frames.map((frame) => Number(frame.index))).toEqual([0, 752]);
    expect(terminal.frames.map((frame) => frame.turn)).toEqual([1n, 753n]);
  });

  test('png_url ends with the unpadded route segment for that index', () => {
    const gameId = terminal.game.game_id;
    expect(
      terminal.frames.every((frame) =>
        frame.png_url.endsWith(archiveFramePngPath(gameId, frame.index)),
      ),
    ).toBe(true);
    expect(archiveFramePngPath(gameId, FrameIndex.make(752))).toBe(
      `/v1/games/${gameId}/frames/752.png`,
    );
  });

  test('source_name survives verbatim so the viewer fallback keeps working', () => {
    expect(terminal.frames.map((frame) => frame.source_name)).toEqual([
      'turn-0001-M-bc--tuZ1Pall.map.ppm',
      'turn-0753-M-bc--tuZ1Pall.map.ppm',
    ]);
    expect(terminal.frames.map((frame) => archivePpmTurn(frame.source_name))).toEqual([
      Option.some(1),
      Option.some(753),
    ]);
  });

  test('legacyFrameTurn agrees with the label when there is one', () => {
    expect(terminal.frames.map(legacyFrameTurn)).toEqual([Option.some(1), Option.some(753)]);
  });

  test('legacyFrameTurn re-derives the turn when pairing produced null', () => {
    const unpaired = { turn: null, source_name: 'turn-0042-M-bc--tuZ1Pall.map.ppm' };
    expect(legacyFrameTurn(unpaired)).toEqual(Option.some(42));
  });

  test('legacyFrameTurn gives up on a PNG source_name, as the viewer does', () => {
    expect(legacyFrameTurn({ turn: null, source_name: '000752.png' })).toEqual(Option.none());
  });

  test('a frame with no autosave decodes with turn null and no players', () => {
    const decoded = rightOrThrow(
      decodeReplayFrame({
        index: 9,
        turn: null,
        map_players: [],
        source_name: '000009.png',
        png_url: 'https://freeciv.localhost/v1/games/x/frames/9.png',
      }),
    );
    expect(decoded.turn).toBeNull();
    expect(decoded.map_players).toHaveLength(0);
  });
});

describe('the three name-to-turn rules disagree, deliberately', () => {
  const NAME = 'snap.turn-12.map.ppm';

  test('the gateway rule is anchored and requires a dash after the digits', () => {
    expect(ARCHIVE_PPM_RE.test(NAME)).toBe(false);
    expect(archivePpmTurn(NAME)).toEqual(Option.none());
    expect(archivePpmTurn('turn-0012-M-bc.map.ppm')).toEqual(Option.some(12));
  });

  test('the supervisor rule searches anywhere in the name', () => {
    expect(SUPERVISOR_FRAME_TURN_RE.exec(NAME)?.[1]).toBe('12');
  });

  test('the viewer rule is anchored but accepts a dot terminator', () => {
    expect(LEGACY_FRAME_TURN_RE.test(NAME)).toBe(false);
    expect(LEGACY_FRAME_TURN_RE.exec('turn-12.map.ppm')?.[1]).toBe('12');
  });
});

describe('map_players: matched rows null their identity, unmatched rows omit it', () => {
  const scoredRows = (payload: JsonObject, scored: boolean): ReadonlyArray<JsonObject> =>
    rawMapPlayers(payload).filter((row) => jsonField(row, 'scored') === scored);

  test('a matched row on the archive path carries all six identity keys', () => {
    const matched = scoredRows(terminalRaw, true);
    expect(matched.length).toBeGreaterThan(0);
    expect(matched.every((row) => Object.hasOwn(row, 'model'))).toBe(true);
    expect(matched.every((row) => Object.hasOwn(row, 'ai_difficulty'))).toBe(true);
    // Present *and null* — not omitted. That distinction is the whole reason
    // this module avoids `optionalWith({ nullable: true })`.
    expect(matched.every((row) => jsonField(row, 'model') === null)).toBe(true);
    expect(matched.every((row) => jsonField(row, 'ai_difficulty') === null)).toBe(true);
  });

  test('an unmatched (dynamic) row omits model and ai_difficulty entirely', () => {
    const dynamic = scoredRows(terminalRaw, false);
    expect(dynamic.length).toBeGreaterThan(0);
    expect(dynamic.some((row) => Object.hasOwn(row, 'model'))).toBe(false);
    expect(dynamic.some((row) => Object.hasOwn(row, 'ai_difficulty'))).toBe(false);
    expect(dynamic.every((row) => jsonField(row, 'place') === null)).toBe(true);
    expect(
      dynamic.every((row) => jsonField(row, 'controller_label') === DYNAMIC_CONTROLLER_LABEL),
    ).toBe(true);
    expect(
      dynamic.every((row) => jsonField(row, 'controller_type') === DYNAMIC_CONTROLLER_TYPE),
    ).toBe(true);
    const seatIds = dynamic.map((row) => jsonField(row, 'seat_id'));
    expect(
      seatIds.every((seat) => typeof seat === 'string' && seat.startsWith(DYNAMIC_SEAT_ID_PREFIX)),
    ).toBe(true);
  });

  test('the live supervisor served nothing but dynamic factions', () => {
    const rows = everyMapPlayer(running);
    expect(rows).toHaveLength(157);
    expect(rows.every((row) => !row.scored)).toBe(true);
    expect(rows.every(isDynamicMapPlayer)).toBe(true);
    expect(rows.every((row) => row.model === undefined)).toBe(true);
  });

  test('omitted stays omitted and null stays null across a round trip', () => {
    const encoded = rightOrThrow(decodeJsonObject(rightOrThrow(encodeWatchResponse(terminal))));
    const matched = rawMapPlayers(encoded).filter((row) => jsonField(row, 'scored') === true);
    const dynamic = rawMapPlayers(encoded).filter((row) => jsonField(row, 'scored') === false);
    expect(matched.length).toBeGreaterThan(0);
    expect(
      matched.every((row) => Object.hasOwn(row, 'model') && jsonField(row, 'model') === null),
    ).toBe(true);
    expect(dynamic.some((row) => Object.hasOwn(row, 'model'))).toBe(false);
  });

  test('every colour is upper-case #RRGGBB, on both producers', () => {
    const colours = [...everyMapPlayer(terminal), ...everyMapPlayer(running)].map(
      (row) => row.player_color,
    );
    expect(colours.length).toBeGreaterThan(150);
    expect(colours.every((colour) => MAP_PLAYER_COLOR_RE.test(colour))).toBe(true);
  });

  test('rows arrive sorted by (player_id, player_name)', () => {
    const ids = terminal.frames.map((frame) => frame.map_players.map((row) => row.player_id));
    expect(ids).toEqual(
      ids.map((row) => row.toSorted((left, right) => Number(left - right))),
    );
  });
});

describe('the frame listing', () => {
  // No capture of `/v1/games/{id}/frames` exists in the corpus. This envelope
  // is the one `_archive_frames` builds at `replay_gateway.py:995-1004`, filled
  // with the captured frames from the watch fixture it would have shared.
  const gameId = terminal.game.game_id;
  const base = 'https://freeciv.localhost';
  const listing = {
    schema_version: ARCHIVE_SCHEMA_VERSION,
    game_id: gameId,
    label: FRAME_MANIFEST_LABEL,
    frames: objectsAt(terminalRaw, 'frames'),
    latest_png_url: `${base}${archiveLatestFramePath(gameId)}`,
  };

  test('it decodes and round-trips', () => {
    const decoded = rightOrThrow(decodeFrameManifest(listing));
    expect(decoded.frames).toHaveLength(2);
    expect(decoded.label).toBe(FRAME_MANIFEST_LABEL);
    expect(JSON.stringify(rightOrThrow(encodeFrameManifest(decoded)))).toBe(
      JSON.stringify(listing),
    );
  });

  test('latest_png_url is null — never omitted — when there are no frames', () => {
    const empty = rightOrThrow(
      decodeFrameManifest({ ...listing, frames: [], latest_png_url: null }),
    );
    expect(empty.latest_png_url).toBeNull();
    expect(Either.isLeft(decodeFrameManifest({ ...listing, latest_png_url: undefined }))).toBe(
      true,
    );
  });

  test('the route paths are the ones the dispatcher matches', () => {
    expect(archiveFramesPath(gameId)).toBe(`/v1/games/${gameId}/frames`);
    expect(archiveLatestFramePath(gameId)).toBe(`/v1/games/${gameId}/frames/latest.png`);
    expect(archiveVideoPath(gameId)).toBe(`/v1/games/${gameId}/video.mp4`);
    expect(listing.latest_png_url.endsWith('/frames/latest.png')).toBe(true);
  });

  test('a malformed game id is rejected before anything else', () => {
    expect(Either.isLeft(decodeFrameManifest({ ...listing, game_id: '../../etc/passwd' }))).toBe(
      true,
    );
  });
});

describe('archive binary metadata', () => {
  test('the on-disk name is padded, the URL segment is not', () => {
    expect(Number(rightOrThrow(decodeArchivePngName('000012.png')))).toBe(12);
    expect(rightOrThrow(encodeArchivePngName(FrameIndex.make(7)))).toBe('000007.png');
    expect(ARCHIVE_PNG_RE.test('12.png')).toBe(false);
    expect(Either.isLeft(decodeArchivePngName('12.png'))).toBe(true);
    expect(Either.isLeft(decodeArchivePngName('0000012.png'))).toBe(true);
    expect(Either.isLeft(decodeArchivePngName('latest.png'))).toBe(true);
  });

  test('an index the six-digit name cannot hold fails to encode, loudly', () => {
    const overflow = encodeArchivePngName(FrameIndex.make(1234567));
    expect(Either.isLeft(overflow)).toBe(true);
  });

  test('the padded name and the frame index agree in both directions', () => {
    const names = ['000000.png', '000001.png', '000752.png', '999999.png'];
    const indices = names.map((name) => rightOrThrow(decodeArchivePngName(name)));
    expect(indices.map(Number)).toEqual([0, 1, 752, 999999]);
    expect(indices.map((index) => rightOrThrow(encodeArchivePngName(index)))).toEqual(names);
  });

  test('content types and the immutable cache header are pinned', () => {
    expect(archiveBinaryContentType('frame')).toBe('image/png');
    expect(archiveBinaryContentType('video')).toBe('video/mp4');
    expect(ARCHIVE_BINARY_CACHE_CONTROL).toBe('public, max-age=31536000, immutable');
  });
});

describe('what a wrong shape does', () => {
  const validRow: Record<string, unknown> = {
    player_id: 0,
    player_name: 'AgentPlace1',
    player_color: '#0067A5',
    seat_id: 'place-1',
    place: 1,
    controller_label: 'pi-gpt-5.6-sol',
    controller_type: 'external',
    model: null,
    ai_difficulty: null,
    scored: true,
  };

  test('a lower-case colour is drift, not a cosmetic difference', () => {
    expect(Either.isLeft(decodeMapPlayer({ ...validRow, player_color: '#0067a5' }))).toBe(true);
    expect(Either.isLeft(decodeMapPlayer({ ...validRow, player_color: 'blue' }))).toBe(true);
  });

  test('place 0 is impossible — _public_places drops anything below 1', () => {
    expect(Either.isLeft(decodeMapPlayer({ ...validRow, place: 0 }))).toBe(true);
    expect(Either.isRight(decodeMapPlayer({ ...validRow, place: null }))).toBe(true);
  });

  test('scored is required: both producers always emit it', () => {
    const { scored: _dropped, ...withoutScored } = validRow;
    expect(Either.isLeft(decodeMapPlayer(withoutScored))).toBe(true);
  });

  test('a fractional or negative frame index is not a file name', () => {
    const frame = {
      index: 3,
      turn: 4,
      map_players: [],
      source_name: '000003.png',
      png_url: 'u',
    };
    expect(Either.isRight(decodeReplayFrame(frame))).toBe(true);
    expect(Either.isLeft(decodeReplayFrame({ ...frame, index: 3.5 }))).toBe(true);
    expect(Either.isLeft(decodeReplayFrame({ ...frame, index: -1 }))).toBe(true);
    expect(Either.isLeft(decodeReplayFrame({ ...frame, turn: 4.5 }))).toBe(true);
    expect(Either.isLeft(decodeReplayFrame({ ...frame, turn: 'four' }))).toBe(true);
  });

  test('every issue is reported, not just the first', () => {
    const broken = decodeReplayFrame({ index: -1, turn: 1.5, source_name: 5, png_url: 'u' });
    const error = Either.isLeft(broken) ? broken.left : undefined;
    expect(error?.schemaName).toBe('ReplayFrame');
    expect((error?.issues.length ?? 0) >= 3).toBe(true);
  });

  test('a watch payload missing its video block fails', () => {
    const { video: _dropped, ...withoutVideo } = terminalRaw;
    expect(Either.isLeft(decodeWatchResponse(withoutVideo))).toBe(true);
  });
});

describe('tolerance: tomorrow’s fields survive today’s schema', () => {
  test('an unknown key on a frame and on a map player is preserved', () => {
    const decoded = rightOrThrow(
      decodeReplayFrame({
        index: 1,
        turn: 2,
        map_players: [
          {
            player_id: 0,
            player_name: 'A',
            player_color: '#0067A5',
            seat_id: 'place-1',
            place: 1,
            controller_label: 'x',
            controller_type: 'external',
            scored: true,
            nation: 'Roman',
          },
        ],
        source_name: 'turn-0002-x.map.ppm',
        png_url: 'u',
        thumbnail_url: 'later',
      }),
    );
    const shipped: ReplayFrame = decoded;
    // Encode before stringifying: a *decoded* gateway body carries `bigint`
    // integers, which `JSON.stringify` refuses outright. Going back through
    // the encoder is also what a real producer would do, so this now proves
    // the unknown keys survive the whole round trip rather than just the
    // decode half.
    expect(JSON.parse(JSON.stringify(rightOrThrow(encodeReplayFrame(shipped))))).toMatchObject({
      thumbnail_url: 'later',
      map_players: [{ nation: 'Roman' }],
    });
  });

  test('the game block keeps every status field it does not name', () => {
    const encoded = rightOrThrow(decodeJsonObject(rightOrThrow(encodeWatchResponse(running))));
    expect(Object.keys(gameOf(encoded))).toEqual(Object.keys(gameOf(runningRaw)));
    // `phase` is a v2-only block the archive path never emits and this schema
    // never names; it has to survive anyway.
    expect(jsonField(gameOf(encoded), 'phase')).toBeDefined();
    expect(terminal.game.game_id).toBe(GameId.make('game_ieTomdES08hpUmFRFzCOAVMo'));
    expect(running.game.state).toBe('running');
  });
});
