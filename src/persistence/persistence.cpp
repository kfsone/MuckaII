#include "muckaii/persistence/persistence.hpp"

namespace muckaii::persistence
{
namespace
{
constexpr std::string_view kLayerName { "persistence" };
static_assert(! kLayerName.empty());
} // namespace

std::string_view getLayerName() noexcept
{
    return kLayerName;
}
} // namespace muckaii::persistence
