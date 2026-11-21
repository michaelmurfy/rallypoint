// engine/net/lockstep.hpp -- the original's multiplayer frame scheduler.
//
// A non-blocking port of Queue_AI_Multiplayer FUN_00647090, Send_Packets
// FUN_00649710, Send_FrameSync FUN_00649A70, Process_Receive_Packet
// FUN_00649B30, Wait_For_Players FUN_00648180 (as a poll), Execute_DoList
// FUN_0064BDF0 (ordering + the network-level events), the timeout kick
// FUN_0064AE80 / Kick_Player FUN_0064ACB0, Destroy_Connection FUN_005DA170
// (REMOVEPLAYER), and the exit wait FUN_00647DE0. The CnCNet spawner's
// "Protocol 0" latency levels (ResponseTime2) are an option.
//
// Per game frame N the caller does (original main loop FUN_0055CFF0 order):
//   sim.logic_step();                        // frame N's logic (0x55AC40)
//   s.begin_frame(frame_crc_of_state);        // ComputeFrameCRC FUN_0064D520 hook
//   while ((st = s.poll()) == FrameStatus::Stall) { pump UI / wait }
//   if (st == FrameStatus::Proceed) for (auto& x : s.executed()) sim.apply(x);
//   s.end_frame();                           // pop executed, ++Frame
// The frame CRC is supplied by the caller (the clone currently plugs a World
// hash; the original's ComputeFrameCRC can replace it later).
#pragma once

#include "net/event.hpp"
#include "net/event_codec.hpp"
#include "net/net_manager.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <vector>

namespace ra2::net {

// Session.GameMode values that reach the scheduler.
enum class GameMode : int { Serial = 1, Modem = 2, LAN = 3, Internet = 4 };

// Wait timeouts table at 0x836338 ([GameMode][4], 60 Hz ticks):
// resend (init), dialog (init), dialog (frame), total.
struct WaitTimeouts {
    uint32_t resend_init, dialog_init, dialog_frame, total;
};
WaitTimeouts wait_timeouts(GameMode m);
// Per-frame FRAMESYNC resend while stalled (0xB4 at the 0x647AF1 call site).
constexpr uint32_t kFrameSyncResendTicks = 180;

struct LockstepConfig {
    GameMode game_mode = GameMode::LAN;
    int protocol = 2;                    // Session.CommProtocol (0 raw, 1, 2 = FSR-quantised)
    bool spawner_protocol_zero = false;  // CnCNet spawn.ini Protocol=0
    uint8_t max_latency_level = 0xFF;    // spawn.ini MaxLatencyLevel
    bool spawner_ext = false;            // accept event 0x30 (implied by protocol zero)
    int local_house = 0;                 // PlayerPtr->ArrayIndex
    std::vector<int> houses;             // every human house index (incl. local)
    bool master = false;                 // Am_I_Master FUN_00697880
    // W9-05-12 (clone hardening): the master's house. TIMING / RESPONSE_TIME /
    // LATENCYFUDGE from any other house are dropped. -1 = unknown (accept all,
    // the original's trust).
    int master_house = -1;
    uint32_t max_ahead = 10;             // LAN start: max(10, roundup(RT/8, FSR))
    uint32_t frame_send_rate = 5;        // LAN start: 5
    int game_speed = 0;                  // global GameSpeed 0xA8DAC8 (TIMING fps cap)
    bool fog_of_war = false;             // Scenario SpecialFlags bit 12 (TIMING +/-10)
    int latency_fudge = 0;               // 0xA8CB04
    uint32_t precalc_max_ahead = 0;      // Session.PrecalcMaxAhead (0xA8A4D4)
    uint32_t precalc_fps = 0;            // Session.PrecalcFPS (0xA8A4D8)
    bool load_game = false;              // Session.LoadGame: ScenarioMagic = 0
    uint32_t start_frame = 0;            // Frame at session start (0, or the save's frame)
    // -1: original behaviour (no CRC compare during the first start+256 frames,
    // the CDTimer at 0xAF93B8). >= 0: compare from that frame on (tests).
    int crc_check_from_frame = -1;
};

enum class FrameStatus {
    Proceed,           // execute executed() and call end_frame()
    Stall,             // waiting for peers; call poll() again
    ScenarioMismatch,  // Wait_For_Players 2: TXT_SCENARIOS_DO_NOT_MATCH
    DoListFull,        // 3
    Timeout,           // 7: initial sync timed out
    OutOfSync,         // Execute_DoList 0: TXT_OUT_OF_SYNC
};

// One event the game must execute, in the original's order (per house in
// HouseClass::Array order; within a house DoList order; that house's
// megamissions last, grouped).
struct ExecItem {
    Event ev;
    int group = -1;       // MEGAMISSION group id within this frame (FUN_0064C810 run), -1 none
    int group_size = 0;
};

struct PeerState {  // 0xAF92C0, stride 0x18
    int32_t their_frame = -1;   // [0] max(pkt.Frame - pkt.Delay)
    uint32_t announced = 0;     // [1] max CommandCount seen
    uint32_t received = 0;      // [2] commands received
    uint32_t rtt = 0x14;        // [3]
    uint32_t last_heard = 0;    // [5]
    int32_t node_time = -1;     // NodeNameType.Time (+0x73), from PROCESS_TIME
};

struct OutOfSyncInfo {
    int house = -1;
    uint32_t frame = 0;       // the sender's frame (Frame - Delay)
    uint32_t local_crc = 0, remote_crc = 0;
};

struct LockstepStats {
    uint32_t frame_stalls = 0;    // MPStats +0x54
    uint32_t command_stalls = 0;  // MPStats +0x58
    uint32_t packets_sent = 0;    // 0xB037E4
    uint32_t late_events = 0;     // "Packet received too late!"
    uint32_t moved_events = 0;    // "DoList: Moving event from frame"
    // W9-05-12 (clone hardening; the original trusted its peers): packets /
    // events dropped because their house was not the sending connection's,
    // their frame was outside the window, or a timing event came from a
    // house other than the master.
    uint32_t rejected_packets = 0;
    uint32_t rejected_events = 0;
};

// W9-05-12: no honest peer is ever further ahead of us than MaxAhead (<= 255,
// the u8 header Delay) plus one FSR; a frame beyond this window is forged.
constexpr int32_t kFrameWindow = 1024;
// The TIMING clamps: MaxAhead travels in the u8 header Delay, the original's
// master never sends FSR > 10 (spawner levels <= 9), fps <= 60.
constexpr uint32_t kMaxMaxAhead = 255;
constexpr uint32_t kMaxFrameSendRate = 30;

class LockstepSession {
public:
    LockstepSession(NetManager& net, const LockstepConfig& cfg);

    // OutList.Add (QueueClass<EventClass,128>). House/frame are stamped at send.
    bool queue_local(const Event& e);
    size_t outlist_size() const { return outlist_.size(); }

    void begin_frame(uint32_t frame_crc);
    FrameStatus poll();
    const std::vector<ExecItem>& executed() const { return exec_; }
    void end_frame();

    // Local quit: queue EXIT, then poll_exit() until true (FUN_00647DE0:
    // FRAMESYNC every 15 ticks, give up after 600).
    void request_exit();
    bool poll_exit();

    uint32_t frame() const { return frame_; }
    uint32_t max_ahead() const { return max_ahead_; }
    uint32_t frame_send_rate() const { return fsr_; }
    uint32_t desired_fps() const { return desired_fps_; }
    uint32_t commands_sent() const { return cmd_count_; }
    uint32_t scenario_magic() const { return scenario_magic_; }
    int latency_fudge() const { return latency_fudge_; }
    uint8_t latency_level() const { return p0_level_; }
    bool reconnect_dialog() const { return dialog_open_; }  // stall passed the dialog timeout
    int slowest_peer() const;                                 // house we wait on, -1 none
    // W9-01-03 [RE: FUN_0055CFF0 @0x55D1C8: the largest per-connection lag
    // 0xA8CAE4[]] [ANCHORED: the clone's lag = our frame minus the peer's
    // reported frame]: how many frames the most-behind peer trails us (0 none).
    uint32_t peer_lag_frames() const;
    int players() const { return mplayer_count_; }            // Session.MPlayerCount
    const std::map<int, PeerState>& peers() const { return peers_; }
    const OutOfSyncInfo& out_of_sync() const { return oos_; }
    const LockstepStats& stats() const { return stats_; }
    uint32_t frame_crc_history(uint32_t frame) const { return crc_hist_[frame & 0xFF]; }

    // Average processing ms per frame for PROCESS_TIME (ProcessTicks /
    // ProcessFrames, main loop accounting). Default: measured internally.
    void set_process_time_sample(uint32_t total_ms, uint32_t frames);
    // W9-01-03 [RE: main loop FUN_0055CFF0 -- ProcessTicks accumulates from
    // the loop start 0x55D04E to just before Queue_AI 0x55DA5B, i.e. the
    // frame's own work WITHOUT the frame-limiter wait and WITHOUT the
    // Wait_For_Players stall inside Queue_AI]: the driver reports each
    // executed frame's processing time. Once called, the internal sample
    // (wall time between begin_frame calls, which counted every wait and so
    // could only ever report the current rate) is no longer used.
    void note_process_time(double ms);
    // W9-01-07 [RE: EventClass::Execute FUN_004C6BD0 case GAMESPEED 0x0D @
    // 0x4C786E: GameSpeed 0xA8DAC8 = the event's value, on every peer at the
    // same frame]: the master's TIMING cap follows it from then on.
    void set_game_speed(int speed) { cfg_.game_speed = speed; }
    int game_speed() const { return cfg_.game_speed; }

    // Destroy_Connection(id, error): error 1 = lost (queues REMOVEPLAYER when
    // frame > 0), 0 = left (EXIT executed).
    void destroy_connection(int house, int error);
    std::function<void(int house, int error)> on_player_removed;

    // NET-PLAY (clone): a peer announced it is away (Android background,
    // 05-transport-infra.md §7.2). Restarts the stall timers so neither the
    // reconnect dialog nor the timeout kick fires while its countdown runs.
    void hold_wait();

    // W9-05-12 test hook (RA2_NET_FUZZ two-process run): sends one forged /
    // malformed game packet of kind 0..3 to every peer, which must drop it.
    void send_test_garbage(int kind);

private:
    bool fsr_boundary() const;
    void initial_reset();
    void send_packets();                      // FUN_00649710
    void send_framesync();                    // FUN_00649A70
    int process_packet(int conn_id, const std::vector<uint8_t>& pkt);  // FUN_00649B30
    int receive_some();                       // <= 64 packets
    void master_timing();                     // 0x64761C..0x6479C4
    void protocol_zero_send();                // spawner SendResponseTime2
    void protocol_zero_handle(const Event& e);
    bool execute_dolist();                    // FUN_0064BDF0
    void execute_net_event(const Event& e);   // network cases of FUN_004C6BD0
    void kick_slowest();
    bool add_to_outlist(const Event& e);
    bool gate_open() const;

    NetManager& net_;
    LockstepConfig cfg_;
    EventEncoder enc_;
    EventDecoder dec_;
    std::deque<Event> outlist_;
    std::deque<Event> dolist_;
    std::map<int, PeerState> peers_;
    std::vector<ExecItem> exec_;

    uint32_t frame_ = 0;
    // desired_fps_ starts at 30 [RE: scenario/session init FUN_0052D6E0 @
    // 0x52D7FD: DesiredFrameRate 0xA8A4C0 = 0x1E] until the master's first
    // TIMING executes.
    uint32_t max_ahead_, fsr_, desired_fps_ = 30, max_max_ahead_ = 0;
    uint32_t precalc_max_ahead_, precalc_fps_;
    int latency_fudge_;
    uint32_t cur_crc_ = 0;
    std::array<uint32_t, 256> crc_hist_{};
    uint32_t scenario_magic_ = 0;
    uint32_t cmd_count_ = 0;      // 0xAF936A (u16 on the wire)
    int mplayer_count_ = 1;
    int32_t move_from_ = 0, move_to_ = 0;  // 0xA8A160 / 0xA8A144
    int32_t crc_timer_start_ = 0, crc_timer_dur_ = 0;  // 0xAF93B8 / 0xAF93C0
    int32_t local_node_time_ = -1;

    // per-frame wait state
    enum class Phase { Idle, InitWait, FrameWait, Ready, Done } phase_ = Phase::Idle;
    bool init_frame_ = false;
    int ready_count_ = 0;
    uint32_t wait_start_ = 0, resend_at_ = 0, resend_period_ = 0, dialog_after_ = 0, total_after_ = 0;
    uint32_t framesync_sent_at_ = 0;
    bool dialog_open_ = false;
    bool stall_counted_ = false;
    FrameStatus sticky_ = FrameStatus::Proceed;

    // process time accounting
    uint32_t proc_ms_ = 0, proc_frames_ = 0, last_begin_ms_ = 0;
    bool have_last_begin_ = false;
    bool ext_proc_ = false;                    // W9-01-03: the driver reports process time
    double ext_proc_ms_ = 0.0;
    uint32_t ext_proc_frames_ = 0;

    // exit
    bool exiting_ = false;
    uint32_t exit_start_ = 0, exit_resend_ = 0;
    bool exit_sent_ = false;

    // protocol zero (spawner)
    int p0_next_send_ = -1;
    int p0_worst_max_ahead_ = 24;
    uint8_t p0_level_ = 0;
    uint8_t p0_new_fsr_ = 3;
    std::array<int32_t, 8> p0_max_ahead_{}, p0_last_frame_{};
    std::array<uint8_t, 8> p0_mode_{};

    OutOfSyncInfo oos_;
    LockstepStats stats_;
    std::array<uint8_t, kMaxGamePacket + 8> pkt_{};  // send buffer (+ the 0xA8C9A8 overrun area)
};

}  // namespace ra2::net
