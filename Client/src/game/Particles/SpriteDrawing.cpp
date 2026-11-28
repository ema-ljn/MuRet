#include "Particles/SpriteDrawing.h"

#include "Math/OriginalRotation.h"
#include "Models/IModelLibrary.h"
#include "Particles/Sprites.h"
#include "Render/Billboard.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/TextureSlot.h"

#include <glm/ext/vector_float2.hpp>

#include <cstddef>
#include <cstdint>

namespace Client::Particles
{
namespace
{
// 0x549290 with the atlas texture: the cell by its +4 (0x54941c), 64 × 64, with a third of the texture (0x3ea8f5c3
// and 0x3f28f5c3).
constexpr float AtlasSide = 64.0f;
constexpr float AtlasThird = 0.33f;
constexpr float AtlasTwoThirds = 0.66f;

glm::vec2 AtlasCell(std::int32_t cell)
{
    switch (cell)
    {
    case 1:
        return {AtlasThird, 0.0f};
    case 2:
        return {AtlasTwoThirds, 0.0f};
    case 3:
        return {0.0f, AtlasThird};
    case 4:
        return {AtlasThird, AtlasThird};
    case 5:
        return {AtlasTwoThirds, AtlasThird};
    case 6:
        return {0.0f, AtlasTwoThirds};
    case 7:
        return {AtlasThird, AtlasTwoThirds};
    default:
        return {0.0f, 0.0f};
    }
}

Render::ModelBlend SpriteBlend(const Sprite& sprite)
{
    if (sprite.texture == AtlasSprite)
    {
        return Render::ModelBlend::Alpha;
    }
    switch (sprite.blend)
    {
    case 1:
        return Render::ModelBlend::Subtractive;
    case 2:
        return Render::ModelBlend::Alpha;
    case 3:
        return Render::ModelBlend::InverseColorAdditive;
    default:
        return Render::ModelBlend::Additive;
    }
}
} // namespace

void DrawFrameSprites(Render::ModelBatch& batch, FrameSprites& sprites, const Models::IModelLibrary& textures,
                      const Math::OriginalMatrix& view, std::uint8_t mode)
{
    for (Sprite& sprite : sprites.Sprites())
    {
        // 0x54944e..0x54949e: mode 1 retains; mode 2 also turns off the lower band, NaN included.
        if (mode == 1 && sprite.position.z > 350.0f)
            continue;
        if (mode == 2 && !(sprite.position.z > 300.0f))
        {
            sprite.alive = false;
            continue;
        }
        if (!sprite.alive)
        {
            continue;
        }
        StepSpriteAlpha(sprite);
        const Render::TextureSlot slot{sprite.texture};
        Render::Billboard billboard;
        // 0x5493de/0x549410 call 0x621400: the components byte picks Color3 or Color4 here too.
        const auto metadata = textures.TextureMetadataOf(sprite.texture);
        constexpr int RgbComponents = 3;
        const int components = metadata ? metadata->components : RgbComponents;
        // The size: the scale times the alpha in the x87, times the texture's width and height.
        const double size = static_cast<double>(sprite.scale) * static_cast<double>(sprite.alpha);
        if (sprite.texture == AtlasSprite)
        {
            billboard =
                Render::MakeBillboard(view, sprite.position, AtlasSide, AtlasSide, sprite.light, sprite.rotation,
                                      AtlasCell(sprite.blend), glm::vec2(AtlasThird), sprite.texture, components);
        }
        else
        {
            const glm::vec2 texture = textures.TextureSize(slot).value_or(glm::vec2(0.0f));
            const auto width = static_cast<float>(size * static_cast<double>(texture.x));
            const auto height = static_cast<float>(size * static_cast<double>(texture.y));
            billboard = Render::MakeBillboard(view, sprite.position, width, height, sprite.light, sprite.rotation,
                                              glm::vec2(0.0f), glm::vec2(1.0f), sprite.texture, components);
        }
        batch.SetState(Render::ModelDrawState{.texture = slot, .blend = SpriteBlend(sprite)});
        const auto vertex = [&billboard](std::size_t k)
        { return Render::ModelVertex{billboard.corners[k].position, billboard.corners[k].uv, billboard.color}; };
        batch.Triangle({vertex(0), vertex(1), vertex(2)});
        batch.Triangle({vertex(0), vertex(2), vertex(3)});
        // 0x549440(0) turns it off after drawing it.
        if (mode == 0 || mode == 2)
            sprite.alive = false;
    }
}
} // namespace Client::Particles
