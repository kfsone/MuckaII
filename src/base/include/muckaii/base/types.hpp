#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Short names for the shapes MuckaII spells most often, after the <cstdint> pattern: OptInt is
// std::optional<int>, OptUint8 is std::optional<std::uint8_t>. Use these in preference to the
// spelled-out type. A structured type that is returned, stored, or passed on gets its own name in
// the layer that owns it (docs/style.md, Types).
namespace muckaii::types
{
// An owned byte sequence: wire data, and a message built to be sent.
using Bytes = std::vector<std::byte>;

using OptBool       = std::optional<bool>;
using OptChar       = std::optional<char>;
using OptInt        = std::optional<int>;
using OptUint       = std::optional<unsigned>;
using OptInt8       = std::optional<std::int8_t>;
using OptInt16      = std::optional<std::int16_t>;
using OptInt32      = std::optional<std::int32_t>;
using OptInt64      = std::optional<std::int64_t>;
using OptUint8      = std::optional<std::uint8_t>;
using OptUint16     = std::optional<std::uint16_t>;
using OptUint32     = std::optional<std::uint32_t>;
using OptUint64     = std::optional<std::uint64_t>;
using OptSize       = std::optional<std::size_t>;
using OptString     = std::optional<std::string>;
using OptStringView = std::optional<std::string_view>;
using OptBytes      = std::optional<Bytes>;
} // namespace muckaii::types
