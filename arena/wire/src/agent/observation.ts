/**
 * The city-investigation observation a command receipt may carry.
 *
 * Clean-room Effect Schema re-expression of
 * `_validate_investigation_observation` (`play/client.py`:1713-1848), read a
 * second time through `play-cli/src/schema/receipt.ts`, and cross-checked
 * against `CityInvestigationObservation` in
 * `play/docs/full-control-v2.openapi.json`.  Nothing here imports from
 * `play-cli`.
 *
 * This is the one place the v2 protocol hands an agent a subset of the *human
 * client's own* `CITY_INFO` packet, captured at exactly the receipt's
 * revision.  Everything strict about it is strict for one of two reasons: an
 * agent must never mistake a historical snapshot for current truth, and must
 * never be handed a city whose numbers do not add up.
 *
 * Three families of rule, all preserved:
 *
 *  - **Provenance.**  `type`, `source` and `freshness` are single-value
 *    literals, and the observation's own `state_revision` must equal the
 *    receipt's.  A snapshot that cannot name the revision it was taken at is
 *    not evidence.
 *  - **Identity.**  Improvement ids, improvement names, specialist ids and
 *    specialist names are each distinct within the city.  A duplicate means
 *    the capture merged two entities.
 *  - **Population.**  Every one of the six feeling rows must satisfy
 *    `happy + content + unhappy + angry + Σ specialist counts == size`.  The
 *    rows are six views of the *same* citizens, so a row that fails this is a
 *    torn read, not a rounding difference.
 *
 * ## Tolerance, and where it stops
 *
 * `_validate_investigation_observation` calls `_exact` eight times, refusing
 * any payload with a key it did not expect.  Per `./primitives.ts`, this port
 * *preserves* unknown keys instead — a server that adds a field to the city
 * block must not break an agent mid-game.  Every other kind of strictness the
 * Python has is kept exactly: missing fields, literals, patterns, bounds,
 * distinctness and the population identity all still refuse.
 *
 * @module
 */

import { Schema } from 'effect';
import { OpaqueId } from './ids.ts';
import { Revision, revisionsEqual } from './revision.ts';
import { decodeTolerant, type TolerantDecoder } from '../tolerant.ts';

// ---------------------------------------------------------------------------
// Numbers and text
// ---------------------------------------------------------------------------

/**
 * `isinstance(x, int) and not isinstance(x, bool) and x >= 0` — the shape the
 * Python demands of every count in the block (`play/client.py:1721`).
 *
 * A JavaScript boolean is not a `number`, so `Schema.Int` refuses `true`
 * without needing Python's explicit `bool` exclusion.  Integers stay `number`
 * here, matching `../ids.ts` and `../gateway/archive.ts`; bridge to
 * `../canon.ts` with `BigInt(value)`, which is total once this schema has run.
 */
const CityCount = Schema.Int.pipe(Schema.nonNegative());

/** A display name the Python requires to be a non-empty `str`. */
const CityName = Schema.String.pipe(Schema.minLength(1));

// ---------------------------------------------------------------------------
// Leaves
// ---------------------------------------------------------------------------

/**
 * What the city is building.  `kind` is a closed two-value enum in both the
 * Python (`play/client.py:1756`) and the OpenAPI document — it selects
 * behaviour, so it is a literal union rather than an open string.
 */
export const CityProduction = Schema.Struct({
  /** Opaque handle an order would name.  Echoed, never parsed. */
  id: OpaqueId,
  kind: Schema.Literal('unit', 'improvement'),
  name: CityName,
}).annotations({ identifier: 'CityProduction' });
/** A decoded production entry. */
export type CityProduction = typeof CityProduction.Type;

/**
 * Shield stock and per-turn surplus.
 *
 * `stock` cannot be negative; `surplus` very much can, and the Python checks
 * only integrality for it (`play/client.py:1770`).  A port that clamped or
 * refused a negative surplus would hide a city eating its own stock.
 */
export const CityShields = Schema.Struct({
  stock: CityCount,
  surplus: Schema.Int,
}).annotations({ identifier: 'CityShields' });
/** A decoded shields entry. */
export type CityShields = typeof CityShields.Type;

/** One completed improvement in the investigated city. */
export const CityImprovement = Schema.Struct({
  id: OpaqueId,
  name: CityName,
}).annotations({ identifier: 'CityImprovement' });
/** A decoded improvement entry. */
export type CityImprovement = typeof CityImprovement.Type;

/** One specialist class, and how many citizens are working it. */
export const CitySpecialist = Schema.Struct({
  id: OpaqueId,
  name: CityName,
  count: CityCount,
}).annotations({ identifier: 'CitySpecialist' });
/** A decoded specialist entry. */
export type CitySpecialist = typeof CitySpecialist.Type;

// ---------------------------------------------------------------------------
// Distinctness — play/client.py:1777-1783 and 1827-1833
// ---------------------------------------------------------------------------

const isDistinct = (values: ReadonlyArray<string>): boolean =>
  new Set(values).size === values.length;

const namedRowsAreDistinct = (
  rows: ReadonlyArray<{ readonly id: string; readonly name: string }>,
  label: string,
): string | undefined => {
  if (!isDistinct(rows.map((row) => row.id))) return `${label} ids are not distinct`;
  if (!isDistinct(rows.map((row) => row.name))) return `${label} names are not distinct`;
  return undefined;
};

/** Longest improvement list the Python will copy (`play/client.py:1764`). */
export const IMPROVEMENTS_MAX = 1024;

/** Longest specialist list the Python will copy (`play/client.py:1808`). */
export const SPECIALISTS_MAX = 256;

/** The city's improvements: bounded, and distinct by both id and name. */
export const CityImprovements = Schema.Array(CityImprovement)
  .pipe(
    Schema.maxItems(IMPROVEMENTS_MAX),
    Schema.filter((rows) => namedRowsAreDistinct(rows, 'improvement')),
  )
  .annotations({ identifier: 'CityImprovements' });

/** The city's specialists: bounded, and distinct by both id and name. */
export const CitySpecialists = Schema.Array(CitySpecialist)
  .pipe(
    Schema.maxItems(SPECIALISTS_MAX),
    Schema.filter((rows) => namedRowsAreDistinct(rows, 'specialist')),
  )
  .annotations({ identifier: 'CitySpecialists' });

// ---------------------------------------------------------------------------
// Feelings — play/client.py:1788-1806
// ---------------------------------------------------------------------------

/**
 * The six happiness stages, **in wire order** (`play/client.py:1788-1790`).
 *
 * Order is load-bearing: the Python compares `feeling["stage"] !=
 * stages[index]` positionally, so this is a fixed sequence and not a set.
 * Each stage is the citizen breakdown after one more effect has been applied
 * — `base` is before anything, `final` is what the city actually experiences.
 */
export const FEELING_STAGES = [
  'base',
  'luxury',
  'effects',
  'nationality',
  'martial_law',
  'final',
] as const;

/** One of the six stage names. */
export type FeelingStage = (typeof FEELING_STAGES)[number];

/** The four mood buckets a feeling row splits citizens into (`play/client.py:1786`). */
export const MOOD_KEYS = ['happy', 'content', 'unhappy', 'angry'] as const;

const feelingAt = <S extends FeelingStage>(stage: S) =>
  Schema.Struct({
    stage: Schema.Literal(stage),
    happy: CityCount,
    content: CityCount,
    unhappy: CityCount,
    angry: CityCount,
  }).annotations({ identifier: `CityFeeling(${stage})` });

/**
 * Exactly six feeling rows, each pinned to its own stage by position.
 *
 * A tuple, not an array plus a filter: the arity check (`len(feelings) !=
 * len(stages)`) and the positional stage check are one rule, and a tuple
 * states it once — in the static type as well as at runtime, so a caller
 * reading `feelings[5]` knows it is the `final` row without asserting.
 */
export const CityFeelings = Schema.Tuple(
  feelingAt('base'),
  feelingAt('luxury'),
  feelingAt('effects'),
  feelingAt('nationality'),
  feelingAt('martial_law'),
  feelingAt('final'),
).annotations({
  identifier: 'CityFeelings',
  description: 'The six positional happiness stages of an investigated city',
});
/** The six feeling rows, in stage order. */
export type CityFeelings = typeof CityFeelings.Type;
/** One feeling row. */
export type CityFeeling = CityFeelings[number];

/** Citizens counted in the four moods of one stage. */
export const feelingPopulation = (feeling: CityFeeling): number =>
  feeling.happy + feeling.content + feeling.unhappy + feeling.angry;

/** Citizens working as specialists, summed across classes. */
export const specialistPopulation = (specialists: ReadonlyArray<CitySpecialist>): number =>
  specialists.reduce((total, row) => total + row.count, 0);

// ---------------------------------------------------------------------------
// The city — play/client.py:1731-1848
// ---------------------------------------------------------------------------

const cityFields = Schema.Struct({
  /**
   * The city's handle.
   *
   * `_opaque` in the Python (`play/client.py:1842`), while the OpenAPI calls
   * it an `ActorId` (`^(?:player|city|unit)_[0-9a-f]{32}$`) — strictly
   * narrower.  The looser rule wins: tightening a check the shipped client
   * does not make is the one change that could refuse a payload a live seat
   * accepts today.  `../ids.ts` exports `CityId` for a caller that wants the
   * narrow form.
   */
  id: OpaqueId,
  name: CityName,
  /** Citizens in the city.  At least one — a size-zero city does not exist. */
  size: Schema.Int.pipe(Schema.positive()),
  production: CityProduction,
  shields: CityShields,
  improvements: CityImprovements,
  citizens: Schema.Struct({
    feelings: CityFeelings,
    specialists: CitySpecialists,
  }).annotations({ identifier: 'CityCitizens' }),
});

/**
 * The population identity (`play/client.py:1837-1843`): each stage must
 * account for every citizen exactly once.
 */
const populationBalances = (city: typeof cityFields.Type): string | undefined => {
  const specialists = specialistPopulation(city.citizens.specialists);
  const torn = city.citizens.feelings.find(
    (feeling) => feelingPopulation(feeling) + specialists !== city.size,
  );
  return torn === undefined
    ? undefined
    : `feeling stage "${torn.stage}" accounts for ${String(
        feelingPopulation(torn) + specialists,
      )} citizens, but the city size is ${String(city.size)}`;
};

/** The investigated city, with its population identity enforced. */
export const InvestigatedCity = cityFields
  .pipe(Schema.filter(populationBalances))
  .annotations({ identifier: 'InvestigatedCity' });
/** A decoded investigated city. */
export type InvestigatedCity = typeof InvestigatedCity.Type;

// ---------------------------------------------------------------------------
// The observation
// ---------------------------------------------------------------------------

/**
 * The observation envelope.
 *
 * The three provenance literals are what make the snapshot legible: it is a
 * `city_investigation`, it came from the `human_client_city_info` packet, and
 * its freshness claim is `captured_at_receipt_revision` — historical from that
 * revision onward, making no claim about the present.
 *
 * This schema does **not** compare `state_revision` against an enclosing
 * receipt, because a bare observation does not know what enclosed it.  Reach
 * for {@link investigationAt}, or decode the whole receipt (`./receipt.ts`),
 * which checks it as an intra-payload invariant.
 */
export const CityInvestigationObservation = Schema.Struct({
  id: OpaqueId,
  type: Schema.Literal('city_investigation'),
  source: Schema.Literal('human_client_city_info'),
  freshness: Schema.Literal('captured_at_receipt_revision'),
  state_revision: Revision,
  city: InvestigatedCity,
}).annotations({
  identifier: 'CityInvestigationObservation',
  description: 'A CITY_INFO subset captured at exactly a receipt revision',
});
/** A decoded city-investigation observation. */
export type CityInvestigationObservation = typeof CityInvestigationObservation.Type;
/** The wire form of a {@link CityInvestigationObservation}. */
export type CityInvestigationObservationEncoded = typeof CityInvestigationObservation.Encoded;

/** Decode an unknown value as a {@link CityInvestigationObservation}. */
export const decodeInvestigation: TolerantDecoder<CityInvestigationObservation> = decodeTolerant(
  CityInvestigationObservation,
  'CityInvestigationObservation',
);

/**
 * The observation, bound to the revision it claims to have been captured at —
 * the `_validate_revision(raw["state_revision"]) != revision` half of the
 * Python's provenance check (`play/client.py:1726`).
 *
 * All three revision fields must match, `state_token` included: the same
 * counters with a new token is a *different* state (a republished boundary),
 * and a snapshot that drifted onto one is not evidence about the state the
 * caller asked about.
 */
export const investigationAt = (
  revision: Revision,
): Schema.Schema<CityInvestigationObservation, CityInvestigationObservationEncoded> =>
  CityInvestigationObservation.pipe(
    Schema.filter((observation) =>
      revisionsEqual(observation.state_revision, revision)
        ? undefined
        : 'the observation was captured at a different state revision',
    ),
  ).annotations({ identifier: 'CityInvestigationObservation' });

/** Decode an observation and require it to name `revision`. */
export const decodeInvestigationAt = (
  revision: Revision,
): TolerantDecoder<CityInvestigationObservation> =>
  decodeTolerant(investigationAt(revision), 'CityInvestigationObservation');
