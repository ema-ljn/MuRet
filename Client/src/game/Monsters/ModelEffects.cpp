#include "Monsters/ModelEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/ModelEffectArguments.h"
#include "Maps/MapId.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Protocol/Events/Events.h"
#include "Models/Animation.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Effects/ArrowEffects.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldMap.h"
#include "Math/Ftol.h"
#include "Effects/ModelEffects.h"
#include "Actors/Drawing/PosePublishing.h"
#include "Kernel/PoseBuffer.h"
#include "Joints/FaunaJointCreation.h"
#include "Assets/FaunaModels.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Terrain/GroundMarks.h"
#include "Kernel/JointRequest.h"
#include "Particles/Particles.h"
#include "Particles/PortableParticleTrigonometry.h"
#include "Particles/SelectionParticleKernels.h"
#include "Particles/Sprites.h"
#include "Terrain/DynamicWorldLight.h"
#include "Audio/SoundId.h"
#include "Sound/SoundIds.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Monsters
{
namespace
{
using Result = Core::Expected<void, std::string>;
// Whether the type has a case in 0x505de0 (true) or goes to the helpers of 0x5085db (false).
using CaseResult = Core::Expected<bool, std::string>;

using Math::D;
using Math::F32;

float Bits(std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}

// 0x6b87b8
using Math::Tenth;
const glm::vec3 White{1.0f, 1.0f, 1.0f};
const glm::vec3 NoOffset{0.0f, 0.0f, 0.0f};

// 0x4ec110(model, +0x120 + bone · 0x30, offset, out, 1): the own bones with the scale (+0x68) and the position (+0x6c)
// 0x505ec6 copied from the character to the model. Before the first draw, a zero matrix (Q-C318).
glm::vec3 BonePoint(const Kernel::ActorState& actor, std::size_t bone, const glm::vec3& offset)
{
    const Math::BoneTransform matrix = bone < actor.bodyPose.size() ? actor.bodyPose[bone] : Math::BoneTransform(0.0f);
    return Math::OriginalTransformPosition(Math::OriginalMatrixOf(matrix), offset, actor.scale, actor.position);
}

// 0x53fe70(type, position, angles, light, subtype, 1, 0). With the pool full it creates nothing and does not cut; with
// one the pool does not know, it cuts (CL11).
Result Emit(ModelEffectsPass& pass, std::uint32_t address, std::int32_t type, std::int32_t subtype,
            const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light)
{
    if (pass.particles == nullptr)
        return Core::Unexpected(std::format("{:#x}: faltan las partículas del modelo {:#x} (lugar {})", address,
                                            pass.actor.type, pass.slot));
    const auto created =
        pass.particles->CreateChecked(type, position, angles, light, subtype, 1.0f, pass.worldClock, pass.random, 0);
    if (!created.HasValue())
        return Core::Unexpected(std::format("{:#x}: la partícula {:#x}/{} del modelo {:#x} (lugar {}) no está portada "
                                            "(error {}, CL11)",
                                            address, type, subtype, pass.actor.type, pass.slot,
                                            static_cast<unsigned>(created.Error())));
    return {};
}

// 0x5f9780(x, y, color, radius, 0x7b4ccc4).
Result Light(ModelEffectsPass& pass, std::string_view address, const glm::vec3& color, std::int32_t radius)
{
    return Terrain::AddDynamicLight(pass.dynamicLighting, pass.actor.position, color, radius, address);
}

// 0x530860(type, first, second, angles, subtype, 0, width, −1, 0, 0, −1). With the pool full it creates nothing.
Result Joint(ModelEffectsPass& pass, std::uint32_t address, std::int32_t type, std::int32_t subtype, glm::vec3 first,
             glm::vec3 second, glm::vec3 angles, float width)
{
    const Kernel::JointRequest request{.type = type,
                                       .firstPosition = first,
                                       .secondPosition = second,
                                       .angles = angles,
                                       .subtype = subtype,
                                       .ownerContext = nullptr,
                                       .scale = width,
                                       .key = -1,
                                       .skill = 0,
                                       .skillSerial = 0,
                                       .target = -1};
    const auto created = Joints::CreateFaunaJoint(pass.world.DrawTail().joints, request, pass.random);
    if (!created.HasValue())
        return Core::Unexpected(
            std::format("{:#x}: {} (modelo {:#x}, lugar {})", address, created.Error(), pass.actor.type, pass.slot));
    return {};
}

// 0x41ed10(sonido, 0, 0) (CL10).
void Sound(ModelEffectsPass& pass, Audio::SoundId sound)
{
    pass.world.Runtime().RequestSound(sound);
}

// FLD of the world clock, _ftol and IDIV: the signed remainder.
Core::Expected<std::int32_t, std::string> ClockRemainder(const ModelEffectsPass& pass, std::uint32_t address,
                                                         std::int32_t modulus)
{
    const auto clock = Math::OriginalFtolLow(D(pass.worldClock));
    if (!clock)
        return Core::Unexpected(std::format("{:#x}: ftol del reloj del mundo fuera de lo acreditado", address));
    return *clock % modulus;
}

// FILD of the remainder and FMUL by a float, stored as a float.
Core::Expected<float, std::string> ClockScroll(const ModelEffectsPass& pass, std::uint32_t address,
                                               std::int32_t modulus, float factor)
{
    const auto remainder = ClockRemainder(pass, address, modulus);
    if (!remainder.HasValue())
        return Core::Unexpected(remainder.Error());
    return F32(static_cast<double>(remainder.Value()) * D(factor));
}

// rand() % n with the IDIV (rand() is never negative).
std::int32_t Roll(ModelEffectsPass& pass, std::int32_t modulus)
{
    return pass.random.Next() % modulus;
}

// Three rand() around the position: (rand() & 63) − 32, (rand() & 63) − 32 and (rand() & 31) − 16, each through FILD
// and the coordinate's FADD, stored as a float (0x50830d..0x508386, 0x5083d7..0x508449, 0x505cb5..0x505d29).
glm::vec3 Scatter(ModelEffectsPass& pass)
{
    const glm::vec3& at = pass.actor.position;
    const float x = F32(static_cast<double>(Roll(pass, 64) - 32) + D(at.x));
    const float y = F32(static_cast<double>(Roll(pass, 64) - 32) + D(at.y));
    const float z = F32(static_cast<double>(Roll(pass, 32) - 16) + D(at.z));
    return {x, y, z};
}

// 0x505bc0(c, model, a, b): +0x4c with bone a and +0x40 with bone b, without offset.
void MarkTheBones(ModelEffectsPass& pass, std::size_t first, std::size_t second)
{
    pass.actor.bonePoint4c = BonePoint(pass.actor, first, NoOffset);
    pass.actor.effectOrigin = BonePoint(pass.actor, second, NoOffset);
}

// 0x505d50: in action 2, the 0x5f3/0 dust at rand() % 200 + x − 100 and rand() % 200 + y − 100 (FILD, FADD of the
// coordinate and FSUB of 100.0f on the x87), the character's z, with its own light (+0xf4).
Result RaiseTheDust(ModelEffectsPass& pass)
{
    if (pass.actor.animation.action != 2)
        return {};
    const glm::vec3& at = pass.actor.position;
    constexpr double Spread = 100.0;
    const float x = F32(static_cast<double>(Roll(pass, 200)) + D(at.x) - Spread);
    const float y = F32(static_cast<double>(Roll(pass, 200)) + D(at.y) - Spread);
    return Emit(pass, 0x505dc4, Particles::DustParticle, 0, {x, y, at.z}, pass.actor.angles, pass.actor.light);
}

// 0x505c50: in action 6 with the frame in [8, 9), twenty rounds: its own light at (1, 1, 1), three rand() around the
// position and the 0x5f3/1 dust.
Result BurstTheDust(ModelEffectsPass& pass)
{
    Kernel::ActorState& actor = pass.actor;
    const float frame = actor.animation.frame;
    if (actor.animation.action != 6 || !(frame >= 8.0f) || !(frame < 9.0f))
        return {};
    constexpr int Bursts = 20;
    for (int burst = 0; burst < Bursts; ++burst)
    {
        actor.light = White;
        const glm::vec3 position = Scatter(pass);
        if (auto emitted = Emit(pass, 0x505d2c, Particles::DustParticle, 1, position, actor.angles, actor.light);
            !emitted.HasValue())
            return emitted;
    }
    return {};
}

// 0x4eb860 (0x4eb86d..0x4eb94c): the key it leaves in +0xa8 of the model, as MonsterDrawing's PoseKeyOf.
std::optional<std::int16_t> PoseKeyOf(const Models::Skeleton& skeleton, const Models::AnimationState& animation)
{
    const auto truncated = Math::OriginalFtolLow(D(animation.frame));
    if (skeleton.actions.empty() || !truncated)
        return std::nullopt;
    auto key = static_cast<std::int16_t>(*truncated);
    if (key < 0)
        key = 0;
    const std::size_t action = animation.action < skeleton.actions.size() ? animation.action : 0;
    if (key >= skeleton.actions[action].keyCount)
        key = 0;
    return key;
}

// The speed of the character's action in its model (+0x30 + action · 0x10 + 4).
Core::Expected<float, std::string> SpeedOf(const ModelEffectsPass& pass, std::uint32_t address)
{
    const std::uint8_t action = pass.actor.animation.action;
    if (action >= pass.skeleton.actions.size())
        return Core::Unexpected(std::format("{:#x}: la acción {} del modelo {:#x} pasa su tabla de acciones (+0x30)",
                                            address, action, pass.actor.type));
    return pass.skeleton.actions[action].speed;
}

// 0x4eb860(model, 0x68b181c, frame − speed, +0x118, +0x112, angles, +0x28, 0, 1): the pose in the global buffer
// (WorldState::PoseCarry), with the flag at 1 (0x4318bc and 0x4314f3 push the 1): the scale and the origin go in the
// root (0x4ebbda..0x4ebc14) and the bones stay in the world, without the dummies, which keep what the buffer held
// (0x4eb973). The key stays in +0xa8 of the model. The model's action, scale and position are the character's
// (Q-C1102). Returns the speed.
Core::Expected<float, std::string> PoseTheModel(ModelEffectsPass& pass, std::uint32_t address)
{
    const auto speed = SpeedOf(pass, address);
    if (!speed.HasValue())
        return speed;
    const Kernel::ActorState& actor = pass.actor;
    const Models::Skeleton& skeleton = pass.skeleton;
    const Models::AnimationState animation{.action = actor.animation.action,
                                           .frame = F32(D(actor.animation.frame) - D(speed.Value())),
                                           .previousAction = actor.animation.previousAction,
                                           .previousKey = actor.animation.previousKey};
    const Models::ObjectTransform transform{
        .position = actor.position, .angles = actor.angles, .scale = actor.scale, .lockedRootHeight = 0.0f};
    Kernel::PoseBuffer& carry = pass.world.PoseCarry();
    if (carry.bones.size() < skeleton.bones.size())
        carry.bones.resize(skeleton.bones.size(), Math::BoneTransform(0.0f));
    const auto bones = std::span(carry.bones).first(skeleton.bones.size());
    if (!Models::PoseWorldSkeleton(skeleton, animation, transform, actor.headTurn.angles, bones))
        return Core::Unexpected(std::format("{:#x}: la pose del modelo {:#x} falló", address, actor.type));
    if (pass.faunaModels == nullptr)
        return Core::Unexpected(std::format("{:#x}: no fauna models (0x5769ad8) for model {:#x}", address, actor.type));
    Assets::FaunaModels& models = *pass.faunaModels;
    if (auto published = Actors::Drawing::PublishPoseRaw(carry, models, actor.type, bones, transform, true, false);
        !published.HasValue())
        return Core::Unexpected(published.Error());
    if (const auto key = PoseKeyOf(skeleton, animation))
        models.RecordAnimation(actor.type, animation.frame, *key);
    return speed;
}

// The strike window of 0x431470: the frame from `first` and before the speed plus `first` (FLD of the speed stored as a
// float, FADD and FCOMP with the frame on the x87). NaN does not enter.
bool InTheStrike(float frame, float speed, float first)
{
    return frame >= first && D(speed) + D(first) > D(frame);
}

// rand() % the model's bone count (MOVSX of +0x22).
Core::Expected<std::size_t, std::string> RandomBone(ModelEffectsPass& pass, std::uint32_t address)
{
    const auto count = static_cast<std::int16_t>(pass.skeleton.bones.size());
    if (count == 0)
        return Core::Unexpected(
            std::format("{:#x}: el modelo {:#x} no tiene huesos y el IDIV divide por 0", address, pass.actor.type));
    return static_cast<std::size_t>(pass.random.Next() % count);
}

Result ScrollU(ModelEffectsPass& pass, std::uint32_t address, std::int32_t modulus, float factor)
{
    const auto scroll = ClockScroll(pass, address, modulus, factor);
    if (!scroll.HasValue())
        return Core::Unexpected(scroll.Error());
    pass.actor.textureU = scroll.Value();
    return {};
}

Result ScrollV(ModelEffectsPass& pass, std::uint32_t address, std::int32_t modulus, float factor)
{
    const auto scroll = ClockScroll(pass, address, modulus, factor);
    if (!scroll.HasValue())
        return Core::Unexpected(scroll.Error());
    pass.actor.textureV = scroll.Value();
    return {};
}

// 0x507006..0x5073c1: type 0x185, fourteen 0x614/7 joints of width 14 between pairs of bones.
Result StepThe185(ModelEffectsPass& pass)
{
    constexpr std::array<std::pair<std::size_t, std::size_t>, 14> Pairs{{{2, 3},
                                                                         {3, 4},
                                                                         {4, 5},
                                                                         {2, 10},
                                                                         {10, 11},
                                                                         {2, 18},
                                                                         {18, 22},
                                                                         {22, 23},
                                                                         {23, 24},
                                                                         {24, 25},
                                                                         {18, 31},
                                                                         {31, 32},
                                                                         {32, 33},
                                                                         {33, 34}}};
    constexpr float Width = 14.0f;
    for (const auto& [first, second] : Pairs)
    {
        if (auto joined = Joint(pass, 0x50706f, 0x614, 7, BonePoint(pass.actor, first, NoOffset),
                                BonePoint(pass.actor, second, NoOffset), pass.actor.angles, Width);
            !joined.HasValue())
            return joined;
    }
    return {};
}

// 0x507414..0x50748e: type 0x182, with rand() % 5 at 0, the 0x5e3 ground mark at (rand() % 21 − 10) · 3.657f plus the x
// and the same in y, with the x angle (+0x1c), its own light and 0.65f. The z comes from [EBP−0x8], whatever the
// earlier code left there: 0x549130 does not read it (only x and y).
Result StepThe182(ModelEffectsPass& pass)
{
    if (Roll(pass, 5) != 0)
        return {};
    const Kernel::ActorState& actor = pass.actor;
    const double step = D(Bits(0x406a0ea1u)); // 3.657f (0x6ba1a8)
    const float x = F32(static_cast<double>(Roll(pass, 21) - 10) * step + D(actor.position.x));
    const float y = F32(static_cast<double>(Roll(pass, 21) - 10) * step + D(actor.position.y));
    const Terrain::GroundMarkRequest request{.type = 0x5e3,
                                             .position = {x, y, 0.0f},
                                             .angle = actor.angles.x,
                                             .color = actor.light,
                                             .size = Bits(0x3f266666u)};
    if (pass.groundMarks == nullptr)
        return Core::Unexpected(std::format("0x507486: no ground marks table (model {:#x})", actor.type));
    if (auto marked = Terrain::AddGroundMark(*pass.groundMarks, request, pass.random); !marked.HasValue())
        return Core::Unexpected(std::format("0x507486: {} (modelo {:#x})", marked.Error(), actor.type));
    return {};
}

// 0x5079c0..0x507a1b: type 0x177 raises +0x68 by 0.1 in actions 3 and 4, capped at 1, and lowers it by 0.1 in the
// others, floored at 0 (the FCOMP looks at the x87 sum, not the stored one; with NaN, 3 and 4 leave it and the others
// set it to 0).
void StepThe177(ModelEffectsPass& pass)
{
    Kernel::ActorState& actor = pass.actor;
    const std::uint8_t action = actor.animation.action;
    if (action >= 3 && action <= 4)
    {
        const double raised = D(actor.blendMeshLight) + D(Tenth);
        actor.blendMeshLight = F32(raised);
        if (raised > 1.0)
            actor.blendMeshLight = 1.0f;
        return;
    }
    const double lowered = D(actor.blendMeshLight) - D(Tenth);
    actor.blendMeshLight = F32(lowered);
    if (!(lowered >= 0.0))
        actor.blendMeshLight = 0.0f;
}

// 0x507a20..0x507ab2: type 0x16f, +0x64 at 3, the clock's V and, with rand() % 2 at 0, the 0x5d9/0 of bone 2.
Result StepThe16f(ModelEffectsPass& pass)
{
    pass.actor.dword64 = 3;
    if (auto scrolled = ScrollV(pass, 0x507a2d, 1000, Bits(0xba83126fu)); !scrolled.HasValue())
        return scrolled;
    if (Roll(pass, 2) != 0)
        return {};
    return Emit(pass, 0x507aaa, Particles::FireParticle, 0, BonePoint(pass.actor, 2, NoOffset), pass.actor.angles,
                White);
}

// 0x507b05..0x507da6: type 0x172. In actions 3, 4, 8 and 9, from the action's bone the 0x5ca/0 with (0.6, 1, 0.8) and
// the 0x5d9/0 with (1, 0.6, 1); in action 6 before frame 12 (or NaN), twenty 0x5d9/0 with (0.1, 0.8, 0.6) on bones of
// rand() % the bone count.
Result StepThe172(ModelEffectsPass& pass)
{
    Kernel::ActorState& actor = pass.actor;
    struct Strike
    {
        std::uint8_t action;
        std::size_t bone;
        std::uint32_t thunder;
        std::uint32_t fire;
    };
    constexpr std::array<Strike, 4> Strikes{{
        {3, 33, 0x507b71, 0x507ba2},
        {4, 20, 0x507beb, 0x507c1c},
        {8, 41, 0x507c65, 0x507c96},
        {9, 49, 0x507cde, 0x507d0f},
    }};
    const glm::vec3 spark{Bits(0x3f19999au), 1.0f, Bits(0x3f4ccccdu)};
    const glm::vec3 flame{1.0f, Bits(0x3f19999au), 1.0f};
    for (const Strike& strike : Strikes)
    {
        if (actor.animation.action != strike.action)
            continue;
        const glm::vec3 point = BonePoint(actor, strike.bone, NoOffset);
        if (auto emitted = Emit(pass, strike.thunder, Particles::ThunderParticle, 0, point, actor.angles, spark);
            !emitted.HasValue())
            return emitted;
        if (auto emitted = Emit(pass, strike.fire, Particles::FireParticle, 0, point, actor.angles, flame);
            !emitted.HasValue())
            return emitted;
    }
    if (actor.animation.action != 6 || actor.animation.frame >= 12.0f)
        return {};
    const glm::vec3 glow{Tenth, Bits(0x3f4ccccdu), Bits(0x3f19999au)};
    constexpr int Flames = 20;
    for (int burst = 0; burst < Flames; ++burst)
    {
        const auto bone = RandomBone(pass, 0x507d69);
        if (!bone.HasValue())
            return Core::Unexpected(bone.Error());
        if (auto emitted = Emit(pass, 0x507d9b, Particles::FireParticle, 0, BonePoint(actor, bone.Value(), NoOffset),
                                actor.angles, glow);
            !emitted.HasValue())
            return emitted;
    }
    return {};
}

// 0x507dab..0x507e5e: type 0x15d, +0x68 at (rand() % 10) · 0.1f; with the level (+0x1d6) at 2, ten 0x5d9/0 on rand()
// bones and the light (s, s · 0.2, 0) of radius 2.
Result StepThe15d(ModelEffectsPass& pass, float brightness)
{
    Kernel::ActorState& actor = pass.actor;
    actor.blendMeshLight = F32(static_cast<double>(Roll(pass, 10)) * D(Tenth));
    if (actor.level != 2)
        return {};
    constexpr int Flames = 10;
    for (int flame = 0; flame < Flames; ++flame)
    {
        const auto bone = RandomBone(pass, 0x507ded);
        if (!bone.HasValue())
            return Core::Unexpected(bone.Error());
        if (auto emitted = Emit(pass, 0x507e1f, Particles::FireParticle, 0, BonePoint(actor, bone.Value(), NoOffset),
                                actor.angles, White);
            !emitted.HasValue())
            return emitted;
    }
    return Light(pass, "0x507e56", {brightness, F32(D(brightness) * D(Bits(0x3e4ccccdu))), 0.0f}, 2);
}

// 0x507ee6..0x50808e: type 0x1ce. With +0x7c & 0x10000000, nothing. In action 0 with the frame in [5, 10], sound 0xa0;
// +0x68 at s, the light (s, s · 0.4, 0) of radius 3 and +0x64 at 4; in action 0 with the frame in [5, 6], from bone 17
// four 0x617/0 joints of width 10 with the angles (rand() % 60 + 150, 0, rand() % 30) and the white 0x5c5/0.
Result StepThe1ce(ModelEffectsPass& pass, float brightness)
{
    Kernel::ActorState& actor = pass.actor;
    if ((actor.flags7c & 0x10000000u) != 0)
        return {};
    const bool resting = actor.animation.action == 0;
    const float frame = actor.animation.frame;
    if (resting && frame >= 5.0f && frame <= 10.0f)
        Sound(pass, Audio::SoundId{0xa0});
    actor.blendMeshLight = brightness;
    if (auto lit = Light(pass, "0x507f74", {brightness, F32(D(brightness) * D(Bits(0x3ecccccdu))), 0.0f}, 3);
        !lit.HasValue())
        return lit;
    actor.dword64 = 4;
    if (!resting || !(frame >= 5.0f) || !(frame <= 6.0f))
        return {};
    const glm::vec3 point = BonePoint(actor, 17, NoOffset);
    constexpr int Sparks = 4;
    for (int spark = 0; spark < Sparks; ++spark)
    {
        const float pitch = static_cast<float>(Roll(pass, 60) + 150);
        const float turn = static_cast<float>(Roll(pass, 30));
        const glm::vec3 angles{pitch, 0.0f, turn};
        if (auto joined = Joint(pass, 0x508060, 0x617, 0, point, point, angles, 10.0f); !joined.HasValue())
            return joined;
        if (auto emitted = Emit(pass, 0x50807f, Particles::FairySparkParticle, 0, point, angles, White);
            !emitted.HasValue())
            return emitted;
    }
    return {};
}

// 0x508093..0x5081c4: type 0x1f5 in action 0: the light (s · 0.5, s · 0.3, 0) of radius 3 and, from bone 37 at (0, 5,
// 10), four 0x617/0 joints of width 10 with the angles (rand() % 60 + 90, 0, rand() % 30) and, with rand() % 2 at 1,
// the 0x5c5/0 with that light.
Result StepThe1f5(ModelEffectsPass& pass, float brightness)
{
    Kernel::ActorState& actor = pass.actor;
    if (actor.animation.action != 0)
        return {};
    const glm::vec3 glow{F32(D(brightness) * D(0.5f)), F32(D(brightness) * D(Bits(0x3e99999au))), 0.0f};
    if (auto lit = Light(pass, "0x5080fd", glow, 3); !lit.HasValue())
        return lit;
    const glm::vec3 point = BonePoint(actor, 37, {0.0f, 5.0f, 10.0f});
    constexpr int Sparks = 4;
    for (int spark = 0; spark < Sparks; ++spark)
    {
        const float pitch = static_cast<float>(Roll(pass, 60) + 90);
        const float turn = static_cast<float>(Roll(pass, 30));
        const glm::vec3 angles{pitch, 0.0f, turn};
        if (auto joined = Joint(pass, 0x508180, 0x617, 0, point, point, angles, 10.0f); !joined.HasValue())
            return joined;
        if (Roll(pass, 2) == 0)
            continue;
        if (auto emitted = Emit(pass, 0x5081b5, Particles::FairySparkParticle, 0, point, angles, glow);
            !emitted.HasValue())
            return emitted;
    }
    return {};
}

// 0x50826d..0x50839c and 0x50844c: types 0x155, 0x158 and 0x166 (and 0x154 after its own). 0x166, the 0x5ac sprite of
// bone 7 with (s, s · 0.4, s · 0.2), which stays in the light; while not dying (+0x389), with rand() % 4 at 0, three
// rand() around and, in Devias, the 0x5f2/0 or, otherwise, the 0x5f3/0 with that light.
Result StepThe155(ModelEffectsPass& pass, float brightness)
{
    Kernel::ActorState& actor = pass.actor;
    glm::vec3 light = White;
    if (actor.type == 0x166)
    {
        const glm::vec3 point = BonePoint(actor, 7, NoOffset);
        light = {brightness, F32(D(brightness) * D(Bits(0x3ecccccdu))), F32(D(brightness) * D(Bits(0x3e4ccccdu)))};
        if (pass.sprites == nullptr)
            return Core::Unexpected(std::format(
                "0x5082dd: faltan los sprites del frame para el modelo {:#x} (lugar {})", actor.type, pass.slot));
        static_cast<void>(pass.sprites->Create(0x5ac, point, 1.0f, light, 0.0f, 0));
    }
    if (actor.dying != 0 || Roll(pass, 4) != 0)
        return {};
    const glm::vec3 position = Scatter(pass);
    constexpr std::int32_t Devias = 2;
    if (pass.map == Devias)
        return Emit(pass, 0x508394, Particles::BreathParticle, 0, position, actor.angles, light);
    return Emit(pass, 0x508451, Particles::DustParticle, 0, position, actor.angles, light);
}

// 0x5081c9..0x508265: type 0x154 in action 3 with the frame at 4 or less (or NaN), the white 0x5d9/1 of bone 7 at (0,
// rand() % 32 + 32, 0); then what 0x155 does.
Result StepThe154(ModelEffectsPass& pass, float brightness)
{
    Kernel::ActorState& actor = pass.actor;
    if (actor.animation.action == 3 && !(actor.animation.frame > 4.0f))
    {
        const glm::vec3 offset{0.0f, static_cast<float>(Roll(pass, 32) + 32), 0.0f};
        if (auto emitted =
                Emit(pass, 0x508265, Particles::FireParticle, 1, BonePoint(actor, 7, offset), actor.angles, White);
            !emitted.HasValue())
            return emitted;
    }
    return StepThe155(pass, brightness);
}

// 0x5083a1..0x508459: type 0x173 while not dying and with the level at 1, with rand() % 4 at 0: three rand() around and
// the white 0x5f3/0.
Result StepThe173(ModelEffectsPass& pass)
{
    Kernel::ActorState& actor = pass.actor;
    if (actor.dying != 0 || actor.level != 1 || Roll(pass, 4) != 0)
        return {};
    const glm::vec3 position = Scatter(pass);
    return Emit(pass, 0x508451, Particles::DustParticle, 0, position, actor.angles, White);
}

// 0x50846c..0x5085d1: types 0x15e and 0x15f, with rand() % 4 at 0, the 0x5f2/0 of bone 22 with its own light.
Result StepThe15e(ModelEffectsPass& pass)
{
    if (Roll(pass, 4) != 0)
        return {};
    return Emit(pass, 0x5085d1, Particles::BreathParticle, 0, BonePoint(pass.actor, 22, NoOffset), pass.actor.angles,
                pass.actor.light);
}

// 0x5084b3..0x5085d1: type 0x152 with the frame in [15, 20] of action 0, in [20, 25] of action 1 or in [2, 3] or [5, 6]
// of action 2: with rand() % 2 at 0, the 0x5f2/0 of bone 24 at (0, −4, 0) with its own light.
Result StepThe152(ModelEffectsPass& pass)
{
    const Kernel::ActorState& actor = pass.actor;
    const float frame = actor.animation.frame;
    bool breath = false;
    switch (actor.animation.action)
    {
    case 0:
        breath = frame >= 15.0f && frame <= 20.0f;
        break;
    case 1:
        breath = frame >= 20.0f && frame <= 25.0f;
        break;
    case 2:
        breath = (frame >= 2.0f && frame <= 3.0f) || (frame >= 5.0f && frame <= 6.0f);
        break;
    default:
        break;
    }
    if (!breath || Roll(pass, 2) != 0)
        return {};
    return Emit(pass, 0x5085d1, Particles::BreathParticle, 0, BonePoint(actor, 24, {0.0f, -4.0f, 0.0f}), actor.angles,
                actor.light);
}

// 0x5078b5..0x507989: type 0x17c. With subtype 1, the white 0x5d9/0 of bones 6 and 13 and the light (−1.3) × 3 of
// radius 3; otherwise, 0x505d50 and 0x505c50.
Result StepThe17c(ModelEffectsPass& pass)
{
    Kernel::ActorState& actor = pass.actor;
    MarkTheBones(pass, 24, 25);
    if (actor.subtype != 1)
    {
        if (auto dust = RaiseTheDust(pass); !dust.HasValue())
            return dust;
        return BurstTheDust(pass);
    }
    for (const auto& [bone, address] : {std::pair<std::size_t, std::uint32_t>{6, 0x507907}, {13, 0x507943}})
    {
        if (auto emitted =
                Emit(pass, address, Particles::FireParticle, 0, BonePoint(actor, bone, NoOffset), actor.angles, White);
            !emitted.HasValue())
            return emitted;
    }
    const float dark = Bits(0xbfa66666u);
    return Light(pass, "0x507970", {dark, dark, dark}, 3);
}

// 0x53fe70(0x5de, point, angles, light, subtype, scale, 0): the flame with the world clock and its sine, which the /2
// and the /3 read when born (0x541722, 0x54178c). With the pool full it creates nothing and takes no rand().
Result Flame(ModelEffectsPass& pass, std::uint32_t address, std::int32_t subtype, const glm::vec3& position,
             const glm::vec3& angles, const glm::vec3& light, float scale)
{
    if (pass.particles == nullptr)
        return Core::Unexpected(std::format("{:#x}: faltan las partículas del modelo {:#x} (lugar {})", address,
                                            pass.actor.type, pass.slot));
    Particles::PortableParticleTrigonometry trigonometry;
    const auto created = pass.particles->CreateChecked(Particles::FlameParticle, position, angles, light, subtype,
                                                       scale, std::cref(pass.worldClock), pass.random, 0, trigonometry);
    if (!created.HasValue())
        return Core::Unexpected(std::format("{:#x}: la llama 0x5de/{} del modelo {:#x} (lugar {}) falla (error {})",
                                            address, subtype, pass.actor.type, pass.slot,
                                            static_cast<unsigned>(created.Error())));
    return {};
}

// A pair of the byte tables of 0x17e: the even byte is the flame's point and the destination of 0x4e9810; the odd one,
// where it looks from.
struct FlamePair
{
    std::uint8_t point;
    std::uint8_t from;
};

// 0x6c882c..0x6c884e: the 35 bones 0x5074f0 takes to the local array [EBP − 0x234] (MOVSX of each byte).
constexpr std::array<std::uint8_t, 35> FlameBones{5,  6,  33, 53, 35, 49, 50, 45, 46, 41, 42, 37,
                                                  38, 11, 31, 13, 27, 28, 23, 24, 19, 20, 15, 16,
                                                  54, 55, 62, 69, 70, 77, 2,  79, 81, 84, 86};
// 0x6c8850..0x6c886d and 0x6c8870..0x6c888d: the fifteen pairs of each table that the loop of 0x50752f reads two at a
// time.
constexpr std::array<FlamePair, 15> FirstFlamePairs{{{0, 2},
                                                     {2, 3},
                                                     {2, 4},
                                                     {4, 5},
                                                     {5, 6},
                                                     {4, 7},
                                                     {7, 8},
                                                     {4, 9},
                                                     {9, 10},
                                                     {4, 11},
                                                     {11, 12},
                                                     {6, 5},
                                                     {8, 7},
                                                     {10, 9},
                                                     {12, 11}}};
constexpr std::array<FlamePair, 15> SecondFlamePairs{{{0, 13},
                                                      {13, 14},
                                                      {13, 15},
                                                      {15, 16},
                                                      {16, 17},
                                                      {15, 18},
                                                      {18, 19},
                                                      {15, 20},
                                                      {20, 21},
                                                      {15, 22},
                                                      {22, 23},
                                                      {17, 16},
                                                      {19, 18},
                                                      {21, 20},
                                                      {23, 22}}};
// 0x6c8890..0x6c8897 and 0x6c8898..0x6c889f: the four pairs of each table of the loop of 0x5075f4.
constexpr std::array<FlamePair, 4> ThirdFlamePairs{{{29, 28}, {28, 27}, {34, 33}, {33, 30}}};
constexpr std::array<FlamePair, 4> FourthFlamePairs{{{26, 25}, {25, 24}, {32, 31}, {31, 30}}};

// 0x4e9810(from, angles, point, 360.0f) with the fstp of the distance to [EBP − 0x50], the light's x, and
// 0x53fe70(0x5de, point, angles, [EBP − 0x50], 2, scale, 0).
Result FlameAlong(ModelEffectsPass& pass, std::uint32_t address, const std::array<glm::vec3, FlameBones.size()>& points,
                  const FlamePair& pair, glm::vec3& angles, glm::vec3& light, float scale)
{
    const glm::vec3& point = points[pair.point];
    light.x = F32(Math::MoveToward(points[pair.from], angles, point, 360.0f));
    return Flame(pass, address, 2, point, angles, light, scale);
}

// 0x5074bb..0x50775c: type 0x17e with +0x376 at 0x3f (monster 63; 0x514bd1 also gives model 0x17e to 61, which goes
// through the other branch). The 35 points of the bones of 0x6c882c without offset ([EBP − 0x60] at 0); with the local
// angles at 0 (0x5074c1), one 0x5de/2 per pair of 0x6c8850 and of 0x6c8870 with scale 1.0f up to pair 11 (ESI from
// 0x16) and 0.5f after, and per pair of 0x6c8890 and of 0x6c8898 with 0.6f; with ftol(clock) even (the signed remainder
// of 0x5076b8), the 0x5de/2 of point 30 looked at from 0 with 1.3f and the 0x5de/3 of point 1 with 0.5f and the same
// light; at the end, the light (−1.3) × 3 of radius 3. The angles pass from one 0x4e9810 to the next. Nobody in
// 0x505de0 writes the light's y and z ([EBP − 0x4c] and [EBP − 0x48]): the /2 only reads the request's x (0x5416dc,
// 0x541702), the /3 does not read the request's light (0x541750..0x5417b2), and both overwrite all three with the
// clock's light (0x54173c..0x541748, 0x5417a6..0x5417b2), so MuRet leaves them at 0.
Result StepTheFlamingLimbs(ModelEffectsPass& pass)
{
    Kernel::ActorState& actor = pass.actor;
    std::array<glm::vec3, FlameBones.size()> points{};
    for (std::size_t index = 0; index < FlameBones.size(); ++index)
        points[index] = BonePoint(actor, FlameBones[index], NoOffset);
    glm::vec3 angles{0.0f, 0.0f, 0.0f};
    glm::vec3 light{0.0f, 0.0f, 0.0f};
    constexpr std::size_t SmallFrom = 0x16 / 2;
    for (std::size_t index = 0; index < FirstFlamePairs.size(); ++index)
    {
        const float scale = index < SmallFrom ? 1.0f : 0.5f;
        if (auto flamed = FlameAlong(pass, 0x50758a, points, FirstFlamePairs[index], angles, light, scale);
            !flamed.HasValue())
            return flamed;
        if (auto flamed = FlameAlong(pass, 0x5075de, points, SecondFlamePairs[index], angles, light, scale);
            !flamed.HasValue())
            return flamed;
    }
    const float tail = Bits(0x3f19999au); // 0.6f
    for (std::size_t index = 0; index < ThirdFlamePairs.size(); ++index)
    {
        if (auto flamed = FlameAlong(pass, 0x507644, points, ThirdFlamePairs[index], angles, light, tail);
            !flamed.HasValue())
            return flamed;
        if (auto flamed = FlameAlong(pass, 0x507699, points, FourthFlamePairs[index], angles, light, tail);
            !flamed.HasValue())
            return flamed;
    }
    const auto parity = ClockRemainder(pass, 0x5076b3, 2);
    if (!parity.HasValue())
        return Core::Unexpected(parity.Error());
    if (parity.Value() == 0)
    {
        if (auto flamed = FlameAlong(pass, 0x507702, points, FlamePair{30, 0}, angles, light, Bits(0x3fa66666u));
            !flamed.HasValue())
            return flamed;
        if (auto flamed = Flame(pass, 0x507724, 3, points[1], angles, light, 0.5f); !flamed.HasValue())
            return flamed;
    }
    const float dark = Bits(0xbfa66666u);
    return Light(pass, "0x507754", {dark, dark, dark}, 3);
}

// 0x507761..0x507897: type 0x17e without 0x3f, from the hands (bones 55 and 70 of 0x51255c). The light (L, L · 0.5, L ·
// 0.5) with L = sin(clock · 0.002f) · 0.3f + 0.7f (FSIN in PC53, fst of L and fmul 0.5 of the unrounded sum); with the
// local angles at 0, from bone 55 to 62 and from 70 to 77 (without offset: [EBP − 0x28] at 0 since 0x506c97), 0x4e9810
// of 360.0f, whose distance is dropped (fstp st(0)), and the 0x5de/1 at the second with 0.2f; then 0x505d50 and
// 0x505c50.
Result StepTheBurningHands(ModelEffectsPass& pass)
{
    Kernel::ActorState& actor = pass.actor;
    const double rate = D(Bits(0x3b03126fu)); // 0.002f (0x6b89bc)
    const auto sine = Math::Detail::SineProductPC53(D(pass.worldClock) * rate, Bits(0x3e99999au));
    if (!sine)
        return Core::Unexpected(std::string("0x50777b: el reloj del mundo está fuera del dominio de FSIN"));
    const double glow = *sine + D(Bits(0x3f333333u)); // 0.7f (0x6b8a5c)
    const glm::vec3 light{F32(glow), F32(glow * 0.5), F32(glow * 0.5)};
    glm::vec3 angles{0.0f, 0.0f, 0.0f};
    struct Hand
    {
        std::size_t from;
        std::size_t to;
        std::uint32_t address;
    };
    constexpr std::array<Hand, 2> Hands{{{55, 62, 0x50780d}, {70, 77, 0x507883}}};
    const float scale = Bits(0x3e4ccccdu); // 0.2f
    for (const Hand& hand : Hands)
    {
        const glm::vec3 start = BonePoint(actor, hand.from, NoOffset);
        const glm::vec3 point = BonePoint(actor, hand.to, NoOffset);
        static_cast<void>(Math::MoveToward(start, angles, point, 360.0f));
        if (auto flamed = Flame(pass, hand.address, 1, point, angles, light, scale); !flamed.HasValue())
            return flamed;
    }
    if (auto dust = RaiseTheDust(pass); !dust.HasValue())
        return dust;
    return BurstTheDust(pass);
}

// 0x50749c..0x507897: type 0x17e, 0x505bc0(8, 9) and the +0x376 branch.
Result StepThe17e(ModelEffectsPass& pass)
{
    MarkTheBones(pass, 8, 9);
    constexpr std::uint16_t Flaming = 0x3f;
    if (pass.actor.word376 == Flaming)
        return StepTheFlamingLimbs(pass);
    return StepTheBurningHands(pass);
}

// The case of the type − 0x152 (0x506ccc..0x50862f).
CaseResult StepTheTypeCase(ModelEffectsPass& pass, float brightness)
{
    Result stepped{};
    switch (pass.actor.type)
    {
    case 0x185:
        stepped = StepThe185(pass);
        break;
    case 0x186:
        stepped = ScrollU(pass, 0x5073cc, 10000, Bits(0xb9d1b717u));
        break;
    case 0x189:
        stepped = ScrollV(pass, 0x5073f3, 10000, Bits(0x38d1b717u));
        break;
    case 0x182:
        stepped = StepThe182(pass);
        break;
    case 0x17f:
    case 0x1aa:
    case 0x17d:
        MarkTheBones(pass, pass.actor.type == 0x17d ? 11 : 8, pass.actor.type == 0x17d ? 12 : 9);
        stepped = RaiseTheDust(pass);
        break;
    case 0x17e:
        stepped = StepThe17e(pass);
        break;
    case 0x17c:
        stepped = StepThe17c(pass);
        break;
    case 0x17b:
        MarkTheBones(pass, 8, 9);
        stepped = RaiseTheDust(pass);
        if (stepped.HasValue())
            stepped = BurstTheDust(pass);
        break;
    case 0x179:
        MarkTheBones(pass, 28, 27);
        break;
    case 0x177:
        StepThe177(pass);
        break;
    case 0x16f:
        stepped = StepThe16f(pass);
        break;
    case 0x16d:
        stepped = ScrollV(pass, 0x507abd, 1000, Bits(0xba83126fu));
        break;
    case 0x16c:
        stepped = ScrollU(pass, 0x507ae4, 10000, Bits(0xb8d1b717u));
        break;
    case 0x172:
        stepped = StepThe172(pass);
        break;
    case 0x15d:
        stepped = StepThe15d(pass, brightness);
        break;
    case 0x161:
        stepped = ScrollV(pass, 0x507e69, 2000, Bits(0xba03126fu));
        break;
    case 0x1d8:
        // 0x507e8a: rand() & 63 at 0, sound 0xa1.
        if (Roll(pass, 64) == 0)
            Sound(pass, Audio::SoundId{0xa1});
        break;
    case 0x1d3:
        // 0x507eb7: rand() & 255 at 0, sound 0xa0.
        if (Roll(pass, 256) == 0)
            Sound(pass, Audio::SoundId{0xa0});
        break;
    case 0x1ce:
        stepped = StepThe1ce(pass, brightness);
        break;
    case 0x1f5:
        stepped = StepThe1f5(pass, brightness);
        break;
    case 0x154:
        stepped = StepThe154(pass, brightness);
        break;
    case 0x155:
    case 0x158:
    case 0x166:
        stepped = StepThe155(pass, brightness);
        break;
    case 0x173:
        stepped = StepThe173(pass);
        break;
    case 0x157:
        stepped = BurstTheDust(pass);
        break;
    case 0x15e:
    case 0x15f:
        stepped = StepThe15e(pass);
        break;
    case 0x152:
        stepped = StepThe152(pass);
        break;
    case 0x183:
    case 0x184:
    case 0x188:
        // Directo a 0x50862f.
        break;
    default:
        return false;
    }
    if (!stepped.HasValue())
        return Core::Unexpected(stepped.Error());
    return true;
}

// 0x43bdd0(c, model), the table 0x43c07c over 0x43c064 from 0xc5. Returns 1 with its own.
CaseResult StepTheKalimaHelper(ModelEffectsPass& pass)
{
    Kernel::ActorState& actor = pass.actor;
    const double clock = D(pass.worldClock);
    const double rate = D(Bits(0x3a83126fu)); // 0.001f (0x6b89c8)
    switch (actor.type)
    {
    case 0xc5:
    {
        // 0x43bdfd..0x43bef3: in action 0, two 0x5f3/0 at rand() % 200 + x − 100 and rand() % 100 + y − 50 with its own
        // light and the 0xc8/0xc9 effects at the actor's position. Both branches reach +0x64 and the pulse of +0x68.
        if (actor.animation.action == 0)
        {
            const float x = F32(static_cast<double>(Roll(pass, 200)) + D(actor.position.x) - 100.0);
            const float y = F32(static_cast<double>(Roll(pass, 100)) + D(actor.position.y) - 50.0);
            const glm::vec3 position{x, y, actor.position.z};
            for (const std::uint32_t address : {0x43be6eu, 0x43be87u})
                if (auto emitted = Emit(pass, address, Particles::DustParticle, 0, position, actor.angles, actor.light);
                    !emitted.HasValue())
                    return Core::Unexpected(emitted.Error());
            // 0x43bea5 and 0x43bec6: the owner is 0; skipping a spawn with the pool full does not stop the second
            // attempt.
            for (const std::int16_t type : {std::int16_t{0xc8}, std::int16_t{0xc9}})
                static_cast<void>(pass.world.Map().effects.CreateStoneChip(type, actor.position, actor.angles,
                                                                           actor.light, pass.random));
        }
        actor.dword64 = 1;
        const auto pulse = Math::Detail::SineSumPC53(clock * rate, 1.0);
        if (!pulse)
            return Core::Unexpected(std::string("0x43bee5: el reloj del mundo está fuera del dominio de FSIN"));
        actor.blendMeshLight = F32(*pulse * 0.5);
        return true;
    }
    case 0x191:
    case 0x196:
    {
        // 0x43bf84: +0x68 at sin(T · 0.001f) · 0.7f + 0.3f.
        const auto pulse = Math::Detail::SineProductPC53(clock * rate, Bits(0x3f333333u));
        if (!pulse)
            return Core::Unexpected(std::string("0x43bf92: el reloj del mundo está fuera del dominio de FSIN"));
        actor.blendMeshLight = F32(*pulse + D(Bits(0x3e99999au)));
        return true;
    }
    case 0x192:
    case 0x197:
    {
        // 0x43befb and 0x43bfa8: 0x197 in action 6, nothing; otherwise, rand() % 2 at 0 requests from bone 6 (0x192) or
        // 31 (0x197) at (2, 30, 0) the 0x5f2/0x11 smoke (Particles::GlowSmokeSubtype).
        if (actor.type == 0x197 && actor.animation.action == 6)
            return true;
        if (Roll(pass, 2) != 0)
            return true;
        const bool tall = actor.type == 0x197;
        const glm::vec3 point = BonePoint(actor, tall ? 31 : 6, {2.0f, 30.0f, 0.0f});
        glm::vec3 light{1.0f, Bits(0x3e4ccccdu), 0.0f};
        if (tall)
            light = actor.subtype == 9 ? glm::vec3{1.0f, 0.0f, 0.0f} : glm::vec3{0.0f, Bits(0x3e99999au), 1.0f};
        if (auto emitted =
                Emit(pass, tall ? 0x43c04b : 0x43bf75, Particles::BreathParticle, 0x11, point, actor.angles, light);
            !emitted.HasValue())
            return Core::Unexpected(emitted.Error());
        return true;
    }
    case 0x193:
    case 0x194:
    case 0x195:
        // 0x43c053.
        return true;
    default:
        return false;
    }
}

// 0x42de90(c, model), only in map 30 ([0x6c9dd0]), the table 0x42df78 over 0x42df64 from 0x19b.
CaseResult StepTheCastleHelper(ModelEffectsPass& pass)
{
    constexpr std::int32_t CastleMap = Maps::CastleSiegeMap;
    if (pass.map != CastleMap)
        return false;
    Kernel::ActorState& actor = pass.actor;
    switch (actor.type)
    {
    case 0x19b:
    case 0x19d:
    case 0x19e:
    case 0x19f:
    case 0x1a0:
        // 0x42df47: the z angle (+0x24) at 0.
        actor.angles.z = 0.0f;
        return true;
    case 0x1a1:
        // 0x42df55: at 45.
        actor.angles.z = 45.0f;
        return true;
    case 0x19c:
    {
        // 0x42deeb..0x42df40: at 0 and the light (v · 0.2, v · 0.7, v) of radius 2, with v = sin(T · 0.0005) · 0.58f +
        // 0.42f (the clock times the double of 0x6b8aa8 on the x87).
        actor.angles.z = 0.0f;
        constexpr double Rate = 0.0005;
        const auto wave = Math::Detail::SineProductPC53(D(pass.worldClock) * Rate, Bits(0x3f147ae1u));
        if (!wave)
            return Core::Unexpected(std::string("0x42df0e: el reloj del mundo está fuera del dominio de FSIN"));
        const double glow = *wave + D(Bits(0x3ed70a3du));
        const glm::vec3 color{F32(glow * D(Bits(0x3e4ccccdu))), F32(glow * D(Bits(0x3f333333u))), F32(glow)};
        if (auto lit = Light(pass, "0x42df38", color, 2); !lit.HasValue())
            return Core::Unexpected(lit.Error());
        return true;
    }
    case 0x1fc:
    {
        // 0x42dec1..0x42dee4: +0x58 at −2; with the siege (0x4294b0) and [0x7239e4] at 0x20000, at −1. Returns 0.
        actor.dword58 = -2;
        const Kernel::WorldRuntime& runtime = pass.world.Runtime();
        constexpr std::uint32_t CrownOpen = 0x20000;
        if (runtime.castleFlags.siegeActive && runtime.castleFlags.castleFlags7239e4 == CrownOpen)
            actor.dword58 = -1;
        return false;
    }
    default:
        return false;
    }
}

// 0x431470 in actions 2 and 0xa: with rand() % 10 at 0, the 0x5f3/0 at the position with the case's light (0x431e86).
Result KickTheDust(ModelEffectsPass& pass, const glm::vec3* light, std::uint32_t local)
{
    const std::uint8_t action = pass.actor.animation.action;
    if (action != 2 && action != 0xa)
        return {};
    if (Roll(pass, 10) != 0)
        return {};
    if (pass.particles != nullptr &&
        std::ranges::all_of(pass.particles->All(), [](const Particles::Particle& particle) { return particle.alive; }))
        return {};
    if (light == nullptr)
        return Core::Unexpected(std::format("0x431e86: la luz del polvo 0x5f3/0 del modelo {:#x} (lugar {}) es "
                                            "[EBP−{:#x}], que ese camino de 0x431470 no escribe (comportamiento "
                                            "indefinido)",
                                            pass.actor.type, pass.slot, local));
    return Emit(pass, 0x431e86, Particles::DustParticle, 0, pass.actor.position, pass.actor.angles, *light);
}

Core::Expected<glm::vec3, std::string> CrywolfNamedPoint(ModelEffectsPass& pass, std::string_view name,
                                                         std::uint32_t address)
{
    const auto& bones = pass.world.Runtime().namedBones;
    const auto found = std::ranges::find_if(bones, [&](const Kernel::NamedBone& bone)
                                            { return bone.actor == pass.slot && bone.name == name; });
    if (found == bones.end())
        return Core::Unexpected(
            std::format("{:#x}: el hueso {} no existe y 0x4010b0 deja el punto sin escribir", address, name));
    return BonePoint(pass.actor, found->bone, NoOffset);
}

glm::vec3 CrywolfGlobalPoint(ModelEffectsPass& pass, std::size_t bone)
{
    const auto& bones = pass.world.PoseCarry().bones;
    const auto matrix = bone < bones.size() ? bones[bone] : Math::BoneTransform(0.0f);
    return Math::OriginalVectorTransform(NoOffset, Math::OriginalMatrixOf(matrix));
}

void CrywolfStone(ModelEffectsPass& pass, std::int16_t type, const glm::vec3& point)
{
    auto& effects = pass.world.Map().effects;
    if (type == 0xe5)
        static_cast<void>(effects.CreateGroundStoneChip(type, point, pass.actor.angles, pass.actor.light, pass.random));
    else
        static_cast<void>(effects.CreateStoneChip(type, point, pass.actor.angles, pass.actor.light, pass.random));
}

// 0x43192b..0x4319aa: the 0x636/2 of 0x518cf0 on bone 33, the 0x5ed/2 explosion of 0x53fe70 (white, scale 1, no owner;
// its spawn requests sound 0x40 at 0x542a82) and six 0xe5/0xa stones.
Result CrywolfHeavyStrike(ModelEffectsPass& pass)
{
    const glm::vec3 point = CrywolfGlobalPoint(pass, 33);
    static_cast<void>(pass.world.Map().effects.CreateCraterEffect(
        {.type = 0x636, .position = point, .angles = pass.actor.angles, .light = pass.actor.light, .subtype = 2}));
    if (pass.particles == nullptr)
        return Core::Unexpected(std::string("0x43197c: falta el pool de la explosión 5ed/2"));
    const auto burst =
        pass.particles->CreateChecked(0x5ed, point, pass.actor.angles, White, 2, 1, pass.worldClock, pass.random);
    if (!burst.HasValue())
        return Core::Unexpected(
            std::format("0x43197c: la explosión 5ed/2 falla con error {}", static_cast<unsigned>(burst.Error())));
    if (burst.Value())
        Sound(pass, Sound::ExplosionSound);
    for (int index = 0; index < 6; ++index)
        CrywolfStone(pass, 0xe5, point);
    return {};
}

Result CrywolfWaveStrike(ModelEffectsPass& pass)
{
    const glm::vec3 point = BonePoint(pass.actor, 27, NoOffset);
    auto& effects = pass.world.Map().effects;
    static_cast<void>(effects.CreateCraterEffect(
        {.type = 0x636, .position = point, .angles = pass.actor.angles, .light = pass.actor.light, .subtype = 2}));
    for (int index = 0; index < 2; ++index)
        if (auto emitted = Emit(pass, 0x4316f7, 0x640, 1, point, pass.actor.angles, White); !emitted.HasValue())
            return emitted;
    for (int index = 0; index < 5; ++index)
        static_cast<void>(effects.CreateCastleShockWave(
            {.type = 0x6eb, .position = point, .angles = pass.actor.angles, .light = {1, 0.6f, 0.4f}, .subtype = 8},
            pass.random));
    for (int index = 0; index < 4; ++index)
    {
        CrywolfStone(pass, 0xe5, point);
        CrywolfStone(pass, 0xc9, point);
    }
    return {};
}

Result CrywolfHeadSprite(ModelEffectsPass& pass, std::uint32_t address)
{
    const auto head = CrywolfNamedPoint(pass, "Monster95_Head", address);
    if (!head.HasValue())
        return Core::Unexpected(head.Error());
    if (pass.sprites == nullptr)
        return Core::Unexpected(std::string("0x431d84: faltan los sprites del monstruo95"));
    static_cast<void>(pass.sprites->Create(0x5ac, head.Value(), 3.5f, {0.9f, 0.2f, 0.1f}, 0, 0));
    return {};
}

Result CrywolfNamedSprites(ModelEffectsPass& pass, std::uint32_t centerAddress)
{
    Kernel::ActorState& actor = pass.actor;
    const float rotation = static_cast<float>(Roll(pass, 360));
    constexpr double Rate = static_cast<double>(std::bit_cast<float>(0x3a9d4952u));
    const auto sine = Math::Detail::SineProductPC53(D(pass.worldClock) * Rate, 0.8f);
    if (!sine)
        return Core::Unexpected(std::string("0x431f72: reloj fuera del dominio FSIN"));
    const float pulse = F32(*sine + D(Bits(0x3fa66666u)));
    const float scale = actor.animation.action == 3 || actor.animation.action == 4 ? 0.5f : 1.0f;
    const auto center = CrywolfNamedPoint(pass, "Monster96_Center", centerAddress);
    if (!center.HasValue())
        return Core::Unexpected(center.Error());
    if (pass.sprites == nullptr)
        return Core::Unexpected(std::string("0x43208e: faltan los sprites del monstruo96"));
    const glm::vec3 light{0, F32(D(pulse) * 0.5), F32(D(pulse) * D(Tenth))};
    static_cast<void>(pass.sprites->Create(0x5ac, center.Value(), scale, light, 0, 0));
    for (const std::string_view name : {"Monster96_Top", "Monster96_Bottom"})
    {
        const auto point = CrywolfNamedPoint(pass, name, 0x43216d);
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        static_cast<void>(
            pass.sprites->Create(0x5fd, point.Value(), F32(D(scale) * 0.5), glm::vec3(0.5f), rotation, 0));
        static_cast<void>(pass.sprites->Create(0x5fd, point.Value(), F32(D(scale) * 0.5), glm::vec3(0.5f),
                                               F32(360.0 - D(rotation)), 0));
    }
    return {};
}

Result CrywolfPiercingStrike(ModelEffectsPass& pass)
{
    // 0x431edc and 0x42f5e3 compute a turned speed in a local that is never read afterwards.
    static_cast<void>(pass.world.Map().effects.CreatePiercingEffect({.type = 0x115,
                                                                     .position = pass.actor.position,
                                                                     .angles = pass.actor.angles,
                                                                     .light = {1, 0, 0.5f},
                                                                     .subtype = 1}));
    return {};
}

// 0x431470(c, c, model), the table 0x432488 from 0x1ab. Returns 0 except 0x1b5 after its effect.
Result StepTheCrywolfHelper(ModelEffectsPass& pass)
{
    Kernel::ActorState& actor = pass.actor;
    const std::uint8_t action = actor.animation.action;
    const float frame = actor.animation.frame;
    const glm::vec3 ember{Bits(0x3f666666u), Bits(0x3e4ccccdu), Tenth};
    const std::uint8_t stage = pass.world.Runtime().crywolf.stage;
    constexpr std::uint8_t Siege = Protocol::Events::CrywolfStartStage;
    switch (actor.type)
    {
    case 0x1ab:
    {
        // 0x431868: 0x505bc0(9, 10), the pose and, in action 4 within [7.5, speed + 7.5), the 0x636 effects of
        // 0x518cf0.
        MarkTheBones(pass, 9, 10);
        const auto speed = PoseTheModel(pass, 0x4318bc);
        if (!speed.HasValue())
            return Core::Unexpected(speed.Error());
        if (action == 4)
        {
            if (InTheStrike(frame, speed.Value(), 7.5f))
                return CrywolfHeavyStrike(pass);
            return {};
        }
        return KickTheDust(pass, nullptr, 0x18);
    }
    case 0x1ad:
    case 0x1af:
        // 0x4319f1 and 0x431bdd: the light (0.9, 0.2, 0.1).
        return KickTheDust(pass, &ember, 0);
    case 0x1ae:
    {
        // 0x431a3c..0x431bd2: the notice is consumed before every spawn, also with the pools full.
        auto& flag = pass.world.Runtime().crywolf.ui.byte723bec;
        if (stage != Siege || flag == 0)
            return {};
        flag = 0;
        const glm::vec3 point = BonePoint(actor, 27, NoOffset);
        auto& effects = pass.world.Map().effects;
        const Kernel::ModelEffectArguments crater{
            .type = 0x636, .position = point, .angles = actor.angles, .light = actor.light, .subtype = 2};
        static_cast<void>(effects.CreateCraterEffect(crater));
        const glm::vec3 red{1, 0.2f, 0.2f};
        for (int index = 0; index < 2; ++index)
        {
            if (auto particle = Emit(pass, 0x431ae8, 0x640, 1, point, actor.angles, red); !particle.HasValue())
                return particle;
        }
        for (int index = 0; index < 5; ++index)
            static_cast<void>(
                effects.CreateCastleShockWave(Kernel::ModelEffectArguments{.type = Effects::ShockWaveEffect,
                                                                           .position = point,
                                                                           .angles = actor.angles,
                                                                           .light = red,
                                                                           .subtype = 8},
                                              pass.random));
        return {};
    }
    case 0x1b0:
    {
        // 0x43161d: in action 3 within [6.5, speed + 6.5), the 0x636 effects and the 0x640 particles.
        const auto speed = SpeedOf(pass, 0x431636);
        if (!speed.HasValue())
            return Core::Unexpected(speed.Error());
        if (action == 3)
        {
            if (InTheStrike(frame, speed.Value(), 6.5f))
                return CrywolfWaveStrike(pass);
            return {};
        }
        return KickTheDust(pass, &White, 0);
    }
    case 0x1b1:
    {
        if (action != 6)
        {
            if (auto head = CrywolfHeadSprite(pass, 0x431d5c); !head.HasValue())
                return head;
        }
        if (action == 3)
        {
            const auto speed = SpeedOf(pass, 0x431d9e);
            if (!speed.HasValue())
                return Core::Unexpected(speed.Error());
            if (InTheStrike(frame, speed.Value(), 2.5f))
            {
                const glm::vec3 point = BonePoint(actor, 20, {0, 0, 100});
                if (actor.scale > 1.25f)
                {
                    const auto created = pass.world.Map().effects.CreateEarthquake(
                        point, actor.angles, White, 1, {.type = &actor.type, .angles = &actor.angles},
                        pass.world.Runtime().ownRecordDepth, pass.world.OwnCharacter().bytef04, pass.random);
                    if (created)
                        pass.world.Map().effects.Effects()[*created].ownerContext = &actor;
                }
            }
            return {};
        }
        return KickTheDust(pass, &White, 0);
    }
    case 0x1b2:
        return CrywolfNamedSprites(pass, 0x431fa4);
    case 0x1b3:
        // 0x431ea1: in action 4 with the frame in [4, 6], effect 0x115.
        if (action == 4 && frame >= 4.0f && frame <= 6.0f)
            return CrywolfPiercingStrike(pass);
        return {};
    case 0x1b4:
    {
        // 0x4314ad: the pose and, in action 4 within [5.5, speed + 5.5), twelve 0xc9 effects.
        const auto speed = PoseTheModel(pass, 0x4314f3);
        if (!speed.HasValue())
            return Core::Unexpected(speed.Error());
        if (action == 4)
        {
            if (InTheStrike(frame, speed.Value(), 5.5f))
            {
                const glm::vec3 first = CrywolfGlobalPoint(pass, 24);
                const glm::vec3 second = CrywolfGlobalPoint(pass, 16);
                for (int index = 0; index < 6; ++index)
                {
                    CrywolfStone(pass, 0xc9, first);
                    CrywolfStone(pass, 0xc9, second);
                }
            }
            return {};
        }
        return KickTheDust(pass, nullptr, 0x24);
    }
    case 0x1b5:
    {
        // 0x4323d2: with the Crywolf stage at 4, rand() % 100 at 0 sets action 3 (0x4ea030) and +0x84 to 1; in action 3
        // with +0x84 at 1 from frame 5, effect 0xed.
        if (stage != Siege)
            return {};
        if (Roll(pass, 100) == 0)
        {
            Models::SetAction(actor.animation, pass.skeleton, 3);
            actor.movementPhase = 1.0f;
        }
        if (actor.animation.action == 3 && std::bit_cast<std::uint32_t>(actor.movementPhase) == 0x3f800000u &&
            actor.animation.frame >= 5.0f)
        {
            auto created = Effects::CreateTheArrowEffect(
                Kernel::ModelEffectArguments{.type = 0xed,
                                             .position = actor.position,
                                             .angles = actor.angles,
                                             .light = actor.light,
                                             .subtype = 1,
                                             .owner = &actor,
                                             .key = static_cast<std::uint16_t>(actor.word8a)},
                pass.world, pass.random);
            if (!created.HasValue())
                return created;
            actor.movementPhase = 0.0f;
        }
        return {};
    }
    default:
        // 0x4315e0: 0x1ac and those outside the table.
        return {};
    }
}

// 0x42f550: the map 35 helper ends with 0, so 0x431470 also runs afterwards.
Result StepTheKanturuHelper(ModelEffectsPass& pass)
{
    if (pass.map != 35)
        return {};
    const auto action = pass.actor.animation.action;
    const float frame = pass.actor.animation.frame;
    switch (pass.actor.type)
    {
    case 0x1b3:
        if (action == 4 && frame >= 4 && frame <= 6)
            return CrywolfPiercingStrike(pass);
        return {};
    case 0x1b2:
        return CrywolfNamedSprites(pass, 0x42f6ce);
    case 0x1b1:
        if (action != 6)
        {
            if (auto head = CrywolfHeadSprite(pass, 0x42f946); !head.HasValue())
                return head;
        }
        {
            const glm::vec3 ember{0.9f, 0.2f, 0.1f};
            return KickTheDust(pass, &ember, 0);
        }
    default:
        return {};
    }
}

// 0x426ce0(c, model): the radius 3 light of 0x1b6, 0x1b7 and 0x1bb. Returns 0.
Result StepTheLightHelper(ModelEffectsPass& pass)
{
    switch (pass.actor.type)
    {
    case 0x1b6:
        return Light(pass, "0x426d7f", {Bits(0x3f333333u), Tenth, Tenth}, 3);
    case 0x1b7:
        return Light(pass, "0x426d7f", {0.0f, 0.0f, Bits(0x3f333333u)}, 3);
    case 0x1bb:
        return Light(pass, "0x426d28", {1.0f, 0.0f, 0.0f}, 3);
    default:
        return {};
    }
}

// 0x5085db: the six, up to the first that returns 1.
Result StepTheHelpers(ModelEffectsPass& pass)
{
    const auto kalima = StepTheKalimaHelper(pass);
    if (!kalima.HasValue())
        return Core::Unexpected(kalima.Error());
    if (kalima.Value())
        return {};
    const auto castle = StepTheCastleHelper(pass);
    if (!castle.HasValue())
        return Core::Unexpected(castle.Error());
    if (castle.Value())
        return {};
    // 0x441210: type 0x1a4, the light (1, 0.2, 0.1) of radius 3. Returns 0.
    if (pass.actor.type == 0x1a4)
        if (auto lit = Light(pass, "0x441249", {1.0f, Bits(0x3e4ccccdu), Tenth}, 3); !lit.HasValue())
            return lit;
    if (auto kanturu = StepTheKanturuHelper(pass); !kanturu.HasValue())
        return kanturu;
    if (auto crywolf = StepTheCrywolfHelper(pass); !crywolf.HasValue())
        return crywolf;
    return StepTheLightHelper(pass);
}
} // namespace

Core::Expected<void, std::string> StepTheModelEffects(ModelEffectsPass& pass)
{
    // 0x506c82..0x506cc6: the local light at (1, 1, 1), the offset at 0 and s = ((rand() & 7) + 2) · 0.1f.
    const float brightness = F32(static_cast<double>(pass.random.Next() % 8 + 2) * D(Tenth));
    const auto handled = StepTheTypeCase(pass, brightness);
    if (!handled.HasValue())
        return Core::Unexpected(handled.Error());
    if (handled.Value())
        return {};
    return StepTheHelpers(pass);
}
} // namespace Client::Monsters
