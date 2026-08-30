#include "server/Session.hpp"

#include "common/Log.hpp"
#include "common/Paths.hpp"

#include <errno.h>
#include <string.h>
#include <sys/epoll.h>

#include <iomanip>
#include <sstream>
#include <utility>

namespace server {

Session::Session(std::uint64_t id, std::unique_ptr<transport::IConnection> conn, std::string outDir)
    : m_id(id),
      m_conn(std::move(conn)),
      m_peer(m_conn->peer()),
      m_outDir(std::move(outDir)),
      m_writer(m_outDir),
      m_lastActivity(std::chrono::steady_clock::now()),
      m_startedAt(m_lastActivity) {}

Session::~Session() {
    if (m_conn) {
        m_conn->close();
    }
}

void Session::touch() { m_lastActivity = std::chrono::steady_clock::now(); }

bool Session::expired(std::chrono::steady_clock::time_point now,
                      std::chrono::seconds timeout) const {
    return now - m_lastActivity > timeout;
}

std::uint32_t Session::interest() const {
    return m_state == State::SendingAck ? static_cast<std::uint32_t>(EPOLLOUT)
                                        : static_cast<std::uint32_t>(EPOLLIN);
}

std::string Session::speedInfo() const {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - m_startedAt)
                             .count();
    if (elapsed < 5) {
        return util::formatSize(m_receivedSize) + " за " + std::to_string(elapsed) + " мс";
    }
    const double bps = static_cast<double>(m_receivedSize) * 1000.0 / static_cast<double>(elapsed);
    std::ostringstream out;
    out << util::formatSize(m_receivedSize) << " за " << std::fixed << std::setprecision(2)
        << (static_cast<double>(elapsed) / 1000.0) << " с ("
        << util::formatSize(static_cast<std::uint64_t>(bps)) << "/с)";
    return out.str();
}

bool Session::handle(std::uint32_t events) {
    if ((events & (EPOLLHUP | EPOLLERR)) != 0 && (events & EPOLLIN) == 0) {
        if (m_state != State::Draining) {
            terminate("соединение разорвано");
        }
        return false;
    }
    if ((events & EPOLLIN) != 0 && !onReadable()) {
        return false;
    }
    if ((events & EPOLLOUT) != 0 && !onWritable()) {
        return false;
    }
    return true;
}

bool Session::onReadable() {
    std::uint8_t buf[64 * 1024];
    for (;;) {
        const ssize_t n = m_conn->read(buf, sizeof(buf));
        if (n > 0) {
            touch();
            if (m_state == State::Receiving) {
                m_parser.feed(buf, static_cast<std::size_t>(n));
                if (!processFrames()) {
                    return m_state == State::SendingAck ? onWritable() : false;
                }
            }
            continue;
        }
        if (n == 0) {
            if (m_state == State::Draining) {
                return false;
            }
            if (m_completed) {
                return false;
            }
            terminate(m_helloReceived ? "клиент отключился, не завершив передачу"
                                      : "клиент отключился до начала передачи");
            return false;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }
        if (errno == EINTR) {
            continue;
        }
        terminate(std::string("ошибка чтения: ") + strerror(errno));
        return false;
    }
}

bool Session::processFrames() {
    for (;;) {
        auto outcome = m_parser.next();
        if (outcome.result == protocol::ParseResult::NeedMore) {
            return true;
        }
        if (outcome.result == protocol::ParseResult::Error) {
            fail(protocol::Status::ProtocolError, outcome.error);
            return false;
        }

        switch (outcome.frame.type) {
            case protocol::Type::Hello:
                if (!onHello(outcome.frame.data, outcome.frame.size)) {
                    return false;
                }
                break;
            case protocol::Type::Data:
                if (!onData(outcome.frame.data, outcome.frame.size)) {
                    return false;
                }
                break;
            case protocol::Type::End:
                if (!onEnd(outcome.frame.data, outcome.frame.size)) {
                    return false;
                }
                return false; // приём завершён (успешно или с ошибкой) - дальше только ACK
            case protocol::Type::Ack:
                fail(protocol::Status::ProtocolError, "клиент прислал ACK");
                return false;
        }
    }
}

bool Session::onHello(const std::uint8_t* data, std::size_t len) {
    if (m_helloReceived) {
        fail(protocol::Status::ProtocolError, "повторный HELLO");
        return false;
    }

    auto hello = protocol::parseHello(data, len);
    if (!hello) {
        fail(protocol::Status::ProtocolError, "некорректный HELLO");
        return false;
    }

    m_helloReceived = true;
    m_expectedSize = hello->fileSize;
    m_sourceName = hello->name.empty() ? "(без имени)" : util::baseName(hello->name);
    m_startedAt = std::chrono::steady_clock::now();

    const std::string err = m_writer.open();
    if (!err.empty()) {
        util::logError("#", m_id, " ", m_peer, ": ", err);
        fail(protocol::Status::IoError, err);
        return false;
    }

    util::logInfo("#", m_id, " ", m_peer, ": начат приём файла '", m_sourceName, "' (",
                  util::formatSize(m_expectedSize), ")");
    return true;
}

bool Session::onData(const std::uint8_t* data, std::size_t len) {
    if (!m_helloReceived) {
        fail(protocol::Status::ProtocolError, "DATA до HELLO");
        return false;
    }
    if (m_receivedSize + len > m_expectedSize) {
        fail(protocol::Status::SizeMismatch, "принято больше данных, чем заявлено в HELLO");
        return false;
    }

    const std::string err = m_writer.write(data, len);
    if (!err.empty()) {
        util::logError("#", m_id, " ", m_peer, ": ", err);
        fail(protocol::Status::IoError, err);
        return false;
    }

    m_crc.update(data, len);
    m_receivedSize += len;
    return true;
}

bool Session::onEnd(const std::uint8_t* data, std::size_t len) {
    if (!m_helloReceived) {
        fail(protocol::Status::ProtocolError, "END до HELLO");
        return false;
    }

    auto fileCrc = protocol::parseEnd(data, len);
    if (!fileCrc) {
        fail(protocol::Status::ProtocolError, "некорректный END");
        return false;
    }
    if (m_receivedSize != m_expectedSize) {
        fail(protocol::Status::SizeMismatch, "получено " + util::formatSize(m_receivedSize) +
                                                  " вместо " + util::formatSize(m_expectedSize));
        return false;
    }
    if (m_crc.value() != *fileCrc) {
        std::ostringstream msg;
        msg << "CRC файла 0x" << std::hex << m_crc.value() << " вместо 0x" << *fileCrc;
        fail(protocol::Status::CrcMismatch, msg.str());
        return false;
    }

    auto commitResult = m_writer.commit();
    if (!commitResult.error.empty()) {
        util::logError("#", m_id, " ", m_peer, ": ", commitResult.error);
        fail(protocol::Status::IoError, commitResult.error);
        return false;
    }

    succeed(commitResult.finalName);
    return true;
}

void Session::succeed(const std::string& fileName) {
    m_completed = true;
    util::logInfo("#", m_id, " ", m_peer, ": приём завершён, сохранено в ", fileName, " — ",
                  speedInfo());
    queueAck(protocol::Status::Ok, fileName);
}

void Session::fail(protocol::Status status, const std::string& message) {
    m_writer.abort();
    util::logWarn("#", m_id, " ", m_peer, ": передача прервана — ", protocol::statusText(status),
                  " (", message, ")");
    queueAck(status, message);
}

void Session::queueAck(protocol::Status status, const std::string& message) {
    m_out = protocol::buildAck(status, message);
    m_outPos = 0;
    m_state = State::SendingAck;
}

bool Session::onWritable() {
    if (m_state != State::SendingAck) {
        // Нет активного ACK для отправки - ложный вызов (например, эхо
        // EPOLLOUT от предыдущего события). Ничего не делаем, соединение
        // продолжает жить как обычно.
        return true;
    }

    while (m_outPos < m_out.size()) {
        const ssize_t n = m_conn->write(m_out.data() + m_outPos, m_out.size() - m_outPos);
        if (n > 0) {
            m_outPos += static_cast<std::size_t>(n);
            touch();
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        // Клиента уже нет - подтверждать некому, файл при успехе уже сохранён.
        util::logWarn("#", m_id, " ", m_peer, ": ошибка отправки ACK: ", strerror(errno));
        return false;
    }

    // ACK отправлен: закрываем передачу на запись и ждём, пока клиент закроет
    // свою сторону - это гарантирует, что данные ACK дошли, а не были
    // сброшены RST-ом при резком close().
    m_conn->shutdownWrite();
    m_state = State::Draining;
    touch();
    return true;
}

void Session::terminate(const std::string& reason) {
    if (m_terminated) {
        return;
    }
    m_terminated = true;

    if (!m_completed && !m_helloReceived) {
        util::logWarn("#", m_id, " ", m_peer, ": соединение закрыто без передачи данных — ",
                      reason);
    }
    if (!m_completed && m_helloReceived) {
        util::logWarn("#", m_id, " ", m_peer, ": передача прервана — ", reason, " (принято ",
                      util::formatSize(m_receivedSize), " из ", util::formatSize(m_expectedSize),
                      ", временный файл удалён)");
    }
    m_writer.abort();
}

} // namespace server
