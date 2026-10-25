#!/bin/sh
# Apply engine patches (if not already applied) and run the standalone engine-math tests.
set -e
cd "$(dirname "$0")/.."
sh patches/apply.sh
c++ -std=c++17 -Wall -Iengine/Core/GameEngine/Include tests/widescreen_test.cpp -o /tmp/widescreen_test
/tmp/widescreen_test && echo "widescreen_test ok"
