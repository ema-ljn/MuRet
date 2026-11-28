#pragma once

#include "Core/Expected.h"
#include "Math/HalfLifeMath.h"

#include <cstdint>
#include <span>
#include <string>

namespace Client::Kernel
{
struct PoseBuffer;
} // namespace Client::Kernel

namespace Client::Models
{
class IModelLibrary;
struct ObjectTransform;
} // namespace Client::Models

// What a draw leaves in the global pose buffer (Kernel/PoseBuffer.h) for the glows and shadows that read it after: the
// characters, the monsters, the fauna, the world's objects and the items on the floor.
namespace Client::Actors::Drawing
{
struct DrawnObject;

// 0x5ffdf0 on the object's model: with `translated`, the bones in the world (0x4eb860 with the flag at 1) and the
// scratch of 0x4ebc80 with Translate at 0; without it, the bones in the model and Translate at 1. Nothing below an
// alpha of 0.01 or without the model.
[[nodiscard]] Core::Expected<void, std::string> PublishPose(Kernel::PoseBuffer& buffer,
                                                            const Models::IModelLibrary& models,
                                                            const DrawnObject& object, bool translated);
// 0x608b0a: an item on the floor, posed in the model; its meshes are skinned only above an alpha of 0.01.
[[nodiscard]] Core::Expected<void, std::string>
PublishFloorPose(Kernel::PoseBuffer& buffer, const Models::IModelLibrary& models, const DrawnObject& object);
// The lent matrices already include an attached head or root; it does not pose again. With `translated`, `bones` are
// already in the world (0x4eb860 with the flag at 1, PoseWorldSkeleton) and the scratch comes out of 0x4ebc80 with
// Translate at 0; without it, they are in the model and the scratch takes the scale and origin of `transform`
// (Translate at 1).
[[nodiscard]] Core::Expected<void, std::string> PublishPoseRaw(Kernel::PoseBuffer& buffer,
                                                               const Models::IModelLibrary& models, std::uint16_t model,
                                                               std::span<const Math::BoneTransform> bones,
                                                               const Models::ObjectTransform& transform,
                                                               bool translated, bool skin = true);
// 0x4ebc80: updates the meshes' scratch with the kept bones, without running 0x4eb860.
[[nodiscard]] Core::Expected<void, std::string>
TransformPose(Kernel::PoseBuffer& buffer, const Models::IModelLibrary& models, const DrawnObject& object);
} // namespace Client::Actors::Drawing
