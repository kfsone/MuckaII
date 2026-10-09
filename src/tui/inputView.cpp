#include "inputView.hpp"

#include "muckaii/base/wire.hpp"

#include <algorithm>
#include <vector>

namespace muckaii::tui
{
namespace
{
using CodePointOffsets = std::vector<std::size_t>;

// kMinimumInputViewWidth is the smallest width that can show a cursor and both neighbours.
constexpr std::size_t kMinimumInputViewWidth { 3 };

// getCodePointOffsets returns each UTF-8 code point's byte offset and the text's end offset.
CodePointOffsets getCodePointOffsets(std::string_view text)
{
    CodePointOffsets offsets;
    for (std::size_t i { 0 }; i < text.size(); ++i)
    {
        const auto byte { static_cast<unsigned char>(text[i]) };
        if ((byte & wire::kUtf8ContinuationMask) != wire::kUtf8Continuation)
            offsets.push_back(i);
    }
    offsets.push_back(text.size());
    return offsets;
}

// getCursorIndex returns the code point index at the UTF-8 boundary `cursor`.
std::size_t getCursorIndex(const CodePointOffsets& offsets, std::size_t cursor)
{
    const auto found { std::ranges::lower_bound(offsets, cursor) };
    return static_cast<std::size_t>(found - offsets.begin());
}

// getFirstVisibleIndex keeps the cursor and its available neighbours visible within `width`.
std::size_t getFirstVisibleIndex(std::size_t at, std::size_t count, std::size_t width, std::size_t& start)
{
    const std::size_t leftNeighbour { at > 0 ? at - 1 : 0 };
    std::size_t       rightNeighbour { at };
    if (at + 1 < count)
        rightNeighbour = at + 1;

    std::size_t first { std::min(start, count) };
    if (first > leftNeighbour)
        first = leftNeighbour;
    if (first + width - 1 < rightNeighbour)
        first = rightNeighbour + 1 - width;
    const std::size_t lastStart { count + 1 > width ? count + 1 - width : 0 };
    first = std::min(first, lastStart);
    start = first;
    return first;
}

// getTextSlice returns the UTF-8 bytes for code points [from, to).
std::string
getTextSlice(std::string_view text, const CodePointOffsets& offsets, std::size_t from, std::size_t to)
{
    const std::size_t begin { offsets[from] };
    const std::size_t length { offsets[to] - begin };
    return std::string { text.substr(begin, length) };
}
} // namespace

// makeInputView exposes the visible text around a cursor without wrapping the command row.
InputView makeInputView(std::string_view text, std::size_t cursor, std::size_t width, std::size_t& start)
{
    width = std::max(width, kMinimumInputViewWidth);
    const CodePointOffsets offsets { getCodePointOffsets(text) };
    const std::size_t      count { offsets.size() - 1 };
    const std::size_t      at { getCursorIndex(offsets, std::min(cursor, text.size())) };
    const std::size_t      first { getFirstVisibleIndex(at, count, width, start) };
    const std::size_t      last { std::min(first + width, count) };

    InputView view;
    view.mBefore = getTextSlice(text, offsets, first, at);
    if (at < count)
    {
        view.mAtCursor = getTextSlice(text, offsets, at, at + 1);
        view.mAfter    = getTextSlice(text, offsets, at + 1, std::max(last, at + 1));
    }
    else
        view.mAtCursor = " ";
    return view;
}
} // namespace muckaii::tui
