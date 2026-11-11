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

// Cada normal, solo girada por su hueso (0x5fcbd0).
void RotateNormals(const Data::Models::Mesh& mesh, std::span<const BoneTransform> bones, SkinnedMesh& out)
{
    out.normals.clear();
    out.normals.reserve(mesh.normals.size());
    for (const Data::Models::Normal& normal : mesh.normals)
    {
        out.normals.push_back(VectorRotate(ToVec3(normal.normal), bones[static_cast<std::size_t>(normal.bone)]));
    }
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
    RotateNormals(mesh, bones, out);
    return true;
}

bool SkinMeshAroundBones(const Data::Models::Mesh& mesh, std::span<const BoneTransform> bones,
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
        const BoneTransform& bone = bones[static_cast<std::size_t>(vertex.bone)];
        const glm::vec3 around = VectorRotate(ToVec3(vertex.position), bone) * boneScale + glm::vec3(bone[3]);
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
    const float x = world.x - objectPosition.x;
    const float z = world.z - objectPosition.z;
    return {((x + spread) * z) / (z - Horizon) + x + objectPosition.x, (world.y - objectPosition.y) + objectPosition.y,
            objectPosition.z + Lift};
}

float ShadowSpreadOf(std::int32_t map)
{
    constexpr std::int32_t CastleSiegeMap = 0x1e;
    constexpr float Spread = 2000.0f;
    constexpr float CastleSiegeSpread = 2500.0f;
    return map == CastleSiegeMap ? CastleSiegeSpread : Spread;
}
} // namespace Client::Render::Models
