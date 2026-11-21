// play_art_cheats.inc -- W3-FIX-A --play harness verbs for the art/asset
// acceptance scripts (scripts/play/art-*.play). Included INSIDE
// GameScene::cheat(); reachable only from a --play script (the Scene::cheat
// hook is driven by the play driver, never by player input).
//
//   cheat reveal                 reveal the whole map for the local player
//   cheat art_dump               log every live building type (art ok,
//                                invisible, own palette) and unit image
//   cheat view_building TYPE [n] centre the camera on the n-th live TYPE
//   cheat view_unit IMAGE [n]    centre the camera on the n-th live unit image
//   cheat view_dressing IMAGE [n] centre on the n-th civilian map-dressing object
//   cheat view_key PREFIX [n]    W4-FIX-POS: centre on the n-th map object
//                                (terrain / civ / overlay / smudge) whose key
//                                starts with PREFIX (terr:, civ:, ovl:, smudge:)
if (args.size() == 1 && args[0] == "reveal") {
    world_.reveal_all_house(0);
    return true;
}
if (args.size() == 1 && args[0] == "art_dump") {
    std::map<std::string, int> bc, uc;
    for (const ra2engine::Building& b : world_.buildings()) {
        if (!b.alive || b.type < 0 || b.type >= static_cast<int>(ba_.art.size())) continue;
        const BuildingArt& a = ba_.art[static_cast<std::size_t>(b.type)];
        const ra2engine::BuildingTypeDef& d = ba_.roster[static_cast<std::size_t>(b.type)];
        ++bc[d.id + " img=" + d.image + " h=" + std::to_string(b.house) +
             (a.ok ? " ok frames=" + std::to_string(a.shp.count()) : " NOART") + (a.invisible ? " invisible" : "") +
             (a.own_pal ? " ownpal" : "")];
    }
    for (const ra2engine::Unit& u : world_.units()) {
        if (!u.alive) continue;
        ++uc[u.image + " h=" + std::to_string(u.house) + (u.is_aircraft ? " air alt=" + std::to_string((int)u.altitude)
                                                                         : u.is_vehicle ? " veh" : " inf")];
    }
    std::map<std::string, int> dc;
    for (const PresMap::Obj& o : map_dressing_.objects())
        if (o.key.rfind("civ:", 0) == 0) ++dc[o.key.substr(4) + (o.own_pal ? " ownpal" : "")];
    for (const auto& kv : dc) std::cerr << "[art-dump] dressing " << kv.first << " x" << kv.second << "\n";
    for (const auto& kv : bc) std::cerr << "[art-dump] bld " << kv.first << " x" << kv.second << "\n";
    for (const auto& kv : uc) std::cerr << "[art-dump] unit " << kv.first << " x" << kv.second << "\n";
    return true;
}
if ((args.size() == 2 || args.size() == 3) && (args[0] == "view_building" || args[0] == "view_unit")) {
    int want = args.size() == 3 ? std::atoi(args[2].c_str()) : 0;
    if (args[0] == "view_building") {
        for (const ra2engine::Building& b : world_.buildings()) {
            if (!b.alive || b.type < 0 || b.type >= static_cast<int>(ba_.roster.size())) continue;
            if (ba_.roster[static_cast<std::size_t>(b.type)].id != args[1] || want-- > 0) continue;
            input_.center_on_cell(b.cx + b.fw / 2.0, b.cy + b.fh / 2.0);
            return true;
        }
        return false;
    }
    for (const ra2engine::Unit& u : world_.units()) {
        if (!u.alive || u.die_time >= 0.0 || u.image != args[1] || want-- > 0) continue;
        input_.center_on_cell(u.fx, u.fy);
        return true;
    }
    return false;
}
if ((args.size() == 2 || args.size() == 3) && args[0] == "view_dressing") {
    int want = args.size() == 3 ? std::atoi(args[2].c_str()) : 0;
    for (const PresMap::Obj& o : map_dressing_.objects()) {
        if (o.key != "civ:" + args[1] || want-- > 0) continue;
        input_.center_on_cell(o.x + o.fw / 2.0, o.y + o.fh / 2.0);
        return true;
    }
    return false;
}

// P2: `cheat map_ascii PATH` writes the cell grid as text for route planning
// (debug only): '.' walkable, '#' blocked (water/cliff/wall), a building's house
// number (0-9, then A-Z) on its foundation, '*' a live unit. Row y, column x.
if (args.size() == 2 && args[0] == "map_ascii") {
    const int gw = world_.grid_w(), gh = world_.grid_h();
    std::vector<std::string> g(static_cast<std::size_t>(gh), std::string(static_cast<std::size_t>(gw), '#'));
    for (int y = 0; y < gh; ++y)
        for (int x = 0; x < gw; ++x)
            g[(std::size_t)y][(std::size_t)x] = world_.walkable(x, y) ? '.' : '#';
    const char* hc = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (const ra2engine::Building& b : world_.buildings()) {
        if (!b.alive) continue;
        for (int y = b.cy; y < b.cy + b.fh; ++y)
            for (int x = b.cx; x < b.cx + b.fw; ++x)
                if (x >= 0 && y >= 0 && x < gw && y < gh) g[(std::size_t)y][(std::size_t)x] = hc[std::min(b.house, 35)];
    }
    for (const ra2engine::Unit& u : world_.units())
        if (u.alive && u.cell_x >= 0 && u.cell_y >= 0 && u.cell_x < gw && u.cell_y < gh)
            g[(std::size_t)u.cell_y][(std::size_t)u.cell_x] = '*';
    std::ofstream f(args[1]);
    for (int y = 0; y < gh; ++y) f << g[(std::size_t)y] << "\n";
    return true;
}
// R3: `cheat map_levels PATH` (debug only) -- each cell's terrain height level
// as one hex digit (0-F); row y, column x. With map_ascii it shows where an
// elevated defence gets the [ElevationModel] range bonus.
if (args.size() == 2 && args[0] == "map_levels") {
    const int gw = world_.grid_w(), gh = world_.grid_h();
    const char* hx = "0123456789ABCDEF";
    std::ofstream f(args[1]);
    for (int y = 0; y < gh; ++y) {
        std::string row(static_cast<std::size_t>(gw), '0');
        for (int x = 0; x < gw; ++x) row[(std::size_t)x] = hx[std::clamp(world_.cell_level(x, y), 0, 15)];
        f << row << "\n";
    }
    return true;
}
// `cheat map_terrain PATH` (debug only): one line per cell
// "x y level ramp speedland land water amph hbridge lowovl build playfield ore" (build: W8-03-01 static buildable, -1 unarmed;
// ore: fv8a, 1 = live ore/gems, which refuse a building on a static-buildable cell).
if (args.size() == 2 && args[0] == "map_terrain") {
    const int gw = world_.grid_w(), gh = world_.grid_h();
    const std::vector<std::uint8_t>& wg = world_.grid_for(ra2engine::MovementDomain::Water);
    const std::vector<std::uint8_t>& ag = world_.grid_for(ra2engine::MovementDomain::Amphibious);
    std::ofstream f(args[1]);
    for (int y = 0; y < gh; ++y)
        for (int x = 0; x < gw; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * gw + x;
            if (!world_.map().at(x, y)) continue;
            f << x << ' ' << y << ' ' << world_.cell_level(x, y) << ' ' << world_.ramp_at(x, y) << ' '
              << world_.speed_land_at(x, y) << ' ' << (world_.walkable(x, y) ? 1 : 0) << ' '
              << (i < wg.size() ? (int)wg[i] : -1) << ' ' << (i < ag.size() ? (int)ag[i] : -1) << ' '
              << (world_.high_bridge_cell(x, y) ? (world_.high_bridge_destroyed(x, y) ? 2 : 1) : 0) << ' '
              << world_.bridge_overlay_at(x, y) << ' '
              << (i < world_.build_static().size() ? (int)world_.build_static()[i] : -1)   // W8-03-01
              << ' ' << (world_.cell_in_playfield(x, y) ? 1 : 0)                          // LocalSize
              << ' ' << (world_.ore().has_ore(x, y) ? 1 : 0)                              // fv8a
              << '\n';
        }
    return true;
}
if ((args.size() == 2 || args.size() == 3) && args[0] == "view_key") {
    int want = args.size() == 3 ? std::atoi(args[2].c_str()) : 0;
    for (const std::vector<PresMap::Obj>* v : {&map_dressing_.objects(), &map_dressing_.overlays(), &map_dressing_.smudges()})
        for (const PresMap::Obj& o : *v) {
            if (o.key.rfind(args[1], 0) != 0 || !o.shp || want-- > 0) continue;
            input_.center_on_cell(o.x + o.fw / 2.0, o.y + o.fh / 2.0);
            std::cerr << "[view_key] " << o.key << " cell=" << o.x << "," << o.y << " level=" << o.level
                      << " canvas=" << o.shp->width() << "x" << o.shp->height() << " frame=" << o.frame << std::endl;
            return true;
        }
    return false;
}
