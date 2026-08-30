#pragma once

#include "common/Crc32.hpp"
#include "protocol/FrameParser.hpp"
#include "protocol/Protocol.hpp"
#include "server/FileWriter.hpp"
#include "transport/ITransport.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace server {

/**
 * @brief Обработчик одного клиентского соединения в epoll-реакторе (роль
 *        "event handler" в терминологии паттерна Reactor).
 *
 * Реализован как явная машина состояний (enum State) — reactor требует,
 * чтобы обработчик события никогда не блокировался: Session обрабатывает
 * ровно то, что уже пришло, продвигается на шаг и возвращает управление,
 * а не ждёт остальные данные сама.
 */
class Session {
public:
    Session(std::uint64_t id, std::unique_ptr<transport::IConnection> conn, std::string outDir);
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    /**
     * @brief Обрабатывает события epoll для этого соединения.
     * @param events Битовая маска EPOLLIN/EPOLLOUT/EPOLLHUP/EPOLLERR.
     * @return false, если сессию нужно закрыть и удалить (успех, ошибка или
     *         обрыв соединения); true, если сессия продолжает работать.
     */
    bool handle(std::uint32_t events);

    /// @return Какие события сейчас интересны (EPOLLIN во время приёма,
    ///         EPOLLOUT во время отправки ACK) — для epoll_ctl(MOD).
    std::uint32_t interest() const;

    int fd() const { return m_conn->fd(); }
    std::uint64_t id() const { return m_id; }

    /// @return true, если с последней активности прошло больше timeout.
    bool expired(std::chrono::steady_clock::time_point now,
                 std::chrono::seconds timeout) const;

    /// Принудительное завершение (таймаут простоя, остановка сервера).
    /// Идемпотентна: повторный вызов на уже завершённой сессии безопасен и
    /// ничего не логирует повторно - можно звать безусловно, не проверяя
    /// заранее, завершилась ли сессия уже сама.
    void terminate(const std::string& reason);

private:
    enum class State {
        /// Ждём кадры от клиента (HELLO/DATA/END).
        Receiving,
        /// Дописываем ACK в сокет.
        SendingAck,
        /// ACK отправлен, ждём, пока клиент закроет соединение.
        Draining,
    };

    bool onReadable();
    bool processFrames();
    bool onWritable();

    bool onHello(const std::uint8_t* data, std::size_t len);
    bool onData(const std::uint8_t* data, std::size_t len);
    bool onEnd(const std::uint8_t* data, std::size_t len);

    void fail(protocol::Status status, const std::string& message);
    void succeed(const std::string& fileName);
    void queueAck(protocol::Status status, const std::string& message);

    void touch();
    std::string speedInfo() const;

    std::uint64_t m_id;
    std::unique_ptr<transport::IConnection> m_conn;
    std::string m_peer;
    std::string m_outDir;

    State m_state = State::Receiving;
    protocol::FrameParser m_parser;
    FileWriter m_writer;

    bool m_helloReceived = false;
    bool m_completed = false;
    /// Защита terminate() от повторного вызова (см. её тело).
    bool m_terminated = false;
    std::string m_sourceName;
    std::uint64_t m_expectedSize = 0;
    std::uint64_t m_receivedSize = 0;
    util::Crc32 m_crc;

    std::vector<std::uint8_t> m_out;
    std::size_t m_outPos = 0;

    std::chrono::steady_clock::time_point m_lastActivity;
    std::chrono::steady_clock::time_point m_startedAt;
};

} // namespace server
