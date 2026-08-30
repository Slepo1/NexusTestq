#pragma once

#include <cstdint>
#include <string>

namespace util {

/**
 * @brief Каталог, в котором лежит исполняемый файл (через /proc/self/exe).
 * @return Путь к каталогу программы; "." если определить не удалось.
 */
std::string executableDir();

/// @return Имя файла без каталога ("a/b/file.txt" -> "file.txt").
std::string baseName(const std::string& path);

/**
 * @brief Читаемый размер: "1.4 МиБ" вместо "1468006".
 * @param bytes Размер в байтах.
 */
std::string formatSize(std::uint64_t bytes);

} // namespace util
