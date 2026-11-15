#include "World/ParticleBurst.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

namespace Client::World
{
namespace
{
constexpr int BurstCount = 20;
constexpr int PitchSpan = 60;
constexpr int PitchBase = 150;
constexpr int YawSpan = 30;
constexpr int PushSparkSubtype = 2;
constexpr double BurstLift = 80.0; // 0x6b9ffc.
} // namespace

Core::Expected<void, ParticleError> CreatePushBurst(Particles& particles, const glm::vec3& position,
                                                    Render::Models::CrtRandom& random)
{
    const glm::vec3 point{position.x, position.y, static_cast<float>(static_cast<double>(position.z) + BurstLift)};
    for (int spark = 0; spark < BurstCount; ++spark)
    {
        // 0x51fe4b/0x51fe6e: también corren si 0x53fe70 no encuentra lugar; su creación no lee el reloj.
        const float pitch = static_cast<float>(random.Next() % PitchSpan + PitchBase);
        const float yaw = static_cast<float>(random.Next() % YawSpan);
        const auto created = particles.CreateChecked(FairySparkParticle, point, {pitch, 0.0f, yaw}, glm::vec3(1.0f),
                                                     PushSparkSubtype, 1.0f, 0.0f, random);
        if (!created.HasValue())
            return Core::Unexpected(created.Error());
    }
    return {};
}
} // namespace Client::World
