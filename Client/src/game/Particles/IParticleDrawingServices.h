#pragma once
#include "Core/Expected.h"
#include "Render/ParticleBillboardRequest.h"
#include "Render/TextureEnvMode.h"
#include "Particles/ParticleDrawing.h"
#include "Particles/Particles.h"
#include <glm/ext/vector_float2.hpp>
#include <cstdint>
namespace Client::Particles
{
// Required, synchronous services. Stable CPU lookups, without loads/reentry; the other boundaries may mutate borrowed
// state. Nothing is retained or deferred to Present.
class IParticleDrawingServices
{
public:
    using Result = Core::Expected<void, ParticleDrawingError>;
    virtual ~IParticleDrawingServices() = default;
    [[nodiscard]] virtual Core::Expected<glm::vec2, ParticleError> Dimensions(std::int32_t image) = 0;
    [[nodiscard]] virtual Core::Expected<std::uint8_t, ParticleError> MaterialFlag(std::int32_t type) = 0;
    [[nodiscard]] virtual Result Material(std::uint8_t rawFlag) = 0;
    [[nodiscard]] virtual Result Subtractive() = 0;
    // 0x620c10: mode 6 (SRC_ALPHA / ONE_MINUS_SRC_ALPHA), the same two helpers and the tail of 0x620b10.
    [[nodiscard]] virtual Result Blended() = 0;
    [[nodiscard]] virtual Result Type620850() = 0;
    [[nodiscard]] virtual Result Type620870() = 0;
    // glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, mode), outside a billboard (0x5489eb, the 6ac/2).
    [[nodiscard]] virtual Result TextureEnv(Render::TextureEnvMode mode) = 0;
    [[nodiscard]] virtual Result Billboard(Render::ParticleBillboardRequest request) = 0;
};
} // namespace Client::Particles
