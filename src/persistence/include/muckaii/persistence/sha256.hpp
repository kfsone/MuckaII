#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace muckaii::persistence
{
// kSha256Size is the fixed output length of a SHA-256 digest in bytes.
constexpr std::size_t kSha256Size { 32 };

using Sha256Digest = std::array<std::uint8_t, kSha256Size>;

// sha256 returns the SHA-256 content address of data.
[[nodiscard]] Sha256Digest sha256(std::span<const std::byte> data) noexcept;
} // namespace muckaii::persistence
