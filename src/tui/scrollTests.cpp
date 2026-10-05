#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <gtest/gtest.h>

#include "scroll.hpp"

#include <string>

using namespace muckaii;

namespace
{
session::SessionEvent makePartial(std::string text, bool isContinued)
{
    return mud::Event { .mSeq     = 0,
                        .mPayload = mud::PartialEvent {
                            .mRuns        = { mud::Run { .mText = std::move(text), .mStyle = {} } },
                            .mIsContinued = isContinued } };
}

std::string draw(const tui::Scroll& scroll)
{
    ftxui::Screen screen { ftxui::Screen::Create(ftxui::Dimension::Fixed(60), ftxui::Dimension::Fixed(3)) };
    ftxui::Render(screen, scroll.render(60, 3));
    return screen.ToString();
}

std::size_t count(const std::string& text, const std::string& what)
{
    std::size_t found { 0 };
    for (std::size_t at { text.find(what) }; at != std::string::npos; at = text.find(what, at + 1))
        ++found;
    return found;
}
} // namespace

// A partial that does not continue the live one replaces it: the second Option prompt after the
// hidden width notice is not drawn beside the first (protocol.md "Line model").
TEST(Scroll, APartialThatDoesNotContinueReplacesTheLiveOne)
{
    tui::Scroll scroll;
    scroll.apply(makePartial("Option (H for help): ", false));
    scroll.apply(mud::Event { .mSeq = 0, .mPayload = mud::WidthAckEvent { .mColumns = 80 } });
    scroll.apply(makePartial("Option (H for help): ", false));
    EXPECT_EQ(count(draw(scroll), "Option"), 1U);
}

TEST(Scroll, AContinuingPartialExtendsTheLiveOne)
{
    tui::Scroll scroll;
    scroll.apply(makePartial("Account ", false));
    scroll.apply(makePartial("ID: ", true));
    EXPECT_NE(draw(scroll).find("Account ID: "), std::string::npos);
}
