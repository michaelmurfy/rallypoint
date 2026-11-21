#include "occupancy.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int
// Occupancy is currently header-only (all members are trivial inline
// accessors). This translation unit exists so the class has a compiled home in
// both the test binary and ra2engine (see CMakeLists.txt / ra2_sources.cmake)
// and to hold any future out-of-line members.
namespace ra2engine {}  // namespace ra2engine
