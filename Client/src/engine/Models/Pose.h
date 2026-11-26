#pragma once

#include "EngineApi.h"
#include "Math/HalfLifeMath.h"
#include "Models/Skeleton.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <span>

// A skeleton's pose like 0x4eb860 (Client/docs/research/06-render-escenas-y-ui.md, "El modelo en el binario" and
// "Cuánto avanza la animación").
namespace Client::Models
{
// What the object stores of its animation (+0x111, +0x114, +0x112 and +0x118 of the object).
struct AnimationState
{
    std::uint8_t action = 0;
    // With the fractional part: the pose interpolates with it.
    float frame = 0.0f;
    // The action and key the advance stored when crossing one (0x4ec200). The original stores the previous frame in a
    // float, and the pose truncates it.
    std::uint8_t previousAction = 0;
    std::int16_t previousKey = 0;
};

// What the model copies from the object before posing (0x6001b0 for a character, 0x5ffdf0 for a monster or an NPC).
struct ObjectTransform
{
    glm::vec3 position{};
    // In degrees: the root rotation (model +0x78, 0x5fc8f0).
    glm::vec3 angles{};
    float scale = 1.0f;
    // Model +0x84: added to the root's z with lock_positions. Callers that pose an object set it to 0 first.
    float lockedRootHeight = 0.0f;
};

// 0x4eb860 as 0x6001b0 calls it for a character's body (0x50ba30 → 0x6001b0(object, 1, …), with the pose's last
// argument at 0): the root rotates with the object's angles, and SkinMesh applies scale and position on each vertex.
// Writes one BoneTransform per bone into `bones`, sized like the skeleton; dummies are left untouched.
//   - Interpolates between the previous action's key (previousKey) and the current one (the truncated frame), with
//     the frame's fractional part: the 0x5fce90 slerp between the quaternions, unless 0x5fc700 finds them equal, and
//     linear between the positions. A missing action or key is 0.
//   - The head bone (Skeleton::headBone) subtracts headAngles.x from its x angle and headAngles.y from its z, in
//     degrees, and converts to a quaternion on the spot.
//   - Bone 0, if the previous or the current action has lock_positions, takes x and y from key 0 of the current one,
//     and adds lockedRootHeight to the interpolated z.
// Does nothing without actions in the skeleton. Returns false, without writing, if `bones` is not the skeleton's
// size, if a posed bone lacks one of the two keys, or if its parent is neither −1 nor an earlier bone (a hand-built
// Skeleton that did not go through MakeSkeleton).
[[nodiscard]] ENGINE_API bool PoseSkeleton(const Skeleton& skeleton, const AnimationState& animation,
                                           const ObjectTransform& object, const glm::vec2& headAngles,
                                           std::span<Math::BoneTransform> bones);

// The root of 0x4eb860 with flags (0, 1): rotates and scales its base with the object, and takes its position
// unscaled. The rest of the pose and its error conditions are PoseSkeleton's.
[[nodiscard]] ENGINE_API bool PoseWorldSkeleton(const Skeleton& skeleton, const AnimationState& animation,
                                                const ObjectTransform& object, const glm::vec2& headAngles,
                                                std::span<Math::BoneTransform> bones);

// 0x4eb860 with its seventh argument at 1, as 0x509340 poses what hangs from the character (Attachment.h): the
// parentless bones use `root`, the one the caller left in 0x68c4a1c, not the object's rotation. No head angles and
// +0x84 at 0. Returns false like PoseSkeleton.
[[nodiscard]] ENGINE_API bool PoseAttached(const Skeleton& skeleton, const AnimationState& animation,
                                           const Math::BoneTransform& root, std::span<Math::BoneTransform> bones);
} // namespace Client::Models
