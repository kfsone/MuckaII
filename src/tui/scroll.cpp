#include "scroll.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <type_traits>
#include <utility>

namespace muckaii::tui
{
namespace
{
constexpr std::size_t  kMaxLines { 5000 };
constexpr std::size_t  kTabStop { 8 };
constexpr std::uint8_t kBrightOffset { 8 };
constexpr std::uint8_t kHighFirst { 0x80 };

// One cell of a rendered row: a 7-bit character and its style.
struct Cell
{
    mud::Style mStyle;
    char       mChar {};
};

void appendRuns(std::vector<mud::Run>& to, const std::vector<mud::Run>& from)
{
    for (const mud::Run& run : from)
    {
        if (! to.empty() && to.back().mStyle == run.mStyle)
            to.back().mText.append(run.mText);
        else
            to.push_back(run);
    }
}

// Tabs expand to the next multiple of 8 columns, counted across runs (protocol.md "Hard wrap"). A
// byte 0x80-0x9A has no glyph in the terminal and shows as '?'.
std::vector<Cell> toCells(const std::vector<mud::Run>& runs)
{
    std::vector<Cell> cells;
    for (const mud::Run& run : runs)
    {
        for (const char c : run.mText)
        {
            if (c == '\t')
            {
                do
                    cells.push_back(Cell { .mStyle = run.mStyle, .mChar = ' ' });
                while (cells.size() % kTabStop != 0);
                continue;
            }
            const bool isHigh { static_cast<std::uint8_t>(c) >= kHighFirst };
            cells.push_back(Cell { .mStyle = run.mStyle, .mChar = isHigh ? '?' : c });
        }
    }
    return cells;
}

ftxui::Element styled(std::string_view text, const mud::Style& style)
{
    ftxui::Element              element { ftxui::text(text) };
    std::optional<std::uint8_t> fg { style.mFg };
    // Bold is drawn with bright promotion (ui.md 1.3).
    if (style.hasAttr(mud::Attr::kBold) && fg.has_value() && *fg < kBrightOffset)
        fg = static_cast<std::uint8_t>(*fg + kBrightOffset);
    if (fg.has_value())
        element = ftxui::color(ftxui::Color { static_cast<ftxui::Color::Palette16>(*fg) }, element);
    if (style.mBg.has_value())
        element = ftxui::bgcolor(ftxui::Color { static_cast<ftxui::Color::Palette16>(*style.mBg) }, element);
    if (style.hasAttr(mud::Attr::kBold))
        element = ftxui::bold(element);
    if (style.hasAttr(mud::Attr::kUnderline))
        element = ftxui::underlined(element);
    if (style.hasAttr(mud::Attr::kItalic))
        element = ftxui::italic(element);
    if (style.hasAttr(mud::Attr::kBlink))
        element = ftxui::blink(element);
    if (style.hasAttr(mud::Attr::kReverse))
        element = ftxui::inverted(element);
    return element;
}

// One row: maximal same-style stretches of cells.
ftxui::Element renderRow(std::span<const Cell> cells)
{
    ftxui::Elements parts;
    std::size_t     start { 0 };
    while (start < cells.size())
    {
        std::size_t end { start };
        std::string text;
        while (end < cells.size() && cells[end].mStyle == cells[start].mStyle)
            text.push_back(cells[end++].mChar);
        parts.push_back(styled(text, cells[start].mStyle));
        start = end;
    }
    return ftxui::hbox(std::move(parts));
}

std::size_t getRowCount(const std::vector<mud::Run>& runs, bool isRule, std::size_t width)
{
    if (isRule)
        return 1;
    const std::size_t cells { toCells(runs).size() };
    return std::max<std::size_t>(1, (cells + width - 1) / width);
}

// A count of columns or rows from the terminal, at least 1.
std::size_t toWidth(int count) noexcept
{
    return static_cast<std::size_t>(std::max(count, 1));
}
} // namespace

void Scroll::apply(const session::SessionEvent& event)
{
    std::visit(
        [this](const auto& item)
        {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, mud::Event>)
                applyMud(item.mPayload);
            else if constexpr (std::is_same_v<T, session::NoticeEvent>)
                addClientLine(item.mText);
            else if constexpr (std::is_same_v<T, session::ConnectionEvent>)
            {
                if (item.mState == session::ConnectionState::kClosed)
                    addClientLine("Disconnected: " + item.mReason);
            }
        },
        event);
}

void Scroll::addClientLine(std::string text)
{
    commitPending();
    commit(Line { .mRuns     = { mud::Run { .mText = std::move(text), .mStyle = {} } },
                  .mIsRule   = false,
                  .mIsClient = true });
}

void Scroll::applyMud(const mud::Payload& payload)
{
    std::visit(
        [this](const auto& event)
        {
            using T = std::decay_t<decltype(event)>;
            if constexpr (std::is_same_v<T, mud::TextEvent>)
            {
                appendRuns(mCurrent, { mud::Run { .mText = event.mText, .mStyle = event.mStyle } });
            }
            else if constexpr (std::is_same_v<T, mud::LineEndEvent>)
            {
                // A partial and the line are one line (prompt then echo); an empty line after a
                // partial adds no blank line; an empty line alone is a blank line.
                Line line;
                appendRuns(line.mRuns, mPartial);
                appendRuns(line.mRuns, mCurrent);
                mPartial.clear();
                mCurrent.clear();
                commit(std::move(line));
            }
            else if constexpr (std::is_same_v<T, mud::PromptEvent>)
            {
                // A prompt replaces the live partial wholesale.
                mPartial = event.mRuns;
            }
            else if constexpr (std::is_same_v<T, mud::PartialEvent>)
            {
                // A pre-game partial extends the live one when no line end came between them
                // (protocol.md, default: line), else replaces it, as a second Option prompt after
                // a hidden width notice does.
                if (! event.mIsContinued)
                    mPartial.clear();
                appendRuns(mPartial, event.mRuns);
            }
            else if constexpr (std::is_same_v<T, mud::ClearScreenEvent>)
            {
                // A server clear-screen leaves scrollback (input.md "Scrollback").
                returnToLive();
                commitPending();
                const bool isAfterRule { ! mLines.empty() && mLines.back().mIsRule };
                if (! mLines.empty() && ! isAfterRule)
                    commit(Line { .mRuns = {}, .mIsRule = true, .mIsClient = false });
            }
        },
        payload);
}

void Scroll::scrollUp(int rows, int columns)
{
    if (mLines.empty())
        return;
    const std::size_t width { toWidth(columns) };
    const std::size_t wanted { toWidth(rows) };
    std::uint64_t     number { mAnchor.value_or(getEndNumber() - 1) };
    std::size_t       moved { 0 };
    while (moved < wanted && number > mFirstNumber)
    {
        const Line& line { getLine(number) };
        moved += getRowCount(line.mRuns, line.mIsRule, width);
        --number;
    }
    mAnchor = number;
}

void Scroll::scrollDown(int rows, int columns)
{
    if (! mAnchor.has_value())
        return;
    const std::size_t   width { toWidth(columns) };
    const std::uint64_t last { getEndNumber() - 1 };
    const std::size_t   wanted { toWidth(rows) };
    std::uint64_t       number { *mAnchor };
    std::size_t         moved { 0 };
    while (moved < wanted && number < last)
    {
        ++number;
        const Line& line { getLine(number) };
        moved += getRowCount(line.mRuns, line.mIsRule, width);
    }
    if (number >= last)
        mAnchor.reset();
    else
        mAnchor = number;
}

void Scroll::returnToLive() noexcept
{
    mAnchor.reset();
}

const Scroll::Line& Scroll::getLine(std::uint64_t number) const noexcept
{
    return mLines[static_cast<std::size_t>(number - mFirstNumber)];
}

std::uint64_t Scroll::getEndNumber() const noexcept
{
    return mFirstNumber + mLines.size();
}

void Scroll::commit(Line line)
{
    mLines.push_back(std::move(line));
    while (mLines.size() > kMaxLines)
    {
        mLines.pop_front();
        ++mFirstNumber;
    }
    if (mAnchor.has_value() && *mAnchor < mFirstNumber)
        mAnchor = mFirstNumber;
}

void Scroll::commitPending()
{
    if (mPartial.empty() && mCurrent.empty())
        return;
    Line line;
    appendRuns(line.mRuns, mPartial);
    appendRuns(line.mRuns, mCurrent);
    mPartial.clear();
    mCurrent.clear();
    commit(std::move(line));
}

ftxui::Element Scroll::render(int columns, int rows) const
{
    const std::size_t width { toWidth(columns) };
    const std::size_t wanted { toWidth(rows) };

    // Rows from the bottom up, until the screen is full: only lines on screen are built.
    std::vector<ftxui::Element> reversed;
    const auto                  addLine {
        [&reversed, width, wanted](const std::vector<mud::Run>& runs, bool isClient)
        {
            const std::vector<Cell>     cells { toCells(runs) };
            std::vector<ftxui::Element> lineRows;
            if (cells.empty())
                lineRows.push_back(ftxui::text(""));
            for (std::size_t start { 0 }; start < cells.size(); start += width)
            {
                const ftxui::Element row { renderRow(
                    std::span { cells }.subspan(start, std::min(width, cells.size() - start))) };
                lineRows.push_back(isClient ? ftxui::dim(row) : row);
            }
            for (auto it { lineRows.rbegin() }; it != lineRows.rend() && reversed.size() < wanted; ++it)
                reversed.push_back(*it);
        }
    };

    std::uint64_t number { getEndNumber() };
    if (mAnchor.has_value())
    {
        number = *mAnchor + 1;
    }
    else
    {
        std::vector<mud::Run> live;
        appendRuns(live, mPartial);
        appendRuns(live, mCurrent);
        if (! live.empty())
            addLine(live, false);
    }
    while (number > mFirstNumber && reversed.size() < wanted)
    {
        --number;
        const Line& line { getLine(number) };
        if (line.mIsRule)
            reversed.push_back(ftxui::separator());
        else
            addLine(line.mRuns, line.mIsClient);
    }

    ftxui::Elements shown { reversed.rbegin(), reversed.rend() };
    return ftxui::vbox({ ftxui::filler(), ftxui::vbox(std::move(shown)) });
}
} // namespace muckaii::tui
