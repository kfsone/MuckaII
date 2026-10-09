#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/terminal.hpp>
#include <gtest/gtest.h>

#include "scroll.hpp"

#include <string>
#include <string_view>

using namespace muckaii;

namespace
{
// kRedSgr is the ANSI SGR sequence for the red foreground asserted by the render test.
constexpr std::string_view kRedSgr { "\x1b[31m" };

// makePartial creates a session event carrying the given partial line.
session::SessionEvent makePartial(std::string text, bool isContinued)
{
    return mud::Event { .mSeq     = 0,
                        .mPayload = mud::PartialEvent {
                            .mRuns        = { mud::Run { .mText = std::move(text), .mStyle = {} } },
                            .mIsContinued = isContinued } };
}

// draw renders `scroll` into a fixed test terminal and returns its output.
std::string draw(const tui::Scroll& scroll)
{
    ftxui::Screen screen { ftxui::Screen::Create(ftxui::Dimension::Fixed(60), ftxui::Dimension::Fixed(3)) };
    ftxui::Render(screen, scroll.render(60, 3));
    return screen.ToString();
}

// count returns the number of occurrences of `what` in `text`.
std::size_t count(const std::string& text, const std::string& what)
{
    std::size_t found { 0 };
    for (std::size_t at { text.find(what) }; at != std::string::npos; at = text.find(what, at + 1))
        ++found;
    return found;
}
} // namespace

// Scroll_APartialThatDoesNotContinueReplacesTheLiveOne checks that a noncontinuing partial replaces existing live text.
TEST(Scroll, APartialThatDoesNotContinueReplacesTheLiveOne)
{
    tui::Scroll scroll;
    scroll.apply(makePartial("Option (H for help): ", false));
    scroll.apply(mud::Event { .mSeq = 0, .mPayload = mud::WidthAckEvent { .mColumns = 80 } });
    scroll.apply(makePartial("Option (H for help): ", false));
    EXPECT_EQ(count(draw(scroll), "Option"), 1U);
}

// Scroll_AContinuingPartialExtendsTheLiveOne checks that a continuing partial extends the live line.
TEST(Scroll, AContinuingPartialExtendsTheLiveOne)
{
    tui::Scroll scroll;
    scroll.apply(makePartial("Account ", false));
    scroll.apply(makePartial("ID: ", true));
    EXPECT_NE(draw(scroll).find("Account ID: "), std::string::npos);
}

// Scroll_AColouredRunIsDrawnWithItsSgrColour checks that a coloured run is rendered using its SGR foreground.
TEST(Scroll, AColouredRunIsDrawnWithItsSgrColour)
{
    const ftxui::Terminal::Color previous { ftxui::Terminal::ColorSupport() };
    ftxui::Terminal::SetColorSupport(ftxui::Terminal::Color::Palette16);
    tui::Scroll      scroll;
    const mud::Style redStyle { .mFg = 1, .mBg = std::nullopt, .mAttrs = 0 };
    const mud::Event redText { .mSeq = 0, .mPayload = mud::TextEvent { .mText = "red", .mStyle = redStyle } };
    scroll.apply(redText);
    scroll.apply(mud::Event { .mSeq = 0, .mPayload = mud::LineEndEvent {} });
    const std::string drawn { draw(scroll) };
    ftxui::Terminal::SetColorSupport(previous);
    const std::size_t sgr { drawn.find(kRedSgr) };
    ASSERT_NE(sgr, std::string::npos);
    EXPECT_LT(sgr, drawn.find("red"));
}
