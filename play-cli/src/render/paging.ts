/**
 * The paged-cursor footer, shared by `state`, `legal` and `show`.
 *
 * The Python spells this inline as `_page_status` (client.py:3549-3554) folded
 * into each renderer's summary line, which is why the three commands drifted
 * from one another twice.  One builder, one byte shape:
 *
 *   `16/43 more --cursor cursor_…`   when another page exists
 *   `16/16 complete`                 when it does not
 */
import { pageStatus } from 'src/render/primitives';
import type { PageBody } from 'src/schema/page';

/** The bare status fragment, exactly as `_page_status` returns it. */
export const pagingStatus = <Item>(page: PageBody<Item>): string => pageStatus(page);

/**
 * Build the same fragment from raw counts, for the compact paths that never
 * hold a page envelope (`legal --all`, `show --grep`).
 *
 * `command` is accepted because the frozen interface in PORT_MAP §1 promises
 * it, and because a caller reads better naming the command it is describing;
 * it is deliberately NOT printed — the Python's footer is exactly
 * `"{shown}/{total} more --cursor {cursor}"` and adding to it would diverge in
 * three commands at once.
 */
export const pagingFooter = (
  shown: number,
  total: number | null,
  cursor: string | null,
  command = ''
): ReadonlyArray<string> => {
  void command;
  const totalText = total === null ? '?' : String(total);
  return cursor === null
    ? [`${shown}/${totalText} complete`]
    : [`${shown}/${totalText} more --cursor ${cursor}`];
};
