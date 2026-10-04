#include "muckaii/mud/golden/golden.hpp"

#include "muckaii/mud/decoder.hpp"

#include <charconv>
#include <fstream>
#include <iterator>
#include <type_traits>
#include <utility>

namespace muckaii::mud::golden
{
namespace
{
constexpr std::uint8_t kLf { 0x0A };
constexpr std::uint8_t kCr { 0x0D };
constexpr std::uint8_t kBackslash { 0x5C };
constexpr std::uint8_t kSpace { 0x20 };
constexpr std::uint8_t kControlLimit { 0x20 };
constexpr std::uint8_t kAsciiLimit { 0x7F };
constexpr std::uint8_t kLowNibble { 0x0F };
constexpr unsigned     kNibbleBits { 4 };
constexpr int          kDecimal { 10 };
// "<ts> rx <payload>": the direction sits between the two spaces.
constexpr std::size_t kDirectionLength { 2 };

class JsonWriter
{
public:
    void raw(std::string_view piece)
    {
        mText.append(piece);
    }

    void string(std::string_view value)
    {
        static constexpr std::string_view kHex { "0123456789abcdef" };
        mText.push_back('"');
        for (const char c : value)
        {
            const auto byte { static_cast<std::uint8_t>(c) };
            if (c == '"' || c == '\\')
            {
                mText.push_back('\\');
                mText.push_back(c);
            }
            else if (c == '\t')
                mText.append("\\t");
            else if (c == '\n')
                mText.append("\\n");
            else if (c == '\r')
                mText.append("\\r");
            else if (byte < kControlLimit || byte > kAsciiLimit)
            {
                mText.append("\\u00");
                mText.push_back(kHex[byte >> kNibbleBits]);
                mText.push_back(kHex[byte & kLowNibble]);
            }
            else
                mText.push_back(c);
        }
        mText.push_back('"');
    }

    void number(long long value)
    {
        mText.append(std::to_string(value));
    }

    void boolean(bool value)
    {
        mText.append(value ? "true" : "false");
    }

    void colour(const Colour& value)
    {
        if (value.has_value())
            number(*value);
        else
            raw("null");
    }

    void optionalNumber(const std::optional<int>& value)
    {
        if (value.has_value())
            number(*value);
        else
            raw("null");
    }

    void key(std::string_view name)
    {
        mText.push_back(',');
        string(name);
        mText.push_back(':');
    }

    void style(const Style& value)
    {
        static constexpr std::array<std::pair<Attr, std::string_view>, 5> kNames { {
            { Attr::kBlink, "blink" },
            { Attr::kBold, "bold" },
            { Attr::kItalic, "italic" },
            { Attr::kReverse, "reverse" },
            { Attr::kUnderline, "underline" },
        } };
        raw(R"({"fg":)");
        colour(value.mFg);
        raw(R"(,"bg":)");
        colour(value.mBg);
        raw(R"(,"attrs":[)");
        bool isFirst { true };
        for (const auto& [attr, name] : kNames)
        {
            if (! value.hasAttr(attr))
                continue;
            if (! isFirst)
                mText.push_back(',');
            string(name);
            isFirst = false;
        }
        raw("]}");
    }

    void runs(const std::vector<Run>& value)
    {
        mText.push_back('[');
        for (std::size_t i { 0 }; i < value.size(); ++i)
        {
            if (i > 0)
                mText.push_back(',');
            raw(R"({"text":)");
            string(value[i].mText);
            raw(R"(,"style":)");
            style(value[i].mStyle);
            mText.push_back('}');
        }
        mText.push_back(']');
    }

    void strings(const std::vector<std::string>& value)
    {
        mText.push_back('[');
        for (std::size_t i { 0 }; i < value.size(); ++i)
        {
            if (i > 0)
                mText.push_back(',');
            string(value[i]);
        }
        mText.push_back(']');
    }

    void numbers(const std::vector<int>& value)
    {
        mText.push_back('[');
        for (std::size_t i { 0 }; i < value.size(); ++i)
        {
            if (i > 0)
                mText.push_back(',');
            number(value[i]);
        }
        mText.push_back(']');
    }

    void players(const std::vector<Player>& value)
    {
        mText.push_back('[');
        for (std::size_t i { 0 }; i < value.size(); ++i)
        {
            if (i > 0)
                mText.push_back(',');
            raw(R"({"name":)");
            string(value[i].mName);
            raw(R"(,"fg":)");
            colour(value[i].mFg);
            mText.push_back('}');
        }
        mText.push_back(']');
    }

    [[nodiscard]] std::string take()
    {
        std::string text { std::move(mText) };
        mText.clear();
        return text;
    }

private:
    std::string mText;
};

std::string_view getKindName(LineKind kind) noexcept
{
    switch (kind)
    {
        case LineKind::kNormal:
            return "normal";
        case LineKind::kChat:
            return "chat";
        case LineKind::kFightStart:
            return "fight_start";
        case LineKind::kFightEnd:
            return "fight_end";
        case LineKind::kCreatureInvisible:
            return "creature_invisible";
    }
    return "normal";
}

std::string_view getRoomStateName(RoomState state) noexcept
{
    switch (state)
    {
        case RoomState::kListed:
            return "listed";
        case RoomState::kBlind:
            return "blind";
        case RoomState::kDark:
            return "dark";
    }
    return "listed";
}

std::string_view getOpName(StackOp op) noexcept
{
    switch (op)
    {
        case StackOp::kPush:
            return "push";
        case StackOp::kPushEmpty:
            return "push_empty";
        case StackOp::kCatch:
            return "catch";
        case StackOp::kThrow:
            return "throw";
    }
    return "push";
}

std::string getRaw(const Header& header)
{
    std::string raw;
    for (std::size_t i { 0 }; i < header.mCount; ++i)
    {
        if (i > 0)
            raw.push_back(' ');
        // i < mCount <= kMaxHeaderNumbers, the array's size.
        raw.push_back(static_cast<char>('0' + (header.mNumbers[i] / kDecimal)));
        raw.push_back(static_cast<char>('0' + (header.mNumbers[i] % kDecimal)));
    }
    return raw;
}

std::string getHex(const std::vector<std::byte>& bytes)
{
    static constexpr std::string_view kHex { "0123456789ABCDEF" };
    std::string                       hex;
    for (const std::byte byte : bytes)
    {
        const auto value { std::to_integer<std::uint8_t>(byte) };
        if (! hex.empty())
            hex.push_back(' ');
        hex.push_back(kHex[value >> kNibbleBits]);
        hex.push_back(kHex[value & kLowNibble]);
    }
    return hex;
}

void writeFesStats(JsonWriter& json, const FesStatsEvent& event)
{
    json.key("source");
    json.string("fes");
    json.key("stamina");
    json.number(event.mStamina);
    json.key("max_stamina");
    json.number(event.mMaxStamina);
    json.key("strength");
    json.number(event.mStrength);
    json.key("max_strength");
    json.number(event.mMaxStrength);
    json.key("dexterity");
    json.number(event.mDexterity);
    json.key("max_dexterity");
    json.number(event.mMaxDexterity);
    json.key("magic");
    json.number(event.mMagic);
    json.key("max_magic");
    json.number(event.mMaxMagic);
    json.key("score");
    json.number(event.mScore);
    json.key("blind");
    json.boolean(event.mIsBlind);
    json.key("deaf");
    json.boolean(event.mIsDeaf);
    json.key("crippled");
    json.boolean(event.mIsCrippled);
    json.key("dumb");
    json.boolean(event.mIsDumb);
    json.key("reset_minutes");
    json.number(event.mResetMinutes);
    json.key("weather");
    if (event.mWeather.has_value())
        json.string(std::string(1, *event.mWeather));
    else
        json.raw("null");
    json.key("stamina_colour");
    json.colour(event.mStaminaColour);
}

void writeBody(JsonWriter& json, const Payload& payload)
{
    std::visit(
        [&json](const auto& event)
        {
            using T = std::decay_t<decltype(event)>;
            if constexpr (std::is_same_v<T, TextEvent>)
            {
                json.key("text");
                json.string(event.mText);
                json.key("style");
                json.style(event.mStyle);
            }
            else if constexpr (std::is_same_v<T, LineEndEvent>)
            {
                json.key("kind");
                json.string(getKindName(event.mKind));
                json.key("chat_continues");
                json.boolean(event.mIsChatContinued);
            }
            else if constexpr (std::is_same_v<T, PromptEvent> || std::is_same_v<T, PartialEvent>)
            {
                json.key("runs");
                json.runs(event.mRuns);
            }
            else if constexpr (std::is_same_v<T, WidthAckEvent>)
            {
                json.key("columns");
                json.number(event.mColumns);
            }
            else if constexpr (std::is_same_v<T, FesStatsEvent>)
            {
                writeFesStats(json, event);
            }
            else if constexpr (std::is_same_v<T, CodeStatsEvent>)
            {
                json.key("source");
                json.string("code");
                json.key("stamina");
                json.optionalNumber(event.mStamina);
                json.key("max_stamina");
                json.optionalNumber(event.mMaxStamina);
                json.key("score");
                json.optionalNumber(event.mScore);
            }
            else if constexpr (std::is_same_v<T, RoomNameEvent> || std::is_same_v<T, RoomLongEvent>)
            {
                json.key("text");
                json.string(event.mText);
            }
            else if constexpr (std::is_same_v<T, ExitsEvent>)
            {
                json.key("exits");
                json.strings(event.mExits);
            }
            else if constexpr (std::is_same_v<T, InventoryEvent>)
            {
                json.key("room_state");
                json.string(getRoomStateName(event.mRoomState));
                json.key("room");
                json.strings(event.mRoom);
                json.key("carried");
                json.strings(event.mCarried);
            }
            else if constexpr (std::is_same_v<T, WhoEvent>)
            {
                json.key("players");
                json.players(event.mPlayers);
            }
            else if constexpr (std::is_same_v<T, DreamwordEvent>)
            {
                json.key("word");
                if (event.mWord.has_value())
                    json.string(*event.mWord);
                else
                    json.raw("null");
            }
            else if constexpr (std::is_same_v<T, ClientModeEvent>)
            {
                json.key("licence");
                json.string(event.mLicence);
                json.key("min_level");
                json.number(event.mMinLevel);
                json.key("max_level");
                json.number(event.mMaxLevel);
                json.key("account");
                json.string(event.mAccount);
                json.key("privs");
                json.number(event.mPrivs);
            }
            else if constexpr (std::is_same_v<T, AccountChangeEvent>)
            {
                json.key("account");
                json.string(event.mAccount);
                json.key("privs");
                json.number(event.mPrivs);
            }
            else if constexpr (std::is_same_v<T, TellerEvent>)
            {
                json.key("name");
                json.string(event.mName);
            }
            else if constexpr (std::is_same_v<T, SoundCodeEvent>)
            {
                json.key("family");
                json.number(event.mFamily);
                json.key("params");
                json.numbers(event.mParams);
            }
            else if constexpr (std::is_same_v<T, ResetSignalEvent>)
            {
                json.key("id");
                json.string(event.mId);
            }
            else if constexpr (std::is_same_v<T, SpellPhraseEvent>)
            {
                json.key("id");
                json.string(event.mId);
                json.key("phrase");
                json.string(event.mPhrase);
            }
            else if constexpr (std::is_same_v<T, CodeEvent>)
            {
                json.key("id");
                json.string(event.mId);
                json.key("raw");
                json.string(getRaw(event.mHeader));
                json.key("op");
                json.string(getOpName(event.mOp));
            }
            else if constexpr (std::is_same_v<T, CloseEvent>)
            {
                json.key("popped");
                json.boolean(event.mIsPopped);
            }
            else if constexpr (std::is_same_v<T, TelnetEvent>)
            {
                json.key("bytes");
                json.string(getHex(event.mBytes));
            }
        },
        payload);
}

std::string_view getEventName(const Payload& payload) noexcept
{
    static constexpr std::array<std::string_view, std::variant_size_v<Payload>> kNames {
        "text",           "line_end",     "prompt",         "partial",         "clear_screen", "erase_eol",
        "end_of_session", "width_ack",    "bell",           "game_enter",      "game_exit",    "stats",
        "stats",          "room_name",    "room_long",      "exits",           "inventory",    "who",
        "dreamword",      "client_mode",  "account_change", "persona_unsaved", "teller",       "sound_code",
        "reset_signal",   "spell_phrase", "code",           "close",           "telnet",
    };
    // index() < variant_size_v: with exceptions off a variant is never valueless.
    return kNames[payload.index()];
}

std::uint8_t byteAt(std::span<const std::byte> bytes, std::size_t index)
{
    return std::to_integer<std::uint8_t>(bytes[index]);
}

std::optional<CaptureRecord> parseRecord(std::span<const std::byte> line)
{
    std::size_t firstSpace { 0 };
    while (firstSpace < line.size() && byteAt(line, firstSpace) != kSpace)
        ++firstSpace;
    const std::size_t secondSpace { firstSpace + 1 + kDirectionLength };
    if (firstSpace == 0 || secondSpace >= line.size() || byteAt(line, secondSpace) != kSpace)
        return std::nullopt;

    std::string timestamp;
    std::string direction;
    for (std::size_t i { 0 }; i < firstSpace; ++i)
        timestamp.push_back(static_cast<char>(byteAt(line, i)));
    for (std::size_t i { firstSpace + 1 }; i < secondSpace; ++i)
        direction.push_back(static_cast<char>(byteAt(line, i)));

    CaptureRecord record;
    const auto    parsed { std::from_chars(
        timestamp.data(), timestamp.data() + timestamp.size(), record.mTimestampMs) };
    if (parsed.ec != std::errc {} || parsed.ptr != timestamp.data() + timestamp.size()
        || timestamp.find_first_not_of("0123456789") != std::string::npos)
        return std::nullopt;

    if (direction == "rx")
        record.mDirection = Direction::kRx;
    else if (direction == "tx")
        record.mDirection = Direction::kTx;
    else if (direction == "an")
        record.mDirection = Direction::kAn;
    else
        return std::nullopt;

    for (std::size_t i { secondSpace + 1 }; i < line.size(); ++i)
    {
        const std::uint8_t byte { byteAt(line, i) };
        if (byte == kCr)
            return std::nullopt;
        if (byte != kBackslash)
        {
            record.mPayload.push_back(line[i]);
            continue;
        }
        if (i + 1 >= line.size())
            return std::nullopt;
        const std::uint8_t escaped { byteAt(line, ++i) };
        if (escaped == kBackslash)
            record.mPayload.push_back(std::byte { kBackslash });
        else if (escaped == 'r')
            record.mPayload.push_back(std::byte { kCr });
        else if (escaped == 'n')
            record.mPayload.push_back(std::byte { kLf });
        else
            return std::nullopt;
    }
    return record;
}
} // namespace

std::optional<std::vector<CaptureRecord>> parseCapture(std::span<const std::byte> data)
{
    if (data.empty() || byteAt(data, data.size() - 1) != kLf)
        return std::nullopt;

    std::vector<CaptureRecord> records;
    std::size_t                start { 0 };
    while (start < data.size())
    {
        std::size_t end { start };
        while (byteAt(data, end) != kLf)
            ++end;
        const std::span<const std::byte> line { data.subspan(start, end - start) };
        start = end + 1;
        if (line.empty())
            return std::nullopt;
        std::optional<CaptureRecord> record { parseRecord(line) };
        if (! record.has_value())
            return std::nullopt;
        records.push_back(std::move(*record));
    }
    return records;
}

std::optional<std::vector<CaptureRecord>> readCapture(const std::filesystem::path& path)
{
    std::ifstream file { path, std::ios::binary };
    if (! file)
        return std::nullopt;
    const std::vector<char> bytes { std::istreambuf_iterator<char> { file },
                                    std::istreambuf_iterator<char> {} };
    return parseCapture(std::as_bytes(std::span { bytes }));
}

std::vector<RxEvent> replay(std::span<const CaptureRecord> records, const ReplayOptions& options)
{
    Decoder            decoder { DecoderOptions { .mWillEmitWireTier = options.mWillEmitWireTier } };
    std::vector<Event> batch;

    if (options.mIsStartInGame)
    {
        decoder.feed(std::as_bytes(std::span { kPrimingBytes }), batch);
        decoder.endOfRead(batch);
        batch.clear();
    }

    std::vector<RxEvent> events;
    int                  rx { 0 };
    for (const CaptureRecord& record : records)
    {
        if (record.mDirection != Direction::kRx)
            continue;
        if (options.mIsByteAtATime)
        {
            for (const std::byte& byte : record.mPayload)
                decoder.feed(std::span { &byte, 1 }, batch);
        }
        else
        {
            decoder.feed(record.mPayload, batch);
        }
        decoder.endOfRead(batch);
        for (Event& event : batch)
            events.push_back(RxEvent { .mRx = rx, .mEvent = std::move(event) });
        batch.clear();
        ++rx;
    }
    return events;
}

std::string toJson(const Payload& payload, int rx)
{
    JsonWriter json;
    json.raw(R"({"ev":)");
    json.string(getEventName(payload));
    json.raw(R"(,"rx":)");
    json.number(rx);
    writeBody(json, payload);
    json.raw("}");
    return json.take();
}

std::vector<std::string>
toGoldenLines(std::string_view source, bool isStartInGame, std::span<const RxEvent> events)
{
    std::vector<std::string> lines;
    JsonWriter               stream;
    stream.raw(R"({"ev":"stream","rx":-1,"schema":2,"source":)");
    stream.string(source);
    stream.raw(R"(,"start_in_game":)");
    stream.boolean(isStartInGame);
    stream.raw("}");
    lines.push_back(stream.take());

    // Adjacent text events with equal styles are one run; the merged run takes the later rx.
    std::optional<RxEvent> pendingText;
    const auto             flush { [&lines, &pendingText]()
                                   {
                           if (pendingText.has_value())
                               lines.push_back(toJson(pendingText->mEvent.mPayload, pendingText->mRx));
                           pendingText.reset();
                       } };

    for (const RxEvent& item : events)
    {
        if (isWireTier(item.mEvent.mPayload))
            continue;
        if (const auto* const text { std::get_if<TextEvent>(&item.mEvent.mPayload) }; text != nullptr)
        {
            if (pendingText.has_value())
            {
                auto& merged { std::get<TextEvent>(pendingText->mEvent.mPayload) };
                if (merged.mStyle == text->mStyle)
                {
                    merged.mText.append(text->mText);
                    pendingText->mRx = item.mRx;
                    continue;
                }
            }
            flush();
            pendingText = item;
            continue;
        }
        flush();
        lines.push_back(toJson(item.mEvent.mPayload, item.mRx));
    }
    flush();
    return lines;
}
} // namespace muckaii::mud::golden
