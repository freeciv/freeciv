/**
 * The imperative ratchet: try/catch is gone, infinite loops live only in the
 * two CPython-scanner ports, and `let` budgets only go DOWN.
 *
 * Why an allowlist instead of zero: `src/render/show-regex.ts` (sre_parse) and
 * `src/services/canonical-body.ts` (json.scanner) are faithful ports of
 * CPython's cursor-state machines, proven byte-identical on a 77k-case
 * differential corpus and perf-measured; their state already lives in classes
 * (the sanctioned pattern), and rewriting their internals risks parity for
 * zero user-visible gain.  Everything else is pinned at its current count —
 * lower a number when you clean a file; never raise one.
 */
import { describe, expect, test } from 'bun:test';
import * as fs from 'node:fs';
import * as path from 'node:path';

const SRC = path.join(import.meta.dir, '..', 'src');

const sources = (): ReadonlyArray<readonly [string, string]> => {
  const walk = (dir: string): ReadonlyArray<string> =>
    fs
      .readdirSync(dir, { withFileTypes: true })
      .flatMap((entry) =>
        entry.isDirectory()
          ? walk(path.join(dir, entry.name))
          : entry.name.endsWith('.ts')
            ? [path.join(dir, entry.name)]
            : []
      );
  return walk(SRC).map((file) => [path.relative(SRC, file), fs.readFileSync(file, 'utf8')] as const);
};

const count = (text: string, pattern: RegExp): number => text.match(pattern)?.length ?? 0;

/** The CPython scanner ports; see the module docstring. */
const PARSER_FILES = new Set(['render/show-regex.ts', 'services/canonical-body.ts']);

/**
 * Remaining `let` debt outside the parsers, pinned.  The heaviest holder is
 * `do.cmd.ts`'s order state machine — a deliberate, careful conversion, not a
 * sweep candidate.  Delete an entry when its file reaches zero.
 */
const LET_BUDGET: Readonly<Record<string, number>> = {
  'render/show-regex.ts': 36,
  'services/canonical-body.ts': 17,
  'commands/do.cmd.ts': 14,
  'services/orders/resolve.ts': 7,
  'render/health.ts': 4,
  'render/show-unicode.ts': 4,
  'services/orders/rebind.ts': 4,
  'render/state/tiles.ts': 3,
  'services/batch-persist.ts': 3,
  'services/invites.ts': 3,
  'services/pregame.ts': 3,
  'render/decisions.ts': 2,
  'render/legal/grouped.ts': 2,
  'render/mirror/map.ts': 2,
  'schema/receipt.ts': 2,
  'services/aliases.ts': 2,
  'services/decisions.ts': 2,
  'services/orders/arguments.ts': 2,
  'services/v2-client.ts': 2,
  'commands/start.cmd.ts': 1,
  'commands/turn.cmd.ts': 1,
  'render/legal/equivalence.ts': 1,
  'render/legal/page.ts': 1,
  'render/phase.ts': 1,
  'render/primitives.ts': 1,
  'render/state/cities.ts': 1,
  'render/state/city-detail.ts': 1,
  'render/state/city-outputs.ts': 1,
  'render/state/diplomacy.ts': 1,
  'render/state/overview.ts': 1,
  'render/state/units.ts': 1,
  'render/turn.ts': 1,
  'schema/health.ts': 1,
  'schema/page.ts': 1,
  'services/alias-refresh.ts': 1,
  'services/do-drain.ts': 1,
  'services/json-output.ts': 1,
  'services/monitor-hook.ts': 1,
  'services/monitor-loop.ts': 1,
  'services/pending-catalogs.ts': 1,
  'services/receipt-ledger.ts': 1,
};

describe('the imperative ratchet', () => {
  test('no literal try/catch anywhere in src', () => {
    for (const [file, text] of sources()) {
      expect(count(text, /(?<![A-Za-z])try\s*\{/g), file).toBe(0);
    }
  });

  test('infinite loops live only in the CPython scanner ports', () => {
    for (const [file, text] of sources()) {
      const loops = count(text, /for \(;;\)|while \(true\)/g);
      if (!PARSER_FILES.has(file)) {
        expect(loops, file).toBe(0);
      }
    }
  });

  test('let budgets only go down', () => {
    for (const [file, text] of sources()) {
      const lets = count(text, /(^|[^A-Za-z])let /gm);
      const budget = LET_BUDGET[file] ?? 0;
      expect(lets, `${file} has ${lets} lets against a budget of ${budget}`).toBeLessThanOrEqual(
        budget
      );
    }
  });

  test('no module-level mutable bindings', () => {
    for (const [file, text] of sources()) {
      expect(count(text, /^let |^var /gm), file).toBe(0);
    }
  });
});
