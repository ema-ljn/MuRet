#include "Scenes/SiegeCueDrawing.h"

#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/SpriteVertex.h"
#include "World/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Client::Scenes
{
namespace
{
struct Shape
{
    float radius;
    float width;
    float length;
    float rise;
    float height;
    int count;
    Render::Color color;
};

Shape ShapeOf(const World::ModelEffect& effect, float progress)
{
    constexpr Render::Color Cyan{0.1f, 0.45f, 0.45f, 1.0f};
    constexpr Render::Color Amber{0.5f, 0.24f, 0.08f, 1.0f};
    constexpr Render::Color Violet{0.26f, 0.16f, 0.48f, 1.0f};
    constexpr Render::Color Blue{0.18f, 0.34f, 0.64f, 1.0f};
    if (effect.type == World::CrywolfArrowEffect)
        return {.radius = 9, .width = 6, .length = 20, .rise = 10, .height = 0, .count = 3, .color = Cyan};
    if (effect.type == World::CrywolfPulseEffect)
        return {.radius = 42, .width = 7, .length = 7, .rise = 19, .height = 0, .count = 6, .color = Violet};
    if (effect.type == World::CrywolfShardEffect)
        return {.radius = 8, .width = 5, .length = 16, .rise = 8, .height = 0, .count = 1, .color = Blue};
    if (World::IsRisingStoneChip(effect.type, effect.subtype))
        return {.radius = 4,
                .width = 5 * effect.transform.scale,
                .length = 2,
                .rise = 9 * effect.transform.scale,
                .height = 0,
                .count = 3,
                .color = Amber};
    if (effect.type == World::SiegeArrivalCue)
        return {.radius = 32.0f * effect.transform.scale,
                .width = 8,
                .length = 0,
                .rise = 34,
                .height = 10,
                .count = 4,
                .color = Cyan};
    if (effect.type == World::SiegeStoneCue)
        return {.radius = 28.0f + 60.0f * progress,
                .width = 10,
                .length = 0,
                .rise = 26,
                .height = -210.0f * progress,
                .count = 5,
                .color = Amber};
    return {.radius = 24.0f + 140.0f * progress,
            .width = 6,
            .length = 24,
            .rise = 0,
            .height = 12,
            .count = 8,
            .color = Violet};
}
} // namespace

void DrawBattleCue(Render::ModelBatch& batch, const World::ModelEffect& effect)
{
    const float intensity =
        std::clamp(effect.type == World::EarthquakeBurstEffect ? effect.objectState.light : effect.alpha, 0.0f, 1.0f);
    if (!(intensity > 0.0f))
        return;
    const Shape shape = ShapeOf(effect, 1.0f - intensity);
    const Render::Color color{shape.color.red * intensity, shape.color.green * intensity, shape.color.blue * intensity,
                              1.0f};
    const auto previous = batch.State();
    batch.SetState({.texture = {}, .blend = Render::ModelBlend::Additive, .texture2d = false});
    constexpr float FullTurn = 2.0f * std::numbers::pi_v<float>;
    constexpr float RadiansPerDegree = FullTurn / 360.0f;
    const float phase = effect.type == World::EarthquakeBurstEffect
                            ? 0.0f
                            : (effect.transform.angles.z + effect.copiedAngles.z) * RadiansPerDegree;
    const glm::vec3 origin = effect.transform.position + glm::vec3{0, 0, shape.height};
    for (int i = 0; i < shape.count; ++i)
    {
        const float angle = phase + FullTurn * static_cast<float>(i) / static_cast<float>(shape.count);
        const glm::vec3 direction{std::cos(angle), std::sin(angle), 0};
        const glm::vec3 tangent{-direction.y, direction.x, 0};
        const auto base = origin + direction * shape.radius;
        const auto half = tangent * shape.width;
        const auto tip = base + direction * shape.length + glm::vec3{0, 0, shape.rise};
        batch.Triangle({Render::ModelVertex{.position = base - half, .color = color},
                        Render::ModelVertex{.position = base + half, .color = color},
                        Render::ModelVertex{.position = tip, .color = color}});
    }
    batch.SetState(previous);
}
} // namespace Client::Scenes
