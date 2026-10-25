#!/bin/sh
# Apply engine patches (if not already applied) and run the standalone engine-math tests.
set -e
cd "$(dirname "$0")/.."
sh patches/apply.sh
for t in widescreen relaywire lanwire detmath; do
  c++ -std=c++17 -Wall -ffp-contract=off -Iengine/Core/GameEngine/Include -Iengine/Core/Libraries/Source/WWVegas/WWLib tests/${t}_test.cpp -o /tmp/${t}_test
  /tmp/${t}_test && echo "${t}_test ok"
done
