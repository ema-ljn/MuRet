#include "Scenes/GameCharacterEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Maps/MapId.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/PassColors.h"
#include "Render/SpriteVertex.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GuildMarkEffects.h"
#include "Scenes/HighModelEffects.h"
#include "Scenes/JointFamilies.h"
#include "Scenes/PartyActors.h"
#include "Scenes/SceneId.h"
#include "Scenes/SocialState.h"
#include "Scenes/VertexEffects.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldState.h"
#include "World/BlastModelEffects.h"
#include "World/ChaosCastle.h"
#include "World/Ftol.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "World/PortableParticleTrigonometry.h"
#include "World/Sprites.h"
#include "World/TerrainHeight.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"

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

namespace Client::Scenes
{
namespace
{
using Result = Core::Expected<void, std::string>;

// Las texturas de los sprites (0x61ccb0): flare01, Spark03, lightning2 y Shiny02.
constexpr std::uint16_t Flare = 0x5ac;
constexpr std::uint16_t ThirdSpark = 0x5c6;
constexpr std::uint16_t Lightning = 0x5d5;
constexpr std::uint16_t Shiny = 0x5fd;

constexpr std::uint32_t HiddenAttachmentFlag = 0x4000;

// 0x50ea38..0x50ea94: la piel especial del mago, en el hueso 19 (0x390 / 0x30), corrida (−4, 11, 0).
constexpr std::uint8_t MageSkinBone = 19;
constexpr glm::vec3 MageSkinOffset{-4.0f, 11.0f, 0.0f};
constexpr float MageSparkScale = std::bit_cast<float>(0x3f19999au);  // 0,6
constexpr float MageShinyClock = std::bit_cast<float>(0x3a83126fu);  // 0x6b89c8, 0,001f
constexpr float MageShinyFactor = std::bit_cast<float>(0x3ecccccdu); // 0x6b87b4, 0,4f

// 0x50f828: las acciones con efectos y la clase 4 (el Dark Lord).
constexpr std::uint8_t LightningAction = 0x94;
constexpr std::uint8_t FirstEffectAction = 0x51;
constexpr std::uint8_t SecondEffectAction = 0x5b;
constexpr std::uint8_t DarkLordClass = 4;
using Math::Tenth;                                                       // 0x6b87b8, 0,1f
constexpr float FirstLightningScale = std::bit_cast<float>(0x3e99999au); // 0x6b89dc, 0,3f
constexpr float ThirdLightningScale = 2.5f;                              // 0x6b89a8
constexpr glm::vec3 FirstLightningLight{Tenth, Tenth, 1.0f};
constexpr float FirstWindowStart = std::bit_cast<float>(0x3f99999au); // 0x6b8bb0, 1,2f
constexpr float FirstWindowEnd = std::bit_cast<float>(0x3fcccccdu);   // 0x6b8bfc, 1,6f
constexpr float SecondWindowStart = 7.0f;                             // 0x6b8878
constexpr float SecondWindowEnd = 8.0f;                               // 0x6b86c4
// 0x50f9cf (0x6b8818): por debajo, la party (0x50f9e0).
constexpr float PartyFrameLimit = 2.0f;

// 0x50fba4: las banderas de +0x78 con sprites y joints.
constexpr std::uint32_t FirstGlowFlag = 0x4;
constexpr std::uint32_t SecondGlowFlag = 0x2000000;
// 0x50fbe5..0x50fc34: la luz de cada mano, k = (rand() % 30 + 70) · 0,01f y (k; k · 0,3f; k · 0,2f).
constexpr int GlowSpan = 0x1e;
constexpr int GlowBase = 0x46;
constexpr double GlowStep = static_cast<double>(std::bit_cast<float>(0x3c23d70au));  // 0x6b88d4, 0,01f
constexpr double GlowGreen = static_cast<double>(std::bit_cast<float>(0x3e99999au)); // 0x6b89dc, 0,3f
constexpr double GlowBlue = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));  // 0x6b8854, 0,2f
constexpr std::uint16_t GlowSprite = 0x5fd;
constexpr float GlowScale = 1.5f; // 0x3fc00000
// 0x50fcf7..0x50fd5c: los cinco 0x10f/4 de ancho 20 (0x41a00000), si no hay un 0x10f/4 ni /9 suyo (0x534fd0).
constexpr std::int32_t AuraType = 0x10f;
constexpr std::int32_t BindingSubtype = 4;
constexpr std::int32_t RiderBindingSubtype = 9;
constexpr float BindingWidth = 20.0f;
constexpr int BindingRings = 5;

// 0x50fd74: +0x358, cada 100 dibujos con la acción por debajo de 0xd, el efecto 0x5ac/3 con la luz (0,2f; 0,7f;
// 0,9f).
constexpr std::uint8_t LastCounterAction = 0xc;
constexpr std::int32_t CounterPeriod = 100;
constexpr glm::vec3 CounterFlareLight{std::bit_cast<float>(0x3e4ccccdu), std::bit_cast<float>(0x3f333333u),
                                      std::bit_cast<float>(0x3f666666u)};

// 0x50fddb: el conjunto completo.
constexpr float HandFlareScale = std::bit_cast<float>(0x3fa66666u); // 1,3
constexpr float FullSetShineFactor = 0.5f;
constexpr std::array<std::int32_t, 3> HandBoneDrops{0, 6, 7};
constexpr std::int16_t FirstGlowingBoots = 0x182f;
constexpr std::int16_t LastGlowingBoots = 0x1833;
constexpr std::int32_t LastQuietLevel = 9;
constexpr int GlowChance = 20;

// 0x50ff37..0x5102a4: la partícula 0x6f1/3 de las botas, en el hueso 0, a (0, ∓18, 50), (0, 0, 70) y (0, ±18, 50); con
// +13, ±20.
constexpr std::int32_t BootsParticle = 0x6f1;
constexpr std::int32_t BootsSubtype = 3;
constexpr std::int32_t BootsBone = 0;
constexpr float BootsSide = 18.0f, WideBootsSide = 20.0f, BootsLow = 50.0f, BootsHigh = 70.0f;

// 0x5102c3..0x5104cb: la órbita 0x60f/0 de escala 0,19f y los joints del conjunto, con el personaje de dueño.
constexpr std::int32_t SparkleType = 0x60f;
constexpr std::int32_t SparkleSubtype = 0, WideSparkleSubtype = 0x12;
constexpr float SparkleOrbitScale = std::bit_cast<float>(0x3e428f5cu); // 0,19f
constexpr float SparkleWidth = 10.0f, WideSparkleWidth = 20.0f;
constexpr std::int32_t TrailType = 0x610, TrailSubtype = 7, TrailKey = 0x28;
constexpr std::int32_t NoKey = -1, NoTarget = -1;
constexpr std::int32_t FirstSkill = 0, SecondSkill = 1;

// 0x52a9ad: el 5ac/3 cada 15 pasos, a 50 del dueño y 20 más arriba, con el joint 619/9 de ancho 10.
constexpr std::int32_t FlarePeriod = 15;
constexpr float FlareRadius = 50.0f;
constexpr std::int32_t EnergyType = 0x619, EnergySubtype = 9;
constexpr float EnergyWidth = 10.0f;

// 0x52a7b7 y 0x52a8a7: los 5ac/1 y /2 de las skills que llegan viven 10 pasos más mientras el dueño esté vivo con su
// bandera de +0x78; con 0x4000 en el dueño no hacen nada. El /1 suelta dos 5ac/4 de la luz (1; 0,5f; 0,1f) con las
// índices de hueso de 0x6c8c38 + rand() % 7 y 0x6c8c3e − ese; el /2, cada 50 de +0xd8, gira 50, se pone a 80 del dueño
// y 50 más arriba, y suelta el joint 619/9 de ancho 15.
constexpr std::int32_t SkillFlareLife = 10;
constexpr std::uint32_t SwellFlag = 0x10, GuardFlag = 0x400, QuietOwnerFlag = 0x4000;
constexpr std::array<float, 7> SwellBones{25, 26, 27, 20, 34, 35, 36};
constexpr std::int32_t GuardPeriod = 50;
constexpr double GuardTurn = 50.0;   // 0x6b86b0
constexpr float GuardRadius = 80.0f; // 0x6b9ffc
constexpr double GuardRise = 50.0;   // 0x6b86b0
constexpr float GuardWidth = 15.0f;  // 0x41700000

// 0x518900: los tres joints 619 de ancho 5 que suelta el calco 0x61e/1 de la mascota en cada paso, 200 más allá
// ([ebp−0x20]) y 120 más arriba (0x6b86c8), con el subtipo del calco. El subtipo 5 pasaría como 0xb, pero MuRet sólo
// crea el /1 y el /3.
constexpr int SummonJoints = 3;
constexpr float SummonWidth = 5.0f;
constexpr float SummonReach = 200.0f;
constexpr double SummonLift = 120.0;
constexpr int SummonPitchSpan = 90, SummonTurnSpan = 360;

// 0x50ff5e..0x510002: la luz de cada bota de 0x182f a 0x1833.
constexpr std::array<glm::vec3, 5> BootsLights{{
    {std::bit_cast<float>(0x3f266666u), std::bit_cast<float>(0x3e99999au), std::bit_cast<float>(0x3dcccccdu)},
    {std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3f666666u)},
    {0.0f, std::bit_cast<float>(0x3ea3d70au), std::bit_cast<float>(0x3e75c28fu)},
    {0.5f, std::bit_cast<float>(0x3e75c28fu), std::bit_cast<float>(0x3f4ccccdu)},
    {std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3ecccccdu), 0.0f},
}};

// 0x510a7a: los mapas sin 0x510ef0 (Blood Castle, Chaos Castle y el asedio).
constexpr std::int32_t FirstBloodCastle = 11;
constexpr std::int32_t LastBloodCastle = 17;
constexpr std::int32_t CastleMap = Maps::CastleSiegeMap;

// 0x5ffdf0: por debajo de 0,01f (0x6b88d4) no posa ni dibuja.
constexpr float DrawnAlpha = std::bit_cast<float>(0x3c23d70au);
// 0x420e11: con +0x78 & 0x4000 el dueño le pasa 10 a 0x4182f0, que 0x6002e0 lleva por 0x60033c.
constexpr std::uint32_t RiderFlag = 0x4000;
// 0x418951: 0x418920 sólo hace algo en los estados 5 y 6.
constexpr std::uint8_t FirstOrderState = 5;
constexpr std::uint8_t SecondOrderState = 6;
// 0x418aae: las juntas del ataque, con +0x38d hasta 2, de rand() % 40 + 20; 0x418b85: el efecto 0x113, con +0x38d
// impar desde 4 y el cuervo 100 (0x6b86b4) más arriba que el dueño.
constexpr std::uint8_t LastJointStep = 2;
constexpr int JointSpan = 40;
constexpr int JointBase = 20;
constexpr std::uint8_t LastQuietStep = 3;
constexpr double AboveTheOwner = 100.0;
// 0x41896d: el grito, con +0x38d hasta 14, una partícula 0x5ac por hueso de a dos desde rand() & 1, de los 66 primeros;
// con 15, 0x4ee770 sobre el personaje de +0x3b0.
constexpr std::uint8_t LastShoutStep = 14;
constexpr std::uint8_t ShoutEnd = 15;
constexpr std::size_t ShoutBones = 66;
constexpr std::uint16_t NoAttackTarget = 0xffff;

using Math::D;
using Math::F32;

// El estado de la cola de 0x50ba30 en un dibujo.
struct Tail
{
    ActorState& actor;
    const GameCharacterEffectsPass& pass;
    // [ebp−0x50]: el corrimiento de 0x4ec110, en 0 desde 0x50ea23. Lo escriben la piel del mago y las
    // partículas de las botas; lo leen las manos de las acciones, de las banderas y del conjunto (Q-C350).
    glm::vec3 offset{};

    // +0x6c..+0x74 del 0x20a (GameCharacterEffectsPass::modelOrigin): la posición de antes del suelo de la sombra.
    [[nodiscard]] glm::vec3 ModelOrigin() const
    {
        return pass.modelOrigin.value_or(actor.position);
    }

    // 0x4ec110 con [ebp−0x14] (el 0x20a: la escala del personaje y ModelOrigin) sobre el hueso `bone` de su pose propia
    // (+0x120), en la llamada `call`.
    [[nodiscard]] Core::Expected<glm::vec3, std::string> BodyPoint(std::int32_t bone, std::string_view call) const
    {
        if (bone < 0 || static_cast<std::size_t>(bone) >= pass.body.size())
            return Core::Unexpected(std::format("{}: el hueso {} del cuerpo no existe", call, bone));
        return Render::Models::OriginalTransformPosition(
            Render::Models::OriginalMatrixOf(pass.body[static_cast<std::size_t>(bone)]), offset, actor.scale,
            ModelOrigin());
    }

    // 0x5491e0: el sprite aditivo; el dueño no se guarda.
    [[nodiscard]] Result Sprite(std::string_view call, std::uint16_t texture, const glm::vec3& point, float scale,
                                const glm::vec3& light, float rotation = 0.0f) const
    {
        if (pass.sprites == nullptr)
            return Core::Unexpected(std::format("{}: falta la tabla de sprites del frame", call));
        static_cast<void>(pass.sprites->Create(texture, point, scale, light, rotation, 0));
        return {};
    }

    // 0x530860(tipo, posición, posición, ángulos, subtipo, personaje, ancho, key, skill, 0, −1) en el personaje. Con
    // los joints llenos no crea ni saca rand().
    [[nodiscard]] Result Joint(std::string_view call, std::int32_t type, std::int32_t subtype, float width,
                               std::int32_t key, std::int32_t skill) const
    {
        if (pass.joints.empty())
            return Core::Unexpected(std::format("{}: faltan los joints del frame (0x530860)", call));
        const auto created = CreateFaunaJoint(
            pass.joints,
            {type, actor.position, actor.position, actor.angles, subtype, &actor, width, key, skill, 0, NoTarget},
            pass.random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("{}: {}", call, created.Error()));
        return {};
    }
};

// 0x50ea38..0x50eb08: la piel especial del mago, sin rand().
Result MageSkin(Tail& tail)
{
    const ActorState& actor = tail.actor;
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
        Render::Models::Detail::SineProductPC53(D(tail.pass.worldClock) * D(MageShinyClock), MageShinyFactor);
    if (!shine)
        return Core::Unexpected<std::string>("0x50eae7: el reloj del mundo está fuera del dominio de FSIN");
    return tail.Sprite("0x50eb00", Shiny, point.Value(), F32(*shine), glm::vec3(1.0f));
}

// 0x50f8e8..0x50f95c: −ftol y ftol del reloj por 0,1f.
float ClockRotation(float clock, bool negated)
{
    const std::int32_t whole = World::FtolLow(D(clock));
    const std::int32_t turned =
        negated ? static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(whole)) : whole; // NEG
    return F32(static_cast<double>(turned) * D(Tenth));
}

// Lo que sigue a las acciones: el resto de la cola (0x50fba4) o el salto de 0x50f9f8 a 0x510a7a.
enum class AfterTheActions : std::uint8_t
{
    Rest,
    GuildMark,
};

// 0x50f828..0x50fba1: las acciones 0x94, 0x51 y 0x5b. Las escriben los paquetes (CL9): lo que crea
// efectos corta con la condición del ASM.
Core::Expected<AfterTheActions, std::string> Actions(Tail& tail)
{
    using enum AfterTheActions;
    const ActorState& actor = tail.actor;
    const std::uint8_t action = actor.animation.action;
    if (action != LightningAction && action != FirstEffectAction && action != SecondEffectAction)
        return Rest;
    const float frame = actor.animation.frame;
    if ((actor.characterClass & 7u) != DarkLordClass && action == LightningAction)
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
    // 0x50f96c..0x50f9c6: con el frame en [1,2; 1,6) (un NaN no entra), 0x518cf0(0x621, la posición, los ángulos, la
    // luz, 2, el personaje, −1, 0, 0, 0, 0), el brillo que sigue su hueso 33, y el sonido 0x1cc (0x41ed10), se haya
    // creado o no. Sin rand().
    const bool original = tail.pass.originalFrame;
    if (original && !(frame < FirstWindowStart) && frame < FirstWindowEnd)
    {
        constexpr std::int16_t GlowEffect = 0x621;
        constexpr std::int32_t HeldSubtype = 2;   // 0x50f9a3
        constexpr std::int32_t GlowSound = 0x1cc; // 0x50f9bc
        if (tail.pass.world == nullptr)
            return Core::Unexpected(std::format("0x50f9b3: falta el mundo para el 0x621/2 de la acción {:#x}", action));
        const auto made = CreateHighModelEffect({.type = GlowEffect,
                                                 .position = actor.position,
                                                 .angles = actor.angles,
                                                 .light = actor.light,
                                                 .subtype = HeldSubtype,
                                                 .owner = &tail.actor,
                                                 .key = 0xffff},
                                                HighEffectContext{.world = *tail.pass.world,
                                                                  .random = tail.pass.random,
                                                                  .worldClock = tail.pass.worldClock});
        if (!made.HasValue())
            return Core::Unexpected(std::format("0x50f9b3: {}", made.Error()));
        tail.pass.world->Runtime().RequestSound(GlowSound);
    }
    // 0x50f9c9: por debajo de 2 (también con NaN, como el fcomp) y con party (0x78aba48 sobre 0), 0x59e680. Fuera de
    // la party salta a 0x510a7a, sin las banderas, el contador ni el conjunto, también entre frames. En la party, dos
    // joints 0x618 (/0xc y /0xd, de ancho 20, BeamJoints.cpp) por cada miembro a la vista (+0x1c en 0 o más) que no es
    // él, desde ese miembro 150 más arriba, con los ángulos del personaje y él de dueño (0x50fa1c..0x50faba).
    if (!(frame >= PartyFrameLimit) && !tail.pass.party.empty())
    {
        if (!IsInTheParty(tail.pass.party, actor))
            return GuildMark;
        for (const PartySlot& member : tail.pass.party)
        {
            if (!original || member.actorIndex < 0 || static_cast<std::size_t>(member.actorIndex) == tail.pass.slot)
                continue;
            const auto index = static_cast<std::size_t>(member.actorIndex);
            if (index >= tail.pass.actors.size())
                return Core::Unexpected(
                    std::format("0x50fa3b: el miembro {} de la party de la acción {:#x} está fuera de los personajes",
                                index, action));
            if (tail.pass.joints.empty())
                return Core::Unexpected(std::string("0x50fa74: faltan los joints del frame (0x530860)"));
            const glm::vec3& at = tail.pass.actors[index].position;
            glm::vec3 from{at.x, at.y, F32(D(at.z) + 150.0)}; // 0x6b87ac
            for (const std::int32_t subtype : {0xc, 0xd})
            {
                const World::FishEffectRequest request{.type = JointFamilies::BeamJoint,
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
                const auto created = CreateFaunaJoint(tail.pass.joints, request, tail.pass.random);
                if (!created.HasValue())
                    return Core::Unexpected(
                        std::format("{}: {}", subtype == 0xc ? "0x50fa74" : "0x50fa97", created.Error()));
            }
        }
    }
    // 0x50fac5..0x50fb9c: en [7, 8) (un NaN no entra), el punto de la mano (0x4ec110 con +0x2bd y el corrimiento de
    // [ebp−0x50]) y dos 0x518cf0(0x117, punto, ángulos, (0,8f; 0,5f; 1), 2, sin dueño, −1, 0, 0, 0, 0): el primero con
    // los ángulos (180, 45, 0) y el segundo con (0, 0, z del personaje). Sin rand() (el /2 no sortea su giro).
    if (original && !(frame < SecondWindowStart) && frame < SecondWindowEnd)
    {
        constexpr std::int16_t DarkLordSkill = 0x117;
        constexpr std::int32_t WideSubtype = 2;                                        // 0x50fb2a
        constexpr glm::vec3 SkillLight{std::bit_cast<float>(0x3f4ccccdu), 0.5f, 1.0f}; // 0x50fb3a..0x50fb48
        constexpr glm::vec3 FirstAngles{180.0f, 45.0f, 0.0f};                          // 0x50fb4f..0x50fb5d
        if (tail.pass.world == nullptr)
            return Core::Unexpected(
                std::format("0x50fb64: falta el mundo para los 0x117/2 de la acción {:#x}", action));
        const auto point = tail.BodyPoint(actor.hands[0].bone, "0x50fb16");
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        const std::array<std::pair<glm::vec3, std::string_view>, 2> skills{
            {{FirstAngles, "0x50fb64"}, {glm::vec3{0.0f, 0.0f, actor.angles.z}, "0x50fb9c"}}};
        for (const auto& [angles, call] : skills)
        {
            const auto made = CreateHighModelEffect({.type = DarkLordSkill,
                                                     .position = point.Value(),
                                                     .angles = angles,
                                                     .light = SkillLight,
                                                     .subtype = WideSubtype,
                                                     .owner = nullptr,
                                                     .key = 0xffff},
                                                    HighEffectContext{.world = *tail.pass.world,
                                                                      .random = tail.pass.random,
                                                                      .worldClock = tail.pass.worldClock});
            if (!made.HasValue())
                return Core::Unexpected(std::format("{}: {}", call, made.Error()));
        }
    }
    return Rest;
}

// 0x50fba4..0x50fd5e: con 0x4 o 0x2000000 en +0x78 y sin 0x4000, por cada mano (+0x2bd y +0x2dd) un rand() da la
// luz y tres sprites 0x5fd de escala 1,5 en los huesos mano, mano − 6 y mano − 7 (0x4ec110 con [ebp−0x50]); después,
// si no hay un joint 0x10f/4 ni /9 suyo (0x534fd0), 0x530860(0x10f, +0x10, +0x10, +0x1c, 4, o, 20, −1, 0, 0, el
// +0x3a4 con signo) cinco veces. Entre frames, sólo los sprites (con los rand() de la copia).
Result Flags(Tail& tail)
{
    ActorState& actor = tail.actor;
    const std::uint32_t flags = actor.flags78;
    const bool glows = (flags & FirstGlowFlag) != 0 || (flags & SecondGlowFlag) != 0;
    if (!glows || (flags & HiddenAttachmentFlag) != 0)
        return {};
    for (const ActorItemPart& hand : actor.hands)
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
    if (tail.pass.joints.empty())
        return Core::Unexpected(std::string("0x50fcff: faltan los joints del frame (0x534fd0)"));
    const bool bound =
        std::ranges::any_of(tail.pass.joints,
                            [&actor](const JointState& joint)
                            {
                                return joint.alive && joint.type == AuraType && joint.ownerContext == &actor &&
                                       (joint.subtype == BindingSubtype || joint.subtype == RiderBindingSubtype);
                            });
    if (bound)
        return {};
    for (int ring = 0; ring < BindingRings; ++ring)
    {
        const World::FishEffectRequest request{.type = AuraType,
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
        if (auto created = CreateFaunaJoint(tail.pass.joints, request, tail.pass.random, JointOwnerKind::Actor);
            !created.HasValue())
            return Core::Unexpected(std::format("0x50fd4d: {}", created.Error()));
    }
    return {};
}

// 0x50fd74..0x50fdd5: con +0x358, el contador de +0x35c y, con la acción por debajo de 0xd y 100 o más, el
// efecto 0x5ac/3 en el personaje, con él de dueño, y el contador en 0. Con la tabla llena no crea.
Result Counter(const Tail& tail)
{
    ActorState& actor = tail.actor;
    if (actor.byte358 == 0)
        return {};
    if (actor.animation.action <= LastCounterAction && actor.counter35c >= CounterPeriod)
    {
        if (tail.pass.originalFrame)
        {
            if (tail.pass.effects == nullptr)
                return Core::Unexpected<std::string>("0x50fdc3: falta la tabla de efectos con modelo del frame");
            static_cast<void>(
                tail.pass.effects->CreateFullSetFlare(actor.position, actor.angles, CounterFlareLight, &actor));
        }
        actor.counter35c = 0;
    }
    ++actor.counter35c;
    return {};
}

// 0x608e00(botas, alpha, 0,5, color, 0): k = alpha · 0,5 en la pila y cada componente k por la del modelo,
// con un fstp; las que el caso pone en 0, un 0 literal.
glm::vec3 FullSetColor(std::uint16_t boots, float alpha)
{
    const Render::Color shine = Render::Models::ShineColorOf(boots);
    const double k = D(alpha) * D(FullSetShineFactor);
    const auto times = [k](float component) { return component == 0.0f ? 0.0f : F32(k * D(component)); };
    return {times(shine.red), times(shine.green), times(shine.blue)};
}

// La luz propia (+0xf4) cambiada hasta el final del bloque (0x5102ac y 0x5104b0).
class SavedLight
{
public:
    SavedLight(ActorState& actor, const glm::vec3& light) : m_actor(actor), m_saved(actor.light)
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
    ActorState& m_actor;
    glm::vec3 m_saved;
};

// 0x53fe70(0x6f1, punto, ángulos, luz propia, 3, 1, 0) en el hueso 0 con el corrimiento `offset`, que queda en
// [ebp−0x50] (Q-C350). Con las partículas llenas no crea ni saca rand().
Result BootsMist(Tail& tail, const glm::vec3& offset, std::string_view call)
{
    tail.offset = offset;
    const auto point = tail.BodyPoint(BootsBone, call);
    if (!point.HasValue())
        return Core::Unexpected(point.Error());
    if (tail.pass.particles == nullptr)
        return Core::Unexpected(std::format("{}: falta el pool de partículas del frame", call));
    ActorState& actor = tail.actor;
    const auto created = tail.pass.particles->CreateKalimaChecked(
        BootsParticle, point.Value(), actor.angles, actor.light, BootsSubtype, 1.0f, tail.pass.worldClock,
        tail.pass.random, static_cast<std::int32_t>(SceneId::Game));
    if (!created.HasValue())
        return Core::Unexpected(std::format("{}: la partícula {:#x}/{} falla con el error {}", call, BootsParticle,
                                            BootsSubtype, static_cast<unsigned>(created.Error())));
    return {};
}

// 0x50ff37..0x5102bd: las botas de 0x182f a 0x1833 con su luz en +0xf4 (guardada y devuelta). Con +10, rand() & 3 en 0;
// con +11, rand() % 3 en 0; con +12, rand() & 1 en 0; con +13, siempre: tres partículas 0x6f1/3 en el hueso 0. Entre
// frames, sólo el rand() que elige.
Result GlowingBoots(Tail& tail, std::int16_t boots, std::int32_t level)
{
    ActorState& actor = tail.actor;
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

// 0x53fe70(0x60f, posición, ángulos, luz propia, 0, 0,19f, personaje): la órbita del conjunto.
Result SparkleOrbit(const Tail& tail, std::string_view call)
{
    if (tail.pass.particles == nullptr)
        return Core::Unexpected(std::format("{}: falta el pool de partículas del frame", call));
    ActorState& actor = tail.actor;
    World::PortableParticleTrigonometry trigonometry;
    const auto created = tail.pass.particles->CreateChecked(
        SparkleType, actor.position, actor.angles, actor.light, SparkleSubtype, SparkleOrbitScale,
        std::cref(tail.pass.worldClock), tail.pass.random, 0, trigonometry,
        World::ParticleOwner{World::ParticleOwnerKind::Actor, tail.pass.slot});
    if (!created.HasValue())
        return Core::Unexpected(std::format("{}: la partícula {:#x}/{} falla con el error {}", call, SparkleType,
                                            SparkleSubtype, static_cast<unsigned>(created.Error())));
    return {};
}

// Los dos 60f/0x12 de ancho 20, con el skill 0 y el 1 (0x5103be y 0x5103db, 0x510435 y 0x510452).
Result WideSparkles(const Tail& tail, std::string_view first, std::string_view second)
{
    if (auto made = tail.Joint(first, SparkleType, WideSparkleSubtype, WideSparkleWidth, NoKey, FirstSkill);
        !made.HasValue())
        return made;
    return tail.Joint(second, SparkleType, WideSparkleSubtype, WideSparkleWidth, NoKey, SecondSkill);
}

// 0x5102c3..0x5104cb: con el nivel por encima de 9 y rand() % 20 en 0, la luz propia en 1 (guardada y devuelta). Con
// +10, la órbita; con +11, rand() & 7 elige la órbita o el 60f/0 de ancho 10; con +12, cada 6 pasos de lógica
// los dos 60f/0x12 y si no cada 3 la órbita; con +13, cada 6 los dos 60f/0x12 y, aparte, cada 4 la órbita y el
// 610/7 (Q-C381). Entre frames, sólo los rand() que eligen. El original lee 0x7d3a0f0 sin abrir el acceso, con
// la basura cifrada del último cierre (Q-C125); como el grito del cuervo, MuRet usa los pasos (P-C22 a).
Result Sparkles(const Tail& tail, std::int32_t level)
{
    if (tail.pass.random.Next() % GlowChance != 0)
        return {};
    ActorState& actor = tail.actor;
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

// 0x50fddb..0x5104cb: el conjunto completo de 0x505820.
Result FullSet(Tail& tail)
{
    const ActorState& actor = tail.actor;
    const std::uint16_t bootsModel = actor.body.back().model;
    const glm::vec3 color = FullSetColor(bootsModel, actor.alpha);
    if ((actor.flags78 & HiddenAttachmentFlag) == 0)
    {
        for (const ActorItemPart& hand : actor.hands)
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

// 0x510a7a..0x510ab6 y 0x510ef0: en la escena 5 (el único caller), con +0x88 en 1 y fuera de los mapas 11 a 23 y
// 30, +0x369 en 1 crea el efecto 0x140 (Scenes/GuildMarkEffects) si +0x36c está en 0 o pasaron más de 5000 ms, y
// +0x36c pasa a ser el reloj (Q-C352). Solo en un frame del original: entre frames, el personaje es una copia.
Result GuildMark(const Tail& tail)
{
    ActorState& actor = tail.actor;
    const std::int32_t map = tail.pass.map;
    // 0x510a7a: sólo en la escena 5.
    if (!tail.pass.gameScene || actor.byte88 != 1 || (map >= FirstBloodCastle && map <= LastBloodCastle) ||
        Maps::IsChaosCastle(map) || map == CastleMap)
        return {};
    if (!tail.pass.originalFrame || !GuildMarkIsDue(actor, tail.pass.worldClock))
        return {};
    if (tail.pass.effects == nullptr || tail.pass.particles == nullptr)
        return Core::Unexpected<std::string>("0x510f4d: el efecto 0x140 pide la tabla de efectos y las partículas");
    // 0x51e121: 0x4ec110 con el hueso 20 de los huesos propios (+0x120) y la escala y la posición que 0x6001b0 le copió
    // al modelo (antes del suelo de la sombra), sin el corrimiento de [ebp−0x50] (el de 0x518cf0 es suyo, en 0).
    if (GuildMarkBone >= tail.pass.body.size())
        return Core::Unexpected(std::format("0x51e121: el hueso {} del cuerpo no existe", GuildMarkBone));
    const glm::vec3 point =
        Render::Models::OriginalTransformPosition(Render::Models::OriginalMatrixOf(tail.pass.body[GuildMarkBone]),
                                                  glm::vec3(0.0f), actor.scale, tail.ModelOrigin());
    if (auto raised = RaiseTheGuildMark(actor, point, *tail.pass.effects, *tail.pass.particles, tail.pass.random,
                                        tail.pass.worldClock);
        !raised.HasValue())
        return Core::Unexpected(std::format("0x510f4d: {}", raised.Error()));
    return {};
}
Core::Expected<void, std::string> RaiseSwellLifeChildren(const World::ModelEffect& effect, World::Particles* particles,
                                                         World::ParticleOwner owner, Crt::CrtRandom& random)
{
    // 0x52a82b..0x52a89a: el selector corre aun con la tabla de partículas llena.
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

Core::Expected<void, std::string> DrawGameCharacterEffects(ActorState& actor, const GameCharacterEffectsPass& pass)
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
    // 0x50fd61: Chaos Castle salta al final, sin el contador ni el conjunto.
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

Core::Expected<void, std::string> StepFullSetFlare(World::ModelEffect& effect, std::span<JointState> joints,
                                                   Crt::CrtRandom& random)
{
    if (!effect.alive)
        return {};
    if (effect.type != World::IcarusFlareEffect || effect.subtype != World::FullSetFlareSubtype)
        return Core::Unexpected(
            std::format("0x52a9ad: el efecto {:#x}/{} no es el 0x5ac/3", effect.type, effect.subtype));
    // 0x5208c5: el primer rand() corre para todo efecto vivo, aunque el 5ac/3 no use la luz.
    static_cast<void>(random.Next());
    constexpr double Step = 1.0;                        // 0x6b87d4
    constexpr double HalfTurn = Math::HalfTurnAsDouble; // 0x6b86bc
    constexpr double Rise = 20.0;                       // 0x6b86d0
    effect.speed = F32(D(effect.speed) + Step);
    if (effect.life % FlarePeriod == 0 && effect.ownerContext != nullptr)
    {
        const auto& owner = *static_cast<const ActorState*>(effect.ownerContext);
        glm::vec3& angles = effect.transform.angles;
        glm::vec3& position = effect.transform.position;
        // 0x52a9fe: fst; el seno sale de la suma sin redondear y el coseno vuelve a cargar el float (Q-C380).
        const double turned = D(angles.z) + HalfTurn;
        angles.z = F32(turned);
        const auto sine = Render::Models::Detail::SineProductPC53(turned * D(Tenth), FlareRadius);
        const auto cosine = Render::Models::Detail::CosineProductPC53(D(angles.z) * D(Tenth), FlareRadius);
        if (!sine || !cosine)
            return Core::Unexpected<std::string>("0x52aa0d: el giro del 0x5ac/3 excede el perfil matemático");
        position.x = F32(*sine + D(owner.position.x));
        position.y = F32(*cosine + D(owner.position.y));
        position.z = F32(D(owner.position.z) + Rise);
        if (joints.empty())
            return Core::Unexpected<std::string>("0x52aa5d: faltan los joints del paso (0x530860)");
        const auto created = CreateFaunaJoint(joints,
                                              {EnergyType, position, position, angles, EnergySubtype,
                                               effect.ownerContext, EnergyWidth, NoKey, 0, 0, NoTarget},
                                              random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x52aa5d: {}", created.Error()));
    }
    // 0x52cb98: el 0x5ac no avanza animación; la velocidad (0) girada y una vida menos.
    World::StepModelEffect(effect, nullptr);
    return {};
}

Core::Expected<void, std::string> StepSkillFlare(World::ModelEffect& effect, std::span<JointState> joints,
                                                 Crt::CrtRandom& random, World::Particles* particles,
                                                 World::ParticleOwner particleOwner)
{
    if (!effect.alive)
        return {};
    if (effect.type != World::IcarusFlareEffect ||
        (effect.subtype != World::SwellFlareSubtype && effect.subtype != World::GuardFlareSubtype))
        return Core::Unexpected(
            std::format("0x52a7b7: el efecto {:#x}/{} no es el 0x5ac/1 ni el /2", effect.type, effect.subtype));
    // 0x5208c5: el primer rand() corre para todo efecto vivo, aunque el 5ac/1 y el /2 no usen la luz.
    static_cast<void>(random.Next());
    const bool swell = effect.subtype == World::SwellFlareSubtype;
    // 0x52a7e6 y 0x52a8dc: +0x78 del dueño se lee aunque +0x108 esté en 0; MuRet los crea siempre con el blanco.
    if (effect.ownerContext == nullptr)
    {
        effect.life = 0; // 0x52a7e3/0x52a8d9, antes de leer +0x78 del dueño nulo.
        return Core::Unexpected(
            std::format("{}: el 0x5ac/{} sin dueño lee +0x78 de 0", swell ? "0x52a7ec" : "0x52a8e2", effect.subtype));
    }
    const auto& owner = *static_cast<const ActorState*>(effect.ownerContext);
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
            // 0x52a8f7..0x52a91e: fst deja la suma en la pila para el ftol (0x69fd3c).
            const double counted = D(effect.speed) + Step;
            effect.speed = F32(counted);
            if (World::FtolLow(counted) % GuardPeriod == 0)
            {
                glm::vec3& angles = effect.transform.angles;
                glm::vec3& position = effect.transform.position;
                // 0x52a92a: fst; el seno sale de la suma sin redondear y el coseno vuelve a cargar el float (Q-C380).
                const double turned = D(angles.z) + GuardTurn;
                angles.z = F32(turned);
                const auto sine = Render::Models::Detail::SineProductPC53(turned * D(Tenth), GuardRadius);
                const auto cosine = Render::Models::Detail::CosineProductPC53(D(angles.z) * D(Tenth), GuardRadius);
                if (!sine || !cosine)
                    return Core::Unexpected<std::string>("0x52a950: el giro del 0x5ac/2 excede el perfil matemático");
                position.x = F32(*sine + D(owner.position.x));
                position.y = F32(*cosine + D(owner.position.y));
                position.z = F32(D(owner.position.z) + GuardRise);
                if (joints.empty())
                    return Core::Unexpected<std::string>("0x52a9a0: faltan los joints del paso (0x530860)");
                const auto created = CreateFaunaJoint(joints,
                                                      {EnergyType, position, position, angles, EnergySubtype,
                                                       effect.ownerContext, GuardWidth, NoKey, 0, 0, NoTarget},
                                                      random);
                if (!created.HasValue())
                    return Core::Unexpected(std::format("0x52a9a0: {}", created.Error()));
            }
        }
    }
    // 0x52cb98: el 0x5ac no avanza animación; la velocidad (0) girada y una vida menos.
    World::StepModelEffect(effect, nullptr);
    return {};
}

bool StepsAsPetSummon(const World::ModelEffect& effect)
{
    // 0x52b984..0x52b9a5: los /4 y /6 a /0xa saltan a 0x52b9b8; los demás de 1 en adelante, a 0x518900.
    constexpr std::int32_t PulsingSubtype = 4, FirstBindingSubtype = 6, LastOwnSubtype = 0xa;
    return effect.type == World::ColoredDecalEffect && effect.subtype != PulsingSubtype &&
           (effect.subtype < FirstBindingSubtype || effect.subtype > LastOwnSubtype);
}

Core::Expected<void, std::string> StepPetSummon(World::ModelEffect& effect, std::span<JointState> joints,
                                                Crt::CrtRandom& random)
{
    if (!effect.alive)
        return {};
    if (!StepsAsPetSummon(effect))
        return Core::Unexpected(
            std::format("0x52b9a6: el efecto {:#x}/{} no va a 0x518900", effect.type, effect.subtype));
    // 0x52b984 → 0x52b9b8 → 0x52bb29: el /0 (y un subtipo negativo) no entra en 0x518900, ni consulta joints o dueño.
    if (effect.subtype < World::PetSummonSubtype)
    {
        static_cast<void>(random.Next());
        World::StepModelEffect(effect, nullptr);
        return {};
    }
    if (joints.empty())
        return Core::Unexpected<std::string>("0x5189cc: faltan los joints del paso (0x530860)");
    // 0x5208c5: el primer rand() corre para todo efecto vivo, aunque el 0x61e no use la luz.
    static_cast<void>(random.Next());
    glm::vec3& position = effect.transform.position;
    // 0x51898b..0x5189bd: el /5 pide el 619/0xb; los demás, el de su subtipo.
    constexpr std::int32_t SwappedSubtype = 5, SwappedJointSubtype = 0xb;
    const std::int32_t jointSubtype = effect.subtype == SwappedSubtype ? SwappedJointSubtype : effect.subtype;
    for (int made = 0; made < SummonJoints; ++made)
    {
        glm::vec3 angles{static_cast<float>(random.Next() % SummonPitchSpan), 0.0f,
                         static_cast<float>(random.Next() % SummonTurnSpan)};
        const glm::vec3 reach = Render::Models::OriginalVectorRotate(glm::vec3(0.0f, -SummonReach, 0.0f),
                                                                     Render::Models::OriginalAngleMatrix(angles));
        // 0x51897a..0x5189b4: cada resta se guarda; la z suma 120 antes de guardarse.
        glm::vec3 start{F32(D(position.x) - D(reach.x)), F32(D(position.y) - D(reach.y)),
                        F32(D(position.z) - D(reach.z) + SummonLift)};
        const auto created = CreateFaunaJoint(
            joints,
            {EnergyType, start, position, angles, jointSubtype, effect.ownerContext, SummonWidth, NoKey, 0, 0,
             NoTarget},
            random, effect.owner.effect != nullptr ? JointOwnerKind::ModelEffect : JointOwnerKind::ByType);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x5189cc: {}", created.Error()));
    }
    // 0x52cb98: el 0x61e no es un BMD y no avanza animación; la velocidad (0) girada y una vida menos.
    World::StepModelEffect(effect, nullptr);
    return {};
}

// 0x418920(CHARACTER, CHARACTER), desde 0x41835a después de 0x602e90, con el cuervo en el trapecio: en el 5 y el 6, los
// efectos del ataque. En el 5, con +0x38d hasta 2, diez joints 0x5ac/1 desde el cuervo y, con 1, el 0x117/3; impar
// desde 4 y alto, el 0x113/0 del pico, en las tablas de `world`. El grito del 6 suelta las partículas 0x5ac/6
// (0x418a26). Sin la pose (+0x120 en 0 hasta el primer dibujo), el 6 no hace nada. El final del grito (+0x38d en 15)
// es EndTheShout.
Core::Expected<void, std::string> DrawTheRavenAttack(DarkSpirit& spirit, const ActorState& owner,
                                                     const Render::Models::Skeleton* skeleton, Crt::CrtRandom& random,
                                                     World::Particles* particles, float worldClock, WorldState* world)
{
    if (spirit.state == FirstOrderState)
    {
        if (spirit.byte38d <= LastJointStep)
        {
            // 0x418aab..0x418b08: con el blanco de +8, que el A8 puso antes del estado 5, diez 0x530860(0x5ac, la
            // posición del cuervo dos veces, sus ángulos, 1, sin dueño, rand() % 40 + 20, −1, 0, 0, −1).
            constexpr int AttackJoints = 10;               // 0x418ac9
            constexpr std::int32_t AttackJoint = 0x5ac;    // 0x418afa
            constexpr std::int32_t AttackJointSubtype = 1; // 0x418af5
            for (int made = 0; made < AttackJoints; ++made)
            {
                const auto width = static_cast<float>(static_cast<std::int32_t>(random.Next() % JointSpan) + JointBase);
                // Sin la tabla, corta en la primera 0x530860, después del rand() de 0x418ad6.
                if (world == nullptr)
                    return Core::Unexpected(std::string("0x418aff: faltan los joints del mundo (0x530860) para el "
                                                        "ataque del cuervo"));
                const World::FishEffectRequest request{.type = AttackJoint,
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
                const auto created = CreateFaunaJoint(world->DrawTail().joints, request, random);
                if (!created.HasValue())
                    return Core::Unexpected(std::format("0x418aff: {}", created.Error()));
            }
            // 0x418b0a..0x418b71: con +0x38d en 1, 0x518cf0(0x117, la posición, (45, rand() % 180 − 90, 0),
            // (1; 0,8f; 0,6f), 3, sin dueño, −1, 0, 0, 0, 0).
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
                const auto made = CreateHighModelEffect(
                    {.type = DarkLordSkill,
                     .position = spirit.position,
                     .angles = angles,
                     .light = SkillLight,
                     .subtype = SkillSubtype,
                     .owner = nullptr,
                     .key = 0xffff},
                    HighEffectContext{.world = *world, .random = random, .worldClock = worldClock});
                if (!made.HasValue())
                    return Core::Unexpected(std::format("0x418b71: {}", made.Error()));
            }
        }
        // 0x418b7c..0x418c0a: impar desde 4 y 100 más arriba que el dueño, 0x4ec110(+0x120 + 6 · 0x30, (50, 0, 0),
        // punto, 0), el hueso 6 en el mundo, y 0x518cf0(0x113, punto, +0x1c, +0xf4, 0, el cuervo, −1, 0, 0, 0, 0). No
        // saca rand(). 0x418ba6..0x418bb7: FCOMP y TEST AH, 1 siguen con C0, que también prende un NaN.
        if (spirit.byte38d > LastQuietStep && (spirit.byte38d & 1u) != 0 &&
            !(static_cast<double>(owner.position.z) + AboveTheOwner >= static_cast<double>(spirit.position.z)))
        {
            constexpr std::size_t BeakBone = 6;                // 0x418bc8: +0x120 más 0x120
            constexpr glm::vec3 BeakOffset{50.0f, 0.0f, 0.0f}; // 0x418bcf: 0x42480000
            constexpr std::int16_t AirForce = 0x113;
            if (BeakBone >= spirit.pose.size())
                return Core::Unexpected(
                    std::format("0x418be4: el hueso {} del cuervo pasa los de su pose (+0x120)", BeakBone));
            if (world == nullptr)
                return Core::Unexpected(std::string("0x418c0a: falta la tabla de efectos (0x518cf0) para el ataque "
                                                    "del cuervo"));
            const glm::vec3 point = Render::Models::OriginalVectorTransform(BeakOffset, spirit.pose[BeakBone]);
            const auto made =
                World::CreateBlastModelEffect(world->Map().effects,
                                              {.type = AirForce,
                                               .position = point,
                                               .angles = spirit.angles,
                                               .light = spirit.light,
                                               .subtype = 0,
                                               .owner = &spirit,
                                               .key = 0xffff},
                                              World::ModelEffectOwner{}, random, World::BlastEffectServices{});
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
        // 0x4189ad..0x418a46: rand() & 1 con signo (rand() no es negativo), y los huesos de a dos que no son dummy ni
        // pasan de los del modelo: 0x4ec110(+0x120 + hueso · 0x30, (0; 0; 0), punto, 0), el hueso en el mundo, y
        // 0x53fe70(0x5ac, punto, cuervo+0x1c, (1; 0,6f; 0,4f), 6, 1,3f, 0). Con el pool lleno, ninguna y sigue.
        constexpr glm::vec3 ShoutLight{1.0f, std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3ecccccdu)};
        constexpr std::int32_t ShoutParticle = 0x5ac, ShoutSubtype = 6;
        constexpr float ShoutScale = std::bit_cast<float>(0x3fa66666u); // 1,3f
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
            const glm::vec3 point = Render::Models::OriginalVectorTransform(glm::vec3(0.0f), spirit.pose[bone]);
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

// 0x418a51..0x418a9b: el final del grito (el estado 6 con +0x38d en 15). Con el personaje de +0x3b0 (MOVSX; 0xffff no),
// 0x4ee770(1, 0x5ac, 1, cuervo+0x1c, el personaje) con el modelo del cuervo (ECX, 0x5769ad8 + 0xc0 · 0xe0) sobre la
// malla 1 del scratch que dejó su 0x602e90 en este dibujo (Scenes/VertexEffects.h: los joints 0x613/0xd de los
// vértices hacia el personaje, con él de dueño) y 0x4164f0(0), el estado y el contador de lejanía en 0. Siempre,
// +0x38d en 0. `meshes` es el scratch del cuervo si se dibujó (`drawn`); si no, 0x4ee770 leería el del último modelo
// posado y corta.
Core::Expected<void, std::string> EndTheShout(DarkSpirit& spirit, std::span<const Render::Models::SkinnedMesh> meshes,
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
        if (const Data::Models::Model* model = pass.models.Find(DarkSpiritModel); model != nullptr)
        {
            const VertexEffectCall call{.mesh = ShoutMesh,
                                        .type = ShoutEffects,
                                        .subtype = ShoutJoints,
                                        .angles = spirit.angles,
                                        .target = &actors[static_cast<std::size_t>(slot)]};
            if (auto emitted = EmitVertexEffectsOf(*model, spirit.scale, meshes, call,
                                                   {.joints = pass.world->DrawTail().joints}, *pass.random);
                !emitted.HasValue())
                return Core::Unexpected(std::format("0x418a8c: {}", emitted.Error()));
        }
        // 0x418a91: 0x4164f0(0) sobre el cuervo.
        spirit.state = Released;
        spirit.far = 0;
    }
    spirit.byte38d = 0;
    return {};
}

Core::Expected<void, std::string> DrawGameRaven(ActorState& owner, const GameRavenPass& pass)
{
    if (!owner.darkSpirit)
        return {};
    DarkSpirit& spirit = *owner.darkSpirit;
    if (!DarkSpiritSeen(spirit, pass.polygon))
    {
        if (pass.originalFrame)
            MissTheView(spirit);
        return {};
    }
    // 0x5ffdff..0x5ffe3b: en los mapas 18 a 23, con +0x1a1 del héroe, el alpha baja 0,15f y queda guardado.
    if (pass.originalFrame)
        spirit.alpha = World::ChaosCastlePoseAlpha(pass.map, pass.heroByte1a1, spirit.alpha);
    // Si 0x602e90 posó al cuervo en este dibujo: su scratch (0x5786f9c) es el del buffer de la pose.
    bool ravenPosed = false;
    if (spirit.alpha >= DrawnAlpha)
    {
        if (pass.lighting == nullptr)
            return Core::Unexpected<std::string>(
                "0x5ffce0: falta el buffer dinámico de luz del terreno para el cuervo");
        // 0x420e00..0x420e11: con 0x4000 en +0x78 del dueño, 0x4182f0(10) y 0x602e90(cuervo, 0, 0, 10).
        const bool shell = (owner.flags78 & RiderFlag) != 0;
        if (!shell && (!pass.terrain.heights || !pass.terrain.attributes))
            return Core::Unexpected<std::string>("0x600a0b: faltan las alturas del terreno para la sombra del cuervo");
        const Render::Models::Skeleton* skeleton = pass.models.SkeletonOf(DarkSpiritModel);
        const glm::vec3 light = World::LightAt(*pass.lighting, spirit.position.x, spirit.position.y);
        // Los personajes del juego se dibujan como los dejó el paso: el cuervo también (`stepAlpha` en 0).
        DrawnObject drawn = DrawnDarkSpirit(spirit, skeleton, light, 0.0f);
        if (pass.carry != nullptr)
        {
            if (auto posed = PublishFaunaPose(*pass.carry, pass.models, drawn, true); !posed.HasValue())
                return posed;
            drawn.retainedMeshes = pass.carry->meshes;
        }
        // +0x120: la pose de 0x5ffdf0, antes del camino de 0x6002e0 que sea.
        if (pass.originalFrame)
            spirit.pose = PosedWorldBones(pass.models, drawn);
        ravenPosed = true;
        if (shell)
        {
            // 0x60033c: el +0x108 del cuervo lo deja en 0 el rep stos de 0x415c1e y nadie lo escribe, así que el lazo
            // del asedio (0x600343..0x600399) no lo encuentra en el pool: siempre DrawChromeShell, sin la sombra ni la
            // altura del terreno, y el modelo compartido queda en su lugar (0x5ffdf0), no en el piso.
            if (auto drawnShell = DrawChromeShell(pass.batch, pass.models, drawn, pass.frameClock);
                !drawnShell.HasValue())
                return drawnShell;
        }
        else
        {
            const float ground =
                F32(World::TerrainHeightAt(*pass.terrain.heights, *pass.terrain.attributes, drawn.transform.position.x,
                                           drawn.transform.position.y, pass.fixedHeight));
            DrawDarkSpirit(pass.batch, pass.models, drawn, ground, pass.frameClock, pass.map);
            if (pass.carry != nullptr)
                pass.carry->models[drawn.model].position = {drawn.transform.position.x, drawn.transform.position.y,
                                                            ground};
        }
    }
    // 0x41835a: 0x418920, en los frames del original.
    if (pass.originalFrame && (spirit.state == FirstOrderState || spirit.state == SecondOrderState))
    {
        if (pass.random == nullptr)
            return Core::Unexpected(std::string("0x41835a: falta el rand() para el ataque del cuervo"));
        if (spirit.state == SecondOrderState && spirit.byte38d == ShoutEnd)
        {
            std::span<const Render::Models::SkinnedMesh> scratch{};
            if (pass.carry != nullptr)
                scratch = pass.carry->meshes;
            return EndTheShout(spirit, scratch, ravenPosed, pass);
        }
        return DrawTheRavenAttack(spirit, owner, pass.models.SkeletonOf(DarkSpiritModel), *pass.random, pass.particles,
                                  pass.worldClock, pass.world);
    }
    return {};
}
} // namespace Client::Scenes
