#include "muckaii/base/wire.hpp"

#include <gtest/gtest.h>

#include "wireAudit.hpp"
#include "wireCheckOptions.hpp"

#include <string>
#include <string_view>
#include <vector>

using namespace muckaii;

namespace
{
// kFirstHeaderByte is the wire byte of header number 0 (docs/spec/protocol.md "Control characters").
constexpr std::uint8_t kFirstHeaderByte { 0x9B };

// kUnlistedNumber is a header number that no code-table row uses at the top level.
constexpr std::uint8_t kUnlistedNumber { 77 };

// makeRecord builds a stored record of `direction` holding the characters of `text`.
persistence::WireRecord makeRecord(persistence::WireDirection direction, std::string_view text)
{
    persistence::WireRecord record { .mDirection = direction };
    for (const char c : text)
        record.mData.push_back(std::byte { static_cast<std::uint8_t>(c) });
    return record;
}

// appendHeader appends the wire bytes of a one-number C1 header to `record`.
void appendHeader(persistence::WireRecord& record, std::uint8_t number)
{
    record.mData.push_back(std::byte { static_cast<std::uint8_t>(kFirstHeaderByte + number) });
    record.mData.push_back(std::byte { wire::kTelnetIac });
    record.mData.push_back(std::byte { wire::kTelnetIac });
}

// parse runs the option parser over `args`.
std::optional<tools::WireCheckOptions> parse(const std::vector<std::string>& args, std::string& error)
{
    return tools::parseWireCheckArguments(args, error);
}
} // namespace

// WireAudit_ReportsRecordsAndEvents checks the counts of a plain text run.
TEST(WireAudit, ReportsRecordsAndEvents)
{
    const std::vector<persistence::WireRecord> records {
        makeRecord(persistence::WireDirection::kReceived, "hello\r\n"),
        makeRecord(persistence::WireDirection::kSent, "look\r\n"),
        makeRecord(persistence::WireDirection::kAnnotation, "note"),
    };
    const tools::RunAudit audit { tools::auditRun(7, records) };
    EXPECT_EQ(audit.mRun, 7);
    EXPECT_EQ(audit.mReceivedRecords, 1U);
    EXPECT_EQ(audit.mSentRecords, 1U);
    EXPECT_EQ(audit.mAnnotations, 1U);
    EXPECT_EQ(audit.mReceivedBytes, 7U);
    EXPECT_EQ(audit.mTextEvents, 1U);
    EXPECT_EQ(audit.mLineEnds, 1U);
    EXPECT_TRUE(audit.isClean());
}

// WireAudit_ASequenceGapIsAFinding checks a skipped sequence number makes the run need a look.
TEST(WireAudit, ASequenceGapIsAFinding)
{
    std::vector<persistence::WireRecord> records {
        makeRecord(persistence::WireDirection::kReceived, "a"),
        makeRecord(persistence::WireDirection::kReceived, "b"),
    };
    records[0].mSequence = 0;
    records[1].mSequence = 2;
    const tools::RunAudit audit { tools::auditRun(1, records) };
    EXPECT_EQ(audit.mSequenceGaps, 1U);
    EXPECT_FALSE(audit.isClean());
}

// WireAudit_BoldTextIsCounted checks styled text is tallied by attribute.
TEST(WireAudit, BoldTextIsCounted)
{
    const std::vector<persistence::WireRecord> records {
        makeRecord(persistence::WireDirection::kReceived, "\x1b[1mbold\x1b[0m plain\r\n"),
    };
    const tools::RunAudit audit { tools::auditRun(1, records) };
    EXPECT_EQ(audit.mBoldTexts, 1U);
    EXPECT_EQ(audit.mReverseTexts, 0U);
}

// WireAudit_ACloseWithNoFrameIsAFinding checks an unbalanced close makes the run need a look.
TEST(WireAudit, ACloseWithNoFrameIsAFinding)
{
    persistence::WireRecord record { makeRecord(persistence::WireDirection::kReceived, "text") };
    record.mData.push_back(std::byte { wire::kTelnetIac });
    record.mData.push_back(std::byte { wire::kTelnetIac });
    const tools::RunAudit audit { tools::auditRun(1, std::vector { record }) };
    EXPECT_EQ(audit.mUnmatchedCloses, 1U);
    EXPECT_FALSE(audit.isClean());
}

// WireAudit_AnUnlistedHeaderIsAFinding checks a header outside the code table is named in the report.
TEST(WireAudit, AnUnlistedHeaderIsAFinding)
{
    persistence::WireRecord record { makeRecord(persistence::WireDirection::kReceived, "") };
    appendHeader(record, kUnlistedNumber);
    const tools::RunAudit audit { tools::auditRun(1, std::vector { record }) };
    EXPECT_EQ(audit.mUnlistedHeaders.count("77"), 1U);
    EXPECT_FALSE(audit.isClean());
    const std::vector<std::string> lines { tools::formatAudit(audit) };
    EXPECT_NE(lines.front().find("NEEDS A LOOK"), std::string::npos);
}

// WireAudit_ARunSplitMidSequenceDecodesTheSameEitherWay checks per-record and per-byte feeding agree on a split CSI.
TEST(WireAudit, ARunSplitMidSequenceDecodesTheSameEitherWay)
{
    const std::vector<persistence::WireRecord> records {
        makeRecord(persistence::WireDirection::kReceived, "\x1b[3"),
        makeRecord(persistence::WireDirection::kReceived, "1mred\r\n"),
    };
    const tools::RunAudit audit { tools::auditRun(1, records) };
    EXPECT_FALSE(audit.mChunkMismatch.has_value());
}

// WireAudit_CaptureExportLeavesOutSentRecordsByDefault checks credentials-bearing tx records are opt-in.
TEST(WireAudit, CaptureExportLeavesOutSentRecordsByDefault)
{
    persistence::WireRecord received { makeRecord(persistence::WireDirection::kReceived, "a\r\n") };
    received.mTimestampMs = 5;
    const std::vector<persistence::WireRecord> records {
        received,
        makeRecord(persistence::WireDirection::kSent, "secret\r\n"),
        makeRecord(persistence::WireDirection::kAnnotation, "note"),
    };
    const mud::golden::CaptureRecords withoutSent { tools::toCaptureRecords(records, false) };
    ASSERT_EQ(withoutSent.size(), 2U);
    EXPECT_EQ(withoutSent[0].mDirection, mud::golden::Direction::kRx);
    EXPECT_EQ(withoutSent[0].mTimestampMs, 5U);
    EXPECT_EQ(withoutSent[1].mDirection, mud::golden::Direction::kAn);
    EXPECT_EQ(tools::toCaptureRecords(records, true).size(), 3U);
}

// WireAudit_ExportedCapturesParseBackIdentically checks the written capture round-trips through the reader.
TEST(WireAudit, ExportedCapturesParseBackIdentically)
{
    persistence::WireRecord record { makeRecord(persistence::WireDirection::kReceived, "a\\b\r\n\0c") };
    record.mData.push_back(std::byte { 0x9B });
    record.mTimestampMs = 1234567890123;
    const mud::golden::CaptureRecords    capture { tools::toCaptureRecords(std::vector { record }, false) };
    const types::Bytes                   bytes { mud::golden::serializeCapture(capture) };
    const mud::golden::OptCaptureRecords parsed { mud::golden::parseCapture(bytes) };
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->size(), 1U);
    EXPECT_EQ((*parsed)[0].mPayload, record.mData);
    EXPECT_EQ((*parsed)[0].mTimestampMs, 1234567890123U);
}

// WireCheckOptions_DefaultsToCheckingEveryRun checks no arguments means a check of all runs.
TEST(WireCheckOptions, DefaultsToCheckingEveryRun)
{
    std::string error;
    const auto  options { parse({}, error) };
    ASSERT_TRUE(options.has_value()) << error;
    EXPECT_EQ(options->mAction, tools::WireCheckAction::kCheck);
    EXPECT_FALSE(options->mRun.has_value());
    EXPECT_FALSE(options->mDatabase.has_value());
}

// WireCheckOptions_ExportNeedsARunAndAValidRange checks the export contract.
TEST(WireCheckOptions, ExportNeedsARunAndAValidRange)
{
    std::string error;
    EXPECT_FALSE(parse({ "--export", "x.c1" }, error).has_value());
    EXPECT_EQ(error, "--export needs --run");
    EXPECT_FALSE(parse({ "--export", "x.c1", "--run", "1", "--from", "9", "--to", "3" }, error).has_value());
    const auto options { parse(
        { "--export", "x.c1", "--run", "1", "--from", "3", "--to", "9", "--with-sent" }, error) };
    ASSERT_TRUE(options.has_value()) << error;
    EXPECT_EQ(options->mAction, tools::WireCheckAction::kExport);
    EXPECT_EQ(options->mFirstSeq, types::OptUint64 { 3 });
    EXPECT_EQ(options->mLastSeq, types::OptUint64 { 9 });
    EXPECT_TRUE(options->mIsIncludingSent);
}

// WireCheckOptions_BadValuesAreRejected checks unknown flags, missing values and non-numbers fail with a message.
TEST(WireCheckOptions, BadValuesAreRejected)
{
    std::string error;
    EXPECT_FALSE(parse({ "--bogus" }, error).has_value());
    EXPECT_NE(error.find("--bogus"), std::string::npos);
    EXPECT_FALSE(parse({ "--run" }, error).has_value());
    EXPECT_FALSE(parse({ "--run", "x" }, error).has_value());
    EXPECT_FALSE(parse({ "--run", "-1" }, error).has_value());
    EXPECT_FALSE(parse({ "--run", "3x" }, error).has_value());
    EXPECT_FALSE(parse({ "--db" }, error).has_value());
}
