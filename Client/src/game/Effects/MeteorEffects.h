#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"

#include <cstdint>
#include <optional>
#include <string>

// The 0x13f that the 0x1a9 (Monster87) drops in its action 4 (0x442cb9) and the two it leaves when it goes out: the
// 0x13d, the stone that pulses, shakes the camera, throws pebbles and gives off sparks through its bones, and the
// 0x13e, its shell. Their creations (0x51efdc and 0x51c727) and steps (0x525f6a, 0x5260bd; the 0x13e, only the tail).
// The last two are drawn by 0x442e10 (0x44357f and 0x4435e9, DrawTheMeteorBody). 06, "Los efectos con modelo de los
// tipos altos".
namespace Client::Render
{
class ModelBatch;
} // namespace Client::Render

namespace Client::Models
{
class IModelLibrary;
struct Skeleton;
} // namespace Client::Models

namespace Client::Effects
{
struct ModelEffect;
} // namespace Client::Effects

namespace Client::Actors::Drawing
{
struct DrawnObject;
} // namespace Client::Actors::Drawing

namespace Client::Effects
{
struct HighEffectStepContext;

inline constexpr std::int16_t MeteorEffect = 0x13d;
inline constexpr std::int16_t MeteorShellEffect = 0x13e;
inline constexpr std::int16_t MeteorCallEffect = 0x13f;

// 0x51c727 (the 0x13d and the 0x13e: life 120 and scale 0.8f) and 0x51efdc (the 0x13f: life 20). Any subtype; no
// rand().
void BeginTheMeteor(Effects::ModelEffect& effect);

// 0x525f6a..0x5260b8, the 0x13f. With life 8, sixteen joints 0x60f/0x18 with no owner, width 90 (0x526028): each with
// the angles (−10, 0, i · 10), at the position with z 100 more, x plus rand() % 600 − 200 and y plus
// rand() % 600 − 400. With life 2, the angles (0, 0, rand() % 360), the 0x13d (0x526090) and the 0x13e (0x5260ad) at
// its position, with its light and itself as owner (key −1, scale 0), and the slot freed (the tail goes on).
[[nodiscard]] Core::Expected<void, std::string> StepTheMeteorCall(Effects::ModelEffect& effect,
                                                                  const HighEffectStepContext& context);

// 0x5260bd..0x526337, the 0x13d:
// - the dynamic light (v, v, v + 0.1) of radius 10 (0x5260fe), with v = FSIN(life · π/120) · −0.5;
// - the camera shake at ((rand() & 3) − 2) · 0.1f (0x526126);
// - if rand() & 1 gives 0, the pebble 0xc8 or 0xc9 (0x526207): rand() % 150 + 300 ahead, rotated rand() % 360 in z,
//   and the type with rand() & 1 (five rand() plus the pebble's);
// - the pose of model 0x13d (0x4eb860) in the global buffer and, for each non-dummy bone, rand() % 12; with 0, at
//   its point (0x4ec110 with the model's scale and origin) the particle 0x6ec/3 with itself as owner (0x5262e8, from
//   another batch: with room it cuts) and the 0x5f2/0x15 (0x526307).
// Without the dynamic light or the shake in the services it cuts, after the earlier rand().
[[nodiscard]] Core::Expected<void, std::string>
StepTheMeteor(Effects::ModelEffect& effect, const Models::Skeleton* skeleton, const HighEffectStepContext& context);

// 0x6002e0 → 0x442e10 with the 0x13d (0x44357f) and the 0x13e (0x4435e9), after the pose and checking the hidden mesh
// −2: between 0x4ecc10 and 0x4ecc20, without RenderBody, the model's color at (1, 1, 1) and 0x4ecc30 on mesh 0 with
// flags 2, the alpha (+0x174) and light 1, on the posed meshes (`object.retainedMeshes`) or, without them, those it
// poses here as 0x5ffdf0 does (nothing with the alpha below 0.01 or without the model or its skeleton):
// - the 0x13d, two passes with the effect's U and V (+0x6c, +0x70): with blendMesh −1 and its texture, and with 0 and
//   texture 0x63c;
// - the 0x13e, one with blendMesh 0 and its texture, the effect's V and the U at FSIN(T · 0.00008f) · 2.5f, which
//   stays in +0x6c; then +0x6c goes back to 1. T is the world clock (0x5767b88): without it, it cuts.
// Returns true if it is one of the two; false with another type (the common pass follows).
[[nodiscard]] Core::Expected<bool, std::string>
DrawTheMeteorBody(Render::ModelBatch& batch, const Models::IModelLibrary& models, Effects::ModelEffect& effect,
                  const Actors::Drawing::DrawnObject& object, Core::Duration frameClock,
                  std::optional<float> worldClock);
} // namespace Client::Effects
