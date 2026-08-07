/**
 * The shared render primitives.
 *
 * Everything here is a byte surface: `show`, `state`, `legal`, `turn` and `do`
 * all print through these, so each assertion is a fragment of five commands'
 * output at once.
 */
import { describe, expect, test } from 'bun:test';
import { Effect, Either } from 'effect';
import {
  coordinates,
  duration,
  flat,
  formatG,
  jsonLiteral,
  named,
  needInt,
  needText,
  packedLines,
  pageStatus,
  plainName,
  probabilityText,
  pyRound,
  requestedScope,
  revisionLabel,
  rowAlias,
  scalar,
  schemaSummary,
  scopeText,
  signed,
  table,
} from 'src/render/primitives';
import { pagingFooter, pagingStatus } from 'src/render/paging';
import { FIXTURE_CURSOR, FIXTURE_REVISION } from 'test/_fixtures';

const run = <A, E>(effect: Effect.Effect<A, E>): Either.Either<A, E> =>
  Effect.runSync(Effect.either(effect));

const right = <A, E>(either: Either.Either<A, E>): A => {
  expect(Either.isRight(either)).toBe(true);
  if (Either.isLeft(either)) throw new Error('expected success');
  return either.right;
};

describe('scalar', () => {
  test('never produces an empty cell', () => {
    expect(scalar(null)).toBe('-');
    expect(scalar('')).toBe('');
    expect(scalar(true)).toBe('yes');
    expect(scalar(false)).toBe('no');
    expect(scalar(3)).toBe('3');
    expect(scalar(2.5)).toBe('2.5');
  });

  test('a container falls back to the compact wire literal', () => {
    expect(scalar({ b: 1, a: 2 })).toBe('{"a":2,"b":1}');
  });
});

describe('formatG', () => {
  test('matches CPython\'s %g for the cases the renderers hit', () => {
    expect(formatG(0)).toBe('0');
    expect(formatG(2)).toBe('2');
    expect(formatG(2.5)).toBe('2.5');
    expect(formatG(0.125)).toBe('0.125');
    expect(formatG(1234567)).toBe('1.23457e+06');
    expect(formatG(0.00001)).toBe('1e-05');
  });
});

describe('names', () => {
  test('plainName prefers name, then id', () => {
    expect(plainName({ name: 'Warriors', id: 'unit_0' })).toBe('Warriors');
    expect(plainName({ id: 'unit_0' })).toBe('unit_0');
    expect(plainName({})).toBeNull();
  });

  test('named resolves through the alias cache first', () => {
    expect(named({ id: 'unit_0', name: 'Warriors' }, { unit_0: 'u1' })).toBe('u1');
    expect(named({ id: 'unit_0', name: 'Warriors' })).toBe('Warriors');
  });

  test('a typed object with no name renders as a compact digest, never raw JSON', () => {
    expect(named({ type: 'tile', x: 3, y: 4 })).toBe('tile:x=3,y=4');
  });

  test('flat joins a list and never returns an empty cell', () => {
    expect(flat([1, 2, 3])).toBe('1|2|3');
    expect(flat([])).toBe('-');
    expect(flat({ nested: { deep: 1 } })).toBe('…');
  });
});

describe('coordinates', () => {
  test('integer x/y render as @x,y', () => {
    expect(right(run(coordinates({ x: -3, y: 4 })))).toBe('@-3,4');
  });

  test('an object without coordinates is not drift', () => {
    expect(right(run(coordinates({ name: 'x' })))).toBeNull();
  });

  test('non-integer coordinates are drift', () => {
    expect(Either.isLeft(run(coordinates({ x: 1.5, y: 0 })))).toBe(true);
  });
});

describe('table', () => {
  test('pads every column but the last and strips the tail', () => {
    expect(
      table([
        ['a', 'bbb', 'c'],
        ['aaaa', 'b', 'cc'],
      ])
    ).toEqual(['a     bbb  c', 'aaaa  b    cc']);
  });

  test('a short final cell leaves no trailing whitespace', () => {
    expect(table([['aaaa', 'b'], ['a', '']])).toEqual(['aaaa  b', 'a']);
  });

  /**
   * `_table` is `len` / `str.ljust` / `str.rstrip`, and all three count *code
   * points*.  A city name is agent input (`found_city`), so an emoji in one is
   * reachable on a live match: measured in UTF-16 units `'🚀🚀🚀'` is six wide,
   * so it would swallow the whole column and every following cell in the table
   * would sit three bytes left of where CPython puts it.
   */
  test('widths and padding are code points, as CPython counts them', () => {
    expect(
      table([
        ['c1', '🚀🚀🚀', 'sz5'],
        ['c2', 'Berlin', 'sz3'],
      ])
    ).toEqual(['c1  🚀🚀🚀     sz5', 'c2  Berlin  sz3']);
  });

  /**
   * `str.rstrip()` is not `/\s+$/`: Python strips U+001C-U+001F and U+0085 and
   * keeps U+FEFF, and JavaScript does the exact opposite on both.  A terrain or
   * city name carrying either round-trips through this function.
   */
  test('the tail is stripped with CPython whitespace, not the host regex', () => {
    // U+001F is whitespace to `str.rstrip` and is not matched by `\s`.
    expect(table([['a', 'b']])).toEqual(['a  b']);
    // U+FEFF is matched by `\s` and is *not* whitespace to `str.rstrip`.
    expect(table([['a', 'b﻿']])).toEqual(['a  b﻿']);
  });
});

describe('labels', () => {
  test('revisionLabel is revN/tM', () => {
    expect(revisionLabel(FIXTURE_REVISION)).toBe('rev12/t5');
  });

  test('pageStatus prints the cursor to continue with', () => {
    expect(
      pageStatus({
        section: 'units',
        items: [1, 2],
        total_items: 43,
        next_cursor: FIXTURE_CURSOR,
        cursor_expires_at: null,
      })
    ).toBe(`2/43 more --cursor ${FIXTURE_CURSOR}`);
  });

  test('a drained page says complete', () => {
    expect(
      pageStatus({
        section: 'units',
        items: [1],
        total_items: 1,
        next_cursor: null,
        cursor_expires_at: null,
      })
    ).toBe('1/1 complete');
  });

  test('the shared footer builder agrees with pageStatus', () => {
    const page = {
      section: 'units',
      items: [1, 2],
      total_items: 43,
      next_cursor: FIXTURE_CURSOR,
      cursor_expires_at: null,
    };
    expect(pagingFooter(2, 43, FIXTURE_CURSOR, 'just state')).toEqual([pagingStatus(page)]);
  });

  test('requestedScope derives the actor and target types from the IDs', () => {
    expect(requestedScope('unit_abc', '')).toEqual({ actor_id: 'unit_abc', actor_type: 'unit' });
    expect(requestedScope('player_abc', 'relation_x')).toEqual({
      actor_id: 'player_abc',
      actor_type: 'player',
      target_id: 'relation_x',
      target_type: 'relation',
    });
    expect(requestedScope('', 'tile_x')).toBeNull();
  });

  test('scopeText resolves both ends through the alias cache', () => {
    expect(scopeText(null)).toBe('scope=all');
    expect(
      scopeText(
        { actor_id: 'unit_a', actor_type: 'unit', target_id: 'tile_b', target_type: 'tile' },
        { unit_a: 'u1', tile_b: 'T(3,4)' }
      )
    ).toBe('scope=unit u1 target=tile T(3,4)');
  });

  test('probabilityText collapses an equal range', () => {
    expect(probabilityText({ kind: 'attack', minimum_percent: 50, maximum_percent: 50 })).toBe(
      'prob=50%/attack'
    );
    expect(probabilityText({ kind: 'attack', minimum_percent: 10, maximum_percent: 90 })).toBe(
      'prob=10-90%/attack'
    );
  });

  test('an unshaped probability still renders visibly', () => {
    expect(probabilityText(null)).toBe('prob=null');
  });
});

describe('schemaSummary', () => {
  test('an empty schema renders away entirely', () => {
    expect(right(run(schemaSummary({})))).toBe('');
    expect(right(run(schemaSummary({ properties: {} })))).toBe('');
  });

  test('optional properties carry a question mark', () => {
    expect(
      right(
        run(
          schemaSummary({
            properties: { name: { type: 'string' }, size: { type: 'integer' } },
            required: ['name'],
          })
        )
      )
    ).toBe('{name:string,size?:integer}');
  });

  test('an enum prints wire literals, never human words', () => {
    expect(
      right(run(schemaSummary({ properties: { ready: { enum: [true, false] } }, required: ['ready'] })))
    ).toBe('{ready:true|false}');
  });

  test('a long enum is elided after four members', () => {
    expect(
      right(
        run(schemaSummary({ properties: { n: { enum: [1, 2, 3, 4, 5] } }, required: ['n'] }))
      )
    ).toBe('{n:1|2|3|4|…}');
  });
});

describe('numbers and packing', () => {
  test('signed never lets +2 and -2 read alike', () => {
    expect(signed(2)).toBe('+2');
    expect(signed(-2)).toBe('-2');
    expect(signed(0)).toBe('+0');
  });

  test('packedLines fills to the budget rather than one fact per line', () => {
    expect(packedLines(['aaa', 'bbb', 'ccc'], 7)).toEqual(['aaa bbb', 'ccc']);
  });

  test('duration says it the way a person would', () => {
    expect(duration(9)).toBe('9s');
    expect(duration(60)).toBe('1m0s');
    expect(duration(587.004)).toBe('9m47s');
    expect(duration(null)).toBe('?');
  });

  test('rounding is half-to-even, as CPython rounds', () => {
    expect(pyRound(0.5)).toBe(0);
    expect(pyRound(1.5)).toBe(2);
    expect(pyRound(2.5)).toBe(2);
  });
});

describe('drift', () => {
  test('needInt and needText name the missing key', () => {
    const either = run(needInt({ a: 'x' }, 'a', 'unit'));
    expect(Either.isLeft(either)).toBe(true);
    if (Either.isLeft(either)) {
      expect(either.left.message).toBe(
        'cannot render unit a: the validated payload does not match the ' +
          'documented contract; re-run the same command with --json'
      );
    }
    expect(Either.isLeft(run(needText({}, 'name', 'unit')))).toBe(true);
  });

  test('rowAlias numbers positionally without a cache and refuses to invent one with', () => {
    expect(right(run(rowAlias(null, {}, 'id', 'u', 3)))).toBe('u3');
    expect(right(run(rowAlias({ unit_a: 'u1' }, { id: 'unit_a' }, 'id', 'u', 3)))).toBe('u1');
    expect(right(run(rowAlias({}, { id: 'unit_b' }, 'id', 'u', 3)))).toBe('unit_b');
    expect(Either.isLeft(run(rowAlias({}, {}, 'id', 'u', 3)))).toBe(true);
  });

  test('jsonLiteral is the exact wire spelling', () => {
    expect(jsonLiteral({ b: 1, a: 2 })).toBe('{"a":2,"b":1}');
    expect(jsonLiteral(null)).toBe('null');
  });
});
