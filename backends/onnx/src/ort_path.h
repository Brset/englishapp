// UTF-8 path -> ORTCHAR_T path for Ort::Session (wchar_t on Windows, char elsewhere).
#pragma once
#include <stdexcept>
#include <string>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace pron {

#ifdef _WIN32
inline std::wstring ort_path(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) throw std::runtime_error("invalid UTF-8 in path: " + utf8);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), &w[0], n);
    return w;
}
#else
inline std::string ort_path(const std::string& utf8) { return utf8; }
#endif

}  // namespace pron
