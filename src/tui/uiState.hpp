#pragma once

#include "muckaii/base/types.hpp"
#include "muckaii/client/lineEditor.hpp"
#include "muckaii/core/core.hpp"
#include "muckaii/session/session.hpp"

#include <ftxui/component/app.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "scroll.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace muckaii::tui
{
// LineSubmitter hands one command line to the core and returns whether the core masked it.
using LineSubmitter = std::function<bool(std::string)>;

// makeSubmitter returns a LineSubmitter that sends lines to `core`.
[[nodiscard]] LineSubmitter makeSubmitter(core::Core& core);

// UiState owns the connected frontend state on the UI thread, including the command editor,
// scrollback and latest session state; core batches arrive through makePostingSink.
class UiState
{
public:
    Scroll                   mScroll;
    client::LineEditor       mEditor; // the frontend-owned line buffer (root CLAUDE.md, Invariant #1)
    std::vector<std::string> mNotices;
    types::OptString         mCloseReason;
    int                      mColumns { session::kDefaultColumns };
    session::ConnectionState mConnection { session::ConnectionState::kConnecting };
    session::FlowState       mFlow { session::FlowState::kProfile };

    // apply folds one session event into the UI state.
    void apply(const session::SessionEvent& event);

    // onKey applies one input event in arrival order and returns false only when it is not input.
    // Enter hands the current line to `submit`.
    bool onKey(const ftxui::Event& event, const LineSubmitter& submit);

    // updateColumns recomputes the effective width and reports whether it changed.
    bool updateColumns(int terminalWidth, types::OptInt maxColumns);

    // renderGame draws the scrollback, status line ending in `host`, and command box.
    [[nodiscard]] ftxui::Element renderGame(std::string_view host) const;

private:
    // onMouse handles a mouse event and consumes it so it cannot reach the command box.
    bool onMouse(const ftxui::Mouse& mouse);

    // onPageKey scrolls one page and consumes the event.
    bool onPageKey(const ftxui::Event& event);

    // onReturn submits the command buffer exactly as it stands when Enter arrives.
    bool onReturn(const LineSubmitter& submit);

    // onRecallKey handles history, Escape and ignored Tab events.
    bool onRecallKey(const ftxui::Event& event);

    // renderInput draws the unmasked command buffer and inverted cursor cell.
    [[nodiscard]] ftxui::Element renderInput() const;

    mutable std::size_t mInputStart {}; // first visible code point of the command box
};

// PostedBatchHandler handles one core event batch on the UI thread.
using PostedBatchHandler = std::function<void(const std::vector<session::SessionEvent>&)>;

// makePostingSink returns an event sink that posts each core batch to `screen`'s UI thread and
// wakes its event loop after `handler` processes the batch.
[[nodiscard]] core::EventSink makePostingSink(ftxui::App& screen, PostedBatchHandler handler);
} // namespace muckaii::tui
