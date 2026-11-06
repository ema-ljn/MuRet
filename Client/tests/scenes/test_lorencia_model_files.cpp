#include "Scenes/LorenciaModelFiles.h"

#include "Data/Models/ModelFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace
{
// Tuplas transcritas del contrato ASM independiente de 0x616ec0, main.exe SHA-256
// c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a.
// Contrato cotejado con ASM: CALL 0x619a21..0x619ea1; research 05, tabla de Lorencia.
// Orden de CALL, no de ID: SteelWall va después de StoneMuWall y Cannon antes de Bridge.
TEST(LorenciaModelFilesTest, KeepsTheOriginalCallOrderAndFileNumbers)
{
    struct Request
    {
        std::uint16_t model;
        std::string_view name;
        int number;
    };
    constexpr std::array<Request, 112> Expected{{
        {0x00, "Tree", 1},         {0x01, "Tree", 2},           {0x02, "Tree", 3},           {0x03, "Tree", 4},
        {0x04, "Tree", 5},         {0x05, "Tree", 6},           {0x06, "Tree", 7},           {0x07, "Tree", 8},
        {0x08, "Tree", 9},         {0x09, "Tree", 10},          {0x0a, "Tree", 11},          {0x0b, "Tree", 12},
        {0x0c, "Tree", 13},        {0x14, "Grass", 1},          {0x15, "Grass", 2},          {0x16, "Grass", 3},
        {0x17, "Grass", 4},        {0x18, "Grass", 5},          {0x19, "Grass", 6},          {0x1a, "Grass", 7},
        {0x1b, "Grass", 8},        {0x1e, "Stone", 1},          {0x1f, "Stone", 2},          {0x20, "Stone", 3},
        {0x21, "Stone", 4},        {0x22, "Stone", 5},          {0x28, "StoneStatue", 1},    {0x29, "StoneStatue", 2},
        {0x2a, "StoneStatue", 3},  {0x2b, "SteelStatue", 1},    {0x2c, "Tomb", 1},           {0x2d, "Tomb", 2},
        {0x2e, "Tomb", 3},         {0x32, "FireLight", 1},      {0x33, "FireLight", 2},      {0x34, "Bonfire", 1},
        {0x37, "DoungeonGate", 1}, {0x3a, "TreasureDrum", 1},   {0x3b, "TreasureChest", 1},  {0x3c, "Ship", 1},
        {0x45, "StoneWall", 1},    {0x46, "StoneWall", 2},      {0x47, "StoneWall", 3},      {0x48, "StoneWall", 4},
        {0x49, "StoneWall", 5},    {0x4a, "StoneWall", 6},      {0x4b, "StoneMuWall", 1},    {0x4c, "StoneMuWall", 2},
        {0x4d, "StoneMuWall", 3},  {0x4e, "StoneMuWall", 4},    {0x41, "SteelWall", 1},      {0x42, "SteelWall", 2},
        {0x43, "SteelWall", 3},    {0x44, "SteelDoor", 1},      {0x5b, "Cannon", 1},         {0x5c, "Cannon", 2},
        {0x5d, "Cannon", 3},       {0x50, "Bridge", 1},         {0x51, "Fence", 1},          {0x52, "Fence", 2},
        {0x53, "Fence", 3},        {0x54, "Fence", 4},          {0x55, "BridgeStone", 1},    {0x5a, "StreetLight", 1},
        {0x5f, "Curtain", 1},      {0x62, "Carriage", 1},       {0x63, "Carriage", 2},       {0x64, "Carriage", 3},
        {0x65, "Carriage", 4},     {0x66, "Straw", 1},          {0x67, "Straw", 2},          {0x60, "Sign", 1},
        {0x61, "Sign", 2},         {0x38, "MerchantAnimal", 1}, {0x39, "MerchantAnimal", 2}, {0x69, "Waterspout", 1},
        {0x6a, "Well", 1},         {0x6b, "Well", 2},           {0x6c, "Well", 3},           {0x6d, "Well", 4},
        {0x6e, "Hanging", 1},      {0x73, "House", 1},          {0x74, "House", 2},          {0x75, "House", 3},
        {0x76, "House", 4},        {0x77, "House", 5},          {0x78, "Tent", 1},           {0x6f, "Stair", 1},
        {0x79, "HouseWall", 1},    {0x7a, "HouseWall", 2},      {0x7b, "HouseWall", 3},      {0x7c, "HouseWall", 4},
        {0x7d, "HouseWall", 5},    {0x7e, "HouseWall", 6},      {0x7f, "HouseEtc", 1},       {0x80, "HouseEtc", 2},
        {0x81, "HouseEtc", 3},     {0x82, "Light", 1},          {0x83, "Light", 2},          {0x84, "Light", 3},
        {0x85, "PoseBox", 1},      {0x8c, "Furniture", 1},      {0x8d, "Furniture", 2},      {0x8e, "Furniture", 3},
        {0x8f, "Furniture", 4},    {0x90, "Furniture", 5},      {0x91, "Furniture", 6},      {0x92, "Furniture", 7},
        {0x96, "Candle", 1},       {0x97, "Beer", 1},           {0x98, "Beer", 2},           {0x99, "Beer", 3},
    }};
    const auto files = Client::Scenes::LorenciaModelFiles();
    ASSERT_EQ(files.size(), Expected.size());
    for (std::size_t index = 0; index < Expected.size(); ++index)
    {
        const auto& file = files[index];
        EXPECT_EQ(file.model, Expected[index].model) << index;
        EXPECT_EQ(file.name, Expected[index].name) << index;
        EXPECT_EQ(file.number, Expected[index].number) << index;
    }
}

// Los huecos no se rellenan con ObjectNN: no hay duplicados ni IDs fuera del bloque 0..159.
TEST(LorenciaModelFilesTest, RequestsTheNamedFilesFromObject1WithoutFillingTheHoles)
{
    const auto files = Client::Scenes::LorenciaModelFiles();
    std::array<bool, 160> requested{};
    for (const Client::Data::Models::ModelFile& file : files)
    {
        EXPECT_EQ(file.folder, "Object1\\");
        EXPECT_NE(file.name, "Object");
        EXPECT_GE(file.number, 1);
        ASSERT_LT(file.model, requested.size());
        EXPECT_FALSE(requested[file.model]) << file.model;
        requested[file.model] = true;
    }
    EXPECT_FALSE(requested[0x0d]);
    EXPECT_FALSE(requested[0x13]);
    EXPECT_FALSE(requested[0x9a]);
}
} // namespace
