#pragma once

#include "transport/Destination.hpp"

#include <string>

namespace client {

/// Настройки одной клиентской передачи.
struct ClientConfig {
    transport::Destination endpoint;
    std::string filePath;
    // таймаут подключения и каждой операции ввода-вывода
    int timeoutMs = 15000;
};

/**
 * @brief Отправляет файл на сервер и дожидается подтверждения.
 * @return Код возврата процесса: 0 - файл принят и сохранён сервером;
 *         ненулевой - конкретная причина неудачи (см. README/main.cpp).
 */
int sendFile(const ClientConfig& cfg);

} // namespace client
