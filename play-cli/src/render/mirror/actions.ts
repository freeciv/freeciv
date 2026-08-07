/**
 * `state/options/<alias>.txt` — one actor's legal-action catalog.
 *
 * Ports `play/state_mirror.py:976-1074` (`_argument_names`, `_target_text`,
 * `_action_flags`, `_render_actions`).
 *
 * The `flags` column is **omit-when-default, never omit-by-field-name**
 * (redesign doc §5 P0.2): each field is hidden only when it holds its exact
 * default value, so a 30 %-success spy mission can never render like a certain
 * one.  The `alias` column is whatever the CLI's alias layer named the action —
 * the mirror never mints an `aN`, because a fabricated one would be typable and
 * would resolve to a *different* capability.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import { isJsonObject, type JsonObject } from 'src/schema/primitives';
import {
  DEFAULT_PROBABILITY,
  cell,
  mirrorError,
  type MirrorAliases,
} from 'src/services/mirror';
import type { RenderedSection } from 'src/render/mirror/section';

export const ACTION_COLUMNS: ReadonlyArray<string> = [
  'alias',
  'kind',
  'target',
  'args',
  'label',
  'flags',
];

/** `_argument_names` — `{name* count}`, required arguments starred. */
export const argumentNames = (schema: unknown): string => {
  if (!isJsonObject(schema)) return '-';
  const properties = schema['properties'];
  if (!isJsonObject(properties) || Object.keys(properties).length === 0) return '-';
  const required = schema['required'];
  const names = new Set<unknown>(Array.isArray(required) ? required : []);
  return `{${Object.keys(properties)
    .map((name) => (names.has(name) ? `${cell(name)}*` : cell(name)))
    .join(' ')}}`;
};

/** `isinstance(value, int)` for a coordinate — a JSON integer, never a bool. */
const isCoordinate = (value: unknown): value is number =>
  typeof value === 'number' && Number.isInteger(value);

/** `_target_text` — coordinates first, then a name, then the bare type. */
export const targetText = (subject: JsonObject): string => {
  const target = subject['target'];
  if (!isJsonObject(target)) return '-';
  const x = target['x'];
  const y = target['y'];
  if (isCoordinate(x) && isCoordinate(y)) return `${x},${y}`;
  const name = target['name'];
  if (typeof name === 'string' && name !== '') return cell(name);
  const kind = target['type'];
  return typeof kind === 'string' ? cell(kind) : '-';
};

/** Python's `probability == _DEFAULT_PROBABILITY` — exact key set, equal values. */
const isDefaultProbability = (value: unknown): boolean => {
  if (!isJsonObject(value)) return false;
  const defaults = Object.entries(DEFAULT_PROBABILITY);
  return (
    Object.keys(value).length === defaults.length &&
    defaults.every(([key, expected]) => Object.hasOwn(value, key) && value[key] === expected)
  );
};

const probabilityFlag = (probability: unknown): string => {
  if (!isJsonObject(probability)) return `p=${cell(probability)}`;
  const kind = cell(probability['kind']);
  const low = probability['minimum_percent'] ?? null;
  const high = probability['maximum_percent'] ?? null;
  if (low === null && high === null) return `p=${kind}`;
  return low === high
    ? `p=${kind}:${cell(low)}%`
    : `p=${kind}:${cell(low)}-${cell(high)}%`;
};

/** `_action_flags` — everything about this capability that is not the default. */
export const actionFlags = (subject: JsonObject, scopeActor: string | null): string => {
  const flags: string[] = [];
  const probability = subject['probability'];
  if (Object.hasOwn(subject, 'probability') && !isDefaultProbability(probability)) {
    flags.push(probabilityFlag(probability));
  }
  const legality = subject['legality'];
  if (Object.hasOwn(subject, 'legality') && legality !== 'legal') {
    flags.push(`legality=${cell(legality)}`);
  }
  if (subject['consuming'] === true) flags.push('consuming');
  const variant = subject['variant'];
  if (variant !== undefined && variant !== null && Object.hasOwn(subject, 'variant')) {
    flags.push(`variant=${cell(variant)}`);
  }
  const target = subject['target'];
  const own = subject['gold_cost'] ?? null;
  const gold = own === null && isJsonObject(target) ? (target['gold_cost'] ?? null) : own;
  if (gold !== null) flags.push(`gold=${cell(gold)}`);
  const actor = subject['actor'];
  if (isJsonObject(actor)) {
    const actorId = actor['id'] ?? null;
    if (scopeActor === null || actorId !== scopeActor) {
      flags.push(`actor=${cell(actor['type'])}:${cell(actorId)}`);
    }
  }
  return flags.length === 0 ? '-' : flags.join(' ');
};

/** `_render_actions` — project one legal-actions page into catalog rows. */
export const renderActions = (
  items: ReadonlyArray<unknown>,
  aliases: MirrorAliases | null | undefined,
  scopeActor: string | null
): Effect.Effect<RenderedSection, PlayerError> => {
  const rows: Array<ReadonlyArray<string>> = [];
  for (const item of items) {
    if (!isJsonObject(item)) {
      return Effect.fail(mirrorError('legal action descriptor is not an object'));
    }
    const subject = item['subject'];
    if (!isJsonObject(subject)) {
      return Effect.fail(mirrorError('legal action descriptor carries no subject'));
    }
    const actionId = cell(item['action_id']);
    const named =
      aliases === null || aliases === undefined || !Object.hasOwn(aliases, actionId)
        ? undefined
        : aliases[actionId];
    rows.push([
      named === undefined ? '-' : cell(named),
      cell(item['kind']),
      targetText(subject),
      argumentNames(item['arguments_schema']),
      cell(item['label']),
      actionFlags(subject, scopeActor),
    ]);
  }
  return Effect.succeed({ columns: ACTION_COLUMNS, rows });
};
