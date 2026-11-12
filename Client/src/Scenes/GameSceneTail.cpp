#include "Scenes/GameScene.h"
#include "Scenes/GameItemRenderState.h"
#include "Scenes/GameItemSpecialDrawing.h"
#include "Scenes/ItemDrawResources.h"
#include "World/ITerrainAttributeQuerySource.h"
#include "World/TerrainAttributeQuery.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "World/KalimaWorld.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/Billboard.h"
#include "Render/Pass3D.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skinning.h"
#include "World/ModelEffects.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GameParticles.h"
#include "Scenes/GameProjection.h"
#include "Scenes/GameTailPasses.h"
#include "Scenes/HelperTable.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/WorldTerrain.h"
#include "World/Sprites.h"

#include <chrono>
#include <algorithm>
#include <cstdint>
#include <glm/ext/vector_float3.hpp>
#include <ratio>
#include <string>
#include <span>

namespace Client::Scenes
{
namespace
{
class TailAttributeQuery final : public World::ITerrainAttributeQuerySource
{
public:
    explicit TailAttributeQuery(const Data::Terrain::TerrainAttributes& attributes) : m_attributes(attributes) {}
    float ReadScale() override
    {
        return World::TerrainAttributeQueryScale;
    }
    const Data::Terrain::TerrainAttributes& Attributes() override
    {
        return m_attributes;
    }

private:
    const Data::Terrain::TerrainAttributes& m_attributes;
};
} // namespace

Core::Expected<void, std::string> GameScene::DrawTheBirds(SceneCanvas& canvas, const World::TerrainPolygon& polygon,
                                                          int frames)
{
    WorldState& world = *m_context.world;
    FaunaDrawContext context{world.Fauna(),
                             world.FaunaResources(),
                             world.Map().terrain,
                             FrameLighting(),
                             polygon,
                             FrameCloth(),
                             FrameSpritesForDraw(),
                             FramePoseCarry(),
                             world.Runtime().map,
                             world.Runtime().storm,
                             canvas.frameClock,
                             &FrameParticles(),
                             frames > 0,
                             CameraState().fixedHeight,
                             true};
    return DrawGameBirds(canvas.models, context);
}

void GameScene::FillTheItemContexts(ItemSpecialDrawContext& special, ItemDrawContext& item, Core::Duration frameClock,
                                    int frames)
{
    FillItemDrawContexts(special, item,
                         ItemDrawResources{.frameClock = frameClock,
                                           .random = FrameCloth().random,
                                           .itemRender = FrameItemRender(),
                                           .sprites = FrameSpritesForDraw(),
                                           .poseCarry = FramePoseCarry(),
                                           .modelState = FrameModelState(),
                                           .joints = FrameTail().joints,
                                           .effects = FrameEffects(),
                                           .map = m_context.world->Runtime().map,
                                           .originalFrame = frames > 0});
}

Core::Expected<void, std::string> GameScene::DrawTheTail(SceneCanvas& canvas, const Render::Pass3D& pass,
                                                         const World::TerrainPolygon& polygon, int frames)
{
    WorldState& world = *m_context.world;
    WorldRuntime& runtime = world.Runtime();
    GameDrawTailState& tail = FrameTail();
    auto& itemState = FrameItemRender();
    const float clock = static_cast<float>(std::chrono::duration<double, std::milli>(canvas.frameClock).count());
    const auto projection = MakeGameProjection(pass, m_drawnCamera.fieldOfView);
    if (!projection.HasValue())
        return Core::Unexpected(projection.Error());
    ItemSpecialDrawContext special;
    ItemDrawContext itemContext;
    FillTheItemContexts(special, itemContext, canvas.frameClock, frames);
    if (std::ranges::any_of(tail.floorItems, [](const FloorItemState& item) { return item.alive; }))
    {
        if (m_models == nullptr)
            return Core::Unexpected(std::string("0x608a98: faltan los modelos globales del piso"));
        FloorItemDrawContext floor{canvas.models, *m_models, m_frustum};
        floor.lighting = &FrameLighting();
        floor.frameClock = canvas.frameClock;
        floor.map = runtime.map;
        floor.project = [&](const glm::vec3& point) { return ProjectGamePoint(projection.Value(), point); };
        floor.onPose = [&](const DrawnObject& object)
        { return PublishFaunaFloorPose(FramePoseCarry(), *m_models, object); };
        floor.posedMeshes = [&]() { return std::span<const Render::Models::SkinnedMesh>(FramePoseCarry().meshes); };
        floor.onSkin = [&](const DrawnObject& object)
        { return TransformFaunaPose(FramePoseCarry(), *m_models, object); };
        if (itemState.initialized)
            floor.randomTable = itemState.floorAngles;
        floor.worldClock = clock;
        floor.waterHeight = [&](float x, float y) -> Core::Expected<float, std::string>
        {
            const auto* kalima = FrameKalima();
            if (kalima == nullptr)
                return Core::Unexpected(std::string("0x4378e0: falta el dueño del agua del piso"));
            const auto height = kalima->Height(x, y);
            if (!height)
                return Core::Unexpected(std::string("0x4378e0: la posición del piso excede la altura Kalima"));
            return *height;
        };
        floor.itemContext = &itemContext;
        if (auto result = DrawFloorItems(tail.floorItems, floor); !result.HasValue())
            return result;
    }
    FaunaDrawContext fauna{world.Fauna(),
                           world.FaunaResources(),
                           world.Map().terrain,
                           FrameLighting(),
                           polygon,
                           FrameCloth(),
                           FrameSpritesForDraw(),
                           FramePoseCarry(),
                           runtime.map,
                           runtime.storm,
                           canvas.frameClock,
                           &FrameParticles(),
                           frames > 0,
                           CameraState().fixedHeight,
                           true};
    if (auto result = DrawGameFish(canvas.models, fauna); !result.HasValue())
        return result;
    if (auto result = DrawHelpers(
            {canvas.models, m_worldModels, world, FrameCloth().random, canvas.frameClock, 0, frames > 0,
             [&](const DrawnObject& object) { return PublishFaunaPose(FramePoseCarry(), m_worldModels, object, true); },
             true, [&]() { return std::span<const Render::Models::SkinnedMesh>(FramePoseCarry().meshes); },
             SceneId::Game, &FrameLighting(), &FrameSpritesForDraw(), clock, &FrameEffects(), &FrameParticles(),
             // timeGetTime (0x6b8544), del calco 0x14e de los Fenrir: el reloj del dibujo en ms.
             [systemTime = static_cast<std::uint32_t>(
                  std::chrono::duration_cast<std::chrono::milliseconds>(canvas.frameClock).count())]()
             { return systemTime; },
             [&]() { return std::span<const Render::Models::BoneTransform>(FramePoseCarry().bones); }});
        !result.HasValue())
        return result;
    const auto view = Render::ViewRows(pass.view);
    const JointDrawResources jointResources{FrameCloth().random, FrameSpritesForDraw(), frames > 0, clock};
    TailAttributeQuery attributes(*world.Map().terrain.attributes);
    const GameObjectPass kalimaPass{.map = static_cast<std::uint8_t>(runtime.map),
                                    .originalFrame = false,
                                    .worldClock = clock,
                                    .retainedPose = &FramePoseCarry(),
                                    .waterTextureFrame = FrameCloth().waterTextureFrame,
                                    .kalima = FrameKalima()};
    ModelEffectDrawResources effectResources{*world.Map().terrain.heights, clock};
    effectResources.onPose = [&](const DrawnObject& object)
    { return PublishFaunaPose(FramePoseCarry(), m_worldModels, object, true); };
    effectResources.onUntranslatedPose = [&](const DrawnObject& object)
    { return PublishFaunaPose(FramePoseCarry(), m_worldModels, object, false); };
    effectResources.posedMeshes = [&]()
    { return std::span<const Render::Models::SkinnedMesh>(FramePoseCarry().meshes); };
    effectResources.sprites = &FrameSpritesForDraw();
    effectResources.dynamicLighting = &FrameLighting();
    effectResources.attributes = &attributes;
    effectResources.siegeActive = runtime.siegeActive;
    effectResources.random = &FrameCloth().random;
    effectResources.itemContext = &itemContext;
    effectResources.kalimaPass = &kalimaPass;
    const auto effects = FrameEffects().Effects();
    const GameTailPassCallbacks callbacks{
        [&](GameTailStage stage, std::uint8_t mode) -> Core::Expected<void, std::string>
        {
            switch (stage)
            {
            case GameTailStage::WorldLeaves:
            case GameTailStage::EyeLeaves:
                // 0x5307b2: el 0x66 del 34 solo se dibuja con el clima de Crywolf en 1.
                return DrawGameLeaves(canvas.models, world.Leaves(), runtime.map, view, runtime.crywolf.weather);
            case GameTailStage::Joints:
                return DrawJoints(canvas.models, tail.joints, mode, jointResources);
            case GameTailStage::ModelEffects:
                return DrawGameModelEffects(canvas.models, m_worldModels, effects, m_frustum, &FrameLighting(),
                                            canvas.frameClock, 0, effectResources, mode);
            case GameTailStage::Ribbons:
                return DrawRibbons(canvas.models, tail.ribbons);
            case GameTailStage::MarkSpritesSeen:
                // Replay conserva la muestra anterior al primer ojo. El segundo549510 no la pisa:
                // modo1 retiene sprites y modo2 consume esa misma tabla más los emitidos entre pases.
                if (mode != 2)
                {
                    if (frames > 0)
                    {
                        m_spriteReplay = FrameSpritesForDraw();
                        m_spriteReplayValid = true;
                    }
                    else if (m_spriteReplayValid)
                    {
                        FrameSpritesForDraw() = m_spriteReplay;
                    }
                }
                FrameSpritesForDraw().MarkSeen();
                return {};
            case GameTailStage::BeginEye:
            case GameTailStage::BeginWorld:
                BeginGameTailPass(canvas.models, pass, stage == GameTailStage::BeginEye, canvas.flushModels);
                return {};
            case GameTailStage::Sprites:
                DrawFrameSprites(canvas.models, FrameSpritesForDraw(), m_worldModels, view, mode);
                return {};
            case GameTailStage::Particles:
                return DrawGameParticles(canvas.models, FrameParticles(), m_worldModels, view, clock, mode);
            case GameTailStage::Damage:
                return DrawDamage(canvas.models, tail.damage, mode, view);
            case GameTailStage::KalimaWater:
                return DrawKalimaWater(canvas);
            }
            return Core::Unexpected(std::string("0x63750d: etapa de cola desconocida"));
        },
        [&]() -> Core::Expected<bool, std::string> { return FrameKalima() != nullptr; }};
    return RunGameTailPasses(runtime.map, runtime.heroCellLayer, callbacks);
}
Core::Expected<void, std::string> GameScene::DrawKalimaWater(SceneCanvas& canvas)
{
    auto* kalima = FrameKalima();
    if (kalima == nullptr)
        return {}; // 4378a0, dueño ausente: el wrapper no entra en41cca0/41cf00.
    const float time = static_cast<float>(std::chrono::duration<double, std::milli>(canvas.frameClock).count());
    World::KalimaFrameContext context{FrameCloth().logicSteps, time, 0, 0, FrameCloth().random};
    context.readHeroX = [&]() -> Core::Expected<std::int32_t, std::string> { return ReadFrameHeroCell(true); };
    context.readHeroY = [&]() -> Core::Expected<std::int32_t, std::string> { return ReadFrameHeroCell(false); };
    if (auto updated = kalima->Update(context); !updated.HasValue())
        return updated;
    // 41cf00 caller: argumentos apilados Y y después X, cada uno en su acceso lógico.
    const auto y = ReadFrameHeroCell(false);
    const auto x = ReadFrameHeroCell(true);
    return kalima->Draw(canvas.models, x, y);
}
} // namespace Client::Scenes
