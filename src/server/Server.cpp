#include "server/Server.hpp"

#include "common/Log.hpp"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <utility>
#include <vector>

namespace server {
namespace {

constexpr int MAX_EVENTS = 128;
constexpr int POLL_TIMEOUT_MS = 1000; // шаг для проверки таймаутов простоя

std::string sysError() {
    return std::string(strerror(errno));
}

} // namespace

Server::Server(std::unique_ptr<transport::IListener> listener, ServerConfig config)
    : m_listener(std::move(listener)), m_config(std::move(config)) {}

Server::~Server() {
    if (m_epollFd >= 0) {
        ::close(m_epollFd);
    }
    if (m_signalFd >= 0) {
        ::close(m_signalFd);
    }
}

bool Server::addFd(int fd, std::uint32_t events) {
    struct epoll_event ev {};
    ev.events = events;
    ev.data.fd = fd;
    return epoll_ctl(m_epollFd, EPOLL_CTL_ADD, fd, &ev) == 0;
}

bool Server::modFd(int fd, std::uint32_t events) {
    struct epoll_event ev {};
    ev.events = events;
    ev.data.fd = fd;
    return epoll_ctl(m_epollFd, EPOLL_CTL_MOD, fd, &ev) == 0;
}

void Server::delFd(int fd) {
    epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, nullptr);
}

int Server::run() {
    // Разрыв соединения не должен убивать процесс сигналом.
    ::signal(SIGPIPE, SIG_IGN);

    m_epollFd = epoll_create1(EPOLL_CLOEXEC);
    if (m_epollFd < 0) {
        util::logError("epoll_create1: ", sysError());
        return 1;
    }

    // SIGINT/SIGTERM приходят как обычное событие epoll - корректная
    // остановка без гонок с классическими обработчиками сигналов.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &mask, nullptr) != 0) {
        util::logError("sigprocmask: ", sysError());
        return 1;
    }
    m_signalFd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (m_signalFd < 0) {
        util::logError("signalfd: ", sysError());
        return 1;
    }

    if (!addFd(m_listener->fd(), EPOLLIN) || !addFd(m_signalFd, EPOLLIN)) {
        util::logError("epoll_ctl: ", sysError());
        return 1;
    }

    util::logInfo("сервер запущен: ", m_listener->describe());
    util::logInfo("каталог для принятых файлов: ", m_config.outDir);
    util::logInfo("ожидание подключений (Ctrl+C - остановка)");

    std::vector<struct epoll_event> events(MAX_EVENTS);
    bool running = true;
    while (running) {
        const int n = epoll_wait(m_epollFd, events.data(), MAX_EVENTS, POLL_TIMEOUT_MS);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            util::logError("epoll_wait: ", sysError());
            return 1;
        }

        for (int i = 0; i < n; ++i) {
            const int fd = events[i].data.fd;
            if (fd == m_listener->fd()) {
                acceptConnections();
            } else if (fd == m_signalFd) {
                struct signalfd_siginfo si {};
                while (::read(m_signalFd, &si, sizeof(si)) == sizeof(si)) {
                    util::logInfo("получен сигнал ", si.ssi_signo, ", останавливаюсь");
                }
                running = false;
            } else {
                handleSession(fd, events[i].events);
            }
        }

        sweepIdle();
    }

    // Незавершённые передачи обрываются, временные файлы удаляются деструкторами Session.
    const std::size_t pending = m_sessions.size();
    m_sessions.clear();
    m_listener->close();
    if (pending > 0) {
        util::logWarn("прервано незавершённых передач: ", pending);
    }
    util::logInfo("сервер остановлен");
    return 0;
}

void Server::acceptConnections() {
    for (;;) {
        auto conn = m_listener->accept();
        if (!conn) {
            return; // EAGAIN - новых соединений больше нет
        }

        const int fd = conn->fd();
        const std::string peer = conn->peer();

        if (m_sessions.size() >= m_config.maxSessions) {
            util::logWarn(peer, ": отклонено, достигнут предел одновременных передач (",
                          m_config.maxSessions, ")");
            const auto ack = protocol::buildAck(protocol::Status::Busy, "сервер перегружен");
            conn->write(ack.data(), ack.size());
            conn->close();
            continue;
        }

        auto session = std::make_unique<Session>(m_nextSessionId++, std::move(conn), m_config.outDir);
        const std::uint32_t interest = session->interest();
        if (!addFd(fd, interest)) {
            util::logError(peer, ": epoll_ctl(ADD): ", sysError());
            continue;
        }

        util::logInfo("#", session->id(), " ", peer, ": подключение принято");
        Entry entry;
        entry.session = std::move(session);
        entry.events = interest;
        m_sessions.emplace(fd, std::move(entry));
    }
}

void Server::handleSession(int fd, std::uint32_t events) {
    auto it = m_sessions.find(fd);
    if (it == m_sessions.end()) {
        delFd(fd);
        return;
    }

    if (!it->second.session->handle(events)) {
        // terminate() идемпотентна: если сессия уже сама себя корректно
        // завершила (успех/ошибка протокола), повторный вызов здесь ничего
        // не залогирует дважды - но гарантирует, что путь очистки всегда
        // один и тот же, а не продублирован вручную.
        closeSession(fd, "обработчик события завершил сессию");
        return;
    }

    const std::uint32_t want = it->second.session->interest();
    if (want != it->second.events) {
        if (!modFd(fd, want)) {
            util::logError("epoll_ctl(MOD): ", sysError());
            closeSession(fd, "ошибка перерегистрации в epoll");
            return;
        }
        it->second.events = want;
    }
}

void Server::closeSession(int fd, const std::string& reason) {
    auto it = m_sessions.find(fd);
    if (it == m_sessions.end()) {
        return;
    }
    it->second.session->terminate(reason);
    delFd(fd);
    m_sessions.erase(it);
}

void Server::sweepIdle() {
    if (m_sessions.empty()) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    std::vector<int> expired;
    for (const auto& kv : m_sessions) {
        if (kv.second.session->expired(now, m_config.idleTimeout)) {
            expired.push_back(kv.first);
        }
    }
    for (const int fd : expired) {
        closeSession(fd, "таймаут ожидания данных");
    }
}

} // namespace server
