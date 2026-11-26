#pragma once

#include "Core/Expected.h"
#include "Data/Textures/TextureSet.h"
#include "Render/TextureSlot.h"

#include <span>
#include <string>

// Where a scene uploads and releases its textures without seeing GL: scenes live in ClientLib, which does not link
// OpenGL.
namespace Client::Render
{
class ITextureStore
{
public:
    virtual ~ITextureStore() = default;

    // In order; an occupied slot is replaced. The first one that fails stops with an error naming its slot; the earlier
    // ones stay.
    [[nodiscard]] virtual Core::Expected<void, std::string>
    Load(std::span<const Data::Textures::LoadedTexture> textures) = 0;

    // Like 0x63c150(slot). An empty one does nothing.
    virtual void Release(TextureSlot slot) = 0;
};
} // namespace Client::Render
