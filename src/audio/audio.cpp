#include "muckaii/audio/audio.hpp"

namespace muckaii::audio
{
namespace
{
constexpr std::string_view kLayerName { "audio" };
static_assert(! kLayerName.empty());
} // namespace

std::string_view getLayerName() noexcept
{
    return kLayerName;
}
} // namespace muckaii::audio
