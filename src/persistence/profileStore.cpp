#include "muckaii/persistence/profileStore.hpp"

#include "muckaii/persistence/wireLog.hpp"

#include <sqlite3.h>

#include <array>
#include <memory>
#include <set>
#include <string_view>

namespace muckaii::persistence
{
namespace
{
// kProfileBusyTimeoutMs bounds SQLite lock waits during synchronous profile operations.
constexpr int kProfileBusyTimeoutMs { 2000 };
// kSteps defines the profile-store migrations tracked independently from the wire log.
constexpr std::array<std::string_view, kProfileSchemaSteps> kSteps {
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
    " last_used_ms INTEGER NOT NULL);",
    // NULL means "not set" (Profile::mMaxColumns nullopt); a step-1 row reads back as nullopt.
    "ALTER TABLE profile ADD COLUMN max_columns INTEGER;",
};

// ConnectionCloser releases an owned profile-store SQLite connection.
struct ConnectionCloser
{
    // operator() closes a database through SQLite's deferred-close API.
    void operator()(sqlite3* connection) const noexcept
    {
        sqlite3_close_v2(connection);
    }
};

// StatementFinalizer releases an owned profile-store statement.
struct StatementFinalizer
{
    // operator() finalizes a prepared SQLite statement.
    void operator()(sqlite3_stmt* statement) const noexcept
    {
        sqlite3_finalize(statement);
    }
};

using ConnectionPtr = std::unique_ptr<sqlite3, ConnectionCloser>;
using StatementPtr  = std::unique_ptr<sqlite3_stmt, StatementFinalizer>;

// readRow maps the selected profile columns into a Profile value.
[[nodiscard]] Profile readRow(sqlite3_stmt* row)
{
    Profile profile;
    profile.mId = sqlite3_column_int64(row, 0);
    auto text { [row](int column)
                {
                    const auto* bytes { reinterpret_cast<const char*>(sqlite3_column_text(row, column)) };
                    return std::string { bytes != nullptr ? bytes : "" };
                } };
    profile.mName                    = text(1);
    profile.mHost                    = text(2);
    profile.mPort                    = sqlite3_column_int(row, 3);
    profile.mAccount                 = text(4);
    profile.mPassword                = text(5);
    profile.mLoginName               = text(6);
    profile.mPersonaName             = text(7);
    profile.mManagedPersonaSelection = sqlite3_column_int(row, 8) != 0;
    profile.mLastUsedMs              = sqlite3_column_int64(row, 9);
    if (sqlite3_column_type(row, 10) != SQLITE_NULL)
        profile.mMaxColumns = sqlite3_column_int(row, 10);
    return profile;
}
} // namespace

// Database owns the synchronous SQLite connection used by ProfileStore.
class ProfileStore::Database
{
public:
    // open creates or migrates the profile database and prepares its reusable statements.
    [[nodiscard]] std::string open(const std::filesystem::path& path)
    {
        std::error_code directoryError;
        std::filesystem::create_directories(path.parent_path(), directoryError);
        sqlite3*          raw {};
        const std::string name { toUtf8(path) };
        const int         code { sqlite3_open_v2(
            name.c_str(), &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) };
        mConnection.reset(raw);
        if (code != SQLITE_OK)
            return raw != nullptr ? sqlite3_errmsg(raw) : kSqliteOutOfMemoryMessage.data();
        sqlite3_busy_timeout(raw, kProfileBusyTimeoutMs);
        if (std::string error { execute("PRAGMA foreign_keys=ON;") }; ! error.empty())
            return error;
        if (std::string error { migrate() }; ! error.empty())
            return error;
        if (! prepare(mInsert,
                      "INSERT INTO profile (name, host, port, account, password, login_name, "
                      "persona_name, managed_persona_selection, last_used_ms, max_columns) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);")
            || ! prepare(mUpdate,
                         "UPDATE profile SET name = ?, host = ?, port = ?, account = ?, password = ?, "
                         "login_name = ?, persona_name = ?, managed_persona_selection = ?, "
                         "last_used_ms = ?, max_columns = ? WHERE id = ?;")
            || ! prepare(mDelete, "DELETE FROM profile WHERE id = ?;")
            || ! prepare(mTouch, "UPDATE profile SET last_used_ms = ? WHERE id = ?;")
            || ! prepare(mList,
                         "SELECT id, name, host, port, account, password, login_name, persona_name, "
                         "managed_persona_selection, last_used_ms, max_columns FROM profile ORDER BY "
                         "last_used_ms DESC, id DESC;"))
            return sqlite3_errmsg(raw);
        return {};
    }

    // list returns profiles in MRU order and records any read failure in error.
    [[nodiscard]] Profiles list(std::string& error) const
    {
        Profiles profiles;
        sqlite3_reset(mList.get());
        int result { sqlite3_step(mList.get()) };
        for (; result == SQLITE_ROW; result = sqlite3_step(mList.get()))
            profiles.push_back(readRow(mList.get()));
        if (result == SQLITE_DONE)
            error.clear();
        else
            error = sqlite3_errmsg(sqlite3_db_handle(mList.get()));
        sqlite3_reset(mList.get());
        return profiles;
    }

    // save inserts a new profile or updates the row identified by its id.
    [[nodiscard]] std::string save(Profile& profile)
    {
        if (profile.mId.has_value())
        {
            bindCommon(mUpdate.get(), profile, 1);
            sqlite3_bind_int64(mUpdate.get(), 11, *profile.mId);
            return step(mUpdate);
        }
        bindCommon(mInsert.get(), profile, 1);
        if (std::string error { step(mInsert) }; ! error.empty())
            return error;
        profile.mId = sqlite3_last_insert_rowid(mConnection.get());
        return {};
    }

    // remove deletes the row with id if it exists.
    [[nodiscard]] std::string remove(std::int64_t id)
    {
        sqlite3_bind_int64(mDelete.get(), 1, id);
        return step(mDelete);
    }

    // touch updates the MRU timestamp for id.
    [[nodiscard]] std::string touch(std::int64_t id, std::int64_t nowMs)
    {
        sqlite3_bind_int64(mTouch.get(), 1, nowMs);
        sqlite3_bind_int64(mTouch.get(), 2, id);
        return step(mTouch);
    }

    // getChanges reports how many rows the last statement changed.
    [[nodiscard]] int getChanges() const noexcept
    {
        return sqlite3_changes(mConnection.get());
    }

private:
    // execute runs one SQL statement and returns its SQLite error, if any.
    [[nodiscard]] std::string execute(const char* sql) const
    {
        char*     message {};
        const int code { sqlite3_exec(mConnection.get(), sql, nullptr, nullptr, &message) };
        if (code == SQLITE_OK)
            return {};
        std::string error { message != nullptr ? message : kSqliteUnknownErrorMessage.data() };
        sqlite3_free(message);
        return error;
    }

    // prepare compiles sql into a reusable statement.
    [[nodiscard]] bool prepare(StatementPtr& statement, const char* sql) const
    {
        sqlite3_stmt* raw {};
        const bool isPrepared { sqlite3_prepare_v2(mConnection.get(), sql, -1, &raw, nullptr) == SQLITE_OK };
        statement.reset(raw);
        return isPrepared;
    }

    // step executes a statement and clears its bindings for the next use.
    [[nodiscard]] std::string step(const StatementPtr& statement) const
    {
        const int code { sqlite3_step(statement.get()) };
        sqlite3_reset(statement.get());
        sqlite3_clear_bindings(statement.get());
        if (code != SQLITE_DONE)
            return sqlite3_errmsg(mConnection.get());
        return {};
    }

    // bindCommon stores all profile fields beginning at firstColumn.
    static void bindCommon(sqlite3_stmt* statement, const Profile& profile, int firstColumn)
    {
        int column { firstColumn };
        sqlite3_bind_text(statement, column++, profile.mName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, column++, profile.mHost.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(statement, column++, profile.mPort);
        sqlite3_bind_text(statement, column++, profile.mAccount.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, column++, profile.mPassword.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, column++, profile.mLoginName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, column++, profile.mPersonaName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(statement, column++, profile.mManagedPersonaSelection ? 1 : 0);
        sqlite3_bind_int64(statement, column++, profile.mLastUsedMs);
        if (profile.mMaxColumns.has_value())
            sqlite3_bind_int(statement, column, *profile.mMaxColumns);
        else
            sqlite3_bind_null(statement, column);
    }

    // migrate applies every profile schema step that is not already recorded.
    [[nodiscard]] std::string migrate() const
    {
        if (std::string error {
                execute("CREATE TABLE IF NOT EXISTS profile_schema_step (step INTEGER PRIMARY KEY, "
                        "applied_ms INTEGER NOT NULL);") };
            ! error.empty())
            return error;
        for (int step { 1 }; step <= kProfileSchemaSteps; ++step)
        {
            if (std::string error { execute("BEGIN IMMEDIATE;") }; ! error.empty())
                return error;
            const std::set<int> applied { readSteps() };
            if (applied.contains(step))
            {
                static_cast<void>(execute("COMMIT;"));
                continue;
            }
            std::string sql { kSteps[static_cast<std::size_t>(step - 1)] };
            sql += "INSERT INTO profile_schema_step (step, applied_ms) VALUES (";
            sql += std::to_string(step) + ", 0);";
            if (std::string error { execute(sql.c_str()) }; ! error.empty())
            {
                static_cast<void>(execute("ROLLBACK;"));
                return error;
            }
            if (std::string error { execute("COMMIT;") }; ! error.empty())
            {
                static_cast<void>(execute("ROLLBACK;"));
                return error;
            }
        }
        return {};
    }

    // readSteps returns the profile schema steps already recorded in the database.
    [[nodiscard]] std::set<int> readSteps() const
    {
        std::set<int> steps;
        StatementPtr  query;
        if (! prepare(query, "SELECT step FROM profile_schema_step;"))
            return steps;
        while (sqlite3_step(query.get()) == SQLITE_ROW)
            steps.insert(sqlite3_column_int(query.get(), 0));
        return steps;
    }

    ConnectionPtr mConnection;
    StatementPtr  mInsert;
    StatementPtr  mUpdate;
    StatementPtr  mDelete;
    StatementPtr  mTouch;
    StatementPtr  mList;
};

ProfileStore::ProfileStore(const std::filesystem::path& path)
{
    mDatabase = std::make_unique<Database>();
    mError    = mDatabase->open(path);
    if (! mError.empty())
        mDatabase.reset();
}

ProfileStore::~ProfileStore() = default;

bool ProfileStore::isOpen() const noexcept
{
    return mDatabase != nullptr;
}

const std::string& ProfileStore::getError() const noexcept
{
    return mError;
}

Profiles ProfileStore::listProfiles() const
{
    std::string discarded;
    return listProfiles(discarded);
}

Profiles ProfileStore::listProfiles(std::string& error) const
{
    error.clear();
    if (mDatabase == nullptr)
        return {};
    return mDatabase->list(error);
}

bool ProfileStore::save(Profile& profile)
{
    if (mDatabase == nullptr)
        return false;
    mError = mDatabase->save(profile);
    return mError.empty();
}

bool ProfileStore::remove(std::int64_t id)
{
    if (mDatabase == nullptr)
        return false;
    mError = mDatabase->remove(id);
    return mError.empty();
}

bool ProfileStore::touch(std::int64_t id, std::int64_t nowMs)
{
    if (mDatabase == nullptr)
        return false;
    mError = mDatabase->touch(id, nowMs);
    return mError.empty() && mDatabase->getChanges() > 0;
}
} // namespace muckaii::persistence
