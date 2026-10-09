#include <ftxui/component/event.hpp>
#include <gtest/gtest.h>

#include "uiState.hpp"

#include <string>
#include <vector>

using namespace muckaii;

namespace
{
// kScrollbackLines is enough text lines that scrolling up leaves live mode.
constexpr int kScrollbackLines { 200 };

// kScrollRows is the wheel-free scroll distance used to leave live mode.
constexpr int kScrollRows { 5 };

// kTestColumns is the width used for scroll calls.
constexpr int kTestColumns { 80 };

// Submissions records the lines a test submitter received and the masking verdict it returns.
struct Submissions
{
    std::vector<std::string> mLines;
    bool                     mIsMasked {};

    // asSubmitter returns a LineSubmitter that records into this object.
    tui::LineSubmitter asSubmitter()
    {
        return [this](std::string line)
        {
            mLines.push_back(std::move(line));
            return mIsMasked;
        };
    }
};

// type sends each character of `text` to `state` as a key event.
void type(tui::UiState& state, const std::string& text, const tui::LineSubmitter& submit)
{
    for (const char c : text)
        EXPECT_TRUE(state.onKey(ftxui::Event::Character(c), submit));
}

// fillScrollback adds lines to the scrollback and scrolls up so the view is not live.
void fillScrollback(tui::UiState& state)
{
    for (int i {}; i < kScrollbackLines; ++i)
    {
        state.mScroll.apply(
            mud::Event { .mSeq = 0, .mPayload = mud::TextEvent { .mText = "line", .mStyle = {} } });
        state.mScroll.apply(mud::Event { .mSeq = 0, .mPayload = mud::LineEndEvent {} });
    }
    state.mScroll.scrollUp(kScrollRows, kTestColumns);
}
} // namespace

// UiState_EnterSubmitsExactlyTheTypedText checks Enter hands over the buffer as typed and empties it.
TEST(UiState, EnterSubmitsExactlyTheTypedText)
{
    tui::UiState state;
    Submissions  sent;
    const auto   submit { sent.asSubmitter() };
    type(state, "go north", submit);
    EXPECT_TRUE(state.onKey(ftxui::Event::Return, submit));
    ASSERT_EQ(sent.mLines.size(), 1U);
    EXPECT_EQ(sent.mLines[0], "go north");
    EXPECT_TRUE(state.mEditor.getText().empty());
}

// UiState_SuccessiveLinesStayInOrder checks text typed after an Enter lands in the next submission.
TEST(UiState, SuccessiveLinesStayInOrder)
{
    tui::UiState state;
    Submissions  sent;
    const auto   submit { sent.asSubmitter() };
    type(state, "one", submit);
    EXPECT_TRUE(state.onKey(ftxui::Event::Return, submit));
    type(state, "two", submit);
    EXPECT_TRUE(state.onKey(ftxui::Event::Return, submit));
    EXPECT_EQ(sent.mLines, (std::vector<std::string> { "one", "two" }));
}

// UiState_AMaskedLineIsNotRecalled checks the submitter's masking verdict keeps a line out of recall.
TEST(UiState, AMaskedLineIsNotRecalled)
{
    tui::UiState state;
    Submissions  sent;
    sent.mIsMasked = true;
    const auto submit { sent.asSubmitter() };
    type(state, "secret", submit);
    EXPECT_TRUE(state.onKey(ftxui::Event::Return, submit));
    EXPECT_TRUE(state.onKey(ftxui::Event::ArrowUp, submit));
    EXPECT_TRUE(state.mEditor.getText().empty());
}

// UiState_TabIsConsumedAndChangesNothing checks Tab is ignored: consumed, no text, nothing sent.
TEST(UiState, TabIsConsumedAndChangesNothing)
{
    tui::UiState state;
    Submissions  sent;
    const auto   submit { sent.asSubmitter() };
    type(state, "ab", submit);
    EXPECT_TRUE(state.onKey(ftxui::Event::Tab, submit));
    EXPECT_EQ(state.mEditor.getText(), "ab");
    EXPECT_TRUE(sent.mLines.empty());
}

// UiState_EscapeInScrollbackReturnsToLiveAndKeepsTheText checks Escape leaves scrollback without clearing input.
TEST(UiState, EscapeInScrollbackReturnsToLiveAndKeepsTheText)
{
    tui::UiState state;
    Submissions  sent;
    const auto   submit { sent.asSubmitter() };
    type(state, "keep", submit);
    fillScrollback(state);
    ASSERT_FALSE(state.mScroll.isLive());
    EXPECT_TRUE(state.onKey(ftxui::Event::Escape, submit));
    EXPECT_TRUE(state.mScroll.isLive());
    EXPECT_EQ(state.mEditor.getText(), "keep");
}

// UiState_AKeyThatLeavesScrollbackIsStillApplied checks a typed key in scrollback returns to live and is not swallowed.
TEST(UiState, AKeyThatLeavesScrollbackIsStillApplied)
{
    tui::UiState state;
    Submissions  sent;
    const auto   submit { sent.asSubmitter() };
    fillScrollback(state);
    ASSERT_FALSE(state.mScroll.isLive());
    EXPECT_TRUE(state.onKey(ftxui::Event::Character('x'), submit));
    EXPECT_TRUE(state.mScroll.isLive());
    EXPECT_EQ(state.mEditor.getText(), "x");
}

// UiState_EnterInScrollbackSubmitsAndReturnsToLive checks Enter in scrollback still sends the buffer.
TEST(UiState, EnterInScrollbackSubmitsAndReturnsToLive)
{
    tui::UiState state;
    Submissions  sent;
    const auto   submit { sent.asSubmitter() };
    type(state, "look", submit);
    fillScrollback(state);
    EXPECT_TRUE(state.onKey(ftxui::Event::Return, submit));
    EXPECT_TRUE(state.mScroll.isLive());
    ASSERT_EQ(sent.mLines.size(), 1U);
    EXPECT_EQ(sent.mLines[0], "look");
}

// UiState_ApplyTracksConnectionFlowAndNotices checks session events update the mirrored state.
TEST(UiState, ApplyTracksConnectionFlowAndNotices)
{
    tui::UiState state;
    state.apply(session::ConnectionEvent { .mReason = "bye", .mState = session::ConnectionState::kClosed });
    state.apply(session::FlowEvent { .mState = session::FlowState::kProfile });
    state.apply(session::NoticeEvent { .mText = "note" });
    EXPECT_EQ(state.mConnection, session::ConnectionState::kClosed);
    EXPECT_EQ(state.mCloseReason, types::OptString { "bye" });
    ASSERT_EQ(state.mNotices.size(), 1U);
    EXPECT_EQ(state.mNotices[0], "note");
}
