#include "Scenes/SceneTextures.h"

#include "Core/Expected.h"
#include "Data/Textures/TextureSet.h"
#include "Render/ITextureStore.h"
#include "Render/TextureSlot.h"
#include "Scenes/TextureMetadata.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Client::Scenes
{
SceneTextures::SceneTextures(Render::ITextureStore& store, std::span<const Data::Textures::TextureSetEntry> set)
    : m_store(store), m_set(set)
{
}

Core::Expected<void, std::string> SceneTextures::Load(const std::filesystem::path& dataRoot)
{
    auto read = Data::Textures::LoadTextureSet(dataRoot, m_set);
    if (!read.HasValue())
    {
        return Core::Unexpected(Data::Textures::Describe(read.Error()));
    }
    const std::vector<Data::Textures::LoadedTexture> textures = std::move(read).Value();
    m_loaded = true;
    auto uploaded = m_store.Load(textures);
    if (!uploaded.HasValue())
    {
        return Core::Unexpected(uploaded.Error());
    }
    for (const Data::Textures::LoadedTexture& texture : textures)
    {
        m_sizes.insert_or_assign(texture.slot, TextureBufferSize{.width = texture.texture.textureWidth,
                                                                 .height = texture.texture.textureHeight});
        m_metadata.insert_or_assign(static_cast<std::uint32_t>(texture.slot), TextureMetadata(texture.texture));
    }
    return {};
}

void SceneTextures::Release()
{
    if (!m_loaded)
    {
        return;
    }
    for (const Data::Textures::TextureSetEntry& entry : m_set)
    {
        m_store.Release(Render::TextureSlot{entry.slot});
    }
    m_loaded = false;
    m_sizes.clear();
    m_metadata.clear();
}

TextureBufferSize SceneTextures::BufferSize(Render::TextureSlot slot) const
{
    const auto found = m_sizes.find(slot.value);
    return found == m_sizes.end() ? TextureBufferSize{} : found->second;
}

std::optional<TextureMetadata> SceneTextures::MetadataOf(std::uint32_t selector) const
{
    const auto found = m_metadata.find(selector);
    return found == m_metadata.end() ? std::nullopt : std::optional(found->second);
}
} // namespace Client::Scenes
