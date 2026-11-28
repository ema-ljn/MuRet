#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Math/HalfLifeMath.h"

#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Render
{
class ModelBatch;
}
namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
class IModelLibrary;
} // namespace Client::Models
namespace Client::Particles
{
class FrameSprites;
} // namespace Client::Particles

namespace Client::Effects
{
struct ModelEffect;
} // namespace Client::Effects

namespace Client::Actors::Drawing
{
struct DrawnObject;
} // namespace Client::Actors::Drawing

// The high types: 0x6002e0's own cases (and those of 0x438fe0, which 0x6002e0 calls at 0x600ef6) for the high-type
// model effects that 0x52d620 sends through 0x52da92 → 0x602e90, after posing them (0x5ffdf0) and checking the hidden
// mesh −2 (0x600328). Contract: Client/docs/research/06-render-escenas-y-ui.md, "Los efectos con modelo de los tipos
// altos".
namespace Client::Effects
{

// What those cases read besides the object: the frame's sprites (0x5491e0), the CRT (0x69fd70), the bones of the
// pose's global buffer (0x68b181c, which 0x5ffdf0 leaves in the world) and the world clock (0x5767b88).
struct HighEffectDrawServices
{
    Particles::FrameSprites* sprites = nullptr;
    Crt::CrtRandom* random = nullptr;
    std::function<std::span<const Math::BoneTransform>()> posedBones{};
    std::optional<float> worldClock{};
};

// Whether the effect has its own case here: the 0x11b, the 0x114/3, /4 and /5, the 0x140 and the 0x125, of any other
// subtype.
[[nodiscard]] bool HasHighEffectBody(const Effects::ModelEffect& effect);

// The body of those cases, with `object` posed (its meshes in `object.retainedMeshes` or posed here like 0x5ffdf0).
// Only an effect with the alpha at 0.01 or more gets here: with less, or unordered, 0x5ffdf0 returns 0
// (0x5ffe2a..0x5ffe3d) and 0x602e90 does not call 0x6002e0 (DrawPlainModelEffect returns before the pose):
// - 0x601e68..0x601faf, the 0x11b: the model's color at (1; 0.6f; 0.3f) and RenderBody(2); +0x40 at bone 2
//   (0x68b187c) and +0x4c at bone 3 (0x68b18ac), with 0x4ec110 without translating; two sprites 0x5ac (scale 1, light
//   (1, 0, 0), turn 0) at +0x40 and at +0x4c, and two 0x5fd (scale 0.5f, light 0.5) at the same points, each with the
//   turn rand() % 360 (0x601f49 and 0x601f7d). In both passes if the blendMesh is −2 (0x52d673): four rand() per frame.
// - 0x601fb4..0x602000, the 0x114/3, /4 and /5: RenderBody(2, …, hidden mesh −1, texture 0x6ef, Skill\pinkWave.jpg).
// - 0x602005..0x602044, the 0x140: the color at (0.4f; 0.6f; 1) and two RenderBody(0x44) with the texture at −1 (the
//   chrome, Chrome01).
// - 0x43909e..0x439214 (0x600ef6 → 0x438fe0), the 0x125: the color at 0.3 and RenderBody(2); with l = FSIN(T · 0.003f)
//   · 0.2f + 0.8f, bones 3 (0x68b18ac) and 4 (0x68b18dc), each with a sprite 0x5ca (scale 0.2f) and a 0x5fd (scale
//   0.5f) of light (l; l · 0.2f; l · 0.1f). Without rand(). The 0x125 is born with the alpha at 0 (0x51cdf5): only its
//   joint 0x6ee/0x12 lights it (0x53949a, the alpha at 1 with the effect's life above 5) and copies its position
//   (0x5394a4..0x5394bb); until then it is neither posed nor drawn.
// Then all go to 0x602365, which with +0x78 & 2 repeats RenderBody(2) with the blendMesh −2 and light 1: a model effect
// does not write its +0x78 (nobody does between 0x518cf0 and 0x52ce13), so here it does not run. Returns true if it
// drew it; false if the effect has no case here. Without the sprites, the CRT, the bones or the clock it asks for, it
// stops.
[[nodiscard]] Core::Expected<bool, std::string>
DrawTheHighEffectBody(Render::ModelBatch& batch, const Models::IModelLibrary& models, Effects::ModelEffect& effect,
                      Actors::Drawing::DrawnObject& object, Core::Duration frameClock,
                      const HighEffectDrawServices& services);
} // namespace Client::Effects
