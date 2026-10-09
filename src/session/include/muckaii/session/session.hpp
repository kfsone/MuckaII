#pragma once

#include "muckaii/base/types.hpp"
#include "muckaii/mud/decoder.hpp"
#include "muckaii/mud/events.hpp"
#include "muckaii/transport/telnet.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// The session layer (docs/spec/architecture.md "Layers"): login, flow state and the ONE ordered
// outbound queue. Every instance is touched by the core thread alone; there are no locks here.
namespace muckaii::session
{
// Who produced an outbound send (architecture.md "Input: two orderings", the origin table).
// Origin identifies which path produced an outbound send.
enum class Origin : std::uint8_t
{
    kTyped,
    kLogin,
    kPassword,
    kTelnet,
    kWidth,
};

// What the wire log masks in a send (docs/spec/persistence.md "Wire log").
// Mask identifies which sensitive bytes the wire log hides.
enum class Mask : std::uint8_t
{
    kNone,
    kAccountId,
    kPassword,
    kBeforeLogin,
};

// isUserOrigin reports whether a send originated from the user.
// True for a send the user originated (typed; macro, hotkey and raw-console sends when they exist).
// Such a send is masked in the wire log before login completes, and when it answers a recognised
// account or password prompt (persistence.md "Wire log").
[[nodiscard]] bool isUserOrigin(Origin origin) noexcept;

// wipeSecret overwrites a string secret before clearing it; earlier copies are not reached.
void wipeSecret(std::string& secret) noexcept;

// wipeSecret overwrites a byte-buffer secret before clearing it; earlier copies are not reached.
void wipeSecret(types::Bytes& secret) noexcept;

// One send: written to the socket in order, never reordered or merged with another.
// Outbound carries one ordered send and its wire-log masking policy.
struct Outbound
{
    types::Bytes mBytes;
    Origin       mOrigin { Origin::kTyped };
    Mask         mMask { Mask::kNone };
};

// The placeholder written over a masked send's bytes (persistence.md "Wire log").
// kMaskByte replaces each masked byte in the wire log.
constexpr std::byte kMaskByte { '*' };

// A send as the wire log stores it: every byte before the line's CR LF replaced, same length.
// getLoggedBytes returns the outbound bytes with any sensitive line replaced.
[[nodiscard]] types::Bytes getLoggedBytes(const Outbound& outbound);

// The annotation that follows a masked send; empty for Mask::kNone.
// getMaskNote returns the annotation for a masking policy.
[[nodiscard]] std::string_view getMaskNote(Mask mask) noexcept;

// ConnectionState describes whether the socket is connecting, connected, or closed.
enum class ConnectionState : std::uint8_t
{
    kConnecting,
    kConnected,
    kClosed,
};

// docs/spec/flow.md.
// FlowState describes whether the client is at the profile, pregame, or game screen.
enum class FlowState : std::uint8_t
{
    kProfile,
    kPregame,
    kGame,
};

// ConnectionEvent reports a socket connection state and its close reason.
struct ConnectionEvent
{
    std::string     mReason; // why it closed; empty otherwise
    ConnectionState mState { ConnectionState::kConnecting };
};

// FlowEvent reports a change in the client game flow.
struct FlowEvent
{
    FlowState mState { FlowState::kProfile };
};

// A client-side note for the terminal: a connect error, a wire-log failure.
// NoticeEvent carries a client-side message for the terminal.
struct NoticeEvent
{
    std::string mText;
};

// SessionEvent is a mud event or a session-level connection, flow, or notice event.
using SessionEvent = std::variant<mud::Event, ConnectionEvent, FlowEvent, NoticeEvent>;

// What the login drive sends (docs/spec/connection.md 3.1). Never logged or shown; the account ID
// and password are wiped when a copy is destroyed.
// kDefaultLoginName is the shell login name used when no profile value is supplied.
constexpr std::string_view kDefaultLoginName { "mud" };

// Credentials holds login values and wipes the secret values when destroyed.
struct Credentials
{
    // ~Credentials wipes the account ID and password before destruction.
    ~Credentials();

    std::string mAccount;
    std::string mPassword;
    std::string mLoginName { kDefaultLoginName };
};

// A credential MUD2 can receive: printable 7-bit only (root CLAUDE.md, "Bytes, not strings"). A
// CR, LF or high byte would end or corrupt the line it is sent in.
// isValidCredential reports whether a credential is printable 7-bit text.
[[nodiscard]] bool isValidCredential(std::string_view text) noexcept;

// The column limits ever told to the server (connection.md 4).
// kMinColumns is the narrowest column count sent to the server.
constexpr int kMinColumns { 20 };
// kMaxColumns is the widest column count sent to the server.
constexpr int kMaxColumns { 160 };
// kDefaultColumns is the width used when no maximum is configured.
constexpr int kDefaultColumns { 80 };

// kClientWrapColumns is the Terminal Width that asks for client-side wrap (connection.md 4.2).
constexpr int kClientWrapColumns { 0 };

// isClientWrapWidth reports whether maxColumns asks for client-side wrap.
[[nodiscard]] constexpr bool isClientWrapWidth(types::OptInt maxColumns) noexcept
{
    return maxColumns == kClientWrapColumns;
}

// Effective columns (connection.md 4): unset max-columns gives min(80, fits); 0 gives min(160,
// fits) (no ceiling but the usual maximum); any other value gives min(setting, fits); then clamped
// to 20..160.
// getEffectiveColumns clamps the chosen width to the terminal and server limits.
[[nodiscard]] int getEffectiveColumns(int fits, types::OptInt maxColumns) noexcept;

// $-[ ESC ^F $-N/T{cols}$-] (connection.md 3.2), no CRLF.
// makeClientModeEntry builds the client-mode request for the given width.
[[nodiscard]] types::Bytes makeClientModeEntry(int columns);

// The bytes a line becomes: its text, every 255 doubled (telnet IAC IAC), then CR LF
// (connection.md 1).
// makeLine turns text into a telnet-safe line terminated by CR LF.
[[nodiscard]] types::Bytes makeLine(std::string_view text);

// Session drives login and flow state while preserving one ordered outbound queue.
class Session
{
public:
    // Credentials are copied (a copy is wiped when destroyed; there is no move that would leave the
    // source's bytes behind unwiped). `isClientWrap` is the profile's terminal width of 0
    // (connection.md 4, "client-side wrap"): `/T` is never sent, and `/F+` is sent once, right
    // after client-mode entry, so the server does not wrap and MuckaII wraps for display instead.
    // Session copies credentials, configures width negotiation, and optionally enables client wrapping.
    Session(const Credentials& credentials, int columns, bool isClientWrap = false);

    // onConnecting emits the initial connecting state.
    void onConnecting(std::vector<SessionEvent>& out);
    // onConnected emits the connected state and enters pregame flow.
    void onConnected(std::vector<SessionEvent>& out);

    // One socket read: decodes it, re-emits every mud event, answers telnet, drives the login.
    // onReceived decodes one socket read, emits its events, and drives login prompts.
    void onReceived(std::span<const std::byte> bytes, std::vector<SessionEvent>& out);

    // onClosed flushes decoder state, reports closure, and resets the telnet session.
    void onClosed(std::string reason, std::vector<SessionEvent>& out);

    // A typed line that has passed the client's 7-bit filter. CR LF is added. It is masked in the
    // wire log when `isTaggedSensitive` (the verdict core gave the frontend at submit) or when
    // isLineSensitive() holds now, named by the prompt when one is recognised.
    // queueLine queues a filtered typed line and applies the frontend sensitivity verdict.
    void queueLine(std::string_view text, bool isTaggedSensitive);

    // Whether a user line sent now is masked (persistence.md "Wire log"): before login completes
    // (the first Option menu), or while a recognised account or password prompt is live.
    // isLineSensitive reports whether a user line must be masked in the wire log.
    [[nodiscard]] bool isLineSensitive() const noexcept;

    // The effective column count changed (connection.md 4.1).
    // setColumns updates telnet negotiation and server width for a changed terminal width.
    void setColumns(int columns);

    // hasOutbound reports whether a send is waiting in the ordered queue.
    [[nodiscard]] bool hasOutbound() const noexcept
    {
        return ! mOutbound.empty();
    }

    // The number of queued sends that came from typed lines.
    // getTypedCount returns the number of queued typed sends.
    [[nodiscard]] std::size_t getTypedCount() const noexcept;

    // The next send, removed from the queue.
    // takeOutbound removes and returns the next ordered send.
    [[nodiscard]] Outbound takeOutbound();

    // Notes for the wire log's annotation records (persistence.md, annotations).
    // takeAnnotations removes and returns pending wire-log annotations.
    [[nodiscard]] std::vector<std::string> takeAnnotations();

    // getFlowState returns the current profile, pregame, or game flow.
    [[nodiscard]] FlowState getFlowState() const noexcept
    {
        return mFlowState;
    }

    // getColumns returns the effective width used by telnet negotiation.
    [[nodiscard]] int getColumns() const noexcept
    {
        return mTelnet.getColumns();
    }

private:
    // handleEvent updates session state and queues replies for one decoded event.
    void handleEvent(const mud::Event& event, std::vector<SessionEvent>& out);
    // drivePrompts recognizes login prompts and queues the next required response.
    void drivePrompts();
    // setFlow emits a flow event only when the state changes.
    void setFlow(FlowState state, std::vector<SessionEvent>& out);
    // queue appends a send and applies wire-log masking when required.
    void queue(types::Bytes bytes, Origin origin, Mask mask = Mask::kNone);

    // getUserMask selects the mask for the currently recognized prompt.
    [[nodiscard]] Mask getUserMask() const noexcept;
    // isAccountPrompt reports whether the live line is an account prompt.
    [[nodiscard]] bool isAccountPrompt() const noexcept;
    // isPasswordPrompt reports whether the live line is a recognized password prompt.
    [[nodiscard]] bool isPasswordPrompt() const noexcept;
    // isOptionPrompt reports whether the live line is an option menu prompt.
    [[nodiscard]] bool isOptionPrompt() const noexcept;

    Credentials                 mCredentials;
    mud::Decoder                mDecoder;
    transport::TelnetNegotiator mTelnet;
    std::deque<Outbound>        mOutbound;
    std::vector<std::string>    mAnnotations;
    std::vector<mud::Event>     mBatch;
    std::string                 mLine; // the live line's text since the last line end
    FlowState                   mFlowState { FlowState::kProfile };
    bool                        mHasSentLoginName {};
    bool                        mHasSentAccount {};
    bool                        mHasSentPassword {};
    bool                        mHasEnteredClientMode {};
    bool                        mIsLoginComplete {}; // the first Option menu has been seen
    bool                        mIsClientWrap {};    // profile terminal width 0: no server wrap
};
} // namespace muckaii::session
