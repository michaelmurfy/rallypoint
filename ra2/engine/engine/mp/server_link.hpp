#pragma once
// engine/mp/server_link -- the game's client for ra2lobbyd's control plane
// (server/internal/lobby/proto.go; JSON lines over TCP 50000): hello /
// version gate, room list, create (room code), join by code, the address
// exchange (tunnel ids), keep-alive pings every 10 s. Non-blocking; the UI
// calls update() every frame and reads the state + event queue. SDL-free.
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "mp/mini_json.hpp"
#include "mp/mp_socket.hpp"

namespace ra2mp {

constexpr std::uint16_t kServerPort = 50000;

struct ServerRoom {
    std::string code, name, host, map;
    int players = 0, max = 0;
    bool started = false, pw = false;
    std::uint32_t sim = 0;
    std::string rules;
    int plat = 0;
};

struct ServerHello {
    std::string magic = "RA2N";
    int proto = 1;
    std::string ver;
    std::uint32_t sim = 1;
    std::uint32_t rules = 0;
    int plat = 1;                    // 1 Windows, 2 Android arm64, 3 Android x86_64
    std::uint64_t pid = 0;
    std::string nick;
};

// What the server told us (drained by the UI).
struct ServerEvent {
    enum Kind { HelloOk, Rejected, Rooms, Created, Joined, Peer, RoomClosed, Error, Notice, Disconnected } kind;
    std::string reason, detail, op, code, nick, host_nick;
    int cid = 0, relay_port = 0;
    std::uint32_t v3 = 0, host_v3 = 0;
    std::string relay_token;
};

class ServerLink {
public:
    enum class State { Idle, Resolving, Connecting, Hello, Ready, Failed };

    // "host[:port]" -> resolves (blocking DNS for names) and connects.
    bool connect(const std::string& address, const ServerHello& hello);
    void close();
    void update(std::uint64_t now);

    void list();
    void create(const std::string& name, int max, bool priv, bool pw, const std::string& map, std::uint32_t v3);
    void update_room(int players, int max, const std::string& map, bool started);
    void close_room();
    void join(const std::string& code, std::uint32_t v3, bool port_relay = false);

    State state() const { return state_; }
    const std::string& error() const { return err_; }
    const Addr& server() const { return addr_; }
    // From hello_ok.
    const std::string& server_name() const { return name_; }
    const std::string& motd() const { return motd_; }
    int v3_port() const { return v3_port_; }
    // The tunnel address: public_ip if the server advertised one, else the
    // address we dialled, with the V3 port.
    Addr tunnel_addr() const;
    const std::vector<ServerRoom>& rooms() const { return rooms_; }
    std::deque<ServerEvent>& events() { return events_; }

private:
    void send(const std::string& line);
    void on_line(const std::string& line);
    TcpStream tcp_;
    State state_ = State::Idle;
    ServerHello hello_;
    Addr addr_;
    std::string err_, name_, motd_, public_ip_;
    int v3_port_ = 0;
    std::vector<ServerRoom> rooms_;
    std::deque<ServerEvent> events_;
    std::uint64_t last_ping_ = 0, started_ = 0;
};

std::string hex64(std::uint64_t v);

}  // namespace ra2mp
