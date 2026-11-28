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
// 0x544f7b: the 5c5/7 (the sparks of the selection's butterfly) falls with 0x6b8a20; the /0, with 0x6b8818.
constexpr double SparkFall = 2.0;
constexpr double FlutterFall = static_cast<double>(std::bit_cast<float>(0x3f19999au));
constexpr std::int32_t FlutterSparkSubtype = 7;
// The 5c5/1, the sparks of staff 0xc18 (0x50f5a8), and those outside the table: life (rand() & 7) + 8
// (0x5403eb..0x5403ff) and the /0's step.
constexpr std::int32_t PushSparkSubtype = 2;
constexpr float PushSparkScale = 2.0f;
constexpr double PushSparkSpeed = 3.0; // 0x5404b8/0x5404c4, 0x6b86b8.
// 0x5404cc..0x5404f2: the /4 triples the scale (0x6b86b8) and multiplies the local vector by 10 (0x6b87a8).
constexpr std::int32_t StrongSparkSubtype = 4;
constexpr double StrongSparkScale = 3.0;
constexpr double StrongSparkSpeed = 10.0;
// 0x5403ad..0x5403e9: the /5 reads its owner's position (+0x40 → +0x10) at birth for its start (+0x6c), goes back to
// the requested scale and lives (rand() & 3) + 12, with the rise at 0 and texture 0x5c6. Its step falls 0.3f, with the
// life's light, and follows its owner like the /6 (0x544fe4).
constexpr std::int32_t OwnerBirthSparkSubtype = 5;
constexpr int OwnerBirthLifeMask = 3, OwnerBirthLifeBase = 12;
// 0x5404f9..0x54053c: the /6 lives (rand() & 3) + 4, takes the requested scale, texture 0x5c6, the rise at 0 and the
// local vector (0; (rand() % 20 + 20) · 0.2f (0x6b8854); 0). Its step falls 0.3f (0x6b89dc), its light is the 0.9f
// from edx (0x544807, 0x54481d or 0x544873) and it follows the owner (0x544fef).
constexpr std::int32_t OwnedSparkSubtype = 6;
constexpr std::int32_t OwnedSparkTexture = 0x5c6;
constexpr double OwnedSparkStep = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
constexpr double OwnedSparkFall = static_cast<double>(std::bit_cast<float>(0x3e99999au));
constexpr float OwnedSparkLight = std::bit_cast<float>(0x3f666666u);
constexpr float SparkLifeLight = 0.0625f;
constexpr float ShinyTurn = 12.0f; // 0x6b8864
// 0x542ad3..0x542b06: the 5fc/2 lives 25, turns rand() % 360 and leaves with the speed (−0.1f; −0.5; −1)
// (0xbdcccccd, 0xbf000000, 0xbf800000) that the common movement turns with the angles (the x at 45, 0x542ac6).
constexpr std::int32_t FallingShineSubtype = 2;
constexpr std::int32_t FallingShineLife = 25;
constexpr int FallingShineTurns = 360;
constexpr glm::vec3 FallingShineVelocity{std::bit_cast<float>(0xbdcccccdu), std::bit_cast<float>(0xbf000000u),
                                         std::bit_cast<float>(0xbf800000u)};
// 0x546c1b..0x546c96: the fall by (rand() & 7) · 0.05f (0x6b8a48), the light by 0x6ba264 and the flare01 sprite
// (0x5ac) with the scale by 2/3 (0x6b8c64).
constexpr int FallingShineFallMask = 7;
constexpr double FallingShineFall = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));
constexpr double FallingShineFade = static_cast<double>(std::bit_cast<float>(0x3f73cf3eu));
constexpr double FallingShineSpriteScale = static_cast<double>(std::bit_cast<float>(0x3f2aaaabu));
constexpr std::uint16_t FallingShineSprite = 0x5ac;
} // namespace

// 0x540333..0x54055a: the 5c5 handles any subtype; the /0, /2, /3, /4 and /6 leave with their vector, the /7 with its
// life, the /5 with its owner's position and the others (the /1 and those outside) by the /1's path (0x5403eb).
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
        // 0x542ab9..0x542acd: the subtypes other than the /2 return without rand and keep the prefix's scale.
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
    // 0x540333..0x54055a, subtype 0: the matrix AFTER writing the yaw, without changing pitch/roll.
    particle.scale = static_cast<float>(static_cast<double>((random.Next() & 3) + 4) * SparkScaleStep);
    if (particle.subtype == FlutterSparkSubtype)
    {
        // 0x54038c..0x5403a7: only the life; no turn or speed, and without adding the argument's scale.
        constexpr int FlutterLifeSpan = 12;
        particle.life = random.Next() % FlutterLifeSpan + FlutterLifeSpan;
        return {};
    }
    const std::int32_t subtype = particle.subtype;
    if (subtype == OwnerBirthSparkSubtype)
    {
        // 0x5403ad..0x5403e9: the start, the requested scale (it overrides the one above), the life, the rise and the
        // texture.
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
        // 0x5403eb..0x540406: only the life, no turn or speed; the sum of the argument's scale belongs to the /7.
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
        // 0x5404ad..0x5404ca: after the five rolls and the matrix, before turning the local vector.
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
        // 0x546ca3..0x546ccc: fst of the scale without pop; the /1 multiplies the RETAINED sine by 0.75.
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
    // 0x544f29..0x544fe9: light, Z and speed before querying the terrain; the common movement already ran.
    const float light = static_cast<float>(static_cast<double>(particle.life) * SparkLifeLight);
    particle.light = glm::vec3(light);
    particle.position.z = static_cast<float>(static_cast<double>(particle.position.z) + particle.verticalSpeed);
    // 0x544f4a and 0x544f5a: the /5 and the /6 fall 0.3f; only the /6 overrides the light.
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
    // 0x544fbd..0x545040: the y and the z stored in a float (0x544fd2, 0x544fde) and the x retained; then they subtract
    // what the owner (+0x40) moved since the start (+0x6c), which becomes its position. It does not check whether the
    // owner is alive.
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
    // 0x546c0f..0x546c18: turns 12 backward.
    particle.rotation = static_cast<float>(static_cast<double>(particle.rotation) - ShinyTurn);
    // 0x546c1b..0x546c49: the fst stores the speed's z in a float and the x87 retains the subtraction for the position.
    const int fall = random.Next() & FallingShineFallMask;
    const double vertical = static_cast<double>(particle.velocity.z) - (static_cast<double>(fall) * FallingShineFall);
    particle.velocity.z = static_cast<float>(vertical);
    // 0x546c4c..0x546c5f: adds the speed again, unrotated (the common movement already added the rotated one).
    particle.position.x =
        static_cast<float>(static_cast<double>(particle.velocity.x) + static_cast<double>(particle.position.x));
    particle.position.y =
        static_cast<float>(static_cast<double>(particle.velocity.y) + static_cast<double>(particle.position.y));
    particle.position.z = static_cast<float>(vertical + static_cast<double>(particle.position.z));
    for (int channel = 0; channel < 3; ++channel)
        particle.light[channel] = static_cast<float>(static_cast<double>(particle.light[channel]) * FallingShineFade);
    // 0x546c84..0x546c96: 0x5491e0(0x5ac, position, scale, light, 0, 0, 0), additive blend and no owner.
    const float scale = static_cast<float>(static_cast<double>(particle.scale) * FallingShineSpriteScale);
    if (sprites == nullptr)
        return Core::Unexpected(ParticleError::MissingSprites);
    static_cast<void>(sprites->Create(FallingShineSprite, particle.position, scale, particle.light, 0.0f, 0));
    return {};
}
} // namespace Client::Particles
