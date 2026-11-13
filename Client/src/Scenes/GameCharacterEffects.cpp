#include "Scenes/GameCharacterEffects.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
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
#include "Scenes/SceneId.h"
#include "World/ChaosCastle.h"
#include "World/Ftol.h"
#include "World/ModelEffects.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "World/PortableParticleTrigonometry.h"
#include "World/Sprites.h"
#include "World/TerrainHeight.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

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
constexpr float Tenth = std::bit_cast<float>(0x3dcccccdu);               // 0x6b87b8, 0,1f
constexpr float FirstLightningScale = std::bit_cast<float>(0x3e99999au); // 0x6b89dc, 0,3f
constexpr float ThirdLightningScale = 2.5f;                              // 0x6b89a8
constexpr glm::vec3 FirstLightningLight{Tenth, Tenth, 1.0f};
constexpr float FirstWindowStart = std::bit_cast<float>(0x3f99999au); // 0x6b8bb0, 1,2f
constexpr float FirstWindowEnd = std::bit_cast<float>(0x3fcccccdu);   // 0x6b8bfc, 1,6f
constexpr float SecondWindowStart = 7.0f;                             // 0x6b8878
constexpr float SecondWindowEnd = 8.0f;                               // 0x6b86c4

// 0x50fba4: las banderas de +0x78 con sprites y joints.
constexpr std::uint32_t FirstGlowFlag = 0x4;
constexpr std::uint32_t SecondGlowFlag = 0x2000000;

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

// 0x518900: los tres joints 619 de ancho 5 que suelta el calco 0x61e/1 de la mascota en cada paso, 200 más allá
// ([ebp−0x20]) y 120 más arriba (0x6b86c8). El subtipo 5 pasaría como 0xb, pero sólo se crea el /1.
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
constexpr std::int32_t CastleMap = 30;

// 0x5ffdf0: por debajo de 0,01f (0x6b88d4) no posa ni dibuja.
constexpr float DrawnAlpha = std::bit_cast<float>(0x3c23d70au);
// 0x420e11: con +0x78 & 0x4000 el dueño le pasa 10 a 0x4182f0, que 0x6002e0 lleva por 0x60033c.
constexpr std::uint32_t RiderFlag = 0x4000;
// 0x418951: 0x418920 sólo hace algo en los estados 5 y 6.
constexpr std::uint8_t FirstOrderState = 5;
constexpr std::uint8_t SecondOrderState = 6;

float F32(double value)
{
    return static_cast<float>(value);
}

double D(float value)
{
    return static_cast<double>(value);
}

// El estado de la cola de 0x50ba30 en un dibujo.
struct Tail
{
    ActorState& actor;
    const GameCharacterEffectsPass& pass;
    // [ebp−0x50]: el corrimiento de 0x4ec110, en 0 desde 0x50ea23. Lo escriben la piel del mago y las
    // partículas de las botas; lo leen las manos de las acciones, de las banderas y del conjunto (Q-C350).
    glm::vec3 offset{};

    // 0x4ec110 con el jugador sobre el hueso `bone` de su pose propia (+0x120), en la llamada `call`.
    [[nodiscard]] Core::Expected<glm::vec3, std::string> BodyPoint(std::int32_t bone, std::string_view call) const
    {
        if (bone < 0 || static_cast<std::size_t>(bone) >= pass.body.size())
            return Core::Unexpected(std::format("{}: el hueso {} del cuerpo no existe", call, bone));
        return Render::Models::OriginalTransformPosition(
            Render::Models::OriginalMatrixOf(pass.body[static_cast<std::size_t>(bone)]), offset, actor.scale,
            actor.position);
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

// 0x50f828..0x50fba1: las acciones 0x94, 0x51 y 0x5b. Las escriben los paquetes (CL9): lo que crea
// efectos corta con la condición del ASM.
Result Actions(Tail& tail)
{
    const ActorState& actor = tail.actor;
    const std::uint8_t action = actor.animation.action;
    if (action != LightningAction && action != FirstEffectAction && action != SecondEffectAction)
        return {};
    const float frame = actor.animation.frame;
    if ((actor.characterClass & 7u) != DarkLordClass && action == LightningAction)
    {
        if ((actor.flags78 & HiddenAttachmentFlag) != 0)
            return {};
        const auto point = tail.BodyPoint(actor.hands[0].bone, "0x50f89b");
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        const float size = F32(D(frame) * D(Tenth));
        if (auto first = tail.Sprite("0x50f8e3", Lightning, point.Value(), F32(D(size) * D(FirstLightningScale)),
                                     FirstLightningLight);
            !first.HasValue())
            return first;
        if (auto second = tail.Sprite("0x50f91f", Lightning, point.Value(), size, actor.light,
                                      ClockRotation(tail.pass.worldClock, true));
            !second.HasValue())
            return second;
        return tail.Sprite("0x50f95c", Lightning, point.Value(), F32(D(size) * D(ThirdLightningScale)), actor.light,
                           ClockRotation(tail.pass.worldClock, false));
    }
    // 0x50f96c: con el frame en [1,2; 1,6), el efecto 0x621/2 y el sonido 0x1cc. Por debajo de 2, con la
    // cantidad del grupo (0x78aba48) por encima de 0, 0x59e680 y los joints 0x618: el grupo es de la CL9 y
    // en MuRet vale 0, como al arrancar. En [7, 8), dos 0x117/2 en la mano derecha.
    if (!tail.pass.originalFrame)
        return {};
    if (!(frame < FirstWindowStart) && frame < FirstWindowEnd)
        return Core::Unexpected(
            std::format("0x50f9b3: el efecto 0x621 y el sonido 0x1cc de la acción {:#x} no están portados", action));
    if (!(frame < SecondWindowStart) && frame < SecondWindowEnd)
        return Core::Unexpected(
            std::format("0x50fb64: los efectos 0x117 de la acción {:#x} no están portados", action));
    return {};
}

// 0x50fba4..0x50fd5e: los sprites de las manos con el color al azar y los cinco joints 0x10f/4. +0x78 sólo lo
// escriben los paquetes (CL9): corta con la condición del ASM.
Result Flags(const Tail& tail)
{
    const std::uint32_t flags = tail.actor.flags78;
    const bool glows = (flags & FirstGlowFlag) != 0 || (flags & SecondGlowFlag) != 0;
    if (glows && (flags & HiddenAttachmentFlag) == 0)
        return Core::Unexpected(std::format(
            "0x50fbd5: los sprites y los joints 0x10f de las banderas +0x78 ({:#x}) no están portados", flags));
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
    Render::Models::CrtRandom& random = tail.pass.random;
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
// 30, +0x369 en 1 crea el efecto 0x140 si +0x36c está en 0 o pasaron más de 5000 ms. +0x369 sólo lo escriben los
// paquetes (CL9) y +0x36c sólo este efecto: en MuRet, siempre 0 (Q-C352).
Result GuildMark(const Tail& tail)
{
    const ActorState& actor = tail.actor;
    const std::int32_t map = tail.pass.map;
    // 0x510a7a: sólo en la escena 5.
    if (!tail.pass.gameScene || actor.byte88 != 1 || (map >= FirstBloodCastle && map <= LastBloodCastle) ||
        World::IsChaosCastleMap(map) || map == CastleMap)
        return {};
    if (actor.byte369 == 1 && tail.pass.originalFrame)
        return Core::Unexpected<std::string>("0x510f4d: el efecto 0x140 de +0x369 no está portado");
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawGameCharacterEffects(ActorState& actor, const GameCharacterEffectsPass& pass)
{
    Tail tail{.actor = actor, .pass = pass};
    if (auto drawn = MageSkin(tail); !drawn.HasValue())
        return drawn;
    if (auto drawn = Actions(tail); !drawn.HasValue())
        return drawn;
    if (auto drawn = Flags(tail); !drawn.HasValue())
        return drawn;
    // 0x50fd61: Chaos Castle salta al final, sin el contador ni el conjunto.
    if (!World::IsChaosCastleMap(pass.map))
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
                                                   Render::Models::CrtRandom& random)
{
    if (!effect.alive)
        return {};
    if (effect.type != World::IcarusFlareEffect || effect.subtype != World::FullSetFlareSubtype)
        return Core::Unexpected(
            std::format("0x52a9ad: el efecto {:#x}/{} no es el 0x5ac/3", effect.type, effect.subtype));
    // 0x5208c5: el primer rand() corre para todo efecto vivo, aunque el 5ac/3 no use la luz.
    static_cast<void>(random.Next());
    constexpr double Step = 1.0;       // 0x6b87d4
    constexpr double HalfTurn = 180.0; // 0x6b86bc
    constexpr double Rise = 20.0;      // 0x6b86d0
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

Core::Expected<void, std::string> StepPetSummon(World::ModelEffect& effect, std::span<JointState> joints,
                                                Render::Models::CrtRandom& random)
{
    if (!effect.alive)
        return {};
    if (effect.type != World::ColoredDecalEffect || effect.subtype != World::PetSummonSubtype)
        return Core::Unexpected(
            std::format("0x52b9a6: el efecto {:#x}/{} no es el 0x61e/1", effect.type, effect.subtype));
    if (joints.empty())
        return Core::Unexpected<std::string>("0x5189cc: faltan los joints del paso (0x530860)");
    // 0x5208c5: el primer rand() corre para todo efecto vivo, aunque el 0x61e no use la luz.
    static_cast<void>(random.Next());
    glm::vec3& position = effect.transform.position;
    for (int made = 0; made < SummonJoints; ++made)
    {
        glm::vec3 angles{static_cast<float>(random.Next() % SummonPitchSpan), 0.0f,
                         static_cast<float>(random.Next() % SummonTurnSpan)};
        const glm::vec3 reach = Render::Models::OriginalVectorRotate(glm::vec3(0.0f, -SummonReach, 0.0f),
                                                                     Render::Models::OriginalAngleMatrix(angles));
        // 0x51897a..0x5189b4: cada resta se guarda; la z suma 120 antes de guardarse.
        glm::vec3 start{F32(D(position.x) - D(reach.x)), F32(D(position.y) - D(reach.y)),
                        F32(D(position.z) - D(reach.z) + SummonLift)};
        const auto created = CreateFaunaJoint(joints,
                                              {EnergyType, start, position, angles, effect.subtype, effect.ownerContext,
                                               SummonWidth, NoKey, 0, 0, NoTarget},
                                              random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x5189cc: {}", created.Error()));
    }
    // 0x52cb98: el 0x61e no es un BMD y no avanza animación; la velocidad (0) girada y una vida menos.
    World::StepModelEffect(effect, nullptr);
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
    // 0x41835a: 0x418920 con los estados 5 y 6 que pone el A8 (Scenes/PetOrders), sin portar: fase 2 (08, P-C266).
    if (spirit.state == FirstOrderState || spirit.state == SecondOrderState)
        return Core::Unexpected(
            std::format("0x418957: el estado {} del cuervo (el ataque del A8) no está portado", spirit.state));
    return {};
}
} // namespace Client::Scenes
