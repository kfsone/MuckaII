#include "muckaii/mud/golden/golden.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Golden replay (docs/spec/events.md, docs/evidence/golden/README.md): each capture is fed rx record
// by rx record and the serialised events are diffed against its golden. The only differences
// allowed are the README's "Known differences", each listed here exactly.

namespace
{
namespace golden = muckaii::mud::golden;

const std::filesystem::path kRepoRoot { MUCKAII_REPO_ROOT };

const std::vector<std::string> kCaptures {
    "blind-vs-water-snake",      "dark-two-rats",       "invisible-man-preemptive", "invisible-player-joins",
    "phone-login-at-20-columns", "wyvern-poison-death",
};

// A record MuckaII writes that the golden lacks: inserted before the golden's 0-based line
// `mBeforeLine`, which must be the `line_end` of the same line.
// KnownDifference records one known extra event inserted into a golden stream.
struct KnownDifference
{
    std::string mCapture;
    std::size_t mBeforeLine {};
    int         mRx {};
    std::string mText;
};

// events.md DECISION (room_long): MuckaII reports the last row of a long description, whose 02 02
// frame the server closes before its line end. The reference, and so the golden, does not.
const std::vector<KnownDifference> kKnownDifferences {
    { .mCapture    = "dark-two-rats",
      .mBeforeLine = 9,
      .mRx         = 0,
      .mText       = "northeast, and below is a dank cellar. " },
    { .mCapture    = "dark-two-rats",
      .mBeforeLine = 27,
      .mRx         = 0,
      .mText       = "turn round and come back were you to enter it. To the west is the doorway to the coal "
                     "bunker. An" },
    { .mCapture    = "invisible-man-preemptive",
      .mBeforeLine = 8,
      .mRx         = 1,
      .mText =
          "You are in a small, claustrophobic, earthen tunnel, curving from the west to the southeast. " },
    { .mCapture    = "invisible-man-preemptive",
      .mBeforeLine = 23,
      .mRx         = 5,
      .mText = "You are in a small, claustrophobic, earthen tunnel, curving from the north to the east. " },
    { .mCapture    = "invisible-man-preemptive",
      .mBeforeLine = 79,
      .mRx         = 15,
      .mText =
          "You are in a small, claustrophobic, earthen tunnel, curving from the northwest to the south. " },
    { .mCapture    = "invisible-man-preemptive",
      .mBeforeLine = 127,
      .mRx         = 23,
      .mText =
          "You are in a small, claustrophobic, earthen tunnel, curving from the north to the southeast. " },
};

// readLines loads a text file as separate lines.
std::vector<std::string> readLines(const std::filesystem::path& path)
{
    std::ifstream            file { path, std::ios::binary };
    std::vector<std::string> lines;
    std::string              line;
    while (std::getline(file, line))
        lines.push_back(line);
    return lines;
}

// loadCapture reads one named capture for replay tests.
std::vector<golden::CaptureRecord> loadCapture(const std::string& name)
{
    const auto records { golden::readCapture(kRepoRoot / "docs" / "evidence" / "captures" / (name + ".c1")) };
    EXPECT_TRUE(records.has_value()) << name;
    return records.value_or(std::vector<golden::CaptureRecord> {});
}

// describeDiff formats the first differing lines of two golden streams.
std::string describeDiff(const std::vector<std::string>& expected, const std::vector<std::string>& actual)
{
    const std::size_t common { std::min(expected.size(), actual.size()) };
    std::size_t       first { 0 };
    while (first < common && expected[first] == actual[first])
        ++first;
    std::ostringstream text;
    text << "first difference at line " << first << " (expected " << expected.size() << " lines, got "
         << actual.size() << ")\n";
    const std::size_t last { std::min(first + 3, std::max(expected.size(), actual.size())) };
    for (std::size_t i { first > 2 ? first - 2 : 0 }; i < last; ++i)
    {
        text << "  expected[" << i << "]: " << (i < expected.size() ? expected[i] : "<none>") << "\n";
        text << "  actual  [" << i << "]: " << (i < actual.size() ? actual[i] : "<none>") << "\n";
    }
    return text.str();
}

// getCaptureTestName converts a capture filename into a valid parameter-test suffix.
std::string getCaptureTestName(std::string_view capture)
{
    std::string testName;
    for (const char c : capture)
        testName.push_back(c == '-' ? '_' : c);
    return testName;
}

// parseCaptureText parses a test string using the capture grammar's byte representation.
golden::OptCaptureRecords parseCaptureText(std::string_view text)
{
    return golden::parseCapture(std::as_bytes(std::span { text.data(), text.size() }));
}

// GoldenReplay runs capture-based checks for each configured capture.
class GoldenReplay : public testing::TestWithParam<std::string>
{
};

// GoldenReplay checks replayed events match the checked-in golden after its documented differences.
TEST_P(GoldenReplay, MatchesGoldenExceptKnownDifferences)
{
    const std::string           name { GetParam() };
    const std::filesystem::path goldenDirectory { kRepoRoot / "docs" / "evidence" / "golden" };
    const std::filesystem::path goldenPath { goldenDirectory / (name + ".jsonl") };
    std::vector<std::string>    expected { readLines(goldenPath) };
    ASSERT_FALSE(expected.empty()) << name;
    const bool isStartInGame { expected.front().find(R"("start_in_game":true)") != std::string::npos };

    // Apply the known differences, last first so earlier line numbers stay valid.
    std::vector<KnownDifference> differences;
    std::ranges::copy_if(kKnownDifferences,
                         std::back_inserter(differences),
                         [&name](const KnownDifference& difference)
                         {
                             return difference.mCapture == name;
                         });
    std::ranges::sort(differences,
                      [](const KnownDifference& a, const KnownDifference& b)
                      {
                          return a.mBeforeLine > b.mBeforeLine;
                      });
    for (const KnownDifference& difference : differences)
    {
        ASSERT_LT(difference.mBeforeLine, expected.size());
        const std::string lineEnd { R"({"ev":"line_end","rx":)" + std::to_string(difference.mRx) + "," };
        ASSERT_TRUE(expected[difference.mBeforeLine].starts_with(lineEnd))
            << name << " line " << difference.mBeforeLine << " is not the line_end the difference names";
        const muckaii::mud::RoomLongEvent event { .mText = difference.mText };
        const std::string                 inserted { golden::toJson(event, difference.mRx) };
        expected.insert(expected.begin() + static_cast<std::ptrdiff_t>(difference.mBeforeLine), inserted);
    }

    const std::vector<golden::CaptureRecord> records { loadCapture(name) };
    const std::vector<golden::RxEvent>       events { golden::replay(
        records, golden::ReplayOptions { .mIsStartInGame = isStartInGame }) };
    const std::vector<std::string>           actual { golden::toGoldenLines(
        "capture:" + name + ".c1", isStartInGame, events) };

    EXPECT_TRUE(actual == expected) << name << ": " << describeDiff(expected, actual);
}

// Chunking invariance: one byte per feed() and one record per feed() give the same events, wire
// tier included, compared before any run merging.
// GoldenReplay checks feeding a capture one byte at a time produces the same events as whole records.
TEST_P(GoldenReplay, ByteAtATimeEqualsWholeRecord)
{
    const std::string                        name { GetParam() };
    const std::vector<golden::CaptureRecord> records { loadCapture(name) };

    golden::ReplayOptions wholeOptions;
    wholeOptions.mWillEmitWireTier = true;
    const auto whole { golden::replay(records, wholeOptions) };

    golden::ReplayOptions byteOptions;
    byteOptions.mWillEmitWireTier = true;
    byteOptions.mIsByteAtATime    = true;
    const auto bytes { golden::replay(records, byteOptions) };
    ASSERT_EQ(whole.size(), bytes.size()) << name;
    for (std::size_t i { 0 }; i < whole.size(); ++i)
    {
        EXPECT_EQ(whole[i].mRx, bytes[i].mRx) << name << " event " << i;
        EXPECT_TRUE(whole[i].mEvent == bytes[i].mEvent)
            << name << " event " << i << ": " << golden::toJson(whole[i].mEvent.mPayload, whole[i].mRx)
            << " vs " << golden::toJson(bytes[i].mEvent.mPayload, bytes[i].mRx);
    }
}

INSTANTIATE_TEST_SUITE_P(Captures,
                         GoldenReplay,
                         testing::ValuesIn(kCaptures),
                         [](const testing::TestParamInfo<std::string>& info)
                         {
                             return getCaptureTestName(info.param);
                         });

// GoldenCaptures checks the configured capture list includes every capture file on disk.
TEST(GoldenCaptures, EveryCaptureIsReplayed)
{
    std::vector<std::string> onDisk;
    for (const auto& entry :
         std::filesystem::directory_iterator { kRepoRoot / "docs" / "evidence" / "captures" })
    {
        if (entry.path().extension() == ".c1")
            onDisk.push_back(entry.path().stem().string());
    }
    std::ranges::sort(onDisk);
    std::vector<std::string> listed { kCaptures };
    std::ranges::sort(listed);
    EXPECT_EQ(onDisk, listed);
}

// GoldenCaptures checks the capture parser rejects malformed records and preserves valid escaped bytes.
TEST(GoldenCaptures, CaptureGrammarViolationsAreRefused)
{
    EXPECT_TRUE(parseCaptureText("1 rx a\\r\\n\n").has_value());
    EXPECT_FALSE(parseCaptureText("1 rx a").has_value());
    EXPECT_FALSE(parseCaptureText("1 xx a\n").has_value());
    EXPECT_FALSE(parseCaptureText("1 rx a\\q\n").has_value());
    EXPECT_FALSE(parseCaptureText("x rx a\n").has_value());
    EXPECT_FALSE(parseCaptureText("1 rx a\n\n").has_value());
    const auto records { parseCaptureText("12 rx a\\\\b\\r\\n\n13 tx c\n") };
    ASSERT_TRUE(records.has_value());
    ASSERT_EQ(records->size(), 2U);
    EXPECT_EQ((*records)[0].mTimestampMs, 12U);
    EXPECT_EQ((*records)[0].mPayload.size(), 5U);
    EXPECT_EQ((*records)[1].mDirection, golden::Direction::kTx);
}
} // namespace
