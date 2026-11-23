#pragma once
// R-171: dev-only sampling profiler (Windows x64; no-op elsewhere). See
// os_profiler.cpp. main() starts it when RA2_PROFILE=<file> is set.
namespace ra2engine {
void os_profiler_start(const char* path);
void os_profiler_stop();
}  // namespace ra2engine
