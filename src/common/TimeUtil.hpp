#pragma once

#include <string>

namespace util {

/**
 * @brief Компактная временная метка для имени файла: "20260830_211500".
 *
 * Формат без пунктуации — годится как часть имени файла на любой файловой
 * системе. Используется сервером для сохранения принятых файлов как
 * date_time.hex.
 */
std::string fileTimestamp();

} // namespace util
