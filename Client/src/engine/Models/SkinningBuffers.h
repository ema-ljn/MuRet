#pragma once

#include "Math/HalfLifeMath.h"
#include "Models/Skinning.h"

#include <vector>

namespace Client::Models
{
// What a model's draw poses and skins into, kept between draws and frames by the draw's owner, so drawing one character
// after another reuses their capacity. Each use empties what it takes: a draw reads nothing a bigger model left there.
struct SkinningBuffers
{
    // The model's bones, and those of what is attached to it.
    std::vector<Math::BoneTransform> bones;
    std::vector<Math::BoneTransform> attachedBones;
    // The meshes of the model being drawn, or of one part after another.
    SkinnedMeshes meshes;
    // The model skinned again for the passes after its outline or its shadow.
    SkinnedMeshes posedMeshes;
    SkinnedMeshes shadowMeshes;
};
} // namespace Client::Models
