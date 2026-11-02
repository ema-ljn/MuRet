#include "TickGrid.h"

namespace Labs
{
namespace
{
// Microsegundos por milisegundo.
constexpr std::int64_t UsPerMs = 1'000;
} // namespace

TickGrid::TickGrid(std::int64_t tickUs) : m_tickUs(tickUs) {}

// Primero el tick (la división entera tira lo que pasó desde el último), y después los ms. El
// cast a 32 bits sin signo es la vuelta del DWORD: está definido, módulo 2^32.
std::uint32_t TickGrid::Ms(std::int64_t nowUs) const
{
    return static_cast<std::uint32_t>((nowUs / m_tickUs) * m_tickUs / UsPerMs);
}

std::int64_t TickGrid::TickUs() const
{
    return m_tickUs;
}
} // namespace Labs
