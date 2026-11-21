// pres_bldanim.inc -- W3-B building animation slots, driven per LOGIC tick.
// Included by main.cpp right after pres_fx.inc (draw_building / PresMap call
// the overlay hooks; Presentation::logic_tick runs pres_bld_anim_tick).
// Purely presentational: it observes World state and never writes it.
//
// Original [RE: BuildingClass::PlayAnim FUN_00451880 (slot anim = an AnimClass
// timed by the anim's own [Anim] Rate/Loop keys), UpdateAnimations
// FUN_004509c0, the slot-anim-finished callback FUN_00451b30, Shape_Number
// FUN_0043ef10, GetRubbleImage FUN_0045f150; slot table in pres_fx.inc]:
//  - Active..ActiveFour loop from the grand opening; a Refinery=yes type
//    shows ONE of them by its storage level (stored*4/Storage: 0 -> Active,
//    1 -> Two, 2 -> Three, >= 3 -> Four) -- RE-Q4 answered;
//  - Idle loops while the factory / depot is not busy;
//  - Production plays when the construction yard builds (placement) or the
//    shipyard launches a ship, then Idle resumes;
//  - UnitRepair depot: Special (arms down) -> SpecialTwo (loop while a unit is
//    serviced) -> SpecialThree (arms up) -> Idle (FUN_00451b30 cases 10/12);
//  - IsAnimDelayedFire (Tesla Coil, Prism Tower): Special on every shot;
//  - refinery SpecialAnim (GAREFNOR / NAREFNOR ore dump) when a harvester
//    starts unloading;
//  - DeployingAnim/UnderDoorAnim for a ground unit leaving a factory,
//    RoofDeployingAnim/UnderRoofDoorAnim for an aircraft;
//  - SuperAnim..Four on the superweapon READY / FIRE edges (W2-GA-14);
//  - <Slot>Damaged below ConditionYellow, <Slot>Garrisoned with occupants;
//  - CanBeOccupied 4-state main frame, Gate=yes GateStages frames, LeaveRubble
//    rubble frame 3, DestroyAnim ruins.

long g_pres_tick = 0;          // W3-B: LOGIC-tick anim clock (Presentation::logic_tick)
// W2-GA-14: the building draw_building is drawing (its overlay pass reads the
// per-building event state below), its rules id, and that state, keyed by the
// building's identity (origin cell + type, stable for its life).
const ra2engine::Building* g_overlay_bld = nullptr;
std::string g_overlay_id;
bool g_pres_low_power = false;  // house 0 drain > output (Presentation::power_eva)

enum : int {
    kBsActive = 3, kBsActive4 = 6, kBsPreProduction = 7, kBsProduction = 8, kBsTurret = 9,
    kBsSpecial = 10, kBsSpecial2 = 11, kBsSpecial3 = 12, kBsSpecial4 = 13, kBsSuper = 14, kBsSuper4 = 17,
    kBsIdle = 18,
};
struct BldSlotRun { long start = -1; bool on = false; };
struct BldEventState {
    long door_start = -1;          // tick a unit left the factory
    bool door_roof = false;        // ... an aircraft (roof pair)
    long super_start = -1;         // tick of the last SW state change
    int super_phase = -1;          // -1 none, 0 opening (ready), 2 closing (fired)
    BldSlotRun run[PresFx::kBldSlots];
    bool init = false;
    bool init_done = false;        // grand-opening slots started (logging)
    int level = -1;                // refinery storage level shown
    int depot = 0;                 // 0 idle, 1 Special, 2 SpecialTwo, 3 SpecialThree
    bool unloading = false;        // a harvester was dumping here last tick
    int gate_stage = 0;            // 0 closed .. GateStages open
    long gate_near = -100000;      // last tick a friendly unit stood at the gate
};
std::map<long long, BldEventState> g_bld_events;
inline long long bld_event_key(const ra2engine::Building& b) {
    return (static_cast<long long>(b.cx) << 32) ^ (static_cast<long long>(b.cy) << 16) ^ b.type;
}
// Frame of a one-shot anim started at `start`; -1 once it has run out.
inline int oneshot_frame(const FxAnimType& t, long start, long now) {
    if (start < 0) return -1;
    const long step = t.delay <= 0 ? 0 : (now - start) / t.delay;
    const long f = t.start + step;
    return f >= t.end ? -1 : static_cast<int>(f);
}
// Frame of a slot run started at `start` (the anim's own LoopCount: -1 loops
// for ever, else the finite passes then End); -1 before start / once ended.
inline int bld_run_frame(const FxAnimType& t, long start, long now) {
    if (start < 0 || now < start) return -1;
    if (t.loops >= 255) return PresFx::loop_frame(t, now - start);
    return t.frame_at(now - start);
}

// The per-building slot machine, one logic tick. `credits(h)` is house h's
// money (the player's wallet for house 0).
template <class Credits>
void pres_bld_anim_tick(const ra2engine::World& w, const BuildingAssets& ba, PresFx& fx, long now,
                        const std::vector<ra2engine::World::FireEvent>& fire, std::size_t& units_seen,
                        std::size_t& bld_seen, Credits credits) {
    if (!ba.ok) return;
    const auto& bs = w.buildings();
    const auto& us = w.units();
    auto id_of = [&](const ra2engine::Building& b) -> std::string {
        if (b.type < 0 || b.type >= static_cast<int>(ba.roster.size())) return std::string();
        return pres_upper(ba.roster[static_cast<std::size_t>(b.type)].id);
    };
    auto image_of = [&](const ra2engine::Building& b) -> std::string {
        if (b.type < 0 || b.type >= static_cast<int>(ba.roster.size())) return std::string();
        const ra2engine::BuildingTypeDef& d = ba.roster[static_cast<std::size_t>(b.type)];
        return pres_upper(d.image.empty() ? d.id : d.image);
    };
    auto start = [&](BldEventState& st, const PresFx::BldAnims& a, int s, long t0) {
        if (!a.slot[s].t) return false;
        st.run[s].start = t0;
        st.run[s].on = true;
        if (st.init_done && s != kBsIdle && !(s >= kBsActive && s <= kBsActive4))   // event slots only
            std::cerr << "[banim] t=" << t0 << " " << a.slot[s].t->name << " slot=" << s << std::endl;
        return true;
    };
    auto stop = [&](BldEventState& st, int s) { st.run[s].on = false; };
    // New units this tick: factory doors (ground / roof) and shipyard launches.
    for (std::size_t i = units_seen; i < us.size(); ++i) {
        const ra2engine::Unit& u = us[i];
        if (!u.alive || u.cargo_of >= 0 || !(u.is_vehicle || u.is_aircraft)) continue;
        for (const ra2engine::Building& b : bs) {
            if (!b.alive || b.house != u.house || b.state != ra2engine::Building::State::Idle) continue;
            if (u.cell_x < b.cx - 1 || u.cell_x > b.cx + b.fw || u.cell_y < b.cy - 1 || u.cell_y > b.cy + b.fh)
                continue;
            const std::string img = image_of(b);
            if (img.empty()) continue;
            const PresFx::BldAnims& a = fx.building(img);
            BldEventState& st = g_bld_events[bld_event_key(b)];
            if (u.is_naval && a.slot[kBsProduction].t) {   // the yard launches a ship
                stop(st, kBsIdle);
                start(st, a, kBsProduction, now);
                break;
            }
            const bool roof = u.is_aircraft && a.roof_door.t;
            if (!roof && !a.door.t) continue;
            st.door_start = now;
            st.door_roof = roof;
            std::cerr << "[banim] t=" << now << " " << img << (roof ? " roof" : "") << " door" << std::endl;
            break;
        }
    }
    units_seen = us.size();
    // A building placed this tick: its house's construction yard plays the
    // crane (ProductionAnim, LoopCount=1) [RE: FUN_0044d880 / FUN_0043c250
    // case 0xC: kill PreProduction + Idle, play Production on a ConstructionYard].
    for (std::size_t i = bld_seen; i < bs.size(); ++i) {
        const ra2engine::Building& nb = bs[i];
        if (!nb.alive) continue;
        for (const ra2engine::Building& b : bs) {
            if (&b == &nb || !b.alive || b.house != nb.house || b.state != ra2engine::Building::State::Idle) continue;
            const std::string id = id_of(b);
            if (id.empty() || !fx.building_rules(id).cons_yard) continue;
            const PresFx::BldAnims& a = fx.building(image_of(b));
            if (!a.slot[kBsProduction].t) continue;
            BldEventState& st = g_bld_events[bld_event_key(b)];
            stop(st, kBsPreProduction);
            stop(st, kBsIdle);
            start(st, a, kBsProduction, now);
        }
    }
    bld_seen = bs.size();
    // Storage share for Refinery=yes level anims.
    std::map<int, int> storers;
    for (const ra2engine::Building& b : bs)
        if (b.alive && b.state == ra2engine::Building::State::Idle) {
            const std::string id = id_of(b);
            if (!id.empty() && fx.building_rules(id).storage > 0) ++storers[b.house];
        }
    for (std::size_t bi = 0; bi < bs.size(); ++bi) {
        const ra2engine::Building& b = bs[bi];
        if (!b.alive || b.state != ra2engine::Building::State::Idle) continue;
        const std::string id = id_of(b), img = image_of(b);
        if (id.empty()) continue;
        const PresFx::BldAnims& a = fx.building(img);
        const PresFx::BldRules& r = fx.building_rules(id);
        BldEventState& st = g_bld_events[bld_event_key(b)];
        if (!st.init) {   // grand opening [FUN_004476d0: Idle, then Active..ActiveFour]
            st.init = true;
            start(st, a, kBsIdle, now);
            if (!r.refinery)
                for (int s = kBsActive; s <= kBsActive4; ++s) start(st, a, s, now);
            st.init_done = true;
        }
        // Finished finite runs, chained at their exact end tick (frame-rate
        // independent) [RE: FUN_00451b30].
        for (int s = 0; s < PresFx::kBldSlots; ++s) {
            BldSlotRun& run = st.run[s];
            if (!run.on || !a.slot[s].t) continue;
            const long len = a.slot[s].t->length_ticks(a.slot[s].t->delay);
            if (len < 0 || now < run.start + len) continue;
            const long end = run.start + len;
            run.on = false;
            if (s == kBsSpecial && r.unit_repair) {
                if (w.depot_client(static_cast<int>(bi)) >= 0 && start(st, a, kBsSpecial2, end)) st.depot = 2;
                else { st.depot = start(st, a, kBsSpecial3, end) ? 3 : 0; if (!st.depot) start(st, a, kBsIdle, end); }
            } else if (s == kBsSpecial3 && r.unit_repair) {
                st.depot = 0;
                start(st, a, kBsIdle, end);
            } else if (s == kBsProduction) {
                // [RE-FALLBACK: the Idle restart point after a Production run
                // was not traced; it resumes when the run ends.]
                start(st, a, kBsIdle, end);
            }
        }
        // Refinery storage level [RE: FUN_004509c0 Refinery=yes].
        // [RE-FALLBACK: the building's own StorageClass amount is the house's
        // money share over its Storage= buildings -- the clone banks ore as
        // credits at once.]
        if (r.refinery && r.storage > 0 && a.slot[kBsActive].t) {
            const int n = std::max(1, storers[b.house]);
            const double stored = std::min<double>(r.storage, std::max(0.0, credits(b.house)) / n);
            int level = std::clamp(static_cast<int>(stored * 4.0 / r.storage), 0, 3);
            while (level > 0 && !a.slot[kBsActive + level].t) --level;
            if (level != st.level) {
                for (int s = kBsActive; s <= kBsActive4; ++s) stop(st, s);
                start(st, a, kBsActive + level, now);
                st.level = level;
            }
        }
        // Service depot.
        if (r.unit_repair) {
            const bool client = w.depot_client(static_cast<int>(bi)) >= 0;
            if (client && st.depot == 0) {
                stop(st, kBsIdle);
                if (start(st, a, kBsSpecial, now)) st.depot = 1;
                else if (start(st, a, kBsSpecial2, now)) st.depot = 2;
            } else if (!client && st.depot == 2) {
                stop(st, kBsSpecial2);
                if (start(st, a, kBsSpecial3, now)) st.depot = 3;
                else { st.depot = 0; start(st, a, kBsIdle, now); }
            }
        }
        // Refinery ore dump: a harvester starts unloading here.
        if (a.slot[kBsSpecial].t && r.refinery) {
            bool dumping = false;
            for (const ra2engine::Unit& u : us)
                if (u.alive && u.is_harvester && u.harv == ra2engine::HarvState::Unloading &&
                    u.dock_refinery == static_cast<int>(bi)) { dumping = true; break; }
            if (dumping && !st.unloading) start(st, a, kBsSpecial, now);
            st.unloading = dumping;
        }
        // Gate=yes: open while a friendly unit stands at it [RE-FALLBACK: the
        // gate's own open/close mission and GateCloseDelay were not traced;
        // one stage per logic tick, closing 15 ticks after the last unit].
        if (r.gate && a.gate_stages > 0) {
            for (const ra2engine::Unit& u : us) {
                if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0 || !w.are_allied(u.house, b.house)) continue;
                if (u.cell_x >= b.cx - 1 && u.cell_x <= b.cx + b.fw && u.cell_y >= b.cy - 1 && u.cell_y <= b.cy + b.fh) {
                    st.gate_near = now;
                    break;
                }
            }
            const bool open = now - st.gate_near < 15;
            if (open && st.gate_stage < a.gate_stages) ++st.gate_stage;
            else if (!open && st.gate_stage > 0) --st.gate_stage;
        }
    }
    // Tesla Coil / Prism Tower: SpecialAnim on each shot (IsAnimDelayedFire).
    for (const ra2engine::World::FireEvent& e : fire) {
        if (e.building < 0 || e.building >= static_cast<int>(bs.size())) continue;
        const ra2engine::Building& b = bs[static_cast<std::size_t>(e.building)];
        const std::string img = image_of(b);
        if (img.empty()) continue;
        const PresFx::BldAnims& a = fx.building(img);
        if (a.delayed_fire) start(g_bld_events[bld_event_key(b)], a, kBsSpecial, now);
    }
}

// W3-B: the main-image frame for CanBeOccupied / Gate=yes types
// [RE: Shape_Number FUN_0043ef10]: occupiable = (occupants ? 2 : 0) +
// (ratio <= ConditionYellow ? 1 : 0) (garrisonable types use the yellow
// threshold); a gate shows its open stage (+ GateStages+1 when damaged).
int pres_building_main_frame(const ra2engine::Building& b, const std::string& id, const std::string& image,
                             int frame, int count, double yellow) {
    if (!g_fx || id.empty()) return frame;
    const PresFx::BldRules& r = g_fx->building_rules(id);
    const int images = std::max(1, count / 2);
    const bool damaged = b.max_hp > 0 && b.hp <= yellow * b.max_hp;
    if (r.occupiable && images >= 4) {
        const int f = (b.occupants.empty() ? 0 : 2) + (damaged ? 1 : 0);
        return std::min(f, images - 1);
    }
    if (r.gate) {
        const PresFx::BldAnims& a = g_fx->building(pres_upper(image));
        auto it = g_bld_events.find(bld_event_key(b));
        const int stage = it == g_bld_events.end() ? 0 : it->second.gate_stage;
        const int f = stage + (damaged ? a.gate_stages + 1 : 0);
        return std::clamp(f, 0, images - 1);
    }
    return frame;
}
// LeaveRubble=yes [RE: GetRubbleImage FUN_0045f150]: a destroyed building
// whose main SHP has more than 3 image frames leaves frame 3 (and its shadow
// frame Frames/2 + 3). Returns -1 when it leaves nothing.
int pres_building_rubble_frame(const std::string& id, int count) {
    if (!g_fx || id.empty() || !g_fx->building_rules(id).leave_rubble) return -1;
    return count / 2 > 3 ? 3 : -1;
}

// G-85: building overlays, called by draw_building. pass 0 = BibShape and the
// under-door interiors (under the main image), pass 1 = slot anims + damage
// fires (over it). `pal` is the building's house-remapped palette; `ckey` its
// colour key.
void pres_building_overlay(SDL_Renderer* ren, ra2engine::TextureCache& tc, const std::string& image,
                           int fcx, int fcy, bool damaged, bool powered, const ra2assets::Palette& pal,
                           int ckey, int W, int H, int pass) {
    if (!g_fx) return;
    const PresFx::BldAnims& a = g_fx->building(pres_upper(image));
    const std::string ck = ":c" + std::to_string(ckey);
    const BldEventState* ev = nullptr;
    bool garrisoned = false;
    if (g_overlay_bld) {
        auto e = g_bld_events.find(bld_event_key(*g_overlay_bld));
        if (e != g_bld_events.end()) ev = &e->second;
        garrisoned = !g_overlay_bld->occupants.empty();
    }
    const long now = g_pres_tick;
    auto slot_t = [&](const PresFx::BldAnims::Slot& s) -> const FxAnimType* {
        if (damaged && s.dmg) return s.dmg;
        if (garrisoned && s.garr) return s.garr;
        return s.t;
    };
    auto draw_t = [&](const FxAnimType* t, int fr, int x, int y) {
        if (!t || fr < 0) return;
        if (t->shadow)
            g_fx->blit_shadow(ren, tc, *t->shp, "bsh:" + t->name, fr + t->shp->count() / 2, fcx + x,
                              fcy + y + t->ydraw, W, H);
        g_fx->blit(ren, tc, *t->shp, "banim:" + t->name + ck, fr, fcx + x, fcy + y + t->ydraw,
                   PresFx::alpha_for(*t, fr), W, H, &pal);
    };
    const bool roof = ev && ev->door_roof;
    const PresFx::BldAnims::Slot& door = roof ? a.roof_door : a.door;
    const PresFx::BldAnims::Slot& under = roof ? a.under_roof_door : a.under_door;
    if (pass == 0) {
        if (a.bib && a.bib->count() > 0)
            g_fx->blit(ren, tc, *a.bib, "bib:" + image + ck, damaged && a.bib->count() > 1 ? 1 : 0, fcx,
                       fcy, 255, W, H, &pal);
        // W2-GA-14: the dark interior behind an opening factory door.
        if (ev && ev->door_start >= 0)
            if (const FxAnimType* d = slot_t(door))
                if (oneshot_frame(*d, ev->door_start, now) >= 0)
                    if (const FxAnimType* u = slot_t(under))
                        draw_t(u, PresFx::loop_frame(*u, now - ev->door_start), under.x, under.y);
        return;
    }
    // W2-GA-14: factory door (one shot per exiting unit) and SW charge states.
    if (ev && ev->door_start >= 0)
        if (const FxAnimType* d = slot_t(door)) draw_t(d, oneshot_frame(*d, ev->door_start, now), door.x, door.y);
    if (ev && ev->super_phase >= 0) {
        const int open = ev->super_phase;          // 0 (ready) or 2 (fired)
        const PresFx::BldAnims::Slot& s1 = a.slot[kBsSuper + open];
        const PresFx::BldAnims::Slot& s2 = a.slot[kBsSuper + open + 1];
        const FxAnimType* t1 = slot_t(s1);
        const int fr = t1 ? oneshot_frame(*t1, ev->super_start, now) : -1;
        if (t1 && fr >= 0) draw_t(t1, fr, s1.x, s1.y);
        else if (const FxAnimType* t2 = slot_t(s2)) draw_t(t2, PresFx::loop_frame(*t2, now), s2.x, s2.y);
    }
    if (ev) {
        for (int s = 0; s < PresFx::kBldSlots; ++s) {
            if (s == kBsTurret || (s >= kBsSuper && s <= kBsSuper4) || !ev->run[s].on) continue;
            const PresFx::BldAnims::Slot& sl = a.slot[s];
            const FxAnimType* t = slot_t(sl);
            if (!t) continue;
            // <Slot>PoweredLight: the anim goes away without power; <Slot>Powered
            // (default yes): it holds still.
            if (!powered && sl.powered_light) continue;
            const int fr = (sl.powered && !powered) ? t->start : bld_run_frame(*t, ev->run[s].start, now);
            draw_t(t, fr, sl.x, sl.y);
        }
    }
    if (damaged && !a.fire_offsets.empty()) {
        // [RE: W4-FIX-POS, RE-P6 resolved -- FUN_0043c050, called from the
        // damage-state update FUN_0043faa0] one DamageFireTypes anim per
        // DamageFireOffset (type +0x15D8, up to 8 points, list ends at the
        // first unset one) while below ConditionYellow, created at the
        // building's DRAW POINT (vtbl+0xAC, the origin-cell top vertex) +
        // the offset converted pixel->lepton (FUN_006d15e0), flags 0x600.
        // [RE-FALLBACK: the type pick -- the original starts at a random
        // DamageFireTypes index and cycles; the clone cycles from 0.]
        static std::vector<std::string> fires = g_fx->damage_fires();
        if (fires.empty()) return;
        for (std::size_t i = 0; i < a.fire_offsets.size(); ++i) {
            const FxAnimType* t = g_fx->type(fires[i % fires.size()]);
            if (!t) continue;
            const int fr = PresFx::loop_frame(*t, now + static_cast<long>(i) * 7);
            g_fx->blit(ren, tc, *t->shp, "fx:" + t->name, fr, fcx + a.fire_offsets[i].first,
                       fcy + a.fire_offsets[i].second + t->ydraw, PresFx::alpha_for(*t, fr), W, H);
        }
    }
}

// W3-B: civilian buildings drawn as map dressing (skirmish) loop their
// Active..ActiveFour and Idle anims from the start of the game, as the
// original's neutral BuildingClass does after its grand opening (they were
// frozen at frame 0). No events reach dressing.
void pres_dressing_overlay(SDL_Renderer* ren, ra2engine::TextureCache& tc, const std::string& image, int cx,
                           int cy, const ra2assets::Palette& pal, int W, int H) {
    if (!g_fx) return;
    const PresFx::BldAnims& a = g_fx->building(pres_upper(image));
    for (int s : {static_cast<int>(kBsIdle), 3, 4, 5, 6}) {
        const FxAnimType* t = a.slot[s].t;
        if (!t) continue;
        const int fr = bld_run_frame(*t, 0, g_pres_tick);
        if (fr < 0) continue;
        if (t->shadow)
            g_fx->blit_shadow(ren, tc, *t->shp, "bsh:" + t->name, fr + t->shp->count() / 2, cx + a.slot[s].x,
                              cy + a.slot[s].y + t->ydraw, W, H);
        g_fx->blit(ren, tc, *t->shp, "danim:" + t->name, fr, cx + a.slot[s].x, cy + a.slot[s].y + t->ydraw,
                   PresFx::alpha_for(*t, fr), W, H, &pal);
    }
}

// W3-B: a dead (even tombstoned) building that leaves rubble stays in the
// draw list (render_world) so its rubble frame is drawn.
bool pres_building_leaves_rubble(const ra2engine::Building& b, const BuildingAssets& ba) {
    if (!g_fx || b.sold || b.state != ra2engine::Building::State::Dead) return false;
    if (b.type < 0 || b.type >= static_cast<int>(ba.roster.size()) || b.type >= static_cast<int>(ba.art.size()))
        return false;
    return pres_building_rubble_frame(ba.roster[static_cast<std::size_t>(b.type)].id,
                                      ba.art[static_cast<std::size_t>(b.type)].shp.count()) >= 0;
}

// W3-B [RE: FootClass::AI FUN_004da440]: each unit's WalkedFramesSoFar
// (+0x538) -- +1 every WalkRate frames (TechnoType +0x294, default 1) while
// its locomotor moves, every IdleRate frames (+0x298, default 0 = never)
// standing. Drives SHP-vehicle walk frames and voxel HVA frames.
std::vector<int> g_walk_stage;
std::map<std::string, std::pair<int, int>> g_walk_rates;   // image -> {WalkRate, IdleRate}
const Ini* g_walk_rules = nullptr;
const std::pair<int, int>& pres_walk_rates(const std::string& image) {
    auto it = g_walk_rates.find(image);
    if (it != g_walk_rates.end()) return it->second;
    int walk = 1, idle = 0;
    if (g_walk_rules) {
        const std::string w = pres_clean(g_walk_rules->get(image, "WalkRate"));
        const std::string i = pres_clean(g_walk_rules->get(image, "IdleRate"));
        if (!w.empty()) walk = std::max(1, std::atoi(w.c_str()));
        if (!i.empty()) idle = std::max(0, std::atoi(i.c_str()));
    }
    return g_walk_rates.emplace(image, std::make_pair(walk, idle)).first->second;
}
int pres_idle_rate(const std::string& image) { return pres_walk_rates(image).second; }
int pres_walk_stage(int idx) {
    return idx >= 0 && idx < static_cast<int>(g_walk_stage.size()) ? g_walk_stage[static_cast<std::size_t>(idx)] : 0;
}
void pres_walk_tick(const ra2engine::World& w, long now) {
    const auto& us = w.units();
    if (g_walk_stage.size() < us.size()) g_walk_stage.resize(us.size(), 0);
    for (std::size_t i = 0; i < us.size(); ++i) {
        const ra2engine::Unit& u = us[i];
        if (!u.alive || !(u.is_vehicle || u.is_aircraft || u.is_naval) || u.die_time >= 0.0) continue;
        const std::pair<int, int>& r = pres_walk_rates(u.image);
        const bool moving = u.moving() || (u.is_aircraft && u.altitude > 0.0);
        if (moving ? (now % r.first == 0) : (r.second > 0 && now % r.second == 0)) ++g_walk_stage[i];
    }
}
