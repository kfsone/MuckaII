#include "muckaii/mud/codes.hpp"
#include "muckaii/mud/decoder.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace muckaii::mud;

namespace
{
Header makeHeader(std::initializer_list<std::uint8_t> numbers)
{
    Header header;
    for (const std::uint8_t number : numbers)
        header.mNumbers[header.mCount++] = number;
    return header;
}

std::string_view findId(std::initializer_list<std::uint8_t> numbers)
{
    const CodeInfo* code { findCode(makeHeader(numbers)) };
    return code != nullptr ? code->mId : std::string_view { "<none>" };
}

// Wire bytes from a readable form: <n n> is a header, </> a lone 255, everything else is itself.
std::vector<std::byte> wire(std::string_view text)
{
    std::vector<std::byte> bytes;
    for (std::size_t i { 0 }; i < text.size(); ++i)
    {
        if (text.substr(i).starts_with("</>"))
        {
            bytes.insert(bytes.end(), { std::byte { 0xFF }, std::byte { 0xFF } });
            i += 2;
            continue;
        }
        if (text[i] == '<')
        {
            const std::size_t end { text.find('>', i) };
            for (std::size_t pos { i + 1 }; pos < end; pos += 3)
            {
                const int number { ((text[pos] - '0') * 10) + (text[pos + 1] - '0') };
                bytes.push_back(std::byte { static_cast<std::uint8_t>(155 + number) });
            }
            bytes.insert(bytes.end(), { std::byte { 0xFF }, std::byte { 0xFF } });
            i = end;
            continue;
        }
        bytes.push_back(std::byte { static_cast<std::uint8_t>(text[i]) });
    }
    return bytes;
}

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

Decoder primedDecoder()
{
    Decoder decoder;
    decode(decoder, "<01 02></><01><01 02>*</></>");
    return decoder;
}
} // namespace

TEST(CodeMatching, ExactIdWins)
{
    EXPECT_EQ(findId({ 8, 1 }), "08 01");
    EXPECT_EQ(findId({ 99, 99 }), "99 99");
}

TEST(CodeMatching, ParametricEntryTakesRemainingNumbers)
{
    EXPECT_EQ(findId({ 99, 10 }), "99");
    EXPECT_EQ(findId({ 98, 3 }), "98");
}

TEST(CodeMatching, TrailingZerosAreRemoved)
{
    EXPECT_EQ(findId({ 8, 0 }), "08");
    EXPECT_EQ(findId({ 7, 2, 0 }), "07 02");
    EXPECT_EQ(findId({ 11, 0 }), "11");
}

TEST(CodeMatching, ZerosAreAppended)
{
    EXPECT_EQ(findId({ 20 }), "20 00");
    EXPECT_EQ(findId({ 14 }), "14 00");
}

TEST(CodeMatching, UnknownHeaderMatchesNothing)
{
    EXPECT_EQ(findId({ 50, 1 }), "<none>");
}

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

TEST(Decoder, PreGameLineIsTextThenLineEnd)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder, "Welcome\r\n") };
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(std::get<TextEvent>(events[0]).mText, "Welcome");
    EXPECT_TRUE(std::holds_alternative<LineEndEvent>(events[1]));
}

TEST(Decoder, TelnetCommandsAreEvents)
{
    Decoder                      decoder;
    std::vector<Event>           events;
    const std::vector<std::byte> bytes { std::byte { 0xFF }, std::byte { 0xFD }, std::byte { 0x18 } };
    decoder.feed(bytes, events);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(std::get<TelnetEvent>(events[0].mPayload).mBytes, bytes);
}

TEST(Decoder, TelnetSubnegotiationIsOneEvent)
{
    Decoder                      decoder;
    std::vector<Event>           events;
    const std::vector<std::byte> bytes {
        std::byte { 0xFF }, std::byte { 0xFA }, std::byte { 0x18 },
        std::byte { 0x01 }, std::byte { 0xFF }, std::byte { 0xF0 },
    };
    decoder.feed(bytes, events);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(std::get<TelnetEvent>(events[0].mPayload).mBytes, bytes);
}

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

TEST(Decoder, EnteringPromptIsShownAsAPrompt)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder, "<01><01 02>*</></>") };
    ASSERT_EQ(events.size(), 2U);
    EXPECT_TRUE(std::holds_alternative<GameEnterEvent>(events[0]));
    const auto& prompt { std::get<PromptEvent>(events[1]) };
    ASSERT_EQ(prompt.mRuns.size(), 1U);
    EXPECT_EQ(prompt.mRuns[0].mText, "*");
    EXPECT_EQ(prompt.mRuns[0].mStyle.mFg, std::optional<std::uint8_t> { 12 });
}

TEST(Decoder, PromptGateDiscardsAPromptWithNoLineBetween)
{
    Decoder                    decoder { primedDecoder() };
    const std::vector<Payload> events { decode(decoder, "<01><01 02>*</></>") };
    EXPECT_TRUE(events.empty());
}

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

TEST(Decoder, AllStarLinesAreNotDisplayedInGame)
{
    Decoder                    decoder { primedDecoder() };
    const std::vector<Payload> events { decode(decoder, "***\r\n") };
    EXPECT_TRUE(events.empty());
}

TEST(Decoder, WidthNoticeIsSwallowedAfterWidthEscape)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder, "\x1b-80W[New terminal width is 80]\r\n") };
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(std::get<WidthAckEvent>(events[0]).mColumns, 80);
}

TEST(Decoder, PreGameWidthNoticeLineIsAWidthAck)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder, "[New terminal width is 20]\r\n") };
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(std::get<WidthAckEvent>(events[0]).mColumns, 20);
}

TEST(Decoder, ClientModeBlockIsReadNotShown)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder,
                                               "<95>Unlicensed product!\r\n1\r\n4\r\nZ0000000\r\n1\r\n</>") };
    ASSERT_EQ(events.size(), 1U);
    const auto& mode { std::get<ClientModeEvent>(events[0]) };
    EXPECT_EQ(mode.mLicence, "Unlicensed product!");
    EXPECT_EQ(mode.mMinLevel, 1);
    EXPECT_EQ(mode.mMaxLevel, 4);
    EXPECT_EQ(mode.mAccount, "Z0000000");
    EXPECT_EQ(mode.mPrivs, 1);
}

TEST(Decoder, ClearScreenWritesPendingTextFirst)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder, "abc\x1b-C") };
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(std::get<TextEvent>(events[0]).mText, "abc");
    EXPECT_TRUE(std::holds_alternative<ClearScreenEvent>(events[1]));
}

TEST(Decoder, SgrSetsColourAndBold)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder, "\x1b[1;31mred\x1b[0m\r\n") };
    ASSERT_GE(events.size(), 1U);
    const auto& text { std::get<TextEvent>(events[0]) };
    EXPECT_EQ(text.mText, "red");
    EXPECT_EQ(text.mStyle.mFg, std::optional<std::uint8_t> { 1 });
    EXPECT_TRUE(text.mStyle.hasAttr(Attr::kBold));
}

TEST(Decoder, SgrResetIsAcceptedAcrossALineEnd)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder, "\x1b[31m\x1b[0;37;40\r\nmx\r\n") };
    ASSERT_EQ(events.size(), 2U);
    const auto& text { std::get<TextEvent>(events[0]) };
    EXPECT_EQ(text.mText, "x");
    EXPECT_EQ(text.mStyle.mFg, std::optional<std::uint8_t> { 7 });
}

TEST(Decoder, DreamwordIsReadAfterItsCode)
{
    Decoder decoder { primedDecoder() };
    bool    hasWord {};
    for (const Payload& event : decode(decoder, "<15 00 00>plugh</>\r\n"))
    {
        if (const auto* word { std::get_if<DreamwordEvent>(&event) }; word != nullptr)
        {
            EXPECT_EQ(word->mWord, std::optional<std::string> { "plugh" });
            hasWord = true;
        }
    }
    EXPECT_TRUE(hasWord);
}

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

TEST(Decoder, AccountLogoutLeavesGameMode)
{
    Decoder                    decoder { primedDecoder() };
    const std::vector<Payload> events { decode(decoder, "<95 03>Z0000000\r\n</>") };
    ASSERT_EQ(events.size(), 1U);
    EXPECT_TRUE(std::holds_alternative<GameExitEvent>(events[0]));
    EXPECT_FALSE(decoder.isInGameMode());
}

TEST(Decoder, CatchAndThrowRestoreTheStack)
{
    Decoder                    decoder { primedDecoder() };
    const std::vector<Payload> events { decode(decoder, "<05 01 06><90>a<99 01>b<90 01>c</>\r\n") };
    ASSERT_GE(events.size(), 3U);
    EXPECT_EQ(std::get<TextEvent>(events[0]).mText, "a");
    EXPECT_EQ(std::get<TextEvent>(events[0]).mStyle.mFg, std::optional<std::uint8_t> { 9 });
    EXPECT_EQ(std::get<TextEvent>(events[1]).mStyle.mFg, std::optional<std::uint8_t> { 1 });
    EXPECT_EQ(std::get<TextEvent>(events[2]).mText, "c");
    EXPECT_EQ(std::get<TextEvent>(events[2]).mStyle.mFg, std::optional<std::uint8_t> { 9 });
}

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
        EXPECT_EQ(texts[i]->mStyle.mFg, std::optional<std::uint8_t> { kExpected[i].second }) << i;
    }
}

// FF FF is telnet's IAC IAC: one data byte 255, which ends a header and, alone, closes a code.
TEST(Decoder, IacIacIsTheDataByte255)
{
    Decoder                      decoder { DecoderOptions { .mWillEmitWireTier = true } };
    std::vector<Event>           events;
    const std::vector<std::byte> bytes {
        std::byte { 0x9C }, std::byte { 0xFF }, std::byte { 0xFF }, std::byte { 0xFF }, std::byte { 0xFF }
    };
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
TEST(Decoder, ATelnetCommandInsideAHeaderLeavesTheHeaderWhole)
{
    Decoder                      decoder { DecoderOptions { .mWillEmitWireTier = true } };
    std::vector<Event>           events;
    const std::vector<std::byte> bytes { std::byte { 0x9C }, std::byte { 0xFF }, std::byte { 0xFB },
                                         std::byte { 0x01 }, std::byte { 0x9D }, std::byte { 0xFF },
                                         std::byte { 0xFF } };
    decoder.feed(bytes, events);
    std::vector<std::byte> telnet;
    std::string_view       codeId { "<none>" };
    for (const Event& event : events)
    {
        if (const auto* command { std::get_if<TelnetEvent>(&event.mPayload) }; command != nullptr)
            telnet = command->mBytes;
        if (const auto* code { std::get_if<CodeEvent>(&event.mPayload) }; code != nullptr)
            codeId = code->mId;
    }
    EXPECT_EQ(telnet,
              (std::vector<std::byte> { std::byte { 0xFF }, std::byte { 0xFB }, std::byte { 0x01 } }));
    EXPECT_EQ(codeId, "01 02");
}

// The decoder's rule: IAC and anything but IAC or SE inside a subnegotiation abandons it, and the
// IAC and that byte are a command of their own.
TEST(Decoder, AnUnexpectedCommandInsideASubnegotiationAbandonsIt)
{
    Decoder                      decoder;
    std::vector<Event>           events;
    const std::vector<std::byte> bytes { std::byte { 0xFF }, std::byte { 0xFA }, std::byte { 0x18 },
                                         std::byte { 0x01 }, std::byte { 0xFF }, std::byte { 0xFD },
                                         std::byte { 0x01 } };
    decoder.feed(bytes, events);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(std::get<TelnetEvent>(events[0].mPayload).mBytes,
              (std::vector<std::byte> { std::byte { 0xFF }, std::byte { 0xFD }, std::byte { 0x01 } }));
}

// An SB that never ends is abandoned at its ceiling (connection.md 2.1, 1024 bytes) and decoding
// resumes, so the text after it is shown.
TEST(Decoder, AnUnterminatedSubnegotiationIsAbandonedAtItsCeiling)
{
    Decoder                decoder;
    std::vector<Event>     events;
    std::vector<std::byte> bytes { std::byte { 0xFF }, std::byte { 0xFA }, std::byte { 0x18 } };
    bytes.insert(bytes.end(), 2000, std::byte { 'x' });
    const std::vector<std::byte> tail { wire("hello\r\n") };
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
    EXPECT_LT(shown.size(), std::size_t { 2000 });
}

// Bytes 0x80-0x9A are not markup (protocol.md "Control characters"): they are text, kept as bytes.
TEST(Decoder, Bytes80To9AAreText)
{
    Decoder                    decoder;
    const std::vector<Payload> events { decode(decoder,
                                               "a\x80\x9A"
                                               "b\r\n") };
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(std::get<TextEvent>(events[0]).mText,
              "a\x80\x9A"
              "b");
}

// Every state machine persists across feed() calls: a subnegotiation, an ESC - escape and a CSI
// split byte by byte give the events the whole read gives.
TEST(Decoder, SequencesSplitAcrossFeedsDecodeAsWhole)
{
    std::vector<std::byte>       bytes { std::byte { 0xFF }, std::byte { 0xFA }, std::byte { 0x18 },
                                         std::byte { 0x01 }, std::byte { 0xFF }, std::byte { 0xF0 } };
    const std::vector<std::byte> rest { wire("\x1b-80W[New terminal width is 80]\r\n\x1b[1;31mred\x1b[0m "
                                             "<20 01><02 01>Hall</>.\r\n</>") };
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

TEST(Decoder, SequenceNumbersCountUp)
{
    Decoder            decoder;
    std::vector<Event> events;
    decoder.feed(wire("a\r\nb\r\n"), events);
    ASSERT_EQ(events.size(), 4U);
    for (std::size_t i { 0 }; i < events.size(); ++i)
        EXPECT_EQ(events[i].mSeq, i);
}
