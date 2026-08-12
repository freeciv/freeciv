#!/usr/bin/env bash
#
# The offline byte-diff oracle (PLAN.md §"The oracle", item 2).
#
# Copies a finished game workspace to a scratch directory twice — once for the
# Python client, once for the TypeScript port — runs the same read-only command
# list against each, and diffs stdout **bytes**.  Every diff is a bug in the TS
# side unless the Python output is nondeterministic; irreducible diffs are
# recorded in NOTES.md with a justification.
#
# Two copies, not one: `show` refreshes the mirror it reads, so a shared
# workspace would let one side's writes decide the other side's output.  Both
# copies start from the same bytes and see the same command sequence, so their
# mirrors evolve in lockstep.
#
# STRICTLY OFFLINE.  Every command here reads the workspace and never opens a
# socket: `help`/`rules`/`prompt` are static text, `show` is documented as
# "reads local files, zero network", and `use` only moves the current-session
# pointer.  `health`, `state`, `legal`, `turn`, `do`, `wait` and `monitor` are
# deliberately absent — they would talk to a live supervisor.
#
# Usage:
#   test/diff-offline.sh                 # diff, print a report, exit 0/1
#   KEEP=1 test/diff-offline.sh          # keep the scratch tree for inspection
#   WORKSPACE=/path/to/game_... test/diff-offline.sh
#
set -uo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CLI_ROOT="$(cd -- "$HERE/.." && pwd)"
REPO_ROOT="$(cd -- "$CLI_ROOT/.." && pwd)"

# Parity mode: the Python side spells command mentions `just <verb>`, so the
# port must too for the byte diff to mean anything.  The `./play` spelling is
# covered by test/prog-prefix.test.ts, not by this oracle.
export PLAY_PROG=just

WORKSPACE="${WORKSPACE:-$REPO_ROOT/.play/game_Hsit9YEuBjKdJPPouFoGVYlk_pi_gpt-5.6-sol}"
PY_CLIENT="$REPO_ROOT/play/client.py"
PY_MIRROR="$REPO_ROOT/play/state_mirror.py"
PY_DOCS="$REPO_ROOT/play/docs"

# By default the port runs from source under `bun`.  Set PLAY_BIN to a compiled
# binary (`bun build --compile`, gate 5) to diff *that* instead — the bundler
# is a second thing that can break `src/docs/*.ts`'s template literals or the
# `bun:ffi` flock binding, and a green source run does not prove a green binary.
TS_ENTRY="${PLAY_BIN:-$CLI_ROOT/src/bin.ts}"

for required in "$WORKSPACE" "$PY_CLIENT" "$PY_MIRROR" "$PY_DOCS" "$TS_ENTRY"; do
  if [[ ! -e "$required" ]]; then
    echo "diff-offline: missing $required" >&2
    exit 1
  fi
done

SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/play-diff-offline.XXXXXXXX")"
cleanup() { [[ -n "${KEEP:-}" ]] || rm -rf "$SCRATCH"; }
trap cleanup EXIT

# --- the two workspace copies ----------------------------------------------
#
# The workspace carries its own (older) client.py and docs/; the port targets
# `play/client.py`, which is the canonical spec, so the canonical sources are
# copied over the workspace's — exactly what `just play` does when it stamps a
# workspace out.  CPython's ROOT is `Path(__file__).resolve().parent`, so
# client.py must sit *inside* the copy for `.sessions` to resolve there.
prepare() {
  local destination="$1"
  cp -R "$WORKSPACE" "$destination"
  chmod -R u+w "$destination"
  cp "$PY_CLIENT" "$PY_MIRROR" "$destination/"
  rm -rf "$destination/docs"
  cp -R "$PY_DOCS" "$destination/docs"
  rm -rf "$destination/__pycache__"
}

PY_WS="$SCRATCH/py"
TS_WS="$SCRATCH/ts"
prepare "$PY_WS"
prepare "$TS_WS"

export PLAY_STATE_DIR=".sessions"
export AGENT_EVAL_SERVICE_URL="${AGENT_EVAL_SERVICE_URL:-https://freeciv-api.localhost}"

# --- the case list ---------------------------------------------------------
#
# One case per line, arguments tab-separated (no argument contains a tab).
CASES=()
add_case() {
  local packed=""
  local part
  for part in "$@"; do packed+="$part"$'\t'; done
  CASES+=("$packed")
}

# doc surfaces — the justfile's `@cat docs/play.md` / `@cat docs/gameplay.md`
add_case help
add_case rules

# the bootstrap card, with and without every substitution
add_case prompt
add_case prompt --game-id game_Hsit9YEuBjKdJPPouFoGVYlk --name harness-model
add_case prompt --game-id game_Hsit9YEuBjKdJPPouFoGVYlk --name harness-model --place north
add_case prompt --game_id game_Hsit9YEuBjKdJPPouFoGVYlk --name harness-model

# the seat pointer
add_case use

# the mirror-reading renders
add_case show
add_case show header
add_case show overview
add_case show units
add_case show cities
add_case show delta
add_case show nations
add_case show styles
add_case show map
add_case show map --yields
add_case show unscoped
add_case show u1
add_case show u2
add_case show c1
add_case show p1
add_case show options/u1
add_case show options/c1

add_case show cache/nations
add_case show cache/styles
add_case show u76
add_case show c19
add_case show options/unscoped

# refusals and near misses
add_case show u999
add_case show nothing-here
add_case show options/u999
add_case show r1
add_case show a1
add_case show 'T(10,10)'
add_case show ''
add_case show map --yields --grep 'f2'
add_case show HEADER
add_case show ../client.py
add_case show /etc/passwd

# grep, literal and regex
add_case show --grep Settlers
add_case show --grep settlers
add_case show --grep 'u1'
add_case show --grep '^u[0-9]+' --regex
add_case show --grep 'Set.les' --regex
# A *leading* word boundary is the single most common regex an agent writes and
# the one the emitter has to work hardest for: `\b` expands to lookarounds over
# CPython's whole word class, so it is tested at every offset of every line
# rather than only where a preceding atom already matched.  Without these cases
# the port could (and did) blow `V2_SHOW_GREP_BUDGET_S` and exit 2 on a pattern
# CPython answers in 0.15 s, with the oracle still green.
add_case show --grep '\bu1\b' --regex
add_case show --grep '\bSettlers\b' --regex
add_case show --grep '\Bx' --regex
add_case show --grep 'u[0-9]+\b' --regex
add_case show --grep zzz-no-such-token
add_case show --grep '(' --regex
add_case show --grep '·'
add_case show --grep 'a.b'
add_case show --grep '[' --regex
add_case show --grep '.' --regex
add_case show --grep ''
add_case show units --grep Settlers
add_case show units --grep '^u' --regex

# --json surfaces that read the mirror
add_case show --json
add_case show units --json
add_case show map --json
add_case show --grep Settlers --json

# --- the reference side is the justfile, not client.py alone ---------------
#
# This CLI replaces `client.py` *and* the 403-line justfile dispatch veneer, so
# the reference for a case is whatever `just <case>` ran (play/justfile):
#
#   help  -> `@cat docs/play.md`        (justfile:398-399)   — not a subcommand
#   rules -> `@cat docs/gameplay.md`    (justfile:402-403)   — not a subcommand
#   everything else -> `python3 -B client.py …`, with the underscored flag
#     spellings the veneer accepted rewritten to the dashed ones argparse
#     declares (`[arg("game_id", long)]` -> `--game-id`, justfile:70-386).
#
# Comparing `python3 client.py help` instead would be comparing against a
# command the Python never had.
reference_run() {
  local -a args=("$@")
  case "${args[0]}" in
    help)
      cat "$PY_WS/docs/play.md"
      return $?
      ;;
    rules)
      cat "$PY_WS/docs/gameplay.md"
      return $?
      ;;
  esac
  local -a translated=()
  local part
  for part in "${args[@]}"; do
    if [[ "$part" == --*_* ]]; then
      translated+=("${part//_/-}")
    else
      translated+=("$part")
    fi
  done
  ( cd "$PY_WS" && python3 -B client.py "${translated[@]}" )
}

# --- the runner ------------------------------------------------------------

PASS=0
FAIL=0
FAILED_LABELS=()

run_case() {
  local packed="$1"
  local -a args
  IFS=$'\t' read -r -a args <<<"$packed"

  local label="play ${args[*]}"
  # Prefixed with the case index: two cases can render the same slug (`show`
  # and `show ""`), and under KEEP=1 the kept files must not overwrite.
  local slug
  slug="$(printf '%03d-%s' "$((PASS + FAIL))" "$(printf '%s' "${args[*]}" | tr -c 'A-Za-z0-9._-' '_')")"

  local py_out="$SCRATCH/out/$slug.py.out"
  local py_err="$SCRATCH/out/$slug.py.err"
  local ts_out="$SCRATCH/out/$slug.ts.out"
  local ts_err="$SCRATCH/out/$slug.ts.err"
  mkdir -p "$SCRATCH/out"

  reference_run "${args[@]}" >"$py_out" 2>"$py_err"
  local py_status=$?

  if [[ -n "${PLAY_BIN:-}" ]]; then
    ( cd "$TS_WS" && PLAY_ROOT="$TS_WS" "$TS_ENTRY" "${args[@]}" ) >"$ts_out" 2>"$ts_err"
  else
    ( cd "$TS_WS" && PLAY_ROOT="$TS_WS" bun "$TS_ENTRY" "${args[@]}" ) >"$ts_out" 2>"$ts_err"
  fi
  local ts_status=$?

  local problems=()
  if ! cmp -s "$py_out" "$ts_out"; then
    problems+=("stdout bytes differ")
  fi
  if [[ "$py_status" != "$ts_status" ]]; then
    problems+=("exit status $py_status != $ts_status")
  fi

  if [[ ${#problems[@]} -eq 0 ]]; then
    PASS=$((PASS + 1))
    printf '  ok    %s\n' "$label"
    return 0
  fi

  FAIL=$((FAIL + 1))
  FAILED_LABELS+=("$label")
  printf '  FAIL  %s  (%s)\n' "$label" "$(IFS='; '; echo "${problems[*]}")"
  if ! cmp -s "$py_out" "$ts_out"; then
    diff -u "$py_out" "$ts_out" | sed -n '1,40p' | sed 's/^/        /'
  fi
  if [[ -s "$py_err" || -s "$ts_err" ]]; then
    printf '        py stderr: %s\n' "$(head -c 300 "$py_err" | head -2)"
    printf '        ts stderr: %s\n' "$(head -c 300 "$ts_err" | head -2)"
  fi
  return 1
}

printf 'offline byte-diff oracle\n'
printf '  workspace : %s\n' "$WORKSPACE"
printf '  python    : %s\n' "$PY_CLIENT"
printf '  port      : %s\n' "$TS_ENTRY"
printf '  scratch   : %s\n\n' "$SCRATCH"

for packed in "${CASES[@]}"; do
  run_case "$packed"
done

printf '\n%d/%d identical\n' "$PASS" "$((PASS + FAIL))"
if [[ $FAIL -ne 0 ]]; then
  printf 'diverged:\n'
  for label in "${FAILED_LABELS[@]}"; do printf '  - %s\n' "$label"; done
  exit 1
fi
exit 0
