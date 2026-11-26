#include "Models/Skinning.h"

#include "Data/Models/Model.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRotation.h"
#include "Models/Pose.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Client::Models
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

// Cada normal, solo girada por su hueso (0x5fcbd0).
void RotateNormals(const Data::Models::Mesh& mesh, std::span<const Math::BoneTransform> bones, SkinnedMesh& out)
{
    out.normals.clear();
    out.normals.reserve(mesh.normals.size());
    for (const Data::Models::Normal& normal : mesh.normals)
    {
        out.normals.push_back(Math::VectorRotate(ToVec3(normal.normal), bones[static_cast<std::size_t>(normal.bone)]));
    }
}
} // namespace

bool SkinMesh(const Data::Models::Mesh& mesh, std::span<const Math::BoneTransform> bones, const ObjectTransform& object,
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
        const Math::BoneTransform& bone = bones[static_cast<std::size_t>(vertex.bone)];
        out.positions.push_back(Math::VectorTransform(ToVec3(vertex.position), bone) * object.scale + object.position);
    }
    RotateNormals(mesh, bones, out);
    return true;
}

bool SkinMeshInWorld(const Data::Models::Mesh& mesh, std::span<const Math::BoneTransform> worldBones, SkinnedMesh& out)
{
    if (!BonesExist(mesh, worldBones.size()))
    {
        return false;
    }
    // 0x4ebde2 y 0x4ebf70: 0x5fccc0 y 0x5fcbd0 leen la 3 × 4 del hueso por filas.
    std::vector<Math::OriginalMatrix> matrices;
    matrices.reserve(worldBones.size());
    for (const Math::BoneTransform& bone : worldBones)
    {
        matrices.push_back(Math::OriginalMatrixOf(bone));
    }
    out.positions.clear();
    out.positions.reserve(mesh.vertices.size());
    for (const Data::Models::Vertex& vertex : mesh.vertices)
    {
        out.positions.push_back(
            Math::OriginalVectorTransform(ToVec3(vertex.position), matrices[static_cast<std::size_t>(vertex.bone)]));
    }
    out.normals.clear();
    out.normals.reserve(mesh.normals.size());
    for (const Data::Models::Normal& normal : mesh.normals)
    {
        out.normals.push_back(
            Math::OriginalVectorRotate(ToVec3(normal.normal), matrices[static_cast<std::size_t>(normal.bone)]));
    }
    return true;
}

bool SkinMeshAroundBones(const Data::Models::Mesh& mesh, std::span<const Math::BoneTransform> bones,
                         const ObjectTransform& object, float boneScale, SkinnedMesh& out)
{
    if (boneScale == 1.0f)
    {
        return SkinMesh(mesh, bones, object, out);
    }
    if (!BonesExist(mesh, bones.size()))
    {
        return false;
    }
    out.positions.clear();
    out.positions.reserve(mesh.vertices.size());
    for (const Data::Models::Vertex& vertex : mesh.vertices)
    {
        const Math::BoneTransform& bone = bones[static_cast<std::size_t>(vertex.bone)];
        const glm::vec3 around = Math::VectorRotate(ToVec3(vertex.position), bone) * boneScale + glm::vec3(bone[3]);
        out.positions.push_back(around * object.scale + object.position);
    }
    RotateNormals(mesh, bones, out);
    return true;
}

float NormalLight(const glm::vec3& worldNormal)
{
    // La de DAT_006c8818 en 1 (0x4ebca1..0x4ebcaf): nadie escribe esa bandera.
    constexpr glm::vec3 Light{1.3f, 0.0f, 2.0f};
    constexpr float Gain = 0.8f;    // 0x6b8a38
    constexpr float Ambient = 0.4f; // 0x6b87b4
    constexpr float Darkest = 0.2f; // 0x6b8854
    // En el orden de 0x4ebc80: y, x y z.
    const float lit = (Light.y * worldNormal.y + Light.x * worldNormal.x + Light.z * worldNormal.z) * Gain + Ambient;
    return lit < Darkest ? Darkest : lit;
}

glm::vec3 FlattenOnTheGround(const glm::vec3& world, const glm::vec3& objectPosition, float spread)
{
    constexpr float Horizon = 4000.0f; // 0x6b8c14
    constexpr float Lift = 5.0f;       // 0x6b87f8
    // 0x4f0040..0x4f0074: x' y z' quedan en la pila sin guardar y la X sale en double con un solo FSTP; la y
    // pasa por float (0x4f0052) antes de sumarle py.
    const double x = static_cast<double>(world.x) - objectPosition.x;
    const double z = static_cast<double>(world.z) - objectPosition.z;
    const double flatX = (x + spread) * z / (z - Horizon) + x + objectPosition.x;
    return {static_cast<float>(flatX), (world.y - objectPosition.y) + objectPosition.y, objectPosition.z + Lift};
}
} // namespace Client::Models
