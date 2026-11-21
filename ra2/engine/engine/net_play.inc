// ============================================================================
// NET-PLAY: one networked match (file scope; included by main.cpp before
// class GameScene, inside its anonymous namespace).
//
// mp_start_session() (mp_pages.inc) builds a NetPlay from the lobby's
// MpSessionConfig and the live MpNet (the lobby's socket / tunnel id), parks
// it in g_pending_netplay and defers a GameScene; the GameScene constructor
// takes it (game_scene_net.inc) and from then on runs in lockstep mode:
//   * local commands -> NetGame::issue (EventClass records, frame now+MaxAhead);
//   * the sim ticks only when NetGame::step() says Run, and applies exactly
//     the commands of that frame (every house, the original's order);
//   * a per-frame state hash feeds the session's FRAMEINFO CRC compare.
// When the game scene ends, the NetPlay lingers here until its EXIT has been
// flushed to the other players (FUN_00647DE0), pumped by the scene driver.
// docs/gap/net/04-engine-lockstep.md (L3-L5), 05-transport-infra.md §6.3/§7.
// ============================================================================

struct NetPlay {
    std::shared_ptr<ra2mp::MpNet> mpnet;          // the lobby's endpoint (socket / tunnel id)
    std::unique_ptr<ra2::net::NetGame> game;      // lockstep driver over mpnet->net()
    ra2mp::MpSessionConfig session;               // seed, house table, names, colours
    bool ai_takeover = false;                     // host option: leaver's house -> AI
    int hash_every = 1;                           // sync CRC period (frames; 1 = every frame, the original)
    // W9-01-04: false = the per-frame CRC (net_frame_crc, FUN_0064D520's
    // shape); true = the debug full-World hash (fnv1a over save_world, the
    // pre-W9 CRC) -- RA2.INI [Net] FullHash=1 or RA2_NET_FULL_HASH=1.
    bool full_hash = false;
    // Linger (after the scene): EXIT flushed or given up.
    std::uint64_t linger_start_ms = 0;

    const ra2mp::MpPlayer* player(int house) const {
        for (const ra2mp::MpPlayer& p : session.players)
            if (p.house == house) return &p;
        return nullptr;
    }
    std::string name_of(int house) const {
        const ra2mp::MpPlayer* p = player(house);
        if (!p) return "Player " + std::to_string(house + 1);
        if (p->is_ai) return "Computer";
        return p->name.empty() ? "Player " + std::to_string(house + 1) : p->name;
    }
    int color_of(int house) const {
        const ra2mp::MpPlayer* p = player(house);
        return p ? p->color : 0;
    }
};

// mp_start_session -> GameScene hand-off (consumed by the next GameScene).
std::shared_ptr<NetPlay> g_pending_netplay;
// The local house while the networked GameScene is being built (local_slot_of
// reads it before the scene exists).
int g_net_local_house = -1;
int g_net_local_house_value() { return g_net_local_house; }
// Matches whose scene ended, still flushing their EXIT.
std::vector<std::shared_ptr<NetPlay>> g_net_lingering;

// Called by run_scene_driver once per frame: services lingering sessions
// until poll_exit() is done (every peer passed the exit frame, or 600 ticks).
void net_linger_pump() {
    if (g_net_lingering.empty()) return;
    const std::uint64_t now = ra2mp::now_ms();
    for (auto it = g_net_lingering.begin(); it != g_net_lingering.end();) {
        NetPlay& np = **it;
        if (np.linger_start_ms == 0) np.linger_start_ms = now;
        bool done = !np.game || np.game->poll_exit();
        if (now - np.linger_start_ms > 12000) done = true;   // hard cap (600 ticks + slack)
        if (done) {
            std::cerr << "[net] session closed (exit flushed)\n";
            it = g_net_lingering.erase(it);
        } else {
            ++it;
        }
    }
}

// RA2.INI [Net] overrides for experiments: FrameSendRate / MaxAhead (start
// values; the host's TIMING adapts them) and HashEvery (sync-hash period).
int net_ini_int(const char* key, int fallback);
// W9-01-04: the debug full-World sync hash (RA2.INI [Net] FullHash=1, or the
// environment RA2_NET_FULL_HASH=1 for test runs).
bool net_full_hash_mode() {
    if (const char* e = std::getenv("RA2_NET_FULL_HASH")) return *e && *e != '0';
    return net_ini_int("FullHash", 0) > 0;
}
int net_ini_int(const char* key, int fallback) {
    const ra2engine::IniDoc doc = read_settings_doc(settings_file_path());
    if (auto v = doc.get("Net", key)) {
        const int n = std::atoi(v->c_str());
        if (n > 0) return n;
    }
    return fallback;
}
