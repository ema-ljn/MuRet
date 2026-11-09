#include "Scenes/GameScene.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Models/Model.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "Scenes/WorldResources.h"
#include "Scenes/WorldState.h"
#include "World/ModelEffects.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <ratio>
#include <span>

namespace Client::Scenes
{
namespace
{
// Una consulta de modelo conserva juntos BMD, esqueleto y slots de la misma biblioteca.
class EffectModels final : public IModelLibrary
{
public:
    EffectModels(const IModelLibrary& world, const IModelLibrary& game) : m_world(world), m_game(game) {}
    const Data::Models::Model* Find(std::uint16_t type) const override
    {
        return Owner(type).Find(type);
    }
    const Render::Models::Skeleton* SkeletonOf(std::uint16_t type) const override
    {
        return Owner(type).SkeletonOf(type);
    }
    std::span<const Render::TextureSlot> MeshTextures(std::uint16_t type) const override
    {
        return Owner(type).MeshTextures(type);
    }

private:
    const IModelLibrary& Owner(std::uint16_t type) const
    {
        return m_world.Find(type) != nullptr ? m_world : m_game;
    }
    const IModelLibrary& m_world;
    const IModelLibrary& m_game;
};
} // namespace

void GameScene::StepEffects()
{
    WorldResources& resources = *m_context.world->Resources();
    const EffectModels models(resources.models, *m_models);
    const World::ModelEffectStepBindings bindings{
        .readClock =
            [this]
        {
            return m_context.clock != nullptr
                       ? static_cast<float>(std::chrono::duration<double, std::milli>(m_context.clock->Now()).count())
                       : 0.0f;
        },
        .animationOf = [&resources, &models](std::int16_t type) -> World::ModelEffectAnimationBinding*
        {
            const auto metadata = resources.objectRenderState.EffectAnimation(type, models);
            return metadata.HasValue() ? &metadata.Value().get() : nullptr;
        },
        .advance =
            [](World::ModelEffectAnimationBinding& metadata, World::ModelEffect& effect, float speed)
        {
            if (metadata.action.get() >= metadata.actions.size())
                return false; // El dispatcher devuelve InvalidMetadataAction en su fallback, sin indexar fuera.
            const auto& action = metadata.actions[metadata.action.get()];
            const float before = effect.animation.frame;
            const float next = before + speed;
            // 4ec200 conserva el frame anterior completo al cruzar key; previousKey es su proyección de pose.
            if (action.keyCount > 1 && std::trunc(next) != std::trunc(before))
                effect.previousFrame = before;
            // AdvanceAnimation devuelve terminó; el AL de4ec200 tiene el sentido contrario.
            return !Render::Models::AdvanceAnimation(effect.animation, action, speed,
                                                     Render::Models::AnimationScene::Other);
        }};
    const auto stepped = StepModelEffects(resources.effects.Effects(), models, m_context.cloth->random, bindings);
    if (!stepped.HasValue())
        m_failure = std::format("efectos del mapa: falta binding/metadata válida (causa {})",
                                static_cast<int>(stepped.Error()));
}

void GameScene::DrawEffects(SceneCanvas& canvas, float alpha)
{
    WorldResources& resources = *m_context.world->Resources();
    const EffectModels models(resources.models, *m_models);
    const auto drawn =
        DrawBoundModelEffects(canvas.models, models, resources.effects.Effects(), resources.modelEffectPlanes,
                              resources.terrain.Lighting(), canvas.frameClock, alpha);
    if (!drawn.HasValue())
        m_failure = std::format("dibujo de efecto: falta binding real del modelo {} (causa {})", drawn.Error().model,
                                static_cast<int>(drawn.Error().kind));
}
} // namespace Client::Scenes
