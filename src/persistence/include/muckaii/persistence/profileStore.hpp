#pragma once

#include "muckaii/base/types.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// ProfileStore keeps saved server connections and login preferences in MRU order.
namespace muckaii::persistence
{
// kDefaultLoginName selects the server shell account when a profile has no other login name.
constexpr std::string_view kDefaultLoginName { "mud" };

// Profile holds one saved server connection and its login preferences.
struct Profile
{
    types::OptInt64 mId; // nullopt: not yet saved
    std::string     mName;
    std::string     mHost;
    int             mPort {};
    std::string     mAccount;
    std::string     mPassword;
    std::string     mLoginName { kDefaultLoginName };
    std::string     mPersonaName; // empty: no persona pinned
    bool            mManagedPersonaSelection { true };
    std::int64_t    mLastUsedMs {}; // MRU order (flow.md "Profile")

    // Terminal width (connection.md 4, "Terminal width"): nullopt takes the frontend's default
    // (80); 0 is reserved to mean no server-side wrap at all (`/F+`), wrapping done by the client
    // instead; 20..160 is an explicit ceiling, same range as `-max-columns`.
    types::OptInt mMaxColumns;
};

// Profiles is an ordered collection of saved profiles.
using Profiles   = std::vector<Profile>;
using OptProfile = std::optional<Profile>;

// kProfileSchemaSteps counts profile-store migrations, independently of wire-log migrations.
constexpr int kProfileSchemaSteps { 2 };

// ProfileStore synchronously reads and writes saved profiles in the database at path.
class ProfileStore
{
public:
    // ProfileStore opens path and migrates the profile schema before accepting edits.
    explicit ProfileStore(const std::filesystem::path& path);
    // ~ProfileStore closes the synchronous database connection.
    ~ProfileStore();
    ProfileStore(const ProfileStore&)            = delete;
    ProfileStore& operator=(const ProfileStore&) = delete;
    ProfileStore(ProfileStore&&)                 = delete;
    ProfileStore& operator=(ProfileStore&&)      = delete;

    // isOpen reports whether the database was opened and migrated successfully.
    [[nodiscard]] bool isOpen() const noexcept;

    // getError returns the latest database failure message.
    [[nodiscard]] const std::string& getError() const noexcept;

    // listProfiles returns profiles from most to least recently used.
    [[nodiscard]] Profiles listProfiles() const;

    // listProfiles returns the profiles as above and sets `error` to the read failure, or clears it.
    [[nodiscard]] Profiles listProfiles(std::string& error) const;

    // save inserts or updates profile and fills its id only after a successful write.
    [[nodiscard]] bool save(Profile& profile);

    // remove deletes id and succeeds even when no row has that id.
    [[nodiscard]] bool remove(std::int64_t id);

    // touch updates a profile's MRU timestamp and fails when id does not exist.
    [[nodiscard]] bool touch(std::int64_t id, std::int64_t nowMs);

private:
    class Database;
    std::unique_ptr<Database> mDatabase;
    std::string               mError;
};
} // namespace muckaii::persistence
