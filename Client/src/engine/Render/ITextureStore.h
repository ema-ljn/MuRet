#pragma once

#include "Core/Expected.h"
#include "Data/Textures/TextureSet.h"
#include "Render/TextureSlot.h"

#include <span>
#include <string>

// Dónde sube y suelta sus texturas una escena, sin ver GL: las escenas viven en ClientLib, que no
// enlaza OpenGL. Lo implementa Gl::TextureTable, en ClientRender, y en los tests de las escenas
// (CL5.4), un falso de tests/support/.
namespace Client::Render
{
class ITextureStore
{
public:
    virtual ~ITextureStore() = default;

    // Sube `textures`, cada una en su slot, en orden. Un slot ocupado se reemplaza. La primera que
    // no se sube corta, con un error que dice su slot; las de antes quedan.
    [[nodiscard]] virtual Core::Expected<void, std::string>
    Load(std::span<const Data::Textures::LoadedTexture> textures) = 0;

    // Suelta la de `slot`, como 0x63c150(slot). Uno vacío no hace nada.
    virtual void Release(TextureSlot slot) = 0;
};
} // namespace Client::Render
