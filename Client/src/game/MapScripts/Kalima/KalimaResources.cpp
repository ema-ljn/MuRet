#include "MapScripts/Kalima/KalimaResources.h"

#include "Core/Expected.h"
#include "Data/Textures/TextureSet.h"
#include "Terrain/WorldTerrain.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Client::World
{
Core::Expected<std::vector<Data::Textures::LoadedTexture>, std::string>
LoadKalimaTextures(const std::filesystem::path& dataRoot)
{
    auto loaded = Data::Textures::LoadTextureSet(dataRoot, Terrain::KalimaWaterTextureSet());
    if (!loaded.HasValue())
        return Core::Unexpected(Data::Textures::Describe(loaded.Error()));
    return std::move(loaded).Value();
}
} // namespace Client::World
