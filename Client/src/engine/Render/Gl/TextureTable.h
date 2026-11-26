#pragma once

#include "Core/Expected.h"
#include "Data/Textures/TextureSet.h"
#include "OpenGLApi.h"
#include "Platform/Sdl/GlLoader.h"
#include "Render/Gl/GlTexture.h"
#include "Render/ITextureStore.h"
#include "Render/TextureSlot.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

// A scene's texture table, indexed like the original's by slot number (0x7d3a198, 1845 slots; the GL name at +0x2c),
// followed by MuRet's 256 text slots (TextureSlot.h). The original's is global and a scene frees its own with 0x63c150;
// this one belongs to the scene that creates it, and what is left is deleted in the destructor, with a live context.
namespace Client::Render::Gl
{
class OPENGL_API TextureTable final : public Render::ITextureStore
{
public:
    explicit TextureTable(const Platform::Sdl::GlFunctions& gl);

    // Scenes hold it as ITextureStore&: neither copied nor moved.
    TextureTable(const TextureTable&) = delete;
    TextureTable& operator=(const TextureTable&) = delete;
    TextureTable(TextureTable&&) = delete;
    TextureTable& operator=(TextureTable&&) = delete;
    ~TextureTable() override = default;

    // A slot of TextureSlotCount (6123) or more, or a texture GlTexture does not upload (also if GL rejects it), stops
    // with an error naming the slot in hex, like the research. The failed slot keeps what it had; the earlier ones in
    // `textures` stay uploaded, because each scene uploads its whole set.
    [[nodiscard]] Core::Expected<void, std::string>
    Load(std::span<const Data::Textures::LoadedTexture> textures) override;

    // nullptr if `slot` has none.
    [[nodiscard]] const GlTexture* Find(TextureSlot slot) const;

    void Release(TextureSlot slot) override;

private:
    const Platform::Sdl::GlFunctions* m_gl;
    // One per slot (the original's, text and marks): the index is the slot number.
    std::vector<std::optional<GlTexture>> m_slots;
};
} // namespace Client::Render::Gl
