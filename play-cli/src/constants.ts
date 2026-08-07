/**
 * Every wire-level constant and regex the port shares.
 *
 * Ports `play/client.py` 38-165, 1990-1997, 2370-2371, 3083-3091, 3788-3815,
 * 4146, 4414-4688, 5183, 5807-5823, 6765 and 6973-6974.  Constants that belong
 * to exactly one command live with that command's unit; anything two units read
 * lives here.
 *
 * The regexes are `RegExp` objects with anchors baked in, because Python's
 * `fullmatch` has no JavaScript equivalent: every pattern below is written
 * `^…$` and matched with `.test()`.
 */

// ---------------------------------------------------------------------------
// client.py:38-49 — identity and protocol
// ---------------------------------------------------------------------------

export const DEFAULT_SERVICE_URL = 'http://127.0.0.1:8765';
export const FULL_CONTROL_V2 = 'full-control-v2';
export const STRATEGIC_V1 = 'strategic-v1';

export const GAME_ID_RE = /^game_[A-Za-z0-9_-]{20,80}$/;
export const CONTROLLER_RE = /^[A-Za-z0-9][A-Za-z0-9._-]{2,95}$/;

export const TERMINAL_STATES: ReadonlySet<string> = new Set([
  'completed',
  'invalid',
  'failed',
  'cancelled',
]);

/**
 * One workspace plays one seat.  `join` records that seat here so no later
 * command has to name it; `use` rebinds it.  The file holds a path and a game
 * ID only — never a bearer token.
 */
export const SEAT_BINDING_NAME = 'current-seat.json';

// ---------------------------------------------------------------------------
// client.py:48-55 — receipt and error vocabularies
// ---------------------------------------------------------------------------

export const V2_RECEIPT_STATES: ReadonlySet<string> = new Set([
  'accepted',
  'applied',
  'rejected',
  'ambiguous',
]);

export const V2_TERMINAL_RECEIPTS: ReadonlySet<string> = new Set([
  'applied',
  'rejected',
  'ambiguous',
]);

export const V2_ERROR_CODES: ReadonlySet<string> = new Set([
  'action_expired',
  'action_outcome_ambiguous',
  'conflict',
  'cursor_expired',
  'illegal_action',
  'internal_error',
  'invalid_batch',
  'invalid_request',
  'not_implemented',
  'rate_limited',
  'scope_too_large',
  'sidecar_unavailable',
  'stale_revision',
  'unsupported_protocol',
]);

// ---------------------------------------------------------------------------
// client.py:56-85 — sections, pages and byte caps
// ---------------------------------------------------------------------------

export const V2_SECTIONS: ReadonlySet<string> = new Set([
  'overview',
  'pregame_nations',
  'pregame_styles',
  'pregame_teams',
  'chat_recipients',
  'votes',
  'research',
  'governments',
  'diplomacy',
  'diplomacy_clauses',
  'known_tiles',
  'map_tiles',
  'infrastructure',
  'cities',
  'city_sites',
  'units',
  'multipliers',
  'spaceship',
  'tombstones',
  'chat',
  'city_detail',
  'city_citizens',
  'city_build_choices',
  'city_worklist',
  'city_improvements',
  'city_worker_tasks',
  'city_trade_routes',
  'tile_window',
  'city_governor',
  'unit_route',
]);

export const V2_CITY_SECTIONS: ReadonlySet<string> = new Set([
  'city_detail',
  'city_citizens',
  'city_build_choices',
  'city_worklist',
  'city_improvements',
  'city_worker_tasks',
  'city_trade_routes',
  'city_governor',
]);

export const V2_TURN_SECTIONS = ['overview', 'cities', 'units', 'research'] as const;
export const V2_TURN_PAGE_LIMIT = 16;
export const V2_LEGAL_MATCH_LIMIT = 64;

/**
 * One actor's whole catalog is the `--actor_id ... --all` promise; a real unit
 * catalog runs past 64 rows, so the actor form is bounded by the byte cap
 * alone unless the agent asks for a window with `--limit`.
 */
export const V2_LEGAL_ACTOR_MATCH_LIMIT = 4096;

/** Stand-in printed for a subject key whose value must not enter agent context. */
export const V2_WITHHELD = '<withheld>';

export const V2_LEGAL_DRAIN_MAX_PAGES = 512;
export const V2_LEGAL_COMPACT_MAX_BYTES = 48 * 1024;
export const V2_LEGAL_SINGLE_ACTION_MAX_BYTES = 64 * 1024;

/** The server never returns more than this many items in one page envelope. */
export const V2_PAGE_MAX_ITEMS = 16;

// ---------------------------------------------------------------------------
// client.py:86-99 — phase and sidecar shapes
// ---------------------------------------------------------------------------

export const V2_PHASE_STATES: ReadonlySet<string> = new Set([
  'synchronizing',
  'native_phase',
  'phase_not_ready',
  'inactive_done',
  'awaiting_agent',
  'ending',
  'ambiguous_ending',
  'terminalizing',
]);

export const V2_SIDECAR_FIELDS: ReadonlySet<string> = new Set([
  'state',
  'generation',
  'player_name',
  'started_at',
  'ready_at',
  'last_seen_at',
  'stopped_at',
  'exit_code',
  'error_code',
  'client_state',
  'server_connected',
  'seat_state',
  // Death forensics: these separate "the client crashed" from "the client is
  // alive and merely slow" on the server side.
  'exit_signal',
  'exit_signal_name',
  'process_alive',
]);

// ---------------------------------------------------------------------------
// client.py:100-126 — opaque IDs and the client-side alias dialect
// ---------------------------------------------------------------------------

export const OPAQUE_ID_RE = /^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$/;
export const ACTION_KIND_RE = /^[a-z][a-z0-9_]*\.[a-z][a-z0-9_]*$/;

/**
 * `--kind` must accept exactly what the kind column prints.  That column shows
 * `unit.order/move` whenever the operation is not already the kind's own tail,
 * so the selector is the public kind with an optional `/operation` suffix; a
 * bare kind still means "every operation under this kind".
 */
export const ACTION_KIND_SELECTOR_RE =
  /^[a-z][a-z0-9_]*\.[a-z][a-z0-9_]*(?:\/[a-z][a-z0-9_]*)?$/;

export const CURSOR_RE = /^cursor_[A-Za-z0-9_-]{32}$/;
export const CATALOG_RE = /^catalog_[A-Za-z0-9_-]{32}$/;
export const CITY_ID_RE = /^city_[0-9a-f]{32}$/;
export const TILE_ID_RE = /^tile_[0-9a-f]{32}$/;
export const ACTOR_ID_RE = /^(?:player|city|unit)_[0-9a-f]{32}$/;
export const RELATION_ID_RE = /^relation_[0-9a-f]{32}$/;
export const PLAYER_COLOR_RE = /^#[0-9A-F]{6}$/;

/**
 * Aliases exist only in this process and in the private cache; every request
 * still carries the server's opaque ID.
 */
export const ACTION_ALIAS_RE = /^a([1-9][0-9]{0,3})$/;
export const ENTITY_ALIAS_RE = /^([ucpr])([1-9][0-9]{0,3})$/;
export const TILE_ALIAS_RE = /^[Tt]\((-?[0-9]{1,4}), ?(-?[0-9]{1,4})\)$/;
export const TILE_KEY_RE = /^(-?[0-9]{1,4}),(-?[0-9]{1,4})$/;

export const ALIAS_ENTITY_PREFIXES: Readonly<Record<string, string>> = {
  unit: 'u',
  city: 'c',
  player: 'p',
  relation: 'r',
};

export const ALIAS_ENTITY_TYPES: Readonly<Record<string, string>> = {
  u: 'unit',
  c: 'city',
  p: 'player',
  r: 'relation',
};

export const ALIAS_ENTITY_KEYS = ['id', 'relation_id', 'player_id'] as const;

// ---------------------------------------------------------------------------
// client.py:127-140 — cache caps, lock timeouts and dispositions
// ---------------------------------------------------------------------------

export const V2_MAX_ACTION_ALIASES = 8192;
/**
 * One alias entry's semantic identity: actor, kind, operation, normalized
 * target and argument-schema shape.  Bounded so a drifted cache cannot grow the
 * private state file without limit.
 */
export const V2_MAX_ALIAS_SEMANTICS = 1024;
export const V2_MAX_ENTITY_ALIASES = 4096;
export const V2_MAX_TILE_ALIASES = 4096;
export const V2_MAX_DRAINED_ACTORS = 4096;
export const V2_MAX_PENDING_CATALOGS = 128;

export const V2_STATE_LOCK_TIMEOUT_S = 5.0;
export const V2_REQUEST_LOCK_TIMEOUT_S = 45.0;

export const V2_DISPOSITIONS: ReadonlySet<string> = new Set([
  'receipt_terminal',
  'receipt_poll',
  'receipt_first',
  'retry_exact',
  'refresh',
]);

// ---------------------------------------------------------------------------
// client.py:141-165 — wake reasons, wait exit codes, evaluation context
// ---------------------------------------------------------------------------

export const V2_WAKE_REASONS: ReadonlySet<string> = new Set([
  'phase_active',
  'game_terminal',
  'revision_changed',
  'timeout',
  // `boundary_recovered` answers any wait that lands while this seat's native
  // boundary is being republished.
  'boundary_recovered',
]);

/**
 * The wake reasons that mean the wait got what it was asked for.  Everything
 * else is either the game ending or the caller being told to come back.
 */
export const V2_SATISFIED_WAKE_REASONS: ReadonlySet<string> = new Set([
  'phase_active',
  'revision_changed',
  'boundary_recovered',
]);

export const V2_EVALUATION_FIELDS: ReadonlySet<string> = new Set([
  'objective',
  'max_turns',
  'turns_remaining',
]);

// ---------------------------------------------------------------------------
// client.py:1990-1997 — recovery events
// ---------------------------------------------------------------------------

export const V2_RECOVERY_FIELDS: ReadonlySet<string> = new Set([
  'attempt',
  'client_state',
  'exit_code',
  'exit_signal',
  'format',
  'game_id',
  'kind',
  'outcome',
  'place',
  'recovered_to_turn',
  'rewound_applied_actions',
  'schema_version',
  'seat_id',
  'sidecar_generation',
  'timestamp',
  'trigger',
  'turn',
]);

export const V2_RECOVERY_KINDS: ReadonlySet<string> = new Set([
  'sidecar_reattach',
  'autosave_rollback',
]);

export const V2_RECOVERY_OUTCOMES: ReadonlySet<string> = new Set([
  'recovered',
  'failed',
  'abandoned',
]);

// ---------------------------------------------------------------------------
// client.py:2370-2371 — busy retry
// ---------------------------------------------------------------------------

/**
 * A busy native sidecar clears in well under a second (it is one event-loop
 * tick), and the server marks the refusal retryable.  Retrying a read inside
 * the same command spares the caller a whole model round trip; mutations are
 * never retried here — their contract is receipt-first.
 */
export const V2_BUSY_RETRIES = 3;
export const V2_BUSY_BACKOFF_S = 0.35;

// ---------------------------------------------------------------------------
// client.py:3083-3091 — the JSON escape hatch
// ---------------------------------------------------------------------------

/**
 * Commands whose *success* output is JSON and which therefore declare no
 * `--json` flag.  Their refusals must stay JSON too: a machine consumer polling
 * `just wait` in a loop has no flag with which to turn prose back off.
 */
export const V2_JSON_ONLY_COMMANDS: ReadonlySet<string> = new Set([
  'act',
  'next',
  'result',
]);

export const V2_JSON_ENVIRONMENT = 'PLAY_JSON';

export const V2_TRUE_STRINGS: ReadonlySet<string> = new Set(['1', 'on', 'true', 'yes']);

// ---------------------------------------------------------------------------
// client.py:3788-3815 — catalog collapse
// ---------------------------------------------------------------------------

/**
 * Kinds a catalog carries for completeness rather than for a decision.  Each
 * entry names the family the summary line leads with and the plural noun it
 * counts.
 */
export const V2_CATALOG_HOUSEKEEPING: ReadonlyMap<string, readonly [string, string]> =
  new Map<string, readonly [string, string]>([
    ['player.propose_server_setting', ['governance', 'setting proposals']],
    ['player.propose_server_setting_boolean', ['governance', 'boolean setting proposals']],
    ['player.propose_server_setting_integer', ['governance', 'integer setting proposals']],
    ['player.propose_server_setting_string', ['governance', 'string setting proposals']],
    ['player.propose_server_setting_enum', ['governance', 'enum setting proposals']],
    ['player.propose_server_setting_bitwise', ['governance', 'bitwise setting proposals']],
    ['player.cast_vote', ['governance', 'ballots']],
    ['player.cancel_vote', ['governance', 'vote withdrawals']],
    ['player.send_chat', ['chat', 'chat recipients']],
  ]);

/**
 * Kinds whose rows differ only by which named choice they select.  The names
 * are the whole decision, so they render inline on one row instead of one row
 * each.
 */
export const V2_CATALOG_CHOICE_KINDS: ReadonlySet<string> = new Set([
  'research.set_goal',
  'research.set_target',
]);

/** A single row is never worth a summary line: collapsing starts at two. */
export const V2_CATALOG_COLLAPSE_MIN = 2;
export const V2_CATALOG_LINE_MAX = 200;

// ---------------------------------------------------------------------------
// client.py:4146 — hidden kinds
// ---------------------------------------------------------------------------

export const V2_HIDDEN_KIND_MAX = 8;

// ---------------------------------------------------------------------------
// client.py:4414-4429 — terrain codes
// ---------------------------------------------------------------------------

export const TERRAIN_CODES: ReadonlyMap<string, string> = new Map([
  ['Arctic', 'Ar'],
  ['Deep Ocean', 'Do'],
  ['Desert', 'De'],
  ['Forest', 'Fo'],
  ['Glacier', 'Gl'],
  ['Grassland', 'Gr'],
  ['Hills', 'Hi'],
  ['Inaccessible', 'In'],
  ['Jungle', 'Ju'],
  ['Lake', 'La'],
  ['Mountains', 'Mo'],
  ['Ocean', 'Oc'],
  ['Plains', 'Pl'],
  ['Swamp', 'Sw'],
  ['Tundra', 'Tu'],
]);

// ---------------------------------------------------------------------------
// client.py:4665-4688 — the city output chain
// ---------------------------------------------------------------------------

export const V2_CITY_OUTPUTS = [
  'food',
  'shields',
  'trade',
  'gold',
  'luxury',
  'science',
] as const;

/**
 * The wire's own derivation chain, left to right: the citizens' raw tile and
 * specialist yield, the multiplied gross, what waste and disorder take off it,
 * and what upkeep leaves as the surplus.
 */
export const V2_CITY_OUTPUT_COLUMNS: ReadonlyArray<readonly [string, string]> = [
  ['base', 'citizen_base'],
  ['gross', 'gross'],
  ['waste', 'waste'],
  ['unhappy', 'unhappy_penalty'],
  ['net', 'net'],
  ['used', 'usage'],
  ['surplus', 'surplus'],
];

export const V2_CITY_OUTPUT_CHAIN = ['base', 'gross', 'net', 'surplus'] as const;

export const V2_CITY_DRILLDOWNS: ReadonlyArray<readonly [string, string, string]> = [
  ['citizen_tiles', 'tile yields', 'city_citizens'],
  ['build_choices', 'build choices', 'city_build_choices'],
  ['improvements', 'improvements', 'city_improvements'],
  ['worklist', 'worklist entries', 'city_worklist'],
  ['trade_routes', 'trade routes', 'city_trade_routes'],
];

export const V2_CITY_ROW_MAX = 118;

// ---------------------------------------------------------------------------
// client.py:5183 — build choices
// ---------------------------------------------------------------------------

export const V2_BUILD_CHOICE_KEYS = ['id', 'city_id', 'kind', 'name', 'cost'] as const;

// ---------------------------------------------------------------------------
// client.py:5807-5823 — the error remedy table and cursor restart
// ---------------------------------------------------------------------------

export const V2_ERROR_REMEDIES: ReadonlyMap<string, string> = new Map([
  [
    'refresh',
    're-read the actor with `just legal --actor_id ID --all`, then re-issue the order',
  ],
  ['retry_exact', 'retry the same batch with `just retry --batch_id ID`'],
  [
    'receipt_first',
    'resolve the outcome with `just receipt --batch_id ID` before any replay',
  ],
]);

/**
 * The page-restart query a retired cursor forwards, in the order the recipe
 * takes it.  A key outside this set means the server described a page this
 * client cannot spell, and the remedy is withheld rather than guessed at.
 */
export const V2_RESTART_OPTIONS = [
  'section',
  'actor_id',
  'target_id',
  'relation_id',
  'center_id',
  'radius',
  'limit',
] as const;

export const V2_RESTART_COMMANDS: ReadonlyMap<string, string> = new Map([
  ['state', 'just state'],
  ['legal_actions', 'just legal'],
]);

// ---------------------------------------------------------------------------
// client.py:6765 — phase end
// ---------------------------------------------------------------------------

export const PHASE_END_REMEDY =
  'the phase may not be yours to end yet -- run `just turn` and check that the phase is active';

// ---------------------------------------------------------------------------
// client.py:6973-6974 — decision rows
// ---------------------------------------------------------------------------

export const V2_DECISION_MAX_OPTIONS = 5;
export const V2_DECISION_MAX_ROWS = 40;

// ---------------------------------------------------------------------------
// The 18 subcommands `play` exposes, in the Python's `parser()` order.
// ---------------------------------------------------------------------------

export const COMMAND_NAMES = [
  'prompt',
  'join',
  'use',
  'next',
  'act',
  'health',
  'turn',
  'start',
  'do',
  'show',
  'state',
  'legal',
  'batch',
  'receipt',
  'retry',
  'wait',
  'monitor',
  'result',
  // The two doc surfaces the justfile carried as recipes; PLAN folds them in.
  'help',
  'rules',
] as const;

export type CommandName = (typeof COMMAND_NAMES)[number];
