#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <filesystem>
#include <string>

// La transición de estado del asedio y sus archivos vivos (0x4294c0).
namespace Client::Scenes
{
class WorldState;

[[nodiscard]] Core::Expected<void, std::string> SetTheSiegeActive(std::uint8_t value, WorldState& world,
                                                                  const std::filesystem::path& dataRoot);
} // namespace Client::Scenes
