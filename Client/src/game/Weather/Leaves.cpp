#include "Weather/Leaves.h"

#include "Crt/CrtRandom.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Math/OriginalRotation.h"
#include "Terrain/TerrainHeight.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Weather
{
namespace
{
// 0x42b1e0, on map 0x37: the scale (rand() % 50) · 0.01 (0x6b86c0) + 0.5 (0x6b882c); the position, the hero plus
// (rand() % 1600 − 800, rand() % 1400 − 500, rand() % 1000 + 50); the x velocity (rand() % 64 + 64) · −0.1
// (0x6b8930), and if the birth y falls below the camera + 400 (0x6b87b0), 3.2 (0x6b8a6c) minus that; the others, in
// steps of 0.1 (0x6b87b8).
constexpr double ScaleStep = static_cast<double>(0.01f);
constexpr double ScaleBase = 0.5;
constexpr double FallSpeed = static_cast<double>(-0.1f);
constexpr double Tenth = Math::TenthAsDouble;
constexpr float CameraMargin = 400.0f;
constexpr float Bounce = 3.2f;
// 0x5303e8: on the ground, the light drops 0.05 (0x6b8a48) per step; at 0 it goes out.
constexpr double Fade = static_cast<double>(0.05f);

using Math::F32;

// rand() % n with the `and 0x8000000f` idiom (n a power of 2) or with cdq and idiv: with a non-negative rand(), both
// are the remainder.
int Around(Crt::CrtRandom& random, int span, int shift)
{
    return (random.Next() % span) + shift;
}

void Born(Leaf& leaf, const EmberWorld& world, Crt::CrtRandom& random, bool ember)
{
    constexpr std::int32_t DriftingLeafTexture = 100;
    leaf.type = ember ? EmberTexture : DriftingLeafTexture;
    if (ember)
        leaf.scale = F32((static_cast<double>(random.Next() % 50) * ScaleStep) + ScaleBase);
    const auto x = F32(static_cast<double>(Around(random, 1600, -800)) + static_cast<double>(world.hero.x));
    const auto y = F32(static_cast<double>(Around(random, 1400, -500)) + static_cast<double>(world.hero.y));
    constexpr int EmberHeightSpan = 1000;
    constexpr int DriftingHeightSpan = 300;
    const auto z = F32(static_cast<double>(Around(random, ember ? EmberHeightSpan : DriftingHeightSpan, 50)) +
                       static_cast<double>(world.hero.z));
    leaf.position = {x, y, z};
    leaf.start = leaf.position;
    const double fall = static_cast<double>(Around(random, 64, 64)) * FallSpeed;
    leaf.velocity.x = F32(fall);
    // `fcomp`: the camera + 400 against the y; with `test ah, 0x41` (less or equal, or unordered) it does not change.
    const double camera = static_cast<double>(world.cameraY) + static_cast<double>(CameraMargin);
    if (!(camera < static_cast<double>(y) || camera == static_cast<double>(y) || std::isnan(camera) || std::isnan(y)))
    {
        leaf.velocity.x = F32(static_cast<double>(Bounce) - fall);
    }
    leaf.velocity.y = F32(static_cast<double>(Around(random, 32, -16)) * Tenth);
    leaf.velocity.z = F32(static_cast<double>(Around(random, 32, -16)) * Tenth);
    leaf.spin.x = F32(static_cast<double>(Around(random, 16, -8)) * Tenth);
    leaf.spin.y = F32(static_cast<double>(Around(random, 64, -32)) * Tenth);
    leaf.spin.z = F32(static_cast<double>(Around(random, 16, -8)) * Tenth);
    if (ember)
        leaf.light = {1.0f, 0.0f, 0.0f};
}

// 0x5303a0: with the terrain below (or a NaN), in the air: each velocity component adds (rand() % 16 − 8) · 0.1 and the
// position, the velocity (the z, with z's before storing it in float). Otherwise, it stays on the ground and fades by
// 0.05; with the red at 0 or less, it dies.
void Fall(Leaf& leaf, const EmberWorld& world, Crt::CrtRandom& random)
{
    const double ground = Terrain::TerrainHeightAt(*world.heights, *world.attributes, leaf.position.x, leaf.position.y,
                                                   world.fixedHeight);
    const auto z = static_cast<double>(leaf.position.z);
    if (ground < z || std::isnan(ground) || std::isnan(z))
    {
        leaf.velocity.x =
            F32((static_cast<double>(Around(random, 16, -8)) * Tenth) + static_cast<double>(leaf.velocity.x));
        leaf.velocity.y =
            F32((static_cast<double>(Around(random, 16, -8)) * Tenth) + static_cast<double>(leaf.velocity.y));
        const double rise =
            (static_cast<double>(Around(random, 16, -8)) * Tenth) + static_cast<double>(leaf.velocity.z);
        leaf.velocity.z = F32(rise);
        leaf.position.x = F32(static_cast<double>(leaf.position.x) + static_cast<double>(leaf.velocity.x));
        leaf.position.y = F32(static_cast<double>(leaf.velocity.y) + static_cast<double>(leaf.position.y));
        leaf.position.z = F32(rise + z);
        return;
    }
    leaf.position.z = F32(ground);
    const double red = static_cast<double>(leaf.light.x) - Fade;
    leaf.light.x = F32(red);
    leaf.light.y = F32(static_cast<double>(leaf.light.y) - Fade);
    leaf.light.z = F32(static_cast<double>(leaf.light.z) - Fade);
    // `fcomp` with 0 and `test ah, 0x41`: greater goes on; less, equal or unordered, it dies.
    if (!(red > 0.0))
    {
        leaf.alive = false;
    }
}
} // namespace

std::span<Leaf> Leaves::All()
{
    return m_leaves;
}

std::span<const Leaf> Leaves::All() const
{
    return m_leaves;
}

bool Leaves::Any() const
{
    for (const Leaf& leaf : m_leaves)
    {
        if (leaf.alive)
        {
            return true;
        }
    }
    return false;
}

void Leaves::Clear()
{
    for (Leaf& leaf : m_leaves)
    {
        leaf.alive = false;
    }
}

void StepLoginEmbers(Leaves& leaves, const EmberWorld& world, Crt::CrtRandom& random)
{
    if (world.heights == nullptr || world.attributes == nullptr)
    {
        return;
    }
    const std::span<Leaf> all = leaves.All();
    for (std::size_t k = 0; k < LoginEmberCount; ++k)
    {
        Leaf& leaf = all[k];
        if (!leaf.alive)
        {
            // 0x5305a0: the light at 1 and the live byte, and then the map's creator.
            leaf.light = glm::vec3(1.0f);
            leaf.alive = true;
            Born(leaf, world, random, true);
        }
        else
        {
            Fall(leaf, world, random);
        }
    }
}

void SpawnDriftingLeaf(Leaf& leaf, const EmberWorld& world, Crt::CrtRandom& random)
{
    Born(leaf, world, random, false);
}

void StepFallingLeaf(Leaf& leaf, const EmberWorld& world, Crt::CrtRandom& random)
{
    Fall(leaf, world, random);
}

LeafQuad LeafQuadOf(const Leaf& leaf)
{
    // 0x5307e2: a = scale + scale, b = scale · 4 (0x6b8a44), both exact in float.
    const float a = F32(static_cast<double>(leaf.scale) + static_cast<double>(leaf.scale));
    const float b = F32(static_cast<double>(leaf.scale) * 4.0);
    const Math::OriginalMatrix turn = Math::OriginalAngleMatrix(leaf.angles);
    const std::array<glm::vec3, 4> local{{{-a, -a, -b}, {a, a, -b}, {a, a, b}, {-a, -a, b}}};
    LeafQuad quad;
    quad.translation = leaf.position;
    for (std::size_t k = 0; k < local.size(); ++k)
    {
        quad.corners[k] = Math::OriginalVectorTransform(local[k], turn);
    }
    quad.uv = {{{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}}};
    return quad;
}
} // namespace Client::Weather
