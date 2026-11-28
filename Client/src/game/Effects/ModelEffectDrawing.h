#pragma once

#include "Actors/Drawing/ModelPoseCallbacks.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Effects/GroundEffectDrawing.h"
#include "Effects/ModelEffects.h"

#include <span>
#include <string>

namespace Client::Models
{
class IModelLibrary;
} // namespace Client::Models

namespace Client::Render
{
class ModelBatch;
} // namespace Client::Render

namespace Client::Terrain
{
struct SphereFrustum;
struct TerrainLighting;
} // namespace Client::Terrain

// The model effects' step and draw in the world pass (0x52d220, 0x52d620) and the ground effects (0x52dc20).
namespace Client::Effects
{
// 0x52dc20 (0x6374d3): for each live place with +0x16c retained, a common rand(), the additive blend and the type's
// case. Keeps the effects of the earlier places if one cuts. One original pass; GroundEffectFrame keeps the geometry
// between MuRet frames (D-C4).
[[nodiscard]] Core::Expected<void, std::string>
DrawGroundEffects(Render::ModelBatch& batch, std::span<ModelEffect> effects, const GroundEffectResources& resources);

// 0x52d220: StepModelEffect on each live effect, with its model's skeleton.
void StepModelEffects(std::span<ModelEffect> effects, const Models::IModelLibrary& models);

// 0x52d620(0) with the live effects: one RenderBody(2, 1, −1, 1, 0, 0, −1, −1) for each, opaque, with the color of
// the terrain light at its place (`lighting`, or 0 without it) plus its own, the light of each normal and the whole
// frame (06 § El dragón de fondo); the position, `alpha` one step further with what it moved in the last one (D-C4:
// the original draws it as is, at 25 per second). First it publishes +0x16c with the 400 sphere, on the logical
// position, even if the model is not found. A dead place keeps its mark; a live one outside the sphere does not query
// the model.
void DrawModelEffects(Render::ModelBatch& batch, const Models::IModelLibrary& models, std::span<ModelEffect> effects,
                      const Terrain::SphereFrustum& frustum, const Terrain::TerrainLighting* lighting,
                      Core::Duration frameClock, float alpha);
// The same legacy walk, publishing the pose and consuming that scratch without posing again.
[[nodiscard]] Core::Expected<void, std::string>
DrawModelEffects(Render::ModelBatch& batch, const Models::IModelLibrary& models, std::span<ModelEffect> effects,
                 const Terrain::SphereFrustum& frustum, const Terrain::TerrainLighting* lighting,
                 Core::Duration frameClock, float alpha, const Actors::Drawing::TailPoseObserver& onPose,
                 const Actors::Drawing::TailMeshProvider& posedMeshes);
} // namespace Client::Effects
