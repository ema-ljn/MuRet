#include "Scenes/ParticleBillboard.h"

#include "Core/Expected.h"
#include "Render/Billboard.h"
#include "Render/Models/OriginalRotation.h"
#include "World/Particles.h"

#include <cmath>
#include <cstdint>
#include <optional>

namespace Client::Scenes
{
Core::Expected<Render::Billboard, World::ParticleError>
MakeParticleBillboard(const Render::Models::OriginalMatrix& view, const World::ParticleDraw& draw,
                      std::optional<std::uint8_t> colorFlag)
{
    if (!colorFlag)
    {
        return Core::Unexpected(World::ParticleError::MissingColorFlag);
    }
    // 621451 prueba C3: unordered toma la misma geometría que ±0, sin alterar el argumento crudo.
    const float rotation = std::isnan(draw.rotation) ? 0.0f : draw.rotation;
    // El parámetro de 16 bits del helper sólo decide alpha 5e4; no es el selector de 32 bits del binder real.
    // Mapear esa excepción exacta evita que bits altos produzcan un falso 5e4 por truncamiento.
    const std::uint16_t alphaCase = draw.texture == static_cast<std::int32_t>(Render::OpaqueAlphaBillboardSlot)
                                        ? Render::OpaqueAlphaBillboardSlot
                                        : std::uint16_t{0};
    return Render::MakeBillboard(view, draw.position, draw.width, draw.height, draw.light, rotation, draw.uv,
                                 draw.uvSize, alphaCase, *colorFlag);
}
} // namespace Client::Scenes
