/**
 * The per-output arithmetic table inside `city_detail`.
 *
 * Ports `_city_output_rows` (client.py:4709-4761).
 *
 * A column that is zero on every row carries no decision and is dropped, and so
 * is a step of the base/gross/net/surplus chain that merely repeats the step
 * before it.  An unstressed city therefore prints `base used surplus`, and a
 * city bleeding shields to corruption and disorder prints the `waste` and
 * `unhappy` terms that explain exactly where they went.
 */
import { Effect } from 'effect';
import type { PlayerError } from 'src/errors';
import {
  V2_CITY_OUTPUTS,
  V2_CITY_OUTPUT_CHAIN,
  V2_CITY_OUTPUT_COLUMNS,
} from 'src/constants';
import { drift, isJsonObject, scalar, signed, type JsonValue } from 'src/render/primitives';

type OutputRow = ReadonlyMap<string, number>;

export const cityOutputRows = (
  outputs: JsonValue
): Effect.Effect<ReadonlyArray<ReadonlyArray<string>>, PlayerError> =>
  Effect.gen(function* () {
    if (!isJsonObject(outputs) || Object.keys(outputs).length === 0) return [];
    const present = new Set(Object.keys(outputs));
    const known: ReadonlySet<string> = new Set<string>(V2_CITY_OUTPUTS);
    const names = [
      ...V2_CITY_OUTPUTS.filter((name) => present.has(name)),
      ...[...present].filter((name) => !known.has(name)).sort(),
    ];
    const values: Array<readonly [string, OutputRow]> = [];
    for (const name of names) {
      const metrics = outputs[name] ?? null;
      if (!isJsonObject(metrics)) return yield* Effect.fail(drift('city output'));
      const row = new Map<string, number>();
      for (const [column, key] of V2_CITY_OUTPUT_COLUMNS) {
        const number = metrics[key] ?? null;
        if (number === null) continue;
        if (typeof number !== 'number' || !Number.isInteger(number)) {
          return yield* Effect.fail(drift('city output'));
        }
        row.set(column, number);
      }
      // An output this city neither makes nor spends is a row of zeroes.
      if ([...row.values()].some((number) => number !== 0)) values.push([name, row]);
    }
    if (values.length === 0) return [];

    const columns = V2_CITY_OUTPUT_COLUMNS.map(([column]) => column).filter(
      (column) =>
        values.every(([, row]) => row.has(column)) &&
        (column === 'surplus' || values.some(([, row]) => row.get(column) !== 0))
    );
    const chain = V2_CITY_OUTPUT_CHAIN.filter((column) => columns.includes(column));
    for (let position = chain.length - 1; position > 0; position -= 1) {
      const column = chain[position] as string;
      const previous = chain[position - 1] as string;
      if (
        column !== 'surplus' &&
        values.every(([, row]) => row.get(column) === row.get(previous))
      ) {
        columns.splice(columns.indexOf(column), 1);
        chain.splice(position, 1);
      }
    }
    if (columns.length === 0) return [];

    return [
      ['output', ...columns],
      ...values.map(([name, row]) => [
        name,
        ...columns.map((column) => {
          const number = row.get(column) as number;
          return column === 'surplus' ? signed(number) : scalar(number);
        }),
      ]),
    ];
  });
