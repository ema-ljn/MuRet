#include "Actors/Drawing/GameCharacterEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/CharacterClass.h"
#include "Maps/MapId.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Models/PassColors.h"
#include "Render/SpriteVertex.h"
#include "Kernel/ActorState.h"
#include "Kernel/ActorSpirit.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Assets/ModelIds.h"
#include "Models/Animation.h"
#include "Models/Skeleton.h"
#include "Tables/PlayerBones.h"
#include "Terrain/TerrainCulling.h"
#include "Actors/Drawing/PosePublishing.h"
#include "Actors/EffectHooks.h"
#include "Actors/JointHooks.h"
#include "Actors/FullSetScan.h"
#include "Kernel/DrawTailState.h"
#include "Party/PartyActors.h"
#include "Kernel/SceneId.h"
#include "Social/SocialState.h"
#include "Actors/Drawing/VertexEffects.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldState.h"
#include "Events/ChaosCastle/ChaosCastle.h"
#include "Math/Ftol.h"
#include "Kernel/JointRequest.h"
#include "Particles/ParticleOwner.h"
#include "Particles/Particles.h"
#include "Particles/PortableParticleTrigonometry.h"
#include "Particles/Sprites.h"
#include "Terrain/TerrainHeight.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/WorldTerrain.h"
#include "Audio/SoundId.h"
#include "Kernel/ModelEffectArguments.h"

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

namespace Client::Actors::Drawing
{
namespace
{
using Result = Core::Expected<void, std::string>;

// The sprites' textures (0x61ccb0): flare01, Spark03, lightning2 and Shiny02.
constexpr std::uint16_t Flare = 0x5ac;
constexpr std::uint16_t ThirdSpark = 0x5c6;
constexpr std::uint16_t Lightning = 0x5d5;
constexpr std::uint16_t Shiny = 0x5fd;

constexpr std::uint32_t HiddenAttachmentFlag = 0x4000;

// 0x50ea38..0x50ea94: the mage's special skin, on bone 19 (0x390 / 0x30), shifted (−4, 11, 0).
constexpr std::uint8_t MageSkinBone = 19;
constexpr glm::vec3 MageSkinOffset{-4.0f, 11.0f, 0.0f};
constexpr float MageSparkScale = std::bit_cast<float>(0x3f19999au);  // 0.6
constexpr float MageShinyClock = std::bit_cast<float>(0x3a83126fu);  // 0x6b89c8, 0.001f
constexpr float MageShinyFactor = std::bit_cast<float>(0x3ecccccdu); // 0x6b87b4, 0.4f

// 0x50f828: the actions with effects and class 4 (the Dark Lord).
constexpr std::uint8_t LightningAction = 0x94;
constexpr std::uint8_t FirstEffectAction = 0x51;
constexpr std::uint8_t SecondEffectAction = 0x5b;
using Math::Tenth;                                                       // 0x6b87b8, 0.1f
constexpr float FirstLightningScale = std::bit_cast<float>(0x3e99999au); // 0x6b89dc, 0.3f
constexpr float ThirdLightningScale = 2.5f;                              // 0x6b89a8
constexpr glm::vec3 FirstLightningLight{Tenth, Tenth, 1.0f};
constexpr float FirstWindowStart = std::bit_cast<float>(0x3f99999au); // 0x6b8bb0, 1.2f
constexpr float FirstWindowEnd = std::bit_cast<float>(0x3fcccccdu);   // 0x6b8bfc, 1.6f
constexpr float SecondWindowStart = 7.0f;                             // 0x6b8878
constexpr float SecondWindowEnd = 8.0f;                               // 0x6b86c4
// 0x50f9cf (0x6b8818): below it, the party (0x50f9e0).
constexpr float PartyFrameLimit = 2.0f;

// 0x50fba4: the +0x78 flags with sprites and joints.
constexpr std::uint32_t FirstGlowFlag = 0x4;
constexpr std::uint32_t SecondGlowFlag = 0x2000000;
// 0x50fbe5..0x50fc34: each hand's light, k = (rand() % 30 + 70) · 0.01f and (k, k · 0.3f, k · 0.2f).
constexpr int GlowSpan = 0x1e;
constexpr int GlowBase = 0x46;
constexpr double GlowStep = static_cast<double>(std::bit_cast<float>(0x3c23d70au));  // 0x6b88d4, 0.01f
constexpr double GlowGreen = static_cast<double>(std::bit_cast<float>(0x3e99999au)); // 0x6b89dc, 0.3f
constexpr double GlowBlue = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));  // 0x6b8854, 0.2f
constexpr std::uint16_t GlowSprite = 0x5fd;
constexpr float GlowScale = 1.5f; // 0x3fc00000
// 0x50fa74 and 0x50fa97: the party's beams.
constexpr std::int32_t BeamJoint = 0x618;

// 0x50fcf7..0x50fd5c: the five 0x10f/4 of width 20 (0x41a00000), if it has no 0x10f/4 or /9 of its own (0x534fd0).
constexpr std::int32_t AuraType = 0x10f;
constexpr std::int32_t BindingSubtype = 4;
constexpr std::int32_t RiderBindingSubtype = 9;
constexpr float BindingWidth = 20.0f;
constexpr int BindingRings = 5;

// 0x50fd74: +0x358, every 100 draws with the action below 0xd, effect 0x5ac/3 with the light (0.2f, 0.7f, 0.9f).
constexpr std::uint8_t LastCounterAction = 0xc;
constexpr std::int32_t CounterPeriod = 100;
constexpr glm::vec3 CounterFlareLight{std::bit_cast<float>(0x3e4ccccdu), std::bit_cast<float>(0x3f333333u),
                                      std::bit_cast<float>(0x3f666666u)};

// 0x50fddb: the full set.
constexpr float HandFlareScale = std::bit_cast<float>(0x3fa66666u); // 1.3
constexpr float FullSetShineFactor = 0.5f;
constexpr std::array<std::int32_t, 3> HandBoneDrops{0, 6, 7};
constexpr std::int16_t FirstGlowingBoots = 0x182f;
constexpr std::int16_t LastGlowingBoots = 0x1833;
constexpr std::int32_t LastQuietLevel = 9;
constexpr int GlowChance = 20;

// 0x50ff37..0x5102a4: the boots' particle 0x6f1/3, on bone 0, at (0, ∓18, 50), (0, 0, 70) and (0, ±18, 50); with +13,
// ±20.
constexpr std::int32_t BootsParticle = 0x6f1;
constexpr std::int32_t BootsSubtype = 3;
constexpr std::int32_t BootsBone = 0;
constexpr float BootsSide = 18.0f, WideBootsSide = 20.0f, BootsLow = 50.0f, BootsHigh = 70.0f;

// 0x5102c3..0x5104cb: the 0x60f/0 orbit of scale 0.19f and the set's joints, with the character as owner.
constexpr std::int32_t SparkleType = 0x60f;
constexpr std::int32_t SparkleSubtype = 0, WideSparkleSubtype = 0x12;
constexpr float SparkleOrbitScale = std::bit_cast<float>(0x3e428f5cu); // 0.19f
constexpr float SparkleWidth = 10.0f, WideSparkleWidth = 20.0f;
constexpr std::int32_t TrailType = 0x610, TrailSubtype = 7, TrailKey = 0x28;
constexpr std::int32_t NoKey = -1, NoTarget = -1;
constexpr std::int32_t FirstSkill = 0, SecondSkill = 1;

// 0x50ff5e..0x510002: the light of each boot from 0x182f to 0x1833.
constexpr std::array<glm::vec3, 5> BootsLights{{
    {std::bit_cast<float>(0x3f266666u), std::bit_cast<float>(0x3e99999au), std::bit_cast<float>(0x3dcccccdu)},
    {std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3f666666u)},
    {0.0f, std::bit_cast<float>(0x3ea3d70au), std::bit_cast<float>(0x3e75c28fu)},
    {0.5f, std::bit_cast<float>(0x3e75c28fu), std::bit_cast<float>(0x3f4ccccdu)},
    {std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3ecccccdu), 0.0f},
}};

// 0x510a7a: the maps without 0x510ef0 (Blood Castle, Chaos Castle and the siege).
constexpr std::int32_t FirstBloodCastle = 11;
constexpr std::int32_t LastBloodCastle = 17;
constexpr std::int32_t CastleMap = Maps::CastleSiegeMap;
// 0x51e121: the guild mark follows the owner's head (+0x120 + 0x3c0).
constexpr std::size_t GuildMarkBone = static_cast<std::size_t>(Tables::PlayerHeadBone);

// 0x5ffdf0: below 0.01f (0x6b88d4) it neither poses nor draws.
constexpr float DrawnAlpha = std::bit_cast<float>(0x3c23d70au);
// 0x420e11: with +0x78 & 0x4000 the owner passes 10 to 0x4182f0, which 0x6002e0 takes through 0x60033c.
constexpr std::uint32_t RiderFlag = 0x4000;
// 0x418951: 0x418920 only does something in states 5 and 6.
constexpr std::uint8_t FirstOrderState = 5;
constexpr std::uint8_t SecondOrderState = 6;
// 0x418aae: the attack's joints, with +0x38d up to 2, of rand() % 40 + 20; 0x418b85: effect 0x113, with +0x38d odd from
// 4 and the raven 100 (0x6b86b4) higher than the owner.
constexpr std::uint8_t LastJointStep = 2;
constexpr int JointSpan = 40;
constexpr int JointBase = 20;
constexpr std::uint8_t LastQuietStep = 3;
constexpr double AboveTheOwner = 100.0;
// 0x41896d: the shout, with +0x38d up to 14, a 0x5ac particle on every other bone from rand() & 1, of the first 66;
// with 15, 0x4ee770 on the character at +0x3b0.
constexpr std::uint8_t LastShoutStep = 14;
constexpr std::uint8_t ShoutEnd = 15;
constexpr std::size_t ShoutBones = 66;
constexpr std::uint16_t NoAttackTarget = 0xffff;

using Math::D;
using Math::F32;

// The state of the tail of 0x50ba30 in a draw.
struct Tail
{
    Kernel::ActorState& actor;
    const GameCharacterEffectsPass& pass;
    // [ebp−0x50]: the offset of 0x4ec110, at 0 since 0x50ea23. The mage's skin and the boots' particles write it; the
    // hands of the actions, of the flags and of the set read it (Q-C350).
    glm::vec3 offset{};

    // +0x6c..+0x74 of the 0x20a (GameCharacterEffectsPass::modelOrigin): the position before the shadow's floor.
    [[nodiscard]] glm::vec3 ModelOrigin() const
    {
        return pass.modelOrigin.value_or(actor.position);
    }

    // 0x4ec110 with [ebp−0x14] (the 0x20a: the character's scale and ModelOrigin) on bone `bone` of its own pose
    // (+0x120), in call `call`.
    [[nodiscard]] Core::Expected<glm::vec3, std::string> BodyPoint(std::int32_t bone, std::string_view call) const
    {
        if (bone < 0 || static_cast<std::size_t>(bone) >= pass.body.size())
            return Core::Unexpected(std::format("{}: el hueso {} del cuerpo no existe", call, bone));
        return Math::OriginalTransformPosition(Math::OriginalMatrixOf(pass.body[static_cast<std::size_t>(bone)]),
                                               offset, actor.scale, ModelOrigin());
    }

    // 0x5491e0: the additive sprite; the owner is not stored.
    [[nodiscard]] Result Sprite(std::string_view call, std::uint16_t texture, const glm::vec3& point, float scale,
                                const glm::vec3& light, float rotation = 0.0f) const
    {
        if (pass.sprites == nullptr)
            return Core::Unexpected(std::format("{}: falta la tabla de sprites del frame", call));
        static_cast<void>(pass.sprites->Create(texture, point, scale, light, rotation, 0));
        return {};
    }

    // 0x530860(type, position, position, angles, subtype, character, width, key, skill, 0, −1) on the character. With
    // the joints full it neither creates nor draws rand().
    [[nodiscard]] Result Joint(std::string_view call, std::int32_t type, std::int32_t subtype, float width,
                               std::int32_t key, std::int32_t skill) const
    {
        if (pass.joints.empty() || pass.jointHooks == nullptr)
            return Core::Unexpected(std::format("{}: faltan los joints del frame (0x530860)", call));
        const auto created = pass.jointHooks->CreateTheJoint(
            pass.joints,
            {type, actor.position, actor.position, actor.angles, subtype, &actor, width, key, skill, 0, NoTarget},
            pass.random, Kernel::JointOwnerKind::ByType);
        if (!created.HasValue())
            return Core::Unexpected(std::format("{}: {}", call, created.Error()));
        return {};
    }
};

// 0x50ea38..0x50eb08: the mage's special skin, without rand().
Result MageSkin(Tail& tail)
{
    const Kernel::ActorState& actor = tail.actor;
    if ((actor.characterClass & 7u) != 0 || (actor.skin & 0xf8u) != 8u || (actor.flags78 & HiddenAttachmentFlag) != 0)
        return {};
    tail.offset = MageSkinOffset;
    const auto point = tail.BodyPoint(MageSkinBone, "0x50eab0");
    if (!point.HasValue())
        return Core::Unexpected(point.Error());
    if (auto spark = tail.Sprite("0x50eacd", ThirdSpark, point.Value(), MageSparkScale, glm::vec3(1.0f));
        !spark.HasValue())
        return spark;
    const std::optional<double> shine =
        Math::Detail::SineProductPC53(D(tail.pass.worldClock) * D(MageShinyClock), MageShinyFactor);
    if (!shine)
        return Core::Unexpected<std::string>("0x50eae7: el reloj del mundo está fuera del dominio de FSIN");
    return tail.Sprite("0x50eb00", Shiny, point.Value(), F32(*shine), glm::vec3(1.0f));
}

// 0x50f8e8..0x50f95c: −ftol and ftol of the clock times 0.1f.
float ClockRotation(float clock, bool negated)
{
    const std::int32_t whole = Math::FtolLow(D(clock));
    const std::int32_t turned =
        negated ? static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(whole)) : whole; // NEG
    return F32(static_cast<double>(turned) * D(Tenth));
}

// What follows the actions: the rest of the tail (0x50fba4) or the jump from 0x50f9f8 to 0x510a7a.
enum class AfterTheActions : std::uint8_t
{
    Rest,
    GuildMark,
};

// 0x50f828..0x50fba1: actions 0x94, 0x51 and 0x5b. The packets write them: what creates effects stops with the ASM's
// condition.
Core::Expected<AfterTheActions, std::string> Actions(Tail& tail)
{
    using enum AfterTheActions;
    const Kernel::ActorState& actor = tail.actor;
    const std::uint8_t action = actor.animation.action;
    if (action != LightningAction && action != FirstEffectAction && action != SecondEffectAction)
        return Rest;
    const float frame = actor.animation.frame;
    if (Kernel::BaseClassOf(actor.characterClass) != Kernel::CharacterClass::DarkLord && action == LightningAction)
    {
        if ((actor.flags78 & HiddenAttachmentFlag) != 0)
            return Rest;
        const auto point = tail.BodyPoint(actor.hands[0].bone, "0x50f89b");
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        const float size = F32(D(frame) * D(Tenth));
        if (auto first = tail.Sprite("0x50f8e3", Lightning, point.Value(), F32(D(size) * D(FirstLightningScale)),
                                     FirstLightningLight);
            !first.HasValue())
            return Core::Unexpected(first.Error());
        if (auto second = tail.Sprite("0x50f91f", Lightning, point.Value(), size, actor.light,
                                      ClockRotation(tail.pass.worldClock, true));
            !second.HasValue())
            return Core::Unexpected(second.Error());
        if (auto third = tail.Sprite("0x50f95c", Lightning, point.Value(), F32(D(size) * D(ThirdLightningScale)),
                                     actor.light, ClockRotation(tail.pass.worldClock, false));
            !third.HasValue())
            return Core::Unexpected(third.Error());
        return Rest;
    }
    // 0x50f96c..0x50f9c6: with the frame in [1.2, 1.6) (a NaN does not get in), 0x518cf0(0x621, the position, the
    // angles, the light, 2, the character, −1, 0, 0, 0, 0), the glow that follows its bone 33, and sound 0x1cc
    // (0x41ed10), whether it was created or not. Without rand().
    const bool original = tail.pass.originalFrame;
    if (original && !(frame < FirstWindowStart) && frame < FirstWindowEnd)
    {
        constexpr std::int16_t GlowEffect = 0x621;
        constexpr std::int32_t HeldSubtype = 2;    // 0x50f9a3
        constexpr Audio::SoundId GlowSound{0x1cc}; // 0x50f9bc
        if (tail.pass.world == nullptr)
            return Core::Unexpected(std::format("0x50f9b3: falta el mundo para el 0x621/2 de la acción {:#x}", action));
        if (tail.pass.effectHooks == nullptr)
            return Core::Unexpected(std::format("0x50f9b3: no effect hooks for the 0x621/2 of action {:#x}", action));
        const auto made =
            tail.pass.effectHooks->CreateTheHighEffect({.type = GlowEffect,
                                                        .position = actor.position,
                                                        .angles = actor.angles,
                                                        .light = actor.light,
                                                        .subtype = HeldSubtype,
                                                        .owner = &tail.actor,
                                                        .key = 0xffff},
                                                       *tail.pass.world, tail.pass.random, tail.pass.worldClock);
        if (!made.HasValue())
            return Core::Unexpected(std::format("0x50f9b3: {}", made.Error()));
        tail.pass.world->Runtime().RequestSound(GlowSound);
    }
    // 0x50f9c9: below 2 (also with NaN, like the fcomp) and with a party (0x78aba48 above 0), 0x59e680. Outside the
    // party it jumps to 0x510a7a, without the flags, the counter or the set, also between frames. In the party, two
    // 0x618 joints (/0xc and /0xd, of width 20, BeamJoints.cpp) for each member in view (+0x1c at 0 or more) other than
    // the character, from that member 150 higher, with the character's angles and the character as owner
    // (0x50fa1c..0x50faba).
    if (!(frame >= PartyFrameLimit) && !tail.pass.party.empty())
    {
        if (!Party::IsInTheParty(tail.pass.party, actor))
            return GuildMark;
        for (const Social::PartySlot& member : tail.pass.party)
        {
            if (!original || member.actorIndex < 0 || static_cast<std::size_t>(member.actorIndex) == tail.pass.slot)
                continue;
            const auto index = static_cast<std::size_t>(member.actorIndex);
            if (index >= tail.pass.actors.size())
                return Core::Unexpected(
                    std::format("0x50fa3b: el miembro {} de la party de la acción {:#x} está fuera de los personajes",
                                index, action));
            if (tail.pass.joints.empty() || tail.pass.jointHooks == nullptr)
                return Core::Unexpected(std::string("0x50fa74: faltan los joints del frame (0x530860)"));
            const glm::vec3& at = tail.pass.actors[index].position;
            glm::vec3 from{at.x, at.y, F32(D(at.z) + 150.0)}; // 0x6b87ac
            for (const std::int32_t subtype : {0xc, 0xd})
            {
                const Kernel::JointRequest request{.type = BeamJoint,
                                                   .firstPosition = from,
                                                   .secondPosition = from,
                                                   .angles = tail.actor.angles,
                                                   .subtype = subtype,
                                                   .ownerContext = &tail.actor,
                                                   .scale = 20.0f,
                                                   .key = -1,
                                                   .skill = 0,
                                                   .skillSerial = 0,
                                                   .target = NoTarget};
                const auto created = tail.pass.jointHooks->CreateTheJoint(tail.pass.joints, request, tail.pass.random,
                                                                          Kernel::JointOwnerKind::ByType);
                if (!created.HasValue())
                    return Core::Unexpected(
                        std::format("{}: {}", subtype == 0xc ? "0x50fa74" : "0x50fa97", created.Error()));
            }
        }
    }
    // 0x50fac5..0x50fb9c: in [7, 8) (a NaN does not get in), the hand's point (0x4ec110 with +0x2bd and the offset of
    // [ebp−0x50]) and two 0x518cf0(0x117, point, angles, (0.8f, 0.5f, 1), 2, no owner, −1, 0, 0, 0, 0): the first with
    // angles (180, 45, 0) and the second with (0, 0, the character's z). Without rand() (the /2 does not draw its
    // turn).
    if (original && !(frame < SecondWindowStart) && frame < SecondWindowEnd)
    {
        constexpr std::int16_t DarkLordSkill = 0x117;
        constexpr std::int32_t WideSubtype = 2;                                        // 0x50fb2a
        constexpr glm::vec3 SkillLight{std::bit_cast<float>(0x3f4ccccdu), 0.5f, 1.0f}; // 0x50fb3a..0x50fb48
        constexpr glm::vec3 FirstAngles{180.0f, 45.0f, 0.0f};                          // 0x50fb4f..0x50fb5d
        if (tail.pass.world == nullptr)
            return Core::Unexpected(
                std::format("0x50fb64: falta el mundo para los 0x117/2 de la acción {:#x}", action));
        if (tail.pass.effectHooks == nullptr)
            return Core::Unexpected(std::format("0x50fb64: no effect hooks for the 0x117/2 of action {:#x}", action));
        const auto point = tail.BodyPoint(actor.hands[0].bone, "0x50fb16");
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        const std::array<std::pair<glm::vec3, std::string_view>, 2> skills{
            {{FirstAngles, "0x50fb64"}, {glm::vec3{0.0f, 0.0f, actor.angles.z}, "0x50fb9c"}}};
        for (const auto& [angles, call] : skills)
        {
            const auto made =
                tail.pass.effectHooks->CreateTheHighEffect({.type = DarkLordSkill,
                                                            .position = point.Value(),
                                                            .angles = angles,
                                                            .light = SkillLight,
                                                            .subtype = WideSubtype,
                                                            .owner = nullptr,
                                                            .key = 0xffff},
                                                           *tail.pass.world, tail.pass.random, tail.pass.worldClock);
            if (!made.HasValue())
                return Core::Unexpected(std::format("{}: {}", call, made.Error()));
        }
    }
    return Rest;
}

// 0x50fba4..0x50fd5e: with 0x4 or 0x2000000 in +0x78 and without 0x4000, for each hand (+0x2bd and +0x2dd) a rand()
// gives the light and three 0x5fd sprites of scale 1.5 on bones hand, hand − 6 and hand − 7 (0x4ec110 with [ebp−0x50]);
// then, if it has no 0x10f/4 or /9 joint of its own (0x534fd0), 0x530860(0x10f, +0x10, +0x10, +0x1c, 4, o, 20, −1, 0,
// 0, the signed +0x3a4) five times. Between frames, only the sprites (with the copy's rand()).
Result Flags(Tail& tail)
{
    Kernel::ActorState& actor = tail.actor;
    const std::uint32_t flags = actor.flags78;
    const bool glows = (flags & FirstGlowFlag) != 0 || (flags & SecondGlowFlag) != 0;
    if (!glows || (flags & HiddenAttachmentFlag) != 0)
        return {};
    for (const Kernel::ActorItemPart& hand : actor.hands)
    {
        const double k = static_cast<double>(tail.pass.random.Next() % GlowSpan + GlowBase) * GlowStep;
        const glm::vec3 light{F32(k), F32(k * GlowGreen), F32(k * GlowBlue)};
        constexpr std::array<std::string_view, 3> BoneCalls{"0x50fc37", "0x50fc7d", "0x50fcc3"};
        constexpr std::array<std::string_view, 3> SpriteCalls{"0x50fc53", "0x50fc99", "0x50fcdf"};
        for (std::size_t i = 0; i < HandBoneDrops.size(); ++i)
        {
            const auto point = tail.BodyPoint(static_cast<std::int32_t>(hand.bone) - HandBoneDrops[i], BoneCalls[i]);
            if (!point.HasValue())
                return Core::Unexpected(point.Error());
            if (auto sprite = tail.Sprite(SpriteCalls[i], GlowSprite, point.Value(), GlowScale, light);
                !sprite.HasValue())
                return sprite;
        }
    }
    if (!tail.pass.originalFrame)
        return {};
    if (tail.pass.joints.empty() || tail.pass.jointHooks == nullptr)
        return Core::Unexpected(std::string("0x50fcff: faltan los joints del frame (0x534fd0)"));
    const bool bound =
        std::ranges::any_of(tail.pass.joints,
                            [&actor](const Kernel::JointState& joint)
                            {
                                return joint.alive && joint.type == AuraType && joint.ownerContext == &actor &&
                                       (joint.subtype == BindingSubtype || joint.subtype == RiderBindingSubtype);
                            });
    if (bound)
        return {};
    for (int ring = 0; ring < BindingRings; ++ring)
    {
        const Kernel::JointRequest request{.type = AuraType,
                                           .firstPosition = actor.position,
                                           .secondPosition = actor.position,
                                           .angles = actor.angles,
                                           .subtype = BindingSubtype,
                                           .ownerContext = &actor,
                                           .scale = BindingWidth,
                                           .key = -1,
                                           .skill = 0,
                                           .skillSerial = 0,
                                           .target = std::bit_cast<std::int16_t>(actor.attackTarget)};
        if (auto created = tail.pass.jointHooks->CreateTheJoint(tail.pass.joints, request, tail.pass.random,
                                                                Kernel::JointOwnerKind::Actor);
            !created.HasValue())
            return Core::Unexpected(std::format("0x50fd4d: {}", created.Error()));
    }
    return {};
}

// 0x50fd74..0x50fdd5: with +0x358, the counter at +0x35c and, with the action below 0xd and 100 or more, effect 0x5ac/3
// on the character, with it as owner, and the counter at 0. With the table full it does not create.
Result Counter(const Tail& tail)
{
    Kernel::ActorState& actor = tail.actor;
    if (actor.byte358 == 0)
        return {};
    if (actor.animation.action <= LastCounterAction && actor.counter35c >= CounterPeriod)
    {
        if (tail.pass.originalFrame)
        {
            if (tail.pass.effects == nullptr)
                return Core::Unexpected<std::string>("0x50fdc3: falta la tabla de efectos con modelo del frame");
            if (tail.pass.effectHooks == nullptr)
                return Core::Unexpected<std::string>("0x50fdc3: no effect hooks for the counter's 0x5ac/3");
            static_cast<void>(tail.pass.effectHooks->CreateTheFullSetFlare(*tail.pass.effects, actor.position,
                                                                           actor.angles, CounterFlareLight, &actor));
        }
        actor.counter35c = 0;
    }
    ++actor.counter35c;
    return {};
}

// 0x608e00(boots, alpha, 0.5, color, 0): k = alpha · 0.5 on the stack and each component k times the model's, with an
// fstp; those the case sets to 0, a literal 0.
glm::vec3 FullSetColor(std::uint16_t boots, float alpha)
{
    const Render::Color shine = Models::ShineColorOf(boots);
    const double k = D(alpha) * D(FullSetShineFactor);
    const auto times = [k](float component) { return component == 0.0f ? 0.0f : F32(k * D(component)); };
    return {times(shine.red), times(shine.green), times(shine.blue)};
}

// The own light (+0xf4) changed until the end of the block (0x5102ac and 0x5104b0).
class SavedLight
{
public:
    SavedLight(Kernel::ActorState& actor, const glm::vec3& light) : m_actor(actor), m_saved(actor.light)
    {
        actor.light = light;
    }
    SavedLight(const SavedLight&) = delete;
    SavedLight& operator=(const SavedLight&) = delete;
    ~SavedLight()
    {
        m_actor.light = m_saved;
    }

private:
    Kernel::ActorState& m_actor;
    glm::vec3 m_saved;
};

// 0x53fe70(0x6f1, point, angles, own light, 3, 1, 0) on bone 0 with offset `offset`, which stays in [ebp−0x50]
// (Q-C350). With the particles full it neither creates nor draws rand().
Result BootsMist(Tail& tail, const glm::vec3& offset, std::string_view call)
{
    tail.offset = offset;
    const auto point = tail.BodyPoint(BootsBone, call);
    if (!point.HasValue())
        return Core::Unexpected(point.Error());
    if (tail.pass.particles == nullptr)
        return Core::Unexpected(std::format("{}: falta el pool de partículas del frame", call));
    Kernel::ActorState& actor = tail.actor;
    const auto created = tail.pass.particles->CreateKalimaChecked(
        BootsParticle, point.Value(), actor.angles, actor.light, BootsSubtype, 1.0f, tail.pass.worldClock,
        tail.pass.random, static_cast<std::int32_t>(Kernel::SceneId::Game));
    if (!created.HasValue())
        return Core::Unexpected(std::format("{}: la partícula {:#x}/{} falla con el error {}", call, BootsParticle,
                                            BootsSubtype, static_cast<unsigned>(created.Error())));
    return {};
}

// 0x50ff37..0x5102bd: the boots from 0x182f to 0x1833 with their light at +0xf4 (saved and restored). With +10, rand()
// & 3 at 0; with +11, rand() % 3 at 0; with +12, rand() & 1 at 0; with +13, always: three 0x6f1/3 particles on bone 0.
// Between frames, only the rand() that chooses.
Result GlowingBoots(Tail& tail, std::int16_t boots, std::int32_t level)
{
    Kernel::ActorState& actor = tail.actor;
    const SavedLight light(actor, BootsLights[static_cast<std::size_t>(boots - FirstGlowingBoots)]);
    Crt::CrtRandom& random = tail.pass.random;
    bool emits = false;
    float side = BootsSide;
    std::array<std::string_view, 3> calls{"0x510158", "0x5101a4", "0x5102a4"};
    switch (level)
    {
    case 10:
        emits = (random.Next() & 3) == 0;
        break;
    case 11:
        emits = random.Next() % 3 == 0;
        calls = {"0x51008d", "0x5100d9", "0x5102a4"};
        break;
    case 12:
        emits = (random.Next() & 1) == 0;
        break;
    case 13:
        emits = true;
        side = WideBootsSide;
        calls = {"0x51020c", "0x510258", "0x5102a4"};
        break;
    default:
        break;
    }
    if (!emits || !tail.pass.originalFrame)
        return {};
    const std::array<glm::vec3, 3> offsets{{{0.0f, -side, BootsLow}, {0.0f, 0.0f, BootsHigh}, {0.0f, side, BootsLow}}};
    for (std::size_t i = 0; i < offsets.size(); ++i)
    {
        if (auto made = BootsMist(tail, offsets[i], calls[i]); !made.HasValue())
            return made;
    }
    return {};
}

// 0x53fe70(0x60f, position, angles, own light, 0, 0.19f, character): the set's orbit.
Result SparkleOrbit(const Tail& tail, std::string_view call)
{
    if (tail.pass.particles == nullptr)
        return Core::Unexpected(std::format("{}: falta el pool de partículas del frame", call));
    Kernel::ActorState& actor = tail.actor;
    Particles::PortableParticleTrigonometry trigonometry;
    const auto created = tail.pass.particles->CreateChecked(
        SparkleType, actor.position, actor.angles, actor.light, SparkleSubtype, SparkleOrbitScale,
        std::cref(tail.pass.worldClock), tail.pass.random, 0, trigonometry,
        Particles::ParticleOwner{Particles::ParticleOwnerKind::Actor, tail.pass.slot});
    if (!created.HasValue())
        return Core::Unexpected(std::format("{}: la partícula {:#x}/{} falla con el error {}", call, SparkleType,
                                            SparkleSubtype, static_cast<unsigned>(created.Error())));
    return {};
}

// The two 0x60f/0x12 of width 20, with skill 0 and 1 (0x5103be and 0x5103db, 0x510435 and 0x510452).
Result WideSparkles(const Tail& tail, std::string_view first, std::string_view second)
{
    if (auto made = tail.Joint(first, SparkleType, WideSparkleSubtype, WideSparkleWidth, NoKey, FirstSkill);
        !made.HasValue())
        return made;
    return tail.Joint(second, SparkleType, WideSparkleSubtype, WideSparkleWidth, NoKey, SecondSkill);
}

// 0x5102c3..0x5104cb: with the level above 9 and rand() % 20 at 0, the own light at 1 (saved and restored). With +10,
// the orbit; with +11, rand() & 7 chooses the orbit or the 0x60f/0 of width 10; with +12, every 6 logic steps the two
// 0x60f/0x12 and otherwise every 3 the orbit; with +13, every 6 the two 0x60f/0x12 and, separately, every 4 the orbit
// and the 0x610/7 (Q-C381). Between frames, only the rand() that choose. The original reads 0x7d3a0f0 without opening
// the access, with the encrypted garbage of the last close (Q-C125); as with the raven's shout, MuRet uses the steps
// (P-C22 a).
Result Sparkles(const Tail& tail, std::int32_t level)
{
    if (tail.pass.random.Next() % GlowChance != 0)
        return {};
    Kernel::ActorState& actor = tail.actor;
    const SavedLight light(actor, glm::vec3(1.0f));
    const std::int32_t steps = tail.pass.logicSteps;
    const bool original = tail.pass.originalFrame;
    switch (level)
    {
    case 10:
        return original ? SparkleOrbit(tail, "0x510339") : Result{};
    case 11:
        if ((tail.pass.random.Next() & 7) != 0)
            return original ? SparkleOrbit(tail, "0x510339") : Result{};
        return original ? tail.Joint("0x51037c", SparkleType, SparkleSubtype, SparkleWidth, NoKey, FirstSkill)
                        : Result{};
    case 12:
        if (!original)
            return {};
        if (steps % 6 == 0)
            return WideSparkles(tail, "0x5103be", "0x5103db");
        if (steps % 3 == 0)
            return SparkleOrbit(tail, "0x510339");
        return {};
    case 13:
        if (!original)
            return {};
        if (steps % 6 == 0)
        {
            if (auto made = WideSparkles(tail, "0x510435", "0x510452"); !made.HasValue())
                return made;
        }
        if (steps % 4 == 0)
        {
            if (auto made = SparkleOrbit(tail, "0x51048b"); !made.HasValue())
                return made;
            return tail.Joint("0x5104a8", TrailType, TrailSubtype, WideSparkleWidth, TrailKey, SecondSkill);
        }
        return {};
    default:
        return {};
    }
}

// 0x50fddb..0x5104cb: the full set of 0x505820.
Result FullSet(Tail& tail)
{
    const Kernel::ActorState& actor = tail.actor;
    const std::uint16_t bootsModel = actor.body.back().model;
    const glm::vec3 color = FullSetColor(bootsModel, actor.alpha);
    if ((actor.flags78 & HiddenAttachmentFlag) == 0)
    {
        for (const Kernel::ActorItemPart& hand : actor.hands)
        {
            for (const std::int32_t drop : HandBoneDrops)
            {
                const auto point = tail.BodyPoint(static_cast<std::int32_t>(hand.bone) - drop, "0x50fe4c");
                if (!point.HasValue())
                    return Core::Unexpected(point.Error());
                if (auto sprite = tail.Sprite("0x50fe68", Flare, point.Value(), HandFlareScale, color);
                    !sprite.HasValue())
                    return sprite;
            }
        }
    }
    const std::int32_t level = tail.pass.scan.globals.dword743a178;
    if (level <= LastQuietLevel)
        return {};
    const auto boots = static_cast<std::int16_t>(bootsModel);
    if (boots >= FirstGlowingBoots && boots <= LastGlowingBoots)
    {
        if (auto glow = GlowingBoots(tail, boots, level); !glow.HasValue())
            return glow;
    }
    return Sparkles(tail, level);
}

// 0x510a7a..0x510ab6 and 0x510ef0: in scene 5 (the only caller), with +0x88 at 1 and outside maps 11 to 23 and 30,
// +0x369 at 1 creates effect 0x140 (Scenes/GuildMarkEffects) if +0x36c is at 0 or more than 5000 ms passed, and +0x36c
// becomes the clock (Q-C352). Only in an original frame: between frames, the character is a copy.
Result GuildMark(const Tail& tail)
{
    Kernel::ActorState& actor = tail.actor;
    const std::int32_t map = tail.pass.map;
    // 0x510a7a: only in scene 5.
    if (!tail.pass.gameScene || actor.byte88 != 1 || (map >= FirstBloodCastle && map <= LastBloodCastle) ||
        Maps::IsChaosCastle(map) || map == CastleMap)
        return {};
    if (!tail.pass.originalFrame)
        return {};
    if (tail.pass.effectHooks == nullptr)
        return Core::Unexpected<std::string>("0x510f00: no effect hooks for the guild mark");
    if (!tail.pass.effectHooks->IsTheGuildMarkDue(actor, tail.pass.worldClock))
        return {};
    if (tail.pass.effects == nullptr || tail.pass.particles == nullptr)
        return Core::Unexpected<std::string>("0x510f4d: el efecto 0x140 pide la tabla de efectos y las partículas");
    // 0x51e121: 0x4ec110 with bone 20 of the own bones (+0x120) and the scale and position 0x6001b0 copied to the model
    // (before the shadow's floor), without the offset of [ebp−0x50] (that of 0x518cf0 is its own, at 0).
    if (GuildMarkBone >= tail.pass.body.size())
        return Core::Unexpected(std::format("0x51e121: el hueso {} del cuerpo no existe", GuildMarkBone));
    const glm::vec3 point = Math::OriginalTransformPosition(Math::OriginalMatrixOf(tail.pass.body[GuildMarkBone]),
                                                            glm::vec3(0.0f), actor.scale, tail.ModelOrigin());
    if (auto raised = tail.pass.effectHooks->RaiseTheGuildMark(actor, point, *tail.pass.effects, *tail.pass.particles,
                                                               tail.pass.random, tail.pass.worldClock);
        !raised.HasValue())
        return Core::Unexpected(std::format("0x510f4d: {}", raised.Error()));
    return {};
}
// 0x418301: the trapezoid with the raven's place times 0.01f (0x6b88d4) and −20 (0xc1a00000); without passing it,
// the speed at 3 (0x40400000) except in state 4. The raven's animation goes at 0.4 (DrawDarkSpirit).
constexpr double RavenCellsPerUnit = static_cast<double>(0.01f);
constexpr double RavenViewMargin = -20.0;
constexpr float RavenUnseenSpeed = 3.0f;
constexpr std::uint8_t RavenLandingState = 4;
constexpr float RavenFrameStep = 0.4f;
} // namespace

Core::Expected<void, std::string> DrawGameCharacterEffects(Kernel::ActorState& actor,
                                                           const GameCharacterEffectsPass& pass)
{
    Tail tail{.actor = actor, .pass = pass};
    if (auto drawn = MageSkin(tail); !drawn.HasValue())
        return drawn;
    const auto acted = Actions(tail);
    if (!acted.HasValue())
        return Core::Unexpected(acted.Error());
    if (acted.Value() == AfterTheActions::GuildMark)
        return GuildMark(tail);
    if (auto drawn = Flags(tail); !drawn.HasValue())
        return drawn;
    // 0x50fd61: Chaos Castle jumps to the end, without the counter or the set.
    if (!Maps::IsChaosCastle(pass.map))
    {
        if (auto counted = Counter(tail); !counted.HasValue())
            return counted;
        if (pass.scan.complete)
        {
            if (auto drawn = FullSet(tail); !drawn.HasValue())
                return drawn;
        }
    }
    return GuildMark(tail);
}

// 0x418920(CHARACTER, CHARACTER), from 0x41835a after 0x602e90, with the raven in the trapezoid: in states 5 and 6, the
// attack's effects. In 5, with +0x38d up to 2, ten 0x5ac/1 joints from the raven and, with 1, the 0x117/3; odd from 4
// on and high, the beak's 0x113/0, in the tables of `world`. The shout of 6 releases the 0x5ac/6 particles (0x418a26).
// Without the pose (+0x120 at 0 until the first draw), 6 does nothing. The end of the shout (+0x38d at 15) is
// EndTheShout.
Core::Expected<void, std::string> DrawTheRavenAttack(Kernel::DarkSpirit& spirit, const Kernel::ActorState& owner,
                                                     const Models::Skeleton* skeleton, Crt::CrtRandom& random,
                                                     Particles::Particles* particles, float worldClock,
                                                     Kernel::WorldState* world, const IJointHooks* jointHooks,
                                                     const IEffectHooks* effectHooks)
{
    if (spirit.state == FirstOrderState)
    {
        if (spirit.byte38d <= LastJointStep)
        {
            // 0x418aab..0x418b08: with the target of +8, which the A8 set before state 5, ten 0x530860(0x5ac, the
            // raven's position twice, its angles, 1, no owner, rand() % 40 + 20, −1, 0, 0, −1).
            constexpr int AttackJoints = 10;               // 0x418ac9
            constexpr std::int32_t AttackJoint = 0x5ac;    // 0x418afa
            constexpr std::int32_t AttackJointSubtype = 1; // 0x418af5
            for (int made = 0; made < AttackJoints; ++made)
            {
                const auto width = static_cast<float>(static_cast<std::int32_t>(random.Next() % JointSpan) + JointBase);
                // Without the table, it stops at the first 0x530860, after the rand() of 0x418ad6.
                if (world == nullptr || jointHooks == nullptr)
                    return Core::Unexpected(std::string("0x418aff: faltan los joints del mundo (0x530860) para el "
                                                        "ataque del cuervo"));
                const Kernel::JointRequest request{.type = AttackJoint,
                                                   .firstPosition = spirit.position,
                                                   .secondPosition = spirit.position,
                                                   .angles = spirit.angles,
                                                   .subtype = AttackJointSubtype,
                                                   .ownerContext = nullptr,
                                                   .scale = width,
                                                   .key = -1,
                                                   .skill = 0,
                                                   .skillSerial = 0,
                                                   .target = -1};
                const auto created = jointHooks->CreateTheJoint(world->DrawTail().joints, request, random,
                                                                Kernel::JointOwnerKind::ByType);
                if (!created.HasValue())
                    return Core::Unexpected(std::format("0x418aff: {}", created.Error()));
            }
            // 0x418b0a..0x418b71: with +0x38d at 1, 0x518cf0(0x117, the position, (45, rand() % 180 − 90, 0), (1, 0.8f,
            // 0.6f), 3, no owner, −1, 0, 0, 0, 0).
            constexpr std::uint8_t SkillStep = 1;
            if (spirit.byte38d == SkillStep)
            {
                constexpr std::int16_t DarkLordSkill = 0x117;
                constexpr std::int32_t SkillSubtype = 3;
                constexpr std::int32_t TurnSpan = 180;                    // 0x418b23: 0xb4
                constexpr double QuarterTurn = Math::QuarterTurnAsDouble; // 0x6b8948
                constexpr float Pitch = 45.0f;                            // 0x418b16: 0x42340000
                constexpr glm::vec3 SkillLight{1.0f, std::bit_cast<float>(0x3f4ccccdu),
                                               std::bit_cast<float>(0x3f19999au)}; // 0x418b42..0x418b50
                const auto turn = static_cast<std::int32_t>(random.Next() % TurnSpan);
                const glm::vec3 angles{Pitch, F32(static_cast<double>(turn) - QuarterTurn), 0.0f};
                if (effectHooks == nullptr)
                    return Core::Unexpected(std::string("0x418b71: no effect hooks for the raven's 0x117/3"));
                const auto made = effectHooks->CreateTheHighEffect({.type = DarkLordSkill,
                                                                    .position = spirit.position,
                                                                    .angles = angles,
                                                                    .light = SkillLight,
                                                                    .subtype = SkillSubtype,
                                                                    .owner = nullptr,
                                                                    .key = 0xffff},
                                                                   *world, random, worldClock);
                if (!made.HasValue())
                    return Core::Unexpected(std::format("0x418b71: {}", made.Error()));
            }
        }
        // 0x418b7c..0x418c0a: odd from 4 on and 100 above the owner, 0x4ec110(+0x120 + 6 · 0x30, (50, 0, 0), point, 0),
        // bone 6 in the world, and 0x518cf0(0x113, point, +0x1c, +0xf4, 0, the raven, −1, 0, 0, 0, 0). It draws no
        // rand(). 0x418ba6..0x418bb7: FCOMP and TEST AH, 1 go on with C0, which a NaN also sets.
        if (spirit.byte38d > LastQuietStep && (spirit.byte38d & 1u) != 0 &&
            !(static_cast<double>(owner.position.z) + AboveTheOwner >= static_cast<double>(spirit.position.z)))
        {
            constexpr std::size_t BeakBone = 6;                // 0x418bc8: +0x120 plus 0x120
            constexpr glm::vec3 BeakOffset{50.0f, 0.0f, 0.0f}; // 0x418bcf: 0x42480000
            constexpr std::int16_t AirForce = 0x113;
            if (BeakBone >= spirit.pose.size())
                return Core::Unexpected(
                    std::format("0x418be4: el hueso {} del cuervo pasa los de su pose (+0x120)", BeakBone));
            if (world == nullptr)
                return Core::Unexpected(std::string("0x418c0a: falta la tabla de efectos (0x518cf0) para el ataque "
                                                    "del cuervo"));
            if (effectHooks == nullptr)
                return Core::Unexpected(std::string("0x418c0a: no effect hooks for the raven's 0x113"));
            const glm::vec3 point = Math::OriginalVectorTransform(BeakOffset, spirit.pose[BeakBone]);
            const auto made = effectHooks->CreateTheBlastEffect(world->Map().effects,
                                                                {.type = AirForce,
                                                                 .position = point,
                                                                 .angles = spirit.angles,
                                                                 .light = spirit.light,
                                                                 .subtype = 0,
                                                                 .owner = &spirit,
                                                                 .key = 0xffff},
                                                                random);
            if (!made.HasValue())
                return Core::Unexpected(std::format("0x418c0a: {}", made.Error()));
        }
        return {};
    }
    if (spirit.byte38d <= LastShoutStep)
    {
        if (spirit.pose.empty())
            return {};
        if (skeleton == nullptr)
            return Core::Unexpected(std::string("0x4189e2: falta el modelo 0xc0 del cuervo para el grito"));
        // 0x4189ad..0x418a46: signed rand() & 1 (rand() is not negative), and the bones two by two that are neither
        // dummies nor past the model's: 0x4ec110(+0x120 + bone · 0x30, (0, 0, 0), point, 0), the bone in the world, and
        // 0x53fe70(0x5ac, point, raven+0x1c, (1, 0.6f, 0.4f), 6, 1.3f, 0). With the pool full, none, and it goes on.
        constexpr glm::vec3 ShoutLight{1.0f, std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3ecccccdu)};
        constexpr std::int32_t ShoutParticle = 0x5ac, ShoutSubtype = 6;
        constexpr float ShoutScale = std::bit_cast<float>(0x3fa66666u); // 1.3f
        const auto first = static_cast<std::size_t>(random.Next() & 1);
        for (std::size_t bone = first; bone < ShoutBones; bone += 2)
        {
            if (bone >= skeleton->bones.size() || skeleton->bones[bone].dummy)
                continue;
            if (bone >= spirit.pose.size())
                return Core::Unexpected(
                    std::format("0x418a07: el hueso {} del grito del cuervo pasa los de su pose (+0x120)", bone));
            if (particles == nullptr)
                return Core::Unexpected(std::string("0x418a26: faltan las partículas del grito del cuervo (0x53fe70)"));
            const glm::vec3 point = Math::OriginalVectorTransform(glm::vec3(0.0f), spirit.pose[bone]);
            const auto created = particles->CreateChecked(ShoutParticle, point, spirit.angles, ShoutLight, ShoutSubtype,
                                                          ShoutScale, worldClock, random);
            if (!created.HasValue())
                return Core::Unexpected(std::format("0x418a26: la partícula 0x5ac/6 del hueso {} del cuervo falla con "
                                                    "el error {}",
                                                    bone, static_cast<unsigned>(created.Error())));
        }
        return {};
    }
    return {};
}

// 0x418a51..0x418a9b: the end of the shout (state 6 with +0x38d at 15). With the character of +0x3b0 (MOVSX; not
// 0xffff), 0x4ee770(1, 0x5ac, 1, raven+0x1c, the character) with the raven's model (ECX, 0x5769ad8 + 0xc0 · 0xe0) on
// mesh 1 of the scratch its 0x602e90 left in this draw (Actors/Drawing/VertexEffects.h: the 0x613/0xd joints from the
// vertices toward the character, with it as owner) and 0x4164f0(0), the state and the distance counter at 0. Always,
// +0x38d at 0. `meshes` is the raven's scratch if it was drawn (`drawn`); otherwise 0x4ee770 would read the last posed
// model's, and it stops.
Core::Expected<void, std::string> EndTheShout(Kernel::DarkSpirit& spirit, std::span<const Models::SkinnedMesh> meshes,
                                              bool drawn, const GameRavenPass& pass)
{
    constexpr std::int32_t ShoutMesh = 1, ShoutEffects = 0x5ac, ShoutJoints = 1;
    constexpr std::uint8_t Released = 0;
    if (spirit.target3b0 != NoAttackTarget)
    {
        if (pass.world == nullptr)
            return Core::Unexpected(std::string("0x418a8c: faltan los personajes y los joints del mundo para el final "
                                                "del grito del cuervo"));
        const auto actors = pass.world->Actors();
        const auto slot = static_cast<std::int16_t>(spirit.target3b0);
        if (slot < 0 || static_cast<std::size_t>(slot) >= actors.size())
            return Core::Unexpected(
                std::format("0x418a77: el personaje {} de +0x3b0 del grito del cuervo no está en el pool", slot));
        if (!drawn)
            return Core::Unexpected(std::string("0x418a8c: sin el cuervo dibujado en este frame, 0x4ee770 lee el "
                                                "scratch (0x5786f9c) del último modelo posado"));
        if (const Data::Models::Model* model = pass.models.Find(Models::ModelId{Assets::DarkSpiritModel});
            model != nullptr)
        {
            const VertexEffectCall call{.mesh = ShoutMesh,
                                        .type = ShoutEffects,
                                        .subtype = ShoutJoints,
                                        .angles = spirit.angles,
                                        .target = &actors[static_cast<std::size_t>(slot)]};
            if (auto emitted = EmitVertexEffectsOf(
                    *model, spirit.scale, meshes, call,
                    {.joints = pass.world->DrawTail().joints, .jointHooks = pass.jointHooks}, *pass.random);
                !emitted.HasValue())
                return Core::Unexpected(std::format("0x418a8c: {}", emitted.Error()));
        }
        // 0x418a91: 0x4164f0(0) on the raven.
        spirit.state = Released;
        spirit.far = 0;
    }
    spirit.byte38d = 0;
    return {};
}

bool DarkSpiritSeen(const Kernel::DarkSpirit& spirit, const Terrain::TerrainPolygon& polygon)
{
    const auto cells = [](float coordinate)
    { return static_cast<double>(F32(static_cast<double>(coordinate) * RavenCellsPerUnit)); };
    return Terrain::PolygonContains(polygon, cells(spirit.position.x), cells(spirit.position.y), RavenViewMargin);
}

void MissTheView(Kernel::DarkSpirit& spirit)
{
    spirit.speed = RavenUnseenSpeed;
    if (spirit.state != RavenLandingState)
    {
        spirit.state = 0;
    }
}

DrawnObject DrawnDarkSpirit(const Kernel::DarkSpirit& spirit, const Models::Skeleton* skeleton,
                            const glm::vec3& terrainLight, float stepAlpha)
{
    DrawnObject drawn;
    drawn.model = Assets::DarkSpiritModel;
    drawn.animation = spirit.animation;
    if (skeleton != nullptr && spirit.animation.action < skeleton->actions.size())
    {
        drawn.animation = Models::Interpolated(spirit.animation, skeleton->actions[spirit.animation.action],
                                               RavenFrameStep, stepAlpha);
    }
    drawn.transform.position = spirit.position + ((spirit.position - spirit.previousPosition) * stepAlpha);
    drawn.transform.angles = spirit.angles;
    drawn.transform.scale = spirit.scale;
    drawn.color = terrainLight + spirit.light;
    drawn.alpha = spirit.alpha;
    drawn.vertexLight = true;
    return drawn;
}

Core::Expected<void, std::string> DrawGameRaven(Kernel::ActorState& owner, const GameRavenPass& pass)
{
    if (!owner.darkSpirit)
        return {};
    Kernel::DarkSpirit& spirit = *owner.darkSpirit;
    if (!DarkSpiritSeen(spirit, pass.polygon))
    {
        if (pass.originalFrame)
            MissTheView(spirit);
        return {};
    }
    // 0x5ffdff..0x5ffe3b: in maps 18 to 23, with the hero's +0x1a1, the alpha drops by 0.15f and stays stored.
    if (pass.originalFrame)
        spirit.alpha = Events::ChaosCastle::ChaosCastlePoseAlpha(pass.map, pass.heroByte1a1, spirit.alpha);
    // If 0x602e90 posed the raven in this draw: its scratch (0x5786f9c) is the pose buffer's.
    bool ravenPosed = false;
    if (spirit.alpha >= DrawnAlpha)
    {
        if (pass.lighting == nullptr)
            return Core::Unexpected<std::string>(
                "0x5ffce0: falta el buffer dinámico de luz del terreno para el cuervo");
        // 0x420e00..0x420e11: with 0x4000 in the owner's +0x78, 0x4182f0(10) and 0x602e90(raven, 0, 0, 10).
        const bool shell = (owner.flags78 & RiderFlag) != 0;
        if (!shell && (!pass.terrain.heights || !pass.terrain.attributes))
            return Core::Unexpected<std::string>("0x600a0b: faltan las alturas del terreno para la sombra del cuervo");
        const Models::Skeleton* skeleton = pass.models.SkeletonOf(Models::ModelId{Assets::DarkSpiritModel});
        const glm::vec3 light = Terrain::LightAt(*pass.lighting, spirit.position.x, spirit.position.y);
        // The game's characters are drawn as the step left them: the raven too (`stepAlpha` at 0).
        DrawnObject drawn = DrawnDarkSpirit(spirit, skeleton, light, 0.0f);
        if (pass.carry != nullptr)
        {
            if (auto posed = PublishPose(*pass.carry, pass.models, drawn, true); !posed.HasValue())
                return posed;
            drawn.retainedMeshes = pass.carry->meshes;
        }
        // +0x120: the pose of 0x5ffdf0, before whichever path of 0x6002e0 follows.
        if (pass.originalFrame)
            spirit.pose = PosedWorldBones(pass.models, drawn);
        ravenPosed = true;
        if (shell)
        {
            // 0x60033c: the raven's +0x108 is left at 0 by the rep stos of 0x415c1e and nobody writes it, so the siege
            // loop (0x600343..0x600399) does not find it in the pool: always DrawChromeShell, without the shadow or the
            // terrain height, and the shared model stays in its place (0x5ffdf0), not on the floor.
            if (auto drawnShell = DrawChromeShell(pass.batch, pass.models, drawn, pass.frameClock);
                !drawnShell.HasValue())
                return drawnShell;
        }
        else
        {
            const float ground =
                F32(Terrain::TerrainHeightAt(*pass.terrain.heights, *pass.terrain.attributes,
                                             drawn.transform.position.x, drawn.transform.position.y, pass.fixedHeight));
            DrawDarkSpirit(pass.batch, pass.models, drawn, ground, pass.frameClock, pass.map);
            if (pass.carry != nullptr)
                pass.carry->models[drawn.model].position = {drawn.transform.position.x, drawn.transform.position.y,
                                                            ground};
        }
    }
    // 0x41835a: 0x418920, in the original's frames.
    if (pass.originalFrame && (spirit.state == FirstOrderState || spirit.state == SecondOrderState))
    {
        if (pass.random == nullptr)
            return Core::Unexpected(std::string("0x41835a: falta el rand() para el ataque del cuervo"));
        if (spirit.state == SecondOrderState && spirit.byte38d == ShoutEnd)
        {
            std::span<const Models::SkinnedMesh> scratch{};
            if (pass.carry != nullptr)
                scratch = pass.carry->meshes;
            return EndTheShout(spirit, scratch, ravenPosed, pass);
        }
        return DrawTheRavenAttack(spirit, owner, pass.models.SkeletonOf(Models::ModelId{Assets::DarkSpiritModel}),
                                  *pass.random, pass.particles, pass.worldClock, pass.world, pass.jointHooks,
                                  pass.effectHooks);
    }
    return {};
}
} // namespace Client::Actors::Drawing
