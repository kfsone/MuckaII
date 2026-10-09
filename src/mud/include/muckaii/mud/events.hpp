#pragma once

#include "muckaii/base/types.hpp"
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
using Colour = types::OptUint8;

// kMaxColourIndex is the highest colour index supported by the protocol.
constexpr std::uint8_t kMaxColourIndex { 15 };

// Style attributes as bits. Their order is the sorted order events.md writes them in.
// Attr names the style attributes that can be set on text.
enum class Attr : std::uint8_t
{
    kBlink     = 1U << 0U,
    kBold      = 1U << 1U,
    kItalic    = 1U << 2U,
    kReverse   = 1U << 3U,
    kUnderline = 1U << 4U,
};

// Style describes the colours and attributes applied to displayed text.
struct Style
{
    Colour       mFg;
    Colour       mBg;
    std::uint8_t mAttrs {};

    // hasAttr reports whether this style contains the requested attribute.
    [[nodiscard]] bool hasAttr(Attr attr) const noexcept
    {
        return (mAttrs & static_cast<std::uint8_t>(attr)) != 0U;
    }

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const Style&) const = default;
};

// Run stores adjacent text that shares one style.
struct Run
{
    std::string mText;
    Style       mStyle;

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const Run&) const = default;
};

// Player stores a persona name and its displayed colour.
struct Player
{
    std::string mName;
    Colour      mFg;

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const Player&) const = default;
};

// LineKind identifies the semantic kind of a completed line.
enum class LineKind : std::uint8_t
{
    kNormal,
    kChat,
    kFightStart,
    kFightEnd,
    kCreatureInvisible,
};

// RoomState describes whether room contents are listed, hidden by blindness, or hidden by darkness.
enum class RoomState : std::uint8_t
{
    kListed,
    kBlind,
    kDark,
};

// -- Text and lines ----------------------------------------------------------------------------

// TextEvent carries displayed text and its style.
struct TextEvent
{
    std::string mText;
    Style       mStyle;

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const TextEvent&) const = default;
};

// LineEndEvent marks a completed line and its semantic kind.
struct LineEndEvent
{
    LineKind mKind { LineKind::kNormal };
    bool     mIsChatContinued {};

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const LineEndEvent&) const = default;
};

// PromptEvent carries the runs that form a prompt.
struct PromptEvent
{
    std::vector<Run> mRuns;

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const PromptEvent&) const = default;
};

// PartialEvent carries unfinished text and whether it continues the prior partial.
struct PartialEvent
{
    std::vector<Run> mRuns;
    bool             mIsContinued {}; // no line end since the previous partial: extend it, else replace

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const PartialEvent&) const = default;
};

// ClearScreenEvent asks the frontend to clear its display.
struct ClearScreenEvent
{
    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const ClearScreenEvent&) const = default;
};

// EraseEolEvent asks the frontend to erase the current line to its end.
struct EraseEolEvent
{
    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const EraseEolEvent&) const = default;
};

// EndOfSessionEvent reports the server end-of-session escape.
struct EndOfSessionEvent
{
    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const EndOfSessionEvent&) const = default;
};

// WidthAckEvent reports the terminal width requested by the server.
struct WidthAckEvent
{
    int mColumns {};

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const WidthAckEvent&) const = default;
};

// BellEvent reports a server bell.
struct BellEvent
{
    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const BellEvent&) const = default;
};

// -- Mode --------------------------------------------------------------------------------------

// GameEnterEvent marks the transition into game mode.
struct GameEnterEvent
{
    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const GameEnterEvent&) const = default;
};

// GameExitEvent marks the transition out of game mode.
struct GameExitEvent
{
    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const GameExitEvent&) const = default;
};

// -- Readings ----------------------------------------------------------------------------------

// `stats` with `source` `fes`: one FES line (docs/spec/protocol.md "FES line").
// FesStatsEvent carries the values parsed from one FES line.
struct FesStatsEvent
{
    int            mStamina {};
    int            mMaxStamina {};
    int            mStrength {};
    int            mMaxStrength {};
    int            mDexterity {};
    int            mMaxDexterity {};
    int            mMagic {};
    int            mMaxMagic {};
    int            mScore {};
    bool           mIsBlind {};
    bool           mIsDeaf {};
    bool           mIsCrippled {};
    bool           mIsDumb {};
    int            mResetMinutes {};
    types::OptChar mWeather;
    Colour         mStaminaColour;

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const FesStatsEvent&) const = default;
};

// `stats` with `source` `code`: the 89 brackets of one displayed line.
// CodeStatsEvent carries readings collected from one displayed line.
struct CodeStatsEvent
{
    types::OptInt mStamina;
    types::OptInt mMaxStamina;
    types::OptInt mScore;

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const CodeStatsEvent&) const = default;
};

// RoomNameEvent carries the displayed room name.
struct RoomNameEvent
{
    std::string mText;

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const RoomNameEvent&) const = default;
};

// RoomLongEvent carries a displayed room-description line.
struct RoomLongEvent
{
    std::string mText;

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const RoomLongEvent&) const = default;
};

// ExitsEvent carries the exits parsed from a server reply.
struct ExitsEvent
{
    std::vector<std::string> mExits;

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const ExitsEvent&) const = default;
};

// InventoryEvent carries room and carried items with the room visibility state.
struct InventoryEvent
{
    RoomState                mRoomState { RoomState::kListed };
    std::vector<std::string> mRoom;
    std::vector<std::string> mCarried;

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const InventoryEvent&) const = default;
};

// WhoEvent carries the players parsed from a WHO reply.
struct WhoEvent
{
    std::vector<Player> mPlayers;

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const WhoEvent&) const = default;
};

// DreamwordEvent carries the current dreamword or its cleared state.
struct DreamwordEvent
{
    types::OptString mWord;

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const DreamwordEvent&) const = default;
};

// ClientModeEvent carries the account and privilege data from a client-mode block.
struct ClientModeEvent
{
    std::string mLicence;
    int         mMinLevel {};
    int         mMaxLevel {};
    std::string mAccount;
    int         mPrivs {};

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const ClientModeEvent&) const = default;
};

// AccountChangeEvent carries the account and privileges from an account-change block.
struct AccountChangeEvent
{
    std::string mAccount;
    int         mPrivs {};

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const AccountChangeEvent&) const = default;
};

// PersonaUnsavedEvent reports that the current persona is unsaved.
struct PersonaUnsavedEvent
{
    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const PersonaUnsavedEvent&) const = default;
};

// TellerEvent carries the persona who sent a tell.
struct TellerEvent
{
    std::string mName;

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const TellerEvent&) const = default;
};

// -- Code events (deferred to the next flush point) ---------------------------------------------

// SoundCodeEvent carries a sound family and its playback parameters.
struct SoundCodeEvent
{
    int              mFamily {};
    std::vector<int> mParams;

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const SoundCodeEvent&) const = default;
};

// ResetSignalEvent carries the id of a reset signal.
struct ResetSignalEvent
{
    std::string_view mId;

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const ResetSignalEvent&) const = default;
};

// SpellPhraseEvent carries a spell id and the phrase associated with it.
struct SpellPhraseEvent
{
    std::string_view mId;
    std::string      mPhrase;

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const SpellPhraseEvent&) const = default;
};

// -- Wire tier ---------------------------------------------------------------------------------

// CodeEvent carries a parsed C1 header and its stack operation.
struct CodeEvent
{
    std::string_view mId; // the matched c1-codes.toml id, or "" for none
    Header           mHeader;
    StackOp          mOp { StackOp::kPush };

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const CodeEvent&) const = default;
};

// CloseEvent reports whether a closing byte popped a context frame.
struct CloseEvent
{
    bool mIsPopped {};

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const CloseEvent&) const = default;
};

// A telnet command as received: IAC, the command and its option or subnegotiation bytes. A
// subnegotiation is IAC SB, its data with IAC IAC undone, then IAC SE.
// TelnetEvent carries one complete telnet command as received.
struct TelnetEvent
{
    types::Bytes mBytes;

    // operator== enables exact value comparisons in replay assertions.
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
// Event pairs a payload with its connection sequence number.
struct Event
{
    std::uint64_t mSeq {};
    Payload       mPayload;

    // operator== enables exact value comparisons in replay assertions.
    bool operator==(const Event&) const = default;
};

// True for the wire-tier events, which never appear in a golden stream.
// isWireTier reports whether a payload belongs to the optional wire tier.
[[nodiscard]] bool isWireTier(const Payload& payload) noexcept;
} // namespace muckaii::mud
