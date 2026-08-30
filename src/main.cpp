#include "client/Client.hpp"
#include "common/Log.hpp"
#include "common/Paths.hpp"
#include "server/Server.hpp"
#include "transport/Destination.hpp"
#include "transport/Factory.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

namespace {

constexpr const char* DEFAULT_PORT = "5555";

void printHelp(const std::string& prog) {
    std::cout << "Передача файлов между процессами (сервер и клиент в одном файле).\n"
              << "\n"
              << "Использование:\n"
              << "  " << prog << " -s [опции]              запустить сервер (ждёт файлы постоянно)\n"
              << "  " << prog << " -c <путь> [опции]       отправить файл и завершиться\n"
              << "\n"
              << "Общие опции:\n"
              << "  --port <порт>              порт TCP (по умолчанию " << DEFAULT_PORT << ")\n"
              << "  --destination <адрес>      адрес целиком вместо --host/--port, например\n"
              << "                             \"127.0.0.1:5555\" или \"[::1]:5555\"\n"
              << "  --timeout <сек>            таймаут неактивности (по умолчанию 30 для сервера,\n"
              << "                             15 для клиента)\n"
              << "  -h, --help                 эта справка\n"
              << "\n"
              << "Опции сервера:\n"
              << "  --dir <путь>               куда сохранять файлы (по умолчанию каталог программы)\n"
              << "  --max-sessions <N>         предел одновременных передач (по умолчанию 1024)\n"
              << "\n"
              << "Опции клиента:\n"
              << "  --host <адрес>             адрес сервера (по умолчанию 127.0.0.1)\n"
              << "\n"
              << "Принятый файл сохраняется как ГГГГММДД_ЧЧММСС.hex; при совпадении имени\n"
              << "добавляется суффикс _001.._999.\n";
}

/// Возвращает значение опции (следующий argv) или nullopt, если его нет.
std::optional<std::string> needValue(int argc, char** argv, int i, const std::string& optName) {
    if (i + 1 >= argc) {
        util::logError("опция ", optName, " требует значение");
        return std::nullopt;
    }
    return std::string(argv[i + 1]);
}

/// Разбирает беззнаковое число из текста опции; nullopt, если это не число.
std::optional<unsigned long> parseUnsigned(const std::string& text, const std::string& optName) {
    char* end = nullptr;
    const unsigned long v = std::strtoul(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0') {
        util::logError("некорректное значение ", optName, ": '", text, "'");
        return std::nullopt;
    }
    return v;
}

int runServer(int argc, char** argv) {
    transport::Destination dest;
    dest.host = "0.0.0.0";
    dest.port = DEFAULT_PORT;

    server::ServerConfig cfg;
    cfg.outDir = util::executableDir();

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port") {
            auto value = needValue(argc, argv, i, arg);
            if (!value) return 1;
            dest.port = *value;
            ++i;
        } else if (arg == "--destination") {
            auto value = needValue(argc, argv, i, arg);
            if (!value) return 1;
            auto parsed = transport::Destination::parse(*value);
            if (!parsed) {
                util::logError("некорректный адрес в --destination: '", *value, "'");
                return 1;
            }
            dest = *parsed;
            ++i;
        } else if (arg == "--dir") {
            auto value = needValue(argc, argv, i, arg);
            if (!value) return 1;
            cfg.outDir = *value;
            ++i;
        } else if (arg == "--timeout") {
            auto value = needValue(argc, argv, i, arg);
            if (!value) return 1;
            auto secs = parseUnsigned(*value, arg);
            if (!secs) return 1;
            cfg.idleTimeout = std::chrono::seconds(*secs);
            ++i;
        } else if (arg == "--max-sessions") {
            auto value = needValue(argc, argv, i, arg);
            if (!value) return 1;
            auto n = parseUnsigned(*value, arg);
            if (!n) return 1;
            cfg.maxSessions = static_cast<std::size_t>(*n);
            ++i;
        } else {
            util::logError("неизвестная опция сервера: ", arg);
            return 1;
        }
    }

    auto [listener, err] = transport::makeListener(dest);
    if (!listener) {
        util::logError(err);
        return 1;
    }

    server::Server srv(std::move(listener), cfg);
    return srv.run();
}

int runClient(int argc, char** argv) {
    if (argc < 3 || argv[2][0] == '-') {
        util::logError("не указан путь к файлу: ", argv[0], " -c <путь>");
        return 1;
    }

    client::ClientConfig cfg;
    cfg.filePath = argv[2];
    cfg.endpoint.host = "127.0.0.1";
    cfg.endpoint.port = DEFAULT_PORT;

    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--host") {
            auto value = needValue(argc, argv, i, arg);
            if (!value) return 1;
            cfg.endpoint.host = *value;
            ++i;
        } else if (arg == "--port") {
            auto value = needValue(argc, argv, i, arg);
            if (!value) return 1;
            cfg.endpoint.port = *value;
            ++i;
        } else if (arg == "--destination") {
            auto value = needValue(argc, argv, i, arg);
            if (!value) return 1;
            auto parsed = transport::Destination::parse(*value);
            if (!parsed) {
                util::logError("некорректный адрес в --destination: '", *value, "'");
                return 1;
            }
            cfg.endpoint = *parsed;
            ++i;
        } else if (arg == "--timeout") {
            auto value = needValue(argc, argv, i, arg);
            if (!value) return 1;
            auto secs = parseUnsigned(*value, arg);
            if (!secs) return 1;
            cfg.timeoutMs = static_cast<int>(*secs * 1000);
            ++i;
        } else {
            util::logError("неизвестная опция клиента: ", arg);
            return 1;
        }
    }

    return client::sendFile(cfg);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printHelp(argv[0]);
        return 1;
    }

    const std::string mode = argv[1];
    if (mode == "-h" || mode == "--help") {
        printHelp(argv[0]);
        return 0;
    }
    if (mode == "-s") {
        return runServer(argc, argv);
    }
    if (mode == "-c") {
        return runClient(argc, argv);
    }

    util::logError("неизвестный режим '", mode, "'");
    printHelp(argv[0]);
    return 1;
}
