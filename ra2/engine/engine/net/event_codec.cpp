// engine/net/event_codec.cpp -- see event_codec.hpp.
//
// The encoder/decoder below follow the control flow of FUN_0064B0D0 and
// FUN_0064B840 step for step (variable names in comments are Ghidra's), so
// that packets are byte-identical to gamemd's, including its quirks.
#include "net/event_codec.hpp"

#include "net/net_bytes.hpp"

#include <cstring>

namespace ra2::net {

namespace {
constexpr uint8_t kMega = static_cast<uint8_t>(EventType::MEGAMISSION);
constexpr uint8_t kRespTime = static_cast<uint8_t>(EventType::RESPONSE_TIME);
constexpr uint8_t kAddPlayer = static_cast<uint8_t>(EventType::ADDPLAYER);
constexpr uint8_t kFrameInfo = static_cast<uint8_t>(EventType::FRAMEINFO);
constexpr uint8_t kFrameSync = static_cast<uint8_t>(EventType::FRAMESYNC);

// Run membership (inside FUN_0064B0D0): same Mission, Target and Destination.
// Whom, slot, Follow and IsPlanning are NOT compared.
bool same_run(const Event& a, const Event& b) {
    return a.data[5] == b.data[5] &&                                  // Mission
           a.data[11] == b.data[11] && get_u32le(&a.data[7]) == get_u32le(&b.data[7]) &&   // Target
           a.data[16] == b.data[16] && get_u32le(&a.data[12]) == get_u32le(&b.data[12]);   // Dest
}
}  // namespace

void FrameHeader::write(uint8_t* p) const {
    p[0] = type;
    p[1] = flags;
    p[2] = static_cast<uint8_t>(house);
    put_u32le(p + 3, frame);
    put_u32le(p + 7, crc);
    put_u16le(p + 11, command_count);
    p[13] = delay;
}

FrameHeader FrameHeader::read(const uint8_t* p) {
    FrameHeader h;
    h.type = p[0];
    h.flags = p[1];
    h.house = static_cast<int8_t>(p[2]);
    h.frame = get_u32le(p + 3);
    h.crc = get_u32le(p + 7);
    h.command_count = get_u16le(p + 11);
    h.delay = p[13];
    return h;
}

uint32_t stamp_frame(uint32_t now, uint32_t max_ahead, uint32_t fsr, int protocol) {
    if (protocol == 2 && fsr != 0) {
        // signed division in the binary; all operands are small positives.
        const int32_t f = static_cast<int32_t>(fsr);
        return static_cast<uint32_t>(((f + static_cast<int32_t>(now) - 1 + static_cast<int32_t>(max_ahead)) / f) * f);
    }
    return now + max_ahead;
}

// ================================================================ encoder
void EventEncoder::reset() {
    for (auto& s : slots_) {
        s.count = 0;
        s.saved_frame = 0;
    }
}

bool EventEncoder::cache_lookup(uint8_t& count, size_t& off, uint8_t* payload, const uint8_t* list,
                                bool finalize, uint32_t now_frame) {
    const uint8_t c = count;
    if (c > 1) {
        for (int s = 0; s < kMegaMissionCacheSlots; ++s) {
            Slot& sl = slots_[s];
            if (c != sl.count) continue;
            if (std::memcmp(sl.whoms.data(), payload, 5) != 0) continue;
            if (std::memcmp(sl.whoms.data() + 5, list, static_cast<size_t>(c - 1) * 5) != 0) continue;
            if (finalize) off = off - static_cast<size_t>(c) * 5 + 5;
            if (!finalize) return true;
            count = 0;
            payload[6] = static_cast<uint8_t>(s);
            return true;
        }
    }
    if (finalize && c > 1) {
        int best = -1, best_age = -1, chosen = -1;
        for (int i = 0; i < kMegaMissionCacheSlots; ++i) {
            if (slots_[i].saved_frame == 0) {
                chosen = i;
                break;
            }
            const int age = static_cast<int>(now_frame - slots_[i].saved_frame);
            if (best_age < age) {
                best = i;
                best_age = age;
            }
        }
        if (chosen == -1) chosen = best;
        if (chosen == -1) chosen = 0;
        Slot& sl = slots_[chosen];
        sl.saved_frame = now_frame;
        std::memcpy(sl.whoms.data(), payload, 5);
        const size_t n = static_cast<size_t>(c) * 5 - 5;
        std::memcpy(sl.whoms.data() + 5, list, n < sl.whoms.size() - 5 ? n : sl.whoms.size() - 5);
        sl.count = c;
        payload[6] = static_cast<uint8_t>(chosen);
    }
    return false;
}

size_t EventEncoder::add_compressed(std::deque<Event>& outlist, uint8_t* buf, size_t buflen,
                                    size_t off, int max_count, uint32_t frame, int8_t house,
                                    uint32_t now_frame, std::vector<Event>& echo,
                                    size_t echo_capacity, int& consumed) {
    consumed = 0;                       // local_7c
    Event prev;                         // local_70 (zeroed: type 0)
    const size_t kNone = static_cast<size_t>(-1);
    size_t count_pos = kNone;           // local_88
    uint8_t run = 0;                    // cVar5
    bool appended = false;              // bVar3
    bool early = false;                 // bVar2
    size_t run_payload = 0;             // local_80
    size_t run_list = 0;                // local_84
    const size_t echo_start = echo.size();

    if (outlist.empty()) return off;
    while (consumed < max_count) {
        Event& cur = outlist.front();
        const uint8_t t = cur.type;
        const int ws = event_wire_size(t, spawner_ext_);
        size_t size = ws < 0 ? 0 : static_cast<size_t>(ws);  // uVar9
        size_t need = (t == kAddPlayer) ? cur.blob.size() + 1 + size : size + 1;  // iVar8

        if (prev.type == kMega) {
            if (t == kMega) {
                if (!early && same_run(prev, cur)) {
                    size = 5;
                    ++run;
                    need = 5;
                    appended = true;
                    goto have_space_check;
                }
                // "Compress: New megamission run"
                cache_lookup(run, off, buf + run_payload, buf + run_list, true, now_frame);
                need += 1;
                appended = false;
                buf[count_pos] = run;
                run = 1;
                count_pos = off + 1;
            } else {
                // "Compress: Non-megamission event"
                cache_lookup(run, off, buf + run_payload, buf + run_list, true, now_frame);
                const size_t old = count_pos;
                count_pos = kNone;
                appended = false;
                buf[old] = run;
                run = 0;
            }
        } else if (t == kMega) {
            need += 1;
            run = 1;
            appended = false;
            count_pos = off + 1;
        }
    have_space_check:
        early = false;
        if (buflen < need + off) {
            if (t == kMega && appended) {
                // "Compress: Out of buffer space": the original finalizes with
                // the incremented count (its list compare reads one stale
                // Whom past `off`) and then writes count-1.
                cache_lookup(run, off, buf + run_payload, buf + run_list, true, now_frame);
                buf[count_pos] = static_cast<uint8_t>(run - 1);
            }
            break;
        }
        cur.frame = frame;
        cur.house = house;
        cur.flags &= 0xFE;
        if (echo.size() - echo_start >= echo_capacity) break;  // DoList full (> 0x3FFF)
        echo.push_back(cur);

        if (t == kMega) {
            buf[count_pos] = run;
            if (appended) {
                std::memcpy(buf + off, cur.data.data(), 5);  // Whom
                off += 5;
                if (outlist.size() == 1 || consumed == max_count - 1) {
                    // "Compress: Ended megamission run"
                    cache_lookup(run, off, buf + run_payload, buf + run_list, true, now_frame);
                    buf[count_pos] = run;
                } else if (cache_lookup(run, off, buf + run_payload, buf + run_list, false, now_frame)) {
                    // "Compress: Found early repeat megamission"
                    early = true;
                    buf[count_pos] = run;
                }
            } else {
                buf[off] = kMega;
                run_payload = off + 2;
                std::memcpy(buf + run_payload, cur.data.data(), size);
                run_list = run_payload + size;
                off += 2 + size;
            }
        } else if (t == kRespTime) {
            buf[off] = kRespTime;
            std::memcpy(buf + off + 1, cur.data.data() + 6, size);
            off += 1 + size;
        } else if (t == kAddPlayer) {
            buf[off] = kAddPlayer;
            put_u32le(buf + off + 1, static_cast<uint32_t>(cur.blob.size()));
            off += 1 + size;
            if (!cur.blob.empty()) std::memcpy(buf + off, cur.blob.data(), cur.blob.size());
            off += cur.blob.size();
        } else {
            buf[off] = t;
            std::memcpy(buf + off + 1, cur.data.data(), size);
            off += 1 + size;
        }
        ++consumed;
        prev = cur;
        outlist.pop_front();
        if (outlist.empty()) break;
    }
    return off;
}

// ================================================================ decoder
void EventDecoder::reset() { counts_.fill(0); }

int EventDecoder::extract(const uint8_t* pkt, size_t len, std::vector<Event>& out, size_t capacity) {
    int added = 0;                 // local_94
    size_t i = 0;                  // iVar10
    size_t remain = len;           // local_98
    size_t sz = 13;                // uVar11
    uint8_t count = 0;             // local_99
    Event ev;                      // local_70 (house/frame persist across records)
    const size_t start = out.size();
    auto push = [&](const Event& e) -> bool {
        if (out.size() - start >= capacity) return false;
        out.push_back(e);
        return true;
    };

    if (len <= 13) return 0;
    do {
        const uint8_t t = pkt[i];
        ev.type = t;
        if (t == kFrameSync) {
            i += 1 + sz;
            remain -= 1 + sz;
            sz = 13;
        } else {
            if (t == kFrameInfo) {
                ev.house = static_cast<int8_t>(pkt[i + 2]);
                ev.frame = get_u32le(pkt + i + 3);
                i += 6;
                remain -= 6;
            } else if (t == kMega) {
                count = pkt[i + 1];
                i += 1;
                remain -= 1;
            }
            ev.data.fill(0);
            ev.blob.clear();
            const int ws = event_wire_size(t, spawner_ext_);
            if (ws < 0) break;  // D3: unknown type (the original indexes past its table)
            sz = static_cast<size_t>(ws);
            size_t adv = sz;  // local_8c
            if (i + 1 + sz > len) break;  // D3: truncated record

            if (t == kMega) {
                std::memcpy(ev.data.data(), pkt + i + 1, sz);
                // iVar5 = (char)param_1[2]: the house byte of the packet header.
                const int house8 = static_cast<int8_t>(pkt[2]) * 8;
                // W9-05-12 (clone hardening): the encoder only ever uses cache
                // slots 0-7 of houses 0-7; a larger slot byte would index
                // ANOTHER house's cache (house*8 + slot) and corrupt it on
                // this receiver only.
                const bool slot_ok = ev.data[6] < kMegaMissionCacheSlots && house8 >= 0 && house8 < kMaxHouses * 8;
                if (count < 2) {
                    if (count == 0) {
                        // Replay cached unit list [house*8 + slot].
                        if (!slot_ok) break;   // W9-05-12
                        const int idx = ev.data[6] + house8;
                        i += 1 + sz;
                        remain -= 1 + sz;
                        adv = 5;
                        if (idx < 0 || idx >= kMaxHouses * kMegaMissionCacheSlots || counts_[idx] < 2) break;  // D3
                        uint8_t left = static_cast<uint8_t>(counts_[idx]);
                        const uint8_t* lst = lists_[idx].data();
                        std::memcpy(ev.data.data(), lst, 5);
                        size_t k = 5;
                        while (left != 0) {
                            if (!push(ev)) return -1;
                            ++added;
                            --left;
                            std::memcpy(ev.data.data(), lst + k, 5);
                            if (left == 1) {
                                adv = static_cast<size_t>(-1);  // i += 1 + (-1) below
                                break;
                            }
                            k += 5;
                        }
                    }
                    // count == 1: single event, falls through with adv = 23
                } else {
                    // "Decoding new MEGAMISSION": store the explicit list.
                    i += 1 + sz;
                    remain -= 1 + sz;
                    adv = 5;
                    const int idx = ev.data[6] + house8;
                    if (!slot_ok || idx < 0 || idx >= kMaxHouses * kMegaMissionCacheSlots) break;  // D3 / W9-05-12
                    uint8_t* lst = lists_[idx].data();
                    std::memcpy(lst, ev.data.data(), 5);
                    uint32_t stored = 1;
                    counts_[idx] = 1;
                    size_t lp = i;  // local_90
                    size_t k = 5;
                    while (count != 0) {
                        if (lp + 5 > len) return added;  // D3
                        if (!push(ev)) return -1;
                        ++added;
                        std::memcpy(ev.data.data(), pkt + lp, 5);
                        if (k + 5 <= lists_[idx].size()) std::memcpy(lst + k, pkt + lp, 5);
                        --count;
                        ++stored;
                        k += 5;
                        counts_[idx] = stored;
                        if (count == 1) {
                            adv = 4;
                            break;
                        }
                        i += 5;
                        lp += 5;
                        remain -= 5;
                    }
                }
            } else if (t == kRespTime) {
                std::memcpy(ev.data.data() + 6, pkt + i + 1, sz);
            } else if (t == kAddPlayer) {
                std::memcpy(ev.data.data() + 4, pkt + i + 1, sz);
                const uint32_t blen = get_u32le(ev.data.data() + 4);
                if (i + 1 + sz + blen > len) break;  // D3
                ev.blob.assign(pkt + i + 1 + sz, pkt + i + 1 + sz + blen);
                i += blen;
                remain -= blen;
            } else {
                std::memcpy(ev.data.data(), pkt + i + 1, sz);
            }
            if (!push(ev)) return -1;
            ++added;
            i += 1 + adv;
            remain -= 1 + adv;
            if (remain == 0) return added;
            if (i >= len) return added;
            const int nws = event_wire_size(pkt[i], spawner_ext_);
            sz = nws < 0 ? 0 : static_cast<size_t>(nws);
            if (pkt[i] == kMega) sz += 1;
        }
    } while (sz + 1 <= remain && i < len);
    return added;
}

// ======================================================= CommProtocol 0
size_t encode_raw_events(std::deque<Event>& outlist, uint8_t* buf, size_t buflen, size_t off,
                         int max_count, uint32_t frame, int8_t house, std::vector<Event>& echo,
                         size_t echo_capacity, int& consumed) {
    consumed = 0;
    const size_t echo_start = echo.size();
    while (!outlist.empty() && consumed < max_count) {
        Event& cur = outlist.front();
        const size_t need = kEventClassSize + (cur.type == kAddPlayer ? cur.blob.size() : 0);
        if (buflen < need + off) break;
        cur.frame = frame;
        cur.house = house;
        cur.flags &= 0xFE;
        if (echo.size() - echo_start >= echo_capacity) break;
        echo.push_back(cur);
        cur.to_raw(buf + off);
        off += kEventClassSize;
        if (cur.type == kAddPlayer && !cur.blob.empty()) {
            std::memcpy(buf + off, cur.blob.data(), cur.blob.size());
            off += cur.blob.size();
        }
        ++consumed;
        outlist.pop_front();
    }
    return off;
}

int decode_raw_events(const uint8_t* pkt, size_t len, std::vector<Event>& out, size_t capacity) {
    int added = 0;
    size_t i = 0;
    const size_t start = out.size();
    while (len - i > 0x6E) {
        const uint8_t* p = pkt + i;
        if (p[0] != kFrameSync) {
            Event e = Event::from_raw(p);
            e.flags &= 0xFE;
            if (e.type == kAddPlayer) {
                // D4: the original copies the blob from packet offset 111
                // (a bug that only bites ADDPLAYER under CommProtocol 0);
                // the blob that follows this event is used here.
                const uint32_t blen = get_u32le(p + 11);
                if (i + kEventClassSize + blen > len) break;
                e.blob.assign(p + kEventClassSize, p + kEventClassSize + blen);
                put_u32le(e.data.data(), 0);
                i += blen;
            }
            if (out.size() - start >= capacity) return -1;
            out.push_back(std::move(e));
            ++added;
        }
        i += kEventClassSize;
        if (i > len) break;
    }
    return added;
}

}  // namespace ra2::net
