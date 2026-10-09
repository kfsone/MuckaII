#include "muckaii/persistence/sha256.hpp"

namespace muckaii::persistence
{
namespace
{
// kBlockSize is the SHA-256 compression block length in bytes.
constexpr std::size_t kBlockSize { 64 };
// kLengthOffset marks the final block bytes reserved for the message bit length.
constexpr std::size_t kLengthOffset { 56 };
// kRounds is the number of compression rounds per message block.
constexpr std::size_t kRounds { 64 };
// kScheduleWords is the number of words encoded directly in each block.
constexpr std::size_t kScheduleWords { 16 };
// kByteBits is the number of bits in a byte.
constexpr unsigned kByteBits { 8 };
// kPadByte begins the SHA-256 message padding.
constexpr std::uint8_t kPadByte { 0x80 };
static_assert(kScheduleWords * 4 == kBlockSize);

// kRoundConstants are the fixed SHA-256 round constants from FIPS 180-4.
constexpr std::array<std::uint32_t, kRounds> kRoundConstants {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

// kInitialState is the standard SHA-256 initial chaining value.
constexpr std::array<std::uint32_t, 8> kInitialState {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

// rotateRight performs the 32-bit rotation used by SHA-256.
constexpr std::uint32_t rotateRight(std::uint32_t value, unsigned bits) noexcept
{
    return (value >> bits) | (value << (32U - bits));
}

// compress folds one padded message block into the SHA-256 chaining state.
void compress(std::array<std::uint32_t, 8>& state, const std::array<std::uint8_t, kBlockSize>& block) noexcept
{
    std::array<std::uint32_t, kRounds> schedule {};
    for (std::size_t i { 0 }; i < kScheduleWords; ++i)
    {
        schedule[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24U)
                      | (static_cast<std::uint32_t>(block[(i * 4) + 1]) << 16U)
                      | (static_cast<std::uint32_t>(block[(i * 4) + 2]) << 8U)
                      | static_cast<std::uint32_t>(block[(i * 4) + 3]);
    }
    for (std::size_t i { kScheduleWords }; i < kRounds; ++i)
    {
        const std::uint32_t low { schedule[i - 15] };
        const std::uint32_t high { schedule[i - 2] };
        const std::uint32_t sigma0 { rotateRight(low, 7) ^ rotateRight(low, 18) ^ (low >> 3U) };
        const std::uint32_t sigma1 { rotateRight(high, 17) ^ rotateRight(high, 19) ^ (high >> 10U) };
        schedule[i] = schedule[i - 16] + sigma0 + schedule[i - 7] + sigma1;
    }

    std::array<std::uint32_t, 8> work { state };
    for (std::size_t i { 0 }; i < kRounds; ++i)
    {
        const std::uint32_t e { work[4] };
        const std::uint32_t a { work[0] };
        const std::uint32_t sum1 { rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25) };
        const std::uint32_t choose { (e & work[5]) ^ (~e & work[6]) };
        const std::uint32_t temp1 { work[7] + sum1 + choose + kRoundConstants[i] + schedule[i] };
        const std::uint32_t sum0 { rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22) };
        const std::uint32_t majority { (a & work[1]) ^ (a & work[2]) ^ (work[1] & work[2]) };
        const std::uint32_t temp2 { sum0 + majority };
        work[7] = work[6];
        work[6] = work[5];
        work[5] = work[4];
        work[4] = work[3] + temp1;
        work[3] = work[2];
        work[2] = work[1];
        work[1] = work[0];
        work[0] = temp1 + temp2;
    }
    for (std::size_t i { 0 }; i < state.size(); ++i)
        state[i] += work[i];
}
} // namespace

// sha256 pads data, compresses each block, and returns the resulting digest.
Sha256Digest sha256(std::span<const std::byte> data) noexcept
{
    std::array<std::uint32_t, 8>         state { kInitialState };
    std::array<std::uint8_t, kBlockSize> block {};
    std::size_t                          used { 0 };
    for (const std::byte byte : data)
    {
        // used < kBlockSize here: it is reset to 0 whenever it reaches kBlockSize.
        block[used++] = std::to_integer<std::uint8_t>(byte);
        if (used == kBlockSize)
        {
            compress(state, block);
            used = 0;
        }
    }

    block[used++] = kPadByte;
    if (used > kLengthOffset)
    {
        while (used < kBlockSize)
            block[used++] = 0;
        compress(state, block);
        used = 0;
    }
    while (used < kLengthOffset)
        block[used++] = 0;
    const std::uint64_t bits { static_cast<std::uint64_t>(data.size()) * kByteBits };
    static_assert(kLengthOffset + kByteBits == kBlockSize);
    for (std::size_t i { 0 }; i < kByteBits; ++i)
        block[kLengthOffset + i] = static_cast<std::uint8_t>(bits >> ((7U - i) * kByteBits));
    compress(state, block);

    Sha256Digest digest {};
    static_assert(std::tuple_size_v<Sha256Digest> == 4 * std::tuple_size_v<decltype(state)>);
    for (std::size_t i { 0 }; i < state.size(); ++i)
    {
        digest[i * 4]       = static_cast<std::uint8_t>(state[i] >> 24U);
        digest[(i * 4) + 1] = static_cast<std::uint8_t>(state[i] >> 16U);
        digest[(i * 4) + 2] = static_cast<std::uint8_t>(state[i] >> 8U);
        digest[(i * 4) + 3] = static_cast<std::uint8_t>(state[i]);
    }
    return digest;
}
} // namespace muckaii::persistence
