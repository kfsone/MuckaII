#include "muckaii/mud/golden/golden.hpp"

#include "muckaii/base/types.hpp"
#include "muckaii/base/wire.hpp"
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
// kBackslash marks escaped bytes in the capture-file grammar.
constexpr std::uint8_t kBackslash { 0x5C };
// kControlLimit and kAsciiLimit delimit JSON's unescaped byte range.
constexpr std::uint8_t kControlLimit { 0x20 };
constexpr std::uint8_t kAsciiLimit { 0x7F };
// kLowNibble and kNibbleBits split a byte into hexadecimal digits.
constexpr std::uint8_t kLowNibble { 0x0F };
constexpr unsigned     kNibbleBits { 4 };
// kDecimal is the base used to format C1 number pairs.
constexpr int kDecimal { 10 };
// kHexDigits maps a nibble to its lowercase hexadecimal character.
constexpr std::string_view kHexDigits { "0123456789abcdef" };
// kLowerHexA and kUpperHexA adjust the shared lowercase digit table for uppercase output.
constexpr char kLowerHexA { 'a' };
constexpr char kUpperHexA { 'A' };
// The kJson* constants encode JSON punctuation, escapes, and primitive values.
constexpr char             kJsonQuote { '"' };
constexpr char             kJsonBackslash { '\\' };
constexpr std::string_view kJsonTabEscape { "\\t" };
constexpr std::string_view kJsonLfEscape { "\\n" };
constexpr std::string_view kJsonCrEscape { "\\r" };
constexpr std::string_view kJsonUnicodePrefix { "\\u00" };
constexpr std::string_view kJsonTrue { "true" };
constexpr std::string_view kJsonFalse { "false" };
constexpr std::string_view kJsonNull { "null" };
// Capture escape and digit constants define the capture reader's textual grammar.
constexpr char             kCaptureEscapeCr { 'r' };
constexpr char             kCaptureEscapeLf { 'n' };
constexpr std::string_view kDecimalDigits { "0123456789" };
constexpr std::string_view kReceiveDirection { "rx" };
constexpr std::string_view kTransmitDirection { "tx" };
// Direction names are the exact tags accepted by capture records.
constexpr std::string_view kAnnotationDirection { "an" };
// kDirectionLength counts the direction field in a capture record.
constexpr std::size_t kDirectionLength { 2 };
// kRecordFieldSeparatorCount accounts for the space between timestamp and direction.
constexpr std::size_t kRecordFieldSeparatorCount { 1 };
// kByteChunkLength feeds exactly one byte in byte-at-a-time replay mode.
constexpr std::size_t kByteChunkLength { 1 };
// kEventsSchemaVersion identifies the serialized events.md schema.
constexpr int kEventsSchemaVersion { 2 };
// kStreamRecordRx reserves rx -1 for the stream metadata record.
constexpr int kStreamRecordRx { -1 };

// getHexDigit returns one hexadecimal digit in the requested letter case.
char getHexDigit(std::uint8_t nibble, bool isUppercase) noexcept
{
    char digit { kHexDigits[nibble] };
    if (isUppercase && digit >= kLowerHexA)
        digit = static_cast<char>(digit - (kLowerHexA - kUpperHexA));
    return digit;
}

// JsonWriter builds one JSON value in its internal text buffer.
class JsonWriter
{
public:
    // raw appends a JSON fragment that is already encoded.
    void raw(std::string_view piece)
    {
        mText.append(piece);
    }

    // string appends a quoted JSON string while escaping non-ASCII bytes.
    void string(std::string_view value)
    {
        mText.push_back(kJsonQuote);
        for (const char c : value)
        {
            const auto byte { static_cast<std::uint8_t>(c) };
            if (c == kJsonQuote || c == kJsonBackslash)
            {
                mText.push_back(kJsonBackslash);
                mText.push_back(c);
            }
            else if (c == static_cast<char>(wire::kTab))
                mText.append(kJsonTabEscape);
            else if (c == static_cast<char>(wire::kLf))
                mText.append(kJsonLfEscape);
            else if (c == static_cast<char>(wire::kCr))
                mText.append(kJsonCrEscape);
            else if (byte < kControlLimit || byte > kAsciiLimit)
            {
                mText.append(kJsonUnicodePrefix);
                mText.push_back(getHexDigit(byte >> kNibbleBits, false));
                mText.push_back(getHexDigit(byte & kLowNibble, false));
            }
            else
                mText.push_back(c);
        }
        mText.push_back(kJsonQuote);
    }

    // number appends an integer in decimal form.
    void number(long long value)
    {
        mText.append(std::to_string(value));
    }

    // boolean appends a JSON boolean literal.
    void boolean(bool value)
    {
        mText.append(value ? kJsonTrue : kJsonFalse);
    }

    // colour appends a colour index or JSON null.
    void colour(const Colour& value)
    {
        if (value.has_value())
            number(*value);
        else
            raw(kJsonNull);
    }

    // optionalNumber appends an integer or JSON null.
    void optionalNumber(const types::OptInt& value)
    {
        if (value.has_value())
            number(*value);
        else
            raw(kJsonNull);
    }

    // key appends a comma-prefixed object key and colon.
    void key(std::string_view name)
    {
        mText.push_back(',');
        string(name);
        mText.push_back(':');
    }

    // style serializes a text style object.
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

    // runs serializes an array of styled text runs.
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

    // strings serializes an array of strings.
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

    // numbers serializes an array of integers.
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

    // players serializes the name and colour of each player.
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

    // take returns the completed JSON text and clears the writer buffer.
    [[nodiscard]] std::string take()
    {
        std::string text { std::move(mText) };
        mText.clear();
        return text;
    }

private:
    std::string mText;
};

// getKindName returns the events.md name for a line kind.
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

// getRoomStateName returns the events.md name for a room state.
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

// getOpName returns the events.md name for a C1 stack operation.
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

// getRaw formats a header number sequence as two-digit decimal groups.
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

// getHex formats bytes as uppercase hexadecimal pairs.
std::string getHex(const types::Bytes& bytes)
{
    std::string hex;
    for (const std::byte byte : bytes)
    {
        const auto value { std::to_integer<std::uint8_t>(byte) };
        if (! hex.empty())
            hex.push_back(' ');
        hex.push_back(getHexDigit(value >> kNibbleBits, true));
        hex.push_back(getHexDigit(value & kLowNibble, true));
    }
    return hex;
}

// writeFesStats appends the FES-specific fields of a stats object.
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
        json.raw(kJsonNull);
    json.key("stamina_colour");
    json.colour(event.mStaminaColour);
}

// writeBody appends the event-specific fields for one payload.
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
            else if constexpr (std::is_same_v<T, PromptEvent>)
            {
                json.key("runs");
                json.runs(event.mRuns);
            }
            else if constexpr (std::is_same_v<T, PartialEvent>)
            {
                json.key("runs");
                json.runs(event.mRuns);
                json.key("continued");
                json.boolean(event.mIsContinued);
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
                    json.raw(kJsonNull);
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

// getEventName returns the events.md event name for a payload type.
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

// byteAt reads one byte value from a capture view.
std::uint8_t byteAt(std::span<const std::byte> bytes, std::size_t index)
{
    return std::to_integer<std::uint8_t>(bytes[index]);
}

// parseRecord parses one capture record and rejects malformed fields.
std::optional<CaptureRecord> parseRecord(std::span<const std::byte> line)
{
    std::size_t firstSpace { 0 };
    while (firstSpace < line.size() && byteAt(line, firstSpace) != wire::kFirstPrintable)
        ++firstSpace;
    const std::size_t secondSpace { firstSpace + kRecordFieldSeparatorCount + kDirectionLength };
    if (firstSpace == 0 || secondSpace >= line.size() || byteAt(line, secondSpace) != wire::kFirstPrintable)
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
        || timestamp.find_first_not_of(kDecimalDigits) != std::string::npos)
        return std::nullopt;

    if (direction == kReceiveDirection)
        record.mDirection = Direction::kRx;
    else if (direction == kTransmitDirection)
        record.mDirection = Direction::kTx;
    else if (direction == kAnnotationDirection)
        record.mDirection = Direction::kAn;
    else
        return std::nullopt;

    for (std::size_t i { secondSpace + 1 }; i < line.size(); ++i)
    {
        const std::uint8_t byte { byteAt(line, i) };
        if (byte == wire::kCr)
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
        else if (escaped == static_cast<std::uint8_t>(kCaptureEscapeCr))
            record.mPayload.push_back(std::byte { wire::kCr });
        else if (escaped == static_cast<std::uint8_t>(kCaptureEscapeLf))
            record.mPayload.push_back(std::byte { wire::kLf });
        else
            return std::nullopt;
    }
    return record;
}

// flushPendingText writes the pending text event and clears the merge slot.
void flushPendingText(std::vector<std::string>& lines, std::optional<RxEvent>& pendingText)
{
    if (pendingText.has_value())
        lines.push_back(toJson(pendingText->mEvent.mPayload, pendingText->mRx));
    pendingText.reset();
}
// getDirectionTag returns the capture-file tag of a direction.
std::string_view getDirectionTag(Direction direction) noexcept
{
    if (direction == Direction::kTx)
        return kTransmitDirection;
    if (direction == Direction::kAn)
        return kAnnotationDirection;
    return kReceiveDirection;
}

// appendText appends the characters of `text` to `out` as bytes.
void appendText(types::Bytes& out, std::string_view text)
{
    for (const char c : text)
        out.push_back(std::byte { static_cast<std::uint8_t>(c) });
}

// appendEscapedPayload appends `payload` with the three escapes of the capture grammar applied.
void appendEscapedPayload(types::Bytes& out, std::span<const std::byte> payload)
{
    for (const std::byte byte : payload)
    {
        const std::uint8_t value { std::to_integer<std::uint8_t>(byte) };
        if (value == kBackslash)
            appendText(out, std::string { kJsonBackslash } + kJsonBackslash);
        else if (value == wire::kCr)
            appendText(out, std::string { kJsonBackslash } + kCaptureEscapeCr);
        else if (value == wire::kLf)
            appendText(out, std::string { kJsonBackslash } + kCaptureEscapeLf);
        else
            out.push_back(byte);
    }
}
} // namespace

types::Bytes serializeCapture(CaptureView records)
{
    types::Bytes out;
    for (const CaptureRecord& record : records)
    {
        appendText(out, std::to_string(record.mTimestampMs));
        out.push_back(std::byte { wire::kFirstPrintable });
        appendText(out, getDirectionTag(record.mDirection));
        out.push_back(std::byte { wire::kFirstPrintable });
        appendEscapedPayload(out, record.mPayload);
        out.push_back(std::byte { wire::kLf });
    }
    return out;
}

OptCaptureRecords parseCapture(std::span<const std::byte> data)
{
    if (data.empty() || byteAt(data, data.size() - 1) != wire::kLf)
        return std::nullopt;

    std::vector<CaptureRecord> records;
    std::size_t                start { 0 };
    while (start < data.size())
    {
        std::size_t end { start };
        while (byteAt(data, end) != wire::kLf)
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

OptCaptureRecords readCapture(const std::filesystem::path& path)
{
    std::ifstream file { path, std::ios::binary };
    if (! file)
        return std::nullopt;
    const std::istreambuf_iterator<char> first { file };
    const std::istreambuf_iterator<char> last {};
    const std::vector<char>              bytes { first, last };
    return parseCapture(std::as_bytes(std::span { bytes }));
}

std::vector<RxEvent> replay(CaptureView records, const ReplayOptions& options)
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
                decoder.feed(std::span { &byte, kByteChunkLength }, batch);
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
    stream.raw(R"({"ev":"stream","rx":)");
    stream.number(kStreamRecordRx);
    stream.raw(R"(,"schema":)");
    stream.number(kEventsSchemaVersion);
    stream.raw(R"(,"source":)");
    stream.string(source);
    stream.raw(R"(,"start_in_game":)");
    stream.boolean(isStartInGame);
    stream.raw("}");
    lines.push_back(stream.take());

    // Adjacent text events with equal styles are one run; the merged run takes the later rx.
    std::optional<RxEvent> pendingText;

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
            flushPendingText(lines, pendingText);
            pendingText = item;
            continue;
        }
        flushPendingText(lines, pendingText);
        lines.push_back(toJson(item.mEvent.mPayload, item.mRx));
    }
    flushPendingText(lines, pendingText);
    return lines;
}
} // namespace muckaii::mud::golden
