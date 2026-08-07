/**
 * One legal action as one row.
 *
 * Ports `_LEGAL_SUBJECT_RESERVED` (client.py:3588-3591), `_legal_row`
 * (3673-3760), `_legal_rows` (3763-3775) and `_legal_row_is_default`
 * (3818-3834).
 *
 * The row is `alias · kind · label · detail`, and the 39-char opaque handle is
 * appended only when column 0 is a positional number that resolves nowhere.
 * With an alias cache in hand the alias (or the ID itself) already addresses
 * the row, and `--json` and the mirror still carry every handle.
 *
 * Detail is omit-when-default only.  A non-default probability, legality,
 * consuming flag or variant is exactly what turns a certain move into a
 * gamble, so each one renders with a leading `!` and none of them is ever
 * dropped.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import {
  coordinates,
  drift,
  named,
  probabilityText,
  rowAlias,
  scalar,
  schemaSummary,
  table,
  type AliasMap,
} from 'src/render/primitives';
import { field, hasField, isJsonObject, type JsonObject, type JsonValue } from 'src/schema/primitives';
import type { PageScope } from 'src/schema/page';
import { tileReference } from 'src/services/aliases';
import type { CompactAction } from 'src/services/legal-compact';

/** Subject keys the row already renders under a name of their own. */
export const LEGAL_SUBJECT_RESERVED: ReadonlySet<string> = new Set([
  'operation',
  'actor',
  'target',
  'probability',
  'legality',
  'consuming',
  'variant',
  'gold_cost',
]);

/**
 * CPython's `value in (None, False)`, which compares by `==`.
 *
 * `0 == False` is true in Python, so a numeric zero counts as the default and
 * an integer `1` does not.  Getting this wrong turns a consuming action into a
 * free one in the one column that says a unit is about to be spent.
 */
const isFalseLike = (value: JsonValue): boolean =>
  value === null || value === false || value === 0;

/** `kind`, suffixed with `/operation` unless the kind already ends in it. */
const kindWithOperation = (kind: string, operation: JsonValue): string =>
  typeof operation === 'string' && operation !== '' && !kind.endsWith(`.${operation}`)
    ? `${kind}/${operation}`
    : kind;

/** Render one action: alias, kind, label, target, non-defaults, ID last. */
export const legalRow = (
  alias: string,
  compact: CompactAction,
  scope: PageScope | null,
  aliases: AliasMap | null = null
): Effect.Effect<ReadonlyArray<string>, PlayerError> =>
  Effect.gen(function* () {
    const subject = field(compact, 'subject');
    if (!isJsonObject(subject)) return yield* Effect.fail(drift('legal action subject'));
    const rawKind = field(compact, 'kind');
    if (typeof rawKind !== 'string') return yield* Effect.fail(drift('legal action kind'));
    const kind = kindWithOperation(rawKind, field(subject, 'operation'));
    const lookup = aliases ?? undefined;
    const detail: string[] = [];

    const target = field(compact, 'target');
    // The coordinate read happens first and can itself be drift, exactly as in
    // CPython — a target carrying non-integer `x`/`y` is a contract violation
    // whether or not it also looks like a tile.
    const coordinate = yield* coordinates(target);
    const tile = tileReference(target, 'id');
    if (tile !== null) {
      // A tile target renders as exactly the alias `--target_id` accepts.
      detail.push(`T(${tile.x},${tile.y})`);
    } else if (coordinate !== null) {
      detail.push(coordinate);
    } else if (target !== null) {
      // A non-tile target is addressed the same way as every other entity:
      // through the alias cache first, so a player target prints `p1`.
      detail.push(`→${named(target, lookup)}`);
    }

    const actor = field(subject, 'actor');
    const scopeActor = scope === null ? null : scope.actor_id;
    if (isJsonObject(actor)) {
      const actorId = field(actor, 'id');
      if (typeof actorId === 'string' && actorId !== scopeActor) {
        detail.push(`actor=${lookup?.[actorId] ?? actorId}`);
      }
    } else if (actor !== null) {
      detail.push(`actor=${scalar(actor)}`);
    }

    for (const [key, value] of Object.entries(subject)) {
      if (LEGAL_SUBJECT_RESERVED.has(key)) continue;
      detail.push(`${key}=${named(value, lookup)}`);
    }

    if (hasField(compact, 'probability')) {
      detail.push(`!${probabilityText(field(compact, 'probability'))}`);
    }
    const legality = field(subject, 'legality');
    if (legality !== null && legality !== 'legal') {
      detail.push(`!legality=${scalar(legality)}`);
    }
    const consuming = field(subject, 'consuming');
    if (consuming === true) {
      detail.push('!consuming');
    } else if (!isFalseLike(consuming)) {
      detail.push(`!consuming=${scalar(consuming)}`);
    }
    const variant = field(subject, 'variant');
    if (variant !== null) detail.push(`!variant=${scalar(variant)}`);

    if (hasField(compact, 'gold_cost')) {
      detail.push(`gold=${scalar(field(compact, 'gold_cost'))}`);
    }
    if (hasField(compact, 'gold_range')) {
      const goldRange = field(compact, 'gold_range');
      const bounds = isJsonObject(goldRange)
        ? (['minimum', 'maximum'] as const)
            .filter((bound) => hasField(goldRange, bound))
            .map((bound) => scalar(field(goldRange, bound)))
        : [];
      detail.push(`gold_range=${bounds.join('-')}`);
    }
    const schema = yield* schemaSummary(field(compact, 'argument_schema'));
    if (schema !== '') detail.push(schema);

    const label = field(compact, 'label');
    const row = [alias, kind, typeof label === 'string' ? label : scalar(label), detail.join(' ')];
    if (aliases === null) {
      const actionId = field(compact, 'action_id');
      row.push(typeof actionId === 'string' ? actionId : scalar(actionId));
    }
    return row;
  });

/** Every action as one padded table. */
export const legalRows = (
  compacts: ReadonlyArray<CompactAction>,
  scope: PageScope | null,
  aliases: AliasMap | null = null
): Effect.Effect<ReadonlyArray<string>, PlayerError> =>
  Effect.gen(function* () {
    const rows: Array<ReadonlyArray<string>> = [];
    for (const [index, compact] of compacts.entries()) {
      const alias = yield* rowAlias(aliases, compact, 'action_id', 'a', index + 1);
      rows.push(yield* legalRow(alias, compact, scope, aliases));
    }
    return table(rows);
  });

/**
 * Report whether one row carries nothing but defaults.
 *
 * A row with a non-default probability, legality, consuming flag or variant, or
 * with a gold price, is exactly the decision a summary line would hide — so it
 * is never collapsed, and prints individually even inside a collapsed family.
 */
export const legalRowIsDefault = (
  compact: CompactAction
): Effect.Effect<boolean, PlayerError> => {
  const subject = field(compact, 'subject');
  if (!isJsonObject(subject)) return Effect.fail(drift('legal action subject'));
  const legality = field(subject, 'legality');
  return Effect.succeed(
    !hasField(compact, 'probability') &&
      !hasField(compact, 'gold_cost') &&
      !hasField(compact, 'gold_range') &&
      (legality === null || legality === 'legal') &&
      isFalseLike(field(subject, 'consuming')) &&
      field(subject, 'variant') === null
  );
};

export { kindWithOperation };
export type { JsonObject, JsonValue };
