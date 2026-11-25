#!/bin/sh
# After a failed test step: `sh scripts/ci-test-report.sh <log>...` turns the failures in doctest and cargo test
# output into annotations (shown on the pull request) and a short job summary, so nobody has to scroll a
# 3000-line log to find the one line that matters.
[ $# -gt 0 ] || exit 0
for f; do [ -f "$f" ] || { echo "no $f: the tests did not run, see the step that failed"; exit 0; }; done
ws="$(printf %s "${GITHUB_WORKSPACE:-$PWD}/" | tr "\\\\" /)"  # awk -v would read the backslashes of a Windows path as escapes
out=$(awk -v ws="$ws" '
  { sub(/\r$/, "") }
  function rel(p) { gsub(/\\/, "/", p); if (index(p, ws) == 1) p = substr(p, length(ws) + 1); return p }
  # doctest: /abs/path/file.cpp:120: FATAL ERROR: message   (also ERROR:)
  match($0, /^[^ ]+\.(cpp|hpp|h|inc):[0-9]+: (FATAL ERROR|ERROR): /) {
    split(substr($0, 1, RLENGTH), a, ":"); msg = substr($0, RLENGTH + 1)
    printf "::error file=%s,line=%s::%s %s\n", rel(a[1]), a[2], a[3] ~ /FATAL/ ? "FATAL ERROR:" : "ERROR:", msg
    next
  }
  # cargo test: thread NAME panicked at path/file.rs:LINE:COL: then the message on the next line
  /panicked at .*\.rs:[0-9]+:[0-9]+:$/ {
    n = split($0, w, " "); loc = w[n]; sub(/:$/, "", loc); split(loc, a, ":")
    test = $2; gsub(/\047/, "", test); file = rel(a[1]); line = a[2]; want = 1; next
  }
  want { printf "::error file=%s,line=%s::%s: %s\n", file, line, test, $0; want = 0 }
' "$@" | sort -u)
[ -n "$out" ] || { echo "no test failure lines found in $*"; exit 0; }
echo "$out" | head -50
hint=""
# Tests that need the real game files must skip without them: CI has none.
if echo "$out" | grep -qE 'RA2_INSTALL_DIR|assets_available|\.mix not found|no RA2 install'; then
  hint="A failing test needs the RA2 game files. CI has none: such a test must skip (doctest MESSAGE + return) when RA2_INSTALL_DIR is unset."
  echo "::error::$hint"
fi
if [ -n "$GITHUB_STEP_SUMMARY" ]; then
  {
    echo "### Test failures"
    [ -z "$hint" ] || printf '\n**%s**\n' "$hint"
    echo; echo '```'
    echo "$out" | head -50 | sed 's/^::error file=\([^,]*\),line=\([^:]*\)::/\1:\2  /'
    echo '```'
  } >> "$GITHUB_STEP_SUMMARY"
fi
