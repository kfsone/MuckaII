#include <gtest/gtest.h>

#include "inputView.hpp"

#include <cstddef>
#include <string>

namespace muckaii::tui
{
namespace
{
// shown concatenates the visible cells into their displayed row text.
std::string shown(const InputView& view)
{
    return view.mBefore + view.mAtCursor + view.mAfter;
}
} // namespace

// InputView_ShortTextIsShownWhole checks that short text remains visible around its cursor.
TEST(InputView, ShortTextIsShownWhole)
{
    std::size_t start {};
    const auto  view { makeInputView("abc", 1, 10, start) };
    EXPECT_EQ(view.mBefore, "a");
    EXPECT_EQ(view.mAtCursor, "b");
    EXPECT_EQ(view.mAfter, "c");
}

// InputView_CursorAtTheEndIsABlankCell checks that the cursor at end of line occupies a blank cell.
TEST(InputView, CursorAtTheEndIsABlankCell)
{
    std::size_t start {};
    const auto  view { makeInputView("abc", 3, 10, start) };
    EXPECT_EQ(view.mBefore, "abc");
    EXPECT_EQ(view.mAtCursor, " ");
    EXPECT_EQ(view.mAfter, "");
}

// InputView_TypingPastTheWidthKeepsTheTailAndTheCursor checks that typing beyond the width keeps the tail and cursor visible.
TEST(InputView, TypingPastTheWidthKeepsTheTailAndTheCursor)
{
    std::size_t start {};
    const auto  view { makeInputView("0123456789", 10, 5, start) };
    EXPECT_EQ(view.mBefore, "6789");
    EXPECT_EQ(view.mAtCursor, " ");
}

// InputView_MovingLeftKeepsALeftNeighbourVisible checks that moving left keeps the preceding character visible.
TEST(InputView, MovingLeftKeepsALeftNeighbourVisible)
{
    std::size_t start {};
    (void) makeInputView("0123456789", 10, 5, start);
    // Cursor under '6': '5' must be on screen.
    const auto view { makeInputView("0123456789", 6, 5, start) };
    EXPECT_EQ(view.mBefore, "5");
    EXPECT_EQ(view.mAtCursor, "6");
    EXPECT_EQ(view.mAfter, "789");
}

// InputView_MovingRightKeepsARightNeighbourVisible checks that moving right keeps the following character visible.
TEST(InputView, MovingRightKeepsARightNeighbourVisible)
{
    std::size_t start {};
    const auto  view { makeInputView("0123456789", 4, 5, start) };
    EXPECT_EQ(view.mBefore, "123");
    EXPECT_EQ(view.mAtCursor, "4");
    EXPECT_EQ(view.mAfter, "5");
}

// InputView_HomeShowsTheStart checks that Home returns the visible window to the start.
TEST(InputView, HomeShowsTheStart)
{
    std::size_t start {};
    (void) makeInputView("0123456789", 10, 5, start);
    const auto view { makeInputView("0123456789", 0, 5, start) };
    EXPECT_EQ(view.mBefore, "");
    EXPECT_EQ(view.mAtCursor, "0");
    EXPECT_EQ(view.mAfter, "1234");
}

// InputView_MultibyteCharactersCountAsOneCell checks that each multibyte code point occupies one view cell.
TEST(InputView, MultibyteCharactersCountAsOneCell)
{
    std::size_t       start {};
    const std::string text { "\xC3\xA9\xC3\xA9\xC3\xA9" }; // three two-byte characters
    const auto        view { makeInputView(text, 2, 3, start) };
    EXPECT_EQ(view.mBefore, "\xC3\xA9");
    EXPECT_EQ(view.mAtCursor, "\xC3\xA9");
    EXPECT_EQ(view.mAfter, "\xC3\xA9");
}

// InputView_EveryCursorPositionAtEveryWidthHonoursTheVisibilityRules checks that every cursor and width pair preserves the view rules.
TEST(InputView, EveryCursorPositionAtEveryWidthHonoursTheVisibilityRules)
{
    const std::string text { "abcdefghijklmnopqrstuvwxyz" };
    for (std::size_t width { 3 }; width <= 30; ++width)
    {
        // Walk right then left, so the remembered start is exercised in both directions.
        std::size_t start {};
        for (int pass { 0 }; pass < 2; ++pass)
        {
            for (std::size_t step { 0 }; step <= text.size(); ++step)
            {
                const std::size_t cursor { pass == 0 ? step : text.size() - step };
                const auto        view { makeInputView(text, cursor, width, start) };
                std::string       context { "width " };
                context += std::to_string(width);
                context += " cursor ";
                context += std::to_string(cursor);
                EXPECT_LE(shown(view).size(), width) << context;
                EXPECT_EQ(view.mAtCursor, cursor < text.size() ? text.substr(cursor, 1) : " ") << context;
                if (cursor > 0)
                {
                    EXPECT_EQ(view.mBefore.empty() ? '\0' : view.mBefore.back(), text[cursor - 1]) << context;
                }
                if (cursor + 1 < text.size())
                {
                    EXPECT_EQ(view.mAfter.empty() ? '\0' : view.mAfter.front(), text[cursor + 1]) << context;
                }
                const std::string visibleText { view.mBefore + view.mAtCursor + view.mAfter };
                const bool        isVisible { text.find(visibleText) != std::string::npos };
                EXPECT_TRUE(isVisible || cursor == text.size()) << context;
            }
        }
    }
}

// InputView_TinyWidthIsRaisedToThree checks that widths below three cells are raised to three.
TEST(InputView, TinyWidthIsRaisedToThree)
{
    std::size_t start {};
    const auto  view { makeInputView("abcdef", 3, 1, start) };
    EXPECT_EQ(view.mBefore, "c");
    EXPECT_EQ(view.mAtCursor, "d");
    EXPECT_EQ(view.mAfter, "e");
}
} // namespace muckaii::tui
