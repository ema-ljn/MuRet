#include "Render/Models/Pose.h"

#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"

#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Client::Render::Models
{
namespace
{
// El float de 0x6ba140: π/180, redondeado una vez (los bits 0x3c8efa35).
constexpr float DegreesToRadians = 0.017453292f;

// Las dos keys entre las que interpola cada hueso, y cuánto.
struct Keys
{
    std::size_t previousAction = 0;
    std::size_t previousKey = 0;
    std::size_t action = 0;
    std::size_t key = 0;
    float t = 0.0f;
};

std::size_t ActionOrFirst(std::uint8_t action, std::size_t count)
{
    return action < count ? action : 0;
}

std::size_t KeyOrFirst(int key, std::int16_t keyCount)
{
    return key >= 0 && key < keyCount ? static_cast<std::size_t>(key) : 0;
}

// Como el principio de 0x4eb860: la parte fraccionaria sale del frame truncado antes de cortarlo, y
// una acción o una key de más es la 0.
Keys ChooseKeys(const Skeleton& skeleton, const AnimationState& animation)
{
    Keys keys;
    keys.previousAction = ActionOrFirst(animation.previousAction, skeleton.actions.size());
    keys.action = ActionOrFirst(animation.action, skeleton.actions.size());
    const float whole = std::trunc(animation.frame);
    keys.t = animation.frame - whole;
    const int truncated = whole > -32768.0f && whole < 32768.0f ? static_cast<int>(whole) : 0;
    keys.key = KeyOrFirst(truncated, skeleton.actions[keys.action].keyCount);
    keys.previousKey = KeyOrFirst(animation.previousKey, skeleton.actions[keys.previousAction].keyCount);
    return keys;
}

// Si cada hueso que se posa tiene las dos keys que se van a leer: así no se escribe a medias.
bool HasTheKeys(const Skeleton& skeleton, const Keys& keys)
{
    for (const SkeletonBone& bone : skeleton.bones)
    {
        if (bone.dummy)
        {
            continue;
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

// El cuaternión de una key; en el hueso de la cabeza, de sus ángulos menos los de la cabeza.
glm::quat KeyRotation(const BoneKey& key, bool isHead, const glm::vec2& headAngles)
{
    if (!isHead)
    {
        return key.rotation;
    }
    glm::vec3 angles = key.angles;
    angles.x -= headAngles.x * DegreesToRadians;
    angles.z -= headAngles.y * DegreesToRadians;
    return AngleQuaternion(angles);
}
} // namespace

bool PoseSkeleton(const Skeleton& skeleton, const AnimationState& animation, const ObjectTransform& object,
                  const glm::vec2& headAngles, std::span<BoneTransform> bones)
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
    if (!HasTheKeys(skeleton, keys))
    {
        return false;
    }
    const float t = keys.t;
    const float oneMinusT = 1.0f - t;
    const bool locked =
        skeleton.actions[keys.previousAction].lockPositions || skeleton.actions[keys.action].lockPositions;
    const BoneTransform root = AngleMatrix(object.angles);

    for (std::size_t index = 0; index < skeleton.bones.size(); ++index)
    {
        const SkeletonBone& bone = skeleton.bones[index];
        if (bone.dummy)
        {
            continue;
        }
        const std::vector<BoneKey>& current = bone.actions[keys.action];
        const BoneKey& from = bone.actions[keys.previousAction][keys.previousKey];
        const BoneKey& to = current[keys.key];

        const bool isHead = static_cast<int>(index) == skeleton.headBone;
        const glm::quat p = KeyRotation(from, isHead, headAngles);
        const glm::quat q = KeyRotation(to, isHead, headAngles);
        const glm::quat rotation = QuaternionsAlmostEqual(p, q) ? p : QuaternionSlerp(p, q, t);

        glm::vec3 position = oneMinusT * from.position + t * to.position;
        if (index == 0 && locked)
        {
            position.x = current[0].position.x;
            position.y = current[0].position.y;
            position.z += object.lockedRootHeight;
        }

        BoneTransform local = QuaternionMatrix(rotation);
        local[3] = position;
        const BoneTransform& parent = bone.parent == -1 ? root : bones[static_cast<std::size_t>(bone.parent)];
        bones[index] = ConcatTransforms(parent, local);
    }
    return true;
}
} // namespace Client::Render::Models
