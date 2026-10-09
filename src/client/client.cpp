#include "muckaii/client/client.hpp"

#include "muckaii/base/types.hpp"
#include "muckaii/base/wire.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

namespace muckaii::client
{
namespace
{
// kMaxCodePoint is the largest Unicode scalar value representable by UTF-8.
constexpr std::uint32_t kMaxCodePoint { 0x10FFFF };

// kTwoByteLeadMask and kTwoByteLead identify UTF-8 leads for two-byte code points.
constexpr std::uint8_t kTwoByteLeadMask { 0xE0 };
constexpr std::uint8_t kTwoByteLead { 0xC0 };

// kThreeByteLeadMask and kThreeByteLead identify UTF-8 leads for three-byte code points.
constexpr std::uint8_t kThreeByteLeadMask { 0xF0 };
constexpr std::uint8_t kThreeByteLead { 0xE0 };

// kFourByteLeadMask and kFourByteLead identify UTF-8 leads for four-byte code points.
constexpr std::uint8_t kFourByteLeadMask { 0xF8 };
constexpr std::uint8_t kFourByteLead { 0xF0 };

// kTwoBytePayloadMask, kThreeBytePayloadMask and kFourBytePayloadMask select lead payload bits.
constexpr std::uint8_t kTwoBytePayloadMask { 0x1F };
constexpr std::uint8_t kThreeBytePayloadMask { 0x0F };
constexpr std::uint8_t kFourBytePayloadMask { 0x07 };

// kNoBreakSpace is the Unicode code point transliterated to a plain space.
constexpr std::uint32_t kNoBreakSpace { 0x00A0 };

// kLeftGuillemet and kRightGuillemet are transliterated to double quotes.
constexpr std::uint32_t kLeftGuillemet { 0x00AB };
constexpr std::uint32_t kRightGuillemet { 0x00BB };

// kAcuteAccent is transliterated to an apostrophe.
constexpr std::uint32_t kAcuteAccent { 0x00B4 };

// kEnSpace and kHairSpace bound the Unicode spaces transliterated to a plain space.
constexpr std::uint32_t kEnSpace { 0x2002 };
constexpr std::uint32_t kHairSpace { 0x200A };

// kHyphen and kHorizontalBar bound the dash characters transliterated to a hyphen-minus.
constexpr std::uint32_t kHyphen { 0x2010 };
constexpr std::uint32_t kHorizontalBar { 0x2015 };

// kLeftSingleQuote and kRightSingleQuote bound the curly apostrophes transliterated to apostrophes.
constexpr std::uint32_t kLeftSingleQuote { 0x2018 };
constexpr std::uint32_t kRightSingleQuote { 0x201B };

// kLeftDoubleQuote and kRightDoubleQuote bound the curly double quotes transliterated to quotes.
constexpr std::uint32_t kLeftDoubleQuote { 0x201C };
constexpr std::uint32_t kRightDoubleQuote { 0x201F };

// kBullet is transliterated to an asterisk.
constexpr std::uint32_t kBullet { 0x2022 };

// kHorizontalEllipsis is transliterated to three periods.
constexpr std::uint32_t kHorizontalEllipsis { 0x2026 };

// kNarrowNoBreakSpace is transliterated to a plain space.
constexpr std::uint32_t kNarrowNoBreakSpace { 0x202F };

// kMinusSign is transliterated to a hyphen-minus.
constexpr std::uint32_t kMinusSign { 0x2212 };

// Transliteration maps an inclusive code-point range to printable ASCII text.
struct Transliteration
{
    std::uint32_t    mFirst {};
    std::uint32_t    mLast {};
    std::string_view mText;
};

// kTransliterationCount counts the supported Unicode transliteration ranges.
constexpr std::size_t kTransliterationCount { 12 };

// kTransliterations maps supported typographic punctuation and spaces to printable ASCII.
constexpr std::array<Transliteration, kTransliterationCount> kTransliterations { {
    { .mFirst = kNoBreakSpace, .mLast = kNoBreakSpace, .mText = " " },         // no-break space
    { .mFirst = kLeftGuillemet, .mLast = kLeftGuillemet, .mText = "\"" },      // left guillemet
    { .mFirst = kAcuteAccent, .mLast = kAcuteAccent, .mText = "'" },           // acute accent
    { .mFirst = kRightGuillemet, .mLast = kRightGuillemet, .mText = "\"" },    // right guillemet
    { .mFirst = kEnSpace, .mLast = kHairSpace, .mText = " " },                 // en space .. hair space
    { .mFirst = kHyphen, .mLast = kHorizontalBar, .mText = "-" },              // hyphen .. horizontal bar
    { .mFirst = kLeftSingleQuote, .mLast = kRightSingleQuote, .mText = "'" },  // single quotation marks
    { .mFirst = kLeftDoubleQuote, .mLast = kRightDoubleQuote, .mText = "\"" }, // double quotation marks
    { .mFirst = kBullet, .mLast = kBullet, .mText = "*" },                     // bullet
    { .mFirst = kHorizontalEllipsis, .mLast = kHorizontalEllipsis, .mText = "..." }, // horizontal ellipsis
    { .mFirst = kNarrowNoBreakSpace, .mLast = kNarrowNoBreakSpace, .mText = " " },   // narrow no-break space
    { .mFirst = kMinusSign, .mLast = kMinusSign, .mText = "-" },                     // minus sign
} };

// decode reads one UTF-8 code point at `index`; malformed input returns nullopt after consuming
// its lead byte so the caller can continue byte by byte.
types::OptUint32 decode(std::string_view text, std::size_t& index) noexcept
{
    const auto    lead { static_cast<std::uint8_t>(text[index++]) };
    std::size_t   extra {};
    std::uint32_t value {};
    if (lead < wire::kUtf8Continuation)
        return lead;
    if ((lead & kTwoByteLeadMask) == kTwoByteLead)
    {
        extra = 1;
        value = lead & kTwoBytePayloadMask;
    }
    else if ((lead & kThreeByteLeadMask) == kThreeByteLead)
    {
        extra = 2;
        value = lead & kThreeBytePayloadMask;
    }
    else if ((lead & kFourByteLeadMask) == kFourByteLead)
    {
        extra = 3;
        value = lead & kFourBytePayloadMask;
    }
    else
        return std::nullopt;

    if (index + extra > text.size())
        return std::nullopt;
    for (std::size_t i { 0 }; i < extra; ++i)
    {
        const auto next { static_cast<std::uint8_t>(text[index + i]) };
        if ((next & wire::kUtf8ContinuationMask) != wire::kUtf8Continuation)
            return std::nullopt;
        value = (value << wire::kUtf8ContinuationShift) | (next & wire::kUtf8ContinuationBits);
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
        const types::OptUint32 codePoint { decode(utf8, index) };
        if (! codePoint.has_value())
            continue;
        const std::uint32_t value { *codePoint };
        if (value == wire::kTab)
        {
            out.push_back(' ');
            continue;
        }
        if (value >= wire::kFirstPrintable && value <= wire::kLastPrintable)
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
