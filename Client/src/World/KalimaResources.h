#pragma once

#include "Core/Expected.h"
#include "Data/Textures/TextureSet.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

// 6189e9..618a18: recursos físicos de agua; el owner los publica en ModelTextureTable.
namespace Client::World
{
[[nodiscard]] std::span<const Data::Textures::TextureSetEntry> KalimaTextureSet();
[[nodiscard]] Core::Expected<std::vector<Data::Textures::LoadedTexture>, std::string>
LoadKalimaTextures(const std::filesystem::path& dataRoot);
} // namespace Client::World
