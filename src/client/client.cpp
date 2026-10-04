#include "muckaii/client/client.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

namespace muckaii::client
{
namespace
{
constexpr std::uint8_t  kFirstPrintable { 0x20 };
constexpr std::uint8_t  kLastPrintable { 0x7E };
constexpr std::uint8_t  kContinuationMask { 0xC0 };
constexpr std::uint8_t  kContinuation { 0x80 };
constexpr std::uint8_t  kContinuationBits { 0x3F };
constexpr unsigned      kContinuationShift { 6 };
constexpr std::uint32_t kMaxCodePoint { 0x10FFFF };

struct Transliteration
{
    std::uint32_t    mFirst {};
    std::uint32_t    mLast {};
    std::string_view mText;
};

// Code points are written as numbers: the repo's text files are 7-bit (root CLAUDE.md).
constexpr std::array<Transliteration, 12> kTransliterations { {
    { .mFirst = 0x00A0, .mLast = 0x00A0, .mText = " " },   // no-break space
    { .mFirst = 0x00AB, .mLast = 0x00AB, .mText = "\"" },  // left guillemet
    { .mFirst = 0x00B4, .mLast = 0x00B4, .mText = "'" },   // acute accent
    { .mFirst = 0x00BB, .mLast = 0x00BB, .mText = "\"" },  // right guillemet
    { .mFirst = 0x2002, .mLast = 0x200A, .mText = " " },   // en space .. hair space
    { .mFirst = 0x2010, .mLast = 0x2015, .mText = "-" },   // hyphen .. horizontal bar
    { .mFirst = 0x2018, .mLast = 0x201B, .mText = "'" },   // single quotation marks
    { .mFirst = 0x201C, .mLast = 0x201F, .mText = "\"" },  // double quotation marks
    { .mFirst = 0x2022, .mLast = 0x2022, .mText = "*" },   // bullet
    { .mFirst = 0x2026, .mLast = 0x2026, .mText = "..." }, // horizontal ellipsis
    { .mFirst = 0x202F, .mLast = 0x202F, .mText = " " },   // narrow no-break space
    { .mFirst = 0x2212, .mLast = 0x2212, .mText = "-" },   // minus sign
} };

// Decodes one UTF-8 code point at `index`, advancing it. nullopt for a malformed sequence, which
// is skipped one byte at a time.
std::optional<std::uint32_t> decode(std::string_view text, std::size_t& index) noexcept
{
    const auto    lead { static_cast<std::uint8_t>(text[index++]) };
    std::size_t   extra {};
    std::uint32_t value {};
    if (lead < kContinuation)
        return lead;
    if ((lead & 0xE0U) == 0xC0U)
    {
        extra = 1;
        value = lead & 0x1FU;
    }
    else if ((lead & 0xF0U) == 0xE0U)
    {
        extra = 2;
        value = lead & 0x0FU;
    }
    else if ((lead & 0xF8U) == 0xF0U)
    {
        extra = 3;
        value = lead & 0x07U;
    }
    else
        return std::nullopt;

    if (index + extra > text.size())
        return std::nullopt;
    for (std::size_t i { 0 }; i < extra; ++i)
    {
        const auto next { static_cast<std::uint8_t>(text[index + i]) };
        if ((next & kContinuationMask) != kContinuation)
            return std::nullopt;
        value = (value << kContinuationShift) | (next & kContinuationBits);
    }
    index += extra;
    if (value > kMaxCodePoint)
        return std::nullopt;
    return value;
}
} // namespace

std::string toSevenBit(std::string_view utf8)
{
    std::string out;
    out.reserve(utf8.size());
    std::size_t index { 0 };
    while (index < utf8.size())
    {
        const std::optional<std::uint32_t> codePoint { decode(utf8, index) };
        if (! codePoint.has_value())
            continue;
        const std::uint32_t value { *codePoint };
        if (value == '\t')
        {
            out.push_back(' ');
            continue;
        }
        if (value >= kFirstPrintable && value <= kLastPrintable)
        {
            out.push_back(static_cast<char>(value));
            continue;
        }
        for (const Transliteration& entry : kTransliterations)
        {
            if (value >= entry.mFirst && value <= entry.mLast)
            {
                out.append(entry.mText);
                break;
            }
        }
    }
    return out;
}
} // namespace muckaii::client
