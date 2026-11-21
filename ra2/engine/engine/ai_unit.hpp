#pragma once
// Wave-7 AI per-unit rules (W7-01-08 / W7-02-04 auto-deploy, W7-04-18
// ToProtect). The World side lives in ai_unit.cpp; this header only carries
// the per-unit state (save v35). All defaults are INERT: a house whose
// AiHouseConfig leaves auto_deploy_frames at -1 and protect_units false never
// reads or writes it.
#include <cstdint>

namespace ra2engine {

struct UnitAi {
    // [RE: MissionClass +0xC0 CurrentMissionStartTime, read by
    // InfantryClass FUN_00521050 @0x521290]: the frame this computer-owned
    // unit last entered its idle Guard state (stopped, no attack order).
    // -1 while it is busy (moving / attack order) or never seen.
    std::int64_t guard_since = -1;
};

}  // namespace ra2engine
