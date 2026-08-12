/**
 * Decode parity for the gateway's game payloads.
 *
 * The Python service is the oracle: every assertion here is about what
 * `agent_eval/replay_gateway.py` and `agent_eval/supervisor.py` *did* emit,
 * captured in `test/fixtures/`. When a fixture and a schema disagree, the
 * schema is wrong.
 *
 * Three things are checked for every captured payload:
 *
 * 1. **it decodes** — no field the live stack emitted is rejected;
 * 2. **it round-trips** — `encode(decode(x))` is `x` key-for-key, including
 *    the fields no schema names, so a proxy built on these codecs cannot drop
 *    a server addition (`JSON.stringify` equality, not `toEqual`, so a
 *    reordered or vanished key fails);
 * 3. **the subtle fields say what the dossier says they say** — the traps are
 *    spot-asserted by name, because a schema can be permissive enough to
 *    decode a payload while being wrong about what it means.
 *
 * The synthetic negatives under `invalid/` are the other half: each is a real
 * payload with exactly one defect, so a rejection test proves the schema
 * caught *that* defect rather than an incidental difference.
 */
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { describe, expect, test } from 'bun:test';
import { Either, Option, Schema } from 'effect';
import {
  ARCHIVE_ERROR_MESSAGE,
  ArchiveResult,
  DEFAULT_OBJECTIVE,
  GameRow,
  GamesIndexResponse,
  GameStatus,
  INTERRUPTED_STATUS,
  MATCH_OUTCOME_STATUSES,
  MatchOutcomeStatus,
  NATIVE_CONTROLLER,
  NATIVE_CONTROLLER_LABEL,
  NATIVE_CONTROLLER_MODEL,
  NATIVE_CONTROLLER_TYPE,
  PUBLIC_AI_DIFFICULTY_LEVELS,
  PUBLIC_CONTROL_PROTOCOLS,
  PUBLIC_SCORE_METRICS,
  PUBLIC_TIMING_MODES,
  REDACTED_INVALID_REASON,
  UNCLAIMED_CONTROLLER_LABEL,
  UpstreamResult,
  asInterruptedRow,
  decodeArchiveResult,
  decodeGameRow,
  decodeGameResult,
  decodeGameStatus,
  decodeGamesIndexResponse,
  decodeInterruptedGameRow,
  decodeMatchOutcomeStatus,
  decodeMatchOutcomeStatusTolerant,
  decodeUpstreamResult,
  encodeArchiveResult,
  encodeGameStatus,
  encodeGamesIndexResponse,
  encodeInterruptedGameRow,
  encodeUpstreamResult,
  hasJointTiming,
  interruptedSummary,
  isInterruptedGameRow,
  isInterruptedRow,
  isKnownOutcomeStatus,
  isPublicScoreMetric,
  isUpstreamResult,
  timingPairIssue,
} from 'src/gateway/games';
import { decodeGatewayProblem } from 'src/gateway/problem';
import { type JsonObject, type JsonValue, decodeJsonObject, isJsonArray, jsonField } from 'src/json';
import {
  decodeTolerant,
  encodeTolerant,
  type TolerantDecoder,
  type TolerantEncoder,
} from 'src/tolerant';

const FIXTURES = join(import.meta.dir, '../fixtures');

const rightOrThrow = <A, E>(either: Either.Either<A, E>): A =>
  Either.getOrThrowWith(either, (error) => new Error(`expected Right, got ${String(error)}`));

const someOrThrow = <A>(option: Option.Option<A>): A =>
  Option.getOrThrowWith(option, () => new Error('expected Some, got None'));

/** A fixture, as `JSON.parse` sees it — the same input the gateway's client gets. */
const fixture = (relative: string): unknown =>
  JSON.parse(readFileSync(join(FIXTURES, relative), 'utf8'));

/**
 * The same bytes as a {@link JsonObject}, for the assertions that have to ask
 * "is this key present at all?" — a question a decoded value cannot answer,
 * because the fields no schema names live outside its static type. Going
 * through `decodeJsonObject` rather than a cast keeps that honest.
 */
const jsonFixture = (relative: string): JsonObject =>
  rightOrThrow(decodeJsonObject(fixture(relative)));

/** One field of a JSON object, or `undefined` when the key is absent. */
const field = (object: JsonObject, key: string): JsonValue | undefined => jsonField(object, key);

/** A decoded value seen as raw JSON again, so unknown fields become reachable. */
const asJson = <A, I>(encode: TolerantEncoder<A, I>, value: A): JsonObject =>
  rightOrThrow(decodeJsonObject(rightOrThrow(encode(value))));

/**
 * Decode, re-encode, and assert nothing changed.
 *
 * Compared against `JSON.stringify(JSON.parse(text))` rather than the file
 * bytes: `JSON.parse` canonicalizes number *spelling* (`600.0` → `600`) before
 * a schema ever sees the value, so raw-byte equality would be testing the JSON
 * reader, not this package. Everything a schema can influence — which keys
 * survive, in which order, with which values — is covered.
 */
const roundTrips = <A, I>(
  decode: TolerantDecoder<A>,
  encode: TolerantEncoder<A, I>,
  raw: unknown,
): boolean => JSON.stringify(rightOrThrow(encode(rightOrThrow(decode(raw))))) === JSON.stringify(raw);

const gatewayIndexRaw = fixture('live/gateway-games-index.json');
const supervisorIndexRaw = fixture('live/supervisor-games-index.json');
const gatewayIndex = rightOrThrow(decodeGamesIndexResponse(gatewayIndexRaw));
const supervisorIndex = rightOrThrow(decodeGamesIndexResponse(supervisorIndexRaw));

/** Game ids the supervisor still owns; every other row in the merged index came off disk. */
const liveIds = new Set(supervisorIndex.games.map((row) => row.game_id));
const diskRows = gatewayIndex.games.filter((row) => !liveIds.has(row.game_id));

describe('GET /v1/games', () => {
  test('the merged index decodes: 5 live rows spliced in front of 26 disk rows', () => {
    expect(gatewayIndex.games).toHaveLength(31);
    expect(supervisorIndex.games).toHaveLength(5);
    expect(diskRows).toHaveLength(26);
    // The upstream-2xx branch emits [*upstream rows, *disk rows not live]
    // (replay_gateway.py:1612), so the live ids lead.
    expect(gatewayIndex.games.slice(0, 5).map((row) => row.game_id)).toEqual(
      supervisorIndex.games.map((row) => row.game_id),
    );
  });

  test('both indexes round-trip with every unknown field intact', () => {
    expect(
      roundTrips(decodeGamesIndexResponse, encodeGamesIndexResponse, gatewayIndexRaw),
    ).toBe(true);
    expect(
      roundTrips(decodeGamesIndexResponse, encodeGamesIndexResponse, supervisorIndexRaw),
    ).toBe(true);
  });

  test('integers decode as bigint and timestamps as number (canon: int vs float)', () => {
    const row = gatewayIndex.games[0];
    expect(typeof row?.turns).toBe('bigint');
    expect(typeof row?.places).toBe('bigint');
    expect(typeof row?.current_turn).toBe('bigint');
    expect(typeof row?.created_at).toBe('number');
    // 1786513188.7446342 is a float in Python too; canon spells the pair
    // apart, which is the whole reason for the bigint/number split.
    expect(row?.created_at).toBe(1786513188.7446342);
  });

  test('T2: _public_number cannot emit null, so only the live row has a null finished_at', () => {
    const nullFinished = gatewayIndex.games.filter((row) => row.finished_at === null);
    expect(nullFinished.map((row) => String(row.game_id))).toEqual(['game_QAoITB7qSmKNSwsXX6LaZG8H']);
    expect(nullFinished.every((row) => liveIds.has(row.game_id))).toBe(true);
    // Every disk row carries a real float in both slots — never null, never absent.
    expect(diskRows.every((row) => typeof row.created_at === 'number')).toBe(true);
    expect(diskRows.every((row) => typeof row.finished_at === 'number')).toBe(true);
  });

  test('T3: timing_mode and action_timeout_s are a strictly joint pair', () => {
    const rows = [...gatewayIndex.games, ...supervisorIndex.games];
    // Through the exported rule, not a re-statement of it: `timingPairIssue`
    // is what a *producer* checks itself against, so the corpus is what keeps
    // the rule honest rather than the two drifting apart.
    expect(rows.flatMap((row) => timingPairIssue(row) ?? [])).toEqual([]);
    expect(rows.every(hasJointTiming)).toBe(true);

    // The gateway omits the pair whenever the preset does not match its
    // hardcoded timeout, so most rows have neither key.
    const timed = gatewayIndex.games.filter((row) => 'timing_mode' in row);
    expect(timed).toHaveLength(12);
    expect(gatewayIndex.games.length - timed.length).toBe(19);

    // The rule has teeth: a half-present pair is named, and which half.
    expect(timingPairIssue({ timing_mode: 'blitz' })).toContain('without action_timeout_s');
    expect(timingPairIssue({ action_timeout_s: 600 })).toContain('without timing_mode');
    expect(timingPairIssue({ timing_mode: 'infinite', action_timeout_s: null })).toBeUndefined();
    expect(timingPairIssue({})).toBeUndefined();

    // action_timeout_s is null only for the "infinite" preset (:449).
    const infinite = rows.filter((row) => row.action_timeout_s === null);
    expect(infinite.every((row) => row.timing_mode === 'infinite')).toBe(true);
    expect(
      rows
        .filter((row) => row.timing_mode === 'infinite')
        .every((row) => row.action_timeout_s === null),
    ).toBe(true);
    expect(new Set<unknown>(timed.map((row) => row.timing_mode))).toEqual(
      new Set<unknown>(['default', 'infinite']),
    );
  });

  test('T4: one response spells the same optional field both ways', () => {
    const places = gatewayIndex.games.flatMap((row) => row.resolved_places);
    // Gateway-built places omit a blank optional; upstream places null it.
    expect(places.some((place) => !('model' in place))).toBe(true);
    expect(places.some((place) => place.model === null)).toBe(true);
    expect(places.some((place) => typeof place.model === 'string')).toBe(true);

    expect(places.some((place) => !('controller_label' in place))).toBe(true);
    expect(places.every((place) => place.controller_label !== null)).toBe(true);

    // ai_difficulty is the sharpest case: never null on a gateway place (it is
    // only added when it resolves), always present-and-null upstream.
    const upstreamPlaces = supervisorIndex.games.flatMap((row) => row.resolved_places);
    expect(upstreamPlaces.every((place) => place.ai_difficulty === null)).toBe(true);
    const diskPlaces = diskRows.flatMap((row) => row.resolved_places);
    expect(diskPlaces.every((place) => !('ai_difficulty' in place))).toBe(true);
  });

  test('T5: benchmark_valid is tri-state on an index row', () => {
    const values = new Set(gatewayIndex.games.map((row) => row.benchmark_valid));
    expect(values).toEqual(new Set<boolean | null>([true, false, null]));
  });

  test('control_protocol is omitted, never nulled, on legacy strategic-v1 archives', () => {
    const withProtocol = gatewayIndex.games.filter((row) => 'control_protocol' in row);
    expect(withProtocol).toHaveLength(20);
    expect(withProtocol.every((row) => row.control_protocol === 'full-control-v2')).toBe(true);
    expect(gatewayIndex.games.every((row) => row.control_protocol !== null)).toBe(true);
  });

  test('state is an open vocabulary and outcome.status is a closed one', () => {
    expect(new Set<unknown>(gatewayIndex.games.map((row) => row.state))).toEqual(
      new Set<unknown>(['completed', 'invalid', 'failed', 'cancelled', 'running']),
    );
    const statuses = new Set(gatewayIndex.games.map((row) => row.outcome.status));
    // "won" and "tied" are decided outcomes; they exist nowhere else in the
    // corpus, because victory is derived and never persisted to a run dir.
    expect(statuses).toEqual(new Set<MatchOutcomeStatus>(['won', 'tied', 'invalid', 'pending']));
    expect([...statuses].every((status) => isKnownOutcomeStatus(status))).toBe(true);
  });

  test('a decided outcome carries leaders, a margin and a score turn', () => {
    const won = gatewayIndex.games.filter((row) => row.outcome.status === 'won');
    expect(won).toHaveLength(3);
    expect(won.every((row) => row.outcome.leaders.length === 1)).toBe(true);
    expect(won.every((row) => typeof row.outcome.margin === 'bigint')).toBe(true);
    expect(won.every((row) => row.outcome.summary.includes(' won by '))).toBe(true);
    expect(won.every((row) => row.outcome.victory === null)).toBe(true);

    const tied = gatewayIndex.games.filter((row) => row.outcome.status === 'tied');
    expect(tied).toHaveLength(1);
    // A tie is margin 0 — not null, which means "nobody to compare against".
    expect(tied[0]?.outcome.margin).toBe(0n);
    expect(tied[0]?.outcome.leaders).toHaveLength(2);
    expect(tied[0]?.outcome.summary).toContain(' finished tied');
  });

  test('rank is not unique: a tie gives two rank-1 leaderboard rows', () => {
    const tied = gatewayIndex.games.find((row) => row.outcome.status === 'tied');
    const ranks = (tied?.leaderboard ?? []).map((entry) => entry.rank);
    expect(ranks).toEqual([1n, 1n]);
    // Both rows quote the same game-wide final_turn, not a per-player one.
    expect(tied?.leaderboard.map((entry) => entry.score_turn)).toEqual([2n, 2n]);
  });

  test('a leaderboard row nulls the model its own place omitted', () => {
    const rows = gatewayIndex.games.flatMap((row) => row.leaderboard);
    expect(rows.length).toBeGreaterThan(0);
    // Gateway rows always carry ai_difficulty (nullable) and never `alive`.
    expect(rows.every((entry) => entry.ai_difficulty === null)).toBe(true);
    expect(rows.every((entry) => !('alive' in entry))).toBe(true);
    expect(rows.some((entry) => entry.model === null)).toBe(true);
    expect(rows.some((entry) => entry.model === NATIVE_CONTROLLER_MODEL)).toBe(true);
  });

  test('a disk row is watchable at a root-relative path', () => {
    expect(diskRows.every((row) => row.watch_path === `/watch/${row.game_id}`)).toBe(true);
  });
});

describe('the interrupted relabel (_as_interrupted)', () => {
  /** The one non-terminal row in the corpus: a match that was in progress. */
  const runningRow = someOrThrow(
    Option.fromNullable(gatewayIndex.games.find((row) => row.state === 'running')),
  );

  test('a run with no recorded turn is dropped, not relabelled', () => {
    // _last_replay_turn returns None for a lobby husk, and _as_interrupted
    // returns None in turn — the row must vanish from the index.
    expect(Option.isNone(asInterruptedRow(runningRow, Option.none()))).toBe(true);
  });

  test('current_turn becomes max(manifest, telemetry) and stops being nullable', () => {
    expect(runningRow.current_turn).toBe(77n);
    const behind = someOrThrow(asInterruptedRow(runningRow, Option.some(70n)));
    expect(behind.current_turn).toBe(77n);

    const ahead = someOrThrow(asInterruptedRow(runningRow, Option.some(90n)));
    expect(ahead.current_turn).toBe(90n);

    const fromNull = someOrThrow(
      asInterruptedRow({ ...runningRow, current_turn: null }, Option.some(12n)),
    );
    expect(fromNull.current_turn).toBe(12n);
  });

  test('the relabelled row survives a serialize/parse cycle unchanged', () => {
    const relabelled = someOrThrow(asInterruptedRow(runningRow, Option.some(90n)));
    // The relabel produces a *decoded* row (bigint turns), so the guard reads
    // it directly; the wire form has to go out and come back.
    expect(isInterruptedGameRow(relabelled)).toBe(true);
    const wire: unknown = JSON.parse(
      JSON.stringify(rightOrThrow(encodeInterruptedGameRow(relabelled))),
    );
    const decoded = rightOrThrow(decodeInterruptedGameRow(wire));
    expect(decoded).toEqual(relabelled);

    expect(decoded.state).toBe(INTERRUPTED_STATUS);
    expect(isInterruptedRow(decoded)).toBe(true);
    expect(decoded.outcome.status).toBe(INTERRUPTED_STATUS);
    expect(decoded.outcome.summary).toBe(
      'Interrupted at turn 90 without a terminal result; the replay is available.',
    );
    // The summary interpolates the *updated* turn, not the telemetry turn.
    expect(decoded.outcome.summary).toBe(interruptedSummary(90n));
    expect(decoded.outcome.leaders).toEqual([]);
    expect(decoded.outcome.margin).toBeNull();
    expect(decoded.outcome.score_turn).toBeNull();
    expect(decoded.outcome.victory).toBeNull();
  });

  test('everything else on the row is left exactly as it was', () => {
    const relabelled = someOrThrow(asInterruptedRow(runningRow, Option.some(90n)));
    expect(relabelled.benchmark_valid).toBe(runningRow.benchmark_valid);
    expect(relabelled.leaderboard).toEqual(runningRow.leaderboard);
    expect(relabelled.finished_at).toBe(runningRow.finished_at);
    expect(relabelled.resolved_places).toEqual(runningRow.resolved_places);
    expect(relabelled.watch_path).toBe(runningRow.watch_path);
  });

  test('an unknown field the server added survives the relabel', () => {
    const games = field(jsonFixture('live/gateway-games-index.json'), 'games');
    const firstRow = rightOrThrow(
      decodeJsonObject(isJsonArray(games) ? (games[0] ?? null) : null),
    );
    const withExtra = rightOrThrow(decodeGameRow({ ...firstRow, future_field: [1, 2] }));
    const relabelled = someOrThrow(asInterruptedRow(withExtra, Option.some(90n)));
    expect(field(asJson(encodeInterruptedGameRow, relabelled), 'future_field')).toEqual([1, 2]);
  });

  test('"interrupted" is a state and a status no other producer emits', () => {
    expect(MATCH_OUTCOME_STATUSES).toContain(INTERRUPTED_STATUS);
    expect(Either.isRight(decodeMatchOutcomeStatus(INTERRUPTED_STATUS))).toBe(true);
    expect(gatewayIndex.games.some((row) => row.state === INTERRUPTED_STATUS)).toBe(false);
  });
});

describe('GET /v1/games/{id} and /status', () => {
  const upstreamRunning = fixture('live/supervisor-status-running.json');
  const upstreamTerminal = fixture('live/supervisor-status-terminal.json');
  /** watch.json embeds the *whole* status document, so it is the archive capture. */
  const archiveStatus = field(jsonFixture('live/gateway-watch-terminal.json'), 'game');
  const proxiedStatus = field(jsonFixture('live/gateway-watch-running.json'), 'game');

  test('every captured status decodes and round-trips', () => {
    for (const raw of [upstreamRunning, upstreamTerminal, archiveStatus, proxiedStatus]) {
      expect(roundTrips(decodeGameStatus, encodeGameStatus, raw)).toBe(true);
    }
  });

  test('the archive path emits created_at/finished_at; upstream omits them entirely', () => {
    const archive = rightOrThrow(decodeGameStatus(archiveStatus));
    expect(typeof archive.created_at).toBe('number');
    expect(typeof archive.finished_at).toBe('number');

    const upstream = rightOrThrow(decodeGameStatus(upstreamRunning));
    expect('created_at' in upstream).toBe(false);
    expect('finished_at' in upstream).toBe(false);
  });

  test('T5: the archive narrows benchmark_valid to a strict bool', () => {
    const archive = rightOrThrow(decodeGameStatus(archiveStatus));
    expect(archive.state).toBe('completed');
    expect(archive.benchmark_valid).toBe(true);

    // Upstream keeps the live tri-state: null while the verdict is open.
    const running = rightOrThrow(decodeGameStatus(upstreamRunning));
    expect(running.state).toBe('running');
    expect(running.benchmark_valid).toBeNull();
    expect(rightOrThrow(decodeGameStatus(upstreamTerminal)).benchmark_valid).toBe(false);
  });

  test('T6: upstream leaks the real error text; the archive publishes a constant', () => {
    const upstream = rightOrThrow(decodeGameStatus(upstreamTerminal));
    expect(upstream.error).toContain('the full-control-v2 native boundary wedged');
    expect(upstream.error).not.toBe(ARCHIVE_ERROR_MESSAGE);

    // The archive capture ended cleanly, so its error is null; the only other
    // value the archive path can produce is the constant.
    expect(rightOrThrow(decodeGameStatus(archiveStatus)).error).toBeNull();
  });

  test('T3: an archived infinite-timing run publishes the pair with a null timeout', () => {
    const archive = rightOrThrow(decodeGameStatus(archiveStatus));
    expect(archive.timing_mode).toBe('infinite');
    expect(archive.action_timeout_s).toBeNull();
    // A strategic-v1 manifest predates control_protocol, so the key is absent.
    expect('control_protocol' in archive).toBe(false);
  });

  test('barrier and phase are upstream-only additions', () => {
    const upstream = rightOrThrow(decodeGameStatus(upstreamRunning));
    expect('barrier' in upstream).toBe(true);
    expect(upstream.barrier).toBeNull();
    expect(upstream.control_protocol).toBe('full-control-v2');
    expect(upstream.phase).not.toBeUndefined();
    expect(upstream.phase_events_url).toContain('/phase-events');

    const archive = rightOrThrow(decodeGameStatus(archiveStatus));
    expect('barrier' in archive).toBe(false);
    expect('phase' in archive).toBe(false);
  });

  test('an upstream place carries a controller fingerprint the index row drops', () => {
    const upstream = rightOrThrow(decodeGameStatus(upstreamRunning));
    const place = upstream.resolved_places[0];
    expect(typeof place?.controller_fingerprint).toBe('string');
    expect(place?.controller_metadata).toEqual({});
    // picker_state's 10-key comprehension drops both from the index row.
    expect(
      supervisorIndex.games
        .flatMap((row) => row.resolved_places)
        .every((row) => !('controller_fingerprint' in row)),
    ).toBe(true);
  });

  test('all eight artifact URLs are present on both producers', () => {
    for (const raw of [upstreamRunning, archiveStatus]) {
      const status = rightOrThrow(decodeGameStatus(raw));
      expect(status.join_url).toContain(`/v1/games/${status.game_id}/join`);
      expect(status.status_url).toContain('/status');
      expect(status.result_url).toContain('/result');
      expect(status.watch_json_url).toContain('/watch.json');
      expect(status.replay_url).toContain('/replay.json');
      expect(status.frames_url).toContain('/frames');
      expect(status.video_url).toContain('/video.mp4');
      expect(status.watch_url).toContain(`/watch/${status.game_id}`);
    }
  });

  test('objective defaults to the sentence the gateway hardcodes', () => {
    expect(rightOrThrow(decodeGameStatus(archiveStatus)).objective).toBe(DEFAULT_OBJECTIVE);
  });
});

describe('the synthetic negatives are caught for the right reason', () => {
  test('an outcome status outside the vocabulary is rejected at outcome.status', () => {
    const decoded = decodeGameStatus(fixture('invalid/status-outcome-status-unknown.json'));
    expect(Either.isLeft(decoded)).toBe(true);
    const paths = new Set(
      Either.match(decoded, {
        onLeft: (error) => error.issues.map((issue) => issue.path.join('.')),
        onRight: () => [],
      }),
    );
    expect(paths).toEqual(new Set(['outcome.status']));

    // The same payload is otherwise fine: the tolerant decoder keeps the
    // unknown status as a brand instead of failing, for callers that must not.
    expect(String(rightOrThrow(decodeMatchOutcomeStatusTolerant('conquered')))).toBe('conquered');
    expect(isKnownOutcomeStatus('conquered')).toBe(false);
  });

  test('a null resolved_places is rejected at resolved_places', () => {
    const decoded = decodeGameStatus(fixture('invalid/status-resolved-places-null.json'));
    expect(Either.isLeft(decoded)).toBe(true);
    const paths = new Set(
      Either.match(decoded, {
        onLeft: (error) => error.issues.map((issue) => issue.path.join('.')),
        onRight: () => [],
      }),
    );
    // `_public_places` returns [] for a non-list, so the key is an array or
    // the payload did not come from the gateway.
    expect(paths).toEqual(new Set(['resolved_places']));
  });
});

/**
 * `/result` is two unrelated documents behind one route (dossier T7).
 *
 * Neither is in `test/fixtures/`: the corpus was captured before this route
 * was modelled. Both bodies below were fetched read-only from the running
 * local stack (`http://127.0.0.1:62190`) while writing this file, verbatim and
 * unedited — they contain no absolute path and no credential-shaped field
 * (the gateway never reads `auth.json`, and the manifest's file fields are
 * bare names like `score.log`). They belong in `fixtures/live/` the next time
 * `index.json` is regenerated.
 */
describe('GET /v1/games/{id}/result', () => {
  const archiveResultText = `{"action_timeout_s":null,"artifact_id":"game_ieTomdES08hpUmFRFzCOAVMo","artifact_urls":{"frames":"https://freeciv.localhost/v1/games/game_ieTomdES08hpUmFRFzCOAVMo/frames","replay":"https://freeciv.localhost/v1/games/game_ieTomdES08hpUmFRFzCOAVMo/replay.json","status":"https://freeciv.localhost/v1/games/game_ieTomdES08hpUmFRFzCOAVMo/status","video":"https://freeciv.localhost/v1/games/game_ieTomdES08hpUmFRFzCOAVMo/video.mp4","watch":"https://freeciv.localhost/watch/game_ieTomdES08hpUmFRFzCOAVMo"},"benchmark_valid":true,"invalid_reasons":[],"leaderboard":[{"ai_difficulty":null,"controller_label":"pi-gpt-5.6-sol","controller_type":"external","model":null,"place":1,"player_color":"#0067A5","player_name":"AgentPlace1","rank":1,"score":1856,"score_turn":753,"seat_id":"place-1"},{"ai_difficulty":null,"controller_label":"pi-claude-opus-5","controller_type":"external","model":null,"place":2,"player_color":"#F38400","player_name":"AgentPlace2","rank":2,"score":760,"score_turn":753,"seat_id":"place-2"}],"outcome":{"leaders":["pi-gpt-5.6-sol"],"margin":1096,"score_turn":753,"status":"won","summary":"pi-gpt-5.6-sol won by 1096","victory":null},"schema_version":1,"score":{"final_turn":753,"players":[{"metrics":{"bnp":452,"cities":43,"contentpop":293,"corruption":80,"culture":0,"gold":16,"gov":3,"happypop":0,"landarea":340000,"literacy":3890,"luxrate":0,"mfg":295,"munits":221,"pollution":39,"pop":332,"riots":0,"scirate":60,"score":1856,"settledarea":214000,"settlers":0,"spaceship":3,"specialists":39,"taxrate":40,"techout":543,"techs":287,"unhappypop":0,"unitsbuilt":1075,"unitskilled":1150,"unitslost":550,"unitsused":275,"wonders":12},"name":"AgentPlace1","player_id":0,"rank":1,"score":1856,"seat_id":"place-1"},{"metrics":{"bnp":314,"cities":50,"contentpop":213,"corruption":52,"culture":0,"gold":34,"gov":3,"happypop":1,"landarea":452000,"literacy":280,"luxrate":0,"mfg":186,"munits":183,"pollution":1,"pop":216,"riots":0,"scirate":50,"score":760,"settledarea":157000,"settlers":0,"spaceship":0,"specialists":1,"taxrate":50,"techout":170,"techs":89,"unhappypop":1,"unitsbuilt":1484,"unitskilled":581,"unitslost":1111,"unitsused":168,"wonders":5},"name":"AgentPlace2","player_id":1,"rank":2,"score":760,"seat_id":"place-2"}]},"state":"completed","timing_mode":"infinite"}`;
  const upstreamResultText = `{"artifact_id":"game_Dn9lOXuTgVaNzhDP3FAKmiDz","artifact_urls":{"frames":"https://freeciv-api.localhost/v1/games/game_Dn9lOXuTgVaNzhDP3FAKmiDz/frames","replay":"https://freeciv-api.localhost/v1/games/game_Dn9lOXuTgVaNzhDP3FAKmiDz/replay.json","status":"https://freeciv-api.localhost/v1/games/game_Dn9lOXuTgVaNzhDP3FAKmiDz/status","video":"https://freeciv-api.localhost/v1/games/game_Dn9lOXuTgVaNzhDP3FAKmiDz/video.mp4","watch":"https://freeciv-api.localhost/watch/game_Dn9lOXuTgVaNzhDP3FAKmiDz"},"benchmark_valid":false,"invalid_reasons":["v2_boundary_wedged"],"leaderboard":[],"manifest":{"benchmark_valid":false,"bridge_status_file":"bridge-status.jsonl","checkpoints":26,"commands_file":"server.commands","config":{"action_timeout_s":600.0,"control_protocol":"full-control-v2","difficulty":"hard","lobby_timeout_s":0.0,"max_agents":2,"mode":"multiplayer","name":"session-game_Dn9lOXu","objective":"Maximize final Freeciv civilization score.","places":2,"ruleset":"classic","schema_version":1,"seats":[{"ai_difficulty":null,"base_url":null,"controller_fingerprint":"c13b2ded092a7e60c2d94f9b6b701e8940144b53f518b9ff7f945af20bdd201f","controller_label":"pi-gpt-5.6-sol","controller_metadata":{},"id":"place-1","instructions":"Maximize final Freeciv civilization score.","model":null,"name":"AgentPlace1","options":{},"type":"external"},{"ai_difficulty":null,"base_url":null,"controller_fingerprint":"552ff12cb67a1116035949e71f1f1d96ca815a8a215e35faec8ec0dd38e1da0c","controller_label":"claude-code-claude-opus-5","controller_metadata":{},"id":"place-2","instructions":"Maximize final Freeciv civilization score.","model":null,"name":"AgentPlace2","options":{},"type":"external"}],"seeds":[867429605],"server":{"frame_interval":1,"frame_zoom":1},"timing_mode":"default","turns":5000},"control_protocol":"full-control-v2","created_at":1786497679.26294,"current_turn":26,"error":"the full-control-v2 native boundary wedged and turn 26 already used its 2 recovery attempts; the seat's client stopped answering while still running (last native client state running, sidecar error deadline_exceeded), lost at turn 26 phase 0 while the phase ledger was awaiting_agent, at seat revision 142","finished_at":1786502727.1123981,"frames":26,"game_id":"game_Dn9lOXuTgVaNzhDP3FAKmiDz","invalid_reasons":["v2_boundary_wedged"],"joined_agents":2,"recovery":{"attempts":4,"by_kind":{"autosave_rollback":2,"sidecar_reattach":2},"by_outcome":{"abandoned":2,"failed":2},"recovered_to_turns":[],"rewound_applied_actions":false},"resolved_places":[{"controller":"agent","controller_fingerprint":"c13b2ded092a7e60c2d94f9b6b701e8940144b53f518b9ff7f945af20bdd201f","controller_label":"pi-gpt-5.6-sol","controller_metadata":{},"controller_type":"external","joined":true,"model":null,"place":1,"player_color":"#0067A5","player_name":"AgentPlace1","seat_id":"place-1"},{"controller":"agent","controller_fingerprint":"552ff12cb67a1116035949e71f1f1d96ca815a8a215e35faec8ec0dd38e1da0c","controller_label":"claude-code-claude-opus-5","controller_metadata":{},"controller_type":"external","joined":true,"model":null,"place":2,"player_color":"#F38400","player_name":"AgentPlace2","seat_id":"place-2"}],"returncode":0,"schema_version":1,"scorelog_file":"score.log","start_count":1,"started_at":1786500395.0029492,"state":"failed","status":"failed","trace_file":"decisions.jsonl","video_file":"game.mp4"},"outcome":{"leaders":[],"margin":null,"score_turn":null,"status":"invalid","summary":"No valid winner; no complete score snapshot is available","victory":null},"recovery":{"attempts":4,"by_kind":{"autosave_rollback":2,"sidecar_reattach":2},"by_outcome":{"abandoned":2,"failed":2},"recovered_to_turns":[],"rewound_applied_actions":false},"score":{"final_turn":26,"players":[{"added_turn":1,"alive":true,"controller_fingerprint":"552ff12cb67a1116035949e71f1f1d96ca815a8a215e35faec8ec0dd38e1da0c","last_score_turn":26,"metrics":{"bnp":7,"cities":3,"contentpop":5,"corruption":4,"culture":0,"gold":98,"gov":1,"happypop":0,"landarea":56000,"literacy":0,"luxrate":0,"mfg":10,"munits":5,"pollution":0,"pop":5,"riots":0,"scirate":60,"score":9,"settledarea":8000,"settlers":0,"spaceship":0,"specialists":0,"taxrate":40,"techout":5,"techs":2,"unhappypop":0,"unitsbuilt":6,"unitskilled":0,"unitslost":0,"unitsused":3,"wonders":0},"name":"Claude-code-claude-opus-5","player_id":1,"rank":1,"removed_turn":null,"score":9,"seat_id":"place-2"},{"added_turn":1,"alive":true,"controller_fingerprint":"c13b2ded092a7e60c2d94f9b6b701e8940144b53f518b9ff7f945af20bdd201f","last_score_turn":26,"metrics":{"bnp":4,"cities":3,"contentpop":5,"corruption":4,"culture":0,"gold":98,"gov":1,"happypop":0,"landarea":51000,"literacy":0,"luxrate":0,"mfg":8,"munits":4,"pollution":0,"pop":5,"riots":0,"scirate":60,"score":7,"settledarea":8000,"settlers":1,"spaceship":0,"specialists":0,"taxrate":40,"techout":2,"techs":1,"unhappypop":0,"unitsbuilt":5,"unitskilled":0,"unitslost":0,"unitsused":2,"wonders":0},"name":"Pi-gpt-5.6-sol","player_id":0,"rank":2,"removed_turn":null,"score":7,"seat_id":"place-1"}]},"seat_stats":{},"state":"failed"}`;
  const archiveRaw: unknown = JSON.parse(archiveResultText);
  const upstreamRaw: unknown = JSON.parse(upstreamResultText);

  test('the archive document decodes and round-trips', () => {
    expect(roundTrips(decodeArchiveResult, encodeArchiveResult, archiveRaw)).toBe(true);
    const result = rightOrThrow(decodeArchiveResult(archiveRaw));
    // Keyed artifact_id, not game_id.
    expect(String(result.artifact_id)).toBe('game_ieTomdES08hpUmFRFzCOAVMo');
    expect('game_id' in result).toBe(false);
    expect(result.benchmark_valid).toBe(true);
    expect(result.score.final_turn).toBe(753n);
    expect(result.outcome.status).toBe('won');
  });

  test('the archive document filters metrics to the publishable allow-list', () => {
    const result = rightOrThrow(decodeArchiveResult(archiveRaw));
    const player = result.score.players[0];
    expect(player?.player_id).toBe(0n);
    expect(player?.rank).toBe(1n);
    expect(Object.keys(player?.metrics ?? {}).every((key) => isPublicScoreMetric(key))).toBe(true);
    expect(Object.keys(player?.metrics ?? {})).toHaveLength(31);
    expect(Object.values(player?.metrics ?? {}).every((value) => typeof value === 'bigint')).toBe(
      true,
    );
    // The scorer's private columns never reach this document.
    for (const dropped of ['alive', 'added_turn', 'removed_turn', 'controller_fingerprint']) {
      expect(dropped in (player ?? {})).toBe(false);
    }
  });

  test('artifact_urls is the five-key, suffix-less rename', () => {
    const result = rightOrThrow(decodeArchiveResult(archiveRaw));
    expect(Object.keys(result.artifact_urls).toSorted()).toEqual([
      'frames',
      'replay',
      'status',
      'video',
      'watch',
    ]);
  });

  test('the upstream document is the whole report, and it round-trips', () => {
    expect(roundTrips(decodeUpstreamResult, encodeUpstreamResult, upstreamRaw)).toBe(true);
    const result = rightOrThrow(decodeUpstreamResult(upstreamRaw));
    expect(result.manifest['game_id']).toBe('game_Dn9lOXuTgVaNzhDP3FAKmiDz');
    expect(result.seat_stats).toEqual({});
    expect(result.recovery).not.toBeUndefined();
    // The path the gateway is careful never to emit.
    expect('episode' in result).toBe(false);
  });

  test('T7: the two documents are discriminable, and the union routes each one', () => {
    // schema_version is on the archive document only; manifest on the
    // upstream one only. Neither key overlaps, so the union cannot mis-route.
    const archiveJson = rightOrThrow(decodeJsonObject(archiveRaw));
    const upstreamJson = rightOrThrow(decodeJsonObject(upstreamRaw));
    expect(field(archiveJson, 'schema_version')).toBe(1);
    expect(field(upstreamJson, 'schema_version')).toBeUndefined();
    expect(field(upstreamJson, 'manifest')).not.toBeUndefined();
    expect(field(archiveJson, 'manifest')).toBeUndefined();

    expect(isUpstreamResult(rightOrThrow(decodeGameResult(upstreamRaw)))).toBe(true);
    expect(isUpstreamResult(rightOrThrow(decodeGameResult(archiveRaw)))).toBe(false);

    // And each schema refuses the other document.
    expect(Either.isLeft(decodeArchiveResult(upstreamRaw))).toBe(true);
    expect(Either.isLeft(decodeUpstreamResult(archiveRaw))).toBe(true);
  });

  test('an unfiltered upstream player still satisfies the shared score schema', () => {
    const result = rightOrThrow(decodeUpstreamResult(upstreamRaw));
    expect(typeof result.score?.players[0]?.player_id).toBe('bigint');

    // The six curated keys are present, so ResultPlayer decodes the row; the
    // scorer's private columns ride along as unknown fields.
    const score = rightOrThrow(
      decodeJsonObject(field(rightOrThrow(decodeJsonObject(upstreamRaw)), 'score') ?? null),
    );
    const players = field(score, 'players');
    const wirePlayer = rightOrThrow(
      decodeJsonObject(isJsonArray(players) ? (players[0] ?? null) : null),
    );
    expect(field(wirePlayer, 'controller_fingerprint')).toBeString();
    expect(field(wirePlayer, 'alive')).toBe(true);
    expect(field(wirePlayer, 'last_score_turn')).toBe(26);
  });

  test('a non-terminal game answers 409 with the problem shape, not a result', () => {
    // Observed from the live stack for the in-progress match.
    const problem = rightOrThrow(decodeGatewayProblem({ error: 'upstream returned HTTP 409' }));
    expect(problem.error).toBe('upstream returned HTTP 409');
    expect(Either.isLeft(decodeGameResult({ error: 'upstream returned HTTP 409' }))).toBe(true);
  });
});

describe('the problem shape', () => {
  test('every captured 4xx is exactly {error: string}', () => {
    for (const relative of [
      'live/supervisor-status-404.json',
      'live/gateway-replay-404.json',
      'live/gateway-events-400.json',
      'live/gateway-board-400.json',
    ]) {
      const problem = rightOrThrow(decodeGatewayProblem(fixture(relative)));
      expect(problem.error).toBeString();
      // Not an RFC-7807 document: no type/title/status/detail.
      expect(Object.keys(jsonFixture(relative))).toEqual(['error']);
    }
  });
});

/**
 * Why every omitted-or-null field is spelled `Schema.optional(Schema.NullOr(X))`.
 *
 * `Schema.optionalWith(X, { nullable: true })` reads like the right tool and is
 * what the dossier suggests, but it is a *transformation*: it folds `null` into
 * `undefined`. On these payloads that is not a nicety — `{"model": null}` is
 * how the supervisor spells a field the gateway omits, so collapsing the two
 * turns an upstream row into a gateway row on the way back out.
 */
describe('the omitted-or-null spelling is load-bearing', () => {
  const Trap = Schema.Struct({
    kept: Schema.optional(Schema.NullOr(Schema.String)),
    lost: Schema.optionalWith(Schema.String, { nullable: true }),
  });
  const decodeTrap = decodeTolerant(Trap, 'Trap');
  const encodeTrap = encodeTolerant(Trap, 'Trap');

  test('optionalWith(nullable) drops an explicit null; optional(NullOr) keeps it', () => {
    const raw = { kept: null, lost: null };
    const back = rightOrThrow(encodeTrap(rightOrThrow(decodeTrap(raw))));
    expect(back).toEqual({ kept: null });
    expect('lost' in back).toBe(false);
  });

  test('optionalWith(nullable) also moves the field out of its original position', () => {
    const raw = { lost: 'a', kept: 'b', unknown_field: 1 };
    const back = rightOrThrow(encodeTrap(rightOrThrow(decodeTrap(raw))));
    // propertyOrder: "original" cannot hold a transformed field in place.
    expect(Object.keys(back)).toEqual(['kept', 'unknown_field', 'lost']);
  });

  test('so the real schemas keep null and omission apart, in place', () => {
    const places = gatewayIndex.games.flatMap((row) => row.resolved_places);
    const omitted = places.filter((place) => !('model' in place));
    const nulled = places.filter((place) => place.model === null);
    expect(omitted.length).toBeGreaterThan(0);
    expect(nulled.length).toBeGreaterThan(0);
    // Already covered by the index round trip, but stated here as the claim
    // it actually is: the distinction survives a decode/encode cycle.
    expect(
      roundTrips(decodeGamesIndexResponse, encodeGamesIndexResponse, gatewayIndexRaw),
    ).toBe(true);
  });
});

/**
 * The Python service is the authority while both implementations run. These
 * re-read its source, so a drifted literal fails here rather than in a parity
 * diff — and the line citations in `src/gateway/games.ts` cannot rot unnoticed.
 */
const AGENT_EVAL = join(import.meta.dir, '../../../../agent_eval');

/**
 * Read a Python source file the parity assertions below are written against.
 *
 * **Ungated on purpose.**  A missing `agent_eval/` used to make these tests
 * *skip*, so a checkout without the Python side reported the whole parity story
 * green while checking nothing.  This throws at module load instead, which is
 * the standard `test/canon.test.ts` and `test/fnv1a64.test.ts` already set for
 * the python3 oracle: a missing authority fails, it does not disappear.
 */
const readPythonSource = (path: string): Promise<string> => Bun.file(path).text();

const gatewaySource = await readPythonSource(join(AGENT_EVAL, 'replay_gateway.py'));
const supervisorSource = await readPythonSource(join(AGENT_EVAL, 'supervisor.py'));

/** The members of a Python `NAME = {"a", "b"}` set literal. */
const pythonSet = (source: string, name: string): ReadonlySet<string> => {
  const match = new RegExp(`${name} = \\{([^}]*)\\}`).exec(source);
  return new Set([...(match?.[1] ?? '').matchAll(/"([^"]+)"/g)].map((entry) => entry[1] ?? ''));
};

describe('parity with the Python producers', () => {
  test('the Python sources are present — ungated, so a missing authority fails instead of skipping', () => {
    expect(gatewaySource.length).toBeGreaterThan(0);
    expect(supervisorSource.length).toBeGreaterThan(0);
  });

  test('PUBLIC_SCORE_METRICS is the same 31 names', () => {
    expect(pythonSet(gatewaySource, 'PUBLIC_SCORE_METRICS')).toEqual(
      new Set<string>(PUBLIC_SCORE_METRICS),
    );
    expect(PUBLIC_SCORE_METRICS).toHaveLength(31);
  });

  test('PUBLIC_CONTROL_PROTOCOLS and PUBLIC_AI_DIFFICULTY_LEVELS are the same sets', () => {
    expect(pythonSet(gatewaySource, 'PUBLIC_CONTROL_PROTOCOLS')).toEqual(
      new Set<string>(PUBLIC_CONTROL_PROTOCOLS),
    );
    expect(pythonSet(gatewaySource, 'PUBLIC_AI_DIFFICULTY_LEVELS')).toEqual(
      new Set<string>(PUBLIC_AI_DIFFICULTY_LEVELS),
    );
  });

  test('the timing presets are still the four _public_timing accepts', () => {
    expect(gatewaySource).toContain('if mode not in {"default", "blitz", "infinite", "custom"}:');
    expect(PUBLIC_TIMING_MODES).toEqual(['default', 'blitz', 'infinite', 'custom']);
  });

  test('the fixed strings are transcribed verbatim', () => {
    expect(gatewaySource).toContain(`"${ARCHIVE_ERROR_MESSAGE}"`);
    expect(gatewaySource).toContain(`"${REDACTED_INVALID_REASON}"`);
    expect(gatewaySource).toContain(`"${DEFAULT_OBJECTIVE}"`);
    expect(gatewaySource).toContain(`"${UNCLAIMED_CONTROLLER_LABEL}"`);
    expect(gatewaySource).toContain(`"${NATIVE_CONTROLLER_LABEL}"`);
    expect(gatewaySource).toContain(`controller == "${NATIVE_CONTROLLER}"`);
    expect(gatewaySource).toContain(`controller_type or "${NATIVE_CONTROLLER_TYPE}"`);
    expect(gatewaySource).toContain(`model or "${NATIVE_CONTROLLER_MODEL}"`);
  });

  test('the interrupted relabel still writes this state and this sentence', () => {
    expect(gatewaySource).toContain('row["state"] = "interrupted"');
    expect(gatewaySource).toContain('"status": "interrupted"');
    expect(gatewaySource).toContain("f\"Interrupted at turn {row['current_turn']} without a \"");
    expect(gatewaySource).toContain('"terminal result; the replay is available."');
    expect(interruptedSummary(7n)).toBe(
      'Interrupted at turn 7 without a terminal result; the replay is available.',
    );
  });

  test('every modelled outcome status is a literal in one of the two sources', () => {
    const missing = MATCH_OUTCOME_STATUSES.filter(
      (status) =>
        !gatewaySource.includes(`"${status}"`) && !supervisorSource.includes(`"${status}"`),
    );
    expect(missing).toEqual([]);
  });

  test('the archive result is still keyed artifact_id and not game_id', () => {
    expect(gatewaySource).toContain('"artifact_id": archive.game_id');
    expect(Object.keys(ArchiveResult.fields)).toContain('artifact_id');
    expect(Object.keys(ArchiveResult.fields)).not.toContain('game_id');
    expect(Object.keys(UpstreamResult.fields)).toContain('manifest');
  });

  test('a status still has both timestamps and an index row still has watch_path', () => {
    expect(Object.keys(GameStatus.fields)).toContain('created_at');
    expect(Object.keys(GameRow.fields)).toContain('watch_path');
    expect(Object.keys(GamesIndexResponse.fields)).toEqual(['schema_version', 'games']);
  });
});
