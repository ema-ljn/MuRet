#include "Scenes/AttackSkillCases.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/ActorFlags.h"
#include "Scenes/ActorState.h"
#include "Scenes/ArrowEffects.h"
#include "Scenes/AttackSkillReads.h"
#include "Scenes/BlastEffectCalls.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/FireBreathEffect.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/HeroAttack.h"
#include "Scenes/HeroSkillCasts.h"
#include "Scenes/HeroSkills.h"
#include "Scenes/HighModelEffects.h"
#include "Scenes/JavelinEffect.h"
#include "Scenes/JointFamilies.h"
#include "Scenes/JointKernels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WizardEffects.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ColoredDecals.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/KalimaWorld.h"
#include "World/Particles.h"
#include "World/TerrainHeight.h"
#include "World/WizardModelEffects.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>

// Los casos de habilidad de 0x4fd5f0 (0x4fdeeb, 0x504372 y 0x5042ce), cotejados con el PE c32bbd5f: 06, "El paso del
// ataque".
namespace Client::Scenes
{
namespace
{
using JointKernels::D;
using JointKernels::F32;
constexpr std::int16_t NoTarget = -1;
// El sonido de los fuegos 0xc2 de la 0x02 y la 0x04 (0x5049f9, 0x504bda).
constexpr std::int32_t FireSound = 0x42;
constexpr std::size_t PoolSlots = 400;
// +0x78 & 0x4000: los casos 0x1b, 0x1c y 0x10 con blanco no hacen nada (0x50451c, 0x5046ca, 0x504836).
constexpr std::uint32_t QuietFlag = 0x4000;
// El joint 0x10f, el de los calcos que se van con 0x534f90 (0x4fe3ce, 0x504876).
constexpr std::int32_t FlameJoint = 0x10f;

// 0x41ed10(sonido, …): la CL10 decide cómo suena (WorldRuntime::lastSoundId en el juego).
void RequestTheSound(const CharacterStepContext& context, std::int32_t sound, const ActorState* actor = nullptr)
{
    if (actor != nullptr && context.soundAt)
        context.soundAt(sound, actor->position);
    else if (context.soundRequest)
        context.soundRequest(sound);
}

// Lo que crea un caso y MuRet no tiene: un efecto con modelo (0x518cf0), un joint (0x530860) o una partícula (0x53fe70)
// de la CL11, con lo de antes y sus rand().
Core::Unexpected<std::string> CutAt(std::uint32_t address, std::string_view what, std::uint8_t skill, std::size_t slot)
{
    return Core::Unexpected(
        std::format("{:#x}: {} de la habilidad {:#x} del lugar {}, sin portar (CL11)", address, what, skill, slot));
}

// 518d0f/530872: lleno sale antes del constructor. Un pool de joints ausente sigue siendo una capacidad pendiente.
bool HasEffectRoom(const CharacterStepContext& context)
{
    return std::ranges::any_of(context.world.Map().effects.Effects(), [](const auto& effect) { return !effect.alive; });
}

bool HasJointRoom(const CharacterStepContext& context)
{
    return context.joints.empty() ||
           std::ranges::any_of(context.joints, [](const auto& joint) { return !joint.alive; });
}

Core::Expected<void, std::string> EffectThenSound(std::uint32_t address, std::string_view what, std::uint8_t skill,
                                                  std::size_t slot, std::int32_t sound,
                                                  const CharacterStepContext& context,
                                                  const ActorState* actor = nullptr)
{
    if (HasEffectRoom(context))
        return CutAt(address, what, skill, slot);
    if (sound >= 0)
        RequestTheSound(context, sound, actor);
    return {};
}

// 0x518cf0 de un tipo alto (Scenes/HighModelEffects) desde un caso: el error lleva la habilidad y el lugar.
Core::Expected<void, std::string> CreateHighEffect(const World::ModelEffectArguments& arguments, std::uint8_t skill,
                                                   std::size_t slot, const CharacterStepContext& context)
{
    if (auto made = CreateHighModelEffect(arguments, HighContextOf(context)); !made.HasValue())
        return Core::Unexpected(std::format("{} (la habilidad {:#x} del lugar {})", made.Error(), skill, slot));
    return {};
}

// 0x4fd81e y 0x4fdaeb..0x4fdb4d: la luz local del paso, (1; 1; 1) o, con la acción 0x4d, 0x4f o 0x5a pasado el frame
// 5,5, la de la partícula 0x5ac/0 (0,3; 0,5; 1).
glm::vec3 CaseLight(const ActorState& actor)
{
    constexpr std::array<std::uint8_t, 3> FlaredActions{0x4d, 0x4f, 0x5a};
    constexpr float FlareFrame = 5.5f; // 0x6b8a40
    const bool flared = std::ranges::find(FlaredActions, actor.animation.action) != FlaredActions.end() &&
                        actor.animation.frame > FlareFrame;
    return flared ? glm::vec3(std::bit_cast<float>(0x3e99999au), 0.5f, 1.0f) : glm::vec3(1.0f);
}

Core::Expected<void, std::string> JointThenSound(std::uint32_t address, std::string_view what, std::uint8_t skill,
                                                 std::size_t slot, std::int32_t sound,
                                                 const CharacterStepContext& context)
{
    if (HasJointRoom(context))
        return CutAt(address, what, skill, slot);
    if (sound >= 0)
        RequestTheSound(context, sound);
    return {};
}

// 0x530860(0x613, desde, hasta, ángulos, subtipo, o, ancho, key, skill, serial, −1): un joint 0x613 colgado de o, con
// lo que su alta pide al paso (el sonido de la /1 y el registro propio de la /0). Con el pool lleno, nada.
struct SpiritJointRequest
{
    std::uint32_t address = 0;
    std::int32_t subtype = 0;
    glm::vec3 from{};
    glm::vec3 to{};
    glm::vec3 angles{};
    float width = 0.0f;
    std::int32_t key = 0;
    std::int32_t skill = 0;
    std::int32_t serial = 0;
};

Core::Expected<void, std::string> CreateTheSpirit(ActorState& actor, const CharacterStepContext& context,
                                                  SpiritJointRequest spirit)
{
    if (context.joints.empty())
        return Core::Unexpected(std::format("{:#x}: faltan los joints del frame (0x530860)", spirit.address));
    const JointKernels::CreationServices services{.playerBonePoint = {},
                                                  .soundRequest = [&context](std::int32_t sound)
                                                  { RequestTheSound(context, sound); },
                                                  .ownRecordDepth = &context.world.Runtime().ownRecordDepth,
                                                  .ownVolley = &context.world.OwnCharacter().bytef04};
    const World::FishEffectRequest request{.type = JointFamilies::SpiritJoint,
                                           .firstPosition = spirit.from,
                                           .secondPosition = spirit.to,
                                           .angles = spirit.angles,
                                           .subtype = spirit.subtype,
                                           .ownerContext = &actor,
                                           .scale = spirit.width,
                                           .key = spirit.key,
                                           .skill = spirit.skill,
                                           .skillSerial = spirit.serial,
                                           .target = -1};
    const auto created = CreateFaunaJoint(context.joints, request, context.random, JointOwnerKind::Actor, &services);
    if (!created.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", spirit.address, created.Error()));
    return {};
}

// 0x530860(0x10f, desde, desde, ángulos, subtipo, dueño, ancho, −1, 0, 0, blanco): un joint 0x10f (AuraJoints.cpp).
// Con el pool lleno, nada.
Core::Expected<void, std::string> CreateTheAura(std::uint32_t address, ActorState& owner, glm::vec3& from,
                                                glm::vec3& angles, std::int32_t subtype, float width,
                                                std::int32_t target, const CharacterStepContext& context)
{
    if (context.joints.empty())
        return Core::Unexpected(std::format("{:#x}: faltan los joints del frame (0x530860)", address));
    const World::FishEffectRequest request{.type = FlameJoint,
                                           .firstPosition = from,
                                           .secondPosition = from,
                                           .angles = angles,
                                           .subtype = subtype,
                                           .ownerContext = &owner,
                                           .scale = width,
                                           .key = -1,
                                           .skill = 0,
                                           .skillSerial = 0,
                                           .target = target};
    const auto created = CreateFaunaJoint(context.joints, request, context.random, JointOwnerKind::Actor);
    if (!created.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", address, created.Error()));
    return {};
}

// Cinco 0x10f de ancho 20 alrededor del blanco, desde su +0x10 y con sus ángulos.
Core::Expected<void, std::string> CreateFiveAuras(std::uint32_t address, ActorState& victim, std::int32_t subtype,
                                                  std::int32_t target, const CharacterStepContext& context)
{
    constexpr float Width = 20;
    for (int ring = 0; ring < 5; ++ring)
        if (auto created =
                CreateTheAura(address, victim, victim.position, victim.angles, subtype, Width, target, context);
            !created.HasValue())
            return created;
    return {};
}

// 0x4fe147..0x4fe18b (la 0x43): 0x530860(0x632, o+0x10, o+0x10, o+0x1c, 7, nulo, 10, −1, 0, 0, −1), que suelta tres
// 0x632/6 (StormJoints.cpp), y el sonido 0x266 aunque el pool esté lleno.
Core::Expected<void, std::string> CallTheStorm(ActorState& actor, const CharacterStepContext& context)
{
    constexpr std::int32_t StormSound = 0x266;
    if (context.joints.empty())
        return Core::Unexpected(std::string("0x4fe172: faltan los joints del frame (0x530860)"));
    const World::FishEffectRequest request{.type = JointFamilies::StormJoint,
                                           .firstPosition = actor.position,
                                           .secondPosition = actor.position,
                                           .angles = actor.angles,
                                           .subtype = JointFamilies::StormSubtype,
                                           .ownerContext = nullptr,
                                           .scale = 10,
                                           .key = -1,
                                           .skill = 0,
                                           .skillSerial = 0,
                                           .target = -1};
    if (auto created = CreateFaunaJoint(context.joints, request, context.random); !created.HasValue())
        return Core::Unexpected(std::format("0x4fe172: {}", created.Error()));
    RequestTheSound(context, StormSound);
    return {};
}

// 0x504ac7..0x504b49: la 0x49 crea un 0x61c/4 sin dueño desde o 130 más arriba, con los ángulos (0, 0, z de o) y ancho
// 40 (InfernoJoints.cpp, sin rand()), y pide el sonido 0x26f aunque el pool esté lleno.
Core::Expected<void, std::string> RaiseTheInferno(const ActorState& actor, const CharacterStepContext& context)
{
    constexpr std::int32_t InfernoSubtype = 4;
    constexpr float InfernoWidth = 40;  // 0x42200000
    constexpr double InfernoLift = 130; // 0x6ba194
    constexpr std::int32_t InfernoSound = 0x26f;
    if (context.joints.empty())
        return Core::Unexpected(std::string("0x504b33: faltan los joints del frame (0x530860)"));
    glm::vec3 from{actor.position.x, actor.position.y, F32(D(actor.position.z) + InfernoLift)};
    glm::vec3 turn{0, 0, actor.angles.z};
    const World::FishEffectRequest request{.type = JointFamilies::InfernoJoint,
                                           .firstPosition = from,
                                           .secondPosition = from,
                                           .angles = turn,
                                           .subtype = InfernoSubtype,
                                           .ownerContext = nullptr,
                                           .scale = InfernoWidth,
                                           .key = -1,
                                           .skill = 0,
                                           .skillSerial = 0,
                                           .target = -1};
    if (auto created = CreateFaunaJoint(context.joints, request, context.random); !created.HasValue())
        return Core::Unexpected(std::format("0x504b33: {}", created.Error()));
    RequestTheSound(context, InfernoSound);
    return {};
}

// 0x4f5b10(o, x, y, z, punto): (x, y, z) girado por los ángulos de o (0x5fc8f0 y 0x5fcbd0) y sumado a su posición,
// sin rand().
glm::vec3 TurnedPoint(const ActorState& actor, const glm::vec3& offset)
{
    const auto turned = Render::Models::OriginalVectorRotate(offset, Render::Models::OriginalAngleMatrix(actor.angles));
    return {F32(D(actor.position.x) + D(turned.x)), F32(D(actor.position.y) + D(turned.y)),
            F32(D(actor.position.z) + D(turned.z))};
}

// 0x4fe795..0x4fe981 (la 0x35): cuatro 0x619/0xa de ancho 15 colgados de o, con los ángulos (−45, 0, z): z 45 y 405
// desde (sen 4,5 · 80; cos 4,5 · 80; 300) más la posición de o, y 225 y 450 desde (cos 22,5 · 80; sen 22,5 · 80; 300)
// (0x6a1a90 es el seno de la CRT y 0x6a19e0 el coseno, FSIN y FCOS con el producto por 80 en la x87). Su alta los pone
// en la posición del dueño (FaunaJointCreation.cpp, CreateSetEnergy). Al final, el sonido 0x7f; sin rand().
Core::Expected<void, std::string> SpinTheEnergy(ActorState& actor, const CharacterStepContext& context)
{
    constexpr std::int32_t EnergyJoint = 0x619, EnergySubtype = 0xa, EnergySound = 0x7f;
    constexpr float Radius = 80.0f, Width = 15.0f;
    if (context.joints.empty())
        return Core::Unexpected(std::string("0x4fe856: faltan los joints del frame (0x530860)"));
    const auto point = [&actor](double argument, bool sineFirst) -> Core::Expected<glm::vec3, std::string>
    {
        const auto sine = Render::Models::Detail::SineProductPC53(argument, Radius);
        const auto cosine = Render::Models::Detail::CosineProductPC53(argument, Radius);
        if (!sine || !cosine)
            return Core::Unexpected(std::string("0x4fe7d7: el seno de la 0x35 excede el perfil PC53"));
        return glm::vec3(F32((sineFirst ? *sine : *cosine) + D(actor.position.x)),
                         F32((sineFirst ? *cosine : *sine) + D(actor.position.y)), F32(D(actor.position.z) + 300.0));
    };
    const auto near = point(4.5, true);
    if (!near.HasValue())
        return Core::Unexpected(near.Error());
    const auto far = point(22.5, false);
    if (!far.HasValue())
        return Core::Unexpected(far.Error());
    struct Energy
    {
        std::uint32_t address;
        float turn;
        glm::vec3 from;
    };
    const std::array<Energy, 4> energies{{{0x4fe856, 45.0f, near.Value()},
                                          {0x4fe895, 405.0f, near.Value()},
                                          {0x4fe92c, 225.0f, far.Value()},
                                          {0x4fe96b, 450.0f, far.Value()}}};
    for (const Energy& energy : energies)
    {
        glm::vec3 from = energy.from;
        glm::vec3 angles{-45.0f, 0.0f, energy.turn};
        const World::FishEffectRequest request{.type = EnergyJoint,
                                               .firstPosition = from,
                                               .secondPosition = from,
                                               .angles = angles,
                                               .subtype = EnergySubtype,
                                               .ownerContext = &actor,
                                               .scale = Width,
                                               .key = -1,
                                               .skill = 0,
                                               .skillSerial = 0,
                                               .target = -1};
        if (auto created = CreateFaunaJoint(context.joints, request, context.random); !created.HasValue())
            return Core::Unexpected(std::format("{:#x}: {}", energy.address, created.Error()));
    }
    RequestTheSound(context, EnergySound);
    return {};
}

// 0x4feb3b..0x4febd3 (la 0x41): el punto de 0x4f5b10(o, 0, −90, −50), 80 más alto en la acción 0x51 y 40 en la 0x5b,
// y 0x518cf0(0x62c, punto, o+0x1c, o+0xf4, 0, o, −1, 0, 0, 0, 0). Su caso (0x51f25e) deja la vida en 0 y, con dueño,
// suelta los cinco 0x62c (SpiralJoints.cpp): /1, /0 de ancho 250, /2, /3 y /4, los demás de ancho 100, desde el punto
// y con los ángulos de o. Después, el sonido 0x1cd aunque la tabla esté llena.
Core::Expected<void, std::string> RaiseTheSpirals(ActorState& actor, const CharacterStepContext& context)
{
    constexpr std::int16_t SpiralEffect = 0x62c;
    constexpr std::int32_t SpiralSound = 0x1cd;
    constexpr std::uint8_t HighAction = 0x51, LowAction = 0x5b;
    glm::vec3 point = TurnedPoint(actor, {0.0f, -90.0f, -50.0f});
    if (actor.animation.action == HighAction)
        point.z = F32(D(point.z) + 80.0); // 0x6b9ffc
    else if (actor.animation.action == LowAction)
        point.z = F32(D(point.z) + 40.0); // 0x6b8bf4
    const World::ModelEffectArguments arguments{.type = SpiralEffect,
                                                .position = point,
                                                .angles = actor.angles,
                                                .light = actor.light,
                                                .subtype = 0,
                                                .owner = &actor,
                                                .key = 0xffff,
                                                .byte89 = 0,
                                                .byte88 = 0,
                                                .byte1bd = 0,
                                                .scale = 0.0f};
    if (const auto slot = context.world.Map().effects.Begin(arguments))
    {
        context.world.Map().effects.Effects()[*slot].life = 0;
        if (context.joints.empty())
            return Core::Unexpected(std::string("0x51f293: faltan los joints del frame (0x530860)"));
        struct Spiral
        {
            std::uint32_t address;
            std::int32_t subtype;
            float width;
        };
        constexpr std::array<Spiral, 5> Spirals{{{0x51f293, 1, 100.0f},
                                                 {0x51f2b6, 0, 250.0f},
                                                 {0x51f2dd, 2, 100.0f},
                                                 {0x51f301, 3, 100.0f},
                                                 {0x51f3ad, 4, 100.0f}}};
        for (const Spiral& spiral : Spirals)
        {
            const World::FishEffectRequest request{.type = JointFamilies::SpiralJoint,
                                                   .firstPosition = point,
                                                   .secondPosition = point,
                                                   .angles = actor.angles,
                                                   .subtype = spiral.subtype,
                                                   .ownerContext = &actor,
                                                   .scale = spiral.width,
                                                   .key = -1,
                                                   .skill = 0,
                                                   .skillSerial = 0,
                                                   .target = -1};
            if (auto created = CreateFaunaJoint(context.joints, request, context.random, JointOwnerKind::Actor);
                !created.HasValue())
                return Core::Unexpected(std::format("{:#x}: {}", spiral.address, created.Error()));
        }
    }
    RequestTheSound(context, SpiralSound, &actor);
    return {};
}

// 0x4fe505..0x4fe790 (la 0x48): +0x78 de o en 0 y seis 0x10f/5..7 de ancho 170 desde o + 100 en z, girados 45, 135 y
// 225 y después 90, 180 y 270; la copia baja 10 después de cada uno salvo los /7. Al final, el sonido 0x26b.
Core::Expected<void, std::string> RaiseTheAuras(ActorState& actor, const CharacterStepContext& context)
{
    constexpr float Width = std::bit_cast<float>(0x432a0000u);
    constexpr std::int32_t RiseSound = 0x26b;
    actor.flags78 = 0;
    glm::vec3 from = actor.position;
    from.z = static_cast<float>(static_cast<double>(from.z) + 100);
    constexpr std::array<std::uint32_t, 6> Turns{0x42340000u, 0x43070000u, 0x43610000u,
                                                 0x42b40000u, 0x43340000u, 0x43870000u};
    constexpr std::array<std::uint32_t, 6> Addresses{0x4fe590, 0x4fe5f5, 0x4fe65a, 0x4fe6ad, 0x4fe712, 0x4fe777};
    for (std::size_t i = 0; i < Turns.size(); ++i)
    {
        glm::vec3 angles{0, 0, std::bit_cast<float>(Turns[i])};
        const auto subtype = static_cast<std::int32_t>(5 + i % 3);
        if (auto created = CreateTheAura(Addresses[i], actor, from, angles, subtype, Width, -1, context);
            !created.HasValue())
            return created;
        if (subtype != 7)
            from.z = static_cast<float>(static_cast<double>(from.z) - 10);
    }
    RequestTheSound(context, RiseSound);
    return {};
}

// 0x4fe015..0x4fe13f (la 0x30) y 0x4fe253..0x4fe37b (la 0x45): treinta y seis joints 0x613 de ancho 60 colgados de o,
// 100 más arriba, con los ángulos (−10, 0, 10 · i); en la vuelta 0 y en la 20, después del joint, el calco 0x61e
// (`decalSubtype`) sobre o con esos ángulos, la luz `light` y o de dueño (0x4fe127, 0x4fe360). Sin rand(). Después, el
// sonido.
struct SpiritRing
{
    std::int32_t subtype = 0;
    std::uint32_t jointAddress = 0;
    std::int32_t decalSubtype = 0;
    glm::vec3 light{1.0f};
    std::int32_t sound = 0;
};

Core::Expected<void, std::string> RingTheSpirits(ActorState& actor, std::size_t slot, std::uint8_t skill,
                                                 const CharacterStepContext& context, const SpiritRing& ring)
{
    constexpr int Spirits = 36, DecalEvery = 20;
    constexpr float Width = 60.0f;  // 0x42700000
    constexpr float Pitch = -10.0f; // 0xc1200000
    for (int index = 0; index < Spirits; ++index)
    {
        const glm::vec3 angles{Pitch, 0.0f, static_cast<float>(static_cast<double>(index) * 10.0)};
        const glm::vec3 point{actor.position.x, actor.position.y,
                              static_cast<float>(static_cast<double>(actor.position.z) + 100.0)};
        if (auto made = CreateTheSpirit(actor, context,
                                        {.address = ring.jointAddress,
                                         .subtype = ring.subtype,
                                         .from = point,
                                         .to = point,
                                         .angles = angles,
                                         .width = Width});
            !made.HasValue())
            return made;
        if (index % DecalEvery != 0)
            continue;
        if (auto made = CreateHighEffect({.type = World::ColoredDecalEffect,
                                          .position = actor.position,
                                          .angles = angles,
                                          .light = ring.light,
                                          .subtype = ring.decalSubtype,
                                          .owner = &actor},
                                         skill, slot, context);
            !made.HasValue())
            return made;
    }
    RequestTheSound(context, ring.sound);
    return {};
}

// 500141/501147/501867/50202e: sólo el héroe publica la tanda; el cierre conserva sus 3848 rand().
void CountHeroVolley(const ActorState& actor, const CharacterStepContext& context)
{
    if (&actor != &context.world.Actors()[context.world.Runtime().hero])
        return;
    const Render::Models::LogicalAccess own(context.world.Runtime().ownRecordDepth, context.random,
                                            Render::Models::OwnRecordCloseRandoms);
    ++context.world.OwnCharacter().bytef04;
}

// El alta de un efecto de los magos (Scenes::CreateWizardEffect). Con la tabla llena, nada y sin rand(); lo que el
// efecto pide y MuRet no tiene corta con la dirección de la llamada.
Core::Expected<void, std::string> CreateWizard(std::uint32_t address, const World::ModelEffectArguments& arguments,
                                               std::uint8_t skill, std::size_t slot,
                                               const CharacterStepContext& context)
{
    const auto created = CreateWizardEffect(context, arguments);
    if (!created.HasValue())
    {
        return Core::Unexpected(std::format("{:#x}: el efecto {:#x}/{} de la habilidad {:#x} del lugar {}: {}", address,
                                            arguments.type, arguments.subtype, skill, slot, created.Error()));
    }
    return {};
}

// 0x5042d3..0x504324: los ángulos de o con la z en el rumbo de 0x4e93f0 de o al blanco (fstp, sin rand()).
glm::vec3 TowardsTheTarget(const ActorState& actor, const ActorState& victim)
{
    return glm::vec3(actor.angles.x, actor.angles.y,
                     static_cast<float>(Render::Models::CreateAngle(actor.position.x, actor.position.y,
                                                                    victim.position.x, victim.position.y)));
}

// Un tipo de la mano como WORD con signo (MOVSX).
std::int32_t HandOf(const ActorState& actor, std::size_t hand)
{
    return std::bit_cast<std::int16_t>(actor.hands[hand].model);
}

// 0x4fdf2b..0x4fdf57 (y 0x4fdfb3, 0x4fdfe4, 0x4fec30): o+0x8c = el arma de la derecha (+0x2b8, MOVSX) − 0x212 y o+0x8e
// = su nivel (+0x2ba), que leen por su dueño los efectos 0xf4 y 0xfa (Scenes/GameDrawTail.cpp).
void KeepTheWeapon(ActorState& actor)
{
    constexpr std::int32_t FirstWeaponModel = 0x212;
    actor.word8c = static_cast<std::int16_t>(HandOf(actor, 0) - FirstWeaponModel);
    actor.byte8e = actor.hands[0].level;
}

// El blanco de +0x3a4 (MOVSX) de los casos 0x44, 0x46 y 0x47: el lugar del pool sin mirar el rango (0x4fe1ad y sus
// copias). Fuera del pool, MuRet corta: el original leería otra memoria.
Core::Expected<ActorState*, std::string> TargetOf(ActorState& actor, std::size_t slot,
                                                  const CharacterStepContext& context, std::uint32_t address)
{
    const auto target = std::bit_cast<std::int16_t>(actor.attackTarget);
    if (target == NoTarget)
        return nullptr;
    auto actors = context.world.Actors();
    if (target < 0 || static_cast<std::size_t>(target) >= actors.size())
    {
        return Core::Unexpected(
            std::format("{:#x}: el blanco {} de +0x3a4 del lugar {} está fuera del pool", address, target, slot));
    }
    return &actors[static_cast<std::size_t>(target)];
}

// 0x4fdf2b..0x4fdfae, la 0x2a: el arma, 0x59fb80(0x2a) y 0x518cf0(0xfa, o+0x10, o+0x1c, o+0xf4, 0, o, o+0x8a, lugar, 0,
// 0, 0), el terremoto con el estado 0 (que su dibujo hace con el arma de o, 0x52d43a); después el sonido 0x95. El
// prefijo de 0x518cf0 deja +0x8a y +0x89 antes del caso (0x51a66d), que no los lee: MuRet los escribe después del alta.
// Con la tabla llena, nada (y el sonido igual).
Core::Expected<void, std::string> ShakeTheGround(ActorState& actor, const CharacterStepContext& context)
{
    constexpr std::uint8_t QuakeSkill = 0x2a;
    constexpr std::int32_t QuakeSound = 0x95;
    KeepTheWeapon(actor);
    const std::int32_t index = OwnSkillIndex(context, QuakeSkill);
    World::ModelEffects& effects = context.world.Map().effects;
    const auto created = effects.CreateEarthquake(
        actor.position, actor.angles, actor.light, 0,
        World::ModelEffectOwner{.type = &actor.type, .effect = nullptr, .angles = &actor.angles},
        context.world.Runtime().ownRecordDepth, context.world.OwnCharacter().bytef04, context.random);
    if (created.has_value())
    {
        World::ModelEffect& quake = effects.Effects()[*created];
        quake.disturbanceState = static_cast<std::uint16_t>(actor.word8a);
        quake.byte89 = static_cast<std::uint8_t>(index & 0xff);
        quake.byte1bd = 0;
        quake.ownerContext = &actor;
        quake.itemType = &actor.word8c;
        quake.itemLevel = &actor.byte8e;
    }
    RequestTheSound(context, QuakeSound);
    return {};
}

// 0x500496..0x500c0b, la 0x05 (la Flame): el punto de la celda de la skill (+0x39c y +0x39d, más 0,5f por 100 en la
// x87, cada eje guardado en float) con la altura del terreno ahí (0x5f93c0), 0x59fb80(5) y 0x518cf0(0x5de, punto,
// o+0x1c, o+0xf4, 0, o, o+0x8a, lugar, 0, 0, 0): el chorro con o de dueño (World::ModelEffects::CreateOwnedFlameJet),
// cuyo paso con el héroe de dueño manda los blancos (0x52ae2a, World/GameModelEffects). Con c el héroe publicado
// ([0x743a170]), el registro propio abierto, su cuenta +0xf04 uno más y cerrado (0x500539..0x500bfa, con sus 3.848
// rand() si queda en 0); después, con cualquiera, el sonido 0x79 (0x500c03). Con la tabla llena no hay chorro (ni el
// registro de su alta) y lo demás sigue igual. Sin las alturas del terreno, MuRet corta antes de 0x59fb80.
Core::Expected<void, std::string> CastTheFlame(ActorState& actor, const CharacterStepContext& context)
{
    constexpr std::uint8_t FlameSkill = 5;
    constexpr std::int32_t FlameSound = 0x79;
    constexpr double CellCentre = 0.5; // 0x6b882c
    constexpr double CellSize = 100.0; // 0x6b86b4
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    const World::WorldTerrain& terrain = context.terrain != nullptr ? *context.terrain : world.Map().terrain;
    if (!terrain.heights || !terrain.attributes)
        return Core::Unexpected(std::string("0x5004ea: falta la altura del terreno (0x5f93c0) del chorro de la Flame"));
    glm::vec3 point{};
    point.x = static_cast<float>((static_cast<double>(actor.skillCellX) + CellCentre) * CellSize);
    point.y = static_cast<float>((static_cast<double>(actor.skillCellY) + CellCentre) * CellSize);
    point.z = static_cast<float>(
        World::TerrainHeightAt(*terrain.heights, *terrain.attributes, point.x, point.y, context.fixedHeight));
    const std::int32_t index = OwnSkillIndex(context, FlameSkill);
    const World::ModelEffectArguments arguments{.type = World::FlameJetEffect,
                                                .position = point,
                                                .angles = actor.angles,
                                                .light = actor.light,
                                                .subtype = 0,
                                                .owner = &actor,
                                                .key = static_cast<std::uint16_t>(actor.word8a),
                                                .byte89 = static_cast<std::uint8_t>(index & 0xff)};
    static_cast<void>(world.Map().effects.CreateOwnedFlameJet(arguments, runtime.ownRecordDepth,
                                                              world.OwnCharacter().bytef04, context.random));
    if (&actor == &world.Actors()[runtime.hero])
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        ++world.OwnCharacter().bytef04;
    }
    RequestTheSound(context, FlameSound);
    return {};
}

// 0x518cf0(tipo, punto, o+0x1c, o+0xf4, 0, o, o+0x8a, `index`, 0, 0, 0): un efecto del lote switch de la CL11
// (Scenes/BlastEffectCalls) con o de dueño, como lo piden la 0x29 (0x4fec9a), la 0xd (0x500d7b y 0x500dba) y la 0xe
// (0x5014da). Con la tabla llena, nada.
Core::Expected<void, std::string> CreateOwnedBlast(std::int16_t type, const glm::vec3& point, ActorState& actor,
                                                   std::int32_t index, const CharacterStepContext& context)
{
    const World::ModelEffectArguments arguments{.type = type,
                                                .position = point,
                                                .angles = actor.angles,
                                                .light = actor.light,
                                                .subtype = 0,
                                                .owner = &actor,
                                                .key = static_cast<std::uint16_t>(actor.word8a),
                                                .byte89 = static_cast<std::uint8_t>(index & 0xff)};
    const auto made = CreateBlastEffect(arguments, &actor, context);
    if (!made.HasValue())
        return Core::Unexpected(made.Error());
    return {};
}

// La celda de la skill (+0x39c y +0x39d, más 0,5f por 100 en la x87, cada eje guardado en float) con la altura del
// terreno ahí (0x5f93c0, guardada en float), como la leen 0x4ffcbe y 0x500cd4. Sin las alturas, MuRet corta en
// `address`.
Core::Expected<glm::vec3, std::string> SkillCellPoint(const ActorState& actor, const CharacterStepContext& context,
                                                      std::uint32_t address)
{
    constexpr double CellCentre = 0.5; // 0x6b882c
    constexpr double CellSize = 100.0; // 0x6b86b4
    const World::WorldTerrain& terrain = context.terrain != nullptr ? *context.terrain : context.world.Map().terrain;
    if (!terrain.heights || !terrain.attributes)
        return Core::Unexpected(
            std::format("{:#x}: falta la altura del terreno (0x5f93c0) de la celda de la skill", address));
    glm::vec3 point{};
    point.x = static_cast<float>((static_cast<double>(actor.skillCellX) + CellCentre) * CellSize);
    point.y = static_cast<float>((static_cast<double>(actor.skillCellY) + CellCentre) * CellSize);
    point.z = static_cast<float>(
        World::TerrainHeightAt(*terrain.heights, *terrain.attributes, point.x, point.y, context.fixedHeight));
    return point;
}

// 0x4ffcbe..0x4ffdb7, la 0x27: la celda de la skill con su altura y diez vueltas: rand() % 50 por la vuelta sobre la
// altura (FILD + FADD, guardado en float) y 0x518cf0(0x108, punto, o+0x1c, la luz local, 0, nadie, vuelta + 1, 0, 0, 0,
// 0), aunque la tabla esté llena (el rand() sale antes del alta). La luz local es (1, 1, 1) de 0x4fd81e, o (0,3; 0,5;
// 1) de 0x4fdb3f con las acciones 0x4d, 0x4f y 0x5a pasado el frame 5,5; da igual: el alta del 0x108/0 la pone en 0
// (0x51bd37).
Core::Expected<void, std::string> DropTheIceStorm(const ActorState& actor, const CharacterStepContext& context)
{
    constexpr int Rounds = 10;
    constexpr int Rise = 50;
    constexpr std::int16_t StormEffect = 0x108;
    constexpr glm::vec3 White{1.0f, 1.0f, 1.0f}; // 0x4fd81e; la de 0x4fdb3f la pisa el alta igual (0x51bd37)
    const auto cell = SkillCellPoint(actor, context, 0x4ffd1e);
    if (!cell.HasValue())
        return Core::Unexpected(cell.Error());
    const float height = cell.Value().z;
    for (int round = 0; round < Rounds; ++round)
    {
        glm::vec3 point = cell.Value();
        point.z =
            static_cast<float>(static_cast<double>(context.random.Next() % Rise * round) + static_cast<double>(height));
        const World::ModelEffectArguments arguments{.type = StormEffect,
                                                    .position = point,
                                                    .angles = actor.angles,
                                                    .light = White,
                                                    .subtype = 0,
                                                    .owner = nullptr,
                                                    .key = static_cast<std::uint16_t>(round + 1)};
        if (auto made = CreateBlastEffect(arguments, nullptr, context); !made.HasValue())
            return Core::Unexpected(made.Error());
    }
    return {};
}

// 0x4fe190..0x4fe24e (la 0x44, el subtipo 0) y 0x4fe442..0x4fe500 (la 0x47, el 1): con un blanco que no es o, el joint
// 0x632 del blanco hacia sí mismo (z + 0x6ba198) y el sonido 0x267.
Core::Expected<void, std::string> BindTheTarget(ActorState& actor, std::size_t slot, std::uint8_t skill,
                                                const CharacterStepContext& context, std::uint32_t read,
                                                std::uint32_t joint)
{
    const auto target = TargetOf(actor, slot, context, read);
    if (!target.HasValue())
        return Core::Unexpected(target.Error());
    if (target.Value() == nullptr || target.Value() == &actor)
        return {};
    constexpr std::int32_t BindSound = 0x267;
    return JointThenSound(joint, "el joint 0x632 sobre el blanco (0x530860)", skill, slot, BindSound, context);
}

// 0x4fe383..0x4fe43d, la 0x46: con blanco, 0x534f90(0x10f, blanco, 4) y (…, 9), el calco 0x61e/6 sobre él (0x518cf0)
// y el sonido 0x269.
Core::Expected<void, std::string> FreeTheTarget(ActorState& actor, std::size_t slot, std::uint8_t skill,
                                                const CharacterStepContext& context)
{
    const auto target = TargetOf(actor, slot, context, 0x4fe399);
    if (!target.HasValue())
        return Core::Unexpected(target.Error());
    if (target.Value() == nullptr)
        return {};
    if (context.joints.empty())
        return Core::Unexpected(std::format("0x4fe3ce: faltan los joints del juego (0x534f90) del lugar {}", slot));
    KillJointsOf(context.joints, FlameJoint, target.Value(), 4);
    KillJointsOf(context.joints, FlameJoint, target.Value(), 9);
    constexpr std::int32_t FreeSound = 0x269;
    const ActorState& freed = *target.Value();
    if (auto made = CreateHighEffect({.type = World::ColoredDecalEffect,
                                      .position = freed.position,
                                      .angles = freed.angles,
                                      .light = freed.light,
                                      .subtype = World::BindingDecalSubtype,
                                      .owner = target.Value()},
                                     skill, slot, context);
        !made.HasValue())
        return made;
    RequestTheSound(context, FreeSound);
    return {};
}

// 0x4fe986..0x4feab6 (la 0x40) y 0x502993..0x502aee (la 0x4b): con otra cosa que 0xa21 en la derecha, el 0x117/0 en su
// hueso (+0x2bd, 0x4ec110 con el offset en 0); con otra cosa que 0xa19 o 0xe12..0x1011 en la izquierda, el 0x117/1 en
// el suyo (+0x2dd); los dos con los ángulos de o y la luz (1; 0,6; 0,3) de la 0x40 o (1; 1; 1) de la 0x4b. Después, la
// 0x40 suena (0x1cf, con o de origen) y la 0x4b crea el 0x138 sobre o (0x502ae6) y suena (0x272).
Core::Expected<void, std::string> SwingTheHands(const ActorState& actor, std::size_t slot, std::uint8_t skill,
                                                const CharacterStepContext& context, std::uint32_t right,
                                                std::uint32_t left)
{
    constexpr std::int32_t PlainRight = 0xa21;
    constexpr std::int32_t PlainLeft = 0xa19;
    constexpr std::int32_t FirstShield = 0xe12;
    constexpr std::int32_t PastShields = 0x1012;
    constexpr std::uint8_t RaiseSkill = 0x40;
    constexpr std::int16_t DarkLordSkill = 0x117, ManaRune = 0x138;
    const glm::vec3 handLight =
        skill == RaiseSkill ? glm::vec3{1.0f, std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3e99999au)}
                            : glm::vec3(1.0f);
    const auto swing = [&](std::size_t hand, std::uint32_t address) -> Core::Expected<void, std::string>
    {
        const glm::vec3 point = ActorBonePoint(actor, actor.hands[hand].bone, glm::vec3(0.0f), context.world);
        if (auto made = CreateHighEffect({.type = DarkLordSkill,
                                          .position = point,
                                          .angles = actor.angles,
                                          .light = handLight,
                                          .subtype = static_cast<std::int32_t>(hand)},
                                         skill, slot, context);
            !made.HasValue())
            return Core::Unexpected(std::format("{:#x}: {}", address, made.Error()));
        return {};
    };
    if (HandOf(actor, 0) != PlainRight)
    {
        if (auto swung = swing(0, right); !swung.HasValue())
            return swung;
    }
    const std::int32_t held = HandOf(actor, 1);
    if (held != PlainLeft && !(held >= FirstShield && held < PastShields))
    {
        if (auto swung = swing(1, left); !swung.HasValue())
            return swung;
    }
    if (skill != RaiseSkill)
    {
        if (auto made = CreateHighEffect(
                {.type = ManaRune, .position = actor.position, .angles = actor.angles, .light = actor.light}, skill,
                slot, context);
            !made.HasValue())
            return Core::Unexpected("0x502ae6: " + made.Error());
    }
    constexpr std::int32_t RaiseSound = 0x1cf;
    constexpr std::int32_t OtherSwingSound = 0x272;
    // 0x4feaae: la 0x40 conserva o como origen.
    RequestTheSound(context, skill == RaiseSkill ? RaiseSound : OtherSwingSound,
                    skill == RaiseSkill ? &actor : nullptr);
    return {};
}

// 0x5023e4..0x502433 (y 0x5024d9..0x502528): la distancia en celdas de o a otro, con dx guardado en float, dy en la
// pila (53 bits) por su copia en float, la raíz (0x6a1920) sobre 100 (0x6b86b4) y el cociente guardado en float.
float CellsBetween(const ActorState& from, const ActorState& to)
{
    constexpr double CellSize = 100.0;
    const auto dx = static_cast<float>(static_cast<double>(from.position.x) - static_cast<double>(to.position.x));
    const double dy = static_cast<double>(from.position.y) - static_cast<double>(to.position.y);
    const auto storedDy = static_cast<float>(dy);
    const double squared = dy * static_cast<double>(storedDy) + static_cast<double>(dx) * static_cast<double>(dx);
    return static_cast<float>(std::sqrt(squared) / CellSize);
}

// 0x502352..0x502949, la 0x4c: el sonido 0x27d y el lazo de los 400 lugares hasta juntar diez blancos (0x502569): un
// monstruo vivo (+0x88 en 2) que no está en la acción 6, o un jugador vivo (+0x88 en 1) fuera de la 0xc3 que 0x5588d0
// deja atacar; de cada uno, la distancia y el alcance de la 0x4c sin dueño (0x5526f0(0x4c, 0), con los rand() de su
// entrada), y entra si el alcance no es menor (FCOMP con C0). Después rand() % 360, que no se usa antes del corte. Con
// +0x434 en −1 o desde 400, nada más; si no, la primera de dos vueltas: el punto de 0x4f5b10(o, 0, −140, 130), sin
// rand(), el ángulo con rand() % 360, 0 y rand() % 360, y el joint 0x14f hacia el blanco de +0x434. La lista de los
// diez la leen los joints de después (0x5026fa).
Core::Expected<void, std::string> StrikeAround(const ActorState& actor, std::size_t slot, std::uint8_t skill,
                                               const CharacterStepContext& context)
{
    constexpr std::int32_t AroundSound = 0x27d;
    constexpr std::size_t MostTargets = 10;
    constexpr std::uint8_t PlayerObject = 1;
    constexpr std::uint8_t MonsterObject = 2;
    constexpr std::uint8_t MonsterFallAction = 6;
    constexpr std::uint8_t PlayerFallAction = 0xc3;
    RequestTheSound(context, AroundSound);
    const HeroSkillContext ranges{.world = context.world, .random = context.random, .skills = context.skills};
    std::size_t gathered = 0;
    for (const ActorState& other : context.world.Actors().first<PoolSlots>())
    {
        bool candidate = other.alive && other.byte88 == MonsterObject && other.animation.action != MonsterFallAction;
        if (!candidate && other.alive && other.byte88 == PlayerObject && other.animation.action != PlayerFallAction)
        {
            const auto attackable = IsAttackableActor(context.world, other, context.controlDown);
            if (!attackable.HasValue())
                return Core::Unexpected(attackable.Error());
            candidate = attackable.Value();
        }
        if (candidate)
        {
            const float cells = CellsBetween(actor, other);
            const auto range = SkillRangeOf(skill, ranges);
            if (!range.HasValue())
                return Core::Unexpected(std::format("0x50243d: {} (la 0x4c del lugar {})", range.Error(), slot));
            if (range.Value() >= cells)
                ++gathered;
        }
        if (gathered >= MostTargets)
            break;
    }
    static_cast<void>(context.random.Next());
    const std::int32_t target = actor.skillTarget4c;
    if (target == NoTarget || target >= static_cast<std::int32_t>(PoolSlots))
        return {};
    if (target < 0)
    {
        return Core::Unexpected(
            std::format("0x5025b8: el blanco {} de +0x434 del lugar {} está fuera del pool", target, slot));
    }
    static_cast<void>(context.random.Next());
    static_cast<void>(context.random.Next());
    if (HasJointRoom(context))
        return CutAt(0x5026a4, "el primero de los cuatro joints 0x14f hacia el blanco de +0x434 (0x530860)", skill,
                     slot);
    // 50261e..5028e4: 2×2 para el blanco principal, N×2×2 para los encontrados y 6×3 para los seis 62c.
    constexpr std::size_t RemainingFixedDraws = 20;
    constexpr std::size_t DrawsPerTarget = 4;
    for (std::size_t draw = 0; draw < RemainingFixedDraws + gathered * DrawsPerTarget; ++draw)
        static_cast<void>(context.random.Next());
    return {};
}

// 0x4feabb..0x4feb36, la 0x3f: 0x518cf0(0xcb, o+0x10, o+0x1c, o+0xf4, 2, o, −1, …) y (0xcc, …, 3, sin dueño, −1, …), y
// el sonido 0x1cb desde o.
Core::Expected<void, std::string> RaiseTheRings(ActorState& actor, std::size_t slot, std::uint8_t skill,
                                                const CharacterStepContext& context)
{
    constexpr std::int32_t RingSound = 0x1cb;
    if (auto inner = CreateWizard(
            0x4feae8, WizardArguments(World::InnerCircleEffect, actor.position, actor.angles, actor.light, 2, &actor),
            skill, slot, context);
        !inner.HasValue())
        return inner;
    if (auto outer = CreateWizard(
            0x4feb1b, WizardArguments(World::OuterCircleEffect, actor.position, actor.angles, actor.light, 3), skill,
            slot, context);
        !outer.HasValue())
        return outer;
    RequestTheSound(context, RingSound, &actor);
    return {};
}

// 0x4fecb5..0x4fed2a, la 0x0a (el Hellfire): 0x59fb80(0xa) y 0x518cf0(0xcb, o+0x10, o+0x1c, o+0xf4, 0, o, o+0x8a,
// lugar, 0, 0, 0), cuyo alta con el héroe de dueño cuenta en el registro propio y manda los blancos de 300
// (0x51aa97); después 0x518cf0(0xcc, …, 0, sin dueño, −1, …). Lo de Kalima y el sonido 0x77 siguen en el caso.
Core::Expected<void, std::string> LightTheHellfire(ActorState& actor, std::size_t slot, std::uint8_t skill,
                                                   const CharacterStepContext& context)
{
    const std::int32_t index = OwnSkillIndex(context, skill);
    World::ModelEffectArguments inner =
        WizardArguments(World::InnerCircleEffect, actor.position, actor.angles, actor.light, 0, &actor,
                        static_cast<std::uint16_t>(actor.word8a));
    inner.byte89 = static_cast<std::uint8_t>(index & 0xff);
    if (auto created = CreateWizard(0x4fecf3, inner, skill, slot, context); !created.HasValue())
        return created;
    return CreateWizard(0x4fed25,
                        WizardArguments(World::OuterCircleEffect, actor.position, actor.angles, actor.light, 0), skill,
                        slot, context);
}

// 0x4ff51c..0x4ff5f2, la 0x26 (el Meteorito): el punto de la celda de la skill (+0x39c y +0x39d, más 0,5f por 100 en
// la x87, cada eje guardado en float) con la altura de 0x5f93c0, la luz local en (0,8f; 0,5; 0,1f) y dos
// 0x518cf0(0xc2, punto, o+0x1c, luz, 6, sin dueño, 0, …); después el sonido 0x8e y la tanda del héroe (0x4ff5f5).
// Sin las alturas del terreno, MuRet corta antes del primero.
Core::Expected<void, std::string> RainTheMeteors(ActorState& actor, std::size_t slot, std::uint8_t skill,
                                                 const CharacterStepContext& context)
{
    constexpr double CellCentre = 0.5; // 0x6b882c
    constexpr double CellSize = 100.0; // 0x6b86b4
    constexpr glm::vec3 MeteorLight{std::bit_cast<float>(0x3f4ccccdu), 0.5f, std::bit_cast<float>(0x3dcccccdu)};
    constexpr std::int32_t MeteorSubtype = 6;
    constexpr std::uint16_t MeteorKey = 0;
    constexpr std::int32_t MeteorSound = 0x8e;
    const World::WorldTerrain& terrain = context.terrain != nullptr ? *context.terrain : context.world.Map().terrain;
    if (!terrain.heights || !terrain.attributes)
        return Core::Unexpected(std::string("0x4ff570: falta la altura del terreno (0x5f93c0) del Meteorito"));
    glm::vec3 point{};
    point.x = static_cast<float>((static_cast<double>(actor.skillCellX) + CellCentre) * CellSize);
    point.y = static_cast<float>((static_cast<double>(actor.skillCellY) + CellCentre) * CellSize);
    point.z = static_cast<float>(
        World::TerrainHeightAt(*terrain.heights, *terrain.attributes, point.x, point.y, context.fixedHeight));
    for (const std::uint32_t address : {0x4ff5b2u, 0x4ff5dcu})
    {
        if (auto created = CreateWizard(
                address,
                WizardArguments(World::FireEffect, point, actor.angles, MeteorLight, MeteorSubtype, nullptr, MeteorKey),
                skill, slot, context);
            !created.HasValue())
            return created;
    }
    RequestTheSound(context, MeteorSound);
    CountHeroVolley(actor, context);
    return {};
}
} // namespace

Core::Expected<void, std::string> StepTheHeadSkill(ActorState& actor, std::size_t slot, std::uint8_t skill,
                                                   const CharacterStepContext& context, const glm::vec3& light)
{
    switch (skill)
    {
    case 0x1e:
    case 0x1f:
    case 0x20:
    case 0x21:
    case 0x22:
    case 0x23:
    case 0x24:
        // 0x4fdef2..0x4fdf26: 0x518cf0(0x61e, o+0x10, o+0x1c, o+0xf4, 3, o, −1, 0, 0, 0, 0), y nada más. Su paso suelta
        // los joints 619/3 de la CL11 (GameCharacterEffects.h, StepPetSummon). Con la tabla llena, nada.
        static_cast<void>(context.world.Map().effects.CreatePetSummon(actor.position, actor.angles, actor.light, &actor,
                                                                      World::SummonDecalSubtype));
        return {};
    case 0x2a:
        return ShakeTheGround(actor, context);
    case 0x37:
    case 0x38:
        // 0x4fdfb3 y 0x4fdfe4: el arma, y nada más.
        KeepTheWeapon(actor);
        return {};
    case 0x30:
        return RingTheSpirits(actor, slot, skill, context,
                              {.subtype = 2,
                               .jointAddress = 0x4fe0e4,
                               .decalSubtype = World::PulsingDecalSubtype,
                               .light = actor.light,
                               .sound = 0x7f});
    case 0x43:
        return CallTheStorm(actor, context);
    case 0x44:
        return BindTheTarget(actor, slot, skill, context, 0x4fe1a6, 0x4fe235);
    case 0x45:
        return RingTheSpirits(actor, slot, skill, context,
                              {.subtype = 0x15,
                               .jointAddress = 0x4fe322,
                               .decalSubtype = World::CoolPulsingDecalSubtype,
                               .light = CaseLight(actor),
                               .sound = 0x268});
    case 0x46:
        return FreeTheTarget(actor, slot, skill, context);
    case 0x47:
        return BindTheTarget(actor, slot, skill, context, 0x4fe458, 0x4fe4e7);
    case 0x48:
        return RaiseTheAuras(actor, context);
    case 0x35:
        return SpinTheEnergy(actor, context);
    case 0x40:
        return SwingTheHands(actor, slot, skill, context, 0x4fea10, 0x4fea9b);
    case 0x3f:
        return RaiseTheRings(actor, slot, skill, context);
    case 0x41:
        return RaiseTheSpirals(actor, context);
    case 0x31:
    {
        // 0x4febdb..0x4fec2b: 0x518cf0(0x6ac, o+0x10, o+0x1c, o+0xf4, 0, o, o+0x8a, [0x5767948], 0, 0, 0) sin
        // 0x59fb80 antes (Scenes::FireBreathEffect: sus cuarenta joints 0x617/1) y el sonido 0x72.
        constexpr std::int32_t BreathSound = 0x72;
        if (auto made =
                CreateHighEffect({.type = FireBreathEffect,
                                  .position = actor.position,
                                  .angles = actor.angles,
                                  .light = actor.light,
                                  .owner = &actor,
                                  .key = static_cast<std::uint16_t>(actor.word8a),
                                  .byte89 = static_cast<std::uint8_t>(context.world.Runtime().lastShotSkill & 0xffu)},
                                 skill, slot, context);
            !made.HasValue())
            return made;
        RequestTheSound(context, BreathSound);
        return {};
    }
    case 0x29:
    {
        // 0x4fec30: el arma, 0x59fb80(0x29), el 0xf3 sobre o (0x4fec9a) y el sonido 0x73.
        KeepTheWeapon(actor);
        const std::int32_t index = OwnSkillIndex(context, skill);
        if (auto made = CreateOwnedBlast(0xf3, actor.position, actor, index, context); !made.HasValue())
            return made;
        RequestTheSound(context, 0x73);
        return {};
    }
    case 0x0a:
        if (auto lit = LightTheHellfire(actor, slot, skill, context); !lit.HasValue())
            return lit;
        // 4fed2d..4ff4a8: Y, X y 437860(x,y,2,-1500); puntero 723c48 nulo es no-op, no una frontera.
        if (World::IsKalimaMap(context.world.Runtime().map))
        {
            const LogicalCell cell = ReadLogicalCell(actor, context.random);
            constexpr std::int32_t WaveRadius = 2, WaveAmplitude = -1500;
            if (auto* kalima = context.world.Map().kalima.get(); kalima != nullptr)
            {
                const auto disturbed = kalima->Disturb(cell.x, cell.y, WaveRadius, WaveAmplitude, context.random);
                if (!disturbed.HasValue())
                    return Core::Unexpected(disturbed.Error());
            }
        }
        RequestTheSound(context, 0x77);
        return {};
    case 0x28:
        // 0x4ff4c3: 0x518cf0(0xcb, o+0x10, o+0x1c, o+0xf4, 1, o, −1, …).
        if (auto circle = CreateWizard(
                0x4ff4ef,
                WizardArguments(World::InnerCircleEffect, actor.position, actor.angles, actor.light, 1, &actor), skill,
                slot, context);
            !circle.HasValue())
            return circle;
        context.world.Runtime().StopSound(0x92, true); // 0x4ff4fe: rebobina antes de pedir 0x93.
        RequestTheSound(context, 0x93);
        return {};
    case 0x26:
        return RainTheMeteors(actor, slot, skill, context);
    case 0x27:
        if (auto storm = DropTheIceStorm(actor, context); !storm.HasValue())
            return storm;
        CountHeroVolley(actor, context);
        RequestTheSound(context, 0x90);
        return {};
    case 0x05:
        return CastTheFlame(actor, context);
    case 0x08:
    {
        // 0x500c10..0x500c5c (el Twister): 0x59fb80(8) y 0x518cf0(0xcf, o+0x10, o+0x1c, la luz local, 0, o, o+0x8a,
        // lugar, 0, 0, 0); después el sonido 0x74.
        constexpr std::int32_t TwisterSound = 0x74;
        const std::int32_t index = OwnSkillIndex(context, skill);
        World::ModelEffectArguments storm = WizardArguments(World::StormEffect, actor.position, actor.angles, light, 0,
                                                            &actor, static_cast<std::uint16_t>(actor.word8a));
        storm.byte89 = static_cast<std::uint8_t>(index & 0xff);
        if (auto created = CreateWizard(0x500c49, storm, skill, slot, context); !created.HasValue())
            return created;
        RequestTheSound(context, TwisterSound);
        return {};
    }
    case 0x0c:
    {
        // 0x500c64..0x500ccf: el punto de 0x4f5b10(o, −20, −90, 100), sin rand(), 0x59fb80(0xc) y 0x518cf0(0x5e8,
        // punto, o+0x1c, la luz del paso, 0, o, o+0x8a, lugar, 0, 0, 0): el rayo y, con el héroe de dueño, sus cuatro
        // 0x517a20. Después, el sonido 0x7a.
        constexpr std::int16_t LightningRay = 0x5e8;
        constexpr glm::vec3 RayOffset{-20.0f, -90.0f, 100.0f};
        const glm::vec3 point = PointAround(actor, RayOffset);
        const std::int32_t index = OwnSkillIndex(context, skill);
        if (auto made = CreateHighEffect({.type = LightningRay,
                                          .position = point,
                                          .angles = actor.angles,
                                          .light = CaseLight(actor),
                                          .owner = &actor,
                                          .key = static_cast<std::uint16_t>(actor.word8a),
                                          .byte89 = static_cast<std::uint8_t>(index)},
                                         skill, slot, context);
            !made.HasValue())
            return made;
        RequestTheSound(context, 0x7a);
        return {};
    }
    case 0x0d:
    {
        // 0x500cd4: la celda con su altura, 0x59fb80(0xd) y dos 0xf5 ahí con o de dueño (0x500d7b y 0x500dba).
        const auto cell = SkillCellPoint(actor, context, 0x500d28);
        if (!cell.HasValue())
            return Core::Unexpected(cell.Error());
        const std::int32_t index = OwnSkillIndex(context, skill);
        for (int blast = 0; blast < 2; ++blast)
        {
            if (auto made = CreateOwnedBlast(0xf5, cell.Value(), actor, index, context); !made.HasValue())
                return made;
        }
        CountHeroVolley(actor, context);
        return {};
    }
    case 0x0e:
        if (auto burst = ScatterCombatBurstAt(actor.position, context); !burst.HasValue())
            return burst;
        // 0x5014da: el 0xf6 sobre o, con o de dueño.
        if (auto made = CreateOwnedBlast(0xf6, actor.position, actor, OwnSkillIndex(context, skill), context);
            !made.HasValue())
            return made;
        CountHeroVolley(actor, context);
        return {};
    case 0x09:
    {
        // 0x501bab..0x501ca4: cuatro vueltas con 0x59fb80(9); en cada una, dos joints 0x613/0 colgados de o desde 100
        // más arriba, con los ángulos (0, 0, 90 · i): el de ancho 80 con la key +0x8a, el índice de la skill y +0x1bd
        // (0x501c67), y el de ancho 20 con la key −1 (0x501c9c).
        constexpr int Turns = 4;
        constexpr float WideWidth = 80.0f, ThinWidth = 20.0f; // 0x42a00000 y 0x41a00000
        const glm::vec3 point{actor.position.x, actor.position.y,
                              static_cast<float>(static_cast<double>(actor.position.z) + 100.0)};
        for (int turn = 0; turn < Turns; ++turn)
        {
            const glm::vec3 angles{0.0f, 0.0f, static_cast<float>(static_cast<double>(turn) * 90.0)};
            const std::int32_t index = OwnSkillIndex(context, skill);
            if (auto wide = CreateTheSpirit(actor, context,
                                            {.address = 0x501c67,
                                             .from = point,
                                             .to = actor.position,
                                             .angles = angles,
                                             .width = WideWidth,
                                             .key = actor.word8a,
                                             .skill = index,
                                             .serial = actor.magicKey});
                !wide.HasValue())
                return wide;
            if (auto thin = CreateTheSpirit(actor, context,
                                            {.address = 0x501c9c,
                                             .from = point,
                                             .to = actor.position,
                                             .angles = angles,
                                             .width = ThinWidth,
                                             .key = -1});
                !thin.HasValue())
                return thin;
        }
        CountHeroVolley(actor, context);
        RequestTheSound(context, 0x75);
        return {};
    }
    case 0x4c:
        return StrikeAround(actor, slot, skill, context);
    case 0x2c:
    {
        // 0x50294e..0x50298e: 0x518cf0(0x13b, o+0x10, o+0x1c, la luz del paso, 0, o, −1, 0, 0, 0, 0), la llama del
        // asedio sobre o, y el sonido 0x26c.
        constexpr std::int16_t SwordForce = 0x13b;
        constexpr std::int32_t ForceSound = 0x26c;
        if (auto made = CreateHighEffect({.type = SwordForce,
                                          .position = actor.position,
                                          .angles = actor.angles,
                                          .light = CaseLight(actor),
                                          .owner = &actor},
                                         skill, slot, context);
            !made.HasValue())
            return Core::Unexpected("0x502975: " + made.Error());
        RequestTheSound(context, ForceSound);
        return {};
    }
    case 0x4b:
        return SwingTheHands(actor, slot, skill, context, 0x502a23, 0x502ab4);
    default:
        // 0x502aff, como el 0.
        return {};
    }
}

// 0x504d87..0x504f51, la 0x3d: el punto (80, 0, 20) del primer hueso de o (0x4ec110) y tres 0x116/0 con el blanco de
// dueño y los ángulos (0, 0, z de o + 90), (0, 0, z) y (0, 0, z − 90); la "luz" (0, 0, techo del blanco (+0x138) / 1)
// del primero y (0, 0, eso − techo / 2) de los otros dos, que el alta guarda como la partida (el punto del blanco al
// que van). Después, el 0x117/0 y el 0x117/1 ahí con los ángulos de o y la luz (1; 0,6; 0,3). Sin rand() ni sonido.
Core::Expected<void, std::string> LaunchThePierParts(const ActorState& actor, ActorState& victim, std::size_t slot,
                                                     std::uint8_t skill, const CharacterStepContext& context)
{
    constexpr glm::vec3 LaunchOffset{80.0f, 0.0f, 20.0f}; // 0x42a00000 y 0x41a00000
    constexpr std::size_t LaunchBone = 0;
    constexpr double Turn = 90.0, Half = 2.0, Whole = 1.0; // 0x6b8948, 0x6b8818 y 0x6b87d4
    constexpr std::int16_t PierPart = 0x116, DarkLordSkill = 0x117;
    const glm::vec3 point = ActorBonePoint(actor, LaunchBone, LaunchOffset, context.world);
    const double yaw = static_cast<double>(actor.angles.z);
    glm::vec3 aim{0.0f, 0.0f, static_cast<float>(static_cast<double>(victim.boundsTop) / Whole)};
    const auto launch = [&](float z, std::uint32_t address) -> Core::Expected<void, std::string>
    {
        if (auto made = CreateHighEffect(
                {.type = PierPart, .position = point, .angles = {0.0f, 0.0f, z}, .light = aim, .owner = &victim}, skill,
                slot, context);
            !made.HasValue())
            return Core::Unexpected(std::format("{:#x}: {}", address, made.Error()));
        return {};
    };
    if (auto made = launch(static_cast<float>(yaw + Turn), 0x504e40); !made.HasValue())
        return made;
    aim.z = static_cast<float>(static_cast<double>(aim.z) - static_cast<double>(victim.boundsTop) / Half);
    if (auto made = launch(actor.angles.z, 0x504e9c); !made.HasValue())
        return made;
    if (auto made = launch(static_cast<float>(yaw - Turn), 0x504ee0); !made.HasValue())
        return made;
    const glm::vec3 handLight{1.0f, std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3e99999au)};
    for (const std::int32_t subtype : {0, 1})
    {
        if (auto made = CreateHighEffect({.type = DarkLordSkill,
                                          .position = point,
                                          .angles = actor.angles,
                                          .light = handLight,
                                          .subtype = subtype},
                                         skill, slot, context);
            !made.HasValue())
            return Core::Unexpected(std::format("{:#x}: {}", subtype == 0 ? 0x504f1fu : 0x504f49u, made.Error()));
    }
    return {};
}

Core::Expected<void, std::string> StepTheTargetSkill(ActorState& actor, ActorState& victim, std::size_t slot,
                                                     std::uint8_t skill, const CharacterStepContext& context,
                                                     const glm::vec3& light)
{
    switch (skill)
    {
    case 0x18:
    {
        // 0x504379..0x5043aa: 0x53e2b0(c, o, 0, 0x59fb80(+0x38e), 1, 0), la tanda sin blanco; sigue en la 0x34
        // (0x5043b2).
        const std::int32_t index = OwnSkillIndex(context, ReadTheSkill(actor, context.random));
        if (auto fired = FireTheArrow(actor, nullptr, slot, index, 1, 0, context); !fired.HasValue())
            return fired;
    }
        [[fallthrough]];
    case 0x34:
    case 0x33:
    {
        // 0x5043b2..0x5043fb y 0x504403..0x50444b: la primera lectura es la skill de 0x53e2b0 y la segunda va a
        // 0x59fb80; 0x53e2b0(c, o, 0, lugar, 0, skill).
        const std::uint8_t shot = ReadTheSkill(actor, context.random);
        const std::int32_t index = OwnSkillIndex(context, ReadTheSkill(actor, context.random));
        return FireTheArrow(actor, nullptr, slot, index, 0, shot, context);
    }
    case 0x2e:
    {
        // 0x504453..0x5044b4: lo mismo con el blanco, 0x53e2b0(c, o, blanco, lugar, 0, skill), y el sonido 0x26e.
        constexpr std::int32_t BlastSound = 0x26e;
        const std::uint8_t shot = ReadTheSkill(actor, context.random);
        const std::int32_t index = OwnSkillIndex(context, ReadTheSkill(actor, context.random));
        if (auto fired = FireTheArrow(actor, &victim, slot, index, 0, shot, context); !fired.HasValue())
            return fired;
        RequestTheSound(context, BlastSound);
        return {};
    }
    case 0x1a:
        // 0x5044b9..0x5044f1: 0x518cf0(0x61e, blanco+0x10, blanco+0x1c, blanco+0xf4, 1, blanco, −1, 0, 0, 0, 0), el
        // calco que suelta los 619/1 hacia su dueño (GameCharacterEffects.h). Con la tabla llena, nada.
        static_cast<void>(
            context.world.Map().effects.CreatePetSummon(victim.position, victim.angles, victim.light, &victim));
        return {};
    case 0x1b:
        // 0x5044fe: con +0x38d y sin 0x4000 en o+0x78, el calco 0x61e/2 sobre el blanco.
        if (actor.skillSucceeded == 0 || (actor.flags78 & QuietFlag) != 0)
            return {};
        if (auto made = CreateHighEffect({.type = World::ColoredDecalEffect,
                                          .position = victim.position,
                                          .angles = victim.angles,
                                          .light = victim.light,
                                          .subtype = 2,
                                          .owner = &victim},
                                         skill, slot, context);
            !made.HasValue())
            return made;
        {
            constexpr std::uint32_t BoundFlag = 8;
            const bool alreadyBound = (victim.flags78 & BoundFlag) != 0;
            if (!alreadyBound)
                victim.flags78 |= BoundFlag;
            const bool hasBinding = std::ranges::any_of(context.joints,
                                                        [&victim](const auto& joint)
                                                        {
                                                            return joint.alive && joint.type == FlameJoint &&
                                                                   joint.ownerContext == &victim &&
                                                                   (joint.subtype == 4 || joint.subtype == 9);
                                                        });
            // 0x5045b7/0x50466e: el blanco de los cinco es el +0x3a4 de c, con signo (movsx).
            if (!alreadyBound || !hasBinding)
                return CreateFiveAuras(alreadyBound ? 0x5046b0 : 0x5045f9, victim, 4,
                                       std::bit_cast<std::int16_t>(actor.attackTarget), context);
        }
        return {};
    case 0x1c:
    {
        // 0x5046bf..0x50473b: sin 0x4000 en o+0x78, 0x518cf0(0x61e, blanco+0x10, blanco+0x1c, blanco+0xf4, 3, blanco,
        // −1, 0, 0, 0, 0), cuyo paso suelta los joints 619/3 de la CL11 (StepPetSummon), y con +0x38d de c, 4 en +0x78
        // del blanco. Con la tabla llena, sin el calco y lo demás igual.
        constexpr std::uint32_t SummonedFlag = 4;
        if ((actor.flags78 & QuietFlag) != 0)
            return {};
        static_cast<void>(context.world.Map().effects.CreatePetSummon(victim.position, victim.angles, victim.light,
                                                                      &victim, World::SummonDecalSubtype));
        if (actor.skillSucceeded != 0)
            victim.flags78 |= SummonedFlag;
        return {};
    }
    case 0x07:
    {
        // 0x504740..0x504800 (el Ice): 0x518cf0(0xc1, blanco+0x10, o+0x1c, la luz local, 0, sin dueño, −1, …) y cinco
        // pedazos de hielo 0xca (blanco+0x10, o+0x1c, o+0xf4; ModelEffects::CreateIceShard, cinco rand() cada uno);
        // con +0x38d, 2 en +0x78 del blanco, y el sonido 0x78.
        constexpr int Shards = 5;
        if (auto created =
                CreateWizard(0x504768, WizardArguments(World::IceEffect, victim.position, actor.angles, light, 0),
                             skill, slot, context);
            !created.HasValue())
            return created;
        for (int shard = 0; shard < Shards; ++shard)
            static_cast<void>(
                context.world.Map().effects.CreateIceShard(victim.position, actor.angles, actor.light, context.random));
        constexpr std::uint32_t FrozenFlag = 2;
        constexpr std::int32_t IceSound = 0x78;
        if (actor.skillSucceeded != 0)
            victim.flags78 |= FrozenFlag;
        RequestTheSound(context, IceSound);
        return {};
    }
    case 0x10:
    {
        // 0x504808..0x5048e5: el 0x189 prende 0x100 en su +0x78 y sale; con 0x4000, nada; si no, 0x100 en +0x78 del
        // blanco, el sonido 0x84, 0x534f90(0x10f, blanco, 0) y cinco joints 0x10f/0.
        constexpr std::uint16_t SelfFlagType = 0x189;
        constexpr std::uint32_t ShieldFlag = 0x100;
        constexpr std::int32_t ShieldSound = 0x84;
        if (actor.type == SelfFlagType)
        {
            actor.flags78 |= ShieldFlag;
            return {};
        }
        if ((actor.flags78 & QuietFlag) != 0)
            return {};
        victim.flags78 |= ShieldFlag;
        RequestTheSound(context, ShieldSound);
        if (context.joints.empty())
            return Core::Unexpected(std::format("0x504876: faltan los joints del juego (0x534f90) del lugar {}", slot));
        KillJointsOf(context.joints, FlameJoint, &victim, 0);
        return CreateFiveAuras(0x5048db, victim, 0, -1, context);
    }
    case 0x01:
    {
        // 0x5048f1..0x50492b: el jugador crea el 0xc3 (blanco+0x10, o+0x1c, o+0xf4, 0, sin dueño, −1, …); después
        // TODOS los tipos, con la luz local en (0,4f; 0,6f; 1), intentan diez partículas 5f2/1 (0x50497e).
        constexpr std::uint16_t PlayerType = 0x20a;
        if (actor.type == PlayerType)
        {
            if (auto created = CreateWizard(
                    0x504926, WizardArguments(World::PoisonEffect, victim.position, actor.angles, actor.light, 0),
                    skill, slot, context);
                !created.HasValue())
                return created;
        }
        // 0x50492e..0x504986: diez 0x53fe70(0x5f2, o + 0x10 del blanco, c + 0x1c, (0,4f; 0,6f; 1), 1, 1,0f, 0), el
        // humo del veneno (0x541eb6, siete rand() cada una mientras haya lugar).
        constexpr std::int32_t PoisonSmoke = 0x5f2;
        constexpr std::int32_t PoisonSmokeSubtype = 1;
        constexpr int PoisonSmokeCount = 10;
        const glm::vec3 poisonLight{std::bit_cast<float>(0x3ecccccdu), std::bit_cast<float>(0x3f19999au), 1.0f};
        if (context.particles == nullptr)
            return Core::Unexpected(std::format("0x50497e: falta el pool de partículas del lugar {}", slot));
        for (int smoke = 0; smoke < PoisonSmokeCount; ++smoke)
        {
            const auto created =
                context.particles->CreateChecked(PoisonSmoke, victim.position, actor.angles, poisonLight,
                                                 PoisonSmokeSubtype, 1.0f, context.worldClock, context.random);
            if (!created.HasValue())
            {
                return Core::Unexpected(std::format("0x50497e: la partícula 0x5f2/1 del lugar {} falla con error {}",
                                                    slot, static_cast<unsigned>(created.Error())));
            }
        }
        if (actor.skillSucceeded != 0)
            victim.flags78 |= 1;
        RequestTheSound(context, 0x22);
        return {};
    }
    case 0x02:
        // 0x5049bf..0x5049fd (el Meteor): 0x518cf0(0xc2, blanco+0x10, blanco+0x1c, o+0xf4, 0, sin dueño, −1, …) y el
        // sonido 0x42.
        if (auto created = CreateWizard(
                0x5049ef, WizardArguments(World::FireEffect, victim.position, victim.angles, actor.light, 0), skill,
                slot, context);
            !created.HasValue())
            return created;
        RequestTheSound(context, FireSound);
        return {};
    case 0x2d:
    {
        // 0x504a0a..0x504abf: tres 0x518cf0(0x139, o+0x10, o+0x1c, o+0xf4, subtipo, blanco, −1, 0, 0, 0, 0), con los
        // subtipos 0, 1 y 2 (Scenes::JavelinEffect, un rand() cada uno), y el sonido 0x26d.
        constexpr std::int32_t JavelinSound = 0x26d;
        constexpr std::array<std::int32_t, 3> Subtypes{0, 1, 2};
        constexpr std::array<std::uint32_t, 3> Addresses{0x504a3a, 0x504a72, 0x504aa9};
        for (std::size_t index = 0; index < Subtypes.size(); ++index)
        {
            if (auto made = CreateHighEffect({.type = JavelinEffect,
                                              .position = actor.position,
                                              .angles = actor.angles,
                                              .light = actor.light,
                                              .subtype = Subtypes[index],
                                              .owner = &victim},
                                             skill, slot, context);
                !made.HasValue())
                return Core::Unexpected(std::format("{:#x}: {}", Addresses[index], made.Error()));
        }
        RequestTheSound(context, JavelinSound);
        return {};
    }
    case 0x49:
        return RaiseTheInferno(actor, context);
    case 0x4a:
    {
        // 0x504b51..0x504b99: el 0x116/2 sobre o con el blanco de dueño y el sonido 0x271.
        constexpr std::int16_t PierPart = 0x116;
        constexpr std::int32_t DivingSubtype = 2;
        constexpr std::int32_t DiveSound = 0x271;
        if (auto made = CreateHighEffect({.type = PierPart,
                                          .position = actor.position,
                                          .angles = actor.angles,
                                          .light = actor.light,
                                          .subtype = DivingSubtype,
                                          .owner = &victim},
                                         skill, slot, context);
            !made.HasValue())
            return Core::Unexpected("0x504b80: " + made.Error());
        RequestTheSound(context, DiveSound);
        return {};
    }
    case 0x04:
        // 0x504b9e..0x504bdc (la Fire Ball): 0x518cf0(0xc2, o+0x10, los ángulos hacia el blanco, o+0xf4, 1, el blanco
        // de dueño, −1, …) y el sonido 0x42.
        if (auto created = CreateWizard(0x504bce,
                                        WizardArguments(World::FireEffect, actor.position,
                                                        TowardsTheTarget(actor, victim), actor.light, 1, &victim),
                                        skill, slot, context);
            !created.HasValue())
            return created;
        RequestTheSound(context, FireSound);
        return {};
    case 0x0b:
    {
        // 0x504be9..0x504cce (el Power Wave): 0x518cf0(0xce, o+0x10, los ángulos hacia el blanco, o+0xf4, 0, sin dueño,
        // −1, …); el 0x164 crea antes dos más, con el rumbo 10 más y después 20 menos (fld/fadd/fstp sobre el local),
        // y lo devuelve con 10 más. Después, el sonido 0x76.
        constexpr std::uint16_t TripleType = 0x164;
        constexpr double SideTurn = 10.0;  // 0x6b87a8
        constexpr double BothSides = 20.0; // 0x6b86d0
        constexpr std::int32_t WaveSound = 0x76;
        glm::vec3 angles = TowardsTheTarget(actor, victim);
        const auto wave = [&](std::uint32_t address)
        {
            return CreateWizard(address, WizardArguments(World::MagicEffect, actor.position, angles, actor.light, 0),
                                skill, slot, context);
        };
        if (actor.type == TripleType)
        {
            angles.z = static_cast<float>(static_cast<double>(angles.z) + SideTurn);
            if (auto created = wave(0x504c38); !created.HasValue())
                return created;
            angles.z = static_cast<float>(static_cast<double>(angles.z) - BothSides);
            if (auto created = wave(0x504c7c); !created.HasValue())
                return created;
            angles.z = static_cast<float>(static_cast<double>(angles.z) + SideTurn);
        }
        if (auto created = wave(0x504cc0); !created.HasValue())
            return created;
        RequestTheSound(context, WaveSound);
        return {};
    }
    case 0x3c:
    {
        // 0x504cdb..0x504d7a: dos 0x518cf0(0x114, o+0x10, o+0x1c, o+0xf4, 1, 0, −1, 0, 0, 0, 0) (0x504d05 y 0x504d37),
        // uno con el 0x115/0 (0x504d69) y después el sonido 0x1c8. Cada 0x114/1 suelta dos joints 0x6e8/0 (0x51c25f).
        constexpr std::uint16_t Wave = 0x114, Piercing = 0x115;
        constexpr std::int32_t SwirlSubtype = 1, WaveSound = 0x1c8;
        for (int wave = 0; wave < 2; ++wave)
            if (auto made = CreateHighEffect({.type = Wave,
                                              .position = actor.position,
                                              .angles = actor.angles,
                                              .light = actor.light,
                                              .subtype = SwirlSubtype},
                                             skill, slot, context);
                !made.HasValue())
                return made;
        if (auto made = CreateHighEffect(
                {.type = Piercing, .position = actor.position, .angles = actor.angles, .light = actor.light}, skill,
                slot, context);
            !made.HasValue())
            return made;
        RequestTheSound(context, WaveSound);
        return {};
    }
    case 0x3d:
        return LaunchThePierParts(actor, victim, slot, skill, context);
    case 0x11:
    {
        // 0x504f56..0x504fd0: los +0x376 de las tablas de 0x50561a y 0x505672 que van a 0x504fd7, y el 0x76, no
        // hacen nada; los demás (los jugadores, con 0xffff) crean el 0xd7 (el tipo 0x15e) o el 0x5ca.
        constexpr std::array<std::uint16_t, 31> QuietKinds{
            0x25, 0x2e, 0x3d, 0x42, 0x45, 0x46, 0x49, 0x4b, 0x4d, 0x57, 0x59, 0x5d, 0x5f, 0x63,  0x70, 0x74,
            0x76, 0x7a, 0x7c, 0x80, 0x82, 0x8d, 0x8f, 0xa3, 0xa5, 0xa7, 0xa9, 0xab, 0xad, 0x125, 0x12f};
        constexpr std::uint16_t RayType = 0x15e;
        if (std::ranges::find(QuietKinds, actor.word376) != QuietKinds.end())
            return {};
        if (actor.type == RayType)
        {
            // 0x504fdc..0x505022: 0x518cf0(0xd7, o+0x10, los ángulos hacia el blanco, o+0xf4, 0, el blanco de dueño,
            // −1, …), sin sonido.
            return CreateWizard(0x50501a,
                                WizardArguments(World::SnowEffect, actor.position, TowardsTheTarget(actor, victim),
                                                actor.light, 0, &victim),
                                skill, slot, context);
        }
        return EffectThenSound(0x505053, "el efecto 0x5ca (0x518cf0)", skill, slot, 0x76, context);
    }
    default:
        // 0x505069: el 3 y los demás.
        return {};
    }
}

Core::Expected<void, std::string> SparkTheTarget(const ActorState& actor, const ActorState& victim, std::size_t slot,
                                                 const CharacterStepContext& context)
{
    // 0x52e740..0x52e804: 0x53fe70(0x5c6, o + 0x10 del blanco, sus ángulos, (1, 1, 1), 0, 1,0f, 0), la chispa del golpe
    // (0x54058e..0x540592: la /0 sale a 0x54304c con sólo el prefijo); el (0, 50, 0) girado por los ángulos del que
    // pega (0x5fc8f0 y 0x5fcbd0) y sumado a la posición no lo lee nadie. Después, veinte veces dos rand() y
    // 0x53fe70(0x5c5, la misma posición y ángulos, (1, 1, 1), 0, 1,0f, 0), aun si el pool está lleno.
    constexpr std::int32_t HitSpark = 0x5c6;
    constexpr std::int32_t FairySpark = 0x5c5;
    constexpr int FairySparks = 20;
    if (context.particles == nullptr)
        return Core::Unexpected(std::format("0x52e77b: falta el pool de partículas del lugar {}", slot));
    const auto fail = [&](std::uint32_t address, std::int32_t type, World::ParticleError error)
    {
        return Core::Unexpected(std::format("{:#x}: la partícula {:#x}/0 de 0x52e740 del lugar {} (acción {:#x}) falla "
                                            "con error {}",
                                            address, type, slot, actor.animation.action, static_cast<unsigned>(error)));
    };
    const auto spark = context.particles->CreateChecked(HitSpark, victim.position, victim.angles, glm::vec3(1.0f), 0,
                                                        1.0f, context.worldClock, context.random);
    if (!spark.HasValue())
        return fail(0x52e77b, HitSpark, spark.Error());
    for (int fairy = 0; fairy < FairySparks; ++fairy)
    {
        static_cast<void>(context.random.Next());
        static_cast<void>(context.random.Next());
        const auto created = context.particles->CreateChecked(
            FairySpark, victim.position, victim.angles, glm::vec3(1.0f), 0, 1.0f, context.worldClock, context.random);
        if (!created.HasValue())
            return fail(0x52e7f3, FairySpark, created.Error());
    }
    return {};
}
} // namespace Client::Scenes
