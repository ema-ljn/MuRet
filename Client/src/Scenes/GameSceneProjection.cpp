#include "Scenes/GameScene.h"

#include "Scenes/GameSceneFrameProjection.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/DynamicWorldLight.h"
#include "Scenes/ActorState.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/Cloth.h"
#include "World/KalimaWorld.h"
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
    m_projection->lighting = world.DynamicLighting().Lighting();
    m_projection->modelState = world.Runtime().objectModelState;
    m_projection->items = world.Runtime().itemRender;
    m_projection->cloth = *m_context.cloth;
    m_projection->effects = world.Map().effects;
    m_projection->particles = world.Particles();
    m_projection->objects = world.Map().objects;
    m_projection->kalima = world.Map().kalima ? world.Map().kalima->CloneForFrame() : nullptr;
    const auto& hero = world.Actors()[world.Runtime().hero];
    m_projection->heroXDepth = hero.cellXAccesses;
    m_projection->heroYDepth = hero.cellYAccesses;
    m_projection->ownRecordDepth = world.Runtime().ownRecordDepth;
}
GameDrawTailState& GameScene::FrameTail()
{
    return m_projectingFrame ? m_projection->tail : m_context.world->DrawTail();
}
World::TerrainLighting& GameScene::FrameLighting()
{
    return m_projectingFrame ? m_projection->lighting : m_context.world->DynamicLighting().Lighting();
}
Render::Models::ClothWorld& GameScene::FrameCloth()
{
    return m_projectingFrame ? m_projection->cloth : *m_context.cloth;
}
World::ObjectModelStepState& GameScene::FrameModelState()
{
    return m_projectingFrame ? m_projection->modelState : m_context.world->Runtime().objectModelState;
}
GameItemRenderState& GameScene::FrameItemRender()
{
    return m_projectingFrame ? m_projection->items : m_context.world->Runtime().itemRender;
}
World::ModelEffects& GameScene::FrameEffects()
{
    return m_projectingFrame ? m_projection->effects : m_context.world->Map().effects;
}
World::Particles& GameScene::FrameParticles()
{
    return m_projectingFrame ? m_projection->particles : m_context.world->Particles();
}
std::span<World::WorldObject> GameScene::FrameObjects()
{
    return m_projectingFrame ? std::span(m_projection->objects) : std::span(m_context.world->Map().objects);
}
World::KalimaWorld* GameScene::FrameKalima()
{
    return m_projectingFrame ? m_projection->kalima.get() : m_context.world->Map().kalima.get();
}
std::uint8_t GameScene::ReadFrameHeroCell(bool x)
{
    auto& hero = m_context.world->Actors()[m_context.world->Runtime().hero];
    auto& depth = m_projectingFrame ? (x ? m_projection->heroXDepth : m_projection->heroYDepth)
                                    : (x ? hero.cellXAccesses : hero.cellYAccesses);
    const Render::Models::LogicalAccess access(depth, FrameCloth().random, Render::Models::SmallLogicalCloseRandoms);
    return x ? hero.cellX : hero.cellY;
}
} // namespace Client::Scenes
