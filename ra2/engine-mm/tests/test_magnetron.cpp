// Rallypoint: Magnetron / Magnetic Beam. Synthetic INI only (no game data).
#include "doctest.h"
#include "magnetron.hpp"
#include "orig_math.hpp"
#include "ra2assets/ini.hpp"
#include "ra2assets/map.hpp"
#include "ra2assets/rules.hpp"
#include "world.hpp"

using namespace ra2engine;
namespace m = ra2engine::magnetron;

namespace {
const char* kSynthetic = R"ini(
[JumpjetControls]
CruiseHeight=500
Climb=5
[CombatDamage]
FallingDamageMultiplier=1.0
CurrentStrengthDamage=yes
[TELE]
Primary=MagneticBeam
Weight=1
[MTNK]
Strength=300
Armor=heavy
Weight=1
[MAGTESTV]
Weight=3
[MagneticBeam]
Damage=5000
ROF=20
Range=12
Warhead=LocomotorBeam
IsMagBeam=yes
Projectile=InvisibleHigh
[InvisibleHigh]
AA=no
AG=yes
[LocomotorBeam]
Verses=0%,0%,0%,100%,100%,100%,0%,0%,0%,100%,0%
IsLocomotor=yes
Locomotor={92612C46-F71F-11d1-AC9F-006008055BB5}
)ini";

struct SynthRules {
    ra2assets::Ini ini = ra2assets::Ini::parse(kSynthetic);
    ra2assets::Rules rules = ra2assets::Rules::load(ini);
};

UnitCombat mk_tank() {
    UnitCombat c;
    c.image = "MTNK";
    c.strength = 300;
    c.armor = "heavy";
    c.is_vehicle = true;
    c.weight_milli = 1000;
    return c;
}
}  // namespace

TEST_CASE("magnetron: Jumpjet CLSID is the LocomotorBeam loco") {
    CHECK(m::is_magnet_locomotor_clsid("{92612C46-F71F-11d1-AC9F-006008055BB5}"));
    CHECK_FALSE(m::is_magnet_locomotor_clsid("{4A582741-xxxx}"));
}

TEST_CASE("magnetron: synthetic INI IsLocomotor IsMagBeam Weight drop keys") {
    const ra2assets::Ini ini = ra2assets::Ini::parse(kSynthetic);
    CHECK(ini.get("LocomotorBeam", "IsLocomotor", "") == "yes");
    const ra2assets::Rules rules = ra2assets::Rules::load(ini);
    CHECK(rules.section_flag("LocomotorBeam", "IsLocomotor", false));
    CHECK(rules.section_flag("MagneticBeam", "IsMagBeam", false));
    CHECK(rules.section_double("MAGTESTV", "Weight", 0.0) == doctest::Approx(3.0));
    CHECK(rules.section_flag("CombatDamage", "CurrentStrengthDamage", false));
}

TEST_CASE("magnetron: Verses lift eligible vehicles; infantry immune") {
    const SynthRules synth;
    const auto verses = synth.rules.verses("LocomotorBeam");
    m::MagVictim tank;
    tank.is_vehicle = true;
    tank.armor_ix = 5;
    tank.hp = 300;
    CHECK(m::can_lift(tank, verses));
    m::MagVictim gi;
    gi.is_infantry = true;
    gi.hp = 125;
    CHECK_FALSE(m::can_lift(gi, verses));
}

TEST_CASE("magnetron: Weight slows climb; drop damage uses current HP") {
    m::MagRules r;
    CHECK(m::climb_step(r, 1000) == 5);
    CHECK(m::climb_step(r, 3000) == 1);
    m::MagVictim v;
    v.hp = 120;
    v.max_hp = 300;
    CHECK(m::drop_damage(r, v) == 120);
}

TEST_CASE("magnetron: water drop destroys non-amphibious vehicles") {
    m::MagVictim v;
    v.is_vehicle = true;
    CHECK(m::drop_destroys_in_water(v, true));
    CHECK_FALSE(m::drop_destroys_in_water(v, false));
}

TEST_CASE("magnetron: world lift / hold / drop with MagBeam profile") {
    const ra2assets::MapFile map =
        ra2assets::MapFile::parse("[Map]\nSize=0,0,20,20\nTheater=TEMPERATE\n");
    World world(map);
    AbilityProfile tele;
    tele.fire_kind = AbilityKind::MagBeam;
    world.register_ability_profile("TELE", tele);
    UnitCombat mag;
    mag.image = "TELE";
    mag.strength = 150;
    mag.is_vehicle = true;
    mag.weight_milli = 1000;
    mag.range = 12.0;
    mag.verses = {0, 0, 0, 100, 100, 100, 0, 0, 0, 100, 0};
    world.add_combat_unit(4, 4, 0, mag);
    world.add_combat_unit(6, 4, 1, mk_tank());
    world.units()[1].hp = 80;
    world.units()[0].target = 1;
    world.units()[0].ability_fired_unit = 1;
    AbilityRules ar;
    ar.mag_cruise_height = 500;
    ar.mag_climb = 100;
    ar.mag_current_strength_damage = true;
    world.set_ability_rules(ar);
    world.step_abilities(1.0 / 15.0);
    bool lifted = false;
    for (const auto& e : world.take_ability_events())
        if (e.kind == World::AbilityEvent::Kind::MagLift) lifted = true;
    CHECK(lifted);
    for (int i = 0; i < 20; ++i) {
        world.units()[0].target = 1;
        world.step_abilities(1.0 / 15.0);
        world.take_ability_events();
    }
    CHECK(world.units()[1].altitude == doctest::Approx(500.0));
    const int hp_before = world.units()[1].hp;
    world.units()[0].target = -1;
    world.step_abilities(1.0 / 15.0);
    bool dropped = false;
    for (const auto& e : world.take_ability_events())
        if (e.kind == World::AbilityEvent::Kind::MagDrop) dropped = true;
    CHECK(dropped);
    CHECK(world.units()[1].hp == 0);
    CHECK(hp_before == 80);
}

TEST_CASE("magnetron: Weight from rules into milli-units") {
    const SynthRules synth;
    const int milli = std::max(1, om::ftol(synth.rules.section_double("MAGTESTV", "Weight", 1.0) * 1000.0));
    CHECK(milli == 3000);
    CHECK(m::climb_step(m::MagRules{}, milli) == 1);
}

TEST_CASE("magnetron: drop is a Crush blast that hits the building under it [RE: 0x746209]") {
    // The Magnetron never targets the building: gamemd's landing detonates
    // CrushWarhead at the landing point, so whatever stands there is hit.
    const ra2assets::MapFile map =
        ra2assets::MapFile::parse("[Map]\nSize=0,0,20,20\nTheater=TEMPERATE\n");
    World world(map);
    BuildingTypeDef bt;
    bt.id = "GAPOWR";
    bt.fw = 2;
    bt.fh = 2;
    world.set_building_types({bt});
    const int bi = world.add_building(0, 8, 4, 2, 2, 1, 750, 0, 0.0, /*instant=*/true);
    AbilityProfile tele;
    tele.fire_kind = AbilityKind::MagBeam;
    world.register_ability_profile("TELE", tele);
    UnitCombat mag;
    mag.image = "TELE";
    mag.strength = 150;
    mag.is_vehicle = true;
    mag.weight_milli = 1000;
    mag.range = 12.0;
    mag.verses = {0, 0, 0, 100, 100, 100, 0, 0, 0, 100, 0};
    world.add_combat_unit(4, 4, 0, mag);
    world.add_combat_unit(8, 4, 1, mk_tank());   // on the plant's foundation
    world.units()[1].hp = 120;
    world.units()[0].target = 1;
    world.units()[0].ability_fired_unit = 1;
    AbilityRules ar;
    ar.mag_climb = 100;
    world.set_ability_rules(ar);
    world.step_abilities(1.0 / 15.0);
    for (int i = 0; i < 8; ++i) {
        world.units()[0].target = 1;
        world.step_abilities(1.0 / 15.0);
    }
    CHECK(world.units()[1].altitude > 0.0);
    CHECK(world.units()[0].target_building == -1);
    const int bhp = world.buildings()[static_cast<std::size_t>(bi)].hp;
    world.units()[0].target = -1;   // let go
    world.step_abilities(1.0 / 15.0);
    CHECK(world.units()[1].altitude == doctest::Approx(0.0));
    CHECK(world.units()[1].hp <= 0);                                  // 120 fall damage, Crush 100%
    CHECK(world.buildings()[static_cast<std::size_t>(bi)].hp == bhp - 120);
}

TEST_CASE("magnetron: CrushWarhead default is this install's [Crush]") {
    const WeaponSpec w = default_crush_warhead();
    CHECK(w.cell_spread == doctest::Approx(0.5));
    CHECK(w.percent_at_max == doctest::Approx(1.0));
    CHECK(w.inf_death == 2);
    for (int v : w.verses) CHECK(v == 100);
}
