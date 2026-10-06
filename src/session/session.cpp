#include "muckaii/session/session.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace muckaii::session
{
namespace
{
constexpr std::uint8_t kEsc { 0x1B };
constexpr std::uint8_t kCtrlF { 0x06 };
constexpr std::uint8_t kIac { 0xFF };
constexpr char         kFirstPrintable { 0x20 };
constexpr char         kLastPrintable { 0x7E };

// Clio's anchored literals (connection.md 3.1, default: line). "login: " matches anywhere; the
// others only at the start of a line.
constexpr std::string_view                kShellLogin { "login: " };
constexpr std::string_view                kAccountPrompt { "Account ID: " };
constexpr std::array<std::string_view, 2> kPasswordPrompts { "Password:", "password:" };
constexpr std::array<std::string_view, 2> kOptionPrompts { "Option: ", "Option (H for help): " };

void appendText(std::vector<std::byte>& out, std::string_view text)
{
    for (const char c : text)
        out.push_back(std::byte { static_cast<std::uint8_t>(c) });
}

void appendEscape(std::vector<std::byte>& out, char final)
{
    out.push_back(std::byte { kEsc });
    out.push_back(std::byte { '-' });
    out.push_back(std::byte { static_cast<std::uint8_t>(final) });
}
void appendWidth(std::vector<std::byte>& out, int columns)
{
    appendEscape(out, '[');
    appendText(out, "/T" + std::to_string(columns));
    appendEscape(out, ']');
}
} // namespace

std::vector<std::byte> getLoggedBytes(const Outbound& outbound)
{
    std::vector<std::byte> bytes { outbound.mBytes };
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
            return "masked: account id";
        case Mask::kPassword:
            return "masked: password";
        case Mask::kBeforeLogin:
            return "masked: line before login";
    }
    return {};
}

bool isValidCredential(std::string_view text) noexcept
{
    return std::ranges::all_of(text,
                               [](char c)
                               {
                                   return c >= kFirstPrintable && c <= kLastPrintable;
                               });
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

void wipeSecret(std::vector<std::byte>& secret) noexcept
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

int getEffectiveColumns(int fits, std::optional<int> maxColumns) noexcept
{
    const int ceiling { maxColumns.value_or(kDefaultColumns) };
    return std::clamp(std::min(ceiling, fits), kMinColumns, kMaxColumns);
}

std::vector<std::byte> makeClientModeEntry(int columns)
{
    std::vector<std::byte> out;
    appendEscape(out, '[');
    out.push_back(std::byte { kEsc });
    out.push_back(std::byte { kCtrlF });
    appendEscape(out, 'N');
    appendText(out, "/T" + std::to_string(columns));
    appendEscape(out, ']');
    return out;
}

std::vector<std::byte> makeLine(std::string_view text)
{
    std::vector<std::byte> out;
    out.reserve(text.size() + 2);
    for (const char c : text)
    {
        const std::byte byte { static_cast<std::uint8_t>(c) };
        out.push_back(byte);
        if (byte == std::byte { kIac })
            out.push_back(byte);
    }
    appendText(out, "\r\n");
    return out;
}

Session::Session(const Credentials& credentials, int columns)
    : mCredentials { credentials }
    , mTelnet { columns }
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
    if (std::vector<std::byte> naws { mTelnet.setColumns(columns) }; ! naws.empty())
        queue(std::move(naws), Origin::kTelnet);
    if (mFlowState == FlowState::kGame)
    {
        std::vector<std::byte> width;
        appendWidth(width, columns);
        queue(std::move(width), Origin::kWidth);
    }
}

std::size_t Session::getTypedCount() const noexcept
{
    return static_cast<std::size_t>(std::ranges::count_if(mOutbound,
                                                          [](const Outbound& outbound)
                                                          {
                                                              return outbound.mOrigin == Origin::kTyped;
                                                          }));
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
                if (std::vector<std::byte> reply { mTelnet.onCommand(payload.mBytes) }; ! reply.empty())
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
                mAnnotations.push_back("terminal width confirmed: " + std::to_string(payload.mColumns));
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

    const bool hasLoginName { ! mCredentials.mLoginName.empty()
                              && isValidCredential(mCredentials.mLoginName) };
    if (hasLoginName && ! mHasSentLoginName && mLine.find(kShellLogin) != std::string::npos)
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
        if (std::optional<std::vector<std::byte>> naws { mTelnet.findNawsForClientMode() }; naws.has_value())
            queue(std::move(*naws), Origin::kTelnet);
        queue(makeClientModeEntry(mTelnet.getColumns()), Origin::kLogin);
    }
}

void Session::setFlow(FlowState state, std::vector<SessionEvent>& out)
{
    if (state == mFlowState)
        return;
    mFlowState = state;
    out.emplace_back(FlowEvent { .mState = state });
}

void Session::queue(std::vector<std::byte> bytes, Origin origin, Mask mask)
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
    return mFlowState != FlowState::kGame
           && std::ranges::any_of(kPasswordPrompts,
                                  [this](std::string_view prompt)
                                  {
                                      return mLine.starts_with(prompt);
                                  });
}

bool Session::isOptionPrompt() const noexcept
{
    return std::ranges::any_of(kOptionPrompts,
                               [this](std::string_view prompt)
                               {
                                   return mLine.starts_with(prompt);
                               });
}
} // namespace muckaii::session
