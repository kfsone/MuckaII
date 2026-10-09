// muckaiiTui: the FTXUI frontend (INTERNAL.md, Stack). With no direct-connect flag it opens the
// Profile List (docs/spec/flow.md "Profile") and returns to it on disconnect; with any of
// --profile --host --port --user --account --password it connects once at launch instead
// (connection.md 6, direct connect) and closes when the connection does (connection.md 1.1).

#include "muckaii/persistence/profileStore.hpp"
#include "muckaii/persistence/wireLog.hpp"

#include "directConnect.hpp"
#include "options.hpp"
#include "profileApp.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace
{
using namespace muckaii;

// kExitOk is the process status for successful startup or help output.
constexpr int kExitOk { 0 };

// kExitFailure is the process status when a required store cannot open.
constexpr int kExitFailure { 1 };

// kExitUsage is the process status for invalid command-line input.
constexpr int kExitUsage { 2 };

// kMessagePrefix identifies diagnostics from the TUI frontend.
constexpr std::string_view kMessagePrefix { "muckaiiTui: " };

// kUsage describes the TUI command-line options and startup modes.
constexpr std::string_view kUsage {
    "usage: muckaiiTui [--profile <name>] [--host <host>] [--port <port>] [--user <login name>]\n"
    "                  [--account <account id>] [--password <password>]\n"
    "                  [--max-columns <20..160>] [--wire-log <path>]\n"
    "\n"
    "With none of --profile --host --port --user --account --password, opens the Profile List\n"
    "instead of connecting (docs/spec/flow.md \"Profile\"): Enter connects the selected profile,\n"
    "n adds one, e edits it, d deletes it (press d again to confirm), q or Ctrl+C quits.\n"
    "\n"
    "With any of those flags, connects once at startup (direct connect), logs in with the\n"
    "account ID, and enters client mode; nothing is saved. The password comes from --password,\n"
    "else from MUCKAII_PASSWORD, else it is asked for at the terminal (and shown as typed); it is\n"
    "never stored. A --password or --account value is visible to other users of this machine in\n"
    "process listings, and in your shell's history. Pick a persona by typing at the Option menu\n"
    "(for example p, then the persona name). Lines typed before login, and at a password prompt,\n"
    "are masked in the wire log and kept out of Up/Down recall.\n"
    "The wire log is %USERPROFILE%\\.muckaii\\muckaii.db unless --wire-log names another file.\n"
    "Direct connect closes when the connection does; from the Profile List, a disconnect returns\n"
    "to it. Ctrl+C quits. PageUp/PageDown scroll; Up/Down recall typed lines.\n"
};

// openStore opens the profile database, or prints why it cannot and returns null.
std::unique_ptr<persistence::ProfileStore> openStore()
{
    const std::optional<std::filesystem::path> dbPath { persistence::findDefaultDatabasePath() };
    if (! dbPath.has_value())
    {
        std::cerr << kMessagePrefix << "cannot find a home directory for the profile database\n";
        return nullptr;
    }
    auto store { std::make_unique<persistence::ProfileStore>(*dbPath) };
    if (! store->isOpen())
    {
        std::cerr << kMessagePrefix << store->getError() << "\n";
        return nullptr;
    }
    return store;
}

// resolveProfile fills `options` from the saved profile it names. It returns an exit code, or
// nullopt on success.
std::optional<int> resolveProfile(tui::Options& options)
{
    const std::unique_ptr<persistence::ProfileStore> store { openStore() };
    if (! store)
        return kExitFailure;
    const persistence::Profiles profiles { store->listProfiles() };
    const auto found { std::ranges::find(profiles, *options.mProfileName, &persistence::Profile::mName) };
    if (found == profiles.end())
    {
        std::cerr << kMessagePrefix << "no profile named '" << *options.mProfileName << "'\n";
        return kExitUsage;
    }
    tui::applyProfile(options, *found);
    return std::nullopt;
}

// runProfiles opens the Profile List and runs it until the user quits.
int runProfiles(const tui::Options& options)
{
    const std::unique_ptr<persistence::ProfileStore> store { openStore() };
    if (! store)
        return kExitFailure;
    tui::ProfileApp app { options, *store };
    return app.run();
}
} // namespace

// main parses the command line and starts the Profile List, help output or direct connection.
int main(int argc, char** argv)
{
    std::string                 error;
    std::optional<tui::Options> options { tui::parseArguments(
        std::span<char* const> { argv, static_cast<std::size_t>(argc) }, error) };
    if (! options.has_value())
    {
        std::cerr << kMessagePrefix << error << "\n\n" << kUsage;
        return kExitUsage;
    }
    if (options->mIsHelp)
    {
        std::cout << kUsage;
        return kExitOk;
    }
    if (! options->mIsDirectConnect)
        return runProfiles(*options);

    if (options->mProfileName.has_value())
    {
        if (const std::optional<int> failure { resolveProfile(*options) }; failure.has_value())
            return *failure;
    }
    if (options->mHost.empty())
    {
        std::cerr << kMessagePrefix << "no host given; use --host or --profile\n";
        return kExitUsage;
    }
    return tui::runDirectConnect(*options);
}
