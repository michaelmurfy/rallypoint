// engine/mp/mp_net.cpp -- see mp_net.hpp.
#include "mp/mp_net.hpp"

#include "net/wire.hpp"

namespace ra2mp {

namespace {
std::uint32_t wall_ms() { return static_cast<std::uint32_t>(now_ms()); }
}  // namespace

bool MpNet::open_direct(std::uint16_t port) {
    auto io = std::make_unique<ra2::net::UdpDatagramIO>();
    if (!io->open(port)) return false;
    udp_ = std::move(io);
    tunnel_mode_ = false;
    net_ = std::make_unique<ra2::net::NetManager>(*udp_, wall_ms);
    return true;
}

bool MpNet::open_tunnel(const Addr& tunnel, std::uint32_t my_v3) {
    auto io = std::make_unique<ra2::net::UdpDatagramIO>();
    if (!io->open(0)) return false;
    io->set_tunnel(ra2::net::TunnelMode::V3, tunnel, my_v3);
    io->on_foreign = [this](const ra2::net::NetAddr& from, const std::uint8_t* d, std::size_t n) {
        if (ping_sent_ && from == tunnel_ && ra2::net::is_tunnel_ping_reply(d, n, ping_nonce_)) {
            rtt_ = static_cast<int>(now_ms() - ping_sent_);
            ping_sent_ = 0;
        }
    };
    udp_ = std::move(io);
    tunnel_mode_ = true;
    tunnel_ = tunnel;
    me_ = my_v3;
    last_keep_ = 0;
    net_ = std::make_unique<ra2::net::NetManager>(*udp_, wall_ms);
    return true;
}

void MpNet::open_loopback(ra2::net::LoopbackHub& hub, const Addr& self, std::function<std::uint32_t()> clock_ms) {
    udp_.reset();
    tunnel_mode_ = false;
    net_ = std::make_unique<ra2::net::NetManager>(*hub.create(ra2::net::PeerAddr{self, 0}), std::move(clock_ms));
}

std::uint16_t MpNet::port() const { return udp_ ? udp_->socket().local_port() : 0; }

ra2::net::PeerAddr MpNet::addr_of(PeerId p) const {
    if (p & kTunnelPeerFlag) return ra2::net::PeerAddr{tunnel_, static_cast<std::uint32_t>(p)};
    return ra2::net::PeerAddr{Addr{static_cast<std::uint32_t>(p >> 16), static_cast<std::uint16_t>(p & 0xFFFF)}, 0};
}

PeerId MpNet::peer_of_addr(const ra2::net::PeerAddr& a) const {
    if (tunnel_mode_) return peer_of_v3(a.tunnel_id);
    return Addr(a.addr).key();
}

std::string MpNet::describe(PeerId p) const {
    if (p & kTunnelPeerFlag) return "tunnel id " + std::to_string(static_cast<std::uint32_t>(p));
    return Addr{static_cast<std::uint32_t>(p >> 16), static_cast<std::uint16_t>(p & 0xFFFF)}.str();
}

void MpNet::tick(std::uint64_t now) {
    if (!tunnel_mode_ || !udp_) return;
    // Register / refresh our id at the tunnel (V3 learns the sender's
    // endpoint from its first packet; the receiver may not exist yet, the
    // tunnel then just drops the payload). 0xFFFFFFFE is never a real id.
    if (last_keep_ == 0 || now - last_keep_ >= 5000) {
        std::vector<std::uint8_t> out;
        ra2::net::tunnel_wrap(ra2::net::TunnelMode::V3, me_, 0xFFFFFFFEu, nullptr, 0, out);
        udp_->socket().send_to(tunnel_, out.data(), out.size());
        if (ping_sent_ == 0 || now - ping_sent_ > 4000) {
            ping_nonce_ = static_cast<std::uint32_t>(now * 2654435761u) | 1u;
            const std::vector<std::uint8_t> ping = ra2::net::make_tunnel_ping(ping_nonce_);
            udp_->socket().send_to(tunnel_, ping.data(), ping.size());
            ping_sent_ = now_ms();
        }
        last_keep_ = now;
    }
}

// ------------------------------------------------------------ LobbyChannel --

LobbyChannel::LobbyChannel(MpNet& n) : n_(n) {
    // 256 ms between resends (16 ticks of 16 ms), 40 tries ~ 10 s.
    if (n_.is_open()) n_.net().set_timing({16, 40, 0xFFFFFFFFu});
}

void LobbyChannel::send(PeerId to, const std::vector<std::uint8_t>& body, bool reliable, std::uint64_t) {
    const ra2::net::PeerAddr a = n_.addr_of(to);
    n_.net().send_global(body.data(), body.size(), reliable, &a);
}

void LobbyChannel::poll(std::uint64_t now, std::vector<In>& in, std::vector<PeerId>& lost) {
    n_.tick(now);
    ra2::net::NetManager& m = n_.net();
    m.service();
    ra2::net::ReceivedPacket p;
    while (m.get_global(p)) {
        if (p.product != ra2::net::kProductIdYR) continue;   // RA2 (0xAA03) and YR filter each other
        in.push_back(In{n_.peer_of_addr(p.from), std::move(p.payload)});
    }
    for (const ra2::net::PeerAddr& a : m.global().take_failed()) lost.push_back(n_.peer_of_addr(a));
}

void LobbyChannel::forget(PeerId p) { n_.net().global().drop_pending_to(n_.addr_of(p)); }

bool LobbyChannel::idle_to(PeerId p) const {
    return !const_cast<MpNet&>(n_).net().global().pending_to(n_.addr_of(p));
}

}  // namespace ra2mp
