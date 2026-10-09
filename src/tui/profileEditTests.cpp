#include <gtest/gtest.h>

#include "profileEdit.hpp"

#include <string_view>

namespace muckaii::tui
{
namespace
{
// type sends each character of `text` to `edit`.
void type(ProfileEditState& edit, std::string_view text)
{
    for (const char c : text)
        EXPECT_TRUE(edit.onKey(ftxui::Event::Character(c)));
}
} // namespace

// ProfileEdit_MoveFocus_WrapsAtBothEnds checks that focus wraps between the first and last form fields.
TEST(ProfileEdit, MoveFocus_WrapsAtBothEnds)
{
    ProfileEditState edit;
    edit.load(persistence::Profile {}, true);
    edit.moveFocus(-1);
    EXPECT_EQ(edit.mFocus, ProfileEditState::kWidthField);
    edit.moveFocus(1);
    EXPECT_EQ(edit.mFocus, ProfileEditState::kNameField);
}

// ProfileEdit_OnKey_TabAndArrowsMoveFocus checks that Tab and arrow keys move form focus.
TEST(ProfileEdit, OnKey_TabAndArrowsMoveFocus)
{
    ProfileEditState edit;
    edit.load(persistence::Profile {}, true);
    EXPECT_TRUE(edit.onKey(ftxui::Event::Tab));
    EXPECT_EQ(edit.mFocus, ProfileEditState::kHostField);
    EXPECT_TRUE(edit.onKey(ftxui::Event::ArrowUp));
    EXPECT_EQ(edit.mFocus, ProfileEditState::kNameField);
}

// ProfileEdit_OnKey_LoginFieldCyclesAndRefusesText checks that Login cycles with arrows and refuses typed characters.
TEST(ProfileEdit, OnKey_LoginFieldCyclesAndRefusesText)
{
    ProfileEditState edit;
    edit.load(persistence::Profile {}, true);
    edit.mFocus = ProfileEditState::kLoginField;
    const std::string before { edit.mFields.mLoginName };
    EXPECT_FALSE(edit.onKey(ftxui::Event::Character('z')));
    EXPECT_EQ(edit.mFields.mLoginName, before);
    EXPECT_TRUE(edit.onKey(ftxui::Event::ArrowRight));
    EXPECT_NE(edit.mFields.mLoginName, before);
    EXPECT_TRUE(edit.onKey(ftxui::Event::ArrowLeft));
    EXPECT_EQ(edit.mFields.mLoginName, before);
}

// ProfileEdit_TrySave_RejectsAnEmptyFormAndKeepsTheText checks that failed validation keeps the form text and exposes its error.
TEST(ProfileEdit, TrySave_RejectsAnEmptyFormAndKeepsTheText)
{
    ProfileEditState edit;
    edit.load(persistence::Profile {}, true);
    EXPECT_FALSE(edit.trySave().has_value());
    EXPECT_TRUE(edit.mError.has_value());
}

// ProfileEdit_TrySave_AcceptsATypedProfile checks that valid typed name and host fields produce a saved profile.
TEST(ProfileEdit, TrySave_AcceptsATypedProfile)
{
    ProfileEditState edit;
    edit.load(persistence::Profile {}, true);
    edit.mEditors[ProfileEditState::kHostField] = client::LineEditor {};
    type(edit, "home");
    EXPECT_TRUE(edit.onKey(ftxui::Event::Tab));
    type(edit, "example.org:4242");
    const persistence::OptProfile saved { edit.trySave() };
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(saved->mName, "home");
    EXPECT_EQ(saved->mHost, "example.org");
    EXPECT_EQ(saved->mPort, 4242);
    EXPECT_FALSE(edit.mError.has_value());
}

// ProfileEdit_Load_ReplacesAnEarlierFormCompletely checks that loading another form resets text, focus and validation error.
TEST(ProfileEdit, Load_ReplacesAnEarlierFormCompletely)
{
    ProfileEditState edit;
    edit.load(persistence::Profile {}, true);
    type(edit, "stale");
    static_cast<void>(edit.trySave());
    edit.mFocus = ProfileEditState::kPersonaField;
    edit.load(persistence::Profile {}, true);
    EXPECT_EQ(edit.mFocus, ProfileEditState::kNameField);
    EXPECT_FALSE(edit.mError.has_value());
    EXPECT_EQ(edit.mEditors[ProfileEditState::kNameField].getText(), "");
}
} // namespace muckaii::tui
