#include "common/Crc32.hpp"

#include <array>

namespace util {
namespace {

std::array<std::uint32_t, 256> makeTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        table[i] = c;
    }
    return table;
}

const std::array<std::uint32_t, 256>& table() {
    static const std::array<std::uint32_t, 256> t = makeTable();
    return t;
}

} // namespace

void Crc32::update(const void* data, std::size_t len) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    const auto& t = table();
    std::uint32_t c = m_crc;
    for (std::size_t i = 0; i < len; ++i) {
        c = t[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    }
    m_crc = c;
}

std::uint32_t crc32(const void* data, std::size_t len) {
    Crc32 c;
    c.update(data, len);
    return c.value();
}

} // namespace util
