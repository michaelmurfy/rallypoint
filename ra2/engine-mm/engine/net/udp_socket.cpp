// engine/net/udp_socket.cpp -- see udp_socket.hpp.
#include "net/udp_socket.hpp"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <mutex>

namespace ra2::net {

namespace {
std::mutex g_init_mu;
int g_init_refs = 0;
bool g_init_ok = false;
}  // namespace

bool net_platform_init() {
    std::lock_guard<std::mutex> lk(g_init_mu);
    if (g_init_refs++ > 0) return g_init_ok;
#if defined(_WIN32)
    WSADATA wsa;
    g_init_ok = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    g_init_ok = true;
#endif
    return g_init_ok;
}

void net_platform_shutdown() {
    std::lock_guard<std::mutex> lk(g_init_mu);
    if (g_init_refs == 0) return;
    if (--g_init_refs == 0) {
#if defined(_WIN32)
        if (g_init_ok) WSACleanup();
#endif
        g_init_ok = false;
    }
}

std::string NetAddr::to_string() const {
    char b[32];
    std::snprintf(b, sizeof b, "%u.%u.%u.%u:%u", (ip >> 24) & 255u, (ip >> 16) & 255u,
                  (ip >> 8) & 255u, ip & 255u, static_cast<unsigned>(port));
    return b;
}

bool NetAddr::parse(const std::string& s, NetAddr& out, uint16_t default_port) {
    unsigned a = 0, b = 0, c = 0, d = 0, p = default_port;
    char tail = 0;
    int n = std::sscanf(s.c_str(), "%u.%u.%u.%u:%u%c", &a, &b, &c, &d, &p, &tail);
    if (n != 4 && n != 5) return false;
    if (a > 255 || b > 255 || c > 255 || d > 255 || p > 65535) return false;
    out.ip = (a << 24) | (b << 16) | (c << 8) | d;
    out.port = static_cast<uint16_t>(p);
    return true;
}

UdpSocket::~UdpSocket() { close(); }

UdpSocket::UdpSocket(UdpSocket&& o) noexcept : fd_(o.fd_), err_(std::move(o.err_)) {
    o.fd_ = kInvalid;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& o) noexcept {
    if (this != &o) {
        close();
        fd_ = o.fd_;
        err_ = std::move(o.err_);
        o.fd_ = kInvalid;
    }
    return *this;
}

void UdpSocket::set_err(const char* what) {
    char b[128];
#if defined(_WIN32)
    std::snprintf(b, sizeof b, "%s: WSA error %d", what, WSAGetLastError());
#else
    std::snprintf(b, sizeof b, "%s: %s", what, std::strerror(errno));
#endif
    err_ = b;
}

bool UdpSocket::open(uint16_t port, uint32_t bind_ip, bool reuse) {
    close();
    if (!net_platform_init()) {
        err_ = "socket library init failed";
        return false;
    }
#if defined(_WIN32)
    SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        set_err("socket");
        return false;
    }
    fd_ = static_cast<Fd>(s);
#else
    int s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        set_err("socket");
        return false;
    }
    fd_ = s;
#endif
    if (reuse) {
        int one = 1;
        ::setsockopt(static_cast<decltype(s)>(fd_), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one),
                     sizeof one);
#if defined(SO_REUSEPORT) && !defined(_WIN32)
        ::setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
#endif
    }
    sockaddr_in sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(bind_ip);
    if (::bind(static_cast<decltype(s)>(fd_), reinterpret_cast<sockaddr*>(&sa), sizeof sa) != 0) {
        set_err("bind");
        close();
        return false;
    }
#if defined(_WIN32)
    u_long nb = 1;
    if (ioctlsocket(static_cast<SOCKET>(fd_), FIONBIO, &nb) != 0) {
        set_err("ioctlsocket(FIONBIO)");
        close();
        return false;
    }
#else
    int fl = fcntl(fd_, F_GETFL, 0);
    if (fl < 0 || fcntl(fd_, F_SETFL, fl | O_NONBLOCK) != 0) {
        set_err("fcntl(O_NONBLOCK)");
        close();
        return false;
    }
#endif
    return true;
}

void UdpSocket::close() {
    if (fd_ == kInvalid) return;
#if defined(_WIN32)
    ::closesocket(static_cast<SOCKET>(fd_));
#else
    ::close(fd_);
#endif
    fd_ = kInvalid;
    net_platform_shutdown();
}

uint16_t UdpSocket::local_port() const {
    if (fd_ == kInvalid) return 0;
    sockaddr_in sa;
    std::memset(&sa, 0, sizeof sa);
#if defined(_WIN32)
    int sl = sizeof sa;
    if (::getsockname(static_cast<SOCKET>(fd_), reinterpret_cast<sockaddr*>(&sa), &sl) != 0) return 0;
#else
    socklen_t sl = sizeof sa;
    if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&sa), &sl) != 0) return 0;
#endif
    return ntohs(sa.sin_port);
}

bool UdpSocket::set_broadcast(bool on) {
    if (fd_ == kInvalid) return false;
    int v = on ? 1 : 0;
#if defined(_WIN32)
    if (::setsockopt(static_cast<SOCKET>(fd_), SOL_SOCKET, SO_BROADCAST,
                     reinterpret_cast<const char*>(&v), sizeof v) != 0) {
#else
    if (::setsockopt(fd_, SOL_SOCKET, SO_BROADCAST, &v, sizeof v) != 0) {
#endif
        set_err("setsockopt(SO_BROADCAST)");
        return false;
    }
    return true;
}

int UdpSocket::send_to(const NetAddr& to, const uint8_t* data, size_t len) {
    if (fd_ == kInvalid) return -1;
    sockaddr_in sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(to.port);
    sa.sin_addr.s_addr = htonl(to.ip);
#if defined(_WIN32)
    int r = ::sendto(static_cast<SOCKET>(fd_), reinterpret_cast<const char*>(data),
                     static_cast<int>(len), 0, reinterpret_cast<sockaddr*>(&sa), sizeof sa);
    if (r == SOCKET_ERROR) {
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK) return -2;
        set_err("sendto");
        return -1;
    }
    return r;
#else
    ssize_t r = ::sendto(fd_, data, len, 0, reinterpret_cast<sockaddr*>(&sa), sizeof sa);
    if (r < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return -2;
        set_err("sendto");
        return -1;
    }
    return static_cast<int>(r);
#endif
}

int UdpSocket::recv_from(NetAddr& from, uint8_t* buf, size_t cap) {
    if (fd_ == kInvalid) return -1;
    sockaddr_in sa;
    std::memset(&sa, 0, sizeof sa);
#if defined(_WIN32)
    int sl = sizeof sa;
    for (;;) {
        int r = ::recvfrom(static_cast<SOCKET>(fd_), reinterpret_cast<char*>(buf),
                           static_cast<int>(cap), 0, reinterpret_cast<sockaddr*>(&sa), &sl);
        if (r == SOCKET_ERROR) {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK) return -2;
            if (e == WSAECONNRESET) continue;  // ICMP unreachable from a previous send
            if (e == WSAEMSGSIZE) {            // datagram larger than cap: truncated, drop it
                continue;
            }
            set_err("recvfrom");
            return -1;
        }
        from.ip = ntohl(sa.sin_addr.s_addr);
        from.port = ntohs(sa.sin_port);
        return r;
    }
#else
    socklen_t sl = sizeof sa;
    for (;;) {
        ssize_t r = ::recvfrom(fd_, buf, cap, 0, reinterpret_cast<sockaddr*>(&sa), &sl);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return -2;
            if (errno == EINTR || errno == ECONNREFUSED) continue;
            set_err("recvfrom");
            return -1;
        }
        from.ip = ntohl(sa.sin_addr.s_addr);
        from.port = ntohs(sa.sin_port);
        return static_cast<int>(r);
    }
#endif
}

}  // namespace ra2::net
