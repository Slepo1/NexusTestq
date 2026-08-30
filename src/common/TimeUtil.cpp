#include "common/TimeUtil.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace util {

std::string fileTimestamp() {
    const std::time_t t = std::time(nullptr);
    struct tm tmv {};
    localtime_r(&t, &tmv);

    std::ostringstream out;
    out << std::setfill('0') << std::setw(4) << (tmv.tm_year + 1900) << std::setw(2)
        << (tmv.tm_mon + 1) << std::setw(2) << tmv.tm_mday << '_' << std::setw(2) << tmv.tm_hour
        << std::setw(2) << tmv.tm_min << std::setw(2) << tmv.tm_sec;
    return out.str();
}

} // namespace util
