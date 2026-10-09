#pragma once

#include "muckaii/base/types.hpp"
#include "muckaii/core/core.hpp"
#include "muckaii/session/session.hpp"

#include "options.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace muckaii::tui
{
// kVersion is the client version string sent to the server.
constexpr std::string_view kVersion { MUCKAII_VERSION };

// ConnectionRequest holds the endpoint, credentials and settings for one connection.
struct ConnectionRequest
{
    std::string          mHost;
    std::uint16_t        mPort { kDefaultPort };
    session::Credentials mCredentials;
    types::OptInt        mMaxColumns; // the Terminal Width setting; 0 asks for client-side wrap
    int                  mColumns {}; // the effective width to tell the server at login
    OptPath              mWireLog;    // nullopt: the default wire log
};

// buildCore returns an unstarted Core configured from `request` and delivering events to `sink`.
[[nodiscard]] std::unique_ptr<core::Core> buildCore(const ConnectionRequest& request, core::EventSink sink);
} // namespace muckaii::tui
