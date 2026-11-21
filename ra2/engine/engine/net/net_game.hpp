// engine/net/net_game.hpp -- NET-PLAY: one networked match's lockstep driver.
//
// Glue between the sim's command layer (engine/command.hpp) and the original-
// format scheduler (LockstepSession over a NetManager): local commands go out
// as EventClass records (command_bridge to_events -> OutList, stamped
// Frame = now + MaxAhead by the session), every house's records come back
// from Execute_DoList on the frame they were scheduled for and are re-grouped
// into Commands (from_net). The sim ticks only when step() says Run.
//
// Frame mapping. Session frame F == the sim's command clock F. One step() for
// frame F: begin_frame(crc of the state BEFORE tick F) -> poll -> on Proceed
// the commands of frame F are handed out and the session's frame is ended;
// the caller applies them at the start of tick F and runs the tick. (The
// original's main loop runs logic N, then Queue_AI N; "state before tick
// F+1" is "state after logic F", so the CRC and the event frame line up the
// same way on every peer.)
//
// Sync hash: the caller supplies the hash function; it is called only on
// frames F % hash_every == 0 and the last value is reused in between, so all
// peers feed identical CRCs every frame and the cost is paid once per period.
// W9-01-04: matches use hash_every 1 with the cheap per-frame CRC
// (net_frame_crc), like the original's ComputeFrameCRC every frame.
//
// World-free (only ra2engine::Command), SDL-free: unit-tested in
// ra2assets_tests with two real Worlds over a LoopbackHub.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "command.hpp"
#include "net/global_msg.hpp"
#include "net/lockstep.hpp"
#include "net/net_manager.hpp"

namespace ra2::net {

struct NetGameConfig {
    int local_house = 0;
    std::vector<int> human_houses;                     // every human house, incl. the local one
    std::vector<std::pair<int, PeerAddr>> remotes;     // remote human house -> address
    bool master = false;                               // the host (sends TIMING)
    int master_house = -1;                             // W9-05-12: the host's house (-1 unknown)
    GameMode mode = GameMode::LAN;                     // LAN (direct) / Internet (tunnel)
    // Start values (docs/re/net-protocol.md §8): LAN FSR 5, MaxAhead 10;
    // the master's TIMING events adapt both to the measured round trip.
    uint32_t frame_send_rate = 5;
    uint32_t max_ahead = 10;
    // The TIMING fps cap the master computes with (global GameSpeed 0xA8DAC8):
    // 0 -> 60, 1 -> 45, n -> 60 / n fps. W9-01-01: a match is set from the
    // lobby's Game Speed (default 1 = 45 fps); 4 (= 15 fps) stays the default
    // for the unit tests' fixed-step drivers.
    int game_speed = 4;
    int hash_every = 1;                                // sync CRC period (frames; 1 = every frame)
    int crc_check_from_frame = -1;                     // -1: the original 256-frame grace
    uint32_t init_patience_ms = 180000;                // frame 0: how long to wait for slow loaders
};

struct NetChat {
    int house = -1;            // sender's house (-1 unknown address)
    std::string sender;        // the name in the packet
    std::string text;          // UTF-8
    int color = 0;             // sender colour (lobby colour index)
};

class NetGame {
public:
    NetGame(NetManager& net, const NetGameConfig& cfg);
    NetGame(const NetGame&) = delete;
    NetGame& operator=(const NetGame&) = delete;

    // A local player action: stamped with the local house, sent as EventClass
    // records; it comes back from step() on its execution frame. False when
    // the OutList is full (the original drops it the same way).
    bool issue(const ra2engine::Command& c);

    enum class Step { Run, Stall, OutOfSync, ScenarioMismatch, Timeout, Failed };
    Step step(const std::function<uint32_t()>& state_hash, std::vector<ra2engine::Command>& out);
    static const char* step_name(Step s);

    uint32_t frame() const { return session_.frame(); }
    LockstepSession& session() { return session_; }
    const LockstepSession& session() const { return session_; }
    NetManager& net() { return net_; }
    const NetGameConfig& config() const { return cfg_; }
    // The CRC fed for frame F (last 256 frames).
    uint32_t crc_of(uint32_t frame) const { return session_.frame_crc_history(frame); }
    uint32_t last_hash() const { return last_hash_; }
    uint32_t hashes_computed() const { return hashes_; }

    // Leaving: EXIT goes out like any command; poll_exit() until true
    // (FUN_00647DE0: every peer passed the exit frame, or 600 ticks).
    void request_exit();
    bool poll_exit();
    bool exiting() const { return exiting_; }

    // Players the session removed since the last call: (house, error);
    // error 0 = left (EXIT), 1 = connection lost (REMOVEPLAYER queued).
    std::vector<std::pair<int, int>> take_removed();
    // Drop a player we are waiting on (the stall dialog's button): the
    // connection is destroyed and REMOVEPLAYER is scheduled for everyone.
    void drop_player(int house);
    bool is_remote(int house) const;

    // --- out-of-band messages on the global channel (never in the sync) ---
    // W9-05-07: `to` = the recipient houses (nullptr = every remote player;
    // the original sends one DATA_ACK copy to each chosen listener).
    void send_chat(const std::string& sender, const std::string& utf8, int color,
                   const std::vector<int>* to = nullptr);
    // W9-05-08: a beacon packet (place / delete / text) to every other player.
    void send_beacon(const BeaconMsg& m);
    void send_away(bool back, uint32_t seconds);
    // Drains the global channel: chat lines and away notices.
    void poll_global(std::vector<NetChat>& chat, std::vector<AwayNotice>& away);
    // ... and the beacon packets (house taken from the sender's address).
    void poll_global(std::vector<NetChat>& chat, std::vector<AwayNotice>& away, std::vector<BeaconMsg>& beacons);
    int house_of(const PeerAddr& a) const;
    // W9-05-12: REMOVEPLAYER events dropped by removeplayer_allowed.
    uint32_t rejected_removes() const { return rejected_removes_; }
    bool removeplayer_allowed(int sender, int target) const;

private:
    NetManager& net_;
    NetGameConfig cfg_;
    LockstepSession session_;
    bool begun_ = false;
    bool have_hash_ = false;
    uint32_t last_hash_ = 0;
    uint32_t hashes_ = 0;
    bool exiting_ = false;
    bool init_wait_started_ = false;
    uint32_t init_wait_since_ = 0;
    std::vector<std::pair<int, int>> removed_;
    uint32_t rejected_removes_ = 0;
};

}  // namespace ra2::net
