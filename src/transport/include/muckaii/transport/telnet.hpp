#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace muckaii::transport
{
// Telnet option codes and commands (docs/spec/connection.md 2).
constexpr std::uint8_t kTelnetIac { 0xFF };
constexpr std::uint8_t kTelnetDont { 0xFE };
constexpr std::uint8_t kTelnetDo { 0xFD };
constexpr std::uint8_t kTelnetWont { 0xFC };
constexpr std::uint8_t kTelnetWill { 0xFB };
constexpr std::uint8_t kTelnetSb { 0xFA };
constexpr std::uint8_t kTelnetSe { 0xF0 };
constexpr std::uint8_t kOptionEcho { 0x01 };
constexpr std::uint8_t kOptionSga { 0x03 };
constexpr std::uint8_t kOptionTtype { 0x18 };
constexpr std::uint8_t kOptionNaws { 0x1F };
constexpr std::uint8_t kOptionNewEnviron { 0x27 };

// Rows told in NAWS (docs/spec/connection.md 4): always 21.
constexpr int kNawsRows { 21 };

// The client side of telnet negotiation, as docs/spec/connection.md 2 decides it and its
// `default:` lines settle the open rows: WONT SGA, WONT NEW-ENVIRON, DO SGA once per connection,
// NAWS at the effective column count. A pure state machine: commands in, reply bytes out. The
// commands come from the mud decoder's `telnet` events; the replies go on the session's one
// ordered outbound queue.
class TelnetNegotiator
{
public:
    explicit TelnetNegotiator(int columns) noexcept;

    // The reply to one command: IAC, the command, and its option or subnegotiation bytes, with IAC
    // IAC inside a subnegotiation already undone. Empty when nothing is sent.
    [[nodiscard]] std::vector<std::byte> onCommand(std::span<const std::byte> command);

    // The column count changed. Returns the NAWS subnegotiation to send if NAWS was agreed and the
    // count differs from the last one advertised (connection.md 4.1); empty otherwise.
    [[nodiscard]] std::vector<std::byte> setColumns(int columns);

    // The NAWS subnegotiation sent immediately before client-mode entry, even if unchanged
    // (connection.md 2.3). nullopt if NAWS was not agreed.
    [[nodiscard]] std::optional<std::vector<std::byte>> findNawsForClientMode() const;

    // A new connection: the one-shot guards re-arm; the advertised size is kept.
    void reset() noexcept;

    [[nodiscard]] bool isNawsAgreed() const noexcept
    {
        return mIsNawsAgreed;
    }

    [[nodiscard]] int getColumns() const noexcept
    {
        return mColumns;
    }

private:
    int  mColumns {};
    bool mHasSentWillTtype {};
    bool mIsNawsAgreed {};
    bool mHasAnsweredWillSga {};
};

// NAWS: IAC SB NAWS width(2) height(2) IAC SE, every 255 in the size doubled.
[[nodiscard]] std::vector<std::byte> makeNaws(int columns, int rows);
} // namespace muckaii::transport
