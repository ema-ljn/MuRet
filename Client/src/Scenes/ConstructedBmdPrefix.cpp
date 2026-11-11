#include "Scenes/ConstructedBmdPrefix.h"

#include "Core/Expected.h"
#include "Render/Models/Skeleton.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Client::Scenes
{
Core::Expected<void, std::string> ConstructedBmdPrefix::Initialize(std::uint16_t constructedBeforeBase)
{
    constexpr std::uint16_t MaximumNativeOffset = 1023;
    if (constructedBeforeBase > MaximumNativeOffset)
        return Core::Unexpected(std::string("0x60dc51: el prefijo construido excede el resto1024"));
    if (m_offset && *m_offset != constructedBeforeBase)
        return Core::Unexpected(std::string("0x60dbe0: la tabla global ya tiene otro prefijo construido"));
    // 6154b3 escribe count0 en cada BMD, incluso antes de la base lógica. No hay acciones a cargar.
    m_offset = constructedBeforeBase;
    return {};
}
const Render::Models::Skeleton* ConstructedBmdPrefix::BorrowForSetAction() const
{
    if (!m_offset || *m_offset == 0)
        return nullptr;
    return &m_actionGuard;
}
std::optional<std::uint16_t> ConstructedBmdPrefix::Offset() const
{
    return m_offset;
}
} // namespace Client::Scenes
