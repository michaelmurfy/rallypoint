#include "capture.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int
namespace ra2engine {
// All capture arithmetic is header-inline (small scalar math, the
// Superweapon::tick precedent). This TU exists so engine/capture.cpp can be
// registered in BOTH cmake files uniformly with the other engine modules; the
// anchor keeps the object non-empty and gives nm-verify a symbol to find.
const char* capture_module_tag() { return "capture"; }
}  // namespace ra2engine
