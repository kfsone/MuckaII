#include <gtest/gtest.h>

#include "profileList.hpp"

namespace muckaii::tui
{
namespace
{
// makeProfile returns a saved profile named `name` with database id `id`.
persistence::Profile makeProfile(const std::string& name, std::int64_t id)
{
    persistence::Profile profile;
    profile.mId   = id;
    profile.mName = name;
    return profile;
}

// makeList returns a profile list containing three ordered test profiles.
ProfileListState makeList()
{
    ProfileListState list;
    list.reload({ makeProfile("a", 1), makeProfile("b", 2), makeProfile("c", 3) });
    return list;
}

// press sends `c` to `list` as a key event.
ListAction press(ProfileListState& list, char c)
{
    return list.onKey(ftxui::Event::Character(c));
}
} // namespace

// ProfileList_MoveSelection_WrapsAtBothEnds checks that selection wraps between the first and last profiles.
TEST(ProfileList, MoveSelection_WrapsAtBothEnds)
{
    ProfileListState list { makeList() };
    list.moveSelection(-1);
    EXPECT_EQ(list.mSelected, 2U);
    list.moveSelection(1);
    EXPECT_EQ(list.mSelected, 0U);
}

// ProfileList_MoveSelection_OnAnEmptyListDoesNothing checks that moving selection on an empty list is harmless.
TEST(ProfileList, MoveSelection_OnAnEmptyListDoesNothing)
{
    ProfileListState list;
    list.moveSelection(1);
    EXPECT_EQ(list.mSelected, 0U);
    EXPECT_EQ(list.findSelected(), nullptr);
}

// ProfileList_Reload_KeepsTheSelectionInRange checks that reloading clamps the selection to the new list size.
TEST(ProfileList, Reload_KeepsTheSelectionInRange)
{
    ProfileListState list { makeList() };
    list.mSelected = 2;
    list.reload({ makeProfile("a", 1) });
    EXPECT_EQ(list.mSelected, 0U);
}

// ProfileList_OnKey_MapsTheCommandKeys checks that Enter, e, n and q request their corresponding actions.
TEST(ProfileList, OnKey_MapsTheCommandKeys)
{
    ProfileListState list { makeList() };
    EXPECT_EQ(list.onKey(ftxui::Event::Return), ListAction::kConnect);
    EXPECT_EQ(press(list, 'e'), ListAction::kEdit);
    EXPECT_EQ(press(list, 'n'), ListAction::kNew);
    EXPECT_EQ(press(list, 'q'), ListAction::kQuit);
}

// ProfileList_OnKey_OnAnEmptyListOnlyAllowsNewAndQuit checks that an empty list only accepts the new and quit commands.
TEST(ProfileList, OnKey_OnAnEmptyListOnlyAllowsNewAndQuit)
{
    ProfileListState list;
    EXPECT_EQ(list.onKey(ftxui::Event::Return), ListAction::kNone);
    EXPECT_EQ(press(list, 'e'), ListAction::kNone);
    EXPECT_EQ(press(list, 'd'), ListAction::kNone);
    EXPECT_EQ(press(list, 'n'), ListAction::kNew);
    EXPECT_EQ(press(list, 'q'), ListAction::kQuit);
}

// ProfileList_OnKey_DeleteNeedsTwoPresses checks that deletion requires two consecutive d presses.
TEST(ProfileList, OnKey_DeleteNeedsTwoPresses)
{
    ProfileListState list { makeList() };
    EXPECT_EQ(press(list, 'd'), ListAction::kNone);
    EXPECT_EQ(list.mPendingDeleteName, "a");
    EXPECT_EQ(press(list, 'd'), ListAction::kDelete);
}

// ProfileList_OnKey_AnyOtherKeyCancelsADelete checks that any key other than d cancels pending deletion.
TEST(ProfileList, OnKey_AnyOtherKeyCancelsADelete)
{
    ProfileListState list { makeList() };
    EXPECT_EQ(press(list, 'd'), ListAction::kNone);
    EXPECT_EQ(press(list, 'x'), ListAction::kNone);
    EXPECT_FALSE(list.mPendingDeleteName.has_value());
    EXPECT_EQ(press(list, 'd'), ListAction::kNone);
}

// ProfileList_OnKey_MovingTheSelectionCancelsADelete checks that moving selection changes the profile pending deletion.
TEST(ProfileList, OnKey_MovingTheSelectionCancelsADelete)
{
    ProfileListState list { makeList() };
    EXPECT_EQ(press(list, 'd'), ListAction::kNone);
    EXPECT_EQ(list.onKey(ftxui::Event::ArrowDown), ListAction::kNone);
    EXPECT_EQ(press(list, 'd'), ListAction::kNone);
    EXPECT_EQ(list.mPendingDeleteName, "b");
}

// ProfileList_OnKey_NeverDeletesAnUnsavedProfile checks that an unsaved profile cannot be marked for deletion.
TEST(ProfileList, OnKey_NeverDeletesAnUnsavedProfile)
{
    ProfileListState     list;
    persistence::Profile unsaved;
    unsaved.mName = "x";
    list.reload({ unsaved });
    EXPECT_EQ(press(list, 'd'), ListAction::kNone);
    EXPECT_EQ(press(list, 'd'), ListAction::kNone);
}
} // namespace muckaii::tui
