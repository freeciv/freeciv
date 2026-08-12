/**
 * The barrel is a written artifact, so something has to keep it honest.
 *
 * `src/index.ts`, `src/gateway/index.ts` and `src/agent/index.ts` list every
 * name they re-export rather than `export *`-ing, which buys a reviewable
 * public surface at the cost of a list that can go stale.  This file is the
 * cost being paid: it walks each module's actual runtime exports and asserts
 * every one of them is reachable from the package barrel, in the right place.
 *
 * Only *value* exports are visible at runtime, so a dropped `type` re-export
 * would slip past this and be caught by `tsc` instead — a consumer importing a
 * missing type does not compile.  Between the two there is no gap.
 *
 * `test/core-barrel.test.ts` pins a hand-written list of the names consumers
 * are known to depend on; this one pins *completeness*.  Both are worth having:
 * the first notices a rename, the second notices an omission.
 */
import { describe, expect, test } from 'bun:test';
import { Schema } from 'effect';
import * as Wire from 'src/index';

import * as Canon from 'src/canon';
import * as ControlProtocol from 'src/control-protocol';
import * as Fnv from 'src/fnv1a64';
import * as Ids from 'src/ids';
import * as Json from 'src/json';
import * as Numeric from 'src/numeric';
import * as Tolerant from 'src/tolerant';

import * as GatewayArchive from 'src/gateway/archive';
import * as GatewayGames from 'src/gateway/games';
import * as GatewayIdentity from 'src/gateway/identity';
import * as GatewayManifest from 'src/gateway/manifest';
import * as GatewayProblem from 'src/gateway/problem';
import * as GatewayReplay from 'src/gateway/replay';

import * as AgentBatch from 'src/agent/batch';
import * as AgentDescriptor from 'src/agent/descriptor';
import * as AgentError from 'src/agent/error';
import * as AgentHealth from 'src/agent/health';
import * as AgentIds from 'src/agent/ids';
import * as AgentLegalPage from 'src/agent/legal-page';
import * as AgentObservation from 'src/agent/observation';
import * as AgentPage from 'src/agent/page';
import * as AgentPrimitives from 'src/agent/primitives';
import * as AgentProtocol from 'src/agent/protocol';
import * as AgentReceipt from 'src/agent/receipt';
import * as AgentRevision from 'src/agent/revision';
import * as AgentWait from 'src/agent/wait';

/** A module's runtime exports, minus the ESM tag `import *` adds. */
const exportsOf = (module: object): readonly string[] =>
  Object.keys(module).filter((name) => name !== 'default');

/** `[label, module]` pairs, so a failure names the module that lost a name. */
type Family = readonly (readonly [string, object])[];

const CORE: Family = [
  ['canon.ts', Canon],
  ['fnv1a64.ts', Fnv],
  ['tolerant.ts', Tolerant],
  ['json.ts', Json],
  ['numeric.ts', Numeric],
  ['control-protocol.ts', ControlProtocol],
  ['ids.ts', Ids],
];

const GATEWAY: Family = [
  ['gateway/identity.ts', GatewayIdentity],
  ['gateway/games.ts', GatewayGames],
  ['gateway/manifest.ts', GatewayManifest],
  ['gateway/archive.ts', GatewayArchive],
  ['gateway/replay.ts', GatewayReplay],
  ['gateway/problem.ts', GatewayProblem],
];

const AGENT: Family = [
  ['agent/ids.ts', AgentIds],
  ['agent/primitives.ts', AgentPrimitives],
  ['agent/protocol.ts', AgentProtocol],
  ['agent/revision.ts', AgentRevision],
  ['agent/error.ts', AgentError],
  ['agent/descriptor.ts', AgentDescriptor],
  ['agent/observation.ts', AgentObservation],
  ['agent/page.ts', AgentPage],
  ['agent/legal-page.ts', AgentLegalPage],
  ['agent/receipt.ts', AgentReceipt],
  ['agent/batch.ts', AgentBatch],
  ['agent/wait.ts', AgentWait],
  ['agent/health.ts', AgentHealth],
];

/** Every name the family exports, deduplicated across convenience re-exports. */
const surfaceOf = (family: Family): readonly string[] => [
  ...new Set(family.flatMap(([, module]) => exportsOf(module))),
];

const missingFrom = (barrel: object, family: Family): readonly string[] =>
  family.flatMap(([label, module]) =>
    exportsOf(module)
      .filter((name) => !Object.hasOwn(barrel, name))
      .map((name) => `${label}:${name}`),
  );

describe('the package barrel is complete', () => {
  test('every core value export is reachable flat from @arena/wire', () => {
    expect(missingFrom(Wire, CORE)).toEqual([]);
  });

  test('every gateway value export is reachable as Wire.Gateway.*', () => {
    expect(missingFrom(Wire.Gateway, GATEWAY)).toEqual([]);
  });

  test('every agent value export is reachable as Wire.Agent.*', () => {
    expect(missingFrom(Wire.Agent, AGENT)).toEqual([]);
  });

  test('the barrels re-export nothing their modules do not have', () => {
    const core = new Set(surfaceOf(CORE));
    // The two names the barrel itself owns, and the two namespaces.
    const owned = new Set(['WIRE_PACKAGE', 'WIRE_REVISION', 'Agent', 'Gateway']);
    expect(exportsOf(Wire).filter((name) => !core.has(name) && !owned.has(name))).toEqual([]);

    const gateway = new Set(surfaceOf(GATEWAY));
    expect(exportsOf(Wire.Gateway).filter((name) => !gateway.has(name))).toEqual([]);

    const agent = new Set(surfaceOf(AGENT));
    expect(exportsOf(Wire.Agent).filter((name) => !agent.has(name))).toEqual([]);
  });

  test('the package identity is present and stable', () => {
    expect(Wire.WIRE_PACKAGE).toBe('@arena/wire');
    expect(Wire.WIRE_REVISION).toBe(0);
  });
});

describe('the namespaces keep names apart that must stay apart', () => {
  test('Wire.WireInt is the gateway spelling, Wire.Agent.WireInt is not', () => {
    // The whole reason `Gateway` and `Agent` are namespaces rather than flat.
    // A gateway integer decodes to `bigint` — a Python `int`, which is what the
    // canonical writer needs in order to spell it without a `.0`.  An agent
    // integer stays a `number`, because the port does arithmetic on it and
    // crosses into `bigint` at the hashing boundary.
    expect(Wire.WireInt).not.toBe(Wire.Agent.WireInt);
    expect(Schema.decodeUnknownSync(Wire.WireInt)(7)).toBe(7n);
    expect(Schema.decodeUnknownSync(Wire.Agent.WireInt)(7)).toBe(7);
  });

  test('a gateway game id and an agent game id are different schemas', () => {
    expect(Wire.GameId).not.toBe(Wire.Agent.PlayGameId);
  });

  test('the shared vocabularies really are shared, not copied', () => {
    // `control-protocol.ts` exists because `gateway/games.ts` and
    // `gateway/manifest.ts` had declared this three times between them.
    expect(Wire.Gateway.PUBLIC_CONTROL_PROTOCOLS).toBe(Wire.CONTROL_PROTOCOLS);
    expect(Wire.Agent.FULL_CONTROL_V2).toBe(Wire.FULL_CONTROL_V2);
    expect(Wire.Agent.STRATEGIC_V1).toBe(Wire.STRATEGIC_V1);
    expect([...Wire.CONTROL_PROTOCOLS]).toEqual([Wire.STRATEGIC_V1, Wire.FULL_CONTROL_V2]);
    expect(Wire.isControlProtocol(Wire.FULL_CONTROL_V2)).toBe(true);
    expect(Wire.isControlProtocol('strategic-v2')).toBe(false);
  });

  test('the numeric doctrine has one home, and the gateway modules use it', () => {
    // Not `toBe`-able across modules unless they genuinely share the binding,
    // which is the property this consolidation was for.
    expect(Wire.WireNumber).toBe(Numeric.WireNumber);
    expect(Wire.WireFloat).toBe(Numeric.WireFloat);
    expect(Wire.WireNonNegativeInt).toBe(Numeric.WireNonNegativeInt);
  });
});
