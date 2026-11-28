#include "Effects/ModelEffectDrawing.h"

#include "Actors/Drawing/CharacterDrawing.h"
#include "Actors/Drawing/ModelPoseCallbacks.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Crt/CrtRandom.h"
#include "Effects/GroundEffectDrawing.h"
#include "Effects/ModelEffects.h"
#include "Math/FloatConstants.h"
#include "Models/IModelLibrary.h"
#include "Render/ModelBatch.h"
#include "Terrain/SphereFrustum.h"
#include "Terrain/TerrainLighting.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

namespace Client::Effects
{
Core::Expected<void, std::string> DrawGroundEffects(Render::ModelBatch& batch, std::span<ModelEffect> effects,
                                                    const GroundEffectResources& resources)
{
    // What the draw creates (0x52e04c, 0x52e0a2) goes to the same table being walked.
    GroundEffectResources onTable = resources;
    onTable.table = effects;
    for (std::size_t slot = 0; slot < effects.size(); ++slot)
    {
        ModelEffect& effect = effects[slot];
        // 0x52dc2d..0x52dc40: does not recompute the sphere, reads this place's history.
        if (!effect.alive || !effect.seen)
        {
            continue;
        }
        static_cast<void>(resources.random.Next()); // 0x52dc46, even without a ground case.
        ApplyGroundEffectAdditive(batch);           // 0x620a90, before dispatching by type.
        if (auto drawn = DrawGroundEffectCase(batch, effect, onTable); !drawn.HasValue())
        {
            return Core::Unexpected(drawn.Error() + std::format(", en el efecto {} (el {:#x})", slot, effect.type));
        }
    }
    return {};
}

void StepModelEffects(std::span<ModelEffect> effects, const Models::IModelLibrary& models)
{
    for (ModelEffect& effect : effects)
    {
        if (effect.alive)
        {
            StepModelEffect(effect, models.SkeletonOf(Models::ModelId{static_cast<std::uint16_t>(effect.type)}));
        }
    }
}

void DrawModelEffects(Render::ModelBatch& batch, const Models::IModelLibrary& models, std::span<ModelEffect> effects,
                      const Terrain::SphereFrustum& frustum, const Terrain::TerrainLighting* lighting,
                      Core::Duration frameClock, float alpha)
{
    // Without a callback there is no error exit: keeps the 7-argument legacy API.
    static_cast<void>(DrawModelEffects(batch, models, effects, frustum, lighting, frameClock, alpha, {}, {}));
}

Core::Expected<void, std::string> DrawModelEffects(Render::ModelBatch& batch, const Models::IModelLibrary& models,
                                                   std::span<ModelEffect> effects,
                                                   const Terrain::SphereFrustum& frustum,
                                                   const Terrain::TerrainLighting* lighting, Core::Duration frameClock,
                                                   float alpha, const Actors::Drawing::TailPoseObserver& onPose,
                                                   const Actors::Drawing::TailMeshProvider& posedMeshes)
{
    for (ModelEffect& effect : effects)
    {
        if (!effect.alive)
        {
            continue;
        }
        // 0x52d63c..0x52d655: publishes +0x16c before the type, the model or the alpha. The next frame's ground pass
        // reads it as is. The sphere uses +0x10 without MuRet's extrapolation.
        constexpr float Radius = 400.0f;
        effect.seen = Terrain::SphereVisible(frustum, effect.transform.position, Radius);
        if (!effect.seen)
        {
            continue;
        }
        // 0x52da84: these types only have the ground pass; even without a model they publish seen. Icarus' 0x5ac
        // (0x52daa1) is not drawn: it shows through its 0x5ac/1 particles.
        if (effect.type == 0x61d || effect.type == 0x61e || effect.type == 0x61f || effect.type == IcarusFlareEffect)
        {
            continue;
        }
        const glm::vec3 terrain =
            lighting != nullptr ? Terrain::LightAt(*lighting, effect.transform.position.x, effect.transform.position.y)
                                : glm::vec3(0.0f);
        Actors::Drawing::DrawnObject drawn;
        drawn.model = static_cast<std::uint16_t>(effect.type);
        drawn.animation = effect.animation;
        drawn.transform = effect.transform;
        drawn.transform.position += (effect.transform.position - effect.previousPosition) * alpha;
        drawn.state = effect.objectState;
        // 0x5ffce0: with per-vertex light, the terrain plus the light; without it (the 0xbb cloud), the terrain times
        // 0.1f plus the light, with x unrounded before the sum and y and z rounded (0x5ffda5..0x5ffde7).
        if (effect.vertexLight)
            drawn.color = terrain + effect.light;
        else
        {
            constexpr double Tenth = Math::TenthAsDouble;
            drawn.color.x = static_cast<float>(static_cast<double>(terrain.x) * Tenth + effect.light.x);
            for (int axis = 1; axis < 3; ++axis)
                drawn.color[axis] = static_cast<float>(
                    static_cast<double>(static_cast<float>(static_cast<double>(terrain[axis]) * Tenth)) +
                    effect.light[axis]);
        }
        drawn.alpha = effect.alpha;
        drawn.vertexLight = effect.vertexLight;
        if (onPose && !(drawn.alpha < 0.01f) && models.Find(Models::ModelId{drawn.model}) != nullptr &&
            models.SkeletonOf(Models::ModelId{drawn.model}) != nullptr)
        {
            if (auto published = onPose(drawn); !published.HasValue())
                return published;
            if (auto rendered = Actors::Drawing::DrawPublishedObject(batch, models, drawn, frameClock, posedMeshes);
                !rendered.HasValue())
                return rendered;
        }
        else
            Actors::Drawing::DrawObject(batch, models, drawn, frameClock);
    }
    return {};
}
} // namespace Client::Effects
