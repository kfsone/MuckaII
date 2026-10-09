#pragma once

#include "options.hpp"

namespace muckaii::tui
{
// runDirectConnect connects once, shows the game until closure and returns the process exit code;
// it acquires the password from options, the environment or a visible terminal prompt, then wipes
// every local copy once the core owns its credentials.
int runDirectConnect(Options& options);
} // namespace muckaii::tui
