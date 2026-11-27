#pragma once

#include "Data/Models/ModelFiles.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace Client::Assets
{
enum class WorldSpecialKind : std::uint8_t
{
    Ordinary,
    Noria,
    Exile,
    BloodCastle,
    ChaosCastle,
    Kalima,
};

// 05 § Los especiales de cada mapa, 0x617325 and the ranges after it. The texture folder only differs from the BMD's
// for Angel; Noria and Exile split the loads from the assignments.
struct WorldSpecialModelSet
{
    std::span<const Data::Models::ModelFile> files{};
    WorldSpecialKind kind = WorldSpecialKind::Ordinary;
    std::string_view textureFolder{};
};

[[nodiscard]] WorldSpecialModelSet SpecialWorldModelsOf(std::uint8_t map);
} // namespace Client::Assets
