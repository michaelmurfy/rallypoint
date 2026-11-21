#include "repair_sell.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int

#include <algorithm>

namespace ra2engine {

int repair_interval_frames(double repair_rate) {
    // [RE: repair-sell.md §A2] FLD Rules.RepairRate; FMUL 900.0; __ftol.
    // __ftol truncates toward zero -- 0.016*900 = 14.4 -> 14.
    const int n = om::ftol(repair_rate * 900.0);
    return n > 1 ? n : 1;   // floor at 1: a zero rate must not divide by zero
}

bool repair_due(std::uint64_t frame, int interval) {
    if (interval <= 0) return false;
    return frame % static_cast<std::uint64_t>(interval) == 0;
}

int repair_step_cost(int cost, int strength, int repair_step, double repair_percent) {
    // [RE: repair-sell.md §A2, FUN_00711470] -- transcribed instruction for
    // instruction; deviation #2 forbids re-deriving it.
    //   IDIV  Strength / RepairStep        (integer)
    //   IDIV  Cost / that                  (integer)
    //   FMUL  * RepairPercent ; __ftol     (truncating)
    //   CMP/JG -> clamp to >= 1
    const int per = (repair_step != 0) ? strength / repair_step : 0;
    const int n = (per != 0) ? cost / per : cost;
    const int c = om::ftol(static_cast<double>(n) * repair_percent);
    return c > 1 ? c : 1;
}

int sell_refund(int cost, int soylent, double refund_percent, bool owner_is_human,
                double country_mult, double house_mult) {
    // [RE: repair-sell.md §B1, FUN_00711300] Soylent is an ABSOLUTE credit
    // override and BYPASSES RefundPercent. [RE-GATE APPLIED §D0.2] GAREFN and
    // NAREFN -- both rostered -- set Soylent=300, so this branch is REACHED in
    // the clone today: a refinery refunds 300, not trunc(2000 * 0.50) = 1000.
    if (soylent != 0) return om::ftol(static_cast<double>(soylent) * country_mult);
    // v = ftol(Cost * HouseCostMult * CountryCostMult)
    const int v = om::ftol(static_cast<double>(cost) * house_mult * country_mult);
    // RefundPercent is applied ONLY when the owning house is human-controlled
    // (TEST AL,AL / JZ at 0x00711399). An AI house receives the UNSCALED value.
    if (!owner_is_human) return v;
    return om::ftol(static_cast<double>(v) * refund_percent);
}

int survivor_divisor(const RepairSellRules& r, int side_ix) {
    // [RE: repair-sell.md §B3] switch on Owner->SideIndex; default -> 0
    // survivors. World::Side is {Allied=0, Soviet=1, Yuri=2, Neutral=3}.
    switch (side_ix) {
        case 0: return r.allied_survivor_divisor;
        case 1: return r.soviet_survivor_divisor;
        case 2: return r.third_survivor_divisor;
        default: return 0;
    }
}

int survivor_count(int refund, int divisor, bool crewed, bool has_been_captured) {
    // [RE: repair-sell.md §B3, FUN_00451320]
    if (!crewed) return 0;              // !Type->Crewed (or NoCrew) -> 0
    int div = divisor;
    if (div == 0) return 0;
    if (has_been_captured) div *= 2;    // captured buildings yield half as many
    const int n = refund / div;         // INTEGER division of the REFUND
    if (n < 1) return 1;                // floor 1 (a Crewed building always
                                        // yields at least one survivor)
    if (n > 5) return 5;                // cap 5
    return n;
}

bool can_be_click_repaired(const Building& b, const BuildingTypeDef& t) {
    // [RE: repair-sell.md §A4, FUN_00452620]
    if (!b.alive) return false;
    if (b.state == Building::State::Dead) return false;
    if (b.hp <= 0) return false;                       // Health == 0
    if (b.state == Building::State::Buildup) return false;  // in limbo / being built
    if (!t.click_repairable) return false;             // ClickRepairable, default yes
    if (b.hp >= b.max_hp) return false;                // nothing to repair
    return true;
}

bool can_be_sold(const Building& b, const BuildingTypeDef& t, bool c4_applied) {
    // [RE: repair-sell.md §B5, FUN_00449410 + Sell_Back's C4 test]
    if (!b.alive) return false;
    if (b.state == Building::State::Dead) return false;
    if (t.unsellable) return false;                    // Unsellable=yes [§D0.3: CAOILD]
    if (b.state == Building::State::Buildup) return false;  // BState == 0 / Construction
    if (b.selling) return false;                       // already mission 0x13
    if (c4_applied) return false;                      // C4 planted
    // NO last-ConYard rule. NO captured rule. See the header comment.
    return true;
}

int mirrored_buildup_frame(const Building& b, int buildup_shp_frame_count) {
    // [RE: repair-sell.md §B2] frame = (start + count - stage) - 1, i.e. the
    // buildup range played BACKWARDS. Same image_frames/period derivation as
    // building.cpp's buildup_frame (building-render.md §5), reading sell_time.
    const int image_frames = buildup_shp_frame_count / 2;  // 2nd half = shadows
    if (image_frames < 1) return 0;
    if (b.buildup_total <= 0.0) return 0;   // no buildup art -> instant sell
    const double period = b.buildup_total / image_frames;
    int f = om::ftol(b.sell_time / period);
    f = std::clamp(f, 0, image_frames - 1);
    return image_frames - 1 - f;
}

}  // namespace ra2engine
