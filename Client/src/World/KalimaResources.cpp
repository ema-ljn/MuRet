#include "World/KalimaResources.h"

#include "Core/Expected.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"

#include <array>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Client::World
{
namespace
{
constexpr std::array<Data::Textures::TextureSetEntry, 2> Textures{{
    {"Object25\\water1.tga", 0x23, {Data::Textures::Filter::Linear, Data::Textures::Wrap::Repeat}},
    {"Object25\\water2.jpg", 0x24, {Data::Textures::Filter::Nearest, Data::Textures::Wrap::Clamp}},
}};
}
std::span<const Data::Textures::TextureSetEntry> KalimaTextureSet()
{
    return Textures;
}
Core::Expected<std::vector<Data::Textures::LoadedTexture>, std::string>
LoadKalimaTextures(const std::filesystem::path& dataRoot)
{
    auto loaded = Data::Textures::LoadTextureSet(dataRoot, Textures);
    if (!loaded.HasValue())
        return Core::Unexpected(Data::Textures::Describe(loaded.Error()));
    return std::move(loaded).Value();
}
} // namespace Client::World
