/**
 * `show --grep` — the case fold and the `re` dialect, against CPython.
 *
 * `--grep` is the flagless default of the unit that carries most of the
 * byte-diff oracle, and it is where a JavaScript-shaped shortcut costs the agent
 * a wrong answer without saying so.  Two shortcuts were taken and are gone:
 *
 * * `String.prototype.toLowerCase()` for `str.casefold()`, which missed the row
 *   holding `Große` on a search for `grosse` — and `Große` is a live string in
 *   this repo's `data/nation/*.ruleset` leader lists.
 * * `new RegExp(pattern, 'i')` for `re.compile(pattern, re.IGNORECASE)`, which
 *   read `\A` and `\Z` as literals, refused `(?i)`, `(?s)`, `(?#…)` and
 *   `(?P<x>…)` that `re` accepts, and accepted `\p{L}`, `\cA`, `\8` and
 *   `(?<name>…)` that `re` rejects.
 *
 * Every `GOLDEN` entry is the verbatim stdout, stderr and exit code of
 * `python3 client.py show …` run against the byte-identical `MIRROR` below,
 * captured from CPython 3.14 — including the `at position N` inside each `re`
 * message, which the port now reproduces rather than paraphrasing.
 *
 * The mirror is deliberately not ASCII: `Große`, `Straßburg`, `ΟΔΟΣ` (whose
 * final sigma folds onto a medial one) and `Eﬀort` (whose ligature folds onto
 * `ff`) are the four folds a `toLowerCase` port gets wrong.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either, Layer } from 'effect';
import { runShow } from 'src/commands/show.cmd';
import { PY_IGNORECASE, compilePythonRegex } from 'src/render/show-regex';
import { casefold } from 'src/render/show-unicode';
import { v2StateSchema } from 'src/services/aliases';
import { PrivateFs } from 'src/services/private-fs';
import { SessionStore, sessionStoreFor } from 'src/services/session-store';
import { FIXTURE_AGENT_ID, FIXTURE_GAME_ID, scratchWorkspace, sessionFile, type Scratch } from 'test/_fixtures';

interface Golden {
  readonly args: ReadonlyArray<string>;
  readonly code: number;
  readonly stdout: string;
  readonly stderr: string;
}

const MIRROR: ReadonlyArray<readonly [string, string]> = [
  ["state/header.txt", "# rev 7 turn 3\ngame      game_12345678901234567890 · seat 1 AgentPlace1 · controller codex-test\nobjective Maximize final Freeciv civilization score.\nbudget    turn 3 of 5000 · 4997 remaining\nphase     awaiting_agent · turn 3 phase 0 · active yes\n"],
  ["state/units.tsv", "# rev 7 turn 3\n# units 4/4 complete\nalias\tunit    \twho\tpos  \tmoves\thp   \tactivity\torders\nu1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\nu2   \tWorkers \town\t30,71\t3/3  \t10/10\tidle    \t-\nu3   \tGroße Garde\town\t29,70\t1/1  \t20/20\tidle    \t-\nu4   \tStraßburg Wache\town\t28,69\t1/1  \t20/20\tidle    \t-\n"],
  ["state/cities.tsv", "# rev 7 turn 3\n# cities 3/3 complete\nalias\tcity  \tpos  \tsize\tfood\tshields\nc1   \tLondon\t31,72\t1   \t2/2 \t1\nc2   \tΟΔΟΣ\t30,70\t2   \t3/3 \t2\nc3   \tEﬀort\t29,69\t3   \t4/4 \t3\n"],
  ["state/delta.md", "# rev 7 turn 3\nsince rev 6 turn 3 · last update: state\n\n## units\n- u1 moved to 31,72\n"],
];

const GOLDEN: Readonly<Record<string, Golden>> = {
  "grep_GROSSE": {
    args: ["--grep", "GROSSE"],
    code: 0,
    stdout: "units:6: u3   \tGroße Garde\town\t29,70\t1/1  \t20/20\tidle    \t-\n",
    stderr: "",
  },
  "grep_Grosse": {
    args: ["--grep", "Große"],
    code: 0,
    stdout: "units:6: u3   \tGroße Garde\town\t29,70\t1/1  \t20/20\tidle    \t-\n",
    stderr: "",
  },
  "grep_STRASSBURG": {
    args: ["--grep", "STRASSBURG"],
    code: 0,
    stdout: "units:7: u4   \tStraßburg Wache\town\t28,69\t1/1  \t20/20\tidle    \t-\n",
    stderr: "",
  },
  "grep_astral_200": {
    args: ["--grep", "🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂"],
    code: 0,
    stdout: "no mirror line matches '🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂'\n",
    stderr: "",
  },
  "grep_astral_201": {
    args: ["--grep", "🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂"],
    code: 2,
    stdout: "",
    stderr: "error: just show --grep takes a pattern of at most 200 characters\n",
  },
  "grep_final_sigma": {
    args: ["--grep", "οδος"],
    code: 0,
    stdout: "cities:5: c2   \tΟΔΟΣ\t30,70\t2   \t3/3 \t2\n",
    stderr: "",
  },
  "grep_grosse": {
    args: ["--grep", "grosse"],
    code: 0,
    stdout: "units:6: u3   \tGroße Garde\town\t29,70\t1/1  \t20/20\tidle    \t-\n",
    stderr: "",
  },
  "grep_ligature": {
    args: ["--grep", "effort"],
    code: 0,
    stdout: "cities:6: c3   \tEﬀort\t29,69\t3   \t4/4 \t3\n",
    stderr: "",
  },
  "grep_long_s": {
    args: ["--grep", "ſettlers"],
    code: 0,
    stdout: "units:4: u1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\n",
    stderr: "",
  },
  "grep_micro": {
    args: ["--grep", "µ"],
    code: 0,
    stdout: "no mirror line matches 'µ'\n",
    stderr: "",
  },
  "grep_sharp_s": {
    args: ["--grep", "ß"],
    code: 0,
    stdout: "units:6: u3   \tGroße Garde\town\t29,70\t1/1  \t20/20\tidle    \t-\nunits:7: u4   \tStraßburg Wache\town\t28,69\t1/1  \t20/20\tidle    \t-\n",
    stderr: "",
  },
  "grep_sigma": {
    args: ["--grep", "οδοσ"],
    code: 0,
    stdout: "cities:5: c2   \tΟΔΟΣ\t30,70\t2   \t3/3 \t2\n",
    stderr: "",
  },
  "grep_strassburg": {
    args: ["--grep", "strassburg"],
    code: 0,
    stdout: "units:7: u4   \tStraßburg Wache\town\t28,69\t1/1  \t20/20\tidle    \t-\n",
    stderr: "",
  },
  "regex_A_anchor": {
    args: ["--grep", "\\Aalias", "--regex"],
    code: 0,
    stdout: "units:3: alias\tunit    \twho\tpos  \tmoves\thp   \tactivity\torders\ncities:3: alias\tcity  \tpos  \tsize\tfood\tshields\n",
    stderr: "",
  },
  "regex_A_bare": {
    args: ["--grep", "\\A", "--regex"],
    code: 0,
    stdout: "header:1: # rev 7 turn 3\nheader:2: game      game_12345678901234567890 · seat 1 AgentPlace1 · controller codex-test\nheader:3: objective Maximize final Freeciv civilization score.\nheader:4: budget    turn 3 of 5000 · 4997 remaining\nheader:5: phase     awaiting_agent · turn 3 phase 0 · active yes\nunits:1: # rev 7 turn 3\nunits:2: # units 4/4 complete\nunits:3: alias\tunit    \twho\tpos  \tmoves\thp   \tactivity\torders\nunits:4: u1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\nunits:5: u2   \tWorkers \town\t30,71\t3/3  \t10/10\tidle    \t-\nunits:6: u3   \tGroße Garde\town\t29,70\t1/1  \t20/20\tidle    \t-\nunits:7: u4   \tStraßburg Wache\town\t28,69\t1/1  \t20/20\tidle    \t-\ncities:1: # rev 7 turn 3\ncities:2: # cities 3/3 complete\ncities:3: alias\tcity  \tpos  \tsize\tfood\tshields\ncities:4: c1   \tLondon\t31,72\t1   \t2/2 \t1\ncities:5: c2   \tΟΔΟΣ\t30,70\t2   \t3/3 \t2\ncities:6: c3   \tEﬀort\t29,69\t3   \t4/4 \t3\ndelta:1: # rev 7 turn 3\ndelta:2: since rev 6 turn 3 · last update: state\ndelta:3: \ndelta:4: ## units\ndelta:5: - u1 moved to 31,72\n",
    stderr: "",
  },
  "regex_Z_anchor": {
    args: ["--grep", "complete\\Z", "--regex"],
    code: 0,
    stdout: "units:2: # units 4/4 complete\ncities:2: # cities 3/3 complete\n",
    stderr: "",
  },
  "regex_ascii_flag": {
    args: ["--grep", "(?a:\\w)+", "--regex"],
    code: 0,
    stdout: "header:1: # rev 7 turn 3\nheader:2: game      game_12345678901234567890 · seat 1 AgentPlace1 · controller codex-test\nheader:3: objective Maximize final Freeciv civilization score.\nheader:4: budget    turn 3 of 5000 · 4997 remaining\nheader:5: phase     awaiting_agent · turn 3 phase 0 · active yes\nunits:1: # rev 7 turn 3\nunits:2: # units 4/4 complete\nunits:3: alias\tunit    \twho\tpos  \tmoves\thp   \tactivity\torders\nunits:4: u1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\nunits:5: u2   \tWorkers \town\t30,71\t3/3  \t10/10\tidle    \t-\nunits:6: u3   \tGroße Garde\town\t29,70\t1/1  \t20/20\tidle    \t-\nunits:7: u4   \tStraßburg Wache\town\t28,69\t1/1  \t20/20\tidle    \t-\ncities:1: # rev 7 turn 3\ncities:2: # cities 3/3 complete\ncities:3: alias\tcity  \tpos  \tsize\tfood\tshields\ncities:4: c1   \tLondon\t31,72\t1   \t2/2 \t1\ncities:5: c2   \tΟΔΟΣ\t30,70\t2   \t3/3 \t2\ncities:6: c3   \tEﬀort\t29,69\t3   \t4/4 \t3\ndelta:1: # rev 7 turn 3\ndelta:2: since rev 6 turn 3 · last update: state\ndelta:4: ## units\ndelta:5: - u1 moved to 31,72\n",
    stderr: "",
  },
  "regex_atomic": {
    args: ["--grep", "(?>Sett)lers", "--regex"],
    code: 0,
    stdout: "units:4: u1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\n",
    stderr: "",
  },
  "regex_backref_ok": {
    args: ["--grep", "(l)\\1", "--regex"],
    code: 0,
    stdout: "header:2: game      game_12345678901234567890 · seat 1 AgentPlace1 · controller codex-test\n",
    stderr: "",
  },
  "regex_bad_Q": {
    args: ["--grep", "\\Q", "--regex"],
    code: 2,
    stdout: "",
    stderr: "error: just show --grep pattern is invalid: bad escape \\Q at position 0; drop --regex to search for the literal text `\\Q`\n",
  },
  "regex_bad_control": {
    args: ["--grep", "\\cA", "--regex"],
    code: 2,
    stdout: "",
    stderr: "error: just show --grep pattern is invalid: bad escape \\c at position 0; drop --regex to search for the literal text `\\cA`\n",
  },
  "regex_bad_group8": {
    args: ["--grep", "\\8", "--regex"],
    code: 2,
    stdout: "",
    stderr: "error: just show --grep pattern is invalid: invalid group reference 8 at position 1; drop --regex to search for the literal text `\\8`\n",
  },
  "regex_bad_h": {
    args: ["--grep", "\\h", "--regex"],
    code: 2,
    stdout: "",
    stderr: "error: just show --grep pattern is invalid: bad escape \\h at position 0; drop --regex to search for the literal text `\\h`\n",
  },
  "regex_bad_range": {
    args: ["--grep", "[a-\\d]", "--regex"],
    code: 2,
    stdout: "",
    stderr: "error: just show --grep pattern is invalid: bad character range a-\\d at position 1; drop --regex to search for the literal text `[a-\\d]`\n",
  },
  "regex_boundary": {
    args: ["--grep", "\\bGro\\w+e\\b", "--regex"],
    code: 0,
    stdout: "units:6: u3   \tGroße Garde\town\t29,70\t1/1  \t20/20\tidle    \t-\n",
    stderr: "",
  },
  "regex_brace_hex": {
    args: ["--grep", "\\x{41}", "--regex"],
    code: 2,
    stdout: "",
    stderr: "error: just show --grep pattern is invalid: incomplete escape \\x at position 0; drop --regex to search for the literal text `\\x{41}`\n",
  },
  "regex_comment": {
    args: ["--grep", "(?#note)alias", "--regex"],
    code: 0,
    stdout: "units:3: alias\tunit    \twho\tpos  \tmoves\thp   \tactivity\torders\ncities:3: alias\tcity  \tpos  \tsize\tfood\tshields\n",
    stderr: "",
  },
  "regex_digit_unicode": {
    args: ["--grep", "\\d", "--regex"],
    code: 0,
    stdout: "header:1: # rev 7 turn 3\nheader:2: game      game_12345678901234567890 · seat 1 AgentPlace1 · controller codex-test\nheader:4: budget    turn 3 of 5000 · 4997 remaining\nheader:5: phase     awaiting_agent · turn 3 phase 0 · active yes\nunits:1: # rev 7 turn 3\nunits:2: # units 4/4 complete\nunits:4: u1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\nunits:5: u2   \tWorkers \town\t30,71\t3/3  \t10/10\tidle    \t-\nunits:6: u3   \tGroße Garde\town\t29,70\t1/1  \t20/20\tidle    \t-\nunits:7: u4   \tStraßburg Wache\town\t28,69\t1/1  \t20/20\tidle    \t-\ncities:1: # rev 7 turn 3\ncities:2: # cities 3/3 complete\ncities:4: c1   \tLondon\t31,72\t1   \t2/2 \t1\ncities:5: c2   \tΟΔΟΣ\t30,70\t2   \t3/3 \t2\ncities:6: c3   \tEﬀort\t29,69\t3   \t4/4 \t3\ndelta:1: # rev 7 turn 3\ndelta:2: since rev 6 turn 3 · last update: state\ndelta:5: - u1 moved to 31,72\n",
    stderr: "",
  },
  "regex_dot_anchor": {
    args: ["--grep", ".\\Z", "--regex"],
    code: 0,
    stdout: "header:1: # rev 7 turn 3\nheader:2: game      game_12345678901234567890 · seat 1 AgentPlace1 · controller codex-test\nheader:3: objective Maximize final Freeciv civilization score.\nheader:4: budget    turn 3 of 5000 · 4997 remaining\nheader:5: phase     awaiting_agent · turn 3 phase 0 · active yes\nunits:1: # rev 7 turn 3\nunits:2: # units 4/4 complete\nunits:3: alias\tunit    \twho\tpos  \tmoves\thp   \tactivity\torders\nunits:4: u1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\nunits:5: u2   \tWorkers \town\t30,71\t3/3  \t10/10\tidle    \t-\nunits:6: u3   \tGroße Garde\town\t29,70\t1/1  \t20/20\tidle    \t-\nunits:7: u4   \tStraßburg Wache\town\t28,69\t1/1  \t20/20\tidle    \t-\ncities:1: # rev 7 turn 3\ncities:2: # cities 3/3 complete\ncities:3: alias\tcity  \tpos  \tsize\tfood\tshields\ncities:4: c1   \tLondon\t31,72\t1   \t2/2 \t1\ncities:5: c2   \tΟΔΟΣ\t30,70\t2   \t3/3 \t2\ncities:6: c3   \tEﬀort\t29,69\t3   \t4/4 \t3\ndelta:1: # rev 7 turn 3\ndelta:2: since rev 6 turn 3 · last update: state\ndelta:4: ## units\ndelta:5: - u1 moved to 31,72\n",
    stderr: "",
  },
  "regex_inline_i": {
    args: ["--grep", "(?i)settlers", "--regex"],
    code: 0,
    stdout: "units:4: u1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\n",
    stderr: "",
  },
  "regex_inline_m": {
    args: ["--grep", "(?m)^alias", "--regex"],
    code: 0,
    stdout: "units:3: alias\tunit    \twho\tpos  \tmoves\thp   \tactivity\torders\ncities:3: alias\tcity  \tpos  \tsize\tfood\tshields\n",
    stderr: "",
  },
  "regex_inline_s": {
    args: ["--grep", "(?s)Sett.", "--regex"],
    code: 0,
    stdout: "units:4: u1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\n",
    stderr: "",
  },
  "regex_js_named": {
    args: ["--grep", "(?<name>u1)", "--regex"],
    code: 2,
    stdout: "",
    stderr: "error: just show --grep pattern is invalid: unknown extension ?<n at position 1; drop --regex to search for the literal text `(?<name>u1)`\n",
  },
  "regex_locale_flag": {
    args: ["--grep", "(?L)a", "--regex"],
    code: 2,
    stdout: "",
    stderr: "error: just show --grep pattern is invalid: bad inline flags: cannot use 'L' flag with a str pattern at position 3; drop --regex to search for the literal text `(?L)a`\n",
  },
  "regex_lookbehind_var": {
    args: ["--grep", "(?<=a|bb)c", "--regex"],
    code: 2,
    stdout: "",
    stderr: "error: just show --grep pattern is invalid: look-behind requires fixed-width pattern; drop --regex to search for the literal text `(?<=a|bb)c`\n",
  },
  "regex_min_max": {
    args: ["--grep", "a{2,1}", "--regex"],
    code: 2,
    stdout: "",
    stderr: "error: just show --grep pattern is invalid: min repeat greater than max repeat at position 2; drop --regex to search for the literal text `a{2,1}`\n",
  },
  "regex_named_backref": {
    args: ["--grep", "(?P<x>Sett)(?P=x)", "--regex"],
    code: 0,
    stdout: "no mirror line matches '(?P<x>Sett)(?P=x)'\n",
    stderr: "",
  },
  "regex_named_char": {
    args: ["--grep", "\\N{EM DASH}", "--regex"],
    code: 0,
    stdout: "no mirror line matches '\\\\N{EM DASH}'\n",
    stderr: "",
  },
  "regex_named_group": {
    args: ["--grep", "(?P<x>Sett)", "--regex"],
    code: 0,
    stdout: "units:4: u1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\n",
    stderr: "",
  },
  "regex_not_space": {
    args: ["--grep", "\\S\\S\\S", "--regex"],
    code: 0,
    stdout: "header:1: # rev 7 turn 3\nheader:2: game      game_12345678901234567890 · seat 1 AgentPlace1 · controller codex-test\nheader:3: objective Maximize final Freeciv civilization score.\nheader:4: budget    turn 3 of 5000 · 4997 remaining\nheader:5: phase     awaiting_agent · turn 3 phase 0 · active yes\nunits:1: # rev 7 turn 3\nunits:2: # units 4/4 complete\nunits:3: alias\tunit    \twho\tpos  \tmoves\thp   \tactivity\torders\nunits:4: u1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\nunits:5: u2   \tWorkers \town\t30,71\t3/3  \t10/10\tidle    \t-\nunits:6: u3   \tGroße Garde\town\t29,70\t1/1  \t20/20\tidle    \t-\nunits:7: u4   \tStraßburg Wache\town\t28,69\t1/1  \t20/20\tidle    \t-\ncities:1: # rev 7 turn 3\ncities:2: # cities 3/3 complete\ncities:3: alias\tcity  \tpos  \tsize\tfood\tshields\ncities:4: c1   \tLondon\t31,72\t1   \t2/2 \t1\ncities:5: c2   \tΟΔΟΣ\t30,70\t2   \t3/3 \t2\ncities:6: c3   \tEﬀort\t29,69\t3   \t4/4 \t3\ndelta:1: # rev 7 turn 3\ndelta:2: since rev 6 turn 3 · last update: state\ndelta:4: ## units\ndelta:5: - u1 moved to 31,72\n",
    stderr: "",
  },
  "regex_not_word": {
    args: ["--grep", "[^\\w]\\W", "--regex"],
    code: 0,
    stdout: "header:1: # rev 7 turn 3\nheader:2: game      game_12345678901234567890 · seat 1 AgentPlace1 · controller codex-test\nheader:4: budget    turn 3 of 5000 · 4997 remaining\nheader:5: phase     awaiting_agent · turn 3 phase 0 · active yes\nunits:1: # rev 7 turn 3\nunits:2: # units 4/4 complete\nunits:3: alias\tunit    \twho\tpos  \tmoves\thp   \tactivity\torders\nunits:4: u1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\nunits:5: u2   \tWorkers \town\t30,71\t3/3  \t10/10\tidle    \t-\nunits:6: u3   \tGroße Garde\town\t29,70\t1/1  \t20/20\tidle    \t-\nunits:7: u4   \tStraßburg Wache\town\t28,69\t1/1  \t20/20\tidle    \t-\ncities:1: # rev 7 turn 3\ncities:2: # cities 3/3 complete\ncities:3: alias\tcity  \tpos  \tsize\tfood\tshields\ncities:4: c1   \tLondon\t31,72\t1   \t2/2 \t1\ncities:5: c2   \tΟΔΟΣ\t30,70\t2   \t3/3 \t2\ncities:6: c3   \tEﬀort\t29,69\t3   \t4/4 \t3\ndelta:1: # rev 7 turn 3\ndelta:2: since rev 6 turn 3 · last update: state\ndelta:4: ## units\ndelta:5: - u1 moved to 31,72\n",
    stderr: "",
  },
  "regex_open_brace_lo": {
    args: ["--grep", "A{,2}lias", "--regex"],
    code: 0,
    stdout: "units:3: alias\tunit    \twho\tpos  \tmoves\thp   \tactivity\torders\ncities:3: alias\tcity  \tpos  \tsize\tfood\tshields\n",
    stderr: "",
  },
  "regex_possessive": {
    args: ["--grep", "Sett++lers", "--regex"],
    code: 0,
    stdout: "units:4: u1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\n",
    stderr: "",
  },
  "regex_property": {
    args: ["--grep", "\\p{L}", "--regex"],
    code: 2,
    stdout: "",
    stderr: "error: just show --grep pattern is invalid: bad escape \\p at position 0; drop --regex to search for the literal text `\\p{L}`\n",
  },
  "regex_scoped_i": {
    args: ["--grep", "(?i:SETT)", "--regex"],
    code: 0,
    stdout: "units:4: u1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\n",
    stderr: "",
  },
  "regex_scoped_no_i": {
    args: ["--grep", "(?-i:Sett)", "--regex"],
    code: 0,
    stdout: "units:4: u1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\n",
    stderr: "",
  },
  "regex_scoped_no_i_miss": {
    args: ["--grep", "(?-i:sett)", "--regex"],
    code: 0,
    stdout: "no mirror line matches '(?-i:sett)'\n",
    stderr: "",
  },
  "regex_unbalanced": {
    args: ["--grep", "(unbalanced", "--regex"],
    code: 2,
    stdout: "",
    stderr: "error: just show --grep pattern is invalid: missing ), unterminated subpattern at position 0; drop --regex to search for the literal text `(unbalanced`\n",
  },
  "regex_undefined_name": {
    args: ["--grep", "\\N{DASH}", "--regex"],
    code: 2,
    stdout: "",
    stderr: "error: just show --grep pattern is invalid: undefined character name 'DASH' at position 0; drop --regex to search for the literal text `\\N{DASH}`\n",
  },
  "regex_word": {
    args: ["--grep", "\\w+\\t\\w+", "--regex"],
    code: 0,
    stdout: "units:3: alias\tunit    \twho\tpos  \tmoves\thp   \tactivity\torders\nunits:4: u1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\nunits:5: u2   \tWorkers \town\t30,71\t3/3  \t10/10\tidle    \t-\nunits:6: u3   \tGroße Garde\town\t29,70\t1/1  \t20/20\tidle    \t-\nunits:7: u4   \tStraßburg Wache\town\t28,69\t1/1  \t20/20\tidle    \t-\ncities:3: alias\tcity  \tpos  \tsize\tfood\tshields\ncities:4: c1   \tLondon\t31,72\t1   \t2/2 \t1\ncities:5: c2   \tΟΔΟΣ\t30,70\t2   \t3/3 \t2\ncities:6: c3   \tEﬀort\t29,69\t3   \t4/4 \t3\n",
    stderr: "",
  },
  "regex_z_anchor": {
    args: ["--grep", "complete\\z", "--regex"],
    code: 0,
    stdout: "units:2: # units 4/4 complete\ncities:2: # cities 3/3 complete\n",
    stderr: "",
  },
};

// ---------------------------------------------------------------------------
// The fixture seat — the same bytes CPython was run against
// ---------------------------------------------------------------------------

const scratches: Scratch[] = [];
afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

const clientState = {
  schema_version: 5,
  game_id: FIXTURE_GAME_ID,
  agent_id: FIXTURE_AGENT_ID,
  last_revision: { revision: 7, state_token: 'token_3_7', turn: 3 },
  actions: {},
  pending_catalogs: {},
  batches: {},
  receipts: {},
  action_aliases: { state_revision: null, by_alias: {} },
  entity_aliases: {},
  tile_aliases: {},
  drained_actors: [],
};

interface Seat {
  readonly sessionPath: string;
  readonly layer: Layer.Layer<SessionStore | PrivateFs>;
}

const seat = (): Seat => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const home = path.join(scratch.workspace.stateRoot, FIXTURE_GAME_ID);
  fs.mkdirSync(home, { mode: 0o700, recursive: true });
  const sessionPath = path.join(home, 'codex-test.json');
  fs.writeFileSync(sessionPath, `${JSON.stringify(sessionFile(), null, 2)}\n`, { mode: 0o600 });
  fs.writeFileSync(path.join(home, 'codex-test.v2-state'), `${JSON.stringify(clientState, null, 2)}\n`, {
    mode: 0o600,
  });
  const mirror = path.join(home, 'codex-test');
  for (const [relative, text] of MIRROR) {
    const target = path.join(mirror, relative);
    fs.mkdirSync(path.dirname(target), { mode: 0o700, recursive: true });
    fs.writeFileSync(target, text, { mode: 0o600 });
  }
  const store = sessionStoreFor(scratch.workspace, scratch.files, v2StateSchema, {});
  return {
    sessionPath,
    layer: Layer.merge(scratch.layer, Layer.succeed(SessionStore, store)),
  };
};

/** `show` with the argv CPython was given, back as the three streams it wrote. */
const show = async (args: ReadonlyArray<string>): Promise<Omit<Golden, 'args'>> => {
  const fixture = seat();
  const options = { session: fixture.sessionPath, name: '', grep: '', regex: false, yields: false, json: false };
  for (let index = 0; index < args.length; index += 1) {
    const argument = args[index];
    if (argument === '--grep') options.grep = args[index + 1] ?? '';
    else if (argument === '--regex') options.regex = true;
    else if (argument === '--json') options.json = true;
    else if (argument !== options.grep) options.name = argument ?? '';
  }
  const written: string[] = [];
  const original = console.log;
  console.log = (...parts: ReadonlyArray<unknown>) => written.push(parts.map(String).join(' '));
  try {
    const result = await Effect.runPromise(
      Effect.either(Effect.provide(runShow(options), fixture.layer))
    );
    return {
      code: Either.isLeft(result) ? 2 : 0,
      stdout: written.length > 0 ? `${written.join('\n')}\n` : '',
      stderr: Either.isLeft(result) ? `error: ${result.left.message}\n` : '',
    };
  } finally {
    console.log = original;
  }
};

describe('show --grep against CPython', () => {
  for (const [name, golden] of Object.entries(GOLDEN)) {
    test(`${name}: ${golden.args.join(' ')}`, async () => {
      expect(await show(golden.args)).toEqual({
        code: golden.code,
        stdout: golden.stdout,
        stderr: golden.stderr,
      });
    });
  }
});

// ---------------------------------------------------------------------------
// casefold — the table, not an approximation of it
// ---------------------------------------------------------------------------

describe('casefold is str.casefold', () => {
  test('the full folds expand rather than lower', () => {
    // Captured from `python3 -c "print(...casefold())"`.
    expect(casefold('Große STRASSE ﬀ ﬃ ſ ΑΣ ΣΣ ς µ İ ı Ꭰ ẞ')).toBe(
      'grosse strasse ff ffi s ασ σσ σ μ i̇ ı Ꭰ ss'
    );
  });

  test('toLowerCase would have answered differently on every one of them', () => {
    for (const sample of ['Große', 'ﬀ', 'ſ', 'ΑΣ', 'ς', 'µ', 'Ꭰ', 'ẞ']) {
      expect(casefold(sample)).not.toBe(sample.toLowerCase());
    }
  });

  test('a fold is idempotent, so the needle and the haystack meet', () => {
    for (const sample of ['Große', 'STRASSE', 'Eﬀort', 'ΟΔΟΣ', 'οδος']) {
      expect(casefold(casefold(sample))).toBe(casefold(sample));
    }
  });
});

// ---------------------------------------------------------------------------
// The two constructs the port refuses rather than answers
// ---------------------------------------------------------------------------

describe('the refusals this build adds', () => {
  test('a conditional group reference is refused, not guessed', () => {
    const result = compilePythonRegex('(a)(?(1)b|c)', PY_IGNORECASE);
    expect(Either.isLeft(result)).toBe(true);
    if (Either.isLeft(result)) {
      expect(result.left.kind).toBe('unsupported');
      expect(result.left.message).toBe(
        'a conditional group reference has no equivalent in this engine'
      );
    }
  });

  test('a backreference to a group that may not have taken part is refused', () => {
    // JavaScript matches the empty string there and CPython fails, so emitting
    // it would answer `(a)?\1` with every line in the mirror.
    const result = compilePythonRegex('(a)?\\1', PY_IGNORECASE);
    expect(Either.isLeft(result)).toBe(true);
    if (Either.isLeft(result)) {
      expect(result.left.kind).toBe('unsupported');
    }
  });

  /**
   * The emission is explicit code point ranges, so one `\w` costs ~10 KB of
   * regex source and one `\b` ~50 KB.  A legal 200-character pattern can
   * therefore emit past JavaScriptCore's ~1 MB ceiling, and `new RegExp` says
   * so by *throwing* — the one place in this module a host call can raise
   * something that is not a `ParseSignal`.  CPython compiles all of these and
   * answers at exit 0, so it is a divergence either way (NOTES.md §19.3,
   * residue 4); what must never happen is the throw escaping as a defect,
   * which `cli-main` would print as a JavaScript stack trace.
   */
  test('a pattern whose emitted source is too large is a value, never a throw', () => {
    for (const pattern of ['\\b'.repeat(21), '\\w'.repeat(100), '\\w'.repeat(84)]) {
      const result = compilePythonRegex(pattern, PY_IGNORECASE);
      expect(Either.isLeft(result)).toBe(true);
      if (Either.isLeft(result)) {
        expect(result.left.kind).toBe('unsupported');
        expect(result.left.message).toBe('the emitted pattern is too large for this engine');
      }
    }
  });

  test('the patterns just under the ceiling still compile and still match', () => {
    // 20 `\b`s and 83 `\w`s are the largest that fit on this engine; both are
    // answers, not refusals, so the refusal above is a ceiling and not a class.
    const boundaries = ['\\b'.repeat(20), '\\w'.repeat(83)] as const;
    for (const pattern of boundaries) {
      const result = compilePythonRegex(pattern, PY_IGNORECASE);
      expect(Either.isRight(result)).toBe(true);
    }
    const word = compilePythonRegex('\\b'.repeat(20), PY_IGNORECASE);
    if (Either.isRight(word)) expect(word.right.test('Settlers')).toBe(true);
  });

  test('a backreference whose group is certain compiles and matches CPython', () => {
    const result = compilePythonRegex('(l)\\1', PY_IGNORECASE);
    expect(Either.isRight(result)).toBe(true);
    if (Either.isRight(result)) {
      expect(result.right.test('Hello')).toBe(true);
      expect(result.right.test('Helo')).toBe(false);
    }
  });
});

// ---------------------------------------------------------------------------
// `(?a:…)` and `(?u:…)` replace the enclosing flag, they do not join it
// ---------------------------------------------------------------------------

describe('a scoped character-set flag replaces the enclosing one', () => {
  // Captured from `re.search(p, s, re.IGNORECASE)` for each pair.  `ﬀ` is the
  // discriminator: it is a word character to `re`'s Unicode tables and not to
  // its ASCII ones.  Joining the flags instead of replacing them leaves both
  // bits set, and then the answer depends on which one the reader tests first.
  const SUBJECTS = ['Große Straße', 'settlers', 'ﬀ ﬁ ﬂ', 'x𝕏', '123'] as const;
  const EXPECTED: ReadonlyArray<readonly [string, ReadonlyArray<boolean>]> = [
    ['(?a)(?u:\\w)', [true, true, false, true, true]],
    ['(?u)(?a:\\w)', [true, true, false, true, true]],
    ['(?a)\\w', [true, true, false, true, true]],
    ['(?a:\\w)', [true, true, false, true, true]],
    ['(?u:\\w)', [true, true, true, true, true]],
  ];

  for (const [pattern, wanted] of EXPECTED) {
    test(pattern, () => {
      const result = compilePythonRegex(pattern, PY_IGNORECASE);
      expect(Either.isRight(result)).toBe(true);
      if (Either.isRight(result)) {
        expect(SUBJECTS.map((subject) => result.right.test(subject))).toEqual([...wanted]);
      }
    });
  }
});

// ---------------------------------------------------------------------------
// The engine bug the emission is shaped around
// ---------------------------------------------------------------------------

describe('astral text answers the same as CPython', () => {
  // `/.$/u` — the natural emission — is `false` for `x𝕏` under JavaScriptCore
  // and `true` under CPython, so the compiler emits surrogate pairs instead.
  test('a dot and an end anchor find the last character of an astral line', () => {
    const result = compilePythonRegex('.\\Z', PY_IGNORECASE);
    expect(Either.isRight(result)).toBe(true);
    if (Either.isRight(result)) {
      expect(result.right.test('x𝕏')).toBe(true);
      expect(result.right.test('🙂 astral 𝕏')).toBe(true);
      expect(result.right.test('')).toBe(false);
    }
  });

  test('a search never begins between the halves of a surrogate pair', () => {
    const result = compilePythonRegex('\\B', PY_IGNORECASE);
    expect(Either.isRight(result)).toBe(true);
    // CPython: `re.search(r'\B', '𝕏')` is None — both ends of the one code
    // point are boundaries.  A UTF-16 search would find the gap inside it.
    if (Either.isRight(result)) expect(result.right.test('𝕏')).toBe(false);
  });

  test('the 200-character cap counts code points, as `len(pattern)` does', () => {
    // Both bounds are golden rows above: 200 emoji search and 201 refuse.
    // `pattern.length` would have made the first one 400 and refused it.
    expect(GOLDEN['grep_astral_200']?.code).toBe(0);
    expect(GOLDEN['grep_astral_201']?.code).toBe(2);
  });
});

/**
 * A **leading** `\b` is the single most common regex an agent writes, and it is
 * the worst case for the emitter: `\b` expands to lookarounds over CPython's
 * whole word class, so with the pattern starting there the assertion is
 * evaluated at every offset of every line rather than only where a preceding
 * atom already matched.  Spelled as one lookaround over the full 50 KB
 * alternation it cost 2.2 s on a real workspace against
 * `V2_SHOW_GREP_BUDGET_S = 2.0` — an exit 2 on a pattern CPython answers in
 * 0.15 s — so the astral half is guarded by a surrogate-half class test that
 * rejects an ordinary character in one step.
 *
 * The guard must not change a single answer, which is what these assert; the
 * end-to-end timing is `test/diff-offline.sh`'s four `\b` cases.
 */
describe('a leading word boundary answers exactly what CPython answers', () => {
  const search = (pattern: string, subject: string): readonly [number, string] | null => {
    const compiled = compilePythonRegex(pattern, PY_IGNORECASE);
    expect(Either.isRight(compiled)).toBe(true);
    if (Either.isLeft(compiled)) throw new Error(compiled.left.message);
    const match = compiled.right.exec(subject);
    return match === null ? null : [Array.from(subject.slice(0, match.index)).length, match[0]];
  };

  test('`\\bu1\\b` is the whole-word search it looks like', () => {
    expect(search('\\bu1\\b', 'u1 Settlers')).toEqual([0, 'u1']);
    expect(search('\\bu1\\b', ' u1 ')).toEqual([1, 'u1']);
    expect(search('\\bu1\\b', 'xu1x')).toBeNull();
    expect(search('\\bu1\\b', 'u10')).toBeNull();
  });

  test('the guard does not hide an astral word character', () => {
    // U+1D54F is a letter, so it is a word character on both sides of the
    // boundary: `\b𝕏\b` matches it alone and not when a letter abuts it.
    expect(search('\\b𝕏\\b', '𝕏')).toEqual([0, '𝕏']);
    expect(search('\\b𝕏\\b', ' 𝕏 ')).toEqual([1, '𝕏']);
    expect(search('\\b𝕏\\b', 'a𝕏b')).toBeNull();
    expect(search('\\b𝕏\\b', '𝕏𝕏')).toBeNull();
    // U+1F680 is a symbol, so it is *not* a word character and the boundary
    // sits between it and the letter beside it.
    expect(search('\\bb\\b', 'a🚀b')).toEqual([2, 'b']);
  });

  test('`\\B` is still the exact complement, leading or not', () => {
    expect(search('\\Bx', 'xu1x')).toEqual([3, 'x']);
    expect(search('\\Bx', 'x y')).toBeNull();
    expect(search('𝕏\\B', 'a𝕏b')).toEqual([1, '𝕏']);
    expect(search('𝕏\\B', ' 𝕏 ')).toBeNull();
  });

  test('(?a) narrows the class on both sides of the boundary', () => {
    // Under ASCII rules `é` is not a word character, so `\bé\b` never matches
    // and the boundary falls between `a` and `é`.
    expect(search('(?a)\\ba\\b', 'aé')).toEqual([0, 'a']);
    expect(search('\\ba\\b', 'aé')).toBeNull();
  });
});
