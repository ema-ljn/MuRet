#pragma once

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skinning.h"
#include "Scenes/ClothCarry.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

// La tela sobre la malla de un monstruo (0x50bae7..0x50bbca, 08 P-C22 f): la clase de la vtable 0x6b8d04
// (Render::Models::Cloth::BuildOnTheMesh) en objeto + 0x190, entre la pose de 0x5ffdf0 y el cuerpo de 0x6002e0.
// Contrato: 06 § El dibujo de los monstruos y los NPC, "La tela sobre la malla".
namespace Client::Scenes
{
// 0x50bb3b..0x50bb84: la malla 2, el hueso 0x12, las banderas 0x400 (la gravedad fuerte, sin viento propio) y dos
// esferas en el hueso 0x12: (0, 0, 0) de radio 50 y (0, −20, 0) de radio 30.
inline constexpr std::int32_t MeshClothMesh = 2;
inline constexpr std::int32_t MeshClothBone = 0x12;
inline constexpr std::uint32_t MeshClothFlags = 0x400;

class MonsterMeshCloth
{
public:
    // Un frame del original: con +0x190 en 0, 0x6a0210 la arma sobre la malla 2 de `meshes` (lo que dejó 0x4ebc80 en
    // 0x5786f9c) y 0x455cd0 le pone sus dos esferas; después 0x455190(0,005f, 5) con `body` (sus huesos propios y,
    // en meshVertices, la malla 2 de `meshes`). Si un paso la rompe, 0x4fb1a0 la suelta y la malla queda como la
    // dejó 0x4ebc80; si no, el +0x18 (0x4568f0) escribe sus partículas en la malla 2 de `meshes`, que dibuja el
    // RenderBody de 0x6002e0. Sin la malla 2 en `model` o en `meshes`, corta con 0x455f06 antes de armarla.
    [[nodiscard]] Core::Expected<void, std::string> Frame(const Data::Models::Model& model, std::uint16_t type,
                                                          std::span<Render::Models::SkinnedMesh> meshes,
                                                          const Render::Models::ClothBody& body,
                                                          Render::Models::ClothWorld& world, std::uint8_t map);

    // Un dibujo entre frames del original (lo que MuRet agrega, #307): sin armarla ni simularla, sus partículas a
    // la malla 2 de `meshes`, llevadas con el hueso 0x12 desde el cuerpo de su última simulación al de `body`.
    // Sin tela, nada.
    void DrawCurrent(std::span<Render::Models::SkinnedMesh> meshes, const Render::Models::ClothBody& body) const;

    // 0x4fb1a0: la suelta (+0x190 y +0x18c en 0).
    void Release();

    // objeto + 0x190, o ninguna.
    [[nodiscard]] const std::optional<Render::Models::Cloth>& Cloth() const
    {
        return m_cloth;
    }

private:
    std::optional<Render::Models::Cloth> m_cloth;
    std::optional<SimulatedClothBody> m_simulatedBody;
};
} // namespace Client::Scenes
