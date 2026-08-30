#pragma once

#include "protocol/Protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace protocol {

/// Итог одного вызова FrameParser::next().
enum class ParseResult {
    /// Накопленных байт пока недостаточно — ждать следующего read().
    NeedMore,
    /// Кадр собран целиком, поля Frame заполнены.
    Ready,
    /// Поток повреждён/рассинхронизирован, поле error заполнено.
    Error,
};

/**
 * @brief Один разобранный кадр.
 *
 * @warning data указывает во внутренний буфер FrameParser — валиден только
 *          до следующего вызова next() или feed(). Нужно использовать сразу,
 *          не сохранять указатель на потом.
 */
struct Frame {
    Type type = Type::Data;
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

/// Результат FrameParser::next() — всё, что нужно вызывающему коду, одним значением.
struct ParseOutcome {
    ParseResult result = ParseResult::NeedMore;
    /// Осмыслен только при result == Ready.
    Frame frame;
    /// Осмыслен только при result == Error.
    std::string error;
};

/**
 * @brief Инкрементальный разбор кадров из байтового потока.
 *
 * Данные приходят из read() произвольными кусками — не по одному кадру за
 * раз (один read() может вернуть меньше кадра, ровно кадр или несколько
 * кадров подряд). FrameParser копит байты и отдаёт кадры только когда они
 * реально собраны целиком, независимо от того, как именно пришли байты.
 */
class FrameParser {
public:
    /// Добавляет очередной кусок байт, полученный от read().
    void feed(const std::uint8_t* data, std::size_t len);

    /// Пытается извлечь один кадр из уже накопленных данных.
    ParseOutcome next();

    /// @return Сколько байт сейчас накоплено и ещё не разобрано.
    std::size_t buffered() const { return m_buf.size() - m_pos; }

    /// Сбрасывает всё накопленное состояние.
    void reset();

private:
    void compact();

    std::vector<std::uint8_t> m_buf;
    std::size_t m_pos = 0;
};

} // namespace protocol
