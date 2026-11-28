#include "Effects/WizardModelEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Kernel/ModelEffectArguments.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Models/Animation.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Models/Skeleton.h"
#include "Math/Ftol.h"
#include "Effects/GameModelEffects.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"
#include "Particles/ParticleOwner.h"
#include "Particles/Particles.h"
#include "Particles/PortableParticleTrigonometry.h"
#include "Particles/SmokeParticleKernels.h"
#include "Particles/Sprites.h"
#include "Terrain/TerrainAttributeQuery.h"
#include "Terrain/DynamicWorldLight.h"
#include "Tables/PlayerModels.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Effects
{
namespace
{
using Crt::CrtRandom;

using Math::D;
using Math::F32;

constexpr double Bits(std::uint32_t bits)
{
    return static_cast<double>(std::bit_cast<float>(bits));
}

// The .rdata constants the creations and steps read, as x87 doubles (PC53).
constexpr double Tenth = Math::TenthAsDouble;             // 0x6b87b8: 0.1f.
constexpr double NegativeTenth = Bits(0xbdcccccdu);       // 0x6b8930: −0.1f.
constexpr double Hundredth = Math::HundredthAsDouble;     // 0x6b86c0 and 0x6b88d4: 0.01f.
constexpr double NegativeHundredth = Bits(0xbc23d70au);   // 0x6b8a58: −0.01f.
constexpr double Fifth = Math::FifthAsDouble;             // 0x6b8854 and 0x6b8cfc: 0.2f.
constexpr double NegativeFifth = Bits(0xbe4ccccdu);       // 0x6b8d3c: −0.2f.
constexpr double ThreeTenths = Bits(0x3e99999au);         // 0x6b89dc: 0.3f.
constexpr double NegativeThreeTenths = Bits(0xbe99999au); // 0x6ba274: −0.3f.
constexpr double NegativeFourTenths = Bits(0xbecccccdu);  // 0x6ba18c: −0.4f.
constexpr double SixTenths = Bits(0x3f19999au);           // 0x6b8a20: 0.6f.
constexpr double EightTenths = Bits(0x3f4ccccdu);         // 0x6b8a38: 0.8f.
constexpr double NineTenths = Bits(0x3f666666u);          // 0x6b8a1c: 0.9f.
constexpr double Twentieth = Math::TwentiethAsDouble;     // 0x6b8a48: 0.05f.
constexpr double ElevenTenths = Bits(0x3f8ccccdu);        // 0x6b8830: 1.1f.
constexpr double DustStep = Bits(0x3c75c28fu);            // 0x6b8a24: 0.015f.
constexpr double SmokeStep = Bits(0x3ccccccdu);           // 0x6b8bd0: 0.025f.
constexpr double MeteorScaleStep = Bits(0x3e051eb8u);     // 0x6ba1f0: 0.13f.

// The scales the creations write by their bits.
constexpr float EightTenthsScale = std::bit_cast<float>(0x3f4ccccdu); // 0x51abf4 and 0x51ad1d.
constexpr float LaserScale = std::bit_cast<float>(0x3fa66666u);       // 0x51a56d: 1.3f.
constexpr float SnowScale = std::bit_cast<float>(0x3f99999au);        // 0x51ada9: 1.2f.
constexpr float HellGateScale = std::bit_cast<float>(0x3f333333u);    // 0x51b90d: 0.7f.
constexpr float LightningScale = std::bit_cast<float>(0x3e99999au);   // 0x51aea2: 0.3f.
constexpr glm::vec3 IceCallerLight{std::bit_cast<float>(0x3ecccccdu), std::bit_cast<float>(0x3e99999au),
                                   std::bit_cast<float>(0x3e4ccccdu)}; // 0x51acd2: (0.4f, 0.3f, 0.2f).

// The children: particles (0x53fe70), joints (0x530860), sprites (0x5491e0) and effects (0x518cf0).
constexpr std::int32_t ShinyParticleType = 0x5fc;
constexpr std::int32_t ColumnParticle = 0x62b;
constexpr std::int32_t MeteorEmberParticle = 0x6ad;
constexpr std::int32_t SnowBlastJoint = 0x613;
constexpr std::int32_t StormJoint = 0x614;
constexpr std::int32_t OrbitJoint = 0x60f;
constexpr std::int32_t LaserJoint = 0x61c;
constexpr std::int32_t ColumnJoint = 0x62b;
constexpr std::int32_t FireTrailJoint = 0x5f2;
constexpr std::uint16_t FlareSprite = 0x5ac;
constexpr std::uint16_t ShineSprite = 0x5fd;
constexpr std::int16_t ChipEffect = 0xc8;  // 0xc8 + (rand() & 1): the pebbles.
constexpr std::int16_t MeteorBurst = 0xf6; // 0x524d05: the switch batch's 0xf6/2.
// +0x2 of the player character.
constexpr std::uint16_t PlayerType = Tables::PlayerSkeletonModel;
constexpr std::uint16_t NoKey = 0xffff;
constexpr std::int32_t NoTarget = -1;
constexpr std::int32_t StoneChipFallMask = 15;

Core::Expected<void, std::string> Emit(const WizardEffectServices& services, CrtRandom& random,
                                       std::string_view address, std::int32_t type, const glm::vec3& position,
                                       const glm::vec3& angles, const glm::vec3& light, std::int32_t subtype,
                                       float scale)
{
    if (services.particles == nullptr)
        return Core::Unexpected(std::format("{}: falta la tabla de partículas (0x53fe70) (CL11)", address));
    const auto created =
        services.particles->CreateChecked(type, position, angles, light, subtype, scale, services.worldClock, random);
    if (!created.HasValue())
        return Core::Unexpected(
            std::format("{}: la partícula {:#x}/{} no está portada (CL11)", address, type, subtype));
    return {};
}

// 0x53fe70 with the mist 0x5f2/28..30, which asks for the terrain height (0x5f93c0) when born and, the /30, the clock's
// sine (0x5767b88).
Core::Expected<void, std::string> EmitOnGround(const WizardEffectServices& services, CrtRandom& random,
                                               std::string_view address, std::int32_t type, const glm::vec3& position,
                                               const glm::vec3& angles, const glm::vec3& light, std::int32_t subtype,
                                               float scale)
{
    if (services.particles == nullptr)
        return Core::Unexpected(std::format("{}: falta la tabla de partículas (0x53fe70) (CL11)", address));
    Particles::PortableParticleTrigonometry trigonometry;
    const auto created = services.particles->CreateOnGroundChecked(type, position, angles, light, subtype, scale,
                                                                   std::cref(services.worldClock), random, trigonometry,
                                                                   services.terrainHeight);
    if (!created.HasValue())
        return Core::Unexpected(std::format("{}: la partícula {:#x}/{} falla con el error {} (0x5f93c0, el reloj)",
                                            address, type, subtype, static_cast<unsigned>(created.Error())));
    return {};
}

// 0x53fe70 with the effect as owner (+0x40): the trail 0x5f2/0x24 follows it the first two steps. The owner is its slot
// in the model effects' table, as ParticleOwnerStatesOf resolves it.
Core::Expected<void, std::string> EmitOwned(const WizardEffectServices& services, CrtRandom& random,
                                            std::string_view address, const ModelEffect& effect, std::int32_t type,
                                            const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                                            std::int32_t subtype, float scale)
{
    if (services.particles == nullptr)
        return Core::Unexpected(std::format("{}: falta la tabla de partículas (0x53fe70) (CL11)", address));
    if (services.effects == nullptr)
        return Core::Unexpected(std::format("{}: falta la tabla de los efectos dueños (CL11)", address));
    const std::span<const ModelEffect> pool = std::as_const(*services.effects).Effects();
    const auto found = std::ranges::find_if(pool, [&effect](const ModelEffect& slot) { return &slot == &effect; });
    if (found == pool.end())
        return Core::Unexpected(std::format("{}: el efecto dueño no está en la tabla (CL11)", address));
    const Particles::ParticleOwner owner{Particles::ParticleOwnerKind::ModelEffect,
                                         static_cast<std::uint64_t>(found - pool.begin())};
    const auto created = services.particles->CreateOwnedChecked(type, position, angles, light, subtype, scale,
                                                                services.worldClock, random, 0, owner);
    if (!created.HasValue())
        return Core::Unexpected(
            std::format("{}: la partícula {:#x}/{} no está portada (CL11)", address, type, subtype));
    return {};
}

struct JointArguments
{
    std::int32_t type = 0;
    glm::vec3 first{};
    glm::vec3 second{};
    glm::vec3 angles{};
    std::int32_t subtype = 0;
    void* owner = nullptr;
    float scale = 0.0f;
    std::int32_t key = -1;
    std::int32_t target = NoTarget;
};

Core::Expected<void, std::string> Joint(const WizardEffectServices& services, std::string_view address,
                                        JointArguments arguments)
{
    if (!services.joint)
        return Core::Unexpected(std::format("{}: falta el alta de joints (0x530860) (CL11)", address));
    const Kernel::JointRequest request{.type = arguments.type,
                                       .firstPosition = arguments.first,
                                       .secondPosition = arguments.second,
                                       .angles = arguments.angles,
                                       .subtype = arguments.subtype,
                                       .ownerContext = arguments.owner,
                                       .scale = arguments.scale,
                                       .key = arguments.key,
                                       .skill = 0,
                                       .skillSerial = 0,
                                       .target = arguments.target};
    if (auto created = services.joint(request); !created.HasValue())
        return Core::Unexpected(std::format("{}: el joint {:#x}/{} ({}) (CL11)", address, arguments.type,
                                            arguments.subtype, created.Error()));
    return {};
}

Core::Expected<void, std::string> Sprite(const WizardEffectServices& services, std::string_view address,
                                         std::uint16_t texture, const glm::vec3& position, float scale,
                                         const glm::vec3& light, float rotation, std::int32_t blend)
{
    if (services.sprites == nullptr)
        return Core::Unexpected(std::format("{}: falta la tabla de sprites (0x5491e0)", address));
    static_cast<void>(services.sprites->Create(texture, position, scale, light, rotation, blend));
    return {};
}

Core::Expected<void, std::string> Light(const WizardEffectServices& services, const glm::vec3& position,
                                        const glm::vec3& color, std::int32_t radius, std::string_view address)
{
    return Terrain::AddDynamicLight(services.lighting, position, color, radius, address);
}

Core::Expected<double, std::string> Height(const WizardEffectServices& services, float x, float y,
                                           std::string_view address)
{
    if (!services.terrainHeight)
        return Core::Unexpected(std::format("{}: falta la altura del terreno (0x5f93c0)", address));
    return services.terrainHeight(x, y);
}

// 0x518cf0(0xc8 + (rand() & 1), point, angles, light, subtype, 0, −1, 0, 0, 0, 0): the arguments are pushed before the
// type's rand(). MuRet only has the pebbles with subtype 0 and 1.
Core::Expected<void, std::string> Chip(const WizardEffectServices& services, CrtRandom& random,
                                       std::string_view address, const glm::vec3& position, const glm::vec3& angles,
                                       const glm::vec3& light, std::int32_t subtype)
{
    if (services.effects == nullptr)
        return Core::Unexpected(std::format("{}: falta la tabla de efectos (0x518cf0)", address));
    const auto type = static_cast<std::int16_t>(ChipEffect + (random.Next() & 1));
    if (subtype != 0 && subtype != RisingStoneChipSubtype && subtype != SpinningStoneChipSubtype)
        return Core::Unexpected(
            std::format("{}: la piedrita {:#x} con el subtipo {} no está portada (CL11)", address, type, subtype));
    static_cast<void>(services.effects->CreateStoneChip(type, position, angles, light, random, subtype));
    return {};
}

Core::Expected<WizardOwnerView, std::string> OwnerOf(const ModelEffect& effect, const WizardEffectServices& services,
                                                     std::string_view address)
{
    if (effect.ownerContext == nullptr)
        return Core::Unexpected(std::format("{}: el efecto {:#x}/{} lee su dueño (+0x108) y no tiene", address,
                                            effect.type, effect.subtype));
    if (!services.ownerOf)
        return Core::Unexpected(std::format("{}: falta quién lea el dueño (+0x108)", address));
    const auto owner = services.ownerOf(effect.ownerContext);
    if (!owner)
        return Core::Unexpected(std::format("{}: el dueño del efecto {:#x}/{} no es un personaje (CL11)", address,
                                            effect.type, effect.subtype));
    return *owner;
}

// 0x4e79a0, +0xf04 to +0x8c as a WORD, +0xf04 plus 1 and 0x40f370: the own record opened and closed.
Core::Expected<void, std::string> CountInTheOwnRecord(ModelEffect& effect, CrtRandom& random,
                                                      const WizardEffectServices& services, std::string_view address)
{
    if (services.ownRecordDepth == nullptr || services.ownRecordCount == nullptr)
        return Core::Unexpected(std::format("{}: falta el registro propio (0x7681eb4)", address));
    const Models::LogicalAccess access(*services.ownRecordDepth, random, Models::OwnRecordCloseRandoms);
    effect.word8c = *services.ownRecordCount;
    ++*services.ownRecordCount;
    return {};
}

void AddTo(glm::vec3& position, const glm::vec3& offset)
{
    for (int axis = 0; axis < 3; ++axis)
        position[axis] = F32(D(offset[axis]) + D(position[axis]));
}

glm::vec3 Turned(const glm::vec3& vector, const glm::vec3& angles)
{
    return Math::OriginalVectorRotate(vector, Math::OriginalAngleMatrix(angles));
}

// (rand() & 63) − 32 in x and y and (rand() & 127) + 32 in z, each added to its axis on the x87 (0x51ac38 and
// 0x5246bc).
glm::vec3 ScatteredAround(const glm::vec3& position, CrtRandom& random)
{
    glm::vec3 point{};
    point.x = F32(D(static_cast<float>((random.Next() & 63) - 32)) + D(position.x));
    point.y = F32(D(static_cast<float>((random.Next() & 63) - 32)) + D(position.y));
    point.z = F32(D(static_cast<float>((random.Next() & 127) + 32)) + D(position.z));
    return point;
}

double LifeTimes(std::int32_t life, double factor)
{
    return static_cast<double>(life) * factor;
}

// 0x51b426: the pebbles' case, which the 0xd8 and the 0xd9 fall into (and the 0xd5 and the 0xd6 with a subtype other
// than 5).
Core::Expected<void, std::string> CreateChipCase(ModelEffect& effect, CrtRandom& random)
{
    constexpr std::int32_t FollowingSubtype = 5, FollowingLife = 60;
    if (effect.subtype == FollowingSubtype)
    {
        // 0x51b5dc: life 60 and the scale; only the 0xd5 and the 0xd6 follow the owner (0x51b3b6).
        effect.life = FollowingLife;
        effect.transform.scale = F32(static_cast<double>((random.Next() & 3) + 8) * Tenth);
        return {};
    }
    constexpr int PushMask = 255, PushBase = 64, LifeMask = 15, LifeBase = 32, ScaleMask = 3, FallMask = 15;
    constexpr int FullTurn = Math::FullTurn;
    // 0x51b432..0x51b4ff (the /10) and 0x51b509..0x51b5d6 (the /0xc): the push ((& 255) + 64) in y, the life
    // (& 15) + 32, the scale ((& 3) + 15), the yaw rand() % 360, the speed (the rotated push) and the fall; the /10
    // with the push times 0.2 (double, 0x6ba1e8), the scale times 0.05f and the fall (& 15) + 28; the /0xc with both
    // times 0.1f and the fall (& 15) + 8.
    if (effect.subtype == GroundStoneChipSubtype || effect.subtype == 0xc)
    {
        const bool ground = effect.subtype == GroundStoneChipSubtype;
        constexpr double GroundPush = std::bit_cast<double>(0x3fc999999999999aull); // 0x6ba1e8: 0.2
        constexpr int GroundScaleBase = 15, GroundFallBase = 28, FallBase = 8;
        const float push =
            F32(static_cast<double>((random.Next() & PushMask) + PushBase) * (ground ? GroundPush : Tenth));
        effect.life = (random.Next() & LifeMask) + LifeBase;
        effect.transform.scale =
            F32(static_cast<double>((random.Next() & ScaleMask) + GroundScaleBase) * (ground ? Twentieth : Tenth));
        effect.transform.angles.z = static_cast<float>(random.Next() % FullTurn);
        effect.velocity = Turned(glm::vec3(0.0f, push, 0.0f), effect.transform.angles);
        effect.turnDirection = static_cast<float>((random.Next() & FallMask) + (ground ? GroundFallBase : FallBase));
        return {};
    }
    // 0x51b612..0x51b658: the /0xb, life 40, the scale ((& 3) + 8) · 0.8f and the yaw; no speed or fall.
    if (effect.subtype == 0xb)
    {
        constexpr std::int32_t FadingLife = 40;
        constexpr int FadingScaleBase = 8;
        effect.life = FadingLife;
        effect.transform.scale = F32(static_cast<double>((random.Next() & ScaleMask) + FadingScaleBase) * EightTenths);
        effect.transform.angles.z = static_cast<float>(random.Next() % FullTurn);
        return {};
    }
    // 0x51b72b..0x51b805: the push (except the /1), the life, the scale, the yaw, the rotated speed and the fall.
    const float push =
        effect.subtype == RisingStoneChipSubtype ? 0.0f : F32(static_cast<double>((random.Next() & 255) + 64) * Tenth);
    effect.life = (random.Next() & 15) + 32;
    effect.transform.scale = F32(static_cast<double>((random.Next() & 3) + 8) * Tenth);
    effect.transform.angles.z = static_cast<float>(random.Next() % 360);
    effect.velocity = Turned(glm::vec3(0.0f, push, 0.0f), effect.transform.angles);
    effect.turnDirection = static_cast<float>((random.Next() & StoneChipFallMask) + 8);
    return {};
}

Core::Expected<void, std::string> CreateFire(ModelEffect& effect, const Kernel::ModelEffectArguments& arguments,
                                             CrtRandom& random, const WizardEffectServices& services)
{
    constexpr std::int32_t FireLife = 40;
    constexpr double Ahead = 130.0, Above = 400.0; // 0x6ba194 and 0x6b87b0.
    glm::vec3& at = effect.transform.position;
    effect.objectState.blendMesh = 1; // 0x51adb5
    const auto scaleFrom = [&random](int mask, int base)
    { return F32(static_cast<double>((random.Next() & mask) + base) * Tenth); };
    const auto keyedFall = [&]()
    {
        // 0x51afab..0x51afeb: hidden, the scale, +0xd8 with the signed key and +0x8a at −1.
        effect.life = FireLife;
        effect.objectState.hiddenMesh = -2;
        effect.transform.scale = scaleFrom(7, 15);
        effect.speed = static_cast<float>(static_cast<std::int16_t>(arguments.key));
        effect.disturbanceState = NoKey;
    };
    switch (effect.subtype)
    {
    case 0: // 0x51adc7
    case 4: // 0x51aeb7
    {
        effect.life = FireLife;
        effect.transform.scale =
            effect.subtype == 0 ? scaleFrom(7, 10) : F32(static_cast<double>(random.Next() % 10 + 15) * Tenth);
        at.x = F32((static_cast<double>(random.Next() & 31) + D(at.x)) + Ahead);
        at.z = F32(D(at.z) + Above);
        effect.velocity.x = 0.0f;
        if (effect.subtype == 0)
        {
            effect.velocity.y = 0.0f;
            effect.velocity.z = -50.0f;
        }
        else
        {
            effect.velocity.y = F32(-(static_cast<double>(random.Next() % 20) + 10.0));
            effect.velocity.z = F32(-(static_cast<double>(random.Next() % 10) + 20.0));
        }
        effect.transform.angles = {0.0f, 20.0f, 0.0f};
        return {};
    }
    case 2: // 0x51ae4f
        effect.life = FireLife;
        effect.transform.scale = scaleFrom(7, 10);
        effect.velocity = {0.0f, 0.0f, -50.0f};
        return {};
    case 3: // 0x51ae9b → 0x51a9b1: the 3's ray (Fauna::StepFaunaLightning does its step).
        effect.life = 80;
        effect.transform.scale = LightningScale;
        effect.velocity = {0.0f, -12.0f, 0.0f};
        return {};
    case 5: // 0x51af6d
        effect.life = FireLife;
        effect.turnDirection = 5.0f;
        effect.velocity = {0.0f, -30.0f, 0.0f};
        return {};
    case 6: // 0x51afa4
    case 7: // 0x51b0b0
    {
        keyedFall();
        at.x = F32(static_cast<double>(random.Next() % 100 + 200) + D(at.x));
        at.y = F32(static_cast<double>(random.Next() % 100 - 50) + D(at.y));
        at.z = F32(static_cast<double>(random.Next() % 300 + 500) + D(at.z));
        effect.velocity.x = 0.0f;
        effect.velocity.y = 0.0f;
        effect.velocity.z = F32(-50.0 - static_cast<double>(random.Next() % 50));
        effect.copiedAngles = {0.0f, 20.0f, 0.0f};
        if (effect.subtype == 7)
            return {};
        // 0x51b098: 0x530860(0x5f2, position, position, +0x28, 0, effect, 100, −1, 0, 0, −1).
        return Joint(services, "0x51b098",
                     {.type = FireTrailJoint,
                      .first = at,
                      .second = at,
                      .angles = effect.copiedAngles,
                      .subtype = 0,
                      .owner = &effect,
                      .scale = 100.0f,
                      .key = -1});
    }
    case 8: // 0x51b19c
    {
        keyedFall();
        at.z = F32(static_cast<double>(random.Next() % 300 + 500) + D(at.z));
        effect.velocity.x = 0.0f;
        effect.velocity.y = 0.0f;
        effect.velocity.z = F32(-50.0 - static_cast<double>(random.Next() % 50));
        effect.copiedAngles.x = 0.0f;
        effect.copiedAngles.y = static_cast<float>(random.Next() % 5 + 5);
        effect.copiedAngles.z = 0.0f;
        return Joint(services, "0x51b268",
                     {.type = FireTrailJoint,
                      .first = at,
                      .second = at,
                      .angles = effect.copiedAngles,
                      .subtype = 0,
                      .owner = &effect,
                      .scale = 60.0f,
                      .key = -1});
    }
    case 9: // 0x51b280
        effect.objectState.light = 0.0f;
        effect.life = 600;
        effect.transform.scale = F32(static_cast<double>(random.Next() % 10 + 20) * MeteorScaleStep);
        at.x = F32((static_cast<double>(random.Next() & 31) + D(at.x)) + Ahead);
        at.z = F32((static_cast<double>(random.Next() & 31) + D(at.z)) + Above);
        effect.velocity.x = 0.0f;
        effect.velocity.y = F32(static_cast<double>((-random.Next()) % 5) - 8.0);
        effect.velocity.z = F32(-10.0 - static_cast<double>(random.Next() % 10));
        effect.transform.angles = {};
        return {};
    default: // 0x51b345: the 1 and from 10 on.
        effect.life = 60;
        effect.transform.scale = scaleFrom(3, 8);
        at.z = F32(D(at.z) + 120.0);
        effect.velocity = {0.0f, -50.0f, 0.0f};
        return {};
    }
}

Core::Expected<void, std::string> CreateCase(ModelEffect& effect, const Kernel::ModelEffectArguments& arguments,
                                             CrtRandom& random, const WizardEffectServices& services,
                                             glm::vec3* callerLight)
{
    glm::vec3& at = effect.transform.position;
    switch (effect.type)
    {
    case ShadowRainEffect: // 0x51bc43
    {
        if (effect.subtype != 0)
            return {};
        effect.life = 50;
        for (int index = -5; index < 5; ++index)
        {
            const double offset = static_cast<double>(index) * 12.0; // 0x6b8864
            const glm::vec3 target{F32((offset + D(at.x)) - 30.0), F32((offset + D(at.y)) + 30.0),
                                   F32(D(at.z) - 300.0)};
            if (auto joint = Joint(services, "0x51bcb0",
                                   {.type = OrbitJoint,
                                    .first = target,
                                    .second = at,
                                    .angles = effect.transform.angles,
                                    .subtype = 0x10,
                                    .owner = effect.ownerContext,
                                    .scale = 120.0f,
                                    .key = -1});
                !joint.HasValue())
                return joint;
        }
        return {};
    }
    case IceEffect: // 0x51abd0
    {
        if (effect.subtype == 0)
        {
            effect.life = 50;
            effect.transform.scale = EightTenthsScale;
            effect.speed = 1.0f;
            effect.transform.angles.x = 0.0f;
            effect.objectState.blendMesh = 0;
            return {};
        }
        if (effect.subtype < 0 || effect.subtype > 2)
            return {};
        effect.life = 20;
        effect.transform.scale = EightTenthsScale;
        effect.transform.angles.x = -20.0f;
        effect.objectState.blendMesh = 0;
        effect.objectState.light = 0.5f;
        effect.turnDirection = 5.0f;
        effect.copiedAngles.x = 0.0f;
        effect.copiedAngles.y = 0.0f;
        effect.copiedAngles.z = static_cast<float>(random.Next() % 360);
        for (int puff = 0; puff < 3; ++puff)
        {
            const glm::vec3 point = ScatteredAround(at, random);
            if (auto smoke = Emit(services, random, "0x51acb0", Particles::BreathParticle, point,
                                  effect.transform.angles, effect.light, 0, 1.0f);
                !smoke.HasValue())
                return smoke;
        }
        // 0x51acc5..0x51acec: the caller's light (the fourth argument) becomes (0.4f, 0.3f, 0.2f) and turns on the
        // dynamic one.
        glm::vec3 own = IceCallerLight;
        glm::vec3& light = callerLight != nullptr ? *callerLight : own;
        light = IceCallerLight;
        if (auto lit = Light(services, at, light, 2, "0x51acec"); !lit.HasValue())
            return lit;
        effect.start = at;
        return {};
    }
    case FireEffect:
        return CreateFire(effect, arguments, random, services);
    case PoisonEffect: // 0x51b94c
        effect.objectState.blendMesh = 1;
        effect.life = 40;
        effect.transform.scale = 1.0f;
        return {};
    case HellGateEffect: // 0x51b901
        effect.life = 50;
        effect.transform.scale = HellGateScale;
        effect.objectState.blendMesh = static_cast<std::int16_t>(arguments.subtype == 0 ? -3 : -4);
        effect.transform.angles = {0.0f, 0.0f, 45.0f};
        return {};
    case PlainSkillEffect: // 0x51abbf
        effect.life = 10;
        effect.objectState.blendMesh = 0;
        return {};
    case InnerCircleEffect: // 0x51aa61
        effect.life = 45;
        effect.objectState.blendMesh = 0;
        switch (effect.subtype)
        {
        case 0:
        {
            if (services.hero == nullptr || effect.ownerContext != services.hero)
                return {};
            if (auto counted = CountInTheOwnRecord(effect, random, services, "0x51aa97"); !counted.HasValue())
                return counted;
            if (!services.skillHits)
                return Core::Unexpected(std::string("0x51aae9: el 0xcb/0 del héroe pide 0x517a20 (Scenes/SkillHits)"));
            return services.skillHits(effect, 300.0f);
        }
        case 1:
            effect.transform.scale = 1.0f;
            effect.objectState.hiddenMesh = -2;
            return {};
        case 2:
            effect.life = 250;
            effect.transform.scale = 1.0f;
            return {};
        case 3:
            effect.life = 30;
            effect.transform.scale = 1.0f;
            return {};
        default:
            return {};
        }
    case OuterCircleEffect: // 0x51ab4d
        effect.life = 40;
        effect.objectState.blendMesh = 0;
        if (arguments.subtype == 1)
            effect.renderFlags = CrackRenderFlags;
        if (effect.subtype == 2)
            effect.transform.scale = 1.0f;
        else if (effect.subtype == 3)
        {
            effect.life = 250;
            effect.transform.scale = 1.0f;
        }
        else if (effect.subtype == 4)
        {
            effect.life = 20;
            effect.transform.scale = 1.0f;
        }
        return {};
    case FollowingMagicEffect: // 0x51a614
        effect.life = 20;
        effect.objectState.blendMesh = 0;
        return {};
    case MagicEffect: // 0x51a85d
        effect.objectState.blendMesh = 0;
        effect.life = 20;
        effect.velocity = {0.0f, -60.0f, 0.0f};
        if (effect.subtype != 2)
            return {};
        return CountInTheOwnRecord(effect, random, services, "0x51a8e4");
    case StormEffect: // 0x51a892, tabla 0x51fbc8
        switch (effect.subtype)
        {
        case 0:
        {
            effect.life = 59;
            effect.objectState.blendMesh = 0;
            effect.velocity = {0.0f, -10.0f, 0.0f};
            const auto height = Height(services, at.x, at.y, "0x51a8cd");
            if (!height.HasValue())
                return Core::Unexpected(height.Error());
            at.z = F32(height.Value());
            return CountInTheOwnRecord(effect, random, services, "0x51a8e4");
        }
        case 1:
            effect.life = 30;
            effect.objectState.hiddenMesh = -2;
            effect.transform.scale = 1.0f;
            effect.objectState.blendMesh = 0;
            effect.velocity = {};
            return {};
        case 2:
            effect.life = 59;
            effect.objectState.hiddenMesh = -2;
            effect.transform.scale = 1.0f;
            effect.objectState.blendMesh = 0;
            effect.velocity = {0.0f, -10.0f, 0.0f};
            effect.light = glm::vec3(1.0f);
            return {};
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        {
            // 0x51a994..0x51aa54: the five small whirls, with their speed.
            constexpr glm::vec3 Velocities[] = {{0.0f, -12.0f, 0.0f},
                                                {7.0f, -6.0f, 0.0f},
                                                {-7.0f, -6.0f, 0.0f},
                                                {4.0f, 5.0f, 0.0f},
                                                {-4.0f, 5.0f, 0.0f}};
            effect.life = 60;
            effect.objectState.hiddenMesh = -2;
            effect.transform.scale = 1.0f;
            effect.velocity = Velocities[effect.subtype - 3];
            return {};
        }
        default:
            return {};
        }
    case LaserEffect: // 0x51a541
        if (effect.subtype == 0 || effect.subtype == 3)
        {
            effect.life = 1;
            effect.objectState.blendMesh = 0;
            effect.objectState.light = effect.light.x;
            effect.transform.scale = LaserScale;
            effect.renderFlags = CrackRenderFlags;
            return {};
        }
        effect.life = 30;
        effect.speed = 1.0f;
        effect.objectState.blendMesh = 0;
        effect.objectState.light = 1.0f;
        effect.transform.scale = LaserScale;
        effect.renderFlags = CrackRenderFlags;
        at.z = F32(D(at.z) + 150.0); // 0x6b87ac
        effect.velocity = {0.0f, -1.0f, 0.0f};
        effect.light = {1.0f, 0.0f, 0.0f};
        effect.transform.angles = {30.0f, 0.0f, arguments.angles.z};
        return Joint(services, "0x51a5dd",
                     {.type = LaserJoint,
                      .first = at,
                      .second = at,
                      .angles = effect.transform.angles,
                      .subtype = 1,
                      .owner = &effect,
                      .scale = 180.0f,
                      .key = -1});
    case SawEffect: // 0x51a4ee
        effect.life = 10;
        at.z = F32(D(at.z) + 130.0); // 0x6ba194
        effect.velocity = Turned(glm::vec3(0.0f, -60.0f, 0.0f), effect.transform.angles);
        return {};
    case FirstBoneEffect: // 0x51b398: 50 more and falls into the 0xd6.
    case SecondBoneEffect:
    {
        if (effect.type == FirstBoneEffect)
            at.z = F32(D(at.z) + 50.0);
        at.z = F32(D(at.z) + 100.0); // 0x51b3a4
        if (effect.subtype != 5)
            return CreateChipCase(effect, random);
        // 0x51b3b6..0x51b41e: life 60, the scale, the start at the position and the position at the owner's.
        effect.life = 60;
        effect.transform.scale = F32(static_cast<double>((random.Next() & 3) + 8) * Tenth);
        effect.start = at;
        const auto owner = OwnerOf(effect, services, "0x51b403");
        if (!owner.HasValue())
            return Core::Unexpected(owner.Error());
        at = owner.Value().position;
        return {};
    }
    case FirstSnowStoneEffect:
    case SecondSnowStoneEffect:
        return CreateChipCase(effect, random);
    case SnowEffect: // 0x51ad3b
        AddTo(at, Turned(glm::vec3(0.0f, -40.0f, 150.0f), effect.transform.angles));
        effect.velocity.y = -40.0f;
        effect.velocity.z = 10.0f;
        effect.life = 20;
        effect.transform.scale = SnowScale;
        return {};
    default:
        return Core::Unexpected(std::format("0x518cf0: el tipo {:#x} no es del lote de los magos", effect.type));
    }
}

// 0x518b60: with the owner alive, seen (+0x16c) and at 100 or less in the plane, life at 1; the 0xc2/1 drops two
// pebbles and the 0xd7 two pebbles 0xd8/0xd9 with their smoke 0x5f2/0 and the sound 0x44.
Core::Expected<void, std::string> HitTheOwner(ModelEffect& effect, CrtRandom& random,
                                              const WizardEffectServices& services)
{
    if (effect.ownerContext == nullptr)
        return {};
    const auto owner = OwnerOf(effect, services, "0x518b7a");
    if (!owner.HasValue())
        return Core::Unexpected(owner.Error());
    if (!owner.Value().alive || !owner.Value().seen)
        return {};
    const double dx = D(effect.transform.position.x) - D(owner.Value().position.x);
    const double dy = D(effect.transform.position.y) - D(owner.Value().position.y);
    if (std::sqrt((dy * dy) + (dx * dx)) > 100.0)
        return {};
    effect.life = 1;
    const glm::vec3 at = effect.transform.position;
    const glm::vec3 angles = effect.transform.angles;
    const glm::vec3 light = effect.light;
    if (effect.type == FireEffect)
    {
        if (effect.subtype != 1)
            return {};
        for (int chip = 0; chip < 2; ++chip)
            if (auto created = Chip(services, random, "0x518cd7", at, angles, light, 0); !created.HasValue())
                return created;
        return {};
    }
    if (effect.type != SnowEffect)
        return {};
    if (services.effects == nullptr)
        return Core::Unexpected(std::string("0x518c5a: falta la tabla de efectos (0x518cf0)"));
    for (int stone = 0; stone < 2; ++stone)
    {
        const Kernel::ModelEffectArguments child{
            .type = static_cast<std::int16_t>(FirstSnowStoneEffect + (random.Next() & 1)),
            .position = at,
            .angles = angles,
            .light = light,
            .subtype = 0,
            .owner = nullptr,
            .key = NoKey};
        if (auto created = CreateWizardModelEffect(*services.effects, child, random, services); !created.HasValue())
            return Core::Unexpected(created.Error());
        if (auto smoke = Emit(services, random, "0x518c70", Particles::BreathParticle, at, angles, light, 0, 1.0f);
            !smoke.HasValue())
            return smoke;
    }
    if (services.sound)
        services.sound(Audio::SoundId{0x44});
    return {};
}

// 0x5238be and 0x523926..0x523b3a: the pebbles 0xd8/0xd9 and the bones 0xd5/0xd6. With subtype 5 they follow the owner
// (the 0xd5 and the 0xd6 die with it); with 0, 10 and 12 they fall and bounce (no fire: their type is not 0xc8/0xc9,
// but the rand() % 10 runs anyway with 0 and 12); with the others they rise rotated.
Core::Expected<void, std::string> StepChip(ModelEffect& effect, CrtRandom& random, const WizardEffectServices& services)
{
    glm::vec3& at = effect.transform.position;
    const bool bone = effect.type == FirstBoneEffect || effect.type == SecondBoneEffect;
    if (effect.subtype == 5)
    {
        const auto owner = OwnerOf(effect, services, bone ? "0x52392b" : "0x5238c5");
        if (!owner.HasValue())
            return Core::Unexpected(owner.Error());
        if (bone && !owner.Value().alive)
            effect.alive = false;
        for (int axis = 0; axis < 3; ++axis)
            at[axis] = F32(D(owner.Value().position[axis]) + D(effect.start[axis]));
        return {};
    }
    if (!bone && effect.subtype == 0xb)
    {
        if (effect.life < 10)
            effect.alpha = F32(D(effect.alpha) * EightTenths);
        return {};
    }
    const bool ground = effect.subtype == 0 || effect.subtype == GroundStoneChipSubtype || effect.subtype == 0xc;
    AddTo(at, ground ? effect.velocity : Turned(effect.velocity, effect.transform.angles));
    for (int axis = 0; axis < 3; ++axis)
        effect.velocity[axis] = F32(D(effect.velocity[axis]) * NineTenths);
    at.z = F32(D(at.z) + D(effect.turnDirection));
    if (!ground)
    {
        if (effect.subtype != 1)
            effect.transform.angles.z = F32(D(effect.transform.angles.z) + 20.0);
        effect.turnDirection = F32(D(effect.turnDirection) + 0.5);
        return {};
    }
    effect.turnDirection = F32(D(effect.turnDirection) - 3.0);
    const auto height = Height(services, at.x, at.y, "0x523a80");
    if (!height.HasValue())
        return Core::Unexpected(height.Error());
    double tilt = D(effect.transform.scale) * 32.0;
    if (height.Value() > D(at.z))
    {
        at.z = F32(height.Value());
        effect.turnDirection = F32(D(effect.turnDirection) * -0.5);
        effect.life -= 4;
        tilt = D(effect.transform.scale) * 128.0;
    }
    effect.transform.angles.x = F32(D(effect.transform.angles.x) - tilt);
    if (effect.subtype == 0 || effect.subtype == 0xc)
        static_cast<void>(random.Next() % 10); // 0x523adb: neither fire nor breath with these types.
    return {};
}

// 0x5244b9..0x5247ac.
Core::Expected<void, std::string> StepIce(ModelEffect& effect, float luminosity, CrtRandom& random,
                                          const WizardEffectServices& services)
{
    glm::vec3& at = effect.transform.position;
    if (effect.subtype == 0)
    {
        // 0x524688: from frame 5, still, sometimes smoke, and it fades by 0.05f.
        if (!(effect.animation.frame < 5.0f))
        {
            effect.speed = 0.0f;
            if ((random.Next() & 1) == 0)
            {
                const glm::vec3 point = ScatteredAround(at, random);
                if (auto smoke = Emit(services, random, "0x524743", Particles::BreathParticle, point,
                                      effect.transform.angles, effect.light, 0, 1.0f);
                    !smoke.HasValue())
                    return smoke;
            }
            const double alpha = D(effect.alpha) - Twentieth;
            effect.alpha = F32(alpha);
            if (alpha < 0.0)
                effect.alive = false;
        }
        const glm::vec3 shade{F32(D(luminosity) * NegativeFourTenths), F32(D(luminosity) * NegativeThreeTenths),
                              F32(D(luminosity) * NegativeFifth)};
        return Light(services, at, shade, 2, "0x5247a4");
    }
    if (effect.subtype < 0 || effect.subtype > 2)
        return {};
    if (effect.ownerContext == nullptr)
    {
        effect.alive = false; // 0x52b5fb
        return {};
    }
    const auto owner = OwnerOf(effect, services, "0x5244e3");
    if (!owner.HasValue())
        return Core::Unexpected(owner.Error());
    const WizardOwnerView& frozen = owner.Value();
    constexpr std::uint32_t FrozenFlag = 0x20; // the owner's +0x78
    if (frozen.alive && (frozen.flags78 & FrozenFlag) == FrozenFlag)
    {
        // 0x5244fb..0x52458d: the player in actions 0xd..0x21 and 0x46..0x47, or the monster in 2, freezes.
        const std::uint8_t action = frozen.action;
        const bool still = frozen.type == PlayerType
                               ? (action >= 0xd && action <= 0x21) || (action >= 0x46 && action <= 0x47)
                               : action == 2;
        if (still)
        {
            effect.animation.frame = 0.0f;
            effect.animation.previousKey = 0;
            effect.objectState.hiddenMesh = -2;
        }
        else
            effect.objectState.hiddenMesh = -1;
        at = frozen.position;
        if (!(effect.animation.frame < 4.0f))
            effect.animation.frame = 4.0f;
        effect.life = 10;
    }
    else
        effect.alpha = F32(D(effect.alpha) - Twentieth);
    if (effect.subtype != 1)
        return {};
    // 0x5245b2..0x52467b: the flame 0x5db/10 that turns around the owner and rises and falls with the clock's sine.
    effect.copiedAngles.z = F32(D(effect.copiedAngles.z) + 20.0);
    const auto sine = Math::Detail::SineProductPC53(D(services.worldClock) * Hundredth, 20.0f);
    if (!sine)
        return Core::Unexpected(std::string("0x5245d5: el seno del 0xc1/1 no cabe en el perfil PC53"));
    effect.turnDirection = F32(*sine + 30.0);
    const glm::vec3 reach = Turned(glm::vec3(60.0f, 0.0f, 0.0f), effect.copiedAngles);
    const glm::vec3 point{F32(D(reach.x) + D(frozen.position.x)), F32(D(reach.y) + D(frozen.position.y)),
                          F32((D(reach.z) + D(frozen.position.z)) + D(effect.turnDirection))};
    return Emit(services, random, "0x52467b", Particles::DeathSpiritFireParticle, point, effect.transform.angles,
                glm::vec3(1.0f), 10, 2.0f);
}

// The cell of 0x5f82a0(ftol(x · 0.01f), ftol(y · 0.01f)), with each one's low byte, and its attribute (0x7d0d348).
Core::Expected<std::uint16_t, std::string> MaskedCellAttribute(const WizardEffectServices& services,
                                                               const glm::vec3& point, std::string_view address)
{
    if (services.terrainAttributes == nullptr)
        return Core::Unexpected(std::format("{}: faltan los atributos de las celdas (0x7d0d348)", address));
    const auto x = static_cast<std::uint32_t>(Math::FtolLow(D(point.x) * Hundredth)) & 0xffu;
    const auto y = static_cast<std::uint32_t>(Math::FtolLow(D(point.y) * Hundredth)) & 0xffu;
    return services.terrainAttributes->cells[(y << 8) + x];
}

constexpr std::uint8_t BlockedCell = 0x8;

// 0x524c64..0x524e95: the 0xc2's fall at `height`: it lands, stays still and explodes.
Core::Expected<void, std::string> LandTheFire(ModelEffect& effect, double height, glm::vec3& local, CrtRandom& random,
                                              const WizardEffectServices& services)
{
    glm::vec3& at = effect.transform.position;
    at.z = F32(height);
    effect.velocity = {};
    glm::vec3 point{at.x, at.y, F32(D(at.z) + 80.0)}; // 0x6b9ffc
    const std::int32_t subtype = effect.subtype;
    if (subtype != 6 && subtype != 7 && subtype != 8)
    {
        for (int chip = 0; chip < 6; ++chip)
            if (auto created = Chip(services, random, "0x524ce3", at, effect.transform.angles, effect.light, 0);
                !created.HasValue())
                return created;
    }
    else
    {
        // 0x524cf3..0x524d38: the 0xf6/2 (Effects/BlastModelEffects, via otherEffect) with the 0xc2 as owner, key
        // 30, the angles at 0 and the light (0, 0.5, 0); it draws no rand().
        const Kernel::ModelEffectArguments burst{.type = MeteorBurst,
                                                 .position = at,
                                                 .angles = {},
                                                 .light = {0.0f, 0.5f, 0.0f},
                                                 .subtype = 2,
                                                 .owner = &effect,
                                                 .key = 30};
        if (!services.otherEffect)
            return Core::Unexpected(std::string("0x524d38: falta el alta del 0xf6/2 que suelta el meteoro (0x518cf0)"));
        if (auto created = services.otherEffect(burst); !created.HasValue())
            return created;
        const int dust = subtype == 7 ? 7 : subtype == 8 ? 5 : 15;
        for (int puff = 0; puff < dust; ++puff)
        {
            point.x = F32(static_cast<double>(random.Next() % 160 - 80) + D(at.x));
            point.y = F32(static_cast<double>(random.Next() % 160 - 100) + D(at.y));
            point.z = F32(D(at.z) + 50.0);
            local = {std::bit_cast<float>(0x3dcccccdu), 0.5f, std::bit_cast<float>(0x3dcccccdu)};
            const float scale = F32(static_cast<double>((random.Next() & 31) + 80) * SmokeStep);
            if (auto smoke = Emit(services, random, "0x524e02", Particles::BreathParticle, point,
                                  effect.transform.angles, local, Particles::StoneDustSubtype, scale);
                !smoke.HasValue())
                return smoke;
        }
        if (subtype == 6)
            for (int chip = 0; chip < 6; ++chip)
                if (auto created = Chip(services, random, "0x524e51", at, effect.transform.angles, local, 0);
                    !created.HasValue())
                    return created;
        if (services.sound)
            services.sound(Audio::SoundId{0x8f});
    }
    if (auto burst = Emit(services, random, "0x524e8a", Particles::ExplosionParticle, point, effect.transform.angles,
                          local, 0, 1.0f);
        !burst.HasValue())
        return burst;
    effect.alive = false;
    return {};
}

// 0x524e99..0x5251b1: the 0xc2's common part (the 3 is Fauna::StepFaunaLightning's).
Core::Expected<void, std::string> FireGlow(ModelEffect& effect, float luminosity, glm::vec3& local, CrtRandom& random,
                                           const WizardEffectServices& services)
{
    glm::vec3& at = effect.transform.position;
    const std::int32_t subtype = effect.subtype;
    const float dim = F32(D(luminosity) * Tenth);
    if (subtype == 5)
    {
        effect.objectState.hiddenMesh = 0;
        effect.objectState.light = 0.0f;
        local = {luminosity, dim, 0.0f};
        if (auto lit = Light(services, at, local, 2, "0x524ed3"); !lit.HasValue())
            return lit;
        if (random.Next() % 5 == 0)
            return {};
        return Emit(services, random, "0x524f05", MeteorEmberParticle, at, effect.transform.angles, local, 0, 1.0f);
    }
    if (subtype == 0 || subtype == 6 || subtype == 7)
        effect.objectState.light = F32(static_cast<double>((random.Next() & 3) + 4) * Tenth);
    else
        effect.objectState.light = 0.0f;
    const bool green = subtype == 6 || subtype == 7 || subtype == 8;
    local = green ? glm::vec3(dim, luminosity, 0.0f) : glm::vec3(luminosity, dim, 0.0f);
    if (auto lit = Light(services, at, local, 2, "0x524fc9"); !lit.HasValue())
        return lit;
    if (subtype == 6 || subtype == 7)
    {
        for (int flame = 0; flame < 2; ++flame)
        {
            const glm::vec3 point{F32((static_cast<double>(random.Next() % 50) + D(at.x)) - 25.0), at.y, at.z};
            if (auto fire = Emit(services, random, "0x525193", Particles::FireParticle, point, effect.transform.angles,
                                 local, 5, 1.0f);
                !fire.HasValue())
                return fire;
        }
    }
    else if (auto fire = Emit(services, random, "0x525001", Particles::FireParticle, at, effect.transform.angles, local,
                              5, 1.0f);
             !fire.HasValue())
        return fire;
    if (subtype == 1)
        return HitTheOwner(effect, random, services);
    return {};
}

// 0x5247b1..0x5251b1.
Core::Expected<void, std::string> StepFire(ModelEffect& effect, float luminosity, glm::vec3& local, CrtRandom& random,
                                           const WizardEffectServices& services)
{
    glm::vec3& at = effect.transform.position;
    const std::int32_t subtype = effect.subtype;
    if (subtype == 3)
        return Core::Unexpected(std::string("0x52500e: el paso del 0xc2/3 lo da Scenes::StepFaunaLightning"));
    if (subtype == 5)
    {
        // 0x5247f1..0x52494b: it bounces against the terrain and, in a cell with the 8 or on its last step, explodes.
        at.z = F32(D(at.z) + D(effect.turnDirection));
        effect.turnDirection = F32(D(effect.turnDirection) - 2.0);
        const auto height = Height(services, at.x, at.y, "0x524819");
        if (!height.HasValue())
            return Core::Unexpected(height.Error());
        const float ground = F32(height.Value());
        if (at.z < ground)
        {
            effect.turnDirection = 10.0f;
            at.z = F32(D(at.z) + 10.0);
            effect.velocity.y = F32(D(effect.velocity.y) * 0.5);
            effect.transform.scale = F32(D(effect.transform.scale) * ElevenTenths);
        }
        bool lands = effect.life == 1;
        if (!lands)
        {
            const auto cell = MaskedCellAttribute(services, at, "0x524894");
            if (!cell.HasValue())
                return Core::Unexpected(cell.Error());
            lands = (static_cast<std::uint8_t>(cell.Value()) & BlockedCell) == BlockedCell;
        }
        if (lands)
        {
            at.z = ground;
            effect.velocity = {};
            const glm::vec3 point{at.x, at.y, F32(D(at.z) + 80.0)};
            if (auto burst = Emit(services, random, "0x5248fa", Particles::ExplosionParticle, point,
                                  effect.transform.angles, local, 0, 1.0f);
                !burst.HasValue())
                return burst;
            for (int chip = 0; chip < 6; ++chip)
                if (auto created = Chip(services, random, "0x524937", at, effect.transform.angles, effect.light, 0);
                    !created.HasValue())
                    return created;
            effect.alive = false;
        }
        return FireGlow(effect, luminosity, local, random, services);
    }
    if (subtype == 9)
    {
        // 0x524959..0x5249f6: two smokes with the speed as light and the effect as owner (push ebx), its own light, and
        // it dies on passing the terrain. The 0x25 keeps the owner but its step (0x545f68) does not read it.
        constexpr std::int32_t FirstTrailSubtype = 0x24, SecondTrailSubtype = 0x25;
        if (auto smoke =
                EmitOwned(services, random, "0x52497d", effect, Particles::BreathParticle, at, effect.transform.angles,
                          effect.velocity, FirstTrailSubtype, F32(D(effect.transform.scale) + 1.0));
            !smoke.HasValue())
            return smoke;
        if (auto smoke = Emit(services, random, "0x5249a0", Particles::BreathParticle, at, effect.transform.angles,
                              effect.velocity, SecondTrailSubtype, F32(D(effect.transform.scale) + 2.0));
            !smoke.HasValue())
            return smoke;
        effect.light = {1.0f, std::bit_cast<float>(0x3e4ccccdu), std::bit_cast<float>(0x3e4ccccdu)};
        if (auto lit = Light(services, at, effect.light, 4, "0x5249d1"); !lit.HasValue())
            return lit;
        const auto height = Height(services, at.x, at.y, "0x5249dd");
        if (!height.HasValue())
            return Core::Unexpected(height.Error());
        if (height.Value() > D(at.z))
        {
            effect.alive = false;
            return {};
        }
        return FireGlow(effect, luminosity, local, random, services);
    }
    if (subtype == 4)
    {
        // 0x524a04..0x524b46.
        const auto cell = MaskedCellAttribute(services, at, "0x524a2c");
        if (!cell.HasValue())
            return Core::Unexpected(cell.Error());
        if ((static_cast<std::uint8_t>(cell.Value()) & BlockedCell) == BlockedCell)
        {
            if (!(at.z < -200.0f))
                return FireGlow(effect, luminosity, local, random, services);
            if (auto landed = LandTheFire(effect, D(at.z), local, random, services); !landed.HasValue())
                return landed;
            return FireGlow(effect, luminosity, local, random, services);
        }
        const auto height = Height(services, at.x, at.y, "0x524a49");
        if (!height.HasValue())
            return Core::Unexpected(height.Error());
        const float ground = F32(height.Value());
        if (!(at.z < ground))
            return FireGlow(effect, luminosity, local, random, services);
        if (services.cameraShake == nullptr)
            return Core::Unexpected(std::string("0x524a87: falta el temblor de la cámara (0x7d325c8)"));
        *services.cameraShake = F32(static_cast<double>((random.Next() & 3) - 4) * Tenth);
        // 0x524a8d..0x524b21: the nearby characters go to 0x4e9cd0 (the 0x20a with +0x318 at 0x1c37) or to 0x4ea030
        // (0xc2 the 0x20a, 5 the others).
        if (!services.pushAround)
            return Core::Unexpected(std::string("0x524a8d: faltan los personajes del pool (0x743a16c)"));
        if (auto pushed = services.pushAround(at.x, at.y); !pushed.HasValue())
            return pushed;
        // 0x524b27: with or without pushed ones, it falls on the ground (fld [ebp-0x7c]; jmp 0x524c64).
        if (auto landed = LandTheFire(effect, D(ground), local, random, services); !landed.HasValue())
            return landed;
        return FireGlow(effect, luminosity, local, random, services);
    }
    if (subtype == 6 || subtype == 7 || subtype == 8)
    {
        // 0x524b5e..0x524c1b: the head's three sprites and the angles of +0x28.
        if (auto sprite = Sprite(services, "0x524b8a", FlareSprite, at, 3.0f, {0.5f, 0.5f, 1.0f}, 0.0f, 0);
            !sprite.HasValue())
            return sprite;
        local = glm::vec3(1.0f);
        if (auto sprite =
                Sprite(services, "0x524bd0", ShineSprite, at, 3.0f, local, static_cast<float>(random.Next() % 360), 1);
            !sprite.HasValue())
            return sprite;
        if (auto sprite = Sprite(services, "0x524c04", ShineSprite, at, 4.0f, effect.light,
                                 static_cast<float>(random.Next() % 360), 0);
            !sprite.HasValue())
            return sprite;
        effect.transform.angles = effect.copiedAngles;
    }
    if (subtype == 0 || subtype == 2 || subtype == 6 || subtype == 7 || subtype == 8)
    {
        // 0x524c1e..0x524c5e: in Kalima (24..29 and 36) the ground is 50 higher.
        if (services.map == nullptr)
            return Core::Unexpected(std::string("0x524c1e: falta el mapa (0x6c9dd0)"));
        const std::int32_t map = *services.map;
        const double lift = (map >= 0x18 && map <= 0x1d) || map == 0x24 ? 50.0 : 0.0;
        const auto height = Height(services, at.x, at.y, "0x524c4b");
        if (!height.HasValue())
            return Core::Unexpected(height.Error());
        const double ground = height.Value() + lift;
        if (ground > D(at.z))
            if (auto landed = LandTheFire(effect, ground, local, random, services); !landed.HasValue())
                return landed;
    }
    return FireGlow(effect, luminosity, local, random, services);
}

// 0x5231be..0x52336f.
Core::Expected<void, std::string> StepInnerCircle(ModelEffect& effect, CrtRandom& random,
                                                  const WizardEffectServices& services)
{
    effect.objectState.light = F32(LifeTimes(effect.life, Tenth));
    if (effect.subtype == 1)
    {
        const auto owner = OwnerOf(effect, services, "0x5231d6");
        if (!owner.HasValue())
            return Core::Unexpected(owner.Error());
        if (effect.life <= 44 - owner.Value().byte1bc)
            return {};
        // 0x5231f6..0x5232f8: 36 joints 0x613/6 fanned out and, with life at 45 − +0x1bc, a 0x613/7 behind each one.
        // The local angles are (−10, 0, z) and the point (x, y, z + 100), the same for both positions; the effect is
        // the owner, width 60, with key 0, skill 0 and no target.
        constexpr int Rays = 36;               // 0x5232f2: 0x24
        constexpr std::int32_t Spread = 10;    // 0x523225: rand() % 10
        constexpr double SpreadBase = 10.0;    // 0x6b87a8
        constexpr double Rise = 100.0;         // 0x6b86b4
        constexpr float Tilt = -10.0f;         // 0x523208: 0xc1200000
        constexpr float RayWidth = 60.0f;      // 0x523234: 0x42700000
        constexpr std::int32_t RayKey = 0;     // 0x523230
        constexpr std::int32_t FirstStep = 45; // 0x523299: 0x2d − +0x1bc
        const std::int32_t firstStep = FirstStep - owner.Value().byte1bc;
        glm::vec3 angles{Tilt, 0.0f, 0.0f};
        for (int ray = 0; ray < Rays; ++ray)
        {
            const auto turn = static_cast<float>(ray); // 0x523205: fild and fstp to [ebp+8]
            const auto spread = static_cast<std::int32_t>(random.Next() % Spread);
            angles.z = F32((static_cast<double>(spread) + SpreadBase) * D(turn));
            glm::vec3 point{effect.transform.position.x, effect.transform.position.y,
                            F32(D(effect.transform.position.z) + Rise)};
            if (auto joint = Joint(services, "0x52328c",
                                   {.type = SnowBlastJoint,
                                    .first = point,
                                    .second = point,
                                    .angles = angles,
                                    .subtype = 6,
                                    .owner = &effect,
                                    .scale = RayWidth,
                                    .key = RayKey});
                !joint.HasValue())
                return joint;
            if (effect.life != firstStep)
                continue;
            angles.z = F32(D(turn) * SpreadBase); // 0x5232b0
            if (auto joint = Joint(services, "0x5232e9",
                                   {.type = SnowBlastJoint,
                                    .first = point,
                                    .second = point,
                                    .angles = angles,
                                    .subtype = 7,
                                    .owner = &effect,
                                    .scale = RayWidth,
                                    .key = RayKey});
                !joint.HasValue())
                return joint;
        }
        return {};
    }
    if (effect.subtype == 2)
    {
        const std::int32_t rise = effect.life > 240 ? 250 - effect.life : effect.life;
        effect.objectState.light = F32(LifeTimes(rise, Tenth));
    }
    else if (effect.subtype == 3)
    {
        const std::int32_t rise = effect.life > 10 ? 30 - effect.life : effect.life;
        effect.objectState.light = F32(LifeTimes(rise, Tenth));
    }
    return {};
}

// 0x523374..0x5238b9.
Core::Expected<void, std::string> StepOuterCircle(ModelEffect& effect, float luminosity, CrtRandom& random,
                                                  const WizardEffectServices& services)
{
    glm::vec3& at = effect.transform.position;
    if (effect.subtype == 3 || effect.subtype == 4)
    {
        const bool big = effect.subtype == 3;
        const std::int32_t edge = big ? 240 : 10;
        const std::int32_t rise = effect.life >= edge ? (big ? 250 : 20) - effect.life : effect.life;
        effect.objectState.light = F32(LifeTimes(rise, Tenth));
        if (effect.objectState.light > 0.5f)
            effect.objectState.light = 0.5f;
        effect.objectState.uv.x = F32(LifeTimes(effect.life, NegativeHundredth));
        const bool column =
            big ? effect.life > 30 && (random.Next() & 1) == 0 : random.Next() % 5 == 0 && effect.life > 5;
        if (column)
        {
            const glm::vec3 reach{0.0f, static_cast<float>(random.Next() % (big ? 200 : 100)), 0.0f};
            const glm::vec3 turn{0.0f, 0.0f, static_cast<float>(random.Next() % 360)};
            glm::vec3 point = Turned(reach, turn);
            AddTo(point, at);
            if (auto beam = Emit(services, random, big ? "0x523677" : "0x52384c", ColumnParticle, point,
                                 effect.transform.angles, effect.light, 0, 1.0f);
                !beam.HasValue())
                return beam;
            if (!big || effect.life > 40)
            {
                point.z = F32(D(point.z) + 600.0); // 0x6b8c08
                if (auto joint = Joint(services, big ? "0x5236ce" : "0x523898",
                                       {.type = ColumnJoint,
                                        .first = point,
                                        .second = point,
                                        .angles = {0.0f, 0.0f, 45.0f},
                                        .subtype = 0x13,
                                        .owner = nullptr,
                                        .scale = 40.0f,
                                        .key = -1});
                    !joint.HasValue())
                    return joint;
            }
        }
        return Light(services, at, glm::vec3(effect.objectState.light), 4, big ? "0x5236fb" : "0x5212da");
    }
    const std::int32_t rise = effect.life >= 30 ? 40 - effect.life : effect.life;
    effect.objectState.light = F32(LifeTimes(rise, Tenth));
    effect.objectState.uv.x = F32(LifeTimes(effect.life, NegativeHundredth));
    if (services.cameraShake == nullptr)
        return Core::Unexpected(std::string("0x5233d6: falta el temblor de la cámara (0x7d325c8)"));
    *services.cameraShake = F32(static_cast<double>(random.Next() % 6 - 6) * Tenth);
    if ((random.Next() & 3) == 0)
    {
        // 0x5233f3..0x5234e1: a rising pebble (subtype 1) at rand() % 300 rotated by rand() % 360.
        const glm::vec3 reach{0.0f, static_cast<float>(random.Next() % 300), 0.0f};
        const glm::vec3 turn{0.0f, 0.0f, static_cast<float>(random.Next() % 360)};
        const glm::vec3 offset = Turned(reach, turn);
        glm::vec3 point{};
        for (int axis = 0; axis < 3; ++axis)
            point[axis] = F32(D(offset[axis]) + D(at[axis]));
        if (auto created = Chip(services, random, "0x5234e1", point, effect.transform.angles, effect.light,
                                RisingStoneChipSubtype);
            !created.HasValue())
            return created;
    }
    const glm::vec3 glow{luminosity, F32(D(luminosity) * EightTenths), F32(D(luminosity) * Fifth)};
    return Light(services, at, glow, 4, "0x52351a");
}

// 0x522bd2..0x522db2.
Core::Expected<void, std::string> StepMagic(ModelEffect& effect, float luminosity, CrtRandom& random,
                                            const WizardEffectServices& services)
{
    glm::vec3& at = effect.transform.position;
    effect.objectState.light = F32(LifeTimes(effect.life, Tenth));
    effect.objectState.uv.x = F32(LifeTimes(effect.life, NegativeFifth));
    for (int smoke = 0; smoke < 4; ++smoke)
        if (auto created = Emit(services, random, "0x522c0e", Particles::BreathParticle, at, effect.transform.angles,
                                effect.light, 3, 1.0f);
            !created.HasValue())
            return created;
    const glm::vec3 local{F32(D(luminosity) * ThreeTenths), F32(D(luminosity) * SixTenths), luminosity};
    if (auto lit = Light(services, at, local, 3, "0x522c4f"); !lit.HasValue())
        return lit;
    if (effect.subtype != 2)
        return {};
    effect.objectState.hiddenMesh = 0;
    glm::vec3 point = at;
    static_cast<void>(random.Next() % 10); // 0x522c8c: the z of a copy of the angles nobody reads.
    const float scale = F32(static_cast<double>((random.Next() & 31) + 80) * DustStep);
    if (auto dust = Emit(services, random, "0x522cdb", Particles::BreathParticle, at, effect.transform.angles, local,
                         Particles::StoneDustSubtype, scale);
        !dust.HasValue())
        return dust;
    point.z = F32(D(point.z) + 50.0);
    for (int shine = 0; shine < 2; ++shine)
        if (auto sprite = Sprite(services, shine == 0 ? "0x522d3d" : "0x522d75", ShineSprite, point, 1.5f,
                                 glm::vec3(1.0f), static_cast<float>(random.Next() % 360), 0);
            !sprite.HasValue())
            return sprite;
    return Sprite(services, "0x522daa", FlareSprite, point, 3.5f, local, static_cast<float>(random.Next() % 360), 0);
}

// 0x522db7..0x52b155.
Core::Expected<void, std::string> StepStorm(ModelEffect& effect, float luminosity, CrtRandom& random,
                                            const WizardEffectServices& services)
{
    glm::vec3& at = effect.transform.position;
    const std::int32_t subtype = effect.subtype;
    if (subtype < 0 || subtype > 7)
        return {};
    if (subtype >= 3)
    {
        // 0x523101..0x52b155.
        if (services.cameraShake == nullptr)
            return Core::Unexpected(std::string("0x52313e: falta el temblor de la cámara (0x7d325c8)"));
        *services.cameraShake = F32(static_cast<double>((random.Next() & 7) - 4) * Tenth);
        // 0x523144, 0x52315d and 0x523176: the mist 0x5f2/28, /29 and /30 at the effect's point, with its angles and
        // light.
        constexpr std::array<std::pair<std::int32_t, std::string_view>, 3> Mists{
            {{Particles::StormMistSubtype, "0x523144"},
             {Particles::DarkStormMistSubtype, "0x52315d"},
             {Particles::ShortStormMistSubtype, "0x523176"}}};
        for (const auto& [mist, address] : Mists)
            if (auto created = EmitOnGround(services, random, address, Particles::BreathParticle, at,
                                            effect.transform.angles, effect.light, mist, 1.0f);
                !created.HasValue())
                return created;
        if ((random.Next() & 1) != 0)
            return {};
        return Chip(services, random, "0x52b14d", at, effect.transform.angles, effect.light, 2);
    }
    if (subtype == 0)
    {
        effect.objectState.light = F32(LifeTimes(effect.life, Tenth));
        effect.objectState.uv.x = F32(LifeTimes(effect.life, NegativeTenth));
        if (auto created = Emit(services, random, "0x522dff", Particles::BreathParticle, at, effect.transform.angles,
                                effect.light, 3, 1.0f);
            !created.HasValue())
            return created;
        const glm::vec3 down{90.0f, 0.0f, effect.transform.angles.z};
        for (const double side : {-200.0, 200.0})
        {
            if ((random.Next() & 1) != 0)
                continue;
            const glm::vec3 top{F32(D(at.x) + side), at.y, F32(D(at.z) + 700.0)}; // 0x6b8928 and 0x6b8bec
            if (auto joint = Joint(services, side < 0.0 ? "0x522e69" : "0x522ebf",
                                   {.type = StormJoint,
                                    .first = top,
                                    .second = at,
                                    .angles = down,
                                    .subtype = 0,
                                    .owner = &effect,
                                    .scale = 10.0f,
                                    .key = -1});
                !joint.HasValue())
                return joint;
        }
        const auto height = Height(services, at.x, at.y, "0x522ece");
        if (!height.HasValue())
            return Core::Unexpected(height.Error());
        at.z = F32(height.Value());
        if ((random.Next() & 3) == 0)
            if (auto created = Chip(services, random, "0x522f17", at, effect.transform.angles, effect.light, 2);
                !created.HasValue())
                return created;
        const glm::vec3 shade{F32(D(luminosity) * NegativeFourTenths), F32(D(luminosity) * NegativeThreeTenths),
                              F32(D(luminosity) * NegativeFifth)};
        if (auto lit = Light(services, at, shade, 5, "0x522f55"); !lit.HasValue())
            return lit;
        if (services.siegeActive)
        {
            if (services.terrainAttributes == nullptr)
                return Core::Unexpected(std::string("0x522f6d: faltan los atributos de las celdas (0x7d0d348)"));
            const auto attribute = Terrain::TerrainAttributeQueryAt(*services.terrainAttributes, at.x, at.y);
            if (!attribute.HasValue())
                return Core::Unexpected(std::string("0x522f6d: la celda del 0xcf/0 cae fuera de la tabla"));
            constexpr std::uint32_t SiegeWall = 0x100;
            if ((attribute.Value() & SiegeWall) == SiegeWall)
            {
                // 0x522f81..0x522fa9: against the siege wall it stops and keeps a fifth of its life.
                effect.speed = 0.0f;
                effect.velocity = {};
                effect.life = Math::FtolLow(LifeTimes(effect.life, Fifth));
                return {};
            }
        }
        if (effect.life % 15 != 0 || services.hero == nullptr || effect.ownerContext != services.hero)
            return {};
        if (!services.skillHits)
            return Core::Unexpected(std::string("0x522ff7: el 0xcf/0 del héroe pide 0x517a20 (Scenes/SkillHits)"));
        return services.skillHits(effect, 150.0f);
    }
    if (subtype == 1)
    {
        effect.objectState.light = F32(LifeTimes(effect.life, Hundredth));
        effect.objectState.uv.x = F32(LifeTimes(effect.life, NegativeTenth));
        effect.transform.angles.z =
            F32((static_cast<double>(random.Next() % 30) + D(effect.transform.angles.z)) + 30.0);
    }
    else
    {
        effect.objectState.light = F32(LifeTimes(effect.life, Tenth));
        effect.objectState.uv.x = F32(LifeTimes(effect.life, NegativeTenth));
        effect.turnDirection = static_cast<float>(random.Next() % 360);
    }
    // 0x52303b..0x5230c3.
    const glm::vec3 point{at.x, at.y, F32(D(at.z) + 100.0)};
    if (auto created = Emit(services, random, "0x523070", Particles::BreathParticle, point, effect.transform.angles,
                            effect.light, 3, 1.0f);
        !created.HasValue())
        return created;
    if (auto created = Emit(services, random, "0x523089", Particles::DropParticle, point, effect.transform.angles,
                            effect.light, 3, std::bit_cast<float>(0x3dcccccdu));
        !created.HasValue())
        return created;
    // 0x52308e..0x5230be: nobody reads the tone (−0.1L, −0.3L, −L) it stores in [ebp-0x1c]; 0x5f9780 receives the
    // effect's light (esi = +0xf4, 0x523049).
    return Light(services, at, effect.light, 5, "0x5230be");
}

Core::Expected<void, std::string> StepCase(ModelEffect& effect, float luminosity, glm::vec3& local, CrtRandom& random,
                                           const WizardEffectServices& services)
{
    glm::vec3& at = effect.transform.position;
    switch (effect.type)
    {
    case ShadowRainEffect: // 0x525438
    {
        if (effect.subtype != 0 || effect.ownerContext == nullptr || effect.life <= 10)
            return {};
        const int spread = (random.Next() & 127) - 64;
        const auto owner = OwnerOf(effect, services, "0x52548e");
        if (!owner.HasValue())
            return Core::Unexpected(owner.Error());
        const glm::vec3& near = owner.Value().position;
        const double base = static_cast<double>(spread) - 50.0;
        const glm::vec3 point{F32(D(F32(base)) + D(near.x)), F32(D(F32(base + 100.0)) + D(near.y)),
                              F32(D(near.z) + 360.0)};
        const float scale = F32(static_cast<double>(random.Next() % 30 + 50) * Hundredth);
        return Emit(services, random, "0x5254f9", ShinyParticleType, point, effect.transform.angles, effect.light, 2,
                    scale);
    }
    case IceEffect:
        return StepIce(effect, luminosity, random, services);
    case FireEffect:
        return StepFire(effect, luminosity, local, random, services);
    case PoisonEffect: // 0x521747
        effect.objectState.light = F32(LifeTimes(effect.life, Tenth));
        effect.alpha = F32(LifeTimes(effect.life, Tenth));
        local = {F32(D(luminosity) * ThreeTenths), luminosity, F32(D(luminosity) * SixTenths)};
        return Light(services, at, local, 2, "0x52c4f1");
    case HellGateEffect: // 0x5251b6
    {
        const auto owner = OwnerOf(effect, services, "0x5251b6");
        if (!owner.HasValue())
            return Core::Unexpected(owner.Error());
        at = owner.Value().position;
        return {};
    }
    case PlainSkillEffect:
        return {};
    case InnerCircleEffect:
        return StepInnerCircle(effect, random, services);
    case OuterCircleEffect:
        return StepOuterCircle(effect, luminosity, random, services);
    case FollowingMagicEffect: // 0x5217da
    {
        const auto owner = OwnerOf(effect, services, "0x5217da");
        if (!owner.HasValue())
            return Core::Unexpected(owner.Error());
        at.x = owner.Value().position.x;
        at.y = owner.Value().position.y;
        at.z = F32(D(owner.Value().position.z) + 100.0);
        effect.transform.angles.y = F32(D(effect.transform.angles.y) + 20.0);
        effect.objectState.light = F32(LifeTimes(effect.life, Tenth));
        return {};
    }
    case MagicEffect:
        return StepMagic(effect, luminosity, random, services);
    case StormEffect:
        return StepStorm(effect, luminosity, random, services);
    case LaserEffect: // 0x52179d
        if (effect.subtype != 0 && effect.subtype != 3)
        {
            effect.velocity.y = F32(D(effect.velocity.y) - D(effect.speed));
            effect.speed = F32(D(effect.speed) + 1.0);
        }
        return {};
    case SawEffect: // 0x52178c
        effect.transform.angles.z = F32(D(effect.transform.angles.z) - 30.0);
        return {};
    case FirstBoneEffect:
    case SecondBoneEffect:
    case FirstSnowStoneEffect:
    case SecondSnowStoneEffect:
        return StepChip(effect, random, services);
    case SnowEffect: // 0x523b6b
        effect.velocity.z = F32(D(effect.velocity.z) - 2.5);
        return HitTheOwner(effect, random, services);
    default:
        return Core::Unexpected(std::format("0x5208a0: el tipo {:#x} no es del lote de los magos", effect.type));
    }
}

// 0x52cb98..0x52cdb2 with the batch's types: all animate ([0xad, 0x151)), the 0xd4 moves unrotated (0x51fc80 with
// 0, the 0x52d1b8 table) and the others rotated; one life less and, at 0 or less, it dies.
void StepTail(ModelEffect& effect, const Models::Skeleton* skeleton, const WizardEffectServices& services)
{
    constexpr std::uint8_t NoAction = 0xff;
    if (services.publishAction)
        services.publishAction(static_cast<std::uint16_t>(effect.type), effect.animation.action);
    if (effect.animation.action != NoAction && skeleton != nullptr &&
        effect.animation.action < skeleton->actions.size())
    {
        const Models::ActionInfo& action = skeleton->actions[effect.animation.action];
        if (Models::AdvanceAnimation(effect.animation, action, effect.speed, Models::AnimationScene::Other))
        {
            effect.animation.frame = static_cast<float>(action.keyCount);
            effect.animation.action = NoAction;
        }
    }
    AddTo(effect.transform.position,
          effect.type == SawEffect ? effect.velocity : Turned(effect.velocity, effect.transform.angles));
    --effect.life;
    if (effect.life <= 0)
        effect.alive = false;
}
} // namespace

bool IsWizardModelEffect(std::int16_t type)
{
    switch (type)
    {
    case ShadowRainEffect:
    case IceEffect:
    case FireEffect:
    case PoisonEffect:
    case HellGateEffect:
    case PlainSkillEffect:
    case InnerCircleEffect:
    case OuterCircleEffect:
    case FollowingMagicEffect:
    case MagicEffect:
    case StormEffect:
    case LaserEffect:
    case SawEffect:
    case FirstBoneEffect:
    case SecondBoneEffect:
    case SnowEffect:
    case FirstSnowStoneEffect:
    case SecondSnowStoneEffect:
        return true;
    default:
        return false;
    }
}

Core::Expected<std::optional<std::size_t>, std::string>
CreateWizardModelEffect(ModelEffects& effects, const Kernel::ModelEffectArguments& arguments, Crt::CrtRandom& random,
                        const WizardEffectServices& services, glm::vec3* callerLight)
{
    if (!IsWizardModelEffect(arguments.type))
        return Core::Unexpected(std::format("0x518cf0: el tipo {:#x} no es del lote de los magos", arguments.type));
    const std::optional<std::size_t> slot = effects.Begin(arguments);
    if (!slot)
        return std::optional<std::size_t>{};
    if (auto created = CreateCase(effects.Effects()[*slot], arguments, random, services, callerLight);
        !created.HasValue())
        return Core::Unexpected(created.Error());
    return slot;
}

Core::Expected<void, std::string> StepWizardModelEffect(ModelEffect& effect, const Models::Skeleton* skeleton,
                                                        Crt::CrtRandom& random, const WizardEffectServices& services)
{
    if (!effect.alive)
        return {};
    if (!IsWizardModelEffect(effect.type))
        return Core::Unexpected(std::format("0x5208a0: el tipo {:#x} no es del lote de los magos", effect.type));
    const glm::vec3 before = effect.transform.position;
    // 0x5208c5..0x52092f: the first rand() gives the local light L and the step's light starts white.
    const float luminosity = StepLuminosity(random.Next(), effect.life);
    glm::vec3 local(1.0f);
    if (auto stepped = StepCase(effect, luminosity, local, random, services); !stepped.HasValue())
        return stepped;
    StepTail(effect, skeleton, services);
    effect.previousPosition = before;
    return {};
}
} // namespace Client::Effects
