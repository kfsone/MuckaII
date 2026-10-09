#include "muckaii/base/types.hpp"
#include "muckaii/base/wire.hpp"
#include "muckaii/mud/codes.hpp"
#include "muckaii/mud/decoder.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace muckaii::mud;
namespace types = muckaii::types;

namespace
{
// kHeaderOpen and kHeaderClose delimit the readable test notation for C1 headers.
constexpr std::string_view kHeaderOpen { "<" };
constexpr std::string_view kHeaderClose { ">" };
// kCloseNotation represents one C1 close marker in readable test notation.
constexpr std::string_view kCloseNotation { "</>" };
// kReadableHeaderOffset and kReadableHeaderStride locate two-digit numbers in readable notation.
constexpr std::size_t kReadableHeaderOffset { 1 };
constexpr std::size_t kReadableHeaderStride { 3 };
// kDecimalBase converts pairs of decimal digits into their numeric value.
constexpr int kDecimalBase { 10 };
// kC1HeaderFirst is the wire byte for the first C1 header number, zero.
constexpr std::uint8_t kC1HeaderFirst { 0x9B };
// kTelnetTerminalTypeOption and kTelnetBinaryOption identify test negotiation options.
constexpr std::uint8_t kTelnetTerminalTypeOption { 0x18 };
constexpr std::uint8_t kTelnetBinaryOption { 0x01 };
// kOversizedSubnegotiationFill exceeds the decoder's subnegotiation ceiling.
constexpr std::size_t kOversizedSubnegotiationFill { 2000 };
// kTestAsciiZero decodes each digit in a readable C1 header number.
constexpr char kTestAsciiZero { '0' };
// kUnknownCodeId is the test sentinel for a header with no table match.
constexpr std::string_view kUnknownCodeId { "<none>" };

// makeBytes converts protocol byte values to the test's byte container.
template <typename... TBytes> types::Bytes makeBytes(TBytes... values)
{
    const std::array<std::uint8_t, sizeof...(values)> byteValues { static_cast<std::uint8_t>(values)... };
    types::Bytes                                      bytes;
    for (const std::uint8_t value : byteValues)
        bytes.push_back(std::byte { value });
    return bytes;
}

// makeTelnetSubnegotiationBytes builds one complete terminal-type subnegotiation fixture.
types::Bytes makeTelnetSubnegotiationBytes()
{
    return makeBytes(muckaii::wire::kTelnetIac,
                     muckaii::wire::kTelnetSb,
                     kTelnetTerminalTypeOption,
                     kTelnetBinaryOption,
                     muckaii::wire::kTelnetIac,
                     muckaii::wire::kTelnetSe);
}

// makeIacIacBytes preserves the test header and close while telnet doubles IAC bytes.
types::Bytes makeIacIacBytes()
{
    return makeBytes(kC1HeaderFirst + 1U,
                     muckaii::wire::kTelnetIac,
                     muckaii::wire::kTelnetIac,
                     muckaii::wire::kTelnetIac,
                     muckaii::wire::kTelnetIac);
}

// makeTelnetHeaderBytes interleaves a telnet negotiation command with a C1 header.
types::Bytes makeTelnetHeaderBytes()
{
    return makeBytes(kC1HeaderFirst + 1U,
                     muckaii::wire::kTelnetIac,
                     muckaii::wire::kTelnetWill,
                     kTelnetBinaryOption,
                     kC1HeaderFirst + 2U,
                     muckaii::wire::kTelnetIac,
                     muckaii::wire::kTelnetIac);
}

// makeUnexpectedSubnegotiationBytes ends an open subnegotiation with a new telnet command.
types::Bytes makeUnexpectedSubnegotiationBytes()
{
    return makeBytes(muckaii::wire::kTelnetIac,
                     muckaii::wire::kTelnetSb,
                     kTelnetTerminalTypeOption,
                     kTelnetBinaryOption,
                     muckaii::wire::kTelnetIac,
                     muckaii::wire::kTelnetDo,
                     kTelnetBinaryOption);
}

// parseHeaderNumber reads one two-digit decimal number from readable C1 notation.
int parseHeaderNumber(std::string_view text, std::size_t position)
{
    return ((text[position] - kTestAsciiZero) * kDecimalBase) + (text[position + 1] - kTestAsciiZero);
}

// makeHeader builds a C1 header from its ordered number list.
Header makeHeader(std::initializer_list<std::uint8_t> numbers)
{
    Header header;
    for (const std::uint8_t number : numbers)
        header.mNumbers[header.mCount++] = number;
    return header;
}

// findId returns the table id selected by a header number list.
std::string_view findId(std::initializer_list<std::uint8_t> numbers)
{
    const CodeInfo* code { findCode(makeHeader(numbers)) };
    return code != nullptr ? code->mId : kUnknownCodeId;
}

// Wire bytes from a readable form: <n n> is a header, </> a lone 255, everything else is itself.
types::Bytes wire(std::string_view text)
{
    types::Bytes bytes;
    for (std::size_t i { 0 }; i < text.size(); ++i)
    {
        if (text.substr(i).starts_with(kCloseNotation))
        {
            bytes.insert(
                bytes.end(),
                { std::byte { muckaii::wire::kTelnetIac }, std::byte { muckaii::wire::kTelnetIac } });
            i += kCloseNotation.size() - 1U;
            continue;
        }
        if (text.substr(i).starts_with(kHeaderOpen))
        {
            const std::size_t end { text.find(kHeaderClose.front(), i) };
            for (std::size_t pos { i + kReadableHeaderOffset }; pos < end; pos += kReadableHeaderStride)
            {
                const int number { parseHeaderNumber(text, pos) };
                bytes.push_back(std::byte { static_cast<std::uint8_t>(kC1HeaderFirst + number) });
            }
            bytes.insert(
                bytes.end(),
                { std::byte { muckaii::wire::kTelnetIac }, std::byte { muckaii::wire::kTelnetIac } });
            i = end;
            continue;
        }
        bytes.push_back(std::byte { static_cast<std::uint8_t>(text[i]) });
    }
    return bytes;
}

// decode feeds readable test notation through a decoder and returns its payloads.
std::vector<Payload> decode(Decoder& decoder, std::string_view text)
{
    std::vector<Event> events;
    decoder.feed(wire(text), events);
    decoder.endOfRead(events);
    std::vector<Payload> payloads;
    for (Event& event : events)
        payloads.push_back(std::move(event.mPayload));
    return payloads;
}

// primedDecoder returns a decoder in game mode with its prompt gate established.
Decoder primedDecoder()
{
    Decoder decoder;
    decode(decoder, "<01 02></><01><01 02>*</></>");
    return decoder;
}
} // namespace

// CodeMatching checks an exact code id takes precedence over other matching forms.
TEST(CodeMatching, ExactIdWins)
{
    EXPECT_EQ(findId({ 8, 1 }), "08 01");
    EXPECT_EQ(findId({ 99, 99 }), "99 99");
}

// CodeMatching checks a parametric code id ignores its remaining header numbers.
TEST(CodeMatching, ParametricEntryTakesRemainingNumbers)
{
    EXPECT_EQ(findId({ 99, 10 }), "99");
    EXPECT_EQ(findId({ 98, 3 }), "98");
}

// CodeMatching checks trailing zero numbers are removed when resolving an id.
TEST(CodeMatching, TrailingZerosAreRemoved)
{
    EXPECT_EQ(findId({ 8, 0 }), "08");
    EXPECT_EQ(findId({ 7, 2, 0 }), "07 02");
    EXPECT_EQ(findId({ 11, 0 }), "11");
}

// CodeMatching checks missing trailing zero numbers are appended when resolving an id.
TEST(CodeMatching, ZerosAreAppended)
{
    EXPECT_EQ(findId({ 20 }), "20 00");
    EXPECT_EQ(findId({ 14 }), "14 00");
}

// CodeMatching checks an unknown header does not resolve to a code id.
TEST(CodeMatching, UnknownHeaderMatchesNothing)
{
    EXPECT_EQ(findId({ 50, 1 }), "<none>");
}

// Decoder checks pre-game text is emitted as a partial at the read boundary.
TEST(Decoder, PreGameTextIsAPartialAtEndOfRead)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder, "Account ID: ") };
    ASSERT_EQ(events.size(), 1U);
    const auto* partial { std::get_if<PartialEvent>(&events[0]) };
    ASSERT_NE(partial, nullptr);
    ASSERT_EQ(partial->mRuns.size(), 1U);
    EXPECT_EQ(partial->mRuns[0].mText, "Account ID: ");
    EXPECT_FALSE(partial->mRuns[0].mStyle.mFg.has_value());
}

// A prompt split across two reads: the second partial continues the first.
// Decoder checks a partial without an intervening line end continues the prior partial.
TEST(Decoder, APartialWithNoLineEndBetweenContinuesTheLast)
{
    Decoder                    decoder;
    const std::vector<Payload> first { decode(decoder, "Account ") };
    const std::vector<Payload> second { decode(decoder, "ID: ") };
    ASSERT_EQ(first.size(), 1U);
    ASSERT_EQ(second.size(), 1U);
    EXPECT_FALSE(std::get<PartialEvent>(first[0]).mIsContinued);
    EXPECT_TRUE(std::get<PartialEvent>(second[0]).mIsContinued);
}

// An LF inside a CSI (Clio's workaround keeps the CSI going) ends no line, so the next partial
// still continues the last.
// Decoder checks an LF inside CSI does not end the line.
TEST(Decoder, AnLfInsideACsiEndsNoLine)
{
    Decoder                    decoder;
    const std::vector<Payload> first { decode(decoder, "Account ") };
    const std::vector<Payload> second { decode(decoder, "\x1b[31\r\nmID: ") };
    ASSERT_EQ(second.size(), 1U);
    EXPECT_TRUE(std::get<PartialEvent>(second[0]).mIsContinued);
}

// The Option menu, client-mode entry, and the server's reply: a 95 block, the hidden width notice,
// and the Option prompt again (the shape of a live login). A line end came between the two prompts,
// though no displayed one, so the second replaces the first rather than joining it on one line.
// Decoder checks a prompt after a hidden line replaces the prior partial.
TEST(Decoder, ThePromptAfterAHiddenLineReplacesTheLast)
{
    Decoder                    decoder;
    const std::vector<Payload> menu { decode(decoder, "MUD login menu.\r\nOption (H for help): ") };
    ASSERT_FALSE(menu.empty());
    const std::vector<Payload> reply { decode(
        decoder,
        "\x1b[1;37;40m<00><95>Unlicensed product!\r\n1\r\n1\r\nZ0000000\r\n1\r\n</>[New terminal width is "
        "80]\r\nOption (H for help): ") };
    ASSERT_FALSE(reply.empty());
    const auto* partial { std::get_if<PartialEvent>(&reply.back()) };
    ASSERT_NE(partial, nullptr);
    EXPECT_FALSE(partial->mIsContinued);
    ASSERT_EQ(partial->mRuns.size(), 1U);
    EXPECT_EQ(partial->mRuns[0].mText, "Option (H for help): ");
    for (const Payload& event : reply)
        EXPECT_FALSE(std::holds_alternative<LineEndEvent>(event));
}

// Decoder checks a completed pre-game line emits text before its line end.
TEST(Decoder, PreGameLineIsTextThenLineEnd)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder, "Welcome\r\n") };
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(std::get<TextEvent>(events[0]).mText, "Welcome");
    EXPECT_TRUE(std::holds_alternative<LineEndEvent>(events[1]));
}

// Decoder checks a complete telnet command is emitted as one event.
TEST(Decoder, TelnetCommandsAreEvents)
{
    Decoder            decoder;
    std::vector<Event> events;
    const types::Bytes bytes { makeBytes(
        muckaii::wire::kTelnetIac, muckaii::wire::kTelnetDo, kTelnetTerminalTypeOption) };
    decoder.feed(bytes, events);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(std::get<TelnetEvent>(events[0].mPayload).mBytes, bytes);
}

// Decoder checks a complete telnet subnegotiation is emitted as one event.
TEST(Decoder, TelnetSubnegotiationIsOneEvent)
{
    Decoder            decoder;
    std::vector<Event> events;
    const types::Bytes bytes { makeTelnetSubnegotiationBytes() };
    decoder.feed(bytes, events);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(std::get<TelnetEvent>(events[0].mPayload).mBytes, bytes);
}

// Decoder checks a room-name code enters game mode and identifies its text.
TEST(Decoder, RoomNameEntersGameMode)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder, "<20 01><02 01>Tea room</>.\r\n</>") };
    ASSERT_GE(events.size(), 2U);
    EXPECT_TRUE(std::holds_alternative<GameEnterEvent>(events[0]));
    EXPECT_TRUE(decoder.isInGameMode());
    bool hasRoomName {};
    for (const Payload& event : events)
    {
        if (const auto* room { std::get_if<RoomNameEvent>(&event) }; room != nullptr)
        {
            EXPECT_EQ(room->mText, "Tea room.");
            hasRoomName = true;
        }
    }
    EXPECT_TRUE(hasRoomName);
}

// Decoder checks the first game-mode prompt is emitted with its captured text and style.
TEST(Decoder, EnteringPromptIsShownAsAPrompt)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder, "<01><01 02>*</></>") };
    ASSERT_EQ(events.size(), 2U);
    EXPECT_TRUE(std::holds_alternative<GameEnterEvent>(events[0]));
    const auto& prompt { std::get<PromptEvent>(events[1]) };
    ASSERT_EQ(prompt.mRuns.size(), 1U);
    EXPECT_EQ(prompt.mRuns[0].mText, "*");
    EXPECT_EQ(prompt.mRuns[0].mStyle.mFg, types::OptUint8 { 12 });
}

// Decoder checks the prompt gate discards a repeated prompt without an intervening line.
TEST(Decoder, PromptGateDiscardsAPromptWithNoLineBetween)
{
    Decoder                    decoder { primedDecoder() };
    const std::vector<Payload> events { decode(decoder, "<01><01 02>*</></>") };
    EXPECT_TRUE(events.empty());
}

// Decoder checks the option menu ends game mode and is emitted as a partial.
TEST(Decoder, OptionMenuTextLeavesGameMode)
{
    Decoder                    decoder { primedDecoder() };
    const std::vector<Payload> events { decode(decoder, "Cheerio!\r\nOption (H for help): ") };
    EXPECT_FALSE(decoder.isInGameMode());
    ASSERT_EQ(events.size(), 4U);
    EXPECT_TRUE(std::holds_alternative<GameExitEvent>(events[2]));
    const auto& partial { std::get<PartialEvent>(events[3]) };
    ASSERT_EQ(partial.mRuns.size(), 1U);
    EXPECT_EQ(partial.mRuns[0].mText, "Option (H for help): ");
}

// Decoder checks an all-star line is suppressed in game mode.
TEST(Decoder, AllStarLinesAreNotDisplayedInGame)
{
    Decoder                    decoder { primedDecoder() };
    const std::vector<Payload> events { decode(decoder, "***\r\n") };
    EXPECT_TRUE(events.empty());
}

// Decoder checks a width escape emits its width acknowledgement and hides the notice.
TEST(Decoder, WidthNoticeIsSwallowedAfterWidthEscape)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder, "\x1b-80W[New terminal width is 80]\r\n") };
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(std::get<WidthAckEvent>(events[0]).mColumns, 80);
}

// Decoder checks a pre-game width notice is emitted as a width acknowledgement.
TEST(Decoder, PreGameWidthNoticeLineIsAWidthAck)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder, "[New terminal width is 20]\r\n") };
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(std::get<WidthAckEvent>(events[0]).mColumns, 20);
}

// Decoder checks a client-mode block emits its fields without displaying its contents.
TEST(Decoder, ClientModeBlockIsReadNotShown)
{
    Decoder                    decoder;
    const std::string_view     input { "<95>Unlicensed product!\r\n1\r\n4\r\nZ0000000\r\n1\r\n</>" };
    const std::vector<Payload> events { decode(decoder, input) };
    ASSERT_EQ(events.size(), 1U);
    const auto& mode { std::get<ClientModeEvent>(events[0]) };
    EXPECT_EQ(mode.mLicence, "Unlicensed product!");
    EXPECT_EQ(mode.mMinLevel, 1);
    EXPECT_EQ(mode.mMaxLevel, 4);
    EXPECT_EQ(mode.mAccount, "Z0000000");
    EXPECT_EQ(mode.mPrivs, 1);
}

// Decoder checks pending text precedes a clear-screen event.
TEST(Decoder, ClearScreenWritesPendingTextFirst)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder, "abc\x1b-C") };
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(std::get<TextEvent>(events[0]).mText, "abc");
    EXPECT_TRUE(std::holds_alternative<ClearScreenEvent>(events[1]));
}

// Decoder checks SGR colour and bold attributes are applied to displayed text.
TEST(Decoder, SgrSetsColourAndBold)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder, "\x1b[1;31mred\x1b[0m\r\n") };
    ASSERT_GE(events.size(), 1U);
    const auto& text { std::get<TextEvent>(events[0]) };
    EXPECT_EQ(text.mText, "red");
    EXPECT_EQ(text.mStyle.mFg, types::OptUint8 { 1 });
    EXPECT_TRUE(text.mStyle.hasAttr(Attr::kBold));
}

// Decoder checks a C1 frame colour replaces an earlier SGR colour.
TEST(Decoder, AFrameColourReplacesAnEarlierSgrColour)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder, "\x1b[1;37;40m<02 02>room</>\r\n") };
    ASSERT_GE(events.size(), 1U);
    const auto& text { std::get<TextEvent>(events[0]) };
    EXPECT_EQ(text.mText, "room");
    EXPECT_EQ(text.mStyle.mFg, types::OptUint8 { 2 });
}

// Decoder checks a C1 frame replaces both SGR colours and its close clears them.
TEST(Decoder, AFrameReplacesBothSgrColoursAndItsCloseDropsThem)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder, "\x1b[31;46m<02 02>inside</>after\r\n") };
    ASSERT_GE(events.size(), 2U);
    const auto& inside { std::get<TextEvent>(events[0]) };
    const auto& after { std::get<TextEvent>(events[1]) };
    EXPECT_EQ(inside.mText, "inside");
    EXPECT_EQ(inside.mStyle.mFg, types::OptUint8 { 2 });
    EXPECT_EQ(inside.mStyle.mBg, types::OptUint8 { 0 });
    EXPECT_EQ(after.mText, "after");
    EXPECT_FALSE(after.mStyle.mFg.has_value());
    EXPECT_FALSE(after.mStyle.mBg.has_value());
}

// Decoder checks an SGR colour overrides its frame colour until the frame closes.
TEST(Decoder, ASgrColourInsideAFrameWinsUntilTheFrameCloses)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder, "<02 02>frame\x1b[31;46mansi</>after\r\n") };
    ASSERT_GE(events.size(), 3U);
    const auto& frame { std::get<TextEvent>(events[0]) };
    const auto& ansi { std::get<TextEvent>(events[1]) };
    const auto& after { std::get<TextEvent>(events[2]) };
    EXPECT_EQ(frame.mStyle.mFg, types::OptUint8 { 2 });
    EXPECT_EQ(ansi.mStyle.mFg, types::OptUint8 { 1 });
    EXPECT_EQ(ansi.mStyle.mBg, types::OptUint8 { 6 });
    EXPECT_FALSE(after.mStyle.mFg.has_value());
    EXPECT_FALSE(after.mStyle.mBg.has_value());
}

// Decoder checks closing a nested frame restores its parent colour.
TEST(Decoder, NestedFramesResumeTheirParentColour)
{
    Decoder                    decoder;
    const std::string_view     input { "\x1b[31;46m<02 02>outer<03 01>inner</>again</>after\r\n" };
    const std::vector<Payload> events { decode(decoder, input) };
    ASSERT_GE(events.size(), 4U);
    const std::array<std::pair<std::uint8_t, std::uint8_t>, 3> expected { { { 2, 0 }, { 6, 0 }, { 2, 0 } } };
    for (std::size_t i { 0 }; i < expected.size(); ++i)
    {
        const auto& text { std::get<TextEvent>(events[i]) };
        EXPECT_EQ(text.mStyle.mFg, types::OptUint8 { expected[i].first }) << i;
        EXPECT_EQ(text.mStyle.mBg, types::OptUint8 { expected[i].second }) << i;
    }
    EXPECT_FALSE(std::get<TextEvent>(events[3]).mStyle.mFg.has_value());
}

// Decoder checks an SGR reset remains valid when its sequence crosses a line end.
TEST(Decoder, SgrResetIsAcceptedAcrossALineEnd)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder, "\x1b[31m\x1b[0;37;40\r\nmx\r\n") };
    ASSERT_EQ(events.size(), 2U);
    const auto& text { std::get<TextEvent>(events[0]) };
    EXPECT_EQ(text.mText, "x");
    EXPECT_EQ(text.mStyle.mFg, types::OptUint8 { 7 });
}

// Decoder checks the numbers after SGR 38 or 48 are not read as attributes.
TEST(Decoder, ExtendedColourParametersAreNotAttributes)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder, "\x1b[38;5;1;48;2;7;7;7mx\r\n") };
    ASSERT_FALSE(events.empty());
    const auto& text { std::get<TextEvent>(events[0]) };
    EXPECT_EQ(text.mStyle.mAttrs, 0U);
}

// Decoder checks a private-prefixed sequence ending in m does not change the style.
TEST(Decoder, PrivateSequenceEndingInMIsNotSgr)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder, "\x1b[>0;1mx\r\n") };
    ASSERT_FALSE(events.empty());
    const auto& text { std::get<TextEvent>(events[0]) };
    EXPECT_EQ(text.mStyle.mAttrs, 0U);
}

// Decoder checks a C1 frame clears a bold set before it.
TEST(Decoder, AFrameClearsEarlierSgrAttributes)
{
    Decoder                    decoder { primedDecoder() };
    const std::vector<Payload> events { decode(decoder, "\x1b[1m<02 02>x</>\r\n") };
    bool                       isChecked {};
    for (const Payload& event : events)
    {
        if (const auto* text { std::get_if<TextEvent>(&event) })
        {
            EXPECT_FALSE(text->mStyle.hasAttr(Attr::kBold));
            isChecked = true;
        }
    }
    EXPECT_TRUE(isChecked);
}

// Decoder checks a dreamword code collects and emits its following word.

TEST(Decoder, DreamwordIsReadAfterItsCode)
{
    Decoder decoder { primedDecoder() };
    bool    hasWord {};
    for (const Payload& event : decode(decoder, "<15 00 00>plugh</>\r\n"))
    {
        if (const auto* word { std::get_if<DreamwordEvent>(&event) }; word != nullptr)
        {
            EXPECT_EQ(word->mWord, types::OptString { "plugh" });
            hasWord = true;
        }
    }
    EXPECT_TRUE(hasWord);
}

// Decoder checks a tell line emits the sender name.
TEST(Decoder, TellerNamesTheSender)
{
    Decoder decoder { primedDecoder() };
    bool    hasTeller {};
    for (const Payload& event : decode(decoder, "<09>Ann tells you \"<09 03>hello</>\".</>\r\n"))
    {
        if (const auto* teller { std::get_if<TellerEvent>(&event) }; teller != nullptr)
        {
            EXPECT_EQ(teller->mName, "Ann");
            hasTeller = true;
        }
    }
    EXPECT_TRUE(hasTeller);
}

// Decoder checks an account-logout code exits game mode.
TEST(Decoder, AccountLogoutLeavesGameMode)
{
    Decoder                    decoder { primedDecoder() };
    const std::vector<Payload> events { decode(decoder, "<95 03>Z0000000\r\n</>") };
    ASSERT_EQ(events.size(), 1U);
    EXPECT_TRUE(std::holds_alternative<GameExitEvent>(events[0]));
    EXPECT_FALSE(decoder.isInGameMode());
}

// Decoder checks a throw restores the context stack saved by a catch.
TEST(Decoder, CatchAndThrowRestoreTheStack)
{
    Decoder                    decoder { primedDecoder() };
    const std::vector<Payload> events { decode(decoder, "<05 01 06><90>a<99 01>b<90 01>c</>\r\n") };
    ASSERT_GE(events.size(), 3U);
    EXPECT_EQ(std::get<TextEvent>(events[0]).mText, "a");
    EXPECT_EQ(std::get<TextEvent>(events[0]).mStyle.mFg, types::OptUint8 { 9 });
    EXPECT_EQ(std::get<TextEvent>(events[1]).mStyle.mFg, types::OptUint8 { 1 });
    EXPECT_EQ(std::get<TextEvent>(events[2]).mText, "c");
    EXPECT_EQ(std::get<TextEvent>(events[2]).mStyle.mFg, types::OptUint8 { 9 });
}

// Decoder checks nested throws restore one saved context stack at a time.
TEST(Decoder, NestedCatchesThrowBackOneAtATime)
{
    Decoder                       decoder { primedDecoder() };
    std::vector<const TextEvent*> texts;
    const std::vector<Payload>    events { decode(
        decoder, "<05 01 06>a<90><99 01>b<90><05 01 06>c<90 01>d<90 01>e</>\r\n") };
    for (const Payload& event : events)
    {
        if (const auto* text { std::get_if<TextEvent>(&event) }; text != nullptr)
            texts.push_back(text);
    }
    ASSERT_EQ(texts.size(), 5U);
    const std::array<std::pair<std::string_view, std::uint8_t>, 5> kExpected { {
        { "a", 9 },
        { "b", 1 },
        { "c", 9 },
        { "d", 1 },
        { "e", 9 },
    } };
    for (std::size_t i { 0 }; i < kExpected.size(); ++i)
    {
        EXPECT_EQ(texts[i]->mText, kExpected[i].first) << i;
        EXPECT_EQ(texts[i]->mStyle.mFg, types::OptUint8 { kExpected[i].second }) << i;
    }
}

// FF FF is telnet's IAC IAC: one data byte 255, which ends a header and, alone, closes a code.
// Decoder checks telnet IAC IAC is passed to the MUD2 parser as data byte 255.
TEST(Decoder, IacIacIsTheDataByte255)
{
    Decoder            decoder { DecoderOptions { .mWillEmitWireTier = true } };
    std::vector<Event> events;
    const types::Bytes bytes { makeIacIacBytes() };
    decoder.feed(bytes, events);
    bool hasCode {};
    bool hasClose {};
    for (const Event& event : events)
    {
        EXPECT_FALSE(std::holds_alternative<TelnetEvent>(event.mPayload));
        if (const auto* code { std::get_if<CodeEvent>(&event.mPayload) }; code != nullptr)
        {
            EXPECT_EQ(code->mHeader.mCount, 1U);
            EXPECT_EQ(code->mHeader.mNumbers[0], 1);
            hasCode = true;
        }
        hasClose = hasClose || std::holds_alternative<CloseEvent>(event.mPayload);
    }
    EXPECT_TRUE(hasCode);
    EXPECT_TRUE(hasClose);
}

// A telnet command inside a C1 header is handled as telnet; the header reads on around it
// (protocol.md "Telnet residue in the stream").
// Decoder checks telnet negotiation inside a C1 header leaves that header intact.
TEST(Decoder, ATelnetCommandInsideAHeaderLeavesTheHeaderWhole)
{
    Decoder            decoder { DecoderOptions { .mWillEmitWireTier = true } };
    std::vector<Event> events;
    const types::Bytes bytes { makeTelnetHeaderBytes() };
    decoder.feed(bytes, events);
    types::Bytes     telnet;
    std::string_view codeId { kUnknownCodeId };
    for (const Event& event : events)
    {
        if (const auto* command { std::get_if<TelnetEvent>(&event.mPayload) }; command != nullptr)
            telnet = command->mBytes;
        if (const auto* code { std::get_if<CodeEvent>(&event.mPayload) }; code != nullptr)
            codeId = code->mId;
    }
    EXPECT_EQ(telnet, makeBytes(muckaii::wire::kTelnetIac, muckaii::wire::kTelnetWill, kTelnetBinaryOption));
    EXPECT_EQ(codeId, "01 02");
}

// The decoder's rule: IAC and anything but IAC or SE inside a subnegotiation abandons it, and the
// IAC and that byte are a command of their own.
// Decoder checks an unexpected telnet command abandons the open subnegotiation.
TEST(Decoder, AnUnexpectedCommandInsideASubnegotiationAbandonsIt)
{
    Decoder            decoder;
    std::vector<Event> events;
    const types::Bytes bytes { makeUnexpectedSubnegotiationBytes() };
    decoder.feed(bytes, events);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(std::get<TelnetEvent>(events[0].mPayload).mBytes,
              makeBytes(muckaii::wire::kTelnetIac, muckaii::wire::kTelnetDo, kTelnetBinaryOption));
}

// An SB that never ends is abandoned at its ceiling (connection.md 2.1, 1024 bytes) and decoding
// resumes, so the text after it is shown.
// Decoder checks an oversized unterminated subnegotiation is abandoned and decoding resumes.
TEST(Decoder, AnUnterminatedSubnegotiationIsAbandonedAtItsCeiling)
{
    Decoder            decoder;
    std::vector<Event> events;
    types::Bytes       bytes { makeBytes(
        muckaii::wire::kTelnetIac, muckaii::wire::kTelnetSb, kTelnetTerminalTypeOption) };
    bytes.insert(bytes.end(), kOversizedSubnegotiationFill, std::byte { 'x' });
    const types::Bytes tail { wire("hello\r\n") };
    bytes.insert(bytes.end(), tail.begin(), tail.end());
    decoder.feed(bytes, events);
    decoder.endOfRead(events);
    std::string shown;
    for (const Event& event : events)
    {
        EXPECT_FALSE(std::holds_alternative<TelnetEvent>(event.mPayload));
        if (const auto* text { std::get_if<TextEvent>(&event.mPayload) }; text != nullptr)
            shown += text->mText;
    }
    EXPECT_TRUE(shown.ends_with("xhello"));
    EXPECT_LT(shown.size(), kOversizedSubnegotiationFill);
}

// Bytes 0x80-0x9A are not markup (protocol.md "Control characters"): they are text, kept as bytes.
// Decoder checks bytes from 0x80 through 0x9A are preserved as text.
TEST(Decoder, Bytes80To9AAreText)
{
    Decoder                    decoder;
    const std::string_view     highBytes { "a\x80\x9A" };
    const std::string_view     lineEnd { "b\r\n" };
    const std::string          input { std::string { highBytes } + std::string { lineEnd } };
    const std::vector<Payload> events { decode(decoder, input) };
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(std::get<TextEvent>(events[0]).mText,
              "a\x80\x9A"
              "b");
}

// Every state machine persists across feed() calls: a subnegotiation, an ESC - escape and a CSI
// split byte by byte give the events the whole read gives.
// Decoder checks split telnet and escape sequences decode like complete sequences.
TEST(Decoder, SequencesSplitAcrossFeedsDecodeAsWhole)
{
    types::Bytes           bytes { makeTelnetSubnegotiationBytes() };
    const std::string_view escapeText { "\x1b-80W[New terminal width is 80]\r\n\x1b[1;31mred\x1b[0m " };
    const std::string_view frameText { "<20 01><02 01>Hall</>.\r\n</>" };
    const std::string      restText { std::string { escapeText } + std::string { frameText } };
    const types::Bytes     rest { wire(restText) };
    bytes.insert(bytes.end(), rest.begin(), rest.end());

    Decoder            whole;
    std::vector<Event> wholeEvents;
    whole.feed(bytes, wholeEvents);
    whole.endOfRead(wholeEvents);

    Decoder            split;
    std::vector<Event> splitEvents;
    for (const std::byte byte : bytes)
        split.feed(std::span { &byte, 1 }, splitEvents);
    split.endOfRead(splitEvents);

    ASSERT_EQ(splitEvents.size(), wholeEvents.size());
    for (std::size_t i { 0 }; i < wholeEvents.size(); ++i)
        EXPECT_EQ(splitEvents[i].mPayload, wholeEvents[i].mPayload) << i;
    EXPECT_TRUE(std::holds_alternative<TelnetEvent>(wholeEvents.front().mPayload));
}

// Decoder checks emitted events receive increasing sequence numbers.
TEST(Decoder, SequenceNumbersCountUp)
{
    Decoder            decoder;
    std::vector<Event> events;
    decoder.feed(wire("a\r\nb\r\n"), events);
    ASSERT_EQ(events.size(), 4U);
    for (std::size_t i { 0 }; i < events.size(); ++i)
        EXPECT_EQ(events[i].mSeq, i);
}
