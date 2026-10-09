#include "directConnect.hpp"

#include "muckaii/base/wire.hpp"

#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/screen/terminal.hpp>

#include "connect.hpp"
#include "environment.hpp"
#include "profileForm.hpp"
#include "uiState.hpp"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace muckaii::tui
{
namespace
{
// kPasswordVariable names the environment fallback used for direct-connect credentials.
constexpr std::string_view kPasswordVariable { "MUCKAII_PASSWORD" };

// kPasswordPrompt asks for a missing password on the terminal.
constexpr std::string_view kPasswordPrompt { "Password: " };

// kMessagePrefix identifies diagnostics from the TUI frontend.
constexpr std::string_view kMessagePrefix { "muckaiiTui: " };

// kBadPasswordMessage explains why a non-printable password is refused.
constexpr std::string_view kBadPasswordMessage { "the password must be printable 7-bit characters" };

// kStartMessage explains a failed network reactor startup.
constexpr std::string_view kStartMessage { "cannot start the network reactor" };

// kExitOk is the process status for a completed connection.
constexpr int kExitOk { 0 };

// kExitFailure is the process status for a startup failure.
constexpr int kExitFailure { 1 };

// kExitUsage is the process status for invalid command-line credentials.
constexpr int kExitUsage { 2 };

// readLine reads one line from the terminal, echoed as typed: a password typed here is visible at
// entry (input.md, DECISION: the input never masks).
std::string readLine(std::string_view prompt)
{
    std::cout << prompt << std::flush;
    std::string line;
    std::getline(std::cin, line);
    if (! line.empty() && static_cast<unsigned char>(line.back()) == wire::kCr)
        line.pop_back();
    return line;
}

// wipeAll erases each present secret buffer before its copy is discarded.
void wipeAll(std::initializer_list<types::OptString*> secrets)
{
    for (types::OptString* const secret : secrets)
    {
        if (secret->has_value())
            session::wipeSecret(**secret);
    }
}

// acquirePassword returns the password for `options`: the given one, else the environment's, else
// asked for. Every other copy is wiped, and the environment variable is removed so nothing started
// later inherits it.
std::string acquirePassword(Options& options)
{
    types::OptString fromEnvironment { findEnvironment(kPasswordVariable) };
    removeEnvironment(kPasswordVariable);
    types::OptString given { findGivenPassword(options, fromEnvironment) };
    std::string      password { given.has_value() ? std::move(*given) : readLine(kPasswordPrompt) };
    wipeAll({ &fromEnvironment, &given, &options.mPassword });
    return password;
}

// printClosing prints what the client said and why the connection closed, once the screen is gone.
void printClosing(const UiState& state)
{
    for (const std::string& notice : state.mNotices)
        std::cerr << kMessagePrefix << notice << "\n";
    if (state.mCloseReason.has_value())
        std::cerr << kMessagePrefix << *state.mCloseReason << "\n";
}

// GameRenderer draws one connected frame and updates the core's width when the terminal resizes.
struct GameRenderer
{
    UiState&           mState;
    core::Core&        mCore;
    const Options&     mOptions;
    const std::string& mHost;

    // operator() renders the current scroll, status and command rows.
    ftxui::Element operator()() const
    {
        if (mState.updateColumns(ftxui::Terminal::Size().dimx, mOptions.mMaxColumns))
            mCore.setColumns(mState.mColumns);
        return mState.renderGame(mHost);
    }
};

// GameEventHandler sends each terminal event to UiState or closes the fullscreen loop.
struct GameEventHandler
{
    ftxui::App& mScreen;
    UiState&    mState;
    core::Core& mCore;

    // operator() consumes framework wakeups and Ctrl+C, and forwards all other input in order.
    bool operator()(const ftxui::Event& event) const
    {
        if (event == ftxui::Event::Custom)
            return true;
        if (event == ftxui::Event::CtrlC)
        {
            mScreen.Exit();
            return true;
        }
        return mState.onKey(event, makeSubmitter(mCore));
    }
};

// DirectConnectBatchHandler applies one core batch and exits the screen after connection closure.
struct DirectConnectBatchHandler
{
    ftxui::App& mScreen;
    UiState&    mState;

    // operator() folds the batch into UI state and exits if the core reports disconnection.
    void operator()(const std::vector<session::SessionEvent>& batch) const
    {
        for (const session::SessionEvent& event : batch)
            mState.apply(event);
        if (mState.mCloseReason.has_value())
            mScreen.Exit();
    }
};

// makeCredentials copies the direct-connect account, password and login name into core credentials.
session::Credentials makeCredentials(const Options& options, const std::string& password)
{
    session::Credentials credentials;
    credentials.mAccount   = options.mAccount;
    credentials.mPassword  = password;
    credentials.mLoginName = options.mLoginName.value_or(std::string { persistence::kDefaultLoginName });
    return credentials;
}

// makeRequest returns the request for `options` with `password`, which it copies.
ConnectionRequest makeRequest(const Options& options, const std::string& password, int columns)
{
    return ConnectionRequest {
        .mHost        = options.mHost,
        .mPort        = options.mPort,
        .mCredentials = makeCredentials(options, password),
        .mMaxColumns  = options.mMaxColumns,
        .mColumns     = columns,
        .mWireLog     = options.mWireLog,
    };
}

// showGame runs the game screen over `core` until the connection closes or the user quits.
void showGame(ftxui::App& screen, UiState& state, core::Core& core, const Options& options)
{
    const std::string host { joinHostPort(options.mHost, options.mPort) };
    const auto        renderer { ftxui::Renderer(GameRenderer(state, core, options, host)) };
    const auto        component { ftxui::CatchEvent(renderer, GameEventHandler(screen, state, core)) };
    screen.Loop(component);
}
} // namespace

int runDirectConnect(Options& options)
{
    std::string password;
    if (options.mAccount.empty())
    {
        // No login to make: a password given anyway is discarded, and the environment copy removed.
        removeEnvironment(kPasswordVariable);
        wipeAll({ &options.mPassword });
    }
    else
    {
        password = acquirePassword(options);
        // A credential with a control or high byte would end or corrupt its line on the wire.
        if (! session::isValidCredential(password))
        {
            session::wipeSecret(password);
            std::cerr << kMessagePrefix << kBadPasswordMessage << "\n";
            return kExitUsage;
        }
    }

    ftxui::App screen { ftxui::App::Fullscreen() };
    screen.TrackMouse(true);
    UiState state;
    state.mColumns = session::getEffectiveColumns(ftxui::Terminal::Size().dimx, options.mMaxColumns);

    // The close ends the client with the connection (connection.md 1.1).
    core::EventSink         sink { makePostingSink(screen, DirectConnectBatchHandler(screen, state)) };
    const ConnectionRequest request { makeRequest(options, password, state.mColumns) };
    const std::unique_ptr<core::Core> core { buildCore(request, std::move(sink)) };
    session::wipeSecret(password);

    if (! core->start())
    {
        std::cerr << kMessagePrefix << kStartMessage << "\n";
        return kExitFailure;
    }
    showGame(screen, state, *core, options);
    core->stop();
    printClosing(state);
    return kExitOk;
}
} // namespace muckaii::tui
