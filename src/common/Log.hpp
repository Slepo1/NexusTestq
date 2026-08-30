#pragma once

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace util {

namespace detail {

// Обычная (не шаблонная) функция — можно объявить здесь, а определить в
// Log.cpp: она не зависит от типов вызывающего кода, компилятору не нужно
// видеть её тело в месте вызова, достаточно объявления.
std::string timestamp();

/**
 * @brief Собирает одну строку лога и пишет её в поток одним вызовом.
 *
 * @tparam Args Типы аргументов сообщения — любые, для которых определён
 *              operator<<(std::ostream&, T) (числа, std::string, и т.д.).
 * @param out Поток вывода (std::cout для info, std::cerr для warn/error).
 * @param level Название уровня ("INFO"/"WARN"/"ERROR"), выравнивается по 5 символам.
 * @param args Части сообщения — печатаются подряд, без автоматических
 *             разделителей (как и с printf, пробелы пишете сами в аргументах).
 */
template <typename... Args>
void logLine(std::ostream& out, const char* level, const Args&... args) {
    // Строка сначала собирается целиком в памяти, а в поток уходит ОДНИМ
    // вызовом — иначе несколько подряд идущих "<<" в поток теоретически
    // могут перемежаться с выводом из другого потока выполнения.
    std::ostringstream line;
    line << "[" << timestamp() << "] " << std::setw(5) << std::left << level << " ";
    if constexpr (sizeof...(args) > 0) {
        (line << ... << args); // fold-выражение: разворачивается в line << args1 << args2 << ...
    }
    line << "\n";

    out << line.str();
    out.flush();
}

} // namespace detail

/// Логирует информационное сообщение в stdout.
template <typename... Args>
void logInfo(const Args&... args) {
    detail::logLine(std::cout, "INFO", args...);
}

/// Логирует предупреждение в stderr.
template <typename... Args>
void logWarn(const Args&... args) {
    detail::logLine(std::cerr, "WARN", args...);
}

/// Логирует ошибку в stderr.
template <typename... Args>
void logError(const Args&... args) {
    detail::logLine(std::cerr, "ERROR", args...);
}

} // namespace util
