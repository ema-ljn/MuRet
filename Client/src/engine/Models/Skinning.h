#pragma once

#include "Data/Models/Model.h"
#include "EngineApi.h"
#include "Math/HalfLifeMath.h"
#include "Models/Pose.h"

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
    // Giradas, una por normal, como las deja 0x4ebc80 en 0x68c53bc: solo por la matriz de su hueso (0x5fcbd0), sin la
    // posición del objeto. Con la escala dentro solo si el hueso la trae: los huesos del mundo de 0x4eb860 con la
    // bandera en 1 (SkinMeshInWorld); con Translate en 1 (SkinMesh), sin ella. Quien las dibuja no les agrega nada.
    std::vector<glm::vec3> normals;
};

// 0x4ebc80 con su último argumento en 1, como dibuja 0x60cf70 cada parte de un personaje con los
// huesos que posó 0x6001b0: cada vértice por la matriz de su hueso (0x5fccc0), por la escala del
// objeto y más su posición; cada normal, solo girada (0x5fcbd0). Devuelve false, sin escribir, si un
// vértice o una normal nombra un hueso que no está en `bones`: el original leería fuera del arreglo.
[[nodiscard]] ENGINE_API bool SkinMesh(const Data::Models::Mesh& mesh, std::span<const BoneTransform> bones,
                                       const ObjectTransform& object, SkinnedMesh& out);

// 0x4ebc80 con su último argumento en 0, como lo corre 0x5ffdf0 (0x600154 y 0x6001a1) después de un 0x4eb860 con la
// bandera en 1 (PoseWorldSkeleton): cada vértice solo por la matriz de su hueso en `worldBones` (0x5fccc0), que ya trae
// la escala y el origen del objeto, y cada normal girada por esa misma matriz (0x5fcbd0), con la escala dentro. Las dos
// con los redondeos de la x87 (OriginalVectorTransform y OriginalVectorRotate). Devuelve false como SkinMesh si un
// vértice o una normal nombra un hueso que falta.
[[nodiscard]] ENGINE_API bool SkinMeshInWorld(const Data::Models::Mesh& mesh, std::span<const BoneTransform> worldBones,
                                              SkinnedMesh& out);

// 0x4ebc80 con DAT_006c881c en `boneScale`, como lo pone 0x60cd60 para el contorno de 0x60cf70: cada
// vértice girado por su hueso (0x5fcbd0), por `boneScale` y más la traslación del hueso, y después
// por la escala del objeto y más su posición. Las normales, como SkinMesh. Con `boneScale` en 1
// justo, el original compara con 1,0 (0x4ebd2f) y toma el camino de SkinMesh.
[[nodiscard]] ENGINE_API bool SkinMeshAroundBones(const Data::Models::Mesh& mesh, std::span<const BoneTransform> bones,
                                                  const ObjectTransform& object, float boneScale, SkinnedMesh& out);

// La luz por vértice de 0x4ebc80, con +0x44 del modelo (la copia de +0xe8 del objeto): por cada normal
// girada por su hueso, sin normalizar, max(0,2; 0,8 · L·n + 0,4), con L = (1,3; 0; 2). Es la rama de
// DAT_006c8818, que vale 1 desde el .data y nadie escribe: las de (0; −1,5; 0) y del mundo 30 no corren
// nunca, y el ángulo con que se gira L (+0x8c del modelo) solo lo escribe la del mundo 30.
[[nodiscard]] ENGINE_API float NormalLight(const glm::vec3& worldNormal);

// 0x4eff40: with x' and z' relative to the object, X = (x' + spread) * z' / (z' - 4000) + x' + px and Z = pz + 5.
[[nodiscard]] ENGINE_API glm::vec3 FlattenOnTheGround(const glm::vec3& world, const glm::vec3& objectPosition,
                                                      float spread);
} // namespace Client::Render::Models
