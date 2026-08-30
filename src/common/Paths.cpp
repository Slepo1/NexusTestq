#include "common/Paths.hpp"

#include <unistd.h>

#include <array>
#include <iomanip>
#include <sstream>

namespace util {

std::string executableDir() {
    std::array<char, 4096> buf{};
    const ssize_t n = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
    if (n <= 0) {
        return ".";
    }

    const std::string path(buf.data(), static_cast<std::size_t>(n));
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return ".";
    }
    if (slash == 0) {
        return "/";
    }
    return path.substr(0, slash);
}

std::string baseName(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string formatSize(std::uint64_t bytes) {
    static const char* units[] = {"Б", "КиБ", "МиБ", "ГиБ", "ТиБ"};
    double v = static_cast<double>(bytes);
    std::size_t u = 0;
    while (v >= 1024.0 && u + 1 < sizeof(units) / sizeof(units[0])) {
        v /= 1024.0;
        ++u;
    }

    std::ostringstream out;
    if (u == 0) {
        out << bytes << " " << units[u];
    } else {
        out << std::fixed << std::setprecision(1) << v << " " << units[u];
    }
    return out.str();
}

} // namespace util
