#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace muckaii::mud
{
// What a header does to the context stack (docs/spec/protocol.md "Context stack").
enum class StackOp : std::uint8_t
{
    kPush,
    kPushEmpty,
    kCatch,
    kThrow,
};

// The event-level role of a code (docs/spec/c1-codes.toml `role`).
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

// Colour column values in c1-codes.toml that are not colour indices.
constexpr std::int8_t kNoColourChange { -1 };
constexpr std::int8_t kColourFromParams { -2 };

// The most numbers an id has (docs/spec/c1-codes.toml).
constexpr std::size_t kMaxIdNumbers { 3 };

// One row of docs/spec/c1-codes.toml. `mNumbers` holds `mCount` numbers, then zeros.
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

// The most numbers a header may carry (docs/spec/protocol.md "C1 framing").
constexpr std::size_t kMaxHeaderNumbers { 4 };

// A header as sent: 1 to 4 numbers, each 0-99.
struct Header
{
    std::array<std::uint8_t, kMaxHeaderNumbers> mNumbers {};
    std::uint8_t                                mCount {};

    bool operator==(const Header&) const = default;
};

// The generated table (src/mud/codeTable.cpp, from docs/spec/c1-codes.toml).
[[nodiscard]] std::span<const CodeInfo> getCodeTable() noexcept;

// The table row a header dispatches on, by the four matching steps of docs/spec/protocol.md
// "Parameters". nullptr when it matches no id.
[[nodiscard]] const CodeInfo* findCode(const Header& header) noexcept;
} // namespace muckaii::mud
