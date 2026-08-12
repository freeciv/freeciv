# Migration notes

Divergences, judgement calls and traps found while porting. `PLAN.md` rule 5 —
where `play/docs/commands.md` and `play/client.py` disagree, the Python wins and the
divergence is noted here.

The scaffold landed on 2026-08-07: build, entry, constants, errors, exits, the schema layer,
`Http`/`V2Client`/`SessionStore`/`PrivateFs`/locks/json-output, the render primitives, the
paging footer and the test harness. `bun run src/bin.ts --help` exits 0, `bunx tsc --noEmit`
is clean, `bun test` is 167/167 green. The exported interface as actually landed is
**PORT_MAP.md §4**; §1 is the frozen sketch and §4 is the authority where they differ.

---

## 1. The "permissive decode" rule and CPython's closed validators

PLAN's known-traps list says: *decode wire JSON permissively (unknown fields pass through),
never `Schema.Struct` with exact-field rejection on server responses — three historical
incidents came from closed validators.*

`play/client.py`'s `_exact` (1247-1269) is a **closed** validator, and
`play/tests/test_client.py` asserts its refusal sentences verbatim:

- `test_client.py:3935` — `"invalid v2 health: unexpected future_field"`
- `test_client.py:4047` — `"unexpected sidecar field.*brand_new_field.*re-materialize"`
- `test_client.py:7177` — `"unexpected invented_field"`

Parity-first wins: the port reproduces `_exact` including its drift sentences, because the
ported tests are the per-unit gate and the byte-diff oracle compares against this Python.

What the rule buys is kept in three ways, and **every one of them is a hard requirement on
new decoding**:

1. **Effect Schema's closed-struct decoding is never used on a response body.** Nothing under
   `src/schema/` builds a `Schema.Struct` and decodes a server payload through it. Every
   check is the hand-ported CPython predicate, so nothing is closed that CPython did not
   close, and no *new* closedness can appear by accident.
2. **Optional-if-present fields widen the key set before `exact` sees it.** `last_recovery`,
   `seat.standing`, `phase.waiting_on`, `phase.auto_end`, `phase.prior_end`,
   `last_phase_end.incarnation`, `last_phase_end.orders_submitted` are all handled this way
   (`src/schema/health.ts`). Adding a server field means adding one line there — that is the
   sanctioned mechanism, not loosening `exact`.
3. **Nested payload values pass through untouched.** `subject`, `arguments_schema`,
   `error.details`, `sidecar` and every state-page item go through `jsonValue`, which copies
   any JSON the wire can carry and only rejects non-finite numbers, absurd nesting and
   absurd sizes.

If a fourth drift incident argues for opening `_exact` itself, that is a deliberate
behavioural change, not a port decision: it changes the tested text and must land with the
tests, in one commit, after the byte-diff oracle is green.

**The schema layer is therefore not built on `effect/Schema`.** The plan asked for "the
Effect Schema wire layer"; what landed is a layer of `Effect`-returning decoders
(`(v: unknown, …) => Effect<T, DriftError>`) that compose exactly like `Schema.decodeUnknown`
does, over hand-ported CPython predicates. The reason is parity: every refusal here has a
byte-exact sentence the ported tests assert, and `ParseError`'s rendering is not that
sentence. Routing through `Schema` would have meant a custom `message` annotation on every
field of all 41 schemas plus a `ParseError`→`DriftError` translator — more code, more
surface, and the exact closed-struct semantics PLAN warns against as the default. The
composition style, the error channel and the call sites are unchanged, so swapping the
implementation later is local to `src/schema/`.

## 2. `json.dumps` cannot be `JSON.stringify`

`src/services/json-output.ts` reimplements CPython's encoder. Three differences would each
have been a byte diff on every non-trivial payload:

- **`ensure_ascii=True` is CPython's default.** One accented city name and
  `JSON.stringify` emits the character where CPython emits `\uXXXX` (and an explicit
  surrogate pair for astral characters). `_canonical_body` (8328-8335) is the one caller that
  passes `ensure_ascii=False`, so `canonicalJson` is a separate export.
- **Separators.** Bare `json.dumps` uses `', '` / `': '`; with `indent=` it uses `','` /
  `': '`; `_print_v2_json` forces `','` / `':'`. `pyJsonDumps` takes all three.
- **Key order.** `sort_keys=True` everywhere the client prints or hashes.

Two printers, matching the Python exactly:

| Python | Port | Shape |
| --- | --- | --- |
| `_print_json` (6127) | `printJson` | `indent=2, sort_keys=True` — strategic-v1 (`next`, `act`, `result`) |
| `_print_v2_json` (3073) | `printV2Json` | `sort_keys=True, separators=(",", ":")` — every v2 `--json` |

**`int` versus `float` — the paragraph that used to live here was wrong.** It read: "this
can only bite if the supervisor sends an integral float. Nothing in
`play/docs/full-control-v2.openapi.json` declares a `number` field that is also integral in
practice." Both sentences are false, and the OpenAPI is simply silent — it types `phase`
and `last_phase_end` as bare `object` and declares no property inside either. The
supervisor is the contract, and it sends integral floats on **every** health response:

- `agent_eval/supervisor.py:125` — `V2_TIMING_MODE_TIMEOUTS = {"default": 600.0, …}`, fed
  to `config["action_timeout_s"]` and published verbatim as `phase.timing.timeout_s`
  (9492-9508). Every other path into that setting normalizes through `float()`
  (`replay_gateway.py:458`, `--action-timeout-s type=float` in `__main__.py:396`).
- `agent_eval/supervisor.py:288` — `V2_AUTO_END_IDLE_GRACE_S = 20.0`, returned verbatim as
  `phase.auto_end.grace_s` (3643) and included unconditionally in every non-terminal
  response (5174).
- `time.time()` deadlines, `round(max(0.0, …), 3)` elapsed/remaining/waiting counters, and
  the phase-event journal's `deadline_started_at` / `ended_at` / `elapsed_s`.

CPython prints `"timeout_s":600.0` and `"grace_s":20.0`; `compactJson` printed `600` and
`20`, so `play health --json` byte-differed from `python3 client.py health --json` on
essentially every live invocation.

`compactJson` itself is unchanged — it has no way to know, because `src/services/http.ts`
runs `JSON.parse` on the body before any unit sees it and the lexeme is gone. Two pieces
of the repair have landed instead:

- **The number model.** U13's `src/services/canonical-body.ts` carries CPython's `int` and
  `float` apart (`PyInt` / `PyFloat` / `PyValue`), serializes them with `pyDumps`
  (`sort_keys=True, separators=(",", ":")`, `encodeStringAscii` for strings, so exactly one
  port of the escape table), and reproduces `repr(float)` in `pyFloatRepr` — fixed notation
  while the decimal point lands in `(-4, 16]`, otherwise a two-digit signed exponent.
- **The health envelope's float map.** U06's `src/services/health-json.ts` marks the twelve
  fields listed above and prints through `pyDumps`; `health --json` now asserts its output
  against goldens generated by CPython's own `json.dumps` (`test/health.test.ts`).

- **The strategic-v1 transport.** U01's `src/services/v1-json.ts` does the general fix inside
  one unit: it reads the response body's text and decodes it with `parsePython`, so `next`,
  `act` and `result` — whose entire stdout is `_print_json(value)` — print the wire's own
  int/float spelling with no field list at all. See §11.9 and PORT_MAP §5.4.

**Still open, for core:** the general fix is for `http.ts` to decode response bodies with
U13's `parsePython` (already written, already tested) instead of `JSON.parse`, so
float-ness survives from the wire rather than being reconstructed from a field list. Until
then every other `--json` surface that carries a supervisor timing — `turn --json`'s
embedded health, `wait --json`, `receipt`'s timings — still prints `600` where CPython
prints `600.0`. See §10.5.

## 3. The private-state sandbox has no `openat`

CPython walks the workspace through directory file descriptors — `os.open(part, O_NOFOLLOW |
O_DIRECTORY, dir_fd=parent)` — so no component can be swapped for a symlink between the check
and the use. Neither Node nor Bun exposes the `*at` family.

`src/services/private-fs.ts` keeps every property that walk was buying — refuse a symlinked
component, refuse a non-directory component, create at mode 0700, write through a temp file
and `rename`, `O_NOFOLLOW` on the final open, enforce mode 0600 on read, refuse anything
outside `PLAY_STATE_DIR` — by `lstat`-walking each component from the workspace root. **The
difference is TOCTOU width, not reachability:** nothing outside `PLAY_STATE_DIR` becomes
writable, and every containment test in `test/private-fs.test.ts` passes.

`ROOT`. CPython's `ROOT` is `Path(__file__).resolve().parent`, i.e. the workspace `client.py`
was copied into — `just` always ran it with that as the working directory. The TypeScript CLI
is not copied into the workspace, so the port uses **`PLAY_ROOT` if set, otherwise
`process.cwd()`**. Every existing invocation keeps working; a CLI installed globally can point
at a workspace without being copied into it.

## 4. `flock(2)` through `bun:ffi`

`_private_advisory_lock` and `_monitor_lock` rely on two kernel properties, and the Python
comments say so explicitly: a second holder simply cannot acquire (no PID file, no liveness
heuristic, no reaping), and the kernel releases on process death (a `kill -9` leaves nothing
stale). Node and Bun expose neither `fs.flock` nor `fs.constants.LOCK_EX`.

`src/services/locks.ts` binds `flock(2)` from libc through `bun:ffi` — a Bun builtin, so no
runtime dependency is added. `hasNativeFlock()` reports whether the binding was made, and
`test/locks.test.ts` **asserts it is `true`**, so a silent degradation fails the suite rather
than the game.

The fallback, for a platform where the binding cannot be made, is an `O_EXCL` sentinel plus a
`kill(pid, 0)` liveness probe. It preserves mutual exclusion and pays for it with exactly the
bookkeeping the Python was avoiding. U17 must not build the monitor singleton on the fallback
without saying so: crash recovery there is a heuristic, not a guarantee.

## 5. `service_url` keeps the port CPython kept

`URL` normalizes a default port away (`https://host:443` → host `host`); CPython's
`urlsplit().netloc` keeps every byte. The session file records this string and `_v2_url`
concatenates it, so `src/services/http.ts` normalizes the **raw authority** — lower-cased,
port preserved — rather than `URL.host`. Verified against CPython for
`https://host:443`, `HTTP://Example.COM:8080/base/` and `http://127.0.0.1:8765`.

## 6. Dual flag spellings are a justfile contract, not an argparse one

`client.py`'s argparse declares only the dashed spelling. The underscored spellings agents
actually type — `--wait_s`, `--game_id`, `--actor_id`, `--batch_id`, `--action_id`,
`--relation_id`, `--center_id`, `--target_id`, `--poll_s` — come from the justfile veneer,
which this CLI replaces (`play/justfile:70-386`). Only `wait` refuses both at once, at
justfile:322-331, with `error: pass only one of --wait-s or --wait_s` and status 2.

The port makes that uniform: `dualText`/`dualFloat`/`dualInteger` declare both names on every
flag that had two spellings, and `resolveDual` produces the same refusal sentence for any of
them. **This is a deliberate widening** — `play state --actor_id X` refusing a second
`--actor-id` is new behaviour, but only for an invocation that was previously impossible to
express. No previously-valid command line changes meaning.

## 7. `_validate_evaluation_context` and `_preconfigured_game_id` moved to core

PORT_MAP §0 assigned both to U02. Both are called from core paths — `decodeHealth` and
`SessionStore.resolveV2` need the first, `_session_path`'s remedy sentence needs the second —
so they live in `src/schema/primitives.ts` and `src/services/session-store.ts` respectively.
**U02 imports them; U02 does not reimplement them.** `_seat_binding_line` is still U02's.

## 8. Two inversion seams, so core could be built first

`.v2-state`'s alias/pending/drained validators and the empty-cache shape are U03's;
`renderErrorPayload` is U14's. Core calls all of them. Rather than have core import files that
do not exist, both are `Context.Tag` seams with a working core default —
`V2StateSchemaDefault` (correct `empty`, no-op `validate`) and `RefusalRenderDefault`
(`_error_text` only). PORT_MAP §4.6 has each unit's landing checklist. Until U03 lands, the
no-op `validate` is safe **only because nothing reads aliases yet**; it must be replaced
before any alias-expanding command is wired up.

## 9. Open questions for the integrator

- `V2_PROTOCOL_CARD` (client.py:2956) is U02's per §2 but is also printed by `join`'s block;
  if a second command needs it, promote it to `src/constants.ts`.
- The stub subcommands declare no positional `Args` (`use TARGET`, `show NAME`,
  `do "orders…"`, `result GAME_ID`). Each owning unit adds its own, because the multiplicity
  is part of that unit's spec.
- `bun test` currently reports nine files because `test/_fixtures/index.ts` is scanned; it
  contains no tests and costs nothing.
- **Dependencies.** Runtime deps are exactly the four PLAN pinned: `effect ^3.22.1`,
  `@effect/cli ^0.77.0`, `@effect/platform ^0.97.1`, `@effect/platform-bun ^0.91.2`. Dev deps
  are `typescript` and `@types/bun ^1.3.14` (PLAN's `^1.4.0` does not exist on npm; 1.3.14 is
  the latest). `node:fs`, `node:path`, `node:os`, `node:crypto` and `bun:ffi` are builtins,
  not dependencies. Nothing else may be added without a line here.

## 10. U06 — health and phase rendering

**Landed:** `src/render/phase.ts`, `src/render/health.ts`, `src/services/health-context.ts`,
`src/services/health-json.ts` (round 2 — ownership recorded in PORT_MAP §5.x),
`src/commands/health.cmd.ts`, `test/phase-render.test.ts`, `test/health.test.ts`, and one
appended fixture file, `test/_fixtures/phase-goldens.ts` (ownership recorded in PORT_MAP §4.10).

### 10.1 The mirror write — landed in round 2

`command_health` (client.py:6552) calls `_mirror_health(path, value, "health")` between the
validation and the render; it writes `state/header.txt` and `state/phase.json`. Round 1 left
a `NOTE(U04)` comment where the call goes, because U04 had not landed. It has:
`mirrorHealth(sessionPath, health, 'health', { commands: V2_PROTOCOL_CARD })` is exported
from `src/services/mirror`, cannot fail (`mirrorGuard`), and `resolveV2` already returns the
session path — so `runHealth` now makes the call and requires `PrivateFs`.

Three details are load-bearing:

- It runs **before** the `--json` fork, not inside the text branch. CPython's order is
  validate → mirror → choose a rendering, and `health --json` refreshes the mirror exactly
  like `health` does.
- `_mirror_health` hardcodes `commands=V2_PROTOCOL_CARD` (client.py:3062-3072); the header's
  command card is not the mirror's default one.
- No `revision` is passed. Health carries no state revision, so `update_from_health` reuses
  the newest revision already stamped in the projections — which is what CPython does.

`test/health.test.ts` asserts both files off disk after `play health` (the `phase` and
`game_state` header lines, and `phase.json`'s turn/phase/active/state), and asserts that
`--json` writes them too.

### 10.2 `health` is not in the subcommand registry yet

Per PORT_MAP §4.8 a unit does not edit `cli-main.ts`. `healthCommand` is exported from
`src/commands/health.cmd.ts` and the integrator swaps the one `healthCommand` entry in
`SUBCOMMAND_REGISTRY`/`subcommands`. Its flag surface (`--session`, `--json`) is identical to
the stub's, so the `--help` contract does not move.

### 10.3 Two judgement calls in the port

- **`turnHealthEpoch` returns an array, and equality is a function.** CPython builds a tuple
  and compares it with `==`; JavaScript has no value equality for arrays, so
  `turnHealthEpochsEqual` is exported alongside. Every member is a scalar by construction, so
  element-wise `===` is the whole comparison. U12 must use the helper, not `===` on the arrays.
- **`turnHealthContext` returns a typed object, not a `JsonObject`.** `PhaseBlock` carries
  optional properties (`waiting_on`, `auto_end`, `prior_end`) and `exactOptionalPropertyTypes`
  makes it non-assignable to `JsonObject`'s index signature. The object is the same *values* in
  the same key order the Python builds; only the TS type differs.

  **Correction (round 3).** Round 2's version of this bullet went on to claim that
  `_composite_json`'s `"context"` part therefore "serializes identically". **That was false,
  and it was the claim most likely to stop a consumer from fixing the divergence.** The
  context copies `health["phase"]` and `health["last_phase_end"]` in by reference
  (client.py:6582-6595), so it carries all twelve of the supervisor's Python floats. Printed
  through core's `printV2Json`/`compactJson`, `turn --json` and
  `turn --end --await --brief --json` emit `"timeout_s":600,"deadline_started_at":1000,`
  `"deadline_at":1600,"elapsed_s":13,"remaining_s":587,"grace_s":20,"remaining_s":12` and
  `prior_end.elapsed_s:600` where CPython emits `600.0/1000.0/1600.0/13.0/587.0/20.0/12.0/600.0`
  — seven-plus diverging tokens in one stdout line, on every live invocation. §10.6 is the fix.

### 10.4 Where the golden bytes come from

Every string asserted in `test/phase-render.test.ts` and `test/health.test.ts` is one of the
constants in `test/_fixtures/phase-goldens.ts`, or is composed from them. The five shapes the
brief names each get a whole-block golden rather than a substring assertion: no phase at all,
your phase active, another seat holding with a deadline, game over, and a phase that ended by
timeout with zero orders. The Python's
originals assert substrings (`assertIn`); the port asserts the whole line, which is strictly
stronger and is what the byte-diff oracle will compare.

`--json`'s two goldens in `test/health.test.ts` are different in kind: they were **generated
by CPython**, by running `json.dumps(value, sort_keys=True, separators=(",", ":"))` over the
dict `_validate_health` builds from the same fixture payloads, and pasted in as literals. A
round-trip assertion (`JSON.parse(out) == JSON.parse(JSON.stringify(envelope))`) is what
round 1 had, and it structurally cannot see a serialization divergence — it re-parses both
sides through the same encoder that caused it. One golden covers the plain running phase;
the other adds `auto_end`, `prior_end` and `last_phase_end` so all twelve float fields are
in one line of bytes.

### 10.5 `--json` floats are reconstructed from the field, not from the wire

`health --json` prints through `src/services/health-json.ts`, not core's `printV2Json`, and
NOTES §2 has the full account of why. The part that belongs here is the limit of the fix.

`FLOAT_PATHS` is a list of twelve dotted paths, each justified by the line of
`agent_eval/supervisor.py` that writes it. It is a claim about the **field**, not about the
bytes that arrived: `src/services/http.ts` runs `JSON.parse` on the body, so by the time
`decodeHealth` — let alone U06 — sees `timeout_s`, `600` and `600.0` are the same JavaScript
value. If a supervisor ever sent an integer in one of those twelve places, this port would
print `600.0` where CPython printed `600`. No supervisor does; every one of the twelve is a
`float()` at its source and `_validate_health` accepts either through `_safe_number`.

The lexeme-preserving fix is a core change and is one line of intent: have `http.ts` decode
with U13's `parsePython` (`src/services/canonical-body.ts`) rather than `JSON.parse`, and
let `PyFloat` survive into `decodeHealth`'s output. Then `FLOAT_PATHS` deletes itself and
every `--json` surface gets the same correctness for free. U06 does not own `http.ts` or
`src/schema/`, so this is recorded rather than done.

Numbers **not** in the list are printed by `pyDumps`'s plain-number rule: an exact integer
prints as an integer, anything fractional prints through `repr`. So a supervisor that sends
`elapsed_s: 12.5` is already right without any marking; the marking only decides what
happens to integral values.

### 10.6 The envelope is almost never printed alone — the float map is re-rootable (round 3)

Round 2 fixed `health --json` and left `healthPyValue` with **zero importers**: `wait --json`
and `turn --json` embed the same numbers and kept printing them through `printV2Json`. The
whole suite stayed green, because nothing asserted those bytes. So round 3 makes the fix
reusable instead of surface-local. `src/services/health-json.ts` now exports:

```ts
export const HEALTH_FLOAT_PATHS: ReadonlySet<string>                       // the twelve, envelope-rooted
export const healthFloatPathsUnder: (...prefixes: string[]) => ReadonlySet<string>
export const pyValueWithFloats: (value: unknown, floatPaths: ReadonlySet<string>) => PyValue
export const healthPyValue: (health: HealthEnvelope) => PyValue            // = under('')
export const turnHealthContextPyValue: (context: TurnHealthContext) => PyValue
export const healthJsonText / printHealthJson                              // unchanged
```

Re-rooting works because `_turn_health_context` copies `health["phase"]` and
`health["last_phase_end"]` **by reference and under their own names**: the two subtrees hold
all twelve floats and keep their keys, so only the prefix changes.

The three call sites, each one line, each with a CPython-generated golden in
`test/health.test.ts` (`describe('the embedded projections')`):

| surface | Python | the line |
| --- | --- | --- |
| `wait --json` (U05) | client.py:2353, 10666/10686/10718 | `pyDumps(pyValueWithFloats(wait, healthFloatPathsUnder('health')), true)` — or the equivalent splice `pyDumps({ ...wait, health: healthPyValue(wait.health) }, true)`, asserted identical |
| `turn --json`, `turn --decisions --json` (U12) | client.py:7535, `_turn_briefing_locked` | `pyDumps(pyValueWithFloats(result, healthFloatPathsUnder('context')), true)` |
| `turn/do --end --await --brief --json` (U12/U16) | client.py:6914-6919 | `pyDumps(pyValueWithFloats(composite, healthFloatPathsUnder('wait.health', 'turn.context')), true)` |

`turnHealthContext` itself is unchanged and still returns plain numbers: `renderTurn` and the
decision renderers read that object, and wrapping its numbers in `PyFloat` would break every
consumer that does arithmetic or comparison on them. The projection is therefore a
*serialization-time* step, and the docstring on `turnHealthContext` says so at the point of
use. **U05/U12/U16 own their own `--json` call sites; U06 cannot land these three lines.**
Until they do, `turn --json` and `wait --json` still diverge — the goldens in
`test/health.test.ts` are what they should assert against once they switch.

The permanent repair remains §10.5's: decode response bodies in `src/services/http.ts` with
U13's `parsePython`, and every path list in this file deletes itself.

---

## 11. U01 — v1 surface (`prompt`, `next`, `act`, `result`) and the doc surfaces

### 11.1 The docs are copies, and the copy is under test

`src/docs/play-card.ts` and `src/docs/gameplay-rules.ts` hold `play/docs/play.md` and
`play/docs/gameplay.md` as template literals, minus their single trailing newline —
`Console.log` puts that newline back, so `play help` emits exactly what `@cat docs/play.md`
emitted (justfile:396-403). `test/docs-surfaces.test.ts` diffs both constants against the
originals, so **editing `play/docs/*.md` fails this suite** rather than drifting silently.
Re-copy the file when that happens; do not edit the constant alone.

### 11.2 `--wait_s` on the wire: `120` and `120.0` are both correct

`next`'s `--wait-s` is argparse `type=float` with `default=120`. argparse does not run `type`
over a non-string default, so an *omitted* flag reaches `urlencode` as the `int` 120 and the
query reads `wait_s=120`, while a *supplied* `--wait-s 120` reaches it as the `float` 120.0 and
the query reads `wait_s=120.0`. The port reproduces both bytes (`pythonFloatText`,
`DEFAULT_WAIT_S` in `src/commands/next.cmd.ts`) instead of normalizing, because the query string
is a wire surface the byte-diff oracle can see. This is a CPython accident, not a design; if it
is ever cleaned up, it must be cleaned up in the Python first.

### 11.3 `--action must be valid JSON: …` — resolved in round 3

Rounds 1–2 appended the JavaScript parser's sentence where CPython appends
`str(json.JSONDecodeError)`. U13's `parsePython` now ports CPython's decoder down to its error
text, so `parsePyArgument` (`src/services/v1-json.ts`) reports CPython's:
`--action must be valid JSON: Expecting property name enclosed in double quotes: line 1
column 2 (char 1)`. Both sentences are pinned in `test/v1-surface.test.ts` against the output of
`python3 -c 'json.loads(…)'`.

`JSON.parse` remains the **verdict** — the CPython message is reported only when the host parser
also refuses the text — so a disagreement between the two scanners can never turn a valid
`--action` into a refusal.

### 11.4 New file: `src/options.ts` (ownership map extended)

PORT_MAP §4.8 promises `dualText` / `dualFloat` / `dualInteger` / `resolveDual` from
`src/cli-main.ts`. A **command file cannot import them from there**: `cli-main` imports the
command modules, so the cycle is resolved during module evaluation, while `Command.make` is
building its options at the top level of the command module. The import reads an uninitialized
binding and `play --help` dies with a `ReferenceError`. Every unit that needs a dual-spelled
flag would hit this.

The four helpers therefore live in `src/options.ts` (plus `resolveDualOption`, which reports
*supplied* vs *defaulted* for §11.2, and `resolveDualRequired`, for a flag argparse marked
`required=True`). **Integrator: make `cli-main` re-export them from `src/options`** — one line —
so §4.8's promise holds and there is one implementation. PORT_MAP §0 has been extended with the
row; the file is listed as U01-owned only because U01 landed it, and it is core-shaped.

### 11.5 The v1 commands accept the underscored spellings too

PLAN §3 rule 3 (*both `--flag_s` and `--flag-s` are accepted*) is applied to every multi-word
flag on this surface, not only `--wait-s`: `--game_id` (`prompt`, `result`), `--after_turn`
(`next`) and `--observation_id` (`act`). These are the spellings the justfile veneer exposed
(`[arg("game_id", long)]`, justfile:70, 104-117, 386) *and* the spellings the bootstrap card
`prompt` itself prints, so folding the veneer into this CLI has to fold them in with it. The
`cli-main` stubs declared only `--wait-s` as dual; the flag surface here is a strict superset of
the stub's, so no `--help` line is lost.

One consequence: `--observation-id` cannot be an `Options`-level required flag once it has two
spellings, so omitting both spellings fails with argparse's own sentence
(`the following arguments are required: --observation-id`) instead of `@effect/cli`'s usage
document. Both exit 2.

### 11.6 `next`'s check order is the contract

Session resolution and the strategic-v1 protocol proof both run *before* the `--after-turn` /
`--wait-s` bounds check, exactly as `command_next` does. For a full-control-v2 workspace the
useful refusal is the one naming `just health`, `just state` and `just legal` — not a complaint
about a flag the agent should not be passing at all. `test/v1-surface.test.ts` pins the ordering.

### 11.7 The Python span the brief named, versus the Python

The unit brief describes `next` as polling `GET /v1/games/{id}/observation`. The Python polls
`GET /v1/games/{id}/me/next?after_turn=…&wait_s=…`, and `act` POSTs to
`/v1/games/{id}/me/actions`, not `/v1/games/{id}/action`. PLAN's rule stands: the Python wins,
and the port uses the `me/next` and `me/actions` paths.

### 11.8 The v1 identity guards read the raw session dict, never the decoded `Session` (round 2)

`command_act`'s acknowledgement cross-check and `command_next`'s two "belongs to a different
game/seat" checks all compare against the **session dictionary as it sits on disk**:
`session["game_id"]`, `session["agent_id"]`, `session.get("place")`, `session.get("seat_id")`,
`session.get("controller_label")`. Round 1 compared against core's decoded `Session` instead,
and `decodeSession` disagrees with the dict in *both* directions:

- `readString(raw, k) ?? ''` invents a value. `_load_session` requires only `game_id`,
  `agent_id`, `agent_token` and `service_url`, so a session with no `controller_label` is a real
  shape. CPython gets `None` and skips the check; the port got `''`, compared it against the
  supervisor's real label and refused with *"the accepted action acknowledgement has the wrong
  controller_label; do not advance LAST_TURN"*. That refusal is terminal for the play loop —
  every later `act` refuses identically, so `LAST_TURN` never advances.
- `readNumber(raw, 'place')` narrows a value away. `just join --place north` sends a non-digit
  place verbatim (client.py:6246), so `place: "north"` is a real shape too. It decodes to `null`,
  which round 1 read as "nothing to check" — an acknowledgement echoing a *different* seat was
  accepted. Same hole for a non-string `seat_id` / `game_id` / `agent_id`.

Both now read through `field(loaded.session.raw, key)`, which is `dict.get`: absent and JSON
`null` both collapse to `None`, exactly as CPython sees them. Note the two commands then use
that `None` differently, and the port keeps the difference:

| | `act` | `next` |
| --- | --- | --- |
| CPython | `expected is not None and key in value and value[key] != expected` | `value.get(key) not in {None, session[key]}` |
| expected is `None` | check skipped | set is `{None}`, so any non-null echo is a **mismatch** |
| echo absent | skipped | `.get` → `None` → fine |

The expectations in `test/v1-surface.test.ts` for these cases are not guesses: each was run
against CPython's `command_act`/`command_next` with a stubbed `request_json` and a session file
written to a scratch `PLAY_STATE_DIR`, and the test asserts that run's observable result.

Two deliberate, noted divergences remain, both fail-closed and both requiring a hand-edited
session file:

- Equality is `compactJson(a) === compactJson(b)` (sorted keys, so dict order does not matter),
  which is total over JSON. CPython's `!=` additionally treats `True == 1`, so a session
  `turn: 1` echoed back as `true` passes in CPython and refuses here.
- The request URL is still built from the decoded `session.gameId`, so a session whose `game_id`
  is not a string posts to `/v1/games//me/actions` where CPython would stringify it. Fixing that
  means reproducing Python's `str()` inside a command file; the decode belongs to core, so it is
  recorded here instead.

### 11.9 `180` versus `180.0`: the whole of this surface's stdout (round 3)

The round-2 port printed the response through core's `printJson`, which serializes the object
`src/services/http.ts` produced with `JSON.parse`. That erases CPython's `int`/`float`
distinction, so `String(180)` printed `180` where CPython prints `180.0`. NOTES §2 already
described the class of bug and called it "still open, for core", but it enumerated only the v2
`--json` surfaces — and on U01's three commands JSON is not one field of a rendered card, it is
**every line of the output**. `next`, `act` and `result` are in `V2_JSON_ONLY_COMMANDS`; their
whole body is `_print_json(value)`.

The damage was total, not marginal:

| command | field | supervisor |
| --- | --- | --- |
| `next` | `action_timeout_s` | `TIMING_MODE_TIMEOUTS["default"] = 180.0` (supervisor.py:117), published verbatim into every `/me/next` payload (8422) — so line 2 of **every** response diverged, on 100% of the strategic-v1 poll loop |
| `next`, `act` | `pending_duration_s` | `round(max(0.0, time.time() - published_at), 3)` (8290), integral whenever the round lands on a whole second |
| `next` | `deadline_at` | `record["deadline_at"]`, a `time.time()` sum |
| `result` | `manifest.config.action_timeout_s`, `lobby_timeout_s`, every `latency_ms` | the run report verbatim — `.agent-eval/runs/game_mEUltpqtzauPGfjI9IlhWJ5x/report.json` in this repo carries `600.0`, `0.0`, `0.0` |

**U06's field map (§10.5) cannot be reused here.** It names twelve dotted paths in a validated
health envelope; `result` returns an arbitrarily deep run report written by four different
modules, and `next` embeds an arbitrary `observation`. There is no finite list.

So this unit took the repair NOTES §2 names as the general fix — decode with U13's
`parsePython`, which reads the *lexeme* (a fraction or an exponent makes a `float`, anything
else an exact `int`, CPython's own `json.scanner` rule) — and applied it to its own transport,
`src/services/v1-json.ts` (PORT_MAP §5.4). Core's `Http` could not be used: it returns an
already-parsed `JsonObject`, and by then the lexeme is gone. Everything else about the request
is core's — the same refusal sentences, `redirect: 'error'`, the same timeout shape, and core's
own `v1ErrorMessage` for a non-2xx — so the two transports can only ever differ on the number
model.

Three consequences worth stating:

- **The printer and the identity guards disagree on purpose.** `pyIndentedJson` keeps `180.0`;
  the `next`/`act` cross-checks compare through `pyToJson`, because CPython's `!=` reads
  `180 == 180.0` as equal. Comparing the `repr`s would invent a mismatch CPython never sees —
  and on `act` that mismatch says *"do not advance LAST_TURN"*, which wedges the play loop.
- **`--action`'s floats now survive onto the wire.** `--action '{"w": 1.0}'` sends
  `{"action":{"w":1.0},…}`, as `json.dumps(json.loads(...))` does; the round-2 path re-encoded
  through `JSON.parse` and sent `1`.
- **The tests had to change shape to be able to fail.** `test/_fixtures/fake-v2-server.ts`
  builds bodies with `JSON.stringify`, so `180.0` can never reach the parser through it — which
  is exactly why a 950-line test file never caught this. The new assertions use a local
  `textFetch` that answers with wire **bytes**, and their goldens are the literal output of
  `python3 -c 'json.dumps(json.loads(wire), indent=2, sort_keys=True)'`.

Offline oracle, run at landing: all 25 `report.json` files under `.agent-eval/runs/` decoded
with `parsePyText` and printed with `pyIndentedJson` are **byte-identical** to
`python3 -c 'json.dumps(json.load(f), indent=2, sort_keys=True)'`. The round-2 path diverged on
`action_timeout_s`, `lobby_timeout_s` and `latency_ms` in every one of them.

**Integrator / core:** when `src/services/http.ts` decodes with `parsePython`, `requestPyJson`
collapses to a one-line delegate and `src/services/health-json.ts`'s `FLOAT_PATHS` deletes
itself. Until then `wait --json`, `turn --json` and `receipt`'s timings still print `600` where
CPython prints `600.0`.

## 12. U04 — the state mirror foundation

**The `_mirror_page` / `_mirror_receipt` bridges land with U07, not U04.** `_mirror`
(client.py:3006-3013) — "a projection never fails a command" — is ported as
`mirrorGuard`, which swallows both the typed error channel and any defect and warns on
stderr with CPython's sentence. `_mirror_health` and `_monitor_mirror` are here
(`mirrorHealth`, `mirrorPhaseMarker`) because `update_from_health` and
`update_phase_marker` are U04's. `_mirror_page` and `_mirror_receipt` wrap
`update_from_page` / `update_from_receipt`, which are U07's, so they belong in
`src/services/mirror/update-page.ts` and `update-receipt.ts`: **U07 writes them as
`mirrorGuard(updateFromPage(...))`.** No ownership row changes.

**`V2_PROTOCOL_CARD` is a parameter, not an import.** CPython's `_mirror_health` passes
`commands=V2_PROTOCOL_CARD` (client.py:2956-2999), which §0 assigns to U02 and which
`src/constants.ts` does not carry (NOTES §9 already flags it). `updateFromHealth` and
`mirrorHealth` therefore take `commands` explicitly and fall back to
`_DEFAULT_COMMAND_CARD` when it is absent — exactly CPython's own default. Every caller
that reproduces `_mirror_health` byte-for-byte must pass the protocol card; if the
integrator promotes it to `src/constants.ts`, nothing here changes.

**`state/phase.json` hits the integral-float case NOTES §2 predicted.** CPython writes
`"held_s": 139.0` and `"deadline_s_left": 41.0` because `timing.elapsed_s` arrives from
the wire as a float; JavaScript has one number type, so the port writes `139` and `41`.
The values are equal as JSON numbers and every reader (`readPhaseMarker`, U17's monitor,
U09's `show`) compares them numerically, but the *bytes* of `phase.json` differ whenever
the supervisor sends an integral float. This is not fixable inside U04: the distinction is
lost in `src/schema/`'s `safeNumber` long before the mirror sees it. `phase.json` is not
on the byte-diff oracle's read-only command list, so it cannot fail that gate — but a
future oracle that diffs the mirror directory itself must exclude this file or teach the
schema layer to carry int-ness.

`updated_at` is `round(time.time(), 3)` in CPython; the port uses
`Math.round(Date.now() / 1000 * 1000) / 1000`. Same value, except that CPython renders a
whole second as `1786058960.0` and the port renders `1786058960` — the same divergence,
one time in a thousand. `phaseMarker` and `updateFromHealth` take an injectable
`MirrorClock` so tests never depend on either.

**`mirror_dir` refuses as a value where CPython raised `ValueError`.** For a path with no
name at all (`/`, `.`, `''`), CPython lets `Path.with_name` raise a bare `ValueError` —
not a `PlayerError`, so `main()` would not have caught it. The port fails with
`state mirror: the session file name has no mirror directory` in the error channel. That
is the *only* remaining divergence in `mirror_dir`, and `_session_path` cannot produce
such a path: it resolves through `_state_relative_path`, which always leaves a component.
Every path CPython accepts resolves identically — see §12.3 for how that was established,
because the round-2 version of this paragraph claimed it and was wrong.

**Divergences that are not divergences.** `_cell` converts an integral float to an int
before `str()`, so the one number shape the mirror renders is *more* aligned in JavaScript
than `phase.json` is, not less. `_read` swallowing `(RuntimeError, OSError)` becomes
`Effect.orElseSucceed(..., () => null)`: same "a projection's own bad file never stops the
client that writes it" semantics, no exception surface.

### 12.1 Text is measured in code points, never in UTF-16 units (round 2)

CPython's `len(text)`, `text[:n]` and `str.ljust(n)` count **code points**. JavaScript's
`String.length`, `slice` and `padEnd` count **UTF-16 code units**, so every astral
character (any emoji, and every plane-1+ script) counts twice. Three places in U04 were
measuring the wrong thing, and each one changed the bytes of a projection file:

* `_cell`'s `_MAX_CELL` cap — a 42-code-point name was truncated to 33 and left a **lone
  high surrogate** at the end, which `PrivateFs.writeText`'s utf8 encoding wrote to disk
  as `EF BF BD` (U+FFFD).
* `_table_text`'s column widths and `ljust` — one astral cell mis-padded its whole column,
  so *every* row of the `.tsv` diverged from CPython. A direct failure of the `show`
  byte-diff oracle.
* `append_monitor_log`'s `[:512]` — split a surrogate pair in half in any logged `--exec`
  string that carried one.

The fix is three shared helpers exported from `src/services/mirror/store.ts` and re-exported
from the barrel — **an additive extension of PORT_MAP §1, recorded there too**:
`codePoints`, `codeLength(text)` = CPython `len`, `codeSlice(text, start, end?)` =
CPython `text[start:end]`, and `ljust(text, width)` = CPython `str.ljust`. `_handle`'s
`digits[:width]` and `_file_name`'s character-class `re.sub` (which now carries the `u`
flag, so one astral character becomes one `_` and not two) were the same class of bug and
are fixed with them. **Any other unit that caps or pads server-supplied text must use these,
not `.length`/`.slice`/`.padEnd`.**

The same pass replaced JavaScript's `\s` with CPython's whitespace class in `_cell`'s
`strip`/`\s{2,}` collapse and in the `rstrip` `_table_text` applies to each row. The two
sets are not nested: CPython strips `U+001C`–`U+001F` and `U+0085` (which JS `\s` does
not) and does *not* strip `U+FEFF` (which JS `\s` does). Verified against the original for
all three.

### 12.2 `_alias_map` reads own keys only (round 2)

CPython's `text in aliases` is own-keys-only; the port's `aliases[text]` was a
prototype-chain lookup, exactly the trap NOTES §15 flagged from U08. For wire ids
`['toString', 'constructor']` CPython renders the fallback handles `u.toStri` / `u.constr`
while the port rendered `function toString() { [native code] }` and
`function Object() { [native code] }` straight into the alias column of
`state/units.tsv` — host-runtime text leaking into a projection file, and a later
`show`/`grep` on that alias resolving to garbage. `aliasMap` now guards with
`Object.hasOwn`, matching its sibling `dig`. (`__proto__` needs no special case: CPython
also returns `__proto__` for it, because `_handle`'s `^[a-z_]+_` eats the whole id and the
empty-digits branch returns the text unchanged — and the port stores into a `Map`, which
has no `__proto__` setter.)

### 12.3 `mirror_dir` is `pathlib`, and `pathlib` changed in 3.14 (round 3)

The round-2 `mirrorDir` reimplemented `Path.stem` / `Path.suffix` by hand as
`basename.lastIndexOf('.')` with a `0 < dot < len-1` guard. That guard is CPython
**3.13**'s rule. This repo runs **CPython 3.14.6**, which rewrote both properties:

```python
# 3.14 pathlib._local.PurePath
suffix: name = self.name.lstrip('.'); i = name.rfind('.'); return name[i:] if i != -1 else ''
stem:   i = self.name.rfind('.'); s = self.name[:i]; return s if i != -1 and s.lstrip('.') else self.name
```

Leading dots are now dropped *before* the suffix search, and a bare trailing dot is now a
suffix. Four verified divergences, all reachable — a session basename is user-controlled,
`--session` and `PLAY_SESSION` flow into `_session_path` (client.py:951-965) unfiltered:

| session file | CPython 3.14.6 | round-2 port |
| --- | --- | --- |
| `/w/x/session.` | `/w/x/session` | `/w/x/session.-mirror` |
| `/w/x/a.` | `/w/x/a` | `/w/x/a.-mirror` |
| `/w/x/x..` | `/w/x/x.` | `/w/x/x..-mirror` |
| `/w/x/..hidden` | `/w/x/..hidden-mirror` | **hard failure** |

Every projection would have landed in a directory no later `show`, `state` or
`_cached_phase_note` reads, or never been written at all.

`node:path` is not a stand-in for `pathlib` either, and the round-2 code used it for the
rest: `path.basename('/w/x/.')` is `'.'` where pathlib's `name` is `'x'` (pathlib drops
`.` components while parsing), and POSIX keeps *exactly two* leading slashes as the root
while collapsing one or three-or-more. `mirrorDir` now ports `_parse_path`,
`_format_parsed_parts`, `name`, `stem`, `suffix` and `with_name` directly.

Established by differential fuzz against the installed interpreter: **8,230 generated
paths** over the alphabet `. / a b .json x .. - 1 ␣`, plus every ordered pair of
`'' . .. ... a a. .a a.b ..a a..` in two positions — all identical, including which
inputs fail. Under 3.14 the `_error("the session file name has no mirror directory")`
branch is **unreachable** (a non-empty suffix implies a stem holding a non-dot
character); the 13 failing inputs are all the `ValueError` case above. The line is kept
so the port fails closed if the rule moves again.

### 12.4 `.trim()` is not `str.strip()`, and the parse half was still on `.trim()` (round 3)

§12.1 fixed the *write* side (`cell`'s strip/collapse, `tableText`'s rstrip) and stated
the rule — CPython strips `U+001C`–`U+001F` and `U+0085`, JavaScript's `\s` does not;
JavaScript strips `U+FEFF`, CPython does not — but the *read* side was left on `.trim()`
in all three parsers: `parseTable` (columns and every cell), `parseDelta`'s section title
and `rowChanges`' first-observation line, and `parseMap`'s legend pair.

This is reachable from server data, not fuzz. `_cell` deliberately does not strip
`U+FEFF`, so a unit named `'﻿Artillery'` is written verbatim into `state/units.tsv`
— and read back by the port as `'Artillery'`, a *different* string. Consequences, all
verified against the original:

* `rowChanges(['alias','kind','at'], prior, sameRows)` is `[]` in CPython and
  `['u1: kind Artillery -> ﻿Artillery']` in the port — a phantom bullet appended to
  `state/delta.md` on **every** page refresh, spending the `MAX_DELTA_LINES` budget and
  telling the agent something moved that did not.
* the merged `.tsv`'s cell content and its recomputed column widths diverge byte-for-byte.
* U09's `show` / `_mirror_table` reads through this same `parseTable`, so the offline
  byte-diff oracle's read-only command list diverges on stdout.
* `parseMap`'s legend loses continuity, and `_render_map` (U08) depends on it to never
  change a character's meaning mid-file.

`store.ts`'s CPython-accurate `strip` is now exported alongside `rstrip` (barrel too — an
additive extension of PORT_MAP §1, recorded there) and used at all five call sites.

Two further divergences of the same family surfaced while building the differential
harness, both in U04's own span and both server-reachable:

* **`splitLines` was missing `U+001C`–`U+001E`.** CPython's `str.splitlines()` breaks on
  the three information separators; `String.split` on the round-2 boundary set did not.
  A mirror file carrying one had two rows read back as a single row, which then rewrote
  the file. The 403-case table fuzz found it immediately.
* **`_MAP_ROW_RE`'s `\s*` and `_MAP_LEGEND_PAIR_RE`'s `\S` are Python's classes, not
  JavaScript's.** Python's `re` `\s` for `str` is exactly the 29-character `str.strip()`
  set (verified by enumerating all of Unicode under both). `_render_map` writes the
  server's terrain name into `# terrain …` **without** passing it through `_cell`, so a
  name holding `"\n\x1f 71 |XXX"` forges a line CPython reads as a grid row and JS `\s*`
  does not — and a name-forged row indented with `U+FEFF` is a row to JS and not to
  CPython. `PY_SPACE_CLASS` is exported from `store.ts` and both regexes are built from
  it. 3,003 generated `map.txt` bodies with injected terrain names agree exactly.

**Residue, deliberately not fixed:** Python's `\d` for `str` matches every Unicode `Nd`
digit and `int()` accepts them, while JavaScript's `\d` is ASCII-only. A forged
`# window`/row/`# rev` line spelled with e.g. Arabic-Indic digits would parse in CPython
and not here. Fixing it means teaching `REV_LINE_RE`, `MAP_WINDOW_RE`, `MAP_ROW_RE`,
`DELTA_SINCE_RE` **and** U09's `_YIELD_TILE_RE` a `\p{Nd}` class plus a CPython-`int()`
conversion — a cross-unit change, and a partial fix would be worse than none. Nothing
this client writes can contain such a digit.

### 12.5 Not U04: `test/batch-persist.test.ts` is red on a concurrent rename (round 3)

At the end of this round `bun test` is 1607 pass / 8 fail and `bunx tsc --noEmit` reports
two errors, all of them in `test/batch-persist.test.ts` and all the same cause:
`src/services/canonical-body.ts` now spells the field `PyParse.failure`, and that test
still reads `parsed.failed`. Nothing in U04 touches either file. Recorded here per the
"note, do not fix, another unit's file" rule; U04's own four suites are 120/120 and no
file U04 owns produces a type error.

---

## 12. U02 — `join`, `use`, invitations

### 12.1 `--game-id` and `--name` are no longer `required=True`

argparse marked both `required=True` (client.py:11626-11627), but the justfile recipe this CLI
folds in *always* passed them — as `""` when the caller omitted them (justfile:85-90) — precisely
so `_apply_play_defaults` could fill them from `.playconfig.json`. Requiring them at the
`Options` level would break `just join` in exactly the workspace `just play` builds, so both
default to `""` and `_game_id` / `_controller_name` produce the refusal. The observable text is
unchanged: a bare `play join` in a workspace with no config still exits 2 with
`error: a valid assigned game ID is required`.

Per NOTES §11.5 the underscored spellings are accepted too (`--game_id`, `--join_token`), because
`just join --game_id GAME_ID --name HARNESS-MODEL` is the string this port's own refusals print
(`_resolve_use_target`, `command_use`, `_no_session_error`).

### 12.2 `V2_PROTOCOL_CARD` lives in `src/render/join.ts`, not `src/constants.ts`

PORT_MAP §0 gives every shared `V2_*` constant to `src/constants.ts`, but the card at
client.py:2952-3000 is outside every line range that row enumerates and the U02 brief assigns it
to this unit. It is exported from `src/render/join.ts`.

**Two consumers, one definition.** `join` prints it (client.py:6000) and `_mirror_health`
(3071) passes it to `state_mirror.update_from_health(..., commands=V2_PROTOCOL_CARD)`, which
writes `state/header.txt`. **U04 imports `V2_PROTOCOL_CARD` from `src/render/join`** rather than
re-typing it; `test_v2_join_card_and_state_header_carry_the_same_contract` asserts the join card
and `header.txt` carry the identical twenty lines, so a second copy is a test failure waiting to
happen. `test/join.test.ts` pins the length at 20 and the first two lines verbatim.

### 12.3 `_validate_evaluation_context` is imported, not reimplemented

PORT_MAP §0 assigned it to U02; §4.4 then moved the decoder into `src/schema/primitives.ts`
because `decodeHealth` and `SessionStore.resolveV2` both call it. `src/services/evaluation-context.ts`
is therefore a *seam*, not a second implementation: it re-exports `decodeEvaluationContext` with
the error mapped from `DriftError` to `PlayerError`, which is what the command layer's error
channel wants. The refusal text is identical either way — both render as
`error: invalid v2 join result: evaluation context is malformed` — because `DriftError.message`
already carries the CPython sentence.

### 12.4 The invite loader's refusals are a rejection matrix, and they are the tests

`_invite` (6010-6126) has eleven refusal paths and ten of them end with the identical sentence:

> Ask the game owner to run `just invite {game_id}` from the repository root, then retry once.

That sentence is the whole recovery surface — the agent reading it cannot run `just invite`
itself and has to quote it to the game owner — so `test/invites.test.ts` is table-driven over
every path (symlinked `.invites`, missing `.invites`, absolute escape, `..` traversal, symlinked
invite file, mode 0644, unparseable JSON, a JSON array, `schema_version` 2, a missing
`schema_version`, another game's ID, a missing `game_id`, a blank / untrimmed / non-string
`join_token`, a null / credentialed `service_url`, and no invitation at all) asserting the exact
string each time. The two path refusals (`.invites must be a real directory inside play/`,
`invite files must stay inside .invites/`) deliberately carry no remedy: they describe a
workspace an operator broke, not an invitation an owner can re-issue.

The credential-override rule is its own describe block, because it is the one place the loader
is *permissive*: `--join-token` / `AGENT_EVAL_JOIN_TOKEN` skips the implicit default file
entirely (so a stale local invitation cannot block documented recovery or redirect the join to
its old origin) but never skips an explicitly configured `--invite` / `PLAY_INVITE` path — that
file's `service_url` still decides the origin, and only its rotted `join_token` is tolerated,
because with an explicit token in hand the stored one is never read.

### 12.5 Two small formatting approximations

- **`{control_protocol!r}`** (client.py:6247). `pyRepr` in `join.cmd.ts` reproduces CPython's
  `repr` for the cases the wire can produce (`'text'`, `None`, `True`/`False`, numbers,
  containers); it does not reproduce CPython's escaping of non-printable characters, which no
  protocol name has ever contained. The tested case is
  `game requires unsupported control protocol 'full-control-v3'`.
- **`invalid .playconfig.json: {exc}`** (6194). The interpolated value is the JSON parser's own
  message, and Bun's differs from CPython's `json.JSONDecodeError`. The Python test asserts only
  that the sentence names `playconfig`, and so does the port's. The *validation* refusal — the
  one an operator acts on — is byte-identical.

### 12.6 `_render_join` drops its unused `path` parameter

CPython took `path: Path` and never referenced it; the comment on the line where it would have
been printed explains why (*"Printing it is what taught every observed agent to re-type it on 79
of 82 commands"*). `renderJoin(session, result, replaced?)` therefore has three parameters, and
`test/join.test.ts` asserts the rendered block contains neither `.sessions` nor a session file
name. Stdout is unchanged, which is the only contract that binds.

### 12.7 The invite loader resolves symlinks before `..`, and owns that walk (round 2)

`_invite` proves containment with `Path.resolve()` twice (client.py:6037, 6047-6048), and
`Path.resolve()` is `os.path.realpath(strict=False)`: it walks the path **one component at a
time**, expanding each symlink as it meets it, so a `..` pops a component off the *already
resolved* prefix.

Round 1 used core's `resolveExisting` (`private-fs.ts:47`), which calls `path.resolve()` first.
`path.resolve` / `path.join` collapse `..` **lexically, before** any symlink is read. The two
predicates are not equivalent, and the difference decides which file a bearer token is read
from. With `.invites/d -> <root>/outside` a directory symlink:

| `--invite` | CPython | round 1 |
| --- | --- | --- |
| `.invites/d/../x.json` | `<root>/x.json` → `invite files must stay inside .invites/` | `<root>/.invites/x.json` → **accepted**, wrong token |
| `.invites/deep/../../keep.json`, `deep -> .invites/a/b` | `<root>/.invites/keep.json` → accepted | `<root>/keep.json` → refused, a good invitation lost |

So the one refusal this unit exists for silently never fired for any path whose `..` crosses a
symlinked component — the exact shape a hostile `.invites/` entry would take.

`posixpath._joinrealpath` is therefore ported into `src/services/invites.ts` as `pathResolve`,
alongside `rawJoin` — `pathlib`'s `/`, which does **not** normalize, so
`ROOT / ".invites/d/../x.json"` reaches the resolver with its `..` intact rather than pre-collapsed
by `path.join`. Both are module-private: they are `_invite`'s proof obligation, not a general
utility, and core's `resolveExisting` is right for its own callers (`workspacePaths`,
`stateRelativePath`), which are lexical in CPython too (`os.path.abspath`, client.py:314-320).

The walk was validated **against CPython itself**: a scratch symlink zoo (directory link,
relative link, link to `..`, dangling link, two-hop loop, `.` / `//` / trailing-slash noise) run
through `Path(p).resolve()` and through `pathResolve`, 25 of 25 paths agreeing, including the
non-strict loop behaviour (keep the unresolved remainder, never raise `ELOOP`). The suite keeps
the conclusions rather than the harness, so `bun test` never shells out to `python3`; four of the
seven new `containment across a symlinked component` cases fail against the round-1 resolver,
which is what makes them worth their runtime.

### 12.8 `str.strip()` is not `.trim()`, and this loader is where it decides a bearer (round 3)

`_invite` calls `.strip()` five times, and rounds 1-2 spelled all five `String.prototype.trim()`.
The two whitespace classes are not the same set, and neither contains the other:

| | `\x1c`-`\x1f`, `\x85` | `﻿` |
| --- | --- | --- |
| CPython `str.strip()` | stripped | **kept** |
| JavaScript `.trim()` | **kept** | stripped |

So the port was looser than CPython in the direction that matters. Verified against
`client._invite` with a patched `ROOT`:

| case | CPython | rounds 1-2 |
| --- | --- | --- |
| stored `join_token` = `"join-secret\x1f"` | `… has an invalid join token. …` | **accepted**, sent as the bearer |
| stored `join_token` = `"join-secret﻿"` | accepted | refused |
| `AGENT_EVAL_JOIN_TOKEN="\x1f"`, default invite on `:7777` | `("join-secret", "http://127.0.0.1:7777")` | **`("", "http://127.0.0.1:8765")`** |
| `PLAY_INVITE="\x1f.invites/x.json\x1f"` | loads the file | `invite files must stay inside .invites/` |
| `--invite "﻿.invites/x.json﻿"` | `invite files must stay inside .invites/` | loads the file |

The third row is the serious one: a lone separator character in the environment flipped the
*credential-override branch*, so the port skipped the default invitation entirely, sent an empty
bearer, and joined the **default origin instead of the invitation's declared one** — the precise
outcome the "a token skips only the default file" rule exists to prevent.

`pyStrip` (exported from `src/services/invites.ts`) is CPython's `str.strip()`, built from the 29
code points `python3` reports as `c.strip() == ''`. All five `_invite` sites go through it, plus
`command_use`'s `(target or "").strip()` and the four in `_apply_play_defaults`. The rejection
matrix now has a `CPython's whitespace class decides who is a credential` block: four assertions
per separator character (stored token, environment override, `--invite` padding, `PLAY_INVITE`
padding) and three for `﻿` in the other direction, every expectation taken from CPython.

U04's `src/services/mirror/store.ts` has a private copy of the same character class for `_cell`.
**Integrator:** if either is ever touched again, collapse them onto one export — they are the
same CPython primitive, and two copies is how one of them drifts.

### 12.9 The invite file is decoded strictly, or it is unreadable (round 3)

`_load_object` reads with `Path.read_text(encoding="utf-8")` and catches `UnicodeDecodeError`,
so a malformed byte makes the invitation *unreadable*. Rounds 1-2 used
`fs.readFileSync(resolved, 'utf8')`, which **substitutes U+FFFD** and never fails. Verified: an
invitation whose `join_token` is the bytes `to\xffk` is refused by `client.py` and was accepted
by the port, which then joined with the bearer `"to�k"` — a credential that is not the bytes
in the file. A loader whose entire brief is to fail closed on a malformed credential file was
failing open on one.

The read is now `new TextDecoder('utf-8', { fatal: true, ignoreBOM: true })` over the raw bytes.
`ignoreBOM: true` means *do not strip a leading U+FEFF*: the utf-8 codec keeps it and
`json.loads` then rejects it, so a BOM'd invitation is `unreadable` in both implementations —
`TextDecoder`'s default would have silently repaired it. `_apply_play_defaults` reads
`.playconfig.json` the same way (`except ValueError` catches `UnicodeDecodeError` there), and got
the same fix.

**Residual, deliberately not fixed:** `JSON.parse` rejects `NaN` / `Infinity` / `-Infinity`,
which `json.loads` accepts. Every such literal reaches a field the loader type-checks
(`schema_version == 1`, a `str` `game_id`/`join_token`/`service_url`), so both implementations
still refuse — only *which* remediation sentence prints differs, and only for a file no
`just invite` ever writes. Building a second JSON parser for that is not worth the surface;
U13's `parsePython` does not accept them either.

### 12.10 `timing_mode` is `str(x or "unknown")`, and `or` is the drift absorber (round 3)

`command_join` prints `str(result.get("timing_mode") or "unknown")` (client.py:6350) into both
conduct blocks. `timing_mode` is a v1 join field with **no schema in
`full-control-v2.openapi.json`**, which is exactly why CPython wrote `or` rather than
`is None` — it absorbs whatever the supervisor sends.

Round 2's ternary only caught `null`, `false` and `0`, so `''`, `[]` and `{}` fell through to
`scalar()`: `timing_mode: ""` printed `Timing mode: ; 5 seconds per agent turn.` where CPython
prints `unknown`, and `timing_mode: []` printed `Timing mode: [];`. The strategic-v1 branch had
the same hole (`Joined in  timing mode:`). The old test pinned only the missing-key case, so the
suite was green over it.

`timingModeText` (exported from `src/render/join.ts`) now implements the two CPython primitives
separately: `pyTruthy` (`None`/`False`/`0`/`0.0`/`""`/`[]`/`{}` are falsy) and `pyStr` (a `str`
is itself, everything else is its `repr()` — `True`, `['a', 1]`, `{'a': 1, 'b': None}`).
`test/join.test.ts` drives a 13-row table through it and through both branches of
`joinGuidance`, every expectation taken from `python3 -c "str(v or 'unknown')"`.

One residue, shared with NOTES §16.4: JSON's `int`/`float` distinction does not survive
`JSON.parse`, so `repr(5.0)` renders as `5`. `timing_mode` has never been a number on any
observed wire.

---

## 13. U03 — v2 client state: aliases, pending catalogs, catalog cache

### 12.13 `join` is idempotent per workspace (deliberate divergence, post-cutover)

CPython re-claimed on every `join`. The session filename is deterministic
(`sessionKey(controller)`), so a re-join whose first response was lost claimed a **second**
seat server-side and silently overwrote the local session with it — one workspace holding two
places, the opponent's join refused with `HTTP 409: all agent places are claimed`, and the
orphaned seat unreachable by anyone (the live `game_vkNE6LdubmOd4dBm73whb4NM` incident; the
supervisor has no release endpoint). The class is exactly what the receipts protocol exists to
prevent on batches, and `join` had no equivalent.

The port now guards: a workspace already holding a session for the assigned game re-binds it,
prints `already joined …`, and makes **zero** network requests; a held-but-unreadable session
refuses with the deletion remedy instead of silently claiming over it. Claiming a fresh seat is
a deliberate act: `delete .sessions/<game>/` first. Covered in `test/join.test.ts`
("a second join re-binds…", "a held-but-corrupt session refuses…").

### 13.1 Two wave-2 seams, because two wave-2 units own the row renderers

`_action_semantics`, `_catalog_signature`, `_catalog_equivalence` and `_cached_kind_scopes` are
U03's per PORT_MAP §2, but CPython defined all four in terms of helpers on U11's and U15's rows
(`_compact_legal_action`, `_action_kind_key`, `_legal_row`, `_kind_selector_matches`,
`_order_actor`, `_order_operation`, `_order_properties`). Two different resolutions, chosen per
helper by whether the dependency is *structural* or *behavioural*:

- **`_action_semantics` reads the descriptor directly.** Every field it took off
  `_compact_legal_action(descriptor)` — `subject.actor`, `kind`, `subject.operation`,
  `subject.target`, `arguments_schema` — is copied through that projection unchanged: none is in
  the `{target, probability, gold_cost}` drop set, none matches the leak guard's reserved terms,
  and `target`/`argument_schema` are verbatim renames. Reading the descriptor is therefore the
  same function, not an approximation, and it keeps the alias tables in wave 1 with no
  callback threaded through `rememberPage`. `test/aliases.test.ts` pins the two properties that
  matter: two enumerations of the same board position produce the same string, and a different
  position does not.
- **`_catalog_signature` / `_cached_kind_scopes` take `CatalogRenderDeps`.** A *row* is genuinely
  U11's output — its byte layout is U11's spec — so the port injects `compactLegalAction`,
  `actionKindKey`, `legalRow` and `kindSelectorMatches` rather than forking them. U03's tests
  supply faithful stubs and test the *comparison*: which catalogs are eligible, what "the same
  options in the same order" means, and which rows come back as differing.

`_refresh_stale_alias`'s drain is the third seam and the one PORT_MAP §1 already froze:
`LegalPageFetcher` is U11's `_drain_legal_unlocked`. The **request-lock policy stays in U03**,
exactly where CPython kept it — `refreshStaleAlias` wraps the fetcher in
`SessionStore.withRequestLock` when `locked: false`, and the fetcher itself must never take one
(the advisory lock is not reentrant).

### 13.2 `_action_target_key` and `V2_KIND_LIST_MAX` moved rows

Recorded in PORT_MAP §6. `actionTargetKey` lives in `src/services/aliases.ts` because
`_action_semantics` needs it; `V2_KIND_LIST_MAX` (client.py:7976) lives in
`src/services/catalog-cache.ts` because core's `src/constants.ts` did not carry it and is not
U03's file to edit. Both should move to their natural homes the next time the integrator touches
them; neither is duplicated anywhere today.

### 13.3 The lock is taken once, and the write inside it bypasses the store

`_remember_page` / `_remember_receipt` / `_drop_pending_for_*` all follow the same CPython shape:
take the `.v2-state` lock, **re-read the cache from disk**, mutate, save, release. The port uses
`SessionStore.withStateLock` for the first two steps and then writes through
`PrivateFs.writeJson(store.statePath(…))` rather than `SessionStore.writeState` — `writeState`
takes the same non-reentrant advisory lock and would deadlock inside the body. That is why every
one of these functions requires `SessionStore | PrivateFs` and not `SessionStore` alone.

The Python's several "save, *then* raise" paths (expired cursor, repeated action ID, metadata
changed, exceeded total, completed short) are reproduced exactly: the discarded catalog is
durable before the refusal is raised, so the next command starts from a clean cache rather than
re-refusing on a prefix nobody can complete.

### 13.4 `rememberPage` takes a discriminated input, not a `legal=` flag

CPython's `_remember_page(path, state, page, *, legal)` typed `page` as `dict`. The port's
`RememberPageInput` is `{ legal: false; page: PageEnvelope } | { legal: true; page:
LegalActionPageEnvelope }`, so `page.page.items` is `LegalActionDescriptor[]` on the legal arm
with no cast. Behaviour is unchanged; only the call shape moved.

Likewise `_remember_page` mutated its `state` argument in a `finally` and returned the promoted
list. The port returns both: `{ state, promoted }`. `_promoted_catalog_page` then consumes
`promoted` exactly as CPython did.

### 13.5 A staged descriptor is decoded only once its revision is proved

CPython compared `pending["items"]` entries as raw dicts. The port decodes them to
`LegalActionDescriptor` so the promoted catalog is typed all the way to the mirror — but *only
after* the metadata check has proved `pending["state_revision"] == revision`, because
`decodeDescriptor` requires the descriptor's own `state_revision` to match. The two places that
touch a pending entry before that proof (dropping same-scope catalogs, and
`dropPendingFor{Cursor,Scope}`) read the raw JSON and never decode. Reversing that order would
turn a hand-edited cache into a decode failure where CPython simply dropped the entry.

### 13.6 `_closest_aliases` breaks ties by the alias *string*, not the number

`sorted(known, key=lambda a: (abs(number(a) - index), a))` compares aliases as strings on a tie,
so with `a1..a12` around `a6` the distance-4 pair sorts `a10` before `a2` and the printed answer
is `a3 a4 a5 a6 a7 a8 a9 a10 …`, not `a2 …`. The port reproduces it and
`test/aliases.test.ts` asserts that exact string — it looked like a bug and is not.

### 13.7 `resolveAliasArguments` needs a fetcher to refresh at all

CPython read `--no-refresh` off the namespace and otherwise always drained. The port takes
`{ noRefresh?, fetch? }`, and **an absent `fetch` behaves exactly like `--no-refresh`**: the
plain "action aliases die with their revision" refusal stands and nothing is sent. Callers that
must refresh (U13 `batch`, U16 `do`) pass U11's drain; callers that never do (read-only paths)
pass nothing. `test/alias-refresh.test.ts` asserts both forms produce the identical refusal.

### 13.8 Cast-free JSON round-trip

`.v2-state` is JSON on disk and typed in memory, and PLAN forbids unchecked casts. The port keeps
a mutable `Draft` narrowed by the *same* validators CPython ran at load
(`parseActionAliases`/`parseEntityAliases`/`parseTileAliases`/`parseDrainedActors`), and writes
back through explicit serializers (`revisionJson`, `descriptorJson`, `scopeJson`,
`aliasEntryJson`, `pendingJson`). `toJsonValue` is the one general walk, used for the receipt
envelope; it *proves* the value is JSON at runtime rather than asserting it. `jsonEquals` is
CPython's key-order-insensitive `dict ==`, because `state["actions"].get(id) == descriptor` is a
load-bearing comparison in four places.

### 13.9 The generic refusal stops where CPython delegated (round-2 review fix)

`_validate_alias_state` and `_validate_pending_catalogs` do **not** wrap the helpers they call.
`_validate_revision` (client.py:1655 and 1573), `_validate_cursor_expiry` (1615) and
`_validate_descriptor` (1620) raise their *own* `PlayerError` straight past the caller, so the
user reads the drifted field, not the table's generic sentence. Round 1 mapped all four to the
generic sentence via `Effect.mapError(…, () => playerError(INVALID))`; round 2 propagates
`drift.message` instead, at `src/services/aliases.ts:272` and
`src/services/pending-catalogs.ts:147,171,175`. The four sentences were read off `python3`
running `client.py`'s own validators and are pinned as golden strings in `test/aliases.test.ts`
("a drifted field is named, not swallowed by the generic sentence"):

| input | sentence |
| --- | --- |
| `action_aliases.state_revision` missing `state_token` | `invalid state revision: missing state_token. Expected exactly revision, state_token, turn` |
| the same drift inside a staged catalog | *(identical)* |
| a staged descriptor with drifted fields | `invalid legal action descriptor: missing … ; unexpected nope. Expected exactly …` |
| unparseable `cursor_expires_at` | `invalid v2 page cursor expiry` |

Structural drift — the checks those two functions make *themselves* — still prints
`private v2 {action aliases,entity aliases,tile aliases,pending catalogs,drained catalogs} are
invalid`, and the same test pins that half so the two halves cannot drift into each other.

The mapping stays `DriftError → PlayerError` rather than widening the error channel, because
`V2StateSchemaApi.validate` (`src/services/session-store.ts:129`, core-owned) is typed
`Effect<void, PlayerError>`. `cli-main.handleError` prints both tags as `error: {message}` and
exits 2, so carrying the message across is byte-identical to carrying the tag and needs no edit
outside U03's files.

Two sibling `mapError` sites are deliberately left generic: `readPending`
(`aliases.ts:391`) and the staged-prefix decode in `rememberPage` (`aliases.ts:984`). Neither has
a CPython counterpart at all — `_remember_page_unlocked` does a bare `dict(pending["items"])`
with no re-validation — and both read a cache `_validate_pending_catalogs` already proved at
load, so they cannot fire. See 13.5.

### 13.9 The alias-semantics cap was a live instance of §12.1 (round 3)

U03 was the real-world case §12.1 warned about, and it was self-inflicted: the producer
(`_action_semantics`, `aliases.ts`) truncated to 1024 **code points**, while the loader
(`parseActionAliases`) measured the same cap with `semantics.length` — 1024 **UTF-16 units**.
The two disagree the moment one action's target name carries a non-BMP character.

The failure was not a cosmetic byte diff but a permanent brick. Ingest wrote a `.v2-state`
byte-identical to CPython's; the *next* command's load then refused it with
`private v2 action aliases are invalid`. Because `toDraft` calls `parseActionAliases` at the
top of every ingest, `rememberPage`, `rememberReceipt`, `aliasMap`, `freshActionAliases` and
`rememberDrainedActor` all exit 2 from then on — every v2 command dead until a human deletes
the private cache. Confirmed against `play/client.py`: for the truncated string CPython reads
`len() == 1024` and accepts, the port read `1025` and refused.

Both sides now share one notion of length — the loader uses `codeLength`, the producer
`codeSlice`, both from `src/services/mirror` per PORT_MAP §1. The `.length <= limit` fast path
kept in `truncateCodePoints` is sound in one direction only, and only that direction is used:
a string's UTF-16 length is never *below* its code point count, so `<= limit` proves
`codeLength <= limit`. Three tests in `test/aliases.test.ts` pin it: the round trip through
`rememberPage` for a page whose semantics is 1024 code points / 1025 units, and the hand-written
cache at exactly the cap (accepted) and one code point past it (refused). The first asserts the
two measurements actually disagree, so it cannot silently stop testing the divergence.

Audited the other four owned files for the same class: every remaining `.length` in U03 measures
an array or an object's key count, and every `.slice` runs on an alias key (`a1`, `u1`, `T(x,y)`)
whose grammar is ASCII. No other site caps or pads server-supplied text.

---

## 14. U05 — the wait engine and `wait`

### 14.1 Three constants that core does not carry

`V2_WAIT_S_MAX = 615`, `V2_WAIT_TICK_S = 15` and `V2_FOR_TURN_GRACE_S = 15` live at
`client.py:9992-10007`, past the last block PORT_MAP §0 assigned to `src/constants.ts`. They
are exported from `src/services/wait.ts` instead. The exit codes themselves were already in
`src/exit.ts` and are imported from there, so there is exactly one spelling of 0/75/66.

### 14.2 The engine renders nothing, and `echo` takes a health envelope

CPython's `_wait_until_turn` takes `echo: Callable[[str], None]` and calls
`_waiting_tick_line(wait["health"])` itself, which makes the engine depend on `_holder_seat`
and `_seat_label` — U06's functions. The port pushes the rendering out one level: the engine's
`echo` takes the `HealthEnvelope`, and `src/commands/wait.cmd.ts` passes
`(health) => echo(waitingTickLine(health))`. Same bytes, same order, and
`src/services/wait.ts` imports no renderer, so the wave-1 engine was testable before U06
landed and stays testable without a terminal.

Everything else the engine needs from another unit arrives as a `WaitHooks` record —
`rememberPage` (U03), `mirrorPage` (U07 via U04's `_mirror_page` bridge), `mirrorHealth` (U04)
and `holderSeat` (U06). The hooks are `Effect`s with **no** requirements: the command captures
`PrivateFs`/`SessionStore` from the CLI's Layer stack and provides them back per call
(`liveWaitHooks`). That is why `waitValue`/`waitUntilTurn` need only `V2Client | SessionStore`,
and why a test can drive the whole `--for-turn` loop with no filesystem at all.

`waitUntilTurn`'s `mirror` option is CPython's own override (the monitor writes its own marker
and nothing else); it shadows `hooks.mirrorHealth` exactly as the Python's does.

### 14.3 `mirrorPage` is wired (round 3) — and "one stale section" understated it

**Rounds 1–2 shipped `liveWaitHooks.mirrorPage` as `() => Effect.void`, because
`src/services/mirror/update-page.ts` is U07's row and had not landed. U07 has landed, and
PORT_MAP §8 assigns this call site to U05, so the hook is now the real
`mirrorPage(sessionPath, page, 'wait', { aliases })`.**

`_legacy_wait_value` (client.py:9976-9977) runs `_remember_page` *and*
`_mirror_page(path, cached, overview, "wait")` on the overview page it polled, with
`aliases=_alias_map(state)` (client.py:3016-3027) read from the dict `_remember_page` has just
folded the page back into. The port re-reads the persisted state inside the hook, which is the
same value for the same reason U12 gives in §U12.3: the ingestion is written to disk before the
engine calls the hook, and the request lock is held across both.

The seam is reachable: a supervisor that predates the private `/wait` route answers
`GET /v2/games/{id}/me/wait` with a bare `{"error": "..."}` 404 — the exact shape
`isMissingRouteRefusal` detects — and `play wait --until revision` then polls `/health` and
`/state?section=overview&limit=16` locally. The wake bytes and the exit status are identical
either way, which is why the inert hook was invisible in every stdout assertion; the divergence
lands in the *next* command, which reads a `state/overview.tsv` (and a `state/delta.md`) still
stamped at the baseline revision instead of the one the wake carried. `turn --end --await`
takes the same fallback through the same engine and, since round 2, already mirrored — so the
two commands left different mirrors for the same wake.

`test/pvp-wait-interop.test.ts` §`liveWaitHooks` now drives the whole fallback end to end
against a real scratch mirror and asserts `state/overview.tsv` opens `# rev 13 turn 5`.

### 14.4 The clock is injected, because the thing under test is how long it blocks

`WaitClock` (`monotonic`/`sleep`) is a field of `WaitCtx`, defaulting to `performance.now()`
and `Effect.sleep`. The Python tests patch `time.monotonic` and drive a responder that advances
the clock by whatever `wait_s` the query asked for; `test/wait.test.ts` and
`test/pvp-wait-interop.test.ts` port that `clocked` harness verbatim on top of the injected
clock. Without it, `--for-turn`'s "waited out the 40 s deadline plus one 15 s grace, and not a
second longer" assertion is a 55-second test.

### 14.5 `_wait_value`'s refusal order is load-bearing

`wait-s` bound → `poll-s` bound → the stateless/phase-mode check → the `.v2-state` read →
`--until` membership → the `--until revision` baseline. `test_a_stateless_wait_is_refused_
outside_phase_mode` passes `until="revision"` *and* `stateless=True` and asserts the
phase-mode sentence, which only holds if the stateless check runs before the membership check.
The port keeps that order and `test/wait.test.ts` asserts each sentence separately.

### 14.6 `--until` is not an `Options.choice`

argparse declared `choices=("phase", "revision")` and exits 2 with a usage document for
anything else. The stub `cli-main` landed uses a plain text option, and the port keeps it: the
refusal then comes from `_wait_value`'s own `wait --until must be phase or revision` (still
exit 2, still on stderr) rather than from argparse's usage block. Stdout is unchanged; the
stderr text differs from CPython's usage dump for one malformed flag value.

### 14.7 The negative-deadline floor is unreachable

`_holder_remaining_s` does `max(0.0, float(remaining))`, but `_safe_number` already refuses a
negative `remaining_s`, so no validated health can carry one. The port keeps the floor (a
defensive `Math.max(0, …)`) and the test asserts the reachable case — a holder pinned at zero —
rather than manufacturing an envelope validation would have rejected.

### 14.8 The wake-reason → exit-code table is required to be total

`test/wait-exit-codes.test.ts` asserts that the table's reasons are exactly `V2_WAKE_REASONS`
and that its satisfied subset is exactly `V2_SATISFIED_WAKE_REASONS`. A reason the supervisor
adds and this table does not name is then a failing test rather than a silent 75 — which is
the precise failure `boundary_recovered` caused before it was added.
`test/pvp-wait-interop.test.ts` additionally diffs `V2_WAKE_REASONS` against the served
`play/docs/full-control-v2.openapi.json` enum, so client and contract cannot drift apart.

### 14.9 `mirrorHealth` must be passed `V2_PROTOCOL_CARD`, and the live hook record is tested

`_mirror_health` (client.py:3068-3072) is a bridge with **no** command-card parameter: it
passes `commands=V2_PROTOCOL_CARD` to `state_mirror.update_from_health` unconditionally. The
port made `commands` an *option* on `mirrorHealth` (U04, `src/services/mirror/phase-marker.ts`),
which defaults to `DEFAULT_COMMAND_CARD` (`_DEFAULT_COMMAND_CARD`, 5 lines) when absent — so a
caller that forwards nothing silently writes a `state/header.txt` missing the 20-line card's
ALIASES / ERRORS / ONE CALL PER TURN / MULTIPLAYER / WHICH BINDING block. That reaches stdout:
`show header` maps to that file (client.py:11170), and `waitUntilTurn` rewrites it on **every**
tick of a `--for-turn` wait. `liveWaitHooks` now passes `{ commands: V2_PROTOCOL_CARD }`.

Two sibling call sites still drop it and are **not** this unit's to fix:

- `src/commands/turn.cmd.ts:150-151` (U12's `waitHooks.mirrorHealth`) — same seam, same effect.
- `src/commands/start.cmd.ts:287` — `command_start`'s `_mirror_health(…, "start")` also carries
  the card in CPython.

The durable fix is U04's: make `mirrorHealth`'s `commands` **default** to `V2_PROTOCOL_CARD`
rather than to `DEFAULT_COMMAND_CARD`, matching `_mirror_health`'s signature, and leave
`DEFAULT_COMMAND_CARD` reachable only through `updateFromHealth` (which is where
`state_mirror.py`'s own default lives). Until then every `_mirror_health` caller must remember
the option.

The round-1 miss was invisible because every `play wait` test injected
`waitCommandWith(() => Effect.succeed(kit.hooks))` and nothing exercised the record the shipped
binary uses. `test/pvp-wait-interop.test.ts` now has a `liveWaitHooks` describe block that runs
the real hooks against a real scratch mirror and asserts every `V2_PROTOCOL_CARD` line lands in
`state/header.txt` — and that no `DEFAULT_COMMAND_CARD` line does — which is the TS half of
`test_v2_join_card_and_state_header_carry_the_same_contract` (tests/test_client.py:7194-7254).
`runCommandWith` takes the hook factory now, so the live record is driven end to end through
`Command.run` and not just called directly.

## 15. U08 — the mirror map and yields writers

### 15.1 A blank square is not a fogged square

`_render_map` draws the bounding box of the *merged* grid, and a coordinate inside it that no
page has ever covered renders `grid.get((x, y), " ")` — a **space**. `?` is reserved for a tile
the seat has looked at and seen nothing (`visibility == "unknown"`). The two are different
claims and the byte-diff sees the difference, so the port keeps them apart and
`test/mirror-map.test.ts` pins a partially-explored board whose middle row is nothing but
spaces.

### 15.2 Grid rows are not right-stripped

`_table_text` right-strips every line it writes; `_render_map` does not. A row whose last
columns are unproven keeps its trailing spaces, which is what keeps column *x* under column *x*
across rows of different reach. The negative-coordinate golden (`   -1 |Tj `) exists mainly to
pin that trailing space.

### 15.3 The terrain legend is ordered by name, filtered by uppercase code

`sorted(legend.items())` orders by terrain *name*, not by code, so the line reads
`# terrain E=123 · B=Deep Ocean · O=Ocean · D=deep water · N=desert` — CPython's code-point
order puts digits before uppercase before lowercase, and JS `<` on ASCII agrees. The filter is
`char in {c.upper() for c in grid.values()}`, so a terrain visible only as a remembered
lowercase `d` still prints `D=Desert`.

### 15.4 `size or "size unknown"`, not `size is None`

`_map_size` returns whatever `state/overview.tsv` holds in the `map` row, having already
rejected `-`. CPython then falls back on falsiness, so an empty cell degrades to
`size unknown` too. The port spells that as `size === null || size === ''` rather than `??`,
which would have let an empty string through.

### 15.5 `_update_yields` never reads its `inner` argument

CPython threads the page envelope into `_update_yields` and never touches it: the note is
always `yields N/N complete` computed from the merged row count, never from `total_items` or
`next_cursor`. The port drops the parameter (PORT_MAP §7). One consequence worth stating: a
paged `city_citizens` section always claims to be complete, even mid-drain, because the overlay
counts what it holds rather than what the server says exists.

### 15.6 Two shapes are silently skipped, one is fatal

In `_update_yields` a non-object item raises (`city citizen item is not an object`), but a
`kind != "tile"` item, a non-string `tile_id` and a non-Mapping `yields` are all skipped without
a word — and a page that skipped everything writes **no file at all**, not an empty table. In
`_tile_chars` the polarity is reversed: a tile item without integer coordinates is fatal
(`tile item carries no coordinates`), so a malformed tiles page leaves `state/map.txt` untouched
rather than half-drawn.

### 15.7 `worked` is an identity check

`"yes" if _dig(item, "worked") is True else "no"` — a truthy string, `1`, or a missing key all
render `no`. The port keeps `=== true`.

### 15.8 The delta entry counts the file, not the page

`_update_map` re-parses the text it just wrote to compute `terrain known: N -> M tiles`. It has
to: a tiles page is a window merged over the grid on disk, so the page's own item count answers
a different question. The port does the same round trip rather than tracking the count in
memory, which also proves the writer's output survives `parseMap`.

### 15.9 Typecheck residue at U08's landing

`bunx tsc --noEmit` is clean for U08's three files and `test/mirror-map.test.ts`. The only
errors in the tree at landing time are in `src/services/pregame.ts` (U18, mid-landing): a
missing `src/render/pregame` module and an error-channel widening at `pregame.ts:112`. Neither
is U08's to fix.

### 15.10 A dict lookup is not a property read (round 2, review fix)

`_update_yields` spells its alias lookup `(aliases or {}).get(tile_id, tile_id)`. A Python dict
sees **own keys only**; the port's first cut was `aliases?.[key] ?? key`, which walks
`Object.prototype`. `tile_id` and `city_id` are unvalidated wire strings — CPython asks
`isinstance(str)` and nothing more — so a server-chosen id of `toString` / `constructor` /
`valueOf` / `__proto__` / `hasOwnProperty` resolved to the *inherited* member and `?? key` never
fired. CPython wrote `toString` into `state/yields.tsv`; the port wrote
`function toString() { [native code] }`: different bytes, a different column width for the whole
table, and a merge key that neither the second city's page nor `render_map_yields`' `T(x,y)`
lookup could match again. `aliased` is now `Object.hasOwn(aliases, key) ? (aliases[key] ?? key)
: key`, and `test/mirror-map.test.ts` pins all five names through both `yieldRows` and the file
round trip, including the case where `toString` *is* an own key and must still alias.

This is the permissive-wire-decode trap PLAN names, in its JS-specific form: any place a
server-controlled string reaches a property read is a prototype-chain lookup unless it is a
`Map` or guarded by `Object.hasOwn`.

**Cross-unit, not U08's to fix:** `aliasMap` in U04's `src/services/mirror/store.ts:271` has the
identical shape (`aliases[text]`, where `text` is `cell(identifier)` over a wire id) and feeds
every entity renderer's alias column. U04 should apply the same `Object.hasOwn` guard.

## 16. U07 — the mirror entity renderers, `update_from_page`, `update_from_receipt`

### 16.1 One new file: `src/render/mirror/section.ts`

CPython's renderers return a `(columns, rows)` tuple that `_table_text` destructures. Seven
renderer modules plus the dispatch need one name for that pair, and none of them is a natural
home for it, so it lands in a types-only module (PORT_MAP §8). It carries no runtime code and
imports nothing from `src/render/mirror/`, so the `import type` edges are erased and the
dependency graph stays a tree.

### 16.2 The renderers fail; they never blank

`_render_units` raises on an item that is not a Mapping rather than writing a row of dashes,
and the same is true of every section. That is the drift contract from the module docstring: a
contract change surfaces as a missing row or a refusal, never as a silently plausible value.
The port keeps the failure in the error channel (`PlayerError`, message prefixed
`state mirror: `), which is why `SectionRenderer` returns an `Effect` rather than a plain pair.
The refusal is what `mirrorGuard` then turns into a stderr warning — a projection never fails
the command that produced it.

### 16.3 `_MISSING` versus `null` is load-bearing in the overview

`add()` writes a row only when the key was *present*; `player: null` and `research: null` get
their own `(none yet)` rows, and an absent `player` key gets nothing at all. Two different
statements about the world, two different projections. `dig` returning the `MISSING` symbol
(U04) is what keeps them apart, so no renderer may use `?? null` to normalize an absent key.

### 16.4 Numbers: two CPython behaviors JavaScript cannot distinguish

`_render_overview`'s `isinstance(exact, int)` and `_target_text`'s `isinstance(x, int)` are
false for a JSON float that happens to be integral (`19.0`, `31.0`); in JavaScript `19.0` *is*
`19`, so the port's `Number.isInteger` accepts it. The reachable consequence is that a server
that sent `"exact": 19.0` would print `score 19` here and `score >=17` under CPython, and a
tile target at `31.0,72.0` would print `31,72` here and fall through to the name under CPython.
Both are the *better* answer and neither is reachable from a validated payload (`safeNumber`
already pins these fields), but it is a divergence, so it is written down. This is the same
residue NOTES §2 records for `_cell`.

`_action_flags`' `probability != _DEFAULT_PROBABILITY` is Python dict equality: exact key set,
values compared numerically (`100.0 == 100`). The port compares the key count and each value
with `===`, which agrees on every JSON shape.

### 16.5 The merge rule differs between a section table and a catalog file

A section merges on **column 0** (the alias/id) and replaces when the page is terminal *and*
`total_items == len(items)`. A catalog file merges on **everything but column 0** — two
capabilities with the same kind, target, args, label and flags are still two rows with two
different `aN` names, and collapsing them would hide one — and replaces only when the page is a
terminal, actor-wide (`target_id` absent) catalog covering its own `total_items`. A
`--target_id` catalog is a narrower question, so even a complete answer to it merges.
`test/mirror-options.test.ts` pins all four corners.

### 16.6 The page note counts items for `overview` and rows for everything else

`shown = len(items) if section in _REPLACE_ONLY else len(rows)`. `overview` projects one item
into ~20 fact rows, so counting rows would print `overview 20/1`. Counting rows everywhere else
is what makes the note honest after a merge: `units 2/2 complete` from two windows of one.

### 16.7 `update_from_page` re-validates the "already validated" payload

CPython takes `client._validate_page`'s output and still checks `state_revision`, the `page`
envelope, `section`, `items` and the `_MAX_ROWS` cap. The port keeps all five checks and types
the parameter `unknown`, so a caller cannot skip them by holding a `PageEnvelope`. The three
refusal sentences (`payload carries no state revision counters`, `page payload carries no page
envelope`, `page envelope carries no section items`, `page envelope carries too many items`)
are ported verbatim.

### 16.8 Oracle: the projections are byte-identical to CPython

Beyond the ported unit tests, three scripted ingest plans — 10 pages plus a receipt, then a
catalog stage/promote/target-scope/stale sequence, then the tiles and `city_citizens` sections
— were run through `play/state_mirror.py` and through this port into two scratch directories
and diffed with `diff -r`. All nine to eleven files per run, `state/delta.md` included, are
byte-identical, including column padding, the `~` truncation of a 80-character unit type, the
`u1`-keyed duplicate rows a replacing page leaves behind, and the terrain legend U08 writes.

### 16.9 U08 landed mid-round; the map/yields seam was removed

`update_from_page` dispatches `known_tiles`/`map_tiles`/`tile_window` to `updateMap` and
`city_citizens` to `updateYields`. Those two files were unlanded when this unit started, so the
dispatch briefly took them as injected hooks; U08 landed during the round and PORT_MAP §7
directs U07 to import them directly, so the seam is gone and the imports are direct.
`updateYields` is called without CPython's unused `inner` argument, per §7.

### 16.10 Typecheck residue at U07's landing

`bunx tsc --noEmit` reports nothing in U07's twelve files. The errors in the tree at landing
time are in `src/commands/legal.cmd.ts`, `src/commands/turn.cmd.ts`, `src/services/legal-drain.ts`,
`test/start.test.ts` and `test/state-render.test.ts` (U10/U11/U12/U18, mid-landing), and the
13 failing tests are all in `test/start.test.ts` and `test/state.test.ts`. Nothing outside
U07's row imports U07's modules yet, so none of that is U07's to fix — `src/commands/state.cmd.ts:130`
still carries the "U07 has not landed" comment its owner will now remove.

---

## 16. U18 — `start` and the pregame catalog

### 16.1 `_phase_aware_refusal` is exported, but `start` is not wrapped in it

The unit brief says to wrap the whole `start` command in `_phase_aware_refusal`. CPython does
not: the context manager has exactly two call sites, `command_legal` (client.py:8284) and
`command_batch` (8549), and `command_start` uses neither. PLAN §3.5 says the Python wins, so
`runStart` is unwrapped and `phaseAwareRefusal` is exported from `src/services/pregame.ts` for
U11 and U13 to import.

`start`'s own answer to "the game has already left pregame" is its `game_state != "lobby"`
refusal — `just start configures a lobby seat; this game is running -- run \`just turn\`` —
which is asserted in `test/start.test.ts`. The combinator itself is asserted directly in
`test/pregame.test.ts`, including the `V2ResponseError` case: CPython's `V2ResponseError`
subclasses `PlayerError`, so the `except PlayerError` catches a validated wire refusal too and
re-raises it as a *plain* `PlayerError`. That trades the refusal body `cli-main` would have
rendered from the payload for the phase sentence. The port keeps the trade rather than
inventing a third shape.

### 16.2 `--nation/--leader/--style` take a display name, never an id

The brief says these flags "accept either a display name or an id". `_pregame_choice`
(client.py:10905-10911) compares `item["name"].casefold()` and nothing else, so an opaque id is
just an unknown name and is refused with the near-miss list. `test/start.test.ts` pins the
divergence with an explicit case.

`--leader` is not matched against a catalog at all: it is a free-text seat label, bounded by
`V2_LEADER_MAX_BYTES` and sanitized by `_sanitized_leader`.

### 16.3 Five cross-unit seams are injected, not imported

`start` is the most cross-unit command in wave 2: it needs U07's `update_from_page`, U11's
legal drain and kind resolution, U13's batch persist/submit and U14's disposition render. None
had landed. Rather than block, `src/services/pregame.ts` declares a `StartHooks` record and
`src/commands/start.cmd.ts` exports `startCommandWith(makeHooks)` / `liveStartHooks`, exactly
the seam U05 used for `wait` (§14.3).

`liveStartHooks` today: `mirrorPage` inert (U07), `choose` a real uniform draw, `receiptOk` the
local `orderReceiptOk`, and `resolveKindAction` / `drainLegal` / `persistBatchForAction` /
`submitPersistedBatch` / `renderDisposition` refusing with
`start cannot … in this build: Uxx owns that seam and has not landed yet` — the same shape as
`cli-main`'s `pending()` stubs. **Integrator:** swapping that one factory is the whole
integration; nothing else in U18 changes.

`test/start.test.ts` supplies hooks that talk to the same fake supervisor, so the *request
order* the Python test actually guards — `health, nations, [overview], legal, batch, legal,
batch`, with the mandatory re-enumeration between configure and set_ready — is asserted against
real traffic rather than stubbed away.

### 16.4 Three small functions were ported onto U18's row

`orderProperties` (`_order_properties`, 8801) and `defaultArguments` (`_default_arguments`,
8946) are on U15's row; `orderReceiptOk` (`_order_receipt_ok`, 9483) is on U14's. All three are
pure, under fifteen lines, and `_check_pregame_arguments` / `command_start` are written
directly in terms of them. They live in `src/services/pregame.ts` so this unit compiles and
tests standalone; see the PORT_MAP amendment. When U14/U15 land, the integrator collapses each
to a re-export — the two definitions must not drift.

### 16.5 `exit_code` travels as `exitWith`, not as a `PlayerError`

`command_start` returns whatever `_submit_persisted_batch` returned (0, or 2 when the transport
outcome is unknown). CPython has already printed that case's warning on stderr, so the port
fails with `exitWith(2)`, which prints nothing, rather than a `PlayerError` that would add a
second `error: …` line for one event. `runStart`'s error channel is therefore
`PlayError | ExitCodeSignal`.

### 16.6 `casefold`, `sorted` and `repr`, approximated

Three CPython primitives have no exact JS equivalent, and none of them can reach a real
divergence on this data:

- `str.casefold()` → `toLowerCase()`. They differ on `ß`/`ﬁ`-class folds; a Freeciv nation or
  style name is Latin-script title case.
- `sorted()` compares code points, `Array#sort` code units. They differ only above U+FFFF.
  `compareStrings` is explicit about which order it implements, and the sort is made stable by
  carrying the original index, because `_pregame_default_nation`'s draw is over the sorted list.
- `{wanted!r}` → `pyRepr` in `src/render/pregame.ts`: single quotes unless the text contains one
  and no double quote, backslash/quote/`\n\r\t` escaped, C0 and DEL as `\xNN`, everything else
  verbatim (Python 3 `repr` does not escape printable non-ASCII).

### 16.7 The mirror re-read fails closed on the whole projection

`_mirror_pregame_catalog` returns `[]` — not a partial list — the moment anything is off: no
`# … complete` note, fewer than two body lines, the first two columns not `id` + the section's
own column, a row whose cell count does not match the header, or an id that lost its prefix,
its shape or its length to truncation. A half-catalog would silently narrow what the agent can
choose, and the cost of being wrong is one `/state` request.

### 16.8 Typecheck residue at U18's landing

`bunx tsc --noEmit` is clean for `src/commands/start.cmd.ts`, `src/services/pregame.ts`,
`src/render/pregame.ts`, `test/start.test.ts` and `test/pregame.test.ts`. The errors present in
the tree at landing time are in `src/commands/do.cmd.ts`, `src/commands/legal.cmd.ts`,
`src/commands/monitor.cmd.ts` and `src/commands/turn.cmd.ts` (U16/U11/U17/U12, landing
concurrently). None is U18's to fix.

### 16.9 U18 `start` imports `V2_PROTOCOL_CARD` for its health mirror (round 2, review fix)

`command_start`'s first act is a health probe it mirrors (client.py:11033). CPython routes that
through `_mirror_health` (3062-3072), which hardcodes `commands=V2_PROTOCOL_CARD` on **every**
call — there is no code path that mirrors health with a different card. The port's
`updateFromHealth` (`src/services/mirror/phase-marker.ts:253`) instead takes the card as an
optional argument and falls back to the five-line `DEFAULT_COMMAND_CARD`, so the argument is
mandatory at each call site rather than implied by the function.

`start` was dropping it, and the header it wrote carried only
`just turn / just state / just legal / just batch / just wait` instead of the 25-entry card
(ALIASES…, `just start`, ONE CALL PER TURN, MULTIPLAYER, WHICH BINDING, …). `state/header.txt`
is printed verbatim by `just show` and `just show header` and sits on the offline byte-diff
oracle's read-only path (PLAN §The oracle item 2), so this was a direct stdout divergence, not a
cosmetic one. Fixed: `src/commands/start.cmd.ts` now passes
`{ commands: V2_PROTOCOL_CARD }`, matching `src/services/turn-end.ts:324`,
`src/commands/turn.cmd.ts:191` and `src/commands/wait.cmd.ts:86`.

**This is a declared cross-unit seam.** Per §12.2 and PORT_MAP §0's addendum, `V2_PROTOCOL_CARD`
has exactly one definition, in U02's `src/render/join.ts`, and every consumer imports it —
U18 now among them. Re-typing the card in a unit that needs it is a test failure waiting to
happen, because `test_v2_join_card_and_state_header_carry_the_same_contract`
(tests/test_client.py:7194-7254) asserts the join card and `header.txt` carry the identical
lines. `test/start.test.ts` pins the header contents against `V2_PROTOCOL_CARD` and asserts no
`DEFAULT_COMMAND_CARD` line survives, the same shape `test/pvp-wait-interop.test.ts` uses for
`wait`.

---

## 17. U13 — `batch`, the canonical body and batch persistence

### 17.1 `_parse_json_object` emits CPython's syntax error, not the host's (round 3)

**Rounds 1–2 shipped V8's sentence here and told the oracle to skip the case. Round 3
removed both.** `JSON.parse` is gone from `parseJsonObject`; `parsePython` is now a full
port of CPython's JSON scanner including its error text, so
`--arguments must be valid strict JSON: {exc}` carries the message CPython carries:

| `--arguments` | CPython, and now the port |
| --- | --- |
| `{"a":}` | `Expecting value: line 1 column 6 (char 5)` |
| `{"a":01}` | `Expecting ',' delimiter: line 1 column 7 (char 6)` |
| `{"a":1}x` | `Extra data: line 1 column 8 (char 7)` |
| `{"a":NaN}` | `non-finite number NaN` |
| `{"a":"\q"}` | `Invalid \escape: line 1 column 7 (char 6)` |
| `{"a":"a<TAB>b"}` | `Invalid control character at: line 1 column 8 (char 7)` |
| `{"a":1,}` | `Illegal trailing comma before end of object: line 1 column 7 (char 6)` |

Three things the port has to get right that a naive reading of `json/decoder.py` does not:

1. **The C accelerator is what runs.** `import json` binds `_json.scanstring` and
   `_json.make_scanner`, and the C versions differ from the pure-Python fallback on two
   messages: `Invalid \escape` is reported at the *backslash* and without the offending
   character's `repr` (the fallback says `Invalid \escape: 'q'` one character later), and
   `Invalid control character at` likewise drops the `repr` and reports at the control
   character. The port matches C.
2. **Positions are code points.** `JSONDecodeError` counts Python string indices, so an
   astral character is one. The scan therefore runs over `[...text]`, not over UTF-16 units:
   `{"a":"😀"x}` is `char 8`, not `char 9`.
3. **A `\uXXXX` that ends the document is `Invalid \uXXXX escape`, not
   `Unterminated string`.** The C scanner wants a character *after* the four digits, so
   `{"a":"A` (no closing quote) reports the escape, at the `u`.

Order of refusals follows CPython's control flow rather than a checklist: `object_pairs_hook`
runs when an object *closes*, so `{"a":1,"a":2}x` is a duplicate key (the hook raises before
`Extra data` is ever reached) while `{"a":1,"a":2,}` is the trailing comma (the scanner fails
before the object closes). `parse_constant` raises as the token is scanned, so it beats a
duplicate later in the same object.

`PlayerError` is a `RuntimeError`, so the `object_pairs_hook`'s refusal is *not* caught by
`_parse_json_object`'s `except (json.JSONDecodeError, ValueError)` and keeps its own sentence
(`--arguments must not contain duplicate keys`). Keys are compared after unescaping, so
`{"name":1,"name":2}` is a duplicate. A literal `__proto__` key is defined with
`Object.defineProperty`, so an argument named `__proto__` stays an argument.

**Version pin.** `Illegal trailing comma before end of {object,array}` and the value suffix
on `Out of range float values are not JSON compliant: inf` (§17.10) are **CPython 3.14**
message texts; 3.13 and earlier say `Expecting property name enclosed in double quotes` and
omit the suffix. The port is pinned to the `python3` that runs `play/client.py` on this
machine (3.14.6). If the reference interpreter ever moves back, these two goldens move with
it — everything else in the table is stable across versions.

**The oracle no longer skips anything here.** Rounds 1–2 instructed the byte-diff oracle not
to run `--arguments` syntax errors as goldens. That exemption is withdrawn: the port was
cross-checked against `python3` over a 128-case hand-built corpus, a 7,513-case mutation fuzz
(seeded JSON with random insert/delete/substitute/truncate edits plus random token soup) and
a 12,000-case escape fuzz (random `\uXXXX`/surrogate/control-character strings in keys and
values), comparing the full refusal sentence *and* the canonical body of every document that
parsed. All 19,641 agree. The harness is not checked in; it is one `parseJsonObject` +
`canonicalText` call per case against the same three functions `_parse_json_object` composes.

### 17.2 U14's renderers are imported now, not copied (round 2)

`_render_disposition`, `_render_receipt`/`_receipt_line` and `_order_receipt_ok` are on U14's
row (PORT_MAP §0). U13's first landing carried byte-identical copies
(`fallbackRenderDisposition`, `fallbackRenderReceipt`, `fallbackOrderReceiptOk`) because
without them `batch` had no default (non-`--json`) output at all and U14 had not landed.

**U14 has landed, so the copies are gone.** `liveBatchHooks` now takes `renderDisposition`
from `src/render/receipt.ts` and `orderReceiptOk` from `src/services/disposition.ts`, and
`test/batch.test.ts` asserts the three disposition-line shapes through U14's function. The
goldens did not move, which is the point: the copies were byte-identical *that day*, and the
reason PORT_MAP forbids them anyway is that nothing keeps them identical the next day.

Still inert, and still integrator steps 2 and 4: `fetchLegal` (U11's drain — a stale `aN`
keeps its plain refusal until it is wired), `orderActor` (U15), `refusedActorOptions` (U16's
`src/render/actor-options.ts`, which wants a `RefusedActorOptionsIo` the caller assembles)
and `nextFocusLine` (U12's `src/services/decisions.ts`). Each costs one *additional* line of
guidance on a refusal and nothing that changes an outcome or an exit code.

`_mirror_receipt` is the same story with no byte cost: `submitBatch` takes a `ReceiptMirror`
and defaults to `inertReceiptMirror`, because CPython's `_mirror` swallows every projection
failure with a stderr warning ("a projection never fails a command"). The mirror bridge is
U04/U07's; until it is wired in, `batch` writes no `state/` projection and stdout is unchanged.

### 17.3 `_phase_aware_refusal` is U18's, and `batch` now imports it (round 2)

It sits at client.py:10802-10817, inside U18's span, and wraps two commands — `command_legal`
(U11) and `command_batch` (U13). U13's first landing carried a local copy because U18 had not
landed; U18 has landed, so `src/commands/batch.cmd.ts` imports `phaseAwareRefusal` from
`src/services/pregame` and the copy is gone, per PORT_MAP §7's "U11 and U13 import it; they do
not reimplement it".

The copy was not merely redundant, which is the argument for the rule. It matched
`_tag === 'PlayerError'`, and CPython catches `PlayerError` — the class `_opaque`
(client.py:1272-1275) and `_private_advisory_lock` (532-563) also raise, and which the port
had re-tagged `DriftError` and `LockTimeoutError`. `play batch --action-id ""` (or any value
`OPAQUE_ID_RE` rejects) after the phase ended is exactly the anti-loop case the Python comment
names, and the copy printed one stderr line where CPython prints two. U18's version catches
every `PlayError` and re-raises a plain `PlayerError` carrying the joined text — literally
`raise PlayerError(f"{note}\n{exc}")` — including the `V2ResponseError` trade CPython makes
(the payload render is dropped in exchange for the phase sentence; see §U18).

**`ExitCodeSignal` must never reach it**, and now cannot: it is not a refusal but the quiet
exit code of a *reported* disposition, and prefixing it would invent an `error:` line the
Python never prints. `runBatch` returns the code out of the wrapped block instead of failing
inside it, which is also what `_batch_command` does — the `with` block wraps the body, and the
exit code is a `return`, not a raise. `test/batch.test.ts` asserts all of it against a stalled
phase: the two-line refusal on an invalid action ID, the untouched refusal when the mirror
does not claim the phase is dead, and the silent exit 2 of a refused disposition.

### 17.4 `_limit` lives in `src/services/batch.ts`

The unit brief lists client.py:6513-6543 (`_limit` **and** `_parse_json_object`) in U13's span,
but `_limit`'s callers are `state` (U10, client.py:7735) and `legal` (U11, 7840/7859) — U13
never calls it. It is exported as `pageLimit` from `src/services/batch.ts` because that is the
only file on U13's row it could go in. **U10 and U11 import it; neither restates the sentence.**
See PORT_MAP §8.

### 17.5 The brief's "validated against the descriptor's argument schema" is not what the
Python does

PORT_MAP §2 (U13) and the unit brief both say `--arguments` is "checked against the
descriptor's argument schema". `_batch_command` does no such check: it parses the JSON object
and `_persist_batch_for_action` embeds it verbatim, leaving argument validation to the server
(which answers `illegal_action` → `refresh`). Client-side argument checking exists only in
`_order_arguments` (U15, for `do`) and `_check_pregame_arguments` (U18, for `start`). PLAN.md
§"the Python wins" applies: the port does not add a check CPython did not have, because adding
one would refuse orders the supervisor would have accepted.

### 17.6 The persist-then-send order is asserted from disk, not from a mock

`test/batch.test.ts` proves the invariant the way the CPython test does — by failing the
transport — and additionally reads `.v2-state` off the filesystem *from inside the responder*,
at the exact moment the POST is attempted, and asserts the batch ID is already there. That is
the property "a process killed between the write and the response leaves a record `retry` can
resolve", stated as a filesystem fact rather than as a call order.

### 17.7 `secrets.token_urlsafe(24)`

`batchToken()` is 24 bytes from `crypto.getRandomValues`, base64url, unpadded — 32 characters,
the same shape and entropy CPython produces. The collision loop CPython writes as an unbounded
`while` is capped at 1024 attempts here and then refuses; the cap is unreachable in practice
and prevents a stubbed token in a test from hanging a suite.

### 17.8 Typecheck and suite residue at U13's landing

`bunx tsc --noEmit` reported nothing in U13's six files at landing; the errors then in the
tree were U16's, U11's, U18's and U17's, all mid-landing. **Round 2: still nothing in U13's
six files, and `bun test` is 1421/1421 green.** The one error left in the tree at the end of
this round is `src/render/show-regex.ts:682` (U09's `show --regex`, mid-edit while this round
ran) — not reachable from, and not caused by, anything on U13's row.

### 17.9 `int` is not `float`, and `--arguments` is agent input (round 2)

The first landing serialized the canonical body with core's `canonicalJson`, whose number
branch is `String(value)`. That is wrong on the one surface where wrongness costs a
duplicate mutation. Verified by running both encoders, for a value the agent types on the
command line:

| `--arguments` | CPython persists / POSTs | `String(value)` produced |
| --- | --- | --- |
| `{"tax":40.0}` | `{"tax":40.0}` | `{"tax":40}` |
| `{"a":1e16}` | `{"a":1e+16}` | `{"a":10000000000000000}` |
| `{"a":1e-7}` | `{"a":1e-07}` | `{"a":1e-7}` |
| `{"a":0.00001}` | `{"a":1e-05}` | `{"a":0.00001}` |
| `{"a":10000000000000000001}` | exact | `{"a":10000000000000000000}` — a **different order** |

Those bytes are the request body, the idempotency key the supervisor de-duplicates on, and
the record `retry` re-sends verbatim. The old `test/batch-persist.test.ts` asserted the
divergence as intended behaviour (`canonicalText({size: 3.0}) === '{"size":3}'`), which is
how it survived a round.

**The model.** `src/services/canonical-body.ts` now carries CPython's two numeric types
apart. `parsePython` is `json.loads` (round 3: it is the *only* parser — `JSON.parse` no
longer runs first, see §17.1) and routes
each literal the way `json.scanner` does — a fraction or an exponent makes a `PyFloat`,
anything else an exact `PyInt` (a `bigint`, so 20-digit integers survive). `pyDumps` is
`json.dumps(sort_keys=True, separators=(",", ":"))` over that model, delegating string
escaping to core's `encodeStringAscii` so there is still exactly one port of the escape
table. `pyFloatRepr` is `float.__repr__`: shortest round-trip digits (JavaScript's
`toExponential()` picks the same ones), fixed notation while the decimal point lands in
`(-4, 16]`, otherwise a signed exponent of at least two digits.

**The proof.** Beyond the golden tests, the implementation was diffed against `python3`
twice: 446 numeric literals (`json.dumps` *and* `_scalar` for each) and 500 random nested
documents with unicode, escapes, both `ensure_ascii` spellings and both key orders. Byte
identical in every case.

**`_batch_intent` needed the same fix, on stdout.** It re-parses the persisted text and
renders each argument through `_scalar`, whose `float` branch is `%g` and whose `int` branch
is `str()`. `render/primitives.ts`'s `scalar` cannot tell the two apart
(`Number.isInteger(value) ? String(value) : formatG(value)`), so a persisted `"tax":1234567.0`
printed `{tax=1234567}` where CPython prints `{tax=1.23457e+06}`. `batchIntent` now parses
with `parsePython` and renders with `pyScalar`; `render/primitives.ts` is untouched, because
every *other* caller of `scalar` is handed a `JsonValue` that lost the distinction upstream.

**Scope, and what is still open.** `PyObject` widens `parseJsonObject` and
`persistBatchForAction`; every `JsonObject` already is one, so U12/U16/U18 compile and behave
unchanged — an order built by U16's parser still serializes through the plain-number branch,
which prints an exact integer as an integer, exactly as CPython's `int` does. U06 imports the
same model for `health --json` (§2, §10.5). The general repair is core's: decode response
bodies in `src/services/http.ts` with `parsePython` instead of `JSON.parse`, and float-ness
survives from the wire for every surface rather than being reconstructed per field.

One display-order divergence is *not* fixed, deliberately: `_batch_intent` prints arguments
in the persisted (canonically sorted) order, and a JavaScript object re-orders keys that look
like array indices, so arguments literally named `"9"` and `"10"` would print `9,10` where
CPython prints `10,9`. The canonical *bytes* are unaffected (`pyDumps` sorts explicitly), and
no advertised action names its arguments with digits.

### 17.10 `_canonical_body` has **two** refusals, and the second was a fail-open (round 3)

`_canonical_body` is `json.dumps(...).encode("utf-8")` inside
`except (TypeError, ValueError)`. Rounds 1–2 ported only the `allow_nan=False` half and the
file's own header called it "the one refusal". The `.encode("utf-8")` half is the dangerous
one:

- `"\ud800"` **is** strict JSON, so `--arguments '{"name":"\ud800"}'` passes
  `_parse_json_object` and `_json_value` — `json.loads` decodes it to a Python string holding
  one lone surrogate.
- `ensure_ascii=False` (load-bearing, §17.9) copies it into the dumped text unescaped.
- UTF-8 cannot encode a surrogate. CPython raises `UnicodeEncodeError`, which **is a
  `ValueError`**, so `_canonical_body` re-raises
  `PlayerError("command batch is not canonical JSON: 'utf-8' codec can't encode character
  '\ud800' in position 163: surrogates not allowed")` from inside `_persist_batch_for_action`,
  *before* `_save_v2_client_state_unlocked`. Nothing is written; nothing is sent; exit 2.

JavaScript has no equivalent throw. `TextEncoder` substitutes `U+FFFD` (`ef bf bd`)
*silently*, and `fetch` does the same to a body string. So the old port persisted the batch,
POSTed it, and issued a mutation CPython refuses — carrying a value the agent never wrote,
and leaving a `.v2-state` record whose string is **not** the bytes that went out, which is
precisely the invariant the module docstring claims (`the persisted string *is* the request
body`) and the one `retry`'s idempotent resend depends on.

`canonicalText` now scans the dumped text and refuses first. Three details are goldens:

- **Order.** `allow_nan` is reached first — `json.dumps` raises while walking the value, and
  the encode only ever runs on a string it produced — so a body carrying both an `inf` and a
  surrogate names the `inf`.
- **Position** is a code-point index into the *dumped* text, so a `München` earlier in the
  body shifts it by 1 and not by 2, and an astral character counts once.
- **Plural form.** The codec batches a maximal run of surrogates into one error:
  one surrogate gives `can't encode character '\ud800' in position 6`, two or more give
  `can't encode characters in position 6-7`, with no `repr`.

`canonicalBytes` is no longer exported. It was the other half of the fail-open — a caller
could reach `TextEncoder` without the refusal — so the encoder now exists only inside
`canonicalBody`, behind `canonicalText`.

The `allow_nan` message also gained its value suffix
(`Out of range float values are not JSON compliant: inf`), which CPython 3.14 added; see the
version pin in §17.1.

## 15. U10 — `state` and the live-state renderers

### 15.1 The mirror projection (round 2: landed)

CPython's `command_state` runs `_v2_session → _resolve_alias_arguments → _state_query →
GET /state → _validate_page → _load_v2_client_state → _remember_page → _mirror_page → render`.
All of it is now ported. Round 1 left `_mirror_page` (client.py:7809) out because
`src/services/mirror/update-page.ts` is U07's row and had not landed; U07 landed mid-round and
PORT_MAP §7 assigns the call site to U10, so `runState` now calls
`mirrorPage(path, value, 'state', { aliases })` and the `NOTE(U07)` is gone.

Two ordering details are CPython's, not conveniences:

- The alias map is read from the state `rememberPage` just wrote, so the mirror files name
  rows exactly the way the render below them does. CPython evaluates `_alias_map(state)` as an
  *argument* to `_mirror`, i.e. outside the guard, so an unparseable alias table still fails
  the command; the port keeps that by computing `aliasMap` in the effect chain and letting only
  `mirrorPage` be total.
- The projection runs before the `--json` branch, because CPython projects on both paths.

The byte-visible consequence inside U10's own surface is the build-choice forfeit:
`cityBuildStock` only trusts a `state/cities.tsv` stamped at the page's own revision, and
before this change nothing in the TS `state` path ever wrote that stamp, so
`state --section city_build_choices` could only ever print `keep N`. `test/state.test.ts` now
drives the pair end to end — `state --section cities` then `state --section city_build_choices`
through `runState`, with no table staged by hand — and asserts the `stock 25 shields; …` header
and the `!forfeits 13 of 25 shields` note. `test/state-city.test.ts` keeps its hand-staged
tables for the stale/absent branches, which are what
`test_v2_build_choice_forfeit_appears_only_when_it_is_derivable` is really about.
`state --section city_citizens` likewise now drives `updateYields` → `state/yields.tsv`, which
is what `show map --yields` overlays.

### 15.2 `state` injects no `LegalPageFetcher`, so an `aN` alias keeps its plain refusal

`_resolve_alias_arguments` re-binds a stale *action* alias by draining a legal page unless
`--no-refresh`. `state` has no `--no-refresh` flag, so CPython always refreshes — but `state`'s
three alias-bearing flags are `--actor_id`, `--relation_id` and `--center_id`, and none of them
ever names an `aN`. U03's `resolveAliasArguments` only refreshes when a fetcher is supplied, and
U11's fetcher has not landed, so `runState` passes none. When U11 lands, the only observable
change would be for `state --actor_id a1`, which is already a refusal on both sides (an action
ID is not a city/unit/relation/tile ID and `_state_query` rejects it).

### 15.3 `_render_state_page` requires `PrivateFs` even when it never reads a file

`_city_build_stock` reads the `cities` mirror, so `renderStatePage` carries `PrivateFs` in its
requirement channel. Effect requirements are static, so the pure paths carry it too. The render
tests provide `privateFsFor({ root: '/nonexistent', … })`: the requirement is satisfied and no
path is ever touched, because `cityBuildStock` short-circuits on `sessionPath === null`.

### 15.4 The two `_mirror_*` helpers are re-declared privately, not imported

`_mirror_cell` and `_mirror_number` (client.py:7031-7041) are on **U12's** row. They are four
lines each and `_city_build_stock` is their only caller outside `turn`. They are declared
`const`, module-private, inside `src/render/state/build-choices.ts` rather than imported from a
file that does not exist. If U12 exports them, delete the private pair. `_mirror_table` /
`_mirror_is_fresh` are *not* re-declared: U04's `mirrorText` + `parseTable` already are them,
and the freshness check is one `turn`/`revision` comparison against `MirrorTable.revision`.

### 15.5 The city output table drops columns twice, in this order

`_city_output_rows` runs two independent eliminations and the order is load-bearing:

1. A column absent from any row, or zero on every row, is dropped — except `surplus`, which
   always survives because `+0` is a decision ("this city is treading water") and a blank is not.
2. *Then* the base→gross→net→surplus chain is walked **right to left**, dropping a step that
   equals the step before it on every row.

Walking left to right instead would drop `gross` before `net` had a chance to compare against
it, and the besieged-city golden (`base waste unhappy net used surplus`, where `gross == base`
everywhere but `net != gross`) is exactly the case that separates the two. `test/state-city.test.ts`
asserts both goldens as whole lines, padding included.

### 15.6 `_render_overview` reads `item['turn']` directly; the port reads it defensively

CPython would raise `KeyError` on an overview item with no `turn`. That path is unreachable —
`_render_section_items` only dispatches to `_render_overview` when every item carries
`turn`/`player`/`research`/`counts` — so the port renders `-` there instead of crashing. No
observable difference on any payload the dispatch admits.

### 15.7 `--limit ""` is treated as "not supplied"

argparse gives `--limit` no default, so `args.limit` is `None` when the flag is absent. The
`@effect/cli` stub `cli-main` landed uses `Options.text('limit').withDefault('')`, so the empty
string is what "absent" looks like. `runState` maps `'' → null` before `stateQuery` sees it.
CPython would reach `_limit("")` and refuse with `limit must be a canonical integer from 1
through 16`; the port treats it as absent. The only way to hit the difference is
`play state --section units --limit ""`, which no recipe and no card ever writes.

### 15.8 `if value:` is not `value !== ''` (round 3, review fix)

`_research_text`, `_score_text` and `_render_overview` filter on CPython's `if value:` in four
places — `research.target`, `research.goal`, `score.components` and `counts`. Round 2 spelled
that as a hand-enumerated `value !== null && value !== false && value !== 0 && value !== ''`,
which is right for scalars and wrong for containers: CPython calls `[]` and `{}` false,
JavaScript calls them true. A state-page item is validated by `_json_value` alone
(client.py:1482) — nothing narrows an overview field to a scalar — so an empty container does
reach these renderers, and the drift was byte-visible on stdout:

| item | CPython | round 2 port |
| --- | --- | --- |
| `research {"target": [], "goal": {}}` | `research NO TARGET` | `research [] goal {}` |
| `counts {"b": [], "c": {}, "f": "x"}` | `counts f x` | `counts b [] c {} f x` |
| `score.components {"x": [], "z": "q"}` | `score >=5 (z q)` | `score >=5 (x [], z q)` |

All four sites now go through one `isPythonTruthy` exported from
`src/render/state/overview.ts` — empty array and empty object false, non-empty either one true
(and `_scalar`'s compact JSON is what prints for the truthy ones: `score 7 (a [1], b {"k":1})`).
The three rows above are pinned against the CPython output in `test/state-render.test.ts`
(*an empty container is as absent as a null* / *a container that holds something prints*).

The same `if <json>:` shape appears three more times in U10's span and all three were already
right: `_flatten_item`'s `isinstance(value, dict) and value` guard (`src/render/state/generic.ts`
checks `Object.keys(value).length > 0`, so `{}` is dumped rather than spread), and
`_city_citizens_text`'s `specialists` / `_render_cities`' `pollution`, which are both behind an
`isinstance(..., int)` narrowing where JS and CPython truthiness agree.

The `isPythonTruthy` name is deliberately not shared with `pythonTruthy` in `src/render/monitor.ts`
or the two file-private `truthy` helpers in `src/render/turn.ts` and `src/render/mirror/overview.ts`
— those are other units' files. If a core home for it is ever wanted, all four should collapse
into one `src/render/primitives.ts` export; that is a cross-unit edit, not U10's.

---

## 18. U17 — `monitor`

### 18.1 `flock(2)` is bound twice, because `locks.ts` is core's

`src/services/locks.ts` binds `flock` through `bun:ffi` and exports only `hasNativeFlock()`.
The monitor's lock is not `withAdvisoryLock` with a different timeout — it never blocks, it
never fails on contention, and the lock file *is* the holder record — so it needs the raw
symbol. Core owns `locks.ts` and U17 does not edit it, so `src/services/monitor-lock.ts`
repeats the ~30-line `dlopen` (same candidate list, same cache).

**Integrator:** the honest fix is one line in core — export the bound `flock` (or a
`tryFlock(fd, operation): boolean`) from `src/services/locks.ts` and delete the copy here.
`test/monitor-lock.test.ts` asserts `hasNativeFlock()` is true, so a silent fallback to the
sentinel path fails the suite rather than quietly turning crash recovery into a liveness probe.

### 18.2 `--exit-code` outside {0, 2, 75, 66} leaves by `process.exit`, not by `ExitCodeSignal`

`monitor --exit-code N` promises "exit with this status on an announcement", `N ∈ [0, 255]`;
CPython's `main()` does `sys.exit(int(handler(args)))` and exits with exactly `N`. The port's
own channel for a non-zero finish is `exitWith(status)`, and `ExitCodeSignal` is typed
`ExitCode = 0 | 2 | 75 | 66` — `passThroughExit` collapses everything else to `2`. Round 1
shipped that clamp, and it is worse than a lost byte: `2` is this CLI's *refusal* status, so
`--exit-code 1` (the canonical escalate value for cron/systemd/pi supervisors) told a harness
"your invocation was rejected" underneath a stdout line saying the phase opened — precisely the
incident the flag exists to prevent (client.py:10240-10247).

`src/exit.ts` and `src/cli-main.ts` are core's and U17 does not edit them, so the fix is in
`monitorCommandWith`: a status *in* the contract still leaves by `exitWith` (`--exit-code 75`
and the ordinary `0`/`75`/`66` outcomes are unchanged, and a test pins that they do not take the
new path), and a status outside it finishes the process directly through the
`MonitorHarness.exitProcess` seam, whose live binding is `process.exit(status)`. Safe because
the monitor's writes — the announce line, `state/phase.json`, `state/monitor.log` — are all
complete before `commandMonitor` returns, the advisory lock is released by
`acquireUseRelease` before that, and POSIX `process.stdout` is synchronous for TTYs, files and
pipes alike. The seam is what keeps the ported tests from ending the test runner: they bind it
to `Effect.die(exitDefect(status))` and read the status back off the cause.

`monitorLoop` still returns a plain `number` — it was already correct across `[0, 255]` — and
`monitorCommandWith` is the only place that decides which channel a status leaves by.

**Integrator:** the tidy fix is still core's — widen `ExitCodeSignal.code` to `number` (or add a
`passThroughStatus` that does not clamp) and have `main` pass it through. When it lands, delete
the `exitProcess` seam and let every status leave by `exitWith`; the tests in
`test/monitor.test.ts` (`--exit-code reaches the process status verbatim, in the contract and
out of it`) assert the observable status and pass either way.

### 18.3 The `--once` / persistent mutual-exclusion refusal is unreachable and is not ported

`command_monitor` opens with `if once and getattr(args, "forever", False): raise PlayerError(…)`
(client.py:10529-10533), but `forever` is set only by `monitor.set_defaults(…, forever=False)`
and no flag ever sets it. The refusal cannot fire from the CLI, so the port does not carry a
flag that exists only to be false. If a `--forever` spelling is ever added, the refusal comes
back with it.

### 18.4 `subprocess.run(command, shell=True)` is `/bin/sh -c`

`shellHookRunner` spawns `['/bin/sh', '-c', command]` with `stdin/stdout/stderr: 'inherit'`,
which is what CPython's `shell=True` plus `subprocess.run`'s default (inherited) stdio does.
The runner is a seam (`MonitorSeams.runHook`) so the ported tests script the outcome instead of
spawning, exactly as `test_client.py` patches `client.subprocess.run`.

**`exitCode` is not `returncode`.** Bun's `bun.d.ts` declares `SyncSubprocess.exitCode: number`,
but the runtime returns `null` when the child dies by a signal (`kill -TERM $$` → `exitCode:
null, signalCode: 'SIGTERM'`), while CPython's `CompletedProcess.returncode` is the *negated*
signal number (`-15`). Passing `exitCode` straight through therefore wrote `--exec exited null`
to stderr and `exec exited null` into `state/monitor.log` — a non-status in the file that exists
to make a hook failure auditable — and `tsc` could not flag it, because the declared type lies.
`completedProcessStatus(exitCode, signalCode)` does the mapping (`exitCode ?? -signals[name]`,
`-1` for a signal this platform cannot name), the read is widened to `number | null` at the call
site so the null arm is explicit, and `test/monitor.test.ts` spawns real `kill -TERM $$` /
`kill -SEGV $$` children to pin `-15` / `-11` against the Python. Reachable on ordinary paths:
`timeout N notify-send …`, SIGPIPE down a closed pager, an OOM kill, a segfaulting notifier.

### 18.5 The read-only property is asserted three ways, not documented once

PORT_MAP requires "a test that fails if any write path is reachable from the loop".
`test/monitor.test.ts` carries three:

- **the call itself** — the loop asks `waitUntilTurn` for `{ stateless: true, forTurn: true }`
  and supplies its own `mirror`, so `rememberPage` / `mirrorPage` / `mirrorHealth` are
  unreachable rather than merely unused;
- **the filesystem** — after a run, `state/` contains exactly `monitor.log` and `phase.json`;
- **the wire and the cache** — running the *real* engine over a fake supervisor, the request
  carries `until=phase` and no `after_state_token`, `SessionStore.readState`/`writeState` are
  never called, and `session-….v2-state` does not exist afterwards.

`liveMonitorSeams` is where that is enforced in the implementation: its `WaitCtx.hooks` are
three `Effect.void`s and U06's `holderSeat`. Wiring any of them to a real writer is what the
first assertion catches.

### 18.6 `--status` exits 75 when nothing is running

Not a divergence — `_monitor_status` returns `V2_WAIT_EXIT_RETRY` — but it surprises readers,
so: a supervisor asking "is my watcher alive" gets a status it can branch on, and `0` is
reserved for "yes, and here is what it is watching".

### 18.7 Both spellings of `--wait-s` / `--poll-s`

`monitor`'s argparse block declares only `--wait-s` and `--poll-s`, but PORT_MAP §3.3 makes the
dual spelling repo-wide and `cli-main`'s stub already declared `dualFloat` for both. The port
keeps `dualFloat`, so `--wait_s` is accepted here as it is on `wait`; passing both is the same
refusal every other command gives.

### 18.8 Typecheck and suite residue at U17's landing

`bunx tsc --noEmit` reports nothing in U17's seven files. At the round-2 pass it reports nothing
anywhere in the tree — U16's `src/commands/do.cmd.ts` errors, the residue noted at U17's first
landing, are gone. `bun test test/monitor.test.ts test/monitor-lock.test.ts`: 56 pass, 0 fail.
Nothing outside U17's row imports U17's modules yet.

---

## 18. U15 — the order resolution engine (`do`'s matcher)

### 18.1 `_compact_legal_action` and `_read_legal_page` arrive injected, not imported

`_order_pool` is defined as "compact every cached descriptor at the newest revision", and
`_drain_legal_unlocked` is defined as "repeat `_read_legal_page` until the cursor chain ends".
Both of those inner functions are U11's, and U11 had not landed when U15 was written. They are
therefore **seams**, exactly as U03's `LegalPageFetcher` is:

```ts
// src/services/orders/match.ts
export interface OrdersDeps {
  readonly compactLegalAction: (d: JsonObject) => Effect<JsonObject, PlayerError>
}
// src/services/orders/resolve.ts
export interface OrdersFetchDeps extends OrdersDeps { readonly drainLegal: LegalPageFetcher }
// src/services/orders/rebind.ts
export type LegalPageReader = (sessionPath, session, query, { cursor, actorId, targetId })
  => Effect<LegalActionPageEnvelope, PlayerError, SessionStore | PrivateFs>
```

`drainLegal` is U03's `LegalPageFetcher` type verbatim, so U16 can pass the *same* callback to
`refreshStaleOrderAliases`, `resolveOrdersFetching`, `refreshOrders` and U03's
`expandActionAliasRefreshing`. `drainLegalUnlocked(read, …)` is the loop U11's fetcher should be
built from; U11 may instead supply its own and never call it, and nothing in U15 notices.

The tests carry a line-for-line port of `_compact_legal_action` (client.py:7899-7947) as a
fixture, so the unit is exercised against the projection it will actually receive — including
the `subject.target` → `target` move, `arguments_schema` → `argument_schema`, the withheld
discriminator, and the omit-when-certain `probability`. When U11 lands, delete the fixture and
import the real one; if the two ever disagree, `test/orders-resolve.test.ts` is the diff.

### 18.2 `_OrderUnresolved` is a tagged error, and it is caught, never rendered

CPython raised `_OrderUnresolved` from deep inside `_resolve_order` and caught it one frame up
in `_order_outcomes`. Here it is `OrderUnresolved` in the Effect error channel, and
`orderOutcomes` is the only place that reads it — it maps the two arms exactly as CPython's two
`except` clauses did (`OrderUnresolved` keeps `reason` + `actorId`; a `PlayerError` keeps its
message and carries **no** actor, which is what stops a failed alias expansion from triggering
a pointless pre-fetch). Nothing outside this unit should ever see an `OrderUnresolved`.

### 18.3 `unresolvedReport` returns lines, not a joined string

PORT_MAP §1 sketched `unresolvedReport: (u: OrderUnresolved) => ReadonlyArray<string>`. The
Python is `_unresolved_report(path, state, outcomes) -> str`: it needs the *whole* outcome list
(the resolved rows are half the value of the refusal) and the session path (the remedy names
it, and the phase note is read off the mirror beside it). The landed signature keeps §1's
`ReadonlyArray<string>` return and takes what the Python took:

```ts
unresolvedReport(sessionPath, state, outcomes) => Effect<ReadonlyArray<string>, PlayerError, SessionStore | PrivateFs>
```

`resolveOrders` joins them with `\n` into the `PlayerError` CPython raised, so the observable
bytes are identical. See PORT_MAP's U15 addendum.

### 18.4 `_order_value`'s `float()` is not `Number()`

The two disagree on exactly the inputs a schema-typed order word can hit: CPython's `float()`
accepts `1_000`, `inf`, `nan` and rejects `0x10` and `""`; `Number()` does the reverse on all
five. `src/services/orders/arguments.ts` implements CPython's grammar as a regex plus an
explicit `inf`/`nan` branch, and `test/orders-parse.test.ts` asserts all five. The integer path
is CPython's own canonical-integer regex, so `007` and `1.0` are non-matches rather than 7 and 1
— an order that binds a leading-zero integer would be a different order than the one written.

### 18.5 `casefold` is the real fold; `repr` is still approximated (round 3)

**Superseded.** This section previously recorded `casefold()` → `toLowerCase()` on the reasoning
that "a catalog verb, label, enum member or target name is ASCII or Latin-script". This
repository's own data contradicts it. `data/nation/{alsatian,eastgerman,anhaltian,badian,
curonian,saxon,teutonic,holyroman,…}.ruleset` ship city names such as `Straßburg`, `Meißen`,
`Weißenburg` and `Roßlau`, and `translations/core/de.po` renders Gunpowder as `Schießpulver` and
Raft as `Floß`. All three arrive in the matcher: as `target.name` (`_named_target_id`,
`_order_discriminators`), as an `enum` member (`_order_value`) and as a compact-catalog subject
word. CPython answers `True` for `"Strassburg".casefold() == "Straßburg".casefold()`, so the
ASCII spelling an agent can actually type binds. `toLowerCase` compares `strassburg` to
`straßburg`, misses, and refuses with `no cached target is named Strassburg` /
`no cached action takes those arguments; N candidate(s) matched the verb` — fail-closed, but it
silently drops leniency CPython chose, on the unit's *primary recovery surface*.

`src/services/orders/parse.ts` now re-exports `casefold` from `src/render/show-unicode.ts`
(U09's generated 297-entry fold table), which PORT_MAP's "U09 gains two files" addendum names as
the one place a Python-compatible fold may be stated — see the ownership addendum appended to
PORT_MAP for the import. It folds per code point with no context rule, so it also gets `ΑΣ`
right where `toLowerCase`'s final-sigma rule would not. All thirteen `.casefold()` sites in
client.py:8679-9421 route through it. `test/orders-parse.test.ts` asserts the fold table against
`python3 -c "print(...casefold())"` output and the four ruleset city names;
`test/orders-resolve.test.ts` asserts `u1 join_city Strassburg` and `c1 queue Floss` bind, and
that a word folding onto nothing cached is still refused rather than guessed.

`{order!r}` → `pyRepr` stays an approximation, on the reasoning U18 recorded: Python 3's `repr`
leaves printable non-ASCII alone, so the only divergence would be a surrogate or an unassigned
code point inside an order string. `pySplit` is the third one and the one that always mattered:
`"".split()` is `[]` in CPython and `[""]` in JavaScript, and `_parse_orders`' word count reads
that difference directly.

**Adjacent, not U15's to fix:** `src/services/legal-compact.ts:97-101` ports
`re.split(r"[^a-z0-9]+", key.casefold())` with `toLowerCase()`. That one is applied to
*subject-dictionary keys*, which the OpenAPI schema constrains to ASCII identifiers, so it is
safe today — but it is the same substitution and U11 should import `casefold` rather than restate
it if those keys ever stop being ASCII.

### 18.6 `_LEGAL_SUBJECT_RESERVED` landed on U15's row

PORT_MAP scopes U11 from client.py:3596; `_LEGAL_SUBJECT_RESERVED` is defined at 3590, one line
above, and `_order_discriminators` is its only caller in this unit. It is exported from
`src/services/orders/match.ts` as `LEGAL_SUBJECT_RESERVED`. **U11 should import it rather than
restate it** — the same arrangement U03 made for `actionTargetKey`.

### 18.7 An array bound is checked before any element is resolved

`c1 queue a b c` against a `maxItems: 2` worklist produces `no cached action takes those
arguments; 1 candidate(s) matched the verb`, **not** the per-word `no cached target is named a`.
CPython checks `minimum <= len(words) <= maximum` before it runs the element resolver, so a
line that overruns the bound never reaches the word-level refusal. This reads like a worse
message and is the correct one: the action genuinely cannot take three items, so naming the
first bad word would send the agent to fix the wrong thing. `test/orders-report.test.ts`
asserts both sides of the bound.

### 18.8 The `--no-refresh` path is `resolveOrders`; the default path is `resolveOrdersFetching`

`command_do` branches on `--no-refresh` before it resolves, and the two branches differ in more
than a flag: the plain path raises the report bare, while the fetching path prepends every note
accumulated so far (the `aN rebound at revN` lines from `refreshStaleOrderAliases` *and* its own
`fetched u1 options (rev7)` lines) so a refusal can never hide a round trip that happened. That
is why `resolveOrdersFetching` takes and returns `notes` instead of mutating a caller's list the
way CPython did. U16 should thread its own `lines` array through both.

### 18.9 Typecheck and suite residue at U15's landing

`bunx tsc --noEmit` reports nothing in U15's seven source files or three test files. The errors
in the tree at landing time are in `src/services/meetings.ts` (missing `src/services/decisions`,
U12) and `src/services/pregame.ts` (U18) — neither is U15's to fix. Nothing outside U15's row
imports U15's modules yet.

**Round 3.** Still nothing in U15's ten files, and `bun test` is green across all 57 suites
(1,542 tests). The tree's residue has moved but is still other units' in-flight work:
`src/commands/act.cmd.ts` (three errors: an `Effect<undefined, unknown, unknown>` widening, a
`PyObject`/`JsonObject` mismatch and a `printJson` that should be `printPyJson`) and
`test/decisions.test.ts:583` (a cast that drops `PlayerError`). Neither file is on U15's row.

### 18.10 A dict is not an object with a prototype (round 2, review fix)

The same defect §15.10 records for U08, in three places on U15's row. A CPython `dict` has own
keys only; `in`, `[]` and `.get()` never see an inherited member. JavaScript's `in` operator and
property read do, and every key set involved here is chosen by somebody other than this client.

**`_order_arguments` (`arguments.ts`).** `fixed` was a plain `{}` and membership was
`!(name in fixed)`, so any argument-schema property named `toString`, `constructor`, `valueOf`,
`hasOwnProperty`, `isPrototypeOf`, `propertyIsEnumerable`, `toLocaleString` or `__proto__` tested
as already-supplied and was dropped from `names` and from the `needed` count. This did **not**
fail closed. Verified against the real `play/client.py`: schema
`{"properties":{"toString":{"type":"string"},"z":{"type":"string"}},"required":[]}` with the one
word `hi` binds `{"toString": "hi"}` in CPython and bound `{"z": "hi"}` in the port — the same
order line, a silently different argument on the wire, no refusal and no note. Other shapes
degraded to `no cached action takes those arguments`, which reads like a schema the seat does not
have rather than a bug. The argument schema is server-supplied and nothing validates its key set,
which is precisely the drift shape PLAN.md's "Known traps" blames for three incidents. `fixed`,
`args` and `_default_arguments`' accumulator are now `Object.create(null)` and membership is
`Object.hasOwn`; the null prototype also removes the `args['__proto__'] = value` hazard, where a
required property so named would have reparented the object instead of becoming a key (CPython
returns `{"__proto__": "hi"}`, and so does the port now).

**`V2_TIER1_VERBS[selector]` (`resolve.ts`).** `selector` is a word the *agent* typed.
`V2_TIER1_VERBS["constructor"]` returned `Object`, so `tier1 !== undefined` held and the order
was refused with ``` `constructor` names undefined/undefined, which this seat's cached catalog
does not advertise here ```. CPython's `.get` misses, the word falls through to the catalog's own
verbs, and the sentence is ``` no cached action advertises the verb `constructor` ``` — the
refusal that actually tells the agent what to type next. Now guarded by `Object.hasOwn`.
`V2_TIER1_VERBS` itself keeps its normal prototype because U12's `decisions.ts` re-exports the
object; only the lookup changed.

**`aliases[actorId]` (`report.ts`).** `_order_enumeration_command` is
`aliases.get(actor_id, actor_id)`, and `actor_id` is an unvalidated wire string. An ID spelling an
inherited name skipped the `?? actorId` fallback and put a function body into the remedy line
(`just legal --actor_id function Object() { [native code] } --all`) — a broken command on the one
surface whose entire job is to be copy-pasteable. Now `Object.hasOwn(aliases, actorId)`.

`test/orders-parse.test.ts` pins the binding matrix by `JSON.stringify`, not `toEqual`, because
`toEqual` cannot tell a bound `toString` from an inherited one nor an own `__proto__` key from a
reparented object — and the stringified form is the bytes `just batch` would send. Every
expectation in that block is the output of the real CPython helper on the same schema.
`orders-resolve.test.ts` and `orders-report.test.ts` pin the other two sentences.

## 19. U09 — `show` and the yields overlay

### 19.1 The oracle ran again after round 2, on real workspaces and on generated ones

`show` is the largest share of the offline byte-diff oracle, so it is run rather than argued
about. Three finished workspaces under `.play/` were copied to scratch (never touched in
place), `play/client.py` + `play/state_mirror.py` were copied beside each copy so CPython ran
at *this* revision, and 109 selections per workspace were diffed byte for byte — stdout,
stderr and exit code — against the ported `runShow`: every fixed section name plain and
`--json`, the bare listing, `map --yields`, alias rows, the traversal and "not both" refusals,
27 literal `--grep` patterns and 55 `--regex` ones. **327 invocations, 3 diverged**, and all
three are the same pattern — `--grep '(a)?\1' --regex`, the one refusal §19.3 introduces on
purpose. Drop that pattern and it is 324 for 324.

A second fixture workspace was built to hold the text the real ones do not — `Große`,
`Straßburg`, `ΟΔΟΣ` and `Eﬀort` — and CPython's stdout, stderr and exit code for 51 more
invocations over it are the goldens embedded in `test/show-regex.test.ts`; all 51 match. The
goldens in `test/show.test.ts` and `test/show-yields.test.ts` are the round-1 captures over
the synthetic mirror (an empty mirror, an unparseable `.v2-state`, a priced window wider than
24, a drifted `yields.tsv` header).

Underneath that, `src/render/show-regex.ts` was compared against CPython's `re` on **140,000
generated patterns** and their subjects: no pattern matched differently, no error message
differed, and nothing was accepted that `re` rejects or rejected that `re` accepts, apart from
the two constructs in §19.3. `casefold` was compared against `str.casefold()` on **every one of
the 1.1M code points** — zero mismatches. Two bugs were found by that run and not by reading:
`_compile_info`'s prefilter (§19.3b) and JavaScriptCore's `u` flag (§19.3a).

### 19.2 `--grep` folds with `str.casefold()`, because `toLowerCase` is a different question

The default, flagless `--grep` is the most-used branch of the unit, and round 1 folded case
with `String.prototype.toLowerCase()`. That is not `str.casefold()`, and the gap is not
theoretical: a `state/units.tsv` row holding `Große` — a live string in this repo's
`data/nation/*.ruleset` leader lists — is found by `--grep grosse` under CPython and was
missed by the port, at exit 0, with nothing said. `--grep ß` prints the lines holding `ss`
under CPython (`'ß'.casefold() == 'ss'`) and printed `no mirror line matches 'ß'` here.

`src/render/show-unicode.ts` closes it with the real table. Full folds expand (`ß`→`ss`,
`ﬀ`→`ff`, `ﬃ`→`ffi`), a handful of common folds differ from lowercasing (`ſ`→`s`, `ς`→`σ`,
`µ`→`μ`), `İ` folds to `i` + combining dot, and Cherokee folds *up* where JavaScript lowers it
*down*. The table is exactly the 297 code points where CPython's `casefold` and JavaScript's
per-character `toLowerCase` disagree, generated from this machine's CPython, and `casefold`
walks code points rather than handing the whole string to `toLowerCase` — whose final-sigma
context rule would disagree with `casefold` on `ΑΣ`. The length cap counts code points too:
`len(pattern) > 200` is 200 emoji, not 400 UTF-16 units, and both sides of that boundary are
golden rows.

### 19.3 `--regex` is CPython's `re`, parsed and re-emitted — not `new RegExp(p, 'i')`

Round 1 compiled the pattern with `new RegExp(pattern, 'i')` and recorded only that the
engine's own syntax-error clause differed. That understated it in all three directions, and a
differential run against CPython found 20 of 28 probed patterns diverging:

* **silently, at exit 0 on both sides** — `\A` and `\Z` are anchors in `re` and identity
  escapes in a non-`u` JavaScript regex, so `--grep '\Z' --regex` printed *every* mirror line
  under CPython and `no mirror line matches` here; `A{,2}` is `A{0,2}` in `re` and three
  literals in JavaScript; `[a-\d]` is refused by `re` and matched everything here; `.`, `$`,
  `\d`, `\w` and `\s` all mean different sets of characters in the two engines;
* **refusing what `re` accepts** — `(?i)`, `(?s)`, `(?m)`, `(?#c)`, `(?P<x>…)`, `(?P=x)`,
  atomic groups and possessive quantifiers all exited 2 with `pattern is invalid`;
* **accepting what `re` refuses** — `(?<name>…)`, `\p{L}`, `\N{DASH}`, `\h`, `\Q`, `\cA`,
  `\8`, `\x{41}` and a variable-width lookbehind all exited 0 and answered a question CPython
  never asked.

So the pattern is no longer handed to JavaScript. `src/render/show-regex.ts` is a port of
CPython 3.14's `re/_parser.py` — the same tokenizer indexed by code point, the same checks in
the same order, the same messages, the same `at position N`, and the `(line L, column C)` tail
CPython adds when the pattern holds a newline — plus the two checks `_compiler.py` makes
(`look-behind requires fixed-width pattern`, `looks too much behind`). It parses to an AST and
*emits* a JavaScript source with `re`'s semantics: `\A`/`\Z`/`\z` become `^`/`$` (the emitted
regex never carries `m`, so they mean the same thing), `^`/`$` become the lookarounds `re`
means by them, `.` and every class become explicit code point ranges, `\d`/`\s`/`\w` become
CPython's own ranges, `IGNORECASE` is expanded into CPython's case-equivalence classes rather
than delegated to the `i` flag — which keeps a scoped `(?-i:…)` meaningful and closes the one
place JavaScript's `iu` canonicalisation disagrees with `re` (`ı` U+0131 against `i`/`I`) — and
atomic groups and possessive quantifiers are emulated with the `(?=(…))\k<…>` identity.

**Three things remain, all of them recorded rather than silent.**

1. **A conditional group reference `(?(1)a|b)` is refused** with
   `pattern is invalid: a conditional group reference has no equivalent in this engine`.
   JavaScript has no conditional and no way to test group participation, so there is nothing
   to emit; refusing is loud, and CPython's own accept/reject decision is unchanged for every
   `(?(…))` form CPython itself rejects, because the parser reaches those first.
2. **A backreference to a group that may not have taken part is refused.** `re` *fails* such a
   backreference and JavaScript matches the **empty string** there, so `--grep '(a)?\1'` finds
   nothing under CPython and would match every line here. The emitter tracks which groups are
   certain to have participated — `(a)\1`, `(a)+\1` and `((a)\2)` are certain; `(a)?\1`,
   `(a)*\1` and `(?:(a)|b)\1` are not — and emits `\k<gN>` only when it is certain. This is
   the only divergence the real-workspace oracle found, and it trades a silent wrong answer
   for an exit-2 sentence.
3. **`\N{…}` resolves names from a bounded table** (Latin-1 and General Punctuation, 302
   names, case-insensitive like `unicodedata.lookup`). CPython resolves every name in the
   database plus the algorithmic CJK and Hangul ones; shipping that costs ~5 MB. A name
   outside the table is reported `undefined character name '…'`, which is CPython's own answer
   for a name that does not exist — so `\N{DASH}` is byte-identical — and a refusal, never a
   wrong match, for one that does.

4. **A pattern whose *emitted* source outgrows JavaScriptCore is refused** with
   `pattern is invalid: the emitted pattern is too large for this engine`. Because every class
   is emitted as explicit code point ranges (§19.3a), one `\w` costs ~10 KB of regex source and
   one `\b` ~50 KB, so a legal pattern well inside the 200-character cap can emit past
   JavaScriptCore's ~1 MB ceiling: measured on this machine, 84 `\w`s (168 characters) and 21
   `\b`s (42 characters) are the first that fail, and `new RegExp` raises
   `SyntaxError: regular expression too large`. CPython compiles all of them —
   `just show --grep '\b\b…' --regex` prints every mirror line at exit 0 — so this is a
   divergence either way, and the choice is only *how* it diverges. Round 2 let the throw
   escape `compilePythonRegex` as a **defect**, which `cli-main` printed as a JavaScript stack
   trace at exit 2; round 3 catches every non-`ParseSignal` throw and returns it as a
   `PyRegexFailure`, so the seat sees the one-line refusal and the remedy that does work
   (`drop --regex`). Emission itself stays bounded: it is linear in the pattern, so the
   200-character cap holds it under 30 ms and a few megabytes of string — the worst 200-character
   pattern found measured 26 ms. `test/show-regex.test.ts` pins both the refusal and the
   just-under-the-ceiling patterns (20 `\b`s, 83 `\w`s) that still compile and still match.

Smaller residue, none of it reachable from a match result: group names are validated with
JavaScript's `ID_Start`/`ID_Continue` tables rather than this CPython's, which agree on every
ASCII name; CPython's `FutureWarning` for `[[`, `[a--b]`, `[a&&b]` and friends is not
reproduced on stderr (it carries a CPython source path and line number); and the two patterns
that make CPython raise `OverflowError`/`ValueError` rather than `re.error` —
`a{4294967295}` and `(?a)(?u)` — escape `_show_grep`'s `except re.error` and end CPython in a
traceback at exit 1, where the port refuses at exit 2 with the same sentence.

### 19.3a The emitted regex is UTF-16, because JavaScriptCore miscompiles `u`

The natural emission is the `u` flag, which makes JavaScript step by code point exactly as
`re` steps over a `str`. Bun's JavaScriptCore gets it wrong twice, and both were found by the
differential run rather than reasoned about:

* `/(?<![\s\S])a/u` — the obvious spelling of `\A` — fails when the character before the
  position is astral. `/(?<=[\s\S])a/u.test('🙂a')` is `false` under Bun and `true` under V8.
* once a class spanning the astral planes has consumed an astral character at a non-zero
  offset, the rest of the pattern fails: `/.$/u.test('x𝕏')` is `false` under Bun and `true`
  under V8 and CPython. Splitting the class at the BMP boundary, complementing it, or writing
  the anchor five different ways does not avoid it.

So `rangesMatcher` writes astral ranges out as surrogate pairs — what the `u` flag would have
done internally — the compiled regex carries **no flags at all**, and the whole pattern is
prefixed with `(?<![\ud800-\udbff])` so a search can never begin between the halves of a pair.
Every consuming atom then spans whole code points, so every interior position is a code point
boundary too and `\b` and the lookarounds see the positions `re` sees. The surrogate block is
excluded from the BMP half, so a lone surrogate — which well-formed mirror text cannot contain
— never stands in for a character. `test/show-regex.test.ts` pins `.\Z` and `\B` against
astral text so a future Bun that fixes the bug cannot quietly reintroduce the shape.

The cost is source size, not time: `\b\w+\b` emits 113 KB and compiles in 2.5 ms, and 2,000
line tests take 10 ms — three orders of magnitude inside `V2_SHOW_GREP_BUDGET_S`.

### 19.3b `(?a:…)` replaces the enclosing flag, and CPython's prefilter does not

Two things about the character-set flags were wrong until the differential run found them, and
both are silent — the pattern compiles either way and answers a different question.

`_compiler._combine_flags` **replaces** the enclosing `a`/`u`/`L` bit rather than joining it:
`if add_flags & TYPE_FLAGS: flags &= ~TYPE_FLAGS`. The port was doing
`(flags | add_flags) & ~del_flags`, which leaves both `ASCII` and `UNICODE` set in
`(?a)…(?u:\w)` and then answers according to whichever bit the reader tests first.

The second is stranger and is CPython's *optimiser* leaking into its semantics.
`_compile_info` looks for a character set it can use to skip start positions;
`_get_charset_prefix` descends through leading groups and combines their flags correctly, but
`_compile_info` then emits the set it found with the **outer** flags — `_compile_charset(charset,
flags, code)`. So in `(?a)(?u:\w)` the body reads the Unicode tables and the prefilter reads the
ASCII ones, and `re.search(r'(?a)(?u:\w)', 'ﬁ', re.I)` is `None` even though the body would
accept `ﬁ`. Add one atom in front — `(?a)(?u:.)(?u:\w)` — and it matches, because the prefilter
is then built from a different atom. `charsetPrefilter` reproduces it as a leading lookahead,
under the same guards CPython applies (no prefilter when the pattern can match empty, when a
literal prefix was found, when any member of the set is cased, or when the set is everything),
and only when the two flag sets actually disagree — which, given the cased-member guard, can
only happen through a category. `test/show-regex.test.ts` pins five flag combinations against
CPython's answers.

### 19.4 Truncation is detected by collecting one match too many

CPython appends only while `len(lines) < V2_SHOW_MAX_MATCHES` and sets `truncated` on the match
it *cannot* append. `scanPresent` collects up to `MAX + 1` and returns; `outcome.length > MAX`
is then exactly CPython's `truncated`. The equivalence matters at the boundary: a search with
**exactly** 200 matches is not truncated and prints no `(stopped at …)` line. Both sides of that
boundary are asserted.

### 19.5 `_show_staleness` reads the stamp through U04, and swallows everything

`_mirror_table`'s `(turn, revision)` tuple is `parseTable(mirrorText(…)).revision`; the
comparison is U04's `isBehind` (which is `isNewer` with the arguments swapped — CPython's
`order()` sorts by revision first, and `isNewer` already does) and the sentence is U04's
`staleLine`. `_mirror_table` proper is on U12's row and is not imported: it is one call to two
U04 primitives, and taking a dependency on `turn` for it would invert the wave order.

The banner is an annotation, never a gate — so *every* failure of the `.v2-state` read collapses
to `''`: a missing cache, an unparseable one, a lock timeout. A file with no `# rev` stamp
(`state/phase.json`, and any options file written before the stamp existed) contributes no
`rendered` candidate, so `show phase` is never banner-prefixed.

### 19.6 `--yields` returns before the banner exists, and that is CPython's order

`command_show` handles `--yields` *after* the "name and `--grep`, not both" and "`--regex` needs
a `--grep`" checks but *before* the selection dispatch and the staleness computation. So
`just show --grep X --yields` earns the `--yields` refusal (not the "not both" one), and
`show map --yields` never carries the stale banner even when `show map` would. Asserted both
ways in `test/show-yields.test.ts`.

### 19.7 `show phase` inherits U04 §12's integral-float residue

`show phase` prints `state/phase.json`'s bytes verbatim, so when CPython wrote the mirror the
port reads it back byte-identically (the oracle above covers exactly that). The residue is
one-directional: a mirror written by the *port* spells `"held_s": 139` where CPython spelled
`139.0`, so `show phase` would differ then. That is U04's note, not a second defect, and it is
the only mirror file whose bytes the two implementations can disagree about.

### 19.8 `_show_option_files` walks in through `PrivateFs.openDirectory`

CPython opens a directory *file descriptor* with `O_NOFOLLOW` and `os.listdir`s it. The port has
no fd-listing primitive, so it uses U04-adjacent core's `openDirectory`, which refuses every
symlinked path component and returns a real path, then `fs.readdirSync`. The refusal window is
the same one CPython closed — a planted `state/options -> /etc` lists nothing — and the test
asserts it with an actual symlink. Any failure at all yields `[]`, exactly like CPython's bare
`except`.

### 19.9 Typecheck and suite state at U09's round-3 landing

`bunx tsc --noEmit` is clean over the whole tree.
`bun test test/show.test.ts test/show-yields.test.ts test/show-regex.test.ts` is 131 tests, all
passing; the full suite at this snapshot is 1,626 across 58 files, 0 failing.

### 19.10 Two files were added to U09's row

`src/render/show-unicode.ts` (the generated CPython tables: the case fold, the `\w`/`\s`/`\d`
ranges, the `IGNORECASE` equivalence classes and the `\N{…}` names) and
`src/render/show-regex.ts` (the `re` parser and the JavaScript emitter), plus
`test/show-regex.test.ts`. They exist because §19.2 and §19.3 are both several hundred lines
of table and transcript that would have buried `src/render/show.ts`, and because the tables are
*generated* — keeping them in one file makes it obvious which lines were written by hand.
Nothing else imports them; `src/render/show.ts` is their only consumer. PORT_MAP's addendum
records the ownership change.

### 19.11 `str.strip()` is not `String.prototype.trim()`, and `repr` is not "escape the controls"

Two whitespace/printability questions decide `show`'s stdout, and round 2 answered both with
JavaScript's own notion instead of CPython's. Both are fixed and both are now pinned against
captures from `python3 client.py` over the fixture mirror.

**The strip.** `command_show` does `pattern.strip()` and `name.strip()` (client.py:11441-11442)
and `_show_rows` does `line.split("\t", 1)[0].strip()`. Python's whitespace class and
JavaScript's overlap but neither contains the other: `str.strip()` removes U+0085 and
U+001C–U+001F, which `trim()` keeps, and keeps U+FEFF, which `trim()` removes. Verified on this
machine: `{c for c in range(0x110000) if chr(c).strip() == ''}` is exactly 29 code points and
`String.prototype.trim()`'s is 25 of those plus U+FEFF. Measured consequences, all of them
stdout **and** exit code:

| invocation | CPython | round 2 |
| --- | --- | --- |
| `show $'\x85units'` | prints `state/units.tsv`, exit 0 | refusal, exit 2 |
| `show $'\x1funits'` | prints `state/units.tsv`, exit 0 | refusal, exit 2 |
| `show $'\ufeffunits'` | refusal (`SHOW_NAME_RE`), exit 2 | prints `state/units.tsv`, exit 0 |
| `show $'u1\ufeff'` | refusal, exit 2 | the alias rows, exit 0 |
| `show --grep $'\x85Settlers'` | the matching row | `no mirror line matches` |
| `show --grep $'\x85'` | the bare default listing | greps for U+0085 |
| `show --grep $'\x85' --regex` | `--regex needs a --grep`, exit 2 | a search, exit 0 |

The fix is not a second copy of the class: `strip` is **U04's**, exported from
`src/services/mirror` (`store.ts`, whose own comment documents this difference), and both
`show.cmd.ts` and `showRows` import it. Round 2 reached for `trim()` because the barrel had not
re-exported it yet; it does now, so there is one spelling of Python's whitespace in the tree.

**The repr.** `no mirror line matches {pattern!r}` goes through CPython's `repr()`, which
escapes by `str.isprintable()` — a general-category test — and not by "is this a control
character". Round 2 escaped only `\x00`–`\x1f` and `\x7f`, so every *format*, *separator*,
*private-use*, *surrogate* and *unassigned* code point printed raw where CPython escaped it:
`show --grep $'\ufeffSettlers'` printed a raw BOM where CPython prints `'\ufeffSettlers'`,
and `--grep $'Set\xa0tlers'` a raw no-break space where CPython prints `'Set\xa0tlers'`. The
escape *width* is CPython's too — `\xNN` below U+0100, `\uXXXX` below U+10000, `\UXXXXXXXX`
above — so an astral tag character is `\U000e0001`, one escape and not two surrogates.
`show-unicode.ts` carries `PRINTABLE_RANGES` (736 runs, ~3 KB of source, generated from this
machine's CPython) and `pyIsPrintable` binary-searches it. `pyRepr` was then compared against
`repr()` on **every code point** in `'x' + chr(c) + 'y'` form — 1,112,064 comparisons, zero
mismatches — and on 20,000 generated strings mixing quotes, backslashes, control characters,
U+0085/U+00A0/U+FEFF, astral and unassigned code points: zero mismatches.

**Re-run of the oracle.** Two runs, both against `python3 play/client.py` at this revision.

*On the fixture seat:* 173 invocations — 160 generated `--grep` patterns drawn from an alphabet
of metacharacters, tabs, U+0085/U+00A0/U+FEFF/U+200B/U+3000, unassigned and astral code points
(40% of them `--regex`), plus 13 names — **170 identical**. The 3 that differ are all a pattern
beginning with `-`, where argparse consumes it as an option (`error: argument --grep: expected
one argument`) before `command_show` ever runs. That is the CLI parser's layer, not `runShow`'s;
it applies to every option in the tree rather than to `show`, so it is core's to answer if
anyone wants it answered.

*On a real finished workspace:* `.play/game_Hsit9YEuBjKdJPPouFoGVYlk_pi_gpt-5.6-sol` was copied
to scratch (never touched in place) with `play/client.py` and `play/state_mirror.py` beside it,
and 171 invocations — 26 names plain **and** `--json`, `map --yields`, and 118 generated
`--grep` patterns, 45% of them `--regex` — were diffed byte for byte on stdout, stderr and exit
code: **171 of 171 identical**. (Run the archived `client.py` that ships *inside* such a
workspace instead and 6 differ, because that copy predates `phase` joining `V2_SHOW_FILES`; the
canonical spec is `play/client.py`, and against it there is nothing left.)

### 18.9 The announce line's revision stamp is unreachable from `monitor`

`_monitor_announce_line` appends `rev{n}/t{turn}` when the wake carries a `state_revision`, but
the monitor always waits in phase mode and `_validate_wait_response` refuses `until=phase` with
a non-null revision. The branch is therefore dead for this command and alive only for the shared
line; `test/monitor.test.ts` pins its bytes through a `revision_changed` wake so the stamp is
still under test, with a comment saying why the wake had to be built that way.

## 19. U14 — receipts, retry and ambiguity

### 19.1 `retry` does not retry

The command's name is the one piece of this unit that is misleading, and the port keeps it
because the Python does. `_command_retry_locked` reaches the wire with a batch **body** in
exactly one situation: the server answers the receipt read with `404 invalid_request` *and*
this client has never seen a receipt for the batch — not in `.v2-state`, and not during this
invocation's own poll loop. Every other path resolves rather than replays:

| what `retry` finds | what it does | requests |
| --- | --- | --- |
| cached `applied`/`rejected`/`ambiguous` | prints it | **none at all** |
| cached `accepted` | polls it | GET |
| polled `accepted`, then gone | terminalizes it as `ambiguous` | GET |
| never seen, and gone | submits the persisted bytes | GET + POST |

The `accepted` observed *during the poll* counts as much as a cached one. That is the case a
naive port gets wrong — it would only consult `.v2-state`, find nothing, and re-send a batch
the server had just said it accepted. `test/ambiguous.test.ts` names it
("an accepted receipt seen only during this poll counts just as much").

### 19.2 The clock is injected, because the poll budget is what is under test

`RETRY_POLL_DEADLINE_S = 30` at `RETRY_POLL_INTERVAL_S = 0.25` is a 30-second test against a
real clock. `RetryClock` (`monotonic`/`sleep`, both in seconds, both `Effect`s) is a parameter
of `retryLocked`/`runRetry` with `systemRetryClock` as the default, mirroring U05's `WaitClock`.
The deadline test drives it with `advanceOnSleep`, so the give-up path is asserted in
microseconds and the suite cannot hang if the loop ever stops terminating.

### 19.3 `exitWith(2)`, not a `PlayerError`, for a non-zero `retry`

CPython's `command_retry` returns `_submit_persisted_batch`'s status (0 or 2) after the
disposition is already on stdout and the warning already on stderr. Failing with a `PlayerError`
here would add an `error: …` line CPython never printed, so `runRetry` fails with
`exitWith(code)` — `ExitCodeSignal`, which `cli-main` turns straight into the process status
and prints nothing for. Same mechanism `wait`'s 75/66 uses.

### 19.4 `_render_error_payload`'s "expressed" set is what makes `--json` honest

`safe_next` and `batch_id` are *always* in the expressed set, even when the remedy table has no
sentence for the `safe_next` value: CPython adds them to the set unconditionally and only then
looks the remedy up. So an unknown `safe_next` prints **no** line at all — neither a remedy nor
a `safe_next=…` raw detail. `retry_after*` and `restart` join the set only when their line was
actually produced, which is why an *unspellable* restart query still shows up as `restart=…`
and still triggers `full payload: … --json`, while a spellable one does neither.
`test/refusal-render.test.ts` pins all four combinations.

### 19.5 `_scalar` over an unvalidated payload

`renderErrorPayload` takes `unknown` (cli-main hands it whatever a refusal carried), so the
JSON domain is re-established locally: `null`/`undefined` → `-`, booleans → `yes`/`no`, numbers
and strings through `scalar`, everything else through `compactJson`. CPython's `_scalar` tail is
`json.dumps(..., sort_keys=True, separators=(",",":"))`, which is exactly `compactJson`.

One unreachable divergence: `_revision_label` reads `revision['turn']` with `[]`, so a payload
carrying `state_revision.revision` but no `turn` raises `KeyError` in CPython and prints
`rev8/t-` here. No validated payload can be in that shape (`decodeRevision` requires both), and
the port prefers a printed refusal over a traceback.

Similarly, `_retry_after_text` accepts any `int`/`float`; the port additionally requires
`Number.isFinite`, which cannot differ on a validated payload because `_json_value` already
refuses non-finite floats.

### 19.6 `receipt` and `retry` reach U13/U03/U04 through `ReceiptHooks`

Same shape as U05's `WaitHooks`: `batchIntent` and `submitPersistedBatch` are U13's,
`rememberReceipt` is U03's, `mirrorReceipt` is U04/U07's. The hooks are `Effect`s with **no**
requirements — `liveReceiptHooks` captures `PrivateFs`/`SessionStore`/`V2Client` from the CLI's
Layer stack and provides them back per call — which is what lets the whole retry state machine
be driven from a test with a fake `fetch` and a hand-cranked clock.

`mirrorReceipt` is still inert (`_mirror_receipt` needs `state_mirror.update_from_receipt`,
U07's row), exactly as U13's `inertReceiptMirror` is. Stdout is unchanged; one mirror section
is not written. One line in `src/services/receipts.ts` closes it.

**No duplication was left behind.** U13 landed while this unit was being written, so
`batchIntent` and `submitBatch` are imported from `src/services/batch` rather than re-ported —
and U13's three temporary fallbacks in `src/commands/batch.cmd.ts` (its NOTES §17.2) can now be
deleted in favour of `renderReceipt`/`renderDisposition`/`orderReceiptOk`. See PORT_MAP's U14
addendum for the integrator's three-step list.

### 19.7 The invariant, and how it is guarded

*A batch this client has ever seen `accepted` — or whose receipt says `applied`, `rejected` or
`ambiguous` — never goes back on the wire.* Three independent layers hold it up, and
`test/ambiguous.test.ts` exercises all three:

1. **The command.** `retryLocked` returns from the cache before it opens a socket for a
   terminal receipt, and only reaches `submitPersistedBatch` with `accepted === null`.
2. **The schema layer.** `decodeReceipt` (core) refuses an `ambiguous` receipt that is not
   `action_outcome_ambiguous` or that is `retryable`, so `missingAcceptedReceipt` cannot mint a
   replayable ambiguity — it is built as a wire payload and run back through the real validator.
3. **The state layer.** U03's `rememberReceipt` refuses `ambiguous → applied`, so terminal is
   terminal *on disk*, not just in the process that decided it.

`mayResubmitCachedReceipt` exists only to state rule 1 as a named function returning `false` for
all four states: a future caller has to argue with a function rather than rediscover the rule.

### 19.8 Adversarial coverage beyond the Python

`test/ambiguous.test.ts` adds three cases `test_client.py` does not have:

- **ambiguous + retry** — cached and server-sent, text and `--json`, plus the second invocation
  that must stay offline;
- **SIGKILL between the persist and the POST** — the body is on disk with no receipt, and
  `retry` re-sends *byte-identical* bytes (a re-serialized body would be a different idempotency
  key), then does not repeat; and the two neighbouring kills (after the POST; with an
  `accepted` receipt already cached) which must never send;
- **`accepted → ambiguous` between polls** — the queue holds a fourth `applied` response that
  the loop must never reach, so a loop that ran past a terminal state fails loudly.

### 19.9 Typecheck and suite residue at U14's landing

`bunx tsc --noEmit` reports nothing in U14's six source files, its four suites or
`test/receipt-harness.ts`. The only errors in the tree at landing time are in
`test/legal.test.ts` (U11, mid-landing: `renderLegalCompact` / `LegalCompactResult` not yet
exported). `bun test` is 1248/1248 green, U14's own four suites 88/88.

---

## U11 — `legal`, the legal renderers and the catalog drain

### U11.1 The concurrency upgrade cannot go inside one catalog

PLAN §"two behavioural upgrades" allows bounded-concurrency page fetching, and the U11 brief
puts it here. Inside a *single* catalog it is not implementable and never was: page N+1's URL
is page N's opaque `next_cursor`, so the chain is causal and there is nothing to prefetch. The
Python's own drain is a `for` loop over `range(1, V2_LEGAL_DRAIN_MAX_PAGES + 1)` for exactly
that reason.

Where it does apply is *across* catalogs, which is what `do` actually needs — one drain per
actor. `drainLegalActors(ctx, actorIds)` (`src/services/legal-drain.ts`) runs those with
`Effect.forEach(…, { concurrency: 4 })`, and two properties make it safe to print from:

- **Order is by input, not by completion.** `Effect.forEach` indexes its result by the input
  array, so the caller emits rows in the Python's order however the network interleaves.
  `test/legal-drain.test.ts` asserts that over four runs with randomized per-actor latency.
- **State writes stay single-file.** This is not optional. `withAdvisoryLock` is `flock(2)`
  and its retry loop is `Atomics.wait`, which **parks the thread**. Two fibers of one process
  contending for the `.v2-state` lock therefore deadlock until the 45 s timeout rather than
  queueing. `drainLegalActors` creates a one-permit `Effect.makeSemaphore` and threads it
  through the new `LegalCtx.gate` seam; `readLegalPage` wraps every `.v2-state` critical
  section (the ingest and both failure-path drops) in it. The HTTP round trips overlap, the
  ingest does not.

**U16:** use `drainLegalActors`, not a hand-rolled `Effect.forEach` over `drainLegal` — the
bare `drainLegal` takes no gate and concurrent callers will deadlock on the state lock.

### U11.2 `_limit` (client.py:6513) had no owner

PORT_MAP's line ranges skip 6513-6519, but `_limit` is what turns `--limit` into a *server page
size* for both `state` (U10) and `legal` (U11). It lands as `pageLimit` in
`src/services/legal-query.ts` with the refusal text verbatim
(`limit must be a canonical integer from 1 through 16`). **U10 imports it; U10 does not
reimplement it.** `V2_PLAYER_SCOPED_KIND_PREFIXES` and `V2_RELATION_SCOPED_KIND_PREFIX`
(client.py:8016-8030) are outside core's `constants.ts` ranges for the same reason and are
exported from `src/services/legal-drain.ts`.

### U11.3 `_phase_aware_refusal` is a private copy until U18 lands

`command_legal` wraps its whole body in `_phase_aware_refusal` (client.py:10802-10817), which
sits on U18's row. `src/commands/legal.cmd.ts` carries a private `withPhaseAwareRefusal` built
on U04's `cachedPhaseNote`, so `legal` behaves as CPython does today. **Integrator:** when U18
exports the shared one, delete the private copy — it is eight lines and marked with the port
reference.

### U11.4 The `LegalPageFetcher` seam is narrower than the drain it wraps

PORT_MAP §6 froze U03's callback as
`(sessionPath, session, actorId) => Effect<void, PlayerError | LockTimeoutError, SessionStore | PrivateFs>`.
The real drain also needs `V2Client` and can fail with `V2ResponseError` or `DriftError`, so
`legalPageFetcher(client)` supplies the client and re-raises those two as `PlayerError`
carrying the identical message.

Exit code (2) and the stderr `error: …` line are unchanged. The one divergence: when the
supervisor refuses *during a stale-alias refresh*, CPython's `V2ResponseError` reached
`cli-main` and printed the structured refusal body on stdout; here that body is not printed,
only the message. Widening the frozen type (adding `V2Client` and the two errors) closes it and
costs one line in `src/services/alias-refresh.ts` — U03's file, so it is the integrator's call.

`drainLegal` itself returns `{ revision, actions }` rather than PORT_MAP §1's
`ReadonlyArray<CompactAction>`: `_drain_legal_unlocked` returns the revision, U16 wants the
menu, and both are free.

### U11.5 `_mirror_page` is inert, as it is in U05 and U12

`readLegalPage` takes an optional `LegalCtx.mirrorPage` and calls it with
`promotedCatalogPage(value, promoted)` exactly where CPython does. Nothing supplies it yet:
`state_mirror.update_from_page` is U07's row. `rememberPage` **is** wired, so alias learning
and catalog promotion are not lost — only the `state/*.txt` projection of a legal page is.

### U11.6 The `consuming` default is `in (None, False)`, and Python compares by `==`

`0 == False` is true in CPython, so a numeric zero counts as the default `consuming` and an
integer `1` does not. `legalRowIsDefault` and `legalRow` both use an `isFalseLike` helper
(`null | false | 0`) rather than a truthiness test, because a truthiness test would also swallow
`""` — and a row that silently lost its `!consuming` flag is a unit spent by surprise.

### U11.7 Line budgets are counted in code points

`V2_CATALOG_LINE_MAX` is compared against CPython's `len(str)`, which counts code points, not
UTF-16 code units. `src/render/legal/grouped.ts` uses `[...text].length` throughout so an
astral character in a technology name cannot shift where the choice line truncates.

### U11.8 Two tests could not be ported as written

- `test_v2_compact_legal_pages_resume_after_byte_bound` patches
  `client.V2_LEGAL_COMPACT_MAX_BYTES` down to one byte under a single action. `constants.ts` is
  core's and its exports are `const`, so the port instead builds actions whose compact
  projection genuinely overruns 48 KiB while staying under the 64 KiB single-action ceiling.
  The assertions (`next_offset` `[1, 2, null]`, `byte_limited` everywhere, `oversized_single`
  everywhere, `matched == 3`) are the Python's, unchanged.
- The "catalog changed mid-drain" case uses a *global* page pair whose `total_items` moves,
  not a scoped one whose revision moves: on a scoped catalog `rememberPage` refuses first with
  `legal-action catalog completed before every item arrived`, which is CPython's behaviour too
  (the guard is in `_remember_page`, upstream of `_command_legal_all`'s).

---

## U12 — `turn`: briefing, decisions, phase end

### U12.1 The unit's cross-unit reach is a `TurnSeams` record, wired live

`turn` is the widest consumer in the port: the briefing needs U10's state renderers, the
decision projection needs U11's `compactLegalAction`/`playerScopeAlias` and U15's matcher,
`--end` needs U13's persistence and submission and U14's disposition rendering, and
`--decisions` needs U18's `fetchStateSection`. All of them landed while this unit was being
written, so `liveTurnSeams` (in `src/commands/turn.cmd.ts`) binds every one of them to the
real implementation — nothing in U12 restates a line of another unit.

The indirection is still there, and deliberately: `RenderTurnDeps`, `DecisionDeps` and
`PhaseEndDeps` keep every projection testable against a hand-built cache and a hand-written
mirror TSV, which is how `test/decisions.test.ts` reproduces the Python's three-actor golden
without a server. It is also the seam **U16 reuses**: `do --end --await --brief` builds its own
`TurnCtx` and calls `phaseEnd` / `awaitAndBrief` / `compositeJson` / `renderTurn`.

### U12.2 `renderTurn` returns an `Effect`, not an array

PORT_MAP §1 froze `renderTurn: (ctx, pages) => ReadonlyArray<string>`. It cannot be pure here:
`_render_turn` calls `_coordinates`, `_row_alias`, `_economy_text` and `_research_text`, all of
which raise `_drift` on a payload that does not match the contract, and all of which are
`Effect`-returning in the landed core and in U10. The signature is therefore

```ts
renderTurn(result: TurnResult, deps: RenderTurnDeps, options?): Effect<ReadonlyArray<string>, PlayerError>
```

`phaseEnd(ctx)`, `awaitAndBrief(ctx, options)` and `compositeJson(command, parts)` keep §1's
shape. **U16 codes against this file, not against §1's sketch.**

### U12.3 `_mirror_page` is wired — and the earlier claim that it was harmless was wrong

**Round 1 shipped `TurnHooks.mirrorPage` and the `StartHooks.mirrorPage` handed to
`fetchStateSection` as `() => Effect.void`, and this note called that "correct, one or two
requests more". It was neither.** `_turn_briefing_locked` (client.py:7679-7681) projects all
four `V2_TURN_SECTIONS` pages *before* `_briefing_decision_lines` reads `state/units.tsv` and
`state/cities.tsv`, and after `_mirror_event_count` reads `state/overview.tsv`. With the seam
inert:

- a fresh workspace's briefing carried **no decision block and no `next N actors:` tail**, and
  `turn --decisions` printed `decisions 0 — no actors need orders` while re-fetching both
  sections every call, because `mirrorIsFresh` could never become true;
- `count_chat` was never written, so `briefingEventsLine` reported the whole chat total as new
  on every briefing instead of the delta — the exact opposite of
  `test_v2_briefing_counts_new_events_and_names_the_feed`.

Round 2 binds both hooks to `mirrorPage` (U04/U07's `update_from_page` bridge). It is imported
from `src/services/mirror/update-page` rather than the barrel, because U04's `index.ts` does
not re-export it; `test/mirror-options.test.ts` and `test/mirror-renderers.test.ts` reach it the
same way. **Integrator: when U04 adds `mirrorPage`/`updateFromPage`/`UpdatePageOptions` to the
barrel, switch this import with U05's and U11's.**

CPython names the rows from the `state` dict its four `_remember_page` calls have already
folded back into (`_remember_page` ends with `state.clear(); state.update(current)`), so the
alias column carries the handles this seat just learned. The port re-reads the persisted state
inside the hook, which is the same value: the ingestion is written to disk and the request lock
is held for the whole briefing.

`test/turn-brief.test.ts` §"the live seams a bare `turn` is built from" drives `liveTurnSeams`
against a real workspace rather than a hand-built hook record, so an inert seam fails four
assertions instead of none.

### U12.4 `V2_SHOW_FILES` is resolved through U04's `SECTION_TARGETS`

`_mirror_table` needs the four projection paths (`state/units.tsv`, `state/cities.tsv`,
`state/diplomacy.tsv`, `state/overview.tsv`). U09 owns the full `V2_SHOW_FILES` map; U04 already
carries the identical paths as `SECTION_TARGETS`, so `mirrorFile(section)` in
`src/services/turn-pages.ts` reads that rather than declaring a second copy of the layout.

### U12.5 `V2_ONE_CALL_END` now has one home

`src/render/turn.ts` exports it (client.py:7406). U16's `src/commands/do.cmd.ts` declared a
private copy with a comment saying it must be imported from here "the moment U12 lands"
(NOTES §16.4). **Integrator: delete `do.cmd.ts`'s copy and import it.** `V2_MAX_ORDERS` went the
other way — U15 owns it, and `src/render/decisions.ts` imports it rather than restating 8.

### U12.6 The invariant, and the test that guards it

"An applied phase end never exits non-zero because of how the wait after it turned out."

`commandTurnEnd` carries the *end's* exit code out of the request lock untouched. A wake that
timed out, a wake into somebody else's phase, and a wake with no briefing all leave it at 0; the
transcript says what happened (`woke timeout`, `not briefed: …`) and the status does not. The
one exception is CPython's own: a briefing that *errored* raises the status to 2, because that
error is printed on the same call. `test/turn-brief.test.ts` asserts all four cases, including
the explicit "phase end applies, then the wait times out → exit 0".

The failure arm — `awaitAndBrief` itself refusing — is the one CPython guards hardest, because a
validation error there once hid three consecutive applied phase ends from a live agent. It
prints the receipt, then `phase ended: the receipt above is authoritative`, then
`await failed: …`, then the `do not re-run` sentence, and returns 2 *on the reporting*. Under
`--json` it re-raises instead, so a machine consumer never gets half a payload.

### U12.7 A `--until phase` wake carries no revision, so the header has no `revN/tN`

`test_v2_turn_end_await_ends_the_phase_then_blocks_then_heads` asserts `T3 rev9/t3` on the
await line, but it patches `_wait_value` wholesale — `_validate_wait_response` never runs. The
real validator refuses `until == "phase" and revision is not None` (client.py:2291-2355, ported
in `src/schema/wait.ts`), so a phase wake off the wire always has `state_revision: null` and
`awaitLine` prints a bare `T3`. The port's test asserts the shape the validator actually admits.

### U12.8 CPython `len()` is code points

`_decision_line`'s 120-column budget and `_batch_focus_command`'s 200-column budget are compared
against `len(str)`. `src/render/decisions.ts` counts with `[...text].length` throughout, for the
same reason as §U11.7.

### U12.9 `orderMatch` degrades to "no match", never to a refusal

CPython's `_order_match` cannot raise. U15's port can (`_named` and `_action_target_key` drift).
`liveDecisionDeps.orderMatch` maps any failure to `null`, so one unmatchable option yields to the
next-ranked one instead of turning a whole briefing into an error.

### U12.10 Typecheck and suite residue at U12's landing

`bunx tsc --noEmit` reports nothing in U12's files or tests. `bun test` is green across
`test/turn.test.ts`, `test/turn-brief.test.ts` and `test/decisions.test.ts` (67 cases after
round 2). Mid-round-2 the whole suite was green — 1392 pass, 0 fail, `tsc` silent across the
whole tree. Minutes later `src/services/batch.ts` (`Cannot find name 'isJsonObject'`),
`src/commands/batch.cmd.ts` and `src/services/health-json.ts` were edited by concurrent agents
and now fail `tsc` and six cases in `test/batch.test.ts` / `test/health.test.ts`. **None of
those are U12's files and none of them are reachable from a U12 change** — `turn.cmd.ts` only
imports `submitBatch`'s name from `src/services/batch`.

### U12.11 `_mirror_health` always names the protocol card

`_mirror_health` (client.py:3062-3072) passes `commands=V2_PROTOCOL_CARD` unconditionally.
Round 1's `waitHooks.mirrorHealth` omitted the option, so `updateFromHealth` fell back to
`DEFAULT_COMMAND_CARD` and every wait tick inside `turn --end --await` rewrote
`state/header.txt` with the five-line card; without `--brief` nothing overwrote it afterwards,
so `just show header` diverged byte-for-byte after a phase end. `turn-end.ts`'s own
`mirrorHealth` call had it right all along, which is why the file disagreed with itself. Both
call sites now pass `commands: V2_PROTOCOL_CARD`, and the live-seams block in
`test/turn-brief.test.ts` asserts the woken header carries `ONE CALL PER TURN`.

---

## 20. U16 — `do`, the concurrent drain and the streamed receipt ledger

### 20.1 Everything crossing a unit boundary arrives as `DoHooks`, and `liveDoHooks` binds it

`command_do` is the confluence of six units. The port keeps the orchestration — the order
loop, its refusal ordering, the summary, the phase-end composition, the ledger — in one
place and reaches every other unit through `DoHooks`, the way U05 reaches U03/U04/U06
through `WaitHooks` and U12 reaches everything through `TurnSeams`. `liveDoHooks` binds
each hook to the landed implementation and re-implements none of them: U15's
`orderOutcomes`/`orderFetchTargets`/`unresolvedReport`/`refreshStaleOrderAliases`/
`rebindOrder`, U11's `drainLegal`/`compactLegalAction`/`legalRows`, U13's
`persistBatchForAction`/`submitBatch`, U12's `phaseEnd`/`awaitAndBrief`/`nextFocusLine`.

Three things are *not* hooks, because they are pure and landed: `parseOrders` (U15),
`orderReceiptOk` (U14) and `renderDisposition` (U14) are imported directly. A hook for a
pure function is a second place for its behaviour to be defined, and the receipt line is
the one string in this command that must not have two spellings.

`do` deliberately does **not** call U15's `resolveOrdersFetching` or `refreshOrders`, even
though both are exported. Those are the two loops PLAN's upgrade 1 is about: they drain one
actor at a time. `do.cmd.ts` re-composes them out of U15's finer pieces plus
`drainActors`, which is the whole latency fix. The refusal text either loop produces is
reproduced line for line (`test/do.test.ts`).

### 20.2 The bug concurrency actually introduces is alias numbering, not line order

Line order was the obvious hazard and it is handled the obvious way: collect inside the
concurrent region, emit outside it. `drainActors` returns outcomes indexed by the caller's
input array whatever order the responses landed in, `firstDrainFailure` re-raises the error
CPython's sequential loop would have hit *first by index* rather than first by clock, and no
note, receipt or refusal is ever printed from inside `Effect.forEach`.

The non-obvious hazard is `a1..aN`. Aliases are numbered by `assignActionAliases` at the
moment a catalog is **committed** to `.v2-state`, so overlapping the drains makes the
numbering follow whichever response arrived first. `do`'s own stdout usually does not show
the digits, which is exactly why this would have reached a live match: the next `just legal`
renumbers itself between two identical runs, and an agent that typed `a2` from the previous
screen sends a different action.

So `drainActors` hands each drain a `DrainGate` — a latch chain in which drain *i* may not
enter its critical section until every lower-indexed drain has finished. The fetches still
overlap (one round trip for N first pages instead of N), and the commits happen in the
orders' own sequence, which is CPython's sequence exactly.
`test/do-concurrency.test.ts` pins both halves: 24 runs with shuffled per-request latency
produce one byte-identical transcript, and the refusal case asserts the literal `a3`/`a1`/`a2`
digits rather than merely the section order — the assertion that failed before the gate
existed.

The gate is the same shape as U11's `LegalCtx.gate`, so `liveDoHooks` passes it straight
through and no drain code is duplicated. U11's own `drainLegalActors` uses a one-permit
semaphore in that slot: it serializes the critical section (which is what stops the
`flock(2)` retry loop from parking two fibers of one process) but does not order it. That is
sufficient where it is used and insufficient for `do`; if `drainLegalActors` ever grows a
caller that cares about numbering, it should take a gate the same way.

### 20.3 One place the port and CPython can still differ, and why it is left there

`_refused_actor_options` re-read `.v2-state` after *each* actor's drain and rendered that
actor immediately. The port drains the (at most three) refused actors concurrently, then
re-reads once and renders all of them. The two agree unless one refused actor's drain lands
at a **newer revision than another's** — in which case CPython would render the first actor
against the older cache and this port renders it against the newer one, usually as an empty
section.

It is left as-is because the whole surface is best-effort enrichment that "degrades to
silence" by construction, and because the condition needs the game to move *during* a
refusal that changed nothing. Making it exact would mean serializing the three drains and
giving back the round trips this unit exists to save.

### 20.4 The receipt ledger is bounded so a `SIGKILL` cannot tear a line

PLAN upgrade 2. `state/receipts.log` sits beside `state/monitor.log` and is written through
U04's `appendMirror`, i.e. one `write(2)` on an `O_APPEND`, mode-0600 descriptor.

POSIX makes an `O_APPEND` write seek-and-write atomic but still permits a *short* write for
a large buffer, and a short write is precisely the torn record the ledger exists to prevent.
`ledgerLine` therefore guarantees ≤ 4096 bytes including the newline, and it gets there by
dropping the one unbounded field — `arguments`, which is a worklist of the player's own
choosing — rather than by truncating the text. A truncated line parses as nothing, which is
the one outcome a crash-recovery reader cannot tolerate. `order` is capped at 512 bytes for
the same reason. `readLedger` skips a line it cannot parse instead of refusing the file.

`recordReceipt` cannot fail and cannot print. Not `mirrorGuard`: that helper writes
`warning: the local state mirror was not updated: …` to stderr, which is correct for a
projection the agent is about to read and wrong for a forensic log it is not — CPython wrote
no such byte, and the byte-diff oracle should not have to know about this file at all.
`test/do-concurrency.test.ts` proves a ledger that cannot be written leaves both stdout and
the exit status untouched.

The record is appended immediately after `renderDisposition` and **before** the next order
is persisted, so the kill window between two orders leaves the earlier one durable. The test
kills after order 2 of 4 and asserts exactly two complete, parseable, newline-terminated
lines.

### 20.5 The prelude is a mutable array, because that is what it has to be

CPython passed `_await_and_brief_locked` its own `lines` list and the wait's `tick` closure
printed and `clear()`ed it in place, so a transcript never shows a phase end arriving after
the ten minutes spent waiting for the phase that end opened. U12 landed
`AwaitAndBriefOptions.prelude` as `string[] | null` for exactly that reason, and `do` hands
it the same array it is about to render. A first pass at an idempotent `flush` effect was
replaced by this: the buffer has to be *empty afterwards* in the caller's frame, and only a
shared array gives that. `test/do-end.test.ts` asserts each line appears exactly once
whether the wait ticked or not.

### 20.6 `--json`'s composite carries `unknown`, not `JsonValue`

`AwaitBriefOutcome.wait`/`briefing` are typed `unknown`. `do` never reads inside either —
it forwards them into `_composite_json`'s `end`/`wait`/`turn`/`turn_error` part names and
`printV2Json` serializes them. Typing them as U12's `WaitEnvelope`/`TurnResult` would make
`do` a consumer of shapes it holds no opinion about, and neither satisfies `JsonValue`
structurally (an `interface` has no index signature) without a cast this codebase forbids.

### 20.7 Two argparse behaviours that needed a decision

- **`positional_orders` is `nargs="*"`.** `Args.text({name:'orders'}).pipe(Args.repeated)` is
  the equivalent; `ordersText` then joins them with a space and refuses `ORDERS_TWICE` when
  `--orders` was also given, exactly as CPython does, before a single request leaves.
- **`--until` is `Options.choice('until', ['phase','revision'])`.** *Corrected in round 2 —
  see §20.11.* U05's `wait` can afford `Options.text` because the only thing `--until`
  decides there is which sentence refuses the run; `do` submits the whole batch first, so a
  free-text `--until` changes whether orders reach the wire.

Every refusal above the request lock — orders given twice, the order-count bounds, `--await`
without `--end`, `--brief` without the wake — happens before `makeHooks` runs, so the
Python tests' `blocked.assert_not_called()` holds for the same reasons it held there.

### 20.8 The empty actor id is a value, and `_refresh_orders` drains it

Round-2 correction. `_order_actor` (client.py:8721-8725) reads `subject.actor.id`, and
`actor` is not a declared property of `LegalActionSubject` in
`full-control-v2.openapi.json` at all — so every research, government, phase and
player-family capability resolves with `actor_id == ""`. That is why `command_do` guards
`if resolved["actor_id"]` in four separate places.

Two loops read that empty id and they read it *differently*, which is the trap:

- `_order_fetch_targets` (9281-9301) **skips** it (`or not actor`) — a pre-send fetch of the
  global catalog would be a round trip on nothing. U15 owns that filter.
- `_refresh_orders` (9389-9398) **keeps** it: a plain `drained` set, and
  `_drain_legal_unlocked(actor_id="")` for whatever is in it, which is a full-catalog
  re-enumeration.

`distinctActors` originally applied the first loop's filter to the second loop's list, so
`do "u1 fortify; end"` — where the first receipt bumps the revision and wipes `actions` —
drained nothing, `rebindOrder` saw the wiped cache, and stdout became
`revN/tM no longer offers end; re-read the actor and re-issue those orders` with exit 2 for
an order CPython re-enumerates and applies. U15's own `refreshOrders`
(`src/services/orders/rebind.ts`) never had the guard, which is the second witness that the
divergence was U16's re-composition and not the semantics.

`distinctActors` now dedupes and keeps `""`. Covered end to end in `test/do.test.ts`
("do — the actorless order"), which needed the harness to be able to *express* an actorless
order: `_do-harness.ts` gained `actorless()`/`phaseEndAction()`, serves such a descriptor
with `subject.actor: null`, and serves a global drain's page with **no** `scope`,
`catalog_id` or `catalog_complete` — `decodePageShape` validates the page's field *set*, and
those three keys only exist together.

### 20.9 A drain refusal keeps its wire payload

Round-2 correction. `liveDoHooks.drainLegalUnlocked` mapped every `drainLegal` failure to
`playerError(error.message)`, which collapsed `V2ResponseError` — part of U11's `LegalError`
— into a `PlayerError`. CPython calls `_drain_legal_unlocked` from
`_resolve_orders_fetching` **outside any try/except**, so a supervisor refusal during `do`'s
pre-send catalog drain reaches `main` (11891-11900) and renders
`_render_error_payload(exc.payload)` on stdout — under `--json`, the byte-identical wire
payload. After the collapse the port printed nothing on stdout and only `error: …` on
stderr, on the hottest pre-send path this command has.

The `mapError` is gone. `DoHooks.drainLegalUnlocked` already declared `PlayError`, so
nothing forced the narrowing. The `LegalPageFetcher` handed to U15's
`refreshStaleOrderAliases` still narrows, because that seam's type is U03's and the
narrowing is documented there (§U11.4); it is also the rarely-hit stale-alias refresh rather
than the pre-send drain.

### 20.10 The ledger swallows defects, not just failures

Round-2 correction, and the sharpest of the four: `recordReceipt` used `Effect.ignore`,
which discards only the **typed** error channel. `PrivateFsApi.appendText` runs
`fstatSync`/`fchmodSync`/`writeSync` inside a bare `try/finally` rather than an
`Effect.try`, so an `ENOSPC`/`EDQUOT`/`EIO`/`EPERM` throw arrives as a *defect* and walks
straight past `ignore` — out of `doLocked`'s order loop, after `renderDisposition` pushed
its lines but before `render` printed them, into `cli-main`'s `catchAllCause`, which prints
`error: <Cause.pretty>` on stderr with an **empty stdout** and status 2.

That is this module's own docstring inverted: a `do` CPython would have completed instead
loses every applied `batch_id` from stdout, which is exactly the "agent re-issues a real
duplicate action" incident PLAN upgrade 2 exists to prevent. The neighbouring correct
pattern is U04's `mirrorGuard`, which uses `Effect.catchAllDefect`.

`recordReceipt` now runs through a local `quiet` = `catchAllDefect ∘ ignore`; interruption is
deliberately *not* caught, so a `SIGINT` still cancels the command. `readLedger` and
`readSessionLedger` got the same treatment for the same reason — `readText` has the same
bare `try/finally`, and both functions promise `never` in their error channel.

`test/do-concurrency.test.ts` now exercises the defect path, not only the typed one: the
bench takes a `files` transform (`bench({ files: explodingAppend })`) whose `appendText`
*throws*, because no temporary directory can be made to run out of space on demand. The
transform is applied to `sessionStoreFor`'s file api as well as to the layer — the store
re-provides its own `PrivateFs` inside the request lock, so a layer-only override would
silently miss `do`'s entire locked body.

### 20.11 `--until` is a choice, because `do` sends before it waits

Round-2 correction to §20.7. `do.add_argument("--until", choices=("phase","revision"))`
means argparse refused a bad value at parse time with a usage dump and status 2, having sent
nothing. With `Options.text`, `['u1 x','--until','bogus']` parsed cleanly into `runDo`, the
port submitted every order in the batch, and the bad value only surfaced as
`wait --until must be phase or revision` — and only if `--end --await` was passed at all.
U05's precedent holds for `wait`, where `--until` decides nothing but the refusal sentence;
it does not hold here. `cli-main` already maps `ValidationError` to 2 after `@effect/cli`
prints the usage document, so the observable contract matches.

### 20.12 Gate state at this landing (round 2)

`bun test test/do.test.ts test/do-end.test.ts test/do-concurrency.test.ts` — 53 pass, 0 fail.
`bunx tsc --noEmit` reports **nothing** in U16's four source files, its three suites or
`test/_do-harness.ts`. Two errors elsewhere in the tree are not U16's and are left alone per
the ownership rule: `src/services/health-json.ts:132` (`WireFloat` vs the index-signature
type) and `test/batch.test.ts:287` (`PyObject` vs `JsonObject`), the latter matching the two
`test/batch.test.ts` failures in the full-suite run — U13's `--arguments`/`PyInt` work
mid-landing.

## U12 round 3 — review fixes

### U12.12 `--until` is a choice, and the phase end is why

`turn.add_argument("--until", choices=("phase", "revision"), default="phase")`
(client.py:11670). The port declared it `Options.text('until')`, so a bad value was not refused
until U05's `waitValue` rejected it — which happens *after* `commandTurnEnd` has resolved the
cached `phase.end` capability and submitted the batch. Verified against the real client:

```
$ python3 play/client.py turn --end --await --until phse
client.py turn: error: argument --until: invalid choice: 'phse' (choose from 'phase', 'revision')
exit=2
```

argparse exits 2 from the parser having touched nothing; the port ended the phase, printed
`phase ended: the receipt above is authoritative` / `await failed: …`, and exited 2. Different
stdout, and an irreversible game action on an input CPython rejects up front — on the one
command whose docstring promises the flag matrix is checked before anything opens a socket.
Now `Options.choice('until', ['phase', 'revision'])`, exactly as U16 already did for `do`
(`src/commands/do.cmd.ts`); `cli-main` maps `ValidationError` to 2 after `@effect/cli` prints
the usage document. `test/turn-brief.test.ts` runs the real command through `Command.run` and
asserts the phase-end log is *empty* on the refused argv — checking the exit code alone would
have passed against the bug, since both arms exit 2.

### U12.13 A dict is not an object with a prototype (the same class as §18.10)

Five lookups on this unit's row keyed a plain object by a wire-controlled string where CPython
uses `dict.get`. `OPAQUE_ID_RE` admits `toString`, `constructor`, `valueOf`, `hasOwnProperty`,
and `_validate_descriptor` leaves `subject.operation` an arbitrary string, so every one of these
keys is reachable from the server.

The severe one was `deps.tier1Verbs[word]` in `_decision_order` (`src/services/decisions.ts`).
`word` is `_tier1_word`'s output, which falls back to the wire's `operation`. For `toString` the
index returned `Function.prototype.toString`, `tier1 === undefined` was false, and reading
`tier1.arguments` **throws** `TypeError: 'arguments', 'callee', and 'caller' cannot be accessed
in this context`. A thrown defect is not a typed error, so it escaped the `Effect.orElseSucceed`
guards in `briefingDecisionLines` and `nextFocusLine` — breaking `_next_focus_line`'s stated
contract that it must never turn a successful receipt into an error, and aborting `turn`,
`turn --decisions` and U16's `do` receipt tail. CPython's `V2_TIER1_VERBS.get("toString")`
misses, `defaults` stays `{}`, and the order composes: `u1 toString T(31,72)`, verified by
running the real `_decision_order`.

The other four mis-rendered without throwing, splicing a function body where CPython takes the
fallback: `aliases[action_id]` in `_decision_options`, `pending[name]` in
`_decision_meeting_rows`, and `aliases[actor_id]` / `aliases[target["id"]]` in
`src/services/meetings.ts` (`_meeting_remedy`, and the grouping `_decision_meeting_rows` walks).
The remedy line's whole job is to be copy-pasteable, and
`just legal --actor_id function Object() { [native code] } --all` is not a command.

All five now go through a local `dictGet`/`aliasGet` guarded by `Object.hasOwn`, matching the
hardening §18.10 records on U15's row. `V2_TIER1_VERBS` keeps its normal prototype — only the
lookups changed — because `decisions.ts` re-exports the object. `test/decisions.test.ts` pins
each sentence against the output of the real CPython helper on the same input, including the
control that a table which *really* carries the key still names it (so the guard did not simply
drop the lookup).

Not hardened, deliberately: `state.entity_aliases[alias]` and `openMeetings`' `found[alias]` are
keyed by `ENTITY_ALIAS_RE` (`^([ucpr])([1-9][0-9]{0,3})$`), which admits neither an inherited
member name nor `__proto__`; and `field(item, key)` in `src/render/turn.ts` is only ever called
with literal keys.

### U12.14 Typecheck and suite residue at round 3

`bunx tsc --noEmit` reports nothing in U12's five source files or three test files. The one
error in the tree is `test/do-concurrency.test.ts:389` (U16's, a `JsonValue` array assertion),
untouched by this round. `bun test test/turn.test.ts test/turn-brief.test.ts
test/decisions.test.ts` is green (73 tests) across repeated runs. The full-suite run shows one
or two failures that move between runs and between files (`test/do-concurrency.test.ts`,
`test/show-regex.test.ts`) and pass when those files run on their own — other units are landing
in this tree concurrently, and neither file is reachable from a U12 change.

One test-local widening: `Bench.layer` in `test/turn-brief.test.ts` is now typed
`Layer<V2Client | SessionStore | Workspace | PrivateFs>` rather than `Layer<V2Client |
SessionStore>`. The value always provided all four (it is `Layer.mergeAll(…, scratch.layer)`);
the narrow annotation just prevented running the whole command against the bench.

### U12.15 The `cli-main` placeholder still carries the old `--until` spelling

`src/cli-main.ts:211-225` still declares its own placeholder `turn` (`() => pending('turn')`)
and it is the one wired into the root command at 382/409, so the fix above is not reachable from
`./play turn` until the integrator swaps in `turnCommand` from `src/commands/turn.cmd.ts`.
`cli-main.ts` is core, not U12's to edit. When that swap happens, note that the placeholder's
`until: textOption('until', 'phase')` must **not** be carried across: it is exactly the free-text
declaration §U12.12 replaces, and on the real command it would end a phase on a value argparse
refuses. The same applies to the `do` placeholder, which U16 already fixed on its own row.

## U16 round 3 — review fixes

### 20.13 The gate now refuses commits, not just orders them

§20.2 fixed the *order* commits happen in. It did not fix whether a commit happens at all, and
that turned out to be the same hazard arriving through the failure path.

`_resolve_orders_fetching` (client.py:9339-9348) and `_refresh_orders` (client.py:9389-9398)
are sequential `for` loops that let `_drain_legal_unlocked` raise. CPython therefore never
fetches and never *ingests* the actors after the failing one. The port ran every drain and only
picked the first failure by index afterwards, so the siblings were committed: they joined
`drained_actors`, their descriptors entered `actions`, and `assignActionAliases` numbered them.
The old docstring's claim that an uncommanded drain is "only a warm cache entry, never a printed
line" was false in two ways at once:

- `_order_fetch_targets` (client.py:9281-9301) skips any actor already in `drained_actors`, and
  the `fetched uN options (revM)` note is only emitted for actors actually fetched. So the
  agent's retry of the identical batch printed one note fewer than CPython prints — a dropped
  stdout line on the ordinary success path.
- the sibling consumed `a1..aM`, so the next `just legal --actor_id … --all` prints different
  alias digits than CPython does. That is §20.2's silent-renumbering incident exactly.

`DrainOptions.stopOnFailure` (default `true`) closes this. The latch chain now carries a
boolean — "every index above this one is uncommanded" — instead of `void`; once drain *i* has
failed, the gate at every higher index declines to admit its body and that drain reports
`DrainOutcome.skipped`. The redundant *fetch* may still have gone out, which is invisible: a GET
that ingests nothing leaves no trace in `.v2-state`, no note and no alias.

`_refused_actor_options` (client.py:9433-9440) is the one loop with the opposite shape — it
catches per actor and `continue`s — so `src/render/actor-options.ts` is the only caller that
passes `stopOnFailure: false`, and it is commented as such at the call site.

Mechanics worth knowing before touching `drainActors`:

- A closed gate signals with a module-private `Symbol.for('play-cli/do-drain/not-issued')`
  **defect**, not a typed error. The gate's type is U11's `<A, E, R>(body) => Effect<A, E, R>`
  and may not widen the drain's error channel; a typed refusal would also be indistinguishable
  from a real drain failure at the seam that reads the outcomes. The defect is recognised and
  converted back to an ordinary `skipped` row inside the same function, so it never escapes.
- `Effect.match` became `Effect.matchCauseEffect` with `Cause.stripFailures` on the
  no-typed-failure branch. That keeps the previous contract that genuine defects and
  interruption are *not* captured as rows — only the private marker is.
- A `drainOne` that ignores its gate is still reported `skipped` when a lower index failed. The
  row must never claim a commit CPython did not make, whatever the seam did with the gate.

Pinned in `test/do-concurrency.test.ts`: the primitive both ways round (`stopOnFailure` true and
false), the gate-ignoring seam, a genuine defect still escaping, and two end-to-end cases —
three cold seats with the middle drain failing leaves `drained_actors == [u1]` and `a1` alone,
and the retry then prints *both* `fetched u2 options (rev8)` and `fetched u3 options (rev8)`;
and the mid-batch `_refresh_orders` failure leaves the alias table exactly as it found it.

### 20.14 `except OSError` is the defect channel, and the refusal options needed it too

§20.10 documents this for the ledger and fixed it there. The same hole was still open in
`src/render/actor-options.ts`: `refusedActorOptions` guarded only the typed channel with
`Effect.orElseSucceed`, while `_refused_actor_options` catches `(PlayerError, V2ResponseError,
OSError)` per actor under the docstring "Every failure below degrades to silence."

`PrivateFsApi.readText` (`src/services/private-fs.ts:299-319`) runs its `fstatSync`/
`readFileSync` inside a bare `try/finally` rather than an `Effect.try`, so an `EIO`/`EACCES`
while re-reading `.v2-state` — or inside the ingest a drain does — arrives as an Effect
**defect**. It sailed past `orElseSucceed`, escaped `doLocked` and `runDo` before `render(lines)`
ever ran, and reached `cli-main`'s `catchAllCause` (`src/cli-main.ts:536`) as
`error: <Cause.pretty>` on stderr with an **empty stdout** and status 2 — losing an applied,
server-issued `batch_id` from a command CPython completed, which is the duplicate-action
incident this unit exists to prevent.

Now `silent()` (`orElseSucceed` + `catchAllDefect`) wraps both `readState` calls and each
`actorOptionsSection`, and `survivable()` converts a drain's defect into the typed failure
`drainActors` already records per actor. Interruption is deliberately left uncaught, exactly as
in the ledger: a `SIGINT` must still cancel the command rather than be absorbed by a menu.

Two cases in `test/do.test.ts` cover it end to end — `u1 move …; u1 found_city …`
`--continue-on-error` with order 1 applied and order 2 rejected, once with a throwing drain and
once with a throwing `.v2-state` re-read. Both assert the applied `batch_00000001` line, the
`1/2 applied` summary and the focus tail all still print, and that the section that could not be
built is simply absent. Note for anyone writing a similar test: `runDo` builds its `readState`
effect **once** and yields it three times, so a counting stub has to sit inside `Effect.suspend`
or it counts descriptions rather than runs.

### 20.15 Gate state at this landing (round 3)

`bun test test/do.test.ts test/do-end.test.ts test/do-concurrency.test.ts` — 60 pass, 0 fail.
`bunx tsc --noEmit` reports nothing in U16's four source files, its three suites or
`test/_do-harness.ts`. The errors left in the tree are U13's, not U16's, and are left alone per
the ownership rule: `src/services/batch.ts:118,125,131,164`, `src/services/v1-json.ts:173` and
`test/batch-persist.test.ts:162,247`, all `Property 'failed'/'duplicateKey' does not exist on
type 'PyParse'` — the `PyParse` shape is mid-landing, and the same drift accounts for the
26 full-suite failures, none of which is in a U16 file.

---

# I. Integration — gates, the offline oracle, and what is still open

Written by the integrator after wiring all twenty subcommands. Everything above this line is a
unit's account of its own port; this section is the whole-CLI state.

## I.1 Gate state at this landing

| gate | result |
| --- | --- |
| 1. every subcommand wired; `bun run src/bin.ts --help` lists them | **20/20** — the 18 argparse commands plus the two doc surfaces the justfile carried. See §I.5: "wired" once meant "registered", and `start`'s registration reached a bundle that refused at its first seam |
| 2. `bunx tsc --noEmit` | **clean** |
| 3. `bun test` | **1643 pass, 0 fail**, 58 files |
| 4. offline byte-diff (`test/diff-offline.sh`) | **62/62 identical** on the named workspace, and 62/62 on each of the other five under `.play/` — 372 stdout comparisons, zero diffs, zero irreducible ones to record |
| 5. `bun build --compile` → `dist/play`, smoke `--help` | **built** (61 MB, 814 modules), `--help` exits 0 and lists all 20; the oracle re-run with `PLAY_BIN=dist/play` is also **62/62** |

Gate 4 is byte-clean, so there is no "irreducible diff" table in this section. That is a
statement about the *read-only, offline* surface only — see §I.3 for what the oracle cannot see,
and §I.5 for six divergences that were live on the *mutating* surface while all five gates were
green.

**Gate 1's original entry read "20/20" unqualified, and that was the misleading number.**
`cli-main` did register twenty `Command`s, but registering `startCommand` registered
`startCommandWith(liveStartHooks)`, and `liveStartHooks` still refused at five seams. Counting
registrations is not a gate on any of them doing anything. The row now says what it measures.

## I.2 The four `--json` surfaces that embed a health envelope — fixed

NOTES §10.6 ended: *"**U05/U12/U16 own their own `--json` call sites; U06 cannot land these three
lines.** Until they do, `turn --json` and `wait --json` still diverge."* They had not landed. The
integrator landed them, since finishing an unapproved unit's small remainder is the integrator's
job and this one was three imports and four expressions:

| surface | file | projection |
| --- | --- | --- |
| `wait --json` | `src/commands/wait.cmd.ts` | `healthFloatPathsUnder('health')` |
| `turn --json`, and the briefing inside `--end --await --brief` | `src/commands/turn.cmd.ts` (`emitTurn`) | `healthFloatPathsUnder('context')` |
| `turn --end --json`, `turn --end --await --brief --json` | `src/services/turn-end.ts` | `healthFloatPathsUnder('wait.health', 'turn.context')` |
| `do --end … --json` | `src/commands/do.cmd.ts` | `healthFloatPathsUnder('wait.health', 'turn.context')` |

Each is `Console.log(pyDumps(pyValueWithFloats(payload, paths), true))` in place of
`printV2Json(payload)`. The prefixes are load-bearing and were checked against the payload
builders, not guessed: `WaitEnvelope` nests the envelope under `health` (`src/schema/wait.ts`),
`TurnResult` nests `_turn_health_context` under `context` (`src/render/turn.ts`), and
`turnEndJson`/`do`'s composite nest those two under `wait` and `turn`. A wrong prefix is a
**silent no-op** — the projection simply marks nothing — which is why the new tests assert the
key names as well as the bytes.

**`turn --decisions --json` is deliberately left on `printV2Json`.** §10.6's table lumped it in
with `turn --json`, but its payload is `{schema_version, command, status, state_revision,
decisions}` (client.py:7534-7547) — no `phase`, no `last_phase_end`, no float to lose. Applying
the projection there would be a no-op that implied a contract it does not have.

**New file: `test/json-floats.test.ts`** (core row, PORT_MAP §I.4). Thirteen cases. Two things
in it are worth defending:

- It asserts the *absent* spelling as well as the present one, and carries an explicit
  self-proof (`compactJson` of the same block contains `"timeout_s":600}` and none of the float
  tokens). Without that, a revert to `printV2Json` could pass vacuously.
- It also reads the four source files by name and asserts each mentions `pyValueWithFloats`.
  A source-shaped assertion is unusual and normally a smell. It is here because §10.6's failure
  mode was **inaction**, not a wrong value: `healthPyValue` shipped with zero importers and the
  entire suite stayed green, because nothing asserted those bytes at all. `test/docs-surfaces.test.ts`
  sets the precedent for a test that reads repo files. When §I.3's permanent repair lands, this
  guard and every path list in this file delete together.

## I.3 What the offline oracle cannot see, and what is still open

Gate 4 covers `help`, `rules`, `prompt`, `use` and the whole of `show` — the surface that runs
without a socket. Four things sit outside it and are **not** proven byte-clean by a green gate 4.

### I.3.1 Bun does not trust the CA the local CPython trusts — the top operational blocker

Running `health` against the live supervisor from the copied workspace:

```
$ python3 -B client.py health
health running | YOUR TURN · t118/p0 · 9m32s left of 10m0s | …            # exit 0

$ bun src/bin.ts health
error: cannot reach the Freeciv supervisor at https://freeciv-api.localhost:
self signed certificate in certificate chain. …                            # exit 2
```

This is not a port bug in any unit; it is a trust-store difference between the two runtimes.
CPython's `ssl` uses OpenSSL's default verify paths — here
`/opt/homebrew/etc/openssl@3/cert.pem`, which carries the local development CA — and honours
`SSL_CERT_FILE`/`SSL_CERT_DIR`. Bun ships its own Mozilla bundle and reads neither; it reads
`NODE_EXTRA_CA_CERTS`. Confirmed by running the same command with it set, which succeeds and
renders the live phase correctly:

```
$ NODE_EXTRA_CA_CERTS=/opt/homebrew/etc/openssl@3/cert.pem bun src/bin.ts health
health running | NOT YOUR TURN · seat 2 AgentPlace2 (…) holds t118/p1 · … # exit 0
```

**Deliberately not fixed in code.** The parity-faithful fix would be "trust whatever OpenSSL the
local interpreter was built against trusts", and that set is not discoverable from Bun —
`ssl.get_default_verify_paths()` is a CPython/OpenSSL fact, not an environment variable, and it
is unset here. Honouring `SSL_CERT_FILE` in `src/services/http.ts` would be closer to CPython
but would still not have fixed this case, because `SSL_CERT_FILE` is not set. Choosing which CA
bundle the CLI trusts is an operator decision with a security consequence, so it is recorded
rather than hardcoded. **Whoever deploys this must export `NODE_EXTRA_CA_CERTS` (or a
`--tls-ca`-shaped option someone adds deliberately) wherever the supervisor uses a private CA.**

**RESOLVED (justfile-cutover follow-up).** The operator decision is now made at provisioning:
`play_setup.py` records the local stack's CA (env `AGENT_EVAL_TLS_CA`, else `~/.portless/ca.pem`
when the service URL is https) as `tls_ca` in the workspace `.playconfig.json`, and
`caTrustedFetch` (`src/services/http.ts`) resolves it per request — `PLAY_TLS_CA=<path>` overrides,
`PLAY_TLS_CA=` (empty) opts out, `.playconfig.json` `tls_ca` is the provisioned default, and a
configured-but-unreadable CA fails the request naming the path rather than degrading to the
untrusted default. Both `HttpLive` (v2) and `V1JsonLive`/`v1Json` (join/next/act/result) go
through the wrapper — the first live run only patched v2, and `result` still failed the
handshake until the v1 transport was wrapped too. Proven against the live supervisor through
both configuration paths, source and compiled. Covered by `test/http-tls.test.ts`.

### I.3.2 The permanent float repair is still core's, and still not done

NOTES §2, §10.5 and §11.9 all converge on one line of intent: have `src/services/http.ts` decode
response bodies with U13's `parsePython` (`src/services/canonical-body.ts`) instead of
`JSON.parse`, so an `int`/`float` distinction survives from the wire rather than being
reconstructed from a field list. §I.2 fixed the four surfaces that carry a *health* block, which
is the finite, enumerable case. It does **not** cover a supervisor float anywhere else: `receipt`
and `retry`'s timings, `batch`'s disposition, `state`/`legal` page items under `--json`. Those
still print `600` where CPython prints `600.0` whenever the wire carried an integral float.

Not attempted here because it is not small: `Http.requestJson` is typed `Effect<JsonObject, …>`
and every decoder in `src/schema/` narrows against plain JavaScript numbers, so making `PyFloat`
survive changes the type of the whole wire layer. It is a deliberate, reviewable change of its
own, and it should land with the oracle extended to a live-server mode (§I.3.4) rather than
before it.

### I.3.3 `state/phase.json`'s bytes, per §12.4 of U04's section

U04 recorded that `phase.json` writes `139` where CPython writes `139.0`. Unchanged, and the
oracle cannot see it: `phase.json` is only reachable through `show phase`, which needs a `health`
call to have written it. Same root cause as §I.3.2, same repair.

### I.3.4 The oracle has no live-server mode

`test/diff-offline.sh` is offline by construction, and stays that way — the brief forbids running
commands against a server, and a shared live game is not a fixture: two clients diffing `health`
against the same supervisor see different turns. The honest way to extend it is a **recorded**
mode: capture the wire traffic of one Python session once, replay it into both clients through
`httpFor(fake)`, diff. That is the missing third of the oracle, and every open item above
(§I.3.1 aside) would be caught by it.

## I.4 Unapproved units — what was reviewed and what was found

The brief listed U01, U02, U03, U04, U05, U09, U10, U12, U13 and U16 as unapproved leftovers.
What the integrator can honestly claim about them:

- **Covered by a green gate.** U01 (`prompt`/`help`/`rules`), U04 and U09 are the units the
  offline oracle exercises end to end, across six workspaces. `show` alone drives U04's parsers,
  U09's renderers and U07/U08's writers over real game data, and every byte matches.
- **Fixed here.** U05, U12 and U16's `--json` float call sites (§I.2). This was a live-invocation
  divergence on every one of those commands, and it was open in NOTES with the fix spelled out.
- **Reviewed, nothing found.** Zero `any`, zero `@ts-ignore`, zero `@ts-expect-error` in `src/`.
  Four `as unknown as` casts remain, all in core files and all narrowing an untyped Node
  constant table (`fs.constants` in `private-fs.ts`, `locks.ts`, `monitor-lock.ts`) or the
  `.v2-state` document (`session-store.ts:525`); none is a wire payload.
- **Not independently verified.** U02 (`join`), U03's alias renumbering under a live revision
  bump, U10 (`state`), U13 (`batch`) and U16's submit path have unit tests and no oracle
  coverage, because every one of them needs a socket. They are green on their own suites — 1637
  tests — and untested against the Python. §I.3.4 is what would close that, and it is the single
  highest-value piece of work left.

## I.5 The inert-seam round: six divergences that survived five green gates

Every finding in this round has the same shape. A unit landed before the unit it depends on,
declared the dependency as an injected seam, filled the seam with a **no-op or a refusal**, and
wrote a comment saying the integrator would swap it. Then the gates went green, because a no-op
projection writes no file and an unfetched catalog issues no request — and nothing offline can
tell "wrote the right file" apart from "wrote no file at all". The repair is the same in every
case, and it is the durable half of this section: **the seam's default is now the real
implementation, and the parameter is an override.** A future unit that forgets to wire one gets
CPython's behaviour rather than silence.

### I.5.1 `play start` was dead in the shipped build

`startCommand = startCommandWith(liveStartHooks)` (`src/commands/start.cmd.ts`), and
`liveStartHooks` returned `Effect.fail(notWired(…))` for `resolveKindAction`, `drainLegal`,
`persistBatchForAction`, `submitPersistedBatch` and `renderDisposition`. `configureAndReady`
calls `resolveKindAction('pregame.configure', …)` unconditionally once the lobby check passes,
so in the *only* situation the command exists for — a lobby seat — `just start` exited 2 with

```
error: start cannot enumerate the pregame.configure capability in this build:
U11 owns that seam and has not landed yet
```

where `command_start` (client.py:11013-11190) configures the seat and readies it. U11, U13 and
U14 had all landed; PORT_MAP's U18 addendum names this exact follow-up. `liveStartHooks.mirrorPage`
was `() => Effect.void` too, so `_fetch_state_section`'s `_mirror_page(…, "start")`
(client.py:10883) never ran and the pregame catalogs the same command later reads back were
never written.

Now bound to the landed entry points: U07's `mirrorPage`, U11's `drainLegal`, U12's
`resolveKindAction` (through the `TurnCtx` `liveTurnSeams` already assembles, rather than a
second copy of the cache-then-drain-then-cache dance), U13's `persistBatchForAction`/`submitBatch`,
U14's `renderDisposition`/`orderReceiptOk`.

**One type change followed.** `StartHooks.submitPersistedBatch` yielded a `JsonValue`
disposition, and U13 produces a decoded `BatchDisposition` — a nominal interface, which is not
assignable to `JsonObject` (interfaces get no implicit index signature). Rather than rebuild the
envelope as a `JsonValue` — which would have to re-serialize a receipt whose decoder passes
unknown fields through, and would drop exactly those fields from `start --json` — `SubmitOutcome`,
`renderDisposition` and `receiptOk` now carry `BatchDisposition`, and `startJson`'s
`dispositions` is `ReadonlyArray<unknown>`, the same shape `do`'s composite payload already used
for the same reason. `pregame.ts`'s local `orderReceiptOk` collapsed into a re-export of U14's,
closing one of the three duplications PORT_MAP's U18 amendment asked the integrator to collapse.

`test/start.test.ts`'s "the shipped hooks refuse rather than pretend" case was the bug written
down as an assertion; it is now the regression guard, running `liveStartHooks` against the same
fake supervisor and asserting the whole sequence — `health`, `nations`, `legal`, `batch`,
`legal`, `batch` — with U14's real disposition lines. Its responder also had to stop comparing
`JSON.stringify(state_revision)` against this file's literal: `_persist_batch_for_action` writes
the *canonical* body, whose keys are sorted, so the string compare answered "different" for the
same revision and handed the configure batch the readied revision.

### I.5.2 `legal` and `do` never projected their catalog pages

`_read_legal_page` (client.py:7895) calls `_mirror_page(path, state, _promoted_catalog_page(…),
"legal")` unconditionally, and that is the **only** path that reaches U07's `updateOptions`. In
the port `LegalCtx.mirrorPage` was optional and every caller — `runLegal`, `do`'s per-actor
drains, `turn`'s `phaseEnd.drainLegal`, U03's `legalPageFetcher` — built `{ sessionPath, session }`
without it, so `state/options/<alias>.txt` and the actions projection stayed frozen at whatever
the workspace shipped with while CPython's stayed current. That is a stdout divergence on every
later `show options/u1`, `show` and `show --grep`, in a file `V2_PROTOCOL_CARD` (which `join`
prints and `show header` echoes) tells the agent to read.

`readLegalPage` now mirrors by default and takes an override, so all four call sites are correct
without touching any of them. The hook gained the alias map as a parameter: CPython names the
rows from the state `_remember_page` has already folded back into the caller's dict, and
`rememberPage` returns that state, so no extra read is needed.

### I.5.3 `_mirror_receipt` was never called anywhere

CPython calls it at four sites: `_submit_persisted_batch` (8528 — every `batch`/`do`/`start`/
`retry` send), `command_receipt` (9797) and `_command_retry_locked` (9884, 9899). In the port
`liveReceiptHooks.mirrorReceipt` was `() => Effect.void`, `liveBatchHooks.mirrorReceipt` was
`inertReceiptMirror`, and `do`/`turn` called `submitBatch` with no options at all — so they got
the inert default. `updateFromReceipt` appends the `applied batch … at rev N` line plus the
`state files still show rev N; run \`just turn\` to refresh them` lag sentence to
`state/delta.md`, and `show delta` renders that file **verbatim**: the in-code justification
("it costs the mirror one section and stdout nothing") was simply wrong.

`submitBatch` now mirrors by default — it already holds the session path and requires
`PrivateFs` — and `SubmitOptions.mirrorReceipt` is the test override. `BatchHooks.mirrorReceipt`
is gone rather than re-pointed, because a hook whose only live value is the default is a way to
forget the default. `liveReceiptHooks.mirrorReceipt` is U07's bridge.

### I.5.4 `batch` never re-bound a stale alias, and dropped two stdout surfaces

`liveBatchHooks` still stubbed four seams whose owning units had all landed — PORT_MAP §8's
integrator checklist items 2 and 4:

- `fetchLegal: undefined`, so `resolveAliasArguments` ran without a drain and **every**
  invocation behaved as though `--no-refresh` had been passed. `play batch --action-id a1` in a
  workspace one revision behind exited 2 with `action alias a1 was enumerated at rev784/t104 but
  this seat now knows rev785/t104 …` where CPython re-enumerates and reaches the wire. This is a
  functional narrowing on the hot path after any revision bump, not only a byte diff.
- `orderActor: () => ''`, so `actor` was always empty and both of the surfaces keyed on it
  vanished: `_next_focus_line` after an accepted receipt (8595-8600) and
  `_refused_actor_options` after a refused one (8601-8604). The refusal case is the one that
  matters — CPython answers a rejected `batch` with up to `V2_REFUSAL_LEGAL_ROWS` of what the
  actor can actually do, and the port answered with nothing.

`orderActor` is now U15's over U11's `compactLegalAction` (so the hook returns an `Effect`, as
the projection can fail), `refusedActorOptions` is U16's over U11's `legalRows`, `nextFocusLine`
is U12's. `fetchLegal` stays `undefined` in the bundle and `runBatch` falls back to
`legalPageFetcher(client)`: `LegalPageFetcher` is frozen with `V2Client` absent from its
requirements (PORT_MAP §6), so the client has to be supplied where the Layer stack has already
provided it. `undefined` now means "the live one"; `--no-refresh` is how a caller asks for the
plain refusal.

`test/batch.test.ts`'s "--json prints exactly one canonical object" asserted
`text.out).toHaveLength(1)` — it was pinning the *missing* focus line. It now asserts both lines.

### I.5.5 `show --grep --regex` refused every pattern that began with `\b`

`boundaryMatcher` emitted `(?:(?<=WORD)(?!WORD)|(?<!WORD)(?=WORD))` where `WORD` was
`rangesMatcher(UNICODE_WORD_RANGES)` — a ~50 KB alternation of one BMP class and ~230
surrogate-pair branches. JavaScriptCore tries every branch at every position where the character
is not a word character, which is most positions in most subjects, and a *leading* `\b` is
tested at every offset of every line. Measured on
`.play/game_Hsit9YEuBjKdJPPouFoGVYlk_pi_gpt-5.6-sol`: `show --grep '\bu1\b' --regex` took 2.2 s
against `V2_SHOW_GREP_BUDGET_S = 2.0` and exited 2 with `just show --grep --regex took too
long`, where CPython returned the matches in 0.15 s and exited 0. `\bSettlers\b` was 2.17 s and
`\Bx` the same; trailing boundaries (`u[0-9]+\b`, `\d\b`) were 0.20 s, because the preceding atom
had already narrowed where the assertion is tested. It is specifically a leading boundary — the
shape of the single most common regex an agent writes — and the oracle's five `--regex` cases
contained no `\b`, which is why gate 4 stayed green.

`rangesAssertion` replaces the single lookaround with two alternatives: the BMP class, or a
one-character surrogate-half guard followed by the pair alternation. Rejecting an ordinary
character now costs two class tests instead of ~230 branches. Measured on 3,000 non-matching
mirror-shaped lines: 299 ms → 15 ms, a 20× improvement, and the same for `\Bx` and
`\bSettlers\b`.

Proved unchanged rather than assumed: 572 hand-picked boundary cases and 76,872 generated ones
(4,000 patterns over a boundary-heavy alphabet × 28 subjects including astral, ligature and
sharp-s text) were run through both this compiler and CPython 3.14's `re` — every match position
and matched text identical, byte for byte. `test/diff-offline.sh` gained four `\b` cases so the
oracle can see it next time, and `test/show-regex.test.ts` gained the semantic assertions.

### I.5.6 `_table` measured columns in UTF-16 units

`src/render/primitives.ts`'s `table()` — the `_table` port every `state`/`legal` row renderer
prints through — used `cell.length` and `cell.padEnd()`, where CPython uses `len()` and
`str.ljust()`, and `.replace(/\s+$/, '')` where CPython uses `str.rstrip()`. PORT_MAP §1's
round-2 rule is explicit ("Never `.length`/`.slice`/`.padEnd`") and U04 exports
`codeLength`/`ljust`/`rstrip` for exactly this; U04's own `src/services/mirror/table.ts` uses
them and core's did not. Proven end to end against CPython:
`_table([['c1','🚀🚀🚀','sz5'],['c2','Berlin','sz3']])` is `'c1  🚀🚀🚀     sz5'` in Python and was
`'c1  🚀🚀🚀  sz5'` here — three bytes of padding lost, and every following cell in the row
shifted. City names are agent-supplied through `found_city`, so this is reachable input on a live
match; it is invisible to the offline oracle because that workspace has ASCII names. The
whitespace class differs in **both** directions (`\s` matches U+FEFF, which `str.rstrip` keeps;
`str.rstrip` strips U+001C-U+001F and U+0085, which `\s` does not), so both are asserted.

Imported from `src/services/mirror/store` rather than the `src/services/mirror` barrel: that
barrel pulls in `src/render/mirror/*`, every one of which imports `primitives` back.

## Cleanup

Scratch paths left by the inert-seam round, for the single end-of-run pass (PLAN §"Defer all
cleanup to the end"). Nothing in the repo depends on any of them.

- `/private/tmp/claude-501/-Users-cryogenicplanet-general-game-eval-freeciv/764655d2-e683-4042-93f6-8b0d2b5a0e37/scratchpad/`
  — the CPython/port regex differential harness (`probe.py`, `gen.py`, `run.py`, `cases.json`,
  `py-regex.txt`, `ts-regex.txt`, `py-rand.txt`, `ts-rand.txt`). 76,872 generated cases plus 572
  hand-picked ones; regenerate with the two scripts rather than keeping the outputs.
