#include "Scenes/LorenciaModelFiles.h"

#include "Data/Models/ModelFiles.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Client::Scenes
{
namespace
{
struct ModelRange
{
    std::uint16_t first;
    std::uint16_t last;
    std::string_view name;
};

// 0x619a21..0x619ea1: 39 grupos, 112 pedidos. Cada grupo numera archivos desde 1.
// El orden de CALL no es el de ID; DoungeonGate conserva el nombre literal del original.
constexpr std::array<ModelRange, 39> ModelRanges{{
    {0x00, 0x0c, "Tree"},         {0x14, 0x1b, "Grass"},          {0x1e, 0x22, "Stone"},
    {0x28, 0x2a, "StoneStatue"},  {0x2b, 0x2b, "SteelStatue"},    {0x2c, 0x2e, "Tomb"},
    {0x32, 0x33, "FireLight"},    {0x34, 0x34, "Bonfire"},        {0x37, 0x37, "DoungeonGate"},
    {0x3a, 0x3a, "TreasureDrum"}, {0x3b, 0x3b, "TreasureChest"},  {0x3c, 0x3c, "Ship"},
    {0x45, 0x4a, "StoneWall"},    {0x4b, 0x4e, "StoneMuWall"},    {0x41, 0x43, "SteelWall"},
    {0x44, 0x44, "SteelDoor"},    {0x5b, 0x5d, "Cannon"},         {0x50, 0x50, "Bridge"},
    {0x51, 0x54, "Fence"},        {0x55, 0x55, "BridgeStone"},    {0x5a, 0x5a, "StreetLight"},
    {0x5f, 0x5f, "Curtain"},      {0x62, 0x65, "Carriage"},       {0x66, 0x67, "Straw"},
    {0x60, 0x61, "Sign"},         {0x38, 0x39, "MerchantAnimal"}, {0x69, 0x69, "Waterspout"},
    {0x6a, 0x6d, "Well"},         {0x6e, 0x6e, "Hanging"},        {0x73, 0x77, "House"},
    {0x78, 0x78, "Tent"},         {0x6f, 0x6f, "Stair"},          {0x79, 0x7e, "HouseWall"},
    {0x7f, 0x81, "HouseEtc"},     {0x82, 0x84, "Light"},          {0x85, 0x85, "PoseBox"},
    {0x8c, 0x92, "Furniture"},    {0x96, 0x96, "Candle"},         {0x97, 0x99, "Beer"},
}};
constexpr std::size_t LorenciaModelCount = 112;

constexpr auto MakeLorenciaFiles()
{
    std::array<Data::Models::ModelFile, LorenciaModelCount> files{};
    std::size_t next = 0;
    for (const ModelRange& range : ModelRanges)
    {
        for (std::uint16_t model = range.first; model <= range.last; ++model)
            files[next++] = {model, "Object1\\", range.name, static_cast<int>(model - range.first) + 1};
    }
    return files;
}

constexpr auto Files = MakeLorenciaFiles();
} // namespace

std::span<const Data::Models::ModelFile> LorenciaModelFiles()
{
    return Files;
}
} // namespace Client::Scenes
