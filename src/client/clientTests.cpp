#include "muckaii/client/client.hpp"
#include "muckaii/client/lineEditor.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using muckaii::client::countCodePoints;
using muckaii::client::LineEditor;
using muckaii::client::toSevenBit;

// SevenBitFilter_KeepsPrintableAscii checks that the filter preserves printable ASCII.
TEST(SevenBitFilter, KeepsPrintableAscii)
{
    EXPECT_EQ(toSevenBit("get all, n.e"), "get all, n.e");
}

// SevenBitFilter_TransliteratesTypographicPunctuation checks that typographic punctuation becomes its ASCII transliteration.
TEST(SevenBitFilter, TransliteratesTypographicPunctuation)
{
    // U+2019 right single quote, U+201C/U+201D double quotes, U+2014 em dash, U+2026 ellipsis.
    EXPECT_EQ(toSevenBit("don\xE2\x80\x99t \xE2\x80\x9Chi\xE2\x80\x9D \xE2\x80\x94 ok\xE2\x80\xA6"),
              "don't \"hi\" - ok...");
}

// SevenBitFilter_TabBecomesASpace checks that a TAB becomes one space.
TEST(SevenBitFilter, TabBecomesASpace)
{
    EXPECT_EQ(toSevenBit("a\tb"), "a b");
}

// SevenBitFilter_DropsEverythingElse checks that unsupported and malformed input bytes are dropped.
TEST(SevenBitFilter, DropsEverythingElse)
{
    // U+00E9 e-acute, U+1F600 an emoji, a control byte, DEL, a stray continuation byte, and 255.
    EXPECT_EQ(toSevenBit("caf\xC3\xA9 \xF0\x9F\x98\x80!\x01\x7F\x80\xFF"), "caf !");
}

// -- LineEditor (input.md; Invariant #1 point 1) -----------------------------------------------

// LineEditor_TypingAndEnterHandTheBufferOverExactly checks that Enter returns the exact buffer while history stores its trimmed form.
TEST(LineEditor, TypingAndEnterHandTheBufferOverExactly)
{
    LineEditor editor;
    editor.insert("n");
    EXPECT_EQ(editor.submit(false), "n");
    EXPECT_EQ(editor.getText(), "");
    editor.insert("ne");
    EXPECT_EQ(editor.submit(false), "ne");
    // Untrimmed for the client; trimmed of spaces and tabs in history.
    editor.insert(" \tlook \t");
    EXPECT_EQ(editor.submit(false), " \tlook \t");
    EXPECT_EQ(editor.getHistory().back(), "look");
}

// LineEditor_EditsAtTheCursorByCodePoint checks that cursor edits preserve UTF-8 code point boundaries.
TEST(LineEditor, EditsAtTheCursorByCodePoint)
{
    LineEditor editor;
    editor.insert("caf\xC3\xA9");
    editor.moveLeft();
    EXPECT_EQ(editor.getCursor(), 3U);
    editor.insert("x");
    EXPECT_EQ(editor.getText(), "cafx\xC3\xA9");
    editor.deleteForward();
    EXPECT_EQ(editor.getText(), "cafx");
    editor.moveHome();
    editor.deleteForward();
    editor.moveEnd();
    editor.backspace();
    EXPECT_EQ(editor.getText(), "af");
    editor.moveHome();
    editor.backspace();
    editor.moveLeft();
    EXPECT_EQ(editor.getText(), "af");
    EXPECT_EQ(editor.getCursor(), 0U);
    editor.moveEnd();
    editor.moveRight();
    EXPECT_EQ(editor.getCursor(), 2U);
}

// LineEditor_DownWithNoRecallKeepsTheDraft checks that Down leaves a draft unchanged when no history entry is recalled.
TEST(LineEditor, DownWithNoRecallKeepsTheDraft)
{
    LineEditor editor;
    editor.insert("north");
    static_cast<void>(editor.submit(false));
    editor.insert("kill ra");
    editor.recallNext();
    EXPECT_EQ(editor.getText(), "kill ra");
}

// LineEditor_UpThenDownRestoresTheDraft checks that Down restores the draft after recalling an entry.
TEST(LineEditor, UpThenDownRestoresTheDraft)
{
    LineEditor editor;
    editor.insert("north");
    static_cast<void>(editor.submit(false));
    editor.insert("kill ra");
    editor.moveLeft();
    editor.recallPrevious();
    EXPECT_EQ(editor.getText(), "north");
    EXPECT_EQ(editor.getCursor(), 5U);
    editor.recallNext();
    EXPECT_EQ(editor.getText(), "kill ra");
    EXPECT_FALSE(editor.isRecalling());
    editor.recallNext();
    EXPECT_EQ(editor.getText(), "kill ra");
}

// LineEditor_AnEditedRecallSurvivesMovingAway checks that an edited history entry is restored when revisited without changing stored history.
TEST(LineEditor, AnEditedRecallSurvivesMovingAway)
{
    LineEditor editor;
    for (const std::string_view line : { "north", "south" })
    {
        editor.insert(line);
        static_cast<void>(editor.submit(false));
    }
    editor.insert("draft");
    editor.recallPrevious();
    editor.insert("east");
    editor.recallPrevious();
    EXPECT_EQ(editor.getText(), "north");
    editor.backspace();
    editor.recallNext();
    EXPECT_EQ(editor.getText(), "southeast");
    editor.recallPrevious();
    EXPECT_EQ(editor.getText(), "nort");
    editor.recallNext();
    editor.recallNext();
    EXPECT_EQ(editor.getText(), "draft");
    EXPECT_EQ(editor.getHistory()[0], "north");
    EXPECT_EQ(editor.getHistory()[1], "south");
    // Enter drops every edited copy.
    static_cast<void>(editor.submit(false));
    editor.recallPrevious();
    editor.recallPrevious();
    EXPECT_EQ(editor.getText(), "south");
}

// LineEditor_HistoryClampsStoresDuplicatesAndHoldsTwoHundred checks that history keeps duplicates, clamps at the oldest entry and retains only 200 lines.
TEST(LineEditor, HistoryClampsStoresDuplicatesAndHoldsTwoHundred)
{
    LineEditor editor;
    editor.recallPrevious();
    EXPECT_EQ(editor.getText(), "");
    for (const std::string_view line : { "a", "a", "b" })
    {
        editor.insert(line);
        static_cast<void>(editor.submit(false));
    }
    for (int i { 0 }; i < 4; ++i)
        editor.recallPrevious();
    EXPECT_EQ(editor.getText(), "a");
    // Up at the oldest leaves the box alone.
    editor.insert("x");
    editor.recallPrevious();
    EXPECT_EQ(editor.getText(), "ax");
    EXPECT_EQ(editor.getHistory().size(), 3U);

    for (int i { 0 }; i < 300; ++i)
    {
        editor.insert(std::to_string(i));
        static_cast<void>(editor.submit(false));
    }
    EXPECT_EQ(editor.getHistory().size(), LineEditor::kHistorySize);
    EXPECT_EQ(editor.getHistory().front(), "100");
}

// LineEditor_BlankAndSensitiveLinesAreNotStored checks that blank and sensitive submissions stay out of recall history.
TEST(LineEditor, BlankAndSensitiveLinesAreNotStored)
{
    LineEditor editor;
    editor.insert(" \t ");
    EXPECT_EQ(editor.submit(false), " \t ");
    editor.insert("hunter22");
    EXPECT_EQ(editor.submit(true), "hunter22");
    EXPECT_TRUE(editor.getHistory().empty());
}

// LineEditor_EscapeEmptiesTheBoxAndEndsTheRecall checks that Escape clears both the buffer and recall state.
TEST(LineEditor, EscapeEmptiesTheBoxAndEndsTheRecall)
{
    LineEditor editor;
    editor.insert("north");
    static_cast<void>(editor.submit(false));
    editor.recallPrevious();
    editor.clear();
    EXPECT_EQ(editor.getText(), "");
    EXPECT_FALSE(editor.isRecalling());
}

// LineEditor_CountsCodePoints checks that UTF-8 leading bytes determine the code point count.
TEST(LineEditor, CountsCodePoints)
{
    EXPECT_EQ(countCodePoints("caf\xC3\xA9 \xF0\x9F\x98\x80"), 6U);
}

namespace
{
using CodePoints = std::vector<std::string>;

// join concatenates UTF-8 code points into their byte string.
std::string join(const CodePoints& points)
{
    std::string text;
    for (const std::string& point : points)
        text.append(point);
    return text;
}

// getBytes returns the byte offset after the first `count` code points.
std::size_t getBytes(const CodePoints& points, std::size_t count)
{
    std::size_t bytes { 0 };
    for (std::size_t i { 0 }; i < count; ++i)
        bytes += points[i].size();
    return bytes;
}

// isWhitespace reports whether `point` is a space or tab code point.
bool isWhitespace(const std::string& point)
{
    return point == " " || point == "\t";
}

// isNonWhitespace reports whether a point can make a submitted line nonblank.
bool isNonWhitespace(const std::string& point)
{
    return ! isWhitespace(point);
}

// SpecModel applies the line-editor contract independently so randomized events can compare the
// implementation against a separate history, draft and cursor model.
struct SpecModel
{
    std::vector<CodePoints> mStored;
    std::vector<CodePoints> mPlaces { CodePoints {} }; // mStored's places, then the draft
    std::size_t             mAt {};                    // the place shown
    std::size_t             mCursor {};

    // getShown returns the text at the model's current history or draft position.
    CodePoints& getShown()
    {
        return mPlaces[mAt];
    }

    // restart restores stored history and an empty draft after submission or Escape.
    void restart()
    {
        mPlaces = mStored;
        mPlaces.emplace_back();
        mAt     = mStored.size();
        mCursor = 0;
    }

    // moveTo selects a history or draft position and places its cursor at the end.
    void moveTo(std::size_t place)
    {
        mAt     = place;
        mCursor = getShown().size();
    }

    // type inserts one code point at the model cursor.
    void type(const std::string& point)
    {
        getShown().insert(getShown().begin() + static_cast<std::ptrdiff_t>(mCursor), point);
        ++mCursor;
    }

    // enter submits the current text and updates model history according to sensitivity.
    std::string enter(bool isSensitive)
    {
        const CodePoints line { getShown() };
        std::size_t      first { 0 };
        std::size_t      last { line.size() };
        while (first < last && isWhitespace(line[first]))
            ++first;
        while (last > first && isWhitespace(line[last - 1]))
            --last;
        if (first < last && ! isSensitive)
        {
            const auto begin { line.begin() + static_cast<std::ptrdiff_t>(first) };
            const auto end { line.begin() + static_cast<std::ptrdiff_t>(last) };
            mStored.emplace_back(begin, end);
            if (mStored.size() > LineEditor::kHistorySize)
                mStored.erase(mStored.begin());
        }
        restart();
        return join(line);
    }
};

// Key identifies one input event understood by the independent editor model.
enum class Key : std::uint8_t
{
    kType,
    kBackspace,
    kDelete,
    kLeft,
    kRight,
    kHome,
    kEnd,
    kUp,
    kDown,
    kEscape,
    kEnter,
};

constexpr std::array<std::string_view, 8> kPoints {
    "a", "b", "n", " ", "\t", "\xC3\xA9", "\xE2\x80\x99", "\xF0\x9F\x98\x80",
};
} // namespace

// LineEditor_RandomKeystrokesMatchTheSpecModelAtEveryEnter checks that random input sequences agree with the independent model at every Enter.
TEST(LineEditor, RandomKeystrokesMatchTheSpecModelAtEveryEnter)
{
    std::mt19937                               random { 20261004U };
    std::uniform_int_distribution<int>         keys { 0, 99 };
    std::uniform_int_distribution<std::size_t> points { 0, kPoints.size() - 1 };
    std::uniform_int_distribution<int>         sensitive { 0, 9 };

    for (int run { 0 }; run < 20; ++run)
    {
        LineEditor editor;
        SpecModel  model;
        int        enters { 0 };
        for (int step { 0 }; step < 5000; ++step)
        {
            const int roll { keys(random) };
            Key       key { Key::kType };
            if (roll >= 50)
                key = static_cast<Key>(1 + ((roll - 50) % 10));
            CodePoints& shown { model.getShown() };
            switch (key)
            {
                case Key::kType:
                {
                    const std::string point { kPoints[points(random)] };
                    editor.insert(point);
                    model.type(point);
                    break;
                }
                case Key::kBackspace:
                    editor.backspace();
                    if (model.mCursor > 0)
                    {
                        shown.erase(shown.begin() + static_cast<std::ptrdiff_t>(model.mCursor - 1));
                        --model.mCursor;
                    }
                    break;
                case Key::kDelete:
                    editor.deleteForward();
                    if (model.mCursor < shown.size())
                        shown.erase(shown.begin() + static_cast<std::ptrdiff_t>(model.mCursor));
                    break;
                case Key::kLeft:
                    editor.moveLeft();
                    model.mCursor = model.mCursor > 0 ? model.mCursor - 1 : 0;
                    break;
                case Key::kRight:
                    editor.moveRight();
                    model.mCursor = std::min(model.mCursor + 1, shown.size());
                    break;
                case Key::kHome:
                    editor.moveHome();
                    model.mCursor = 0;
                    break;
                case Key::kEnd:
                    editor.moveEnd();
                    model.mCursor = shown.size();
                    break;
                case Key::kUp:
                    editor.recallPrevious();
                    if (model.mAt > 0)
                        model.moveTo(model.mAt - 1);
                    break;
                case Key::kDown:
                    editor.recallNext();
                    if (model.mAt + 1 < model.mPlaces.size())
                        model.moveTo(model.mAt + 1);
                    break;
                case Key::kEscape:
                    editor.clear();
                    model.restart();
                    break;
                case Key::kEnter:
                {
                    const bool        isSensitive { sensitive(random) == 0 };
                    const std::string expected { model.enter(isSensitive) };
                    ASSERT_EQ(editor.submit(isSensitive), expected) << "run " << run << " step " << step;
                    ++enters;
                    break;
                }
            }
            ASSERT_EQ(editor.getText(), join(model.getShown())) << "run " << run << " step " << step;
            ASSERT_EQ(editor.getCursor(), getBytes(model.getShown(), model.mCursor))
                << "run " << run << " step " << step;
        }
        EXPECT_GT(enters, 100);
    }
}

// LineEditor_LinesConcatenateToTheKeystrokeStream checks that submitted lines concatenate to the text stream produced by the same keystrokes.
TEST(LineEditor, LinesConcatenateToTheKeystrokeStream)
{
    std::mt19937                               random { 120130U };
    std::uniform_int_distribution<int>         keys { 0, 99 };
    std::uniform_int_distribution<std::size_t> points { 0, kPoints.size() - 1 };
    LineEditor                                 editor;
    CodePoints                                 box;
    std::size_t                                cursor { 0 };
    bool                                       hasHistory {};
    std::string                                expected;
    std::string                                handed;
    for (int i { 0 }; i < 50000; ++i)
    {
        const int roll { keys(random) };
        if (roll < 60)
        {
            const std::string point { kPoints[points(random)] };
            editor.insert(point);
            box.insert(box.begin() + static_cast<std::ptrdiff_t>(cursor), point);
            ++cursor;
        }
        else if (roll < 65)
        {
            editor.backspace();
            if (cursor > 0)
                box.erase(box.begin() + static_cast<std::ptrdiff_t>(--cursor));
        }
        else if (roll < 70)
        {
            editor.deleteForward();
            if (cursor < box.size())
                box.erase(box.begin() + static_cast<std::ptrdiff_t>(cursor));
        }
        else if (roll < 75)
        {
            editor.moveLeft();
            cursor = cursor > 0 ? cursor - 1 : 0;
        }
        else if (roll < 80)
        {
            editor.moveRight();
            cursor = std::min(cursor + 1, box.size());
        }
        else if (roll < 83)
        {
            editor.moveHome();
            cursor = 0;
        }
        else if (roll < 86)
        {
            editor.moveEnd();
            cursor = box.size();
        }
        else if (roll < 94)
        {
            // Up, maybe edit the recalled line, Down: back to the line being typed.
            editor.recallPrevious();
            if (roll % 2 == 0)
                editor.insert("zz");
            editor.recallNext();
            if (hasHistory)
            {
                cursor = box.size();
            }
            else if (roll % 2 == 0)
            {
                // No history: Up and Down do nothing, so the edit lands in the box.
                box.insert(box.begin() + static_cast<std::ptrdiff_t>(cursor), { "z", "z" });
                cursor += 2;
            }
        }
        else
        {
            const std::string line { join(box) };
            expected.append(line).push_back('\n');
            handed.append(editor.submit(false)).push_back('\n');
            hasHistory = hasHistory || std::ranges::any_of(box, isNonWhitespace);
            box.clear();
            cursor = 0;
        }
        ASSERT_EQ(editor.getText(), join(box)) << i;
    }
    EXPECT_EQ(handed, expected);
}
