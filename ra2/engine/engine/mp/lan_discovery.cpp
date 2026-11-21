// engine/mp/lan_discovery.cpp -- see lan_discovery.hpp.
#include "mp/lan_discovery.hpp"

#include <algorithm>
#include <mutex>

namespace ra2mp {

namespace {
const char kMagic[4] = {'R', 'A', '2', 'L'};
constexpr std::uint64_t kBeaconMs = 1000;
constexpr std::uint64_t kExpireMs = 4000;
constexpr std::uint64_t kQueryMs = 5000;
constexpr std::uint64_t kTargetsMs = 10000;

void put_str(std::vector<std::uint8_t>& o, const std::string& s, std::size_t max) {
    const std::size_t n = std::min(s.size(), max);
    o.push_back(static_cast<std::uint8_t>(n));
    o.insert(o.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
}

void put16(std::vector<std::uint8_t>& o, std::uint16_t v) {
    o.push_back(v & 0xFF);
    o.push_back(v >> 8);
}

void put32(std::vector<std::uint8_t>& o, std::uint32_t v) {
    put16(o, v & 0xFFFF);
    put16(o, v >> 16);
}

std::function<void(bool)>& hook() {
    static std::function<void(bool)> h;
    return h;
}
int& lock_count() {
    static int n = 0;
    return n;
}
std::mutex& lock_mu() {
    static std::mutex m;
    return m;
}
}  // namespace

std::vector<std::uint8_t> encode_beacon(const LanBeacon& b) {
    std::vector<std::uint8_t> o(kMagic, kMagic + 4);
    put16(o, b.net_proto);
    o.push_back(b.type);
    o.push_back(static_cast<std::uint8_t>((b.started ? 1 : 0) | (b.password ? 2 : 0) | (b.cross_platform ? 4 : 0)));
    put32(o, b.sim_ver);
    put32(o, b.rules_crc);
    put16(o, b.game_port);
    o.push_back(b.players);
    o.push_back(b.max);
    put32(o, b.nonce);
    put_str(o, b.room, 32);
    put_str(o, b.host, 16);
    put_str(o, b.map, 48);
    put_str(o, b.version, 24);
    o.push_back(b.platform);   // W9-04-13 (appended)
    return o;
}

bool decode_beacon(const std::uint8_t* p, std::size_t n, LanBeacon& out) {
    if (n < 24 || n > 512 || !std::equal(kMagic, kMagic + 4, reinterpret_cast<const char*>(p))) return false;
    std::size_t k = 4;
    auto u8 = [&]() -> std::uint8_t { return k < n ? p[k++] : 0; };
    auto u16 = [&]() { const std::uint16_t lo = u8(); return static_cast<std::uint16_t>(lo | (u8() << 8)); };
    auto u32 = [&]() { const std::uint32_t lo = u16(); return lo | (static_cast<std::uint32_t>(u16()) << 16); };
    bool bad = false;
    auto str = [&](std::size_t max) {
        const std::size_t len = u8();
        if (len > max || k + len > n) { bad = true; return std::string(); }
        std::string s(reinterpret_cast<const char*>(p + k), len);
        k += len;
        for (char& c : s)
            if (static_cast<unsigned char>(c) < 32) c = '?';
        return s;
    };
    LanBeacon b;
    b.net_proto = u16();
    b.type = u8();
    const std::uint8_t flags = u8();
    b.started = (flags & 1) != 0;
    b.password = (flags & 2) != 0;
    b.cross_platform = (flags & 4) != 0;
    b.sim_ver = u32();
    b.rules_crc = u32();
    b.game_port = u16();
    b.players = u8();
    b.max = u8();
    b.nonce = u32();
    if (b.type == 1) {
        b.room = str(32);
        b.host = str(16);
        b.map = str(48);
        b.version = str(24);
        if (k < n) b.platform = u8();   // W9-04-13: absent from older builds
    }
    if (bad || (b.type != 1 && b.type != 2)) return false;
    out = b;
    return true;
}

std::vector<std::uint8_t> encode_lobby_msg(const LanLobbyMsg& m) {
    std::vector<std::uint8_t> o(kMagic, kMagic + 4);
    put16(o, m.net_proto);
    o.push_back(m.type);
    o.push_back(0);
    put32(o, m.nonce);
    put16(o, m.id);
    put_str(o, m.nick, 16);
    if (m.type == 4) put_str(o, m.text, 120);
    return o;
}

bool decode_lobby_msg(const std::uint8_t* p, std::size_t n, LanLobbyMsg& out) {
    if (n < 15 || n > 512 || !std::equal(kMagic, kMagic + 4, reinterpret_cast<const char*>(p))) return false;
    std::size_t k = 4;
    auto u8 = [&]() -> std::uint8_t { return k < n ? p[k++] : 0; };
    auto u16 = [&]() { const std::uint16_t lo = u8(); return static_cast<std::uint16_t>(lo | (u8() << 8)); };
    auto u32 = [&]() { const std::uint32_t lo = u16(); return lo | (static_cast<std::uint32_t>(u16()) << 16); };
    bool bad = false;
    auto str = [&](std::size_t max) {
        const std::size_t len = u8();
        if (len > max || k + len > n) { bad = true; return std::string(); }
        std::string s(reinterpret_cast<const char*>(p + k), len);
        k += len;
        for (char& c : s)
            if (static_cast<unsigned char>(c) < 32) c = '?';
        return s;
    };
    LanLobbyMsg m;
    m.net_proto = u16();
    m.type = u8();
    (void)u8();
    if (m.type != 3 && m.type != 4) return false;
    m.nonce = u32();
    m.id = u16();
    m.nick = str(16);
    if (m.type == 4) m.text = str(120);
    if (bad || m.nick.empty()) return false;
    out = m;
    return true;
}

void lan_set_multicast_hook(std::function<void(bool)> h) {
    std::lock_guard<std::mutex> g(lock_mu());
    hook() = std::move(h);
}

void lan_multicast_acquire() {
    std::lock_guard<std::mutex> g(lock_mu());
    if (lock_count()++ == 0 && hook()) hook()(true);
}

void lan_multicast_release() {
    std::lock_guard<std::mutex> g(lock_mu());
    if (lock_count() > 0 && --lock_count() == 0 && hook()) hook()(false);
}

// ------------------------------------------------------------- announcer --

bool LanAnnouncer::open(const LanBeacon& b) {
    close();
    set(b);
    // Shared 50010: also receives queries; falls back to an ephemeral
    // socket (beacons still go out, queries go unanswered) if it is busy.
    if (!sock_.open(kLanBeaconPort, 0, /*reuse=*/true) && !sock_.open(0)) return false;
    sock_.set_broadcast(true);
    lan_multicast_acquire();
    locked_ = true;
    last_ = 0;
    targets_at_ = 0;
    return true;
}

void LanAnnouncer::close() {
    sock_.close();
    if (locked_) lan_multicast_release();
    locked_ = false;
}

void LanAnnouncer::update(std::uint64_t now) {
    if (!sock_.is_open()) return;
    std::uint8_t buf[600];
    Addr from;
    for (int n; (n = sock_.recv_from(from, buf, sizeof buf)) > 0;) {
        LanBeacon q;
        if (!decode_beacon(buf, static_cast<std::size_t>(n), q) || q.type != 2) continue;
        const std::vector<std::uint8_t> b = encode_beacon(beacon_);
        sock_.send_to(from, b.data(), b.size());   // unicast answer
    }
    if (targets_at_ == 0 || now - targets_at_ >= kTargetsMs) {
        targets_ = broadcast_targets();
        targets_at_ = now;
    }
    if (last_ == 0 || now - last_ >= kBeaconMs) {
        const std::vector<std::uint8_t> b = encode_beacon(beacon_);
        for (std::uint32_t ip : targets_) sock_.send_to(Addr{ip, kLanBeaconPort}, b.data(), b.size());
        last_ = now;
    }
}

// --------------------------------------------------------------- browser --

bool LanBrowser::open() {
    close();
    const bool l = listen_.open(kLanBeaconPort, 0, /*reuse=*/true) && listen_.set_broadcast(true);
    const bool q = query_.open(0) && query_.set_broadcast(true);
    if (!l && !q) return false;
    lan_multicast_acquire();
    locked_ = true;
    games_.clear();
    last_query_ = 0;
    targets_ = broadcast_targets();
    return true;
}

void LanBrowser::close() {
    listen_.close();
    query_.close();
    if (locked_) lan_multicast_release();
    locked_ = false;
}

bool LanBrowser::compatible(const LanGame& g, std::uint16_t proto, std::uint32_t sim, std::uint32_t rules) {
    return g.beacon.net_proto == proto && g.beacon.sim_ver == sim && g.beacon.rules_crc == rules;
}

void LanBrowser::set_presence(const std::string& nick, std::uint32_t nonce) {
    nick_ = nick;
    nonce_ = nonce;
    last_presence_ = 0;   // announce at the next update
}

void LanBrowser::send_presence(const Addr& to) {
    LanLobbyMsg m;
    m.type = 3;
    m.net_proto = net_proto;
    m.nonce = nonce_;
    m.nick = nick_;
    const std::vector<std::uint8_t> b = encode_lobby_msg(m);
    query_.send_to(to, b.data(), b.size());
}

void LanBrowser::say(const std::string& text) {
    if (nick_.empty() || text.empty() || !query_.is_open()) return;
    LanLobbyMsg m;
    m.type = 4;
    m.net_proto = net_proto;
    m.nonce = nonce_;
    m.id = next_id_++;
    m.nick = nick_;
    m.text = text.substr(0, 120);
    const std::vector<std::uint8_t> b = encode_lobby_msg(m);
    for (std::uint32_t ip : targets_) query_.send_to(Addr{ip, kLanBeaconPort}, b.data(), b.size());
    for (const LanPlayer& p : players_) query_.send_to(p.from, b.data(), b.size());
    chat_.push_back(LanChatLine{nick_, m.text});
    seen_chat_.emplace_back(nonce_, m.id);
}

void LanBrowser::take_lobby(const Addr& from, const LanLobbyMsg& m, std::uint64_t now) {
    if (nick_.empty() || m.nonce == nonce_ || m.net_proto != net_proto) return;
    LanPlayer* p = nullptr;
    for (LanPlayer& q : players_)
        if (q.nonce == m.nonce) p = &q;
    if (!p) {
        players_.push_back(LanPlayer{m.nick, m.nonce, from, now, 0});
        p = &players_.back();
    }
    p->nick = m.nick;
    p->seen = now;
    if (m.type == 3) {
        p->from = from;   // presence comes from the sender's page socket
        if (p->answered == 0 || now - p->answered >= kBeaconMs) {   // unicast answer
            send_presence(from);
            p->answered = now;
        }
        return;
    }
    const auto key = std::make_pair(m.nonce, m.id);
    if (std::find(seen_chat_.begin(), seen_chat_.end(), key) != seen_chat_.end()) return;
    seen_chat_.push_back(key);
    if (seen_chat_.size() > 256) seen_chat_.erase(seen_chat_.begin());
    chat_.push_back(LanChatLine{m.nick, m.text});
    if (chat_.size() > 50) chat_.erase(chat_.begin());
}

void LanBrowser::take(const Addr& from, const std::uint8_t* p, int n, std::uint64_t now) {
    LanLobbyMsg lm;
    if (n >= 7 && (p[6] == 3 || p[6] == 4)) {
        if (decode_lobby_msg(p, static_cast<std::size_t>(n), lm)) take_lobby(from, lm, now);
        return;
    }
    LanBeacon b;
    if (!decode_beacon(p, static_cast<std::size_t>(n), b) || b.type != 1) return;
    const Addr host{from.ip, b.game_port};
    for (LanGame& g : games_)
        if (g.beacon.nonce == b.nonce) {
            // Same host seen on several interfaces: keep a non-loopback address.
            if ((host.ip >> 24) != 127 || (g.host.ip >> 24) == 127) g.host = host;
            g.beacon = b;
            g.seen = now;
            return;
        }
    games_.push_back(LanGame{b, host, now});
}

void LanBrowser::update(std::uint64_t now) {
    std::uint8_t buf[600];
    Addr from;
    for (UdpSocket* s : {&listen_, &query_})
        if (s->is_open())
            for (int n; (n = s->recv_from(from, buf, sizeof buf)) > 0;) take(from, buf, n, now);
    games_.erase(std::remove_if(games_.begin(), games_.end(),
                                [&](const LanGame& g) { return now - g.seen > kExpireMs; }),
                 games_.end());
    players_.erase(std::remove_if(players_.begin(), players_.end(),
                                  [&](const LanPlayer& q) { return now - q.seen > kExpireMs; }),
                   players_.end());
    if (!nick_.empty() && query_.is_open() && (last_presence_ == 0 || now - last_presence_ >= kBeaconMs)) {
        for (std::uint32_t ip : targets_) send_presence(Addr{ip, kLanBeaconPort});
        last_presence_ = now;
    }
    if (query_.is_open() && (last_query_ == 0 || now - last_query_ >= kQueryMs)) {
        LanBeacon q;
        q.type = 2;
        q.net_proto = net_proto;
        q.sim_ver = sim_ver;
        q.rules_crc = rules_crc;
        const std::vector<std::uint8_t> b = encode_beacon(q);
        for (std::uint32_t ip : targets_) query_.send_to(Addr{ip, kLanBeaconPort}, b.data(), b.size());
        last_query_ = now;
    }
}

}  // namespace ra2mp
