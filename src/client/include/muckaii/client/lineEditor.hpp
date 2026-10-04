#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <string_view>

namespace muckaii::client
{
// The frontend-owned line buffer and its recall list (root CLAUDE.md Invariant #1;
// docs/spec/input.md "Enter", "History", "Escape"). A frontend translates each input event into one
// call, in arrival order, and nothing else touches the buffer. Text is UTF-8; the cursor is a byte
// offset that always sits on a code point boundary.
//
// History follows input.md "History" with OPEN D2 and D3's defaults: 200 entries, oldest dropped,
// duplicates stored, Up stops at the oldest, Down stops past the newest, Down with no recall in
// progress does nothing. No key but Escape and Enter discards typed text: the unsent draft and an
// edited copy of each recalled entry are kept, and shown again on return, until Enter or Escape.
class LineEditor
{
public:
    static constexpr std::size_t kHistorySize { 200 };

    // Typed or pasted text, inserted at the cursor.
    void insert(std::string_view utf8);

    void backspace();     // deletes the code point before the cursor
    void deleteForward(); // deletes the code point at the cursor
    void moveLeft();
    void moveRight();
    void moveHome();
    void moveEnd();
    void recallPrevious(); // Up
    void recallNext();     // Down

    // Escape: the box is empty, and the draft and every edited recall are dropped.
    void clear();

    // Enter: returns the buffer exactly as it is, empties it, and ends any recall; the draft and
    // edited recalls are dropped. The line is stored in history, trimmed of spaces and tabs, unless
    // it is blank or `isSensitive` (a credential never enters the recall list).
    [[nodiscard]] std::string submit(bool isSensitive);

    [[nodiscard]] const std::string& getText() const noexcept
    {
        return mText;
    }

    [[nodiscard]] std::size_t getCursor() const noexcept
    {
        return mCursor;
    }

    [[nodiscard]] const std::deque<std::string>& getHistory() const noexcept
    {
        return mHistory;
    }

    [[nodiscard]] bool isRecalling() const noexcept
    {
        return mRecall < mHistory.size();
    }

private:
    void leave();
    void arrive();
    void resetRecall();

    std::string                            mText;
    std::string                            mDraft; // the unsent line, while an entry is recalled
    std::deque<std::string>                mHistory;
    std::deque<std::optional<std::string>> mEdits; // per entry: its edited copy, if edited
    std::size_t                            mCursor {};
    std::size_t                            mRecall {}; // index into mHistory; size() when not recalling
};

// The number of code points in UTF-8 text (a byte that is not a continuation byte starts one).
[[nodiscard]] std::size_t countCodePoints(std::string_view utf8) noexcept;
} // namespace muckaii::client
