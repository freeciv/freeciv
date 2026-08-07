# PORT_MAP — unit ownership for the play-cli migration

Read `PLAN.md` first. This file is the ownership contract: **no two agents write the same
file.** If you need something you do not own, import it from the interface sketch below and
code against the signature — do not reach into another unit's file, do not create a file that
is not on your row.

Paths are relative to `play-cli/`. Python spans are `play/client.py` line numbers unless
prefixed `mirror:` (= `play/state_mirror.py`).

---

## 0. Ownership table

### Core — owned by the scaffold/integrator, never by a unit

| File | Contents |
| --- | --- |
| `package.json`, `tsconfig.json`, `bunfig.toml` | bun single package, `bin { play: "./src/bin.ts" }`, strict + `noUncheckedIndexedAccess` + `verbatimModuleSyntax` |
| `src/bin.ts` | `BunRuntime.runMain` bootstrap |
| `src/cli-main.ts` | root `Command`, subcommand registry, Layer stack, the single error→exit-code mapping site |
| `src/constants.ts` | every `V2_*` / regex constant from client.py:38–165, 1990–1997, 2370–2371, 3083–3091, 3788–3815, 4146, 4414–4688, 5183, 5807–5823, 6765, 6973–6974 |
| `src/errors.ts` | tagged errors: `PlayerError`, `V2ResponseError`, `DriftError`, `SessionMissingError`, `AliasStaleError`, `LockTimeoutError` |
| `src/exit.ts` | 0 / 2 / 75 / 66 mapping |
| `src/schema/*.ts` | the 41 OpenAPI schemas + **all wire-payload validators** (see §1) |
| `src/services/http.ts` | `serviceUrl`, `requestJson`, `requestJsonResponse`, redirect refusal, timeouts (197–310) |
| `src/services/v2-client.ts` | `v2Url`, `v2Response`, busy retry/backoff, `raiseValidatedV2Error` (2358–2406) |
| `src/services/session-store.ts` | state root, session path/load, seat-binding read/write, `.v2-state` load/save, current-session pointer (727–1246 minus the bits listed for U02/U03) |
| `src/services/private-fs.ts` | `openStateDirectory`, private read/write/append, atomic replace, mode 0600 checks (311–530) |
| `src/services/locks.ts` | generic advisory lock, `.v2-state` lock, request lock (531–595, 714–726) |
| `src/services/json-output.ts` | `printV2Json`, `jsonRequested`, `PLAY_JSON` env, JSON-only command set (3075–3116) |
| `src/render/primitives.ts` | `render`, `echo`, `drift`, `need*`, `scalar`, `jsonLiteral`, `plainName`, `flat`, `named`, `coordinates`, `table`, `revisionLabel`, `pageStatus`, `requestedScope`, `scopeText`, `probabilityText`, `schemaSummary`, `signed`, `packedLines`, `duration` (3401–3595, 4691–4708, 5335–5349) |
| `src/render/paging.ts` | the `"16/43 more --cursor …"` footer builder shared by state/legal/show |
| `test/_fixtures/**` | shared golden-fixture loaders and the fake-server harness |

### Units

| Unit | Files owned |
| --- | --- |
| U01 v1-surface-and-docs | `src/commands/prompt.cmd.ts`, `src/commands/help.cmd.ts`, `src/commands/rules.cmd.ts`, `src/commands/next.cmd.ts`, `src/commands/act.cmd.ts`, `src/commands/result.cmd.ts`, `src/docs/play-card.ts`, `src/docs/gameplay-rules.ts`, `src/render/prompt-text.ts`, `src/options.ts` (added — see §5.1), `src/services/v1-json.ts` (added — see §5.4), `test/v1-surface.test.ts`, `test/docs-surfaces.test.ts` |
| U02 join-use-invites | `src/commands/join.cmd.ts`, `src/commands/use.cmd.ts`, `src/services/invites.ts`, `src/services/evaluation-context.ts`, `src/render/join.ts`, `test/join.test.ts`, `test/use.test.ts`, `test/invites.test.ts` |
| U03 v2-client-state | `src/services/aliases.ts`, `src/services/alias-expand.ts`, `src/services/alias-refresh.ts`, `src/services/catalog-cache.ts`, `src/services/pending-catalogs.ts`, `test/aliases.test.ts`, `test/alias-refresh.test.ts`, `test/catalog-cache.test.ts` |
| U04 mirror-store | `src/services/mirror/store.ts`, `src/services/mirror/table.ts`, `src/services/mirror/map-parse.ts`, `src/services/mirror/delta.ts`, `src/services/mirror/phase-marker.ts`, `src/services/mirror/monitor-log.ts`, `src/services/mirror/index.ts`, `test/mirror-store.test.ts`, `test/mirror-table.test.ts`, `test/mirror-delta.test.ts`, `test/mirror-atomicity.test.ts` |
| U05 wait-engine | `src/services/wait.ts`, `src/render/wait.ts`, `src/commands/wait.cmd.ts`, `test/wait.test.ts`, `test/wait-exit-codes.test.ts`, `test/pvp-wait-interop.test.ts` |
| U06 health-and-phase | `src/render/health.ts`, `src/render/phase.ts`, `src/services/health-context.ts`, `src/services/health-json.ts` (added — see §5.2), `src/commands/health.cmd.ts`, `test/health.test.ts`, `test/phase-render.test.ts` |
| U07 mirror-entity-renderers | `src/render/mirror/overview.ts`, `units.ts`, `cities.ts`, `nations.ts`, `styles.ts`, `diplomacy.ts`, `governments.ts`, `actions.ts`, `options.ts`, `src/services/mirror/update-page.ts`, `src/services/mirror/update-receipt.ts`, `test/mirror-renderers.test.ts`, `test/mirror-options.test.ts` |
| U08 mirror-map-writers | `src/render/mirror/map.ts`, `src/services/mirror/update-map.ts`, `src/services/mirror/update-yields.ts`, `test/mirror-map.test.ts` |
| U09 show | `src/commands/show.cmd.ts`, `src/render/show.ts`, `src/render/mirror/yields-overlay.ts`, `test/show.test.ts`, `test/show-yields.test.ts` |
| U10 state | `src/commands/state.cmd.ts`, `src/services/state-query.ts`, `src/render/state/page.ts`, `overview.ts`, `units.ts`, `cities.ts`, `research.ts`, `tiles.ts`, `diplomacy.ts`, `city-detail.ts`, `city-outputs.ts`, `citizens.ts`, `build-choices.ts`, `government.ts`, `generic.ts`, `test/state.test.ts`, `test/state-city.test.ts`, `test/state-render.test.ts` |
| U11 legal | `src/commands/legal.cmd.ts`, `src/services/legal-query.ts`, `src/services/legal-drain.ts`, `src/services/legal-compact.ts`, `src/render/legal/rows.ts`, `grouped.ts`, `equivalence.ts`, `kinds.ts`, `page.ts`, `test/legal.test.ts`, `test/legal-compact.test.ts`, `test/legal-drain.test.ts` |
| U12 turn | `src/commands/turn.cmd.ts`, `src/services/turn-pages.ts`, `src/services/turn-end.ts`, `src/services/decisions.ts`, `src/services/meetings.ts`, `src/services/composite-json.ts`, `src/render/turn.ts`, `src/render/decisions.ts`, `test/turn.test.ts`, `test/turn-brief.test.ts`, `test/decisions.test.ts` |
| U13 batch | `src/commands/batch.cmd.ts`, `src/services/batch.ts`, `src/services/batch-persist.ts`, `src/services/canonical-body.ts`, `test/batch.test.ts`, `test/batch-persist.test.ts` |
| U14 receipt-retry-safety | `src/commands/receipt.cmd.ts`, `src/commands/retry.cmd.ts`, `src/services/receipts.ts`, `src/services/disposition.ts`, `src/render/receipt.ts`, `src/render/refusal.ts`, `test/receipt.test.ts`, `test/retry.test.ts`, `test/ambiguous.test.ts`, `test/refusal-render.test.ts` |
| U15 orders-engine | `src/services/orders/parse.ts`, `match.ts`, `arguments.ts`, `resolve.ts`, `rebind.ts`, `report.ts`, `index.ts`, `test/orders-parse.test.ts`, `test/orders-resolve.test.ts`, `test/orders-report.test.ts` |
| U16 do | `src/commands/do.cmd.ts`, `src/services/do-drain.ts`, `src/services/receipt-ledger.ts`, `src/render/actor-options.ts`, `test/do.test.ts`, `test/do-end.test.ts`, `test/do-concurrency.test.ts` |
| U17 monitor | `src/commands/monitor.cmd.ts`, `src/services/monitor-lock.ts`, `src/services/monitor-loop.ts`, `src/services/monitor-hook.ts`, `src/render/monitor.ts`, `test/monitor.test.ts`, `test/monitor-lock.test.ts` |
| U18 start-pregame | `src/commands/start.cmd.ts`, `src/services/pregame.ts`, `src/render/pregame.ts`, `test/start.test.ts`, `test/pregame.test.ts` |

---

## 1. Shared-core interface sketch

Signatures workers may import. The scaffold agent lands these first; everything else is
private to its unit. Names are final — do not rename.

```ts
// src/errors.ts
export class PlayerError extends Data.TaggedError("PlayerError")<{ readonly message: string }> {}
export class V2ResponseError extends Data.TaggedError("V2ResponseError")<{
  readonly message: string; readonly status: number; readonly payload: unknown
}> {}
export class DriftError extends Data.TaggedError("DriftError")<{ readonly label: string }> {}

// src/schema/index.ts — permissive decode: unknown fields PASS THROUGH.
export const decodeRevision: (v: unknown) => Effect.Effect<Revision, DriftError>
export const decodeV2Header: (v: unknown, label: string) => Effect.Effect<V2Header, DriftError>
export const decodeError: (v: unknown) => Effect.Effect<StructuredError, DriftError>
export const decodeDescriptor: (v: unknown, label: string) => Effect.Effect<LegalActionDescriptor, DriftError>
export const decodePage: (v: unknown) => Effect.Effect<PageEnvelope, DriftError>
export const decodeLegalPage: (v: unknown) => Effect.Effect<LegalActionPageEnvelope, DriftError>
export const decodeReceipt: (v: unknown) => Effect.Effect<CommandReceipt, DriftError>
export const decodeHealth: (v: unknown, session: Session) => Effect.Effect<HealthEnvelope, DriftError>
export const decodeWait: (v: unknown) => Effect.Effect<WaitEnvelope, DriftError>
export const decodePhaseEndEvent: (v: unknown) => Effect.Effect<PhaseEndEvent, DriftError>
export const decodeRecoveryEvent: (v: unknown) => Effect.Effect<RecoveryEvent, DriftError>
export const decodeInvestigation: (v: unknown) => Effect.Effect<CityInvestigationObservation, DriftError>
export const exact: (v: unknown, fields: ReadonlySet<string>, label: string) => ...
export const opaque: (v: unknown, label: string) => ...
export const jsonValue: (v: unknown, label: string, depth?: number) => ...
export const safeNumber: (v: unknown, label: string, opts?: { nullable?: boolean }) => ...
export const cursorExpiry: (v: unknown) => string
export const legacyCatalogId: (...) => string

// src/services/http.ts
export interface JsonResponse { readonly status: number; readonly body: unknown; readonly headers: ... }
export declare const Http: Context.Tag<Http, {
  readonly requestJson: (method: string, url: string, opts?: RequestOptions) => Effect.Effect<unknown, PlayerError>
  readonly requestJsonResponse: (method: string, url: string, opts?: RequestOptions) => Effect.Effect<JsonResponse, PlayerError>
}>
export const serviceUrl: (value?: string) => Effect.Effect<string, PlayerError>

// src/services/v2-client.ts
export declare const V2Client: Context.Tag<V2Client, {
  readonly get: (suffix: string, query?: ReadonlyRecord<string, string>) => Effect.Effect<unknown, V2ResponseError | PlayerError>
  readonly post: (suffix: string, body: unknown) => Effect.Effect<unknown, V2ResponseError | PlayerError>
}>

// src/services/session-store.ts
export interface Session { readonly gameId: string; readonly controller: string; readonly serviceUrl: string; ... }
export declare const SessionStore: Context.Tag<SessionStore, {
  readonly resolve: (explicit: string) => Effect.Effect<{ path: string; session: Session }, PlayerError>
  readonly resolveV2: (explicit: string) => Effect.Effect<{ path: string; session: Session }, PlayerError>
  readonly readState: (path: string) => Effect.Effect<V2ClientState, PlayerError>
  readonly writeState: (path: string, next: V2ClientState) => Effect.Effect<void, PlayerError>
  readonly withStateLock: <A, E, R>(path: string, body: (s: V2ClientState) => Effect.Effect<A, E, R>) => Effect.Effect<A, E | PlayerError, R>
  readonly withRequestLock: <A, E, R>(path: string, body: Effect.Effect<A, E, R>) => Effect.Effect<A, E | PlayerError, R>
  readonly readSeatBinding: () => Effect.Effect<Option.Option<SeatBinding>, PlayerError>
  readonly bindWorkspaceSeat: (...) => Effect.Effect<void, PlayerError>
  readonly setCurrentSession: (path: string) => Effect.Effect<void, PlayerError>
}>

// src/services/private-fs.ts
export declare const PrivateFs: Context.Tag<PrivateFs, {
  readonly writeText / writeJson / readText / loadObject / appendText / regularFile: ...
}>

// src/services/locks.ts
export const withAdvisoryLock: <A, E, R>(path: string, timeoutS: number, body: Effect.Effect<A, E, R>) => ...

// src/services/json-output.ts
export const printV2Json: (value: unknown) => Effect.Effect<void>
export const jsonRequested: (command: string, flag: boolean) => boolean

// src/render/primitives.ts
export const render: (lines: ReadonlyArray<string>) => Effect.Effect<void>
export const echo: (line: string) => Effect.Effect<void>
export const table: (rows: ReadonlyArray<ReadonlyArray<string>>) => ReadonlyArray<string>
export const named: (value: unknown, aliases?: ReadonlyRecord<string, string>) => string
export const scalar / flat / coordinates / jsonLiteral / plainName / signed / packedLines / duration
export const revisionLabel / pageStatus / requestedScope / scopeText / probabilityText / schemaSummary
export const need / needInt / needText / drift

// src/render/paging.ts
export const pagingFooter: (shown: number, total: number | null, cursor: string | null, command: string) => ReadonlyArray<string>
```

### Contract-first cross-unit imports

A few wave-2 units consume another wave-2 unit. Those signatures are frozen here so both
sides can be written in parallel; the *owner* writes the implementation, the consumer codes
against this text.

```ts
// U11 legal → consumed by U15, U16, U03(alias refresh callback), U12
export const readLegalPage: (ctx: LegalCtx, query: LegalQuery) => Effect.Effect<LegalPageEnvelope, ...>
export const drainLegal: (ctx: LegalCtx, actorId: string) => Effect.Effect<ReadonlyArray<CompactAction>, ...>
export const compactLegalAction: (d: LegalActionDescriptor) => CompactAction

// U13 batch → consumed by U16, U14
export const submitBatch: (ctx, commands: ReadonlyArray<Command>) => Effect.Effect<BatchDisposition, ...>
export const persistBatchForAction: (ctx, actionId, args) => Effect.Effect<PersistedBatch, ...>
export const batchIntent: (state: V2ClientState, batchId: string) => string

// U14 receipt → consumed by U16, U13, and cli-main (refusal rendering)
export const orderReceiptOk: (d: BatchDisposition) => boolean
export const renderReceipt: (r: CommandReceipt, intent: string) => ReadonlyArray<string>
export const renderDisposition: (d: BatchDisposition, intent: string) => ReadonlyArray<string>
export const renderErrorPayload: (payload: unknown) => ReadonlyArray<string>   // cli-main imports this

// U15 orders → consumed by U16
export const parseOrders: (text: unknown) => Effect.Effect<ReadonlyArray<string>, PlayerError>
export const resolveOrders: (ctx, orders) => Effect.Effect<OrderOutcomes, PlayerError>
export const unresolvedReport: (u: OrderUnresolved) => ReadonlyArray<string>

// U12 turn → consumed by U16 (do --end/--await/--brief prints the same bytes)
export const phaseEnd: (ctx, opts: PhaseEndOptions) => Effect.Effect<PhaseEndResult, ...>
export const awaitAndBrief: (ctx, opts) => Effect.Effect<ReadonlyArray<string>, ...>
export const compositeJson: (command: string, parts: Record<string, unknown>) => unknown
export const renderTurn: (ctx, pages) => ReadonlyArray<string>

// U06 health → consumed by U05, U12, U16, U17
export const renderHealth: (health: HealthEnvelope) => ReadonlyArray<string>
export const phaseHeadline / phaseText / holderSeat / seatLabel / priorEndLine
export const turnHealthEpoch / turnHealthContext

// U05 wait → consumed by U12, U16, U17
export const waitValue: (ctx, args: WaitArgs) => Effect.Effect<WaitEnvelope, ...>
export const waitExitCode: (w: WaitEnvelope) => 0 | 75 | 66
export const renderWait: (w: WaitEnvelope) => ReadonlyArray<string>
export const awaitLine: (w: WaitEnvelope, follow?: string) => string
export const waitArgs: (raw) => WaitArgs

// U03 v2-client-state → consumed by nearly everything
export const rememberPage / rememberReceipt / rememberDrainedActor
export const expandAlias / expandActionAlias / expandEntityAlias / expandTileAlias
export const expandActionAliasRefreshing: (ctx, alias, fetch: LegalPageFetcher) => ...
export const resolveAliasArguments / aliasMap / closestAliases / freshActionAliases
export const cachedDescriptors / cachedActorCatalog / cachedKindScopes / catalogEquivalence

// U04 mirror → consumed by U07, U08, U09, U12, U17
export const mirrorDir / writeMirror / readMirror / mirrorText / cachedPhaseNote
export const parseTable / tableText / pageNote / revLine / isNewer / isStale
export const parseMap / mapSize / tileChars / terrainChars
export const readPhaseMarker / updatePhaseMarker / appendMonitorLog / updateFromHealth
// round-2 additive extension (NOTES §12.1): CPython text measurement, for any unit
// that caps or pads server-supplied text.  Never `.length`/`.slice`/`.padEnd`.
export const codePoints / codeLength / codeSlice / ljust
// round-3 additive extension (NOTES §12.4): CPython's whitespace, for any unit that
// strips, splits or regex-matches server-supplied text.  Never `.trim()`, never `\s`.
export const strip / rstrip / splitLines / PY_SPACE_CLASS
```

---

## 2. Per-unit specs

### Wave 1

**U01 — v1 surface and doc surfaces.** Ports `command_prompt` (6131–6178), `command_next`
(11498–11527), `command_act` (11528–11580), `command_result` (11581–11594), and the two
justfile doc recipes (`help` → `play/docs/play.md`, `rules` → `play/docs/gameplay.md`,
justfile:396–404). The three v1 commands hit `/v1/games/{id}/observation`,
`/v1/games/{id}/action` and `/v1/games/{id}/result` through core `Http` and are JSON-only
(they are in `V2_JSON_ONLY_COMMANDS`, so they always print `json.dumps(indent=2,
sort_keys=True)` shape — key order sorted, two-space indent, trailing newline). `prompt`
emits the long f-string card with `--game-id/--name/--place` substitution and the `--place`
fragment only when non-empty. `help`/`rules` must emit the doc bodies **byte-identical**;
copy the two markdown files into `src/docs/*.ts` as template literals and add a test that
diffs them against the originals so drift in `play/docs/` is caught. Exit 0 always unless the
underlying request fails.

**U02 — join, use, invites.** Ports `_invite` (6010–6126), `_apply_play_defaults` +
`command_join` (6179–6417), `_workspace_relative` / `_resolve_use_target` / `command_use`
(6418–6512), `_render_join` (5965–6009), `_seat_binding_line` (898–913),
`_preconfigured_game_id` (914–932) and `_validate_evaluation_context` (1010–1056). The
invite loader is security-critical: `.invites` must be a real directory inside `play/` (not a
symlink), invite files must resolve inside it, must be mode 0600, must carry
`schema_version == 1` and a matching `game_id`; a CLI/env token (`--join-token` /
`AGENT_EVAL_JOIN_TOKEN`) is a complete credential override that skips the *default* invite
file but not an explicitly configured one. Every failure path has an exact remediation
sentence naming `just invite {game_id}` — port those strings verbatim. `join` probes
`/health`, reads `/v1/games/{id}/status`, POSTs `/v1/games/{id}/join`, writes the session
file 0600, binds the workspace seat, then prints the join block plus the v2 protocol card
(`V2_PROTOCOL_CARD`, 2956). `use` with no target lists candidate sessions; with a target
resolves a path or bare controller name and repoints the current-session marker.

**U03 — v2 client state: aliases + catalog cache.** The single trap-heaviest unit besides
U14. Ports 1085–1113 (`_empty_action_aliases`, `_empty_v2_client_state`), 1559–1713
(`_validate_pending_catalogs`, `_entity_alias_id_matches`, `_validate_alias_state`,
`_validate_drained_actors`), 2407–3001 (`_revision_order`, `_cursor_expired`,
`_drop_pending_for_cursor`, `_drop_pending_for_scope`, `_entity_alias_prefix`,
`_action_semantics`, `_alias_entries`, `_assign_action_aliases`, `_rebind_action_aliases`,
`_assign_entity_aliases`, `_assign_tile_aliases`, `_tile_reference`, `_learn_state_aliases`,
`_learn_descriptor_aliases`, `_remember_drained_actor`, `_remember_page_unlocked`,
`_remember_page`, `_remember_receipt`), 3030–3052 (`_promoted_catalog_page`), 3117–3400 (all
alias expansion/refresh/resolution), 3984–4067 (`_catalog_signature`, `_cached_actor_catalog`,
`_catalog_equivalence`) and 7979–8029 (`_cached_descriptors`, `_kind_list`,
`_cached_kind_scopes`). Exact renumbering behavior is the spec: `a1..aN` action aliases die
with their revision, `u1/c1/p1/r1` entity aliases and `T(x,y)` tile aliases persist and are
capped (`V2_MAX_*`). `_refresh_stale_alias` needs to re-fetch a legal page — take it as an
injected `LegalPageFetcher` callback (signature in §1) so this unit stays wave 1; U11 supplies
the real one. Every cap, every "closest alias" suggestion string, and the
`_alias_refresh_command` text must match. Tests: the `test_v2_alias*`, `test_v2_taught_*`,
`test_v2_cursor_*` and `test_a*` cases in `test_client.py`.

**U04 — mirror store and parse primitives.** Ports `state_mirror.py` 215–440 (`_client`,
`_error`, `_write`, `_read`, `_dig`, `_cell`, `_pair`, `_position`, `_handle`, `_alias_map`,
`_file_name`, `_revision_pair`, `_rev_line`, `_newer`, `_stale`, `_Table`, `_parse_table`,
`_table_text`, `_page_note`), 730–853 (`_terrain_chars`, `_Map`, `_parse_map`, `_map_size`,
`_tile_chars`), 1084–1214 (delta parse/text/update/row-changes), 1215–1223 (`mirror_dir`),
1517–1757 (phase marker, `append_monitor_log`, `update_from_health`, `_number`, `_text`);
plus client.py 3002–3074 (`_mirror_path`, `_mirror`, `_mirror_page`, `_mirror_receipt`,
`_mirror_health` bridge), 10441–10456 and 10755–10801 (`_mirror_text`, `_cached_phase_note`).
Writes are atomic (temp + rename, mode 0600) and must never leak partial files — port
`AtomicityTests` and `LeakTests` first. The revision stamp line and the "stale: rendered at
revN, now revM" comparison live here and are consumed verbatim by U09. Tests:
`MirrorDirectoryTests`, `RevisionStampTests`, `TableRenderingTests`, `DeltaTests`,
`AtomicityTests`, `LeakTests`, `ApiTests`, `DriftTests` from `test_state_mirror.py`.

**U05 — wait engine and the `wait` command.** Ports 6698–6770 (`_await_line`, `_wait_args`),
9916–10270 (`_local_wait_response`, `_legacy_wait_value`, `_wait_value`, `_phase_is_mine`,
`_wait_exit_code`, `_holder_remaining_s`, `_wait_until_turn`, `_waiting_tick_line`,
`_render_wait`, `_wait_command_value`) and 10698–10754 (`_seat_rebound`, `command_wait`).
The exit-code contract is the whole point: `V2_WAIT_EXIT_ACTIVE=0`, `V2_WAIT_EXIT_RETRY=75`,
`V2_WAIT_EXIT_TERMINAL=66`, driven by `V2_WAKE_REASONS` / `V2_SATISFIED_WAKE_REASONS`.
`--for-turn` blocks until the phase is genuinely this seat's or the current holder's deadline
runs out, capped by `--max` (default = holder's remaining deadline). Both `--wait_s` and
`--wait-s` spellings are accepted, one at a time. Polling uses `--poll-s` and must emit the
same tick lines in the same order. Tests: `PvPWaitInteropTests` and every `test_v2_wait_*`.

**U06 — health and phase rendering, `health` command.** Ports 5350–5551 (`_holder_seat`,
`_seat_label`, `_phase_headline`, `_phase_text`, `_prior_end_line`, `_render_health`),
6544–6569 (`command_health`) and 6570–6597 (`_turn_health_epoch`, `_turn_health_context`) —
the last two are exported because U12/U16 build the turn header from them. `--json` prints the
raw validated health envelope. The phase headline and the "prior phase ended by …" line are
consumed byte-for-byte by `turn`, `do --end --await`, `wait` and `monitor`, so this unit's
golden fixtures are the shared truth for all of them. Tests: every `test_v2_health_*` plus
the phase-header assertions inside the turn/wait tests.

### Wave 2

**U07 — mirror entity renderers.** Ports `state_mirror.py` 441–729 (`_render_overview`,
`_render_units`, `_render_cities`, `_render_nations`, `_render_styles`, `_render_diplomacy`,
`_render_governments`), 976–1083 (`_argument_names`, `_target_text`, `_action_flags`,
`_render_actions`), 1224–1299 (`update_from_page` section dispatch), 1388–1470
(`_update_options`) and 1471–1516 (`update_from_receipt`). Column widths, truncation, the
per-section page note and the alias column all come from U04 primitives — do not reimplement
`_table`/`_cell`. `_update_options` writes the pregame option files that U18 reads back.
Tests: `OptionTests` and the renderer half of `TableRenderingTests`.

**U08 — mirror map writers.** Ports `state_mirror.py` 854–900 (`_render_map`), 1300–1323
(`_update_map`) and 1324–1387 (`_update_yields`) — the write side that turns a tiles page into
`state/map.txt` and `state/map-yields.txt`. Parsing helpers (`_parse_map`, `_tile_chars`,
`_terrain_chars`, `_map_size`) belong to U04; import them. Terrain legend ordering and the
unseen-tile character must match exactly. Tests: `MapTests`.

**U09 — `show` and the yields overlay.** Ports 11191–11497 (`_show_option_files`,
`_show_catalog`, `_show_present`, `_show_empty`, `_show_sources`, `_show_staleness`,
`_show_default`, `_show_rows`, `_show_named`, `_show_grep`, `command_show`) plus
`state_mirror.render_map_yields` (901–975), which is a read-side renderer invoked only by
`show map --yields`. `show` never hits the network: it reads the mirror, reports what is
present, and prefixes stale sections with the "stale: rendered at revN, now revM"
re-verification line from U04. `--grep` is literal by default and a regex with `--regex`;
matches print with their section header. The empty-mirror error names the command that would
populate it. This unit carries the largest share of the offline byte-diff oracle — write its
goldens from a copied finished workspace. Tests: `test_v2_show_*`.

**U10 — `state` command and live-state renderers.** Ports 4249–5334: `_route_summary`,
`_unit_row`, `_render_units`, `_city_row`, `_render_cities`, `_render_research`,
`_terrain_code(s)`, `_tile_cells`, `_render_tiles`, `_economy_text`, `_research_text`,
`_score_text`, `_render_overview`, `_city_output_rows`, `_city_granary_text`,
`_city_citizens_text`, `_city_production_lines`, `_city_management_lines`,
`_city_drilldown_lines`, `_render_city_detail`, `_meeting_summary`, `_render_diplomacy`,
`_render_city_citizens`, `_flatten_item`, `_render_generic_items`, `_render_section_items`,
`_city_build_stock`, `_build_choice_note`, `_render_city_build_choices`,
`_government_scope_lines`, `_render_state_page`; plus 7707–7816 (`_state_query`,
`command_state`). Section names come from `V2_SECTIONS`/`V2_CITY_SECTIONS`;
`--actor-id/--relation-id/--center-id/--radius/--limit/--cursor` map onto the query and the
paging footer comes from `render/paging.ts`. Every page is fed to `rememberPage` (U03) and
mirrored (U04) before rendering. The city output chain (`base→gross→net→surplus`) and the
118-column row packing are the fussiest byte surfaces here. Tests: all `test_v2_state_*`,
`test_v2_city_*`, `test_v2_build_*`, `test_v2_buy_*`, `test_v2_relation_*`.

**U11 — `legal` command, renderers and drain.** Ports 3596–4248 (`_legal_subject` reserved
set, `_row_alias`, `_legal_row`, `_legal_rows`, `_legal_row_is_default`,
`_catalog_choice_line`, `_grouped_legal_lines`, `_action_kind_key`, `_descriptor_kind_key`,
`_kind_selector_matches`, `_action_target_key`, `_render_legal_page`, `_render_legal_compact`,
`_hidden_kind_lines`, `_alias_span`, `_equivalence_lines`) and 7817–8287 (`_legal_query`,
`_read_legal_page`, `_compact_legal_action`, `_compact_legal_offset`, `_compact_legal_limit`,
`_player_scope_alias`, `_kind_matched_nothing`, `_unknown_kind`, `_command_legal_all`,
`command_legal`). `--limit` means server page size 1..16 without `--kind/--all` and a compact
window 1..64 with them; `--offset` requires `--kind/--all`. `--all` drains up to
`V2_LEGAL_DRAIN_MAX_PAGES` with the byte caps `V2_LEGAL_COMPACT_MAX_BYTES` /
`V2_LEGAL_SINGLE_ACTION_MAX_BYTES` — this is where PLAN's allowed concurrency upgrade applies:
fetch pages with `Effect.forEach(..., { concurrency: 4 })` but emit rows in the Python's
order. Export `readLegalPage`, `drainLegal` and `compactLegalAction` per §1; also supply the
`LegalPageFetcher` U03 takes as a callback. Tests: all `test_v2_legal_*`, `test_v2_compact_*`,
`test_v2_scoped_*`, `test_v2_unknown_kind*`, `test_v2_global_*`.

**U12 — `turn`: briefing, decisions, phase end.** Ports 6598–6697 (`_turn_next_commands`,
`_turn_compact_page`, `_turn_page`, `_turn_health`, `_emit_turn`, `_cached_kind_action`,
`_resolve_kind_action`), 6771–6994 (`_phase_end_locked`, `_await_and_brief_locked`,
`_command_turn_end`, `_composite_json`), 6995–7563 (`_mirror_table`, `_mirror_is_fresh`,
`_mirror_cell`, `_mirror_number`, `_decision_*`, `_meeting_remedy`, `_open_meetings`,
`_batch_focus_command`, `_next_focus_line`, `_briefing_decision_lines`,
`_mirror_event_count`, `_briefing_events_line`, `_command_turn_decisions`), 5552–5770
(`_unit_status`, `_briefing_unit_lines`, `_briefing_needs_decision`, `_render_turn`,
`_researchable_names`, `_briefing_truncation`), 7564–7598 (`command_turn`) and 7599–7706
(`_turn_briefing_locked`). Flag matrix: bare `turn` prints the briefing over
`V2_TURN_SECTIONS` at `V2_TURN_PAGE_LIMIT`; `--decisions` prints one row per still-actable
actor capped at `V2_DECISION_MAX_ROWS`/`V2_DECISION_MAX_OPTIONS`; `--end` ends the phase via
the cached `phase.end` capability; `--end --await` blocks on U05's wait engine and prints the
next phase header; `--end --await --brief` prints the whole next briefing. **An applied phase
end never exits non-zero because of how the wait after it turned out** — encode that as an
explicit test. `--json` uses `_composite_json` and its exact part names. Consumes U05, U06,
U03, U04; exports `phaseEnd`/`awaitAndBrief`/`compositeJson`/`renderTurn` for U16.

**U13 — `batch` and batch persistence.** Ports 8288–8678: `_legal_command`, `_canonical_body`,
`_batch_disposition`, `_batch_error_disposition`, `_persist_batch_for_action`,
`_submit_persisted_batch`, `command_batch`, `_batch_command`; plus `_batch_intent`
(5771–5801). `--action-id` accepts an `aN` alias and is expanded through U03, re-binding a
stale alias unless `--no-refresh`. `--arguments` is a JSON object parsed by
`_parse_json_object` (6521–6543, this unit's) and validated against the descriptor's schema.
The canonical body is the byte-stable idempotency payload — the same order twice must produce
the identical `CommandBatch` bytes, so key ordering and number formatting are load-bearing.
Persistence writes the batch into `.v2-state` *before* the POST so a crash between write and
response is recoverable by U14. Exports `submitBatch`, `persistBatchForAction`, `batchIntent`.
Tests: `test_v2_batch_*`, `test_v2_action_*`, `test_v2_busy_*`, `test_v2_rate_*`.

**U14 — receipts, retry, ambiguity (safety-critical).** Ports 9777–9915 (`_get_receipt_response`,
`command_receipt`, `_missing_accepted_receipt`, `_command_retry_locked`, `command_retry`),
9483–9490 (`_order_receipt_ok`) and the refusal/receipt render block 5802–5964 (`_error_text`,
`_ERROR_REMEDIES`, `_restart_command`, `_retry_after_text`, `_render_error_payload`,
`_receipt_line`, `_observation_lines`, `_render_receipt`, `_render_disposition`). The invariant
that must never break: a receipt in state `ambiguous` is **terminal** — `retry` must refuse to
resubmit it, and no code path may replay it; `applied` and `rejected` are likewise terminal;
only `accepted` is pollable. `retry` re-reads the persisted batch and re-derives the
disposition rather than re-sending blind. Port `_order_receipt_ok` with a truth table test over
all four states × both refresh dispositions. `renderErrorPayload` is imported by `cli-main`
for the top-level `V2ResponseError` path (exit 2, refusal body on stdout, `error: …` on
stderr) — keep its signature stable. Tests: `test_v2_receipt*`, `test_v2_retry*`,
`test_v2_refusal*`, `test_v2_refusals*`, `test_v2_applied_*`, plus new adversarial ambiguity
tests.

**U15 — order resolution engine.** Ports 8679–9490 minus `_order_receipt_ok`: `_OrderUnresolved`,
`_parse_orders`, `_order_pool`, `_order_actor`, `_order_operation`, `_order_verbs`,
`_order_target_keys`, `_order_value`, `_order_properties`, `_order_array_bounds`,
`_is_array_property`, `_order_arguments`, `_order_discriminators`, `_order_match`,
`_default_arguments`, `_named_target_id`, `_order_element_resolver`, `_order_resolution`,
`_resolve_order`, `_rebind_order`, `_order_enumeration_command`, `_unresolved_report`,
`_refresh_stale_order_aliases`, `_order_outcomes`, `_order_fetch_targets`, `_resolve_orders`,
`_resolve_orders_fetching`, `_drain_legal_unlocked`, `_refresh_orders`, `_refused_actor_options`
minus its rendering. This is the natural-language-ish order matcher (`"u1 found_city London"`):
1..8 semicolon-separated orders, actor token → alias expansion, verb → action-kind match,
remaining tokens → typed arguments against the descriptor schema. The `_unresolved_report`
output — what it says when a verb is ambiguous, when a target name matches nothing, when an
alias went stale — is the agent's primary recovery surface, so port those strings exactly.
Ambiguity is a refusal (exit 2), never a guess. Exports `parseOrders`, `resolveOrders`,
`unresolvedReport`. Tests: the eleven `test_v2_do_*` order-shape cases plus
`test_v2_order_*`, `test_v2_multi_*`, `test_v2_two_*`.

**U16 — `do` command orchestration.** Ports 9491–9744 (`command_do`), 9745–9776
(`_do_phase_end`), the rendering half of `_refused_actor_options`/`_actor_options_section`
(9422–9482) and PLAN's two allowed upgrades. Accepts orders via `--orders` **or**
positionally (`./play do "u1 found_city London"` must equal `just do "…"`). Flow: resolve
orders (U15) → per-actor legal drains (U11) with `Effect.forEach` concurrency 4 → submit
(U13) → receipt/disposition (U14) → optional `--end/--await/--brief` (U12). `--continue-on-error`
keeps issuing after a rejection; `--no-refresh` refuses a stale alias instead of re-binding.
**Notes, receipts and refusals must print in exactly the Python's deterministic order despite
the concurrent fetches** — collect then emit, never emit from inside the concurrent region.
Implements the streamed receipt ledger: append each receipt to `state/receipts.log` atomically
the moment it applies; stdout unchanged. Tests: `test_v2_do_*` end-to-end cases,
`test_v2_composed_*`, `test_v2_await_*`, `test_v2_brief_*`, plus a new concurrency-determinism
test that shuffles server response latency and asserts identical stdout.

**U17 — `monitor`.** Ports 592–712 (`_monitor_lock_path`, `_monitor_lock`,
`_read_monitor_holder`, `_monitor_holder`), 10271–10440 (`_monitor_announce_line`,
`_monitor_terminal_line`, `_announced_tuple`, `_missed_phase`, `_missed_line`,
`_monitor_exec_refusal`, `_run_monitor_hook`), 10457–10512 (`_monitor_status`,
`_monitor_stop`), 10513–10589 (`command_monitor`) and 10590–10697 (`_monitor_loop`).
Monitor is strictly read-only: it never ends a phase, submits a batch or mutates cached
state — enforce that with a test that fails if any write path is reachable. Bare `monitor` is
a lock-singleton persistent process; `--once` takes no lock; `--stop`/`--status` talk to the
holder file. `--exec` runs a shell command per announcement with
`FREECIV_GAME_ID/TURN/PHASE/YOUR_TURN/DEADLINE_S/HOLDER_LABEL` exported, and must refuse any
command that would issue a game action (`_monitor_exec_refusal`). `--exit-code` overrides the
announcement status, `--max-s` gives up with 75. The missed-phase alarm (a phase of yours that
opened and timed out unseen) is unique to this command. `--json` prints one wake object per
line byte-identical to `wait --json`. Tests: the three `test_monitor_*` plus
`test_persistent_*`, `test_stopping_*`, `test_status_*`, `test_once_*`.

**U18 — `start` and the pregame catalog.** Ports 10802–11012 (`_phase_aware_refusal`,
`_mirror_pregame_catalog`, `_fetch_state_section`, `_pregame_catalog`, `_pregame_choice`,
`_check_pregame_arguments`, `_sanitized_leader`, `_pregame_default_nation`,
`_pregame_seat_defaults`, `_cached_style_name`) and 11013–11190 (`command_start`).
`--nation/--leader/--style` accept names or ids and are matched against the pregame catalogs
that U07's `_update_options` wrote to the mirror; unmatched values produce a refusal listing
the near misses. `--male`/`--female` are mutually exclusive; the default nation/leader/style
selection when nothing is passed must reproduce the Python's deterministic pick.
`_phase_aware_refusal` rewrites "you cannot do that" into a phase-aware message when the game
has already left pregame. Tests: all seven `test_v2_start_*`.

---

## 3. Rules for every worker

1. You may create only files on your row. Need one more? Ask the integrator; do not squat.
2. Decode server JSON permissively — unknown fields pass through. Never `Schema.Struct` with
   exact-field rejection on a response body. Three historical incidents came from closed
   validators.
3. Both `--flag_s` and `--flag-s` spellings are accepted; supplying both is an error.
4. Errors are values in the Effect error channel. Only `cli-main` maps them to exit codes and
   stderr text. Never `throw` for an expected failure.
5. Where `play/docs/commands.md` and `play/client.py` disagree, the Python wins — note the
   divergence in `NOTES.md`.
6. Never edit anything under `play/`, `agent_eval/`, or `.play/`.

---

## 4. Scaffold addendum — what the landed core interface actually is

The scaffold is built. §1 above is the *sketch* the plan froze; this section is the
**authoritative** signature list where the two differ. Everything not mentioned here is
exactly as §1 promised. Import paths are `src/…` (the tsconfig `paths` alias); never use a
relative `../..` import.

### 4.1 Files that exist beyond §0's core row

| File | Why |
| --- | --- |
| `src/schema/legal-page.ts` | §0 promised the file; the legal page shares `_validate_page` with the state page, so it re-exports the named entry point and adds `scopesEqual`. |
| `src/render/refusal-seam.ts` | `cli-main` must render a `V2ResponseError` body, but `renderErrorPayload` is U14's. Core cannot import a unit file that does not exist, so the dependency is **inverted** through a `Context.Tag`. See §4.6. |

### 4.2 `src/errors.ts`

`DriftError` carries **two** fields, not one:

```ts
export class DriftError extends Data.TaggedError('DriftError')<{
  readonly label: string    // "v2 page envelope" — what drifted
  readonly message: string  // the verbatim CPython sentence cli-main prints
}> {}
export const invalid: (label: string, detail?: string) => DriftError   // "invalid {label}[: {detail}]"
export const drifted: (label: string, message: string) => DriftError   // a non-"invalid …" sentence
export const playerError: (message: string) => PlayerError
export type PlayError = PlayerError | V2ResponseError | DriftError
  | SessionMissingError | AliasStaleError | LockTimeoutError
```

`AliasStaleError` also carries `alias: string`; `LockTimeoutError` also carries `path: string`.
Both, plus `SessionMissingError`, carry `message`.

### 4.3 `src/exit.ts`

Adds the signal `wait`/`monitor`/`turn --end --await` finish with:

```ts
export type ExitCode = 0 | 2 | 75 | 66
export class ExitCodeSignal extends Data.TaggedError('ExitCodeSignal')<{ readonly code: ExitCode }> {}
export const exitWith: (status: number) => ExitCodeSignal
export const passThroughExit: (status: number) => ExitCode
export const isExitCode: (value: number) => value is ExitCode
```

Fail with `exitWith(75)` to finish quietly with that status. It is **not** a `PlayerError`:
nothing prints `error: …` for it, which is how "an applied phase end never exits non-zero
because of how the wait after it turned out" is enforced.

### 4.4 `src/schema/index.ts`

Every decoder that CPython checked against the session takes the session. `SessionIdentity`
(defined in `schema/primitives.ts`, satisfied by `Session`) is that argument's type.

```ts
export const decodeRevision:   (v: unknown) => Effect<Revision, DriftError>
export const decodeV2Header:   (v: unknown, s: SessionIdentity, fields: ReadonlySet<string>, label: string) => Effect<JsonObject, DriftError>
export const decodeError:      (v: unknown) => Effect<StructuredError, DriftError>
export const decodeDescriptor: (v: unknown, revision: Revision, label?: string) => Effect<LegalActionDescriptor, DriftError>
export const decodePage:       (v: unknown, s: SessionIdentity) => Effect<PageEnvelope, DriftError>
export const decodeLegalPage:  (v: unknown, s: SessionIdentity) => Effect<LegalActionPageEnvelope, DriftError>
export const decodeReceipt:    (v: unknown, s: SessionIdentity, o?: { batchId?: string }) => Effect<CommandReceipt, DriftError>
export const decodeHealth:     (v: unknown, s: SessionIdentity) => Effect<HealthEnvelope, DriftError>
export const decodeWait:       (v: unknown, s: SessionIdentity, c: WaitContract) => Effect<WaitEnvelope, DriftError>
export const decodePhaseEndEvent: (v: unknown, s: SessionIdentity, seat: HealthSeat) => Effect<PhaseEndEvent, DriftError>
export const decodeRecoveryEvent: (v: unknown, s: SessionIdentity, seat: HealthSeat) => Effect<RecoveryEvent, DriftError>
export const decodeInvestigation: (v: unknown, revision: Revision) => Effect<CityInvestigationObservation, DriftError>
export const cursorExpiry:     (v: unknown) => Effect<string, DriftError>   // Effect, not a bare string
export const legacyCatalogId:  (s: SessionIdentity, r: Revision, scope: PageScope) => string
export const exact / opaque / jsonValue / jsonObject / safeNumber  // all Effect-returning
```

Also exported and **not** in §1, because more than one unit needs them:

- `decodeEvaluationContext` — `_validate_evaluation_context`. §0 assigned it to U02, but
  `decodeHealth` and `SessionStore.resolveV2` both call it, so it lives here. **U02 imports
  it; U02 does not reimplement it.**
- `decodeCommand` / `decodeCommandBatch` / `decodeBatchDisposition` and the `Command`,
  `CommandBatch`, `BatchDisposition`, `Disposition` types (`src/schema/batch.ts`) — U13 and
  U14 share them.
- `compareRevisions` / `revisionsEqual` / `revisionOrder`; `scopesEqual`; the id predicates
  `isActorId` / `isRelationId` / `isTileId` / `isCityId` / `isCursor` / `isCatalogId` /
  `isOpaqueId` / `isGameId` / `idPrefix` / `entityAliasIdMatches`.

**Decoded values keep the wire's snake_case keys** (`state_revision`, `receipt_state`,
`total_items`, …). This is deliberate: `--json` prints the *validated* envelope, so the
decoded object has to be the object that gets serialized.

### 4.5 Services

```ts
// src/services/private-fs.ts
export class Workspace extends Context.Tag('Workspace')<Workspace, WorkspacePaths>() {}
export interface WorkspacePaths { readonly root: string; readonly stateRoot: string }
export const WorkspaceLive: Layer<Workspace, PlayerError>
export const workspaceLayer: (root: string, stateDir?: string) => Layer<Workspace>   // tests
export class PrivateFs extends Context.Tag('PrivateFs')<PrivateFs, PrivateFsApi>() {}
export const PrivateFsLive: Layer<PrivateFs, never, Workspace>
export const privateFsFor: (w: WorkspacePaths) => PrivateFsApi                        // tests
// PrivateFsApi: workspace, resolve, writeText, writeJson, readText, loadObject,
//               appendText, regularFile, exists, openDirectory

// src/services/locks.ts
export const withAdvisoryLock: <A,E,R>(target: string, timeoutS: number, body: Effect<A,E,R>)
  => Effect<A, E | PlayerError | LockTimeoutError, R | PrivateFs>
export const withV2StateLock / withV2RequestLock: <A,E,R>(sessionPath, body) => …
export const withSuffix / v2StatePath / v2StateLockPath / v2RequestLockPath / monitorLockPath
export const lockFilePath: (files: PrivateFsApi, target: string) => Effect<string, PlayerError>  // U17
export const hasNativeFlock: () => boolean

// src/services/http.ts
export class Http extends Context.Tag('Http')<Http, HttpApi>() {}
export const HttpLive: Layer<Http>
export const httpLayer / httpFor: (fetch: typeof fetch) => …        // the fake-server seam
export const serviceUrl: (value?: string, env?: Record<string,string|undefined>) => Effect<string, PlayerError>
export const encodeRequestBody / v1ErrorMessage
// HttpApi.requestJson(method, url, opts?) -> Effect<JsonObject, PlayerError>
// HttpApi.requestJsonResponse(...)        -> Effect<JsonResponse, PlayerError>   // every status
// RequestOptions: { token?, body?, encodedBody?, timeout? }

// src/services/v2-client.ts
export interface V2Credentials { gameId: string; agentToken: string; serviceUrl: string }
export class V2Client extends Context.Tag('V2Client')<V2Client, V2ClientApi>() {}
export const V2ClientLive: Layer<V2Client, never, Http>
export const v2ClientFor: (http, sleep?) => V2ClientApi              // injectable sleep, for tests
export const v2Url / isBusyRefusal
// Every call takes the credentials explicitly — the client is stateless:
//   get(credentials, suffix, query?) / post(credentials, suffix, body)
//   response(method, url, credentials, opts?)  // raw, every status
//   raiseValidated(response)                   // -> V2ResponseError | DriftError

// src/services/session-store.ts
export interface Session extends SessionIdentity {
  gameId, agentId, agentToken, serviceUrl, controllerLabel, controlProtocol,
  place, seatId, playerName, evaluation, raw    // `raw` is the exact on-disk object
}
export const credentialsOf: (s: Session) => V2Credentials
export class SessionStore extends Context.Tag('SessionStore')<SessionStore, SessionStoreApi>() {}
export const SessionStoreLive: Layer<SessionStore, never, Workspace | PrivateFs | V2StateSchema>
export const sessionStoreFor: (w, files, schema, env?) => SessionStoreApi    // tests
export const gameId / controllerName / sessionKey / emptyV2ClientState
// SessionStoreApi adds, beyond §1: workspace, seatBindingPath, sessionPath, listSessions,
//   preconfiguredGameId, writeSession, statePath.
// readState/writeState/withStateLock take the Session too (the empty shape is derived
//   from it), and can fail with LockTimeoutError as well as PlayerError.

// src/services/json-output.ts
export const printV2Json / printJson
export const jsonRequested: (command: string, flag: boolean, env?) => boolean
export const jsonEnvironment
export const pyJsonDumps / compactJson / indentedJson / canonicalJson / encodeStringAscii
```

`SessionStore.preconfiguredGameId` ports `_preconfigured_game_id`, which §0 assigned to U02.
`_session_path` needs it for its remedy sentence, so core owns it — **U02 imports it.**
`_seat_binding_line` remains U02's.

### 4.6 The two inversion seams

Core is built before the units, so two things core *calls* are supplied by a unit as a Layer.
Both have a working core default, so the CLI runs today and improves when the unit lands.

```ts
// src/services/session-store.ts — U03 replaces V2StateSchemaDefault
export interface V2StateSchemaApi {
  readonly empty: (session: Session) => V2ClientState              // _empty_v2_client_state
  readonly validate: (state: V2ClientState) => Effect<void, PlayerError>
      // _validate_alias_state + _validate_drained_actors + _validate_pending_catalogs
  readonly cursorExpired: (expiresAt: string | null) => boolean    // _cursor_expired
}
export class V2StateSchema extends Context.Tag('V2StateSchema')<V2StateSchema, V2StateSchemaApi>() {}
export const V2StateSchemaDefault: Layer<V2StateSchema>   // correct `empty`, no-op `validate`

// src/render/refusal-seam.ts — U14 replaces RefusalRenderDefault
export interface RefusalRenderApi {
  readonly renderErrorPayload: (payload: unknown) => ReadonlyArray<string>
}
export class RefusalRender extends Context.Tag('RefusalRender')<RefusalRender, RefusalRenderApi>() {}
export const RefusalRenderDefault: Layer<RefusalRender>   // `_error_text` only
export const errorText: (payload: unknown) => string       // "{code}: {message}"
```

**U03's landing checklist:** export a `V2StateSchemaLive` Layer from `src/services/aliases.ts`
and tell the integrator; `cli-main`'s `AppLayer` swaps `V2StateSchemaDefault` for it.
**U14's landing checklist:** export `RefusalRenderLive` from `src/render/refusal.ts` wrapping
your real `renderErrorPayload`; `cli-main` swaps `RefusalRenderDefault` for it. Neither unit
edits `cli-main` itself — tell the integrator.

### 4.7 `src/render/primitives.ts` and `src/render/paging.ts`

As §1, plus:

- `render` / `echo` / `coordinates` / `need` / `needInt` / `needText` / `schemaSummary` /
  `rowAlias` return `Effect`s (they can fail with `drift`); `scalar` / `flat` / `named` /
  `table` / `signed` / `packedLines` / `duration` / `revisionLabel` / `pageStatus` /
  `requestedScope` / `scopeText` / `probabilityText` / `jsonLiteral` / `plainName` are pure.
- `drift: (label: string) => PlayerError` — it *returns* the error, it does not fail.
- `rowAlias` ports `_row_alias`, which §0 left unassigned; it is a primitive both U07 and
  U10 need.
- `formatG` (CPython `%g`) and `pyRound` (half-to-even) are exported: use them instead of
  `toPrecision`/`Math.round`, which differ from CPython on the exact cases the renderers hit.
- `pagingFooter(shown, total, cursor, command)` accepts `command` for readability but does
  **not** print it — the footer is exactly `"{shown}/{total} more --cursor {cursor}"`.
  `pagingStatus(page)` is the same fragment from a page envelope.

### 4.8 `src/cli-main.ts`

```ts
export const dualText / dualFloat / dualInteger: (dashedName: string) => Options<DualSpelling<A>>
export const resolveDual: <A>(dashedName: string, value: DualSpelling<A>, fallback: A)
  => Effect<A, PlayerError>          // both spellings at once -> "pass only one of --x-y or --x_y"
export const SUBCOMMAND_REGISTRY / SUBCOMMAND_NAMES / COMMAND_OWNERS
export const rootCommand / subcommands / AppLayer
export const handleError / runCli / main / teardown / runMain
export const commandFromArgv / jsonFlagFromArgv
```

Every subcommand in `subcommands` is a **stub owned by cli-main**: the flag surface is real
(it is the `--help` and dual-spelling contract), the handler fails with "not wired up in this
build (Uxx owns it)". A unit replaces one by exporting a `Command` from its own
`src/commands/<name>.cmd.ts`; the integrator swaps the single registry entry. Positional
arguments (`use TARGET`, `show NAME`, `do "orders…"`, `result GAME_ID`) are **not** declared
in the stubs — the owning unit adds them with `Args`, because their multiplicity is part of
that unit's spec.

### 4.9 `test/_fixtures/**`

```ts
// test/_fixtures — import from 'test/_fixtures', not from its files
identity(overrides?) -> SessionIdentity          sessionFile(overrides?) -> JsonObject
errorPayload / pagePayload / legalPagePayload / receiptPayload / healthPayload / waitPayload
FIXTURE_GAME_ID / FIXTURE_AGENT_ID / FIXTURE_CONTROLLER / FIXTURE_CURSOR / FIXTURE_REVISION
fakeFetch(plan) / recordingFetch(plan) / jsonResponse(body, status?)   // plan: queue or route map
scratchWorkspace(stateDir?) -> Scratch { workspace, files, layer, cleanup }
withScratchWorkspace(body)
```

`pagePayload`/`legalPagePayload` take `overrides.page` that **replaces** the page body rather
than merging into it — the envelope's key set is itself validated, so a test that adds `scope`
must be able to drop `cursor_expires_at` in the same breath.

The fake server is a `fetch` implementation, not a listener: no port is bound, so a suite
never races another agent's live server. Inject it with `httpLayer(fake)` / `httpFor(fake)`.

### 4.10 U06 addendum — `test/_fixtures/phase-goldens.ts`

`test/_fixtures/**` is core's row, with one appended exception: **`test/_fixtures/phase-goldens.ts`
is owned by U06.** The unit brief requires the phase headline and the "prior phase ended by …"
line to be asserted from one place, because `wait` (U05), `turn` (U12), `do --end --await`
(U16) and `monitor` (U17) all print those bytes verbatim; four units writing their own expected
strings is how a fifth silently diverges. The file exports payload builders and golden text and
contains no tests:

```ts
// test/_fixtures/phase-goldens — import from this path, not from 'test/_fixtures'
phaseHealthPayload(options) -> JsonObject      // PvPWaitInteropTests.health, ported
priorEndPayload(source, ordersSubmitted, elapsedS?) -> JsonObject
opponentSeat(place?) -> JsonObject             // one waiting_on.seats[] row
OPPONENT_CONTROLLER / OPPONENT_PLAYER
PHASE_HEADLINE  // { none, yourTurn, yourPhaseNotReady, holder, unnamedHolder, infiniteUnnamed }
PHASE_TEXT      // { none, yourTurn, holder, infinite }
PRIOR_END       // one golden per prior_end.source, plus PRIOR_END_LEAD
```

It is deliberately **not** re-exported from `test/_fixtures/index.ts`: that file is core's and
U06 does not edit it. Consumers import `'test/_fixtures/phase-goldens'` directly.

U06's exported surface, as landed (PORT_MAP §1 named these; this is the signature list):

```ts
// src/render/phase.ts
export const holderSeat: (phase: PhaseBlock | null | undefined) => WaitingOnSeat | null
export const seatLabel: (row: SeatLabelSource) => string
export const phaseHeadline / phaseText: (phase: PhaseBlock | null | undefined) => string
export const priorEndLine: (phase: PhaseBlock | null | undefined) => string

// src/render/health.ts
export const renderHealth: (health: HealthEnvelope) => ReadonlyArray<string>
export const BLOCKED_NEXT_LINE: string          // the `just wait --for-turn` remedy

// src/services/health-context.ts
export const turnHealthEpoch: (health: HealthEnvelope) => TurnHealthEpoch   // a 7-tuple
export const turnHealthEpochsEqual: (a: TurnHealthEpoch, b: TurnHealthEpoch) => boolean
export const turnHealthContext: (health: HealthEnvelope) => TurnHealthContext

// src/services/health-json.ts   (round 2 — see §5.2)
export const healthPyValue / healthJsonText / printHealthJson: (health: HealthEnvelope) => …

// src/commands/health.cmd.ts
export const healthCommand: Command                 // integrator swaps the registry entry
export const runHealth: (options: HealthOptions) => Effect<void, …, PrivateFs | SessionStore | V2Client>
export const fetchHealth: (session: Session) => Effect<HealthEnvelope, …, V2Client>
export const healthLines: (health: HealthEnvelope) => ReadonlyArray<string>
export const HEALTH_TIMEOUT_S = 10
```

---

## 5. Ownership-map amendments

Appended by workers, newest last. Each entry names the unit that landed the change.

### 5.1 `src/options.ts` — the dual-spelling helpers (U01, round 1)

**New file, shared, core-shaped.** §4.8 promises `dualText` / `dualFloat` / `dualInteger` /
`resolveDual` from `src/cli-main.ts`. A command file **cannot** import them from there:
`cli-main` imports the command modules, so `cli-main` → `<name>.cmd` → `cli-main` is a cycle
resolved during module evaluation, while `Command.make` is building its options at the command
module's top level. The import reads an uninitialized binding and the process dies with a
`ReferenceError` before `--help` prints.

The implementation therefore lives in `src/options.ts`:

```ts
export interface DualSpelling<A> { readonly dashed: Option<A>; readonly underscored: Option<A> }
export const dualText / dualFloat / dualInteger: (dashedName: string) => Options<DualSpelling<A>>
export const resolveDual:        <A>(dashedName, value: DualSpelling<A>, fallback: A) => Effect<A, PlayerError>
export const resolveDualOption:  <A>(dashedName, value: DualSpelling<A>) => Effect<Option<A>, PlayerError>
export const resolveDualRequired:<A>(dashedName, value: DualSpelling<A>) => Effect<A, PlayerError>
```

`resolveDual` keeps §4.8's exact signature. `resolveDualOption` reports *supplied* versus
*defaulted*, which `next` needs because argparse never ran `--wait-s`'s `type=float` over its
own default and so puts a different byte on the wire (NOTES §11.2). `resolveDualRequired` is for
a flag argparse marked `required=True`, which cannot stay `Options`-level required once it has
two spellings.

**Every unit imports these from `src/options`, never from `src/cli-main`.**
**Integrator:** replace `cli-main`'s copies with `export { … } from 'src/options'` so §4.8's
promise holds against one implementation.

### 5.2 `src/services/health-json.ts` — the float map for `--json` (U06, round 2)

**New file, owned by U06**, appended to U06's row in §0. It exists because
`printV2Json`/`compactJson` (core, `src/services/json-output.ts`) print a JavaScript number
and CPython prints a Python one: the supervisor's timings are floats
(`V2_TIMING_MODE_TIMEOUTS["default"] = 600.0`, `V2_AUTO_END_IDLE_GRACE_S = 20.0`, every
`time.time()` deadline and every `round(max(0.0, …), 3)` counter), so CPython writes
`"timeout_s":600.0` and `compactJson` wrote `"timeout_s":600` — a byte diff on every live
`health --json`. NOTES §2 and §10.5 carry the evidence and the limits.

The file adds no encoder of its own. It names the twelve fields and delegates:

```ts
// src/services/health-json.ts  (U06)
export const healthPyValue: (health: HealthEnvelope) => PyValue   // floats marked as PyFloat
export const healthJsonText: (health: HealthEnvelope) => string   // pyDumps(…, true)
export const printHealthJson: (health: HealthEnvelope) => Effect<void>
```

`PyValue` / `pyFloat` / `pyDumps` / `pyFloatRepr` are **U13's**, from
`src/services/canonical-body.ts`; U06 imports them and adds nothing to that file.

**U05 and U12:** `wait --json` and `turn --json` embed this same envelope under `"health"`.
Serialize it with `healthPyValue` + `pyDumps` (or splice `healthJsonText`) rather than
`compactJson`, or the divergence comes back on your surface. **Core:** the general repair is
to decode response bodies in `src/services/http.ts` with U13's `parsePython` instead of
`JSON.parse`, so `PyFloat` survives from the wire and `FLOAT_PATHS` can be deleted.

**Round 3 widens that surface** (see NOTES §10.6). Round 2's three exports could only mark a
*whole health envelope*, and `turn --json` does not embed one — it embeds
`turnHealthContext`'s projection under `"context"`, which carries the same twelve floats by
reference. So the path set is now re-rootable and the file exports:

```ts
// src/services/health-json.ts  (U06, round 3)
export const HEALTH_FLOAT_PATHS: ReadonlySet<string>                     // the twelve, envelope-rooted
export const healthFloatPathsUnder: (...prefixes: string[]) => ReadonlySet<string>
export const pyValueWithFloats: (value: unknown, floatPaths: ReadonlySet<string>) => PyValue
export const turnHealthContextPyValue: (context: TurnHealthContext) => PyValue
```

One line per consumer, each with a CPython-generated golden in `test/health.test.ts`
(`describe('the embedded projections')`) to assert against:

- **U05 `wait --json`:** `pyDumps(pyValueWithFloats(wait, healthFloatPathsUnder('health')), true)`
- **U12 `turn --json`:** `pyDumps(pyValueWithFloats(result, healthFloatPathsUnder('context')), true)`
- **U12/U16 `--end --await --brief --json`:**
  `healthFloatPathsUnder('wait.health', 'turn.context')`

`turnHealthContext` keeps returning plain numbers — the renderers read that object — so the
marking is a serialization-time step at the `--json` call site, which U06 does not own.

### 5.3 `runHealth` now requires `PrivateFs` (U06, round 2)

`command_health` (client.py:6552) calls `_mirror_health(path, value, "health")` between the
validation and the render, and that call is inside U06's assigned span. It has landed, so
§4.10's signature list changes in one place:

```ts
export const runHealth: (options: HealthOptions) =>
  Effect<void, …, PrivateFs | SessionStore | V2Client>   // was SessionStore | V2Client
```

`cli-main`'s Layer stack already provides `PrivateFsLayer`, so the integrator's registry swap
is unaffected. `fetchHealth`, `healthLines` and `HEALTH_TIMEOUT_S` are unchanged.

### 5.4 `src/services/v1-json.ts` — the lexeme-preserving v1 transport (U01, round 3)

**New file, owned by U01**, appended to U01's row in §0. `next`, `act` and `result` are the
three `V2_JSON_ONLY_COMMANDS` whose *entire* stdout is `_print_json(value)`, so the int/float
question that §5.2 solved for twelve health fields is, on this surface, every line of the
output. A field map cannot work here: `result` returns the whole run report (an arbitrarily
deep document — `.agent-eval/runs/*/report.json` carries `action_timeout_s: 600.0`,
`lobby_timeout_s: 0.0`, `latency_ms: 0.0`) and `next` embeds an arbitrary `observation`.

The file therefore takes NOTES §2's "general fix" — decode the body with U13's `parsePython`,
which reads the **lexeme** — and applies it to this unit's own transport, since
`src/services/http.ts` is core's file and hands back an already-`JSON.parse`d object:

```ts
// src/services/v1-json.ts — U01
export const pyIndentedJson: (value: PyValue) => string        // json.dumps(indent=2, sort_keys=True)
export const printPyJson:    (value: PyValue) => Effect<void>  // _print_json
export const pyField:  (value: PyObject, key: string) => PyValue        // dict.get
export const pyToJson: (value: PyValue) => JsonValue    // for the `!=` guards only
export const parsePyArgument: (text: string) => PyArgument             // json.loads(--action)
export const parsePyText:     (text: string) => PyValue
export class V1Json extends Context.Tag('V1Json')<V1Json, V1JsonApi>() {}
export const v1JsonFor / v1JsonLayer / V1JsonLive
export const v1Json: Effect<V1JsonApi>   // the ambient transport, else the live one
// V1JsonApi.requestPyJson(method, url, opts?) -> Effect<PyObject, PlayerError>
```

`v1Json` resolves through `Effect.serviceOption`, which does **not** add the tag to the
requirement channel, so **the integrator needs no change to `cli-main`'s Layer stack**: the
three commands still require only `SessionStore` (`result` requires nothing), a test injects a
fake with `v1JsonLayer(fetch)`, and production gets the live transport over the global `fetch`.
Everything about the request other than the number model is core's — the same refusal
sentences, the same `redirect: 'error'`, the same timeout shape, and core's own
`v1ErrorMessage` for a non-2xx.

**Core:** this stays a unit-local transport only until `src/services/http.ts` decodes with
`parsePython` itself. When it does, `requestPyJson` becomes a one-line delegate and every other
`--json` surface gets the same correctness (NOTES §2, §10.5, §11.9).

---

## 6. U02 addendum — what `join`/`use`/invites export

`src/render/join.ts` (U02) is the home of **`V2_PROTOCOL_CARD`**. PORT_MAP §0 routes shared
`V2_*` constants through `src/constants.ts`, but client.py:2952-3000 falls outside every line
range that row lists and the U02 brief assigns the card to this unit. It has two consumers and
must have exactly one definition (NOTES §12.2):

```ts
// src/render/join.ts — U02
export const V2_PROTOCOL_CARD: ReadonlyArray<string>            // client.py:2952-3000
export const seatBindingLine: (gameId: string, replaced?: Option<SeatBinding>) => string
export const renderJoin: (session: JsonObject, result: JsonObject, replaced?: Option<SeatBinding>)
  => ReadonlyArray<string>                                       // _render_join, minus its unused `path`
export const joinGuidance: (session: JsonObject, result: JsonObject, binding: string) => string
export const deadlineText: (actionTimeoutS: JsonValue) => string
export const timingModeText: (result: JsonObject) => string    // str(x or "unknown"), round 3
```

**U04 imports `V2_PROTOCOL_CARD` from `src/render/join`** for the `state/header.txt` bridge
(`_mirror_health`, client.py:3071, passes it as `commands=`). Do not re-declare it.

**Every unit that calls `mirrorHealth` imports it too.** CPython's `_mirror_health`
(3062-3072) hardcodes `commands=V2_PROTOCOL_CARD`; the port's `mirrorHealth`/`updateFromHealth`
take it as an *optional* argument that falls back to the five-line `DEFAULT_COMMAND_CARD`, so
every call site must pass `{ commands: V2_PROTOCOL_CARD }` or the header it writes silently
loses the ALIASES/ERRORS/ONE CALL PER TURN/MULTIPLAYER/WHICH BINDING block that `just show
header` prints. Current call sites: `src/services/turn-end.ts` (U12), `src/commands/turn.cmd.ts`
(U12), `src/commands/wait.cmd.ts` (U05) and `src/commands/start.cmd.ts` (U18). New callers
follow the same rule; see NOTES §16.9 under U18.

```ts
// src/services/invites.ts — U02
export const loadInvitation: (w: WorkspacePaths, r: InviteRequest, env?) => Effect<Invitation, PlayerError>
export const INVITE_ROOT_NOT_REAL / INVITE_ESCAPES     // the two refusals that carry no remedy
export const pyStrip: (text: string) => string         // CPython `str.strip()`, round 3
export interface InviteRequest { gameId; invite; joinToken }
export interface Invitation { token; base }

// src/services/evaluation-context.ts — U02, a seam over §4.4's decoder, not a second copy
export const validateEvaluationContext: (v: unknown, label: string, o?: EvaluationOptions)
  => Effect<EvaluationContext | null, PlayerError>

// src/commands/join.cmd.ts — U02
export const applyPlayDefaults: (w: WorkspacePaths, a: PlayIdentity) => Effect<PlayIdentity, PlayerError>
export const commandJoin: (a: JoinArgs, env?) => Effect<void, PlayerError, Workspace | Http | SessionStore>
export const joinCommand: Command                      // integrator: swap the cli-main stub

// src/commands/use.cmd.ts — U02
export const workspaceRelative: (w: WorkspacePaths, target: string) => string
export const resolveUseTarget: (w, files, store, value: string) => Effect<string, PlayerError>
export const commandUse: (a: UseArgs, env?) => Effect<void, PlayerError, Workspace | PrivateFs | SessionStore>
export const useCommand: Command                       // integrator: swap the cli-main stub
```

**Integrator:** `joinCommand` and `useCommand` are ready to replace their `cli-main` stubs.
`useCommand` adds the `TARGET` positional the stub deliberately left out; `joinCommand` defaults
`--game-id`/`--name` to `""` and accepts `--game_id`/`--join_token` (NOTES §12.1).

---

## 6. U03 addendum — the landed v2-client-state surface

Everything below is exported from the five files on U03's row. Import paths are `src/…`.

```ts
// src/services/aliases.ts
export const V2StateSchemaLive: Layer<V2StateSchema>   // integrator: swap V2StateSchemaDefault
export const v2StateSchema: V2StateSchemaApi           // the same API without the Layer wrapper
export const validateAliasState / parseActionAliases / parseEntityAliases
export const parseTileAliases / parseDrainedActors
export const rememberPage: (sessionPath, session, input: RememberPageInput)
  => Effect<RememberedPage, PlayerError | LockTimeoutError, SessionStore | PrivateFs>
export const rememberReceipt: (sessionPath, session, receipt: CommandReceipt)
  => Effect<V2ClientState, PlayerError | LockTimeoutError, SessionStore | PrivateFs>
export const rememberDrainedActor: (state, actorId) => Effect<V2ClientState, PlayerError>
export const aliasMap: (state) => Effect<AliasMap, PlayerError>
export const freshActionAliases: (state) => Effect<Record<string, ActionAliasEntry>, PlayerError>
export const actionSemantics / aliasEntries / rebindActionAliases
export const actionTargetKey / tileReference / entityAliasPrefix / jsonEquals
export type RememberPageInput =
  | { legal: false; page: PageEnvelope } | { legal: true; page: LegalActionPageEnvelope }
export interface RememberedPage { state: V2ClientState; promoted: LegalActionDescriptor[] | null }
export interface ActionAliasEntry { action_id: string; actor_id: string; semantics: string }
export type AliasMap = Readonly<Record<string, string>>

// src/services/alias-expand.ts
export const looksLikeAlias: (text: string) => boolean
export const closestAliases: (known: ReadonlyArray<string>, wanted: string) => string
export const aliasRefreshCommand: (sessionPath, actorId) => Effect<string, never, SessionStore>
export const expandAlias / expandActionAlias: (state, text, sessionPath)
  => Effect<string, PlayerError, SessionStore>
export const expandEntityAlias: (state, alias) => Effect<string, PlayerError>
export const expandTileAlias: (state, x, y) => Effect<string, PlayerError>

// src/services/alias-refresh.ts
export type LegalPageFetcher = (sessionPath, session, actorId)
  => Effect<void, PlayerError | LockTimeoutError, SessionStore | PrivateFs>   // U11 supplies it
export const refreshStaleAlias: (sessionPath, session, state, alias, { locked, fetch })
  => Effect<Option<AliasRebound>, RefreshError, SessionStore | PrivateFs>
export const expandActionAliasRefreshing: (sessionPath, session, state, alias, { locked, fetch })
  => Effect<ExpandedActionAlias, RefreshError, SessionStore | PrivateFs>
export const resolveAliasArguments: (sessionPath, session, values, { noRefresh?, fetch? })
  => Effect<ResolvedAliasArguments, RefreshError, SessionStore | PrivateFs>

// src/services/catalog-cache.ts
export const promotedCatalogPage: (page, promoted) => LegalActionPageEnvelope
export const cachedDescriptors / cachedActorCatalog: (state[, actorId]) => JsonObject[]
export const kindList: (kinds) => string
export const cachedKindScopes: (state, selector, asked, kindSelectorMatches)
  => Effect<ReadonlyArray<string>, PlayerError>
export const catalogSignature: (compacts, scope, aliases, deps) => Effect<CatalogSignature, …>
export const catalogEquivalence: (state, result, scope, aliases, deps)
  => Effect<CatalogEquivalence | null, PlayerError>
export interface CatalogRenderDeps {          // U11 supplies all four
  compactLegalAction; actionKindKey; legalRow; kindSelectorMatches
}
export const V2_KIND_LIST_MAX = 24            // client.py:7976, absent from core constants.ts

// src/services/pending-catalogs.ts
export const cursorExpired: (expiresAt: string | null, now?: number) => boolean
export const validatePendingCatalogs: (value) => Effect<void, PlayerError>
export const dropPendingForCursor / dropPendingForScope: (sessionPath, session, …)
  => Effect<V2ClientState, PlayerError | LockTimeoutError, SessionStore | PrivateFs>
export const PENDING_CATALOG_FIELDS: ReadonlySet<string>
export interface PendingCatalog { state_revision; scope; total_items; items; next_cursor; cursor_expires_at }
```

**Ownership change (append per §3.1):** `_action_target_key` (client.py:3970-3981) is listed on
U11's row but `_action_semantics` — the identity a stale `aN` is re-bound by — is defined in
terms of it, so it lands in `src/services/aliases.ts` as `actionTargetKey`. **U11 imports it;
U11 does not reimplement it.** `V2_KIND_LIST_MAX` lands in `src/services/catalog-cache.ts` for
the same reason (core's `src/constants.ts` did not carry it and is not U03's to edit).

**Integrator:** swap `V2StateSchemaDefault` for `V2StateSchemaLive` in `cli-main`'s `AppLayer`.
Nothing else in U03 touches a core file.

---

## Addendum — U05 wait engine

`V2_WAIT_S_MAX`, `V2_WAIT_TICK_S` and `V2_FOR_TURN_GRACE_S` (client.py:9992-10007) are **not**
in core's `src/constants.ts` — §0 scoped that file to the constant blocks up to client.py:6974
and these three sit at 9992. They are exported from `src/services/wait.ts`, which is U05's row.
`src/exit.ts` already carries `V2_WAIT_EXIT_ACTIVE/RETRY/TERMINAL`; U05 imports those and does
not restate them.

`src/commands/wait.cmd.ts` also exports `waitCommandWith(makeHooks)` and `liveWaitHooks`, so a
consumer (and the integrator) can rebuild the command against different cross-unit seams
without editing it.

**Integrator:** swap `cli-main`'s `waitCommand` stub for `waitCommand` from
`src/commands/wait.cmd.ts`. Nothing in U05 touches a core file.

---

## 7. U08 addendum — the mirror map/yields writers

Three files, all on U08's row. Import paths are `src/…`.

```ts
// src/render/mirror/map.ts — U08
export const renderMap: (dir: string, revision: MirrorRevision, prior: MirrorMap,
  items: ReadonlyArray<unknown>) => Effect<string, PlayerError, PrivateFs>   // _render_map
export const terrainLegendLine: (legend: ReadonlyMap<string, string>,
  grid: ReadonlyMap<string, string>) => string        // the `# terrain …` line, or ''
export const MAP_LEGEND_LINE: string                  // the fixed `# legend '?'=…` line
export const MAP_EMPTY_NOTE: string                   // `# no tiles known yet`

// src/services/mirror/update-map.ts — U08
export const updateMap: (dir, command: string, revision: MirrorRevision,
  items: ReadonlyArray<unknown>) => Effect<ReadonlyArray<string>, PlayerError, PrivateFs>
export const MAP_DELTA_TITLE = 'map'

// src/services/mirror/update-yields.ts — U08
export const updateYields: (dir, command: string, revision: MirrorRevision,
  items: ReadonlyArray<unknown>, aliases?: MirrorAliases | null)
  => Effect<ReadonlyArray<string>, PlayerError, PrivateFs>
export const yieldRows: (items: ReadonlyArray<unknown>, aliases?: MirrorAliases | null)
  => Effect<ReadonlyArray<ReadonlyArray<string>>, PlayerError>
export const YIELD_DELTA_TITLE = 'yields'
```

Both writers return the files they wrote, absolute, in CPython's order (the projection first,
`state/delta.md` second when the digest moved) and `[]` when the page is older than the mirror
— `_update_map`/`_update_yields` return `tuple[Path, ...]`, and `update_from_page` forwards it
verbatim.

**`updateYields` drops CPython's `inner` argument.** `_update_yields(session_dir, command,
revision, inner, items, aliases)` never reads `inner`; the port takes `(dir, command, revision,
items, aliases)`. Same precedent as §6's `renderJoin`, "minus its unused `path`".

**These exports are deliberately absent from `src/services/mirror/index.ts`** — that file is
U04's row and U08 does not edit it. **U07's `update-page.ts` imports `updateMap` from
`src/services/mirror/update-map` and `updateYields` from `src/services/mirror/update-yields`;
U09 imports `renderMap`'s siblings from `src/render/mirror/map`.** If the integrator wants
them on the barrel, that is a one-line addition to U04's `index.ts` — the modules use their
imports only inside function bodies, so the resulting cycle is inert.

---

## 8. U07 addendum — the mirror entity renderers and the page/receipt entry points

Everything below is exported from U07's row. Import paths are `src/…`.

```ts
// src/render/mirror/section.ts — NEW FILE, appended to U07's row (see below)
export interface RenderedSection { columns: ReadonlyArray<string>; rows: ReadonlyArray<ReadonlyArray<string>> }
export type SectionRenderer = (items: ReadonlyArray<unknown>, aliases: MirrorAliases | null)
  => Effect<RenderedSection, PlayerError>

// src/render/mirror/{overview,units,cities,nations,styles,diplomacy,governments}.ts — U07
export const renderOverview / renderUnits / renderCities / renderNations / renderStyles
       / renderDiplomacy / renderGovernments: SectionRenderer
export const OVERVIEW_COLUMNS / UNIT_COLUMNS / CITY_COLUMNS / NATION_COLUMNS
       / STYLE_COLUMNS / DIPLOMACY_COLUMNS / GOVERNMENT_COLUMNS: ReadonlyArray<string>

// src/render/mirror/actions.ts — U07
export const renderActions: (items, aliases, scopeActor: string | null) => Effect<RenderedSection, PlayerError>
export const argumentNames: (schema: unknown) => string          // _argument_names
export const targetText:    (subject: JsonObject) => string      // _target_text
export const actionFlags:   (subject: JsonObject, scopeActor: string | null) => string
export const ACTION_COLUMNS: ReadonlyArray<string>

// src/render/mirror/options.ts — U07 (a writer, despite living under render/)
export const updateOptions: (dir, command, revision: MirrorRevision, inner: JsonObject,
  items: ReadonlyArray<unknown>, aliases: MirrorAliases | null | undefined)
  => Effect<ReadonlyArray<string>, PlayerError, PrivateFs>       // _update_options

// src/services/mirror/update-page.ts — U07
export const updateFromPage: (dir, command: string, page: unknown, options?: UpdatePageOptions)
  => Effect<ReadonlyArray<string>, PlayerError, PrivateFs>
export interface UpdatePageOptions { readonly aliases?: MirrorAliases | null }
export const RENDERERS: ReadonlyMap<string, SectionRenderer>     // _RENDERERS
export const mirrorPage: (sessionPath, page: unknown, command: string, options?)
  => Effect<void, never, PrivateFs>                              // client.py:3015 `_mirror_page`

// src/services/mirror/update-receipt.ts — U07
export const updateFromReceipt: (dir, command: string, receipt: unknown)
  => Effect<ReadonlyArray<string>, PlayerError, PrivateFs>
export const mirrorReceipt: (sessionPath, receipt: unknown, command?: string)
  => Effect<void, never, PrivateFs>                              // client.py:3054 `_mirror_receipt`
```

`updateFromPage` takes the **unvalidated-shaped** `unknown`, exactly as CPython does: it
re-checks `state_revision`, the page envelope, the section name, the item list and the
`_MAX_ROWS` cap itself, and a failure there is a `PlayerError` prefixed `state mirror: `. It
returns the absolute paths it wrote in CPython's order (the section file, then `state/delta.md`
when the digest moved) and `[]` for a page older than the mirror or a section the mirror does
not project.

**Ownership change (per §3.1): `src/render/mirror/section.ts` is added to U07's row.** Seven
renderer files, `update-page.ts` and every future consumer need one name for the
`(columns, rows)` pair CPython returns as a tuple. The file is types only — no runtime code, no
imports back into `src/render/mirror/` — so it introduces no cycle. See NOTES §16.1.

**`_mirror_page` / `_mirror_receipt` (client.py:3002-3074) are listed on the core row but were
left unlanded by U04**, because both call functions that did not exist yet. They land here as
`mirrorPage` / `mirrorReceipt`, next to the entry points they wrap and using U04's own
`mirrorGuard`, so a projection failure still only warns on stderr. **U05 should replace the
inert `liveWaitHooks.mirrorPage` (NOTES §14.3) with `mirrorPage`; U10/U12/U13/U16 should call
these rather than re-wrapping `updateFromPage`.** Nothing here edits a file U07 does not own.

**Integration, inert-seam round — "should call these" was not enough.** Four consumers built a
`LegalCtx` without `mirrorPage` and `submitBatch` defaulted `mirrorReceipt` to a no-op, so
`_update_options` and `_update_from_receipt` ran from *nowhere* in the shipped CLI while the
whole gate set stayed green — an unwritten file is invisible to a byte diff of stdout. Both are
now the **default** rather than an instruction: `readLegalPage` mirrors unless a `LegalCtx`
overrides it, and `submitBatch` mirrors unless `SubmitOptions` overrides it. Write future
seams the same way round (NOTES §I.5.2, §I.5.3).

**These exports are deliberately absent from `src/services/mirror/index.ts`** — that barrel is
U04's row. Consumers import `src/services/mirror/update-page` and
`src/services/mirror/update-receipt` directly.

---

## 7. U10 addendum — the landed `state` surface

Everything below is exported from the fifteen files on U10's row. Import paths are `src/…`.

```ts
// src/commands/state.cmd.ts
export const stateCommand: Command                      // integrator: swap the cli-main stub
export const runState: (o: StateOptions) => Effect<void, StateError, SessionStore | V2Client | PrivateFs>
export const fetchStatePage: (sessionPath, session, query, cursor) => Effect<PageEnvelope, …>
export const STATE_TIMEOUT_S = 10
export interface StateOptions { session; section; actorId; relationId; centerId; radius; limit; cursor; json }

// src/services/state-query.ts
export const stateQuery: (a: StateQueryArgs) => Effect<string, PlayerError>   // _state_query
export const stateLimit: (v: string | null, fallback?: number) => Effect<number, PlayerError>  // _limit
export interface StateQueryArgs { section; actorId; relationId; centerId; radius; limit; cursor }

// src/render/state/page.ts
export const renderStatePage: (v: StatePageValue, o?: StatePageOptions)
  => Effect<ReadonlyArray<string>, PlayerError, PrivateFs>
export const renderSectionItems: (section, items, aliases?) => Effect<ReadonlyArray<string>, PlayerError>
export const STATE_RENDERERS: ReadonlyMap<string, { required; render }>
export interface StatePageValue { state_revision: Revision; page: PageBody<JsonValue> }
export interface StatePageOptions { aliases?; sessionPath?; state? }

// src/render/state/{units,cities,research,tiles,overview,diplomacy,citizens,city-detail}.ts
export const renderUnits / renderCities / renderResearch / renderTiles / renderOverview
export const renderDiplomacy / renderCityCitizens / renderCityDetail
//   all: (items: ReadonlyArray<JsonValue>, aliases?: AliasMap | null) => Effect<string[], PlayerError>
export const routeSummary / unitRow          // units.ts
export const cityRow / surplusText           // cities.ts  — surplusText is shared with city-detail
export const terrainCode / terrainCodes / tileCells   // tiles.ts
export const economyText / researchText / scoreText   // overview.ts
export const isPythonTruthy: (v: JsonValue | undefined) => boolean   // overview.ts — CPython `if value:`
export const meetingSummary                  // diplomacy.ts
export const cityGranaryText / cityCitizensText / cityProductionLines
export const cityManagementLines / cityDrilldownLines  // city-detail.ts

// src/render/state/city-outputs.ts
export const cityOutputRows: (outputs: JsonValue) => Effect<ReadonlyArray<ReadonlyArray<string>>, PlayerError>

// src/render/state/generic.ts
export const flattenItem: (item: JsonValue) => ReadonlyMap<string, string>
export const renderGenericItems: (items: ReadonlyArray<JsonValue>) => ReadonlyArray<string>

// src/render/state/build-choices.ts
export const cityBuildStock: (r: BuildStockRequest) => Effect<number | null, never, PrivateFs>
export const buildChoiceNote / renderCityBuildChoices
export const CITIES_MIRROR_FILE: ReadonlyArray<string>   // ['state', 'cities.tsv']

// src/render/state/government.ts
export const governmentScopeLines: (items, state: V2ClientState | null) => Effect<string[], PlayerError>
export const playerScopeAlias: (state: V2ClientState) => Effect<string, PlayerError>
```

**Ownership changes (appended per §3.1):**

- `_player_scope_alias` (client.py:8030-8039) is listed on U11's row, but `_government_scope_lines`
  is defined in terms of it and U10 lands first. It lands in `src/render/state/government.ts` as
  `playerScopeAlias`. **U11 imports it; U11 does not reimplement it.**
- `_limit` (client.py:6513-6518) is shared by `state` and `legal` and sits on no unit's row.
  It lands in `src/services/state-query.ts` as `stateLimit`. **U11 imports it** rather than
  declaring a second copy; `legal`'s wider 1..64 compact window is a separate function on U11's
  row and does not go through `stateLimit`.
- `V2_SHOW_FILES` is U09's. `_city_build_stock` needs exactly one of its rows, so
  `CITIES_MIRROR_FILE = ['state', 'cities.tsv']` lands in `src/render/state/build-choices.ts`.
  **U09 keeps the full map**; if U09 exports it, this constant should be replaced by an import.

**Integrator:** `stateCommand` is ready to replace `cli-main`'s `state` stub. It adds no
positional argument and keeps the stub's exact flag surface (`dualText` on the three ID flags,
`Options.optional(Options.integer('radius'))`, `--limit` as text). Nothing in U10 touches a
core file.

---

## Addendum — U18 `start` and the pregame catalog

Everything below is exported from the three files on U18's row. Import paths are `src/…`.

```ts
// src/services/pregame.ts
export const V2_PREGAME_CATALOGS: ReadonlyMap<string, PregameCatalogSpec>
export const V2_LEADER_MAX_BYTES = 47
export const LEADER_STRIP_RE: RegExp
export const PREGAME_STATE_TIMEOUT_S = 10 / PREGAME_PAGE_LIMIT = 16
export const phaseAwareRefusal: <A, E extends PlayError, R>(sessionPath, body: Effect<A,E,R>)
  => Effect<A, E | PlayerError, R | PrivateFs>                    // _phase_aware_refusal
export const mirrorPregameCatalog: (sessionPath, section) => Effect<PregameItem[], never, PrivateFs>
export const fetchStateSection: (ctx: PregameCtx, section)
  => Effect<JsonValue[], PlayError, V2Client | SessionStore | PrivateFs>
export const pregameCatalog:  (ctx, section) => Effect<PregameItem[], PlayError, …>
export const pregameChoice:   (items, wanted, label) => Effect<PregameItem, PlayerError>
export const pregameDefaultNation: (items, choose) => Effect<PregameItem, PlayerError>
export const pregameSeatDefaults: (ctx) => Effect<SeatDefaults, PlayError, …>
export const cachedStyleName: (sessionPath, styleId) => Effect<string, never, PrivateFs>
export const checkPregameArguments: (action, argumentValues) => Effect<void, PlayerError>
export const sanitizedLeader: (label: string) => string
export const defaultSex: (leader: string) => 'male' | 'female'
export const orderProperties / defaultArguments / orderReceiptOk   // see the ownership note
export interface StartHooks / SubmitOutcome / PregameAction / PregameCtx / PregameItem
export interface PregameCatalogSpec / SeatDefaults

// src/render/pregame.ts
export const PREGAME_SHOWN_MAX = 12
export const pyRepr: (text: string) => string                     // CPython `repr()` for a str
export const shownChoices: (names) => string
export const unknownChoiceRefusal / ambiguousChoiceRefusal: (…) => PlayerError
export const sexWord / startingLine / configureIntent
export const READY_INTENT / NOT_READIED_LINE
export const startJson: (parts) => JsonValue                      // the `--json` composite

// src/commands/start.cmd.ts
export type StartHooksFor = (sessionPath, session) => Effect<StartHooks, never, PrivateFs | SessionStore | V2Client>
export const liveStartHooks: StartHooksFor
export const resolveSeat: (ctx, request) => Effect<ResolvedSeat, PlayError, …>
export const runStart: (options: StartOptions, makeHooks?) => Effect<void, PlayError | ExitCodeSignal, SessionStore | V2Client | PrivateFs>
export const startCommandWith: (makeHooks: StartHooksFor) => Command
export const startCommand: Command
export interface StartOptions / ResolvedSeat
```

**`phaseAwareRefusal` has two consumers outside this unit.** `_phase_aware_refusal`
(client.py:10802-10817) sits in U18's line span but wraps `command_legal` (U11) and
`command_batch` (U13). **U11 and U13 import it from `src/services/pregame`; they do not
reimplement it.** `command_start` itself is *not* wrapped — CPython does not wrap it (NOTES
§16.1).

**Ownership change (append per §3.1):** `orderProperties` (`_order_properties`, client.py:8801)
and `defaultArguments` (`_default_arguments`, 8946) are on U15's row, and `orderReceiptOk`
(`_order_receipt_ok`, 9483) is on U14's. All three are pure and are what
`_check_pregame_arguments` and `command_start` are written against, so they land in
`src/services/pregame.ts` to keep U18 compilable and testable before wave 2 finishes.
**Integrator:** once `src/services/orders/arguments.ts` and `src/services/disposition.ts` exist,
collapse each of the three to a re-export from the owning unit so there is one definition.

~~**Integrator:** `startCommand` is ready to replace `cli-main`'s `start` stub. Its behaviour is
gated on `liveStartHooks`, which today refuses at the five seams U11/U13/U14 own; rebuild it as
`startCommandWith(realHooks)` when they land (NOTES §16.3).~~ **Done (integration, inert-seam
round):** `liveStartHooks` binds all six — U07's `mirrorPage`, U11's `drainLegal`, U12's
`resolveKindAction`, U13's `persistBatchForAction`/`submitBatch`, U14's `renderDisposition` — so
`startCommand` is `startCommandWith(liveStartHooks)` and that bundle now works. Between the swap
of the `cli-main` entry and this, `play start` exited 2 at `pregame.configure` on every lobby
seat, which is the one situation the command exists for (NOTES §I.5.1).

`SubmitOutcome.disposition`, `StartHooks.renderDisposition` and `StartHooks.receiptOk` now carry
U13's decoded `BatchDisposition` rather than `JsonValue`, and `startJson`'s `dispositions` is
`ReadonlyArray<unknown>` — a decoded envelope is a nominal interface and so is not assignable to
`JsonObject`, and rebuilding one as a `JsonValue` would drop the unknown fields the receipt
decoder deliberately passes through. `orderReceiptOk` is now a re-export of
`src/services/disposition.ts`'s, collapsing the first of the three duplications the ownership
note above asked the integrator to collapse; `orderProperties`/`defaultArguments` are still
local copies.

Nothing in U18 touches a core file.

---

## 8. U13 addendum — `batch`, the canonical body and batch persistence

Everything below is exported from the four `src/` files on U13's row. Import paths are `src/…`.

```ts
// src/services/canonical-body.ts — the Python number model (round 2), then the body
export class PyInt { readonly value: bigint }      // an exact CPython int, at any width
export class PyFloat { readonly value: number }    // a CPython float, which always keeps its `.0`
export type PyNumber = PyInt | PyFloat
export type PyValue = null | boolean | number | string | PyNumber | readonly PyValue[] | PyObject
export interface PyObject { readonly [key: string]: PyValue }   // every JsonObject is one
export const pyInt: (value: bigint) => PyInt
export const pyFloat: (value: number) => PyFloat
export const isPyMapping: (value: PyValue) => value is PyObject
export const pyFloatRepr: (value: number) => string             // repr(float): 1e+16, 1e-05, 3.0
export const pyDumps: (value: PyValue, ensureAscii: boolean) => string
                                          // json.dumps(sort_keys=True, separators=(",",":"))
export const pyScalar: (value: PyValue) => string               // _scalar, float branch through %g
export const parsePython: (text: string) => PyParse             // json.loads, int/float kept apart
export interface PyParse { value: PyValue; duplicateKey: boolean; failed: 'too_deep' | 'invalid' | null }
                       // `value` is the hookless last-wins answer; `duplicateKey` is the hook's verdict
export const pyJsonValue: (value: PyValue, label: string) => Effect<PyValue, PlayerError>   // _json_value
export const pyJsonObject: (value: PyValue, label: string) => Effect<PyObject, PlayerError>
export const canonicalText: (value: PyValue) => Effect<string, PlayerError>   // _canonical_body().decode()
export const canonicalBody: (value: PyValue) => Effect<Uint8Array, PlayerError>
export const canonicalBytes: (text: string) => Uint8Array
export const NON_FINITE_DETAIL: string

// src/services/batch.ts
export const pageLimit: (value: string | null | undefined, fallback?: number) => Effect<number, PlayerError>
export const parseJsonObject: (value: string, label: string) => Effect<PyObject, PlayerError>
export const batchIntent: (state: V2ClientState, batchId: string) => string        // pure
export const batchDisposition: (session, batchId, disposition: string, parts?: DispositionParts)
  => Effect<BatchDisposition, PlayerError | DriftError>
export const batchErrorDisposition: (response: JsonResponse, session, batchId)
  => Effect<BatchDisposition, PlayerError | DriftError>
export const submitBatch: (sessionPath, session, batchId, options?: SubmitOptions)
  => Effect<BatchSubmission, PlayerError | DriftError | LockTimeoutError, SessionStore | PrivateFs | V2Client>
export const dispositionReceiptState: (d: BatchDisposition) => string | null
export const inertReceiptMirror: ReceiptMirror
export interface BatchSubmission { disposition: BatchDisposition; warning: string | null; exitCode: 0 | 2 }
export interface DispositionParts { receipt?: unknown; error?: unknown }
export type ReceiptMirror = (receipt: CommandReceipt, command: string) => Effect<void>

// src/services/batch-persist.ts
export const persistBatchForAction: (sessionPath, session, actionId, args: PyObject, options?: PersistOptions)
  => Effect<string, PlayerError | LockTimeoutError, SessionStore | PrivateFs>
export const batchToken: () => string                    // secrets.token_urlsafe(24)
export interface PersistOptions { token?: () => string }  // pin the ID in a test

// src/commands/batch.cmd.ts
export const batchCommand: Command                        // integrator: swap the cli-main stub
export const batchCommandWith: (hooks: BatchHooks) => Command
export const runBatch: (args: BatchArgs, hooks?: BatchHooks) => Effect<void, BatchError, BatchEnv>
export const liveBatchHooks: BatchHooks
export type BatchEnv = SessionStore | PrivateFs | V2Client
export type BatchError = PlayerError | DriftError | LockTimeoutError | SessionMissingError | ExitCodeSignal
```

**PORT_MAP §1's sketch** froze `submitBatch: (ctx, commands)`. The landed signature follows the
Python instead: `_submit_persisted_batch(path, session, batch_id)` and
`_persist_batch_for_action(path, session, action_id, arguments)` are what U12 (client.py:6790),
U14 (9896), U16 (9550) and U18 (11101, 11133) actually call, and every one of them persists
first and submits by ID. `batchIntent(state, batchId)` is unchanged from §1.

**Interface change (round 2, append per §3.1):** `parseJsonObject` and `persistBatchForAction`
now speak `PyObject` rather than `JsonObject`, and `canonicalText`/`canonicalBody` take
`PyValue`. **This is source-compatible for every existing caller** — every `JsonObject` *is* a
`PyObject` and every `JsonValue` *is* a `PyValue`, so U12/U16/U18 pass their argument objects
unchanged. What it buys is the one thing a `JsonValue` cannot carry: whether the agent wrote
`40` or `40.0`, `1e16` or `10000000000000000`, and whether a 20-digit integer is exact. Those
bytes are the batch's idempotency key on the wire and its record in `.v2-state`
(NOTES §17.9). U06's `src/services/health-json.ts` imports the same model for `health --json`
(§5.2).

**Ownership change (append per §3.1):** `_limit` (client.py:6513-6518) is inside U13's assigned
Python span but has no U13 caller — `state` (U10) and `legal` (U11) are its only two. It lands
as `pageLimit` in `src/services/batch.ts`, the only file on U13's row that could hold it.
**U10 and U11 import it; neither reimplements it.** (NOTES §17.4.)

**Integrator checklist:**

1. Swap `cli-main`'s `batchCommand` stub for `batchCommand` from `src/commands/batch.cmd.ts`.
   It adds the `--action_id` spelling alongside `--action-id` via `dualText`, because the
   justfile recipe `batch session action_id …` is the surface agents type.
2. ~~When **U11** lands, pass its `drainLegal`-backed `LegalPageFetcher` as `hooks.fetchLegal`~~
   **Done (integration, inert-seam round):** `runBatch` builds `legalPageFetcher(client)` when
   `hooks.fetchLegal` is absent — `LegalPageFetcher` is frozen with `V2Client` outside its
   requirements (§6), so the client has to be bound where the Layer stack provides it rather
   than inside the hook value. **`undefined` now means "the live drain", not "no refresh"**;
   `--no-refresh` is the only way to ask for the plain refusal. Until this landed, *every*
   `batch` behaved as though `--no-refresh` had been given (NOTES §I.5.4).
3. ~~When **U14** lands, pass `renderDisposition` and `orderReceiptOk` …~~ **Done (round 2):**
   `liveBatchHooks` imports `renderDisposition` from `src/render/receipt.ts` and
   `orderReceiptOk` from `src/services/disposition.ts`; the three `fallback*` copies are
   deleted and `batch.cmd.ts` exports none of them (NOTES §17.2).
4. ~~When **U12**/**U16** land, pass `nextFocusLine` and `refusedActorOptions`~~ **Done
   (integration, inert-seam round):** `liveBatchHooks` binds `nextFocusLine` (U12),
   `refusedActorOptions` (U16, over U11's `legalRows`) and `orderActor` (U15, over U11's
   `compactLegalAction` — so the hook yields an `Effect`, since the projection can fail).
   "Each costs one guidance line, never an outcome" was wrong twice over: with
   `orderActor: () => ''` the actor was always empty, so `_next_focus_line` never printed *and*
   `_refused_actor_options` was always empty — and the latter is up to
   `V2_REFUSAL_LEGAL_ROWS` of what the actor can actually do, printed under a refusal, which is
   the difference between one more call and two (NOTES §I.5.4).
6. **New, this round.** `BatchHooks.mirrorReceipt` is **deleted**, not re-pointed:
   `submitBatch` now calls U07's `mirrorReceipt` by default and `SubmitOptions.mirrorReceipt`
   is the test override. A seam whose only live value is the default is a way to forget the
   default — which is exactly how `_mirror_receipt` ended up called from nowhere in the whole
   CLI while CPython calls it at four sites (NOTES §I.5.3).
5. ~~When **U18** lands, replace `phaseAwareRefusal` with U18's copy.~~ **Done (round 2):**
   `batch.cmd.ts` imports `phaseAwareRefusal` from `src/services/pregame` and exports none of
   its own. `runBatch` returns its exit code out of the wrapped block, so the quiet
   `ExitCodeSignal` of a reported disposition never reaches a refusal prefixer (NOTES §17.3).

Nothing in U13 touches a core file.

---

## Addendum — U15 orders engine

Everything below is exported from `src/services/orders/index.ts`. **Import from
`src/services/orders`, not from its files.** Import paths are `src/…`.

```ts
// The two U11 seams (U11 supplies both; see NOTES §18.1)
export interface OrdersDeps {
  readonly compactLegalAction: (d: JsonObject) => Effect<JsonObject, PlayerError>
}
export interface OrdersFetchDeps extends OrdersDeps {
  readonly drainLegal: LegalPageFetcher            // U03's type, verbatim
}
export type LegalPageReader = (sessionPath, session, query, { cursor, actorId, targetId })
  => Effect<LegalActionPageEnvelope, PlayerError, SessionStore | PrivateFs>

// The three names §1 froze for U16
export const parseOrders: (text: unknown) => Effect<ReadonlyArray<string>, PlayerError>
export const resolveOrders: (deps, state, sessionPath, orders)
  => Effect<ReadonlyArray<ResolvedOrder>, PlayerError, SessionStore | PrivateFs>
export const unresolvedReport: (sessionPath, state, outcomes: ReadonlyArray<OrderOutcome>)
  => Effect<ReadonlyArray<string>, PlayerError, SessionStore | PrivateFs>
  // §1 sketched `(u: OrderUnresolved)`; the Python takes the whole outcome list plus the
  // path.  Return type is §1's.  `resolveOrders` joins the lines into the PlayerError
  // CPython raised, so the printed bytes are unchanged.  See NOTES §18.3.

// The rest of `do`'s flow
export const resolveOrdersFetching: (deps: OrdersFetchDeps, sessionPath, session, state, orders, notes?)
  => Effect<OrderOutcomes, PlayerError | LockTimeoutError, SessionStore | PrivateFs>
export const refreshStaleOrderAliases: (deps: OrdersFetchDeps, sessionPath, session, state, orders, notes?)
  => Effect<RefreshedOrderAliases, RefreshError, SessionStore | PrivateFs>
export const refreshOrders: (deps: OrdersFetchDeps, sessionPath, session, pending)
  => Effect<void, PlayerError | LockTimeoutError, SessionStore | PrivateFs>
export const rebindOrder: (deps, state, resolved) => Effect<ResolvedOrder | null, PlayerError>
export const drainLegalUnlocked: (read: LegalPageReader, sessionPath, session, actorId?)
  => Effect<Revision | null, PlayerError, SessionStore | PrivateFs>
export const resolveOrder / orderOutcomes / orderFetchTargets / orderEnumerationCommand

// The matcher's parts — U12's `_phase_end_locked` and U18's `command_start` need
// `defaultArguments`; U11 needs `LEGAL_SUBJECT_RESERVED`.
export const defaultArguments: (compact: JsonObject) => JsonObject | null
export const LEGAL_SUBJECT_RESERVED: ReadonlySet<string>       // client.py:3590-3593
export const orderPool / orderActor / orderOperation / orderVerbs / orderTargetKeys
export const orderDiscriminators / orderResolution / compactText / kindHead / kindTail
export const orderValue / orderProperties / orderArrayBounds / isArrayProperty / poolIsListed
export const orderArguments / orderMatch / namedTargetId / orderElementResolver / ORDER_BAD
export const V2_MAX_ORDERS / V2_MAX_ORDER_WORDS / V2_ACTION_FAMILIES / V2_TIER1_VERBS
export const ORDER_COORDINATE_RE / casefold / pySplit / pyRepr
export const OrderUnresolved / orderUnresolved                 // tagged error, caught internally

export interface ResolvedOrder {
  order; action_id; kind; operation; label; actor_id; target_key; arguments: JsonObject
}
export interface OrderOutcome { text; resolved: ResolvedOrder | null; reason; actor }
export interface OrderOutcomes { resolved: ReadonlyArray<ResolvedOrder>; state; notes }
```

**Ownership change (append per §3.1):** `_LEGAL_SUBJECT_RESERVED` (client.py:3590-3593) sits one
line above U11's span and `_order_discriminators` is its only caller here, so it lands in
`src/services/orders/match.ts` as `LEGAL_SUBJECT_RESERVED`. **U11 imports it; U11 does not
reimplement it.** `V2_MAX_ORDERS`, `V2_MAX_ORDER_WORDS`, `V2_ACTION_FAMILIES`,
`ORDER_COORDINATE_RE` and `V2_TIER1_VERBS` (client.py:8633-8676) are likewise outside every line
range core's `src/constants.ts` covers, so they live in `src/services/orders/parse.ts`.

**Integrator:** U15 owns no command and touches no core file. `cli-main` needs no change.

---

## Addendum — U17 `monitor`

Everything below is exported from the five source files on U17's row. Import paths are `src/…`.
No file outside the row was created or edited.

```ts
// src/services/monitor-lock.ts
export const withMonitorLock: <A,E,R>(sessionPath, holder: JsonObject,
  body: (running: JsonObject | null) => Effect<A,E,R>) => Effect<A, E | PlayerError, R | PrivateFs>
  // `running` is the recorded holder when the lock is taken, null when we own it
export const monitorHolder: (sessionPath) => Effect<JsonObject | null, never, PrivateFs>
export const readMonitorHolder: (descriptor: number) => JsonObject
export { monitorLockPath }                       // re-export of core's locks.ts

// src/services/monitor-hook.ts
export const V2_MONITOR_FORBIDDEN_VERBS / V2_MONITOR_MAX_EXIT_CODE / MONITOR_HOOK_VARIABLES
export const monitorExecRefusal: (command: string) => string        // "" when allowed
export const monitorExecRefusalMessage: (verb: string) => string
export const monitorHookEnvironment: (wait: WaitEnvelope, base?) => Record<string,string>
export type HookOutcome = { _tag:'exited'; status:number } | { _tag:'unstarted'; message:string }
export type HookRunner = (command, environment) => Effect<HookOutcome>
export const shellHookRunner: HookRunner
export const runMonitorHook: (sessionPath, command, wait, runner?, base?) => Effect<void, never, PrivateFs>

// src/services/monitor-loop.ts
export const V2_MONITOR_BACKOFF_START_S / V2_MONITOR_BACKOFF_MAX_S
export const announcedTuple: (health: HealthEnvelope) => ReadonlyArray<number> | null
export const missedPhase: (health, announced: JsonValue | undefined) => JsonObject | null
export type WaitUntilTurnFn = (args: WaitArgs, options: WaitUntilTurnOptions) => Effect<WaitEnvelope, PlayError, V2Client | SessionStore>
export interface MonitorSeams { waitUntilTurn; runHook; clock }     // the three the tests script
export const liveMonitorSeams: (sessionPath, session, clock?) => MonitorSeams
export const monitorLoop: (sessionPath, seams, options: MonitorLoopOptions)
  => Effect<number, PlayerError, PrivateFs | SessionStore | V2Client>

// src/render/monitor.ts
export const monitorAnnounceLine / monitorTerminalLine: (wait: WaitEnvelope) => string
export const missedLine: (event: JsonObject, consecutive: number, since: JsonValue | undefined) => string
export const watchingText / whoseText: (marker: JsonObject | null) => string
export const monitorRunningLine / monitorAlreadyRunningLine: (holder, marker) => string
export const monitorStoppedLine: (pid: number) => string
export const pythonTruthy: (value: JsonValue | undefined) => boolean
export const MONITOR_NOT_RUNNING / MONITOR_STATUS_IDLE / MONITOR_REBOUND_LINE / MONITOR_MAX_S_LINE

// src/commands/monitor.cmd.ts
export const monitorCommand: Command                 // integrator: swap the cli-main stub
export const monitorCommandWith: (harness: MonitorHarness) => Command
export const commandMonitor: (options: MonitorOptions, harness?, environment?)
  => Effect<number, PlayerError | SessionMissingError, PrivateFs | SessionStore | V2Client>
export const monitorStatus: (sessionPath) => Effect<number, never, PrivateFs>
export const monitorStop: (sessionPath, harness?) => Effect<number, PlayerError, PrivateFs>
export const liveMonitorHarness / systemKill / holderSince / monitorDefaults
export const MONITOR_STOP_TIMEOUT_S
export type KillOutcome = 'signalled' | 'gone' | 'forbidden'
export interface MonitorHarness { seams; kill; clock; pid; since }
export interface MonitorOptions { session; waitS; pollS; once; stop; status; exec; exitCode; maxS; json }
```

`monitorLoop` returns a **plain number**, not an `ExitCodeSignal`: `--exit-code` may name any
value in `[0, 255]` and core's `ExitCode` is `0 | 2 | 75 | 66`. `monitorCommandWith` is the only
place that narrows it, and NOTES §18.2 records what that costs and how to fix it in core.

**Integrator:** `monitorCommand` is ready to replace `cli-main`'s `monitor` stub — the flag
surface is identical to the stub's plus the descriptions from argparse. Two core-side follow-ups
are recorded in NOTES §18.1 (export the bound `flock` from `src/services/locks.ts` so U17's copy
can go) and §18.2 (widen the exit-status channel so `--exit-code 42` exits 42). Neither blocks
the swap.

---

## Addendum — U09 `show` and the yields overlay

Everything below is exported from the three files on U09's row. Import paths are `src/…`.

```ts
// src/commands/show.cmd.ts
export const showCommand: Command                       // integrator: swap the cli-main stub
export const runShow: (options: ShowOptions)
  => Effect<void, PlayerError | SessionMissingError | LockTimeoutError, SessionStore | PrivateFs>
export interface ShowOptions {
  session; name; grep; regex; yields; json
  clock?: () => number        // monotonic seconds, so the --regex budget is testable
}

// src/render/show.ts
export const V2_SHOW_FILES: ReadonlyMap<string, ReadonlyArray<string>>   // insertion order is byte surface
export const V2_SHOW_ROW_FILES / V2_SHOW_MAX_MATCHES / SHOW_NAME_RE
export const NESTED_QUANTIFIER_RE / V2_SHOW_GREP_BUDGET_S
export const showOptionFiles / showCatalog / showPresent: (sessionPath) => Effect<…, never, PrivateFs>
export const showEmpty:      (sessionPath) => Effect<never, PlayerError>
export const showSources:    (sessionPath, name, pattern) => Effect<ReadonlyArray<ReadonlyArray<string>>, never, PrivateFs>
export const showStaleness:  (sessionPath, session, sources) => Effect<string, never, SessionStore | PrivateFs>
export const showDefault / showNamed: (sessionPath[, name]) => Effect<ReadonlyArray<string>, PlayerError, PrivateFs>
export const showRows:       (sessionPath, alias) => Effect<ReadonlyArray<string>, never, PrivateFs>
export const showGrep:       (sessionPath, pattern, ShowGrepOptions) => Effect<ReadonlyArray<string>, PlayerError, PrivateFs>
export const pyRepr:         (text: string) => string     // CPython `repr()` for the echoed pattern
export interface ShowCatalogEntry { label; parts }
export interface ShowPresentFile extends ShowCatalogEntry { text }
export interface ShowGrepOptions { regex: boolean; clock?: () => number }

// src/render/mirror/yields-overlay.ts
export const renderMapYields: (mirrorDir: string) => Effect<ReadonlyArray<string>, never, PrivateFs>
export const YIELD_TILE_RE / YIELD_WINDOW_MAX / YIELD_NONE_NOTE / YIELD_CELL_NOTE
```

`renderMapYields` takes the **mirror directory**, not the session path — it is
`state_mirror.render_map_yields(session_dir)`, whose two inputs are `state/map.txt` and
`state/yields.tsv` and whose caller resolves `mirrorDir` first. An empty array means "no grid at
all"; the caller turns that into the "no map projection yet" refusal, because an empty list and
an empty overlay are different answers.

U09 imports and does not restate: `mirrorDir` / `mirrorText` / `parseTable` / `parseMap` /
`readMirror` / `revLine` / `tileKey` / `splitLines` / `isBehind` / `staleLine` / `STATE_DIR` /
`OPTIONS_DIR` / `MAP_FILE` / `YIELD_FILE` / `YIELD_COLUMNS` (U04), `ENTITY_ALIAS_RE` (core
constants), `PrivateFs.openDirectory` (core). `_mirror_table` (U12's row) is **not** imported:
in the port it is one call to two U04 primitives, so U09 composes them rather than taking a
wave-2 dependency. See NOTES §19.5.

**Integrator:** swap `cli-main`'s `showCommand` stub for `showCommand` from
`src/commands/show.cmd.ts`. It adds the `NAME` positional the stub deliberately left out
(`Args.text({ name: 'name' })`, optional). Nothing in U09 touches a core file.

---

## Addendum — U14 receipts, retry and refusal rendering

Everything below is exported from the six files on U14's row. Import paths are `src/…`.

```ts
// src/render/refusal.ts
export const errorText: (error: StructuredError) => string          // _error_text
export const ERROR_REMEDIES: ReadonlyMap<string, string>            // _ERROR_REMEDIES
export const restartCommand: (value: unknown) => string             // _restart_command
export const retryAfterText: (details: JsonObject) => string        // _retry_after_text
export const renderErrorPayload: (payload: unknown) => ReadonlyArray<string>
export const refusalRender: RefusalRenderApi
export const RefusalRenderLive: Layer<RefusalRender>                // §4.6's promised Layer

// src/render/receipt.ts
export const receiptLine: (r: CommandReceipt, intent: string) => string
export const observationLines: (o: CityInvestigationObservation) => ReadonlyArray<string>
export const renderReceipt: (r: CommandReceipt, intent: string) => ReadonlyArray<string>
export const renderDisposition: (d: BatchDisposition, intent: string) => ReadonlyArray<string>

// src/services/disposition.ts
export const orderReceiptOk: (d: BatchDisposition) => boolean       // _order_receipt_ok
export const isTerminalReceipt: (s: ReceiptState) => boolean
export const isPollableReceipt: (s: ReceiptState) => boolean
export const mayResubmitCachedReceipt: (s: ReceiptState) => false

// src/services/receipts.ts
export const RECEIPT_TIMEOUT_S / RETRY_POLL_DEADLINE_S / RETRY_POLL_INTERVAL_S
export const AMBIGUOUS_IS_TERMINAL / ACCEPTED_RECEIPT_VANISHED / AMBIGUOUS_REPLAY_UNSAFE
export const noPersistedBatch: (batchId: string) => PlayerError
export const getReceiptResponse: (s: Session, batchId) => Effect<JsonResponse, PlayerError, V2Client>
export const missingAcceptedReceipt: (s, cached: CommandReceipt, batchId)
  => Effect<CommandReceipt, DriftError>                             // _missing_accepted_receipt
export interface RetryClock { monotonic; sleep }                    // injectable, like U05's
export const systemRetryClock: RetryClock
export interface ReceiptHooks { batchIntent; rememberReceipt; mirrorReceipt; submitPersistedBatch }
export type ReceiptHooksFor = (sessionPath, session) => Effect<ReceiptHooks, never, PrivateFs | SessionStore | V2Client>
export const liveReceiptHooks: ReceiptHooksFor
export type SubmitOutcome = BatchSubmission                         // U13's type, re-aliased

// src/commands/receipt.cmd.ts
export const runReceipt: (o: ReceiptOptions, makeHooks?) => Effect<void, ReceiptError, SessionStore | V2Client | PrivateFs>
export const warnIfAmbiguous: (r: CommandReceipt) => Effect<void>   // stderr, never stdout
export const receiptCommandWith: (makeHooks: ReceiptHooksFor) => Command
export const receiptCommand: Command

// src/commands/retry.cmd.ts
export const retryLocked: (path, session, o: RetryOptions, makeHooks?, clock?) => Effect<number, RetryError, …>
export const runRetry: (o: RetryOptions, makeHooks?, clock?) => Effect<void, RetryError | ExitCodeSignal, …>
export const retryCommandWith: (makeHooks: ReceiptHooksFor) => Command
export const retryCommand: Command
```

**New test-only file (per §3.1):** `test/receipt-harness.ts` — the wire builders, scratch
workspace and stdout/stderr capture the unit's three suites share. It is **not** a `*.test.ts`
file, so `bun test` never collects it; it lives beside the suites rather than in
`test/_fixtures/**` (core's row) so nothing outside U14 depends on it.

**Integrator:**
1. Swap `RefusalRenderDefault` for `RefusalRenderLive` (`src/render/refusal`) in `cli-main`'s
   `AppLayer`. That is what turns the top-level `V2ResponseError` path from a one-line
   `code: message` into the full remedy-first refusal body.
2. Swap `cli-main`'s `receipt` and `retry` stubs for `receiptCommand` / `retryCommand`.
3. ~~U13's NOTES §17.2 fallbacks … can now be deleted …~~ **Done (U13 round 2):**
   `src/commands/batch.cmd.ts` imports `renderDisposition` (`src/render/receipt`) and
   `orderReceiptOk` (`src/services/disposition`) into `liveBatchHooks`, and exports no
   renderer of its own.

Nothing in U14 edits a core file.

---

## Addendum — U11 `legal`, the legal renderers and the catalog drain

Everything below is exported from the nine files on U11's row. Import paths are `src/…`.

```ts
// src/services/legal-compact.ts
export type CompactAction = JsonObject                       // _compact_legal_action's output
export interface LegalCompactResult { schema_version; command; kind; state_revision;
  catalog_total; pages_read; matched; offset; limit; shown; truncated; has_more;
  next_offset; byte_limited; oversized_single; hidden_kinds; actions }
export const compactLegalAction: (d: JsonObject) => Effect<CompactAction, PlayerError>
export const descriptorToJson:   (d: LegalActionDescriptor) => JsonObject
export const compactActionBytes: (c: CompactAction) => number          // the byte budget
export const compactLegalOffset: (v: string | null | undefined) => Effect<number, PlayerError>
export const compactLegalLimit:  (v, default?) => Effect<number, PlayerError>
export const OFFSET_REFUSAL / COMPACT_LIMIT_REFUSAL

// src/services/legal-query.ts
export interface LegalQuery { query; cursor; actorId; targetId }
export interface LegalCtx {
  sessionPath; session
  mirrorPage?: (page, label) => Effect<void, never, PrivateFs>     // U07's seam, inert today
  gate?: <A,E,R>(body: Effect<A,E,R>) => Effect<A,E,R>             // the concurrency seam
}
export type LegalError = PlayerError | V2ResponseError | DriftError | LockTimeoutError
export const legalQuery:    (args: LegalQueryArgs, o?: { ignoreLimit?: boolean }) => Effect<string, PlayerError>
export const readLegalPage: (ctx: LegalCtx, q: LegalQuery)
  => Effect<LegalActionPageEnvelope, LegalError, SessionStore | PrivateFs | V2Client>
export const pageLimit: (v: string | null | undefined, default?) => Effect<number, PlayerError>
export const PAGE_LIMIT_REFUSAL / LEGAL_TIMEOUT_S

// src/services/legal-drain.ts
export const LEGAL_DRAIN_CONCURRENCY = 4
export interface DrainedCatalog { revision: Revision | null; actions: ReadonlyArray<CompactAction> }
export const drainLegal:        (ctx, actorId?) => Effect<DrainedCatalog, LegalError, …>
export const drainLegalActors:  (ctx, actorIds) => Effect<ReadonlyArray<DrainedCatalog>, …>
export const legalPageFetcher:  (client: V2ClientApi) => LegalPageFetcher     // U03's callback
export const drainLegalAll:     (ctx, args: DrainAllArgs, kind: string) => Effect<DrainAllOutcome, …>
export const kindMatchedNothing / unknownKind / playerScopeAlias
export const V2_PLAYER_SCOPED_KIND_PREFIXES / V2_RELATION_SCOPED_KIND_PREFIX

// src/render/legal/rows.ts
export const LEGAL_SUBJECT_RESERVED: ReadonlySet<string>
export const legalRow:  (alias, compact, scope, aliases?) => Effect<ReadonlyArray<string>, PlayerError>
export const legalRows: (compacts, scope, aliases?) => Effect<ReadonlyArray<string>, PlayerError>
export const legalRowIsDefault: (compact) => Effect<boolean, PlayerError>
export const kindWithOperation: (kind: string, operation: JsonValue) => string

// src/render/legal/kinds.ts
export const actionKindKey:      (compact) => Effect<string, PlayerError>
export const descriptorKindKey:  (descriptor: JsonObject) => string            // total
export const kindSelectorMatches:(descriptor: JsonObject, selector: string) => boolean
export const hiddenKindLines:    (result, scope, aliases) => ReadonlyArray<string>

// src/render/legal/grouped.ts
export const catalogChoiceLine / groupedLegalLines

// src/render/legal/equivalence.ts
export const aliasSpan:        (aliases: ReadonlyArray<string>) => string
export const equivalenceLines: (result, scope, aliases, equivalence) => Effect<…, PlayerError>

// src/render/legal/page.ts
export const catalogRenderDeps: CatalogRenderDeps    // the live bundle U03's catalog-cache takes
export const renderLegalPage:    (value, aliases?, o?: { full?: boolean }) => Effect<…, PlayerError>
export const renderLegalCompact: (result, scope, aliases?, equivalence?, o?) => Effect<…, PlayerError>

// src/commands/legal.cmd.ts
export const legalCommand: Command                   // integrator: swap the cli-main stub
export const runLegal: (options: LegalOptions) => Effect<void, …, SessionStore | PrivateFs | V2Client>
```

**Ownership changes (per §3.1):**

1. `_limit` (client.py:6513-6518) was on no row. It lands as `pageLimit` in
   `src/services/legal-query.ts`. **U10 imports it; U10 does not reimplement it.**
2. `V2_PLAYER_SCOPED_KIND_PREFIXES` / `V2_RELATION_SCOPED_KIND_PREFIX` (client.py:8016-8030)
   are outside `src/constants.ts`'s scoped ranges and land in `src/services/legal-drain.ts`.
3. `_drain_legal_unlocked` (client.py:9358-9387) is listed on **U15's** row, but U03's
   `LegalPageFetcher` and U16's per-actor drains both need it and it is the same loop as
   `_command_legal_all`. It lands here as `drainLegal`. **U15 imports it.**
4. `_phase_aware_refusal` (U18's row) is copied privately into `src/commands/legal.cmd.ts`
   until U18 lands; see NOTES.md §U11.3.

**Integrator:** swap `cli-main`'s `legalCommand` stub for the one in
`src/commands/legal.cmd.ts` — the flag surface is identical to the stub's, `dualText('actor-id')`
and `dualText('target-id')` included. Nothing in U11 touches a core file. If the
`LegalPageFetcher` stdout divergence in NOTES.md §U11.4 matters, widen U03's type in
`src/services/alias-refresh.ts` to `Effect<void, PlayerError | LockTimeoutError | V2ResponseError | DriftError, SessionStore | PrivateFs | V2Client>`
and drop `legalPageFetcher`'s error mapping.

---

## Addendum — U12 `turn`

`src/render/turn.ts` is the home of **`V2_ONE_CALL_END`** (client.py:7406); U16's `do.cmd.ts`
carries a temporary private copy that the integrator deletes in favour of this import.
`V2_MAX_ORDERS` is **U15's** (`src/services/orders`) and `src/render/decisions.ts` imports it.
The four mirror projections `_mirror_table` reads resolve through U04's `SECTION_TARGETS`
rather than through a second copy of U09's `V2_SHOW_FILES`.

U12's exported surface, as landed. §1 promised `renderTurn` as a pure function; it returns an
`Effect` because `_render_turn` can drift (NOTES §U12.2). Everything else keeps §1's shape.

```ts
// src/render/turn.ts
export const renderTurn: (result: TurnResult, deps: RenderTurnDeps, options?: RenderTurnOptions)
  => Effect<ReadonlyArray<string>, PlayerError>
export const unitStatus / briefingUnitLines / briefingNeedsDecision
export const researchableNames / briefingTruncation / isTurnReady
export const V2_ONE_CALL_END: string
export type TurnResult / TurnReadyResult / TurnStatusResult / TurnCompactPage / RenderTurnDeps

// src/render/decisions.ts
export const decisionLine: (row: DecisionRow) => string
export const batchFocusCommand: (rows: ReadonlyArray<DecisionRow>) => string
export const V2_DECISION_ROW_MAX / V2_FOCUS_LINE_MAX
export type DecisionRow

// src/services/turn-pages.ts
export const turnNextCommands / turnCompactPage / turnPage / turnHealth
export const mirrorTable / mirrorIsFresh / mirrorCell / mirrorNumber / mirrorFile
export const mirrorEventCount / briefingEventsLine
export type TurnHooks                    // rememberPage, mirrorPage, fetchStateSection

// src/services/decisions.ts
export const decisionRows / decisionActorRow / decisionUnitRows / decisionCityRows
export const decisionMeetingRows / decisionOptions / decisionOrder / decisionVerb
export const decisionOptionRank / tier1Word / nextFocusLine / briefingDecisionLines
export const liveDecisionDeps: DecisionDeps          // bound to U11 + U15
export type DecisionDeps

// src/services/meetings.ts
export const meetingRemedy / openMeetings / meetingGroups / V2_DIPLOMACY_READ

// src/services/composite-json.ts
export const compositeJson: (command: string, parts: Record<string, CompositePart>) => CompositeJson

// src/services/turn-end.ts   — the four names U16 consumes
export const phaseEnd: (ctx: TurnCtx) => Effect<PhaseEndResult, PlayError, …>
export const awaitAndBrief: (ctx: TurnCtx, options: AwaitAndBriefOptions) => Effect<AwaitAndBriefResult, …>
export const turnBriefingLocked: (ctx: TurnCtx) => Effect<TurnBriefing, PlayError, …>
export const commandTurnEnd: (ctx: TurnCtx, options: TurnEndOptions) => Effect<TurnEndOutcome, …>
export const cachedKindAction / resolveKindAction / turnEndJson
export type TurnCtx / PhaseEndDeps / SubmitOutcome

// src/commands/turn.cmd.ts
export const turnCommand: Command                    // integrator: swap the cli-main stub
export const turnCommandWith: (makeSeams: TurnSeamsFor) => Command
export const liveTurnSeams / liveRenderTurnDeps / turnCtx
export const runTurn / commandTurnDecisions / emitTurn / checkTurnFlags
export const AWAIT_WITHOUT_END / BRIEF_WITHOUT_WAKE / DECISIONS_WITH_END
```

**Integrator:** swap `cli-main`'s `turnCommand` stub for `turnCommand` from
`src/commands/turn.cmd.ts`; nothing in U12 touches a core file. Then delete `do.cmd.ts`'s
private `V2_ONE_CALL_END`.

**Round 2 (done, no longer an integrator task):** `TurnHooks.mirrorPage`, the
`StartHooks.mirrorPage` handed to `fetchStateSection`, and `WaitHooks.mirrorPage` are all bound
to U04/U07's `mirrorPage`, and `WaitHooks.mirrorHealth` now passes `commands: V2_PROTOCOL_CARD`
the way `_mirror_health` (client.py:3062-3072) always does. `liveTurnSeams` imports the page
bridge from `src/services/mirror/update-page` because U04's `src/services/mirror/index.ts`
barrel does not re-export `mirrorPage`/`updateFromPage`/`UpdatePageOptions`; **U04 should add
them, after which U05, U11 and U12 can all switch to the barrel in one change.** See NOTES
§U12.3 and §U12.11.

---

## 8. U16 addendum — `do`, the drain gate and the receipt ledger

### 8.1 One new file, appended per §3.1: `test/_do-harness.ts`

`test/_fixtures/**` is core's row and U16 does not edit it, but three test files
(`do.test.ts`, `do-end.test.ts`, `do-concurrency.test.ts`) drive the same bench, and a
bench copied three times is how three copies drift. The bench therefore lives in
`test/_do-harness.ts` — U16's row, leading underscore so `bun test`'s `*.test.ts` glob
does not collect it as a suite. It exports `bench()`, `doArgs()`, `runDoCaptured()`, the
`World` script record, and the `FakeAction` builders. **No other unit imports it.**

Round 2 added three things to it, all for regressions the round-1 bench could not express:
`actorless()` / `phaseEndAction()` build a descriptor with no `subject.actor` — the
`actor_id == ""` family, NOTES §20.8 — and `bench(options: BenchOptions)` takes a
`files: (api: PrivateFsApi) => PrivateFsApi` transform so a file layer that *throws* rather
than fails can be injected (NOTES §20.10). The transform is applied to `sessionStoreFor`'s
file api as well as to the layer, because the store re-provides its own `PrivateFs` inside
the request lock.

### 8.2 What U16 exports

```ts
// src/commands/do.cmd.ts
export const runDo: (args: DoArguments, makeHooks: DoHooksFor)
  => Effect<void, PlayError | ExitCodeSignal, PrivateFs | SessionStore | V2Client>
export const doCommandWith: (makeHooks: DoHooksFor) => Command
export const doCommand: Command            // integrator: swap the cli-main stub
export const liveDoHooks: DoHooksFor       // every hook bound to its landed unit
export const doPhaseEnd / ordersText
export const ORDERS_TWICE / AWAIT_WITHOUT_END / BRIEF_WITHOUT_WAKE
export interface DoHooks / DoArguments / SubmitOutcome / PhaseEndOutcome
export interface AwaitBriefOutcome / RefreshedAliases / DoRecord / DoPhaseEndResult

// src/services/do-drain.ts
export const DO_DRAIN_CONCURRENCY = 4
export type DrainGate = <A,E,R>(body: Effect<A,E,R>) => Effect<A,E,R>
export const openGate: DrainGate
export const drainActors: <R>(actors, drainOne: (actorId, gate) => Effect<Revision|null, PlayError, R>, options?)
  => Effect<ReadonlyArray<DrainOutcome>, never, R>
export const firstDrainFailure / distinctActors / actorAliases / fetchedOptionsNote
export interface DrainOutcome { actorId; revision; failure; skipped }
export interface DrainOptions { concurrency?; stopOnFailure? }   // stopOnFailure defaults true
// distinctActors dedupes in first-seen order and KEEPS "" — it is
// `_refresh_orders`' `drained` set, whose empty id is a global drain (NOTES §20.8).
// stopOnFailure reproduces the sequential loop that RAISES: after the drain at
// index i fails, every higher index commits nothing and reports `skipped`.
// `_refused_actor_options` is the one `except … : continue` loop, so
// src/render/actor-options.ts is the only caller passing false (NOTES §20.13).

// src/services/receipt-ledger.ts
export const RECEIPT_LEDGER_FILE / LEDGER_SCHEMA_VERSION / MAX_LEDGER_LINE / MAX_LEDGER_ORDER
export const ledgerEntry / ledgerLine / appendLedgerLine / appendLedgerEntry
export const recordReceipt: (sessionPath, entry, at?) => Effect<void, never, PrivateFs>
export const readLedger / readSessionLedger

// src/render/actor-options.ts
export const V2_REFUSAL_LEGAL_ROWS = 12 / V2_REFUSAL_LEGAL_ACTORS = 3
export const actorNeedsDrain / actorOptionsSection / refusedActorOptions
export interface ActorOptionsDeps / RefusedActorOptionsIo
```

### 8.3 `DrainGate` is the same shape as U11's `LegalCtx.gate`

Deliberately. `drainActors` builds one gate per actor and `liveDoHooks` passes it straight
into `drainLegal({ …, gate })`, so the fetches overlap and the `.v2-state` commits stay
single-file **and in the caller's order**. U11's own `drainLegalActors` uses a one-permit
semaphore for the same critical section; that serializes but does not order, which is
enough for `legal --all` (one actor) and not enough for `do` (see NOTES §20.2).

**Integrator:** swap `cli-main`'s `do` stub for `doCommand` from `src/commands/do.cmd.ts`.
It adds the `orders` positional the stub deliberately left out, and it requires `V2Client`
on top of the stub's services.

---

## Ownership change — U09 gains two files (round 2)

U09's row (§"Unit ownership", `U09 show`) is extended with two `src/` files and one test file:

| unit | files |
| --- | --- |
| U09 show | …as listed, **plus** `src/render/show-unicode.ts`, `src/render/show-regex.ts`, `test/show-regex.test.ts` |

```ts
// src/render/show-unicode.ts — CPython's tables, generated and pinned
export type CodeRange = readonly [number, number]
export const casefold: (text: string) => string                 // str.casefold(), per code point
export const UNICODE_WORD_RANGES / UNICODE_SPACE_RANGES / UNICODE_DIGIT_RANGES: readonly CodeRange[]
export const unicodeCaseClass / asciiCaseClass: (code: number) => readonly number[]
export const CASED_CODE_POINTS / ASCII_CASED_CODE_POINTS: readonly number[]
export const lookupCharacterName: (name: string) => number | null   // \N{…}, bounded table
export const pyIsPrintable: (code: number) => boolean            // str.isprintable(), for repr()

// src/render/show-regex.ts — CPython 3.14 `re/_parser.py`, ported, then re-emitted for JS
export const PY_IGNORECASE = 2
export interface PyRegexFailure { _tag; kind: 'error' | 'uncaught' | 'unsupported'; message }
export const compilePythonRegex: (pattern: string, flags?: number)
  => Either<RegExp, PyRegexFailure>       // `message` is `str(re.error)` verbatim
```

**Why they are separate files and not more of `src/render/show.ts`.** `--grep`'s two halves are
a 297-entry fold table plus a 1,454-class case partition plus three code point range tables
(~27 KB of generated source), and a ~900-line transcript of CPython's regex parser. Both are
*generated from or transcribed from* CPython; keeping them apart from the hand-written renderer
is what makes it obvious which lines a reviewer should read as a port and which as a table.
NOTES §19.2, §19.3 and §19.10 say what they fix and what they cost.

### Round 3: `strip` comes from U04, it is not restated

`command_show`'s `pattern.strip()` / `name.strip()` and `_show_rows`' alias-column compare now
import **`strip` from `src/services/mirror`** (U04's `store.ts`, which spells CPython's 29-code
point whitespace class). They used `String.prototype.trim()`, whose class is neither a superset
nor a subset of Python's, and both directions were observable — see NOTES §19.11. U04 exports it
from the barrel today, so no ownership change was needed; **nothing in U09 defines a second
copy of that class**, and nothing should.

**Nothing else imports them.** `src/render/show.ts` is the only consumer — `showGrep` calls
`casefold` for the literal path and `compilePythonRegex` for `--regex`. If another unit ever
needs a Python-compatible fold or regex (nothing does today: `SHOW_NAME_RE`, `ENTITY_ALIAS_RE`
and friends are all fixed ASCII patterns written directly in TypeScript), import from here
rather than restating the tables.

---

## Import change — U15 takes `casefold` from `src/render/show-unicode.ts` (round 3)

No file changes owner. The addendum above ends "**Nothing else imports them**"; that is now one
consumer out of date, and this is the amendment it invited.

`src/services/orders/parse.ts` (U15) previously defined `casefold` as `text.toLowerCase()`. It
now re-exports U09's:

```ts
// src/services/orders/parse.ts
export { casefold } from 'src/render/show-unicode';
```

`casefold` stays on U15's published surface — `src/services/orders/index.ts` re-exports it and
U15's `match.ts` / `arguments.ts` / `resolve.ts` import it from `parse.ts` — so **no other unit's
import path changes**. `src/render/show-unicode.ts` has no imports of its own, so this adds no
cycle; it costs U15's consumers the ~27 KB table's one-time decode at module load.

**Why.** `toLowerCase` is not a case fold. `data/nation/*.ruleset` ships `Straßburg`, `Meißen`,
`Weißenburg`, `Roßlau`; `translations/core/de.po` ships `Schießpulver` and `Floß`. All arrive as
`target.name`, as `enum` members and as compact-catalog subject words, and CPython's
`"Strassburg".casefold() == "Straßburg".casefold()` is `True`. A `toLowerCase` port refuses those
orders instead of binding them — fail-closed, but on the agent's primary recovery surface.
NOTES §18.5 has the full account and the tests.

**Also for U11:** `src/services/legal-compact.ts` ports `key.casefold()` as `toLowerCase()` for
subject-dictionary keys. Those keys are ASCII identifiers per the OpenAPI schema so it is correct
today; if that ever stops holding, import `casefold` from `src/services/orders` rather than
restating it.

---

## Integration addendum — the wiring, the oracle, and two new core files

Landed by the integrator. No unit loses a file; two files are added to the **core** row
(PORT_MAP §0), which is the integrator's.

### I.1 `src/cli-main.ts` is no longer a stub registry

§4.8 said "every subcommand in `subcommands` is a **stub** owned by cli-main … a unit replaces
one by exporting a `Command` from its own `src/commands/<name>.cmd.ts`; the integrator swaps
the single registry entry." All twenty entries are now swapped. `cli-main.ts` imports the
twenty `Command`s and defines none, and `pending`/`notImplemented` are gone. `COMMAND_OWNERS`
stays — it is still the ownership record, and `test/cli-main.test.ts` asserts every name has one.

`dualText`/`dualFloat`/`dualInteger`/`resolveDual` are now **re-exported** from `src/options`
rather than defined here, closing NOTES §11.4: §4.8 promised them from `cli-main`, a command
module cannot import from `cli-main` (which imports it), and there is now one implementation
behind both import paths. `resolveDualOption` and `resolveDualRequired` are re-exported too.

### I.2 Both inversion seams are closed (§4.6)

`AppLayer` now provides `V2StateSchemaLive` (U03, `src/services/aliases.ts`) and
`RefusalRenderLive` (U14, `src/render/refusal.ts`). The `*Default` layers are still exported
and still used by tests; nothing in the shipped stack reaches them. NOTES §8's caveat — "the
no-op `validate` is safe **only because nothing reads aliases yet**; it must be replaced before
any alias-expanding command is wired up" — is discharged by this change, in the same commit
that wires the alias-expanding commands.

### I.3 New file: `test/diff-offline.sh` (core row)

PLAN's oracle #2. Copies a finished workspace to scratch **twice** — `show` refreshes the
mirror it reads, so one shared copy would let one side's writes decide the other's output —
runs 58 read-only cases under both clients and diffs stdout bytes and exit status.

The reference side is the **justfile**, not `client.py` alone, because this CLI replaces both:
`help`/`rules` are `@cat docs/play.md` / `@cat docs/gameplay.md` (justfile:398-403) and are not
argparse subcommands at all, and the underscored flag spellings are rewritten to the dashed ones
argparse declares, which is exactly what `[arg("game_id", long)]` did. Comparing
`python3 client.py help` would be comparing against a command the Python never had.

Knobs: `WORKSPACE=` picks a different game, `PLAY_BIN=` diffs a compiled binary instead of
`bun src/bin.ts`, `KEEP=1` keeps the scratch tree.

### I.4 New file: `test/json-floats.test.ts` (core row)

The regression guard for NOTES §10.6's four call sites. See NOTES §I.2 for why it also reads
four source files by name.
