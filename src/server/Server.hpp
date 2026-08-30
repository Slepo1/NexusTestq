#pragma once

#include "server/Session.hpp"
#include "transport/ITransport.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace server {

/// Настройки сервера, не относящиеся к самому транспорту.
struct ServerConfig {
    std::string outDir;
    std::chrono::seconds idleTimeout{30};
    std::size_t maxSessions = 1024;
};

/**
 * @brief Диспетчер паттерна Reactor: единственный поток на epoll, держит
 *        произвольное число одновременных Session и никогда не блокируется
 *        внутри обработки одного события.
 *
 * Не хранит глобального состояния - при необходимости масштабирования можно
 * поднять несколько таких реакторов в отдельных потоках, каждый со своим
 * epoll и своим набором сессий.
 */
class Server {
public:
    Server(std::unique_ptr<transport::IListener> listener, ServerConfig config);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;

    /**
     * @brief Запускает цикл реактора. Блокируется, пока не придёт
     *        SIGINT/SIGTERM или не случится фатальная ошибка epoll.
     * @return Код возврата процесса (0 - штатная остановка).
     */
    int run();

private:
    bool addFd(int fd, std::uint32_t events);
    bool modFd(int fd, std::uint32_t events);
    void delFd(int fd);

    void acceptConnections();
    void handleSession(int fd, std::uint32_t events);
    void closeSession(int fd, const std::string& reason);
    void sweepIdle();

    std::unique_ptr<transport::IListener> m_listener;
    ServerConfig m_config;

    int m_epollFd = -1;
    int m_signalFd = -1;
    std::uint64_t m_nextSessionId = 1;

    struct Entry {
        std::unique_ptr<Session> session;
        std::uint32_t events = 0;
    };
    std::unordered_map<int, Entry> m_sessions;
};

} // namespace server
