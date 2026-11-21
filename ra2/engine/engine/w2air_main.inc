// w2air_main.inc -- wave-2 air/naval/SW (W2-AN-*) rules resolution, #included
// by main.cpp right before resolve_combat_unit (needs speed_to_cps/to_lower).

inline std::array<int, 11> w2_verses11(const Rules& rules, const std::string& warhead) {
    std::array<int, 11> a{};
    a.fill(100);
    const std::vector<int> v = rules.verses(warhead);
    for (std::size_t i = 0; i < v.size() && i < a.size(); ++i) a[i] = v[i];
    return a;
}

// The per-type naval-targeting, submarine, sonar, spawner, fixed-wing and
// missile keys [rules.ini; TechnoTypeClass offsets in engine/air_naval.hpp].
// Every key absent -> the inert AnType defaults.
void resolve_an_type(const Rules& rules, const std::string& unit_id, const std::string& primary,
                     const std::string& secondary, ResolvedUnit& r) {
    ra2engine::AnType& a = r.combat.an;
    a.naval_targeting = static_cast<int>(rules.section_double(unit_id, "NavalTargeting", 0.0));
    a.land_targeting = static_cast<int>(rules.section_double(unit_id, "LandTargeting", 0.0));
    a.no_passive_acquire = !rules.section_flag(unit_id, "CanPassiveAquire", true);
    // InfantryTypes are organic by type (the Chronosphere and the Iron Curtain
    // kill infantry); vehicles only with Organic=yes (DLPH, SQD).
    a.organic = rules.section_flag(unit_id, "Organic", rules.is_infantry(unit_id));
    a.teleporter = rules.section_flag(unit_id, "Teleporter", false);
    a.underwater = rules.section_flag(unit_id, "Underwater", false);
    a.cloakable = rules.section_flag(unit_id, "Cloakable", false);
    a.sensors = rules.section_flag(unit_id, "Sensors", false);
    a.sensors_sight = static_cast<int>(rules.section_double(unit_id, "SensorsSight", 0.0));
    a.crashable = rules.section_flag(unit_id, "Crashable", false);
    a.weight = rules.section_double(unit_id, "Weight", 1.0);
    a.landable = rules.section_flag(unit_id, "Landable", false);
    a.min_range = rules.section_double(primary, "MinimumRange", 0.0);
    a.cell_rangefinding = !primary.empty() && rules.section_flag(primary, "CellRangefinding", false);   // W7-05-11
    a.spawned = rules.section_flag(unit_id, "Spawned", false);
    a.missile = rules.section_flag(unit_id, "MissileSpawn", false);
    // Spawner weapon slot (Spawner=yes) + the Spawns= complement.
    if (!primary.empty() && rules.section_flag(primary, "Spawner", false)) a.spawner_slot = 0;
    else if (!secondary.empty() && rules.section_flag(secondary, "Spawner", false)) a.spawner_slot = 1;
    if (a.spawner_slot >= 0) {
        a.spawns = rules.section_string(unit_id, "Spawns", "");
        a.spawns_number = static_cast<int>(rules.section_double(unit_id, "SpawnsNumber", 1.0));
        a.spawn_regen_frames = static_cast<int>(rules.section_double(unit_id, "SpawnRegenRate", 0.0));
        a.spawn_reload_frames = static_cast<int>(rules.section_double(unit_id, "SpawnReloadRate", 0.0));
        if (a.spawns.empty()) a.spawner_slot = -1;   // V3 tank etc: [General] V3RocketType
    }
    // [ASWLauncher] Range=-2 ";infinite": fv7c (13a #2) keeps the RAW -2
    // (-0x200 leptons). FUN_006F6750 starts with `if (Range == -0x200) return
    // true` (range_infinite), while the threat scan FUN_006F8320 takes
    // max(GetWeaponRange(1), GetWeaponRange(0)) of the raw values, so -2 loses
    // to the 155mm's 8 (DEST scans 9 cells, not the 31 a 30-cell stand-in gave).
    // [V3Launcher]/[DredLauncher] spawn the [General] rocket types.
    if (a.spawner_slot < 0 && !primary.empty() && rules.section_flag(primary, "Spawner", false)) {
        a.spawner_slot = 0;
        a.spawns = to_lower(primary).find("dred") != std::string::npos
                       ? rules.section_string("General", "DMislType", "DMISL")
                       : rules.section_string("General", "V3RocketType", "V3ROCKET");
        a.spawns_number = std::max(1, static_cast<int>(rules.section_double(unit_id, "SpawnsNumber", 1.0)));
        a.spawn_regen_frames = static_cast<int>(rules.section_double(unit_id, "SpawnRegenRate", 0.0));
    }
    // --- wave 7 ---
    // W7-05-04 [RE: Enter_Idle_Mode FUN_004176F0 reads AircraftType +0xE0D].
    a.airport_bound = rules.section_flag(unit_id, "AirportBound", false);
    // W7-05-09 [RE: FireDeathWeapon FUN_0070CAC0]: DeathWeapon (+0xD18), else
    // the primary weapon (vtbl+0x3F4); Damage x DeathWeaponDamageModifier
    // (+0xD1C, ftol @0x70CB1B); fired as a bullet of the weapon's warhead and
    // detonated at once (FUN_0046AF70 -> BulletClass::Detonate FUN_004690A0).
    {
        const std::string dw = rules.section_string(unit_id, "DeathWeapon", "");
        const std::string w = dw.empty() ? primary : dw;
        if (!w.empty() && !rules.weapon_warhead(w).empty()) {
            ra2engine::WeaponSpec& c = a.crash_weapon;
            const std::string wh = rules.weapon_warhead(w);
            c.valid = true;
            c.damage = static_cast<int>(rules.weapon_damage(w) *
                                        rules.section_double(unit_id, "DeathWeaponDamageModifier", 1.0));
            c.range = rules.weapon_range(w);
            c.verses = rules.verses(wh);
            c.cell_spread = rules.section_double(wh, "CellSpread", 0.0);
            c.percent_at_max = rules.section_double(wh, "PercentAtMax", 1.0);
            c.affects_allies = rules.section_flag(wh, "AffectsAllies", true);
            c.prone_damage = ra2engine::warhead_prone_damage(rules, wh);
            c.inf_death = static_cast<int>(rules.section_double(wh, "InfDeath", 0.0));
            c.wall_flags = ra2engine::warhead_wall_flags(rules, wh);
        }
    }
    // W7-06-03 [RE: SpawnManager status 0 -> InRange(target, SelectWeapon)].
    if (a.spawner_slot >= 0) {
        const std::string sw = a.spawner_slot == 1 ? secondary : primary;
        a.spawner_range = sw.empty() ? 0.0 : rules.weapon_range(sw);
        // Range=-2 "infinite" (ASWLauncher) stays raw: range_infinite (fv7c).
    }
    // W7-06-10 [RE: WeaponType IsSonic +0x130 (@0x770EF4), AmbientDamage +0x98
    // (@0x770EDC); the wave reads GetWeapon(0) of the firer, FUN_0075E150].
    if (!primary.empty() && rules.section_flag(primary, "IsSonic", false))
        a.sonic_ambient = static_cast<int>(rules.section_double(primary, "AmbientDamage", 0.0));
    {
        const std::string ep = rules.section_string(unit_id, "ElitePrimary", "");
        if (!ep.empty() && rules.section_flag(ep, "IsSonic", false))
            a.sonic_ambient_elite = static_cast<int>(rules.section_double(ep, "AmbientDamage", 0.0));
        else
            a.sonic_ambient_elite = a.sonic_ambient;
    }
    // W7-06-11 [RE: TechnoType DistributedFire +0x6B0 (@0x713BF0),
    // RadialFireSegments +0x6A4 (@0x713B54)].
    a.distributed_fire = rules.section_flag(unit_id, "DistributedFire", false);
    a.radial_segments = static_cast<int>(rules.section_double(unit_id, "RadialFireSegments", 0.0));
    // W7-05-13 [RE: ObjectType RadarInvisible +0x22F (@0x5F8C6F); TechnoType
    // HoverAttack +0x390 (FUN_00711510 @0x7118FA)].
    a.radar_invisible = rules.section_flag(unit_id, "RadarInvisible", false);
    a.hover_attack = rules.section_flag(unit_id, "HoverAttack", false);
    const std::string loco = to_lower(rules.locomotor(unit_id));
    // FlyLocomotion {4A582746} (ORCA/BEAG/HORNET/ASW/PDPLANE) [roster2.md §C].
    static const std::string kFly = "{4a582746-9839-11d1-b709-00a024ddafd1}";
    // RocketLocomotion {B7B49766} (V3ROCKET/DMISL).
    static const std::string kRocket = "{b7b49766-e576-11d3-9bd9-00104b972fe8}";
    if (loco == kFly || a.missile || loco == kRocket) {
        r.combat.is_aircraft = true;
        r.combat.is_vehicle = true;
        // W7-05-14 [RE: FlyLocomotion Apparent_Speed FUN_004CFD40 =
        // ftol(TechnoType+0x678 x CurrentSpeed), +0x678 = min(255,
        // Speed*256/100)]: full throttle is the RAW lepton speed per frame, no
        // GameSpeedBias (ORCA Speed=14 -> 35 leptons/frame).
        a.fly_speed_cps = ra2engine::fly_full_speed_lpf(r.speed) * 15.0 / 256.0;
        r.combat.flight_speed = a.fly_speed_cps;
        // W7-05-02 [RE: AircraftClass FUN_00413F80 -> FacingClass::Set_ROT
        // FUN_004C95A0(type+0x71C ROT) = min(ROT,127)<<8 u16/frame]: ROT, not x2.
        r.combat.flight_turn = std::min(127, std::max(0, rules.rot(unit_id))) * 256.0 * 15.0;
        // W7-05-02 [RE: Is_Strafe FUN_0041B820 / Is_Fighter FUN_0041B870,
        // SlowdownDistance TechnoType +0x2F8 (default 500)].
        {
            const std::string proj = rules.section_string(primary, "Projectile", "");
            a.strafe = !primary.empty() && !proj.empty() &&
                       rules.section_double(proj, "ROT", 0.0) <= 1.0 &&
                       !rules.section_flag(proj, "Inviso", false);
            a.fighter = rules.section_flag(unit_id, "Fighter", false);
            a.slowdown_distance = static_cast<int>(rules.section_double(unit_id, "SlowdownDistance", 500.0));
        }
        r.combat.cruise_altitude = rules.general_double("FlightLevel", 1500.0);
        r.combat.climb_rate = 1500.0;
        r.combat.descend_rate = 1500.0;
        r.combat.omni_fire = true;
        if (loco == kFly && !a.missile) {
            a.fixed_wing = true;
            a.ammo = static_cast<int>(rules.section_double(unit_id, "Ammo", 1.0));
        }
        if (a.missile || loco == kRocket) {
            a.missile = true;
            // W4 [FUN_00661D00 head]: the rocket's OWN type picks its block --
            // V3RocketType -> V3, CMislType -> CMisl, anything else DMisl.
            const ra2engine::RocketParams rp = ra2engine::resolve_rocket_params(rules);
            a.rocket_kind = ra2engine::rocket_kind_for(rp, unit_id);
            a.missile_damage = rp.block[static_cast<std::size_t>(a.rocket_kind)].damage;
            // type+0x678 = min(255, clamp(Speed,0,100)*256/100) (voxel-render.md 5).
            a.rocket_speed = std::min(255, std::max(0, std::min(100, r.speed)) * 256 / 100);
        }
        a.no_shadow = rules.section_flag(unit_id, "NoShadow", false);
    }
}

// The [General] superweapon / aircraft / naval values for World::set_sw_params.
ra2engine::SwParams resolve_sw_params(const Rules& rules) {
    ra2engine::SwParams p;
    auto gd = [&](const char* k, double d) { return rules.general_double(k, d); };
    auto gs = [&](const char* k, const char* d) {
        std::string v = rules.section_string("General", k, d);
        const std::size_t c = v.find(',');
        if (c != std::string::npos) v = v.substr(0, c);
        return v;
    };
    p.iron_frames = static_cast<int>(gd("IronCurtainDuration", 750.0));
    p.chrono_delay_frames = static_cast<int>(gd("ChronoDelay", 60.0));
    p.storm_deferment = static_cast<int>(gd("LightningDeferment", 250.0));
    p.storm_damage = static_cast<int>(gd("LightningDamage", 250.0));
    p.storm_duration = static_cast<int>(gd("LightningStormDuration", 180.0));
    p.storm_hit_delay = static_cast<int>(gd("LightningHitDelay", 10.0));
    p.storm_scatter_delay = static_cast<int>(gd("LightningScatterDelay", 5.0));
    p.storm_cell_spread = static_cast<int>(gd("LightningCellSpread", 10.0));
    p.storm_separation = static_cast<int>(gd("LightningSeparation", 3.0));
    const std::string swh = gs("LightningWarhead", "IonWH");
    p.storm_wh_spread = rules.section_double(swh, "CellSpread", 2.0);
    p.storm_wh_pmax = rules.section_double(swh, "PercentAtMax", 0.5);
    p.storm_verses = w2_verses11(rules, swh);
    p.storm_wall_flags = ra2engine::warhead_wall_flags(rules, swh);   // W6-04-04 (IonWH Wall/Wood)
    p.amer_para_inf = gs("AmerParaDropInf", "E1");
    p.amer_para_num = static_cast<int>(gd("AmerParaDropNum", 8.0));
    p.ally_para_inf = gs("AllyParaDropInf", "E1");
    p.ally_para_num = static_cast<int>(gd("AllyParaDropNum", 6.0));
    p.sov_para_inf = gs("SovParaDropInf", "E2");
    p.sov_para_num = static_cast<int>(gd("SovParaDropNum", 9.0));
    p.paradrop_radius = static_cast<int>(gd("ParadropRadius", 1024.0));
    p.parachute_fall = std::max(1, static_cast<int>(std::lround(-gd("ParachuteMaxFallRate", -5.0))));
    p.cloak_delay_frames = std::max(1, static_cast<int>(gd("CloakDelay", 0.02) * 900.0));
    p.ship_sinking_weight = gd("ShipSinkingWeight", 3.0);
    p.reload_frames = std::max(1, static_cast<int>(gd("ReloadRate", 0.3) * 900.0));
    p.flight_level = static_cast<int>(gd("FlightLevel", 1500.0));
    // W4 [FUN_00662A70]: every rocket type explodes with its OWN warhead.
    p.rockets = ra2engine::resolve_rocket_params(rules);
    p.repair_step = static_cast<int>(gd("RepairStep", 7.0));
    p.repair_rate_frames = std::max(1, static_cast<int>(gd("RepairRate", 0.016) * 900.0));
    p.repair_percent = gd("RepairPercent", 0.15);
    {   // W7-05-09 [RE: Rules+0xFA8 = [CombatDamage] C4Warhead (Super)].
        const std::string c4 = rules.section_string("CombatDamage", "C4Warhead", "Super");
        const std::vector<int> v = rules.verses(c4);
        if (!v.empty()) p.c4_verses = v;
        p.c4_spread = rules.section_double(c4, "CellSpread", 0.0);
        p.c4_pmax = rules.section_double(c4, "PercentAtMax", 1.0);
    }
    const std::string payload = "NukePayload";
    p.nuke_rad_level = static_cast<int>(rules.section_double(payload, "RadLevel", 500.0));
    p.nuke_wall_flags = ra2engine::warhead_wall_flags(   // W6-04-04: [NUKE] WallAbsoluteDestroyer
        rules, rules.section_string(payload, "Warhead", "NUKE"));
    return p;
}
