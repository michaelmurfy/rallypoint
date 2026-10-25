#!/bin/sh
# Apply patches/*.patch to the engine submodule; skips patches that are already applied.
set -e
cd "$(dirname "$0")/.."
for p in patches/*.patch; do
  if git -C engine apply --reverse --check "../$p" 2>/dev/null; then echo "already applied: $p"
  else git -C engine apply "../$p" && echo "applied: $p"; fi
done
