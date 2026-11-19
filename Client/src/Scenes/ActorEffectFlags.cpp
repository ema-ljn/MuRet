#include "Scenes/ActorEffectFlags.h"

#include "Core/Expected.h"
#include "App/WindowState.h"
#include "Data/Models/PlayerModels.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorFlags.h"
#include "Scenes/ActorState.h"
#include "World/GameCamera.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/JointFamilies.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/WizardModelEffects.h"
#include "Data/Text/TextTable.h"
#include "Scenes/GameMessages.h"
#include "Scenes/WizardEffects.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <utility>

// 0x4e6380, cotejado con el PE (1.418 bytes), y 0x42ab40 (266 bytes).
namespace Client::Scenes
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

// Los tipos 0x204 a 0x208 (con signo, 0x4e64a2..0x4e64a8) llevan la bandera en +0x7c, entera.
bool KeepsItIn7c(const ActorState& actor)
{
    constexpr std::int16_t FirstType = 0x204;
    constexpr std::int16_t LastType = 0x208;
    const auto type = static_cast<std::int16_t>(actor.type);
    return type >= FirstType && type <= LastType;
}

// 0x10000, 0x20000, 0x80000 (sin la parte del 0x40000) y 0x100000.
void PutBitOr7c(ActorState& actor, std::uint32_t bit)
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

Core::Expected<void, std::string> ShowTheBlessingNotice(const WorldEntryResources& resources)
{
    if (resources.texts == nullptr)
        return Cut("0x4e68f5: falta Text.bmd para el aviso de la bandera 0x2000000");
    if (resources.textMeasure == nullptr)
        return Cut("0x4e68f5: falta el medidor para 0x555190 y el aviso de la bandera 0x2000000");
    const auto text = resources.texts->Text(BlessingNoticeText);
    if (!text)
        return Cut("0x4e68f5: Text.bmd no ofrece el registro 1828 del aviso");
    const App::PixelSize screen = resources.screen ? resources.screen() : App::PixelSize{640, 480};
    // 0x5767b6c es una ancla de nombre vacía. Incluso un Text1828 vacío debe envejecer el log y añadir su fila.
    return AddGameMessage(resources.world.Runtime(), *resources.textMeasure, screen, {}, *text, SystemMessage);
}

// 0x530860(0x10f, desde, desde, ángulos, subtipo, el personaje, ancho, −1, 0, 0, −1) (AuraJoints.cpp). Con el pool
// lleno, nada.
Core::Expected<void, std::string> CreateAura(ActorState& actor, glm::vec3& from, glm::vec3& angles,
                                             std::int32_t subtype, float width, const WorldEntryResources& resources)
{
    const World::FishEffectRequest request{.type = JointFamilies::AuraJoint,
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
    const auto created =
        CreateFaunaJoint(resources.world.DrawTail().joints, request, resources.cloth.random, JointOwnerKind::Actor);
    if (!created.HasValue())
        return Core::Unexpected(created.Error());
    return {};
}
} // namespace

// 0x518ac0(0xc1, personaje, 1) apaga sus 0xc1/1 y dos 0x518cf0(0xc1, +0x10, ángulos, +0xf4, 1 y 2, personaje, −1, …)
// crean el hielo con los ángulos del personaje y, en el segundo, la z 180 más (fld, fadd y fstp). La luz es la del
// personaje (+0xf4), que cada alta pisa con (0,4f; 0,3f; 0,2f) (0x51acd2): el segundo ya la lee pisada. Sus humos 5f2/0
// no leen el reloj del mundo.
Core::Expected<void, std::string> WrapInIce(ActorState& actor, const WorldEntryResources& resources,
                                            const IceCalls& calls)
{
    constexpr std::int32_t FirstIce = 1;
    constexpr std::int32_t SecondIce = 2;
    constexpr double HalfTurn = 180.0; // 0x6b86bc
    WorldState& world = resources.world;
    static_cast<void>(KillModelEffectsOf(world.Map().effects, World::IceEffect, &actor, FirstIce));
    const World::WizardEffectServices services =
        WizardEffectServicesOf(world, resources.cloth.random,
                               WizardSceneParts{.particles = &world.Particles(),
                                                .terrain = &world.Map().terrain,
                                                .fixedHeight = resources.camera.fixedHeight});
    glm::vec3 angles = actor.angles;
    for (const std::int32_t subtype : {FirstIce, SecondIce})
    {
        if (subtype == SecondIce)
            angles.z = static_cast<float>(static_cast<double>(angles.z) + HalfTurn);
        const auto created = World::CreateWizardModelEffect(
            world.Map().effects,
            WizardArguments(World::IceEffect, actor.position, angles, actor.light, subtype, &actor),
            resources.cloth.random, services, &actor.light);
        if (!created.HasValue())
            return Core::Unexpected(std::format("{:#x}: el 0xc1/{} del hielo: {}",
                                                subtype == FirstIce ? calls.first : calls.second, subtype,
                                                created.Error()));
    }
    return {};
}

Core::Expected<void, std::string> ApplyEffectFlags(ActorState& actor, std::uint32_t flags, bool publishedHero,
                                                   const WorldEntryResources& resources)
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
    // 0x4e63d3..0x4e6412: los cuatro bits de abajo.
    actor.flags78 |= flags & 0xfu;
    // 0x4e641a..0x4e6492.
    constexpr std::uint32_t Bit100 = 0x100;
    if (Has(flags, Bit100))
    {
        if (!Has(actor.flags78, Bit100) && actor.type == Data::Models::PlayerSkeletonModel)
        {
            constexpr std::int32_t Bit100Sound = 0x84;
            requestSound(Bit100Sound);
            // 0x4e6447..0x4e6483: apaga sus 0x10f/0 (0x534f90) y crea cinco, de ancho 50, que giran a su alrededor.
            auto& joints = resources.world.DrawTail().joints;
            KillJointsOf(joints, JointFamilies::AuraJoint, &actor, 0);
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
    // 0x4e64df..0x4e6543: el primero de 0x80000, 0x400000 y 0x800000 que tenga, afuera, y el 0x40000 adentro.
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
    // 0x4e65c8..0x4e65ec: el bit va antes de los joints.
    constexpr std::uint32_t Bit1000 = 0x1000;
    if (Has(flags, Bit1000))
    {
        actor.flags78 |= Bit1000;
        // 0x4e65df..0x4e66c3: tres 0x10f/8 de ancho 30 desde una copia de su +0x10, girados 90, 180 y 270 en z, la
        // copia 10 más abajo después de cada uno.
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
    // 0x4e670f..0x4e67a1: siempre, sin mirar si ya tenía el bit y sin rand(), apaga su 0x5ac/2 (o /1) y crea otro con
    // él de dueño (0x518ac0 y 0x518cf0, como la skill 0x35 y la 0x30 de SkillReplies, sin los joints 0x619/9 de la
    // 0x35); con la tabla llena, ninguno. Después prende el bit y sigue. El paso del /1 corta en 0x52a866 (CL11).
    for (const auto& [bit, subtype] :
         {std::pair{0x400u, World::GuardFlareSubtype}, std::pair{0x10u, World::SwellFlareSubtype}})
    {
        if (!Has(flags, bit))
            continue;
        KillModelEffectsOf(effects, World::IcarusFlareEffect, &actor, subtype);
        static_cast<void>(effects.CreateSkillFlare(actor.position, actor.angles, actor.light, subtype, &actor));
        actor.flags78 |= bit;
    }
    // 0x4e67a6..0x4e6833: con el bit ya prendido en +0x78, solo lo vuelve a prender.
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
            return Cut("0x4e6869: 0x518ac0, el efecto 0x628/0 (0x518cf0) y el sonido 0x85 de la bandera 0x40 son de la "
                       "CL11 y la CL10");
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

void ApplyCastleSiegeFlags(ActorState& actor, std::uint32_t flags, std::int32_t map)
{
    constexpr std::int32_t CastleSiegeMap = 30;
    if (map != CastleSiegeMap)
        return;
    constexpr std::uint8_t GuildLayer = 0xc;
    // 0x42ab52..0x42abe7: el primero de los tres, con la capa 0xc soltada antes.
    constexpr std::uint8_t FirstLayer = 6;
    std::uint8_t layer = FirstLayer;
    for (const std::uint32_t bit : {Bit80000, Bit400000, Bit800000})
    {
        if (Has(flags, bit))
        {
            if (actor.byte360 == GuildLayer)
                ReleaseTheLayer(actor);
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
            ReleaseTheLayer(actor);
        actor.byte360 = GuildLayer;
        return;
    }
    // 0x42ac1e..0x42ac45.
    if (siegeLayer || actor.byte360 == GuildLayer)
        ReleaseTheLayer(actor);
}
} // namespace Client::Scenes
