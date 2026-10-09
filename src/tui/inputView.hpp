#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace muckaii::tui
{
// InputView holds the command-box cells visible on one row; its cursor cell and available
// neighbours stay visible while the text scrolls sideways instead of wrapping game output.
struct InputView
{
    std::string mBefore;   // visible text left of the cursor
    std::string mAtCursor; // the cursor cell: the character under it, or a space at the end
    std::string mAfter;    // visible text right of the cursor cell
};

// makeInputView selects the visible UTF-8 text around `cursor`, updating `start` to preserve the
// smallest necessary scroll; widths below three cells are raised to three.
[[nodiscard]] InputView
makeInputView(std::string_view text, std::size_t cursor, std::size_t width, std::size_t& start);
} // namespace muckaii::tui
