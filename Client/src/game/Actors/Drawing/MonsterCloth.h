#pragma once

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Models/Cloth.h"
#include "Models/Skinning.h"
#include "Actors/Drawing/ClothCarry.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

// The cloth on a monster's mesh (0x50bae7..0x50bbca, 08 P-C22 f): the class of vtable 0x6b8d04
// (Models::Cloth::BuildOnTheMesh) at object + 0x190, between the pose of 0x5ffdf0 and the body of 0x6002e0.
// Contract: 06 § El dibujo de los monstruos y los NPC, "La tela sobre la malla".
namespace Client::Actors::Drawing
{
// 0x50bb3b..0x50bb84: mesh 2, bone 0x12, flags 0x400 (strong gravity, no wind of its own) and two spheres on bone
// 0x12: (0, 0, 0) of radius 50 and (0, −20, 0) of radius 30.
inline constexpr std::int32_t MeshClothMesh = 2;
inline constexpr std::int32_t MeshClothBone = 0x12;
inline constexpr std::uint32_t MeshClothFlags = 0x400;

class MonsterMeshCloth
{
public:
    // One frame of the original: with +0x190 at 0, 0x6a0210 builds it on mesh 2 of `meshes` (what 0x4ebc80 left at
    // 0x5786f9c) and 0x455cd0 gives it its two spheres; then 0x455190(0.005f, 5) with `body` (its own bones and, in
    // meshVertices, mesh 2 of `meshes`). If a step breaks it, 0x4fb1a0 releases it and the mesh stays as 0x4ebc80 left
    // it; otherwise the +0x18 (0x4568f0) writes its particles into mesh 2 of `meshes`, which the RenderBody of 0x6002e0
    // draws. Without mesh 2 in `model` or in `meshes`, it cuts with 0x455f06 before building it.
    [[nodiscard]] Core::Expected<void, std::string> Frame(const Data::Models::Model& model, std::uint16_t type,
                                                          std::span<Models::SkinnedMesh> meshes,
                                                          const Models::ClothBody& body, Models::ClothWorld& world,
                                                          std::uint8_t map);

    // A draw between frames of the original (what MuRet adds, #307): without building or simulating it, its particles
    // to mesh 2 of `meshes`, carried with bone 0x12 from the body of its last simulation to `body`. Without cloth,
    // nothing.
    void DrawCurrent(std::span<Models::SkinnedMesh> meshes, const Models::ClothBody& body) const;

    // 0x4fb1a0: releases it (+0x190 and +0x18c at 0).
    void Release();

    // object + 0x190, or none.
    [[nodiscard]] const std::optional<Models::Cloth>& Cloth() const
    {
        return m_cloth;
    }

private:
    std::optional<Models::Cloth> m_cloth;
    std::optional<Actors::Drawing::SimulatedClothBody> m_simulatedBody;
};
} // namespace Client::Actors::Drawing
