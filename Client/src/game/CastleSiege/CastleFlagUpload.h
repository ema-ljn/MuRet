#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <span>
#include <string>

namespace Client::Assets
{
class ModelTextureTable;
} // namespace Client::Assets

namespace Client::CastleSiege
{

// 0x4e0790 → 0x5f1890: each receipt repaints and uploads 0x69c, without changing its metadata or its references.
[[nodiscard]] Core::Expected<void, std::string> UploadTheCastleFlag(std::span<const std::uint8_t, 64> mark,
                                                                    Assets::ModelTextureTable& table);
} // namespace Client::CastleSiege
