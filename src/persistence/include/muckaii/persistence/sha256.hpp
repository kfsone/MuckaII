#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace muckaii::persistence
{
constexpr std::size_t kSha256Size { 32 };

using Sha256Digest = std::array<std::uint8_t, kSha256Size>;

// SHA-256 (FIPS 180-4) of `data`: the content address of a wire payload (docs/spec/persistence.md
// "Retention and deduplication").
[[nodiscard]] Sha256Digest sha256(std::span<const std::byte> data) noexcept;
} // namespace muckaii::persistence
