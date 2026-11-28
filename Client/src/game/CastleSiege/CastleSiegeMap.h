#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Terrain
{
struct WorldTerrain;
} // namespace Client::Terrain

// The siege state transition and its live files (0x4294c0).
namespace Client::CastleSiege
{

// In map 30, `terrain` (the current map's) gets the other attributes and light; without it, that stops.
[[nodiscard]] Core::Expected<void, std::string> SetTheSiegeActive(std::uint8_t value, Kernel::WorldState& world,
                                                                  Terrain::WorldTerrain* terrain,
                                                                  const std::filesystem::path& dataRoot);
} // namespace Client::CastleSiege
