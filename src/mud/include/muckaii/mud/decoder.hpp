#pragma once

#include "muckaii/mud/codes.hpp"
#include "muckaii/mud/events.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace muckaii::mud
{
struct DecoderOptions
{
    // Emit the wire-tier `code` and `close` events (docs/spec/events.md "Wire tier"). `telnet`
    // events are always emitted: the session layer answers them.
    bool mWillEmitWireTier {};
};

// Bytes in, docs/spec/events.md events out (docs/spec/architecture.md "Decoder API"). Synchronous,
// no clock, no I/O. Every state machine persists across feed() calls, so where a read is split
// into calls never changes the events; only endOfRead() marks a network read boundary.
class Decoder
{
public:
    explicit Decoder(DecoderOptions options = {});

    // Decode `bytes` (raw wire bytes, telnet included), appending events to `out`.
    void feed(std::span<const std::byte> bytes, std::vector<Event>& out);

    // The end of one network read: flushes a pre-game partial line.
    void endOfRead(std::vector<Event>& out);

    // The connection closed: leaves game mode if in it (docs/spec/protocol.md "Game mode").
    void endOfConnection(std::vector<Event>& out);

    [[nodiscard]] bool isInGameMode() const noexcept
    {
        return mIsInGameMode;
    }

private:
    enum class TelnetState : std::uint8_t
    {
        kData,
        kIac,
        kOption,
        kSub,
        kSubIac,
    };

    enum class EscapeState : std::uint8_t
    {
        kNone,
        kEsc,
        kEscDash,
        kEscDashDigits,
        kCsi,
    };

    enum class Reply : std::uint8_t
    {
        kNone,
        kFes,
        kExits,
        kInventory,
        kWho,
        kAccount,
    };

    struct Frame
    {
        const CodeInfo* mCode {};
        Header          mHeader;
        Colour          mFg;
        Colour          mBg;
        bool            mIsPromptOwner {};
        bool            mIsReading {};
        bool            mIsReplyRoot {};
        bool            mIsWhoName {};
    };

    struct SavedStack
    {
        std::vector<Frame> mFrames;
        Colour             mBaseFg;
        Colour             mBaseBg;
    };

    struct Line
    {
        std::string                mText;
        bool                       mHasPrintable {};
        bool                       mHasPrompt {};
        bool                       mIsRoomName {};
        bool                       mIsRoomNameSeen {};
        bool                       mIsRoomLong {};
        bool                       mIsChat {};
        bool                       mWasChatOpenAtStart {};
        LineKind                   mLastKind { LineKind::kNormal };
        bool                       mHasReadings {};
        std::optional<int>         mStamina;
        std::optional<int>         mMaxStamina;
        std::optional<int>         mScore;
        bool                       mHasTell {};
        std::optional<std::size_t> mLabelEnd;
    };

    struct ReplyState
    {
        Reply                    mKind { Reply::kNone };
        const CodeInfo*          mCode {};
        std::string              mCurrent;
        std::vector<std::string> mLines;
        // FES
        std::vector<std::string> mFields;
        bool                     mIsFesDone {};
        bool                     mIsAwaitingWeather {};
        Colour                   mStaminaColour;
        bool                     mHasStaminaColour {};
        // WHO
        std::vector<Player> mPlayers;
        bool                mIsNameOpen {};
        bool                mIsNameEnded {};
        std::string         mName;
        Colour              mNameFg;
    };

    // Layer 1: telnet.
    void onWireByte(std::uint8_t byte, std::vector<Event>& out);
    void emitTelnet(std::vector<Event>& out);
    void appendSubnegotiation(std::uint8_t byte, std::vector<Event>& out);

    // Layer 2: the MUD2 stream.
    void onDataByte(std::uint8_t byte, std::vector<Event>& out);
    bool onEscapeByte(std::uint8_t byte, std::vector<Event>& out);
    void onCsiFinal(std::uint8_t final, std::vector<Event>& out);
    void applySgr();
    void applySgrParameter(int code);
    void onHeader(std::vector<Event>& out);
    void applyHeaderRole(Role role, const CodeInfo* code, std::vector<Event>& out);
    void pushFrame(const Header& header, const CodeInfo* code, Colour currentFg, Colour currentBg);
    void onClose(std::vector<Event>& out);
    void finishReading();
    void onLineEnd(std::vector<Event>& out);
    void onText(std::uint8_t byte, std::vector<Event>& out);
    void onDisplayedText(char c, const Style& style, std::vector<Event>& out);
    void onClearScreen(std::vector<Event>& out);

    // Collectors that run alongside the text path.
    void endPhrase();
    void endDreamword(std::vector<Event>& out);
    void queueCodeEvents(const Header& header, const CodeInfo* code);
    void startReply(Reply kind, const CodeInfo* code);
    void onReplyChar(char c);
    void onReplyLineEnd(std::vector<Event>& out);
    void onAccountLine(std::vector<Event>& out);
    void finishFes(std::vector<Event>& out);
    void finishReply(std::vector<Event>& out);
    void finishExits(std::vector<Event>& out);
    void finishInventory(std::vector<Event>& out);
    void finishWhoName();
    void finishPrompt(bool isTentative, std::vector<Event>& out);

    // Emission.
    void emit(Payload payload, std::vector<Event>& out);
    void emitRaw(Payload payload, std::vector<Event>& out);
    void flushPending(std::vector<Event>& out);
    void flushDeferred(std::vector<Event>& out);
    void endDisplayedLine(std::vector<Event>& out);
    void emitTeller(std::vector<Event>& out);
    void resetLine();
    void enterGame(std::vector<Event>& out);
    void exitGame(bool willFlushPending, std::vector<Event>& out);

    // The reply a role's bracket holds; Reply::kNone for a role that holds none.
    [[nodiscard]] static Reply getReplyKind(Role role) noexcept;

    // The context stack.
    [[nodiscard]] Style getStyle() const noexcept;
    [[nodiscard]] bool hasFrame(Role role) const noexcept;
    [[nodiscard]] bool hasFrameWithId(std::string_view id) const noexcept;
    [[nodiscard]] bool isSuppressed() const noexcept
    {
        return mReply.mKind != Reply::kNone;
    }

    // Ordered largest alignment first (clang-analyzer-optin.performance.Padding).
    std::uint64_t           mSeq {};
    std::vector<std::byte>  mTelnet;  // the telnet command being read
    std::vector<Frame>      mStack;   // the context stack
    std::vector<SavedStack> mSaved;   // stacks saved by catches
    std::vector<Run>        mPending; // displayed text not yet written
    std::vector<Run>        mCapture; // the prompt bracket's text
    std::vector<Payload>    mDeferred;
    std::string             mCsi;
    std::string             mDreamword;
    std::string             mReading; // the open 89 bracket's text
    Line                    mLine;
    ReplyState              mReply;
    int                     mEscapeNumber {};
    DecoderOptions          mOptions;
    TelnetState             mTelnetState { TelnetState::kData };
    bool                    mIsInHeader {};
    EscapeState             mEscape { EscapeState::kNone };
    bool                    mIsSwallowingWidth {};
    std::uint8_t            mAnsiAttrs {};
    bool                    mIsEscReverse {};
    bool                    mIsInGameMode {};
    bool                    mIsGateOpen { true };
    bool                    mIsCapturing {};
    bool                    mIsCaptureTentative {};
    bool                    mIsCollectingPhrase {};
    bool                    mIsCollectingDreamword {};
    bool                    mHasPartial {};             // a partial has been emitted this connection
    bool                    mHasLineEndSincePartial {}; // a line end, shown or not, since the last partial
    Role                    mReadingRole { Role::kNone };
    Colour                  mBaseFg;
    Colour                  mBaseBg;
    Colour                  mAnsiFg;
    Colour                  mAnsiBg;
    Header                  mHeader; // the header being read
};
} // namespace muckaii::mud
