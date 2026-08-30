#pragma once

#include <cstdint>
#include <cstring>

namespace protocol {

// Клиент и сервер всегда работают на одной и той же архитектуре (x86/x86-64),
// поэтому здесь можно просто копировать байты числа "как есть", без явных
// битовых сдвигов под конкретный порядок байт — родной порядок машины и так
// будет одинаков по обе стороны соединения.

inline void putU16(std::uint8_t* p, std::uint16_t v) {
    std::memcpy(p, &v, sizeof(v));
}

inline std::uint16_t getU16(const std::uint8_t* p) {
    std::uint16_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

inline void putU32(std::uint8_t* p, std::uint32_t v) {
    std::memcpy(p, &v, sizeof(v));
}

inline std::uint32_t getU32(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

inline void putU64(std::uint8_t* p, std::uint64_t v) {
    std::memcpy(p, &v, sizeof(v));
}

inline std::uint64_t getU64(const std::uint8_t* p) {
    std::uint64_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

} // namespace protocol
