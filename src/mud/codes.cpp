#include "muckaii/mud/codes.hpp"

#include <algorithm>

namespace muckaii::mud
{
namespace
{
// findExact finds a code row whose complete number sequence matches.
const CodeInfo* findExact(std::span<const std::uint8_t> numbers) noexcept
{
    if (numbers.empty() || numbers.size() > kMaxIdNumbers)
        return nullptr;

    for (const CodeInfo& code : getCodeTable())
    {
        if (code.mCount == numbers.size()
            && std::ranges::equal(numbers, std::span { code.mNumbers }.first(code.mCount)))
            return &code;
    }
    return nullptr;
}
} // namespace

const CodeInfo* findCode(const Header& header) noexcept
{
    if (header.mCount == 0)
        return nullptr;

    const std::span<const std::uint8_t> numbers { std::span { header.mNumbers }.first(header.mCount) };

    // Step 1: the numbers equal an id exactly.
    if (const CodeInfo* const exact = findExact(numbers); exact != nullptr)
        return exact;

    // Step 2: the first number is that of a parametric entry.
    for (const CodeInfo& code : getCodeTable())
    {
        if (code.mParamCount > 0 && code.mCount == 1 && code.mNumbers[0] == numbers[0])
            return &code;
    }

    // Step 3: trailing zeros removed one at a time.
    std::size_t count { numbers.size() };
    while (count > 1 && numbers[count - 1] == 0)
    {
        --count;
        if (const CodeInfo* const trimmed = findExact(numbers.first(count)); trimmed != nullptr)
            return trimmed;
    }

    // Step 4: zeros appended, up to three numbers.
    if (numbers.size() >= kMaxIdNumbers)
        return nullptr;

    std::array<std::uint8_t, kMaxIdNumbers> padded {};
    std::ranges::copy(numbers, padded.begin());
    for (std::size_t length { numbers.size() + 1U }; length <= kMaxIdNumbers; ++length)
    {
        if (const CodeInfo* const extended = findExact(std::span { padded }.first(length));
            extended != nullptr)
            return extended;
    }
    return nullptr;
}
} // namespace muckaii::mud
