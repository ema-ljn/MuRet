#include "World/Leaves.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "World/TerrainHeight.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <span>

namespace Client::World
{
namespace
{
// 0x42b1e0, en el mapa 0x37: la escala (rand() % 50) · 0,01 (0x6b86c0) + 0,5 (0x6b882c); la posición, el
// héroe más (rand() % 1600 − 800, rand() % 1400 − 500, rand() % 1000 + 50); la velocidad x
// (rand() % 64 + 64) · −0,1 (0x6b8930), y si la y del nacimiento queda debajo de la cámara + 400
// (0x6b87b0), 3,2 (0x6b8a6c) menos esa; las demás, de a 0,1 (0x6b87b8).
constexpr double ScaleStep = static_cast<double>(0.01f);
constexpr double ScaleBase = 0.5;
constexpr double FallSpeed = static_cast<double>(-0.1f);
constexpr double Tenth = static_cast<double>(0.1f);
constexpr float CameraMargin = 400.0f;
constexpr float Bounce = 3.2f;
// 0x5303e8: en el suelo, la luz baja 0,05 (0x6b8a48) por paso; en 0 se apaga.
constexpr double Fade = static_cast<double>(0.05f);

float F32(double value)
{
    return static_cast<float>(value);
}

// rand() % n con el idiom de `and 0x8000000f` (n potencia de 2) o con cdq e idiv: con un rand() no
// negativo, los dos son el resto.
int Around(Render::Models::CrtRandom& random, int span, int shift)
{
    return (random.Next() % span) + shift;
}

void Born(Leaf& leaf, const EmberWorld& world, Render::Models::CrtRandom& random)
{
    leaf.type = EmberTexture;
    leaf.scale = F32((static_cast<double>(random.Next() % 50) * ScaleStep) + ScaleBase);
    const auto x = F32(static_cast<double>(Around(random, 1600, -800)) + static_cast<double>(world.hero.x));
    const auto y = F32(static_cast<double>(Around(random, 1400, -500)) + static_cast<double>(world.hero.y));
    const auto z = F32(static_cast<double>(Around(random, 1000, 50)) + static_cast<double>(world.hero.z));
    leaf.position = {x, y, z};
    leaf.start = leaf.position;
    const double fall = static_cast<double>(Around(random, 64, 64)) * FallSpeed;
    leaf.velocity.x = F32(fall);
    // `fcomp`: la cámara + 400 contra la y; con `test ah, 0x41` (menor o igual, o sin orden) no cambia.
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
    leaf.light = {1.0f, 0.0f, 0.0f};
}

// 0x5303a0: con el terreno por debajo (o un NaN), en el aire: cada componente de la velocidad suma
// (rand() % 16 − 8) · 0,1 y la posición, la velocidad (la z, con la de z antes de guardarla en float). Si
// no, queda en el suelo y se apaga de a 0,05; con el rojo en 0 o menos, muere.
void Fall(Leaf& leaf, const EmberWorld& world, Render::Models::CrtRandom& random)
{
    const double ground = TerrainHeightAt(*world.heights, *world.attributes, leaf.position.x, leaf.position.y);
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
    // `fcomp` con 0 y `test ah, 0x41`: mayor sigue; menor, igual o sin orden, muere.
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

void StepLoginEmbers(Leaves& leaves, const EmberWorld& world, Render::Models::CrtRandom& random)
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
            // 0x5305a0: la luz en 1 y el byte vivo, y después el creador del mapa.
            leaf.light = glm::vec3(1.0f);
            leaf.alive = true;
            Born(leaf, world, random);
        }
        else
        {
            Fall(leaf, world, random);
        }
    }
}

LeafQuad LeafQuadOf(const Leaf& leaf)
{
    // 0x5307e2: a = escala + escala, b = escala · 4 (0x6b8a44), los dos justos en float.
    const float a = F32(static_cast<double>(leaf.scale) + static_cast<double>(leaf.scale));
    const float b = F32(static_cast<double>(leaf.scale) * 4.0);
    const Render::Models::OriginalMatrix turn = Render::Models::OriginalAngleMatrix(leaf.angles);
    const std::array<glm::vec3, 4> local{{{-a, -a, -b}, {a, a, -b}, {a, a, b}, {-a, -a, b}}};
    LeafQuad quad;
    quad.translation = leaf.position;
    for (std::size_t k = 0; k < local.size(); ++k)
    {
        quad.corners[k] = Render::Models::OriginalVectorTransform(local[k], turn);
    }
    quad.uv = {{{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}}};
    return quad;
}
} // namespace Client::World
