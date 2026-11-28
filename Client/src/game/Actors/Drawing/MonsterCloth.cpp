#include "Actors/Drawing/MonsterCloth.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Maps/ClothWind.h"
#include "Models/Cloth.h"
#include "Models/Skinning.h"
#include "Actors/Drawing/ClothCarry.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

namespace Client::Actors::Drawing
{
namespace
{
// 0x50bba2: 0.005f (0x3ba3d70a) per step, five steps; without checking +0x78 & 0x1000, as the class 4 one does.
constexpr float StepTime = std::bit_cast<float>(0x3ba3d70au);
constexpr int StepsPerFrame = 5;
constexpr float CenterRadius = 50.0f; // 0x42480000
constexpr float LowerRadius = 30.0f;  // 0x41f00000
constexpr float LowerOffset = -20.0f; // 0xc1a00000

const Models::SkinnedMesh* ClothMeshOf(std::span<Models::SkinnedMesh> meshes)
{
    constexpr auto Index = static_cast<std::size_t>(MeshClothMesh);
    return Index < meshes.size() ? &meshes[Index] : nullptr;
}
} // namespace

Core::Expected<void, std::string> MonsterMeshCloth::Frame(const Data::Models::Model& model, std::uint16_t type,
                                                          std::span<Models::SkinnedMesh> meshes,
                                                          const Models::ClothBody& body, Models::ClothWorld& world,
                                                          std::uint8_t map)
{
    constexpr auto Index = static_cast<std::size_t>(MeshClothMesh);
    const Models::SkinnedMesh* skinned = ClothMeshOf(meshes);
    if (Index >= model.meshes.size() || skinned == nullptr ||
        skinned->positions.size() < model.meshes[Index].vertices.size())
        return Core::Unexpected(
            std::format("0x455f06: el modelo {:#x} no tiene la malla 2 de la tela sobre la malla", type));
    if (!m_cloth)
    {
        // 0x50bb0b..0x50bb8f: a new at once, 0x455e30 and the +0x24 (0x455ea0) with (object, 2, 0x12, 0x400, −1).
        Models::Cloth& cloth = m_cloth.emplace();
        cloth.BuildOnTheMesh(model.meshes[Index], MeshClothMesh, MeshClothBone, MeshClothFlags, skinned->positions);
        cloth.AddSphere({0.0f, 0.0f, 0.0f}, CenterRadius, MeshClothBone);
        cloth.AddSphere({0.0f, LowerOffset, 0.0f}, LowerRadius, MeshClothBone);
    }
    Models::ClothBody onTheMesh = body;
    onTheMesh.meshVertices = skinned->positions;
    m_simulatedBody = Actors::Drawing::SimulatedBodyOf(onTheMesh);
    if (!m_cloth->Simulate(StepTime, StepsPerFrame, onTheMesh, world, Maps::ClothWindForMap(map)))
    {
        // 0x50bbb5: 0x4fb1a0(character, object, 0); the next frame, with +0x190 at 0, builds it again.
        Release();
        return {};
    }
    // 0x50bbc7: the +0x18 with (0, 0).
    m_cloth->WriteTheMesh(meshes[Index].positions);
    return {};
}

void MonsterMeshCloth::DrawCurrent(std::span<Models::SkinnedMesh> meshes, const Models::ClothBody& body) const
{
    constexpr auto Index = static_cast<std::size_t>(MeshClothMesh);
    if (!m_cloth || Index >= meshes.size())
        return;
    Models::SkinnedMesh& skinned = meshes[Index];
    m_cloth->WriteTheMesh(skinned.positions);
    if (!m_simulatedBody)
        return;
    const auto carry = Actors::Drawing::ClothCarryOf(*m_simulatedBody, body, MeshClothBone);
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
} // namespace Client::Actors::Drawing
