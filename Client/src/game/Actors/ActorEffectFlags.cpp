#include "Actors/ActorEffectFlags.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Tables/PlayerModels.h"
#include "Maps/MapId.h"
#include "Math/Angles.h"
#include "Models/Cloth.h"
#include "Actors/ActorFlags.h"
#include "Kernel/ActorState.h"
#include "Camera/GameCamera.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/JointFamilies.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/WizardModelEffects.h"
#include "Tables/TextTable.h"
#include "Scenes/GameMessages.h"
#include "Scenes/SkullEffects.h"
#include "Scenes/WizardEffects.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <utility>

// 0x4e6380, checked against the PE (1,418 bytes), and 0x42ab40 (266 bytes).
namespace Client::Actors
{
namespace
{
constexpr std::uint32_t Bit40000 = 0x40000;
constexpr std::uint32_t Bit80000 = 0x80000;
constexpr std::uint32_t Bit400000 = 0x400000;
constexpr std::uint32_t Bit800000 = 0x800000;
constexpr std::size_t BlessingNoticeText = 1828; // 0x76bd1dc + 1828 * 300 = 0x774300c.
constexpr std::int32_t SystemMessage = 1;

bool Has(std::uint32_t flags, std::uint32_t bit)
{
    return (flags & bit) == bit;
}

// Types 0x204 to 0x208 (signed, 0x4e64a2..0x4e64a8) carry the flag in +0x7c, whole.
bool KeepsItIn7c(const Kernel::ActorState& actor)
{
    constexpr std::int16_t FirstType = 0x204;
    constexpr std::int16_t LastType = 0x208;
    const auto type = static_cast<std::int16_t>(actor.type);
    return type >= FirstType && type <= LastType;
}

// 0x10000, 0x20000, 0x80000 (without the 0x40000 part) and 0x100000.
void PutBitOr7c(Kernel::ActorState& actor, std::uint32_t bit)
{
    if (KeepsItIn7c(actor))
        actor.flags7c = bit;
    else
        actor.flags78 |= bit;
}

void ClearBit(std::uint32_t& flags, std::uint32_t bit)
{
    if (Has(flags, bit))
        flags -= bit;
}

Core::Expected<void, std::string> Cut(const char* text)
{
    return Core::Unexpected(std::string(text));
}

Core::Expected<void, std::string> ShowTheBlessingNotice(const Scenes::WorldEntryResources& resources)
{
    if (resources.texts == nullptr)
        return Cut("0x4e68f5: falta Text.bmd para el aviso de la bandera 0x2000000");
    if (resources.textMeasure == nullptr)
        return Cut("0x4e68f5: falta el medidor para 0x555190 y el aviso de la bandera 0x2000000");
    const auto text = resources.texts->Text(BlessingNoticeText);
    if (!text)
        return Cut("0x4e68f5: Text.bmd no ofrece el registro 1828 del aviso");
    const Core::PixelSize screen = resources.screen ? resources.screen() : Core::PixelSize{640, 480};
    // 0x5767b6c is an empty name anchor. Even an empty Text1828 must age the log and add its row.
    return Scenes::AddGameMessage(resources.world.Runtime(), *resources.textMeasure, screen, {}, *text, SystemMessage);
}

// 0x530860(0x10f, from, from, angles, subtype, the character, width, −1, 0, 0, −1) (AuraJoints.cpp). With the pool
// full, nothing.
Core::Expected<void, std::string> CreateAura(Kernel::ActorState& actor, glm::vec3& from, glm::vec3& angles,
                                             std::int32_t subtype, float width,
                                             const Scenes::WorldEntryResources& resources)
{
    const World::FishEffectRequest request{.type = Scenes::JointFamilies::AuraJoint,
                                           .firstPosition = from,
                                           .secondPosition = from,
                                           .angles = angles,
                                           .subtype = subtype,
                                           .ownerContext = &actor,
                                           .scale = width,
                                           .key = -1,
                                           .skill = 0,
                                           .skillSerial = 0,
                                           .target = -1};
    const auto created = Scenes::CreateFaunaJoint(resources.world.DrawTail().joints, request, resources.cloth.random,
                                                  Scenes::JointOwnerKind::Actor);
    if (!created.HasValue())
        return Core::Unexpected(created.Error());
    return {};
}
} // namespace

// 0x518ac0(0xc1, character, 1) turns off its 0xc1/1 and two 0x518cf0(0xc1, +0x10, angles, +0xf4, 1 and 2, character,
// −1, …) create the ice with the character's angles and, in the second, the z 180 higher (fld, fadd and fstp). The
// light is the character's (+0xf4), which each creation overwrites with (0.4f, 0.3f, 0.2f) (0x51acd2): the second
// already reads it overwritten. Their 0x5f2/0 smokes do not read the world's clock.
Core::Expected<void, std::string> WrapInIce(Kernel::ActorState& actor, const Scenes::WorldEntryResources& resources,
                                            const IceCalls& calls)
{
    constexpr std::int32_t FirstIce = 1;
    constexpr std::int32_t SecondIce = 2;
    constexpr double HalfTurn = Math::HalfTurnAsDouble; // 0x6b86bc
    Kernel::WorldState& world = resources.world;
    static_cast<void>(KillModelEffectsOf(world.Map().effects, World::IceEffect, &actor, FirstIce));
    const World::WizardEffectServices services =
        Scenes::WizardEffectServicesOf(world, resources.cloth.random,
                                       Scenes::WizardSceneParts{.particles = &world.Particles(),
                                                                .terrain = &world.Map().terrain,
                                                                .fixedHeight = resources.camera.fixedHeight});
    glm::vec3 angles = actor.angles;
    for (const std::int32_t subtype : {FirstIce, SecondIce})
    {
        if (subtype == SecondIce)
            angles.z = static_cast<float>(static_cast<double>(angles.z) + HalfTurn);
        const auto created = World::CreateWizardModelEffect(
            world.Map().effects,
            Scenes::WizardArguments(World::IceEffect, actor.position, angles, actor.light, subtype, &actor),
            resources.cloth.random, services, &actor.light);
        if (!created.HasValue())
            return Core::Unexpected(std::format("{:#x}: el 0xc1/{} del hielo: {}",
                                                subtype == FirstIce ? calls.first : calls.second, subtype,
                                                created.Error()));
    }
    return {};
}

Core::Expected<void, std::string> ApplyEffectFlags(Kernel::ActorState& actor, std::uint32_t flags, bool publishedHero,
                                                   const Scenes::WorldEntryResources& resources)
{
    const auto requestSound = [&resources](std::int32_t id) { resources.world.Runtime().RequestSound(id); };
    auto& effects = resources.world.Map().effects;
    // 0x4e6390..0x4e63b7.
    if (Has(flags, Bit800000))
    {
        ClearBit(actor.flags78, Bit40000);
        actor.flags78 |= Bit800000;
    }
    // 0x4e63c0..0x4e63ce.
    constexpr std::uint32_t Hidden = 0x10000000;
    if (Has(flags, Hidden))
        actor.flags7c |= Hidden;
    // 0x4e63d3..0x4e6412: the four low bits.
    actor.flags78 |= flags & 0xfu;
    // 0x4e641a..0x4e6492.
    constexpr std::uint32_t Bit100 = 0x100;
    if (Has(flags, Bit100))
    {
        if (!Has(actor.flags78, Bit100) && actor.type == Tables::PlayerSkeletonModel)
        {
            constexpr std::int32_t Bit100Sound = 0x84;
            requestSound(Bit100Sound);
            // 0x4e6447..0x4e6483: turns off its 0x10f/0 (0x534f90) and creates five, 50 wide, that turn around it.
            auto& joints = resources.world.DrawTail().joints;
            KillJointsOf(joints, Scenes::JointFamilies::AuraJoint, &actor, 0);
            for (int ring = 0; ring < 5; ++ring)
                if (auto created = CreateAura(actor, actor.position, actor.angles, 0, 50, resources);
                    !created.HasValue())
                    return created;
        }
        actor.flags78 |= Bit100;
    }
    constexpr std::uint32_t Bit10000 = 0x10000;
    constexpr std::uint32_t Bit20000 = 0x20000;
    if (Has(flags, Bit10000))
        PutBitOr7c(actor, Bit10000);
    if (Has(flags, Bit20000))
        PutBitOr7c(actor, Bit20000);
    // 0x4e64df..0x4e6543: the first of 0x80000, 0x400000 and 0x800000 it has, off, and 0x40000 on.
    if (Has(flags, Bit40000))
    {
        if (KeepsItIn7c(actor))
        {
            actor.flags7c = Bit40000;
        }
        else
        {
            if (Has(actor.flags78, Bit80000))
                actor.flags78 -= Bit80000;
            else if (Has(actor.flags78, Bit400000))
                actor.flags78 -= Bit400000;
            else if (Has(actor.flags78, Bit800000))
                actor.flags78 -= Bit800000;
            actor.flags78 |= Bit40000;
        }
    }
    // 0x4e6546..0x4e6565.
    if (Has(flags, Bit80000))
    {
        if (KeepsItIn7c(actor))
        {
            actor.flags7c = Bit80000;
        }
        else
        {
            ClearBit(actor.flags78, Bit40000);
            actor.flags78 |= Bit80000;
        }
    }
    constexpr std::uint32_t Bit100000 = 0x100000;
    if (Has(flags, Bit100000))
        PutBitOr7c(actor, Bit100000);
    // 0x4e659d..0x4e65c1.
    if (Has(flags, Bit400000))
    {
        ClearBit(actor.flags78, Bit40000);
        actor.flags78 |= Bit400000;
    }
    // 0x4e65c8..0x4e65ec: the bit goes before the joints.
    constexpr std::uint32_t Bit1000 = 0x1000;
    if (Has(flags, Bit1000))
    {
        actor.flags78 |= Bit1000;
        // 0x4e65df..0x4e66c3: three 0x10f/8, 30 wide, from a copy of its +0x10, turned 90, 180 and 270 in z, the
        // copy 10 lower after each one.
        glm::vec3 from = actor.position;
        for (const std::uint32_t turn : {0x42b40000u, 0x43340000u, 0x43870000u})
        {
            glm::vec3 angles{0, 0, std::bit_cast<float>(turn)};
            if (auto created = CreateAura(actor, from, angles, 8, 30, resources); !created.HasValue())
                return created;
            from.z = static_cast<float>(static_cast<double>(from.z) - 10);
        }
    }
    // 0x4e66c6..0x4e6709.
    for (const std::uint32_t bit : {0x4000u, 0x200u, 0x8000u})
    {
        if (Has(flags, bit))
            actor.flags78 |= bit;
    }
    // 0x4e670f..0x4e67a1: always, without checking whether it already had the bit and without rand(), turns off its
    // 0x5ac/2 (or /1) and creates another with it as owner (0x518ac0 and 0x518cf0, like skills 0x35 and 0x30 of
    // SkillReplies, without the 0x619/9 joints of 0x35); with the table full, none. Then it sets the bit and goes on.
    // The step of /1 stops at 0x52a866.
    for (const auto& [bit, subtype] :
         {std::pair{0x400u, World::GuardFlareSubtype}, std::pair{0x10u, World::SwellFlareSubtype}})
    {
        if (!Has(flags, bit))
            continue;
        KillModelEffectsOf(effects, World::IcarusFlareEffect, &actor, subtype);
        static_cast<void>(effects.CreateSkillFlare(actor.position, actor.angles, actor.light, subtype, &actor));
        actor.flags78 |= bit;
    }
    // 0x4e67a6..0x4e6833: with the bit already set in +0x78, it only sets it again.
    constexpr std::uint32_t Bit20 = 0x20;
    if (Has(flags, Bit20))
    {
        if (!Has(actor.flags78, Bit20))
        {
            // 0x4e67bc..0x4e682a.
            if (auto frozen = WrapInIce(actor, resources, IceCalls{.first = 0x4e67fc, .second = 0x4e6825});
                !frozen.HasValue())
                return frozen;
        }
        actor.flags78 |= Bit20;
    }
    constexpr std::uint32_t Bit40 = 0x40;
    if (Has(flags, Bit40))
    {
        if (!Has(actor.flags78, Bit40))
        {
            // 0x4e6842..0x4e6899: the skull 0x628/0 over the character (the previous one, off) and sound 0x85 on it.
            constexpr std::int32_t SkullSound = 0x85;
            if (auto skull = Scenes::PutTheSkullOn(actor, resources.world, resources.cloth.random, 0x4e688c);
                !skull.HasValue())
                return skull;
            resources.world.Runtime().RequestSound(SkullSound, actor.position);
        }
        actor.flags78 |= Bit40;
    }
    // 0x4e68a7..0x4e68cc.
    for (const std::uint32_t bit : {0x80u, 0x1000000u})
    {
        if (Has(flags, bit))
            actor.flags78 |= bit;
    }
    // 0x4e68d3..0x4e68fd.
    constexpr std::uint32_t Bit2000000 = 0x2000000;
    if (Has(flags, Bit2000000))
    {
        if (publishedHero)
        {
            if (auto notice = ShowTheBlessingNotice(resources); !notice.HasValue())
                return notice;
        }
        actor.flags78 |= Bit2000000;
    }
    return {};
}

void ApplyCastleSiegeFlags(Kernel::ActorState& actor, std::uint32_t flags, std::int32_t map)
{
    using Maps::CastleSiegeMap;
    if (map != CastleSiegeMap)
        return;
    constexpr std::uint8_t GuildLayer = 0xc;
    // 0x42ab52..0x42abe7: the first of the three, with layer 0xc freed first.
    constexpr std::uint8_t FirstLayer = 6;
    std::uint8_t layer = FirstLayer;
    for (const std::uint32_t bit : {Bit80000, Bit400000, Bit800000})
    {
        if (Has(flags, bit))
        {
            if (actor.byte360 == GuildLayer)
                Kernel::ReleaseTheLayer(actor);
            actor.byte360 = layer;
            return;
        }
        ++layer;
    }
    const bool siegeLayer = actor.byte360 == 6 || actor.byte360 == 7 || actor.byte360 == 8;
    // 0x42abe8..0x42ac1d.
    if (Has(flags, Bit40000))
    {
        if (siegeLayer)
            Kernel::ReleaseTheLayer(actor);
        actor.byte360 = GuildLayer;
        return;
    }
    // 0x42ac1e..0x42ac45.
    if (siegeLayer || actor.byte360 == GuildLayer)
        Kernel::ReleaseTheLayer(actor);
}
} // namespace Client::Actors
