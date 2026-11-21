// W5-B (wave-5 agent B, combat and movement core): arms the real game's World
// with the RE'd movement/combat rules. Called from the GameScene ctor / load
// path and the save-load gate World (never a unit-test World, which keeps the
// pre-W5 one-unit-per-cell model unless a test opts in).
//   R-061 infantry sub-cells (three infantry per cell, FUN_00481090).
//   R-062 land speed: [LandType] Foot..FloatBeach (GroundType FUN_00673A40),
//         TMP terrain-type -> LandType (DAT_008278F4), slope and damage
//         modifiers (DriveLocomotionClass FUN_004B2550).
//   R-050 crush steering: [CombatDamage] Crush / PlayerAutoCrush (FUN_00742720).

// "75%" -> 0.75, ".5" -> 0.5; absent -> fallback.
inline double w5_rules_fraction(const Rules& rules, const std::string& sec, const std::string& key,
                                double fallback) {
    std::string v = rules.section_string(sec, key, "");
    const std::size_t sc = v.find(';');
    if (sc != std::string::npos) v = v.substr(0, sc);
    if (v.find_first_not_of(" \t") == std::string::npos) return fallback;
    const double d = std::strtod(v.c_str(), nullptr);
    return v.find('%') != std::string::npos ? d / 100.0 : d;
}

// TMP image header byte 41 (terrain type) -> LandType: CellClass's land
// recalculation FUN_0047D1C0 -> FUN_00544910 -> table DAT_008278F4.
inline int w5_land_of_terrain_type(int tt) {
    static const int kTab[16] = {0, 8, 8, 8, 8, 10, 9, 3, 3, 2, 6, 1, 1, 0, 7, 3};
    return kTab[tt & 15];
}

void w5_arm_combat(ra2engine::World& world, const Rules& rules, const ra2assets::MapFile* map = nullptr,
                   ra2engine::Theater* theater = nullptr) {
    const char* off_env = std::getenv("W5_OFF");   // diagnostics: "sub,land,crush,elev"
    const std::string off = off_env ? off_env : "";
    if (off.find("sub") == std::string::npos) world.enable_subcells();
    // --- R-062 land speed ---------------------------------------------------
    ra2engine::World::LandSpeedRules r;
    static const char* kLand[12] = {"Clear", "Road", "Water", "Rock", "Wall", "Tiberium",
                                    "Beach", "Rough", "Ice", "Railroad", "Tunnel", "Weeds"};
    static const char* kSpeed[8] = {"Foot", "Track", "Wheel", "Hover", "", "Float", "Amphibious", "FloatBeach"};
    for (int l = 0; l < 12; ++l)
        for (int s = 0; s < 8; ++s) {
            double v = (s == 4) ? 1.0 : w5_rules_fraction(rules, kLand[l], kSpeed[s], 1.0);   // Winged = 1.0
            if (v > 1.0) v = 1.0;   // FUN_00673A40 caps every column at 1.0
            r.cost[l][s] = static_cast<float>(v);
        }
    r.tracked_up = rules.general_double("TrackedUphill", 1.0);
    r.tracked_down = rules.general_double("TrackedDownhill", 1.0);
    r.wheeled_up = rules.general_double("WheeledUphill", 1.0);
    r.wheeled_down = rules.general_double("WheeledDownhill", 1.0);
    r.condition_red = w5_rules_fraction(rules, "AudioVisual", "ConditionRed", 0.25);
    const int gw = world.grid_w(), gh = world.grid_h();
    std::vector<std::uint8_t> land(static_cast<std::size_t>(gw) * gh, 0), ramp(land.size(), 0);
    if (map && theater) {
        for (const ra2assets::MapCell& c : map->cells()) {
            if (c.x < 0 || c.y < 0 || c.x >= gw || c.y >= gh) continue;
            const std::size_t i = static_cast<std::size_t>(c.y) * gw + c.x;
            const ra2assets::TmpFile* t = theater->tmp_for_tile(c.tile);
            if (!t || c.subtile >= t->count()) continue;
            const std::uint8_t* hdr = nullptr;
            try { hdr = t->image_header(c.subtile); } catch (const std::exception&) { hdr = nullptr; }
            if (!hdr) continue;
            land[i] = static_cast<std::uint8_t>(w5_land_of_terrain_type(hdr[41]));
            ramp[i] = hdr[42];
        }
    }
    if (off.find("land") == std::string::npos) world.arm_land_speed(land, ramp, r);
    // --- W7 locomotion rules [RE: Rules+0x1760 PathDelay (minutes, x900 ->
    // frames, disasm 0x4D3F42), +0x1768 BlockagePathDelay (frames), +0x1718
    // CloseEnough (cells -> leptons), +0x1700 ConditionYellow, +0x5E0/+0x5E8/
    // +0x5D8 HoverAcceleration / HoverBrake / HoverBoost] -----------------------
    {
        ra2engine::LocoRules lr;
        lr.path_delay_frames = static_cast<int>(rules.section_double("AI", "PathDelay", 0.01) * 900.0);
        lr.blockage_path_delay = static_cast<int>(rules.section_double("AI", "BlockagePathDelay", 60.0));
        lr.close_enough = static_cast<int>(rules.general_double("CloseEnough", 2.25) * 256.0);
        lr.condition_yellow = w5_rules_fraction(rules, "AudioVisual", "ConditionYellow", 0.5);
        lr.hover_accel = rules.general_double("HoverAcceleration", 0.02);
        lr.hover_brake = rules.general_double("HoverBrake", 0.03);
        lr.hover_boost = w5_rules_fraction(rules, "General", "HoverBoost", 1.5);
        world.arm_loco_rules(lr);
    }
    // --- R-050 crush steering ------------------------------------------------
    ra2engine::World::CrushRules cr;
    cr.armed = true;
    // [CombatDamage] Crush= cells -> leptons (FUN_00474550: ftol(cells*256)).
    cr.crush_leptons = static_cast<int>(rules.section_double("CombatDamage", "Crush", 1.8) * 256.0);
    cr.player_auto_crush = rules.section_flag("CombatDamage", "PlayerAutoCrush", false);
    cr.ai_any_building = off.find("threat") == std::string::npos;   // R-064
    if (off.find("crush") == std::string::npos) world.arm_crush(cr);
    // --- R-063 3-D range: [ElevationModel] -----------------------------------
    ra2engine::World::ElevationRules er;
    er.armed = true;
    er.increment = static_cast<int>(rules.section_double("ElevationModel", "ElevationIncrement", 4.0));
    if (er.increment < 1) er.increment = 1;
    er.bonus = rules.section_double("ElevationModel", "ElevationIncrementBonus", 2.0);
    er.cap = rules.section_double("ElevationModel", "ElevationBonusCap", 2.0);
    if (off.find("elev") == std::string::npos) world.arm_elevation(er);
    // --- w7fix-combat: the wave-7 shared-combat rules ------------------------
    ra2engine::World::CombatW7Rules w7;
    w7.armed = off.find("w7") == std::string::npos;
    w7.player_return_fire = rules.section_flag("General", "PlayerReturnFire", false) ||
                            rules.section_flag("CombatDamage", "PlayerReturnFire", false);
    w7.player_scatter = rules.section_flag("General", "PlayerScatter", false) ||
                        rules.section_flag("CombatDamage", "PlayerScatter", false);
    w7.iq_guard_area = static_cast<int>(rules.section_double("IQ", "GuardArea", 2.0));
    w7.iq_auto_crush = static_cast<int>(rules.section_double("IQ", "AutoCrush", 2.0));
    w7.iq_scatter = static_cast<int>(rules.section_double("IQ", "Scatter", 2.0));
    w7.max_iq = static_cast<int>(rules.section_double("IQ", "MaxIQLevels", 5.0));
    w7.close_enough = rules.general_double("CloseEnough", 2.25);
    w7.normal_targeting_delay = static_cast<int>(rules.general_double("NormalTargetingDelay", 27.0));
    w7.guard_area_targeting_delay = static_cast<int>(rules.general_double("GuardAreaTargetingDelay", 36.0));
    w7.incoming = static_cast<int>(rules.section_double("CombatDamage", "Incoming", 10.0));
    w7.my_eff = rules.general_double("MyEffectivenessCoefficientDefault", 200.0);
    w7.tgt_eff = rules.general_double("TargetEffectivenessCoefficientDefault", -200.0);
    w7.tgt_special = rules.general_double("TargetSpecialThreatCoefficientDefault", 200.0);
    w7.tgt_strength = rules.general_double("TargetStrengthCoefficientDefault", -200.0);
    w7.tgt_distance = rules.general_double("TargetDistanceCoefficientDefault", -10.0);
    w7.enemy_house_bonus = rules.general_double("EnemyHouseThreatBonus", 400.0);
    // fv7c [RE: FUN_00707500 budget Rules+0xB18 / cooldown Rules+0x14D8 x 900.0]
    w7.call_for_help_budget = static_cast<double>(static_cast<int>(rules.section_double("AI", "ComputerBaseDefenseResponse", 3.0)));
    w7.base_defense_delay = static_cast<int>(rules.general_double("BaseDefenseDelay", 0.25) * 900.0);   // ftol (truncation)
    w7.iq_repair_sell = static_cast<int>(rules.section_double("IQ", "RepairSell", 1.0));       // fv7c
    w7.ai_credit_reserve = rules.section_double("AI", "CreditReserve", 100.0);                // fv7c
    world.arm_combat_w7(w7);
}
