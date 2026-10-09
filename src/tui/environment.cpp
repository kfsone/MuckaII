// The only file that includes <windows.h>, so the platform split stays out of the rest of the tui.
#include "environment.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace muckaii::tui
{
#ifdef _WIN32
namespace
{
// toWide widens an ASCII variable name for the Win32 API.
std::wstring toWide(std::string_view name)
{
    return std::wstring { name.begin(), name.end() };
}

// toUtf8 converts `wide` to UTF-8.
std::string toUtf8(const std::wstring& wide)
{
    if (wide.empty())
        return {};
    const int   length { WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr) };
    std::string value(static_cast<std::size_t>(std::max(length, 0)), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), value.data(), length, nullptr, nullptr);
    return value;
}
} // namespace

types::OptString findEnvironment(std::string_view name)
{
    const std::wstring key { toWide(name) };
    const DWORD        size { GetEnvironmentVariableW(key.c_str(), nullptr, 0) };
    if (size == 0)
        return std::nullopt;
    std::wstring wide(size, L'\0');
    const DWORD  written { GetEnvironmentVariableW(key.c_str(), wide.data(), size) };
    if (written >= size)
        return std::nullopt;
    wide.resize(written);
    std::string value { toUtf8(wide) };
    // The wide copy may be a password.
    SecureZeroMemory(wide.data(), wide.size() * sizeof(wchar_t));
    return value;
}

void removeEnvironment(std::string_view name)
{
    SetEnvironmentVariableW(toWide(name).c_str(), nullptr);
}
#else
types::OptString findEnvironment(std::string_view name)
{
    const char* const value { std::getenv(std::string { name }.c_str()) };
    if (value == nullptr)
        return std::nullopt;
    return std::string { value };
}

void removeEnvironment(std::string_view name)
{
    unsetenv(std::string { name }.c_str());
}
#endif
} // namespace muckaii::tui
