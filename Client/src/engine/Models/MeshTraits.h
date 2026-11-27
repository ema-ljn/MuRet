#pragma once

#include "Data/Models/Model.h"
#include "EngineApi.h"

#include <vector>

namespace Client::Models
{
// What 0x4f13d0 keeps of a mesh's texture script (0x4f17f3), and the mark of 0x6117fd..0x611847: what 0x4ef0c0 and
// 0x4ecc30 read of each mesh when they draw it.
struct MeshTraits
{
    // H: 0x4ef0c0 draws it in no pass.
    bool hidden = false;
    // R: 0x4ef0c0 passes 0x4ecc30 its index as the blendMesh, instead of the object's.
    bool blendScript = false;
    // S: 0x4ecc30 scrolls its UVs with the object's U and V.
    bool scrollScript = false;
    // Without N or the mark: 0x4ecc30 runs the glow passes on it.
    bool shines = true;
};

// One per mesh, in order, from each texture name's script.
[[nodiscard]] ENGINE_API std::vector<MeshTraits> ReadMeshTraits(const Data::Models::Model& model);
} // namespace Client::Models
