#include "client/Client.hpp"

#include "common/Crc32.hpp"
#include "common/Log.hpp"
#include "common/Paths.hpp"
#include "protocol/FrameParser.hpp"
#include "protocol/Protocol.hpp"
#include "transport/Factory.hpp"

#include <errno.h>
#include <string.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace client {
namespace {

constexpr std::size_t CHUNK_SIZE = protocol::MAX_PAYLOAD;

std::string sysError() {
    return std::string(strerror(errno));
}

/**
 * @brief Пишет все len байт в соединение, повторяя попытку, пока
 *        write() отправляет меньше запрошенного (как и read(), write()
 *        не гарантирует отправку всего за один вызов).
 * @return Пустая строка при успехе; текст ошибки при неудаче.
 */
std::string writeAll(transport::IConnection& conn, const std::uint8_t* data, std::size_t len) {
    std::size_t sent = 0;
    while (sent < len) {
        const ssize_t n = conn.write(data + sent, len - sent);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return "таймаут отправки: сервер не читает данные";
        }
        return "ошибка отправки: " + sysError();
    }
    return "";
}

/// Результат ожидания одного кадра ACK от сервера.
struct AckResult {
    /// Осмыслен только когда error пуст.
    protocol::AckPayload ack;
    std::string error;
};

/// Блокирующе читает из соединения, пока FrameParser не соберёт кадр ACK целиком.
AckResult readAck(transport::IConnection& conn) {
    protocol::FrameParser parser;
    std::uint8_t buf[4096];

    for (;;) {
        auto outcome = parser.next();
        if (outcome.result == protocol::ParseResult::Error) {
            return {{}, "повреждён ответ сервера: " + outcome.error};
        }
        if (outcome.result == protocol::ParseResult::Ready) {
            if (outcome.frame.type != protocol::Type::Ack) {
                return {{}, std::string("неожиданный кадр от сервера: ") +
                                protocol::typeName(outcome.frame.type)};
            }
            auto ack = protocol::parseAck(outcome.frame.data, outcome.frame.size);
            if (!ack) {
                return {{}, "некорректный ACK от сервера"};
            }
            return {*ack, ""};
        }

        const ssize_t n = conn.read(buf, sizeof(buf));
        if (n > 0) {
            parser.feed(buf, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) {
            return {{}, "сервер закрыл соединение, не подтвердив приём"};
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return {{}, "таймаут ожидания подтверждения от сервера"};
        }
        return {{}, "ошибка чтения ответа: " + sysError()};
    }
}

} // namespace

int sendFile(const ClientConfig& cfg) {
    std::ifstream file(cfg.filePath, std::ios::binary | std::ios::ate);
    if (!file) {
        util::logError("не удалось открыть '", cfg.filePath, "'");
        return 2;
    }
    const std::uint64_t fileSize = static_cast<std::uint64_t>(file.tellg());
    file.seekg(0);
    if (!file) {
        util::logError("не удалось прочитать размер '", cfg.filePath, "'");
        return 2;
    }

    auto [conn, connErr] = transport::makeConnection(cfg.endpoint, cfg.timeoutMs);
    if (!conn) {
        util::logError(connErr);
        return 3;
    }

    const std::string name = util::baseName(cfg.filePath);
    util::logInfo("начата передача '", name, "' (", util::formatSize(fileSize), ") на ", conn->peer());

    const auto started = std::chrono::steady_clock::now();
    auto lastReport = started;

    const auto hello = protocol::buildHello(fileSize, name);
    if (std::string err = writeAll(*conn, hello.data(), hello.size()); !err.empty()) {
        util::logError("передача не начата: ", err);
        return 4;
    }

    std::vector<std::uint8_t> chunk(CHUNK_SIZE);
    util::Crc32 crc;
    std::uint64_t sent = 0;

    while (sent < fileSize) {
        file.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
        const std::streamsize n = file.gcount();
        if (n <= 0) {
            util::logError("файл '", cfg.filePath, "' изменился во время передачи (отправлено ",
                           util::formatSize(sent), " из ", util::formatSize(fileSize), ")");
            return 2;
        }

        crc.update(chunk.data(), static_cast<std::size_t>(n));
        const auto frame = protocol::buildData(chunk.data(), static_cast<std::size_t>(n));
        if (std::string err = writeAll(*conn, frame.data(), frame.size()); !err.empty()) {
            util::logError("передача прервана: ", err);
            return 4;
        }
        sent += static_cast<std::uint64_t>(n);

        const auto now = std::chrono::steady_clock::now();
        if (fileSize > 0 && now - lastReport >= std::chrono::seconds(1)) {
            lastReport = now;
            std::ostringstream percent;
            percent << std::fixed << std::setprecision(1)
                    << (100.0 * static_cast<double>(sent) / static_cast<double>(fileSize));
            util::logInfo("передано ", util::formatSize(sent), " из ", util::formatSize(fileSize),
                          " (", percent.str(), "%)");
        }
    }

    const auto end = protocol::buildEnd(crc.value());
    if (std::string err = writeAll(*conn, end.data(), end.size()); !err.empty()) {
        util::logError("не удалось завершить передачу: ", err);
        return 4;
    }

    auto ackResult = readAck(*conn);
    if (!ackResult.error.empty()) {
        util::logError("передача завершена без подтверждения: ", ackResult.error);
        return 5;
    }
    if (ackResult.ack.status != protocol::Status::Ok) {
        util::logError("сервер отклонил файл: ", protocol::statusText(ackResult.ack.status), " (",
                       ackResult.ack.message, ")");
        return 6;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    if (elapsed < 5) {
        util::logInfo("передача завершена: ", util::formatSize(fileSize), ", CRC32 0x", std::hex,
                      crc.value(), std::dec, ", сохранено сервером как '", ackResult.ack.message,
                      "' — ", elapsed, " мс");
    } else {
        const double seconds = static_cast<double>(elapsed) / 1000.0;
        const double bps = static_cast<double>(fileSize) / seconds;
        std::ostringstream out;
        out << util::formatSize(fileSize) << ", CRC32 0x" << std::hex << crc.value() << std::dec
            << ", сохранено сервером как '" << ackResult.ack.message << "' — " << std::fixed
            << std::setprecision(2) << seconds << " с (" << util::formatSize(static_cast<std::uint64_t>(bps))
            << "/с)";
        util::logInfo("передача завершена: ", out.str());
    }

    conn->shutdownWrite();
    conn->close();
    return 0;
}

} // namespace client
