#include "World/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <span>

namespace Client::World
{
namespace
{
// 0x549290: el paso del alpha (0x6b87b8), el tope (0x6b87d4) y el piso (0x6b8854).
constexpr double AlphaStep = static_cast<double>(0.1f);
constexpr float AlphaCeiling = 1.0f;
constexpr float AlphaFloor = 0.2f;
} // namespace

bool FrameSprites::Create(std::uint16_t texture, const glm::vec3& position, float scale, const glm::vec3& light,
                          float rotation, std::int32_t blend)
{
    for (Sprite& sprite : m_sprites)
    {
        if (sprite.alive)
        {
            continue;
        }
        sprite = Sprite{.alive = true,
                        .texture = texture,
                        .blend = blend,
                        .scale = scale,
                        .position = position,
                        .light = light,
                        .rotation = rotation,
                        .alpha = 1.0f,
                        .seen = false};
        return true;
    }
    return false;
}

void FrameSprites::MarkSeen()
{
    for (Sprite& sprite : m_sprites)
    {
        if (sprite.alive)
        {
            sprite.seen = true;
        }
    }
}

void FrameSprites::Clear()
{
    for (Sprite& sprite : m_sprites)
    {
        sprite.alive = false;
    }
}

bool FrameSprites::Any() const
{
    for (const Sprite& sprite : m_sprites)
    {
        if (sprite.alive)
        {
            return true;
        }
    }
    return false;
}

std::span<Sprite> FrameSprites::Sprites()
{
    return m_sprites;
}

void StepSpriteAlpha(Sprite& sprite)
{
    if (sprite.seen)
    {
        sprite.alpha = static_cast<float>(static_cast<double>(sprite.alpha) + AlphaStep);
        // `test ah, 0x41`: menor o igual queda; si no (también sin orden), el tope.
        if (!(sprite.alpha <= AlphaCeiling))
        {
            sprite.alpha = AlphaCeiling;
        }
        return;
    }
    sprite.alpha = static_cast<float>(static_cast<double>(sprite.alpha) - AlphaStep);
    if (sprite.alpha < AlphaFloor)
    {
        sprite.alpha = AlphaFloor;
    }
}
} // namespace Client::World
