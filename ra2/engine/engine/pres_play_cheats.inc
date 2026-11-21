// pres_play_cheats.inc -- W5-PRES --play verbs for the presentation
// acceptance scripts (scripts/play/pres-*.play). Included INSIDE
// GameScene::cheat() after play_art_cheats.inc; reachable only from a --play
// script.
//
//   cheat deck_spawn TYPE H [n] [land]  spawn TYPE for house H on the n-th
//                                 high-bridge deck cell over water (or over
//                                 land: the unit is UNDER the deck), camera on it
//   cheat deck_layers on|off      R-100 deck layering on/off (diagnostic)
auto pres_spawn_at = [&](const std::string& id, int h, int x, int y) -> bool {
    const ra2engine::UnitTypeDef* def = nullptr;
    for (const UnitRoster* r : {&ur_, &ur_ai_})
        for (const ra2engine::UnitTypeDef& d : r->defs)
            if (!def && d.id == id) def = &d;
    ra2engine::UnitTypeDef tmp;
    if (!def) {
        tmp.id = id;
        tmp.combat = resolve_combat_unit(rules_, id).combat;
        if (tmp.combat.image.empty()) return false;
        def = &tmp;
    }
    world_.add_combat_unit(x, y, h, def->combat);
    world_.set_anim(def->combat.image,
                    ua_.anims.count(def->combat.image) ? ua_.anims.at(def->combat.image) : ra2engine::default_anim());
    return true;
};
if (args.size() >= 3 && args.size() <= 5 && args[0] == "deck_spawn") {
    int want = args.size() >= 4 ? std::atoi(args[3].c_str()) : 0;
    const bool over_land = args.size() == 5 && args[4] == "land";
    const std::vector<std::uint8_t>& wg = world_.grid_for(ra2engine::MovementDomain::Water);
    for (int di : map_dressing_.decks()) {
        const PresMap::Obj& o = map_dressing_.overlays()[static_cast<std::size_t>(di)];
        const std::size_t i = static_cast<std::size_t>(o.y) * world_.grid_w() + o.x;
        if (i >= wg.size() || (wg[i] != 0) == over_land || want-- > 0) continue;
        if (!pres_spawn_at(args[1], std::atoi(args[2].c_str()), o.x, o.y)) return false;
        input_.center_on_cell(o.x, o.y);
        std::cerr << "[deck_spawn] " << args[1] << " at deck cell " << o.x << "," << o.y << " level=" << o.level << std::endl;
        return true;
    }
    std::cerr << "[deck_spawn] decks=" << map_dressing_.decks().size() << " water-grid=" << wg.size() << std::endl;
    for (std::size_t k = 0; k < map_dressing_.decks().size() && k < 40; ++k) {
        const PresMap::Obj& o = map_dressing_.overlays()[static_cast<std::size_t>(map_dressing_.decks()[k])];
        const std::size_t i = static_cast<std::size_t>(o.y) * world_.grid_w() + o.x;
        std::cerr << "  deck " << o.key << " " << o.x << "," << o.y << " L" << o.level << " fr=" << o.frame
                  << " water=" << (i < wg.size() ? (int)wg[i] : -1) << " pass=" << (int)world_.passable()[i] << std::endl;
    }
    return false;
}
if (args.size() == 2 && args[0] == "deck_layers") {
    map_dressing_.set_layer_decks(args[1] == "on");
    return true;
}
// W5-PRES R-120 state-effect verbs (sim fields set directly -- --play only):
//   cheat pres_iron H:TYPE[:n]      Iron Curtain the unit (IronCurtainDuration)
//   cheat pres_vet H:TYPE[:n] V     set its veterancy (2.0 = elite -> promotion)
auto pres_find_unit = [&](const std::string& spec) -> int {
    std::vector<std::string> p;
    std::stringstream ss(spec);
    std::string tok;
    while (std::getline(ss, tok, ':')) p.push_back(tok);
    if (p.size() < 2) return -1;
    const int h = std::atoi(p[0].c_str());
    int want = p.size() >= 3 ? std::atoi(p[2].c_str()) : 0;
    for (int i = 0; i < static_cast<int>(world_.units().size()); ++i) {
        const ra2engine::Unit& u = world_.units()[static_cast<std::size_t>(i)];
        if (!u.alive || u.die_time >= 0.0 || u.house != h) continue;
        if (p[1] != "*" && play_unit_id(u) != p[1]) continue;
        if (want-- > 0) continue;
        return i;
    }
    return -1;
};
if (args.size() == 2 && args[0] == "pres_iron") {
    const int i = pres_find_unit(args[1]);
    if (i < 0) return false;
    world_.units()[static_cast<std::size_t>(i)].an.iron_left_frames = std::max(1, world_.sw_params().iron_frames);
    return true;
}
if (args.size() == 3 && args[0] == "pres_vet") {
    const int i = pres_find_unit(args[1]);
    if (i < 0) return false;
    world_.units()[static_cast<std::size_t>(i)].vet.veterancy = static_cast<float>(std::atof(args[2].c_str()));
    return true;
}
//   cheat poison_next_load            W8-06-01: the next saved-game load fails
//                                    after load_world (the bad-save path)
if (args.size() == 1 && args[0] == "poison_next_load") {
    g_play_poison_next_load = true;
    return true;
}
//   cheat pres_para TYPE H DX DY ALT  a paratrooper hanging at ALT leptons over
//                                    the player's first building + (DX, DY)
if (args.size() == 6 && args[0] == "pres_para") {
    int x = std::atoi(args[3].c_str()), y = std::atoi(args[4].c_str());
    for (const ra2engine::Building& b : world_.buildings())
        if (b.alive && b.house == 0) { x += b.cx; y += b.cy; break; }
    const std::size_t n0 = world_.units().size();
    if (!pres_spawn_at(args[1], std::atoi(args[2].c_str()), x, y) || world_.units().size() <= n0) return false;
    ra2engine::Unit& u = world_.units().back();
    u.cargo_of = ra2engine::kParachuteCargo;
    u.an.para_alt = std::atof(args[5].c_str());
    input_.center_on_cell(x, y);
    std::cerr << "[pres_para] " << u.image << " at " << x << "," << y << " alt " << u.an.para_alt << std::endl;
    return true;
}
