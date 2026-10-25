#!/bin/sh
# Apply patches/*.patch to the engine submodule; skips patches that are already applied.
# A later patch may edit lines an earlier one added, so an applied earlier patch can fail its reverse check.
# So find the newest patch that is applied: it and everything before it are applied; the rest are applied now.
set -e
cd "$(dirname "$0")/.."
list=$(ls patches/*.patch)
done_upto=""
for p in $(echo "$list" | sort -r); do
  if git -C engine apply --reverse --check "../$p" 2>/dev/null; then done_upto="$p"; break; fi
done
state=todo; [ -z "$done_upto" ] && state=todo
for p in $list; do
  if [ -n "$done_upto" ] && [ "$state" = todo ]; then
    echo "already applied: $p"
    [ "$p" = "$done_upto" ] && state=done
  else
    git -C engine apply "../$p" && echo "applied: $p"
  fi
done
