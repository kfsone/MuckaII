#include "muckaii/transport/telnet.hpp"

#include "muckaii/base/types.hpp"
#include "muckaii/base/wire.hpp"

#include <array>
#include <initializer_list>
#include <string_view>

namespace muckaii::transport
{
namespace
{
// kTtypeSend is the TTYPE subnegotiation request command.
constexpr std::uint8_t kTtypeSend { 0x01 };
// kTtypeIs identifies the client TTYPE response.
constexpr std::uint8_t kTtypeIs { 0x00 };
// kTerminalType is the terminal type advertised after a TTYPE SEND.
constexpr std::string_view kTerminalType { "ansi" };
// kByteBits is the number of bits in a wire byte.
constexpr unsigned kByteBits { 8 };
// kByteMask selects the low byte of a NAWS dimension.
constexpr unsigned kByteMask { 0xFF };
// IAC SB TTYPE SEND IAC SE.
// kTtypeSendLength is the byte count of an unescaped TTYPE SEND block.
constexpr std::size_t kTtypeSendLength { 6 };

// NEW-ENVIRON (connection.md 2.2): command bytes IS = 0, SEND = 1; type bytes VAR = 0, VALUE = 1.
// kEnvironSend is the NEW-ENVIRON request command.
constexpr std::uint8_t kEnvironSend { 0x01 };
// kEnvironIs identifies the NEW-ENVIRON response command.
constexpr std::uint8_t kEnvironIs { 0x00 };
// kEnvironVar marks the USER variable name in NEW-ENVIRON.
constexpr std::uint8_t kEnvironVar { 0x00 };
// kEnvironValue separates the USER name from its value.
constexpr std::uint8_t kEnvironValue { 0x01 };
// kEnvironUserName is the NEW-ENVIRON variable MuckaII advertises.
constexpr std::string_view kEnvironUserName { "USER" };
// IAC SB NEW-ENVIRON SEND, at least this long before the (possibly empty) requested-variable list.
// kEnvironSendMinLength is the shortest NEW-ENVIRON SEND block.
constexpr std::size_t kEnvironSendMinLength { 4 };

// bytes converts telnet command values into an outbound byte buffer.
types::Bytes bytes(std::initializer_list<std::uint8_t> values)
{
    types::Bytes out;
    out.reserve(values.size());
    for (const std::uint8_t value : values)
        out.push_back(std::byte { value });
    return out;
}

// appendSizeByte appends one size byte and doubles an IAC value.
void appendSizeByte(types::Bytes& out, unsigned value)
{
    const auto byte { static_cast<std::uint8_t>(value & kByteMask) };
    out.push_back(std::byte { byte });
    if (byte == wire::kTelnetIac)
        out.push_back(std::byte { wire::kTelnetIac });
}

// at returns one command byte as an unsigned value.
std::uint8_t at(std::span<const std::byte> command, std::size_t index)
{
    return std::to_integer<std::uint8_t>(command[index]);
}
} // namespace

types::Bytes makeNaws(int columns, int rows)
{
    types::Bytes out { bytes({ wire::kTelnetIac, wire::kTelnetSb, kOptionNaws }) };
    const auto   width { static_cast<unsigned>(columns) };
    const auto   height { static_cast<unsigned>(rows) };
    appendSizeByte(out, width >> kByteBits);
    appendSizeByte(out, width);
    appendSizeByte(out, height >> kByteBits);
    appendSizeByte(out, height);
    out.push_back(std::byte { wire::kTelnetIac });
    out.push_back(std::byte { wire::kTelnetSe });
    return out;
}

TelnetNegotiator::TelnetNegotiator(int columns) noexcept
    : mColumns { columns }
{
}

TelnetNegotiator::TelnetNegotiator(int columns, std::string loginName) noexcept
    : mColumns { columns }
    , mLoginName { std::move(loginName) }
{
}

types::Bytes TelnetNegotiator::onCommand(std::span<const std::byte> command)
{
    if (command.size() < 2 || at(command, 0) != wire::kTelnetIac)
        return {};

    const std::uint8_t verb { at(command, 1) };
    if (verb == wire::kTelnetSb)
    {
        // TTYPE SEND gets TTYPE IS "ansi", only if WILL TTYPE was sent. Any other SB is ignored.
        const bool hasTtypeSendLength { command.size() == kTtypeSendLength };
        const bool hasTtypeOption { hasTtypeSendLength && at(command, 2) == kOptionTtype };
        const bool isTtypeSend { hasTtypeOption && at(command, 3) == kTtypeSend };
        if (isTtypeSend && mHasSentWillTtype)
        {
            types::Bytes out { bytes({ wire::kTelnetIac, wire::kTelnetSb, kOptionTtype, kTtypeIs }) };
            for (const char c : kTerminalType)
                out.push_back(std::byte { static_cast<std::uint8_t>(c) });
            out.push_back(std::byte { wire::kTelnetIac });
            out.push_back(std::byte { wire::kTelnetSe });
            return out;
        }
        // NEW-ENVIRON SEND gets NEW-ENVIRON IS USER=<name> (2.2), only when WILL NEW-ENVIRON was
        // sent, which happens only when the login name is non-empty.
        const bool hasEnvironSendLength { command.size() >= kEnvironSendMinLength };
        const bool hasEnvironOption { hasEnvironSendLength && at(command, 2) == kOptionNewEnviron };
        const bool isEnvironSend { hasEnvironOption && at(command, 3) == kEnvironSend };
        if (isEnvironSend && ! mLoginName.empty())
        {
            types::Bytes out { bytes(
                { wire::kTelnetIac, wire::kTelnetSb, kOptionNewEnviron, kEnvironIs, kEnvironVar }) };
            for (const char c : kEnvironUserName)
                out.push_back(std::byte { static_cast<std::uint8_t>(c) });
            out.push_back(std::byte { kEnvironValue });
            for (const char c : mLoginName)
                out.push_back(std::byte { static_cast<std::uint8_t>(c) });
            out.push_back(std::byte { wire::kTelnetIac });
            out.push_back(std::byte { wire::kTelnetSe });
            return out;
        }
        return {};
    }
    if (command.size() < 3)
        return {};

    const std::uint8_t option { at(command, 2) };
    switch (verb)
    {
        case wire::kTelnetDo:
            if (option == kOptionTtype)
            {
                if (mHasSentWillTtype)
                    return {};
                mHasSentWillTtype = true;
                return bytes({ wire::kTelnetIac, wire::kTelnetWill, kOptionTtype });
            }
            if (option == kOptionNaws)
            {
                if (mIsNawsAgreed)
                    return {};
                mIsNawsAgreed = true;
                types::Bytes       out { bytes({ wire::kTelnetIac, wire::kTelnetWill, kOptionNaws }) };
                const types::Bytes naws { makeNaws(mColumns, kNawsRows) };
                out.insert(out.end(), naws.begin(), naws.end());
                return out;
            }
            if (option == kOptionNewEnviron)
            {
                // First time only (connection.md 2.1); the reference sends WILL when the login
                // name is non-empty, else Clio's WONT.
                if (mHasAnsweredDoNewEnviron)
                    return {};
                mHasAnsweredDoNewEnviron = true;
                return bytes(
                    { wire::kTelnetIac, mLoginName.empty() ? wire::kTelnetWont : wire::kTelnetWill, option });
            }
            // ECHO, SGA (Clio's WONT) and every other option.
            return bytes({ wire::kTelnetIac, wire::kTelnetWont, option });
        case wire::kTelnetWill:
            if (option == kOptionEcho)
                return {};
            if (option == kOptionSga)
            {
                if (mHasAnsweredWillSga)
                    return {};
                mHasAnsweredWillSga = true;
                return bytes({ wire::kTelnetIac, wire::kTelnetDo, kOptionSga });
            }
            return bytes({ wire::kTelnetIac, wire::kTelnetDont, option });
        default:
            // DONT and WONT get nothing.
            return {};
    }
}

types::Bytes TelnetNegotiator::setColumns(int columns)
{
    if (columns == mColumns)
        return {};
    mColumns = columns;
    if (! mIsNawsAgreed)
        return {};
    return makeNaws(mColumns, kNawsRows);
}

types::OptBytes TelnetNegotiator::findNawsForClientMode() const
{
    if (! mIsNawsAgreed)
        return std::nullopt;
    return makeNaws(mColumns, kNawsRows);
}

void TelnetNegotiator::reset() noexcept
{
    mHasSentWillTtype        = false;
    mIsNawsAgreed            = false;
    mHasAnsweredWillSga      = false;
    mHasAnsweredDoNewEnviron = false;
}
} // namespace muckaii::transport
