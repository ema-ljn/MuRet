#include "Render/Models/Skinning.h"

#include "Data/Models/Model.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Pose.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Render::Models
{
namespace
{
glm::vec3 ToVec3(const std::array<float, 3>& value)
{
    return {value[0], value[1], value[2]};
}

bool IsABone(std::int16_t bone, std::size_t count)
{
    return bone >= 0 && static_cast<std::size_t>(bone) < count;
}

// Si cada vértice y cada normal nombra un hueso de `bones`.
bool BonesExist(const Data::Models::Mesh& mesh, std::size_t count)
{
    for (const Data::Models::Vertex& vertex : mesh.vertices)
    {
        if (!IsABone(vertex.bone, count))
        {
            return false;
        }
    }
    for (const Data::Models::Normal& normal : mesh.normals)
    {
        if (!IsABone(normal.bone, count))
        {
            return false;
        }
    }
    return true;
}
} // namespace

bool SkinMesh(const Data::Models::Mesh& mesh, std::span<const BoneTransform> bones, const ObjectTransform& object,
              SkinnedMesh& out)
{
    if (!BonesExist(mesh, bones.size()))
    {
        return false;
    }
    out.positions.clear();
    out.positions.reserve(mesh.vertices.size());
    for (const Data::Models::Vertex& vertex : mesh.vertices)
    {
        const BoneTransform& bone = bones[static_cast<std::size_t>(vertex.bone)];
        out.positions.push_back(VectorTransform(ToVec3(vertex.position), bone) * object.scale + object.position);
    }
    out.normals.clear();
    out.normals.reserve(mesh.normals.size());
    for (const Data::Models::Normal& normal : mesh.normals)
    {
        out.normals.push_back(VectorRotate(ToVec3(normal.normal), bones[static_cast<std::size_t>(normal.bone)]));
    }
    return true;
}
} // namespace Client::Render::Models
