#include "Scenes/CharacterEffectSteps.h"

#include "Actors/JointHooks.h"
#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/ActorState.h"
#include "Kernel/JointRequest.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/Ftol.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Math/X87.h"
#include "World/ModelEffects.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

namespace Client::Scenes
{
namespace
{
using Math::D;
using Math::F32;
using Math::Tenth;

constexpr std::int32_t NoKey = -1, NoTarget = -1;

// 0x52a9ad: the 0x5ac/3 every 15 steps, 50 from the owner and 20 higher, with joint 0x619/9 of width 10.
constexpr std::int32_t FlarePeriod = 15;
constexpr float FlareRadius = 50.0f;
constexpr std::int32_t EnergyType = 0x619, EnergySubtype = 9;
constexpr float EnergyWidth = 10.0f;

// 0x52a7b7 and 0x52a8a7: the 0x5ac/1 and /2 of the incoming skills live 10 more steps while the owner is alive with its
// +0x78 flag; with 0x4000 on the owner they do nothing. The /1 drops two 0x5ac/4 of light (1, 0.5f, 0.1f) with the bone
// indices 0x6c8c38 + rand() % 7 and 0x6c8c3e − that; the /2, every 50 of +0xd8, turns 50, moves 80 from the owner and
// 50 higher, and drops joint 0x619/9 of width 15.
constexpr std::int32_t SkillFlareLife = 10;
constexpr std::uint32_t SwellFlag = 0x10, GuardFlag = 0x400, QuietOwnerFlag = 0x4000;
constexpr std::array<float, 7> SwellBones{25, 26, 27, 20, 34, 35, 36};
constexpr std::int32_t GuardPeriod = 50;
constexpr double GuardTurn = 50.0;   // 0x6b86b0
constexpr float GuardRadius = 80.0f; // 0x6b9ffc
constexpr double GuardRise = 50.0;   // 0x6b86b0
constexpr float GuardWidth = 15.0f;  // 0x41700000

// 0x518900: the three 0x619 joints of width 5 the pet's decal 0x61e/1 drops on each step, 200 farther ([ebp−0x20]) and
// 120 higher (0x6b86c8), with the decal's subtype. Subtype 5 would pass as 0xb, but MuRet only creates /1 and /3.
constexpr int SummonJoints = 3;
constexpr float SummonWidth = 5.0f;
constexpr float SummonReach = 200.0f;
constexpr double SummonLift = 120.0;
constexpr int SummonPitchSpan = 90, SummonTurnSpan = 360;

Core::Expected<void, std::string> RaiseSwellLifeChildren(const World::ModelEffect& effect, World::Particles* particles,
                                                         World::ParticleOwner owner, Crt::CrtRandom& random)
{
    // 0x52a82b..0x52a89a: the selector runs even with the particle table full.
    const std::size_t bone = static_cast<std::size_t>(random.Next()) % SwellBones.size();
    if (particles == nullptr)
        return Core::Unexpected<std::string>("0x52a866: faltan las partículas de Swell Life");
    for (const std::size_t index : std::array{bone, SwellBones.size() - 1 - bone})
    {
        const auto made = particles->CreateOwnedChecked(World::SwellLifeParticle, effect.transform.position,
                                                        glm::vec3(0), {1, 0.5f, 0.1f}, World::SwellLifeParticleSubtype,
                                                        SwellBones[index], 0, random, 0, owner);
        if (!made.HasValue())
            return Core::Unexpected<std::string>("0x52a866/0x52a89a: no se pudo crear la luz de Swell Life");
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> StepFullSetFlare(World::ModelEffect& effect, std::span<JointState> joints,
                                                   const Actors::IJointHooks* jointHooks, Crt::CrtRandom& random)
{
    if (!effect.alive)
        return {};
    if (effect.type != World::IcarusFlareEffect || effect.subtype != World::FullSetFlareSubtype)
        return Core::Unexpected(
            std::format("0x52a9ad: el efecto {:#x}/{} no es el 0x5ac/3", effect.type, effect.subtype));
    // 0x5208c5: the first rand() runs for every live effect, even if the 0x5ac/3 does not use the light.
    static_cast<void>(random.Next());
    constexpr double Step = 1.0;                        // 0x6b87d4
    constexpr double HalfTurn = Math::HalfTurnAsDouble; // 0x6b86bc
    constexpr double Rise = 20.0;                       // 0x6b86d0
    effect.speed = F32(D(effect.speed) + Step);
    if (effect.life % FlarePeriod == 0 && effect.ownerContext != nullptr)
    {
        const auto& owner = *static_cast<const Kernel::ActorState*>(effect.ownerContext);
        glm::vec3& angles = effect.transform.angles;
        glm::vec3& position = effect.transform.position;
        // 0x52a9fe: fst; the sine comes from the unrounded sum and the cosine loads the float again (Q-C380).
        const double turned = D(angles.z) + HalfTurn;
        angles.z = F32(turned);
        const auto sine = Math::Detail::SineProductPC53(turned * D(Tenth), FlareRadius);
        const auto cosine = Math::Detail::CosineProductPC53(D(angles.z) * D(Tenth), FlareRadius);
        if (!sine || !cosine)
            return Core::Unexpected<std::string>("0x52aa0d: el giro del 0x5ac/3 excede el perfil matemático");
        position.x = F32(*sine + D(owner.position.x));
        position.y = F32(*cosine + D(owner.position.y));
        position.z = F32(D(owner.position.z) + Rise);
        if (joints.empty() || jointHooks == nullptr)
            return Core::Unexpected<std::string>("0x52aa5d: faltan los joints del paso (0x530860)");
        const auto created = jointHooks->CreateTheJoint(joints,
                                                        {EnergyType, position, position, angles, EnergySubtype,
                                                         effect.ownerContext, EnergyWidth, NoKey, 0, 0, NoTarget},
                                                        random, Kernel::JointOwnerKind::ByType);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x52aa5d: {}", created.Error()));
    }
    // 0x52cb98: 0x5ac does not advance an animation; the speed (0) turned and one life less.
    World::StepModelEffect(effect, nullptr);
    return {};
}

Core::Expected<void, std::string> StepSkillFlare(World::ModelEffect& effect, std::span<JointState> joints,
                                                 const Actors::IJointHooks* jointHooks, Crt::CrtRandom& random,
                                                 World::Particles* particles, World::ParticleOwner particleOwner)
{
    if (!effect.alive)
        return {};
    if (effect.type != World::IcarusFlareEffect ||
        (effect.subtype != World::SwellFlareSubtype && effect.subtype != World::GuardFlareSubtype))
        return Core::Unexpected(
            std::format("0x52a7b7: el efecto {:#x}/{} no es el 0x5ac/1 ni el /2", effect.type, effect.subtype));
    // 0x5208c5: the first rand() runs for every live effect, even if the 0x5ac/1 and the /2 do not use the light.
    static_cast<void>(random.Next());
    const bool swell = effect.subtype == World::SwellFlareSubtype;
    // 0x52a7e6 and 0x52a8dc: the owner's +0x78 is read even with +0x108 at 0; MuRet always creates them with the
    // target.
    if (effect.ownerContext == nullptr)
    {
        effect.life = 0; // 0x52a7e3/0x52a8d9, before reading the null owner's +0x78.
        return Core::Unexpected(
            std::format("{}: el 0x5ac/{} sin dueño lee +0x78 de 0", swell ? "0x52a7ec" : "0x52a8e2", effect.subtype));
    }
    const auto& owner = *static_cast<const Kernel::ActorState*>(effect.ownerContext);
    const std::uint32_t flag = swell ? SwellFlag : GuardFlag;
    effect.life = owner.alive && (owner.flags78 & flag) == flag ? SkillFlareLife : 0;
    if ((owner.flags78 & QuietOwnerFlag) != QuietOwnerFlag)
    {
        if (swell)
        {
            const auto raised = RaiseSwellLifeChildren(effect, particles, particleOwner, random);
            if (!raised.HasValue())
                return raised;
        }
        else
        {
            constexpr double Step = 1.0; // 0x6b87d4
            // 0x52a8f7..0x52a91e: fst leaves the sum on the stack for the ftol (0x69fd3c).
            const double counted = D(effect.speed) + Step;
            effect.speed = F32(counted);
            if (Math::FtolLow(counted) % GuardPeriod == 0)
            {
                glm::vec3& angles = effect.transform.angles;
                glm::vec3& position = effect.transform.position;
                // 0x52a92a: fst; the sine comes from the unrounded sum and the cosine loads the float again (Q-C380).
                const double turned = D(angles.z) + GuardTurn;
                angles.z = F32(turned);
                const auto sine = Math::Detail::SineProductPC53(turned * D(Tenth), GuardRadius);
                const auto cosine = Math::Detail::CosineProductPC53(D(angles.z) * D(Tenth), GuardRadius);
                if (!sine || !cosine)
                    return Core::Unexpected<std::string>("0x52a950: el giro del 0x5ac/2 excede el perfil matemático");
                position.x = F32(*sine + D(owner.position.x));
                position.y = F32(*cosine + D(owner.position.y));
                position.z = F32(D(owner.position.z) + GuardRise);
                if (joints.empty() || jointHooks == nullptr)
                    return Core::Unexpected<std::string>("0x52a9a0: faltan los joints del paso (0x530860)");
                const auto created =
                    jointHooks->CreateTheJoint(joints,
                                               {EnergyType, position, position, angles, EnergySubtype,
                                                effect.ownerContext, GuardWidth, NoKey, 0, 0, NoTarget},
                                               random, Kernel::JointOwnerKind::ByType);
                if (!created.HasValue())
                    return Core::Unexpected(std::format("0x52a9a0: {}", created.Error()));
            }
        }
    }
    // 0x52cb98: 0x5ac does not advance an animation; the speed (0) turned and one life less.
    World::StepModelEffect(effect, nullptr);
    return {};
}

bool StepsAsPetSummon(const World::ModelEffect& effect)
{
    // 0x52b984..0x52b9a5: /4 and /6 to /0xa jump to 0x52b9b8; the others from 1 on, to 0x518900.
    constexpr std::int32_t PulsingSubtype = 4, FirstBindingSubtype = 6, LastOwnSubtype = 0xa;
    return effect.type == World::ColoredDecalEffect && effect.subtype != PulsingSubtype &&
           (effect.subtype < FirstBindingSubtype || effect.subtype > LastOwnSubtype);
}

Core::Expected<void, std::string> StepPetSummon(World::ModelEffect& effect, std::span<JointState> joints,
                                                const Actors::IJointHooks* jointHooks, Crt::CrtRandom& random)
{
    if (!effect.alive)
        return {};
    if (!StepsAsPetSummon(effect))
        return Core::Unexpected(
            std::format("0x52b9a6: el efecto {:#x}/{} no va a 0x518900", effect.type, effect.subtype));
    // 0x52b984 → 0x52b9b8 → 0x52bb29: /0 (and a negative subtype) does not enter 0x518900 or look up joints or an
    // owner.
    if (effect.subtype < World::PetSummonSubtype)
    {
        static_cast<void>(random.Next());
        World::StepModelEffect(effect, nullptr);
        return {};
    }
    if (joints.empty() || jointHooks == nullptr)
        return Core::Unexpected<std::string>("0x5189cc: faltan los joints del paso (0x530860)");
    // 0x5208c5: the first rand() runs for every live effect, even if 0x61e does not use the light.
    static_cast<void>(random.Next());
    glm::vec3& position = effect.transform.position;
    // 0x51898b..0x5189bd: /5 asks for 0x619/0xb; the others, the one of their subtype.
    constexpr std::int32_t SwappedSubtype = 5, SwappedJointSubtype = 0xb;
    const std::int32_t jointSubtype = effect.subtype == SwappedSubtype ? SwappedJointSubtype : effect.subtype;
    for (int made = 0; made < SummonJoints; ++made)
    {
        glm::vec3 angles{static_cast<float>(random.Next() % SummonPitchSpan), 0.0f,
                         static_cast<float>(random.Next() % SummonTurnSpan)};
        const glm::vec3 reach =
            Math::OriginalVectorRotate(glm::vec3(0.0f, -SummonReach, 0.0f), Math::OriginalAngleMatrix(angles));
        // 0x51897a..0x5189b4: each subtraction is stored; z adds 120 before it is stored.
        glm::vec3 start{F32(D(position.x) - D(reach.x)), F32(D(position.y) - D(reach.y)),
                        F32(D(position.z) - D(reach.z) + SummonLift)};
        const auto created = jointHooks->CreateTheJoint(
            joints,
            {EnergyType, start, position, angles, jointSubtype, effect.ownerContext, SummonWidth, NoKey, 0, 0,
             NoTarget},
            random,
            effect.owner.effect != nullptr ? Kernel::JointOwnerKind::ModelEffect : Kernel::JointOwnerKind::ByType);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x5189cc: {}", created.Error()));
    }
    // 0x52cb98: 0x61e is not a BMD and does not advance an animation; the speed (0) turned and one life less.
    World::StepModelEffect(effect, nullptr);
    return {};
}
} // namespace Client::Scenes
