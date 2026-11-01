#!/bin/sh
# Apply engine patches (if not already applied) and run the standalone engine-math tests.
set -e
cd "$(dirname "$0")/.."
sh patches/apply.sh
for t in widescreen relaywire lanwire detmath; do
  c++ -std=c++17 -Wall -ffp-contract=off -Iengine/Core/GameEngine/Include -Iengine/Core/Libraries/Source/WWVegas/WWLib tests/${t}_test.cpp -o /tmp/${t}_test
  /tmp/${t}_test && echo "${t}_test ok"
done
# The macOS Win32 shim (Windows has the real FindFirstFile).
if [ "$(uname)" = Darwin ]; then
  c++ -std=c++17 -w -Iengine/Dependencies/Utility/osdep_compat -Iengine/Dependencies/Utility tests/findfile_test.cpp -o /tmp/findfile_test_bin
  /tmp/findfile_test_bin
fi
