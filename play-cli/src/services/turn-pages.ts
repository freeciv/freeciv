/**
 * The pages one turn briefing is built from, and the local tables it reads back.
 *
 * Ports `play/client.py`:
 *
 * - 6598-6612 `_turn_next_commands`
 * - 6614-6624 `_turn_compact_page`
 * - 6627-6640 `_turn_page`
 * - 6642-6649 `_turn_health`
 * - 6995-7016 `_mirror_table`
 * - 7018-7028 `_mirror_is_fresh`
 * - 7030-7040 `_mirror_cell` / `_mirror_number`
 * - 7488-7514 `_mirror_event_count` / `_briefing_events_line`
 *
 * Two halves of one contract.  The wire half fetches the four sections `turn`
 * shows at `V2_TURN_PAGE_LIMIT` and compacts each into the shape `--json`
 * publishes.  The local half reads the TSV projections U04/U07 already wrote,
 * because everything `--decisions` and the focus tail say is a projection of
 * files this seat owns — no round trip, ever.
 */
import { Effect } from 'effect';
import { V2_TURN_PAGE_LIMIT, V2_TURN_SECTIONS } from 'src/constants';
import type { PlayError } from 'src/errors';
import { isJsonObject, type JsonValue } from 'src/render/primitives';
import type { TurnCompactPage } from 'src/render/turn';
import { decodeHealth, type HealthEnvelope } from 'src/schema/health';
import { decodePage, type PageEnvelope } from 'src/schema/page';
import type { Revision } from 'src/schema/revision';
import {
  SECTION_TARGETS,
  mirrorText,
  parseTable,
  type MirrorTable,
} from 'src/services/mirror';
import { PrivateFs } from 'src/services/private-fs';
import { SessionStore, credentialsOf, type Session } from 'src/services/session-store';
import { V2Client } from 'src/services/v2-client';

// ---------------------------------------------------------------------------
// The cross-unit hooks
// ---------------------------------------------------------------------------

/**
 * What building a briefing needs from units this one does not own.
 *
 * `rememberPage` is U03's, `mirrorPage` is U04/U07's `update_from_page` bridge,
 * and `fetchStateSection` is U18's `_fetch_state_section`, which `--decisions`
 * uses to refresh exactly the mirror table that went stale.  They arrive as
 * functions so this unit is testable without any of them — but the live wiring
 * in `src/commands/turn.cmd.ts` binds all three, because `--decisions` and the
 * focus tail are projections of the very TSVs `mirrorPage` writes.
 */
export interface TurnHooks {
  readonly rememberPage: (page: PageEnvelope) => Effect.Effect<void, PlayError>;
  readonly mirrorPage: (page: PageEnvelope, source: string) => Effect.Effect<void, PlayError>;
  readonly fetchStateSection: (
    section: string
  ) => Effect.Effect<void, PlayError, PrivateFs | SessionStore | V2Client>;
}

// ---------------------------------------------------------------------------
// _turn_next_commands (client.py:6598-6612)
// ---------------------------------------------------------------------------

const TURN_TAIL_COMMANDS: ReadonlyArray<string> = [
  'just legal --kind phase.end --all',
  'just legal --kind research.set_target --all',
  'just legal --kind economy.set_rates --all',
  'just legal --actor_id ACTOR_ID --all',
];

/** Name the follow-up commands bare: `--session` resolves itself. */
export const turnNextCommands = (
  pages: Readonly<Record<string, PageEnvelope>>
): ReadonlyArray<string> => {
  const commands: string[] = [];
  for (const section of V2_TURN_SECTIONS) {
    const page = pages[section];
    const cursor = page === undefined ? null : page.page.next_cursor;
    if (cursor !== null) commands.push(`just state --cursor ${cursor}`);
  }
  return [...commands, ...TURN_TAIL_COMMANDS];
};

// ---------------------------------------------------------------------------
// _turn_compact_page (client.py:6614-6624)
// ---------------------------------------------------------------------------

export const turnCompactPage = (page: PageEnvelope): TurnCompactPage => ({
  shown: page.page.items.length,
  total: page.page.total_items,
  truncated: page.page.next_cursor !== null,
  items: page.page.items,
  next_cursor: page.page.next_cursor,
  cursor_expires_at: page.page.cursor_expires_at,
});

// ---------------------------------------------------------------------------
// _turn_page / _turn_health (client.py:6627-6649)
// ---------------------------------------------------------------------------

const TURN_TIMEOUT_S = 10;

const notOk = (status: number): boolean => status < 200 || status >= 300;

/** One state section, at the briefing's page limit. */
export const turnPage = (
  session: Session,
  section: string
): Effect.Effect<PageEnvelope, PlayError, V2Client> =>
  Effect.gen(function* () {
    const client = yield* V2Client;
    const credentials = credentialsOf(session);
    const query = new URLSearchParams({
      section,
      limit: String(V2_TURN_PAGE_LIMIT),
    }).toString();
    const url = `${yield* client.url(credentials, '/state')}?${query}`;
    const response = yield* client.response('GET', url, credentials, {
      timeout: TURN_TIMEOUT_S,
    });
    if (notOk(response.status)) return yield* client.raiseValidated(response);
    return yield* decodePage(response.value, session);
  });

/** The health read the briefing brackets its four pages with. */
export const turnHealth = (
  session: Session
): Effect.Effect<HealthEnvelope, PlayError, V2Client> =>
  Effect.gen(function* () {
    const client = yield* V2Client;
    const credentials = credentialsOf(session);
    const url = yield* client.url(credentials, '/health');
    const response = yield* client.response('GET', url, credentials, {
      timeout: TURN_TIMEOUT_S,
    });
    if (notOk(response.status)) return yield* client.raiseValidated(response);
    return yield* decodeHealth(response.value, session);
  });

// ---------------------------------------------------------------------------
// The mirror tables the decision projection reads (client.py:6995-7040)
// ---------------------------------------------------------------------------

/** `MIRROR_CELL_RE` — a mirror cell that is a plain bounded integer. */
export const MIRROR_CELL_RE = /^-?[0-9]{1,9}$/;

/**
 * `V2_SHOW_FILES` for the four tables this unit reads.
 *
 * The full table is U09's (`show`); core's mirror layer already carries the
 * same paths as `SECTION_TARGETS`, so this resolves through that rather than
 * restating a second copy of the layout.
 */
export const mirrorFile = (section: string): ReadonlyArray<string> => {
  const parts = SECTION_TARGETS.get(section);
  if (parts === undefined) {
    // Unreachable for the four sections this unit names; a typo must not read
    // some other seat's file.
    return [];
  }
  return parts;
};

/** `_mirror_table` — read one mirror TSV; an absent file is an empty table. */
export const mirrorTable = (
  sessionPath: string,
  parts: ReadonlyArray<string>
): Effect.Effect<MirrorTable, never, PrivateFs> =>
  parts.length === 0
    ? Effect.succeed(parseTable(null))
    : Effect.map(mirrorText(sessionPath, parts), (text) => parseTable(text));

/** `_mirror_is_fresh` — whether one table already reflects this exact revision. */
export const mirrorIsFresh = (
  sessionPath: string,
  parts: ReadonlyArray<string>,
  revision: Revision | null
): Effect.Effect<boolean, never, PrivateFs> =>
  revision === null
    ? Effect.succeed(false)
    : Effect.map(mirrorTable(sessionPath, parts), (table) => {
        const found = table.revision;
        return (
          found !== null && found.turn === revision.turn && found.revision === revision.revision
        );
      });

/** `_mirror_cell` — one named column of one row, or `""`. */
export const mirrorCell = (
  columns: ReadonlyArray<string>,
  row: ReadonlyArray<string>,
  name: string
): string => {
  const index = columns.indexOf(name);
  if (index < 0) return '';
  return row[index] ?? '';
};

/** `_mirror_number` — read the left half of a `3/3` cell as an integer. */
export const mirrorNumber = (text: string): number | null => {
  const head = (text.split('/', 1)[0] ?? '').trim();
  return MIRROR_CELL_RE.test(head) ? Number.parseInt(head, 10) : null;
};

// ---------------------------------------------------------------------------
// _mirror_event_count / _briefing_events_line (client.py:7488-7514)
// ---------------------------------------------------------------------------

/** Read the event total the mirror recorded the last time it was written. */
export const mirrorEventCount = (
  sessionPath: string
): Effect.Effect<number | null, never, PrivateFs> =>
  Effect.map(mirrorTable(sessionPath, mirrorFile('overview')), (table) => {
    for (const row of table.rows) {
      if (mirrorCell(table.columns, row, 'fact') === 'count_chat') {
        return mirrorNumber(mirrorCell(table.columns, row, 'value'));
      }
    }
    return null;
  });

/** Name the typed event feed when it has grown since the last briefing. */
export const briefingEventsLine = (overview: JsonValue, seen: number | null): string => {
  const counts = isJsonObject(overview) ? overview['counts'] : undefined;
  const total = isJsonObject(counts) ? counts['chat'] : undefined;
  if (typeof total !== 'number' || !Number.isInteger(total)) return '';
  const fresh = seen === null ? total : total - seen;
  if (fresh <= 0) return '';
  return `events: ${fresh} new — just state --section chat`;
};
