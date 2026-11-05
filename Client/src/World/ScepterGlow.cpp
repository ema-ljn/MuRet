#include "World/ScepterGlow.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "World/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>

namespace Client::World
{
namespace
{
constexpr std::uint16_t Shiny = 0x5fd;
constexpr std::uint16_t Flare = 0x5ac;
constexpr std::int32_t Additive = 0;
constexpr float Step = 0.05f;       // 0x6b8ad0
constexpr float Reach = 100.0f;     // 0x6b86b4
constexpr float BigShrink = 0.3f;   // 0x6b89dc
constexpr float SmallShrink = 0.2f; // 0x6b8854

float F32(double value)
{
    return static_cast<float>(value);
}

// fild, fmul por 0,05f y la suma de `base`, en 53 bits, guardado en float.
float Steps(int count, double base)
{
    return F32((static_cast<double>(count) * static_cast<double>(Step)) + base);
}

// (0, p ± 100, p): la y en 53 bits y guardada en float.
glm::vec3 OffsetOf(float p, double reach)
{
    return {0.0f, F32(static_cast<double>(p) + reach), p};
}
} // namespace

void CreateScepterGlow(FrameSprites& sprites, const Render::Models::OriginalMatrix& bone, float bodyScale,
                       const glm::vec3& bodyOrigin, Render::Models::CrtRandom& random)
{
    const glm::vec3 light{1.0f, 0.3f, 0.0f};
    const auto at = [&](const glm::vec3& offset)
    { return Render::Models::OriginalTransformPosition(bone, offset, bodyScale, bodyOrigin); };

    // El tridente: p = (rand() % 60) · 0,05 − 1,5 y la escala (rand() % 10) · 0,05 + 1,4.
    float p = Steps(random.Next() % 60, -1.5);
    float scale = Steps(random.Next() % 10, 1.4f);
    glm::vec3 position = at(OffsetOf(p, -Reach));
    (void)sprites.Create(Shiny, position, scale, light, 0.0f, Additive);
    (void)sprites.Create(Shiny, position, F32(static_cast<double>(scale) - static_cast<double>(BigShrink)), light,
                         20.0f, Additive);

    // El flare01, con otro p y la escala (rand() % 10) · 0,05 + 1, más 0,3 al pedirlo.
    p = Steps(random.Next() % 60, -1.5);
    scale = Steps(random.Next() % 10, 1.0);
    position = at(OffsetOf(p, -Reach));
    (void)sprites.Create(Flare, position, F32(static_cast<double>(scale) + static_cast<double>(BigShrink)), light, 0.0f,
                         Additive);

    // El pomo: p = (rand() % 40) · 0,05 − 1 y la escala (rand() % 8) · 0,05 + 0,4.
    p = Steps(random.Next() % 40, -1.0);
    scale = Steps(random.Next() % 8, 0.4f);
    position = at(OffsetOf(p, Reach));
    (void)sprites.Create(Shiny, position, scale, light, 0.0f, Additive);
    (void)sprites.Create(Shiny, position, F32(static_cast<double>(scale) - static_cast<double>(SmallShrink)), light,
                         90.0f, Additive);
}
} // namespace Client::World
