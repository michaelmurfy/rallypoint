// engine/net/net_manager.cpp -- see net_manager.hpp.
#include "net/net_manager.hpp"

#include "net/net_bytes.hpp"

#include <algorithm>

namespace ra2::net {

// ---------------------------------------------------------- UdpDatagramIO
bool UdpDatagramIO::send(const PeerAddr& to, const uint8_t* data, size_t len) {
    if (mode_ == TunnelMode::None) return sock_.send_to(to.addr, data, len) >= 0;
    tunnel_wrap(mode_, local_id_, to.tunnel_id, data, len, scratch_);
    return sock_.send_to(server_, scratch_.data(), scratch_.size()) >= 0;
}

bool UdpDatagramIO::recv(PeerAddr& from, std::vector<uint8_t>& data) {
    uint8_t buf[2048];
    for (;;) {
        NetAddr src;
        const int n = sock_.recv_from(src, buf, sizeof buf);
        if (n < 0) return false;
        if (mode_ == TunnelMode::None) {
            from = PeerAddr{src, 0};
            data.assign(buf, buf + n);
            return true;
        }
        uint32_t fid = 0, tid = 0;
        const uint8_t* p = nullptr;
        size_t plen = 0;
        const int64_t expect = mode_ == TunnelMode::V2 ? static_cast<int64_t>(local_id_ & 0xFFFF)
                                                       : static_cast<int64_t>(local_id_);
        if (!tunnel_unwrap(mode_, buf, static_cast<size_t>(n), &fid, &tid, &p, &plen, expect)) {
            ++dropped_foreign_;
            if (on_foreign) on_foreign(src, buf, static_cast<size_t>(n));
            continue;
        }
        from = PeerAddr{src, fid};
        data.assign(p, p + plen);
        return true;
    }
}

// ------------------------------------------------------------ LoopbackHub
LoopbackHub::Endpoint* LoopbackHub::create(const PeerAddr& self) {
    endpoints_.push_back(std::make_unique<Endpoint>(this, self));
    return endpoints_.back().get();
}

void LoopbackHub::set_partitioned(const PeerAddr& who, bool c) {
    auto it = std::find(partitioned_.begin(), partitioned_.end(), who);
    if (c && it == partitioned_.end()) partitioned_.push_back(who);
    if (!c && it != partitioned_.end()) partitioned_.erase(it);
}

bool LoopbackHub::cut(const PeerAddr& a) const {
    return std::find(partitioned_.begin(), partitioned_.end(), a) != partitioned_.end();
}

bool LoopbackHub::Endpoint::send(const PeerAddr& to, const uint8_t* data, size_t len) {
    LoopbackHub& h = *hub_;
    if (h.cut(self_) || h.cut(to)) {
        ++h.dropped_;
        return true;
    }
    std::uniform_real_distribution<double> u(0.0, 1.0);
    if (h.loss_ > 0.0 && u(h.rng_) < h.loss_) {
        ++h.dropped_;
        return true;
    }
    int copies = (h.dup_ > 0.0 && u(h.rng_) < h.dup_) ? 2 : 1;
    for (int c = 0; c < copies; ++c) {
        uint32_t lat = h.lat_min_;
        if (h.lat_max_ > h.lat_min_)
            lat += std::uniform_int_distribution<uint32_t>(0, h.lat_max_ - h.lat_min_)(h.rng_);
        InFlight f;
        f.due_ms = h.clock_() + lat;
        f.seq = h.seq_++;
        f.from = self_;
        f.to = to;
        f.data.assign(data, data + len);
        h.flight_.push_back(std::move(f));
    }
    return true;
}

bool LoopbackHub::Endpoint::recv(PeerAddr& from, std::vector<uint8_t>& data) {
    LoopbackHub& h = *hub_;
    const uint32_t now = h.clock_();
    // Earliest due datagram addressed to us (ties by send order).
    auto best = h.flight_.end();
    for (auto it = h.flight_.begin(); it != h.flight_.end(); ++it) {
        if (it->to != self_) continue;
        if (static_cast<int32_t>(now - it->due_ms) < 0) continue;
        if (best == h.flight_.end() || static_cast<int32_t>(it->due_ms - best->due_ms) < 0 ||
            (it->due_ms == best->due_ms && it->seq < best->seq))
            best = it;
    }
    if (best == h.flight_.end()) return false;
    from = best->from;
    data = std::move(best->data);
    h.flight_.erase(best);
    ++h.delivered_;
    return true;
}

// ------------------------------------------------------------- NetManager
NetManager::NetManager(DatagramIO& io, ClockMs clock_ms, uint16_t product)
    : io_(io), clock_(std::move(clock_ms)), product_(product), global_(product) {
    global_.init();
    global_.set_timing(timing_);
}

bool NetManager::create_connection(int id, const PeerAddr& peer) {
    if (num_connections() >= kMaxPrivateConnections) return false;
    if (connection_index(id) >= 0) return false;
    auto c = std::make_unique<PrivateChannel>(id, peer, product_);
    c->init();
    c->set_timing(timing_);
    conns_.push_back(std::move(c));
    return true;
}

bool NetManager::destroy_connection(int id) {
    const int i = connection_index(id);
    if (i < 0) return false;
    conns_.erase(conns_.begin() + i);
    if (bad_conn_ == id) bad_conn_ = -1;
    return true;
}

int NetManager::connection_id(int index) const {
    if (index < 0 || index >= num_connections()) return -1;
    return conns_[static_cast<size_t>(index)]->id();
}

int NetManager::connection_index(int id) const {
    for (size_t i = 0; i < conns_.size(); ++i)
        if (conns_[i]->id() == id) return static_cast<int>(i);
    return -1;
}

PrivateChannel* NetManager::connection(int id) {
    const int i = connection_index(id);
    return i < 0 ? nullptr : conns_[static_cast<size_t>(i)].get();
}

const PrivateChannel* NetManager::connection(int id) const {
    const int i = connection_index(id);
    return i < 0 ? nullptr : conns_[static_cast<size_t>(i)].get();
}

void NetManager::set_timing(const ChannelTiming& t) {
    timing_ = t;
    for (auto& c : conns_) c->set_timing(t);
    global_.set_timing(t);
}

bool NetManager::send_private(const uint8_t* payload, size_t len, bool ack, int conn_id) {
    bool ok = true;
    for (auto& c : conns_)
        if (conn_id < 0 || c->id() == conn_id) ok = c->send(payload, len, ack) && ok;
    return ok;
}

bool NetManager::get_private(std::vector<uint8_t>& payload, int& conn_id) {
    ReceivedPacket r;
    for (auto& c : conns_) {
        if (c->pop_received(r)) {
            payload = std::move(r.payload);
            conn_id = c->id();
            return true;
        }
    }
    return false;
}

bool NetManager::send_global(const uint8_t* payload, size_t len, bool ack, const PeerAddr* to) {
    return global_.send(payload, len, ack, to, broadcast_);
}

bool NetManager::get_global(ReceivedPacket& out) { return global_.pop_received(out); }

void NetManager::emit(const PeerAddr& to, const uint8_t* pkt, size_t len) {
    if (!build_datagram(pkt, len, scratch_)) return;
    io_.send(to, scratch_.data(), scratch_.size());
}

void NetManager::service() {
    auto em = [this](const PeerAddr& to, const uint8_t* p, size_t n) { emit(to, p, n); };
    PeerAddr from;
    std::vector<uint8_t> dg;
    while (io_.recv(from, dg)) {
        const uint8_t* pkt = nullptr;
        size_t n = 0;
        if (!check_datagram(dg.data(), dg.size(), &pkt, &n)) {
            ++crc_errors_;
            continue;
        }
        if (n < 2) {
            ++unknown_;
            continue;
        }
        const uint16_t magic = get_u16le(pkt);
        if (magic == kGlobalMagic) {
            global_.receive(pkt, n, from, em);
        } else if (magic == product_) {
            PrivateChannel* target = nullptr;
            for (auto& c : conns_)
                if (c->peer() == from) {
                    target = c.get();
                    break;
                }
            if (!target && rebind_ && n >= kPrivateHeaderSize + 3 && pkt[2] == kCodeDataNoAck &&
                pkt[kPrivateHeaderSize] == 0x19) {
                // 0x541550: NOACK FRAMESYNC from an unknown endpoint -> that
                // house's connection moved (NAT port change).
                target = connection(static_cast<int8_t>(pkt[kPrivateHeaderSize + 2]));
                if (target) target->set_peer(from);
            }
            if (target)
                target->receive(pkt, n, em);
            else
                ++unknown_;
        } else {
            ++unknown_;
        }
    }
    const uint32_t now = now_ticks();
    int bad = -1;
    for (auto& c : conns_)
        if (!c->service(now, em) && bad < 0) bad = c->id();
    global_.service(now, em);
    bad_conn_ = bad;
}

uint32_t NetManager::response_time() const {
    uint32_t rt = 0;
    for (const auto& c : conns_) rt = std::max(rt, c->response_time());
    return rt;
}

}  // namespace ra2::net
