#pragma once

#include "muckaii/base/types.hpp"

#include <string_view>

namespace muckaii::tui
{
// findEnvironment returns the environment variable `name` as UTF-8 (never the ANSI code page,
// which would turn a character it lacks into '?'), or nullopt when it is unset.
[[nodiscard]] types::OptString findEnvironment(std::string_view name);

// removeEnvironment removes `name` from this process's environment, so a password read from it
// is not inherited by anything started later.
void removeEnvironment(std::string_view name);
} // namespace muckaii::tui
