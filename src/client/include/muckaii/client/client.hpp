#pragma once

#include <string>
#include <string_view>

namespace muckaii::client
{
// The single send boundary's 7-bit filter (root CLAUDE.md, "Bytes, not strings"): UTF-8 text the
// player typed, reduced to printable 7-bit characters. Typographic punctuation is transliterated
// (curly quotes, dashes, the ellipsis, non-breaking spaces); a TAB becomes a space; anything else
// outside 0x20-0x7E, and any byte that is not well-formed UTF-8, is dropped.
[[nodiscard]] std::string toSevenBit(std::string_view utf8);
} // namespace muckaii::client
