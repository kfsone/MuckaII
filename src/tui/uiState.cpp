#include "uiState.hpp"

#include <ftxui/screen/terminal.hpp>

#include "editKeys.hpp"
#include "inputView.hpp"

#include <algorithm>
#include <memory>

namespace muckaii::tui
{
namespace
{
// kChromeRows counts the status and command rows outside the game scrollback.
constexpr int kChromeRows { 2 };

// kWheelRows counts the scrollback rows moved by one wheel notch.
constexpr int kWheelRows { 3 };

// kPrompt is the command-box prefix shown before the editable line.
constexpr std::string_view kPrompt { "> " };

// kPromptCells counts the terminal cells occupied by kPrompt.
constexpr int kPromptCells { static_cast<int>(kPrompt.size()) };

// kStatusSeparator separates status-line values.
constexpr std::string_view kStatusSeparator { " | " };

// kScrollbackNotice identifies the status shown while the view is anchored in history.
constexpr std::string_view kScrollbackNotice { "SCROLLBACK - Esc returns to live" };

// getConnectionName returns the word the status line shows for `state`.
std::string_view getConnectionName(session::ConnectionState state) noexcept
{
    switch (state)
    {
        case session::ConnectionState::kConnecting:
            return "connecting";
        case session::ConnectionState::kConnected:
            return "connected";
        case session::ConnectionState::kClosed:
            return "closed";
    }
    return "";
}

// getFlowName returns the word the status line shows for `state`.
std::string_view getFlowName(session::FlowState state) noexcept
{
    switch (state)
    {
        case session::FlowState::kProfile:
            return "Profile";
        case session::FlowState::kPregame:
            return "Pre-game";
        case session::FlowState::kGame:
            return "Game";
    }
    return "";
}

// makeStatusText returns the status line: connection, flow state, columns, `host`, and a reminder
// while the view is scrolled back.
std::string makeStatusText(const UiState& state, std::string_view host)
{
    std::string text { " " };
    text += getConnectionName(state.mConnection);
    text += kStatusSeparator;
    text += getFlowName(state.mFlow);
    text += kStatusSeparator;
    text += std::to_string(state.mColumns) + " columns";
    text += kStatusSeparator;
    text += host;
    if (! state.mScroll.isLive())
    {
        text += kStatusSeparator;
        text += kScrollbackNotice;
    }
    return text;
}

// isPageKey reports whether `event` is PageUp or PageDown.
bool isPageKey(const ftxui::Event& event)
{
    return event == ftxui::Event::PageUp || event == ftxui::Event::PageDown;
}
} // namespace

LineSubmitter makeSubmitter(core::Core& core)
{
    return [&core](std::string line)
    {
        return core.submitLine(std::move(line));
    };
}

void UiState::apply(const session::SessionEvent& event)
{
    if (const auto* connection { std::get_if<session::ConnectionEvent>(&event) }; connection != nullptr)
    {
        mConnection = connection->mState;
        if (connection->mState == session::ConnectionState::kClosed)
            mCloseReason = connection->mReason;
    }
    if (const auto* flow { std::get_if<session::FlowEvent>(&event) }; flow != nullptr)
        mFlow = flow->mState;
    if (const auto* notice { std::get_if<session::NoticeEvent>(&event) }; notice != nullptr)
        mNotices.push_back(notice->mText);
    mScroll.apply(event);
}

bool UiState::onKey(const ftxui::Event& event, const LineSubmitter& submit)
{
    if (event.is_mouse())
    {
        ftxui::Event copy { event }; // Event::mouse() is not const
        return onMouse(copy.mouse());
    }
    if (isPageKey(event))
        return onPageKey(event);
    // In scrollback, Escape and End return to live (input.md "Scrollback"); every other key
    // returns to live and then acts, so no key is swallowed (OPEN D4, default).
    if (! mScroll.isLive() && (event == ftxui::Event::Escape || isEnd(event)))
    {
        mScroll.returnToLive();
        return true;
    }
    if (event == ftxui::Event::Return)
        return onReturn(submit);
    if (applyEditKey(mEditor, event) || onRecallKey(event))
    {
        mScroll.returnToLive();
        return true;
    }
    return false;
}

bool UiState::updateColumns(int terminalWidth, types::OptInt maxColumns)
{
    const int columns { session::getEffectiveColumns(terminalWidth, maxColumns) };
    if (columns == mColumns)
        return false;
    mColumns = columns;
    return true;
}

ftxui::Element UiState::renderGame(std::string_view host) const
{
    const int rows { std::max(ftxui::Terminal::Size().dimy - kChromeRows, 1) };
    return ftxui::vbox({
        mScroll.render(mColumns, rows) | ftxui::flex,
        ftxui::text(makeStatusText(*this, host)) | ftxui::inverted,
        renderInput(),
    });
}

// onMouse scrolls the scrollback by kWheelRows per wheel notch. Every mouse event is consumed, so
// none reaches the command box.
bool UiState::onMouse(const ftxui::Mouse& mouse)
{
    if (mouse.button == ftxui::Mouse::WheelUp)
        mScroll.scrollUp(kWheelRows, mColumns);
    else if (mouse.button == ftxui::Mouse::WheelDown)
        mScroll.scrollDown(kWheelRows, mColumns);
    return true;
}

// onPageKey scrolls the scrollback one page, a screenful less the status line and one row of
// overlap.
bool UiState::onPageKey(const ftxui::Event& event)
{
    const int page { std::max(ftxui::Terminal::Size().dimy - kChromeRows - 1, 1) };
    if (event == ftxui::Event::PageUp)
        mScroll.scrollUp(page, mColumns);
    else
        mScroll.scrollDown(page, mColumns);
    return true;
}

// onReturn submits the command box's line exactly as it stands when Enter arrives.
bool UiState::onReturn(const LineSubmitter& submit)
{
    mScroll.returnToLive();
    // Core alone says whether the line is masked; the recall list follows its verdict.
    const bool isMasked { submit(mEditor.getText()) };
    static_cast<void>(mEditor.submit(isMasked));
    return true;
}

// onRecallKey handles the command box's keys beyond plain editing: Up and Down recall, Escape
// clears, and Tab does nothing (OPEN D9, default).
bool UiState::onRecallKey(const ftxui::Event& event)
{
    if (event == ftxui::Event::ArrowUp)
        mEditor.recallPrevious();
    else if (event == ftxui::Event::ArrowDown)
        mEditor.recallNext();
    else if (event == ftxui::Event::Escape)
        mEditor.clear();
    else
        return event == ftxui::Event::Tab || event == ftxui::Event::TabReverse;
    return true;
}

// renderInput draws the command box as typed (never masked: input.md), the cursor cell inverted.
ftxui::Element UiState::renderInput() const
{
    const int         columns { ftxui::Terminal::Size().dimx };
    const std::size_t width { static_cast<std::size_t>(std::max(columns - kPromptCells, 1)) };
    const InputView   view { makeInputView(mEditor.getText(), mEditor.getCursor(), width, mInputStart) };
    return ftxui::hbox({ ftxui::text(std::string { kPrompt }),
                         ftxui::text(view.mBefore),
                         ftxui::text(view.mAtCursor) | ftxui::inverted,
                         ftxui::text(view.mAfter) });
}

namespace
{
using SharedBatch = std::shared_ptr<std::vector<session::SessionEvent>>;

// BatchTask runs a handler over one batch on the UI thread.
class BatchTask
{
public:
    // BatchTask pairs `handler` with the `batch` it will receive.
    BatchTask(PostedBatchHandler handler, SharedBatch batch)
        : mHandler { std::move(handler) }
        , mBatch { std::move(batch) }
    {
    }

    // operator() hands the batch to the handler.
    void operator()() const
    {
        mHandler(*mBatch);
    }

private:
    PostedBatchHandler mHandler;
    SharedBatch        mBatch;
};

// PostingSink posts each core batch to the screen's UI thread and wakes its loop.
class PostingSink
{
public:
    // PostingSink posts to `screen`; each batch goes to `handler`.
    PostingSink(ftxui::App& screen, PostedBatchHandler handler)
        : mScreen { &screen }
        , mHandler { std::move(handler) }
    {
    }

    // operator() posts `batch` to the UI thread, then wakes the loop.
    void operator()(std::vector<session::SessionEvent> batch) const
    {
        SharedBatch shared { std::make_shared<std::vector<session::SessionEvent>>(std::move(batch)) };
        mScreen->Post(BatchTask { mHandler, std::move(shared) });
        mScreen->PostEvent(ftxui::Event::Custom);
    }

private:
    ftxui::App*        mScreen;
    PostedBatchHandler mHandler;
};
} // namespace

core::EventSink makePostingSink(ftxui::App& screen, PostedBatchHandler handler)
{
    return PostingSink { screen, std::move(handler) };
}
} // namespace muckaii::tui
