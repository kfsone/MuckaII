#pragma once

#include "muckaii/base/types.hpp"
#include "muckaii/mud/events.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Test support for the mud layer (docs/build.md "Targets": muckaiiMudGolden): the `.c1` capture
// reader (docs/evidence/captures/README.md), the capture replay of docs/spec/events.md "Driving
// the decoder", and the golden serialiser that writes events as events.md JSON Lines.
namespace muckaii::mud::golden
{
// Direction identifies whether a capture record is received, transmitted, or annotated.
enum class Direction : std::uint8_t
{
    kRx,
    kTx,
    kAn,
};

// CaptureRecord stores the timestamp, direction, and raw bytes of one capture record.
struct CaptureRecord
{
    std::uint64_t mTimestampMs {};
    Direction     mDirection { Direction::kRx };
    types::Bytes  mPayload;
};

// CaptureRecords are the records of one capture file, in file order.
using CaptureRecords = std::vector<CaptureRecord>;

// OptCaptureRecords is CaptureRecords, or nothing when the capture is malformed.
using OptCaptureRecords = std::optional<CaptureRecords>;

// Parses a whole capture's bytes under the README grammar. nullopt on any grammar violation.
// parseCapture validates and parses a complete capture byte sequence.
[[nodiscard]] OptCaptureRecords parseCapture(std::span<const std::byte> data);

// Reads a capture in binary mode and parses it.
// readCapture loads a capture file and parses its contents.
[[nodiscard]] OptCaptureRecords readCapture(const std::filesystem::path& path);

// serializeCapture writes records in the capture grammar, the inverse of parseCapture.
[[nodiscard]] types::Bytes serializeCapture(std::span<const CaptureRecord> records);

// The 18 priming bytes events.md feeds before a capture that begins mid-session.
constexpr std::array<std::uint8_t, 18> kPrimingBytes {
    0x9C, 0x9D, 0xFF, 0xFF, 0xFF, 0xFF, 0x9C, 0xFF, 0xFF,
    0x9C, 0x9D, 0xFF, 0xFF, 0x2A, 0xFF, 0xFF, 0xFF, 0xFF,
};

// ReplayOptions controls decoder setup and record chunking during replay.
struct ReplayOptions
{
    bool mIsStartInGame { true };
    bool mWillEmitWireTier {};
    // Feed one byte per feed() call instead of one call per record. endOfRead() still comes only
    // at record boundaries.
    bool mIsByteAtATime {};
};

// An event and the zero-based ordinal of the rx record whose bytes completed it.
// RxEvent pairs a decoded event with the received-record ordinal that completed it.
struct RxEvent
{
    int   mRx {};
    Event mEvent;
};

// CaptureView exposes records to replay without copying.
using CaptureView = std::span<const CaptureRecord>;

// replay feeds received capture records through a decoder and returns their events.
[[nodiscard]] std::vector<RxEvent> replay(CaptureView records, const ReplayOptions& options);

// One event as one events.md JSON object. Wire-tier events are written too.
// toJson serializes one event payload as an events.md JSON object.
[[nodiscard]] std::string toJson(const Payload& payload, int rx);

// toGoldenLines serializes the stream header and merged non-wire events as JSON lines.
[[nodiscard]] std::vector<std::string>
toGoldenLines(std::string_view source, bool isStartInGame, std::span<const RxEvent> events);
} // namespace muckaii::mud::golden
