#pragma once

#include "Core/Expected.h"
#include "Data/Textures/Texture.h"

#include <cstdint>
#include <span>
#include <string>

namespace Client::CastleSiege
{
// 0x5f1890: repaints the existing flag's RGBA buffer; the 8×8 mark is scaled to 32×32 in the center.
// The native upload uses internalformat RGB even though the buffer and the CPU metadata stay RGBA.
[[nodiscard]] Core::Expected<Data::Textures::Texture, std::string>
PaintTheCastleFlag(std::span<const std::uint8_t, 64> mark, int width, int height);
} // namespace Client::CastleSiege
