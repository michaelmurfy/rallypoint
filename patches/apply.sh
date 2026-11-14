#!/bin/sh
# Apply patches/*.patch to engine/ (a plain directory in this repository); skips patches that are already applied.
# A later patch may edit lines an earlier one added, so an applied earlier patch can fail its reverse check.
# So find the newest patch that is applied: it and everything before it are applied; the rest are applied now.
# The patches are relative to engine/, so they are applied from the repository root with --directory.
set -e
cd "$(dirname "$0")/.."
list=$(ls patches/*.patch)
done_upto=""
for p in $(echo "$list" | sort -r); do
  if git apply --directory=engine --reverse --check "$p" 2>/dev/null; then done_upto="$p"; break; fi
done
state=todo
for p in $list; do
  if [ -n "$done_upto" ] && [ "$state" = todo ]; then
    echo "already applied: $p"
    [ "$p" = "$done_upto" ] && state=done
  else
    git apply --directory=engine "$p" && echo "applied: $p"
  fi
done
