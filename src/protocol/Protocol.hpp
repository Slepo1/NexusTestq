#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace protocol {

// Формат кадра:
//   magic(4) type(1) length(4) headerCrc(4) | payload(length байт) | payloadCrc(4)
//
// headerCrc считается по первым 9 байтам (magic+type+length).
// payloadCrc считается по payload.
//
// HELLO : size(u64) nameLen(u16) name[nameLen]   клиент -> сервер, начало передачи
// DATA  : произвольный кусок файла                клиент -> сервер
// END   : fileCrc(u32)                             клиент -> сервер, конец передачи
// ACK   : status(u8) msgLen(u16) msg[msgLen]      сервер -> клиенту, итог приёма

constexpr std::uint32_t MAGIC = 0x5446584Eu; // "NXFT" в little-endian

constexpr std::size_t HEADER_CRC_SPAN = 9;  // magic(4) + type(1) + length(4) — то, что покрывает headerCrc
constexpr std::size_t HEADER_SIZE = 13;    // HEADER_CRC_SPAN + headerCrc(4)
constexpr std::size_t TRAILER_SIZE = 4;    // payloadCrc

constexpr std::uint32_t MAX_PAYLOAD = 64 * 1024; // защита от переполнения при испорченном length
constexpr std::uint16_t MAX_NAME_LEN = 1024;

/// Вид кадра — определяет, как интерпретировать payload.
enum class Type : std::uint8_t {
    Hello = 1,
    Data = 2,
    End = 3,
    Ack = 4,
};

/// Итог приёма файла, передаётся в ACK.
enum class Status : std::uint8_t {
    Ok = 0,
    ProtocolError = 1,
    CrcMismatch = 2,
    SizeMismatch = 3,
    IoError = 4,
    Timeout = 5,
    Busy = 6,
};

/// Человекочитаемое имя типа кадра, для логов.
const char* typeName(Type t);

/// Человекочитаемое описание статуса, для логов и текста ACK.
const char* statusText(Status s);

/// Разобранное содержимое кадра HELLO.
struct HelloPayload {
    std::uint64_t fileSize = 0;
    std::string name;
};

/// Разобранное содержимое кадра ACK.
struct AckPayload {
    Status status = Status::ProtocolError;
    std::string message;
};

/**
 * @brief Собирает произвольный кадр: заголовок + payload + payloadCrc.
 * @param type Вид кадра.
 * @param payload Указатель на данные payload (может быть nullptr при len == 0).
 * @param len Размер payload в байтах.
 * @return Готовые байты кадра, целиком.
 */
std::vector<std::uint8_t> buildFrame(Type type, const std::uint8_t* payload, std::size_t len);

/// Собирает кадр HELLO — размер файла и его имя.
std::vector<std::uint8_t> buildHello(std::uint64_t fileSize, const std::string& name);

/// Собирает кадр DATA — очередной кусок файла.
std::vector<std::uint8_t> buildData(const std::uint8_t* data, std::size_t len);

/// Собирает кадр END — CRC всего файла.
std::vector<std::uint8_t> buildEnd(std::uint32_t fileCrc);

/// Собирает кадр ACK — итог приёма.
std::vector<std::uint8_t> buildAck(Status status, const std::string& message);

/**
 * @brief Разбирает payload кадра HELLO.
 * @param data Указатель на payload (без заголовка кадра).
 * @param len Размер payload.
 * @return Заполненный HelloPayload при успехе; std::nullopt, если payload
 *         не соответствует формату (слишком короткий, nameLen не совпадает
 *         с реальной длиной остатка).
 */
std::optional<HelloPayload> parseHello(const std::uint8_t* data, std::size_t len);

/// Разбирает payload кадра END. std::nullopt, если размер payload не 4 байта.
std::optional<std::uint32_t> parseEnd(const std::uint8_t* data, std::size_t len);

/// Разбирает payload кадра ACK. std::nullopt, если payload короче минимума
/// или msgLen не совпадает с реальной длиной остатка.
std::optional<AckPayload> parseAck(const std::uint8_t* data, std::size_t len);

} // namespace protocol
