#pragma once

#include "muckaii/base/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace muckaii::transport
{
// Telnet option codes (docs/spec/connection.md 2); the command bytes are in muckaii/base/wire.hpp.
// kOptionEcho is the telnet ECHO option code.
constexpr std::uint8_t kOptionEcho { 0x01 };
// kOptionSga is the telnet Suppress Go Ahead option code.
constexpr std::uint8_t kOptionSga { 0x03 };
// kOptionTtype is the telnet Terminal Type option code.
constexpr std::uint8_t kOptionTtype { 0x18 };
// kOptionNaws is the telnet Negotiate About Window Size option code.
constexpr std::uint8_t kOptionNaws { 0x1F };
// kOptionNewEnviron is the telnet NEW-ENVIRON option code.
constexpr std::uint8_t kOptionNewEnviron { 0x27 };

// Rows told in NAWS (docs/spec/connection.md 4): always 21.
// kNawsRows is the fixed terminal height sent in NAWS.
constexpr int kNawsRows { 21 };

// The client side of telnet negotiation, as docs/spec/connection.md 2 decides it and its
// `default:` lines settle the open rows: WONT SGA, WONT NEW-ENVIRON, DO SGA once per connection,
// NAWS at the effective column count. A pure state machine: commands in, reply bytes out. The
// commands come from the mud decoder's `telnet` events; the replies go on the session's one
// ordered outbound queue.
// TelnetNegotiator tracks telnet option agreement and builds client replies.
class TelnetNegotiator
{
public:
    // TelnetNegotiator initializes width negotiation without a login name.
    explicit TelnetNegotiator(int columns) noexcept;

    // As above, with the telnet login name to advertise over NEW-ENVIRON (connection.md 2.1): a
    // non-empty name sends WILL (once) and answers NEW-ENVIRON SEND with IS USER=<name>; an empty
    // name keeps Clio's WONT.
    // TelnetNegotiator initializes width negotiation and the login name advertised by NEW-ENVIRON.
    TelnetNegotiator(int columns, std::string loginName) noexcept;

    // The reply to one command: IAC, the command, and its option or subnegotiation bytes, with IAC
    // IAC inside a subnegotiation already undone. Empty when nothing is sent.
    // onCommand returns the reply for one server telnet command.
    [[nodiscard]] types::Bytes onCommand(std::span<const std::byte> command);

    // The column count changed. Returns the NAWS subnegotiation to send if NAWS was agreed and the
    // count differs from the last one advertised (connection.md 4.1); empty otherwise.
    // setColumns updates the width and returns NAWS when an agreed size changes.
    [[nodiscard]] types::Bytes setColumns(int columns);

    // The NAWS subnegotiation sent immediately before client-mode entry, even if unchanged
    // (connection.md 2.3). nullopt if NAWS was not agreed.
    // findNawsForClientMode returns the current NAWS block when that option is agreed.
    [[nodiscard]] types::OptBytes findNawsForClientMode() const;

    // A new connection: the one-shot guards re-arm; the advertised size is kept.
    // reset rearms one-shot negotiation replies for a new connection.
    void reset() noexcept;

    // isNawsAgreed reports whether the peer accepted NAWS.
    [[nodiscard]] bool isNawsAgreed() const noexcept
    {
        return mIsNawsAgreed;
    }

    // getColumns returns the current width used for negotiation.
    [[nodiscard]] int getColumns() const noexcept
    {
        return mColumns;
    }

private:
    int         mColumns {};
    std::string mLoginName;
    bool        mHasSentWillTtype {};
    bool        mIsNawsAgreed {};
    bool        mHasAnsweredWillSga {};
    bool        mHasAnsweredDoNewEnviron {};
};

// NAWS: IAC SB NAWS width(2) height(2) IAC SE, every 255 in the size doubled.
// makeNaws builds a NAWS subnegotiation for the given dimensions.
[[nodiscard]] types::Bytes makeNaws(int columns, int rows);
} // namespace muckaii::transport
