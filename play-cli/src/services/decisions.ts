/**
 * The focus loop: which actors can still act, and what they can do.
 *
 * Ports `play/client.py` 6960-6994 (the vocabulary) and 7042-7503:
 *
 * - 7042-7047 `_decision_verb`
 * - 7049-7059 `_decision_option_rank`
 * - 7061-7086 `_decision_options`
 * - 7088-7103 `_tier1_word`
 * - 7105-7145 `_decision_order`
 * - 7147-7168 `_decision_actor_row`
 * - 7170-7207 `_decision_unit_rows`
 * - 7209-7246 `_decision_city_rows`
 * - 7318-7363 `_decision_meeting_rows`
 * - 7365-7379 `_decision_rows`
 * - 7436-7455 `_next_focus_line`
 * - 7458-7486 `_briefing_decision_lines`
 *
 * The heuristic, in one place:
 *
 * - a **unit** needs a decision when it has moves left, is idle, and carries no
 *   standing route or queued orders;
 * - a **city** needs one when it is building nothing, or finishes what it is
 *   building this turn;
 * - a **relation** needs one when a meeting is open and unanswered.
 *
 * Relevance of an option: the verbs a player reaches for first
 * ({@link V2_DECISION_PREFERRED}, in order), then any verb this table has never
 * heard of, then the housekeeping that merely parks an actor
 * ({@link V2_DECISION_HOUSEKEEPING}).  A ruleset verb we do not know is
 * therefore still offered ahead of sentry/disband, never dropped.
 *
 * Sources are the L1 mirror tables and the revision-scoped action cache — both
 * local files.  Nothing here opens a socket.
 */
import { Effect } from 'effect';
import {
  ENTITY_ALIAS_RE,
  V2_DECISION_MAX_OPTIONS,
  V2_DECISION_MAX_ROWS,
} from 'src/constants';
import type { PlayerError } from 'src/errors';
import {
  batchFocusCommand,
  decisionLine,
  type DecisionRow,
} from 'src/render/decisions';
import type { AliasMap, JsonObject, JsonValue } from 'src/render/primitives';
import { V2_ONE_CALL_END } from 'src/render/turn';
import { actionTargetKey, aliasMap } from 'src/services/aliases';
import { compactLegalAction, type CompactAction } from 'src/services/legal-compact';
import { playerScopeAlias } from 'src/services/legal-drain';
import { meetingGroups, meetingRemedy, openMeetings, V2_DIPLOMACY_READ } from 'src/services/meetings';
import {
  V2_TIER1_VERBS,
  compactText,
  kindTail,
  orderActor,
  orderMatch,
  orderOperation,
  orderPool,
  orderTargetKeys,
  type Tier1Verb,
} from 'src/services/orders';
import { PrivateFs } from 'src/services/private-fs';
import type { V2ClientState } from 'src/services/session-store';
import { mirrorCell, mirrorFile, mirrorNumber, mirrorTable } from 'src/services/turn-pages';

// ---------------------------------------------------------------------------
// The compacted action this projection reads
// ---------------------------------------------------------------------------

/**
 * What the focus loop needs from the order engine (U15) and `legal` (U11).
 *
 * Both were written in parallel with this unit, so their entry points arrive as
 * functions rather than as direct imports: it keeps every projection below
 * testable against a hand-built cache, and it is the seam `do` (U16) reuses.
 * {@link liveDecisionDeps} binds them to the landed implementations.
 */
export interface DecisionDeps {
  /** `_order_pool` — every cached descriptor still naming the newest revision. */
  readonly orderPool: (
    state: V2ClientState
  ) => Effect.Effect<ReadonlyArray<CompactAction>, PlayerError>;
  /** `_order_actor` — the opaque actor ID one compact acts for. */
  readonly orderActor: (compact: CompactAction) => string;
  /** `_order_operation` — the subject's operation, or `""`. */
  readonly orderOperation: (compact: CompactAction) => string;
  /** `_order_target_keys` — the target keys a coordinate word could name. */
  readonly orderTargetKeys: (token: string) => ReadonlyArray<string>;
  /** `_order_match` — the arguments this action would take, or `null`. */
  readonly orderMatch: (
    compact: CompactAction,
    values: ReadonlyArray<string>,
    defaults: Readonly<Record<string, JsonValue>>
  ) => Effect.Effect<JsonObject | null, PlayerError>;
  /** `V2_TIER1_VERBS` — the six words `do` accepts that no catalog prints. */
  readonly tier1Verbs: Readonly<Record<string, Tier1Verb>>;
  /** `_player_scope_alias` — this seat's own player actor, however it is known. */
  readonly playerScopeAlias: (state: V2ClientState) => Effect.Effect<string, PlayerError>;
}

/**
 * The landed U11/U15 implementations, bound.
 *
 * `orderMatch` degrades a *matcher* failure to "no match" rather than to a
 * refusal: CPython's `_order_match` cannot raise, so a port that can must not
 * turn a briefing into an error over one unmatchable option.
 */
export const liveDecisionDeps: DecisionDeps = {
  orderPool: (state) => orderPool(state, { compactLegalAction }),
  orderActor,
  orderOperation,
  orderTargetKeys,
  orderMatch: (compact, values, defaults) =>
    Effect.orElseSucceed(orderMatch(compact, values, defaults), (): JsonObject | null => null),
  tier1Verbs: V2_TIER1_VERBS,
  playerScopeAlias,
};

// ---------------------------------------------------------------------------
// The vocabulary (client.py:6977-6994)
// ---------------------------------------------------------------------------

/** The verbs a player reaches for first, in order. */
export const V2_DECISION_PREFERRED: ReadonlyArray<string> = [
  'found_city',
  'found',
  'set_production',
  'buy_production',
  'set_worklist',
  'build',
  'set_route',
  'attack_route',
  'connect_route',
  'goto',
  'goto_and_perform',
  'move',
  'start_activity',
  'work_tile',
  'change_worker_task',
  'request_worker_task',
  'set_rally',
  'join_city',
  'establish_trade',
  'help_wonder',
  'upgrade',
  'attack',
  'set_target',
  'set_goal',
  'set_rates',
  'assign_citizen',
  'place_infrastructure',
];

/** The verbs that merely park an actor; they rank last, never dropped. */
export const V2_DECISION_HOUSEKEEPING: ReadonlySet<string> = new Set([
  'sentry',
  'fortify',
  'disband',
  'disband_recover',
  'cancel_orders',
  'cancel_activity',
  'cancel_automation',
  'clear_action_decision',
  'rehome',
  'homeless',
  'unload',
  'load',
  'board',
  'deboard',
  'embark',
  'disembark',
  'auto_work',
  'auto_explore',
  'set_options',
  'rename',
  'clear_rally',
  'clear_governor',
  'set_governor',
  'remove_worker_task',
]);

/** How many decision rows the *briefing* carries before it defers to the list. */
export const V2_BRIEFING_DECISION_ROWS = 8;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

const isEntityAlias = (value: string): boolean => ENTITY_ALIAS_RE.test(value);

/** One key of a compacted action, as the JSON value it carries. */
const compactField = (compact: CompactAction, key: string): JsonValue =>
  compact[key] ?? null;

/** The number an entity alias carries, for the stable `u1, u2, …` walk. */
const aliasNumber = (alias: string): number => Number.parseInt(alias.slice(1), 10);

const entityAliasId = (state: V2ClientState, alias: string): string => {
  const value = state.entity_aliases[alias];
  return typeof value === 'string' ? value : '';
};

/**
 * `dict.get(key, fallback)` — a lookup, not a prototype walk.
 *
 * Every key read through this helper is chosen off the wire: an `action_id`,
 * an opaque relation alias, or a subject's `operation`, none of which any
 * validator constrains beyond `OPAQUE_ID_RE` (which admits `toString`,
 * `constructor`, `valueOf`, `hasOwnProperty`). A CPython `dict` has own keys
 * only, so `.get` misses on all of them; a plain JS object read returns the
 * inherited function instead, which either splices a function body into a
 * copy-pasteable command or — for `tier1Verbs` — hands `decisionOrder` an
 * `arguments` property that *throws* on access, aborting a briefing CPython
 * prints normally. See NOTES §18.10 for the same defect on U15's row.
 */
const dictGet = <T>(table: Readonly<Record<string, T>>, key: string): T | undefined =>
  Object.hasOwn(table, key) ? table[key] : undefined;

// ---------------------------------------------------------------------------
// _decision_verb / _decision_option_rank (client.py:7042-7059)
// ---------------------------------------------------------------------------

export const decisionVerb = (compact: CompactAction, deps: DecisionDeps): string => {
  const tail = kindTail(compactText(compact, 'kind'));
  const operation = deps.orderOperation(compact);
  return operation !== '' && operation !== tail ? operation : tail;
};

export type DecisionRank = readonly [tier: number, index: number, tail: string];

export const decisionOptionRank = (compact: CompactAction, deps: DecisionDeps): DecisionRank => {
  const tail = kindTail(compactText(compact, 'kind'));
  const words = new Set([tail, deps.orderOperation(compact)]);
  words.delete('');
  for (const [index, verb] of V2_DECISION_PREFERRED.entries()) {
    if (words.has(verb)) return [0, index, tail];
  }
  for (const word of words) {
    if (V2_DECISION_HOUSEKEEPING.has(word)) return [2, 0, tail];
  }
  return [1, 0, tail];
};

const compareRanks = (left: DecisionRank, right: DecisionRank): number => {
  if (left[0] !== right[0]) return left[0] - right[0];
  if (left[1] !== right[1]) return left[1] - right[1];
  return left[2] < right[2] ? -1 : left[2] > right[2] ? 1 : 0;
};

/** `by_verb`, in first-seen order, exactly as CPython's dict keeps it. */
const groupByVerb = (
  pool: ReadonlyArray<CompactAction>,
  deps: DecisionDeps
): ReadonlyArray<readonly [string, ReadonlyArray<CompactAction>]> => {
  const groups = new Map<string, CompactAction[]>();
  for (const compact of pool) {
    const verb = decisionVerb(compact, deps);
    const found = groups.get(verb);
    if (found === undefined) groups.set(verb, [compact]);
    else found.push(compact);
  }
  return [...groups.entries()];
};

const rankedVerbs = (
  pool: ReadonlyArray<CompactAction>,
  deps: DecisionDeps
): ReadonlyArray<readonly [string, ReadonlyArray<CompactAction>]> =>
  // `Array.prototype.sort` is stable, which is what makes this `sorted()`.
  [...groupByVerb(pool, deps)].sort((left, right) => {
    const first = left[1][0];
    const second = right[1][0];
    if (first === undefined || second === undefined) return 0;
    return compareRanks(decisionOptionRank(first, deps), decisionOptionRank(second, deps));
  });

// ---------------------------------------------------------------------------
// _decision_options (client.py:7061-7086)
// ---------------------------------------------------------------------------

export interface DecisionOptions {
  readonly options: ReadonlyArray<string>;
  /** How many distinct verbs the actor has, before the cap. */
  readonly total: number;
}

/**
 * Name one actor's most relevant options, one line item per verb.
 *
 * Collapsing by verb is what keeps sixty enumerated `move` rows from crowding
 * out `found_city`: the row shows the verb once, with its count, and the
 * drill-down prints every target.
 */
export const decisionOptions = (
  pool: ReadonlyArray<CompactAction>,
  aliases: AliasMap,
  deps: DecisionDeps
): Effect.Effect<DecisionOptions, PlayerError> =>
  Effect.gen(function* () {
    const ranked = rankedVerbs(pool, deps);
    const options: string[] = [];
    for (const [verb, rows] of ranked.slice(0, V2_DECISION_MAX_OPTIONS)) {
      const compact = rows[0];
      if (compact === undefined) continue;
      let text = rows.length === 1 ? verb : `${verb}x${rows.length}`;
      if (rows.length === 1) {
        const target = yield* actionTargetKey(compactField(compact, 'target'));
        if (target !== '') text += ` ${target}`;
      }
      const alias = dictGet(aliases, compactText(compact, 'action_id')) ?? '';
      options.push(`${alias} ${text}`.trim());
    }
    return { options, total: ranked.length };
  });

// ---------------------------------------------------------------------------
// _tier1_word (client.py:7088-7103)
// ---------------------------------------------------------------------------

/**
 * Prefer the short word `just do` documents over the wire operation.
 *
 * Only the tier-1 verbs that add no arguments of their own are substituted:
 * `route` means `set_route mode=goto` and would silently pick a mode the
 * enumerated action may not have, so it is left alone.
 */
export const tier1Word = (compact: CompactAction, verb: string, deps: DecisionDeps): string => {
  for (const [word, tier1] of Object.entries(deps.tier1Verbs)) {
    if (Object.keys(tier1.arguments).length > 0) continue;
    if (
      compactText(compact, 'kind') === tier1.kind &&
      deps.orderOperation(compact) === tier1.operation
    ) {
      return word;
    }
  }
  return verb;
};

// ---------------------------------------------------------------------------
// _decision_order (client.py:7105-7145)
// ---------------------------------------------------------------------------

/**
 * Compose one runnable order from this actor's best cached option.
 *
 * The order is written the way `just do` reads it — `ALIAS VERB [TARGET]` — and
 * is only offered when it resolves to exactly one cached action *and* binds
 * that action's whole argument schema, so the batch line runs exactly as
 * printed.  An option that cannot meet both (a target no single word can name,
 * or an argument only the player can choose, like a new city's name) yields to
 * the next-ranked option rather than printing a command that would refuse.
 */
export const decisionOrder = (
  pool: ReadonlyArray<CompactAction>,
  alias: string,
  deps: DecisionDeps
): Effect.Effect<string, PlayerError> =>
  Effect.gen(function* () {
    if (alias === '' || pool.length === 0) return '';
    for (const [verb, rows] of rankedVerbs(pool, deps)) {
      const first = rows[0];
      if (first === undefined) continue;
      const word = tier1Word(first, verb, deps);
      // `word` is `_tier1_word`'s output, which falls back to the *wire's*
      // `subject.operation` — an arbitrary string. `V2_TIER1_VERBS.get(word)`
      // misses on `toString`; `deps.tier1Verbs[word]` would have returned the
      // built-in, and reading `.arguments` off a function throws a TypeError
      // that no `orElseSucceed` in this file can catch.
      const tier1 = dictGet(deps.tier1Verbs, word);
      const defaults: Readonly<Record<string, JsonValue>> = tier1 === undefined ? {} : tier1.arguments;
      const keys: string[] = [];
      for (const compact of rows) keys.push(yield* actionTargetKey(compactField(compact, 'target')));
      const single = keys.filter((key) => key !== '' && key.split(/\s+/).length === 1);
      const unique = new Set(
        single.filter((key) => single.filter((other) => other === key).length === 1)
      );
      for (const [offset, compact] of rows.entries()) {
        let key = keys[offset] ?? '';
        if (!unique.has(key)) {
          // Nothing one word long picks this action out of its siblings, so
          // the printed line could not select it.
          if (rows.length > 1) continue;
          key = '';
        }
        // A coordinate word is eaten by the target selector; any other target
        // word goes on to the argument schema.
        const values =
          key !== '' && deps.orderTargetKeys(key).length > 0 ? [] : key !== '' ? [key] : [];
        if ((yield* deps.orderMatch(compact, values, defaults)) === null) continue;
        return [alias, word, key].filter((part) => part !== '').join(' ');
      }
    }
    return '';
  });

// ---------------------------------------------------------------------------
// _decision_actor_row (client.py:7147-7168)
// ---------------------------------------------------------------------------

export const decisionActorRow = (
  alias: string,
  actorId: string,
  description: string,
  state: V2ClientState,
  aliases: AliasMap,
  deps: DecisionDeps
): Effect.Effect<DecisionRow, PlayerError> =>
  Effect.gen(function* () {
    const all = yield* deps.orderPool(state);
    const pool = all.filter((compact) => deps.orderActor(compact) === actorId);
    const { options, total } = yield* decisionOptions(pool, aliases, deps);
    return {
      alias,
      actor_id: actorId,
      state: description,
      options,
      option_count: total,
      order: yield* decisionOrder(pool, alias, deps),
      remedy: `just legal --actor_id ${alias} --all`,
    };
  });

// ---------------------------------------------------------------------------
// _decision_unit_rows (client.py:7170-7207)
// ---------------------------------------------------------------------------

const MISSING_CELLS: ReadonlySet<string> = new Set(['-', '']);

export const decisionUnitRows = (
  sessionPath: string,
  state: V2ClientState,
  aliases: AliasMap,
  deps: DecisionDeps
): Effect.Effect<ReadonlyArray<DecisionRow>, PlayerError, PrivateFs> =>
  Effect.gen(function* () {
    const table = yield* mirrorTable(sessionPath, mirrorFile('units'));
    const found: Array<readonly [number, DecisionRow]> = [];
    for (const row of table.rows) {
      const alias = mirrorCell(table.columns, row, 'alias');
      if (!isEntityAlias(alias) || alias[0] !== 'u') continue;
      const who = mirrorCell(table.columns, row, 'who');
      if (who !== 'own' && !MISSING_CELLS.has(who)) continue;
      const moves = mirrorNumber(mirrorCell(table.columns, row, 'moves'));
      if (moves !== null && moves <= 0) continue;
      if (!MISSING_CELLS.has(mirrorCell(table.columns, row, 'orders'))) continue;
      const activity = mirrorCell(table.columns, row, 'activity');
      if (activity !== 'idle' && !MISSING_CELLS.has(activity)) continue;
      const position = mirrorCell(table.columns, row, 'pos');
      const description = [
        mirrorCell(table.columns, row, 'unit'),
        MISSING_CELLS.has(position) ? '' : `@${position}`,
        'idle',
      ]
        .filter((part) => part !== '')
        .join(' ');
      found.push([
        aliasNumber(alias),
        yield* decisionActorRow(
          alias,
          entityAliasId(state, alias),
          description,
          state,
          aliases,
          deps
        ),
      ]);
    }
    return found.sort((left, right) => left[0] - right[0]).map(([, row]) => row);
  });

// ---------------------------------------------------------------------------
// _decision_city_rows (client.py:7209-7246)
// ---------------------------------------------------------------------------

export const decisionCityRows = (
  sessionPath: string,
  state: V2ClientState,
  aliases: AliasMap,
  deps: DecisionDeps
): Effect.Effect<ReadonlyArray<DecisionRow>, PlayerError, PrivateFs> =>
  Effect.gen(function* () {
    const table = yield* mirrorTable(sessionPath, mirrorFile('cities'));
    const found: Array<readonly [number, DecisionRow]> = [];
    for (const row of table.rows) {
      const alias = mirrorCell(table.columns, row, 'alias');
      if (!isEntityAlias(alias) || alias[0] !== 'c') continue;
      const building = mirrorCell(table.columns, row, 'building');
      const shields = mirrorCell(table.columns, row, 'shields');
      const separator = shields.indexOf('/');
      const stock = separator < 0 ? shields : shields.slice(0, separator);
      const cost = separator < 0 ? '' : shields.slice(separator + 1);
      const stockValue = mirrorNumber(stock);
      const costValue = mirrorNumber(cost);
      const empty = MISSING_CELLS.has(building);
      const completing = stockValue !== null && costValue !== null && stockValue >= costValue;
      if (!empty && !completing) continue;
      const position = mirrorCell(table.columns, row, 'pos');
      const buy = mirrorCell(table.columns, row, 'buy');
      const description = [
        mirrorCell(table.columns, row, 'city'),
        MISSING_CELLS.has(position) ? '' : `@${position}`,
        empty ? 'no production' : `${building} ${shields} done`,
        // The price of finishing it now, from the mirror this briefing just
        // wrote — the buy action itself never quotes one.
        mirrorNumber(buy) === null ? '' : `buy=${buy}`,
      ]
        .filter((part) => part !== '')
        .join(' ');
      found.push([
        aliasNumber(alias),
        yield* decisionActorRow(
          alias,
          entityAliasId(state, alias),
          description,
          state,
          aliases,
          deps
        ),
      ]);
    }
    return found.sort((left, right) => left[0] - right[0]).map(([, row]) => row);
  });

// ---------------------------------------------------------------------------
// _decision_meeting_rows (client.py:7318-7363)
// ---------------------------------------------------------------------------

export const decisionMeetingRows = (
  sessionPath: string,
  state: V2ClientState,
  aliases: AliasMap,
  deps: DecisionDeps
): Effect.Effect<ReadonlyArray<DecisionRow>, PlayerError, PrivateFs> =>
  Effect.gen(function* () {
    const pool = yield* deps.orderPool(state);
    const groups = meetingGroups(pool, aliases);
    const pending = yield* openMeetings(sessionPath);
    const player = yield* deps.playerScopeAlias(state);
    const names = [
      ...groups.keys(),
      ...Object.keys(pending).filter((alias) => !groups.has(alias)),
    ];
    const rows: DecisionRow[] = [];
    for (const name of names) {
      const relationPool = groups.get(name) ?? [];
      const { options, total } = yield* decisionOptions(relationPool, aliases, deps);
      rows.push({
        alias: name,
        actor_id: '',
        // Without the diplomacy table this row knows a meeting is open and
        // nothing else about it, so it names the read that would say who and
        // what — the one page it is missing.
        state: dictGet(pending, name) ?? `meeting pending (unread: ${V2_DIPLOMACY_READ})`,
        options,
        option_count: total,
        // A meeting's actor is this seat's own player, not the relation the row
        // is named after, so no `ALIAS VERB` order can be composed for it.
        order: '',
        remedy: meetingRemedy(relationPool, name, aliases, player),
      });
    }
    return rows;
  });

// ---------------------------------------------------------------------------
// _decision_rows (client.py:7365-7379)
// ---------------------------------------------------------------------------

export interface DecisionRowsOptions {
  /** Actors this command has just ordered; the focus loop moves past them. */
  readonly done?: ReadonlySet<string> | undefined;
}

/** Walk this seat's actors in a stable order: units, cities, meetings. */
export const decisionRows = (
  sessionPath: string,
  state: V2ClientState,
  deps: DecisionDeps,
  options: DecisionRowsOptions = {}
): Effect.Effect<ReadonlyArray<DecisionRow>, PlayerError, PrivateFs> =>
  Effect.gen(function* () {
    const aliases = yield* aliasMap(state);
    const rows = [
      ...(yield* decisionUnitRows(sessionPath, state, aliases, deps)),
      ...(yield* decisionCityRows(sessionPath, state, aliases, deps)),
      ...(yield* decisionMeetingRows(sessionPath, state, aliases, deps)),
    ];
    const done = options.done ?? new Set<string>();
    // An actor this command has just ordered is never offered back: the focus
    // loop moves on, exactly as clicking through the native client does.
    return rows
      .filter((row) => row.actor_id === '' || !done.has(row.actor_id))
      .slice(0, V2_DECISION_MAX_ROWS);
  });

// ---------------------------------------------------------------------------
// _next_focus_line (client.py:7436-7455)
// ---------------------------------------------------------------------------

/**
 * Name what still needs orders, from local caches only.
 *
 * This runs on the receipt path, so it must never fetch and never fail: a cache
 * too stale to name options degrades to the actor and its enumeration command,
 * and a cache that cannot be read at all prints nothing rather than turning a
 * successful receipt into an error.
 */
export const nextFocusLine = (
  sessionPath: string,
  state: V2ClientState,
  done: ReadonlySet<string>,
  deps: DecisionDeps
): Effect.Effect<string, never, PrivateFs> =>
  Effect.map(
    Effect.orElseSucceed(
      decisionRows(sessionPath, state, deps, { done }),
      (): ReadonlyArray<DecisionRow> | null => null
    ),
    (rows) => {
      if (rows === null) return '';
      if (rows.length === 0) return `next: no actors need orders — ${V2_ONE_CALL_END}`;
      const batched = batchFocusCommand(rows);
      if (batched !== '') return batched;
      const first = rows[0];
      return first === undefined ? '' : `next: ${decisionLine(first)}`;
    }
  );

// ---------------------------------------------------------------------------
// _briefing_decision_lines (client.py:7458-7486)
// ---------------------------------------------------------------------------

/**
 * Summarise each actor that needs orders, from local caches only.
 *
 * This is the same projection `turn --decisions` prints, minus its fetches: the
 * briefing has already written the unit and city tables it walks, and an actor
 * whose catalog is not cached degrades to its own drill-down command rather
 * than costing a round trip the briefing never promised.
 */
export const briefingDecisionLines = (
  sessionPath: string,
  state: V2ClientState,
  deps: DecisionDeps
): Effect.Effect<ReadonlyArray<string>, never, PrivateFs> =>
  Effect.map(
    Effect.orElseSucceed(
      decisionRows(sessionPath, state, deps),
      (): ReadonlyArray<DecisionRow> | null => null
    ),
    (rows) => {
      if (rows === null) return [];
      const lines = rows.slice(0, V2_BRIEFING_DECISION_ROWS).map((row) => decisionLine(row));
      if (rows.length > V2_BRIEFING_DECISION_ROWS) {
        lines.push(
          `+${rows.length - V2_BRIEFING_DECISION_ROWS} more actors — just turn --decisions`
        );
      }
      // The briefing is where the next `do` is chosen, so it closes with that
      // `do` already written: reading the menu and composing the batch are the
      // same act, not two calls.
      const batched = batchFocusCommand(rows);
      if (batched !== '') lines.push(batched);
      return lines;
    }
  );

export type { CompactAction, DecisionRow, Tier1Verb };
