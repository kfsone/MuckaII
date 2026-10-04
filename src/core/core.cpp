#include "muckaii/core/core.hpp"

namespace muckaii::core
{
namespace
{
constexpr std::string_view kLayerName { "core" };
static_assert(! kLayerName.empty());
} // namespace

std::string_view getLayerName() noexcept
{
    return kLayerName;
}
} // namespace muckaii::core
