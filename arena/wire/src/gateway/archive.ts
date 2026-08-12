/**
 * Archive routes: the frame listing, `watch.json`, and the on-disk metadata
 * behind the binary routes.
 *
 * ## What this module covers
 *
 * | Route | Producer (disk) | Producer (upstream) |
 * |---|---|---|
 * | `GET /v1/games/{id}/frames` | `_archive_frames`, `agent_eval/replay_gateway.py:968-1004` | `Game.frame_manifest`, `agent_eval/supervisor.py:10206-10244` |
 * | `GET /v1/games/{id}/watch.json` | `_archive_watch`, `replay_gateway.py:1033-1063` | `Game.watch_state`, `supervisor.py:9613-9633` |
 * | `GET /v1/games/{id}/frames/{n}.png`, `/frames/latest.png`, `/video.mp4` | `_archive_binary_route`, `replay_gateway.py:1935-1963` | streamed through |
 *
 * The gateway is a merging proxy: when the supervisor answers 2xx its bytes
 * are relayed **verbatim**, and only when it answers 404/405 (or is
 * unreachable) does the gateway synthesize the payload from
 * `runs_root/<game_id>/`.  So every schema here decodes *two* producers, and
 * the differences are the interesting part:
 *
 * - `video.kind` is `"archived-video"` on the disk path and `"video-so-far"`
 *   upstream ({@link watchProducer} turns that into a branch).
 * - `timeline` entries carry only `turn` on the disk path
 *   (`replay_gateway.py:1038-1042`) and the full live record — `year`,
 *   `responded_seats`, `timed_out_seats`, `resolved_at` — upstream
 *   (`supervisor.py:8783-8790`).
 * - a *matched* `map_players` row has all six identity keys present on the
 *   disk path (possibly `null`), while upstream omits `ai_difficulty` for an
 *   external seat and omits `model` **and** `ai_difficulty` for an unmatched
 *   ("dynamic") faction.  Hence {@link MapPlayer} spells those two
 *   `Schema.optional(Schema.NullOr(...))` and the other four required-nullable.
 *
 * ## Three lies of omission in the archive `watch.json` (dossier T9)
 *
 * 1. `replay.available` is **hardcoded `true`** on the disk path
 *    (`replay_gateway.py:1055`) whether or not any replay telemetry exists.
 *    Never treat it as evidence; ask `/replay.json`.
 * 2. `timeline` silently drops every frame whose `turn` could not be paired,
 *    so `timeline.length <= frames.length` and the timeline is *not* an index
 *    into `frames`.
 * 3. A frame's `turn` comes from **positional** pairing: PNG index `i` is
 *    matched with the `i`-th `saves/turn-<n>-*.map.ppm` sorted by file name
 *    (`replay_gateway.py:975-982`).  One missing PPM shifts every later label.
 *    The viewer defends itself by re-deriving the turn from `source_name`
 *    (`agent_eval/viewer/src/api.ts:84-90`), which is why {@link ReplayFrame}
 *    keeps `source_name` verbatim and {@link legacyFrameTurn} ports that
 *    fallback exactly.
 *
 * A fourth, cheaper surprise: **frame indices are sparse**.  `_archive_frames`
 * enumerates whatever PNGs survive in `watch_frames/`, so a pruned archive
 * legitimately reports indices `[0, 752]` — the index is a file name, not an
 * offset into the array, and `index + 1 === turn` is a coincidence of a
 * complete archive.
 *
 * ## Conventions this module follows
 *
 * - **No server-chosen string is spelled as a `Schema.Literal`.**  `label`,
 *   `video.kind` and `schema_version` are hardcoded in Python today, but
 *   locking them in the schema would turn a harmless server bump into a failed
 *   decode — exactly the failure `decodeTolerant` exists to prevent.  The
 *   expected values are exported as constants instead, so drift is a *branch*
 *   a caller can take, not an exception.  *Formats* (a `#RRGGBB` colour, a
 *   `NNNNNN.png` file name, a game id) are refined, because those are
 *   machine-generated and a violation means the producer changed shape.
 * - **Omitted and `null` are different values**, so an optional-and-nullable
 *   field is `Schema.optional(Schema.NullOr(X))` rather than
 *   `Schema.optionalWith(X, { nullable: true })`: the latter decodes `null` to
 *   *absent*, which drops the key on re-encode and breaks the round-trip
 *   guarantee this package sells.
 * - **Integers decode as `bigint`**, via `WireInt` in `../numeric.ts`, like
 *   every other `Gateway` module.  Every archive body goes out through
 *   `_json`→`_canonical`, so a `number` here would re-canonicalize as
 *   `"turn":7.0` where Python wrote `"turn":7` — the silent digest mismatch
 *   `../numeric.ts` opens by warning about.  `{@link FrameIndex}` in
 *   `../ids.ts` stays a `number` deliberately: it is a *path segment* and a
 *   loop counter, never a field in a canonicalized body.
 *
 * @module
 */

import { Option, Schema } from 'effect';
import { FrameIndex, GameId } from '../ids.ts';
import { WireInt, WireNonNegativeInt } from '../numeric.ts';
import {
  decodeTolerant,
  encodeTolerant,
  type TolerantDecoder,
  type TolerantEncoder,
} from '../tolerant.ts';
import { GameStatus } from './games.ts';

// ---------------------------------------------------------------------------
// Constants the producers hardcode
// ---------------------------------------------------------------------------

/**
 * `schema_version` on every archive payload — `replay_gateway.py:996` and
 * `:1049`, `supervisor.py:10222`.  Not a discriminator: nothing branches on it
 * yet, and no payload has ever carried another value.
 */
export const ARCHIVE_SCHEMA_VERSION = 1;

/** `label` on the frame listing (`replay_gateway.py:998`, `supervisor.py:10224`). */
export const FRAME_MANIFEST_LABEL = 'Omniscient strategic map snapshots (not GUI video)';

/** `label` on `watch.json` (`replay_gateway.py:1050`, `supervisor.py:9621`). */
export const WATCH_LABEL = 'Omniscient Freeciv agent match replay';

/** `video.kind` when the payload was built from the archive (`replay_gateway.py:1061`). */
export const ARCHIVED_VIDEO_KIND = 'archived-video';

/** `video.kind` when the payload came from the live supervisor (`supervisor.py:9632`). */
export const LIVE_VIDEO_KIND = 'video-so-far';

/** Identity label given to a PPM faction with no configured place (`replay_gateway.py:951`). */
export const DYNAMIC_CONTROLLER_LABEL = 'Freeciv dynamic faction';

/** `controller_type` on that same unmatched row (`replay_gateway.py:952`). */
export const DYNAMIC_CONTROLLER_TYPE = 'dynamic';

/** `seat_id` prefix synthesized for an unmatched faction (`replay_gateway.py:949`). */
export const DYNAMIC_SEAT_ID_PREFIX = 'dynamic-player-';

// ---------------------------------------------------------------------------
// On-disk archive layout (what the binary routes read)
// ---------------------------------------------------------------------------

/** Directory holding the rendered frames, `runs_root/<id>/watch_frames` (`:969`). */
export const ARCHIVE_FRAMES_DIRECTORY = 'watch_frames';

/** Directory holding the autosaves the frames are labelled from (`:970`). */
export const ARCHIVE_SAVES_DIRECTORY = 'saves';

/** The archived video file, probed by `_archive_video_path` (`:1023`). */
export const ARCHIVE_VIDEO_FILE = 'game.mp4';

/** `Content-Type` for a frame (`_archive_binary_route`, `:1962`). */
export const ARCHIVE_FRAME_CONTENT_TYPE = 'image/png';

/** `Content-Type` for the video (`:1962`). */
export const ARCHIVE_VIDEO_CONTENT_TYPE = 'video/mp4';

/**
 * `Cache-Control` on every archive binary (`_send_local_file`, `:1510`).  The
 * gateway-built JSON routes use `no-store` instead, so a port must not share
 * one header table between them.
 */
export const ARCHIVE_BINARY_CACHE_CONTROL = 'public, max-age=31536000, immutable';

/**
 * `ARCHIVE_PNG_RE` — `replay_gateway.py:37`: `^([0-9]{6})\.png$`.
 *
 * The *on-disk* frame name, always zero-padded to six digits.  Note the
 * asymmetry with `FRAME_INDEX_RE` (`../ids.ts`), which governs the *URL*
 * segment and forbids padding: `watch_frames/000007.png` is served at
 * `/frames/7.png`, and `/frames/007.png` is a 404.
 */
export const ARCHIVE_PNG_RE: RegExp = /^([0-9]{6})\.png$/;

/**
 * `ARCHIVE_PPM_RE` — `replay_gateway.py:38`: `^turn-(\d+)-.*\.map\.ppm$`.
 *
 * Matched with `fullmatch` against an autosave name in `saves/`.  The capture
 * is the turn the gateway attaches to the frame at the same *position* — see
 * the module doc, lie #3.
 */
export const ARCHIVE_PPM_RE: RegExp = /^turn-(\d+)-.*\.map\.ppm$/;

/**
 * `FRAME_TURN_RE` — `supervisor.py:177`:
 * `(?:^|[^A-Za-z0-9])turn-(\d+)(?:-|[^0-9]|$)`.
 *
 * The supervisor's own name→turn rule, applied with `re.search` (not
 * `fullmatch`) to any `*.ppm` under the episode.  It is deliberately laxer
 * than {@link ARCHIVE_PPM_RE}: `snap.turn-12.map.ppm` yields `12` upstream and
 * yields nothing on the disk path, which is one way the same archive can label
 * the same frame differently depending on who served it.
 */
export const SUPERVISOR_FRAME_TURN_RE: RegExp = /(?:^|[^A-Za-z0-9])turn-(\d+)(?:-|[^0-9]|$)/;

/**
 * The viewer's third rule (`agent_eval/viewer/src/api.ts:84-90`):
 * `^turn-(\d+)(?:-|\.|$)`, used only when `frame.turn` is missing.  Anchored,
 * unlike the supervisor's.  Ported by {@link legacyFrameTurn}.
 */
export const LEGACY_FRAME_TURN_RE: RegExp = /^turn-(\d+)(?:-|\.|$)/;

/**
 * The on-disk frame file name, `"000012.png"`, as a frame index.
 *
 * Encoding pads back to six digits, so a decode/encode round trip is the
 * identity — and an index above `999999` fails to *encode*, because the name
 * it would produce is one `ARCHIVE_PNG_RE` rejects.  That is the real archive
 * limit surfacing as an error value rather than as a silently unreachable
 * file.
 */
export const ArchivePngName: Schema.Schema<FrameIndex, string> = Schema.transform(
  Schema.String.pipe(
    Schema.pattern(ARCHIVE_PNG_RE, {
      identifier: 'ArchivePngName',
      description: 'Zero-padded archive frame file name, e.g. "000012.png" (ARCHIVE_PNG_RE)',
    }),
  ),
  FrameIndex,
  {
    strict: true,
    // Total: the pattern admits six digits and nothing else.
    decode: (name) => FrameIndex.make(Number(name.slice(0, 6))),
    encode: (index) => `${String(index).padStart(6, '0')}.png`,
  },
).annotations({ identifier: 'ArchivePngName' });

/** Decode a `watch_frames/` file name as a {@link FrameIndex}. */
export const decodeArchivePngName: TolerantDecoder<FrameIndex> = decodeTolerant(ArchivePngName);

/** Encode a {@link FrameIndex} back to its zero-padded on-disk file name. */
export const encodeArchivePngName: TolerantEncoder<FrameIndex, string> =
  encodeTolerant(ArchivePngName);

/**
 * The turn an autosave name declares, per {@link ARCHIVE_PPM_RE} — `None` in
 * Python, {@link Option.none} here.  Not a `Schema.transform`, because the
 * mapping is one-way: the middle of the name (`-M-bc--tuZ1Pall`) is not
 * recoverable from a turn number.
 */
export const archivePpmTurn = (name: string): Option.Option<number> =>
  Option.map(Option.fromNullable(ARCHIVE_PPM_RE.exec(name)?.[1]), Number);

// ---------------------------------------------------------------------------
// URL paths
// ---------------------------------------------------------------------------

/** `/v1/games/{id}/frames` — the frame listing (`replay_gateway.py:2019`). */
export const archiveFramesPath = (gameId: GameId): string => `/v1/games/${gameId}/frames`;

/**
 * `/v1/games/{id}/frames/{n}.png` — the tail of a frame's `png_url`
 * (`replay_gateway.py:991-993`).  Unpadded, per `FRAME_INDEX_RE`.
 */
export const archiveFramePngPath = (gameId: GameId, index: FrameIndex): string =>
  `${archiveFramesPath(gameId)}/${index}.png`;

/**
 * `/v1/games/{id}/frames/latest.png` — the tail of `latest_png_url`.  Resolves
 * to `max(index)` on the disk path (`_archive_frame_path`, `:1017`) and to the
 * *last* frame upstream (`Game.png_frame`, `supervisor.py:10251`); those agree
 * only while the indices are dense.
 */
export const archiveLatestFramePath = (gameId: GameId): string =>
  `${archiveFramesPath(gameId)}/latest.png`;

/** `/v1/games/{id}/video.mp4` (`replay_gateway.py:2025`). */
export const archiveVideoPath = (gameId: GameId): string => `/v1/games/${gameId}/video.mp4`;

/** `/v1/games/{id}/watch.json` (`replay_gateway.py:2016`). */
export const archiveWatchJsonPath = (gameId: GameId): string => `/v1/games/${gameId}/watch.json`;

/** Which archive binary a request is for. */
export type ArchiveBinaryKind = 'frame' | 'video';

/** `Content-Type` the gateway sends for each (`_archive_binary_route`, `:1962`). */
export const archiveBinaryContentType = (kind: ArchiveBinaryKind): string =>
  kind === 'video' ? ARCHIVE_VIDEO_CONTENT_TYPE : ARCHIVE_FRAME_CONTENT_TYPE;

// ---------------------------------------------------------------------------
// Wire shapes
// ---------------------------------------------------------------------------

/** `playerno:` from the PPM header — a native player number, never negative. */
const PpmPlayerId = WireNonNegativeInt;

/** A place number; `_public_places` drops anything below 1 (`:509`). */
const PlaceNumber = WireInt.pipe(Schema.positiveBigInt());

/** A turn label parsed out of an autosave name: `(\d+)`, so never negative. */
const FrameTurn = WireNonNegativeInt;

/**
 * Both producers format the colour as `f"#{red:02X}{green:02X}{blue:02X}"`
 * after rejecting any component outside 0-255 (`replay_gateway.py:928-943`,
 * `supervisor.py:10145-10160`), so the hex is always six **upper-case**
 * digits.  Refined rather than left as `string`: this value is generated, not
 * relayed, and a lower-case one would mean the producer changed.
 */
export const MAP_PLAYER_COLOR_RE: RegExp = /^#[0-9A-F]{6}$/;

/**
 * One faction read out of a frame's PPM header
 * (`_archive_ppm_players`, `replay_gateway.py:906-965`;
 * `Game._ppm_map_players`, `supervisor.py:10124-10182`).
 *
 * Rows are sorted by `(player_id, player_name)` and fall into two branches:
 *
 * - **matched** — the PPM name equals a configured place's `player_name`.  The
 *   disk path copies `seat_id`, `place`, `controller_label`, `controller_type`,
 *   `model`, `ai_difficulty` with `place.get(key)`, so all six keys are
 *   *present and possibly `null`*.  Upstream builds the identity instead
 *   (`_place_identity`, `supervisor.py:1027-1050`) and therefore **omits**
 *   `ai_difficulty` for an external seat.
 * - **unmatched** — a faction Freeciv invented.  `seat_id` becomes
 *   `dynamic-player-{player_id}`, `place` is `null`, `scored` is `false`, and
 *   `model`/`ai_difficulty` are **absent entirely** on both paths.
 *
 * The viewer's `MapPlayer` (`viewer/src/types.ts:108-118`) declares a `nation`
 * key no producer emits and marks `seat_id`/`place`/`controller_*` optional;
 * this schema follows the producers, and an unexpected `nation` would survive
 * on the decoded value anyway (excess properties are preserved).
 */
export const MapPlayer = Schema.Struct({
  player_id: PpmPlayerId,
  player_name: Schema.String,
  player_color: Schema.String.pipe(
    Schema.pattern(MAP_PLAYER_COLOR_RE, {
      identifier: 'MapPlayerColor',
      description: 'Upper-case #RRGGBB colour rendered from the PPM header',
    }),
  ),
  seat_id: Schema.NullOr(Schema.String),
  place: Schema.NullOr(PlaceNumber),
  controller_label: Schema.NullOr(Schema.String),
  controller_type: Schema.NullOr(Schema.String),
  model: Schema.optional(Schema.NullOr(Schema.String)),
  ai_difficulty: Schema.optional(Schema.NullOr(Schema.String)),
  scored: Schema.Boolean,
}).annotations({ identifier: 'MapPlayer' });
/** One faction in a frame's PPM header. */
export type MapPlayer = typeof MapPlayer.Type;

/** Decode one {@link MapPlayer}. */
export const decodeMapPlayer: TolerantDecoder<MapPlayer> = decodeTolerant(MapPlayer);

/**
 * True when this row was matched to a configured place — i.e. it is scored
 * rather than a faction Freeciv invented mid-game.  `scored` carries the same
 * information and is the field to trust; this guard exists so the
 * `dynamic-player-N` convention has one home.
 */
export const isDynamicMapPlayer = (player: MapPlayer): boolean =>
  player.place === null && player.controller_type === DYNAMIC_CONTROLLER_TYPE;

/**
 * One archived map snapshot (`_archive_frames`, `replay_gateway.py:983-994`;
 * `Game.frame_manifest`, `supervisor.py:10225-10238`).
 *
 * `index` is the six-digit file name, **not** a position in `frames`; `turn`
 * is `null` whenever no autosave sat at that position; `source_name` is the
 * PPM's name when one did and the PNG's name otherwise, which is why it can
 * read either `turn-0753-M-bc--tuZ1Pall.map.ppm` or `000752.png`.
 */
export const ReplayFrame = Schema.Struct({
  index: FrameIndex,
  turn: Schema.NullOr(FrameTurn),
  map_players: Schema.Array(MapPlayer),
  source_name: Schema.String,
  png_url: Schema.String,
}).annotations({ identifier: 'ReplayFrame' });
/** One archived map snapshot. */
export type ReplayFrame = typeof ReplayFrame.Type;

/** Decode one {@link ReplayFrame}. */
export const decodeReplayFrame: TolerantDecoder<ReplayFrame> = decodeTolerant(ReplayFrame);

/** Re-encode one frame row, unknown server fields included. */
export const encodeReplayFrame: TolerantEncoder<ReplayFrame, typeof ReplayFrame.Encoded> =
  encodeTolerant(ReplayFrame);

/**
 * The viewer's `legacyFrameTurn` (`viewer/src/api.ts:84-90`), ported exactly:
 * trust `frame.turn` when it is a finite non-negative number, otherwise
 * re-derive the turn from `source_name` with {@link LEGACY_FRAME_TURN_RE}.
 *
 * Keep this in any consumer that renders a turn label.  Positional pairing
 * (module doc, lie #3) makes `frame.turn` unreliable in exactly the archives
 * where a PPM went missing — the same archives where `source_name` still says
 * the truth.
 */
export const legacyFrameTurn = (frame: {
  readonly turn?: bigint | number | null | undefined;
  readonly source_name: string;
}): Option.Option<number> =>
  typeof frame.turn === 'bigint' && frame.turn >= 0n
    ? Option.some(Number(frame.turn))
    : typeof frame.turn === 'number' && Number.isFinite(frame.turn) && frame.turn >= 0
      ? Option.some(Math.trunc(frame.turn))
      : Option.map(Option.fromNullable(LEGACY_FRAME_TURN_RE.exec(frame.source_name)?.[1]), Number);

/**
 * `GET /v1/games/{id}/frames` (`_archive_frames`, `replay_gateway.py:995-1004`;
 * `Game.frame_manifest`, `supervisor.py:10221-10244`).
 *
 * `latest_png_url` is `null` — never omitted — exactly when `frames` is empty.
 * The viewer has no type for this route: it consumes `frames` through
 * `watch.json` instead, so this schema's only oracle is the Python.
 */
export const FrameManifest = Schema.Struct({
  schema_version: WireInt,
  game_id: GameId,
  label: Schema.String,
  frames: Schema.Array(ReplayFrame),
  latest_png_url: Schema.NullOr(Schema.String),
}).annotations({ identifier: 'FrameManifest' });
/** The `/frames` listing. */
export type FrameManifest = typeof FrameManifest.Type;

/** Decode a `/frames` listing. */
export const decodeFrameManifest: TolerantDecoder<FrameManifest> = decodeTolerant(FrameManifest);

/** Re-encode a `/frames` listing, unknown server fields included. */
export const encodeFrameManifest: TolerantEncoder<
  FrameManifest,
  typeof FrameManifest.Encoded
> = encodeTolerant(FrameManifest);

/**
 * One `timeline` row of `watch.json`.
 *
 * The disk path emits `{turn}` and nothing else (`replay_gateway.py:1038-1042`);
 * upstream relays the live record it appended when the turn resolved
 * (`supervisor.py:8783-8790`), which adds `year`, the two seat lists and
 * `resolved_at`.  One schema decodes both, so a consumer must treat every
 * field but `turn` as "only when the supervisor served this".
 *
 * `year` is a Freeciv game year and is **negative before AD 1**, so it is a
 * plain `WireInt` with no lower bound.
 */
export const TurnTimelineEntry = Schema.Struct({
  turn: WireNonNegativeInt,
  year: Schema.optional(Schema.NullOr(WireInt)),
  responded_seats: Schema.optional(Schema.Array(Schema.String)),
  timed_out_seats: Schema.optional(Schema.Array(Schema.String)),
  resolved_at: Schema.optional(Schema.Number),
}).annotations({ identifier: 'TurnTimelineEntry' });
/** One `timeline` row. */
export type TurnTimelineEntry = typeof TurnTimelineEntry.Type;

/** Decode one `timeline` row. */
export const decodeTurnTimelineEntry: TolerantDecoder<TurnTimelineEntry> =
  decodeTolerant(TurnTimelineEntry);

/**
 * `watch.json`'s `replay` block.  `available` is hardcoded `true` on the disk
 * path (`replay_gateway.py:1055`) and real upstream (`supervisor.py:9628`) —
 * see the module doc, lie #1.
 */
export const WatchReplayLink = Schema.Struct({
  available: Schema.Boolean,
  url: Schema.String,
}).annotations({ identifier: 'WatchReplayLink' });
/** `watch.json`'s `replay` block. */
export type WatchReplayLink = typeof WatchReplayLink.Type;

/**
 * `watch.json`'s `video` block.  `available` probes `game.mp4` for a non-empty
 * regular non-symlink file on the disk path (`replay_gateway.py:1022-1031`),
 * but upstream reports `bool(frames)` — the *frames*, not the video
 * (`supervisor.py:9631`).  So an upstream `available: true` does not promise
 * `/video.mp4` will answer 200.
 */
export const WatchVideoLink = Schema.Struct({
  available: Schema.Boolean,
  url: Schema.String,
  kind: Schema.String,
}).annotations({ identifier: 'WatchVideoLink' });
/** `watch.json`'s `video` block. */
export type WatchVideoLink = typeof WatchVideoLink.Type;

/**
 * `GET /v1/games/{id}/watch.json` (`_archive_watch`,
 * `replay_gateway.py:1033-1063`; `Game.watch_state`, `supervisor.py:9613-9633`).
 *
 * The viewer's `WatchResponse` (`viewer/src/types.ts:128-136`) marks `replay`
 * and `video` optional; no producer has ever omitted them, so they are
 * required here and a missing one is real drift worth failing on.
 */
export const WatchResponse = Schema.Struct({
  schema_version: WireInt,
  label: Schema.String,
  /**
   * The full status document (`_archive_status`, `replay_gateway.py:827-868`,
   * or `Game.status`, `supervisor.py:9527-9565`) — the same `GameStatus`
   * `/status` answers with, embedded.
   *
   * This was a two-field stub (`game_id` + `state`) behind a forward reference
   * to a `gateway/status.ts` that was never going to land: `GameStatus` shipped
   * in this same package, in `./games.ts`.  The stub cost real safety —
   * `watch.json`'s `game` block is ~30 fields, so `watch.game.current_turn` was
   * a type error for every consumer, and a corrupt embedded status (a missing
   * `outcome`, a `resolved_places` row that would fail `GamePlace`) decoded
   * clean as preserved excess.
   */
  game: GameStatus,
  timeline: Schema.Array(TurnTimelineEntry),
  frames: Schema.Array(ReplayFrame),
  replay: WatchReplayLink,
  video: WatchVideoLink,
}).annotations({ identifier: 'WatchResponse' });
/** A `watch.json` document from either producer. */
export type WatchResponse = typeof WatchResponse.Type;

/** Decode a `watch.json` document. */
export const decodeWatchResponse: TolerantDecoder<WatchResponse> = decodeTolerant(WatchResponse);

/** Re-encode a `watch.json` document, unknown server fields included. */
export const encodeWatchResponse: TolerantEncoder<
  WatchResponse,
  typeof WatchResponse.Encoded
> = encodeTolerant(WatchResponse);

/** Who built a `watch.json`: the gateway's archive reader, or the supervisor. */
export type WatchProducer = 'archive' | 'live' | 'unknown';

const WATCH_PRODUCER_BY_VIDEO_KIND: Readonly<Record<string, WatchProducer>> = {
  [ARCHIVED_VIDEO_KIND]: 'archive',
  [LIVE_VIDEO_KIND]: 'live',
};

/**
 * `video.kind` is the only self-describing field in the payload, and the two
 * producers disagree on every subtlety above, so this is the branch to take
 * before trusting `replay.available`, a `timeline` row's extra keys, or a
 * frame's `turn`.
 */
export const watchProducer = (watch: WatchResponse): WatchProducer =>
  WATCH_PRODUCER_BY_VIDEO_KIND[watch.video.kind] ?? 'unknown';
