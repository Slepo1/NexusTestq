#include "transport/Factory.hpp"

#include "transport/TcpTransport.hpp"

namespace transport {

// Сейчас Destination умеет описывать только TCP-адрес (host + port), поэтому
// выбирать пока не из чего — обе функции всегда идут в TcpListener/TcpConnection.
// Если позже в Destination появится поле схемы (например, для com-порта),
// ветвление по нему нужно будет добавить именно здесь — это единственное
// место в проекте, которое имеет право знать о конкретных реализациях
// транспорта.

std::pair<std::unique_ptr<IListener>, std::string> makeListener(const Destination& dest) {
    return TcpListener::create(dest);
}

std::pair<std::unique_ptr<IConnection>, std::string> makeConnection(const Destination& dest,
                                                                     int timeoutMs) {
    return TcpConnection::connect(dest, timeoutMs);
}

} // namespace transport
