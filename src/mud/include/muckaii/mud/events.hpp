#pragma once

#include "muckaii/mud/codes.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// The decoder's events: one type per event in docs/spec/events.md. Text is bytes, never decoded:
// a std::string here holds the wire's byte values (0x20-0x7E, TAB, and 0x80-0x9A as themselves).
namespace muckaii::mud
{
// A colour index 0-15 (docs/spec/protocol.md "Colour indices"); nullopt is the terminal default.
using Colour = std::optional<std::uint8_t>;

constexpr std::uint8_t kMaxColourIndex { 15 };

// Style attributes as bits. Their order is the sorted order events.md writes them in.
enum class Attr : std::uint8_t
{
    kBlink     = 1U << 0U,
    kBold      = 1U << 1U,
    kItalic    = 1U << 2U,
    kReverse   = 1U << 3U,
    kUnderline = 1U << 4U,
};

struct Style
{
    Colour       mFg;
    Colour       mBg;
    std::uint8_t mAttrs {};

    [[nodiscard]] bool hasAttr(Attr attr) const noexcept
    {
        return (mAttrs & static_cast<std::uint8_t>(attr)) != 0U;
    }

    bool operator==(const Style&) const = default;
};

struct Run
{
    std::string mText;
    Style       mStyle;

    bool operator==(const Run&) const = default;
};

struct Player
{
    std::string mName;
    Colour      mFg;

    bool operator==(const Player&) const = default;
};

enum class LineKind : std::uint8_t
{
    kNormal,
    kChat,
    kFightStart,
    kFightEnd,
    kCreatureInvisible,
};

enum class RoomState : std::uint8_t
{
    kListed,
    kBlind,
    kDark,
};

// -- Text and lines ----------------------------------------------------------------------------

struct TextEvent
{
    std::string mText;
    Style       mStyle;

    bool operator==(const TextEvent&) const = default;
};

struct LineEndEvent
{
    LineKind mKind { LineKind::kNormal };
    bool     mIsChatContinued {};

    bool operator==(const LineEndEvent&) const = default;
};

struct PromptEvent
{
    std::vector<Run> mRuns;

    bool operator==(const PromptEvent&) const = default;
};

struct PartialEvent
{
    std::vector<Run> mRuns;
    bool             mIsContinued {}; // no line end since the previous partial: extend it, else replace

    bool operator==(const PartialEvent&) const = default;
};

struct ClearScreenEvent
{
    bool operator==(const ClearScreenEvent&) const = default;
};

struct EraseEolEvent
{
    bool operator==(const EraseEolEvent&) const = default;
};

struct EndOfSessionEvent
{
    bool operator==(const EndOfSessionEvent&) const = default;
};

struct WidthAckEvent
{
    int mColumns {};

    bool operator==(const WidthAckEvent&) const = default;
};

struct BellEvent
{
    bool operator==(const BellEvent&) const = default;
};

// -- Mode --------------------------------------------------------------------------------------

struct GameEnterEvent
{
    bool operator==(const GameEnterEvent&) const = default;
};

struct GameExitEvent
{
    bool operator==(const GameExitEvent&) const = default;
};

// -- Readings ----------------------------------------------------------------------------------

// `stats` with `source` `fes`: one FES line (docs/spec/protocol.md "FES line").
struct FesStatsEvent
{
    int                 mStamina {};
    int                 mMaxStamina {};
    int                 mStrength {};
    int                 mMaxStrength {};
    int                 mDexterity {};
    int                 mMaxDexterity {};
    int                 mMagic {};
    int                 mMaxMagic {};
    int                 mScore {};
    bool                mIsBlind {};
    bool                mIsDeaf {};
    bool                mIsCrippled {};
    bool                mIsDumb {};
    int                 mResetMinutes {};
    std::optional<char> mWeather;
    Colour              mStaminaColour;

    bool operator==(const FesStatsEvent&) const = default;
};

// `stats` with `source` `code`: the 89 brackets of one displayed line.
struct CodeStatsEvent
{
    std::optional<int> mStamina;
    std::optional<int> mMaxStamina;
    std::optional<int> mScore;

    bool operator==(const CodeStatsEvent&) const = default;
};

struct RoomNameEvent
{
    std::string mText;

    bool operator==(const RoomNameEvent&) const = default;
};

struct RoomLongEvent
{
    std::string mText;

    bool operator==(const RoomLongEvent&) const = default;
};

struct ExitsEvent
{
    std::vector<std::string> mExits;

    bool operator==(const ExitsEvent&) const = default;
};

struct InventoryEvent
{
    RoomState                mRoomState { RoomState::kListed };
    std::vector<std::string> mRoom;
    std::vector<std::string> mCarried;

    bool operator==(const InventoryEvent&) const = default;
};

struct WhoEvent
{
    std::vector<Player> mPlayers;

    bool operator==(const WhoEvent&) const = default;
};

struct DreamwordEvent
{
    std::optional<std::string> mWord;

    bool operator==(const DreamwordEvent&) const = default;
};

struct ClientModeEvent
{
    std::string mLicence;
    int         mMinLevel {};
    int         mMaxLevel {};
    std::string mAccount;
    int         mPrivs {};

    bool operator==(const ClientModeEvent&) const = default;
};

struct AccountChangeEvent
{
    std::string mAccount;
    int         mPrivs {};

    bool operator==(const AccountChangeEvent&) const = default;
};

struct PersonaUnsavedEvent
{
    bool operator==(const PersonaUnsavedEvent&) const = default;
};

struct TellerEvent
{
    std::string mName;

    bool operator==(const TellerEvent&) const = default;
};

// -- Code events (deferred to the next flush point) ---------------------------------------------

struct SoundCodeEvent
{
    int              mFamily {};
    std::vector<int> mParams;

    bool operator==(const SoundCodeEvent&) const = default;
};

struct ResetSignalEvent
{
    std::string_view mId;

    bool operator==(const ResetSignalEvent&) const = default;
};

struct SpellPhraseEvent
{
    std::string_view mId;
    std::string      mPhrase;

    bool operator==(const SpellPhraseEvent&) const = default;
};

// -- Wire tier ---------------------------------------------------------------------------------

struct CodeEvent
{
    std::string_view mId; // the matched c1-codes.toml id, or "" for none
    Header           mHeader;
    StackOp          mOp { StackOp::kPush };

    bool operator==(const CodeEvent&) const = default;
};

struct CloseEvent
{
    bool mIsPopped {};

    bool operator==(const CloseEvent&) const = default;
};

// A telnet command as received: IAC, the command and its option or subnegotiation bytes. A
// subnegotiation is IAC SB, its data with IAC IAC undone, then IAC SE.
struct TelnetEvent
{
    std::vector<std::byte> mBytes;

    bool operator==(const TelnetEvent&) const = default;
};

using Payload = std::variant<TextEvent,
                             LineEndEvent,
                             PromptEvent,
                             PartialEvent,
                             ClearScreenEvent,
                             EraseEolEvent,
                             EndOfSessionEvent,
                             WidthAckEvent,
                             BellEvent,
                             GameEnterEvent,
                             GameExitEvent,
                             FesStatsEvent,
                             CodeStatsEvent,
                             RoomNameEvent,
                             RoomLongEvent,
                             ExitsEvent,
                             InventoryEvent,
                             WhoEvent,
                             DreamwordEvent,
                             ClientModeEvent,
                             AccountChangeEvent,
                             PersonaUnsavedEvent,
                             TellerEvent,
                             SoundCodeEvent,
                             ResetSignalEvent,
                             SpellPhraseEvent,
                             CodeEvent,
                             CloseEvent,
                             TelnetEvent>;

// An event and its per-connection sequence number (docs/spec/architecture.md "Decoder API").
struct Event
{
    std::uint64_t mSeq {};
    Payload       mPayload;

    bool operator==(const Event&) const = default;
};

// True for the wire-tier events, which never appear in a golden stream.
[[nodiscard]] bool isWireTier(const Payload& payload) noexcept;
} // namespace muckaii::mud
