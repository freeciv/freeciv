/**
 * The mirror's table read/write round trip.
 *
 * Ports `play/state_mirror.py:365-434` — `_Table`, `_REV_LINE_RE`,
 * `_parse_table`, `_table_text`, `_page_note`.
 *
 * The round trip is the merge contract: a page is a *window*, so page 2 of a
 * unit catalog must not erase page 1.  `parseTable` reads the file back, the
 * caller merges rows keyed by column 0, `tableText` writes it out again with
 * the column widths recomputed.  Column 0 stays aligned across every row, which
 * is what makes `grep '^u1'` on `state/units.tsv` work.
 */
import {
  cell,
  codeLength,
  ljust,
  revLine,
  rstrip,
  splitLines,
  strip,
  type MirrorRevision,
} from 'src/services/mirror/store';

/** `_Table` — a parsed mirror table: its revision, column names and rows. */
export interface MirrorTable {
  readonly revision: MirrorRevision | null;
  readonly columns: ReadonlyArray<string>;
  readonly rows: ReadonlyArray<ReadonlyArray<string>>;
}

/** `_REV_LINE_RE` — the stamp `parse_table` and `parse_map` both read. */
export const REV_LINE_RE = /^# rev (\d+) turn (\d+)\b/;

/** Read the `# rev N turn T` stamp off a projection's first line. */
export const parseRevLine = (line: string | undefined): MirrorRevision | null => {
  if (line === undefined) return null;
  const match = REV_LINE_RE.exec(line);
  if (match === null) return null;
  const revision = Number(match[1]);
  const turn = Number(match[2]);
  return { turn, revision };
};

/** `_Table.by_key` — rows keyed by column 0, later rows winning. */
export const tableByKey = (
  table: MirrorTable
): ReadonlyMap<string, ReadonlyArray<string>> => {
  const keyed = new Map<string, ReadonlyArray<string>>();
  for (const row of table.rows) {
    const key = row[0];
    if (key !== undefined) keyed.set(key, row);
  }
  return keyed;
};

const EMPTY_TABLE: MirrorTable = { revision: null, columns: [], rows: [] };

/** `_parse_table` — read a mirror table back off disk. */
export const parseTable = (text: string | null | undefined): MirrorTable => {
  if (text === null || text === undefined || text === '') return EMPTY_TABLE;
  const lines = splitLines(text);
  const revision = parseRevLine(lines[0]);
  const body = lines.filter((line) => line !== '' && !line.startsWith('#'));
  const head = body[0];
  if (head === undefined) return { revision, columns: [], rows: [] };
  // `strip`, never `.trim()`: this is the read half of the round trip `cell`
  // wrote, and the two whitespace classes are not nested (NOTES.md §12.1).
  return {
    revision,
    columns: head.split('\t').map(strip),
    rows: body.slice(1).map((line) => line.split('\t').map(strip)),
  };
};

/**
 * `_table_text` — render a table.
 *
 * Every column is padded to the widest value it holds (the header name counts),
 * cells are tab-joined and the line is right-stripped, so the trailing column
 * never carries padding into the file.
 */
export const tableText = (
  revision: MirrorRevision | null,
  notes: ReadonlyArray<string>,
  columns: ReadonlyArray<string>,
  rows: ReadonlyArray<ReadonlyArray<string>>
): string => {
  // Widths and padding are measured in code points, matching CPython's
  // `len` / `str.ljust`.  A single astral character counted as two UTF-16
  // units would mis-pad its whole column and diverge every row of the file.
  const widths = rows.reduce<ReadonlyArray<number>>(
    (accumulated, row) =>
      accumulated.map((width, index) => Math.max(width, codeLength(row[index] ?? ''))),
    columns.map((name) => codeLength(name))
  );
  const line = (values: ReadonlyArray<string>): string =>
    rstrip(
      values
        .map((value, index) => {
          const width = widths[index];
          return width === undefined ? value : ljust(value, width);
        })
        .join('\t')
    );
  return `${[
    revLine(revision),
    ...notes.map((note) => `# ${note}`),
    line(columns),
    ...rows.map((row) => line(row)),
  ].join('\n')}\n`;
};

/** `_page_note` — the `# units 2/2 complete` line above every table. */
export const pageNote = (
  section: string,
  shown: number,
  total: unknown,
  complete: boolean
): string =>
  `${section} ${shown}/${cell(total)} ${
    complete ? 'complete' : 'partial - fetch the next cursor'
  }`;
