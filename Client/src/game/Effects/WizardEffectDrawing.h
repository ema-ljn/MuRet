#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"

#include <string>

namespace Client::Render
{
class ModelBatch;
}
namespace Client::Effects
{
struct ModelEffect;
}

namespace Client::Models
{
class IModelLibrary;
}

namespace Client::Actors::Drawing
{
struct DrawnObject;
} // namespace Client::Actors::Drawing

// CL11, wizards batch: what 0x6002e0 does differently from the common pass (RenderBody(2)) with a model effect from
// 0x602e90, after posing it and checking the hidden mesh −2 (0x600328). Contract:
// Client/docs/research/06-render-escenas-y-ui.md, "Los efectos con modelo de los magos".
namespace Client::Effects
{

// 0x6004aa..0x6004d7: with +0x74 at 0x80 (the 0xcc/1, the 0xd0 and the subtype 1 cracks), RenderBody(0x82, +0x174,
// +0x64, +0x68, +0x6c, +0x70, +0x58, −1) and exits: the pass of each mesh's texture with the darkening blend
// (ZERO / ONE_MINUS_SRC_COLOR), on the posed meshes (`object.retainedMeshes`) or, without them, those it poses here as
// 0x5ffdf0 does (nothing with the alpha below 0.01 or without the model or its skeleton).
// 0x601da8..0x601df1: the 0xcc/3 and /4 with the model's color at (0.1f, 0.1f, 10) and the common pass.
// 0x601df6..0x601e39: the 0xcb/2 and /3 with the color at (0.5, 0.5, 1) and RenderBody(2, …, 0x6e9):
// Effect\Magic_b.jpg on each mesh. Then (0x6021fe) the color goes back to (1, 1, 1), which is not the effect's.
// 0x60213e..0x60216b: the 0x109 with the subtype (+0x4) at 1, before the common pass, RenderBody(0x1040, …, 1, −1):
// the additive chrome without mesh 1. The edx it compares is 0x601095's 1, so it is not the 0x109/0's (0x4cb3b0's
// shield, the only 0x109 the original creates), which goes only through the common pass.
// Returns true if it drew it; false if `object`'s common pass follows (with the color it set).
[[nodiscard]] Core::Expected<bool, std::string> DrawTheWizardEffectBody(Render::ModelBatch& batch,
                                                                        const Models::IModelLibrary& models,
                                                                        const Effects::ModelEffect& effect,
                                                                        Actors::Drawing::DrawnObject& object,
                                                                        Core::Duration frameClock);
} // namespace Client::Effects
