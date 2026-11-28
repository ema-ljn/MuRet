#pragma once

#include "Core/Expected.h"
#include "Data/Textures/TextureSet.h"

#include <filesystem>
#include <string>
#include <vector>

// 0x6189e9..0x618a18: Kalima's water textures (Terrain::KalimaWaterTextureSet), read from `dataRoot`.
namespace Client::MapScripts::Kalima
{
[[nodiscard]] Core::Expected<std::vector<Data::Textures::LoadedTexture>, std::string>
LoadKalimaTextures(const std::filesystem::path& dataRoot);
} // namespace Client::MapScripts::Kalima
