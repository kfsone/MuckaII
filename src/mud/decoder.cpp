#include "muckaii/mud/decoder.hpp"

#include "muckaii/base/types.hpp"
#include "muckaii/base/wire.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <string_view>
#include <utility>

namespace muckaii::mud
{
namespace
{
// Telnet (docs/spec/connection.md 2).
// An SB longer than this is abandoned (docs/spec/connection.md 2.1).
constexpr std::size_t kMaxSubnegotiation { 1024 };

// The MUD2 stream (docs/spec/protocol.md).
// kHeaderFirst and kHeaderLast delimit the bytes that can appear in a C1 header.
constexpr std::uint8_t kHeaderFirst { 0x9B };
constexpr std::uint8_t kHeaderLast { 0xFE };
// kHeaderBase converts a C1 header byte into its displayed numeric value.
constexpr std::uint8_t kHeaderBase { kHeaderFirst };
// CSI parameter and final-byte ranges follow the ANSI control-sequence grammar.
constexpr std::uint8_t kCsiParamFirst { wire::kFirstPrintable };
constexpr std::uint8_t kCsiParamLast { 0x3F };
constexpr std::uint8_t kCsiFinalFirst { 0x40 };
constexpr std::uint8_t kCsiFinalLast { wire::kLastPrintable };
constexpr char         kAsciiZero { '0' };
constexpr char         kAsciiNine { '9' };
constexpr char         kAsciiUpperA { 'A' };
constexpr char         kAsciiUpperZ { 'Z' };
constexpr char         kAsciiLowerA { 'a' };
constexpr char         kAsciiLowerZ { 'z' };
// The ASCII range constants identify letters and decimal digits used during parsing.
// Escape constants name the supported MUD2 ESC commands and ANSI CSI final bytes.
constexpr char             kEscapeDash { '-' };
constexpr char             kEscapeOpenBracket { '[' };
constexpr char             kEscapeClearScreen { 'C' };
constexpr char             kEscapeReverseOn { 'R' };
constexpr char             kEscapeReverseOff { 'r' };
constexpr char             kEscapeEraseEol { 'K' };
constexpr char             kEscapeEndSession { 'Q' };
constexpr char             kEscapeSetWidth { 'W' };
constexpr char             kCsiSgrFinal { 'm' };
constexpr char             kCsiClearScreenFinal { 'J' };
constexpr std::string_view kCsiClearScreenParameter { "2" };
// kNullByte is ignored inside an unfinished CSI sequence.
constexpr std::uint8_t kNullByte { 0 };
// Text parser markers identify tell quotes and comma-grouped readings.
constexpr char             kTellQuote { '"' };
constexpr char             kReadingGroupSeparator { ',' };
constexpr std::string_view kWhitespaceSeparators { " \t" };
constexpr std::string_view kYesFlag { "Y" };
constexpr std::string_view kNoFlag { "N" };
constexpr std::string_view kGenericSpellId { "11" };
constexpr std::string_view kGenericSpellPhraseId { "11 00" };
constexpr std::string_view kSpellPhraseIdOne { "11 01" };
constexpr std::string_view kSpellPhraseIdTwo { "11 02" };
constexpr std::string_view kSpellPhraseIdThree { "11 03" };
constexpr std::string_view kResetSignalIdOne { "06 04" };
constexpr std::string_view kResetSignalIdTwo { "06 06" };
// kMaxCsi bounds the stored parameter bytes of one CSI sequence.
constexpr std::size_t kMaxCsi { 64 };
// A numeric parameter in an ESC sequence stops growing here, so a run of digits cannot overflow
// int; no real sequence carries a number this large.
constexpr int kMaxEscapeNumber { 100000 };
// kDecimal is the radix used while accumulating an ESC width number.
constexpr int kDecimal { 10 };

// These colour constants define the base and snoop-frame palettes.
constexpr std::uint8_t kWhite { 7 };
constexpr std::uint8_t kBlack { 0 };
constexpr std::uint8_t kSnoopOddBg { 4 };
constexpr std::uint8_t kSnoopEvenBg { 5 };

// The following strings are exact server text, code ids, or collected delimiters.
constexpr std::string_view kOptionMenu { "Option (H for help)" };
constexpr std::string_view kWidthNoticeHead { "[New terminal width is " };
constexpr std::string_view kWidthNoticeTail { "]" };
constexpr std::string_view kTellsYou { " tells you" };
constexpr std::string_view kYouTell { "You tell " };
constexpr std::string_view kSomeone { "someone" };
constexpr std::string_view kInventorySplit { "========" };
constexpr std::string_view kRoomBlind { "--" };
constexpr std::string_view kRoomDark { "oo" };
constexpr std::string_view kTellId { "09 03" };
// kMaxDreamword bounds the number of lowercase letters retained from one dreamword.
constexpr std::size_t kMaxDreamword { 14 };

// FES (docs/spec/protocol.md "FES line"): field positions.
constexpr std::size_t kFesFieldsWithWeather { 15 };
constexpr std::size_t kFesFields { 14 };
constexpr std::size_t kFesStamina { 0 };
constexpr std::size_t kFesMaxStamina { 1 };
constexpr std::size_t kFesStrength { 2 };
constexpr std::size_t kFesMaxStrength { 3 };
constexpr std::size_t kFesDexterity { 4 };
constexpr std::size_t kFesMaxDexterity { 5 };
constexpr std::size_t kFesMagic { 6 };
constexpr std::size_t kFesMaxMagic { 7 };
constexpr std::size_t kFesScore { 8 };
constexpr std::size_t kFesBlind { 9 };
constexpr std::size_t kFesDeaf { 10 };
constexpr std::size_t kFesCrippled { 11 };
constexpr std::size_t kFesDumb { 12 };
constexpr std::size_t kFesReset { 13 };
constexpr std::size_t kFesWeather { 14 };
static_assert(kFesWeather + 1 == kFesFieldsWithWeather);
static_assert(kFesReset + 1 == kFesFields);

// The 95 blocks (docs/spec/protocol.md "Client-mode data (95)"); each constant is its expected line count.
constexpr std::size_t kClientModeLines { 5 };
constexpr std::size_t kAccountChangeLines { 2 };
// kUnknownPrivilege marks account data whose privilege field does not parse.
constexpr int kUnknownPrivilege { -1 };

// Sound-code family and parameter values from docs/spec/events.md "Code events".
constexpr std::uint8_t kFamilyUnsolicited { 6 };
constexpr std::uint8_t kFamilyHit { 7 };
constexpr std::uint8_t kFamilyFight { 8 };
constexpr std::uint8_t kFamilySpell { 11 };
constexpr std::uint8_t kFamilySound { 13 };
constexpr std::uint8_t kFamilyWeather { 14 };
constexpr std::uint8_t kFamilyEvent { 18 };
constexpr int          kFightYouHit { 1 };
constexpr int          kFightTheyHit { 3 };
constexpr int          kSpellFod { 6 };
constexpr int          kSpellWhere { 9 };
constexpr int          kSpellSummon { 14 };
constexpr int          kWeatherThird { 3 };
constexpr int          kWeatherFourth { 2 };
constexpr int          kMaxEventSound { 6 };

// SGR parameters (docs/spec/protocol.md "CSI"); each constant names one supported control value.
constexpr int kSgrReset { 0 };
constexpr int kSgrBold { 1 };
constexpr int kSgrItalic { 3 };
constexpr int kSgrUnderline { 4 };
constexpr int kSgrBlink { 5 };
constexpr int kSgrReverse { 7 };
constexpr int kSgrOff { 20 };
constexpr int kSgrFg { 30 };
constexpr int kSgrFgDefault { 39 };
constexpr int kSgrBg { 40 };
constexpr int kSgrBgDefault { 49 };
constexpr int kSgrBrightFg { 90 };
constexpr int kSgrBrightBg { 100 };
constexpr int kSgrColours { 8 };

// 38 and 48 select an extended foreground or background: "5;n" (256 colours) or "2;r;g;b" (RGB).
constexpr int kSgrExtendedFg { 38 };
constexpr int kSgrExtendedBg { 48 };
constexpr int kSgrExtended256 { 5 };
constexpr int kSgrExtended256Args { 2 };
constexpr int kSgrExtendedRgb { 2 };
constexpr int kSgrExtendedRgbArgs { 4 };

// isPrintableAscii reports whether a byte belongs to the printable 7-bit range.
bool isPrintableAscii(std::uint8_t byte) noexcept
{
    return byte >= wire::kFirstPrintable && byte <= wire::kLastPrintable;
}

// isDigit reports whether a byte is an ASCII decimal digit.
bool isDigit(std::uint8_t byte) noexcept
{
    return byte >= static_cast<std::uint8_t>(kAsciiZero) && byte <= static_cast<std::uint8_t>(kAsciiNine);
}

// isSpace reports whether a reply separator is a space or tab.
bool isSpace(char c) noexcept
{
    return c == ' ' || c == static_cast<char>(wire::kTab);
}

// toLower converts one ASCII uppercase letter to lowercase.
char toLower(char c) noexcept
{
    return (c >= kAsciiUpperA && c <= kAsciiUpperZ) ? static_cast<char>(c - kAsciiUpperA + kAsciiLowerA) : c;
}

// toLower converts every ASCII uppercase letter in text to lowercase.
std::string toLower(std::string_view text)
{
    std::string lower;
    lower.reserve(text.size());
    for (const char c : text)
        lower.push_back(toLower(c));
    return lower;
}

// trim removes leading and trailing spaces and tabs from a view.
std::string_view trim(std::string_view text) noexcept
{
    while (! text.empty() && isSpace(text.front()))
        text.remove_prefix(1);
    while (! text.empty() && isSpace(text.back()))
        text.remove_suffix(1);
    return text;
}

// parseInt returns a value only when the entire view is a valid integer.
types::OptInt parseInt(std::string_view text) noexcept
{
    int        value {};
    const auto parsed { std::from_chars(text.data(), text.data() + text.size(), value) };
    if (parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size())
        return std::nullopt;
    return value;
}

// parseFlag parses the protocol Y or N flag and rejects other values.
types::OptBool parseFlag(std::string_view text) noexcept
{
    if (text == kYesFlag)
        return true;
    if (text == kNoFlag)
        return false;
    return std::nullopt;
}

// toColour returns a valid colour index or the supplied fallback.
Colour toColour(std::uint8_t value, std::uint8_t fallback) noexcept
{
    return value <= kMaxColourIndex ? value : fallback;
}

// appendRun appends a character to the last run when its style matches.
void appendRun(std::vector<Run>& runs, char c, const Style& style)
{
    if (runs.empty() || runs.back().mStyle != style)
        runs.push_back(Run { .mText = std::string {}, .mStyle = style });
    runs.back().mText.push_back(c);
}

// isAllStars reports whether nonempty text contains only asterisks.
bool isAllStars(std::string_view text) noexcept
{
    if (text.empty())
        return false;
    for (const char c : text)
    {
        if (c != '*')
            return false;
    }
    return true;
}

// parseWidthNotice extracts the number from a complete width notice.
types::OptInt parseWidthNotice(std::string_view text) noexcept
{
    if (! text.starts_with(kWidthNoticeHead) || ! text.ends_with(kWidthNoticeTail))
        return std::nullopt;
    text.remove_prefix(kWidthNoticeHead.size());
    text.remove_suffix(kWidthNoticeTail.size());
    return parseInt(text);
}

// The spell_phrase id. A matched "11" is written in its two-number form: "11" and "11 00" are the
// same code (docs/spec/protocol.md "Parameters"), and the events.md schema enumerates "11 00".
// findSpellId returns the supported spell phrase id for a matched code.
types::OptStringView findSpellId(const CodeInfo* code) noexcept
{
    if (code == nullptr)
        return std::nullopt;
    if (code->mId == kGenericSpellId)
        return kGenericSpellPhraseId;
    if (code->mId == kSpellPhraseIdOne || code->mId == kSpellPhraseIdTwo || code->mId == kSpellPhraseIdThree)
        return code->mId;
    return std::nullopt;
}

// SoundParams are the numbers a sound code carries, in header order.
using SoundParams = std::vector<int>;

// OptSoundParams is SoundParams, or nothing for a silent header.
using OptSoundParams = std::optional<SoundParams>;

// findSoundParams returns the numbers sounds.toml's file-from-code rule needs, by the table in docs/spec/events.md
// "Code events".
// findSoundParams returns the sound parameters selected by the code family.
OptSoundParams findSoundParams(const Header& header)
{
    const std::size_t after { header.mCount - 1U };
    const int         first { header.mNumbers[1] };
    const int         second { header.mNumbers[2] };
    switch (header.mNumbers[0])
    {
        case kFamilyUnsolicited:
            return std::vector<int> {};
        case kFamilyHit:
            if (after == 0)
                return std::vector<int> { 0, 0 };
            if (after == 1)
                return std::vector<int> { first };
            if (after == 2)
                return std::vector<int> { first, second };
            return std::nullopt;
        case kFamilyFight:
            if (after >= 1 && (first == kFightYouHit || first == kFightTheyHit))
                return std::vector<int> { first };
            return std::nullopt;
        case kFamilySpell:
            if (after == 0)
                return std::vector<int> { 0 };
            if (after == 1 && first != kSpellFod && first != kSpellWhere && first != kSpellSummon)
                return std::vector<int> { first };
            return std::nullopt;
        case kFamilySound:
            if (after == 2)
                return std::vector<int> { first };
            return std::nullopt;
        case kFamilyWeather:
            if (after == 2 && first == kWeatherThird && second == kWeatherFourth)
                return std::vector<int> { first, second };
            return std::nullopt;
        case kFamilyEvent:
            if (after == 1 && first <= kMaxEventSound)
                return std::vector<int> { first };
            return std::nullopt;
        default:
            return std::nullopt;
    }
}
} // namespace

bool isWireTier(const Payload& payload) noexcept
{
    return std::holds_alternative<CodeEvent>(payload) || std::holds_alternative<CloseEvent>(payload)
           || std::holds_alternative<TelnetEvent>(payload);
}

Decoder::Decoder(DecoderOptions options)
    : mOptions { options }
{
}

void Decoder::feed(std::span<const std::byte> bytes, std::vector<Event>& out)
{
    for (const std::byte byte : bytes)
        onWireByte(std::to_integer<std::uint8_t>(byte), out);
}

void Decoder::endOfRead(std::vector<Event>& out)
{
    if (mIsInGameMode)
    {
        // A game-mode line that may yet prove to be all stars or the Option menu stays held.
        const bool isHoldingStars { ! mLine.mHasPrompt && isAllStars(mLine.mText) };
        const bool isHoldingMenu { ! mLine.mHasPrompt && kOptionMenu.starts_with(mLine.mText) };
        if (! isHoldingStars && ! isHoldingMenu)
            flushPending(out);
        return;
    }

    flushDeferred(out);
    if (mPending.empty())
        return;

    std::vector<Run> runs { std::move(mPending) };
    mPending.clear();
    mLine.mText.clear();
    mLine.mHasPrintable = false;
    // A line end since the last partial, even one whose line is not displayed (a width notice, a
    // 95 block), means this partial replaces the live one rather than extending it.
    emitRaw(
        PartialEvent { .mRuns = std::move(runs), .mIsContinued = mHasPartial && ! mHasLineEndSincePartial },
        out);
    mHasPartial             = true;
    mHasLineEndSincePartial = false;
}

void Decoder::endOfConnection(std::vector<Event>& out)
{
    endPhrase();
    if (mIsInGameMode)
        exitGame(true, out);
}

// -- Layer 1: telnet ---------------------------------------------------------------------------

void Decoder::onWireByte(std::uint8_t byte, std::vector<Event>& out)
{
    switch (mTelnetState)
    {
        case TelnetState::kData:
            if (byte == wire::kTelnetIac)
            {
                mTelnetState = TelnetState::kIac;
                return;
            }
            onDataByte(byte, out);
            return;
        case TelnetState::kIac:
            if (byte == wire::kTelnetIac)
            {
                mTelnetState = TelnetState::kData;
                onDataByte(wire::kTelnetIac, out);
                return;
            }
            mTelnet.assign({ std::byte { wire::kTelnetIac }, std::byte { byte } });
            if (byte >= wire::kTelnetWill && byte <= wire::kTelnetDont)
            {
                mTelnetState = TelnetState::kOption;
                return;
            }
            if (byte == wire::kTelnetSb)
            {
                mTelnetState = TelnetState::kSub;
                return;
            }
            mTelnetState = TelnetState::kData;
            emitTelnet(out);
            return;
        case TelnetState::kOption:
            mTelnet.push_back(std::byte { byte });
            mTelnetState = TelnetState::kData;
            emitTelnet(out);
            return;
        case TelnetState::kSub:
            if (byte == wire::kTelnetIac)
            {
                mTelnetState = TelnetState::kSubIac;
                return;
            }
            appendSubnegotiation(byte, out);
            return;
        case TelnetState::kSubIac:
            if (byte == wire::kTelnetIac)
            {
                mTelnetState = TelnetState::kSub;
                appendSubnegotiation(wire::kTelnetIac, out);
                return;
            }
            if (byte == wire::kTelnetSe)
            {
                mTelnetState = TelnetState::kData;
                mTelnet.push_back(std::byte { wire::kTelnetIac });
                mTelnet.push_back(std::byte { wire::kTelnetSe });
                emitTelnet(out);
                return;
            }
            // IAC and anything else inside an SB ends it: the SB is dropped, the command stands.
            mTelnetState = TelnetState::kIac;
            onWireByte(byte, out);
            return;
    }
}

void Decoder::appendSubnegotiation(std::uint8_t byte, std::vector<Event>& out)
{
    if (mTelnet.size() < kMaxSubnegotiation)
    {
        mTelnet.push_back(std::byte { byte });
        return;
    }
    // Past the ceiling the SB is abandoned and this byte is data again, so an unterminated SB
    // cannot swallow the stream (protocol.md "Telnet subnegotiation").
    mTelnet.clear();
    mTelnetState = TelnetState::kData;
    onDataByte(byte, out);
}

void Decoder::emitTelnet(std::vector<Event>& out)
{
    types::Bytes bytes { std::move(mTelnet) };
    mTelnet.clear();
    emit(TelnetEvent { .mBytes = std::move(bytes) }, out);
}

// -- Layer 2: the MUD2 stream ------------------------------------------------------------------

void Decoder::onDataByte(std::uint8_t byte, std::vector<Event>& out)
{
    if (mIsSwallowingWidth)
    {
        if (byte >= wire::kFirstHighByte)
        {
            mIsSwallowingWidth = false;
        }
        else
        {
            if (byte != wire::kLf)
                return;
            // The swallowed width notice's own line end: a hidden line ended.
            mHasLineEndSincePartial = true;
            mIsSwallowingWidth      = false;
            if (mLine.mText.empty() && mPending.empty())
                return;
        }
    }

    if (byte >= kHeaderFirst && byte <= kHeaderLast)
    {
        mEscape = EscapeState::kNone;
        if (! mIsInHeader)
        {
            endPhrase();
            endDreamword(out);
            mIsInHeader = true;
            mHeader     = Header {};
        }
        if (mHeader.mCount < kMaxHeaderNumbers)
            mHeader.mNumbers[mHeader.mCount++] = static_cast<std::uint8_t>(byte - kHeaderBase);
        return;
    }

    if (byte == wire::kTelnetIac)
    {
        mEscape = EscapeState::kNone;
        if (mIsInHeader)
        {
            mIsInHeader = false;
            onHeader(out);
            return;
        }
        endPhrase();
        endDreamword(out);
        onClose(out);
        return;
    }

    // A header that is not ended by 255 is abandoned.
    mIsInHeader = false;

    if (mEscape != EscapeState::kNone && onEscapeByte(byte, out))
        return;

    if (isPrintableAscii(byte) || byte == wire::kTab || (byte >= wire::kFirstHighByte && byte < kHeaderFirst))
    {
        onText(byte, out);
        return;
    }

    endPhrase();
    endDreamword(out);
    switch (byte)
    {
        case wire::kEsc:
            mEscape = EscapeState::kEsc;
            return;
        case wire::kLf:
            onLineEnd(out);
            return;
        case wire::kBel:
            emit(BellEvent {}, out);
            return;
        default:
            // CR and NUL are dropped; every other control byte is stripped.
            return;
    }
}

bool Decoder::onEscapeByte(std::uint8_t byte, std::vector<Event>& out)
{
    switch (mEscape)
    {
        case EscapeState::kNone:
            return false;
        case EscapeState::kEsc:
            if (byte == static_cast<std::uint8_t>(kEscapeDash))
            {
                mEscape = EscapeState::kEscDash;
                return true;
            }
            if (byte == static_cast<std::uint8_t>(kEscapeOpenBracket))
            {
                mEscape = EscapeState::kCsi;
                mCsi.clear();
                return true;
            }
            mEscape = EscapeState::kNone;
            return false;
        case EscapeState::kEscDash:
            mEscape = EscapeState::kNone;
            if (isDigit(byte))
            {
                mEscape       = EscapeState::kEscDashDigits;
                mEscapeNumber = byte - static_cast<std::uint8_t>(kAsciiZero);
                return true;
            }
            if (byte == static_cast<std::uint8_t>(kEscapeClearScreen))
                onClearScreen(out);
            else if (byte == static_cast<std::uint8_t>(kEscapeReverseOn))
                mIsEscReverse = true;
            else if (byte == static_cast<std::uint8_t>(kEscapeReverseOff))
                mIsEscReverse = false;
            else if (byte == static_cast<std::uint8_t>(kEscapeEraseEol))
                emit(EraseEolEvent {}, out);
            else if (byte == static_cast<std::uint8_t>(kEscapeEndSession))
                emit(EndOfSessionEvent {}, out);
            return true;
        case EscapeState::kEscDashDigits:
            if (isDigit(byte))
            {
                const int digit { byte - static_cast<std::uint8_t>(kAsciiZero) };
                const int nextNumber { (mEscapeNumber * kDecimal) + digit };
                mEscapeNumber = std::min(nextNumber, kMaxEscapeNumber);
                return true;
            }
            mEscape = EscapeState::kNone;
            if (byte == static_cast<std::uint8_t>(kEscapeSetWidth))
            {
                emit(WidthAckEvent { .mColumns = mEscapeNumber }, out);
                mIsSwallowingWidth = true;
            }
            return true;
        case EscapeState::kCsi:
            // Clio's workaround: a CR or LF inside a CSI does not end it.
            if (byte == wire::kCr || byte == wire::kLf || byte == kNullByte)
                return true;
            if (byte >= kCsiParamFirst && byte <= kCsiParamLast)
            {
                if (mCsi.size() < kMaxCsi)
                    mCsi.push_back(static_cast<char>(byte));
                return true;
            }
            mEscape = EscapeState::kNone;
            if (byte >= kCsiFinalFirst && byte <= kCsiFinalLast)
            {
                onCsiFinal(byte, out);
                return true;
            }
            return false;
    }
    return false;
}

void Decoder::onCsiFinal(std::uint8_t final, std::vector<Event>& out)
{
    if (final == static_cast<std::uint8_t>(kCsiSgrFinal))
    {
        applySgr();
        return;
    }
    if (final == static_cast<std::uint8_t>(kCsiClearScreenFinal)
        && (mCsi.empty() || mCsi == kCsiClearScreenParameter))
        onClearScreen(out);
}

void Decoder::resetAnsiStyle() noexcept
{
    mAnsiFg.reset();
    mAnsiBg.reset();
    mAnsiAttrs = 0;
}

// isPlainSgr reports whether `parameters` holds only digits and semicolons. A private-use prefix
// ('>', '?', '=', '<') or a sub-parameter colon makes it another sequence that ends in 'm'.
static bool isPlainSgr(std::string_view parameters) noexcept
{
    return std::ranges::all_of(parameters,
                               [](char c)
                               {
                                   return c == ';' || (c >= '0' && c <= '9');
                               });
}

// getExtendedColourArgCount returns how many parameters follow an extended-colour selector (38 or
// 48) that MuckaII cannot draw: 2 for "5;n" and 4 for "2;r;g;b". Skipping them keeps their numbers
// from being read as attributes.
static int getExtendedColourArgCount(types::OptInt mode) noexcept
{
    if (mode == kSgrExtended256)
        return kSgrExtended256Args;
    if (mode == kSgrExtendedRgb)
        return kSgrExtendedRgbArgs;
    return 0;
}

void Decoder::applySgr()
{
    if (! isPlainSgr(mCsi))
        return;
    std::vector<types::OptInt> items;
    std::string_view           rest { mCsi };
    for (bool isMore { true }; isMore;)
    {
        const std::size_t      split { rest.find(';') };
        const std::string_view item { rest.substr(0, split) };
        items.push_back(item.empty() ? types::OptInt { kSgrReset } : parseInt(item));
        isMore = split != std::string_view::npos;
        if (isMore)
            rest.remove_prefix(split + 1);
    }
    for (std::size_t i {}; i < items.size(); ++i)
    {
        if (! items[i].has_value())
            continue;
        if (*items[i] == kSgrExtendedFg || *items[i] == kSgrExtendedBg)
        {
            const types::OptInt mode { i + 1 < items.size() ? items[i + 1] : types::OptInt {} };
            i += static_cast<std::size_t>(getExtendedColourArgCount(mode));
            continue;
        }
        applySgrParameter(*items[i]);
    }
}
void Decoder::applySgrParameter(int code)
{
    static constexpr std::array<std::pair<int, Attr>, 5> kAttrs { {
        { kSgrBold, Attr::kBold },
        { kSgrItalic, Attr::kItalic },
        { kSgrUnderline, Attr::kUnderline },
        { kSgrBlink, Attr::kBlink },
        { kSgrReverse, Attr::kReverse },
    } };

    if (code == kSgrReset)
    {
        resetAnsiStyle();
        return;
    }
    for (const auto& [on, attr] : kAttrs)
    {
        // 22 turns off bold (as 21 would), 23 italic, 24 underline, 25 blink, 27 reverse.
        const int off { on == kSgrBold ? on + kSgrOff + 1 : on + kSgrOff };
        if (code == on)
            mAnsiAttrs |= static_cast<std::uint8_t>(attr);
        else if (code == off)
            mAnsiAttrs &= static_cast<std::uint8_t>(~static_cast<std::uint8_t>(attr));
    }
    if (code >= kSgrFg && code < kSgrFg + kSgrColours)
        mAnsiFg = static_cast<std::uint8_t>(code - kSgrFg);
    else if (code == kSgrFgDefault)
        mAnsiFg.reset();
    else if (code >= kSgrBg && code < kSgrBg + kSgrColours)
        mAnsiBg = static_cast<std::uint8_t>(code - kSgrBg);
    else if (code == kSgrBgDefault)
        mAnsiBg.reset();
    else if (code >= kSgrBrightFg && code < kSgrBrightFg + kSgrColours)
        mAnsiFg = static_cast<std::uint8_t>(code - kSgrBrightFg + kSgrColours);
    else if (code >= kSgrBrightBg && code < kSgrBrightBg + kSgrColours)
        mAnsiBg = static_cast<std::uint8_t>(code - kSgrBrightBg + kSgrColours);
}

void Decoder::onHeader(std::vector<Event>& out)
{
    const Header          header { mHeader };
    const CodeInfo* const code { findCode(header) };
    const Role            role { code != nullptr ? code->mRole : Role::kNone };
    const StackOp         op { code != nullptr ? code->mOp : StackOp::kPush };

    if (mReply.mKind == Reply::kFes && mReply.mIsAwaitingWeather)
        finishFes(out);

    if (! mIsInGameMode && ! isSuppressed() && (role == Role::kPromptCore || role == Role::kRoomName))
        enterGame(out);
    if (mIsInGameMode && role == Role::kAccountLogout)
        exitGame(true, out);

    if (mOptions.mWillEmitWireTier)
    {
        emit(CodeEvent { .mId     = code != nullptr ? code->mId : std::string_view {},
                         .mHeader = header,
                         .mOp     = op },
             out);
    }

    queueCodeEvents(header, code);
    if (! isSuppressed() && ! mIsCapturing)
        applyHeaderRole(role, code, out);

    // The current C1 colour, before this header changes it.
    Colour currentFg { mBaseFg };
    Colour currentBg { mBaseBg };
    if (! mStack.empty())
    {
        currentFg = mStack.back().mFg;
        currentBg = mStack.back().mBg;
    }

    if (op == StackOp::kCatch)
    {
        mSaved.push_back(SavedStack { .mFrames = std::move(mStack), .mBaseFg = mBaseFg, .mBaseBg = mBaseBg });
        mStack.clear();
        mBaseFg = currentFg;
        mBaseBg = currentBg;
        return;
    }
    if (op == StackOp::kThrow)
    {
        if (mSaved.empty())
            return;
        mStack  = std::move(mSaved.back().mFrames);
        mBaseFg = mSaved.back().mBaseFg;
        mBaseBg = mSaved.back().mBaseBg;
        mSaved.pop_back();
        return;
    }
    pushFrame(header, code, currentFg, currentBg);
}

void Decoder::applyHeaderRole(Role role, const CodeInfo* code, std::vector<Event>& out)
{
    switch (role)
    {
        case Role::kPersonaUnsaved:
            emit(PersonaUnsavedEvent {}, out);
            return;
        case Role::kDreamword:
            mIsCollectingDreamword = true;
            mDreamword.clear();
            return;
        case Role::kDreamwordCleared:
            emit(DreamwordEvent { .mWord = std::nullopt }, out);
            return;
        case Role::kFightStart:
            mLine.mLastKind = LineKind::kFightStart;
            return;
        case Role::kFightEnd:
            mLine.mLastKind = LineKind::kFightEnd;
            return;
        case Role::kCreatureInvisible:
            mLine.mLastKind = LineKind::kCreatureInvisible;
            return;
        case Role::kChat:
            mLine.mIsChat = true;
            if (code->mId == kTellId)
                mLine.mHasTell = true;
            return;
        case Role::kRoomName:
            if (! mLine.mIsRoomNameSeen)
            {
                mLine.mIsRoomNameSeen = true;
                mLine.mIsRoomName     = ! mLine.mHasPrintable;
            }
            return;
        default:
            return;
    }
}

void Decoder::pushFrame(const Header& header, const CodeInfo* code, Colour currentFg, Colour currentBg)
{
    const Role role { code != nullptr ? code->mRole : Role::kNone };
    Frame      frame { .mCode = code, .mHeader = header, .mFg = currentFg, .mBg = currentBg };
    if (code == nullptr)
    {
        // An unlisted header pushes white on black.
        frame.mFg = kWhite;
        frame.mBg = kBlack;
    }
    else if (code->mFg == kColourFromParams)
    {
        frame.mFg = toColour(header.mCount > 1 ? header.mNumbers[1] : kBlack, kWhite);
        frame.mBg = toColour(header.mCount > 2 ? header.mNumbers[2] : kBlack, kBlack);
    }
    else if (code->mFg == kNoColourChange)
    {
        frame.mFg = currentFg;
        frame.mBg = currentBg;
    }
    else
    {
        frame.mFg = static_cast<std::uint8_t>(code->mFg);
        frame.mBg = static_cast<std::uint8_t>(code->mBg);
        if (role == Role::kSnoopPrefix && header.mCount > 1)
            frame.mBg = (header.mNumbers[1] % 2U) != 0U ? kSnoopOddBg : kSnoopEvenBg;
    }

    if (! isSuppressed())
    {
        if (role == Role::kPromptContainer)
        {
            // A new outer 01 owns the prompt; an earlier one's capture restarts.
            for (Frame& open : mStack)
                open.mIsPromptOwner = false;
            frame.mIsPromptOwner = true;
            mIsCapturing         = true;
            mIsCaptureTentative  = ! mIsInGameMode;
            mCapture.clear();
        }
        else if (! mIsCapturing && mReadingRole == Role::kNone
                 && (role == Role::kStatStamina || role == Role::kStatMaxStamina || role == Role::kStatScore))
        {
            frame.mIsReading = true;
            mReadingRole     = role;
            mReading.clear();
        }
        else if (const Reply kind { getReplyKind(role) }; kind != Reply::kNone)
        {
            frame.mIsReplyRoot = true;
            startReply(kind, code);
        }
    }
    else if (mReply.mKind == Reply::kWho && role == Role::kWhoName && ! mReply.mIsNameOpen)
    {
        frame.mIsWhoName    = true;
        mReply.mIsNameOpen  = true;
        mReply.mIsNameEnded = false;
        mReply.mNameFg      = frame.mFg;
        mReply.mName.clear();
    }
    else if (mReply.mKind == Reply::kFes && ! mReply.mHasStaminaColour && code != nullptr
             && code->mFg == kColourFromParams && hasFrame(Role::kStatStamina))
    {
        mReply.mStaminaColour    = frame.mFg;
        mReply.mHasStaminaColour = true;
    }

    // A C1 frame sets the colour afresh, as the latest colour sent wins in Clio. Attributes go too,
    // so a bold left on before the frame cannot promote the frame's exact colour.
    resetAnsiStyle();
    mStack.push_back(frame);
}

void Decoder::onClose(std::vector<Event>& out)
{
    if (mReply.mKind == Reply::kFes && mReply.mIsAwaitingWeather)
        finishFes(out);

    if (mStack.empty())
    {
        if (mOptions.mWillEmitWireTier)
            emit(CloseEvent { .mIsPopped = false }, out);
        return;
    }

    const Frame frame { mStack.back() };
    mStack.pop_back();
    resetAnsiStyle();
    if (mOptions.mWillEmitWireTier)
        emit(CloseEvent { .mIsPopped = true }, out);

    if (frame.mIsReading)
        finishReading();
    if (frame.mIsWhoName)
        finishWhoName();
    if (frame.mIsReplyRoot)
        finishReply(out);
    if (frame.mIsPromptOwner && mIsCapturing)
        finishPrompt(mIsCaptureTentative, out);
}

void Decoder::finishReading()
{
    std::string digits;
    for (const char c : mReading)
    {
        if (c != kReadingGroupSeparator)
            digits.push_back(c);
    }
    bool isNumber { ! digits.empty() };
    for (const char c : digits)
    {
        if (c < '0' || c > '9')
        {
            isNumber = false;
            break;
        }
    }
    const types::OptInt value { isNumber ? parseInt(digits) : std::nullopt };

    mLine.mHasReadings = true;
    if (value.has_value())
    {
        if (mReadingRole == Role::kStatStamina)
            mLine.mStamina = value;
        else if (mReadingRole == Role::kStatMaxStamina)
            mLine.mMaxStamina = value;
        else
            mLine.mScore = value;
    }
    mReadingRole = Role::kNone;
    mReading.clear();
}

void Decoder::onText(std::uint8_t byte, std::vector<Event>& out)
{
    const char c { static_cast<char>(byte) };

    if (mIsCollectingPhrase)
    {
        auto* const spell { mDeferred.empty() ? nullptr : std::get_if<SpellPhraseEvent>(&mDeferred.back()) };
        if (isPrintableAscii(byte) && spell != nullptr)
            spell->mPhrase.push_back(c);
        else
            endPhrase();
    }
    if (mIsCollectingDreamword)
    {
        if (byte >= static_cast<std::uint8_t>(kAsciiLowerA) && byte <= static_cast<std::uint8_t>(kAsciiLowerZ)
            && mDreamword.size() < kMaxDreamword)
            mDreamword.push_back(c);
        else
            endDreamword(out);
    }

    if (isSuppressed())
    {
        onReplyChar(c);
        return;
    }

    const Style style { getStyle() };
    if (mIsCapturing)
    {
        appendRun(mCapture, c, style);
        return;
    }
    if (mReadingRole != Role::kNone)
        mReading.push_back(c);
    onDisplayedText(c, style, out);
}

void Decoder::onDisplayedText(char c, const Style& style, std::vector<Event>& out)
{
    appendRun(mPending, c, style);
    if (! mLine.mLabelEnd.has_value() && (c == kTellQuote || hasFrameWithId(kTellId)))
        mLine.mLabelEnd = mLine.mText.size();
    mLine.mText.push_back(c);
    mLine.mHasPrintable = true;
    if (hasFrame(Role::kRoomLong))
        mLine.mIsRoomLong = true;
    if (hasFrame(Role::kChat))
        mLine.mIsChat = true;

    if (mIsInGameMode && ! mLine.mHasPrompt && mLine.mText == kOptionMenu)
        exitGame(false, out);
}

void Decoder::onLineEnd(std::vector<Event>& out)
{
    // Every line end, displayed or not (a suppressed reply's, a hidden notice's), ends the live
    // partial's line (events.md `partial`, `continued`).
    mHasLineEndSincePartial = true;
    if (isSuppressed())
    {
        onReplyLineEnd(out);
        return;
    }

    if (mIsCapturing)
    {
        // A line end inside the prompt bracket abandons the capture; what it held is ordinary text.
        const std::vector<Run> captured { std::move(mCapture) };
        mCapture.clear();
        mIsCapturing = false;
        for (Frame& frame : mStack)
            frame.mIsPromptOwner = false;
        for (const Run& run : captured)
        {
            for (const char c : run.mText)
                onDisplayedText(c, run.mStyle, out);
        }
    }

    if (mIsInGameMode && isAllStars(mLine.mText))
    {
        mPending.clear();
        flushDeferred(out);
        resetLine();
        return;
    }
    if (! mIsInGameMode)
    {
        if (const types::OptInt columns { parseWidthNotice(mLine.mText) }; columns.has_value())
        {
            mPending.clear();
            flushDeferred(out);
            emit(WidthAckEvent { .mColumns = *columns }, out);
            resetLine();
            return;
        }
    }
    endDisplayedLine(out);
}

void Decoder::endDisplayedLine(std::vector<Event>& out)
{
    flushPending(out);
    flushDeferred(out);

    if (mIsInGameMode && mLine.mHasTell)
        emitTeller(out);
    if (mLine.mHasReadings)
    {
        CodeStatsEvent stats { .mStamina = mLine.mStamina };
        stats.mMaxStamina = mLine.mMaxStamina;
        stats.mScore      = mLine.mScore;
        emit(stats, out);
    }
    if (mLine.mIsRoomName)
        emit(RoomNameEvent { .mText = mLine.mText }, out);
    if (mLine.mIsRoomLong)
        emit(RoomLongEvent { .mText = mLine.mText }, out);

    const bool     isChat { mLine.mIsChat || hasFrame(Role::kChat) };
    const LineKind kind { isChat ? LineKind::kChat : mLine.mLastKind };
    emit(LineEndEvent { .mKind = kind, .mIsChatContinued = isChat && mLine.mWasChatOpenAtStart }, out);
    mIsGateOpen = true;
    resetLine();
}

void Decoder::emitTeller(std::vector<Event>& out)
{
    const std::string_view label { std::string_view { mLine.mText }.substr(
        0, mLine.mLabelEnd.value_or(std::string::npos)) };
    const std::string      lower { toLower(label) };
    const std::size_t      tells { lower.find(kTellsYou) };
    if (tells == std::string::npos || tells < 1 || label.starts_with(kYouTell))
        return;

    const std::size_t space { label.find_first_of(kWhitespaceSeparators) };
    std::string       name { label.substr(0, std::min(space, tells)) };
    if (name.empty() || toLower(name) == kSomeone)
        return;
    emit(TellerEvent { .mName = std::move(name) }, out);
}

void Decoder::resetLine()
{
    mLine                     = Line {};
    mLine.mWasChatOpenAtStart = hasFrame(Role::kChat);
}

void Decoder::onClearScreen(std::vector<Event>& out)
{
    flushPending(out);
    emit(ClearScreenEvent {}, out);
}

// -- Collectors --------------------------------------------------------------------------------

void Decoder::endPhrase()
{
    if (! mIsCollectingPhrase)
        return;
    mIsCollectingPhrase = false;
    if (mDeferred.empty())
        return;
    if (auto* const spell { std::get_if<SpellPhraseEvent>(&mDeferred.back()) }; spell != nullptr)
        spell->mPhrase = std::string { trim(spell->mPhrase) };
}

void Decoder::endDreamword(std::vector<Event>& out)
{
    if (! mIsCollectingDreamword)
        return;
    mIsCollectingDreamword = false;
    std::string word { std::move(mDreamword) };
    mDreamword.clear();
    if (! word.empty())
        emit(DreamwordEvent { .mWord = std::move(word) }, out);
}

void Decoder::queueCodeEvents(const Header& header, const CodeInfo* code)
{
    if (OptSoundParams params { findSoundParams(header) }; params.has_value())
        mDeferred.emplace_back(
            SoundCodeEvent { .mFamily = header.mNumbers[0], .mParams = std::move(*params) });

    if (code != nullptr && (code->mId == kResetSignalIdOne || code->mId == kResetSignalIdTwo))
        mDeferred.emplace_back(ResetSignalEvent { .mId = code->mId });

    if (const types::OptStringView spell { findSpellId(code) }; spell.has_value())
    {
        mDeferred.emplace_back(SpellPhraseEvent { .mId = *spell, .mPhrase = std::string {} });
        mIsCollectingPhrase = true;
    }
}

void Decoder::startReply(Reply kind, const CodeInfo* code)
{
    mReply       = ReplyState {};
    mReply.mKind = kind;
    mReply.mCode = code;
}

void Decoder::onReplyChar(char c)
{
    switch (mReply.mKind)
    {
        case Reply::kNone:
            return;
        case Reply::kFes:
            if (mReply.mIsFesDone)
                return;
            if (mReply.mIsAwaitingWeather)
            {
                if (isSpace(c))
                    return;
                mReply.mIsAwaitingWeather = false;
            }
            if (isSpace(c))
            {
                if (! mReply.mCurrent.empty())
                    mReply.mFields.push_back(std::move(mReply.mCurrent));
                mReply.mCurrent.clear();
                return;
            }
            mReply.mCurrent.push_back(c);
            return;
        case Reply::kExits:
        case Reply::kInventory:
        case Reply::kAccount:
            mReply.mCurrent.push_back(c);
            return;
        case Reply::kWho:
            if (mReply.mIsNameOpen && ! mReply.mIsNameEnded)
                mReply.mName.push_back(c);
            return;
    }
}

void Decoder::onReplyLineEnd(std::vector<Event>& out)
{
    switch (mReply.mKind)
    {
        case Reply::kNone:
            return;
        case Reply::kFes:
            // The FES line is not a flush point.
            if (mReply.mIsFesDone)
                return;
            if (! mReply.mCurrent.empty())
                mReply.mFields.push_back(std::move(mReply.mCurrent));
            mReply.mCurrent.clear();
            if (mReply.mIsAwaitingWeather || mReply.mFields.size() >= kFesFieldsWithWeather)
                finishFes(out);
            else if (mReply.mFields.size() == kFesFields)
                mReply.mIsAwaitingWeather = true;
            return;
        case Reply::kExits:
        case Reply::kInventory:
            mReply.mLines.push_back(std::move(mReply.mCurrent));
            mReply.mCurrent.clear();
            flushDeferred(out);
            return;
        case Reply::kWho:
            if (mReply.mIsNameOpen)
                mReply.mIsNameEnded = true;
            flushDeferred(out);
            return;
        case Reply::kAccount:
            // A line of a 95 block is not a flush point.
            onAccountLine(out);
            return;
    }
}

void Decoder::onAccountLine(std::vector<Event>& out)
{
    mReply.mLines.push_back(std::move(mReply.mCurrent));
    mReply.mCurrent.clear();
    const std::vector<std::string>& lines { mReply.mLines };
    const Role                      role { mReply.mCode != nullptr ? mReply.mCode->mRole : Role::kNone };
    if (role == Role::kClientMode && lines.size() == kClientModeLines)
    {
        ClientModeEvent event;
        event.mLicence  = trim(lines[0]);
        event.mMinLevel = parseInt(trim(lines[1])).value_or(0);
        event.mMaxLevel = parseInt(trim(lines[2])).value_or(0);
        event.mAccount  = trim(lines[3]);
        event.mPrivs    = parseInt(trim(lines[4])).value_or(kUnknownPrivilege);
        emit(std::move(event), out);
    }
    else if (role == Role::kAccountChange && lines.size() == kAccountChangeLines)
    {
        AccountChangeEvent event;
        event.mAccount = trim(lines[0]);
        event.mPrivs   = parseInt(trim(lines[1])).value_or(kUnknownPrivilege);
        emit(std::move(event), out);
    }
}

void Decoder::finishFes(std::vector<Event>& out)
{
    mReply.mIsFesDone         = true;
    mReply.mIsAwaitingWeather = false;
    if (! mReply.mCurrent.empty())
        mReply.mFields.push_back(std::move(mReply.mCurrent));
    mReply.mCurrent.clear();

    const std::vector<std::string>& fields { mReply.mFields };
    if (fields.size() < kFesFields)
        return;

    const auto number { [&fields](std::size_t index)
                        {
                            return parseInt(fields[index]);
                        } };
    const auto flag { [&fields](std::size_t index)
                      {
                          return parseFlag(fields[index]);
                      } };
    const std::array<types::OptInt, 10> numbers {
        number(kFesStamina),   number(kFesMaxStamina),   number(kFesStrength), number(kFesMaxStrength),
        number(kFesDexterity), number(kFesMaxDexterity), number(kFesMagic),    number(kFesMaxMagic),
        number(kFesScore),     number(kFesReset),
    };
    const std::array<types::OptBool, 4> flags {
        flag(kFesBlind), flag(kFesDeaf), flag(kFesCrippled), flag(kFesDumb)
    };
    for (const types::OptInt& numberValue : numbers)
    {
        if (! numberValue.has_value())
            return;
    }
    for (const types::OptBool& flagValue : flags)
    {
        if (! flagValue.has_value())
            return;
    }

    types::OptChar weather;
    if (fields.size() >= kFesFieldsWithWeather && fields[kFesWeather].size() == 1)
        weather = fields[kFesWeather].front();

    emit(FesStatsEvent { .mStamina       = *numbers[0],
                         .mMaxStamina    = *numbers[1],
                         .mStrength      = *numbers[2],
                         .mMaxStrength   = *numbers[3],
                         .mDexterity     = *numbers[4],
                         .mMaxDexterity  = *numbers[5],
                         .mMagic         = *numbers[6],
                         .mMaxMagic      = *numbers[7],
                         .mScore         = *numbers[8],
                         .mIsBlind       = *flags[0],
                         .mIsDeaf        = *flags[1],
                         .mIsCrippled    = *flags[2],
                         .mIsDumb        = *flags[3],
                         .mResetMinutes  = *numbers[9],
                         .mWeather       = weather,
                         .mStaminaColour = mReply.mStaminaColour },
         out);
}

void Decoder::finishReply(std::vector<Event>& out)
{
    switch (mReply.mKind)
    {
        case Reply::kNone:
        case Reply::kAccount:
            break;
        case Reply::kFes:
            if (! mReply.mIsFesDone)
                finishFes(out);
            break;
        case Reply::kExits:
            finishExits(out);
            break;
        case Reply::kInventory:
            finishInventory(out);
            break;
        case Reply::kWho:
        {
            finishWhoName();
            std::vector<Player> players { std::move(mReply.mPlayers) };
            emit(WhoEvent { .mPlayers = std::move(players) }, out);
            break;
        }
    }
    mReply = ReplyState {};
}

void Decoder::finishExits(std::vector<Event>& out)
{
    if (! mReply.mCurrent.empty())
        mReply.mLines.push_back(std::move(mReply.mCurrent));
    std::vector<std::string> exits;
    for (const std::string& line : mReply.mLines)
    {
        std::string_view rest { trim(line) };
        while (! rest.empty())
        {
            const std::size_t end { std::min(rest.find(' '), rest.find('\t')) };
            exits.emplace_back(rest.substr(0, end));
            rest = trim(rest.substr(std::min(end, rest.size())));
        }
    }
    emit(ExitsEvent { .mExits = std::move(exits) }, out);
}

void Decoder::finishInventory(std::vector<Event>& out)
{
    if (! mReply.mCurrent.empty())
        mReply.mLines.push_back(std::move(mReply.mCurrent));
    InventoryEvent inventory;
    bool           isCarried {};
    for (const std::string& line : mReply.mLines)
    {
        const std::string_view entry { trim(line) };
        if (entry.empty())
            continue;
        if (! isCarried && entry == kInventorySplit)
        {
            isCarried = true;
            continue;
        }
        if (isCarried)
            inventory.mCarried.emplace_back(entry);
        else
            inventory.mRoom.emplace_back(entry);
    }
    if (inventory.mRoom.size() == 1 && inventory.mRoom.front() == kRoomBlind)
    {
        inventory.mRoomState = RoomState::kBlind;
        inventory.mRoom.clear();
    }
    else if (inventory.mRoom.size() == 1 && inventory.mRoom.front() == kRoomDark)
    {
        inventory.mRoomState = RoomState::kDark;
        inventory.mRoom.clear();
    }
    emit(std::move(inventory), out);
}

void Decoder::finishWhoName()
{
    if (! mReply.mIsNameOpen)
        return;
    mReply.mPlayers.push_back(Player { .mName = std::string { trim(mReply.mName) }, .mFg = mReply.mNameFg });
    mReply.mIsNameOpen = false;
    mReply.mName.clear();
}

void Decoder::finishPrompt(bool isTentative, std::vector<Event>& out)
{
    std::vector<Run> runs { std::move(mCapture) };
    mCapture.clear();
    mIsCapturing = false;

    if (isTentative && ! mIsInGameMode)
    {
        // An outer 01 outside game mode whose core never entered it: ordinary text.
        for (const Run& run : runs)
        {
            for (const char c : run.mText)
                onDisplayedText(c, run.mStyle, out);
        }
        return;
    }
    if (! mIsGateOpen)
        return;

    emit(PromptEvent { .mRuns = std::move(runs) }, out);
    mIsGateOpen      = false;
    mLine.mHasPrompt = true;
}

// -- Emission ----------------------------------------------------------------------------------

void Decoder::emit(Payload payload, std::vector<Event>& out)
{
    // In game mode text goes out as it arrives, so it is written before any later event. Before
    // game mode it waits for its line end or for end-of-read (events.md "partial").
    if (mIsInGameMode)
        flushPending(out);
    emitRaw(std::move(payload), out);
}

void Decoder::emitRaw(Payload payload, std::vector<Event>& out)
{
    out.push_back(Event { .mSeq = mSeq++, .mPayload = std::move(payload) });
}

void Decoder::flushPending(std::vector<Event>& out)
{
    std::vector<Run> runs { std::move(mPending) };
    mPending.clear();
    for (Run& run : runs)
        emitRaw(TextEvent { .mText = std::move(run.mText), .mStyle = run.mStyle }, out);
}

void Decoder::flushDeferred(std::vector<Event>& out)
{
    endPhrase();
    std::vector<Payload> deferred { std::move(mDeferred) };
    mDeferred.clear();
    for (Payload& payload : deferred)
        emit(std::move(payload), out);
}

void Decoder::enterGame(std::vector<Event>& out)
{
    emitRaw(GameEnterEvent {}, out);
    mIsInGameMode = true;
}

void Decoder::exitGame(bool willFlushPending, std::vector<Event>& out)
{
    if (willFlushPending)
        flushPending(out);
    mDeferred.clear();
    mIsCollectingPhrase = false;
    mStack.clear();
    mSaved.clear();
    resetAnsiStyle();
    mIsEscReverse = false;
    mBaseFg.reset();
    mBaseBg.reset();
    mIsCapturing        = false;
    mIsCaptureTentative = false;
    mCapture.clear();
    mReply       = ReplyState {};
    mReadingRole = Role::kNone;
    mReading.clear();
    mIsInGameMode = false;
    emitRaw(GameExitEvent {}, out);
}

// -- The context stack -------------------------------------------------------------------------

Style Decoder::getStyle() const noexcept
{
    Style style { .mFg = mBaseFg, .mBg = mBaseBg, .mAttrs = mAnsiAttrs };
    if (! mStack.empty())
    {
        style.mFg = mStack.back().mFg;
        style.mBg = mStack.back().mBg;
    }
    if (mAnsiFg.has_value())
        style.mFg = mAnsiFg;
    if (mAnsiBg.has_value())
        style.mBg = mAnsiBg;
    if (mIsEscReverse)
        style.mAttrs |= static_cast<std::uint8_t>(Attr::kReverse);
    return style;
}

bool Decoder::hasFrame(Role role) const noexcept
{
    for (const Frame& frame : mStack)
    {
        if (frame.mCode != nullptr && frame.mCode->mRole == role)
            return true;
    }
    return false;
}

bool Decoder::hasFrameWithId(std::string_view id) const noexcept
{
    for (const Frame& frame : mStack)
    {
        if (frame.mCode != nullptr && frame.mCode->mId == id)
            return true;
    }
    return false;
}

Decoder::Reply Decoder::getReplyKind(Role role) noexcept
{
    switch (role)
    {
        case Role::kFesLine:
            return Reply::kFes;
        case Role::kFeExits:
            return Reply::kExits;
        case Role::kFeInventory:
            return Reply::kInventory;
        case Role::kFeWho:
            return Reply::kWho;
        case Role::kClientMode:
        case Role::kAccountChange:
        case Role::kAccountLogout:
            return Reply::kAccount;
        default:
            return Reply::kNone;
    }
}
} // namespace muckaii::mud
