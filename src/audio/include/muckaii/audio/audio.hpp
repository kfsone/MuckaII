#pragma once

#include <string_view>

namespace muckaii::audio
{
// getLayerName returns the stable layer name used in layer metadata and diagnostics.
std::string_view getLayerName() noexcept;
}
