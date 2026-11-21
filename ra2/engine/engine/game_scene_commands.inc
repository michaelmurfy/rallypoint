// NET L1: GameScene's command layer -- #included INSIDE the GameScene class body
// (public section). Every player action reaches the World through a Command
// (engine/command.hpp): the UI builds it, issue() stamps it for the next logic
// tick (single player: delay 0) and queues it, apply_due_commands() applies the
// due ones at the START of each logic tick in the deterministic order (frame,
// issuing house, sequence). Production and placement act on this scene's
// economy through the CommandHost below (L2 moves the economy into the World and
// swaps these bodies).
//
// Record / replay (R-184 reworked): `--record <f>` writes this scene's config
// and every applied command with its tick; `--replay <f>` rebuilds the scene
// from the config and re-applies the log tick by tick (game_scene_replay.inc).

    std::unique_ptr<CmdRecorder> cmd_rec_;   // --record
    ra2engine::CommandQueue cmdq_;
    ra2engine::CommandScheduler sched_{cmdq_, 0, 0};
    std::uint32_t sim_frame_ = 0;            // logic ticks this scene has run (the command clock)
    ra2engine::Selection sim_sel_;           // the selection the sim saw last (SELECT_NOTIFY)
    std::vector<int> notified_sel_;          // ids of the last SELECT_NOTIFY issued
    bool replaying_ = false;                 // --replay: the log feeds the queue, one tick per update
    std::uint32_t cmds_applied_ = 0;

    struct SceneCmdHost : ra2engine::CommandHost {
        GameScene* g;
        explicit SceneCmdHost(GameScene* gs) : g(gs) {}
        const ra2engine::OrderRules* order_rules() const override { return &g->input_.order_rules(); }
        void produce(int h, int rtti, int type, bool) override { g->cmd_produce(h, rtti, type); }
        void suspend(int h, int rtti, int type, bool) override { g->cmd_suspend(h, rtti, type); }
        void abandon(int h, int rtti, int type, bool) override { g->cmd_abandon(h, rtti, type, false); }
        void abandon_all(int h, int rtti, int type, bool) override { g->cmd_abandon(h, rtti, type, true); }
        void place(int h, int, int type, int cx, int cy) override { g->cmd_place(h, type, cx, cy); }
        void selection_notify(int h, const std::vector<int>& ids) override {
            if (h != g->local_house_) return;
            g->sim_sel_.clear();
            for (int i : ids) g->sim_sel_.set(i);
        }
        // W9-05-03: TXT_HAS_ALLIED / TXT_AT_WAR + the alliance EVA (every peer).
        void diplomacy(int h, int other, ra2engine::World::AllyChange ch) override { g->mp_on_diplomacy(h, other, ch); }
    };
    SceneCmdHost cmd_host_{this};

    // The UI's way in: stamp (house, frame = next tick + delay, sequence), queue.
    void issue(ra2engine::Command c) {
        if (replaying_) return;   // a replay takes its commands from the log only
        if (net_) {   // NET-PLAY: through the session (frame now + MaxAhead), back via net_gate()
            // W9-05-05 [RE: FUN_00730B00 & co. refuse an observer]: a defeated
            // player watching on issues no orders.
            if (observer_) return;
            if (!net_local_left_ && !net_end_dialog_) net_->game->issue(c);
            return;
        }
        sched_.set_house(local_house_);
        sched_.set_now(sim_frame_);
        sched_.schedule(std::move(c));
    }
    // Apply every queued command due by the current tick (tick start).
    void apply_due_commands() {
        if (cmdq_.empty()) return;
        std::vector<ra2engine::Command> due = cmdq_.take_due(sim_frame_);
        for (ra2engine::Command& c : due) {
            c.frame = sim_frame_;   // the tick it took effect on (= its stamp with delay 0)
            if (cmd_rec_) cmd_rec_->command(c);
            net_on_command(c);   // NET-PLAY: EXIT / REMOVEPLAYER -> the leaver's house
            world_.set_order_issuer(c.house);   // L2: the issuer the World's checks answer for
            ra2engine::apply_command(world_, c, &cmd_host_);
            world_.set_order_issuer(local_house_);
            ++cmds_applied_;
        }
    }
    // Event 33 "Selected by player": a campaign's triggers see the selection
    // through the command log (a replay reproduces it).
    void select_notify_tick() {
        if (!campaign_ || replaying_) return;
        input_.selection().prune(world_);
        std::vector<int> ids = input_.selection().ids();
        if (ids == notified_sel_) return;
        notified_sel_ = ids;
        ra2engine::Command c;
        c.type = ra2engine::EventType::SelectNotify;
        c.ext.assign(ids.begin(), ids.end());
        issue(std::move(c));
    }

    // --- production / placement applies (the pre-L1 cameo_click bodies) -----
    // [RE: StripClass cameo action FUN_006AA390 decides the event; HouseClass
    // Begin_Production FUN_004FA170 / FUN_004FA730 / Abandon FUN_004FCCA0 run
    // it] through the World's per-house economy (L2).
    static bool is_building_rtti(int rtti) { return rtti == static_cast<int>(ra2engine::Rtti::BuildingType); }
    ra2engine::QueueCat building_cat(int type) const {
        const bool def = type >= 0 && type < static_cast<int>(ba_.defense.size()) && ba_.defense[type] != 0;
        return def ? ra2engine::QueueCat::Defense : ra2engine::QueueCat::Structure;
    }
    ra2engine::Rtti unit_rtti(int type) const {
        if (type < 0 || type >= static_cast<int>(ur_.defs.size())) return ra2engine::Rtti::UnitType;
        const ra2engine::UnitTypeDef& d = ur_.defs[static_cast<std::size_t>(type)];
        if (d.combat.is_aircraft) return ra2engine::Rtti::AircraftType;
        return d.is_vehicle ? ra2engine::Rtti::UnitType : ra2engine::Rtti::InfantryType;
    }
    bool unit_naval(int type) const {
        return type >= 0 && type < static_cast<int>(ur_.defs.size()) && ur_.defs[static_cast<std::size_t>(type)].combat.is_naval;
    }
    void issue_type_event(ra2engine::EventType t, bool building, int type) {
        issue(ra2engine::make_type_event(t, building ? ra2engine::Rtti::BuildingType : unit_rtti(type), type,
                                         building ? false : unit_naval(type)));
    }
    // L2: the economy lives in the World (HouseEcon); each apply is the
    // World's command API, which re-validates the event (EventClass::Execute).
    // NET-PLAY: a unit type index is the ISSUING house's roster index (each
    // human house builds from its own country's roster -- house_roster), so a
    // remote player's PRODUCE resolves its queue from that house's roster.
    ra2engine::QueueCat type_cat(int rtti, int type, int house) const {
        if (is_building_rtti(rtti)) return building_cat(type);
        if (house == local_house_) return rp_cat_of_unit(type);
        const UnitRoster& r = house_roster(house);
        if (type < 0 || type >= static_cast<int>(r.defs.size())) return ra2engine::QueueCat::Infantry;
        return rp_unit_cat(r.defs[static_cast<std::size_t>(type)]);
    }
    bool type_ok(int rtti, int type, int house) const {
        if (is_building_rtti(rtti)) return type >= 0 && type < static_cast<int>(ba_.roster.size());
        const std::size_t n = house == local_house_ ? ur_.defs.size() : house_roster(house).defs.size();
        return type >= 0 && type < static_cast<int>(n);
    }
    void cmd_produce(int house, int rtti, int type) {
        if (type_ok(rtti, type, house)) world_.econ_produce(house, type_cat(rtti, type, house), type);
    }
    void cmd_suspend(int house, int rtti, int type) {
        if (!type_ok(rtti, type, house)) return;
        const ra2engine::QueueCat cat = type_cat(rtti, type, house);
        const ra2engine::HouseEcon* e = world_.econ(house);
        if (e && e->queues.producing(cat) == type) world_.econ_suspend(house, cat);
    }
    void cmd_abandon(int house, int rtti, int type, bool all) {
        if (type_ok(rtti, type, house)) world_.econ_abandon(house, type_cat(rtti, type, house), type, all);
    }
    void cmd_place(int house, int type, int cx, int cy) {
        if (type < 0 || type >= static_cast<int>(ba_.roster.size())) return;
        const ra2engine::QueueCat cat = building_cat(type);
        const ra2engine::HouseEcon* e = world_.econ(house);
        if (!e || e->queues.producing(cat) != type) return;   // nothing of this type to place
        world_.econ_place(house, cat, cx, cy);
    }

    // The complete simulation state the replay compares: the World, this
    // scene's economy (wallet, queues, backlog / hold, building states) and the
    // campaign trigger runtime. Camera, sidebar tab and the rolling credits
    // display are UI and stay out.
    // MR2: the hashed blobs, named (RA2_REC_DUMP writes them one file each).
    // place_cat is UI too: the PLACE command carries the type, the replay
    // never clicks a cameo (it made every replay after a defense placement
    // report match=no).
    std::vector<std::pair<const char*, std::vector<uint8_t>>> sim_state_blobs() const {
        std::vector<std::pair<const char*, std::vector<uint8_t>>> v;
        v.emplace_back("world", ra2engine::save::save_world(world_));
        ra2engine::save::SceneSave sc = capture_save();
        sc.cam_x = sc.cam_y = 0;
        sc.active_tab = 0;
        sc.credits_shown = 0.0;
        sc.place_cat = ra2engine::QueueCat::Structure;
        v.emplace_back("scene", ra2engine::save::to_bytes(sc));
        if (campaign_) {
            v.emplace_back("trig", ra2engine::save::save_triggers(const_cast<CampaignRuntime&>(*campaign_).trigger_system()));
            ra2engine::CampaignHostSave hs = const_cast<CampaignRuntime&>(*campaign_).capture();
            v.emplace_back("host", ra2engine::save::to_bytes(hs));
        }
        return v;
    }
    std::uint64_t sim_state_hash() const {
        std::uint64_t h = 1469598103934665603ull;
        for (const auto& [name, b] : sim_state_blobs())
            for (uint8_t c : b) { h ^= c; h *= 1099511628211ull; }
        return h;
    }
    void dump_state_blobs(const std::string& prefix) const {
        for (const auto& [name, b] : sim_state_blobs()) {
            std::ofstream o(prefix + "." + name + ".bin", std::ios::binary);
            o.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
        }
    }
    std::uint32_t sim_frame() const { return sim_frame_; }
    const ra2engine::Selection& ui_selection() const { return input_.selection(); }
    ra2engine::Selection& ui_selection_mut() { return input_.selection(); }
    // MR2: RA2_REC_CHECK=N -- --record writes a state-hash checkpoint every N
    // ticks; --replay compares each one and names the first differing tick.
    // RA2_REC_DUMP=T (+ RA2_REC_DUMP_PREFIX) writes the hashed blobs at tick T
    // in both runs, to cmp them.
    std::uint32_t rec_check_every_ = 0, rec_checked_ = 0, rec_dumped_ = 0;
    std::vector<std::pair<std::uint32_t, std::string>> replay_checks_;
    std::size_t replay_check_ix_ = 0;
    bool replay_diverged_ = false;
    static std::uint32_t rec_check_env() {
        const char* e = std::getenv("RA2_REC_CHECK");
        return e ? static_cast<std::uint32_t>(std::max(0, std::atoi(e))) : 0u;
    }
    void rec_checkpoint() {   // end of update(), before the frame's late commands
        if (const char* d = std::getenv("RA2_REC_DUMP"))
            if (static_cast<std::uint32_t>(std::atoi(d)) == sim_frame_ && sim_frame_ != rec_dumped_) {
                rec_dumped_ = sim_frame_;
                const char* pfx = std::getenv("RA2_REC_DUMP_PREFIX");
                dump_state_blobs(std::string(pfx ? pfx : "state") + (replaying_ ? ".replay" : ".record"));
            }
        if (cmd_rec_ && rec_check_every_ && sim_frame_ % rec_check_every_ == 0 && sim_frame_ != rec_checked_) {
            rec_checked_ = sim_frame_;
            cmd_rec_->check(sim_frame_, sim_state_hash());
        }
        while (replaying_ && replay_check_ix_ < replay_checks_.size() &&
               replay_checks_[replay_check_ix_].first <= sim_frame_) {
            const auto& ck = replay_checks_[replay_check_ix_++];
            if (ck.first != sim_frame_ || replay_diverged_) continue;
            char hb[32];
            std::snprintf(hb, sizeof hb, "%016llx", static_cast<unsigned long long>(sim_state_hash()));
            if (ck.second != hb) {
                replay_diverged_ = true;
                std::cerr << "[replay] first divergence at checkpoint tick " << ck.first << ": " << hb
                          << " recorded " << ck.second << '\n';
            }
        }
    }
    void begin_replay_checks(const std::vector<std::pair<std::uint32_t, std::string>>& c) { replay_checks_ = c; }
    // --replay: the log's commands go straight into the queue (each at its
    // recorded tick); update() then runs exactly one tick per call.
    void begin_replay(const std::vector<ra2engine::Command>& cmds) {
        replaying_ = true;
        input_.set_command_sink([](ra2engine::Command) {});   // no UI commands in a replay
        for (const ra2engine::Command& c : cmds) cmdq_.push(c);
    }
