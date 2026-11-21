// engine/mp/server_link.cpp -- see server_link.hpp.
#include "mp/server_link.hpp"

#include <cstdio>

namespace ra2mp {

namespace {
constexpr std::uint64_t kPingMs = 10000;
constexpr std::uint64_t kConnectTimeoutMs = 8000;

std::string hex32(std::uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "%08x", v);
    return b;
}
}  // namespace

std::string hex64(std::uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
    return b;
}

bool ServerLink::connect(const std::string& address, const ServerHello& hello) {
    close();
    hello_ = hello;
    state_ = State::Resolving;
    if (!resolve(address, kServerPort, addr_)) {
        state_ = State::Failed;
        err_ = "Cannot find server " + address;
        return false;
    }
    if (!tcp_.connect(addr_)) {
        state_ = State::Failed;
        err_ = "Cannot connect to " + addr_.str();
        return false;
    }
    state_ = State::Connecting;
    started_ = now_ms();
    JsonWriter w;
    w.str("t", "hello").str("magic", hello.magic).num("proto", hello.proto).str("ver", hello.ver)
        .u64("sim", hello.sim).str("rules", hex32(hello.rules)).num("plat", hello.plat)
        .str("pid", hex64(hello.pid)).str("nick", hello.nick);
    send(w.done());   // queued until the connect completes
    return true;
}

void ServerLink::close() {
    tcp_.close();
    state_ = State::Idle;
    rooms_.clear();
    err_.clear();
}

void ServerLink::send(const std::string& line) { tcp_.send(line + "\n"); }

Addr ServerLink::tunnel_addr() const {
    Addr a = addr_;
    std::uint32_t ip = 0;
    if (!public_ip_.empty() && parse_ipv4(public_ip_, ip)) a.ip = ip;
    a.port = static_cast<std::uint16_t>(v3_port_);
    return a;
}

void ServerLink::list() { if (state_ == State::Ready) send(JsonWriter().str("t", "list").done()); }

void ServerLink::create(const std::string& name, int max, bool priv, bool pw, const std::string& map, std::uint32_t v3) {
    if (state_ != State::Ready) return;
    JsonWriter w;
    w.str("t", "create").str("name", name).num("max", max).boolean("private", priv).boolean("pw", pw)
        .str("map", map).u64("v3", v3);
    send(w.done());
}

void ServerLink::update_room(int players, int max, const std::string& map, bool started) {
    if (state_ != State::Ready) return;
    JsonWriter w;
    w.str("t", "update").num("players", players).num("max", max).str("map", map).boolean("started", started);
    send(w.done());
}

void ServerLink::close_room() { if (state_ == State::Ready) send(JsonWriter().str("t", "close").done()); }

void ServerLink::join(const std::string& code, std::uint32_t v3, bool port_relay) {
    if (state_ != State::Ready) return;
    JsonWriter w;
    w.str("t", "join").str("code", code).u64("v3", v3).str("relay", port_relay ? "port" : "v3");
    send(w.done());
}

void ServerLink::on_line(const std::string& line) {
    Json m;
    if (!json_parse(line, m) || m.kind != Json::Object) return;
    const std::string t = m.s("t");
    ServerEvent ev{ServerEvent::Notice};
    if (t == "hello_ok") {
        state_ = State::Ready;
        name_ = m.s("server");
        motd_ = m.s("motd");
        v3_port_ = static_cast<int>(m.i("v3_port"));
        public_ip_ = m.s("public_ip");
        ev.kind = ServerEvent::HelloOk;
    } else if (t == "reject") {
        state_ = State::Failed;
        ev.kind = ServerEvent::Rejected;
        ev.reason = m.s("reason");
        ev.detail = m.s("detail");
        err_ = ev.detail.empty() ? "The server refused the connection (" + ev.reason + ")" : ev.detail;
    } else if (t == "rooms") {
        rooms_.clear();
        for (const Json& r : m["rooms"].arr) {
            ServerRoom sr;
            sr.code = r.s("code");
            sr.name = r.s("name");
            sr.host = r.s("host");
            sr.map = r.s("map");
            sr.players = static_cast<int>(r.i("players"));
            sr.max = static_cast<int>(r.i("max"));
            sr.started = r.flag("started");
            sr.pw = r.flag("pw");
            sr.sim = r.u32("sim");
            sr.rules = r.s("rules");
            sr.plat = static_cast<int>(r.i("plat"));
            rooms_.push_back(sr);
        }
        ev.kind = ServerEvent::Rooms;
    } else if (t == "created") {
        ev.kind = ServerEvent::Created;
        ev.code = m.s("code");
    } else if (t == "joined") {
        ev.kind = ServerEvent::Joined;
        ev.code = m.s("code");
        ev.cid = static_cast<int>(m.i("cid"));
        ev.host_nick = m.s("host_nick");
        ev.host_v3 = m.u32("host_v3");
        ev.relay_port = static_cast<int>(m.i("relay_port"));
        ev.relay_token = m.s("relay_token");
        if (m.has("v3_port")) v3_port_ = static_cast<int>(m.i("v3_port"));
        if (m.has("public_ip")) public_ip_ = m.s("public_ip");
    } else if (t == "peer") {
        ev.kind = ServerEvent::Peer;
        ev.cid = static_cast<int>(m.i("cid"));
        ev.nick = m.s("nick");
        ev.v3 = m.u32("v3");
        ev.relay_port = static_cast<int>(m.i("relay_port"));
        ev.relay_token = m.s("relay_token");
    } else if (t == "room_closed") {
        ev.kind = ServerEvent::RoomClosed;
        ev.code = m.s("code");
    } else if (t == "error") {
        ev.kind = ServerEvent::Error;
        ev.op = m.s("op");
        ev.reason = m.s("reason");
    } else if (t == "notice") {
        ev.kind = ServerEvent::Notice;
        ev.detail = m.s("text");
    } else if (t == "pong") {
        return;
    } else {
        return;
    }
    events_.push_back(ev);
}

void ServerLink::update(std::uint64_t now) {
    if (state_ == State::Idle || state_ == State::Failed) return;
    const TcpStream::State ts = tcp_.poll();
    std::string line;
    while (tcp_.pop_line(line)) on_line(line);
    if (state_ == State::Failed) return;
    if (ts == TcpStream::State::Failed) {
        state_ = State::Failed;
        err_ = tcp_.error().empty() ? "Disconnected from the server" : tcp_.error();
        events_.push_back(ServerEvent{ServerEvent::Disconnected});
        return;
    }
    if (state_ == State::Connecting && ts == TcpStream::State::Open) state_ = State::Hello;
    if ((state_ == State::Connecting || state_ == State::Hello) && now_ms() - started_ > kConnectTimeoutMs) {
        tcp_.close();
        state_ = State::Failed;
        err_ = "The server did not answer (" + addr_.str() + ")";
        return;
    }
    if (state_ == State::Ready && (last_ping_ == 0 || now - last_ping_ >= kPingMs)) {
        if (last_ping_ != 0) send(JsonWriter().str("t", "ping").done());
        last_ping_ = now;
    }
}

}  // namespace ra2mp
