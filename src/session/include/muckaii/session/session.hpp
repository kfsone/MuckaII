#pragma once

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
enum class Origin : std::uint8_t
{
    kTyped,
    kLogin,
    kPassword,
    kTelnet,
    kWidth,
};

// What the wire log masks in a send (docs/spec/persistence.md "Wire log").
enum class Mask : std::uint8_t
{
    kNone,
    kAccountId,
    kPassword,
    kPregameLine,
};

// True for a send the user originated (typed; macro, hotkey and raw-console sends when they exist).
// Outside game mode every such send is masked in the wire log (persistence.md "Wire log").
[[nodiscard]] bool isUserOrigin(Origin origin) noexcept;

// Overwrites a secret's bytes before clearing it. Best effort: copies made earlier (a reallocation,
// a moved-from buffer) are not reached.
void wipeSecret(std::string& secret) noexcept;
void wipeSecret(std::vector<std::byte>& secret) noexcept;

// One send: written to the socket in order, never reordered or merged with another.
struct Outbound
{
    std::vector<std::byte> mBytes;
    Origin                 mOrigin { Origin::kTyped };
    Mask                   mMask { Mask::kNone };
};

// The placeholder written over a masked send's bytes (persistence.md "Wire log").
constexpr std::byte kMaskByte { '*' };

// A send as the wire log stores it: every byte before the line's CR LF replaced, same length.
[[nodiscard]] std::vector<std::byte> getLoggedBytes(const Outbound& outbound);

// The annotation that follows a masked send; empty for Mask::kNone.
[[nodiscard]] std::string_view getMaskNote(Mask mask) noexcept;

enum class ConnectionState : std::uint8_t
{
    kConnecting,
    kConnected,
    kClosed,
};

// docs/spec/flow.md.
enum class FlowState : std::uint8_t
{
    kProfile,
    kPregame,
    kGame,
};

// Outside game mode every line the user types may be a credential, whatever prompt (if any) the
// server is showing: the wire log masks it and a frontend hides it (persistence.md "Wire log").
[[nodiscard]] constexpr bool isInputSensitive(FlowState state) noexcept
{
    return state != FlowState::kGame;
}

struct ConnectionEvent
{
    std::string     mReason; // why it closed; empty otherwise
    ConnectionState mState { ConnectionState::kConnecting };
};

struct FlowEvent
{
    FlowState mState { FlowState::kProfile };
};

// A client-side note for the terminal: a connect error, a wire-log failure.
struct NoticeEvent
{
    std::string mText;
};

using SessionEvent = std::variant<mud::Event, ConnectionEvent, FlowEvent, NoticeEvent>;

// What the login drive sends (docs/spec/connection.md 3.1). Never logged or shown; the account ID
// and password are wiped when a copy is destroyed.
struct Credentials
{
    ~Credentials();

    std::string mAccount;
    std::string mPassword;
    std::string mLoginName { "mud" };
};

// A credential MUD2 can receive: printable 7-bit only (root CLAUDE.md, "Bytes, not strings"). A
// CR, LF or high byte would end or corrupt the line it is sent in.
[[nodiscard]] bool isValidCredential(std::string_view text) noexcept;

// The column limits ever told to the server (connection.md 4).
constexpr int kMinColumns { 20 };
constexpr int kMaxColumns { 160 };
constexpr int kDefaultColumns { 80 };

// Effective columns (connection.md 4): unset max-columns gives min(80, fits); a set one gives
// min(setting, fits); then clamped to 20..160.
[[nodiscard]] int getEffectiveColumns(int fits, std::optional<int> maxColumns) noexcept;

// $-[ ESC ^F $-N/T{cols}$-] (connection.md 3.2), no CRLF.
[[nodiscard]] std::vector<std::byte> makeClientModeEntry(int columns);

// The bytes a line becomes: its text, every 255 doubled (telnet IAC IAC), then CR LF
// (connection.md 1).
[[nodiscard]] std::vector<std::byte> makeLine(std::string_view text);

class Session
{
public:
    // Credentials are copied (a copy is wiped when destroyed; there is no move that would leave the
    // source's bytes behind unwiped).
    Session(const Credentials& credentials, int columns);

    void onConnecting(std::vector<SessionEvent>& out);
    void onConnected(std::vector<SessionEvent>& out);

    // One socket read: decodes it, re-emits every mud event, answers telnet, drives the login.
    void onReceived(std::span<const std::byte> bytes, std::vector<SessionEvent>& out);

    void onClosed(std::string reason, std::vector<SessionEvent>& out);

    // A typed line that has passed the client's 7-bit filter. CR LF is added. It is masked in the
    // wire log when `isTaggedSensitive` (the verdict core gave the frontend at submit) or when the
    // flow is not Game now, named by the prompt when one is recognised.
    void queueLine(std::string_view text, bool isTaggedSensitive);

    // The effective column count changed (connection.md 4.1).
    void setColumns(int columns);

    [[nodiscard]] bool hasOutbound() const noexcept
    {
        return ! mOutbound.empty();
    }

    // The number of queued sends that came from typed lines.
    [[nodiscard]] std::size_t getTypedCount() const noexcept;

    // The next send, removed from the queue.
    [[nodiscard]] Outbound takeOutbound();

    // Notes for the wire log's annotation records (persistence.md, annotations).
    [[nodiscard]] std::vector<std::string> takeAnnotations();

    [[nodiscard]] FlowState getFlowState() const noexcept
    {
        return mFlowState;
    }

    [[nodiscard]] int getColumns() const noexcept
    {
        return mTelnet.getColumns();
    }

private:
    void handleEvent(const mud::Event& event, std::vector<SessionEvent>& out);
    void drivePrompts();
    void setFlow(FlowState state, std::vector<SessionEvent>& out);
    void queue(std::vector<std::byte> bytes, Origin origin, Mask mask = Mask::kNone);

    [[nodiscard]] Mask getUserMask() const noexcept;
    [[nodiscard]] bool isAccountPrompt() const noexcept;
    [[nodiscard]] bool isPasswordPrompt() const noexcept;
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
};
} // namespace muckaii::session
