#include "Render/Models/LogicalAccess.h"

#include "Render/Models/Cloth.h"

#include <cstdint>

namespace Client::Render::Models
{
LogicalAccess::LogicalAccess(std::uint8_t& depth, CrtRandom& random, std::uint16_t closeRandoms)
    : m_depth(depth), m_random(random), m_closeRandoms(closeRandoms)
{
    m_depth = static_cast<std::uint8_t>(m_depth + 1u);
}
LogicalAccess::~LogicalAccess()
{
    m_depth = static_cast<std::uint8_t>(m_depth - 1u);
    if (m_depth != 0)
        return;
    for (std::uint16_t i = 0; i < m_closeRandoms; ++i)
        static_cast<void>(m_random.Next());
}
} // namespace Client::Render::Models
