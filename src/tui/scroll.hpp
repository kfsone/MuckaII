#pragma once

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
// The scroll: completed lines and one live partial line, by docs/spec/protocol.md "Line model",
// rendered hard-wrapped at the column count told to the server. UI thread only.
//
// Scrollback (docs/spec/input.md "Scrollback") holds the view on one line, so new output never
// moves it. Rendering builds only the rows on screen, whatever the scrollback depth.
class Scroll
{
public:
    void apply(const session::SessionEvent& event);

    // A line the client itself writes (a notice, a disconnect).
    void addClientLine(std::string text);

    // The `rows` rows that end at the live line, or at the scrollback line.
    [[nodiscard]] ftxui::Element render(int columns, int rows) const;

    // PageUp / PageDown by `rows` rows at `columns`. Scrolling down past the newest line returns
    // to live.
    void scrollUp(int rows, int columns);
    void scrollDown(int rows, int columns);
    void returnToLive() noexcept;

    [[nodiscard]] bool isLive() const noexcept
    {
        return ! mAnchor.has_value();
    }

private:
    struct Line
    {
        std::vector<mud::Run> mRuns;
        bool                  mIsRule {};
        bool                  mIsClient {};
    };

    void applyMud(const mud::Payload& payload);
    void commit(Line line);
    void commitPending();

    [[nodiscard]] const Line& getLine(std::uint64_t number) const noexcept;
    [[nodiscard]] std::uint64_t getEndNumber() const noexcept;

    std::deque<Line>             mLines;
    std::uint64_t                mFirstNumber {}; // the line number of mLines.front()
    std::optional<std::uint64_t> mAnchor;         // in scrollback: the number of the bottom line shown
    std::vector<mud::Run>        mPartial;        // the live partial (a prompt, or pre-game text)
    std::vector<mud::Run>        mCurrent;        // text of the line being received
};
} // namespace muckaii::tui
