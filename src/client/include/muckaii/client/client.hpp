#pragma once

#include <charconv>
#include <string_view>

namespace muckaii::client
{
// Parses the whole of `text` as a decimal count into `count`. Non-throwing, std::from_chars style:
// success is std::errc{}; on failure `count` is untouched.
std::errc parseCount(std::string_view text, int& count) noexcept;
} // namespace muckaii::client
