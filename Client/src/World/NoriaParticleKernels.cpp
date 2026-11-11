#include "World/NoriaParticleKernels.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>

namespace Client::World
{
namespace
{
constexpr float SparkScaleStep = std::bit_cast<float>(0x3dcccccdu);
constexpr float ShinySineStep = std::bit_cast<float>(0x3e32b8c2u); // 6ba370.
constexpr float ShinyOneScale = 0.75f;
constexpr float SparkCollision = std::bit_cast<float>(0xbf19999au);
// 0x544f7b: el 5c5/7 (las chispas de la mariposa de la selección) cae con 0x6b8a20; el /0, con 0x6b8818.
constexpr double SparkFall = 2.0;
constexpr double FlutterFall = static_cast<double>(std::bit_cast<float>(0x3f19999au));
constexpr std::int32_t FlutterSparkSubtype = 7;
constexpr float SparkLifeLight = 0.0625f;
constexpr float ShinyTurn = 12.0f;
} // namespace

bool IsNoriaParticle(std::int32_t type, std::int32_t subtype)
{
    return (type == ShinyParticle && (subtype == 0 || subtype == 1)) ||
           (type == FairySparkParticle && (subtype == 0 || subtype == FlutterSparkSubtype));
}

void CreateNoriaParticle(Particle& particle, Render::Models::CrtRandom& random)
{
    if (particle.type == ShinyParticle)
    {
        // 542ab9..542acd: los subtipos0/1 retornan sin rand y conservan la escala del prefijo.
        particle.life = 18;
        particle.angles.x = 45.0f;
        return;
    }
    // 540333..54055a, subtipo0: matriz DESPUÉS de escribir el yaw, sin cambiar pitch/roll.
    particle.scale = static_cast<float>(static_cast<double>((random.Next() & 3) + 4) * SparkScaleStep);
    if (particle.subtype == FlutterSparkSubtype)
    {
        // 54038c..5403a7: sólo la vida; sin giro ni velocidad, y sin sumar la escala del argumento.
        constexpr int FlutterLifeSpan = 12;
        particle.life = random.Next() % FlutterLifeSpan + FlutterLifeSpan;
        return;
    }
    particle.life = (random.Next() & 15) + 24;
    particle.angles.z = static_cast<float>(random.Next() % 360);
    const auto matrix = Render::Models::OriginalAngleMatrix(particle.angles);
    particle.verticalSpeed = static_cast<float>((random.Next() & 15) + 6);
    const float forward = static_cast<float>(static_cast<double>(random.Next() % 20 + 20) * SparkScaleStep);
    particle.velocity = Render::Models::OriginalVectorRotate(glm::vec3(0.0f, forward, 0.0f), matrix);
}

Core::Expected<void, ParticleError> StepNoriaParticle(Particle& particle, const ParticleTerrainHeight& terrainHeight)
{
    if (particle.type == ShinyParticle)
    {
        // 546ca3..546ccc: fst escala sin pop; /1 multiplica el seno RETENIDO por0.75.
        const double argument = static_cast<double>(particle.life) * ShinySineStep;
        const auto sine = Render::Models::Detail::SineProductPC53(argument, 1.0f);
        if (!sine)
            return Core::Unexpected(ParticleError::UnrepresentableTrigonometry);
        particle.scale = static_cast<float>(*sine);
        if (particle.subtype == 1)
        {
            const auto scaled = Render::Models::Detail::SineProductPC53(argument, ShinyOneScale);
            if (!scaled)
                return Core::Unexpected(ParticleError::UnrepresentableTrigonometry);
            particle.scale = static_cast<float>(*scaled);
            particle.rotation = static_cast<float>(static_cast<double>(particle.rotation) - ShinyTurn);
        }
        return {};
    }
    // 544f29..544fe9: luz, Z y velocidad antes de consultar terreno; el movimiento común ya corrió.
    const float light = static_cast<float>(static_cast<double>(particle.life) * SparkLifeLight);
    particle.light = glm::vec3(light);
    particle.position.z = static_cast<float>(static_cast<double>(particle.position.z) + particle.verticalSpeed);
    particle.verticalSpeed = static_cast<float>(static_cast<double>(particle.verticalSpeed) -
                                                (particle.subtype == FlutterSparkSubtype ? FlutterFall : SparkFall));
    if (!terrainHeight)
        return Core::Unexpected(ParticleError::MissingTerrainHeight);
    const double height = terrainHeight(particle.position.x, particle.position.y);
    if (height > static_cast<double>(particle.position.z))
    {
        particle.position.z = static_cast<float>(height);
        particle.verticalSpeed = static_cast<float>(static_cast<double>(particle.verticalSpeed) * SparkCollision);
        particle.life -= 4;
    }
    for (int axis = 0; axis < 3; ++axis)
        particle.position[axis] =
            static_cast<float>(static_cast<double>(particle.position[axis]) + particle.velocity[axis]);
    return {};
}
} // namespace Client::World
