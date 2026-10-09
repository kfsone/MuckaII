#include "profileApp.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/screen/terminal.hpp>

#include "connect.hpp"
#include "numbers.hpp"
#include "profileForm.hpp"

#include <chrono>
#include <functional>
#include <string>
#include <utility>

namespace muckaii::tui
{
namespace
{
// kStartError explains why a profile connection could not start.
constexpr std::string_view kStartError { "muckaiiTui: cannot start the network reactor" };

// kInvalidProfilePrefix begins the diagnostic for an undialable saved profile.
constexpr std::string_view kInvalidProfilePrefix { "profile '" };

// kInvalidProfileSuffix describes a saved profile with an invalid endpoint.
constexpr std::string_view kInvalidProfileSuffix { "' has an invalid host or port" };

// kExitOk is the status returned after the ProfileApp exits normally.
constexpr int kExitOk { 0 };

// getWallMs returns the wall-clock milliseconds for a profile's MRU timestamp
// (persistence::Profile::mLastUsedMs).
std::int64_t getWallMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// isConnectable reports whether `profile` has a host and a port that can be dialled.
bool isConnectable(const persistence::Profile& profile)
{
    return ! profile.mHost.empty() && isValidPort(profile.mPort);
}

// makeCredentials copies a saved profile's credentials into the session request.
session::Credentials makeCredentials(const persistence::Profile& profile)
{
    session::Credentials credentials;
    credentials.mAccount   = profile.mAccount;
    credentials.mPassword  = profile.mPassword;
    credentials.mLoginName = profile.mLoginName;
    return credentials;
}

// makeRequest returns the connection request for `profile` at `columns` wide.
ConnectionRequest makeRequest(const persistence::Profile& profile, int columns, const Options& options)
{
    return ConnectionRequest {
        .mHost        = profile.mHost,
        .mPort        = static_cast<std::uint16_t>(profile.mPort),
        .mCredentials = makeCredentials(profile),
        .mMaxColumns  = profile.mMaxColumns,
        .mColumns     = columns,
        .mWireLog     = options.mWireLog,
    };
}
} // namespace

ProfileApp::ProfileApp(const Options& options, persistence::ProfileStore& store)
    : mScreen { ftxui::App::Fullscreen() }
    , mOptions { options }
    , mStore { store }
{
    mScreen.TrackMouse(true);
    reloadList();
}

int ProfileApp::run()
{
    const auto renderer { ftxui::Renderer(std::bind_front(&ProfileApp::render, this)) };
    const auto component { ftxui::CatchEvent(renderer, std::bind_front(&ProfileApp::onEvent, this)) };
    mScreen.Loop(component);
    stopCore();
    return kExitOk;
}

bool ProfileApp::onEvent(const ftxui::Event& event)
{
    if (event == ftxui::Event::Custom)
    {
        // The close reason is set by onBatch; it is acted on here, on the UI thread's own turn.
        if (mMode == AppMode::kConnected && mState.mCloseReason.has_value())
            returnToList();
        return true;
    }
    if (event == ftxui::Event::CtrlC)
    {
        stopCore();
        mScreen.Exit();
        return true;
    }
    switch (mMode)
    {
        case AppMode::kList:
            return onListKey(event);
        case AppMode::kEdit:
            return onEditKey(event);
        case AppMode::kConnected:
            return onConnectedKey(event);
    }
    return true;
}

bool ProfileApp::onListKey(const ftxui::Event& event)
{
    const persistence::Profile* const selected { mList.findSelected() };
    switch (mList.onKey(event))
    {
        case ListAction::kNone:
            break;
        case ListAction::kConnect:
            connect(*selected);
            break;
        case ListAction::kNew:
            openForm(makeNewProfile(), true);
            break;
        case ListAction::kEdit:
            openForm(*selected, false);
            break;
        case ListAction::kDelete:
            deleteSelected();
            break;
        case ListAction::kQuit:
            mScreen.Exit();
            break;
    }
    return true;
}

bool ProfileApp::onEditKey(const ftxui::Event& event)
{
    if (event == ftxui::Event::Return)
        saveForm();
    else if (event == ftxui::Event::Escape)
        mMode = AppMode::kList;
    else
        return mEdit.onKey(event);
    return true;
}

bool ProfileApp::onConnectedKey(const ftxui::Event& event)
{
    if (! mCore)
        return true;
    if (mState.mCloseReason.has_value())
    {
        returnToList();
        return true;
    }
    return mState.onKey(event, makeSubmitter(*mCore));
}

void ProfileApp::connect(const persistence::Profile& profile)
{
    if (! isConnectable(profile))
    {
        mList.mError = std::string { kInvalidProfilePrefix } + profile.mName
                       + std::string { kInvalidProfileSuffix };
        return;
    }
    mState         = UiState {};
    mHasTouchedMru = false;
    mConnected     = profile;
    const int terminalWidth { ftxui::Terminal::Size().dimx };
    mState.mColumns = session::getEffectiveColumns(terminalWidth, profile.mMaxColumns);

    mCore = buildCore(makeRequest(profile, mState.mColumns, mOptions),
                      makePostingSink(mScreen, makeBatchHandler(++mGeneration)));
    if (! mCore->start())
    {
        mCore.reset();
        mConnected.reset();
        mList.mError = std::string { kStartError };
        return;
    }
    mMode = AppMode::kConnected;
}

// reloadList refreshes the list from the store and shows a failed read as the list error.
void ProfileApp::reloadList()
{
    std::string error;
    mList.reload(mStore.listProfiles(error));
    if (! error.empty())
        mList.mError = std::move(error);
}

void ProfileApp::returnToList()
{
    ++mGeneration;
    stopCore();
    mConnected.reset();
    mList.mError.reset();
    reloadList();
    mState = UiState {};
    mMode  = AppMode::kList;
}

void ProfileApp::openForm(const persistence::Profile& base, bool isNew)
{
    mEdit.load(base, isNew);
    mMode = AppMode::kEdit;
}

void ProfileApp::saveForm()
{
    persistence::OptProfile saved { mEdit.trySave() };
    if (! saved.has_value())
        return;
    if (mStore.save(*saved))
    {
        reloadList();
        mMode = AppMode::kList;
    }
    else
        mEdit.mError = mStore.getError();
}

void ProfileApp::deleteSelected()
{
    const persistence::Profile* const selected { mList.findSelected() };
    if (selected == nullptr || ! selected->mId.has_value())
        return;
    if (! mStore.remove(*selected->mId))
    {
        mList.mError = mStore.getError();
        return;
    }
    mList.mSelected = 0;
    reloadList();
}

// makeBatchHandler returns the handler for connection generation: it drops any batch that arrives
// after that connection was replaced or ended, so a late batch never touches the next state.
PostedBatchHandler ProfileApp::makeBatchHandler(std::uint64_t generation)
{
    return [this, generation](const std::vector<session::SessionEvent>& batch)
    {
        if (generation == mGeneration && mMode == AppMode::kConnected)
            onBatch(batch);
    };
}

void ProfileApp::onBatch(const std::vector<session::SessionEvent>& batch)
{
    for (const session::SessionEvent& event : batch)
        mState.apply(event);
    touchConnectedProfile();
}

void ProfileApp::touchConnectedProfile()
{
    // Entering Pre-game (flow.md "Profile") bumps the profile's MRU order, once per connection.
    const bool isPastProfile { mState.mFlow != session::FlowState::kProfile };
    if (mHasTouchedMru || ! isPastProfile || ! mConnected.has_value() || ! mConnected->mId.has_value())
        return;
    mHasTouchedMru = true;
    static_cast<void>(mStore.touch(*mConnected->mId, getWallMs()));
}

void ProfileApp::stopCore()
{
    if (mCore)
    {
        mCore->stop();
        mCore.reset();
    }
}

ftxui::Element ProfileApp::render()
{
    switch (mMode)
    {
        case AppMode::kList:
            return mList.render();
        case AppMode::kEdit:
            return mEdit.render();
        case AppMode::kConnected:
            return renderConnected();
    }
    return ftxui::emptyElement();
}

ftxui::Element ProfileApp::renderConnected()
{
    types::OptInt maxColumns;
    std::string   host;
    if (mConnected.has_value())
    {
        maxColumns = mConnected->mMaxColumns;
        host       = joinHostPort(mConnected->mHost, mConnected->mPort);
    }
    if (mState.updateColumns(ftxui::Terminal::Size().dimx, maxColumns) && mCore)
        mCore->setColumns(mState.mColumns);
    return mState.renderGame(host);
}
} // namespace muckaii::tui
