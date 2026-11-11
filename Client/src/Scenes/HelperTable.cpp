#include "Scenes/HelperTable.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ChaosCastle.h"
#include "World/OriginalFtol.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace Client::Scenes
{
namespace
{
// 0x443faa..0x443fb3 y 0x5ffdff..0x5ffe07: los mapas 18 a 23.
constexpr std::int32_t FirstChaosCastleMap = 18;
constexpr std::int32_t LastChaosCastleMap = 23;
// 0x443fc0 y 0x4452c2: el ayudante que no pide un jugador de jinete.
constexpr std::uint16_t RiderlessHelper = 0x1c12;
// 0x443e82: la escala del 0xbf al crearlo; 0x445309, la del dibujo en la escena 5.
constexpr float HorseScale = 1.0f;
constexpr float RidingHelperScale = 0.9f;
constexpr float GuardianScale = 0.7f;

bool IsRidingHelper(std::uint16_t type)
{
    constexpr std::uint16_t Unilia = 0xc6;
    constexpr std::uint16_t Dinorant = 0x110;
    constexpr std::uint16_t FirstFenrir = 0x14a;
    constexpr std::uint16_t LastFenrir = 0x14c;
    return type == Unilia || type == Dinorant || (type >= FirstFenrir && type <= LastFenrir);
}
// 0x443e4b y 0x443df7: la velocidad (+0xd8) y el alpha (+0x174) al crearlo.
constexpr float CreatedSpeed = 0.5f;
constexpr float CreatedAlpha = 0.0f;
// 0x4440bb y 0x4440c9: la posición del jinete por 0,01f (0x6b86c0) y truncada (0x69fd3c).
constexpr double CellsPerUnit = static_cast<double>(0.01f);
// 0x5f82a0: el byte bajo de cada coordenada.
constexpr std::int32_t CellMask = 0xff;
// 0x4440e5: el bit 0 del byte bajo del atributo, la zona segura.
constexpr std::uint16_t SafeCellBit = 0x01;
// 0x4440f2: el EBX en 0 de 0x44401e.
constexpr float HiddenAlpha = 0.0f;
// 0x44403e: con el +0x88 del jinete distinto de 1, la escena 5 lo apaga.
constexpr std::uint8_t RiderShown = 1;
// Las acciones del jinete (+0x111) y del caballo que miran el polvo, el aliento y el efecto.
constexpr std::uint8_t GallopingRiderAction = 0x4b;
constexpr std::uint8_t DustHorseAction = 1;
constexpr std::uint8_t EffectHorseAction = 3;
// 0x444196: en el mapa 10 el polvo es otro.
constexpr std::int32_t IcarusMap = 10;
// 0x4443f6..0x444408: los mapas de Kalima, del 24 al 29 y el 36, no tienen polvo.
constexpr std::int32_t FirstKalimaMap = 24;
constexpr std::int32_t LastKalimaMap = 29;
constexpr std::int32_t SeventhKalimaMap = 36;
// 0x444414..0x44441d y 0x44455a..0x444563: con 0x4000 en +0x78 del jinete, ni polvo ni aliento; 0x445310
// dibuja con 10. 0x5ffefc: con 0x1000, el frame del caballo en 0.
constexpr std::uint32_t RiderWithoutDust = 0x4000;
constexpr std::uint32_t RiderFrozen = 0x1000;
// 0x4443e3 y 0x44456b: rand() % 2 y rand() % 3 en 0.
constexpr int DustOneIn = 2;
constexpr int BreathOneIn = 3;
// 0x444425..0x444476: los tres rand() del lugar del polvo.
constexpr int DustPlaceRandoms = 3;
// 0x444693: con 1 en +8 del jinete, los doce 0x60f.
constexpr std::uint8_t RiderWithOrbits = 1;
// 0x5ffe30: el alpha con que 0x5ffdf0 todavía dibuja.
constexpr float DrawnAlpha = 0.01f;

bool InChaosCastle(std::int32_t map)
{
    return map >= FirstChaosCastleMap && map <= LastChaosCastleMap;
}

bool InKalima(std::int32_t map)
{
    return (map >= FirstKalimaMap && map <= LastKalimaMap) || map == SeventhKalimaMap;
}

// 0x4440b8..0x4440e9: la celda de la posición del jinete, por 0,01f y truncada, con el byte bajo de cada
// una (0x5f82a0), y el bit 0 de su atributo (0x7d0d348). El producto de dos float es exacto en double.
Core::Expected<bool, std::string> InTheRidersSafeCell(const WorldState& world, const ActorState& rider)
{
    const auto& attributes = world.Map().terrain.attributes;
    if (!attributes)
    {
        return Core::Unexpected(std::string("0x4440db: el paso del caballo lee el atributo de la celda del jinete "
                                            "(0x7d0d348) sin el mapa cargado"));
    }
    const std::optional<std::int32_t> y =
        World::OriginalFtolLow(static_cast<double>(rider.position.y) * CellsPerUnit); // 0x4440c1
    const std::optional<std::int32_t> x =
        World::OriginalFtolLow(static_cast<double>(rider.position.x) * CellsPerUnit); // 0x4440d0
    if (!x || !y)
    {
        return Core::Unexpected(std::string("0x4440c1: la posición del jinete por 0,01 no entra en el entero de "
                                            "0x69fd3c, y su continuación no está observada"));
    }
    const std::size_t cell =
        Data::Terrain::CellIndex(static_cast<std::uint8_t>(*x & CellMask), static_cast<std::uint8_t>(*y & CellMask));
    return (attributes->cells[cell] & SafeCellBit) != 0;
}

// 0x44416b..0x4444ef con la acción 0x4b del jinete, después de 0x4ea030(1): el polvo.
Core::Expected<void, std::string> RaiseTheDust(const ActorState& rider, std::int32_t map,
                                               Render::Models::CrtRandom& random)
{
    if (map == IcarusMap)
    {
        return Core::Unexpected(std::string("0x444196: en el mapa 10 el galope del caballo saca su polvo con la pose "
                                            "del dibujo anterior (0x4ec110) y 0x518cf0(0x6eb), que MuRet no tiene "
                                            "en el juego"));
    }
    if (random.Next() % DustOneIn != 0)
    {
        return {};
    }
    // 0x44440a..0x444412: sin jinete, nada; 0x443fa0 siempre le da uno.
    if (InKalima(map) || (rider.flags78 & RiderWithoutDust) != 0)
    {
        return {};
    }
    for (int i = 0; i < DustPlaceRandoms; ++i)
    {
        static_cast<void>(random.Next());
    }
    return Core::Unexpected(std::format("0x4444a6: el polvo del caballo, 0x53fe70 con el {} en el mapa {}, va a las "
                                        "partículas del juego (0x743a198), que MuRet no tiene",
                                        map == 2 ? "0x5f2" : "0x5f3", map));
}

// 0x44453f..0x4445de: el aliento, con la acción del caballo distinta de 1 y sin el 0x4000 del jinete.
Core::Expected<void, std::string> Breathe(const HelperState& helper, const ActorState& rider,
                                          Render::Models::CrtRandom& random)
{
    if (helper.horse.animation.action == DustHorseAction || (rider.flags78 & RiderWithoutDust) != 0)
    {
        return {};
    }
    if (random.Next() % BreathOneIn != 0)
    {
        return {};
    }
    return Core::Unexpected(std::string("0x44459d: el aliento del caballo sale del hueso 27 de la pose del dibujo "
                                        "anterior (0x4ec110) a las partículas del juego (0x53fe70), y MuRet no "
                                        "tiene ni las partículas del juego ni el dibujo del caballo (0x637508)"));
}

// 0x444010(lugar, 0) en la escena 5 con un lugar vivo.
Core::Expected<void, std::string> StepHelper(HelperState& helper, const HelperStepContext& context)
{
    WorldState& world = context.world;
    ActorState& rider = world.Actors()[helper.rider];
    // 0x44402a..0x444047: en la escena 5, con el jinete muerto o sin el 1 en +0x88.
    if (!rider.alive || rider.byte88 != RiderShown)
    {
        helper.alive = false;
        return {};
    }
    if (helper.type != DarkHorseModel)
    {
        return Core::Unexpected(std::format("0x444083: el paso del ayudante {:#x} no está portado", helper.type));
    }
    if (context.horseSkeleton == nullptr)
    {
        return Core::Unexpected(std::string("0x444010: el caballo (0xbf) sin Skill\\DarkHorse.bmd (0x615700)"));
    }
    const Render::Models::Skeleton& skeleton = *context.horseSkeleton;
    FadeInDarkHorse(helper.horse); // 0x444054
    const auto safe = InTheRidersSafeCell(world, rider);
    if (!safe.HasValue())
    {
        return Core::Unexpected(safe.Error());
    }
    if (safe.Value())
    {
        // 0x4440f2..0x4440f8: al avance con la acción y la velocidad que tenía.
        helper.horse.alpha = HiddenAlpha;
        AdvanceDarkHorse(helper.horse, skeleton);
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
    const std::int32_t map = world.Runtime().map;
    if (riderAction == GallopingRiderAction)
    {
        if (auto dust = RaiseTheDust(rider, map, context.random); !dust.HasValue())
        {
            return dust;
        }
    }
    if (auto breath = Breathe(helper, rider, context.random); !breath.HasValue())
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
    // 0x445006..0x445039: +0xa0 del modelo con la acción, que lo portado no lee, y el avance.
    AdvanceDarkHorse(helper.horse, skeleton);
    return {};
}

// 0x445260(lugar, 0) en la escena 5 con un lugar vivo.
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
    if (helper.type != DarkHorseModel)
    {
        return Core::Unexpected(std::format("0x445260: el dibujo del ayudante {:#x} no está portado", helper.type));
    }
    helper.scale = HorseScale; // 0x445309
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
                std::string("0x60033c: con +0x78 & 0x4000 del jinete, 0x445310 dibuja el caballo con "
                            "10, que 0x6002e0 lleva por otro camino que MuRet no tiene"));
        }
        const Render::Models::ObjectTransform place{.position = helper.position, .angles = helper.angles};
        auto drawn = DrawnDarkHorse(helper.horse, context.models.SkeletonOf(DarkHorseModel), place, helper.scale,
                                    context.stepAlpha);
        if (context.onPose)
            if (const auto published = context.onPose(drawn); !published.HasValue())
                return published;
        if (context.posedMeshes)
            drawn.retainedMeshes = context.posedMeshes();
        DrawDarkHorse(context.batch, context.models, drawn, runtime.map, context.frameClock);
    }
    // 0x445336..0x445358: rand() % 30 + 70, la luz que solo usa el 0x1c12.
    static_cast<void>(context.random.Next());
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
        if (type == RiderlessHelper)
        {
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
                                      context.posedMeshes};
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
