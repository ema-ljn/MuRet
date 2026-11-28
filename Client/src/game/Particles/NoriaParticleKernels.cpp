#include "Particles/NoriaParticleKernels.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <optional>

namespace Client::Particles
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
// La 5c5/1, las chispas del bastón 0xc18 (0x50f5a8), y las de fuera de la tabla: vida (rand() & 7) + 8
// (0x5403eb..0x5403ff) y el paso del /0.
constexpr std::int32_t PushSparkSubtype = 2;
constexpr float PushSparkScale = 2.0f;
constexpr double PushSparkSpeed = 3.0; // 0x5404b8/0x5404c4, 0x6b86b8.
// 0x5404cc..0x5404f2: la /4 triplica la escala (0x6b86b8) y multiplica por 10 (0x6b87a8) el vector local.
constexpr std::int32_t StrongSparkSubtype = 4;
constexpr double StrongSparkScale = 3.0;
constexpr double StrongSparkSpeed = 10.0;
// 0x5403ad..0x5403e9: la /5 lee la posición de su dueño (+0x40 → +0x10) al nacer para su partida (+0x6c), vuelve a
// la escala del pedido y vive (rand() & 3) + 12, con la subida en 0 y la textura 0x5c6. Su paso cae 0,3f, con la luz de
// la vida, y sigue a su dueño como la /6 (0x544fe4).
constexpr std::int32_t OwnerBirthSparkSubtype = 5;
constexpr int OwnerBirthLifeMask = 3, OwnerBirthLifeBase = 12;
// 0x5404f9..0x54053c: la /6 vive (rand() & 3) + 4, toma la escala del pedido, la textura 0x5c6, la subida en 0 y el
// vector local (0; (rand() % 20 + 20) · 0,2f (0x6b8854); 0). Su paso cae 0,3f (0x6b89dc), su luz es el 0,9f de edx
// (0x544807, 0x54481d o 0x544873) y sigue al dueño (0x544fef).
constexpr std::int32_t OwnedSparkSubtype = 6;
constexpr std::int32_t OwnedSparkTexture = 0x5c6;
constexpr double OwnedSparkStep = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
constexpr double OwnedSparkFall = static_cast<double>(std::bit_cast<float>(0x3e99999au));
constexpr float OwnedSparkLight = std::bit_cast<float>(0x3f666666u);
constexpr float SparkLifeLight = 0.0625f;
constexpr float ShinyTurn = 12.0f; // 0x6b8864
// 0x542ad3..0x542b06: la 5fc/2 vive 25, gira rand() % 360 y sale con la velocidad (−0,1f; −0,5; −1) (0xbdcccccd,
// 0xbf000000, 0xbf800000) que el movimiento común gira con los ángulos (el x en 45, 0x542ac6).
constexpr std::int32_t FallingShineSubtype = 2;
constexpr std::int32_t FallingShineLife = 25;
constexpr int FallingShineTurns = 360;
constexpr glm::vec3 FallingShineVelocity{std::bit_cast<float>(0xbdcccccdu), std::bit_cast<float>(0xbf000000u),
                                         std::bit_cast<float>(0xbf800000u)};
// 0x546c1b..0x546c96: la caída por (rand() & 7) · 0,05f (0x6b8a48), la luz por 0x6ba264 y el sprite flare01 (0x5ac)
// con la escala por 2/3 (0x6b8c64).
constexpr int FallingShineFallMask = 7;
constexpr double FallingShineFall = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));
constexpr double FallingShineFade = static_cast<double>(std::bit_cast<float>(0x3f73cf3eu));
constexpr double FallingShineSpriteScale = static_cast<double>(std::bit_cast<float>(0x3f2aaaabu));
constexpr std::uint16_t FallingShineSprite = 0x5ac;
} // namespace

// 0x540333..0x54055a: la 5c5 trata cualquier subtipo; la /0, /2, /3, /4 y /6 salen con su vector, la /7 con su vida,
// la /5 con la posición de su dueño y las demás (la /1 y las de fuera) por el camino de la /1 (0x5403eb).
bool IsNoriaParticle(std::int32_t type, std::int32_t subtype)
{
    return (type == ShinyParticle && (subtype == 0 || subtype == 1)) || IsFallingShine(type, subtype) ||
           type == FairySparkParticle;
}

bool IsFallingShine(std::int32_t type, std::int32_t subtype)
{
    return type == ShinyParticle && subtype == FallingShineSubtype;
}

bool IsOwnedFairySpark(std::int32_t type, std::int32_t subtype)
{
    return type == FairySparkParticle && subtype == OwnedSparkSubtype;
}

bool IsOwnerBirthFairySpark(std::int32_t type, std::int32_t subtype)
{
    return type == FairySparkParticle && subtype == OwnerBirthSparkSubtype;
}

Core::Expected<void, ParticleError> CreateNoriaParticle(Particle& particle, float scale, Crt::CrtRandom& random,
                                                        const glm::vec3* ownerPosition)
{
    if (particle.type == ShinyParticle)
    {
        // 542ab9..542acd: los subtipos menos la /2 retornan sin rand y conservan la escala del prefijo.
        particle.life = 18;
        particle.angles.x = 45.0f;
        if (particle.subtype != FallingShineSubtype)
            return {};
        particle.life = FallingShineLife;
        const int turn = random.Next() % FallingShineTurns;
        particle.velocity = FallingShineVelocity;
        particle.rotation = static_cast<float>(turn);
        return {};
    }
    // 540333..54055a, subtipo0: matriz DESPUÉS de escribir el yaw, sin cambiar pitch/roll.
    particle.scale = static_cast<float>(static_cast<double>((random.Next() & 3) + 4) * SparkScaleStep);
    if (particle.subtype == FlutterSparkSubtype)
    {
        // 54038c..5403a7: sólo la vida; sin giro ni velocidad, y sin sumar la escala del argumento.
        constexpr int FlutterLifeSpan = 12;
        particle.life = random.Next() % FlutterLifeSpan + FlutterLifeSpan;
        return {};
    }
    const std::int32_t subtype = particle.subtype;
    if (subtype == OwnerBirthSparkSubtype)
    {
        // 0x5403ad..0x5403e9: la partida, la escala del pedido (pisa la de arriba), la vida, la subida y la textura.
        if (ownerPosition == nullptr)
            return Core::Unexpected(ParticleError::MissingOwner);
        particle.start = *ownerPosition;
        particle.scale = scale;
        particle.life = (random.Next() & OwnerBirthLifeMask) + OwnerBirthLifeBase;
        particle.verticalSpeed = 0.0f;
        particle.texture = OwnedSparkTexture;
        return {};
    }
    if (subtype != 0 && subtype != PushSparkSubtype && subtype != 3 && subtype != StrongSparkSubtype &&
        subtype != OwnedSparkSubtype)
    {
        // 0x5403eb..0x540406: sólo la vida, sin giro ni velocidad; la suma de la escala del argumento es del /7.
        constexpr int StaffLifeMask = 7, StaffLifeBase = 8;
        particle.life = (random.Next() & StaffLifeMask) + StaffLifeBase;
        return {};
    }
    particle.life = (random.Next() & 15) + 24;
    particle.angles.z = static_cast<float>(random.Next() % 360);
    const auto matrix = Math::OriginalAngleMatrix(particle.angles);
    particle.verticalSpeed = static_cast<float>((random.Next() & 15) + 6);
    float forward = static_cast<float>(static_cast<double>(random.Next() % 20 + 20) * SparkScaleStep);
    if (subtype == PushSparkSubtype)
    {
        // 0x5404ad..0x5404ca: después de los cinco sorteos y de la matriz, antes de girar el vector local.
        particle.scale *= PushSparkScale;
        forward = static_cast<float>(static_cast<double>(forward) * PushSparkSpeed);
    }
    else if (subtype == StrongSparkSubtype)
    {
        particle.scale = static_cast<float>(static_cast<double>(particle.scale) * StrongSparkScale);
        forward = static_cast<float>(static_cast<double>(forward) * StrongSparkSpeed);
    }
    else if (subtype == OwnedSparkSubtype)
    {
        constexpr int OwnedLifeMask = 3, OwnedLifeBase = 4;
        particle.life = (random.Next() & OwnedLifeMask) + OwnedLifeBase;
        particle.scale = scale;
        particle.verticalSpeed = 0.0f;
        particle.texture = OwnedSparkTexture;
        forward = static_cast<float>(static_cast<double>(random.Next() % 20 + 20) * OwnedSparkStep);
    }
    particle.velocity = Math::OriginalVectorRotate(glm::vec3(0.0f, forward, 0.0f), matrix);
    return {};
}

Core::Expected<void, ParticleError> StepNoriaParticle(Particle& particle, const ParticleTerrainHeight& terrainHeight,
                                                      const ParticleOwnerStates& ownerStates)
{
    if (particle.type == ShinyParticle)
    {
        // 546ca3..546ccc: fst escala sin pop; /1 multiplica el seno RETENIDO por0.75.
        const double argument = static_cast<double>(particle.life) * ShinySineStep;
        const auto sine = Math::Detail::SineProductPC53(argument, 1.0f);
        if (!sine)
            return Core::Unexpected(ParticleError::UnrepresentableTrigonometry);
        particle.scale = static_cast<float>(*sine);
        if (particle.subtype == 1)
        {
            const auto scaled = Math::Detail::SineProductPC53(argument, ShinyOneScale);
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
    // 0x544f4a y 0x544f5a: la /5 y la /6 caen 0,3f; sólo la /6 pisa la luz.
    const bool owned = particle.subtype == OwnedSparkSubtype;
    const bool follows = owned || particle.subtype == OwnerBirthSparkSubtype;
    const double fall = follows ? OwnedSparkFall : particle.subtype == FlutterSparkSubtype ? FlutterFall : SparkFall;
    if (owned)
        particle.light = glm::vec3(OwnedSparkLight);
    particle.verticalSpeed = static_cast<float>(static_cast<double>(particle.verticalSpeed) - fall);
    if (!terrainHeight)
        return Core::Unexpected(ParticleError::MissingTerrainHeight);
    const double height = terrainHeight(particle.position.x, particle.position.y);
    if (height > static_cast<double>(particle.position.z))
    {
        particle.position.z = static_cast<float>(height);
        particle.verticalSpeed = static_cast<float>(static_cast<double>(particle.verticalSpeed) * SparkCollision);
        particle.life -= 4;
    }
    if (!follows)
    {
        for (int axis = 0; axis < 3; ++axis)
            particle.position[axis] =
                static_cast<float>(static_cast<double>(particle.position[axis]) + particle.velocity[axis]);
        return {};
    }
    // 0x544fbd..0x545040: la y y la z guardadas en float (0x544fd2, 0x544fde) y la x retenida; después restan lo que
    // se movió el dueño (+0x40) desde la partida (+0x6c), que pasa a ser su posición. No mira si el dueño vive.
    const double x = static_cast<double>(particle.velocity.x) + static_cast<double>(particle.position.x);
    particle.position.x = static_cast<float>(x);
    const float y = static_cast<float>(static_cast<double>(particle.velocity.y) + particle.position.y);
    particle.position.y = y;
    const float z = static_cast<float>(static_cast<double>(particle.velocity.z) + particle.position.z);
    particle.position.z = z;
    const auto owner = ownerStates ? ownerStates(particle.typedOwner) : std::nullopt;
    if (!owner)
        return Core::Unexpected(ParticleError::MissingOwner);
    const double movedX = static_cast<double>(particle.start.x) - static_cast<double>(owner->position.x);
    const float movedY = static_cast<float>(static_cast<double>(particle.start.y) - owner->position.y);
    const float movedZ = static_cast<float>(static_cast<double>(particle.start.z) - owner->position.z);
    particle.start = owner->position;
    particle.position.x = static_cast<float>(x - movedX);
    particle.position.y = static_cast<float>(static_cast<double>(y) - movedY);
    particle.position.z = static_cast<float>(static_cast<double>(z) - movedZ);
    return {};
}

Core::Expected<void, ParticleError> StepTheFallingShine(Particle& particle, Crt::CrtRandom& random,
                                                        FrameSprites* sprites)
{
    // 0x546c0f..0x546c18: gira 12 al revés.
    particle.rotation = static_cast<float>(static_cast<double>(particle.rotation) - ShinyTurn);
    // 0x546c1b..0x546c49: el fst guarda la z de la velocidad en float y la x87 retiene la resta para la posición.
    const int fall = random.Next() & FallingShineFallMask;
    const double vertical = static_cast<double>(particle.velocity.z) - (static_cast<double>(fall) * FallingShineFall);
    particle.velocity.z = static_cast<float>(vertical);
    // 0x546c4c..0x546c5f: vuelve a sumar la velocidad, sin girar (el movimiento común ya sumó la girada).
    particle.position.x =
        static_cast<float>(static_cast<double>(particle.velocity.x) + static_cast<double>(particle.position.x));
    particle.position.y =
        static_cast<float>(static_cast<double>(particle.velocity.y) + static_cast<double>(particle.position.y));
    particle.position.z = static_cast<float>(vertical + static_cast<double>(particle.position.z));
    for (int channel = 0; channel < 3; ++channel)
        particle.light[channel] = static_cast<float>(static_cast<double>(particle.light[channel]) * FallingShineFade);
    // 0x546c84..0x546c96: 0x5491e0(0x5ac, posición, escala, luz, 0, 0, 0), la mezcla aditiva y sin dueño.
    const float scale = static_cast<float>(static_cast<double>(particle.scale) * FallingShineSpriteScale);
    if (sprites == nullptr)
        return Core::Unexpected(ParticleError::MissingSprites);
    static_cast<void>(sprites->Create(FallingShineSprite, particle.position, scale, particle.light, 0.0f, 0));
    return {};
}
} // namespace Client::Particles
