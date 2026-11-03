#pragma once

#include "Core/Expected.h"
#include "Data/Textures/TextureSet.h"
#include "Platform/GlLoader.h"
#include "Render/Gl/GlTexture.h"
#include "Render/ITextureStore.h"
#include "Render/TextureSlot.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

// La tabla de texturas de una escena, indexada como la del original por el número de slot
// (0x7d3a198, 1.845 slots; el nombre de GL en +0x2c), con los 256 del texto de MuRet después
// (TextureSlot.h, CL5.3). La del original es global y una escena suelta lo suyo con 0x63c150; esta
// es de la escena que la crea (pregunta 11 del plan de la CL5), y lo que queda se borra en el
// destructor, con el contexto vivo.
namespace Client::Render::Gl
{
class TextureTable final : public Render::ITextureStore
{
public:
    explicit TextureTable(const Platform::GlFunctions& gl);

    // Las escenas la guardan como ITextureStore&: ni se copia ni se mueve.
    TextureTable(const TextureTable&) = delete;
    TextureTable& operator=(const TextureTable&) = delete;
    TextureTable(TextureTable&&) = delete;
    TextureTable& operator=(TextureTable&&) = delete;
    ~TextureTable() override = default;

    // Sube cada una con GlTexture::Upload, en su slot. Un slot de TextureSlotCount (2.101) o más, o
    // una textura que GlTexture no sube (también si GL la rechaza), corta con un error que nombra
    // el slot en hexadecimal, como el research. El slot de la que falló se queda con lo que tenía;
    // las de antes en `textures` ya quedaron subidas, porque cada escena sube su juego entero.
    [[nodiscard]] Core::Expected<void, std::string>
    Load(std::span<const Data::Textures::LoadedTexture> textures) override;

    // La textura de `slot`, o nullptr si no hay.
    [[nodiscard]] const GlTexture* Find(TextureSlot slot) const;

    void Release(TextureSlot slot) override;

private:
    const Platform::GlFunctions* m_gl;
    // Uno por slot, los del original y los del texto: el índice es el número de slot.
    std::vector<std::optional<GlTexture>> m_slots;
};
} // namespace Client::Render::Gl
