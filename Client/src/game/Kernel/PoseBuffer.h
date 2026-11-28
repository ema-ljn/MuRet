#pragma once

#include "Math/HalfLifeMath.h"
#include "Models/Pose.h"
#include "Models/Skinning.h"

#include <cstdint>
#include <map>
#include <vector>

// The global pose buffer (0x68b181c) and the vertex scratch (0x5786f9c, 0x68c53bc) that glows and shadows read. The
// world holds it (WorldState::PoseCarry): every draw that poses a model publishes there
// (Actors/Drawing/PosePublishing.h), and the next one that poses overwrites it.
namespace Client::Kernel
{
struct PoseBuffer
{
    std::vector<Math::BoneTransform> bones;
    std::vector<Models::SkinnedMesh> meshes;
    std::map<std::uint16_t, Models::ObjectTransform> models;
    // One mesh skinned before it goes over the head of `meshes`, kept for its capacity.
    Models::SkinnedMesh skinned;
};
} // namespace Client::Kernel
