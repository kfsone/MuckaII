#pragma once

#include "muckaii/base/types.hpp"
#include "muckaii/session/session.hpp"

#include <ftxui/dom/elements.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace muckaii::tui
{
// Scroll stores completed lines and the live partial line, renders them hard-wrapped, and keeps
// a fixed scrollback anchor while new output arrives. It is owned by the UI thread.
class Scroll
{
public:
    // apply folds one session event into the scroll and its live line.
    void apply(const session::SessionEvent& event);

    // addClientLine appends a client-generated notice or disconnect line.
    void addClientLine(std::string text);

    // render draws up to `rows` rows ending at the live line or scrollback anchor.
    [[nodiscard]] ftxui::Element render(int columns, int rows) const;

    // scrollUp moves the scrollback anchor up by `rows` rendered rows at `columns` wide.
    void scrollUp(int rows, int columns);

    // scrollDown moves the anchor down and returns to live when it passes the newest line.
    void scrollDown(int rows, int columns);

    // returnToLive removes the scrollback anchor so rendering follows new output.
    void returnToLive() noexcept;

    // isLive reports whether rendering follows the newest output.
    [[nodiscard]] bool isLive() const noexcept
    {
        return ! mAnchor.has_value();
    }

private:
    // Line holds the runs and rendering flags for one completed scroll row group.
    struct Line
    {
        std::vector<mud::Run> mRuns;
        bool                  mIsRule {};
        bool                  mIsClient {};
    };

    // applyMud folds one MUD2 payload into the current line and scrollback.
    void applyMud(const mud::Payload& payload);

    // commit appends a completed line and enforces the scrollback limit.
    void commit(Line line);

    // commitPending commits the current partial and text runs when either is nonempty.
    void commitPending();

    // getLine returns the retained line with absolute sequence number `number`.
    [[nodiscard]] const Line& getLine(std::uint64_t number) const noexcept;

    // getEndNumber returns the absolute number one past the newest retained line.
    [[nodiscard]] std::uint64_t getEndNumber() const noexcept;

    std::deque<Line>      mLines;
    std::uint64_t         mFirstNumber {}; // the line number of mLines.front()
    types::OptUint64      mAnchor;         // in scrollback: the number of the bottom line shown
    std::vector<mud::Run> mPartial;        // the live partial (a prompt, or pre-game text)
    std::vector<mud::Run> mCurrent;        // text of the line being received
};
} // namespace muckaii::tui
