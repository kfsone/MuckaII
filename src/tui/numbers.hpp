#pragma once

#include "muckaii/base/types.hpp"

#include <cstdint>
#include <string_view>

// Number parsing and the TCP port range, shared by the command line, the profile form and the
// connect path.
namespace muckaii::tui
{
// kDefaultPort is the MUD2 TCP port used when no port is given.
constexpr std::uint16_t kDefaultPort { 23 };

// kMinPort is the first usable TCP port.
constexpr int kMinPort { 1 };

// kMaxPort is the last usable TCP port.
constexpr int kMaxPort { 65535 };

// parseInt reads all of `text` as a decimal integer with an optional leading minus, returning
// nullopt for empty, trailing or overflowing input.
[[nodiscard]] types::OptInt parseInt(std::string_view text) noexcept;

// isValidPort reports whether `port` lies within the usable TCP port range.
[[nodiscard]] constexpr bool isValidPort(int port) noexcept
{
    return port >= kMinPort && port <= kMaxPort;
}
} // namespace muckaii::tui
