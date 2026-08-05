# Full-control-v2 context-efficiency redesign

**Status:** proposed · **Evidence:** forensic audit of `play/turn-one.jsonl`
(one complete turn played by `pi-gpt-5.5`, 42 tool calls, ~80k tokens of agent
context) — per-call audit by 9 independent analysts + 1 protocol-design review,
consolidated into the ledger below.

## 1. The problem, measured

One turn — join, lobby, found one city, dispatch four units, end phase — cost:

| Ledger | Value |
|---|---|
| Tool-result payload ingested | 193,341 chars (~48k tokens) |
| Chars the agent demonstrably used downstream | **4,527 (2.34%)** |
| Wasted | 188,814 chars (~47k tokens), **97.66%** |
| Call verdicts (42 calls) | 4 needed · 14 partially needed · 7 redundant · 16 avoidable · 1 error/retry |

An ideal replay of the *same turn with the same outcome* — reconstructed
call-by-call with every number anchored to data the real payloads carried —
costs **6 calls and ~2,510 chars (~630 tokens): a 77× reduction** (§8).

At 80k/turn, a 100-turn game is 8M tokens of context churn per seat before the
agent has thought a single strategic thought. This is not viable, and it also
distorts the eval: we are measuring a model's ability to survive our wire
format, not to play Freeciv.

### Where the bytes went

| Source | Wasted chars | % of turn |
|---|---|---|
| Stale + duplicate actor `legal` catalogs (10 calls; 2 verbatim dupes, 8 re-fetches after the agent's own revision bumps) | 79,181 | 41.2% |
| First-time `legal` catalogs — per-descriptor boilerplate + per-tile goto enumeration | 43,384 | 22.7% |
| Bootstrap preamble (4 doc/justfile reads incl. the *wrong protocol's* docs, join banner) | 37,061 | 20.2% |
| Observation payloads (`turn`×2, `state`×6, `health`) | 24,581 | 13.3% |
| Receipts, flag-error retry, monitor start | 4,607 | 2.4% |

Cross-cutting (inside the rows above):

- **Opaque 32-hex IDs: ~25–30% of every payload.** 1,076 occurrences, 38.8k
  chars — and hex tokenizes at ~2.5–3 chars/token, so realistically 13–15k
  tokens. Of ~180 `action_id`s ingested, **6 were ever used**. `state_token`
  appears 206 times in results and **0 times** in any command the agent sent.
- **Per-item `state_revision` blocks: 19,760 chars, provably dead** —
  `play/client.py:919` rejects any descriptor whose revision differs from the
  page envelope, so the field can never legally vary within a page.
- **Default-valued ceremony: ~39,700 chars** — 173 copies of the literal empty
  `arguments_schema`, 180 constant `{exact,100.0,100.0}` probability blocks,
  172 actor echoes on pages already scoped by `--actor_id`,
  `legality:"legal"` on every row of a *legal-actions* endpoint.
- **Receipt self-duplication:** every 586-char receipt nests a `receipt`
  object restating 6 top-level fields verbatim (211 chars = 36% of itself),
  and reports four overlapping encodings of "it worked".
- **The 122-char absolute session path** re-typed in 37 of 42 commands
  (~4.5k chars of *input* tokens) and 5× inside a single `next_commands` block.
- **Pagination as pure tax:** the 16-item page cap against always-≥17-item
  unit catalogs forces a cursor round trip per unit; the drained page-2 items
  were used in **zero** cases (the winning action was always on page 1) — only
  the `catalog_complete:true` bit was load-bearing, because the protocol
  forbids acting without it.

### The three root causes

1. **Enumerate-then-execute is backwards for an LLM client.** The agent almost
   always already holds its intent ("found city", "auto explore", "end turn");
   the API forces it to ingest a 13–15k-char menu to launder that intent into
   a 39-char opaque handle. 92% of some call batches existed solely for this
   conversion. Worse, every self-inflicted revision bump (founding London)
   invalidates all outstanding handles, forcing byte-equivalent re-fetches of
   catalogs that are 99.7–99.96% identical after hash normalization.
2. **The wire format was designed for programs and shipped verbatim to a
   language model.** `client.py` `json.dumps`es the server response with zero
   rendering (`play/client.py:2395,2585`). Revision tokens per item, JSON
   Schema per action, probability envelopes, audit hashes — all of it is
   correctness plumbing that belongs in the *eval's log*, not the *agent's
   context*.
3. **Bounds tuned to the wrong budget.** `MAX_PAGE_ITEMS = 16`
   (`agent_eval/v2_control.py:37`) protects nothing the 64KB byte cap doesn't
   already protect, and the per-tile goto enumeration (up to 64 × 608-byte
   descriptors per unit) proves at enumeration time what `set_route` +
   dispatch-time validation already covers.

## 2. What v2's design actually buys — and must keep

The design review confirmed v2 is a **capability system, not an RPC API**, and
four guarantees are real, load-bearing, and visible in code:

1. **Provable legality.** Descriptors derive from Freeciv's own probability
   replies; execution submits an opaque handle, so the server never parses an
   agent-supplied native ID. Illegal actions are *unrepresentable*.
2. **Revision binding as unforgeable capability.**
   `action_id = HMAC(session_secret, revision, slot)`. A stale ID cannot
   replay. Optimistic concurrency with zero reasoning burden on the agent.
3. **Anti-hallucination with the right failure mode.** A hallucinated integer
   ID is a *valid different unit* (silent wrong action); a hallucinated
   128-bit HMAC is cryptographically nonexistent (fails closed). Opaque tile
   IDs also block fog-probing by ID arithmetic.
4. **Counterfactual-menu auditability.** The eval can prove what the agent
   *was shown*, not just what it chose — "chose Y when X was on the menu" is
   the scientific asset of this harness. No intent-parsed API can reconstruct
   that after the fact.

**The core insight of this redesign: all four guarantees constrain the WIRE.
None of them constrain what the agent READS or TYPES.** The server must keep
materializing the full catalog (guarantee 4) and the wire must keep carrying
revision-bound HMAC handles (1–3). But the CLI already persists every
descriptor to `.v2-state` and reconstructs canonical batch bodies locally
(`play/client.py:2684–2714`) — so the agent-facing surface can be a compact
text dialect resolved client-side, with the audit-grade JSON flowing
underneath, unchanged.

## 3. Architecture: three layers

```
┌───────────────────────────────────────────────────────────────┐
│ L2  AGENT SURFACE (what enters model context)                 │
│     compact text: tables, aliases (a1/u3/T(31,72)), digests,  │
│     omit-when-default. Target ≤4k tokens per steady turn.     │
├───────────────────────────────────────────────────────────────┤
│ L1  LOCAL STATE MIRROR (files in the session dir)             │
│     sanctioned projections of .v2-state + static catalogs,    │
│     TSV/text, jq/grep-able, zero network, zero context until  │
│     read. Rebuilt by the CLI on every response it ingests.    │
├───────────────────────────────────────────────────────────────┤
│ L0  WIRE + AUDIT (unchanged guarantees)                       │
│     HMAC capability IDs, revision binding, full counterfactual│
│     catalogs, durable receipts, 64KB byte-capped pages.       │
└───────────────────────────────────────────────────────────────┘
```

The eval's audit log records L0. The agent lives in L2 and dips into L1 when
it wants depth. Nothing at L2/L1 can widen what L0 permits: every alias
resolves against the revision-scoped local cache and dies on a revision bump,
so a stale alias fails closed exactly like a stale HMAC.

## 4. Control model: fast paths, deep paths, no autopilot

The eval's intent: agents must play like a human — real micro, no delegating
play to the game's AI — but a human gets ergonomic leverage from the GUI:
click a far tile and the unit walks there over several turns; queue five
builds; set a distant tech and the prereq chain enqueues. The API must offer
those same **baseline fast paths** as one-liners, and every deeper control a
human has must stay reachable, unrestrained.

**Tier 1 — fast paths (the default surface).** One line per intent, each
matching a single GUI gesture a human player makes:

```
u2 route 40,60                        # multi-turn goto; persists; agent waits
u2 patrol 30,72 33,70
c1 build Warriors                     # and: c1 queue Granary Settlers (worklist)
c1 rally 33,70                        # new units head somewhere
u5 fortify | u5 sentry                # with standard wake conditions
research goal Currency                # prereq chain enqueued
turn --end --await
```

Standing orders persist across turns and **report by exception**: the next
briefing/`delta.md` leads with what needs a decision — idle units, arrivals,
blocked routes, completed builds, combat events — not a full-state reprint. A
steady-state turn where everything is en route should cost a few hundred
tokens: read the delta, adjust, end.

**Tier 2 — deep paths (opt-in, never restrained).** The fast path is sugar
over the same L0 capabilities — never a separate privileged channel, and
never a ceiling. Everything the wire offers stays reachable in ≤2 calls: full
per-actor option catalogs (`options u3`; `--json` for raw descriptors),
tile-level micro (citizen arrangement, an exact worker verb on an exact tile,
per-step movement instead of a route), diplomacy clause construction,
espionage variant selection, and the L1 mirror for arbitrary offline
analysis. Rule for every future surface change: **compression must come from
projection and defaults, never from removing a decision the human client
could make.**

**Excluded — delegation to Freeciv's AI.** `auto_work`, `auto_explore`, and
CMA-governor equivalents place a unit under the built-in AI indefinitely.
That is autoplay, not a fast path, and it corrupts the eval: the audited
turn-one agent put **3 of its 5 units on autopilot within its first six
decisions**, so the game score partly measures Freeciv's AI, not the model.
Remove these kinds from the enumerated catalogs (server-side config,
P2-class). The dividing line: a fast path *executes a decision the agent
already made* (walk to X, build these in this order); autopilot *makes future
decisions on the agent's behalf*.

## 5. The changes

### Status: what the rounding-out pass already landed

The uncommitted pass on `agent-eval-v1` (post-snapshot, by Sol) rounds out
*capability coverage* — the Tier-2 deep paths — and starts on compaction:

- **Deep paths now covered:** global/allied/private chat via
  `chat_recipients` with `can_message` and injection-safe routing; structured
  votes with outcome history and live ballot enums; `unit.attack_route` plus
  the `unit_route` inspection section (reconstruct a unit's remaining queued
  movement — the read half of "route far and wait"); `city_trade_routes`;
  `city.manage_worker_task`; richer `city_build_choices` (city-context cost,
  post-change shield stock, turn estimates, six-output upkeep, structured
  stats — production is choosable from one page); the full targeted espionage
  family (building sabotage/strike, both targeted tech-theft variants incl.
  Future tech, subtargets as opaque named choices, subresults as public
  `effects`); bribe/incite/bribe-stack with request-correlated `gold_cost`
  quotes and atomic ceiling guards; paradrop/teleport/non-lethal/hut
  variants; `pregame_teams` + `pregame.set_team`; `map_tiles` full fog-safe
  board.
- **Compaction started:** the `--kind --all` projection now carries `label`
  plus the semantic `subject` discriminators, and gained `--offset` /
  `--limit 1..64` windows with `has_more`/`next_offset` and byte-limit
  resume.
- **Fast-path building blocks exist at the wire:** `set_route`,
  `attack_route`, `unit_route`, worklists, worker tasks, city rally.

**What remains is almost entirely the L2/L1 surface.** None of the new
sections have compact renderings; actor-scoped `legal` — the audit's
dominant cost — still prints raw descriptor JSON; and each new capability
*adds* context pressure under the current format (more sections to read,
richer payloads). One regression against §4: the pass exposes
`city_governor` (Freeciv's CMA) — under the no-autopilot rule that kind must
be excluded or gated exactly like `auto_work`/`auto_explore`.

### P0 — presentation layer only (wire byte-identical, ship first)

1. **Render catalogs and state as text tables by default; `--json` opt-out.**
   One header line carries the envelope once
   (`rev13/t1 scope=unit u1 Settlers @31,72 mv3/3 [22 acts, complete]`), then
   one row per action. 608 B/item → ~40 chars/item (~15×on the dominant cost).
   The compact projection that `--kind --all` already prints proves the
   renderer pattern exists (`_compact_legal_actions`); apply it everywhere.
   The renderer must consume `_validate_page`-validated objects — no
   defensive `.get()` parallel parser that prints blanks on contract drift.
2. **Omit-when-default, never omit-by-field-name:** drop `probability` only
   when exact-100/100, `legality` only when `legal`, `consuming` only when
   false, `variant` only when null, `subject.actor` only when equal to page
   scope, per-item `state_revision` always (provably constant). A suppressed
   *non-default* — an `unknown`-probability espionage gamble rendering as
   certain — is the one over-compaction that corrupts decisions.
3. **Client-side aliases.** Per revision, number actions `a1..aN`; entities
   get stable short names (`u1`, `c1`, `T(31,72)` — entity MACs are
   game-stable). CLI accepts aliases everywhere an ID is accepted and expands
   them from `.v2-state` before the wire ever sees them. Aliases are stored
   *inside* the revision-scoped `state["actions"]` bucket the client already
   wipes on a newer revision — an alias can never outlive its capability.
4. **Default `--session`.** `_session_path` already resolves a sole session
   and honors `PLAY_SESSION`; stop printing the 122-char path in every doc
   example and every `next_commands` line.
5. **Allow `--all` with `--actor_id`.** The drain loop exists
   (`_command_legal_all`); the current `--kind`-only restriction cost this
   turn an error, a retry, and guaranteed two-page ingests per unit. Auto-
   drain kills the cursor ceremony from the agent's view entirely.
6. **One-line receipts:** `u1 found_city London → London sz1 @31,72, rev9`.
   Drop the nested `receipt` envelope (36% literal self-duplication) and the
   four redundant status encodings. Echo *what happened*, which the current
   receipt, absurdly, does not.
7. **Compact re-fetches:** when a post-bump catalog is unchanged modulo
   hashes, render `u4 Workers @31,72 == u3 (rev13 refresh)` instead of
   reprinting 10.7k chars. Rendering-level dedup only — server-side dedup by
   unit type is unsound (moves left / tile / activity / transport differ).

### P1 — local state mirror (the "live files you can jq")

The session dir gains a CLI-maintained, **sanctioned projection** directory
(the raw `.v2-state` stays private — it holds canonical batch bodies and
receipt internals, and policy forbids exposing it):

```
.sessions/GAME_ID/SEAT/
  state/
    header.txt        # objective, budget, timing, protocol card (~600 chars)
    overview.tsv      # economy, research, government — one row per fact
    units.tsv         # u1  Settlers  31,72  mv3/3  hp20/20  idle
    cities.tsv        # c1  London    31,72  sz1    Warriors 2/10
    map.txt           # ASCII terrain grid, fog as '?', updated per revision
    options/u1.txt    # the rendered catalog, refreshed on fetch
    delta.md          # what changed since the agent's last read, prose
  cache/
    nations.tsv  styles.tsv  governments.tsv  build_choices/  # static, fetched once at join
```

- `just show u1`, `just show --grep goto`, or plain `jq`/`grep`/`Read` on
  these files: **zero network, zero server load, and zero context cost until
  the agent chooses to look.** Everything needed is already persisted by
  `_remember_page` (`play/client.py:1585–1610`).
- Formats: **TSV and fixed-width text, not JSON.** These files are read by a
  model; column alignment beats braces. JSON remains available in
  `--json`/L0 logs for programs.
- Files carry a `# rev N` first line; the CLI rewrites them on every response
  it ingests, so they are always exactly as fresh as the agent's knowledge.
  Reading a file can never leak beyond fog because it is a projection of
  pages the seat already received.

### P1.5 — workspace = seat: ambient session and a player CLI

Field evidence (`game-play-so-far.jsonl`, first live game on the new
surface): the agent still passed the full quoted `--session` path on **79 of
82** gameplay commands (~10.5k chars of pure argument overhead) even though
the flag is optional — because join prints the `session_file` path
prominently and the agent pattern-matches it, and because a second join in
the same workspace re-arms the multi-session fail-closed rule. Optional is
not the same as ambient.

- **Bind the workspace to the seat at join.** `join` records the session as
  the workspace's current seat; every subsequent command uses it with no
  flag and no inference. One workspace = one seat is already the e2e
  pattern (each harness plays in a copied workspace); make it the contract
  instead of guarding against sharing. `play use SESSION` remains as the
  explicit override; two-seats-in-one-workspace becomes unsupported rather
  than ambiguously guarded.
- **Promote the CLI over the justfile as the front door.** `client.py`
  already is the CLI; give it a `play` entrypoint and let the justfile
  survive as a thin alias. The justfile layer costs real tokens and breaks
  the error contract: `just`'s own errors cannot carry remedies (the only
  refusal class on the surface that can't name its next command), recipe
  argument forwarding drifts from argparse (`turn --until` exists only on
  the plumbing), quoting is a footgun (`just do u1 fortify` dies inside
  just), and agents read the justfile itself when confused (30.7k chars in
  the observed game).
- **After binding, join's output should say** "this workspace is now playing
  game X — commands need no session argument" and print bare commands only.

### P2 — protocol changes (each trades something; priced here)

1. **Kill per-tile goto enumeration; keep single-step moves + `set_route`.**
   ~39KB/unit → ~1KB/unit, the biggest single win. Reachability becomes an
   advisory grid in `tile_window`/`map.txt`; route legality is validated at
   dispatch (already is). *Trades:* goto destinations stop being proved legal
   at enumeration time; the counterfactual menu records "route capability +
   grid" instead of 64 pre-proved rows. Acceptable — the eval still logs what
   was offered.
2. **`MAX_PAGE_ITEMS: 16 → 128.`** The 64KB byte cap still binds first; with
   compacted items a page holds ~450. Pure round-trip reduction. *Trades:*
   breaks published `maxItems: 16` in the OpenAPI — version bump, and
   re-verify the `catalog_complete`/byte-cap interaction in the client's
   pagination validator.
3. **Hoist per-page constants:** `page.common = {actor, state_revision,
   schema_by_kind}`; items carry deltas. ~250 B/item on the wire itself.
   *Trades:* descriptors stop being self-describing; the closed-object
   validator (`full_control_v2.py:207–246`) needs a v3 schema.
4. **Multi-order submit:** `just do "u1 found_city London; u2 goto 32,73;
   u3 auto_work"` — client expands each order to its cached capability and
   submits sequentially under the existing one-command-per-batch wire rule;
   one receipt line each. No server change required, only CLI; the wire still
   sees N single-command batches.
5. **Collapse the turn rituals:**
   - `just start --nation English --leader Ada --female` = configure + ready
     (replaces 8 lobby calls incl. two overview reads and two full lobby
     catalogs).
   - `just turn` briefing folds in: terrain ring (the data `tile_window`
     already returns), per-unit one-line option summaries, researchable-now
     names, and the `phase.end` alias — eliminating `health` (a strict subset
     of the briefing, measured byte-identical modulo two floats) and the
     end-of-turn `legal --kind phase.end` lookup (whose own metadata admitted
     `pages_read:6` for one row).
   - `just turn --end --await` = end phase + block + next briefing header
     (replaces the legal→batch→monitor-wait 3-call, ~500-token ritual).
6. **Remove AI-delegation kinds from the catalogs; add standing-order wake
   events.** Drop `auto_work`/`auto_explore`/governor enumeration per §4;
   in exchange, routes, worklists, and sentry/fortify generate arrival /
   blocked / completed events that surface in the next briefing and
   `delta.md`, so "route far and wait" is a genuinely cheap pattern.
   *Trades:* nothing the eval wants to keep — it stops scoring Freeciv's AI.
7. **Join prints the protocol card** (§8 call 1) and **stops shipping the
   other protocol's docs.** This turn read 4,607 chars of pure strategic-v1
   documentation it could never use, because the protocol is unknowable until
   join returns. Move failure contracts (cursor_expired, ambiguous receipts,
   scope_too_large) out of the upfront docs and **into the error payloads
   that name them** — an error should carry its own remedy and the working
   command form.

### Explicitly rejected

- **Server-side intent parsing** (`move u42 to (30,72)` on the wire) —
  destroys guarantees 1, 3, and 4. Intent addressing lives in the CLI only.
- **Server-side catalog dedup by unit type** — unsound; offers illegal
  actions.
- **Shortening wire IDs to 8 hex** — a 32-bit collision resolves to a *real
  different capability*, reinstating the silent-wrong-action failure mode the
  128-bit design exists to kill. Shorten at the alias layer instead, where a
  bad alias fails closed.
- **Unconditional field omission** — see P0.2; only defaults may be elided.
- **Raw agent access to `.v2-state`** — projections only (`state/`, `just
  show`).

## 6. Docs budget

`justfile` + 3 docs = 34.4k chars read before the first game fact entered
context, delivering the same 9-command menu three times. Post-redesign the
agent-facing surface is: join-time protocol card (~600 chars) + `state/
header.txt` + errors that teach. The full prose contract remains for harness
authors, out of the agent's hot path. Rule: **every doc char the agent must
read each game is part of the per-turn token budget** — docs are not free
because they are amortized; turn one is where models form their model of the
API, and this one spent 20% of its budget there.

## 7. Budget targets and regression gate

| Metric (steady-state turn) | turn-one.jsonl | Target |
|---|---|---|
| Tool calls | 42 | ≤8 |
| Result chars into context | 193k | ≤16k (~4k tokens) |
| Useful-char ratio | 2.3% | ≥25% |
| Calls that exist only to obtain a handle | 16 | 0 |

Add to the eval sidecar: per-turn accounting of (calls, result chars, chars
per decision made), logged next to the game score. Context cost is now a
first-class eval metric — a harness regression that doubles turn cost should
fail CI the same way a scoring bug does. `agent_eval/tests` gains a golden
"turn-one replay" fixture asserting the rendered surface for a scripted turn
stays under budget.

## 8. The same turn, redesigned (anchored replay)

Every number below is anchored to data the real payloads carried: the terrain
block is the 16 tiles idx 15 returned; option lines are the same 21–22 labels
the 10.7k-char catalogs carried; receipts report the same five orders the
agent actually executed.

```
1  just join --game_id game_xMC… --name pi-gpt-5.5                    ~800 chars
   session …/pi-gpt-5-5.json  proto full-control-v2  state lobby
   objective max-final-score  max_turns 5000  turn_timeout 180s
   PROTOCOL — capabilities; aliases a1../u1.. are client-side, die on rev bump
     just start --nation N --leader L --male|--female [--style S]
     just turn                    briefing: economy, research, units+options, terrain
     just options <u1|London>     full option table for one actor
     just do "<u> <verb> [args]; …"   1..8 orders, one receipt line each
     just turn --end --await      end phase, block, next briefing header
   nations(50): American Apache … Zulu   styles(6): European … Celtic

2  just start --nation English --leader Ada --female                   ~90 chars
   ok configure+ready rev4 → game running | T1 awaiting_agent 178/180s

3  just turn                                                          ~900 chars
   T1 rev8 | Despotism | gold 50 tax40/lux0/sci60 | research Bronze Working 0/28
   units @31,72 (all mv3/3 idle): u1 u2 Settlers | u3 u4 Workers | u5 Explorer
     u1,u2 found_city cultivate … | step 30,72 31,71 31,73 32,72
     …
   terrain r1: 30,71 Oc  31,71 De  30,72 De  32,72 Sw  31,73 Fo  32,73 Gr …

4  just options u1                                  (optional confirm) ~350 chars
   u1 Settlers @31,72 rev8 [22 acts, complete] all legal/100%/non-consuming
   a1 found_city{name}  a2 cultivate … step a10 30,72 … goto a14 29,72 …

5  just do "u1 found_city London; u2 goto 32,73; u3 auto_work;
            u4 auto_work; u5 auto_explore"                             ~240 chars
   rev8→13  5/5 applied
   u1 found_city London → London sz1 @31,72, building Warriors 0/10 (2t) …

6  just turn --end --await                                             ~130 chars
   T1 ended rev14. waiting 41s… T2 awaiting_agent rev15
   you: London sz1 (Warriors 1/10), 4 units, 1 idle
```

**Total: 6 calls, ~2,510 chars (~630 tokens) vs 42 calls / 193,341 chars —
77× — for an identical game outcome, with all four wire guarantees intact.**

Note: the `auto_work`/`auto_explore` orders appear because the replay is
anchored to what the audited agent actually did. Under the §4 no-autopilot
rule those become explicit orders at identical cost — e.g.
`u3 cultivate; u4 route 30,73; u5 route 27,65` — and later turns get
*cheaper*, because standing routes report by exception instead of the agent
re-deciding delegated units.

## 9. Field results and open gaps (first live game on the new surface)

From `game-play-so-far.jsonl` (5 turn numbers reached): gameplay-only cost
was **86 calls / ~37k tokens ≈ 7–9k tokens per turn** — roughly 9× better
than the audited v1 turn, still ~2× over the §7 target. Where the remaining
fat is: `legal` is still the workhorse (22 calls, 3.4k chars average —
revision bumps and retries force re-enumeration), one bare `just show`
dumped a 16k-char mirror (scoped output needs to be the default, with a
budget), `monitor`/wait tails cost 23k chars, and the justfile self-reads
cost 30.7k. Separately — and dwarfing all of it — the agent spent ~400k
chars (~100k tokens) reading harness source to debug live failures: an HTTP
500 `internal_error` from `legal`, `start` rejecting its own configure
arguments, and a style-name resolution bug. **Reliability is now the
dominant token cost: every harness bug converts directly into an agent
source-diving expedition.**

Known telemetry gap: the live viewer never shows data for v2 games. Replay
telemetry (`replay.jsonl`) is written by the strategic-v1 Lua bridge, and
`supervisor.py` strips every `AGENT_EVAL_*` variable from the server
environment for `FULL_CONTROL_V2` games — so the file stays empty and the
viewer waits forever for its first snapshot, even though v2 games already
write per-turn autosaves and `score.log`. Fix: have the live replay endpoint
fall back to `save_replay.replay_from_autosaves` (the reconstruction path
the archived-run gateway already uses) when `replay.jsonl` is empty.

### Second field report (28 turns, post-P0/P1 surface)

What held: the compact `turn` briefing, stable entity aliases, durable
receipts with fail-closed validation, `show` for cheap local reads, and
errors naming their refresh command — through 28 turns of founding, workers,
research, production, and turn transitions. The remaining gaps, each with
its planned fix and the layer it lives at:

1. **Catalog noise** — player scope enumerated 147 actions, including ~50
   `player.propose_server_setting` rows and dozens of research goals.
   *Fix (client):* the default global-catalog rendering groups by family and
   collapses non-gameplay families to one line each
   (`governance: 50 setting proposals — just legal --kind
   player.propose_server_setting --all`); research goals render as one row
   with the choices inline. `--full` restores the flat list. The deep path
   is untouched.
2. **Micromanagement doesn't scale** — each city/unit needs separate
   inspection. *Fix (client):* a decision-focused projection —
   `turn --decisions` (and the briefing's tail line grows into it): one row
   per actor that can act this turn, with its state and its 3–5 most
   relevant options, batched across actors in one call. This is the
   freeciv-GUI focus loop as a projection — and it also drives the receipt
   tail: every successful `do`/`batch` receipt ends with one `next:` line
   naming the next actor that needs a decision (with cached option aliases
   when fresh, the bare `options` command when not), so acting walks the
   focus loop exactly like the Civ client. Zero extra network from receipt
   paths.
3. **Alias churn** — every applied action bumps the revision and kills
   `a1..aN`, forcing re-enumeration. *Fix (client, guarantee-preserving):*
   semantic alias continuity — on a revision bump the client re-resolves
   each alias to the new descriptor with the same (actor, kind, operation,
   target) from the refreshed catalog it was going to fetch anyway, keeping
   `a1` meaning "that same move". The wire still only ever carries the
   fresh revision-bound id; an alias whose semantic action vanished fails
   closed exactly as today. `do` already re-binds internally; this extends
   the same mechanism to the interactive surface.
4. **Generic native rejections** — a schema-valid `{"value":30}` endturn
   proposal rejected as bare `illegal_action`. *Fix (server):* receipts
   carry the native reason (Freeciv's own rejection string or the failed
   precondition) whenever the boundary has one; `illegal_action` without a
   reason becomes a contract violation.
5. **Stuck non-ready phases** — after an applied `fixedlength` proposal the
   game sat `phase_not_ready` and `wait` timed out silently. *Fix
   (server):* `phase_not_ready` and every `wait` timeout carry a
   `waiting_on` field (which seats/settings/native transition), so a stuck
   control loop is diagnosable from the response.
6. **Surrender semantics** — surrender `applied` but the game stayed
   `running`. *Fix (server):* health exposes the seat's standing
   (`active | surrendered | eliminated | termination_pending`) so an agent
   can distinguish "I resigned and am waiting for the engine" from "nothing
   happened".
7. **Spatial reasoning** — the text map works but planning roads/exploration
   across units is hard. *Fix (client/mirror):* `map.txt` gains optional
   yield/resource overlays (`show map --yields`), and `unit_route` data
   renders as route summaries on the units table (`u2 →(40,60) 3t left`).
8. **Pregame friction** — English/Ada required explicitly picking European,
   and every flag was mandatory. *Fix (client):* `just start` works with
   zero arguments — "get me into the game": random available nation
   (cosmetic under fixed traits), leader defaults to the controller label,
   sex defaults, style follows the nation. Every flag stays as an override,
   and start prints the one-line resolved choice
   (`starting as English — Ada (female), style European`).

### DX audit (2 live games + 2 adversarial reviews, pre-P1.6 snapshot)

Two agents played 18 and 20 turns; two adversarial reviewers audited cold
start/misuse and design-promise keeping. Validated unprompted by everyone:
`turn` (600–830 chars for a whole civilization), `turn --end --await`
(~170 chars), receipts, `show --grep`. Empty turns cost ~350 chars.

**The systemic finding: the taught loop doesn't work as printed.** Plain
`turn` never primes the action cache, so the first `do` of every turn fails
and each actor costs a `legal --actor_id --all` call (~3.3k chars); options
enumeration was 75–85% of every turn's bytes (~3.4 handle-fetch calls/turn
against §7's target of zero), and ~55 such calls exhausted the server cursor
registry — an HTTP 429 that then blocked `turn --end` itself. §8's ideal
transcript only closed its budget because the briefing carried per-unit
option summaries; the shipped briefing dropped exactly that field.

Blockers/majors driving the next wave (beyond what P1.6/server fixes already
landed): trust — `show` renders stale alias numbers with no staleness
banner (semantic continuity closes the misfire, the display still lies);
`--kind` rejects the exact strings its own kind column prints and returns
0/0 for kinds that exist; §4's Tier-1 verbs (route/patrol/build/queue/rally)
are structurally unreachable through `do`; the optimized objective (score)
is unobservable through the surface; no negative space — both players
independently lost ~4 game turns to "found_city silently absent because
goto spent all MP" with no way to ask why; `do` discards good orders when a
neighbor fails and (pre-continuity) lost its tail to its own first order's
revision bump; pregame catalogs advertise nations another seat already
holds; `wait` alone violates the text-by-default contract; the
"re-run with --json" remedy line decorates errors it cannot help (429s).

## 10. Rollout

1. **P0** (client.py rendering + aliases + defaults): no server change, no
   schema change, immediately benefits every seat. Golden-fixture tests on
   rendered output.
2. **P1** (state mirror + `just show` + static caches at join): client-only;
   update `play/docs` to teach file reads as the primary observation path.
3. **P2** in order of win/risk: 2 (page cap) → 5/6 (ritual collapse, join
   card) → 1 (goto enumeration) → 3 (hoisted constants, schema v3).
4. Re-run the turn-one scenario after each phase and publish the ledger
   (§7 metrics) alongside game scores.
