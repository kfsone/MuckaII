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

#include "scroll.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <csignal>
#include <cstdio>
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
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace
{
using namespace muckaii;

constexpr std::string_view kVersion { MUCKAII_VERSION };
constexpr std::string_view kPasswordVariable { "MUCKAII_PASSWORD" };
constexpr std::uint16_t    kDefaultPort { 23 };
// The status line and the command box.
constexpr int  kChromeRows { 2 };
constexpr char kEscape { '\x1b' };
constexpr char kFirstPrintable { ' ' };
constexpr char kLastPrintable { '~' };

constexpr std::string_view kUsage {
    "usage: muckaiiTui --host <host> [--port <port>] [--account <account id>]\n"
    "                  [--max-columns <20..160>] [--wire-log <path>]\n"
    "\n"
    "Connects once, logs in with the account ID, and enters client mode. The password is asked\n"
    "for at the terminal without echo, or read from MUCKAII_PASSWORD; it is never stored. Pick a\n"
    "persona by typing at the Option menu (for example p, then the persona name). Until the game\n"
    "starts, what you type is shown as * and masked in the wire log.\n"
    "The wire log is %USERPROFILE%\\.muckaii\\muckaii.db unless --wire-log names another file.\n"
    "The client closes when the connection does. Ctrl+C quits. PageUp/PageDown scroll; Up/Down\n"
    "recall typed lines.\n"
};

// What one command-line word is (connection.md 6): not a flag, a flag MuckaII does not know, or
// one of these.
enum class Flag : std::uint8_t
{
    kNotAFlag,
    kUnknown,
    kHost,
    kPort,
    kAccount,
    kMaxColumns,
    kWireLog,
    kHelp,
};

struct Options
{
    std::string                          mHost;
    std::string                          mAccount;
    std::optional<std::filesystem::path> mWireLog;
    std::optional<int>                   mMaxColumns;
    std::uint16_t                        mPort { kDefaultPort };
    bool                                 mIsHelp {};
};

std::string toLower(std::string_view text)
{
    std::string lower;
    for (const char c : text)
        lower.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
    return lower;
}

std::optional<int> parseInt(std::string_view text) noexcept
{
    int        value {};
    const auto parsed { std::from_chars(text.data(), text.data() + text.size(), value) };
    if (parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size())
        return std::nullopt;
    return value;
}

// A flag is -name with any number of leading dashes, case-insensitive, followed by its value
// (connection.md 6). Words that are not flags are ignored.
Flag getFlag(std::string_view arg)
{
    struct Name
    {
        std::string_view mText;
        Flag             mFlag {};
    };
    static constexpr std::array<Name, 6> kNames { {
        { .mText = "host", .mFlag = Flag::kHost },
        { .mText = "port", .mFlag = Flag::kPort },
        { .mText = "account", .mFlag = Flag::kAccount },
        { .mText = "max-columns", .mFlag = Flag::kMaxColumns },
        { .mText = "wire-log", .mFlag = Flag::kWireLog },
        { .mText = "help", .mFlag = Flag::kHelp },
    } };
    if (! arg.starts_with('-'))
        return Flag::kNotAFlag;
    while (arg.starts_with('-'))
        arg.remove_prefix(1);
    const std::string name { toLower(arg) };
    const auto        found { std::ranges::find(kNames, name, &Name::mText) };
    return found != kNames.end() ? found->mFlag : Flag::kUnknown;
}

std::optional<Options> parseArguments(std::span<char*> args, std::string& error)
{
    Options options;
    for (std::size_t i { 1 }; i < args.size(); ++i)
    {
        const std::string_view arg { args[i] };
        const Flag             flag { getFlag(arg) };
        if (flag == Flag::kNotAFlag)
            continue;
        if (flag == Flag::kUnknown)
        {
            error = "unknown flag " + std::string { arg };
            return std::nullopt;
        }
        if (flag == Flag::kHelp)
        {
            options.mIsHelp = true;
            continue;
        }
        if (i + 1 >= args.size() || getFlag(args[i + 1]) != Flag::kNotAFlag)
        {
            error = "missing value for " + std::string { arg };
            return std::nullopt;
        }
        const std::string_view value { args[++i] };
        switch (flag)
        {
            case Flag::kHost:
                options.mHost = value;
                break;
            case Flag::kAccount:
                options.mAccount = value;
                break;
            case Flag::kWireLog:
                options.mWireLog = std::filesystem::path { value };
                break;
            case Flag::kMaxColumns:
            {
                const std::optional<int> columns { parseInt(value) };
                if (! columns.has_value() || *columns < session::kMinColumns
                    || *columns > session::kMaxColumns)
                {
                    error = "--max-columns takes a number from 20 to 160";
                    return std::nullopt;
                }
                options.mMaxColumns = columns;
                break;
            }
            case Flag::kPort:
            {
                // Out of range or not a number: ignored without warning (connection.md 6).
                const std::optional<int> port { parseInt(value) };
                constexpr int            kMaxPort { 65535 };
                if (port.has_value() && *port >= 1 && *port <= kMaxPort)
                    options.mPort = static_cast<std::uint16_t>(*port);
                break;
            }
            case Flag::kNotAFlag:
            case Flag::kUnknown:
            case Flag::kHelp:
                break;
        }
    }
    if (! options.mIsHelp && options.mHost.empty())
    {
        error = "--host is required";
        return std::nullopt;
    }
    return options;
}

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

// The console state readHidden changed, restored by the Ctrl+C handler if the user quits there.
#ifdef _WIN32
std::atomic<void*> gHiddenInput {};
std::atomic<DWORD> gHiddenMode {};

BOOL WINAPI restoreEchoOnCtrlC(DWORD /*type*/)
{
    if (void* const input { gHiddenInput.load() }; input != nullptr)
        SetConsoleMode(input, gHiddenMode.load());
    return FALSE; // the default handler still ends the process
}
#else
termios               gHiddenTermios {};
volatile sig_atomic_t gIsHiding {};

void restoreEchoOnInterrupt(int signal)
{
    if (gIsHiding != 0)
        tcsetattr(STDIN_FILENO, TCSANOW, &gHiddenTermios);
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}
#endif

// Reads one line from the terminal without echo. The password never reaches the screen, the log
// or the disk. Echo comes back even if the user presses Ctrl+C at the prompt.
std::string readHidden(std::string_view prompt)
{
    std::cout << prompt << std::flush;
    std::string line;
#ifdef _WIN32
    void* const input { GetStdHandle(STD_INPUT_HANDLE) };
    DWORD       mode {};
    const bool  isConsole { GetConsoleMode(input, &mode) != 0 };
    if (isConsole)
    {
        gHiddenMode.store(mode);
        gHiddenInput.store(input);
        SetConsoleCtrlHandler(restoreEchoOnCtrlC, TRUE);
        SetConsoleMode(input, mode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT));
    }
    std::getline(std::cin, line);
    if (isConsole)
    {
        SetConsoleMode(input, mode);
        SetConsoleCtrlHandler(restoreEchoOnCtrlC, FALSE);
        gHiddenInput.store(nullptr);
    }
#else
    const bool isTerminal { tcgetattr(STDIN_FILENO, &gHiddenTermios) == 0 };
    if (isTerminal)
    {
        gIsHiding = 1;
        std::signal(SIGINT, restoreEchoOnInterrupt);
        termios hidden { gHiddenTermios };
        hidden.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &hidden);
    }
    std::getline(std::cin, line);
    if (isTerminal)
    {
        tcsetattr(STDIN_FILENO, TCSANOW, &gHiddenTermios);
        gIsHiding = 0;
        std::signal(SIGINT, SIG_DFL);
    }
#endif
    std::cout << "\n" << std::flush;
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
            // Core alone says whether the line is sensitive; the recall list follows its verdict.
            const bool isSensitive { core.submitLine(mEditor.getText()) };
            static_cast<void>(mEditor.submit(isSensitive));
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

    // The command box: masked while core says input is sensitive, the cursor cell inverted.
    [[nodiscard]] ftxui::Element renderInput(const core::Core& core) const
    {
        const std::string& text { mEditor.getText() };
        const std::size_t  cursor { mEditor.getCursor() };
        std::string        before { text.substr(0, cursor) };
        std::string        after { text.substr(cursor) };
        if (core.isInputSensitive())
        {
            before = std::string(client::countCodePoints(before), '*');
            after  = std::string(client::countCodePoints(after), '*');
        }
        std::string atCursor { " " };
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
int runApp(const Options& options, std::string& password)
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
                state.renderInput(*core),
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
    std::string                  error;
    const std::optional<Options> options { parseArguments(std::span { argv, static_cast<std::size_t>(argc) },
                                                          error) };
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
    // A credential with a control or high byte would end or corrupt its line on the wire.
    if (! session::isValidCredential(options->mAccount))
    {
        std::cerr << "muckaiiTui: the account ID must be printable 7-bit characters\n";
        return 2;
    }

    std::string password;
    if (! options->mAccount.empty())
    {
        std::optional<std::string> fromEnvironment { findEnvironment(kPasswordVariable) };
        // Read once, then gone from the environment, so nothing started later inherits it.
        removeEnvironment(kPasswordVariable);
        password = fromEnvironment.has_value() ? std::move(*fromEnvironment) : readHidden("Password: ");
        if (fromEnvironment.has_value())
            session::wipeSecret(*fromEnvironment);
        if (! session::isValidCredential(password))
        {
            session::wipeSecret(password);
            std::cerr << "muckaiiTui: the password must be printable 7-bit characters\n";
            return 2;
        }
    }
    return runApp(*options, password);
}
