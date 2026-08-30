#pragma once

#include <cstddef>
#include <cstdint>

namespace util {

/**
 * @brief Инкрементальный расчёт CRC-32 (IEEE 802.3, полином 0xEDB88320).
 *
 * Позволяет считать контрольную сумму по кускам, не имея всех данных сразу —
 * важно для протокола, где файл идёт по сети частями, а не единым блоком.
 */
class Crc32 {
public:
    /// Добавляет очередной кусок данных к уже накопленной сумме.
    void update(const void* data, std::size_t len);

    /// @return Итоговое значение CRC-32 по всем данным, переданным в update().
    std::uint32_t value() const { return m_crc ^ 0xFFFFFFFFu; }

    /// Сбрасывает накопленное состояние — можно считать сумму заново.
    void reset() { m_crc = 0xFFFFFFFFu; }

private:
    std::uint32_t m_crc = 0xFFFFFFFFu;
};

/// Удобный способ посчитать CRC-32 за один вызов, если все данные уже под рукой.
std::uint32_t crc32(const void* data, std::size_t len);

} // namespace util
