#include "Models/LogicalAccess.h"

#include "Crt/CrtRandom.h"

#include <cstdint>

// Contract: Client/docs/research/06-render-escenas-y-ui.md, "La tabla de protección". The access count is the byte
// after the encrypted copy of the entry, so it adds and subtracts in 8 bits.
namespace Client::Models
{
LogicalAccess::LogicalAccess(std::uint8_t& depth, Crt::CrtRandom& random, std::uint16_t closeRandoms)
    : m_depth(depth), m_random(random), m_closeRandoms(closeRandoms)
{
    // Opening costs nothing: what the research counts per open and close is the close (3848 in MoveHero,
    // 0x57d051..0x57d10d; 8 in the camera, 0x635ae0, for the two cells).
    // The first access's decryption (0x40b440, 0x412810) is not ported: the value is already in the clear.
    m_depth = static_cast<std::uint8_t>(m_depth + 1u);
}

LogicalAccess::LogicalAccess(std::uint8_t& depth, Crt::CrtRandom& random, std::uint16_t closeRandoms,
                             std::uint32_t& left)
    : LogicalAccess(depth, random, closeRandoms)
{
    m_left = &left;
}

LogicalAccess::~LogicalAccess()
{
    m_depth = static_cast<std::uint8_t>(m_depth - 1u);
    if (m_depth != 0)
    {
        return;
    }
    // The encryption of the last close: one rand() per byte (0x40b4d0 calls 0x69fd70 at 0x40b52e).
    constexpr std::uint16_t LeftBytes = 4;
    if (m_left == nullptr || m_closeRandoms > LeftBytes)
    {
        m_random.Discard(m_closeRandoms);
        return;
    }
    // 0x4128e2..0x4128f9: AND 0x800000ff of a non-negative rand(), into byte i of the variable.
    constexpr std::uint32_t ByteMask = 0xff;
    std::uint32_t left = 0;
    for (std::uint16_t index = 0; index < m_closeRandoms; ++index)
        left |= (static_cast<std::uint32_t>(m_random.Next()) & ByteMask) << (8u * index);
    *m_left = left;
}
} // namespace Client::Models
