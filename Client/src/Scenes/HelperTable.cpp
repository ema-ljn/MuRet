#include "Scenes/HelperTable.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/GameRibbonCreation.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/HelperStepParts.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/PetHelperSteps.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ChaosCastle.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/TerrainLighting.h"
#include "World/WeaponSparkKernels.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
// 0x443faa..0x443fb3 y 0x5ffdff..0x5ffe07: los mapas 18 a 23.
constexpr std::int32_t FirstChaosCastleMap = 18;
constexpr std::int32_t LastChaosCastleMap = 23;
// 0x443fc0 y 0x4452c2: el ayudante que no pide un jugador de jinete.
constexpr std::uint16_t RiderlessHelper = GuardianAngelModel;
// 0x443e82: la escala del 0xbf al crearlo; 0x445309, la del dibujo en la escena 5.
constexpr float HorseScale = 1.0f;
constexpr float RidingHelperScale = 0.9f;
constexpr float GuardianScale = 0.7f;
// 0x4452d2: con el segundo argumento de 0x445260 distinto de 0 (el retrato de las cartas), 0x3f3ae148.
constexpr float PortraitHelperScale = std::bit_cast<float>(0x3f3ae148u);

bool IsFenrir(std::uint16_t type)
{
    return type >= FirstFenrirModel && type <= LastFenrirModel;
}

bool IsRidingHelper(std::uint16_t type)
{
    return type == UniriaModel || type == DinorantModel || IsFenrir(type);
}
// 0x443e4b y 0x443df7: la velocidad (+0xd8) y el alpha (+0x174) al crearlo.
constexpr float CreatedSpeed = 0.5f;
constexpr float CreatedAlpha = 0.0f;
// 0x4440f2: el EBX en 0 de 0x44401e.
constexpr float HiddenAlpha = 0.0f;
// 0x44403e: con el +0x88 del jinete distinto de 1, la escena 5 lo apaga.
constexpr std::uint8_t RiderShown = 1;
// Las acciones del jinete (+0x111) y del caballo que miran el polvo, el aliento y el efecto.
constexpr std::uint8_t GallopingRiderAction = 0x4b;
constexpr std::uint8_t DustHorseAction = 1;
constexpr std::uint8_t EffectHorseAction = 3;
// 0x444196: en el mapa 10 el polvo es otro; 0x4445a9: en el 7 el aliento es la gota 607.
constexpr std::int32_t IcarusMap = 10;
constexpr std::int32_t AtlansMap = 7;
// 0x444414..0x44441d y 0x44455a..0x444563: con 0x4000 en +0x78 del jinete, ni polvo ni aliento; 0x445310
// dibuja con 10. 0x5ffefc: con 0x1000, el frame del caballo en 0.
constexpr std::uint32_t RiderWithoutDust = 0x4000;
constexpr std::uint32_t RiderFrozen = 0x1000;
// 0x4443e3 y 0x44456b: rand() % 2 y rand() % 3 en 0.
constexpr int DustOneIn = 2;
constexpr int BreathOneIn = 3;
// 0x4445b5..0x4445c8: la luz del aliento, +0xf4, el 3 de 0x443d70 (nadie más lo escribe). Las partículas nacen con
// el subtipo 0.
constexpr glm::vec3 HorseLight{3.0f, 3.0f, 3.0f};
constexpr std::int32_t ParticleSubtype = 0;
// 0x444588..0x44459d: el aliento sale de (50; −4; 0) en el hueso 27 del buffer global (0x68b1d2c).
constexpr std::size_t BreathBone = 27;
constexpr glm::vec3 BreathOffset{50.0f, -4.0f, 0.0f};
// 0x444693: con 1 en +8 del jinete, los doce 0x60f. Lo pone en 1 sólo 0x421db0, en el héroe, con el A9 de las
// mascotas y su byte 4 en 0xfe (0x4220b1..0x4220c1), sin portar.
constexpr std::uint8_t RiderWithOrbits = 1;
// 0x5ffe30: el alpha con que 0x5ffdf0 todavía dibuja.
constexpr float DrawnAlpha = 0.01f;
// 0x443f1a: el blendMesh del 0x1c12; los demás, el −1 de 0x443d70.
constexpr std::int16_t GuardianBlendMesh = 1;
constexpr std::int16_t NoBlendMesh = -1;
// 0x445336..0x445390: rand() % 30 + 70 por 0,01f (0x6b88d4), y con el 0x1c12 el sprite 0x5ac (Effect\flare01.jpg) de
// escala 1, sin rotación y aditivo, con esa luz por (0,5; 0,8; 0,6) (0x6b882c, 0x6b8a38 y 0x6b8a20).
constexpr int GlowSpan = 30;
constexpr int GlowBase = 70;
constexpr double GlowUnit = static_cast<double>(std::bit_cast<float>(0x3c23d70au));
constexpr std::uint16_t GlowSprite = 0x5ac;
constexpr float GlowScale = 1.0f;
constexpr double GlowRed = static_cast<double>(std::bit_cast<float>(0x3f000000u));
constexpr double GlowGreen = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu));
constexpr double GlowBlue = static_cast<double>(std::bit_cast<float>(0x3f19999au));

float F32(double value)
{
    return static_cast<float>(value);
}

bool InChaosCastle(std::int32_t map)
{
    return map >= FirstChaosCastleMap && map <= LastChaosCastleMap;
}

// 0x4441cd..0x4442ad: el hueso del buffer global por el frame del caballo, ya en la acción 1. Las ventanas se pisan
// y gana la primera; fuera de todas (o sin orden), ninguno.
std::optional<std::size_t> IcarusDustBone(float frame)
{
    struct Window
    {
        float low;
        float high;
        bool closed; // 0x44429a mira C0 y C3: la última incluye su tope.
        std::size_t bone;
    };
    constexpr std::array<Window, 4> Windows{{
        {std::bit_cast<float>(0x3f800000u), std::bit_cast<float>(0x3f99999au), false, 19}, // 0x6b87d4, 0x6b8bb0
        {std::bit_cast<float>(0x3f8ccccdu), std::bit_cast<float>(0x3fb33333u), false, 25}, // 0x6b8830, 0x6b8c00
        {std::bit_cast<float>(0x3fa66666u), std::bit_cast<float>(0x3fcccccdu), false, 32}, // 0x6b8ac0, 0x6b8bfc
        {std::bit_cast<float>(0x3fc00000u), std::bit_cast<float>(0x3fe66666u), true, 38},  // 0x6b8b94, 0x6b8bf8
    }};
    for (const Window& window : Windows)
    {
        const bool belowTop = window.closed ? frame <= window.high : frame < window.high;
        if (frame > window.low && belowTop)
        {
            return window.bone;
        }
    }
    return std::nullopt;
}

// 0x44419f..0x4443d1, el galope en el mapa 10, sin mirar Kalima ni +0x78 del jinete: (rand() & 63) − 32 para la z del
// desplazamiento (120; 0; z) antes de mirar el frame, el punto del hueso, la onda 0x6eb/1 corrida (rand() & 15) − 8
// en x y en y, y las 5f2 (PuffTheIcarusDust).
Core::Expected<void, std::string> RaiseTheIcarusDust(const HelperState& helper, const HelperStepContext& context)
{
    constexpr float Reach = 120.0f;                                                 // 0x44419f
    constexpr double Drop = static_cast<double>(std::bit_cast<float>(0x42000000u)); // 0x6b883c: 32.
    constexpr int LiftSpread = 64, WaveSpread = 16, WaveCentre = 8;
    Render::Models::CrtRandom& random = context.random;
    const int lift = random.Next() % LiftSpread;
    const glm::vec3 offset(Reach, 0.0f, F32(static_cast<double>(lift) - Drop));
    const std::optional<std::size_t> bone = IcarusDustBone(helper.horse.animation.frame);
    if (!bone)
    {
        return {};
    }
    const auto point = GlobalPosePoint(context.world, *bone, offset, "0x4442b4");
    if (!point.HasValue())
    {
        return Core::Unexpected(point.Error());
    }
    const glm::vec3 base = point.Value();
    const int waveX = (random.Next() % WaveSpread) - WaveCentre;
    const int waveY = (random.Next() % WaveSpread) - WaveCentre;
    const glm::vec3 wave(F32(static_cast<double>(waveX) + static_cast<double>(base.x)),
                         F32(static_cast<double>(waveY) + static_cast<double>(base.y)), base.z);
    static_cast<void>(context.world.Map().effects.CreateGallopShockWave(wave, helper.angles, HelperDustLight, random));
    return PuffTheIcarusDust(base, helper.angles, context, "0x4443b1");
}

// 0x44416b..0x4444ef con la acción 0x4b del jinete, después de 0x4ea030(1): el polvo. Fuera del 10, con rand() % 2 en
// 0, fuera de Kalima y sin el 0x4000 del jinete (0x44440a..0x444412: sin jinete, nada; 0x443fa0 siempre le da uno),
// el polvo alrededor del caballo (ScatterTheDust: el 10 de 0x4444c0 no llega).
Core::Expected<void, std::string> RaiseTheDust(const HelperState& helper, const ActorState& rider,
                                               const HelperStepContext& context)
{
    const std::int32_t map = context.world.Runtime().map;
    if (map == IcarusMap)
    {
        return RaiseTheIcarusDust(helper, context);
    }
    if (context.random.Next() % DustOneIn != 0)
    {
        return {};
    }
    if (InKalima(map) || (rider.flags78 & RiderWithoutDust) != 0)
    {
        return {};
    }
    return ScatterTheDust(helper, context, "0x4444a6");
}

// 0x44453f..0x4445de: el aliento, con la acción del caballo distinta de 1 y sin el 0x4000 del jinete: con rand() % 3
// en 0, el punto del hueso 27 del buffer global y la 5f2, o la gota 607 en el 7, con la luz del caballo.
Core::Expected<void, std::string> Breathe(const HelperState& helper, const ActorState& rider,
                                          const HelperStepContext& context)
{
    if (helper.horse.animation.action == DustHorseAction || (rider.flags78 & RiderWithoutDust) != 0)
    {
        return {};
    }
    if (context.random.Next() % BreathOneIn != 0)
    {
        return {};
    }
    const auto point = GlobalPosePoint(context.world, BreathBone, BreathOffset, "0x44459d");
    if (!point.HasValue())
    {
        return Core::Unexpected(point.Error());
    }
    const std::int32_t type =
        context.world.Runtime().map == AtlansMap ? World::DropParticle : std::int32_t{World::BreathParticle};
    return EmitHelperParticle(context, type, ParticleSubtype, point.Value(), helper.angles, HorseLight, "0x4445d9");
}

// 0x4445e1..0x444654: con la acción 3 del caballo, la cinta 0x52e470 con el héroe (0x743a170) de dueño, del punto
// (60; 0; 0) del hueso 9 del buffer global (0x68b19cc) al punto del mismo hueso, en blanco, con el tipo, el largo y la
// key en 0. No saca rand().
constexpr std::size_t EarthquakeRibbonBone = 9;
constexpr glm::vec3 EarthquakeRibbonReach{60.0f, 0.0f, 0.0f}; // 0x4445ff
constexpr glm::vec3 EarthquakeRibbonLight{1.0f, 1.0f, 1.0f};  // 0x444629..0x444637
// 0x4453e0: la cuenta de la onda (0x7d3a0f0 % 10), la ventana del frame (0x6b86c4 y 0x6b8c04), el radio de las
// piedras por cada dos de la cuenta (0x6b87ac), el giro entre piedras (0x6b86cc), el temblor (0x6b8a5c) y la cuenta
// con que crea el 0xfa y la que le deja después.
constexpr std::int32_t ShockWaveEvery = 10;
constexpr float QuakeFirstFrame = 8.0f;
constexpr float QuakeLastFrame = 9.5f;
constexpr double StoneRingStep = 150.0;
constexpr double StoneTurn = 60.0;
constexpr int StoneCount = 6;
constexpr int StoneDegrees = 360;
constexpr int ShakeSpan = 3;
constexpr double ShakeUnit = static_cast<double>(std::bit_cast<float>(0x3f333333u));
constexpr std::uint8_t EarthquakeCount = 0x13;
constexpr std::uint8_t EarthquakeDone = 0xfd;
constexpr std::uint8_t EarthquakeState = 2; // 0x44558e, el estado (+0x88) del 0xfa

Core::Expected<void, std::string> LeaveTheEarthquakeRibbon(const HelperStepContext& context)
{
    const auto first = GlobalPosePoint(context.world, EarthquakeRibbonBone, EarthquakeRibbonReach, "0x44463e");
    if (!first.HasValue())
    {
        return Core::Unexpected(first.Error());
    }
    const auto second = GlobalPosePoint(context.world, EarthquakeRibbonBone, glm::vec3(0.0f), "0x444654");
    if (!second.HasValue())
    {
        return Core::Unexpected(second.Error());
    }
    ActorState& hero = context.world.Actors()[context.world.Runtime().hero];
    const auto created =
        CreateRibbon(context.world.DrawTail().ribbons, RibbonCreateRequest{.ownerContext = &hero,
                                                                           .ownerEquipment = &hero.level,
                                                                           .first = first.Value(),
                                                                           .second = second.Value(),
                                                                           .light = EarthquakeRibbonLight,
                                                                           .type = 0,
                                                                           .shortTrail = false,
                                                                           .key = 0});
    if (!created.HasValue())
    {
        return Core::Unexpected("0x444671 → " + created.Error());
    }
    return {};
}

// 0x4453e0(caballo, modelo), con la acción 3: la cuenta +0x8e sube (un byte) y, cada 10 pasos lógicos (0x7d3a0f0 con
// signo), la onda 0x6eb/0 en el caballo con sus ángulos y su luz (+0xf4). Con el frame en [8; 9,5] (un NaN cae
// afuera): con la cuenta impar, seis piedras 0x118 + (rand() & 1) en un anillo de radio (cuenta / 2) · 150 alrededor
// del caballo, con sus ángulos y su luz; la primera a rand() % 360 + 60 grados y cada una 60 más, el tipo sacado
// después del giro y antes del alta. Siempre, dentro de la ventana, el temblor de la cámara ((rand() % 3) − 3) · 0,7f.
// Fuera de ella, con la cuenta en 0x13, el terremoto 0xfa con el caballo de dueño y el estado 2 (que abre y cierra el
// registro propio, 3.848 rand() con la profundidad en 0), y la cuenta en 0xfd.
Core::Expected<void, std::string> ShakeTheEarth(HelperState& helper, const HelperStepContext& context)
{
    ++helper.horse.effectCount;
    World::ModelEffects& effects = context.world.Map().effects;
    if (context.logicSteps % ShockWaveEvery == 0)
    {
        static_cast<void>(effects.CreateStompShockWave(helper.position, helper.angles, helper.light)); // 0x445425
    }
    const float frame = helper.horse.animation.frame;
    if (frame >= QuakeFirstFrame && frame <= QuakeLastFrame)
    {
        if ((helper.horse.effectCount & 1) != 0)
        {
            const World::WorldTerrain& terrain =
                context.terrain != nullptr ? *context.terrain : context.world.Map().terrain;
            const glm::vec3 reach(0.0f, F32(static_cast<double>(helper.horse.effectCount >> 1) * StoneRingStep), 0.0f);
            glm::vec3 turn(0.0f, 0.0f, static_cast<float>(context.random.Next() % StoneDegrees));
            for (int stone = 0; stone < StoneCount; ++stone)
            {
                turn.z = F32(static_cast<double>(turn.z) + StoneTurn);
                const glm::vec3 offset =
                    Render::Models::OriginalVectorRotate(reach, Render::Models::OriginalAngleMatrix(turn));
                const glm::vec3 place(F32(static_cast<double>(offset.x) + static_cast<double>(helper.position.x)),
                                      F32(static_cast<double>(offset.y) + static_cast<double>(helper.position.y)),
                                      F32(static_cast<double>(offset.z) + static_cast<double>(helper.position.z)));
                const auto type =
                    static_cast<std::int16_t>(World::FirstGroundStoneEffect + (context.random.Next() & 1));
                const auto created = effects.CreateGroundStone(type, place, helper.angles, helper.light,
                                                               terrain.attributes.get(), context.random);
                if (!created.HasValue())
                {
                    return Core::Unexpected("0x44554a → " + created.Error());
                }
            }
        }
        if (context.cameraShake == nullptr)
        {
            return Core::Unexpected(std::string("0x445577: falta el temblor de la cámara (0x7d325c8)"));
        }
        *context.cameraShake = F32(static_cast<double>((context.random.Next() % ShakeSpan) - ShakeSpan) * ShakeUnit);
        return {};
    }
    if (helper.horse.effectCount == EarthquakeCount)
    {
        // 0x44558a..0x4455b3: el dueño se lee vivo (su tipo y sus ángulos) mientras el 0xfa dure.
        static_cast<void>(effects.CreateEarthquake(
            helper.position, helper.angles, helper.light, EarthquakeState,
            World::ModelEffectOwner{.type = &helper.type, .effect = nullptr, .angles = &helper.angles},
            context.world.Runtime().ownRecordDepth, context.world.OwnCharacter().bytef04, context.random));
        helper.horse.effectCount = EarthquakeDone;
    }
    return {};
}

// 0x44469d..0x44474c, con +8 del jinete en 1: cuatro vueltas que suben la z de los ángulos 90 (0x6b8948) desde
// (0; 0; 0), giran (0; 50; 0) con su matriz (0x5fc8f0 y 0x5fcbd0) y le suman la posición del caballo (+0x10), cada eje
// guardado en float. En cada vuelta, tres 0x530860(0x60f, punto, punto, ángulos, 0, caballo, 40, 2, 0, 0, −1) iguales,
// con el caballo (su HelperState) de dueño: nacen con su luz (+0xf4) y, con el ancho en 40, el paso no lo vuelve a
// leer. Tres rand() por joint; con el pool lleno, ninguno.
Core::Expected<void, std::string> RaiseTheOrbits(HelperState& helper, const HelperStepContext& context)
{
    constexpr int Turns = 4;                                                            // 0x4446c7
    constexpr int JointsPerTurn = 3;                                                    // 0x444705
    constexpr double TurnStep = static_cast<double>(std::bit_cast<float>(0x42b40000u)); // 0x6b8948: 90.
    constexpr std::int32_t OrbitJoint = 0x60f, OrbitSubtype = 0, OrbitKey = 2, NoTarget = -1;
    constexpr float OrbitWidth = 40.0f;       // 0x444727
    const glm::vec3 reach{0.0f, 50.0f, 0.0f}; // 0x4446b2..0x4446c0
    glm::vec3 angles{};                       // 0x44469d..0x4446ab
    for (int turn = 0; turn < Turns; ++turn)
    {
        angles.z = F32(static_cast<double>(angles.z) + TurnStep);
        const glm::vec3 rotated =
            Render::Models::OriginalVectorRotate(reach, Render::Models::OriginalAngleMatrix(angles));
        glm::vec3 point{F32(static_cast<double>(rotated.x) + static_cast<double>(helper.position.x)),
                        F32(static_cast<double>(rotated.y) + static_cast<double>(helper.position.y)),
                        F32(static_cast<double>(rotated.z) + static_cast<double>(helper.position.z))};
        for (int made = 0; made < JointsPerTurn; ++made)
        {
            const World::FishEffectRequest request{.type = OrbitJoint,
                                                   .firstPosition = point,
                                                   .secondPosition = point,
                                                   .angles = angles,
                                                   .subtype = OrbitSubtype,
                                                   .ownerContext = &helper,
                                                   .scale = OrbitWidth,
                                                   .key = OrbitKey,
                                                   .skill = 0,
                                                   .skillSerial = 0,
                                                   .target = NoTarget};
            const auto created =
                CreateFaunaJoint(context.world.DrawTail().joints, request, context.random, JointOwnerKind::Helper);
            if (!created.HasValue())
            {
                return Core::Unexpected(std::format("0x444740: {}", created.Error()));
            }
        }
    }
    return {};
}

// 0x4440b8..0x444766, el cuerpo del 0xbf.
Core::Expected<void, std::string> StepTheDarkHorse(HelperState& helper, ActorState& rider,
                                                   const Render::Models::Skeleton& skeleton,
                                                   const HelperStepContext& context)
{
    const auto safe = InTheRidersSafeCell(context, rider, "0x4440db");
    if (!safe.HasValue())
    {
        return Core::Unexpected(safe.Error());
    }
    // 0x4440eb: con el segundo argumento distinto de 0 (el retrato de las cartas), la celda no lo esconde.
    if (safe.Value() && !context.ignoresTheSafeCell)
    {
        // 0x4440f2..0x4440f8: al avance con la acción y la velocidad que tenía.
        helper.horse.alpha = HiddenAlpha;
        return {};
    }
    // 0x444103..0x444142: el hueso de la cabeza del modelo (+0x54) en 7 y la cabeza del jinete (+0x28..+0x30):
    // en DarkHorse.bmd el 7 es una raíz sin hijos que ningún vértice usa, y MuRet no la lleva.
    helper.position = rider.position;
    helper.angles = rider.angles;
    const std::uint8_t riderAction = rider.animation.action;
    // 0x444145..0x444535. Con otra acción, 0x444523 pone además +0x8e en 0, que solo lee el efecto de la
    // acción 3 (0x4453e0), que corta abajo.
    TakeTheRidersGait(helper.horse, skeleton, riderAction);
    if (riderAction == GallopingRiderAction)
    {
        if (auto dust = RaiseTheDust(helper, rider, context); !dust.HasValue())
        {
            return dust;
        }
    }
    if (auto breath = Breathe(helper, rider, context); !breath.HasValue())
    {
        return breath;
    }
    // 0x4445e1..0x44468a: la acción 3, el terremoto del Dark Lord: la cinta y 0x4453e0 (la acción se vuelve a mirar
    // entre las dos, y la cinta no la cambia).
    if (helper.horse.animation.action == EffectHorseAction)
    {
        if (auto ribbon = LeaveTheEarthquakeRibbon(context); !ribbon.HasValue())
        {
            return ribbon;
        }
        if (auto quake = ShakeTheEarth(helper, context); !quake.HasValue())
        {
            return quake;
        }
    }
    if (rider.byte8 == RiderWithOrbits)
    {
        if (auto orbits = RaiseTheOrbits(helper, context); !orbits.HasValue())
        {
            return orbits;
        }
    }
    rider.byte8 = 0;            // 0x444758
    helper.alive = rider.alive; // 0x44475c..0x444764
    return {};
}

// 0x444059..0x4440b3: el cuerpo por el tipo. El 0xae (0x44476b) no se crea en MuRet; los demás tipos van directo al
// avance, pero 0x443d70 no los construye.
bool HasItsBody(std::uint16_t type)
{
    return type == DarkHorseModel || type == RiderlessHelper || IsRidingHelper(type);
}

Core::Expected<void, std::string> StepTheBody(HelperState& helper, ActorState& rider,
                                              const Render::Models::Skeleton& skeleton,
                                              const HelperStepContext& context)
{
    if (helper.type == DarkHorseModel)
    {
        return StepTheDarkHorse(helper, rider, skeleton, context);
    }
    if (helper.type == RiderlessHelper)
    {
        return SparkleTheGuardianAngel(helper, rider, context);
    }
    if (IsFenrir(helper.type))
    {
        return StepFenrir(helper, rider, skeleton, context);
    }
    return StepRidingHelper(helper, rider, skeleton, context);
}

// 0x444010(lugar, 0) con un lugar vivo, en la escena 4 o en la 5, con su jinete (+0x108).
Core::Expected<void, std::string> StepHelper(HelperState& helper, ActorState& rider, const HelperStepContext& context)
{
    // 0x44402a..0x444047: solo en la escena 5, con el jinete muerto o sin el 1 en +0x88.
    if (context.scene == SceneId::Game && (!rider.alive || rider.byte88 != RiderShown))
    {
        helper.alive = false;
        return {};
    }
    FadeInDarkHorse(helper.horse); // 0x444054
    if (!HasItsBody(helper.type))
    {
        return Core::Unexpected(std::format("0x444083: el paso del ayudante {:#x} no está portado", helper.type));
    }
    const Render::Models::Skeleton* skeleton =
        context.models != nullptr ? context.models->SkeletonOf(helper.type) : nullptr;
    if (skeleton == nullptr)
    {
        return Core::Unexpected(
            std::format("0x444010: el ayudante {:#x} sin su modelo ({})", helper.type,
                        helper.type == RiderlessHelper ? "Player\\Helper01.bmd, 0x60ff50" : "Skill\\, 0x615700"));
    }
    // 0x444080..0x44408b: dónde estaba el jinete al empezar, para el vuelo del 0x1c12.
    const glm::vec2 riderStart(rider.position.x, rider.position.y);
    if (auto body = StepTheBody(helper, rider, *skeleton, context); !body.HasValue())
    {
        return body;
    }
    // 0x445006..0x445039: +0xa0 del modelo con la acción, que lo portado no lee, y el avance.
    AdvanceDarkHorse(helper.horse, *skeleton,
                     context.scene == SceneId::CharacterSelect ? Render::Models::AnimationScene::CharacterSelect
                                                               : Render::Models::AnimationScene::Other);
    if (helper.type == RiderlessHelper)
    {
        FlyTheGuardianAngel(helper, rider, riderStart, context.random);
    }
    return {};
}

// 0x4452db..0x445309: con el segundo argumento en 0, 1,6 en la escena 2 y 1 en la 4; en las demás, los Fenrir se
// quedan con la de 0x443d70 (0,9) y los demás van a 1.
float DrawScaleOf(const HelperState& helper, SceneId scene)
{
    if (scene == SceneId::Login)
    {
        return DarkHorseLoginScale; // 0x3fcccccd
    }
    if (scene != SceneId::CharacterSelect && IsFenrir(helper.type))
    {
        return helper.scale;
    }
    return HorseScale;
}

// Lo que se dibuja de un ayudante que no es el caballo: su lugar, sus ángulos y su escala, la animación entre dos
// pasos, el blendMesh con su luz en 1, la U y la V en 0, sin malla oculta, el color de 0x5ffce0 con la luz por
// vértice (la del terreno más la de +0xf4, el 3 de 0x443d70) y su alpha.
DrawnObject DrawnHelper(const HelperState& helper, const Render::Models::Skeleton* skeleton,
                        const glm::vec3& terrainLight, float stepAlpha)
{
    DrawnObject drawn;
    drawn.model = helper.type;
    drawn.animation = helper.horse.animation;
    if (skeleton != nullptr && helper.horse.animation.action < skeleton->actions.size())
    {
        drawn.animation = Render::Models::Interpolated(
            helper.horse.animation, skeleton->actions[helper.horse.animation.action], helper.horse.speed, stepAlpha);
    }
    drawn.transform = Render::Models::ObjectTransform{.position = helper.position, .angles = helper.angles};
    drawn.transform.scale = helper.scale;
    drawn.state.blendMesh = helper.blendMesh;
    drawn.color = terrainLight + HorseLight;
    drawn.alpha = helper.horse.alpha;
    drawn.vertexLight = true;
    return drawn;
}

// 0x60277b..0x602e7d: el seno del reloj, los dos huesos de los sprites con sus corrimientos, sus luces y escalas,
// la luz y el subtipo de los rayos por color, y las ventanas de los calcos.
constexpr double FenrirPulseRate = static_cast<double>(std::bit_cast<float>(0x3b03126fu)); // 0x6b89bc: 0,002f.
constexpr float FenrirPulseSwing = std::bit_cast<float>(0x3e4ccccdu);                      // 0x6b8854: 0,2f.
constexpr std::uint8_t FenrirChargeAction = 4;
constexpr std::uint8_t FenrirFirstStrikeAction = 1, FenrirSecondStrikeAction = 2;
constexpr int FenrirSparkSpreadX = 10, FenrirSparkSpreadZ = 40, FenrirSparkCentreZ = 20;
constexpr double Half = 0.5;                                                                    // 0x6b882c.
constexpr double FenrirSparkScaleStep = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu)); // 0x6b8a48.
constexpr double FenrirSparkScaleBase = static_cast<double>(std::bit_cast<float>(0x3f333333u)); // 0x6b8a5c.
constexpr std::size_t FenrirSparkBone = 14, FenrirEyeBone = 11, FenrirChestBone = 13;
constexpr double FenrirEyeRed = static_cast<double>(std::bit_cast<float>(0x3f666666u));   // 0x6b8a1c: 0,9f.
constexpr double FenrirEyeGreen = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)); // 0x6b8854: 0,2f.
constexpr double FenrirEyeBlue = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));  // 0x6b87b8: 0,1f.
constexpr double FenrirEyeScaleStep = FenrirEyeBlue;                                      // 0x6b87b8.
constexpr glm::vec3 FenrirLeftEye{50.0f, 2.0f, 11.0f}, FenrirRightEye{50.0f, 2.0f, -11.0f};
constexpr glm::vec3 FenrirChest{40.0f, 15.0f, 0.0f};
constexpr glm::vec3 FenrirChestLight{1.0f, std::bit_cast<float>(0x3e99999au), std::bit_cast<float>(0x3e4ccccdu)};
constexpr float FenrirChestScale = 1.5f;
constexpr std::uint16_t FenrirFlare = 0x5ac;
constexpr std::uint16_t RedFenrir = 0x14b, BlueFenrir = 0x14c;
constexpr std::array<std::size_t, 4> FenrirStrikeBones{22, 28, 36, 44};

// Un punto del buffer global con un hueso de los de arriba, todos por debajo de los 200 del buffer.
glm::vec3 FenrirBonePoint(std::span<const Render::Models::BoneTransform> bones, std::size_t bone,
                          const glm::vec3& offset)
{
    return GlobalPosePoint(bones, bone, offset, "0x4ec110").Value();
}

// 0x60277b..0x602e7d, después de 0x5ffdf0: el cuerpo y su sombra, la chispa de la acción 4, los sprites, los rayos y
// los calcos. Pide todo antes de sacar el primer rand().
Core::Expected<void, std::string> DrawTheFenrir(HelperState& helper, const HelperDrawContext& context)
{
    if (context.effects == nullptr || context.particles == nullptr || context.sprites == nullptr ||
        !context.systemTime || !context.globalPose)
    {
        return Core::Unexpected(std::format("0x60277b: el dibujo del Fenrir {:#x} pide los efectos (0x518cf0), las "
                                            "partículas (0x53fe70), los sprites (0x5491e0), timeGetTime() y el buffer "
                                            "global de la pose",
                                            helper.type));
    }
    // 0x60277b..0x602792: fld del reloj, fmul 0,002f, fsin, fmul 0,2f y fstp.
    const auto sine = Render::Models::Detail::SineProductPC53(static_cast<double>(context.worldClock) * FenrirPulseRate,
                                                              FenrirPulseSwing);
    if (!sine)
        return Core::Unexpected(std::string("0x60278a: el seno del reloj del Fenrir no cabe en el perfil PC53"));
    const double pulse = static_cast<double>(F32(*sine));
    const std::int32_t map = context.world.Runtime().map;
    auto drawn = DrawnHelper(helper, context.models.SkeletonOf(helper.type), glm::vec3(0.0f), context.stepAlpha);
    if (context.onPose)
        if (const auto published = context.onPose(drawn); !published.HasValue())
            return published;
    if (context.posedMeshes)
        drawn.retainedMeshes = context.posedMeshes();
    const bool charging = helper.horse.animation.action == FenrirChargeAction;
    if (auto body = DrawFenrirBody(context.batch, context.models, drawn, map, charging, context.frameClock);
        !body.HasValue())
        return body;
    const std::span<const Render::Models::BoneTransform> bones = context.globalPose();
    Render::Models::CrtRandom& random = context.random;
    if (charging)
    {
        // 0x60282f..0x6028c6: (rand() % 10 − 10; 0; rand() % 40 − 20) por 0,5 desde el hueso 14 y la 5c6/15 roja,
        // con los ángulos del Fenrir y la escala del seno por 0,05f más 0,7f.
        const int x = (random.Next() % FenrirSparkSpreadX) - FenrirSparkSpreadX;
        const int z = (random.Next() % FenrirSparkSpreadZ) - FenrirSparkCentreZ;
        const glm::vec3 offset(F32(static_cast<double>(x) * Half), 0.0f, F32(static_cast<double>(z) * Half));
        const glm::vec3 point = FenrirBonePoint(bones, FenrirSparkBone, offset);
        const float scale = F32(pulse * FenrirSparkScaleStep + FenrirSparkScaleBase);
        const auto created =
            context.particles->CreateChecked(World::SparkParticle, point, helper.angles, glm::vec3(1.0f, 0.0f, 0.0f),
                                             World::FenrirSparkSubtype, scale, context.worldClock, random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x6028c6: el alta de 0x5c6/15 falla con error {}",
                                                static_cast<unsigned>(created.Error())));
    }
    // 0x602930..0x602aa2: los ojos (el hueso 11) con la luz del seno, dos sprites en cada uno de escala seno · 0,1f +
    // 0,5, y el pecho (el 13) con uno de 1,5 y otro de 1 en (1; 0,3; 0,2).
    const double halfPulse = pulse * Half;
    const glm::vec3 eyeLight(F32(pulse + FenrirEyeRed), F32(halfPulse + FenrirEyeGreen),
                             F32(halfPulse + FenrirEyeBlue));
    const float eyeScale = F32(pulse * FenrirEyeScaleStep + Half);
    for (const glm::vec3& eye : {FenrirLeftEye, FenrirRightEye})
    {
        const glm::vec3 point = FenrirBonePoint(bones, FenrirEyeBone, eye);
        static_cast<void>(context.sprites->Create(FenrirFlare, point, eyeScale, eyeLight, 0.0f, 0));
        static_cast<void>(context.sprites->Create(FenrirFlare, point, eyeScale, eyeLight, 0.0f, 0));
    }
    const glm::vec3 chest = FenrirBonePoint(bones, FenrirChestBone, FenrirChest);
    static_cast<void>(context.sprites->Create(FenrirFlare, chest, FenrirChestScale, FenrirChestLight, 0.0f, 0));
    static_cast<void>(context.sprites->Create(FenrirFlare, chest, 1.0f, FenrirChestLight, 0.0f, 0));
    // 0x602aa7..0x602b50: la luz y el subtipo por color y dos 0x518cf0(0x14d, lugar, ángulos, luz, 0, Fenrir, −1).
    glm::vec3 strikeLight(1.0f, 1.0f, std::bit_cast<float>(0x3e4ccccdu));
    std::int32_t strike = 3;
    if (helper.type == RedFenrir)
    {
        strikeLight = glm::vec3(std::bit_cast<float>(0x3f4ccccdu), 0.0f, 0.0f);
        strike = 1;
    }
    else if (helper.type == BlueFenrir)
    {
        strikeLight = glm::vec3(std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3dcccccdu),
                                std::bit_cast<float>(0x3f4ccccdu));
        strike = 2;
    }
    const auto bonePoint = [bones](std::size_t bone) { return FenrirBonePoint(bones, bone, glm::vec3(0.0f)); };
    for (int bolt = 0; bolt < 2; ++bolt)
    {
        const auto created = context.effects->CreateLightning(helper.position, helper.angles, strikeLight, &helper,
                                                              random, bonePoint, *context.sprites);
        if (!created.HasValue())
            return Core::Unexpected("0x602b30 → " + created.Error());
    }
    // 0x602b55..0x602e75: los calcos 0x14e en blanco, con el subtipo del color, en los huesos de la ventana del frame.
    const std::uint8_t action = helper.horse.animation.action;
    const float frame = helper.horse.animation.frame;
    std::span<const std::size_t> strikes{};
    if (action == FenrirFirstStrikeAction && frame >= 0.0f && frame <= 1.5f)
        strikes = FenrirStrikeBones;
    else if (action == FenrirSecondStrikeAction && frame > 1.0f && frame <= std::bit_cast<float>(0x3fb33333u))
        strikes = std::span(FenrirStrikeBones).first(2);
    else if (action == FenrirSecondStrikeAction && frame > std::bit_cast<float>(0x4099999au) &&
             frame <= std::bit_cast<float>(0x40a66666u))
        strikes = std::span(FenrirStrikeBones).last(2);
    for (const std::size_t bone : strikes)
    {
        const glm::vec3 point = FenrirBonePoint(bones, bone, glm::vec3(0.0f));
        static_cast<void>(context.effects->CreateLightningDecal(point, helper.angles, glm::vec3(1.0f), strike, &helper,
                                                                context.systemTime()));
    }
    return {};
}

// 0x6002e0 con el ayudante, ya posado por 0x5ffdf0.
Core::Expected<void, std::string> DrawTheBody(HelperState& helper, const HelperDrawContext& context)
{
    const std::int32_t map = context.world.Runtime().map;
    if (helper.type == DarkHorseModel)
    {
        const Render::Models::ObjectTransform place{.position = helper.position, .angles = helper.angles};
        auto drawn = DrawnDarkHorse(helper.horse, context.models.SkeletonOf(DarkHorseModel), place, helper.scale,
                                    context.stepAlpha);
        if (context.onPose)
            if (const auto published = context.onPose(drawn); !published.HasValue())
                return published;
        if (context.posedMeshes)
            drawn.retainedMeshes = context.posedMeshes();
        DrawDarkHorse(context.batch, context.models, drawn, map, context.frameClock);
        return {};
    }
    if (IsFenrir(helper.type))
        return DrawTheFenrir(helper, context);
    // 0x5ffce0: la luz del terreno en su lugar.
    if (context.lighting == nullptr)
    {
        return Core::Unexpected(std::format(
            "0x5ffce0: falta el buffer dinámico de la luz del terreno para el ayudante {:#x}", helper.type));
    }
    const glm::vec3 terrainLight = World::LightAt(*context.lighting, helper.position.x, helper.position.y);
    auto drawn = DrawnHelper(helper, context.models.SkeletonOf(helper.type), terrainLight, context.stepAlpha);
    if (context.onPose)
        if (const auto published = context.onPose(drawn); !published.HasValue())
            return published;
    if (context.posedMeshes)
        drawn.retainedMeshes = context.posedMeshes();
    // 0x600b35 (el Dinorant) y 0x602360 (el Uniria y el Guardian Angel): RenderBody(2, alpha, blendMesh, 1, 0, 0,
    // −1, −1).
    DrawObject(context.batch, context.models, drawn, context.frameClock);
    return {};
}

// 0x600343..0x600399: el camino del 10 de 0x6002e0 busca al jinete (+0x108) en los 400 lugares del pool (0x743a16c,
// de a 0x438). Si es otro que el héroe (0x743a170), con el asedio (0x4294b0 lee 0x7239ec) y su +0x78 & 0x4000, con
// +0x370 distinto de 1 sale por 0x602e7d sin dibujar nada ni tocar el color del modelo.
constexpr std::size_t CharacterTableSlots = 400; // 0x69780 / 0x438
constexpr std::uint8_t ShownInTheSiege = 1;

bool HiddenInTheSiege(const WorldState& world, std::size_t rider)
{
    const WorldRuntime& runtime = world.Runtime();
    if (rider >= CharacterTableSlots || rider == runtime.hero || !runtime.siegeActive)
    {
        return false;
    }
    const ActorState& riding = world.Actors()[rider];
    return (riding.flags78 & RiderWithoutDust) != 0 && riding.byte370 != ShownInTheSiege;
}

// 0x6002e0 con 10 (0x60033c..0x600435), con el ayudante ya posado por 0x5ffdf0 como lo posa el camino de su tipo: no
// llega al caso del tipo, así que el Fenrir no pide sus efectos ni saca sus rand(), y ninguno lee la luz del terreno,
// que 0x60039d pisa con el blanco. El 0x1c12 conserva su blendMesh, que el 0x4000 no mira.
Core::Expected<void, std::string> DrawTheShell(HelperState& helper, const HelperDrawContext& context)
{
    DrawnObject drawn;
    if (helper.type == DarkHorseModel)
    {
        const Render::Models::ObjectTransform place{.position = helper.position, .angles = helper.angles};
        drawn = DrawnDarkHorse(helper.horse, context.models.SkeletonOf(DarkHorseModel), place, helper.scale,
                               context.stepAlpha);
    }
    else
    {
        drawn = DrawnHelper(helper, context.models.SkeletonOf(helper.type), glm::vec3(0.0f), context.stepAlpha);
    }
    if (context.onPose)
        if (const auto published = context.onPose(drawn); !published.HasValue())
            return published;
    if (context.posedMeshes)
        drawn.retainedMeshes = context.posedMeshes();
    if (HiddenInTheSiege(context.world, helper.rider))
    {
        return {};
    }
    return DrawChromeShell(context.batch, context.models, drawn, context.frameClock);
}

// 0x445260(lugar, `always`) con un lugar vivo y su jinete (+0x108). Con `always` (el retrato de las cartas), visto sin
// el trapecio (0x4452a4) y con la escala de 0x4452d2.
Core::Expected<void, std::string> DrawHelper(HelperState& helper, const ActorState& rider, bool always,
                                             const HelperDrawContext& context)
{
    WorldState& world = context.world;
    const WorldRuntime& runtime = world.Runtime();
    helper.seen = always || CharacterSeen(runtime.terrainPolygon, helper.position);
    if (!helper.seen)
    {
        return {};
    }
    if (rider.type != Data::Models::PlayerSkeletonModel && helper.type != RiderlessHelper)
    {
        return {};
    }
    if (helper.type != DarkHorseModel && helper.type != RiderlessHelper && !IsRidingHelper(helper.type))
    {
        return Core::Unexpected(std::format("0x445260: el dibujo del ayudante {:#x} no está portado", helper.type));
    }
    helper.scale = always ? PortraitHelperScale : DrawScaleOf(helper, context.scene);
    // 0x602e90 → 0x5ffdf0: en los mapas 18 a 23, con +0x1a1 del héroe, el alpha baja 0,15f y queda guardado, una
    // vez por frame del original.
    if (context.originalFrame)
        helper.horse.alpha =
            World::ChaosCastlePoseAlpha(runtime.map, world.Actors()[runtime.hero].byte1a1, helper.horse.alpha);
    // 0x5ffe2a..0x5ffe3b: por debajo de 0,01 (o sin orden) no lo dibuja.
    if (helper.horse.alpha >= DrawnAlpha)
    {
        if ((rider.flags78 & RiderFrozen) != 0)
        {
            helper.horse.animation.frame = 0.0f; // 0x5fff0d
        }
        // 0x445310..0x445323: con 0x4000 en +0x78 del jinete, 0x602e90(lugar, 0, 0, 10).
        const bool shell = (rider.flags78 & RiderWithoutDust) != 0;
        if (auto body = shell ? DrawTheShell(helper, context) : DrawTheBody(helper, context); !body.HasValue())
        {
            return body;
        }
    }
    // 0x445336..0x445358: rand() % 30 + 70, por 0,01f en la x87; solo el 0x1c12 la usa.
    const double glow = static_cast<double>((context.random.Next() % GlowSpan) + GlowBase) * GlowUnit;
    if (helper.type != RiderlessHelper)
    {
        return {};
    }
    if (context.sprites == nullptr)
    {
        return Core::Unexpected(std::string("0x445390: falta la tabla de los sprites del frame (0x5491e0) para el "
                                            "brillo del Guardian Angel"));
    }
    const glm::vec3 light(F32(glow * GlowRed), F32(glow * GlowGreen), F32(glow * GlowBlue));
    static_cast<void>(context.sprites->Create(GlowSprite, helper.position, GlowScale, light, 0.0f, 0));
    return {};
}
} // namespace

Core::Expected<bool, std::string> ConstructHelper(HelperState& helper, std::uint16_t type, const glm::vec3& position,
                                                  const ActorState& rider, std::size_t riderSlot, std::int32_t map,
                                                  Render::Models::CrtRandom* random)
{
    // 0x443d73..0x443d87: en los mapas 18 a 23, nada (devuelve 0).
    if (InChaosCastle(map))
    {
        return false;
    }
    // 0x443d8d: el lugar vivo queda como está (devuelve 1: 0x443fa0 prueba el siguiente).
    if (helper.alive)
    {
        return false;
    }
    if (type != DarkHorseModel && type != RiderlessHelper && !IsRidingHelper(type))
    {
        return Core::Unexpected(std::format("0x443d70: el constructor del ayudante {:#x} no está portado", type));
    }
    helper.alive = true;
    helper.type = type;
    helper.rider = riderSlot;
    helper.seen = false;
    helper.scale = type == DarkHorseModel ? HorseScale : IsRidingHelper(type) ? RidingHelperScale : GuardianScale;
    helper.position = position;
    helper.angles = rider.angles;
    // 0x443e1d y 0x443e23: la key anterior y el frame; +0x111 y +0x112 quedan (Q-C221).
    helper.horse.animation.previousKey = 0;
    helper.horse.animation.frame = 0.0f;
    helper.horse.speed = CreatedSpeed;
    helper.horse.alpha = CreatedAlpha;
    helper.light = HorseLight; // 0x443e2f..0x443e45
    helper.blendMesh = NoBlendMesh;
    if (type == RiderlessHelper)
    {
        helper.blendMesh = GuardianBlendMesh; // 0x443f1a
        if (random == nullptr)
        {
            return Core::Unexpected(std::string("0x443f21: falta el rand() compartido del Guardian Angel"));
        }
        constexpr int HorizontalSpan = 512;
        constexpr int HorizontalCentre = 256;
        constexpr int VerticalSpan = 128;
        constexpr int VerticalBase = 128;
        helper.position.x = static_cast<float>(static_cast<double>(rider.position.x) +
                                               (random->Next() % HorizontalSpan) - HorizontalCentre);
        helper.position.y = static_cast<float>(static_cast<double>(rider.position.y) +
                                               (random->Next() % HorizontalSpan) - HorizontalCentre);
        helper.position.z =
            static_cast<float>(static_cast<double>(rider.position.z) + (random->Next() % VerticalSpan) + VerticalBase);
    }
    return true;
}

Core::Expected<std::optional<std::size_t>, std::string> CreateHelper(WorldState& world, std::uint16_t type,
                                                                     const glm::vec3& position, std::size_t rider,
                                                                     Render::Models::CrtRandom* random)
{
    const std::int32_t map = world.Runtime().map;
    const ActorState& riding = world.Actors()[rider];
    if (InChaosCastle(map) || (riding.type != Data::Models::PlayerSkeletonModel && type != RiderlessHelper))
    {
        return std::optional<std::size_t>{};
    }
    if (type != DarkHorseModel && type != RiderlessHelper && !IsRidingHelper(type))
    {
        return Core::Unexpected(std::format("0x443d70: el constructor del ayudante {:#x} no está portado", type));
    }
    HelperTable& helpers = world.Helpers();
    for (std::size_t slot = 0; slot < helpers.size(); ++slot)
    {
        auto built = ConstructHelper(helpers[slot], type, position, riding, rider, map, random);
        if (!built.HasValue())
        {
            return Core::Unexpected(built.Error());
        }
        if (built.Value())
        {
            return std::optional<std::size_t>{slot};
        }
    }
    return std::optional<std::size_t>{};
}

void TurnOffHelpersOf(HelperTable& helpers, std::size_t rider)
{
    for (HelperState& helper : helpers)
    {
        if (helper.alive && helper.rider == rider)
        {
            helper.alive = false;
        }
    }
}

Core::Expected<void, std::string> StepHelpers(const HelperStepContext& context)
{
    for (HelperState& helper : context.world.Helpers())
    {
        if (!helper.alive)
        {
            continue;
        }
        if (auto stepped = StepHelper(helper, context.world.Actors()[helper.rider], context); !stepped.HasValue())
        {
            return stepped;
        }
    }
    return {};
}

Core::Expected<void, std::string> StepTheHelperOutsideTheTable(HelperState& helper, ActorState& rider,
                                                               const HelperStepContext& context)
{
    // 0x444021: sin el byte vivo, nada.
    if (!helper.alive)
    {
        return {};
    }
    HelperStepContext anywhere = context;
    anywhere.ignoresTheSafeCell = true;
    return StepHelper(helper, rider, anywhere);
}

Core::Expected<void, std::string> DrawTheHelperOutsideTheTable(HelperState& helper, const ActorState& rider,
                                                               const HelperDrawContext& context)
{
    // 0x44526b: sin el byte vivo, nada.
    if (!helper.alive)
    {
        return {};
    }
    return DrawHelper(helper, rider, true, context);
}

Core::Expected<void, std::string> DrawHelpers(const HelperDrawContext& context)
{
    Render::Models::CrtRandom projectedRandom = context.random;
    const HelperDrawContext projected{context.batch,
                                      context.models,
                                      context.world,
                                      projectedRandom,
                                      context.frameClock,
                                      context.stepAlpha,
                                      true,
                                      context.publishProjectedPose ? context.onPose : decltype(context.onPose){},
                                      false,
                                      context.posedMeshes,
                                      context.scene,
                                      context.lighting,
                                      context.sprites,
                                      context.worldClock,
                                      context.effects,
                                      context.particles,
                                      context.systemTime,
                                      context.globalPose};
    for (HelperState& helper : context.world.Helpers())
    {
        if (!helper.alive)
        {
            continue;
        }
        HelperState projectedHelper = helper;
        const ActorState& rider = context.world.Actors()[helper.rider];
        auto drawn = context.originalFrame ? DrawHelper(helper, rider, false, context)
                                           : DrawHelper(projectedHelper, rider, false, projected);
        if (!drawn.HasValue())
        {
            return drawn;
        }
    }
    return {};
}
} // namespace Client::Scenes
