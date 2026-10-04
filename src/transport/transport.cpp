#include "muckaii/transport/transport.hpp"

namespace muckaii::transport
{
namespace
{
constexpr std::string_view kLayerName { "transport" };
static_assert(! kLayerName.empty());
} // namespace

std::string_view getLayerName() noexcept
{
    return kLayerName;
}
} // namespace muckaii::transport
