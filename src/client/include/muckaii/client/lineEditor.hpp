#pragma once

#include "muckaii/base/types.hpp"

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <string_view>

namespace muckaii::client
{
// LineEditor owns the frontend command buffer and its UTF-8 cursor and recall state; each input
// event changes it in arrival order, and Enter returns the exact buffer contents. History keeps up
// to 200 trimmed, non-sensitive lines, while drafts and edited recalled lines remain available
// until Enter or Escape.
class LineEditor
{
public:
    // kHistorySize is the maximum number of non-sensitive lines retained for recall.
    static constexpr std::size_t kHistorySize { 200 };

    // insert adds typed or pasted UTF-8 text at the cursor.
    void insert(std::string_view utf8);

    // backspace removes the code point before the cursor, when present.
    void backspace();

    // deleteForward removes the code point at the cursor, when present.
    void deleteForward();

    // moveLeft places the cursor at the previous UTF-8 code point boundary.
    void moveLeft();

    // moveRight places the cursor at the next UTF-8 code point boundary.
    void moveRight();

    // moveHome places the cursor at the start of the line.
    void moveHome();

    // moveEnd places the cursor at the end of the line.
    void moveEnd();

    // recallPrevious moves toward the oldest history entry, preserving the current draft.
    void recallPrevious();

    // recallNext moves toward the draft and does nothing when already there.
    void recallNext();

    // clear empties the buffer and drops the draft and every edited recall.
    void clear();

    // submit returns the exact buffer, clears it and recall state, and stores nonblank trimmed text
    // unless `isSensitive` marks the line as a credential.
    [[nodiscard]] std::string submit(bool isSensitive);

    // getText returns the current UTF-8 line buffer.
    [[nodiscard]] const std::string& getText() const noexcept
    {
        return mText;
    }

    // getCursor returns the cursor byte offset, which always lies on a UTF-8 boundary.
    [[nodiscard]] std::size_t getCursor() const noexcept
    {
        return mCursor;
    }

    // getHistory returns stored history in oldest-to-newest order.
    [[nodiscard]] const std::deque<std::string>& getHistory() const noexcept
    {
        return mHistory;
    }

    // isRecalling reports whether the buffer currently shows a history entry.
    [[nodiscard]] bool isRecalling() const noexcept
    {
        return mRecall < mHistory.size();
    }

private:
    // leave saves edits to the history entry or draft being left.
    void leave();

    // arrive restores the edited entry, stored entry or draft at the current recall position.
    void arrive();

    // resetRecall drops draft and entry edits and positions recall after the newest entry.
    void resetRecall();

    std::string                  mText;
    std::string                  mDraft; // the unsent line, while an entry is recalled
    std::deque<std::string>      mHistory;
    std::deque<types::OptString> mEdits; // per entry: its edited copy, if edited
    std::size_t                  mCursor {};
    std::size_t                  mRecall {}; // index into mHistory; size() when not recalling
};

// countCodePoints counts UTF-8 code points by their leading bytes.
[[nodiscard]] std::size_t countCodePoints(std::string_view utf8) noexcept;
} // namespace muckaii::client
