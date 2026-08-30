#pragma once

#include <memory>
#include <string>

namespace transport {
// Интерфейс и для сервера, и для клиента
class IConnection {
public:
    virtual ~IConnection() = default;

    virtual ssize_t read(void* buf, size_t len) = 0;
    virtual ssize_t write(const void* buf, size_t len) = 0;

    virtual int fd() const = 0;
    // Адрес собеседника
    virtual std::string peer() const = 0;

    virtual void shutdownWrite() {}

    virtual void close() = 0;
        
};

// Интерфейс только для сервера
class IListener {
public:
    virtual ~IListener() = default;

    virtual std::unique_ptr<IConnection> accept() = 0;

    virtual int fd() const = 0;
    // Описание себя(Схема + адрес)
    virtual std::string describe() const = 0;
    virtual void close() = 0;
};

} // namespace transport