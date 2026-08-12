/**
 * The city-investigation observation: provenance, identity, and the population
 * identity that makes the six feeling rows one coherent reading.
 *
 * The population cases are the interesting ones.  A CITY_INFO capture that is
 * torn — taken while the server was mid-update — shows up as a stage whose
 * moods do not sum to the city size, and a decoder that let it through would
 * hand an agent a city that has five citizens in one row and four in the next.
 */

import { describe, expect, test } from 'bun:test';
import { Either } from 'effect';
import {
  CityInvestigationObservation,
  decodeInvestigation,
  decodeInvestigationAt,
  FEELING_STAGES,
  feelingPopulation,
  IMPROVEMENTS_MAX,
  MOOD_KEYS,
  specialistPopulation,
  SPECIALISTS_MAX,
} from 'src/agent/observation';
import { decodeRevision } from 'src/agent/revision';
import type { JsonObject, JsonValue } from 'src/json';
import { encodeTolerant } from 'src/tolerant';
import {
  cityWire,
  feelingsWire,
  investigationWire,
  NEXT_REVISION,
  REPUBLISHED_REVISION,
  REVISION,
} from 'test/agent/wire-fixtures';

const accepts = (either: Either.Either<unknown, unknown>): boolean => Either.isRight(either);

const refusalOf = (either: Either.Either<unknown, { readonly message: string }>): string =>
  Either.isLeft(either) ? either.left.message : '<accepted>';

const revision = Either.getOrThrowWith(decodeRevision(REVISION), () => new Error('bad fixture'));

/** The reference observation with `overrides` merged into its city block. */
const withCity = (overrides: JsonObject): unknown =>
  investigationWire({ city: cityWire(overrides) });

/** The reference observation with a replacement `citizens` block. */
const withCitizens = (citizens: JsonValue, size = 4): unknown =>
  withCity({ size, citizens });

// ---------------------------------------------------------------------------
// Provenance
// ---------------------------------------------------------------------------

describe('provenance', () => {
  test('the reference capture decodes', () => {
    expect(accepts(decodeInvestigation(investigationWire()))).toBe(true);
  });

  test('all three provenance literals are pinned', () => {
    for (const [key, wrong] of [
      ['type', 'unit_investigation'],
      ['source', 'sidecar_guess'],
      ['freshness', 'current'],
    ] as const) {
      expect(accepts(decodeInvestigation(investigationWire({ [key]: wrong })))).toBe(false);
    }
  });

  test('bound to a revision, it must name that exact revision', () => {
    const decode = decodeInvestigationAt(revision);
    expect(accepts(decode(investigationWire()))).toBe(true);
    expect(refusalOf(decode(investigationWire({ state_revision: NEXT_REVISION })))).toContain(
      'different state revision',
    );
  });

  test('same counters with a fresh token is a different state', () => {
    const decode = decodeInvestigationAt(revision);
    expect(accepts(decode(investigationWire({ state_revision: REPUBLISHED_REVISION })))).toBe(
      false,
    );
  });

  test('unbound, any well-formed revision is accepted', () => {
    expect(accepts(decodeInvestigation(investigationWire({ state_revision: NEXT_REVISION })))).toBe(
      true,
    );
  });
});

// ---------------------------------------------------------------------------
// The city
// ---------------------------------------------------------------------------

describe('the investigated city', () => {
  test('a size-zero city does not exist', () => {
    expect(accepts(decodeInvestigation(withCity({ size: 0, citizens: { feelings: feelingsWire(0), specialists: [] } })))).toBe(
      false,
    );
  });

  test('a blank name is refused', () => {
    expect(accepts(decodeInvestigation(withCity({ name: '' })))).toBe(false);
  });

  test('production kind is a closed two-value enum', () => {
    expect(
      accepts(
        decodeInvestigation(
          withCity({ production: { id: 'wonder_1', kind: 'wonder', name: 'Pyramids' } }),
        ),
      ),
    ).toBe(false);
  });

  test('a negative shield surplus is legal — a city can eat its stock', () => {
    expect(accepts(decodeInvestigation(withCity({ shields: { stock: 12, surplus: -3 } })))).toBe(
      true,
    );
  });

  test('a negative shield stock is not', () => {
    expect(accepts(decodeInvestigation(withCity({ shields: { stock: -1, surplus: 0 } })))).toBe(
      false,
    );
  });

  test('a fractional count is refused, not rounded', () => {
    expect(accepts(decodeInvestigation(withCity({ shields: { stock: 1.5, surplus: 0 } })))).toBe(
      false,
    );
  });
});

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

describe('identity within the city', () => {
  test('improvements must be distinct by id', () => {
    const wire = withCity({
      improvements: [
        { id: 'b_1', name: 'Barracks' },
        { id: 'b_1', name: 'Granary' },
      ],
    });
    expect(refusalOf(decodeInvestigation(wire))).toContain('improvement ids are not distinct');
  });

  test('improvements must be distinct by name', () => {
    const wire = withCity({
      improvements: [
        { id: 'b_1', name: 'Barracks' },
        { id: 'b_2', name: 'Barracks' },
      ],
    });
    expect(refusalOf(decodeInvestigation(wire))).toContain('improvement names are not distinct');
  });

  test('specialists must be distinct by id and by name', () => {
    const twoSpecialists = (rows: ReadonlyArray<JsonObject>): unknown =>
      withCity({
        size: 4,
        citizens: { feelings: feelingsWire(2), specialists: rows },
      });
    expect(
      refusalOf(
        decodeInvestigation(
          twoSpecialists([
            { id: 's_1', name: 'Scientist', count: 1 },
            { id: 's_1', name: 'Taxman', count: 1 },
          ]),
        ),
      ),
    ).toContain('specialist ids are not distinct');
    expect(
      refusalOf(
        decodeInvestigation(
          twoSpecialists([
            { id: 's_1', name: 'Scientist', count: 1 },
            { id: 's_2', name: 'Scientist', count: 1 },
          ]),
        ),
      ),
    ).toContain('specialist names are not distinct');
  });

  test('the list bounds are the Python\'s', () => {
    expect(IMPROVEMENTS_MAX).toBe(1024);
    expect(SPECIALISTS_MAX).toBe(256);
    const tooMany = Array.from({ length: IMPROVEMENTS_MAX + 1 }, (_unused, index) => ({
      id: `b_${String(index)}`,
      name: `Improvement ${String(index)}`,
    }));
    expect(accepts(decodeInvestigation(withCity({ improvements: tooMany })))).toBe(false);
  });
});

// ---------------------------------------------------------------------------
// Feelings and the population identity
// ---------------------------------------------------------------------------

describe('the six feeling stages', () => {
  test('are exactly these six names, in this order', () => {
    expect(FEELING_STAGES).toEqual([
      'base',
      'luxury',
      'effects',
      'nationality',
      'martial_law',
      'final',
    ]);
    expect(MOOD_KEYS).toEqual(['happy', 'content', 'unhappy', 'angry']);
  });

  test('a reordered pair is refused — the rows are positional, not a set', () => {
    const rows = [
      ...feelingsWire(4).slice(1, 2),
      ...feelingsWire(4).slice(0, 1),
      ...feelingsWire(4).slice(2),
    ];
    expect(accepts(decodeInvestigation(withCitizens({ feelings: rows, specialists: [] })))).toBe(
      false,
    );
  });

  test('five rows is refused, and so is seven', () => {
    expect(
      accepts(
        decodeInvestigation(withCitizens({ feelings: feelingsWire(4).slice(1), specialists: [] })),
      ),
    ).toBe(false);
    expect(
      accepts(
        decodeInvestigation(
          withCitizens({
            feelings: [...feelingsWire(4), ...feelingsWire(4).slice(5)],
            specialists: [],
          }),
        ),
      ),
    ).toBe(false);
  });

  test('specialists count toward the population identity', () => {
    const balanced = withCitizens({
      feelings: feelingsWire(2),
      specialists: [{ id: 's_1', name: 'Scientist', count: 2 }],
    });
    expect(accepts(decodeInvestigation(balanced))).toBe(true);
  });

  test('a torn capture — one stage short a citizen — is refused', () => {
    const rows = feelingsWire(4).map((row, index) =>
      index === 2 ? { ...row, happy: 3 } : row,
    );
    expect(refusalOf(decodeInvestigation(withCitizens({ feelings: rows, specialists: [] })))).toContain(
      'feeling stage "effects" accounts for 3 citizens, but the city size is 4',
    );
  });

  test('the population helpers agree with the invariant', () => {
    const decoded = decodeInvestigation(investigationWire());
    expect(Either.isRight(decoded)).toBe(true);
    if (!Either.isRight(decoded)) return;
    const city = decoded.right.city;
    expect(specialistPopulation(city.citizens.specialists)).toBe(0);
    for (const feeling of city.citizens.feelings) {
      expect(feelingPopulation(feeling) + specialistPopulation(city.citizens.specialists)).toBe(
        city.size,
      );
    }
  });

  test('the final row is statically the final stage', () => {
    const decoded = decodeInvestigation(investigationWire());
    expect(Either.isRight(decoded)).toBe(true);
    if (Either.isRight(decoded)) {
      const [base] = decoded.right.city.citizens.feelings;
      expect(base.stage).toBe('base');
      expect(decoded.right.city.citizens.feelings[5].stage).toBe('final');
    }
  });
});

// ---------------------------------------------------------------------------
// Round trip
// ---------------------------------------------------------------------------

describe('round trip', () => {
  test('an unknown city field survives decode and re-encode, in place', () => {
    const wire = investigationWire({ city: cityWire({ pollution: 2 }) });
    const decoded = decodeInvestigation(wire);
    expect(Either.isRight(decoded)).toBe(true);
    if (!Either.isRight(decoded)) return;
    const encoded = encodeTolerant(CityInvestigationObservation)(decoded.right);
    expect(Either.isRight(encoded)).toBe(true);
    if (!Either.isRight(encoded)) return;
    expect(JSON.stringify(encoded.right)).toBe(JSON.stringify(wire));
  });
});
