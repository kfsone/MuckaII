#include "muckaii/mud/mud.hpp"

namespace muckaii::mud
{
namespace
{
constexpr std::string_view kLayerName { "mud" };
static_assert(! kLayerName.empty());
} // namespace

std::string_view getLayerName() noexcept
{
    return kLayerName;
}
} // namespace muckaii::mud
