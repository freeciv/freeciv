/**
 * `just show` — the offline read of the mirror.
 *
 * Ports the `show` half of `test_v2_responses_are_mirrored_and_show_never_opens_a_socket`,
 * `test_v2_show_leads_with_a_staleness_banner_when_it_is_behind` and
 * `test_v2_show_in_the_lobby_names_a_command_that_works_there` from
 * `play/tests/test_client.py`.
 *
 * Every `GOLDEN` entry below is the **verbatim** stdout (or `error: …` stderr)
 * of `python3 client.py show …` run against the byte-identical mirror in
 * `MIRROR`, captured from the CPython original.  The whole unit is byte-diff
 * oracle, so the assertions compare whole streams rather than substrings.
 *
 * `show` is proved to open no socket by construction: `runShow` requires
 * `SessionStore | PrivateFs` and nothing else, so a request would not typecheck,
 * and the suite provides no `Http`/`V2Client` layer for one to reach.
 */
import * as fs from 'node:fs';
import * as path from 'node:path';
import { afterEach, describe, expect, test } from 'bun:test';
import { Effect, Either, Layer } from 'effect';
import { runShow, type ShowOptions } from 'src/commands/show.cmd';
import { pyRepr, showOptionFiles, showSources, V2_SHOW_MAX_MATCHES } from 'src/render/show';
import { v2StateSchema } from 'src/services/aliases';
import { PrivateFs } from 'src/services/private-fs';
import { SessionStore, sessionStoreFor } from 'src/services/session-store';
import { FIXTURE_AGENT_ID, FIXTURE_GAME_ID, scratchWorkspace, sessionFile, type Scratch } from 'test/_fixtures';

// ---------------------------------------------------------------------------
// The fixture mirror — the exact bytes CPython was run against
// ---------------------------------------------------------------------------

const MIRROR: ReadonlyArray<readonly [string, string]> = [
  ["state/header.txt", "# rev 7 turn 3\ngame      game_12345678901234567890 · seat 1 AgentPlace1 · controller codex-test\nobjective Maximize final Freeciv civilization score.\nbudget    turn 3 of 5000 · 4997 remaining\nphase     awaiting_agent · turn 3 phase 0 · active yes\n"],
  ["state/phase.json", "{\n  \"active\": true,\n  \"phase\": 0,\n  \"schema_version\": 1,\n  \"state\": \"awaiting_agent\",\n  \"turn\": 3\n}\n"],
  ["state/overview.tsv", "# rev 7 turn 3\n# overview 3/3 complete\nkey     \tvalue\nmap     \t64x64\ngold    \t25\nresearch\tBronze Working\n"],
  ["state/units.tsv", "# rev 7 turn 3\n# units 2/2 complete\nalias\tunit    \twho\tpos  \tmoves\thp   \tactivity\torders\nu1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\nu2   \tWorkers \town\t30,71\t3/3  \t10/10\tidle    \t-\n"],
  ["state/cities.tsv", "# rev 7 turn 3\n# cities 1/1 complete\nalias\tcity  \tpos  \tsize\tfood\tshields\nc1   \tLondon\t31,72\t1   \t2/2 \t1\n"],
  ["state/map.txt", "# rev 7 turn 3\n# map 64x64 · 12 of 12 tiles known\n# window x 30..33 y 71..73\n# legend '?'=never seen · UPPERCASE=visible now · lowercase=remembered\n# terrain F=Forest · G=Grassland · H=Hills · O=Ocean\n   71 |GGFH\n   72 |GGgh\n   73 |OOOO\n"],
  ["state/yields.tsv", "# rev 7 turn 3\n# yields 3/3 complete\ntile    \tfood\tshields\ttrade\tworked\tcity\nT(30,71)\t3   \t0      \t1    \tno    \t-\nT(31,72)\t2   \t1      \t0    \tyes   \tc1\nT(32,72)\t1   \t2      \t1    \tno    \t-\n"],
  ["state/diplomacy.tsv", "# rev 7 turn 3\n# diplomacy 1/1 complete\nalias\tcounterpart\tstate\tmeeting\nr1   \tRomans     \twar  \t-\n"],
  ["state/delta.md", "# rev 7 turn 3\nsince rev 6 turn 3 · last update: state\n\n## units\n- u1 moved to 31,72\n"],
  ["cache/nations.tsv", "# rev 7 turn 3\n# nations 2/2 complete\nid                                     \tnation  \tdefault_style_id\nnation_0cc67771419649722f5e79cac23dd5c6\tAmerican\tstyle_5f71e8712eb4fd2349ed14ee3fea1a9b\nnation_1dd67771419649722f5e79cac23dd5c6\tRoman   \tstyle_6f71e8712eb4fd2349ed14ee3fea1a9b\n"],
  ["cache/styles.tsv", "# rev 7 turn 3\n# styles 1/1 complete\nid                                    \tstyle\nstyle_5f71e8712eb4fd2349ed14ee3fea1a9b\tClassical\n"],
  ["cache/governments.tsv", "# rev 7 turn 3\n# governments 1/1 complete\nid                                  \tgovernment\ngovernment_5f71e8712eb4fd2349ed14ee\tDespotism\n"],
  ["state/options/u1.txt", "# rev 7 turn 3\n# actor unit u1\n# actions 2/2 complete\nalias\tkind             \ttarget\targs\tlabel                 \tflags\na1   \tunit.found_city  \t31,72 \t{name}\tFound a city here   \t-\na2   \tunit.order       \t31,71 \t-   \tMove unit to (31, 71) \tvariant=standard\n"],
  ["state/options/unscoped.txt", "# rev 7 turn 3\n# unscoped catalog (union of the kinds fetched at this rev)\n# actions 1/1 complete\nalias\tkind      \ttarget\targs\tlabel        \tflags\na3   \tphase.end \t-     \t-   \tEnd the phase\t-\n"],
];

const GOLDEN = {
  "current/alias_c1": {"code": 0, "stdout": "cities: c1   \tLondon\t31,72\t1   \t2/2 \t1\n", "stderr": ""},
  "current/alias_r1": {"code": 0, "stdout": "diplomacy: r1   \tRomans     \twar  \t-\n", "stderr": ""},
  "current/alias_u1": {"code": 0, "stdout": "units: u1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\n\n# rev 7 turn 3\n# actor unit u1\n# actions 2/2 complete\nalias\tkind             \ttarget\targs\tlabel                 \tflags\na1   \tunit.found_city  \t31,72 \t{name}\tFound a city here   \t-\na2   \tunit.order       \t31,71 \t-   \tMove unit to (31, 71) \tvariant=standard\n", "stderr": ""},
  "current/alias_u1_json": {"code": 0, "stdout": "{\"command\":\"show\",\"lines\":[\"units: u1   \\tSettlers\\town\\t31,72\\t1/1  \\t20/20\\tidle    \\t-\",\"\",\"# rev 7 turn 3\",\"# actor unit u1\",\"# actions 2/2 complete\",\"alias\\tkind             \\ttarget\\targs\\tlabel                 \\tflags\",\"a1   \\tunit.found_city  \\t31,72 \\t{name}\\tFound a city here   \\t-\",\"a2   \\tunit.order       \\t31,71 \\t-   \\tMove unit to (31, 71) \\tvariant=standard\"],\"schema_version\":1,\"selection\":\"u1\"}\n", "stderr": ""},
  "current/alias_u9": {"code": 2, "stdout": "", "stderr": "error: the mirror holds nothing named u9; run `just legal --actor_id u9 --all` to write state/options/u9.txt, or `just show` to list what is there\n"},
  "current/alias_unscoped": {"code": 0, "stdout": "# rev 7 turn 3\n# unscoped catalog (union of the kinds fetched at this rev)\n# actions 1/1 complete\nalias\tkind      \ttarget\targs\tlabel        \tflags\na3   \tphase.end \t-     \t-   \tEnd the phase\t-\n", "stderr": ""},
  "current/default": {"code": 0, "stdout": "# rev 7 turn 3\ngame      game_12345678901234567890 · seat 1 AgentPlace1 · controller codex-test\nobjective Maximize final Freeciv civilization score.\nbudget    turn 3 of 5000 · 4997 remaining\nphase     awaiting_agent · turn 3 phase 0 · active yes\n\n# rev 7 turn 3\nsince rev 6 turn 3 · last update: state\n\n## units\n- u1 moved to 31,72\n\nfiles: header phase overview units cities map yields diplomacy delta nations styles governments options/u1 options/unscoped\nread one with `just show NAME`, search with `just show --grep PATTERN`\n", "stderr": ""},
  "current/default_json": {"code": 0, "stdout": "{\"command\":\"show\",\"lines\":[\"# rev 7 turn 3\",\"game      game_12345678901234567890 \\u00b7 seat 1 AgentPlace1 \\u00b7 controller codex-test\",\"objective Maximize final Freeciv civilization score.\",\"budget    turn 3 of 5000 \\u00b7 4997 remaining\",\"phase     awaiting_agent \\u00b7 turn 3 phase 0 \\u00b7 active yes\",\"\",\"# rev 7 turn 3\",\"since rev 6 turn 3 \\u00b7 last update: state\",\"\",\"## units\",\"- u1 moved to 31,72\",\"\",\"files: header phase overview units cities map yields diplomacy delta nations styles governments options/u1 options/unscoped\",\"read one with `just show NAME`, search with `just show --grep PATTERN`\"],\"schema_version\":1,\"selection\":null}\n", "stderr": ""},
  "current/grep_astral_nonprintable": {"code": 0, "stdout": "no mirror line matches 'a\\U000e0001b'\n", "stderr": ""},
  "current/grep_bom_and_name": {"code": 2, "stdout": "", "stderr": "error: just show takes either a name or --grep PATTERN, not both\n"},
  "current/grep_bom_only": {"code": 0, "stdout": "no mirror line matches '\\ufeff'\n", "stderr": ""},
  "current/grep_bom_settlers": {"code": 0, "stdout": "no mirror line matches '\\ufeffSettlers'\n", "stderr": ""},
  "current/grep_case": {"code": 0, "stdout": "units:4: u1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\n", "stderr": ""},
  "current/grep_catastrophic": {"code": 0, "stdout": "no mirror line matches '(a|aa)+$'\n", "stderr": ""},
  "current/grep_interior_nonprintable": {"code": 0, "stdout": "no mirror line matches 'Set\\u0378tlers'\n", "stderr": ""},
  "current/grep_literal_dot": {"code": 0, "stdout": "no mirror line matches 'Settl.rs'\n", "stderr": ""},
  "current/grep_long": {"code": 2, "stdout": "", "stderr": "error: just show --grep takes a pattern of at most 200 characters\n"},
  "current/grep_missing": {"code": 0, "stdout": "no mirror line matches 'zzz-not-here'\n", "stderr": ""},
  "current/grep_nbsp_interior": {"code": 0, "stdout": "no mirror line matches 'Set\\xa0tlers'\n", "stderr": ""},
  "current/grep_nbsp_only": {"code": 0, "stdout": "# rev 7 turn 3\ngame      game_12345678901234567890 · seat 1 AgentPlace1 · controller codex-test\nobjective Maximize final Freeciv civilization score.\nbudget    turn 3 of 5000 · 4997 remaining\nphase     awaiting_agent · turn 3 phase 0 · active yes\n\n# rev 7 turn 3\nsince rev 6 turn 3 · last update: state\n\n## units\n- u1 moved to 31,72\n\nfiles: header phase overview units cities map yields diplomacy delta nations styles governments options/u1 options/unscoped\nread one with `just show NAME`, search with `just show --grep PATTERN`\n", "stderr": ""},
  "current/grep_regex_dot": {"code": 0, "stdout": "units:4: u1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\n", "stderr": ""},
  "current/grep_regex_nested": {"code": 2, "stdout": "", "stderr": "error: just show --grep --regex refuses a quantifier applied to an already quantified group; drop --regex to search for the literal text, for example `just show --grep found_city`\n"},
  "current/grep_settlers": {"code": 0, "stdout": "units:4: u1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\n", "stderr": ""},
  "current/grep_settlers_json": {"code": 0, "stdout": "{\"command\":\"show\",\"lines\":[\"units:4: u1   \\tSettlers\\town\\t31,72\\t1/1  \\t20/20\\tidle    \\t-\"],\"schema_version\":1,\"selection\":\"grep Settlers\"}\n", "stderr": ""},
  "current/grep_x85_only": {"code": 0, "stdout": "# rev 7 turn 3\ngame      game_12345678901234567890 · seat 1 AgentPlace1 · controller codex-test\nobjective Maximize final Freeciv civilization score.\nbudget    turn 3 of 5000 · 4997 remaining\nphase     awaiting_agent · turn 3 phase 0 · active yes\n\n# rev 7 turn 3\nsince rev 6 turn 3 · last update: state\n\n## units\n- u1 moved to 31,72\n\nfiles: header phase overview units cities map yields diplomacy delta nations styles governments options/u1 options/unscoped\nread one with `just show NAME`, search with `just show --grep PATTERN`\n", "stderr": ""},
  "current/grep_x85_only_regex": {"code": 2, "stdout": "", "stderr": "error: just show --regex needs a --grep PATTERN to apply to\n"},
  "current/grep_x85_settlers": {"code": 0, "stdout": "units:4: u1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\n", "stderr": ""},
  "current/hostile_abs": {"code": 2, "stdout": "", "stderr": "error: just show takes one mirror file name or one entity alias, for example `just show units` or `just show u1`\n"},
  "current/hostile_dotdot": {"code": 2, "stdout": "", "stderr": "error: just show takes one mirror file name or one entity alias, for example `just show units` or `just show u1`\n"},
  "current/map": {"code": 0, "stdout": "# rev 7 turn 3\n# map 64x64 · 12 of 12 tiles known\n# window x 30..33 y 71..73\n# legend '?'=never seen · UPPERCASE=visible now · lowercase=remembered\n# terrain F=Forest · G=Grassland · H=Hills · O=Ocean\n   71 |GGFH\n   72 |GGgh\n   73 |OOOO\n", "stderr": ""},
  "current/name_and_grep": {"code": 2, "stdout": "", "stderr": "error: just show takes either a name or --grep PATTERN, not both\n"},
  "current/name_bom_lead": {"code": 2, "stdout": "", "stderr": "error: just show takes one mirror file name or one entity alias, for example `just show units` or `just show u1`\n"},
  "current/name_bom_trail": {"code": 2, "stdout": "", "stderr": "error: just show takes one mirror file name or one entity alias, for example `just show units` or `just show u1`\n"},
  "current/name_nbsp_lead": {"code": 0, "stdout": "# rev 7 turn 3\n# units 2/2 complete\nalias\tunit    \twho\tpos  \tmoves\thp   \tactivity\torders\nu1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\nu2   \tWorkers \town\t30,71\t3/3  \t10/10\tidle    \t-\n", "stderr": ""},
  "current/name_space_lead": {"code": 0, "stdout": "# rev 7 turn 3\n# units 2/2 complete\nalias\tunit    \twho\tpos  \tmoves\thp   \tactivity\torders\nu1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\nu2   \tWorkers \town\t30,71\t3/3  \t10/10\tidle    \t-\n", "stderr": ""},
  "current/name_x1c_lead": {"code": 0, "stdout": "# rev 7 turn 3\n# units 2/2 complete\nalias\tunit    \twho\tpos  \tmoves\thp   \tactivity\torders\nu1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\nu2   \tWorkers \town\t30,71\t3/3  \t10/10\tidle    \t-\n", "stderr": ""},
  "current/name_x1f_lead": {"code": 0, "stdout": "# rev 7 turn 3\n# units 2/2 complete\nalias\tunit    \twho\tpos  \tmoves\thp   \tactivity\torders\nu1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\nu2   \tWorkers \town\t30,71\t3/3  \t10/10\tidle    \t-\n", "stderr": ""},
  "current/name_x85_lead": {"code": 0, "stdout": "# rev 7 turn 3\n# units 2/2 complete\nalias\tunit    \twho\tpos  \tmoves\thp   \tactivity\torders\nu1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\nu2   \tWorkers \town\t30,71\t3/3  \t10/10\tidle    \t-\n", "stderr": ""},
  "current/nations": {"code": 0, "stdout": "# rev 7 turn 3\n# nations 2/2 complete\nid                                     \tnation  \tdefault_style_id\nnation_0cc67771419649722f5e79cac23dd5c6\tAmerican\tstyle_5f71e8712eb4fd2349ed14ee3fea1a9b\nnation_1dd67771419649722f5e79cac23dd5c6\tRoman   \tstyle_6f71e8712eb4fd2349ed14ee3fea1a9b\n", "stderr": ""},
  "current/phase": {"code": 0, "stdout": "{\n  \"active\": true,\n  \"phase\": 0,\n  \"schema_version\": 1,\n  \"state\": \"awaiting_agent\",\n  \"turn\": 3\n}\n", "stderr": ""},
  "current/regex_without_grep": {"code": 2, "stdout": "", "stderr": "error: just show --regex needs a --grep PATTERN to apply to\n"},
  "current/units": {"code": 0, "stdout": "# rev 7 turn 3\n# units 2/2 complete\nalias\tunit    \twho\tpos  \tmoves\thp   \tactivity\torders\nu1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\nu2   \tWorkers \town\t30,71\t3/3  \t10/10\tidle    \t-\n", "stderr": ""},
  "current/unknown_name": {"code": 2, "stdout": "", "stderr": "error: bogus is not a mirror file or an entity alias; the file names are cities delta diplomacy governments header map nations overview phase styles units yields, an alias looks like u1 or c1, and `just show` lists what this seat has written\n"},
  "empty/units": {"code": 2, "stdout": "", "stderr": "error: this seat has no units projection yet; run `just turn` to write one\n"},
  "nopriced/default": {"code": 0, "stdout": "# rev 7 turn 3\ngame      game_12345678901234567890 · seat 1 AgentPlace1 · controller codex-test\nobjective Maximize final Freeciv civilization score.\nbudget    turn 3 of 5000 · 4997 remaining\nphase     awaiting_agent · turn 3 phase 0 · active yes\n\n# rev 7 turn 3\nsince rev 6 turn 3 · last update: state\n\n## units\n- u1 moved to 31,72\n\nfiles: header phase overview units cities map diplomacy delta nations styles governments options/u1 options/unscoped\nread one with `just show NAME`, search with `just show --grep PATTERN`\n", "stderr": ""},
  "nostate/default": {"code": 0, "stdout": "# rev 7 turn 3\ngame      game_12345678901234567890 · seat 1 AgentPlace1 · controller codex-test\nobjective Maximize final Freeciv civilization score.\nbudget    turn 3 of 5000 · 4997 remaining\nphase     awaiting_agent · turn 3 phase 0 · active yes\n\n# rev 7 turn 3\nsince rev 6 turn 3 · last update: state\n\n## units\n- u1 moved to 31,72\n\nfiles: header phase overview units cities map yields diplomacy delta nations styles governments options/u1 options/unscoped\nread one with `just show NAME`, search with `just show --grep PATTERN`\n", "stderr": ""},
  "nostate/units": {"code": 0, "stdout": "# rev 7 turn 3\n# units 2/2 complete\nalias\tunit    \twho\tpos  \tmoves\thp   \tactivity\torders\nu1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\nu2   \tWorkers \town\t30,71\t3/3  \t10/10\tidle    \t-\n", "stderr": ""},
  "stale/alias_c1": {"code": 0, "stdout": "stale: rendered at rev7, now rev12 — aliases will be re-verified by meaning on use\ncities: c1   \tLondon\t31,72\t1   \t2/2 \t1\n", "stderr": ""},
  "stale/alias_r1": {"code": 0, "stdout": "stale: rendered at rev7, now rev12 — aliases will be re-verified by meaning on use\ndiplomacy: r1   \tRomans     \twar  \t-\n", "stderr": ""},
  "stale/alias_u1": {"code": 0, "stdout": "stale: rendered at rev7, now rev12 — aliases will be re-verified by meaning on use\nunits: u1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\n\n# rev 7 turn 3\n# actor unit u1\n# actions 2/2 complete\nalias\tkind             \ttarget\targs\tlabel                 \tflags\na1   \tunit.found_city  \t31,72 \t{name}\tFound a city here   \t-\na2   \tunit.order       \t31,71 \t-   \tMove unit to (31, 71) \tvariant=standard\n", "stderr": ""},
  "stale/alias_u1_json": {"code": 0, "stdout": "{\"command\":\"show\",\"lines\":[\"stale: rendered at rev7, now rev12 \\u2014 aliases will be re-verified by meaning on use\",\"units: u1   \\tSettlers\\town\\t31,72\\t1/1  \\t20/20\\tidle    \\t-\",\"\",\"# rev 7 turn 3\",\"# actor unit u1\",\"# actions 2/2 complete\",\"alias\\tkind             \\ttarget\\targs\\tlabel                 \\tflags\",\"a1   \\tunit.found_city  \\t31,72 \\t{name}\\tFound a city here   \\t-\",\"a2   \\tunit.order       \\t31,71 \\t-   \\tMove unit to (31, 71) \\tvariant=standard\"],\"schema_version\":1,\"selection\":\"u1\"}\n", "stderr": ""},
  "stale/alias_unscoped": {"code": 0, "stdout": "stale: rendered at rev7, now rev12 — aliases will be re-verified by meaning on use\n# rev 7 turn 3\n# unscoped catalog (union of the kinds fetched at this rev)\n# actions 1/1 complete\nalias\tkind      \ttarget\targs\tlabel        \tflags\na3   \tphase.end \t-     \t-   \tEnd the phase\t-\n", "stderr": ""},
  "stale/default": {"code": 0, "stdout": "stale: rendered at rev7, now rev12 — aliases will be re-verified by meaning on use\n# rev 7 turn 3\ngame      game_12345678901234567890 · seat 1 AgentPlace1 · controller codex-test\nobjective Maximize final Freeciv civilization score.\nbudget    turn 3 of 5000 · 4997 remaining\nphase     awaiting_agent · turn 3 phase 0 · active yes\n\n# rev 7 turn 3\nsince rev 6 turn 3 · last update: state\n\n## units\n- u1 moved to 31,72\n\nfiles: header phase overview units cities map yields diplomacy delta nations styles governments options/u1 options/unscoped\nread one with `just show NAME`, search with `just show --grep PATTERN`\n", "stderr": ""},
  "stale/grep_missing": {"code": 0, "stdout": "stale: rendered at rev7, now rev12 — aliases will be re-verified by meaning on use\nno mirror line matches 'zzz-not-here'\n", "stderr": ""},
  "stale/grep_settlers": {"code": 0, "stdout": "stale: rendered at rev7, now rev12 — aliases will be re-verified by meaning on use\nunits:4: u1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\n", "stderr": ""},
  "stale/map": {"code": 0, "stdout": "stale: rendered at rev7, now rev12 — aliases will be re-verified by meaning on use\n# rev 7 turn 3\n# map 64x64 · 12 of 12 tiles known\n# window x 30..33 y 71..73\n# legend '?'=never seen · UPPERCASE=visible now · lowercase=remembered\n# terrain F=Forest · G=Grassland · H=Hills · O=Ocean\n   71 |GGFH\n   72 |GGgh\n   73 |OOOO\n", "stderr": ""},
  "stale/nations": {"code": 0, "stdout": "stale: rendered at rev7, now rev12 — aliases will be re-verified by meaning on use\n# rev 7 turn 3\n# nations 2/2 complete\nid                                     \tnation  \tdefault_style_id\nnation_0cc67771419649722f5e79cac23dd5c6\tAmerican\tstyle_5f71e8712eb4fd2349ed14ee3fea1a9b\nnation_1dd67771419649722f5e79cac23dd5c6\tRoman   \tstyle_6f71e8712eb4fd2349ed14ee3fea1a9b\n", "stderr": ""},
  "stale/phase": {"code": 0, "stdout": "{\n  \"active\": true,\n  \"phase\": 0,\n  \"schema_version\": 1,\n  \"state\": \"awaiting_agent\",\n  \"turn\": 3\n}\n", "stderr": ""},
  "stale/units": {"code": 0, "stdout": "stale: rendered at rev7, now rev12 — aliases will be re-verified by meaning on use\n# rev 7 turn 3\n# units 2/2 complete\nalias\tunit    \twho\tpos  \tmoves\thp   \tactivity\torders\nu1   \tSettlers\town\t31,72\t1/1  \t20/20\tidle    \t-\nu2   \tWorkers \town\t30,71\t3/3  \t10/10\tidle    \t-\n", "stderr": ""},
} as const;

// ---------------------------------------------------------------------------
// The fixture seat
// ---------------------------------------------------------------------------

const scratches: Scratch[] = [];
afterEach(() => {
  while (scratches.length > 0) scratches.pop()?.cleanup();
});

const clientState = (revision: number | null): unknown => ({
  schema_version: 5,
  game_id: FIXTURE_GAME_ID,
  agent_id: FIXTURE_AGENT_ID,
  last_revision:
    revision === null ? null : { revision, state_token: `token_3_${revision}`, turn: 3 },
  actions: {},
  pending_catalogs: {},
  batches: {},
  receipts: {},
  action_aliases: { state_revision: null, by_alias: {} },
  entity_aliases: {},
  tile_aliases: {},
  drained_actors: [],
});

interface Seat {
  readonly sessionPath: string;
  readonly mirror: string;
  readonly layer: Layer.Layer<SessionStore | PrivateFs>;
}

interface SeatOptions {
  readonly files?: ReadonlyArray<readonly [string, string]>;
  /** `null` writes no `.v2-state` at all; a number is its `last_revision`. */
  readonly revision?: number | null;
}

const seat = (options: SeatOptions = {}): Seat => {
  const scratch = scratchWorkspace();
  scratches.push(scratch);
  const home = path.join(scratch.workspace.stateRoot, FIXTURE_GAME_ID);
  fs.mkdirSync(home, { mode: 0o700, recursive: true });
  const sessionPath = path.join(home, 'codex-test.json');
  fs.writeFileSync(sessionPath, `${JSON.stringify(sessionFile(), null, 2)}\n`, { mode: 0o600 });
  const revision = options.revision === undefined ? 7 : options.revision;
  if (revision !== null) {
    fs.writeFileSync(
      path.join(home, 'codex-test.v2-state'),
      `${JSON.stringify(clientState(revision), null, 2)}\n`,
      { mode: 0o600 }
    );
  }
  const mirror = path.join(home, 'codex-test');
  for (const [relative, text] of options.files ?? MIRROR) {
    const target = path.join(mirror, relative);
    fs.mkdirSync(path.dirname(target), { mode: 0o700, recursive: true });
    fs.writeFileSync(target, text, { mode: 0o600 });
  }
  const store = sessionStoreFor(scratch.workspace, scratch.files, v2StateSchema, {});
  return {
    sessionPath,
    mirror,
    layer: Layer.merge(scratch.layer, Layer.succeed(SessionStore, store)),
  };
};

interface Outcome {
  readonly stdout: string;
  readonly stderr: string;
}

const show = async (
  fixture: Seat,
  overrides: Partial<ShowOptions> = {}
): Promise<Outcome> => {
  const out: string[] = [];
  const originalLog = console.log;
  console.log = (...parts: ReadonlyArray<unknown>) => out.push(parts.map(String).join(' '));
  try {
    const result = await Effect.runPromise(
      Effect.either(
        Effect.provide(
          runShow({
            session: fixture.sessionPath,
            name: '',
            grep: '',
            regex: false,
            yields: false,
            json: false,
            ...overrides,
          }),
          fixture.layer
        )
      )
    );
    return {
      stdout: out.length === 0 ? '' : `${out.join('\n')}\n`,
      stderr: Either.isLeft(result) ? `error: ${result.left.message}\n` : '',
    };
  } finally {
    console.log = originalLog;
  }
};

/** Assert one whole stream against the CPython capture. */
const golden = async (
  key: keyof typeof GOLDEN,
  fixture: Seat,
  overrides: Partial<ShowOptions> = {}
): Promise<void> => {
  const expected = GOLDEN[key];
  const actual = await show(fixture, overrides);
  expect(actual.stdout).toBe(expected.stdout);
  expect(actual.stderr).toBe(expected.stderr);
};

// ---------------------------------------------------------------------------

describe('the default listing', () => {
  test('bare show prints the header, the delta and the catalog', async () => {
    await golden('current/default', seat());
  });

  test('--json wraps the same lines in the show envelope', async () => {
    await golden('current/default_json', seat(), { json: true });
  });

  test('a mirror missing one file simply does not list it', async () => {
    await golden(
      'nopriced/default',
      seat({ files: MIRROR.filter(([name]) => name !== 'state/yields.tsv') })
    );
  });

  test('an empty mirror names `just state --section overview` before `just turn`', async () => {
    const fixture = seat({ files: [] });
    const { stdout, stderr } = await show(fixture);
    expect(stdout).toBe('');
    // The lobby has no units and no briefing to write, so `just turn` there
    // returns the lobby card without writing a file.
    expect(stderr).toBe(
      'error: this seat has no local state mirror yet; run `just state --section ' +
        'overview` (it works in the lobby too, and `just turn` writes the rest once ' +
        `the game is running); the files then appear under ${fixture.mirror}\n`
    );
    expect(stderr.indexOf('just state --section overview')).toBeLessThan(
      stderr.indexOf('just turn')
    );
  });

  test('--grep over an empty mirror earns the same refusal', async () => {
    const fixture = seat({ files: [] });
    const { stderr } = await show(fixture, { grep: 'anything' });
    expect(stderr).toContain('this seat has no local state mirror yet');
    expect(stderr).toContain(fixture.mirror);
  });
});

describe('a named section', () => {
  test('one file prints byte for byte, and nothing else', async () => {
    await golden('current/units', seat(), { name: 'units' });
    await golden('current/map', seat(), { name: 'map' });
    await golden('current/nations', seat(), { name: 'nations' });
    // `phase.json` carries no `# rev` stamp, so it is never banner-eligible.
    await golden('current/phase', seat(), { name: 'phase' });
  });

  test('a file this seat has not written names `just turn`', async () => {
    await golden('empty/units', seat({ files: [] }), { name: 'units' });
  });

  test('an unknown name lists the file names, sorted', async () => {
    await golden('current/unknown_name', seat(), { name: 'bogus' });
  });

  test('an alias-shaped name that is absent names its own repair', async () => {
    await golden('current/alias_u9', seat(), { name: 'u9' });
  });

  test('a traversal attempt never reaches outside the mirror', async () => {
    await golden('current/hostile_dotdot', seat(), { name: '../codex-test.v2-state' });
    await golden('current/hostile_abs', seat(), { name: '/etc/passwd' });
  });
});

describe('an entity alias', () => {
  test('u1 is its unit row followed by its option projection', async () => {
    await golden('current/alias_u1', seat(), { name: 'u1' });
  });

  test('c1 and r1 reach the city and relation tables the same way', async () => {
    await golden('current/alias_c1', seat(), { name: 'c1' });
    await golden('current/alias_r1', seat(), { name: 'r1' });
  });

  test('an option projection with no row prints alone, with no leading blank', async () => {
    await golden('current/alias_unscoped', seat(), { name: 'unscoped' });
  });

  test('--json carries the alias as the selection', async () => {
    await golden('current/alias_u1_json', seat(), { name: 'u1', json: true });
  });
});

describe('--grep', () => {
  test('a literal match prints `label:line: text`', async () => {
    await golden('current/grep_settlers', seat(), { grep: 'Settlers' });
  });

  test('the literal search is case-insensitive', async () => {
    await golden('current/grep_case', seat(), { grep: 'settlers' });
  });

  test('no match says so, with the pattern in CPython `repr` form', async () => {
    await golden('current/grep_missing', seat(), { grep: 'zzz-not-here' });
  });

  test('literal means literal: a metacharacter stands for itself', async () => {
    await golden('current/grep_literal_dot', seat(), { grep: 'Settl.rs' });
  });

  test('--regex opts into the engine', async () => {
    await golden('current/grep_regex_dot', seat(), { grep: 'Settl.rs', regex: true });
  });

  test('a catastrophic pattern is just text without --regex, and answers at once', async () => {
    const started = performance.now();
    await golden('current/grep_catastrophic', seat(), { grep: '(a|aa)+$' });
    expect(performance.now() - started).toBeLessThan(1000);
  });

  test('--regex refuses a quantifier applied to an already quantified group', async () => {
    await golden('current/grep_regex_nested', seat(), { grep: '(a+)+$', regex: true });
  });

  test('--regex reports an uncompilable pattern and names the flagless form', async () => {
    const { stdout, stderr } = await show(seat(), { grep: '(unbalanced', regex: true });
    expect(stdout).toBe('');
    // The engine's own wording differs from CPython's `missing ),
    // unterminated subpattern at position 0`; NOTES.md §U09 records it.
    expect(stderr.startsWith('error: just show --grep pattern is invalid: ')).toBe(true);
    expect(stderr.endsWith(
      '; drop --regex to search for the literal text `(unbalanced`\n'
    )).toBe(true);
  });

  /**
   * A pattern CPython compiles and this engine cannot: `\w` emits ~10 KB of
   * explicit code point ranges, so 84 of them (168 characters, well inside the
   * 200-character cap) outgrow JavaScriptCore's ~1 MB regex ceiling.  CPython
   * answers `just show --grep '\w\w…' --regex` at exit 0 — `no mirror line
   * matches …`, since no mirror line holds 100 word characters in a row — and
   * `--grep '\b\b…' --regex` with every line.  The port refuses instead, at
   * exit 2, with the remedy that does work; NOTES.md §19.3 records it.  The
   * assertion that matters is that this is a refusal at all: before it was a
   * `SyntaxError` out of the defect channel, printed as a stack trace.
   */
  test('a pattern too large for the engine refuses in words, not in a stack trace', async () => {
    for (const grep of ['\\w'.repeat(100), '\\b'.repeat(21)]) {
      const { stdout, stderr } = await show(seat(), { grep, regex: true });
      expect(stdout).toBe('');
      expect(stderr).toBe(
        'error: just show --grep pattern is invalid: the emitted pattern is too large ' +
          `for this engine; drop --regex to search for the literal text \`${grep}\`\n`
      );
    }
  });

  test('--regex gives up when it spends its wall-clock budget', async () => {
    const ticks = [0, 0, 99];
    const { stderr } = await show(seat(), {
      grep: '(a|aa)+$',
      regex: true,
      clock: () => ticks.shift() ?? 99,
    });
    expect(stderr).toBe(
      'error: just show --grep --regex took too long; narrow the pattern, or drop ' +
        '--regex to search for the literal text `(a|aa)+$`\n'
    );
  });

  test('a pattern over 200 characters is refused before anything is read', async () => {
    await golden('current/grep_long', seat(), { grep: 'x'.repeat(201) });
  });

  test('the match list stops at 200 and says it stopped', async () => {
    const rows = Array.from({ length: 250 }, (_unused, index) => `u${index}\tSettlers`);
    const fixture = seat({
      files: [['state/units.tsv', `# rev 7 turn 3\n${rows.join('\n')}\n`]],
    });
    const { stdout } = await show(fixture, { grep: 'Settlers' });
    const lines = stdout.split('\n').slice(0, -1);
    expect(lines).toHaveLength(V2_SHOW_MAX_MATCHES + 1);
    expect(lines[0]).toBe('units:2: u0\tSettlers');
    expect(lines[V2_SHOW_MAX_MATCHES - 1]).toBe('units:201: u199\tSettlers');
    expect(lines[V2_SHOW_MAX_MATCHES]).toBe(
      '(stopped at 200 matches; narrow the pattern)'
    );
  });

  test('exactly 200 matches is not truncated', async () => {
    const rows = Array.from({ length: 199 }, (_unused, index) => `u${index}\tSettlers`);
    const fixture = seat({
      files: [['state/units.tsv', `# Settlers rev 7 turn 3\n${rows.join('\n')}\n`]],
    });
    const { stdout } = await show(fixture, { grep: 'Settlers' });
    expect(stdout.split('\n').slice(0, -1)).toHaveLength(V2_SHOW_MAX_MATCHES);
    expect(stdout).not.toContain('stopped at');
  });

  test('--json carries `grep PATTERN` as the selection', async () => {
    await golden('current/grep_settlers_json', seat(), { grep: 'Settlers', json: true });
  });
});

describe('flag precedence', () => {
  test('a name and --grep together are refused', async () => {
    await golden('current/name_and_grep', seat(), { name: 'units', grep: 'Settlers' });
  });

  test('--regex without --grep is refused', async () => {
    await golden('current/regex_without_grep', seat(), { regex: true });
  });
});

describe('the staleness banner', () => {
  test('a current projection carries no banner', async () => {
    await golden('current/units', seat({ revision: 7 }), { name: 'units' });
  });

  test('a lagging projection leads with the re-verification line', async () => {
    await golden('stale/units', seat({ revision: 12 }), { name: 'units' });
    await golden('stale/map', seat({ revision: 12 }), { name: 'map' });
    await golden('stale/nations', seat({ revision: 12 }), { name: 'nations' });
  });

  test('the option projection carries the banner and the file still says rev 7', async () => {
    const fixture = seat({ revision: 12 });
    await golden('stale/alias_u1', fixture, { name: 'u1' });
    // Written at read time: the file itself is untouched.
    const stored = fs.readFileSync(
      path.join(fixture.mirror, 'state', 'options', 'u1.txt'),
      'utf8'
    );
    expect(stored).not.toContain('stale:');
    expect(stored).toContain('# rev 7 turn 3');
  });

  test('the default listing, the alias rows and --grep all carry it', async () => {
    await golden('stale/default', seat({ revision: 12 }));
    await golden('stale/alias_c1', seat({ revision: 12 }), { name: 'c1' });
    await golden('stale/alias_r1', seat({ revision: 12 }), { name: 'r1' });
    await golden('stale/alias_unscoped', seat({ revision: 12 }), { name: 'unscoped' });
    await golden('stale/grep_settlers', seat({ revision: 12 }), { grep: 'Settlers' });
    await golden('stale/grep_missing', seat({ revision: 12 }), { grep: 'zzz-not-here' });
    await golden('stale/alias_u1_json', seat({ revision: 12 }), { name: 'u1', json: true });
  });

  test('a file with no revision stamp is never judged stale', async () => {
    await golden('stale/phase', seat({ revision: 12 }), { name: 'phase' });
  });

  test('an unreadable private cache annotates nothing and blocks nothing', async () => {
    await golden('nostate/default', seat({ revision: null }));
    await golden('nostate/units', seat({ revision: null }), { name: 'units' });
    // The same holds for a `.v2-state` that exists but does not parse.
    const broken = seat({ revision: 12 });
    fs.writeFileSync(`${broken.sessionPath.slice(0, -5)}.v2-state`, 'not json\n', {
      mode: 0o600,
    });
    await golden('nostate/units', broken, { name: 'units' });
  });

  test('the sources of a bare listing are every present file', async () => {
    const fixture = seat();
    const sources = await Effect.runPromise(
      Effect.provide(showSources(fixture.sessionPath, '', ''), fixture.layer)
    );
    expect(sources).toHaveLength(MIRROR.length);
  });

  test('the sources of an alias are the three row tables plus its option file', async () => {
    const fixture = seat();
    const sources = await Effect.runPromise(
      Effect.provide(showSources(fixture.sessionPath, 'u1', ''), fixture.layer)
    );
    expect(sources).toEqual([
      ['state', 'units.tsv'],
      ['state', 'cities.tsv'],
      ['state', 'diplomacy.tsv'],
      ['state', 'options', 'u1.txt'],
    ]);
  });
});

describe('the options listing', () => {
  test('only `.txt` files count, dotfiles never do, and the order is sorted', async () => {
    const fixture = seat({
      files: [
        ...MIRROR,
        ['state/options/.hidden.txt', 'x\n'],
        ['state/options/notes.md', 'x\n'],
        ['state/options/c1.txt', '# rev 7 turn 3\n'],
      ],
    });
    const names = await Effect.runPromise(
      Effect.provide(showOptionFiles(fixture.sessionPath), fixture.layer)
    );
    expect(names).toEqual(['c1.txt', 'u1.txt', 'unscoped.txt']);
  });

  test('a symlinked options directory lists nothing rather than following it', async () => {
    const fixture = seat();
    const options = path.join(fixture.mirror, 'state', 'options');
    fs.rmSync(options, { recursive: true, force: true });
    fs.symlinkSync('/etc', options);
    const names = await Effect.runPromise(
      Effect.provide(showOptionFiles(fixture.sessionPath), fixture.layer)
    );
    expect(names).toEqual([]);
  });
});

describe('pyRepr', () => {
  test('it reproduces CPython `repr` for the patterns a refusal echoes', () => {
    expect(pyRepr('zzz-not-here')).toBe("'zzz-not-here'");
    expect(pyRepr('(a|aa)+$')).toBe("'(a|aa)+$'");
    expect(pyRepr("it's")).toBe('"it\'s"');
    expect(pyRepr('say "hi"')).toBe('\'say "hi"\'');
    expect(pyRepr('both \' and "')).toBe('\'both \\\' and "\'');
    expect(pyRepr('a\\b')).toBe("'a\\\\b'");
    expect(pyRepr('tab\there')).toBe("'tab\\there'");
    expect(pyRepr('bell\x07')).toBe("'bell\\x07'");
    expect(pyRepr('naïve · ok')).toBe("'naïve · ok'");
  });

  /**
   * `repr` escapes by `str.isprintable()`, not by "is it a control character",
   * and the width of the escape follows the code point.  Every expectation
   * below is `repr(…)` from this machine's CPython; the same comparison was run
   * over **every** code point in `'x' + chr(c) + 'y'` form (1,112,064 of them,
   * zero mismatches) and over 20,000 generated strings.
   */
  test('an unprintable character is escaped at CPython’s own width', () => {
    // U+FEFF and U+00A0 are `Cf`/`Zs`, not controls: `\s`-thinking misses them.
    expect(pyRepr('\ufeffSettlers')).toBe("'\\ufeffSettlers'");
    expect(pyRepr('Set\xa0tlers')).toBe("'Set\\xa0tlers'");
    // An unassigned code point is unprintable too (`Cn`).
    expect(pyRepr('Set\u0378tlers')).toBe("'Set\\u0378tlers'");
    // Astral, so `\U` with eight digits — and one code point, not two units.
    expect(pyRepr('a\u{e0001}b')).toBe("'a\\U000e0001b'");
    // U+2028 is `Zl`; the plain space is the one `Zs` that stays printable.
    expect(pyRepr('a\u2028b')).toBe("'a\\u2028b'");
    expect(pyRepr('a b')).toBe("'a b'");
    // Printable non-ASCII stays verbatim, at every width.
    expect(pyRepr('Große Δ \u{1f642}')).toBe("'Große Δ \u{1f642}'");
  });
});

/**
 * `command_show` strips the name and the pattern with `str.strip()`, whose
 * whitespace class is neither a superset nor a subset of `String.trim()`'s.
 * Every golden below is CPython's own stdout/stderr for the same argument
 * against the same mirror.
 */
describe('CPython `str.strip()`, not `String.trim()`', () => {
  test('U+0085 and U+001C-U+001F are stripped from a name', async () => {
    await golden('current/name_x85_lead', seat(), { name: '\x85units' });
    await golden('current/name_x1c_lead', seat(), { name: '\x1cunits' });
    await golden('current/name_x1f_lead', seat(), { name: '\x1funits' });
    // The characters both classes agree about still work, of course.
    await golden('current/name_nbsp_lead', seat(), { name: '\xa0units' });
    await golden('current/name_space_lead', seat(), { name: '  units  ' });
  });

  test('U+FEFF is not stripped, so SHOW_NAME_RE refuses the name', async () => {
    await golden('current/name_bom_lead', seat(), { name: '\ufeffunits' });
    // Trailing, too: `u1` with a trailing U+FEFF is not the alias `u1`.
    await golden('current/name_bom_trail', seat(), { name: 'u1\ufeff' });
  });

  test('the same class decides what --grep actually searches for', async () => {
    // Stripped: the needle is `Settlers`, and the row is found.
    await golden('current/grep_x85_settlers', seat(), { grep: '\x85Settlers' });
    // Not stripped: the needle keeps the BOM and matches nothing.
    await golden('current/grep_bom_settlers', seat(), { grep: '\ufeffSettlers' });
  });

  test('a pattern of nothing but whitespace strips to empty and is no --grep at all', async () => {
    await golden('current/grep_x85_only', seat(), { grep: '\x85' });
    await golden('current/grep_nbsp_only', seat(), { grep: '\xa0' });
    // …which is what makes `--regex` with it a refusal rather than a search.
    await golden('current/grep_x85_only_regex', seat(), { grep: '\x85', regex: true });
    // A BOM survives the strip, so it *is* a pattern — and it matches nothing.
    await golden('current/grep_bom_only', seat(), { grep: '\ufeff' });
  });

  /**
   * `_show_rows` compares `line.split("\t", 1)[0].strip()` with the alias, so
   * the same class decides which rows a `just show u1` prints.  The three rows
   * below were run through CPython verbatim: `u1` prints its row, `u2` is a
   * refusal (the BOM stays in the cell), `u3` prints its row with the padding
   * intact — the strip decides the *comparison*, never the printed bytes.
   *
   * The `u1` row also pins U04's `splitLines`: `str.splitlines()` breaks on
   * U+001C and U+0085, so `\x1cu1\x85\tSettlers` is three lines to CPython and
   * the alias cell it matches is the bare `u1` on the second of them.
   */
  test('the alias column is compared after CPython’s strip', async () => {
    const rows =
      '# rev 7 turn 3\n# units 3/3 complete\nalias\tunit\n' +
      '\x1cu1\x85\tSettlers\n' +
      'u2\ufeff\tWorkers\n' +
      '  u3  \tExplorer\n';
    const files: ReadonlyArray<readonly [string, string]> = [
      ...MIRROR.filter(([name]) => name !== 'state/units.tsv'),
      ['state/units.tsv', rows],
    ];
    const options = MIRROR.find(([name]) => name === 'state/options/u1.txt')?.[1] ?? '';
    expect((await show(seat({ files }), { name: 'u1' })).stdout).toBe(`units: u1\n\n${options}`);
    expect((await show(seat({ files }), { name: 'u2' })).stderr).toBe(
      'error: the mirror holds nothing named u2; run `just legal --actor_id u2 --all` ' +
        'to write state/options/u2.txt, or `just show` to list what is there\n'
    );
    expect((await show(seat({ files }), { name: 'u3' })).stdout).toBe('units:   u3  \tExplorer\n');
  });

  test('a name of nothing but whitespace is no name, so it is not "both"', async () => {
    await golden('current/grep_bom_and_name', seat(), { name: '\ufeff', grep: 'Settlers' });
  });

  test('an unprintable needle is echoed through CPython `repr`', async () => {
    await golden('current/grep_interior_nonprintable', seat(), { grep: 'Set\u0378tlers' });
    await golden('current/grep_nbsp_interior', seat(), { grep: 'Set\xa0tlers' });
    await golden('current/grep_astral_nonprintable', seat(), { grep: 'a\u{e0001}b' });
  });
});
