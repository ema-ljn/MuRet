#include "Scenes/GameScene.h"

#include "Actors/ActorPickingBox.h"
#include "Scenes/GameSceneFrameProjection.h"
#include "Items/UiItemDrawing.h"
#include "Kernel/WorldState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/ActorState.h"
#include "Terrain/DynamicWorldLight.h"
#include "Kernel/ActorState.h"
#include "Models/LogicalAccess.h"
#include "Models/Cloth.h"
#include "Ui/GameUiState.h"
#include "MapScripts/Kalima/KalimaWorld.h"
#include <memory>
#include <span>
#include <cstdint>

namespace Client::Scenes
{
void GameScene::PrepareDrawProjection(int frames)
{
    m_projectingFrame = frames == 0;
    if (!m_projectingFrame)
        return;
    if (!m_projection)
        m_projection = std::make_unique<GameSceneFrameProjection>();
    auto& world = *m_context.world;
    m_projection->tail = world.DrawTail();
    // Las uniones del objeto local de la UI (0x1a18) mueren antes del dibujo siguiente del original (Q-C1821).
    Items::HideUiItemJoints(m_projection->tail);
    m_projection->lighting = DynamicLight().Lighting();
    m_projection->modelState = world.Runtime().objectModelState;
    m_projection->items = world.Runtime().itemRender;
    m_projection->cloth = *m_context.cloth;
    m_projection->effects = world.Map().effects;
    m_projection->particles = world.Particles();
    m_projection->objects = world.Map().objects;
    m_projection->actorPickingBoxes.clear();
    m_projection->actorPickingBoxes.reserve(world.Actors().size());
    for (const Kernel::ActorState& actor : world.Actors())
        m_projection->actorPickingBoxes.push_back(Actors::PickingBoxOf(actor));
    m_projection->kalima = world.Map().kalima ? world.Map().kalima->CloneForFrame() : nullptr;
    const auto& hero = world.Hero();
    m_projection->heroXDepth = hero.cellXAccesses;
    m_projection->heroYDepth = hero.cellYAccesses;
    m_projection->ownRecordDepth = world.Runtime().ownRecordDepth;
    m_projection->cameraShake = CameraInputState().shake;
    m_projection->hud = world.Runtime().hud;
    m_projection->crywolfSandOverlay = world.Runtime().crywolf.sandOverlay;
    m_projection->crywolfInterface = world.Runtime().crywolf.ui;
    m_projection->sound.reset();
    m_projection->altLatch = world.Runtime().ui.keyLatches[Ui::AltVirtualKey];
    m_projection->altNames = world.Runtime().ui.altNames;
    m_projection->oldWindows = world.Runtime().oldWindows;
}
GameDrawTailState& GameScene::FrameTail()
{
    return m_projectingFrame ? m_projection->tail : m_context.world->DrawTail();
}
Terrain::TerrainLighting& GameScene::FrameLighting()
{
    return m_projectingFrame ? m_projection->lighting : DynamicLight().Lighting();
}
Models::ClothWorld& GameScene::FrameCloth()
{
    return m_projectingFrame ? m_projection->cloth : *m_context.cloth;
}
WorldObjects::ObjectModelStepState& GameScene::FrameModelState()
{
    return m_projectingFrame ? m_projection->modelState : m_context.world->Runtime().objectModelState;
}
Actors::Drawing::GameItemRenderState& GameScene::FrameItemRender()
{
    return m_projectingFrame ? m_projection->items : m_context.world->Runtime().itemRender;
}
Effects::ModelEffects& GameScene::FrameEffects()
{
    return m_projectingFrame ? m_projection->effects : m_context.world->Map().effects;
}
Particles::Particles& GameScene::FrameParticles()
{
    return m_projectingFrame ? m_projection->particles : m_context.world->Particles();
}
std::span<WorldObjects::WorldObject> GameScene::FrameObjects()
{
    return m_projectingFrame ? std::span(m_projection->objects) : std::span(m_context.world->Map().objects);
}
MapScripts::Kalima::KalimaWorld* GameScene::FrameKalima()
{
    return m_projectingFrame ? m_projection->kalima.get() : m_context.world->Map().kalima.get();
}
std::uint8_t GameScene::ReadFrameHeroCell(bool x)
{
    auto& hero = m_context.world->Hero();
    auto& depth = m_projectingFrame ? (x ? m_projection->heroXDepth : m_projection->heroYDepth)
                                    : (x ? hero.cellXAccesses : hero.cellYAccesses);
    const Models::LogicalAccess access(depth, FrameCloth().random, Models::SmallLogicalCloseRandoms);
    return x ? hero.cellX : hero.cellY;
}
} // namespace Client::Scenes
