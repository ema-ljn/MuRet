#include "Scenes/GameScene.h"

#include "Core/Expected.h"
#include "Render/Billboard.h"
#include "Render/Pass3D.h"
#include "Render/Models/Skinning.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GameParticles.h"
#include "Scenes/GameProjection.h"
#include "Scenes/GameTailPasses.h"
#include "Scenes/HelperTable.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/WorldTerrain.h"
#include "World/Sprites.h"

#include <chrono>
#include <cstdint>
#include <glm/ext/vector_float3.hpp>
#include <ratio>
#include <string>
#include <span>

namespace Client::Scenes
{
Core::Expected<void, std::string> GameScene::DrawTheBirds(SceneCanvas& canvas, const World::TerrainPolygon& polygon,
                                                          int frames)
{
    WorldState& world = *m_context.world;
    FaunaDrawContext context{world.Fauna(),
                             world.FaunaResources(),
                             world.Map().terrain,
                             world.DynamicLighting().Lighting(),
                             polygon,
                             *m_context.cloth,
                             FrameSpritesForDraw(),
                             FramePoseCarry(),
                             world.Runtime().map,
                             world.Runtime().storm,
                             canvas.frameClock,
                             &world.Particles(),
                             frames > 0,
                             CameraState().fixedHeight,
                             true};
    return DrawGameBirds(canvas.models, context);
}

Core::Expected<void, std::string> GameScene::DrawTheTail(SceneCanvas& canvas, const Render::Pass3D& pass,
                                                         const World::TerrainPolygon& polygon, int frames)
{
    WorldState& world = *m_context.world;
    WorldRuntime& runtime = world.Runtime();
    GameDrawTailState& tail = world.DrawTail();
    const auto projection = MakeGameProjection(pass, m_drawnCamera.fieldOfView);
    if (!projection.HasValue())
        return Core::Unexpected(projection.Error());
    const FloorItemDrawContext floor{
        canvas.models,
        m_worldModels,
        m_frustum,
        &world.DynamicLighting().Lighting(),
        canvas.frameClock,
        runtime.map,
        [&](const glm::vec3& point) { return ProjectGamePoint(projection.Value(), point); },
        [&](const DrawnObject& object) { return PublishFaunaFloorPose(FramePoseCarry(), m_worldModels, object); },
        [&]() { return std::span<const Render::Models::SkinnedMesh>(FramePoseCarry().meshes); }};
    if (auto result = DrawFloorItems(tail.floorItems, floor); !result.HasValue())
        return result;
    FaunaDrawContext fauna{world.Fauna(),
                           world.FaunaResources(),
                           world.Map().terrain,
                           world.DynamicLighting().Lighting(),
                           polygon,
                           *m_context.cloth,
                           FrameSpritesForDraw(),
                           FramePoseCarry(),
                           runtime.map,
                           runtime.storm,
                           canvas.frameClock,
                           &world.Particles(),
                           frames > 0,
                           CameraState().fixedHeight,
                           true};
    if (auto result = DrawGameFish(canvas.models, fauna); !result.HasValue())
        return result;
    if (auto result = DrawHelpers(
            {canvas.models, m_worldModels, world, m_context.cloth->random, canvas.frameClock, 0, frames > 0,
             [&](const DrawnObject& object) { return PublishFaunaPose(FramePoseCarry(), m_worldModels, object, true); },
             true, [&]() { return std::span<const Render::Models::SkinnedMesh>(FramePoseCarry().meshes); }});
        !result.HasValue())
        return result;
    const auto view = Render::ViewRows(pass.view);
    Render::Models::CrtRandom projectedRandom = m_context.cloth->random;
    const JointDrawResources jointResources{frames > 0 ? m_context.cloth->random : projectedRandom,
                                            FrameSpritesForDraw(), frames > 0};
    const float clock = static_cast<float>(std::chrono::duration<double, std::milli>(canvas.frameClock).count());
    const ModelEffectDrawResources effectResources{
        *world.Map().terrain.heights, clock,
        [&](const DrawnObject& object) { return PublishFaunaPose(FramePoseCarry(), m_worldModels, object, true); },
        [&]() { return std::span<const Render::Models::SkinnedMesh>(FramePoseCarry().meshes); }};
    const auto effects = frames > 0 ? world.Map().effects.Effects() : m_projectedEffects.Effects();
    const GameTailPassCallbacks callbacks{
        [&](GameTailStage stage, std::uint8_t mode) -> Core::Expected<void, std::string>
        {
            switch (stage)
            {
            case GameTailStage::WorldLeaves:
            case GameTailStage::EyeLeaves:
                return DrawGameLeaves(canvas.models, world.Leaves(), runtime.map, view);
            case GameTailStage::Joints:
                return DrawJoints(canvas.models, tail.joints, mode, jointResources);
            case GameTailStage::ModelEffects:
                return DrawGameModelEffects(canvas.models, m_worldModels, effects, m_frustum,
                                            &world.DynamicLighting().Lighting(), canvas.frameClock, 0, effectResources,
                                            mode);
            case GameTailStage::Ribbons:
                return DrawRibbons(canvas.models, tail.ribbons);
            case GameTailStage::MarkSpritesSeen:
                // Replay conserva la muestra anterior al primer ojo. El segundo549510 no la pisa:
                // modo1 retiene sprites y modo2 consume esa misma tabla más los emitidos entre pases.
                if (mode != 2)
                {
                    if (frames > 0)
                        m_spriteReplay = FrameSpritesForDraw();
                    else
                        FrameSpritesForDraw() = m_spriteReplay;
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
                return DrawGameParticles(canvas.models, world.Particles(), m_worldModels, view, clock, mode);
            case GameTailStage::Damage:
                return DrawDamage(canvas.models, tail.damage, mode, view);
            case GameTailStage::KalimaWater:
                return Core::Unexpected(
                    std::string("0x4378a0: falta la actualización y el dibujo del mundo Kalima (0x41cca0/0x41cf00)"));
            }
            return Core::Unexpected(std::string("0x63750d: etapa de cola desconocida"));
        },
        [&]() -> Core::Expected<bool, std::string>
        {
            // 437850 lee723c48, no local_8. Fuera de Kalima, LoadMap deja ese mundo ausente.
            // En Kalima el loader corta4377a0: no inventar un puntero válido por el byte del mapa.
            if (GameTailFirstMode(runtime.map) != 0)
                return Core::Unexpected(
                    std::string("0x437850: falta el estado Kalima 0x723c48 (LoadMap corta en 0x4377a0)"));
            return false;
        }};
    return RunGameTailPasses(runtime.map, runtime.heroCellLayer, callbacks);
}
} // namespace Client::Scenes
