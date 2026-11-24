#include "World/ParticleBurst.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>

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

Core::Expected<bool, ParticleError> CreateParticleBurst(Particles& particles, const glm::vec3& position,
                                                        Crt::CrtRandom& random, bool explosion, float worldClock)
{
    const glm::vec3 point{position.x, position.y, static_cast<float>(static_cast<double>(position.z) + BurstLift)};
    glm::vec3 angles{};
    for (int spark = 0; spark < BurstCount; ++spark)
    {
        // 0x51fe4b/0x51fe6e: también corren si 0x53fe70 no encuentra lugar; su creación no lee el reloj.
        const float pitch = static_cast<float>(random.Next() % PitchSpan + PitchBase);
        const float yaw = static_cast<float>(random.Next() % YawSpan);
        angles = {pitch, 0.0f, yaw};
        const auto created = particles.CreateChecked(FairySparkParticle, point, angles, glm::vec3(1.0f),
                                                     PushSparkSubtype, 1.0f, worldClock, random);
        if (!created.HasValue())
            return Core::Unexpected(created.Error());
    }
    if (!explosion)
        return false;
    // 0x51fea8..0x51fee0: usa los ángulos del último intento incluso si no pudo crear su chispa.
    const glm::vec3 light(std::bit_cast<float>(0x3f333333u));
    const auto created = particles.CreateChecked(ExplosionParticle, point, angles, light, 0, 1.0f, worldClock, random);
    if (!created.HasValue())
        return Core::Unexpected(created.Error());
    return created.Value().has_value();
}

Core::Expected<void, ParticleError> CreatePushBurst(Particles& particles, const glm::vec3& position,
                                                    Crt::CrtRandom& random)
{
    const auto created = CreateParticleBurst(particles, position, random, false);
    if (!created.HasValue())
        return Core::Unexpected(created.Error());
    return {};
}
} // namespace Client::World
