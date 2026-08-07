/**
 * The state mirror (layer L1) — the CLI-maintained sanctioned projection of the
 * pages this seat has already received.
 *
 * U04 owns the foundation: directory layout, atomic private writes, the table
 * round trip, the revision stamp and its stale comparison, the delta digest,
 * the map parse primitives, the phase marker and the monitor log.  The section
 * renderers are U07's, the map writers U08's, the yields overlay U09's — all of
 * them import from here.
 *
 * Import from `src/services/mirror`, not from its files.
 */
export {
  CACHE_DIR,
  CONTROL_RE,
  DEFAULT_COMMAND_CARD,
  DEFAULT_PROBABILITY,
  DELTA_FILE,
  HANDLE_WIDTHS,
  HEADER_FILE,
  MAP_FILE,
  MAX_CELL,
  MAX_DELTA_LINES,
  MAX_LOG_LINE,
  MAX_ROWS,
  MIRROR_PHASE_RE,
  MISSING,
  MONITOR_LOG_FILE,
  OPTIONS_DIR,
  OVERVIEW_FILE,
  PHASE_FILE,
  PHASE_SCHEMA_VERSION,
  PY_SPACE_CLASS,
  REPLACE_ONLY,
  SECTION_TARGETS,
  SECTION_TITLES,
  STATE_DIR,
  V2_MIRROR_INACTIVE,
  V2_PHASE_STALLED,
  YIELD_COLUMNS,
  YIELD_FILE,
  aliasMap,
  appendMirror,
  cachedPhaseNote,
  cell,
  codeLength,
  codePoints,
  codeSlice,
  dig,
  fileName,
  handle,
  isBehind,
  isNewer,
  isStale,
  ljust,
  mirrorDir,
  mirrorError,
  mirrorGuard,
  mirrorPath,
  mirrorText,
  numberValue,
  pair,
  position,
  readMirror,
  revLine,
  revisionPair,
  rstrip,
  sameRevision,
  splitLines,
  staleLine,
  strip,
  textValue,
  writeMirror,
  type MirrorAliases,
  type MirrorRevision,
  type Missing,
} from 'src/services/mirror/store';

export {
  REV_LINE_RE,
  pageNote,
  parseRevLine,
  parseTable,
  tableByKey,
  tableText,
  type MirrorTable,
} from 'src/services/mirror/table';

export {
  LEGEND_SEPARATOR,
  MAP_LEGEND_PAIR_RE,
  MAP_ROW_RE,
  MAP_TERRAIN_RE,
  MAP_WINDOW_RE,
  TERRAIN_CHARS,
  TERRAIN_FALLBACK,
  knownTiles,
  mapSize,
  parseMap,
  parseTileKey,
  terrainChars,
  tileChars,
  tileKey,
  type MirrorMap,
  type TileChars,
} from 'src/services/mirror/map-parse';

export {
  DELTA_SINCE_RE,
  deltaText,
  parseDelta,
  rowChanges,
  updateDelta,
  type MirrorDelta,
} from 'src/services/mirror/delta';

export {
  announcedTuple,
  mirrorHealth,
  mirrorPhaseMarker,
  phaseMarker,
  readPhaseMarker,
  updateFromHealth,
  updatePhaseMarker,
  type Announced,
  type HealthMirrorOptions,
  type MirrorClock,
} from 'src/services/mirror/phase-marker';

export {
  appendMonitorLog,
  localTimestamp,
  mirrorMonitorLog,
} from 'src/services/mirror/monitor-log';
