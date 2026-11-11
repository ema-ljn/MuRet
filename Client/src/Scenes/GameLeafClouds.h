#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::World
{
struct Leaf;
}

// Creadores externos de 530490: 443990 (31) y 435600 (34/estado2).
namespace Client::Scenes
{
struct GameLeafContext;
[[nodiscard]] Core::Expected<void, std::string> SpawnGameLeafCloud(World::Leaf& leaf, GameLeafContext& context,
                                                                   bool crywolf);
} // namespace Client::Scenes
