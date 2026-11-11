#pragma once

#include "Data/Models/Model.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/TextureMetadata.h"
#include "Scenes/TextureSets.h"

#include <glm/ext/vector_float2.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace Client::Tests
{
// Las texturas de efectos que GameModels sube con los modelos (0x61ccb0, EffectTextureSet), que piden las
// partículas y los sprites del mundo: en los tests, buffers de 32×32 RGB en su propio slot.
[[nodiscard]] inline bool IsEffectTexture(std::uint32_t selector)
{
    return std::ranges::any_of(Scenes::EffectTextureSet, [selector](const Data::Textures::TextureSetEntry& entry)
                               { return entry.slot == selector; });
}

[[nodiscard]] inline std::optional<Scenes::TextureMetadata> EffectTextureMetadata(std::uint32_t selector)
{
    if (!IsEffectTexture(selector))
    {
        return std::nullopt;
    }
    Scenes::TextureMetadata metadata(Data::Textures::Texture{});
    metadata.size = {32.0f, 32.0f};
    metadata.components = 3;
    return metadata;
}

[[nodiscard]] inline std::optional<Render::TextureSlot> EffectTextureSlot(std::uint32_t selector)
{
    return IsEffectTexture(selector) ? std::optional(Render::TextureSlot{static_cast<std::uint16_t>(selector)})
                                     : std::nullopt;
}

// Una biblioteca de modelos de test con las texturas de efectos de GameModels encima.
class WithEffectTextures final : public Scenes::IModelLibrary
{
public:
    explicit WithEffectTextures(const Scenes::IModelLibrary& base) : m_base(base) {}

    [[nodiscard]] const Data::Models::Model* Find(std::uint16_t model) const override
    {
        return m_base.get().Find(model);
    }
    [[nodiscard]] const Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return m_base.get().SkeletonOf(model);
    }
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(std::uint16_t model) const override
    {
        return m_base.get().MeshTextures(model);
    }
    [[nodiscard]] std::optional<Scenes::TextureMetadata> TextureMetadataOf(std::uint32_t selector) const override
    {
        return IsEffectTexture(selector) ? EffectTextureMetadata(selector) : m_base.get().TextureMetadataOf(selector);
    }
    [[nodiscard]] std::optional<Render::TextureSlot> TextureSlotOf(std::uint32_t selector) const override
    {
        return IsEffectTexture(selector) ? EffectTextureSlot(selector) : m_base.get().TextureSlotOf(selector);
    }
    [[nodiscard]] std::optional<glm::vec2> TextureSize(Render::TextureSlot slot) const override
    {
        return IsEffectTexture(slot.value) ? std::optional(glm::vec2(32.0f, 32.0f)) : m_base.get().TextureSize(slot);
    }

private:
    std::reference_wrapper<const Scenes::IModelLibrary> m_base;
};
} // namespace Client::Tests
