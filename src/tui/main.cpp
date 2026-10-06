// muckaiiTui: the FTXUI frontend (INTERNAL.md, Stack). It connects once at launch (direct connect,
// docs/spec/connection.md 6), logs in, and shows the scroll, a one-line status and the command box.
// It closes when the connection does (connection.md 1.1, direct-connect mode).

#include "muckaii/client/lineEditor.hpp"
#include "muckaii/core/core.hpp"
#include "muckaii/session/session.hpp"

#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>

#include "options.hpp"
#include "scroll.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
using namespace muckaii;

constexpr std::string_view kVersion { MUCKAII_VERSION };
constexpr std::string_view kPasswordVariable { "MUCKAII_PASSWORD" };
// The status line and the command box.
constexpr int  kChromeRows { 2 };
constexpr char kEscape { '\x1b' };
constexpr char kFirstPrintable { ' ' };
constexpr char kLastPrintable { '~' };

constexpr std::string_view kUsage {
    "usage: muckaiiTui --host <host> [--port <port>] [--account <account id>]\n"
    "                  [--password <password>] [--max-columns <20..160>] [--wire-log <path>]\n"
    "\n"
    "Connects once, logs in with the account ID, and enters client mode. The password comes from\n"
    "--password, else from MUCKAII_PASSWORD, else it is asked for at the terminal (and shown as\n"
    "typed); it is never stored. A --password value is visible to other users of this machine in\n"
    "process listings, and in your shell's history. Pick a persona by typing at the Option menu\n"
    "(for example p, then the persona name). Lines typed before login, and at a password prompt,\n"
    "are masked in the wire log and kept out of Up/Down recall.\n"
    "The wire log is %USERPROFILE%\\.muckaii\\muckaii.db unless --wire-log names another file.\n"
    "The client closes when the connection does. Ctrl+C quits. PageUp/PageDown scroll; Up/Down\n"
    "recall typed lines.\n"
};

// An environment variable as UTF-8 (never the ANSI code page, which would turn a character it
// lacks into '?').
std::optional<std::string> findEnvironment(std::string_view name)
{
#ifdef _WIN32
    const std::wstring key { name.begin(), name.end() };
    const DWORD        size { GetEnvironmentVariableW(key.c_str(), nullptr, 0) };
    if (size == 0)
        return std::nullopt;
    std::wstring wide(size, L'\0');
    const DWORD  written { GetEnvironmentVariableW(key.c_str(), wide.data(), size) };
    if (written >= size)
        return std::nullopt;
    wide.resize(written);
    if (wide.empty())
        return std::string {};
    const int   length { WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr) };
    std::string value(static_cast<std::size_t>(std::max(length, 0)), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), value.data(), length, nullptr, nullptr);
    // The wide copy may be a password.
    SecureZeroMemory(wide.data(), wide.size() * sizeof(wchar_t));
    return value;
#else
    const char* const value { std::getenv(std::string { name }.c_str()) };
    if (value == nullptr)
        return std::nullopt;
    return std::string { value };
#endif
}

// Removes a variable from this process's environment (a password read from it is held elsewhere).
void removeEnvironment(std::string_view name)
{
#ifdef _WIN32
    const std::wstring key { name.begin(), name.end() };
    SetEnvironmentVariableW(key.c_str(), nullptr);
#else
    unsetenv(std::string { name }.c_str());
#endif
}

// Reads one line from the terminal, echoed as typed: a password typed here is visible at entry
// (input.md, DECISION: the input never masks).
std::string readLine(std::string_view prompt)
{
    std::cout << prompt << std::flush;
    std::string line;
    std::getline(std::cin, line);
    if (line.ends_with('\r'))
        line.pop_back();
    return line;
}

std::string_view getConnectionName(session::ConnectionState state) noexcept
{
    switch (state)
    {
        case session::ConnectionState::kConnecting:
            return "connecting";
        case session::ConnectionState::kConnected:
            return "connected";
        case session::ConnectionState::kClosed:
            return "closed";
    }
    return "";
}

std::string_view getFlowName(session::FlowState state) noexcept
{
    switch (state)
    {
        case session::FlowState::kProfile:
            return "Profile";
        case session::FlowState::kPregame:
            return "Pre-game";
        case session::FlowState::kGame:
            return "Game";
    }
    return "";
}

// Alt+<printable> arrives as ESC and the key: the key is typed text, never discarded.
std::optional<char> findAltCharacter(const ftxui::Event& event)
{
    const std::string& input { event.input() };
    if (input.size() != 2 || input[0] != kEscape || input[1] < kFirstPrintable || input[1] > kLastPrintable)
        return std::nullopt;
    return input[1];
}

bool isHome(const ftxui::Event& event)
{
    return event == ftxui::Event::Home || event.input() == "\x1b[1~" || event.input() == "\x1bOH";
}

bool isEnd(const ftxui::Event& event)
{
    return event == ftxui::Event::End || event.input() == "\x1b[4~" || event.input() == "\x1bOF";
}

// Everything the UI thread owns. The core thread never touches it: batches arrive through Post().
struct UiState
{
    tui::Scroll                mScroll;
    client::LineEditor         mEditor; // the frontend-owned line buffer (root CLAUDE.md, Invariant #1)
    std::vector<std::string>   mNotices;
    std::optional<std::string> mCloseReason;
    int                        mColumns { session::kDefaultColumns };
    session::ConnectionState   mConnection { session::ConnectionState::kConnecting };
    session::FlowState         mFlow { session::FlowState::kProfile };

    void apply(const session::SessionEvent& event)
    {
        if (const auto* connection { std::get_if<session::ConnectionEvent>(&event) }; connection != nullptr)
        {
            mConnection = connection->mState;
            if (connection->mState == session::ConnectionState::kClosed)
                mCloseReason = connection->mReason;
        }
        if (const auto* flow { std::get_if<session::FlowEvent>(&event) }; flow != nullptr)
            mFlow = flow->mState;
        if (const auto* notice { std::get_if<session::NoticeEvent>(&event) }; notice != nullptr)
            mNotices.push_back(notice->mText);
        mScroll.apply(event);
    }

    // Applies one key event to the line buffer or the scroll, in arrival order. False for an event
    // that is not input.
    bool onKey(const ftxui::Event& event, core::Core& core)
    {
        if (event == ftxui::Event::PageUp || event == ftxui::Event::PageDown)
        {
            const int page { std::max(ftxui::Terminal::Size().dimy - kChromeRows - 1, 1) };
            if (event == ftxui::Event::PageUp)
                mScroll.scrollUp(page, mColumns);
            else
                mScroll.scrollDown(page, mColumns);
            return true;
        }
        // In scrollback, Escape and End return to live (input.md "Scrollback"); every other key
        // returns to live and then acts, so no key is swallowed (OPEN D4, default).
        if (! mScroll.isLive() && (event == ftxui::Event::Escape || isEnd(event)))
        {
            mScroll.returnToLive();
            return true;
        }
        if (event == ftxui::Event::Return)
        {
            mScroll.returnToLive();
            // Core alone says whether the line is masked; the recall list follows its verdict.
            const bool isMasked { core.submitLine(mEditor.getText()) };
            static_cast<void>(mEditor.submit(isMasked));
            return true;
        }
        if (event.is_character())
        {
            mScroll.returnToLive();
            mEditor.insert(event.character());
            return true;
        }
        if (const std::optional<char> alt { findAltCharacter(event) }; alt.has_value())
        {
            mScroll.returnToLive();
            mEditor.insert(std::string_view { &*alt, 1 });
            return true;
        }
        return onEditKey(event);
    }

    bool onEditKey(const ftxui::Event& event)
    {
        if (event == ftxui::Event::Backspace)
            mEditor.backspace();
        else if (event == ftxui::Event::Delete)
            mEditor.deleteForward();
        else if (event == ftxui::Event::ArrowLeft)
            mEditor.moveLeft();
        else if (event == ftxui::Event::ArrowRight)
            mEditor.moveRight();
        else if (isHome(event))
            mEditor.moveHome();
        else if (isEnd(event))
            mEditor.moveEnd();
        else if (event == ftxui::Event::ArrowUp)
            mEditor.recallPrevious();
        else if (event == ftxui::Event::ArrowDown)
            mEditor.recallNext();
        else if (event == ftxui::Event::Escape)
            mEditor.clear();
        else if (event == ftxui::Event::Tab || event == ftxui::Event::TabReverse)
        {
            // Tab does nothing in the box (OPEN D9, default).
        }
        else
            return false;
        mScroll.returnToLive();
        return true;
    }

    // The command box, shown as typed (never masked: input.md), the cursor cell inverted.
    [[nodiscard]] ftxui::Element renderInput() const
    {
        const std::string& text { mEditor.getText() };
        const std::size_t  cursor { mEditor.getCursor() };
        const std::string  before { text.substr(0, cursor) };
        std::string        after { text.substr(cursor) };
        std::string        atCursor { " " };
        if (! after.empty())
        {
            std::size_t length { 1 };
            while (length < after.size() && (static_cast<unsigned char>(after[length]) & 0xC0U) == 0x80U)
                ++length;
            atCursor = after.substr(0, length);
            after.erase(0, length);
        }
        return ftxui::hbox({ ftxui::text("> "),
                             ftxui::text(before),
                             ftxui::text(atCursor) | ftxui::inverted,
                             ftxui::text(after) });
    }
};

// Takes the password over: it is wiped here once the core holds its own copy.
int runApp(const tui::Options& options, std::string& password)
{
    auto    screen { ftxui::App::Fullscreen() };
    UiState state;
    screen.TrackMouse(false);

    const ftxui::Dimensions size { ftxui::Terminal::Size() };
    state.mColumns = session::getEffectiveColumns(size.dimx, options.mMaxColumns);

    core::Builder builder;
    builder.setHost(options.mHost)
        .setPort(options.mPort)
        .setCredentials(
            session::Credentials { .mAccount = options.mAccount, .mPassword = password, .mLoginName = "mud" })
        .setColumns(state.mColumns)
        .setClientVersion(std::string { kVersion })
        .setSink(
            [&screen, &state](std::vector<session::SessionEvent> batch)
            {
                // Core thread: hand the batch to the UI thread in one hop.
                auto shared { std::make_shared<std::vector<session::SessionEvent>>(std::move(batch)) };
                screen.Post(
                    [&state, &screen, shared]()
                    {
                        for (const session::SessionEvent& event : *shared)
                            state.apply(event);
                        // Direct connect: the client closes with the connection (connection.md 1.1).
                        if (state.mCloseReason.has_value())
                            screen.Exit();
                    });
                screen.PostEvent(ftxui::Event::Custom);
            });
    if (options.mWireLog.has_value())
        builder.setWireLog(*options.mWireLog);
    else
        builder.setDefaultWireLog();
    const std::unique_ptr<core::Core> core { builder.build() };
    session::wipeSecret(password);

    const std::string host { options.mHost + ":" + std::to_string(options.mPort) };
    const auto        renderer { ftxui::Renderer(
        [&state, &options, &core, &host]()
        {
            const ftxui::Dimensions terminal { ftxui::Terminal::Size() };
            const int columns { session::getEffectiveColumns(terminal.dimx, options.mMaxColumns) };
            if (columns != state.mColumns)
            {
                state.mColumns = columns;
                core->setColumns(columns);
            }
            const int   rows { std::max(terminal.dimy - kChromeRows, 1) };
            std::string status { " " + std::string { getConnectionName(state.mConnection) } + " | "
                                 + std::string { getFlowName(state.mFlow) } + " | "
                                 + std::to_string(state.mColumns) + " columns | " + host };
            if (! state.mScroll.isLive())
                status += " | SCROLLBACK - Esc returns to live";
            return ftxui::vbox({
                state.mScroll.render(state.mColumns, rows) | ftxui::flex,
                ftxui::text(status) | ftxui::inverted,
                state.renderInput(),
            });
        }) };

    // The lambda only translates: every key event goes to UiState::onKey in arrival order, and
    // Enter acts exactly where it falls (root CLAUDE.md, Invariant #1).
    const auto component { ftxui::CatchEvent(renderer,
                                             [&state, &core, &screen](const ftxui::Event& event)
                                             {
                                                 if (event == ftxui::Event::Custom)
                                                     return true;
                                                 if (event == ftxui::Event::CtrlC)
                                                 {
                                                     screen.Exit();
                                                     return true;
                                                 }
                                                 return state.onKey(event, *core);
                                             }) };

    if (! core->start())
    {
        std::cerr << "muckaiiTui: cannot start the network reactor\n";
        return 1;
    }
    screen.Loop(component);
    core->stop();

    // The screen is gone: what the client said, and why the connection closed, go to the terminal.
    for (const std::string& notice : state.mNotices)
        std::cerr << "muckaiiTui: " << notice << "\n";
    if (state.mCloseReason.has_value())
        std::cerr << "muckaiiTui: " << *state.mCloseReason << "\n";
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    std::string                 error;
    std::optional<tui::Options> options { tui::parseArguments(
        std::span<char* const> { argv, static_cast<std::size_t>(argc) }, error) };
    if (! options.has_value())
    {
        std::cerr << "muckaiiTui: " << error << "\n\n" << kUsage;
        return 2;
    }
    if (options->mIsHelp)
    {
        std::cout << kUsage;
        return 0;
    }

    std::string password;
    if (! options->mAccount.empty())
    {
        std::optional<std::string> fromEnvironment { findEnvironment(kPasswordVariable) };
        // Read once, then gone from the environment, so nothing started later inherits it.
        removeEnvironment(kPasswordVariable);
        std::optional<std::string> given { tui::findGivenPassword(*options, fromEnvironment) };
        password = given.has_value() ? std::move(*given) : readLine("Password: ");
        for (std::optional<std::string>* const copy : { &fromEnvironment, &given, &options->mPassword })
        {
            if (copy->has_value())
                session::wipeSecret(**copy);
        }
        // A credential with a control or high byte would end or corrupt its line on the wire.
        if (! session::isValidCredential(password))
        {
            session::wipeSecret(password);
            std::cerr << "muckaiiTui: the password must be printable 7-bit characters\n";
            return 2;
        }
    }
    return runApp(*options, password);
}
