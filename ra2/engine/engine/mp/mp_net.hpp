#pragma once
// engine/mp/mp_net -- the multiplayer session's ONE network endpoint.
//
// The lobby and the match share it: engine/net's UdpDatagramIO (plain UDP,
// or the CnCNet V3 tunnel header) under a NetManager, whose 0x1236 global
// channel carries the lobby (and, in game, chat / away notices) and whose
// private channels carry the lockstep packets once the match starts. So the
// game keeps the very socket / tunnel id the lobby exchanged, and there is a
// single implementation of the socket, the datagram CRC, the tunnel header
// and the global channel's ACK / resend / dedupe (engine/net/).
//
// Peers are named by a 64-bit PeerId: ip:port (direct) or a V3 tunnel id
// with the top bit set. SDL-free (ra2assets_tests).
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "mp/mp_socket.hpp"
#include "net/net_manager.hpp"

namespace ra2mp {

using PeerId = std::uint64_t;
constexpr PeerId kTunnelPeerFlag = 1ull << 63;
inline PeerId peer_of(const Addr& a) { return a.key(); }
inline PeerId peer_of_v3(std::uint32_t v3) { return kTunnelPeerFlag | v3; }

class MpNet {
public:
    MpNet() = default;
    MpNet(const MpNet&) = delete;
    MpNet& operator=(const MpNet&) = delete;

    // LAN / Join by IP: a direct UDP socket on `port` (0 = ephemeral).
    bool open_direct(std::uint16_t port);
    // Online: the CnCNet V3 tunnel (8-byte LE [from][to] header); peers are
    // V3 ids. tick() keeps our id registered and measures the tunnel RTT.
    bool open_tunnel(const Addr& tunnel, std::uint32_t my_v3);
    // Tests: an endpoint of an in-process LoopbackHub, with the test's clock.
    void open_loopback(ra2::net::LoopbackHub& hub, const Addr& self, std::function<std::uint32_t()> clock_ms);

    bool is_open() const { return net_ != nullptr; }
    ra2::net::NetManager& net() { return *net_; }
    bool tunnel() const { return tunnel_mode_; }
    std::uint16_t port() const;               // direct mode: the bound UDP port
    std::uint32_t my_v3() const { return me_; }
    const Addr& tunnel_addr() const { return tunnel_; }
    int rtt_ms() const { return rtt_; }       // tunnel ping RTT (-1 unknown)

    ra2::net::PeerAddr addr_of(PeerId p) const;
    PeerId peer_of_addr(const ra2::net::PeerAddr& a) const;
    std::string describe(PeerId p) const;

    // Tunnel keep-alive (every 5 s) + ping; call every frame (cheap).
    void tick(std::uint64_t now);

private:
    std::unique_ptr<ra2::net::UdpDatagramIO> udp_;
    std::unique_ptr<ra2::net::NetManager> net_;
    bool tunnel_mode_ = false;
    Addr tunnel_;
    std::uint32_t me_ = 0;
    std::uint64_t last_keep_ = 0, ping_sent_ = 0;
    std::uint32_t ping_nonce_ = 0;
    int rtt_ = -1;
};

// The lobby's reliable messaging over MpNet's 0x1236 global channel: DATA_ACK
// packets are resent every 256 ms, 40 tries, and dedupe by (peer, PacketID)
// (engine/net/channel.cpp); a peer whose packet ran out of tries is "lost".
class LobbyChannel {
public:
    explicit LobbyChannel(MpNet& n);
    void send(PeerId to, const std::vector<std::uint8_t>& body, bool reliable, std::uint64_t now);
    struct In { PeerId from; std::vector<std::uint8_t> body; };
    void poll(std::uint64_t now, std::vector<In>& in, std::vector<PeerId>& lost);
    void forget(PeerId p);
    bool idle_to(PeerId p) const;   // nothing un-ACKed to p
    // W9-04-07: put queued datagrams on the wire NOW (the app is about to
    // be frozen: no later poll would send them).
    void flush() { n_.net().service(); }
    MpNet& net() { return n_; }
    const MpNet& net() const { return n_; }

private:
    MpNet& n_;
};

}  // namespace ra2mp
