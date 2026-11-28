#pragma once

#include "Terrain/TerrainHeight.h"

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

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

// The leaves (CL7.6 B3): the table of 0x530490 (0x75e34b8, 200 slots of 0x78 bytes), which are something else on each
// map (rain, leaves, snow, bubbles); in the login (map 0x37) they are 80 embers of Effect\FireSnuff.jpg that are born
// around the hero (0x42b1e0), fall with their random velocity (0x5303a0) and go out on the ground, and their draw,
// 0x5306a0. Contract: 06 § Los efectos del login. Verified bit for bit against the emulated real code
// (tests/vectors/world55/leaves.txt).
namespace Client::Weather
{
inline constexpr std::size_t LeafCount = 200;
// 0x5304e6
inline constexpr std::size_t LoginEmberCount = 80;
inline constexpr std::uint16_t EmberTexture = 0x635;

struct Leaf
{
    bool alive = false;    // +0x0
    std::int32_t type = 0; // +0x4, the texture
    float scale = 0.0f;    // +0x10
    glm::vec3 position{};  // +0x14
    glm::vec3 angles{};    // +0x20: the embers do not write them (Q-C139)
    glm::vec3 light{};     // +0x2c
    glm::vec3 velocity{};  // +0x54
    glm::vec3 spin{};      // +0x60: the creator rolls them; the embers do not use them
    glm::vec3 start{};     // +0x6c, where it was born
};

class Leaves
{
public:
    [[nodiscard]] std::span<Leaf> All();
    [[nodiscard]] std::span<const Leaf> All() const;
    [[nodiscard]] bool Any() const;
    // 0x6070a0 on entering: each one's live byte.
    void Clear();

private:
    // On the heap: the scene that holds it has to fit on the stack.
    std::vector<Leaf> m_leaves = std::vector<Leaf>(LeafCount);
};

// What the embers read: the hero's position (0x743a170 + 0x10), the camera's y (0x7d36888) and the terrain, for
// 0x5f93c0.
struct EmberWorld
{
    glm::vec3 hero{};
    float cameraY = 0.0f;
    const Data::Terrain::TerrainHeights* heights = nullptr;
    const Data::Terrain::TerrainAttributes* attributes = nullptr;
    float fixedHeight = Terrain::FixedTerrainHeight;
};

// 0x530490 on map 0x37, with the CRT rand(): the first 80, in order; the dead one is born (with the light at 1 and the
// live byte, 0x42b1e0) and the live one falls (0x5303a0), both in the same step for different slots. Without the
// terrain it does not run.
void StepLoginEmbers(Leaves& leaves, const EmberWorld& world, Crt::CrtRandom& random);

// The twin creators 0x52fa60 (map 0) and 0x52fe30 (3/7): nine rand(), without writing scale or angles.
void SpawnDriftingLeaf(Leaf& leaf, const EmberWorld& world, Crt::CrtRandom& random);
// 0x5303a0: the shared fallback of the game and the embers; needs a valid terrain.
void StepFallingLeaf(Leaf& leaf, const EmberWorld& world, Crt::CrtRandom& random);

// What 0x5306a0 asks GL for per live ember: glTranslatef to its position and the square of 0x621320 with
// a = 2 · scale and b = 4 · scale, its four corners by AngleMatrix of the angles (0x5fccc0), with the UVs (0, 1),
// (1, 1), (1, 0) and (0, 0).
struct LeafQuad
{
    glm::vec3 translation{};
    std::array<glm::vec3, 4> corners{};
    std::array<glm::vec2, 4> uv{};
};
[[nodiscard]] LeafQuad LeafQuadOf(const Leaf& leaf);
} // namespace Client::Weather
