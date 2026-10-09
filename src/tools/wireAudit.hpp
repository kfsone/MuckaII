#pragma once

#include "muckaii/base/types.hpp"
#include "muckaii/mud/golden/golden.hpp"
#include "muckaii/persistence/wireLog.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Manual checks of a recorded wire log against the decoder (docs/build.md "Targets":
// muckaiiWireCheck). The checks hold for any real run, so a failure is a lead for a new capture.
namespace muckaii::tools
{
// ChunkMismatch locates where record-sized and byte-sized feeding of one run first differ.
struct ChunkMismatch
{
    std::size_t mEventIndex {};
    std::size_t mRecordEvents {};
    std::size_t mByteEvents {};
};

// OptChunkMismatch is the first disagreement between the two feeding modes, or nothing.
using OptChunkMismatch = std::optional<ChunkMismatch>;

// HeaderCounts counts C1 headers by their printed numbers, such as "03 02".
using HeaderCounts = std::map<std::string, std::size_t>;

// RunAudit is what decoding one recorded run found.
struct RunAudit
{
    std::int64_t     mRun {};
    std::size_t      mReceivedRecords {};
    std::size_t      mSentRecords {};
    std::size_t      mAnnotations {};
    std::size_t      mReceivedBytes {};
    std::size_t      mEvents {};
    std::size_t      mTextEvents {};
    std::size_t      mLineEnds {};
    std::size_t      mTelnetCommands {};
    std::size_t      mCodes {};
    std::size_t      mUnmatchedCloses {};
    std::size_t      mSequenceGaps {}; // places where stored sequence numbers skip
    std::size_t      mBoldTexts {};
    std::size_t      mReverseTexts {};
    std::size_t      mBlinkTexts {};
    HeaderCounts     mUnlistedHeaders; // headers that matched no row of the code table
    OptChunkMismatch mChunkMismatch;

    // isClean reports whether the run raised nothing that needs a human look.
    [[nodiscard]] bool isClean() const noexcept;
};

// auditRun decodes the received records of `records` from the start of a connection, feeds them
// twice (one call per record, one byte per call), and reports what it found.
[[nodiscard]] RunAudit auditRun(std::int64_t run, std::span<const persistence::WireRecord> records);

// formatAudit renders `audit` as report lines.
[[nodiscard]] std::vector<std::string> formatAudit(const RunAudit& audit);

// toCaptureRecords converts stored records to capture records; sent records are left out unless
// `isIncludingSent`, because they may carry credentials.
[[nodiscard]] mud::golden::CaptureRecords toCaptureRecords(std::span<const persistence::WireRecord> records,
                                                           bool isIncludingSent);
} // namespace muckaii::tools
