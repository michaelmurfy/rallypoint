#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace ra2assets {
// Reads an entire binary file with a single block read (seek-to-end to
// size, one read()). Throws std::runtime_error("cannot open file: " + path)
// if the file cannot be opened, and ("short read: " + path) if fewer bytes
// arrive than the file reported. Replaces per-byte istreambuf_iterator
// slurps (measured ~79 s startup -> seconds; see docs/superpowers/specs/
// 2026-07-09-foundation-batch-design.md, sub-project 4).
std::vector<uint8_t> read_file(const std::string& path);
}
