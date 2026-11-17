#include "Scenes/MonsterCloth.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skinning.h"
#include "Scenes/ClothCarry.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

namespace Client::Scenes
{
namespace
{
// 0x50bba2: 0,005f (0x3ba3d70a) por paso, cinco pasos; sin mirar +0x78 & 0x1000, como sí hace la de la clase 4.
constexpr float StepTime = std::bit_cast<float>(0x3ba3d70au);
constexpr int StepsPerFrame = 5;
constexpr float CenterRadius = 50.0f; // 0x42480000
constexpr float LowerRadius = 30.0f;  // 0x41f00000
constexpr float LowerOffset = -20.0f; // 0xc1a00000

const Render::Models::SkinnedMesh* ClothMeshOf(std::span<Render::Models::SkinnedMesh> meshes)
{
    constexpr auto Index = static_cast<std::size_t>(MeshClothMesh);
    return Index < meshes.size() ? &meshes[Index] : nullptr;
}
} // namespace

Core::Expected<void, std::string> MonsterMeshCloth::Frame(const Data::Models::Model& model, std::uint16_t type,
                                                          std::span<Render::Models::SkinnedMesh> meshes,
                                                          const Render::Models::ClothBody& body,
                                                          Render::Models::ClothWorld& world, std::uint8_t map)
{
    constexpr auto Index = static_cast<std::size_t>(MeshClothMesh);
    const Render::Models::SkinnedMesh* skinned = ClothMeshOf(meshes);
    if (Index >= model.meshes.size() || skinned == nullptr ||
        skinned->positions.size() < model.meshes[Index].vertices.size())
        return Core::Unexpected(
            std::format("0x455f06: el modelo {:#x} no tiene la malla 2 de la tela sobre la malla", type));
    if (!m_cloth)
    {
        // 0x50bb0b..0x50bb8f: new de una, 0x455e30 y el +0x24 (0x455ea0) con (objeto, 2, 0x12, 0x400, −1).
        Render::Models::Cloth& cloth = m_cloth.emplace();
        cloth.BuildOnTheMesh(model.meshes[Index], MeshClothMesh, MeshClothBone, MeshClothFlags, skinned->positions);
        cloth.AddSphere({0.0f, 0.0f, 0.0f}, CenterRadius, MeshClothBone);
        cloth.AddSphere({0.0f, LowerOffset, 0.0f}, LowerRadius, MeshClothBone);
    }
    Render::Models::ClothBody onTheMesh = body;
    onTheMesh.meshVertices = skinned->positions;
    m_simulatedBody = SimulatedBodyOf(onTheMesh);
    if (!m_cloth->Simulate(StepTime, StepsPerFrame, onTheMesh, world, map))
    {
        // 0x50bbb5: 0x4fb1a0(personaje, objeto, 0); el frame siguiente, con +0x190 en 0, la arma de nuevo.
        Release();
        return {};
    }
    // 0x50bbc7: el +0x18 con (0, 0).
    m_cloth->WriteTheMesh(meshes[Index].positions);
    return {};
}

void MonsterMeshCloth::DrawCurrent(std::span<Render::Models::SkinnedMesh> meshes,
                                   const Render::Models::ClothBody& body) const
{
    constexpr auto Index = static_cast<std::size_t>(MeshClothMesh);
    if (!m_cloth || Index >= meshes.size())
        return;
    Render::Models::SkinnedMesh& skinned = meshes[Index];
    m_cloth->WriteTheMesh(skinned.positions);
    if (!m_simulatedBody)
        return;
    const auto carry = ClothCarryOf(*m_simulatedBody, body, MeshClothBone);
    if (!carry)
        return;
    const auto particles = m_cloth->Particles();
    for (std::size_t vertex = 0; vertex < particles.size() && vertex < skinned.positions.size(); ++vertex)
        skinned.positions[vertex] = carry->Carried(particles[vertex].position);
}

void MonsterMeshCloth::Release()
{
    if (m_cloth)
        m_cloth->Release();
    m_cloth.reset();
    m_simulatedBody.reset();
}
} // namespace Client::Scenes
