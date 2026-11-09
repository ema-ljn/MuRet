#pragma once

#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"

#include <array>

namespace Client::Scenes
{
// 61a560, lista íntegra Texturas: map2→World3; calls61ac64/61ac92/61acc0.
// La segunda carga reemplaza100 sólo si tiene éxito. No inferir componentes por el nombre.
inline constexpr std::array<Data::Textures::TextureSetEntry, 3> DaviasLeafTextureSet{{
    {"World3\\leaf01.tga", 0x64, {Data::Textures::Filter::Nearest, Data::Textures::Wrap::Clamp}},
    {"World3\\leaf01.jpg", 0x64, {Data::Textures::Filter::Nearest, Data::Textures::Wrap::Clamp}},
    {"World3\\leaf02.jpg", 0x65, {Data::Textures::Filter::Nearest, Data::Textures::Wrap::Clamp}},
}};
} // namespace Client::Scenes
