#include "protocol/Protocol.hpp"

#include "common/Crc32.hpp"
#include "protocol/ByteIO.hpp"

#include <cstring>

namespace protocol {

const char* typeName(Type t) {
    switch (t) {
        case Type::Hello: return "HELLO";
        case Type::Data: return "DATA";
        case Type::End: return "END";
        case Type::Ack: return "ACK";
    }
    return "UNKNOWN";
}

const char* statusText(Status s) {
    switch (s) {
        case Status::Ok: return "успешно";
        case Status::ProtocolError: return "нарушение протокола";
        case Status::CrcMismatch: return "не совпала контрольная сумма";
        case Status::SizeMismatch: return "не совпал размер файла";
        case Status::IoError: return "ошибка записи на диск";
        case Status::Timeout: return "таймаут передачи";
        case Status::Busy: return "сервер перегружен";
    }
    return "неизвестный код";
}

std::vector<std::uint8_t> buildFrame(Type type, const std::uint8_t* payload, std::size_t len) {
    std::vector<std::uint8_t> frame(HEADER_SIZE + len + TRAILER_SIZE);
    std::uint8_t* p = frame.data();

    putU32(p, MAGIC);
    p[4] = static_cast<std::uint8_t>(type);
    putU32(p + 5, static_cast<std::uint32_t>(len));
    putU32(p + HEADER_CRC_SPAN, util::crc32(p, HEADER_CRC_SPAN));

    if (len > 0) {
        std::memcpy(p + HEADER_SIZE, payload, len);
    }
    putU32(p + HEADER_SIZE + len, util::crc32(payload, len));
    return frame;
}

std::vector<std::uint8_t> buildHello(std::uint64_t fileSize, const std::string& name) {
    const std::size_t nameLen = name.size() > MAX_NAME_LEN ? MAX_NAME_LEN : name.size();
    std::vector<std::uint8_t> payload(8 + 2 + nameLen);
    putU64(payload.data(), fileSize);
    putU16(payload.data() + 8, static_cast<std::uint16_t>(nameLen));
    std::memcpy(payload.data() + 10, name.data(), nameLen);
    return buildFrame(Type::Hello, payload.data(), payload.size());
}

std::vector<std::uint8_t> buildData(const std::uint8_t* data, std::size_t len) {
    return buildFrame(Type::Data, data, len);
}

std::vector<std::uint8_t> buildEnd(std::uint32_t fileCrc) {
    std::uint8_t payload[4];
    putU32(payload, fileCrc);
    return buildFrame(Type::End, payload, sizeof(payload));
}

std::vector<std::uint8_t> buildAck(Status status, const std::string& message) {
    const std::size_t msgLen = message.size() > MAX_NAME_LEN ? MAX_NAME_LEN : message.size();
    std::vector<std::uint8_t> payload(1 + 2 + msgLen);
    payload[0] = static_cast<std::uint8_t>(status);
    putU16(payload.data() + 1, static_cast<std::uint16_t>(msgLen));
    std::memcpy(payload.data() + 3, message.data(), msgLen);
    return buildFrame(Type::Ack, payload.data(), payload.size());
}

std::optional<HelloPayload> parseHello(const std::uint8_t* data, std::size_t len) {
    if (len < 10) {
        return std::nullopt;
    }
    HelloPayload result;
    result.fileSize = getU64(data);
    const std::uint16_t nameLen = getU16(data + 8);
    if (len != static_cast<std::size_t>(10) + nameLen || nameLen > MAX_NAME_LEN) {
        return std::nullopt;
    }
    result.name.assign(reinterpret_cast<const char*>(data + 10), nameLen);
    return result;
}

std::optional<std::uint32_t> parseEnd(const std::uint8_t* data, std::size_t len) {
    if (len != 4) {
        return std::nullopt;
    }
    return getU32(data);
}

std::optional<AckPayload> parseAck(const std::uint8_t* data, std::size_t len) {
    if (len < 3) {
        return std::nullopt;
    }
    AckPayload result;
    result.status = static_cast<Status>(data[0]);
    const std::uint16_t msgLen = getU16(data + 1);
    if (len != static_cast<std::size_t>(3) + msgLen) {
        return std::nullopt;
    }
    result.message.assign(reinterpret_cast<const char*>(data + 3), msgLen);
    return result;
}

} // namespace protocol
