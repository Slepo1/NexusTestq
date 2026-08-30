#include "common/Log.hpp"

#include <sys/time.h>

#include <ctime>
#include <iomanip>
#include <sstream>

namespace util {

namespace detail {

std::string timestamp() {
    struct timeval tv {};
    gettimeofday(&tv, nullptr);
    struct tm tmv {};
    localtime_r(&tv.tv_sec, &tmv);

    std::ostringstream out;
    out << std::setfill('0') << std::setw(4) << (tmv.tm_year + 1900) << '-' << std::setw(2)
        << (tmv.tm_mon + 1) << '-' << std::setw(2) << tmv.tm_mday << ' ' << std::setw(2)
        << tmv.tm_hour << ':' << std::setw(2) << tmv.tm_min << ':' << std::setw(2) << tmv.tm_sec
        << '.' << std::setw(3) << static_cast<int>(tv.tv_usec / 1000);
    return out.str();
}

} // namespace detail

} // namespace util
