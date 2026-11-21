#!/bin/sh
# RA2 two-client multiplayer harness (see ra2/README.md "Multiplayer harness").
# Layer A (default): lobby + tunnel token auth with fake game sockets (CI).
# Layer B: add --real to also launch two engines under Xvfb when data + patched engine exist.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$root"
export RA2_MP_LOG="${RA2_MP_LOG:-/tmp/ra2-mp-harness}"
export RA2_DATA="${RA2_DATA:-$HOME/rp-steam/game/RA2}"
export RA2ENGINE="${RA2ENGINE:-$HOME/rp-build/ra2engine}"
mkdir -p "$RA2_MP_LOG"
exec cargo run -p generals-core --example ra2_mp_harness -- "$@"
