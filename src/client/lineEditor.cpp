#include "muckaii/client/lineEditor.hpp"

#include "muckaii/base/wire.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace muckaii::client
{
namespace
{

// isContinuation reports whether `c` is a UTF-8 continuation byte.
bool isContinuation(char c) noexcept
{
    return (static_cast<std::uint8_t>(c) & wire::kUtf8ContinuationMask) == wire::kUtf8Continuation;
}

// isSpace reports whether `c` is a space or tab.
bool isSpace(char c) noexcept
{
    return c == ' ' || c == '\t';
}

// trim returns `text` without leading or trailing spaces and tabs.
// isCodePointLead reports whether `c` begins a UTF-8 code point.
bool isCodePointLead(char c) noexcept
{
    return ! isContinuation(c);
}

std::string_view trim(std::string_view text) noexcept
{
    while (! text.empty() && isSpace(text.front()))
        text.remove_prefix(1);
    while (! text.empty() && isSpace(text.back()))
        text.remove_suffix(1);
    return text;
}

// getPrevious returns the preceding UTF-8 code point boundary, or zero.
std::size_t getPrevious(std::string_view text, std::size_t at) noexcept
{
    if (at == 0)
        return 0;
    --at;
    while (at > 0 && isContinuation(text[at]))
        --at;
    return at;
}

// getNext returns the following UTF-8 code point boundary, or the text size.
std::size_t getNext(std::string_view text, std::size_t at) noexcept
{
    if (at >= text.size())
        return text.size();
    ++at;
    while (at < text.size() && isContinuation(text[at]))
        ++at;
    return at;
}
} // namespace

std::size_t countCodePoints(std::string_view utf8) noexcept
{
    return static_cast<std::size_t>(std::ranges::count_if(utf8, isCodePointLead));
}

void LineEditor::insert(std::string_view utf8)
{
    mText.insert(mCursor, utf8);
    mCursor += utf8.size();
}

void LineEditor::backspace()
{
    const std::size_t start { getPrevious(mText, mCursor) };
    mText.erase(start, mCursor - start);
    mCursor = start;
}

void LineEditor::deleteForward()
{
    const std::size_t end { getNext(mText, mCursor) };
    mText.erase(mCursor, end - mCursor);
}

void LineEditor::moveLeft()
{
    mCursor = getPrevious(mText, mCursor);
}

void LineEditor::moveRight()
{
    mCursor = getNext(mText, mCursor);
}

void LineEditor::moveHome()
{
    mCursor = 0;
}

void LineEditor::moveEnd()
{
    mCursor = mText.size();
}

void LineEditor::recallPrevious()
{
    // At the oldest entry Up stays where it is, the box untouched.
    if (mRecall == 0)
        return;
    leave();
    --mRecall;
    arrive();
}

void LineEditor::recallNext()
{
    if (! isRecalling())
        return;
    leave();
    ++mRecall;
    arrive();
}

void LineEditor::clear()
{
    mText.clear();
    mCursor = 0;
    resetRecall();
}

std::string LineEditor::submit(bool isSensitive)
{
    std::string line { std::exchange(mText, {}) };
    mCursor = 0;
    const std::string_view stored { trim(line) };
    if (! stored.empty() && ! isSensitive)
    {
        mHistory.emplace_back(stored);
        if (mHistory.size() > kHistorySize)
            mHistory.pop_front();
    }
    resetRecall();
    return line;
}

// Keeps what the box holds for the place being left: the draft, or an entry's edited copy.
void LineEditor::leave()
{
    if (! isRecalling())
    {
        mDraft = mText;
        return;
    }
    if (mText == mHistory[mRecall])
        mEdits[mRecall].reset();
    else
        mEdits[mRecall] = mText;
}

// Shows the place arrived at: its edited copy if it has one, else the stored entry or the draft.
void LineEditor::arrive()
{
    if (isRecalling())
        mText = mEdits[mRecall].value_or(mHistory[mRecall]);
    else
        mText = std::exchange(mDraft, {});
    mCursor = mText.size();
}

void LineEditor::resetRecall()
{
    mDraft.clear();
    mEdits.assign(mHistory.size(), std::nullopt);
    mRecall = mHistory.size();
}
} // namespace muckaii::client
