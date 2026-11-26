#pragma once

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "EngineApi.h"

#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

// A model's skeleton as 0x4f13d0 leaves it after reading it: each key with its quaternion (0x5fcd20 per key) and
// each action with what the pose and the advance look at (Client/docs/research/06-render-escenas-y-ui.md, "Cuánto
// avanza la animación").
namespace Client::Models
{
struct BoneKey
{
    glm::vec3 position{};
    // The file's angles, in radians: the head bone uses them to rotate (0x4eb860).
    glm::vec3 angles{};
    glm::quat rotation = glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f);
};

struct SkeletonBone
{
    // Not posed: its matrix stays as it was (0x4eb860).
    bool dummy = false;
    std::int16_t parent = -1;
    // [action][key]. A dummy has none.
    std::vector<std::vector<BoneKey>> actions;
};

// The original's 0x10-byte action.
struct ActionInfo
{
    bool repeats = true;        // +0 at 0, as 0x4f13d0 writes it
    float speed = 0.0f;         // +4, keys per step: 0x4f13d0 does not write it (Q-C87)
    std::int16_t keyCount = 0;  // +8
    bool lockPositions = false; // +10
};

struct Skeleton
{
    std::vector<SkeletonBone> bones;
    std::vector<ActionInfo> actions;
    // +0x54: −1 as 0x4f1f20(0) leaves it; the character's is set by 0x60dbe0 (Data::Models::PlayerHeadBone).
    std::int16_t headBone = -1;
};

// A bone whose parent does not come before it: the pose would concatenate it with a matrix not yet computed (Q-C108).
struct SkeletonError
{
    std::size_t bone = 0;
    std::int16_t parent = -1;
};

// Each key's quaternions, like 0x4f13d0. A parent that is neither −1 nor an earlier bone is an error:
// NPC\BloodCastle01.bmd has two (Q-C108), and what to do with them belongs to CL9.
[[nodiscard]] ENGINE_API Core::Expected<Skeleton, SkeletonError> MakeSkeleton(const Data::Models::Model& model);

// MakeSkeleton plus what 0x60dbe0 sets on Player.bmd: each action's speed (Data::Models::PlayerActionSpeed; the 57
// unwritten ones stay at 0, Q-C87), the four that do not repeat (PlayerActionsWithoutRepeat) and the head bone
// (PlayerHeadBone).
[[nodiscard]] ENGINE_API Core::Expected<Skeleton, SkeletonError> MakePlayerSkeleton(const Data::Models::Model& model);
} // namespace Client::Models
