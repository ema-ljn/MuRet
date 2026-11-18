#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <span>
#include <string>

namespace Client::Scenes
{
class ModelTextureTable;

// 0x4e0790 → 0x5f1890: cada recepción repinta y sube el 0x69c, sin cambiar su metadata ni sus referencias.
[[nodiscard]] Core::Expected<void, std::string> UploadTheCastleFlag(std::span<const std::uint8_t, 64> mark,
                                                                    ModelTextureTable& table);
} // namespace Client::Scenes
