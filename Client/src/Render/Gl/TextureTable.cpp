#include "Render/Gl/TextureTable.h"

#include "Core/Expected.h"
#include "Data/Textures/TextureSet.h"
#include "Platform/GlLoader.h"
#include "Render/Gl/GlTexture.h"
#include "Render/TextureSlot.h"

#include <format>
#include <span>
#include <string>
#include <utility>

namespace Client::Render::Gl
{
TextureTable::TextureTable(const Platform::GlFunctions& gl) : m_gl(&gl), m_slots(TextureSlotCount) {}

Core::Expected<void, std::string> TextureTable::Load(std::span<const Data::Textures::LoadedTexture> textures)
{
    for (const Data::Textures::LoadedTexture& texture : textures)
    {
        if (texture.slot >= m_slots.size())
        {
            return Core::Unexpected(std::format("el slot {:#x} no está en la tabla, que tiene {} (los {} del original "
                                                "y los {} del texto)",
                                                texture.slot, TextureSlotCount, OriginalTextureSlotCount,
                                                TextTextureSlotCount));
        }
        Core::Expected<GlTexture, std::string> uploaded = GlTexture::Upload(*m_gl, texture.texture, texture.sampling);
        if (!uploaded.HasValue())
        {
            return Core::Unexpected(
                std::format("no se subió la textura del slot {:#x}: {}", texture.slot, uploaded.Error()));
        }
        // emplace borra la que había: su GlName la suelta con glDeleteTextures.
        m_slots[texture.slot].emplace(std::move(uploaded).Value());
    }
    return {};
}

const GlTexture* TextureTable::Find(TextureSlot slot) const
{
    if (slot.value >= m_slots.size() || !m_slots[slot.value].has_value())
    {
        return nullptr;
    }
    return &*m_slots[slot.value];
}

void TextureTable::Release(TextureSlot slot)
{
    if (slot.value < m_slots.size())
    {
        m_slots[slot.value].reset();
    }
}
} // namespace Client::Render::Gl
