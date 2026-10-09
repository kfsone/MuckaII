#include "profileEdit.hpp"

#include "editKeys.hpp"
#include "inputView.hpp"

#include <vector>

namespace muckaii::tui
{
namespace
{
using FieldText = std::string ProfileFields::*;

// kFieldTexts maps editor slots to their corresponding ProfileFields members.
constexpr std::array<FieldText, ProfileEditState::kFieldCount> kFieldTexts {
    &ProfileFields::mName,           &ProfileFields::mHostPort, &ProfileFields::mLoginName,
    &ProfileFields::mAccount,        &ProfileFields::mPassword, &ProfileFields::mPersonaName,
    &ProfileFields::mMaxColumnsText,
};

// kFieldLabels gives the visible label for each ordered profile form field.
constexpr std::array<std::string_view, ProfileEditState::kFieldCount> kFieldLabels {
    "Name", "Host", "Login", "Account", "Password", "Persona", "Width",
};

// kValueCells is the text width available to a focused form field.
constexpr std::size_t kValueCells { 40 };

// kLabelCells is the fixed width reserved for each form field label.
constexpr int kLabelCells { 10 };

// kNewTitle labels a form creating an unsaved profile.
constexpr std::string_view kNewTitle { "New profile" };

// kEditTitle labels a form changing a saved profile.
constexpr std::string_view kEditTitle { "Edit profile" };

// kFocusMarker marks the currently focused form field.
constexpr std::string_view kFocusMarker { "> " };

// kNoMarker reserves the same label space when a field is not focused.
constexpr std::string_view kNoMarker { "  " };

// kLoginLeftMarker marks the left side of the focused Login choice.
constexpr std::string_view kLoginLeftMarker { "< " };

// kLoginRightMarker marks the right side of the focused Login choice.
constexpr std::string_view kLoginRightMarker { " >" };

// kHelpLine lists the form keys and their actions.
constexpr std::string_view kHelpLine {
    "Enter: save  Esc: cancel  Tab/Up/Down: move  Left/Right on Login: cycle account"
};

// kNextField advances focus to the following form field.
constexpr int kNextField { 1 };

// kPreviousField moves focus to the preceding form field.
constexpr int kPreviousField { -1 };

// wrapIndex returns `index` moved by `direction`, wrapped into [0, count).
std::size_t wrapIndex(std::size_t index, int direction, std::size_t count) noexcept
{
    const int size { static_cast<int>(count) };
    const int moved { static_cast<int>(index) + direction };
    return static_cast<std::size_t>(((moved % size) + size) % size);
}
} // namespace

void ProfileEditState::load(const persistence::Profile& base, bool isNew)
{
    mBase   = base;
    mFields = toFields(base);
    mIsNew  = isNew;
    mFocus  = 0;
    mError.reset();
    mEditors     = {};
    mInputStarts = {};
    for (std::size_t field {}; field < kFieldCount; ++field)
    {
        if (field != kLoginField)
            mEditors[field].insert(mFields.*kFieldTexts[field]);
    }
}

void ProfileEditState::moveFocus(int direction) noexcept
{
    mFocus = wrapIndex(mFocus, direction, kFieldCount);
}

persistence::OptProfile ProfileEditState::trySave()
{
    syncFieldsFromEditors();
    ProfileResult result { fromFields(mFields, mBase) };
    if (! result.mProfile.has_value())
        mError = std::move(result.mError);
    else
        mError.reset();
    return std::move(result.mProfile);
}

bool ProfileEditState::onKey(const ftxui::Event& event)
{
    if (onFocusKey(event))
        return true;
    if (mFocus == kLoginField)
        return onLoginKey(event);
    return applyEditKey(mEditors[mFocus], event);
}

ftxui::Element ProfileEditState::render() const
{
    std::vector<ftxui::Element> rows;
    rows.push_back(ftxui::text(std::string { mIsNew ? kNewTitle : kEditTitle }) | ftxui::bold);
    for (std::size_t field {}; field < kFieldCount; ++field)
        rows.push_back(renderRow(field));
    if (mError.has_value())
        rows.push_back(ftxui::text(*mError) | ftxui::color(ftxui::Color::Red));
    rows.push_back(ftxui::text(" "));
    rows.push_back(ftxui::text(std::string { kHelpLine }));
    return ftxui::vbox(rows);
}

// syncFieldsFromEditors copies every editor's text into mFields; Login is already there.
void ProfileEditState::syncFieldsFromEditors()
{
    for (std::size_t field {}; field < kFieldCount; ++field)
    {
        if (field != kLoginField)
            mFields.*kFieldTexts[field] = mEditors[field].getText();
    }
}

// onFocusKey moves the focus on Tab, Shift+Tab, Down and Up.
bool ProfileEditState::onFocusKey(const ftxui::Event& event)
{
    if (event == ftxui::Event::Tab || event == ftxui::Event::ArrowDown)
        moveFocus(kNextField);
    else if (event == ftxui::Event::TabReverse || event == ftxui::Event::ArrowUp)
        moveFocus(kPreviousField);
    else
        return false;
    return true;
}

// onLoginKey cycles the login name on Left and Right. Every other key is not the Login field's.
bool ProfileEditState::onLoginKey(const ftxui::Event& event)
{
    if (event == ftxui::Event::ArrowLeft)
        mFields.mLoginName = std::string { cycleLoginName(mFields.mLoginName, kPreviousField) };
    else if (event == ftxui::Event::ArrowRight)
        mFields.mLoginName = std::string { cycleLoginName(mFields.mLoginName, kNextField) };
    else
        return false;
    return true;
}

// renderRow draws one field: the focus marker, its label and its value.
ftxui::Element ProfileEditState::renderRow(std::size_t field) const
{
    const std::string_view marker { field == mFocus ? kFocusMarker : kNoMarker };
    const std::string      label { std::string { kFieldLabels[field] } + ": " };
    return ftxui::hbox({
        ftxui::text(std::string { marker }),
        ftxui::text(label) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kLabelCells),
        renderValue(field),
    });
}

// renderValue draws a field's value: Login with arrows when focused, a focused editor with its
// cursor cell inverted, or an unfocused editor as plain text.
ftxui::Element ProfileEditState::renderValue(std::size_t field) const
{
    const bool isFocused { field == mFocus };
    if (field == kLoginField)
    {
        const std::string& login { mFields.mLoginName };
        if (isFocused)
            return ftxui::text(std::string { kLoginLeftMarker } + login + std::string { kLoginRightMarker });
        return ftxui::text(login);
    }
    const client::LineEditor& editor { mEditors[field] };
    if (! isFocused)
        return ftxui::text(editor.getText());
    const InputView view { makeInputView(
        editor.getText(), editor.getCursor(), kValueCells, mInputStarts[field]) };
    return ftxui::hbox({ ftxui::text(view.mBefore),
                         ftxui::text(view.mAtCursor) | ftxui::inverted,
                         ftxui::text(view.mAfter) });
}
} // namespace muckaii::tui
