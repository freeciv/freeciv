/**
 * Grouping the global catalog (redesign doc §9.1).
 *
 * Ports `_catalog_choice_line` (client.py:3837-3866) and `_grouped_legal_lines`
 * (3869-3910).
 *
 * The player scope enumerates ~147 actions, of which ~50 are server-setting
 * proposals and dozens more are one research goal or target each.  Flat, they
 * bury the handful of rows an agent actually chooses between.  The default
 * rendering of the *global* catalog therefore groups by action kind and
 * collapses the families below; `--full` restores the flat list, and every
 * scoped (`--actor_id`/`--kind`) rendering is untouched.  This is rendering
 * only: the staged descriptor cache and `--json` are byte-identical.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import {
  V2_CATALOG_CHOICE_KINDS,
  V2_CATALOG_COLLAPSE_MIN,
  V2_CATALOG_HOUSEKEEPING,
  V2_CATALOG_LINE_MAX,
} from 'src/constants';
import type { AliasMap } from 'src/render/primitives';
import { field, type JsonValue } from 'src/schema/primitives';
import { actionTargetKey } from 'src/services/aliases';
import type { CompactAction } from 'src/services/legal-compact';
import { legalRowIsDefault, legalRows } from 'src/render/legal/rows';

/** CPython's `len(str)` counts code points, not UTF-16 code units. */
const codePoints = (text: string): number => [...text].length;

const SEPARATOR = ' · ';

/**
 * Collapse one choice family to a row that still names every choice.
 *
 * The aliases come first while they fit, because they are what the agent types
 * back.  Past the budget the names alone lead and the drill-down command
 * recovers the rows; a name is never dropped without one.
 */
export const catalogChoiceLine = (
  kind: string,
  compacts: ReadonlyArray<CompactAction>,
  aliases: AliasMap | null
): Effect.Effect<string, PlayerError> =>
  Effect.gen(function* () {
    const lookup = aliases ?? {};
    const choices: Array<readonly [string, string]> = [];
    for (const compact of compacts) {
      const actionId = field(compact, 'action_id');
      const alias = typeof actionId === 'string' ? (lookup[actionId] ?? '') : '';
      const key = yield* actionTargetKey(field(compact, 'target'));
      const label = field(compact, 'label');
      const name = key !== '' ? key : labelText(label);
      choices.push([alias, name]);
    }
    const head = `${kind}: ${compacts.length} choices — `;
    const withAliases =
      head + choices.map(([alias, name]) => (alias !== '' ? `${alias} ${name}` : name)).join(SEPARATOR);
    if (codePoints(withAliases) <= V2_CATALOG_LINE_MAX) return withAliases;
    const drill = ` — just legal --kind ${kind} --all`;
    const plain = head + choices.map(([, name]) => name).join(SEPARATOR);
    if (codePoints(plain) + codePoints(drill) <= V2_CATALOG_LINE_MAX) return plain + drill;
    const budget = V2_CATALOG_LINE_MAX - codePoints(head) - codePoints(drill) - 2;
    const shown: string[] = [];
    let used = 0;
    for (const [, name] of choices) {
      if (used + codePoints(name) + 3 > budget) break;
      shown.push(name);
      used += codePoints(name) + 3;
    }
    return `${head}${shown.join(SEPARATOR)} …${drill}`;
  });

const labelText = (label: JsonValue): string => (typeof label === 'string' ? label : '');

/** Render a global catalog grouped by kind, housekeeping collapsed. */
export const groupedLegalLines = (
  compacts: ReadonlyArray<CompactAction>,
  aliases: AliasMap | null
): Effect.Effect<ReadonlyArray<string>, PlayerError> =>
  Effect.gen(function* () {
    const order: string[] = [];
    const families = new Map<string, CompactAction[]>();
    for (const compact of compacts) {
      const kind = field(compact, 'kind');
      const key = typeof kind === 'string' ? kind : '';
      const family = families.get(key);
      if (family === undefined) {
        order.push(key);
        families.set(key, [compact]);
      } else {
        family.push(compact);
      }
    }
    const kept: CompactAction[] = [];
    const summaries: string[] = [];
    let collapsed = 0;
    for (const kind of order) {
      const rows = families.get(kind) ?? [];
      const plain: CompactAction[] = [];
      const decorated: CompactAction[] = [];
      for (const row of rows) {
        if (yield* legalRowIsDefault(row)) plain.push(row);
        else decorated.push(row);
      }
      const housekeeping = V2_CATALOG_HOUSEKEEPING.get(kind);
      const summary =
        plain.length < V2_CATALOG_COLLAPSE_MIN
          ? null
          : housekeeping !== undefined
            ? `${housekeeping[0]}: ${plain.length} ${housekeeping[1]} — just legal --kind ${kind} --all`
            : V2_CATALOG_CHOICE_KINDS.has(kind)
              ? yield* catalogChoiceLine(kind, plain, aliases)
              : null;
      if (summary === null) {
        // Rows are emitted in first-seen kind order, so a family that is not
        // collapsed is still grouped rather than interleaved.
        kept.push(...rows);
        continue;
      }
      summaries.push(summary);
      collapsed += plain.length;
      kept.push(...decorated);
    }
    const lines = kept.length > 0 ? [...(yield* legalRows(kept, null, aliases))] : [];
    lines.push(...summaries);
    if (collapsed > 0) {
      lines.push(
        `${collapsed} rows collapsed into ${summaries.length} kinds; ` +
          'add --full for the flat list'
      );
    }
    return lines;
  });
