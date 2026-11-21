// engine/net/channel.hpp -- the original's reliable-UDP channel layer.
//
// Two channel kinds share one send-queue engine, exactly like gamemd's class
// tree (ConnectionClass -> IPXConnClass -> IPXGlobalConnClass):
//
//  PrivateChannel  (one per remote player, magic = ProductID 0xAA04)
//    14-byte header, built in Send_Packet FUN_0048BE50, parsed in
//    Receive_Packet FUN_0048BF50:
//      +0 u16 magic   +2 u8 code (0 DATA_ACK, 1 DATA_NOACK, 2 ACK)
//      +3 u8 router byte (0)   +4 u32 PacketID   +8 6 bytes (unwritten -> 0)
//    Separate PacketID counters for DATA_ACK (conn+0x40) and DATA_NOACK
//    (conn+0x3C); in-order high-water mark LastSeqID (conn[0x11], -1 = none).
//
//  GlobalChannel   (one per manager, magic 0x1236; lobby, chat, progress...)
//    16-byte header FUN_0053F900:
//      +0 u16 0x1236 +2 u8 code (0 if ack requested AND addressed, else 1)
//      +3 u8 0 +4 u32 PacketID (= packets queued so far) +8 u32 0 +12 u16 0
//      +14 u16 ProductID
//    Duplicates filtered with a ring of (address, PacketID) (FUN_0053F9E0),
//    ring size = 2 x queue size.
//
// Reliability (Service_Send_Queue FUN_0048C2F0):
//  * pass 1: drop entries flagged done (ACKed DATA_ACK, or a sent NOACK); an
//    ACKed DATA_ACK contributes an RTT sample (now - first send) FUN_0048B920;
//  * pass 2: every entry whose (now - last send) > RetryDelta is (re)sent;
//    NOACK entries are flagged done after their first send; an entry fails
//    once send count > MaxRetries or (last - first send) > Timeout, which
//    marks the connection bad.
// Time is in the original's ticks: timeGetTime() >> 4 (FUN_006C82C0).
#pragma once

#include "net/udp_socket.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

namespace ra2::net {

constexpr uint16_t kProductIdYR = 0xAA04;   // gamemd.exe, push at 0x4E7FB0
constexpr uint16_t kProductIdRA2 = 0xAA03;  // game.exe (RA2), file offset 0xD48D5
constexpr uint16_t kGlobalMagic = 0x1236;
constexpr uint16_t kRouterMagic = 0xCDEF;   // WOL packet router (never used here)

enum PacketCode : uint8_t { kCodeDataAck = 0, kCodeDataNoAck = 1, kCodeAck = 2 };

constexpr size_t kPrivateHeaderSize = 14;
constexpr size_t kGlobalHeaderSize = 16;
constexpr size_t kPrivateMaxPayload = 0x22E;  // 558, IPXManager ctor arg (0x4E7FC1)
constexpr size_t kGlobalMaxPayload = 0x1C7;   // 455, IPXManager ctor arg (0x4E7FC6)
constexpr int kPrivateQueueSize = 0x20;       // 32 packets each way (0x4E7FBA)
constexpr int kGlobalQueueSize = 0xA0;        // 160 packets each way (0x4E7FBC)
constexpr int kMaxPrivateConnections = 7;     // Create_Connection FUN_00540AF0

// FUN_006C82C0: the network layer's clock is timeGetTime() >> 4.
inline uint32_t ticks_from_ms(uint32_t ms) { return ms >> 4; }

// Set_Timing(RetryDelta, MaxRetries, Timeout); -1 = unlimited. Defaults are
// the IPXManager ctor's (FUN_00540620: 2, -1, 60).
struct ChannelTiming {
    uint32_t retry_delta = 2;
    uint32_t max_retries = 0xFFFFFFFFu;
    uint32_t timeout = 60;
};

// A destination: a UDP endpoint, or (tunnel mode) a tunnel peer id.
struct PeerAddr {
    NetAddr addr;
    uint32_t tunnel_id = 0;
    bool operator==(const PeerAddr& o) const { return addr == o.addr && tunnel_id == o.tunnel_id; }
    bool operator!=(const PeerAddr& o) const { return !(*this == o); }
};

struct ChannelStats {
    uint32_t rtt_count = 0, rtt_sum = 0, rtt_avg = 0, rtt_max = 0;  // FUN_0048B920
    uint32_t resends = 0;          // conn[2]
    uint32_t bad_magic = 0;        // conn[6]
    uint32_t dropped = 0;          // conn[5] (queue full)
    uint32_t received_ack = 0;     // NumRecAck  conn[0xE]
    uint32_t received_noack = 0;   // NumRecNoAck conn[0xD]
    uint32_t lost_noack = 0;       // conn[3]
    uint32_t pct_lost = 0;         // conn[4]
};

// Emits one raw channel packet (header + payload) to `to`.
using ChannelEmit = std::function<void(const PeerAddr& to, const uint8_t* pkt, size_t len)>;

struct ReceivedPacket {
    PeerAddr from;
    uint8_t code = 0;
    uint32_t id = 0;
    uint16_t product = 0;          // global channel only (header +14)
    std::vector<uint8_t> payload;  // header stripped
    bool read = false;             // handed to the game; kept for dedupe until retired
};

// Shared queue machinery (ConnectionClass + CommBufferClass).
class ChannelBase {
public:
    ChannelBase(uint16_t magic, size_t header_size, size_t max_payload, int queue_size);
    virtual ~ChannelBase() = default;

    void init();  // ConnectionClass::Init: counters 0, LastSeqID = -1, queues empty
    void set_timing(const ChannelTiming& t) { timing_ = t; }
    const ChannelTiming& timing() const { return timing_; }

    // Service_Send_Queue. Returns false when an entry ran out of retries /
    // time ("bad connection").
    bool service(uint32_t now_ticks, const ChannelEmit& emit);

    // Get_Packet (0x48C230): oldest deliverable unread packet; it is marked
    // read and stays queued until Service_Receive_Queue retires it.
    bool pop_received(ReceivedPacket& out);
    size_t received_pending() const;  // unread packets
    size_t receive_queue_size() const { return recv_.size(); }
    size_t send_pending() const { return send_.size(); }

    uint32_t response_time() const { return stats_.rtt_avg; }  // FUN_0048B990 (ticks)
    const ChannelStats& stats() const { return stats_; }
    uint16_t magic() const { return magic_; }

protected:
    struct SendEntry {
        bool done = false;     // flag 2 (ACKed / NOACK sent)
        bool failed = false;   // flag 4
        uint32_t first_time = 0, last_time = 0, send_count = 0;
        PeerAddr to;
        std::vector<uint8_t> pkt;  // header + payload
    };
    bool queue_send(std::vector<uint8_t>&& pkt, const PeerAddr& to);  // FUN_0048B320
    bool queue_receive(ReceivedPacket&& p);                          // FUN_0048B660
    void ack_sent_packet(uint32_t id);  // mark the DATA_ACK entry with this id done
    void rtt_sample(uint32_t ticks);    // FUN_0048B920
    // Service_Receive_Queue: a read packet stays queued (it still dedupes
    // retransmissions) until it is "dead".
    virtual bool dead(const ReceivedPacket& r) const { return r.read; }
    virtual bool deliverable(const ReceivedPacket&) const { return true; }
    virtual void on_delivered(const ReceivedPacket&) {}
    // An entry ran out of retries / time. true = retire it now (the clone's
    // global channel: one dead lobby peer must not fill the 160-entry queue);
    // false = keep it (private channels: the original keeps resending and
    // reports the bad connection to the scheduler).
    virtual bool retire_failed(const SendEntry&) { return false; }

    uint16_t magic_;
    size_t header_size_;
    size_t max_payload_;
    int queue_size_;
    ChannelTiming timing_;
    ChannelStats stats_;
    std::deque<SendEntry> send_;
    std::deque<ReceivedPacket> recv_;
    uint32_t total_queued_ = 0;    // CommBuffer +0x2C (global PacketID source)
    uint32_t total_received_ = 0;  // CommBuffer +0x3C
};

// IPXConnClass: one remote player. All packets go to `peer`.
class PrivateChannel : public ChannelBase {
public:
    PrivateChannel(int id, const PeerAddr& peer, uint16_t product = kProductIdYR);

    int id() const { return id_; }
    const PeerAddr& peer() const { return peer_; }
    void set_peer(const PeerAddr& p) { peer_ = p; }

    // Send_Packet FUN_0048BE50: header + queue. false = queue full / too big.
    bool send(const uint8_t* payload, size_t len, bool ack);
    // Receive_Packet FUN_0048BF50. Emits the ACK for DATA_ACK packets.
    bool receive(const uint8_t* pkt, size_t len, const ChannelEmit& emit);

    uint32_t last_seq_id() const { return last_seq_; }

protected:
    // Service_Receive_Queue 0x48C4B0: dead = read, and NOACK or id < LastSeqID.
    bool dead(const ReceivedPacket& r) const override {
        return r.read && (r.code == kCodeDataNoAck || r.id < last_seq_);
    }
    // Get_Packet 0x48C230: DATA_ACK packets are delivered strictly in
    // PacketID order (id == LastReadID + 1); NOACK packets as they come.
    bool deliverable(const ReceivedPacket& r) const override {
        return r.code == kCodeDataNoAck || (r.code == kCodeDataAck && r.id == last_read_ + 1);
    }
    void on_delivered(const ReceivedPacket& r) override {
        if (r.code == kCodeDataAck) last_read_ = r.id;
    }

private:
    int id_;
    PeerAddr peer_;
    uint32_t num_send_ack_ = 0;    // conn+0x40
    uint32_t num_send_noack_ = 0;  // conn+0x3C
    uint32_t last_seq_ = 0xFFFFFFFFu;   // conn+0x44
    uint32_t last_read_ = 0xFFFFFFFFu;  // conn+0x48
};

// IPXGlobalConnClass (magic 0x1236).
class GlobalChannel : public ChannelBase {
public:
    explicit GlobalChannel(uint16_t product = kProductIdYR, int queue_size = kGlobalQueueSize);

    // Send_Packet FUN_0053F900. `to == nullptr` means broadcast (always NOACK).
    bool send(const uint8_t* payload, size_t len, bool ack, const PeerAddr* to,
              const PeerAddr& broadcast_to);
    // Receive_Packet FUN_0053F9E0.
    bool receive(const uint8_t* pkt, size_t len, const PeerAddr& from, const ChannelEmit& emit);

    uint16_t product() const { return product_; }

    // NET-PLAY (clone): per-destination bookkeeping the lobby needs.
    // true while a DATA_ACK packet to `to` is still waiting for its ACK.
    bool pending_to(const PeerAddr& to) const;
    // Forget every queued packet to `to` (a peer that left).
    void drop_pending_to(const PeerAddr& to);
    // Destinations whose DATA_ACK packet failed (ran out of retries / time)
    // since the last call. Failed entries are retired.
    std::vector<PeerAddr> take_failed();

protected:
    bool retire_failed(const SendEntry& e) override;

private:
    std::vector<PeerAddr> failed_;
    struct RingEntry {
        PeerAddr from;
        uint32_t id = 0xFFFFFFFFu;
        bool valid = false;
    };
    uint16_t product_;
    std::vector<RingEntry> ring_;
    size_t ring_pos_ = 0;
};

}  // namespace ra2::net
