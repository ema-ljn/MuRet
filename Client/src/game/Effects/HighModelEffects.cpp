#include "Effects/HighModelEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/ModelEffectArguments.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Models/Animation.h"
#include "Math/HalfLifeMath.h"
#include "Models/IModelLibrary.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Actors/CharacterStep.h"
#include "Assets/FaunaModels.h"
#include "Effects/GuildMarkEffects.h"
#include "Effects/FireBreathEffect.h"
#include "Effects/HighEffectEmitters.h"
#include "Effects/JavelinEffect.h"
#include "Effects/KalimaMonsterEffects.h"
#include "Effects/KundunEffects.h"
#include "Effects/KundunFallEffects.h"
#include "Effects/MeteorEffects.h"
#include "Effects/RingEffects.h"
#include "Effects/SkillBurstEffects.h"
#include "Effects/SkullEffects.h"
#include "Actors/SkillHits.h"
#include "Effects/WaveEffects.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Effects/GameModelEffects.h"
#include "Effects/ColoredDecals.h"
#include "Terrain/WorldTerrain.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "CastleSiege/SiegeStoneEffects.h"
#include "Terrain/DynamicWorldLight.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"
#include "CastleSiege/CastleSiegeState.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

// The creations (0x518cf0) and steps (0x5208a0) of the high types, checked against the PE c32bbd5f: 06, "Los efectos
// con modelo de los tipos altos".
namespace Client::Effects
{
namespace
{
using Crt::CrtRandom;

// The prefix of the siege effects (0x42d070, 0x42dcf0) and of Crywolf's (0x4324c0, 0x4f5b70).
constexpr std::int16_t SiegeAuraEffect = 0x134;     // the siege arrival's aura (0x42d513, 0x42d5af)
constexpr std::int16_t SiegeFlareEffect = 0x13b;    // the flame dropped by the pulse 0x61c and by 0x4fd5f0
constexpr std::int16_t SiegeSpearmanEffect = 0x19f; // the spearman's model (Monster78), not drawn: drops the 0x12b/2
constexpr std::int16_t SiegePulseEffect = 0x61c;    // not drawn: the siege pulse with its joints
constexpr std::int16_t RadialShardEffect = 0xc1a;   // the shard of Crywolf's radial discharge
// The growing flash of 0x429110 (0x429202). 0x615700 (the skills' models) does not load it: 0x616ec0 loads it only in
// map 0x21 (0x618592 and 0x6185ad, Object34\tree_eff; WorldSpecialModels.cpp), so outside that map the draw does not
// find its BMD.
constexpr std::int16_t GrowingFlashEffect = 0x141;
constexpr std::uint8_t NoAction = 0xff;
// The light rays drawn as sprites along their speed (0x52d941): they do not move and have no step.
constexpr std::int16_t MovingLightFirst = 0x5e8, MovingLightSecond = 0x5e9, MovingLightLast = 0x5ea;
// Skill\PierPart (skills 0x3d and 0x4a of 0x4fd5f0), Skill\DarkLordSkill (the hands of 0x40, 0x4b and 0x3d) and
// Skill\ManaRune (0x4b).
constexpr std::int16_t PierPartEffect = 0x116, DarkLordSkillEffect = 0x117, ManaRuneEffect = 0x138;
// No model or draw: they drop joints in their step. The 0x812, that of skills 0x2b and 0x2f (0x4f97b0); the 0x813, that
// of 0x2f's hand.
constexpr std::int16_t SparkEmitterEffect = 0x812, BoltEmitterEffect = 0x813;
// The piercing shot's glow (0x621): the /0 is born and steps in ModelEffects::CreatePiercingShot and
// StepGameModelEffect; the other subtypes, here.
constexpr std::int16_t PiercingGlowEffect = Effects::PiercingShotGlowEffect;
// The ones that fall on the target of 0x46 (+0x376) in its step 5 (0x4f5cb6): not drawn, only their fire and sprites.
constexpr std::int16_t FallingStarEffect = 0x625;

using Math::D;
using Math::F32;

// 0x5fc8f0 and 0x5fcbd0: the vector rotated by the original's angle matrix (in degrees).
glm::vec3 Rotated(const glm::vec3& vector, const glm::vec3& angles)
{
    return Math::OriginalVectorRotate(vector, Math::OriginalAngleMatrix(angles));
}

// One fld/fadd/fstp per axis.
glm::vec3 Sum(const glm::vec3& a, const glm::vec3& b)
{
    return {F32(D(a.x) + D(b.x)), F32(D(a.y) + D(b.y)), F32(D(a.z) + D(b.z))};
}

// The global table's BMD (0x5769ad8 + type · 0xe0): the one in `faunaModels`, which follows the map's models, or else
// the one in `models` (the items and Player.bmd), as MonsterSkeletonOf.
const Models::Skeleton* ModelSkeletonOf(const Assets::FaunaModels* faunaModels, const Models::IModelLibrary* models,
                                        std::uint16_t model)
{
    const auto* skeleton = Assets::FaunaModelsOrEmpty(faunaModels).SkeletonOf(Models::ModelId{model});
    if (skeleton == nullptr && models != nullptr)
        skeleton = models->SkeletonOf(Models::ModelId{model});
    return skeleton;
}

// 0x51d714..0x51d73a, the 0x134 (the siege arrival, 0x42d513 and 0x42d5af): life 100, the key's scale (+0x8a, MOVSX)
// times 0.01f and +0x64 and +0x68 at 0. Any subtype; no rand().
void BeginTheSiegeAura(Effects::ModelEffect& effect)
{
    constexpr std::int32_t AuraLife = 100;
    constexpr double ScalePerKey = static_cast<double>(std::bit_cast<float>(0x3c23d70au)); // 0x6b86c0, 0.01f
    effect.life = AuraLife;
    effect.transform.scale =
        F32(static_cast<double>(std::bit_cast<std::int16_t>(effect.disturbanceState)) * ScalePerKey);
    effect.objectState.blendMesh = 0;
    effect.objectState.light = 0.0f;
}

// 0x51e4ba..0x51e4f5, the 0x19f with subtype 0 (0x42dd9c): life 20, +0x58 at 3, +0xd8 at 0.33f, 0x4ea030(effect, 2) and
// the angles at 0. With another subtype it exits through 0x51db73 with what the prefix set. 0x4ea030 sets action 2 if
// model 0x19f (Monster78) has more than two actions (+0x26) and the action is not already 2.
void BeginTheSiegeSpearman(Effects::ModelEffect& effect, const HighEffectContext& context)
{
    if (effect.subtype != 0)
        return;
    constexpr std::int32_t SpearmanLife = 20;
    constexpr std::int16_t SpearmanHidden = 3;
    constexpr float SpearmanSpeed = std::bit_cast<float>(0x3ea8f5c3u); // 0.33f
    constexpr std::uint8_t SpearmanAction = 2;
    effect.life = SpearmanLife;
    effect.objectState.hiddenMesh = SpearmanHidden;
    effect.speed = SpearmanSpeed;
    // 0x4ea030 reads the global table's BMD (0x5769ad8 + type · 0xe0). Without the BMD loaded, the original's action
    // count is 0 and it does nothing.
    const auto* skeleton =
        ModelSkeletonOf(context.faunaModels, context.models, static_cast<std::uint16_t>(effect.type));
    if (skeleton != nullptr)
        Models::SetAction(effect.animation, *skeleton, SpearmanAction);
    effect.transform.angles = glm::vec3(0.0f);
}

// 0x51f154..0x51f25d, the 0x61c (0x42dd48 with subtype 0, 0x42925a with 1): life 20, the speed (0, −550, 0) with 0 or
// (0, −250, 0) with 1, the angles at (−90, 0, z of the argument), +0x28 and +0x2c at 0, +0x30 at rand() % 360 and the
// start at the position. The /0, between the rand() and the start, the joint 0x61c/3 at the position (0x51f1c5).
// Another subtype exits through 0x51db73 with what the prefix set.
Core::Expected<void, std::string> BeginTheSiegePulse(Effects::ModelEffect& effect,
                                                     const Kernel::ModelEffectArguments& arguments,
                                                     const HighEffectContext& context)
{
    constexpr std::int32_t PulseLife = 20;
    constexpr float FirstRise = -550.0f, SecondRise = -250.0f, Pitch = -90.0f; // 0xc4098000, 0xc37a0000, 0xc2b40000
    constexpr int FullTurn = Math::FullTurn;
    if (effect.subtype != 0 && effect.subtype != 1)
        return {};
    effect.life = PulseLife;
    effect.velocity = {0.0f, effect.subtype == 0 ? FirstRise : SecondRise, 0.0f};
    effect.transform.angles = {Pitch, 0.0f, arguments.angles.z};
    effect.copiedAngles.x = 0.0f;
    effect.copiedAngles.y = 0.0f;
    effect.copiedAngles.z = static_cast<float>(context.random.Next() % FullTurn);
    if (effect.subtype == 0)
    {
        constexpr std::int32_t PulseJoint = 0x61c, PulseJointSubtype = 3;
        constexpr float PulseJointScale = 80.0f; // 0x42a00000
        if (auto joint = CreateHighJoint(context.world, context.random, PulseJoint, effect.transform.position,
                                         effect.transform.position, effect.transform.angles, PulseJointSubtype, nullptr,
                                         PulseJointScale, 0x51f1c5);
            !joint.HasValue())
            return joint;
    }
    effect.start = arguments.position;
    return {};
}

// 0x51e059..0x51e0ca, the 0x13b: +0xd8 at 0.25f. The /0 (0x502975): life 15, scale at 0, z 100 higher, speed
// (0, −10, 0). The /1 (the one the pulse 0x61c drops, 0x52c39c): life 5 and scale 3.5. All: +0x64 at 0 and +0x68 at 1;
// another subtype keeps the previous life and scale. No rand().
void BeginTheSiegeFlare(Effects::ModelEffect& effect)
{
    constexpr float FlareSpeed = 0.25f;
    effect.speed = FlareSpeed;
    if (effect.subtype == 0)
    {
        constexpr std::int32_t FlareLife = 15;
        constexpr double FlareLift = 100.0;
        constexpr float FlareRise = -10.0f;
        effect.life = FlareLife;
        effect.transform.scale = 0.0f;
        effect.transform.position.z = F32(D(effect.transform.position.z) + FlareLift);
        effect.velocity = {0.0f, FlareRise, 0.0f};
    }
    else if (effect.subtype == 1)
    {
        constexpr std::int32_t SparkLife = 5;
        constexpr float SparkScale = 3.5f; // 0x40600000
        effect.life = SparkLife;
        effect.transform.scale = SparkScale;
    }
    effect.objectState.blendMesh = 0;
    effect.objectState.light = 1.0f;
}

// 0x51f85a..0x51f8a7, the 0xc1a (0x4329db and 0x4f6780): life 30, +0x64 at −2, scale 1, z 280 higher, the angles' x 20
// more and speed (0, −80, −10). Any subtype; no rand().
void BeginTheRadialShard(Effects::ModelEffect& effect)
{
    constexpr std::int32_t ShardLife = 30;
    constexpr double ShardLift = 280.0, ShardPitch = 20.0;
    effect.life = ShardLife;
    effect.objectState.blendMesh = -2;
    effect.transform.scale = 1.0f;
    effect.transform.position.z = F32(D(effect.transform.position.z) + ShardLift);
    effect.transform.angles.x = F32(D(effect.transform.angles.x) + ShardPitch);
    effect.velocity = {0.0f, -80.0f, -10.0f};
}

// 0x51c410..0x51c437, the 0x141: z 20 higher, life 20, +0xe8 at 0 and scale at 0.05f. No rand().
void BeginTheGrowingFlash(Effects::ModelEffect& effect)
{
    constexpr double FlashLift = 20.0; // 0x6b86d0
    constexpr std::int32_t FlashLife = 20;
    constexpr float FlashScale = std::bit_cast<float>(0x3d4ccccdu); // 0.05f
    effect.transform.position.z = F32(D(effect.transform.position.z) + FlashLift);
    effect.life = FlashLife;
    effect.vertexLight = false;
    effect.transform.scale = FlashScale;
}

// 0x51e596..0x51e772, the 0x5e8 (the blue ray; with subtype 1, white and of scale 3), the 0x5e9 and the 0x5ea (the
// orange ones): life 20, the light, the scale and the speed (0, −50, 0) (the 0x5ea, (0, −15, 0)) rotated by the angles.
// With the hero ([0x743a170]) as owner, four times: the point 150 ahead (rotated) and 0x517a20(+0x89, point,
// 150, +0x8c, +0x8a, 0), the targets around and the C1 10. No rand() of their own (0x517a20's, yes).
Core::Expected<void, std::string> BeginTheMovingLight(Effects::ModelEffect& effect, const HighEffectContext& context)
{
    constexpr std::int32_t LightLife = 20;
    constexpr float Orange = std::bit_cast<float>(0x3ecccccdu), Ember = std::bit_cast<float>(0x3e4ccccdu);
    constexpr float BlueGreen = std::bit_cast<float>(0x3f333333u);
    constexpr float WideScale = 16.0f, NarrowScale = 5.0f, WhiteScale = 3.0f; // 0x41800000, 0x40a00000, 0x40400000
    constexpr float Rush = -50.0f, SlowRush = -15.0f;                         // 0xc2480000, 0xc1700000
    constexpr std::int32_t WhiteSubtype = 1;
    effect.life = LightLife;
    glm::vec3 rush{0.0f, Rush, 0.0f};
    if (effect.type == MovingLightLast)
    {
        effect.light = {1.0f, Orange, Ember};
        rush.y = SlowRush;
        effect.transform.scale = NarrowScale;
    }
    else if (effect.type == MovingLightSecond)
    {
        effect.light = {1.0f, Orange, Ember};
        effect.transform.scale = WideScale;
    }
    else
    {
        effect.light = {0.5f, BlueGreen, 1.0f};
        effect.transform.scale = WideScale;
        if (effect.subtype == WhiteSubtype)
        {
            effect.transform.scale = WhiteScale;
            effect.light = glm::vec3(1.0f);
        }
    }
    effect.velocity = Rotated(rush, effect.transform.angles);
    Kernel::WorldState& world = context.world;
    if (effect.ownerContext != &world.Hero())
        return {};
    constexpr float Reach = -150.0f; // 0xc3160000
    constexpr float HitRadius = 150.0f;
    constexpr int Hits = 4;
    const glm::vec3 step = Rotated({0.0f, Reach, 0.0f}, effect.transform.angles);
    glm::vec3 point = effect.transform.position;
    for (int hit = 0; hit < Hits; ++hit)
    {
        point = Sum(step, point);
        const Actors::SkillHitsRequest request{.skillIndex = effect.byte89,
                                               .point = point,
                                               .radius = HitRadius,
                                               .volley = static_cast<std::uint8_t>(effect.word8c & 0xff),
                                               .key = effect.disturbanceState,
                                               .extra = 0};
        if (auto hits = Actors::HitTheTargetsAround(request, world, Terrain::TerrainOrEmpty(context.terrain),
                                                    context.random, context.send);
            !hits.HasValue())
            return Core::Unexpected(std::format("0x51e75b: {}", hits.Error()));
    }
    return {};
}

// 0x51f048..0x51f153, the 0x625 (the twenty that fall on 0x4f5cb6's target): life rand() % 15 + 15, +0xe4 (the rise per
// step) at −20, +0xd8 at rand() % 360 and the light at 0. The /1 sets the scale to 0.5f, lowers +0xe4 by
// rand() % 20 + 10 more (fsubr) and spreads over 300; the others, scale at 0 and spread over 200. The x and the y plus
// rand() % spread minus half (fild, fadd, fsub on the x87), z 500 higher and x 100 more, and the start (+0x17c) at the
// position. It ends with the sound 0x41ed10(0x42, 0, 0). Four rand() (five with the /1): the life, +0xd8, [+0xe4], the
// x and y.
void BeginTheFallingStar(Effects::ModelEffect& effect, const HighEffectContext& context)
{
    constexpr int LifeSpan = 15, LifeBase = 15, FullTurn = Math::FullTurn;
    constexpr float InitialRise = -20.0f; // 0xc1a00000
    constexpr std::int32_t WideSubtype = 1;
    constexpr int WideSpread = 300, NarrowSpread = 200;    // 0x12c and 0xc8
    constexpr double WideHalf = 150.0, NarrowHalf = 100.0; // 0x96 and 0x64, via fild and fstp
    constexpr float WideScale = 0.5f;                      // 0x3f000000
    constexpr int ExtraFallSpan = 20, ExtraFallBase = 10;  // 0x14 and 0xa
    constexpr double Height = 500.0, Lead = 100.0;         // 0x6b8a98 and 0x6b86b4
    constexpr Audio::SoundId FallingSound = Sound::MeteoriteSound;
    CrtRandom& random = context.random;
    effect.life = random.Next() % LifeSpan + LifeBase;
    effect.turnDirection = InitialRise;
    effect.speed = static_cast<float>(random.Next() % FullTurn);
    effect.light = glm::vec3(0.0f);
    int spread = NarrowSpread;
    double half = NarrowHalf;
    if (effect.subtype == WideSubtype)
    {
        spread = WideSpread;
        half = WideHalf;
        effect.transform.scale = WideScale;
        const int extra = random.Next() % ExtraFallSpan + ExtraFallBase;
        effect.turnDirection = F32(D(effect.turnDirection) - static_cast<double>(extra));
    }
    else
    {
        effect.transform.scale = 0.0f;
    }
    glm::vec3& position = effect.transform.position;
    position.x = F32(static_cast<double>(random.Next() % spread) + D(position.x) - half);
    position.y = F32(static_cast<double>(random.Next() % spread) + D(position.y) - half);
    position.z = F32(D(position.z) + Height);
    position.x = F32(D(position.x) + Lead);
    effect.start = position;
    context.world.Runtime().RequestSound(FallingSound);
}

// 0x51ca31..0x51cb46, the 0x116 (Skill\PierPart). The /0 (the three of skill 0x3d, 0x504e40): life 20, +0xe4 (how many
// /1 it drops per step) at 2, +0xd8 (the turn) at 10, +0x58 at 1, scale 1.2f, light at 1, speed (0, −26, 0), the start
// (+0x17c) at the light it received (the prefix's ecx: the owner's point it heads to), +0x28..+0x30 at the angles and
// the angles at (0, 0, z of the argument). The /1 (dropped by the /0's step, with it as owner): the owner's life
// (+0x108 → +0x60), +0x58 at 0, scale 0.5f, alpha (20 − life) · 0.2f, speed at 0 and the flame 0x5da/0 (0x53fe70 with
// scale 1 and the effect as owner). The /2 (skill 0x4a, 0x504b80): life 20, +0xd8 at 50, +0x58 at −2, z 20 lower, speed
// (0, −40, 0), +0x28..+0x30 at the angles and the angles at (0, 0, z). Another subtype, only the prefix. No rand() (the
// flame's, yes).
Core::Expected<void, std::string> BeginThePierPart(Effects::ModelEffect& effect,
                                                   const Kernel::ModelEffectArguments& arguments,
                                                   const HighEffectContext& context)
{
    constexpr std::int32_t PartLife = 20;
    if (effect.subtype == 0)
    {
        constexpr float Bursts = 2.0f, Turn = 10.0f, Rush = -26.0f; // 0x40000000, 0x41200000 and 0xc1d00000
        constexpr std::int16_t PartHidden = 1;
        constexpr float PartScale = std::bit_cast<float>(0x3f99999au); // 1.2f
        effect.life = PartLife;
        effect.turnDirection = Bursts;
        effect.speed = Turn;
        effect.objectState.hiddenMesh = PartHidden;
        effect.transform.scale = PartScale;
        effect.light = glm::vec3(1.0f);
        effect.velocity = {0.0f, Rush, 0.0f};
        effect.start = arguments.light;
        effect.copiedAngles = effect.transform.angles;
        effect.transform.angles = {0.0f, 0.0f, arguments.angles.z};
        return {};
    }
    if (effect.subtype == 1)
    {
        const auto* parent = static_cast<const Effects::ModelEffect*>(effect.ownerContext);
        if (parent == nullptr)
            return Core::Unexpected(std::string("0x51cad9: el 0x116/1 lee la vida de un dueño (+0x108) nulo"));
        constexpr float TrailScale = 0.5f;                                                  // 0x3f000000
        constexpr double FadeStep = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)); // 0x6b8cfc, 0.2f
        effect.life = parent->life;
        effect.objectState.hiddenMesh = 0;
        effect.transform.scale = TrailScale;
        effect.alpha = F32(static_cast<double>(PartLife - effect.life) * FadeStep);
        effect.velocity = glm::vec3(0.0f);
        constexpr std::int32_t TrailFire = Particles::MissileFireParticle, TrailFireSubtype = 0;
        if (auto fire = CreateHighParticle(context.world, context.random, context.worldClock, TrailFire,
                                           effect.transform.position, effect.transform.angles, effect.light,
                                           TrailFireSubtype, 1.0f, 0x51cb38);
            !fire.HasValue())
            return Core::Unexpected(fire.Error());
        return {};
    }
    if (effect.subtype == 2)
    {
        constexpr float Turn = 50.0f, Rush = -40.0f; // 0x42480000 and 0xc2200000
        constexpr std::int16_t PartHidden = -2;
        constexpr double Drop = 20.0; // 0x6b86d0
        effect.life = PartLife;
        effect.speed = Turn;
        effect.objectState.hiddenMesh = PartHidden;
        effect.transform.position.z = F32(D(effect.transform.position.z) - Drop);
        effect.velocity = {0.0f, Rush, 0.0f};
        effect.copiedAngles = effect.transform.angles;
        effect.transform.angles = {0.0f, 0.0f, arguments.angles.z};
    }
    return {};
}

// 0x51cbb2..0x51cc6a, the 0x117 (Skill\DarkLordSkill): life 10, scale 0.2f and +0xd8 (the growth) at 0.1f. With subtype
// 1 or less (signed), the angles (45, y, 0): y is subtype · −90 + 45 if rand() % 2 is non-zero and, otherwise, 45 with
// the /0 or −45 with the others. The /2, life 12 and +0xd8 at 0.4f, no rand(). Another subtype, nothing more.
void BeginTheDarkLordSkill(Effects::ModelEffect& effect, CrtRandom& random)
{
    constexpr std::int32_t SkillLife = 10, WideLife = 12;
    constexpr float SkillScale = std::bit_cast<float>(0x3e4ccccdu); // 0.2f
    constexpr float Growth = std::bit_cast<float>(0x3dcccccdu);     // 0.1f
    constexpr float WideGrowth = std::bit_cast<float>(0x3ecccccdu); // 0.4f
    constexpr double TiltPerSubtype = -90.0, TiltBase = 45.0;       // 0x6ba1dc and 0x6b8848
    constexpr float Pitch = 45.0f, LeftTilt = -45.0f;               // 0x42340000 and 0x6b8af4
    effect.life = SkillLife;
    effect.transform.scale = SkillScale;
    effect.speed = Growth;
    const std::int32_t subtype = effect.subtype;
    if (subtype <= 1)
    {
        const float tilt = F32(static_cast<double>(subtype) * TiltPerSubtype + TiltBase);
        float yaw = tilt;
        if (random.Next() % 2 == 0)
            yaw = subtype == 0 ? Pitch : LeftTilt;
        effect.transform.angles = {Pitch, yaw, 0.0f};
        return;
    }
    if (subtype == 2)
    {
        effect.life = WideLife;
        effect.speed = WideGrowth;
    }
}

// 0x51da46..0x51daca, the 0x138 (Skill\ManaRune). The /0 (skill 0x4b, 0x502ae6): life 50, scale at 0, +0xe4 (the
// growth) at 0.1f, alpha at 0.3f, +0x58 at 0, z 300 higher and the angles at (0, 0, 45). The /1 (dropped by the /0's
// step): life 10, scale 1.1f, +0xe4 at 0.1f, +0x58 at −1, +0x64 at −2 and +0x68 at 0.4f. Another subtype, only the
// prefix. No rand().
void BeginTheManaRune(Effects::ModelEffect& effect)
{
    constexpr float Growth = std::bit_cast<float>(0x3dcccccdu); // 0.1f
    if (effect.subtype == 0)
    {
        constexpr std::int32_t RuneLife = 50;
        constexpr float RuneAlpha = std::bit_cast<float>(0x3e99999au); // 0.3f
        constexpr double Lift = 300.0;                                 // 0x6b8900
        constexpr float RuneTurn = 45.0f;                              // 0x42340000
        effect.life = RuneLife;
        effect.transform.scale = 0.0f;
        effect.turnDirection = Growth;
        effect.alpha = RuneAlpha;
        effect.objectState.hiddenMesh = 0;
        effect.transform.position.z = F32(D(effect.transform.position.z) + Lift);
        effect.transform.angles = {0.0f, 0.0f, RuneTurn};
        return;
    }
    if (effect.subtype == 1)
    {
        constexpr std::int32_t FlashLife = 10;
        constexpr float FlashScale = std::bit_cast<float>(0x3f8ccccdu); // 1.1f
        constexpr std::int16_t FlashHidden = -1, FlashBlend = -2;
        constexpr float FlashLight = std::bit_cast<float>(0x3ecccccdu); // 0.4f
        effect.life = FlashLife;
        effect.transform.scale = FlashScale;
        effect.turnDirection = Growth;
        effect.objectState.hiddenMesh = FlashHidden;
        effect.objectState.blendMesh = FlashBlend;
        effect.objectState.light = FlashLight;
    }
}

// 0x51e4f6, the 0x812 (and the 0x5c6): life 10. 0x51f8a8, the 0x813: the angles at 0 and life 5. No rand().
void BeginTheEmitter(Effects::ModelEffect& effect)
{
    constexpr std::int32_t SparkLife = 10, BoltLife = 5;
    if (effect.type == BoltEmitterEffect)
    {
        effect.transform.angles = glm::vec3(0.0f);
        effect.life = BoltLife;
        return;
    }
    effect.life = SparkLife;
}

// 0x51efa5..0x51efe9, the 0x621 except the /0: life 10; the /1 and the /2, life 20 and the start (+0x17c) at the
// position received. No rand().
void BeginThePiercingGlow(Effects::ModelEffect& effect, const Kernel::ModelEffectArguments& arguments)
{
    constexpr std::int32_t GlowLife = 10, HeldLife = 20;
    effect.life = GlowLife;
    if (effect.subtype == 1 || effect.subtype == 2)
    {
        effect.start = arguments.position;
        effect.life = HeldLife;
    }
}

// 0x52cbf4..0x52cc7b: the action published on the model (+0xa0) and 0x4ec200 with +0xd8. If the action ends, the frame
// stays at its keys and the action at 0xff.
void Animate(Effects::ModelEffect& effect, const Models::Skeleton* skeleton,
             const Effects::ModelEffectStepServices& services)
{
    if (services.publishAction)
        services.publishAction(static_cast<std::uint16_t>(effect.type), effect.animation.action);
    if (effect.animation.action == NoAction || skeleton == nullptr ||
        effect.animation.action >= skeleton->actions.size())
        return;
    const Models::ActionInfo& action = skeleton->actions[effect.animation.action];
    if (Models::AdvanceAnimation(effect.animation, action, effect.speed, Models::AnimationScene::Other))
    {
        effect.animation.frame = static_cast<float>(action.keyCount);
        effect.animation.action = NoAction;
    }
}

// 0x51fc80(effect, 1): the speed rotated by the angles, added to the position; with 0, unrotated.
void Move(Effects::ModelEffect& effect, bool rotated)
{
    const glm::vec3 step = rotated ? Rotated(effect.velocity, effect.transform.angles) : effect.velocity;
    effect.transform.position = Sum(step, effect.transform.position);
}

// 0x52cb98..0x52cd9d for this file's types: the animation of the BMDs in [0xad, 0x151) (except the 0x116 and the 0x147,
// which jump to the life), the movement (except 0x5e8..0x5ea and the 0x139, which 0x52ccb0's table sends to the life,
// 0x52cd94; the 0x13b animates again before, 0x52cd41) and one life less.
Core::Expected<void, std::string> CommonTail(Effects::ModelEffect& effect, const Models::Skeleton* skeleton,
                                             const Effects::ModelEffectStepServices& services)
{
    constexpr std::int16_t FirstAnimated = 0xad, PastAnimated = 0x151;
    const std::int16_t type = effect.type;
    const bool still = type == 0x116 || type == 0x147;
    if (!still)
    {
        if (type >= FirstAnimated && type < PastAnimated)
            Animate(effect, skeleton, services);
        if (!(type >= MovingLightFirst && type <= MovingLightLast) && type != JavelinEffect)
        {
            if (type == SiegeFlareEffect)
            {
                // 0x52cd41..0x52cd84: publishes the action and calls 0x4ec200 without checking it or its result. With
                // the action at 0xff it reads the model's action 255, outside its table.
                if (services.publishAction)
                    services.publishAction(static_cast<std::uint16_t>(type), effect.animation.action);
                if (skeleton != nullptr && !skeleton->actions.empty())
                {
                    if (effect.animation.action >= skeleton->actions.size())
                        return Core::Unexpected(std::format("0x52cd84: la segunda animación del {:#x} lee la acción "
                                                            "{} fuera de las {} del modelo",
                                                            type, effect.animation.action, skeleton->actions.size()));
                    static_cast<void>(Models::AdvanceAnimation(effect.animation,
                                                               skeleton->actions[effect.animation.action], effect.speed,
                                                               Models::AnimationScene::Other));
                }
            }
            Move(effect, true);
        }
    }
    --effect.life;
    if (effect.life <= 0)
        effect.alive = false;
    return {};
}

// 0x52952d..0x52959f, the 0x134: with the owner (+0x108, the arrival's character) alive (+0x0 at 1), +0x58 at −1 with
// the siege (0x4294b0) or −2 without it, life at 2 and +0x68 at FSIN(T · 0.001f) · 0.1f + 0.2f (PC53); otherwise, the
// slot is freed. Both branches, +0x6c at T · 0.0005f. T is the world clock (0x5767b88).
Core::Expected<void, std::string> StepTheSiegeAura(Effects::ModelEffect& effect, const HighEffectStepContext& context)
{
    constexpr double WaveRate = static_cast<double>(std::bit_cast<float>(0x3a83126fu));   // 0x6b89c8, 0.001f
    constexpr float WaveDepth = std::bit_cast<float>(0x3dcccccdu);                        // 0x6b87b8, 0.1f
    constexpr double WaveBase = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));   // 0x6b8854, 0.2f
    constexpr double ScrollRate = static_cast<double>(std::bit_cast<float>(0x3a03126fu)); // 0x6b8a68, 0.0005f
    constexpr std::int32_t HeldLife = 2;
    const auto* owner = static_cast<const Kernel::ActorState*>(effect.ownerContext);
    const double clock = D(context.worldClock);
    if (owner != nullptr && owner->alive)
    {
        effect.objectState.hiddenMesh =
            context.world.Runtime().castleSiege.siegeActive ? std::int16_t{-1} : std::int16_t{-2};
        effect.life = HeldLife;
        const auto wave = Math::Detail::SineProductPC53(clock * WaveRate, WaveDepth);
        if (!wave)
            return Core::Unexpected(std::string("0x529568: la luz del aura 0x134 no cabe en el perfil PC53"));
        effect.objectState.light = F32(*wave + WaveBase);
    }
    else
    {
        effect.alive = false;
    }
    effect.objectState.uv.x = F32(clock * ScrollRate);
    return {};
}

// 0x52aa6a..0x52aaff and 0x52725b, the 0x19f: with subtype 0 and the life a multiple of 5, the shard 0x12b/2 at the
// position shifted rand() % 100 − 50 in x and −(rand() % 30 + 15) in y; with under 5 life, the alpha times 0.769f.
Core::Expected<void, std::string> StepTheSiegeSpearman(Effects::ModelEffect& effect,
                                                       const HighEffectStepContext& context)
{
    constexpr std::int32_t DropEvery = 5;
    if (effect.subtype == 0 && effect.life % DropEvery == 0)
    {
        constexpr int SideSpan = 100, FallSpan = 30;
        constexpr double SideCenter = 50.0, FallBase = 15.0;
        glm::vec3 point = effect.transform.position;
        point.x = F32(static_cast<double>(context.random.Next() % SideSpan) - SideCenter + D(point.x));
        point.y = F32(D(point.y) - (static_cast<double>(context.random.Next() % FallSpan) + FallBase));
        constexpr std::int32_t FragmentSubtype = 2;
        const Kernel::ModelEffectArguments arguments{.type = CastleSiege::SiegeStoneEffect,
                                                     .position = point,
                                                     .angles = effect.transform.angles,
                                                     .light = effect.light,
                                                     .subtype = FragmentSubtype};
        const auto& hero = context.world.Hero();
        if (auto made =
                CastleSiege::CreateSiegeStone(arguments, context.world.Map().effects, hero.position, context.random);
            !made.HasValue())
            return Core::Unexpected(made.Error());
    }
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu)); // 0x6ba240, 0.769f
    if (effect.life < DropEvery)
        effect.alpha = F32(D(effect.alpha) * Fade);
    return {};
}

// 0x52c19a..0x52c34a, the 0x61c's part before the one it shares with the 0x13b: with life below 11 and even, the angles
// at (90, 0, 0), +0x30 72 more and, from the start, the speed rotated by +0x28; with the /0 the joint 0x632/5
// rand() % 400 + 700 higher (scale 110) and with the /1 two 0x615/6 100 higher (scale 80).
Core::Expected<void, std::string> StepTheSiegePulseJoints(Effects::ModelEffect& effect,
                                                          const HighEffectStepContext& context)
{
    constexpr std::int32_t LastJointLife = 10;
    if ((effect.subtype != 0 && effect.subtype != 1) || effect.life > LastJointLife || effect.life % 2 != 0)
        return {};
    constexpr double Spin = 72.0;
    effect.transform.angles = {90.0f, 0.0f, 0.0f};
    effect.copiedAngles.z = F32(D(effect.copiedAngles.z) + Spin);
    const glm::vec3 reach = Rotated(effect.velocity, effect.copiedAngles);
    glm::vec3 point = Sum(reach, effect.start);
    if (effect.subtype == 0)
    {
        constexpr int RiseSpan = 400;
        constexpr double RiseBase = 700.0;
        constexpr float BeamScale = 110.0f; // 0x42dc0000
        point.z = F32(static_cast<double>(context.random.Next() % RiseSpan) + D(point.z) + RiseBase);
        return CreateHighJoint(context.world, context.random, 0x632, point, point, effect.transform.angles, 5, &effect,
                               BeamScale, 0x52c266);
    }
    // 0x52c307..0x52c31b: the start's z and the 100 on the x87, stored once.
    point.z = F32(D(reach.z) + D(effect.start.z) + 100.0);
    constexpr float BoltScale = 80.0f; // 0x42a00000
    if (auto first = CreateHighJoint(context.world, context.random, 0x615, point, point, effect.transform.angles, 6,
                                     &effect, BoltScale, 0x52c31e);
        !first.HasValue())
        return first;
    return CreateHighJoint(context.world, context.random, 0x615, point, point, effect.transform.angles, 6, &effect,
                           BoltScale, 0x52c342);
}

// 0x52c34e..0x52c537, the part the 0x13b and the 0x61c share (the 0x61c enters after its joints). With the /0 and over
// 12 life, scale 0.9f more, the speed's y 2 less and the 0x13b/1 with the effect as owner; with 12 or less, scale 0.05f
// less, +0x68, the alpha and the light at life / 18 (0.0555…f), the speed's y 2 less and, from a point shifted
// rand() % 30 − 15 in x and y and 100 lower, four times the joint 0x617/0 (scale 10) with the angles
// (rand() % 60 + 150, 0, z) and the spark 0x5d9/2 (scale 1.5f); then, the dynamic light (1, 0.8f, 0.6f) of radius 1.
// With the /1, +0x68, the alpha and the light at life · 0.1f.
Core::Expected<void, std::string> StepTheSiegeFlare(Effects::ModelEffect& effect, const HighEffectStepContext& context)
{
    if (effect.subtype == 1)
    {
        constexpr double Tenth = Math::TenthAsDouble; // 0x6b8a34
        const float light = F32(static_cast<double>(effect.life) * Tenth);
        effect.objectState.light = light;
        effect.alpha = light;
        effect.light = glm::vec3(light);
        return {};
    }
    if (effect.subtype != 0)
        return {};
    constexpr std::int32_t GrowingLife = 12;
    constexpr double Fall = 2.0;
    if (effect.life > GrowingLife)
    {
        constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3f666666u)); // 0x6b8a1c, 0.9f
        effect.transform.scale = F32(D(effect.transform.scale) + Growth);
        effect.velocity.y = F32(D(effect.velocity.y) - Fall);
        const Kernel::ModelEffectArguments spark{.type = SiegeFlareEffect,
                                                 .position = effect.transform.position,
                                                 .angles = effect.transform.angles,
                                                 .light = effect.light,
                                                 .subtype = 1,
                                                 .owner = &effect};
        if (auto made = CreateHighModelEffect(
                spark, {.world = context.world, .random = context.random, .worldClock = context.worldClock});
            !made.HasValue())
            return Core::Unexpected(made.Error());
    }
    else
    {
        constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));  // 0x6b8a48, 0.05f
        constexpr double PerLife = static_cast<double>(std::bit_cast<float>(0x3d638e39u)); // 0x6ba204, 1/18
        effect.transform.scale = F32(D(effect.transform.scale) - Shrink);
        const float light = F32(static_cast<double>(effect.life) * PerLife);
        effect.objectState.light = light;
        effect.alpha = light;
        effect.light = glm::vec3(light);
        effect.velocity.y = F32(D(effect.velocity.y) - Fall);
        constexpr int Spread = 30;
        constexpr double SpreadCenter = 15.0, Drop = 100.0;
        glm::vec3 point = effect.transform.position;
        point.x = F32(static_cast<double>(context.random.Next() % Spread) - SpreadCenter + D(point.x));
        point.y = F32(static_cast<double>(context.random.Next() % Spread) - SpreadCenter + D(point.y));
        point.z = F32(D(point.z) - Drop);
        constexpr int Sparks = 4, PitchSpan = 60, PitchBase = 150;
        constexpr float SparkJointScale = 10.0f, SparkScale = 1.5f;
        constexpr std::int32_t SparkSubtype = 2;
        for (int spark = 0; spark < Sparks; ++spark)
        {
            glm::vec3 angles{static_cast<float>(context.random.Next() % PitchSpan + PitchBase), 0.0f,
                             effect.transform.angles.z};
            if (auto joint = CreateHighJoint(context.world, context.random, 0x617, point, point, angles, 0, nullptr,
                                             SparkJointScale, 0x52c4a2);
                !joint.HasValue())
                return joint;
            if (auto made =
                    CreateHighParticle(context.world, context.random, context.worldClock, Particles::FireParticle,
                                       point, angles, effect.light, SparkSubtype, SparkScale, 0x52c4be);
                !made.HasValue())
                return Core::Unexpected(made.Error());
        }
    }
    constexpr glm::vec3 GlowColor{1.0f, std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3f19999au)};
    return Terrain::AddDynamicLight(context.services.lighting, effect.transform.position, GlowColor, 1, "0x52c4f1");
}

// 0x525f1a..0x525f65, the 0x141: scale 0.1f more (FST) and, with the x87 sum at 1 or more, the alpha 0.07f less and
// scale at 1.
void StepTheGrowingFlash(Effects::ModelEffect& effect)
{
    constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b87b8, 0.1f
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3d8f5c29u));   // 0x6ba190, 0.07f
    const double grown = D(effect.transform.scale) + Growth;
    effect.transform.scale = F32(grown);
    if (grown < 1.0)
        return;
    effect.alpha = F32(D(effect.alpha) - Fade);
    effect.transform.scale = 1.0f;
}

// 0x52be78..0x52c195, the 0x625: nothing until life 15. From there, the /0 wobbles around the start (x and y at
// FSIN(rand() % 1000 · 0.01f) · 10 plus the start) and the /1 stays on it; both rise by +0xe4 (which starts negative:
// they fall), +0xe4 goes down 2 and the start's x 10, and they drop the fire 0x5db (/7 the /0, /0xb the /1) with its
// scale and the local light (1, 1, 1), the light +0xf4..+0xfc 0.1f more (FST) and two sprites with that light and the
// effect as owner: the 0x5fd with rotation rand() % 360 and scale ((rand() & 3) + 4) · 0.2f, and the 0x5ac of scale 1
// with another rotation. The /1, with the terrain (0x5f93c0) above its z, the smoke 0x5f2/0xb 50 higher with the light
// (0.24, 0.28, 0.8) and scale ((rand() & 31) + 80) · 0.025f, and one in five times (rand() % 5) the ice piece 0xca
// there with its light. Another subtype, nothing.
Core::Expected<void, std::string> StepTheFallingStar(Effects::ModelEffect& effect, const HighEffectStepContext& context)
{
    constexpr std::int32_t QuietLife = 15;
    constexpr std::int32_t WobblySubtype = 0, StraightSubtype = 1;
    if (effect.life > QuietLife || (effect.subtype != WobblySubtype && effect.subtype != StraightSubtype))
        return {};
    CrtRandom& random = context.random;
    glm::vec3& position = effect.transform.position;
    if (effect.subtype == WobblySubtype)
    {
        constexpr int WobbleSpan = 1000;                                                      // 0x3e8
        constexpr double WobbleRate = static_cast<double>(std::bit_cast<float>(0x3c23d70au)); // 0x6b88d4, 0.01f
        constexpr float WobbleSize = 10.0f;                                                   // 0x6b87a8
        for (int axis = 0; axis < 2; ++axis)
        {
            const int draw = random.Next() % WobbleSpan;
            const auto wobble = Math::Detail::SineProductPC53(static_cast<double>(draw) * WobbleRate, WobbleSize);
            if (!wobble)
                return Core::Unexpected(std::string("0x52bea9: el temblor del 0x625 no cabe en el perfil PC53"));
            position[axis] = F32(*wobble + D(effect.start[axis]));
        }
    }
    else
    {
        position.x = effect.start.x;
        position.y = effect.start.y;
    }
    constexpr double RiseLoss = 2.0, Drift = 10.0; // 0x6b8818 and 0x6b87a8
    position.z = F32(D(position.z) + D(effect.turnDirection));
    effect.turnDirection = F32(D(effect.turnDirection) - RiseLoss);
    effect.start.x = F32(D(effect.start.x) - Drift);
    const bool wobbly = effect.subtype == WobblySubtype;
    constexpr std::int32_t WobblyFire = 7, StraightFire = 0xb;
    const glm::vec3 localLight{1.0f};
    if (auto fire =
            CreateHighParticle(context.world, random, context.worldClock, Particles::DeathSpiritFireParticle, position,
                               effect.transform.angles, localLight, wobbly ? WobblyFire : StraightFire,
                               effect.transform.scale, wobbly ? 0x52bf29u : 0x52c036u);
        !fire.HasValue())
        return Core::Unexpected(fire.Error());
    constexpr double Glow = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b87b8, 0.1f
    const float glow = F32(D(effect.light.x) + Glow);
    effect.light = glm::vec3(glow);
    constexpr std::uint16_t ShineSprite = 0x5fd, FlareSprite = 0x5ac; // Effect\Shiny02.jpg and Effect\flare01.jpg
    constexpr int FullTurn = Math::FullTurn, ShineSizes = 3, ShineBase = 4;
    constexpr double ShineStep = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)); // 0x6b8854, 0.2f
    const auto shineTurn = static_cast<float>(random.Next() % FullTurn);
    const float shineScale = F32(static_cast<double>((random.Next() & ShineSizes) + ShineBase) * ShineStep);
    if (auto shine = CreateHighSprite(context, ShineSprite, position, shineScale, effect.light, shineTurn,
                                      wobbly ? 0x52bf98u : 0x52c0a5u);
        !shine.HasValue())
        return shine;
    const auto flareTurn = static_cast<float>(random.Next() % FullTurn);
    if (auto flare = CreateHighSprite(context, FlareSprite, position, 1.0f, effect.light, flareTurn,
                                      wobbly ? 0x52bfc6u : 0x52c0d3u);
        !flare.HasValue())
        return flare;
    if (wobbly)
        return {};
    if (!context.services.terrainHeight)
        return Core::Unexpected(std::string("0x52c0df: el 0x625/1 pide la altura del terreno (0x5f93c0)"));
    if (!(context.services.terrainHeight(position.x, position.y) > D(position.z)))
        return {};
    constexpr glm::vec3 SmokeLight{std::bit_cast<float>(0x3e75c28fu), std::bit_cast<float>(0x3e8f5c29u),
                                   std::bit_cast<float>(0x3f4ccccdu)}; // (0.24, 0.28, 0.8)
    constexpr double SmokeLift = 50.0;                                 // 0x6b86b0
    const glm::vec3 point{position.x, position.y, F32(D(position.z) + SmokeLift)};
    constexpr int SmokeSizes = 31, SmokeBase = 0x50;
    constexpr double SmokeStep = static_cast<double>(std::bit_cast<float>(0x3ccccccdu)); // 0x6b8bd0, 0.025f
    const float smokeScale = F32(static_cast<double>((random.Next() & SmokeSizes) + SmokeBase) * SmokeStep);
    if (auto smoke =
            CreateHighParticle(context.world, random, context.worldClock, Particles::BreathParticle, point,
                               effect.transform.angles, SmokeLight, Particles::StoneDustSubtype, smokeScale, 0x52c15a);
        !smoke.HasValue())
        return Core::Unexpected(smoke.Error());
    constexpr int ShardOdds = 5;
    if (random.Next() % ShardOdds != 0)
        return {};
    static_cast<void>(context.world.Map().effects.CreateIceShard(point, effect.transform.angles, effect.light, random));
    return {};
}

// The speed (+0xcc) rotated by the angles (0x5fc8f0 and 0x5fcbd0) and added to the position, one fstp per axis.
void Advance(Effects::ModelEffect& effect)
{
    effect.transform.position = Sum(Rotated(effect.velocity, effect.transform.angles), effect.transform.position);
}

// 0x52727b..0x527447, the 0x116/0: with an owner (+0x108, a character), the destination is its position plus the start.
// If +0xe4 exceeds 1, from 1 up to before +0xe4: with rand() & 1 at 0, the pitch 20 more (below −90) or 20 less;
// 0x4e9810 turns toward the destination by +0xd8 at a time; +0xd8 0.4f more (and 0.1f more with under 10 life, from the
// unrounded sum); it advances and drops the 0x116/1 in its place with itself as owner. Then, +0xe4 0.1f more and the
// sound 0x1ca. With the last turn's distance below 40 and life at 5, it reads the terrain height (0x5f93c0) into a
// variable nobody reads: MuRet does not ask for it. Without an owner, nothing.
Core::Expected<void, std::string> StepTheHomingPart(Effects::ModelEffect& effect, const HighEffectStepContext& context)
{
    const auto* owner = static_cast<const Kernel::ActorState*>(effect.ownerContext);
    if (owner == nullptr)
        return {};
    const glm::vec3 target = Sum(owner->position, effect.start);
    constexpr double Tilt = 20.0, TiltFloor = -90.0;                                        // 0x6b86d0 and 0x6ba1dc
    constexpr double Acceleration = static_cast<double>(std::bit_cast<float>(0x3ecccccdu)); // 0x6b87b4, 0.4f
    constexpr double LateBoost = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));    // 0x6b87b8, 0.1f
    constexpr std::int32_t LateLife = 10;
    constexpr std::int32_t TrailSubtype = 1;
    const HighEffectContext created{.world = context.world,
                                    .random = context.random,
                                    .worldClock = context.worldClock,
                                    .terrain = context.terrain,
                                    .faunaModels = context.faunaModels};
    for (int count = 1; static_cast<double>(count) < D(effect.turnDirection); ++count)
    {
        if ((context.random.Next() & 1) == 0)
        {
            const double pitch = D(effect.transform.angles.x);
            effect.transform.angles.x = F32(!(pitch >= TiltFloor) ? pitch + Tilt : pitch - Tilt);
        }
        static_cast<void>(Math::MoveToward(effect.transform.position, effect.transform.angles, target, effect.speed));
        const double faster = D(effect.speed) + Acceleration;
        effect.speed = F32(effect.life < LateLife ? faster + LateBoost : faster);
        Advance(effect);
        constexpr std::int16_t PierPart = 0x116;
        if (auto trail = CreateHighModelEffect({.type = PierPart,
                                                .position = effect.transform.position,
                                                .angles = effect.transform.angles,
                                                .light = effect.light,
                                                .subtype = TrailSubtype,
                                                .owner = &effect},
                                               created);
            !trail.HasValue())
            return Core::Unexpected("0x5273c6: " + trail.Error());
    }
    constexpr double MoreBursts = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b87b8, 0.1f
    constexpr Audio::SoundId PartSound{0x1ca};
    effect.turnDirection = F32(D(effect.turnDirection) + MoreBursts);
    context.world.Runtime().RequestSound(PartSound);
    return {};
}

// 0x527455..0x527534, the 0x116/2: with an owner, 0x4e9810 turns toward its position by +0xd8 at a time, +0xd8 2.4f
// more, it advances and, with life a multiple of 3, the joint 0x61c/2 in its place with the angles (−90, 0, z) and
// scale 150. Without an owner, nothing.
Core::Expected<void, std::string> StepTheDivingPart(Effects::ModelEffect& effect, const HighEffectStepContext& context)
{
    const auto* owner = static_cast<const Kernel::ActorState*>(effect.ownerContext);
    if (owner == nullptr)
        return {};
    constexpr double Acceleration = static_cast<double>(std::bit_cast<float>(0x4019999au)); // 0x6ba238, 2.4f
    static_cast<void>(
        Math::MoveToward(effect.transform.position, effect.transform.angles, owner->position, effect.speed));
    effect.speed = F32(D(effect.speed) + Acceleration);
    Advance(effect);
    constexpr std::int32_t JointEvery = 3;
    if (effect.life % JointEvery != 0)
        return {};
    constexpr std::int32_t DiveJoint = 0x61c, DiveJointSubtype = 2;
    constexpr float DiveJointScale = 150.0f; // 0x43160000
    constexpr float DivePitch = -90.0f;      // 0xc2b40000
    const glm::vec3& position = effect.transform.position;
    return CreateHighJoint(context.world, context.random, DiveJoint, position, position,
                           {DivePitch, 0.0f, effect.transform.angles.z}, DiveJointSubtype, nullptr, DiveJointScale,
                           0x52752c);
}

// 0x52727b..0x527276, the 0x116: the /0 and the /2 seek their owner; the /1, with under 5 life (edi, 5 from 0x5208df),
// the alpha times 0.7692308f.
Core::Expected<void, std::string> StepThePierPart(Effects::ModelEffect& effect, const HighEffectStepContext& context)
{
    if (effect.subtype == 0)
        return StepTheHomingPart(effect, context);
    if (effect.subtype == 2)
        return StepTheDivingPart(effect, context);
    constexpr std::int32_t FadeLife = 5;
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu)); // 0x6ba240
    if (effect.subtype == 1 && effect.life < FadeLife)
        effect.alpha = F32(D(effect.alpha) * Fade);
    return {};
}

// 0x527539..0x5275c8, the 0x117: the scale grows by +0xd8. With subtype 1 or less, +0xd8 0.02f more and, with under 7
// life, +0x68 times 0.5555556f. The /2, +0xd8 0.06f less and, with under 8, +0x68 times 0.6666667f. The others, +0xd8
// 0.04f more and, with under 8, +0x68 times 0.5555556f.
void StepTheDarkLordSkill(Effects::ModelEffect& effect)
{
    constexpr double SmallGrowth = static_cast<double>(std::bit_cast<float>(0x3ca3d70au)); // 0x6b8b90, 0.02f
    constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3d75c28fu));      // 0x6ba230, 0.06f
    constexpr double WideGrowth = static_cast<double>(std::bit_cast<float>(0x3d23d70au));  // 0x6b8aec, 0.04f
    constexpr double QuickFade = static_cast<double>(std::bit_cast<float>(0x3f0e38e4u));   // 0x6ba234
    constexpr double SlowFade = static_cast<double>(std::bit_cast<float>(0x3f2aaaabu));    // 0x6b8c64
    constexpr std::int32_t EarlyFadeLife = 7, LateFadeLife = 8;
    effect.transform.scale = F32(D(effect.transform.scale) + D(effect.speed));
    double fade = QuickFade;
    std::int32_t fadeLife = LateFadeLife;
    if (effect.subtype <= 1)
    {
        effect.speed = F32(D(effect.speed) + SmallGrowth);
        fadeLife = EarlyFadeLife;
    }
    else if (effect.subtype == 2)
    {
        effect.speed = F32(D(effect.speed) - Shrink);
        fade = SlowFade;
    }
    else
    {
        effect.speed = F32(D(effect.speed) + WideGrowth);
    }
    if (effect.life < fadeLife)
        effect.objectState.light = F32(D(effect.objectState.light) * fade);
}

// 0x529a4f..0x529b81, the 0x138. The /0: with over 43 life, +0x58 at −2; with over 40, at 0; with over 30, the scale
// grows by +0xe4 and +0xe4 0.15f more, and on passing 1 the scale stays at 1, +0xe4 at 0.01f and it drops the 0x138/1
// in its place with its light; with under 15, the scale shrinks by +0xe4 (floor 0), +0xe4 0.2f more, the alpha is
// life · 0.05f and it goes down 10. The /1: scale 0.02f less (compared unrounded: below 1, it stays at 1) and the alpha
// life · 0.05f.
Core::Expected<void, std::string> StepTheManaRune(Effects::ModelEffect& effect, const HighEffectStepContext& context)
{
    constexpr double AlphaPerLife = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu)); // 0x6b8ad0, 0.05f
    if (effect.subtype == 1)
    {
        constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3ca3d70au)); // 0x6b8b90, 0.02f
        const double scale = D(effect.transform.scale) - Shrink;
        effect.transform.scale = F32(scale);
        if (!(scale >= 1.0))
            effect.transform.scale = 1.0f;
        effect.alpha = F32(static_cast<double>(effect.life) * AlphaPerLife);
        return {};
    }
    if (effect.subtype != 0)
        return {};
    constexpr std::int32_t HiddenLife = 43, ShownLife = 40, GrowingLife = 30, FadingLife = 15;
    constexpr std::int16_t Hidden = -2;
    if (effect.life > HiddenLife)
    {
        effect.objectState.hiddenMesh = Hidden;
        return {};
    }
    if (effect.life > ShownLife)
    {
        effect.objectState.hiddenMesh = 0;
        return {};
    }
    if (effect.life > GrowingLife)
    {
        constexpr double Acceleration = static_cast<double>(std::bit_cast<float>(0x3e19999au)); // 0x6b8ae8, 0.15f
        constexpr float SettledGrowth = std::bit_cast<float>(0x3c23d70au);                      // 0.01f
        effect.transform.scale = F32(D(effect.transform.scale) + D(effect.turnDirection));
        effect.turnDirection = F32(D(effect.turnDirection) + Acceleration);
        if (!(effect.transform.scale > 1.0f))
            return {};
        effect.transform.scale = 1.0f;
        effect.turnDirection = SettledGrowth;
        constexpr std::int16_t ManaRune = 0x138;
        constexpr std::int32_t FlashSubtype = 1;
        const HighEffectContext created{.world = context.world,
                                        .random = context.random,
                                        .worldClock = context.worldClock,
                                        .terrain = context.terrain,
                                        .faunaModels = context.faunaModels};
        if (auto flash = CreateHighModelEffect({.type = ManaRune,
                                                .position = effect.transform.position,
                                                .angles = effect.transform.angles,
                                                .light = effect.light,
                                                .subtype = FlashSubtype},
                                               created);
            !flash.HasValue())
            return Core::Unexpected("0x529ae2: " + flash.Error());
        return {};
    }
    if (effect.life >= FadingLife)
        return {};
    constexpr double Acceleration = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)); // 0x6b8854, 0.2f
    constexpr double Sink = 10.0;                                                           // 0x6b87a8
    effect.transform.scale = F32(D(effect.transform.scale) - D(effect.turnDirection));
    effect.turnDirection = F32(D(effect.turnDirection) + Acceleration);
    if (!(effect.transform.scale >= 0.0f))
        effect.transform.scale = 0.0f;
    effect.alpha = F32(static_cast<double>(effect.life) * AlphaPerLife);
    effect.transform.position.z = F32(D(effect.transform.position.z) - Sink);
    return {};
}

// 0x52bb79..0x52bbf1, the 0x621 except the /0: with the /1 and the /2, the shared origin of the owner's model (+0x108
// → +0x2, the +0x6c..+0x74 of its BMD) becomes the start and the position is the point (0, 0, 10) of the owner's bone
// 33 (0x4ec110 with its bones, +0x120 + 0x630). Then, all of them, the /0's three emissions (0x52bbf6).
Core::Expected<void, std::string> StepThePiercingGlow(Effects::ModelEffect& effect,
                                                      const HighEffectStepContext& context)
{
    if (effect.subtype == 1 || effect.subtype == 2)
    {
        const auto* owner = static_cast<const Kernel::ActorState*>(effect.ownerContext);
        if (owner == nullptr)
            return Core::Unexpected(std::string("0x52bb86: el brillo 0x621 lee el modelo de un dueño (+0x108) nulo"));
        constexpr std::size_t GlowBone = 0x630 / 0x30;
        constexpr glm::vec3 GlowOffset{0.0f, 0.0f, 10.0f}; // 0x41200000
        const auto type = static_cast<std::uint16_t>(owner->type);
        Kernel::SharedModelPlacement placement = context.world.ModelPlacement(type);
        placement.origin = effect.start;
        context.world.PlaceModel(type, placement);
        effect.transform.position = ActorBonePoint(*owner, GlowBone, GlowOffset, context.world);
    }
    return Effects::EmitPiercingShotGlow(effect, context.world.Particles(), context.random, context.services);
}

// 0x52c981..0x52c9e2, the 0x812: each step, the joint 0x60f/0xc (the /1, scale 100) or 0x60f/4 (the /0, scale 50) in
// its place with itself as owner (0x530860(0x60f, position, position, angles, subtype, effect, scale, −1, 0, 0, −1)).
// Another subtype, nothing.
Core::Expected<void, std::string> StepTheSparkEmitter(Effects::ModelEffect& effect,
                                                      const HighEffectStepContext& context)
{
    constexpr std::int32_t SparkJoint = 0x60f;
    constexpr std::int32_t WideSubtype = 0xc, NarrowSubtype = 4;
    constexpr float WideScale = 100.0f, NarrowScale = 50.0f; // 0x42c80000 and 0x42480000
    if (effect.subtype != 0 && effect.subtype != 1)
        return {};
    const bool wide = effect.subtype == 1;
    const glm::vec3& position = effect.transform.position;
    return CreateHighJoint(context.world, context.random, SparkJoint, position, position, effect.transform.angles,
                           wide ? WideSubtype : NarrowSubtype, &effect, wide ? WideScale : NarrowScale,
                           wide ? 0x52c9a7u : 0x52c9dau);
}

// 0x52caa5..0x52cb94, the 0x813: three times, the angles (rand() % 90, 0, rand() % 360), the point 100 back (the
// position minus (0, −100, 0) rotated by them, one fstp per axis) and the joint 0x619/6 from the point to the position,
// with no owner and scale 5.
Core::Expected<void, std::string> StepTheBoltEmitter(Effects::ModelEffect& effect, const HighEffectStepContext& context)
{
    constexpr int Bolts = 3, PitchSpan = 90, FullTurn = Math::FullTurn;
    constexpr glm::vec3 Reach{0.0f, -100.0f, 0.0f}; // 0xc2c80000
    constexpr std::int32_t BoltJoint = 0x619, BoltJointSubtype = 6;
    constexpr float BoltJointScale = 5.0f; // 0x40a00000
    const glm::vec3& position = effect.transform.position;
    for (int bolt = 0; bolt < Bolts; ++bolt)
    {
        glm::vec3 angles{0.0f};
        angles.x = static_cast<float>(context.random.Next() % PitchSpan);
        angles.z = static_cast<float>(context.random.Next() % FullTurn);
        const glm::vec3 back = Rotated(Reach, angles);
        const glm::vec3 from{F32(D(position.x) - D(back.x)), F32(D(position.y) - D(back.y)),
                             F32(D(position.z) - D(back.z))};
        if (auto joint = CreateHighJoint(context.world, context.random, BoltJoint, from, position, angles,
                                         BoltJointSubtype, nullptr, BoltJointScale, 0x52cb85);
            !joint.HasValue())
            return joint;
    }
    return {};
}

// 0x52c9fd..0x52caa0 and 0x521216, the 0xc1a: with the terrain (0x5f93c0) above its z, the explosion 0x5ed/0 80 higher
// (with its sound 0x40), six pebbles 0xc8 or 0xc9 (rand() & 1) with subtype 0 and the slot freed; always, the dynamic
// light (light · 0.2f, light · 0.4f, light) of radius 2 with the local light of the first rand().
Core::Expected<void, std::string> StepTheRadialShard(Effects::ModelEffect& effect, int lightDraw,
                                                     const HighEffectStepContext& context)
{
    if (!context.services.terrainHeight)
        return Core::Unexpected(std::string("0x52ca08: el fragmento 0xc1a pide la altura del terreno (0x5f93c0)"));
    const glm::vec3& position = effect.transform.position;
    if (context.services.terrainHeight(position.x, position.y) > D(position.z))
    {
        constexpr double BlastLift = 80.0;
        const glm::vec3 blast{position.x, position.y, F32(D(position.z) + BlastLift)};
        const auto made =
            CreateHighParticle(context.world, context.random, context.worldClock, Particles::ExplosionParticle, blast,
                               effect.transform.angles, glm::vec3(1.0f), 0, 1.0f, 0x52ca4f);
        if (!made.HasValue())
            return Core::Unexpected(made.Error());
        constexpr Audio::SoundId BlastSound = Sound::ExplosionSound;
        if (made.Value().has_value())
            context.world.Runtime().RequestSound(BlastSound);
        constexpr int Chips = 6;
        for (int chip = 0; chip < Chips; ++chip)
        {
            const auto type = static_cast<std::int16_t>(Effects::FirstStoneChipEffect + (context.random.Next() & 1));
            static_cast<void>(context.world.Map().effects.CreateStoneChip(
                type, effect.transform.position, effect.transform.angles, effect.light, context.random));
        }
        effect.alive = false;
    }
    const float luminosity = Effects::StepLuminosity(lightDraw, effect.life);
    constexpr double Fifth = Math::FifthAsDouble;                                        // 0x6b8854
    constexpr double TwoFifths = static_cast<double>(std::bit_cast<float>(0x3ecccccdu)); // 0x6b87b4
    const glm::vec3 color{F32(D(luminosity) * Fifth), F32(D(luminosity) * TwoFifths), luminosity};
    return Terrain::AddDynamicLight(context.services.lighting, position, color, 2, "0x521246");
}

// 0x52b9c1..0x52bb24, the 0x61e/6 (the decal 0x46 puts on the target, its owner, 0x4fe424): the owner's model
// (0x5769ad8 + its type · 0xe0) takes the decal's start as origin (+0x6c); +0x68 at (rand() % 10) · 0.1f; as many
// rounds as life, each with a random bone (rand() % its bones) that, if it is not a dummy, gives three rand() of the
// shift (± 50, ± 10, ± 10), its point (0x4ec110 with the owner's own bones, the model's scale and that origin) and the
// particle 0x5ac/8 with the owner's angles, light 1 and scale 1; then, the owner's alpha (+0x174) at life · 0.0125f.
Core::Expected<void, std::string> StepTheBindingDecal(Effects::ModelEffect& effect,
                                                      const HighEffectStepContext& context)
{
    auto* owner = static_cast<Kernel::ActorState*>(effect.ownerContext);
    if (owner == nullptr)
        return Core::Unexpected(std::string("0x52b9c1: el 0x61e/6 lee el modelo de un dueño nulo"));
    Kernel::SharedModelPlacement placement = context.world.ModelPlacement(owner->type);
    placement.origin = effect.start;
    context.world.PlaceModel(owner->type, placement);
    constexpr int GlowSpan = 10;
    constexpr double Tenth = Math::TenthAsDouble; // 0x6b87b8
    effect.objectState.light = F32(static_cast<double>(context.random.Next() % GlowSpan) * Tenth);
    // 0x52ba1d: the global table's BMD by the owner's type, by the same path as the 0x19f's creation.
    const Models::Skeleton* skeleton = ModelSkeletonOf(context.faunaModels, context.models, owner->type);
    constexpr int SideSpan = 100, SideCenter = 50, DepthSpan = 20, DepthCenter = 10;
    constexpr std::int32_t SparkSubtype = 8;
    constexpr float SparkScale = 1.0f;
    for (std::int32_t turn = 0; turn < effect.life; ++turn)
    {
        const std::size_t bones = skeleton != nullptr ? skeleton->bones.size() : 0;
        if (bones == 0)
            return Core::Unexpected(std::format("0x52ba47: el modelo {:#x} del dueño del 0x61e/6 no tiene huesos (el "
                                                "rand() % 0 del original)",
                                                owner->type));
        const auto bone = static_cast<std::size_t>(context.random.Next() % static_cast<int>(bones));
        if (skeleton->bones[bone].dummy)
            continue;
        glm::vec3 offset{};
        offset.x = static_cast<float>(context.random.Next() % SideSpan - SideCenter);
        offset.y = static_cast<float>(context.random.Next() % DepthSpan - DepthCenter);
        offset.z = static_cast<float>(context.random.Next() % DepthSpan - DepthCenter);
        // Without own bones (before the owner's first draw), the matrix at zero, as HandShinePoint (Q-C318).
        const Math::BoneTransform matrix =
            bone < owner->bodyPose.size() ? owner->bodyPose[bone] : Math::BoneTransform(0.0f);
        const glm::vec3 point =
            Math::OriginalTransformPosition(Math::OriginalMatrixOf(matrix), offset, placement.scale, placement.origin);
        if (auto made =
                CreateHighParticle(context.world, context.random, context.worldClock, Particles::SwellLifeParticle,
                                   point, owner->angles, glm::vec3(1.0f), SparkSubtype, SparkScale, 0x52baf5);
            !made.HasValue())
            return Core::Unexpected(made.Error());
    }
    constexpr double FadePerLife = static_cast<double>(std::bit_cast<float>(0x3c4ccccdu)); // 0x6ba208, 0.0125f
    owner->alpha = F32(static_cast<double>(effect.life) * FadePerLife);
    return {};
}

// 0x52bb29..0x52bb74, the 0x61e/9: scale 0.3f more and the light times 0.909f in each channel.
void StepTheGrowingDecal(Effects::ModelEffect& effect)
{
    constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3e99999au)); // 0x6b89dc, 0.3f
    constexpr double Decay = static_cast<double>(std::bit_cast<float>(0x3f68ba2eu));  // 0x6ba26c
    effect.transform.scale = F32(D(effect.transform.scale) + Growth);
    for (int channel = 0; channel < 3; ++channel)
        effect.light[channel] = F32(D(effect.light[channel]) * Decay);
}
} // namespace

glm::vec3 PointAround(const Kernel::ActorState& actor, const glm::vec3& offset)
{
    return Sum(actor.position, Rotated(offset, actor.angles));
}

HighEffectContext HighContextOf(const Actors::CharacterStepContext& context)
{
    return {.world = context.world,
            .random = context.random,
            .worldClock = context.worldClock,
            .send = context.send,
            .models = context.models,
            .terrain = context.terrain,
            .faunaModels = context.faunaModels};
}

glm::vec3 ActorBonePoint(const Kernel::ActorState& actor, std::size_t bone, const glm::vec3& offset,
                         const Kernel::WorldState& world)
{
    const Math::BoneTransform matrix = bone < actor.bodyPose.size() ? actor.bodyPose[bone] : Math::BoneTransform(0.0f);
    const auto placement = world.ModelPlacement(actor.type);
    return Math::OriginalTransformPosition(Math::OriginalMatrixOf(matrix), offset, placement.scale, placement.origin);
}

bool IsHighModelEffect(std::int16_t type)
{
    return type == SiegeAuraEffect || type == SiegeFlareEffect || type == SiegeSpearmanEffect ||
           type == SiegePulseEffect || type == RadialShardEffect || type == GrowingFlashEffect ||
           type == Effects::ColoredDecalEffect || (type >= MovingLightFirst && type <= MovingLightLast) ||
           type == FallingStarEffect || type == PierPartEffect || type == DarkLordSkillEffect ||
           type == ManaRuneEffect || type == SparkEmitterEffect || type == BoltEmitterEffect ||
           type == PiercingGlowEffect || type == SkullEffect || type == SeamanEffect || type == FireBreathEffect ||
           type == JavelinEffect || type == ThunderEffect || type == WaveEffect || type == PiercingEffect ||
           type == RingEffect || type == MeteorEffect || type == MeteorShellEffect || type == MeteorCallEffect ||
           type == GuildMarkEffect || type == KundunSpiritEffect || type == KundunPhoenixEffect ||
           type == KundunCallEffect || (type >= FirstKundunPieceEffect && type <= LastKundunPieceEffect) ||
           type == KundunEchoEffect || type == SweepEmitterEffect || type == EnergyBallEffect ||
           type == FlameBurstEffect;
}

Core::Expected<std::optional<std::size_t>, std::string>
CreateHighModelEffect(const Kernel::ModelEffectArguments& arguments, const HighEffectContext& context)
{
    if (!IsHighModelEffect(arguments.type))
        return Core::Unexpected(std::format("0x518cf0: el alta del efecto {:#x} no es de este lote", arguments.type));
    // 0x51efea: the /0 is ModelEffects'.
    if (arguments.type == PiercingGlowEffect && arguments.subtype == 0)
        return context.world.Map().effects.CreatePiercingShot(arguments);
    // 0x51c2b3..0x51c36d: the 0x114/2..6 waves and the 0x115 (0x51c8e8), with no rand() or outside input:
    // ModelEffects'.
    constexpr std::int32_t FirstWorldWave = 2, LastWorldWave = 6;
    if (arguments.type == WaveEffect && arguments.subtype >= FirstWorldWave && arguments.subtype <= LastWorldWave)
        return context.world.Map().effects.CreateWaveEffect(arguments);
    if (arguments.type == PiercingEffect)
        return context.world.Map().effects.CreatePiercingEffect(arguments);
    const auto slot = context.world.Map().effects.Begin(arguments);
    if (!slot.has_value())
        return slot;
    Effects::ModelEffect& effect = context.world.Map().effects.Effects()[*slot];
    Core::Expected<void, std::string> begun;
    switch (arguments.type)
    {
    case SiegeAuraEffect:
        BeginTheSiegeAura(effect);
        break;
    case SiegeFlareEffect:
        BeginTheSiegeFlare(effect);
        break;
    case SiegeSpearmanEffect:
        BeginTheSiegeSpearman(effect, context);
        break;
    case SiegePulseEffect:
        begun = BeginTheSiegePulse(effect, arguments, context);
        break;
    case RadialShardEffect:
        BeginTheRadialShard(effect);
        break;
    case GrowingFlashEffect:
        BeginTheGrowingFlash(effect);
        break;
    case Effects::ColoredDecalEffect:
        Effects::BeginColoredDecal(effect, context.random);
        break;
    case MovingLightFirst:
    case MovingLightSecond:
    case MovingLightLast:
        begun = BeginTheMovingLight(effect, context);
        break;
    case FallingStarEffect:
        BeginTheFallingStar(effect, context);
        break;
    case PierPartEffect:
        begun = BeginThePierPart(effect, arguments, context);
        break;
    case DarkLordSkillEffect:
        BeginTheDarkLordSkill(effect, context.random);
        break;
    case ManaRuneEffect:
        BeginTheManaRune(effect);
        break;
    case SparkEmitterEffect:
    case BoltEmitterEffect:
        BeginTheEmitter(effect);
        break;
    case PiercingGlowEffect:
        BeginThePiercingGlow(effect, arguments);
        break;
    case SkullEffect:
        BeginTheSkull(effect);
        break;
    case SeamanEffect:
        BeginTheSeaman(effect);
        break;
    case FireBreathEffect:
        begun = BeginTheFireBreath(effect, context);
        break;
    case JavelinEffect:
        begun = BeginTheJavelin(effect, context);
        break;
    case ThunderEffect:
        BeginTheThunder(effect);
        break;
    case WaveEffect:
        begun = BeginTheWave(effect, arguments, context);
        break;
    case RingEffect:
        begun = BeginTheRing(effect, context);
        break;
    case MeteorEffect:
    case MeteorShellEffect:
    case MeteorCallEffect:
        BeginTheMeteor(effect);
        break;
    case KundunSpiritEffect:
        begun = BeginTheKundunSpirit(effect, context);
        break;
    case KundunPhoenixEffect:
        begun = BeginTheKundunPhoenix(effect, context);
        break;
    case KundunCallEffect:
        BeginTheKundunCall(context.world.Map().effects, *slot);
        break;
    case KundunEchoEffect:
        BeginTheKundunEcho(effect);
        break;
    case SweepEmitterEffect:
        BeginTheSweepEmitter(effect);
        break;
    case EnergyBallEffect:
        BeginTheEnergyBall(effect);
        break;
    case FlameBurstEffect:
        BeginTheFlameBurst(effect);
        break;
    case GuildMarkEffect:
    {
        // 0x51e0d8: the owner's model and own bones (+0x108 → +0x2 and +0x120).
        const auto* owner = static_cast<const Kernel::ActorState*>(arguments.owner);
        if (owner == nullptr)
        {
            begun = Core::Unexpected(std::string("0x51e0d8: el 0x140 lee el modelo de un dueño (+0x108) nulo"));
            break;
        }
        begun = BeginTheGuildMark(effect, *slot, *owner,
                                  ActorBonePoint(*owner, GuildMarkBone, glm::vec3(0.0f), context.world),
                                  context.world.Particles(), context.random, context.worldClock);
        break;
    }
    default:
        // 0x51e234: the Kundun fall pieces; without the terrain height (the ones CreateTheKundunPiece calls have it),
        // the /2, the /3 and the /4 cut.
        if (arguments.type >= FirstKundunPieceEffect && arguments.type <= LastKundunPieceEffect)
            begun = BeginTheKundunPiece(effect, context.random, {});
        break;
    }
    if (!begun.HasValue())
        return Core::Unexpected(begun.Error());
    return slot;
}

bool StepsAsHighModelEffect(const Effects::ModelEffect& effect)
{
    // 0x52b984: the 0x61e that do not go to 0x518900 (the /0, /1, /3 and the others that do go are StepPetSummon's).
    if (effect.type == Effects::ColoredDecalEffect)
        return effect.subtype == Effects::PulsingDecalSubtype || effect.subtype == Effects::BindingDecalSubtype ||
               effect.subtype == Effects::BindingChildSubtype || effect.subtype == Effects::SpawningDecalSubtype ||
               effect.subtype == Effects::GrowingDecalSubtype || effect.subtype == Effects::CoolPulsingDecalSubtype;
    // The 0x621/0 steps in StepGameModelEffect.
    if (effect.type == PiercingGlowEffect)
        return effect.subtype != 0;
    // The 0x114 and the 0x115 are born here and step in StepGameModelEffect (0x526dd0 and 0x526f9d).
    if (effect.type == WaveEffect || effect.type == PiercingEffect)
        return false;
    // The 0x13a is born in Effects/ArrowEffects (CreateTheBlast) and steps here (0x529dee).
    if (effect.type == ArrowBlastEffect)
        return true;
    return IsHighModelEffect(effect.type);
}

Core::Expected<void, std::string> StepHighModelEffect(Effects::ModelEffect& effect, const Models::Skeleton* skeleton,
                                                      const HighEffectStepContext& context)
{
    if (!effect.alive)
        return {};
    if (!StepsAsHighModelEffect(effect))
        return Core::Unexpected(std::format("0x5208a0: el paso del efecto {:#x} no es de este lote", effect.type));
    // 0x5208c5: the first rand() of every live effect, the local light.
    const int lightDraw = context.random.Next();
    const glm::vec3 before = effect.transform.position;
    Core::Expected<void, std::string> stepped;
    switch (effect.type)
    {
    case SiegeAuraEffect:
        stepped = StepTheSiegeAura(effect, context);
        break;
    case SiegeSpearmanEffect:
        stepped = StepTheSiegeSpearman(effect, context);
        break;
    case SiegePulseEffect:
        stepped = StepTheSiegePulseJoints(effect, context);
        if (stepped.HasValue())
            stepped = StepTheSiegeFlare(effect, context);
        break;
    case SiegeFlareEffect:
        stepped = StepTheSiegeFlare(effect, context);
        break;
    case RadialShardEffect:
        stepped = StepTheRadialShard(effect, lightDraw, context);
        break;
    case GrowingFlashEffect:
        StepTheGrowingFlash(effect);
        break;
    case Effects::ColoredDecalEffect:
        if (effect.subtype == Effects::BindingDecalSubtype)
            stepped = StepTheBindingDecal(effect, context);
        else if (effect.subtype == Effects::GrowingDecalSubtype)
            StepTheGrowingDecal(effect);
        break;
    case FallingStarEffect:
        stepped = StepTheFallingStar(effect, context);
        break;
    case PierPartEffect:
        stepped = StepThePierPart(effect, context);
        break;
    case DarkLordSkillEffect:
        StepTheDarkLordSkill(effect);
        break;
    case ManaRuneEffect:
        stepped = StepTheManaRune(effect, context);
        break;
    case SparkEmitterEffect:
        stepped = StepTheSparkEmitter(effect, context);
        break;
    case BoltEmitterEffect:
        stepped = StepTheBoltEmitter(effect, context);
        break;
    case PiercingGlowEffect:
        stepped = StepThePiercingGlow(effect, context);
        break;
    case SkullEffect:
        stepped = StepTheSkull(effect, context);
        break;
    case SeamanEffect:
        stepped = StepTheSeaman(effect, Effects::StepLuminosity(lightDraw, effect.life), context);
        break;
    case FireBreathEffect:
        stepped = StepTheFireBreath(effect, Effects::StepLuminosity(lightDraw, effect.life), context);
        break;
    case JavelinEffect:
        stepped = StepTheJavelin(effect, context);
        break;
    case ThunderEffect:
        stepped = StepTheThunder(effect, context);
        break;
    case RingEffect:
        stepped = StepTheRing(effect, Effects::StepLuminosity(lightDraw, effect.life), context);
        break;
    case MeteorEffect:
        stepped = StepTheMeteor(effect, skeleton, context);
        break;
    case MeteorCallEffect:
        stepped = StepTheMeteorCall(effect, context);
        break;
    case GuildMarkEffect:
        stepped = StepTheGuildMark(effect, context);
        break;
    case KundunSpiritEffect:
        stepped = StepTheKundunSpirit(effect, context);
        break;
    case KundunPhoenixEffect:
        StepTheKundunPhoenix(effect);
        break;
    case KundunCallEffect:
        stepped = StepTheKundunCall(effect, context);
        break;
    case KundunEchoEffect:
        stepped = StepTheKundunEcho(effect, context);
        break;
    case SweepEmitterEffect:
        stepped = StepTheSweepEmitter(effect, context);
        break;
    case EnergyBallEffect:
        stepped = StepTheEnergyBall(effect, context);
        break;
    case FlameBurstEffect:
        stepped = StepTheFlameBurst(effect, Effects::StepLuminosity(lightDraw, effect.life), context);
        break;
    case ArrowBlastEffect:
        stepped = StepTheArrowBlast(effect, context);
        break;
    default:
        if (effect.type >= FirstKundunPieceEffect && effect.type <= LastKundunPieceEffect)
            stepped = StepTheKundunPiece(effect, context);
        break;
    }
    if (!stepped.HasValue())
        return stepped;
    auto tail = CommonTail(effect, skeleton, context.services);
    // D-C4: the draw interpolates from before the whole step.
    effect.previousPosition = before;
    return tail;
}

std::function<std::optional<glm::vec3>(const void*)> ActorPositionOf(const Kernel::WorldState& world)
{
    return [&world](const void* owner) -> std::optional<glm::vec3>
    {
        for (const Kernel::ActorState& actor : world.Actors())
        {
            if (&actor == owner)
                return actor.position;
        }
        return std::nullopt;
    };
}
} // namespace Client::Effects
