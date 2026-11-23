    // ========================================================================
    // W7-09-05: a skirmish / multiplayer map's own [Units] / [Infantry] /
    // [Aircraft] (the cows, civilians and cars: ~1,700 objects on the 197
    // stock maps). Included in GameScene's class body; called from
    // init_map_dressing after the neutral [Structures] and the blocked
    // foundations, so a placed object never stands inside a building.
    //
    // [RE: campaign.md §4 -- the scenario reader places [Units]/[Infantry]/
    // [Aircraft] in every game mode, field order OWNER,TYPE,HEALTH,X,Y,...].
    // [RE-FALLBACK] Only the Neutral / Special / Civilian owners are placed,
    // all onto the session's civilian (first non-combatant) house -- a country
    // owner (Americans on FIGHT / RUSHHR) has no house in a skirmish session.
    // The map's start forces are already standing (launch_skirmish runs first),
    // so a map object whose cell is taken goes to the nearest free cell within
    // 3, else is dropped: start positions and the AI anchors never move.
    // Deterministic (map file order, no RNG), so every netplay peer agrees.
    // Types resolve through the campaign's camp::Types (SHPs / voxels loaded
    // once; the vehicle assets join vlist_ so the cars draw -- W7-09-16).
    // ========================================================================
    std::unique_ptr<camp::Types> sk_types_;
    int sk_place_map_units(const Ini& map_ini, int neutral) {
        if (neutral < 0) return 0;
        sk_types_ = std::make_unique<camp::Types>(dir_, ra2_, language_, ua_, rules_, art_, rules_ini_);
        int placed = 0, skipped = 0, foreign = 0;
        for (const ra2engine::MapObjectRec& o : ra2engine::parse_map_objects(map_ini)) {
            if (o.kind == 0) continue;   // [Structures]: place_structures owns them
            const std::string owner = camp::up(o.owner);
            if (owner != "NEUTRAL" && owner != "SPECIAL" && owner != "CIVILIAN") { ++foreign; continue; }
            const camp::UnitType* ut = sk_types_->unit(o.type);
            if (!ut) { ++skipped; continue; }
            const bool naval = ut->combat.is_naval || ut->combat.movement_domain == ra2engine::MovementDomain::Water;
            auto free_at = [&](int x, int y) {
                if (x < 0 || y < 0 || x >= world_.grid_w() || y >= world_.grid_h()) return false;
                if (naval) {
                    const std::vector<std::uint8_t>& wg = world_.grid_for(ra2engine::MovementDomain::Water);
                    const std::size_t i = (std::size_t)y * (std::size_t)world_.grid_w() + (std::size_t)x;
                    if (i >= wg.size() || !wg[i]) return false;
                } else if (!ut->combat.is_aircraft && !world_.walkable(x, y)) {
                    return false;
                }
                return world_.unit_at_cell(x, y) < 0 && world_.building_at_cell(x, y) < 0 &&
                       world_.occupancy().is_free(x, y);
            };
            const bool inf = !ut->combat.is_vehicle && !ut->combat.is_aircraft && !ut->combat.is_naval;
            int cx = -1, cy = -1, spot = -1;
            // W5-B R-061: a map infantry takes its own SubCell spot (up to three share a cell).
            if (inf && world_.subcells_enabled() && world_.walkable(o.x, o.y) && world_.building_at_cell(o.x, o.y) < 0)
                spot = world_.occupancy().free_spot(o.x, o.y, o.subcell);
            if (spot >= 0) {
                cx = o.x; cy = o.y;
            } else {
                for (int r = 0; r <= 3 && cx < 0; ++r)
                    for (int dy = -r; dy <= r && cx < 0; ++dy)
                        for (int dx = -r; dx <= r && cx < 0; ++dx) {
                            if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                            if (free_at(o.x + dx, o.y + dy)) { cx = o.x + dx; cy = o.y + dy; }
                        }
            }
            if (cx < 0) { ++skipped; continue; }
            ra2engine::UnitCombat c = ut->combat;
            c.veteran_rank = o.veterancy >= 200 ? 2 : o.veterancy >= 100 ? 1 : 0;
            world_.add_combat_unit(cx, cy, neutral, c);
            const int idx = (int)world_.units().size() - 1;
            if (spot >= 0) world_.place_in_spot(idx, spot);
            ra2engine::Unit& u = world_.units()[(std::size_t)idx];
            if (o.health > 0 && o.health < 256) u.hp = ra2engine::campaign_strength(u.max_hp, o.health, false);
            {   // W5-A R-033: the map Facing byte.
                const double f16 = (double)((o.facing & 0xFF) << 8);
                if (u.is_vehicle || u.is_aircraft || u.is_naval) {
                    u.body_facing = u.turret_facing = f16;
                } else {
                    constexpr double kPi = 3.14159265358979323846;
                    const double th = f16 * (2.0 * kPi / 65536.0);
                    const int b = ra2engine::facing_from_delta(ra2engine::om::sin(th), -ra2engine::om::cos(th));
                    if (b >= 0) u.facing = b;
                }
            }
            // The placed Mission= [RE: W4-SQ-05 / W6-02-11 as in the campaign]:
            // Sleep / Harmless never acquire nor return fire; Area Guard scans
            // at the doubled radius; Guard / Sticky / anything else guards.
            const int mi = ra2engine::mission_from_name(o.mission);
            if (mi == ra2engine::kMissionAreaGuard) u.tg.area_guard = true;
            if (mi == ra2engine::kMissionSleep || mi == 23 /* Harmless */) {
                u.an.t.no_passive_acquire = true;
                u.abil.no_retaliate = true;
            }
            if (ua_.anims.count(u.image)) world_.set_anim(u.image, ua_.anims.at(u.image));
            ++placed;
        }
        for (const VehicleAssets* v : sk_types_->vehicle_assets()) vlist_.push_back(v);
        std::cerr << "[map-units] skirmish map objects placed=" << placed << " skipped=" << skipped
                  << " foreign-owner=" << foreign << " (house " << neutral << ")\n";
        return placed;
    }
