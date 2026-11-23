// Rallypoint: --launch <file.ini> starts a skirmish or a network game directly,
// with no menu and no lobby (in the spirit of CnCNet's spawn.ini). Every
// machine of a network game is given the same file except [Game] LocalPlayer
// and its own tunnel id. Keys: see ra2/README.md in the Rallypoint repository.
// Included by main.cpp after mp_pages.inc.
namespace launchfile {

inline std::string low(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return mpui::trim(s);
}
inline bool flag(const Ini& ini, const char* key, bool def) {
    const std::string v = low(ini.get("Game", key, def ? "yes" : "no"));
    return v == "yes" || v == "true" || v == "1" || v == "on";
}
// Present key -> 0/1; absent -> -1 (leave to GameMode's AlliesAllowed).
inline int optional_flag(const Ini& ini, const char* key) {
    const std::string raw = ini.get("Game", key, "\x01");
    if (raw == "\x01") return -1;
    const std::string v = low(raw);
    if (v == "yes" || v == "true" || v == "1" || v == "on") return 1;
    if (v == "no" || v == "false" || v == "0" || v == "off") return 0;
    return -1;
}
inline int num(const std::string& v, int def) {
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 0);
    return (end == v.c_str() || *end) ? def : static_cast<int>(n);
}
// Present integer key -> value; absent or junk -> -1.
inline int optional_num(const Ini& ini, const char* key) {
    const std::string raw = ini.get("Game", key, "\x01");
    if (raw == "\x01") return -1;
    const int n = num(raw, -1);
    return n < 0 ? -1 : n;
}
inline int country_of(const std::string& v) {
    const std::string l = low(v);
    if (l.empty() || l == "random") return ra2mp::kRandom;
    const auto& k = gf_lobby_countries();
    for (std::size_t i = 0; i < k.size(); ++i)
        if (low(k[i]) == l) return static_cast<int>(i);
    const int n = num(l, -100);
    if (n >= 0 && n < static_cast<int>(k.size())) return n;
    throw std::runtime_error("unknown Country=" + v);
}
inline int ai_of(const std::string& v) {
    const std::string l = low(v);
    if (l == "easy") return ra2engine::kLobbyAiEasy;
    if (l == "brutal" || l == "hard") return ra2engine::kLobbyAiBrutal;
    return ra2engine::kLobbyAiMedium;
}
inline int team_of(const std::string& v) {
    const std::string l = low(v);
    if (l.size() == 1 && l[0] >= 'a' && l[0] <= 'd') return l[0] - 'a';
    const int n = num(l, -1);
    return n >= 1 && n <= 4 ? n - 1 : ra2mp::kNoTeam;   // Team=1..4 = A..D
}
// `[Tunnel] Token=` for the Rallypoint agent: 32 hex digits -> 16 bytes.
inline bool parse_token(const std::string& hex, std::uint8_t out[16]) {
    if (hex.size() != 32) return false;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < 16; ++i) {
        const int hi = nib(hex[2 * i]), lo = nib(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return true;
}

}  // namespace launchfile

int run_launch_file(const std::string& path, bool no_audio) {
    using namespace launchfile;
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read launch file " + path);
    const Ini ini = Ini::parse(std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()));

    ra2engine::Platform plat = ra2engine::Platform::create(kWindowTitle, 1280, 720);
    if (!ensure_data_files(plat)) return 0;
    const std::string dir = env_install_dir();
    Ini rules_ini = load_ini_from_ra2md(dir, "rulesmd.ini");
    Rules rules = Rules::load(rules_ini);
    ra2engine::Settings user = load_user_settings();
    g_user_settings = &user;   // sound volumes, scroll rate (never written back here)
    ra2engine::TextureCache tc(plat.renderer());

    const std::string mode = low(ini.get("Game", "Mode", "skirmish"));
    const bool network = mode == "network" || mode == "online" || mode == "lan";
    ra2mp::MpSessionConfig s;
    ra2mp::MpLobbyModel& m = s.lobby;

    // Map: a multimd.mix entry ("xmp01t4.map") or a file path.
    m.map.file = ini.get("Game", "Map", "");
    if (m.map.file.empty()) throw std::runtime_error("[Game] Map= is missing");
    {
        const auto raw = read_map_bytes(dir, mp_map_selection(m.map.file));
        if (raw.empty()) throw std::runtime_error("map not found: " + m.map.file);
        const std::string text(raw.begin(), raw.end());
        m.map.digest = ra2mp::map_digest_of(text);
        m.map.size = static_cast<std::uint32_t>(raw.size());
        m.map.official = mp_map_selection(m.map.file).mix == "multimd.mix";
        const int starts = static_cast<int>(ra2assets::MapFile::parse(text).start_waypoints().size());
        m.map.max_players = std::max(1, std::min(8, starts));
    }
    m.mode = num(ini.get("Game", "GameMode", "1"), 1);
    ra2mp::MpOptions& o = m.opt;
    o.game_speed = std::max(0, std::min(6, num(ini.get("Game", "GameSpeed", "1"), 1)));
    o.credits = std::max(0, num(ini.get("Game", "Credits", "10000"), 10000));
    o.unit_count = std::max(0, std::min(10, num(ini.get("Game", "UnitCount", "10"), 10)));
    o.tech_level = std::max(1, std::min(10, num(ini.get("Game", "TechLevel", "10"), 10)));
    o.short_game = flag(ini, "ShortGame", true);
    o.superweapons = flag(ini, "SuperWeapons", true);
    o.build_off_ally = flag(ini, "BuildOffAlly", true);
    o.mcv_repacks = flag(ini, "MCVRedeploy", true);
    o.crates = flag(ini, "Crates", true);
    o.allies_allowed = optional_flag(ini, "AlliesAllowed");
    o.auto_repair = optional_flag(ini, "AutoRepair");
    o.crate_minimum = optional_num(ini, "CrateMinimum");
    // [RP-AI-BOOST] AIBoost=0..3; absent -> -1 (resolve_ai_boost reads RA2_AI_BOOST).
    {
        const int n = optional_num(ini, "AIBoost");
        o.ai_boost = (n < 0) ? -1 : (n > 3 ? 3 : n);
    }
    o.ai_takeover = flag(ini, "AITakeover", false);

    const std::string seed_text = ini.get("Game", "Seed", "");
    if (network && seed_text.empty()) throw std::runtime_error("[Game] Seed= is required for a network game");
    s.seed = seed_text.empty() ? static_cast<std::uint32_t>(SDL_GetTicks() * 2654435761u) ^ gf_seed_of(m.map.file)
                               : static_cast<std::uint32_t>(std::strtoul(seed_text.c_str(), nullptr, 0));
    m.seed = s.seed;

    // [Player0]..[Player7]: the lobby slots. Player0 is the host (the lockstep master).
    std::vector<std::uint32_t> tunnel_ids(ra2mp::kSlots, 0);
    int first_human = -1;
    for (int i = 0; i < ra2mp::kSlots; ++i) {
        const std::string sec = "Player" + std::to_string(i);
        ra2mp::MpSlot& sl = m.slots[i];
        if (!ini.has(sec)) { sl.kind = ra2mp::SlotKind::Closed; continue; }
        const std::string type = low(ini.get(sec, "Type", "human"));
        if (type == "closed" || type == "open") { sl.kind = ra2mp::SlotKind::Closed; continue; }
        sl.kind = type == "ai" || type == "computer" ? ra2mp::SlotKind::AI : ra2mp::SlotKind::Human;
        sl.name = ra2mp::sanitize_name(ini.get(sec, "Name", sl.kind == ra2mp::SlotKind::AI ? "" : "Player"));
        sl.ai = ai_of(ini.get(sec, "AI", "medium"));
        sl.country = country_of(ini.get(sec, "Country", "random"));
        const int color = num(low(ini.get(sec, "Color", "random")), -1);
        sl.color = color >= 0 && color < 8 ? color : ra2mp::kRandom;
        const int start = num(low(ini.get(sec, "Start", "random")), 0);   // 1..8, else random
        sl.start = start >= 1 && start <= m.map.max_players ? start - 1 : ra2mp::kRandomStart;
        sl.team = team_of(ini.get(sec, "Team", "none"));
        sl.ready = true;
        sl.host = i == 0;
        sl.pid_lo = static_cast<std::uint32_t>(i + 1);
        if (sl.kind == ra2mp::SlotKind::Human) {
            if (first_human < 0) first_human = i;
            tunnel_ids[(std::size_t)i] = static_cast<std::uint32_t>(num(ini.get(sec, "TunnelId", "0"), 0));
        }
    }
    s.local_slot = num(ini.get("Game", "LocalPlayer", std::to_string(first_human)), first_human);
    if (s.local_slot < 0 || s.local_slot >= ra2mp::kSlots || m.slots[s.local_slot].kind != ra2mp::SlotKind::Human)
        throw std::runtime_error("[Game] LocalPlayer= must name a human [PlayerN]");
    s.is_host = s.local_slot == 0;
    s.options = ra2mp::encode_options(m);

    Scene* first = nullptr;
    std::shared_ptr<ra2mp::MpNet> transport;
    if (network) {
        if (m.slots[0].kind != ra2mp::SlotKind::Human) throw std::runtime_error("[Player0] must be a human (the host)");
        // [Tunnel]: a CnCNet tunnel (Version=2: u16 BE ids; 3: u32 LE ids); ids are nonzero.
        ra2mp::Addr tunnel;
        const std::string host = ini.get("Tunnel", "Address", "127.0.0.1");
        const int port = num(ini.get("Tunnel", "Port", "0"), 0);
        if (port <= 0 || port > 65535 || !ra2mp::resolve(host, static_cast<std::uint16_t>(port), tunnel))
            throw std::runtime_error("[Tunnel] Address= / Port= is not a usable address");
        const bool v2 = num(ini.get("Tunnel", "Version", "2"), 2) == 2;
        for (int i = 0; i < ra2mp::kSlots; ++i)
            if (m.slots[i].kind == ra2mp::SlotKind::Human && (tunnel_ids[(std::size_t)i] == 0 ||
                                                              (v2 && tunnel_ids[(std::size_t)i] > 0xFFFF)))
                throw std::runtime_error("[Player" + std::to_string(i) + "] needs a TunnelId=");
        std::uint8_t token[16];
        const std::uint8_t* token_ptr = nullptr;
        const std::string token_hex = ini.get("Tunnel", "Token", "");
        if (!token_hex.empty()) {
            if (!parse_token(token_hex, token))
                throw std::runtime_error("[Tunnel] Token= must be 32 hex digits");
            token_ptr = token;
        }
        transport = std::make_shared<ra2mp::MpNet>();
        if (!transport->open_tunnel(tunnel, tunnel_ids[(std::size_t)s.local_slot],
                                    v2 ? ra2::net::TunnelMode::V2 : ra2::net::TunnelMode::V3, token_ptr))
            throw std::runtime_error("cannot open the game socket");
        s.transport = ra2mp::MpTransportKind::Tunnel;
        s.host_addr = tunnel.str();
        s.local_v3 = tunnel_ids[(std::size_t)s.local_slot];
        s.host_v3 = tunnel_ids[0];
        for (int i = 0; i < ra2mp::kSlots; ++i)
            if (i != s.local_slot && m.slots[i].kind == ra2mp::SlotKind::Human)
                s.slot_peers.emplace_back(i, ra2mp::peer_of_v3(tunnel_ids[(std::size_t)i]));
        ra2mp::resolve_session(s, static_cast<int>(gf_lobby_countries().size()));
        static const MenuArt kNoArt{};
        first = mp_start_session(rules, no_audio, kNoArt, s, transport);
    } else {
        for (int i = 0; i < ra2mp::kSlots; ++i)
            if (i != s.local_slot && m.slots[i].kind == ra2mp::SlotKind::Human)
                throw std::runtime_error("a skirmish has one human player; use Mode=network for more");
        ra2mp::resolve_session(s, static_cast<int>(gf_lobby_countries().size()));
        std::cerr << ra2mp::describe_session(s);
        ra2mp::MpConvertCtx ctx;
        ctx.countries = gf_lobby_countries();
        const Rules* r = &rules;
        ctx.side_of = [r](const std::string& k) { return gf_side_of_country(*r, k); };
        ra2engine::SkirmishConfig cfg = ra2mp::session_to_skirmish(s, ctx);
        const mpui::GameMode& gm = mpui::game_mode(m.mode);
        if (gm.index != 1 && !gm.rules.empty()) cfg.mode_rules = gm.rules;
        cfg.session_seed = s.seed;
        g_net_local_house = s.local_house;   // the house this machine plays
        g_simdiff_starts.clear();            // the resolved start of each combatant
        for (const ra2mp::MpPlayer& p : s.players) g_simdiff_starts.push_back(p.start);
        first = defer_game_scene(no_audio, cfg);
    }
    // No main menu behind the game: its score page's Continue ends the process.
    const int rc = run_scene_driver(plat, tc, new BootScene(first));
    g_user_settings = nullptr;
    return rc;
}
