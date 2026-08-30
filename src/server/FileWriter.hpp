#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace server {

/**
 * @brief Приём файла всегда идёт во временный файл; только полностью принятый
 *        и проверенный файл атомарно публикуется как date_time.hex.
 *
 * Оборванная передача не оставляет мусора: деструктор удаляет временный файл,
 * если commit() так и не был вызван.
 */
class FileWriter {
public:
    explicit FileWriter(std::string dir);
    ~FileWriter();

    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;
    FileWriter(FileWriter&&) = delete;
    FileWriter& operator=(FileWriter&&) = delete;

    /**
     * @brief Создаёт временный файл в каталоге dir.
     * @return Пустая строка при успехе; текст ошибки при неудаче.
     */
    std::string open();

    /**
     * @brief Дописывает очередной кусок данных во временный файл.
     * @return Пустая строка при успехе; текст ошибки при неудаче.
     */
    std::string write(const void* data, std::size_t len);

    /// Результат commit() — имя итогового файла и/или текст ошибки.
    struct CommitResult {
        /// Осмыслено только когда error пуст.
        std::string finalName;
        /// Пусто при успехе.
        std::string error;
    };

    /**
     * @brief Публикует файл как <dir>/ГГГГММДД_ЧЧММСС.hex.
     *
     * При совпадении имени (несколько клиентов в одну секунду) добавляется
     * суффикс _001.._999. Использует link()+unlink() вместо rename() — так
     * публикация не может случайно затереть уже существующий файл.
     */
    CommitResult commit();

    /// Удаляет временный файл, если он ещё существует. Безопасно вызывать
    /// повторно и после commit() — тогда просто ничего не делает.
    void abort();

    const std::string& tempPath() const { return m_tempPath; }
    std::uint64_t written() const { return m_written; }

private:
    std::string m_dir;
    std::string m_tempPath;
    int m_fd = -1;
    std::uint64_t m_written = 0;
};

} // namespace server
