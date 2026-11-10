#pragma once

#include "Core/Expected.h"
#include "Render/Models/Skeleton.h"

#include <cstdint>
#include <optional>
#include <string>

// 60dc20..60dc7a: evidencia funcional del BMD construido antes de la base lógica.
namespace Client::Scenes
{
class ConstructedBmdPrefix final
{
public:
    // Recibe la cantidad real de objetos construidos antes de la base, no una dirección física.
    [[nodiscard]] Core::Expected<void, std::string> Initialize(std::uint16_t constructedBeforeBase);
    // Sólo4ea030:6154b3 prueba actionCount0. Los demás campos del BMD no se proyectan.
    [[nodiscard]] const Render::Models::Skeleton* BorrowForSetAction() const;
    [[nodiscard]] std::optional<std::uint16_t> Offset() const;

private:
    std::optional<std::uint16_t> m_offset;
    Render::Models::Skeleton m_actionGuard;
};
} // namespace Client::Scenes
