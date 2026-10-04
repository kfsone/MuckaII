#include "muckaii/session/session.hpp"

namespace muckaii::session
{
namespace
{
constexpr std::string_view kLayerName { "session" };
static_assert(! kLayerName.empty());
} // namespace

std::string_view getLayerName() noexcept
{
    return kLayerName;
}
} // namespace muckaii::session
