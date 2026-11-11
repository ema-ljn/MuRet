#pragma once
#include "Core/Expected.h"
#include "Render/ParticleBillboardRequest.h"
#include "Render/TextureEnvMode.h"
#include "World/ParticleDrawing.h"
#include "World/Particles.h"
#include <glm/ext/vector_float2.hpp>
#include <cstdint>
namespace Client::World
{
// Servicios obligatorios y síncronos. Lookups CPU estables, sin cargas/reentrada; las otras
// fronteras pueden mutar estado prestado. Nada se retiene ni se difiere al Present.
class IParticleDrawingServices
{
public:
    using Result = Core::Expected<void, ParticleDrawingError>;
    virtual ~IParticleDrawingServices() = default;
    [[nodiscard]] virtual Core::Expected<glm::vec2, ParticleError> Dimensions(std::int32_t image) = 0;
    [[nodiscard]] virtual Core::Expected<std::uint8_t, ParticleError> MaterialFlag(std::int32_t type) = 0;
    [[nodiscard]] virtual Result Material(std::uint8_t rawFlag) = 0;
    [[nodiscard]] virtual Result Subtractive() = 0;
    // 0x620c10: el modo 6 (SRC_ALPHA / ONE_MINUS_SRC_ALPHA), los mismos dos helpers y la cola de 0x620b10.
    [[nodiscard]] virtual Result Blended() = 0;
    [[nodiscard]] virtual Result Type620850() = 0;
    [[nodiscard]] virtual Result Type620870() = 0;
    // glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, modo), fuera de un billboard (0x5489eb, la 6ac/2).
    [[nodiscard]] virtual Result TextureEnv(Render::TextureEnvMode mode) = 0;
    [[nodiscard]] virtual Result Billboard(Render::ParticleBillboardRequest request) = 0;
};
} // namespace Client::World
