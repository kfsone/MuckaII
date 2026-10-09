#include "profileList.hpp"

#include "profileForm.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace muckaii::tui
{
namespace
{
// kTitle names the saved-profile page.
constexpr std::string_view kTitle { "MuckaII profiles" };

// kEmptyHint tells how to create the first saved profile.
constexpr std::string_view kEmptyHint { "No profiles yet - press 'n' to add one." };

// kHelpLine lists the keys accepted by the Profile List.
constexpr std::string_view kHelpLine { "Enter: connect  n: new  e: edit  d: delete  q: quit" };

// kColumnGap separates profile name, endpoint and account columns.
constexpr std::string_view kColumnGap { "   " };

// kDeletePromptPrefix begins the confirmation shown for the selected saved profile.
constexpr std::string_view kDeletePromptPrefix { "Press 'd' again to delete '" };

// kDeletePromptSuffix explains that any other key cancels the pending deletion.
constexpr std::string_view kDeletePromptSuffix { "'; any other key cancels" };

// kNextRow is the selection step for Down.
constexpr int kNextRow { 1 };

// kPreviousRow is the selection step for Up.
constexpr int kPreviousRow { -1 };

// describe returns a profile's list row: its name, host and port, and its account when it has one.
std::string describe(const persistence::Profile& profile)
{
    std::string line { profile.mName };
    line += kColumnGap;
    line += joinHostPort(profile.mHost, profile.mPort);
    if (! profile.mAccount.empty())
    {
        line += kColumnGap;
        line += profile.mAccount;
    }
    return line;
}

// makeRedText returns `text` in red, for the prompts and errors.
ftxui::Element makeRedText(const std::string& text)
{
    return ftxui::text(text) | ftxui::color(ftxui::Color::Red);
}
} // namespace

void ProfileListState::reload(persistence::Profiles profiles)
{
    mProfiles = std::move(profiles);
    mSelected = mProfiles.empty() ? 0 : std::min(mSelected, mProfiles.size() - 1);
    mPendingDeleteName.reset();
}

const persistence::Profile* ProfileListState::findSelected() const noexcept
{
    return mSelected < mProfiles.size() ? &mProfiles[mSelected] : nullptr;
}

void ProfileListState::moveSelection(int direction) noexcept
{
    if (mProfiles.empty())
        return;
    const int count { static_cast<int>(mProfiles.size()) };
    const int next { ((static_cast<int>(mSelected) + direction) % count + count) % count };
    mSelected = static_cast<std::size_t>(next);
    mPendingDeleteName.reset();
}

ListAction ProfileListState::onKey(const ftxui::Event& event)
{
    const bool isDelete { event == ftxui::Event::Character('d') };
    if (! isDelete)
        mPendingDeleteName.reset();

    const persistence::Profile* const selected { findSelected() };
    if (event == ftxui::Event::ArrowUp)
        moveSelection(kPreviousRow);
    else if (event == ftxui::Event::ArrowDown)
        moveSelection(kNextRow);
    else if (event == ftxui::Event::Character('n'))
        return ListAction::kNew;
    else if (event == ftxui::Event::Character('q'))
        return ListAction::kQuit;
    else if (selected == nullptr)
        return ListAction::kNone;
    else if (event == ftxui::Event::Return)
        return ListAction::kConnect;
    else if (event == ftxui::Event::Character('e'))
        return ListAction::kEdit;
    else if (isDelete)
    {
        const bool isConfirmed { mPendingDeleteName == selected->mName && selected->mId.has_value() };
        mPendingDeleteName = selected->mName;
        return isConfirmed ? ListAction::kDelete : ListAction::kNone;
    }
    return ListAction::kNone;
}

ftxui::Element ProfileListState::render() const
{
    std::vector<ftxui::Element> rows;
    rows.push_back(ftxui::text(std::string { kTitle }) | ftxui::bold);
    if (mProfiles.empty())
        rows.push_back(ftxui::text(std::string { kEmptyHint }));
    for (std::size_t row {}; row < mProfiles.size(); ++row)
    {
        const ftxui::Element line { ftxui::text(describe(mProfiles[row])) };
        rows.push_back(row == mSelected ? line | ftxui::inverted : line);
    }
    if (mPendingDeleteName.has_value())
    {
        std::string prompt { kDeletePromptPrefix };
        prompt += *mPendingDeleteName;
        prompt += kDeletePromptSuffix;
        rows.push_back(makeRedText(prompt));
    }
    if (mError.has_value())
        rows.push_back(makeRedText(*mError));
    rows.push_back(ftxui::text(" "));
    rows.push_back(ftxui::text(std::string { kHelpLine }));
    return ftxui::vbox(rows);
}
} // namespace muckaii::tui
