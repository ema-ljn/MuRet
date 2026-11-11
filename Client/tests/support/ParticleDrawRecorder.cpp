#include "support/ParticleDrawRecorder.h"

#include "Core/Expected.h"
#include "Render/ParticleBillboardRequest.h"
#include "Render/TextureEnvMode.h"
#include "World/IParticleDrawingServices.h"
#include "World/ParticleDrawing.h"
#include "World/Particles.h"

#include <glm/ext/vector_float2.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace Client::Tests
{
Core::Expected<glm::vec2, World::ParticleError> ParticleDrawRecorder::Dimensions(std::int32_t image)
{
    const auto found = dimensions.find(image);
    if (found == dimensions.end())
    {
        return Core::Unexpected(World::ParticleError::MissingTextureSize);
    }
    return found->second;
}

Core::Expected<std::uint8_t, World::ParticleError> ParticleDrawRecorder::MaterialFlag(std::int32_t type)
{
    const auto found = materialFlags.find(type);
    if (found == materialFlags.end())
    {
        return Core::Unexpected(World::ParticleError::MissingMaterialFlag);
    }
    return found->second;
}

ParticleDrawRecorder::Result ParticleDrawRecorder::Material(std::uint8_t rawFlag)
{
    m_blend = rawFlag == 3 ? RecordedParticleDraw::Blend::Additive : RecordedParticleDraw::Blend::Alpha;
    return {};
}

ParticleDrawRecorder::Result ParticleDrawRecorder::Subtractive()
{
    m_blend = RecordedParticleDraw::Blend::Subtractive;
    return {};
}

ParticleDrawRecorder::Result ParticleDrawRecorder::Blended()
{
    m_blend = RecordedParticleDraw::Blend::Blended;
    return {};
}

ParticleDrawRecorder::Result ParticleDrawRecorder::Type620850()
{
    return Core::Unexpected(World::ParticleDrawingError{
        World::ParticleError::MissingDrawingService, World::ParticleDrawingPhase::Type620850, {}});
}

ParticleDrawRecorder::Result ParticleDrawRecorder::Type620870()
{
    return Core::Unexpected(World::ParticleDrawingError{
        World::ParticleError::MissingDrawingService, World::ParticleDrawingPhase::Type620870, {}});
}

ParticleDrawRecorder::Result ParticleDrawRecorder::TextureEnv(Render::TextureEnvMode mode)
{
    textureEnv = mode;
    ++textureEnvCalls;
    return {};
}

ParticleDrawRecorder::Result ParticleDrawRecorder::Billboard(Render::ParticleBillboardRequest request)
{
    draws.push_back(RecordedParticleDraw{.texture = request.selector,
                                         .blend = m_blend,
                                         .textureEnv = textureEnv,
                                         .position = request.position.get(),
                                         .width = request.width,
                                         .height = request.height,
                                         .light = request.light.get(),
                                         .rotation = request.rotation,
                                         .uv = request.uv,
                                         .uvSize = request.uvSize});
    return {};
}

Core::Expected<std::optional<RecordedParticleDraw>, World::ParticleDrawingError>
DrawOneParticle(World::Particle& particle, std::optional<glm::vec2> textureSize,
                std::optional<std::uint8_t> materialFlag, float clock, std::size_t slot, std::uint8_t mode,
                Render::TextureEnvMode* textureEnv)
{
    World::Particles pool;
    pool.All()[slot] = particle;
    ParticleDrawRecorder recorder;
    if (textureSize)
    {
        recorder.dimensions.insert_or_assign(particle.texture, *textureSize);
    }
    if (materialFlag)
    {
        recorder.materialFlags.insert_or_assign(particle.type, *materialFlag);
    }
    const auto drawn = World::DrawParticlesChecked(pool, mode, std::cref(clock), recorder);
    particle = pool.All()[slot];
    if (textureEnv != nullptr)
    {
        *textureEnv = recorder.textureEnv;
    }
    if (!drawn.HasValue())
    {
        return Core::Unexpected(drawn.Error().error);
    }
    if (recorder.draws.empty())
    {
        return std::optional<RecordedParticleDraw>{};
    }
    return std::optional(recorder.draws.front());
}
} // namespace Client::Tests
