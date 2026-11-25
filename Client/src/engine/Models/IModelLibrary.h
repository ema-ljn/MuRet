#pragma once

#include "Data/Models/Model.h"
#include "Render/ITextureSlots.h"
#include "Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Models/TextureMetadata.h"

#include <glm/ext/vector_float2.hpp>

#include <cstdint>
#include <optional>
#include <span>

namespace Client::Render::Models
{
// The models drawing asks for, the character's and its items', with their skeleton and each mesh's texture slot.
// The loading scene loads them (GameModels).
class IModelLibrary : public ITextureSlots
{
public:
    virtual ~IModelLibrary() = default;
    [[nodiscard]] virtual const Data::Models::Model* Find(std::uint16_t model) const = 0;
    // Player.bmd's for 0x20a, with its speeds and its head (MakePlayerSkeleton).
    [[nodiscard]] virtual const Skeleton* SkeletonOf(std::uint16_t model) const = 0;
    // One per mesh, in order; empty if its textures were not loaded.
    [[nodiscard]] virtual std::span<const TextureSlot> MeshTextures(std::uint16_t model) const = 0;
    // A raw32 lookup in the same table: an explicit absence, never truncated to the models' slot16.
    [[nodiscard]] virtual std::optional<Scenes::TextureMetadata> TextureMetadataOf(std::uint32_t selector) const
    {
        (void)selector;
        return std::nullopt;
    }
    // The handle a successful load published. Its absence never allows truncating the raw32 selector or taking it
    // for a GL name. Legacy libraries do not offer it.
    [[nodiscard]] std::optional<TextureSlot> TextureSlotOf(std::uint32_t selector) const override
    {
        (void)selector;
        return std::nullopt;
    }
    // The size of `slot`'s texture (+0x20 and +0x24 of its place in the table, 0x7d3a1b8), if this library uploaded
    // it: the sprites' size (0x549290).
    [[nodiscard]] virtual std::optional<glm::vec2> TextureSize(TextureSlot slot) const
    {
        (void)slot;
        return std::nullopt;
    }
};
} // namespace Client::Render::Models
