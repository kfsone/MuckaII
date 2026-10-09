#pragma once

#include "muckaii/base/types.hpp"
#include "muckaii/persistence/profileStore.hpp"

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <cstddef>
#include <cstdint>

namespace muckaii::tui
{
// ListAction names an action the Profile List asks its owner to perform after a key.
enum class ListAction : std::uint8_t
{
    kNone,
    kConnect, // connect the selected profile
    kNew,     // open the form on a new profile
    kEdit,    // open the form on the selected profile
    kDelete,  // remove the selected profile, which the user has confirmed
    kQuit,
};

// ProfileListState owns the MRU-ordered Profile List, its selection and its pending delete.
struct ProfileListState
{
    persistence::Profiles mProfiles;
    std::size_t           mSelected {};
    types::OptString      mError;
    types::OptString      mPendingDeleteName; // set by the first d, confirmed by a second

    // reload replaces the profiles, keeps the selection in range and cancels a pending delete.
    void reload(persistence::Profiles profiles);

    // findSelected returns the selected profile, or nullptr when the list is empty.
    [[nodiscard]] const persistence::Profile* findSelected() const noexcept;

    // moveSelection moves the selection `direction` rows (+1 or -1), wrapping at either end, and
    // cancels a pending delete.
    void moveSelection(int direction) noexcept;

    // onKey applies one key and returns what the owner must do. Any key but d cancels a pending
    // delete, so a delete needs two d presses in a row on the same profile.
    [[nodiscard]] ListAction onKey(const ftxui::Event& event);

    // render draws the list, with the pending-delete prompt and the last error.
    [[nodiscard]] ftxui::Element render() const;
};
} // namespace muckaii::tui
