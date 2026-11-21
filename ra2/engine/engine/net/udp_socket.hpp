// engine/net/udp_socket.hpp -- minimal non-blocking IPv4 UDP socket.
//
// Winsock 2 on Windows (MinGW, links ws2_32), BSD sockets on Android / Linux.
// No SDL dependency so the net layer can be unit-tested in ra2assets_tests.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ra2::net {

// IPv4 endpoint. `ip` is in HOST byte order (127.0.0.1 == 0x7F000001).
struct NetAddr {
    uint32_t ip = 0;
    uint16_t port = 0;

    bool operator==(const NetAddr& o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const NetAddr& o) const { return !(*this == o); }
    bool operator<(const NetAddr& o) const { return ip != o.ip ? ip < o.ip : port < o.port; }
    std::string to_string() const;  // "a.b.c.d:port"
    // Parses "a.b.c.d" or "a.b.c.d:port" (dotted quad only; no DNS).
    static bool parse(const std::string& s, NetAddr& out, uint16_t default_port = 0);
    static NetAddr loopback(uint16_t port) { return NetAddr{0x7F000001u, port}; }
    static NetAddr broadcast(uint16_t port) { return NetAddr{0xFFFFFFFFu, port}; }
};

// Process-wide socket library init (WSAStartup, reference counted). Safe to
// call repeatedly; UdpSocket::open() calls it.
bool net_platform_init();
void net_platform_shutdown();

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& o) noexcept;
    UdpSocket& operator=(UdpSocket&& o) noexcept;

    // Bind to `port` (0 = ephemeral) on `bind_ip` (host order, 0 = INADDR_ANY)
    // and switch to non-blocking mode. SO_REUSEADDR is NOT set by default:
    // two processes must not silently share the game port. `reuse` sets it
    // (the LAN beacon listener: several games on one PC share UDP 50010).
    bool open(uint16_t port, uint32_t bind_ip = 0, bool reuse = false);
    void close();
    bool is_open() const { return fd_ != kInvalid; }
    uint16_t local_port() const;
    bool set_broadcast(bool on);

    // >= 0: bytes sent; -1: error; -2: would block.
    int send_to(const NetAddr& to, const uint8_t* data, size_t len);
    // >= 0: bytes received; -1: error; -2: nothing pending.
    // A Windows WSAECONNRESET (ICMP port unreachable from an earlier send) is
    // swallowed and reported as -2, like the original's FD_READ loop ignores it.
    int recv_from(NetAddr& from, uint8_t* buf, size_t cap);

    const std::string& last_error() const { return err_; }

private:
#if defined(_WIN32)
    using Fd = uintptr_t;
    static constexpr Fd kInvalid = ~static_cast<uintptr_t>(0);
#else
    using Fd = int;
    static constexpr Fd kInvalid = -1;
#endif
    Fd fd_ = kInvalid;
    std::string err_;
    void set_err(const char* what);
};

}  // namespace ra2::net
