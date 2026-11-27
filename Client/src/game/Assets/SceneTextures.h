#pragma once

#include "Core/Expected.h"
#include "Data/Textures/TextureSet.h"
#include "Render/ITextureStore.h"
#include "Render/TextureSlot.h"
#include "Models/TextureMetadata.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>

// A scene's texture set (TextureSets.h) while the scene lives: read from Data\ and uploaded in its Enter, as 0x622e40
// and 0x61c7c0 do, and released in its Leave, as 0x63c150 does with each slot. Keeps each one's buffer size, which
// the new UI uses for its UVs.
namespace Client::Assets
{
// The sides of an uploaded texture's buffer (Texture::textureWidth and textureHeight).
struct TextureBufferSize
{
    int width = 1;
    int height = 1;
};

class SceneTextures
{
public:
    // `set` must outlive this: the TextureSets.h tables are constexpr.
    SceneTextures(Render::ITextureStore& store, std::span<const Data::Textures::TextureSetEntry> set);

    // Reads the whole set from `dataRoot` (LoadTextureSet) and uploads it to the table at once. One that is missing or
    // unreadable stops before anything is uploaded, with an error naming it as the original asks for it; if the table
    // does not upload, its error.
    [[nodiscard]] Core::Expected<void, std::string> Load(const std::filesystem::path& dataRoot);

    // Releases each slot of the set, in order, if there was a Load (even a failed one: what the table uploaded before
    // the error does not stay). Another call does nothing.
    void Release();
    // Keeps the slot that also belongs to the common load of GameModels.
    void Release(std::optional<Render::TextureSlot> sharedSlot);

    // That of `slot` if it was uploaded, or 1 × 1.
    [[nodiscard]] TextureBufferSize BufferSize(Render::TextureSlot slot) const;
    // The size and components of the one uploaded at `selector` (those of its place in table 0x7d3a1b8, which 0x548550
    // and 0x549440 read); empty if it was not uploaded.
    [[nodiscard]] std::optional<Models::TextureMetadata> MetadataOf(std::uint32_t selector) const;

private:
    Render::ITextureStore& m_store;
    std::span<const Data::Textures::TextureSetEntry> m_set;
    bool m_loaded = false;
    std::map<std::uint16_t, TextureBufferSize> m_sizes;
    std::map<std::uint32_t, Models::TextureMetadata> m_metadata;
};
} // namespace Client::Assets
