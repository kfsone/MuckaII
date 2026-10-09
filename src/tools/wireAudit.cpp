#include "wireAudit.hpp"

#include "muckaii/mud/events.hpp"

#include <algorithm>
#include <cstdio>
#include <variant>

namespace muckaii::tools
{
namespace
{
namespace golden = mud::golden;

// kHeaderNumberDigits is the printed width of one C1 header number.
constexpr int kHeaderNumberDigits { 2 };

// kMaxPrintedHeaders limits how many distinct unlisted headers a report lists.
constexpr std::size_t kMaxPrintedHeaders { 20 };

// ReplayMode names the two ways a run is fed to the decoder.
enum class ReplayMode : std::uint8_t
{
    kPerRecord,
    kPerByte,
};

// replayRun decodes the received records of `records` in the requested mode.
std::vector<mud::Event> replayRun(const golden::CaptureRecords& records, ReplayMode mode)
{
    const golden::ReplayOptions options { .mIsStartInGame    = false,
                                          .mWillEmitWireTier = true,
                                          .mIsByteAtATime    = mode == ReplayMode::kPerByte };
    std::vector<mud::Event>     events;
    for (golden::RxEvent& rx : golden::replay(records, options))
        events.push_back(std::move(rx.mEvent));
    return events;
}

// formatHeader prints a header's numbers as two-digit groups separated by spaces.
std::string formatHeader(const mud::Header& header)
{
    std::string text;
    for (std::size_t i {}; i < header.mCount; ++i)
    {
        char digits[kHeaderNumberDigits + 1] {};
        std::snprintf(digits, sizeof digits, "%02u", static_cast<unsigned>(header.mNumbers[i]));
        if (i > 0)
            text += ' ';
        text += digits;
    }
    return text;
}

// findMismatch returns where `recordEvents` and `byteEvents` first disagree, or nothing.
OptChunkMismatch findMismatch(const std::vector<mud::Event>& recordEvents,
                              const std::vector<mud::Event>& byteEvents)
{
    const std::size_t shared { std::min(recordEvents.size(), byteEvents.size()) };
    for (std::size_t i {}; i < shared; ++i)
    {
        if (recordEvents[i].mPayload != byteEvents[i].mPayload)
            return ChunkMismatch { .mEventIndex   = i,
                                   .mRecordEvents = recordEvents.size(),
                                   .mByteEvents   = byteEvents.size() };
    }
    if (recordEvents.size() != byteEvents.size())
        return ChunkMismatch { .mEventIndex   = shared,
                               .mRecordEvents = recordEvents.size(),
                               .mByteEvents   = byteEvents.size() };
    return std::nullopt;
}

// tally folds one event into the counts of `audit`.
void tally(RunAudit& audit, const mud::Payload& payload)
{
    ++audit.mEvents;
    if (const auto* text { std::get_if<mud::TextEvent>(&payload) })
    {
        ++audit.mTextEvents;
        audit.mBoldTexts    += text->mStyle.hasAttr(mud::Attr::kBold) ? 1U : 0U;
        audit.mReverseTexts += text->mStyle.hasAttr(mud::Attr::kReverse) ? 1U : 0U;
        audit.mBlinkTexts   += text->mStyle.hasAttr(mud::Attr::kBlink) ? 1U : 0U;
    }
    else if (std::holds_alternative<mud::LineEndEvent>(payload))
        ++audit.mLineEnds;
    else if (std::holds_alternative<mud::TelnetEvent>(payload))
        ++audit.mTelnetCommands;
    else if (const auto* close { std::get_if<mud::CloseEvent>(&payload) })
        audit.mUnmatchedCloses += close->mIsPopped ? 0U : 1U;
    else if (const auto* code { std::get_if<mud::CodeEvent>(&payload) })
    {
        ++audit.mCodes;
        if (code->mId.empty())
            ++audit.mUnlistedHeaders[formatHeader(code->mHeader)];
    }
}

// toCaptureDirection maps a stored direction to the capture grammar's.
golden::Direction toCaptureDirection(persistence::WireDirection direction) noexcept
{
    if (direction == persistence::WireDirection::kSent)
        return golden::Direction::kTx;
    if (direction == persistence::WireDirection::kAnnotation)
        return golden::Direction::kAn;
    return golden::Direction::kRx;
}
} // namespace

bool RunAudit::isClean() const noexcept
{
    return mUnlistedHeaders.empty() && mUnmatchedCloses == 0 && mSequenceGaps == 0
           && ! mChunkMismatch.has_value();
}

golden::CaptureRecords toCaptureRecords(std::span<const persistence::WireRecord> records,
                                        bool                                     isIncludingSent)
{
    golden::CaptureRecords out;
    for (const persistence::WireRecord& record : records)
    {
        if (record.mDirection == persistence::WireDirection::kSent && ! isIncludingSent)
            continue;
        out.push_back(golden::CaptureRecord { .mTimestampMs = static_cast<std::uint64_t>(record.mTimestampMs),
                                              .mDirection   = toCaptureDirection(record.mDirection),
                                              .mPayload     = record.mData });
    }
    return out;
}

RunAudit auditRun(std::int64_t run, std::span<const persistence::WireRecord> records)
{
    RunAudit audit { .mRun = run };
    for (std::size_t i {}; i < records.size(); ++i)
    {
        const persistence::WireRecord& record { records[i] };
        if (i > 0 && record.mSequence > records[i - 1].mSequence + 1)
            ++audit.mSequenceGaps;
        if (record.mDirection == persistence::WireDirection::kReceived)
        {
            ++audit.mReceivedRecords;
            audit.mReceivedBytes += record.mData.size();
        }
        else if (record.mDirection == persistence::WireDirection::kSent)
            ++audit.mSentRecords;
        else
            ++audit.mAnnotations;
    }

    const golden::CaptureRecords  capture { toCaptureRecords(records, false) };
    const std::vector<mud::Event> recordEvents { replayRun(capture, ReplayMode::kPerRecord) };
    const std::vector<mud::Event> byteEvents { replayRun(capture, ReplayMode::kPerByte) };
    for (const mud::Event& event : recordEvents)
        tally(audit, event.mPayload);
    audit.mChunkMismatch = findMismatch(recordEvents, byteEvents);
    return audit;
}

std::vector<std::string> formatAudit(const RunAudit& audit)
{
    std::vector<std::string> lines;
    lines.push_back("run " + std::to_string(audit.mRun) + ": "
                    + (audit.isClean() ? "clean" : "NEEDS A LOOK"));
    lines.push_back("  records: " + std::to_string(audit.mReceivedRecords) + " rx ("
                    + std::to_string(audit.mReceivedBytes) + " bytes), " + std::to_string(audit.mSentRecords)
                    + " tx, " + std::to_string(audit.mAnnotations) + " notes");
    lines.push_back("  events: " + std::to_string(audit.mEvents) + " (" + std::to_string(audit.mTextEvents)
                    + " text, " + std::to_string(audit.mLineEnds) + " line ends, "
                    + std::to_string(audit.mTelnetCommands) + " telnet, " + std::to_string(audit.mCodes)
                    + " codes)");
    lines.push_back("  styled text: " + std::to_string(audit.mBoldTexts) + " bold, "
                    + std::to_string(audit.mReverseTexts) + " reverse, " + std::to_string(audit.mBlinkTexts)
                    + " blink");
    if (audit.mSequenceGaps > 0)
        lines.push_back("  FINDING: " + std::to_string(audit.mSequenceGaps) + " gap(s) in record sequence");
    if (audit.mUnmatchedCloses > 0)
        lines.push_back("  FINDING: " + std::to_string(audit.mUnmatchedCloses)
                        + " close(s) with no open frame");
    std::size_t printed {};
    for (const auto& [header, count] : audit.mUnlistedHeaders)
    {
        if (printed++ == kMaxPrintedHeaders)
        {
            lines.push_back("  FINDING: ... and "
                            + std::to_string(audit.mUnlistedHeaders.size() - kMaxPrintedHeaders)
                            + " more unlisted headers");
            break;
        }
        lines.push_back("  FINDING: unlisted header <" + header + "> x" + std::to_string(count));
    }
    if (audit.mChunkMismatch.has_value())
        lines.push_back("  FINDING: record-sized and byte-sized feeding differ at event "
                        + std::to_string(audit.mChunkMismatch->mEventIndex) + " ("
                        + std::to_string(audit.mChunkMismatch->mRecordEvents) + " vs "
                        + std::to_string(audit.mChunkMismatch->mByteEvents) + " events)");
    return lines;
}
} // namespace muckaii::tools
