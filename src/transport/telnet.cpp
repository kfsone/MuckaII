#include "muckaii/transport/telnet.hpp"

#include <array>
#include <initializer_list>
#include <string_view>

namespace muckaii::transport
{
namespace
{
constexpr std::uint8_t     kTtypeSend { 0x01 };
constexpr std::uint8_t     kTtypeIs { 0x00 };
constexpr std::string_view kTerminalType { "ansi" };
constexpr unsigned         kByteBits { 8 };
constexpr unsigned         kByteMask { 0xFF };
// IAC SB TTYPE SEND IAC SE.
constexpr std::size_t kTtypeSendLength { 6 };

std::vector<std::byte> bytes(std::initializer_list<std::uint8_t> values)
{
    std::vector<std::byte> out;
    out.reserve(values.size());
    for (const std::uint8_t value : values)
        out.push_back(std::byte { value });
    return out;
}

void appendSizeByte(std::vector<std::byte>& out, unsigned value)
{
    const auto byte { static_cast<std::uint8_t>(value & kByteMask) };
    out.push_back(std::byte { byte });
    if (byte == kTelnetIac)
        out.push_back(std::byte { kTelnetIac });
}

std::uint8_t at(std::span<const std::byte> command, std::size_t index)
{
    return std::to_integer<std::uint8_t>(command[index]);
}
} // namespace

std::vector<std::byte> makeNaws(int columns, int rows)
{
    std::vector<std::byte> out { bytes({ kTelnetIac, kTelnetSb, kOptionNaws }) };
    const auto             width { static_cast<unsigned>(columns) };
    const auto             height { static_cast<unsigned>(rows) };
    appendSizeByte(out, width >> kByteBits);
    appendSizeByte(out, width);
    appendSizeByte(out, height >> kByteBits);
    appendSizeByte(out, height);
    out.push_back(std::byte { kTelnetIac });
    out.push_back(std::byte { kTelnetSe });
    return out;
}

TelnetNegotiator::TelnetNegotiator(int columns) noexcept
    : mColumns { columns }
{
}

std::vector<std::byte> TelnetNegotiator::onCommand(std::span<const std::byte> command)
{
    if (command.size() < 2 || at(command, 0) != kTelnetIac)
        return {};

    const std::uint8_t verb { at(command, 1) };
    if (verb == kTelnetSb)
    {
        // TTYPE SEND gets TTYPE IS "ansi", only if WILL TTYPE was sent. Any other SB is ignored.
        const bool isTtypeSend { command.size() == kTtypeSendLength && at(command, 2) == kOptionTtype
                                 && at(command, 3) == kTtypeSend };
        if (! isTtypeSend || ! mHasSentWillTtype)
            return {};
        std::vector<std::byte> out { bytes({ kTelnetIac, kTelnetSb, kOptionTtype, kTtypeIs }) };
        for (const char c : kTerminalType)
            out.push_back(std::byte { static_cast<std::uint8_t>(c) });
        out.push_back(std::byte { kTelnetIac });
        out.push_back(std::byte { kTelnetSe });
        return out;
    }
    if (command.size() < 3)
        return {};

    const std::uint8_t option { at(command, 2) };
    switch (verb)
    {
        case kTelnetDo:
            if (option == kOptionTtype)
            {
                if (mHasSentWillTtype)
                    return {};
                mHasSentWillTtype = true;
                return bytes({ kTelnetIac, kTelnetWill, kOptionTtype });
            }
            if (option == kOptionNaws)
            {
                if (mIsNawsAgreed)
                    return {};
                mIsNawsAgreed = true;
                std::vector<std::byte>       out { bytes({ kTelnetIac, kTelnetWill, kOptionNaws }) };
                const std::vector<std::byte> naws { makeNaws(mColumns, kNawsRows) };
                out.insert(out.end(), naws.begin(), naws.end());
                return out;
            }
            // ECHO, SGA (Clio's WONT), NEW-ENVIRON (Clio's WONT) and every other option.
            return bytes({ kTelnetIac, kTelnetWont, option });
        case kTelnetWill:
            if (option == kOptionEcho)
                return {};
            if (option == kOptionSga)
            {
                if (mHasAnsweredWillSga)
                    return {};
                mHasAnsweredWillSga = true;
                return bytes({ kTelnetIac, kTelnetDo, kOptionSga });
            }
            return bytes({ kTelnetIac, kTelnetDont, option });
        default:
            // DONT and WONT get nothing.
            return {};
    }
}

std::vector<std::byte> TelnetNegotiator::setColumns(int columns)
{
    if (columns == mColumns)
        return {};
    mColumns = columns;
    if (! mIsNawsAgreed)
        return {};
    return makeNaws(mColumns, kNawsRows);
}

std::optional<std::vector<std::byte>> TelnetNegotiator::findNawsForClientMode() const
{
    if (! mIsNawsAgreed)
        return std::nullopt;
    return makeNaws(mColumns, kNawsRows);
}

void TelnetNegotiator::reset() noexcept
{
    mHasSentWillTtype   = false;
    mIsNawsAgreed       = false;
    mHasAnsweredWillSga = false;
}
} // namespace muckaii::transport
