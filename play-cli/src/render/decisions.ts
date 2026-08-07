/**
 * How the focus loop reads.
 *
 * Ports `_decision_line` (client.py:7381-7398), `V2_FOCUS_LINE_MAX` /
 * `V2_ONE_CALL_END` (7405-7406) and `_batch_focus_command` (7409-7434).
 *
 * Freeciv's GUI walks a human from one actor that needs orders to the next.
 * The same walk is a projection here: `just turn --decisions` prints the whole
 * list, and the text rendering of every successful `do`/`batch` receipt ends
 * with one `next:` line naming what is left.  Nothing in this file fetches or
 * reads a file — it renders rows `src/services/decisions.ts` already built.
 */
import { V2_ONE_CALL_END } from 'src/render/turn';
import { V2_MAX_ORDERS } from 'src/services/orders';

// ---------------------------------------------------------------------------
// Bounds (client.py:6975-6976, 7405-7406)
// ---------------------------------------------------------------------------

/** The widest one decision row is allowed to print. */
export const V2_DECISION_ROW_MAX = 120;

/** The widest the composed `just do` tail is allowed to print. */
export const V2_FOCUS_LINE_MAX = 200;

// ---------------------------------------------------------------------------
// The row shape
// ---------------------------------------------------------------------------

/** One actor that can still act, with its best cached options. */
export interface DecisionRow {
  /** `u1`, `c1`, `r1` — what `just do` and `just legal` accept. */
  readonly alias: string;
  /** The opaque actor ID, or `""` for a meeting row (its actor is this seat). */
  readonly actor_id: string;
  /** The prose describing why this actor is on the list. */
  readonly state: string;
  readonly options: ReadonlyArray<string>;
  readonly option_count: number;
  /** One runnable `ALIAS VERB [TARGET]` order, or `""` when none composes. */
  readonly order: string;
  /** The enumeration command that reads this actor's whole menu. */
  readonly remedy: string;
}

/** The four keys `--decisions --json` publishes, in the Python's order. */
export const DECISION_JSON_KEYS = [
  'alias',
  'actor_id',
  'state',
  'options',
  'option_count',
] as const;

/** CPython's `len()` counts code points; JavaScript's `.length` does not. */
const width = (text: string): number => [...text].length;

// ---------------------------------------------------------------------------
// _decision_line (client.py:7381-7398)
// ---------------------------------------------------------------------------

/**
 * One actor on one line, with as many of its options as the budget allows.
 *
 * An actor whose cached options have died with their revision still gets a
 * line — the enumeration command that would bring them back — because a silent
 * row is indistinguishable from an actor that needs nothing.
 */
export const decisionLine = (row: DecisionRow): string => {
  const head = `${row.alias} ${row.state}`;
  let line = head;
  let shown = 0;
  for (const option of row.options) {
    const candidate = line + (shown === 0 ? ' — ' : ' · ') + option;
    if (width(candidate) > V2_DECISION_ROW_MAX) break;
    line = candidate;
    shown += 1;
  }
  if (shown === 0) return `${head} — ${row.remedy}`;
  if (row.option_count > shown) {
    const more = ` · more: ${row.remedy}`;
    if (width(line) + width(more) <= V2_DECISION_ROW_MAX) line += more;
  }
  return line;
};

// ---------------------------------------------------------------------------
// _batch_focus_command (client.py:7409-7434)
// ---------------------------------------------------------------------------

/**
 * Compose the whole remaining turn as one `just do` line.
 *
 * The focus loop used to name one actor at a time, and a seat that reads one
 * actor plays one actor: the shipped agent batched in 8% of its calls.  So when
 * more than one actor needs orders the tail is the composed command itself —
 * every actor's top option, ready to edit and ready to run — and the
 * single-actor form is kept only for the case where it is the truth.
 */
export const batchFocusCommand = (rows: ReadonlyArray<DecisionRow>): string => {
  const orders = rows
    .slice(0, V2_MAX_ORDERS)
    .map((row) => row.order)
    .filter((order) => order !== '');
  // One composable actor is not a batch, and an actor whose catalog this seat
  // cannot read is better served by the single-actor form.
  if (orders.length < 2) return '';
  const total = rows.length;
  while (orders.length > 1) {
    const head = `next ${total} actors` + (orders.length === total ? '' : `, top ${orders.length}`);
    const line = `${head}: just do "${orders.join('; ')}"`;
    if (width(line) <= V2_FOCUS_LINE_MAX) return line;
    orders.pop();
  }
  return `next ${total} actors need orders — just turn --decisions`;
};

export { V2_MAX_ORDERS, V2_ONE_CALL_END };
