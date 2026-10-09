#include "muckaii/persistence/wireReader.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string_view>

using namespace muckaii;
using namespace muckaii::persistence;

namespace
{
// kFlushWait bounds how long a test waits for the writer thread.
constexpr std::chrono::seconds kFlushWait { 5 };

// makeTempDatabase clears stale SQLite files and returns a test path.
std::filesystem::path makeTempDatabase(const std::string& name)
{
    const std::filesystem::path directory { std::filesystem::temp_directory_path()
                                            / "muckaiiWireReaderTests" };
    std::filesystem::create_directories(directory);
    const std::filesystem::path path { directory / (name + ".db") };
    std::error_code             error;
    for (const std::string suffix : { ".db", ".db-wal", ".db-shm" })
        std::filesystem::remove(directory / (name + suffix), error);
    return path;
}

// makeRecord builds a record of `direction` holding the characters of `text`.
WireRecord makeRecord(std::uint64_t sequence, WireDirection direction, std::string_view text)
{
    WireRecord record { .mSequence    = sequence,
                        .mTimestampMs = static_cast<std::int64_t>(100 + sequence),
                        .mDirection   = direction };
    for (const char c : text)
        record.mData.push_back(std::byte { static_cast<std::uint8_t>(c) });
    return record;
}

// writeTwoRuns stores a run of three records and a second run of one, and returns the path.
std::filesystem::path writeTwoRuns(const std::string& name)
{
    const std::filesystem::path path { makeTempDatabase(name) };
    WireLog                     log { path };
    log.beginRun(RunInfo { .mHost = "first", .mClientVersion = "t", .mStartedMs = 1 });
    EXPECT_TRUE(log.append(makeRecord(0, WireDirection::kReceived, "one")));
    EXPECT_TRUE(log.append(makeRecord(1, WireDirection::kSent, "two")));
    EXPECT_TRUE(log.append(makeRecord(2, WireDirection::kAnnotation, "three")));
    log.endRun(9);
    log.beginRun(RunInfo { .mHost = "second", .mClientVersion = "t", .mStartedMs = 20 });
    EXPECT_TRUE(log.append(makeRecord(0, WireDirection::kReceived, "x\0y")));
    EXPECT_TRUE(log.flush(kFlushWait));
    return path;
}
} // namespace

// WireReader_ListsRunsWithTheirRecordCounts checks run metadata and the open-run marker.
TEST(WireReader, ListsRunsWithTheirRecordCounts)
{
    WireReader reader { writeTwoRuns("listRuns") };
    ASSERT_TRUE(reader.isOpen()) << reader.getError();
    const RunSummaries runs { reader.listRuns() };
    ASSERT_EQ(runs.size(), 2U);
    EXPECT_EQ(runs[0].mHost, "first");
    EXPECT_EQ(runs[0].mRecords, 3U);
    EXPECT_EQ(runs[0].mEndedMs, types::OptInt64 { 9 });
    EXPECT_EQ(runs[1].mHost, "second");
    EXPECT_EQ(runs[1].mRecords, 1U);
    EXPECT_FALSE(runs[1].mEndedMs.has_value());
}

// WireReader_ReadsRecordsInOrderWithDirectionsAndBytes checks a full read, including an embedded NUL.
TEST(WireReader, ReadsRecordsInOrderWithDirectionsAndBytes)
{
    WireReader        reader { writeTwoRuns("readAll") };
    const WireRecords records { reader.readRecords(1) };
    ASSERT_EQ(records.size(), 3U);
    EXPECT_EQ(records[1].mDirection, WireDirection::kSent);
    EXPECT_EQ(records[2].mDirection, WireDirection::kAnnotation);
    EXPECT_EQ(records[0].mSequence, 0U);
    EXPECT_EQ(records[2].mData.size(), 5U);
    const WireRecords second { reader.readRecords(2) };
    ASSERT_EQ(second.size(), 1U);
    EXPECT_EQ(second[0].mData.size(), 1U);
}

// WireReader_SequenceRangesAreInclusive checks both bounds select their own records and open bounds stay open.
TEST(WireReader, SequenceRangesAreInclusive)
{
    WireReader reader { writeTwoRuns("range") };
    EXPECT_EQ(reader.readRecords(1, SeqRange { .mFirst = 1, .mLast = 1 }).size(), 1U);
    EXPECT_EQ(reader.readRecords(1, SeqRange { .mFirst = 1, .mLast = {} }).size(), 2U);
    EXPECT_EQ(reader.readRecords(1, SeqRange { .mFirst = {}, .mLast = 1 }).size(), 2U);
    EXPECT_TRUE(reader.readRecords(1, SeqRange { .mFirst = 5, .mLast = 9 }).empty());
    EXPECT_TRUE(reader.getError().empty());
}

// WireReader_AnUnknownRunIsAnError checks reading a run that does not exist fails loudly.
TEST(WireReader, AnUnknownRunIsAnError)
{
    WireReader reader { writeTwoRuns("noRun") };
    EXPECT_TRUE(reader.readRecords(99).empty());
    EXPECT_EQ(reader.getError(), "no run 99");
}

// WireReader_ABoundAboveInt64IsRejected checks an oversized sequence bound is an error, not a wrap.
TEST(WireReader, ABoundAboveInt64IsRejected)
{
    WireReader     reader { writeTwoRuns("bigBound") };
    const SeqRange range { .mFirst = std::numeric_limits<std::uint64_t>::max() };
    EXPECT_TRUE(reader.readRecords(1, range).empty());
    EXPECT_EQ(reader.getError(), "sequence bound out of range");
}

// WireReader_AFileThatIsNotAWireLogIsRejected checks missing files and foreign files do not open.
TEST(WireReader, AFileThatIsNotAWireLogIsRejected)
{
    const std::filesystem::path missing { makeTempDatabase("missing") };
    WireReader                  absent { missing };
    EXPECT_FALSE(absent.isOpen());
    EXPECT_FALSE(std::filesystem::exists(missing));

    const std::filesystem::path text { makeTempDatabase("text") };
    std::ofstream { text } << "this is not a database, but it is long enough to have a header to reject\n";
    WireReader foreign { text };
    EXPECT_FALSE(foreign.isOpen());
    EXPECT_FALSE(foreign.getError().empty());
}

// WireReader_ReadingDoesNotModifyTheFile checks the database bytes are unchanged by a read.
TEST(WireReader, ReadingDoesNotModifyTheFile)
{
    const std::filesystem::path path { writeTwoRuns("readonly") };
    const auto                  before { std::filesystem::file_size(path) };
    {
        WireReader reader { path };
        EXPECT_FALSE(reader.readRecords(1).empty());
    }
    EXPECT_EQ(std::filesystem::file_size(path), before);
}
