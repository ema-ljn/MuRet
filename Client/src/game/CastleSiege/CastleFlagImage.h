#pragma once

#include "Core/Expected.h"
#include "Data/Textures/Texture.h"

#include <cstdint>
#include <span>
#include <string>

namespace Client::CastleSiege
{
// 0x5f1890: repinta el buffer RGBA de la bandera existente; la marca 8×8 se amplía a 32×32 en el centro.
// La subida nativa usa internalformat RGB aunque el buffer y la metadata CPU sigan siendo RGBA.
[[nodiscard]] Core::Expected<Data::Textures::Texture, std::string>
PaintTheCastleFlag(std::span<const std::uint8_t, 64> mark, int width, int height);
} // namespace Client::CastleSiege
