#include <gtest/gtest.h>

#include "editKeys.hpp"

#include <string>

namespace muckaii::tui
{
namespace
{
constexpr char kEscapeByte { '\x1b' };

// makeAlt returns the Alt+`c` chord a terminal sends: ESC then the character.
ftxui::Event makeAlt(char c)
{
    return ftxui::Event::Special(std::string { kEscapeByte, c });
}
} // namespace

// EditKeys_FindAltCharacter_ReturnsTheCharacterOfAnAltChord checks that Alt chords yield their printable character.
TEST(EditKeys, FindAltCharacter_ReturnsTheCharacterOfAnAltChord)
{
    EXPECT_EQ(findAltCharacter(makeAlt('x')), 'x');
}

// EditKeys_FindAltCharacter_IgnoresOtherEvents checks that ordinary key events are not treated as Alt chords.
TEST(EditKeys, FindAltCharacter_IgnoresOtherEvents)
{
    EXPECT_FALSE(findAltCharacter(ftxui::Event::ArrowLeft).has_value());
    EXPECT_FALSE(findAltCharacter(ftxui::Event::Character('x')).has_value());
}

// EditKeys_FindAltCharacter_RejectsAControlCharacter checks that control bytes are rejected from Alt chords.
TEST(EditKeys, FindAltCharacter_RejectsAControlCharacter)
{
    EXPECT_FALSE(findAltCharacter(makeAlt('\x01')).has_value());
}

// EditKeys_ApplyEditKey_TypesAndDeletes checks that editing keys update the line editor.
TEST(EditKeys, ApplyEditKey_TypesAndDeletes)
{
    client::LineEditor editor;
    EXPECT_TRUE(applyEditKey(editor, ftxui::Event::Character('a')));
    EXPECT_TRUE(applyEditKey(editor, ftxui::Event::Character('b')));
    EXPECT_TRUE(applyEditKey(editor, ftxui::Event::ArrowLeft));
    EXPECT_TRUE(applyEditKey(editor, ftxui::Event::Backspace));
    EXPECT_EQ(editor.getText(), "b");
}

// EditKeys_ApplyEditKey_InsertsAnAltCharacter checks that a printable Alt chord is inserted as typed text.
TEST(EditKeys, ApplyEditKey_InsertsAnAltCharacter)
{
    client::LineEditor editor;
    EXPECT_TRUE(applyEditKey(editor, makeAlt('x')));
    EXPECT_EQ(editor.getText(), "x");
}

// EditKeys_ApplyEditKey_LeavesTheEditorAloneForOtherKeys checks that unsupported keys leave the editor unchanged.
TEST(EditKeys, ApplyEditKey_LeavesTheEditorAloneForOtherKeys)
{
    client::LineEditor editor;
    EXPECT_TRUE(applyEditKey(editor, ftxui::Event::Character('a')));
    EXPECT_FALSE(applyEditKey(editor, ftxui::Event::F5));
    EXPECT_EQ(editor.getText(), "a");
}

// EditKeys_IsHomeAndIsEnd_AcceptTheirKeys checks that Home and End are recognized separately.
TEST(EditKeys, IsHomeAndIsEnd_AcceptTheirKeys)
{
    EXPECT_TRUE(isHome(ftxui::Event::Home));
    EXPECT_TRUE(isEnd(ftxui::Event::End));
    EXPECT_FALSE(isHome(ftxui::Event::End));
    EXPECT_FALSE(isEnd(ftxui::Event::Home));
}
} // namespace muckaii::tui
