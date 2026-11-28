#pragma once

#include "Data/Models/Model.h"
#include "EngineApi.h"
#include "Math/HalfLifeMath.h"
#include "Models/Pose.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <span>
#include <vector>

// A mesh's world vertices, like 0x4ebc80: rigid skinning, one bone per vertex and no weights
// (Client/docs/research/06-render-escenas-y-ui.md, "El personaje, vértice a vértice").
namespace Client::Models
{
struct SkinnedMesh
{
    // In world space, one per mesh vertex.
    std::vector<glm::vec3> positions;
    // Rotated, one per normal, as 0x4ebc80 leaves them in 0x68c53bc: only by their bone's matrix (0x5fcbd0), without
    // the object's position. With the scale inside only if the bone carries it: the world bones of 0x4eb860 with the
    // flag at 1 (SkinMeshInWorld); with Translate at 1 (SkinMesh), without it. Whoever draws them adds nothing.
    std::vector<glm::vec3> normals;
};

// A model's skinned meshes in buffers that outlive the draw. Reset hands out the first `count`, emptied; the ones past
// it keep their capacity for a bigger model, so skinning model after model allocates only while the biggest grows them.
class SkinnedMeshes
{
public:
    [[nodiscard]] std::span<SkinnedMesh> Reset(std::size_t count)
    {
        if (m_meshes.size() < count)
            m_meshes.resize(count);
        const std::span<SkinnedMesh> meshes = std::span(m_meshes).first(count);
        for (SkinnedMesh& mesh : meshes)
        {
            mesh.positions.clear();
            mesh.normals.clear();
        }
        return meshes;
    }

private:
    std::vector<SkinnedMesh> m_meshes;
};

// 0x4ebc80 with its last argument at 1, as 0x60cf70 draws each part of a character with the bones 0x6001b0 posed: each
// vertex by its bone's matrix (0x5fccc0), times the object's scale plus its position; each normal, only rotated
// (0x5fcbd0). Returns false, without writing, if a vertex or a normal names a bone not in `bones`: the original would
// read outside the array.
[[nodiscard]] ENGINE_API bool SkinMesh(const Data::Models::Mesh& mesh, std::span<const Math::BoneTransform> bones,
                                       const ObjectTransform& object, SkinnedMesh& out);

// 0x4ebc80 with its last argument at 0, as 0x5ffdf0 runs it (0x600154 and 0x6001a1) after a 0x4eb860 with the flag at
// 1 (PoseWorldSkeleton): each vertex only by its bone's matrix in `worldBones` (0x5fccc0), which already carries the
// object's scale and origin, and each normal rotated by that same matrix (0x5fcbd0), with the scale inside. Both with
// the x87 roundings (OriginalVectorTransform and OriginalVectorRotate). Returns false like SkinMesh if a vertex or a
// normal names a missing bone.
[[nodiscard]] ENGINE_API bool SkinMeshInWorld(const Data::Models::Mesh& mesh,
                                              std::span<const Math::BoneTransform> worldBones, SkinnedMesh& out);

// 0x4ebc80 with DAT_006c881c in `boneScale`, as 0x60cd60 sets it for the outline of 0x60cf70: each vertex rotated by
// its bone (0x5fcbd0), times `boneScale` plus the bone's translation, then times the object's scale plus its position.
// Normals as in SkinMesh. With `boneScale` exactly 1, the original compares with 1.0 (0x4ebd2f) and takes the SkinMesh
// path.
[[nodiscard]] ENGINE_API bool SkinMeshAroundBones(const Data::Models::Mesh& mesh,
                                                  std::span<const Math::BoneTransform> bones,
                                                  const ObjectTransform& object, float boneScale, SkinnedMesh& out);

// The per-vertex light of 0x4ebc80, with model +0x44 (the copy of object +0xe8): for each normal rotated by its bone,
// unnormalized, max(0.2; 0.8 · L·n + 0.4), with L = (1.3; 0; 2). It is the DAT_006c8818 branch, which is 1 from .data
// and nobody writes: the ones with (0; −1.5; 0) and of world 30 never run, and the angle L is rotated by (model +0x8c)
// is only written by the world 30 one.
[[nodiscard]] ENGINE_API float NormalLight(const glm::vec3& worldNormal);

// 0x4eff40: with x' and z' relative to the object, X = (x' + spread) * z' / (z' - 4000) + x' + px and Z = pz + 5.
[[nodiscard]] ENGINE_API glm::vec3 FlattenOnTheGround(const glm::vec3& world, const glm::vec3& objectPosition,
                                                      float spread);
} // namespace Client::Models
