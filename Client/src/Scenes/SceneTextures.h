#pragma once

#include "Core/Expected.h"
#include "Data/Textures/TextureSet.h"
#include "Render/ITextureStore.h"
#include "Render/TextureSlot.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>

// El juego de texturas de una escena (TextureSets.h) mientras la escena vive (CL5.4): lo lee del
// Data\ y lo sube en su Enter, como 0x622e40 y 0x61c7c0, y lo suelta en su Leave, como 0x63c150
// con cada slot. Guarda el tamaño del buffer de cada una, que la UI nueva usa para sus UV.
namespace Client::Scenes
{
// Los lados del buffer de una textura subida (Texture::textureWidth y textureHeight).
struct TextureBufferSize
{
    int width = 1;
    int height = 1;
};

class SceneTextures
{
public:
    // `set` tiene que vivir más que esto: las tablas de TextureSets.h son constexpr.
    SceneTextures(Render::ITextureStore& store, std::span<const Data::Textures::TextureSetEntry> set);

    // Lee todo el juego desde `dataRoot` (LoadTextureSet) y lo sube a la tabla de una vez. Una que
    // falta o no se lee corta antes de subir nada, con un error que la nombra como la pide el
    // original; si la tabla no sube, su error.
    [[nodiscard]] Core::Expected<void, std::string> Load(const std::filesystem::path& dataRoot);

    // Suelta cada slot del juego, en orden, si hubo un Load (aunque haya fallado: lo que la tabla
    // subió antes del error no queda). Uno más no hace nada.
    void Release();

    // El de `slot` si se subió, o 1 × 1.
    [[nodiscard]] TextureBufferSize BufferSize(Render::TextureSlot slot) const;

private:
    Render::ITextureStore& m_store;
    std::span<const Data::Textures::TextureSetEntry> m_set;
    bool m_loaded = false;
    std::map<std::uint16_t, TextureBufferSize> m_sizes;
};
} // namespace Client::Scenes
