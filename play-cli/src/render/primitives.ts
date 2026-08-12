/**
 * Shared text-rendering primitives.
 *
 * Ports `_render`, `_echo`, `_drift`, `_need`, `_need_int`, `_need_text`,
 * `_scalar`, `_json_literal`, `_plain_name`, `_flat`, `_named`, `_coordinates`,
 * `_table`, `_revision_label`, `_page_status`, `_requested_scope`,
 * `_scope_text`, `_probability_text`, `_schema_summary` (client.py:3401-3650),
 * `_signed` and `_packed_lines` (4691-4708), and `_duration` (5335-5348).
 *
 * Every function here is byte-critical: `show`, `state`, `legal`, `turn` and
 * `do` all render through them, so a one-space change is a diff in five
 * commands at once.
 */
import { Console, Effect } from 'effect';
import { PlayerError, playerError } from 'src/errors';
import { V2_CITY_ROW_MAX } from 'src/constants';
import { compactJson } from 'src/services/json-output';
// Imported from the module rather than `src/services/mirror`: that barrel pulls
// in `src/render/mirror/*`, every one of which imports this file back.
import { codeLength, ljust, rstrip } from 'src/services/mirror/store';
import { isJsonObject, type JsonObject, type JsonValue } from 'src/schema/primitives';
import type { PageBody, PageScope } from 'src/schema/page';
import type { Revision } from 'src/schema/revision';

export type AliasMap = Readonly<Record<string, string>>;

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

export const render = (lines: ReadonlyArray<string>): Effect.Effect<void> =>
  Effect.forEach(lines, (line) => Console.log(line), { discard: true });

/**
 * Print one progress line now, not whenever the buffer happens to flush.
 *
 * A blocking command that produces nothing for ten minutes is read as a hang —
 * it was, three times, by a human watching a live match.
 */
export const echo = (line: string): Effect.Effect<void> => Console.log(line);

// ---------------------------------------------------------------------------
// Drift
// ---------------------------------------------------------------------------

export const drift = (label: string): PlayerError =>
  playerError(
    `cannot render ${label}: the validated payload does not match the ` +
      'documented contract; re-run the same command with --json'
  );

export const need = (item: JsonValue, key: string, label: string): Effect.Effect<JsonValue, PlayerError> =>
  isJsonObject(item) && key in item
    ? Effect.succeed(item[key] as JsonValue)
    : Effect.fail(drift(`${label} ${key}`));

export const needInt = (
  item: JsonValue,
  key: string,
  label: string
): Effect.Effect<number, PlayerError> =>
  Effect.flatMap(need(item, key, label), (value) =>
    typeof value === 'number' && Number.isInteger(value)
      ? Effect.succeed(value)
      : Effect.fail(drift(`${label} ${key}`))
  );

export const needText = (
  item: JsonValue,
  key: string,
  label: string
): Effect.Effect<string, PlayerError> =>
  Effect.flatMap(need(item, key, label), (value) =>
    typeof value === 'string' && value !== ''
      ? Effect.succeed(value)
      : Effect.fail(drift(`${label} ${key}`))
  );

// ---------------------------------------------------------------------------
// Cells
// ---------------------------------------------------------------------------

const stripTrailingZeros = (text: string): string =>
  text.includes('.') ? text.replace(/0+$/, '').replace(/\.$/, '') : text;

/**
 * CPython's `format(value, "g")`: six significant digits, exponential notation
 * when the exponent leaves `[-4, 6)`, trailing zeros stripped.
 */
export const formatG = (value: number): string => {
  if (Number.isNaN(value)) return 'nan';
  if (!Number.isFinite(value)) return value > 0 ? 'inf' : '-inf';
  if (value === 0) return Object.is(value, -0) ? '-0' : '0';
  const precision = 6;
  const scientific = value.toExponential(precision - 1);
  const exponent = Number.parseInt(scientific.slice(scientific.indexOf('e') + 1), 10);
  if (exponent < -4 || exponent >= precision) {
    const mantissa = stripTrailingZeros(scientific.slice(0, scientific.indexOf('e')));
    const sign = exponent < 0 ? '-' : '+';
    return `${mantissa}e${sign}${String(Math.abs(exponent)).padStart(2, '0')}`;
  }
  return stripTrailingZeros(value.toFixed(Math.max(0, precision - 1 - exponent)));
};

/** CPython's `round()`: half-to-even, unlike `Math.round`'s half-up. */
export const pyRound = (value: number): number => {
  const floor = Math.floor(value);
  const remainder = value - floor;
  if (remainder > 0.5) return floor + 1;
  if (remainder < 0.5) return floor;
  return floor % 2 === 0 ? floor : floor + 1;
};

/** Render one validated JSON value without ever producing an empty cell. */
export const scalar = (value: JsonValue): string => {
  if (value === null) return '-';
  if (typeof value === 'boolean') return value ? 'yes' : 'no';
  if (typeof value === 'number') return Number.isInteger(value) ? String(value) : formatG(value);
  if (typeof value === 'string') return value;
  return compactJson(value);
};

/** Render one value exactly as it must appear on the wire. */
export const jsonLiteral = (value: JsonValue): string => compactJson(value);

/** Return the display name a payload object carries, or `null`. */
export const plainName = (value: JsonValue): string | null => {
  if (!isJsonObject(value)) return null;
  const name = value['name'];
  if (typeof name === 'string' && name !== '') return name;
  const identifier = value['id'];
  if (typeof identifier === 'string' && identifier !== '') return identifier;
  return null;
};

/** Render one nested value on a single line, never as raw JSON. */
export const flat = (value: JsonValue): string => {
  if (Array.isArray(value)) {
    const joined = value.map((item) => flat(item)).join('|');
    return joined === '' ? '-' : joined;
  }
  if (isJsonObject(value)) {
    const plain = plainName(value);
    if (plain !== null) return plain;
    return 'type' in value ? scalar(value['type']) : '…';
  }
  return scalar(value);
};

/**
 * Name one payload object as the agent can type it back.
 *
 * An object is resolved through the alias cache first, then by its own name or
 * ID.  An object carrying neither renders as a compact typed digest — never as
 * `json.dumps` of the whole payload, which would put more bytes in the row than
 * the field it replaced.
 */
export const named = (value: JsonValue, aliases?: AliasMap  ): string => {
  if (!isJsonObject(value)) return scalar(value);
  const identifier = value['id'];
  if (typeof identifier === 'string' && identifier !== '' && aliases !== undefined) {
    const alias = aliases[identifier];
    if (alias !== undefined && alias !== '') return alias;
  }
  const plain = plainName(value);
  if (plain !== null) return plain;
  const kind = value['type'];
  if (typeof kind === 'string' && kind !== '') {
    const details = Object.keys(value)
      .sort((left, right) => (left < right ? -1 : left > right ? 1 : 0))
      .filter((key) => key !== 'type' && value[key] !== null)
      .map((key) => `${key}=${flat(value[key] as JsonValue)}`);
    return kind + (details.length > 0 ? `:${details.join(',')}` : '');
  }
  return flat(value);
};

/** Return `@x,y` when a validated payload carries integer coordinates. */
export const coordinates = (value: JsonValue): Effect.Effect<string | null, PlayerError> => {
  if (!isJsonObject(value) || !('x' in value) || !('y' in value)) return Effect.succeed(null);
  const x = value['x'];
  const y = value['y'];
  if (typeof x !== 'number' || !Number.isInteger(x) || typeof y !== 'number' || !Number.isInteger(y)) {
    return Effect.fail(drift('tile coordinates'));
  }
  return Effect.succeed(`@${x},${y}`);
};

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

/**
 * Pad every column to its widest cell, join with two spaces, strip the tail.
 * The final cell of a row is never padded, which is what keeps a ragged last
 * column from carrying trailing spaces into the byte diff.
 *
 * Widths and padding are measured in **code points** and the tail is stripped
 * with **CPython's** whitespace class, because `_table` is `len` / `str.ljust`
 * / `str.rstrip` and none of the three agrees with its JavaScript lookalike:
 * `"🚀".length` is 2 where `len("🚀")` is 1 (so a column holding one emoji
 * loses a byte of padding per unit, on every row of the table), and
 * `/\s+$/` strips U+FEFF that Python keeps and keeps U+001C-U+001F and U+0085
 * that Python strips.  City names are agent-supplied through `found_city`, so
 * this is reachable input rather than a theoretical one — PORT_MAP §1's
 * round-2 rule ("never `.length`/`.slice`/`.padEnd`") exists for exactly this
 * function.  U04 owns the three helpers; `src/services/mirror/table.ts`
 * already renders the mirror's own tables with them.
 */
export const table = (rows: ReadonlyArray<ReadonlyArray<string>>): ReadonlyArray<string> => {
  const widths: number[] = [];
  for (const row of rows) {
    row.forEach((cell, index) => {
      const current = widths[index];
      const width = codeLength(cell);
      widths[index] = current === undefined ? width : Math.max(current, width);
    });
  }
  return rows.map((row) =>
    rstrip(
      row
        .map((cell, index) =>
          index === row.length - 1 ? cell : ljust(cell, widths[index] ?? codeLength(cell))
        )
        .join('  ')
    )
  );
};

// ---------------------------------------------------------------------------
// Labels
// ---------------------------------------------------------------------------

export const revisionLabel = (revision: Revision): string =>
  `rev${revision.revision}/t${revision.turn}`;

export const pageStatus = <Item>(page: PageBody<Item>): string => {
  const shown = page.items.length;
  if (page.next_cursor === null) return `${shown}/${page.total_items} complete`;
  return `${shown}/${page.total_items} more --cursor ${page.next_cursor}`;
};

/** Describe the scope this command asked for, from its own arguments. */
export const requestedScope = (actorId: string, targetId: string): PageScope | null => {
  if (actorId === '') return null;
  const actorType = (actorId.split('_', 1)[0] ?? '') as PageScope['actor_type'];
  if (targetId === '') return { actor_id: actorId, actor_type: actorType };
  return {
    actor_id: actorId,
    actor_type: actorType,
    target_id: targetId,
    target_type: targetId.split('_', 1)[0] ?? '',
  };
};

export const scopeText = (
  scope: PageScope | null | undefined,
  aliases?: AliasMap  
): string => {
  if (scope == null) return 'scope=all';
  const table_ = aliases ?? {};
  let text = `scope=${scope.actor_type} ${table_[scope.actor_id] ?? scope.actor_id}`;
  if (scope.target_id !== undefined) {
    text += ` target=${scope.target_type} ${table_[scope.target_id] ?? scope.target_id}`;
  }
  return text;
};

export const probabilityText = (probability: JsonValue): string => {
  if (!isJsonObject(probability)) {
    // A present-but-unshaped probability is still a non-default, and a
    // non-default must always render visibly.  The JSON literal is used so
    // `null` cannot be mistaken for an empty cell.
    return `prob=${jsonLiteral(probability)}`;
  }
  const kind = probability['kind'];
  const low = probability['minimum_percent'];
  const high = probability['maximum_percent'];
  if (
    typeof kind !== 'string' ||
    kind === '' ||
    typeof low !== 'number' ||
    typeof high !== 'number'
  ) {
    return `prob=${scalar(probability)}`;
  }
  if (low === high) return `prob=${formatG(low)}%/${kind}`;
  return `prob=${formatG(low)}-${formatG(high)}%/${kind}`;
};

/**
 * Summarize an argument schema as `{name:type,…}`.
 * An empty schema renders away entirely instead of shipping `{}` per item.
 */
export const schemaSummary = (schema: JsonValue): Effect.Effect<string, PlayerError> => {
  if (!isJsonObject(schema)) return Effect.fail(drift('action arguments schema'));
  const properties = schema['properties'];
  if (properties === undefined || properties === null || properties === false || properties === 0) {
    return Effect.succeed('');
  }
  if (!isJsonObject(properties)) {
    if (Array.isArray(properties) && properties.length === 0) return Effect.succeed('');
    return Effect.fail(drift('action arguments schema'));
  }
  if (Object.keys(properties).length === 0) return Effect.succeed('');
  const required = schema['required'];
  const requiredNames = new Set(
    Array.isArray(required) ? required.filter((name): name is string => typeof name === 'string') : []
  );
  const parts = Object.entries(properties).map(([name, specification]) => {
    const kind = ((): string => {
      if (!isJsonObject(specification)) return '?';
      const choices = specification['enum'];
      const declared = specification['type'];
      if (Array.isArray(choices) && choices.length > 0) {
        // An enum member is printed as the JSON literal the wire needs, never
        // as a human word: `{ready:yes}` would teach the agent to send the
        // string "yes" where the schema wants true.
        const shown = choices.slice(0, 4).map((choice) => jsonLiteral(choice));
        if (choices.length > 4) shown.push('…');
        return shown.join('|');
      }
      if (typeof declared === 'string' && declared !== '') return declared;
      if (Array.isArray(declared) && declared.length > 0) {
        return declared.map((item) => scalar(item)).join('|');
      }
      return '?';
    })();
    return `${name}${requiredNames.has(name) ? '' : '?'}:${kind}`;
  });
  return Effect.succeed(`{${parts.join(',')}}`);
};

/**
 * Name a row by its durable alias.
 *
 * Without a cache the row is display-only and is numbered positionally.  With a
 * cache in hand the row always shows something that resolves.
 */
export const rowAlias = (
  aliases: AliasMap | null,
  item: JsonValue,
  key: string,
  prefix: string,
  index: number
): Effect.Effect<string, PlayerError> => {
  if (aliases === null) return Effect.succeed(`${prefix}${index}`);
  const identifier = isJsonObject(item) ? item[key] : null;
  if (typeof identifier !== 'string' || identifier === '') {
    return Effect.fail(drift('entity id'));
  }
  return Effect.succeed(aliases[identifier] ?? identifier);
};

// ---------------------------------------------------------------------------
// Numbers and packing
// ---------------------------------------------------------------------------

/** Render a surplus with its sign, so `+2` and `-2` never read alike. */
export const signed = (value: JsonValue): string => {
  if (typeof value !== 'number' || !Number.isInteger(value)) return scalar(value);
  return value < 0 ? String(value) : `+${value}`;
};

/** Fill lines to the row budget rather than emitting one fact per line. */
export const packedLines = (
  parts: ReadonlyArray<string>,
  limit: number = V2_CITY_ROW_MAX
): ReadonlyArray<string> =>
  parts.reduce<string[]>((lines, part) => {
    const last = lines[lines.length - 1];
    if (last !== undefined && last.length + 1 + part.length <= limit) {
      lines[lines.length - 1] = `${last} ${part}`;
    } else {
      lines.push(part);
    }
    return lines;
  }, []);

/**
 * Render a phase-scale duration the way a person says it out loud.
 *
 * `587.004s` is three digits of precision on a number whose meaning is "about
 * ten minutes", and a reader has to do the division before the fact lands.
 */
export const duration = (seconds: JsonValue): string => {
  if (typeof seconds !== 'number' || !Number.isFinite(seconds)) return '?';
  const whole = pyRound(Math.max(0, seconds));
  if (whole < 60) return `${whole}s`;
  return `${Math.floor(whole / 60)}m${whole % 60}s`;
};

/** Re-exported so renderers do not each import the schema layer for one guard. */
export { isJsonObject, type JsonObject, type JsonValue };
