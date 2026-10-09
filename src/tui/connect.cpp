#include "connect.hpp"

#include <utility>

namespace muckaii::tui
{
std::unique_ptr<core::Core> buildCore(const ConnectionRequest& request, core::EventSink sink)
{
    core::Builder builder;
    builder.setHost(request.mHost)
        .setPort(request.mPort)
        .setCredentials(request.mCredentials)
        .setColumns(request.mColumns)
        .setClientVersion(std::string { kVersion })
        .setClientWrap(session::isClientWrapWidth(request.mMaxColumns))
        .setSink(std::move(sink));
    if (request.mWireLog.has_value())
        builder.setWireLog(*request.mWireLog);
    else
        builder.setDefaultWireLog();
    return builder.build();
}
} // namespace muckaii::tui
