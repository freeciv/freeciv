/**
 * Decode parity for the four documents a run directory keeps on disk.
 *
 * The Python that writes and reads those files is the oracle, and the archived
 * corpus under `test/fixtures/runs/` is its testimony: 12 manifest shapes, 8
 * report shapes and both technology-catalog shapes, captured verbatim from
 * `.agent-eval/runs/`.  **If a fixture does not decode, the schema is wrong.**
 *
 * Four properties are checked over the whole corpus rather than per file,
 * because each is a way a port diverges without anyone noticing:
 *
 * 1. every fixture decodes;
 * 2. re-encoding reproduces the parsed document exactly — same keys, same
 *    order, including fields the schema never named;
 * 3. the schema actually names every field the corpus carries, so nothing is
 *    passing only because excess properties are tolerated;
 * 4. every number decodes on the right side of the int/float line, checked
 *    against how the *raw bytes* spell it.  That is the one the canonical
 *    writer depends on: Python prints a float `600.0` and an int `600`, and
 *    `JSON.parse` erases the difference.
 *
 * The rest are spot assertions on the fields the gateway dossier flags as
 * traps, each citing the Python line that makes it true.
 */
import { readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';
import { describe, expect, test } from 'bun:test';
import { Either, Schema } from 'effect';
import {
  AI_DIFFICULTY_LEVELS,
  EmptyManifest,
  MANIFEST_TIMING_MODES,
  Manifest,
  PLACE_CONTROLLERS,
  ReplayCatalog,
  Report,
  TechnologyEntry,
  VICTORY_LABELS,
  decodeManifest,
  decodeReplayCatalog,
  decodeReport,
  decodeVictoryRecord,
  encodeManifest,
  encodeReplayCatalog,
  encodeReport,
  encodeVictoryRecord,
  victoryLabel,
} from 'src/gateway/manifest';
import { CONTROL_PROTOCOLS, isControlProtocol } from 'src/control-protocol';
import { WireInt } from 'src/numeric';
import { formatIssuePath, isTolerant } from 'src/tolerant';

// ---------------------------------------------------------------------------
// Corpus access
// ---------------------------------------------------------------------------

const FIXTURES_DIR = join(import.meta.dir, '..', 'fixtures');

const readFixture = (path: string): string => readFileSync(join(FIXTURES_DIR, path), 'utf8');

const parseFixture = (path: string): unknown => JSON.parse(readFixture(path));

const fixtureNames = (dir: string): ReadonlyArray<string> =>
  readdirSync(join(FIXTURES_DIR, dir)).toSorted();

const MANIFEST_DIR = 'runs/manifest';
const REPORT_DIR = 'runs/report';
const CATALOG_DIR = 'runs/replay-catalog';

const manifestFile = (name: string): string => `${MANIFEST_DIR}/${name}`;
const reportFile = (name: string): string => `${REPORT_DIR}/${name}`;
const catalogFile = (name: string): string => `${CATALOG_DIR}/${name}`;

/** Anything with a rendered explanation — a `WireDecodeError` or a `ParseError`. */
interface Explained {
  readonly message: string;
}

type Decoder<A> = (input: unknown) => Either.Either<A, Explained>;

/** Decode a value or fail the test with the schema's own explanation. */
const decodeOrThrow = <A>(decode: Decoder<A>, input: unknown, label: string): A => {
  const result = decode(input);
  if (Either.isLeft(result)) {
    throw new Error(`${label} did not decode:\n${result.left.message}`);
  }
  return result.right;
};

const decoded = <A>(decode: Decoder<A>, path: string): A =>
  decodeOrThrow(decode, parseFixture(path), path);

const manifest = (name: string): Manifest => decoded(decodeManifest, manifestFile(name));
const report = (name: string): Report => decoded(decodeReport, reportFile(name));

/** Every archived manifest and report, decoded once, keyed by fixture name. */
const allManifests: ReadonlyArray<readonly [string, Manifest]> = fixtureNames(MANIFEST_DIR).map(
  (name) => [name, manifest(name)] as const,
);

const allReports: ReadonlyArray<readonly [string, Report]> = fixtureNames(REPORT_DIR).map(
  (name) => [name, report(name)] as const,
);

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === 'object' && value !== null && !Array.isArray(value);

const isManifest: (input: unknown) => input is Manifest = isTolerant(Manifest);

// ---------------------------------------------------------------------------
// Corpus-wide properties
// ---------------------------------------------------------------------------

/**
 * Number literals in document order, skipping anything inside a string.
 *
 * A JSON number is the only token that may *start* with a digit or `-` outside
 * a string, so `true`/`false`/`null` cannot be mistaken for one even though
 * `true` contains an `e`.
 */
interface ScanState {
  readonly inString: boolean;
  readonly escaped: boolean;
  readonly pending: string;
  readonly literals: ReadonlyArray<string>;
}

const NUMBER_START = /[-0-9]/;
const NUMBER_BODY = /[-+.0-9eE]/;

const scanStep = (state: ScanState, char: string): ScanState => {
  if (state.inString) {
    if (state.escaped) return { ...state, escaped: false };
    if (char === '\\') return { ...state, escaped: true };
    return char === '"' ? { ...state, inString: false } : state;
  }
  if (state.pending !== '') {
    return NUMBER_BODY.test(char)
      ? { ...state, pending: state.pending + char }
      : {
          ...state,
          pending: '',
          inString: char === '"',
          literals: [...state.literals, state.pending],
        };
  }
  if (char === '"') return { ...state, inString: true };
  return NUMBER_START.test(char) ? { ...state, pending: char } : state;
};

const numberLiterals = (text: string): ReadonlyArray<string> => {
  const final = [...text].reduce<ScanState>(scanStep, {
    inString: false,
    escaped: false,
    pending: '',
    literals: [],
  });
  return final.pending === '' ? final.literals : [...final.literals, final.pending];
};

/** How Python spelled a literal: with a fraction or exponent, or without. */
const spelling = (literal: string): 'float' | 'int' => (/[.eE]/.test(literal) ? 'float' : 'int');

/**
 * Numeric leaves of a decoded value in document order.  `bigint` is a Python
 * `int`, `number` is a Python `float` — see the module doc of
 * `src/gateway/manifest.ts`.
 */
const numericLeaves = (value: unknown): ReadonlyArray<'float' | 'int'> => {
  if (typeof value === 'bigint') return ['int'];
  if (typeof value === 'number') return ['float'];
  if (Array.isArray(value)) return value.flatMap(numericLeaves);
  return isRecord(value) ? Object.values(value).flatMap(numericLeaves) : [];
};

/**
 * Fixtures whose bytes spell an integral value as a float, which `JSON.parse`
 * erases before any schema can see it.  The value is how many such literals
 * the file has, so the exemption cannot quietly widen.
 *
 * `tech-tree-without-depth.json` is the whole list: bridge.lua writes ids and
 * costs with Lua's `tostring`, so all 87 ids arrive as `1.0`-style literals,
 * as do the 9 technologies whose research cost happens to be a round number
 * (`640.0`, `3430.0`, ...).  Ids are normalized to ints by the reader anyway
 * (`supervisor.py:435`); costs are not, so a port that re-publishes *this*
 * file rather than a reconstructed catalog has to take the spelling from the
 * writer's identity, not from the value.
 */
const KNOWN_INTEGRAL_FLOATS: ReadonlyMap<string, number> = new Map([
  ['tech-tree-without-depth.json', 87 + 9],
]);

interface Family<A> {
  readonly label: string;
  readonly dir: string;
  readonly decode: Decoder<A>;
  readonly encode: (value: A) => Either.Either<unknown, Explained>;
  /**
   * The same schema with `onExcessProperty: "error"`.  The package never
   * decodes this way — it is an audit, and the inverse of what production
   * does: an unmodelled field would be preserved silently, and would also make
   * the number check below vacuous for that field.
   */
  readonly strict: Decoder<A>;
}

const strictly = <A, I>(schema: Schema.Schema<A, I>): Decoder<A> =>
  Schema.decodeUnknownEither(schema, {
    onExcessProperty: 'error',
    errors: 'all',
    propertyOrder: 'original',
  });

const describeFamily = <A>(family: Family<A>): void => {
  describe(`corpus parity: ${family.label}`, () => {
    const names = fixtureNames(family.dir);

    test('the corpus is not empty', () => {
      expect(names.length).toBeGreaterThan(0);
    });

    test('every archived fixture decodes', () => {
      const failures = names.flatMap((name) => {
        const result = family.decode(parseFixture(`${family.dir}/${name}`));
        return Either.isLeft(result) ? [`${name}: ${result.left.message}`] : [];
      });
      expect(failures).toEqual([]);
    });

    test('re-encoding reproduces the document, key order and all', () => {
      const drifted = names.flatMap((name) => {
        const path = `${family.dir}/${name}`;
        const parsed = parseFixture(path);
        const encodedValue = family.encode(decoded(family.decode, path));
        if (Either.isLeft(encodedValue)) return [`${name}: ${encodedValue.left.message}`];
        return JSON.stringify(encodedValue.right) === JSON.stringify(parsed) ? [] : [name];
      });
      expect(drifted).toEqual([]);
    });

    test('the schema names every field the corpus carries', () => {
      const unmodelled = names.flatMap((name) => {
        const result = family.strict(parseFixture(`${family.dir}/${name}`));
        return Either.isLeft(result) ? [`${name}: ${result.left.message}`] : [];
      });
      expect(unmodelled).toEqual([]);
    });

    test('every number decodes on the side of the int/float line its bytes say', () => {
      const rows = names.map((name) => {
        const path = `${family.dir}/${name}`;
        const literals = numberLiterals(readFixture(path));
        const leaves = numericLeaves(decoded(family.decode, path));
        const mismatched = literals.flatMap((literal, index) =>
          spelling(literal) === leaves[index]
            ? []
            : [`${literal} decoded as ${String(leaves[index])}`],
        );
        return { name, counted: literals.length === leaves.length, mismatched };
      });

      // Every value the schema classified has to be accounted for, or the
      // comparison below is walking two different documents.
      expect(rows.filter((row) => !row.counted).map((row) => row.name)).toEqual([]);

      // A float-spelled integer is erased by JSON.parse and is exempted by
      // name; the reverse — an int-spelled literal decoding as a float — is
      // always a schema bug, and is never exempt.
      const backwards = rows.flatMap((row) =>
        row.mismatched
          .filter((entry) => entry.endsWith('decoded as float'))
          .map((entry) => `${row.name}: ${entry}`),
      );
      expect(backwards).toEqual([]);

      const unexpected = rows.flatMap((row) =>
        row.mismatched.length === (KNOWN_INTEGRAL_FLOATS.get(row.name) ?? 0)
          ? []
          : [`${row.name}: ${String(row.mismatched.length)} float-spelled ints`],
      );
      expect(unexpected).toEqual([]);
    });
  });
};

describeFamily<Manifest>({
  label: 'manifest.json',
  dir: MANIFEST_DIR,
  decode: decodeManifest,
  encode: encodeManifest,
  strict: strictly(Manifest),
});

describeFamily<Report>({
  label: 'report.json',
  dir: REPORT_DIR,
  decode: decodeReport,
  encode: encodeReport,
  strict: strictly(Report),
});

describeFamily<ReplayCatalog>({
  label: 'replay-catalog.json',
  dir: CATALOG_DIR,
  decode: decodeReplayCatalog,
  encode: encodeReplayCatalog,
  strict: strictly(ReplayCatalog),
});

describe('additive server fields', () => {
  test('a field this build never heard of survives decode and re-encode', () => {
    // The supervisor adds keys without bumping schema_version; a middleman
    // that drops them is how a viewer loses a field it was never told about.
    const source = parseFixture(manifestFile('running-v2-multiplayer.json'));
    const augmented = { ...(isRecord(source) ? source : {}), future_field: { nested: [1, 'two'] } };
    const value = decodeOrThrow(decodeManifest, augmented, 'augmented manifest');
    const round = encodeManifest(value);
    expect(Either.isRight(round)).toBe(true);
    if (Either.isLeft(round)) return;
    expect(JSON.stringify(round.right)).toBe(JSON.stringify(augmented));
  });
});

// ---------------------------------------------------------------------------
// manifest.json
// ---------------------------------------------------------------------------

describe('manifest.json', () => {
  test('schema_version is 1 everywhere, and is an int', () => {
    const versions = new Set(allManifests.map(([, value]) => value.schema_version));
    expect(versions).toEqual(new Set([1n]));
  });
  test('state and status are the same value written twice', () => {
    // supervisor.py:1084 writes both from one expression, and nothing keeps
    // them in sync afterwards.
    const split = allManifests.filter(([, value]) => value.state !== value.status);
    expect(split.map(([name]) => name)).toEqual([]);
    expect(new Set(allManifests.map(([, value]) => value.state))).toEqual(
      new Set(['completed', 'failed', 'cancelled', 'invalid', 'running']),
    );
  });
  test('state is an open vocabulary: an unknown one decodes rather than failing', () => {
    // `_public_text(..., "unknown", 32)` passes any string through, so the
    // gateway serves "quantum" verbatim.  The corpus files this payload under
    // invalid/, but the oracle accepts it and so must the schema; the
    // UnrecognizedRunState brand is what forces a consumer to notice.
    const value = decoded(decodeManifest, 'invalid/manifest-status-unknown.json');
    // Compared as plain strings: the brand is the point of the schema, and
    // there is deliberately no way to write a branded literal by hand.
    expect(String(value.status)).toBe('quantum');
    expect(String(value.state)).toBe('cancelled');
  });
  test('benchmark_valid is tri-state', () => {
    const observed = new Set(allManifests.map(([, value]) => value.benchmark_valid));
    expect(observed).toEqual(new Set([true, false, null]));
  });
  test('control_protocol is absent on strategic-v1 and full-control-v2 on v2', () => {
    // Corrects a claim in the corpus notes: the key is optional at the top
    // level *and* inside config.  Six of the twelve archived shapes predate it
    // in both places, and none of them names strategic-v1 explicitly.
    const missingTop = allManifests.filter(([, value]) => value.control_protocol === undefined);
    const missingConfig = allManifests.filter(
      ([, value]) => value.config.control_protocol === undefined,
    );
    expect(missingTop.map(([name]) => name)).toEqual(missingConfig.map(([name]) => name));
    expect(missingTop.length).toBe(6);

    const present = allManifests.flatMap(([, value]) =>
      value.control_protocol === undefined ? [] : [value.control_protocol],
    );
    expect(new Set(present)).toEqual(new Set(['full-control-v2']));
    expect(present.every((protocol) => isControlProtocol(protocol))).toBe(true);
    expect(CONTROL_PROTOCOLS).toContain('strategic-v1');
    expect(isControlProtocol('strategic-v2')).toBe(false);
  });
  test('the timing pair is a Python float, and infinite spells it null', () => {
    // _finite_number returns float(value) (supervisor.py:627) and the presets
    // are 180.0/60.0/600.0, so the bytes on disk never say a bare 600.  The
    // gateway's _public_timing hardcodes 180.0/60.0 and therefore drops v2
    // timing from the wire entirely — read it here instead.
    const v2 = manifest('running-v2-multiplayer.json');
    expect(v2.config.action_timeout_s).toBe(600);
    expect(typeof v2.config.action_timeout_s).toBe('number');
    expect(readFixture(manifestFile('running-v2-multiplayer.json'))).toContain(
      '"action_timeout_s": 600.0',
    );
    expect(typeof v2.config.lobby_timeout_s).toBe('number');

    const infinite = manifest('completed-strategic-v1-multiplayer.json');
    expect(infinite.config.timing_mode).toBe('infinite');
    expect(infinite.config.action_timeout_s).toBe(null);

    const modes = allManifests.flatMap(([, value]) =>
      value.config.timing_mode === undefined ? [] : [value.config.timing_mode],
    );
    expect(new Set(modes)).toEqual(new Set(['default', 'infinite']));
    // Never observed, but reachable: a caller-supplied timeout matching no
    // preset is named "custom" (supervisor.py:10615).
    expect(MANIFEST_TIMING_MODES).toContain('custom');
  });
  test('timestamps are float epoch seconds, nullable except created_at', () => {
    const running = manifest('running-v2-multiplayer.json');
    expect(typeof running.created_at).toBe('number');
    expect(running.created_at).toBeGreaterThan(1_700_000_000);
    expect(running.finished_at).toBe(null);
    expect(running.returncode).toBe(null);
    expect(allManifests.every(([, value]) => typeof value.created_at === 'number')).toBe(true);

    const neverStarted = manifest('cancelled-v2-never-started-recovery.json');
    expect(neverStarted.started_at).toBe(null);
    expect(neverStarted.checkpoints).toBe(0n);
    expect(neverStarted.start_count).toBe(0n);
  });
  test('turn and count fields decode as ints', () => {
    const completed = manifest('completed-strategic-v1-multiplayer.json');
    expect(completed.current_turn).toBe(752n);
    expect(completed.checkpoints).toBe(753n);
    expect(completed.frames).toBe(753n);
    expect(completed.returncode).toBe(0n);
    expect(completed.config.turns).toBe(5000n);
    expect(completed.config.places).toBe(2n);
    expect(completed.config.seeds).toEqual([2100029438n]);
    expect(manifest('running-v2-multiplayer.json').current_turn).toBe(null);
  });
  test('invalid_reasons mixes slugs with free text and is never a literal union', () => {
    expect(manifest('failed-v2-boundary-wedged-recovery.json').invalid_reasons).toEqual([
      'v2_boundary_wedged',
    ]);
    expect(manifest('failed-v2-sidecar-exited.json').invalid_reasons).toEqual(['sidecar_exited']);

    const freetext = manifest('invalid-strategic-v1-freetext-reasons.json');
    expect(freetext.invalid_reasons.some((reason) => reason.includes(' '))).toBe(true);
    expect(manifest('cancelled-strategic-v1-many-freetext-reasons.json').invalid_reasons.length).toBe(
      10,
    );
    expect(manifest('failed-strategic-v1-three-places.json').invalid_reasons).toEqual([]);
  });
  test('recovery is absent, null, or an object, and the three stay distinct', () => {
    const absent = allManifests.filter(([, value]) => !('recovery' in value));
    const nulled = allManifests.filter(([, value]) => value.recovery === null);
    const present = allManifests.flatMap(([, value]) =>
      value.recovery === null || value.recovery === undefined ? [] : [value.recovery],
    );
    expect(absent.length).toBe(8);
    expect(nulled.length).toBe(3);
    expect(present.length).toBe(1);

    const [wedged] = present;
    expect(wedged?.attempts).toBe(4n);
    expect(wedged?.rewound_applied_actions).toBe(false);
    expect(wedged?.by_kind).toEqual({ autosave_rollback: 2n, sidecar_reattach: 2n });
    expect(wedged?.by_outcome).toEqual({ abandoned: 2n, failed: 2n });
    expect(wedged?.recovered_to_turns).toEqual([]);

    // A null must not re-encode as a missing key, nor a missing key as null.
    const encodedNull = encodeManifest(manifest('running-v2-multiplayer.json'));
    expect(Either.isRight(encodedNull)).toBe(true);
    if (Either.isRight(encodedNull)) {
      expect(JSON.stringify(encodedNull.right)).toContain('"recovery":null');
    }
    const encodedAbsent = encodeManifest(manifest('completed-strategic-v1-multiplayer.json'));
    expect(Either.isRight(encodedAbsent)).toBe(true);
    if (Either.isRight(encodedAbsent)) {
      expect(JSON.stringify(encodedAbsent.right)).not.toContain('"recovery"');
    }
  });
  test('a resolved place omits fields rather than nulling them', () => {
    // supervisor.py:992 adds the controller keys only for a joined or a native
    // place, so the oldest rows carry five keys and nothing else.
    const [first] = manifest('cancelled-strategic-v1-never-started.json').resolved_places;
    expect(Object.keys(first ?? {}).toSorted()).toEqual([
      'controller',
      'joined',
      'place',
      'player_name',
      'seat_id',
    ]);
    expect(first?.controller).toBe('agent');
    expect(PLACE_CONTROLLERS).toContain('native_classic_ai');

    const native = manifest('failed-v2-sidecar-exited.json').resolved_places[1];
    expect(native?.controller).toBe('native_classic_ai');
    expect(native?.controller_label).toBe('Freeciv Classic AI');
    expect(native?.model).toBe('classic');
    expect(native?.joined).toBe(false);
    expect('controller_fingerprint' in (native ?? {})).toBe(false);

    const joined = manifest('running-v2-multiplayer.json').resolved_places[0];
    expect(joined?.place).toBe(1n);
    expect(joined?.controller_type).toBe('external');
    // Nulled where the native row omits it: the same field, both ways, in one
    // corpus — which is why every one of these is optional *and* nullable.
    expect(joined?.model).toBe(null);
    expect(joined?.controller_metadata).toEqual({});
  });
  test('seats carry the fingerprint identity, and ai_difficulty only on newer runs', () => {
    const [seat] = manifest('running-v2-multiplayer.json').config.seats;
    expect(seat?.type).toBe('external');
    expect(seat?.instructions).toBe('Maximize final Freeciv civilization score.');
    expect(seat?.base_url).toBe(null);
    expect(seat?.model).toBe(null);
    expect(seat?.options).toEqual({});
    expect(seat?.ai_difficulty).toBe(null);
    expect(seat?.controller_fingerprint).toMatch(/^[0-9a-f]{64}$/);

    const legacy = manifest('completed-strategic-v1-multiplayer.json');
    expect('ai_difficulty' in (legacy.config.seats[0] ?? {})).toBe(false);
    expect(legacy.config.difficulty).toBe(undefined);
    expect(manifest('running-v2-multiplayer.json').config.difficulty).toBe('hard');
    expect(AI_DIFFICULTY_LEVELS).toContain('hard');
  });
  test('game_id is validated, and a manifest without one is not a game', () => {
    // replay_gateway.py:568 — a manifest whose game_id does not match the
    // requested id is a 404, not a malformed archive.
    expect(String(manifest('running-v2-multiplayer.json').game_id)).toBe(
      'game_QAoITB7qSmKNSwsXX6LaZG8H',
    );
    expect(Either.isLeft(decodeManifest(parseFixture('invalid/manifest-missing-game-id.json')))).toBe(
      true,
    );
  });
});

// ---------------------------------------------------------------------------
// report.json
// ---------------------------------------------------------------------------

describe('report.json', () => {
  test('the embedded manifest decodes as a manifest', () => {
    const embedded = report('completed-two-seats-full-score.json').manifest;
    expect(isManifest(embedded)).toBe(true);
    if (!isManifest(embedded)) return;
    expect(embedded.state).toBe('completed');
    expect(embedded.benchmark_valid).toBe(true);
    expect(embedded.config.seats.length).toBe(2);
  });
  test('an empty embedded manifest is legal, but a half-built one is not', () => {
    // scoring.py:169 — summarize_episode substitutes {} when manifest.json is
    // missing.  The filter on EmptyManifest is what stops that branch from
    // swallowing a manifest that merely failed to decode.
    const source = parseFixture(reportFile('empty-score-no-recovery.json'));
    const body = isRecord(source) ? source : {};
    expect(Either.isRight(decodeReport({ ...body, manifest: {} }))).toBe(true);
    expect(Either.isRight(Schema.decodeUnknownEither(EmptyManifest)({}))).toBe(true);
    expect(Either.isLeft(decodeReport({ ...body, manifest: { game_id: 'nope' } }))).toBe(true);
  });
  test('alive is absent on legacy runs — absent, not null', () => {
    // parse_scorelog gained alive/added_turn/removed_turn/last_score_turn
    // after these runs were scored (scoring.py:90).  The corpus notes call
    // this fixture "alive=null"; the bytes say the keys are missing.
    const [player] = report('alive-null-legacy-players.json').score.players;
    expect(player?.alive).toBe(undefined);
    expect('alive' in (player ?? {})).toBe(false);
    expect(readFixture(reportFile('alive-null-legacy-players.json'))).not.toContain('"alive"');
    expect(player?.rank).toBe(1n);
    expect(player?.controller_fingerprint).toMatch(/^[0-9a-f]{64}$/);
  });
  test('a removed player is alive:false with a removed_turn', () => {
    const dead = report('dead-player-alive-false.json');
    const eliminated = dead.score.players.find((player) => player.alive === false);
    expect(eliminated?.removed_turn).toBe(193n);
    expect(eliminated?.last_score_turn).toBe(193n);
    expect(dead.score.players.find((player) => player.alive === true)?.removed_turn).toBe(null);
  });
  test('rank is dense and not unique', () => {
    // scoring.py:103 — equal scores share a rank, so two rank-1 players is a
    // tie, not corruption.
    const tied = report('tied-ranks-empty-seat-stats.json');
    expect(tied.score.players.map((player) => player.rank)).toEqual([1n, 1n]);
    expect(new Set(tied.score.players.map((player) => player.score))).toEqual(new Set([2n]));
    expect(report('three-players-ranked.json').score.players.map((player) => player.rank)).toEqual([
      1n,
      2n,
      3n,
    ]);
  });
  test('metrics are integer counters, and final_turn is nullable', () => {
    const full = report('completed-two-seats-full-score.json');
    const [winner] = full.score.players;
    expect(full.score.final_turn).toBe(753n);
    expect(winner?.score).toBe(1856n);
    expect(Object.keys(winner?.metrics ?? {}).length).toBe(31);
    expect(Object.values(winner?.metrics ?? {}).every((value) => typeof value === 'bigint')).toBe(
      true,
    );
    expect(winner?.metrics['score']).toBe(1856n);

    const empty = report('empty-score-with-recovery.json');
    expect(empty.score.final_turn).toBe(null);
    expect(empty.score.players).toEqual([]);
  });
  test('latency_ms survives exactly when a seat made no decisions', () => {
    // scoring.py:262 — `round(current.pop("latency_ms") / count, 3) if count
    // else 0`.  Python evaluates the condition first, so with no decisions the
    // pop never runs: the accumulator stays and mean_latency_ms is the integer
    // 0.  That pairing is also the recipe for spelling mean_latency_ms back
    // out byte-exactly, since an integral float is otherwise indistinguishable
    // from an int once JSON.parse has been over it.
    const rows = allReports.flatMap(([name, value]) =>
      Object.entries(value.seat_stats).map(([seat, stats]) => ({ name, seat, stats })),
    );
    expect(rows.length).toBeGreaterThan(0);

    const wrong = rows.filter(
      ({ stats }) => (stats.latency_ms !== undefined) !== (stats.decisions === 0n),
    );
    expect(wrong.map(({ name, seat }) => `${name}#${seat}`)).toEqual([]);

    const idle = rows.filter(({ stats }) => stats.decisions === 0n);
    const busy = rows.filter(({ stats }) => stats.decisions > 0n);
    expect(idle.length).toBeGreaterThan(0);
    expect(busy.length).toBeGreaterThan(0);
    expect(idle.every(({ stats }) => stats.mean_latency_ms === 0n)).toBe(true);
    expect(idle.every(({ stats }) => stats.latency_ms === 0)).toBe(true);
    expect(busy.every(({ stats }) => typeof stats.mean_latency_ms === 'number')).toBe(true);
  });
  test('a native seat with no decisions still reports turns', () => {
    // scoring.py:242 backfills native seats with final_turn - 1.
    const ranked = report('three-players-ranked.json');
    expect(ranked.seat_stats['place-2']?.decisions).toBe(0n);
    expect(ranked.seat_stats['place-2']?.turns).toBe(331n);
    expect(ranked.score.final_turn).toBe(332n);
  });
  test('seat_stats ranges over empty, partial and complete', () => {
    const sizes = allReports.map(([, value]) => Object.keys(value.seat_stats).length);
    expect(sizes.some((size) => size === 0)).toBe(true);

    const partial = report('partial-seat-stats-with-recovery.json');
    expect(partial.score.players.length).toBe(2);
    expect(Object.keys(partial.seat_stats)).toEqual(['place-2']);
    expect(Object.keys(report('completed-two-seats-full-score.json').seat_stats).length).toBe(2);
  });
  test('report-level recovery is an object when present, never null', () => {
    const withRecovery = allReports.flatMap(([, value]) =>
      value.recovery === undefined ? [] : [value.recovery],
    );
    const without = allReports.filter(([, value]) => !('recovery' in value));
    expect(withRecovery.length).toBe(4);
    expect(without.length).toBe(4);
    expect(withRecovery.every((recovery) => !recovery.rewound_applied_actions)).toBe(true);
    expect(withRecovery.filter((recovery) => recovery.attempts === 0n).length).toBe(3);
  });
  test('episode is an absolute local path that must never be published', () => {
    // supervisor.py:10069 pops it before serving /result, and the archive
    // /result builds a curated payload that never mentions it.
    const paths = allReports.map(([, value]) => value.episode);
    expect(paths.every((path) => path.startsWith('/'))).toBe(true);
    expect(paths.every((path) => path.includes('/runs/'))).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// victory.json
// ---------------------------------------------------------------------------

describe('victory.json', () => {
  // No archived run has one: victory is otherwise derived, from
  // score.players[].rank, and nothing under .agent-eval/runs matches
  // `victory*`.  These payloads are transcribed from the writer
  // (bridge.lua:315) and from the oracle's own unit tests
  // (agent_eval/tests/test_replay_gateway.py:1471-1540).
  const WRITTEN_LINE =
    '{"schema_version":1,"victory":"spacerace","winners":["AgentPlace1"],"turn":753,"year":2326}';
  test('the exact line bridge.lua writes decodes and round-trips', () => {
    const parsed: unknown = JSON.parse(WRITTEN_LINE);
    const value = decodeOrThrow(decodeVictoryRecord, parsed, 'victory.json');
    expect(value.victory).toBe('spacerace');
    expect(value.winners).toEqual(['AgentPlace1']);
    expect(value.turn).toBe(753n);
    expect(value.year).toBe(2326n);
    expect(value.schema_version).toBe(1n);

    const round = encodeVictoryRecord(value);
    expect(Either.isRight(round)).toBe(true);
    if (Either.isRight(round)) {
      expect(JSON.stringify(round.right)).toBe(WRITTEN_LINE);
    }
  });
  test('a turn-limit finish has no winners and is still a victory record', () => {
    const value = decodeOrThrow(
      decodeVictoryRecord,
      { schema_version: 1, victory: 'turn_limit', winners: [], turn: 5000, year: 3000 },
      'turn-limit victory',
    );
    expect(value.winners).toEqual([]);
    expect(victoryLabel(value.victory)).toBe('score victory');
  });
  test('an empty or missing victory code is not a record', () => {
    // replay_gateway.py:693 — `if not isinstance(code, str) or not code`.
    expect(Either.isLeft(decodeVictoryRecord({ victory: '', winners: [] }))).toBe(true);
    expect(Either.isLeft(decodeVictoryRecord({ winners: [] }))).toBe(true);
    expect(Either.isLeft(decodeVictoryRecord(['not', 'a', 'dict']))).toBe(true);
  });
  test('a year before year zero is negative, and turn survives as an int', () => {
    const value = decodeOrThrow(
      decodeVictoryRecord,
      { victory: 'conquest', winners: ['Boudica'], turn: 1, year: -4000 },
      'ancient victory',
    );
    expect(value.year).toBe(-4000n);
    expect(value.turn).toBe(1n);
  });
  test('an unknown code is its own label', () => {
    // replay_gateway.py:697 — VICTORY_LABELS.get(code, code), so a newer
    // engine's condition still narrates instead of vanishing.
    expect(victoryLabel('spacerace')).toBe('spaceship victory');
    expect(victoryLabel('turn_limit')).toBe('score victory');
    expect(victoryLabel('all_defeated')).toBe('all civilizations defeated');
    expect(victoryLabel('brand_new')).toBe('brand_new');
    expect(Object.keys(VICTORY_LABELS).length).toBe(9);
  });
  test('label lookup does not answer from the prototype chain', () => {
    // A Python dict has no inherited members; a JavaScript object literal does,
    // and a plain `labels[code]` would answer with a function here.
    expect(victoryLabel('constructor')).toBe('constructor');
    expect(victoryLabel('toString')).toBe('toString');
    expect(victoryLabel('__proto__')).toBe('__proto__');
  });
});

// ---------------------------------------------------------------------------
// replay-catalog.json
// ---------------------------------------------------------------------------

/** A minimal well-formed technology row, varying only its id. */
const technologyWithId = (id: number): unknown => ({
  id,
  rule_name: 'Alphabet',
  name: 'Alphabet',
  cost_base: 0,
});

describe('replay-catalog.json', () => {
  const withDepth = decoded(decodeReplayCatalog, catalogFile('tech-tree-with-depth-and-requires.json'));
  const withoutDepth = decoded(decodeReplayCatalog, catalogFile('tech-tree-without-depth.json'));
  test('both writers claim schema_version 1', () => {
    expect(withDepth.schema_version).toBe(1n);
    expect(withoutDepth.schema_version).toBe(1n);
    expect(withDepth.technologies.length).toBe(87);
    expect(withoutDepth.technologies.length).toBe(87);
  });
  test('depth and requires are optional under the same schema_version', () => {
    const [first] = withDepth.technologies;
    expect(first?.rule_name).toBe('Advanced Flight');
    expect(first?.depth).toBe(13n);
    expect(first?.requires).toEqual([64n, 43n]);

    const [legacy] = withoutDepth.technologies;
    expect(legacy?.depth).toBe(undefined);
    expect(legacy?.requires).toBe(undefined);
    expect('depth' in (legacy ?? {})).toBe(false);
  });
  test('an autosave-reconstructed catalog has integer zero costs; the Lua one has floats', () => {
    // save_replay.py:328 sets cost_base to the int 0 because an autosave
    // cannot answer research cost; bridge.lua:357 writes the ruleset's float.
    expect(withDepth.technologies.every((tech) => tech.cost_base === 0n)).toBe(true);
    expect(withoutDepth.technologies[1]?.cost_base).toBeCloseTo(28.284271247461902, 10);

    // Lua spells every cost as a float, so the nine round ones ("640.0",
    // "3430.0", ...) are indistinguishable from ints after JSON.parse and
    // decode as bigint.  See KNOWN_INTEGRAL_FLOATS: this is a spelling gap in
    // a file the ported gateway reads but never re-publishes, since the disk
    // path reconstructs its own catalog from the autosaves.
    const bigints = withoutDepth.technologies.filter((tech) => typeof tech.cost_base === 'bigint');
    expect(bigints.length).toBe(9);
    expect(readFixture(catalogFile('tech-tree-without-depth.json'))).toContain(
      '"cost_base":3430.0',
    );
  });
  test('a float-spelled technology id normalizes to an int, as the reader does', () => {
    // bridge.lua:353 spells ids with Lua's tostring, so the archived
    // strategic-v1 catalog literally says "id":1.0 — and supervisor.py:435
    // normalizes it with int(tech_id) before anything is published.  The
    // spelling therefore cannot survive a round trip, and does not need to.
    const raw = readFixture(catalogFile('tech-tree-without-depth.json'));
    expect(raw).toContain('"id":1.0');
    expect(withoutDepth.technologies[0]?.id).toBe(1n);

    const round = encodeReplayCatalog(withoutDepth);
    expect(Either.isRight(round)).toBe(true);
    if (Either.isLeft(round)) return;
    // Lossy against the raw bytes, faithful against the parsed document.
    expect(JSON.stringify(round.right)).not.toBe(raw.trim());
    expect(JSON.stringify(round.right)).toBe(JSON.stringify(JSON.parse(raw)));
  });
  test('the reader rejects an id outside 0..511 and a fractional one', () => {
    // supervisor.py:424-434 — finite, integral and in range, or the whole
    // catalog is "malformed" and the replay degrades to catalog: null.
    const decode = Schema.decodeUnknownEither(TechnologyEntry);
    expect(Either.isRight(decode(technologyWithId(0)))).toBe(true);
    expect(Either.isRight(decode(technologyWithId(511)))).toBe(true);
    expect(Either.isLeft(decode(technologyWithId(512)))).toBe(true);
    expect(Either.isLeft(decode(technologyWithId(-1)))).toBe(true);
    expect(Either.isLeft(decode(technologyWithId(1.5)))).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// Synthetic negatives
// ---------------------------------------------------------------------------

interface Issue {
  readonly kind: string;
  readonly path: ReadonlyArray<string>;
}

interface Negative {
  readonly file: string;
  readonly decode: (input: unknown) => Either.Either<unknown, { readonly issues: ReadonlyArray<Issue> }>;
  /** The `kind@path` the rejection must name. */
  readonly expected: string;
  /** Every reported path must sit under this one, so the catch is the defect. */
  readonly prefix: string;
  readonly why: string;
}

const NEGATIVES: ReadonlyArray<Negative> = [
  {
    file: 'invalid/manifest-missing-game-id.json',
    decode: decodeManifest,
    expected: 'Missing@game_id',
    prefix: 'game_id',
    why: 'the gateway 404s a manifest whose game_id does not match (replay_gateway.py:568)',
  },
  {
    file: 'invalid/manifest-schema-version-string.json',
    decode: decodeManifest,
    expected: 'Type@schema_version',
    prefix: 'schema_version',
    why: 'schema_version is a Python int; "1" would canonicalize as a string',
  },
  {
    file: 'invalid/manifest-seats-object-not-array.json',
    decode: decodeManifest,
    expected: 'Type@config.seats',
    prefix: 'config.seats',
    why: 'summarize_episode iterates seats; a dict yields keys, not seats (scoring.py:173)',
  },
  {
    file: 'invalid/manifest-invalid-reasons-string.json',
    decode: decodeManifest,
    expected: 'Type@invalid_reasons',
    prefix: 'invalid_reasons',
    why: 'a bare string would be published one character per reason',
  },
  {
    file: 'invalid/manifest-current-turn-fractional.json',
    decode: decodeManifest,
    expected: 'Type@current_turn',
    prefix: 'current_turn',
    why: 'turns are Python ints; the gateway would coerce 3.5 to null instead of serving it',
  },
  {
    file: 'invalid/report-players-object-not-array.json',
    decode: decodeReport,
    expected: 'Type@score.players',
    prefix: 'score.players',
    why: 'the archive leaderboard iterates score.players (replay_gateway.py:781)',
  },
  {
    file: 'invalid/report-missing-seat-stats.json',
    decode: decodeReport,
    expected: 'Missing@seat_stats',
    prefix: 'seat_stats',
    why: 'summarize_episode always writes the key, empty if there were no seats',
  },
  {
    file: 'invalid/report-final-turn-string.json',
    decode: decodeReport,
    expected: 'Type@score.final_turn',
    prefix: 'score.final_turn',
    why: 'final_turn is an int or null; "none" is neither',
  },
  {
    file: 'invalid/replay-catalog-tech-missing-id.json',
    decode: decodeReplayCatalog,
    expected: 'Missing@technologies.0.id',
    prefix: 'technologies.0.id',
    why: 'a row without an id is "technology catalog is malformed" (supervisor.py:420)',
  },
  {
    file: 'invalid/replay-catalog-requires-names-not-ids.json',
    decode: decodeReplayCatalog,
    expected: 'Type@technologies.0.requires.0',
    prefix: 'technologies.0.requires',
    why: 'requires holds prerequisite ids, not rule names (save_replay.py:360)',
  },
];

const WHY_TEXTS: ReadonlyArray<string> = NEGATIVES.map((negative) => negative.why);

describe('synthetic negatives', () => {
  test.each(NEGATIVES.map((negative) => [negative.file, negative] as const))(
    '%s is rejected',
    (_file, negative) => {
      const result = negative.decode(parseFixture(negative.file));
      expect(Either.isLeft(result)).toBe(true);
      if (Either.isRight(result)) return;
      const found = result.left.issues.map(
        (issue) => `${issue.kind}@${formatIssuePath(issue.path)}`,
      );
      expect(found).toContain(negative.expected);
      // `why` is a literal in the table above, so asserting it is non-empty
    // proves nothing.  What is worth pinning is that no two rows share a
    // rationale — a copy-pasted row that forgot to change its subject is the
    // realistic way this table rots.
    expect(WHY_TEXTS.filter((text) => text === negative.why)).toHaveLength(1);
    },
  );
  test('each negative is caught at its own defect, not somewhere incidental', () => {
    // Every invalid/ fixture is a captured payload with exactly one deliberate
    // change; a rejection that also complained elsewhere would prove nothing
    // about the defect it was built to demonstrate.
    const noisy = NEGATIVES.flatMap((negative) => {
      const result = negative.decode(parseFixture(negative.file));
      if (Either.isRight(result)) return [`${negative.file}: accepted`];
      const strays = result.left.issues
        .map((issue) => formatIssuePath(issue.path))
        .filter((path) => !path.startsWith(negative.prefix));
      return strays.length === 0 ? [] : [`${negative.file}: ${strays.join(',')}`];
    });
    expect(noisy).toEqual([]);
  });
  test('WireInt refuses a fractional number and keeps a Python int a bigint', () => {
    const decode = Schema.decodeUnknownEither(WireInt);
    expect(Either.isLeft(decode(3.5))).toBe(true);
    expect(Either.isLeft(decode('3'))).toBe(true);
    const three = decode(3);
    expect(Either.isRight(three)).toBe(true);
    if (Either.isRight(three)) expect(three.right).toBe(3n);
  });
});
