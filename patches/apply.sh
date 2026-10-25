#!/bin/sh
# Apply patches/*.patch to the engine submodule; skips patches that are already applied.
# A later patch may edit lines an earlier one added, so an applied earlier patch can fail its reverse check;
# if the last patch is applied, everything before it is too.
set -e
cd "$(dirname "$0")/.."
last=$(ls patches/*.patch | tail -n 1)
for p in patches/*.patch; do
  if git -C engine apply --reverse --check "../$p" 2>/dev/null; then echo "already applied: $p"
  elif git -C engine apply --reverse --check "../$last" 2>/dev/null; then echo "already applied (with later patches): $p"
  else git -C engine apply "../$p" && echo "applied: $p"; fi
done
