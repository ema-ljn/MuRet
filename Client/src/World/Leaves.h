#pragma once

#include "World/TerrainHeight.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Client::Data::Terrain
{
struct TerrainAttributes;
struct TerrainHeights;
} // namespace Client::Data::Terrain

namespace Client::Render::Models
{
class CrtRandom;
} // namespace Client::Render::Models

// Las hojas (CL7.6 B3): la tabla de 0x530490 (0x75e34b8, 200 lugares de 0x78 bytes), que en cada mapa son
// otra cosa (lluvia, hojas, nieve, burbujas); en el login (el mapa 0x37) son 80 brasas de
// Effect\FireSnuff.jpg que nacen alrededor del héroe (0x42b1e0), caen con su velocidad al azar
// (0x5303a0) y en el suelo se apagan, y su dibujo, 0x5306a0. Contrato: 06 § Los efectos del login.
// Verificado bit a bit contra el código real emulado (tests/vectors/world55/leaves.txt).
namespace Client::World
{
inline constexpr std::size_t LeafCount = 200;
// Cuántas usa el mapa 0x37 (0x5304e6).
inline constexpr std::size_t LoginEmberCount = 80;
inline constexpr std::uint16_t EmberTexture = 0x635;

struct Leaf
{
    bool alive = false;    // +0x0
    std::int32_t type = 0; // +0x4, la textura
    float scale = 0.0f;    // +0x10
    glm::vec3 position{};  // +0x14
    glm::vec3 angles{};    // +0x20: las brasas no los escriben (Q-C139)
    glm::vec3 light{};     // +0x2c
    glm::vec3 velocity{};  // +0x54
    glm::vec3 spin{};      // +0x60: el creador los sortea; las brasas no los usan
    glm::vec3 start{};     // +0x6c, donde nació
};

class Leaves
{
public:
    [[nodiscard]] std::span<Leaf> All();
    [[nodiscard]] std::span<const Leaf> All() const;
    [[nodiscard]] bool Any() const;
    // 0x6070a0 al entrar: el byte vivo de cada una.
    void Clear();

private:
    // En el heap: la escena que la lleva tiene que caber en la pila.
    std::vector<Leaf> m_leaves = std::vector<Leaf>(LeafCount);
};

// Lo que leen las brasas: la posición del héroe (0x743a170 + 0x10), la y de la cámara (0x7d36888) y el
// terreno, para 0x5f93c0.
struct EmberWorld
{
    glm::vec3 hero{};
    float cameraY = 0.0f;
    const Data::Terrain::TerrainHeights* heights = nullptr;
    const Data::Terrain::TerrainAttributes* attributes = nullptr;
    float fixedHeight = FixedTerrainHeight;
};

// 0x530490 en el mapa 0x37, con el rand() de la CRT: las 80 primeras, en orden; la apagada nace (con la luz
// en 1 y el byte vivo, 0x42b1e0) y la viva cae (0x5303a0), las dos en el mismo paso para lugares
// distintos. Sin el terreno no corre.
void StepLoginEmbers(Leaves& leaves, const EmberWorld& world, Render::Models::CrtRandom& random);

// Lo que 0x5306a0 le pide a GL por una brasa viva: glTranslatef a su posición y el cuadrado de 0x621320
// con a = 2 · escala y b = 4 · escala, sus cuatro esquinas por AngleMatrix de los ángulos (0x5fccc0), con
// las UV (0, 1), (1, 1), (1, 0) y (0, 0).
struct LeafQuad
{
    glm::vec3 translation{};
    std::array<glm::vec3, 4> corners{};
    std::array<glm::vec2, 4> uv{};
};
[[nodiscard]] LeafQuad LeafQuadOf(const Leaf& leaf);
} // namespace Client::World
