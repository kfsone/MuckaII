#pragma once

#include "muckaii/base/types.hpp"
#include "muckaii/core/core.hpp"
#include "muckaii/persistence/profileStore.hpp"

#include <ftxui/component/app.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "options.hpp"
#include "profileEdit.hpp"
#include "profileList.hpp"
#include "uiState.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace muckaii::tui
{
// AppMode identifies the page currently shown by ProfileApp.
enum class AppMode : std::uint8_t
{
    kList,
    kEdit,
    kConnected,
};

// ProfileApp runs the Profile List, profile editor and connected game loop; disconnect returns to
// the list, while runDirectConnect handles one-shot connections.
class ProfileApp
{
public:
    // ProfileApp binds the command-line options and profile store to a fullscreen UI.
    ProfileApp(const Options& options, persistence::ProfileStore& store);

    // run shows the app until the user quits, and returns the process exit code.
    int run();

private:
    // onEvent routes one UI event to the active page and reports whether it was consumed.
    bool onEvent(const ftxui::Event& event);

    // onListKey handles one Profile List key.
    bool onListKey(const ftxui::Event& event);

    // onEditKey handles one profile-form key.
    bool onEditKey(const ftxui::Event& event);

    // onConnectedKey handles one key while the game is connected.
    bool onConnectedKey(const ftxui::Event& event);

    // connect starts a core for `profile` and enters the connected page on success.
    void connect(const persistence::Profile& profile);

    // reloadList refreshes the Profile List from the store, surfacing a read failure.
    void reloadList();

    // returnToList stops the core and reloads the Profile List after disconnect.
    void returnToList();

    // openForm loads `base` into the editor and selects the edit page.
    void openForm(const persistence::Profile& base, bool isNew);

    // saveForm validates and saves the current profile form.
    void saveForm();

    // deleteSelected removes the selected saved profile and reloads the list.
    void deleteSelected();

    // makeBatchHandler binds one generation of core events to the connected profile update path.
    PostedBatchHandler makeBatchHandler(std::uint64_t generation);

    // onBatch applies one core event batch and updates the connected profile MRU timestamp.
    void onBatch(const std::vector<session::SessionEvent>& batch);

    // touchConnectedProfile updates the profile MRU timestamp once after entering Pre-game.
    void touchConnectedProfile();

    // stopCore stops and releases the current core, when one exists.
    void stopCore();

    // render draws the page selected by mMode.
    [[nodiscard]] ftxui::Element render();

    // renderConnected draws the game and updates the effective terminal width.
    [[nodiscard]] ftxui::Element renderConnected();

    ftxui::App                  mScreen;
    const Options&              mOptions;
    persistence::ProfileStore&  mStore;
    AppMode                     mMode { AppMode::kList };
    ProfileListState            mList;
    ProfileEditState            mEdit;
    UiState                     mState;
    std::unique_ptr<core::Core> mCore;
    persistence::OptProfile     mConnected; // a copy: the list may be reloaded while connected
    bool                        mHasTouchedMru {};
    std::uint64_t mGeneration {}; // identifies the live connection; late batches of an older one are dropped
};
} // namespace muckaii::tui
