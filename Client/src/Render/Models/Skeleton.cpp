#include "Render/Models/Skeleton.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Render/Models/HalfLifeMath.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace Client::Render::Models
{
namespace
{
glm::vec3 ToVec3(const std::array<float, 3>& value)
{
    return {value[0], value[1], value[2]};
}

// Las keys de un hueso en una acción, con el cuaternión de cada una (0x4f13d0 llama a 0x5fcd20 por
// key). Las posiciones y los ángulos vienen del mismo largo: LoadBmd lee los dos con la cantidad de
// keys de la acción.
std::vector<BoneKey> MakeKeys(const Data::Models::BoneKeys& keys)
{
    std::vector<BoneKey> made;
    made.reserve(keys.positions.size());
    for (std::size_t key = 0; key < keys.positions.size() && key < keys.rotations.size(); ++key)
    {
        const glm::vec3 angles = ToVec3(keys.rotations[key]);
        made.push_back(BoneKey{ToVec3(keys.positions[key]), angles, AngleQuaternion(angles)});
    }
    return made;
}
} // namespace

Core::Expected<Skeleton, SkeletonError> MakeSkeleton(const Data::Models::Model& model)
{
    Skeleton skeleton;
    skeleton.actions.reserve(model.actions.size());
    for (const Data::Models::Action& action : model.actions)
    {
        skeleton.actions.push_back(ActionInfo{true, 0.0f, action.keyCount, action.lockPositions});
    }

    skeleton.bones.reserve(model.bones.size());
    for (std::size_t index = 0; index < model.bones.size(); ++index)
    {
        const Data::Models::Bone& bone = model.bones[index];
        SkeletonBone made;
        made.dummy = bone.dummy;
        if (!bone.dummy)
        {
            // La pose concatena cada hueso con la matriz de su padre ya calculada: tiene que ir antes.
            if (bone.parent < -1 || (bone.parent >= 0 && static_cast<std::size_t>(bone.parent) >= index))
            {
                return Core::Unexpected(SkeletonError{index, bone.parent});
            }
            made.parent = bone.parent;
            made.actions.reserve(bone.actions.size());
            for (const Data::Models::BoneKeys& keys : bone.actions)
            {
                made.actions.push_back(MakeKeys(keys));
            }
        }
        skeleton.bones.push_back(std::move(made));
    }
    return skeleton;
}
} // namespace Client::Render::Models
