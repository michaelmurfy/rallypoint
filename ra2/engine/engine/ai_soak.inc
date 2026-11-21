// ============================================================================
// --ai-soak : the REAL-PLAY AI acceptance harness [G-60..G-67].
//
// Constructs the actual GameScene (SkirmishSetupScene's Start -> GameScene(cfg)
// -> launch_skirmish -> arm_skirmish_ai) headless and drives it through
// GameScene::update at the fixed dt = 1/15 with a PASSIVE human (house 0 never
// gets an order). Reports, once per game minute, what the AI did: structures,
// credits, units fielded, teams created/executing, triggers fired, superweapon
// shots, damage dealt to the human's base. Optional AI-base damage probe (G-65)
// and an end-of-run PNG centred on the human base. NOT a byte-identity gate.
//
//   ra2engine --ai-soak [--minutes N] [--diff easy|normal|brutal]
//                       [--ai-side soviet|allied] [--map <file.map>]
//                       [--damage-ai-at MIN] [--ai-vs-ai] [--png <path>]
//                       [--cash N]   (W2-AN: lobby money, e.g. to reach tech)
// ============================================================================

#include "simdiff_dump.inc"   // --diff-dump (tools/simdiff harness)

namespace {
struct AiSoakProbe {
    static ra2engine::World& world(GameScene& g) { return g.world_; }
    static const BuildingAssets& ba(GameScene& g) { return g.ba_; }

    static int run(int argc, char** argv) {
        int minutes = 15, damage_ai_at = -1, kill_cy_at = -1, ruin_at = -1;
        double cash = -1.0;   // W2-AN: --cash N
        ra2engine::AiDifficulty diff = ra2engine::AiDifficulty::Normal;
        bool ai_allied = false, ai_vs_ai = false;
        std::string map_file = "xmp31s2.map", png;   // the --probe-realplay menu map
        // simdiff harness (docs/re/simdiff-harness.md): --diff-dump writes one
        // record per frame; the other switches only shape the scenario so it
        // matches the original run (idle human, countries, start waypoints).
        std::string diff_dump, human_country, ai_country;
        long long diff_frames = -1;
        bool no_human_deploy = false, long_game = false, ai_passive = false;
        std::uint32_t session_seed = 0;   // Net L0: --seed <n> (0 = none)
        double hash_full_us = 0, hash_crc_us = 0, hash_full_max = 0, hash_crc_max = 0;   // W9-01-04
        long long hash_n = 0;
        int players = 2;      // w9int: --players N (an 8-player sync-hash cost run)
        int hash_every = 0;   // --hash-every N prints the full sync hash (world_sync_hash) + net_frame_crc
        auto side_of = [](const std::string& c) {
            if (c == "YuriCountry") return ra2engine::World::Side::Yuri;
            if (c == "Russians" || c == "Confederation" || c == "Africans" || c == "Arabs")
                return ra2engine::World::Side::Soviet;
            return ra2engine::World::Side::Allied;
        };
        for (int i = 2; i < argc; ++i) {
            const std::string a = argv[i];
            auto next = [&]() -> std::string { return i + 1 < argc ? std::string(argv[++i]) : ""; };
            if (a == "--minutes") minutes = std::max(1, std::atoi(next().c_str()));
            else if (a == "--diff") {
                const std::string d = next();
                diff = d == "easy" ? ra2engine::AiDifficulty::Easy
                     : (d == "brutal" || d == "hard") ? ra2engine::AiDifficulty::Difficult
                     : ra2engine::AiDifficulty::Normal;
            } else if (a == "--ai-side") ai_allied = next() == "allied";
            else if (a == "--map") map_file = next();
            else if (a == "--damage-ai-at") damage_ai_at = std::atoi(next().c_str());
            else if (a == "--png") png = next();
            else if (a == "--ai-vs-ai") ai_vs_ai = true;   // house 0 is an AI too
            else if (a == "--kill-ai-conyard-at") kill_cy_at = std::atoi(next().c_str());   // W2-SK-07 probe
            else if (a == "--cash") cash = std::atof(next().c_str());
            else if (a == "--ruin-ai-at") ruin_at = std::atoi(next().c_str());   // W6-03-08 probe
            else if (a == "--diff-dump") diff_dump = next();
            else if (a == "--frames") diff_frames = std::atoll(next().c_str());
            else if (a == "--players") players = std::max(2, std::min(8, std::atoi(next().c_str())));   // w9int: N combatants (2..N-1 = AI)
            else if (a == "--hash-every") hash_every = std::max(0, std::atoi(next().c_str()));   // Net L0
            else if (a == "--seed") session_seed = static_cast<std::uint32_t>(std::strtoul(next().c_str(), nullptr, 0));   // Net L0
            else if (a == "--no-human-deploy") no_human_deploy = true;
            else if (a == "--long-game") long_game = true;   // lobby ShortGame=no
            else if (a == "--ai-passive") ai_passive = true;   // slot 1 idle (no AI): "map ticking" rung
            else if (a == "--human-country") human_country = next();
            else if (a == "--ai-country") ai_country = next();
            else if (a == "--starts") {   // comma list of [Waypoints] indices, one per combatant
                std::string v = next(); std::size_t p = 0;
                while (p <= v.size()) {
                    const std::size_t q = v.find(',', p);
                    g_simdiff_starts.push_back(std::atoi(v.substr(p, q - p).c_str()));
                    if (q == std::string::npos) break;
                    p = q + 1;
                }
            }
        }
        const std::string dir = env_install_dir();
        Ini rules_ini = load_ini_from_ra2md(dir, "rulesmd.ini");
        Rules rules = Rules::load(rules_ini);
        ra2engine::SkirmishConfig cfg = ra2engine::default_skirmish_config(rules);
        if (!map_file.empty() && map_file != "default") { cfg.map_file = map_file; cfg.use_map_starts = true; }
        cfg.slots[1].diff = diff;
        if (cash > 0.0) cfg.starting_cash = cash;
        if (ai_allied) {   // human Soviet vs Allied AI (the SK-S scenario)
            cfg.slots[0].side = ra2engine::World::Side::Soviet;  cfg.slots[0].country = "Russians";
            cfg.slots[1].side = ra2engine::World::Side::Allied;  cfg.slots[1].country = "Americans";
        }
        if (ai_vs_ai) cfg.slots[0].is_ai = true;
        for (int k = 2; k < players; ++k) {   // extra AI combatants, before the civilian slot
            ra2engine::SkirmishSlot s = cfg.slots[static_cast<std::size_t>(k % 2)];
            s.is_ai = true;
            s.diff = diff;
            s.color_ix = k;
            s.alliance_team = k;
            cfg.slots.insert(cfg.slots.begin() + k, s);
        }
        for (std::size_t k = static_cast<std::size_t>(players); k < cfg.slots.size(); ++k)
            if (players > 2) { cfg.slots[k].color_ix = static_cast<int>(k) % 8; cfg.slots[k].alliance_team = static_cast<int>(k); }
        if (long_game) cfg.short_game = false;
        if (session_seed) cfg.session_seed = session_seed;   // Net L0: the original's Seed
        if (ai_passive) cfg.slots[1].is_ai = false;
        if (!human_country.empty()) { cfg.slots[0].country = human_country; cfg.slots[0].side = side_of(human_country); }
        if (!ai_country.empty()) { cfg.slots[1].country = ai_country; cfg.slots[1].side = side_of(ai_country); }
        GameScene g(/*no_audio=*/true, cfg);
        ra2engine::World& w = world(g);
        const BuildingAssets& b_a = ba(g);
        const int ai = 1, human = 0;
        // gF G-23: the skirmish starts from an MCV; the passive human still
        // unfolds it (its one action) so the AI has a base to attack.
        for (int i = 0; i < (int)w.units().size() && !no_human_deploy; ++i)
            if (w.units()[(std::size_t)i].house == human && w.is_mcv(i)) { w.order_deploy_mcv(i); break; }
        std::FILE* ddump = diff_dump.empty() ? nullptr : std::fopen(diff_dump.c_str(), "w");
        if (ddump) simdiff::write_header(ddump, cfg.map_file, w.session_seed());   // Net L0
        auto base_hp = [&](int h) {
            long long hp = 0, mx = 0;
            for (const ra2engine::Building& b : w.buildings())
                if (b.house == h && b.alive && b.state != ra2engine::Building::State::Dead) {
                    hp += std::max(0, b.hp); mx += b.max_hp;
                }
            return std::make_pair(hp, mx);
        };
        auto hp0 = base_hp(human);   // W2-SK-27: re-based while the base grows (MCV deploy)
        long long ai_units_built = 0, first_dmg = -1, repair_frames = 0, sell_frames = 0;
        int max_alive = 0, sw_shots = 0;
        bool sw_was_ready = false;
        std::size_t prev_units = w.units().size();
        std::cout << "[ai-soak] map=" << (cfg.map_file.empty() ? "<default>" : cfg.map_file)
                  << " ai_side=" << (ai_allied ? "Allied" : "Soviet") << " diff=" << (int)diff
                  << " minutes=" << minutes << "\n";
        const long long frames = diff_frames > 0 ? diff_frames : (long long)minutes * 900;
        for (long long f = 1; f <= frames; ++f) {
            if (damage_ai_at >= 0 && f == (long long)damage_ai_at * 900) {
                long long t = 0;
                for (ra2engine::Building& b : w.buildings())
                    if (b.alive && b.house == ai && b.state == ra2engine::Building::State::Idle) {
                        b.hp = std::max(1, b.hp / 2);
                        t += b.hp;
                    }
                std::cout << "[ai-soak] probe: AI buildings halved at min " << damage_ai_at
                          << " ai_base_hp=" << t << "\n";
            }
            if (kill_cy_at >= 0 && f == (long long)kill_cy_at * 900) {   // W2-SK-07: destroy the AI ConYard(s)
                for (int bi = 0; bi < (int)w.buildings().size(); ++bi) {
                    const ra2engine::Building& b = w.buildings()[(std::size_t)bi];
                    if (b.alive && b.house == ai && b.state != ra2engine::Building::State::Dead && b.type >= 0 &&
                        b.type < (int)b_a.roster.size() && b_a.roster[b.type].conyard)
                        w.campaign_damage_building(bi, b.hp + 1);
                }
                std::cout << "[ai-soak] probe: AI ConYard destroyed at min " << kill_cy_at << "\n";
            }
            if (ruin_at >= 0 && f == (long long)ruin_at * 900) {   // W6-03-08: wreck the AI economy
                int nb = 0, nu = 0;
                for (int bi = 0; bi < (int)w.buildings().size(); ++bi) {
                    const ra2engine::Building& b = w.buildings()[(std::size_t)bi];
                    if (b.alive && b.house == ai && b.state != ra2engine::Building::State::Dead && b.type >= 0 &&
                        b.type < (int)b_a.roster.size() &&
                        (b_a.roster[b.type].id == "NAREFN" || b_a.roster[b.type].id == "GAREFN")) {
                        w.campaign_damage_building(bi, b.hp + 1);
                        ++nb;
                    }
                }
                for (int ui = 0; ui < (int)w.units().size(); ++ui) {
                    const ra2engine::Unit& u = w.units()[(std::size_t)ui];
                    if (u.alive && u.die_time < 0.0 && u.house == ai && u.is_harvester) {
                        w.campaign_damage_unit(ui, u.hp + 1);
                        ++nu;
                    }
                }
                w.ai_set_credits_for_test(ai, 300.0);
                std::cout << "[ai-soak] probe: AI economy ruined at min " << ruin_at << " refineries=" << nb
                          << " miners=" << nu << " credits=300" << std::endl;
            }
            bool ready_before = false;
            for (ra2engine::SuperweaponType t : w.sw_types(ai)) ready_before |= w.sw_ready(ai, t);
            const int seq0 = w.sw_events().empty() ? 0 : w.sw_events().back().seq;
            g.update(1.0 / 15.0);
            if (hash_every > 0 && f % hash_every == 0) {
                // W9-01-04 / W9-05: both sync hashes timed -- the debug
                // full-World hash ([Net] FullHash=1: save::world_sync_hash,
                // SyncHasher streamed under the sync-hash policy) and the
                // per-frame net_frame_crc (the FRAMEINFO CRC).
                const auto t0 = std::chrono::steady_clock::now();
                const std::uint64_t hh = ra2engine::save::world_sync_hash(w);
                const auto t1 = std::chrono::steady_clock::now();
                const std::uint32_t fc = ra2engine::net_frame_crc(w);
                const auto t2 = std::chrono::steady_clock::now();
                const double full_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
                const double crc_us = std::chrono::duration<double, std::micro>(t2 - t1).count();
                hash_full_us += full_us; hash_crc_us += crc_us; ++hash_n;
                hash_full_max = std::max(hash_full_max, full_us); hash_crc_max = std::max(hash_crc_max, crc_us);
                std::printf("[ai-soak] hash f=%lld h=%016llx crc=%08x\n", f, static_cast<unsigned long long>(hh), fc);
            }
            if (ddump) simdiff::write_frame(ddump, w, (int)(f - 1));   // frame 0 = state after the first logic step
            // W2-AN-21: every AI superweapon launch/activation (any type).
            for (const ra2engine::SwEvent& e : w.sw_events())
                if (e.seq > seq0 && e.house == ai &&
                    (e.kind == ra2engine::SwEvent::Launched || e.kind == ra2engine::SwEvent::Activated))
                    ++sw_shots;
            sw_was_ready = sw_was_ready || ready_before;
            for (std::size_t u = prev_units; u < w.units().size(); ++u)
                if (w.units()[u].house == ai) ++ai_units_built;
            prev_units = w.units().size();
            {   // W2-SK-27: the baseline is the HP of the standing base (the MCV
                // unfolds after frame 0); a drop below it is the first damage.
                const auto now = base_hp(human);
                if (first_dmg < 0 && now.second == hp0.second && now.first < hp0.first) first_dmg = f;
                if (now.second != hp0.second || now.first > hp0.first) hp0 = now;   // re-base on growth
            }
            int alive = 0;
            for (const ra2engine::Unit& u : w.units())
                if (u.alive && u.house == ai) ++alive;
            max_alive = std::max(max_alive, alive);
            for (const ra2engine::Building& b : w.buildings())
                if (b.house == ai && b.alive) { repair_frames += b.being_repaired; sell_frames += b.selling; }
            if (f % 900 == 0) {
                int nb = 0;
                std::map<std::string, int> types;
                for (const ra2engine::Building& b : w.buildings())
                    if (b.alive && b.house == ai && b.state != ra2engine::Building::State::Dead) {
                        ++nb;
                        if (b.type >= 0 && b.type < (int)b_a.roster.size()) ++types[b_a.roster[b.type].id];
                    }
                const ra2engine::AiTeamState* ts = w.ai_team_state(ai);
                const ra2engine::AiTriggerState* tr = w.ai_trigger_state(ai);
                int executing = 0;
                if (ts) for (const ra2engine::TeamInstance& t : ts->teams)
                    if (t.state == ra2engine::TeamState::Executing) ++executing;
                const auto hp = base_hp(human);
                int miners = 0, navy = 0, air = 0;
                for (const ra2engine::Unit& u : w.units()) {
                    if (u.alive && u.house == ai && u.is_harvester) ++miners;
                    if (u.alive && u.die_time < 0.0 && u.house == ai && !u.an.t.spawned) {
                        if (u.is_naval || u.movement_domain == ra2engine::MovementDomain::Amphibious) ++navy;
                        if (u.is_aircraft) ++air;
                    }
                }
                std::cout << "min " << f / 900 << ": ai_buildings=" << nb << " ai_miners=" << miners
                          << " ai_credits=" << (long long)w.ai_credits(ai) << " ai_units_alive=" << alive
                          << " ai_units_built=" << ai_units_built
                          << " teams_created=" << (ts ? ts->teams_created : 0)
                          << " teams_live=" << (ts ? (int)ts->teams.size() : 0)
                          << " executing=" << executing
                          << " triggers_fired=" << (tr ? tr->triggers_fired : 0)
                          << " sw_shots=" << sw_shots
                          << " ai_navy=" << navy << " ai_air=" << air
                          << " human_base_hp=" << hp.first << "/" << hp.second;
                {   // W2-SK economy witnesses: ore left, mine output, every house's wallet + miner states
                    int cells = 0, mined = 0;
                    for (int y = 0; y < w.ore().h(); ++y)
                        for (int x = 0; x < w.ore().w(); ++x) cells += w.ore().has_ore(x, y) ? 1 : 0;
                    for (const ra2engine::OreMine& m : w.ore().mines()) mined += m.spawned;
                    std::cout << " ore_cells=" << cells << " mines=" << w.ore().mines().size()
                              << " mine_spawned=" << mined;
                    for (int h = 0; h < w.house_count(); ++h) {
                        if (!w.house_combatant(h)) continue;
                        int st[8] = {0}, alive_h = 0, bld_h = 0;
                        for (const ra2engine::Unit& u : w.units()) {
                            if (u.alive && u.house == h && u.is_harvester) ++st[std::min(7, (int)u.harv)];
                            if (u.alive && u.die_time < 0.0 && u.house == h && !u.is_harvester) ++alive_h;
                        }
                        for (const ra2engine::Building& b : w.buildings())
                            if (b.alive && b.house == h && b.state != ra2engine::Building::State::Dead) ++bld_h;
                        std::cout << " h" << h << "{u=" << alive_h << " b=" << bld_h << " cr=" << (long long)w.credits(h)
                                  << " harv=" << st[1] << "/" << st[2] << "/" << st[3] << "/" << st[4] << "/"
                                  << st[5] << "/" << st[6] << "}";
                    }
                }
                {   // W2-SK-09/10 witnesses: special infantry, deploys, transports, captures
                    int eng = 0, spy = 0, dep = 0, aboard = 0, techs = 0;
                    for (const ra2engine::Unit& u : w.units()) {
                        if (!u.alive || u.die_time >= 0.0 || u.house != ai) continue;
                        if (u.image == "ENGINEER") ++eng;
                        if (u.image == "SPY") ++spy;
                        if (u.deployed) ++dep;
                        if (u.cargo_of >= 0) ++aboard;
                    }
                    for (const ra2engine::Building& b : w.buildings())
                        if (b.alive && b.house == ai && b.type >= 0 && b.type < (int)b_a.roster.size() &&
                            b_a.roster[b.type].produce_cash_amount > 0) ++techs;
                    std::cout << " ai_eng=" << eng << " ai_spy=" << spy << " ai_deployed=" << dep
                              << " ai_aboard=" << aboard << " ai_tech=" << techs << " ai_enemy=" << w.ai_enemy_house(ai);
                }
                std::cout << "  [";
                for (const auto& kv : types) std::cout << kv.first << "x" << kv.second << " ";
                std::cout << "]\n";
            }
        }
        const auto hp1 = base_hp(human);
        for (int ai_h = 0; ai_h < w.house_count(); ++ai_h)   // W2-SK: every AI house
        if (const ra2engine::AiTeamState* ts = w.ai_team_state(ai_h)) {   // where are the teams?
            int hx = -1, hy = -1;
            for (const ra2engine::Building& b : w.buildings())
                if (hx < 0 && b.house == human && b.alive) { hx = b.cx; hy = b.cy; }
            for (const ra2engine::TeamInstance& t : ts->teams) {
                long sx = 0, sy = 0, n = 0;
                for (int m : t.members)
                    if (m >= 0 && m < (int)w.units().size() && w.units()[m].alive) {
                        sx += w.units()[m].cell_x; sy += w.units()[m].cell_y; ++n;
                    }
                std::cout << "[ai-soak] team h" << ai_h << " tt=" << t.teamtype << " state=" << (int)t.state
                          << " pc=" << t.script_pc << " members=" << n;
                if (const ra2engine::AiHouseConfig* hc = w.ai_config(ai_h)) {
                    const ra2engine::AimdData& d = hc->team.aimd;
                    if (t.teamtype >= 0 && t.teamtype < (int)d.teams.size()) {
                        const ra2engine::ScriptType& sc = d.scripts[d.teams[t.teamtype].script];
                        std::cout << " '" << d.teams[t.teamtype].name << "'";
                        if (t.script_pc >= 0 && t.script_pc < (int)sc.actions.size())
                            std::cout << " line=" << sc.actions[t.script_pc].action << ","
                                      << sc.actions[t.script_pc].argument;
                    }
                }
                { int mv = 0; for (int m : t.members) if (m >= 0 && m < (int)w.units().size() && w.units()[m].alive && w.units()[m].moving()) ++mv;
                  std::cout << " gather=(" << t.gather_x << "," << t.gather_y << ") issued=" << t.move_issued << " moving=" << mv << " stall=" << t.stall_reissues; }
                if (n) std::cout << " at=(" << sx / n << "," << sy / n << ") human_base=(" << hx << "," << hy << ")";
                std::cout << "\n";
            }
        }
        // W2-SK economy diagnostics: every live miner at the end.
        for (int i = 0; i < (int)w.units().size(); ++i) {
            const ra2engine::Unit& u = w.units()[(std::size_t)i];
            if (!u.alive || !u.is_harvester) continue;
            std::cout << "[ai-soak] miner " << i << " h" << u.house << " at=(" << u.cell_x << "," << u.cell_y
                      << ") st=" << (int)u.harv << " bales=" << (u.bales_ore + u.bales_gems)
                      << " ref=" << u.dock_refinery << " moving=" << u.moving() << " path=" << u.path.size()
                      << " target=(" << u.harv_cell_x << "," << u.harv_cell_y << ")";
            if (u.dock_refinery >= 0 && u.dock_refinery < (int)w.buildings().size()) {
                const ra2engine::Building& b = w.buildings()[(std::size_t)u.dock_refinery];
                const int dx = ra2engine::dock_cell_x(b), dy = ra2engine::dock_cell_y(b);
                std::cout << " dock=(" << dx << "," << dy << ") dock_owner=" << w.occupancy().at(dx, dy)
                          << " pass=" << w.walkable(dx, dy);
            }
            std::cout << "\n";
        }
        const auto ai_hp = base_hp(ai);
        std::cout << "[ai-soak] ai_base_hp_end=" << ai_hp.first << "/" << ai_hp.second
                  << " repair_frames=" << repair_frames << " sell_frames=" << sell_frames << "\n";
        if (hash_n)
            std::printf("[ai-soak] hash cost: %lld samples, full world_sync_hash avg %.1f us max %.1f us; "
                        "frame crc avg %.2f us max %.2f us\n", hash_n, hash_full_us / hash_n, hash_full_max,
                        hash_crc_us / hash_n, hash_crc_max);
        std::cout << "[ai-soak] RESULT ai_units_built=" << ai_units_built
                  << " max_ai_units_alive=" << max_alive << " sw_ever_ready=" << sw_was_ready
                  << " sw_shots=" << sw_shots
                  << " human_base_hp " << hp0.first << " -> " << hp1.first
                  << " first_human_damage_min=" << (first_dmg < 0 ? -1.0 : first_dmg / 900.0)
                  << " human_defeated=" << w.house_defeated(human)
                  << " winning_team=" << w.winning_team() << "\n";
        {   // Net L0: the whole World state (save_world bytes, FNV-1a 64) -- the
            // cross-platform gate compares it (tools/xplat_gate.ps1 -Soak).
            const std::vector<std::uint8_t> sb = ra2engine::save::save_world(w);
            std::uint64_t hh = 1469598103934665603ull;
            for (std::uint8_t c : sb) { hh ^= c; hh *= 1099511628211ull; }
            char hb[40];
            std::snprintf(hb, sizeof hb, "%016llx", static_cast<unsigned long long>(hh));
            std::cout << "[ai-soak] state_hash=" << hb << " bytes=" << sb.size() << "\n";
        }
        if (!png.empty()) {
            const int W = 1280, H = 720;
            if (SDL_Init(SDL_INIT_VIDEO) == 0) {
                SDL_Window* win = SDL_CreateWindow("ai-soak", SDL_WINDOWPOS_CENTERED,
                                                   SDL_WINDOWPOS_CENTERED, W, H, SDL_WINDOW_HIDDEN);
                SDL_Renderer* ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED) : nullptr;
                if (ren) {
                    {
                        ra2engine::TextureCache tc(ren);
                        ra2engine::Camera cam = centered_camera(g.a_.map, W, H);
                        int fx = -1, fy = -1;   // the human base, else the AI base
                        for (int h : {human, ai})
                            for (const ra2engine::Building& b : w.buildings())
                                if (fx < 0 && b.house == h && b.alive) { fx = b.cx; fy = b.cy; }
                        if (fx >= 0) {
                            const ra2engine::ScreenPt p = ra2engine::cell_to_screen(fx, fy, 0, 60, 30, 15);
                            cam.x = W / 2 - p.x;
                            cam.y = H / 2 - p.y;
                        }
                        SDL_SetRenderDrawColor(ren, 10, 20, 60, 255);
                        SDL_RenderClear(ren);
                        render_world(ren, tc, g.a_, g.ua_, w, cam, W, H, nullptr, &g.vlist_, &g.ba_,
                                     &g.ore_art_);
                        capture_png(ren, png, W, H);
                        std::cout << "png=" << png << "\n";
                    }
                    SDL_DestroyRenderer(ren);
                }
                if (win) SDL_DestroyWindow(win);
                SDL_Quit();
            }
        }
        if (ddump) std::fclose(ddump);
        return 0;
    }
};
}  // namespace

int run_ai_soak(int argc, char** argv) { return AiSoakProbe::run(argc, argv); }
