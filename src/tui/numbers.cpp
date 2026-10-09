#include "numbers.hpp"

#include <charconv>
#include <system_error>

namespace muckaii::tui
{
types::OptInt parseInt(std::string_view text) noexcept
{
    int        value {};
    const auto parsed { std::from_chars(text.data(), text.data() + text.size(), value) };
    if (parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size())
        return std::nullopt;
    return value;
}
} // namespace muckaii::tui
