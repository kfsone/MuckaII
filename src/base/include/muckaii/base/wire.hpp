#pragma once

#include <cstddef>
#include <cstdint>

// The byte values every layer agrees on: 7-bit ASCII controls and the printable range, the telnet
// command bytes (docs/spec/protocol.md, Telnet layer), and the first byte above the 7-bit range.
// A layer that needs one of these takes it from here and never defines its own.
namespace muckaii::wire
{
constexpr std::uint8_t kBel { 0x07 };
constexpr std::uint8_t kTab { 0x09 };
constexpr std::uint8_t kLf { 0x0A };
constexpr std::uint8_t kCr { 0x0D };
constexpr std::uint8_t kEsc { 0x1B };

// The printable 7-bit range, space through tilde: the only bytes the client sends as text.
constexpr std::uint8_t kFirstPrintable { 0x20 };
constexpr std::uint8_t kLastPrintable { 0x7E };

// The first byte past 7-bit ASCII. Bytes from here up are MUD2 markup or UTF-8, never plain text.
constexpr std::uint8_t kFirstHighByte { 0x80 };

// UTF-8 continuation bytes are 10xxxxxx: the mask isolates the tag, and the payload is the low six bits.
constexpr std::uint8_t kUtf8ContinuationMask { 0xC0 };
constexpr std::uint8_t kUtf8Continuation { 0x80 };
constexpr std::uint8_t kUtf8ContinuationBits { 0x3F };
constexpr unsigned     kUtf8ContinuationShift { 6 };

constexpr std::uint8_t kTelnetSe { 0xF0 };
constexpr std::uint8_t kTelnetSb { 0xFA };
constexpr std::uint8_t kTelnetWill { 0xFB };
constexpr std::uint8_t kTelnetWont { 0xFC };
constexpr std::uint8_t kTelnetDo { 0xFD };
constexpr std::uint8_t kTelnetDont { 0xFE };
constexpr std::uint8_t kTelnetIac { 0xFF };

// isPrintable reports whether `value` is a printable 7-bit character.
[[nodiscard]] constexpr bool isPrintable(std::uint8_t value) noexcept
{
    return value >= kFirstPrintable && value <= kLastPrintable;
}

static_assert(kFirstPrintable < kLastPrintable && kLastPrintable < kFirstHighByte);
} // namespace muckaii::wire
