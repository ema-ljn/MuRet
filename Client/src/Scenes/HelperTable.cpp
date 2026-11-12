#include "Scenes/HelperTable.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/HelperStepParts.h"
#include "Scenes/PetHelperSteps.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ChaosCastle.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/TerrainLighting.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
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
// 0x444693: con 1 en +8 del jinete, los doce 0x60f.
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
    if (safe.Value())
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
    if (helper.horse.animation.action == EffectHorseAction)
    {
        return Core::Unexpected(std::string("0x4445e1: con la acción 3 del caballo, 0x52e470 con el héroe y 0x4453e0 "
                                            "crean efectos que MuRet no tiene en el juego"));
    }
    if (rider.byte8 == RiderWithOrbits)
    {
        return Core::Unexpected(std::string("0x444693: con +8 del jinete en 1, doce 0x530860(0x60f) alrededor del "
                                            "caballo, que MuRet no tiene en el juego"));
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

// 0x444010(lugar, 0) con un lugar vivo, en la escena 4 o en la 5.
Core::Expected<void, std::string> StepHelper(HelperState& helper, const HelperStepContext& context)
{
    ActorState& rider = context.world.Actors()[helper.rider];
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
    {
        return Core::Unexpected(std::format("0x60277b: el dibujo del Fenrir {:#x} (sus pases, la partícula 0x5c6, la "
                                            "sombra, los sprites de sus huesos y los dos efectos 0x14d de 0x602b30) "
                                            "no está portado",
                                            helper.type));
    }
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

// 0x445260(lugar, 0) con un lugar vivo.
Core::Expected<void, std::string> DrawHelper(HelperState& helper, const HelperDrawContext& context)
{
    WorldState& world = context.world;
    const WorldRuntime& runtime = world.Runtime();
    helper.seen = CharacterSeen(runtime.terrainPolygon, helper.position);
    if (!helper.seen)
    {
        return {};
    }
    const ActorState& rider = world.Actors()[helper.rider];
    if (rider.type != Data::Models::PlayerSkeletonModel && helper.type != RiderlessHelper)
    {
        return {};
    }
    if (helper.type != DarkHorseModel && helper.type != RiderlessHelper && !IsRidingHelper(helper.type))
    {
        return Core::Unexpected(std::format("0x445260: el dibujo del ayudante {:#x} no está portado", helper.type));
    }
    helper.scale = DrawScaleOf(helper, context.scene);
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
        if ((rider.flags78 & RiderWithoutDust) != 0)
        {
            return Core::Unexpected(
                std::string("0x60033c: con +0x78 & 0x4000 del jinete, 0x445310 dibuja el ayudante con "
                            "10, que 0x6002e0 lleva por otro camino que MuRet no tiene"));
        }
        if (auto body = DrawTheBody(helper, context); !body.HasValue())
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
        HelperState& helper = helpers[slot];
        if (helper.alive)
        {
            continue;
        }
        helper.alive = true;
        helper.type = type;
        helper.rider = rider;
        helper.seen = false;
        helper.scale = type == DarkHorseModel ? HorseScale : IsRidingHelper(type) ? RidingHelperScale : GuardianScale;
        helper.position = position;
        helper.angles = riding.angles;
        // 0x443e1d y 0x443e23: la key anterior y el frame; +0x111 y +0x112 quedan (Q-C221).
        helper.horse.animation.previousKey = 0;
        helper.horse.animation.frame = 0.0f;
        helper.horse.speed = CreatedSpeed;
        helper.horse.alpha = CreatedAlpha;
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
            helper.position.x = static_cast<float>(static_cast<double>(riding.position.x) +
                                                   (random->Next() % HorizontalSpan) - HorizontalCentre);
            helper.position.y = static_cast<float>(static_cast<double>(riding.position.y) +
                                                   (random->Next() % HorizontalSpan) - HorizontalCentre);
            helper.position.z = static_cast<float>(static_cast<double>(riding.position.z) +
                                                   (random->Next() % VerticalSpan) + VerticalBase);
        }
        return std::optional<std::size_t>{slot};
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
        if (auto stepped = StepHelper(helper, context); !stepped.HasValue())
        {
            return stepped;
        }
    }
    return {};
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
                                      context.sprites};
    for (HelperState& helper : context.world.Helpers())
    {
        if (!helper.alive)
        {
            continue;
        }
        HelperState projectedHelper = helper;
        auto drawn = context.originalFrame ? DrawHelper(helper, context) : DrawHelper(projectedHelper, projected);
        if (!drawn.HasValue())
        {
            return drawn;
        }
    }
    return {};
}
} // namespace Client::Scenes
