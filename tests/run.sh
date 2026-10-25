#!/bin/sh
# Apply engine patches (if not already applied) and run the standalone engine-math tests.
set -e
cd "$(dirname "$0")/.."
sh patches/apply.sh
for t in widescreen relaywire; do
  c++ -std=c++17 -Wall -Iengine/Core/GameEngine/Include tests/${t}_test.cpp -o /tmp/${t}_test
  /tmp/${t}_test && echo "${t}_test ok"
done
