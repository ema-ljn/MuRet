#include "Scenes/GameScene.h"
#include "Scenes/GameItemRenderState.h"
#include "Scenes/GameItemSpecialDrawing.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/FaunaEffects.h"
#include "World/ITerrainAttributeQuerySource.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Render/Models/OriginalRotation.h"
#include "World/KalimaWorld.h"
#include "World/IFishStepServices.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/Billboard.h"
#include "Render/Pass3D.h"
#include "Render/Models/Skinning.h"
#include "Render/Models/HalfLifeMath.h"
#include "World/ModelEffects.h"
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
#include <algorithm>
#include <cstddef>
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
        return 0.01f;
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
    special.worldClock = static_cast<float>(std::chrono::duration<double, std::milli>(frameClock).count());
    special.random = &FrameCloth().random;
    special.pulse = &FrameItemRender().specialPulse;
    special.sprites = &FrameSpritesForDraw();
    special.bonePoint = [this](std::size_t bone) -> Core::Expected<glm::vec3, std::string>
    {
        const auto& bones = FramePoseCarry().bones;
        if (bone >= bones.size())
            return Core::Unexpected(std::string("0x4ec110: el hueso del ítem excede la pose compartida"));
        return Render::Models::OriginalVectorTransform({}, Render::Models::OriginalMatrixOf(bones[bone]));
    };
    // privateBoneOrigin sigue ausente: object+120 no está representado por el scratch global.
    item.special = &special;
    item.glowPulse = FrameItemRender().glowPulse;
    item.selections = &FrameItemRender().selections;
    item.originalFrame = frames > 0;
    item.modelState = &FrameModelState();
    item.map = m_context.world->Runtime().map;
    item.poseBones = [this]() { return std::span<const Render::Models::BoneTransform>(FramePoseCarry().bones); };
    item.createJoint = [this](const World::FishEffectRequest& request) -> Core::Expected<void, std::string>
    {
        // Productores de Helper07, dueño opaco (FloorItemState del piso o ActorState de lo que cuelga): nunca
        // interpretarlo como FaunaObject.
        if (!((request.type == 0x613 && request.subtype == 4) || (request.type == 0x614 && request.subtype == 14)))
            return Core::Unexpected(std::string("0x530860: creador del joint de ítem fuera del contrato de dueño"));
        const auto created = CreateFaunaJoint(FrameTail().joints, request, FrameCloth().random);
        return created.HasValue() ? Core::Expected<void, std::string>{} : Core::Unexpected(created.Error());
    };
    item.createEffect = [this](std::uint16_t type, std::int32_t subtype,
                               const DrawnObject& object) -> Core::Expected<void, std::string>
    {
        if (type == 0xc2 && subtype == 3)
        {
            static_cast<void>(CreateFaunaLightning(FrameEffects(), object.transform.position));
            return {};
        }
        if (subtype != 0 ||
            (type != World::DragonEffect && type != World::ArrivalEffect && type != World::TeleportEffect))
            return Core::Unexpected(std::string("0x518cf0: creador del efecto de ítem fuera del contrato portable"));
        static_cast<void>(FrameEffects().Create(static_cast<std::int16_t>(type), object.transform.position,
                                                object.transform.angles, object.color, object.transform.scale));
        return {};
    };
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
             true, [&]() { return std::span<const Render::Models::SkinnedMesh>(FramePoseCarry().meshes); }});
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
                return DrawGameLeaves(canvas.models, world.Leaves(), runtime.map, view);
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
