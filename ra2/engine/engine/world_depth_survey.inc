// G16 world depth (agent G): `--depth-survey` dev mode. Lists, per stock
// multiplayer map, the bridge overlays (by RUNTIME overlay index -- the order
// of appearance in rulesmd [OverlayTypes], docs/re/world-depth.md §B1), the
// bridge repair huts and the garrisonable (CanBeOccupied=yes) [Structures].
// Read-only; prints one line per map with any of them. Not a gate.

int run_depth_survey() {
    const std::string dir = env_install_dir();
    Ini rules_ini = load_ini_from_ra2md(dir, "rulesmd.ini");
    const std::vector<std::string> ovl = overlay_type_names(rules_ini);
    const ra2engine::MapCatalog cat = ra2engine::load_yr_map_catalog(dir, "standard");
    MixFile maps = MixFile::open_path(dir + "multimd.mix");
    auto flag = [&](const std::string& sec, const char* key) {
        const std::string v = camp::lower(rules_ini.get(sec, key));
        return !v.empty() && (v[0] == 'y' || v[0] == 't' || v[0] == '1');
    };
    for (const ra2engine::MapEntry& m : cat.entries) {
        const MixEntry* me = maps.find(m.file_name);
        if (!me) continue;
        const auto raw = maps.read(*me);
        const std::string text(raw.begin(), raw.end());
        const ra2assets::MapFile map = ra2assets::MapFile::parse(text);
        const Ini mi = Ini::parse(text);
        int lo = 0, lo_conc = 0, hi = 0;
        for (const ra2assets::MapCell& c : map.cells()) {
            const int id = map.overlay_at(c.x, c.y);
            const ra2engine::BridgeClass k = ra2engine::bridge_class_of_overlay(id);
            if (k == ra2engine::BridgeClass::LowWood) ++lo;
            else if (k == ra2engine::BridgeClass::LowConcrete) ++lo_conc;
            else if (k == ra2engine::BridgeClass::HighHead) ++hi;
        }
        int huts = 0, garrison = 0;
        std::string gtypes;
        for (const auto& kv : mi.section("Structures")) {
            std::vector<std::string> f;
            std::stringstream ss(kv.second);
            for (std::string t; std::getline(ss, t, ',');) f.push_back(t);
            if (f.size() < 5) continue;
            const std::string type = camp::up(f[1]);
            if (flag(type, "BridgeRepairHut")) ++huts;
            if (flag(type, "CanBeOccupied")) {
                ++garrison;
                if (gtypes.size() < 60) gtypes += type + "@" + f[3] + "," + f[4] + " ";
            }
        }
        if (lo + lo_conc + hi + huts + garrison == 0) continue;
        std::cout << "depth-survey " << m.file_name << " \"" << m.display_name << "\" lowbridge=" << lo
                  << " lowconcrete=" << lo_conc << " highhead=" << hi << " huts=" << huts
                  << " garrisonable=" << garrison << " " << gtypes << "\n";
    }
    (void)ovl;
    return 0;
}

// `--depth-dump <map> x0 y0 x1 y1`: the runtime overlay index (and overlay
// data) of every cell in the rectangle (".." = none). Dev aid.
int run_depth_dump(const std::string& mapname, int x0, int y0, int x1, int y1) {
    const std::string dir = env_install_dir();
    MixFile maps = MixFile::open_path(dir + "multimd.mix");
    const MixEntry* me = maps.find(mapname);
    if (!me) { std::cerr << "no map " << mapname << "\n"; return 1; }
    const auto raw = maps.read(*me);
    const ra2assets::MapFile map = ra2assets::MapFile::parse(std::string(raw.begin(), raw.end()));
    for (int y = y0; y <= y1; ++y) {
        std::cout << (y < 100 ? " " : "") << y << ": ";
        for (int x = x0; x <= x1; ++x) {
            const int id = map.overlay_at(x, y);
            const ra2assets::MapCell* c = map.at(x, y);
            if (id < 0 || id == 255) std::cout << (c ? "  .." : "    ");
            else std::cout << " " << (id < 100 ? " " : "") << id;
        }
        std::cout << "\n";
    }
    return 0;
}
