#include "muckaii/session/session.hpp"

#include "muckaii/base/types.hpp"
#include "muckaii/base/wire.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace muckaii::session
{
namespace
{
// Ctrl+F, the byte that asks the shell to enter client mode (connection.md 3.2).
// kCtrlF is the client-mode control byte sent after ESC.
constexpr std::uint8_t kCtrlF { 0x06 };

// The bracketing escapes of a client-mode request: ESC '-' <final> (connection.md 3.2, written
// `$-[ ... $-]`), and the final that separates the entry from the width command.
constexpr char kClientIntroducer { '-' };
constexpr char kClientBegin { '[' };
constexpr char kClientNext { 'N' };
constexpr char kClientEnd { ']' };

// The width command the server takes inside a client-mode request: /T<columns>.
// kWidthCommand is the server command prefix that sets terminal width.
constexpr std::string_view kWidthCommand { "/T" };
// kClientWrapCommand disables server wrapping after client mode starts.
constexpr std::string_view kClientWrapCommand { "/F+" };

// kLineEnd terminates each text line sent to MUD2.
constexpr std::string_view kLineEnd { "\r\n" };

// Clio's anchored literals (connection.md 3.1, default: line). "login: " matches anywhere; the
// others only at the start of a line.
// kShellLogin is the shell prompt text that receives the configured login name.
constexpr std::string_view kShellLogin { "login: " };
// kAccountPrompt begins the recognized account ID prompt.
constexpr std::string_view kAccountPrompt { "Account ID: " };
// kPasswordPrompts are the recognized password prompt prefixes.
constexpr std::array<std::string_view, 2> kPasswordPrompts { "Password:", "password:" };
// kOptionPrompts are the recognized option menu prefixes.
constexpr std::array<std::string_view, 2> kOptionPrompts { "Option: ", "Option (H for help): " };
// kAccountMaskNote is the wire-log annotation for a masked account ID.
constexpr std::string_view kAccountMaskNote { "masked: account id" };
// kPasswordMaskNote is the wire-log annotation for a masked password.
constexpr std::string_view kPasswordMaskNote { "masked: password" };
// kBeforeLoginMaskNote is the wire-log annotation for a line sent before login.
constexpr std::string_view kBeforeLoginMaskNote { "masked: line before login" };
// kWidthConfirmedNote starts the annotation for a confirmed terminal width.
constexpr std::string_view kWidthConfirmedNote { "terminal width confirmed: " };

// appendText appends `text`'s characters to `out` as bytes.
void appendText(types::Bytes& out, std::string_view text)
{
    for (const char c : text)
        out.push_back(std::byte { static_cast<std::uint8_t>(c) });
}

// appendEscape appends the client escape ESC '-' `final`.
void appendEscape(types::Bytes& out, char final)
{
    out.push_back(std::byte { wire::kEsc });
    out.push_back(std::byte { static_cast<std::uint8_t>(kClientIntroducer) });
    out.push_back(std::byte { static_cast<std::uint8_t>(final) });
}

// appendWidthCommand appends "/T<columns>".
void appendWidthCommand(types::Bytes& out, int columns)
{
    appendText(out, kWidthCommand);
    appendText(out, std::to_string(columns));
}

// appendWidth appends a bracketed width request, which sets the width once client mode is on.
void appendWidth(types::Bytes& out, int columns)
{
    appendEscape(out, kClientBegin);
    appendWidthCommand(out, columns);
    appendEscape(out, kClientEnd);
}

// isPrintableChar reports whether `c` is a printable 7-bit character.
bool isPrintableChar(char c) noexcept
{
    return wire::isPrintable(static_cast<std::uint8_t>(c));
}

// getColumnsCeiling returns the most columns `maxColumns` allows before the terminal's own width
// is considered: unset takes the default, and client-side wrap takes the usual maximum.
// getColumnsCeiling selects the configured or default upper width limit.
// startsWithPrompt reports whether a live line begins with one of the recognized prompts.
template <std::size_t Size>
bool startsWithPrompt(std::string_view line, const std::array<std::string_view, Size>& prompts) noexcept
{
    for (const std::string_view prompt : prompts)
    {
        if (line.starts_with(prompt))
            return true;
    }
    return false;
}

int getColumnsCeiling(types::OptInt maxColumns) noexcept
{
    if (! maxColumns.has_value())
        return kDefaultColumns;
    if (isClientWrapWidth(maxColumns))
        return kMaxColumns;
    return *maxColumns;
}
} // namespace

types::Bytes getLoggedBytes(const Outbound& outbound)
{
    types::Bytes bytes { outbound.mBytes };
    if (outbound.mMask == Mask::kNone)
        return bytes;
    std::size_t end { bytes.size() };
    if (end >= 2 && bytes[end - 2] == std::byte { '\r' } && bytes[end - 1] == std::byte { '\n' })
        end -= 2;
    std::fill_n(bytes.begin(), static_cast<std::ptrdiff_t>(end), kMaskByte);
    return bytes;
}

std::string_view getMaskNote(Mask mask) noexcept
{
    switch (mask)
    {
        case Mask::kNone:
            return {};
        case Mask::kAccountId:
            return kAccountMaskNote;
        case Mask::kPassword:
            return kPasswordMaskNote;
        case Mask::kBeforeLogin:
            return kBeforeLoginMaskNote;
    }
    return {};
}

bool isValidCredential(std::string_view text) noexcept
{
    return std::ranges::all_of(text, isPrintableChar);
}

bool isUserOrigin(Origin origin) noexcept
{
    // No default, so clang's -Wswitch (-Wall -Werror) fails the build until a new origin is
    // classified here. An unclassified value counts as the user's: masked is the safe side.
    switch (origin)
    {
        case Origin::kTyped:
            return true;
        case Origin::kLogin:
        case Origin::kPassword:
        case Origin::kTelnet:
        case Origin::kWidth:
            return false;
    }
    return true;
}

void wipeSecret(std::string& secret) noexcept
{
    // Through a volatile pointer, so the writes are not removed as dead stores.
    volatile char* const bytes { secret.data() };
    for (std::size_t i { 0 }; i < secret.size(); ++i)
        bytes[i] = '\0';
    secret.clear();
}

void wipeSecret(types::Bytes& secret) noexcept
{
    volatile std::byte* const bytes { secret.data() };
    for (std::size_t i { 0 }; i < secret.size(); ++i)
        bytes[i] = std::byte { 0 };
    secret.clear();
}

Credentials::~Credentials()
{
    wipeSecret(mAccount);
    wipeSecret(mPassword);
}

int getEffectiveColumns(int fits, types::OptInt maxColumns) noexcept
{
    const int ceiling { getColumnsCeiling(maxColumns) };
    return std::clamp(std::min(ceiling, fits), kMinColumns, kMaxColumns);
}

types::Bytes makeClientModeEntry(int columns)
{
    types::Bytes out;
    appendEscape(out, kClientBegin);
    out.push_back(std::byte { wire::kEsc });
    out.push_back(std::byte { kCtrlF });
    appendEscape(out, kClientNext);
    appendWidthCommand(out, columns);
    appendEscape(out, kClientEnd);
    return out;
}

types::Bytes makeLine(std::string_view text)
{
    types::Bytes out;
    out.reserve(text.size() + 2);
    for (const char c : text)
    {
        const std::byte byte { static_cast<std::uint8_t>(c) };
        out.push_back(byte);
        if (byte == std::byte { wire::kTelnetIac })
            out.push_back(byte);
    }
    appendText(out, kLineEnd);
    return out;
}

Session::Session(const Credentials& credentials, int columns, bool isClientWrap)
    : mCredentials { credentials }
    // NEW-ENVIRON only advertises a login name MuckaII would itself send at the shell `login:`
    // prompt (connection.md 2.1/3.1): the same validity check as drivePrompts() below.
    , mTelnet { columns,
                isValidCredential(mCredentials.mLoginName) ? mCredentials.mLoginName : std::string {} }
    , mIsClientWrap { isClientWrap }
{
}

void Session::onConnecting(std::vector<SessionEvent>& out)
{
    out.emplace_back(ConnectionEvent { .mReason = {}, .mState = ConnectionState::kConnecting });
}

void Session::onConnected(std::vector<SessionEvent>& out)
{
    out.emplace_back(ConnectionEvent { .mReason = {}, .mState = ConnectionState::kConnected });
    setFlow(FlowState::kPregame, out);
}

void Session::onReceived(std::span<const std::byte> bytes, std::vector<SessionEvent>& out)
{
    mBatch.clear();
    mDecoder.feed(bytes, mBatch);
    mDecoder.endOfRead(mBatch);
    for (mud::Event& event : mBatch)
    {
        handleEvent(event, out);
        out.emplace_back(std::move(event));
    }
    mBatch.clear();
    drivePrompts();
}

void Session::onClosed(std::string reason, std::vector<SessionEvent>& out)
{
    mBatch.clear();
    mDecoder.endOfConnection(mBatch);
    for (mud::Event& event : mBatch)
    {
        handleEvent(event, out);
        out.emplace_back(std::move(event));
    }
    mBatch.clear();
    out.emplace_back(ConnectionEvent { .mReason = std::move(reason), .mState = ConnectionState::kClosed });
    setFlow(FlowState::kProfile, out);
    mTelnet.reset();
}

void Session::queueLine(std::string_view text, bool isTaggedSensitive)
{
    queue(makeLine(text), Origin::kTyped, isTaggedSensitive ? getUserMask() : Mask::kNone);
}

void Session::setColumns(int columns)
{
    if (columns == mTelnet.getColumns())
        return;
    if (types::Bytes naws { mTelnet.setColumns(columns) }; ! naws.empty())
        queue(std::move(naws), Origin::kTelnet);
    // `/F+` already told the server not to wrap at all; resending `/T` would turn wrapping back on.
    if (mFlowState == FlowState::kGame && ! mIsClientWrap)
    {
        types::Bytes width;
        appendWidth(width, columns);
        queue(std::move(width), Origin::kWidth);
    }
}

std::size_t Session::getTypedCount() const noexcept
{
    std::size_t count {};
    for (const Outbound& outbound : mOutbound)
    {
        if (outbound.mOrigin == Origin::kTyped)
            ++count;
    }
    return count;
}

Outbound Session::takeOutbound()
{
    Outbound next { std::move(mOutbound.front()) };
    mOutbound.pop_front();
    return next;
}

std::vector<std::string> Session::takeAnnotations()
{
    return std::exchange(mAnnotations, {});
}

void Session::handleEvent(const mud::Event& event, std::vector<SessionEvent>& out)
{
    std::visit(
        [this, &out](const auto& payload)
        {
            using T = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<T, mud::TelnetEvent>)
            {
                if (types::Bytes reply { mTelnet.onCommand(payload.mBytes) }; ! reply.empty())
                    queue(std::move(reply), Origin::kTelnet);
            }
            else if constexpr (std::is_same_v<T, mud::TextEvent>)
            {
                mLine.append(payload.mText);
            }
            else if constexpr (std::is_same_v<T, mud::PartialEvent>)
            {
                // A partial appends to the live partial when no line end came between them
                // (protocol.md "Line model", default: line), so a split prompt matches whole;
                // otherwise it replaces it.
                if (! payload.mIsContinued)
                    mLine.clear();
                for (const mud::Run& run : payload.mRuns)
                    mLine.append(run.mText);
            }
            else if constexpr (std::is_same_v<T, mud::LineEndEvent>)
            {
                mLine.clear();
            }
            else if constexpr (std::is_same_v<T, mud::WidthAckEvent>)
            {
                const std::string width { std::to_string(payload.mColumns) };
                const std::string note { std::string { kWidthConfirmedNote } + width };
                mAnnotations.push_back(note);
            }
            else if constexpr (std::is_same_v<T, mud::GameEnterEvent>)
            {
                mLine.clear();
                mIsLoginComplete = true;
                setFlow(FlowState::kGame, out);
            }
            else if constexpr (std::is_same_v<T, mud::GameExitEvent>)
            {
                setFlow(FlowState::kPregame, out);
            }
        },
        event.mPayload);
}

void Session::drivePrompts()
{
    if (mFlowState == FlowState::kGame)
        return;
    // Login is complete at the first Option menu: the account and password are behind it.
    if (isOptionPrompt())
        mIsLoginComplete = true;

    const bool hasLoginName { ! mCredentials.mLoginName.empty() };
    const bool isLoginNameValid { isValidCredential(mCredentials.mLoginName) };
    if (hasLoginName && isLoginNameValid && ! mHasSentLoginName
        && mLine.find(kShellLogin) != std::string::npos)
    {
        mHasSentLoginName = true;
        queue(makeLine(mCredentials.mLoginName), Origin::kLogin);
        return;
    }

    // Auto-login runs only with both an account ID and a password (connection.md 3.1), and never
    // with one MUD2 cannot receive.
    const bool isAutoLogin { ! mCredentials.mAccount.empty() && ! mCredentials.mPassword.empty()
                             && isValidCredential(mCredentials.mAccount)
                             && isValidCredential(mCredentials.mPassword) };
    if (isAutoLogin && ! mHasSentAccount && isAccountPrompt())
    {
        mHasSentAccount = true;
        queue(makeLine(mCredentials.mAccount), Origin::kLogin, Mask::kAccountId);
        return;
    }
    // The password goes only after the account ID went (Clio's ordering guard). Once queued it is
    // needed no more, so this copy is wiped (the queued send is wiped when written, by core).
    if (isAutoLogin && mHasSentAccount && ! mHasSentPassword && isPasswordPrompt())
    {
        mHasSentPassword = true;
        queue(makeLine(mCredentials.mPassword), Origin::kPassword, Mask::kPassword);
        wipeSecret(mCredentials.mPassword);
        return;
    }
    if (! mHasEnteredClientMode && isOptionPrompt())
    {
        // NAWS is resent immediately before client-mode entry, once per login (connection.md 3.2).
        mHasEnteredClientMode = true;
        if (types::OptBytes naws { mTelnet.findNawsForClientMode() }; naws.has_value())
            queue(std::move(*naws), Origin::kTelnet);
        queue(makeClientModeEntry(mTelnet.getColumns()), Origin::kLogin);
        // A terminal width of 0 means "don't wrap at all; MuckaII wraps for display" (connection.md
        // 4, DECISION, source: director): `/F+` is a typed line at the Option menu, same as any
        // other option command, sent once right after client-mode entry.
        if (mIsClientWrap)
            queue(makeLine(kClientWrapCommand), Origin::kLogin);
    }
}

void Session::setFlow(FlowState state, std::vector<SessionEvent>& out)
{
    if (state == mFlowState)
        return;
    mFlowState = state;
    out.emplace_back(FlowEvent { .mState = state });
}

void Session::queue(types::Bytes bytes, Origin origin, Mask mask)
{
    // The one place every send passes: a sensitive user-originated send is masked, whatever path
    // queued it (persistence.md "Wire log").
    if (mask == Mask::kNone && isUserOrigin(origin) && isLineSensitive())
        mask = getUserMask();
    mOutbound.push_back(Outbound { .mBytes = std::move(bytes), .mOrigin = origin, .mMask = mask });
}

bool Session::isLineSensitive() const noexcept
{
    return ! mIsLoginComplete || isAccountPrompt() || isPasswordPrompt();
}

Mask Session::getUserMask() const noexcept
{
    if (isAccountPrompt())
        return Mask::kAccountId;
    if (isPasswordPrompt())
        return Mask::kPassword;
    return Mask::kBeforeLogin;
}

bool Session::isAccountPrompt() const noexcept
{
    return mFlowState != FlowState::kGame && mLine.starts_with(kAccountPrompt);
}

bool Session::isPasswordPrompt() const noexcept
{
    return mFlowState != FlowState::kGame && startsWithPrompt(mLine, kPasswordPrompts);
}

bool Session::isOptionPrompt() const noexcept
{
    return startsWithPrompt(mLine, kOptionPrompts);
}
} // namespace muckaii::session
