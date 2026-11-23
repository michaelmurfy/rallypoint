// engine/net/lockstep.cpp -- see lockstep.hpp. Addresses refer to gamemd.exe
// (YR 1.001, this install); docs/re/net-protocol.md section 6.
#include "net/lockstep.hpp"

#include "net/net_bytes.hpp"

#include <algorithm>
#include <cstring>

namespace ra2::net {

namespace {
constexpr size_t kDoListCap = 0x4000;  // QueueClass<EventClass,16384>; full at > 0x3FFF
constexpr size_t kOutListCap = 0x80;   // QueueClass<EventClass,128>
constexpr uint8_t T(EventType t) { return static_cast<uint8_t>(t); }

// Spawner LatencyLevel::GetMaxAhead.
constexpr int kP0MaxAhead[10] = {1, 4, 6, 12, 16, 20, 24, 28, 32, 36};
uint8_t p0_level_from_rt(uint8_t rt) {
    for (int i = 1; i < 9; ++i)
        if (rt <= kP0MaxAhead[i]) return static_cast<uint8_t>(i);
    return 9;
}
}  // namespace

WaitTimeouts wait_timeouts(GameMode m) {
    switch (m) {  // 0x836338 + mode*16
        case GameMode::Serial:
        case GameMode::Modem: return {120, 900, 420, 3600};
        case GameMode::LAN: return {60, 900, 420, 2400};
        case GameMode::Internet: return {120, 1800, 720, 7200};
    }
    return {60, 900, 420, 2400};
}

LockstepSession::LockstepSession(NetManager& net, const LockstepConfig& cfg)
    : net_(net), cfg_(cfg),
      enc_(cfg.spawner_ext || cfg.spawner_protocol_zero),
      dec_(cfg.spawner_ext || cfg.spawner_protocol_zero),
      frame_(cfg.start_frame), max_ahead_(cfg.max_ahead), fsr_(cfg.frame_send_rate),
      precalc_max_ahead_(cfg.precalc_max_ahead), precalc_fps_(cfg.precalc_fps),
      latency_fudge_(cfg.latency_fudge) {
    for (int h : cfg_.houses)
        if (h != cfg_.local_house) peers_[h] = PeerState{};
    mplayer_count_ = static_cast<int>(peers_.size()) + 1;
    std::sort(cfg_.houses.begin(), cfg_.houses.end());
    if (std::find(cfg_.houses.begin(), cfg_.houses.end(), cfg_.local_house) == cfg_.houses.end()) {
        cfg_.houses.push_back(cfg_.local_house);
        std::sort(cfg_.houses.begin(), cfg_.houses.end());
    }
    // 0x647090 static init: the CRC grace timer starts expired.
    crc_timer_start_ = static_cast<int32_t>(frame_);
    crc_timer_dur_ = 0;
    // The int that follows the 0x22E-byte send buffer in Session (0xA8C9A8):
    // the encoder's out-of-space path may read a few bytes of it.
    put_u32le(pkt_.data() + kMaxGamePacket, static_cast<uint32_t>(kMaxGamePacket));
}

bool LockstepSession::add_to_outlist(const Event& e) {
    if (outlist_.size() >= kOutListCap) return false;
    outlist_.push_back(e);
    return true;
}

bool LockstepSession::queue_local(const Event& e) {
    Event c = e;
    c.house = static_cast<int8_t>(cfg_.local_house);
    return add_to_outlist(c);
}

bool LockstepSession::fsr_boundary() const {
    if (cfg_.protocol != 2 || fsr_ == 0) return true;
    const int32_t f = static_cast<int32_t>(fsr_), fr = static_cast<int32_t>(frame_);
    return fr == ((f - 1 + fr) / f) * f;
}

void LockstepSession::set_process_time_sample(uint32_t total_ms, uint32_t frames) {
    proc_ms_ = total_ms;
    proc_frames_ = frames;
}

void LockstepSession::note_process_time(double ms) {
    ext_proc_ = true;
    ext_proc_ms_ += ms > 0.0 ? ms : 0.0;
    ++ext_proc_frames_;
}

// --------------------------------------------------------------- sending
void LockstepSession::send_framesync() {
    // FUN_00649A70: 14 bytes, DATA_NOACK, to every connection.
    FrameHeader h;
    h.type = T(EventType::FRAMESYNC);
    h.house = static_cast<int8_t>(cfg_.local_house);
    h.frame = stamp_frame(frame_, max_ahead_, fsr_, cfg_.protocol);
    h.crc = scenario_magic_;
    h.command_count = static_cast<uint16_t>(cmd_count_);
    h.delay = static_cast<uint8_t>(max_ahead_);
    uint8_t b[kFrameHeaderSize];
    h.write(b);
    net_.send_private(b, sizeof b, false, -1);
    framesync_sent_at_ = net_.now_ticks();
    ++stats_.packets_sent;
}

void LockstepSession::send_packets() {
    // FUN_00649710(maxlen = 0x22E, MaxAhead, CommandCount)
    int cap = static_cast<int>(outlist_.size());
    const int room = static_cast<int>(0x2000 - std::min<size_t>(dolist_.size(), 0x2000));
    if (room < cap) cap = room;
    const size_t initial = outlist_.size();
    const uint16_t cmd = static_cast<uint16_t>(cmd_count_);
    int total = 0;
    std::vector<Event> echo;
    for (;;) {
        bool ack = !(cap == 0 || outlist_.empty() || mplayer_count_ == 1);
        FrameHeader h;
        h.type = T(EventType::FRAMEINFO);
        h.house = static_cast<int8_t>(cfg_.local_house);
        h.frame = stamp_frame(frame_, max_ahead_, fsr_, cfg_.protocol);
        h.crc = cur_crc_;
        h.command_count = cmd;
        h.delay = static_cast<uint8_t>(max_ahead_);
        h.write(pkt_.data());
        size_t len = 0;
        int n = 0;
        echo.clear();
        const size_t echo_room = kDoListCap - std::min(dolist_.size(), kDoListCap);
        if (cfg_.protocol == 0) {
            std::memset(pkt_.data() + kFrameHeaderSize, 0, kEventClassSize - kFrameHeaderSize);
            len = encode_raw_events(outlist_, pkt_.data(), kMaxGamePacket, kEventClassSize, cap,
                                    frame_ + max_ahead_, static_cast<int8_t>(cfg_.local_house),
                                    echo, echo_room, n);
        } else {
            len = enc_.add_compressed(outlist_, pkt_.data(), kMaxGamePacket, kFrameHeaderSize, cap,
                                      h.frame, static_cast<int8_t>(cfg_.local_house), frame_, echo,
                                      echo_room, n);
        }
        for (auto& e : echo) dolist_.push_back(std::move(e));  // own events, same frame
        cap -= n;
        total += n;
        if (n != 0) ack = true;
        net_.send_private(pkt_.data(), len, ack, -1);
        ++stats_.packets_sent;
        net_.service();
        if (outlist_.empty() || initial < outlist_.size() || cap == 0) break;
        if (n == 0) break;  // an event that can never fit (the original would spin)
    }
    cmd_count_ = static_cast<uint16_t>(cmd_count_ + total);
}

// ------------------------------------------------------------- receiving
int LockstepSession::process_packet(int conn_id, const std::vector<uint8_t>& pkt) {
    auto it = peers_.find(conn_id);
    if (it == peers_.end() || pkt.size() < kFrameHeaderSize) return 0;
    PeerState& p = it->second;
    const FrameHeader h = FrameHeader::read(pkt.data());
    int ret = 0;
    const int32_t sf = h.sender_frame();
    // W9-05-12 (clone hardening): the header's house is the sender's own
    // (compressed events inherit it), and an honest sender's frame is never
    // far ahead of ours. Anything else is forged or corrupt: dropped whole,
    // before it can move their_frame (which opens the gate) or the counts.
    if (h.house != conn_id || sf > static_cast<int32_t>(frame_) + kFrameWindow) {
        ++stats_.rejected_packets;
        return 0;
    }
    if (p.their_frame < sf) {
        if (p.their_frame == -1) ret = 1;  // "Player ready"
        p.their_frame = sf;
    }
    if (p.announced < h.command_count) p.announced = h.command_count;
    p.last_heard = net_.now_ticks();
    if (h.type == T(EventType::FRAMESYNC)) {
        if (h.crc == scenario_magic_) {
            p.rtt = net_.now_ticks() - framesync_sent_at_;
            return ret;
        }
        return 2;
    }
    std::vector<Event> evs;
    const size_t room = kDoListCap - std::min(dolist_.size(), kDoListCap);
    int n = cfg_.protocol == 0 ? decode_raw_events(pkt.data(), pkt.size(), evs, room)
                               : dec_.extract(pkt.data(), pkt.size(), evs, room);
    if (n == -1) return 3;
    // The command count: every decoded event except the FRAMEINFO header
    // itself (the original: n - 1). W9-05-12: a rejected event does not count
    // either -- an honest sender never sends one, and counting forged events
    // could let the gate open before a lost real command arrived.
    bool header = h.type == T(EventType::FRAMEINFO);
    uint32_t counted = 0;
    for (auto& e : evs) {
        const bool is_header = header && e.type == T(EventType::FRAMEINFO);
        if (is_header) header = false;
        // W9-05-12: every event must be the sender's own (a raw packet, or a
        // second FRAMEINFO record, can name any house) and inside the frame
        // window.
        if (e.house != conn_id || static_cast<int64_t>(e.frame) > static_cast<int64_t>(frame_) + kFrameWindow) {
            ++stats_.rejected_events;
            continue;
        }
        if (!is_header) ++counted;
        dolist_.push_back(std::move(e));
    }
    p.received += counted & 0xFFFFu;
    return ret;
}

int LockstepSession::receive_some() {
    std::vector<uint8_t> pkt;
    int id = -1;
    for (int i = 0; i < 64; ++i) {
        if (!net_.get_private(pkt, id)) break;
        const int r = process_packet(id, pkt);
        if (r == 1) ++ready_count_;
        if (r == 2 || r == 3) return r;
        net_.service();
    }
    return 0;
}

bool LockstepSession::gate_open() const {
    // Wait_For_Players normal-frame condition.
    if (mplayer_count_ == 1) return true;
    int64_t min_frame = static_cast<int64_t>(frame_) + 1000;
    for (const auto& kv : peers_) min_frame = std::min<int64_t>(min_frame, kv.second.their_frame);
    for (const auto& kv : peers_)
        if (kv.second.received < kv.second.announced) return false;
    return static_cast<int64_t>(frame_) < min_frame + static_cast<int64_t>(max_ahead_);
}

int LockstepSession::slowest_peer() const {
    int best = -1;
    int64_t mf = INT64_MAX;
    for (int i = 0; i < net_.num_connections(); ++i) {
        const int id = net_.connection_id(i);
        auto it = peers_.find(id);
        if (it == peers_.end()) continue;
        if (it->second.their_frame < mf) {
            mf = it->second.their_frame;
            best = id;
        }
    }
    return best;
}

uint32_t LockstepSession::peer_lag_frames() const {
    uint32_t lag = 0;
    for (const auto& kv : peers_) {
        if (kv.second.their_frame < 0) continue;
        const int64_t d = static_cast<int64_t>(frame_) - kv.second.their_frame;
        if (d > static_cast<int64_t>(lag)) lag = static_cast<uint32_t>(d);
    }
    return lag;
}

void LockstepSession::kick_slowest() {
    // FUN_0064AE80 (LAN/Internet): Kick_Player(index of min theirFrame, 1).
    const int id = slowest_peer();
    if (id >= 0) destroy_connection(id, 1);
}

void LockstepSession::destroy_connection(int house, int error) {
    // FUN_005DA170
    auto it = peers_.find(house);
    if (it == peers_.end()) return;
    net_.destroy_connection(house);
    if (error != 0 && frame_ > 0) {
        Event e = ev::int_event(EventType::REMOVEPLAYER, cfg_.local_house, house);
        add_to_outlist(e);
    }
    peers_.erase(it);
    --mplayer_count_;
    if (on_player_removed) on_player_removed(house, error);
}

// --------------------------------------------------------------- timing
void LockstepSession::master_timing() {
    if (!cfg_.master) return;
    if (!(precalc_max_ahead_ != 0 || precalc_fps_ != 0 || (frame_ & 0xFF) == 0)) return;
    Event t = ev::make(EventType::TIMING, cfg_.local_house, frame_);
    if (precalc_max_ahead_ == 0 && precalc_fps_ == 0) {
        // "Generate Real Timing Event": needs > 4 commands sent (spawner P0: always).
        if (!(static_cast<uint16_t>(cmd_count_) > 4 || cfg_.spawner_protocol_zero)) return;
        int32_t max_time = local_node_time_;
        if (max_time == -1) return;
        for (const auto& kv : peers_) {
            if (kv.second.node_time == -1) return;
            max_time = std::max(max_time, kv.second.node_time);
        }
        int fps = 60;
        if (max_time != 0) fps = std::max(1, 1000 / max_time);
        const int cap = cfg_.game_speed == 0 ? 60 : cfg_.game_speed == 1 ? 45 : 60 / cfg_.game_speed;
        fps = std::min(fps, cap);
        uint32_t rt = cfg_.spawner_protocol_zero ? static_cast<uint32_t>(p0_worst_max_ahead_)
                                                 : net_.response_time();
        uint32_t rt2 = rt;
        uint32_t fsr = fsr_;
        if (cfg_.game_mode == GameMode::Internet) fsr = fps < 31 ? 5 : 10;
        if (cfg_.spawner_protocol_zero) fsr = p0_new_fsr_;
        if (fsr == 0) fsr = 1;
        uint32_t add = 0;
        if (rt != 0) {
            switch (latency_fudge_) {
                case 1: add = 10; rt += rt >> 1; rt2 += rt2 / 2; break;
                case 2: rt *= 2; rt2 *= 2; add = 20; break;
                case 3: rt *= 3; rt2 *= 3; add = 30; break;
                default: break;
            }
        }
        int32_t nw = static_cast<int32_t>((rt * static_cast<uint32_t>(fps)) / 120u + add);
        if (static_cast<int32_t>(max_ahead_) < nw && static_cast<int32_t>(max_ahead_ + fsr) <= nw)
            nw = static_cast<int32_t>(max_ahead_ + fsr);
        const int32_t f = static_cast<int32_t>(fsr);
        nw = ((nw - 1 + f) / f) * f;
        if (nw <= 3 * f) nw = 3 * f;
        const int32_t cap2 = ((f + 0xF9) / f) * f;
        if (cap2 <= nw) nw = cap2;
        t.set_u16(0, static_cast<uint16_t>(fps));
        t.set_u16(2, static_cast<uint16_t>(nw + (cfg_.fog_of_war ? 10 : 0)));
        t.data[4] = static_cast<uint8_t>(fsr);
        add_to_outlist(t);
        const uint32_t to = std::max<uint32_t>(rt2 * 8 + 15, 120);
        net_.set_timing({rt2 + 10, 0xFFFFFFFFu, to});
    } else {
        // "Sending precalculated network timing"
        t.set_u16(0, static_cast<uint16_t>(precalc_fps_));
        t.set_u16(2, static_cast<uint16_t>(precalc_max_ahead_));
        t.data[4] = cfg_.spawner_protocol_zero ? p0_new_fsr_ : static_cast<uint8_t>(precalc_fps_ > 30 ? 10 : 5);
        add_to_outlist(t);
        precalc_max_ahead_ = 0;
        precalc_fps_ = 0;
    }
}

void LockstepSession::protocol_zero_send() {
    // Spawner ProtocolZero::SendResponseTime2 (MainLoop after render).
    const int now = static_cast<int>(frame_);
    if (p0_next_send_ < 0) {
        p0_next_send_ = now + static_cast<int>(fsr_) + 8 * 30;
        return;
    }
    if (p0_next_send_ >= now) return;
    const uint32_t rt = net_.response_time();
    ev::ResponseTime2 r;
    r.max_ahead = static_cast<int8_t>(static_cast<int8_t>(rt) + 1);
    r.latency_level = p0_level_from_rt(static_cast<uint8_t>(rt));
    Event e = ev::response_time2(cfg_.local_house, r);
    e.frame = frame_ + max_ahead_;
    if (add_to_outlist(e))
        p0_next_send_ = now + 30;
    else
        ++p0_next_send_;
}

void LockstepSession::protocol_zero_handle(const Event& e) {
    // Spawner ProtocolZero::HandleResponseTime2 + LatencyLevel::Apply.
    if (!cfg_.spawner_protocol_zero) return;
    const ev::ResponseTime2 r = ev::get_response_time2(e);
    if (r.max_ahead == 0) return;
    const int h = e.house & 7;
    p0_max_ahead_[h] = r.max_ahead;
    p0_mode_[h] = r.latency_level;
    p0_last_frame_[h] = static_cast<int32_t>(e.frame);
    uint8_t lvl = 0;
    int maxma = 0;
    for (int i = 0; i < 8; ++i) {
        if (static_cast<int32_t>(frame_) >= p0_last_frame_[i] + 8 * 30 / 2) {
            p0_max_ahead_[i] = 0;
            p0_mode_[i] = 0;
        } else {
            maxma = std::max(maxma, p0_max_ahead_[i]);
            lvl = std::max(lvl, p0_mode_[i]);
        }
    }
    p0_worst_max_ahead_ = maxma;
    if (lvl > 9) lvl = 9;
    if (lvl > cfg_.max_latency_level) lvl = cfg_.max_latency_level;
    if (lvl <= p0_level_) return;
    p0_level_ = lvl;
    p0_new_fsr_ = lvl;
    precalc_fps_ = 60;
    precalc_max_ahead_ = static_cast<uint32_t>(kP0MaxAhead[lvl]);
}

void LockstepSession::hold_wait() {
    const uint32_t now = net_.now_ticks();
    wait_start_ = now;
    dialog_open_ = false;
}

void LockstepSession::execute_net_event(const Event& e) {
    switch (e.kind()) {
        case EventType::TIMING: {  // case 0x20 in FUN_004C6BD0
            ev::Timing t = ev::get_timing(e);
            uint16_t ma = t.max_ahead;
            if (cfg_.fog_of_war && !cfg_.spawner_protocol_zero) ma = static_cast<uint16_t>(ma - 10);
            // W9-05-12 (clone hardening): clamp to what the header can carry
            // (Delay is a u8) and what an honest master sends. Every peer
            // executes the same TIMING, so the clamp is deterministic.
            ma = static_cast<uint16_t>(std::clamp<uint32_t>(ma, 1u, kMaxMaxAhead));
            t.frame_send_rate = static_cast<uint8_t>(std::clamp<uint32_t>(t.frame_send_rate, 1u, kMaxFrameSendRate));
            t.desired_fps = static_cast<uint16_t>(std::clamp<uint32_t>(t.desired_fps, 1u, 60u));
            const uint32_t fsr = t.frame_send_rate ? t.frame_send_rate : 1;
            if (max_ahead_ < ma || fsr_ < t.frame_send_rate) {
                move_from_ = static_cast<int32_t>(e.frame);
                const int32_t f = static_cast<int32_t>(fsr);
                move_to_ = ((static_cast<int32_t>(ma) + f - 1 + static_cast<int32_t>(e.frame)) / f) * f;
            } else {
                move_from_ = move_to_ = 0;
            }
            desired_fps_ = t.desired_fps;
            max_ahead_ = ma;
            max_max_ahead_ = std::max(max_max_ahead_, max_ahead_);
            fsr_ = t.frame_send_rate;
            break;
        }
        case EventType::RESPONSE_TIME:
            max_ahead_ = std::clamp<uint32_t>(static_cast<uint32_t>(ev::get_response_time(e)), 1u, kMaxMaxAhead);
            break;
        case EventType::PROCESS_TIME: {
            const int32_t v = e.u16(0);
            if (e.house == cfg_.local_house)
                local_node_time_ = v;
            else if (peers_.count(e.house))
                peers_[e.house].node_time = v;
            break;
        }
        case EventType::LATENCYFUDGE: latency_fudge_ = std::clamp(e.i32(0), 0, 3); break;
        case EventType::RESPONSE_TIME2: protocol_zero_handle(e); break;
        default: break;
    }
}

bool LockstepSession::execute_dolist() {
    // Prologue: after a TIMING grew the horizon, move queued events.
    for (auto& e : dolist_) {
        if (e.type != T(EventType::FRAMEINFO) && move_from_ < static_cast<int32_t>(e.frame) &&
            static_cast<int32_t>(e.frame) < move_to_) {
            e.frame = static_cast<uint32_t>(move_to_);
            ++stats_.moved_events;
        }
    }
    int group_id = 0;
    for (int h : cfg_.houses) {
        std::vector<Event> mega;
        for (auto& e : dolist_) {
            if (e.house != h || e.frame > frame_ || e.executed()) continue;
            if (e.frame < frame_ && e.type != T(EventType::FRAMEINFO)) {
                ++stats_.late_events;  // "Packet received too late!": dropped
                continue;
            }
            const uint8_t t = e.type;
            if (t == T(EventType::EXIT)) {
                exec_.push_back(ExecItem{e});
                if (h != cfg_.local_house) destroy_connection(h, 0);
            } else if (t == T(EventType::OPTIONS) || t == T(EventType::PAGEUSER)) {
                if (h == cfg_.local_house) exec_.push_back(ExecItem{e});
            } else if (t == T(EventType::FRAMEINFO)) {
                bool compare;
                if (cfg_.crc_check_from_frame >= 0) {
                    compare = static_cast<int32_t>(frame_) >= cfg_.crc_check_from_frame;
                } else {
                    // CDTimer {start 0xAF93B8, duration 0xAF93C0}
                    const int32_t el = static_cast<int32_t>(frame_) - crc_timer_start_;
                    compare = crc_timer_dur_ <= el;
                }
                if (compare && e.frame == frame_) {
                    const ev::FrameInfo fi = ev::get_frame_info(e);
                    const uint32_t sf = e.frame - fi.delay;
                    if (crc_hist_[sf & 0xFF] != fi.crc) {
                        oos_.house = h;
                        oos_.frame = sf;
                        oos_.local_crc = crc_hist_[sf & 0xFF];
                        oos_.remote_crc = fi.crc;
                        return false;
                    }
                }
            } else if (t == T(EventType::MEGAMISSION)) {
                if (mega.size() < 0x100) mega.push_back(e);
            } else if (t == T(EventType::TIMING) || t == T(EventType::RESPONSE_TIME) ||
                       t == T(EventType::LATENCYFUDGE)) {
                // W9-05-12: only the master times the session (Am_I_Master).
                if (cfg_.master_house < 0 || h == cfg_.master_house)
                    execute_net_event(e);
                else
                    ++stats_.rejected_events;
            } else if (t == T(EventType::PROCESS_TIME) || t == T(EventType::RESPONSE_TIME2)) {
                execute_net_event(e);
            } else {
                exec_.push_back(ExecItem{e});
            }
            e.flags |= 1;
        }
        // Megamission grouping (FUN_0064C810 on runs of equal Mission/Target/Dest).
        const size_t base = exec_.size();
        for (auto& m : mega) exec_.push_back(ExecItem{m});
        int8_t prev_mission = -1;
        uint32_t prev_t = 0, prev_d = 0;
        uint8_t prev_tr = 0, prev_dr = 0;
        size_t run_start = 0;
        auto mark = [&](size_t a, size_t b) {
            for (size_t k = a; k < b; ++k) {
                exec_[base + k].group = group_id;
                exec_[base + k].group_size = static_cast<int>(b - a);
            }
            ++group_id;
        };
        for (size_t i = 0; i < mega.size(); ++i) {
            const Event& m = mega[i];
            bool brk = false;
            if (prev_mission != -1) {
                brk = static_cast<int8_t>(m.data[5]) != prev_mission;
                if (m.data[11] != prev_tr || get_u32le(&m.data[7]) != prev_t || m.data[16] != prev_dr ||
                    get_u32le(&m.data[12]) != prev_d)
                    brk = true;
            }
            prev_mission = static_cast<int8_t>(m.data[5]);
            prev_t = get_u32le(&m.data[7]);
            prev_tr = m.data[11];
            prev_d = get_u32le(&m.data[12]);
            prev_dr = m.data[16];
            if (brk) {
                const size_t len = i - run_start;
                const size_t s = run_start;
                run_start = i;
                if (len > 1) mark(s, i);
            } else if (i == mega.size() - 1 && (i - run_start) + 1 > 1) {
                mark(run_start, i + 1);
            }
        }
    }
    return true;
}

// ------------------------------------------------------------ the frame
void LockstepSession::initial_reset() {
    for (auto& kv : peers_) {
        PeerState& p = kv.second;
        const int32_t nt = p.node_time;
        p = PeerState{};
        p.node_time = nt;
    }
    crc_timer_start_ = static_cast<int32_t>(frame_);
    crc_timer_dur_ = static_cast<int32_t>(frame_) + 0x100;
    cmd_count_ = 0;
    crc_hist_.fill(0);
    enc_.reset();
    dec_.reset();
    if (cfg_.load_game)
        scenario_magic_ = 0;  // Session +0x684 flag set: ScenarioMagic = 0
    else
        scenario_magic_ = cur_crc_;
}

void LockstepSession::begin_frame(uint32_t frame_crc) {
    exec_.clear();
    const uint32_t now_ms = net_.now_ms();
    if (have_last_begin_) {
        proc_ms_ += now_ms - last_begin_ms_;
        ++proc_frames_;
    }
    last_begin_ms_ = now_ms;
    have_last_begin_ = true;
    sticky_ = FrameStatus::Proceed;
    dialog_open_ = false;
    stall_counted_ = false;

    if (cfg_.spawner_protocol_zero) protocol_zero_send();

    cur_crc_ = frame_crc;  // ComputeFrameCRC -> CurrentFrameCRC (0xAC4164)
    crc_hist_[frame_ & 0xFF] = frame_crc;

    const WaitTimeouts to = wait_timeouts(cfg_.game_mode);
    const uint32_t now = net_.now_ticks();
    if (frame_ == 0 || (cfg_.load_game && frame_ == cfg_.start_frame)) {
        initial_reset();
        send_framesync();
        init_frame_ = true;
        ready_count_ = 0;
        phase_ = Phase::InitWait;
        wait_start_ = resend_at_ = now;
        resend_period_ = to.resend_init;
        dialog_after_ = to.dialog_init;
        total_after_ = to.total;
        return;
    }
    init_frame_ = false;
    if ((frame_ & 0x7F) == 0) {
        uint32_t rt = net_.response_time();
        switch (latency_fudge_) {
            case 1: rt = rt + (rt >> 1); break;
            case 2: rt *= 2; break;
            case 3: rt *= 3; break;
            default: break;
        }
        net_.set_timing({rt + 10, 0xFFFFFFFFu, std::max<uint32_t>(rt * 8 + 15, 0x78)});
        uint16_t avg = proc_frames_ ? static_cast<uint16_t>(proc_ms_ / proc_frames_) : 0;
        if (ext_proc_) {   // W9-01-03: the driver's own processing time (no waits)
            const double a = ext_proc_frames_ ? ext_proc_ms_ / ext_proc_frames_ : 0.0;
            avg = static_cast<uint16_t>(std::min(65535.0, a));
            ext_proc_ms_ = 0.0;
            ext_proc_frames_ = 0;
        }
        Event pt = ev::process_time(cfg_.local_house, avg);
        pt.frame = frame_;
        add_to_outlist(pt);
        proc_ms_ = 0;
        proc_frames_ = 0;
    }
    master_timing();
    if (!fsr_boundary()) {  // CommProtocol 2: only send / wait / execute on FSR boundaries
        net_.service();
        phase_ = Phase::Done;
        return;
    }
    send_packets();
    phase_ = Phase::FrameWait;
    wait_start_ = resend_at_ = now;
    resend_period_ = kFrameSyncResendTicks;
    dialog_after_ = std::max<uint32_t>(to.dialog_frame, net_.response_time() * 3);
    total_after_ = to.total;
}

FrameStatus LockstepSession::poll() {
    if (sticky_ != FrameStatus::Proceed) return sticky_;
    if (phase_ == Phase::Ready || phase_ == Phase::Done || phase_ == Phase::Idle) return FrameStatus::Proceed;

    const uint32_t now = net_.now_ticks();
    if (now - resend_at_ >= resend_period_) {
        send_framesync();
        resend_at_ = now;
    }
    net_.service();
    if (net_.bad_connection() >= 0) {
        // The channel gave up on a peer (Timeout ticks without an ACK). The
        // original records BadConnection and keeps waiting; the total timer
        // below then kicks the slowest peer.
    }
    if (now - wait_start_ >= dialog_after_) dialog_open_ = true;
    if (now - wait_start_ >= total_after_) {
        if (init_frame_) return sticky_ = FrameStatus::Timeout;
        kick_slowest();
        wait_start_ = resend_at_ = now;
        dialog_open_ = false;
    }
    const int r = receive_some();
    if (r == 2) return sticky_ = FrameStatus::ScenarioMismatch;
    if (r == 3) return sticky_ = FrameStatus::DoListFull;

    if (phase_ == Phase::InitWait) {
        if (net_.num_connections() > ready_count_) return FrameStatus::Stall;
        // Initial sync done: their frames restart at 0.
        for (auto& kv : peers_) kv.second.their_frame = 0;
        master_timing();
        if (!fsr_boundary()) {
            net_.service();
            phase_ = Phase::Done;
            return FrameStatus::Proceed;
        }
        send_packets();
        if (frame_ == 0) {
            phase_ = Phase::Done;
            return FrameStatus::Proceed;
        }
        const WaitTimeouts to = wait_timeouts(cfg_.game_mode);
        init_frame_ = false;
        phase_ = Phase::FrameWait;
        wait_start_ = resend_at_ = now;
        resend_period_ = kFrameSyncResendTicks;
        dialog_after_ = std::max<uint32_t>(to.dialog_frame, net_.response_time() * 3);
        total_after_ = to.total;
        return poll();
    }
    // FrameWait
    if (!gate_open()) {
        if (!stall_counted_) {
            stall_counted_ = true;
            bool cmd_stall = false;
            for (const auto& kv : peers_)
                if (kv.second.received < kv.second.announced) cmd_stall = true;
            if (cmd_stall)
                ++stats_.command_stalls;
            else
                ++stats_.frame_stalls;
        }
        return FrameStatus::Stall;
    }
    if (!execute_dolist()) return sticky_ = FrameStatus::OutOfSync;
    phase_ = Phase::Ready;
    return FrameStatus::Proceed;
}

void LockstepSession::end_frame() {
    if (phase_ == Phase::Ready) {
        // Pop executed / stale entries from the head (end of FUN_00647090).
        while (!dolist_.empty() && (dolist_.front().executed() || dolist_.front().frame < frame_))
            dolist_.pop_front();
    }
    phase_ = Phase::Idle;
    ++frame_;
}

// --------------------------------------------------------- test hook
void LockstepSession::send_test_garbage(int kind) {
    // W9-05-12 two-process test (RA2_NET_FUZZ): forged / malformed packets to
    // every peer. Each kind must be dropped by the receivers without any
    // effect on the match (the trails stay identical).
    const int other = cfg_.houses.empty() ? 0 : (cfg_.houses.front() == cfg_.local_house ? cfg_.houses.back()
                                                                                          : cfg_.houses.front());
    std::array<uint8_t, kMaxGamePacket + 8> b{};
    FrameHeader h;
    h.type = T(EventType::FRAMEINFO);
    h.house = static_cast<int8_t>(cfg_.local_house);
    h.frame = stamp_frame(frame_, max_ahead_, fsr_, cfg_.protocol);
    // A real header otherwise: sender frame (Frame - Delay) = our last
    // begun frame with its CRC, so the receiver's CRC compare matches.
    const uint32_t sf = frame_ > 0 ? frame_ - 1 : 0;
    h.crc = crc_hist_[sf & 0xFF];
    h.command_count = static_cast<uint16_t>(cmd_count_);
    h.delay = static_cast<uint8_t>(h.frame - sf);
    size_t len = kFrameHeaderSize;
    switch (kind & 3) {
        case 0:   // another house's name in the header (impersonation)
            h.house = static_cast<int8_t>(other);
            break;
        case 1:   // a frame far beyond the window (would open the gate early)
            h.frame = frame_ + 50000;
            break;
        case 2: {   // a TIMING from a house that is not the master
            ev::Timing t;
            t.desired_fps = 999;
            t.max_ahead = 60000;
            t.frame_send_rate = 200;
            Event e = ev::timing(cfg_.local_house, t);
            const int ws = event_wire_size(e.type, false);
            b[len] = e.type;
            std::memcpy(b.data() + len + 1, e.data.data(), static_cast<size_t>(ws));
            len += 1 + static_cast<size_t>(ws);
            break;
        }
        default: {   // a MEGAMISSION replay naming cache slot 200
            b[len] = T(EventType::MEGAMISSION);
            b[len + 1] = 0;   // count 0: replay
            const int ws = event_wire_size(T(EventType::MEGAMISSION), false);
            std::memset(b.data() + len + 2, 0, static_cast<size_t>(ws));
            b[len + 2 + 6] = 200;
            len += 2 + static_cast<size_t>(ws);
            break;
        }
    }
    h.write(b.data());
    net_.send_private(b.data(), len, false, -1);
    net_.service();
}

// ------------------------------------------------------------------ exit
void LockstepSession::request_exit() {
    queue_local(ev::make(EventType::EXIT, cfg_.local_house));
    exiting_ = true;
}

bool LockstepSession::poll_exit() {
    const uint32_t now = net_.now_ticks();
    if (!exit_sent_) {
        send_packets();
        exit_sent_ = true;
        exit_start_ = exit_resend_ = now;
    }
    if (now - exit_resend_ >= 15) {
        send_framesync();
        exit_resend_ = now;
    }
    net_.service();
    if (now - exit_start_ >= 600) return true;  // "Timed out waiting for end game"
    std::vector<uint8_t> pkt;
    int id = -1;
    for (int i = 0; i < 64; ++i) {
        if (!net_.get_private(pkt, id)) break;
        if (process_packet(id, pkt) == 3) return true;
        net_.service();
    }
    int64_t mf = static_cast<int64_t>(frame_) + 1000;
    for (const auto& kv : peers_) mf = std::min<int64_t>(mf, kv.second.their_frame);
    if (static_cast<int64_t>(frame_) <= static_cast<int64_t>(fsr_) + mf) {
        for (int i = 0; i < 4; ++i) send_framesync();  // "All players have finished"
        return true;
    }
    return false;
}

}  // namespace ra2::net
