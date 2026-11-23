// engine/net/net_manager.hpp -- IPXManagerClass equivalent: owns the private
// channels (one per remote player) and the global channel, puts the datagram
// checksum on every packet, and demultiplexes received datagrams on their
// first u16 (FUN_00541550):
//   0x1236          -> global channel
//   ProductID       -> the private channel whose peer address matches
// Below it sits a DatagramIO: real UDP (optionally through a CnCNet V2/V3
// tunnel) or an in-process loopback hub for tests.
#pragma once

#include "net/channel.hpp"
#include "net/udp_socket.hpp"
#include "net/wire.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <random>
#include <vector>

namespace ra2::net {

// Raw datagram transport (the bytes after the tunnel header: [crc][packet]).
class DatagramIO {
public:
    virtual ~DatagramIO() = default;
    virtual bool send(const PeerAddr& to, const uint8_t* data, size_t len) = 0;
    // false = nothing pending.
    virtual bool recv(PeerAddr& from, std::vector<uint8_t>& data) = 0;
};

// UDP socket transport. Direct mode: PeerAddr::addr is the destination.
// Tunnel mode (CnCNet V2/V3, or our relay speaking the same header): every
// datagram goes to the tunnel server wrapped as [local_id][peer.tunnel_id];
// received datagrams addressed to another id are dropped, and the sender is
// reported as PeerAddr{server, from_id}.
class UdpDatagramIO : public DatagramIO {
public:
    bool open(uint16_t port, uint32_t bind_ip = 0) { return sock_.open(port, bind_ip); }
    void set_tunnel(TunnelMode mode, const NetAddr& server, uint32_t local_id) {
        mode_ = mode;
        server_ = server;
        local_id_ = local_id;
    }
    TunnelMode tunnel_mode() const { return mode_; }
    UdpSocket& socket() { return sock_; }

    bool send(const PeerAddr& to, const uint8_t* data, size_t len) override;
    bool recv(PeerAddr& from, std::vector<uint8_t>& data) override;

    uint32_t dropped_foreign() const { return dropped_foreign_; }
    // NET-PLAY: sees every datagram that is not for this endpoint (tunnel
    // mode: wrong receiver id or too short -- e.g. a tunnel ping reply).
    std::function<void(const NetAddr& from, const uint8_t* data, size_t len)> on_foreign;

private:
    UdpSocket sock_;
    TunnelMode mode_ = TunnelMode::None;
    NetAddr server_;
    uint32_t local_id_ = 0;
    std::vector<uint8_t> scratch_;
    uint32_t dropped_foreign_ = 0;
};

// In-process network for tests: endpoints exchange datagrams through a hub
// with optional seeded loss / duplication / extra latency. Time comes from
// the caller's clock (milliseconds).
class LoopbackHub {
public:
    explicit LoopbackHub(std::function<uint32_t()> clock_ms, uint32_t seed = 1)
        : clock_(std::move(clock_ms)), rng_(seed) {}

    class Endpoint : public DatagramIO {
    public:
        Endpoint(LoopbackHub* hub, const PeerAddr& self) : hub_(hub), self_(self) {}
        bool send(const PeerAddr& to, const uint8_t* data, size_t len) override;
        bool recv(PeerAddr& from, std::vector<uint8_t>& data) override;
        const PeerAddr& self() const { return self_; }

    private:
        LoopbackHub* hub_;
        PeerAddr self_;
    };

    Endpoint* create(const PeerAddr& self);
    void set_loss(double p) { loss_ = p; }
    void set_duplicate(double p) { dup_ = p; }
    void set_latency(uint32_t min_ms, uint32_t max_ms) {
        lat_min_ = min_ms;
        lat_max_ = max_ms;
    }
    // Cut / restore all traffic to and from `who` (simulates a dropped peer).
    void set_partitioned(const PeerAddr& who, bool cut);
    uint64_t delivered() const { return delivered_; }
    uint64_t dropped() const { return dropped_; }

private:
    struct InFlight {
        uint32_t due_ms;
        uint64_t seq;
        PeerAddr from, to;
        std::vector<uint8_t> data;
    };
    friend class Endpoint;
    bool cut(const PeerAddr& a) const;
    std::function<uint32_t()> clock_;
    std::mt19937 rng_;
    std::vector<std::unique_ptr<Endpoint>> endpoints_;
    std::vector<PeerAddr> partitioned_;
    std::deque<InFlight> flight_;
    uint64_t seq_ = 0, delivered_ = 0, dropped_ = 0;
    double loss_ = 0.0, dup_ = 0.0;
    uint32_t lat_min_ = 0, lat_max_ = 0;
};

class NetManager {
public:
    using ClockMs = std::function<uint32_t()>;

    NetManager(DatagramIO& io, ClockMs clock_ms, uint16_t product = kProductIdYR);

    // Create_Connection FUN_00540AF0 (max 7). `id` is the remote HouseIndex.
    bool create_connection(int id, const PeerAddr& peer);
    // Delete_Connection FUN_00540C30.
    bool destroy_connection(int id);
    int num_connections() const { return static_cast<int>(conns_.size()); }
    int connection_id(int index) const;   // vtable +0x14
    int connection_index(int id) const;   // vtable +0x18 (-1 = none)
    PrivateChannel* connection(int id);
    const PrivateChannel* connection(int id) const;

    // Set_Timing FUN_00540990: applies to every channel, present and future.
    void set_timing(const ChannelTiming& t);
    const ChannelTiming& timing() const { return timing_; }

    // Send_Private_Message: to one connection, or all of them (conn_id -1).
    bool send_private(const uint8_t* payload, size_t len, bool ack, int conn_id = -1);
    // Get_Private_Message: next packet from any connection (index order).
    bool get_private(std::vector<uint8_t>& payload, int& conn_id);

    // Send_Global_Message FUN_00540E20. to == nullptr: broadcast address.
    bool send_global(const uint8_t* payload, size_t len, bool ack, const PeerAddr* to);
    bool get_global(ReceivedPacket& out);
    void set_broadcast(const PeerAddr& b) { broadcast_ = b; }
    GlobalChannel& global() { return global_; }

    // IPXManagerClass::Service FUN_00541550: receive + demux all pending
    // datagrams, then service every send queue.
    void service();
    // Connection id whose send queue failed in the last service (-1: none).
    int bad_connection() const { return bad_conn_; }
    // Response_Time FUN_00542180: worst average RTT over private channels (ticks).
    uint32_t response_time() const;
    uint32_t now_ticks() const { return ticks_from_ms(clock_()); }
    uint32_t now_ms() const { return clock_(); }

    // NAT fix-up from FUN_00541550: an Internet game rebinds a player's
    // address when a NOACK FRAMESYNC arrives from an unknown endpoint.
    void set_rebind_on_framesync(bool on) { rebind_ = on; }

    uint32_t crc_errors() const { return crc_errors_; }
    uint32_t unknown_packets() const { return unknown_; }
    uint16_t product() const { return product_; }

private:
    void emit(const PeerAddr& to, const uint8_t* pkt, size_t len);
    DatagramIO& io_;
    ClockMs clock_;
    uint16_t product_;
    ChannelTiming timing_;
    std::vector<std::unique_ptr<PrivateChannel>> conns_;
    GlobalChannel global_;
    PeerAddr broadcast_;
    int bad_conn_ = -1;
    bool rebind_ = false;
    uint32_t crc_errors_ = 0, unknown_ = 0;
    std::vector<uint8_t> scratch_;
};

}  // namespace ra2::net
