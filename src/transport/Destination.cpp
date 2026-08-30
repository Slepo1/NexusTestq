#include "Destination.hpp"

#include <cctype>

namespace transport {

std::optional<Destination> Destination::parse(const std::string& uri) {
    std::string newHost;
    std::string newPort;

    if (!uri.empty() && uri.front() == '[') {
        // IPv6 в скобках: "[::1]:5555"
        const size_t close = uri.find(']');
        if (close == std::string::npos || close + 1 >= uri.size() || uri[close + 1] != ':') {
            return std::nullopt;
        }
        newHost = uri.substr(1, close - 1);
        newPort = uri.substr(close + 2);
    } else {
        const size_t colon = uri.rfind(':');
        if (colon == std::string::npos) {
            // Хоста нет вовсе — считаем, что дан только порт.
            // Метод статический, "текущего" объекта нет, поэтому берём
            // дефолт из самой структуры, а не откуда-то извне.
            newHost = "0.0.0.0";
            newPort = uri;
        } else {
            newHost = uri.substr(0, colon);
            newPort = uri.substr(colon + 1);
        }
    }

    if (newHost.empty() || newPort.empty()) {
        return std::nullopt;
    }
    for (char c : newPort) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return std::nullopt;
        }
    }

    Destination result;
    result.host = newHost;
    result.port = newPort;
    return result;
}

std::string Destination::str() const {
    const bool ipv6 = host.find(':') != std::string::npos;
    return (ipv6 ? "[" + host + "]" : host) + ":" + port;
}

} // namespace transport