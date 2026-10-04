#include "muckaii/core/core.hpp"

#include <cstdio>
#include <string_view>

// Placeholder frontend: FTXUI is wired in a later pass (docs/build.md, "Dependencies").
int main()
{
    const std::string_view name { muckaii::core::getLayerName() };
    std::printf("muckaiiTui placeholder; linked layer: %.*s\n", static_cast<int>(name.size()), name.data());
    return 0;
}
