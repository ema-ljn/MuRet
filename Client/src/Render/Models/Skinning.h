#pragma once

#include "Data/Models/Model.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Pose.h"

#include <glm/ext/vector_float3.hpp>

#include <span>
#include <vector>

// Los vértices de una malla en el mundo, como 0x4ebc80: el skinning rígido, un hueso por vértice y
// sin pesos (Client/docs/research/06-render-escenas-y-ui.md, "El personaje, vértice a vértice").
namespace Client::Render::Models
{
struct SkinnedMesh
{
    // En el mundo, uno por vértice de la malla.
    std::vector<glm::vec3> positions;
    // Giradas, una por normal: sin la escala ni la posición del objeto.
    std::vector<glm::vec3> normals;
};

// 0x4ebc80 con su último argumento en 1, como dibuja 0x60cf70 cada parte de un personaje con los
// huesos que posó 0x6001b0: cada vértice por la matriz de su hueso (0x5fccc0), por la escala del
// objeto y más su posición; cada normal, solo girada (0x5fcbd0). Devuelve false, sin escribir, si un
// vértice o una normal nombra un hueso que no está en `bones`: el original leería fuera del arreglo.
[[nodiscard]] bool SkinMesh(const Data::Models::Mesh& mesh, std::span<const BoneTransform> bones,
                            const ObjectTransform& object, SkinnedMesh& out);

// 0x4ebc80 con DAT_006c881c en `boneScale`, como lo pone 0x60cd60 para el contorno de 0x60cf70: cada
// vértice girado por su hueso (0x5fcbd0), por `boneScale` y más la traslación del hueso, y después
// por la escala del objeto y más su posición. Las normales, como SkinMesh. Con `boneScale` en 1
// justo, el original compara con 1,0 (0x4ebd2f) y toma el camino de SkinMesh.
[[nodiscard]] bool SkinMeshAroundBones(const Data::Models::Mesh& mesh, std::span<const BoneTransform> bones,
                                       const ObjectTransform& object, float boneScale, SkinnedMesh& out);

// 0x4eff40: un vértice de la sombra, aplastado sobre el piso desde la posición del objeto. Con x' y
// z' desde ella, X = (x' + 2000) · z' / (z' − 4000) + x' + px, la y queda y Z = pz + 5 (2500 en vez de
// 2000 en el mundo 30, que MuRet todavía no tiene).
[[nodiscard]] glm::vec3 FlattenOnTheGround(const glm::vec3& world, const glm::vec3& objectPosition);
} // namespace Client::Render::Models
