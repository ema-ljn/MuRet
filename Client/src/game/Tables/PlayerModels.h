#pragma once

#include "Data/Models/ModelFiles.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// As the original loads them: 0x60dbe0 (load step 1 of 0x61ccb0) and 0x60ff50 (step 3)
// (Client/docs/research/06-render-escenas-y-ui.md, "El modelo en el binario"). Each row is a call to 0x60da30, in the
// original's order and with its address: the tables come from emulating both loaders on main.exe c32bbd5f.
namespace Client::Tables
{
// The skeleton (0x60e90d).
inline constexpr std::uint16_t PlayerSkeletonModel = 0x20a;
// Shadow01.bmd, the shadow 0x50ba30 draws under each character (0x60f2f4).
inline constexpr std::uint16_t PlayerShadowModel = 0x20b;
// 0x489d models of 0xe0 bytes (0x60dc23).
inline constexpr std::size_t ModelTableSize = 0x489d;

// 0x60ff50: an item's model is 0x212 plus 512 per group plus its index.
inline constexpr std::uint16_t FirstItemModel = 0x212;
inline constexpr std::uint16_t ItemModelsPerGroup = 512;
[[nodiscard]] constexpr std::uint16_t ItemModel(std::uint8_t group, std::uint16_t index)
{
    return static_cast<std::uint16_t>(FirstItemModel + group * ItemModelsPerGroup + index);
}

// The bones 0x60dbe0 writes at +0x54, +0x58 and +0x5c of the skeleton (0x60f301..0x60f32b): the head, and two that
// would be the feet (*inferred*).
inline constexpr std::int16_t PlayerHeadBone = 20;
inline constexpr std::array<std::int16_t, 2> PlayerFootBones = {6, 13};

// The helms 0x50ba30 draws with their mask in class 4: the model + 0x328b, 0x60dbe0's MaskHelmMale01, 06, 07, 09
// and 10.
inline constexpr std::uint16_t MaskHelmOffset = 0x328b;
inline constexpr std::array<std::uint16_t, 5> HelmsWithMask{0x1012, 0x1017, 0x1018, 0x101a, 0x101b};

// The 213 loads of 0x60dbe0 in the Data\ mode: Player.bmd, each class's parts, the equipment's and Shadow01.bmd. All
// are in the lab's Data\.
[[nodiscard]] std::span<const Data::Models::ModelFile> PlayerModelFiles();

// The 243 of 0x60ff50, each a different model: weapons, shields, items and Player\'s four pets (Helper01 to 04). The
// lab lacks only the three giftbox (09), which the original skips silently.
[[nodiscard]] std::span<const Data::Models::ModelFile> ItemModelFiles();

// The 106 loads of 0x615700 in the Data branch, in native order. ManyFlag is optional and may be missing.
[[nodiscard]] std::span<const Data::Models::ModelFile> SkillModelFiles();

struct ModelMesh
{
    std::uint16_t model = 0;
    std::uint16_t mesh = 0;

    friend bool operator==(const ModelMesh&, const ModelMesh&) = default;
};

// The five meshes whose byte +0 0x60ff50 turns on at the end (0x6117fd..0x611847), so 0x4ecc30 runs no shine passes
// on them: mesh 1 of Spear01, Sword11, Mace07 and MagicBox07 and mesh 2 of Staff07.
inline constexpr std::array<ModelMesh, 5> MeshesWithoutShine{{
    {0x812, 1},  // Spear01
    {0x21c, 1},  // Sword11
    {0xc18, 2},  // Staff07
    {0x618, 1},  // Mace07
    {0x2243, 1}, // MagicBox07
}};
} // namespace Client::Tables
