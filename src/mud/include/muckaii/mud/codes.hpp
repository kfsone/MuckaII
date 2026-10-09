#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace muckaii::mud
{
// What a header does to the context stack (docs/spec/protocol.md "Context stack").
// StackOp describes how a header changes the context stack.
enum class StackOp : std::uint8_t
{
    kPush,
    kPushEmpty,
    kCatch,
    kThrow,
};

// The event-level role of a code (docs/spec/c1-codes.toml `role`).
// Role identifies the event-level purpose of a C1 code.
enum class Role : std::uint8_t
{
    kNone,
    kPromptContainer,
    kPromptCore,
    kPromptIndicator,
    kRoomName,
    kRoomLong,
    kCreatureSentence,
    kCreatureInvisible,
    kWhoName,
    kPresenceName,
    kFightStart,
    kFightEnd,
    kPersonaUnsaved,
    kChat,
    kFesLine,
    kFeExits,
    kFeInventory,
    kFeWho,
    kDreamword,
    kDreamwordCleared,
    kStatStamina,
    kStatMaxStamina,
    kStatScore,
    kSnoopPrefix,
    kClientMode,
    kAccountChange,
    kAccountLogout,
};

// kNoColourChange preserves the current foreground and background; kColourFromParams reads them from the header.
constexpr std::int8_t kNoColourChange { -1 };
constexpr std::int8_t kColourFromParams { -2 };

// kMaxIdNumbers is the longest id sequence in the C1 code table.
constexpr std::size_t kMaxIdNumbers { 3 };

// One row of docs/spec/c1-codes.toml. `mNumbers` holds `mCount` numbers, then zeros.
// CodeInfo describes one C1 code-table row.
struct CodeInfo
{
    std::array<std::uint8_t, kMaxIdNumbers> mNumbers;
    std::uint8_t                            mCount;
    std::string_view                        mId;
    StackOp                                 mOp;
    std::int8_t                             mFg;
    std::int8_t                             mBg;
    std::uint8_t                            mParamCount;
    Role                                    mRole;
};

// kMaxHeaderNumbers is the protocol's maximum number count in a C1 header.
constexpr std::size_t kMaxHeaderNumbers { 4 };

// A header as sent: 1 to 4 numbers, each 0-99.
// Header stores the numbers carried by one C1 header.
struct Header
{
    std::array<std::uint8_t, kMaxHeaderNumbers> mNumbers {};
    std::uint8_t                                mCount {};

    // operator== compares two C1 headers by their stored numbers and count.
    bool operator==(const Header&) const = default;
};

// The generated table (src/mud/codeTable.cpp, from docs/spec/c1-codes.toml).
// getCodeTable returns the C1 code table in source order.
[[nodiscard]] std::span<const CodeInfo> getCodeTable() noexcept;

// The table row a header dispatches on, by the four matching steps of docs/spec/protocol.md
// "Parameters". nullptr when it matches no id.
// findCode returns the code row selected by a header, or null when none matches.
[[nodiscard]] const CodeInfo* findCode(const Header& header) noexcept;
} // namespace muckaii::mud
