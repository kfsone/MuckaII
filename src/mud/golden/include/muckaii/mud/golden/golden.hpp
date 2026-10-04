#pragma once

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
enum class Direction : std::uint8_t
{
    kRx,
    kTx,
    kAn,
};

struct CaptureRecord
{
    std::uint64_t          mTimestampMs {};
    Direction              mDirection { Direction::kRx };
    std::vector<std::byte> mPayload;
};

// Parses a whole capture's bytes under the README grammar. nullopt on any grammar violation.
[[nodiscard]] std::optional<std::vector<CaptureRecord>> parseCapture(std::span<const std::byte> data);

// Reads a capture in binary mode and parses it.
[[nodiscard]] std::optional<std::vector<CaptureRecord>> readCapture(const std::filesystem::path& path);

// The 18 priming bytes events.md feeds before a capture that begins mid-session.
constexpr std::array<std::uint8_t, 18> kPrimingBytes {
    0x9C, 0x9D, 0xFF, 0xFF, 0xFF, 0xFF, 0x9C, 0xFF, 0xFF,
    0x9C, 0x9D, 0xFF, 0xFF, 0x2A, 0xFF, 0xFF, 0xFF, 0xFF,
};

struct ReplayOptions
{
    bool mIsStartInGame { true };
    bool mWillEmitWireTier {};
    // Feed one byte per feed() call instead of one call per record. endOfRead() still comes only
    // at record boundaries.
    bool mIsByteAtATime {};
};

// An event and the zero-based ordinal of the rx record whose bytes completed it.
struct RxEvent
{
    int   mRx {};
    Event mEvent;
};

// Feeds every rx record as one read, as events.md "Driving the decoder" requires.
[[nodiscard]] std::vector<RxEvent> replay(std::span<const CaptureRecord> records,
                                          const ReplayOptions&           options);

// One event as one events.md JSON object. Wire-tier events are written too.
[[nodiscard]] std::string toJson(const Payload& payload, int rx);

// A golden stream: the `stream` record, then every non-wire event with adjacent equal-style
// `text` events merged (events.md "run": runs are maximal).
[[nodiscard]] std::vector<std::string>
toGoldenLines(std::string_view source, bool isStartInGame, std::span<const RxEvent> events);
} // namespace muckaii::mud::golden
