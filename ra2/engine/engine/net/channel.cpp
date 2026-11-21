// engine/net/channel.cpp -- see channel.hpp for the format and addresses.
#include "net/channel.hpp"

#include <algorithm>
#include <iterator>

#include "net/net_bytes.hpp"

#include <cstring>
#include <utility>

namespace ra2::net {

// ------------------------------------------------------------ ChannelBase
ChannelBase::ChannelBase(uint16_t magic, size_t header_size, size_t max_payload, int queue_size)
    : magic_(magic), header_size_(header_size), max_payload_(max_payload), queue_size_(queue_size) {}

void ChannelBase::init() {
    send_.clear();
    recv_.clear();
    stats_ = ChannelStats{};
    total_queued_ = 0;
    total_received_ = 0;
}

bool ChannelBase::queue_send(std::vector<uint8_t>&& pkt, const PeerAddr& to) {
    // FUN_0048B320: refuse when the queue is full or the packet exceeds the
    // per-entry buffer (MaxPacketLen = max payload + header).
    if (static_cast<int>(send_.size()) >= queue_size_) return false;
    if (pkt.size() > max_payload_ + header_size_) return false;
    SendEntry e;
    e.pkt = std::move(pkt);
    e.to = to;
    send_.push_back(std::move(e));
    ++total_queued_;
    return true;
}

bool ChannelBase::queue_receive(ReceivedPacket&& p) {
    // FUN_0048B660
    if (static_cast<int>(recv_.size()) >= queue_size_) return false;
    if (p.payload.size() > max_payload_) return false;
    recv_.push_back(std::move(p));
    ++total_received_;
    return true;
}

void ChannelBase::ack_sent_packet(uint32_t id) {
    // Receive_Packet case ACK: the first DATA_ACK entry with this PacketID.
    for (auto& e : send_) {
        if (get_u32le(e.pkt.data() + 4) == id && e.pkt[2] == kCodeDataAck) {
            e.done = true;
            return;
        }
    }
}

void ChannelBase::rtt_sample(uint32_t t) {
    // FUN_0048B920: plain average over the first 256 samples, then a running
    // average with a 256-sample window, rounded to nearest.
    if (stats_.rtt_count == 0x100) {
        stats_.rtt_sum = stats_.rtt_sum + (t - stats_.rtt_avg);
        stats_.rtt_avg = (stats_.rtt_sum >> 8) + ((stats_.rtt_sum & 0xFF) > 0x7F ? 1u : 0u);
    } else {
        ++stats_.rtt_count;
        stats_.rtt_sum += t;
        stats_.rtt_avg = stats_.rtt_sum / stats_.rtt_count;
    }
    if (stats_.rtt_max < t) stats_.rtt_max = t;
}

bool ChannelBase::service(uint32_t now, const ChannelEmit& emit) {
    for (auto it = recv_.begin(); it != recv_.end();) {  // Service_Receive_Queue
        if (dead(*it))
            it = recv_.erase(it);
        else
            ++it;
    }
    // Pass 1: retire done entries.
    for (auto it = send_.begin(); it != send_.end();) {
        if (it->done) {
            if (it->pkt[2] == kCodeDataAck) rtt_sample(now - it->first_time);
            it = send_.erase(it);
        } else {
            ++it;
        }
    }
    // Pass 2: (re)send due entries.
    bool bad = false;
    for (auto& e : send_) {
        if (e.done) continue;
        // A fresh entry has last_time 0 in the original, whose tick clock is
        // never near 0, so it always goes out on the first service.
        if (e.send_count == 0 || now - e.last_time > timing_.retry_delta) {
            emit(e.to, e.pkt.data(), e.pkt.size());
            e.last_time = now;
            if (e.send_count == 0) {
                e.first_time = now;
                if (e.pkt[2] == kCodeDataNoAck) e.done = true;
            } else {
                ++stats_.resends;
            }
            ++e.send_count;
            if (timing_.max_retries != 0xFFFFFFFFu && timing_.max_retries < e.send_count) {
                bad = true;
                e.failed = true;
            }
            if (timing_.timeout != 0xFFFFFFFFu && timing_.timeout < e.last_time - e.first_time) {
                bad = true;
                e.failed = true;
            }
        }
    }
    if (bad)
        for (auto it = send_.begin(); it != send_.end();)
            it = (it->failed && retire_failed(*it)) ? send_.erase(it) : std::next(it);
    return !bad;
}

bool ChannelBase::pop_received(ReceivedPacket& out) {
    for (auto& r : recv_) {
        if (r.read || !deliverable(r)) continue;
        r.read = true;
        on_delivered(r);
        out = r;
        return true;
    }
    return false;
}

size_t ChannelBase::received_pending() const {
    size_t n = 0;
    for (const auto& r : recv_)
        if (!r.read) ++n;
    return n;
}

// --------------------------------------------------------- PrivateChannel
PrivateChannel::PrivateChannel(int id, const PeerAddr& peer, uint16_t product)
    : ChannelBase(product, kPrivateHeaderSize, kPrivateMaxPayload, kPrivateQueueSize),
      id_(id), peer_(peer) {}

bool PrivateChannel::send(const uint8_t* payload, size_t len, bool ack) {
    if (len > max_payload_) return false;
    std::vector<uint8_t> pkt(kPrivateHeaderSize + len, 0);
    put_u16le(pkt.data(), magic_);
    pkt[2] = ack ? kCodeDataAck : kCodeDataNoAck;
    pkt[3] = 0;  // router forward byte: only under the WOL packet router
    put_u32le(pkt.data() + 4, ack ? num_send_ack_ : num_send_noack_);
    // bytes 8..13: never written by the original (stale buffer); zero here.
    if (len) std::memcpy(pkt.data() + kPrivateHeaderSize, payload, len);
    if (!queue_send(std::move(pkt), peer_)) return false;
    if (ack)
        ++num_send_ack_;
    else
        ++num_send_noack_;
    return true;
}

bool PrivateChannel::receive(const uint8_t* pkt, size_t len, const ChannelEmit& emit) {
    if (len < kPrivateHeaderSize) return false;
    if (get_u16le(pkt) != magic_) {
        ++stats_.bad_magic;
        return false;
    }
    const uint8_t code = pkt[2];
    const uint32_t id = get_u32le(pkt + 4);
    auto make = [&]() {
        ReceivedPacket r;
        r.from = peer_;
        r.code = code;
        r.id = id;
        r.payload.assign(pkt + kPrivateHeaderSize, pkt + len);
        return r;
    };
    const int free_slots = queue_size_ - static_cast<int>(recv_.size());

    if (code == kCodeAck) {
        ack_sent_packet(id);
        return true;
    }
    if (code == kCodeDataNoAck) {
        if (free_slots < 2) {  // keep two slots for DATA_ACK traffic
            ++stats_.dropped;
            return false;
        }
        if (!queue_receive(make())) {
            ++stats_.dropped;
            return false;
        }
        ++stats_.received_noack;
        stats_.lost_noack = (id - stats_.received_noack) + 1;
        if (id != 0) stats_.pct_lost = 100 - (stats_.received_noack * 100) / (id + 1);
        return true;
    }
    if (code != kCodeDataAck) return false;  // see docs/re/net-protocol.md (deviation D2)

    if (id > last_seq_ || last_seq_ == 0xFFFFFFFFu) {
        bool dup = false;
        for (const auto& r : recv_)
            if (r.code == kCodeDataAck && r.id == id) {
                dup = true;
                break;
            }
        if (!dup) {
            if (free_slots < 2 && id != last_seq_ + 1) return false;  // no ACK: sender retries
            if (!queue_receive(make())) return false;
            ++stats_.received_ack;
            if (id == last_seq_ + 1) {
                // Advance the in-order high-water mark over still-queued packets.
                uint32_t next = id;
                for (;;) {
                    last_seq_ = next;
                    bool found = false;
                    for (const auto& r : recv_)
                        if (r.code == kCodeDataAck && r.id == last_seq_ + 1) {
                            found = true;
                            break;
                        }
                    if (!found) break;
                    next = last_seq_ + 1;
                }
            }
        }
    }
    // Always ACK: 14 bytes, code 2, same PacketID (bytes 8..13 are stack
    // garbage in the original; zero here).
    uint8_t ackpkt[kPrivateHeaderSize] = {0};
    put_u16le(ackpkt, magic_);
    ackpkt[2] = kCodeAck;
    ackpkt[3] = 0;
    put_u32le(ackpkt + 4, id);
    emit(peer_, ackpkt, sizeof ackpkt);
    return true;
}

// ---------------------------------------------------------- GlobalChannel
GlobalChannel::GlobalChannel(uint16_t product, int queue_size)
    : ChannelBase(kGlobalMagic, kGlobalHeaderSize, kGlobalMaxPayload, queue_size),
      product_(product), ring_(static_cast<size_t>(queue_size) * 2) {}

bool GlobalChannel::send(const uint8_t* payload, size_t len, bool ack, const PeerAddr* to,
                         const PeerAddr& broadcast_to) {
    if (len > max_payload_) return false;
    std::vector<uint8_t> pkt(kGlobalHeaderSize + len, 0);
    put_u16le(pkt.data(), kGlobalMagic);
    pkt[2] = (ack && to != nullptr) ? kCodeDataAck : kCodeDataNoAck;
    pkt[3] = 0;
    put_u32le(pkt.data() + 4, total_queued_);
    // +8 u32 0, +12 u16 0 (written explicitly by FUN_0053F900)
    put_u16le(pkt.data() + 14, product_);
    if (len) std::memcpy(pkt.data() + kGlobalHeaderSize, payload, len);
    return queue_send(std::move(pkt), to ? *to : broadcast_to);
}

bool GlobalChannel::receive(const uint8_t* pkt, size_t len, const PeerAddr& from,
                            const ChannelEmit& emit) {
    if (len < kGlobalHeaderSize) return false;
    if (get_u16le(pkt) != kGlobalMagic) {
        ++stats_.bad_magic;
        return false;
    }
    const uint8_t code = pkt[2];
    const uint32_t id = get_u32le(pkt + 4);
    auto make = [&]() {
        ReceivedPacket r;
        r.from = from;
        r.code = code;
        r.id = id;
        r.product = get_u16le(pkt + 14);
        r.payload.assign(pkt + kGlobalHeaderSize, pkt + len);
        return r;
    };
    if (code == kCodeDataAck) {
        bool dup = false;
        for (size_t i = 0; i < ring_.size() && i < total_received_; ++i)
            if (ring_[i].valid && ring_[i].from == from && ring_[i].id == id) {
                dup = true;
                break;
            }
        if (!dup) {
            if (!queue_receive(make())) {
                ++stats_.dropped;  // "Failed to queue incoming packet": no ACK
                return true;
            }
            ring_[ring_pos_].from = from;
            ring_[ring_pos_].id = id;
            ring_[ring_pos_].valid = true;
            if (++ring_pos_ >= ring_.size()) ring_pos_ = 0;
        }
        uint8_t ackpkt[kGlobalHeaderSize] = {0};
        put_u16le(ackpkt, kGlobalMagic);
        ackpkt[2] = kCodeAck;
        ackpkt[3] = 0;
        put_u32le(ackpkt + 4, id);
        put_u16le(ackpkt + 14, product_);
        emit(from, ackpkt, sizeof ackpkt);
        return true;
    }
    if (code == kCodeDataNoAck) {
        queue_receive(make());
        return true;
    }
    if (code == kCodeAck) {
        ack_sent_packet(id);
        return true;
    }
    return true;
}

// NET-PLAY: the lobby's per-peer view of the global channel.
bool GlobalChannel::pending_to(const PeerAddr& to) const {
    for (const auto& e : send_)
        if (!e.done && e.to == to && e.pkt[2] == kCodeDataAck) return true;
    return false;
}

void GlobalChannel::drop_pending_to(const PeerAddr& to) {
    for (auto it = send_.begin(); it != send_.end();) it = it->to == to ? send_.erase(it) : std::next(it);
}

std::vector<PeerAddr> GlobalChannel::take_failed() {
    std::vector<PeerAddr> out;
    out.swap(failed_);
    return out;
}

bool GlobalChannel::retire_failed(const SendEntry& e) {
    if (std::find(failed_.begin(), failed_.end(), e.to) == failed_.end()) failed_.push_back(e.to);
    return true;
}

}  // namespace ra2::net
