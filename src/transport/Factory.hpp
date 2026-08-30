#pragma once

#include "transport/Destination.hpp"
#include "transport/ITransport.hpp"

#include <memory>
#include <string>
#include <utility>

namespace transport {

/**
 * @brief Создаёт слушатель, готовый принимать входящие подключения на dest.
 *
 * Единственное место в проекте, где выбирается конкретная реализация
 * транспорта (сейчас — только TCP, через TcpListener::create()). Вызывающий
 * код получает обратно только абстрактный IListener и не знает, что именно
 * скрывается за ним.
 *
 * @param dest Адрес и порт, на которых нужно принимать подключения.
 *
 * @return Пара {слушатель, текст ошибки}. При успехе первый элемент — готовый
 *         к работе слушатель, второй пуст. При неудаче первый элемент —
 *         nullptr, второй содержит причину.
 */
std::pair<std::unique_ptr<IListener>, std::string> makeListener(const Destination& dest);

/**
 * @brief Устанавливает соединение с адресом dest.
 *
 * Аналог makeListener() для клиентской стороны: выбирает конкретную
 * реализацию транспорта (сейчас — только TCP, через TcpConnection::connect())
 * и возвращает результат уже в виде абстрактного IConnection.
 *
 * @param dest Адрес назначения.
 * @param timeoutMs Таймаут подключения в миллисекундах.
 *
 * @return Пара {соединение, текст ошибки}. При успехе первый элемент —
 *         рабочее соединение, второй пуст. При неудаче первый элемент —
 *         nullptr, второй содержит причину.
 */
std::pair<std::unique_ptr<IConnection>, std::string> makeConnection(const Destination& dest,
                                                                     int timeoutMs);

} // namespace transport
