#pragma once

#include "transport/ITransport.hpp"
#include "transport/Destination.hpp"

#include <memory>
#include <string>
#include <utility>

namespace transport {

/**
 * @brief TCP-реализация IConnection — установленное TCP-соединение поверх
 *        файлового дескриптора сокета.
 *
 * Объект появляется одним из двух способов: как результат
 * TcpConnection::connect() (клиент подключается сам) или как результат
 * TcpListener::accept() (сервер принимает входящее соединение). После
 * создания оба случая неотличимы — read()/write() работают одинаково
 * независимо от того, кто инициировал соединение.
 */
class TcpConnection : public IConnection {
public:
    /**
     * @brief Оборачивает уже открытый файловый дескриптор соединения.
     *
     * @param fd Дескриптор установленного TCP-соединения.
     * @param peer Адрес собеседника в виде "host:port", используется только
     *             для логов.
     */
    TcpConnection(int fd, std::string peer);

    /// Закрывает дескриптор сокета, если он ещё не был закрыт явно через close().
    ~TcpConnection() override;

    /// Копирование запрещено: m_fd — эксклюзивный дескриптор ОС, копия привела
    /// бы к тому, что два объекта считали бы себя владельцами одного и того
    /// же соединения и оба вызвали бы close() на одном fd.
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    /// Перемещение тоже запрещено: объект используется в проекте только через
    /// unique_ptr, перемещение по значению нигде не требуется.
    TcpConnection(TcpConnection&&) = delete;
    TcpConnection& operator=(TcpConnection&&) = delete;

    /**
     * @brief Устанавливает TCP-соединение с адресом dest.
     *
     * @param dest Адрес назначения (host + port).
     * @param timeoutMs Таймаут подключения в миллисекундах.
     *
     * @return Пара {соединение, текст ошибки}. При успехе первый элемент —
     *         рабочее соединение, второй пуст. При неудаче первый элемент —
     *         nullptr, второй содержит причину (например, "Connection refused").
     */
    static std::pair<std::unique_ptr<TcpConnection>, std::string> connect(const Destination& dest,
                                                                           int timeoutMs);

    /**
     * @brief Читает данные из сокета (POSIX read()).
     *
     * @param buf Буфер, в который будут записаны полученные данные.
     * @param len Максимальное количество байт, которое можно прочитать.
     *
     * @return `>0` — количество прочитанных байт; `0` — удалённая сторона
     *         закрыла соединение; `<0` — ошибка, причина в errno (значения
     *         EAGAIN/EWOULDBLOCK означают "данных пока нет", это не сбой).
     */
    ssize_t read(void* buf, size_t len) override;

    /**
     * @brief Отправляет данные в сокет (POSIX write()).
     *
     * @param buf Буфер с данными для отправки.
     * @param len Количество байт, которое нужно отправить.
     *
     * @return `>0` — количество реально отправленных байт (может быть
     *         меньше len — это не ошибка, вызывающий код обязан дописать
     *         остаток); `<0` — ошибка, причина в errno.
     */
    ssize_t write(const void* buf, size_t len) override;

    /// @return Файловый дескриптор соединения, для регистрации в epoll.
    int fd() const override { return m_fd; }

    /// @return Адрес собеседника в виде "host:port".
    std::string peer() const override { return m_peer; }

    /// Полузакрытие на запись (shutdown(fd, SHUT_WR)) — сообщает удалённой
    /// стороне об окончании отправки данных, не закрывая приём.
    void shutdownWrite() override;

    /// Закрывает файловый дескриптор соединения.
    void close() override;

private:
    /// Дескриптор установленного соединения.
    int m_fd;
    /// Кэшированный результат peer(), считается один раз при создании.
    std::string m_peer;
};

/**
 * @brief TCP-реализация IListener — слушающий сокет, принимающий входящие
 *        подключения через accept().
 */
class TcpListener : public IListener {
public:
    /// Закрывает слушающий сокет, если он ещё не был закрыт явно через close().
    ~TcpListener() override;

    /// Копирование запрещено по той же причине, что и у TcpConnection —
    /// m_fd эксклюзивный, копия привела бы к двойному close().
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    /**
     * @brief Создаёт TCP-сокет, привязывает его к адресу dest и переводит
     *        в режим прослушивания (socket() -> bind() -> listen()).
     *        Слушающий сокет создаётся неблокирующим — принятые через
     *        него соединения отдаются неблокирующими автоматически (для epoll).
     *
     * @param dest Адрес и порт, на которых нужно принимать подключения
     *             (например, host="0.0.0.0" — слушать на всех интерфейсах).
     *
     * @return Пара {слушатель, текст ошибки}. При успехе первый элемент —
     *         готовый к работе слушатель, второй пуст. При неудаче первый
     *         элемент — nullptr, второй содержит причину (например,
     *         "Address already in use").
     */
    static std::pair<std::unique_ptr<TcpListener>, std::string> create(const Destination& dest);

    /**
     * @brief Принимает одно входящее соединение, если оно уже есть в очереди.
     *
     * @return Новое соединение, если кто-то подключился; `nullptr`, если
     *         новых подключений сейчас нет (errno == EAGAIN) — это не ошибка,
     *         а нормальный результат в неблокирующем режиме, вызывающий код
     *         просто продолжает ждать следующего события epoll.
     */
    std::unique_ptr<IConnection> accept() override;

    /// @return Файловый дескриптор слушающего сокета, для регистрации в epoll.
    int fd() const override { return m_fd; }

    /// @return Строка вида "tcp://host:port" — адрес, на котором идёт приём.
    std::string describe() const override { return m_desc; }

    /// Закрывает слушающий сокет.
    void close() override;

private:
    /// Конструктор приватный: объекты создаются только через create(),
    /// который гарантирует, что m_fd уже успешно прошёл bind()+listen().
    TcpListener(int fd, std::string desc);

    /// Дескриптор слушающего сокета.
    int m_fd;
    /// Кэшированный результат describe(), считается один раз при создании.
    std::string m_desc;
};

} // namespace transport
