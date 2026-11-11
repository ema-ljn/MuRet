#include "Scenes/WorldObjectsDrawing.h"

#include "Scenes/ActorState.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/AidaObjectServices.h"
#include "Scenes/EventMapObjectFrame.h"
#include "Scenes/NoriaObjectServices.h"
#include "Scenes/ObjectFrameSprites.h"
#include "Scenes/IcarusObjectFrame.h"
#include "Scenes/TarkanObjectFrame.h"
#include "Scenes/KalimaObjectServices.h"
#include "Scenes/LandOfTrialsObjectServices.h"
#include "World/ObjectModelStepState.h"
#include "Scenes/GameParticles.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/ModelBatch.h"
#include "Render/ModelBatchParticleBackend.h"
#include "Render/ModelBatchParticleState.h"
#include "Render/ModelVertex.h"
#include "Render/SpriteVertex.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/ModelPoseCallbacks.h"
#include "Scenes/ParticleDrawServices.h"
#include "World/Leaves.h"
#include "World/ChaosCastle.h"
#include "World/KalimaWorld.h"
#include "World/ModelEffects.h"
#include "World/GroundEffectDrawing.h"
#include "World/SphereFrustum.h"
#include "World/ParticleDrawing.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "Render/Billboard.h"
#include "Render/Models/OriginalRotation.h"
#include "World/TerrainCulling.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <format>
#include <functional>
#include <optional>
#include <ratio>
#include <span>
#include <string>
#include <vector>

namespace Client::Scenes
{
namespace
{
// 0x6046a0: el margen del sector (−180) y el paso de la posición a celdas.
constexpr double SectorMargin = -180.0;
constexpr double CellsPerUnit = static_cast<double>(0.01f);
constexpr int CellsPerSector = 16;
constexpr double SectorCenter = 8.0;
constexpr std::uint8_t LoginCloudMap = 55;
constexpr std::int16_t LoginCloudMarker = 0x54;
constexpr int LoginCloudChance = 5;

bool SectorPasses(const World::TerrainPolygon& polygon, int sectorX, int sectorY)
{
    return World::PolygonContains(polygon, (sectorX * CellsPerSector) + SectorCenter,
                                  (sectorY * CellsPerSector) + SectorCenter, SectorMargin);
}

// 0x6046a0 pasa la posición por 0,01f (0x6b88d4) guardada en float y el margen individual (+0xdc), con
// `extra` sumado en la x87 y guardado en float (0x604744..0x60474e).
bool ObjectPasses(const World::TerrainPolygon& polygon, const World::WorldObject& object, float extra)
{
    const auto cells = [](float coordinate)
    { return static_cast<double>(static_cast<float>(static_cast<double>(coordinate) * CellsPerUnit)); };
    return World::PolygonContains(polygon, cells(object.transform.position.x), cells(object.transform.position.y),
                                  static_cast<double>(extra + object.cullingMargin));
}

DrawnObject Drawn(const World::WorldObject& object, const World::ObjectDraw& draw,
                  const Render::Models::Skeleton* skeleton, float alpha)
{
    DrawnObject drawn;
    drawn.model = static_cast<std::uint16_t>(object.type);
    drawn.animation = object.animation;
    if (skeleton != nullptr && object.animation.action < skeleton->actions.size())
    {
        drawn.animation = Render::Models::Interpolated(object.animation, skeleton->actions[object.animation.action],
                                                       object.speed, alpha);
    }
    drawn.transform = object.transform;
    drawn.state = draw.state;
    drawn.color = draw.color;
    drawn.alpha = draw.alpha;
    drawn.vertexLight = draw.vertexLight;
    return drawn;
}
// 0x6046a0 con un objeto: su sector pasa el trapecio y él también (+0x16c, que guarda el resultado; los de
// un sector que no pasa quedan sin ver, como los apaga 0x606560).
// 0x6046b2..0x6046c3: el margen de más de cada objeto, −10 en Icarus (el mapa 10) y 0 en los demás.
float ExtraObjectMargin(std::uint8_t map)
{
    constexpr std::uint8_t IcarusMap = 10;
    constexpr float IcarusMargin = -10.0f;
    return map == IcarusMap ? IcarusMargin : 0.0f;
}

bool SeenInTheDraw(const World::TerrainPolygon& polygon, const World::WorldObject& object, float extraMargin = 0.0f)
{
    return SectorPasses(polygon, World::SectorX(object), World::SectorY(object)) &&
           ObjectPasses(polygon, object, extraMargin);
}

// 0x5ffce0 con la luz del terreno en su lugar (0x5f99e0), o 0 sin ella, y lo que decide World::DrawOf.
World::ObjectDraw ObjectDrawAt(const World::WorldObject& object, const World::TerrainLighting* lighting,
                               std::uint8_t map)
{
    const glm::vec3 light = lighting != nullptr
                                ? World::LightAt(*lighting, object.transform.position.x, object.transform.position.y)
                                : glm::vec3(0.0f);
    return World::DrawOf(object, light, map);
}

// 0x602e90 con `draw`: el modelo posado como 0x5ffdf0 y dibujado como 0x6002e0 sin casos por tipo, con su
// animación `alpha` de un paso más. Devuelve lo que se posó.
DrawnObject DrawTheObject(Render::ModelBatch& batch, const IModelLibrary& models, const World::WorldObject& object,
                          const World::ObjectDraw& draw, Core::Duration frameClock, float alpha,
                          const World::ObjectModelStepState* modelState = nullptr,
                          const FaunaDrawCarry* retainedPose = nullptr)
{
    const Render::Models::Skeleton* skeleton = models.SkeletonOf(static_cast<std::uint16_t>(object.type));
    DrawnObject drawn = Drawn(object, draw, skeleton, alpha);
    if (modelState != nullptr)
    {
        const auto found = modelState->unlitMeshes.find(object.type);
        if (found != modelState->unlitMeshes.end())
            drawn.unlitMesh = found->second;
    }
    if (retainedPose != nullptr)
        drawn.retainedMeshes = retainedPose->meshes;
    switch (draw.kind)
    {
    case World::ObjectDrawKind::Hidden:
        break;
    case World::ObjectDrawKind::Body:
        DrawObject(batch, models, drawn, frameClock);
        break;
    case World::ObjectDrawKind::SubtractiveMesh:
        DrawSubtractiveMesh(batch, models, drawn, frameClock);
        break;
    case World::ObjectDrawKind::Waterspout:
        DrawWaterspout(batch, models, drawn, frameClock);
        break;
    case World::ObjectDrawKind::TarkanChrome:
        DrawTarkanChrome(batch, models, drawn, frameClock);
        break;
    }
    return drawn;
}

// La pose se toma una vez por dibujo; la emisión sigue el orden frame → objetos.
struct SeenObjectEffects
{
    World::WorldObject* object = nullptr;
    std::size_t index = 0;
    std::optional<std::array<glm::vec3, World::StreetLightGlowBones.size()>> glowPositions;
    std::optional<int> lastRandom;
};

void CreateObjectCloud(const World::WorldObject& object, std::uint8_t map, float clock,
                       const ObjectFrameEffects& effects)
{
    // 0x6044bc: el marcador oculto del mapa 55, después del rand común, sin guard de escena.
    if (map != LoginCloudMap || object.type != LoginCloudMarker || effects.random->Next() % LoginCloudChance != 0 ||
        effects.particles == nullptr)
    {
        return;
    }
    // El caller original pasa luz indeterminada; el subtipo 10 la sobrescribe con RGB cero.
    (void)effects.particles->Create(World::CloudParticle, object.transform.position, object.transform.angles,
                                    glm::vec3(0.0f), World::LoginCloudSubtype, object.transform.scale, clock,
                                    *effects.random);
}

void AdvanceObjectEffects(std::span<SeenObjectEffects> seen, std::uint8_t map, Core::Duration frameClock,
                          const ObjectFrameEffects& effects)
{
    const auto clock = static_cast<float>(std::chrono::duration<double, std::milli>(frameClock).count());
    for (int frame = 0; frame < effects.frames; ++frame)
    {
        for (SeenObjectEffects& entry : seen)
        {
            entry.lastRandom = effects.random->Next();
            CreateObjectCloud(*entry.object, map, clock, effects);
        }
    }
}

void DrawObjectGlows(std::span<const SeenObjectEffects> seen, const ObjectFrameEffects& effects)
{
    if (effects.frames > 0)
    {
        // Solo se puede repetir una muestra del último frame original, no de una aparición anterior.
        for (std::optional<int>& random : effects.lastRandoms)
        {
            random.reset();
        }
    }
    for (const SeenObjectEffects& entry : seen)
    {
        if (entry.index < effects.lastRandoms.size())
        {
            effects.lastRandoms[entry.index] = entry.lastRandom;
        }
        if (!entry.glowPositions || !entry.lastRandom)
        {
            continue;
        }
        const glm::vec3 glow = World::StreetLightGlowLight(*entry.lastRandom);
        for (const glm::vec3& position : *entry.glowPositions)
        {
            (void)effects.sprites->Create(World::StreetLightGlowTexture, position, 1.0f, glow, 0.0f, 0);
        }
    }
}
} // namespace

Core::Expected<void, std::string> StepGameWorldObjects(std::span<World::WorldObject> objects, std::uint8_t map,
                                                       const IModelLibrary& models, Render::Models::CrtRandom& random,
                                                       const World::ObjectStepGlobals& globals,
                                                       const AfterObjectStep& after)
{
    for (const std::size_t index : World::DrawOrder(objects))
    {
        World::WorldObject& object = objects[index];
        if (!object.alive || !object.seen)
        {
            continue;
        }
        if (auto stepped = World::StepWorldObjectChecked(
                object, map, models.SkeletonOf(static_cast<std::uint16_t>(object.type)), random, globals);
            !stepped.HasValue())
        {
            return stepped;
        }
        // 0x6065b7: 0x6063d0(objeto, &selector, &0x7d365a0) después de 0x604ab0.
        if (after)
        {
            if (auto stepped = after(object); !stepped.HasValue())
                return stepped;
        }
    }
    return {};
}

void StepWorldObjects(std::span<World::WorldObject> objects, std::uint8_t map, const IModelLibrary& models,
                      const SpecialObjectStep& special)
{
    for (const std::size_t index : World::DrawOrder(objects))
    {
        World::WorldObject& object = objects[index];
        if (!object.seen || (special && special(object)))
        {
            continue;
        }
        World::StepWorldObject(object, map, models.SkeletonOf(static_cast<std::uint16_t>(object.type)));
    }
}

void DrawWorldObjects(Render::ModelBatch& batch, const IModelLibrary& models, std::span<World::WorldObject> objects,
                      const World::TerrainPolygon& polygon, const World::TerrainLighting* lighting, std::uint8_t map,
                      Core::Duration frameClock, float alpha, const ObjectFrameEffects& effects)
{
    // El buffer global de la pose, en el orden en que lo pisan: lo de antes de los objetos y cada objeto
    // que 0x5ffdf0 posa. Solo hace falta con los sprites del farol.
    const bool glows =
        effects.random != nullptr && effects.sprites != nullptr && effects.scene == World::ObjectScene::Login;
    std::vector<PosedInTheBuffer> posed;
    std::vector<SeenObjectEffects> seen;
    if (glows)
    {
        posed.assign(effects.posedBefore.begin(), effects.posedBefore.end());
    }
    for (const std::size_t index : World::DrawOrder(objects))
    {
        World::WorldObject& object = objects[index];
        object.seen = SeenInTheDraw(polygon, object, ExtraObjectMargin(map));
        if (!object.seen)
        {
            continue;
        }
        const World::ObjectDraw draw = ObjectDrawAt(object, lighting, map);
        // 5ffdf0 posa antes del gate hiddenMesh=-2 de600328; no confundir pose con geometría.
        if (effects.onPose && (effects.frames > 0 || effects.publishProjectedPose) && draw.posed)
        {
            const auto poseObject =
                Drawn(object, draw, models.SkeletonOf(static_cast<std::uint16_t>(object.type)), alpha);
            if (poseObject.alpha >= .01f)
                if (auto published = effects.onPose(poseObject); !published.HasValue())
                    return;
        }
        const auto* retained = (effects.frames > 0 || effects.publishProjectedPose) ? effects.retainedPose : nullptr;
        const DrawnObject drawn = DrawTheObject(batch, models, object, draw, frameClock, alpha, nullptr, retained);
        if (glows && draw.posed)
        {
            posed.push_back(PosedInTheBuffer{&models, drawn});
        }
        if (effects.random == nullptr)
        {
            continue;
        }
        SeenObjectEffects entry{.object = &object,
                                .index = index,
                                .glowPositions = std::nullopt,
                                .lastRandom =
                                    index < effects.lastRandoms.size() ? effects.lastRandoms[index] : std::nullopt};
        if (glows && object.type == World::StreetLight)
        {
            entry.glowPositions.emplace();
            for (std::size_t k = 0; k < World::StreetLightGlowBones.size(); ++k)
            {
                const auto bone = World::StreetLightGlowBones[k];
                if (retained != nullptr && bone < retained->bones.size())
                    (*entry.glowPositions)[k] = Render::Models::OriginalVectorTransform(
                        {}, Render::Models::OriginalMatrixOf(retained->bones[bone]));
                else
                    (*entry.glowPositions)[k] = LastPosedBonePoint(posed, bone, glm::vec3(0)).value_or(glm::vec3(0));
            }
        }
        seen.push_back(entry);
    }
    AdvanceObjectEffects(seen, map, frameClock, effects);
    DrawObjectGlows(seen, effects);
}

namespace
{
constexpr std::uint8_t LorenciaMap = 0;
constexpr std::uint8_t DeviasMap = 2;
constexpr std::uint8_t NoriaMap = 3;
constexpr std::uint8_t LostTowerMap = 4;
constexpr std::uint8_t ArenaMap = 6;
constexpr std::uint8_t AtlansMap = 7;
constexpr std::uint8_t TarkanMap = 8;
constexpr std::uint8_t DevilSquareMap = 9;
constexpr std::uint8_t IcarusMap = 10;
constexpr std::uint8_t FirstBloodCastleMap = 11;
constexpr std::uint8_t LastBloodCastleMap = 17;
constexpr std::uint8_t LandOfTrialsMap = 31;
constexpr std::uint8_t AidaMap = 33;
constexpr std::uint8_t SelectionWorldMap = 54;
// 0x60479c: el 0x64 de Devias se dibuja según el nivel del registro propio (0x604944..0x604957, jl con
// signo): desde 0x35 con la clase del héroe (+0x1d4 & 7) en 3 o 4, y desde 0x50 con las demás.
constexpr std::int16_t DeviasLeveledObject = 0x64;
// 0x600d04..0x600d0e: los dos que llevan sombra en 11 a 17.
constexpr std::int16_t BloodCastleShadowedFirst = 0x1c;
constexpr std::int16_t BloodCastleShadowedSecond = 0x1d;
constexpr std::uint8_t ClassMask = 7;
constexpr std::uint8_t FirstLowLevelClass = 3;
constexpr std::uint8_t SecondLowLevelClass = 4;
constexpr int LowLevelThreshold = 0x35;
constexpr int LevelThreshold = 0x50;

// 0x6047a7..0x604a36: la clase del héroe, antes del acceso; el nivel, con el registro propio abierto
// (0x6047cb) y cerrado (0x604a1a: con la cuenta en 0, 0x40b4d0 lo vuelve a cifrar con sus 3.848 rand()).
// En un dibujo intermedio de MuRet no hay frame del original y no se abre.
bool OwnLevelAllows(const GameObjectPass& pass)
{
    const std::uint8_t heroClass = pass.heroClass & ClassMask;
    const int threshold =
        heroClass == FirstLowLevelClass || heroClass == SecondLowLevelClass ? LowLevelThreshold : LevelThreshold;
    if (pass.originalFrame && pass.random != nullptr && pass.ownRecordDepth != nullptr)
    {
        const Render::Models::LogicalAccess access(*pass.ownRecordDepth, *pass.random,
                                                   Render::Models::OwnRecordCloseRandoms);
        return static_cast<int>(pass.ownLevel) >= threshold;
    }
    return static_cast<int>(pass.ownLevel) >= threshold;
}

// 0x6002e0 con el mapa 4 (0x600b80..0x601067 en el pseudocódigo, líneas del mapa 4): el 0x17, el 0x13, el
// 0x14, el 3 y el 4 sacan un rand() que se descarta (en el frame del original, que es cuando corre 0x6002e0
// y su rand); los cuatro últimos, antes del cuerpo, un RenderBody(2) con Chrome01 (0x5c0) forzado, la luz
// del modelo en (1; 0,2; 0,1) y el model+0x88 en 2 (0x13 y 0x14) o en 1 (3 y 4): esa malla sin luz y con la
// U y la V del objeto. Después +0x88 vuelve a 0xff, que en el 4 es lo que esos tipos ya tenían.
void DrawLostTowerChrome(Render::ModelBatch& batch, const IModelLibrary& models, const World::WorldObject& object,
                         const World::ObjectDraw& draw, Core::Duration frameClock, const GameObjectPass& pass,
                         const FaunaDrawCarry* retained)
{
    const std::int16_t type = object.type;
    const bool chrome = type == 0x13 || type == 0x14 || type == 3 || type == 4;
    if (!chrome && type != 0x17)
        return;
    if (pass.originalFrame)
        static_cast<void>(pass.random->Next());
    if (!chrome)
        return;
    constexpr glm::vec3 ChromeLight{1.0f, std::bit_cast<float>(0x3e4ccccdu), std::bit_cast<float>(0x3dcccccdu)};
    DrawnObject chromed = Drawn(object, draw, models.SkeletonOf(static_cast<std::uint16_t>(type)), 0.0f);
    if (retained != nullptr)
        chromed.retainedMeshes = retained->meshes;
    chromed.color = ChromeLight;
    chromed.unlitMesh = type == 0x13 || type == 0x14 ? std::int16_t{2} : std::int16_t{1};
    DrawObjectWithTexture(batch, models, chromed, frameClock, ChromeTextureSlot);
}

// 0x602ed0 después del rand() común (0x602ef4): los casos por tipo de los mapas 0 (0x602ffb), 2
// (0x6031e5), 4 (0x603799), 8 (0x603a3e), 9 (0x603dd7) y 11 a 17 (0x604152); el 1 va a la cadena de 0x6044ed,
// que no tiene nada para él, y los Chaos Castle (18 a 23) a su primer eslabón, 0x4059d0. El 0x69 crea las
// gotas con el pool compartido. `posed`: si 0x602e90 llamó a 0x6002e0, cuyo marco queda en la pila que leen
// los sprites del 0x38 del 0 y del 9 del 4, y si 0x5ffdf0 dejó el +0xa8 del modelo del 0x67 del 2.
Core::Expected<void, std::string> FrameCaseOf(World::WorldObject& object, const GameObjectPass& pass,
                                              const IModelLibrary& models, const DrawnObject& drawn, int commonRandom,
                                              bool posed)
{
    const std::uint8_t map = pass.map;
    if (World::IsKalimaMap(map))
        return StepKalimaObjectFrame(object, pass);
    if (map == NoriaMap)
        return StepNoriaObjectFrame(object, commonRandom, pass);
    if (map == AidaMap || map == SelectionWorldMap)
        return StepAidaObjectFrame(object, pass);
    if (map == LandOfTrialsMap)
        return StepLandOfTrialsObjectFrame(object, pass);
    if (map == LostTowerMap)
        return StepLostTowerObjectFrame(object, commonRandom, pass, posed);
    if (map == ArenaMap)
        return StepArenaObjectFrame(object, commonRandom, pass, posed);
    if (map == TarkanMap)
        return StepTarkanObjectFrame(object, pass);
    if (map == IcarusMap)
        return StepIcarusObjectFrame(object, pass);
    if (map == DevilSquareMap)
        return StepDevilSquareObjectFrame(object, pass);
    if (map >= FirstBloodCastleMap && map <= LastBloodCastleMap)
        return StepBloodCastleObjectFrame(object, pass);
    if (World::IsChaosCastleMap(map))
        return StepChaosCastleObjectFrame(object, pass);
    if (map == LorenciaMap && object.type == 0x38)
        return StepMerchantAnimalFrame(object, commonRandom, pass, posed);
    if (map == LorenciaMap && object.type == World::Waterspout)
    {
        if (pass.particles != nullptr)
        {
            return StepGameWaterspoutFrame(object, *pass.particles, *pass.random, pass.worldClock, models, drawn,
                                           pass.retainedPose);
        }
        return World::StepTheWaterspoutFrame(object, *pass.random, pass.worldClock);
    }
    if (map == DeviasMap)
    {
        return StepDeviasObjectFrame(object, commonRandom, pass,
                                     models.SkeletonOf(static_cast<std::uint16_t>(object.type)), posed);
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawGameWorldObjects(Render::ModelBatch& batch, const IModelLibrary& models,
                                                       std::span<World::WorldObject> objects,
                                                       const World::TerrainPolygon& polygon,
                                                       const World::TerrainLighting* lighting,
                                                       Core::Duration frameClock, const GameObjectPass& pass)
{
    const bool bloodCastle = pass.map >= FirstBloodCastleMap && pass.map <= LastBloodCastleMap;
    if (pass.map > LostTowerMap && pass.map != ArenaMap && pass.map != AtlansMap && pass.map != TarkanMap &&
        pass.map != DevilSquareMap && pass.map != IcarusMap && pass.map != LandOfTrialsMap && pass.map != AidaMap &&
        pass.map != SelectionWorldMap && !World::IsKalimaMap(pass.map) && !bloodCastle &&
        !World::IsChaosCastleMap(pass.map))
    {
        return Core::Unexpected(std::format("0x6046a0: los casos por tipo del mapa {} (0x6002e0 y 0x602ed0) no están "
                                            "leídos",
                                            static_cast<int>(pass.map)));
    }
    if (pass.originalFrame && pass.random == nullptr)
    {
        return Core::Unexpected(std::string("0x602ef4: el frame del original saca rand() por objeto, y no lo tiene"));
    }
    for (const std::size_t index : World::DrawOrder(objects))
    {
        World::WorldObject& object = objects[index];
        // 0x60473b: el que no está vivo no se mira.
        if (!object.alive)
        {
            continue;
        }
        object.seen = SeenInTheDraw(polygon, object, ExtraObjectMargin(pass.map));
        if (!object.seen)
        {
            continue;
        }
        if (pass.map == DeviasMap && object.type == DeviasLeveledObject && !OwnLevelAllows(pass))
        {
            continue;
        }
        // 0x5ffdff..0x5ffe24: en los mapas 18 a 23, con +0x1a1 del héroe (solo lo escriben los paquetes), el alpha
        // del objeto baja 0,15f y queda guardado, una vez por frame del original.
        if (pass.originalFrame && pass.hero != nullptr)
            object.alpha = World::ChaosCastlePoseAlpha(pass.map, pass.hero->byte1a1, object.alpha);
        // 0x602e90: 0x5ffdf0 y, si lo posó, 0x6002e0 (en el 0, el 0x69 con sus cuatro 0x4ecc30).
        World::ObjectDraw draw = ObjectDrawAt(object, lighting, pass.map);
        const auto poseObject = Drawn(object, draw, models.SkeletonOf(static_cast<std::uint16_t>(object.type)), 0);
        // Los marcadores ocultos también pisan la pose global antes de omitir la geometría.
        if ((pass.originalFrame || pass.publishProjectedPose) && pass.onPose && draw.posed &&
            poseObject.alpha >= 0.01f && models.Find(poseObject.model) != nullptr &&
            models.SkeletonOf(poseObject.model) != nullptr)
        {
            if (auto published = pass.onPose(poseObject); !published.HasValue())
                return published;
        }
        const auto* retained = (pass.originalFrame || pass.publishProjectedPose) ? pass.retainedPose : nullptr;
        if (World::IsKalimaMap(pass.map) && draw.kind != World::ObjectDrawKind::Hidden)
        {
            // 438fe0 tiene casos globales fuera del dominio público43948d: no usar el cuerpo
            // como fallback para un tipo que todavía necesita su propio handler.
            constexpr std::int16_t FirstKalimaBody = 0;
            constexpr std::int16_t EndKalimaBody = 160;
            if (object.type < FirstKalimaBody || object.type >= EndKalimaBody)
                return Core::Unexpected(std::string("0x438fe0: tipo Kalima fuera del cuerpo público0..159"));
            if (models.Find(poseObject.model) == nullptr || models.SkeletonOf(poseObject.model) == nullptr)
                return Core::Unexpected(std::string("0x5ffdf0: falta el modelo o el esqueleto de Kalima"));
        }
        if (pass.map == LostTowerMap && draw.kind != World::ObjectDrawKind::Hidden)
            DrawLostTowerChrome(batch, models, object, draw, frameClock, pass, retained);
        // 0x4403b0, la cadena de 0x6002e0 en el 31: sin rand() ni estado, en cada dibujo de MuRet.
        if (pass.map == LandOfTrialsMap && draw.kind == World::ObjectDrawKind::Body)
        {
            const auto body = LandOfTrialsBody(object, draw.color, pass.worldClock);
            if (!body.HasValue())
                return Core::Unexpected(body.Error());
            if (body.Value())
            {
                draw.color = body.Value()->color;
                // Q-C317: el RenderBody propio del 0x34 y, después, el de 0x6002e0 con el mismo color.
                if (body.Value()->twice)
                    static_cast<void>(
                        DrawTheObject(batch, models, object, draw, frameClock, 0.0f, pass.modelState, retained));
            }
        }
        const DrawnObject drawn =
            DrawTheObject(batch, models, object, draw, frameClock, 0.0f, pass.modelState, retained);
        // 0x600cf2..0x600db0: el 0x1c y el 0x1d de 11 a 17, después de su RenderBody, la sombra con la altura
        // del terreno; +0x58 queda en −1.
        if (bloodCastle && (object.type == BloodCastleShadowedFirst || object.type == BloodCastleShadowedSecond) &&
            draw.kind == World::ObjectDrawKind::Body)
        {
            if (!pass.terrainHeight)
                return Core::Unexpected(std::string("0x600d80: falta la altura del terreno (0x5f93c0) para la sombra "
                                                    "del objeto"));
            DrawBloodCastleShadow(batch, models, drawn,
                                  pass.terrainHeight(object.transform.position.x, object.transform.position.y),
                                  frameClock);
            object.state.hiddenMesh = Render::Models::NoHiddenMesh;
        }
        if (World::IsKalimaMap(pass.map) && draw.kind != World::ObjectDrawKind::Hidden)
        {
            if (auto tail = DrawKalimaObjectTail(batch, models, object, drawn, frameClock, pass); !tail.HasValue())
                return tail;
        }
        // 0x425260 (desde 0x602ed0): el 0x29 y el 0x41/0x42 de Aida repiten su malla 0 con 0x4ecc30. Sin rand()
        // ni estado, van en cada dibujo de MuRet; el orden con el rand() común no cambia nada.
        if (pass.map == AidaMap || pass.map == SelectionWorldMap)
        {
            if (auto meshPass = DrawAidaMeshPass(batch, models, drawn, object.type, pass.worldClock, frameClock);
                !meshPass.HasValue())
                return meshPass;
        }
        // 0x604a4d: 0x602ed0, una vez por frame del original.
        if (!pass.originalFrame)
        {
            continue;
        }
        const int commonRandom = pass.random->Next();
        if (auto frameCase = FrameCaseOf(object, pass, models, drawn, commonRandom, draw.posed); !frameCase.HasValue())
        {
            return frameCase;
        }
    }
    return {};
}

Core::Expected<void, std::string> DrawGroundEffects(Render::ModelBatch& batch, std::span<World::ModelEffect> effects,
                                                    const World::GroundEffectResources& resources)
{
    for (std::size_t slot = 0; slot < effects.size(); ++slot)
    {
        World::ModelEffect& effect = effects[slot];
        // 0x52dc2d..0x52dc40: no recalcula la esfera, lee la historia de este lugar.
        if (!effect.alive || !effect.seen)
        {
            continue;
        }
        static_cast<void>(resources.random.Next()); // 0x52dc46, aun sin un caso de suelo.
        World::ApplyGroundEffectAdditive(batch);    // 0x620a90, antes de despachar por tipo.
        if (auto drawn = World::DrawGroundEffectCase(batch, effect, resources); !drawn.HasValue())
        {
            return Core::Unexpected(drawn.Error() + std::format(", en el efecto {} (el {:#x})", slot, effect.type));
        }
    }
    return {};
}

void StepModelEffects(std::span<World::ModelEffect> effects, const IModelLibrary& models)
{
    for (World::ModelEffect& effect : effects)
    {
        if (effect.alive)
        {
            World::StepModelEffect(effect, models.SkeletonOf(static_cast<std::uint16_t>(effect.type)));
        }
    }
}

void DrawModelEffects(Render::ModelBatch& batch, const IModelLibrary& models, std::span<World::ModelEffect> effects,
                      const World::SphereFrustum& frustum, const World::TerrainLighting* lighting,
                      Core::Duration frameClock, float alpha)
{
    // Sin callback no existe una salida de error: preserva la API legacy de7argumentos.
    static_cast<void>(DrawModelEffects(batch, models, effects, frustum, lighting, frameClock, alpha, {}, {}));
}

Core::Expected<void, std::string>
DrawModelEffects(Render::ModelBatch& batch, const IModelLibrary& models, std::span<World::ModelEffect> effects,
                 const World::SphereFrustum& frustum, const World::TerrainLighting* lighting, Core::Duration frameClock,
                 float alpha, const TailPoseObserver& onPose, const TailMeshProvider& posedMeshes)
{
    for (World::ModelEffect& effect : effects)
    {
        if (!effect.alive)
        {
            continue;
        }
        // 0x52d63c..0x52d655: publica +0x16c antes del tipo, el modelo o el alpha. El pase del suelo
        // del próximo frame lo lee tal cual. La esfera usa +0x10 sin la extrapolación de MuRet.
        constexpr float Radius = 400.0f;
        effect.seen = World::SphereVisible(frustum, effect.transform.position, Radius);
        if (!effect.seen)
        {
            continue;
        }
        // 0x52da84: estos tipos sólo tienen el pase del suelo; aun sin modelo publican seen. El 0x5ac de Icarus
        // (0x52daa1) no se dibuja: se ve por sus partículas 5ac/1.
        if (effect.type == 0x61d || effect.type == 0x61e || effect.type == 0x61f ||
            effect.type == World::IcarusFlareEffect)
        {
            continue;
        }
        const glm::vec3 terrain =
            lighting != nullptr ? World::LightAt(*lighting, effect.transform.position.x, effect.transform.position.y)
                                : glm::vec3(0.0f);
        DrawnObject drawn;
        drawn.model = static_cast<std::uint16_t>(effect.type);
        drawn.animation = effect.animation;
        drawn.transform = effect.transform;
        drawn.transform.position += (effect.transform.position - effect.previousPosition) * alpha;
        drawn.state = effect.objectState;
        // 0x5ffce0: con luz por vértice, el terreno más la luz; sin ella (la nube 0xbb), el terreno por 0,1f más
        // la luz, con la x sin redondear antes de la suma y la y y la z redondeadas (0x5ffda5..0x5ffde7).
        if (effect.vertexLight)
            drawn.color = terrain + effect.light;
        else
        {
            constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
            drawn.color.x = static_cast<float>(static_cast<double>(terrain.x) * Tenth + effect.light.x);
            for (int axis = 1; axis < 3; ++axis)
                drawn.color[axis] = static_cast<float>(
                    static_cast<double>(static_cast<float>(static_cast<double>(terrain[axis]) * Tenth)) +
                    effect.light[axis]);
        }
        drawn.alpha = effect.alpha;
        drawn.vertexLight = effect.vertexLight;
        if (onPose && !(drawn.alpha < 0.01f) && models.Find(drawn.model) != nullptr &&
            models.SkeletonOf(drawn.model) != nullptr)
        {
            if (auto published = onPose(drawn); !published.HasValue())
                return published;
            if (auto rendered = DrawPublishedObject(batch, models, drawn, frameClock, posedMeshes);
                !rendered.HasValue())
                return rendered;
        }
        else
            DrawObject(batch, models, drawn, frameClock);
    }
    return {};
}

namespace
{
// 0x549290 con la textura del atlas: el cuadro por su +4 (0x54941c), de 64 × 64, con un tercio de la
// textura (0x3ea8f5c3 y 0x3f28f5c3).
constexpr float AtlasSide = 64.0f;
constexpr float AtlasThird = 0.33f;
constexpr float AtlasTwoThirds = 0.66f;

glm::vec2 AtlasCell(std::int32_t cell)
{
    switch (cell)
    {
    case 1:
        return {AtlasThird, 0.0f};
    case 2:
        return {AtlasTwoThirds, 0.0f};
    case 3:
        return {0.0f, AtlasThird};
    case 4:
        return {AtlasThird, AtlasThird};
    case 5:
        return {AtlasTwoThirds, AtlasThird};
    case 6:
        return {0.0f, AtlasTwoThirds};
    case 7:
        return {AtlasThird, AtlasTwoThirds};
    default:
        return {0.0f, 0.0f};
    }
}

Render::ModelBlend SpriteBlend(const World::Sprite& sprite)
{
    if (sprite.texture == World::AtlasSprite)
    {
        return Render::ModelBlend::Alpha;
    }
    switch (sprite.blend)
    {
    case 1:
        return Render::ModelBlend::Subtractive;
    case 2:
        return Render::ModelBlend::Alpha;
    case 3:
        return Render::ModelBlend::InverseColorAdditive;
    default:
        return Render::ModelBlend::Additive;
    }
}
} // namespace

void DrawFrameSprites(Render::ModelBatch& batch, World::FrameSprites& sprites, const IModelLibrary& textures,
                      const Render::Models::OriginalMatrix& view, std::uint8_t mode)
{
    for (World::Sprite& sprite : sprites.Sprites())
    {
        // 0x54944e..0x54949e: mode1 retiene; mode2 apaga también la banda inferior, incluido NaN.
        if (mode == 1 && sprite.position.z > 350.0f)
            continue;
        if (mode == 2 && !(sprite.position.z > 300.0f))
        {
            sprite.alive = false;
            continue;
        }
        if (!sprite.alive)
        {
            continue;
        }
        World::StepSpriteAlpha(sprite);
        const Render::TextureSlot slot{sprite.texture};
        Render::Billboard billboard;
        // El tamaño: la escala por el alpha en la x87, por el ancho y el alto de la textura.
        const double size = static_cast<double>(sprite.scale) * static_cast<double>(sprite.alpha);
        if (sprite.texture == World::AtlasSprite)
        {
            billboard =
                Render::MakeBillboard(view, sprite.position, AtlasSide, AtlasSide, sprite.light, sprite.rotation,
                                      AtlasCell(sprite.blend), glm::vec2(AtlasThird), sprite.texture, 3);
        }
        else
        {
            const glm::vec2 texture = textures.TextureSize(slot).value_or(glm::vec2(0.0f));
            const auto width = static_cast<float>(size * static_cast<double>(texture.x));
            const auto height = static_cast<float>(size * static_cast<double>(texture.y));
            billboard = Render::MakeBillboard(view, sprite.position, width, height, sprite.light, sprite.rotation,
                                              glm::vec2(0.0f), glm::vec2(1.0f), sprite.texture, 3);
        }
        batch.SetState(Render::ModelDrawState{.texture = slot, .blend = SpriteBlend(sprite)});
        const auto vertex = [&billboard](std::size_t k)
        { return Render::ModelVertex{billboard.corners[k].position, billboard.corners[k].uv, billboard.color}; };
        batch.Triangle({vertex(0), vertex(1), vertex(2)});
        batch.Triangle({vertex(0), vertex(2), vertex(3)});
        // 0x549440(0) lo apaga después de dibujarlo.
        if (mode == 0 || mode == 2)
            sprite.alive = false;
    }
}

Core::Expected<void, World::ParticleDrawingFailure>
DrawParticles(Render::ModelBatch& batch, World::Particles& particles, const IModelLibrary& textures,
              const Render::Models::OriginalMatrix& view, float worldClock, std::uint8_t mode)
{
    // Un adaptador por pase: su primer servicio importa el estado que dejó el lote (los sprites), sin
    // suponer el de un frame anterior.
    Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(textures),
                                              Render::ModelBatchParticleState::ForModelPass());
    ParticleDrawServices services(std::cref(textures), std::ref(backend), std::cref(view));
    // 0x548550(0): el modo 0 no filtra por altura.
    return World::DrawParticlesChecked(particles, mode, std::cref(worldClock), services);
}

void DrawLeaves(Render::ModelBatch& batch, const World::Leaves& leaves)
{
    const Render::Color white{1.0f, 1.0f, 1.0f, 1.0f};
    for (const World::Leaf& leaf : leaves.All())
    {
        if (!leaf.alive)
        {
            continue;
        }
        const World::LeafQuad quad = World::LeafQuadOf(leaf);
        batch.SetState(Render::ModelDrawState{.texture = Render::TextureSlot{static_cast<std::uint16_t>(leaf.type)},
                                              .blend = Render::ModelBlend::Additive});
        // glTranslatef y el vértice: la suma la hace GL, en float.
        const auto vertex = [&quad, &white](std::size_t k)
        { return Render::ModelVertex{quad.translation + quad.corners[k], quad.uv[k], white}; };
        batch.Triangle({vertex(0), vertex(1), vertex(2)});
        batch.Triangle({vertex(0), vertex(2), vertex(3)});
    }
}
} // namespace Client::Scenes
