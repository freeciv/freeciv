# `@arena/wire` fixture corpus

Every byte in `runs/` and `live/` was produced by the Python implementation this
package is porting. Nothing was hand-edited. When a `@arena/wire` schema and one
of these files disagree, the file is right and the schema is wrong — that is the
whole point of a decode-parity corpus.

`index.json` is the machine-readable table of contents: one entry per file with
its `origin`, its provenance, its byte size, and its SHA-256. Tests enumerate
`index.json` rather than globbing, so a fixture that is added without being
described fails the corpus test.

## Where it came from

| origin | count | source |
| --- | --- | --- |
| `run-dir` | 24 | files copied verbatim out of `.agent-eval/runs/<game_id>/` |
| `live-capture` | 18 | HTTP GETs against the supervisor and replay gateway that were already running on 2026-08-12 |
| `synthetic-negative` | 14 | a captured fixture with exactly one deliberate defect, generated in-repo |

`.agent-eval/` is gitignored, so the run dirs themselves are not in the
repository and cannot be regenerated from it. That is why these copies are
committed.

### `runs/` — files written to disk by a completed or in-flight run

Inventoried across all 31 run directories under `.agent-eval/runs/`. The
selection targets *distinct shapes*, not distinct runs: 31 manifests collapse
into 12 shape classes, 30 reports into 8, and 12 replay catalogs into 2.

The run directories were opened read-only. Two files present in every run dir
were deliberately **not** copied:

- `auth.json` — join credentials, one per run. Never read into a fixture.
- `saves/`, `v2-receipts/`, `sidecars/` — thousands of files, none of them a
  distinct wire shape.

### `live/` — captured off a running local stack

The stack was already up when this corpus was built (`agent_eval.local_stack`
invocation `77873-d06bcf4656`, supervisor on `127.0.0.1:62188`, replay gateway on
`127.0.0.1:62190`, ready files in `.agent-eval/local-stack/`). Nothing was
started, stopped, or restarted to produce these; every request is a `GET`.

Game `game_QAoITB7qSmKNSwsXX6LaZG8H` was mid-match during the capture, which is
why the running-game payloads show a populated `phase` block, `has_more: true`
cursors, and `outcome.status: "pending"`.

The 4xx bodies are real server responses, not fabrications — the supervisor and
gateway both answer errors as `{"error": "..."}` rather than RFC-7807 problem
documents on these routes, and the port has to match that.

### `invalid/` — synthetic, and labelled as such

These are the only files in the corpus that never crossed a wire. Each one is a
captured payload with a single mutation, so a rejection test proves the schema
caught *that* defect rather than some incidental difference. `not-json.txt`
exercises the parse-failure branch ahead of the schema branch.

## What is deliberately absent

- **`victory.json`** — the brief asked for it; it does not exist. `find` over
  `.agent-eval/` matched nothing named `victory*`, and no JSON under `runs/`
  contains a `winner` key. Victory is *derived*, never stored: the supervisor and
  gateway compute an `outcome` object (`status` ∈ `won | tied | invalid |
  pending`, plus `leaders`, `margin`, `score_turn`, `victory`) from
  `report.json`'s `score.players[].rank`. Decided outcomes therefore live in
  `live/gateway-games-index.json`, which carries three `won` games and one
  `tied` game.
- **A lobby-state manifest** — no run has ever been persisted in a pre-start
  state. The closest real thing is a run cancelled before it started:
  `runs/manifest/cancelled-strategic-v1-never-started.json` and
  `runs/manifest/cancelled-v2-never-started-recovery.json` both have
  `started_at: null` and `checkpoints: 0`.
- **A malformed on-disk manifest** — all 31 parse cleanly. Hence `invalid/`.

## Credential handling

Every candidate was parsed and walked before being written. A field was treated
as credential-shaped if its key matched
`token|secret|bearer|password|api_key|credential|authorization` **and its value
is a string**, or if any string value looked like a bearer/JWT/API key. Zero
matches; nothing needed redacting.

Two near-misses are worth naming so nobody re-flags them:

- `report.json` → `seat_stats.<seat>.input_tokens` / `output_tokens` are integer
  LLM accounting counters.
- `.agent-eval/local-stack/supervisor-*.json` carries `admin_token_env` — the
  *name* of an environment variable, not its value. That file is not a fixture.

`controller_fingerprint` is a SHA-256 digest of a controller identity, already
non-reversible, and is load-bearing for decode parity. It stays.

## Two things that are stable-looking but are not

- **Absolute paths.** `report.json` → `episode`, the gateway health payload, and
  `sidecar-exit-diagnostic.json` → `forensics.stderr_tail` all embed
  `/Users/cryogenicplanet/general/game_eval/freeciv/...`. These are verbatim
  captures; schemas must accept an arbitrary absolute path string, not this one.
- **Ports and pids.** `live/*-health.json` pin `62188` / `62190` and the pids of
  that day's processes. Assert on the shape, never on the numbers.

## Shape notes worth knowing before writing schemas

- `schema_version` is `1` everywhere in this corpus. It is not yet a
  discriminator, but every top-level payload carries it.
- **Two protocol generations coexist.** Older runs have no `control_protocol`
  key at all (strategic-v1); newer ones set it to `"full-control-v2"`. It is
  optional on the manifest and required in `config`.
- `recovery` is an optional top-level key on both `manifest.json` and
  `report.json`, present on 11 of 31 runs.
- `benchmark_valid` is genuinely tri-state: `true`, `false`, and `null`.
- `player.alive` is `true`/`false` on full-control-v2 runs and `null` on
  strategic-v1 runs.
- `rank` is not unique — ties give two players rank `1`.
- `seat_stats` ranges over `{}`, a partial map that omits seats present in
  `score.players`, and a complete map.
- `invalid_reasons` mixes enum-ish slugs (`v2_boundary_wedged`,
  `sidecar_exited`, `score_snapshot_incomplete`, `v2_phase_timeout_failed`,
  `v2_phase_progress_stalled`) with free-text prose
  (`"turn 322 timed out waiting for agent_FK4ZXc2nUbvNoOVc"`). It must decode as
  `string[]`, never as a literal union.
- `replay-catalog.json` has two shapes under the same `schema_version: 1`:
  older entries carry only `{id, rule_name, name, cost_base}`, newer ones add
  `depth` and `requires`. `depth` and `requires` must be optional.
- Timestamps (`created_at`, `started_at`, `finished_at`, `last_seen_at`) are
  float epoch seconds, nullable, never ISO-8601 strings.

## Rebuilding

There is no checked-in generator; the run dirs it reads are gitignored and
machine-local, so a script would not be reproducible for anyone else. To extend
the corpus: copy the file, add an `index.json` entry with its real size and
SHA-256, and describe the shape class it adds. `fixtures-corpus.test.ts` will
tell you if the entry and the file disagree.
