#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace ra2assets {

// One section's 3x4 transform for one animation frame (row-major rows).
struct HvaSectionXform { float transform[3][4]; };

// Decoded .hva voxel-animation file: per animation frame, one transform per
// VXL section. section_names[i] pairs with frames[f][i].
struct HvaModel {
    int frame_count = 0;
    std::vector<std::string> section_names;
    std::vector<std::vector<HvaSectionXform>> frames;  // [frame][section]
};

// Parse a .hva byte buffer. Throws std::runtime_error (with context) on a
// truncated buffer, zero frame/section counts, or a total-size mismatch
// (the file must be exactly 24 + 16*sections + 48*frames*sections bytes).
HvaModel load_hva(const uint8_t* data, size_t len);

} // namespace ra2assets
