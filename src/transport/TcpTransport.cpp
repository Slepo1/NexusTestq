#include "transport/TcpTransport.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <utility>

namespace transport {
namespace {

std::string sysError() { return std::string(strerror(errno)); }

std::string formatPeer(const struct sockaddr* addr, socklen_t len) {
    char host[NI_MAXHOST] = {0};
    char serv[NI_MAXSERV] = {0};
    if (getnameinfo(addr, len, host, sizeof(host), serv, sizeof(serv),
                    NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
        return "unknown";
    }
    std::string h(host);
    if (h.find(':') != std::string::npos) {
        h = "[" + h + "]"; // IPv6-адрес в квадратных скобках, чтобы не путать с ":порт"
    }
    return h + ":" + serv;
}

bool setBlocking(int fd, bool blocking) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    const int want = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
    return fcntl(fd, F_SETFL, want) == 0;
}

bool setIoTimeout(int fd, int timeoutMs) {
    struct timeval tv {};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0 &&
           setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
}

} // namespace

// TcpConnection
TcpConnection::TcpConnection(int fd, std::string peer) : m_fd(fd), m_peer(std::move(peer)) {}

TcpConnection::~TcpConnection() { close(); }

std::pair<std::unique_ptr<TcpConnection>, std::string> TcpConnection::connect(
    const Destination& dest, int timeoutMs) {
    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = nullptr;
    const int rc = getaddrinfo(dest.host.c_str(), dest.port.c_str(), &hints, &res);
    if (rc != 0) {
        return {nullptr, std::string("не удалось разрешить адрес: ") + gai_strerror(rc)};
    }

    std::string lastError = "нет подходящих адресов";
    for (struct addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
        const int fd = ::socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol);
        if (fd < 0) {
            lastError = sysError();
            continue;
        }

        if (!setBlocking(fd, false)) {
            lastError = sysError();
            ::close(fd);
            continue;
        }

        bool connected = ::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0;
        if (!connected && errno == EINPROGRESS) {
            struct pollfd pfd {};
            pfd.fd = fd;
            pfd.events = POLLOUT;
            const int pr = ::poll(&pfd, 1, timeoutMs);
            if (pr > 0) {
                int soErr = 0;
                socklen_t elen = sizeof(soErr);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soErr, &elen) == 0 && soErr == 0) {
                    connected = true;
                } else {
                    errno = soErr != 0 ? soErr : errno;
                    lastError = sysError();
                }
            } else if (pr == 0) {
                lastError = "таймаут подключения (" + std::to_string(timeoutMs) + " мс)";
            } else {
                lastError = sysError();
            }
        } else if (!connected) {
            lastError = sysError();
        }

        if (!connected) {
            ::close(fd);
            continue;
        }

        if (!setBlocking(fd, true) || !setIoTimeout(fd, timeoutMs)) {
            lastError = sysError();
            ::close(fd);
            continue;
        }

        const std::string peer = formatPeer(ai->ai_addr, ai->ai_addrlen);
        freeaddrinfo(res);
        return {std::unique_ptr<TcpConnection>(new TcpConnection(fd, peer)), ""};
    }

    freeaddrinfo(res);
    return {nullptr, "не удалось подключиться: " + lastError};
}

ssize_t TcpConnection::read(void* buf, size_t len) { return ::read(m_fd, buf, len); }

ssize_t TcpConnection::write(const void* buf, size_t len) {
    return ::send(m_fd, buf, len, MSG_NOSIGNAL);
}

void TcpConnection::shutdownWrite() {
    if (m_fd >= 0) {
        ::shutdown(m_fd, SHUT_WR);
    }
}

void TcpConnection::close() {
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

// TcpListener
TcpListener::TcpListener(int fd, std::string desc) : m_fd(fd), m_desc(std::move(desc)) {}

TcpListener::~TcpListener() { close(); }

std::pair<std::unique_ptr<TcpListener>, std::string> TcpListener::create(const Destination& dest) {
    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo* res = nullptr;
    const int rc = getaddrinfo(dest.host.c_str(), dest.port.c_str(), &hints, &res);
    if (rc != 0) {
        return {nullptr, std::string("не удалось разрешить адрес: ") + gai_strerror(rc)};
    }

    std::string lastError = "нет подходящих адресов";
    for (struct addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
        const int fd =
            ::socket(ai->ai_family, ai->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, ai->ai_protocol);
        if (fd < 0) {
            lastError = sysError();
            continue;
        }

        const int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        if (::bind(fd, ai->ai_addr, ai->ai_addrlen) != 0 || ::listen(fd, SOMAXCONN) != 0) {
            lastError = sysError();
            ::close(fd);
            continue;
        }

        const std::string desc = "tcp://" + formatPeer(ai->ai_addr, ai->ai_addrlen);
        freeaddrinfo(res);
        return {std::unique_ptr<TcpListener>(new TcpListener(fd, desc)), ""};
    }

    freeaddrinfo(res);
    return {nullptr, "не удалось открыть слушающий сокет: " + lastError};
}

std::unique_ptr<IConnection> TcpListener::accept() {
    struct sockaddr_storage addr {};
    socklen_t len = sizeof(addr);
    const int fd = ::accept4(m_fd, reinterpret_cast<struct sockaddr*>(&addr), &len,
                             SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0) {
        return nullptr;
    }

    const int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    return std::unique_ptr<IConnection>(
        new TcpConnection(fd, formatPeer(reinterpret_cast<struct sockaddr*>(&addr), len)));
}

void TcpListener::close() {
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

} // namespace transport
