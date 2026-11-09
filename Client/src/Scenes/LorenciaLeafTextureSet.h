#pragma once

#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"

#include <array>

namespace Client::Scenes
{
// 61a560: map0→World1, calls61ac64/61ac92/61acc0. Se intentan también los archivos ausentes;
// cada fallo conserva la última fila publicada y no omite las solicitudes posteriores.
inline constexpr std::array<Data::Textures::TextureSetEntry, 3> LorenciaLeafTextureSet{{
    {"World1\\leaf01.tga", 0x64, {Data::Textures::Filter::Nearest, Data::Textures::Wrap::Clamp}},
    {"World1\\leaf01.jpg", 0x64, {Data::Textures::Filter::Nearest, Data::Textures::Wrap::Clamp}},
    {"World1\\leaf02.jpg", 0x65, {Data::Textures::Filter::Nearest, Data::Textures::Wrap::Clamp}},
}};
} // namespace Client::Scenes
