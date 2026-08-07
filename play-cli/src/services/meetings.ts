/**
 * Open diplomacy meetings — the third kind of actor that needs a decision.
 *
 * Ports `_meeting_remedy` (client.py:7248-7278), `V2_DIPLOMACY_READ` (7281) and
 * `_open_meetings` (7284-7315), plus the pool grouping `_decision_meeting_rows`
 * (7318-7363) walks.
 *
 * A meeting used to reach the decisions list only once a diplomacy descriptor
 * happened to be cached, which meant an unopened one was invisible: a real game
 * left Spain's cease-fire sitting open while the block offered an idle worker.
 * The diplomacy mirror is this seat's own record that the meeting exists, so it
 * is read too, and a relation named by either source gets a row.
 */
import { Effect } from 'effect';
import { ENTITY_ALIAS_RE } from 'src/constants';
import { isJsonObject, type AliasMap, type JsonValue } from 'src/render/primitives';
import type { CompactAction } from 'src/services/legal-compact';
import { kindHead } from 'src/services/orders';
import { mirrorCell, mirrorFile, mirrorNumber, mirrorTable } from 'src/services/turn-pages';
import { PrivateFs } from 'src/services/private-fs';

/** The one page a meeting row is missing when only the mirror knows about it. */
export const V2_DIPLOMACY_READ = 'just state --section diplomacy --limit 16';

const isEntityAlias = (value: string): boolean => ENTITY_ALIAS_RE.test(value);

const objectField = (value: JsonValue | undefined, key: string): JsonValue | undefined =>
  isJsonObject(value) ? value[key] : undefined;

/**
 * `aliases.get(key, fallback)` — a dict lookup, not a prototype walk.
 *
 * Both callers below key the alias table by an unvalidated wire ID. A CPython
 * `dict.get("toString")` misses and takes the fallback; a plain JS property
 * read returns the inherited built-in, which would put a function body into
 * `just legal --actor_id …` (the one surface whose whole job is to be
 * copy-pasteable) and would file a diplomacy group under a bogus relation
 * name. Same defect as NOTES §18.10 on U15's row.
 */
const aliasGet = (aliases: AliasMap, key: string): string | undefined =>
  Object.hasOwn(aliases, key) ? aliases[key] : undefined;

// ---------------------------------------------------------------------------
// _meeting_remedy (client.py:7248-7278)
// ---------------------------------------------------------------------------

/**
 * Name the query that re-enumerates one meeting.
 *
 * Diplomacy is never in the global catalog and a relation is never an actor, so
 * the only command that reaches a clause is this seat's own player bound to the
 * relation.  A remedy that names any other form sends an agent that already
 * knows a meeting is open around the one loop it cannot exit.
 *
 * A meeting known only from the diplomacy mirror has no cached descriptor to
 * read the actor off, which is exactly the case this remedy exists for: the
 * caller supplies the seat's own player alias instead.
 */
export const meetingRemedy = (
  pool: ReadonlyArray<CompactAction>,
  name: string,
  aliases: AliasMap,
  player = ''
): string => {
  const first = pool[0];
  const actor =
    first === undefined ? undefined : objectField(first['subject'] ?? null, 'actor');
  const actorId = objectField(actor, 'id');
  const actorName =
    typeof actorId === 'string'
      ? (aliasGet(aliases, actorId) ?? actorId)
      : player !== ''
        ? player
        : 'YOUR_PLAYER_ID';
  const target = first === undefined ? undefined : (first['target'] ?? null);
  const targetId = objectField(target, 'id');
  const targetName = isEntityAlias(name)
    ? name
    : typeof targetId === 'string'
      ? targetId
      : 'RELATION_ID';
  return `just legal --actor_id ${actorName} --target_id ${targetName} --all`;
};

// ---------------------------------------------------------------------------
// _open_meetings (client.py:7284-7315)
// ---------------------------------------------------------------------------

/**
 * Describe every relation the diplomacy mirror shows a meeting open on.
 *
 * Reads one local table and opens no socket.  A meeting this seat has already
 * accepted is not a decision it still owes, so only the relations still waiting
 * on the seat are returned — keyed by relation alias, valued as the sentence
 * the decisions row prints.
 */
export const openMeetings = (
  sessionPath: string
): Effect.Effect<Readonly<Record<string, string>>, never, PrivateFs> =>
  Effect.map(mirrorTable(sessionPath, mirrorFile('diplomacy')), (table) => {
    const found: Record<string, string> = {};
    for (const row of table.rows) {
      const alias = mirrorCell(table.columns, row, 'alias');
      if (
        !isEntityAlias(alias) ||
        alias[0] !== 'r' ||
        mirrorCell(table.columns, row, 'meeting') !== 'open'
      ) {
        continue;
      }
      const accepted = mirrorCell(table.columns, row, 'accepted');
      if (accepted.split('+').includes('you')) continue;
      const clauses = mirrorCell(table.columns, row, 'clauses');
      const detail = [
        mirrorCell(table.columns, row, 'player'),
        mirrorCell(table.columns, row, 'state'),
        mirrorNumber(clauses) === null ? '' : `${clauses} clauses`,
      ]
        .filter((part) => part !== '-' && part !== '')
        .join(', ');
      found[alias] = `meeting pending${detail === '' ? '' : `: ${detail}`}`;
    }
    return found;
  });

// ---------------------------------------------------------------------------
// The grouping `_decision_meeting_rows` walks (client.py:7327-7340)
// ---------------------------------------------------------------------------

/**
 * Group every cached diplomacy action by the relation alias it targets.
 *
 * A target this seat has no alias for is filed under `diplomacy`, exactly as
 * CPython's `groups.setdefault(name or "diplomacy", …)` does — one row for the
 * unnamed remainder rather than none.
 */
export const meetingGroups = (
  pool: ReadonlyArray<CompactAction>,
  aliases: AliasMap
): ReadonlyMap<string, ReadonlyArray<CompactAction>> => {
  const groups = new Map<string, CompactAction[]>();
  for (const compact of pool) {
    const kind = compact['kind'];
    if (typeof kind !== 'string' || kindHead(kind) !== 'diplomacy') continue;
    const target = compact['target'] ?? null;
    const identifier = objectField(target, 'id');
    const name = typeof identifier === 'string' ? (aliasGet(aliases, identifier) ?? '') : '';
    const key = name === '' ? 'diplomacy' : name;
    const found = groups.get(key);
    if (found === undefined) groups.set(key, [compact]);
    else found.push(compact);
  }
  return groups;
};
