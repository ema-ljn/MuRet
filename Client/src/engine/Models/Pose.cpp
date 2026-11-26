#include "Models/Pose.h"

#include "Math/Angles.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRotation.h"
#include "Models/Skeleton.h"

#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Client::Models
{
namespace
{
// The float at 0x6ba140 the head converts its degrees to radians with: bits 0x3c8efa36, one ulp above the float
// nearest to π/180 (0x3c8efa35, the one at 0x6ba360 that AngleMatrix uses).
constexpr float HeadDegreesToRadians = Math::DegreesToRadiansRoundedUp;

// The truncated frame that fits in the short of 0x4eb860's key; otherwise, key 0.
constexpr float ShortLimit = 32768.0f;

struct Keys
{
    std::size_t previousAction = 0;
    std::size_t previousKey = 0;
    std::size_t action = 0;
    std::size_t key = 0;
    float t = 0.0f;
    bool locked = false;
};

std::size_t ActionOrFirst(std::uint8_t action, std::size_t count)
{
    return action < count ? action : 0;
}

std::size_t KeyOrFirst(int key, std::int16_t keyCount)
{
    return key >= 0 && key < keyCount ? static_cast<std::size_t>(key) : 0;
}

// Like the start of 0x4eb860: the fractional part comes from the truncated frame before it is cut, and an action or
// key out of range is 0.
Keys ChooseKeys(const Skeleton& skeleton, const AnimationState& animation)
{
    Keys keys;
    keys.previousAction = ActionOrFirst(animation.previousAction, skeleton.actions.size());
    keys.action = ActionOrFirst(animation.action, skeleton.actions.size());
    const float whole = std::trunc(animation.frame);
    keys.t = animation.frame - whole;
    const int truncated = whole > -ShortLimit && whole < ShortLimit ? static_cast<int>(whole) : 0;
    keys.key = KeyOrFirst(truncated, skeleton.actions[keys.action].keyCount);
    keys.previousKey = KeyOrFirst(animation.previousKey, skeleton.actions[keys.previousAction].keyCount);
    keys.locked = skeleton.actions[keys.previousAction].lockPositions || skeleton.actions[keys.action].lockPositions;
    return keys;
}

// Whether every posed bone has both keys that will be read, and a parent that is −1 or an earlier, already computed
// bone: that way nothing is half written and nothing is read outside `bones`. A hand-built Skeleton did not go through
// MakeSkeleton.
bool CanPose(const Skeleton& skeleton, const Keys& keys)
{
    for (std::size_t index = 0; index < skeleton.bones.size(); ++index)
    {
        const SkeletonBone& bone = skeleton.bones[index];
        if (bone.dummy)
        {
            continue;
        }
        if (bone.parent < -1 || (bone.parent >= 0 && static_cast<std::size_t>(bone.parent) >= index))
        {
            return false;
        }
        if (keys.action >= bone.actions.size() || keys.previousAction >= bone.actions.size() ||
            keys.key >= bone.actions[keys.action].size() ||
            keys.previousKey >= bone.actions[keys.previousAction].size())
        {
            return false;
        }
    }
    return true;
}

// A key's quaternion; on the head bone, from its angles minus the head's.
glm::quat KeyRotation(const BoneKey& key, bool isHead, const glm::vec2& headAngles)
{
    if (!isHead)
    {
        return key.rotation;
    }
    glm::vec3 angles = key.angles;
    angles.x -= headAngles.x * HeadDegreesToRadians;
    angles.z -= headAngles.y * HeadDegreesToRadians;
    return Math::AngleQuaternion(angles);
}

Math::BoneTransform LocalTransform(const SkeletonBone& bone, std::size_t index, const Keys& keys,
                                   const Skeleton& skeleton, const ObjectTransform& object, const glm::vec2& headAngles)
{
    const std::vector<BoneKey>& current = bone.actions[keys.action];
    const BoneKey& from = bone.actions[keys.previousAction][keys.previousKey];
    const BoneKey& to = current[keys.key];

    const bool isHead = static_cast<int>(index) == skeleton.headBone;
    const glm::quat p = KeyRotation(from, isHead, headAngles);
    const glm::quat q = KeyRotation(to, isHead, headAngles);
    const glm::quat rotation = Math::QuaternionsAlmostEqual(p, q) ? p : Math::QuaternionSlerp(p, q, keys.t);

    glm::vec3 position = (1.0f - keys.t) * from.position + keys.t * to.position;
    if (index == 0 && keys.locked)
    {
        position.x = current[0].position.x;
        position.y = current[0].position.y;
        position.z += object.lockedRootHeight;
    }

    Math::BoneTransform local = Math::QuaternionMatrix(rotation);
    local[3] = position;
    return local;
}

Math::BoneTransform WorldRoot(const ObjectTransform& object)
{
    Math::OriginalMatrix root = Math::OriginalAngleMatrix(object.angles);
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            const std::size_t index = row * 4 + column;
            root[index] *= object.scale;
        }
    }
    root[3] = object.position.x;
    root[7] = object.position.y;
    root[11] = object.position.z;
    return Math::BoneTransformOf(root);
}

// The pose with `root` as the root of the parentless bones.
bool PoseFromRoot(const Skeleton& skeleton, const AnimationState& animation, const ObjectTransform& object,
                  const Math::BoneTransform& root, const glm::vec2& headAngles, std::span<Math::BoneTransform> bones,
                  bool originalConcat = false)
{
    if (bones.size() != skeleton.bones.size())
    {
        return false;
    }
    if (skeleton.actions.empty())
    {
        return true;
    }
    const Keys keys = ChooseKeys(skeleton, animation);
    if (!CanPose(skeleton, keys))
    {
        return false;
    }
    for (std::size_t index = 0; index < skeleton.bones.size(); ++index)
    {
        const SkeletonBone& bone = skeleton.bones[index];
        if (bone.dummy)
        {
            continue;
        }
        const Math::BoneTransform local = LocalTransform(bone, index, keys, skeleton, object, headAngles);
        const Math::BoneTransform& parent = bone.parent == -1 ? root : bones[static_cast<std::size_t>(bone.parent)];
        bones[index] = originalConcat ? Math::BoneTransformOf(Math::OriginalConcatTransforms(
                                            Math::OriginalMatrixOf(parent), Math::OriginalMatrixOf(local)))
                                      : Math::ConcatTransforms(parent, local);
    }
    return true;
}
} // namespace

bool PoseSkeleton(const Skeleton& skeleton, const AnimationState& animation, const ObjectTransform& object,
                  const glm::vec2& headAngles, std::span<Math::BoneTransform> bones)
{
    return PoseFromRoot(skeleton, animation, object, Math::AngleMatrix(object.angles), headAngles, bones);
}

bool PoseWorldSkeleton(const Skeleton& skeleton, const AnimationState& animation, const ObjectTransform& object,
                       const glm::vec2& headAngles, std::span<Math::BoneTransform> bones)
{
    return PoseFromRoot(skeleton, animation, object, WorldRoot(object), headAngles, bones, true);
}

bool PoseAttached(const Skeleton& skeleton, const AnimationState& animation, const Math::BoneTransform& root,
                  std::span<Math::BoneTransform> bones)
{
    return PoseFromRoot(skeleton, animation, ObjectTransform{}, root, glm::vec2(0.0f), bones);
}
} // namespace Client::Models
