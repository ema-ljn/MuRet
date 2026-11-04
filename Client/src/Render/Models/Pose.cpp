#include "Render/Models/Pose.h"

#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"

#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Client::Render::Models
{
namespace
{
// El float de 0x6ba140, con el que la cabeza pasa sus grados a radianes: los bits 0x3c8efa36, un ulp
// más que el float más cercano a π/180 (0x3c8efa35, el de 0x6ba360, que usa AngleMatrix).
constexpr float HeadDegreesToRadians = std::bit_cast<float>(0x3c8efa36u);

// El frame truncado que entra en el short de la key de 0x4eb860; fuera de eso, la 0.
constexpr float ShortLimit = 32768.0f;

// Las dos keys entre las que interpola cada hueso, cuánto, y lo que vale para todos los huesos.
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

// Como el principio de 0x4eb860: la parte fraccionaria sale del frame truncado antes de cortarlo, y
// una acción o una key de más es la 0.
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

// Si cada hueso que se posa tiene las dos keys que se van a leer, y un padre que es −1 o un hueso de
// antes, ya calculado: así no se escribe a medias ni se lee fuera de `bones`. Un Skeleton armado a
// mano no pasó por MakeSkeleton.
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

// El cuaternión de una key; en el hueso de la cabeza, de sus ángulos menos los de la cabeza.
glm::quat KeyRotation(const BoneKey& key, bool isHead, const glm::vec2& headAngles)
{
    if (!isHead)
    {
        return key.rotation;
    }
    glm::vec3 angles = key.angles;
    angles.x -= headAngles.x * HeadDegreesToRadians;
    angles.z -= headAngles.y * HeadDegreesToRadians;
    return AngleQuaternion(angles);
}

// La matriz de un hueso relativa a su padre, entre las dos keys.
BoneTransform LocalTransform(const SkeletonBone& bone, std::size_t index, const Keys& keys, const Skeleton& skeleton,
                             const ObjectTransform& object, const glm::vec2& headAngles)
{
    const std::vector<BoneKey>& current = bone.actions[keys.action];
    const BoneKey& from = bone.actions[keys.previousAction][keys.previousKey];
    const BoneKey& to = current[keys.key];

    const bool isHead = static_cast<int>(index) == skeleton.headBone;
    const glm::quat p = KeyRotation(from, isHead, headAngles);
    const glm::quat q = KeyRotation(to, isHead, headAngles);
    const glm::quat rotation = QuaternionsAlmostEqual(p, q) ? p : QuaternionSlerp(p, q, keys.t);

    glm::vec3 position = (1.0f - keys.t) * from.position + keys.t * to.position;
    if (index == 0 && keys.locked)
    {
        position.x = current[0].position.x;
        position.y = current[0].position.y;
        position.z += object.lockedRootHeight;
    }

    BoneTransform local = QuaternionMatrix(rotation);
    local[3] = position;
    return local;
}

// La pose con la raíz `root` para los huesos sin padre.
bool PoseFromRoot(const Skeleton& skeleton, const AnimationState& animation, const ObjectTransform& object,
                  const BoneTransform& root, const glm::vec2& headAngles, std::span<BoneTransform> bones)
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
        const BoneTransform local = LocalTransform(bone, index, keys, skeleton, object, headAngles);
        const BoneTransform& parent = bone.parent == -1 ? root : bones[static_cast<std::size_t>(bone.parent)];
        bones[index] = ConcatTransforms(parent, local);
    }
    return true;
}
} // namespace

bool PoseSkeleton(const Skeleton& skeleton, const AnimationState& animation, const ObjectTransform& object,
                  const glm::vec2& headAngles, std::span<BoneTransform> bones)
{
    return PoseFromRoot(skeleton, animation, object, AngleMatrix(object.angles), headAngles, bones);
}

bool PoseAttached(const Skeleton& skeleton, const AnimationState& animation, const BoneTransform& root,
                  std::span<BoneTransform> bones)
{
    return PoseFromRoot(skeleton, animation, ObjectTransform{}, root, glm::vec2(0.0f), bones);
}
} // namespace Client::Render::Models
