/**
 * The `overview` state section.
 *
 * Ports `_economy_text` (client.py:4545-4567), `_research_text` (4570-4587),
 * `_score_text` (4590-4617) and `_render_overview` (4620-4652).
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import {
  drift,
  isJsonObject,
  plainName,
  scalar,
  type AliasMap,
  type JsonValue,
} from 'src/render/primitives';

const RATE_KEYS = ['tax', 'luxury', 'science'] as const;

/**
 * CPython's `if value:` over a decoded JSON value.
 *
 * State-page items are validated by `_json_value` alone (client.py:1482), so an
 * empty list or empty object reaches these renderers unchanged — and CPython
 * calls both of them false. JavaScript calls both true, so every `if value:` in
 * the ported span goes through this one helper rather than a hand-enumerated
 * list of scalar falsy values.
 */
export const isPythonTruthy = (value: JsonValue | undefined): boolean => {
  if (value === undefined || value === null || value === false) return false;
  if (value === 0 || value === '') return false;
  if (Array.isArray(value)) return value.length > 0;
  if (isJsonObject(value)) return Object.keys(value).length > 0;
  return true;
};

export const economyText = (player: JsonValue): Effect.Effect<string, PlayerError> => {
  if (!isJsonObject(player)) return Effect.fail(drift('overview player'));
  // Only a real name leads the line: `named`'s digest of an unnamed object
  // would repeat the very facts this line goes on to print.
  const plain = plainName(player);
  const parts: string[] = plain === null ? [] : [plain];
  const nation = player['nation'] ?? null;
  if (typeof nation === 'string' && nation !== '') parts.push(`(${nation})`);
  if ('government' in player) parts.push(scalar(player['government'] ?? null));
  const economy = player['economy'] ?? null;
  if (isJsonObject(economy)) {
    if ('gold' in economy) parts.push(`gold ${scalar(economy['gold'] ?? null)}`);
    const rates = RATE_KEYS.filter((key) => key in economy).map(
      (key) => `${key.slice(0, 3)}${scalar(economy[key] ?? null)}`
    );
    if (rates.length > 0) parts.push(rates.join('/'));
  }
  return Effect.succeed(parts.join(' '));
};

export const researchText = (research: JsonValue): Effect.Effect<string, PlayerError> => {
  if (!isJsonObject(research)) return Effect.fail(drift('overview research'));
  const target = research['target'] ?? null;
  const parts = [`research ${isPythonTruthy(target) ? scalar(target) : 'NO TARGET'}`];
  if ('bulbs_researched' in research && 'cost' in research) {
    parts.push(
      `${scalar(research['bulbs_researched'] ?? null)}/${scalar(research['cost'] ?? null)}`
    );
  }
  if ('output' in research) parts.push(`+${scalar(research['output'] ?? null)}/turn`);
  const goal = research['goal'] ?? null;
  if (isPythonTruthy(goal)) parts.push(`goal ${scalar(goal)}`);
  return Effect.succeed(parts.join(' '));
};

/**
 * Name the objective this seat is graded on, as precisely as it is known.
 *
 * `exact` is the engine's own number when the boundary can carry it; otherwise
 * the seat prints the lower bound it can prove from its own rows, marked `>=`
 * so it is never mistaken for the score itself.  An older server sends no score
 * at all and this line simply does not appear.
 */
export const scoreText = (score: JsonValue): string => {
  if (!isJsonObject(score)) return '';
  const exact = score['exact'] ?? null;
  let text: string;
  if (typeof exact === 'number' && Number.isInteger(exact)) {
    text = `score ${exact}`;
  } else {
    const lower = score['lower_bound'] ?? null;
    if (typeof lower !== 'number' || !Number.isInteger(lower)) return '';
    text = `score >=${lower}`;
  }
  const components = score['components'] ?? null;
  if (isJsonObject(components)) {
    const named = Object.entries(components)
      .filter(([, value]) => isPythonTruthy(value))
      .map(([key, value]) => `${key} ${scalar(value ?? null)}`)
      .join(', ');
    if (named !== '') text += ` (${named})`;
  }
  return text;
};

export const renderOverview = (
  items: ReadonlyArray<JsonValue>,
  aliases: AliasMap | null = null
): Effect.Effect<ReadonlyArray<string>, PlayerError> =>
  Effect.gen(function* () {
    void aliases;
    const lines: string[] = [];
    for (const item of items) {
      const fields = isJsonObject(item) ? item : {};
      const head = [`turn ${scalar(fields['turn'] ?? null)}`];
      if ('phase' in fields && 'phase_count' in fields) {
        head.push(
          `phase ${scalar(fields['phase'] ?? null)}/${scalar(fields['phase_count'] ?? null)}`
        );
      }
      if ('client_state' in fields) head.push(scalar(fields['client_state'] ?? null));
      head.push(yield* economyText(fields['player'] ?? null));
      const score = scoreText(fields['score'] ?? null);
      if (score !== '') head.push(score);
      lines.push(head.join(' | '));
      lines.push(yield* researchText(fields['research'] ?? null));
      const gameMap = fields['map'] ?? null;
      if (isJsonObject(gameMap)) {
        lines.push(
          `map ${scalar(gameMap['width'] ?? null)}x${scalar(gameMap['height'] ?? null)} ` +
            scalar(gameMap['topology'] ?? null)
        );
      }
      const counts = fields['counts'] ?? null;
      if (isJsonObject(counts)) {
        lines.push(
          `counts ${Object.entries(counts)
            .filter(([, value]) => isPythonTruthy(value))
            .map(([key, value]) => `${key} ${scalar(value ?? null)}`)
            .join(' ')}`
        );
      }
    }
    return lines;
  });
