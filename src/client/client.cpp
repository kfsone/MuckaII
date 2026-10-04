#include "muckaii/client/client.hpp"

namespace muckaii::client
{
std::errc parseCount(std::string_view text, int& count) noexcept
{
    const char* const first { text.data() };
    const char* const last { text.data() + text.size() };

    int        value {};
    const auto parsed { std::from_chars(first, last, value) };
    if (parsed.ec != std::errc {})
        return parsed.ec;

    if (parsed.ptr != last)
        return std::errc::invalid_argument;

    count = value;
    return std::errc {};
}
} // namespace muckaii::client
