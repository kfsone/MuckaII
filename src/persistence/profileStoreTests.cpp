#include "muckaii/persistence/profileStore.hpp"
#include "muckaii/persistence/wireLog.hpp"

#include <gtest/gtest.h>

#include <sqlite3.h>

#include <chrono>
#include <filesystem>

using namespace muckaii::persistence;

namespace
{
// makeTempDatabase clears stale SQLite files and returns a test database path.
std::filesystem::path makeTempDatabase(const std::string& name)
{
    const auto testRoot { std::filesystem::temp_directory_path() / "muckaiiProfileStoreTests" };
    const std::filesystem::path directory { testRoot };
    std::filesystem::create_directories(directory);
    const std::filesystem::path path { directory / (name + ".db") };
    std::error_code             error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(directory / (name + ".db-wal"), error);
    std::filesystem::remove(directory / (name + ".db-shm"), error);
    return path;
}

// makeProfile creates a saved-profile value with deterministic test credentials.
Profile makeProfile(std::string name, std::int64_t lastUsedMs)
{
    return Profile { .mName                    = std::move(name),
                     .mHost                    = "mud2.com",
                     .mPort                    = 6669,
                     .mAccount                 = "acc",
                     .mPassword                = "pw",
                     .mLoginName               = "mud",
                     .mPersonaName             = "",
                     .mManagedPersonaSelection = true,
                     .mLastUsedMs              = lastUsedMs };
}
} // namespace

// ProfileStore_OpensAndStartsEmpty checks a new profile database opens with no saved profiles.
TEST(ProfileStore, OpensAndStartsEmpty)
{
    ProfileStore store { makeTempDatabase("empty") };
    ASSERT_TRUE(store.isOpen()) << store.getError();
    EXPECT_TRUE(store.listProfiles().empty());
}

// ProfileStore_SaveInsertsAndFillsInTheId checks saving a new profile inserts it and returns its database id.
TEST(ProfileStore, SaveInsertsAndFillsInTheId)
{
    ProfileStore store { makeTempDatabase("insert") };
    ASSERT_TRUE(store.isOpen());
    Profile profile { makeProfile("MUD2 UK", 1) };
    EXPECT_FALSE(profile.mId.has_value());
    ASSERT_TRUE(store.save(profile)) << store.getError();
    ASSERT_TRUE(profile.mId.has_value());

    const std::vector<Profile> profiles { store.listProfiles() };
    ASSERT_EQ(profiles.size(), 1U);
    EXPECT_EQ(profiles[0].mId, profile.mId);
    EXPECT_EQ(profiles[0].mName, "MUD2 UK");
    EXPECT_EQ(profiles[0].mHost, "mud2.com");
    EXPECT_EQ(profiles[0].mPort, 6669);
    EXPECT_EQ(profiles[0].mAccount, "acc");
    EXPECT_EQ(profiles[0].mPassword, "pw");
    EXPECT_TRUE(profiles[0].mManagedPersonaSelection);
}

// ProfileStore_SaveWithAnIdUpdatesInPlace checks saving a profile with an id updates its existing row.
TEST(ProfileStore, SaveWithAnIdUpdatesInPlace)
{
    ProfileStore store { makeTempDatabase("update") };
    Profile      profile { makeProfile("MUD2 UK", 1) };
    ASSERT_TRUE(store.save(profile));

    profile.mHost = "mud2.com.other";
    profile.mPort = 1234;
    ASSERT_TRUE(store.save(profile));

    const std::vector<Profile> profiles { store.listProfiles() };
    ASSERT_EQ(profiles.size(), 1U);
    EXPECT_EQ(profiles[0].mHost, "mud2.com.other");
    EXPECT_EQ(profiles[0].mPort, 1234);
}

// ProfileStore_ListIsMostRecentlyUsedFirst checks profile listing orders rows by most recent use.
TEST(ProfileStore, ListIsMostRecentlyUsedFirst)
{
    ProfileStore store { makeTempDatabase("mru") };
    Profile      first { makeProfile("First", 1) };
    Profile      second { makeProfile("Second", 2) };
    Profile      third { makeProfile("Third", 3) };
    ASSERT_TRUE(store.save(first));
    ASSERT_TRUE(store.save(second));
    ASSERT_TRUE(store.save(third));

    std::vector<Profile> profiles { store.listProfiles() };
    ASSERT_EQ(profiles.size(), 3U);
    EXPECT_EQ(profiles[0].mName, "Third");
    EXPECT_EQ(profiles[1].mName, "Second");
    EXPECT_EQ(profiles[2].mName, "First");

    // Playing "First" moves it to the front (flow.md "Profile").
    ASSERT_TRUE(store.touch(*first.mId, 100));
    profiles = store.listProfiles();
    ASSERT_EQ(profiles.size(), 3U);
    EXPECT_EQ(profiles[0].mName, "First");
}

// ProfileStore_TouchOnAMissingIdFails checks touch rejects an id that has no profile row.
TEST(ProfileStore, TouchOnAMissingIdFails)
{
    ProfileStore store { makeTempDatabase("touchMissing") };
    EXPECT_FALSE(store.touch(999, 1));
}

// ProfileStore_RemoveDeletesTheRow checks remove deletes an existing profile and tolerates a missing id.
TEST(ProfileStore, RemoveDeletesTheRow)
{
    ProfileStore store { makeTempDatabase("remove") };
    Profile      profile { makeProfile("Gone", 1) };
    ASSERT_TRUE(store.save(profile));
    ASSERT_TRUE(store.remove(*profile.mId));
    EXPECT_TRUE(store.listProfiles().empty());
    // Removing again is not an error (ProfileStore::remove, "whether or not id existed").
    EXPECT_TRUE(store.remove(*profile.mId));
}

// ProfileStore_ProfilesSurviveReopeningTheDatabase checks saved profiles remain available after reopening the database.
TEST(ProfileStore, ProfilesSurviveReopeningTheDatabase)
{
    const std::filesystem::path path { makeTempDatabase("reopen") };
    {
        ProfileStore store { path };
        Profile      profile { makeProfile("Persisted", 1) };
        ASSERT_TRUE(store.save(profile));
    }
    ProfileStore reopened { path };
    ASSERT_TRUE(reopened.isOpen()) << reopened.getError();
    const std::vector<Profile> profiles { reopened.listProfiles() };
    ASSERT_EQ(profiles.size(), 1U);
    EXPECT_EQ(profiles[0].mName, "Persisted");
}

// ProfileStore_SharesADatabaseFileWithTheWireLog checks profile tables coexist with wire-log tables in one database.
TEST(ProfileStore, SharesADatabaseFileWithTheWireLog)
{
    // "one SQLite database holds the wire log, and any later store that joins it"
    // (persistence.md "Data directory"): opening a profile store against a wire-log-shaped file
    // must not collide with its tables.
    const std::filesystem::path path { makeTempDatabase("shared") };
    {
        muckaii::persistence::WireLog log { path };
        log.beginRun(
            muckaii::persistence::RunInfo { .mHost = "h", .mClientVersion = "test", .mStartedMs = 0 });
        log.endRun(1);
        ASSERT_TRUE(log.flush(std::chrono::seconds { 5 }));
    }
    ProfileStore store { path };
    ASSERT_TRUE(store.isOpen()) << store.getError();
    Profile profile { makeProfile("Shared", 1) };
    EXPECT_TRUE(store.save(profile));
}

// ProfileStore_ListReportsAReadFailureWithoutTouchingTheStoreError checks a failed read fills only the caller's error.
TEST(ProfileStore, ListReportsAReadFailureWithoutTouchingTheStoreError)
{
    const std::filesystem::path path { makeTempDatabase("listFailure") };
    ProfileStore                store { path };
    ASSERT_TRUE(store.isOpen()) << store.getError();
    sqlite3* raw {};
    ASSERT_EQ(sqlite3_open(path.string().c_str(), &raw), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(raw, "DROP TABLE profile;", nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(raw);

    std::string error;
    EXPECT_TRUE(store.listProfiles(error).empty());
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(store.getError().empty());
}

// ProfileStore_ListClearsTheCallersError checks a good read leaves the caller's error empty.
TEST(ProfileStore, ListClearsTheCallersError)
{
    ProfileStore store { makeTempDatabase("listClears") };
    ASSERT_TRUE(store.isOpen());
    std::string error { "stale" };
    EXPECT_TRUE(store.listProfiles(error).empty());
    EXPECT_TRUE(error.empty());
}

// ProfileStore_OpeningAPathThatCannotBeADatabaseFails checks opening a directory as a database reports failure.
TEST(ProfileStore, OpeningAPathThatCannotBeADatabaseFails)
{
    const std::filesystem::path directory { makeTempDatabase("notADatabase") };
    std::filesystem::create_directories(directory);
    ProfileStore store { directory };
    EXPECT_FALSE(store.isOpen());
    EXPECT_FALSE(store.getError().empty());
}

// ProfileStore_MaxColumnsDefaultsToNullopt checks unset terminal width remains unset after storage.
TEST(ProfileStore, MaxColumnsDefaultsToNullopt)
{
    ProfileStore store { makeTempDatabase("maxColumnsDefault") };
    Profile      profile { makeProfile("Default", 1) };
    EXPECT_FALSE(profile.mMaxColumns.has_value());
    ASSERT_TRUE(store.save(profile));
    const std::vector<Profile> profiles { store.listProfiles() };
    ASSERT_EQ(profiles.size(), 1U);
    EXPECT_FALSE(profiles[0].mMaxColumns.has_value());
}

// 0 is a real, distinct value (connection.md 4, "client-side wrap"), never conflated with nullopt.
// ProfileStore_MaxColumnsRoundTripsZeroAndAnExplicitValue checks zero and explicit terminal widths survive storage distinctly.
TEST(ProfileStore, MaxColumnsRoundTripsZeroAndAnExplicitValue)
{
    ProfileStore store { makeTempDatabase("maxColumnsValues") };
    Profile      zero { makeProfile("ClientWrap", 1) };
    zero.mMaxColumns = 0;
    Profile explicitWidth { makeProfile("Explicit", 2) };
    explicitWidth.mMaxColumns = 100;
    ASSERT_TRUE(store.save(zero));
    ASSERT_TRUE(store.save(explicitWidth));

    const std::vector<Profile> profiles { store.listProfiles() };
    ASSERT_EQ(profiles.size(), 2U);
    const Profile* zeroRow { profiles[0].mName == "ClientWrap" ? &profiles[0] : &profiles[1] };
    const Profile* explicitRow { profiles[0].mName == "Explicit" ? &profiles[0] : &profiles[1] };
    ASSERT_TRUE(zeroRow->mMaxColumns.has_value());
    EXPECT_EQ(*zeroRow->mMaxColumns, 0);
    ASSERT_TRUE(explicitRow->mMaxColumns.has_value());
    EXPECT_EQ(*explicitRow->mMaxColumns, 100);
}

// An update can clear an explicit value back to nullopt; the bind-null path (not just bind-int)
// runs on the UPDATE statement as well as INSERT.
// ProfileStore_UpdateCanClearMaxColumnsBackToNullopt checks an update clears a previously stored terminal width.
TEST(ProfileStore, UpdateCanClearMaxColumnsBackToNullopt)
{
    ProfileStore store { makeTempDatabase("maxColumnsClear") };
    Profile      profile { makeProfile("Clearing", 1) };
    profile.mMaxColumns = 120;
    ASSERT_TRUE(store.save(profile));
    profile.mMaxColumns = std::nullopt;
    ASSERT_TRUE(store.save(profile));

    const std::vector<Profile> profiles { store.listProfiles() };
    ASSERT_EQ(profiles.size(), 1U);
    EXPECT_FALSE(profiles[0].mMaxColumns.has_value());
}

// A database written by schema step 1 (no max_columns column) upgrades forward: step 2 adds the
// column, existing rows read back with mMaxColumns nullopt, and saving still works afterwards
// (persistence.md "Schema forward-compatibility").
// ProfileStore_MigratesForwardFromSchemaStepOne checks step-one profiles migrate forward and remain writable.
TEST(ProfileStore, MigratesForwardFromSchemaStepOne)
{
    const std::filesystem::path path { makeTempDatabase("migrateFromV1") };
    {
        sqlite3* raw {};
        ASSERT_EQ(sqlite3_open(path.string().c_str(), &raw), SQLITE_OK);
        // kV1Schema reproduces the profile database shape before max_columns was added.
        const char* const kV1Schema {
            "CREATE TABLE profile ("
            " id INTEGER PRIMARY KEY,"
            " name TEXT NOT NULL,"
            " host TEXT NOT NULL,"
            " port INTEGER NOT NULL,"
            " account TEXT NOT NULL,"
            " password TEXT NOT NULL,"
            " login_name TEXT NOT NULL,"
            " persona_name TEXT NOT NULL,"
            " managed_persona_selection INTEGER NOT NULL,"
            " last_used_ms INTEGER NOT NULL);"
            "CREATE TABLE profile_schema_step (step INTEGER PRIMARY KEY, applied_ms INTEGER NOT NULL);"
            "INSERT INTO profile_schema_step (step, applied_ms) VALUES (1, 0);"
            "INSERT INTO profile (name, host, port, account, password, login_name, persona_name, "
            "managed_persona_selection, last_used_ms) VALUES "
            "('Old', 'mud2.com', 6669, 'acc', 'pw', 'mud', '', 1, 1);"
        };
        char* message {};
        ASSERT_EQ(sqlite3_exec(raw, kV1Schema, nullptr, nullptr, &message), SQLITE_OK)
            << (message != nullptr ? message : "");
        sqlite3_close(raw);
    }

    ProfileStore store { path };
    ASSERT_TRUE(store.isOpen()) << store.getError();
    std::vector<Profile> profiles { store.listProfiles() };
    ASSERT_EQ(profiles.size(), 1U);
    EXPECT_EQ(profiles[0].mName, "Old");
    EXPECT_FALSE(profiles[0].mMaxColumns.has_value());

    // The migrated store is fully functional: a new save and an update of the old row both work.
    profiles[0].mMaxColumns = 40;
    ASSERT_TRUE(store.save(profiles[0]));
    Profile fresh { makeProfile("New", 2) };
    fresh.mMaxColumns = 0;
    ASSERT_TRUE(store.save(fresh));

    const std::vector<Profile> reread { store.listProfiles() };
    ASSERT_EQ(reread.size(), 2U);
    const Profile* oldRow { reread[0].mName == "Old" ? &reread[0] : &reread[1] };
    const Profile* newRow { reread[0].mName == "New" ? &reread[0] : &reread[1] };
    ASSERT_TRUE(oldRow->mMaxColumns.has_value());
    EXPECT_EQ(*oldRow->mMaxColumns, 40);
    ASSERT_TRUE(newRow->mMaxColumns.has_value());
    EXPECT_EQ(*newRow->mMaxColumns, 0);
}
