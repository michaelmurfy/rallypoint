#include "weapon.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int

namespace ra2engine {

bool verses_allows(const std::vector<int>& verses, int armor_ix) {
    if (verses.empty()) return true;
    int idx = armor_ix;
    if (idx < 0 || idx >= static_cast<int>(verses.size())) idx = 0;
    return verses[idx] != 0;
}

bool slot_can_target(const WeaponSlot& w, bool air, int armor_ix) {
    if (!w.valid || w.never_use) return false;   // W7-01-01: NeverUse never fires
    if (air ? !w.aa : !w.ag) return false;
    return verses_allows(w.verses, armor_ix);
}

int choose_weapon(const WeaponSlot& primary, const WeaponSlot& secondary, bool air,
                  int armor_ix) {
    if (slot_can_target(primary, air, armor_ix)) return 0;
    if (slot_can_target(secondary, air, armor_ix)) return 1;
    return -1;
}

}  // namespace ra2engine
