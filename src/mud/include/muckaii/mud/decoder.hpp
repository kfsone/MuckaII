#pragma once

#include "muckaii/base/types.hpp"
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
// DecoderOptions selects optional decoder event output.
struct DecoderOptions
{
    // Emit the wire-tier `code` and `close` events (docs/spec/events.md "Wire tier"). `telnet`
    // events are always emitted: the session layer answers them.
    bool mWillEmitWireTier {};
};

// Bytes in, docs/spec/events.md events out (docs/spec/architecture.md "Decoder API"). Synchronous,
// no clock, no I/O. Every state machine persists across feed() calls, so where a read is split
// into calls never changes the events; only endOfRead() marks a network read boundary.
// Decoder converts MUD2 wire bytes into ordered events while retaining stream state.
class Decoder
{
public:
    // Decoder starts a new connection with the requested event options.
    explicit Decoder(DecoderOptions options = {});

    // Decode `bytes` (raw wire bytes, telnet included), appending events to `out`.
    // feed decodes wire bytes and appends the resulting events in order.
    void feed(std::span<const std::byte> bytes, std::vector<Event>& out);

    // The end of one network read: flushes a pre-game partial line.
    // endOfRead marks a network-read boundary and emits any eligible partial text.
    void endOfRead(std::vector<Event>& out);

    // The connection closed: leaves game mode if in it (docs/spec/protocol.md "Game mode").
    // endOfConnection closes game mode and finalizes connection-scoped collection.
    void endOfConnection(std::vector<Event>& out);

    // isInGameMode reports whether the decoder has entered game mode.
    [[nodiscard]] bool isInGameMode() const noexcept
    {
        return mIsInGameMode;
    }

private:
    // TelnetState records progress through a telnet command.
    enum class TelnetState : std::uint8_t
    {
        kData,
        kIac,
        kOption,
        kSub,
        kSubIac,
    };

    // EscapeState records progress through a MUD2 or ANSI escape sequence.
    enum class EscapeState : std::uint8_t
    {
        kNone,
        kEsc,
        kEscDash,
        kEscDashDigits,
        kCsi,
    };

    // Reply identifies the structured reply currently being collected.
    enum class Reply : std::uint8_t
    {
        kNone,
        kFes,
        kExits,
        kInventory,
        kWho,
        kAccount,
    };

    // Frame stores one open C1 context and its derived collection state.
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

    // SavedStack preserves the context and base colours across a catch.
    struct SavedStack
    {
        std::vector<Frame> mFrames;
        Colour             mBaseFg;
        Colour             mBaseBg;
    };

    // Line accumulates text and semantic readings until a displayed line ends.
    struct Line
    {
        std::string    mText;
        bool           mHasPrintable {};
        bool           mHasPrompt {};
        bool           mIsRoomName {};
        bool           mIsRoomNameSeen {};
        bool           mIsRoomLong {};
        bool           mIsChat {};
        bool           mWasChatOpenAtStart {};
        LineKind       mLastKind { LineKind::kNormal };
        bool           mHasReadings {};
        types::OptInt  mStamina;
        types::OptInt  mMaxStamina;
        types::OptInt  mScore;
        bool           mHasTell {};
        types::OptSize mLabelEnd;
    };

    // ReplyState accumulates the fields and nested state of one structured reply.
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
    // onWireByte consumes a byte at the telnet layer.
    void onWireByte(std::uint8_t byte, std::vector<Event>& out);
    // emitTelnet publishes the complete telnet command collected so far.
    void emitTelnet(std::vector<Event>& out);
    // appendSubnegotiation collects subnegotiation data or abandons it at its ceiling.
    void appendSubnegotiation(std::uint8_t byte, std::vector<Event>& out);

    // Layer 2: the MUD2 stream.
    // onDataByte routes one non-telnet byte through the MUD2 stream parser.
    void onDataByte(std::uint8_t byte, std::vector<Event>& out);
    // onEscapeByte advances the active escape parser and reports whether it consumed the byte.
    bool onEscapeByte(std::uint8_t byte, std::vector<Event>& out);
    // onCsiFinal applies the supported action for one completed CSI sequence.
    void onCsiFinal(std::uint8_t final, std::vector<Event>& out);
    // resetAnsiStyle drops the SGR colours and attributes.
    void resetAnsiStyle() noexcept;
    // applySgr parses the active CSI parameters as style changes.
    void applySgr();
    // applySgrParameter updates ANSI colours and attributes for one SGR code.
    void applySgrParameter(int code);
    // onHeader dispatches a completed C1 header and updates the context stack.
    void onHeader(std::vector<Event>& out);
    // applyHeaderRole applies the event and collection effects of a code role.
    void applyHeaderRole(Role role, const CodeInfo* code, std::vector<Event>& out);
    // pushFrame opens a context frame with the colour and role state of a header.
    void pushFrame(const Header& header, const CodeInfo* code, Colour currentFg, Colour currentBg);
    // onClose closes the current frame and finalizes any collection it owns.
    void onClose(std::vector<Event>& out);
    // finishReading parses the active numeric reading into the current line.
    void finishReading();
    // onLineEnd finalizes a line or routes its boundary to an active reply.
    void onLineEnd(std::vector<Event>& out);
    // onText routes one text byte to active collectors and the display path.
    void onText(std::uint8_t byte, std::vector<Event>& out);
    // onDisplayedText appends a character to the displayed line with its current style.
    void onDisplayedText(char c, const Style& style, std::vector<Event>& out);
    // onClearScreen flushes pending text before publishing the clear event.
    void onClearScreen(std::vector<Event>& out);

    // Collectors that run alongside the text path.
    // endPhrase trims and closes the active spell phrase collector.
    void endPhrase();
    // endDreamword emits a collected dreamword when it is nonempty.
    void endDreamword(std::vector<Event>& out);
    // queueCodeEvents defers sound, reset, and spell events derived from a header.
    void queueCodeEvents(const Header& header, const CodeInfo* code);
    // startReply resets reply state for a newly opened structured reply.
    void startReply(Reply kind, const CodeInfo* code);
    // onReplyChar routes one character to the active structured reply.
    void onReplyChar(char c);
    // onReplyLineEnd records a reply line boundary and finishes eligible replies.
    void onReplyLineEnd(std::vector<Event>& out);
    // onAccountLine stores one account-block line and emits complete account records.
    void onAccountLine(std::vector<Event>& out);
    // finishFes validates collected fields and emits stats only when required values parse.
    void finishFes(std::vector<Event>& out);
    // finishReply emits the completed payload for the active reply collector.
    void finishReply(std::vector<Event>& out);
    // finishExits splits collected reply text into exit names.
    void finishExits(std::vector<Event>& out);
    // finishInventory separates room items from carried items and detects hidden rooms.
    void finishInventory(std::vector<Event>& out);
    // finishWhoName closes the current WHO name and appends the player.
    void finishWhoName();
    // finishPrompt emits the captured prompt or restores tentative text to the display path.
    void finishPrompt(bool isTentative, std::vector<Event>& out);

    // Emission.
    // emit flushes pending display text when needed before appending a payload.
    void emit(Payload payload, std::vector<Event>& out);
    // emitRaw appends a payload with the next sequence number.
    void emitRaw(Payload payload, std::vector<Event>& out);
    // flushPending emits accumulated display text without changing line state.
    void flushPending(std::vector<Event>& out);
    // flushDeferred emits deferred code events at the next stream boundary.
    void flushDeferred(std::vector<Event>& out);
    // endDisplayedLine emits line-associated events and resets line state.
    void endDisplayedLine(std::vector<Event>& out);
    // emitTeller recognizes and emits the sender from a tell line.
    void emitTeller(std::vector<Event>& out);
    // resetLine clears per-line state while preserving whether chat remains open.
    void resetLine();
    // enterGame publishes the transition into game mode.
    void enterGame(std::vector<Event>& out);
    // exitGame clears game-scoped state and publishes the transition out of game mode.
    void exitGame(bool willFlushPending, std::vector<Event>& out);

    // The reply a role's bracket holds; Reply::kNone for a role that holds none.
    // getReplyKind maps a code role to the reply collector it opens.
    [[nodiscard]] static Reply getReplyKind(Role role) noexcept;

    // The context stack.
    // getStyle combines the active C1 and ANSI style state.
    [[nodiscard]] Style getStyle() const noexcept;
    // hasFrame reports whether the context stack contains a frame with the given role.
    [[nodiscard]] bool hasFrame(Role role) const noexcept;
    // hasFrameWithId reports whether the context stack contains the given code id.
    [[nodiscard]] bool hasFrameWithId(std::string_view id) const noexcept;
    // isSuppressed reports whether a structured reply currently hides display text.
    [[nodiscard]] bool isSuppressed() const noexcept
    {
        return mReply.mKind != Reply::kNone;
    }

    // Ordered largest alignment first (clang-analyzer-optin.performance.Padding).
    std::uint64_t           mSeq {};
    types::Bytes            mTelnet;  // the telnet command being read
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
