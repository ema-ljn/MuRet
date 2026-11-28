#pragma once

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// The table of 0x5491e0 (0x760f990, 1000 slots of 0x1d4 bytes), filled by the characters' and objects' drawing and
// drawn and cleared by 0x549440(0) in the same frame (06 § Los efectos del login).
namespace Client::Particles
{
inline constexpr std::size_t SpriteCount = 1000;
// The texture with its 3 × 3 atlas (0x549290): its +4 is the frame, not the blend, and it uses the normal blend.
inline constexpr std::uint16_t AtlasSprite = 0x637;

struct Sprite
{
    bool alive = false;        // +0x0
    std::uint16_t texture = 0; // +0x2
    std::int32_t blend = 0;    // +0x4: 0 additive, 1 0x620b10, 2 normal, 3 0x620b90 (or the atlas frame)
    float scale = 0.0f;        // +0xc
    glm::vec3 position{};      // +0x10
    glm::vec3 light{};         // +0xf4
    float rotation = 0.0f;     // +0x24, in degrees
    float alpha = 1.0f;        // +0x114
    bool seen = false;         // +0x16c
};

class FrameSprites
{
public:
    // 0x5491e0(texture, position, scale, light, owner, rotation, blend) in the first slot without the alive byte, with
    // alpha at 1. With the 1000 full, nothing is created.
    bool Create(std::uint16_t texture, const glm::vec3& position, float scale, const glm::vec3& light, float rotation,
                std::int32_t blend);
    // 0x549510: sets +0x16c of the live ones, before the pass.
    void MarkSeen();
    void Clear();

    [[nodiscard]] std::span<Sprite> Sprites();
    [[nodiscard]] bool Any() const;

private:
    // On the heap: the scene that holds it must fit on the stack.
    std::vector<Sprite> m_sprites = std::vector<Sprite>(SpriteCount);
};

// 0x549290, when drawing: seen, alpha rises 0.1 up to 1; unseen, it falls 0.1 down to 0.2. In 53 bits, stored as
// float.
void StepSpriteAlpha(Sprite& sprite);
} // namespace Client::Particles
