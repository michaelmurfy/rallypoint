// engine/net/net_game.cpp -- see net_game.hpp.
#include "net/net_game.hpp"

#include <algorithm>

#include "net/command_bridge.hpp"
#include "net/event_map.hpp"

namespace ra2::net {

namespace {

LockstepConfig make_session_config(const NetGameConfig& c) {
    LockstepConfig s;
    s.game_mode = c.mode;
    s.protocol = 2;
    s.local_house = c.local_house;
    s.houses = c.human_houses;
    s.master = c.master;
    s.master_house = c.master_house;
    s.max_ahead = c.max_ahead;
    s.frame_send_rate = c.frame_send_rate;
    s.game_speed = c.game_speed;
    s.crc_check_from_frame = c.crc_check_from_frame;
    return s;
}

NetManager& with_connections(NetManager& net, const NetGameConfig& c) {
    for (const auto& r : c.remotes) net.create_connection(r.first, r.second);
    // Set_Timing at session start (net-protocol.md §8): (max(RT + 2, 30), -1,
    // 600) from FUN_005DBD30 (RT unknown yet -> 30). W9-01-06: the Internet
    // session starts with the same original values -- RT is the lobby's
    // measured round trip on this NetManager -- instead of the CnCNet
    // spawner's (60, -1, 600), whose 1 s first retry made one lost order
    // packet stall the game for about a second until the 128-frame re-tune
    // (Set_Timing(RT + 10, ...)) caught up.
    const uint32_t rt = net.response_time();
    net.set_timing({std::max<uint32_t>(rt + 2, 30), 0xFFFFFFFFu, 600});
    return net;
}

}  // namespace

NetGame::NetGame(NetManager& net, const NetGameConfig& cfg)
    : net_(with_connections(net, cfg)), cfg_(cfg), session_(net_, make_session_config(cfg)) {
    session_.on_player_removed = [this](int house, int error) { removed_.emplace_back(house, error); };
}

bool NetGame::issue(const ra2engine::Command& c0) {
    ra2engine::Command c = c0;
    c.house = static_cast<std::int8_t>(cfg_.local_house);
    bool ok = true;
    for (const Event& e : to_events(c)) ok = session_.queue_local(e) && ok;
    return ok;
}

NetGame::Step NetGame::step(const std::function<uint32_t()>& state_hash, std::vector<ra2engine::Command>& out) {
    out.clear();
    if (!begun_) {
        const uint32_t f = session_.frame();
        const uint32_t every = cfg_.hash_every > 1 ? static_cast<uint32_t>(cfg_.hash_every) : 1u;
        if (!have_hash_ || f % every == 0) {
            last_hash_ = state_hash ? state_hash() : 0u;
            have_hash_ = true;
            ++hashes_;
        }
        session_.begin_frame(last_hash_);
        begun_ = true;
    }
    switch (session_.poll()) {
        case FrameStatus::Stall:
            // Frame 0 waits for every player to finish LOADING (a phone can take
            // a minute): the original's loading-progress barrier holds the
            // initial-sync timeout (2400 ticks LAN) meanwhile; we hold it for
            // init_patience_ms, then the session's own timeout applies.
            if (session_.frame() == 0) {
                const uint32_t now = net_.now_ms();
                if (!init_wait_started_) {
                    init_wait_started_ = true;
                    init_wait_since_ = now;
                }
                if (now - init_wait_since_ < cfg_.init_patience_ms) session_.hold_wait();
            }
            return Step::Stall;
        case FrameStatus::OutOfSync: return Step::OutOfSync;
        case FrameStatus::ScenarioMismatch: return Step::ScenarioMismatch;
        case FrameStatus::Timeout: return Step::Timeout;
        case FrameStatus::DoListFull: return Step::Failed;
        case FrameStatus::Proceed: break;
    }
    std::vector<NetCommand> recs;
    recs.reserve(session_.executed().size());
    for (const ExecItem& x : session_.executed()) {
        NetCommand n;
        if (!event_to_command(x.ev, n)) continue;
        if (n.kind == EventType::REMOVEPLAYER && !removeplayer_allowed(n.house, n.value)) {
            ++rejected_removes_;
            continue;
        }
        recs.push_back(std::move(n));
    }
    out = from_net(recs);
    const uint32_t f = session_.frame();
    for (ra2engine::Command& c : out) c.frame = f;
    session_.end_frame();
    begun_ = false;
    return Step::Run;
}

// W9-05-12 (clone hardening; the original executed any REMOVEPLAYER): a
// house may be removed by the master (the host's timeout / "Drop player"),
// by itself, or -- when the master is the one that vanished -- by any human
// house. The target must be a human house of this match. Decided from the
// event alone (house + target), so every peer decides the same.
bool NetGame::removeplayer_allowed(int sender, int target) const {
    const auto& hh = cfg_.human_houses;
    if (std::find(hh.begin(), hh.end(), target) == hh.end()) return false;
    if (std::find(hh.begin(), hh.end(), sender) == hh.end()) return false;
    if (cfg_.master_house < 0) return true;
    return sender == cfg_.master_house || sender == target || target == cfg_.master_house;
}

const char* NetGame::step_name(Step s) {
    switch (s) {
        case Step::Run: return "run";
        case Step::Stall: return "stall";
        case Step::OutOfSync: return "out-of-sync";
        case Step::ScenarioMismatch: return "scenario-mismatch";
        case Step::Timeout: return "timeout";
        case Step::Failed: return "failed";
    }
    return "?";
}

void NetGame::request_exit() {
    if (exiting_) return;
    exiting_ = true;
    session_.request_exit();
}

bool NetGame::poll_exit() { return !exiting_ || session_.poll_exit(); }

std::vector<std::pair<int, int>> NetGame::take_removed() {
    std::vector<std::pair<int, int>> r;
    r.swap(removed_);
    return r;
}

bool NetGame::is_remote(int house) const {
    for (const auto& r : cfg_.remotes)
        if (r.first == house) return session_.peers().count(house) != 0;
    return false;
}

void NetGame::drop_player(int house) {
    if (is_remote(house)) session_.destroy_connection(house, 1);
}

int NetGame::house_of(const PeerAddr& a) const {
    for (const auto& r : cfg_.remotes)
        if (r.second == a) return r.first;
    return -1;
}

void NetGame::send_chat(const std::string& sender, const std::string& utf8, int color, const std::vector<int>* to) {
    ChatMessage m;
    m.sender = sender;
    m.text = utf8_to_u16(utf8);
    m.color = color;
    const std::vector<uint8_t> b = encode_chat(m);
    for (const auto& r : cfg_.remotes) {
        if (!session_.peers().count(r.first)) continue;
        if (to && std::find(to->begin(), to->end(), r.first) == to->end()) continue;   // W9-05-07
        net_.send_global(b.data(), b.size(), true, &r.second);
    }
    net_.service();
}

void NetGame::send_beacon(const BeaconMsg& m0) {
    BeaconMsg m = m0;
    m.house = cfg_.local_house;
    const std::vector<uint8_t> b = encode_beacon_msg(m);
    for (const auto& r : cfg_.remotes)
        if (session_.peers().count(r.first)) net_.send_global(b.data(), b.size(), true, &r.second);
    net_.service();
}

void NetGame::poll_global(std::vector<NetChat>& chat, std::vector<AwayNotice>& away) {
    std::vector<BeaconMsg> ignored;
    poll_global(chat, away, ignored);
}

void NetGame::send_away(bool back, uint32_t seconds) {
    AwayNotice a;
    a.back = back;
    a.house = cfg_.local_house;
    a.seconds = seconds;
    const std::vector<uint8_t> b = encode_away(a);
    for (const auto& r : cfg_.remotes) {
        if (!session_.peers().count(r.first)) continue;
        // 05 §7.2: one unreliable copy right now plus a reliable one.
        net_.send_global(b.data(), b.size(), false, &r.second);
        net_.send_global(b.data(), b.size(), true, &r.second);
    }
    net_.service();
}

void NetGame::poll_global(std::vector<NetChat>& chat, std::vector<AwayNotice>& away, std::vector<BeaconMsg>& beacons) {
    net_.service();
    ReceivedPacket p;
    while (net_.get_global(p)) {
        const uint32_t cmd = global_cmd(p.payload);
        const int h = house_of(p.from);
        if (cmd == kGlobalChat) {
            ChatMessage m;
            if (!decode_chat(p.payload, m)) continue;
            chat.push_back(NetChat{h, m.sender, u16_to_utf8(m.text), m.color});
        } else if (cmd == kGlobalAway || cmd == kGlobalBack) {
            AwayNotice a;
            if (!decode_away(p.payload, a)) continue;
            if (h >= 0) a.house = h;   // trust the address, not the body
            away.push_back(a);
        } else if (cmd == kGlobalBeaconPlace || cmd == kGlobalBeaconDelete || cmd == kGlobalBeaconMessage) {
            BeaconMsg m;
            if (!decode_beacon_msg(p.payload, m) || h < 0) continue;
            m.house = h;   // trust the address, not the body (W9-05-12)
            beacons.push_back(std::move(m));
        } else if (cmd == kGlobalSignOff) {
            // A global NET_SIGN_OFF in game -> Destroy_Connection (case 0x0A in
            // FUN_0048D0F0): REMOVEPLAYER for everyone.
            if (h >= 0) session_.destroy_connection(h, 1);
        }
        // Anything else (late lobby resends, pings) is ignored.
    }
}

}  // namespace ra2::net
