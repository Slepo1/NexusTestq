#pragma once

#include <optional>
#include <string>

namespace transport {

// Если нужно дописать логику путей для com-порта, то добавлять нужно сюда
struct Destination {
    std::string host = "0.0.0.0";
    std::string port = "5555";

    /**
     * @brief Разбирает строку адреса вида "host:port", "[ipv6]:port" или "port"
     *        в структуру Destination.
     *
     * @param uri Строка адреса, например "127.0.0.1:5555" или "5555"
     *            (в последнем случае host принимает значение по умолчанию, "0.0.0.0").
     *
     * @return Заполненный Destination, если разбор успешен; std::nullopt, если
     *         строка не соответствует ожидаемому формату (порт не число, пустой
     *         хост, не закрыта скобка IPv6 и т.п.).
     */
    static std::optional<Destination> parse(const std::string& uri);

    // строчное представление конечного пути
    std::string str() const;
};

} // namespace transport