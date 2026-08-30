#include "protocol/FrameParser.hpp"

#include "common/Crc32.hpp"
#include "protocol/ByteIO.hpp"

namespace protocol {

void FrameParser::feed(const std::uint8_t* data, std::size_t len) {
    compact();
    m_buf.insert(m_buf.end(), data, data + len);
}

void FrameParser::compact() {
    if (m_pos == 0) {
        return;
    }
    m_buf.erase(m_buf.begin(), m_buf.begin() + static_cast<std::ptrdiff_t>(m_pos));
    m_pos = 0;
}

void FrameParser::reset() {
    m_buf.clear();
    m_pos = 0;
}

ParseOutcome FrameParser::next() {
    const std::size_t avail = m_buf.size() - m_pos;
    if (avail < HEADER_SIZE) {
        return {}; 
    }

    const std::uint8_t* h = m_buf.data() + m_pos;

    if (getU32(h) != MAGIC) {
        return {ParseResult::Error, {}, "неверная сигнатура кадра"};
    }
    if (getU32(h + HEADER_CRC_SPAN) != util::crc32(h, HEADER_CRC_SPAN)) {
        return {ParseResult::Error, {}, "повреждён заголовок кадра (CRC)"};
    }

    // байт 4 - это type (1 байт), length начинается с 5
    const std::uint32_t len = getU32(h + 5);
    if (len > MAX_PAYLOAD) {
        return {ParseResult::Error, {}, "слишком большой кадр: " + std::to_string(len) + " Б"};
    }

    const std::size_t total = HEADER_SIZE + len + TRAILER_SIZE;
    if (avail < total) {
        // кадр ещё не пришёл целиком
        return {}; 
    }

    const std::uint8_t* payload = h + HEADER_SIZE;
    if (getU32(payload + len) != util::crc32(payload, len)) {
        return {ParseResult::Error, {}, "повреждены данные кадра (CRC)"};
    }

    const std::uint8_t rawType = h[4];
    if (rawType < static_cast<std::uint8_t>(Type::Hello) || rawType > static_cast<std::uint8_t>(Type::Ack)) {
        return {ParseResult::Error, {}, "неизвестный тип кадра: " + std::to_string(rawType)};
    }

    Frame frame;
    frame.type = static_cast<Type>(rawType);
    frame.data = payload;
    frame.size = len;

    m_pos += total;
    return {ParseResult::Ready, frame, {}};
}

} // namespace protocol
