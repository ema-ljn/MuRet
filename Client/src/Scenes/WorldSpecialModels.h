#pragma once

#include "Data/Models/ModelFiles.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace Client::Scenes
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

// 05 § Los especiales de cada mapa, 0x617325 y los rangos que lo siguen. La carpeta de texturas
// solo difiere de la del BMD en Angel; Noria y Exile separan las cargas de las asignaciones.
struct WorldSpecialModelSet
{
    std::span<const Data::Models::ModelFile> files{};
    WorldSpecialKind kind = WorldSpecialKind::Ordinary;
    std::string_view textureFolder{};
};

[[nodiscard]] WorldSpecialModelSet SpecialWorldModelsOf(std::uint8_t map);
} // namespace Client::Scenes
