// W2-CA-19: campaign --play cheats (`cheat camp_* ...`). Textually included
// INSIDE class GameScene; every command fails (returns false) outside a
// campaign. They only skip time/combat or issue the SAME World orders a
// player's clicks issue (move / capture / attack / deploy) -- the mission's
// trigger logic still decides the outcome.
private:
    static bool camp_live(const ra2engine::Unit& u) { return u.alive && u.die_time < 0.0 && u.cargo_of < 0; }
    std::string camp_bid(const ra2engine::Building& b) const {
        return b.type >= 0 && b.type < (int)world_.btypes().size() ? world_.btypes()[(std::size_t)b.type].id : "";
    }
    int camp_find_b(int h, const std::string& id) const {
        for (int i = 0; i < (int)world_.buildings().size(); ++i) {
            const ra2engine::Building& b = world_.buildings()[(std::size_t)i];
            if (b.alive && b.state != ra2engine::Building::State::Dead && (h < 0 || b.house == h) &&
                camp_bid(b) == id)
                return i;
        }
        return -1;
    }
    bool camp_unit_is(int i, const std::string& id) const {
        return id == "*" || campaign_->unit_type(i) == id || world_.units()[(std::size_t)i].image == id;
    }

    bool campaign_cheat(const std::vector<std::string>& a) {
        if (!campaign_ || a.empty() || a[0].rfind("camp_", 0) != 0) return false;
        const std::string& c = a[0];
        auto num = [&](std::size_t k) { return k < a.size() ? std::atoi(a[k].c_str()) : 0; };
        if (c == "camp_killb" && a.size() >= 3) {   // H TYPE (H=-1 any house)
            for (int i = 0; i < (int)world_.buildings().size(); ++i) {
                const ra2engine::Building& b = world_.buildings()[(std::size_t)i];
                if (b.alive && b.state != ra2engine::Building::State::Dead && (num(1) < 0 || b.house == num(1)) &&
                    camp_bid(b) == a[2])
                    world_.campaign_damage_building(i, b.hp + 1);
            }
            return true;
        }
        if (c == "camp_killu" && a.size() >= 3) {   // H TYPE|*
            for (int i = 0; i < (int)world_.units().size(); ++i)
                if (camp_live(world_.units()[(std::size_t)i]) && world_.units()[(std::size_t)i].house == num(1) &&
                    camp_unit_is(i, a[2]))
                    world_.campaign_kill_unit(i);
            return true;
        }
        if (c == "camp_fire" && a.size() >= 2) {   // W3-FX: fire the map trigger NAME (spaces as '_' or quoted)
            std::string name = a[1];
            for (std::size_t k = 2; k < a.size(); ++k) name += " " + a[k];
            if (campaign_->force_trigger(name) > 0) return true;
            std::replace(name.begin(), name.end(), '_', ' ');
            return campaign_->force_trigger(name) > 0;
        }
        if (c == "camp_log") {   // W3-FX: echo the trigger witness lines containing SUBSTR
            const std::string sub = a.size() >= 2 ? a[1] : std::string();
            for (const std::string& l : campaign_->witness())
                if (sub.empty() || l.find(sub) != std::string::npos) std::cout << "[camp-log] " << l << "\n";
            return true;
        }
        if (c == "camp_killdef") {   // every armed enemy building (combat skip)
            for (int i = 0; i < (int)world_.buildings().size(); ++i) {
                const ra2engine::Building& b = world_.buildings()[(std::size_t)i];
                if (!b.alive || b.state == ra2engine::Building::State::Dead || b.type < 0) continue;
                if (!world_.is_enemy(local_house_, b.house) || !world_.btypes()[(std::size_t)b.type].armed) continue;
                world_.campaign_damage_building(i, b.hp + 1);
            }
            return true;
        }
        if (c == "camp_spawn" && a.size() >= 5) {   // TYPE H X Y [n]
            const int n = a.size() > 5 ? num(5) : 1;
            bool ok = false;
            for (int k = 0; k < n; ++k) ok = campaign_->spawn(a[1], num(2), num(3), num(4)) >= 0 || ok;
            return ok;
        }
        if (c == "camp_move" && a.size() >= 4) {   // UTYPE|* X Y : every controllable unit of the type
            for (int i = 0; i < (int)world_.units().size(); ++i)
                if (camp_live(world_.units()[(std::size_t)i]) && world_.controllable(world_.units()[(std::size_t)i].house) &&
                    !world_.units()[(std::size_t)i].is_naval && camp_unit_is(i, a[1]))
                    world_.order_unit_move(i, num(2), num(3));
            return true;
        }
        if ((c == "camp_capture" || c == "camp_occupy") && a.size() >= 4) {   // UTYPE BH BTYPE
            const int b = camp_find_b(num(2), a[3]);
            if (b < 0) return false;
            for (int i = 0; i < (int)world_.units().size(); ++i)
                if (camp_live(world_.units()[(std::size_t)i]) && world_.controllable(world_.units()[(std::size_t)i].house) &&
                    camp_unit_is(i, a[1])) {
                    if (c == "camp_capture") world_.order_engineer_capture(i, b);
                    else world_.order_occupy(i, b);
                    return true;
                }
            return false;
        }
        if (c == "camp_attackb" && a.size() >= 3) {   // BH BTYPE [UTYPE]
            const int b = camp_find_b(num(1), a[2]);
            if (b < 0) return false;
            for (int i = 0; i < (int)world_.units().size(); ++i)
                if (camp_live(world_.units()[(std::size_t)i]) && world_.controllable(world_.units()[(std::size_t)i].house) &&
                    camp_unit_is(i, a.size() > 3 ? a[3] : "*"))
                    world_.order_unit_attack_building(i, b);
            return true;
        }
        if (c == "camp_attacku" && a.size() >= 4) {   // UTYPE TH TTYPE : attack the first TTYPE unit of house TH
            int tu = -1;
            for (int i = 0; i < (int)world_.units().size() && tu < 0; ++i)
                if (camp_live(world_.units()[(std::size_t)i]) && world_.units()[(std::size_t)i].house == num(2) &&
                    camp_unit_is(i, a[3]))
                    tu = i;
            if (tu < 0) return false;
            for (int i = 0; i < (int)world_.units().size(); ++i)
                if (camp_live(world_.units()[(std::size_t)i]) && world_.controllable(world_.units()[(std::size_t)i].house) &&
                    camp_unit_is(i, a[1]))
                    world_.order_unit_attack(i, tu);
            return true;
        }
        if (c == "camp_deploy") {   // first controllable MCV: to the nearest deployable cell, or unfold here
            for (int i = 0; i < (int)world_.units().size(); ++i) {
                const ra2engine::Unit& u = world_.units()[(std::size_t)i];
                if (!camp_live(u) || !world_.controllable(u.house) || !world_.is_mcv(i)) continue;
                if (world_.order_deploy_mcv(i)) return true;
                for (int r = 1; r < 15; ++r)
                    for (int dy = -r; dy <= r; ++dy)
                        for (int dx = -r; dx <= r; ++dx) {
                            if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                            const int x = u.cell_x + dx, y = u.cell_y + dy;
                            if (!world_.walkable(x, y) || !world_.can_deploy_at(i, x, y)) continue;
                            if (ra2engine::find_path(world_.passable(), world_.grid_w(), world_.grid_h(),
                                                     {u.cell_x, u.cell_y}, {x, y}).size() < 2)
                                continue;
                            world_.order_unit_move(i, x, y);
                            return true;
                        }
                return false;
            }
            return false;
        }
        return false;
    }
public:
