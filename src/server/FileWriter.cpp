#include "server/FileWriter.hpp"

#include "common/TimeUtil.hpp"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <iomanip>
#include <sstream>
#include <utility>

namespace server {
namespace {

// Единственная задача счётчика - снизить шанс коллизии имени временного
// файла между несколькими одновременными сессиями в одном процессе; сама
// корректность гарантируется флагом O_EXCL при открытии, а не этим числом.
// Процесс однопоточный (epoll-реактор), поэтому обычный static, а не atomic.
std::uint64_t nextTempSeq() {
    static std::uint64_t seq = 0;
    return seq++;
}

std::string sysError() {
    return std::string(strerror(errno));
}

} // namespace

FileWriter::FileWriter(std::string dir) : m_dir(std::move(dir)) {}

FileWriter::~FileWriter() { abort(); }

std::string FileWriter::open() {
    std::ostringstream name;
    name << "/.nxft_" << getpid() << "_" << nextTempSeq() << ".part";
    m_tempPath = m_dir + name.str();

    m_fd = ::open(m_tempPath.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (m_fd < 0) {
        const std::string err = "не удалось создать " + m_tempPath + ": " + sysError();
        m_tempPath.clear();
        return err;
    }
    return "";
}

std::string FileWriter::write(const void* data, std::size_t len) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::size_t left = len;
    while (left > 0) {
        const ssize_t n = ::write(m_fd, p, left);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return "ошибка записи в " + m_tempPath + ": " + sysError();
        }
        p += n;
        left -= static_cast<std::size_t>(n);
        m_written += static_cast<std::uint64_t>(n);
    }
    return "";
}

FileWriter::CommitResult FileWriter::commit() {
    if (m_fd < 0) {
        return {"", "файл не открыт"};
    }
    if (::fsync(m_fd) != 0) {
        return {"", "fsync: " + sysError()};
    }
    ::close(m_fd);
    m_fd = -1;

    const std::string stamp = util::fileTimestamp();
    for (int suffix = 0; suffix < 1000; ++suffix) {
        std::string name = stamp + ".hex";
        if (suffix > 0) {
            std::ostringstream withSuffix;
            withSuffix << stamp << "_" << std::setfill('0') << std::setw(3) << suffix << ".hex";
            name = withSuffix.str();
        }
        const std::string path = m_dir + "/" + name;

        // link() не перезаписывает существующий файл - атомарная проверка
        // уникальности имени без гонки между несколькими сессиями.
        if (::link(m_tempPath.c_str(), path.c_str()) == 0) {
            ::unlink(m_tempPath.c_str());
            m_tempPath.clear();
            return {name, ""};
        }
        if (errno != EEXIST) {
            return {"", "не удалось создать " + path + ": " + sysError()};
        }
    }

    return {"", "исчерпаны варианты имени для " + stamp + ".hex"};
}

void FileWriter::abort() {
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    if (!m_tempPath.empty()) {
        ::unlink(m_tempPath.c_str());
        m_tempPath.clear();
    }
}

} // namespace server
