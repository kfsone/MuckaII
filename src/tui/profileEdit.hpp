#pragma once

#include "muckaii/base/types.hpp"
#include "muckaii/client/lineEditor.hpp"
#include "muckaii/persistence/profileStore.hpp"

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "profileForm.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace muckaii::tui
{
// ProfileEditState owns one profile form; text fields reuse LineEditor, while Login cycles through
// kLoginNames and is never freely typed.
struct ProfileEditState
{
    // kNameField identifies the first form field and editor slot.
    static constexpr std::size_t kNameField { 0 };
    // kHostField identifies the Host form field and editor slot.
    static constexpr std::size_t kHostField { 1 };

    // kLoginField identifies Login, which cycles instead of using an editor.
    static constexpr std::size_t kLoginField { 2 };

    // kAccountField identifies the Account form field and editor slot.
    static constexpr std::size_t kAccountField { 3 };

    // kPasswordField identifies the Password form field and editor slot.
    static constexpr std::size_t kPasswordField { 4 };

    // kPersonaField identifies the Persona form field and editor slot.
    static constexpr std::size_t kPersonaField { 5 };

    // kWidthField identifies the Width form field and editor slot.
    static constexpr std::size_t kWidthField { 6 };

    // kFieldCount is the number of ordered profile form fields.
    static constexpr std::size_t kFieldCount { 7 };

    persistence::Profile                         mBase; // keeps the id, MRU time and managed-persona flag
    ProfileFields                                mFields;
    std::array<client::LineEditor, kFieldCount>  mEditors; // the entry at kLoginField is never used
    mutable std::array<std::size_t, kFieldCount> mInputStarts {};
    std::size_t                                  mFocus {};
    types::OptString                             mError;
    bool                                         mIsNew {};

    // load starts editing `base`, a saved profile or, when `isNew`, a fresh one.
    void load(const persistence::Profile& base, bool isNew);

    // moveFocus moves the focus `direction` fields (+1 or -1), wrapping at either end.
    void moveFocus(int direction) noexcept;

    // trySave validates what was typed. It returns the profile to store, or an empty result with
    // mError set to the reason. The fields stay as typed either way.
    [[nodiscard]] persistence::OptProfile trySave();

    // onKey applies one key to the focused field, in arrival order, and returns false for a key the
    // form does not take (Enter and Escape belong to the caller).
    bool onKey(const ftxui::Event& event);

    // render draws the form.
    [[nodiscard]] ftxui::Element render() const;

private:
    // syncFieldsFromEditors copies editor text into its corresponding form fields.
    void syncFieldsFromEditors();

    // onFocusKey changes the focused field for Tab, Shift+Tab, Up and Down.
    bool onFocusKey(const ftxui::Event& event);

    // onLoginKey cycles Login on Left and Right without taking typed characters.
    bool onLoginKey(const ftxui::Event& event);

    // renderRow draws one form field with its label and focus marker.
    [[nodiscard]] ftxui::Element renderRow(std::size_t field) const;

    // renderValue draws the field value and its cursor when focused.
    [[nodiscard]] ftxui::Element renderValue(std::size_t field) const;
};
} // namespace muckaii::tui
