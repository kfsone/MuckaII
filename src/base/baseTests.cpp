#include "muckaii/base/types.hpp"
#include "muckaii/base/wire.hpp"

#include <gtest/gtest.h>

#include <type_traits>

namespace muckaii
{
namespace
{
static_assert(std::is_same_v<types::OptInt, std::optional<int>>);
static_assert(std::is_same_v<types::OptUint8, std::optional<std::uint8_t>>);
static_assert(std::is_same_v<types::OptString, std::optional<std::string>>);
static_assert(std::is_same_v<types::OptBytes, std::optional<std::vector<std::byte>>>);

// A default-constructed alias is empty, so "nothing" is the unset state, not a zero value.
// Types_AliasesStartEmpty checks optional aliases default to the unset state.
TEST(Types, AliasesStartEmpty)
{
    EXPECT_FALSE(types::OptInt {}.has_value());
    EXPECT_FALSE(types::OptUint16 {}.has_value());
    EXPECT_FALSE(types::OptBytes {}.has_value());
}

// Zero is a value, distinct from unset.
// Types_ZeroIsNotUnset checks an engaged optional preserves zero as a value.
TEST(Types, ZeroIsNotUnset)
{
    EXPECT_TRUE(types::OptInt { 0 }.has_value());
}

// The printable range is exactly space through tilde; DEL and the controls are outside it.
// Wire_PrintableBoundaries checks printable bytes include space and tilde but exclude controls and DEL.
TEST(Wire, PrintableBoundaries)
{
    EXPECT_FALSE(wire::isPrintable(0x1F));
    EXPECT_TRUE(wire::isPrintable(0x20));
    EXPECT_TRUE(wire::isPrintable(0x7E));
    EXPECT_FALSE(wire::isPrintable(0x7F));
    EXPECT_FALSE(wire::isPrintable(wire::kFirstHighByte));
}

// The telnet command bytes are the RFC 854 values the decoder and the negotiator both rely on.
// Wire_TelnetCommandValues checks telnet command constants match their assigned protocol bytes.
TEST(Wire, TelnetCommandValues)
{
    EXPECT_EQ(wire::kTelnetIac, 0xFF);
    EXPECT_EQ(wire::kTelnetSe, 0xF0);
    EXPECT_EQ(wire::kTelnetSb, 0xFA);
    EXPECT_EQ(wire::kTelnetWill, 0xFB);
    EXPECT_EQ(wire::kTelnetWont, 0xFC);
    EXPECT_EQ(wire::kTelnetDo, 0xFD);
    EXPECT_EQ(wire::kTelnetDont, 0xFE);
}
} // namespace
} // namespace muckaii
